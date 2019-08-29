// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "WidgetBlueprintCompiler.h"
#include "Components/SlateWrapperTypes.h"
#include "Blueprint/UserWidget.h"

#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_VariableGet.h"
#include "Blueprint/WidgetTree.h"
#include "Animation/WidgetAnimation.h"
#include "MovieScene.h"

#include "Kismet2/Kismet2NameValidators.h"
#include "Kismet2/KismetReinstanceUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Components/NamedSlot.h"
#include "WidgetBlueprintEditorUtils.h"
#include "WidgetGraphSchema.h"
#include "IUMGModule.h"
#include "UMGEditorProjectSettings.h"

#define LOCTEXT_NAMESPACE "UMG"

#define CPF_Instanced (CPF_PersistentInstance | CPF_ExportObject | CPF_InstancedReference)

extern COREUOBJECT_API bool GMinimalCompileOnLoad;

FWidgetBlueprintCompiler::FWidgetBlueprintCompiler(UWidgetBlueprint* SourceSketch, FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompilerOptions, TArray<UObject*>* InObjLoaded)
	: Super(SourceSketch, InMessageLog, InCompilerOptions, InObjLoaded)
	, NewWidgetBlueprintClass(nullptr)
	, WidgetSchema(nullptr)
{
}

FWidgetBlueprintCompiler::~FWidgetBlueprintCompiler()
{
}

UEdGraphSchema_K2* FWidgetBlueprintCompiler::CreateSchema()
{
	WidgetSchema = NewObject<UWidgetGraphSchema>();
	return WidgetSchema;
}

void FWidgetBlueprintCompiler::CreateFunctionList()
{
	Super::CreateFunctionList();

	for ( FDelegateEditorBinding& EditorBinding : WidgetBlueprint()->Bindings )
	{
		if ( EditorBinding.SourcePath.IsEmpty() )
		{
			const FName PropertyName = EditorBinding.SourceProperty;

			UProperty* Property = FindField<UProperty>(Blueprint->SkeletonGeneratedClass, PropertyName);
			if ( Property )
			{
				// Create the function graph.
				FString FunctionName = FString(TEXT("__Get")) + PropertyName.ToString();
				UEdGraph* FunctionGraph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, FunctionName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());

				// Update the function binding to match the generated graph name
				EditorBinding.FunctionName = FunctionGraph->GetFName();

				const UEdGraphSchema_K2* K2Schema = Cast<const UEdGraphSchema_K2>(FunctionGraph->GetSchema());

				Schema->CreateDefaultNodesForGraph(*FunctionGraph);

				K2Schema->MarkFunctionEntryAsEditable(FunctionGraph, true);

				// Create a function entry node
				FGraphNodeCreator<UK2Node_FunctionEntry> FunctionEntryCreator(*FunctionGraph);
				UK2Node_FunctionEntry* EntryNode = FunctionEntryCreator.CreateNode();
				EntryNode->FunctionReference.SetSelfMember(FunctionGraph->GetFName());
				FunctionEntryCreator.Finalize();

				FGraphNodeCreator<UK2Node_FunctionResult> FunctionReturnCreator(*FunctionGraph);
				UK2Node_FunctionResult* ReturnNode = FunctionReturnCreator.CreateNode();
				ReturnNode->FunctionReference.SetSelfMember(FunctionGraph->GetFName());
				ReturnNode->NodePosX = EntryNode->NodePosX + EntryNode->NodeWidth + 256;
				ReturnNode->NodePosY = EntryNode->NodePosY;
				FunctionReturnCreator.Finalize();

				FEdGraphPinType PinType;
				K2Schema->ConvertPropertyToPinType(Property, /*out*/ PinType);

				UEdGraphPin* ReturnPin = ReturnNode->CreateUserDefinedPin(TEXT("ReturnValue"), PinType, EGPD_Input);

				// Auto-connect the pins for entry and exit, so that by default the signature is properly generated
				UEdGraphPin* EntryNodeExec = K2Schema->FindExecutionPin(*EntryNode, EGPD_Output);
				UEdGraphPin* ResultNodeExec = K2Schema->FindExecutionPin(*ReturnNode, EGPD_Input);
				EntryNodeExec->MakeLinkTo(ResultNodeExec);

				FGraphNodeCreator<UK2Node_VariableGet> MemberGetCreator(*FunctionGraph);
				UK2Node_VariableGet* VarNode = MemberGetCreator.CreateNode();
				VarNode->VariableReference.SetSelfMember(PropertyName);
				MemberGetCreator.Finalize();

				ReturnPin->MakeLinkTo(VarNode->GetValuePin());

				// We need to flag the entry node to make sure that the compiled function is callable from Kismet2
				int32 ExtraFunctionFlags = ( FUNC_Private | FUNC_Const );
				K2Schema->AddExtraFunctionFlags(FunctionGraph, ExtraFunctionFlags);

				//Blueprint->FunctionGraphs.Add(FunctionGraph);

				ProcessOneFunctionGraph(FunctionGraph, true);
				//FEdGraphUtilities::MergeChildrenGraphsIn(Ubergraph, FunctionGraph, /*bRequireSchemaMatch=*/ true);
			}
		}
	}
}

