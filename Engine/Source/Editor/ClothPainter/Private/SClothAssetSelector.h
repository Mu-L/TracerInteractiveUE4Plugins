// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SComboBox.h"
#include "EditorUndoClient.h"

struct FAssetData;
class USkeletalMesh;
class UClothingAssetCommon;
struct FClothPhysicalMeshData;
struct FPointWeightMap;

struct FClothingAssetListItem
{
	TWeakObjectPtr<UClothingAssetCommon> ClothingAsset;
};

struct FClothingMaskListItem
{
	FClothingMaskListItem()
		: LodIndex(INDEX_NONE)
		, MaskIndex(INDEX_NONE)
	{}

	FPointWeightMap* GetMask();
	FClothPhysicalMeshData* GetMeshData();
	USkeletalMesh* GetOwningMesh();

	TWeakObjectPtr<UClothingAssetCommon> ClothingAsset;
	int32 LodIndex;
	int32 MaskIndex;
};

typedef SListView<TSharedPtr<FClothingAssetListItem>> SAssetList;
typedef SListView<TSharedPtr<FClothingMaskListItem>> SMaskList;

DECLARE_DELEGATE_ThreeParams(FOnClothAssetSelectionChanged, TWeakObjectPtr<UClothingAssetCommon>, int32, int32);

class SClothAssetSelector : public SCompoundWidget, public FEditorUndoClient
{
public:

	SLATE_BEGIN_ARGS(SClothAssetSelector) {}
		SLATE_EVENT(FOnClothAssetSelectionChanged, OnSelectionChanged)
	SLATE_END_ARGS()

	~SClothAssetSelector();

	void Construct(const FArguments& InArgs, USkeletalMesh* InMesh);

	TWeakObjectPtr<UClothingAssetCommon> GetSelectedAsset() const;
	int32 GetSelectedLod() const;
	int32 GetSelectedMask() const;

	/** FEditorUndoClient interface */
	virtual void PostUndo(bool bSuccess) override;
	/** End FEditorUndoClient interface */

protected:

#if WITH_APEX_CLOTHING
	FReply OnImportApexFileClicked();
#endif

	/* Copies clothing setup from source SkelMesh */
	void OnCopyClothingAssetSelected(const FAssetData& AssetData);

	// Generate a drop-down for choosing the source skeletal mesh for copying cloth assets
	TSharedRef<SWidget> OnGenerateSkeletalMeshPickerForClothCopy();

	EVisibility GetAssetHeaderButtonTextVisibility() const;
	EVisibility GetMaskHeaderButtonTextVisibility() const;

	TSharedRef<SWidget> OnGetLodMenu();
	FText GetLodButtonText() const;

	TSharedRef<ITableRow> OnGenerateWidgetForClothingAssetItem(TSharedPtr<FClothingAssetListItem> InItem, const TSharedRef<STableViewBase>& OwnerTable);
	void OnAssetListSelectionChanged(TSharedPtr<FClothingAssetListItem> InSelectedItem, ESelectInfo::Type InSelectInfo);

	TSharedRef<ITableRow> OnGenerateWidgetForMaskItem(TSharedPtr<FClothingMaskListItem> InItem, const TSharedRef<STableViewBase>& OwnerTable);
	void OnMaskSelectionChanged(TSharedPtr<FClothingMaskListItem> InSelectedItem, ESelectInfo::Type InSelectInfo);

	// Mask manipulation
	FReply AddNewMask();
	bool CanAddNewMask() const;

	void OnRefresh();

	void RefreshAssetList();
	void RefreshMaskList();

	TOptional<float> GetCurrentKernelRadius() const;
	void OnCurrentKernelRadiusChanged(float InValue);
	void OnCurrentKernelRadiusCommitted(float InValue, ETextCommit::Type CommitType);
	bool CurrentKernelRadiusIsEnabled() const;

	ECheckBoxState GetCurrentUseMultipleInfluences() const;
	void OnCurrentUseMultipleInfluencesChanged(ECheckBoxState InValue);
	bool CurrentUseMultipleInfluencesIsEnabled() const;

	void OnClothingLodSelected(int32 InNewLod);

	// Setters for the list selections so we can handle list selections changing properly
	void SetSelectedAsset(TWeakObjectPtr<UClothingAssetCommon> InSelectedAsset);
	void SetSelectedLod(int32 InLodIndex, bool bRefreshMasks = true);
	void SetSelectedMask(int32 InMaskIndex);

	USkeletalMesh* Mesh;

	TSharedPtr<SButton> NewMaskButton;
	TSharedPtr<SAssetList> AssetList;
	TSharedPtr<SMaskList> MaskList;

	TSharedPtr<SHorizontalBox> AssetHeaderBox;
	TSharedPtr<SHorizontalBox> MaskHeaderBox;

	TArray<TSharedPtr<FClothingAssetListItem>> AssetListItems;
	TArray<TSharedPtr<FClothingMaskListItem>> MaskListItems;

	// Currently selected clothing asset, Lod Index and Mask index
	TWeakObjectPtr<UClothingAssetCommon> SelectedAsset;
	int32 SelectedLod;
	int32 SelectedMask;

	FOnClothAssetSelectionChanged OnSelectionChanged;

	// Handle for mesh event callback when clothing changes.
	FDelegateHandle MeshClothingChangedHandle;
};
