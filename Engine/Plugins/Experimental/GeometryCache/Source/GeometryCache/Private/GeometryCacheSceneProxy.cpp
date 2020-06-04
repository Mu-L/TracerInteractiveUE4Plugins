// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCacheSceneProxy.h"
#include "MaterialShared.h"
#include "SceneManagement.h"
#include "EngineGlobals.h"
#include "Materials/Material.h"
#include "Engine/Engine.h"
#include "GeometryCacheComponent.h"
#include "GeometryCacheMeshData.h"
#include "GeometryCache.h"
#include "GeometryCacheTrackStreamable.h"
#include "GeometryCacheModule.h"
#include "GeometryCacheHelpers.h"
#include "RayTracingDefinitions.h"
#include "RayTracingInstance.h"

DECLARE_CYCLE_STAT(TEXT("Gather Mesh Elements"), STAT_GeometryCacheSceneProxy_GetMeshElements, STATGROUP_GeometryCache);
DECLARE_DWORD_COUNTER_STAT(TEXT("Triangle Count"), STAT_GeometryCacheSceneProxy_TriangleCount, STATGROUP_GeometryCache);
DECLARE_DWORD_COUNTER_STAT(TEXT("Batch Count"), STAT_GeometryCacheSceneProxy_MeshBatchCount, STATGROUP_GeometryCache);
DECLARE_CYCLE_STAT(TEXT("Vertex Buffer Update"), STAT_VertexBufferUpdate, STATGROUP_GeometryCache);
DECLARE_CYCLE_STAT(TEXT("Index Buffer Update"), STAT_IndexBufferUpdate, STATGROUP_GeometryCache);
DECLARE_CYCLE_STAT(TEXT("Buffer Update Task"), STAT_BufferUpdateTask, STATGROUP_GeometryCache);
DECLARE_CYCLE_STAT(TEXT("InterpolateFrames"), STAT_InterpolateFrames, STATGROUP_GeometryCache);

static TAutoConsoleVariable<int32> CVarOffloadUpdate(
	TEXT("GeometryCache.OffloadUpdate"),
	0,
	TEXT("Offloat some updates from the render thread to the workers & RHI threads."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarInterpolateFrames(
	TEXT("GeometryCache.InterpolateFrames"),
	1,
	TEXT("Interpolate between geometry cache frames (if topology allows this)."),
	ECVF_Scalability | ECVF_RenderThreadSafe);


/**
* All vertex information except the position.
*/
struct FNoPositionVertex
{
	FVector2D TextureCoordinate[MAX_STATIC_TEXCOORDS];
	FPackedNormal TangentX;
	FPackedNormal TangentZ;
	FColor Color;
};

/** Return the GeometryCacheMeshData that was selected for use during FrameUpdate, which must also be used during GetDynamicMeshElements */
static const FGeometryCacheMeshData* GetSelectedMeshData(const FGeomCacheTrackProxy* InTrackProxy, float Time, bool bLooping, bool bIsPlayingBackwards)
{
	// Need to determine which MeshData was selected for use in FGeometryCacheSceneProxy::FrameUpdate
	// so the same conditions must be checked
	int32 ExpectedFrameIndex;
	int32 ExpectedNextFrameIndex;
	float ExpectedInterpolationFactor;
	FGeomCacheTrackProxy* TrackProxy = const_cast<FGeomCacheTrackProxy*>(InTrackProxy); // the TrackProxy functions should have been const...
	TrackProxy->FindSampleIndexesFromTime(Time, bLooping, bIsPlayingBackwards, ExpectedFrameIndex, ExpectedNextFrameIndex, ExpectedInterpolationFactor);

	// The decoding status can be deduced from the state of the TrackProxy
	const bool bCanInterpolate = TrackProxy->IsTopologyCompatible(TrackProxy->FrameIndex, TrackProxy->NextFrameIndex);
	const bool bDecodedAnything = TrackProxy->NextFrameIndex == ExpectedNextFrameIndex;
	const bool bDecoderError = TrackProxy->FrameIndex == -1 || TrackProxy->NextFrameIndex == -1;

	bool bNextFrameSelected = false;
	if (bCanInterpolate && !bDecoderError && CVarInterpolateFrames.GetValueOnRenderThread() != 0)
	{
		bNextFrameSelected = false;
	}
	else if (bDecodedAnything || bDecoderError)
	{
		bNextFrameSelected = !!FMath::RoundToInt(TrackProxy->InterpolationFactor) && TrackProxy->NextFrameMeshData->Positions.Num() > 0;
	}

	return bNextFrameSelected ? TrackProxy->NextFrameMeshData : TrackProxy->MeshData;
}

FGeometryCacheSceneProxy::FGeometryCacheSceneProxy(UGeometryCacheComponent* Component) 
: FGeometryCacheSceneProxy(Component, [this]() { return new FGeomCacheTrackProxy(GetScene().GetFeatureLevel()); })
{
}

FGeometryCacheSceneProxy::FGeometryCacheSceneProxy(UGeometryCacheComponent* Component, TFunction<FGeomCacheTrackProxy*()> TrackProxyCreator)
: FPrimitiveSceneProxy(Component)
, MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetFeatureLevel()))
, CreateTrackProxy(TrackProxyCreator)
{
	Time = Component->GetAnimationTime();
	bLooping = Component->IsLooping();
	bAlwaysHasVelocity = true;
	PlaybackSpeed = (Component->IsPlaying()) ? Component->GetPlaybackSpeed() : 0.0f;
	UpdatedFrameNum = 0;

	// Copy each section
	const int32 NumTracks = Component->TrackSections.Num();
	Tracks.Reserve(NumTracks);
	for (int32 TrackIdx = 0; TrackIdx < NumTracks; TrackIdx++)
	{
		FTrackRenderData& SrcSection = Component->TrackSections[TrackIdx];
		UGeometryCacheTrack* CurrentTrack = Component->GeometryCache->Tracks[TrackIdx];

		const FGeometryCacheTrackSampleInfo& SampleInfo = CurrentTrack->GetSampleInfo(Time, bLooping);

		// Add track only if it has (visible) geometry
		if (SampleInfo.NumVertices > 0)
		{
			FGeomCacheTrackProxy* NewSection = CreateTrackProxy();

			NewSection->Track = CurrentTrack;
			NewSection->WorldMatrix = SrcSection.Matrix;
			NewSection->FrameIndex = -1;
			NewSection->UploadedSampleIndex = -1;
			NewSection->NextFrameIndex = -1;
			NewSection->InterpolationFactor = 0.f;
			NewSection->NextFrameMeshData = nullptr;

			// Allocate verts


			NewSection->TangentXBuffer.Init(SampleInfo.NumVertices * sizeof(FPackedNormal));
			NewSection->TangentZBuffer.Init(SampleInfo.NumVertices * sizeof(FPackedNormal));
			NewSection->TextureCoordinatesBuffer.Init(SampleInfo.NumVertices * sizeof(FVector2D));
			NewSection->ColorBuffer.Init(SampleInfo.NumVertices * sizeof(FColor));


			//NewSection->VertexBuffer.Init(SampleInfo.NumVertices * sizeof(FNoPositionVertex));
			NewSection->PositionBuffers[0].Init(SampleInfo.NumVertices * sizeof(FVector));
			NewSection->PositionBuffers[1].Init(SampleInfo.NumVertices * sizeof(FVector));
			NewSection->CurrentPositionBufferIndex = -1;
			NewSection->PositionBufferFrameIndices[0] = NewSection->PositionBufferFrameIndices[1] = -1;
			NewSection->PositionBufferFrameTimes[0] = NewSection->PositionBufferFrameTimes[1] = -1.0f;

			// Allocate index buffer
			NewSection->IndexBuffer.NumIndices = SampleInfo.NumIndices;

			// Init vertex factory
			NewSection->VertexFactory.Init(&NewSection->PositionBuffers[0], &NewSection->PositionBuffers[1], &NewSection->TangentXBuffer, &NewSection->TangentZBuffer, &NewSection->TextureCoordinatesBuffer, &NewSection->ColorBuffer);

			// Enqueue initialization of render resource
			BeginInitResource(&NewSection->PositionBuffers[0]);
			BeginInitResource(&NewSection->PositionBuffers[1]);
			BeginInitResource(&NewSection->TangentXBuffer);
			BeginInitResource(&NewSection->TangentZBuffer);
			BeginInitResource(&NewSection->TextureCoordinatesBuffer);
			BeginInitResource(&NewSection->ColorBuffer);			
			BeginInitResource(&NewSection->IndexBuffer);
			BeginInitResource(&NewSection->VertexFactory);

			// Grab materials
			int32 Dummy = -1;
			NewSection->MeshData = new FGeometryCacheMeshData();
			NewSection->UpdateMeshData(Time, bLooping, Dummy, *NewSection->MeshData);
			NewSection->NextFrameMeshData = new FGeometryCacheMeshData();

			// Some basic sanity checks
			for (FGeometryCacheMeshBatchInfo& BatchInfo : NewSection->MeshData->BatchesInfo)
			{
				UMaterialInterface* Material = Component->GetMaterial(BatchInfo.MaterialIndex);
				if (Material == nullptr || !Material->CheckMaterialUsage_Concurrent(EMaterialUsage::MATUSAGE_GeometryCache))
				{
					Material = UMaterial::GetDefaultMaterial(MD_Surface);
				}

				NewSection->Materials.Add(Material);
			}

			// Save ref to new section
			Tracks.Add(NewSection);
		}
	}

	if (IsRayTracingEnabled())
	{
		// Update at least once after the scene proxy has been constructed
		// Otherwise it is invisible until animation starts
		FGeometryCacheSceneProxy* SceneProxy = this;
		ENQUEUE_RENDER_COMMAND(FGeometryCacheUpdateAnimation)(
			[SceneProxy](FRHICommandListImmediate& RHICmdList)
		{
			SceneProxy->FrameUpdate();
		});

#if RHI_RAYTRACING
		{
			ENQUEUE_RENDER_COMMAND(FGeometryCacheInitRayTracingGeometry)(
				[SceneProxy](FRHICommandListImmediate& RHICmdList)
			{
				for (FGeomCacheTrackProxy* Section : SceneProxy->Tracks)
				{
					if (Section != nullptr)
					{
						FRayTracingGeometryInitializer Initializer;
						const int PositionBufferIndex = Section->CurrentPositionBufferIndex != -1 ? Section->CurrentPositionBufferIndex % 2 : 0;
						Initializer.IndexBuffer = Section->IndexBuffer.IndexBufferRHI;
						Initializer.TotalPrimitiveCount = 0;
						Initializer.GeometryType = RTGT_Triangles;
						Initializer.bFastBuild = false;

						TArray<FRayTracingGeometrySegment> Segments;
						const FGeometryCacheMeshData* MeshData = GetSelectedMeshData(Section, SceneProxy->Time, SceneProxy->bLooping, SceneProxy->bIsPlayingBackwards);
						for (const FGeometryCacheMeshBatchInfo& BatchInfo : MeshData->BatchesInfo)
						{
							FRayTracingGeometrySegment Segment;
							Segment.FirstPrimitive = BatchInfo.StartIndex / 3;
							Segment.NumPrimitives = BatchInfo.NumTriangles;
							Segment.VertexBuffer = Section->PositionBuffers[PositionBufferIndex].VertexBufferRHI;
							Segments.Add(Segment);
							Initializer.TotalPrimitiveCount += BatchInfo.NumTriangles;
						}

						Initializer.Segments = Segments;

						Section->RayTracingGeometry.SetInitializer(Initializer);
						Section->RayTracingGeometry.InitResource();
					}
				}
			});
		}
#endif
	}
}

