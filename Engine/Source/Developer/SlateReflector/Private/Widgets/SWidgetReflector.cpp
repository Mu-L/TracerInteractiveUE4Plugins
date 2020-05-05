// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SWidgetReflector.h"
#include "Rendering/DrawElements.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "Framework/Application/SlateApplication.h"
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
#include "Framework/Docking/TabManager.h"
#include "Models/WidgetReflectorNode.h"
#include "Widgets/SWidgetReflectorTreeWidgetItem.h"
#include "Widgets/SWidgetReflectorToolTipWidget.h"
#include "ISlateReflectorModule.h"
#include "Widgets/SInvalidationPanel.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SWidgetSnapshotVisualizer.h"
#include "WidgetSnapshotService.h"
#include "Widgets/Input/SNumericDropDown.h"
#include "Types/ReflectionMetadata.h"
#include "Widgets/Input/SSearchBox.h"
#include "Misc/TextFilterExpressionEvaluator.h"
#include "Debugging/SlateDebugging.h"
#include "VisualTreeCapture.h"
#include "SWidgetEventLog.h"

#if SLATE_REFLECTOR_HAS_DESKTOP_PLATFORM
#include "DesktopPlatformModule.h"
#endif // SLATE_REFLECTOR_HAS_DESKTOP_PLATFORM

#if SLATE_REFLECTOR_HAS_SESSION_SERVICES
#include "ISessionManager.h"
#include "ISessionServicesModule.h"
#endif // SLATE_REFLECTOR_HAS_SESSION_SERVICES

#if WITH_EDITOR
#include "PropertyEditorModule.h"
#endif

#define LOCTEXT_NAMESPACE "SWidgetReflector"
#define WITH_EVENT_LOGGING 0


static const int32 MaxLoggedEvents = 100;

/**
 * Widget reflector user widget.
 */

/* Local helpers
 *****************************************************************************/

struct FLoggedEvent
{
	FLoggedEvent( const FInputEvent& InEvent, const FReplyBase& InReply )
		: Event(InEvent)
		, Handler(InReply.GetHandler())
		, EventText(InEvent.ToText())
		, HandlerText(InReply.GetHandler().IsValid() ? FText::FromString(InReply.GetHandler()->ToString()) : LOCTEXT("NullHandler", "null"))
	{ }

	FText ToText()
	{
		return FText::Format(LOCTEXT("LoggedEvent","{0}  |  {1}"), EventText, HandlerText);
	}
	
	FInputEvent Event;
	TWeakPtr<SWidget> Handler;
	FText EventText;
	FText HandlerText;
};

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
	static const FName SlateStats = "WidgetReflector.SlateStatsTab";
	static const FName SnapshotWidgetPicker = "WidgetReflector.SnapshotWidgetPickerTab";
	static const FName WidgetDetails = "WidgetReflector.WidgetDetailsTab";
	static const FName WidgetEvents = "WidgetReflector.WidgetEventsTab";
}


enum class EWidgetPickingMode : uint8
{
	None,
	Focus,
	HitTesting,
	Drawable
};

/**
 * Widget reflector implementation
 */
class SWidgetReflector : public ::SWidgetReflector
{
	// The reflector uses a tree that observes FWidgetReflectorNodeBase objects.
	typedef STreeView<TSharedRef<FWidgetReflectorNodeBase>> SReflectorTree;

public:

	~SWidgetReflector();

private:

	virtual void Construct( const FArguments& InArgs ) override;

	TSharedRef<SDockTab> SpawnWidgetHierarchyTab(const FSpawnTabArgs& Args);

	TSharedRef<SDockTab> SpawnSnapshotWidgetPicker(const FSpawnTabArgs& Args);

#if WITH_EDITOR
	TSharedRef<SDockTab> SpawnWidgetDetails(const FSpawnTabArgs& Args);
#endif

#if WITH_SLATE_DEBUGGING
	TSharedRef<SDockTab> SpawnWidgetEvents(const FSpawnTabArgs& Args);
#endif

	void OnTabSpawned(const FName& TabIdentifier, const TSharedRef<SDockTab>& SpawnedTab);

	void CloseTab(const FName& TabIdentifier);

	void OnFilterTextChanged(const FText& InFilterText);
	void OnFilterTextCommitted(const FText& InText, ETextCommit::Type CommitInfo);

