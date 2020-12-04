// Copyright Epic Games, Inc. All Rights Reserved.


#include "BlueprintEditorModule.h"

#include "BlueprintDebugger.h"
#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "EditorUndoClient.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/UObjectHash.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "BlueprintEditor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Editor/LevelEditor/Public/LevelEditor.h"
#include "UserDefinedEnumEditor.h"
#include "MessageLogInitializationOptions.h"
#include "IMessageLogListing.h"
#include "Developer/MessageLog/Public/MessageLogModule.h"
#include "Misc/UObjectToken.h"
#include "InstancedStaticMeshSCSEditorCustomization.h"
#include "InstancedReferenceSubobjectHelper.h"
#include "ISettingsModule.h"
#include "UserDefinedStructureEditor.h"
#include "EdGraphUtilities.h"
#include "BlueprintGraphPanelPinFactory.h"
#include "WatchPointViewer.h"
#include "KismetCompiler.h"
#include "KismetWidgets.h"

#define LOCTEXT_NAMESPACE "BlueprintEditor"

IMPLEMENT_MODULE( FBlueprintEditorModule, Kismet );

//////////////////////////////////////////////////////////////////////////
// FBlueprintEditorModule

TSharedRef<FExtender> ExtendLevelViewportContextMenuForBlueprints(const TSharedRef<FUICommandList> CommandList, TArray<AActor*> SelectedActors);

FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors LevelViewportContextMenuBlueprintExtender;

static void FocusBlueprintEditorOnObject(const TSharedRef<IMessageToken>& Token)
{
	if( Token->GetType() == EMessageToken::Object )
	{
		const TSharedRef<FUObjectToken> UObjectToken = StaticCastSharedRef<FUObjectToken>(Token);
		if(UObjectToken->GetObject().IsValid())
		{
			FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(UObjectToken->GetObject().Get());
		}
	}
}

struct FBlueprintUndoRedoHandler : public FEditorUndoClient
{	
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
};
static FBlueprintUndoRedoHandler* UndoRedoHandler = nullptr;

void FixSubObjectReferencesPostUndoRedo(UObject* InObject)
{
	// Post undo/redo, these may have the correct Outer but are not referenced by the CDO's UProperties
	TArray<UObject*> SubObjects;
	GetObjectsWithOuter(InObject, SubObjects, false);

	// Post undo/redo, these may have the in-correct Outer but are incorrectly referenced by the CDO's UProperties
	TSet<FInstancedSubObjRef> PropertySubObjectReferences;
	UClass* ObjectClass = InObject->GetClass();
	FFindInstancedReferenceSubobjectHelper::GetInstancedSubObjects(InObject, PropertySubObjectReferences);

	TMap<UObject*, UObject*> OldToNewInstanceMap;
	for (UObject* PropertySubObject : PropertySubObjectReferences)
	{
		bool bFoundMatchingSubObject = false;
		for (UObject* SubObject : SubObjects)
		{
			// The property and sub-objects should have the same name.
			if (PropertySubObject->GetFName() == SubObject->GetFName())
			{
				// We found a matching property, we do not want to re-make the property
				bFoundMatchingSubObject = true;

				// Check if the properties have different outers so we can map old-to-new
				if (PropertySubObject->GetOuter() != InObject)
				{
					OldToNewInstanceMap.Add(PropertySubObject, SubObject);
				}
				// Recurse on the SubObject to correct any sub-object/property references
				FixSubObjectReferencesPostUndoRedo(SubObject);
				break;
			}
		}

		// If the property referenced does not exist in the current context as a subobject, we need to duplicate it and fix up references
		// This will occur during post-undo/redo of deletions
		if (!bFoundMatchingSubObject)
		{
			UObject* NewSubObject = DuplicateObject(PropertySubObject, InObject, PropertySubObject->GetFName());

			// Don't forget to fix up all references and sub-object references
			OldToNewInstanceMap.Add(PropertySubObject, NewSubObject);
		}
	}

	FArchiveReplaceObjectRef<UObject> Replacer(InObject, OldToNewInstanceMap, false, false, false, false);
}

