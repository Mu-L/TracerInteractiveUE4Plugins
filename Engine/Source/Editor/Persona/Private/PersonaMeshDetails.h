// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Visibility.h"
#include "Input/Reply.h"
#include "Widgets/SWidget.h"
#include "Widgets/SBoxPanel.h"
#include "EngineDefines.h"
#include "Engine/SkeletalMesh.h"
#include "PropertyHandle.h"
#include "IDetailCustomNodeBuilder.h"
#include "IDetailCustomization.h"
#include "Widgets/Input/SComboBox.h"

struct FAssetData;
class FDetailWidgetRow;
class FPersonaMeshDetails;
class IDetailChildrenBuilder;
class IDetailLayoutBuilder;
class IPersonaToolkit;
class SUniformGridPanel;
struct FSectionLocalizer;
class IDetailCategoryBuilder;
class IDetailGroup;

/**
 * Struct to uniquely identify clothing applied to a material section
 * Contains index into the ClothingAssets array and the submesh index.
 */
struct FClothAssetSubmeshIndex
{
	FClothAssetSubmeshIndex(int32 InAssetIndex, int32 InSubmeshIndex)
		:	AssetIndex(InAssetIndex)
		,	SubmeshIndex(InSubmeshIndex)
	{}

	int32 AssetIndex;
	int32 SubmeshIndex;

	bool operator==(const FClothAssetSubmeshIndex& Other) const
	{
		return (AssetIndex	== Other.AssetIndex 
			&& SubmeshIndex	== Other.SubmeshIndex
			);
	}
};

struct FClothingComboInfo
{
	/* Per-material clothing combo boxes, array size must be same to # of sections */
	TArray<TSharedPtr< class STextComboBox >>		ClothingComboBoxes;
	/* Clothing combo box strings */
	TArray<TSharedPtr<FString> >			ClothingComboStrings;
	/* Mapping from a combo box string to the asset and submesh it was generated from */
	TMap<FString, FClothAssetSubmeshIndex>	ClothingComboStringReverseLookup;
	/* The currently-selected index from each clothing combo box */
	TArray<int32>							ClothingComboSelectedIndices;
};

struct FSectionLocalizer
{
	FSectionLocalizer(int32 InLODIndex, int32 InSectionIndex)
		: LODIndex(InLODIndex)
		, SectionIndex(InSectionIndex)
	{}

	bool operator==(const FSectionLocalizer& Other) const
	{
		return (LODIndex == Other.LODIndex && SectionIndex == Other.SectionIndex);
	}

	bool operator!=(const FSectionLocalizer& Other) const
	{
		return !((*this) == Other);
	}

	int32 LODIndex;
	int32 SectionIndex;
};

class FPersonaMeshDetails : public IDetailCustomization
{
public:
	FPersonaMeshDetails(TSharedRef<class IPersonaToolkit> InPersonaToolkit);
	~FPersonaMeshDetails();

	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance(TWeakPtr<class IPersonaToolkit> InPersonaToolkit);

	/** IDetailCustomization interface */
	virtual void CustomizeDetails( IDetailLayoutBuilder& DetailLayout ) override;

private:

	FReply AddMaterialSlot();

	FText GetMaterialArrayText() const;

	/**
	 * Called by the material list widget when we need to get new materials for the list
	 *
	 * @param OutMaterials	Handle to a material list builder that materials should be added to
	 */
	void OnGetSectionsForView( class ISectionListBuilder& OutSections, int32 LODIndex );

	/**
	 * Called when a user drags a new material over a list item to replace it
	 *
	 * @param NewMaterial	The material that should replace the existing material
	 * @param PrevMaterial	The material that should be replaced
	 * @param SlotIndex		The index of the slot on the component where materials should be replaces
	 * @param bReplaceAll	If true all materials in the slot should be replaced not just ones using PrevMaterial
	 */
	void OnSectionChanged(int32 LODIndex, int32 SectionIndex, int32 NewMaterialSlotIndex, FName NewMaterialSlotName);

