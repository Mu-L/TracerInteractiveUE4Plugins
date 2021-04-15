// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraRendererMeshes.h"
#include "ParticleResources.h"
#include "NiagaraMeshVertexFactory.h"
#include "NiagaraDataSet.h"
#include "NiagaraStats.h"
#include "Async/ParallelFor.h"
#include "Engine/StaticMesh.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraSortingGPU.h"
#include "NiagaraGPURayTracingTransformsShader.h"
#include "RayTracingDefinitions.h"
#include "RayTracingDynamicGeometryCollection.h"
#include "RayTracingInstance.h"
#include "Renderer/Private/ScenePrivate.h"

#ifdef HMD_MODULE_INCLUDED
	#include "IXRTrackingSystem.h"
#endif

DECLARE_CYCLE_STAT(TEXT("Generate Mesh Vertex Data [GT]"), STAT_NiagaraGenMeshVertexData, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Render Meshes [RT]"), STAT_NiagaraRenderMeshes, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Render Meshes - Allocate GPU Data [RT]"), STAT_NiagaraRenderMeshes_AllocateGPUData, STATGROUP_Niagara);


DECLARE_DWORD_COUNTER_STAT(TEXT("NumMeshesRenderer"), STAT_NiagaraNumMeshes, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("NumMesheVerts"), STAT_NiagaraNumMeshVerts, STATGROUP_Niagara);

static int32 GbEnableNiagaraMeshRendering = 1;
static FAutoConsoleVariableRef CVarEnableNiagaraMeshRendering(
	TEXT("fx.EnableNiagaraMeshRendering"),
	GbEnableNiagaraMeshRendering,
	TEXT("If == 0, Niagara Mesh Renderers are disabled. \n"),
	ECVF_Default
);

#if RHI_RAYTRACING
static TAutoConsoleVariable<int32> CVarRayTracingNiagaraMeshes(
	TEXT("r.RayTracing.Geometry.NiagaraMeshes"),
	1,
	TEXT("Include Niagara meshes in ray tracing effects (default = 1 (Niagara meshes enabled in ray tracing))"));
#endif

extern int32 GbEnableMinimalGPUBuffers;

struct FNiagaraDynamicDataMesh : public FNiagaraDynamicDataBase
{
	FNiagaraDynamicDataMesh(const FNiagaraEmitterInstance* InEmitter)
		: FNiagaraDynamicDataBase(InEmitter)
	{
	}

	TArray<FMaterialRenderProxy*, TInlineAllocator<8>> Materials;
};

//////////////////////////////////////////////////////////////////////////

class FNiagaraMeshCollectorResourcesMesh : public FOneFrameResource
{
public:
	FNiagaraMeshVertexFactory VertexFactory;
	FNiagaraMeshUniformBufferRef UniformBuffer;

	virtual ~FNiagaraMeshCollectorResourcesMesh()
	{
		VertexFactory.ReleaseResource();
	}
};

//////////////////////////////////////////////////////////////////////////

FNiagaraRendererMeshes::FNiagaraRendererMeshes(ERHIFeatureLevel::Type FeatureLevel, const UNiagaraRendererProperties* Props, const FNiagaraEmitterInstance* Emitter)
	: FNiagaraRenderer(FeatureLevel, Props, Emitter)
	, MaterialParamValidMask(0)
{
	check(Emitter);
	check(Props);

	const UNiagaraMeshRendererProperties* Properties = CastChecked<const UNiagaraMeshRendererProperties>(Props);
	UStaticMesh* Mesh = Properties->ParticleMesh;
	check(Mesh);

	MeshRenderData = Mesh->RenderData.Get();
	FacingMode = Properties->FacingMode;
	PivotOffset = Properties->PivotOffset;
	PivotOffsetSpace = Properties->PivotOffsetSpace;
	bLockedAxisEnable = Properties->bLockedAxisEnable;
	LockedAxis = Properties->LockedAxis;
	LockedAxisSpace = Properties->LockedAxisSpace;
	SortMode = Properties->SortMode;
	bSortOnlyWhenTranslucent = Properties->bSortOnlyWhenTranslucent;
	bOverrideMaterials = Properties->bOverrideMaterials;
	SubImageSize = Properties->SubImageSize;
	bSubImageBlend = Properties->bSubImageBlend;
	bEnableFrustumCulling = Properties->bEnableFrustumCulling;
	bEnableCulling = bEnableFrustumCulling;
	DistanceCullRange = FVector2D(0, FLT_MAX);
	RendererVisibility = Properties->RendererVisibility;	
	LocalCullingSphere = Mesh->ExtendedBounds.GetSphere();

	if (Properties->bEnableCameraDistanceCulling)
	{
		DistanceCullRange = FVector2D(Properties->MinCameraDistance, Properties->MaxCameraDistance);
		bEnableCulling = true;
	}

	// Ensure valid value for the locked axis
	if (!LockedAxis.Normalize())
	{
		LockedAxis.Set(0.0f, 0.0f, 1.0f);
	}

	const FNiagaraDataSet& Data = Emitter->GetData();

	RendererVisTagOffset = INDEX_NONE;
	int32 FloatOffset;
	int32 HalfOffset;
	if (Data.GetVariableComponentOffsets(Properties->RendererVisibilityTagBinding.GetDataSetBindableVariable(), FloatOffset, RendererVisTagOffset, HalfOffset))
	{
		// If the renderer visibility tag is bound, we have to do it in the culling pass		
		bEnableCulling = true;
	}

	MaterialParamValidMask = Properties->MaterialParamValidMask;

	RendererLayoutWithCustomSorting = &Properties->RendererLayoutWithCustomSorting;
	RendererLayoutWithoutCustomSorting = &Properties->RendererLayoutWithoutCustomSorting;

	MeshMinimumLOD = Properties->ParticleMesh->MinLOD.GetValue();

	if (MeshRenderData)
	{
		const int32 LODCount = MeshRenderData->LODResources.Num();
		IndexInfoPerSection.SetNum(LODCount);

		for (int32 LODIdx = 0; LODIdx < LODCount; ++LODIdx)
		{
			Properties->GetIndexInfoPerSection(LODIdx, IndexInfoPerSection[LODIdx]);
		}
	}
}

FNiagaraRendererMeshes::~FNiagaraRendererMeshes()
{	
}

void FNiagaraRendererMeshes::ReleaseRenderThreadResources()
{
}

int32 FNiagaraRendererMeshes::GetMaxIndirectArgs() const
{
	// If we're CPU, we only need indirect args if we're culling
	if (SimTarget == ENiagaraSimTarget::CPUSim && !bEnableCulling)
	{
		return 0;
	}

	// currently the most indirect args we can add would be for a single lod, so search for the LOD with the highest number of sections
	// this value should be constant for the life of the renderer as it is being used for NumRegisteredGPURenderers
	int32 MaxSectionCount = 0;

	for (const auto& IndexInfo : IndexInfoPerSection)
	{
		MaxSectionCount = FMath::Max(MaxSectionCount, IndexInfo.Num());
	}
	
	//TODO: This needs to be multiplied by the number of active viewsv
	return MaxSectionCount;
}

