// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ConvertToUniformMesh.cpp
=============================================================================*/

#include "CoreMinimal.h"
#include "RHI.h"
#include "ShaderParameters.h"
#include "Shader.h"
#include "MeshBatch.h"
#include "MaterialShaderType.h"
#include "MaterialShader.h"
#include "MeshMaterialShader.h"
#include "ScenePrivate.h"
#include "DistanceFieldLightingShared.h"
#include "MeshPassProcessor.inl"
#include "MaterialShared.h"

class FConvertToUniformMeshVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FConvertToUniformMeshVS,MeshMaterial);

protected:

	FConvertToUniformMeshVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		:	FMeshMaterialShader(Initializer)
	{
		PassUniformBuffer.Bind(Initializer.ParameterMap, FSceneTexturesUniformParameters::StaticStructMetadata.GetShaderVariableName());
	}

	FConvertToUniformMeshVS()
	{
	}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) 
			&& DoesPlatformSupportDistanceFieldGI(Parameters.Platform)
			&& (FCString::Strstr(Parameters.VertexFactoryType->GetName(), TEXT("LocalVertexFactory")) != NULL
				|| FCString::Strstr(Parameters.VertexFactoryType->GetName(), TEXT("InstancedStaticMeshVertexFactory")) != NULL);
	}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(,FConvertToUniformMeshVS,TEXT("/Engine/Private/ConvertToUniformMesh.usf"),TEXT("ConvertToUniformMeshVS"),SF_Vertex); 

void GetUniformMeshStreamOutLayout(FStreamOutElementList& Layout)
{
	Layout.Add(FStreamOutElement(0, "SV_Position", 0, 4, 0));
	Layout.Add(FStreamOutElement(0, "Tangent", 0, 3, 0));
	Layout.Add(FStreamOutElement(0, "Tangent", 1, 3, 0));
	Layout.Add(FStreamOutElement(0, "Tangent", 2, 3, 0));
	Layout.Add(FStreamOutElement(0, "UV", 0, 2, 0));
	Layout.Add(FStreamOutElement(0, "UV", 1, 2, 0));
	Layout.Add(FStreamOutElement(0, "VertexColor", 0, 4, 0));
}

// In float4's, must match usf
int32 FSurfelBuffers::InterpolatedVertexDataStride = 6;

/** Returns number of float's in the uniform vertex. */
int32 ComputeUniformVertexStride()
{
	FStreamOutElementList Layout;
	int32 StreamStride = 0;

	GetUniformMeshStreamOutLayout(Layout);

	for (int32 ElementIndex = 0; ElementIndex < Layout.Num(); ElementIndex++)
	{
		StreamStride += Layout[ElementIndex].ComponentCount;
	}

	// D3D11 stream out buffer element stride must be a factor of 4
	return FMath::DivideAndRoundUp(StreamStride, 4) * 4;
}

void FUniformMeshBuffers::Initialize()
{
	if (MaxElements > 0)
	{
		const int32 VertexStride = ComputeUniformVertexStride();
		FRHIResourceCreateInfo CreateInfo;
		TriangleData = RHICreateVertexBuffer(MaxElements * VertexStride * GPixelFormats[PF_R32_FLOAT].BlockBytes, BUF_ShaderResource | BUF_StreamOutput, CreateInfo);
		TriangleDataSRV = RHICreateShaderResourceView(TriangleData, GPixelFormats[PF_R32_FLOAT].BlockBytes, PF_R32_FLOAT);

		TriangleAreas.Initialize(sizeof(float), MaxElements, PF_R32_FLOAT);
		TriangleCDFs.Initialize(sizeof(float), MaxElements, PF_R32_FLOAT);
	}
}

class FConvertToUniformMeshGS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FConvertToUniformMeshGS,MeshMaterial);

