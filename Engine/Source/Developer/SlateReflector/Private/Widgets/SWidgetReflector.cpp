// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SWidgetReflector.h"

#include "ISlateReflectorModule.h"
#include "SlateReflectorModule.h"
#include "Rendering/DrawElements.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "InputEventVisualizer.h"
#include "Styling/WidgetReflectorStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/SSlateOptions.h"
#include "Widgets/SWidgetReflectorTreeWidgetItem.h"
#include "Widgets/SWidgetReflectorToolTipWidget.h"
#include "Widgets/SWidgetEventLog.h"
#include "Widgets/SWidgetHittestGrid.h"
#include "Widgets/SInvalidationPanel.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SWidgetSnapshotVisualizer.h"
#include "WidgetSnapshotService.h"
#include "Types/ReflectionMetadata.h"
#include "Debugging/SlateDebugging.h"
#include "VisualTreeCapture.h"
#include "Models/WidgetReflectorNode.h"

#if SLATE_REFLECTOR_HAS_DESKTOP_PLATFORM
#include "DesktopPlatformModule.h"
#endif // SLATE_REFLECTOR_HAS_DESKTOP_PLATFORM

#if SLATE_REFLECTOR_HAS_SESSION_SERVICES
#include "ISessionManager.h"
#include "ISessionServicesModule.h"
#endif // SLATE_REFLECTOR_HAS_SESSION_SERVICES

#if WITH_EDITOR
#include "Framework/Docking/LayoutService.h"
#include "PropertyEditorModule.h"
#include "UnrealEdMisc.h"
#endif

#define LOCTEXT_NAMESPACE "SWidgetReflector"

/**
 * Widget reflector user widget.
 */

/* Local helpers
 *****************************************************************************/
 
namespace WidgetReflectorImpl
{

/** Information about a potential widget snapshot target */
struct FWidgetSnapshotTarget
{
	/** Display name of the target (used in the UI) */
	FText DisplayName;

	/** Instance ID of the target */
	FGuid InstanceId;
};

/** Different UI modes the widget reflector can be in */
enum class EWidgetReflectorUIMode : uint8
{
	Live,
	Snapshot,
};

namespace WidgetReflectorTabID
{
	static const FName WidgetHierarchy = "WidgetReflector.WidgetHierarchyTab";
	static const FName SnapshotWidgetPicker = "WidgetReflector.SnapshotWidgetPickerTab";
	static const FName WidgetDetails = "WidgetReflector.WidgetDetailsTab";
	static const FName SlateOptions = "WidgetReflector.SlateOptionsTab";
	static const FName WidgetEvents = "WidgetReflector.WidgetEventsTab";
	static const FName HittestGrid = "WidgetReflector.HittestGridTab";
}

namespace WidgetReflectorText
{
	static const FText HitTestPicking = LOCTEXT("PickHitTestable", "Pick Hit-Testable Widgets");
	static const FText VisualPicking = LOCTEXT("PickVisual", "Pick Painted Widgets");
	static const FText Focus = LOCTEXT("ShowFocus", "Show Focus");
	static const FText Focusing = LOCTEXT("ShowingFocus", "Showing Focus (Esc to Stop)");
	static const FText Picking = LOCTEXT("PickingWidget", "Picking (Esc to Stop)");
}

namespace WidgetReflectorIcon
{
	static const FName FocusPicking = "Icon.FocusPicking";
	static const FName HitTestPicking = "Icon.HitTestPicking";
	static const FName VisualPicking = "Icon.VisualPicking";
}

enum class EWidgetPickingMode : uint8
{
	None = 0,
	Focus,
	HitTesting,
	Drawable
};

EWidgetPickingMode ConvertToWidgetPickingMode(int32 Number)
{
	if (Number < 0 || Number > static_cast<int32>(EWidgetPickingMode::Drawable))
	{
		return EWidgetPickingMode::None;
	}
	return static_cast<EWidgetPickingMode>(Number);
}

/**
 * Widget reflector implementation
 */
class SWidgetReflector : public ::SWidgetReflector
{
	// The reflector uses a tree that observes FWidgetReflectorNodeBase objects.
	typedef STreeView<TSharedRef<FWidgetReflectorNodeBase>> SReflectorTree;

private:

	//~ Begin ::SWidgetReflector implementation
	virtual void Construct( const FArguments& InArgs) override;
	//~ End ::SWidgetReflector implementation

	void HandlePullDownAtlasesMenu(FMenuBuilder& MenuBuilder);
	void HandlePullDownWindowMenu(FMenuBuilder& MenuBuilder);

