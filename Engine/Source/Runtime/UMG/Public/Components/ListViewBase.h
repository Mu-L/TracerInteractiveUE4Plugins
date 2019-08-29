// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/Widget.h"
#include "Slate/SObjectTableRow.h"
#include "Blueprint/UserWidgetPool.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Views/STileView.h"
#include "Widgets/Views/STreeView.h"

#include "ListViewBase.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnListEntryGeneratedDynamic, UUserWidget*, Widget);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnListEntryReleasedDynamic, UUserWidget*, Widget);

//////////////////////////////////////////////////////////////////////////
// ITypedUMGListView<T>
//////////////////////////////////////////////////////////////////////////

/**
 * Mirrored SListView<T> API for easier interaction with a bound UListViewBase widget that isn't based on UObject* items
 * See declarations on SListView for more info on each function and event
 *
 * Note that, being a template class, this is not a UClass and therefore cannot be exposed to Blueprint.
 * If you are using UObject* items, just use (or inherit from) UListView directly
 * Otherwise, it is up to the child class to propagate events and/or expose functions to BP as needed
 *
 * Use the IMPLEMENT_TYPED_UMG_LIST() for the implementation boilerplate in your implementing class.
 */
template <typename ItemType>
class ITypedUMGListView
{
public:
	using NullableItemType = typename SListView<ItemType>::NullableItemType;

	DECLARE_EVENT_OneParam(UListView, FSimpleListItemEvent, ItemType);
	virtual FSimpleListItemEvent& OnItemClicked() const = 0;
	virtual FSimpleListItemEvent& OnItemDoubleClicked() const = 0;

	DECLARE_EVENT_TwoParams(UListView, FOnItemIsHoveredChanged, ItemType, bool);
	virtual FOnItemIsHoveredChanged& OnItemIsHoveredChanged() const = 0;

	DECLARE_EVENT_OneParam(UListView, FOnItemSelectionChanged, NullableItemType);
	virtual FOnItemSelectionChanged& OnItemSelectionChanged() const = 0;

	DECLARE_EVENT_TwoParams(UListView, FOnItemScrolledIntoView, ItemType, UUserWidget&);
	virtual FOnItemScrolledIntoView& OnItemScrolledIntoView() const = 0;

	DECLARE_EVENT_TwoParams(UTreeView, FOnItemExpansionChanged, ItemType, bool);
	virtual FOnItemExpansionChanged& OnItemExpansionChanged() const = 0;

protected:
	/** Override and return a ptr to the SListView owned by the implementing UListViewBase */
	virtual SListView<ItemType>* GetMyListView() const = 0;

public:
	// Public API to match that of SListView
	NullableItemType GetSelectedItem() const
	{
		if (SListView<ItemType>* MyListView = GetMyListView())
		{
			TArray<ItemType> SelectedItems = MyListView->GetSelectedItems();
			if (SelectedItems.Num() > 0)
			{
				return SelectedItems[0];
			}
		}
		return nullptr;
	}

	const ItemType* ItemFromEntryWidget(const UUserWidget& EntryWidget) const
	{
		SListView<ItemType>* MyListView = GetMyListView();
		if (ensure(EntryWidget.Implements<UUserListEntry>()) && MyListView)
		{
			TSharedPtr<SObjectTableRow<ItemType>> ObjectTableRow = StaticCastSharedPtr<SObjectTableRow<ItemType>>(EntryWidget.GetCachedWidget());
			if (ObjectTableRow.IsValid())
			{
				return MyListView->ItemFromWidget(ObjectTableRow.Get());
			}
		}
		return nullptr;
	}

	template <typename RowWidgetT = UUserWidget>
	RowWidgetT* GetEntryWidgetFromItem(const ItemType& Item) const
	{
		TSharedPtr<SObjectTableRow<ItemType>> ObjectRow = GetObjectRowFromItem(Item);
		if (ObjectRow.IsValid())
		{
			return Cast<RowWidgetT>(ObjectRow->GetWidgetObject());
		}
		return nullptr;
	}

	int32 GetSelectedItems(TArray<ItemType>& OutSelectedItems) const
	{
		SListView<ItemType>* MyListView = GetMyListView();
		return MyListView ? MyListView->GetSelectedItems(OutSelectedItems) : 0;
	}

	int32 GetNumItemsSelected() const
	{
		SListView<ItemType>* MyListView = GetMyListView();
		return MyListView ? MyListView->GetNumItemsSelected() : 0;
	}

