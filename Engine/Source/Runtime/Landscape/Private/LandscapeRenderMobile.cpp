// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
LandscapeRenderMobile.cpp: Landscape Rendering without using vertex texture fetch
=============================================================================*/

#include "LandscapeRenderMobile.h"
#include "ShaderParameterUtils.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#include "PrimitiveSceneInfo.h"
#include "LandscapeLayerInfoObject.h"

void FLandscapeVertexFactoryMobile::InitRHI()
{
	// list of declaration items
	FVertexDeclarationElementList Elements;

	// position decls
	Elements.Add(AccessStreamComponent(MobileData.PositionComponent,0));

	if (MobileData.LODHeightsComponent.Num())
	{
		const int32 BaseAttribute = 1;
		for(int32 Index = 0;Index < MobileData.LODHeightsComponent.Num();Index++)
		{
			Elements.Add(AccessStreamComponent(MobileData.LODHeightsComponent[Index], BaseAttribute + Index));
		}
	}

	// create the actual device decls
	InitDeclaration(Elements);
}

/** Shader parameters for use with FLandscapeVertexFactory */
class FLandscapeVertexFactoryMobileVertexShaderParameters : public FVertexFactoryShaderParameters
{
public:
	/**
	* Bind shader constants by name
	* @param	ParameterMap - mapping of named shader constants to indices
	*/
	virtual void Bind(const FShaderParameterMap& ParameterMap) override
	{
		LodValuesParameter.Bind(ParameterMap,TEXT("LodValues"));
		LodTessellationParameter.Bind(ParameterMap, TEXT("LodTessellationParams"));
		NeighborSectionLodParameter.Bind(ParameterMap,TEXT("NeighborSectionLod"));
		LodBiasParameter.Bind(ParameterMap,TEXT("LodBias"));
		SectionLodsParameter.Bind(ParameterMap,TEXT("SectionLods"));
	}