FGeometryCacheSceneProxy::~FGeometryCacheSceneProxy()
{
	for (FGeomCacheTrackProxy* Section : Tracks)
	{
		if (Section != nullptr)
		{
			Section->TangentXBuffer.ReleaseResource();
			Section->TangentZBuffer.ReleaseResource();
			Section->TextureCoordinatesBuffer.ReleaseResource();
			Section->ColorBuffer.ReleaseResource();
			Section->IndexBuffer.ReleaseResource();
			Section->VertexFactory.ReleaseResource();
			Section->PositionBuffers[0].ReleaseResource();
			Section->PositionBuffers[1].ReleaseResource();
		#if RHI_RAYTRACING
			Section->RayTracingGeometry.ReleaseResource();
		#endif
			delete Section->MeshData;
			if (Section->NextFrameMeshData != nullptr)
				delete Section->NextFrameMeshData;
			delete Section;
		}
	}
	Tracks.Empty();
}

#if 0
FRHICOMMAND_MACRO(FRHICommandUpdateGeometryCacheBuffer)
{
	FGraphEventRef BufferGenerationCompleteFence;

	FRHIVertexBuffer* VertexBuffer;
	//void *VertexData;
	//uint32 VertexSize;
	TArray<uint8> VertexData;

	FRHIIndexBuffer* IndexBuffer;
	//void *IndexData;
	//uint32 IndexSize;
	TArray<uint8> IndexData;
	
	virtual ~FRHICommandUpdateGeometryCacheBuffer() {}
	
	FORCEINLINE_DEBUGGABLE FRHICommandUpdateGeometryCacheBuffer(
		FGraphEventRef& InBufferGenerationCompleteFence,
		FRHIVertexBuffer* InVertexBuffer,
		void *InVertexData,
		uint32 InVertexSize,
		FRHIIndexBuffer* InIndexBuffer,
		void *InIndexData,
		uint32 InIndexSize)
	:
		BufferGenerationCompleteFence(InBufferGenerationCompleteFence)
		, VertexBuffer(InVertexBuffer)
		, IndexBuffer(InIndexBuffer)
	{
		VertexData.SetNumUninitialized(InVertexSize);
		FMemory::Memcpy(VertexData.GetData(), InVertexData, InVertexSize);
		IndexData.SetNumUninitialized(InIndexSize);
		FMemory::Memcpy(IndexData.GetData(), InIndexData, InIndexSize);
	}

	/**
		This is scheduled by the render thread on the RHI thread and defers updating the buffers untill just before rendering.
		That way we can run the decoding/interpolation on the task graph.
		Completion of these tasks is marked by the BufferGenerationCompleteFence
	*/
	void Execute(FRHICommandListBase& CmdList)
	{
		//FTaskGraphInterface::Get().WaitUntilTaskCompletes(BufferGenerationCompleteFence, IsRunningRHIInSeparateThread() ? ENamedThreads::RHIThread : ENamedThreads::RenderThread);

		// Upload vertex data
		void* RESTRICT Data = (void* RESTRICT)GDynamicRHI->RHILockVertexBuffer(VertexBuffer, 0, VertexData.Num(), RLM_WriteOnly);
		FMemory::BigBlockMemcpy(Data, VertexData.GetData(), VertexData.Num());
		GDynamicRHI->RHIUnlockVertexBuffer(VertexBuffer);

		// Upload index data
		Data = (void* RESTRICT)GDynamicRHI->RHILockIndexBuffer(IndexBuffer, 0, IndexData.Num(), RLM_WriteOnly);
		FMemory::BigBlockMemcpy(Data, IndexData.GetData(), IndexData.Num());
		GDynamicRHI->RHIUnlockIndexBuffer(IndexBuffer);

		// Make sure to release refcounted things asap
		IndexBuffer = nullptr;
		VertexBuffer = nullptr;
		BufferGenerationCompleteFence = nullptr;
	}
};
#endif

class FGeometryCacheVertexFactoryUserDataWrapper : public FOneFrameResource
{
public:
	FGeometryCacheVertexFactoryUserData Data;
};