	void SetSelectedItem(const ItemType& SoleSelectedItem, ESelectInfo::Type SelectInfo = ESelectInfo::Direct) 
	{ 
		if (SListView<ItemType>* MyListView = GetMyListView())
		{
			MyListView->SetSelection(SoleSelectedItem, SelectInfo);
		}
	}

	void SetItemSelection(const ItemType& Item, bool bIsSelected, ESelectInfo::Type SelectInfo = ESelectInfo::Direct)
	{
		if (SListView<ItemType>* MyListView = GetMyListView())
		{
			MyListView->SetItemSelection(Item, bIsSelected, SelectInfo);
		}
	}

	void ClearSelection()
	{
		if (SListView<ItemType>* MyListView = GetMyListView())
		{
			MyListView->ClearSelection();
		}
	}

	bool IsItemVisible(const ItemType& Item) const
	{
		SListView<ItemType>* MyListView = GetMyListView();
		return MyListView ? MyListView->IsItemVisible(Item) : false;
	}

	bool IsItemSelected(const ItemType& Item) const
	{
		SListView<ItemType>* MyListView = GetMyListView();
		return MyListView ? MyListView->IsItemSelected(Item) : false;
	}

	void RequestNavigateToItem(const ItemType& Item, uint32 UserIndex = 0)
	{
		if (SListView<ItemType>* MyListView = GetMyListView())
		{
			MyListView->RequestNavigateToItem(Item, UserIndex);
		}
	}

	void RequestScrollItemIntoView(const ItemType& Item, const uint32 UserIndex = 0)
	{
		if (SListView<ItemType>* MyListView = GetMyListView())
		{
			MyListView->RequestScrollIntoView(Item, UserIndex);
		}
	}

	void CancelScrollIntoView()
	{
		if (SListView<ItemType>* MyListView = GetMyListView())
		{
			MyListView->CancelScrollIntoView();
		}
	}

protected:
	/**
	 * ListView construction helpers
	 * Use these instead of SNew-ing your owned ListView directly to get exposed events for free
	 */

	template <template<typename> class ListViewT = SListView, typename UListViewBaseT>
	static TSharedRef<ListViewT<ItemType>> ConstructListView(UListViewBaseT* Implementer,
		TArray<ItemType>& ListItems,
		ESelectionMode::Type SelectionMode = ESelectionMode::Single,
		bool bClearSelectionOnClick = false,
		EConsumeMouseWheel ConsumeMouseWheel = EConsumeMouseWheel::WhenScrollingPossible)
	{
		static_assert(TIsDerivedFrom<ListViewT<ItemType>, SListView<ItemType>>::IsDerived, "ConstructListView can only construct instances of SListView classes");
		return SNew(ListViewT<ItemType>)
			.HandleGamepadEvents(true)
			.ListItemsSource(&ListItems)
			.ClearSelectionOnClick(bClearSelectionOnClick)
			.ConsumeMouseWheel(ConsumeMouseWheel)
			.SelectionMode(SelectionMode)
			.OnGenerateRow_UObject(Implementer, &UListViewBaseT::HandleGenerateRow)
			.OnSelectionChanged_UObject(Implementer, &UListViewBaseT::HandleSelectionChanged)
			.OnRowReleased_UObject(Implementer, &UListViewBaseT::HandleRowReleased)
			.OnItemScrolledIntoView_UObject(Implementer, &UListViewBaseT::HandleItemScrolledIntoView)
			.OnMouseButtonClick_UObject(Implementer, &UListViewBaseT::HandleItemClicked)
			.OnMouseButtonDoubleClick_UObject(Implementer, &UListViewBaseT::HandleItemDoubleClicked);
	}

