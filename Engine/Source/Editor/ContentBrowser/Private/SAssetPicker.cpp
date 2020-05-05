// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAssetPicker.h"
#include "Styling/SlateTypes.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Commands/UICommandList.h"
#include "Widgets/SBoxPanel.h"
#include "Layout/WidgetPath.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "EditorStyleSet.h"
#include "FrontendFilters.h"
#include "SAssetSearchBox.h"
#include "SFilterList.h"
#include "SAssetView.h"
#include "SContentBrowser.h"
#include "Framework/Commands/GenericCommands.h"
#include "Editor.h"
#include "PropertyHandle.h"

#define LOCTEXT_NAMESPACE "ContentBrowser"

SAssetPicker::~SAssetPicker()
{
	SaveSettings();
}

void SAssetPicker::Construct( const FArguments& InArgs )
{
	BindCommands();

	OnAssetsActivated = InArgs._AssetPickerConfig.OnAssetsActivated;
	OnAssetSelected = InArgs._AssetPickerConfig.OnAssetSelected;
	OnAssetDoubleClicked = InArgs._AssetPickerConfig.OnAssetDoubleClicked;
	OnAssetEnterPressed = InArgs._AssetPickerConfig.OnAssetEnterPressed;
	bPendingFocusNextFrame = InArgs._AssetPickerConfig.bFocusSearchBoxWhenOpened;
	DefaultFilterMenuExpansion = InArgs._AssetPickerConfig.DefaultFilterMenuExpansion;
	SaveSettingsName = InArgs._AssetPickerConfig.SaveSettingsName;
	OnFolderEnteredDelegate = InArgs._AssetPickerConfig.OnFolderEntered;

	if ( InArgs._AssetPickerConfig.bFocusSearchBoxWhenOpened )
	{
		RegisterActiveTimer( 0.f, FWidgetActiveTimerDelegate::CreateSP( this, &SAssetPicker::SetFocusPostConstruct ) );
	}

	for (auto DelegateIt = InArgs._AssetPickerConfig.GetCurrentSelectionDelegates.CreateConstIterator(); DelegateIt; ++DelegateIt)
	{
		if ((*DelegateIt) != NULL)
		{
			(**DelegateIt) = FGetCurrentSelectionDelegate::CreateSP(this, &SAssetPicker::GetCurrentSelection);
		}
	}

	for(auto DelegateIt = InArgs._AssetPickerConfig.SyncToAssetsDelegates.CreateConstIterator(); DelegateIt; ++DelegateIt)
	{
		if((*DelegateIt) != NULL)
		{
			(**DelegateIt) = FSyncToAssetsDelegate::CreateSP(this, &SAssetPicker::SyncToAssets);
		}
	}

	for (auto DelegateIt = InArgs._AssetPickerConfig.SetFilterDelegates.CreateConstIterator(); DelegateIt; ++DelegateIt)
	{
		if ((*DelegateIt) != NULL)
		{
			(**DelegateIt) = FSetARFilterDelegate::CreateSP(this, &SAssetPicker::SetNewBackendFilter);
		}
	}

	for (auto DelegateIt = InArgs._AssetPickerConfig.RefreshAssetViewDelegates.CreateConstIterator(); DelegateIt; ++DelegateIt)
	{
		if ((*DelegateIt) != NULL)
		{
			(**DelegateIt) = FRefreshAssetViewDelegate::CreateSP(this, &SAssetPicker::RefreshAssetView);
		}
	}

	TSharedRef<SVerticalBox> VerticalBox = SNew(SVerticalBox);

	ChildSlot
	[
		VerticalBox
	];

	TAttribute< FText > HighlightText;
	EThumbnailLabel::Type ThumbnailLabel = InArgs._AssetPickerConfig.ThumbnailLabel;

	FrontendFilters = MakeShareable(new FAssetFilterCollectionType());

	// Search box
	if (!InArgs._AssetPickerConfig.bAutohideSearchBar)
	{
		HighlightText = TAttribute< FText >( this, &SAssetPicker::GetHighlightedText );

		TSharedRef<SHorizontalBox> HorizontalBox = SNew(SHorizontalBox);

		if (InArgs._AssetPickerConfig.bAddFilterUI)
		{
			// Filter
			HorizontalBox->AddSlot()
			.AutoWidth()
			[
				SAssignNew(FilterComboButtonPtr, SComboButton)
				.ComboButtonStyle( FEditorStyle::Get(), "GenericFilters.ComboButtonStyle" )
				.ForegroundColor(FLinearColor::White)
				.ToolTipText( LOCTEXT( "AddFilterToolTip", "Add an asset filter." ) )
				.OnGetMenuContent( this, &SAssetPicker::MakeAddFilterMenu )
				.HasDownArrow( true )
				.ContentPadding( FMargin( 1, 0 ) )
				.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("ContentBrowserFiltersCombo")))
				.ButtonContent()
				[
					SNew( STextBlock )
					.TextStyle( FEditorStyle::Get(), "GenericFilters.TextStyle" )
					.Text( LOCTEXT( "Filters", "Filters" ) )
				]
			];
		}

		HorizontalBox->AddSlot()
		.FillWidth(1.0f)
		[
			SAssignNew( SearchBoxPtr, SAssetSearchBox )
			.HintText(NSLOCTEXT( "ContentBrowser", "SearchBoxHint", "Search Assets" ))
			.OnTextChanged( this, &SAssetPicker::OnSearchBoxChanged )
			.OnTextCommitted( this, &SAssetPicker::OnSearchBoxCommitted )
			.DelayChangeNotificationsWhileTyping( true )
			.OnKeyDownHandler( this, &SAssetPicker::HandleKeyDownFromSearchBox )
		];

		// The 'Other Developers' filter is always on by design.
		HorizontalBox->AddSlot()
		.AutoWidth()
		[
			SNew(SCheckBox)
			.Style(FEditorStyle::Get(), "ToggleButtonCheckbox")
			.ToolTipText(this, &SAssetPicker::GetShowOtherDevelopersToolTip)
			.OnCheckStateChanged(this, &SAssetPicker::HandleShowOtherDevelopersCheckStateChanged)
			.IsChecked(this, &SAssetPicker::GetShowOtherDevelopersCheckState)
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("ContentBrowser.ColumnViewDeveloperFolderIcon"))
			]
		];

		VerticalBox->AddSlot()
		.AutoHeight()
		.Padding( 0, 0, 0, 1 )
		[
			HorizontalBox
		];
	}

	// "None" button
	if (InArgs._AssetPickerConfig.bAllowNullSelection)
	{
		VerticalBox->AddSlot()
		.AutoHeight()
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SButton)
						.ButtonStyle( FEditorStyle::Get(), "ContentBrowser.NoneButton" )
						.TextStyle( FEditorStyle::Get(), "ContentBrowser.NoneButtonText" )
						.Text( LOCTEXT("NoneButtonText", "( None )") )
						.ToolTipText( LOCTEXT("NoneButtonTooltip", "Clears the asset selection.") )
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.OnClicked(this, &SAssetPicker::OnNoneButtonClicked)
				]

			// Trailing separator
			+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 4)
				[
					SNew(SSeparator)
						.Orientation(Orient_Horizontal)
				]
		];
	}

	// Asset view
	
	// Break up the incoming filter into a sources data and backend filter.
	CurrentSourcesData = FSourcesData(InArgs._AssetPickerConfig.Filter.PackagePaths, InArgs._AssetPickerConfig.Collections);
	CurrentBackendFilter = InArgs._AssetPickerConfig.Filter;
	CurrentBackendFilter.PackagePaths.Reset();

	if (InArgs._AssetPickerConfig.bAddFilterUI)
	{
		// Filters
		TArray<UClass*> FilterClassList;
		for(auto Iter = CurrentBackendFilter.ClassNames.CreateIterator(); Iter; ++Iter)
		{
			FName ClassName = (*Iter);
			UClass* FilterClass = FindObject<UClass>(ANY_PACKAGE, *ClassName.ToString());
			if(FilterClass)
			{
				FilterClassList.AddUnique(FilterClass);
			}
		}

		VerticalBox->AddSlot()
		.AutoHeight()
		[
			SAssignNew(FilterListPtr, SFilterList)
			.OnFilterChanged(this, &SAssetPicker::OnFilterChanged)
			.FrontendFilters(FrontendFilters)
			.InitialClassFilters(FilterClassList)
			.ExtraFrontendFilters(InArgs._AssetPickerConfig.ExtraFrontendFilters)
		];

		// Use the 'other developer' filter from the filter list widget. 
		OtherDevelopersFilter = StaticCastSharedPtr<FFrontendFilter_ShowOtherDevelopers>(FilterListPtr->GetFrontendFilter(TEXT("ShowOtherDevelopers")));
	}
	else
	{
		// Filter UI is off, but the 'other developer' filter is a built-in feature.
		OtherDevelopersFilter = MakeShared<FFrontendFilter_ShowOtherDevelopers>(nullptr);
		FrontendFilters->Add(OtherDevelopersFilter);
	}

	// Make game-specific filter
	FOnShouldFilterAsset ShouldFilterAssetDelegate;
	{
		FAssetReferenceFilterContext AssetReferenceFilterContext;
		AssetReferenceFilterContext.ReferencingAssets = InArgs._AssetPickerConfig.AdditionalReferencingAssets;
		if (InArgs._AssetPickerConfig.PropertyHandle.IsValid())
		{
			TArray<UObject*> ReferencingObjects;
			InArgs._AssetPickerConfig.PropertyHandle->GetOuterObjects(ReferencingObjects);
			for (UObject* ReferencingObject : ReferencingObjects)
			{
				AssetReferenceFilterContext.ReferencingAssets.Add(FAssetData(ReferencingObject));
			}
		}
		TSharedPtr<IAssetReferenceFilter> AssetReferenceFilter = GEditor ? GEditor->MakeAssetReferenceFilter(AssetReferenceFilterContext) : nullptr;
		if (AssetReferenceFilter.IsValid())
		{
			FOnShouldFilterAsset ConfigFilter = InArgs._AssetPickerConfig.OnShouldFilterAsset;
			ShouldFilterAssetDelegate = FOnShouldFilterAsset::CreateLambda([ConfigFilter, AssetReferenceFilter](const FAssetData& AssetData) -> bool {
				if (!AssetReferenceFilter->PassesFilter(AssetData))
				{
					return true;
				}
				if (ConfigFilter.IsBound())
				{
					return ConfigFilter.Execute(AssetData);
				}
				return false;
			});
		}
		else
		{
			ShouldFilterAssetDelegate = InArgs._AssetPickerConfig.OnShouldFilterAsset;
		}
	}

	VerticalBox->AddSlot()
	.FillHeight(1.f)
	[
		SAssignNew(AssetViewPtr, SAssetView)
		.SelectionMode( InArgs._AssetPickerConfig.SelectionMode )
		.OnShouldFilterAsset(ShouldFilterAssetDelegate)
		.OnAssetSelectionChanged(this, &SAssetPicker::HandleAssetSelectionChanged)
		.OnAssetsActivated(this, &SAssetPicker::HandleAssetsActivated)
		.OnGetAssetContextMenu(InArgs._AssetPickerConfig.OnGetAssetContextMenu)
		.OnGetFolderContextMenu(InArgs._AssetPickerConfig.OnGetFolderContextMenu)
		.OnIsAssetValidForCustomToolTip(InArgs._AssetPickerConfig.OnIsAssetValidForCustomToolTip)
		.OnGetCustomAssetToolTip(InArgs._AssetPickerConfig.OnGetCustomAssetToolTip)
		.OnVisualizeAssetToolTip(InArgs._AssetPickerConfig.OnVisualizeAssetToolTip)
		.OnAssetToolTipClosing(InArgs._AssetPickerConfig.OnAssetToolTipClosing)
		.AreRealTimeThumbnailsAllowed(this, &SAssetPicker::IsHovered)
		.FrontendFilters(FrontendFilters)
		.InitialSourcesData(CurrentSourcesData)
		.InitialBackendFilter(CurrentBackendFilter)
		.InitialViewType(InArgs._AssetPickerConfig.InitialAssetViewType)
		.InitialAssetSelection(InArgs._AssetPickerConfig.InitialAssetSelection)
		.ThumbnailScale(InArgs._AssetPickerConfig.ThumbnailScale)
		.ShowBottomToolbar(InArgs._AssetPickerConfig.bShowBottomToolbar)
		.OnAssetTagWantsToBeDisplayed(InArgs._AssetPickerConfig.OnAssetTagWantsToBeDisplayed)
		.OnGetCustomSourceAssets(InArgs._AssetPickerConfig.OnGetCustomSourceAssets)
		.AllowDragging( InArgs._AssetPickerConfig.bAllowDragging )
		.CanShowClasses( InArgs._AssetPickerConfig.bCanShowClasses )
		.CanShowFolders( InArgs._AssetPickerConfig.bCanShowFolders )
		.ShowPathInColumnView( InArgs._AssetPickerConfig.bShowPathInColumnView)
		.ShowTypeInColumnView( InArgs._AssetPickerConfig.bShowTypeInColumnView)
		.SortByPathInColumnView( InArgs._AssetPickerConfig.bSortByPathInColumnView)
		.FilterRecursivelyWithBackendFilter( false )
		.CanShowRealTimeThumbnails( InArgs._AssetPickerConfig.bCanShowRealTimeThumbnails )
		.CanShowDevelopersFolder( InArgs._AssetPickerConfig.bCanShowDevelopersFolder )
		.ForceShowEngineContent( InArgs._AssetPickerConfig.bForceShowEngineContent)
		.PreloadAssetsForContextMenu( InArgs._AssetPickerConfig.bPreloadAssetsForContextMenu )
		.HighlightedText( HighlightText )
		.ThumbnailLabel( ThumbnailLabel )
		.AssetShowWarningText( InArgs._AssetPickerConfig.AssetShowWarningText)
		.AllowFocusOnSync(false)	// Stop the asset view from stealing focus (we're in control of that)
		.OnPathSelected(this, &SAssetPicker::FolderEntered)
		.HiddenColumnNames(InArgs._AssetPickerConfig.HiddenColumnNames)
		.CustomColumns(InArgs._AssetPickerConfig.CustomColumns)
		.OnSearchOptionsChanged(this, &SAssetPicker::HandleSearchSettingsChanged)
	];

	LoadSettings();

	if (AssetViewPtr.IsValid() && !InArgs._AssetPickerConfig.bAutohideSearchBar)
	{
		TextFilter = MakeShareable(new FFrontendFilter_Text());
		bool bClassNamesProvided = (InArgs._AssetPickerConfig.Filter.ClassNames.Num() != 1);
		TextFilter->SetIncludeClassName(bClassNamesProvided || AssetViewPtr->IsIncludingClassNames());
		TextFilter->SetIncludeAssetPath(AssetViewPtr->IsIncludingAssetPaths());
		TextFilter->SetIncludeCollectionNames(AssetViewPtr->IsIncludingCollectionNames());
	}

	AssetViewPtr->RequestSlowFullListRefresh();
}

