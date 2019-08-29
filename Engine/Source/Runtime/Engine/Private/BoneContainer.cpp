// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "BoneContainer.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "EngineLogs.h"
#include "Animation/AnimCurveTypes.h"

DEFINE_LOG_CATEGORY(LogSkeletalControl);

//////////////////////////////////////////////////////////////////////////
// FBoneContainer

FBoneContainer::FBoneContainer()
: Asset(nullptr)
, AssetSkeletalMesh(nullptr)
, AssetSkeleton(nullptr)
, RefSkeleton(nullptr)
, bDisableRetargeting(false)
, bUseRAWData(false)
, bUseSourceData(false)
{
	BoneIndicesArray.Empty();
	BoneSwitchArray.Empty();
	SkeletonToPoseBoneIndexArray.Empty();
	PoseToSkeletonBoneIndexArray.Empty();
}

FBoneContainer::FBoneContainer(const TArray<FBoneIndexType>& InRequiredBoneIndexArray, const FCurveEvaluationOption& CurveEvalOption, UObject& InAsset)
: BoneIndicesArray(InRequiredBoneIndexArray)
, Asset(&InAsset)
, AssetSkeletalMesh(nullptr)
, AssetSkeleton(nullptr)
, RefSkeleton(nullptr)
, bDisableRetargeting(false)
, bUseRAWData(false)
, bUseSourceData(false)
{
	Initialize(CurveEvalOption);
}

void FBoneContainer::InitializeTo(const TArray<FBoneIndexType>& InRequiredBoneIndexArray, const FCurveEvaluationOption& CurveEvalOption, UObject& InAsset)
{
	BoneIndicesArray = InRequiredBoneIndexArray;
	Asset = &InAsset;

	Initialize(CurveEvalOption);
}

struct FBoneContainerScratchArea : public TThreadSingleton<FBoneContainerScratchArea>
{
	TArray<int32> MeshIndexToCompactPoseIndex;
};

