// Copyright Epic Games, Inc. All Rights Reserved.

#include "SReferenceViewer.h"
#include "Widgets/SOverlay.h"
#include "Engine/GameViewportClient.h"
#include "Dialogs/Dialogs.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "EditorStyleSet.h"
#include "Engine/Selection.h"
#include "ReferenceViewer/EdGraph_ReferenceViewer.h"
#include "ReferenceViewer/EdGraphNode_Reference.h"
#include "ReferenceViewer/ReferenceViewerSchema.h"
#include "AssetRegistryModule.h"
#include "ICollectionManager.h"
#include "CollectionManagerModule.h"
#include "Editor.h"
#include "AssetManagerEditorCommands.h"
#include "EditorWidgetsModule.h"
#include "Toolkits/GlobalEditorCommonCommands.h"
#include "Engine/AssetManager.h"
#include "Widgets/Input/SComboBox.h"
#include "HAL/PlatformApplicationMisc.h"
#include "AssetManagerEditorModule.h"
#include "Framework/Application/SlateApplication.h"

#include "ObjectTools.h"

#define LOCTEXT_NAMESPACE "ReferenceViewer"

SReferenceViewer::~SReferenceViewer()
{
	if (!GExitPurge)
	{
		if ( ensure(GraphObj) )
		{
			GraphObj->RemoveFromRoot();
		}		
	}
}

