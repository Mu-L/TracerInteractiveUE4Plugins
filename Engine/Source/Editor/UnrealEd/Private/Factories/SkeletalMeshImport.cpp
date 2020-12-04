// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SkeletalMeshImport.cpp: Skeletal mesh import code.
=============================================================================*/

#include "Factories/FbxSkeletalMeshImportData.h"

#include "ClothingAssetBase.h"
#include "CoreMinimal.h"
#include "EditorFramework/ThumbnailInfo.h"
#include "Engine/AssetUserData.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "FbxImporter.h"
#include "LODUtilities.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FbxErrors.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "SkelImport.h"
#include "UObject/MetaData.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogSkeletalMeshImport, Log, All);

#define LOCTEXT_NAMESPACE "SkeletalMeshImport"

namespace SkeletalMeshHelperImpl
{
	/** Check that root bone is the same, and that any bones that are common have the correct parent. */
	bool SkeletonsAreCompatible(const FReferenceSkeleton& NewSkel, const FReferenceSkeleton& ExistSkel, bool bFailNoError);

	bool SkeletalMeshIsUsingMaterialSlotNameWorkflow(UAssetImportData* AssetImportData);

	void SaveSkeletalMeshLODModelSections(USkeletalMesh* SourceSkeletalMesh, TSharedPtr<FExistingSkelMeshData>& ExistingMeshDataPtr, int32 LodIndex, bool bSaveNonReducedMeshData);

	void SaveSkeletalMeshMaterialNameWorkflowData(TSharedPtr<FExistingSkelMeshData>& ExistingMeshDataPtr, const USkeletalMesh* SourceSkeletalMesh);

	void SaveSkeletalMeshAssetUserData(TSharedPtr<FExistingSkelMeshData>& ExistingMeshDataPtr, const TArray<UAssetUserData*>* UserData);

	void RestoreDependentLODs(const TSharedPtr<const FExistingSkelMeshData>& MeshData, USkeletalMesh* SkeletalMesh);

	void RestoreLODInfo(const TSharedPtr<const FExistingSkelMeshData>& MeshData, USkeletalMesh* SkeletalMesh, int32 LodIndex);

	void RestoreMaterialNameWorkflowSection(const TSharedPtr<const FExistingSkelMeshData>& MeshData, USkeletalMesh* SkeletalMesh, int32 LodIndex, TArray<int32>& RemapMaterial, bool bMaterialReset);
}

bool SkeletalMeshHelperImpl::SkeletonsAreCompatible(const FReferenceSkeleton& NewSkel, const FReferenceSkeleton& ExistSkel, bool bFailNoError)
{
	if (NewSkel.GetBoneName(0) != ExistSkel.GetBoneName(0))
	{
		if (!bFailNoError)
		{
			UnFbx::FFbxImporter* FFbxImporter = UnFbx::FFbxImporter::GetInstance();
			FFbxImporter->AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Error, FText::Format(LOCTEXT("MeshHasDifferentRoot", "Root Bone is '{0}' instead of '{1}'.\nDiscarding existing LODs."),
				FText::FromName(NewSkel.GetBoneName(0)), FText::FromName(ExistSkel.GetBoneName(0)))), FFbxErrors::SkeletalMesh_DifferentRoots);
		}
		return false;
	}

	for (int32 i = 1; i < NewSkel.GetRawBoneNum(); i++)
	{
		// See if bone is in both skeletons.
		int32 NewBoneIndex = i;
		FName NewBoneName = NewSkel.GetBoneName(NewBoneIndex);
		int32 BBoneIndex = ExistSkel.FindBoneIndex(NewBoneName);

		// If it is, check parents are the same.
		if (BBoneIndex != INDEX_NONE)
		{
			FName NewParentName = NewSkel.GetBoneName(NewSkel.GetParentIndex(NewBoneIndex));
			FName ExistParentName = ExistSkel.GetBoneName(ExistSkel.GetParentIndex(BBoneIndex));

			if (NewParentName != ExistParentName)
			{
				if (!bFailNoError)
				{
					UnFbx::FFbxImporter* FFbxImporter = UnFbx::FFbxImporter::GetInstance();
					FFbxImporter->AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Error, FText::Format(LOCTEXT("MeshHasDifferentRoot", "Root Bone is '{0}' instead of '{1}'.\nDiscarding existing LODs."),
						FText::FromName(NewBoneName), FText::FromName(NewParentName))), FFbxErrors::SkeletalMesh_DifferentRoots);
				}
				return false;
			}
		}
	}

	return true;
}

/**
* Process and fill in the mesh Materials using the raw binary import data
*
* @param Materials - [out] array of materials to update
* @param ImportData - raw binary import data to process
*/
void SkeletalMeshHelper::ProcessImportMeshMaterials(TArray<FSkeletalMaterial>& Materials, FSkeletalMeshImportData& ImportData)
{
	TArray <SkeletalMeshImportData::FMaterial>&	ImportedMaterials = ImportData.Materials;

	// If direct linkup of materials is requested, try to find them here - to get a texture name from a 
	// material name, cut off anything in front of the dot (beyond are special flags).
	Materials.Empty();
	int32 SkinOffset = INDEX_NONE;
	for (int32 MatIndex = 0; MatIndex < ImportedMaterials.Num(); ++MatIndex)
	{
		const SkeletalMeshImportData::FMaterial& ImportedMaterial = ImportedMaterials[MatIndex];

		UMaterialInterface* Material = NULL;
		FString MaterialNameNoSkin = ImportedMaterial.MaterialImportName;
		if (ImportedMaterial.Material.IsValid())
		{
			Material = ImportedMaterial.Material.Get();
		}
		else
		{
			const FString& MaterialName = ImportedMaterial.MaterialImportName;
			MaterialNameNoSkin = MaterialName;
			Material = FindObject<UMaterialInterface>(ANY_PACKAGE, *MaterialName);
			if (Material == nullptr)
			{
				SkinOffset = MaterialName.Find(TEXT("_skin"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
				if (SkinOffset != INDEX_NONE)
				{
					FString SkinXXNumber = MaterialName.Right(MaterialName.Len() - (SkinOffset + 1)).RightChop(4);
					if (SkinXXNumber.IsNumeric())
					{
						MaterialNameNoSkin = MaterialName.LeftChop(MaterialName.Len() - SkinOffset);
						Material = FindObject<UMaterialInterface>(ANY_PACKAGE, *MaterialNameNoSkin);
					}
				}
			}
		}

		const bool bEnableShadowCasting = true;
		Materials.Add(FSkeletalMaterial(Material, bEnableShadowCasting, false, Material != nullptr ? Material->GetFName() : FName(*MaterialNameNoSkin), FName(*(ImportedMaterial.MaterialImportName))));
	}

	int32 NumMaterialsToAdd = FMath::Max<int32>(ImportedMaterials.Num(), ImportData.MaxMaterialIndex + 1);

	// Pad the material pointers
	while (NumMaterialsToAdd > Materials.Num())
	{
		Materials.Add(FSkeletalMaterial(NULL, true, false, NAME_None, NAME_None));
	}
}

/**
* Process and fill in the mesh ref skeleton bone hierarchy using the raw binary import data
*
* @param RefSkeleton - [out] reference skeleton hierarchy to update
* @param SkeletalDepth - [out] depth of the reference skeleton hierarchy
* @param ImportData - raw binary import data to process
* @return true if the operation completed successfully
*/
bool SkeletalMeshHelper::ProcessImportMeshSkeleton(const USkeleton* SkeletonAsset, FReferenceSkeleton& RefSkeleton, int32& SkeletalDepth, FSkeletalMeshImportData& ImportData)
{
	TArray <SkeletalMeshImportData::FBone>&	RefBonesBinary = ImportData.RefBonesBinary;

	// Setup skeletal hierarchy + names structure.
	RefSkeleton.Empty();

	FReferenceSkeletonModifier RefSkelModifier(RefSkeleton, SkeletonAsset);

	// Digest bones to the serializable format.
	for (int32 b = 0; b < RefBonesBinary.Num(); b++)
	{
		const SkeletalMeshImportData::FBone & BinaryBone = RefBonesBinary[b];
		const FString BoneName = FSkeletalMeshImportData::FixupBoneName(BinaryBone.Name);
		const FMeshBoneInfo BoneInfo(FName(*BoneName, FNAME_Add), BinaryBone.Name, BinaryBone.ParentIndex);
		const FTransform BoneTransform(BinaryBone.BonePos.Transform);

		if (RefSkeleton.FindRawBoneIndex(BoneInfo.Name) != INDEX_NONE)
		{
			UnFbx::FFbxImporter* FFbxImporter = UnFbx::FFbxImporter::GetInstance();
			FFbxImporter->AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Error, FText::Format(LOCTEXT("SkeletonHasDuplicateBones", "Skeleton has non-unique bone names.\nBone named '{0}' encountered more than once."), FText::FromName(BoneInfo.Name))), FFbxErrors::SkeletalMesh_DuplicateBones);
			return false;
		}

		RefSkelModifier.Add(BoneInfo, BoneTransform);
	}

	// Add hierarchy index to each bone and detect max depth.
	SkeletalDepth = 0;

	TArray<int32> SkeletalDepths;
	SkeletalDepths.Empty(RefBonesBinary.Num());
	SkeletalDepths.AddZeroed(RefBonesBinary.Num());
	for (int32 b = 0; b < RefSkeleton.GetRawBoneNum(); b++)
	{
		int32 Parent = RefSkeleton.GetRawParentIndex(b);
		int32 Depth = 1.0f;

		SkeletalDepths[b] = 1.0f;
		if (Parent != INDEX_NONE)
		{
			Depth += SkeletalDepths[Parent];
		}
		if (SkeletalDepth < Depth)
		{
			SkeletalDepth = Depth;
		}
		SkeletalDepths[b] = Depth;
	}

	return true;
}