void FNiagaraRendererMeshes::SetupVertexFactory(FNiagaraMeshVertexFactory* InVertexFactory, const FStaticMeshLODResources& LODResources) const
{
	FStaticMeshDataType Data;

	LODResources.VertexBuffers.PositionVertexBuffer.BindPositionVertexBuffer(InVertexFactory, Data);
	LODResources.VertexBuffers.StaticMeshVertexBuffer.BindTangentVertexBuffer(InVertexFactory, Data);
	LODResources.VertexBuffers.StaticMeshVertexBuffer.BindTexCoordVertexBuffer(InVertexFactory, Data, MAX_TEXCOORDS);
	LODResources.VertexBuffers.ColorVertexBuffer.BindColorVertexBuffer(InVertexFactory, Data);
	InVertexFactory->SetData(Data);
}

int32 FNiagaraRendererMeshes::GetLODIndex() const
{
	if (!MeshRenderData) { return INDEX_NONE; }
	check(IsInRenderingThread());
	const int32 LODCount = MeshRenderData->LODResources.Num();

	// Doesn't seem to work for some reason. See comment in FDynamicMeshEmitterData::GetMeshLODIndexFromProxy()
	// const int32 LODIndex = FMath::Max<int32>((int32)MeshRenderData->CurrentFirstLODIdx, MeshMinimumLOD);
	int32 LODIndex = FMath::Clamp<int32>(MeshRenderData->CurrentFirstLODIdx, 0, LODCount - 1);

	while (LODIndex < LODCount && !MeshRenderData->LODResources[LODIndex].GetNumVertices())
	{
		++LODIndex;
	}

	check(MeshRenderData->LODResources[LODIndex].GetNumVertices());

	return LODIndex;
}

void FNiagaraRendererMeshes::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector, const FNiagaraSceneProxy* SceneProxy) const
{
	check(SceneProxy);

	SCOPE_CYCLE_COUNTER(STAT_NiagaraRender);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderMeshes);
	PARTICLE_PERF_STAT_CYCLES(SceneProxy->PerfAsset, GetDynamicMeshElements);

	NiagaraEmitterInstanceBatcher* Batcher = SceneProxy->GetBatcher();
	FNiagaraDynamicDataMesh* DynamicDataMesh = (static_cast<FNiagaraDynamicDataMesh*>(DynamicDataRender));
	if (!DynamicDataMesh || !Batcher)
	{
		return;
	}

	FNiagaraDataBuffer* SourceParticleData = DynamicDataMesh->GetParticleDataToRender();
	if (SourceParticleData == nullptr ||
		MeshRenderData == nullptr ||
		SourceParticleData->GetNumInstancesAllocated() == 0 ||
		SourceParticleData->GetNumInstances() == 0 ||
		GbEnableNiagaraMeshRendering == 0 ||
		!GSupportsResourceView  // Current shader requires SRV to draw properly in all cases.
		)
	{
		return;
	}

#if STATS
	FScopeCycleCounter EmitterStatsCounter(EmitterStatID);
