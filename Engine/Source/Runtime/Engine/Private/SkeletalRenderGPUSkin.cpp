// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SkeletalRenderGPUSkin.cpp: GPU skinned skeletal mesh rendering code.
=============================================================================*/

#include "SkeletalRenderGPUSkin.h"
#include "Components/SkeletalMeshComponent.h"
#include "SceneUtils.h"
#include "SkeletalRender.h"
#include "GPUSkinCache.h"
#include "Animation/MorphTarget.h"
#include "ClearQuad.h"
#include "ShaderParameterUtils.h"
#include "SkeletalMeshTypes.h"
#include "HAL/LowLevelMemTracker.h"

DEFINE_LOG_CATEGORY_STATIC(LogSkeletalGPUSkinMesh, Warning, All);

// 0/1
#define UPDATE_PER_BONE_DATA_ONLY_FOR_OBJECT_BEEN_VISIBLE 1

DECLARE_CYCLE_STAT(TEXT("Morph Vertex Buffer Update"),STAT_MorphVertexBuffer_Update,STATGROUP_MorphTarget);
DECLARE_CYCLE_STAT(TEXT("Morph Vertex Buffer Init"),STAT_MorphVertexBuffer_Init,STATGROUP_MorphTarget);
DECLARE_CYCLE_STAT(TEXT("Morph Vertex Buffer Apply Delta"),STAT_MorphVertexBuffer_ApplyDelta,STATGROUP_MorphTarget);
DECLARE_CYCLE_STAT(TEXT("Morph Vertex Buffer Alloc"), STAT_MorphVertexBuffer_Alloc, STATGROUP_MorphTarget);
DECLARE_CYCLE_STAT(TEXT("Morph Vertex Buffer RHI Lock and copy"), STAT_MorphVertexBuffer_RhiLockAndCopy, STATGROUP_MorphTarget);
DECLARE_CYCLE_STAT(TEXT("Morph Vertex Buffer RHI Unlock"), STAT_MorphVertexBuffer_RhiUnlock, STATGROUP_MorphTarget);
DECLARE_GPU_STAT_NAMED(MorphTargets, TEXT("Morph Target Compute"));

static TAutoConsoleVariable<int32> CVarMotionBlurDebug(
	TEXT("r.MotionBlurDebug"),
	0,
	TEXT("Defines if we log debugging output for motion blur rendering.\n")
	TEXT(" 0: off (default)\n")
	TEXT(" 1: on"),
	ECVF_Cheat | ECVF_RenderThreadSafe);

static int32 GUseGPUMorphTargets = 1;
static FAutoConsoleVariableRef CVarUseGPUMorphTargets(
	TEXT("r.MorphTarget.Mode"),
	GUseGPUMorphTargets,
	TEXT("Use GPU for computing morph targets.\n")
	TEXT(" 0: Use original CPU method (loop per morph then by vertex)\n")
	TEXT(" 1: Enable GPU method (default)\n"),
	ECVF_Default
	);

static int32 GForceUpdateMorphTargets = 0;
static FAutoConsoleVariableRef CVarForceUpdateMorphTargets(
	TEXT("r.MorphTarget.ForceUpdate"),
	GForceUpdateMorphTargets,
	TEXT("Force morph target deltas to be calculated every frame.\n")
	TEXT(" 0: Default\n")
	TEXT(" 1: Force Update\n"),
	ECVF_Default
	);

static bool UseGPUMorphTargets(const EShaderPlatform Platform)
{
	return GUseGPUMorphTargets != 0 && IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
}

static float GMorphTargetWeightThreshold = SMALL_NUMBER;
static FAutoConsoleVariableRef CVarMorphTargetWeightThreshold(
	TEXT("r.MorphTarget.WeightThreshold"),
	GMorphTargetWeightThreshold,
	*FString::Printf(TEXT("Set MorphTarget Weight Threshold (Default : %f).\n"), SMALL_NUMBER), 
	ECVF_Default
);


/*-----------------------------------------------------------------------------
FMorphVertexBuffer
-----------------------------------------------------------------------------*/

void FMorphVertexBuffer::InitDynamicRHI()
{
	// LOD of the skel mesh is used to find number of vertices in buffer
	FSkeletalMeshLODRenderData& LodData = SkelMeshRenderData->LODRenderData[LODIdx];

	// Create the buffer rendering resource
	uint32 Size = LodData.GetNumVertices() * sizeof(FMorphGPUSkinVertex);
	FRHIResourceCreateInfo CreateInfo;
	CreateInfo.DebugName = TEXT("MorphVertexBuffer");

	const bool bUseGPUMorphTargets = UseGPUMorphTargets(GMaxRHIShaderPlatform);
	bUsesComputeShader = bUseGPUMorphTargets;

#if PLATFORM_PS4
	// PS4 requires non-static buffers in order to be updated on the GPU while the CPU is writing into it
	EBufferUsageFlags Flags = bUseGPUMorphTargets ? (EBufferUsageFlags)(BUF_Dynamic | BUF_UnorderedAccess) : BUF_Dynamic;
#else
	EBufferUsageFlags Flags = bUseGPUMorphTargets ? (EBufferUsageFlags)(BUF_Static | BUF_UnorderedAccess) : BUF_Dynamic;
#endif

	// BUF_ShaderResource is needed for Morph support of the SkinCache
	Flags = (EBufferUsageFlags)(Flags | BUF_ShaderResource);

	VertexBufferRHI = RHICreateVertexBuffer(Size, Flags, CreateInfo);
	bool bUsesSkinCache = RHISupportsComputeShaders(GMaxRHIShaderPlatform) && IsGPUSkinCacheAvailable(GMaxRHIShaderPlatform) && GEnableGPUSkinCache;
	if (bUsesSkinCache)
	{
		SRVValue = RHICreateShaderResourceView(VertexBufferRHI, 4, PF_R32_FLOAT);
	}

	if (!bUseGPUMorphTargets)
	{
		// Lock the buffer.
		void* BufferData = RHILockVertexBuffer(VertexBufferRHI, 0, sizeof(FMorphGPUSkinVertex)*LodData.GetNumVertices(), RLM_WriteOnly);
		FMorphGPUSkinVertex* Buffer = (FMorphGPUSkinVertex*)BufferData;
		FMemory::Memzero(&Buffer[0], sizeof(FMorphGPUSkinVertex)*LodData.GetNumVertices());
		// Unlock the buffer.
		RHIUnlockVertexBuffer(VertexBufferRHI);
		bNeedsInitialClear = false;
	}
	else
	{
		UAVValue = RHICreateUnorderedAccessView(VertexBufferRHI, PF_R32_UINT);
		bNeedsInitialClear = true;
	}

	// hasn't been updated yet
	bHasBeenUpdated = false;
}

void FMorphVertexBuffer::ReleaseDynamicRHI()
{
	UAVValue.SafeRelease();
	VertexBufferRHI.SafeRelease();
	SRVValue.SafeRelease();
}

/*-----------------------------------------------------------------------------
FSkeletalMeshObjectGPUSkin
-----------------------------------------------------------------------------*/
FSkeletalMeshObjectGPUSkin::FSkeletalMeshObjectGPUSkin(USkinnedMeshComponent* InMeshComponent, FSkeletalMeshRenderData* InSkelMeshRenderData, ERHIFeatureLevel::Type InFeatureLevel)
	: FSkeletalMeshObject(InMeshComponent, InSkelMeshRenderData, InFeatureLevel)
	,	DynamicData(NULL)
	,	bNeedsUpdateDeferred(false)
	,	bMorphNeedsUpdateDeferred(false)
	,	bMorphResourcesInitialized(false)
	, 	LastBoneTransformRevisionNumber(0)
{
	// create LODs to match the base mesh
	LODs.Empty(SkeletalMeshRenderData->LODRenderData.Num());
	for( int32 LODIndex=0;LODIndex < SkeletalMeshRenderData->LODRenderData.Num();LODIndex++ )
	{
		new(LODs) FSkeletalMeshObjectLOD(SkeletalMeshRenderData,LODIndex);
	}

	InitResources(InMeshComponent);
}


FSkeletalMeshObjectGPUSkin::~FSkeletalMeshObjectGPUSkin()
{
	check(!RHIThreadFenceForDynamicData.GetReference());
	if (DynamicData)
	{
		FDynamicSkelMeshObjectDataGPUSkin::FreeDynamicSkelMeshObjectDataGPUSkin(DynamicData);
	}
	DynamicData = nullptr;
}


void FSkeletalMeshObjectGPUSkin::InitResources(USkinnedMeshComponent* InMeshComponent)
{
	for( int32 LODIndex=0;LODIndex < LODs.Num();LODIndex++ )
	{
		FSkeletalMeshObjectLOD& SkelLOD = LODs[LODIndex];
		
		// Skip LODs that have their render data stripped
		if (SkelLOD.SkelMeshRenderData && SkelLOD.SkelMeshRenderData->LODRenderData.IsValidIndex(LODIndex) && SkelLOD.SkelMeshRenderData->LODRenderData[LODIndex].GetNumVertices() > 0)
		{
			const FSkelMeshObjectLODInfo& MeshLODInfo = LODInfo[LODIndex];

			FSkelMeshComponentLODInfo* CompLODInfo = nullptr;
			if (InMeshComponent->LODInfo.IsValidIndex(LODIndex))
			{
				CompLODInfo = &InMeshComponent->LODInfo[LODIndex];
			}

			SkelLOD.InitResources(MeshLODInfo, CompLODInfo, FeatureLevel);
		}
	}

#if RHI_RAYTRACING
	BeginInitResource(&RayTracingGeometry);
#endif
}

void FSkeletalMeshObjectGPUSkin::ReleaseResources()
{
	for( int32 LODIndex=0;LODIndex < LODs.Num();LODIndex++ )
	{
		FSkeletalMeshObjectLOD& SkelLOD = LODs[LODIndex];
		
		// Skip LODs that have their render data stripped
		if (SkelLOD.SkelMeshRenderData && SkelLOD.SkelMeshRenderData->LODRenderData.IsValidIndex(LODIndex) && SkelLOD.SkelMeshRenderData->LODRenderData[LODIndex].GetNumVertices() > 0)
		{
			SkelLOD.ReleaseResources();
		}
	}
	// also release morph resources
	ReleaseMorphResources();
	FSkeletalMeshObjectGPUSkin* MeshObject = this;
	FGPUSkinCacheEntry** PtrSkinCacheEntry = &SkinCacheEntry;
	ENQUEUE_RENDER_COMMAND(WaitRHIThreadFenceForDynamicData)(
		[MeshObject, PtrSkinCacheEntry](FRHICommandList& RHICmdList)
		{
			FGPUSkinCacheEntry*& LocalSkinCacheEntry = *PtrSkinCacheEntry;
			FGPUSkinCache::Release(LocalSkinCacheEntry);

			FScopeCycleCounter Context(MeshObject->GetStatId());
			MeshObject->WaitForRHIThreadFenceForDynamicData();
			*PtrSkinCacheEntry = nullptr;
		}
	);

#if RHI_RAYTRACING
	BeginReleaseResource(&RayTracingGeometry);
	ENQUEUE_RENDER_COMMAND(ReleaseRayTracingDynamicVertexBuffer)(
		[RayTracingDynamicVertexBuffer = MoveTemp(RayTracingDynamicVertexBuffer)](FRHICommandListImmediate& RHICmdList) mutable
	{
		RayTracingDynamicVertexBuffer.Release();
	});

	check(!RayTracingDynamicVertexBuffer.Buffer.IsValid());
#endif
}

void FSkeletalMeshObjectGPUSkin::InitMorphResources(bool bInUsePerBoneMotionBlur, const TArray<float>& MorphTargetWeights)
{
	if( bMorphResourcesInitialized )
	{
		// release first if already initialized
		ReleaseMorphResources();
	}

	for( int32 LODIndex=0;LODIndex < LODs.Num();LODIndex++ )
	{
		FSkeletalMeshObjectLOD& SkelLOD = LODs[LODIndex];

		// Check the LOD render data for verts, if it's been stripped we don't create morph buffers
		const int32 LodIndexInMesh = SkelLOD.LODIndex;
		const FSkeletalMeshLODRenderData& RenderData = SkelLOD.SkelMeshRenderData->LODRenderData[LodIndexInMesh];

		if(RenderData.GetNumVertices() > 0)
		{
			// init any morph vertex buffers for each LOD
			const FSkelMeshObjectLODInfo& MeshLODInfo = LODInfo[LODIndex];
			SkelLOD.InitMorphResources(MeshLODInfo, bInUsePerBoneMotionBlur, FeatureLevel);
		}
	}
	bMorphResourcesInitialized = true;
}

void FSkeletalMeshObjectGPUSkin::ReleaseMorphResources()
{
	for( int32 LODIndex=0;LODIndex < LODs.Num();LODIndex++ )
	{
		FSkeletalMeshObjectLOD& SkelLOD = LODs[LODIndex];

		// release morph vertex buffers and factories if they were created
		SkelLOD.ReleaseMorphResources();
	}

	bMorphResourcesInitialized = false;
}