	TSharedRef<SDockTab> SpawnSlateOptionWidgetTab(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnWidgetHierarchyTab(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnSnapshotWidgetPicker(const FSpawnTabArgs& Args);

#if WITH_EDITOR
	TSharedRef<SDockTab> SpawnWidgetDetails(const FSpawnTabArgs& Args);
#endif

#if WITH_SLATE_DEBUGGING
	TSharedRef<SDockTab> SpawnWidgetEvents(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnWidgeHittestGrid(const FSpawnTabArgs& Args);
#endif

	void HandleTabManagerPersistLayout(const TSharedRef<FTabManager::FLayout>& LayoutToSave);
	void OnTabSpawned(const FName& TabIdentifier, const TSharedRef<SDockTab>& SpawnedTab);
	void CloseTab(const FName& TabIdentifier);

	void SaveSettings();
	void LoadSettings();

	void SetUIMode(const EWidgetReflectorUIMode InNewMode);

	//~ Begin SCompoundWidget overrides
	virtual void Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime ) override;
	//~ End SCompoundWidget overrides

	//~ Begin IWidgetReflector interface
	virtual bool IsInPickingMode() const override
	{
		return PickingMode != EWidgetPickingMode::None;
	}

	virtual bool IsShowingFocus() const override
	{
		return PickingMode == EWidgetPickingMode::Focus;
	}

	virtual bool IsVisualizingLayoutUnderCursor() const override
	{
		return PickingMode == EWidgetPickingMode::HitTesting || PickingMode == EWidgetPickingMode::Drawable;
	}

	virtual void OnWidgetPicked() override
	{
		SetPickingMode(EWidgetPickingMode::None);
	}

	virtual bool ReflectorNeedsToDrawIn( TSharedRef<SWindow> ThisWindow ) const override;

	virtual void SetSourceAccessDelegate( FAccessSourceCode InDelegate ) override
	{
		SourceAccessDelegate = InDelegate;
	}

	virtual void SetAssetAccessDelegate(FAccessAsset InDelegate) override
	{
		AsseetAccessDelegate = InDelegate;
	}

	virtual void SetWidgetsToVisualize( const FWidgetPath& InWidgetsToVisualize ) override;
	virtual int32 Visualize( const FWidgetPath& InWidgetsToVisualize, FSlateWindowElementList& OutDrawElements, int32 LayerId ) override;
	//~ End IWidgetReflector interface

	/**
	 * Generates a tool tip for the given reflector tree node.
	 *
	 * @param InReflectorNode The node to generate the tool tip for.
	 * @return The tool tip widget.
	 */
	TSharedRef<SToolTip> GenerateToolTipForReflectorNode( TSharedRef<FWidgetReflectorNodeBase> InReflectorNode );

	/**
	 * Mark the provided reflector nodes such that they stand out in the tree and are visible.
	 *
	 * @param WidgetPathToObserve The nodes to mark.
	 */
	void VisualizeAsTree( const TArray< TSharedRef<FWidgetReflectorNodeBase> >& WidgetPathToVisualize );

	/**
	 * Draw the widget path to the picked widget as the widgets' outlines.
	 *
	 * @param InWidgetsToVisualize A widget path whose widgets' outlines to draw.
	 * @param OutDrawElements A list of draw elements; we will add the output outlines into it.
	 * @param LayerId The maximum layer achieved in OutDrawElements so far.
	 * @return The maximum layer ID we achieved while painting.
	 */
	int32 VisualizePickAsRectangles( const FWidgetPath& InWidgetsToVisualize, FSlateWindowElementList& OutDrawElements, int32 LayerId );

	/**
	 * Draw an outline for the specified nodes.
	 *
	 * @param InNodesToDraw A widget path whose widgets' outlines to draw.
	 * @param WindowGeometry The geometry of the window in which to draw.
	 * @param OutDrawElements A list of draw elements; we will add the output outlines into it.
	 * @param LayerId the maximum layer achieved in OutDrawElements so far.
	 * @return The maximum layer ID we achieved while painting.
	 */
	int32 VisualizeSelectedNodesAsRectangles( const TArray<TSharedRef<FWidgetReflectorNodeBase>>& InNodesToDraw, const TSharedRef<SWindow>& VisualizeInWindow, FSlateWindowElementList& OutDrawElements, int32 LayerId );

	/** Draw the actual highlight */
	void DrawWidgetVisualization(const FPaintGeometry& WidgetGeometry, FLinearColor Color, FSlateWindowElementList& OutDrawElements, int32& LayerId);

	/** Clear previous selection and set the selection to the live widget. */
	void SelectLiveWidget( TSharedPtr<const SWidget> InWidget );

	/** Set the current selected node as the root of the tree. */
	void SetSelectedAsReflectorTreeRoot();

	/** Is there any selected node in the reflector tree. */
	bool DoesReflectorTreeHasSelectedItem() const { return SelectedNodes.Num() > 0; }

	/** Apply the requested filter to the reflected tree root. */
	void UpdateFilteredTreeRoot();

	void HandleDisplayTextureAtlases();
	void HandleDisplayFontAtlases();

	//~ Handle for the picking button
	ECheckBoxState HandleGetPickingButtonChecked() const;
	void HandlePickingModeStateChanged(ECheckBoxState NewValue);
	const FSlateBrush* HandleGetPickingModeImage() const;
	FText HandleGetPickingModeText() const;
	TSharedRef<SWidget> HandlePickingModeContextMenu();	
	void HandlePickButtonClicked(EWidgetPickingMode InPickingMode);

	void SetPickingMode(EWidgetPickingMode InMode)
	{
#if WITH_SLATE_DEBUGGING
		static auto CVarSlateGlobalInvalidation = IConsoleManager::Get().FindConsoleVariable(TEXT("Slate.EnableGlobalInvalidation"));
#endif

		if (PickingMode != InMode)
		{
			// Disable visual picking, and re-enable widget caching.
#if WITH_SLATE_DEBUGGING
			SInvalidationPanel::EnableInvalidationPanels(true);

			if (PickingMode == EWidgetPickingMode::None)
			{
				bLastGlobalInvalidationState = CVarSlateGlobalInvalidation->GetBool();
			}

			CVarSlateGlobalInvalidation->Set(bLastGlobalInvalidationState);
#endif
			VisualCapture.Disable();

			// Enable the picking mode.
			PickingMode = InMode;

			// If we're enabling hit test, reset the visual capture entirely, we don't want to use the visual tree.
			if (PickingMode == EWidgetPickingMode::HitTesting)
			{
				VisualCapture.Reset();
#if WITH_SLATE_DEBUGGING
				SInvalidationPanel::EnableInvalidationPanels(false);
#endif
				VisualCapture.Reset();
			}
			// If we're using the drawing picking mode enable it!
			else if (PickingMode == EWidgetPickingMode::Drawable)
			{
				VisualCapture.Enable();
#if WITH_SLATE_DEBUGGING
				SInvalidationPanel::EnableInvalidationPanels(false);
				CVarSlateGlobalInvalidation->Set(false);
#endif
			}
		}
	}

	/** Callback to see whether the "Snapshot Target" combo should be enabled */
	bool IsSnapshotTargetComboEnabled() const;

	/** Callback to see whether the "Take Snapshot" button should be enabled */
	bool IsTakeSnapshotButtonEnabled() const;

	/** Callback for clicking the "Take Snapshot" button. */
	FReply HandleTakeSnapshotButtonClicked();

	/** Build option menu for snaphot. */
	TSharedRef<SWidget> HandleSnapshotOptionsTreeContextMenu();

	/** Takes a snapshot of the current state of the snapshot target. */
	void TakeSnapshot();

	/** Used as a callback for the "snapshot pending" notification item buttons, called when we should give up on a snapshot request */
	void OnCancelPendingRemoteSnapshot();

	/** Callback for when a remote widget snapshot is available */
	void HandleRemoteSnapshotReceived(const TArray<uint8>& InSnapshotData);

#if SLATE_REFLECTOR_HAS_DESKTOP_PLATFORM
	/** Callback for clicking the "Load Snapshot" button. */
	FReply HandleLoadSnapshotButtonClicked();
#endif // SLATE_REFLECTOR_HAS_DESKTOP_PLATFORM

	/** Called to update the list of available snapshot targets */
	void UpdateAvailableSnapshotTargets();

	/** Called to update the currently selected snapshot target (after the list has been refreshed) */
	void UpdateSelectedSnapshotTarget();

	/** Called when the list of available snapshot targets changes */
	void OnAvailableSnapshotTargetsChanged();

	/** Get the display name of the currently selected snapshot target */
	FText GetSelectedSnapshotTargetDisplayName() const;

	/** Generate a row widget for the available targets combo box */
	TSharedRef<SWidget> HandleGenerateAvailableSnapshotComboItemWidget(TSharedPtr<FWidgetSnapshotTarget> InItem) const;
	
	/** Update the selected target when the combo box selection is changed */
	void HandleAvailableSnapshotComboSelectionChanged(TSharedPtr<FWidgetSnapshotTarget> InItem, ESelectInfo::Type InSeletionInfo);

	/** Callback for generating a row in the reflector tree view. */
	TSharedRef<ITableRow> HandleReflectorTreeGenerateRow( TSharedRef<FWidgetReflectorNodeBase> InReflectorNode, const TSharedRef<STableViewBase>& OwnerTable );

	/** Callback for getting the child items of the given reflector tree node. */
	void HandleReflectorTreeGetChildren( TSharedRef<FWidgetReflectorNodeBase> InReflectorNode, TArray<TSharedRef<FWidgetReflectorNodeBase>>& OutChildren );

	/** Callback for when the selection in the reflector tree has changed. */
	void HandleReflectorTreeSelectionChanged( TSharedPtr<FWidgetReflectorNodeBase>, ESelectInfo::Type /*SelectInfo*/ );

	/** Callback for when the context menu in the reflector tree is requested. */
	TSharedRef<SWidget> HandleReflectorTreeContextMenu();
	TSharedPtr<SWidget> HandleReflectorTreeContextMenuPtr();

	/** Callback for when the reflector tree header list changed. */
	void HandleReflectorTreeHiddenColumnsListChanged();

	/** Reset the filtered tree root. */
	void HandleResetFilteredTreeRoot();

	/** Show the start of the UMG tree. */
	void HandleStartTreeWithUMG();

	/** Should we show only the UMG tree. */
	bool HandleIsStartTreeWithUMGEnabled() const { return bFilterReflectorTreeRootWithUMG; }

private:
	TSharedPtr<FTabManager> TabManager;
	TMap<FName, TWeakPtr<SDockTab>> SpawnedTabs;

	TSharedPtr<SReflectorTree> ReflectorTree;
	TArray<FString> HiddenReflectorTreeColumns;

	/** Node that are currently selected */
	TArray<TSharedRef<FWidgetReflectorNodeBase>> SelectedNodes;
	/** The original path of the widget picked. It may include node that are now hidden by the filter */
	TArray<TSharedRef<FWidgetReflectorNodeBase>> PickedWidgetPath;
	/** Root of the tree before filtering */
	TArray<TSharedRef<FWidgetReflectorNodeBase>> ReflectorTreeRoot;
	/** Root of the tree after filtering */
	TArray<TSharedRef<FWidgetReflectorNodeBase>> FilteredTreeRoot;

	/** When working with a snapshotted tree, this will contain the snapshot hierarchy and screenshot info */
	FWidgetSnapshotData SnapshotData;
	TSharedPtr<SWidgetSnapshotVisualizer> WidgetSnapshotVisualizer;

	/** List of available snapshot targets, as well as the one we currently have selected */
	TSharedPtr<SComboBox<TSharedPtr<FWidgetSnapshotTarget>>> AvailableSnapshotTargetsComboBox;
	TArray<TSharedPtr<FWidgetSnapshotTarget>> AvailableSnapshotTargets;
	FGuid SelectedSnapshotTargetInstanceId;
	TSharedPtr<FWidgetSnapshotService> WidgetSnapshotService;
	TWeakPtr<SNotificationItem> WidgetSnapshotNotificationPtr;
	FGuid RemoteSnapshotRequestId;

	FAccessSourceCode SourceAccessDelegate;
	FAccessAsset AsseetAccessDelegate;

	EWidgetReflectorUIMode CurrentUIMode;
	EWidgetPickingMode PickingMode;
	EWidgetPickingMode LastPickingMode;
	bool bFilterReflectorTreeRootWithUMG;

#if WITH_EDITOR
	TSharedPtr<IDetailsView> PropertyViewPtr;
#endif
#if WITH_SLATE_DEBUGGING
	TWeakPtr<SWidgetHittestGrid> WidgetHittestGrid;
#endif

	FVisualTreeCapture VisualCapture;

	bool bLastGlobalInvalidationState = false;

private:
	float SnapshotDelay;
	bool bIsPendingDelayedSnapshot;
	bool bRequestNavigationSimulation;
	double TimeOfScheduledSnapshot;
};

void SWidgetReflector::Construct( const FArguments& InArgs )
{
	LoadSettings();

	CurrentUIMode = EWidgetReflectorUIMode::Live;
	PickingMode = EWidgetPickingMode::None;
	//LastPickingMode = EWidgetPickingMode::HitTesting; //initialized in LoadSettings
	bFilterReflectorTreeRootWithUMG = false;

	SnapshotDelay = 0.0f;
	bIsPendingDelayedSnapshot = false;
	bRequestNavigationSimulation = false;
	TimeOfScheduledSnapshot = -1.0;

	WidgetSnapshotService = InArgs._WidgetSnapshotService;

#if SLATE_REFLECTOR_HAS_SESSION_SERVICES
	{
		TSharedPtr<ISessionManager> SessionManager = FModuleManager::LoadModuleChecked<ISessionServicesModule>("SessionServices").GetSessionManager();
		if (SessionManager.IsValid())
		{
			SessionManager->OnSessionsUpdated().AddSP(this, &SWidgetReflector::OnAvailableSnapshotTargetsChanged);
		}
	}
#endif // SLATE_REFLECTOR_HAS_SESSION_SERVICES
	SelectedSnapshotTargetInstanceId = FApp::GetInstanceId();
	UpdateAvailableSnapshotTargets();

	const FName TabLayoutName = "WidgetReflector_Layout_NoStats_v2";

	TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout(TabLayoutName)
	->AddArea
	(
		FTabManager::NewPrimaryArea()
		->SetOrientation(Orient_Vertical)
		->Split
		(
			FTabManager::NewStack()
			->SetHideTabWell(true)
			->AddTab(WidgetReflectorTabID::SlateOptions, ETabState::OpenedTab)
		)
		->Split
		(
			// Main application area
			FTabManager::NewSplitter()
			->SetOrientation(Orient_Horizontal)
			->Split
			(
				// Main application area
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Vertical)
				->Split
				(
					FTabManager::NewStack()
					->SetHideTabWell(true)
					->SetSizeCoefficient(0.7f)
					->AddTab(WidgetReflectorTabID::WidgetHierarchy, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetHideTabWell(true)
					->SetSizeCoefficient(0.3f)
					->AddTab(WidgetReflectorTabID::SnapshotWidgetPicker, ETabState::ClosedTab)
#if WITH_SLATE_DEBUGGING
					->AddTab(WidgetReflectorTabID::WidgetEvents, ETabState::ClosedTab)
					->AddTab(WidgetReflectorTabID::HittestGrid, ETabState::ClosedTab)
#endif
				)
			)
#if WITH_EDITOR
			->Split
			(
				FTabManager::NewStack()
				->SetHideTabWell(true)
				->SetSizeCoefficient(0.3f)
				->AddTab(WidgetReflectorTabID::WidgetDetails, ETabState::ClosedTab)
			)
#endif
		)
	);

	auto RegisterTrackedTabSpawner = [this](const FName& TabId, const FOnSpawnTab& OnSpawnTab) -> FTabSpawnerEntry&
	{
		return TabManager->RegisterTabSpawner(TabId, FOnSpawnTab::CreateLambda([this, OnSpawnTab](const FSpawnTabArgs& Args) -> TSharedRef<SDockTab>
		{
			TSharedRef<SDockTab> SpawnedTab = OnSpawnTab.Execute(Args);
			OnTabSpawned(Args.GetTabId().TabType, SpawnedTab);
			return SpawnedTab;
		}));
	};

	check(InArgs._ParentTab.IsValid());
	TabManager = FGlobalTabmanager::Get()->NewTabManager(InArgs._ParentTab.ToSharedRef());
	TabManager->SetOnPersistLayout(FTabManager::FOnPersistLayout::CreateRaw(this, &SWidgetReflector::HandleTabManagerPersistLayout));

	RegisterTrackedTabSpawner(WidgetReflectorTabID::SlateOptions, FOnSpawnTab::CreateSP(this, &SWidgetReflector::SpawnSlateOptionWidgetTab))
		.SetDisplayName(LOCTEXT("OptionsTab", "Slate Debug Options"));

	RegisterTrackedTabSpawner(WidgetReflectorTabID::WidgetHierarchy, FOnSpawnTab::CreateSP(this, &SWidgetReflector::SpawnWidgetHierarchyTab))
		.SetDisplayName(LOCTEXT("WidgetHierarchyTab", "Widget Hierarchy"));

	RegisterTrackedTabSpawner(WidgetReflectorTabID::SnapshotWidgetPicker, FOnSpawnTab::CreateSP(this, &SWidgetReflector::SpawnSnapshotWidgetPicker))
		.SetDisplayName(LOCTEXT("SnapshotWidgetPickerTab", "Snapshot Widget Picker"));

#if WITH_EDITOR
	if (GIsEditor)
	{
		RegisterTrackedTabSpawner(WidgetReflectorTabID::WidgetDetails, FOnSpawnTab::CreateSP(this, &SWidgetReflector::SpawnWidgetDetails))
			.SetDisplayName(LOCTEXT("WidgetDetailsTab", "Widget Details"));
	}
#endif //WITH_EDITOR

#if WITH_SLATE_DEBUGGING
	RegisterTrackedTabSpawner(WidgetReflectorTabID::WidgetEvents, FOnSpawnTab::CreateSP(this, &SWidgetReflector::SpawnWidgetEvents))
		.SetDisplayName(LOCTEXT("WidgetEventsTab", "Widget Events"));
	RegisterTrackedTabSpawner(WidgetReflectorTabID::HittestGrid, FOnSpawnTab::CreateSP(this, &SWidgetReflector::SpawnWidgeHittestGrid))
		.SetDisplayName(LOCTEXT("HitTestGridTab", "Hit Test Grid"));
#endif

#if WITH_EDITOR
	if (GIsEditor)
	{
		Layout = FLayoutSaveRestore::LoadFromConfig(GEditorLayoutIni, Layout);
	}
#endif

	FMenuBarBuilder MenuBarBuilder = FMenuBarBuilder(TSharedPtr<FUICommandList>());
#if WITH_SLATE_DEBUGGING
	MenuBarBuilder.AddPullDownMenu(
		LOCTEXT("DemoModeLabel", "Demo Mode"),
		FText::GetEmpty(),
		FNewMenuDelegate::CreateRaw(FSlateReflectorModule::GetModulePtr()->GetInputEventVisualizer(), &FInputEventVisualizer::PopulateMenu),
		"DemoMode"
	);
#endif
	MenuBarBuilder.AddPullDownMenu(
		LOCTEXT("AtlasesMenuLabel", "Atlases"),
		FText::GetEmpty(),
		FNewMenuDelegate::CreateSP(this, &SWidgetReflector::HandlePullDownAtlasesMenu),
		"Atlases"
		);
	MenuBarBuilder.AddPullDownMenu(
		LOCTEXT("WindowMenuLabel", "Window"),
		FText::GetEmpty(),
		FNewMenuDelegate::CreateSP(this, &SWidgetReflector::HandlePullDownWindowMenu),
		"Window"
		);

	this->ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(FLinearColor::Gray) // Darken the outer border
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
			[
				MenuBarBuilder.MakeWidget()
			]
			
			+ SVerticalBox::Slot()
			.Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
			[
				TabManager->RestoreFrom(Layout, nullptr).ToSharedRef()
			]
		]
	];
}

void SWidgetReflector::HandlePullDownAtlasesMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		LOCTEXT("DisplayTextureAtlases", "Display Texture Atlases"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &SWidgetReflector::HandleDisplayTextureAtlases)
		));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("DisplayFontAtlases", "Display Font Atlases"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &SWidgetReflector::HandleDisplayFontAtlases)
		));
}