/**
* Process and update the vertex Influences using the raw binary import data
*
* @param ImportData - raw binary import data to process
*/
void SkeletalMeshHelper::ProcessImportMeshInfluences(FSkeletalMeshImportData& ImportData, const FString& SkeletalMeshName)
{
	FLODUtilities::ProcessImportMeshInfluences(ImportData.Wedges.Num(), ImportData.Influences, SkeletalMeshName);
}

bool SkeletalMeshHelperImpl::SkeletalMeshIsUsingMaterialSlotNameWorkflow(UAssetImportData* AssetImportData)
{
	UFbxSkeletalMeshImportData* ImportData = Cast<UFbxSkeletalMeshImportData>(AssetImportData);
	if (ImportData == nullptr || ImportData->ImportMaterialOriginalNameData.Num() <= 0)
	{
		return false;
	}
	bool AllNameAreNone = true;
	for (FName ImportMaterialName : ImportData->ImportMaterialOriginalNameData)
	{
		if (ImportMaterialName != NAME_None)
		{
			AllNameAreNone = false;
			break;
		}
	}
	return !AllNameAreNone;
}

void SkeletalMeshHelperImpl::SaveSkeletalMeshLODModelSections(USkeletalMesh* SourceSkeletalMesh, TSharedPtr<FExistingSkelMeshData>& ExistingMeshDataPtr, int32 LodIndex, bool bSaveNonReducedMeshData)
{
	const FSkeletalMeshModel* SourceMeshModel = SourceSkeletalMesh->GetImportedModel();
	const FSkeletalMeshLODModel* SourceLODModel = &SourceMeshModel->LODModels[LodIndex];
	FSkeletalMeshLODModel OriginalLODModel;

	if (bSaveNonReducedMeshData && (SourceMeshModel->OriginalReductionSourceMeshData.IsValidIndex(LodIndex) && !SourceMeshModel->OriginalReductionSourceMeshData[LodIndex]->IsEmpty()))
	{
		TMap<FString, TArray<FMorphTargetDelta>> TempLODMorphTargetData;
		//Get the before reduce LODModel, this lod model contain all the possible sections
		SourceMeshModel->OriginalReductionSourceMeshData[LodIndex]->LoadReductionData(OriginalLODModel, TempLODMorphTargetData, SourceSkeletalMesh);
		//If there was section that was remove by the reduction (Disabled in the original data, zero triangle after reduction, GenerateUpTo settings...),
		//we have to use the original section data and apply the section data that was modified after the reduction
		if (OriginalLODModel.Sections.Num() > SourceLODModel->Sections.Num())
		{
			TArray<bool> OriginalMatched;
			OriginalMatched.AddZeroed(OriginalLODModel.Sections.Num());
			//Now apply the after reduce settings change, but we need to match the section since there can be reduced one
			for (int32 ReduceSectionIndex = 0; ReduceSectionIndex < SourceLODModel->Sections.Num(); ++ReduceSectionIndex)
			{
				const FSkelMeshSection& ReduceSection = SourceLODModel->Sections[ReduceSectionIndex];
				for (int32 OriginalSectionIndex = 0; OriginalSectionIndex < OriginalLODModel.Sections.Num(); ++OriginalSectionIndex)
				{
					if (OriginalMatched[OriginalSectionIndex])
					{
						continue;
					}
					FSkelMeshSection& OriginalSection = OriginalLODModel.Sections[OriginalSectionIndex];
					if ((OriginalSection.bDisabled) || (OriginalSection.GenerateUpToLodIndex != INDEX_NONE && OriginalSection.GenerateUpToLodIndex < LodIndex))
					{
						continue;
					}

					if (ReduceSection.MaterialIndex == OriginalSection.MaterialIndex)
					{
						OriginalMatched[OriginalSectionIndex] = true;
						OriginalSection.bDisabled = ReduceSection.bDisabled;
						OriginalSection.bCastShadow = ReduceSection.bCastShadow;
						OriginalSection.bRecomputeTangent = ReduceSection.bRecomputeTangent;
						OriginalSection.RecomputeTangentsVertexMaskChannel = ReduceSection.RecomputeTangentsVertexMaskChannel;
						OriginalSection.GenerateUpToLodIndex = ReduceSection.GenerateUpToLodIndex;
						break;
					}
				}
			}
			//Set the unmatched original section data using the current UserSectionsData so we keep the user changes
			for (int32 OriginalSectionIndex = 0; OriginalSectionIndex < OriginalLODModel.Sections.Num(); ++OriginalSectionIndex)
			{
				if (OriginalMatched[OriginalSectionIndex])
				{
					continue;
				}
				FSkelMeshSection& OriginalSection = OriginalLODModel.Sections[OriginalSectionIndex];
				if (const FSkelMeshSourceSectionUserData* ReduceUserSectionData = SourceLODModel->UserSectionsData.Find(OriginalSection.OriginalDataSectionIndex))
				{
					OriginalSection.bDisabled = ReduceUserSectionData->bDisabled;
					OriginalSection.bCastShadow = ReduceUserSectionData->bCastShadow;
					OriginalSection.bRecomputeTangent = ReduceUserSectionData->bRecomputeTangent;
					OriginalSection.RecomputeTangentsVertexMaskChannel = ReduceUserSectionData->RecomputeTangentsVertexMaskChannel;
					OriginalSection.GenerateUpToLodIndex = ReduceUserSectionData->GenerateUpToLodIndex;
				}
			}
			//Use the OriginalLODModel
			SourceLODModel = &OriginalLODModel;
		}
	}
	ExistingMeshDataPtr->ExistingImportMeshLodSectionMaterialData.AddZeroed();
	check(ExistingMeshDataPtr->ExistingImportMeshLodSectionMaterialData.IsValidIndex(LodIndex));

	for (const FSkelMeshSection& CurrentSection : SourceLODModel->Sections)
	{
		int32 SectionMaterialIndex = CurrentSection.MaterialIndex;
		bool SectionCastShadow = CurrentSection.bCastShadow;
		bool SectionRecomputeTangents = CurrentSection.bRecomputeTangent;
		ESkinVertexColorChannel RecomputeTangentsVertexMaskChannel = CurrentSection.RecomputeTangentsVertexMaskChannel;
		int32 GenerateUpTo = CurrentSection.GenerateUpToLodIndex;
		bool bDisabled = CurrentSection.bDisabled;
		bool bBoneChunkedSection = CurrentSection.ChunkedParentSectionIndex != INDEX_NONE;
		//Save all the sections, even the chunked sections
		if (ExistingMeshDataPtr->ExistingImportMaterialOriginalNameData.IsValidIndex(SectionMaterialIndex))
		{
			ExistingMeshDataPtr->ExistingImportMeshLodSectionMaterialData[LodIndex].Emplace(ExistingMeshDataPtr->ExistingImportMaterialOriginalNameData[SectionMaterialIndex], SectionCastShadow, SectionRecomputeTangents, RecomputeTangentsVertexMaskChannel, GenerateUpTo, bDisabled);
		}
	}
}