	void SetUIMode(const EWidgetReflectorUIMode InNewMode);

	// SCompoundWidget overrides
	virtual void Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime ) override;

	// IWidgetReflector interface

	virtual void OnEventProcessed( const FInputEvent& Event, const FReplyBase& InReply ) override;

	virtual bool IsInPickingMode() const override
	{
		return PickingMode == EWidgetPickingMode::HitTesting || PickingMode == EWidgetPickingMode::Drawable;
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
	virtual int32 VisualizeCursorAndKeys(FSlateWindowElementList& OutDrawElements, int32 LayerId) const override;

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

	/** Callback for changing the application scale slider. */
	void HandleAppScaleSliderChanged( float NewValue )
	{
		FSlateApplication::Get().SetApplicationScale(NewValue);
	}

	FReply HandleDisplayTextureAtlases();
	FReply HandleDisplayFontAtlases();

	/** Callback for getting the value of the application scale slider. */
	float HandleAppScaleSliderValue() const
	{
		return FSlateApplication::Get().GetApplicationScale();
	}

	/** Callback for checked state changes of the focus check box. */
	void HandleFocusCheckBoxCheckedStateChanged( ECheckBoxState NewValue );

	/** Callback for getting the checked state of the focus check box. */
	ECheckBoxState HandleFocusCheckBoxIsChecked() const
	{
		return PickingMode == EWidgetPickingMode::Focus ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	/** Callback for clicking the pick button. */
	FReply HandlePickButtonClicked(EWidgetPickingMode InPickingMode)
	{
		SetPickingMode(PickingMode != InPickingMode ? InPickingMode : EWidgetPickingMode::None);

		if (IsVisualizingLayoutUnderCursor())
		{
			SetUIMode(EWidgetReflectorUIMode::Live);
		}

		return FReply::Handled();
	}

	void SetPickingMode(EWidgetPickingMode InMode)
	{
		if (PickingMode != InMode)
		{
			// Disable visual picking, and renable widget caching.
#if WITH_SLATE_DEBUGGING
			SInvalidationPanel::EnableInvalidationPanels(true);
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
#endif
			}
		}
	}

	/** Callback for getting the color of the pick button text. */
	FSlateColor HandlePickButtonColorAndOpacity(EWidgetPickingMode InPickingMode) const
	{
		static const FName SelectionColor("SelectionColor");

		return PickingMode == InPickingMode
			? FCoreStyle::Get().GetSlateColor(SelectionColor)
			: FLinearColor::White;
	}

	/** Callback for getting the text of the pick button. */
	FText HandlePickButtonText(EWidgetPickingMode InPickingMode) const;

	/** Callback to see whether the "Snapshot Target" combo should be enabled */
	bool IsSnapshotTargetComboEnabled() const;

	/** Callback to see whether the "Take Snapshot" button should be enabled */
	bool IsTakeSnapshotButtonEnabled() const;

	/** Callback for clicking the "Take Snapshot" button. */
	FReply HandleTakeSnapshotButtonClicked();

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

	TSharedRef<ITableRow> GenerateEventLogRow( TSharedRef<FLoggedEvent> InReflectorNode, const TSharedRef<STableViewBase>& OwnerTable );

	EWidgetReflectorUIMode CurrentUIMode;

	TSharedPtr<FTabManager> TabManager;
	TMap<FName, TWeakPtr<SDockTab>> SpawnedTabs;

	TArray< TSharedRef<FLoggedEvent> > LoggedEvents;
	TSharedPtr< SListView< TSharedRef< FLoggedEvent > > > EventListView;
	TSharedPtr<SReflectorTree> ReflectorTree;

	TSharedPtr<SSearchBox> SearchBox;

	/** Compiled filter search terms. */
	TSharedPtr<FTextFilterExpressionEvaluator> TextFilterPtr;

	TArray<TSharedRef<FWidgetReflectorNodeBase>> SelectedNodes;
	TArray<TSharedRef<FWidgetReflectorNodeBase>> ReflectorTreeRoot;
	TArray<TSharedRef<FWidgetReflectorNodeBase>> PickedPath;

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

	SSplitter::FSlot* WidgetInfoLocation;

	FAccessSourceCode SourceAccessDelegate;
	FAccessAsset AsseetAccessDelegate;

	EWidgetPickingMode PickingMode;


#if WITH_EDITOR
	TSharedPtr<IDetailsView> PropertyViewPtr;
#endif

	FVisualTreeCapture VisualCapture;

private:
	// DEMO MODE
	bool bEnableDemoMode;
	double LastMouseClickTime;
	FVector2D CursorPingPosition;

	float SnapshotDelay;
	bool bIsPendingDelayedSnapshot;
	double TimeOfScheduledSnapshot;
};