void FixSubObjectReferencesPostUndoRedo(const FTransaction* Transaction)
{
	TArray<UBlueprint*> ModifiedBlueprints;

	// Look at the transaction this function is responding to, see if any object in it has an outermost of the Blueprint
	if (Transaction != nullptr)
	{
		TArray<UObject*> TransactionObjects;
		Transaction->GetTransactionObjects(TransactionObjects);
		for (UObject* Object : TransactionObjects)
		{
			UBlueprint* Blueprint = nullptr;

			while (Object != nullptr && Blueprint == nullptr)
			{
				Blueprint = Cast<UBlueprint>(Object);
				Object = Object->GetOuter();
			}

			if (Blueprint != nullptr)
			{
				if (Blueprint->ShouldBeMarkedDirtyUponTransaction())
				{
					ModifiedBlueprints.AddUnique(Blueprint);
				}
			}
		}
	}

	// Transaction affects the Blueprints this editor handles, so react as necessary
	for (UBlueprint* Blueprint : ModifiedBlueprints)
	{
		FixSubObjectReferencesPostUndoRedo(Blueprint->GeneratedClass->GetDefaultObject());
		// Will cause a call to RefreshEditors()
		if (Blueprint->ShouldBeMarkedDirtyUponTransaction())
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		}
		else
		{
			Blueprint->MarkPackageDirty();
		}
	}
}

void FBlueprintUndoRedoHandler::PostUndo(bool bSuccess)
{
	FixSubObjectReferencesPostUndoRedo(GEditor->Trans->GetTransaction(GEditor->Trans->GetQueueLength() - GEditor->Trans->GetUndoCount()));
}

void FBlueprintUndoRedoHandler::PostRedo(bool bSuccess)
{
	// Note: We add 1 to get the correct slot, because the transaction buffer will have decremented the UndoCount prior to getting here.
	if( GEditor->Trans->GetQueueLength() > 0 )
	{
		FixSubObjectReferencesPostUndoRedo(GEditor->Trans->GetTransaction(GEditor->Trans->GetQueueLength() - (GEditor->Trans->GetUndoCount() + 1)));
	}
}

void FBlueprintEditorModule::StartupModule()
{
	check(GEditor);

	delete UndoRedoHandler;
	UndoRedoHandler = new FBlueprintUndoRedoHandler();
	GEditor->RegisterForUndo(UndoRedoHandler);

	MenuExtensibilityManager = MakeShareable(new FExtensibilityManager);
	SharedBlueprintEditorCommands = MakeShareable(new FUICommandList);

	BlueprintDebugger = MakeUnique<FBlueprintDebugger>();

	// Have to check GIsEditor because right now editor modules can be loaded by the game
	// Once LoadModule is guaranteed to return NULL for editor modules in game, this can be removed
	// Without this check, loading the level editor in the game will crash
	if (GIsEditor)
	{
		// Extend the level viewport context menu to handle blueprints
		LevelViewportContextMenuBlueprintExtender = FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors::CreateStatic(&ExtendLevelViewportContextMenuForBlueprints);
		FLevelEditorModule& LevelEditorModule = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		auto& MenuExtenders = LevelEditorModule.GetAllLevelViewportContextMenuExtenders();
		MenuExtenders.Add(LevelViewportContextMenuBlueprintExtender);
		LevelViewportContextMenuBlueprintExtenderDelegateHandle = MenuExtenders.Last().GetHandle();

		FModuleManager::Get().LoadModuleChecked<FKismetWidgetsModule>("KismetWidgets");
	}

	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	FMessageLogInitializationOptions InitOptions;
	InitOptions.bShowFilters = true;
	InitOptions.bShowPages = true;
	MessageLogModule.RegisterLogListing("BlueprintLog", LOCTEXT("BlueprintLog", "Blueprint Log"), InitOptions);

	// Listen for clicks in log so we can focus on the object, might have to restart K2 if the K2 tab has been closed
	MessageLogModule.GetLogListing("BlueprintLog")->OnMessageTokenClicked().AddStatic( &FocusBlueprintEditorOnObject );
	
	// Also listen for clicks in the PIE log, runtime errors with Blueprints may post clickable links there
	MessageLogModule.GetLogListing("PIE")->OnMessageTokenClicked().AddStatic( &FocusBlueprintEditorOnObject );

	// Add a page for pre-loading of the editor
	MessageLogModule.GetLogListing("BlueprintLog")->NewPage(LOCTEXT("PreloadLogPageLabel", "Editor Load"));

	// Register internal SCS editor customizations
	RegisterSCSEditorCustomization("InstancedStaticMeshComponent", FSCSEditorCustomizationBuilder::CreateStatic(&FInstancedStaticMeshSCSEditorCustomization::MakeInstance));
	RegisterSCSEditorCustomization("HierarchicalInstancedStaticMeshComponent", FSCSEditorCustomizationBuilder::CreateStatic(&FInstancedStaticMeshSCSEditorCustomization::MakeInstance));

	TSharedPtr<FBlueprintGraphPanelPinFactory> BlueprintGraphPanelPinFactory = MakeShareable(new FBlueprintGraphPanelPinFactory());
	FEdGraphUtilities::RegisterVisualPinFactory(BlueprintGraphPanelPinFactory);

	PrepareAutoGeneratedDefaultEvents();

	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
	}
}