void SkeletalMeshHelperImpl::SaveSkeletalMeshMaterialNameWorkflowData(TSharedPtr<FExistingSkelMeshData>& ExistingMeshDataPtr, const USkeletalMesh* SourceSkeletalMesh)
{
	const UFbxSkeletalMeshImportData* ImportData = Cast<UFbxSkeletalMeshImportData>(SourceSkeletalMesh->AssetImportData);
	if (!ImportData)
	{
		return;
	}

	for (int32 ImportMaterialOriginalNameDataIndex = 0; ImportMaterialOriginalNameDataIndex < ImportData->ImportMaterialOriginalNameData.Num(); ++ImportMaterialOriginalNameDataIndex)
	{
		FName MaterialName = ImportData->ImportMaterialOriginalNameData[ImportMaterialOriginalNameDataIndex];
		ExistingMeshDataPtr->LastImportMaterialOriginalNameData.Add(MaterialName);
	}

	for (int32 LodIndex = 0; LodIndex < ImportData->ImportMeshLodData.Num(); ++LodIndex)
	{
		ExistingMeshDataPtr->LastImportMeshLodSectionMaterialData.AddZeroed();
		const FImportMeshLodSectionsData &ImportMeshLodSectionsData = ImportData->ImportMeshLodData[LodIndex];
		for (int32 SectionIndex = 0; SectionIndex < ImportMeshLodSectionsData.SectionOriginalMaterialName.Num(); ++SectionIndex)
		{
			FName MaterialName = ImportMeshLodSectionsData.SectionOriginalMaterialName[SectionIndex];
			ExistingMeshDataPtr->LastImportMeshLodSectionMaterialData[LodIndex].Add(MaterialName);
		}
	}
}

void SkeletalMeshHelperImpl::SaveSkeletalMeshAssetUserData(TSharedPtr<FExistingSkelMeshData>& ExistingMeshDataPtr, const TArray<UAssetUserData*>* UserData)
{
	if (!UserData)
	{
		return;
	}

	for (int32 Idx = 0; Idx < UserData->Num(); Idx++)
	{
		if ((*UserData)[Idx] != nullptr)
		{
			UAssetUserData* DupObject = (UAssetUserData*)StaticDuplicateObject((*UserData)[Idx], GetTransientPackage());
			bool bAddDupToRoot = !(DupObject->IsRooted());
			if (bAddDupToRoot)
			{
				DupObject->AddToRoot();
			}
			ExistingMeshDataPtr->ExistingAssetUserData.Add(DupObject, bAddDupToRoot);
		}
	}
}