	/**
	* Serialize shader params to an archive
	* @param	Ar - archive to serialize to
	*/
	virtual void Serialize(FArchive& Ar) override
	{
		Ar << LodValuesParameter;
		Ar << LodTessellationParameter;
		Ar << NeighborSectionLodParameter;
		Ar << LodBiasParameter;
		Ar << SectionLodsParameter;
	}
	/**
	* Set any shader data specific to this vertex factory
	*/
	virtual void SetMesh(FRHICommandList& RHICmdList, FShader* VertexShader,const class FVertexFactory* VertexFactory,const class FSceneView& View,const struct FMeshBatchElement& BatchElement,uint32 DataFlags) const override
	{
		SCOPE_CYCLE_COUNTER(STAT_LandscapeVFDrawTimeVS);

		const FLandscapeBatchElementParams* BatchElementParams = (const FLandscapeBatchElementParams*)BatchElement.UserData;
		check(BatchElementParams);

		const FLandscapeComponentSceneProxyMobile* SceneProxy = (const FLandscapeComponentSceneProxyMobile*)BatchElementParams->SceneProxy;
		SetUniformBufferParameter(RHICmdList, VertexShader->GetVertexShader(),VertexShader->GetUniformBufferParameter<FLandscapeUniformShaderParameters>(),*BatchElementParams->LandscapeUniformShaderParametersResource);

		if (LodValuesParameter.IsBound())
		{
			FVector4 LodValues(
				0.0f, // this is the mesh's LOD, ES2 always uses the LOD0 mesh
				0.0f, // unused
				(float)SceneProxy->SubsectionSizeQuads,
				1.f / (float)SceneProxy->SubsectionSizeQuads);

			SetShaderValue(RHICmdList, VertexShader->GetVertexShader(), LodValuesParameter, LodValues);
		}

		if (LodBiasParameter.IsBound())
		{
			FVector CameraLocalPos3D = SceneProxy->WorldToLocal.TransformPosition(View.ViewMatrices.GetViewOrigin());

			FVector4 LodBias(
				0.0f, // unused
				0.0f, // unused
				CameraLocalPos3D.X + SceneProxy->SectionBase.X,
				CameraLocalPos3D.Y + SceneProxy->SectionBase.Y
			);
			SetShaderValue(RHICmdList, VertexShader->GetVertexShader(), LodBiasParameter, LodBias);
		}

		FLandscapeComponentSceneProxy::FViewCustomDataLOD* LODData = (FLandscapeComponentSceneProxy::FViewCustomDataLOD*)View.GetCustomData(SceneProxy->GetPrimitiveSceneInfo()->GetIndex());
		int32 SubSectionIndex = BatchElementParams->SubX + BatchElementParams->SubY * SceneProxy->NumSubsections;

		if (LODData != nullptr)
		{
			if (LodTessellationParameter.IsBound())
			{
				SetShaderValue(RHICmdList, VertexShader->GetVertexShader(), LodTessellationParameter, LODData->LodTessellationParams);
			}

			if (SectionLodsParameter.IsBound())
			{
				if (LODData->UseCombinedMeshBatch)
				{
					SetShaderValue(RHICmdList, VertexShader->GetVertexShader(), SectionLodsParameter, LODData->ShaderCurrentLOD);
				}
				else // in non combined, only the one representing us as we'll be called 4 times (once per sub section)
				{
					check(SubSectionIndex >= 0);
					FVector4 ShaderCurrentLOD(ForceInitToZero);
					ShaderCurrentLOD.Component(SubSectionIndex) = LODData->ShaderCurrentLOD.Component(SubSectionIndex);

					SetShaderValue(RHICmdList, VertexShader->GetVertexShader(), SectionLodsParameter, ShaderCurrentLOD);
				}
			}

			if (NeighborSectionLodParameter.IsBound())
			{
				FVector4 ShaderCurrentNeighborLOD[FLandscapeComponentSceneProxy::NEIGHBOR_COUNT] = { FVector4(ForceInitToZero), FVector4(ForceInitToZero), FVector4(ForceInitToZero), FVector4(ForceInitToZero) };

				if (LODData->UseCombinedMeshBatch)
				{
					int32 SubSectionCount = SceneProxy->NumSubsections == 1 ? 1 : FLandscapeComponentSceneProxy::MAX_SUBSECTION_COUNT;

					for (int32 NeighborSubSectionIndex = 0; NeighborSubSectionIndex < SubSectionCount; ++NeighborSubSectionIndex)
					{
						ShaderCurrentNeighborLOD[NeighborSubSectionIndex] = LODData->SubSections[NeighborSubSectionIndex].ShaderCurrentNeighborLOD;
						check(ShaderCurrentNeighborLOD[NeighborSubSectionIndex].X != -1.0f); // they should all match so only check the 1st one for simplicity
					}

					SetShaderValue(RHICmdList, VertexShader->GetVertexShader(), NeighborSectionLodParameter, ShaderCurrentNeighborLOD);
				}
				else // in non combined, only the one representing us as we'll be called 4 times (once per sub section)
				{
					check(SubSectionIndex >= 0);
					ShaderCurrentNeighborLOD[SubSectionIndex] = LODData->SubSections[SubSectionIndex].ShaderCurrentNeighborLOD;
					check(ShaderCurrentNeighborLOD[SubSectionIndex].X != -1.0f); // they should all match so only check the 1st one for simplicity

					SetShaderValue(RHICmdList, VertexShader->GetVertexShader(), NeighborSectionLodParameter, ShaderCurrentNeighborLOD);
				}
			}
		}	
	}
protected:
	FShaderParameter LodValuesParameter;
	FShaderParameter LodTessellationParameter;
	FShaderParameter NeighborSectionLodParameter;
	FShaderParameter LodBiasParameter;
	FShaderParameter SectionLodsParameter;
	TShaderUniformBufferParameter<FLandscapeUniformShaderParameters> LandscapeShaderParameters;
};