void FBlueprintEditorModule::ShutdownModule()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Engine", "Blueprints");
		ConfigurationPanel = TSharedPtr<SWidget>();
	}
	// we're intentionally leaking UndoRedoHandler because the GEditor may be garbage when ShutdownModule is called:

	// Cleanup all information for auto generated default event nodes by this module
	FKismetEditorUtilities::UnregisterAutoBlueprintNodeCreation(this);

	SharedBlueprintEditorCommands.Reset();
	MenuExtensibilityManager.Reset();

	// Remove level viewport context menu extenders
	if ( FModuleManager::Get().IsModuleLoaded( "LevelEditor" ) )
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>( "LevelEditor" );
		LevelEditorModule.GetAllLevelViewportContextMenuExtenders().RemoveAll([&](const FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors& Delegate) {
			return Delegate.GetHandle() == LevelViewportContextMenuBlueprintExtenderDelegateHandle;
		});
	}

	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	MessageLogModule.UnregisterLogListing("BlueprintLog");

	// Unregister internal SCS editor customizations
	UnregisterSCSEditorCustomization("InstancedStaticMeshComponent");

	UEdGraphPin::ShutdownVerification();
}

TSharedRef<IBlueprintEditor> FBlueprintEditorModule::CreateBlueprintEditor(const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, UBlueprint* Blueprint, bool bShouldOpenInDefaultsMode)
{
	TArray<UBlueprint*> BlueprintsToEdit = { Blueprint };
	return CreateBlueprintEditor(Mode, InitToolkitHost, BlueprintsToEdit, bShouldOpenInDefaultsMode);
}

TSharedRef<IBlueprintEditor> FBlueprintEditorModule::CreateBlueprintEditor(const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, const TArray< UBlueprint* >& BlueprintsToEdit, bool bShouldOpenInDefaultsMode)
{
	TSharedRef< FBlueprintEditor > NewBlueprintEditor( new FBlueprintEditor() );

	NewBlueprintEditor->InitBlueprintEditor(Mode, InitToolkitHost, BlueprintsToEdit, bShouldOpenInDefaultsMode);

	NewBlueprintEditor->SetDetailsCustomization(DetailsObjectFilter, DetailsRootCustomization);
	NewBlueprintEditor->SetSCSEditorUICustomization(SCSEditorUICustomization);

	for(auto It(SCSEditorCustomizations.CreateConstIterator()); It; ++It)
	{
		NewBlueprintEditor->RegisterSCSEditorCustomization(It->Key, It->Value.Execute(NewBlueprintEditor));
	}

	for (UBlueprint* Blueprint : BlueprintsToEdit)
	{
		WatchViewer::UpdateWatchListFromBlueprint(Blueprint);
	}

	EBlueprintType const BPType = ( (BlueprintsToEdit.Num() > 0) && (BlueprintsToEdit[0] != NULL) ) 
		? (EBlueprintType) BlueprintsToEdit[0]->BlueprintType
		: BPTYPE_Normal;

	BlueprintEditorOpened.Broadcast(BPType);

	BlueprintEditors.Add(NewBlueprintEditor);

	return NewBlueprintEditor;
}