	template <template<typename> class TileViewT = STileView, typename UListViewBaseT>
	static TSharedRef<TileViewT<ItemType>> ConstructTileView(UListViewBaseT* Implementer,
		TArray<ItemType>& ListItems,
		EListItemAlignment TileAlignment,
		TAttribute<float> TileHeight,
		TAttribute<float> TileWidth,
		ESelectionMode::Type SelectionMode = ESelectionMode::Single,
		bool bClearSelectionOnClick = false,
		bool bWrapHorizontalNavigation = false,
		EConsumeMouseWheel ConsumeMouseWheel = EConsumeMouseWheel::WhenScrollingPossible)
	{
		static_assert(TIsDerivedFrom<TileViewT<ItemType>, STileView<ItemType>>::IsDerived, "ConstructTileView can only construct instances of STileView classes");
		return SNew(TileViewT<ItemType>)
			.HandleGamepadEvents(true)
			.ListItemsSource(&ListItems)
			.ClearSelectionOnClick(bClearSelectionOnClick)
			.WrapHorizontalNavigation(bWrapHorizontalNavigation)
			.ConsumeMouseWheel(ConsumeMouseWheel)
			.SelectionMode(SelectionMode)
			.ItemHeight(TileHeight)
			.ItemWidth(TileWidth)
			.ItemAlignment(TileAlignment)
			.OnGenerateTile_UObject(Implementer, &UListViewBaseT::HandleGenerateRow)
			.OnTileReleased_UObject(Implementer, &UListViewBaseT::HandleRowReleased)
			.OnSelectionChanged_UObject(Implementer, &UListViewBaseT::HandleSelectionChanged)
			.OnItemScrolledIntoView_UObject(Implementer, &UListViewBaseT::HandleItemScrolledIntoView)
			.OnMouseButtonClick_UObject(Implementer, &UListViewBaseT::HandleItemClicked)
			.OnMouseButtonDoubleClick_UObject(Implementer, &UListViewBaseT::HandleItemDoubleClicked);
	}

	template <template<typename> class TreeViewT = STreeView, typename UListViewBaseT>
	static TSharedRef<TreeViewT<ItemType>> ConstructTreeView(UListViewBaseT* Implementer,
		TArray<ItemType>& ListItems,
		ESelectionMode::Type SelectionMode = ESelectionMode::Single,
		bool bClearSelectionOnClick = false,
		EConsumeMouseWheel ConsumeMouseWheel = EConsumeMouseWheel::WhenScrollingPossible)
	{
		static_assert(TIsDerivedFrom<TreeViewT<ItemType>, STreeView<ItemType>>::IsDerived, "ConstructTreeView can only construct instances of STreeView classes");
		return SNew(TreeViewT<ItemType>)
			.HandleGamepadEvents(true)
			.TreeItemsSource(&ListItems)
			.ClearSelectionOnClick(bClearSelectionOnClick)
			.ConsumeMouseWheel(ConsumeMouseWheel)
			.SelectionMode(SelectionMode)
			.OnGenerateRow_UObject(Implementer, &UListViewBaseT::HandleGenerateRow)
			.OnSelectionChanged_UObject(Implementer, &UListViewBaseT::HandleSelectionChanged)
			.OnRowReleased_UObject(Implementer, &UListViewBaseT::HandleRowReleased)
			.OnItemScrolledIntoView_UObject(Implementer, &UListViewBaseT::HandleItemScrolledIntoView)
			.OnMouseButtonClick_UObject(Implementer, &UListViewBaseT::HandleItemClicked)
			.OnMouseButtonDoubleClick_UObject(Implementer, &UListViewBaseT::HandleItemDoubleClicked)
			.OnGetChildren_UObject(Implementer, &UListViewBaseT::HandleGetChildren)
			.OnExpansionChanged_UObject(Implementer, &UListViewBaseT::HandleExpansionChanged);
	}

	/** Gets the SObjectTableRow underlying the UMG EntryWidget that represents the given item (if one exists) */
	template <template<typename> class ObjectRowT = SObjectTableRow>
	TSharedPtr<ObjectRowT<ItemType>> GetObjectRowFromItem(const ItemType& Item) const
	{
		static_assert(TIsDerivedFrom<ObjectRowT<ItemType>, SObjectTableRow<ItemType>>::IsDerived, "All UMG table rows must be or derive from SObjectTableRow.");

		if (SListView<ItemType>* MyListView = GetMyListView())
		{
			TSharedPtr<ITableRow> RowWidget = MyListView->WidgetFromItem(Item);
			return StaticCastSharedPtr<ObjectRowT<ItemType>>(RowWidget);
		}
		return nullptr;
	}

	/**
	 * Generates the actual entry widget that represents the given item.
	 *Expected to be used in concert with UListViewBase::GenerateTypedEntry().
	 */
	virtual UUserWidget& OnGenerateEntryWidgetInternal(ItemType Item, const TSharedRef<STableViewBase>& OwnerTable) = 0;

	/** Gets the desired padding for the entry representing the given item */
	virtual FMargin GetDesiredEntryPadding(ItemType Item) const { return FMargin(0.f); }