void SReferenceViewer::Construct(const FArguments& InArgs)
{
	// Create an action list and register commands
	RegisterActions();

	// Set up the history manager
	HistoryManager.SetOnApplyHistoryData(FOnApplyHistoryData::CreateSP(this, &SReferenceViewer::OnApplyHistoryData));
	HistoryManager.SetOnUpdateHistoryData(FOnUpdateHistoryData::CreateSP(this, &SReferenceViewer::OnUpdateHistoryData));

	// Create the graph
	GraphObj = NewObject<UEdGraph_ReferenceViewer>();
	GraphObj->Schema = UReferenceViewerSchema::StaticClass();
	GraphObj->AddToRoot();
	GraphObj->SetReferenceViewer(StaticCastSharedRef<SReferenceViewer>(AsShared()));

	SGraphEditor::FGraphEditorEvents GraphEvents;
	GraphEvents.OnNodeDoubleClicked = FSingleNodeEvent::CreateSP(this, &SReferenceViewer::OnNodeDoubleClicked);
	GraphEvents.OnCreateActionMenu = SGraphEditor::FOnCreateActionMenu::CreateSP(this, &SReferenceViewer::OnCreateGraphActionMenu);

	// Create the graph editor
	GraphEditorPtr = SNew(SGraphEditor)
		.AdditionalCommands(ReferenceViewerActions)
		.GraphToEdit(GraphObj)
		.GraphEvents(GraphEvents)
		.OnNavigateHistoryBack(FSimpleDelegate::CreateSP(this, &SReferenceViewer::GraphNavigateHistoryBack))
		.OnNavigateHistoryForward(FSimpleDelegate::CreateSP(this, &SReferenceViewer::GraphNavigateHistoryForward));

	FEditorWidgetsModule& EditorWidgetsModule = FModuleManager::LoadModuleChecked<FEditorWidgetsModule>("EditorWidgets");
	TSharedRef<SWidget> AssetDiscoveryIndicator = EditorWidgetsModule.CreateAssetDiscoveryIndicator(EAssetDiscoveryIndicatorScaleMode::Scale_None, FMargin(16, 8), false);

	static const FName DefaultForegroundName("DefaultForeground");

	// Visual options visibility
	FixAndHideSearchDepthLimit = 0;
	FixAndHideSearchBreadthLimit = 0;
	bShowCollectionFilter = true;
	bShowShowReferencesOptions = true;
	bShowShowSearchableNames = true;
	bShowShowNativePackages = true;
	bDirtyResults = false;

	ChildSlot
	[
		SNew(SVerticalBox)

		// Path and history
		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding( 0, 0, 0, 4 )
		[
			SNew(SHorizontalBox)

			// History Back Button
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(1,0)
			[
				SNew(SButton)
				.ButtonStyle( FEditorStyle::Get(), "HoverHintOnly" )
				.ForegroundColor( FEditorStyle::GetSlateColor(DefaultForegroundName) )
				.ToolTipText( this, &SReferenceViewer::GetHistoryBackTooltip )
				.ContentPadding( 0 )
				.OnClicked(this, &SReferenceViewer::BackClicked)
				.IsEnabled(this, &SReferenceViewer::IsBackEnabled)
				[
					SNew(SImage) .Image(FEditorStyle::GetBrush("ContentBrowser.HistoryBack"))
				]
			]

			// History Forward Button
			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(1,0,3,0)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle( FEditorStyle::Get(), "HoverHintOnly" )
				.ForegroundColor( FEditorStyle::GetSlateColor(DefaultForegroundName) )
				.ToolTipText( this, &SReferenceViewer::GetHistoryForwardTooltip )
				.ContentPadding( 0 )
				.OnClicked(this, &SReferenceViewer::ForwardClicked)
				.IsEnabled(this, &SReferenceViewer::IsForwardEnabled)
				[
					SNew(SImage) .Image(FEditorStyle::GetBrush("ContentBrowser.HistoryForward"))
				]
			]

			// Refresh Button
			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(1,0,3,0)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle( FEditorStyle::Get(), "HoverHintOnly" )
				.ForegroundColor( FEditorStyle::GetSlateColor(DefaultForegroundName) )
				.ToolTipText(LOCTEXT("RefreshTooltip", "Refresh current view"))
				.ContentPadding( 0 )
				.OnClicked(this, &SReferenceViewer::RefreshClicked)
				[
					SNew(SImage) .Image(FEditorStyle::GetBrush("Icons.Refresh"))
				]
			]

			// Path
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Fill)
			.FillWidth(1.f)
			[
				SNew(SBorder)
				.BorderImage( FEditorStyle::GetBrush("ToolPanel.GroupBorder") )
				[
					SNew(SEditableTextBox)
					.Text(this, &SReferenceViewer::GetAddressBarText)
					.OnTextCommitted(this, &SReferenceViewer::OnAddressBarTextCommitted)
					.OnTextChanged(this, &SReferenceViewer::OnAddressBarTextChanged)
					.SelectAllTextWhenFocused(true)
					.SelectAllTextOnCommit(true)
					.Style(FEditorStyle::Get(), "ReferenceViewer.PathText")
				]
			]
		]

		// Graph
		+SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SNew(SOverlay)

			+SOverlay::Slot()
			[
				GraphEditorPtr.ToSharedRef()
			]

			+SOverlay::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			.Padding(8)
			[
				SNew(SBorder)
				.BorderImage( FEditorStyle::GetBrush("ToolPanel.GroupBorder") )
				[
					SNew(SVerticalBox)
					+SVerticalBox::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Center)
					.Padding(2.f)
					.AutoHeight()
					[
						SAssignNew(SearchBox, SSearchBox)
						.HintText(LOCTEXT("Search", "Search..."))
						.ToolTipText(LOCTEXT("SearchTooltip", "Type here to search (pressing Enter zooms to the results)"))
						.OnTextChanged(this, &SReferenceViewer::HandleOnSearchTextChanged)
						.OnTextCommitted(this, &SReferenceViewer::HandleOnSearchTextCommitted)
					]

					+SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						.Visibility_Lambda([this]() { return (FixAndHideSearchDepthLimit > 0 ? EVisibility::Collapsed : EVisibility::Visible); })

						+SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("SearchDepthLabelText", "Search Depth Limit"))
						]

						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(SCheckBox)
							.OnCheckStateChanged( this, &SReferenceViewer::OnSearchDepthEnabledChanged )
							.IsChecked( this, &SReferenceViewer::IsSearchDepthEnabledChecked )
						]
					
						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(SBox)
							.WidthOverride(100)
							[
								SNew(SSpinBox<int32>)
								.Value(this, &SReferenceViewer::GetSearchDepthCount)
								.OnValueChanged(this, &SReferenceViewer::OnSearchDepthCommitted)
								.MinValue(1)
								.MaxValue(50)
								.MaxSliderValue(12)
							]
						]
					]

					+SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						.Visibility_Lambda([this]() { return (FixAndHideSearchBreadthLimit > 0 ? EVisibility::Collapsed : EVisibility::Visible); })

						+SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("SearchBreadthLabelText", "Search Breadth Limit"))
						]

						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(SCheckBox)
							.OnCheckStateChanged( this, &SReferenceViewer::OnSearchBreadthEnabledChanged )
							.IsChecked( this, &SReferenceViewer::IsSearchBreadthEnabledChecked )
						]
					
						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(SBox)
							.WidthOverride(100)
							[
								SNew(SSpinBox<int32>)
								.Value(this, &SReferenceViewer::GetSearchBreadthCount)
								.OnValueChanged(this, &SReferenceViewer::OnSearchBreadthCommitted)
								.MinValue(1)
								.MaxValue(1000)
								.MaxSliderValue(50)
							]
						]
					]

					+SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						.Visibility_Lambda([this]() { return (bShowCollectionFilter ? EVisibility::Visible : EVisibility::Collapsed); })

						+SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("CollectionFilter", "Collection Filter"))
						]

						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(SCheckBox)
							.OnCheckStateChanged( this, &SReferenceViewer::OnEnableCollectionFilterChanged )
							.IsChecked( this, &SReferenceViewer::IsEnableCollectionFilterChecked )
						]

						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(SBox)
							.WidthOverride(100)
							[
								SAssignNew(CollectionsCombo, SComboBox<TSharedPtr<FName>>)
								.OptionsSource(&CollectionsComboList)
								.OnComboBoxOpening(this, &SReferenceViewer::UpdateCollectionsComboList)
								.OnGenerateWidget(this, &SReferenceViewer::GenerateCollectionFilterItem)
								.OnSelectionChanged(this, &SReferenceViewer::HandleCollectionFilterChanged)
								.ToolTipText(this, &SReferenceViewer::GetCollectionFilterText)
								[
									SNew(STextBlock)
									.Text(this, &SReferenceViewer::GetCollectionFilterText)
								]
							]
						]
					]

					+SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						.Visibility_Lambda([this]() { return (bShowShowReferencesOptions ? EVisibility::Visible : EVisibility::Collapsed); })

						+SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ShowHideSoftReferences", "Show Soft References"))
						]

						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(SCheckBox)
							.OnCheckStateChanged( this, &SReferenceViewer::OnShowSoftReferencesChanged )
							.IsChecked( this, &SReferenceViewer::IsShowSoftReferencesChecked )
						]
					]

				
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						.Visibility_Lambda([this]() { return (bShowShowReferencesOptions ? EVisibility::Visible : EVisibility::Collapsed); })

						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ShowHideHardReferences", "Show Hard References"))
						]

						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(SCheckBox)
							.OnCheckStateChanged(this, &SReferenceViewer::OnShowHardReferencesChanged)
							.IsChecked(this, &SReferenceViewer::IsShowHardReferencesChecked)
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						.Visibility_Lambda([this]() { return (bShowShowReferencesOptions ? EVisibility::Visible : EVisibility::Collapsed); })

						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ShowHideEditorOnlyReferences", "Show EditorOnly References"))
						]

						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(SCheckBox)
							.OnCheckStateChanged(this, &SReferenceViewer::OnShowEditorOnlyReferencesChanged)
							.IsChecked(this, &SReferenceViewer::IsShowEditorOnlyReferencesChecked)
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						.Visibility(this, &SReferenceViewer::GetManagementReferencesVisibility)

						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ShowHideManagementReferences", "Show Management References"))
						]

						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(SCheckBox)
							.OnCheckStateChanged(this, &SReferenceViewer::OnShowManagementReferencesChanged)
							.IsChecked(this, &SReferenceViewer::IsShowManagementReferencesChecked)
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						.Visibility_Lambda([this]() { return (bShowShowSearchableNames ? EVisibility::Visible : EVisibility::Collapsed); })

						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ShowHideSearchableNames", "Show Searchable Names"))
						]

						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(SCheckBox)
							.OnCheckStateChanged(this, &SReferenceViewer::OnShowSearchableNamesChanged)
							.IsChecked(this, &SReferenceViewer::IsShowSearchableNamesChecked)
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						.Visibility_Lambda([this]() { return (bShowShowNativePackages ? EVisibility::Visible : EVisibility::Collapsed); })

						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ShowHideNativePackages", "Show Native Packages"))
						]

						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.f)
						[
							SNew(SCheckBox)
							.OnCheckStateChanged(this, &SReferenceViewer::OnShowNativePackagesChanged)
							.IsChecked(this, &SReferenceViewer::IsShowNativePackagesChecked)
						]
					]
				]
			]

			+SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(FMargin(24, 0, 24, 0))
			[
				AssetDiscoveryIndicator
			]

			+SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Bottom)
			.Padding(FMargin(0, 0, 0, 16))
			[
				SNew(STextBlock)
				.Text(this, &SReferenceViewer::GetStatusText)
			]
		]
	];

	UpdateCollectionsComboList();
}