void SWidgetReflector::HandlePullDownWindowMenu(FMenuBuilder& MenuBuilder)
{
	if (!TabManager.IsValid())
	{
		return;
	}

	TabManager->PopulateLocalTabSpawnerMenu(MenuBuilder);
}

TSharedRef<SDockTab> SWidgetReflector::SpawnSlateOptionWidgetTab(const FSpawnTabArgs& Args)
{
	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("WidgetHierarchyTab", "Widget Hierarchy"))
		.ShouldAutosize(true)
		[
			SNew(SSlateOptions)
		];
	return SpawnedTab;
}

TSharedRef<SDockTab> SWidgetReflector::SpawnWidgetHierarchyTab(const FSpawnTabArgs& Args)
{
	TArray<FName> HiddenColumnsList;
	HiddenColumnsList.Reserve(HiddenReflectorTreeColumns.Num());
	for (const FString& Item : HiddenReflectorTreeColumns)
	{
		HiddenColumnsList.Add(*Item);
	}

	// Button that controls the target for the snapshot operation
	AvailableSnapshotTargetsComboBox = SNew(SComboBox<TSharedPtr<FWidgetSnapshotTarget>>)
	.IsEnabled(this, &SWidgetReflector::IsSnapshotTargetComboEnabled)
	.ToolTipText(LOCTEXT("ChooseSnapshotTargetToolTipText", "Choose Snapshot Target"))
	.OptionsSource(&AvailableSnapshotTargets)
	.OnGenerateWidget(this, &SWidgetReflector::HandleGenerateAvailableSnapshotComboItemWidget)
	.OnSelectionChanged(this, &SWidgetReflector::HandleAvailableSnapshotComboSelectionChanged)
	[
		SNew(STextBlock)
		.Text(this, &SWidgetReflector::GetSelectedSnapshotTargetDisplayName)
	];

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("WidgetHierarchyTab", "Widget Hierarchy"))
		//.OnCanCloseTab_Lambda([]() { return false; }) // Can't prevent this as it stops the editor from being able to close while the widget reflector is open
		[
			SNew(SVerticalBox)
				
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 2.0f))
			[
				SNew(SHorizontalBox)

				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(5.0f, 0.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					[
						SNew(SCheckBox)
						.Style(FWidgetReflectorStyle::Get(), "CheckBoxNoHover")
						.Padding(FMargin(4, 0))
						.HAlign(HAlign_Left)
						.IsChecked(this, &SWidgetReflector::HandleGetPickingButtonChecked)
						.IsEnabled_Lambda([this]() { return !bIsPendingDelayedSnapshot; })
						.OnCheckStateChanged(this, &SWidgetReflector::HandlePickingModeStateChanged)
						[
							SNew(SBox)
							.MinDesiredWidth(175)
							.VAlign(VAlign_Center)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.AutoWidth()
								[
									SNew(SImage)
									.Image(this, &SWidgetReflector::HandleGetPickingModeImage)
								]
								+ SHorizontalBox::Slot()
								.AutoWidth()
								.Padding(10.f, 4.f, 4.f, 4.f)
								[
									SNew(STextBlock)
									.Text(this, &SWidgetReflector::HandleGetPickingModeText)
								]
							]
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SComboButton)
						.ButtonStyle(FWidgetReflectorStyle::Get(), "Button")
						.IsEnabled_Lambda([this]() { return !bIsPendingDelayedSnapshot; })
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.OnGetMenuContent(this, &SWidgetReflector::HandlePickingModeContextMenu)
					]
                ]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SComboButton)
					.ButtonStyle(FWidgetReflectorStyle::Get(), "Button")
					.IsEnabled_Lambda([this]() { return !bIsPendingDelayedSnapshot; })
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.OnGetMenuContent(this, &SWidgetReflector::HandleReflectorTreeContextMenu)
					.ButtonContent()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("FilterLabel", "Filter "))
						.ColorAndOpacity(FLinearColor::White)
					]
				]

				+SHorizontalBox::Slot()
				[
					SNew(SSpacer)
				]

				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(5.0f, 0.0f))
				[
					SNew(SHorizontalBox)

					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						// Button that controls taking a snapshot of the current window(s)
						SNew(SButton)
						.VAlign(VAlign_Center)
						.IsEnabled(this, &SWidgetReflector::IsTakeSnapshotButtonEnabled)
						.OnClicked(this, &SWidgetReflector::HandleTakeSnapshotButtonClicked)
						[
							SNew(STextBlock)
							.Text_Lambda([this]() { return bIsPendingDelayedSnapshot ? LOCTEXT("CancelSnapshotButtonText", "Cancel Snapshot") : LOCTEXT("TakeSnapshotButtonText", "Take Snapshot"); })
						]
					]

					+SHorizontalBox::Slot()
					.Padding(FMargin(5.0f, 0.0f))
					.AutoWidth()
					[
						SNew(SComboButton)
						.IsEnabled_Lambda([this]() { return !bIsPendingDelayedSnapshot; })
						.OnGetMenuContent(this, &SWidgetReflector::HandleSnapshotOptionsTreeContextMenu)
						.ButtonContent()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("OptionsLabel", "Options"))
						]
					]