#endif

	int32 NumInstances = SourceParticleData->GetNumInstances();

	FGlobalDynamicReadBuffer& DynamicReadBuffer = Collector.GetDynamicReadBuffer();
	FParticleRenderData ParticleFloatData;
	FGlobalDynamicReadBuffer::FAllocation ParticleIntData;

	// Grab the material proxies we'll be using for each section and check them for translucency.
	bool bHasTranslucentMaterials = false;
	for (FMaterialRenderProxy* MaterialProxy : DynamicDataMesh->Materials)
	{
		check(MaterialProxy);
		EBlendMode BlendMode = MaterialProxy->GetMaterial(FeatureLevel)->GetBlendMode();
		bHasTranslucentMaterials |= IsTranslucentBlendMode(BlendMode);
	}

	// NOTE: have to run the GPU sort when culling is enabled if supported on this platform
	// TODO: implement culling and renderer visibility on the CPU for other platforms
	const bool bGPUSortEnabled = FNiagaraUtilities::AllowComputeShaders(Batcher->GetShaderPlatform());
	const bool bDoGPUCulling = bEnableCulling && GNiagaraGPUCulling && FNiagaraUtilities::AllowComputeShaders(Batcher->GetShaderPlatform());
	const bool bShouldSort = SortMode != ENiagaraSortMode::None && (bHasTranslucentMaterials || !bSortOnlyWhenTranslucent);
	const bool bCustomSorting = SortMode == ENiagaraSortMode::CustomAscending || SortMode == ENiagaraSortMode::CustomDecending;

	const FNiagaraRendererLayout* RendererLayout = bCustomSorting ? RendererLayoutWithCustomSorting : RendererLayoutWithoutCustomSorting;

	//For cpu sims we allocate render buffers from the global pool. GPU sims own their own.
	if (SimTarget == ENiagaraSimTarget::CPUSim)
	{
		if (GbEnableMinimalGPUBuffers)
		{
			ParticleFloatData = TransferDataToGPU(DynamicReadBuffer, RendererLayout, SourceParticleData);
		}
		else
		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderMeshes_AllocateGPUData);
			int32 TotalFloatSize = SourceParticleData->GetFloatBuffer().Num() / sizeof(float);
			ParticleFloatData.FloatData = DynamicReadBuffer.AllocateFloat(TotalFloatSize);
			FMemory::Memcpy(ParticleFloatData.FloatData.Buffer, SourceParticleData->GetFloatBuffer().GetData(), SourceParticleData->GetFloatBuffer().Num());
			int32 TotalHalfSize = SourceParticleData->GetHalfBuffer().Num() / sizeof(FFloat16);
			ParticleFloatData.HalfData = DynamicReadBuffer.AllocateHalf(TotalFloatSize);
			FMemory::Memcpy(ParticleFloatData.HalfData.Buffer, SourceParticleData->GetHalfBuffer().GetData(), SourceParticleData->GetHalfBuffer().Num());
		}

		if (RendererVisTagOffset != INDEX_NONE)
		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderMeshes_AllocateGPUData);

			// For CPU sims, we need to also copy off the renderer visibility tags for the sort shader
			ParticleIntData = DynamicReadBuffer.AllocateInt32(NumInstances);
			int32* Dest = (int32*)ParticleIntData.Buffer;
			const int32* Src = (const int32*)SourceParticleData->GetInt32Buffer().GetData();
			const uint32 IntStride = SourceParticleData->GetInt32Stride() / sizeof(uint32);
			for (int32 InstIdx = 0; InstIdx < NumInstances; ++InstIdx)
			{
				Dest[InstIdx] = Src[RendererVisTagOffset * IntStride + InstIdx];
			}
		}
	}
	
	// @TODO : support multiple LOD	
	const int32 LODIndex = GetLODIndex();
	const FStaticMeshLODResources& LODModel = MeshRenderData->LODResources[LODIndex];
	const int32 SectionCount = LODModel.Sections.Num();

	{
		// Compute the per-view uniform buffers.
		const int32 NumViews = Views.Num();
		for (int32 ViewIndex = 0; ViewIndex < NumViews; ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				const FSceneView* View = Views[ViewIndex];				
				
				const bool bIsInstancedStereo = View->bIsInstancedStereoEnabled && IStereoRendering::IsStereoEyeView(*View);				
				if (bIsInstancedStereo && !IStereoRendering::IsAPrimaryView(*View))
				{
					// One eye renders everything, so we can skip non-primaries
					continue;
				}

				int32 CulledGPUCountOffset = bDoGPUCulling ? Batcher->GetGPUInstanceCounterManager().AcquireCulledEntry() : INDEX_NONE;

				// Alloc indirect draw counts for every section this view
				TArray<uint32, TInlineAllocator<8>> IndirectArgsOffsets;
				if (SimTarget == ENiagaraSimTarget::GPUComputeSim || bDoGPUCulling)
				{
					IndirectArgsOffsets.SetNum(SectionCount);
					for (int32 SectionIdx = 0; SectionIdx < SectionCount; ++SectionIdx)
					{
						IndirectArgsOffsets[SectionIdx] = Batcher->GetGPUInstanceCounterManager().AddDrawIndirect(
							bDoGPUCulling ? CulledGPUCountOffset : SourceParticleData->GetGPUInstanceCountBufferOffset(),
							IndexInfoPerSection[LODIndex][SectionIdx].Key,
							IndexInfoPerSection[LODIndex][SectionIdx].Value,
							bIsInstancedStereo,
							bDoGPUCulling);
					}
				}

				FNiagaraMeshCollectorResourcesMesh& CollectorResources = Collector.AllocateOneFrameResource<FNiagaraMeshCollectorResourcesMesh>();

				// Get the next vertex factory to use
				// TODO: Find a way to safely pool these such that they won't be concurrently accessed by multiple views
				FNiagaraMeshVertexFactory* VertexFactory = &CollectorResources.VertexFactory;
				VertexFactory->SetParticleFactoryType(NVFT_Mesh);
				VertexFactory->SetLODIndex(LODIndex);
				VertexFactory->InitResource();
				SetupVertexFactory(VertexFactory, LODModel);

				FNiagaraMeshUniformParameters PerViewUniformParameters;// = UniformParameters;
				FMemory::Memzero(&PerViewUniformParameters, sizeof(PerViewUniformParameters)); // Clear unset bytes

				PerViewUniformParameters.bLocalSpace = bLocalSpace;
				PerViewUniformParameters.PrevTransformAvailable = false;
				PerViewUniformParameters.DeltaSeconds = ViewFamily.DeltaWorldTime;

				// Calculate pivot offset
				FVector WorldSpacePivotOffset = FVector(0, 0, 0);
				FSphere OffsetCullingSphere = LocalCullingSphere;
				if (PivotOffsetSpace == ENiagaraMeshPivotOffsetSpace::Mesh)
				{
					OffsetCullingSphere.Center += PivotOffset;

					PerViewUniformParameters.PivotOffset = PivotOffset;
					PerViewUniformParameters.bPivotOffsetIsWorldSpace = false;
				}
				else
				{
					WorldSpacePivotOffset = PivotOffset;
					if (PivotOffsetSpace == ENiagaraMeshPivotOffsetSpace::Local || (bLocalSpace && PivotOffsetSpace == ENiagaraMeshPivotOffsetSpace::Simulation))
					{
						// The offset is in local space, transform it to world
						WorldSpacePivotOffset = SceneProxy->GetLocalToWorld().TransformVector(WorldSpacePivotOffset);
					}

					PerViewUniformParameters.PivotOffset = WorldSpacePivotOffset;
					PerViewUniformParameters.bPivotOffsetIsWorldSpace = true;
				}

				TConstArrayView<FNiagaraRendererVariableInfo> VFVariables = RendererLayout->GetVFVariables_RenderThread();
				PerViewUniformParameters.PositionDataOffset = VFVariables[ENiagaraMeshVFLayout::Position].GetGPUOffset();
				PerViewUniformParameters.VelocityDataOffset = VFVariables[ENiagaraMeshVFLayout::Velocity].GetGPUOffset();
				PerViewUniformParameters.ColorDataOffset = VFVariables[ENiagaraMeshVFLayout::Color].GetGPUOffset();
				PerViewUniformParameters.ScaleDataOffset = VFVariables[ENiagaraMeshVFLayout::Scale].GetGPUOffset();
				PerViewUniformParameters.TransformDataOffset = VFVariables[ENiagaraMeshVFLayout::Transform].GetGPUOffset();
				PerViewUniformParameters.NormalizedAgeDataOffset = VFVariables[ENiagaraMeshVFLayout::NormalizedAge].GetGPUOffset();
				PerViewUniformParameters.MaterialRandomDataOffset = VFVariables[ENiagaraMeshVFLayout::MaterialRandom].GetGPUOffset();
				PerViewUniformParameters.SubImageDataOffset = VFVariables[ENiagaraMeshVFLayout::SubImage].GetGPUOffset();
				PerViewUniformParameters.MaterialParamDataOffset = VFVariables[ENiagaraMeshVFLayout::DynamicParam0].GetGPUOffset();
				PerViewUniformParameters.MaterialParam1DataOffset = VFVariables[ENiagaraMeshVFLayout::DynamicParam1].GetGPUOffset();
				PerViewUniformParameters.MaterialParam2DataOffset = VFVariables[ENiagaraMeshVFLayout::DynamicParam2].GetGPUOffset();
				PerViewUniformParameters.MaterialParam3DataOffset = VFVariables[ENiagaraMeshVFLayout::DynamicParam3].GetGPUOffset();
				PerViewUniformParameters.CameraOffsetDataOffset = VFVariables[ENiagaraMeshVFLayout::CameraOffset].GetGPUOffset();

				PerViewUniformParameters.MaterialParamValidMask = MaterialParamValidMask;
				PerViewUniformParameters.SizeDataOffset = INDEX_NONE;
				PerViewUniformParameters.DefaultPos = bLocalSpace ? FVector4(0.0f, 0.0f, 0.0f, 1.0f) : FVector4(SceneProxy->GetLocalToWorld().GetOrigin());				
				PerViewUniformParameters.SubImageSize = FVector4(SubImageSize.X, SubImageSize.Y, 1.0f / SubImageSize.X, 1.0f / SubImageSize.Y);
				PerViewUniformParameters.SubImageBlendMode = bSubImageBlend;
				PerViewUniformParameters.FacingMode = (uint32)FacingMode;
				PerViewUniformParameters.bLockedAxisEnable = bLockedAxisEnable;
				PerViewUniformParameters.LockedAxis = LockedAxis;
				PerViewUniformParameters.LockedAxisSpace = (uint32)LockedAxisSpace;

				//Sort particles if needed.
				VertexFactory->SetSortedIndices(nullptr, 0xFFFFFFFF);

				FNiagaraGPUSortInfo SortInfo;
				int32 SortVarIdx = INDEX_NONE;
				if (bShouldSort || bDoGPUCulling)
				{
					SortInfo.ParticleCount = NumInstances;
					SortInfo.SortMode = SortMode;
					SortInfo.SetSortFlags(GNiagaraGPUSortingUseMaxPrecision != 0, bHasTranslucentMaterials); 
					SortInfo.bEnableCulling = bDoGPUCulling;
					SortInfo.LocalBSphere = OffsetCullingSphere;
					SortInfo.CullingWorldSpaceOffset = WorldSpacePivotOffset;
					SortInfo.RendererVisTagAttributeOffset = RendererVisTagOffset;
					SortInfo.RendererVisibility = RendererVisibility;
					SortInfo.DistanceCullRange = DistanceCullRange;

					SortVarIdx = bCustomSorting ? ENiagaraMeshVFLayout::CustomSorting : ENiagaraMeshVFLayout::Position;
					SortInfo.SortAttributeOffset = VFVariables[SortVarIdx].GetGPUOffset();

					auto GetViewMatrices = [](const FSceneView& View, FVector& OutViewOrigin) -> const FViewMatrices&
					{
						OutViewOrigin = View.ViewLocation;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
						const FSceneViewState* ViewState = View.State != nullptr ? View.State->GetConcreteViewState() : nullptr;
						if (ViewState && ViewState->bIsFrozen && ViewState->bIsFrozenViewMatricesCached)
						{
							// Use the frozen view for culling so we can test that it's working
							OutViewOrigin = ViewState->CachedViewMatrices.GetViewOrigin();

							// Don't retrieve the cached matrices for shadow views
							bool bIsShadow = View.GetDynamicMeshElementsShadowCullFrustum() != nullptr;
							if (!bIsShadow)
							{
								return ViewState->CachedViewMatrices;
							}
						}
#endif

						return View.ViewMatrices;
					};

					const FViewMatrices& ViewMatrices = GetViewMatrices(*View, SortInfo.ViewOrigin);
					SortInfo.ViewDirection = ViewMatrices.GetViewMatrix().GetColumn(2);


#ifdef HMD_MODULE_INCLUDED
					if (View->StereoPass != eSSP_FULL && GEngine->XRSystem.IsValid() && (GEngine->XRSystem->GetHMDDevice() != nullptr))
#else
					if (View->StereoPass != eSSP_FULL && Views.Num() > 1)
#endif
					{
						// For VR, do distance culling and sorting from a central eye position to prevent differences between views
						const uint32 PairedViewIdx = (ViewIndex & 1) ? (ViewIndex - 1) : (ViewIndex + 1);
						const FSceneView* PairedView = Views[PairedViewIdx];
						check(PairedView);

						FVector PairedViewOrigin;
						GetViewMatrices(*PairedView, PairedViewOrigin);
						SortInfo.ViewOrigin = 0.5f * (SortInfo.ViewOrigin + PairedViewOrigin);
					}						

					if (bEnableFrustumCulling)
					{
						if (const FConvexVolume* ShadowFrustum = View->GetDynamicMeshElementsShadowCullFrustum())
						{
							// Ensure we don't break the maximum number of planes here
							// (For an accurate shadow frustum, a tight hull is formed from the silhouette and back-facing planes of the view frustum)
							check(ShadowFrustum->Planes.Num() <= FNiagaraGPUSortInfo::MaxCullPlanes);
							SortInfo.CullPlanes = ShadowFrustum->Planes;

							// Remove pre-shadow translation to get the planes in world space
							const FVector PreShadowTranslation = View->GetPreShadowTranslation();
							for (FPlane& Plane : SortInfo.CullPlanes)
							{
								Plane.W -= FVector::DotProduct(FVector(Plane), PreShadowTranslation);
							}
						}
						else
						{
							SortInfo.CullPlanes.SetNumZeroed(6);

							// Gather the culling planes from the view projection matrix
							const FMatrix& ViewProj = ViewMatrices.GetViewProjectionMatrix();
							ViewProj.GetFrustumNearPlane(SortInfo.CullPlanes[0]);
							ViewProj.GetFrustumFarPlane(SortInfo.CullPlanes[1]);
							ViewProj.GetFrustumTopPlane(SortInfo.CullPlanes[2]);
							ViewProj.GetFrustumBottomPlane(SortInfo.CullPlanes[3]);

							ViewProj.GetFrustumLeftPlane(SortInfo.CullPlanes[4]);
							if (bIsInstancedStereo)
							{
								// For Instanced Stereo, cull using an extended frustum that encompasses both eyes
								ensure(View->StereoPass == eSSP_LEFT_EYE); // Sanity check that the primary eye is the left
								const FSceneView* RightEyeView = Views[ViewIndex + 1];
								check(RightEyeView);
								FVector RightEyePos;
								GetViewMatrices(*RightEyeView, RightEyePos).GetViewProjectionMatrix().GetFrustumRightPlane(SortInfo.CullPlanes[5]);
							}
							else
							{
								ViewProj.GetFrustumRightPlane(SortInfo.CullPlanes[5]);
							}
						}
					}

					if (bLocalSpace)
					{
						SortInfo.ViewOrigin = SceneProxy->GetLocalToWorldInverse().TransformPosition(SortInfo.ViewOrigin);
						SortInfo.ViewDirection = SceneProxy->GetLocalToWorld().GetTransposed().TransformVector(SortInfo.ViewDirection);
						if (bEnableFrustumCulling)
						{
							for (FPlane& Plane : SortInfo.CullPlanes)
							{
								Plane = Plane.TransformBy(SceneProxy->GetLocalToWorldInverse());
							}
						}
					}

					if (bDoGPUCulling)
					{
						SortInfo.CullPositionAttributeOffset = VFVariables[ENiagaraMeshVFLayout::Position].GetGPUOffset();
						SortInfo.CullOrientationAttributeOffset = VFVariables[ENiagaraMeshVFLayout::Transform].GetGPUOffset();
						SortInfo.CullScaleAttributeOffset = VFVariables[ENiagaraMeshVFLayout::Scale].GetGPUOffset();
					}
				}

				if (SimTarget == ENiagaraSimTarget::CPUSim)
				{
					check(RendererVisTagOffset == INDEX_NONE || ParticleIntData.IsValid());

					FRHIShaderResourceView* FloatSRV = ParticleFloatData.FloatData.IsValid() ? (FRHIShaderResourceView*)ParticleFloatData.FloatData.SRV : FNiagaraRenderer::GetDummyFloatBuffer();
					FRHIShaderResourceView* HalfSRV = ParticleFloatData.HalfData.IsValid() ? (FRHIShaderResourceView*)ParticleFloatData.HalfData.SRV : FNiagaraRenderer::GetDummyHalfBuffer();
					FRHIShaderResourceView* IntSRV = ParticleIntData.IsValid() ? (FRHIShaderResourceView*)ParticleIntData.SRV : FNiagaraRenderer::GetDummyIntBuffer();
					const uint32 FloatParticleDataStride = GbEnableMinimalGPUBuffers ? NumInstances : (SourceParticleData->GetFloatStride() / sizeof(float));
					const uint32 HalfParticleDataStride = GbEnableMinimalGPUBuffers ? NumInstances : (SourceParticleData->GetHalfStride() / sizeof(FFloat16));
					const uint32 IntParticleDataStride = ParticleIntData.IsValid() ? NumInstances : 0; // because we copied it off

					if (bShouldSort || bDoGPUCulling)
					{
						const int32 Threshold = GNiagaraGPUSortingCPUToGPUThreshold;
						if (bDoGPUCulling || (bGPUSortEnabled && Threshold >= 0 && SortInfo.ParticleCount > Threshold))
						{
							// We want to sort or cull on GPU
							SortInfo.ParticleCount = NumInstances;
							SortInfo.ParticleDataFloatSRV = FloatSRV;
							SortInfo.ParticleDataHalfSRV = HalfSRV;
							SortInfo.ParticleDataIntSRV = IntSRV;
							SortInfo.FloatDataStride = FloatParticleDataStride;
							SortInfo.HalfDataStride = HalfParticleDataStride;
							SortInfo.IntDataStride = IntParticleDataStride;
							SortInfo.GPUParticleCountSRV = Batcher->GetGPUInstanceCounterManager().GetInstanceCountBuffer().SRV;
							SortInfo.GPUParticleCountOffset = SourceParticleData->GetGPUInstanceCountBufferOffset();
							SortInfo.CulledGPUParticleCountOffset = CulledGPUCountOffset;
							SortInfo.RendererVisTagAttributeOffset = RendererVisTagOffset == INDEX_NONE ? INDEX_NONE : 0; // because it's copied off							

							const int32 IndexBufferOffset = Batcher->AddSortedGPUSimulation(SortInfo);
							if (IndexBufferOffset != INDEX_NONE)
							{
								VertexFactory->SetSortedIndices(SortInfo.AllocationInfo.BufferSRV, SortInfo.AllocationInfo.BufferOffset);
							}
						}
						else
						{
							// We want to sort on CPU
							FGlobalDynamicReadBuffer::FAllocation SortedIndices;
							SortedIndices = DynamicReadBuffer.AllocateInt32(NumInstances);
							SortIndices(SortInfo, VFVariables[SortVarIdx], *SourceParticleData, SortedIndices);
							VertexFactory->SetSortedIndices(SortedIndices.SRV, 0);
						}
					}

					PerViewUniformParameters.NiagaraFloatDataStride = FloatParticleDataStride;
					PerViewUniformParameters.NiagaraParticleDataFloat = FloatSRV;
					PerViewUniformParameters.NiagaraParticleDataHalf = HalfSRV;
				}
				else
				{
					FRHIShaderResourceView* FloatSRV = SourceParticleData->GetGPUBufferFloat().SRV.IsValid() ? (FRHIShaderResourceView*)SourceParticleData->GetGPUBufferFloat().SRV : FNiagaraRenderer::GetDummyFloatBuffer();
					FRHIShaderResourceView* HalfSRV = SourceParticleData->GetGPUBufferHalf().SRV.IsValid() ? (FRHIShaderResourceView*)SourceParticleData->GetGPUBufferHalf().SRV : FNiagaraRenderer::GetDummyHalfBuffer();
					FRHIShaderResourceView* IntSRV = SourceParticleData->GetGPUBufferInt().SRV.IsValid() ? (FRHIShaderResourceView*)SourceParticleData->GetGPUBufferInt().SRV : FNiagaraRenderer::GetDummyIntBuffer();
					const uint32 FloatParticleDataStride = SourceParticleData->GetFloatStride() / sizeof(float);
					const uint32 HalfParticleDataStride = SourceParticleData->GetHalfStride() / sizeof(FFloat16);
					const uint32 IntParticleDataStride = SourceParticleData->GetInt32Stride() / sizeof(int32);

					if (bShouldSort || bDoGPUCulling)
					{
						// Here we need to be conservative about the InstanceCount, since the final value is only known on the GPU after the simulation.
						SortInfo.ParticleCount = SourceParticleData->GetNumInstances();
						SortInfo.ParticleDataFloatSRV = FloatSRV;
						SortInfo.ParticleDataHalfSRV = HalfSRV;
						SortInfo.ParticleDataIntSRV = IntSRV;
						SortInfo.FloatDataStride = FloatParticleDataStride;
						SortInfo.HalfDataStride = HalfParticleDataStride;
						SortInfo.IntDataStride = IntParticleDataStride;
						SortInfo.GPUParticleCountSRV = Batcher->GetGPUInstanceCounterManager().GetInstanceCountBuffer().SRV;
						SortInfo.GPUParticleCountOffset = SourceParticleData->GetGPUInstanceCountBufferOffset();
						SortInfo.CulledGPUParticleCountOffset = CulledGPUCountOffset;
						SortInfo.RendererVisTagAttributeOffset = RendererVisTagOffset;

						const int32 IndexBufferOffset = Batcher->AddSortedGPUSimulation(SortInfo);
						if (IndexBufferOffset != INDEX_NONE && SortInfo.GPUParticleCountOffset != INDEX_NONE)
						{
							VertexFactory->SetSortedIndices(SortInfo.AllocationInfo.BufferSRV, SortInfo.AllocationInfo.BufferOffset);
						}
					}
					
					PerViewUniformParameters.NiagaraFloatDataStride = FloatParticleDataStride;
					PerViewUniformParameters.NiagaraParticleDataFloat = FloatSRV;
					PerViewUniformParameters.NiagaraParticleDataHalf = HalfSRV;
				}

				// Collector.AllocateOneFrameResource uses default ctor, initialize the vertex factory
				CollectorResources.UniformBuffer = FNiagaraMeshUniformBufferRef::CreateUniformBufferImmediate(PerViewUniformParameters, UniformBuffer_SingleFrame);
				VertexFactory->SetUniformBuffer(CollectorResources.UniformBuffer);

				// Increment stats
				INC_DWORD_STAT_BY(STAT_NiagaraNumMeshVerts, NumInstances * LODModel.GetNumVertices());
				INC_DWORD_STAT_BY(STAT_NiagaraNumMeshes, NumInstances);

				const bool bIsWireframe = AllowDebugViewmodes() && View && View->Family->EngineShowFlags.Wireframe;
				for (int32 SectionIndex = 0; SectionIndex < SectionCount; SectionIndex++)
				{
					const FStaticMeshSection& Section = LODModel.Sections[SectionIndex];
					FMaterialRenderProxy* MaterialProxy = DynamicDataMesh->Materials[SectionIndex];
					if ((Section.NumTriangles == 0) || (MaterialProxy == NULL))
					{
						//@todo. This should never occur, but it does occasionally.
						continue;
					}

					FMeshBatch& Mesh = Collector.AllocateMesh();
					Mesh.VertexFactory = VertexFactory;
					Mesh.LCI = NULL;
					Mesh.ReverseCulling = SceneProxy->IsLocalToWorldDeterminantNegative();
					Mesh.CastShadow = SceneProxy->CastsDynamicShadow();
#if RHI_RAYTRACING
					Mesh.CastRayTracedShadow = SceneProxy->CastsDynamicShadow();
#endif
					Mesh.DepthPriorityGroup = (ESceneDepthPriorityGroup)SceneProxy->GetDepthPriorityGroup(View);

					FMeshBatchElement& BatchElement = Mesh.Elements[0];
					BatchElement.PrimitiveUniformBuffer = IsMotionBlurEnabled() ? SceneProxy->GetUniformBuffer() : SceneProxy->GetUniformBufferNoVelocity();
					BatchElement.FirstIndex = 0;
					BatchElement.MinVertexIndex = 0;
					BatchElement.MaxVertexIndex = 0;
					BatchElement.NumInstances = NumInstances;

					if (bIsWireframe)
					{
						if (LODModel.AdditionalIndexBuffers && LODModel.AdditionalIndexBuffers->WireframeIndexBuffer.IsInitialized())
						{
							Mesh.Type = PT_LineList;
							Mesh.MaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
							BatchElement.FirstIndex = 0;
							BatchElement.IndexBuffer = &LODModel.AdditionalIndexBuffers->WireframeIndexBuffer;
							BatchElement.NumPrimitives = LODModel.AdditionalIndexBuffers->WireframeIndexBuffer.GetNumIndices() / 2;
						}
						else
						{
							Mesh.Type = PT_TriangleList;
							Mesh.MaterialRenderProxy = MaterialProxy;
							Mesh.bWireframe = true;
							BatchElement.FirstIndex = 0;
							BatchElement.IndexBuffer = &LODModel.IndexBuffer;
							BatchElement.NumPrimitives = LODModel.IndexBuffer.GetNumIndices() / 3;
						}
					}
					else
					{
						Mesh.Type = PT_TriangleList;
						Mesh.MaterialRenderProxy = MaterialProxy;
						BatchElement.IndexBuffer = &LODModel.IndexBuffer;
						BatchElement.FirstIndex = Section.FirstIndex;
						BatchElement.NumPrimitives = Section.NumTriangles;
					}

					if (IndirectArgsOffsets.IsValidIndex(SectionIndex))
					{
						BatchElement.NumPrimitives = 0;
						BatchElement.IndirectArgsOffset = IndirectArgsOffsets[SectionIndex];
						BatchElement.IndirectArgsBuffer = Batcher->GetGPUInstanceCounterManager().GetDrawIndirectBuffer().Buffer;
					}
					else
					{
						check(BatchElement.NumPrimitives > 0);
					}

					Mesh.bCanApplyViewModeOverrides = true;
					Mesh.bUseWireframeSelectionColoring = SceneProxy->IsSelected();

					Collector.AddMesh(ViewIndex, Mesh);
				}
			}
		}
	}
}

