// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshMaterialShader.h"
#include "ScenePrivate.h"
#include "RayTracingDynamicGeometryCollection.h"

#if RHI_RAYTRACING


static bool IsSupportedDynamicVertexFactoryType(const FVertexFactoryType* VertexFactoryType)
{
	return VertexFactoryType == FindVertexFactoryType(FName(TEXT("FNiagaraSpriteVertexFactory"), FNAME_Find))
		|| VertexFactoryType == FindVertexFactoryType(FName(TEXT("FNiagaraRibbonVertexFactory"), FNAME_Find))
		|| VertexFactoryType == FindVertexFactoryType(FName(TEXT("FLocalVertexFactory"), FNAME_Find))
		|| VertexFactoryType == FindVertexFactoryType(FName(TEXT("FLandscapeVertexFactory"), FNAME_Find))
		|| VertexFactoryType == FindVertexFactoryType(FName(TEXT("FLandscapeFixedGridVertexFactory"), FNAME_Find))
		|| VertexFactoryType == FindVertexFactoryType(FName(TEXT("FLandscapeXYOffsetVertexFactory"), FNAME_Find))
		|| VertexFactoryType == FindVertexFactoryType(FName(TEXT("FGPUSkinPassthroughVertexFactory"), FNAME_Find));
}

class FRayTracingDynamicGeometryConverterCS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FRayTracingDynamicGeometryConverterCS, MeshMaterial);
public:
	FRayTracingDynamicGeometryConverterCS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		PassUniformBuffer.Bind(Initializer.ParameterMap, FSceneTexturesUniformParameters::StaticStructMetadata.GetShaderVariableName());

		RWVertexPositions.Bind(Initializer.ParameterMap, TEXT("VertexPositions"));
		VertexBufferSize.Bind(Initializer.ParameterMap, TEXT("VertexBufferSize"));
		NumVertices.Bind(Initializer.ParameterMap, TEXT("NumVertices"));
		MinVertexIndex.Bind(Initializer.ParameterMap, TEXT("MinVertexIndex"));
		PrimitiveId.Bind(Initializer.ParameterMap, TEXT("PrimitiveId"));
	}

	FRayTracingDynamicGeometryConverterCS() = default;

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return IsSupportedDynamicVertexFactoryType(Parameters.VertexFactoryType) && ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FMeshMaterialShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);
	}

	void GetElementShaderBindings(
		const FShaderMapPointerTable& PointerTable,
		const FScene* Scene,
		const FSceneView* ViewIfDynamicMeshCommand,
		const FVertexFactory* VertexFactory,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMeshBatch& MeshBatch, 
		const FMeshBatchElement& BatchElement,
		const FMeshMaterialShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const
	{
		FMeshMaterialShader::GetElementShaderBindings(PointerTable, Scene, ViewIfDynamicMeshCommand, VertexFactory, InputStreamType, FeatureLevel, PrimitiveSceneProxy, MeshBatch, BatchElement, ShaderElementData, ShaderBindings, VertexStreams);
	}

	LAYOUT_FIELD(FRWShaderParameter, RWVertexPositions);
	LAYOUT_FIELD(FShaderParameter, VertexBufferSize);
	LAYOUT_FIELD(FShaderParameter, NumVertices);
	LAYOUT_FIELD(FShaderParameter, MinVertexIndex);
	LAYOUT_FIELD(FShaderParameter, PrimitiveId);
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FRayTracingDynamicGeometryConverterCS, TEXT("/Engine/Private/RayTracing/RayTracingDynamicMesh.usf"), TEXT("RayTracingDynamicGeometryConverterCS"), SF_Compute);

FRayTracingDynamicGeometryCollection::FRayTracingDynamicGeometryCollection()
{
	DispatchCommands = MakeUnique<TArray<FMeshComputeDispatchCommand>>();
}