#if SLATE_REFLECTOR_HAS_DESKTOP_PLATFORM
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						// Button that controls loading a saved snapshot
						SNew(SButton)
						.VAlign(VAlign_Center)
						.IsEnabled_Lambda([this]() { return !bIsPendingDelayedSnapshot; })
						.OnClicked(this, &SWidgetReflector::HandleLoadSnapshotButtonClicked)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("LoadSnapshotButtonText", "Load Snapshot"))
						]
					]
#endif // SLATE_REFLECTOR_HAS_DESKTOP_PLATFORM
				]
			]

			+SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SBorder)
				.Padding(0)
				.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				[
					// The tree view that shows all the info that we capture.
					SAssignNew(ReflectorTree, SReflectorTree)
					.ItemHeight(24.0f)
					.TreeItemsSource(&FilteredTreeRoot)
					.OnGenerateRow(this, &SWidgetReflector::HandleReflectorTreeGenerateRow)
					.OnGetChildren(this, &SWidgetReflector::HandleReflectorTreeGetChildren)
					.OnSelectionChanged(this, &SWidgetReflector::HandleReflectorTreeSelectionChanged)
					.OnContextMenuOpening(this, &SWidgetReflector::HandleReflectorTreeContextMenuPtr)
					.HighlightParentNodesForSelection(true)
					.HeaderRow
					(
						SNew(SHeaderRow)
						.CanSelectGeneratedColumn(true)
						.HiddenColumnsList(HiddenColumnsList)
						.OnHiddenColumnsListChanged(this, &SWidgetReflector::HandleReflectorTreeHiddenColumnsListChanged)

						+SHeaderRow::Column(SReflectorTreeWidgetItem::NAME_WidgetName)
						.DefaultLabel(LOCTEXT("WidgetName", "Widget Name"))
						.FillWidth(0.80f)
						.ShouldGenerateWidget(true)

						+SHeaderRow::Column(SReflectorTreeWidgetItem::NAME_ForegroundColor)
						.DefaultLabel(LOCTEXT("ForegroundColor", "FG"))
						.DefaultTooltip(LOCTEXT("ForegroundColorToolTip", "Foreground Color"))
						.FixedWidth(24.0f)

						+SHeaderRow::Column(SReflectorTreeWidgetItem::NAME_Visibility)
						.DefaultLabel(LOCTEXT("Visibility", "Visibility"))
						.DefaultTooltip(LOCTEXT("VisibilityTooltip", "Visibility"))
						.FixedWidth(125.0f)

						+ SHeaderRow::Column(SReflectorTreeWidgetItem::NAME_Focusable)
						.DefaultLabel(LOCTEXT("Focus", "Focus?"))
						.DefaultTooltip(LOCTEXT("FocusableTooltip", "Focusability (Note that for hit-test directional navigation to work it must be Focusable and \"Visible\"!)"))
						.FixedWidth(50.0f)

						+SHeaderRow::Column(SReflectorTreeWidgetItem::NAME_Clipping)
						.DefaultLabel(LOCTEXT("Clipping", "Clipping" ))
						.FixedWidth(100.0f)

						+SHeaderRow::Column(SReflectorTreeWidgetItem::NAME_WidgetInfo)
						.DefaultLabel(LOCTEXT("Source", "Source" ))
						.FillWidth(0.20f)

						+SHeaderRow::Column(SReflectorTreeWidgetItem::NAME_Address)
						.DefaultLabel( LOCTEXT("Address", "Address") )
						.FixedWidth(170.0f)
					)
				]
			]
		];

	UpdateSelectedSnapshotTarget();

	return SpawnedTab;
}

TSharedRef<SDockTab> SWidgetReflector::SpawnSnapshotWidgetPicker(const FSpawnTabArgs& Args)
{
	TWeakPtr<SWidgetReflector> WeakSelf = SharedThis(this);
	auto OnTabClosed = [WeakSelf](TSharedRef<SDockTab>)
	{
		// Tab closed - leave snapshot mode
		if (TSharedPtr<SWidgetReflector> SelfPinned = WeakSelf.Pin())
		{
			SelfPinned->SetUIMode(EWidgetReflectorUIMode::Live);
		}
	};

	auto OnWidgetPathPicked = [WeakSelf](const TArray<TSharedRef<FWidgetReflectorNodeBase>>& InPickedWidgetPath)
	{
		if (TSharedPtr<SWidgetReflector> SelfPinned = WeakSelf.Pin())
		{
			SelfPinned->SelectedNodes.Reset();
			SelfPinned->PickedWidgetPath = InPickedWidgetPath;
			SelfPinned->UpdateFilteredTreeRoot();
		}
	};

	auto OnSnapshotWidgetPicked = [WeakSelf](FWidgetReflectorNodeBase::TPointerAsInt InSnapshotWidget)
	{
		if (TSharedPtr<SWidgetReflector> SelfPinned = WeakSelf.Pin())
		{
			SelfPinned->SelectedNodes.Reset();
			FWidgetReflectorNodeUtils::FindSnaphotWidget(SelfPinned->ReflectorTreeRoot, InSnapshotWidget, SelfPinned->PickedWidgetPath);
			SelfPinned->UpdateFilteredTreeRoot();
		}
	};

	return SNew(SDockTab)
		.Label(LOCTEXT("SnapshotWidgetPickerTab", "Snapshot Widget Picker"))
		.OnTabClosed_Lambda(OnTabClosed)
		[
			SAssignNew(WidgetSnapshotVisualizer, SWidgetSnapshotVisualizer)
			.SnapshotData(&SnapshotData)
			.OnWidgetPathPicked_Lambda(OnWidgetPathPicked)
			.OnSnapshotWidgetSelected_Lambda(OnSnapshotWidgetPicked)
		];
}

#if WITH_EDITOR

TSharedRef<SDockTab> SWidgetReflector::SpawnWidgetDetails(const FSpawnTabArgs& Args)
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	{
		DetailsViewArgs.bAllowSearch = true;
		DetailsViewArgs.bShowOptions = true;
		DetailsViewArgs.bAllowMultipleTopLevelObjects = false;
		DetailsViewArgs.bAllowFavoriteSystem = true;
		DetailsViewArgs.bShowActorLabel = false;
		DetailsViewArgs.bHideSelectionTip = true;
	}
	TSharedRef<IDetailsView> PropertyView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	PropertyViewPtr = PropertyView;

	auto OnTabClosed = [this](TSharedRef<SDockTab>)
	{
	};

	return SNew(SDockTab)
		.Label(LOCTEXT("WidgetDetailsTab", "Widget Details"))
		.OnTabClosed_Lambda(OnTabClosed)
		[
			PropertyView
		];
}

#endif //WITH_EDITOR

#if WITH_SLATE_DEBUGGING

TSharedRef<SDockTab> SWidgetReflector::SpawnWidgetEvents(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("WidgetEventsTab", "Widget Events"))
		[
			SNew(SWidgetEventLog, AsShared())
			.OnWidgetTokenActivated(this, &SWidgetReflector::SelectLiveWidget)
		];
}

TSharedRef<SDockTab> SWidgetReflector::SpawnWidgeHittestGrid(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("HitTestGridTab", "Hit Test Grid"))
		[
			SAssignNew(WidgetHittestGrid, SWidgetHittestGrid, AsShared())
			.OnWidgetSelected(this, &SWidgetReflector::SelectLiveWidget)
			.OnVisualizeWidget(this, &SWidgetReflector::SetWidgetsToVisualize)
		];
}

#endif //WITH_SLATE_DEBUGGING

void SWidgetReflector::HandleTabManagerPersistLayout(const TSharedRef<FTabManager::FLayout>& LayoutToSave)
{
#if WITH_EDITOR
	if (FUnrealEdMisc::Get().IsSavingLayoutOnClosedAllowed())
	{
		FLayoutSaveRestore::SaveToConfig(GEditorLayoutIni, LayoutToSave);
	}
#endif //WITH_EDITOR
}


void SWidgetReflector::SaveSettings()
{
	GConfig->SetArray(TEXT("WidgetReflector"), TEXT("HiddenReflectorTreeColumns"), HiddenReflectorTreeColumns, *GEditorPerProjectIni);
	GConfig->SetInt(TEXT("WidgetReflector"), TEXT("LastPickingMode"), static_cast<int32>(LastPickingMode), *GEditorPerProjectIni);
}