#if RHI_RAYTRACING

void FNiagaraRendererMeshes::GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances, const FNiagaraSceneProxy* SceneProxy)
{
	if (!CVarRayTracingNiagaraMeshes.GetValueOnRenderThread())
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_NiagaraRender);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderMeshes);
	check(SceneProxy);

	NiagaraEmitterInstanceBatcher* Batcher = SceneProxy->GetBatcher();
	FNiagaraDynamicDataMesh* DynamicDataMesh = (static_cast<FNiagaraDynamicDataMesh*>(DynamicDataRender));
	if (!DynamicDataMesh || !Batcher)
	{
		return;
	}

	FNiagaraDataBuffer* SourceParticleData = DynamicDataMesh->GetParticleDataToRender();
	if (SourceParticleData == nullptr ||
		MeshRenderData == nullptr ||
		SourceParticleData->GetNumInstancesAllocated() == 0 ||
		SourceParticleData->GetNumInstances() == 0 ||
		GbEnableNiagaraMeshRendering == 0 ||
		!GSupportsResourceView  // Current shader requires SRV to draw properly in all cases.
		)
	{
		return;
	}

	int32 LODIndex = (int32)MeshRenderData->CurrentFirstLODIdx;
	while (LODIndex < MeshRenderData->LODResources.Num() - 1 && !MeshRenderData->LODResources[LODIndex].GetNumVertices())
	{
		++LODIndex;
	}

	const FStaticMeshLODResources& LODModel = MeshRenderData->LODResources[LODIndex];
	FRayTracingGeometry& Geometry = MeshRenderData->LODResources[LODIndex].RayTracingGeometry;
	FRayTracingInstance RayTracingInstance;
	RayTracingInstance.Geometry = &Geometry;

	for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); SectionIndex++)
	{
		const FStaticMeshSection& Section = LODModel.Sections[SectionIndex];
		FMaterialRenderProxy* MaterialProxy = DynamicDataMesh->Materials[SectionIndex];
		if ((Section.NumTriangles == 0) || (MaterialProxy == NULL))
		{
			continue;
		}

		FMeshBatch MeshBatch;
		const FStaticMeshLODResources& LOD = MeshRenderData->LODResources[LODIndex];
		const FStaticMeshVertexFactories& VFs = MeshRenderData->LODVertexFactories[LODIndex];
		FVertexFactory* VertexFactory = &MeshRenderData->LODVertexFactories[LODIndex].VertexFactory;

		MeshBatch.VertexFactory = VertexFactory;
		MeshBatch.MaterialRenderProxy = MaterialProxy;
		MeshBatch.SegmentIndex = SectionIndex;
		MeshBatch.LODIndex = LODIndex;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		MeshBatch.VisualizeLODIndex = LODIndex;
#endif
		MeshBatch.CastShadow = SceneProxy->CastsDynamicShadow();
		MeshBatch.CastRayTracedShadow = SceneProxy->CastsDynamicShadow();

		FMeshBatchElement& MeshBatchElement = MeshBatch.Elements[0];
		MeshBatchElement.VertexFactoryUserData = VFs.VertexFactory.GetUniformBuffer();
		MeshBatchElement.MinVertexIndex = Section.MinVertexIndex;
		MeshBatchElement.MaxVertexIndex = Section.MaxVertexIndex;
		
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		MeshBatchElement.VisualizeElementIndex = SectionIndex;
#endif
		RayTracingInstance.Materials.Add(MeshBatch);
	}

	const FNiagaraRendererLayout* RendererLayout = RendererLayoutWithCustomSorting;
	TConstArrayView<FNiagaraRendererVariableInfo> VFVariables = RendererLayout->GetVFVariables_RenderThread();
	const int32 NumInstances = SourceParticleData->GetNumInstances();
	const int32 TotalFloatSize = RendererLayout->GetTotalFloatComponents_RenderThread() * SourceParticleData->GetNumInstances();
	const int32 ComponentStrideDest = SourceParticleData->GetNumInstances() * sizeof(float);

	//ENiagaraMeshVFLayout::Transform just contains a Quat, not the whole transform
	const FNiagaraRendererVariableInfo& VarPositionInfo = VFVariables[ENiagaraMeshVFLayout::Position];
	const FNiagaraRendererVariableInfo& VarScaleInfo = VFVariables[ENiagaraMeshVFLayout::Scale];
	const FNiagaraRendererVariableInfo& VarTransformInfo = VFVariables[ENiagaraMeshVFLayout::Transform];
	
	int32 PositionBaseCompOffset = VarPositionInfo.DatasetOffset;
	int32 ScaleBaseCompOffset = VarScaleInfo.DatasetOffset;
	int32 TransformBaseCompOffset = VarTransformInfo.DatasetOffset;
	
	float* RESTRICT PositionX = (float*)SourceParticleData->GetComponentPtrFloat(PositionBaseCompOffset);
	float* RESTRICT PositionY = (float*)SourceParticleData->GetComponentPtrFloat(PositionBaseCompOffset + 1);
	float* RESTRICT PositionZ = (float*)SourceParticleData->GetComponentPtrFloat(PositionBaseCompOffset + 2);

	float* RESTRICT ScaleX = (float*)SourceParticleData->GetComponentPtrFloat(ScaleBaseCompOffset);
	float* RESTRICT ScaleY = (float*)SourceParticleData->GetComponentPtrFloat(ScaleBaseCompOffset + 1);
	float* RESTRICT ScaleZ = (float*)SourceParticleData->GetComponentPtrFloat(ScaleBaseCompOffset + 2);
	
	float* RESTRICT QuatArrayX = (float*)SourceParticleData->GetComponentPtrFloat(TransformBaseCompOffset);
	float* RESTRICT QuatArrayY = (float*)SourceParticleData->GetComponentPtrFloat(TransformBaseCompOffset + 1);
	float* RESTRICT QuatArrayZ = (float*)SourceParticleData->GetComponentPtrFloat(TransformBaseCompOffset + 2);
	float* RESTRICT QuatArrayW = (float*)SourceParticleData->GetComponentPtrFloat(TransformBaseCompOffset + 3);

	auto GetInstancePosition = [&PositionX, &PositionY, &PositionZ](int32 Idx)
	{
		return FVector4(PositionX[Idx], PositionY[Idx], PositionZ[Idx], 1);
	};

	auto GetInstanceScale = [&ScaleX, &ScaleY, &ScaleZ](int32 Idx)
	{
		return FVector(ScaleX[Idx], ScaleY[Idx], ScaleZ[Idx]);
	};

	auto GetInstanceQuat = [&QuatArrayX, &QuatArrayY, &QuatArrayZ, &QuatArrayW](int32 Idx)
	{
		return FQuat(QuatArrayX[Idx], QuatArrayY[Idx], QuatArrayZ[Idx], QuatArrayW[Idx]);
	};

	//#dxr_todo: handle MESH_FACING_VELOCITY, MESH_FACING_CAMERA_POSITION, MESH_FACING_CAMERA_PLANE
	bool bHasPosition = PositionBaseCompOffset > 0;
	bool bHasRotation = TransformBaseCompOffset > 0;
	bool bHasScale = ScaleBaseCompOffset > 0;

	FMatrix LocalTransform(SceneProxy->GetLocalToWorld());

	for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; InstanceIndex++)
	{	
		FMatrix InstanceTransform(FMatrix::Identity);

		if (SimTarget == ENiagaraSimTarget::CPUSim)
		{
			FVector4 InstancePos = bHasPosition ? GetInstancePosition(InstanceIndex) : FVector4(0,0,0,0);
			
			FVector4 Transform1 = FVector4(1.0f, 0.0f, 0.0f, InstancePos.X);
			FVector4 Transform2 = FVector4(0.0f, 1.0f, 0.0f, InstancePos.Y);
			FVector4 Transform3 = FVector4(0.0f, 0.0f, 1.0f, InstancePos.Z);

			if (bHasRotation)
			{
				FQuat InstanceQuat = GetInstanceQuat(InstanceIndex);
				FTransform RotationTransform(InstanceQuat.GetNormalized());
				FMatrix RotationMatrix = RotationTransform.ToMatrixWithScale();			

				Transform1.X = RotationMatrix.M[0][0];
				Transform1.Y = RotationMatrix.M[0][1];
				Transform1.Z = RotationMatrix.M[0][2];

				Transform2.X = RotationMatrix.M[1][0];
				Transform2.Y = RotationMatrix.M[1][1];
				Transform2.Z = RotationMatrix.M[1][2];

				Transform3.X = RotationMatrix.M[2][0];
				Transform3.Y = RotationMatrix.M[2][1];
				Transform3.Z = RotationMatrix.M[2][2];
			}

			FMatrix ScaleMatrix(FMatrix::Identity);
			if (bHasScale)
			{
				FVector InstanceSca(GetInstanceScale(InstanceIndex));
				ScaleMatrix.M[0][0] *= InstanceSca.X;
				ScaleMatrix.M[1][1] *= InstanceSca.Y;
				ScaleMatrix.M[2][2] *= InstanceSca.Z;
			}

			InstanceTransform = FMatrix(FPlane(Transform1), FPlane(Transform2), FPlane(Transform3), FPlane(0.0, 0.0, 0.0, 1.0));
			InstanceTransform = InstanceTransform * ScaleMatrix;
			InstanceTransform = InstanceTransform.GetTransposed();

			if (bLocalSpace)
			{
				InstanceTransform = InstanceTransform * LocalTransform;
			}
		}
		else
		{
			// Indirect instancing dispatching: transforms are not available at this point but computed in GPU instead
			// Set invalid transforms so ray tracing ignores them. Valid transforms will be set later directly in the GPU
			FMatrix ScaleTransform = FMatrix::Identity;
			ScaleTransform.M[0][0] = 0.0;
			ScaleTransform.M[1][1] = 0.0;
			ScaleTransform.M[2][2] = 0.0;

			InstanceTransform = ScaleTransform * InstanceTransform;
		}

		RayTracingInstance.InstanceTransforms.Add(InstanceTransform);
	}

	// Set indirect transforms for GPU instances
	if (SimTarget == ENiagaraSimTarget::GPUComputeSim 
		&& FNiagaraUtilities::AllowComputeShaders(GShaderPlatformForFeatureLevel[FeatureLevel])
		&& FDataDrivenShaderPlatformInfo::GetSupportsRayTracingIndirectInstanceData(GShaderPlatformForFeatureLevel[FeatureLevel])
		)
	{
		FRHICommandListImmediate& RHICmdList = Context.RHICmdList;
		
		uint32 CPUInstancesCount = SourceParticleData->GetNumInstances();
	
		RayTracingInstance.NumTransforms = CPUInstancesCount;

		FRWBufferStructured InstanceGPUTransformsBuffer;
		//InstanceGPUTransformsBuffer.Initialize(sizeof(FMatrix), CPUInstancesCount, BUF_Static);
		InstanceGPUTransformsBuffer.Initialize(3 * 4 * sizeof(float), CPUInstancesCount, BUF_Static);
		RayTracingInstance.InstanceGPUTransformsSRV = InstanceGPUTransformsBuffer.SRV;

		FNiagaraGPURayTracingTransformsCS::FPermutationDomain PermutationVector;

		TShaderMapRef<FNiagaraGPURayTracingTransformsCS> GPURayTracingTransformsCS(GetGlobalShaderMap(FeatureLevel), PermutationVector);
		RHICmdList.SetComputeShader(GPURayTracingTransformsCS.GetComputeShader());

		const FUintVector4 NiagaraOffsets(
			VFVariables[ENiagaraMeshVFLayout::Position].GetGPUOffset(),
			VFVariables[ENiagaraMeshVFLayout::Transform].GetGPUOffset(),
			VFVariables[ENiagaraMeshVFLayout::Scale].GetGPUOffset(),
			bLocalSpace ? 1 : 0);

		uint32 FloatDataOffset = 0;
		uint32 FloatDataStride = SourceParticleData->GetFloatStride() / sizeof(float);

		GPURayTracingTransformsCS->SetParameters(
			RHICmdList, 
			CPUInstancesCount,
			SourceParticleData->GetGPUBufferFloat().SRV, 
			FloatDataOffset, 
			FloatDataStride, 
			SourceParticleData->GetGPUInstanceCountBufferOffset(),
			Batcher->GetGPUInstanceCounterManager().GetInstanceCountBuffer().SRV,
			NiagaraOffsets, 
			LocalTransform, 
			InstanceGPUTransformsBuffer.UAV);

		uint32 NGroups = FMath::DivideAndRoundUp(CPUInstancesCount, FNiagaraGPURayTracingTransformsCS::ThreadGroupSize);
		DispatchComputeShader(RHICmdList, GPURayTracingTransformsCS, NGroups, 1, 1);
		GPURayTracingTransformsCS->UnbindBuffers(RHICmdList);

		RHICmdList.Transition(FRHITransitionInfo(InstanceGPUTransformsBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute));
	}

	RayTracingInstance.BuildInstanceMaskAndFlags();
	OutRayTracingInstances.Add(RayTracingInstance);
}
#endif