protected:

	FConvertToUniformMeshGS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		:	FMeshMaterialShader(Initializer)
	{
		PassUniformBuffer.Bind(Initializer.ParameterMap, FSceneTexturesUniformParameters::StaticStructMetadata.GetShaderVariableName());
	}

	FConvertToUniformMeshGS()
	{
	}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) 
			&& DoesPlatformSupportDistanceFieldGI(Parameters.Platform)
			&& (FCString::Strstr(Parameters.VertexFactoryType->GetName(), TEXT("LocalVertexFactory")) != NULL
				|| FCString::Strstr(Parameters.VertexFactoryType->GetName(), TEXT("InstancedStaticMeshVertexFactory")) != NULL);
	}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(,FConvertToUniformMeshGS,TEXT("/Engine/Private/ConvertToUniformMesh.usf"),TEXT("ConvertToUniformMeshGS"),SF_Geometry);

class FConvertToUniformMeshProcessor : public FMeshPassProcessor
{
public:
	FConvertToUniformMeshProcessor(const FScene* Scene, const FViewInfo* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext);

	void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final;

private:
	void Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		ERasterizerFillMode MeshFillMod1e,
		ERasterizerCullMode MeshCullMode);

	FMeshPassProcessorRenderState PassDrawRenderState;
};

FConvertToUniformMeshProcessor::FConvertToUniformMeshProcessor(const FScene* Scene, const FViewInfo* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
	: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
{
	PassDrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());
	PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());

	PassDrawRenderState.SetViewUniformBuffer(Scene->UniformBuffers.ViewUniformBuffer);
	PassDrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);
	PassDrawRenderState.SetPassUniformBuffer(Scene->UniformBuffers.ConvertToUniformMeshPassUniformBuffer);
}

void FConvertToUniformMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
	const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);

	const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;

	const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
	const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material, OverrideSettings);
	const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, Material, OverrideSettings);

	Process(MeshBatch, BatchElementMask, PrimitiveSceneProxy, MaterialRenderProxy, Material, MeshFillMode, MeshCullMode);
}

void FConvertToUniformMeshProcessor::Process(
	const FMeshBatch& MeshBatch,
	uint64 BatchElementMask,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	TMeshProcessorShaders<
		FConvertToUniformMeshVS,
		FMeshMaterialShader,
		FMeshMaterialShader,
		FMeshMaterialShader,
		FConvertToUniformMeshGS> PassShaders;

	PassShaders.VertexShader = MaterialResource.GetShader<FConvertToUniformMeshVS>(VertexFactory->GetType());
	PassShaders.GeometryShader = MaterialResource.GetShader<FConvertToUniformMeshGS>(VertexFactory->GetType());

	FMeshMaterialShaderElementData ShaderElementData;
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, -1, true);

	const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(PassShaders.VertexShader, PassShaders.PixelShader);

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		PassDrawRenderState,
		PassShaders,
		MeshFillMode,
		MeshCullMode,
		SortKey,
		EMeshPassFeatures::Default,
		ShaderElementData);
}

bool ShouldGenerateSurfelsOnMesh(const FMeshBatch& Mesh, ERHIFeatureLevel::Type FeatureLevel)
{
	//@todo - support for tessellated meshes
	return Mesh.Type == PT_TriangleList 
		&& !Mesh.IsTranslucent(FeatureLevel) 
		&& Mesh.MaterialRenderProxy->GetMaterial(FeatureLevel)->GetShadingModels().IsLit();
}

bool ShouldConvertMesh(const FMeshBatch& Mesh)
{
	return Mesh.Type == PT_TriangleList
		//@todo - import types and compare directly
		&& (FCString::Strstr(Mesh.VertexFactory->GetType()->GetName(), TEXT("LocalVertexFactory")) != NULL
			|| FCString::Strstr(Mesh.VertexFactory->GetType()->GetName(), TEXT("InstancedStaticMeshVertexFactory")) != NULL);
}

FUniformMeshBuffers GUniformMeshTemporaryBuffers;