void SReferenceViewer::SetGraphRootIdentifiers(const TArray<FAssetIdentifier>& NewGraphRootIdentifiers, const FReferenceViewerParams& ReferenceViewerParams)
{
	GraphObj->SetGraphRoot(NewGraphRootIdentifiers);
	// Set properties
	GraphObj->SetShowReferencers(ReferenceViewerParams.bShowReferencers);
	GraphObj->SetShowDependencies(ReferenceViewerParams.bShowDependencies);
	// Set user-interactive properties
	FixAndHideSearchDepthLimit = ReferenceViewerParams.FixAndHideSearchDepthLimit;
	if (FixAndHideSearchDepthLimit > 0)
	{
		GraphObj->SetSearchDepthLimit(FixAndHideSearchDepthLimit);
		GraphObj->SetSearchDepthLimitEnabled(true);
	}
	FixAndHideSearchBreadthLimit = ReferenceViewerParams.FixAndHideSearchBreadthLimit;
	if (FixAndHideSearchBreadthLimit > 0)
	{
		GraphObj->SetSearchBreadthLimit(FixAndHideSearchBreadthLimit);
		GraphObj->SetSearchBreadthLimitEnabled(true);
	}
	bShowCollectionFilter = ReferenceViewerParams.bShowCollectionFilter;
	bShowShowReferencesOptions = ReferenceViewerParams.bShowShowReferencesOptions;
	bShowShowSearchableNames = ReferenceViewerParams.bShowShowSearchableNames;
	bShowShowNativePackages = ReferenceViewerParams.bShowShowNativePackages;

	RebuildGraph();

	// Zoom once this frame to make sure widgets are visible, then zoom again so size is correct
	TriggerZoomToFit(0, 0);
	RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(this, &SReferenceViewer::TriggerZoomToFit));

	// Set the initial history data
	HistoryManager.AddHistoryData();
}

EActiveTimerReturnType SReferenceViewer::TriggerZoomToFit(double InCurrentTime, float InDeltaTime)
{
	if (GraphEditorPtr.IsValid())
	{
		GraphEditorPtr->ZoomToFit(false);
	}
	return EActiveTimerReturnType::Stop;
}

void SReferenceViewer::SetCurrentRegistrySource(const FAssetManagerEditorRegistrySource* RegistrySource)
{
	RebuildGraph();
}

void SReferenceViewer::OnNodeDoubleClicked(UEdGraphNode* Node)
{
	TSet<UObject*> Nodes;
	Nodes.Add(Node);
	ReCenterGraphOnNodes( Nodes );
}

void SReferenceViewer::RebuildGraph()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	if (AssetRegistryModule.Get().IsLoadingAssets())
	{
		// We are still discovering assets, listen for the completion delegate before building the graph
		if (!AssetRegistryModule.Get().OnFilesLoaded().IsBoundToObject(this))
		{
			AssetRegistryModule.Get().OnFilesLoaded().AddSP(this, &SReferenceViewer::OnInitialAssetRegistrySearchComplete);
		}
	}
	else
	{
		// All assets are already discovered, build the graph now, if we have one
		if (GraphObj)
		{
			GraphObj->RebuildGraph();
		}

		bDirtyResults = false;
		if (!AssetRefreshHandle.IsValid())
		{
			// Listen for updates
			AssetRefreshHandle = AssetRegistryModule.Get().OnAssetUpdated().AddSP(this, &SReferenceViewer::OnAssetRegistryChanged);
			AssetRegistryModule.Get().OnAssetAdded().AddSP(this, &SReferenceViewer::OnAssetRegistryChanged);
			AssetRegistryModule.Get().OnAssetRemoved().AddSP(this, &SReferenceViewer::OnAssetRegistryChanged);
		}
	}
}

FActionMenuContent SReferenceViewer::OnCreateGraphActionMenu(UEdGraph* InGraph, const FVector2D& InNodePosition, const TArray<UEdGraphPin*>& InDraggedPins, bool bAutoExpand, SGraphEditor::FActionMenuClosed InOnMenuClosed)
{
	// no context menu when not over a node
	return FActionMenuContent();
}

bool SReferenceViewer::IsBackEnabled() const
{
	return HistoryManager.CanGoBack();
}

bool SReferenceViewer::IsForwardEnabled() const
{
	return HistoryManager.CanGoForward();
}

FReply SReferenceViewer::BackClicked()
{
	HistoryManager.GoBack();

	return FReply::Handled();
}

FReply SReferenceViewer::ForwardClicked()
{
	HistoryManager.GoForward();

	return FReply::Handled();
}

FReply SReferenceViewer::RefreshClicked()
{
	RebuildGraph();
	TriggerZoomToFit(0, 0);
	RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(this, &SReferenceViewer::TriggerZoomToFit));

	return FReply::Handled();
}

void SReferenceViewer::GraphNavigateHistoryBack()
{
	BackClicked();
}

void SReferenceViewer::GraphNavigateHistoryForward()
{
	ForwardClicked();
}

FText SReferenceViewer::GetHistoryBackTooltip() const
{
	if ( HistoryManager.CanGoBack() )
	{
		return FText::Format( LOCTEXT("HistoryBackTooltip", "Back to {0}"), HistoryManager.GetBackDesc() );
	}
	return FText::GetEmpty();
}

FText SReferenceViewer::GetHistoryForwardTooltip() const
{
	if ( HistoryManager.CanGoForward() )
	{
		return FText::Format( LOCTEXT("HistoryForwardTooltip", "Forward to {0}"), HistoryManager.GetForwardDesc() );
	}
	return FText::GetEmpty();
}

FText SReferenceViewer::GetAddressBarText() const
{
	if ( GraphObj )
	{
		if (TemporaryPathBeingEdited.IsEmpty())
		{
			const TArray<FAssetIdentifier>& CurrentGraphRootPackageNames = GraphObj->GetCurrentGraphRootIdentifiers();
			if (CurrentGraphRootPackageNames.Num() == 1)
			{
				return FText::FromString(CurrentGraphRootPackageNames[0].ToString());
			}
			else if (CurrentGraphRootPackageNames.Num() > 1)
			{
				return FText::Format(LOCTEXT("AddressBarMultiplePackagesText", "{0} and {1} others"), FText::FromString(CurrentGraphRootPackageNames[0].ToString()), FText::AsNumber(CurrentGraphRootPackageNames.Num()));
			}
		}
		else
		{
			return TemporaryPathBeingEdited;
		}
	}

	return FText();
}