void FWidgetBlueprintCompiler::ValidateWidgetNames()
{
	UWidgetBlueprint* WidgetBP = WidgetBlueprint();

	TSharedPtr<FKismetNameValidator> ParentBPNameValidator;
	if ( WidgetBP->ParentClass != nullptr )
	{
		UBlueprint* ParentBP = Cast<UBlueprint>(WidgetBP->ParentClass->ClassGeneratedBy);
		if ( ParentBP != nullptr )
		{
			ParentBPNameValidator = MakeShareable(new FKismetNameValidator(ParentBP));
		}
	}
}

template<typename TOBJ>
struct FCullTemplateObjectsHelper
{
	const TArray<TOBJ*>& Templates;

	FCullTemplateObjectsHelper(const TArray<TOBJ*>& InComponentTemplates)
		: Templates(InComponentTemplates)
	{}

	bool operator()(const UObject* const RemovalCandidate) const
	{
		return ( NULL != Templates.FindByKey(RemovalCandidate) );
	}
};


void FWidgetBlueprintCompiler::CleanAndSanitizeClass(UBlueprintGeneratedClass* ClassToClean, UObject*& InOutOldCDO)
{
	UWidgetBlueprint* WidgetBP = WidgetBlueprint();

	const bool bRecompilingOnLoad = Blueprint->bIsRegeneratingOnLoad;
	const ERenameFlags RenFlags = REN_DontCreateRedirectors | (bRecompilingOnLoad ? REN_ForceNoResetLoaders : 0) | REN_NonTransactional | REN_DoNotDirty;

	if ( !Blueprint->bIsRegeneratingOnLoad && bIsFullCompile )
	{
		UPackage* WidgetTemplatePackage = WidgetBP->GetWidgetTemplatePackage();
		UUserWidget* OldArchetype = FindObjectFast<UUserWidget>(WidgetTemplatePackage, TEXT("WidgetArchetype"));
		if (OldArchetype)
		{
			FString TransientArchetypeString = FString::Printf(TEXT("OLD_TEMPLATE_%s"), *OldArchetype->GetName());
			FName TransientArchetypeName = MakeUniqueObjectName(GetTransientPackage(), OldArchetype->GetClass(), FName(*TransientArchetypeString));
			OldArchetype->Rename(*TransientArchetypeName.ToString(), GetTransientPackage(), RenFlags);
			OldArchetype->SetFlags(RF_Transient);
			OldArchetype->ClearFlags(RF_Public | RF_Standalone | RF_ArchetypeObject);
			FLinkerLoad::InvalidateExport(OldArchetype);

			TArray<UObject*> Children;
			ForEachObjectWithOuter(OldArchetype, [&Children] (UObject* Child) {
				Children.Add(Child);
			}, false);

			for ( UObject* Child : Children )
			{
				Child->Rename(nullptr, GetTransientPackage(), RenFlags);
				Child->SetFlags(RF_Transient);
				FLinkerLoad::InvalidateExport(Child);
			}
		}
	}

	Super::CleanAndSanitizeClass(ClassToClean, InOutOldCDO);

	// Make sure our typed pointer is set
	check(ClassToClean == NewClass && NewWidgetBlueprintClass == NewClass);

	for (UWidgetAnimation* Animation : NewWidgetBlueprintClass->Animations)
	{
		Animation->Rename(nullptr, GetTransientPackage(), RenFlags);
	}
	NewWidgetBlueprintClass->Animations.Empty();

	NewWidgetBlueprintClass->Bindings.Empty();
}

