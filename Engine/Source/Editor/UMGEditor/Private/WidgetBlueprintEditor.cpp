// Copyright Epic Games, Inc. All Rights Reserved.

#include "WidgetBlueprintEditor.h"
#include "MovieSceneBinding.h"
#include "MovieSceneFolder.h"
#include "MovieScene.h"
#include "Animation/WidgetAnimation.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Engine/SimpleConstructionScript.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "WidgetBlueprint.h"
#include "Editor.h"

#if WITH_EDITOR
	#include "EditorStyleSet.h"
#endif // WITH_EDITOR

#include "Algo/AllOf.h"

#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Settings/WidgetDesignerSettings.h"

#include "Tracks/MovieScenePropertyTrack.h"
#include "ISequencerModule.h"
#include "SequencerSettings.h"
#include "ObjectEditorUtils.h"

#include "PropertyCustomizationHelpers.h"

#include "BlueprintModes/WidgetBlueprintApplicationModes.h"
#include "Blueprint/WidgetTree.h"
#include "WidgetBlueprintEditorUtils.h"
#include "WorkflowOrientedApp/ApplicationMode.h"
#include "BlueprintModes/WidgetDesignerApplicationMode.h"
#include "BlueprintModes/WidgetGraphApplicationMode.h"

#include "WidgetBlueprintEditorToolbar.h"
#include "Components/CanvasPanel.h"
#include "Framework/Commands/GenericCommands.h"
#include "Kismet2/CompilerResultsLog.h"
#include "IMessageLogListing.h"
#include "WidgetGraphSchema.h"

#include "Animation/MovieSceneWidgetMaterialTrack.h"
#include "Animation/WidgetMaterialTrackUtilities.h"

#include "ScopedTransaction.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "UMGEditorActions.h"
#include "GameProjectGenerationModule.h"

#include "SPaletteViewModel.h"

#define LOCTEXT_NAMESPACE "UMG"

FWidgetBlueprintEditor::FWidgetBlueprintEditor()
	: PreviewScene(FPreviewScene::ConstructionValues().AllowAudioPlayback(true).ShouldSimulatePhysics(true))
	, PreviewBlueprint(nullptr)
	, bIsSimulateEnabled(false)
	, bIsRealTime(true)
	, bRefreshGeneratedClassAnimations(false)
	, bUpdatingSequencerSelection(false)
	, bUpdatingExternalSelection(false)
{
	PreviewScene.GetWorld()->bBegunPlay = false;

	// Register sequencer menu extenders.
	ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>( "Sequencer" );
	{
		int32 NewIndex = SequencerModule.GetAddTrackMenuExtensibilityManager()->GetExtenderDelegates().Add(
			FAssetEditorExtender::CreateRaw(this, &FWidgetBlueprintEditor::GetAddTrackSequencerExtender));
		SequencerAddTrackExtenderHandle = SequencerModule.GetAddTrackMenuExtensibilityManager()->GetExtenderDelegates()[NewIndex].GetHandle();
	}
}

FWidgetBlueprintEditor::~FWidgetBlueprintEditor()
{
	UWidgetBlueprint* Blueprint = GetWidgetBlueprintObj();
	if ( Blueprint )
	{
		Blueprint->OnChanged().RemoveAll(this);
		Blueprint->OnCompiled().RemoveAll(this);
	}

	GEditor->OnObjectsReplaced().RemoveAll(this);
	
	if ( Sequencer.IsValid() )
	{
		Sequencer->OnMovieSceneDataChanged().RemoveAll( this );
		Sequencer->OnMovieSceneBindingsPasted().RemoveAll( this );
		Sequencer.Reset();
	}

	// Un-Register sequencer menu extenders.
	ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
	SequencerModule.GetAddTrackMenuExtensibilityManager()->GetExtenderDelegates().RemoveAll([this](const FAssetEditorExtender& Extender)
	{
		return SequencerAddTrackExtenderHandle == Extender.GetHandle();
	});
}

void FWidgetBlueprintEditor::InitWidgetBlueprintEditor(const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, const TArray<UBlueprint*>& InBlueprints, bool bShouldOpenInDefaultsMode)
{
	bShowDashedOutlines = GetDefault<UWidgetDesignerSettings>()->bShowOutlines;
	bRespectLocks = GetDefault<UWidgetDesignerSettings>()->bRespectLocks;

	TSharedPtr<FWidgetBlueprintEditor> ThisPtr(SharedThis(this));

	PaletteViewModel = MakeShareable(new FPaletteViewModel(ThisPtr));
	PaletteViewModel->RegisterToEvents();

	WidgetToolbar = MakeShareable(new FWidgetBlueprintEditorToolbar(ThisPtr));

	BindToolkitCommands();

	InitBlueprintEditor(Mode, InitToolkitHost, InBlueprints, bShouldOpenInDefaultsMode);

	// register for any objects replaced
	GEditor->OnObjectsReplaced().AddSP(this, &FWidgetBlueprintEditor::OnObjectsReplaced);

	// for change selected widgets on sequencer tree view
	UWidgetBlueprint* Blueprint = GetWidgetBlueprintObj();

	UpdatePreview(GetWidgetBlueprintObj(), true);

	DesignerCommandList = MakeShareable(new FUICommandList);

	DesignerCommandList->MapAction(FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &FWidgetBlueprintEditor::DeleteSelectedWidgets),
		FCanExecuteAction::CreateSP(this, &FWidgetBlueprintEditor::CanDeleteSelectedWidgets)
		);

	DesignerCommandList->MapAction(FGenericCommands::Get().Copy,
		FExecuteAction::CreateSP(this, &FWidgetBlueprintEditor::CopySelectedWidgets),
		FCanExecuteAction::CreateSP(this, &FWidgetBlueprintEditor::CanCopySelectedWidgets)
		);

	DesignerCommandList->MapAction(FGenericCommands::Get().Cut,
		FExecuteAction::CreateSP(this, &FWidgetBlueprintEditor::CutSelectedWidgets),
		FCanExecuteAction::CreateSP(this, &FWidgetBlueprintEditor::CanCutSelectedWidgets)
		);

	DesignerCommandList->MapAction(FGenericCommands::Get().Paste,
		FExecuteAction::CreateSP(this, &FWidgetBlueprintEditor::PasteWidgets),
		FCanExecuteAction::CreateSP(this, &FWidgetBlueprintEditor::CanPasteWidgets)
		);

	DesignerCommandList->MapAction(FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateSP(this, &FWidgetBlueprintEditor::DuplicateSelectedWidgets),
		FCanExecuteAction::CreateSP(this, &FWidgetBlueprintEditor::CanDuplicateSelectedWidgets)
		);
}

void FWidgetBlueprintEditor::InitalizeExtenders()
{
	FBlueprintEditor::InitalizeExtenders();

	AddMenuExtender(CreateMenuExtender());
}

TSharedPtr<FExtender> FWidgetBlueprintEditor::CreateMenuExtender()
{
	TSharedPtr<FExtender> MenuExtender = MakeShareable(new FExtender);

	// Extend the File menu with asset actions
	MenuExtender->AddMenuExtension(
		"FileLoadAndSave",
		EExtensionHook::After,
		GetToolkitCommands(),
		FMenuExtensionDelegate::CreateSP(this, &FWidgetBlueprintEditor::FillFileMenu));
	
	return MenuExtender;
}

void FWidgetBlueprintEditor::FillFileMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection(TEXT("WidgetBlueprint"), LOCTEXT("WidgetBlueprint", "Widget Blueprint"));
	MenuBuilder.AddMenuEntry(FUMGEditorCommands::Get().CreateNativeBaseClass);
	MenuBuilder.EndSection();
}

void FWidgetBlueprintEditor::BindToolkitCommands()
{
	FUMGEditorCommands::Register();

	GetToolkitCommands()->MapAction(FUMGEditorCommands::Get().CreateNativeBaseClass,
		FUIAction(
			FExecuteAction::CreateSP(this, &FWidgetBlueprintEditor::OpenCreateNativeBaseClassDialog),
			FCanExecuteAction::CreateSP(this, &FWidgetBlueprintEditor::IsParentClassNative)
		)
	);
}

void FWidgetBlueprintEditor::OpenCreateNativeBaseClassDialog()
{
	FGameProjectGenerationModule::Get().OpenAddCodeToProjectDialog(
		FAddToProjectConfig()
		.DefaultClassPrefix(TEXT(""))
		.DefaultClassName(GetWidgetBlueprintObj()->GetName() + TEXT("Base"))
		.ParentClass(GetWidgetBlueprintObj()->ParentClass)
		.ParentWindow(FGlobalTabmanager::Get()->GetRootWindow())
		.OnAddedToProject(FOnAddedToProject::CreateSP(this, &FWidgetBlueprintEditor::OnCreateNativeBaseClassSuccessfully))
	);
}

void FWidgetBlueprintEditor::OnCreateNativeBaseClassSuccessfully(const FString& InClassName, const FString& InClassPath, const FString& InModuleName)
{
	UClass* NewNativeClass = FindObject<UClass>(ANY_PACKAGE, *InClassName);
	if (NewNativeClass)
	{
		ReparentBlueprint_NewParentChosen(NewNativeClass);
	}
}