static float OneOver255 = 1.0f / 255.0f;

// Avoid converting from 8 bit normalized to float and back again.
inline FPackedNormal InterpolatePackedNormal(const FPackedNormal &A, const FPackedNormal &B, int32 ScaledFactor, int32 OneMinusScaledFactor)
{
	FPackedNormal result;
	result.Vector.X = (A.Vector.X * OneMinusScaledFactor + B.Vector.X * ScaledFactor) * OneOver255;
	result.Vector.Y = (A.Vector.Y * OneMinusScaledFactor + B.Vector.Y * ScaledFactor) * OneOver255;
	result.Vector.Z = (A.Vector.Z * OneMinusScaledFactor + B.Vector.Z * ScaledFactor) * OneOver255;
	result.Vector.W = (A.Vector.W * OneMinusScaledFactor + B.Vector.W * ScaledFactor) * OneOver255;
	return result;
}

// Avoid converting from 8 bit normalized to float and back again.
inline FColor InterpolatePackedColor(const FColor &A, const FColor &B, int32 ScaledFactor, int32 OneMinusScaledFactor)
{
	FColor result;
	result.R = (A.R * OneMinusScaledFactor + B.R * ScaledFactor) * OneOver255;
	result.G = (A.G * OneMinusScaledFactor + B.G * ScaledFactor) * OneOver255;
	result.B = (A.B * OneMinusScaledFactor + B.B * ScaledFactor) * OneOver255;
	result.A = (A.A * OneMinusScaledFactor + B.A * ScaledFactor) * OneOver255;
	return result;
}

SIZE_T FGeometryCacheSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

void FGeometryCacheSceneProxy::CreateMeshBatch(
	const FGeomCacheTrackProxy* TrackProxy, 
	const FGeometryCacheMeshBatchInfo& BatchInfo, 
	FGeometryCacheVertexFactoryUserDataWrapper& UserDataWrapper,
	FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer,
	FMeshBatch& Mesh) const
{
	FGeometryCacheVertexFactoryUserData& UserData = UserDataWrapper.Data;

	UserData.MeshExtension = FVector::OneVector;
	UserData.MeshOrigin = FVector::ZeroVector;

	const bool bHasMotionVectors = (
		TrackProxy->MeshData->VertexInfo.bHasMotionVectors &&
		TrackProxy->NextFrameMeshData->VertexInfo.bHasMotionVectors &&
		TrackProxy->MeshData->Positions.Num() == TrackProxy->MeshData->MotionVectors.Num())
		&& (TrackProxy->NextFrameMeshData->Positions.Num() == TrackProxy->NextFrameMeshData->MotionVectors.Num());

				if (!bHasMotionVectors)
	            {
		            const float PreviousPositionScale = (GFrameNumber <= UpdatedFrameNum) ? 1.f : 0.f;
					UserData.MotionBlurDataExtension = FVector::OneVector * PreviousPositionScale;
					UserData.MotionBlurDataOrigin = FVector::ZeroVector;
					UserData.MotionBlurPositionScale = 1.f - PreviousPositionScale;
	            }
	            else
	            {
		            UserData.MotionBlurDataExtension = FVector::OneVector * PlaybackSpeed;
		            UserData.MotionBlurDataOrigin = FVector::ZeroVector;
		            UserData.MotionBlurPositionScale = 1.0f;
	            }

	if (IsRayTracingEnabled())
	{
		// No vertex manipulation is allowed in the vertex shader
		// Otherwise we need an additional compute shader pass to execute the vertex shader and dump to a staging buffer
		check(UserData.MeshExtension == FVector::OneVector);
		check(UserData.MeshOrigin == FVector::ZeroVector);
	}

	UserData.PositionBuffer = &TrackProxy->PositionBuffers[TrackProxy->CurrentPositionBufferIndex % 2];
	UserData.MotionBlurDataBuffer = &TrackProxy->PositionBuffers[(TrackProxy->CurrentPositionBufferIndex+1) % 2];

	FGeometryCacheVertexFactoryUniformBufferParameters UniformBufferParameters;

	UniformBufferParameters.MeshOrigin = UserData.MeshOrigin;
	UniformBufferParameters.MeshExtension = UserData.MeshExtension;
	UniformBufferParameters.MotionBlurDataOrigin = UserData.MotionBlurDataOrigin;
	UniformBufferParameters.MotionBlurDataExtension = UserData.MotionBlurDataExtension;
	UniformBufferParameters.MotionBlurPositionScale = UserData.MotionBlurPositionScale;

	UserData.UniformBuffer = FGeometryCacheVertexFactoryUniformBufferParametersRef::CreateUniformBufferImmediate(UniformBufferParameters, UniformBuffer_SingleFrame);
	TrackProxy->VertexFactory.CreateManualVertexFetchUniformBuffer(UserData.PositionBuffer, UserData.MotionBlurDataBuffer, UserData);

	// Draw the mesh.
	FMeshBatchElement& BatchElement = Mesh.Elements[0];
	BatchElement.IndexBuffer = &TrackProxy->IndexBuffer;
	Mesh.VertexFactory = &TrackProxy->VertexFactory;
	Mesh.SegmentIndex = 0;

	const FMatrix& LocalToWorldTransform = TrackProxy->WorldMatrix * GetLocalToWorld();

	DynamicPrimitiveUniformBuffer.Set(LocalToWorldTransform, LocalToWorldTransform, GetBounds(), GetLocalBounds(), true, false, DrawsVelocity(), false);
	BatchElement.PrimitiveUniformBuffer = DynamicPrimitiveUniformBuffer.UniformBuffer.GetUniformBufferRHI();

	const FGeometryCacheMeshData* MeshData = GetSelectedMeshData(TrackProxy, Time, bLooping, bIsPlayingBackwards);

	BatchElement.FirstIndex = BatchInfo.StartIndex;
	BatchElement.NumPrimitives = BatchInfo.NumTriangles;
	BatchElement.MinVertexIndex = 0;
	BatchElement.MaxVertexIndex = MeshData->Positions.Num() - 1;
	BatchElement.VertexFactoryUserData = &UserDataWrapper.Data;
	Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
	Mesh.Type = PT_TriangleList;
	Mesh.DepthPriorityGroup = SDPG_World;
	Mesh.bCanApplyViewModeOverrides = false;
}

void FGeometryCacheSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	SCOPE_CYCLE_COUNTER(STAT_GeometryCacheSceneProxy_GetMeshElements);

	// Set up wire frame material (if needed)
	const bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;

	FColoredMaterialRenderProxy* WireframeMaterialInstance = nullptr;
	if (bWireframe)
	{
		const FEngineShowFlags& EngineShowFlags = ViewFamily.EngineShowFlags;
		const bool bLevelColorationEnabled = EngineShowFlags.LevelColoration;
		const bool bPropertyColorationEnabled = EngineShowFlags.PropertyColoration;

		FLinearColor ViewWireframeColor(bLevelColorationEnabled ? GetLevelColor() : GetWireframeColor());
		if (bPropertyColorationEnabled)
		{
			ViewWireframeColor = GetPropertyColor();
		}

		WireframeMaterialInstance = new FColoredMaterialRenderProxy(
			GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : nullptr,
			GetSelectionColor(ViewWireframeColor, !(GIsEditor && EngineShowFlags.Selection) || IsSelected(), IsHovered(), false)
		);

		Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);
	}
	
	const bool bVisible = [&Views, VisibilityMap]()
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				return true;
			}
		}
		return false;
	}();

	if (bVisible)
	{
		if (!IsRayTracingEnabled())
		{
			// When ray tracing is disabled, update only when visible
			// This is the old behavior
			FrameUpdate();
		}

		// Iterate over all batches in all tracks and add them to all the relevant views	
		for (const FGeomCacheTrackProxy* TrackProxy : Tracks)
		{
			const FVisibilitySample& VisibilitySample = TrackProxy->GetVisibilitySample(Time, bLooping);
			if (!VisibilitySample.bVisibilityState)
			{
				continue;
			}

			const FGeometryCacheMeshData* MeshData = GetSelectedMeshData(TrackProxy, Time, bLooping, bIsPlayingBackwards);
			const int32 NumBatches = MeshData->BatchesInfo.Num();

			for (int32 BatchIndex = 0; BatchIndex < NumBatches; ++BatchIndex)
			{
				const FGeometryCacheMeshBatchInfo& BatchInfo = MeshData->BatchesInfo[BatchIndex];

				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					if (VisibilityMap & (1 << ViewIndex))
					{
						FMeshBatch& MeshBatch = Collector.AllocateMesh();

						FGeometryCacheVertexFactoryUserDataWrapper& UserDataWrapper = Collector.AllocateOneFrameResource<FGeometryCacheVertexFactoryUserDataWrapper>();
						FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
						CreateMeshBatch(TrackProxy, BatchInfo, UserDataWrapper, DynamicPrimitiveUniformBuffer, MeshBatch);

						// Apply view mode material overrides
						FMaterialRenderProxy* MaterialProxy = bWireframe ? WireframeMaterialInstance : TrackProxy->Materials[BatchIndex]->GetRenderProxy();
						MeshBatch.bWireframe = bWireframe;
						MeshBatch.MaterialRenderProxy = MaterialProxy;

						Collector.AddMesh(ViewIndex, MeshBatch);

						INC_DWORD_STAT_BY(STAT_GeometryCacheSceneProxy_TriangleCount, MeshBatch.Elements[0].NumPrimitives);
						INC_DWORD_STAT_BY(STAT_GeometryCacheSceneProxy_MeshBatchCount, 1);

					#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
						// Render bounds
						RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
					#endif
					}
				}
			}
		}
	}
}

#if RHI_RAYTRACING
void FGeometryCacheSceneProxy::GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances)
{
	for (FGeomCacheTrackProxy* TrackProxy : Tracks)
	{
		const FVisibilitySample& VisibilitySample = TrackProxy->GetVisibilitySample(Time, bLooping);
		if (!VisibilitySample.bVisibilityState)
		{
			continue;
		}

		FRayTracingInstance RayTracingInstance;
		RayTracingInstance.Geometry = &TrackProxy->RayTracingGeometry;
		RayTracingInstance.InstanceTransforms.Add(GetLocalToWorld());

		const FGeometryCacheMeshData* MeshData = GetSelectedMeshData(TrackProxy, Time, bLooping, bIsPlayingBackwards);
		for (int32 SegmentIndex = 0; SegmentIndex < MeshData->BatchesInfo.Num(); ++SegmentIndex)
		{
			const FGeometryCacheMeshBatchInfo& BatchInfo = MeshData->BatchesInfo[SegmentIndex];
			FMeshBatch MeshBatch;

			FGeometryCacheVertexFactoryUserDataWrapper& UserDataWrapper = Context.RayTracingMeshResourceCollector.AllocateOneFrameResource<FGeometryCacheVertexFactoryUserDataWrapper>();
			FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Context.RayTracingMeshResourceCollector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
			CreateMeshBatch(TrackProxy, BatchInfo, UserDataWrapper, DynamicPrimitiveUniformBuffer, MeshBatch);

			MeshBatch.MaterialRenderProxy = TrackProxy->Materials[SegmentIndex]->GetRenderProxy();
			MeshBatch.CastRayTracedShadow = IsShadowCast(Context.ReferenceView);
			MeshBatch.SegmentIndex = SegmentIndex;

			RayTracingInstance.Materials.Add(MeshBatch);
		}

		RayTracingInstance.BuildInstanceMaskAndFlags();

		OutRayTracingInstances.Add(RayTracingInstance);
	}
}
#endif

FPrimitiveViewRelevance FGeometryCacheSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View);
	Result.bShadowRelevance = IsShadowCast(View);
	Result.bDynamicRelevance = true;
	Result.bRenderCustomDepth = ShouldRenderCustomDepth();
	Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
	MaterialRelevance.SetPrimitiveViewRelevance(Result);
	Result.bVelocityRelevance = IsMovable() && Result.bOpaque && Result.bRenderInMainPass;
	return Result;
}

bool FGeometryCacheSceneProxy::CanBeOccluded() const
{
	return !MaterialRelevance.bDisableDepthTest;
}

bool FGeometryCacheSceneProxy::IsUsingDistanceCullFade() const
{
	return MaterialRelevance.bUsesDistanceCullFade;
}

uint32 FGeometryCacheSceneProxy::GetMemoryFootprint(void) const
{
	return(sizeof(*this) + GetAllocatedSize());
}

uint32 FGeometryCacheSceneProxy::GetAllocatedSize(void) const
{
	return(FPrimitiveSceneProxy::GetAllocatedSize());
}

void FGeometryCacheSceneProxy::UpdateAnimation(float NewTime, bool bNewLooping, bool bNewIsPlayingBackwards, float NewPlaybackSpeed)
{
	Time = NewTime;
	bLooping = bNewLooping;
	bIsPlayingBackwards = bNewIsPlayingBackwards;
	PlaybackSpeed = NewPlaybackSpeed;
	UpdatedFrameNum = GFrameNumber + 1;

	if (IsRayTracingEnabled())
	{
		// When ray tracing is enabled, update regardless of visibility
		FrameUpdate();

	#if RHI_RAYTRACING
		for (FGeomCacheTrackProxy* Section : Tracks)
		{
			if (Section != nullptr)
			{
				const int PositionBufferIndex = Section->CurrentPositionBufferIndex != -1 ? Section->CurrentPositionBufferIndex % 2 : 0;

				Section->RayTracingGeometry.Initializer.IndexBuffer = Section->IndexBuffer.IndexBufferRHI;
				Section->RayTracingGeometry.Initializer.TotalPrimitiveCount = 0;
				
				TMemoryImageArray<FRayTracingGeometrySegment>& Segments = Section->RayTracingGeometry.Initializer.Segments;
				Segments.Reset();

				const FGeometryCacheMeshData* MeshData = GetSelectedMeshData(Section, Time, bLooping, bIsPlayingBackwards);
				for (const FGeometryCacheMeshBatchInfo& BatchInfo : MeshData->BatchesInfo)
				{
					FRayTracingGeometrySegment Segment;
					Segment.FirstPrimitive = BatchInfo.StartIndex / 3;
					Segment.NumPrimitives = BatchInfo.NumTriangles;
					Segment.VertexBuffer = Section->PositionBuffers[PositionBufferIndex].VertexBufferRHI;

					Segments.Add(Segment);
					Section->RayTracingGeometry.Initializer.TotalPrimitiveCount += BatchInfo.NumTriangles;
				}
							
				Section->RayTracingGeometry.UpdateRHI();
			}
		}
	#endif
	}
}