void FBoneContainer::Initialize(const FCurveEvaluationOption& CurveEvalOption)
{
	RefSkeleton = nullptr;
	UObject* AssetObj = Asset.Get();
	USkeletalMesh* AssetSkeletalMeshObj = Cast<USkeletalMesh>(AssetObj);
	USkeleton* AssetSkeletonObj = nullptr;

	if (AssetSkeletalMeshObj)
	{
		RefSkeleton = &AssetSkeletalMeshObj->RefSkeleton;
		AssetSkeletonObj = AssetSkeletalMeshObj->Skeleton;
	}
	else
	{
		AssetSkeletonObj = Cast<USkeleton>(AssetObj);
		if (AssetSkeletonObj)
		{
			RefSkeleton = &AssetSkeletonObj->GetReferenceSkeleton();
		}
	}

	// Only supports SkeletalMeshes or Skeletons.
	check( AssetSkeletalMeshObj || AssetSkeletonObj );
	// Skeleton should always be there.
	checkf( AssetSkeletonObj, TEXT("%s missing skeleton"), *GetNameSafe(AssetSkeletalMeshObj));
	check( RefSkeleton );

	AssetSkeleton = AssetSkeletonObj;
	AssetSkeletalMesh = AssetSkeletalMeshObj;

	// Take biggest amount of bones between SkeletalMesh and Skeleton for BoneSwitchArray.
	// SkeletalMesh can have less, but AnimSequences tracks will map to Skeleton which can have more.
	const int32 MaxBones = AssetSkeletonObj ? FMath::Max<int32>(RefSkeleton->GetNum(), AssetSkeletonObj->GetReferenceSkeleton().GetNum()) : RefSkeleton->GetNum();

	// Initialize BoneSwitchArray.
	BoneSwitchArray.Init(false, MaxBones);
	const int32 NumRequiredBones = BoneIndicesArray.Num();
	for(int32 Index=0; Index<NumRequiredBones; Index++)
	{
		const FBoneIndexType BoneIndex = BoneIndicesArray[Index];
		checkSlow( BoneIndex < MaxBones );
		BoneSwitchArray[BoneIndex] = true;
	}

	// Clear remapping table
	SkeletonToPoseBoneIndexArray.Reset();

	// Cache our mapping tables
	// Here we create look up tables between our target asset and its USkeleton's refpose.
	// Most times our Target is a SkeletalMesh
	if (AssetSkeletalMeshObj)
	{
		RemapFromSkelMesh(*AssetSkeletalMeshObj, *AssetSkeletonObj);
	}
	// But we also support a Skeleton's RefPose.
	else
	{
		// Right now we only support a single Skeleton. Skeleton hierarchy coming soon!
		RemapFromSkeleton(*AssetSkeletonObj);
	}

	//Set up compact pose data
	int32 NumReqBones = BoneIndicesArray.Num();
	CompactPoseParentBones.Reset(NumReqBones);

	CompactPoseRefPoseBones.Reset(NumReqBones);
	CompactPoseRefPoseBones.AddUninitialized(NumReqBones);

	CompactPoseToSkeletonIndex.Reset(NumReqBones);
	CompactPoseToSkeletonIndex.AddUninitialized(NumReqBones);

	SkeletonToCompactPose.Reset(SkeletonToPoseBoneIndexArray.Num());

	VirtualBoneCompactPoseData.Reset(RefSkeleton->GetVirtualBoneRefData().Num());

	const TArray<FTransform>& RefPoseArray = RefSkeleton->GetRefBonePose();
	TArray<int32>& MeshIndexToCompactPoseIndex = FBoneContainerScratchArea::Get().MeshIndexToCompactPoseIndex;
	MeshIndexToCompactPoseIndex.Reset(PoseToSkeletonBoneIndexArray.Num());
	MeshIndexToCompactPoseIndex.AddUninitialized(PoseToSkeletonBoneIndexArray.Num());

	for (int32& Item : MeshIndexToCompactPoseIndex)
	{
		Item = -1;
	}
		
	for (int32 CompactBoneIndex = 0; CompactBoneIndex < NumReqBones; ++CompactBoneIndex)
	{
		FBoneIndexType MeshPoseIndex = BoneIndicesArray[CompactBoneIndex];
		MeshIndexToCompactPoseIndex[MeshPoseIndex] = CompactBoneIndex;

		//Parent Bone
		const int32 ParentIndex = GetParentBoneIndex(MeshPoseIndex);
		const int32 CompactParentIndex = ParentIndex == INDEX_NONE ? INDEX_NONE : MeshIndexToCompactPoseIndex[ParentIndex];

		CompactPoseParentBones.Add(FCompactPoseBoneIndex(CompactParentIndex));
	}

	//Ref Pose
	for (int32 CompactBoneIndex = 0; CompactBoneIndex < NumReqBones; ++CompactBoneIndex)
	{
		FBoneIndexType MeshPoseIndex = BoneIndicesArray[CompactBoneIndex];
		CompactPoseRefPoseBones[CompactBoneIndex] = RefPoseArray[MeshPoseIndex];
	}

	for (int32 CompactBoneIndex = 0; CompactBoneIndex < NumReqBones; ++CompactBoneIndex)
	{
		FBoneIndexType MeshPoseIndex = BoneIndicesArray[CompactBoneIndex];
		CompactPoseToSkeletonIndex[CompactBoneIndex] = PoseToSkeletonBoneIndexArray[MeshPoseIndex];
	}


	for (int32 SkeletonBoneIndex = 0; SkeletonBoneIndex < SkeletonToPoseBoneIndexArray.Num(); ++SkeletonBoneIndex)
	{
		int32 PoseBoneIndex = SkeletonToPoseBoneIndexArray[SkeletonBoneIndex];
		int32 CompactIndex = (PoseBoneIndex != INDEX_NONE) ? MeshIndexToCompactPoseIndex[PoseBoneIndex] : INDEX_NONE;
		SkeletonToCompactPose.Add(FCompactPoseBoneIndex(CompactIndex));
	}

	for (const FVirtualBoneRefData& VBRefBone : RefSkeleton->GetVirtualBoneRefData())
	{
		const int32 VBInd = MeshIndexToCompactPoseIndex[VBRefBone.VBRefSkelIndex];
		const int32 SourceInd = MeshIndexToCompactPoseIndex[VBRefBone.SourceRefSkelIndex];
		const int32 TargetInd = MeshIndexToCompactPoseIndex[VBRefBone.TargetRefSkelIndex];

		if ((VBInd != INDEX_NONE) && (SourceInd != INDEX_NONE) && (TargetInd != INDEX_NONE))
		{
			VirtualBoneCompactPoseData.Add(FVirtualBoneCompactPoseData(FCompactPoseBoneIndex(VBInd), FCompactPoseBoneIndex(SourceInd), FCompactPoseBoneIndex(TargetInd)));
		}
	}
	// cache required curve UID list according to new bone sets
	CacheRequiredAnimCurveUids(CurveEvalOption);

	// Reset retargeting cached data look up table.
	RetargetSourceCachedDataLUT.Reset();
}

