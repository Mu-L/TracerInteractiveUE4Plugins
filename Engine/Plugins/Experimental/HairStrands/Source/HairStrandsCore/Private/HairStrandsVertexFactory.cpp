// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	StrandHairVertexFactory.cpp: Strand hair vertex factory implementation
=============================================================================*/

#include "HairStrandsVertexFactory.h"
#include "SceneView.h"
#include "MeshBatch.h"
#include "ShaderParameterUtils.h"
#include "Rendering/ColorVertexBuffer.h"
#include "MeshMaterialShader.h"
#include "HairStrandsInterface.h"

/////////////////////////////////////////////////////////////////////////////////////////

template<typename T> inline void BindParam(FMeshDrawSingleShaderBindings& ShaderBindings, const FShaderResourceParameter& Param, T* Value)	{ if (Param.IsBound() && Value) ShaderBindings.Add(Param, Value); }
template<typename T> inline void BindParam(FMeshDrawSingleShaderBindings& ShaderBindings, const FShaderParameter& Param, const T& Value)	{ if (Param.IsBound()) ShaderBindings.Add(Param, Value); }

class FDummyCulledDispatchVertexIdsBuffer : public FVertexBuffer
{
public:
	FShaderResourceViewRHIRef SRVUint;
	FShaderResourceViewRHIRef SRVFloat;

	virtual void InitRHI() override
	{
		FRHIResourceCreateInfo CreateInfo;
		void* BufferData = nullptr;
		uint32 NumBytes = sizeof(uint32) * 4;
		VertexBufferRHI = RHICreateAndLockVertexBuffer(NumBytes, BUF_Static | BUF_ShaderResource, CreateInfo, BufferData);
		uint32* DummyContents = (uint32*)BufferData;
		DummyContents[0] = DummyContents[1] = DummyContents[2] = DummyContents[3] = 0;
		RHIUnlockVertexBuffer(VertexBufferRHI);

		SRVUint = RHICreateShaderResourceView(VertexBufferRHI, sizeof(uint32), PF_R32_UINT);
		SRVFloat = RHICreateShaderResourceView(VertexBufferRHI, sizeof(uint32), PF_R32_FLOAT);
	}

};
TGlobalResource<FDummyCulledDispatchVertexIdsBuffer> GDummyCulledDispatchVertexIdsBuffer;

class FHairStrandsVertexFactoryShaderParameters : public FVertexFactoryShaderParameters
{
	DECLARE_TYPE_LAYOUT(FHairStrandsVertexFactoryShaderParameters, NonVirtual);
public:

	LAYOUT_FIELD(FShaderParameter, Radius);
	LAYOUT_FIELD(FShaderParameter, Length);
	LAYOUT_FIELD(FShaderParameter, RadiusAtDepth1_Primary);	// unused
	LAYOUT_FIELD(FShaderParameter, RadiusAtDepth1_Velocity);	// unused
	LAYOUT_FIELD(FShaderParameter, PositionOffset);
	LAYOUT_FIELD(FShaderParameter, PreviousPositionOffset);
	LAYOUT_FIELD(FShaderParameter, Density);
	LAYOUT_FIELD(FShaderParameter, Culling);
	LAYOUT_FIELD(FShaderParameter, StableRasterization);
	LAYOUT_FIELD(FShaderParameter, ScatterSceneLighing);

	LAYOUT_FIELD(FShaderResourceParameter, PositionBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, PreviousPositionBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, AttributeBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, MaterialBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, TangentBuffer);

	LAYOUT_FIELD(FShaderResourceParameter, CulledVertexIdsBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, CulledVertexRadiusScaleBuffer);