/** Shader parameters for use with FLandscapeVertexFactory */
class FLandscapeVertexFactoryMobilePixelShaderParameters : public FLandscapeVertexFactoryPixelShaderParameters
{
public:
	/**
	* Bind shader constants by name
	* @param	ParameterMap - mapping of named shader constants to indices
	*/
	virtual void Bind(const FShaderParameterMap& ParameterMap) override
	{
		FLandscapeVertexFactoryPixelShaderParameters::Bind(ParameterMap);
		BlendableLayerMaskParameter.Bind(ParameterMap, TEXT("BlendableLayerMask"));
	}

	/**
	* Serialize shader params to an archive
	* @param	Ar - archive to serialize to
	*/
	virtual void Serialize(FArchive& Ar) override
	{
		FLandscapeVertexFactoryPixelShaderParameters::Serialize(Ar);
		Ar << BlendableLayerMaskParameter;
	}
	/**
	* Set any shader data specific to this vertex factory
	*/
	virtual void SetMesh(FRHICommandList& RHICmdList, FShader* PixelShader, const FVertexFactory* VertexFactory, const FSceneView& View, const FMeshBatchElement& BatchElement, uint32 DataFlags) const override
	{
		SCOPE_CYCLE_COUNTER(STAT_LandscapeVFDrawTimePS);

		FLandscapeVertexFactoryPixelShaderParameters::SetMesh(RHICmdList, PixelShader, VertexFactory, View, BatchElement, DataFlags);

		const FLandscapeBatchElementParams* BatchElementParams = (const FLandscapeBatchElementParams*)BatchElement.UserData;
		check(BatchElementParams);

		const FLandscapeComponentSceneProxyMobile* SceneProxy = (const FLandscapeComponentSceneProxyMobile*)BatchElementParams->SceneProxy;

		if (BlendableLayerMaskParameter.IsBound())
		{
			FVector MaskVector;
			MaskVector[0] = (SceneProxy->BlendableLayerMask & (1 << 0)) ? 1 : 0;
			MaskVector[1] = (SceneProxy->BlendableLayerMask & (1 << 1)) ? 1 : 0;
			MaskVector[2] = (SceneProxy->BlendableLayerMask & (1 << 2)) ? 1 : 0;
			SetShaderValue(RHICmdList, PixelShader->GetPixelShader(), BlendableLayerMaskParameter, MaskVector);
		}
	}
protected:
	FShaderParameter BlendableLayerMaskParameter;
};

FVertexFactoryShaderParameters* FLandscapeVertexFactoryMobile::ConstructShaderParameters(EShaderFrequency ShaderFrequency)
{
	switch( ShaderFrequency )
	{
	case SF_Vertex:
		return new FLandscapeVertexFactoryMobileVertexShaderParameters();
	case SF_Pixel:
		return new FLandscapeVertexFactoryMobilePixelShaderParameters();
	default:
		return NULL;
	}
}

IMPLEMENT_VERTEX_FACTORY_TYPE(FLandscapeVertexFactoryMobile, "/Engine/Private/LandscapeVertexFactory.ush", true, true, true, false, false);

/**
* Initialize the RHI for this rendering resource
*/
void FLandscapeVertexBufferMobile::InitRHI()
{
	// create a static vertex buffer
	FRHIResourceCreateInfo CreateInfo;
	void* VertexDataPtr = nullptr;
	VertexBufferRHI = RHICreateAndLockVertexBuffer(VertexData.Num(), BUF_Static, CreateInfo, VertexDataPtr);

	// Copy stored platform data and free CPU copy
	FMemory::Memcpy(VertexDataPtr, VertexData.GetData(), VertexData.Num());
	VertexData.Empty();

	RHIUnlockVertexBuffer(VertexBufferRHI);
}

/**
 * Container for FLandscapeVertexBufferMobile that we can reference from a thread-safe shared pointer
 * while ensuring the vertex buffer is always destroyed on the render thread.
 **/
struct FLandscapeMobileRenderData
{
	FLandscapeVertexBufferMobile* VertexBuffer;
	FOccluderVertexArraySP OccluderVerticesSP;