TArray<TSharedRef<IBlueprintEditor>> FBlueprintEditorModule::GetBlueprintEditors() const
{
	TArray<TSharedRef<IBlueprintEditor>> ValidBlueprintEditors;
	ValidBlueprintEditors.Reserve(BlueprintEditors.Num());

	for (TWeakPtr<FBlueprintEditor> BlueprintEditor : BlueprintEditors)
	{
		if (TSharedPtr<FBlueprintEditor> BlueprintEditorPinned = BlueprintEditor.Pin())
		{
			ValidBlueprintEditors.Add(BlueprintEditorPinned.ToSharedRef());
		}
	}

	if (BlueprintEditors.Num() > ValidBlueprintEditors.Num())
	{
		TArray<TWeakPtr<FBlueprintEditor>>& BlueprintEditorsNonConst = const_cast<TArray<TWeakPtr<FBlueprintEditor>>&>(BlueprintEditors);
		BlueprintEditorsNonConst.Reset(ValidBlueprintEditors.Num());
		for (const TSharedRef<IBlueprintEditor>& ValidBlueprintEditor : ValidBlueprintEditors)
		{
			BlueprintEditorsNonConst.Add(StaticCastSharedRef<FBlueprintEditor>(ValidBlueprintEditor));
		}
	}

	return ValidBlueprintEditors;
}

TSharedRef<IUserDefinedEnumEditor> FBlueprintEditorModule::CreateUserDefinedEnumEditor(const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, UUserDefinedEnum* UDEnum)
{
	TSharedRef<FUserDefinedEnumEditor> UserDefinedEnumEditor(new FUserDefinedEnumEditor());
	UserDefinedEnumEditor->InitEditor(Mode, InitToolkitHost, UDEnum);
	return UserDefinedEnumEditor;
}

TSharedRef<IUserDefinedStructureEditor> FBlueprintEditorModule::CreateUserDefinedStructEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UUserDefinedStruct* UDStruct)
{
	TSharedRef<FUserDefinedStructureEditor> UserDefinedStructureEditor(new FUserDefinedStructureEditor());
	UserDefinedStructureEditor->InitEditor(Mode, InitToolkitHost, UDStruct);
	return UserDefinedStructureEditor;
}

void FBlueprintEditorModule::SetDetailsCustomization(TSharedPtr<FDetailsViewObjectFilter> InDetailsObjectFilter, TSharedPtr<IDetailRootObjectCustomization> InDetailsRootCustomization)
{
	DetailsObjectFilter = InDetailsObjectFilter;
	DetailsRootCustomization = InDetailsRootCustomization;

	for (const TSharedRef<IBlueprintEditor>& BlueprintEditor : GetBlueprintEditors())
	{
		StaticCastSharedRef<FBlueprintEditor>(BlueprintEditor)->SetDetailsCustomization(DetailsObjectFilter, DetailsRootCustomization);
	}
}

void FBlueprintEditorModule::SetSCSEditorUICustomization(TSharedPtr<ISCSEditorUICustomization> InSCSEditorUICustomization)
{
	SCSEditorUICustomization = InSCSEditorUICustomization;

	for (const TSharedRef<IBlueprintEditor>& BlueprintEditor : GetBlueprintEditors())
	{
		StaticCastSharedRef<FBlueprintEditor>(BlueprintEditor)->SetSCSEditorUICustomization(SCSEditorUICustomization);
	}
}

void FBlueprintEditorModule::RegisterSCSEditorCustomization(const FName& InComponentName, FSCSEditorCustomizationBuilder InCustomizationBuilder)
{
	SCSEditorCustomizations.Add(InComponentName, InCustomizationBuilder);
}

void FBlueprintEditorModule::UnregisterSCSEditorCustomization(const FName& InComponentName)
{
	SCSEditorCustomizations.Remove(InComponentName);
}

void FBlueprintEditorModule::RegisterVariableCustomization(FFieldClass* InFieldClass, FOnGetVariableCustomizationInstance InOnGetVariableCustomization)
{
	VariableCustomizations.Add(InFieldClass, InOnGetVariableCustomization);
}

void FBlueprintEditorModule::UnregisterVariableCustomization(FFieldClass* InFieldClass)
{
	VariableCustomizations.Remove(InFieldClass);
}

void FBlueprintEditorModule::RegisterGraphCustomization(const UEdGraphSchema* InGraphSchema, FOnGetGraphCustomizationInstance InOnGetGraphCustomization)
{
	GraphCustomizations.Add(InGraphSchema, InOnGetGraphCustomization);
}

void FBlueprintEditorModule::UnregisterGraphCustomization(const UEdGraphSchema* InGraphSchema)
{
	GraphCustomizations.Remove(InGraphSchema);
}