void SWidgetReflector::LoadSettings()
{
	int32 LastPickingModeAsInt = static_cast<int32>(EWidgetPickingMode::HitTesting);
	GConfig->GetInt(TEXT("WidgetReflector"), TEXT("LastPickingMode"), LastPickingModeAsInt, *GEditorPerProjectIni);
	LastPickingMode = ConvertToWidgetPickingMode(LastPickingModeAsInt);
	if (LastPickingMode == EWidgetPickingMode::None)
	{
		LastPickingMode = EWidgetPickingMode::HitTesting;
	}

	GConfig->GetArray(TEXT("WidgetReflector"), TEXT("HiddenReflectorTreeColumns"), HiddenReflectorTreeColumns, *GEditorPerProjectIni);
}


void SWidgetReflector::OnTabSpawned(const FName& TabIdentifier, const TSharedRef<SDockTab>& SpawnedTab)
{
	TWeakPtr<SDockTab>* const ExistingTab = SpawnedTabs.Find(TabIdentifier);
	if (!ExistingTab)
	{
		SpawnedTabs.Add(TabIdentifier, SpawnedTab);
	}
	else
	{
		check(!ExistingTab->IsValid());
		*ExistingTab = SpawnedTab;
	}
}


void SWidgetReflector::CloseTab(const FName& TabIdentifier)
{
	TWeakPtr<SDockTab>* const ExistingTab = SpawnedTabs.Find(TabIdentifier);
	if (ExistingTab)
	{
		TSharedPtr<SDockTab> ExistingTabPin = ExistingTab->Pin();
		if (ExistingTabPin.IsValid())
		{
			ExistingTabPin->RequestCloseTab();
		}
	}
}

void SWidgetReflector::SetUIMode(const EWidgetReflectorUIMode InNewMode)
{
	if (CurrentUIMode != InNewMode)
	{
		CurrentUIMode = InNewMode;

		SelectedNodes.Reset();
		PickedWidgetPath.Reset();
		ReflectorTreeRoot.Reset();
		FilteredTreeRoot.Reset();
		ReflectorTree->RequestTreeRefresh();

		if (CurrentUIMode == EWidgetReflectorUIMode::Snapshot)
		{
			TabManager->TryInvokeTab(WidgetReflectorTabID::SnapshotWidgetPicker);
		}
		else
		{
			SnapshotData.ClearSnapshot();

			if (WidgetSnapshotVisualizer.IsValid())
			{
				WidgetSnapshotVisualizer->SnapshotDataUpdated();
			}

			CloseTab(WidgetReflectorTabID::SnapshotWidgetPicker);
		}
	}
}


/* SCompoundWidget overrides
 *****************************************************************************/

void SWidgetReflector::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	if (bIsPendingDelayedSnapshot && FSlateApplication::Get().GetCurrentTime() > TimeOfScheduledSnapshot)
	{
		// TakeSnapshot leads to the widget being ticked indirectly recursively,
		// so the recursion of this tick mustn't trigger a recursive snapshot.
		// Immediately clear the pending snapshot flag.
		bIsPendingDelayedSnapshot = false;
		TimeOfScheduledSnapshot = -1.0;

		TakeSnapshot();
	}
}


/* IWidgetReflector overrides
 *****************************************************************************/

bool SWidgetReflector::ReflectorNeedsToDrawIn( TSharedRef<SWindow> ThisWindow ) const
{
	return ((SelectedNodes.Num() > 0) && (ReflectorTreeRoot.Num() > 0) && (ReflectorTreeRoot[0]->GetLiveWidget() == ThisWindow));
}

void SWidgetReflector::SetWidgetsToVisualize( const FWidgetPath& InWidgetsToVisualize )
{
	ReflectorTreeRoot.Reset();
	FilteredTreeRoot.Reset();
	PickedWidgetPath.Reset();
	SelectedNodes.Reset();

	if (InWidgetsToVisualize.IsValid())
	{
		ReflectorTreeRoot.Add(FWidgetReflectorNodeUtils::NewLiveNodeTreeFrom(InWidgetsToVisualize.Widgets[0]));
		FWidgetReflectorNodeUtils::FindLiveWidgetPath(ReflectorTreeRoot, InWidgetsToVisualize, PickedWidgetPath);
		UpdateFilteredTreeRoot();
	}

	ReflectorTree->RequestTreeRefresh();
}

int32 SWidgetReflector::Visualize( const FWidgetPath& InWidgetsToVisualize, FSlateWindowElementList& OutDrawElements, int32 LayerId )
{
	if (!InWidgetsToVisualize.IsValid() && SelectedNodes.Num() > 0 && ReflectorTreeRoot.Num() > 0)
	{
		TSharedPtr<SWidget> WindowWidget = ReflectorTreeRoot[0]->GetLiveWidget();
		if (WindowWidget.IsValid())
		{
			TSharedPtr<SWindow> Window = StaticCastSharedPtr<SWindow>(WindowWidget);
			return VisualizeSelectedNodesAsRectangles(SelectedNodes, Window.ToSharedRef(), OutDrawElements, LayerId);
		}
	}

	const bool bAttemptingToVisualizeReflector = InWidgetsToVisualize.ContainsWidget(ReflectorTree.ToSharedRef());

	if (PickingMode == EWidgetPickingMode::Drawable)
	{
		TSharedPtr<FVisualTreeSnapshot> Tree = VisualCapture.GetVisualTreeForWindow(OutDrawElements.GetPaintWindow());
		if (Tree.IsValid())
		{
			const FVector2D AbsPoint = FSlateApplication::Get().GetCursorPos();
			const FVector2D WindowPoint = AbsPoint - OutDrawElements.GetPaintWindow()->GetPositionInScreen();
			if (TSharedPtr<const SWidget> PickedWidget = Tree->Pick(WindowPoint))
			{
				FWidgetPath WidgetsToVisualize = InWidgetsToVisualize;
				FSlateApplication::Get().FindPathToWidget(PickedWidget.ToSharedRef(), WidgetsToVisualize, EVisibility::All);
				if (!bAttemptingToVisualizeReflector)
				{
					SetWidgetsToVisualize(WidgetsToVisualize);
					return VisualizePickAsRectangles(WidgetsToVisualize, OutDrawElements, LayerId);
				}
			}
		}
	}
	else if (!bAttemptingToVisualizeReflector)
	{
		SetWidgetsToVisualize(InWidgetsToVisualize);
		return VisualizePickAsRectangles(InWidgetsToVisualize, OutDrawElements, LayerId);
	}

	return LayerId;
}

/* SWidgetReflector implementation
 *****************************************************************************/

void SWidgetReflector::SelectLiveWidget(TSharedPtr<const SWidget> InWidget)
{
	bool bFound = false;
	if (this->CurrentUIMode == EWidgetReflectorUIMode::Live && InWidget)
	{
		TArray<TSharedRef<FWidgetReflectorNodeBase>> FoundList;
		FWidgetReflectorNodeUtils::FindLiveWidget(ReflectorTreeRoot, InWidget, FoundList);
		if (FoundList.Num() > 0)
		{
			for (const TSharedRef<FWidgetReflectorNodeBase>& FoundItem : FoundList)
			{
				ReflectorTree->SetItemExpansion(FoundItem, true);
			}
			ReflectorTree->RequestScrollIntoView(FoundList.Last());
			ReflectorTree->SetSelection(FoundList.Last());
			bFound = true;
		}
	}

	if (!bFound)
	{
		ReflectorTree->ClearSelection();
	}
}

namespace WidgetReflectorRecursive
{
	bool FindNodeWithReflectionData(const TArray<TSharedRef<FWidgetReflectorNodeBase>>& NodeBase, TArray<TSharedRef<FWidgetReflectorNodeBase>>& Result)
	{
		for (const TSharedRef<FWidgetReflectorNodeBase>& Node : NodeBase)
		{
			if (Node->HasValidWidgetAssetData())
			{
				return true;
			}

		}
		for (const TSharedRef<FWidgetReflectorNodeBase>& Node : NodeBase)
		{
			if (FindNodeWithReflectionData(Node->GetChildNodes(), Result))
			{
				Result.Add(Node);
			}
		}
		return false;
	}
}

void SWidgetReflector::UpdateFilteredTreeRoot()
{
	FilteredTreeRoot.Reset();
	if (bFilterReflectorTreeRootWithUMG)
	{
		WidgetReflectorRecursive::FindNodeWithReflectionData(ReflectorTreeRoot, FilteredTreeRoot);
		VisualizeAsTree(PickedWidgetPath);
	}
	else
	{
		FilteredTreeRoot = ReflectorTreeRoot;
		VisualizeAsTree(PickedWidgetPath);
	}
}

void SWidgetReflector::SetSelectedAsReflectorTreeRoot()
{
	if (SelectedNodes.Num() > 0)
	{
		FilteredTreeRoot.Reset();
		FilteredTreeRoot.Append(SelectedNodes);
		ReflectorTree->RequestTreeRefresh();
	}
}

TSharedRef<SToolTip> SWidgetReflector::GenerateToolTipForReflectorNode( TSharedRef<FWidgetReflectorNodeBase> InReflectorNode )
{
	return SNew(SToolTip)
		[
			SNew(SReflectorToolTipWidget)
				.WidgetInfoToVisualize(InReflectorNode)
		];
}

void SWidgetReflector::VisualizeAsTree( const TArray<TSharedRef<FWidgetReflectorNodeBase>>& WidgetPathToVisualize )
{
	if (WidgetPathToVisualize.Num() > 0)
	{
		const FLinearColor TopmostWidgetColor(1.0f, 0.0f, 0.0f);
		const FLinearColor LeafmostWidgetColor(0.0f, 1.0f, 0.0f);

		for (int32 WidgetIndex = 0; WidgetIndex < WidgetPathToVisualize.Num(); ++WidgetIndex)
		{
			const auto& CurWidget = WidgetPathToVisualize[WidgetIndex];

			// Tint the item based on depth in picked path
			const float ColorFactor = static_cast<float>(WidgetIndex) / WidgetPathToVisualize.Num();
			CurWidget->SetTint(FMath::Lerp(TopmostWidgetColor, LeafmostWidgetColor, ColorFactor));

			// Make sure the user can see the picked path in the tree.
			ReflectorTree->SetItemExpansion(CurWidget, true);
		}

		ReflectorTree->RequestScrollIntoView(WidgetPathToVisualize.Last());
		ReflectorTree->SetSelection(WidgetPathToVisualize.Last());
	}
	else
	{
		ReflectorTree->ClearSelection();
	}
}