FNiagaraDynamicDataBase* FNiagaraRendererMeshes::GenerateDynamicData(const FNiagaraSceneProxy* Proxy, const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitter) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderGT);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraGenMeshVertexData);

	const UNiagaraMeshRendererProperties* Properties = CastChecked<const UNiagaraMeshRendererProperties>(InProperties);

	if (Properties->ParticleMesh == nullptr)
	{
		return nullptr;
	}

	FNiagaraDynamicDataMesh* DynamicData = nullptr;
	FNiagaraDataBuffer* DataToRender = Emitter->GetData().GetCurrentData();
	if (DataToRender && MeshRenderData)
	{
		DynamicData = new FNiagaraDynamicDataMesh(Emitter);

		// We must use LOD 0 when setting up materials as this is the super set of materials
		// StaticMesh streaming will adjust LOD in a render command which can lead to differences in LOD selection between GT / RT
		const int32 LODIndex = 0;
		const FStaticMeshLODResources& LODModel = MeshRenderData->LODResources[LODIndex];

		check(BaseMaterials_GT.Num() == LODModel.Sections.Num());

		DynamicData->Materials.Reset(LODModel.Sections.Num());
		DynamicData->SetMaterialRelevance(BaseMaterialRelevance_GT);
		for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); SectionIndex++)
		{
			const FStaticMeshSection& Section = LODModel.Sections[SectionIndex];
			UMaterialInterface* SectionMat = nullptr;

			//In preparation for a material override feature, we pass our material(s) and relevance in via dynamic data.
			//The renderer ensures we have the correct usage and relevance for materials in BaseMaterials_GT.
			//Any override feature must also do the same for materials that are set.
			check(BaseMaterials_GT[SectionIndex]->CheckMaterialUsage_Concurrent(MATUSAGE_NiagaraMeshParticles));
			DynamicData->Materials.Add(BaseMaterials_GT[SectionIndex]->GetRenderProxy());
		}
	}

	return DynamicData;
}