	FLandscapeMobileRenderData(const TArray<uint8>& InPlatformData)
	{
		FMemoryReader MemAr(InPlatformData);
		{
			int32 NumMobileVertices;
			TArray<uint8> MobileVerticesData;

			MemAr << NumMobileVertices;
			MobileVerticesData.SetNumUninitialized(NumMobileVertices*sizeof(FLandscapeMobileVertex));
			MemAr.Serialize(MobileVerticesData.GetData(), MobileVerticesData.Num());

			VertexBuffer = new FLandscapeVertexBufferMobile(MoveTemp(MobileVerticesData));
		}
		
		int32 NumOccluderVertices;
		MemAr << NumOccluderVertices;
		if (NumOccluderVertices > 0)
		{
			OccluderVerticesSP = MakeShared<FOccluderVertexArray, ESPMode::ThreadSafe>();
			OccluderVerticesSP->SetNumUninitialized(NumOccluderVertices);
			MemAr.Serialize(OccluderVerticesSP->GetData(), NumOccluderVertices*sizeof(FVector));

			INC_DWORD_STAT_BY(STAT_LandscapeOccluderMem, OccluderVerticesSP->GetAllocatedSize());
		}
	}

	~FLandscapeMobileRenderData()
	{
		// Make sure the vertex buffer is always destroyed from the render thread 
		if (VertexBuffer != nullptr)
		{
			if (IsInRenderingThread())
			{
				delete VertexBuffer;
			}
			else
			{
				ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER(
					InitCommand,
					FLandscapeVertexBufferMobile*, VertexBuffer, VertexBuffer,
					{
						delete VertexBuffer;
					});
			}
		}

		if (OccluderVerticesSP.IsValid())
		{
			DEC_DWORD_STAT_BY(STAT_LandscapeOccluderMem, OccluderVerticesSP->GetAllocatedSize());
		}
	}
};

FLandscapeComponentSceneProxyMobile::FLandscapeComponentSceneProxyMobile(ULandscapeComponent* InComponent)
	: FLandscapeComponentSceneProxy(InComponent)
	, MobileRenderData(InComponent->PlatformData.GetRenderData())
{
	check(InComponent);
	
	check(InComponent->MobileMaterialInterface);
	check(InComponent->MobileWeightmapTextures.Num() > 0);

	WeightmapTextures = InComponent->MobileWeightmapTextures;
	NormalmapTexture = InComponent->MobileWeightmapTextures[0];

	BlendableLayerMask = InComponent->MobileBlendableLayerMask;

#if WITH_EDITOR
	TArray<FWeightmapLayerAllocationInfo>& LayerAllocations = InComponent->MobileWeightmapLayerAllocations.Num() ? InComponent->MobileWeightmapLayerAllocations : InComponent->WeightmapLayerAllocations;
	LayerColors.Empty();
	for (auto& Allocation : LayerAllocations)
	{
		if (Allocation.LayerInfo != nullptr)
		{
			LayerColors.Add(Allocation.LayerInfo->LayerUsageDebugColor);
		}
	}
#endif
}

bool FLandscapeComponentSceneProxyMobile::CollectOccluderElements(FOccluderElementsCollector& Collector) const
{
	if (MobileRenderData->OccluderVerticesSP.IsValid() && SharedBuffers->OccluderIndicesSP.IsValid())
	{
		Collector.AddElements(MobileRenderData->OccluderVerticesSP, SharedBuffers->OccluderIndicesSP, GetLocalToWorld());
		return true;
	}

	return false;
}

FLandscapeComponentSceneProxyMobile::~FLandscapeComponentSceneProxyMobile()
{
	if (VertexFactory)
	{
		delete VertexFactory;
		VertexFactory = NULL;
	}
}

SIZE_T FLandscapeComponentSceneProxyMobile::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