void FGeometryCacheSceneProxy::FrameUpdate() const
{
	for (FGeomCacheTrackProxy* TrackProxy : Tracks)
	{
		// Render out stored TrackProxy's
		if (TrackProxy != nullptr)
		{
			const FVisibilitySample& VisibilitySample = TrackProxy->GetVisibilitySample(Time, bLooping);
			if (!VisibilitySample.bVisibilityState)
			{
				continue;
			}

			// Figure out which frame(s) we need to decode
			int32 FrameIndex;
			int32 NextFrameIndex;
			float InterpolationFactor;
			TrackProxy->FindSampleIndexesFromTime(Time, bLooping, bIsPlayingBackwards, FrameIndex, NextFrameIndex, InterpolationFactor);
			bool bDecodedAnything = false; // Did anything new get decoded this frame
			bool bSeeked = false; // Is this frame a seek and thus the previous rendered frame's data invalid
			bool bDecoderError = false; // If we have a decoder error we don't interpolate and we don't update the vertex buffers
										// so essentially we just keep the last valid frame...
			
			bool bFrameIndicesChanged = false;
			bool bDifferentInterpolationFactor = FMath::RoundToInt(InterpolationFactor) != FMath::RoundToInt(TrackProxy->InterpolationFactor);
			TrackProxy->InterpolationFactor = InterpolationFactor;

			// Compare this against the frames we got and keep some/all/none of them
			// This will work across frames but also within a frame if the mesh is in several views
			if (TrackProxy->FrameIndex != FrameIndex || TrackProxy->NextFrameIndex != NextFrameIndex)
			{
				// Normal case the next frame is the new current frame
				if (TrackProxy->NextFrameIndex == FrameIndex)
				{
					// Cycle the current and next frame double buffer
					FGeometryCacheMeshData *OldFrameMesh = TrackProxy->MeshData;
					TrackProxy->MeshData = TrackProxy->NextFrameMeshData;
					TrackProxy->NextFrameMeshData = OldFrameMesh;

					int32 OldFrameIndex = TrackProxy->FrameIndex;
					TrackProxy->FrameIndex = TrackProxy->NextFrameIndex;
					TrackProxy->NextFrameIndex = OldFrameIndex;

					// Decode the new next frame
					if (TrackProxy->GetMeshData(NextFrameIndex, *TrackProxy->NextFrameMeshData))
					{
						bDecodedAnything = true;
						// Only register this if we actually successfully decoded
						TrackProxy->NextFrameIndex = NextFrameIndex;
					}
					else
					{
						// Mark the frame as corrupted
						TrackProxy->NextFrameIndex = -1;
						bDecoderError = true;
					}
				}
				// Probably a seek or the mesh hasn't been visible in a while decode two frames
				else
				{
					if (TrackProxy->GetMeshData(FrameIndex, *TrackProxy->MeshData))
					{
						TrackProxy->NextFrameMeshData->Indices = TrackProxy->MeshData->Indices;
						if (TrackProxy->GetMeshData(NextFrameIndex, *TrackProxy->NextFrameMeshData))
						{
							TrackProxy->FrameIndex = FrameIndex;
							TrackProxy->NextFrameIndex = NextFrameIndex;
							bSeeked = true;
							bDecodedAnything = true;
						}
						else
						{
							// The first frame decoded fine but the second didn't 
							// we need to specially handle this
							TrackProxy->NextFrameIndex = -1;
							bDecoderError = true;
						}
					}
					else
					{
						TrackProxy->FrameIndex = -1;
						bDecoderError = true;
					}
				}

				bFrameIndicesChanged = true;
			}

			// Check if we can interpolate between the two frames we have available
			const bool bCanInterpolate = TrackProxy->IsTopologyCompatible(TrackProxy->FrameIndex, TrackProxy->NextFrameIndex);

			// Check if we have explicit motion vectors
			const bool bHasMotionVectors = (
				TrackProxy->MeshData->VertexInfo.bHasMotionVectors &&
				TrackProxy->NextFrameMeshData->VertexInfo.bHasMotionVectors &&
				TrackProxy->MeshData->Positions.Num() == TrackProxy->MeshData->MotionVectors.Num())
				&& (TrackProxy->NextFrameMeshData->Positions.Num() == TrackProxy->NextFrameMeshData->MotionVectors.Num());

			// Can we interpolate the vertex data?
			if (bCanInterpolate && !bDecoderError && CVarInterpolateFrames.GetValueOnRenderThread() != 0)
			{
				SCOPE_CYCLE_COUNTER(STAT_InterpolateFrames);
				// Interpolate if the time has changed.
				// note: This is a bit precarious as this code is called multiple times per frame. This ensures
				// we only interpolate once (which is a nice optimization) but more importantly that we only
				// bump the CurrentPositionBufferIndex once per frame. This ensures that last frame's position
				// buffer is not overwritten.
				// If motion blur suddenly seems to stop working while it should be working it may be that the
				// CurrentPositionBufferIndex gets inadvertently bumped twice per frame essentially using the same
				// data for current and previous during rendering.
				if (TrackProxy->PositionBufferFrameTimes[TrackProxy->CurrentPositionBufferIndex % 2] != Time)
				{
					TArray<FVector> InterpolatedPositions;
					TArray<FPackedNormal> InterpolatedTangentX;
					TArray<FPackedNormal> InterpolatedTangentZ;
					TArray<FVector2D> InterpolatedUVs;
					TArray<FColor> InterpolatedColors;

					const int32 NumVerts = TrackProxy->MeshData->Positions.Num();

					InterpolatedPositions.AddUninitialized(NumVerts);
					InterpolatedTangentX.AddUninitialized(NumVerts);
					InterpolatedTangentZ.AddUninitialized(NumVerts);
					InterpolatedUVs.AddUninitialized(NumVerts);
					InterpolatedColors.AddUninitialized(NumVerts);

					TArray<FVector> InterpolatedMotionVectors;
					if (bHasMotionVectors)
					{
						InterpolatedMotionVectors.AddUninitialized(NumVerts);
					}


					const float OneMinusInterp = 1.0 - InterpolationFactor;
					const int32 InterpFixed = (int32)(InterpolationFactor * 255.0f);
					const int32 OneMinusInterpFixed = 255 - InterpFixed;

					{
						check(TrackProxy->MeshData->Positions.Num() >= NumVerts);
						check(TrackProxy->NextFrameMeshData->Positions.Num() >= NumVerts);
						check(InterpolatedPositions.Num() >= NumVerts);
						const FVector* PositionAPtr = TrackProxy->MeshData->Positions.GetData();
						const FVector* PositionBPtr = TrackProxy->NextFrameMeshData->Positions.GetData();
						FVector* InterpolationPositionsPtr = InterpolatedPositions.GetData();
						for (int32 Index = 0; Index < NumVerts; ++Index)
						{
							const FVector& PositionA = PositionAPtr[Index];
							const FVector& PositionB = PositionBPtr[Index];
							InterpolationPositionsPtr[Index] = PositionA * OneMinusInterp + PositionB * InterpolationFactor;
						}
					}
					
					{
						check(TrackProxy->MeshData->TangentsX.Num() >= NumVerts);
						check(TrackProxy->NextFrameMeshData->TangentsX.Num() >= NumVerts);
						check(TrackProxy->MeshData->TangentsZ.Num() >= NumVerts);
						check(TrackProxy->NextFrameMeshData->TangentsZ.Num() >= NumVerts);
						check(InterpolatedTangentX.Num() >= NumVerts);
						check(InterpolatedTangentZ.Num() >= NumVerts);
						const FPackedNormal* TangentXAPtr = TrackProxy->MeshData->TangentsX.GetData();
						const FPackedNormal* TangentXBPtr = TrackProxy->NextFrameMeshData->TangentsX.GetData();
						const FPackedNormal* TangentZAPtr = TrackProxy->MeshData->TangentsZ.GetData();
						const FPackedNormal* TangentZBPtr = TrackProxy->NextFrameMeshData->TangentsZ.GetData();
						FPackedNormal* InterpolatedTangentXPtr = InterpolatedTangentX.GetData();
						FPackedNormal* InterpolatedTangentZPtr = InterpolatedTangentZ.GetData();
						for (int32 Index = 0; Index < NumVerts; ++Index)
						{
							// The following are already 8 bit so quantized enough we can do exact equal comparisons
							const FPackedNormal& TangentXA = TangentXAPtr[Index];
							const FPackedNormal& TangentXB = TangentXBPtr[Index];
							const FPackedNormal& TangentZA = TangentZAPtr[Index];
							const FPackedNormal& TangentZB = TangentZBPtr[Index];

							InterpolatedTangentXPtr[Index] = InterpolatePackedNormal(TangentXA, TangentXB, InterpFixed, OneMinusInterpFixed);
							InterpolatedTangentZPtr[Index] = InterpolatePackedNormal(TangentZA, TangentZB, InterpFixed, OneMinusInterpFixed);
						}
					}
					

					if (TrackProxy->MeshData->VertexInfo.bHasColor0)
					{
						check(TrackProxy->MeshData->Colors.Num() >= NumVerts);
						check(TrackProxy->NextFrameMeshData->Colors.Num() >= NumVerts);
						check(InterpolatedColors.Num() >= NumVerts);
						const FColor* ColorAPtr = TrackProxy->MeshData->Colors.GetData();
						const FColor* ColorBPtr = TrackProxy->NextFrameMeshData->Colors.GetData();
						FColor* InterpolatedColorsPtr = InterpolatedColors.GetData();
						for (int32 Index = 0; Index < NumVerts; ++Index)
						{
							const FColor& ColorA = ColorAPtr[Index];
							const FColor& ColorB = ColorBPtr[Index];
							InterpolatedColorsPtr[Index] = InterpolatePackedColor(ColorA, ColorB, InterpFixed, OneMinusInterpFixed);
						}
					}

					if (TrackProxy->MeshData->VertexInfo.bHasUV0)
					{
						check(TrackProxy->MeshData->TextureCoordinates.Num() >= NumVerts);
						check(TrackProxy->NextFrameMeshData->TextureCoordinates.Num() >= NumVerts);
						check(InterpolatedUVs.Num() >= NumVerts);
						const FVector2D* UVAPtr = TrackProxy->MeshData->TextureCoordinates.GetData();
						const FVector2D* UVBPtr = TrackProxy->NextFrameMeshData->TextureCoordinates.GetData();
						FVector2D* InterpolatedUVsPtr = InterpolatedUVs.GetData();
						for (int32 Index = 0; Index < NumVerts; ++Index)
						{
							const FVector2D& UVA = UVAPtr[Index];
							const FVector2D& UVB = UVBPtr[Index];
							InterpolatedUVsPtr[Index] = UVA * OneMinusInterp + UVB * InterpolationFactor;
						}
					}

					if (bHasMotionVectors)
					{
						check(TrackProxy->MeshData->MotionVectors.Num() >= NumVerts);
						check(TrackProxy->NextFrameMeshData->MotionVectors.Num() >= NumVerts);
						check(InterpolatedMotionVectors.Num() >= NumVerts);
						const FVector* MotionVectorAPtr = TrackProxy->MeshData->MotionVectors.GetData();
						const FVector* MotionVectorBPtr = TrackProxy->NextFrameMeshData->MotionVectors.GetData();
						FVector* InterpolatedMotionVectorsPtr = InterpolatedMotionVectors.GetData();

						for (int32 Index = 0; Index < NumVerts; ++Index)
						{
							InterpolatedMotionVectorsPtr[Index] = MotionVectorAPtr[Index] * OneMinusInterp + MotionVectorBPtr[Index] * InterpolationFactor;
						}
					}

					// Upload other non-motionblurred data
					if (!TrackProxy->MeshData->VertexInfo.bConstantIndices)
						TrackProxy->IndexBuffer.Update(TrackProxy->MeshData->Indices);

					if (TrackProxy->MeshData->VertexInfo.bHasTangentX)
						TrackProxy->TangentXBuffer.Update(InterpolatedTangentX);
					if (TrackProxy->MeshData->VertexInfo.bHasTangentZ)
						TrackProxy->TangentZBuffer.Update(InterpolatedTangentZ);

					if (TrackProxy->MeshData->VertexInfo.bHasUV0)
						TrackProxy->TextureCoordinatesBuffer.Update(InterpolatedUVs);

					if (TrackProxy->MeshData->VertexInfo.bHasColor0)
						TrackProxy->ColorBuffer.Update(InterpolatedColors);

					bool bIsCompatibleWithCachedFrame = TrackProxy->IsTopologyCompatible(
						TrackProxy->PositionBufferFrameIndices[TrackProxy->CurrentPositionBufferIndex % 2],
						TrackProxy->FrameIndex);

					if (!bHasMotionVectors)
					{
						// Initialize both buffers the first frame
						if (TrackProxy->CurrentPositionBufferIndex == -1 || !bIsCompatibleWithCachedFrame)
						{
							TrackProxy->PositionBuffers[0].Update(InterpolatedPositions);
							TrackProxy->PositionBuffers[1].Update(InterpolatedPositions);
							TrackProxy->CurrentPositionBufferIndex = 0;
							TrackProxy->PositionBufferFrameTimes[0] = Time;
							TrackProxy->PositionBufferFrameTimes[1] = Time;
							// We need to keep a frame index in order to ensure topology consistency. As we can interpolate 
							// FrameIndex and NextFrameIndex are certainly topo-compatible so it doesn't really matter which 
							// one we keep here. But wee keep NextFrameIndex as that is most useful to validate against
							// the frame coming up
							TrackProxy->PositionBufferFrameIndices[0] = TrackProxy->NextFrameIndex;
							TrackProxy->PositionBufferFrameIndices[1] = TrackProxy->NextFrameIndex;
						}
						else
						{
							TrackProxy->CurrentPositionBufferIndex++;
							TrackProxy->PositionBuffers[TrackProxy->CurrentPositionBufferIndex % 2].Update(InterpolatedPositions);
							TrackProxy->PositionBufferFrameTimes[TrackProxy->CurrentPositionBufferIndex % 2] = Time;
							TrackProxy->PositionBufferFrameIndices[TrackProxy->CurrentPositionBufferIndex % 2] = TrackProxy->NextFrameIndex;
						}
					}
					else
					{
						TrackProxy->CurrentPositionBufferIndex = 0;
						TrackProxy->PositionBuffers[0].Update(InterpolatedPositions);
						TrackProxy->PositionBuffers[1].Update(InterpolatedMotionVectors);
						TrackProxy->PositionBufferFrameIndices[0] = TrackProxy->FrameIndex;
						TrackProxy->PositionBufferFrameIndices[1] = -1;
						TrackProxy->PositionBufferFrameTimes[0] = Time;
						TrackProxy->PositionBufferFrameTimes[1] = Time;
					}
				}


			}
			else
			{
				// We just don't interpolate between frames if we got GPU to burn we could someday render twice and stipple fade between it :-D like with lods

				// Only bother uploading if anything changed or when the we failed to decode anything make sure update the gpu buffers regardless
				if (bFrameIndicesChanged || bDifferentInterpolationFactor || bDecodedAnything || bDecoderError)
				{
					const bool bNextFrame = !!FMath::RoundToInt(InterpolationFactor) && TrackProxy->NextFrameMeshData->Positions.Num() > 0; // use next frame only if it's valid
					const uint32 FrameIndexToUse = bNextFrame ? TrackProxy->NextFrameIndex : TrackProxy->FrameIndex;
					const FGeometryCacheMeshData* MeshDataToUse = bNextFrame ? TrackProxy->NextFrameMeshData : TrackProxy->MeshData;

					const int32 NumVertices = MeshDataToUse->Positions.Num();

					if (MeshDataToUse->VertexInfo.bHasTangentX)
						TrackProxy->TangentXBuffer.Update(MeshDataToUse->TangentsX);
					if (MeshDataToUse->VertexInfo.bHasTangentZ)
						TrackProxy->TangentZBuffer.Update(MeshDataToUse->TangentsZ);

					if (!MeshDataToUse->VertexInfo.bConstantIndices)
						TrackProxy->IndexBuffer.Update(MeshDataToUse->Indices);

					if (MeshDataToUse->VertexInfo.bHasUV0)
						TrackProxy->TextureCoordinatesBuffer.Update(MeshDataToUse->TextureCoordinates);

					if (MeshDataToUse->VertexInfo.bHasColor0)
						TrackProxy->ColorBuffer.Update(MeshDataToUse->Colors);

					const bool bIsCompatibleWithCachedFrame = TrackProxy->IsTopologyCompatible(
							TrackProxy->PositionBufferFrameIndices[TrackProxy->CurrentPositionBufferIndex % 2],
						FrameIndexToUse);

					if (!bHasMotionVectors)
					{
						// Initialize both buffers the first frame or when topology changed as we can't render
						// with a previous buffer referencing a buffer from another topology
						if (TrackProxy->CurrentPositionBufferIndex == -1 || !bIsCompatibleWithCachedFrame || bSeeked)
						{
							TrackProxy->PositionBuffers[0].Update(MeshDataToUse->Positions);
							TrackProxy->PositionBuffers[1].Update(MeshDataToUse->Positions);
							TrackProxy->CurrentPositionBufferIndex = 0;
							TrackProxy->PositionBufferFrameIndices[0] = FrameIndexToUse;
							TrackProxy->PositionBufferFrameIndices[1] = FrameIndexToUse;
						}
						// We still use the previous frame's buffer as a motion blur previous position. As interpolation is switched
						// off the actual time of this previous frame depends on the geometry cache framerate and playback speed
						// so the motion blur vectors may not really be anything relevant. Do we want to just disable motion blur? 
						// But as an optimization skipping interpolation when the cache fps is near to the actual game fps this is obviously nice...
						else
						{
							TrackProxy->CurrentPositionBufferIndex++;
							TrackProxy->PositionBuffers[TrackProxy->CurrentPositionBufferIndex % 2].Update(MeshDataToUse->Positions);
							TrackProxy->PositionBufferFrameIndices[TrackProxy->CurrentPositionBufferIndex % 2] = FrameIndexToUse;
						}
					}
					else
					{
						TrackProxy->CurrentPositionBufferIndex = 0;
						TrackProxy->PositionBuffers[0].Update(MeshDataToUse->Positions);
						TrackProxy->PositionBuffers[1].Update(MeshDataToUse->MotionVectors);
						TrackProxy->PositionBufferFrameIndices[0] = FrameIndexToUse;
						TrackProxy->PositionBufferFrameIndices[1] = -1;
						TrackProxy->PositionBufferFrameTimes[0] = Time;
						TrackProxy->PositionBufferFrameTimes[1] = Time;
					}
				}
			}

		#if 0
			bool bOffloadUpdate = CVarOffloadUpdate.GetValueOnRenderThread() != 0;
			if (TrackProxy->SampleIndex != TrackProxy->UploadedSampleIndex)
			{
				TrackProxy->UploadedSampleIndex = TrackProxy->SampleIndex;

				if (bOffloadUpdate)
				{
					check(false);
					// Only update the size on this thread
					TrackProxy->IndexBuffer.UpdateSizeOnly(TrackProxy->MeshData->Indices.Num());
					TrackProxy->VertexBuffer.UpdateSizeTyped<FNoPositionVertex>(TrackProxy->MeshData->Vertices.Num());

					// Do the interpolation on a worker thread
					FGraphEventRef CompletionFence = FFunctionGraphTask::CreateAndDispatchWhenReady([]()
					{

					}, GET_STATID(STAT_BufferUpdateTask), NULL, ENamedThreads::AnyThread);

					// Queue a command on the RHI thread that waits for the interpolation job and then uploads them to the GPU
					FRHICommandListImmediate &RHICommandList = GetImmediateCommandList_ForRenderCommand();
					new (RHICommandList.AllocCommand<FRHICommandUpdateGeometryCacheBuffer>())FRHICommandUpdateGeometryCacheBuffer(
						CompletionFence,
						TrackProxy->VertexBuffer.VertexBufferRHI,
						TrackProxy->MeshData->Vertices.GetData(),
						TrackProxy->VertexBuffer.GetSizeInBytes(),
						TrackProxy->IndexBuffer.IndexBufferRHI,
						TrackProxy->MeshData->Indices.GetData(),
						TrackProxy->IndexBuffer.SizeInBytes());

					// Upload vertex data
					/*void* RESTRICT Data = (void* RESTRICT)GDynamicRHI->RHILockVertexBuffer(TrackProxy->VertexBuffer.VertexBufferRHI, 0, TrackProxy->VertexBuffer.SizeInBytes(), RLM_WriteOnly);
					FMemory::BigBlockMemcpy(Data, TrackProxy->MeshData->Vertices.GetData(), TrackProxy->VertexBuffer.SizeInBytes());
					GDynamicRHI->RHIUnlockVertexBuffer(TrackProxy->VertexBuffer.VertexBufferRHI);

					// Upload index data
					Data = (void* RESTRICT)GDynamicRHI->RHILockIndexBuffer(TrackProxy->IndexBuffer.IndexBufferRHI, 0, TrackProxy->IndexBuffer.SizeInBytes(), RLM_WriteOnly);
					FMemory::BigBlockMemcpy(Data, TrackProxy->MeshData->Indices.GetData(), TrackProxy->IndexBuffer.SizeInBytes());
					GDynamicRHI->RHIUnlockIndexBuffer(TrackProxy->IndexBuffer.IndexBufferRHI);*/
				}
				else
				{
					TrackProxy->IndexBuffer.Update(TrackProxy->MeshData->Indices);
					TrackProxy->VertexBuffer.Update(TrackProxy->MeshData->Vertices);

					// Initialize both buffers the first frame
					if (TrackProxy->CurrentPositionBufferIndex == -1)
					{
						TrackProxy->PositonBuffers[0].Update(TrackProxy->MeshData->Vertices);
						TrackProxy->PositonBuffers[1].Update(TrackProxy->MeshData->Vertices);
						TrackProxy->CurrentPositionBufferIndex = 0;
					}
					else
					{
						TrackProxy->CurrentPositionBufferIndex++;
						TrackProxy->PositonBuffers[TrackProxy->CurrentPositionBufferIndex%2].Update(TrackProxy->MeshData->Vertices);
					}
				}
			}
		#endif

		}
	}
}