SWidgetReflector::~SWidgetReflector()
{
	TabManager->UnregisterTabSpawner(WidgetReflectorTabID::WidgetHierarchy);
	TabManager->UnregisterTabSpawner(WidgetReflectorTabID::SlateStats);
	TabManager->UnregisterTabSpawner(WidgetReflectorTabID::SnapshotWidgetPicker);
}

void SWidgetReflector::Construct( const FArguments& InArgs )
{
	LoggedEvents.Reserve(MaxLoggedEvents);

	CurrentUIMode = EWidgetReflectorUIMode::Live;

	PickingMode = EWidgetPickingMode::None;

	bEnableDemoMode = false;
	LastMouseClickTime = -1.0;
	CursorPingPosition = FVector2D::ZeroVector;

	SnapshotDelay = 0.0f;
	bIsPendingDelayedSnapshot = false;
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

	const FName TabLayoutName = "WidgetReflector_Layout_NoStats_v1";

	TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout(TabLayoutName)
	->AddArea
	(
		FTabManager::NewPrimaryArea()
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
			//->Split
			//(
			//	FTabManager::NewStack()
			//	->SetHideTabWell(true)
			//	->SetSizeCoefficient(0.3f)
			//	->AddTab(WidgetReflectorTabID::SnapshotWidgetPicker, ETabState::ClosedTab)
			//	->AddTab(WidgetReflectorTabID::WidgetEvents, ETabState::OpenedTab)
			//)
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
#endif

#if WITH_SLATE_DEBUGGING
	RegisterTrackedTabSpawner(WidgetReflectorTabID::WidgetEvents, FOnSpawnTab::CreateSP(this, &SWidgetReflector::SpawnWidgetEvents))
		.SetDisplayName(LOCTEXT("WidgetEventsTab", "Widget Events"));
#endif

	this->ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(FLinearColor::Gray) // Darken the outer border
		[
			SNew(SVerticalBox)
				
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))
			[
				SNew(SHorizontalBox)
					
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AppScale", "Application Scale: "))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.MinDesiredWidth(100)
					.MaxDesiredWidth(250)
					[
						SNew(SSpinBox<float>)
						.Value(this, &SWidgetReflector::HandleAppScaleSliderValue)
						.MinValue(0.50f)
						.MaxValue(3.0f)
						.Delta(0.01f)
						.OnValueChanged(this, &SWidgetReflector::HandleAppScaleSliderChanged)
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(5.0f, 0.0f))
				[
					SNew(SCheckBox)
					.Style(FCoreStyle::Get(), "ToggleButtonCheckbox")
					.IsChecked_Lambda([&]()
					{
#if WITH_SLATE_DEBUGGING
						return SInvalidationPanel::AreInvalidationPanelsEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
#else
						return ECheckBoxState::Unchecked;
#endif
					})
					.OnCheckStateChanged_Lambda([&](const ECheckBoxState NewState)
					{
#if WITH_SLATE_DEBUGGING
						SInvalidationPanel::EnableInvalidationPanels(( NewState == ECheckBoxState::Checked ) ? true : false);
#endif
					})
					[
						SNew(SBox)
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Center)
						.Padding(FMargin(4.0, 2.0))
						[
							SNew(STextBlock)
							.Text(LOCTEXT("EnableWidgetCaching", "Widget Caching"))
						]
					]
				]

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(5.0f, 0.0f))
				[
					SNew(SCheckBox)
					.Style(FCoreStyle::Get(), "ToggleButtonCheckbox")
					.IsChecked_Lambda([&]()
					{
						return GSlateInvalidationDebugging ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([&](const ECheckBoxState NewState)
					{
						GSlateInvalidationDebugging = ( NewState == ECheckBoxState::Checked ) ? true : false;
					})
					[
						SNew(SBox)
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Center)
						.Padding(FMargin(4.0, 2.0))
						[
							SNew(STextBlock)
							.Text(LOCTEXT("InvalidationDebugging", "Invalidation Debugging"))
						]
					]
				]
#endif

				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(5.0f, 0.0f))
				[
					SNew(SCheckBox)
					.Style(FCoreStyle::Get(), "ToggleButtonCheckbox")
					.IsChecked_Lambda([&]()
					{
						return bEnableDemoMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([&](const ECheckBoxState NewState)
					{
						bEnableDemoMode = (NewState == ECheckBoxState::Checked) ? true : false;
					})
					[
						SNew(SBox)
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Center)
						.Padding(FMargin(4.0, 2.0))
						[
							SNew(STextBlock)
							.Text(LOCTEXT("EnableDemoMode", "Demo Mode"))
						]
					]
				]
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SSpacer)
				]

				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(5.0f, 0.0f))
				[
					SNew(SButton)
					.Text(LOCTEXT("DisplayTextureAtlases", "Display Texture Atlases"))
					.OnClicked(this, &SWidgetReflector::HandleDisplayTextureAtlases)
				]

				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(5.0f, 0.0f))
				[
					SNew(SButton)
					.Text(LOCTEXT("DisplayFontAtlases", "Display Font Atlases"))
					.OnClicked(this, &SWidgetReflector::HandleDisplayFontAtlases)
				]
			]

			+SVerticalBox::Slot()
			[
				TabManager->RestoreFrom(Layout, nullptr).ToSharedRef()
			]
		]
	];
}