void FWidgetBlueprintEditor::RegisterApplicationModes(const TArray<UBlueprint*>& InBlueprints, bool bShouldOpenInDefaultsMode, bool bNewlyCreated/* = false*/)
{
	//FBlueprintEditor::RegisterApplicationModes(InBlueprints, bShouldOpenInDefaultsMode);

	if ( InBlueprints.Num() == 1 )
	{
		TSharedPtr<FWidgetBlueprintEditor> ThisPtr(SharedThis(this));

		// Create the modes and activate one (which will populate with a real layout)
		TArray< TSharedRef<FApplicationMode> > TempModeList;
		TempModeList.Add(MakeShareable(new FWidgetDesignerApplicationMode(ThisPtr)));
		TempModeList.Add(MakeShareable(new FWidgetGraphApplicationMode(ThisPtr)));

		for ( TSharedRef<FApplicationMode>& AppMode : TempModeList )
		{
			AddApplicationMode(AppMode->GetModeName(), AppMode);
		}

		SetCurrentMode(FWidgetBlueprintApplicationModes::DesignerMode);
	}
	else
	{
		//// We either have no blueprints or many, open in the defaults mode for multi-editing
		//AddApplicationMode(
		//	FBlueprintEditorApplicationModes::BlueprintDefaultsMode,
		//	MakeShareable(new FBlueprintDefaultsApplicationMode(SharedThis(this))));
		//SetCurrentMode(FBlueprintEditorApplicationModes::BlueprintDefaultsMode);
	}
}

void FWidgetBlueprintEditor::SelectWidgets(const TSet<FWidgetReference>& Widgets, bool bAppendOrToggle)
{
	TSet<FWidgetReference> TempSelection;
	for ( const FWidgetReference& Widget : Widgets )
	{
		if ( Widget.IsValid() )
		{
			TempSelection.Add(Widget);
		}
	}

	OnSelectedWidgetsChanging.Broadcast();

	// Finally change the selected widgets after we've updated the details panel 
	// to ensure values that are pending are committed on focus loss, and migrated properly
	// to the old selected widgets.
	if ( !bAppendOrToggle )
	{
		SelectedWidgets.Empty();
	}
	SelectedObjects.Empty();
	SelectedNamedSlot.Reset();

	for ( const FWidgetReference& Widget : TempSelection )
	{
		if ( bAppendOrToggle && SelectedWidgets.Contains(Widget) )
		{
			SelectedWidgets.Remove(Widget);
		}
		else
		{
			SelectedWidgets.Add(Widget);
		}
	}

	OnSelectedWidgetsChanged.Broadcast();
}

void FWidgetBlueprintEditor::SelectObjects(const TSet<UObject*>& Objects)
{
	OnSelectedWidgetsChanging.Broadcast();

	SelectedWidgets.Empty();
	SelectedObjects.Empty();
	SelectedNamedSlot.Reset();

	for ( UObject* Obj : Objects )
	{
		SelectedObjects.Add(Obj);
	}

	OnSelectedWidgetsChanged.Broadcast();
}

bool FWidgetBlueprintEditor::IsBindingSelected(const FMovieSceneBinding& InBinding)
{
	TSet<FWidgetReference> Widgets = GetSelectedWidgets();
	if (Widgets.Num() == 0)
	{
		return true;
	}

	UMovieSceneSequence* AnimationSequence = GetSequencer().Get()->GetFocusedMovieSceneSequence();
	UObject* BindingContext = GetAnimationPlaybackContext();
	TArray<UObject*, TInlineAllocator<1>> BoundObjects = AnimationSequence->LocateBoundObjects(InBinding.GetObjectGuid(), BindingContext);

	if (BoundObjects.Num() == 0)
	{
		return false;
	}
	else if (Cast<UPanelSlot>(BoundObjects[0]))
	{
		return Widgets.Contains(GetReferenceFromPreview(Cast<UPanelSlot>(BoundObjects[0])->Content));
	}
	else
	{
		return Widgets.Contains(GetReferenceFromPreview(Cast<UWidget>(BoundObjects[0])));
	}
}

void FWidgetBlueprintEditor::SetSelectedNamedSlot(TOptional<FNamedSlotSelection> InSelectedNamedSlot)
{
	OnSelectedWidgetsChanging.Broadcast();

	SelectedWidgets.Empty();
	SelectedObjects.Empty();
	SelectedNamedSlot.Reset();

	SelectedNamedSlot = InSelectedNamedSlot;
	if (InSelectedNamedSlot.IsSet())
	{
		SelectedWidgets.Add(InSelectedNamedSlot->NamedSlotHostWidget);
	}

	OnSelectedWidgetsChanged.Broadcast();
}

void FWidgetBlueprintEditor::CleanSelection()
{
	TSet<FWidgetReference> TempSelection;

	TArray<UWidget*> WidgetsInTree;
	GetWidgetBlueprintObj()->WidgetTree->GetAllWidgets(WidgetsInTree);
	TSet<UWidget*> TreeWidgetSet(WidgetsInTree);

	for ( FWidgetReference& WidgetRef : SelectedWidgets )
	{
		if ( WidgetRef.IsValid() )
		{
			if ( TreeWidgetSet.Contains(WidgetRef.GetTemplate()) )
			{
				TempSelection.Add(WidgetRef);
			}
		}
	}

	if ( TempSelection.Num() != SelectedWidgets.Num() )
	{
		SelectWidgets(TempSelection, false);
	}
}

const TSet<FWidgetReference>& FWidgetBlueprintEditor::GetSelectedWidgets() const
{
	return SelectedWidgets;
}

const TSet< TWeakObjectPtr<UObject> >& FWidgetBlueprintEditor::GetSelectedObjects() const
{
	return SelectedObjects;
}

TOptional<FNamedSlotSelection> FWidgetBlueprintEditor::GetSelectedNamedSlot() const
{
	return SelectedNamedSlot;
}

void FWidgetBlueprintEditor::InvalidatePreview(bool bViewOnly)
{
	if ( bViewOnly )
	{
		OnWidgetPreviewUpdated.Broadcast();
	}
	else
	{
		bPreviewInvalidated = true;
	}
}

void FWidgetBlueprintEditor::OnBlueprintChangedImpl(UBlueprint* InBlueprint, bool bIsJustBeingCompiled )
{
	DestroyPreview();

	FBlueprintEditor::OnBlueprintChangedImpl(InBlueprint, bIsJustBeingCompiled);

	if ( InBlueprint )
	{
		RefreshPreview();
	}
}

void FWidgetBlueprintEditor::OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap)
{
	// Remove dead references and update references
	for ( int32 HandleIndex = WidgetHandlePool.Num() - 1; HandleIndex >= 0; HandleIndex-- )
	{
		TSharedPtr<FWidgetHandle> Ref = WidgetHandlePool[HandleIndex].Pin();

		if ( Ref.IsValid() )
		{
			UObject* const* NewObject = ReplacementMap.Find(Ref->Widget.Get());
			if ( NewObject )
			{
				Ref->Widget = Cast<UWidget>(*NewObject);
			}
		}
		else
		{
			WidgetHandlePool.RemoveAtSwap(HandleIndex);
		}
	}
}

bool FWidgetBlueprintEditor::CanDeleteSelectedWidgets()
{
	TSet<FWidgetReference> Widgets = GetSelectedWidgets();
	return Widgets.Num() > 0;
}

void FWidgetBlueprintEditor::DeleteSelectedWidgets()
{
	TSet<FWidgetReference> Widgets = GetSelectedWidgets();
	FWidgetBlueprintEditorUtils::DeleteWidgets(GetWidgetBlueprintObj(), Widgets);

	// Clear the selection now that the widget has been deleted.
	TSet<FWidgetReference> Empty;
	SelectWidgets(Empty, false);
}

bool FWidgetBlueprintEditor::CanCopySelectedWidgets()
{
	TSet<FWidgetReference> Widgets = GetSelectedWidgets();
	return Widgets.Num() > 0;
}

void FWidgetBlueprintEditor::CopySelectedWidgets()
{
	TSet<FWidgetReference> Widgets = GetSelectedWidgets();
	FWidgetBlueprintEditorUtils::CopyWidgets(GetWidgetBlueprintObj(), Widgets);
}

bool FWidgetBlueprintEditor::CanCutSelectedWidgets()
{
	TSet<FWidgetReference> Widgets = GetSelectedWidgets();
	return Widgets.Num() > 0;
}

void FWidgetBlueprintEditor::CutSelectedWidgets()
{
	TSet<FWidgetReference> Widgets = GetSelectedWidgets();
	FWidgetBlueprintEditorUtils::CutWidgets(GetWidgetBlueprintObj(), Widgets);
}

const UWidgetAnimation* FWidgetBlueprintEditor::RefreshCurrentAnimation()
{
	return CurrentAnimation.Get();
}