	/**
	* Called by the material list widget when we need to get new materials for the list
	*
	* @param OutMaterials	Handle to a material list builder that materials should be added to
	*/
	void OnGetMaterialsForArray(class IMaterialListBuilder& OutMaterials, int32 LODIndex);

	/**
	* Called when a user drags a new material over a list item to replace it
	*
	* @param NewMaterial	The material that should replace the existing material
	* @param PrevMaterial	The material that should be replaced
	* @param SlotIndex		The index of the slot on the component where materials should be replaces
	* @param bReplaceAll	If true all materials in the slot should be replaced not just ones using PrevMaterial
	*/
	void OnMaterialArrayChanged(UMaterialInterface* NewMaterial, UMaterialInterface* PrevMaterial, int32 SlotIndex, bool bReplaceAll, int32 LODIndex);

	
	/**
	 * Called by the material list widget on generating each name widget
	 *
	 * @param Material		The material that is being displayed
	 * @param SlotIndex		The index of the material slot
	 */
	TSharedRef<SWidget> OnGenerateCustomNameWidgetsForSection(int32 LodIndex, int32 SectionIndex);

	/**
	 * Called by the material list widget on generating each thumbnail widget
	 *
	 * @param Material		The material that is being displayed
	 * @param SlotIndex		The index of the material slot
	 */
	TSharedRef<SWidget> OnGenerateCustomSectionWidgetsForSection(int32 LODIndex, int32 SectionIndex);

	bool IsSectionEnabled(int32 LodIndex, int32 SectionIndex) const;
	EVisibility ShowEnabledSectionDetail(int32 LodIndex, int32 SectionIndex) const;
	EVisibility ShowDisabledSectionDetail(int32 LodIndex, int32 SectionIndex) const;
	void OnSectionEnabledChanged(int32 LodIndex, int32 SectionIndex, bool bEnable);

	TOptional<int8> GetSectionGenerateUpToValue(int32 LodIndex, int32 SectionIndex) const;
	void SetSectionGenerateUpToValue(int8 Value, int32 LodIndex, int32 SectionIndex);
	void SetSectionGenerateUpToValueCommitted(int8 Value, ETextCommit::Type CommitInfo, int32 LodIndex, int32 SectionIndex);
	EVisibility ShowSectionGenerateUpToSlider(int32 LodIndex, int32 SectionIndex) const;
	ECheckBoxState IsGenerateUpToSectionEnabled(int32 LodIndex, int32 SectionIndex) const;
	void OnSectionGenerateUpToChanged(ECheckBoxState NewState, int32 LodIndex, int32 SectionIndex);

	TSharedRef<SWidget> OnGenerateLodComboBoxForLodPicker();
	EVisibility LodComboBoxVisibilityForLodPicker() const;
	bool IsLodComboBoxEnabledForLodPicker() const;

	/*
	 * Generate the context menu to choose the LOD we will display the picker list
	*/
	TSharedRef<SWidget> OnGenerateLodMenuForLodPicker();
	FText GetCurrentLodName() const;
	FText GetCurrentLodTooltip() const;

	void SetCurrentLOD(int32 NewLodIndex);

	void UpdateLODCategoryVisibility() const;

	FText GetMaterialNameText(int32 MaterialIndex)const ;
	void OnMaterialNameCommitted(const FText& InValue, ETextCommit::Type CommitType, int32 MaterialIndex);

	FText GetOriginalImportMaterialNameText(int32 MaterialIndex)const;

	/**
	* Called by the material list widget on generating name side content
	*
	* @param Material		The material that is being displayed
	* @param MaterialIndex	The index of the material slot
	*/
	TSharedRef<SWidget> OnGenerateCustomNameWidgetsForMaterialArray(UMaterialInterface* Material, int32 MaterialIndex);
	
	/**
	* Called by the material list widget on generating each thumbnail widget
	*
	* @param Material		The material that is being displayed
	* @param MaterialIndex	The index of the material slot
	*/
	TSharedRef<SWidget> OnGenerateCustomMaterialWidgetsForMaterialArray(UMaterialInterface* Material, int32 MaterialIndex, int32 LODIndex);