EActiveTimerReturnType SAssetPicker::SetFocusPostConstruct( double InCurrentTime, float InDeltaTime )
{
	if ( SearchBoxPtr.IsValid() )
	{
		FWidgetPath WidgetToFocusPath;
		FSlateApplication::Get().GeneratePathToWidgetUnchecked( SearchBoxPtr.ToSharedRef(), WidgetToFocusPath );
		FSlateApplication::Get().SetKeyboardFocus( WidgetToFocusPath, EFocusCause::SetDirectly );
		WidgetToFocusPath.GetWindow()->SetWidgetToFocusOnActivate(SearchBoxPtr);

		return EActiveTimerReturnType::Stop;
	}

	return EActiveTimerReturnType::Continue;
}

FReply SAssetPicker::HandleKeyDownFromSearchBox(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Hide the filter list
	if (FilterComboButtonPtr.IsValid())
	{
		FilterComboButtonPtr->SetIsOpen(false);
	}

	// Up and down move thru the filtered list
	int32 SelectionDelta = 0;

	if (InKeyEvent.GetKey() == EKeys::Up)
	{
		SelectionDelta = -1;
	}
	else if (InKeyEvent.GetKey() == EKeys::Down)
	{
		SelectionDelta = +1;
	}

	if (SelectionDelta != 0)
	{
		AssetViewPtr->AdjustActiveSelection(SelectionDelta);

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SAssetPicker::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Enter)
	{
		TArray<FAssetData> SelectionSet = AssetViewPtr->GetSelectedAssets();
		HandleAssetsActivated(SelectionSet, EAssetTypeActivationMethod::Opened);

		return FReply::Handled();
	}

	if (Commands->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SAssetPicker::FolderEntered(const FString& FolderPath)
{
	CurrentSourcesData.PackagePaths.Reset();
	CurrentSourcesData.PackagePaths.Add(FName(*FolderPath));

	AssetViewPtr->SetSourcesData(CurrentSourcesData);

	OnFolderEnteredDelegate.ExecuteIfBound(FolderPath);
}

FText SAssetPicker::GetHighlightedText() const
{
	return TextFilter->GetRawFilterText();
}

void SAssetPicker::SetSearchBoxText(const FText& InSearchText)
{
	// Has anything changed? (need to test case as the operators are case-sensitive)
	if (!InSearchText.ToString().Equals(TextFilter->GetRawFilterText().ToString(), ESearchCase::CaseSensitive))
	{
		TextFilter->SetRawFilterText(InSearchText);
		if (InSearchText.IsEmpty())
		{
			FrontendFilters->Remove(TextFilter);
			AssetViewPtr->SetUserSearching(false);
		}
		else
		{
			FrontendFilters->Add(TextFilter);
			AssetViewPtr->SetUserSearching(true);
		}
	}
}

void SAssetPicker::OnSearchBoxChanged(const FText& InSearchText)
{
	SetSearchBoxText( InSearchText );
}

void SAssetPicker::OnSearchBoxCommitted(const FText& InSearchText, ETextCommit::Type CommitInfo)
{
	SetSearchBoxText( InSearchText );

	if (CommitInfo == ETextCommit::OnEnter)
	{
		TArray<FAssetData> SelectionSet = AssetViewPtr->GetSelectedAssets();
		if ( SelectionSet.Num() == 0 )
		{
			AssetViewPtr->AdjustActiveSelection(1);
			SelectionSet = AssetViewPtr->GetSelectedAssets();
		}
		HandleAssetsActivated(SelectionSet, EAssetTypeActivationMethod::Opened);
	}
}

void SAssetPicker::SetNewBackendFilter(const FARFilter& NewFilter)
{
	CurrentSourcesData.PackagePaths = NewFilter.PackagePaths;
	if(AssetViewPtr.IsValid())
	{
		AssetViewPtr->SetSourcesData(CurrentSourcesData);
	}

	CurrentBackendFilter = NewFilter;
	CurrentBackendFilter.PackagePaths.Reset();

	// Update the Text filter too, since now class names may no longer matter
	if (TextFilter.IsValid())
	{
		TextFilter->SetIncludeClassName(NewFilter.ClassNames.Num() != 1);
	}

	OnFilterChanged();
}

TSharedRef<SWidget> SAssetPicker::MakeAddFilterMenu()
{
	return FilterListPtr->ExternalMakeAddFilterMenu(DefaultFilterMenuExpansion);
}

void SAssetPicker::OnFilterChanged()
{
	FARFilter Filter;
	
	if ( FilterListPtr.IsValid() )
	{
		Filter = FilterListPtr->GetCombinedBackendFilter();
	}

	Filter.Append(CurrentBackendFilter);
	if (AssetViewPtr.IsValid())
	{
		AssetViewPtr->SetBackendFilter( Filter );
	}
}

FReply SAssetPicker::OnNoneButtonClicked()
{
	OnAssetSelected.ExecuteIfBound(FAssetData());
	if (AssetViewPtr.IsValid())
	{
		AssetViewPtr->ClearSelection(true);
	}
	return FReply::Handled();
}

void SAssetPicker::HandleAssetSelectionChanged(const FAssetData& InAssetData, ESelectInfo::Type InSelectInfo)
{
	if(InSelectInfo != ESelectInfo::Direct)
	{
		OnAssetSelected.ExecuteIfBound(InAssetData);
	}
}

void SAssetPicker::HandleAssetsActivated(const TArray<FAssetData>& ActivatedAssets, EAssetTypeActivationMethod::Type ActivationMethod)
{
	if (ActivationMethod == EAssetTypeActivationMethod::DoubleClicked)
	{
		if (ActivatedAssets.Num() == 1)
		{
			OnAssetDoubleClicked.ExecuteIfBound(ActivatedAssets[0]);
		}
	}
	else if (ActivationMethod == EAssetTypeActivationMethod::Opened)
	{
		OnAssetEnterPressed.ExecuteIfBound(ActivatedAssets);
	}

	OnAssetsActivated.ExecuteIfBound( ActivatedAssets, ActivationMethod );
}

void SAssetPicker::SyncToAssets(const TArray<FAssetData>& AssetDataList)
{
	AssetViewPtr->SyncToAssets(AssetDataList);
}

TArray< FAssetData > SAssetPicker::GetCurrentSelection()
{
	return AssetViewPtr->GetSelectedAssets();
}

void SAssetPicker::RefreshAssetView(bool bRefreshSources)
{
	if (bRefreshSources)
	{
		AssetViewPtr->RequestSlowFullListRefresh();
	}
	else
	{
		AssetViewPtr->RequestQuickFrontendListRefresh();
	}
}

FText SAssetPicker::GetShowOtherDevelopersToolTip() const
{
	// NOTE: This documents the filter effect rather than the button action.
	if (FilterListPtr ? FilterListPtr->IsFrontendFilterActive(OtherDevelopersFilter) : OtherDevelopersFilter->GetShowOtherDeveloperAssets())
	{
		return LOCTEXT("ShowingOtherDevelopersFilterTooltipText", "Showing Other Developers Assets");
	}
	else
	{
		return LOCTEXT("HidingOtherDevelopersFilterTooltipText", "Hiding Other Developers Assets");
	}
}

void SAssetPicker::HandleShowOtherDevelopersCheckStateChanged( ECheckBoxState InCheckboxState )
{
	if (FilterListPtr) // Filter UI enabled?
	{
		// Pin+activate or unpin+deactivate the filter. A widget is pinned on the filter UI. It allows the user to activate/deactive the filter independently of the 'checked' state.
		FilterListPtr->SetFrontendFilterCheckState(OtherDevelopersFilter, InCheckboxState);
	}
	else
	{
		OtherDevelopersFilter->SetShowOtherDeveloperAssets(InCheckboxState == ECheckBoxState::Checked); // The checked state matches the active state.
	}
}

ECheckBoxState SAssetPicker::GetShowOtherDevelopersCheckState() const
{
	if (FilterListPtr) // Filter UI enabled?
	{
		return FilterListPtr->GetFrontendFilterCheckState(OtherDevelopersFilter); // Tells whether the 'other developer' filter is pinned on the filter UI. (The filter itself may be active or not).
	}
	else
	{
		return OtherDevelopersFilter->GetShowOtherDeveloperAssets() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; // The checked state matches the active state.
	}
}

void SAssetPicker::OnRenameRequested() const
{
	TArray< FAssetData > AssetViewSelectedAssets = AssetViewPtr->GetSelectedAssets();
	TArray< FString > SelectedFolders = AssetViewPtr->GetSelectedFolders();

	if ( AssetViewSelectedAssets.Num() == 1 && SelectedFolders.Num() == 0 )
	{
		// Don't operate on Redirectors
		if ( AssetViewSelectedAssets[0].AssetClass != UObjectRedirector::StaticClass()->GetFName() )
		{
			AssetViewPtr->RenameAsset(AssetViewSelectedAssets[0]);
		}
	}
	else if ( AssetViewSelectedAssets.Num() == 0 && SelectedFolders.Num() == 1 )
	{
		AssetViewPtr->RenameFolder(SelectedFolders[0]);
	}
}

bool SAssetPicker::CanExecuteRenameRequested()
{
	TArray< FAssetData > AssetViewSelectedAssets = AssetViewPtr->GetSelectedAssets();
	TArray< FString > SelectedFolders = AssetViewPtr->GetSelectedFolders();

	const bool bCanRenameFolder = ( AssetViewSelectedAssets.Num() == 0 && SelectedFolders.Num() == 1 );
	const bool bCanRenameAsset = ( AssetViewSelectedAssets.Num() == 1 && SelectedFolders.Num() == 0 ) && ( AssetViewSelectedAssets[0].AssetClass != UObjectRedirector::StaticClass()->GetFName() ); 

	return	bCanRenameFolder || bCanRenameAsset;
}

void SAssetPicker::BindCommands()
{
	Commands = MakeShareable(new FUICommandList);
	// bind commands
	Commands->MapAction( FGenericCommands::Get().Rename, FUIAction(
		FExecuteAction::CreateSP( this, &SAssetPicker::OnRenameRequested ),
		FCanExecuteAction::CreateSP( this, &SAssetPicker::CanExecuteRenameRequested )
		));
}

void SAssetPicker::LoadSettings()
{
	const FString& SettingsString = SaveSettingsName;

	if ( !SettingsString.IsEmpty() )
	{
		// Load all our data using the settings string as a key in the user settings ini
		if (FilterListPtr.IsValid())
		{
			FilterListPtr->LoadSettings(GEditorPerProjectIni, SContentBrowser::SettingsIniSection, SettingsString);
		}
		
		AssetViewPtr->LoadSettings(GEditorPerProjectIni, SContentBrowser::SettingsIniSection, SettingsString);
	}
}

void SAssetPicker::SaveSettings() const
{
	const FString& SettingsString = SaveSettingsName;

	if ( !SettingsString.IsEmpty() )
	{
		// Save all our data using the settings string as a key in the user settings ini
		if (FilterListPtr.IsValid())
		{
			FilterListPtr->SaveSettings(GEditorPerProjectIni, SContentBrowser::SettingsIniSection, SettingsString);
		}

		AssetViewPtr->SaveSettings(GEditorPerProjectIni, SContentBrowser::SettingsIniSection, SettingsString);
	}
}

void SAssetPicker::HandleSearchSettingsChanged()
{
	bool bClassNamesProvided = (FilterListPtr.IsValid() ? FilterListPtr->GetInitialClassFilters().Num() != 1 : false);
	TextFilter->SetIncludeClassName(bClassNamesProvided || AssetViewPtr->IsIncludingClassNames());
	TextFilter->SetIncludeAssetPath(AssetViewPtr->IsIncludingAssetPaths());
	TextFilter->SetIncludeCollectionNames(AssetViewPtr->IsIncludingCollectionNames());
}

#undef LOCTEXT_NAMESPACE