	/** TreeViews only. Gets the items to consider children of the given item when generating child entries. */
	virtual void OnGetChildrenInternal(ItemType Item, TArray<ItemType>& OutChildren) const {}

	/** ListView events - implement these instead of binding handlers directly to a list */
	virtual void OnItemClickedInternal(ItemType Item) {}
	virtual void OnItemDoubleClickedInternal(ItemType Item) {}
	virtual void OnSelectionChangedInternal(NullableItemType FirstSelectedItem) {}
	virtual void OnItemScrolledIntoViewInternal(ItemType Item, UUserWidget& EntryWidget) {}
	virtual void OnItemExpansionChangedInternal(ItemType Item, bool bIsExpanded) {}

private:
	TSharedRef<ITableRow> HandleGenerateRow(ItemType Item, const TSharedRef<STableViewBase>& OwnerTable)
	{
		UUserWidget& EntryWidget = OnGenerateEntryWidgetInternal(Item, OwnerTable);
		EntryWidget.SetPadding(GetDesiredEntryPadding(Item));
		return StaticCastSharedPtr<SObjectTableRow<ItemType>>(EntryWidget.GetCachedWidget()).ToSharedRef();
	}

	void HandleItemClicked(ItemType Item)
	{
		OnItemClickedInternal(Item);
		OnItemClicked().Broadcast(Item);
	}

	void HandleItemDoubleClicked(ItemType Item)
	{
		OnItemDoubleClickedInternal(Item);
		OnItemDoubleClicked().Broadcast(Item);
	}

	void HandleSelectionChanged(NullableItemType Item, ESelectInfo::Type SelectInfo)
	{
		//@todo DanH ListView: This really isn't the event that many will expect it to be - is it worth having at all? 
		//		It only works for single selection lists, and even then only broadcasts at the end - you don't get anything for de-selection
		OnSelectionChangedInternal(Item);
		OnItemSelectionChanged().Broadcast(Item);
	}

	void HandleItemScrolledIntoView(ItemType Item, const TSharedPtr<ITableRow>& InWidget)
	{
		UUserWidget* RowWidget = GetEntryWidgetFromItem(Item);
		if (ensure(RowWidget))
		{
			OnItemScrolledIntoViewInternal(Item, *RowWidget);
			OnItemScrolledIntoView().Broadcast(Item, *RowWidget);
		}
	}

	void HandleExpansionChanged(ItemType Item, bool bIsExpanded)
	{
		// If this item is currently visible (i.e. has a widget representing it), notify the widget of the expansion change
		auto ObjectRow = GetObjectRowFromItem(Item);
		if (ObjectRow.IsValid())
		{
			ObjectRow->NotifyItemExpansionChanged(bIsExpanded);
		}

		OnItemExpansionChangedInternal(Item, bIsExpanded);
		OnItemExpansionChanged().Broadcast(Item, bIsExpanded);
	}

	void HandleGetChildren(ItemType Item, TArray<ItemType>& OutChildren) const
	{
		OnGetChildrenInternal(Item, OutChildren);
	}
};

//////////////////////////////////////////////////////////////////////////
// UListViewBase
//////////////////////////////////////////////////////////////////////////

/**
 * Bare-bones base class to make creating custom UListView widgets easier.
 * Child classes should also inherit from ITypedUMGListView<T> to get a basic public ListView API for free.
 *
 * Child classes will own the actual SListView<T> widgets, but this provides some boilerplate functionality for generating entries.
 * To generate a row for the child list, use GenerateTypedRow with the appropriate SObjectTableRow<T> type for your list
 *
 * Additionally, the entry widget class can be filtered for a particular class and interface with the EntryClass and EntryInterface metadata arguments
 * This can be specified either on the class directly (see below) or on any BindWidget UProperty
 *
 * Example:
 * class UMyUserWidget : public UUserWidget
 * {
 *		UPROPERTY(BindWidget, meta = (EntryClass = MyListEntryWidget))
 *		UListView* ListView_InventoryItems;
 * }
 *
 */
