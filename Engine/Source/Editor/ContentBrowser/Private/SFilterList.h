// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "AssetData.h"
#include "ARFilter.h"
#include "FrontendFilterBase.h"

class FMenuBuilder;
class SFilter;
class SWrapBox;
enum class ECheckBoxState : uint8;

/**
 * A list of filters currently applied to an asset view.
 */
class SFilterList : public SCompoundWidget
{
public:
	/** Delegate for when filters have changed */
	DECLARE_DELEGATE( FOnFilterChanged );

	DECLARE_DELEGATE_RetVal( TSharedPtr<SWidget>, FOnGetContextMenu );

	SLATE_BEGIN_ARGS( SFilterList ){}

		/** Called when an asset is right clicked */
		SLATE_EVENT( FOnGetContextMenu, OnGetContextMenu )

		/** Delegate for when filters have changed */
		SLATE_EVENT( FOnFilterChanged, OnFilterChanged )

		/** The filter collection used to further filter down assets returned from the backend */
		SLATE_ARGUMENT( TSharedPtr<FAssetFilterCollectionType>, FrontendFilters)

		/** An array of classes to filter the menu by */
		SLATE_ARGUMENT( TArray<UClass*>, InitialClassFilters)

		/** Custom front end filters to be displayed */
		SLATE_ARGUMENT( TArray< TSharedRef<FFrontendFilter> >, ExtraFrontendFilters )

	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct( const FArguments& InArgs );

	/** Returns true if any filters are applied */
	bool HasAnyFilters() const;

	/** Returns all of the filters combined */
	FARFilter GetCombinedBackendFilter() const;

	/** Retrieve a specific frontend filter */
	TSharedPtr<FFrontendFilter> GetFrontendFilter(const FString& InName) const;

	/** Handler for when the floating add filter button was clicked */
	TSharedRef<SWidget> ExternalMakeAddFilterMenu(EAssetTypeCategories::Type MenuExpansion = EAssetTypeCategories::Basic);

	/** Enables all filters */
	void EnableAllFilters();

	/** Disables any applied filters */
	void DisableAllFilters();

	/** Removes all filters in the list */
	void RemoveAllFilters();

	/** Removes all filters in the list except the given one. */
	void RemoveAllButThis(const TSharedRef<SFilter>& FilterToKeep);

	/** Disables any active filters that would hide the supplied items */
	void DisableFiltersThatHideItems(TArrayView<const FContentBrowserItem> ItemList);

	/** Saves any settings to config that should be persistent between editor sessions */
	void SaveSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString) const;

	/** Loads any settings to config that should be persistent between editor sessions */
	void LoadSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString);

	virtual FReply OnMouseButtonUp( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent ) override;

	/** Returns the class filters specified at construction using argument 'InitialClassFilters'. */
	const TArray<UClass*>& GetInitialClassFilters();

	/** Set the check box state of the specified frontend filter (in the filter drop down) and pin/unpin a filter widget on/from the filter bar. When a filter is pinned (was not already pinned), it is activated and deactivated when unpinned. */
	void SetFrontendFilterCheckState(const TSharedPtr<FFrontendFilter>& InFrontendFilter, ECheckBoxState CheckState);

	/** Returns the check box state of the specified frontend filter (in the filter drop down). This tells whether the filter is pinned or not on the filter bar, but not if filter is active or not. @see IsFrontendFilterActive(). */
	ECheckBoxState GetFrontendFilterCheckState(const TSharedPtr<FFrontendFilter>& InFrontendFilter) const;

	/** Returns true if the specified frontend filter is both checked (pinned on the filter bar) and active (contributing to filter the result). */
	bool IsFrontendFilterActive(const TSharedPtr<FFrontendFilter>& InFrontendFilter) const;

