// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSceneOutliner.h"

#include "EdMode.h"
#include "Editor.h"
#include "Editor/GroupActor.h"
#include "Editor/UnrealEdEngine.h"
#include "EditorModeManager.h"
#include "EditorStyleSet.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformApplicationMisc.h"
#include "ISceneOutlinerColumn.h"
#include "Kismet2/ComponentEditorUtils.h"
#include "Layout/WidgetPath.h"
#include "Modules/ModuleManager.h"
#include "SceneOutlinerDelegates.h"
#include "SceneOutlinerFilters.h"
#include "SceneOutlinerModule.h"
#include "SceneOutlinerSettings.h"
#include "ScopedTransaction.h"
#include "Textures/SlateIcon.h"
#include "ToolMenus.h"
#include "UnrealEdGlobals.h"
#include "UObject/PackageReload.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SOverlay.h"

#include "SceneOutlinerDragDrop.h"
#include "SceneOutlinerMenuContext.h"

#include "ActorEditorUtils.h"
#include "LevelUtils.h"

#include "EditorActorFolders.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Features/IModularFeatures.h"
#include "ISceneOutlinerTraversal.h"

DEFINE_LOG_CATEGORY_STATIC(LogSceneOutliner, Log, All);

#define LOCTEXT_NAMESPACE "SSceneOutliner"

// The amount of time that must pass before the Scene Outliner will attempt a sort when in PIE/SIE.
#define SCENE_OUTLINER_RESORT_TIMER 1.0f

namespace SceneOutliner
{
	DECLARE_MULTICAST_DELEGATE(FOnSharedSettingsChanged);
	FOnSharedSettingsChanged OnSharedSettingChangedDelegate;

	FText GetWorldDescription(UWorld* World)
	{
		FText Description;
		if(World)
		{
			FText PostFix;
			const FWorldContext* WorldContext = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if(Context.World() == World)
				{
					WorldContext = &Context;
					break;
				}
			}

			if (World->WorldType == EWorldType::PIE)
			{
				switch(World->GetNetMode())
				{
					case NM_Client:
						if (WorldContext)
						{
							PostFix = FText::Format(LOCTEXT("ClientPostfixFormat", "(Client {0})"), FText::AsNumber(WorldContext->PIEInstance - 1));
						}
						else
						{
							PostFix = LOCTEXT("ClientPostfix", "(Client)");
						}
						break;
					case NM_DedicatedServer:
					case NM_ListenServer:
						PostFix = LOCTEXT("ServerPostfix", "(Server)");
						break;
					case NM_Standalone:
						PostFix = LOCTEXT("PlayInEditorPostfix", "(Play In Editor)");
						break;
				}
			}
			else if(World->WorldType == EWorldType::Editor)
			{
				PostFix = LOCTEXT("EditorPostfix", "(Editor)");
			}

			Description = FText::Format(LOCTEXT("WorldFormat", "{0} {1}"), FText::FromString(World->GetFName().GetPlainNameString()), PostFix);	
		}