	/* If the material list is dirty this function will return true */
	bool OnMaterialListDirty();

	bool CanDeleteMaterialSlot(int32 MaterialIndex) const;

	void OnDeleteMaterialSlot(int32 MaterialIndex);

	TSharedRef<SWidget> OnGetMaterialSlotUsedByMenuContent(int32 MaterialIndex);

	FText GetFirstMaterialSlotUsedBySection(int32 MaterialIndex) const;

	/**
	* Handler for check box display based on whether the material is highlighted
	*
	* @param MaterialIndex	The material index that is being selected
	*/
	ECheckBoxState IsMaterialSelected(int32 MaterialIndex) const;

	/**
	* Handler for changing highlight status on a material
	*
	* @param MaterialIndex	The material index that is being selected
	*/
	void OnMaterialSelectedChanged(ECheckBoxState NewState, int32 MaterialIndex);

	/**
	* Handler for check box display based on whether the material is isolated
	*
	* @param MaterialIndex	The material index that is being isolate
	*/
	ECheckBoxState IsIsolateMaterialEnabled(int32 MaterialIndex) const;

	/**
	* Handler for changing isolated status on a material
	*
	* @param MaterialIndex	The material index that is being isolate
	*/
	void OnMaterialIsolatedChanged(ECheckBoxState NewState, int32 MaterialIndex);


	/**
	 * Handler for check box display based on whether the material is highlighted
	 *
	 * @param SectionIndex	The material section that is being tested
	 */
	ECheckBoxState IsSectionSelected(int32 SectionIndex) const;

	/**
	 * Handler for changing highlight status on a material
	 *
	 * @param SectionIndex	The material section that is being tested
	 */
	void OnSectionSelectedChanged(ECheckBoxState NewState, int32 SectionIndex);

	/**
	* Handler for check box display based on whether the material is isolated
	*
	* @param SectionIndex	The material section that is being tested
	*/
	ECheckBoxState IsIsolateSectionEnabled(int32 SectionIndex) const;

	/**
	* Handler for changing isolated status on a material
	*
	* @param SectionIndex	The material section that is being tested
	*/
	void OnSectionIsolatedChanged(ECheckBoxState NewState, int32 SectionIndex);

	/**
	 * Handler for check box display based on whether the material has shadow casting enabled
	 *
	 * @param MaterialIndex	The material index which a section in a specific LOD model has
	 */
	ECheckBoxState IsShadowCastingEnabled(int32 MaterialIndex) const;

	/**
	 * Handler for changing shadow casting status on a material
	 *
	 * @param MaterialIndex	The material index which a section in a specific LOD model has
	 */
	void OnShadowCastingChanged(ECheckBoxState NewState, int32 MaterialIndex);

	/**
	* Handler for check box display based on whether this section does recalculate normal or not
	*
	* @param MaterialIndex	The material index which a section in a specific LOD model has
	*/
	ECheckBoxState IsRecomputeTangentEnabled(int32 MaterialIndex) const;

	/**
	* Handler for changing recalulate normal status on a material
	*
	* @param MaterialIndex	The material index which a section in a specific LOD model has
	*/
	void OnRecomputeTangentChanged(ECheckBoxState NewState, int32 MaterialIndex);

	/**
	* Handler for check box display based on whether the material has shadow casting enabled
	*
	* @param LODIndex	The LODIndex we want to change
	* @param SectionIndex	The SectionIndex we change the RecomputeTangent
	*/
	ECheckBoxState IsSectionShadowCastingEnabled(int32 LODIndex, int32 SectionIndex) const;

	/**
	* Handler for changing shadow casting status on a section
	*
	* @param LODIndex	The LODIndex we want to change
	* @param SectionIndex	The SectionIndex we change the RecomputeTangent
	*/
	void OnSectionShadowCastingChanged(ECheckBoxState NewState, int32 LODIndex, int32 SectionIndex);

	/**
	* Handler for check box display based on whether this section does recalculate normal or not
	*
	* @param LODIndex	The LODIndex we want to change
	* @param SectionIndex	The SectionIndex we change the RecomputeTangent
	*/
	ECheckBoxState IsSectionRecomputeTangentEnabled(int32 LODIndex, int32 SectionIndex) const;