private:
	/** Sets the active state of a frontend filter. */
	void SetFrontendFilterActive(const TSharedRef<FFrontendFilter>& Filter, bool bActive);

	/** Adds a filter to the end of the filter box. */
	TSharedRef<SFilter> AddFilter(const TWeakPtr<IAssetTypeActions>& AssetTypeActions);
	TSharedRef<SFilter> AddFilter(const TSharedRef<FFrontendFilter>& FrontendFilter);
	void AddFilter(const TSharedRef<SFilter>& FilterToAdd);

	/** Handler for when the remove filter button was clicked on a filter */
	void RemoveFilter(const TWeakPtr<IAssetTypeActions>& AssetTypeActions, bool ExecuteOnFilterChanged = true);
	void RemoveFilter(const TSharedRef<FFrontendFilter>& FrontendFilter, bool ExecuteOnFilterChanged = true);
	void RemoveFilter(const TSharedRef<SFilter>& FilterToRemove);
	void RemoveFilterAndUpdate(const TSharedRef<SFilter>& FilterToRemove);

	/** Handler for when the enable only this button was clicked on a single filter */
	void EnableOnlyThisFilter(const TSharedRef<SFilter>& FilterToEnable);

	/** Handler for when a frontend filter state has changed */
	void FrontendFilterChanged(TSharedRef<FFrontendFilter> FrontendFilter);

	/** Handler for when the add filter menu is populated by a category */
	void CreateFiltersMenuCategory(FMenuBuilder& MenuBuilder, const TArray<TWeakPtr<IAssetTypeActions>> AssetTypeActionsList) const;

	/** Handler for when the add filter menu is populated by a non-category */
	void CreateOtherFiltersMenuCategory(FMenuBuilder& MenuBuilder, TSharedPtr<FFrontendFilterCategory> MenuCategory) const;

	/** Handler for when the add filter button was clicked */
	TSharedRef<SWidget> MakeAddFilterMenu(EAssetTypeCategories::Type MenuExpansion = EAssetTypeCategories::Basic);

	/** Handler for when filter by type is selected */
	void FilterByTypeClicked(TWeakPtr<IAssetTypeActions> AssetTypeActions);

	/** Handler to determine the "checked" state of an asset type in the filter dropdown */
	bool IsAssetTypeActionsInUse(TWeakPtr<IAssetTypeActions> AssetTypeActions) const;

	/** Handler for when filter by type category is selected */
	void FilterByTypeCategoryClicked(EAssetTypeCategories::Type Category);

	/** Handler to determine the "checked" state of an asset type category in the filter dropdown */
	bool IsAssetTypeCategoryInUse(EAssetTypeCategories::Type Category) const;

	/** Returns all the asset type actions objects for the specified category */
	void GetTypeActionsForCategory(EAssetTypeCategories::Type Category, TArray< TWeakPtr<IAssetTypeActions> >& TypeActions) const;

	void FrontendFilterClicked(TSharedRef<FFrontendFilter> FrontendFilter);
	bool IsFrontendFilterInUse(TSharedRef<FFrontendFilter> FrontendFilter) const;
	void FrontendFilterCategoryClicked(TSharedPtr<FFrontendFilterCategory> MenuCategory);
	bool IsFrontendFilterCategoryInUse(TSharedPtr<FFrontendFilterCategory> MenuCategory) const;

	/** Called when reset filters option is pressed */
	void OnResetFilters();

	/** Called to set a filter active externally */
	void OnSetFilterActive(bool bInActive, TWeakPtr<FFrontendFilter> InWeakFilter);

private:
	/** The horizontal box which contains all the filters */
	TSharedPtr<SWrapBox> FilterBox;

	/** All SFilters in the list */
	TArray<TSharedRef<SFilter>> Filters;

	/** The filter collection used to further filter down assets returned from the backend */
	TSharedPtr<FAssetFilterCollectionType> FrontendFilters;

	/** All possible frontend filter objects */
	TArray< TSharedRef<FFrontendFilter> > AllFrontendFilters;

	/** All frontend filter categories (for menu construction) */
	TArray< TSharedPtr<FFrontendFilterCategory> > AllFrontendFilterCategories;

	/** List of classes that our filters must match */
	TArray<UClass*> InitialClassFilters;

	/** Delegate for getting the context menu. */
	FOnGetContextMenu OnGetContextMenu;

	/** Delegate for when filters have changed */
	FOnFilterChanged OnFilterChanged;

	friend struct FFrontendFilterExternalActivationHelper;
};