void FWidgetBlueprintCompiler::SaveSubObjectsFromCleanAndSanitizeClass(FSubobjectCollection& SubObjectsToSave, UBlueprintGeneratedClass* ClassToClean)
{
	Super::SaveSubObjectsFromCleanAndSanitizeClass(SubObjectsToSave, ClassToClean);

	// Make sure our typed pointer is set
	check(ClassToClean == NewClass);
	NewWidgetBlueprintClass = CastChecked<UWidgetBlueprintGeneratedClass>((UObject*)NewClass);

	UWidgetBlueprint* WidgetBP = WidgetBlueprint();

	// We need to save the widget tree to survive the initial sub-object clean blitz, 
	// otherwise they all get renamed, and it causes early loading errors.
	SubObjectsToSave.AddObject(WidgetBP->WidgetTree);
}

void FWidgetBlueprintCompiler::CreateClassVariablesFromBlueprint()
{
	Super::CreateClassVariablesFromBlueprint();

	UWidgetBlueprint* WidgetBP = WidgetBlueprint();
	UClass* ParentClass = WidgetBP->ParentClass;

	ValidateWidgetNames();

	// Build the set of variables based on the variable widgets in the widget tree.
	TArray<UWidget*> Widgets = WidgetBP->GetAllSourceWidgets();

	// Sort the widgets alphabetically
	Widgets.Sort( []( const UWidget& Lhs, const UWidget& Rhs ) { return Rhs.GetFName() < Lhs.GetFName(); } );

	// Add widget variables
	for ( UWidget* Widget : Widgets )
	{
		bool bIsVariable = Widget->bIsVariable;

		// In the event there are bindings for a widget, but it's not marked as a variable, make it one, but hide it from the UI.
		// we do this so we can use FindField to locate it at runtime.
		bIsVariable |= WidgetBP->Bindings.ContainsByPredicate([&Widget] (const FDelegateEditorBinding& Binding) {
			return Binding.ObjectName == Widget->GetName();
		});

		// All UNamedSlot widgets are automatically variables, so that we can properly look them up quickly with FindField
		// in UserWidgets.
		bIsVariable |= Widget->IsA<UNamedSlot>();

		// This code was added to fix the problem of recompiling dependent widgets, not using the newest
		// class thus resulting in REINST failures in dependent blueprints.
		UClass* WidgetClass = Widget->GetClass();
		if ( UBlueprintGeneratedClass* BPWidgetClass = Cast<UBlueprintGeneratedClass>(WidgetClass) )
		{
			WidgetClass = BPWidgetClass->GetAuthoritativeClass();
		}

		UObjectPropertyBase* ExistingProperty = Cast<UObjectPropertyBase>(ParentClass->FindPropertyByName(Widget->GetFName()));
		if (ExistingProperty && 
			FWidgetBlueprintEditorUtils::IsBindWidgetProperty(ExistingProperty) && 
			Widget->IsA(ExistingProperty->PropertyClass))
		{
			WidgetToMemberVariableMap.Add(Widget, ExistingProperty);
			continue;
		}

		// Skip non-variable widgets
		if ( !bIsVariable )
		{
			continue;
		}

		FEdGraphPinType WidgetPinType(UEdGraphSchema_K2::PC_Object, NAME_None, WidgetClass, EPinContainerType::None, false, FEdGraphTerminalType());
		
		// Always name the variable according to the underlying FName of the widget object
		UProperty* WidgetProperty = CreateVariable(Widget->GetFName(), WidgetPinType);
		if ( WidgetProperty != nullptr )
		{
			const FString DisplayName = Widget->IsGeneratedName() ? Widget->GetName() : Widget->GetLabelText().ToString();
			WidgetProperty->SetMetaData(TEXT("DisplayName"), *DisplayName);
			
			// Only show variables if they're explicitly marked as variables.
			if ( Widget->bIsVariable )
			{
				WidgetProperty->SetPropertyFlags(CPF_BlueprintVisible);

				// Only include Category metadata for variables (i.e. a visible/editable property); otherwise, UHT will raise a warning if this Blueprint is nativized.
				WidgetProperty->SetMetaData(TEXT("Category"), *WidgetBP->GetName());
			}

			WidgetProperty->SetPropertyFlags(CPF_Instanced);
			WidgetProperty->SetPropertyFlags(CPF_RepSkip);

			WidgetToMemberVariableMap.Add(Widget, WidgetProperty);
		}
	}

	// Add movie scenes variables here
	for(UWidgetAnimation* Animation : WidgetBP->Animations)
	{
		FEdGraphPinType WidgetPinType(UEdGraphSchema_K2::PC_Object, NAME_None, Animation->GetClass(), EPinContainerType::None, true, FEdGraphTerminalType());
		UProperty* AnimationProperty = CreateVariable(Animation->GetFName(), WidgetPinType);

		if ( AnimationProperty != nullptr )
		{
			AnimationProperty->SetMetaData(TEXT("Category"), TEXT("Animations"));

			AnimationProperty->SetPropertyFlags(CPF_Instanced);
			AnimationProperty->SetPropertyFlags(CPF_BlueprintVisible);
			AnimationProperty->SetPropertyFlags(CPF_BlueprintReadOnly);
			AnimationProperty->SetPropertyFlags(CPF_RepSkip);
		}
	}
}