TSharedPtr<FExistingSkelMeshData> SkeletalMeshHelper::SaveExistingSkelMeshData(USkeletalMesh* SourceSkeletalMesh, bool bSaveMaterials, int32 ReimportLODIndex)
{
	using namespace SkeletalMeshHelperImpl;

	if (!SourceSkeletalMesh)
	{
		return TSharedPtr<FExistingSkelMeshData>();
	}

	const int32 SafeReimportLODIndex = ReimportLODIndex < 0 ? 0 : ReimportLODIndex;
	TSharedPtr<FExistingSkelMeshData> ExistingMeshDataPtr(MakeShared<FExistingSkelMeshData>());

	//Save the package UMetaData
	ExistingMeshDataPtr->ExistingUMetaDataTagValues = UMetaData::GetMapForObject(SourceSkeletalMesh);
	ExistingMeshDataPtr->UseMaterialNameSlotWorkflow = SkeletalMeshIsUsingMaterialSlotNameWorkflow(SourceSkeletalMesh->AssetImportData);
	ExistingMeshDataPtr->MinLOD = SourceSkeletalMesh->MinLod;
	ExistingMeshDataPtr->DisableBelowMinLodStripping = SourceSkeletalMesh->DisableBelowMinLodStripping;
	ExistingMeshDataPtr->bOverrideLODStreamingSettings = SourceSkeletalMesh->bOverrideLODStreamingSettings;
	ExistingMeshDataPtr->bSupportLODStreaming = SourceSkeletalMesh->bSupportLODStreaming;
	ExistingMeshDataPtr->MaxNumStreamedLODs = SourceSkeletalMesh->MaxNumStreamedLODs;
	ExistingMeshDataPtr->MaxNumOptionalLODs = SourceSkeletalMesh->MaxNumOptionalLODs;

	//Add the existing Material slot name data
	for (int32 MaterialIndex = 0; MaterialIndex < SourceSkeletalMesh->Materials.Num(); ++MaterialIndex)
	{
		ExistingMeshDataPtr->ExistingImportMaterialOriginalNameData.Add(SourceSkeletalMesh->Materials[MaterialIndex].ImportedMaterialSlotName);
	}

	FSkeletalMeshModel* SourceMeshModel = SourceSkeletalMesh->GetImportedModel();
	for (int32 LodIndex = 0; LodIndex < SourceMeshModel->LODModels.Num(); ++LodIndex)
	{
		const bool bImportNonReducedData = LodIndex == SafeReimportLODIndex;
		SaveSkeletalMeshLODModelSections(SourceSkeletalMesh, ExistingMeshDataPtr, LodIndex, bImportNonReducedData);
	}

	ExistingMeshDataPtr->ExistingSockets = SourceSkeletalMesh->GetMeshOnlySocketList();
	ExistingMeshDataPtr->bSaveRestoreMaterials = bSaveMaterials;
	if (ExistingMeshDataPtr->bSaveRestoreMaterials)
	{
		ExistingMeshDataPtr->ExistingMaterials = SourceSkeletalMesh->Materials;
	}
	ExistingMeshDataPtr->ExistingRetargetBasePose = SourceSkeletalMesh->RetargetBasePose;

	if (SourceMeshModel->LODModels.Num() > 0 &&
		SourceSkeletalMesh->GetLODNum() == SourceMeshModel->LODModels.Num())
	{
		// Copy LOD models and LOD Infos.
		check(SourceMeshModel->LODModels.Num() == SourceSkeletalMesh->GetLODInfoArray().Num());
		ExistingMeshDataPtr->ExistingLODModels.Empty(SourceMeshModel->LODModels.Num());
		for ( int32 LODIndex = 0; LODIndex < SourceMeshModel->LODModels.Num() ; ++LODIndex)
		{
			//const int32 ReductionLODIndex = LODModelIndex + OffsetReductionLODIndex;
			TSharedPtr<FReductionBaseSkeletalMeshBulkData> ReductionLODData;
			if (SourceMeshModel->OriginalReductionSourceMeshData.IsValidIndex(LODIndex) && !SourceMeshModel->OriginalReductionSourceMeshData[LODIndex]->IsEmpty())
			{
				FSkeletalMeshLODModel BaseLODModel;
				TMap<FString, TArray<FMorphTargetDelta>> BaseLODMorphTargetData;
				SourceMeshModel->OriginalReductionSourceMeshData[LODIndex]->LoadReductionData(BaseLODModel, BaseLODMorphTargetData, SourceSkeletalMesh);
				ReductionLODData = MakeShared<FReductionBaseSkeletalMeshBulkData>();
				ReductionLODData->SaveReductionData(BaseLODModel, BaseLODMorphTargetData, SourceSkeletalMesh);
			}
			//Add the reduction source mesh data if it exist, otherwise an empty sharedPtr.
			ExistingMeshDataPtr->ExistingOriginalReductionSourceMeshData.Add(MoveTemp(ReductionLODData));
			
			//Add a new LOD Model to the existing LODModels data
			const FSkeletalMeshLODModel& LODModel = SourceMeshModel->LODModels[LODIndex];
			ExistingMeshDataPtr->ExistingLODModels.Add(FSkeletalMeshLODModel::CreateCopy(&LODModel));
		}
		check(ExistingMeshDataPtr->ExistingLODModels.Num() == SourceMeshModel->LODModels.Num());

		ExistingMeshDataPtr->ExistingLODInfo = SourceSkeletalMesh->GetLODInfoArray();
		ExistingMeshDataPtr->ExistingRefSkeleton = SourceSkeletalMesh->RefSkeleton;
	}

	// First asset should be the one that the skeletal mesh should point too
	ExistingMeshDataPtr->ExistingPhysicsAssets.Empty();
	ExistingMeshDataPtr->ExistingPhysicsAssets.Add(SourceSkeletalMesh->PhysicsAsset);
	for (TObjectIterator<UPhysicsAsset> It; It; ++It)
	{
		UPhysicsAsset* PhysicsAsset = *It;
		if (PhysicsAsset->PreviewSkeletalMesh == SourceSkeletalMesh && SourceSkeletalMesh->PhysicsAsset != PhysicsAsset)
		{
			ExistingMeshDataPtr->ExistingPhysicsAssets.Add(PhysicsAsset);
		}
	}

	ExistingMeshDataPtr->ExistingShadowPhysicsAsset = SourceSkeletalMesh->ShadowPhysicsAsset;
	ExistingMeshDataPtr->ExistingSkeleton = SourceSkeletalMesh->Skeleton;
	// since copying back original skeleton, this should be safe to do
	ExistingMeshDataPtr->ExistingPostProcessAnimBlueprint = SourceSkeletalMesh->PostProcessAnimBlueprint;
	ExistingMeshDataPtr->ExistingLODSettings = SourceSkeletalMesh->LODSettings;
	SourceSkeletalMesh->ExportMirrorTable(ExistingMeshDataPtr->ExistingMirrorTable);
	ExistingMeshDataPtr->ExistingMorphTargets = SourceSkeletalMesh->MorphTargets;
	ExistingMeshDataPtr->ExistingAssetImportData = SourceSkeletalMesh->AssetImportData;
	ExistingMeshDataPtr->ExistingThumbnailInfo = SourceSkeletalMesh->ThumbnailInfo;
	ExistingMeshDataPtr->ExistingClothingAssets = SourceSkeletalMesh->MeshClothingAssets;
	ExistingMeshDataPtr->ExistingSamplingInfo = SourceSkeletalMesh->GetSamplingInfo();

	if (ExistingMeshDataPtr->UseMaterialNameSlotWorkflow)
	{
		//Add the last fbx import data
		SaveSkeletalMeshMaterialNameWorkflowData(ExistingMeshDataPtr, SourceSkeletalMesh);
	}

	//Store the user asset data
	SaveSkeletalMeshAssetUserData(ExistingMeshDataPtr, SourceSkeletalMesh->GetAssetUserDataArray());
	
	//Store mesh changed delegate data
	ExistingMeshDataPtr->ExistingOnMeshChanged = SourceSkeletalMesh->GetOnMeshChanged();

	return ExistingMeshDataPtr;
}

void SkeletalMeshHelperImpl::RestoreDependentLODs(const TSharedPtr<const FExistingSkelMeshData>& MeshData, USkeletalMesh* SkeletalMesh)
{
	check(SkeletalMesh != nullptr);
	const int32 TotalLOD = MeshData->ExistingLODModels.Num();
	FSkeletalMeshModel* SkeletalMeshImportedModel = SkeletalMesh->GetImportedModel();

	for (int32 LODIndex = 1; LODIndex < TotalLOD; ++LODIndex)
	{
		if (LODIndex >= SkeletalMesh->GetLODInfoArray().Num())
		{
			// Create a copy of LODInfo and reset material maps, it won't work anyway. 
			FSkeletalMeshLODInfo ExistLODInfo = MeshData->ExistingLODInfo[LODIndex];
			ExistLODInfo.LODMaterialMap.Empty();
			// add LOD info back
			SkeletalMesh->AddLODInfo(MoveTemp(ExistLODInfo));
			check(LODIndex < SkeletalMesh->GetLODInfoArray().Num());

			const FSkeletalMeshLODModel& ExistLODModel = MeshData->ExistingLODModels[LODIndex];
			SkeletalMeshImportedModel->LODModels.Add(FSkeletalMeshLODModel::CreateCopy(&ExistLODModel));
		}
	}
}

void SkeletalMeshHelperImpl::RestoreLODInfo(const TSharedPtr<const FExistingSkelMeshData>& MeshData, USkeletalMesh* SkeletalMesh, int32 LodIndex)
{
	FSkeletalMeshLODInfo& ImportedLODInfo = SkeletalMesh->GetLODInfoArray()[LodIndex];
	if (!MeshData->ExistingLODInfo.IsValidIndex(LodIndex))
	{
		return;
	}

	const FSkeletalMeshLODInfo& ExistingLODInfo = MeshData->ExistingLODInfo[LodIndex];

	ImportedLODInfo.ScreenSize = ExistingLODInfo.ScreenSize;
	ImportedLODInfo.LODHysteresis = ExistingLODInfo.LODHysteresis;
	ImportedLODInfo.BuildSettings = ExistingLODInfo.BuildSettings;
	//Old assets may have non-applied reduction settings, so only restore the reduction settings if the LOD was effectively reduced.
	if (ExistingLODInfo.bHasBeenSimplified)
	{
		ImportedLODInfo.ReductionSettings = ExistingLODInfo.ReductionSettings;
	}
	ImportedLODInfo.BonesToRemove = ExistingLODInfo.BonesToRemove;
	ImportedLODInfo.BonesToPrioritize = ExistingLODInfo.BonesToPrioritize;
	ImportedLODInfo.WeightOfPrioritization = ExistingLODInfo.WeightOfPrioritization;
	ImportedLODInfo.BakePose = ExistingLODInfo.BakePose;
	ImportedLODInfo.BakePoseOverride = ExistingLODInfo.BakePoseOverride;
	ImportedLODInfo.SourceImportFilename = ExistingLODInfo.SourceImportFilename;
	ImportedLODInfo.SkinCacheUsage = ExistingLODInfo.SkinCacheUsage;
	ImportedLODInfo.bAllowCPUAccess = ExistingLODInfo.bAllowCPUAccess;
	ImportedLODInfo.bSupportUniformlyDistributedSampling = ExistingLODInfo.bSupportUniformlyDistributedSampling;
}