void FGeometryCacheSceneProxy::UpdateSectionWorldMatrix(const int32 SectionIndex, const FMatrix& WorldMatrix)
{
	check(SectionIndex < Tracks.Num() && "Section Index out of range");
	Tracks[SectionIndex]->WorldMatrix = WorldMatrix;
}

void FGeometryCacheSceneProxy::ClearSections()
{
	Tracks.Empty();
}

bool FGeomCacheTrackProxy::UpdateMeshData(float Time, bool bLooping, int32& InOutMeshSampleIndex, FGeometryCacheMeshData& OutMeshData)
{
	if (UGeometryCacheTrackStreamable* StreamableTrack = Cast<UGeometryCacheTrackStreamable>(Track))
	{
		return StreamableTrack->GetRenderResource()->UpdateMeshData(Time, bLooping, InOutMeshSampleIndex, OutMeshData);
	}
	return false;
}

bool FGeomCacheTrackProxy::GetMeshData(int32 SampleIndex, FGeometryCacheMeshData& OutMeshData)
{
	if (UGeometryCacheTrackStreamable* StreamableTrack = Cast<UGeometryCacheTrackStreamable>(Track))
	{
		return StreamableTrack->GetRenderResource()->DecodeMeshData(SampleIndex, OutMeshData);
	}
	return false;
}