void FSkeletalMeshObjectGPUSkin::Update(int32 LODIndex,USkinnedMeshComponent* InMeshComponent,const TArray<FActiveMorphTarget>& ActiveMorphTargets, const TArray<float>& MorphTargetWeights, EPreviousBoneTransformUpdateMode PreviousBoneTransformUpdateMode)
{
	LLM_SCOPE(ELLMTag::SkeletalMesh);

	// make sure morph data has been initialized for each LOD
	if(InMeshComponent && !bMorphResourcesInitialized && ActiveMorphTargets.Num() > 0 )
	{
		// initialized on-the-fly in order to avoid creating extra vertex streams for each skel mesh instance
		InitMorphResources(InMeshComponent->bPerBoneMotionBlur, MorphTargetWeights);		
	}

	// create the new dynamic data for use by the rendering thread
	// this data is only deleted when another update is sent
	FDynamicSkelMeshObjectDataGPUSkin* NewDynamicData = FDynamicSkelMeshObjectDataGPUSkin::AllocDynamicSkelMeshObjectDataGPUSkin();		
	NewDynamicData->InitDynamicSkelMeshObjectDataGPUSkin(InMeshComponent,SkeletalMeshRenderData,LODIndex,ActiveMorphTargets, MorphTargetWeights, PreviousBoneTransformUpdateMode);

	// We prepare the next frame but still have the value from the last one
	uint32 FrameNumberToPrepare = GFrameNumber + 1;
	uint32 RevisionNumber = 0;

	FGPUSkinCache* GPUSkinCache = nullptr;
	if (InMeshComponent && InMeshComponent->SceneProxy)
	{
		// We allow caching of per-frame, per-scene data
		FrameNumberToPrepare = InMeshComponent->SceneProxy->GetScene().GetFrameNumber() + 1;
		GPUSkinCache = InMeshComponent->SceneProxy->GetScene().GetGPUSkinCache();
		RevisionNumber = InMeshComponent->GetBoneTransformRevisionNumber();
	}

	// queue a call to update this data
	FSkeletalMeshObjectGPUSkin* MeshObject = this;
	ENQUEUE_RENDER_COMMAND(SkelMeshObjectUpdateDataCommand)(
		[MeshObject, FrameNumberToPrepare, RevisionNumber, NewDynamicData, GPUSkinCache](FRHICommandListImmediate& RHICmdList)
		{
			FScopeCycleCounter Context(MeshObject->GetStatId());
			MeshObject->UpdateDynamicData_RenderThread(GPUSkinCache, RHICmdList, NewDynamicData, nullptr, FrameNumberToPrepare, RevisionNumber);
		}
	);
}

void FSkeletalMeshObjectGPUSkin::UpdateSkinWeightBuffer(USkinnedMeshComponent* InMeshComponent)
{
	for (int32 LODIndex = 0; LODIndex < LODs.Num(); LODIndex++)
	{
		FSkeletalMeshObjectLOD& SkelLOD = LODs[LODIndex];

		// Skip LODs that have their render data stripped
		if (InMeshComponent && SkelLOD.SkelMeshRenderData->LODRenderData[LODIndex].GetNumVertices() > 0)
		{
			FSkelMeshComponentLODInfo* CompLODInfo = nullptr;
			if (InMeshComponent->LODInfo.IsValidIndex(LODIndex))
			{
				CompLODInfo = &InMeshComponent->LODInfo[LODIndex];
			}

			SkelLOD.UpdateSkinWeights(CompLODInfo);

			if (InMeshComponent && InMeshComponent->SceneProxy)
			{
				FGPUSkinCache* GPUSkinCache = InMeshComponent->SceneProxy->GetScene().GetGPUSkinCache();
				FGPUSkinCacheEntry* SkinCacheEntryToUpdate = SkinCacheEntry;
				if (GPUSkinCache && SkinCacheEntryToUpdate)
				{
					ENQUEUE_RENDER_COMMAND(UpdateSkinCacheSkinWeightBuffer)(
						[GPUSkinCache, SkinCacheEntryToUpdate](FRHICommandListImmediate& RHICmdList)
					{
						GPUSkinCache->UpdateSkinWeightBuffer(SkinCacheEntryToUpdate);
					});
				}
			}
		}
	}

}

static TAutoConsoleVariable<int32> CVarDeferSkeletalDynamicDataUpdateUntilGDME(
	TEXT("r.DeferSkeletalDynamicDataUpdateUntilGDME"),
	0,
	TEXT("If > 0, then do skeletal mesh dynamic data updates will be deferred until GDME. Experimental option."));

void FSkeletalMeshObjectGPUSkin::UpdateDynamicData_RenderThread(FGPUSkinCache* GPUSkinCache, FRHICommandListImmediate& RHICmdList, FDynamicSkelMeshObjectDataGPUSkin* InDynamicData, FSceneInterface* Scene, uint32 FrameNumberToPrepare, uint32 RevisionNumber)
{
	SCOPE_CYCLE_COUNTER(STAT_GPUSkinUpdateRTTime);
	check(InDynamicData != nullptr);
	CA_ASSUME(InDynamicData != nullptr);
	bool bMorphNeedsUpdate=false;
	// figure out if the morphing vertex buffer needs to be updated. compare old vs new active morphs
	bMorphNeedsUpdate = 
		(bMorphNeedsUpdateDeferred && bNeedsUpdateDeferred) || // the need for an update sticks
		(DynamicData ? (DynamicData->LODIndex != InDynamicData->LODIndex ||
		!DynamicData->ActiveMorphTargetsEqual(InDynamicData->ActiveMorphTargets, InDynamicData->MorphTargetWeights))
		: true);

#if RHI_RAYTRACING
	bRequireRecreatingRayTracingGeometry = (DynamicData == nullptr || RayTracingGeometry.Initializer.Segments.Num() == 0 || // Newly created
		(DynamicData != nullptr && DynamicData->LODIndex != InDynamicData->LODIndex)); // LOD level changed
	
	if (!bRequireRecreatingRayTracingGeometry)
	{
		for (FRayTracingGeometrySegment& Segment : RayTracingGeometry.Initializer.Segments)
		{
			if (Segment.VertexBuffer == nullptr)
			{
				bRequireRecreatingRayTracingGeometry = true;
				break;
			}
		}
	}
#endif // RHI_RAYTRACING

	WaitForRHIThreadFenceForDynamicData();
	if (DynamicData)
	{
		FDynamicSkelMeshObjectDataGPUSkin::FreeDynamicSkelMeshObjectDataGPUSkin(DynamicData);
	}
	// update with new data
	DynamicData = InDynamicData;
	LastBoneTransformRevisionNumber = RevisionNumber;

	if (CVarDeferSkeletalDynamicDataUpdateUntilGDME.GetValueOnRenderThread())
	{
		bMorphNeedsUpdateDeferred = bMorphNeedsUpdate;
		bNeedsUpdateDeferred = true;
	}
	else
	{
		ProcessUpdatedDynamicData(GPUSkinCache, RHICmdList, FrameNumberToPrepare, RevisionNumber, bMorphNeedsUpdate);
	}

#if RHI_RAYTRACING
	if (IsRayTracingEnabled())
	{
		if (GEnableGPUSkinCache && SkinCacheEntry)
		{
			if (bRequireRecreatingRayTracingGeometry)
			{
				FSkeletalMeshLODRenderData& LODModel = this->SkeletalMeshRenderData->LODRenderData[DynamicData->LODIndex];
				FIndexBufferRHIRef IndexBufferRHI = LODModel.MultiSizeIndexContainer.GetIndexBuffer()->IndexBufferRHI;
				uint32 VertexBufferStride = LODModel.StaticVertexBuffers.PositionVertexBuffer.GetStride();

				//#dxr_todo: do we need support for separate sections in FRayTracingGeometryData?
				uint32 TrianglesCount = 0;
				for (int32 SectionIndex = 0; SectionIndex < LODModel.RenderSections.Num(); SectionIndex++)
				{
					const FSkelMeshRenderSection& Section = LODModel.RenderSections[SectionIndex];
					TrianglesCount += Section.NumTriangles;
				}

				FRayTracingGeometryInitializer Initializer;

				FRHIResourceCreateInfo CreateInfo;

				Initializer.IndexBuffer = IndexBufferRHI;
				Initializer.TotalPrimitiveCount = TrianglesCount;
				Initializer.GeometryType = RTGT_Triangles;
				Initializer.bFastBuild = true;
				Initializer.bAllowUpdate = true;

				Initializer.Segments.Reserve(LODModel.RenderSections.Num());
				for (const FSkelMeshRenderSection& Section : LODModel.RenderSections)
				{
					FRayTracingGeometrySegment Segment;
					Segment.VertexBuffer = nullptr;
					Segment.VertexBufferElementType = VET_Float3;
					Segment.VertexBufferStride = VertexBufferStride;
					Segment.VertexBufferOffset = 0;
					Segment.FirstPrimitive = Section.BaseIndex / 3;
					Segment.NumPrimitives = Section.NumTriangles;
					Segment.bEnabled = !Section.bDisabled;
					Initializer.Segments.Add(Segment);
				}

				FGPUSkinCache::GetRayTracingSegmentVertexBuffers(*SkinCacheEntry, Initializer.Segments);

				// Flush pending resource barriers before BVH is built for the first time
				GPUSkinCache->TransitionAllToReadable(RHICmdList);

				RayTracingGeometry.SetInitializer(Initializer);
				RayTracingGeometry.UpdateRHI();
			}
			else
			{
				// If we are not using world position offset in material, handle BLAS refit here
				if (!DynamicData->bAnySegmentUsesWorldPositionOffset)
				{
					// Refit BLAS with new vertex buffer data
					FGPUSkinCache::GetRayTracingSegmentVertexBuffers(*SkinCacheEntry, RayTracingGeometry.Initializer.Segments);
					GPUSkinCache->AddRayTracingGeometryToUpdate(&RayTracingGeometry);
				}
				else
				{
					// Otherwise, we will run the dynamic ray tracing geometry path, i.e. runnning VSinCS and refit geometry there, so do nothing here
				}
			}
		}
	}
#endif
}

void FSkeletalMeshObjectGPUSkin::PreGDMECallback(FGPUSkinCache* GPUSkinCache, uint32 FrameNumber)
{
	if (bNeedsUpdateDeferred)
	{
		ProcessUpdatedDynamicData(GPUSkinCache, FRHICommandListExecutor::GetImmediateCommandList(), FrameNumber, LastBoneTransformRevisionNumber, bMorphNeedsUpdateDeferred);
	}
}

void FSkeletalMeshObjectGPUSkin::WaitForRHIThreadFenceForDynamicData()
{
	// we should be done with the old data at this point
	if (RHIThreadFenceForDynamicData.GetReference())
	{
		FRHICommandListExecutor::WaitOnRHIThreadFence(RHIThreadFenceForDynamicData);
		RHIThreadFenceForDynamicData = nullptr;
	}
}