FText SReferenceViewer::GetStatusText() const
{
	FString DirtyPackages;
	if (GraphObj)
	{
		const TArray<FAssetIdentifier>& CurrentGraphRootPackageNames = GraphObj->GetCurrentGraphRootIdentifiers();
		
		for (const FAssetIdentifier& CurrentAsset : CurrentGraphRootPackageNames)
		{
			if (CurrentAsset.IsPackage())
			{
				FString PackageString = CurrentAsset.PackageName.ToString();
				UPackage* InMemoryPackage = FindPackage(nullptr, *PackageString);
				if (InMemoryPackage && InMemoryPackage->IsDirty())
				{
					DirtyPackages += FPackageName::GetShortName(*PackageString);

					// Break on first modified asset to avoid string going too long, the multi select case is fairly rare
					break;
				}
			}
		}
	}

	if (DirtyPackages.Len() > 0)
	{
		return FText::Format(LOCTEXT("ModifiedWarning", "Showing old saved references for edited asset {0}"), FText::FromString(DirtyPackages));
	}

	if (bDirtyResults)
	{
		return LOCTEXT("DirtyWarning", "Saved references changed, refresh for update");
	}

	return FText();
}

void SReferenceViewer::OnAddressBarTextCommitted(const FText& NewText, ETextCommit::Type CommitInfo)
{
	if (CommitInfo == ETextCommit::OnEnter)
	{
		TArray<FAssetIdentifier> NewPaths;
		NewPaths.Add(FAssetIdentifier::FromString(NewText.ToString()));

		SetGraphRootIdentifiers(NewPaths);
	}

	TemporaryPathBeingEdited = FText();
}

void SReferenceViewer::OnAddressBarTextChanged(const FText& NewText)
{
	TemporaryPathBeingEdited = NewText;
}

void SReferenceViewer::OnApplyHistoryData(const FReferenceViewerHistoryData& History)
{
	if ( GraphObj )
	{
		GraphObj->SetGraphRoot(History.Identifiers);
		UEdGraphNode_Reference* NewRootNode = GraphObj->RebuildGraph();
		
		if ( NewRootNode && ensure(GraphEditorPtr.IsValid()) )
		{
			GraphEditorPtr->SetNodeSelection(NewRootNode, true);
		}
	}
}

void SReferenceViewer::OnUpdateHistoryData(FReferenceViewerHistoryData& HistoryData) const
{
	if ( GraphObj )
	{
		const TArray<FAssetIdentifier>& CurrentGraphRootIdentifiers = GraphObj->GetCurrentGraphRootIdentifiers();
		HistoryData.HistoryDesc = GetAddressBarText();
		HistoryData.Identifiers = CurrentGraphRootIdentifiers;
	}
	else
	{
		HistoryData.HistoryDesc = FText::GetEmpty();
		HistoryData.Identifiers.Empty();
	}
}

void SReferenceViewer::OnSearchDepthEnabledChanged( ECheckBoxState NewState )
{
	if ( GraphObj )
	{
		GraphObj->SetSearchDepthLimitEnabled(NewState == ECheckBoxState::Checked);
		RebuildGraph();
	}
}