void FBoneContainer::CacheRequiredAnimCurveUids(const FCurveEvaluationOption& CurveEvalOption)
{
	if (AssetSkeleton.IsValid())
	{
		// this is placeholder. In the future, this will change to work with linked joint of curve meta data
		// anim curve name Uids; For now it adds all of them
		const FSmartNameMapping* Mapping = AssetSkeleton->GetSmartNameContainer(USkeleton::AnimCurveMappingName);
		if (Mapping != nullptr)
		{
			UIDToArrayIndexLUT.Reset();
			
			const SmartName::UID_Type MaxUID = Mapping->GetMaxUID();

			if (MaxUID == SmartName::MaxUID)
			{
				// No smart names, nothing to do
				return;
			}

			//Init UID LUT to everything unused
			UIDToArrayIndexLUT.AddUninitialized(MaxUID+1);
			for (SmartName::UID_Type& Item : UIDToArrayIndexLUT)
			{
				Item = SmartName::MaxUID;
			}

			// Get Current Names / UIDs
			TArray<FName> CurveNames;
			Mapping->FillNameArray(CurveNames);

			// Get UID List
			TArray<SmartName::UID_Type> UIDList;
			Mapping->FillUidArray(UIDList);

			// if the linked joints don't exists in RequiredBones, remove itself
			if (CurveNames.Num() > 0)
			{
				int32 NumAvailableUIDs = 0;
				for (int32 CurveNameIndex = CurveNames.Num() - 1; CurveNameIndex >=0 ; --CurveNameIndex)
				{
					const FName& CurveName = CurveNames[CurveNameIndex];
					bool bBeingUsed = true;
					if (!CurveEvalOption.bAllowCurveEvaluation)
					{
						bBeingUsed = false;
					}
					else
					{
						// CurveNameIndex shouyld match to UID
						if (CurveEvalOption.DisallowedList && CurveEvalOption.DisallowedList->Contains(CurveName))
						{
							//remove the UID
							bBeingUsed = false;
						}
						else
						{
							const FCurveMetaData* CurveMetaData = Mapping->GetCurveMetaData(CurveNames[CurveNameIndex]);
							if (CurveMetaData)
							{
								if (CurveMetaData->MaxLOD < CurveEvalOption.LODIndex)
								{
									bBeingUsed = false;
								}
								else if (CurveMetaData->LinkedBones.Num() > 0)
								{
									bBeingUsed = false;
									for (int32 LinkedBoneIndex = 0; LinkedBoneIndex < CurveMetaData->LinkedBones.Num(); ++LinkedBoneIndex)
									{
										const FBoneReference& BoneReference = CurveMetaData->LinkedBones[LinkedBoneIndex];
										// we want to make sure all the joints are removed from RequiredBones before removing this UID
										if (BoneReference.GetCompactPoseIndex(*this) != INDEX_NONE)
										{
											// still has some joint that matters, do not remove
											bBeingUsed = true;
											break;
										}
									}
								}
							}
						}
					}

					if (bBeingUsed)
					{
						UIDToArrayIndexLUT[UIDList[CurveNameIndex]] = NumAvailableUIDs++;
					}
				}
			}
		}
	}
	else
	{
		UIDToArrayIndexLUT.Reset();
	}
}