int32 FUniformMeshConverter::Convert(
	FRHICommandListImmediate& RHICmdList, 
	FSceneRenderer& Renderer,
	FViewInfo& View, 
	const FPrimitiveSceneInfo* PrimitiveSceneInfo, 
	int32 LODIndex,
	FUniformMeshBuffers*& OutUniformMeshBuffers,
	const FMaterialRenderProxy*& OutMaterialRenderProxy,
	FRHIUniformBuffer*& OutPrimitiveUniformBuffer)
{
	const FPrimitiveSceneProxy* PrimitiveSceneProxy = PrimitiveSceneInfo->Proxy;
	const auto FeatureLevel = View.GetFeatureLevel();

	TArray<FMeshBatch> MeshElements;
	PrimitiveSceneInfo->Proxy->GetMeshDescription(LODIndex, MeshElements);

	int32 NumTriangles = 0;

	for (int32 MeshIndex = 0; MeshIndex < MeshElements.Num(); MeshIndex++)
	{
		if (ShouldConvertMesh(MeshElements[MeshIndex]))
		{
			NumTriangles += MeshElements[MeshIndex].GetNumPrimitives();
		}
	}
	
	if (NumTriangles > 0)
	{
		if (GUniformMeshTemporaryBuffers.MaxElements < NumTriangles * 3)
		{
			GUniformMeshTemporaryBuffers.MaxElements = NumTriangles * 3;
			GUniformMeshTemporaryBuffers.Release();
			GUniformMeshTemporaryBuffers.Initialize();
		}

		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		UnbindRenderTargets(RHICmdList);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS

		uint32 Offsets[1] = {0};
		FRHIVertexBuffer* const StreamOutTargets[1] = {GUniformMeshTemporaryBuffers.TriangleData.GetReference()};
		//#todo-RemoveStreamOut
		checkf(0, TEXT("SetStreamOutTargets() is not supported"));
		//RHICmdList.SetStreamOutTargets(1, StreamOutTargets, Offsets);

		for (int32 MeshIndex = 0; MeshIndex < MeshElements.Num(); MeshIndex++)
		{
			const FMeshBatch& Mesh = MeshElements[MeshIndex];

			if (ShouldConvertMesh(Mesh))
			{
				//@todo - fix
				OutMaterialRenderProxy = Mesh.MaterialRenderProxy;
				OutPrimitiveUniformBuffer = Mesh.Elements.Num() > 0 ? Mesh.Elements[0].PrimitiveUniformBuffer : nullptr;

				DrawDynamicMeshPass(View, RHICmdList,
					[&View, &RHICmdList, &Mesh, &PrimitiveSceneProxy](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
				{
					FConvertToUniformMeshProcessor PassMeshProcessor(
						View.Family->Scene->GetRenderScene(),
						&View,
						DynamicMeshPassContext);

					const uint64 DefaultBatchElementMask = ~0ull;
					PassMeshProcessor.AddMeshBatch(Mesh, DefaultBatchElementMask, PrimitiveSceneProxy);
				});
			}
		}

		//#todo-RemoveStreamOut
		checkf(0, TEXT("SetStreamOutTargets() is not supported"));
		//RHICmdList.SetStreamOutTargets(1, nullptr, Offsets);
	}

	OutUniformMeshBuffers = &GUniformMeshTemporaryBuffers;
	return NumTriangles;
}

int32 GEvaluateSurfelMaterialGroupSize = 64;

class FEvaluateSurfelMaterialCS : public FMaterialShader
{
	DECLARE_SHADER_TYPE(FEvaluateSurfelMaterialCS,Material)
public:

	static bool ShouldCompilePermutation(const FMaterialShaderPermutationParameters& Parameters)
	{
		if (Parameters.MaterialParameters.MaterialDomain == MD_UI)
		{
			return false;
		}

		if (Parameters.MaterialParameters.ShadingModels.IsUnlit())
		{
			return false;
		}

		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("EVALUATE_SURFEL_MATERIAL_GROUP_SIZE"), GEvaluateSurfelMaterialGroupSize);
		OutEnvironment.SetDefine(TEXT("HAS_PRIMITIVE_UNIFORM_BUFFER"), 1);
	}

	FEvaluateSurfelMaterialCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMaterialShader(Initializer)
	{
		SurfelBufferParameters.Bind(Initializer.ParameterMap);
		SurfelStartIndex.Bind(Initializer.ParameterMap, TEXT("SurfelStartIndex"));
		NumSurfelsToGenerate.Bind(Initializer.ParameterMap, TEXT("NumSurfelsToGenerate"));
		Instance0InverseTransform.Bind(Initializer.ParameterMap, TEXT("Instance0InverseTransform"));
	}

	FEvaluateSurfelMaterialCS()
	{
	}

	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FSceneView& View, 
		int32 SurfelStartIndexValue,
		int32 NumSurfelsToGenerateValue,
		const FMaterialRenderProxy* MaterialProxy,
		FRHIUniformBuffer* PrimitiveUniformBuffer,
		const FMatrix& Instance0Transform
		)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();
		FMaterialShader::SetParameters(RHICmdList, ShaderRHI, MaterialProxy, *MaterialProxy->GetMaterial(View.GetFeatureLevel()), View, View.ViewUniformBuffer, ESceneTextureSetupMode::None);

		SetUniformBufferParameter(RHICmdList, ShaderRHI,GetUniformBufferParameter<FPrimitiveUniformShaderParameters>(),PrimitiveUniformBuffer);

		const FScene* Scene = (const FScene*)View.Family->Scene;

		FRHIUnorderedAccessView* UniformMeshUAVs[1];
		UniformMeshUAVs[0] = Scene->DistanceFieldSceneData.SurfelBuffers->Surfels.UAV;
		RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, UniformMeshUAVs, UE_ARRAY_COUNT(UniformMeshUAVs));

		SurfelBufferParameters.Set(RHICmdList, ShaderRHI, *Scene->DistanceFieldSceneData.SurfelBuffers, *Scene->DistanceFieldSceneData.InstancedSurfelBuffers);
		
		SetShaderValue(RHICmdList, ShaderRHI, SurfelStartIndex, SurfelStartIndexValue);
		SetShaderValue(RHICmdList, ShaderRHI, NumSurfelsToGenerate, NumSurfelsToGenerateValue);

		SetShaderValue(RHICmdList, ShaderRHI, Instance0InverseTransform, Instance0Transform.Inverse());
	}

	void UnsetParameters(FRHICommandList& RHICmdList, FViewInfo& View)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();
		SurfelBufferParameters.UnsetParameters(RHICmdList, ShaderRHI);

		const FScene* Scene = (const FScene*)View.Family->Scene;
		FRHIUnorderedAccessView* UniformMeshUAVs[1];
		UniformMeshUAVs[0] = Scene->DistanceFieldSceneData.SurfelBuffers->Surfels.UAV;
		RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, UniformMeshUAVs, UE_ARRAY_COUNT(UniformMeshUAVs));
	}