ECheckBoxState SReferenceViewer::IsSearchDepthEnabledChecked() const
{
	if ( GraphObj )
	{
		return GraphObj->IsSearchDepthLimited() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	else
	{
		return ECheckBoxState::Unchecked;
	}
}

int32 SReferenceViewer::GetSearchDepthCount() const
{
	if ( GraphObj )
	{
		return GraphObj->GetSearchDepthLimit();
	}
	else
	{
		return 0;
	}
}

void SReferenceViewer::OnSearchDepthCommitted(int32 NewValue)
{
	if ( GraphObj )
	{
		GraphObj->SetSearchDepthLimit(NewValue);
		RebuildGraph();
	}
}

void SReferenceViewer::OnSearchBreadthEnabledChanged( ECheckBoxState NewState )
{
	if ( GraphObj )
	{
		GraphObj->SetSearchBreadthLimitEnabled(NewState == ECheckBoxState::Checked);
		RebuildGraph();
	}
}

ECheckBoxState SReferenceViewer::IsSearchBreadthEnabledChecked() const
{
	if ( GraphObj )
	{
		return GraphObj->IsSearchBreadthLimited() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	else
	{
		return ECheckBoxState::Unchecked;
	}
}

TSharedRef<SWidget> SReferenceViewer::GenerateCollectionFilterItem(TSharedPtr<FName> InItem)
{
	FText ItemAsText = FText::FromName(*InItem);
	return
		SNew(SBox)
		.WidthOverride(300)
		[
			SNew(STextBlock)
			.Text(ItemAsText)
			.ToolTipText(ItemAsText)
		];
}

void SReferenceViewer::OnEnableCollectionFilterChanged(ECheckBoxState NewState)
{
	if (GraphObj)
	{
		const bool bNewValue = NewState == ECheckBoxState::Checked;
		const bool bCurrentValue = GraphObj->GetEnableCollectionFilter();
		if (bCurrentValue != bNewValue)
		{
			GraphObj->SetEnableCollectionFilter(NewState == ECheckBoxState::Checked);
			RebuildGraph();
		}
	}
}

ECheckBoxState SReferenceViewer::IsEnableCollectionFilterChecked() const
{
	if (GraphObj)
	{
		return GraphObj->GetEnableCollectionFilter() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	else
	{
		return ECheckBoxState::Unchecked;
	}
}

void SReferenceViewer::UpdateCollectionsComboList()
{
	TArray<FName> CollectionNames;
	{
		FCollectionManagerModule& CollectionManagerModule = FCollectionManagerModule::GetModule();

		TArray<FCollectionNameType> AllCollections;
		CollectionManagerModule.Get().GetCollections(AllCollections);

		for (const FCollectionNameType& Collection : AllCollections)
		{
			ECollectionStorageMode::Type StorageMode = ECollectionStorageMode::Static;
			CollectionManagerModule.Get().GetCollectionStorageMode(Collection.Name, Collection.Type, StorageMode);

			if (StorageMode == ECollectionStorageMode::Static)
			{
				CollectionNames.AddUnique(Collection.Name);
			}
		}
	}
	CollectionNames.Sort([](const FName& A, const FName& B) { return A.Compare(B) < 0; });

	CollectionsComboList.Reset();
	CollectionsComboList.Add(MakeShared<FName>(NAME_None));
	for (FName CollectionName : CollectionNames)
	{
		CollectionsComboList.Add(MakeShared<FName>(CollectionName));
	}

	if (CollectionsCombo)
	{
		CollectionsCombo->ClearSelection();
		CollectionsCombo->RefreshOptions();

		if (GraphObj)
		{
			const FName CurrentFilter = GraphObj->GetCurrentCollectionFilter();

			const int32 SelectedItemIndex = CollectionsComboList.IndexOfByPredicate([CurrentFilter](const TSharedPtr<FName>& InItem)
			{
				return CurrentFilter == *InItem;
			});

			if (SelectedItemIndex != INDEX_NONE)
			{
				CollectionsCombo->SetSelectedItem(CollectionsComboList[SelectedItemIndex]);
			}
		}
	}
}

void SReferenceViewer::HandleCollectionFilterChanged(TSharedPtr<FName> Item, ESelectInfo::Type SelectInfo)
{
	if (GraphObj && Item)
	{
		const FName NewFilter = *Item;
		const FName CurrentFilter = GraphObj->GetCurrentCollectionFilter();
		if (CurrentFilter != NewFilter)
		{
			if (CurrentFilter == NAME_None)
			{
				// Automatically check the box to enable the filter if the previous filter was None
				GraphObj->SetEnableCollectionFilter(true);
			}

			GraphObj->SetCurrentCollectionFilter(NewFilter);
			RebuildGraph();
		}
	}
}

FText SReferenceViewer::GetCollectionFilterText() const
{
	return FText::FromName(GraphObj->GetCurrentCollectionFilter());
}

void SReferenceViewer::OnShowSoftReferencesChanged(ECheckBoxState NewState)
{
	if (GraphObj)
	{
		GraphObj->SetShowSoftReferencesEnabled(NewState == ECheckBoxState::Checked);
		RebuildGraph();
	}
}

ECheckBoxState SReferenceViewer::IsShowSoftReferencesChecked() const
{
	if (GraphObj)
	{
		return GraphObj->IsShowSoftReferences()? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	else
	{
		return ECheckBoxState::Unchecked;
	}
}

void SReferenceViewer::OnShowHardReferencesChanged(ECheckBoxState NewState)
{
	if (GraphObj)
	{
		GraphObj->SetShowHardReferencesEnabled(NewState == ECheckBoxState::Checked);
		RebuildGraph();
	}
}


ECheckBoxState SReferenceViewer::IsShowHardReferencesChecked() const
{
	if (GraphObj)
	{
		return GraphObj->IsShowHardReferences() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	else
	{
		return ECheckBoxState::Unchecked;
	}
}

void SReferenceViewer::OnShowEditorOnlyReferencesChanged(ECheckBoxState NewState)
{
	if (GraphObj)
	{
		GraphObj->SetShowEditorOnlyReferencesEnabled(NewState == ECheckBoxState::Checked);
		RebuildGraph();
	}
}


ECheckBoxState SReferenceViewer::IsShowEditorOnlyReferencesChecked() const
{
	if (GraphObj)
	{
		return GraphObj->IsShowEditorOnlyReferences() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	else
	{
		return ECheckBoxState::Unchecked;
	}
}


EVisibility SReferenceViewer::GetManagementReferencesVisibility() const
{
	if (bShowShowReferencesOptions && UAssetManager::IsValid())
	{
		return EVisibility::SelfHitTestInvisible;
	}
	return EVisibility::Collapsed;
}

void SReferenceViewer::OnShowManagementReferencesChanged(ECheckBoxState NewState)
{
	if (GraphObj)
	{
		// This can take a few seconds if it isn't ready
		UAssetManager::Get().UpdateManagementDatabase();

		GraphObj->SetShowManagementReferencesEnabled(NewState == ECheckBoxState::Checked);
		RebuildGraph();
	}
}

ECheckBoxState SReferenceViewer::IsShowManagementReferencesChecked() const
{
	if (GraphObj)
	{
		return GraphObj->IsShowManagementReferences() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	else
	{
		return ECheckBoxState::Unchecked;
	}
}

void SReferenceViewer::OnShowSearchableNamesChanged(ECheckBoxState NewState)
{
	if (GraphObj)
	{
		GraphObj->SetShowSearchableNames(NewState == ECheckBoxState::Checked);
		RebuildGraph();
	}
}

ECheckBoxState SReferenceViewer::IsShowSearchableNamesChecked() const
{
	if (GraphObj)
	{
		return GraphObj->IsShowSearchableNames() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	else
	{
		return ECheckBoxState::Unchecked;
	}
}

void SReferenceViewer::OnShowNativePackagesChanged(ECheckBoxState NewState)
{
	if (GraphObj)
	{
		GraphObj->SetShowNativePackages(NewState == ECheckBoxState::Checked);
		RebuildGraph();
	}
}

ECheckBoxState SReferenceViewer::IsShowNativePackagesChecked() const
{
	if (GraphObj)
	{
		return GraphObj->IsShowNativePackages() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	else
	{
		return ECheckBoxState::Unchecked;
	}
}

int32 SReferenceViewer::GetSearchBreadthCount() const
{
	if ( GraphObj )
	{
		return GraphObj->GetSearchBreadthLimit();
	}
	else
	{
		return 0;
	}
}

void SReferenceViewer::OnSearchBreadthCommitted(int32 NewValue)
{
	if ( GraphObj )
	{
		GraphObj->SetSearchBreadthLimit(NewValue);
		RebuildGraph();
	}
}

void SReferenceViewer::RegisterActions()
{
	ReferenceViewerActions = MakeShareable(new FUICommandList);
	FAssetManagerEditorCommands::Register();

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().ZoomToFit,
		FExecuteAction::CreateSP(this, &SReferenceViewer::ZoomToFit),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::CanZoomToFit));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().Find,
		FExecuteAction::CreateSP(this, &SReferenceViewer::OnFind));

	ReferenceViewerActions->MapAction(
		FGlobalEditorCommonCommands::Get().FindInContentBrowser,
		FExecuteAction::CreateSP(this, &SReferenceViewer::ShowSelectionInContentBrowser),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasAtLeastOnePackageNodeSelected));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().OpenSelectedInAssetEditor,
		FExecuteAction::CreateSP(this, &SReferenceViewer::OpenSelectedInAssetEditor),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasAtLeastOneRealNodeSelected));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().ReCenterGraph,
		FExecuteAction::CreateSP(this, &SReferenceViewer::ReCenterGraph),
		FCanExecuteAction());

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().CopyReferencedObjects,
		FExecuteAction::CreateSP(this, &SReferenceViewer::CopyReferencedObjects),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasAtLeastOnePackageNodeSelected));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().CopyReferencingObjects,
		FExecuteAction::CreateSP(this, &SReferenceViewer::CopyReferencingObjects),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasAtLeastOnePackageNodeSelected));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().ShowReferencedObjects,
		FExecuteAction::CreateSP(this, &SReferenceViewer::ShowReferencedObjects),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasAtLeastOnePackageNodeSelected));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().ShowReferencingObjects,
		FExecuteAction::CreateSP(this, &SReferenceViewer::ShowReferencingObjects),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasAtLeastOnePackageNodeSelected));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().MakeLocalCollectionWithReferencers,
		FExecuteAction::CreateSP(this, &SReferenceViewer::MakeCollectionWithReferencersOrDependencies, ECollectionShareType::CST_Local, true),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasExactlyOnePackageNodeSelected));
	
	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().MakePrivateCollectionWithReferencers,
		FExecuteAction::CreateSP(this, &SReferenceViewer::MakeCollectionWithReferencersOrDependencies, ECollectionShareType::CST_Private, true),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasExactlyOnePackageNodeSelected));
	
	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().MakeSharedCollectionWithReferencers,
		FExecuteAction::CreateSP(this, &SReferenceViewer::MakeCollectionWithReferencersOrDependencies, ECollectionShareType::CST_Shared, true),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasExactlyOnePackageNodeSelected));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().MakeLocalCollectionWithDependencies,
		FExecuteAction::CreateSP(this, &SReferenceViewer::MakeCollectionWithReferencersOrDependencies, ECollectionShareType::CST_Local, false),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasExactlyOnePackageNodeSelected));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().MakePrivateCollectionWithDependencies,
		FExecuteAction::CreateSP(this, &SReferenceViewer::MakeCollectionWithReferencersOrDependencies, ECollectionShareType::CST_Private, false),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasExactlyOnePackageNodeSelected));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().MakeSharedCollectionWithDependencies,
		FExecuteAction::CreateSP(this, &SReferenceViewer::MakeCollectionWithReferencersOrDependencies, ECollectionShareType::CST_Shared, false),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasExactlyOnePackageNodeSelected));
	
	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().ShowReferenceTree,
		FExecuteAction::CreateSP(this, &SReferenceViewer::ShowReferenceTree),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasExactlyOnePackageNodeSelected));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().ViewSizeMap,
		FExecuteAction::CreateSP(this, &SReferenceViewer::ViewSizeMap),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasAtLeastOneRealNodeSelected));

	ReferenceViewerActions->MapAction(
		FAssetManagerEditorCommands::Get().ViewAssetAudit,
		FExecuteAction::CreateSP(this, &SReferenceViewer::ViewAssetAudit),
		FCanExecuteAction::CreateSP(this, &SReferenceViewer::HasAtLeastOneRealNodeSelected));
}