const FRetargetSourceCachedData& FBoneContainer::GetRetargetSourceCachedData(const FName& InRetargetSourceName) const
{
	FRetargetSourceCachedData* RetargetSourceCachedData = RetargetSourceCachedDataLUT.Find(InRetargetSourceName);
	if (!RetargetSourceCachedData)
	{
		RetargetSourceCachedData = &RetargetSourceCachedDataLUT.Add(InRetargetSourceName);

		// Build Cached Data for OrientAndScale retargeting.

		const TArray<FTransform>& AuthoredOnRefSkeleton = AssetSkeleton->GetRefLocalPoses(InRetargetSourceName);
		const TArray<FTransform>& PlayingOnRefSkeleton = GetRefPoseCompactArray();
		const int32 CompactPoseNumBones = GetCompactPoseNumBones();

		RetargetSourceCachedData->CompactPoseIndexToOrientAndScaleIndex.Reset();

		for (int32 CompactBoneIndex = 0; CompactBoneIndex < CompactPoseNumBones; CompactBoneIndex++)
		{
			const int32& SkeletonBoneIndex = CompactPoseToSkeletonIndex[CompactBoneIndex];

			if (AssetSkeleton->GetBoneTranslationRetargetingMode(SkeletonBoneIndex) == EBoneTranslationRetargetingMode::OrientAndScale)
			{
				const FVector SourceSkelTrans = AuthoredOnRefSkeleton[SkeletonBoneIndex].GetTranslation();
				const FVector TargetSkelTrans = PlayingOnRefSkeleton[CompactBoneIndex].GetTranslation();

				// If translations are identical, we don't need to do any retargeting
				if (!SourceSkelTrans.Equals(TargetSkelTrans, BONE_TRANS_RT_ORIENT_AND_SCALE_PRECISION))
				{
					const float SourceSkelTransLength = SourceSkelTrans.Size();
					const float TargetSkelTransLength = TargetSkelTrans.Size();

					// this only works on non zero vectors.
					if (!FMath::IsNearlyZero(SourceSkelTransLength * TargetSkelTransLength))
					{
						const FVector SourceSkelTransDir = SourceSkelTrans / SourceSkelTransLength;
						const FVector TargetSkelTransDir = TargetSkelTrans / TargetSkelTransLength;

						const FQuat DeltaRotation = FQuat::FindBetweenNormals(SourceSkelTransDir, TargetSkelTransDir);
						const float Scale = TargetSkelTransLength / SourceSkelTransLength;
						const int32 OrientAndScaleIndex = RetargetSourceCachedData->OrientAndScaleData.Add(FOrientAndScaleRetargetingCachedData(DeltaRotation, Scale, SourceSkelTrans, TargetSkelTrans));

						// initialize CompactPoseBoneIndex to OrientAndScale Index LUT on demand
						if (RetargetSourceCachedData->CompactPoseIndexToOrientAndScaleIndex.Num() == 0)
						{
							RetargetSourceCachedData->CompactPoseIndexToOrientAndScaleIndex.Init(INDEX_NONE, CompactPoseNumBones);
						}

						RetargetSourceCachedData->CompactPoseIndexToOrientAndScaleIndex[CompactBoneIndex] = OrientAndScaleIndex;
					}
				}
			}
		}
	}

	return *RetargetSourceCachedData;
}

int32 FBoneContainer::GetPoseBoneIndexForBoneName(const FName& BoneName) const
{
	checkSlow( IsValid() );
	return RefSkeleton->FindBoneIndex(BoneName);
}

int32 FBoneContainer::GetParentBoneIndex(const int32 BoneIndex) const
{
	checkSlow( IsValid() );
	checkSlow(BoneIndex != INDEX_NONE);
	return RefSkeleton->GetParentIndex(BoneIndex);
}

FCompactPoseBoneIndex FBoneContainer::GetParentBoneIndex(const FCompactPoseBoneIndex& BoneIndex) const
{
	checkSlow(IsValid());
	checkSlow(BoneIndex != INDEX_NONE);
	return CompactPoseParentBones[BoneIndex.GetInt()];
}

int32 FBoneContainer::GetDepthBetweenBones(const int32 BoneIndex, const int32 ParentBoneIndex) const
{
	checkSlow( IsValid() );
	checkSlow( BoneIndex != INDEX_NONE );
	return RefSkeleton->GetDepthBetweenBones(BoneIndex, ParentBoneIndex);
}

bool FBoneContainer::BoneIsChildOf(const int32 BoneIndex, const int32 ParentBoneIndex) const
{
	checkSlow( IsValid() );
	checkSlow( (BoneIndex != INDEX_NONE) && (ParentBoneIndex != INDEX_NONE) );
	return RefSkeleton->BoneIsChildOf(BoneIndex, ParentBoneIndex);
}

bool FBoneContainer::BoneIsChildOf(const FCompactPoseBoneIndex& BoneIndex, const FCompactPoseBoneIndex& ParentBoneIndex) const
{
	checkSlow(IsValid());
	checkSlow((BoneIndex != INDEX_NONE) && (ParentBoneIndex != INDEX_NONE));

	// Bones are in strictly increasing order.
	// So child must have an index greater than his parent.
	if (BoneIndex > ParentBoneIndex)
	{
		FCompactPoseBoneIndex SearchBoneIndex = GetParentBoneIndex(BoneIndex);
		do
		{
			if (SearchBoneIndex == ParentBoneIndex)
			{
				return true;
			}
			SearchBoneIndex = GetParentBoneIndex(SearchBoneIndex);

		} while (SearchBoneIndex != INDEX_NONE);
	}

	return false;
}