	/**
	* Handler for changing recalulate normal status on a section
	*
	* @param LODIndex	The LODIndex we want to change
	* @param SectionIndex	The SectionIndex we change the RecomputeTangent
	*/
	void OnSectionRecomputeTangentChanged(ECheckBoxState NewState, int32 LODIndex, int32 SectionIndex);

	/**
	 * Handler for enabling delete button on materials
	 *
	 * @param SectionIndex - index of the section to check
	 */
	bool CanDeleteMaterialElement(int32 LODIndex, int32 SectionIndex) const;

	/**
	 * Handler for deleting material elements
	 * 
	 * @Param SectionIndex - material section to remove
	 */
	FReply OnDeleteButtonClicked(int32 LODIndex, int32 SectionIndex);

	/** Creates the UI for Current LOD panel */
	void AddLODLevelCategories(IDetailLayoutBuilder& DetailLayout);

	bool IsDuplicatedMaterialIndex(int32 LODIndex, int32 MaterialIndex);

	/** Get a material index from LOD index and section index */
	int32 GetMaterialIndex(int32 LODIndex, int32 SectionIndex) const;

	/** for LOD settings category */
	void CustomizeLODSettingsCategories(IDetailLayoutBuilder& DetailLayout);

	/** Called when a LOD is imported. Refreshes the UI. */
	void OnAssetPostLODImported(UObject* InObject, int32 InLODIndex);
	/** Called from the PersonalMeshDetails UI to import a LOD. */
	void OnImportLOD(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo, IDetailLayoutBuilder* DetailLayout);
	void UpdateLODNames();
	int32 GetLODCount() const;
	void OnLODCountChanged(int32 NewValue);
	void OnLODCountCommitted(int32 InValue, ETextCommit::Type CommitInfo);
	FText GetLODCountTooltip() const;
	FText GetLODImportedText(int32 LODIndex) const;

	FText GetMaterialSlotNameText(int32 MaterialIndex) const;

	/** apply LOD changes if the user modified LOD reduction settings */
	FReply OnApplyChanges();
	/** regenerate one specific LOD Index no dependencies*/
	void RegenerateOneLOD(int32 LODIndex, bool bReregisterComponent = true);
	/** regenerate the specific all LODs dependent of InLODIndex. This is not regenerating the InLODIndex*/
	void RegenerateDependentLODs(int32 LODIndex, bool bReregisterComponent = true);
	/** Apply specified LOD Index */
	FReply RegenerateLOD(int32 LODIndex);
	/** Removes the specified lod from the skeletal mesh */
	FReply RemoveOneLOD(int32 LODIndex);
	/** Remove Bones again */
	FReply RemoveBones(int32 LODIndex);
	/** hide properties which don't need to be showed to end users */
	void HideUnnecessaryProperties(IDetailLayoutBuilder& DetailLayout);

	// Handling functions for post process blueprint selection combo box
	void OnPostProcessBlueprintChanged(IDetailLayoutBuilder* DetailBuilder);
	FString GetCurrentPostProcessBlueprintPath() const;
	bool OnShouldFilterPostProcessBlueprint(const FAssetData& AssetData) const;
	void OnSetPostProcessBlueprint(const FAssetData& AssetData, TSharedRef<IPropertyHandle> BlueprintProperty);

	/** Access the persona toolkit ptr. It should always be valid in the lifetime of this customization */
	TSharedRef<IPersonaToolkit> GetPersonaToolkit() const { check(PersonaToolkitPtr.IsValid()); return PersonaToolkitPtr.Pin().ToSharedRef(); }
	bool HasValidPersonaToolkit() const { return PersonaToolkitPtr.IsValid(); }

	EVisibility GetOverrideUVDensityVisibililty() const;
	ECheckBoxState IsUVDensityOverridden(int32 MaterialIndex) const;
	void OnOverrideUVDensityChanged(ECheckBoxState NewState, int32 MaterialIndex);