int32 SWidgetReflector::VisualizePickAsRectangles( const FWidgetPath& InWidgetsToVisualize, FSlateWindowElementList& OutDrawElements, int32 LayerId)
{
	const FLinearColor TopmostWidgetColor(1.0f, 0.0f, 0.0f);
	const FLinearColor LeafmostWidgetColor(0.0f, 1.0f, 0.0f);

	for (int32 WidgetIndex = 0; WidgetIndex < InWidgetsToVisualize.Widgets.Num(); ++WidgetIndex)
	{
		const FArrangedWidget& WidgetGeometry = InWidgetsToVisualize.Widgets[WidgetIndex];
		const float ColorFactor = static_cast<float>(WidgetIndex)/InWidgetsToVisualize.Widgets.Num();
		const FLinearColor Tint(1.0f - ColorFactor, ColorFactor, 0.0f, 1.0f);

		// The FGeometry we get is from a WidgetPath, so it's rooted in desktop space.
		// We need to APPEND a transform to the Geometry to essentially undo this root transform
		// and get us back into Window Space.
		// This is nonstandard so we have to go through some hoops and a specially exposed method 
		// in FPaintGeometry to allow appending layout transforms.
		FPaintGeometry WindowSpaceGeometry = WidgetGeometry.Geometry.ToPaintGeometry();
		WindowSpaceGeometry.AppendTransform(TransformCast<FSlateLayoutTransform>(Inverse(InWidgetsToVisualize.TopLevelWindow->GetPositionInScreen())));

		FLinearColor Color = FMath::Lerp(TopmostWidgetColor, LeafmostWidgetColor, ColorFactor);
		DrawWidgetVisualization(WindowSpaceGeometry, Color, OutDrawElements, LayerId);
	}

	return LayerId;
}

int32 SWidgetReflector::VisualizeSelectedNodesAsRectangles( const TArray<TSharedRef<FWidgetReflectorNodeBase>>& InNodesToDraw, const TSharedRef<SWindow>& VisualizeInWindow, FSlateWindowElementList& OutDrawElements, int32 LayerId )
{
	for (int32 NodeIndex = 0; NodeIndex < InNodesToDraw.Num(); ++NodeIndex)
	{
		const TSharedRef<FWidgetReflectorNodeBase>& NodeToDraw = InNodesToDraw[NodeIndex];
		const FLinearColor Tint(0.0f, 1.0f, 0.0f);

		// The FGeometry we get is from a WidgetPath, so it's rooted in desktop space.
		// We need to APPEND a transform to the Geometry to essentially undo this root transform
		// and get us back into Window Space.
		// This is nonstandard so we have to go through some hoops and a specially exposed method 
		// in FPaintGeometry to allow appending layout transforms.
		FPaintGeometry WindowSpaceGeometry(NodeToDraw->GetAccumulatedLayoutTransform(), NodeToDraw->GetAccumulatedRenderTransform(), NodeToDraw->GetLocalSize(), NodeToDraw->GetGeometry().HasRenderTransform());
		WindowSpaceGeometry.AppendTransform(TransformCast<FSlateLayoutTransform>(Inverse(VisualizeInWindow->GetPositionInScreen())));

		DrawWidgetVisualization(WindowSpaceGeometry, NodeToDraw->GetTint(), OutDrawElements, LayerId);
	}

	return LayerId;
}

void SWidgetReflector::DrawWidgetVisualization(const FPaintGeometry& WidgetGeometry, FLinearColor Color, FSlateWindowElementList& OutDrawElements, int32& LayerId)
{
	WidgetGeometry.CommitTransformsIfUsingLegacyConstructor();
	const FVector2D LocalSize = WidgetGeometry.GetLocalSize();

	// If the size is 0 in any dimension, we're going to draw a line to represent the widget, since it's going to take up
	// padding space since it's visible, even though it's zero sized.
	if (FMath::IsNearlyZero(LocalSize.X) || FMath::IsNearlyZero(LocalSize.Y))
	{
		TArray<FVector2D> LinePoints;
		LinePoints.SetNum(2);

		LinePoints[0] = FVector2D::ZeroVector;
		LinePoints[1] = LocalSize;

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			++LayerId,
			WidgetGeometry,
			LinePoints,
			ESlateDrawEffect::None,
			Color,
			true,
			2
		);
	}
	else
	{
		// Draw a normal box border around the geometry
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			++LayerId,
			WidgetGeometry,
			FCoreStyle::Get().GetBrush(TEXT("Debug.Border")),
			ESlateDrawEffect::None,
			Color
		);
	}
}


/* SWidgetReflector callbacks
 *****************************************************************************/

void SWidgetReflector::HandleDisplayTextureAtlases()
{
	static const FName SlateReflectorModuleName("SlateReflector");
	FModuleManager::LoadModuleChecked<ISlateReflectorModule>(SlateReflectorModuleName).DisplayTextureAtlasVisualizer();
}

void SWidgetReflector::HandleDisplayFontAtlases()
{
	static const FName SlateReflectorModuleName("SlateReflector");
	FModuleManager::LoadModuleChecked<ISlateReflectorModule>(SlateReflectorModuleName).DisplayFontAtlasVisualizer();
}


/* Picking button
 *****************************************************************************/

ECheckBoxState SWidgetReflector::HandleGetPickingButtonChecked() const
{
	return PickingMode != EWidgetPickingMode::None ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SWidgetReflector::HandlePickingModeStateChanged(ECheckBoxState NewValue)
{
	if (PickingMode == EWidgetPickingMode::None)
	{
		SetPickingMode(LastPickingMode);
	}
	else
	{
		SetPickingMode(EWidgetPickingMode::None);
	}

	if (IsVisualizingLayoutUnderCursor())
	{
		SetUIMode(EWidgetReflectorUIMode::Live);
	}
}

const FSlateBrush* SWidgetReflector::HandleGetPickingModeImage() const
{
	switch (LastPickingMode)
	{
	case EWidgetPickingMode::Focus:
		return FWidgetReflectorStyle::Get().GetBrush(WidgetReflectorIcon::FocusPicking);
	case EWidgetPickingMode::HitTesting:
		return FWidgetReflectorStyle::Get().GetBrush(WidgetReflectorIcon::HitTestPicking);
	case EWidgetPickingMode::Drawable:
		return FWidgetReflectorStyle::Get().GetBrush(WidgetReflectorIcon::VisualPicking);
	case EWidgetPickingMode::None:
	default:
		return nullptr;
	}
	return nullptr;
}

FText SWidgetReflector::HandleGetPickingModeText() const
{
	if (PickingMode == EWidgetPickingMode::None)
	{
		switch(LastPickingMode)
		{
		case EWidgetPickingMode::Focus:
			return WidgetReflectorText::Focus;
		case EWidgetPickingMode::Drawable:
			return WidgetReflectorText::VisualPicking;
		case EWidgetPickingMode::HitTesting:
			return WidgetReflectorText::HitTestPicking;
		}
	}
	else if (PickingMode == EWidgetPickingMode::Focus)
	{
		return WidgetReflectorText::Focusing;
	}
	return WidgetReflectorText::Picking;
}

TSharedRef<SWidget> SWidgetReflector::HandlePickingModeContextMenu()
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, nullptr);

	const bool bIsFocus = PickingMode == EWidgetPickingMode::Focus;
	MenuBuilder.AddMenuEntry(
		WidgetReflectorText::Focus,
		FText::GetEmpty(),
		FSlateIcon(FWidgetReflectorStyle::GetStyleSetName(), WidgetReflectorIcon::FocusPicking),
		FUIAction(
			FExecuteAction::CreateSP(this, &SWidgetReflector::HandlePickButtonClicked, EWidgetPickingMode::Focus),
			FCanExecuteAction::CreateLambda([bIsFocus](){ return !bIsFocus; })
		));

	const bool bIsHitTestPicking = PickingMode == EWidgetPickingMode::HitTesting;
	MenuBuilder.AddMenuEntry(
		WidgetReflectorText::HitTestPicking,
		FText::GetEmpty(),
		FSlateIcon(FWidgetReflectorStyle::GetStyleSetName(), WidgetReflectorIcon::HitTestPicking),
		FUIAction(
			FExecuteAction::CreateSP(this, &SWidgetReflector::HandlePickButtonClicked, EWidgetPickingMode::HitTesting),
			FCanExecuteAction::CreateLambda([bIsHitTestPicking]() { return !bIsHitTestPicking; })
		));

	const bool bIsDrawable = PickingMode == EWidgetPickingMode::Drawable;
	MenuBuilder.AddMenuEntry(
		WidgetReflectorText::VisualPicking,
		FText::GetEmpty(),
		FSlateIcon(FWidgetReflectorStyle::GetStyleSetName(), WidgetReflectorIcon::VisualPicking),
		FUIAction(
			FExecuteAction::CreateSP(this, &SWidgetReflector::HandlePickButtonClicked, EWidgetPickingMode::Drawable),
			FCanExecuteAction::CreateLambda([bIsDrawable]() { return !bIsDrawable; })
		));

	return MenuBuilder.MakeWidget();
}