void FBoneContainer::RemapFromSkelMesh(USkeletalMesh const & SourceSkeletalMesh, USkeleton& TargetSkeleton)
{
	int32 const SkelMeshLinkupIndex = TargetSkeleton.GetMeshLinkupIndex(&SourceSkeletalMesh);
	check(SkelMeshLinkupIndex != INDEX_NONE);

	FSkeletonToMeshLinkup const & LinkupTable = TargetSkeleton.LinkupCache[SkelMeshLinkupIndex];

	// Copy LinkupTable arrays for now.
	// @laurent - Long term goal is to trim that down based on LOD, so we can get rid of the BoneIndicesArray and branch cost of testing if PoseBoneIndex is in that required bone index array.
	SkeletonToPoseBoneIndexArray = LinkupTable.SkeletonToMeshTable;
	PoseToSkeletonBoneIndexArray = LinkupTable.MeshToSkeletonTable;
}

void FBoneContainer::RemapFromSkeleton(USkeleton const & SourceSkeleton)
{
	// Map SkeletonBoneIndex to the SkeletalMesh Bone Index, taking into account the required bone index array.
	SkeletonToPoseBoneIndexArray.Init(INDEX_NONE, SourceSkeleton.GetRefLocalPoses().Num());
	for(int32 Index=0; Index<BoneIndicesArray.Num(); Index++)
	{
		int32 const & PoseBoneIndex = BoneIndicesArray[Index];
		SkeletonToPoseBoneIndexArray[PoseBoneIndex] = PoseBoneIndex;
	}

	// Skeleton to Skeleton mapping...
	PoseToSkeletonBoneIndexArray = SkeletonToPoseBoneIndexArray;
}


/////////////////////////////////////////////////////
// FBoneReference

bool FBoneReference::Initialize(const FBoneContainer& RequiredBones)
{
	BoneName = *BoneName.ToString().TrimStartAndEnd();
	BoneIndex = RequiredBones.GetPoseBoneIndexForBoneName(BoneName);

	bUseSkeletonIndex = false;
	// If bone name is not found, look into the master skeleton to see if it's found there.
	// SkeletalMeshes can exclude bones from the master skeleton, and that's OK.
	// If it's not found in the master skeleton, the bone does not exist at all! so we should report it as a warning.
	if (BoneIndex == INDEX_NONE && BoneName != NAME_None)
	{
		if (USkeleton* SkeletonAsset = RequiredBones.GetSkeletonAsset())
		{
			if (SkeletonAsset->GetReferenceSkeleton().FindBoneIndex(BoneName) == INDEX_NONE)
			{
				UE_LOG(LogAnimation, Warning, TEXT("FBoneReference::Initialize BoneIndex for Bone '%s' does not exist in Skeleton '%s'"),
					*BoneName.ToString(), *GetNameSafe(SkeletonAsset));
			}
		}
	}

	CachedCompactPoseIndex = RequiredBones.MakeCompactPoseIndex(GetMeshPoseIndex(RequiredBones));

	return (BoneIndex != INDEX_NONE);
}

bool FBoneReference::Initialize(const USkeleton* Skeleton)
{
	if (Skeleton && (BoneName != NAME_None))
	{
		BoneName = *BoneName.ToString().TrimStartAndEnd();
		BoneIndex = Skeleton->GetReferenceSkeleton().FindBoneIndex(BoneName);
		bUseSkeletonIndex = true;
	}
	else
	{
		BoneIndex = INDEX_NONE;
	}

	CachedCompactPoseIndex = FCompactPoseBoneIndex(INDEX_NONE);

	return (BoneIndex != INDEX_NONE);
}

bool FBoneReference::IsValidToEvaluate(const FBoneContainer& RequiredBones) const
{
	return (BoneIndex != INDEX_NONE && RequiredBones.Contains(BoneIndex));
}

bool FBoneReference::IsValid(const FBoneContainer& RequiredBones) const
{
	return IsValidToEvaluate(RequiredBones);
}