	void Bind(const FShaderParameterMap& ParameterMap)
	{
		Radius.Bind(ParameterMap, TEXT("HairStrandsVF_Radius"));
		Length.Bind(ParameterMap, TEXT("HairStrandsVF_Length"));
		PositionOffset.Bind(ParameterMap, TEXT("HairStrandsVF_PositionOffset"));
		PreviousPositionOffset.Bind(ParameterMap, TEXT("HairStrandsVF_PreviousPositionOffset"));
		Density.Bind(ParameterMap, TEXT("HairStrandsVF_Density"));	
		Culling.Bind(ParameterMap, TEXT("HairStrandsVF_CullingEnable"));
		StableRasterization.Bind(ParameterMap, TEXT("HairStrandsVF_bUseStableRasterization"));
		ScatterSceneLighing.Bind(ParameterMap, TEXT("HairStrandsVF_bScatterSceneLighing"));

		PositionBuffer.Bind(ParameterMap, TEXT("HairStrandsVF_PositionBuffer"));
		PreviousPositionBuffer.Bind(ParameterMap, TEXT("HairStrandsVF_PreviousPositionBuffer"));
		AttributeBuffer.Bind(ParameterMap, TEXT("HairStrandsVF_AttributeBuffer"));
		MaterialBuffer.Bind(ParameterMap, TEXT("HairStrandsVF_MaterialBuffer"));
		TangentBuffer.Bind(ParameterMap, TEXT("HairStrandsVF_TangentBuffer"));

		CulledVertexIdsBuffer.Bind(ParameterMap, TEXT("CulledVertexIdsBuffer"));
		CulledVertexRadiusScaleBuffer.Bind(ParameterMap, TEXT("CulledVertexRadiusScaleBuffer"));
	}

	void GetElementShaderBindings(
		const FSceneInterface* Scene,
		const FSceneView* View,
		const FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams
	) const
	{
		const FHairStrandsVertexFactory* VF = static_cast<const FHairStrandsVertexFactory*>(VertexFactory);

		const FHairGroupPublicData* GroupPublicData = reinterpret_cast<const FHairGroupPublicData*>(BatchElement.VertexFactoryUserData);
		check(GroupPublicData);
		const uint64 GroupIndex = GroupPublicData->GetGroupIndex();
		BindParam(ShaderBindings, PositionBuffer, VF->GetPositionSRV(GroupIndex));
		BindParam(ShaderBindings, PreviousPositionBuffer, VF->GetPreviousPositionSRV(GroupIndex));
		BindParam(ShaderBindings, AttributeBuffer, VF->GetAttributeSRV(GroupIndex));
		BindParam(ShaderBindings, MaterialBuffer, VF->GetMaterialSRV(GroupIndex));
		BindParam(ShaderBindings, TangentBuffer, VF->GetTangentSRV(GroupIndex));
		BindParam(ShaderBindings, Radius, VF->GetMaxStrandRadius(GroupIndex));
		BindParam(ShaderBindings, Length, VF->GetMaxStrandLength(GroupIndex));
		BindParam(ShaderBindings, PositionOffset, VF->GetPositionOffset(GroupIndex));
		BindParam(ShaderBindings, PreviousPositionOffset, VF->GetPreviousPositionOffset(GroupIndex));
		BindParam(ShaderBindings, Density, VF->GetHairDensity(GroupIndex));
		BindParam(ShaderBindings, StableRasterization, VF->UseStableRasterization(GroupIndex) ? 1u : 0u);
		BindParam(ShaderBindings, ScatterSceneLighing, VF->UseScatterSceneLighting(GroupIndex) ? 1u : 0u);
		
		FShaderResourceViewRHIRef CulledDispatchVertexIdsSRV = GDummyCulledDispatchVertexIdsBuffer.SRVUint;
		FShaderResourceViewRHIRef CulledCompactedRadiusScaleBufferSRV = GDummyCulledDispatchVertexIdsBuffer.SRVFloat;

		FHairGroupPublicData* PublicGroupData = VF->GetHairGroupPublicData(GroupIndex);
		
		const bool bCulling = PublicGroupData ? PublicGroupData->GetCullingResultAvailable() : false;
		if (bCulling && PublicGroupData)
		{
			CulledDispatchVertexIdsSRV = PublicGroupData->GetCulledVertexIdBuffer().SRV;
			CulledCompactedRadiusScaleBufferSRV = PublicGroupData->GetCulledVertexRadiusScaleBuffer().SRV;
		}

		BindParam(ShaderBindings, Culling, bCulling ? 1 : 0);
		ShaderBindings.Add(CulledVertexIdsBuffer, CulledDispatchVertexIdsSRV);
		ShaderBindings.Add(CulledVertexRadiusScaleBuffer, CulledCompactedRadiusScaleBufferSRV);
	}
};

IMPLEMENT_TYPE_LAYOUT(FHairStrandsVertexFactoryShaderParameters);

/**
 * Should we cache the material's shadertype on this platform with this vertex factory? 
 */
bool FHairStrandsVertexFactory::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	return (Parameters.MaterialParameters.MaterialDomain == MD_Surface && Parameters.MaterialParameters.bIsUsedWithHairStrands && Parameters.Platform == EShaderPlatform::SP_PCD3D_SM5) || Parameters.MaterialParameters.bIsSpecialEngineMaterial;
}