void FWidgetBlueprintCompiler::CopyTermDefaultsToDefaultObject(UObject* DefaultObject)
{
	FKismetCompilerContext::CopyTermDefaultsToDefaultObject(DefaultObject);

	UWidgetBlueprint* WidgetBP = WidgetBlueprint();

	UUserWidget* DefaultWidget = CastChecked<UUserWidget>(DefaultObject);
	UWidgetBlueprintGeneratedClass* WidgetClass = CastChecked<UWidgetBlueprintGeneratedClass>(DefaultObject->GetClass());

	if ( DefaultWidget )
	{
		//TODO Once we handle multiple derived blueprint classes, we need to check parent versions of the class.
		const UFunction* ReceiveTickEvent = FKismetCompilerUtilities::FindOverriddenImplementableEvent(GET_FUNCTION_NAME_CHECKED(UUserWidget, Tick), NewWidgetBlueprintClass);
		if (ReceiveTickEvent)
		{
			DefaultWidget->bHasScriptImplementedTick = true;
		}
		else
		{
			DefaultWidget->bHasScriptImplementedTick = false;
		}

		//TODO Once we handle multiple derived blueprint classes, we need to check parent versions of the class.
		if ( const UFunction* ReceivePaintEvent = FKismetCompilerUtilities::FindOverriddenImplementableEvent(GET_FUNCTION_NAME_CHECKED(UUserWidget, OnPaint), NewWidgetBlueprintClass) )
		{
			DefaultWidget->bHasScriptImplementedPaint = true;
		}
		else
		{
			DefaultWidget->bHasScriptImplementedPaint = false;
		}

	}


	bool bClassOrParentsHaveLatentActions = false;
	bool bClassOrParentsHaveAnimations = false;
	bool bClassRequiresNativeTick = false;


	WidgetBP->UpdateTickabilityStats(bClassOrParentsHaveLatentActions, bClassOrParentsHaveAnimations, bClassRequiresNativeTick);
	WidgetClass->SetClassRequiresNativeTick(bClassRequiresNativeTick);

	// If the widget is not tickable, warn the user that widgets with animations or implemented ticks will most likely not work
	if (DefaultWidget->GetDesiredTickFrequency() == EWidgetTickFrequency::Never)
	{
		if (bClassOrParentsHaveAnimations)
		{
			MessageLog.Warning(*LOCTEXT("NonTickableButAnimationsFound", "This widget has animations but the widget is set to never tick.  These animations will not function correctly.").ToString());
		}

		if (bClassOrParentsHaveLatentActions)
		{
			MessageLog.Warning(*LOCTEXT("NonTickableButLatentActionsFound", "This widget has latent actions but the widget is set to never tick.  These latent actions will not function correctly.").ToString());
		}

		if (bClassRequiresNativeTick)
		{
			MessageLog.Warning(*LOCTEXT("NonTickableButNativeTickFound", "This widget may require a native tick but the widget is set to never tick.  Native tick will not be called.").ToString());
		}

		if (DefaultWidget->bHasScriptImplementedTick)
		{
			MessageLog.Warning(*LOCTEXT("NonTickableButTickFound", "This widget has a blueprint implemented Tick event but the widget is set to never tick.  This tick event will never be called.").ToString());
		}
	}


}