void SkeletalMeshHelper::ApplySkinning(USkeletalMesh* SkeletalMesh, FSkeletalMeshLODModel& SrcLODModel, FSkeletalMeshLODModel& DestLODModel)
{
	TArray<FSoftSkinVertex> SrcVertices;
	SrcLODModel.GetVertices(SrcVertices);

	FBox OldBounds(EForceInit::ForceInit);
	for (int32 SrcIndex = 0; SrcIndex < SrcVertices.Num(); ++SrcIndex)
	{
		const FSoftSkinVertex& SrcVertex = SrcVertices[SrcIndex];
		OldBounds += SrcVertex.Position;
	}

	TWedgeInfoPosOctree SrcWedgePosOctree(OldBounds.GetCenter(), OldBounds.GetExtent().GetMax());
	// Add each old vertex to the octree
	for (int32 SrcIndex = 0; SrcIndex < SrcVertices.Num(); ++SrcIndex)
	{
		FWedgeInfo WedgeInfo;
		WedgeInfo.WedgeIndex = SrcIndex;
		WedgeInfo.Position = SrcVertices[SrcIndex].Position;
		SrcWedgePosOctree.AddElement(WedgeInfo);
	}

	FOctreeQueryHelper OctreeQueryHelper(&SrcWedgePosOctree);

	TArray<FBoneIndexType> RequiredActiveBones;

	bool bUseBone = false;
	for (int32 SectionIndex = 0; SectionIndex < DestLODModel.Sections.Num(); SectionIndex++)
	{
		FSkelMeshSection& Section = DestLODModel.Sections[SectionIndex];
		Section.BoneMap.Reset();
		for (FSoftSkinVertex& DestVertex : Section.SoftVertices)
		{
			//Find the nearest wedges in the src model
			TArray<FWedgeInfo> NearestSrcWedges;
			OctreeQueryHelper.FindNearestWedgeIndexes(DestVertex.Position, NearestSrcWedges);
			if (NearestSrcWedges.Num() < 1)
			{
				//Should we check???
				continue;
			}
			//Find the matching wedges in the src model
			int32 MatchingSrcWedge = INDEX_NONE;
			for (FWedgeInfo& SrcWedgeInfo : NearestSrcWedges)
			{
				int32 SrcIndex = SrcWedgeInfo.WedgeIndex;
				const FSoftSkinVertex& SrcVertex = SrcVertices[SrcIndex];
				if (SrcVertex.Position.Equals(DestVertex.Position, THRESH_POINTS_ARE_SAME) &&
					SrcVertex.UVs[0].Equals(DestVertex.UVs[0], THRESH_UVS_ARE_SAME) &&
					(SrcVertex.TangentX == DestVertex.TangentX) &&
					(SrcVertex.TangentY == DestVertex.TangentY) &&
					(SrcVertex.TangentZ == DestVertex.TangentZ))
				{
					MatchingSrcWedge = SrcIndex;
					break;
				}
			}
			if (MatchingSrcWedge == INDEX_NONE)
			{
				//We have to find the nearest wedges, then find the most similar normal
				float MinDistance = MAX_FLT;
				float MinNormalAngle = MAX_FLT;
				for (FWedgeInfo& SrcWedgeInfo : NearestSrcWedges)
				{
					int32 SrcIndex = SrcWedgeInfo.WedgeIndex;
					const FSoftSkinVertex& SrcVertex = SrcVertices[SrcIndex];
					float VectorDelta = FVector::DistSquared(SrcVertex.Position, DestVertex.Position);
					if (VectorDelta <= (MinDistance + KINDA_SMALL_NUMBER))
					{
						if (VectorDelta < MinDistance - KINDA_SMALL_NUMBER)
						{
							MinDistance = VectorDelta;
							MinNormalAngle = MAX_FLT;
						}
						FVector DestTangentZ = DestVertex.TangentZ;
						DestTangentZ.Normalize();
						FVector SrcTangentZ = SrcVertex.TangentZ;
						SrcTangentZ.Normalize();
						float AngleDiff = FMath::Abs(FMath::Acos(FVector::DotProduct(DestTangentZ, SrcTangentZ)));
						if (AngleDiff < MinNormalAngle)
						{
							MinNormalAngle = AngleDiff;
							MatchingSrcWedge = SrcIndex;
						}
					}
				}
			}
			check(SrcVertices.IsValidIndex(MatchingSrcWedge));
			const FSoftSkinVertex& SrcVertex = SrcVertices[MatchingSrcWedge];

			//Find the src section to assign the correct remapped bone
			int32 SrcSectionIndex = INDEX_NONE;
			int32 SrcSectionWedgeIndex = INDEX_NONE;
			SrcLODModel.GetSectionFromVertexIndex(MatchingSrcWedge, SrcSectionIndex, SrcSectionWedgeIndex);
			check(SrcSectionIndex != INDEX_NONE);

			for (int32 InfluenceIndex = 0; InfluenceIndex < MAX_TOTAL_INFLUENCES; ++InfluenceIndex)
			{
				if (SrcVertex.InfluenceWeights[InfluenceIndex] > 0.0f)
				{
					Section.MaxBoneInfluences = FMath::Max(Section.MaxBoneInfluences, InfluenceIndex + 1);
					//Copy the weight
					DestVertex.InfluenceWeights[InfluenceIndex] = SrcVertex.InfluenceWeights[InfluenceIndex];
					//Copy the bone ID
					FBoneIndexType OriginalBoneIndex = SrcLODModel.Sections[SrcSectionIndex].BoneMap[SrcVertex.InfluenceBones[InfluenceIndex]];
					int32 OverrideIndex;
					if (Section.BoneMap.Find(OriginalBoneIndex, OverrideIndex))
					{
						DestVertex.InfluenceBones[InfluenceIndex] = OverrideIndex;
					}
					else
					{
						DestVertex.InfluenceBones[InfluenceIndex] = Section.BoneMap.Add(OriginalBoneIndex);
						DestLODModel.ActiveBoneIndices.AddUnique(OriginalBoneIndex);
					}
					bUseBone = true;
				}
			}
		}
	}

	if (bUseBone)
	{
		//Set the required/active bones
		DestLODModel.RequiredBones = SrcLODModel.RequiredBones;
		DestLODModel.RequiredBones.Sort();
		SkeletalMesh->RefSkeleton.EnsureParentsExistAndSort(DestLODModel.ActiveBoneIndices);
	}
}