bool FWidgetBlueprintEditor::CanPasteWidgets()
{
	TSet<FWidgetReference> Widgets = GetSelectedWidgets();
	if ( Widgets.Num() == 1 )
	{
		// Always return true here now since we want to support pasting widgets as siblings
		return true;
	}
	else if ( GetWidgetBlueprintObj()->WidgetTree->RootWidget == nullptr )
	{
		return true;
	}
	else
	{
		TOptional<FNamedSlotSelection> NamedSlotSelection = GetSelectedNamedSlot();
		if ( NamedSlotSelection.IsSet() )
		{
			INamedSlotInterface* NamedSlotHost = Cast<INamedSlotInterface>(NamedSlotSelection->NamedSlotHostWidget.GetTemplate());
			if ( NamedSlotHost == nullptr )
			{
				return false;
			}
			else if ( NamedSlotHost->GetContentForSlot(NamedSlotSelection->SlotName) != nullptr )
			{
				return false;
			}

			return true;
		}
	}

	return false;
}

void FWidgetBlueprintEditor::PasteWidgets()
{
	TSet<FWidgetReference> Widgets = GetSelectedWidgets();
	FWidgetReference Target = Widgets.Num() > 0 ? *Widgets.CreateIterator() : FWidgetReference();
	FName SlotName = NAME_None;

	TOptional<FNamedSlotSelection> NamedSlotSelection = GetSelectedNamedSlot();
	if ( NamedSlotSelection.IsSet() )
	{
		Target = NamedSlotSelection->NamedSlotHostWidget;
		SlotName = NamedSlotSelection->SlotName;
	}

	TArray<UWidget*> PastedWidgets = FWidgetBlueprintEditorUtils::PasteWidgets(SharedThis(this), GetWidgetBlueprintObj(), Target, SlotName, PasteDropLocation);

	PasteDropLocation = PasteDropLocation + FVector2D(25, 25);

	TSet<FWidgetReference> PastedWidgetRefs;
	for (UWidget* Widget : PastedWidgets)
	{
		PastedWidgetRefs.Add(GetReferenceFromPreview(Widget));
	}
	SelectWidgets(PastedWidgetRefs, false);
}

bool FWidgetBlueprintEditor::CanDuplicateSelectedWidgets()
{
	TSet<FWidgetReference> Widgets = GetSelectedWidgets();
	if (Widgets.Num() == 1)
	{
		FWidgetReference Target = *Widgets.CreateIterator();
		UPanelWidget* ParentWidget = Target.GetTemplate()->GetParent();
		return ParentWidget && ParentWidget->CanAddMoreChildren();
	}
	return false;
}

void FWidgetBlueprintEditor::DuplicateSelectedWidgets()
{
	TSet<FWidgetReference> Widgets = GetSelectedWidgets();
	FWidgetBlueprintEditorUtils::DuplicateWidgets(SharedThis(this), GetWidgetBlueprintObj(), Widgets);
}

void FWidgetBlueprintEditor::Tick(float DeltaTime)
{
	FBlueprintEditor::Tick(DeltaTime);

	// Tick the preview scene world.
	if ( !GIntraFrameDebuggingGameThread )
	{
		// Allow full tick only if preview simulation is enabled and we're not currently in an active SIE or PIE session
		if ( bIsSimulateEnabled && GEditor->PlayWorld == nullptr && !GEditor->bIsSimulatingInEditor )
		{
			PreviewScene.GetWorld()->Tick(bIsRealTime ? LEVELTICK_All : LEVELTICK_TimeOnly, DeltaTime);
		}
		else
		{
			PreviewScene.GetWorld()->Tick(bIsRealTime ? LEVELTICK_ViewportsOnly : LEVELTICK_TimeOnly, DeltaTime);
		}
	}

	// Whenever animations change the generated class animations need to be updated since they are copied on compile.  This
	// update is deferred to tick since some edit operations (e.g. drag/drop) cause large numbers of changes to the data.
	if ( bRefreshGeneratedClassAnimations )
	{
		TArray<UWidgetAnimation*>& PreviewAnimations = Cast<UWidgetBlueprintGeneratedClass>( PreviewBlueprint->GeneratedClass )->Animations;
		PreviewAnimations.Empty();
		for ( UWidgetAnimation* WidgetAnimation : PreviewBlueprint->Animations )
		{
			PreviewAnimations.Add( DuplicateObject<UWidgetAnimation>( WidgetAnimation, PreviewBlueprint->GeneratedClass ) );
		}
		bRefreshGeneratedClassAnimations = false;
	}

	// Note: The weak ptr can become stale if the actor is reinstanced due to a Blueprint change, etc. In that case we 
	//       look to see if we can find the new instance in the preview world and then update the weak ptr.
	if ( PreviewWidgetPtr.IsStale(true) || bPreviewInvalidated )
	{
		bPreviewInvalidated = false;
		RefreshPreview();
	}

	// Updat the palette view model.
	if (PaletteViewModel->NeedUpdate())
	{
		PaletteViewModel->Update();
	}
}

static bool MigratePropertyValue(UObject* SourceObject, UObject* DestinationObject, FEditPropertyChain::TDoubleLinkedListNode* PropertyChainNode, FProperty* MemberProperty, bool bIsModify)
{
	FProperty* CurrentProperty = PropertyChainNode->GetValue();
	FEditPropertyChain::TDoubleLinkedListNode* NextNode = PropertyChainNode->GetNextNode();

	if ( !ensure(SourceObject && DestinationObject) )
	{
		return false;
	}

	ensure(SourceObject->GetClass() == DestinationObject->GetClass());

	// If the current property is an array, map or set, short-circuit current progress so that we copy the whole container.
	if ( CastField<FArrayProperty>(CurrentProperty) || CastField<FMapProperty>(CurrentProperty) || CastField<FSetProperty>(CurrentProperty) )
	{
		NextNode = nullptr;
	}

	if ( FObjectProperty* CurrentObjectProperty = CastField<FObjectProperty>(CurrentProperty) )
	{
		UObject* NewSourceObject = CurrentObjectProperty->GetObjectPropertyValue_InContainer(SourceObject);
		UObject* NewDestionationObject = CurrentObjectProperty->GetObjectPropertyValue_InContainer(DestinationObject);

		if ( NewSourceObject == nullptr || NewDestionationObject == nullptr )
		{
			NextNode = nullptr;
		}
	}
	
	if ( NextNode == nullptr )
	{
		if (bIsModify)
		{
			if (DestinationObject)
			{
				DestinationObject->Modify();
			}
			return true;
		}
		else
		{
			// Check to see if there's an edit condition property we also need to migrate.
			bool bDummyNegate = false;
			FBoolProperty* EditConditionProperty = PropertyCustomizationHelpers::GetEditConditionProperty(MemberProperty, bDummyNegate);
			if ( EditConditionProperty != nullptr )
			{
				FObjectEditorUtils::MigratePropertyValue(SourceObject, EditConditionProperty, DestinationObject, EditConditionProperty);
			}

			return FObjectEditorUtils::MigratePropertyValue(SourceObject, MemberProperty, DestinationObject, MemberProperty);
		}
	}

	if ( FObjectProperty* CurrentObjectProperty = CastField<FObjectProperty>(CurrentProperty) )
	{
		UObject* NewSourceObject = CurrentObjectProperty->GetObjectPropertyValue_InContainer(SourceObject);
		UObject* NewDestionationObject = CurrentObjectProperty->GetObjectPropertyValue_InContainer(DestinationObject);

		return MigratePropertyValue(NewSourceObject, NewDestionationObject, NextNode, NextNode->GetValue(), bIsModify);
	}

	// ExportText/ImportText works on all property types
	return MigratePropertyValue(SourceObject, DestinationObject, NextNode, MemberProperty, bIsModify);
}

void FWidgetBlueprintEditor::AddReferencedObjects( FReferenceCollector& Collector )
{
	FBlueprintEditor::AddReferencedObjects( Collector );

	UUserWidget* Preview = GetPreview();
	Collector.AddReferencedObject( Preview );
}

void FWidgetBlueprintEditor::MigrateFromChain(FEditPropertyChain* PropertyThatChanged, bool bIsModify)
{
	UWidgetBlueprint* Blueprint = GetWidgetBlueprintObj();

	UUserWidget* PreviewUserWidget = GetPreview();
	if ( PreviewUserWidget != nullptr )
	{
		for ( TWeakObjectPtr<UObject> ObjectRef : SelectedObjects )
		{
			// dealing with root widget here
			FEditPropertyChain::TDoubleLinkedListNode* PropertyChainNode = PropertyThatChanged->GetHead();
			UObject* WidgetCDO = ObjectRef.Get()->GetClass()->GetDefaultObject(true);
			MigratePropertyValue(ObjectRef.Get(), WidgetCDO, PropertyChainNode, PropertyChainNode->GetValue(), bIsModify);
		}

		for ( FWidgetReference& WidgetRef : SelectedWidgets )
		{
			UWidget* PreviewWidget = WidgetRef.GetPreview();

			if ( PreviewWidget )
			{
				FName PreviewWidgetName = PreviewWidget->GetFName();
				UWidget* TemplateWidget = Blueprint->WidgetTree->FindWidget(PreviewWidgetName);

				if ( TemplateWidget )
				{
					FEditPropertyChain::TDoubleLinkedListNode* PropertyChainNode = PropertyThatChanged->GetHead();
					MigratePropertyValue(PreviewWidget, TemplateWidget, PropertyChainNode, PropertyChainNode->GetValue(), bIsModify);
				}
			}
		}
	}
}