bool FWidgetBlueprintCompiler::CanAllowTemplate(FCompilerResultsLog& MessageLog, UWidgetBlueprintGeneratedClass* InClass)
{
	if ( InClass == nullptr )
	{
		MessageLog.Error(*LOCTEXT("NoWidgetClass", "No Widget Class Found.").ToString());

		return false;
	}

	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(InClass->ClassGeneratedBy);

	if ( WidgetBP == nullptr )
	{
		MessageLog.Error(*LOCTEXT("NoWidgetBlueprint", "No Widget Blueprint Found.").ToString());

		return false;
	}

	// If this widget forces the slow construction path, we can't template it.
	if ( WidgetBP->bForceSlowConstructionPath )
	{
		if (GetDefault<UUMGEditorProjectSettings>()->bCookSlowConstructionWidgetTree)
		{
			MessageLog.Note(*LOCTEXT("ForceSlowConstruction", "Fast Templating Disabled By User.").ToString());
			return false;
		}
		else
		{
			MessageLog.Error(*LOCTEXT("UnableToForceSlowConstruction", "This project has [Cook Slow Construction Widget Tree] disabled, so [Force Slow Construction Path] is no longer allowed.").ToString());
		}
	}

	// For now we don't support nativization, it's going to require some extra work moving the template support
	// during the nativization process.
	if ( WidgetBP->NativizationFlag != EBlueprintNativizationFlag::Disabled )
	{
		MessageLog.Warning(*LOCTEXT("TemplatingAndNativization", "Nativization and Fast Widget Creation is not supported at this time.").ToString());

		return false;
	}

	if (WidgetBP->bGenerateAbstractClass)
	{
		return false;
	}

	return true;
}

bool FWidgetBlueprintCompiler::CanTemplateWidget(FCompilerResultsLog& MessageLog, UUserWidget* ThisWidget, TArray<FText>& OutErrors)
{
	UWidgetBlueprintGeneratedClass* WidgetClass = Cast<UWidgetBlueprintGeneratedClass>(ThisWidget->GetClass());
	if ( WidgetClass == nullptr )
	{
		MessageLog.Error(*LOCTEXT("NoWidgetClass", "No Widget Class Found.").ToString());

		return false;
	}

	if ( WidgetClass->bAllowTemplate == false )
	{
		MessageLog.Warning(*LOCTEXT("ClassDoesNotAllowTemplating", "This widget class is not allowed to be templated.").ToString());
		return false;
	}

	return ThisWidget->VerifyTemplateIntegrity(OutErrors);
}