bool FGeomCacheTrackProxy::IsTopologyCompatible(int32 SampleIndexA, int32 SampleIndexB)
{
	if (UGeometryCacheTrackStreamable* StreamableTrack = Cast<UGeometryCacheTrackStreamable>(Track))
	{
		return StreamableTrack->GetRenderResource()->IsTopologyCompatible(SampleIndexA, SampleIndexB);
	}
	return false;
}

const FVisibilitySample& FGeomCacheTrackProxy::GetVisibilitySample(float Time, const bool bLooping) const
{
	if (UGeometryCacheTrackStreamable* StreamableTrack = Cast<UGeometryCacheTrackStreamable>(Track))
	{
		return StreamableTrack->GetVisibilitySample(Time, bLooping);
	}
	return FVisibilitySample::InvisibleSample;
}

void FGeomCacheTrackProxy::FindSampleIndexesFromTime(float Time, bool bLooping, bool bIsPlayingBackwards, int32 &OutFrameIndex, int32 &OutNextFrameIndex, float& InInterpolationFactor)
{
	if (UGeometryCacheTrackStreamable* StreamableTrack = Cast<UGeometryCacheTrackStreamable>(Track))
	{
		StreamableTrack->FindSampleIndexesFromTime(Time, bLooping, bIsPlayingBackwards, OutFrameIndex, OutNextFrameIndex, InInterpolationFactor);
	}
}