UCLASS(Abstract, NotBlueprintable, hidedropdown, meta = (EntryInterface = UserListEntry))
class UMG_API UListViewBase : public UWidget
{
	GENERATED_BODY()

public:
	UListViewBase(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
	virtual void ValidateCompiledDefaults(FCompilerResultsLog& CompileLog) const override;
#endif

	TSubclassOf<UUserWidget> GetEntryWidgetClass() const { return EntryWidgetClass; }

	/** Gets all of the list entry widgets currently being displayed by the list */
	UFUNCTION(BlueprintCallable, Category = ListViewBase)
	const TArray<UUserWidget*>& GetDisplayedEntryWidgets() const;

	/**
	 * Full regeneration of all entries in the list. Note that the entry UWidget instances will not be destroyed, but they will be released and re-generated.
	 * In other words, entry widgets will not receive Destruct/Construct events. They will receive OnEntryReleased and IUserObjectListEntry implementations will receive OnListItemObjectSet.
	 */
	UFUNCTION(BlueprintCallable, Category = ListViewBase)
	void RegenerateAllEntries();

	/** Scroll the entire list up to the first item */
	UFUNCTION(BlueprintCallable, Category = ListViewBase)
	void ScrollToTop();

	/** Scroll the entire list down to the bottom-most item */
	UFUNCTION(BlueprintCallable, Category = ListViewBase)
	void ScrollToBottom();

	DECLARE_EVENT_OneParam(UListView, FOnListEntryGenerated, UUserWidget&);
	FOnListEntryGenerated& OnEntryWidgetGenerated() { return OnListEntryGeneratedEvent; }

	DECLARE_EVENT_OneParam(UListView, FOnEntryWidgetReleased, UUserWidget&);
	FOnEntryWidgetReleased& OnEntryWidgetReleased() { return OnEntryWidgetReleasedEvent; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override final;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual void SynchronizeProperties() override;

	/** Implement in child classes to construct the actual ListView Slate widget */
	virtual TSharedRef<STableViewBase> RebuildListWidget();

	//@todo DanH: Should probably have the events for native & BP built in up here - need to update existing binds to UListView's version
	virtual void HandleListEntryHovered(UUserWidget& EntryWidget) {}
	virtual void HandleListEntryUnhovered(UUserWidget& EntryWidget) {}

	/** Expected to be bound to the actual ListView widget created by a child class */
	void HandleRowReleased(const TSharedRef<ITableRow>& Row);

	/**
	 * Sets the list to refresh on the next tick.
	 *
	 * Note that refreshing, from a list perspective, is limited to accounting for discrepancies between items and entries.
	 * In other words, it will only release entries that no longer have items and create entries for new items (or newly visible items).
	 *
	 * It does NOT account for changes within existing items - that is up to the item to announce for an entry to listen to as needed.
	 * This can be onerous to set up for simple cases, so it's also reasonable to call RegenerateAllEntries when changes within N list items need to be reflected.
	 */
	void RequestRefresh();

	template <typename WidgetEntryT = UUserWidget, typename ObjectTableRowT = SObjectTableRow<UObject*>>
	WidgetEntryT& GenerateTypedEntry(const TSharedRef<STableViewBase>& OwnerTable)
	{
		static_assert(TIsDerivedFrom<ObjectTableRowT, ITableRow>::IsDerived && TIsDerivedFrom<ObjectTableRowT, SObjectWidget>::IsDerived,
			"GenerateObjectTableRow can only be used to create SObjectWidget types that also inherit from ITableRow. See SObjectTableRow.");

		WidgetEntryT* ListEntryWidget = EntryWidgetPool.GetOrCreateInstance<WidgetEntryT>(*EntryWidgetClass,
			[this, &OwnerTable] (UUserWidget* WidgetObject, TSharedRef<SWidget> Content)
			{
				return SNew(ObjectTableRowT, OwnerTable, *WidgetObject)
					.OnHovered_UObject(this, &UListViewBase::HandleListEntryHovered)
					.OnUnhovered_UObject(this, &UListViewBase::HandleListEntryUnhovered)
					[
						Content
					];
			});
		check(ListEntryWidget);

		FinishGeneratingEntry(*ListEntryWidget);

		return *ListEntryWidget;
	}

#if WITH_EDITORONLY_DATA
	/**
	 * Called during design time to allow lists to generate preview entries via dummy data.
	 * Since that data could be of any type, the child has to be the one to generate them.
	 * 
	 * Expected to call RefreshDesignerItems<T> with the appropriate T for your underlying list
	 * @see UListView::OnRefreshDesignerItems for a usage example
	 */
	virtual void OnRefreshDesignerItems() {}

	/**
	 * Helper intended to be called by overrides of OnRefreshDesignerItems.
	 * @see UListView::OnRefreshDesignerItems for a usage example
	 */
	template <typename PlaceholderItemT>
	void RefreshDesignerItems(TArray<PlaceholderItemT>& ListItems, TFunctionRef<PlaceholderItemT()> CreateItemFunc)
	{
		bNeedsToCallRefreshDesignerItems = false;
		bool bRefresh = false;
		if (EntryWidgetClass && NumDesignerPreviewEntries > 0)
		{
			if (ListItems.Num() < NumDesignerPreviewEntries)
			{
				while (ListItems.Num() < NumDesignerPreviewEntries)
				{
					ListItems.Add(CreateItemFunc());
				}
				bRefresh = true;
			}
			else if (ListItems.Num() > NumDesignerPreviewEntries)
			{
				const int32 NumExtras = ListItems.Num() - NumDesignerPreviewEntries;
				ListItems.RemoveAtSwap(ListItems.Num() - (NumExtras + 1), NumExtras);
				bRefresh = true;
			}
		}
		else
		{
			ListItems.Reset();
			bRefresh = true;
		}

		if (bRefresh)
		{
			RequestRefresh();
		}
	}
#endif

private:
	void FinishGeneratingEntry(UUserWidget& GeneratedEntry);
	void HandleAnnounceGeneratedEntries();

private:

	// Note: Options for this property can be configured via class and property metadata. See class declaration comment above.
	/** The type of widget to create for each entry displayed in the list. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = ListEntries, meta = (DesignerRebuild, AllowPrivateAccess = true))
	TSubclassOf<UUserWidget> EntryWidgetClass;
	
	/** Called when a row widget is generated for a list item */
	UPROPERTY(BlueprintAssignable, Category = Events, meta = (DisplayName = "On Entry Generated"))
	FOnListEntryGeneratedDynamic BP_OnEntryGenerated;

	/** Called when a row widget is released by the list (i.e. when it no longer represents a list item) */
	UPROPERTY(BlueprintAssignable, Category = Events, meta = (DisplayName = "On Entry Released"))
	FOnListEntryReleasedDynamic BP_OnEntryReleased;

#if WITH_EDITORONLY_DATA
	bool bNeedsToCallRefreshDesignerItems = false;;

	/** The number of dummy item entry widgets to preview in the widget designer */
	UPROPERTY(EditAnywhere, Category = ListEntries, meta = (ClampMin = 0, ClampMax = 20))
	int32 NumDesignerPreviewEntries = 5;
#endif

	FUserWidgetPool EntryWidgetPool;
	FTimerHandle EntryGenAnnouncementTimerHandle;
	TArray<TWeakObjectPtr<UUserWidget>> GeneratedEntriesToAnnounce;

	FOnListEntryGenerated OnListEntryGeneratedEvent;
	FOnEntryWidgetReleased OnEntryWidgetReleasedEvent;

	TSharedPtr<STableViewBase> MyTableViewBase;

	friend class FListViewBaseDetails;
};


#define IMPLEMENT_TYPED_UMG_LIST(ItemType, ListPropertyName)	\
protected:	\
	virtual SListView<ItemType>* GetMyListView() const override { return ListPropertyName.Get(); }	\
private:	\
	friend class ITypedUMGListView<ItemType>;	\
	mutable FSimpleListItemEvent OnItemClickedEvent;	\
	mutable FSimpleListItemEvent OnItemDoubleClickedEvent;	\
	mutable FOnItemSelectionChanged OnItemSelectionChangedEvent;	\
	mutable FOnItemIsHoveredChanged OnItemIsHoveredChangedEvent;	\
	mutable FOnItemScrolledIntoView OnItemScrolledIntoViewEvent;	\
	mutable FOnItemExpansionChanged OnItemExpansionChangedEvent;	\
public:	\
	virtual FSimpleListItemEvent& OnItemClicked() const override { return OnItemClickedEvent; }	\
	virtual FSimpleListItemEvent& OnItemDoubleClicked() const override { return OnItemDoubleClickedEvent; }	\
	virtual FOnItemIsHoveredChanged& OnItemIsHoveredChanged() const override { return OnItemIsHoveredChangedEvent; }	\
	virtual FOnItemSelectionChanged& OnItemSelectionChanged() const override { return OnItemSelectionChangedEvent; }	\
	virtual FOnItemScrolledIntoView& OnItemScrolledIntoView() const override { return OnItemScrolledIntoViewEvent; }	\
	virtual FOnItemExpansionChanged& OnItemExpansionChanged() const override { return OnItemExpansionChangedEvent; }	