int FNiagaraRendererMeshes::GetDynamicDataSize()const
{
	uint32 Size = sizeof(FNiagaraDynamicDataMesh);
	return Size;
}

bool FNiagaraRendererMeshes::IsMaterialValid(const UMaterialInterface* Mat)const
{
	return Mat && Mat->CheckMaterialUsage_Concurrent(MATUSAGE_NiagaraMeshParticles);
}


//////////////////////////////////////////////////////////////////////////
// Proposed class for ensuring Niagara/Cascade components who's proxies reference render data of other objects (Materials, Meshes etc) do not have data freed from under them.
// Our components register themselves with the referenced component which then calls InvalidateRenderDependencies() whenever it's render data is changed or when it is destroyed.
// UNTESTED - DO NOT USE.
struct FComponentRenderDependencyHandler
{
	void AddDependency(UPrimitiveComponent* Component)
	{
		DependentComponents.Add(Component);
	}

	void RemoveDependancy(UPrimitiveComponent* Component)
	{
		DependentComponents.RemoveSwap(Component);
	}

	void InvalidateRenderDependencies()
	{
		int32 i = DependentComponents.Num();
		while (--i >= 0)
		{
			if (UPrimitiveComponent* Comp = DependentComponents[i].Get())
			{
				Comp->MarkRenderStateDirty();
			}
			else
			{
				DependentComponents.RemoveAtSwap(i);
			}
		}
	}

	TArray<TWeakObjectPtr<UPrimitiveComponent>> DependentComponents;
};

//////////////////////////////////////////////////////////////////////////