void FWidgetBlueprintCompiler::FinishCompilingClass(UClass* Class)
{
	UWidgetBlueprint* WidgetBP = WidgetBlueprint();
	UWidgetBlueprintGeneratedClass* BPGClass = CastChecked<UWidgetBlueprintGeneratedClass>(Class);

	// Don't do a bunch of extra work on the skeleton generated class
	if ( WidgetBP->SkeletonGeneratedClass != Class )
	{
		if( !WidgetBP->bHasBeenRegenerated )
		{
			UBlueprint::ForceLoadMembers(WidgetBP->WidgetTree);
		}

		BPGClass->bCookSlowConstructionWidgetTree = GetDefault<UUMGEditorProjectSettings>()->bCookSlowConstructionWidgetTree;

		BPGClass->WidgetTree = Cast<UWidgetTree>(StaticDuplicateObject(WidgetBP->WidgetTree, BPGClass, NAME_None, RF_AllFlags & ~RF_DefaultSubObject));

		for ( const UWidgetAnimation* Animation : WidgetBP->Animations )
		{
			UWidgetAnimation* ClonedAnimation = DuplicateObject<UWidgetAnimation>(Animation, BPGClass, *( Animation->GetName() + TEXT("_INST") ));
			//ClonedAnimation->SetFlags(RF_Public); // Needs to be marked public so that it can be referenced from widget instances.
			//if (ClonedAnimation->MovieScene)
			//{
			//	ClonedAnimation->MovieScene->SetFlags(RF_Public); // Needs to be marked public so that it can be referenced from widget instances.
			//}

			BPGClass->Animations.Add(ClonedAnimation);
		}

		// Only check bindings on a full compile.  Also don't check them if we're regenerating on load,
		// that has a nasty tendency to fail because the other dependent classes that may also be blueprints
		// might not be loaded yet.
		const bool bIsLoading = WidgetBP->bIsRegeneratingOnLoad;
		if ( bIsFullCompile )
		{
			// Convert all editor time property bindings into a list of bindings
			// that will be applied at runtime.  Ensure all bindings are still valid.
			for ( const FDelegateEditorBinding& EditorBinding : WidgetBP->Bindings )
			{
				if ( bIsLoading || EditorBinding.IsBindingValid(Class, WidgetBP, MessageLog) )
				{
					BPGClass->Bindings.Add(EditorBinding.ToRuntimeBinding(WidgetBP));
				}
			}
		}

		// Add all the names of the named slot widgets to the slot names structure.
		BPGClass->NamedSlots.Reset();
		WidgetBP->ForEachSourceWidget([&] (UWidget* Widget) {
			if ( Widget && Widget->IsA<UNamedSlot>() )
			{
				BPGClass->NamedSlots.Add(Widget->GetFName());
			}
		});

		// Make sure that we don't have dueling widget hierarchies
		if (UWidgetBlueprintGeneratedClass* SuperBPGClass = Cast<UWidgetBlueprintGeneratedClass>(BPGClass->GetSuperClass()))
		{
			UWidgetBlueprint* SuperBlueprint = Cast<UWidgetBlueprint>(SuperBPGClass->ClassGeneratedBy);
			if (SuperBlueprint->WidgetTree != nullptr)
			{
				if ((SuperBlueprint->WidgetTree->RootWidget != nullptr) && (WidgetBlueprint()->WidgetTree->RootWidget != nullptr))
				{
					// We both have a widget tree, terrible things will ensue
					// @todo: nickd - we need to switch this back to a warning in engine, but note for games
					MessageLog.Note(*LOCTEXT("ParentAndChildBothHaveWidgetTrees", "This widget @@ and parent class widget @@ both have a widget hierarchy, which is not supported.  Only one of them should have a widget tree.").ToString(),
						WidgetBP, SuperBPGClass->ClassGeneratedBy);
				}
			}
		}

		// Check that all BindWidget properties are present and of the appropriate type
		UClass* ParentClass = WidgetBP->ParentClass;
		for (TUObjectPropertyBase<UWidget*>* WidgetProperty : TFieldRange<TUObjectPropertyBase<UWidget*>>(ParentClass))
		{
			bool bIsOptional = false;

			if (FWidgetBlueprintEditorUtils::IsBindWidgetProperty(WidgetProperty, bIsOptional))
			{
				const FText OptionalBindingAvailableNote = LOCTEXT("OptionalWidgetNotBound", "An optional widget binding @@ of type @@ is available.");
				const FText RequiredWidgetNotBoundError = LOCTEXT("RequiredWidgetNotBound", "A required widget binding @@ of type @@ was not found.");
				const FText IncorrectWidgetTypeError = LOCTEXT("IncorrectWidgetTypes", "The widget @@ is of type @@, but the bind widget property is of type @@.");

				UWidget* const* Widget = WidgetToMemberVariableMap.FindKey(WidgetProperty);
				if (!Widget)
				{
					if (bIsOptional)
					{
						MessageLog.Note(*OptionalBindingAvailableNote.ToString(), WidgetProperty, WidgetProperty->PropertyClass);
					}
					else if (Blueprint->bIsNewlyCreated)
					{
						MessageLog.Warning(*RequiredWidgetNotBoundError.ToString(), WidgetProperty, WidgetProperty->PropertyClass);
					}
					else
					{
						MessageLog.Error(*RequiredWidgetNotBoundError.ToString(), WidgetProperty, WidgetProperty->PropertyClass);
					}
				}
				else if (!(*Widget)->IsA(WidgetProperty->PropertyClass))
				{
					if (Blueprint->bIsNewlyCreated)
					{
						MessageLog.Warning(*IncorrectWidgetTypeError.ToString(), *Widget, (*Widget)->GetClass(), WidgetProperty->PropertyClass);
					}
					else
					{
						MessageLog.Error(*IncorrectWidgetTypeError.ToString(), *Widget, (*Widget)->GetClass(), WidgetProperty->PropertyClass);
					}
				}
			}
		}
	}

	Super::FinishCompilingClass(Class);
}