void FSkeletalMeshObjectGPUSkin::ProcessUpdatedDynamicData(FGPUSkinCache* GPUSkinCache, FRHICommandListImmediate& RHICmdList, uint32 FrameNumberToPrepare, uint32 RevisionNumber, bool bMorphNeedsUpdate)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FSkeletalMeshObjectGPUSkin_ProcessUpdatedDynamicData);
	bNeedsUpdateDeferred = false;
	bMorphNeedsUpdateDeferred = false;

	FSkeletalMeshObjectLOD& LOD = LODs[DynamicData->LODIndex];

	// if hasn't been updated, force update again
	bMorphNeedsUpdate = LOD.MorphVertexBuffer.bHasBeenUpdated ? bMorphNeedsUpdate : true;
	bMorphNeedsUpdate |= (GForceUpdateMorphTargets != 0);

	const FSkeletalMeshLODRenderData& LODData = SkeletalMeshRenderData->LODRenderData[DynamicData->LODIndex];
	const TArray<FSkelMeshRenderSection>& Sections = GetRenderSections(DynamicData->LODIndex);

	// Only consider morphs with active curves and data to deform.
	const bool bMorph = DynamicData->NumWeightedActiveMorphTargets > 0 && LODData.GetNumVertices() > 0;

	// use correct vertex factories based on alternate weights usage
	FVertexFactoryData& VertexFactoryData = LOD.GPUSkinVertexFactories;

	bool bDataPresent = false;

	bool bGPUSkinCacheEnabled = GPUSkinCache && GEnableGPUSkinCache && (FeatureLevel >= ERHIFeatureLevel::SM5) && DynamicData->bIsSkinCacheAllowed;

	if (LOD.MorphVertexBuffer.bNeedsInitialClear && !(bMorph && bMorphNeedsUpdate))
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FSkeletalMeshObjectGPUSkin_ProcessUpdatedDynamicData_ClearMorphBuffer);
		if (IsValidRef(LOD.MorphVertexBuffer.GetUAV()))
		{
			RHICmdList.ClearUAVUint(LOD.MorphVertexBuffer.GetUAV(), FUintVector4(0, 0, 0, 0));
			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, LOD.MorphVertexBuffer.GetUAV());
		}
	}
	LOD.MorphVertexBuffer.bNeedsInitialClear = false;

	if(bMorph) 
	{
		bDataPresent = true;
		checkSlow((VertexFactoryData.MorphVertexFactories.Num() == Sections.Num()));
		
		// only update if the morph data changed and there are weighted morph targets
		if(bMorphNeedsUpdate)
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_FSkeletalMeshObjectGPUSkin_ProcessUpdatedDynamicData_UpdateMorphBuffer);
			if (UseGPUMorphTargets(GMaxRHIShaderPlatform))
			{
				// sometimes this goes out of bound, we'll ensure here
				ensureAlways(DynamicData->MorphTargetWeights.Num() == LODData.MorphTargetVertexInfoBuffers.GetNumMorphs());

				// update the morph data for the lod (before SkinCache)
				TArray<float> MorphTargetWeights;
				MorphTargetWeights.Reserve(LODData.MorphTargetVertexInfoBuffers.GetNumMorphs());
				for (int i = 0; i < DynamicData->MorphTargetWeights.Num(); i++)
				{
					for (uint32 j = 0; j < LODData.MorphTargetVertexInfoBuffers.GetNumSplitsPerMorph(i); j++)
					{
						MorphTargetWeights.Add(DynamicData->MorphTargetWeights[i]);
					}
				}
				LOD.UpdateMorphVertexBufferGPU(RHICmdList, MorphTargetWeights, LODData.MorphTargetVertexInfoBuffers, DynamicData->SectionIdsUseByActiveMorphTargets);
			}
			else
			{
				// update the morph data for the lod (before SkinCache)
				LOD.UpdateMorphVertexBufferCPU(DynamicData->ActiveMorphTargets, DynamicData->MorphTargetWeights);
			}		
		}
	}
	else
	{
//		checkSlow(VertexFactoryData.MorphVertexFactories.Num() == 0);
		bDataPresent = VertexFactoryData.VertexFactories.Num() > 0;
	}

	if (bDataPresent)
	{
		for (int32 SectionIdx = 0; SectionIdx < Sections.Num(); SectionIdx++)
		{
			const FSkelMeshRenderSection& Section = Sections[SectionIdx];

			bool bClothFactory = (FeatureLevel >= ERHIFeatureLevel::SM5) && (DynamicData->ClothingSimData.Num() > 0) && Section.HasClothingData();

			FGPUBaseSkinVertexFactory* VertexFactory;
			{
				if(bClothFactory)
				{
					VertexFactory = VertexFactoryData.ClothVertexFactories[SectionIdx]->GetVertexFactory();
				}
				else
				{
					if(DynamicData->NumWeightedActiveMorphTargets > 0 && DynamicData->SectionIdsUseByActiveMorphTargets.Contains(SectionIdx))
					{
						VertexFactory = VertexFactoryData.MorphVertexFactories[SectionIdx].Get();
					}
					else
					{
						VertexFactory = VertexFactoryData.VertexFactories[SectionIdx].Get();
					}
				}
			}

			FGPUBaseSkinVertexFactory::FShaderDataType& ShaderData = VertexFactory->GetShaderData();

			bool bUseSkinCache = bGPUSkinCacheEnabled;
			if (bUseSkinCache)
			{
				if (Section.MaxBoneInfluences == 0)
				{
					//INC_DWORD_STAT(STAT_GPUSkinCache_SkippedForZeroInfluences);
					bUseSkinCache = false;
				}

				{
#if (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT)
					// In this mode the SkinCache should only be used for RecomputeTangent meshes
					if (GEnableGPUSkinCache == 2)
					{
						if (!Section.bRecomputeTangent)
						{
							bUseSkinCache = false;
						}
					}
#endif
				}
			}

			// if we have previous reference to loca, we also update to previous frame
			if (DynamicData->PreviousReferenceToLocal.Num() > 0)
			{
				TArray<FMatrix>& PreviousReferenceToLocalMatrices = DynamicData->PreviousReferenceToLocal;
				ShaderData.UpdateBoneData(RHICmdList, PreviousReferenceToLocalMatrices, Section.BoneMap, RevisionNumber, true, FeatureLevel, bUseSkinCache);
			}

			// Create a uniform buffer from the bone transforms.
			TArray<FMatrix>& ReferenceToLocalMatrices = DynamicData->ReferenceToLocal;
			bool bNeedFence = ShaderData.UpdateBoneData(RHICmdList, ReferenceToLocalMatrices, Section.BoneMap, RevisionNumber, false, FeatureLevel, bUseSkinCache);

#if WITH_APEX_CLOTHING || WITH_CHAOS_CLOTHING
			// Update uniform buffer for APEX cloth simulation mesh positions and normals
			if( bClothFactory )
			{				
				FGPUBaseSkinAPEXClothVertexFactory::ClothShaderType& ClothShaderData = VertexFactoryData.ClothVertexFactories[SectionIdx]->GetClothShaderData();
				ClothShaderData.ClothBlendWeight = DynamicData->ClothBlendWeight;
				int16 ActorIdx = Section.CorrespondClothAssetIndex;
				if(FClothSimulData* SimData = DynamicData->ClothingSimData.Find(ActorIdx))
				{
					bNeedFence = ClothShaderData.UpdateClothSimulData(RHICmdList, DynamicData->ClothingSimData[ActorIdx].Positions, DynamicData->ClothingSimData[ActorIdx].Normals, FrameNumberToPrepare, FeatureLevel) || bNeedFence;
					ClothShaderData.GetClothLocalToWorldForWriting(FrameNumberToPrepare) = DynamicData->ClothingSimData[ActorIdx].ComponentRelativeTransform.ToMatrixWithScale() * DynamicData->ClothObjectLocalToWorld;
				}
			}
#endif // WITH_APEX_CLOTHING || WITH_CHAOS_CLOTHING

			// Try to use the GPU skinning cache if possible
			if (bUseSkinCache)
			{
				// This takes the cloth positions from cloth space into world space
				FMatrix ClothLocalToWorld = bClothFactory ? VertexFactoryData.ClothVertexFactories[SectionIdx]->GetClothShaderData().GetClothLocalToWorldForWriting(FrameNumberToPrepare) : FMatrix::Identity;
				// Matrices are transposed in ue4 meaning matrix multiples need to happen in reverse ((AB)x = b becomes xTBTAT = b).
				FMatrix LocalToCloth = DynamicData->ClothObjectLocalToWorld * ClothLocalToWorld.Inverse();

				GPUSkinCache->ProcessEntry(RHICmdList, VertexFactory,
					VertexFactoryData.PassthroughVertexFactories[SectionIdx].Get(), Section, this, bMorph ? &LOD.MorphVertexBuffer : 0, bClothFactory ? &LODData.ClothVertexBuffer : 0,
					bClothFactory ? DynamicData->ClothingSimData.Find(Section.CorrespondClothAssetIndex) : 0, LocalToCloth, DynamicData->ClothBlendWeight, RevisionNumber, SectionIdx, SkinCacheEntry);
			}

			if (bNeedFence)
			{
				RHIThreadFenceForDynamicData = RHICmdList.RHIThreadFence(true);
			}
		}
	}
}

TArray<float> FSkeletalMeshObjectGPUSkin::FSkeletalMeshObjectLOD::MorphAccumulatedWeightArray;

void FGPUMorphUpdateCS::SetParameters(FRHICommandList& RHICmdList, const FVector4& LocalScale, const FMorphTargetVertexInfoBuffers& MorphTargetVertexInfoBuffers, FMorphVertexBuffer& MorphVertexBuffer)
{
	FRHIComputeShader* CS = RHICmdList.GetBoundComputeShader();

	SetUAVParameter(RHICmdList, CS, MorphVertexBufferParameter, MorphVertexBuffer.GetUAV());

	SetShaderValue(RHICmdList, CS, PositionScaleParameter, LocalScale);

	SetSRVParameter(RHICmdList, CS, VertexIndicesParameter, MorphTargetVertexInfoBuffers.VertexIndicesSRV);
	SetSRVParameter(RHICmdList, CS, MorphDeltasParameter, MorphTargetVertexInfoBuffers.MorphDeltasSRV);
}

static const uint32 GMorphTargetDispatchBatchSize = 128;

void FGPUMorphUpdateCS::SetOffsetAndSize(FRHICommandList& RHICmdList, uint32 StartIndex, uint32 EndIndexPlusOne, const FMorphTargetVertexInfoBuffers& MorphTargetVertexInfoBuffers, const TArray<float>& MorphTargetWeights)
{
	FRHIComputeShader* CS = RHICmdList.GetBoundComputeShader();

	uint32 ThreadOffsets[GMorphTargetDispatchBatchSize];
	float Weights[GMorphTargetDispatchBatchSize];
	
	uint32 BaseOffset = MorphTargetVertexInfoBuffers.GetStartOffset(StartIndex);
	check(EndIndexPlusOne <= MorphTargetVertexInfoBuffers.GetNumMorphs());
	uint32 ThreadOffset = 0u;
	for (uint32 i = 0; i < GMorphTargetDispatchBatchSize; i++)
	{
		if (StartIndex + i < EndIndexPlusOne)
		{
			Weights[i] = MorphTargetWeights[StartIndex + i];
			ThreadOffsets[i] = ThreadOffset;
			ThreadOffset += MorphTargetVertexInfoBuffers.GetNumWorkItems(StartIndex + i);
		}
		else
		{
			uint32 LastStart = MorphTargetVertexInfoBuffers.GetStartOffset(EndIndexPlusOne - 1);
			uint32 LastSize = MorphTargetVertexInfoBuffers.GetNumWorkItems(EndIndexPlusOne - 1);
			Weights[i] = 0.0f;
			ThreadOffsets[i] = ThreadOffset;
		}
	}

	SetShaderValue(RHICmdList, CS, GlobalDispatchOffsetParameter, BaseOffset);
	SetShaderValue(RHICmdList, CS, ThreadOffsetsParameter, ThreadOffsets);
	SetShaderValue(RHICmdList, CS, MorphTargetWeightParameter, Weights);
}


void FGPUMorphUpdateCS::Dispatch(FRHICommandList& RHICmdList, uint32 Size)
{
	RHICmdList.DispatchComputeShader(1, (Size + 31) / 32, 1);	
}

void FGPUMorphUpdateCS::EndAllDispatches(FRHICommandList& RHICmdList)
{
	FRHIComputeShader* CS = RHICmdList.GetBoundComputeShader();
	SetUAVParameter(RHICmdList, CS, MorphVertexBufferParameter, nullptr);
}

IMPLEMENT_SHADER_TYPE(, FGPUMorphUpdateCS, TEXT("/Engine/Private/MorphTargets.usf"), TEXT("GPUMorphUpdateCS"), SF_Compute);

void FGPUMorphNormalizeCS::SetParameters(FRHICommandList& RHICmdList, const FVector4& InvLocalScale, const FMorphTargetVertexInfoBuffers& MorphTargetVertexInfoBuffers, FMorphVertexBuffer& MorphVertexBuffer)
{
	FRHIComputeShader* CS = RHICmdList.GetBoundComputeShader();
	SetUAVParameter(RHICmdList, CS, MorphVertexBufferParameter, MorphVertexBuffer.GetUAV());
	SetSRVParameter(RHICmdList, CS, MorphPermutationBufferParameter, MorphTargetVertexInfoBuffers.MorphPermutationsSRV);
	SetShaderValue(RHICmdList, CS, PositionScaleParameter, InvLocalScale);
}

void FGPUMorphNormalizeCS::SetOffsetAndSize(FRHICommandList& RHICmdList, uint32 StartIndex, uint32 EndIndexPlusOne, const FMorphTargetVertexInfoBuffers& MorphTargetVertexInfoBuffers, const TArray<float>& InverseAccumulatedWeights)
{
	FRHIComputeShader* CS = RHICmdList.GetBoundComputeShader();

	uint32 ThreadOffsets[GMorphTargetDispatchBatchSize];
	float Weights[GMorphTargetDispatchBatchSize];

	uint32 BaseOffset = MorphTargetVertexInfoBuffers.GetPermutationStartOffset(StartIndex);
	check(EndIndexPlusOne <= MorphTargetVertexInfoBuffers.GetNumPermutations());

	uint32 ThreadOffset = 0u;
	for (uint32 i = 0; i < GMorphTargetDispatchBatchSize; i++)
	{
		if (StartIndex + i < EndIndexPlusOne)
		{
			Weights[i] = InverseAccumulatedWeights[StartIndex + i];
			ThreadOffsets[i] = ThreadOffset;
			ThreadOffset += MorphTargetVertexInfoBuffers.GetPermutationSize(StartIndex + i);
		}
		else
		{
			uint32 LastStart = MorphTargetVertexInfoBuffers.GetPermutationStartOffset(EndIndexPlusOne - 1);
			uint32 LastSize = MorphTargetVertexInfoBuffers.GetPermutationSize(EndIndexPlusOne - 1);
			Weights[i] = 0.0f;
			ThreadOffsets[i] = ThreadOffset;
		}
	}

	SetShaderValue(RHICmdList, CS, GlobalDispatchOffsetParameter, BaseOffset);
	SetShaderValue(RHICmdList, CS, ThreadOffsetsParameter, ThreadOffsets);
	SetShaderValue(RHICmdList, CS, MorphTargetWeightParameter, Weights);
}

void FGPUMorphNormalizeCS::Dispatch(FRHICommandList& RHICmdList, uint32 NumVerticies)
{
	RHICmdList.DispatchComputeShader(1, (NumVerticies + 31) / 32, 1);
}

void FGPUMorphNormalizeCS::EndAllDispatches(FRHICommandList& RHICmdList)
{
	FRHIComputeShader* CS = RHICmdList.GetBoundComputeShader();
	SetUAVParameter(RHICmdList, CS, MorphVertexBufferParameter, nullptr);
}

IMPLEMENT_SHADER_TYPE(, FGPUMorphNormalizeCS, TEXT("/Engine/Private/MorphTargets.usf"), TEXT("GPUMorphNormalizeCS"), SF_Compute);