void SReferenceViewer::ShowSelectionInContentBrowser()
{
	TArray<FAssetData> AssetList;

	// Build up a list of selected assets from the graph selection set
	TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
	for (FGraphPanelSelectionSet::TConstIterator It(SelectedNodes); It; ++It)
	{
		if (UEdGraphNode_Reference* ReferenceNode = Cast<UEdGraphNode_Reference>(*It))
		{
			if (ReferenceNode->GetAssetData().IsValid())
			{
				AssetList.Add(ReferenceNode->GetAssetData());
			}
		}
	}

	if (AssetList.Num() > 0)
	{
		GEditor->SyncBrowserToObjects(AssetList);
	}
}

void SReferenceViewer::OpenSelectedInAssetEditor()
{
	TArray<FAssetIdentifier> IdentifiersToEdit;
	TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
	for (FGraphPanelSelectionSet::TConstIterator It(SelectedNodes); It; ++It)
	{
		if (UEdGraphNode_Reference* ReferenceNode = Cast<UEdGraphNode_Reference>(*It))
		{
			if (!ReferenceNode->IsCollapsed())
			{
				ReferenceNode->GetAllIdentifiers(IdentifiersToEdit);
			}
		}
	}

	// This will handle packages as well as searchable names if other systems register
	FEditorDelegates::OnEditAssetIdentifiers.Broadcast(IdentifiersToEdit);
}

void SReferenceViewer::ReCenterGraph()
{
	ReCenterGraphOnNodes( GraphEditorPtr->GetSelectedNodes() );
}

FString SReferenceViewer::GetReferencedObjectsList() const
{
	FString ReferencedObjectsList;

	TSet<FName> AllSelectedPackageNames;
	GetPackageNamesFromSelectedNodes(AllSelectedPackageNames);

	if (AllSelectedPackageNames.Num() > 0)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

		for (const FName& SelectedPackageName : AllSelectedPackageNames)
		{
			TArray<FName> HardDependencies;
			AssetRegistryModule.Get().GetDependencies(SelectedPackageName, HardDependencies, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::EDependencyQuery::Hard);
			
			TArray<FName> SoftDependencies;
			AssetRegistryModule.Get().GetDependencies(SelectedPackageName, SoftDependencies, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::EDependencyQuery::Soft);

			ReferencedObjectsList += FString::Printf(TEXT("[%s - Dependencies]\n"), *SelectedPackageName.ToString());
			if (HardDependencies.Num() > 0)
			{
				ReferencedObjectsList += TEXT("  [HARD]\n");
				for (const FName& HardDependency : HardDependencies)
				{
					const FString PackageString = HardDependency.ToString();
					ReferencedObjectsList += FString::Printf(TEXT("    %s.%s\n"), *PackageString, *FPackageName::GetLongPackageAssetName(PackageString));
				}
			}
			if (SoftDependencies.Num() > 0)
			{
				ReferencedObjectsList += TEXT("  [SOFT]\n");
				for (const FName& SoftDependency : SoftDependencies)
				{
					const FString PackageString = SoftDependency.ToString();
					ReferencedObjectsList += FString::Printf(TEXT("    %s.%s\n"), *PackageString, *FPackageName::GetLongPackageAssetName(PackageString));
				}
			}
		}
	}

	return ReferencedObjectsList;
}

FString SReferenceViewer::GetReferencingObjectsList() const
{
	FString ReferencingObjectsList;

	TSet<FName> AllSelectedPackageNames;
	GetPackageNamesFromSelectedNodes(AllSelectedPackageNames);

	if (AllSelectedPackageNames.Num() > 0)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

		for (const FName& SelectedPackageName : AllSelectedPackageNames)
		{
			TArray<FName> HardDependencies;
			AssetRegistryModule.Get().GetReferencers(SelectedPackageName, HardDependencies, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::EDependencyQuery::Hard);

			TArray<FName> SoftDependencies;
			AssetRegistryModule.Get().GetReferencers(SelectedPackageName, SoftDependencies, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::EDependencyQuery::Soft);

			ReferencingObjectsList += FString::Printf(TEXT("[%s - Referencers]\n"), *SelectedPackageName.ToString());
			if (HardDependencies.Num() > 0)
			{
				ReferencingObjectsList += TEXT("  [HARD]\n");
				for (const FName& HardDependency : HardDependencies)
				{
					const FString PackageString = HardDependency.ToString();
					ReferencingObjectsList += FString::Printf(TEXT("    %s.%s\n"), *PackageString, *FPackageName::GetLongPackageAssetName(PackageString));
				}
			}
			if (SoftDependencies.Num() > 0)
			{
				ReferencingObjectsList += TEXT("  [SOFT]\n");
				for (const FName& SoftDependency : SoftDependencies)
				{
					const FString PackageString = SoftDependency.ToString();
					ReferencingObjectsList += FString::Printf(TEXT("    %s.%s\n"), *PackageString, *FPackageName::GetLongPackageAssetName(PackageString));
				}
			}
		}
	}

	return ReferencingObjectsList;
}