void FRayTracingDynamicGeometryCollection::AddDynamicMeshBatchForGeometryUpdate(
	const FScene* Scene, 
	const FSceneView* View, 
	const FPrimitiveSceneProxy* PrimitiveSceneProxy, 
	FRayTracingDynamicGeometryUpdateParams UpdateParams,
	uint32 PrimitiveId
)
{
	FRayTracingGeometry& Geometry = *UpdateParams.Geometry;
	bool bUsingIndirectDraw = UpdateParams.bUsingIndirectDraw;
	uint32 NumMaxVertices = UpdateParams.NumVertices;
	FRWBuffer& Buffer = *UpdateParams.Buffer;

	for (const FMeshBatch& MeshBatch : UpdateParams.MeshBatches)
	{
		const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
		const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(Scene->GetFeatureLevel(), FallbackMaterialRenderProxyPtr);
		auto* MaterialInterface = Material.GetMaterialInterface();
		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;

		TMeshProcessorShaders<
			FMeshMaterialShader,
			FMeshMaterialShader,
			FMeshMaterialShader,
			FMeshMaterialShader,
			FMeshMaterialShader,
			FMeshMaterialShader,
			FRayTracingDynamicGeometryConverterCS> Shaders;

		FMeshComputeDispatchCommand DispatchCmd;

		TShaderRef<FRayTracingDynamicGeometryConverterCS> Shader = Material.GetShader<FRayTracingDynamicGeometryConverterCS>(MeshBatch.VertexFactory->GetType());
		DispatchCmd.MaterialShader = Shader;
		FMeshDrawShaderBindings& ShaderBindings = DispatchCmd.ShaderBindings;

		Shaders.ComputeShader = Shader;
		ShaderBindings.Initialize(Shaders.GetUntypedShaders());

		FMeshMaterialShaderElementData ShaderElementData;
		ShaderElementData.InitializeMeshMaterialData(View, PrimitiveSceneProxy, MeshBatch, -1, false);

		int32 DataOffset = 0;
		FMeshDrawSingleShaderBindings SingleShaderBindings = ShaderBindings.GetSingleShaderBindings(SF_Compute, DataOffset);
		FMeshPassProcessorRenderState DrawRenderState(Scene->UniformBuffers.ViewUniformBuffer, Scene->UniformBuffers.OpaqueBasePassUniformBuffer);
		Shader->GetShaderBindings(Scene, Scene->GetFeatureLevel(), PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, SingleShaderBindings);

		FVertexInputStreamArray DummyArray;
		FMeshMaterialShader::GetElementShaderBindings(Shader, Scene, View, MeshBatch.VertexFactory, EVertexInputStreamType::Default, Scene->GetFeatureLevel(), PrimitiveSceneProxy, MeshBatch, MeshBatch.Elements[0], ShaderElementData, SingleShaderBindings, DummyArray);

		DispatchCmd.TargetBuffer = &Buffer;
		DispatchCmd.NumMaxVertices = UpdateParams.NumVertices;
		DispatchCmd.NumCPUVertices = !bUsingIndirectDraw ? UpdateParams.NumVertices : 0;
		DispatchCmd.PrimitiveId = PrimitiveId;
		if (MeshBatch.Elements[0].MinVertexIndex < MeshBatch.Elements[0].MaxVertexIndex)
		{
			DispatchCmd.NumCPUVertices = MeshBatch.Elements[0].MaxVertexIndex - MeshBatch.Elements[0].MinVertexIndex;
		}
		DispatchCmd.MinVertexIndex = MeshBatch.Elements[0].MinVertexIndex;

#if MESH_DRAW_COMMAND_DEBUG_DATA
		FMeshProcessorShaders ShadersForDebug = Shaders.GetUntypedShaders();
		ShaderBindings.Finalize(&ShadersForDebug);
#endif

		DispatchCommands->Add(DispatchCmd);
	}

	bool bRefit = true;

	uint32 DesiredVertexBufferSize = UpdateParams.VertexBufferSize;
	if (Buffer.NumBytes != DesiredVertexBufferSize)
	{
		Buffer.Initialize(sizeof(float), DesiredVertexBufferSize / sizeof(float), PF_R32_FLOAT, BUF_UnorderedAccess | BUF_ShaderResource, TEXT("FRayTracingDynamicGeometryCollection::RayTracingDynamicVertexBuffer"));
		bRefit = false;
	}

	if (!Geometry.RayTracingGeometryRHI.IsValid())
	{
		bRefit = false;
	}

	if (!Geometry.Initializer.bAllowUpdate)
	{
		bRefit = false;
	}

	check(Geometry.IsInitialized());

	if (Geometry.Initializer.TotalPrimitiveCount != UpdateParams.NumTriangles)
	{
		check(Geometry.Initializer.Segments.Num() <= 1);
		Geometry.Initializer.TotalPrimitiveCount = UpdateParams.NumTriangles;
		Geometry.Initializer.Segments.Empty();
		FRayTracingGeometrySegment Segment;
		Segment.NumPrimitives = UpdateParams.NumTriangles;
		Geometry.Initializer.Segments.Add(Segment);
		bRefit = false;
	}

	for (FRayTracingGeometrySegment& Segment : Geometry.Initializer.Segments)
	{
		Segment.VertexBuffer = Buffer.Buffer;
	}

	if (!bRefit)
	{
		Geometry.RayTracingGeometryRHI = RHICreateRayTracingGeometry(Geometry.Initializer);
	}

	FAccelerationStructureBuildParams Params;
	Params.Geometry = Geometry.RayTracingGeometryRHI;
	Params.BuildMode = bRefit
		? EAccelerationStructureBuildMode::Update
		: EAccelerationStructureBuildMode::Build;
	BuildParams.Add(Params);
}

