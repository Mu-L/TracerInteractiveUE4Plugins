// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Misc/NotifyHook.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Views/SListView.h"
#include "Framework/Commands/UICommandList.h"
#include "UObject/WeakObjectPtr.h"

#include "VariantManager.h"

class FExtender;
class FVariantManager;
class UVariant;
class FVariantManagerDisplayNode;
class SVariantManagerNodeTreeView;
class FVariantManagerDisplayNode;
class SVariantManagerActorListView;
class FVariantManagerPropertyNameNode;
class FVariantManagerPropertyNode;
class ITableRow;
class STableViewBase;
class SSplitter;
class FTransactionObjectEvent;
struct FSlateImageBrush;
enum class EMapChangeType : uint8;

namespace VariantManagerLayoutConstants
{
	/** The amount to indent child nodes of the layout tree */
	const float IndentAmount = 10.0f;

	/** Height of each folder node */
	const float FolderNodeHeight = 20.0f;

	/** Height of each object node */
	const float ObjectNodeHeight = 20.0f;

	/** Height of each section area if there are no sections (note: section areas may be larger than this if they have children. This is the height of a section area with no children or all children hidden) */
	const float SectionAreaDefaultHeight = 15.0f;

	/** Height of each key area */
	const float KeyAreaHeight = 15.0f;

	/** Height of each category node */
	const float CategoryNodeHeight = 15.0f;
}

// Convenience struct to save/load how the user configured the main splitters
struct FSplitterValues
{
	float VariantColumn = 0.25f;
	float ActorColumn = 0.25f;
	float PropertyNameColumn = 0.25f;
	float PropertyValueColumn = 0.25f;

	FSplitterValues(){};
	FSplitterValues(FString& InSerialized);
	FString ToString();
};

// Replica of FDetailColumnSizeData used by DetailViews
struct FPropertyColumnSizeData
{
	TAttribute<float> LeftColumnWidth;
	TAttribute<float> RightColumnWidth;
	SSplitter::FOnSlotResized OnWidthChanged;

	void SetColumnWidth(float InWidth) { OnWidthChanged.ExecuteIfBound(InWidth); }
};

class SVariantManager
	: public SCompoundWidget
	, public FNotifyHook
{
public:

	DECLARE_DELEGATE_OneParam(FOnToggleBoolOption, bool)
	SLATE_BEGIN_ARGS(SVariantManager)
	{ }
		/** Extender to use for the add menu. */
		SLATE_ARGUMENT(TSharedPtr<FExtender>, AddMenuExtender)

		/** Extender to use for the toolbar. */
		SLATE_ARGUMENT(TSharedPtr<FExtender>, ToolbarExtender)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<FVariantManager> InVariantManager);
	~SVariantManager();

	void CreateCommandBindings();

	TSharedPtr<FUICommandList> GetVariantTreeCommandBindings() const
	{
		return VariantTreeCommandBindings;
	}

	TSharedPtr<FUICommandList> GetActorListCommandBindings() const
	{
		return ActorListCommandBindings;
	}

	TSharedPtr<FUICommandList> GetPropertyListCommandBindings() const
	{
		return PropertyListCommandBindings;
	}

	// Commands
	void AddEditorSelectedActorsToVariant();
	bool CanAddEditorSelectedActorsToVariant();

	void CreateNewVariantSet();
	bool CanCreateNewVariantSet();

	void CutSelectionVariantTree();
	void CopySelectionVariantTree();
	void PasteSelectionVariantTree();
	void DeleteSelectionVariantTree();
	void DuplicateSelectionVariantTree();
	void RenameSelectionVariantTree();

	bool CanCutVariantTree();
	bool CanCopyVariantTree();
	bool CanPasteVariantTree();
	bool CanDeleteVariantTree();
	bool CanDuplicateVariantTree();
	bool CanRenameVariantTree();

	void CutSelectionActorList();
	void CopySelectionActorList();
	void PasteSelectionActorList();
	void DeleteSelectionActorList();
	void DuplicateSelectionActorList();
	void RenameSelectionActorList();

	bool CanCutActorList();
	bool CanCopyActorList();
	bool CanPasteActorList();
	bool CanDeleteActorList();
	bool CanDuplicateActorList();
	bool CanRenameActorList();

	void SwitchOnSelectedVariant();
	void CreateThumbnail();
	void LoadThumbnail();
	void ClearThumbnail();

	bool CanSwitchOnVariant();
	bool CanCreateThumbnail();
	bool CanLoadThumbnail();
	bool CanClearThumbnail();

	void CaptureNewPropertiesFromSelectedActors();
	bool CanCaptureNewPropertiesFromSelectedActors();

	void AddFunctionCaller();
	bool CanAddFunctionCaller();

	void RebindToSelectedActor();
	bool CanRebindToSelectedActor();

	void RemoveActorBindings();
	bool CanRemoveActorBindings();

	void ApplyProperty();
	void RecordProperty();
	void RemoveCapture();
	void CallDirectorFunction();
	void RemoveDirectorFunctionCaller();

	bool CanApplyProperty();
	bool CanRecordProperty();
	bool CanRemoveCapture();
	bool CanCallDirectorFunction();
	bool CanRemoveDirectorFunctionCaller();

	void SwitchOnVariant(UVariant* Variant);

	void GetSelectedBindingAndEditorActor(UVariantObjectBinding*& OutSelectedBinding, UObject*& OutSelectedObject);

	// Sorts display nodes based on their order on the screen
	// Can be used to sort selected nodes
	void SortDisplayNodes(TArray<TSharedRef<FVariantManagerDisplayNode>>& DisplayNodes);

	TSharedRef<SWidget> MakeAddButton();
	FPropertyColumnSizeData& GetPropertyColumnSizeData()
	{
		return ColumnSizeData;
	}

	TSharedRef<ITableRow> MakeCapturedPropertyRow(TSharedPtr<FVariantManagerPropertyNode> Item, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedPtr<SWidget> OnPropertyListContextMenuOpening();

	void OnActorNodeSelectionChanged();

	// These completely refresh the data and the view for each display
	void RefreshVariantTree();
	void RefreshActorList();
	void RefreshPropertyList();
	void UpdatePropertyDefaults();

	void OnBlueprintCompiled();
	void OnMapChanged(UWorld* World, EMapChangeType MapChangeType);

	virtual bool SupportsKeyboardFocus() const override
	{
		return true;
	}

	void OnOutlinerSearchChanged(const FText& Filter);

	virtual void OnDragEnter( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent ) override;
	virtual void OnDragLeave( const FDragDropEvent& DragDropEvent ) override;
	virtual FReply OnDragOver( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent ) override;
	virtual FReply OnDrop( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent ) override;
	virtual FReply OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent ) override;
	virtual void OnFocusChanging( const FWeakWidgetPath& PreviousFocusPath, const FWidgetPath& NewWidgetPath, const FFocusEvent& InFocusEvent ) override;

	FReply OnAddVariantSetClicked();
	FReply OnSummonAddActorMenu();

	// Callbacks for ColumnSizeData
	float OnGetLeftColumnWidth() const { return 1.0f - RightPropertyColumnWidth; }
	float OnGetRightColumnWidth() const { return RightPropertyColumnWidth; }
	void OnSetColumnWidth(float InWidth) { RightPropertyColumnWidth = InWidth; }

	void OnObjectTransacted(UObject* Object, const class FTransactionObjectEvent& Event);
	void OnObjectPropertyChanged(UObject* Object, struct FPropertyChangedEvent& Event);
	void OnPreObjectPropertyChanged(UObject* Object, const class FEditPropertyChain& PropChain);
	void OnPieEvent(bool bIsSimulating);
	void OnEditorSelectionChanged(UObject* NewSelection);

	void ReorderPropertyNodes(const TArray<TSharedPtr<FVariantManagerPropertyNode>>& TheseNodes, TSharedPtr<FVariantManagerPropertyNode> Pivot, EItemDropZone RelativePosition);