void FLandscapeComponentSceneProxyMobile::CreateRenderThreadResources()
{
	if (IsComponentLevelVisible())
	{
		RegisterNeighbors();
	}
	
	auto FeatureLevel = GetScene().GetFeatureLevel();
	// Use only Index buffers
	SharedBuffers = FLandscapeComponentSceneProxy::SharedBuffersMap.FindRef(SharedBuffersKey);
	if (SharedBuffers == nullptr)
	{
		int32 NumOcclusionVertices = MobileRenderData->OccluderVerticesSP.IsValid() ? MobileRenderData->OccluderVerticesSP->Num() : 0;
				
		SharedBuffers = new FLandscapeSharedBuffers(
			SharedBuffersKey, SubsectionSizeQuads, NumSubsections,
			GetScene().GetFeatureLevel(), false, NumOcclusionVertices);

		FLandscapeComponentSceneProxy::SharedBuffersMap.Add(SharedBuffersKey, SharedBuffers);
	}

	SharedBuffers->AddRef();

	// Init vertex buffer
	check(MobileRenderData->VertexBuffer);
	MobileRenderData->VertexBuffer->InitResource();

	FLandscapeVertexFactoryMobile* LandscapeVertexFactory = new FLandscapeVertexFactoryMobile(FeatureLevel);
	LandscapeVertexFactory->MobileData.PositionComponent = FVertexStreamComponent(MobileRenderData->VertexBuffer, STRUCT_OFFSET(FLandscapeMobileVertex,Position), sizeof(FLandscapeMobileVertex), VET_UByte4N);
	for( uint32 Index = 0; Index < LANDSCAPE_MAX_ES_LOD_COMP; ++Index )
	{
		LandscapeVertexFactory->MobileData.LODHeightsComponent.Add
			(FVertexStreamComponent(MobileRenderData->VertexBuffer, STRUCT_OFFSET(FLandscapeMobileVertex,LODHeights) + sizeof(uint8) * 4 * Index, sizeof(FLandscapeMobileVertex), VET_UByte4N));
	}

	LandscapeVertexFactory->InitResource();
	VertexFactory = LandscapeVertexFactory;

	// Assign LandscapeUniformShaderParameters
	LandscapeUniformShaderParameters.InitResource();
}

TSharedPtr<FLandscapeMobileRenderData, ESPMode::ThreadSafe> FLandscapeComponentDerivedData::GetRenderData()
{
	check(IsInGameThread());

	if (FPlatformProperties::RequiresCookedData() && CachedRenderData.IsValid())
	{
		// on device we can re-use the cached data if we are re-registering our component.
		return CachedRenderData;
	}
	else
	{
		check(CompressedLandscapeData.Num() > 0);

		FMemoryReader Ar(CompressedLandscapeData);

		// Note: change LANDSCAPE_FULL_DERIVEDDATA_VER when modifying the serialization layout
		int32 UncompressedSize;
		Ar << UncompressedSize;

		int32 CompressedSize;
		Ar << CompressedSize;

		TArray<uint8> CompressedData;
		CompressedData.Empty(CompressedSize);
		CompressedData.AddUninitialized(CompressedSize);
		Ar.Serialize(CompressedData.GetData(), CompressedSize);

		TArray<uint8> UncompressedData;
		UncompressedData.Empty(UncompressedSize);
		UncompressedData.AddUninitialized(UncompressedSize);

		verify(FCompression::UncompressMemory((ECompressionFlags)COMPRESS_ZLIB, UncompressedData.GetData(), UncompressedSize, CompressedData.GetData(), CompressedSize));

		TSharedPtr<FLandscapeMobileRenderData, ESPMode::ThreadSafe> RenderData = MakeShareable(new FLandscapeMobileRenderData(MoveTemp(UncompressedData)));

		// if running on device		
		if (FPlatformProperties::RequiresCookedData())
		{
			// free the compressed data now that we have used it to create the render data.
			CompressedLandscapeData.Empty();
			// store a reference to the render data so we can use it again should the component be reregistered.
			CachedRenderData = RenderData;
		}

		return RenderData;
	}
}