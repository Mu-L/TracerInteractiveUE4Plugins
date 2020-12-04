// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHIDefinitions.h"
#include "Containers/IndirectArray.h"
#include "Rendering/SkeletalMeshLODRenderData.h"

struct FMeshUVChannelInfo;
class USkeletalMesh;
struct FSkeletalMaterial;
class UMorphTarget;
struct FResourceSizeEx;

class FSkeletalMeshRenderData
{
public:
	/** Per-LOD render data. */
	TIndirectArray<FSkeletalMeshLODRenderData> LODRenderData;

	/** True if rhi resources are initialized */
	bool bReadyForStreaming;

	/** Const after serialization. */
	uint8 NumInlinedLODs;

	/** Const after serialization. */
	uint8 NumNonOptionalLODs;

	/** [RenderThread] Index of the most detailed valid LOD. */
	uint8 CurrentFirstLODIdx;

	/** [GameThread/RenderThread] Future value of CurrentFirstLODIdx. */
	uint8 PendingFirstLODIdx;

#if WITH_EDITORONLY_DATA
	/** UV data used for streaming accuracy debug view modes. In sync for rendering thread */
	TArray<FMeshUVChannelInfo> UVChannelDataPerMaterial;
#endif

	FSkeletalMeshRenderData();
	~FSkeletalMeshRenderData();

#if WITH_EDITOR
	void Cache(const ITargetPlatform* TargetPlatform, USkeletalMesh* Owner);
	FString GetDerivedDataKey(const ITargetPlatform* TargetPlatform, USkeletalMesh* Owner);

	void SyncUVChannelData(const TArray<FSkeletalMaterial>& ObjectData);
#endif

	/** Serialize to/from the specified archive.. */
	void Serialize(FArchive& Ar, USkeletalMesh* Owner);

	/** Initializes rendering resources. */
	void InitResources(bool bNeedsVertexColors, TArray<UMorphTarget*>& InMorphTargets, USkeletalMesh* Owner);

	/** Releases rendering resources. */
	ENGINE_API void ReleaseResources();

	/** Return the resource size */
	void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize);

	/** Returns true if this resource must be skinned on the CPU for the given feature level. */
	ENGINE_API bool RequiresCPUSkinning(ERHIFeatureLevel::Type FeatureLevel) const;
	
	/** Returns true if this resource must be skinned on the CPU for the given feature level starting at MinLODIdx */
	ENGINE_API bool RequiresCPUSkinning(ERHIFeatureLevel::Type FeatureLevel, int32 MinLODIdx) const;

	/** Returns the number of bone influences per vertex. */
	uint32 GetNumBoneInfluences() const;

	/** Returns the number of bone influences per vertex starting at MinLODIdx. */
	uint32 GetNumBoneInfluences(int32 MinLODIdx) const;
	
	/**
	* Computes the maximum number of bones per section used to render this mesh.
	*/
	ENGINE_API int32 GetMaxBonesPerSection() const;
	
	/**
	* Computes the maximum number of bones per section used to render this mesh starting at MinLODIdx.
	*/
	ENGINE_API int32 GetMaxBonesPerSection(int32 MinLODIdx) const;
	
	/** Return first valid LOD index starting at MinLODIdx. */
	ENGINE_API int32 GetFirstValidLODIdx(int32 MinLODIdx) const;

	/** Return the pending first LODIdx that can be used. */
	FORCEINLINE int32 GetPendingFirstLODIdx(int32 MinLODIdx) const
	{
		return GetFirstValidLODIdx(FMath::Max<int32>(PendingFirstLODIdx, MinLODIdx));
	}

	/** 
	 * Return the pending first LOD that can be used for rendering starting at MinLODIdx.
	 * This takes into account the streaming status from PendingFirstLODIdx, 
	 * and MinLODIdx is expected to be USkeletalMesh::MinLOD, which is platform specific.
	 */
	FORCEINLINE const FSkeletalMeshLODRenderData* GetPendingFirstLOD(int32 MinLODIdx) const
	{
		const int32 PendingFirstIdx = GetPendingFirstLODIdx(MinLODIdx);
		return PendingFirstIdx == INDEX_NONE ? nullptr : &LODRenderData[PendingFirstIdx];
	}

private:

	/** Count the number of LODs that are inlined and not streamable. Starting from the last LOD and stopping at the first non inlined LOD. */
	int32 GetNumNonStreamingLODs() const;
	/** Count the number of LODs that not optional and guarantied to be installed. Starting from the last LOD and stopping at the first optional LOD. */
	int32 GetNumNonOptionalLODs() const;

	/** True if the resource has been initialized. */
	bool bInitialized = false;
};