void SkeletalMeshHelper::RestoreExistingSkelMeshData(TSharedPtr<const FExistingSkelMeshData> MeshData, USkeletalMesh* SkeletalMesh, int32 ReimportLODIndex, bool bCanShowDialog, bool bImportSkinningOnly, bool bForceMaterialReset)
{
	using namespace SkeletalMeshHelperImpl;
	if (!MeshData || !SkeletalMesh)
	{
		return;
	}

	//Restore the package metadata
	if (MeshData->ExistingUMetaDataTagValues)
	{
		UMetaData* PackageMetaData = SkeletalMesh->GetOutermost()->GetMetaData();
		checkSlow(PackageMetaData);
		PackageMetaData->SetObjectValues(SkeletalMesh, *MeshData->ExistingUMetaDataTagValues);
	}

	int32 SafeReimportLODIndex = ReimportLODIndex < 0 ? 0 : ReimportLODIndex;
	SkeletalMesh->MinLod = MeshData->MinLOD;
	SkeletalMesh->DisableBelowMinLodStripping = MeshData->DisableBelowMinLodStripping;
	SkeletalMesh->bOverrideLODStreamingSettings = MeshData->bOverrideLODStreamingSettings;
	SkeletalMesh->bSupportLODStreaming = MeshData->bSupportLODStreaming;
	SkeletalMesh->MaxNumStreamedLODs = MeshData->MaxNumStreamedLODs;
	SkeletalMesh->MaxNumOptionalLODs = MeshData->MaxNumOptionalLODs;

	FSkeletalMeshModel* SkeletalMeshImportedModel = SkeletalMesh->GetImportedModel();

	//Create a remap material Index use to find the matching section later
	TArray<int32> RemapMaterial;
	RemapMaterial.AddZeroed(SkeletalMesh->Materials.Num());
	TArray<FName> RemapMaterialName;
	RemapMaterialName.AddZeroed(SkeletalMesh->Materials.Num());

	bool bMaterialReset = false;
	if (MeshData->bSaveRestoreMaterials)
	{
		UnFbx::EFBXReimportDialogReturnOption ReturnOption;
		//Ask the user to match the materials conflict
		UnFbx::FFbxImporter::PrepareAndShowMaterialConflictDialog<FSkeletalMaterial>(MeshData->ExistingMaterials, SkeletalMesh->Materials, RemapMaterial, RemapMaterialName, bCanShowDialog, false, bForceMaterialReset, ReturnOption);

		if (ReturnOption != UnFbx::EFBXReimportDialogReturnOption::FBXRDRO_ResetToFbx)
		{
			//Build a ordered material list that try to keep intact the existing material list
			TArray<FSkeletalMaterial> MaterialOrdered;
			TArray<bool> MatchedNewMaterial;
			MatchedNewMaterial.AddZeroed(SkeletalMesh->Materials.Num());
			for (int32 ExistMaterialIndex = 0; ExistMaterialIndex < MeshData->ExistingMaterials.Num(); ++ExistMaterialIndex)
			{
				int32 MaterialIndexOrdered = MaterialOrdered.Add(MeshData->ExistingMaterials[ExistMaterialIndex]);
				FSkeletalMaterial& OrderedMaterial = MaterialOrdered[MaterialIndexOrdered];
				int32 NewMaterialIndex = INDEX_NONE;
				if (RemapMaterial.Find(ExistMaterialIndex, NewMaterialIndex))
				{
					MatchedNewMaterial[NewMaterialIndex] = true;
					RemapMaterial[NewMaterialIndex] = MaterialIndexOrdered;
					OrderedMaterial.ImportedMaterialSlotName = SkeletalMesh->Materials[NewMaterialIndex].ImportedMaterialSlotName;
				}
				else
				{
					//Unmatched material must be conserve
				}
			}

			//Add the new material entries (the one that do not match with any existing material)
			for (int32 NewMaterialIndex = 0; NewMaterialIndex < MatchedNewMaterial.Num(); ++NewMaterialIndex)
			{
				if (MatchedNewMaterial[NewMaterialIndex] == false)
				{
					int32 NewMeshIndex = MaterialOrdered.Add(SkeletalMesh->Materials[NewMaterialIndex]);
					RemapMaterial[NewMaterialIndex] = NewMeshIndex;
				}
			}

			//Set the RemapMaterialName array helper
			for (int32 MaterialIndex = 0; MaterialIndex < RemapMaterial.Num(); ++MaterialIndex)
			{
				int32 SourceMaterialMatch = RemapMaterial[MaterialIndex];
				if (MeshData->ExistingMaterials.IsValidIndex(SourceMaterialMatch))
				{
					RemapMaterialName[MaterialIndex] = MeshData->ExistingMaterials[SourceMaterialMatch].ImportedMaterialSlotName;
				}
			}

			//Copy the re ordered materials (this ensure the material array do not change when we re-import)
			SkeletalMesh->Materials = MaterialOrdered;
		}
		else
		{
			bMaterialReset = true;
		}
	}

	SkeletalMesh->LODSettings = MeshData->ExistingLODSettings;
	// ensure LOD 0 contains correct setting 
	if (SkeletalMesh->LODSettings && SkeletalMesh->GetLODInfoArray().Num() > 0)
	{
		SkeletalMesh->LODSettings->SetLODSettingsToMesh(SkeletalMesh, 0);
	}

	//Do everything we need for base LOD re-import
	if (SafeReimportLODIndex == 0)
	{
		// this is not ideal. Ideally we'll have to save only diff with indicating which joints, 
		// but for now, we allow them to keep the previous pose IF the element count is same
		if (MeshData->ExistingRetargetBasePose.Num() == SkeletalMesh->RefSkeleton.GetRawBoneNum())
		{
			SkeletalMesh->RetargetBasePose = MeshData->ExistingRetargetBasePose;
		}

		// Assign sockets from old version of this SkeletalMesh.
		// Only copy ones for bones that exist in the new mesh.
		for (int32 i = 0; i < MeshData->ExistingSockets.Num(); i++)
		{
			const int32 BoneIndex = SkeletalMesh->RefSkeleton.FindBoneIndex(MeshData->ExistingSockets[i]->BoneName);
			if (BoneIndex != INDEX_NONE)
			{
				SkeletalMesh->GetMeshOnlySocketList().Add(MeshData->ExistingSockets[i]);
			}
		}

		// We copy back and fix-up the LODs that still work with this skeleton.
		if (MeshData->ExistingLODModels.Num() > 1)
		{
			auto RestoreReductionSourceData = [&SkeletalMesh, &SkeletalMeshImportedModel, &MeshData](int32 ExistingIndex, int32 NewIndex)
			{
				if (!MeshData->ExistingOriginalReductionSourceMeshData[ExistingIndex].IsValid() || MeshData->ExistingOriginalReductionSourceMeshData[ExistingIndex]->IsEmpty())
				{
					return;
				}
				//Restore the original reduction source mesh data
				FSkeletalMeshLODModel BaseLODModel;
				TMap<FString, TArray<FMorphTargetDelta>> BaseLODMorphTargetData;
				MeshData->ExistingOriginalReductionSourceMeshData[ExistingIndex]->LoadReductionData(BaseLODModel, BaseLODMorphTargetData, SkeletalMesh);
				FReductionBaseSkeletalMeshBulkData* ReductionLODData = new FReductionBaseSkeletalMeshBulkData();
				ReductionLODData->SaveReductionData(BaseLODModel, BaseLODMorphTargetData, SkeletalMesh);
				//Add necessary empty slot
				while (SkeletalMeshImportedModel->OriginalReductionSourceMeshData.Num() < NewIndex)
				{
					FReductionBaseSkeletalMeshBulkData* EmptyReductionLODData = new FReductionBaseSkeletalMeshBulkData();
					SkeletalMeshImportedModel->OriginalReductionSourceMeshData.Add(EmptyReductionLODData);
				}
				SkeletalMeshImportedModel->OriginalReductionSourceMeshData.Add(ReductionLODData);
			};

			if (SkeletonsAreCompatible(SkeletalMesh->RefSkeleton, MeshData->ExistingRefSkeleton, bImportSkinningOnly))
			{
				// First create mapping table from old skeleton to new skeleton.
				TArray<int32> OldToNewMap;
				OldToNewMap.AddUninitialized(MeshData->ExistingRefSkeleton.GetRawBoneNum());
				for (int32 i = 0; i < MeshData->ExistingRefSkeleton.GetRawBoneNum(); i++)
				{
					OldToNewMap[i] = SkeletalMesh->RefSkeleton.FindBoneIndex(MeshData->ExistingRefSkeleton.GetBoneName(i));
				}

				//Starting at index 1 because we only need to add LOD models of LOD 1 and higher.
				for (int32 LODIndex = 1; LODIndex < MeshData->ExistingLODModels.Num(); ++LODIndex)
				{
					FSkeletalMeshLODModel* LODModelCopy = FSkeletalMeshLODModel::CreateCopy(&MeshData->ExistingLODModels[LODIndex]);
					const FSkeletalMeshLODInfo& LODInfo = MeshData->ExistingLODInfo[LODIndex];

					// Fix ActiveBoneIndices array.
					bool bMissingBone = false;
					FName MissingBoneName = NAME_None;
					for (int32 j = 0; j < LODModelCopy->ActiveBoneIndices.Num() && !bMissingBone; j++)
					{
						int32 OldActiveBoneIndex = LODModelCopy->ActiveBoneIndices[j];
						if (OldToNewMap.IsValidIndex(OldActiveBoneIndex))
						{
							int32 NewBoneIndex = OldToNewMap[OldActiveBoneIndex];
							if (NewBoneIndex == INDEX_NONE)
							{
								bMissingBone = true;
								MissingBoneName = MeshData->ExistingRefSkeleton.GetBoneName(LODModelCopy->ActiveBoneIndices[j]);
							}
							else
							{
								LODModelCopy->ActiveBoneIndices[j] = NewBoneIndex;
							}
						}
						else
						{
							LODModelCopy->ActiveBoneIndices.RemoveAt(j, 1, false);
							--j;
						}
					}

					// Fix RequiredBones array.
					for (int32 j = 0; j < LODModelCopy->RequiredBones.Num() && !bMissingBone; j++)
					{
						const int32 OldBoneIndex = LODModelCopy->RequiredBones[j];

						if (OldToNewMap.IsValidIndex(OldBoneIndex))	//Previously virtual bones could end up in this array
																	// Must validate against this
						{
							const int32 NewBoneIndex = OldToNewMap[OldBoneIndex];
							if (NewBoneIndex == INDEX_NONE)
							{
								bMissingBone = true;
								MissingBoneName = MeshData->ExistingRefSkeleton.GetBoneName(OldBoneIndex);
							}
							else
							{
								LODModelCopy->RequiredBones[j] = NewBoneIndex;
							}
						}
						else
						{
							//Bone didn't exist in our required bones, clean up. 
							LODModelCopy->RequiredBones.RemoveAt(j, 1, false);
							--j;
						}
					}

					// Sort ascending for parent child relationship
					LODModelCopy->RequiredBones.Sort();
					SkeletalMesh->RefSkeleton.EnsureParentsExistAndSort(LODModelCopy->ActiveBoneIndices);

					// Fix the sections' BoneMaps.
					for (int32 SectionIndex = 0; SectionIndex < LODModelCopy->Sections.Num(); SectionIndex++)
					{
						FSkelMeshSection& Section = LODModelCopy->Sections[SectionIndex];
						for (int32 BoneIndex = 0; BoneIndex < Section.BoneMap.Num(); BoneIndex++)
						{
							int32 NewBoneIndex = OldToNewMap[Section.BoneMap[BoneIndex]];
							if (NewBoneIndex == INDEX_NONE)
							{
								bMissingBone = true;
								MissingBoneName = MeshData->ExistingRefSkeleton.GetBoneName(Section.BoneMap[BoneIndex]);
								break;
							}
							else
							{
								Section.BoneMap[BoneIndex] = NewBoneIndex;
							}
						}
						if (bMissingBone)
						{
							break;
						}
					}

					if (bMissingBone)
					{
						UnFbx::FFbxImporter* FFbxImporter = UnFbx::FFbxImporter::GetInstance();
						FFbxImporter->AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Warning, FText::Format(LOCTEXT("NewMeshMissingBoneFromLOD", "New mesh is missing bone '{0}' required by an LOD."), FText::FromName(MissingBoneName))), FFbxErrors::SkeletalMesh_LOD_MissingBone);
						break;
					}
					else
					{
						//We need to add LODInfo
						SkeletalMeshImportedModel->LODModels.Add(LODModelCopy);
						SkeletalMesh->AddLODInfo(LODInfo);
						RestoreReductionSourceData(LODIndex, SkeletalMesh->GetLODNum() - 1);
					}
				}
			}
			//We just need to restore the LOD model and LOD info the build should regenerate the LODs
			RestoreDependentLODs(MeshData, SkeletalMesh);
			
			
			//Old asset cannot use the new build system, we need to regenerate dependent LODs
			if (SkeletalMesh->IsLODImportedDataBuildAvailable(SafeReimportLODIndex) == false)
			{
				FLODUtilities::RegenerateDependentLODs(SkeletalMesh, SafeReimportLODIndex);
			}
		}

		for (int32 AssetIndex = 0; AssetIndex < MeshData->ExistingPhysicsAssets.Num(); ++AssetIndex)
		{
			UPhysicsAsset* PhysicsAsset = MeshData->ExistingPhysicsAssets[AssetIndex];
			if (AssetIndex == 0)
			{
				// First asset is the one that the skeletal mesh should point too
				SkeletalMesh->PhysicsAsset = PhysicsAsset;
			}
			// No need to mark as modified here, because the asset hasn't actually changed
			if (PhysicsAsset)
			{
				PhysicsAsset->PreviewSkeletalMesh = SkeletalMesh;
			}
		}

		SkeletalMesh->ShadowPhysicsAsset = MeshData->ExistingShadowPhysicsAsset;
		SkeletalMesh->Skeleton = MeshData->ExistingSkeleton;
		SkeletalMesh->PostProcessAnimBlueprint = MeshData->ExistingPostProcessAnimBlueprint;

		// Copy mirror table.
		SkeletalMesh->ImportMirrorTable(MeshData->ExistingMirrorTable);
		SkeletalMesh->MorphTargets.Empty(MeshData->ExistingMorphTargets.Num());
		SkeletalMesh->MorphTargets.Append(MeshData->ExistingMorphTargets);
		SkeletalMesh->InitMorphTargets();
		SkeletalMesh->AssetImportData = MeshData->ExistingAssetImportData.Get();
		SkeletalMesh->ThumbnailInfo = MeshData->ExistingThumbnailInfo.Get();
		SkeletalMesh->MeshClothingAssets = MeshData->ExistingClothingAssets;

		for (UClothingAssetBase* ClothingAsset : SkeletalMesh->MeshClothingAssets)
		{
			if (ClothingAsset)
			{
				ClothingAsset->RefreshBoneMapping(SkeletalMesh);
			}
		}

		SkeletalMesh->SetSamplingInfo(MeshData->ExistingSamplingInfo);
	}

	//Restore the section change only for the reimport LOD, other LOD are not affected since the material array can only grow.
	if (MeshData->UseMaterialNameSlotWorkflow)
	{
		RestoreMaterialNameWorkflowSection(MeshData, SkeletalMesh, SafeReimportLODIndex, RemapMaterial, bMaterialReset);
	}

	//Copy back the reimported LOD's specific data
	if (SkeletalMesh->GetLODInfoArray().IsValidIndex(SafeReimportLODIndex))
	{
		RestoreLODInfo(MeshData, SkeletalMesh, SafeReimportLODIndex);
	}

	// Copy user data to newly created mesh
	for (auto Kvp : MeshData->ExistingAssetUserData)
	{
		UAssetUserData* UserDataObject = Kvp.Key;
		if (Kvp.Value)
		{
			//if the duplicated temporary UObject was add to root, we must remove it from the root
			UserDataObject->RemoveFromRoot();
		}
		UserDataObject->Rename(nullptr, SkeletalMesh, REN_DontCreateRedirectors | REN_DoNotDirty);
		SkeletalMesh->AddAssetUserData(UserDataObject);
	}

	if (!bImportSkinningOnly && (!MeshData->ExistingLODInfo.IsValidIndex(SafeReimportLODIndex) || !MeshData->ExistingLODInfo[SafeReimportLODIndex].bHasBeenSimplified))
	{
		if (SkeletalMeshImportedModel->OriginalReductionSourceMeshData.IsValidIndex(SafeReimportLODIndex))
		{
			SkeletalMeshImportedModel->OriginalReductionSourceMeshData[SafeReimportLODIndex]->EmptyBulkData();
		}
	}

	//Copy mesh changed delegate data
	SkeletalMesh->GetOnMeshChanged() = MeshData->ExistingOnMeshChanged;
}