void FWidgetBlueprintEditor::PostUndo(bool bSuccessful)
{
	FBlueprintEditor::PostUndo(bSuccessful);

	OnWidgetBlueprintTransaction.Broadcast();
}

void FWidgetBlueprintEditor::PostRedo(bool bSuccessful)
{
	FBlueprintEditor::PostRedo(bSuccessful);

	OnWidgetBlueprintTransaction.Broadcast();
}

TSharedRef<SWidget> FWidgetBlueprintEditor::CreateSequencerWidget()
{
	TSharedRef<SOverlay> SequencerOverlayRef =
		SNew(SOverlay)
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Sequencer")));
	SequencerOverlay = SequencerOverlayRef;

	TSharedRef<STextBlock> NoAnimationTextBlockRef = 
		SNew(STextBlock)
		.TextStyle(FEditorStyle::Get(), "UMGEditor.NoAnimationFont")
		.Text(LOCTEXT("NoAnimationSelected", "No Animation Selected"));
	NoAnimationTextBlock = NoAnimationTextBlockRef;

	SequencerOverlayRef->AddSlot(0)
	[
		GetSequencer()->GetSequencerWidget()
	];

	SequencerOverlayRef->AddSlot(1)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
	[
		NoAnimationTextBlockRef
	];

	return SequencerOverlayRef;
}

UWidgetBlueprint* FWidgetBlueprintEditor::GetWidgetBlueprintObj() const
{
	return Cast<UWidgetBlueprint>(GetBlueprintObj());
}

UUserWidget* FWidgetBlueprintEditor::GetPreview() const
{
	if ( PreviewWidgetPtr.IsStale(true) )
	{
		return nullptr;
	}

	return PreviewWidgetPtr.Get();
}

FPreviewScene* FWidgetBlueprintEditor::GetPreviewScene()
{
	return &PreviewScene;
}

bool FWidgetBlueprintEditor::IsSimulating() const
{
	return bIsSimulateEnabled;
}

void FWidgetBlueprintEditor::SetIsSimulating(bool bSimulating)
{
	bIsSimulateEnabled = bSimulating;
}

FWidgetReference FWidgetBlueprintEditor::GetReferenceFromTemplate(UWidget* TemplateWidget)
{
	TSharedRef<FWidgetHandle> Reference = MakeShareable(new FWidgetHandle(TemplateWidget));
	WidgetHandlePool.Add(Reference);

	return FWidgetReference(SharedThis(this), Reference);
}

FWidgetReference FWidgetBlueprintEditor::GetReferenceFromPreview(UWidget* PreviewWidget)
{
	UUserWidget* PreviewRoot = GetPreview();
	if ( PreviewRoot )
	{
		UWidgetBlueprint* Blueprint = GetWidgetBlueprintObj();

		if ( PreviewWidget )
		{
			FName Name = PreviewWidget->GetFName();
			return GetReferenceFromTemplate(Blueprint->WidgetTree->FindWidget(Name));
		}
	}

	return FWidgetReference(SharedThis(this), TSharedPtr<FWidgetHandle>());
}

TSharedPtr<ISequencer>& FWidgetBlueprintEditor::GetSequencer()
{
	if(!Sequencer.IsValid())
	{
		const float InTime  = 0.f;
		const float OutTime = 5.0f;

		FSequencerViewParams ViewParams(TEXT("UMGSequencerSettings"));
		{
			ViewParams.OnGetAddMenuContent = FOnGetAddMenuContent::CreateSP(this, &FWidgetBlueprintEditor::OnGetAnimationAddMenuContent);
		    ViewParams.OnBuildCustomContextMenuForGuid = FOnBuildCustomContextMenuForGuid::CreateSP(this, &FWidgetBlueprintEditor::OnBuildCustomContextMenuForGuid);
		}

		FSequencerInitParams SequencerInitParams;
		{
			UWidgetAnimation* NullAnimation = UWidgetAnimation::GetNullAnimation();
			FFrameRate TickResolution = NullAnimation->MovieScene->GetTickResolution();
			FFrameNumber StartFrame = (InTime  * TickResolution).FloorToFrame();
			FFrameNumber EndFrame   = (OutTime * TickResolution).CeilToFrame();
			NullAnimation->MovieScene->SetPlaybackRange(StartFrame, (EndFrame-StartFrame).Value);
			FMovieSceneEditorData& EditorData = NullAnimation->MovieScene->GetEditorData();
			EditorData.WorkStart = InTime;
			EditorData.WorkEnd   = OutTime;

			SequencerInitParams.ViewParams = ViewParams;
			SequencerInitParams.RootSequence = NullAnimation;
			SequencerInitParams.bEditWithinLevelEditor = false;
			SequencerInitParams.ToolkitHost = GetToolkitHost();
			SequencerInitParams.PlaybackContext = TAttribute<UObject*>(this, &FWidgetBlueprintEditor::GetAnimationPlaybackContext);
			SequencerInitParams.EventContexts = TAttribute<TArray<UObject*>>(this, &FWidgetBlueprintEditor::GetAnimationEventContexts);

			SequencerInitParams.HostCapabilities.bSupportsCurveEditor = true;
		};

		Sequencer = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer").CreateSequencer(SequencerInitParams);
		// Never recompile the blueprint on evaluate as this can create an insidious loop
		Sequencer->GetSequencerSettings()->SetCompileDirectorOnEvaluate(false);
		Sequencer->OnMovieSceneDataChanged().AddSP( this, &FWidgetBlueprintEditor::OnMovieSceneDataChanged );
		Sequencer->OnMovieSceneBindingsPasted().AddSP( this, &FWidgetBlueprintEditor::OnMovieSceneBindingsPasted );
		// Change selected widgets in the sequencer tree view
		Sequencer->GetSelectionChangedObjectGuids().AddSP(this, &FWidgetBlueprintEditor::SyncSelectedWidgetsWithSequencerSelection);
		OnSelectedWidgetsChanged.AddSP(this, &FWidgetBlueprintEditor::SyncSequencerSelectionToSelectedWidgets);
		
		// Allow sequencer to test which bindings are selected
		Sequencer->OnGetIsBindingVisible().BindRaw(this, &FWidgetBlueprintEditor::IsBindingSelected);

		ChangeViewedAnimation(*UWidgetAnimation::GetNullAnimation());
	}

	return Sequencer;
}

void FWidgetBlueprintEditor::ChangeViewedAnimation( UWidgetAnimation& InAnimationToView )
{
	CurrentAnimation = &InAnimationToView;

	if (Sequencer.IsValid())
	{
		Sequencer->ResetToNewRootSequence(InAnimationToView);
	}

	TSharedPtr<SOverlay> SequencerOverlayPin = SequencerOverlay.Pin();
	if (SequencerOverlayPin.IsValid())
	{
		TSharedPtr<STextBlock> NoAnimationTextBlockPin = NoAnimationTextBlock.Pin();
		if( &InAnimationToView == UWidgetAnimation::GetNullAnimation())
		{
			const FName CurveEditorTabName = FName(TEXT("SequencerGraphEditor"));
			TSharedPtr<SDockTab> ExistingTab = GetToolkitHost()->GetTabManager()->FindExistingLiveTab(CurveEditorTabName);
			if (ExistingTab)
			{
				ExistingTab->RequestCloseTab();
			}

			// Disable sequencer from interaction
			Sequencer->GetSequencerWidget()->SetEnabled(false);
			Sequencer->SetAutoChangeMode(EAutoChangeMode::None);
			NoAnimationTextBlockPin->SetVisibility(EVisibility::Visible);
			SequencerOverlayPin->SetVisibility( EVisibility::HitTestInvisible );
		}
		else
		{
			// Allow sequencer to be interacted with
			Sequencer->GetSequencerWidget()->SetEnabled(true);
			NoAnimationTextBlockPin->SetVisibility(EVisibility::Collapsed);
			SequencerOverlayPin->SetVisibility( EVisibility::SelfHitTestInvisible );
		}
	}
	InvalidatePreview();
}

void FWidgetBlueprintEditor::RefreshPreview()
{
	// Rebuilding the preview can force objects to be recreated, so the selection may need to be updated.
	OnSelectedWidgetsChanging.Broadcast();

	UpdatePreview(GetWidgetBlueprintObj(), true);

	CleanSelection();

	// Fire the selection updated event to ensure everyone is watching the same widgets.
	OnSelectedWidgetsChanged.Broadcast();
}

void FWidgetBlueprintEditor::Compile()
{
	DestroyPreview();

	FBlueprintEditor::Compile();
}