static void CalculateMorphDeltaBounds(const TArray<float>& MorphTargetWeights, const FMorphTargetVertexInfoBuffers& MorphTargetVertexInfoBuffers, FVector4& MorphScale, FVector4& InvMorphScale)
{
	double MinAccumScale[4] = { 0, 0, 0, 0 };
	double MaxAccumScale[4] = { 0, 0, 0, 0 };
	double MaxScale[4] = { 0, 0, 0, 0 };
	for (uint32 i = 0; i < MorphTargetVertexInfoBuffers.GetNumMorphs(); i++)
	{
		FVector4 MinMorphScale = MorphTargetVertexInfoBuffers.GetMinimumMorphScale(i);
		FVector4 MaxMorphScale = MorphTargetVertexInfoBuffers.GetMaximumMorphScale(i);

		for (uint32 j = 0; j < 4; j++)
		{
			MinAccumScale[j] += MorphTargetWeights[i] * MinMorphScale[j];
			MaxAccumScale[j] += MorphTargetWeights[i] * MaxMorphScale[j];

			double AbsMorphScale = FMath::Max(FMath::Abs(MinMorphScale[j]), FMath::Abs(MaxMorphScale[j]));
			double AbsAccumScale = FMath::Max(FMath::Abs(MinAccumScale[j]), FMath::Abs(MaxAccumScale[j]));
			//the maximum accumulated and the maximum local value have to fit into out int24
			MaxScale[j] = FMath::Max(MaxScale[j], FMath::Max(AbsMorphScale, AbsAccumScale));
		}
	}

	const double ScaleToInt24 = 16777216.0;
	MorphScale = FVector4
	(
		ScaleToInt24 / (double)((uint64)(MaxScale[0] + 1.0)),
		ScaleToInt24 / (double)((uint64)(MaxScale[1] + 1.0)),
		ScaleToInt24 / (double)((uint64)(MaxScale[2] + 1.0)),
		ScaleToInt24 / (double)((uint64)(MaxScale[3] + 1.0))
	);

	InvMorphScale = FVector4
	(
		(double)((uint64)(MaxScale[0] + 1.0)) / ScaleToInt24,
		(double)((uint64)(MaxScale[1] + 1.0)) / ScaleToInt24,
		(double)((uint64)(MaxScale[2] + 1.0)) / ScaleToInt24,
		(double)((uint64)(MaxScale[3] + 1.0)) / ScaleToInt24
	);
}

void FSkeletalMeshObjectGPUSkin::FSkeletalMeshObjectLOD::UpdateMorphVertexBufferGPU(FRHICommandListImmediate& RHICmdList, const TArray<float>& MorphTargetWeights, const FMorphTargetVertexInfoBuffers& MorphTargetVertexInfoBuffers, const TArray<int32>& SectionIdsUseByActiveMorphTargets)
{
	if (IsValidRef(MorphVertexBuffer.VertexBufferRHI))
	{
		SCOPE_CYCLE_COUNTER(STAT_MorphVertexBuffer_Update);

		// LOD of the skel mesh is used to find number of vertices in buffer
		FSkeletalMeshLODRenderData& LodData = SkelMeshRenderData->LODRenderData[LODIndex];

		const bool bUseGPUMorphTargets = UseGPUMorphTargets(GMaxRHIShaderPlatform);
		MorphVertexBuffer.RecreateResourcesIfRequired(bUseGPUMorphTargets);

		SCOPED_GPU_STAT(RHICmdList, MorphTargets);

		SCOPED_DRAW_EVENTF(RHICmdList, MorphUpdate,
			TEXT("MorphUpdate LodVertices=%d Threads=%d"),
			LodData.GetNumVertices(),
			MorphTargetVertexInfoBuffers.GetNumWorkItems());
		RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EGfxToCompute, MorphVertexBuffer.GetUAV());

		RHICmdList.ClearUAVUint(MorphVertexBuffer.GetUAV(), FUintVector4(0, 0, 0, 0));
		RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, MorphVertexBuffer.GetUAV());

		{
			RHICmdList.BeginUAVOverlap();

			FVector4 MorphScale; 
			FVector4 InvMorphScale; 
			TArray<float> InverseAccumulatedWeights;
			{
				SCOPE_CYCLE_COUNTER(STAT_MorphVertexBuffer_ApplyDelta);
				CalculateMorphDeltaBounds(MorphTargetWeights, MorphTargetVertexInfoBuffers, MorphScale, InvMorphScale);
				MorphTargetVertexInfoBuffers.CalculateInverseAccumulatedWeights(MorphTargetWeights, InverseAccumulatedWeights);
			}

			{
				SCOPED_DRAW_EVENTF(RHICmdList, MorphUpdateScatter, TEXT("Scatter"));

				//the first pass scatters all morph targets into the vertexbuffer using atomics
				//multiple morph targets can be batched by a single shader where the shader will rely on
				//binary search to find the correct target weight within the batch.
				TShaderMapRef<FGPUMorphUpdateCS> GPUMorphUpdateCS(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
				for (uint32 i = 0; i < MorphTargetVertexInfoBuffers.GetNumMorphs();)
				{
					uint32 NumMorphDeltas = 0;
					uint32 j = 0;
					for (; j < GMorphTargetDispatchBatchSize - 1; j++)
					{
						if (i + j < MorphTargetVertexInfoBuffers.GetNumMorphs())
						{
							if (NumMorphDeltas + MorphTargetVertexInfoBuffers.GetNumWorkItems(i + j) <= FMorphTargetVertexInfoBuffers::GetMaximumThreadGroupSize())
							{
								NumMorphDeltas += MorphTargetVertexInfoBuffers.GetNumWorkItems(i + j);
								continue;
							}
						}
						break;
					}
					check(j > 0);

					if (NumMorphDeltas > 0)
					{
						RHICmdList.SetComputeShader(GPUMorphUpdateCS.GetComputeShader());
						GPUMorphUpdateCS->SetParameters(RHICmdList, MorphScale, MorphTargetVertexInfoBuffers, MorphVertexBuffer);
						GPUMorphUpdateCS->SetOffsetAndSize(RHICmdList, i, i + j, MorphTargetVertexInfoBuffers, MorphTargetWeights);
						check(NumMorphDeltas <= FMorphTargetVertexInfoBuffers::GetMaximumThreadGroupSize());
						GPUMorphUpdateCS->Dispatch(RHICmdList, NumMorphDeltas);
						RHICmdList.TransitionResource(EResourceTransitionAccess::ERWNoBarrier, EResourceTransitionPipeline::EComputeToCompute, MorphVertexBuffer.GetUAV());
					}
					i += j;
				}
				GPUMorphUpdateCS->EndAllDispatches(RHICmdList);
				RHICmdList.EndUAVOverlap();
				RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, MorphVertexBuffer.GetUAV());
			}

			{
				SCOPED_DRAW_EVENTF(RHICmdList, MorphUpdateNormalize, TEXT("Normalize"));

				//The second pass normalizes the scattered result and converts it back into floats.
				//The dispatches are split by morph permutation (and their accumulated weight) .
				//Every vertex is touched only by a single permutation. 
				//multiple permutations can be batched by a single shader where the shader will rely on
				//binary search to find the correct target weight within the batch.
				TShaderMapRef<FGPUMorphNormalizeCS> GPUMorphNormalizeCS(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
				for (uint32 i = 0; i < MorphTargetVertexInfoBuffers.GetNumPermutations();)
				{
					uint32 DispatchSize = 0;
					uint32 j = 0;
					for (; j < GMorphTargetDispatchBatchSize - 1; j++)
					{
						if (i + j < MorphTargetVertexInfoBuffers.GetNumPermutations())
						{
							if (DispatchSize + MorphTargetVertexInfoBuffers.GetPermutationSize(i + j) <= FMorphTargetVertexInfoBuffers::GetMaximumThreadGroupSize())
							{
								DispatchSize += MorphTargetVertexInfoBuffers.GetPermutationSize(i + j);
								continue;
							}
						}
						break;
					}
					check(j > 0);

					if (DispatchSize > 0)
					{
						RHICmdList.SetComputeShader(GPUMorphNormalizeCS.GetComputeShader());
						GPUMorphNormalizeCS->SetParameters(RHICmdList, InvMorphScale, MorphTargetVertexInfoBuffers, MorphVertexBuffer);
						GPUMorphNormalizeCS->SetOffsetAndSize(RHICmdList, i, i + j, MorphTargetVertexInfoBuffers, InverseAccumulatedWeights);
						check(DispatchSize <= FMorphTargetVertexInfoBuffers::GetMaximumThreadGroupSize());
						GPUMorphNormalizeCS->Dispatch(RHICmdList, DispatchSize);
						RHICmdList.TransitionResource(EResourceTransitionAccess::ERWNoBarrier, EResourceTransitionPipeline::EComputeToCompute, MorphVertexBuffer.GetUAV());
					}
					i += j;
				}
				GPUMorphNormalizeCS->EndAllDispatches(RHICmdList);
				RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, MorphVertexBuffer.GetUAV());
			}
		}

		// Copy the section Ids use by all active morph targets
		MorphVertexBuffer.SectionIds = SectionIdsUseByActiveMorphTargets;
		
		// set update flag
		MorphVertexBuffer.bHasBeenUpdated = true;
	}
}

void FSkeletalMeshObjectGPUSkin::FSkeletalMeshObjectLOD::UpdateSkinWeights(FSkelMeshComponentLODInfo* CompLODInfo)
{	
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FSkeletalMeshObjectLOD_UpdateSkinWeights);

	check(SkelMeshRenderData);
	check(SkelMeshRenderData->LODRenderData.IsValidIndex(LODIndex));

	// If we have a skin weight override buffer (and it's the right size) use it
	FSkeletalMeshLODRenderData& LODData = SkelMeshRenderData->LODRenderData[LODIndex];	
	if (CompLODInfo)
	{
		FSkinWeightVertexBuffer* NewMeshObjectWeightBuffer = nullptr;

		if (CompLODInfo->OverrideSkinWeights &&
			CompLODInfo->OverrideSkinWeights->GetNumVertices() == LODData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices())
		{
			check(LODData.SkinWeightVertexBuffer.GetMaxBoneInfluences() == CompLODInfo->OverrideSkinWeights->GetMaxBoneInfluences());
			NewMeshObjectWeightBuffer = CompLODInfo->OverrideSkinWeights;
		}
		else if (CompLODInfo->OverrideProfileSkinWeights &&
			CompLODInfo->OverrideProfileSkinWeights->GetNumVertices() == LODData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices())
		{
			check(LODData.SkinWeightVertexBuffer.GetMaxBoneInfluences() == CompLODInfo->OverrideProfileSkinWeights->GetMaxBoneInfluences());
			NewMeshObjectWeightBuffer = CompLODInfo->OverrideProfileSkinWeights;
		}
		else
		{
			NewMeshObjectWeightBuffer = LODData.GetSkinWeightVertexBuffer();
		}

		if (MeshObjectWeightBuffer != NewMeshObjectWeightBuffer)
		{
			MeshObjectWeightBuffer = NewMeshObjectWeightBuffer;

			FVertexFactoryBuffers VertexBuffers;
			GetVertexBuffers(VertexBuffers, LODData);

			FSkeletalMeshObjectGPUSkin::FSkeletalMeshObjectLOD* Self = this;
			ENQUEUE_RENDER_COMMAND(UpdateSkinWeightsGPUSkin)(
				[NewMeshObjectWeightBuffer, VertexBuffers, Self](FRHICommandListImmediate& RHICmdList)
			{
				Self->GPUSkinVertexFactories.UpdateVertexFactoryData(VertexBuffers);
			});
		}
	}
	
}