TSharedRef<SDockTab> SWidgetReflector::SpawnWidgetHierarchyTab(const FSpawnTabArgs& Args)
{
	TArray<SNumericDropDown<float>::FNamedValue> NamedValuesForSnapshotDelay;
	NamedValuesForSnapshotDelay.Add(SNumericDropDown<float>::FNamedValue(0.0f, LOCTEXT("NoDelayValueName", "None"), LOCTEXT("NoDelayValueDescription", "Snapshot will be taken immediately upon clickng to take the snapshot.")));

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
					// Check box that controls LIVE MODE
					SNew(SCheckBox)
					.IsChecked(this, &SWidgetReflector::HandleFocusCheckBoxIsChecked)
					.OnCheckStateChanged(this, &SWidgetReflector::HandleFocusCheckBoxCheckedStateChanged)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ShowFocus", "Show Focus"))
					]
                ]
				
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(5.0f, 0.0f))
				[
					// Check box that controls PICKING A WIDGET TO INSPECT
					SNew(SButton)
					.IsEnabled_Lambda([this]() { return !bIsPendingDelayedSnapshot; })
					.OnClicked(this, &SWidgetReflector::HandlePickButtonClicked, EWidgetPickingMode::HitTesting)
					.ButtonColorAndOpacity(this, &SWidgetReflector::HandlePickButtonColorAndOpacity, EWidgetPickingMode::HitTesting)
					[
						SNew(STextBlock)
						.Text(this, &SWidgetReflector::HandlePickButtonText, EWidgetPickingMode::HitTesting)
					]
				]

				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(5.0f, 0.0f))
				[
					// Check box that controls PICKING A WIDGET TO INSPECT
					SNew(SButton)
					.IsEnabled_Lambda([this]() { return !bIsPendingDelayedSnapshot; })
					.OnClicked(this, &SWidgetReflector::HandlePickButtonClicked, EWidgetPickingMode::Drawable)
					.ButtonColorAndOpacity(this, &SWidgetReflector::HandlePickButtonColorAndOpacity, EWidgetPickingMode::Drawable)
					[
						SNew(STextBlock)
						.Text(this, &SWidgetReflector::HandlePickButtonText, EWidgetPickingMode::Drawable)
					]
				]

				//+ SHorizontalBox::Slot()
				//.AutoWidth()
				//.Padding(FMargin(5.0f, 0.0f))
				//[
				//	SAssignNew(SearchBox, SSearchBox)
				//	.MinDesiredWidth(210.0f)
				//	.OnTextChanged(this, &SWidgetReflector::OnFilterTextChanged)
				//	.OnTextCommitted(this, &SWidgetReflector::OnFilterTextCommitted)
				//]

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
						.IsEnabled(this, &SWidgetReflector::IsTakeSnapshotButtonEnabled)
						.OnClicked(this, &SWidgetReflector::HandleTakeSnapshotButtonClicked)
						[
							SNew(STextBlock)
							.Text_Lambda([this]() { return bIsPendingDelayedSnapshot ? LOCTEXT("CancelSnapshotButtonText", "Cancel Snapshot") : LOCTEXT("TakeSnapshotButtonText", "Take Snapshot"); })
						]
					]

					+SHorizontalBox::Slot()
					.Padding(FMargin(4.0f, 0.0f))
					.AutoWidth()
					[
						SNew(SNumericDropDown<float>)
						.LabelText(LOCTEXT("DelayLabel", "Delay:"))
						.bShowNamedValue(true)
						.DropDownValues(NamedValuesForSnapshotDelay)
						.IsEnabled_Lambda([this]() { return !bIsPendingDelayedSnapshot; })
						.Value_Lambda([this]() { return SnapshotDelay; })
						.OnValueChanged_Lambda([this](const float InValue) { SnapshotDelay = FMath::Max(0.0f, InValue); })
					]

					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						// Button that controls the target for the snapshot operation
						SAssignNew(AvailableSnapshotTargetsComboBox, SComboBox<TSharedPtr<FWidgetSnapshotTarget>>)
						.IsEnabled(this, &SWidgetReflector::IsSnapshotTargetComboEnabled)
						.ToolTipText(LOCTEXT("ChooseSnapshotTargetToolTipText", "Choose Snapshot Target"))
						.OptionsSource(&AvailableSnapshotTargets)
						.OnGenerateWidget(this, &SWidgetReflector::HandleGenerateAvailableSnapshotComboItemWidget)
						.OnSelectionChanged(this, &SWidgetReflector::HandleAvailableSnapshotComboSelectionChanged)
						[
							SNew(STextBlock)
							.Text(this, &SWidgetReflector::GetSelectedSnapshotTargetDisplayName)
						]
					]
				]