void FWidgetBlueprintEditor::DestroyPreview()
{
	UUserWidget* PreviewUserWidget = GetPreview();
	if ( PreviewUserWidget != nullptr )
	{
		check(PreviewScene.GetWorld());

		// Immediately release the preview ptr to let people know it's gone.
		PreviewWidgetPtr.Reset();

		// Immediately notify anyone with a preview out there they need to dispose of it right now,
		// otherwise the leak detection can't be trusted.
		OnWidgetPreviewUpdated.Broadcast();

		TWeakPtr<SWidget> PreviewSlateWidgetWeak = PreviewUserWidget->GetCachedWidget();

		PreviewUserWidget->MarkPendingKill();
		PreviewUserWidget->ReleaseSlateResources(true);

		FCompilerResultsLog LogResults;
		LogResults.bAnnotateMentionedNodes = false;

		ensure(!PreviewSlateWidgetWeak.IsValid());

		bool bFoundLeak = false;
		
		// NOTE: This doesn't explore sub UUserWidget trees, searching for leaks there.

		// Verify everything is going to be garbage collected.
		PreviewUserWidget->WidgetTree->ForEachWidget([&LogResults, &bFoundLeak] (UWidget* Widget) {
			if ( !bFoundLeak )
			{
				TWeakPtr<SWidget> PreviewChildWidget = Widget->GetCachedWidget();
				if ( PreviewChildWidget.IsValid() )
				{
					bFoundLeak = true;
					if ( UPanelWidget* ParentWidget = Widget->GetParent() )
					{
						LogResults.Warning(
							*FText::Format(
								LOCTEXT("LeakingWidgetsWithParent_WarningFmt", "Leak Detected!  {0} (@@) still has living Slate widgets, it or the parent {1} (@@) is keeping them in memory.  Release all Slate resources in ReleaseSlateResources()."),
								FText::FromString(Widget->GetName()),
								FText::FromString(ParentWidget->GetName())
							).ToString(),
							Widget->GetClass(),
							ParentWidget->GetClass()
						);
					}
					else
					{
						LogResults.Warning(
							*FText::Format(
								LOCTEXT("LeakingWidgetsWithoutParent_WarningFmt", "Leak Detected!  {0} (@@) still has living Slate widgets, it or the parent widget is keeping them in memory.  Release all Slate resources in ReleaseSlateResources()."),
								FText::FromString(Widget->GetName())
							).ToString(),
							Widget->GetClass()
						);
					}
				}
			}
		});

		DesignerCompilerMessages = LogResults.Messages;
	}
}

void FWidgetBlueprintEditor::AppendExtraCompilerResults(TSharedPtr<class IMessageLogListing> ResultsListing)
{
	FBlueprintEditor::AppendExtraCompilerResults(ResultsListing);

	ResultsListing->AddMessages(DesignerCompilerMessages);
}

void FWidgetBlueprintEditor::UpdatePreview(UBlueprint* InBlueprint, bool bInForceFullUpdate)
{
	UUserWidget* PreviewUserWidget = GetPreview();

	// Signal that we're going to be constructing editor components
	if ( InBlueprint != nullptr && InBlueprint->SimpleConstructionScript != nullptr )
	{
		InBlueprint->SimpleConstructionScript->BeginEditorComponentConstruction();
	}

	// If the Blueprint is changing
	if ( InBlueprint != PreviewBlueprint || bInForceFullUpdate )
	{
		// Destroy the previous actor instance
		DestroyPreview();

		// Save the Blueprint we're creating a preview for
		PreviewBlueprint = Cast<UWidgetBlueprint>(InBlueprint);

		// Create the Widget, we have to do special swapping out of the widget tree.
		{
			// Assign the outer to the game instance if it exists, otherwise use the world
			{
				FMakeClassSpawnableOnScope TemporarilySpawnable(PreviewBlueprint->GeneratedClass);
				PreviewUserWidget = NewObject<UUserWidget>(PreviewScene.GetWorld(), PreviewBlueprint->GeneratedClass);
			}

			// The preview widget should not be transactional.
			PreviewUserWidget->ClearFlags(RF_Transactional);

			// Establish the widget as being in design time before initializing and before duplication 
            // (so that IsDesignTime is reliable within both calls to Initialize)
            // The preview widget is also the outer widget that will update all child flags
			PreviewUserWidget->SetDesignerFlags(GetCurrentDesignerFlags());

			if ( ULocalPlayer* Player = PreviewScene.GetWorld()->GetFirstLocalPlayerFromController() )
			{
				PreviewUserWidget->SetPlayerContext(FLocalPlayerContext(Player));
			}

			UWidgetTree* LatestWidgetTree = PreviewBlueprint->WidgetTree;

			// If there is no RootWidget, we look for a WidgetTree in the parents classes until we find one.
			if (LatestWidgetTree->RootWidget == nullptr)
			{
				UWidgetBlueprintGeneratedClass* BGClass = PreviewUserWidget->GetWidgetTreeOwningClass();
				if (BGClass)
				{
					LatestWidgetTree = BGClass->WidgetTree;
				}
			}

			// Update the widget tree directly to match the blueprint tree.  That way the preview can update
			// without needing to do a full recompile.
			PreviewUserWidget->DuplicateAndInitializeFromWidgetTree(LatestWidgetTree);

			// Establish the widget as being in design time before initializing (so that IsDesignTime is reliable within Initialize)
            // We have to call it to make sure that all the WidgetTree had the DesignerFlags set correctly
			PreviewUserWidget->SetDesignerFlags(GetCurrentDesignerFlags());
		}

		// Store a reference to the preview actor.
		PreviewWidgetPtr = PreviewUserWidget;
	}

	OnWidgetPreviewUpdated.Broadcast();

	// We've changed the binding context so drastically that we should just clear all knowledge of our previous cached bindings

	if (Sequencer.IsValid())
	{
		Sequencer->State.ClearObjectCaches(*Sequencer);
		Sequencer->ForceEvaluate();
	}
}

FGraphAppearanceInfo FWidgetBlueprintEditor::GetGraphAppearance(UEdGraph* InGraph) const
{
	FGraphAppearanceInfo AppearanceInfo = FBlueprintEditor::GetGraphAppearance(InGraph);

	if ( GetBlueprintObj()->IsA(UWidgetBlueprint::StaticClass()) )
	{
		AppearanceInfo.CornerText = LOCTEXT("AppearanceCornerText", "WIDGET BLUEPRINT");
	}

	return AppearanceInfo;
}

TSubclassOf<UEdGraphSchema> FWidgetBlueprintEditor::GetDefaultSchemaClass() const
{
	return UWidgetGraphSchema::StaticClass();
}

void FWidgetBlueprintEditor::ClearHoveredWidget()
{
	HoveredWidget = FWidgetReference();
	OnHoveredWidgetCleared.Broadcast();
}

void FWidgetBlueprintEditor::SetHoveredWidget(FWidgetReference& InHoveredWidget)
{
	if (InHoveredWidget != HoveredWidget)
	{
		HoveredWidget = InHoveredWidget;
		OnHoveredWidgetSet.Broadcast(InHoveredWidget);
	}
}

const FWidgetReference& FWidgetBlueprintEditor::GetHoveredWidget() const
{
	return HoveredWidget;
}

void FWidgetBlueprintEditor::AddPostDesignerLayoutAction(TFunction<void()> Action)
{
	QueuedDesignerActions.Add(MoveTemp(Action));
}

void FWidgetBlueprintEditor::OnEnteringDesigner()
{
	OnEnterWidgetDesigner.Broadcast();
}

TArray< TFunction<void()> >& FWidgetBlueprintEditor::GetQueuedDesignerActions()
{
	return QueuedDesignerActions;
}

EWidgetDesignFlags FWidgetBlueprintEditor::GetCurrentDesignerFlags() const
{
	EWidgetDesignFlags Flags = EWidgetDesignFlags::Designing;
	
	if ( bShowDashedOutlines )
	{
		Flags |= EWidgetDesignFlags::ShowOutline;
	}

	if ( const UWidgetDesignerSettings* Designer = GetDefault<UWidgetDesignerSettings>() )
	{
		if ( Designer->bExecutePreConstructEvent )
		{
			Flags |= EWidgetDesignFlags::ExecutePreConstruct;
		}
	}

	return Flags;
}

bool FWidgetBlueprintEditor::GetShowDashedOutlines() const
{
	return bShowDashedOutlines;
}

void FWidgetBlueprintEditor::SetShowDashedOutlines(bool Value)
{
	bShowDashedOutlines = Value;
}

bool FWidgetBlueprintEditor::GetIsRespectingLocks() const
{
	return bRespectLocks;
}

void FWidgetBlueprintEditor::SetIsRespectingLocks(bool Value)
{
	bRespectLocks = Value;
}

class FObjectAndDisplayName
{
public:
	FObjectAndDisplayName(FText InDisplayName, UObject* InObject)
	{
		DisplayName = InDisplayName;
		Object = InObject;
	}

	bool operator<(FObjectAndDisplayName const& Other) const
	{
		return DisplayName.CompareTo(Other.DisplayName) < 0;
	}

	FText DisplayName;
	UObject* Object;

};