void FSkeletalMeshObjectGPUSkin::FSkeletalMeshObjectLOD::UpdateMorphVertexBufferCPU(const TArray<FActiveMorphTarget>& ActiveMorphTargets, const TArray<float>& MorphTargetWeights)
{
	SCOPE_CYCLE_COUNTER(STAT_MorphVertexBuffer_Update);

	if (IsValidRef(MorphVertexBuffer.VertexBufferRHI))
	{
		extern ENGINE_API bool DoRecomputeSkinTangentsOnGPU_RT();
		bool bBlendTangentsOnCPU = !DoRecomputeSkinTangentsOnGPU_RT();

		// LOD of the skel mesh is used to find number of vertices in buffer
		FSkeletalMeshLODRenderData& LodData = SkelMeshRenderData->LODRenderData[LODIndex];
		
		const bool bUseGPUMorphTargets = UseGPUMorphTargets(GMaxRHIShaderPlatform);
		MorphVertexBuffer.RecreateResourcesIfRequired(bUseGPUMorphTargets);

		uint32 Size = LodData.GetNumVertices() * sizeof(FMorphGPUSkinVertex);

		FMorphGPUSkinVertex* Buffer = nullptr;
		{
			SCOPE_CYCLE_COUNTER(STAT_MorphVertexBuffer_Alloc);
			Buffer = (FMorphGPUSkinVertex*)FMemory::Malloc(Size);
		}

		{
			SCOPE_CYCLE_COUNTER(STAT_MorphVertexBuffer_Init);

			if (bBlendTangentsOnCPU)
			{
				// zero everything
				int32 vertsToAdd = static_cast<int32>(LodData.GetNumVertices()) - MorphAccumulatedWeightArray.Num();
				if (vertsToAdd > 0)
				{
					MorphAccumulatedWeightArray.AddUninitialized(vertsToAdd);
				}

				FMemory::Memzero(MorphAccumulatedWeightArray.GetData(), sizeof(float)*LodData.GetNumVertices());
			}

			// PackedNormals will be wrong init with 0, but they'll be overwritten later
			FMemory::Memzero(&Buffer[0], sizeof(FMorphGPUSkinVertex)*LodData.GetNumVertices());
		}

		{
			SCOPE_CYCLE_COUNTER(STAT_MorphVertexBuffer_ApplyDelta);

			// iterate over all active morph targets and accumulate their vertex deltas
			for (int32 AnimIdx = 0; AnimIdx < ActiveMorphTargets.Num(); AnimIdx++)
			{
				const FActiveMorphTarget& MorphTarget = ActiveMorphTargets[AnimIdx];
				checkSlow(MorphTarget.MorphTarget != NULL);
				checkSlow(MorphTarget.MorphTarget->HasDataForLOD(LODIndex));
				const float MorphTargetWeight = MorphTargetWeights[MorphTarget.WeightIndex];
				const float MorphAbsWeight = FMath::Abs(MorphTargetWeight);
				checkSlow(MorphAbsWeight >= MinMorphTargetBlendWeight && MorphAbsWeight <= MaxMorphTargetBlendWeight);


				// Get deltas
				int32 NumDeltas;
				FMorphTargetDelta* Deltas = MorphTarget.MorphTarget->GetMorphTargetDelta(LODIndex, NumDeltas);

				// iterate over the vertices that this lod model has changed
				for (int32 MorphVertIdx = 0; MorphVertIdx < NumDeltas; MorphVertIdx++)
				{
					const FMorphTargetDelta& MorphVertex = Deltas[MorphVertIdx];

					// @TODO FIXMELH : temp hack until we fix importing issue
					if ((MorphVertex.SourceIdx < LodData.GetNumVertices()))
					{
						FMorphGPUSkinVertex& DestVertex = Buffer[MorphVertex.SourceIdx];

						DestVertex.DeltaPosition += MorphVertex.PositionDelta * MorphTargetWeight;

						// todo: could be moved out of the inner loop to be more efficient
						if (bBlendTangentsOnCPU)
						{
							DestVertex.DeltaTangentZ += MorphVertex.TangentZDelta * MorphTargetWeight;
							// accumulate the weight so we can normalized it later
							MorphAccumulatedWeightArray[MorphVertex.SourceIdx] += MorphAbsWeight;
						}
					}
				} // for all vertices
			} // for all morph targets

			if (bBlendTangentsOnCPU)
			{
				// copy back all the tangent values (can't use Memcpy, since we have to pack the normals)
				for (uint32 iVertex = 0; iVertex < LodData.GetNumVertices(); ++iVertex)
				{
					FMorphGPUSkinVertex& DestVertex = Buffer[iVertex];
					float AccumulatedWeight = MorphAccumulatedWeightArray[iVertex];

					// if accumulated weight is >1.f
					// previous code was applying the weight again in GPU if less than 1, but it doesn't make sense to do so
					// so instead, we just divide by AccumulatedWeight if it's more than 1.
					// now DeltaTangentZ isn't FPackedNormal, so you can apply any value to it. 
					if (AccumulatedWeight > 1.f)
					{
						DestVertex.DeltaTangentZ /= AccumulatedWeight;
					}
				}
			}
		} // ApplyDelta

		// Lock the real buffer.
		{
			SCOPE_CYCLE_COUNTER(STAT_MorphVertexBuffer_RhiLockAndCopy);
			FMorphGPUSkinVertex* ActualBuffer = (FMorphGPUSkinVertex*)RHILockVertexBuffer(MorphVertexBuffer.VertexBufferRHI, 0, Size, RLM_WriteOnly);
			FMemory::Memcpy(ActualBuffer, Buffer, Size);
			FMemory::Free(Buffer);
		}

		{
			SCOPE_CYCLE_COUNTER(STAT_MorphVertexBuffer_RhiUnlock);
			// Unlock the buffer.
			RHIUnlockVertexBuffer(MorphVertexBuffer.VertexBufferRHI);
			// set update flag
			MorphVertexBuffer.bHasBeenUpdated = true;
		}
	}
}

const FVertexFactory* FSkeletalMeshObjectGPUSkin::GetSkinVertexFactory(const FSceneView* View, int32 LODIndex,int32 ChunkIdx) const
{
	checkSlow( LODs.IsValidIndex(LODIndex) );
	checkSlow( DynamicData );

	const FSkelMeshObjectLODInfo& MeshLODInfo = LODInfo[LODIndex];
	const FSkeletalMeshObjectLOD& LOD = LODs[LODIndex];

	// If the GPU skinning cache was used, return the passthrough vertex factory
	if (SkinCacheEntry && FGPUSkinCache::IsEntryValid(SkinCacheEntry, ChunkIdx) && DynamicData->bIsSkinCacheAllowed)
	{
		return LOD.GPUSkinVertexFactories.PassthroughVertexFactories[ChunkIdx].Get();
	}

	// cloth simulation is updated & if this ChunkIdx is for ClothVertexFactory
	if ( DynamicData->ClothingSimData.Num() > 0 
		&& LOD.GPUSkinVertexFactories.ClothVertexFactories.IsValidIndex(ChunkIdx)  
		&& LOD.GPUSkinVertexFactories.ClothVertexFactories[ChunkIdx].IsValid() )
	{
		return LOD.GPUSkinVertexFactories.ClothVertexFactories[ChunkIdx]->GetVertexFactory();
	}

	// use the morph enabled vertex factory if any active morphs are set
	if (DynamicData->NumWeightedActiveMorphTargets > 0)
	{
		for (FActiveMorphTarget& ActiveMorphTarget : DynamicData->ActiveMorphTargets)
		{
			if (ActiveMorphTarget.MorphTarget->MorphLODModels.IsValidIndex(LODIndex) && ActiveMorphTarget.MorphTarget->MorphLODModels[LODIndex].SectionIndices.Contains(ChunkIdx))
	{
		return LOD.GPUSkinVertexFactories.MorphVertexFactories[ChunkIdx].Get();
	}
		}
	}

	// use the default gpu skin vertex factory
	return LOD.GPUSkinVertexFactories.VertexFactories[ChunkIdx].Get();
}

FSkinWeightVertexBuffer* FSkeletalMeshObjectGPUSkin::GetSkinWeightVertexBuffer(int32 LODIndex) const
{
	checkSlow(LODs.IsValidIndex(LODIndex));
	return LODs[LODIndex].MeshObjectWeightBuffer;
}

void FSkeletalMeshObjectGPUSkin::RefreshClothingTransforms(const FMatrix& InNewLocalToWorld, uint32 FrameNumber)
{
	if(DynamicData && DynamicData->ClothingSimData.Num() > 0)
	{
		FSkeletalMeshObjectLOD& LOD = LODs[DynamicData->LODIndex];
		const TArray<FSkelMeshRenderSection>& Sections = GetRenderSections(DynamicData->LODIndex);
		const int32 NumSections = Sections.Num();

		DynamicData->ClothObjectLocalToWorld = InNewLocalToWorld;

		for(int32 SectionIndex = 0; SectionIndex < NumSections; ++SectionIndex)
		{
			if(LOD.GPUSkinVertexFactories.ClothVertexFactories.IsValidIndex(SectionIndex))
			{
				FGPUBaseSkinAPEXClothVertexFactory* ClothFactory = LOD.GPUSkinVertexFactories.ClothVertexFactories[SectionIndex].Get();

				if(ClothFactory)
				{
					const FSkelMeshRenderSection& Section = Sections[SectionIndex];
					FGPUBaseSkinAPEXClothVertexFactory::ClothShaderType& ClothShaderData = LOD.GPUSkinVertexFactories.ClothVertexFactories[SectionIndex]->GetClothShaderData();
					const int16 ActorIdx = Section.CorrespondClothAssetIndex;

					if(FClothSimulData* SimData = DynamicData->ClothingSimData.Find(ActorIdx))
					{
						ClothShaderData.GetClothLocalToWorldForWriting(FrameNumber) = SimData->ComponentRelativeTransform.ToMatrixWithScale() * InNewLocalToWorld;
					}
				}
			}
		}
	}
}

/** 
 * Initialize the stream components common to all GPU skin vertex factory types 
 *
 * @param VertexFactoryData - context for setting the vertex factory stream components. commited later
 * @param VertexBuffers - vertex buffers which contains the data and also stride info
 * @param bUseInstancedVertexWeights - use instanced influence weights instead of default weights
 */
template<class VertexFactoryType>
void InitGPUSkinVertexFactoryComponents(typename VertexFactoryType::FDataType* VertexFactoryData, 
										const FSkeletalMeshObjectGPUSkin::FVertexFactoryBuffers& VertexBuffers, VertexFactoryType* VertexFactory)
{
	typedef TGPUSkinVertexBase BaseVertexType;

	//position
	VertexBuffers.StaticVertexBuffers->PositionVertexBuffer.BindPositionVertexBuffer(VertexFactory, *VertexFactoryData);

	// tangents
	VertexBuffers.StaticVertexBuffers->StaticMeshVertexBuffer.BindTangentVertexBuffer(VertexFactory, *VertexFactoryData);
	VertexBuffers.StaticVertexBuffers->StaticMeshVertexBuffer.BindTexCoordVertexBuffer(VertexFactory, *VertexFactoryData);

	bool bUse16BitBoneIndex = VertexBuffers.SkinWeightVertexBuffer->Use16BitBoneIndex();
	VertexFactoryData->bUse16BitBoneIndex = bUse16BitBoneIndex;
	VertexFactoryData->NumBoneInfluences = VertexBuffers.SkinWeightVertexBuffer->GetMaxBoneInfluences();

	GPUSkinBoneInfluenceType BoneInfluenceType = VertexBuffers.SkinWeightVertexBuffer->GetBoneInfluenceType();
	if (BoneInfluenceType == GPUSkinBoneInfluenceType::UnlimitedBoneInfluence)
	{
		FGPUBaseSkinVertexFactory::FShaderDataType& ShaderData = VertexFactory->GetShaderData();
		ShaderData.InputWeightIndexSize = VertexBuffers.SkinWeightVertexBuffer->GetBoneIndexByteSize();
		ShaderData.InputWeightStream = VertexBuffers.SkinWeightVertexBuffer->GetDataVertexBuffer()->GetSRV();

		const FSkinWeightLookupVertexBuffer* LookupVertexBuffer = VertexBuffers.SkinWeightVertexBuffer->GetLookupVertexBuffer();
		VertexFactoryData->BlendOffsetCount = FVertexStreamComponent(LookupVertexBuffer, 0, LookupVertexBuffer->GetStride(), VET_UInt);
	}
	else
	{
		// bone indices & weights
		const FSkinWeightDataVertexBuffer* WeightDataVertexBuffer = VertexBuffers.SkinWeightVertexBuffer->GetDataVertexBuffer();
		const uint32 Stride = VertexBuffers.SkinWeightVertexBuffer->GetConstantInfluencesVertexStride();
		const uint32 WeightsOffset = VertexBuffers.SkinWeightVertexBuffer->GetConstantInfluencesBoneWeightsOffset();
		VertexFactoryData->BoneIndices = FVertexStreamComponent(WeightDataVertexBuffer, 0, Stride, bUse16BitBoneIndex ? VET_UShort4 : VET_UByte4);
		VertexFactoryData->BoneWeights = FVertexStreamComponent(WeightDataVertexBuffer, WeightsOffset, Stride, VET_UByte4N);

		if (BoneInfluenceType == GPUSkinBoneInfluenceType::ExtraBoneInfluence)
		{
			// Extra streams for bone indices & weights
			VertexFactoryData->ExtraBoneIndices = FVertexStreamComponent(
				WeightDataVertexBuffer, 4 * VertexBuffers.SkinWeightVertexBuffer->GetBoneIndexByteSize(), Stride, bUse16BitBoneIndex ? VET_UShort4 : VET_UByte4);
			VertexFactoryData->ExtraBoneWeights = FVertexStreamComponent(
				WeightDataVertexBuffer, WeightsOffset + 4, Stride, VET_UByte4N);
		}
	}

	// Color data may be NULL
	if( VertexBuffers.ColorVertexBuffer != NULL && 
		VertexBuffers.ColorVertexBuffer->IsInitialized() )
	{
		// Color
		VertexBuffers.ColorVertexBuffer->BindColorVertexBuffer(VertexFactory, *VertexFactoryData);
	}
	else
	{
		VertexFactoryData->ColorComponentsSRV = nullptr;
		VertexFactoryData->ColorIndexMask = 0;
	}
}

/** 
 * Initialize the stream components common to all GPU skin vertex factory types 
 *
 * @param VertexFactoryData - context for setting the vertex factory stream components. commited later
 * @param VertexBuffers - vertex buffers which contains the data and also stride info
 * @param bUseInstancedVertexWeights - use instanced influence weights instead of default weights
 */
template<class VertexFactoryType>
void InitMorphVertexFactoryComponents(typename VertexFactoryType::FDataType* VertexFactoryData, 
										const FSkeletalMeshObjectGPUSkin::FVertexFactoryBuffers& VertexBuffers)
{
	// delta positions
	VertexFactoryData->DeltaPositionComponent = FVertexStreamComponent(
		VertexBuffers.MorphVertexBuffer,STRUCT_OFFSET(FMorphGPUSkinVertex,DeltaPosition),sizeof(FMorphGPUSkinVertex),VET_Float3);
	// delta normals
	VertexFactoryData->DeltaTangentZComponent = FVertexStreamComponent(
		VertexBuffers.MorphVertexBuffer, STRUCT_OFFSET(FMorphGPUSkinVertex, DeltaTangentZ), sizeof(FMorphGPUSkinVertex), VET_Float3);
}