void SReferenceViewer::CopyReferencedObjects()
{
	const FString ReferencedObjectsList = GetReferencedObjectsList();
	FPlatformApplicationMisc::ClipboardCopy(*ReferencedObjectsList);
}

void SReferenceViewer::CopyReferencingObjects()
{
	const FString ReferencingObjectsList = GetReferencingObjectsList();
	FPlatformApplicationMisc::ClipboardCopy(*ReferencingObjectsList);
}

void SReferenceViewer::ShowReferencedObjects()
{
	const FString ReferencedObjectsList = GetReferencedObjectsList();
	SGenericDialogWidget::OpenDialog(LOCTEXT("ReferencedObjectsDlgTitle", "Referenced Objects"), SNew(STextBlock).Text(FText::FromString(ReferencedObjectsList)));
}

void SReferenceViewer::ShowReferencingObjects()
{	
	const FString ReferencingObjectsList = GetReferencingObjectsList();
	SGenericDialogWidget::OpenDialog(LOCTEXT("ReferencingObjectsDlgTitle", "Referencing Objects"), SNew(STextBlock).Text(FText::FromString(ReferencingObjectsList)));
}

void SReferenceViewer::MakeCollectionWithReferencersOrDependencies(ECollectionShareType::Type ShareType, bool bReferencers)
{
	TSet<FName> AllSelectedPackageNames;
	GetPackageNamesFromSelectedNodes(AllSelectedPackageNames);

	if (AllSelectedPackageNames.Num() > 0)
	{
		if (ensure(ShareType != ECollectionShareType::CST_All))
		{
			FCollectionManagerModule& CollectionManagerModule = FCollectionManagerModule::GetModule();

			FText CollectionNameAsText;
			FString FirstAssetName = FPackageName::GetLongPackageAssetName(AllSelectedPackageNames.Array()[0].ToString());
			if (bReferencers)
			{
				if (AllSelectedPackageNames.Num() > 1)
				{
					CollectionNameAsText = FText::Format(LOCTEXT("ReferencersForMultipleAssetNames", "{0}AndOthers_Referencers"), FText::FromString(FirstAssetName));
				}
				else
				{
					CollectionNameAsText = FText::Format(LOCTEXT("ReferencersForSingleAsset", "{0}_Referencers"), FText::FromString(FirstAssetName));
				}
			}
			else
			{
				if (AllSelectedPackageNames.Num() > 1)
				{
					CollectionNameAsText = FText::Format(LOCTEXT("DependenciesForMultipleAssetNames", "{0}AndOthers_Dependencies"), FText::FromString(FirstAssetName));
				}
				else
				{
					CollectionNameAsText = FText::Format(LOCTEXT("DependenciesForSingleAsset", "{0}_Dependencies"), FText::FromString(FirstAssetName));
				}
			}

			FName CollectionName;
			CollectionManagerModule.Get().CreateUniqueCollectionName(*CollectionNameAsText.ToString(), ShareType, CollectionName);

			FText ResultsMessage;
			
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			TArray<FName> PackageNamesToAddToCollection;
			if (bReferencers)
			{
				for (FName SelectedPackage : AllSelectedPackageNames)
				{
					AssetRegistryModule.Get().GetReferencers(SelectedPackage, PackageNamesToAddToCollection);
				}
			}
			else
			{
				for (FName SelectedPackage : AllSelectedPackageNames)
				{
					AssetRegistryModule.Get().GetDependencies(SelectedPackage, PackageNamesToAddToCollection);
				}
			}

			TSet<FName> PackageNameSet;
			for (FName PackageToAdd : PackageNamesToAddToCollection)
			{
				if (!AllSelectedPackageNames.Contains(PackageToAdd))
				{
					PackageNameSet.Add(PackageToAdd);
				}
			}

			IAssetManagerEditorModule::Get().WriteCollection(CollectionName, ShareType, PackageNameSet.Array(), true);
		}
	}
}

void SReferenceViewer::ShowReferenceTree()
{
	UObject* SelectedObject = GetObjectFromSingleSelectedNode();

	if ( SelectedObject )
	{
		bool bObjectWasSelected = false;
		for (FSelectionIterator It(*GEditor->GetSelectedObjects()) ; It; ++It)
		{
			if ( (*It) == SelectedObject )
			{
				GEditor->GetSelectedObjects()->Deselect( SelectedObject );
				bObjectWasSelected = true;
			}
		}

		ObjectTools::ShowReferenceGraph( SelectedObject );

		if ( bObjectWasSelected )
		{
			GEditor->GetSelectedObjects()->Select( SelectedObject );
		}
	}
}

void SReferenceViewer::ViewSizeMap()
{
	TArray<FAssetIdentifier> AssetIdentifiers;
	TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
	for (UObject* Node : SelectedNodes)
	{
		UEdGraphNode_Reference* ReferenceNode = Cast<UEdGraphNode_Reference>(Node);
		if (ReferenceNode)
		{
			ReferenceNode->GetAllIdentifiers(AssetIdentifiers);
		}
	}

	if (AssetIdentifiers.Num() > 0)
	{
		IAssetManagerEditorModule::Get().OpenSizeMapUI(AssetIdentifiers);
	}
}

void SReferenceViewer::ViewAssetAudit()
{
	TSet<FName> SelectedAssetPackageNames;
	GetPackageNamesFromSelectedNodes(SelectedAssetPackageNames);

	if (SelectedAssetPackageNames.Num() > 0)
	{
		IAssetManagerEditorModule::Get().OpenAssetAuditUI(SelectedAssetPackageNames.Array());
	}
}