void GetBindableObjects(UWidgetTree* WidgetTree, TArray<FObjectAndDisplayName>& BindableObjects)
{
	// Add the 'this' widget so you can animate it.
	BindableObjects.Add(FObjectAndDisplayName(LOCTEXT("RootWidgetThis", "[[This]]"), WidgetTree->GetOuter()));

	WidgetTree->ForEachWidget([&BindableObjects] (UWidget* Widget) {
		
		// if the widget has a generated name this is just some unimportant widget, don't show it in the list?
		if (Widget->IsGeneratedName() && !Widget->bIsVariable)
		{
			return;
		}
		
		BindableObjects.Add(FObjectAndDisplayName(Widget->GetLabelText(), Widget));

		if (Widget->Slot && Widget->Slot->Parent)
		{
			FText SlotDisplayName = FText::Format(LOCTEXT("AddMenuSlotFormat", "{0} ({1})"), Widget->GetLabelText(), Widget->Slot->GetClass()->GetDisplayNameText());
			BindableObjects.Add(FObjectAndDisplayName(SlotDisplayName, Widget->Slot));
		}
	});
}

void FWidgetBlueprintEditor::OnGetAnimationAddMenuContent(FMenuBuilder& MenuBuilder, TSharedRef<ISequencer> InSequencer)
{
	if (CurrentAnimation.IsValid())
	{
		const TSet<FWidgetReference>& Selection = GetSelectedWidgets();
		for (const FWidgetReference& SelectedWidget : Selection)
		{
			if (UWidget* Widget = SelectedWidget.GetPreview())
			{
				FUIAction AddWidgetTrackAction(FExecuteAction::CreateSP(this, &FWidgetBlueprintEditor::AddObjectToAnimation, (UObject*)Widget));
				MenuBuilder.AddMenuEntry(Widget->GetLabelText(), FText(), FSlateIcon(), AddWidgetTrackAction);

				if (Widget->Slot && Widget->Slot->Parent)
				{
					FText SlotDisplayName = FText::Format(LOCTEXT("AddMenuSlotFormat", "{0} ({1})"), Widget->GetLabelText(), Widget->Slot->GetClass()->GetDisplayNameText());
					FUIAction AddSlotTrackAction(FExecuteAction::CreateSP(this, &FWidgetBlueprintEditor::AddObjectToAnimation, (UObject*)Widget->Slot));
					MenuBuilder.AddMenuEntry(SlotDisplayName, FText(), FSlateIcon(), AddSlotTrackAction);
				}
			}
		}

		MenuBuilder.AddSubMenu(
			LOCTEXT("AllNamedWidgets", "All Named Widgets"),
			LOCTEXT("AllNamedWidgetsTooltip", "Select a widget or slot to create an animation track for"),
			FNewMenuDelegate::CreateRaw(this, &FWidgetBlueprintEditor::OnGetAnimationAddMenuContentAllWidgets),
			false,
			FSlateIcon()
		);
	}
}

void FWidgetBlueprintEditor::OnGetAnimationAddMenuContentAllWidgets(FMenuBuilder& MenuBuilder)
{
	TArray<FObjectAndDisplayName> BindableObjects;
	{
		GetBindableObjects(GetPreview()->WidgetTree, BindableObjects);
		BindableObjects.Sort();
	}

	for (FObjectAndDisplayName& BindableObject : BindableObjects)
	{
		FGuid BoundObjectGuid = Sequencer->FindObjectId(*BindableObject.Object, Sequencer->GetFocusedTemplateID());
		if (BoundObjectGuid.IsValid() == false)
		{
			FUIAction AddMenuAction(FExecuteAction::CreateSP(this, &FWidgetBlueprintEditor::AddObjectToAnimation, BindableObject.Object));
			MenuBuilder.AddMenuEntry(BindableObject.DisplayName, FText(), FSlateIcon(), AddMenuAction);
		}
	}
}

void FWidgetBlueprintEditor::AddObjectToAnimation(UObject* ObjectToAnimate)
{
	UMovieScene* MovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
	if (MovieScene->IsReadOnly())
	{
		return;
	}

	const FScopedTransaction Transaction( LOCTEXT( "AddWidgetToAnimation", "Add widget to animation" ) );
	Sequencer->GetFocusedMovieSceneSequence()->Modify();

	// @todo Sequencer - Make this kind of adding more explicit, this current setup seem a bit brittle.
	FGuid NewGuid = Sequencer->GetHandleToObject(ObjectToAnimate);

	TArray<UMovieSceneFolder*> SelectedParentFolders;
	Sequencer->GetSelectedFolders(SelectedParentFolders);

	if (SelectedParentFolders.Num() > 0)
	{
		SelectedParentFolders[0]->AddChildObjectBinding(NewGuid);
	}
}

TSharedRef<FExtender> FWidgetBlueprintEditor::GetAddTrackSequencerExtender( const TSharedRef<FUICommandList> CommandList, const TArray<UObject*> ContextSensitiveObjects )
{
	TSharedRef<FExtender> AddTrackMenuExtender( new FExtender() );
	AddTrackMenuExtender->AddMenuExtension(
		SequencerMenuExtensionPoints::AddTrackMenu_PropertiesSection,
		EExtensionHook::Before,
		CommandList,
		FMenuExtensionDelegate::CreateRaw( this, &FWidgetBlueprintEditor::ExtendSequencerAddTrackMenu, ContextSensitiveObjects ) );
	return AddTrackMenuExtender;
}

void FWidgetBlueprintEditor::OnBuildCustomContextMenuForGuid(FMenuBuilder& MenuBuilder, FGuid ObjectBinding)
{
	if (CurrentAnimation.IsValid())
	{
		TArray<FWidgetReference> ValidSelectedWidgets;
		for (FWidgetReference SelectedWidget : SelectedWidgets)
		{
			if (SelectedWidget.IsValid())
			{
				//need to make sure it's a widget, if not bound assume it is.
				UWidget* BoundWidget = nullptr;
				bool bNotBound = true;
				for (TWeakObjectPtr<> WeakObjectPtr : GetSequencer()->FindObjectsInCurrentSequence(ObjectBinding))
				{
					BoundWidget = Cast<UWidget>(WeakObjectPtr.Get());
					bNotBound = false;
					break;
				}

				if (bNotBound || (BoundWidget && SelectedWidget.GetPreview()->GetTypedOuter<UWidgetTree>() == BoundWidget->GetTypedOuter<UWidgetTree>()))
				{
					ValidSelectedWidgets.Add(SelectedWidget);
				}
			}
		}
		
		if(ValidSelectedWidgets.Num() > 0)
		{
			MenuBuilder.AddMenuSeparator();
			
			MenuBuilder.AddMenuEntry(
				LOCTEXT("AddSelectedToBinding", "Add Selected"),
				LOCTEXT("AddSelectedToBindingToolTip", "Add selected objects to this track"),
				FSlateIcon(),
				FExecuteAction::CreateRaw(this, &FWidgetBlueprintEditor::AddWidgetsToTrack, ValidSelectedWidgets, ObjectBinding)
			);
			
			if (ValidSelectedWidgets.Num() > 1)
			{
				MenuBuilder.AddMenuEntry(
					LOCTEXT("ReplaceBindingWithSelected", "Replace with Selected"),
					LOCTEXT("ReplaceBindingWithSelectedToolTip", "Replace the object binding with selected objects"),
					FSlateIcon(),
					FExecuteAction::CreateRaw(this, &FWidgetBlueprintEditor::ReplaceTrackWithWidgets, ValidSelectedWidgets, ObjectBinding)
				);
			}
			else
			{
				MenuBuilder.AddMenuEntry(
					FText::Format(LOCTEXT("ReplaceObject", "Replace with {0}"), FText::FromString(ValidSelectedWidgets[0].GetPreview()->GetName())),
					FText::Format(LOCTEXT("ReplaceObjectToolTip", "Replace the bound widget in this animation with {0}"), FText::FromString(ValidSelectedWidgets[0].GetPreview()->GetName())),
					FSlateIcon(),
					FExecuteAction::CreateRaw(this, &FWidgetBlueprintEditor::ReplaceTrackWithWidgets, ValidSelectedWidgets, ObjectBinding)
				);
			}
			
			MenuBuilder.AddMenuEntry(
				LOCTEXT("RemoveSelectedFromBinding", "Remove Selected"),
				LOCTEXT("RemoveSelectedFromBindingToolTip", "Remove selected objects from this track"),
				FSlateIcon(),
				FExecuteAction::CreateRaw(this, &FWidgetBlueprintEditor::RemoveWidgetsFromTrack, ValidSelectedWidgets, ObjectBinding)
			);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("RemoveAllBindings", "Remove All"),
				LOCTEXT("RemoveAllBindingsToolTip", "Remove all bound objects from this track"),
				FSlateIcon(),
				FExecuteAction::CreateRaw(this, &FWidgetBlueprintEditor::RemoveAllWidgetsFromTrack, ObjectBinding)
			);
			
			MenuBuilder.AddMenuEntry(
				LOCTEXT("RemoveMissing", "Remove Missing"),
				LOCTEXT("RemoveMissingToolTip", "Remove missing objects bound to this track"),
				FSlateIcon(),
				FExecuteAction::CreateRaw(this, &FWidgetBlueprintEditor::RemoveMissingWidgetsFromTrack, ObjectBinding)
			);
		}
	}
}