/** 
 * Initialize the stream components common to all GPU skin vertex factory types 
 *
 * @param VertexFactoryData - context for setting the vertex factory stream components. commited later
 * @param VertexBuffers - vertex buffers which contains the data and also stride info
 * @param bUseInstancedVertexWeights - use instanced influence weights instead of default weights
 */
template<class VertexFactoryType>
void InitAPEXClothVertexFactoryComponents(typename VertexFactoryType::FDataType* VertexFactoryData, 
										const FSkeletalMeshObjectGPUSkin::FVertexFactoryBuffers& VertexBuffers)
{
	// barycentric coord for positions
	VertexFactoryData->CoordPositionComponent = FVertexStreamComponent(
		VertexBuffers.APEXClothVertexBuffer,STRUCT_OFFSET(FMeshToMeshVertData,PositionBaryCoordsAndDist),sizeof(FMeshToMeshVertData),VET_Float4);
	// barycentric coord for normals
	VertexFactoryData->CoordNormalComponent = FVertexStreamComponent(
		VertexBuffers.APEXClothVertexBuffer,STRUCT_OFFSET(FMeshToMeshVertData,NormalBaryCoordsAndDist),sizeof(FMeshToMeshVertData),VET_Float4);
	// barycentric coord for tangents
	VertexFactoryData->CoordTangentComponent = FVertexStreamComponent(
		VertexBuffers.APEXClothVertexBuffer,STRUCT_OFFSET(FMeshToMeshVertData,TangentBaryCoordsAndDist),sizeof(FMeshToMeshVertData),VET_Float4);
	// indices for reference physics mesh vertices
	VertexFactoryData->SimulIndicesComponent = FVertexStreamComponent(
		VertexBuffers.APEXClothVertexBuffer,STRUCT_OFFSET(FMeshToMeshVertData, SourceMeshVertIndices),sizeof(FMeshToMeshVertData),VET_UShort4);
	VertexFactoryData->ClothBuffer = VertexBuffers.APEXClothVertexBuffer->GetSRV();
	VertexFactoryData->ClothIndexMapping = VertexBuffers.APEXClothVertexBuffer->GetClothIndexMapping();
}

/** 
 * Handles transferring data between game/render threads when initializing vertex factory components 
 */
template <class VertexFactoryType>
class TDynamicUpdateVertexFactoryData
{
public:
	TDynamicUpdateVertexFactoryData(
		VertexFactoryType* InVertexFactory,
		const FSkeletalMeshObjectGPUSkin::FVertexFactoryBuffers& InVertexBuffers)
		:	VertexFactory(InVertexFactory)
		,	VertexBuffers(InVertexBuffers)
	{}
	VertexFactoryType* VertexFactory;
	const FSkeletalMeshObjectGPUSkin::FVertexFactoryBuffers VertexBuffers;
	
};

/**
 * Creates a vertex factory entry for the given type and initialize it on the render thread
 */
template <class VertexFactoryTypeBase, class VertexFactoryType>
static VertexFactoryType* CreateVertexFactory(TArray<TUniquePtr<VertexFactoryTypeBase>>& VertexFactories,
						 const FSkeletalMeshObjectGPUSkin::FVertexFactoryBuffers& InVertexBuffers,
						 ERHIFeatureLevel::Type FeatureLevel
						 )
{
	VertexFactoryType* VertexFactory = new VertexFactoryType(FeatureLevel, InVertexBuffers.NumVertices);
	VertexFactories.Add(TUniquePtr<VertexFactoryTypeBase>(VertexFactory));

	// Setup the update data for enqueue
	TDynamicUpdateVertexFactoryData<VertexFactoryType> VertexUpdateData(VertexFactory,InVertexBuffers);

	// update vertex factory components and sync it
	ENQUEUE_RENDER_COMMAND(InitGPUSkinVertexFactory)(
		[VertexUpdateData](FRHICommandList& CmdList)
		{
			typename VertexFactoryType::FDataType Data;
			InitGPUSkinVertexFactoryComponents<VertexFactoryType>(&Data, VertexUpdateData.VertexBuffers, VertexUpdateData.VertexFactory);
			VertexUpdateData.VertexFactory->SetData(Data);
			VertexUpdateData.VertexFactory->InitResource();
		}
	);

	return VertexFactory;
}

void FGPUSkinPassthroughVertexFactory::SetData(const FDataType& InData)
{
	FLocalVertexFactory::SetData(InData);
	const int32 DefaultBaseVertexIndex = 0;
	const int32 DefaultPreSkinBaseVertexIndex = 0;
	if (RHISupportsManualVertexFetch(GMaxRHIShaderPlatform))
	{
		UniformBuffer = CreateLocalVFUniformBuffer(this, Data.LODLightmapDataIndex, nullptr, DefaultBaseVertexIndex, DefaultPreSkinBaseVertexIndex);
	}
}

template <class VertexFactoryTypeBase, class VertexFactoryType>
void UpdateVertexFactory(TArray<TUniquePtr<VertexFactoryTypeBase>>& VertexFactories,
	const FSkeletalMeshObjectGPUSkin::FVertexFactoryBuffers& InVertexBuffers)
{
	for (TUniquePtr<VertexFactoryTypeBase>& FactoryPtr : VertexFactories)
	{
		VertexFactoryType* VertexFactory = (VertexFactoryType*)FactoryPtr.Get();

		if (VertexFactory != nullptr)
		{
			// Setup the update data for enqueue
			TDynamicUpdateVertexFactoryData<VertexFactoryType> VertexUpdateData(VertexFactory, InVertexBuffers);

			// update vertex factory components and sync it
			ENQUEUE_RENDER_COMMAND(UpdateGPUSkinVertexFactory)(
				[VertexUpdateData](FRHICommandList& CmdList)
			{
				typename VertexFactoryType::FDataType Data;
				InitGPUSkinVertexFactoryComponents<VertexFactoryType>(&Data, VertexUpdateData.VertexBuffers, VertexUpdateData.VertexFactory);
				VertexUpdateData.VertexFactory->SetData(Data);
			});
		}
	}
}

template<typename VertexFactoryType>
static void CreatePassthroughVertexFactory(ERHIFeatureLevel::Type InFeatureLevel, TArray<TUniquePtr<FGPUSkinPassthroughVertexFactory>>& PassthroughVertexFactories,
	VertexFactoryType* SourceVertexFactory)
{
	FGPUSkinPassthroughVertexFactory* NewPassthroughVertexFactory = new FGPUSkinPassthroughVertexFactory(InFeatureLevel);
	PassthroughVertexFactories.Add(TUniquePtr<FGPUSkinPassthroughVertexFactory>(NewPassthroughVertexFactory));

	// update vertex factory components and sync it
	ENQUEUE_RENDER_COMMAND(InitPassthroughGPUSkinVertexFactory)(
		[NewPassthroughVertexFactory, SourceVertexFactory](FRHICommandList& RHICmdList)
		{
			SourceVertexFactory->CopyDataTypeForPassthroughFactory(NewPassthroughVertexFactory);
			NewPassthroughVertexFactory->InitResource();
		}
	);
}

/**
 * Creates a vertex factory entry for the given type and initialize it on the render thread
 */
template <class VertexFactoryTypeBase, class VertexFactoryType>
static VertexFactoryType* CreateVertexFactoryMorph(TArray<TUniquePtr<VertexFactoryTypeBase>>& VertexFactories,
						 const FSkeletalMeshObjectGPUSkin::FVertexFactoryBuffers& InVertexBuffers,
						 ERHIFeatureLevel::Type FeatureLevel
						 )

{
	VertexFactoryType* VertexFactory = new VertexFactoryType(FeatureLevel, InVertexBuffers.NumVertices);
	VertexFactories.Add(TUniquePtr<VertexFactoryTypeBase>(VertexFactory));
						
	// Setup the update data for enqueue
	TDynamicUpdateVertexFactoryData<VertexFactoryType> VertexUpdateData(VertexFactory, InVertexBuffers);

	// update vertex factory components and sync it
	ENQUEUE_RENDER_COMMAND(InitGPUSkinVertexFactoryMorph)(
		[VertexUpdateData](FRHICommandList& RHICmdList)
		{
			typename VertexFactoryType::FDataType Data;
			InitGPUSkinVertexFactoryComponents<VertexFactoryType>(&Data, VertexUpdateData.VertexBuffers, VertexUpdateData.VertexFactory);
			InitMorphVertexFactoryComponents<VertexFactoryType>(&Data, VertexUpdateData.VertexBuffers);
			VertexUpdateData.VertexFactory->SetData(Data);
			VertexUpdateData.VertexFactory->InitResource();
		}
	);

	return VertexFactory;
}

template <class VertexFactoryTypeBase, class VertexFactoryType>
static void UpdateVertexFactoryMorph(TArray<TUniquePtr<VertexFactoryTypeBase>>& VertexFactories,
	const FSkeletalMeshObjectGPUSkin::FVertexFactoryBuffers& InVertexBuffers)
{
	for (TUniquePtr<VertexFactoryTypeBase>& FactoryPtr : VertexFactories)
	{
		VertexFactoryType* VertexFactory = (VertexFactoryType*)FactoryPtr.Get();

		if (VertexFactory != nullptr)
		{
			// Setup the update data for enqueue
			TDynamicUpdateVertexFactoryData<VertexFactoryType> VertexUpdateData(VertexFactory, InVertexBuffers);

			// update vertex factory components and sync it
			ENQUEUE_RENDER_COMMAND(InitGPUSkinVertexFactoryMorph)(
				[VertexUpdateData](FRHICommandList& RHICmdList)
			{
				typename VertexFactoryType::FDataType Data;
				InitGPUSkinVertexFactoryComponents<VertexFactoryType>(&Data, VertexUpdateData.VertexBuffers, VertexUpdateData.VertexFactory);
				InitMorphVertexFactoryComponents<VertexFactoryType>(&Data, VertexUpdateData.VertexBuffers);
				VertexUpdateData.VertexFactory->SetData(Data);
			});
		}
	}
}


// APEX cloth

/**
 * Creates a vertex factory entry for the given type and initialize it on the render thread
 */
template <class VertexFactoryTypeBase, class VertexFactoryType>
static void CreateVertexFactoryCloth(TArray<TUniquePtr<VertexFactoryTypeBase>>& VertexFactories,
						 const FSkeletalMeshObjectGPUSkin::FVertexFactoryBuffers& InVertexBuffers,
						 ERHIFeatureLevel::Type FeatureLevel
						 )

{
	VertexFactoryType* VertexFactory = new VertexFactoryType(FeatureLevel, InVertexBuffers.NumVertices);
	VertexFactories.Add(TUniquePtr<VertexFactoryTypeBase>(VertexFactory));
						
	// Setup the update data for enqueue
	TDynamicUpdateVertexFactoryData<VertexFactoryType> VertexUpdateData(VertexFactory, InVertexBuffers);

	// update vertex factory components and sync it
	ENQUEUE_RENDER_COMMAND(InitGPUSkinAPEXClothVertexFactory)(
		[VertexUpdateData](FRHICommandList& RHICmdList)
		{
			typename VertexFactoryType::FDataType Data;
			InitGPUSkinVertexFactoryComponents<VertexFactoryType>(&Data, VertexUpdateData.VertexBuffers, VertexUpdateData.VertexFactory);
			InitAPEXClothVertexFactoryComponents<VertexFactoryType>(&Data, VertexUpdateData.VertexBuffers);
			VertexUpdateData.VertexFactory->SetData(Data);
			VertexUpdateData.VertexFactory->InitResource();
		}
	);
}

template <class VertexFactoryTypeBase, class VertexFactoryType>
static void UpdateVertexFactoryCloth(TArray<TUniquePtr<VertexFactoryTypeBase>>& VertexFactories,
	const FSkeletalMeshObjectGPUSkin::FVertexFactoryBuffers& InVertexBuffers)
{
	for (TUniquePtr<VertexFactoryTypeBase>& FactoryPtr : VertexFactories)
	{
		VertexFactoryType* VertexFactory = (VertexFactoryType*)FactoryPtr.Get();

		if (VertexFactory != nullptr)
		{
			// Setup the update data for enqueue
			TDynamicUpdateVertexFactoryData<VertexFactoryType> VertexUpdateData(VertexFactory, InVertexBuffers);

			// update vertex factory components and sync it
			ENQUEUE_RENDER_COMMAND(InitGPUSkinAPEXClothVertexFactory)(
				[VertexUpdateData](FRHICommandList& RHICmdList)
			{
				typename VertexFactoryType::FDataType Data;
				InitGPUSkinVertexFactoryComponents<VertexFactoryType>(&Data, VertexUpdateData.VertexBuffers, VertexUpdateData.VertexFactory);
				InitAPEXClothVertexFactoryComponents<VertexFactoryType>(&Data, VertexUpdateData.VertexBuffers);
				VertexUpdateData.VertexFactory->SetData(Data);
			});
		}
	}
}


/**
 * Determine the current vertex buffers valid for the current LOD
 *
 * @param OutVertexBuffers output vertex buffers
 */