#if SLATE_REFLECTOR_HAS_DESKTOP_PLATFORM
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(5.0f, 0.0f))
				[
					// Button that controls loading a saved snapshot
					SNew(SButton)
					.IsEnabled_Lambda([this]() { return !bIsPendingDelayedSnapshot; })
					.OnClicked(this, &SWidgetReflector::HandleLoadSnapshotButtonClicked)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("LoadSnapshotButtonText", "Load Snapshot"))
					]
				]
#endif // SLATE_REFLECTOR_HAS_DESKTOP_PLATFORM
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
					.TreeItemsSource(&ReflectorTreeRoot)
					.OnGenerateRow(this, &SWidgetReflector::HandleReflectorTreeGenerateRow)
					.OnGetChildren(this, &SWidgetReflector::HandleReflectorTreeGetChildren)
					.OnSelectionChanged(this, &SWidgetReflector::HandleReflectorTreeSelectionChanged)
					.HighlightParentNodesForSelection(true)
					.HeaderRow
					(
						SNew(SHeaderRow)

						+SHeaderRow::Column(SReflectorTreeWidgetItem::NAME_WidgetName)
						.DefaultLabel(LOCTEXT("WidgetName", "Widget Name"))
						.FillWidth(0.80f)

						+SHeaderRow::Column(SReflectorTreeWidgetItem::NAME_ForegroundColor)
						.FixedWidth(24.0f)
						.VAlignHeader(VAlign_Center)
						.HeaderContent()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ForegroundColor", "FG"))
							.ToolTipText(LOCTEXT("ForegroundColorToolTip", "Foreground Color"))
						]

						+SHeaderRow::Column(SReflectorTreeWidgetItem::NAME_Visibility)
						.FixedWidth(125.0f)
						.HAlignHeader(HAlign_Center)
						.VAlignHeader(VAlign_Center)
						.HeaderContent()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("Visibility", "Visibility" ))
							.ToolTipText(LOCTEXT("VisibilityTooltip", "Visibility"))
						]

						+ SHeaderRow::Column(SReflectorTreeWidgetItem::NAME_Focusable)
						.DefaultLabel(LOCTEXT("Focus", "Focus?"))
						.FixedWidth(50.0f)
						.HAlignHeader(HAlign_Center)
						.VAlignHeader(VAlign_Center)
						.HeaderContent()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("Focus", "Focus?"))
							.ToolTipText(LOCTEXT("FocusableTooltip", "Focusability (Note that for hit-test directional navigation to work it must be Focusable and \"Visible\"!)"))
						]

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
	auto OnTabClosed = [this](TSharedRef<SDockTab>)
	{
		// Tab closed - leave snapshot mode
		SetUIMode(EWidgetReflectorUIMode::Live);
	};

	auto OnWidgetPathPicked = [this](const TArray<TSharedRef<FWidgetReflectorNodeBase>>& PickedWidgetPath)
	{
		VisualizeAsTree(PickedWidgetPath);
	};

	return SNew(SDockTab)
		.Label(LOCTEXT("SnapshotWidgetPickerTab", "Snapshot Widget Picker"))
		.OnTabClosed_Lambda(OnTabClosed)
		[
			SAssignNew(WidgetSnapshotVisualizer, SWidgetSnapshotVisualizer)
			.SnapshotData(&SnapshotData)
			.OnWidgetPathPicked_Lambda(OnWidgetPathPicked)
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

#endif

#if WITH_SLATE_DEBUGGING

TSharedRef<SDockTab> SWidgetReflector::SpawnWidgetEvents(const FSpawnTabArgs& Args)
{
	auto OnTabClosed = [this](TSharedRef<SDockTab>)
	{
	};

	return SNew(SDockTab)
		.Label(LOCTEXT("WidgetEventsTab", "Widget Events"))
		[
			SNew(SWidgetEventLog)
		];
}

#endif

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

void SWidgetReflector::OnFilterTextChanged(const FText& InFilterText)
{
	// Update the compiled filter and report any syntax error information back to the user
	TextFilterPtr->SetFilterText(InFilterText);
	SearchBox->SetError(TextFilterPtr->GetFilterErrorText());

	// Repopulate the list to show only what has not been filtered out.
	//Refresh();
}

void SWidgetReflector::OnFilterTextCommitted(const FText& InText, ETextCommit::Type CommitInfo)
{
	if (CommitInfo == ETextCommit::OnEnter)
	{
		//ReflectorTree->SetFilterText(InText);
	}
}


void SWidgetReflector::SetUIMode(const EWidgetReflectorUIMode InNewMode)
{
	if (CurrentUIMode != InNewMode)
	{
		CurrentUIMode = InNewMode;

		SelectedNodes.Reset();
		ReflectorTreeRoot.Reset();
		PickedPath.Reset();
		ReflectorTree->RequestTreeRefresh();

		if (CurrentUIMode == EWidgetReflectorUIMode::Snapshot)
		{
			TabManager->InvokeTab(WidgetReflectorTabID::SnapshotWidgetPicker);
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

void SWidgetReflector::OnEventProcessed( const FInputEvent& Event, const FReplyBase& InReply )
{
	if ( Event.IsPointerEvent() )
	{
		const FPointerEvent& PtrEvent = static_cast<const FPointerEvent&>(Event);
		if (PtrEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			LastMouseClickTime = FSlateApplication::Get().GetCurrentTime();
			CursorPingPosition = PtrEvent.GetScreenSpacePosition();
		}
	}

	#if WITH_EVENT_LOGGING
		if (LoggedEvents.Num() >= MaxLoggedEvents)
		{
			LoggedEvents.Empty();
		}

		LoggedEvents.Add(MakeShareable(new FLoggedEvent(Event, InReply)));
		EventListView->RequestListRefresh();
		EventListView->RequestScrollIntoView(LoggedEvents.Last());
	#endif //WITH_EVENT_LOGGING
}


/* IWidgetReflector overrides
 *****************************************************************************/

bool SWidgetReflector::ReflectorNeedsToDrawIn( TSharedRef<SWindow> ThisWindow ) const
{
	return ((SelectedNodes.Num() > 0) && (ReflectorTreeRoot.Num() > 0) && (ReflectorTreeRoot[0]->GetLiveWidget() == ThisWindow));
}


void SWidgetReflector::SetWidgetsToVisualize( const FWidgetPath& InWidgetsToVisualize )
{
	ReflectorTreeRoot.Empty();

	if (InWidgetsToVisualize.IsValid())
	{
		FWidgetPath WidgetsToVisualize = InWidgetsToVisualize;

		//int32 Index = WidgetsToVisualize.Widgets.GetInternalArray().IndexOfByPredicate([](const FArrangedWidget& Entry) {
		//	return Entry.Widget->GetType() == TEXT("SGameLayerManager");
		//});

		//if (Index != INDEX_NONE)
		//{
		//	WidgetsToVisualize.Widgets.Remove(0, Index + 1);
		//}

		ReflectorTreeRoot.Add(FWidgetReflectorNodeUtils::NewLiveNodeTreeFrom(WidgetsToVisualize.Widgets[0]));
		PickedPath.Empty();

		FWidgetReflectorNodeUtils::FindLiveWidgetPath(ReflectorTreeRoot, WidgetsToVisualize, PickedPath);
		VisualizeAsTree(PickedPath);
	}

	ReflectorTree->RequestTreeRefresh();
}


int32 SWidgetReflector::Visualize( const FWidgetPath& InWidgetsToVisualize, FSlateWindowElementList& OutDrawElements, int32 LayerId )
{
	FWidgetPath WidgetsToVisualize = InWidgetsToVisualize;

	if (!WidgetsToVisualize.IsValid() && SelectedNodes.Num() > 0 && ReflectorTreeRoot.Num() > 0)
	{
		TSharedPtr<SWidget> WindowWidget = ReflectorTreeRoot[0]->GetLiveWidget();
		if (WindowWidget.IsValid())
		{
			TSharedPtr<SWindow> Window = StaticCastSharedPtr<SWindow>(WindowWidget);
			return VisualizeSelectedNodesAsRectangles(SelectedNodes, Window.ToSharedRef(), OutDrawElements, LayerId);
		}
	}

	const bool bAttemptingToVisualizeReflector = WidgetsToVisualize.ContainsWidget(ReflectorTree.ToSharedRef());

	TSharedPtr<FVisualTreeSnapshot> Tree = VisualCapture.GetVisualTreeForWindow(OutDrawElements.GetPaintWindow());
	if (Tree.IsValid())
	{
		FVector2D AbsPoint = FSlateApplication::Get().GetCursorPos();
		FVector2D WindowPoint = AbsPoint - OutDrawElements.GetPaintWindow()->GetPositionInScreen();
		TSharedPtr<const SWidget> PickedWidget = Tree->Pick(WindowPoint);

		if (PickedWidget.IsValid())
		{
			FSlateApplication::Get().FindPathToWidget(PickedWidget.ToSharedRef(), WidgetsToVisualize, EVisibility::All);
		}
	}

	if (!bAttemptingToVisualizeReflector)
	{
		SetWidgetsToVisualize(WidgetsToVisualize);
		return VisualizePickAsRectangles(WidgetsToVisualize, OutDrawElements, LayerId);
	}

	return LayerId;
}

int32 SWidgetReflector::VisualizeCursorAndKeys(FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	if (bEnableDemoMode)
	{
		static const float ClickFadeTime = 0.5f;
		static const float PingScaleAmount = 3.0f;
		static const FName CursorPingBrush("DemoRecording.CursorPing");
		const SWindow* WindowBeingDrawn = OutDrawElements.GetPaintWindow();

		// Normalized animation value for the cursor ping between 0 and 1.
		const float AnimAmount = (FSlateApplication::Get().GetCurrentTime() - LastMouseClickTime) / ClickFadeTime;

		if (WindowBeingDrawn && AnimAmount <= 1.0f)
		{
			const FVector2D CursorPosDesktopSpace = CursorPingPosition;
			const FVector2D CursorSize = FSlateApplication::Get().GetCursorSize();
			const FVector2D PingSize = CursorSize*PingScaleAmount*FCurveHandle::ApplyEasing(AnimAmount, ECurveEaseFunction::QuadOut);
			const FLinearColor PingColor = FLinearColor(1.0f, 0.0f, 1.0f, 1.0f - FCurveHandle::ApplyEasing(AnimAmount, ECurveEaseFunction::QuadIn));

			FGeometry CursorHighlightGeometry = FGeometry::MakeRoot(PingSize, FSlateLayoutTransform(CursorPosDesktopSpace - PingSize / 2));
			CursorHighlightGeometry.AppendTransform(Inverse(WindowBeingDrawn->GetLocalToScreenTransform()));
			CursorHighlightGeometry.AppendTransform(FSlateLayoutTransform(WindowBeingDrawn->GetDPIScaleFactor(), FVector2D::ZeroVector));

			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId++,
				CursorHighlightGeometry.ToPaintGeometry(),
				FCoreStyle::Get().GetBrush(CursorPingBrush),
				ESlateDrawEffect::None,
				PingColor
				);
		}
	}
	
	return LayerId;
}


/* SWidgetReflector implementation
 *****************************************************************************/

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
	const FLinearColor TopmostWidgetColor(1.0f, 0.0f, 0.0f);
	const FLinearColor LeafmostWidgetColor(0.0f, 1.0f, 0.0f);

	for (int32 WidgetIndex = 0; WidgetIndex<WidgetPathToVisualize.Num(); ++WidgetIndex)
	{
		const auto& CurWidget = WidgetPathToVisualize[WidgetIndex];

		// Tint the item based on depth in picked path
		const float ColorFactor = static_cast<float>(WidgetIndex)/WidgetPathToVisualize.Num();
		CurWidget->SetTint(FMath::Lerp(TopmostWidgetColor, LeafmostWidgetColor, ColorFactor));

		// Make sure the user can see the picked path in the tree.
		ReflectorTree->SetItemExpansion(CurWidget, true);
	}

	ReflectorTree->RequestScrollIntoView(WidgetPathToVisualize.Last());
	ReflectorTree->SetSelection(WidgetPathToVisualize.Last());
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

FReply SWidgetReflector::HandleDisplayTextureAtlases()
{
	static const FName SlateReflectorModuleName("SlateReflector");
	FModuleManager::LoadModuleChecked<ISlateReflectorModule>(SlateReflectorModuleName).DisplayTextureAtlasVisualizer();
	return FReply::Handled();
}

FReply SWidgetReflector::HandleDisplayFontAtlases()
{
	static const FName SlateReflectorModuleName("SlateReflector");
	FModuleManager::LoadModuleChecked<ISlateReflectorModule>(SlateReflectorModuleName).DisplayFontAtlasVisualizer();
	return FReply::Handled();
}

void SWidgetReflector::HandleFocusCheckBoxCheckedStateChanged( ECheckBoxState NewValue )
{
	bool bShowFocus = NewValue != ECheckBoxState::Unchecked;
	SetPickingMode(bShowFocus ? EWidgetPickingMode::Focus : EWidgetPickingMode::None);
}

FText SWidgetReflector::HandlePickButtonText(EWidgetPickingMode InPickingMode) const
{
	static const FText HitTestPicking = LOCTEXT("PickHitTestable", "Pick Hit-Testable Widgets");
	static const FText VisualPicking = LOCTEXT("PickVisual", "Pick Painted Widgets");
	static const FText Picking = LOCTEXT("PickingWidget", "Picking (Esc to Stop)");

	if (PickingMode == InPickingMode)
	{
		return Picking;
	}

	switch (InPickingMode)
	{
	default:
	case EWidgetPickingMode::HitTesting:
		return HitTestPicking;
	case EWidgetPickingMode::Drawable:
		return VisualPicking;
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

void SWidgetReflector::TakeSnapshot()
{
	// Local snapshot?
	if (SelectedSnapshotTargetInstanceId == FApp::GetInstanceId())
	{
		SetUIMode(EWidgetReflectorUIMode::Snapshot);

		// Take a snapshot of any window(s) that are currently open
		SnapshotData.TakeSnapshot();

		// Rebuild the reflector tree from the snapshot data
		ReflectorTreeRoot = SnapshotData.GetWindowsRef();
		ReflectorTree->RequestTreeRefresh();

		WidgetSnapshotVisualizer->SnapshotDataUpdated();
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
	ReflectorTreeRoot = SnapshotData.GetWindowsRef();
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
		TabManager->InvokeTab(WidgetReflectorTabID::WidgetDetails);
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

TSharedRef<ITableRow> SWidgetReflector::GenerateEventLogRow( TSharedRef<FLoggedEvent> InLoggedEvent, const TSharedRef<STableViewBase>& OwnerTable )
{
	return SNew(STableRow<TSharedRef<FLoggedEvent>>, OwnerTable)
	[
		SNew(STextBlock)
		.Text(InLoggedEvent->ToText())
	];
}


} // namespace WidgetReflectorImpl

TSharedRef<SWidgetReflector> SWidgetReflector::New()
{
  return MakeShareable( new WidgetReflectorImpl::SWidgetReflector() );
}

#undef LOCTEXT_NAMESPACE