void FWidgetBlueprintEditor::ExtendSequencerAddTrackMenu( FMenuBuilder& AddTrackMenuBuilder, const TArray<UObject*> ContextObjects )
{
	if ( ContextObjects.Num() == 1 )
	{
		UWidget* Widget = Cast<UWidget>( ContextObjects[0] );

		if ( Widget != nullptr && Widget->GetTypedOuter<UUserWidget>() == GetPreview() )
		{
			if( Widget->GetParent() != nullptr && Widget->Slot != nullptr )
			{
				AddTrackMenuBuilder.BeginSection( "Slot", LOCTEXT( "SlotSection", "Slot" ) );
				{
					FUIAction AddSlotAction( FExecuteAction::CreateRaw( this, &FWidgetBlueprintEditor::AddSlotTrack, Widget->Slot ) );
					FText AddSlotLabel = FText::Format(LOCTEXT("SlotLabelFormat", "{0} Slot"), FText::FromString(Widget->GetParent()->GetName()));
					FText AddSlotToolTip = FText::Format(LOCTEXT("SlotToolTipFormat", "Add {0} slot"), FText::FromString( Widget->GetParent()->GetName()));
					AddTrackMenuBuilder.AddMenuEntry(AddSlotLabel, AddSlotToolTip, FSlateIcon(), AddSlotAction);
				}
				AddTrackMenuBuilder.EndSection();
			}

			TArray<FWidgetMaterialPropertyPath> MaterialBrushPropertyPaths;
			WidgetMaterialTrackUtilities::GetMaterialBrushPropertyPaths( Widget, MaterialBrushPropertyPaths );
			if ( MaterialBrushPropertyPaths.Num() > 0 )
			{
				AddTrackMenuBuilder.BeginSection( "Materials", LOCTEXT( "MaterialsSection", "Materials" ) );
				{
					for (FWidgetMaterialPropertyPath& MaterialBrushPropertyPath : MaterialBrushPropertyPaths )
					{
						FString DisplayName = MaterialBrushPropertyPath.PropertyPath[0]->GetDisplayNameText().ToString();
						for ( int32 i = 1; i < MaterialBrushPropertyPath.PropertyPath.Num(); i++)
						{
							DisplayName.AppendChar( '.' );
							DisplayName.Append( MaterialBrushPropertyPath.PropertyPath[i]->GetDisplayNameText().ToString() );
						}
						DisplayName.AppendChar('.');
						DisplayName.Append(MaterialBrushPropertyPath.DisplayName);

						FText DisplayNameText = FText::FromString( DisplayName );
						FUIAction AddMaterialAction( FExecuteAction::CreateRaw( this, &FWidgetBlueprintEditor::AddMaterialTrack, Widget, MaterialBrushPropertyPath.PropertyPath, DisplayNameText ) );
						FText AddMaterialLabel = DisplayNameText;
						FText AddMaterialToolTip = FText::Format( LOCTEXT( "MaterialToolTipFormat", "Add a material track for the {0} property." ), DisplayNameText );
						AddTrackMenuBuilder.AddMenuEntry( AddMaterialLabel, AddMaterialToolTip, FSlateIcon(), AddMaterialAction );
					}
				}
				AddTrackMenuBuilder.EndSection();
			}
		}
	}
}

void FWidgetBlueprintEditor::AddWidgetsToTrack(const TArray<FWidgetReference> Widgets, FGuid ObjectId)
{
	const FScopedTransaction Transaction(LOCTEXT("AddSelectedWidgetsToTrack", "Add Widgets to Track"));

	UWidgetAnimation* WidgetAnimation = Cast<UWidgetAnimation>(Sequencer->GetFocusedMovieSceneSequence());
	UMovieScene* MovieScene = WidgetAnimation->GetMovieScene();

	TArray<FWidgetReference> WidgetsToAdd;
	for (const FWidgetReference& Widget : Widgets)
	{
		UWidget* PreviewWidget = Widget.GetPreview();

		// If this widget is already bound to the animation we cannot add it to 2 separate bindings
		FGuid SelectedWidgetId = Sequencer->FindObjectId(*PreviewWidget, MovieSceneSequenceID::Root);
		if (!SelectedWidgetId.IsValid())
		{
			WidgetsToAdd.Add(Widget);
		}
	}

	if (WidgetsToAdd.Num() == 0)
	{
		FNotificationInfo Info(LOCTEXT("Widgetalreadybound", "Widget already bound"));
		Info.FadeInDuration = 0.1f;
		Info.FadeOutDuration = 0.5f;
		Info.ExpireDuration = 2.5f;
		auto NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);

		NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
		NotificationItem->ExpireAndFadeout();
	}
	else
	{
		MovieScene->Modify();
		WidgetAnimation->Modify();

		for (const FWidgetReference Widget : WidgetsToAdd)
		{
			UWidget* PreviewWidget = Widget.GetPreview();
			WidgetAnimation->BindPossessableObject(ObjectId, *PreviewWidget, GetAnimationPlaybackContext());
		}

		UpdateTrackName(ObjectId);

		Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
	}
}

void FWidgetBlueprintEditor::RemoveWidgetsFromTrack(const TArray<FWidgetReference> Widgets, FGuid ObjectId)
{
	const FScopedTransaction Transaction(LOCTEXT("RemoveWidgetsFromTrack", "Remove Widgets from Track"));

	UWidgetAnimation* WidgetAnimation = Cast<UWidgetAnimation>(Sequencer->GetFocusedMovieSceneSequence());
	UMovieScene* MovieScene = WidgetAnimation->GetMovieScene();

	TArray<FWidgetReference> WidgetsToRemove;

	for (const FWidgetReference& Widget : Widgets)
	{
		UWidget* PreviewWidget = Widget.GetPreview();
		FGuid WidgetId = Sequencer->FindObjectId(*PreviewWidget, MovieSceneSequenceID::Root);
		if (WidgetId.IsValid() && WidgetId == ObjectId)
		{
			WidgetsToRemove.Add(Widget);
		}
	}

	if (WidgetsToRemove.Num() == 0)
	{
		FNotificationInfo Info(LOCTEXT("SelectedWidgetNotBound", "Selected Widget not Bound to Track"));
		Info.FadeInDuration = 0.1f;
		Info.FadeOutDuration = 0.5f;
		Info.ExpireDuration = 2.5f;
		auto NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);

		NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
		NotificationItem->ExpireAndFadeout();
	}
	else
	{
		MovieScene->Modify();
		WidgetAnimation->Modify();

		for (const FWidgetReference& Widget : WidgetsToRemove)
		{
			UWidget* PreviewWidget = Widget.GetPreview();
			WidgetAnimation->RemoveBinding(*PreviewWidget);

			Sequencer->RestorePreAnimatedState(*PreviewWidget);
		}

		UpdateTrackName(ObjectId);

		Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
	}
}

void FWidgetBlueprintEditor::RemoveAllWidgetsFromTrack(FGuid ObjectId)
{
	const FScopedTransaction Transaction(LOCTEXT("RemoveAllWidgetsFromTrack", "Remove All Widgets from Track"));

	UWidgetAnimation* WidgetAnimation = Cast<UWidgetAnimation>(Sequencer->GetFocusedMovieSceneSequence());
	UMovieScene* MovieScene = WidgetAnimation->GetMovieScene();

	UUserWidget* PreviewRoot = GetPreview();
	check(PreviewRoot);

	WidgetAnimation->Modify();
	MovieScene->Modify();

	// Restore object animation state
	for (TWeakObjectPtr<> WeakObject : Sequencer->FindBoundObjects(ObjectId, MovieSceneSequenceID::Root))
	{
		if (UObject* Obj = WeakObject.Get())
		{
			Sequencer->RestorePreAnimatedState(*Obj);
		}
	}

	// Remove bindings
	for (int32 Index = WidgetAnimation->AnimationBindings.Num() - 1; Index >= 0; --Index)
	{
		if (WidgetAnimation->AnimationBindings[Index].AnimationGuid == ObjectId)
		{
			WidgetAnimation->AnimationBindings.RemoveAt(Index, 1, false);
		}
	}

	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
}

void FWidgetBlueprintEditor::RemoveMissingWidgetsFromTrack(FGuid ObjectId)
{
	const FScopedTransaction Transaction(LOCTEXT("RemoveMissingWidgetsFromTrack", "Remove Missing Widgets from Track"));

	UWidgetAnimation* WidgetAnimation = Cast<UWidgetAnimation>(Sequencer->GetFocusedMovieSceneSequence());
	UMovieScene* MovieScene = WidgetAnimation->GetMovieScene();

	UUserWidget* PreviewRoot = GetPreview();
	check(PreviewRoot);

	WidgetAnimation->Modify();
	MovieScene->Modify();

	for (int32 Index = WidgetAnimation->AnimationBindings.Num() - 1; Index >= 0; --Index)
	{
		const FWidgetAnimationBinding& Binding = WidgetAnimation->AnimationBindings[Index];
		if (Binding.AnimationGuid == ObjectId && Binding.FindRuntimeObject(*PreviewRoot->WidgetTree, *PreviewRoot) == nullptr)
		{
			WidgetAnimation->AnimationBindings.RemoveAt(Index, 1, false);
		}
	}

	UpdateTrackName(ObjectId);
}