void FWidgetBlueprintCompiler::PostCompile()
{
	Super::PostCompile();

	WidgetToMemberVariableMap.Empty();

	UWidgetBlueprintGeneratedClass* WidgetClass = NewWidgetBlueprintClass;

	UWidgetBlueprint* WidgetBP = WidgetBlueprint();

	WidgetClass->bAllowDynamicCreation = WidgetBP->WidgetSupportsDynamicCreation();
	WidgetClass->bAllowTemplate = CanAllowTemplate(MessageLog, NewWidgetBlueprintClass);

	if (!Blueprint->bIsRegeneratingOnLoad && bIsFullCompile)
	{
		WidgetClass->GetDefaultObject<UUserWidget>()->ValidateBlueprint(*WidgetBP->WidgetTree, MessageLog);

		if (MessageLog.NumErrors == 0 && WidgetClass->bAllowTemplate)
		{
			UUserWidget* WidgetTemplate = NewObject<UUserWidget>(GetTransientPackage(), WidgetClass);
			WidgetTemplate->TemplateInit();

			int32 TotalWidgets = 0;
			WidgetTemplate->WidgetTree->ForEachWidgetAndDescendants([&TotalWidgets](UWidget* Widget) {
				TotalWidgets++;
			});
			WidgetBP->InclusiveWidgets = TotalWidgets;

			// Determine if we can generate a template for this widget to speed up CreateWidget time.
			TArray<FText> PostCompileErrors;
			if (CanTemplateWidget(MessageLog, WidgetTemplate, PostCompileErrors))
			{
				MessageLog.Note(*LOCTEXT("TemplateSuccess", "Fast Template Successfully Created.").ToString());
			}
			else
			{
				MessageLog.Error(*LOCTEXT("NoTemplate", "Unable To Create Template For Widget.").ToString());
				for (FText& Error : PostCompileErrors)
				{
					MessageLog.Error(*Error.ToString());
				}
			}
		}
	}
}