TArray<TSharedPtr<IDetailCustomization>> FBlueprintEditorModule::CustomizeVariable(FFieldClass* InFieldClass, TSharedPtr<IBlueprintEditor> InBlueprintEditor)
{
	TArray<TSharedPtr<IDetailCustomization>> DetailsCustomizations;
	TArray<FFieldClass*> ParentClassesToQuery;
	if (InFieldClass)
	{
		ParentClassesToQuery.Add(InFieldClass);

		FFieldClass* ParentClass = InFieldClass->GetSuperClass();
		while (ParentClass)
		{
			ParentClassesToQuery.Add(ParentClass);
			ParentClass = ParentClass->GetSuperClass();
		}

		for (FFieldClass* ClassToQuery : ParentClassesToQuery)
		{
			FOnGetVariableCustomizationInstance* CustomizationDelegate = VariableCustomizations.Find(ClassToQuery);
			if (CustomizationDelegate && CustomizationDelegate->IsBound())
			{
				TSharedPtr<IDetailCustomization> Customization = CustomizationDelegate->Execute(InBlueprintEditor);
				if(Customization.IsValid())
				{ 
					DetailsCustomizations.Add(Customization);
				}
			}
		}
	}

	return DetailsCustomizations;
}

TArray<TSharedPtr<IDetailCustomization>> FBlueprintEditorModule::CustomizeGraph(const UEdGraphSchema* InGraphSchema, TSharedPtr<IBlueprintEditor> InBlueprintEditor)
{
	TArray<TSharedPtr<IDetailCustomization>> DetailsCustomizations;
	TArray<UClass*> ParentSchemaClassesToQuery;
	if (InGraphSchema)
	{
		UClass* GraphSchemaClass = InGraphSchema->GetClass();
		ParentSchemaClassesToQuery.Add(InGraphSchema->GetClass());

		UClass* ParentSchemaClass = GraphSchemaClass->GetSuperClass();
		while (ParentSchemaClass && ParentSchemaClass->IsChildOf(UEdGraphSchema::StaticClass()))
		{
			ParentSchemaClassesToQuery.Add(ParentSchemaClass);
			ParentSchemaClass = ParentSchemaClass->GetSuperClass();
		}

		for (UClass* ClassToQuery : ParentSchemaClassesToQuery)
		{
			UEdGraphSchema* SchemaToQuery = CastChecked<UEdGraphSchema>(ClassToQuery->GetDefaultObject());
			FOnGetGraphCustomizationInstance* CustomizationDelegate = GraphCustomizations.Find(SchemaToQuery);
			if (CustomizationDelegate && CustomizationDelegate->IsBound())
			{
				TSharedPtr<IDetailCustomization> Customization = CustomizationDelegate->Execute(InBlueprintEditor);
				if(Customization.IsValid())
				{ 
					DetailsCustomizations.Add(Customization);
				}
			}
		}
	}

	return DetailsCustomizations;
}

void FBlueprintEditorModule::PrepareAutoGeneratedDefaultEvents()
{
	// Load up all default events that should be spawned for Blueprints that are children of specific classes
	const FString ConfigSection = TEXT("DefaultEventNodes");
	const FString SettingName = TEXT("Node");
	TArray< FString > NodeSpawns;
	GConfig->GetArray(*ConfigSection, *SettingName, NodeSpawns, GEditorPerProjectIni);

	for(FString CurrentNodeSpawn : NodeSpawns)
	{
		FString TargetClassName;
		if(!FParse::Value(*CurrentNodeSpawn, TEXT("TargetClass="), TargetClassName))
		{
			// Could not find a class name, cannot continue with this line
			continue;
		}

		UClass* FoundTargetClass = FindObject<UClass>(ANY_PACKAGE, *TargetClassName, true);
		if(FoundTargetClass)
		{
			FString TargetEventFunction;
			if(!FParse::Value(*CurrentNodeSpawn, TEXT("TargetEvent="), TargetEventFunction))
			{
				// Could not find a class name, cannot continue with this line
				continue;
			}

			FName TargetEventFunctionName(*TargetEventFunction);
			if ( FoundTargetClass->FindFunctionByName(TargetEventFunctionName) )
			{
				FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, FoundTargetClass, FName(*TargetEventFunction));
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