void FSkeletalMeshObjectGPUSkin::FSkeletalMeshObjectLOD::GetVertexBuffers(FVertexFactoryBuffers& OutVertexBuffers, FSkeletalMeshLODRenderData& LODData)
{
	OutVertexBuffers.StaticVertexBuffers = &LODData.StaticVertexBuffers;
	OutVertexBuffers.ColorVertexBuffer = MeshObjectColorBuffer;
	OutVertexBuffers.SkinWeightVertexBuffer = MeshObjectWeightBuffer;
	OutVertexBuffers.MorphVertexBuffer = &MorphVertexBuffer;
	OutVertexBuffers.APEXClothVertexBuffer = &LODData.ClothVertexBuffer;
	OutVertexBuffers.NumVertices = LODData.GetNumVertices();
}

/** 
 * Init vertex factory resources for this LOD 
 *
 * @param VertexBuffers - available vertex buffers to reference in vertex factory streams
 * @param Chunks - relevant chunk information (either original or from swapped influence)
 */
void FSkeletalMeshObjectGPUSkin::FVertexFactoryData::InitVertexFactories(
	const FVertexFactoryBuffers& VertexBuffers, 
	const TArray<FSkelMeshRenderSection>& Sections, 
	ERHIFeatureLevel::Type InFeatureLevel)
{
	// first clear existing factories (resources assumed to have been released already)
	// then [re]create the factories

	VertexFactories.Empty(Sections.Num());
	{
		for(int32 FactoryIdx = 0; FactoryIdx < Sections.Num(); ++FactoryIdx)
		{
			GPUSkinBoneInfluenceType BoneInfluenceType = VertexBuffers.SkinWeightVertexBuffer->GetBoneInfluenceType();
			if (BoneInfluenceType == GPUSkinBoneInfluenceType::DefaultBoneInfluence)
			{
				TGPUSkinVertexFactory<GPUSkinBoneInfluenceType::DefaultBoneInfluence>* VertexFactory = 
					CreateVertexFactory< FGPUBaseSkinVertexFactory, TGPUSkinVertexFactory<GPUSkinBoneInfluenceType::DefaultBoneInfluence> >(VertexFactories, VertexBuffers, InFeatureLevel);
				CreatePassthroughVertexFactory<TGPUSkinVertexFactory<GPUSkinBoneInfluenceType::DefaultBoneInfluence>>(InFeatureLevel, PassthroughVertexFactories, VertexFactory);
			}
			else if (BoneInfluenceType == GPUSkinBoneInfluenceType::ExtraBoneInfluence)
			{
				TGPUSkinVertexFactory<GPUSkinBoneInfluenceType::ExtraBoneInfluence>* VertexFactory = 
					CreateVertexFactory< FGPUBaseSkinVertexFactory, TGPUSkinVertexFactory<GPUSkinBoneInfluenceType::ExtraBoneInfluence> >(VertexFactories, VertexBuffers, InFeatureLevel);
				CreatePassthroughVertexFactory<TGPUSkinVertexFactory<GPUSkinBoneInfluenceType::ExtraBoneInfluence>>(InFeatureLevel, PassthroughVertexFactories, VertexFactory);
			}
			else
			{
				TGPUSkinVertexFactory<GPUSkinBoneInfluenceType::UnlimitedBoneInfluence>* VertexFactory = 
					CreateVertexFactory< FGPUBaseSkinVertexFactory, TGPUSkinVertexFactory<GPUSkinBoneInfluenceType::UnlimitedBoneInfluence> >(VertexFactories, VertexBuffers, InFeatureLevel);
				CreatePassthroughVertexFactory<TGPUSkinVertexFactory<GPUSkinBoneInfluenceType::UnlimitedBoneInfluence>>(InFeatureLevel, PassthroughVertexFactories, VertexFactory);
			}
		}
	}
}

/** 
 * Release vertex factory resources for this LOD 
 */
void FSkeletalMeshObjectGPUSkin::FVertexFactoryData::ReleaseVertexFactories()
{
	// Default factories
	for( int32 FactoryIdx=0; FactoryIdx < VertexFactories.Num(); FactoryIdx++)
	{
		BeginReleaseResource(VertexFactories[FactoryIdx].Get());
	}

	for (int32 FactoryIdx = 0; FactoryIdx < PassthroughVertexFactories.Num(); FactoryIdx++)
	{
		BeginReleaseResource(PassthroughVertexFactories[FactoryIdx].Get());
	}
}

void FSkeletalMeshObjectGPUSkin::FVertexFactoryData::InitMorphVertexFactories(
	const FVertexFactoryBuffers& VertexBuffers, 
	const TArray<FSkelMeshRenderSection>& Sections,
	bool bInUsePerBoneMotionBlur,
	ERHIFeatureLevel::Type InFeatureLevel)
{
	// clear existing factories (resources assumed to have been released already)
	MorphVertexFactories.Empty(Sections.Num());
	for( int32 FactoryIdx=0; FactoryIdx < Sections.Num(); FactoryIdx++ )
	{
		GPUSkinBoneInfluenceType BoneInfluenceType = VertexBuffers.SkinWeightVertexBuffer->GetBoneInfluenceType();
		if (BoneInfluenceType == GPUSkinBoneInfluenceType::DefaultBoneInfluence)
		{
			CreateVertexFactoryMorph<FGPUBaseSkinVertexFactory, TGPUSkinMorphVertexFactory< GPUSkinBoneInfluenceType::DefaultBoneInfluence > >(MorphVertexFactories,VertexBuffers,InFeatureLevel);
		}
		else if (BoneInfluenceType == GPUSkinBoneInfluenceType::ExtraBoneInfluence)
		{
			CreateVertexFactoryMorph<FGPUBaseSkinVertexFactory, TGPUSkinMorphVertexFactory<GPUSkinBoneInfluenceType::ExtraBoneInfluence> >(MorphVertexFactories, VertexBuffers,InFeatureLevel);
		}
		else
		{
			CreateVertexFactoryMorph<FGPUBaseSkinVertexFactory, TGPUSkinMorphVertexFactory<GPUSkinBoneInfluenceType::UnlimitedBoneInfluence> >(MorphVertexFactories, VertexBuffers, InFeatureLevel);
		}
	}
}

/** 
 * Release morph vertex factory resources for this LOD 
 */
void FSkeletalMeshObjectGPUSkin::FVertexFactoryData::ReleaseMorphVertexFactories()
{
	// Default morph factories
	for( int32 FactoryIdx=0; FactoryIdx < MorphVertexFactories.Num(); FactoryIdx++ )
	{
		BeginReleaseResource(MorphVertexFactories[FactoryIdx].Get());
	}
}

void FSkeletalMeshObjectGPUSkin::FVertexFactoryData::InitAPEXClothVertexFactories(
	const FVertexFactoryBuffers& VertexBuffers, 
	const TArray<FSkelMeshRenderSection>& Sections,
	ERHIFeatureLevel::Type InFeatureLevel)
{
	// clear existing factories (resources assumed to have been released already)
	ClothVertexFactories.Empty(Sections.Num());
	for( int32 FactoryIdx=0; FactoryIdx < Sections.Num(); FactoryIdx++ )
	{
		if (Sections[FactoryIdx].HasClothingData() && InFeatureLevel >= ERHIFeatureLevel::SM5)
		{
			GPUSkinBoneInfluenceType BoneInfluenceType = VertexBuffers.SkinWeightVertexBuffer->GetBoneInfluenceType();
			if (BoneInfluenceType == GPUSkinBoneInfluenceType::DefaultBoneInfluence)
			{
				CreateVertexFactoryCloth<FGPUBaseSkinAPEXClothVertexFactory, TGPUSkinAPEXClothVertexFactory<GPUSkinBoneInfluenceType::DefaultBoneInfluence> >(ClothVertexFactories, VertexBuffers, InFeatureLevel);
			}
			else if (BoneInfluenceType == GPUSkinBoneInfluenceType::ExtraBoneInfluence)
			{
				CreateVertexFactoryCloth<FGPUBaseSkinAPEXClothVertexFactory, TGPUSkinAPEXClothVertexFactory<GPUSkinBoneInfluenceType::ExtraBoneInfluence> >(ClothVertexFactories, VertexBuffers, InFeatureLevel);
			}
			else
			{
				CreateVertexFactoryCloth<FGPUBaseSkinAPEXClothVertexFactory, TGPUSkinAPEXClothVertexFactory<GPUSkinBoneInfluenceType::UnlimitedBoneInfluence> >(ClothVertexFactories, VertexBuffers, InFeatureLevel);
			}
		}
		else
		{
			ClothVertexFactories.Add(TUniquePtr<FGPUBaseSkinAPEXClothVertexFactory>(nullptr));
		}
	}
}

/** 
 * Release APEX cloth vertex factory resources for this LOD 
 */
void FSkeletalMeshObjectGPUSkin::FVertexFactoryData::ReleaseAPEXClothVertexFactories()
{
	// Default APEX cloth factories
	for( int32 FactoryIdx=0; FactoryIdx < ClothVertexFactories.Num(); FactoryIdx++ )
	{
		TUniquePtr<FGPUBaseSkinAPEXClothVertexFactory>& ClothVertexFactory = ClothVertexFactories[FactoryIdx];
		if (ClothVertexFactory)
		{
			BeginReleaseResource(ClothVertexFactory->GetVertexFactory());
		}
	}
}

void FSkeletalMeshObjectGPUSkin::FVertexFactoryData::UpdateVertexFactoryData(const FVertexFactoryBuffers& VertexBuffers)
{
	GPUSkinBoneInfluenceType BoneInfluenceType = VertexBuffers.SkinWeightVertexBuffer->GetBoneInfluenceType();
	if (BoneInfluenceType == GPUSkinBoneInfluenceType::DefaultBoneInfluence)
	{
		UpdateVertexFactory<FGPUBaseSkinVertexFactory, TGPUSkinVertexFactory<GPUSkinBoneInfluenceType::DefaultBoneInfluence>>(VertexFactories, VertexBuffers);
		UpdateVertexFactoryCloth<FGPUBaseSkinAPEXClothVertexFactory, TGPUSkinAPEXClothVertexFactory<GPUSkinBoneInfluenceType::DefaultBoneInfluence>>(ClothVertexFactories, VertexBuffers);
		UpdateVertexFactoryMorph<FGPUBaseSkinVertexFactory, TGPUSkinMorphVertexFactory<GPUSkinBoneInfluenceType::DefaultBoneInfluence>>(MorphVertexFactories, VertexBuffers);
	}
	else if (BoneInfluenceType == GPUSkinBoneInfluenceType::ExtraBoneInfluence)
	{
		UpdateVertexFactory<FGPUBaseSkinVertexFactory, TGPUSkinVertexFactory<GPUSkinBoneInfluenceType::ExtraBoneInfluence>>(VertexFactories, VertexBuffers);
		UpdateVertexFactoryCloth<FGPUBaseSkinAPEXClothVertexFactory, TGPUSkinAPEXClothVertexFactory<GPUSkinBoneInfluenceType::ExtraBoneInfluence>>(ClothVertexFactories, VertexBuffers);
		UpdateVertexFactoryMorph<FGPUBaseSkinVertexFactory, TGPUSkinMorphVertexFactory<GPUSkinBoneInfluenceType::ExtraBoneInfluence>>(MorphVertexFactories, VertexBuffers);
	}
	else
	{
		UpdateVertexFactory<FGPUBaseSkinVertexFactory, TGPUSkinVertexFactory<GPUSkinBoneInfluenceType::UnlimitedBoneInfluence>>(VertexFactories, VertexBuffers);
		UpdateVertexFactoryCloth<FGPUBaseSkinAPEXClothVertexFactory, TGPUSkinAPEXClothVertexFactory<GPUSkinBoneInfluenceType::UnlimitedBoneInfluence>>(ClothVertexFactories, VertexBuffers);
		UpdateVertexFactoryMorph<FGPUBaseSkinVertexFactory, TGPUSkinMorphVertexFactory<GPUSkinBoneInfluenceType::UnlimitedBoneInfluence>>(MorphVertexFactories, VertexBuffers);
	}
}