	EVisibility GetUVDensityVisibility(int32 MaterialIndex, int32 UVChannelIndex) const;
	TOptional<float> GetUVDensityValue(int32 MaterialIndex, int32 UVChannelIndex) const;
	void SetUVDensityValue(float InDensity, ETextCommit::Type CommitType, int32 MaterialIndex, int32 UVChannelIndex);

	SVerticalBox::FSlot& GetUVDensitySlot(int32 MaterialIndex, int32 UVChannelIndex) const;

	// Used to control the type of reimport to do with a named parameter
	enum class EReimportButtonType : uint8
	{
		Reimport,
		ReimportWithNewFile
	};

	// Handler for reimport buttons in LOD details
	FReply OnReimportLodClicked(EReimportButtonType InReimportType, int32 InLODIndex);

	void OnCopySectionList(int32 LODIndex);
	bool OnCanCopySectionList(int32 LODIndex) const;
	void OnPasteSectionList(int32 LODIndex);

	void OnCopySectionItem(int32 LODIndex, int32 SectionIndex);
	bool OnCanCopySectionItem(int32 LODIndex, int32 SectionIndex) const;
	void OnPasteSectionItem(int32 LODIndex, int32 SectionIndex);

	void OnCopyMaterialList();
	bool OnCanCopyMaterialList() const;
	void OnPasteMaterialList();

	void OnCopyMaterialItem(int32 CurrentSlot);
	bool OnCanCopyMaterialItem(int32 CurrentSlot) const;
	void OnPasteMaterialItem(int32 CurrentSlot);

	void OnPreviewMeshChanged(USkeletalMesh* OldSkeletalMesh, USkeletalMesh* NewMesh);
	
	bool FilterOutBakePose(const struct FAssetData& AssetData, USkeleton* Skeleton) const;

	FText GetLODCustomModeNameContent(int32 LODIndex) const;
	ECheckBoxState IsLODCustomModeCheck(int32 LODIndex) const;
	void SetLODCustomModeCheck(ECheckBoxState NewState, int32 LODIndex);
	bool IsLODCustomModeEnable(int32 LODIndex) const;

	/** Gets the max LOD that can be set from the lod count slider (current num plus an interval) */
	TOptional<int32> GetLodSliderMaxValue() const;

	void CustomizeSkinWeightProfiles(IDetailLayoutBuilder& DetailLayout);
	TSharedRef<SWidget> CreateSkinWeightProfileMenuContent();
public:

	bool IsApplyNeeded() const;
	bool IsGenerateAvailable() const;
	void ApplyChanges();
	FText GetApplyButtonText() const;

private:
	// Container for the objects to display
	TWeakObjectPtr<USkeletalMesh> SkeletalMeshPtr;

	// Reference the persona toolkit
	TWeakPtr<class IPersonaToolkit> PersonaToolkitPtr;

	IDetailLayoutBuilder* MeshDetailLayout;

	/** LOD import options */
	TArray<TSharedPtr<FString> > LODNames;
	/** Helper value that corresponds to the 'Number of LODs' spinbox.*/
	int32 LODCount;

	/* This is to know if material are used by any LODs sections. */
	TMap<int32, TArray<FSectionLocalizer>> MaterialUsedMap;

	TArray<class IDetailCategoryBuilder*> LodCategories;
	IDetailCategoryBuilder* LodCustomCategory;

	bool CustomLODEditMode;
	TArray<bool> DetailDisplayLODs;

	/*
	 * Helper to keep the old GenerateUpTo slider value to register transaction correctly.
	 * The key is the union of LOD index and section index.
	 */
	TMap<int64, int8> OldGenerateUpToSliderValues;

	/*
	 * This prevent showing the delete material slot warning dialog more then once per editor session
	 */
	bool bDeleteWarningConsumed;

private:

	// info about clothing combo boxes for multiple LOD
	TArray<FClothingComboInfo>				ClothingComboLODInfos;
	TArray<int32> ClothingSelectedSubmeshIndices;

	// Menu entry for clothing dropdown
	struct FClothingEntry
	{
		// Asset index inside the mesh
		int32 AssetIndex;