void FHairStrandsVertexFactory::ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	const bool bUseGPUSceneAndPrimitiveIdStream = Parameters.VertexFactoryType->SupportsPrimitiveIdStream() && UseGPUScene(Parameters.Platform, GetMaxSupportedFeatureLevel(Parameters.Platform));
	OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), bUseGPUSceneAndPrimitiveIdStream);
	OutEnvironment.SetDefine(TEXT("VF_STRAND_HAIR"), TEXT("1"));
	OutEnvironment.SetDefine(TEXT("VF_GPU_SCENE_TEXTURE"), bUseGPUSceneAndPrimitiveIdStream && GPUSceneUseTexture2D(Parameters.Platform));
}

void FHairStrandsVertexFactory::ValidateCompiledResult(const FVertexFactoryType* Type, EShaderPlatform Platform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors)
{
	if (Type->SupportsPrimitiveIdStream() 
		&& UseGPUScene(Platform, GetMaxSupportedFeatureLevel(Platform)) 
		&& ParameterMap.ContainsParameterAllocation(FPrimitiveUniformShaderParameters::StaticStructMetadata.GetShaderVariableName()))
	{
		OutErrors.AddUnique(*FString::Printf(TEXT("Shader attempted to bind the Primitive uniform buffer even though Vertex Factory %s computes a PrimitiveId per-instance.  This will break auto-instancing.  Shaders should use GetPrimitiveData(PrimitiveId).Member instead of Primitive.Member."), Type->GetName()));
	}
}

void FHairStrandsVertexFactory::SetData(const FDataType& InData)
{
	check(IsInRenderingThread());
	Data = InData;
	UpdateRHI();
}

/**
* Copy the data from another vertex factory
* @param Other - factory to copy from
*/
void FHairStrandsVertexFactory::Copy(const FHairStrandsVertexFactory& Other)
{
	FHairStrandsVertexFactory* VertexFactory = this;
	const FDataType* DataCopy = &Other.Data;
	ENQUEUE_RENDER_COMMAND(FHairStrandsVertexFactoryCopyData)(
		[VertexFactory, DataCopy](FRHICommandListImmediate& RHICmdList)
		{
			VertexFactory->Data = *DataCopy;
		});
	BeginUpdateResourceRHI(this);
}

void FHairStrandsVertexFactory::InitRHI()
{
	bNeedsDeclaration = false;
	bSupportsManualVertexFetch = true;

	// We create different streams based on feature level
	check(HasValidFeatureLevel());

	// VertexFactory needs to be able to support max possible shader platform and feature level
	// in case if we switch feature level at runtime.
	const bool bCanUseGPUScene = UseGPUScene(GMaxRHIShaderPlatform, GMaxRHIFeatureLevel);

	FVertexDeclarationElementList Elements;
	SetPrimitiveIdStreamIndex(EVertexInputStreamType::Default, -1);
	if (GetType()->SupportsPrimitiveIdStream() && bCanUseGPUScene)
	{
		// When the VF is used for rendering in normal mesh passes, this vertex buffer and offset will be overridden
		Elements.Add(AccessStreamComponent(FVertexStreamComponent(&GPrimitiveIdDummy, 0, 0, sizeof(uint32), VET_UInt, EVertexStreamUsage::Instancing), 13));
		SetPrimitiveIdStreamIndex(EVertexInputStreamType::Default, Elements.Last().StreamIndex);
		bNeedsDeclaration = true;
	}

	check(Streams.Num() > 0);

	InitDeclaration(Elements);
	check(IsValidRef(GetDeclaration()));
}

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FHairStrandsVertexFactory, SF_Vertex,		FHairStrandsVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FHairStrandsVertexFactory, SF_Pixel,		FHairStrandsVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FHairStrandsVertexFactory, SF_Compute,		FHairStrandsVertexFactoryShaderParameters);
#if RHI_RAYTRACING
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FHairStrandsVertexFactory, SF_RayHitGroup,	FHairStrandsVertexFactoryShaderParameters);
#endif

void FHairStrandsVertexFactory::ReleaseRHI()
{
	FVertexFactory::ReleaseRHI();
}

IMPLEMENT_VERTEX_FACTORY_TYPE_EX(FHairStrandsVertexFactory,"/Engine/Private/HairStrands/HairStrandsVertexFactory.ush",true,false,true,true,true,true,true);