void SReferenceViewer::ReCenterGraphOnNodes(const TSet<UObject*>& Nodes)
{
	TArray<FAssetIdentifier> NewGraphRootNames;
	FIntPoint TotalNodePos(ForceInitToZero);
	for ( auto NodeIt = Nodes.CreateConstIterator(); NodeIt; ++NodeIt )
	{
		UEdGraphNode_Reference* ReferenceNode = Cast<UEdGraphNode_Reference>(*NodeIt);
		if ( ReferenceNode )
		{
			ReferenceNode->GetAllIdentifiers(NewGraphRootNames);
			TotalNodePos.X += ReferenceNode->NodePosX;
			TotalNodePos.Y += ReferenceNode->NodePosY;
		}
	}

	if ( NewGraphRootNames.Num() > 0 )
	{
		const FIntPoint AverageNodePos = TotalNodePos / NewGraphRootNames.Num();
		GraphObj->SetGraphRoot(NewGraphRootNames, AverageNodePos);
		UEdGraphNode_Reference* NewRootNode = GraphObj->RebuildGraph();

		if ( NewRootNode && ensure(GraphEditorPtr.IsValid()) )
		{
			GraphEditorPtr->ClearSelectionSet();
			GraphEditorPtr->SetNodeSelection(NewRootNode, true);
		}

		// Set the initial history data
		HistoryManager.AddHistoryData();
	}
}

UObject* SReferenceViewer::GetObjectFromSingleSelectedNode() const
{
	UObject* ReturnObject = nullptr;

	TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
	if ( ensure(SelectedNodes.Num()) == 1 )
	{
		UEdGraphNode_Reference* ReferenceNode = Cast<UEdGraphNode_Reference>(SelectedNodes.Array()[0]);
		if ( ReferenceNode )
		{
			const FAssetData& AssetData = ReferenceNode->GetAssetData();
			if (AssetData.IsAssetLoaded())
			{
				ReturnObject = AssetData.GetAsset();
			}
			else
			{
				FScopedSlowTask SlowTask(0, LOCTEXT("LoadingSelectedObject", "Loading selection..."));
				SlowTask.MakeDialog();
				ReturnObject = AssetData.GetAsset();
			}
		}
	}

	return ReturnObject;
}

void SReferenceViewer::GetPackageNamesFromSelectedNodes(TSet<FName>& OutNames) const
{
	TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
	for (UObject* Node : SelectedNodes)
	{
		UEdGraphNode_Reference* ReferenceNode = Cast<UEdGraphNode_Reference>(Node);
		if (ReferenceNode)
		{
			TArray<FName> NodePackageNames;
			ReferenceNode->GetAllPackageNames(NodePackageNames);
			OutNames.Append(NodePackageNames);
		}
	}
}

bool SReferenceViewer::HasExactlyOneNodeSelected() const
{
	if ( GraphEditorPtr.IsValid() )
	{
		return GraphEditorPtr->GetSelectedNodes().Num() == 1;
	}
	
	return false;
}

bool SReferenceViewer::HasExactlyOnePackageNodeSelected() const
{
	if (GraphEditorPtr.IsValid())
	{
		if (GraphEditorPtr->GetSelectedNodes().Num() != 1)
		{
			return false;
		}

		TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
		for (UObject* Node : SelectedNodes)
		{
			UEdGraphNode_Reference* ReferenceNode = Cast<UEdGraphNode_Reference>(Node);
			if (ReferenceNode)
			{
				if (ReferenceNode->IsPackage())
				{
					return true;
				}
			}
			return false;
		}
	}

	return false;
}

bool SReferenceViewer::HasAtLeastOnePackageNodeSelected() const
{
	if ( GraphEditorPtr.IsValid() )
	{
		TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
		for (UObject* Node : SelectedNodes)
		{
			UEdGraphNode_Reference* ReferenceNode = Cast<UEdGraphNode_Reference>(Node);
			if (ReferenceNode)
			{
				if (ReferenceNode->IsPackage())
				{
					return true;
				}
			}
		}
	}
	
	return false;
}

bool SReferenceViewer::HasAtLeastOneRealNodeSelected() const
{
	if (GraphEditorPtr.IsValid())
	{
		TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
		for (UObject* Node : SelectedNodes)
		{
			UEdGraphNode_Reference* ReferenceNode = Cast<UEdGraphNode_Reference>(Node);
			if (ReferenceNode)
			{
				if (!ReferenceNode->IsCollapsed())
				{
					return true;
				}
			}
		}
	}

	return false;
}

void SReferenceViewer::OnAssetRegistryChanged(const FAssetData& AssetData)
{
	// We don't do more specific checking because that data is not exposed, and it wouldn't handle newly added references anyway
	bDirtyResults = true;
}

void SReferenceViewer::OnInitialAssetRegistrySearchComplete()
{
	if ( GraphObj )
	{
		GraphObj->RebuildGraph();
	}
}

void SReferenceViewer::ZoomToFit()
{
	if (GraphEditorPtr.IsValid())
	{
		GraphEditorPtr->ZoomToFit(true);
	}
}

bool SReferenceViewer::CanZoomToFit() const
{
	if (GraphEditorPtr.IsValid())
	{
		return true;
	}

	return false;
}

void SReferenceViewer::OnFind()
{
	FSlateApplication::Get().SetKeyboardFocus(SearchBox, EFocusCause::SetDirectly);
}

void SReferenceViewer::HandleOnSearchTextChanged(const FText& SearchText)
{
	if (GraphObj == nullptr || !GraphEditorPtr.IsValid())
	{
		return;
	}

	GraphEditorPtr->ClearSelectionSet();

	if (SearchText.IsEmpty())
	{
		return;
	}

	FString SearchString = SearchText.ToString();
	TArray<FString> SearchWords;
	SearchString.ParseIntoArrayWS( SearchWords );

	TArray<UEdGraphNode_Reference*> AllNodes;
	GraphObj->GetNodesOfClass<UEdGraphNode_Reference>( AllNodes );

	TArray<FName> NodePackageNames;
	for (UEdGraphNode_Reference* Node : AllNodes)
	{
		NodePackageNames.Empty();
		Node->GetAllPackageNames(NodePackageNames);

		for (const FName& PackageName : NodePackageNames)
		{
			// package name must match all words
			bool bMatch = true;
			for (const FString& Word : SearchWords)
			{
				if (!PackageName.ToString().Contains(Word))
				{
					bMatch = false;
					break;
				}
			}

			if (bMatch)
			{
				GraphEditorPtr->SetNodeSelection(Node, true);
				break;
			}
		}
	}
}

void SReferenceViewer::HandleOnSearchTextCommitted(const FText& SearchText, ETextCommit::Type CommitType)
{
	if (!GraphEditorPtr.IsValid())
	{
		return;
	}

	if (CommitType == ETextCommit::OnCleared)
	{
		GraphEditorPtr->ClearSelectionSet();
	}
	else if (CommitType == ETextCommit::OnEnter)
	{
		HandleOnSearchTextChanged(SearchBox->GetText());
	}
	
	GraphEditorPtr->ZoomToFit(true);
}

#undef LOCTEXT_NAMESPACE