		// LOD index inside the clothing asset
		int32 AssetLodIndex;

		// Pointer back to the asset for this clothing entry
		TWeakObjectPtr<UClothingAssetBase> Asset;
	};

	// Cloth combo box tracking for refreshes post-import/creation
	typedef SComboBox<TSharedPtr<FClothingEntry>> SClothComboBox;
	typedef TSharedPtr<SClothComboBox> SClothComboBoxPtr;
	TArray<SClothComboBoxPtr> ClothComboBoxes;

	// Clothing entries available to bind to the mesh
	TArray<TSharedPtr<FClothingEntry>> NewClothingAssetEntries;

	// Cached item in above array that is used as the "None" entry in the list
	TSharedPtr<FClothingEntry> ClothingNoneEntry;

	// Update the list of valid entries
	void UpdateClothingEntries();

	// Refreshes clothing combo boxes that are currently active
	void RefreshClothingComboBoxes();

	// Called as clothing combo boxes open to validate option entries
	void OnClothingComboBoxOpening();

	// Generate a widget for the clothing details panel
	TSharedRef<SWidget> OnGenerateWidgetForClothingEntry(TSharedPtr<FClothingEntry> InEntry);

	// Get the current text for the clothing selection combo box for the specified LOD and section
	FText OnGetClothingComboText(int32 InLodIdx, int32 InSectionIdx) const;

	// Callback when the clothing asset is changed
	void OnClothingSelectionChanged(TSharedPtr<FClothingEntry> InNewEntry, ESelectInfo::Type InSelectType, int32 BoxIndex, int32 InLodIdx, int32 InSectionIdx);

	// If the clothing details widget is editable
	bool IsClothingPanelEnabled() const;

	// Callback after the clothing details are changed
	void OnFinishedChangingClothingProperties(const FPropertyChangedEvent& Event, int32 InAssetIndex);

	/* Generate slate UI for Clothing category */
	void CustomizeClothingProperties(class IDetailLayoutBuilder& DetailLayout, class IDetailCategoryBuilder& ClothingFilesCategory);

	/* Generate each ClothingAsset array entry */
	void OnGenerateElementForClothingAsset( TSharedRef<IPropertyHandle> ElementProperty, int32 ElementIndex, IDetailChildrenBuilder& ChildrenBuilder, IDetailLayoutBuilder* DetailLayout );

	/* Make uniform grid widget for Apex details */
	TSharedRef<SUniformGridPanel> MakeClothingDetailsWidget(int32 AssetIndex) const;

	/* Opens dialog to add a new clothing asset */
	FReply OnOpenClothingFileClicked(IDetailLayoutBuilder* DetailLayout);

	/* Reimports a clothing asset */ 
	FReply OnReimportApexFileClicked(int32 AssetIndex, IDetailLayoutBuilder* DetailLayout);

	/* Removes a clothing asset */ 
	FReply OnRemoveApexFileClicked(int32 AssetIndex, IDetailLayoutBuilder* DetailLayout);

	/* Create LOD setting assets from current setting */
	FReply OnSaveLODSettings();

	/** LOD Settings Selected */
	void OnLODSettingsSelected(const FAssetData& AssetData);

	/** LOD Info editing is enabled? LODIndex == -1, then it just verifies if the asset exists */
	bool IsLODInfoEditingEnabled(int32 LODIndex) const;

	// Property handle used to determine if the VertexColorImportOverride property should be enabled.
	TSharedPtr<IPropertyHandle> VertexColorImportOptionHandle;

	// Property handle used during UI construction
	TSharedPtr<IPropertyHandle> VertexColorImportOverrideHandle;

	// Delegate implementation of FOnInstancedPropertyIteration used during DataImport UI construction
	void OnInstancedFbxSkeletalMeshImportDataPropertyIteration(IDetailCategoryBuilder& BaseCategory, IDetailGroup* PropertyGroup, TSharedRef<IPropertyHandle>& Property) const;

	// Delegate used at runtime to determine the state of the VertexOverrideColor property
	bool GetVertexOverrideColorEnabledState() const;
};