private:
	TWeakPtr<FVariantManager> VariantManagerPtr;

	TSharedPtr<SVariantManagerNodeTreeView> NodeTreeView;

	TSharedPtr<SVariantManagerActorListView> ActorListView;
	TArray<TSharedRef<FVariantManagerDisplayNode>> DisplayedActors;

	TSharedPtr<SListView<TSharedPtr<FVariantManagerPropertyNode>>> CapturedPropertyListView;
	TArray<TSharedPtr<FVariantManagerPropertyNode>> DisplayedPropertyNodes;

	// We use paths here to avoid having to check if the bindings are resolved
	TSet<FString> CachedSelectedActorPaths;
	TSet<FString> CachedDisplayedActorPaths;
	TSet<FString> CachedAllActorPaths;

	TSharedPtr<SScrollBar> ScrollBar;

	TArray<TSharedPtr<class IPropertyChangeListener>> PropertyChangeListeners;

	TSharedPtr<FUICommandList> VariantTreeCommandBindings;
	TSharedPtr<FUICommandList> ActorListCommandBindings;
	TSharedPtr<FUICommandList> PropertyListCommandBindings;

	bool bAutoCaptureProperties = false;

	// Mirrors detailview, its used by all splitters in the column, so that they move in sync
	FPropertyColumnSizeData ColumnSizeData;
	float RightPropertyColumnWidth;

	FDelegateHandle OnObjectTransactedHandle;
	FDelegateHandle OnBlueprintCompiledHandle;
	FDelegateHandle OnMapChangedHandle;
	FDelegateHandle OnObjectPropertyChangedHandle;
	FDelegateHandle OnPreObjectPropertyChangedHandle;
	FDelegateHandle OnBeginPieHandle;
	FDelegateHandle OnEndPieHandle;
	FDelegateHandle OnEditorSelectionChangedHandle;

	// We keep track of this to remember splitter values between loads
	TSharedPtr<SSplitter> MainSplitter;

	// TODO: Make separate VariantManagerStyle
	TSharedPtr<FSlateImageBrush> RecordButtonBrush;

	struct FCachedPropertyPath
	{
		UObject* Object;
		FProperty* ParentProperty;
		FProperty* ChildProperty;
		AActor* TargetActor;
		FString Path;
	};

	// Structures used to optimize construction and usage of property paths related to auto-expose,
	// as we must use the pre- and post-property-changed events in combination
	TMap<int32, SVariantManager::FCachedPropertyPath> CachedPropertyPaths;
	TArray<SVariantManager::FCachedPropertyPath> CachedPropertyPathStack;

	bool bRespondToEditorSelectionEvents = true;
};