void FSkeletalMeshObjectGPUSkin::FSkeletalMeshObjectLOD::InitResources(const FSkelMeshObjectLODInfo& MeshLODInfo, FSkelMeshComponentLODInfo* CompLODInfo, ERHIFeatureLevel::Type InFeatureLevel)
{
	check(SkelMeshRenderData);
	check(SkelMeshRenderData->LODRenderData.IsValidIndex(LODIndex));

	// vertex buffer for each lod has already been created when skelmesh was loaded
	FSkeletalMeshLODRenderData& LODData = SkelMeshRenderData->LODRenderData[LODIndex];
	
	// If we have a skin weight override buffer (and it's the right size) use it
	if (CompLODInfo &&
		CompLODInfo->OverrideSkinWeights &&
		CompLODInfo->OverrideSkinWeights->GetNumVertices() == LODData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices())
	{
		check(LODData.SkinWeightVertexBuffer.GetMaxBoneInfluences() == CompLODInfo->OverrideSkinWeights->GetMaxBoneInfluences());
		MeshObjectWeightBuffer = CompLODInfo->OverrideSkinWeights;
	}
	else if (CompLODInfo && CompLODInfo->OverrideProfileSkinWeights &&
			CompLODInfo->OverrideProfileSkinWeights->GetNumVertices() == LODData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices())
	{
		check(LODData.SkinWeightVertexBuffer.GetMaxBoneInfluences() == CompLODInfo->OverrideProfileSkinWeights->GetMaxBoneInfluences());
		MeshObjectWeightBuffer = CompLODInfo->OverrideProfileSkinWeights;
	}
	else
	{
		MeshObjectWeightBuffer = LODData.GetSkinWeightVertexBuffer();
	}

	// If we have a vertex color override buffer (and it's the right size) use it
	if (CompLODInfo &&
		CompLODInfo->OverrideVertexColors &&
		CompLODInfo->OverrideVertexColors->GetNumVertices() == LODData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices())
	{
		MeshObjectColorBuffer = CompLODInfo->OverrideVertexColors;
	}
	else
	{
		MeshObjectColorBuffer = &LODData.StaticVertexBuffers.ColorVertexBuffer;
	}

	// Vertex buffers available for the LOD
	FVertexFactoryBuffers VertexBuffers;
	GetVertexBuffers(VertexBuffers, LODData);

	// init gpu skin factories
	GPUSkinVertexFactories.InitVertexFactories(VertexBuffers, LODData.RenderSections, InFeatureLevel);
	if (LODData.HasClothData() )
	{
		GPUSkinVertexFactories.InitAPEXClothVertexFactories(VertexBuffers, LODData.RenderSections, InFeatureLevel);
	}
}

/** 
 * Release rendering resources for this LOD 
 */
void FSkeletalMeshObjectGPUSkin::FSkeletalMeshObjectLOD::ReleaseResources()
{	
	// Release gpu skin vertex factories
	GPUSkinVertexFactories.ReleaseVertexFactories();

	// Release APEX cloth vertex factory
	GPUSkinVertexFactories.ReleaseAPEXClothVertexFactories();
}

void FSkeletalMeshObjectGPUSkin::FSkeletalMeshObjectLOD::InitMorphResources(const FSkelMeshObjectLODInfo& MeshLODInfo, bool bInUsePerBoneMotionBlur, ERHIFeatureLevel::Type InFeatureLevel)
{
	check(SkelMeshRenderData);
	check(SkelMeshRenderData->LODRenderData.IsValidIndex(LODIndex));

	// vertex buffer for each lod has already been created when skelmesh was loaded
	FSkeletalMeshLODRenderData& LODData = SkelMeshRenderData->LODRenderData[LODIndex];

	// init the delta vertex buffer for this LOD
	BeginInitResource(&MorphVertexBuffer);

	// Vertex buffers available for the LOD
	FVertexFactoryBuffers VertexBuffers;
	GetVertexBuffers(VertexBuffers, LODData);
	// init morph skin factories
	GPUSkinVertexFactories.InitMorphVertexFactories(VertexBuffers, LODData.RenderSections, bInUsePerBoneMotionBlur, InFeatureLevel);
}

/** 
* Release rendering resources for the morph stream of this LOD
*/
void FSkeletalMeshObjectGPUSkin::FSkeletalMeshObjectLOD::ReleaseMorphResources()
{
	// Release morph vertex factories
	GPUSkinVertexFactories.ReleaseMorphVertexFactories();
	// release the delta vertex buffer
	BeginReleaseResource(&MorphVertexBuffer);
}


TArray<FTransform>* FSkeletalMeshObjectGPUSkin::GetComponentSpaceTransforms() const
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if(DynamicData)
	{
		return &(DynamicData->MeshComponentSpaceTransforms);
	}
	else
#endif
	{
		return NULL;
	}
}

const TArray<FMatrix>& FSkeletalMeshObjectGPUSkin::GetReferenceToLocalMatrices() const
{
	return DynamicData->ReferenceToLocal;
}

/*-----------------------------------------------------------------------------
FDynamicSkelMeshObjectDataGPUSkin
-----------------------------------------------------------------------------*/

void FDynamicSkelMeshObjectDataGPUSkin::Clear()
{
	ReferenceToLocal.Reset();
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST) 
	MeshComponentSpaceTransforms.Reset();
#endif
	LODIndex = 0;
	ActiveMorphTargets.Reset();
	MorphTargetWeights.Reset();
	NumWeightedActiveMorphTargets = 0;
	ClothingSimData.Reset();
	ClothBlendWeight = 0.0f;
	bIsSkinCacheAllowed = false;
#if RHI_RAYTRACING
	bAnySegmentUsesWorldPositionOffset = false;
#endif
}

#define SKELETON_POOL_GPUSKINS 1
#if SKELETON_POOL_GPUSKINS
TArray<FDynamicSkelMeshObjectDataGPUSkin*> FreeGpuSkins;
FCriticalSection FreeGpuSkinsCriticalSection;

static int32 GPoolGpuSkins = 1;
static int32 GMinPoolCount = 0;
static int32 GAllocationCounter = 0;
static const int32 GAllocationsBeforeCleanup = 1000; // number of allocations we make before we clean up the pool, this number is increased when we have to allocate not from the pool
static FAutoConsoleVariableRef CVarPoolGpuSkins(
	TEXT("r.GpuSkin.Pool"),
	GPoolGpuSkins,
	TEXT("Should we pool gpu skins.\n")
	TEXT(" 0: Don't pool anything\n")
	TEXT(" 1: Pool gpu skins bro (default)\n"),
	ECVF_Default
);
#endif


FDynamicSkelMeshObjectDataGPUSkin* FDynamicSkelMeshObjectDataGPUSkin::AllocDynamicSkelMeshObjectDataGPUSkin()
{
#if SKELETON_POOL_GPUSKINS
	if (!GPoolGpuSkins)
	{
		return new FDynamicSkelMeshObjectDataGPUSkin;
	}

	FScopeLock S(&FreeGpuSkinsCriticalSection);
	++GAllocationCounter;
	GMinPoolCount = FMath::Min(FreeGpuSkins.Num(), GMinPoolCount);
	if ( FreeGpuSkins.Num() > 0 )
	{
		FDynamicSkelMeshObjectDataGPUSkin *Result = FreeGpuSkins[0];
		FreeGpuSkins.RemoveAtSwap(0);
		return Result;
	}
	else
	{
		return new FDynamicSkelMeshObjectDataGPUSkin;
	}
#else
	return new FDynamicSkelMeshObjectDataGPUSkin;
#endif
}

void FDynamicSkelMeshObjectDataGPUSkin::FreeDynamicSkelMeshObjectDataGPUSkin(FDynamicSkelMeshObjectDataGPUSkin* Who)
{
#if SKELETON_POOL_GPUSKINS
	if (!GPoolGpuSkins)
	{
		delete Who;

		if ( FreeGpuSkins.Num() > 0 )
		{
			FScopeLock S(&FreeGpuSkinsCriticalSection);
			for ( FDynamicSkelMeshObjectDataGPUSkin* I : FreeGpuSkins )
			{
				delete I;
			}
			FreeGpuSkins.Empty();
		}
		return;
	}

	Who->Clear();
	FScopeLock S(&FreeGpuSkinsCriticalSection);
	FreeGpuSkins.Add(Who);
	if ( GAllocationCounter > GAllocationsBeforeCleanup )
	{
		GAllocationCounter = 0;
		for ( int32 I = 0; I < GMinPoolCount; ++I )
		{
			delete FreeGpuSkins[0];
			FreeGpuSkins.RemoveAtSwap(0);
		}
		GMinPoolCount = FreeGpuSkins.Num();
	}
#else
	delete Who;
#endif
}

void FDynamicSkelMeshObjectDataGPUSkin::InitDynamicSkelMeshObjectDataGPUSkin(
	USkinnedMeshComponent* InMeshComponent,
	FSkeletalMeshRenderData* InSkeletalMeshRenderData,
	int32 InLODIndex,
	const TArray<FActiveMorphTarget>& InActiveMorphTargets,
	const TArray<float>& InMorphTargetWeights, 
	EPreviousBoneTransformUpdateMode PreviousBoneTransformUpdateMode
	)
{
	LODIndex = InLODIndex;
	check(!ActiveMorphTargets.Num() && !ReferenceToLocal.Num() && !ClothingSimData.Num() && !MorphTargetWeights.Num());

	// append instead of equals to avoid alloc
	ActiveMorphTargets.Append(InActiveMorphTargets);
	MorphTargetWeights.Append(InMorphTargetWeights);
	NumWeightedActiveMorphTargets = 0;

	// Gather any bones referenced by shadow shapes
	FSkeletalMeshSceneProxy* SkeletalMeshProxy = (FSkeletalMeshSceneProxy*)InMeshComponent->SceneProxy;
	const TArray<FBoneIndexType>* ExtraRequiredBoneIndices = SkeletalMeshProxy ? &SkeletalMeshProxy->GetSortedShadowBoneIndices() : nullptr;

	// update ReferenceToLocal
	UpdateRefToLocalMatrices( ReferenceToLocal, InMeshComponent, InSkeletalMeshRenderData, LODIndex, ExtraRequiredBoneIndices );
	switch(PreviousBoneTransformUpdateMode)
	{
	case EPreviousBoneTransformUpdateMode::None:
		// otherwise, clear it, it will use previous buffer
		PreviousReferenceToLocal.Reset();
		break;
	case EPreviousBoneTransformUpdateMode::UpdatePrevious:
		UpdatePreviousRefToLocalMatrices(PreviousReferenceToLocal, InMeshComponent, InSkeletalMeshRenderData, LODIndex, ExtraRequiredBoneIndices);
		break;
	case EPreviousBoneTransformUpdateMode::DuplicateCurrentToPrevious:
		UpdateRefToLocalMatrices(PreviousReferenceToLocal, InMeshComponent, InSkeletalMeshRenderData, LODIndex, ExtraRequiredBoneIndices);
		break;
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	check(!MeshComponentSpaceTransforms.Num());
	// append instead of equals to avoid alloc
	MeshComponentSpaceTransforms.Append(InMeshComponent->GetComponentSpaceTransforms());
#endif
	SectionIdsUseByActiveMorphTargets.Empty();
	// find number of morphs that are currently weighted and will affect the mesh
	for( int32 MorphIdx=ActiveMorphTargets.Num()-1; MorphIdx >= 0; MorphIdx-- )
	{
		const FActiveMorphTarget& Morph = ActiveMorphTargets[MorphIdx];
		const float MorphTargetWeight = MorphTargetWeights[Morph.WeightIndex];
		const float MorphAbsWeight = FMath::Abs(MorphTargetWeight);

		if( Morph.MorphTarget != NULL && 
			MorphAbsWeight >= MinMorphTargetBlendWeight &&
			MorphAbsWeight <= MaxMorphTargetBlendWeight &&
			Morph.MorphTarget->HasDataForLOD(LODIndex) ) 
		{
			NumWeightedActiveMorphTargets++;
			for (int32 SecId = 0; SecId < Morph.MorphTarget->MorphLODModels[LODIndex].SectionIndices.Num(); ++SecId)
			{
				SectionIdsUseByActiveMorphTargets.AddUnique(Morph.MorphTarget->MorphLODModels[LODIndex].SectionIndices[SecId]);
			}
		}
		else
		{
			ActiveMorphTargets.RemoveAt(MorphIdx, 1, false);
		}
	}
	// Update the clothing simulation mesh positions and normals
	UpdateClothSimulationData(InMeshComponent);

	bIsSkinCacheAllowed = InMeshComponent->IsSkinCacheAllowed(InLODIndex);

#if RHI_RAYTRACING
	if (SkeletalMeshProxy != nullptr)
	{
		bAnySegmentUsesWorldPositionOffset = SkeletalMeshProxy->bAnySegmentUsesWorldPositionOffset;
	}
#endif
}

bool FDynamicSkelMeshObjectDataGPUSkin::ActiveMorphTargetsEqual( const TArray<FActiveMorphTarget>& CompareActiveMorphTargets, const TArray<float>& CompareMorphTargetWeights)
{
	bool Result=true;
	if( CompareActiveMorphTargets.Num() == ActiveMorphTargets.Num() )
	{
		for( int32 MorphIdx=0; MorphIdx < ActiveMorphTargets.Num(); MorphIdx++ )
		{
			const FActiveMorphTarget& Morph = ActiveMorphTargets[MorphIdx];
			const FActiveMorphTarget& CompMorph = CompareActiveMorphTargets[MorphIdx];

			if( Morph.MorphTarget != CompMorph.MorphTarget ||
				FMath::Abs(MorphTargetWeights[Morph.WeightIndex] - CompareMorphTargetWeights[CompMorph.WeightIndex]) >= GMorphTargetWeightThreshold)
			{
				Result=false;
				break;
			}
		}
	}
	else
	{
		Result = false;
	}
	return Result;
}

bool FDynamicSkelMeshObjectDataGPUSkin::UpdateClothSimulationData(USkinnedMeshComponent* InMeshComponent)
{
	USkeletalMeshComponent* SimMeshComponent = Cast<USkeletalMeshComponent>(InMeshComponent);

	if (SimMeshComponent)
	{
		ClothObjectLocalToWorld = SimMeshComponent->GetComponentToWorld().ToMatrixWithScale();
		if(SimMeshComponent->bDisableClothSimulation)
		{
			ClothBlendWeight = 0.0f;
			ClothingSimData.Reset();
		}
		else
		{
			ClothBlendWeight = SimMeshComponent->ClothBlendWeight;
			ClothingSimData = SimMeshComponent->GetCurrentClothingData_GameThread();
		}

		return true;
	}
	return false;
}