void SWidgetReflector::HandlePickButtonClicked(EWidgetPickingMode InPickingMode)
{
	bool bHasChanged = LastPickingMode != InPickingMode;
	LastPickingMode = InPickingMode;
	SetPickingMode(PickingMode != InPickingMode ? InPickingMode : EWidgetPickingMode::None);

	if (IsVisualizingLayoutUnderCursor())
	{
		SetUIMode(EWidgetReflectorUIMode::Live);
	}

	if (bHasChanged)
	{
		SaveSettings();
	}
}

bool SWidgetReflector::IsSnapshotTargetComboEnabled() const
{
	if (bIsPendingDelayedSnapshot)
	{
		return false;
	}

#if SLATE_REFLECTOR_HAS_SESSION_SERVICES
	return !RemoteSnapshotRequestId.IsValid();
#else
	return false;
#endif
}

bool SWidgetReflector::IsTakeSnapshotButtonEnabled() const
{
	return SelectedSnapshotTargetInstanceId.IsValid() && !RemoteSnapshotRequestId.IsValid();
}

FReply SWidgetReflector::HandleTakeSnapshotButtonClicked()
{
	if (!bIsPendingDelayedSnapshot)
	{
		if (SnapshotDelay > 0.0f)
		{
			bIsPendingDelayedSnapshot = true;
			TimeOfScheduledSnapshot = FSlateApplication::Get().GetCurrentTime() + SnapshotDelay;
		}
		else
		{
			TakeSnapshot();
		}
	}
	else
	{
		bIsPendingDelayedSnapshot = false;
		TimeOfScheduledSnapshot = -1.0f;
	}

	return FReply::Handled();
}

TSharedRef<SWidget> SWidgetReflector::HandleSnapshotOptionsTreeContextMenu()
{
	TSharedRef<SWidget> DelayWidget = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("DelayLabel", "Delay"))
		]
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Right)
		[
			SNew(SSpinBox<float>)
			.MinValue(0)
			.MinDesiredWidth(40)
			.Value_Lambda([this]() { return SnapshotDelay; })
			.OnValueCommitted_Lambda([this](const float InValue, ETextCommit::Type) { SnapshotDelay = FMath::Max(0.0f, InValue); })
		];

	TSharedRef<SWidget> NavigationEventSimulationWidget = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NavigationEventSimulationLabel", "Navigation Event Simulation"))
			.ToolTipText(LOCTEXT("NavigationEventSimulationTooltip", "Build a simulation of all the possible Navigation Events that can occur in the windows."))
		]
		+ SHorizontalBox::Slot()
		.Padding(FMargin(4.f, 0.f))
		.HAlign(HAlign_Right)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([this]() { return bRequestNavigationSimulation ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { bRequestNavigationSimulation = NewState == ECheckBoxState::Checked; })
		];

	return SNew(SVerticalBox)
	+ SVerticalBox::Slot()
	.Padding(2.f)
	[
		DelayWidget
	]
	+ SVerticalBox::Slot()
	.Padding(2.f)
	[
		NavigationEventSimulationWidget
	]
	+ SVerticalBox::Slot()
	.Padding(2.f)
	[
		AvailableSnapshotTargetsComboBox.ToSharedRef()
	];
}

void SWidgetReflector::TakeSnapshot()
{
	// Local snapshot?
	if (SelectedSnapshotTargetInstanceId == FApp::GetInstanceId())
	{
		SetUIMode(EWidgetReflectorUIMode::Snapshot);

#if WITH_SLATE_DEBUGGING
		if (TSharedPtr<SWidgetHittestGrid> WidgetHittestGridPin = WidgetHittestGrid.Pin())
		{
			WidgetHittestGridPin->SetPause(true);
		}
#endif

		// Take a snapshot of any window(s) that are currently open
		SnapshotData.TakeSnapshot(bRequestNavigationSimulation);

		// Rebuild the reflector tree from the snapshot data
		SelectedNodes.Reset();
		PickedWidgetPath.Reset();
		ReflectorTreeRoot = FilteredTreeRoot = SnapshotData.GetWindowsRef();
		ReflectorTree->RequestTreeRefresh();

		WidgetSnapshotVisualizer->SnapshotDataUpdated();

#if WITH_SLATE_DEBUGGING
		if (TSharedPtr<SWidgetHittestGrid> WidgetHittestGridPin = WidgetHittestGrid.Pin())
		{
			WidgetHittestGridPin->SetPause(false);
		}
#endif
	}
	else
	{
		// Remote snapshot - these can take a while, show a progress message
		FNotificationInfo Info(LOCTEXT("RemoteWidgetSnapshotPendingNotificationText", "Waiting for Remote Widget Snapshot Data"));

		// Add the buttons with text, tooltip and callback
		Info.ButtonDetails.Add(FNotificationButtonInfo(
			LOCTEXT("CancelPendingSnapshotButtonText", "Cancel"),
			LOCTEXT("CancelPendingSnapshotButtonToolTipText", "Cancel the pending widget snapshot request."),
			FSimpleDelegate::CreateSP(this, &SWidgetReflector::OnCancelPendingRemoteSnapshot)
		));

		// We will be keeping track of this ourselves
		Info.bFireAndForget = false;

		// Launch notification
		WidgetSnapshotNotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);

		if (WidgetSnapshotNotificationPtr.IsValid())
		{
			WidgetSnapshotNotificationPtr.Pin()->SetCompletionState(SNotificationItem::CS_Pending);
		}

		RemoteSnapshotRequestId = WidgetSnapshotService->RequestSnapshot(SelectedSnapshotTargetInstanceId, FWidgetSnapshotService::FOnWidgetSnapshotResponse::CreateSP(this, &SWidgetReflector::HandleRemoteSnapshotReceived));

		if (!RemoteSnapshotRequestId.IsValid())
		{
			TSharedPtr<SNotificationItem> WidgetSnapshotNotificationPin = WidgetSnapshotNotificationPtr.Pin();

			if (WidgetSnapshotNotificationPin.IsValid())
			{
				WidgetSnapshotNotificationPin->SetText(LOCTEXT("RemoteWidgetSnapshotFailedNotificationText", "Remote Widget Snapshot Failed"));
				WidgetSnapshotNotificationPin->SetCompletionState(SNotificationItem::CS_Fail);
				WidgetSnapshotNotificationPin->ExpireAndFadeout();

				WidgetSnapshotNotificationPtr.Reset();
			}
		}
	}
}

void SWidgetReflector::OnCancelPendingRemoteSnapshot()
{
	TSharedPtr<SNotificationItem> WidgetSnapshotNotificationPin = WidgetSnapshotNotificationPtr.Pin();

	if (WidgetSnapshotNotificationPin.IsValid())
	{
		WidgetSnapshotNotificationPin->SetText(LOCTEXT("RemoteWidgetSnapshotAbortedNotificationText", "Aborted Remote Widget Snapshot"));
		WidgetSnapshotNotificationPin->SetCompletionState(SNotificationItem::CS_Fail);
		WidgetSnapshotNotificationPin->ExpireAndFadeout();

		WidgetSnapshotNotificationPtr.Reset();
	}

	WidgetSnapshotService->AbortSnapshotRequest(RemoteSnapshotRequestId);
	RemoteSnapshotRequestId = FGuid();
}

void SWidgetReflector::HandleRemoteSnapshotReceived(const TArray<uint8>& InSnapshotData)
{
	{
		TSharedPtr<SNotificationItem> WidgetSnapshotNotificationPin = WidgetSnapshotNotificationPtr.Pin();

		if (WidgetSnapshotNotificationPin.IsValid())
		{
			WidgetSnapshotNotificationPin->SetText(LOCTEXT("RemoteWidgetSnapshotReceivedNotificationText", "Remote Widget Snapshot Data Received"));
			WidgetSnapshotNotificationPin->SetCompletionState(SNotificationItem::CS_Success);
			WidgetSnapshotNotificationPin->ExpireAndFadeout();

			WidgetSnapshotNotificationPtr.Reset();
		}
	}

	RemoteSnapshotRequestId = FGuid();

	SetUIMode(EWidgetReflectorUIMode::Snapshot);

	// Load up the remote data
	SnapshotData.LoadSnapshotFromBuffer(InSnapshotData);

	// Rebuild the reflector tree from the snapshot data
	SelectedNodes.Reset();
	PickedWidgetPath.Reset();
	ReflectorTreeRoot = FilteredTreeRoot = SnapshotData.GetWindowsRef();
	ReflectorTree->RequestTreeRefresh();

	WidgetSnapshotVisualizer->SnapshotDataUpdated();
}

#if SLATE_REFLECTOR_HAS_DESKTOP_PLATFORM

FReply SWidgetReflector::HandleLoadSnapshotButtonClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();

	if (DesktopPlatform)
	{
		TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(SharedThis(this));

		TArray<FString> OpenFilenames;
		const bool bOpened = DesktopPlatform->OpenFileDialog(
			(ParentWindow.IsValid()) ? ParentWindow->GetNativeWindow()->GetOSWindowHandle() : nullptr,
			LOCTEXT("LoadSnapshotDialogTitle", "Load Widget Snapshot").ToString(),
			FPaths::GameAgnosticSavedDir(),
			TEXT(""),
			TEXT("Slate Widget Snapshot (*.widgetsnapshot)|*.widgetsnapshot"),
			EFileDialogFlags::None,
			OpenFilenames
			);

		if (bOpened && SnapshotData.LoadSnapshotFromFile(OpenFilenames[0]))
		{
			SetUIMode(EWidgetReflectorUIMode::Snapshot);

			// Rebuild the reflector tree from the snapshot data
			ReflectorTreeRoot = SnapshotData.GetWindowsRef();
			ReflectorTree->RequestTreeRefresh();

			WidgetSnapshotVisualizer->SnapshotDataUpdated();
		}
	}

	return FReply::Handled();
}