		return Description;
	}

	TSharedPtr< FOutlinerFilter > CreateSelectedActorFilter()
	{
		auto* Filter = new FOutlinerPredicateFilter(FActorFilterPredicate::CreateStatic([](const AActor* InActor){	return InActor->IsSelected(); }), EDefaultFilterBehaviour::Fail);

		// If anything fails this filter, make it non interactive. We don't want to allow selection of implicitly included parents which might nuke the actor selection.
		Filter->FailedItemState = EFailedFilterState::NonInteractive;
		return MakeShareable( Filter );
	}

	TSharedPtr< FOutlinerFilter > CreateHideTemporaryActorsFilter()
	{
		return MakeShareable( new FOutlinerPredicateFilter( FActorFilterPredicate::CreateStatic( []( const AActor* InActor ){			
			return ((InActor->GetWorld() && InActor->GetWorld()->WorldType != EWorldType::PIE) || GEditor->ObjectsThatExistInEditorWorld.Get(InActor)) && !InActor->HasAnyFlags(EObjectFlags::RF_Transient);
		} ), EDefaultFilterBehaviour::Pass ) );
	}

	TSharedPtr< FOutlinerFilter > CreateIsInCurrentLevelFilter()
	{
		struct FOnlyCurrentLevelFilter : FOutlinerFilter
		{
			FOnlyCurrentLevelFilter() : FOutlinerFilter(EDefaultFilterBehaviour::Fail, EFailedFilterState::Interactive) {}

			virtual bool PassesFilter(const AActor* InActor) const override
			{
				if (InActor->GetWorld())
				{
					return InActor->GetLevel() == InActor->GetWorld()->GetCurrentLevel();
				}
				
				return false;
			}
		};

		return MakeShared<FOnlyCurrentLevelFilter>();
	}

	TSharedPtr< FOutlinerFilter > CreateShowActorComponentsFilter()
	{
		TSharedRef< FOutlinerPredicateFilter > Filter = MakeShared<FOutlinerPredicateFilter>(FActorFilterPredicate::CreateStatic([](const AActor* InActor) {	return InActor!= nullptr; }), EDefaultFilterBehaviour::Fail);
		Filter->ComponentPred = FComponentFilterPredicate::CreateStatic([](const UActorComponent* InComponent) {return Cast<UPrimitiveComponent>(InComponent) != nullptr; });

		// If anything fails this filter, make it non interactive. We don't want to allow selection of implicitly included parents which might nuke the actor selection.
		Filter->FailedItemState = EFailedFilterState::NonInteractive;
		return Filter;
	}

	struct FItemSelection : IMutableTreeItemVisitor
	{
		mutable TArray<FActorTreeItem*> Actors;
		mutable TArray<FWorldTreeItem*> Worlds;
		mutable TArray<FFolderTreeItem*> Folders;
		mutable TArray<FComponentTreeItem*> Components;
		mutable TArray<FSubComponentTreeItem*> SubComponents;

		FItemSelection()
		{}

		FItemSelection(SOutlinerTreeView& Tree)
		{
			for (auto& Item : Tree.GetSelectedItems())
			{
				Item->Visit(*this);
			}
		}

		TArray<TWeakObjectPtr<AActor>> GetWeakActors() const
		{
			TArray<TWeakObjectPtr<AActor>> ActorArray;
			for (const auto* ActorItem : Actors)
			{
				if (ActorItem->Actor.IsValid())
				{
					ActorArray.Add(ActorItem->Actor);
				}
			}
			return ActorArray;
		}

		TArray<AActor*> GetActorPtrs() const
		{
			TArray<AActor*> ActorArray;
			for (const auto* ActorItem : Actors)
			{
				if (AActor* Actor = ActorItem->Actor.Get())
				{
					ActorArray.Add(Actor);
				}
			}

			// if we select a component then we are actually wanting the owning actor to be selected
			for (const auto* ComponentItem : Components)
			{
				if (UActorComponent* ActorComponent = ComponentItem->Component.Get())
				{
					AActor* Actor = ActorComponent->GetOwner();
					ActorArray.Add(Actor);
				}
			}

			// if we select a sub item from within a component then we are actually wanting the owning actor to be selected
			for (const auto* SubItem : SubComponents)
			{
				if (UActorComponent* ActorComponent = SubItem->ParentComponent.Get())
				{
					AActor* Actor = ActorComponent->GetOwner();
					ActorArray.Add(Actor);
				}
			}

			return ActorArray;
		}

	private:
		virtual void Visit(FActorTreeItem& ActorItem) const override
		{
			Actors.Add(&ActorItem);
		}
		virtual void Visit(FWorldTreeItem& WorldItem) const override
		{
			Worlds.Add(&WorldItem);
		}
		virtual void Visit(FFolderTreeItem& FolderItem) const override
		{
			Folders.Add(&FolderItem);
		}
		virtual void Visit(FComponentTreeItem& ComponentItem) const override
		{
			Components.Add(&ComponentItem);
		}
		virtual void Visit(FSubComponentTreeItem& SubComponentItem) const override
		{
			SubComponents.Add(&SubComponentItem);
		}

	};

	void SSceneOutliner::Construct( const FArguments& InArgs, const FInitializationOptions& InInitOptions )
	{
		// Copy over the shared data from the initialization options
		static_cast<FSharedDataBase&>(*SharedData) = static_cast<const FSharedDataBase&>(InInitOptions);

		SelectionMode = (SharedData->Mode == ESceneOutlinerMode::ActorBrowsing) ? ESelectionMode::Multi : ESelectionMode::Single;

		OnItemPicked = InArgs._OnItemPickedDelegate;

		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		if (InInitOptions.OnSelectionChanged.IsBound())
		{
			FSceneOutlinerDelegates::Get().SelectionChanged.Add(InInitOptions.OnSelectionChanged);
		}
		PRAGMA_ENABLE_DEPRECATION_WARNINGS

		bFullRefresh = true;
		bNeedsRefresh = true;
		bNeedsColumRefresh = true;
		bIsReentrant = false;
		bSortDirty = true;
		bActorSelectionDirty = SharedData->Mode == ESceneOutlinerMode::ActorBrowsing;
		FilteredActorCount = 0;
		SortOutlinerTimer = 0.0f;
		bPendingFocusNextFrame = InInitOptions.bFocusSearchBoxWhenOpened;

		// Use the variable for the User Chosen World to enforce the Specified World To Display
		if ( InInitOptions.SpecifiedWorldToDisplay )
		{
			SharedData->UserChosenWorld = InInitOptions.SpecifiedWorldToDisplay;
		}

		SortByColumn = FBuiltInColumnTypes::Label();
		SortMode = EColumnSortMode::Ascending;

		// @todo outliner: Should probably save this in layout!
		// @todo outliner: Should save spacing for list view in layout

		NoBorder = FEditorStyle::GetBrush( "LevelViewport.NoViewportBorder" );
		PlayInEditorBorder = FEditorStyle::GetBrush( "LevelViewport.StartingPlayInEditorBorder" );
		SimulateBorder = FEditorStyle::GetBrush( "LevelViewport.StartingSimulateBorder" );

		//Setup the SearchBox filter
		{
			auto Delegate = TreeItemTextFilter::FItemToStringArray::CreateSP( this, &SSceneOutliner::PopulateSearchStrings );
			SearchBoxFilter = MakeShareable( new TreeItemTextFilter( Delegate ) );
		}

		TSharedRef<SVerticalBox> VerticalBox = SNew(SVerticalBox);

		// We use the filter collection provided, otherwise we create our own
		Filters = InInitOptions.Filters.IsValid() ? InInitOptions.Filters : MakeShareable(new FOutlinerFilters);
	
		// Add additional filters
		if (SharedData->Mode == ESceneOutlinerMode::ActorBrowsing)
		{
			FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::LoadModuleChecked< FSceneOutlinerModule >("SceneOutliner");

			for (auto& OutlinerFilterInfo : SceneOutlinerModule.OutlinerFilterInfoMap)
			{
				OutlinerFilterInfo.Value.InitFilter(Filters);
			}
		}

		SearchBoxFilter->OnChanged().AddSP( this, &SSceneOutliner::FullRefresh );
		Filters->OnChanged().AddSP( this, &SSceneOutliner::FullRefresh );

		//Apply filters based on global preferences
		if (SharedData->Mode == ESceneOutlinerMode::ActorBrowsing)
		{
			ApplyShowOnlySelectedFilter( IsShowingOnlySelected() );
			ApplyHideTemporaryActorsFilter( IsHidingTemporaryActors() );
			ApplyShowOnlyCurrentLevelFilter( IsShowingOnlyCurrentLevel() );
		}

		HeaderRowWidget =
			SNew( SHeaderRow )
				// Only show the list header if the user configured the outliner for that
				.Visibility(InInitOptions.bShowHeaderRow ? EVisibility::Visible : EVisibility::Collapsed);

		SetupColumns(*HeaderRowWidget);

		ChildSlot
		[
			SNew( SBorder )
			.BorderImage( this, &SSceneOutliner::OnGetBorderBrush )
			.BorderBackgroundColor( this, &SSceneOutliner::OnGetBorderColorAndOpacity )
			.ShowEffectWhenDisabled( false )
			[
				VerticalBox
			]
		];

		auto Toolbar = SNew(SHorizontalBox);

		Toolbar->AddSlot()
		.VAlign(VAlign_Center)
		[
			SAssignNew( FilterTextBoxWidget, SSearchBox )
			.Visibility( InInitOptions.bShowSearchBox ? EVisibility::Visible : EVisibility::Collapsed )
			.HintText( LOCTEXT( "FilterSearch", "Search..." ) )
			.ToolTipText( LOCTEXT("FilterSearchHint", "Type here to search (pressing enter selects the results)") )
			.OnTextChanged( this, &SSceneOutliner::OnFilterTextChanged )
			.OnTextCommitted( this, &SSceneOutliner::OnFilterTextCommitted )
		];

		if (SharedData->Mode == ESceneOutlinerMode::ActorBrowsing && InInitOptions.bShowCreateNewFolder)
		{
			Toolbar->AddSlot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				.Padding(4.f, 0.f, 0.f, 0.f)
				[
					SNew(SButton)
					.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
					.ToolTipText(LOCTEXT("CreateFolderToolTip", "Create a new folder containing the current actor selection"))
					.OnClicked(this, &SSceneOutliner::OnCreateFolderClicked)
					[
						SNew(SImage)
						.Image(FEditorStyle::GetBrush("SceneOutliner.NewFolderIcon"))
					]
				];
		}

		VerticalBox->AddSlot()
		.AutoHeight()
		.Padding( 0.0f, 0.0f, 0.0f, 4.0f )
		[
			Toolbar
		];

		VerticalBox->AddSlot()
		.FillHeight( 1.0f )
		[
			SNew( SOverlay )
			+SOverlay::Slot()
			.HAlign( HAlign_Center )
			[
				SNew( STextBlock )
				.Visibility( this, &SSceneOutliner::GetEmptyLabelVisibility )
				.Text( LOCTEXT( "EmptyLabel", "Empty" ) )
				.ColorAndOpacity( FLinearColor( 0.4f, 1.0f, 0.4f ) )
			]

			+SOverlay::Slot()
			[
				SAssignNew( OutlinerTreeView, SOutlinerTreeView, StaticCastSharedRef<SSceneOutliner>(AsShared()) )

				// multi-select if we're in browsing mode, 
				// single-select if we're in picking mode,
				.SelectionMode( this, &SSceneOutliner::GetSelectionMode )

				// Point the tree to our array of root-level items.  Whenever this changes, we'll call RequestTreeRefresh()
				.TreeItemsSource( &RootTreeItems )

				// Find out when the user selects something in the tree
				.OnSelectionChanged( this, &SSceneOutliner::OnOutlinerTreeSelectionChanged )

				// Called when the user double-clicks with LMB on an item in the list
				.OnMouseButtonDoubleClick( this, &SSceneOutliner::OnOutlinerTreeDoubleClick )

				// Called when an item is scrolled into view
				.OnItemScrolledIntoView( this, &SSceneOutliner::OnOutlinerTreeItemScrolledIntoView )

				// Called when an item is expanded or collapsed
				.OnExpansionChanged(this, &SSceneOutliner::OnItemExpansionChanged)

				// Called to child items for any given parent item
				.OnGetChildren( this, &SSceneOutliner::OnGetChildrenForOutlinerTree )

				// Generates the actual widget for a tree item
				.OnGenerateRow( this, &SSceneOutliner::OnGenerateRowForOutlinerTree ) 

				// Use the level viewport context menu as the right click menu for tree items
				.OnContextMenuOpening(this, &SSceneOutliner::OnOpenContextMenu)

				// Header for the tree
				.HeaderRow( HeaderRowWidget )

				// Called when an item is expanded or collapsed with the shift-key pressed down
				.OnSetExpansionRecursive(this, &SSceneOutliner::SetItemExpansionRecursive)

				// Make it easier to see hierarchies when there are a lot of items
				.HighlightParentNodesForSelection(true)
			]
		];

		// Separator
		if ( SharedData->Mode == ESceneOutlinerMode::ActorBrowsing || !InInitOptions.SpecifiedWorldToDisplay )
		{
			VerticalBox->AddSlot()
			.AutoHeight()
			.Padding(0, 0, 0, 1)
			[
				SNew(SSeparator)
			];
		}

		if (SharedData->Mode == ESceneOutlinerMode::ActorBrowsing)
		{
			// Bottom panel
			VerticalBox->AddSlot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				// Asset count
				+SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				.Padding(8, 0)
				[
					SNew( STextBlock )
					.Text( this, &SSceneOutliner::GetFilterStatusText )
					.ColorAndOpacity( this, &SSceneOutliner::GetFilterStatusTextColor )
				]

				// View mode combo button
				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SAssignNew( ViewOptionsComboButton, SComboButton )
					.ContentPadding(0)
					.ForegroundColor( this, &SSceneOutliner::GetViewButtonForegroundColor )
					.ButtonStyle( FEditorStyle::Get(), "ToggleButton" ) // Use the tool bar item style for this button
					.OnGetMenuContent( this, &SSceneOutliner::GetViewButtonContent, false, !InInitOptions.SpecifiedWorldToDisplay)
					.ButtonContent()
					[
						SNew(SHorizontalBox)

						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SImage).Image( FEditorStyle::GetBrush("GenericViewButton") )
						]

						+SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(2, 0, 0, 0)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text( LOCTEXT("ViewButton", "View Options") )
						]
					]
				]
			];
		} //end if (SharedData->Mode == ESceneOutlinerMode::ActorBrowsing)
		else if ( !InInitOptions.SpecifiedWorldToDisplay )
		{
			// Bottom panel
			VerticalBox->AddSlot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				// World picker combo button
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.HAlign(HAlign_Right)
				[
					SAssignNew( ViewOptionsComboButton, SComboButton )
					.ContentPadding(0)
					.ForegroundColor( this, &SSceneOutliner::GetViewButtonForegroundColor )
					.ButtonStyle( FEditorStyle::Get(), "ToggleButton" ) // Use the tool bar item style for this button
					.OnGetMenuContent( this, &SSceneOutliner::GetViewButtonContent, true, !InInitOptions.SpecifiedWorldToDisplay)
					.ButtonContent()
					[
						SNew(SHorizontalBox)

						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SImage).Image( FEditorStyle::GetBrush("SceneOutliner.World") )
						]

						+SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(2, 0, 0, 0)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text( LOCTEXT("ChooseWorldMenu", "Choose World") )
						]
					]
				]
			];
		}


		// Don't allow tool-tips over the header
		HeaderRowWidget->EnableToolTipForceField( true );


		// Populate our data set
		Populate();

		// We only synchronize selection when in actor browsing mode
		if (SharedData->Mode == ESceneOutlinerMode::ActorBrowsing)
		{
			// Populate and register to find out when the level's selection changes
			OnLevelSelectionChanged( NULL );
			USelection::SelectionChangedEvent.AddRaw(this, &SSceneOutliner::OnLevelSelectionChanged);
			USelection::SelectObjectEvent.AddRaw(this, &SSceneOutliner::OnLevelSelectionChanged);

			// Capture selection changes of bones from mesh selection in fracture tools
			FSceneOutlinerDelegates::Get().OnComponentSelectionChanged.AddRaw(this, &SSceneOutliner::OnComponentSelectionChanged);
			FSceneOutlinerDelegates::Get().OnComponentsUpdated.AddRaw(this, &SSceneOutliner::OnComponentsUpdated);
		}

		// Register to find out when actors are added or removed
		// @todo outliner: Might not catch some cases (see: CALLBACK_ActorPropertiesChange, CALLBACK_LayerChange, CALLBACK_LevelDirtied, CALLBACK_OnActorMoved, CALLBACK_UpdateLevelsForAllActors)
		FEditorDelegates::MapChange.AddSP( this, &SSceneOutliner::OnMapChange );
		FEditorDelegates::NewCurrentLevel.AddSP( this, &SSceneOutliner::OnNewCurrentLevel );
		GEngine->OnLevelActorListChanged().AddSP( this, &SSceneOutliner::OnLevelActorListChanged );
		FWorldDelegates::LevelAddedToWorld.AddSP( this, &SSceneOutliner::OnLevelAdded );
		FWorldDelegates::LevelRemovedFromWorld.AddSP( this, &SSceneOutliner::OnLevelRemoved );

		GEngine->OnLevelActorAdded().AddSP( this, &SSceneOutliner::OnLevelActorsAdded );
		GEngine->OnLevelActorDetached().AddSP( this, &SSceneOutliner::OnLevelActorsDetached );
		GEngine->OnLevelActorFolderChanged().AddSP( this, &SSceneOutliner::OnLevelActorFolderChanged );

		GEngine->OnLevelActorDeleted().AddSP( this, &SSceneOutliner::OnLevelActorsRemoved );
		GEngine->OnLevelActorAttached().AddSP( this, &SSceneOutliner::OnLevelActorsAttached );

		GEngine->OnLevelActorRequestRename().AddSP( this, &SSceneOutliner::OnLevelActorsRequestRename );

		// Register to update when an undo/redo operation has been called to update our list of actors
		GEditor->RegisterForUndo( this );

		// Register to be notified when properties are edited
		FCoreDelegates::OnActorLabelChanged.AddRaw(this, &SSceneOutliner::OnActorLabelChanged);
		FCoreUObjectDelegates::OnPackageReloaded.AddRaw(this, &SSceneOutliner::OnAssetReloaded);

		auto& Folders = FActorFolders::Get();
		Folders.OnFolderCreate.AddSP(this, &SSceneOutliner::OnBroadcastFolderCreate);
		Folders.OnFolderMove.AddSP(this, &SSceneOutliner::OnBroadcastFolderMove);
		Folders.OnFolderDelete.AddSP(this, &SSceneOutliner::OnBroadcastFolderDelete);

		if ( SharedData->Mode == ESceneOutlinerMode::ActorBrowsing )
		{
			// Only the actor browsing mode seems to need those
			FEditorDelegates::OnEditCutActorsBegin.AddSP(this, &SSceneOutliner::OnEditCutActorsBegin);
			FEditorDelegates::OnEditCutActorsEnd.AddSP(this, &SSceneOutliner::OnEditCutActorsEnd);
			FEditorDelegates::OnEditCopyActorsBegin.AddSP(this, &SSceneOutliner::OnEditCopyActorsBegin);
			FEditorDelegates::OnEditCopyActorsEnd.AddSP(this, &SSceneOutliner::OnEditCopyActorsEnd);
			FEditorDelegates::OnEditPasteActorsBegin.AddSP(this, &SSceneOutliner::OnEditPasteActorsBegin);
			FEditorDelegates::OnEditPasteActorsEnd.AddSP(this, &SSceneOutliner::OnEditPasteActorsEnd);
			FEditorDelegates::OnDuplicateActorsBegin.AddSP(this, &SSceneOutliner::OnDuplicateActorsBegin);
			FEditorDelegates::OnDuplicateActorsEnd.AddSP(this, &SSceneOutliner::OnDuplicateActorsEnd);
			FEditorDelegates::OnDeleteActorsBegin.AddSP(this, &SSceneOutliner::OnDeleteActorsBegin);
			FEditorDelegates::OnDeleteActorsEnd.AddSP(this, &SSceneOutliner::OnDeleteActorsEnd);
		}

		SetUseSharedSceneOutlinerSettings(SharedData->Mode == ESceneOutlinerMode::Custom);
		OnSharedSettingChangedDelegate.AddSP(this, &SSceneOutliner::OnSharedSettingChanged);
	}

	void SSceneOutliner::SetupColumns(SHeaderRow& HeaderRow)
	{
		FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::LoadModuleChecked<FSceneOutlinerModule>("SceneOutliner");

		if (SharedData->ColumnMap.Num() == 0)
		{
			SharedData->UseDefaultColumns();
		}

		Columns.Empty(SharedData->ColumnMap.Num());
		HeaderRow.ClearColumns();

		// Get a list of sorted columns IDs to create
		TArray<FName> SortedIDs;
		SortedIDs.Reserve(SharedData->ColumnMap.Num());
		SharedData->ColumnMap.GenerateKeyArray(SortedIDs);

		SortedIDs.Sort([&](const FName& A, const FName& B){
			return SharedData->ColumnMap[A].PriorityIndex < SharedData->ColumnMap[B].PriorityIndex;
		});

		for (const FName& ID : SortedIDs)
		{
			if (SharedData->ColumnMap[ID].Visibility == EColumnVisibility::Invisible)
			{
				continue;
			}

			TSharedPtr<ISceneOutlinerColumn> Column;

			if (SharedData->ColumnMap[ID].Factory.IsBound())
			{
				Column = SharedData->ColumnMap[ID].Factory.Execute(*this);
			}
			else
			{
				Column = SceneOutlinerModule.FactoryColumn(ID, *this);
			}

			if (ensure(Column.IsValid()))
			{
				check(Column->GetColumnID() == ID);
				Columns.Add(Column->GetColumnID(), Column);

				auto ColumnArgs = Column->ConstructHeaderRowColumn();

				if (Column->SupportsSorting())
				{
					ColumnArgs
						.SortMode(this, &SSceneOutliner::GetColumnSortMode, Column->GetColumnID())
						.OnSort(this, &SSceneOutliner::OnColumnSortModeChanged);
				}

				HeaderRow.AddColumn(ColumnArgs);
			}
		}

		Columns.Shrink();
		bNeedsColumRefresh = false;
	}

	void SSceneOutliner::RefreshColums()
	{
		bNeedsColumRefresh = true;
	}

	SSceneOutliner::~SSceneOutliner()
	{
		FSceneOutlinerDelegates::Get().OnComponentSelectionChanged.RemoveAll(this);
		FSceneOutlinerDelegates::Get().OnComponentsUpdated.RemoveAll(this);

		// We only synchronize selection when in actor browsing mode
		if( SharedData->Mode == ESceneOutlinerMode::ActorBrowsing )
		{
			USelection::SelectionChangedEvent.RemoveAll(this);
			USelection::SelectObjectEvent.RemoveAll(this);
		}
		FEditorDelegates::MapChange.RemoveAll( this );
		FEditorDelegates::NewCurrentLevel.RemoveAll( this );

		if(GEngine)
		{
			GEngine->OnLevelActorListChanged().RemoveAll(this);
			GEditor->UnregisterForUndo(this);
		}

		SearchBoxFilter->OnChanged().RemoveAll( this );
		Filters->OnChanged().RemoveAll( this );
		
		FWorldDelegates::LevelAddedToWorld.RemoveAll( this );
		FWorldDelegates::LevelRemovedFromWorld.RemoveAll( this );

		FCoreDelegates::OnActorLabelChanged.RemoveAll(this);
		FCoreUObjectDelegates::OnPackageReloaded.RemoveAll(this);

		if (FActorFolders::IsAvailable())
		{
			auto& Folders = FActorFolders::Get();
			Folders.OnFolderCreate.RemoveAll(this);
			Folders.OnFolderMove.RemoveAll(this);
			Folders.OnFolderDelete.RemoveAll(this);
		}

		FEditorDelegates::OnEditCutActorsBegin.RemoveAll(this);
		FEditorDelegates::OnEditCutActorsEnd.RemoveAll(this);
		FEditorDelegates::OnEditCopyActorsBegin.RemoveAll(this);
		FEditorDelegates::OnEditCopyActorsEnd.RemoveAll(this);
		FEditorDelegates::OnEditPasteActorsBegin.RemoveAll(this);
		FEditorDelegates::OnEditPasteActorsEnd.RemoveAll(this);
		FEditorDelegates::OnDuplicateActorsBegin.RemoveAll(this);
		FEditorDelegates::OnDuplicateActorsEnd.RemoveAll(this);
		FEditorDelegates::OnDeleteActorsBegin.RemoveAll(this);
		FEditorDelegates::OnDeleteActorsEnd.RemoveAll(this);
	}

	void SSceneOutliner::OnItemAdded(const FTreeItemID& ItemID, uint8 ActionMask)
	{
		NewItemActions.Add(ItemID, ActionMask);
	}

	FSlateColor SSceneOutliner::GetViewButtonForegroundColor() const
	{
		static const FName InvertedForegroundName("InvertedForeground");
		static const FName DefaultForegroundName("DefaultForeground");

		return ViewOptionsComboButton->IsHovered() ? FEditorStyle::GetSlateColor(InvertedForegroundName) : FEditorStyle::GetSlateColor(DefaultForegroundName);
	}

	TSharedRef<SWidget> SSceneOutliner::GetViewButtonContent(bool bWorldPickerOnly, bool bShouldDisplayChooseWorld)
	{
		FMenuBuilder MenuBuilder(!bWorldPickerOnly, NULL);

		if(bWorldPickerOnly)
		{
			BuildWorldPickerContent(MenuBuilder);
		}
		else
		{
			MenuBuilder.BeginSection("AssetThumbnails", LOCTEXT("ShowHeading", "Show"));
			{
				MenuBuilder.AddMenuEntry(
					LOCTEXT("ToggleShowOnlySelected", "Only Selected"),
					LOCTEXT("ToggleShowOnlySelectedToolTip", "When enabled, only displays actors that are currently selected."),
					FSlateIcon(),
					FUIAction(
					FExecuteAction::CreateSP( this, &SSceneOutliner::ToggleShowOnlySelected ),
					FCanExecuteAction(),
					FIsActionChecked::CreateSP( this, &SSceneOutliner::IsShowingOnlySelected )
					),
					NAME_None,
					EUserInterfaceActionType::ToggleButton
				);

				MenuBuilder.AddMenuEntry(
					LOCTEXT("ToggleHideTemporaryActors", "Hide Temporary Actors"),
					LOCTEXT("ToggleHideTemporaryActorsToolTip", "When enabled, hides temporary/run-time Actors."),
					FSlateIcon(),
					FUIAction(
					FExecuteAction::CreateSP( this, &SSceneOutliner::ToggleHideTemporaryActors ),
					FCanExecuteAction(),
					FIsActionChecked::CreateSP( this, &SSceneOutliner::IsHidingTemporaryActors )
					),
					NAME_None,
					EUserInterfaceActionType::ToggleButton
				);

				MenuBuilder.AddMenuEntry(
					LOCTEXT("ToggleShowOnlyCurrentLevel", "Only in Current Level"),
					LOCTEXT("ToggleShowOnlyCurrentLevelToolTip", "When enabled, only shows Actors that are in the Current Level."),
					FSlateIcon(),
					FUIAction(
					FExecuteAction::CreateSP( this, &SSceneOutliner::ToggleShowOnlyCurrentLevel ),
					FCanExecuteAction(),
					FIsActionChecked::CreateSP( this, &SSceneOutliner::IsShowingOnlyCurrentLevel )
					),
					NAME_None,
					EUserInterfaceActionType::ToggleButton
				);

				// Temporarily disable this feature until it can be redesigned.
				//MenuBuilder.AddMenuEntry(
				//	LOCTEXT("ToggleHideFoldersContainingHiddenActors", "Hide Folders with Only Hidden Actors"),
				//	LOCTEXT("ToggleHideFoldersContainingHiddenActorsToolTip", "When enabled, only shows Folders containing non-hidden Actors."),
				//	FSlateIcon(),
				//	FUIAction(
				//		FExecuteAction::CreateSP(this, &SSceneOutliner::ToggleHideFoldersContainingOnlyHiddenActors),
				//		FCanExecuteAction(),
				//		FIsActionChecked::CreateSP(this, &SSceneOutliner::IsHidingFoldersContainingOnlyHiddenActors)
				//	),
				//	NAME_None,
				//	EUserInterfaceActionType::ToggleButton
				//);

				MenuBuilder.AddMenuEntry(
					LOCTEXT("ToggleShowActorComponents", "Show Actor Components"),
					LOCTEXT("ToggleShowActorComponentsToolTip", "When enabled, shows components belonging to actors."),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &SSceneOutliner::ToggleShowActorComponents),
						FCanExecuteAction(),
						FIsActionChecked::CreateSP(this, &SSceneOutliner::IsShowingActorComponents)
					),
					NAME_None,
					EUserInterfaceActionType::ToggleButton
				);

				// Add additional filters
				FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::LoadModuleChecked< FSceneOutlinerModule >("SceneOutliner");
			
				for (auto& OutlinerFilterInfo : SceneOutlinerModule.OutlinerFilterInfoMap)
				{
					OutlinerFilterInfo.Value.AddMenu(MenuBuilder);
				}
			}
			MenuBuilder.EndSection();

			if( bShouldDisplayChooseWorld )
			{
				MenuBuilder.BeginSection("AssetThumbnails", LOCTEXT("ShowWorldHeading", "World"));
				{
					MenuBuilder.AddSubMenu(
						LOCTEXT("ChooseWorldSubMenu", "Choose World"),
						LOCTEXT("ChooseWorldSubMenuToolTip", "Choose the world to display in the outliner."),
						FNewMenuDelegate::CreateSP(this, &SSceneOutliner::BuildWorldPickerContent)
					);
				}
				MenuBuilder.EndSection();
			}
		}

		return MenuBuilder.MakeWidget();
	}

	void SSceneOutliner::BuildWorldPickerContent(FMenuBuilder& MenuBuilder)
	{
		MenuBuilder.BeginSection("Worlds", LOCTEXT("WorldsHeading", "Worlds"));
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("AutoWorld", "Auto"),
				LOCTEXT("AutoWorldToolTip", "Automatically pick the world to display based on context."),
				FSlateIcon(),
				FUIAction(
				FExecuteAction::CreateSP( this, &SSceneOutliner::OnSelectWorld, TWeakObjectPtr<UWorld>() ),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP( this, &SSceneOutliner::IsWorldChecked, TWeakObjectPtr<UWorld>() )
				),
				NAME_None,
				EUserInterfaceActionType::RadioButton
			);

			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (World && (World->WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Editor))
				{
					MenuBuilder.AddMenuEntry(
						GetWorldDescription(World),
						LOCTEXT("ChooseWorldToolTip", "Display actors for this world."),
						FSlateIcon(),
						FUIAction(
						FExecuteAction::CreateSP( this, &SSceneOutliner::OnSelectWorld, MakeWeakObjectPtr(World) ),
						FCanExecuteAction(),
						FIsActionChecked::CreateSP( this, &SSceneOutliner::IsWorldChecked, MakeWeakObjectPtr(World) )
						),
						NAME_None,
						EUserInterfaceActionType::RadioButton
					);
				}
			}
		}
		MenuBuilder.EndSection();
	}

	/** FILTERS */

	void SSceneOutliner::OnSharedSettingChanged()
	{
		// Only update if we use the shared settings
		if (!SceneOutlinerSettings)
		{
			ApplyHideTemporaryActorsFilter(IsHidingTemporaryActors());
			ApplyShowActorComponentsFilter(IsShowingActorComponents());
			ApplyShowOnlyCurrentLevelFilter(IsShowingOnlyCurrentLevel());
			ApplyShowOnlySelectedFilter(IsShowingOnlySelected());
		}
	}

	// Show Only Selected
	void SSceneOutliner::ToggleShowOnlySelected()
	{
		const bool bEnableFlag = !IsShowingOnlySelected();

		if (SceneOutlinerSettings)
		{
			SceneOutlinerSettings->bShowOnlySelectedActors = bEnableFlag;
		}
		else
		{
			USceneOutlinerSettings* Settings = GetMutableDefault<USceneOutlinerSettings>();
			Settings->bShowOnlySelectedActors = bEnableFlag;
			Settings->PostEditChange();
			OnSharedSettingChangedDelegate.Broadcast();
		}

		ApplyShowOnlySelectedFilter(bEnableFlag);
	}

	void SSceneOutliner::ApplyShowOnlySelectedFilter(bool bShowOnlySelected)
	{
		if ( !SelectedActorFilter.IsValid() )
		{
			SelectedActorFilter = CreateSelectedActorFilter();
		}

		if ( bShowOnlySelected )
		{			
			Filters->Add( SelectedActorFilter );
		}
		else
		{
			Filters->Remove( SelectedActorFilter );
		}
	}

	bool SSceneOutliner::IsShowingOnlySelected() const
	{
		if (SceneOutlinerSettings)
		{
			return SceneOutlinerSettings->bShowOnlySelectedActors;
		}
		return GetDefault<USceneOutlinerSettings>()->bShowOnlySelectedActors;
	}

	// Hide Temporary Actors
	void SSceneOutliner::ToggleHideTemporaryActors()
	{
		const bool bEnableFlag = !IsHidingTemporaryActors();

		if (SceneOutlinerSettings)
		{
			SceneOutlinerSettings->bHideTemporaryActors = bEnableFlag;
		}
		else
		{
			USceneOutlinerSettings* Settings = GetMutableDefault<USceneOutlinerSettings>();
			Settings->bHideTemporaryActors = bEnableFlag;
			Settings->PostEditChange();
			OnSharedSettingChangedDelegate.Broadcast();
		}

		ApplyHideTemporaryActorsFilter(bEnableFlag);
	}

	void SSceneOutliner::ApplyHideTemporaryActorsFilter(bool bHideTemporaryActors)
	{
		if ( !HideTemporaryActorsFilter.IsValid() )
		{
			HideTemporaryActorsFilter = CreateHideTemporaryActorsFilter();
		}

		if ( bHideTemporaryActors )
		{			
			Filters->Add( HideTemporaryActorsFilter );
		}
		else
		{
			Filters->Remove( HideTemporaryActorsFilter );
		}
	}
	bool SSceneOutliner::IsHidingTemporaryActors() const
	{
		if (SceneOutlinerSettings)
		{
			return SceneOutlinerSettings->bHideTemporaryActors;
		}
		return GetDefault<USceneOutlinerSettings>()->bHideTemporaryActors;
	}

	// Show Only Actors In Current Level
	void SSceneOutliner::ToggleShowOnlyCurrentLevel()
	{
		const bool bEnableFlag = !IsShowingOnlyCurrentLevel();

		if (SceneOutlinerSettings)
		{
			SceneOutlinerSettings->bShowOnlyActorsInCurrentLevel = bEnableFlag;
		}
		else
		{
			USceneOutlinerSettings* Settings = GetMutableDefault<USceneOutlinerSettings>();
			Settings->bShowOnlyActorsInCurrentLevel = bEnableFlag;
			Settings->PostEditChange();
			OnSharedSettingChangedDelegate.Broadcast();
		}

		ApplyShowOnlyCurrentLevelFilter(bEnableFlag);
	}
	void SSceneOutliner::ApplyShowOnlyCurrentLevelFilter(bool bShowOnlyActorsInCurrentLevel)
	{
		if ( !ShowOnlyActorsInCurrentLevelFilter.IsValid() )
		{
			ShowOnlyActorsInCurrentLevelFilter = CreateIsInCurrentLevelFilter();
		}

		if ( bShowOnlyActorsInCurrentLevel )
		{			
			Filters->Add( ShowOnlyActorsInCurrentLevelFilter );
		}
		else
		{
			Filters->Remove( ShowOnlyActorsInCurrentLevelFilter );
		}
	}

	void SSceneOutliner::ToggleHideFoldersContainingOnlyHiddenActors()
	{
		const bool bEnableFlag = !IsHidingFoldersContainingOnlyHiddenActors();

		USceneOutlinerSettings* Settings = GetMutableDefault<USceneOutlinerSettings>();
		Settings->bHideFoldersContainingHiddenActors = bEnableFlag;
		Settings->PostEditChange();

		FullRefresh();
	}

	bool SSceneOutliner::IsShowingActorComponents() const
	{
		if (SceneOutlinerSettings)
		{
			return SceneOutlinerSettings->bShowActorComponents;
		}
		return SharedData->Mode == ESceneOutlinerMode::ComponentPicker || (GetDefault<USceneOutlinerSettings>()->bShowActorComponents);
	}

	void SSceneOutliner::ToggleShowActorComponents()
	{
		if ( SharedData->Mode != ESceneOutlinerMode::ComponentPicker )
		{
			const bool bEnableFlag = !IsShowingActorComponents();

			if (SceneOutlinerSettings)
			{
				SceneOutlinerSettings->bShowActorComponents = bEnableFlag;
			}
			else
			{
				USceneOutlinerSettings* Settings = GetMutableDefault<USceneOutlinerSettings>();
				Settings->bShowActorComponents = bEnableFlag;
				Settings->PostEditChange();
				OnSharedSettingChangedDelegate.Broadcast();
			}

			ApplyShowActorComponentsFilter(bEnableFlag);
		}
	}

	void SSceneOutliner::ApplyShowActorComponentsFilter(bool bShowActorComponents)
	{
		if (!ShowActorComponentsFilter.IsValid())
		{
			ShowActorComponentsFilter = CreateShowActorComponentsFilter();
		}

		if (bShowActorComponents)
		{
			Filters->Add(ShowActorComponentsFilter);
		}
		else
		{
			Filters->Remove(ShowActorComponentsFilter);
		}
	}

	bool SSceneOutliner::IsShowingOnlyCurrentLevel() const
	{
		if (SceneOutlinerSettings)
		{
			return SceneOutlinerSettings->bShowOnlyActorsInCurrentLevel;
		}
		return GetDefault<USceneOutlinerSettings>()->bShowOnlyActorsInCurrentLevel;
	}

	bool SSceneOutliner::IsHidingFoldersContainingOnlyHiddenActors() const
	{
		// Temporarily disable this feature until it can be redesigned.
		return false;
		//return GetDefault<USceneOutlinerSettings>()->bHideFoldersContainingHiddenActors;
	}

	/** END FILTERS */


	const FSlateBrush* SSceneOutliner::OnGetBorderBrush() const
	{
		if (SharedData->bRepresentingPlayWorld)
		{
			return GEditor->bIsSimulatingInEditor ? SimulateBorder : PlayInEditorBorder;
		}
		else
		{
			return NoBorder;
		}
	}

	FSlateColor SSceneOutliner::OnGetBorderColorAndOpacity() const
	{
		return SharedData->bRepresentingPlayWorld	? FLinearColor(1.0f, 1.0f, 1.0f, 0.6f)
									   				: FLinearColor::Transparent;
	}

	ESelectionMode::Type SSceneOutliner::GetSelectionMode() const
	{
		return SelectionMode;
	}


	void SSceneOutliner::Refresh()
	{
		if (IsHidingFoldersContainingOnlyHiddenActors())
		{
			bFullRefresh = true;
		}
		bNeedsRefresh = true;
	}

	void SSceneOutliner::FullRefresh()
	{
		bFullRefresh = true;
		Refresh();
	}

	void SSceneOutliner::OnLevelActorListChanged()
	{
		bDisableIntermediateSorting = true;
		FullRefresh();
	}

	void SSceneOutliner::Populate()
	{
		// Block events while we clear out the list.  We don't want actors in the level to become deselected
		// while we doing this
 		TGuardValue<bool> ReentrantGuard(bIsReentrant, true);

		SharedData->RepresentingWorld = nullptr;

		// check if the user-chosen world is valid and in the editor contexts
		if(UWorld* UserChosenWorld = SharedData->UserChosenWorld.Get())
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if(UserChosenWorld == Context.World())
				{
					SharedData->RepresentingWorld = UserChosenWorld;
					break;
				}
			}
		}
		
		if(SharedData->RepresentingWorld == nullptr)
		{
			// try to pick the most suitable world context

			// ideally we want a PIE world that is standalone or the first client
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (World && Context.WorldType == EWorldType::PIE)
				{
					if(World->GetNetMode() == NM_Standalone)
					{
						SharedData->RepresentingWorld = World;
						break;
					}
					else if(World->GetNetMode() == NM_Client && Context.PIEInstance == 2)	// Slightly dangerous: assumes server is always PIEInstance = 1;
					{
						SharedData->RepresentingWorld = World;
						break;
					}
				}
			}
		}

		if(SharedData->RepresentingWorld == nullptr)
		{
			// still not world so fallback to old logic where we just prefer PIE over Editor

			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::PIE)
				{
					SharedData->RepresentingWorld = Context.World();
					break;
				}
				else if (Context.WorldType == EWorldType::Editor)
				{
					SharedData->RepresentingWorld = Context.World();
				}
			}
		}
		
		if (!CheckWorld())
		{
			return;
		}

		SharedData->bRepresentingPlayWorld = SharedData->RepresentingWorld->WorldType == EWorldType::PIE;

		// Get a collection of items and folders which were formerly collapsed
		const FParentsExpansionState ExpansionStateInfo = GetParentsExpansionState();

		bool bMadeAnySignificantChanges = false;
		if(bFullRefresh)
		{
			// Clear the selection here - RepopulateEntireTree will reconstruct it.
			OutlinerTreeView->ClearSelection();

			RepopulateEntireTree();

			bMadeAnySignificantChanges = true;
			bFullRefresh = false;
		}

		// Only deal with 500 at a time
		const int32 End = FMath::Min(PendingOperations.Num(), 500);
		for (int32 Index = 0; Index < End; ++Index)
		{
			auto& PendingOp = PendingOperations[Index];
			switch (PendingOp.Type)
			{
				case FPendingTreeOperation::Added:
					bMadeAnySignificantChanges = AddItemToTree(PendingOp.Item) || bMadeAnySignificantChanges;
					break;

				case FPendingTreeOperation::Moved:
					bMadeAnySignificantChanges = true;
					OnItemMoved(PendingOp.Item);
					break;

				case FPendingTreeOperation::Removed:
					bMadeAnySignificantChanges = true;
					RemoveItemFromTree(PendingOp.Item);
					break;

				default:
					check(false);
					break;
			}
		}

		PendingOperations.RemoveAt(0, End);
		SetParentsExpansionState(ExpansionStateInfo);


		for (FName Folder : PendingFoldersSelect)
		{
			if (FTreeItemPtr* Item = TreeItemMap.Find(Folder))
			{
				OutlinerTreeView->SetItemSelection(*Item, true);
			}
		}
		PendingFoldersSelect.Empty();

		// Check if we need to sort because we are finished with the populating operations
		bool bFinalSort = false;
		if (PendingOperations.Num() == 0)
		{
			// We're fully refreshed now.
			NewItemActions.Empty();
			bNeedsRefresh = false;
			if (bDisableIntermediateSorting)
			{
				bDisableIntermediateSorting = false;
				bFinalSort = true;
			}

			HideFoldersContainingOnlyHiddenActors();
		}

		// If we are allowing intermediate sorts and met the conditions, or this is the final sort after all ops are complete
		if ((bMadeAnySignificantChanges && !bDisableIntermediateSorting) || bFinalSort)
		{
			RequestSort();
		}
	}

	bool SSceneOutliner::ShouldShowFolders() const
	{
		return SharedData->Mode == ESceneOutlinerMode::ActorBrowsing || SharedData->bOnlyShowFolders;
	}

	void SSceneOutliner::EmptyTreeItems()
	{
		FilteredActorCount = 0;
		ApplicableActors.Empty();

		PendingOperations.Empty();
		TreeItemMap.Reset();
		PendingTreeItemMap.Empty();

		RootTreeItems.Empty();
	}

	void SSceneOutliner::RepopulateEntireTree()
	{
		// to avoid dependencies Custom Tree Items are accessed via modular features
		TArray<ISceneOutlinerTraversal*> ConstructTreeItemImp = IModularFeatures::Get().GetModularFeatureImplementations<ISceneOutlinerTraversal>("SceneOutlinerTraversal");
		ISceneOutlinerTraversal *CustomImplementation = nullptr;
		if (ConstructTreeItemImp.Num() > 0 && ConstructTreeItemImp[0] != nullptr)
		{
			// As an optimization, since we have only one customisation at present, just grab the one CustomImplementation to mitigate a further for loop inside the actor iterator
			check(ConstructTreeItemImp.Num() < 2);
			CustomImplementation = ConstructTreeItemImp[0];
		}

		EmptyTreeItems();

		ConstructItemFor<FWorldTreeItem>(SharedData->RepresentingWorld);

		if (!SharedData->bOnlyShowFolders)
		{
			// Iterate over every actor in memory. WARNING: This is potentially very expensive!
			for( FActorIterator ActorIt(SharedData->RepresentingWorld); ActorIt; ++ActorIt )
			{
				AActor* Actor = *ActorIt;
				if (Actor && IsActorDisplayable(Actor))
				{
					if (Filters->PassesAllFilters(FActorTreeItem(Actor)))
					{
						ApplicableActors.Emplace(Actor);
					}
					ConstructItemFor<FActorTreeItem>(Actor);

					if (IsShowingActorComponents())
					{
						for (UActorComponent* Component : Actor->GetComponents())
						{
							if (Filters->PassesAllFilters(FComponentTreeItem(Component)))
							{
								bool IsHandled = false;
								if (CustomImplementation)
								{
									IsHandled = CustomImplementation->ConstructTreeItem(*this, Component);
								}
								if (!IsHandled)
								{
									// add the actor's components - default implementation
									ConstructItemFor<FComponentTreeItem>(Component);
								}
							}
						}
					}
				}
			}
		}

		if (!IsShowingOnlySelected() && ShouldShowFolders())
		{
			// Add any folders which might match the current search terms
			for (const auto& Pair : FActorFolders::Get().GetFolderPropertiesForWorld(*SharedData->RepresentingWorld))
			{
				if (!TreeItemMap.Contains(Pair.Key))
				{
					ConstructItemFor<FFolderTreeItem>(Pair.Key);
				}
			}
		}
	}

	void SSceneOutliner::OnChildRemovedFromParent(ITreeItem& Parent)
	{
		if (Parent.Flags.bIsFilteredOut && !Parent.GetChildren().Num())
		{
			// The parent no longer has any children that match the current search terms. Remove it.
			RemoveItemFromTree(Parent.AsShared());
		}
	}

	void SSceneOutliner::OnItemMoved(const FTreeItemRef& Item)
	{
		// Just remove the item if it no longer matches the filters
		if (!Item->Flags.bIsFilteredOut && !SearchBoxFilter->PassesFilter(*Item))
		{
			// This will potentially remove any non-matching, empty parents as well
			RemoveItemFromTree(Item);
		}
		else
		{
			// The item still matches the filters (or has children that do)
			// When an item has been asked to move, it will still reside under its old parent
			FTreeItemPtr Parent = Item->GetParent();
			if (Parent.IsValid())
			{
				Parent->RemoveChild(Item);
				OnChildRemovedFromParent(*Parent);
			}
			else
			{
				RootTreeItems.Remove(Item);
			}

			Parent = EnsureParentForItem(Item);
			if (Parent.IsValid())
			{
				Parent->AddChild(Item);
				OutlinerTreeView->SetItemExpansion(Parent, true);
			}
			else
			{
				RootTreeItems.Add(Item);
			}
		}
	}

	void SSceneOutliner::RemoveItemFromTree(FTreeItemRef InItem)
	{
		if (TreeItemMap.Contains(InItem->GetID()))
		{
			auto Parent = InItem->GetParent();

			if (Parent.IsValid())
			{
				Parent->RemoveChild(InItem);
				OnChildRemovedFromParent(*Parent);
			}
			else
			{
				RootTreeItems.Remove(InItem);
			}

			InItem->Visit(FFunctionalVisitor().Actor([&](const FActorTreeItem& ActorItem){
				if (!ActorItem.Flags.bIsFilteredOut)
				{
					--FilteredActorCount;
				}
			}));

			TreeItemMap.Remove(InItem->GetID());
		}
	}

	FTreeItemPtr SSceneOutliner::EnsureParentForItem(FTreeItemRef Item)
	{
		if (SharedData->bShowParentTree)
		{
			FTreeItemPtr Parent = Item->FindParent(TreeItemMap);
			if (Parent.IsValid())
			{
				return Parent;
			}
			else
			{
				auto NewParent = Item->CreateParent();
				if (NewParent.IsValid())
				{
					NewParent->Flags.bIsFilteredOut = !Filters->TestAndSetInteractiveState(*NewParent) || !SearchBoxFilter->PassesFilter(*NewParent);

					AddUnfilteredItemToTree(NewParent.ToSharedRef());
					return NewParent;
				}
			}
		}

		return nullptr;
	}

	bool SSceneOutliner::AddItemToTree(FTreeItemRef Item)
	{
		const auto ItemID = Item->GetID();

		PendingTreeItemMap.Remove(ItemID);

		FValidateItemBeforeAddingToTree ValidateItemVisitor;
		Item->Visit(ValidateItemVisitor);

		// If a tree item already exists that represents the same data or if the actor is invalid, bail
		if (TreeItemMap.Find(ItemID)  || !ValidateItemVisitor.Result())
		{
			return false;
		}

		// Set the filtered out flag
		Item->Flags.bIsFilteredOut = !SearchBoxFilter->PassesFilter(*Item);

		if (!Item->Flags.bIsFilteredOut)
		{
			AddUnfilteredItemToTree(Item);

			// Check if we need to do anything with this new item
			if (uint8* ActionMask = NewItemActions.Find(ItemID))
			{
				if (*ActionMask & ENewItemAction::Select)
				{
					OutlinerTreeView->ClearSelection();
					OutlinerTreeView->SetItemSelection(Item, true);
				}

				if (*ActionMask & ENewItemAction::Rename && CanExecuteRenameRequest(Item))
				{
					PendingRenameItem = Item;
				}

				if (*ActionMask & (ENewItemAction::ScrollIntoView | ENewItemAction::Rename))
				{
					ScrollItemIntoView(Item);
				}
			}
		}

		return true;
	}

	void SSceneOutliner::AddUnfilteredItemToTree(FTreeItemRef Item)
	{
		Item->SharedData = SharedData;

		FTreeItemPtr Parent = EnsureParentForItem(Item);

		const FTreeItemID ItemID = Item->GetID();
		if(TreeItemMap.Contains(ItemID))
		{
			UE_LOG(LogSceneOutliner, Error, TEXT("(%d | %s) already exists in tree.  Dumping map..."), GetTypeHash(ItemID), *Item->GetDisplayString() );
			for(TPair<FTreeItemID, FTreeItemPtr>& Entry : TreeItemMap)
			{
				UE_LOG(LogSceneOutliner, Log, TEXT("(%d | %s)"), GetTypeHash(Entry.Key), *Entry.Value->GetDisplayString());
			}

			// this is a fatal error
			check(false);
		}

		TreeItemMap.Add(ItemID, Item);

		if (Parent.IsValid())
		{
			Parent->AddChild(Item);
		}
		else
		{
			RootTreeItems.Add(Item);
		}

		if (SharedData->Mode == ESceneOutlinerMode::ActorBrowsing)
		{
			Item->Visit(FOnItemAddedToTree(*this));
		}
		else if (SharedData->Mode == ESceneOutlinerMode::Custom)
		{
			if(TTreeItemGetter<bool>* ShouldSelectNewItem = ShouldSelectNewItemVisitor.Get())
			{
				Item->Visit(*ShouldSelectNewItem);
				if (ShouldSelectNewItem->Result())
				{
					OutlinerTreeView->SetItemSelection(Item, true);
				}
			}
		}
	}

	SSceneOutliner::FParentsExpansionState SSceneOutliner::GetParentsExpansionState() const
	{
		FParentsExpansionState States;
		for (const auto& Pair : TreeItemMap)
		{
			if (Pair.Value->GetChildren().Num())
			{
				States.Add(Pair.Key, Pair.Value->Flags.bIsExpanded);
			}
		}
		return States;
	}

	void SSceneOutliner::SetParentsExpansionState(const FParentsExpansionState& ExpansionStateInfo) const
	{
		for (const auto& Pair : TreeItemMap)
		{
			auto& Item = Pair.Value;
			if (Item->GetChildren().Num())
			{
				const bool* bIsExpanded = ExpansionStateInfo.Find(Pair.Key);
				if (bIsExpanded)
				{
					OutlinerTreeView->SetItemExpansion(Item, *bIsExpanded);
				}
				else
				{
					OutlinerTreeView->SetItemExpansion(Item, Item->Flags.bIsExpanded);
				}
			}
		}
	}

	void SSceneOutliner::HideFoldersContainingOnlyHiddenActors()
	{
		if (IsHidingFoldersContainingOnlyHiddenActors())
		{
			for (const FTreeItemPtr & TreeItem : RootTreeItems)
			{
				HideFoldersContainingOnlyHiddenActors(TreeItem, true);
			}
		}
	}

	bool SSceneOutliner::HideFoldersContainingOnlyHiddenActors(FTreeItemPtr Parent, bool bIsRoot)
	{
		TArray<FTreeItemPtr> ItemsToRemove;

		bool bActorsHidden = true;
		bool bFoldersHidden = true;

		const TArray<TWeakPtr<ITreeItem>>& Children = Parent->GetChildren();

		if (Children.Num())
		{

			for (const TWeakPtr<ITreeItem> & ChildItem : Children)
			{
				FTreeItemPtr TreeItem = ChildItem.Pin();

				TWeakObjectPtr<AActor> TrueTreeActor = StaticCastSharedPtr<FActorTreeItem>(TreeItem)->Actor;

				if (!bIsRoot && TrueTreeActor.IsValid())
				{
					if (bActorsHidden)
					{
						if (!(TrueTreeActor->IsTemporarilyHiddenInEditor()))
						{
							bActorsHidden = false;
						}
					}
				}
				else
				{
					bool bCurrentFolderHidden = HideFoldersContainingOnlyHiddenActors(TreeItem);

					if (bCurrentFolderHidden)
					{
						ItemsToRemove.Add(TreeItem);
					}

					bFoldersHidden = bCurrentFolderHidden & bFoldersHidden;

				}

			}
		}
		else
		{
			return false;
		}

		for (const FTreeItemPtr & Item : ItemsToRemove)
		{
			FTreeItemRef RemoveItem = Item.ToSharedRef();
			Parent->RemoveChild(RemoveItem);
		}

		return bActorsHidden && bFoldersHidden;
	}

	void SSceneOutliner::PopulateSearchStrings(const ITreeItem& Item, TArray< FString >& OutSearchStrings) const
	{
		for (const auto& Pair : Columns)
		{
			Pair.Value->PopulateSearchStrings(Item, OutSearchStrings);
		}
	}

	TArray<FFolderTreeItem*> SSceneOutliner::GetSelectedFolders() const
	{
		return FItemSelection(*OutlinerTreeView).Folders;
	}

	TArray<FName> SSceneOutliner::GetSelectedFolderNames() const
	{
		TArray<FFolderTreeItem*> SelectedFolders = GetSelectedFolders();
		TArray<FName> SelectedFolderNames;
		for (FFolderTreeItem* Folder : SelectedFolders)
		{
			if (Folder)
			{
				SelectedFolderNames.Add(Folder->Path);
			}
		}
		return SelectedFolderNames;
	}

	TSharedPtr<SWidget> SSceneOutliner::OnOpenContextMenu()
	{
		/** Legacy mode and now also used by the custom mode */
		if (SharedData->ContextMenuOverride.IsBound())
		{
			return SharedData->ContextMenuOverride.Execute();
		}

		if (SharedData->Mode == ESceneOutlinerMode::ActorBrowsing)
		{
			TArray<AActor*> SelectedActors;
			GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors);

			// Make sure that no components are selected
			if (GEditor->GetSelectedComponentCount() > 0)
			{
				// We want to be able to undo to regain the previous component selection
				const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "ClickingOnActorsContextMenu", "Clicking on Actors (context menu)"));
				USelection* ComponentSelection = GEditor->GetSelectedComponents();
				ComponentSelection->Modify(false);
				ComponentSelection->DeselectAll();

				GUnrealEd->UpdatePivotLocationForSelection();
				GEditor->RedrawLevelEditingViewports(false);
			}

			return BuildDefaultContextMenu();
		}

		return TSharedPtr<SWidget>();
	}
	
	bool SSceneOutliner::Delete_CanExecute()
	{
		if (SharedData->Mode == ESceneOutlinerMode::ActorPicker)
		{
			FItemSelection ItemSelection(*OutlinerTreeView);
			if (ItemSelection.Folders.Num() > 0 && ItemSelection.Folders.Num() == OutlinerTreeView->GetNumItemsSelected())
			{
				return true;
			}
		}
		return false;
	}

	bool SSceneOutliner::Rename_CanExecute()
	{
		if (SharedData->Mode == ESceneOutlinerMode::ActorBrowsing)
		{
			FItemSelection ItemSelection(*OutlinerTreeView);
			if (ItemSelection.Folders.Num() == 1 && ItemSelection.Folders.Num() == OutlinerTreeView->GetNumItemsSelected())
			{
				return true;
			}
		}
		return false;
	}

	void SSceneOutliner::Rename_Execute()
	{
		FItemSelection ItemSelection(*OutlinerTreeView);
		FTreeItemPtr ItemToRename;

		if (SharedData->Mode == ESceneOutlinerMode::ActorBrowsing)
		{
			// handle folders only here, actors and components are handled in LevelEditorActions::Rename_Execute
			if (ItemSelection.Folders.Num() == 1 && ItemSelection.Folders.Num() == OutlinerTreeView->GetNumItemsSelected())
			{
				ItemToRename = OutlinerTreeView->GetSelectedItems()[0];
			}
		}
		else if (SharedData->Mode == ESceneOutlinerMode::Custom)
		{
			if (OutlinerTreeView->GetNumItemsSelected() == 1)
			{
				ItemToRename = OutlinerTreeView->GetSelectedItems()[0];
			}
		}

		if (ItemToRename.IsValid() && CanExecuteRenameRequest(ItemToRename) && ItemToRename->CanInteract())
		{
			PendingRenameItem = ItemToRename->AsShared();
			ScrollItemIntoView(ItemToRename);
		}
	}

	bool SSceneOutliner::Cut_CanExecute()
	{
		if (SharedData->Mode == ESceneOutlinerMode::ActorBrowsing)
		{
			FItemSelection ItemSelection(*OutlinerTreeView);
			if (ItemSelection.Folders.Num() > 0 && ItemSelection.Folders.Num() == OutlinerTreeView->GetNumItemsSelected())
			{
				return true;
			}
		}
		return false;
	}

	bool SSceneOutliner::Copy_CanExecute()
	{
		if (SharedData->Mode == ESceneOutlinerMode::ActorBrowsing)
		{
			FItemSelection ItemSelection(*OutlinerTreeView);
			if (ItemSelection.Folders.Num() > 0 && ItemSelection.Folders.Num() == OutlinerTreeView->GetNumItemsSelected())
			{
				return true;
			}
		}
		return false;
	}

	bool SSceneOutliner::Paste_CanExecute()
	{
		if (SharedData->Mode == ESceneOutlinerMode::ActorBrowsing)
		{
			if (CanPasteFoldersOnlyFromClipboard())
			{
				return true;
			}
		}
		return false;
	}

	bool SSceneOutliner::CanPasteFoldersOnlyFromClipboard()
	{
		// Intentionally not checking if the level is locked/hidden here, as it's better feedback for the user if they attempt to paste
		// and get the message explaining why it's failed, than just not having the option available to them.
		FString PasteString;
		FPlatformApplicationMisc::ClipboardPaste(PasteString);
		return PasteString.StartsWith("BEGIN FOLDERLIST");
	}

	bool SSceneOutliner::CanSupportDragAndDrop() const
	{
		return SharedData->Mode == ESceneOutlinerMode::ActorBrowsing || SharedData->Mode == ESceneOutlinerMode::Custom;
	}

	bool SSceneOutliner::CanExecuteRenameRequest(const FTreeItemPtr& ItemPtr) const
	{
		if (TTreeItemGetter<bool>* Visitor = CanRenameItemVisitor.Get())
		{
			ItemPtr->Visit(*Visitor);
			return Visitor->Result();
		}

		// Legacy default behavior
		return true;
	}

	int32 SSceneOutliner::AddFilter(const TSharedRef<SceneOutliner::FOutlinerFilter>& Filter)
	{
		// Deal with build in filters. If a build in filter is already in the filters, add will return it's index
		if (Filter == HideTemporaryActorsFilter && !IsHidingTemporaryActors())
		{
			ToggleHideTemporaryActors();
			return Filters->Num() - 1;
		}
		else if (Filter == ShowActorComponentsFilter && !IsShowingActorComponents())
		{
			ToggleShowActorComponents();
			return Filters->Num() - 1;
		}
		else if (Filter == ShowOnlyActorsInCurrentLevelFilter && !IsShowingOnlyCurrentLevel())
		{
			ToggleShowOnlyCurrentLevel();
			return Filters->Num() - 1;
		}
		else if (Filter == SelectedActorFilter && !IsShowingOnlySelected())
		{
			ToggleShowOnlySelected();
			return Filters->Num() - 1;
		}

		// Custom Filter
		return Filters->Add(Filter);
	}

	bool SSceneOutliner::RemoveFilter(const TSharedRef<SceneOutliner::FOutlinerFilter>& Filter)
	{
		bool bRemovedAFilter = false;

		// Deal with build in filters. If a build in filter is already in the filters toggle to update the settings
		if (Filter == HideTemporaryActorsFilter && IsHidingTemporaryActors())
		{
			ToggleHideTemporaryActors();
			bRemovedAFilter = true;
		}
		else if (Filter == ShowActorComponentsFilter && IsShowingActorComponents())
		{
			ToggleShowActorComponents();
			bRemovedAFilter = true;
		}
		else if (Filter == ShowOnlyActorsInCurrentLevelFilter && IsShowingOnlyCurrentLevel())
		{
			ToggleShowOnlyCurrentLevel();
			bRemovedAFilter = true;
		}
		else if (Filter == SelectedActorFilter && IsShowingOnlySelected())
		{
			ToggleShowOnlySelected();
			bRemovedAFilter = true;
		}
		else
		{
			bRemovedAFilter = Filters->Remove(Filter) > 0;
		}

		return bRemovedAFilter;
	}

	TSharedPtr<SceneOutliner::FOutlinerFilter> SSceneOutliner::GetFilterAtIndex(int32 Index)
	{
		return StaticCastSharedPtr<SceneOutliner::FOutlinerFilter>(Filters->GetFilterAtIndex(Index));
	}

	int32 SSceneOutliner::GetFilterCount() const
	{
		return Filters->Num();
	}

	void SSceneOutliner::AddColumn(FName ColumId, const SceneOutliner::FColumnInfo& ColumInfo)
	{
		if (!SharedData->ColumnMap.Contains(ColumId))
		{
			SharedData->ColumnMap.Add(ColumId, ColumInfo);
			RefreshColums();
		}
	}

	void SSceneOutliner::RemoveColumn(FName ColumId)
	{
		if (SharedData->ColumnMap.Contains(ColumId))
		{
			SharedData->ColumnMap.Remove(ColumId);
			RefreshColums();
		}
	}

	TArray<FName> SSceneOutliner::GetColumnIds() const
	{
		TArray<FName> ColumnsName;
		SharedData->ColumnMap.GenerateKeyArray(ColumnsName);
		return ColumnsName;
	}

	ICustomSceneOutliner& SSceneOutliner::SetSelectionMode(ESelectionMode::Type InSelectionMode)
	{
		SelectionMode = InSelectionMode;
		return *this;
	}

	ICustomSceneOutliner& SSceneOutliner::SetCanRenameItem(TUniquePtr<TTreeItemGetter<bool>>&& CanRenameItem)
	{
		CanRenameItemVisitor = MoveTemp(CanRenameItem);
		return *this;
	}

	ICustomSceneOutliner& SSceneOutliner::SetShouldSelectItemWhenAdded(TUniquePtr<TTreeItemGetter<bool>>&& ShouldSelectItemWhenAdded)
	{
		ShouldSelectNewItemVisitor = MoveTemp(ShouldSelectItemWhenAdded);
		return *this;
	}

	ICustomSceneOutliner& SSceneOutliner::SetOnItemDragDetected(TUniqueFunction<FReply(const SceneOutliner::ITreeItem&)> Callback)
	{
		OnItemDragDetected = MoveTemp(Callback);
		return *this;
	}

	ICustomSceneOutliner& SSceneOutliner::SetOnDragOverItem(TUniqueFunction<FReply(const FDragDropEvent&, const SceneOutliner::ITreeItem&)> Callback)
	{
		OnDragOverItem = MoveTemp(Callback);
		return *this;
	}

	ICustomSceneOutliner& SSceneOutliner::SetOnDropOnItem(TUniqueFunction<FReply(const FDragDropEvent&, const SceneOutliner::ITreeItem&)> Callback)
	{
		OnDropOnItem = MoveTemp(Callback);
		return *this;
	}

	ICustomSceneOutliner& SSceneOutliner::SetOnDragEnterItem(TUniqueFunction<void(const FDragDropEvent&, const SceneOutliner::ITreeItem&)> Callback)
	{
		OnDragEnterItem = MoveTemp(Callback);
		return *this;
	}

	ICustomSceneOutliner& SSceneOutliner::SetOnDragLeaveItem(TUniqueFunction<void(const FDragDropEvent&, const SceneOutliner::ITreeItem&)> Callback)
	{
		OnDragLeaveItem = MoveTemp(Callback);
		return *this;
	}


	const TUniqueFunction<FReply(const SceneOutliner::ITreeItem&)>& SSceneOutliner::GetOnItemDragDetected() const
	{
		return OnItemDragDetected;
	}

	const TUniqueFunction<FReply(const FDragDropEvent&, const SceneOutliner::ITreeItem&)>& SSceneOutliner::GetOnDragOverItem() const
	{
		return OnDragOverItem;
	}

	const TUniqueFunction<FReply(const FDragDropEvent&, const SceneOutliner::ITreeItem&)>& SSceneOutliner::GetOnDropOnItem() const
	{
		return OnDropOnItem;
	}

	const TUniqueFunction<void(const FDragDropEvent&, const SceneOutliner::ITreeItem&)>& SSceneOutliner::GetOnDragEnterItem() const
	{
		return OnDragEnterItem;
	}

	const TUniqueFunction<void(const FDragDropEvent&, const SceneOutliner::ITreeItem&)>& SSceneOutliner::GetOnDragLeaveItem() const
	{
		return OnDragLeaveItem;
	}

	ICustomSceneOutliner& SSceneOutliner::SetUseSharedSceneOutlinerSettings(bool bUseSharedSettings)
	{
		if ( bUseSharedSettings && !SceneOutlinerSettings )
		{
			SceneOutlinerSettings = NewObject<USceneOutlinerSettings>();
			ApplyHideTemporaryActorsFilter(IsHidingTemporaryActors());
			ApplyShowActorComponentsFilter(IsShowingActorComponents());
			ApplyShowOnlyCurrentLevelFilter(IsShowingOnlyCurrentLevel());
			ApplyShowOnlySelectedFilter(IsShowingOnlySelected());
		}
		else if ( !bUseSharedSettings && SceneOutlinerSettings )
		{
			SceneOutlinerSettings = nullptr;
			ApplyHideTemporaryActorsFilter(IsHidingTemporaryActors());
			ApplyShowActorComponentsFilter(IsShowingActorComponents());
			ApplyShowOnlyCurrentLevelFilter(IsShowingOnlyCurrentLevel());
			ApplyShowOnlySelectedFilter(IsShowingOnlySelected());
		}
		return *this;
	}

	bool SSceneOutliner::IsUsingSharedSceneOutlinerSettings() const 
	{
		return SceneOutlinerSettings != nullptr;
	}

	ICustomSceneOutliner& SSceneOutliner::SetHideTemporaryActors(bool bHideTemporaryActors)
	{
		if ( bHideTemporaryActors != IsHidingTemporaryActors() )
		{
			ToggleHideTemporaryActors();
		}
		return *this;
	}

	ICustomSceneOutliner& SSceneOutliner::SetShowOnlyCurrentLevel(bool bShowOnlyCurrentLevel)
	{
		if ( bShowOnlyCurrentLevel != IsShowingOnlyCurrentLevel() )
		{
			ToggleShowOnlyCurrentLevel();
		}
		return *this;
	}

	ICustomSceneOutliner& SSceneOutliner::SetShownOnlySelected(bool bShowOnlySelected)
	{
		if ( bShowOnlySelected != IsShowingOnlySelected() )
		{
			ToggleShowOnlySelected();
		}
		return *this;
	}

	ICustomSceneOutliner& SSceneOutliner::SetShowActorComponents(bool bShowActorComponents)
	{
		if ( bShowActorComponents != IsShowingActorComponents() )
		{
			ToggleShowActorComponents();
		}
		return *this;
	}

	void SSceneOutliner::SetSelection(const SceneOutliner::TTreeItemGetter<bool>& ItemSelector)
	{
		TArray<FTreeItemPtr> ItemsToAdd;
		for (const auto& TPairIdAndItem : TreeItemMap)
		{
			FTreeItemPtr ItemPtr = TPairIdAndItem.Value;
			if (ITreeItem*  Item = ItemPtr.Get())
			{
				Item->Visit(ItemSelector);
				if (ItemSelector.Result())
				{
					ItemsToAdd.Add(ItemPtr);
				}
			}
		}
		OutlinerTreeView->ClearSelection();
		OutlinerTreeView->SetItemSelection(ItemsToAdd, true);
	}

	void SSceneOutliner::AddToSelection(const SceneOutliner::TTreeItemGetter<bool>& ItemSelector)
	{
		TArray<FTreeItemPtr> ItemsToAdd;
		for (const auto& TPairIdAndItem : TreeItemMap)
		{
			FTreeItemPtr ItemPtr = TPairIdAndItem.Value;
			if (ITreeItem*  Item = ItemPtr.Get())
			{
				Item->Visit(ItemSelector);
				if (ItemSelector.Result())
				{
					ItemsToAdd.Add(ItemPtr);
				}
			}
		}
		OutlinerTreeView->SetItemSelection(ItemsToAdd, true);
	}

	void SSceneOutliner::RemoveFromSelection(const SceneOutliner::TTreeItemGetter<bool>& ItemDeselector)
	{
		TArray<FTreeItemPtr> ItemsToRemove;
		for (const FTreeItemPtr& ItemPtr : OutlinerTreeView->GetSelectedItems())
		{
			if (ITreeItem* Item = ItemPtr.Get())
			{
				Item->Visit(ItemDeselector);
				if (ItemDeselector.Result())
				{
					ItemsToRemove.Add(ItemPtr);
				}
			}
		}
		OutlinerTreeView->SetItemSelection(ItemsToRemove, false);
	}

	void SSceneOutliner::AddObjectToSelection(const UObject* Object)
	{
		if(FTreeItemPtr* ItemPtr = TreeItemMap.Find(Object))
		{
			OutlinerTreeView->SetItemSelection(*ItemPtr, true);
		}
	}

	void SSceneOutliner::RemoveObjectFromSelection(const UObject* Object)
	{
		if (FTreeItemPtr* ItemPtr = TreeItemMap.Find(Object))
		{
			OutlinerTreeView->SetItemSelection(*ItemPtr, false);
		}
	}

	void SSceneOutliner::AddFolderToSelection(const FName& FolderName)
	{
		if (FTreeItemPtr* ItemPtr = TreeItemMap.Find(FolderName))
		{
			OutlinerTreeView->SetItemSelection(*ItemPtr, true);
		}
	}

	void SSceneOutliner::RemoveFolderFromSelection(const FName& FolderName)
	{
		if (FTreeItemPtr* ItemPtr = TreeItemMap.Find(FolderName))
		{
			OutlinerTreeView->SetItemSelection(*ItemPtr, false);
		}
	}

	void SSceneOutliner::ClearSelection()
	{
		OutlinerTreeView->ClearSelection();
	}

	TSharedPtr<SWidget> SSceneOutliner::BuildDefaultContextMenu()
	{
		if (!CheckWorld())
		{
			return nullptr;
		}

		RegisterDefaultContextMenu();

		FItemSelection ItemSelection(*OutlinerTreeView);

		USceneOutlinerMenuContext* ContextObject = NewObject<USceneOutlinerMenuContext>();
		ContextObject->SceneOutliner = SharedThis(this);
		ContextObject->bShowParentTree = SharedData->bShowParentTree;
		ContextObject->NumSelectedItems = OutlinerTreeView->GetNumItemsSelected();
		ContextObject->NumSelectedFolders = ItemSelection.Folders.Num();
		ContextObject->NumWorldsSelected = ItemSelection.Worlds.Num();
		FToolMenuContext Context(ContextObject);

		// Allow other systems to override menu name and provide additional context
		static const FName DefaultContextMenuName("SceneOutliner.DefaultContextMenu");
		FName MenuName = DefaultContextMenuName;
		SharedData->ModifyContextMenu.ExecuteIfBound(MenuName, Context);

		// Build up the menu for a selection
		UToolMenus* ToolMenus = UToolMenus::Get();
		UToolMenu* Menu = ToolMenus->GenerateMenu(MenuName, Context);

		for (const FToolMenuSection& Section : Menu->Sections)
		{
			if (Section.Blocks.Num() > 0)
			{
				return ToolMenus->GenerateWidget(Menu);
			}
		}

		return nullptr;
	}

	void SSceneOutliner::RegisterDefaultContextMenu()
	{
		static const FName DefaultContextBaseMenuName("SceneOutliner.DefaultContextMenuBase");
		static const FName DefaultContextMenuName("SceneOutliner.DefaultContextMenu");

		UToolMenus* ToolMenus = UToolMenus::Get();

		if (!ToolMenus->IsMenuRegistered(DefaultContextBaseMenuName))
		{
			UToolMenu* Menu = ToolMenus->RegisterMenu(DefaultContextBaseMenuName);

			Menu->AddDynamicSection("DynamicSection1", FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
			{
				USceneOutlinerMenuContext* Context = InMenu->FindContext<USceneOutlinerMenuContext>();
				if (!Context || !Context->SceneOutliner.IsValid())
				{
					return;
				}

				SSceneOutliner* SceneOutliner = Context->SceneOutliner.Pin().Get();
				if (Context->bShowParentTree)
				{
					if (Context->NumSelectedItems == 0)
					{
						InMenu->FindOrAddSection("Section").AddMenuEntry(
							"CreateFolder",
							LOCTEXT("CreateFolder", "Create Folder"),
							FText(),
							FSlateIcon(FEditorStyle::GetStyleSetName(), "SceneOutliner.NewFolderIcon"),
							FUIAction(FExecuteAction::CreateSP(SceneOutliner, &SSceneOutliner::CreateFolder)));
					}
					else
					{
						if (Context->NumSelectedItems == 1)
						{
							SceneOutliner->GetTree().GetSelectedItems()[0]->GenerateContextMenu(InMenu, *SceneOutliner);
						}

						// If we've only got folders selected, show the selection and edit sub menus
						if (Context->NumSelectedItems > 0 && Context->NumSelectedFolders == Context->NumSelectedItems)
						{
							InMenu->FindOrAddSection("Section").AddSubMenu(
								"SelectSubMenu",
								LOCTEXT("SelectSubmenu", "Select"),
								LOCTEXT("SelectSubmenu_Tooltip", "Select the contents of the current selection"),
								FNewToolMenuDelegate::CreateSP(SceneOutliner, &SSceneOutliner::FillSelectionSubMenu));
						}
					}
				}
			}));

			Menu->AddDynamicSection("DynamicMainSection", FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
			{
				// We always create a section here, even if there is no parent so that clients can still extend the menu
				FToolMenuSection& Section = InMenu->AddSection("MainSection");

				if (USceneOutlinerMenuContext* Context = InMenu->FindContext<USceneOutlinerMenuContext>())
				{
					// Don't add any of these menu items if we're not showing the parent tree
					// Can't move worlds or level blueprints
					if (Context->bShowParentTree && Context->NumSelectedItems > 0 && Context->NumWorldsSelected == 0 && Context->SceneOutliner.IsValid())
					{
						Section.AddSubMenu(
							"MoveActorsTo",
							LOCTEXT("MoveActorsTo", "Move To"),
							LOCTEXT("MoveActorsTo_Tooltip", "Move selection to another folder"),
							FNewToolMenuDelegate::CreateSP(Context->SceneOutliner.Pin().Get(), &SSceneOutliner::FillFoldersSubMenu));
					}
				}
			}));
		}

		if (!ToolMenus->IsMenuRegistered(DefaultContextMenuName))
		{
			ToolMenus->RegisterMenu(DefaultContextMenuName, DefaultContextBaseMenuName);
		}
	}

	void SSceneOutliner::FillFoldersSubMenu(UToolMenu* Menu) const
	{
		FToolMenuSection& Section = Menu->AddSection("Section");
		Section.AddMenuEntry("CreateNew", LOCTEXT( "CreateNew", "Create New Folder" ), LOCTEXT( "CreateNew_ToolTip", "Move to a new folder" ),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "SceneOutliner.NewFolderIcon"), FExecuteAction::CreateSP(const_cast<SSceneOutliner*>(this), &SSceneOutliner::CreateFolder));

		AddMoveToFolderOutliner(Menu);
	}

	TSharedRef<TSet<FName>> SSceneOutliner::GatherInvalidMoveToDestinations() const
	{
		// We use a pointer here to save copying the whole array for every invocation of the filter delegate
		TSharedRef<TSet<FName>> ExcludedParents(new TSet<FName>());

		struct FFindInvalidFolders : ITreeItemVisitor
		{
			TSet<FName>& ExcludedParents;
			const TMap<FTreeItemID, FTreeItemPtr>& TreeItemMap;
			FFindInvalidFolders(TSet<FName>& InExcludedParents, const TMap<FTreeItemID, FTreeItemPtr>& InTreeItemMap)
				: ExcludedParents(InExcludedParents), TreeItemMap(InTreeItemMap)
			{}

			static bool ItemHasSubFolders(const TWeakPtr<ITreeItem>& WeakItem)
			{
				bool bHasSubFolder = false;
				WeakItem.Pin()->Visit(FFunctionalVisitor().Folder([&](const FFolderTreeItem&){
					bHasSubFolder = true;
				}));
				return bHasSubFolder;
			}

			virtual void Visit(const FActorTreeItem& ActorItem) const override
			{
				if (AActor* Actor = ActorItem.Actor.Get())
				{
					// We exclude actor parent folders if they don't have any sub folders
					const FName& Folder = Actor->GetFolderPath();
					if (!Folder.IsNone() && !ExcludedParents.Contains(Folder))
					{
						FTreeItemPtr FolderItem = TreeItemMap.FindRef(Folder);
						if (FolderItem.IsValid() && !FolderItem->GetChildren().ContainsByPredicate(&ItemHasSubFolders))
						{
							ExcludedParents.Add(Folder);
						}
					}
				}
			}

			virtual void Visit(const FFolderTreeItem& Folder) const override
			{
				// Cannot move into its parent
				const FName ParentPath = GetParentPath(Folder.Path);
				if (!ParentPath.IsNone())
				{
					ExcludedParents.Add(ParentPath);
				}
				else
				{
					// Failing that, cannot move into itself, or any child
					ExcludedParents.Add(Folder.Path);
				}
			}
		};

		auto Visitor = FFindInvalidFolders(*ExcludedParents, TreeItemMap);
		for (const auto& Item : OutlinerTreeView->GetSelectedItems())
		{
			Item->Visit(Visitor);
		}

		return ExcludedParents;
	}

	void SSceneOutliner::AddMoveToFolderOutliner(UToolMenu* Menu) const
	{
		// We don't show this if there aren't any folders in the world
		if (!FActorFolders::Get().GetFolderPropertiesForWorld(*SharedData->RepresentingWorld).Num())
		{
			return;
		}

		// Add a mini scene outliner for choosing an existing folder
		FInitializationOptions MiniSceneOutlinerInitOptions;
		MiniSceneOutlinerInitOptions.bShowHeaderRow = false;
		MiniSceneOutlinerInitOptions.bFocusSearchBoxWhenOpened = true;
		MiniSceneOutlinerInitOptions.bOnlyShowFolders = true;
		
		// Don't show any folders that are a child of any of the selected folders
		auto ExcludedParents = GatherInvalidMoveToDestinations();
		if (ExcludedParents->Num())
		{
			// Add a filter if necessary
			auto FilterOutChildFolders = [](FName Path, TSharedRef<TSet<FName>> InExcludedParents){
				for (const auto& Parent : *InExcludedParents)
				{
					if (Path == Parent || FActorFolders::PathIsChildOf(Path.ToString(), Parent.ToString()))
					{
						return false;
					}
				}
				return true;
			};

			MiniSceneOutlinerInitOptions.Filters->AddFilterPredicate(FFolderFilterPredicate::CreateStatic(FilterOutChildFolders, ExcludedParents), EDefaultFilterBehaviour::Pass);
		}

		{
			// Filter in/out the world according to whether it is valid to move to/from the root
			FDragDropPayload DraggedObjects(OutlinerTreeView->GetSelectedItems());

			const bool bMoveToRootValid = FFolderDropTarget(FName()).ValidateDrop(DraggedObjects, *SharedData->RepresentingWorld).IsValid();

			MiniSceneOutlinerInitOptions.Filters->AddFilterPredicate(FWorldFilterPredicate::CreateStatic([](const UWorld*, bool bInMoveToRootValid){
				return bInMoveToRootValid;
			}, bMoveToRootValid), EDefaultFilterBehaviour::Pass);
		}

		// Don't show the actor info column
		MiniSceneOutlinerInitOptions.UseDefaultColumns();
		MiniSceneOutlinerInitOptions.ColumnMap.Remove(FBuiltInColumnTypes::ActorInfo());

		// Actor selector to allow the user to choose a folder
		FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::LoadModuleChecked<FSceneOutlinerModule>("SceneOutliner");
		TSharedRef< SWidget > MiniSceneOutliner =
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.MaxHeight(400.0f)
			[
				SNew(SSceneOutliner, MiniSceneOutlinerInitOptions)
				.IsEnabled(FSlateApplication::Get().GetNormalExecutionAttribute())
				.OnItemPickedDelegate(FOnSceneOutlinerItemPicked::CreateSP(const_cast<SSceneOutliner*>(this), &SSceneOutliner::MoveSelectionTo))
			];

		FToolMenuSection& Section = Menu->AddSection(FName(), LOCTEXT("ExistingFolders", "Existing:"));
		Section.AddEntry(FToolMenuEntry::InitWidget(
			"MiniSceneOutliner",
			MiniSceneOutliner,
			FText::GetEmpty(),
			false));
	}

	void SSceneOutliner::FillSelectionSubMenu(UToolMenu* Menu) const
	{
		FToolMenuSection& Section = Menu->AddSection("Section");
		Section.AddMenuEntry(
			"AddChildrenToSelection",
			LOCTEXT( "AddChildrenToSelection", "Immediate Children" ),
			LOCTEXT( "AddChildrenToSelection_ToolTip", "Select all immediate actor children of the selected folders" ),
			FSlateIcon(),
			FExecuteAction::CreateSP(const_cast<SSceneOutliner*>(this), &SSceneOutliner::SelectFoldersDescendants, /*bSelectImmediateChildrenOnly=*/ true));
		Section.AddMenuEntry(
			"AddDescendantsToSelection",
			LOCTEXT( "AddDescendantsToSelection", "All Descendants" ),
			LOCTEXT( "AddDescendantsToSelection_ToolTip", "Select all actor descendants of the selected folders" ),
			FSlateIcon(),
			FExecuteAction::CreateSP(const_cast<SSceneOutliner*>(this), &SSceneOutliner::SelectFoldersDescendants, /*bSelectImmediateChildrenOnly=*/ false));
	}

	void SSceneOutliner::SelectFoldersDescendants(bool bSelectImmediateChildrenOnly)
	{
		struct FExpandFoldersRecursive : IMutableTreeItemVisitor
		{
			SSceneOutliner& Outliner;
			bool bSelectImmediateChildrenOnly;

			FExpandFoldersRecursive(SSceneOutliner& InOutliner, bool bInSelectImmediateChildrenOnly) :
				Outliner(InOutliner), bSelectImmediateChildrenOnly(bInSelectImmediateChildrenOnly) {}

			virtual void Visit(FActorTreeItem& ActorItem) const override
			{
				if (!Outliner.OutlinerTreeView->IsItemExpanded(ActorItem.AsShared()))
				{
					Outliner.OutlinerTreeView->SetItemExpansion(ActorItem.AsShared(), true);
				}

				if (!bSelectImmediateChildrenOnly)
				{
					for (TWeakPtr<ITreeItem> Child : ActorItem.GetChildren())
					{
						Child.Pin()->Visit(*this);
					}
				}
			}

			virtual void Visit(FFolderTreeItem& FolderItem) const override
			{
				if (!Outliner.OutlinerTreeView->IsItemExpanded(FolderItem.AsShared()))
				{
					Outliner.OutlinerTreeView->SetItemExpansion(FolderItem.AsShared(), true);
				}

				if (!bSelectImmediateChildrenOnly)
				{
					for (TWeakPtr<ITreeItem> Child : FolderItem.GetChildren())
					{
						Child.Pin()->Visit(*this);
					}
				}

			}
		};

		struct FSelectActorsRecursive : ITreeItemVisitor
		{
			bool bSelectImmediateChildrenOnly;
			FSelectActorsRecursive(bool bInSelectImmediateChildrenOnly) : bSelectImmediateChildrenOnly(bInSelectImmediateChildrenOnly) {}

			virtual void Visit(const FActorTreeItem& ActorItem) const override
			{
				if (AActor* Actor = ActorItem.Actor.Get())
				{
					GEditor->SelectActor(Actor, true, /*bNotify=*/false);
				}

				if (!bSelectImmediateChildrenOnly)
				{
					for (TWeakPtr<ITreeItem> Child : ActorItem.GetChildren())
					{
						Child.Pin()->Visit(*this);
					}
				}
			}

			virtual void Visit(const FFolderTreeItem& FolderItem) const override
			{
				if (!bSelectImmediateChildrenOnly)
				{
					for (TWeakPtr<ITreeItem> Child : FolderItem.GetChildren())
					{
						Child.Pin()->Visit(*this);
					}
				}
			}
		};

		struct FSelectFoldersRecursive : IMutableTreeItemVisitor
		{
			SSceneOutliner& Outliner;
			bool bSelectImmediateChildrenOnly;
			FSelectFoldersRecursive(SSceneOutliner& InOutliner, bool bInSelectImmediateChildrenOnly) : Outliner(InOutliner), bSelectImmediateChildrenOnly(bInSelectImmediateChildrenOnly) {}

			virtual void Visit(FFolderTreeItem& FolderItem) const override
			{
				Outliner.OutlinerTreeView->SetItemSelection(FolderItem.AsShared(), true);

				if (!bSelectImmediateChildrenOnly)
				{
					for (TWeakPtr<ITreeItem> Child : FolderItem.GetChildren())
					{
						Child.Pin()->Visit(*this);
					}
				}	
			}
		};

		TArray<FFolderTreeItem*> SelectedFolders = GetSelectedFolders();
		OutlinerTreeView->ClearSelection();

		FExpandFoldersRecursive ExpandFoldersRecursive(*this, bSelectImmediateChildrenOnly);
		for (FFolderTreeItem* Folder : SelectedFolders)
		{
			Folder->Visit(ExpandFoldersRecursive);
		}

		if (SelectedFolders.Num())
		{
			// We'll batch selection changes instead by using BeginBatchSelectOperation()
			GEditor->GetSelectedActors()->BeginBatchSelectOperation();

			FSelectActorsRecursive SelectActorsRecursive(bSelectImmediateChildrenOnly);
			for (const FFolderTreeItem* Folder : SelectedFolders)
			{
				for (TWeakPtr<ITreeItem> Child : Folder->GetChildren())
				{
					Child.Pin()->Visit(SelectActorsRecursive);
				}
			}

			GEditor->GetSelectedActors()->EndBatchSelectOperation(/*bNotify*/false);
			GEditor->NoteSelectionChange();
		}

		// Don't select folders, only select actors
		/*  
		FSelectFoldersRecursive SelectFoldersRecursiveVisitor(*this, bSelectImmediateChildrenOnly);
		for (const FFolderTreeItem* Folder : SelectedFolders)
		{
			for (TWeakPtr<ITreeItem> Child : Folder->GetChildren())
			{
				Child.Pin()->Visit(SelectFoldersRecursiveVisitor);
			}
		}
		*/

		Refresh();
	}

	void SSceneOutliner::MoveSelectionTo(FTreeItemRef NewParent)
	{
		struct FMoveToFolder : ITreeItemVisitor
		{
			SSceneOutliner& Outliner;
			FMoveToFolder(SSceneOutliner& InOutliner) : Outliner(InOutliner) {}

			virtual void Visit(const FFolderTreeItem& Folder) const override
			{
				Outliner.MoveSelectionTo(Folder.Path);
			}
			virtual void Visit(const FWorldTreeItem&) const override
			{
				Outliner.MoveSelectionTo(FName());
			}
		};

		NewParent->Visit(FMoveToFolder(*this));
	}

	void SSceneOutliner::MoveSelectionTo(FName NewParent)
	{
		if (!CheckWorld())
		{
			return;
		}

		FSlateApplication::Get().DismissAllMenus();
		
		FFolderDropTarget 	DropTarget(NewParent);
		FDragDropPayload 	DraggedObjects(OutlinerTreeView->GetSelectedItems());

		FDragValidationInfo Validation = DropTarget.ValidateDrop(DraggedObjects, *SharedData->RepresentingWorld);
		if (!Validation.IsValid())
		{
			FNotificationInfo Info(Validation.ValidationText);
			Info.ExpireDuration = 3.0f;
			Info.bUseLargeFont = false;
			Info.bFireAndForget = true;
			Info.bUseSuccessFailIcons = true;
			FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::CS_Fail);
			return;
		}

		const FScopedTransaction Transaction( LOCTEXT("MoveOutlinerItems", "Move World Outliner Items") );
		DropTarget.OnDrop(DraggedObjects, *SharedData->RepresentingWorld, Validation, SNullWidget::NullWidget);
	}

	FReply SSceneOutliner::OnCreateFolderClicked()
	{
		CreateFolder();
		return FReply::Handled();
	}

	void SSceneOutliner::CreateFolder()
	{
		if (!CheckWorld())
		{
			return;
		}

		UWorld& World = *SharedData->RepresentingWorld;
		const FScopedTransaction Transaction(LOCTEXT("UndoAction_CreateFolder", "Create Folder"));

		const FName NewFolderName = FActorFolders::Get().GetDefaultFolderNameForSelection(World);
		FActorFolders::Get().CreateFolderContainingSelection(World, NewFolderName);

		auto PreviouslySelectedItems = OutlinerTreeView->GetSelectedItems();

		auto Visit = [&](const FFolderTreeItem& Folder)
		{
			MoveFolderTo(Folder.Path, NewFolderName, World);
		};
		auto Visitor = FFunctionalVisitor().Folder(Visit);

		// Move any selected folders into the new folder name
		for (const auto& Item : PreviouslySelectedItems)
		{
			Item->Visit(Visitor);
		}

		// At this point the new folder will be in our newly added list, so select it and open a rename when it gets refreshed
		NewItemActions.Add(NewFolderName, ENewItemAction::Select | ENewItemAction::Rename);
	}

	void SSceneOutliner::OnBroadcastFolderCreate(UWorld& InWorld, FName NewPath)
	{
		if (!ShouldShowFolders() || &InWorld != SharedData->RepresentingWorld)
		{
			return;
		}

		if (!TreeItemMap.Contains(NewPath))
		{
			ConstructItemFor<FFolderTreeItem>(NewPath);
		}
	}

	void SSceneOutliner::OnBroadcastFolderMove(UWorld& InWorld, FName OldPath, FName NewPath)
	{
		if (!ShouldShowFolders() || &InWorld != SharedData->RepresentingWorld)
		{
			return;
		}

		FTreeItemPtr Item = TreeItemMap.FindRef(OldPath);
		if (Item.IsValid())
		{
			// Remove it from the map under the old ID (which is derived from the folder path)
			TreeItemMap.Remove(Item->GetID());

			// Now change the path and put it back in the map with its new ID
			auto Folder = StaticCastSharedPtr<FFolderTreeItem>(Item);
			Folder->Path = NewPath;
			Folder->LeafName = GetFolderLeafName(NewPath);

			TreeItemMap.Add(Item->GetID(), Item);

			// Add an operation to move the item in the hierarchy
			PendingOperations.Emplace(FPendingTreeOperation::Moved, Item.ToSharedRef());
			Refresh();
		}
	}

	void SSceneOutliner::OnBroadcastFolderDelete(UWorld& InWorld, FName Path)
	{
		if (&InWorld != SharedData->RepresentingWorld)
		{
			return;
		}

		auto* Folder = TreeItemMap.Find(Path);
		if (Folder)
		{
			PendingOperations.Emplace(FPendingTreeOperation::Removed, Folder->ToSharedRef());
			Refresh();
		}
	}

	void SSceneOutliner::OnEditCutActorsBegin()
	{
		// Only a callback in actor browsing mode
		CopyFoldersBegin();
		DeleteFoldersBegin();
	}

	void SSceneOutliner::OnEditCutActorsEnd()
	{
		// Only a callback in actor browsing mode
		CopyFoldersEnd();
		DeleteFoldersEnd();
	}

	void SSceneOutliner::OnEditCopyActorsBegin()
	{
		// Only a callback in actor browsing mode
		CopyFoldersBegin();
	}

	void SSceneOutliner::OnEditCopyActorsEnd()
	{
		// Only a callback in actor browsing mode
		CopyFoldersEnd();
	}

	void SSceneOutliner::OnEditPasteActorsBegin()
	{
		// Only a callback in actor browsing mode
		TArray<FName> Folders = GetClipboardPasteFolders();
		PasteFoldersBegin(Folders);
	}

	void SSceneOutliner::OnEditPasteActorsEnd()
	{
		// Only a callback in actor browsing mode
		PasteFoldersEnd();
	}

	void SSceneOutliner::OnDuplicateActorsBegin()
	{
		// Only a callback in actor browsing mode
		TArray<FFolderTreeItem*> SelectedFolders = GetSelectedFolders();
		PasteFoldersBegin(SelectedFolders);
	}

	void SSceneOutliner::OnDuplicateActorsEnd()
	{
		// Only a callback in actor browsing mode
		PasteFoldersEnd();
	}

	void SSceneOutliner::OnDeleteActorsBegin()
	{
		// Only a callback in actor browsing mode
		DeleteFoldersBegin();
	}

	void SSceneOutliner::OnDeleteActorsEnd()
	{
		// Only a callback in actor browsing mode
		DeleteFoldersEnd();
	}

	void SSceneOutliner::CopyFoldersBegin()
	{
		// Only a callback in actor browsing mode
		CacheFoldersEdit = GetSelectedFolderNames();
		FPlatformApplicationMisc::ClipboardPaste(CacheClipboardContents);
	}

	void SSceneOutliner::CopyFoldersEnd()
	{
		// Only a callback in actor browsing mode
		if (CacheFoldersEdit.Num() > 0)
		{
			CopyFoldersToClipboard(CacheFoldersEdit, CacheClipboardContents);
			CacheFoldersEdit.Reset();
		}
	}

	void SSceneOutliner::CopyFoldersToClipboard(const TArray<FName>& InFolders, const FString& InPrevClipboardContents)
	{
		if (InFolders.Num() > 0)
		{
			// If clipboard paste has changed since we cached it, actors must have been cut 
			// so folders need to appended to clipboard contents rather than replacing them.
			FString CurrClipboardContents;
			FPlatformApplicationMisc::ClipboardPaste(CurrClipboardContents);

			FString Buffer = ExportFolderList(InFolders);

			FString* SourceData = (CurrClipboardContents != InPrevClipboardContents ? &CurrClipboardContents : nullptr);

			if (SourceData)
			{
				SourceData->Append(Buffer);
			}
			else
			{
				SourceData = &Buffer;
			}

			// Replace clipboard contents with original plus folders appended
			FPlatformApplicationMisc::ClipboardCopy(**SourceData);
		}
	}

	void SSceneOutliner::PasteFoldersBegin(TArray<FFolderTreeItem*> InFolders)
	{
		TArray<FName> FolderNames;

		for (FFolderTreeItem* Folder : InFolders)
		{
			if (Folder)
			{
				FolderNames.Add(Folder->Path);
			}
		}

		PasteFoldersBegin(FolderNames);
	}

	void SSceneOutliner::PasteFoldersBegin(TArray<FName> InFolders)
	{
		struct FCacheExistingChildrenAction : IMutableTreeItemVisitor
		{
			SSceneOutliner& Outliner;

			FCacheExistingChildrenAction(SSceneOutliner& InOutliner) : Outliner(InOutliner) {}

			virtual void Visit(FFolderTreeItem& FolderItem) const override
			{
				TArray<FTreeItemID> ExistingChildren;
				for (const TWeakPtr<ITreeItem>& Child : FolderItem.GetChildren())
				{
					if (Child.IsValid())
					{
						ExistingChildren.Add(Child.Pin()->GetID());
					}
				}

				Outliner.CachePasteFolderExistingChildrenMap.Add(FolderItem.Path, ExistingChildren);
			}
		};


		CacheFoldersEdit.Reset();
		CachePasteFolderExistingChildrenMap.Reset();
		PendingFoldersSelect.Reset();

		CacheFoldersEdit = InFolders;

		// Sort folder names so parents appear before children
		CacheFoldersEdit.Sort(FNameLexicalLess());

		// Cache existing children
		for (FName Folder : CacheFoldersEdit)
		{
			if (FTreeItemPtr* TreeItem = TreeItemMap.Find(Folder))
			{
				FCacheExistingChildrenAction CacheExistingChildrenAction(*this);
				(*TreeItem)->Visit(CacheExistingChildrenAction);
			}
		}
	}

	void SSceneOutliner::PasteFoldersEnd()
	{
		struct FReparentDuplicatedActorsAction : IMutableTreeItemVisitor
		{
			SSceneOutliner& Outliner;
			mutable bool bVisitedFolder;
			mutable FName* NewFolderPath;
			TMap<FName, FName>* FolderMap;

			FReparentDuplicatedActorsAction(SSceneOutliner& InOutliner, TMap<FName, FName>* InFolderMap) : Outliner(InOutliner), bVisitedFolder(false), NewFolderPath(nullptr), FolderMap(InFolderMap) {}

			virtual void Visit(FFolderTreeItem& FolderItem) const override
			{
				if (!bVisitedFolder)
				{
					bVisitedFolder = true;

					if (!FolderItem.Path.IsNone())
					{
						NewFolderPath = FolderMap->Find(FolderItem.Path);
						if (NewFolderPath && *NewFolderPath != FolderItem.Path)
						{
							for (TWeakPtr<ITreeItem> Child : FolderItem.GetChildren())
							{
								if (Child.IsValid())
								{
									Child.Pin()->Visit(*this);
								}
							}
						}
					}
				}
			}

			virtual void Visit(FActorTreeItem& ActorItem) const override
			{
				if (NewFolderPath)
				{
					if (AActor* Actor = ActorItem.Actor.Get())
					{
						FName ParentPath = Actor->GetFolderPath();
						TArray<FTreeItemID>* ExistingChildren = Outliner.CachePasteFolderExistingChildrenMap.Find(ParentPath);

						if (ExistingChildren && !ExistingChildren->Contains(ActorItem.GetID()))
						{
							Actor->SetFolderPath_Recursively(*NewFolderPath);
						}
					}
				}
			}
		};

		if (!CheckWorld())
		{
			return;
		}
			
		const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "PasteItems", "Paste Items"));

		// Create new folder
		TMap<FName, FName> FolderMap;
		for (FName Folder : CacheFoldersEdit)
		{
			FName ParentPath = GetParentPath(Folder);
			FName LeafName = GetFolderLeafName(Folder);
			if (LeafName != TEXT(""))
			{
				if (FName* NewParentPath = FolderMap.Find(ParentPath))
				{
					ParentPath = *NewParentPath;
				}

				FName NewFolderPath = FActorFolders::Get().GetFolderName(*SharedData->RepresentingWorld, ParentPath, LeafName);
				FActorFolders::Get().CreateFolder(*SharedData->RepresentingWorld, NewFolderPath);
				FolderMap.Add(Folder, NewFolderPath);
			}
		}

		// Populate our data set
		Populate();

		// Reparent duplicated actors if the folder has been pasted/duplicated
		for (FName Folder : CacheFoldersEdit)
		{
			if (const FName* NewFolder = FolderMap.Find(Folder))
			{
				if (FTreeItemPtr* FolderItem = TreeItemMap.Find(Folder))
				{
					FReparentDuplicatedActorsAction ReparentDuplicatedActors(*this, &FolderMap);
					(*FolderItem)->Visit(ReparentDuplicatedActors);
				}

				Folder = *NewFolder;
			}

			PendingFoldersSelect.Add(Folder);
		}

		CacheFoldersEdit.Reset();
		CachePasteFolderExistingChildrenMap.Reset();
		FullRefresh();
	}

	void SSceneOutliner::DuplicateFoldersHierarchy()
	{
		struct FSelectFoldersRecursive : IMutableTreeItemVisitor
		{
			SSceneOutliner& Outliner;
			FSelectFoldersRecursive(SSceneOutliner& InOutliner) : Outliner(InOutliner) {}

			virtual void Visit(FFolderTreeItem& FolderItem) const override
			{
				// select folders to be duplicated
				Outliner.OutlinerTreeView->SetItemSelection(FolderItem.AsShared(), true);

				// FItemSelection Selection(*Outliner.OutlinerTreeView);

				for (TWeakPtr<ITreeItem> Child : FolderItem.GetChildren())
				{
					Child.Pin()->Visit(*this);
				}
			}
		};

		if (!CheckWorld())
		{
			return;
		}

		const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "DuplicateFoldersHierarchy", "Duplicate Folders Hierarchy"));

		TArray<FFolderTreeItem*> SelectedFolders = GetSelectedFolders();

		if (SelectedFolders.Num() > 0)
		{
			// Select actor descendants
			SelectFoldersDescendants();

			// Select all sub-folders
			for (FFolderTreeItem* Folder : SelectedFolders)
			{
				FSelectFoldersRecursive SelectFoldersRecursive(*this);
				Folder->Visit(SelectFoldersRecursive);
			}

			// Duplicate selected
			GUnrealEd->Exec(SharedData->RepresentingWorld, TEXT("DUPLICATE"));
		}
	}

	void SSceneOutliner::DeleteFoldersBegin()
	{
		// Only a callback in actor browsing mode
		CacheFoldersDelete = GetSelectedFolders();
	}

	void SSceneOutliner::DeleteFoldersEnd()
	{
		// Only a callback in actor browsing mode
		struct FMatchName
		{
			FMatchName(const FName InPathName)
				: PathName(InPathName) {}

			const FName PathName;

			bool operator()(const FFolderTreeItem *Entry)
			{
				return PathName == Entry->Path;
			}
		};

		if (CacheFoldersDelete.Num() > 0)
		{
			// Sort in descending order so children will be deleted before parents
			CacheFoldersDelete.Sort([](const FFolderTreeItem& FolderA, const FFolderTreeItem& FolderB)
			{
				return FolderB.Path.LexicalLess(FolderA.Path);
			});

			for (FFolderTreeItem* Folder : CacheFoldersDelete)
			{
				if (Folder)
				{
					// Find lowest parent not being deleted, for reparenting children of current folder
					FName NewParentPath = GetParentPath(Folder->Path);
					while (!NewParentPath.IsNone() && CacheFoldersDelete.FindByPredicate(FMatchName(NewParentPath)))
					{
						NewParentPath = GetParentPath(NewParentPath);
					}

					Folder->Delete(NewParentPath);
				}
			}

			CacheFoldersDelete.Reset();
			FullRefresh();
		}
	}

	TArray<FName> SSceneOutliner::GetClipboardPasteFolders() const
	{
		FString PasteString;
		FPlatformApplicationMisc::ClipboardPaste(PasteString);
		return ImportFolderList(*PasteString);
	}

	FString SSceneOutliner::ExportFolderList(TArray<FName> InFolders) const
	{
		FString Buffer = FString(TEXT("Begin FolderList\n"));

		for (auto& FolderName : InFolders)
		{
			Buffer.Append(FString(TEXT("\tFolder=")) + FolderName.ToString() + FString(TEXT("\n")));
		}

		Buffer += FString(TEXT("End FolderList\n"));

		return Buffer;
	}

	TArray<FName> SSceneOutliner::ImportFolderList(const FString& InStrBuffer) const
	{
		TArray<FName> Folders;

		int32 Index = InStrBuffer.Find(TEXT("Begin FolderList"));
		if (Index != INDEX_NONE)
		{
			FString TmpStr = InStrBuffer.RightChop(Index);
			const TCHAR* Buffer = *TmpStr;

			FString StrLine;
			while (FParse::Line(&Buffer, StrLine))
			{
				const TCHAR* Str = *StrLine;				
				FString FolderName;

				if (FParse::Command(&Str, TEXT("Begin")) && FParse::Command(&Str, TEXT("FolderList")))
				{
					continue;
				}
				else if (FParse::Command(&Str, TEXT("End")) && FParse::Command(&Str, TEXT("FolderList")))
				{
					break;
				}
				else if (FParse::Value(Str, TEXT("Folder="), FolderName))
				{
					Folders.Add(FName(*FolderName));
				}
			}
		}
		return Folders;
	}

	void SSceneOutliner::ScrollItemIntoView(FTreeItemPtr Item)
	{
		auto Parent = Item->GetParent();
		while(Parent.IsValid())
		{
			OutlinerTreeView->SetItemExpansion(Parent->AsShared(), true);
			Parent = Parent->GetParent();
		}

		OutlinerTreeView->RequestScrollIntoView(Item);
	}

	TSharedRef< ITableRow > SSceneOutliner::OnGenerateRowForOutlinerTree( FTreeItemPtr Item, const TSharedRef< STableViewBase >& OwnerTable )
	{
		return SNew( SSceneOutlinerTreeRow, OutlinerTreeView.ToSharedRef(), SharedThis(this) ).Item( Item );
	}

	void SSceneOutliner::OnGetChildrenForOutlinerTree( FTreeItemPtr InParent, TArray< FTreeItemPtr >& OutChildren )
	{
		if( SharedData->bShowParentTree )
		{
			for (auto& WeakChild : InParent->GetChildren())
			{
				auto Child = WeakChild.Pin();
				// Should never have bogus entries in this list
				check(Child.IsValid());
				OutChildren.Add(Child);
			}

			// If the item needs it's children sorting, do that now
			if (OutChildren.Num() && InParent->Flags.bChildrenRequireSort)
			{
				// Sort the children we returned
				SortItems(OutChildren);

				// Empty out the children and repopulate them in the correct order
				InParent->Children.Empty();
				for (auto& Child : OutChildren)
				{
					InParent->Children.Emplace(Child);
				}
				
				// They no longer need sorting
				InParent->Flags.bChildrenRequireSort = false;
			}
		}
	}

	static const FName SequencerActorTag(TEXT("SequencerActor"));

	bool SSceneOutliner::IsActorDisplayable( const AActor* Actor ) const
	{
		return	!SharedData->bOnlyShowFolders && 												// Don't show actors if we're only showing folders
				Actor->IsEditable() &&															// Only show actors that are allowed to be selected and drawn in editor
				Actor->IsListedInSceneOutliner() &&
				( (SharedData->bRepresentingPlayWorld || !Actor->HasAnyFlags(RF_Transient)) ||
				  (SharedData->bShowTransient && Actor->HasAnyFlags(RF_Transient)) ||			// Don't show transient actors in non-play worlds
				  (Actor->ActorHasTag(SequencerActorTag)) ) &&		
				!Actor->IsTemplate() &&															// Should never happen, but we never want CDOs displayed
				!FActorEditorUtils::IsABuilderBrush(Actor) &&									// Don't show the builder brush
				!Actor->IsA( AWorldSettings::StaticClass() ) &&									// Don't show the WorldSettings actor, even though it is technically editable
				!Actor->IsPendingKill() &&														// We don't want to show actors that are about to go away
				FLevelUtils::IsLevelVisible( Actor->GetLevel() );								// Only show Actors whose level is visible
	}

	void SSceneOutliner::OnOutlinerTreeSelectionChanged( FTreeItemPtr TreeItem, ESelectInfo::Type SelectInfo )
	{
		if (SelectInfo == ESelectInfo::Direct)
		{
			return;
		}

		if (SharedData->Mode == ESceneOutlinerMode::Custom)
		{
			OnItemSelectionChanged.Broadcast(TreeItem, SelectInfo);
			return;
		}

		if( SharedData->Mode == ESceneOutlinerMode::ActorPicker || SharedData->Mode == ESceneOutlinerMode::ComponentPicker )
		{
			// In actor picking mode, we fire off the notification to whoever is listening.
			// This may often cause the widget itself to be enqueued for destruction
			if( OutlinerTreeView->GetNumItemsSelected() > 0 )
			{
				auto FirstItem = OutlinerTreeView->GetSelectedItems()[0];
				if (FirstItem->CanInteract())
				{
					OnItemPicked.ExecuteIfBound( FirstItem->AsShared() );
				}
			}
		}

		// We only synchronize selection when in actor browsing mode
		else if( SharedData->Mode == ESceneOutlinerMode::ActorBrowsing )
		{
			if( !bIsReentrant )
			{
				TGuardValue<bool> ReentrantGuard(bIsReentrant,true);

				// @todo outliner: Can be called from non-interactive selection

				// The tree let us know that selection has changed, but wasn't able to tell us
				// what changed.  So we'll perform a full difference check and update the editor's
				// selected actors to match the control's selection set.

				// Make a list of all the actors that should now be selected in the world.
				FItemSelection Selection(*OutlinerTreeView);

				// notify components of selection change
				if (Selection.SubComponents.Num() > 0)
				{
					FSceneOutlinerDelegates::Get().OnSubComponentSelectionChanged.Broadcast(Selection.SubComponents);
				}

				auto SelectedActors = TSet<AActor*>(Selection.GetActorPtrs());

				bool bChanged = false;
				bool bAnyInPIE = false;
				for (auto* Actor : SelectedActors)
				{
					if (!bAnyInPIE && Actor && Actor->GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor))
					{
						bAnyInPIE = true;
					}
					if (!GEditor->GetSelectedActors()->IsSelected(Actor))
					{
						bChanged = true;
						break;
					}
				}

				for (FSelectionIterator SelectionIt( *GEditor->GetSelectedActors() ); SelectionIt && !bChanged; ++SelectionIt)
				{
					AActor* Actor = CastChecked< AActor >( *SelectionIt );
					if (!bAnyInPIE && Actor->GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor))
					{
						bAnyInPIE = true;
					}
					if (!SelectedActors.Contains(Actor))
					{
						// Actor has been deselected
						bChanged = true;

						// If actor was a group actor, remove its members from the ActorsToSelect list
						AGroupActor* DeselectedGroupActor = Cast<AGroupActor>(Actor);
						if ( DeselectedGroupActor )
						{
							TArray<AActor*> GroupActors;
							DeselectedGroupActor->GetGroupActors( GroupActors );

							for (auto* GroupActor : GroupActors)
							{
								SelectedActors.Remove(GroupActor);
							}

						}
					}
				}

				// If there's a discrepancy, update the selected actors to reflect this list.
				if ( bChanged )
				{
					const FScopedTransaction Transaction( NSLOCTEXT("UnrealEd", "ClickingOnActors", "Clicking on Actors"), !bAnyInPIE );
					GEditor->GetSelectedActors()->Modify();

					// Clear the selection.
					GEditor->SelectNone(false, true, true);

					// We'll batch selection changes instead by using BeginBatchSelectOperation()
					GEditor->GetSelectedActors()->BeginBatchSelectOperation();

					const bool bShouldSelect = true;
					const bool bNotifyAfterSelect = false;
					const bool bSelectEvenIfHidden = true;	// @todo outliner: Is this actually OK?
					for (auto* Actor : SelectedActors)
					{
						UE_LOG(LogSceneOutliner, Verbose,  TEXT("Clicking on Actor (world outliner): %s (%s)"), *Actor->GetClass()->GetName(), *Actor->GetActorLabel());
						GEditor->SelectActor( Actor, bShouldSelect, bNotifyAfterSelect, bSelectEvenIfHidden );
					}

					// Commit selection changes
					GEditor->GetSelectedActors()->EndBatchSelectOperation(/*bNotify*/false);

					// Fire selection changed event
					GEditor->NoteSelectionChange();
				}

				bActorSelectionDirty = true;
			}
		}
	}

	
	void SSceneOutliner::OnLevelSelectionChanged(UObject* Obj)
	{
		// We only synchronize selection when in actor browsing mode
		if( SharedData->Mode == ESceneOutlinerMode::ActorBrowsing )
		{
			// @todo outliner: Because we are not notified of which items are being added/removed from selection, we have
			// no immediate means to incrementally update the tree when selection changes.

			// Ideally, we can improve the filtering paradigm to better support incremental updates in cases such as these
			if ( IsShowingOnlySelected() )
			{
				FullRefresh();
			}
			else if (!bIsReentrant)
			{
				OutlinerTreeView->ClearSelection();
				bActorSelectionDirty = true;

				// Scroll last item into view - this means if we are multi-selecting, we show newest selection. @TODO Not perfect though
				if (AActor* LastSelectedActor = GEditor->GetSelectedActors()->GetBottom<AActor>())
				{
					FTreeItemPtr TreeItem = TreeItemMap.FindRef(LastSelectedActor);
					if (TreeItem.IsValid())
					{
						if (!OutlinerTreeView->IsItemVisible(TreeItem))
						{
							ScrollItemIntoView(TreeItem);
						}
					}
					else
					{
						OnItemAdded(LastSelectedActor, ENewItemAction::ScrollIntoView);
					}
				}
			}
		}
	}

	void SSceneOutliner::OnOutlinerTreeDoubleClick( FTreeItemPtr TreeItem )
	{
		if(SharedData->Mode == ESceneOutlinerMode::ActorBrowsing)
		{
			auto ExpandCollapseFolder = [&](const FFolderTreeItem& Folder){
				auto Shared = const_cast<FFolderTreeItem&>(Folder).AsShared();
				OutlinerTreeView->SetItemExpansion(Shared, !OutlinerTreeView->IsItemExpanded(Shared));
			};

			if (TreeItem->CanInteract())
			{
				TreeItem->Visit(FFunctionalVisitor()

					.Actor([&](const FActorTreeItem&){
						// Move all actors into view
						FItemSelection Selection(*OutlinerTreeView);
						if( Selection.Actors.Num() > 0 )
						{
							const bool bActiveViewportOnly = false;
							GEditor->MoveViewportCamerasToActor( Selection.GetActorPtrs(), bActiveViewportOnly );
						}
					})

					.Folder(ExpandCollapseFolder)

					.World([](const FWorldTreeItem& WorldItem){
						WorldItem.OpenWorldSettings();
					})

					.Component([](const FComponentTreeItem& ComponentItem) {
						ComponentItem.OnDoubleClick();
					})

					.SubComponent([](const FSubComponentTreeItem& SubComponentItem) {
						SubComponentItem.OnDoubleClick();
					})
				);

			}
			else
			{
				TreeItem->Visit(FFunctionalVisitor()

					.Folder(ExpandCollapseFolder)

					.Actor([&](const FActorTreeItem& Item){
						// Move just this actor into view
						if (AActor* Actor = Item.Actor.Get())
						{
							const bool bActiveViewportOnly = false;
							GEditor->MoveViewportCamerasToActor( *Actor, bActiveViewportOnly );
						}
					})
				);
			}
		}
		else if (SharedData->Mode == ESceneOutlinerMode::Custom)
		{
			OnDoubleClickOnTreeEvent.Broadcast(TreeItem);
		}
	}

	void SSceneOutliner::OnOutlinerTreeItemScrolledIntoView( FTreeItemPtr TreeItem, const TSharedPtr<ITableRow>& Widget )
	{
		if (TreeItem == PendingRenameItem.Pin())
		{
			PendingRenameItem = nullptr;
			TreeItem->RenameRequestEvent.ExecuteIfBound();
		}
	}
	
	void SSceneOutliner::OnItemExpansionChanged(FTreeItemPtr TreeItem, bool bIsExpanded) const
	{
		TreeItem->Flags.bIsExpanded = bIsExpanded;
		TreeItem->OnExpansionChanged();

		// Expand any children that are also expanded
		for (auto WeakChild : TreeItem->GetChildren())
		{
			auto Child = WeakChild.Pin();
			if (Child->Flags.bIsExpanded)
			{
				OutlinerTreeView->SetItemExpansion(Child, true);
			}
		}
	}

	void SSceneOutliner::OnLevelAdded(ULevel* InLevel, UWorld* InWorld)
	{
		if ( SharedData->RepresentingWorld == InWorld )
		{
			FullRefresh();
		}
	}

	void SSceneOutliner::OnLevelRemoved(ULevel* InLevel, UWorld* InWorld)
	{
		if (SharedData->RepresentingWorld == InWorld)
		{
			FullRefresh();
		}
	}

	void SSceneOutliner::OnLevelActorsAdded(AActor* InActor)
	{
		if( !bIsReentrant )
		{
			if( InActor && SharedData->RepresentingWorld == InActor->GetWorld() && IsActorDisplayable(InActor) )
			{
				if (!TreeItemMap.Find(InActor) && !PendingTreeItemMap.Find(InActor))
				{
					// Update the total actor count that match the filters
					if (Filters->PassesAllFilters(FActorTreeItem(InActor)))
					{
						ApplicableActors.Emplace(InActor);
					}

					ConstructItemFor<FActorTreeItem>(InActor);

					if (IsShowingActorComponents())
					{
						TArray<ISceneOutlinerTraversal*> ConstructTreeItemImp = IModularFeatures::Get().GetModularFeatureImplementations<ISceneOutlinerTraversal>("SceneOutlinerTraversal");
						for (UActorComponent* Component : InActor->GetComponents())
						{
							if (Filters->PassesAllFilters(FComponentTreeItem(Component)))
							{
								bool IsHandled = false;
								for (ISceneOutlinerTraversal* CustomImplementation : ConstructTreeItemImp)
								{
									IsHandled = CustomImplementation->ConstructTreeItem(*this, Component);
									if (IsHandled)
									{
										break;
									}
								}
								if (!IsHandled)
								{
									// add the actor's components - default implementation
									ConstructItemFor<FComponentTreeItem>(Component);
								}
							}
						}
					}
				}
			}
		}
	}

	void SSceneOutliner::OnLevelActorsRemoved(AActor* InActor)
	{
		if( !bIsReentrant )
		{
			if( InActor && SharedData->RepresentingWorld == InActor->GetWorld() )
			{
				ApplicableActors.Remove(InActor);
				auto* ItemPtr = TreeItemMap.Find(InActor);
				if (!ItemPtr)
				{
					ItemPtr = PendingTreeItemMap.Find(InActor);
				}

				if (ItemPtr)
				{
					PendingOperations.Emplace(FPendingTreeOperation::Removed, ItemPtr->ToSharedRef());
					Refresh();
				}
			}
		}
	}

	void SSceneOutliner::OnLevelActorsAttached(AActor* InActor, const AActor* InParent)
	{
		// InActor can be equal to InParent in cases of components being attached internally. The Scene Outliner does not need to do anything in this case.
		if( !bIsReentrant && InActor != InParent )
		{
			if( InActor && SharedData->RepresentingWorld == InActor->GetWorld() )
			{
				if (auto* ItemPtr = TreeItemMap.Find(InActor))
				{
					PendingOperations.Emplace(FPendingTreeOperation::Moved, ItemPtr->ToSharedRef());
					Refresh();
				}
			}
		}
	}

	void SSceneOutliner::OnLevelActorsDetached(AActor* InActor, const AActor* InParent)
	{
		// InActor can be equal to InParent in cases of components being attached internally. The Scene Outliner does not need to do anything in this case.
		if( !bIsReentrant && InActor != InParent)
		{
			if( InActor && SharedData->RepresentingWorld == InActor->GetWorld() )
			{
				if (auto* ItemPtr = TreeItemMap.Find(InActor))
				{
					PendingOperations.Emplace(FPendingTreeOperation::Moved, ItemPtr->ToSharedRef());
					Refresh();
				}
				else
				{
					// We should find the item, but if we don't, do an add.
					OnLevelActorsAdded(InActor);
				}
			}
		}
	}

	/** Called by the engine when an actor's folder is changed */
	void SSceneOutliner::OnLevelActorFolderChanged(const AActor* InActor, FName OldPath)
	{
		auto* ActorTreeItem = TreeItemMap.Find(InActor);
		if (!ShouldShowFolders() || !InActor || !ActorTreeItem)
		{
			return;
		}
		
		PendingOperations.Emplace(FPendingTreeOperation::Moved, ActorTreeItem->ToSharedRef());
		Refresh();
	}

	void SSceneOutliner::OnLevelActorsRequestRename(const AActor* InActor)
	{
		auto SelectedItems = OutlinerTreeView->GetSelectedItems();
		if( SelectedItems.Num() > 0)
		{
			// Ensure that the item we want to rename is visible in the tree
			FTreeItemPtr ItemToRename = SelectedItems[SelectedItems.Num() - 1];
			if (CanExecuteRenameRequest(ItemToRename) && ItemToRename->CanInteract())
			{
				PendingRenameItem = ItemToRename->AsShared();
				ScrollItemIntoView(ItemToRename);
			}
		}
	}

	void SSceneOutliner::OnMapChange(uint32 MapFlags)
	{
		FullRefresh();
	}

	void SSceneOutliner::OnNewCurrentLevel()
	{
		if (IsShowingOnlyCurrentLevel())
		{
			FullRefresh();
		}
	}

	void SSceneOutliner::PostUndo(bool bSuccess)
	{
		// Refresh our tree in case any changes have been made to the scene that might effect our actor list
		if( !bIsReentrant )
		{
            bDisableIntermediateSorting = true;
			FullRefresh();
		}
	}

	void SSceneOutliner::OnActorLabelChanged(AActor* ChangedActor)
	{
		if ( !ensure(ChangedActor) )
		{
			return;
		}
		
		FTreeItemPtr TreeItem = TreeItemMap.FindRef(ChangedActor);
		if (TreeItem.IsValid())
		{
			if (SearchBoxFilter->PassesFilter(*TreeItem))
			{
				OutlinerTreeView->FlashHighlightOnItem(TreeItem);
				RequestSort();
			}
			else
			{
				// Do longer matches the filters, remove it
				PendingOperations.Emplace(FPendingTreeOperation::Removed, TreeItem.ToSharedRef());
				Refresh();
			}
		}
		else if (IsActorDisplayable(ChangedActor) && SharedData->RepresentingWorld == ChangedActor->GetWorld())
		{
			// Attempt to add the item if we didn't find it - perhaps it now matches the filter?
			ConstructItemFor<FActorTreeItem>(ChangedActor);
		}
	}

	void SSceneOutliner::OnAssetReloaded(const EPackageReloadPhase InPackageReloadPhase, FPackageReloadedEvent* InPackageReloadedEvent)
	{
		if (InPackageReloadPhase == EPackageReloadPhase::PostBatchPostGC)
		{
			// perhaps overkill but a simple Refresh() doesn't appear to work.
			FullRefresh();
		}
	}

	void SSceneOutliner::OnFilterTextChanged( const FText& InFilterText )
	{
		SearchBoxFilter->SetRawFilterText( InFilterText );
		FilterTextBoxWidget->SetError( SearchBoxFilter->GetFilterErrorText() );

		// Scroll last item (if it passes the filter) into view - this means if we are multi-selecting, we show newest selection that passes the filter
		if (AActor* LastSelectedActor = GEditor->GetSelectedActors()->GetBottom<AActor>())
		{
			// This part is different than that of OnLevelSelectionChanged(nullptr) because IsItemVisible(TreeItem) & ScrollItemIntoView(TreeItem) are applied to
			// the current visual state, not to the one after applying the filter. Thus, the scroll would go to the place where the object was located
			// before applying the FilterText

			// If the object is already in the list, but it does not passes the filter, then we do not want to re-add it, because it will be removed by the filter
			const FTreeItemPtr TreeItem = TreeItemMap.FindRef(LastSelectedActor);
			if (TreeItem.IsValid() && !SearchBoxFilter->PassesFilter(*TreeItem))
			{
				return;
			}

			// If the object is not in the list, and it does not passes the filter, then we should not re-add it, because it would be removed by the filter again. Unfortunately,
			// there is no code to check if a future element (i.e., one that is currently not in the TreeItemMap list) will pass the filter. Therefore, we kind of overkill it
			// by re-adding that element (even though it will be removed). However, AddItemToTree(FTreeItemRef Item) and similar functions already check the element before
			// adding it. So this solution is fine.
			// This solution might affect the performance of the World Outliner when a key is pressed, but it will still work properly when the remove/del keys are pressed. Not
			// updating the filter when !TreeItem.IsValid() would result in the focus not being updated when the remove/del keys are pressed.

			// In any other case (i.e., if the object passes the current filter), re-add it
			OnItemAdded(LastSelectedActor, ENewItemAction::ScrollIntoView);
		}
	}

	void SSceneOutliner::OnFilterTextCommitted( const FText& InFilterText, ETextCommit::Type CommitInfo )
	{
		const FString CurrentFilterText = InFilterText.ToString();
		// We'll only select actors if the user actually pressed the enter key.  We don't want to change
		// selection just because focus was lost from the search text field.
		if( CommitInfo == ETextCommit::OnEnter )
		{
			// Any text in the filter?  If not, we won't bother doing anything
			if( !CurrentFilterText.IsEmpty() )
			{
				FItemSelection Selection;

				// Gather all of the actors that match the filter text
				for (auto& Pair : TreeItemMap)
				{
					if (!Pair.Value->Flags.bIsFilteredOut)
					{
						Pair.Value->Visit(Selection);
					}
				}

				// We only select level actors when in actor browsing mode
				if( SharedData->Mode == ESceneOutlinerMode::ActorBrowsing )
				{
					// Start batching selection changes
					GEditor->GetSelectedActors()->BeginBatchSelectOperation();

					// Select actors (and only the actors) that match the filter text
					const bool bNoteSelectionChange = false;
					const bool bDeselectBSPSurfs = false;
					const bool WarnAboutManyActors = true;
					GEditor->SelectNone( bNoteSelectionChange, bDeselectBSPSurfs, WarnAboutManyActors );
					for (auto* Actor : Selection.GetActorPtrs())
					{
						const bool bShouldSelect = true;
						const bool bSelectEvenIfHidden = false;
						GEditor->SelectActor( Actor, bShouldSelect, bNoteSelectionChange, bSelectEvenIfHidden );
					}

					// Commit selection changes
					GEditor->GetSelectedActors()->EndBatchSelectOperation(/*bNotify*/false);

					// Fire selection changed event
					GEditor->NoteSelectionChange();

					// Set keyboard focus to the SceneOutliner, so the user can perform keyboard commands that interact
					// with selected actors (such as Delete, to delete selected actors.)
					SetKeyboardFocus();
				}

				// In 'actor picking' mode, we allow the user to commit their selection by pressing enter
				// in the search window when a single actor is available
				else if( SharedData->Mode == ESceneOutlinerMode::ActorPicker || SharedData->Mode == ESceneOutlinerMode::ComponentPicker )
				{
					// In actor picking mode, we check to see if we have a selected actor, and if so, fire
					// off the notification to whoever is listening.  This may often cause the widget itself
					// to be enqueued for destruction
					if( Selection.Actors.Num() == 1 )
					{
						// Signal that an actor was selected. We assume it is valid as it won't have been added to ActorsToSelect if not.
						OutlinerTreeView->SetSelection( Selection.Actors[0]->AsShared(), ESelectInfo::OnKeyPress );
					}
				}
				// In the custom mode we want to mimic a similar result to the actor browser
				else if (SharedData->Mode == ESceneOutlinerMode::Custom)
				{
					if (SelectionMode == ESelectionMode::Single || SelectionMode == ESelectionMode::SingleToggle)
					{
						OutlinerTreeView->SetSelection( Selection.Actors[0]->AsShared(), ESelectInfo::OnKeyPress );
					}
					else if (SelectionMode == ESelectionMode::Multi)
					{
						TArray<FTreeItemPtr> ItemsPtr;
						ItemsPtr.Reserve(Selection.Actors.Num());
						for (FActorTreeItem* Item : Selection.Actors)
						{
							ItemsPtr.Add(Item->AsShared());
						}
						OutlinerTreeView->ClearSelection();
						OutlinerTreeView->SetItemSelection( ItemsPtr, true, ESelectInfo::OnKeyPress );
					}
				}
			}
		}
		else if (CommitInfo == ETextCommit::OnCleared)
		{
			OnFilterTextChanged(InFilterText);
		}
	}

	EVisibility SSceneOutliner::GetFilterStatusVisibility() const
	{
		return IsFilterActive() ? EVisibility::Visible : EVisibility::Collapsed;
	}

	EVisibility SSceneOutliner::GetEmptyLabelVisibility() const
	{
		return ( IsFilterActive() || RootTreeItems.Num() > 0 ) ? EVisibility::Collapsed : EVisibility::Visible;
	}

	FText SSceneOutliner::GetFilterStatusText() const
	{
		const int32 TotalActorCount = ApplicableActors.Num();

		int32 SelectedActorCount = 0;
		auto Count = [&](const FActorTreeItem&) { ++SelectedActorCount; };
		for (const auto& Item : OutlinerTreeView->GetSelectedItems())
		{
			Item->Visit(FFunctionalVisitor().Actor(Count));
		}

		if ( !IsFilterActive() )
		{
			if (SelectedActorCount == 0) //-V547
			{
				return FText::Format( LOCTEXT("ShowingAllActorsFmt", "{0} actors"), FText::AsNumber( TotalActorCount ) );
			}
			else
			{
				return FText::Format( LOCTEXT("ShowingAllActorsSelectedFmt", "{0} actors ({1} selected)"), FText::AsNumber( TotalActorCount ), FText::AsNumber( SelectedActorCount ) );
			}
		}
		else if( IsFilterActive() && FilteredActorCount == 0 )
		{
			return FText::Format( LOCTEXT("ShowingNoActorsFmt", "No matching actors ({0} total)"), FText::AsNumber( TotalActorCount ) );
		}
		else if (SelectedActorCount != 0) //-V547
		{
			return FText::Format( LOCTEXT("ShowingOnlySomeActorsSelectedFmt", "Showing {0} of {1} actors ({2} selected)"), FText::AsNumber( FilteredActorCount ), FText::AsNumber( TotalActorCount ), FText::AsNumber( SelectedActorCount ) );
		}
		else
		{
			return FText::Format( LOCTEXT("ShowingOnlySomeActorsFmt", "Showing {0} of {1} actors"), FText::AsNumber( FilteredActorCount ), FText::AsNumber( TotalActorCount ) );
		}
	}

	FSlateColor SSceneOutliner::GetFilterStatusTextColor() const
	{
		if ( !IsFilterActive() )
		{
			// White = no text filter
			return FLinearColor( 1.0f, 1.0f, 1.0f );
		}
		else if( FilteredActorCount == 0 )
		{
			// Red = no matching actors
			return FLinearColor( 1.0f, 0.4f, 0.4f );
		}
		else
		{
			// Green = found at least one match!
			return FLinearColor( 0.4f, 1.0f, 0.4f );
		}
	}

	bool SSceneOutliner::IsFilterActive() const
	{
		return FilterTextBoxWidget->GetText().ToString().Len() > 0 && ApplicableActors.Num() != FilteredActorCount;
	}

	const FSlateBrush* SSceneOutliner::GetFilterButtonGlyph() const
	{
		if( IsFilterActive() )
		{
			return FEditorStyle::GetBrush(TEXT("SceneOutliner.FilterCancel"));
		}
		else
		{
			return FEditorStyle::GetBrush(TEXT("SceneOutliner.FilterSearch"));
		}
	}

	FString SSceneOutliner::GetFilterButtonToolTip() const
	{
		return IsFilterActive() ? LOCTEXT("ClearSearchFilter", "Clear search filter").ToString() : LOCTEXT("StartSearching", "Search").ToString();

	}

	TAttribute<FText> SSceneOutliner::GetFilterHighlightText() const
	{
		return TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateStatic([](TWeakPtr<TreeItemTextFilter> Filter){
			auto FilterPtr = Filter.Pin();
			return FilterPtr.IsValid() ? FilterPtr->GetRawFilterText() : FText();
		}, TWeakPtr<TreeItemTextFilter>(SearchBoxFilter)));
	}

	void SSceneOutliner::SetKeyboardFocus()
	{
		if (SupportsKeyboardFocus())
		{
			FWidgetPath OutlinerTreeViewWidgetPath;
			// NOTE: Careful, GeneratePathToWidget can be reentrant in that it can call visibility delegates and such
			FSlateApplication::Get().GeneratePathToWidgetUnchecked( OutlinerTreeView.ToSharedRef(), OutlinerTreeViewWidgetPath );
			FSlateApplication::Get().SetKeyboardFocus( OutlinerTreeViewWidgetPath, EFocusCause::SetDirectly );
		}
	}

	const FSlateBrush* SSceneOutliner::GetCachedIconForClass(FName InClassName) const
	{ 
		if (CachedIcons.Find(InClassName))
		{
			return *CachedIcons.Find(InClassName);
		}
		else
		{
			return nullptr;
		}
	}

	void SSceneOutliner::CacheIconForClass(FName InClassName, const FSlateBrush* InSlateBrush)
	{
		CachedIcons.Emplace(InClassName, InSlateBrush);
	}

	bool SSceneOutliner::SupportsKeyboardFocus() const
	{
		// We only need to support keyboard focus if we're in actor browsing mode
		if( SharedData->Mode == ESceneOutlinerMode::ActorBrowsing )
		{
			// Scene outliner needs keyboard focus so the user can press keys to activate commands, such as the Delete
			// key to delete selected actors
			return true;
		}

		return false;
	}

	FReply SSceneOutliner::OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent )
	{
		// @todo outliner: Use command system for these for discoverability? (allow bindings?)

		// We only allow these operations in actor browsing mode
		if( SharedData->Mode == ESceneOutlinerMode::ActorBrowsing )
		{
			// Rename key: Rename selected actors (not rebindable, because it doesn't make much sense to bind.)
			if( InKeyEvent.GetKey() == EKeys::F2 )
			{
				if (OutlinerTreeView->GetNumItemsSelected() == 1)
				{
					FTreeItemPtr ItemToRename = OutlinerTreeView->GetSelectedItems()[0];
					
					if (CanExecuteRenameRequest(ItemToRename) && ItemToRename->CanInteract())
					{
						PendingRenameItem = ItemToRename->AsShared();
						ScrollItemIntoView(ItemToRename);
					}

					return FReply::Handled();
				}
			}

			// F5 forces a full refresh
			else if ( InKeyEvent.GetKey() == EKeys::F5 )
			{
				FullRefresh();
				return FReply::Handled();
			}

			// Delete key: Delete selected actors (not rebindable, because it doesn't make much sense to bind.)
			// Use Delete and Backspace instead of Platform_Delete because the LevelEditor default Edit Delete is bound to both 
			else if ( InKeyEvent.GetKey() == EKeys::Delete || InKeyEvent.GetKey() == EKeys::BackSpace )
			{
				const FItemSelection Selection(*OutlinerTreeView);

				if( SharedData->CustomDelete.IsBound() )
				{
					SharedData->CustomDelete.Execute( Selection.GetWeakActors() );
				}
				else
				{
					if (CheckWorld())
					{
						GUnrealEd->Exec(SharedData->RepresentingWorld, TEXT("DELETE"));
					}
				}
				return FReply::Handled();
			}
		}

		return FReply::Unhandled();
	}

	void SSceneOutliner::SynchronizeActorSelection()
	{
		TGuardValue<bool> ReentrantGuard(bIsReentrant, true);

		USelection* SelectedActors = GEditor->GetSelectedActors();

		// Deselect actors in the tree that are no longer selected in the world
		FItemSelection Selection(*OutlinerTreeView);
		if (Selection.Actors.Num())
		{
			TArray<FTreeItemPtr> ActorItems;
			for (FActorTreeItem* ActorItem : Selection.Actors)
			{
				if(!ActorItem->Actor.IsValid() || !ActorItem->Actor.Get()->IsSelected())
				{
					ActorItems.Add(ActorItem->AsShared());
				}
			}

			OutlinerTreeView->SetItemSelection(ActorItems, false);
		}
		
		// Show actor selection but only if sub objects are not selected
		if (Selection.Components.Num() == 0 && Selection.SubComponents.Num() == 0)
		{
			// See if the tree view selector is pointing at a selected item
			bool bSelectorInSelectionSet = false;

			TArray<FTreeItemPtr> ActorItems;
			for (FSelectionIterator SelectionIt(*SelectedActors); SelectionIt; ++SelectionIt)
			{
				AActor* Actor = CastChecked< AActor >(*SelectionIt);
				if (FTreeItemPtr* ActorItem = TreeItemMap.Find(Actor))
				{
					if (!bSelectorInSelectionSet && OutlinerTreeView->Private_HasSelectorFocus(*ActorItem))
					{
						bSelectorInSelectionSet = true;
					}

					ActorItems.Add(*ActorItem);
				}
			}

			// If NOT bSelectorInSelectionSet then we want to just move the selector to the first selected item.
			ESelectInfo::Type SelectInfo = bSelectorInSelectionSet ? ESelectInfo::Direct : ESelectInfo::OnMouseClick;
			OutlinerTreeView->SetItemSelection(ActorItems, true, SelectInfo);
		}

		// Broadcast selection changed delegate
		FSceneOutlinerDelegates::Get().SelectionChanged.Broadcast();
	}

	void SSceneOutliner::OnComponentSelectionChanged(UActorComponent* Component)
	{
		if (!Component)
			return;

		if (FTreeItemPtr* ComponentItem = TreeItemMap.Find(Component))
		{
			if (ComponentItem->IsValid())
			{
				(*ComponentItem)->SynchronizeSubItemSelection(OutlinerTreeView);
			}
		}
	}
	
	void SSceneOutliner::OnComponentsUpdated()
	{
		// #todo: A bit overkill, only one actors sub-components have changed
		FullRefresh();
	}

	void SSceneOutliner::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
	{	
		for (auto& Pair : Columns)
		{
			Pair.Value->Tick(InCurrentTime, InDeltaTime);
		}

		if ( bPendingFocusNextFrame && FilterTextBoxWidget->GetVisibility() == EVisibility::Visible )
		{
			FWidgetPath WidgetToFocusPath;
			FSlateApplication::Get().GeneratePathToWidgetUnchecked( FilterTextBoxWidget.ToSharedRef(), WidgetToFocusPath );
			FSlateApplication::Get().SetKeyboardFocus( WidgetToFocusPath, EFocusCause::SetDirectly );
			bPendingFocusNextFrame = false;
		}

		if ( bNeedsColumRefresh )
		{
			SetupColumns(*HeaderRowWidget);
		}

		if( bNeedsRefresh )
		{
			if( !bIsReentrant )
			{
				Populate();
			}
		}
		SortOutlinerTimer -= InDeltaTime;

		if (bSortDirty && (!SharedData->bRepresentingPlayWorld || SortOutlinerTimer <= 0))
		{
			SortItems(RootTreeItems);
			for (const auto& Pair : TreeItemMap)
			{
				Pair.Value->Flags.bChildrenRequireSort = true;
			}

			OutlinerTreeView->RequestTreeRefresh();
			bSortDirty = false;
		}

		if (SortOutlinerTimer <= 0)
		{
			SortOutlinerTimer = SCENE_OUTLINER_RESORT_TIMER;
		}


		if (bActorSelectionDirty)
		{
			SynchronizeActorSelection();
			bActorSelectionDirty = false;
		}
	}

	void SSceneOutliner::AddReferencedObjects(FReferenceCollector& Collector)
	{
		Collector.AddReferencedObject(SceneOutlinerSettings);
	}

	EColumnSortMode::Type SSceneOutliner::GetColumnSortMode( const FName ColumnId ) const
	{
		if (SortByColumn == ColumnId)
		{
			auto Column = Columns.FindRef(ColumnId);
			if (Column.IsValid() && Column->SupportsSorting())
			{
				return SortMode;
			}
		}

		return EColumnSortMode::None;
	}

	void SSceneOutliner::OnColumnSortModeChanged( const EColumnSortPriority::Type SortPriority, const FName& ColumnId, const EColumnSortMode::Type InSortMode )
	{
		auto Column = Columns.FindRef(ColumnId);
		if (!Column.IsValid() || !Column->SupportsSorting())
		{
			return;
		}

		SortByColumn = ColumnId;
		SortMode = InSortMode;

		RequestSort();
	}

	void SSceneOutliner::RequestSort()
	{
		bSortDirty = true;
	}

	void SSceneOutliner::SortItems(TArray<FTreeItemPtr>& Items) const
	{
		auto Column = Columns.FindRef(SortByColumn);
		if (Column.IsValid())
		{
			Column->SortItems(Items, SortMode);
		}
	}

	void SSceneOutliner::FOnItemAddedToTree::Visit(FActorTreeItem& ActorItem) const
	{
		Outliner.FilteredActorCount += ActorItem.Flags.bIsFilteredOut ? 0 : 1;

		// Synchronize selection
		if (GEditor->GetSelectedActors()->IsSelected(ActorItem.Actor.Get()))
		{
			Outliner.OutlinerTreeView->SetItemSelection(ActorItem.AsShared(), true);
		}
	}

	void SSceneOutliner::FOnItemAddedToTree::Visit(FFolderTreeItem& Folder) const
	{
		if (!Outliner.SharedData->RepresentingWorld)
		{
			return;
		}

		if (FActorFolderProps* Props = FActorFolders::Get().GetFolderProperties(*Outliner.SharedData->RepresentingWorld, Folder.Path))
		{
			Folder.Flags.bIsExpanded = Props->bIsExpanded;
		}
	}

	void SSceneOutliner::OnSelectWorld(TWeakObjectPtr<UWorld> InWorld)
	{
		SharedData->UserChosenWorld = InWorld;
		FullRefresh();
	}

	bool SSceneOutliner::IsWorldChecked(TWeakObjectPtr<UWorld> InWorld)
	{
		return (InWorld == SharedData->UserChosenWorld);
	}

	void SSceneOutliner::SetItemExpansionRecursive(FTreeItemPtr Model, bool bInExpansionState)
	{
		if (Model.IsValid())
		{
			OutlinerTreeView->SetItemExpansion(Model, bInExpansionState);
			for (auto& Child : Model->Children)
			{
				if (Child.IsValid())
				{
					SetItemExpansionRecursive(Child.Pin(), bInExpansionState);
				}
			}
		}
	}
}	// namespace SceneOutliner

#undef LOCTEXT_NAMESPACE