void FRayTracingDynamicGeometryCollection::DispatchUpdates(FRHIComputeCommandList& RHICmdList)
{
#if WANTS_DRAW_MESH_EVENTS
#define SCOPED_DRAW_OR_COMPUTE_EVENT(RHICmdList, Name) FDrawEvent PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents()) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(RHICmdList, FColor(0), TEXT(#Name));
#else
#define SCOPED_DRAW_OR_COMPUTE_EVENT(...)
#endif

	if (DispatchCommands->Num() > 0)
	{
		SCOPED_DRAW_OR_COMPUTE_EVENT(RHICmdList, RayTracingDynamicGeometryUpdate)

		{
			SCOPED_DRAW_OR_COMPUTE_EVENT(RHICmdList, VSinCSComputeDispatch)

			TArray<FRHIUnorderedAccessView*> BuffersToTransition;
			BuffersToTransition.Reserve(DispatchCommands->Num());

			for (FMeshComputeDispatchCommand& Cmd : *DispatchCommands)
			{
				BuffersToTransition.Add(Cmd.TargetBuffer->UAV.GetReference());
			}

			RHICmdList.TransitionResources(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EGfxToCompute, BuffersToTransition.GetData(), BuffersToTransition.Num());

			for (FMeshComputeDispatchCommand& Cmd : *DispatchCommands)
			{
				{
					const TShaderRef<FRayTracingDynamicGeometryConverterCS>& Shader = Cmd.MaterialShader;

					RHICmdList.SetComputeShader(Shader.GetComputeShader());

					Cmd.ShaderBindings.SetOnCommandList(RHICmdList, Shader.GetComputeShader());
					Shader->RWVertexPositions.SetBuffer(RHICmdList, Shader.GetComputeShader(), *Cmd.TargetBuffer);
					SetShaderValue(RHICmdList, Shader.GetComputeShader(), Shader->VertexBufferSize, Cmd.TargetBuffer->NumBytes / sizeof(FVector));
					SetShaderValue(RHICmdList, Shader.GetComputeShader(), Shader->NumVertices, Cmd.NumCPUVertices);
					SetShaderValue(RHICmdList, Shader.GetComputeShader(), Shader->MinVertexIndex, Cmd.MinVertexIndex);
					SetShaderValue(RHICmdList, Shader.GetComputeShader(), Shader->PrimitiveId, Cmd.PrimitiveId);
					RHICmdList.DispatchComputeShader(FMath::DivideAndRoundUp<uint32>(Cmd.NumMaxVertices, 64), 1, 1);

					Shader->RWVertexPositions.UnsetUAV(RHICmdList, Shader.GetComputeShader());
				}
			}

			RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, BuffersToTransition.GetData(), BuffersToTransition.Num());
		}

		SCOPED_DRAW_OR_COMPUTE_EVENT(RHICmdList, Build);
		RHICmdList.BuildAccelerationStructures(BuildParams);

		Clear();
	}
}

void FRayTracingDynamicGeometryCollection::Clear()
{
	DispatchCommands->Empty();
	BuildParams.Empty();
}

#endif // RHI_RAYTRACING