#endif // SLATE_REFLECTOR_HAS_DESKTOP_PLATFORM

void SWidgetReflector::UpdateAvailableSnapshotTargets()
{
	AvailableSnapshotTargets.Reset();

#if SLATE_REFLECTOR_HAS_SESSION_SERVICES
	{
		TSharedPtr<ISessionManager> SessionManager = FModuleManager::LoadModuleChecked<ISessionServicesModule>("SessionServices").GetSessionManager();
		if (SessionManager.IsValid())
		{
			TArray<TSharedPtr<ISessionInfo>> AvailableSessions;
			SessionManager->GetSessions(AvailableSessions);

			for (const auto& AvailableSession : AvailableSessions)
			{
				// Only allow sessions belonging to the current user
				if (AvailableSession->GetSessionOwner() != FApp::GetSessionOwner())
				{
					continue;
				}

				TArray<TSharedPtr<ISessionInstanceInfo>> AvailableInstances;
				AvailableSession->GetInstances(AvailableInstances);

				for (const auto& AvailableInstance : AvailableInstances)
				{
					FWidgetSnapshotTarget SnapshotTarget;
					SnapshotTarget.DisplayName = FText::Format(LOCTEXT("SnapshotTargetDisplayNameFmt", "{0} ({1})"), FText::FromString(AvailableInstance->GetInstanceName()), FText::FromString(AvailableInstance->GetPlatformName()));
					SnapshotTarget.InstanceId = AvailableInstance->GetInstanceId();

					AvailableSnapshotTargets.Add(MakeShareable(new FWidgetSnapshotTarget(SnapshotTarget)));
				}
			}
		}
	}
#else
	{
		// No session services, just add an entry that lets us snapshot ourself
		FWidgetSnapshotTarget SnapshotTarget;
		SnapshotTarget.DisplayName = FText::FromString(FApp::GetInstanceName());
		SnapshotTarget.InstanceId = FApp::GetInstanceId();

		AvailableSnapshotTargets.Add(MakeShareable(new FWidgetSnapshotTarget(SnapshotTarget)));
	}
#endif
}

void SWidgetReflector::UpdateSelectedSnapshotTarget()
{
	if (AvailableSnapshotTargetsComboBox.IsValid())
	{
		const TSharedPtr<FWidgetSnapshotTarget>* FoundSnapshotTarget = AvailableSnapshotTargets.FindByPredicate([this](const TSharedPtr<FWidgetSnapshotTarget>& InAvailableSnapshotTarget) -> bool
		{
			return InAvailableSnapshotTarget->InstanceId == SelectedSnapshotTargetInstanceId;
		});

		if (FoundSnapshotTarget)
		{
			AvailableSnapshotTargetsComboBox->SetSelectedItem(*FoundSnapshotTarget);
		}
		else if (AvailableSnapshotTargets.Num() > 0)
		{
			SelectedSnapshotTargetInstanceId = AvailableSnapshotTargets[0]->InstanceId;
			AvailableSnapshotTargetsComboBox->SetSelectedItem(AvailableSnapshotTargets[0]);
		}
		else
		{
			SelectedSnapshotTargetInstanceId = FGuid();
			AvailableSnapshotTargetsComboBox->SetSelectedItem(nullptr);
		}
	}
}

void SWidgetReflector::OnAvailableSnapshotTargetsChanged()
{
	UpdateAvailableSnapshotTargets();
	UpdateSelectedSnapshotTarget();
}

FText SWidgetReflector::GetSelectedSnapshotTargetDisplayName() const
{
	if (AvailableSnapshotTargetsComboBox.IsValid())
	{
		TSharedPtr<FWidgetSnapshotTarget> SelectedSnapshotTarget = AvailableSnapshotTargetsComboBox->GetSelectedItem();
		if (SelectedSnapshotTarget.IsValid())
		{
			return SelectedSnapshotTarget->DisplayName;
		}
	}

	return FText::GetEmpty();
}

TSharedRef<SWidget> SWidgetReflector::HandleGenerateAvailableSnapshotComboItemWidget(TSharedPtr<FWidgetSnapshotTarget> InItem) const
{
	return SNew(STextBlock)
		.Text(InItem->DisplayName);
}

void SWidgetReflector::HandleAvailableSnapshotComboSelectionChanged(TSharedPtr<FWidgetSnapshotTarget> InItem, ESelectInfo::Type InSeletionInfo)
{
	if (InItem.IsValid())
	{
		SelectedSnapshotTargetInstanceId = InItem->InstanceId;
	}
	else
	{
		SelectedSnapshotTargetInstanceId = FGuid();
	}
}

TSharedRef<ITableRow> SWidgetReflector::HandleReflectorTreeGenerateRow( TSharedRef<FWidgetReflectorNodeBase> InReflectorNode, const TSharedRef<STableViewBase>& OwnerTable )
{
	return SNew(SReflectorTreeWidgetItem, OwnerTable)
		.WidgetInfoToVisualize(InReflectorNode)
		.ToolTip(GenerateToolTipForReflectorNode(InReflectorNode))
		.SourceCodeAccessor(SourceAccessDelegate)
		.AssetAccessor(AsseetAccessDelegate);
}

void SWidgetReflector::HandleReflectorTreeGetChildren(TSharedRef<FWidgetReflectorNodeBase> InReflectorNode, TArray<TSharedRef<FWidgetReflectorNodeBase>>& OutChildren)
{
	OutChildren = InReflectorNode->GetChildNodes();
}

void SWidgetReflector::HandleReflectorTreeSelectionChanged( TSharedPtr<FWidgetReflectorNodeBase>, ESelectInfo::Type /*SelectInfo*/ )
{
	SelectedNodes = ReflectorTree->GetSelectedItems();

	if (CurrentUIMode == EWidgetReflectorUIMode::Snapshot)
	{
		WidgetSnapshotVisualizer->SetSelectedWidgets(SelectedNodes);
	}

#if WITH_EDITOR
	TArray<UObject*> SelectedWidgetObjects;
	for (TSharedRef<FWidgetReflectorNodeBase>& Node : SelectedNodes)
	{
		TSharedPtr<SWidget> Widget = Node->GetLiveWidget();
		if (Widget.IsValid())
		{
			TSharedPtr<FReflectionMetaData> ReflectinMetaData = Widget->GetMetaData<FReflectionMetaData>();
			if (ReflectinMetaData.IsValid())
			{
				if (UObject* SourceObject = ReflectinMetaData->SourceObject.Get())
				{
					SelectedWidgetObjects.Add(SourceObject);
				}
			}
		}
	}

	if (SelectedWidgetObjects.Num() > 0)
	{
		TabManager->TryInvokeTab(WidgetReflectorTabID::WidgetDetails);
		if (PropertyViewPtr.IsValid())
		{
			PropertyViewPtr->SetObjects(SelectedWidgetObjects);
		}
	}
	//else
	//{
	//	CloseTab(WidgetReflectorTabID::WidgetDetails);
	//}
#endif
}

TSharedRef<SWidget> SWidgetReflector::HandleReflectorTreeContextMenu()
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, nullptr);

	bool bHasFilteredTreeRoot = ReflectorTreeRoot != FilteredTreeRoot;

	MenuBuilder.AddMenuEntry(
		LOCTEXT("SetAsRootLabel", "Selected node as root"),
		LOCTEXT("SetAsRootTooltip", "Set selected node as the root of the graph"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &SWidgetReflector::SetSelectedAsReflectorTreeRoot),
			FCanExecuteAction::CreateSP(this, &SWidgetReflector::DoesReflectorTreeHasSelectedItem)
		));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("ShowOnlyUMGLabel", "UMG as root"),
		LOCTEXT("ShowOnlyUMGTooltip", "Set UMG as the root of the graph"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &SWidgetReflector::HandleStartTreeWithUMG),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &SWidgetReflector::HandleIsStartTreeWithUMGEnabled)
		),
		NAME_None,
		EUserInterfaceActionType::ToggleButton);

	MenuBuilder.AddMenuSeparator();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("ResetRoot", "Reset filter"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &SWidgetReflector::HandleResetFilteredTreeRoot),
			FCanExecuteAction::CreateLambda([bHasFilteredTreeRoot](){ return bHasFilteredTreeRoot; })
		));

	return MenuBuilder.MakeWidget();
}

TSharedPtr<SWidget> SWidgetReflector::HandleReflectorTreeContextMenuPtr()
{
	return HandleReflectorTreeContextMenu();
}

void SWidgetReflector::HandleReflectorTreeHiddenColumnsListChanged()
{
#if WITH_EDITOR
	if (ReflectorTree && ReflectorTree->GetHeaderRow())
	{
		const TArray<FName> HiddenColumnIds = ReflectorTree->GetHeaderRow()->GetHiddenColumnIds();
		HiddenReflectorTreeColumns.Reset(HiddenColumnIds.Num());
		for (const FName Id : HiddenColumnIds)
		{
			HiddenReflectorTreeColumns.Add(Id.ToString());
		}
		SaveSettings();
	}
#endif
}

void SWidgetReflector::HandleResetFilteredTreeRoot()
{
	bFilterReflectorTreeRootWithUMG = false;
	UpdateFilteredTreeRoot();
	ReflectorTree->RequestTreeRefresh();
}

void SWidgetReflector::HandleStartTreeWithUMG()
{
	bFilterReflectorTreeRootWithUMG = !bFilterReflectorTreeRootWithUMG;
	UpdateFilteredTreeRoot();
	ReflectorTree->RequestTreeRefresh();
}

} // namespace WidgetReflectorImpl

TSharedRef<SWidgetReflector> SWidgetReflector::New()
{
  return MakeShareable( new WidgetReflectorImpl::SWidgetReflector() );
}

#undef LOCTEXT_NAMESPACE