FGeomCacheVertexFactory::FGeomCacheVertexFactory(ERHIFeatureLevel::Type InFeatureLevel)
	: FGeometryCacheVertexVertexFactory(InFeatureLevel)
{

}

void FGeomCacheVertexFactory::Init_RenderThread(const FVertexBuffer* PositionBuffer, const FVertexBuffer* MotionBlurDataBuffer, const FVertexBuffer* TangentXBuffer, const FVertexBuffer* TangentZBuffer, const FVertexBuffer* TextureCoordinateBuffer, const FVertexBuffer* ColorBuffer)
{
	check(IsInRenderingThread());

	// Initialize the vertex factory's stream components.
	FDataType NewData;
	NewData.PositionComponent = FVertexStreamComponent(PositionBuffer, 0, sizeof(FVector), VET_Float3);

	NewData.TextureCoordinates.Add(FVertexStreamComponent(TextureCoordinateBuffer, 0, sizeof(FVector2D), VET_Float2));
	NewData.TangentBasisComponents[0] = FVertexStreamComponent(TangentXBuffer, 0, sizeof(FPackedNormal), VET_PackedNormal);
	NewData.TangentBasisComponents[1] = FVertexStreamComponent(TangentZBuffer, 0, sizeof(FPackedNormal), VET_PackedNormal);
	NewData.ColorComponent = FVertexStreamComponent(ColorBuffer, 0, sizeof(FColor), VET_Color);
	NewData.MotionBlurDataComponent = FVertexStreamComponent(MotionBlurDataBuffer, 0, sizeof(FVector), VET_Float3);

	SetData(NewData);
}

void FGeomCacheVertexFactory::Init(const FVertexBuffer* PositionBuffer, const FVertexBuffer* MotionBlurDataBuffer, const FVertexBuffer* TangentXBuffer, const FVertexBuffer* TangentZBuffer, const FVertexBuffer* TextureCoordinateBuffer, const FVertexBuffer* ColorBuffer)
{
	if (IsInRenderingThread())
	{
		Init_RenderThread(PositionBuffer, MotionBlurDataBuffer, TangentXBuffer, TangentZBuffer, TextureCoordinateBuffer, ColorBuffer);
	}
	else
	{

		ENQUEUE_RENDER_COMMAND(InitGeomCacheVertexFactory)(
			[this, PositionBuffer, MotionBlurDataBuffer, TangentXBuffer, TangentZBuffer, TextureCoordinateBuffer, ColorBuffer](FRHICommandListImmediate& RHICmdList)
			{
			Init_RenderThread(PositionBuffer, MotionBlurDataBuffer, TangentXBuffer, TangentZBuffer, TextureCoordinateBuffer, ColorBuffer);
		});
	}
}

void FGeomCacheIndexBuffer::InitRHI()
{
	FRHIResourceCreateInfo CreateInfo;
	void* Buffer = nullptr;
	IndexBufferRHI = RHICreateAndLockIndexBuffer(sizeof(uint32), NumIndices * sizeof(uint32), BUF_Dynamic | BUF_ShaderResource, CreateInfo, Buffer);
	RHIUnlockIndexBuffer(IndexBufferRHI);
}

void FGeomCacheIndexBuffer::Update(const TArray<uint32> &Indices)
{
	SCOPE_CYCLE_COUNTER(STAT_IndexBufferUpdate);

	check(IsInRenderingThread());

	void* Buffer = nullptr;

	// We only ever grow in size. Ok for now?
	if (Indices.Num() > NumIndices)
	{
		NumIndices = Indices.Num();
		FRHIResourceCreateInfo CreateInfo;
		IndexBufferRHI = RHICreateAndLockIndexBuffer(sizeof(uint32), NumIndices * sizeof(uint32), BUF_Dynamic | BUF_ShaderResource, CreateInfo, Buffer);
	}
	else
	{
		// Copy the index data into the index buffer.
		Buffer = RHILockIndexBuffer(IndexBufferRHI, 0, Indices.Num() * sizeof(uint32), RLM_WriteOnly);
	}

	FMemory::Memcpy(Buffer, Indices.GetData(), Indices.Num() * sizeof(uint32));
	RHIUnlockIndexBuffer(IndexBufferRHI);
}

void FGeomCacheIndexBuffer::UpdateSizeOnly(int32 NewNumIndices)
{
	check(IsInRenderingThread());

	// We only ever grow in size. Ok for now?
	if (NewNumIndices > NumIndices)
	{
		FRHIResourceCreateInfo CreateInfo;
		IndexBufferRHI = RHICreateIndexBuffer(sizeof(uint32), NewNumIndices * sizeof(uint32), BUF_Dynamic | BUF_ShaderResource, CreateInfo);
		NumIndices = NewNumIndices;
	}
}

void FGeomCacheVertexBuffer::InitRHI()
{
	FRHIResourceCreateInfo CreateInfo;
	void* BufferData = nullptr;
	VertexBufferRHI = RHICreateAndLockVertexBuffer(SizeInBytes, BUF_Static | BUF_ShaderResource, CreateInfo, BufferData);
	RHIUnlockVertexBuffer(VertexBufferRHI);
}

void FGeomCacheVertexBuffer::UpdateRaw(const void *Data, int32 NumItems, int32 ItemSizeBytes, int32 ItemStrideBytes)
{
	SCOPE_CYCLE_COUNTER(STAT_VertexBufferUpdate);
	int32 NewSizeInBytes = ItemSizeBytes * NumItems;
	bool bCanMemcopy = ItemSizeBytes == ItemStrideBytes;

	void* VertexBufferData = nullptr;

	if (NewSizeInBytes > SizeInBytes)
	{
		SizeInBytes = NewSizeInBytes;
		FRHIResourceCreateInfo CreateInfo;
		VertexBufferRHI = RHICreateAndLockVertexBuffer(SizeInBytes, BUF_Static | BUF_ShaderResource, CreateInfo, VertexBufferData);
	}
	else
	{
		VertexBufferData = RHILockVertexBuffer(VertexBufferRHI, 0, SizeInBytes, RLM_WriteOnly);
	}

	if (bCanMemcopy)
	{
		FMemory::Memcpy(VertexBufferData, Data, NewSizeInBytes);
	}
	else
	{
		int8 *InBytes = (int8 *)Data;
		int8 *OutBytes = (int8 *)VertexBufferData;
		for (int32 ItemId=0; ItemId < NumItems; ItemId++)
		{
			FMemory::Memcpy(OutBytes, InBytes, ItemSizeBytes);
			InBytes += ItemStrideBytes;
			OutBytes += ItemSizeBytes;
		}
	}

	RHIUnlockVertexBuffer(VertexBufferRHI);
}

void FGeomCacheVertexBuffer::UpdateSize(int32 NewSizeInBytes)
{
	if (NewSizeInBytes > SizeInBytes)
	{
		SizeInBytes = NewSizeInBytes;
		FRHIResourceCreateInfo CreateInfo;
		VertexBufferRHI = RHICreateVertexBuffer(SizeInBytes, BUF_Static | BUF_ShaderResource, CreateInfo);
	}
}