void FWidgetBlueprintEditor::ReplaceTrackWithWidgets(TArray<FWidgetReference> Widgets, FGuid ObjectId)
{
	const FScopedTransaction Transaction( LOCTEXT( "ReplaceTrackWithSelectedWidgets", "Replace Track with Selected Widgets" ) );

	UWidgetAnimation* WidgetAnimation = Cast<UWidgetAnimation>(Sequencer->GetFocusedMovieSceneSequence());
	UMovieScene* MovieScene = WidgetAnimation->GetMovieScene();

	WidgetAnimation->Modify();
	MovieScene->Modify();

	// Remove everything from the track
	RemoveAllWidgetsFromTrack(ObjectId);

	// Filter out anything in the input array that is currently bound to another object in the animation
	for (int32 Index = Widgets.Num()-1; Index >= 0; --Index)
	{
		UWidget* PreviewWidget = Widgets[Index].GetPreview();
		FGuid WidgetId = Sequencer->FindObjectId(*PreviewWidget, MovieSceneSequenceID::Root);
		if (WidgetId.IsValid())
		{
			Widgets.RemoveAt(Index, 1, false);
		}
	}

	if (Widgets.Num() > 0)
	{
		AddWidgetsToTrack(Widgets, ObjectId);
	}
	else
	{
		FNotificationInfo Info(LOCTEXT("Widgetalreadybound", "Widget already bound"));
		Info.FadeInDuration = 0.1f;
		Info.FadeOutDuration = 0.5f;
		Info.ExpireDuration = 2.5f;
		auto NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);

		NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
		NotificationItem->ExpireAndFadeout();
	}

	UpdateTrackName(ObjectId);
	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
}

void FWidgetBlueprintEditor::AddSlotTrack( UPanelSlot* Slot )
{
	GetSequencer()->GetHandleToObject( Slot );
}

void FWidgetBlueprintEditor::AddMaterialTrack( UWidget* Widget, TArray<FProperty*> MaterialPropertyPath, FText MaterialPropertyDisplayName )
{
	FGuid WidgetHandle = Sequencer->GetHandleToObject( Widget );
	if ( WidgetHandle.IsValid() )
	{
		UMovieScene* MovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();

		if (MovieScene->IsReadOnly())
		{
			return;
		}

		TArray<FName> MaterialPropertyNamePath;
		for ( FProperty* Property : MaterialPropertyPath )
		{
			MaterialPropertyNamePath.Add( Property->GetFName() );
		}
		if( MovieScene->FindTrack( UMovieSceneWidgetMaterialTrack::StaticClass(), WidgetHandle, WidgetMaterialTrackUtilities::GetTrackNameFromPropertyNamePath( MaterialPropertyNamePath ) ) == nullptr)
		{
			const FScopedTransaction Transaction( LOCTEXT( "AddWidgetMaterialTrack", "Add widget material track" ) );

			MovieScene->Modify();

			UMovieSceneWidgetMaterialTrack* NewTrack = Cast<UMovieSceneWidgetMaterialTrack>( MovieScene->AddTrack( UMovieSceneWidgetMaterialTrack::StaticClass(), WidgetHandle ) );
			NewTrack->Modify();
			NewTrack->SetBrushPropertyNamePath( MaterialPropertyNamePath );
			NewTrack->SetDisplayName( FText::Format( LOCTEXT( "TrackDisplayNameFormat", "{0}"), MaterialPropertyDisplayName ) );

			Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded );
		}
	}
}

void FWidgetBlueprintEditor::OnMovieSceneDataChanged(EMovieSceneDataChangeType DataChangeType)
{
	bRefreshGeneratedClassAnimations = true;
}

void FWidgetBlueprintEditor::OnMovieSceneBindingsPasted(const TArray<FMovieSceneBinding>& BindingsPasted)
{
	TArray<FObjectAndDisplayName> BindableObjects;
	{
		GetBindableObjects(GetPreview()->WidgetTree, BindableObjects);
	}

	UMovieSceneSequence* AnimationSequence = GetSequencer().Get()->GetFocusedMovieSceneSequence();
	UObject* BindingContext = GetAnimationPlaybackContext();

	// First, rebind top level possessables (without parents) - match binding pasted's name with the bindable object name
	for (const FMovieSceneBinding& BindingPasted : BindingsPasted)
	{
		FMovieScenePossessable* Possessable = AnimationSequence->GetMovieScene()->FindPossessable(BindingPasted.GetObjectGuid());
		if (Possessable && !Possessable->GetParent().IsValid())
		{
			for (FObjectAndDisplayName& BindableObject : BindableObjects)
			{
				if (BindableObject.DisplayName.ToString() == BindingPasted.GetName())
				{
					AnimationSequence->BindPossessableObject(BindingPasted.GetObjectGuid(), *BindableObject.Object, BindingContext);			
					break;
				}
			}
		}
	}

	// Second, bind child possessables - match the binding pasted's parent guid with the bindable slot's content guid
	for (const FMovieSceneBinding& BindingPasted : BindingsPasted)
	{
		FMovieScenePossessable* Possessable = AnimationSequence->GetMovieScene()->FindPossessable(BindingPasted.GetObjectGuid());
		if (Possessable && Possessable->GetParent().IsValid())
		{
			for (FObjectAndDisplayName& BindableObject : BindableObjects)
			{
				UPanelSlot* PanelSlot = Cast<UPanelSlot>(BindableObject.Object);
				if (PanelSlot && PanelSlot->Content)
				{
					FGuid ParentGuid = AnimationSequence->FindPossessableObjectId(*PanelSlot->Content, BindingContext);

					if (ParentGuid == Possessable->GetParent())
					{
						AnimationSequence->BindPossessableObject(BindingPasted.GetObjectGuid(), *BindableObject.Object, BindingContext);			
						break;
					}

					// Special case for canvas slots, they need to be added again
					if (BindableObject.Object->GetFName().ToString() == BindingPasted.GetName())
					{
						// Create handle, to rebind correctly
						Sequencer->GetHandleToObject(BindableObject.Object);
						// Remove the existing binding, as it is now replaced by the that was just added by getting the handle
						AnimationSequence->GetMovieScene()->RemovePossessable(BindingPasted.GetObjectGuid());
						break;
					}
				}
			}
		}
	}
}

void FWidgetBlueprintEditor::SyncSelectedWidgetsWithSequencerSelection(TArray<FGuid> ObjectGuids)
{
	if (bUpdatingSequencerSelection)
	{
		return;
	}

	TGuardValue<bool> Guard(bUpdatingExternalSelection, true);

	UMovieSceneSequence* AnimationSequence = GetSequencer().Get()->GetFocusedMovieSceneSequence();
	UObject* BindingContext = GetAnimationPlaybackContext();
	TSet<FWidgetReference> SequencerSelectedWidgets;
	for (FGuid Guid : ObjectGuids)
	{
		TArray<UObject*, TInlineAllocator<1>> BoundObjects = AnimationSequence->LocateBoundObjects(Guid, BindingContext);
		if (BoundObjects.Num() == 0)
		{
			continue;
		}
		else if (Cast<UPanelSlot>(BoundObjects[0]))
		{
			SequencerSelectedWidgets.Add(GetReferenceFromPreview(Cast<UPanelSlot>(BoundObjects[0])->Content));
		}
		else
		{
			UWidget* BoundWidget = Cast<UWidget>(BoundObjects[0]);
			SequencerSelectedWidgets.Add(GetReferenceFromPreview(BoundWidget));
		}
	}
	if (SequencerSelectedWidgets.Num() != 0)
	{
		SelectWidgets(SequencerSelectedWidgets, false);
	}
}

void FWidgetBlueprintEditor::SyncSequencerSelectionToSelectedWidgets()
{
	if (bUpdatingExternalSelection)
	{
		return;
	}

	TGuardValue<bool> Guard(bUpdatingSequencerSelection, true);

	if (GetSequencer()->GetSequencerSettings()->GetShowSelectedNodesOnly())
	{
		GetSequencer()->RefreshTree();
	}

	GetSequencer()->ExternalSelectionHasChanged();
}

void FWidgetBlueprintEditor::UpdateTrackName(FGuid ObjectId)
{
	UWidgetAnimation* WidgetAnimation = Cast<UWidgetAnimation>(Sequencer->GetFocusedMovieSceneSequence());
	UMovieScene* MovieScene = WidgetAnimation->GetMovieScene();

	const TArray<FWidgetAnimationBinding>& WidgetBindings = WidgetAnimation->GetBindings();
	if (WidgetBindings.Num() > 0)
	{
		FString NewLabel = WidgetBindings[0].WidgetName.ToString();
		if (WidgetBindings.Num() > 1)
		{
			NewLabel.Append(FString::Printf(TEXT(" (%d)"), WidgetBindings.Num()));
		}

		MovieScene->SetObjectDisplayName(ObjectId, FText::FromString(NewLabel));
	}
}
#undef LOCTEXT_NAMESPACE