void FWidgetBlueprintCompiler::EnsureProperGeneratedClass(UClass*& TargetUClass)
{
	if ( TargetUClass && !( (UObject*)TargetUClass )->IsA(UWidgetBlueprintGeneratedClass::StaticClass()) )
	{
		FKismetCompilerUtilities::ConsignToOblivion(TargetUClass, Blueprint->bIsRegeneratingOnLoad);
		TargetUClass = nullptr;
	}
}

void FWidgetBlueprintCompiler::SpawnNewClass(const FString& NewClassName)
{
	NewWidgetBlueprintClass = FindObject<UWidgetBlueprintGeneratedClass>(Blueprint->GetOutermost(), *NewClassName);

	if ( NewWidgetBlueprintClass == nullptr )
	{
		NewWidgetBlueprintClass = NewObject<UWidgetBlueprintGeneratedClass>(Blueprint->GetOutermost(), FName(*NewClassName), RF_Public | RF_Transactional);
	}
	else
	{
		// Already existed, but wasn't linked in the Blueprint yet due to load ordering issues
		FBlueprintCompileReinstancer::Create(NewWidgetBlueprintClass);
	}
	NewClass = NewWidgetBlueprintClass;
}

void FWidgetBlueprintCompiler::OnNewClassSet(UBlueprintGeneratedClass* ClassToUse)
{
	NewWidgetBlueprintClass = CastChecked<UWidgetBlueprintGeneratedClass>(ClassToUse);
}

void FWidgetBlueprintCompiler::PrecompileFunction(FKismetFunctionContext& Context, EInternalCompilerFlags InternalFlags)
{
	Super::PrecompileFunction(Context, InternalFlags);

	VerifyEventReplysAreNotEmpty(Context);
}

void FWidgetBlueprintCompiler::VerifyEventReplysAreNotEmpty(FKismetFunctionContext& Context)
{
	TArray<UK2Node_FunctionResult*> FunctionResults;
	Context.SourceGraph->GetNodesOfClass<UK2Node_FunctionResult>(FunctionResults);

	UScriptStruct* EventReplyStruct = FEventReply::StaticStruct();
	FEdGraphPinType EventReplyPinType(UEdGraphSchema_K2::PC_Struct, NAME_None, EventReplyStruct, EPinContainerType::None, /*bIsReference =*/false, /*InValueTerminalType =*/FEdGraphTerminalType());

	for ( UK2Node_FunctionResult* FunctionResult : FunctionResults )
	{
		for ( UEdGraphPin* ReturnPin : FunctionResult->Pins )
		{
			if ( ReturnPin->PinType == EventReplyPinType )
			{
				const bool IsUnconnectedEventReply = ReturnPin->Direction == EEdGraphPinDirection::EGPD_Input && ReturnPin->LinkedTo.Num() == 0;
				if ( IsUnconnectedEventReply )
				{
					MessageLog.Warning(*LOCTEXT("MissingEventReply_Warning", "Event Reply @@ should not be empty.  Return a reply such as Handled or Unhandled.").ToString(), ReturnPin);
				}
			}
		}
	}
}

bool FWidgetBlueprintCompiler::ValidateGeneratedClass(UBlueprintGeneratedClass* Class)
{
	bool SuperResult = Super::ValidateGeneratedClass(Class);
	bool Result = UWidgetBlueprint::ValidateGeneratedClass(Class);

	return SuperResult && Result;
}

#undef LOCTEXT_NAMESPACE