private:

	LAYOUT_FIELD(FSurfelBufferParameters, SurfelBufferParameters);
	LAYOUT_FIELD(FShaderParameter, SurfelStartIndex);
	LAYOUT_FIELD(FShaderParameter, NumSurfelsToGenerate);
	LAYOUT_FIELD(FShaderParameter, Instance0InverseTransform);
};

IMPLEMENT_MATERIAL_SHADER_TYPE(,FEvaluateSurfelMaterialCS,TEXT("/Engine/Private/EvaluateSurfelMaterial.usf"),TEXT("EvaluateSurfelMaterialCS"),SF_Compute);

void FUniformMeshConverter::GenerateSurfels(
	FRHICommandListImmediate& RHICmdList, 
	FViewInfo& View, 
	const FPrimitiveSceneInfo* PrimitiveSceneInfo, 
	const FMaterialRenderProxy* MaterialProxy,
	FRHIUniformBuffer* PrimitiveUniformBuffer,
	const FMatrix& Instance0Transform,
	int32 SurfelOffset,
	int32 NumSurfels)
{
	const FMaterial* Material = MaterialProxy->GetMaterial(View.GetFeatureLevel());
	const FMaterialShaderMap* MaterialShaderMap = Material->GetRenderingThreadShaderMap();
	TShaderRef<FEvaluateSurfelMaterialCS> ComputeShader = MaterialShaderMap->GetShader<FEvaluateSurfelMaterialCS>();

	RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());
	ComputeShader->SetParameters(RHICmdList, View, SurfelOffset, NumSurfels, MaterialProxy, PrimitiveUniformBuffer, Instance0Transform);
	DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), FMath::DivideAndRoundUp(NumSurfels, GEvaluateSurfelMaterialGroupSize), 1, 1);

	ComputeShader->UnsetParameters(RHICmdList, View);
}