void SkeletalMeshHelperImpl::RestoreMaterialNameWorkflowSection(const TSharedPtr<const FExistingSkelMeshData>& MeshData, USkeletalMesh* SkeletalMesh, int32 LodIndex, TArray<int32>& RemapMaterial, bool bMaterialReset)
{
	FSkeletalMeshModel* SkeletalMeshImportedModel = SkeletalMesh->GetImportedModel();
	FSkeletalMeshLODModel &SkeletalMeshLodModel = SkeletalMeshImportedModel->LODModels[LodIndex];

	//Restore the base LOD materialMap the LODs LODMaterialMap are restore differently
	if (LodIndex == 0 && SkeletalMesh->GetLODInfoArray().IsValidIndex(LodIndex))
	{
		FSkeletalMeshLODInfo& BaseLODInfo = SkeletalMesh->GetLODInfoArray()[LodIndex];
		if (bMaterialReset)
		{
			//If we reset the material array there is no point keeping the user changes
			BaseLODInfo.LODMaterialMap.Empty();
		}
		else if (SkeletalMeshImportedModel->LODModels.IsValidIndex(LodIndex))
		{
			//Restore the Base MaterialMap
			for (int32 SectionIndex = 0; SectionIndex < SkeletalMeshLodModel.Sections.Num(); ++SectionIndex)
			{
				int32 MaterialIndex = SkeletalMeshLodModel.Sections[SectionIndex].MaterialIndex;
				if (MeshData->ExistingLODInfo[LodIndex].LODMaterialMap.IsValidIndex(SectionIndex))
				{
					int32 ExistingLODMaterialIndex = MeshData->ExistingLODInfo[LodIndex].LODMaterialMap[SectionIndex];
					while (BaseLODInfo.LODMaterialMap.Num() <= SectionIndex)
					{
						BaseLODInfo.LODMaterialMap.Add(INDEX_NONE);
					}
					BaseLODInfo.LODMaterialMap[SectionIndex] = ExistingLODMaterialIndex;
				}
			}
		}
	}

	const bool bIsValidSavedSectionMaterialData = MeshData->ExistingImportMeshLodSectionMaterialData.IsValidIndex(LodIndex) && MeshData->LastImportMeshLodSectionMaterialData.IsValidIndex(LodIndex);
	const int32 MaxExistSectionNumber = bIsValidSavedSectionMaterialData ? FMath::Max(MeshData->ExistingImportMeshLodSectionMaterialData[LodIndex].Num(), MeshData->LastImportMeshLodSectionMaterialData[LodIndex].Num()) : 0;
	TBitArray<> MatchedExistSectionIndex;
	MatchedExistSectionIndex.Init(false, MaxExistSectionNumber);

	//Restore the section changes from the old import data
	for (int32 SectionIndex = 0; SectionIndex < SkeletalMeshLodModel.Sections.Num(); SectionIndex++)
	{
		//Find the import section material index by using the RemapMaterial array. Fallback on the imported index if the remap entry is not valid
		FSkelMeshSection& NewSection = SkeletalMeshLodModel.Sections[SectionIndex];
		int32 RemapMaterialIndex = RemapMaterial.IsValidIndex(NewSection.MaterialIndex) ? RemapMaterial[NewSection.MaterialIndex] : NewSection.MaterialIndex;
		if (!SkeletalMesh->Materials.IsValidIndex(RemapMaterialIndex))
		{
			//We have an invalid material section, in this case we set the material index to 0
			NewSection.MaterialIndex = 0;
			UE_LOG(LogSkeletalMeshImport, Display, TEXT("Reimport material match issue: Invalid RemapMaterialIndex [%d], will make it point to material index [0]"), RemapMaterialIndex);
			continue;
		}
		NewSection.MaterialIndex = RemapMaterialIndex;

		//skip the rest of the loop if we do not have valid saved data
		if (!bIsValidSavedSectionMaterialData)
		{
			continue;
		}
		//Get the RemapMaterial section Imported material slot name. We need it to match the saved existing section, so we can put back the saved existing section data
		FName CurrentSectionImportedMaterialName = SkeletalMesh->Materials[RemapMaterialIndex].ImportedMaterialSlotName;
		for (int32 ExistSectionIndex = 0; ExistSectionIndex < MaxExistSectionNumber; ++ExistSectionIndex)
		{
			//Skip already matched exist section
			if (MatchedExistSectionIndex[ExistSectionIndex])
			{
				continue;
			}
			//Verify we have valid existing section data, if not break from the loop higher index wont be valid
			if (!MeshData->LastImportMeshLodSectionMaterialData[LodIndex].IsValidIndex(ExistSectionIndex) || !MeshData->ExistingImportMeshLodSectionMaterialData[LodIndex].IsValidIndex(ExistSectionIndex))
			{
				break;
			}

			//Get the Last imported skelmesh section slot import name
			FName OriginalImportMeshSectionSlotName = MeshData->LastImportMeshLodSectionMaterialData[LodIndex][ExistSectionIndex];
			if (OriginalImportMeshSectionSlotName != CurrentSectionImportedMaterialName)
			{
				//Skip until we found a match between the last import
				continue;
			}

			//We have a match put back the data
			NewSection.bCastShadow = MeshData->ExistingImportMeshLodSectionMaterialData[LodIndex][ExistSectionIndex].bCastShadow;
			NewSection.bRecomputeTangent = MeshData->ExistingImportMeshLodSectionMaterialData[LodIndex][ExistSectionIndex].bRecomputeTangents;
			NewSection.RecomputeTangentsVertexMaskChannel = MeshData->ExistingImportMeshLodSectionMaterialData[LodIndex][ExistSectionIndex].RecomputeTangentsVertexMaskChannel;
			NewSection.GenerateUpToLodIndex = MeshData->ExistingImportMeshLodSectionMaterialData[LodIndex][ExistSectionIndex].GenerateUpTo;
			NewSection.bDisabled = MeshData->ExistingImportMeshLodSectionMaterialData[LodIndex][ExistSectionIndex].bDisabled;
			bool bBoneChunkedSection = NewSection.ChunkedParentSectionIndex >= 0;
			int32 ParentOriginalSectionIndex = NewSection.OriginalDataSectionIndex;
			if (!bBoneChunkedSection)
			{
				//Set the new Parent Index
				FSkelMeshSourceSectionUserData& UserSectionData = SkeletalMeshLodModel.UserSectionsData.FindOrAdd(ParentOriginalSectionIndex);
				UserSectionData.bDisabled = NewSection.bDisabled;
				UserSectionData.bCastShadow = NewSection.bCastShadow;
				UserSectionData.bRecomputeTangent = NewSection.bRecomputeTangent;
				UserSectionData.RecomputeTangentsVertexMaskChannel = NewSection.RecomputeTangentsVertexMaskChannel;
				UserSectionData.GenerateUpToLodIndex = NewSection.GenerateUpToLodIndex;
				//The cloth will be rebind later after the reimport is done
			}
			//Set the matched section to true to avoid using it again
			MatchedExistSectionIndex[ExistSectionIndex] = true;

			//find the corresponding current slot name in the skeletal mesh materials list to remap properly the material index, in case the user have change it before re-importing
			FName ExistMeshSectionSlotName = MeshData->ExistingImportMeshLodSectionMaterialData[LodIndex][ExistSectionIndex].ImportedMaterialSlotName;
			{
				for (int32 SkelMeshMaterialIndex = 0; SkelMeshMaterialIndex < SkeletalMesh->Materials.Num(); ++SkelMeshMaterialIndex)
				{
					const FSkeletalMaterial &NewSectionMaterial = SkeletalMesh->Materials[SkelMeshMaterialIndex];
					if (NewSectionMaterial.ImportedMaterialSlotName == ExistMeshSectionSlotName)
					{
						if (ExistMeshSectionSlotName != OriginalImportMeshSectionSlotName)
						{
							NewSection.MaterialIndex = SkelMeshMaterialIndex;
						}
						break;
					}
				}
			}
			//Break because we found a match and have restore the data for this SectionIndex
			break;
		}
	}
	//Make sure we reset the User section array to only what we have in the fbx
	SkeletalMeshLodModel.SyncronizeUserSectionsDataArray(true);
}

#undef LOCTEXT_NAMESPACE
