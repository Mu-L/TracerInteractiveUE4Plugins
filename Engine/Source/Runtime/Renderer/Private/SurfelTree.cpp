// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SurfelTree.cpp
=============================================================================*/

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "ShaderParameters.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "ScenePrivate.h"
#include "DistanceFieldLightingShared.h"

float GSurfelDensity = .05f;
FAutoConsoleVariableRef CVarSurfelDensity(
	TEXT("r.SurfelDensity"),
	GSurfelDensity,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

int32 GMaxSurfelsPerObject = 10000;
FAutoConsoleVariableRef CVarMaxSurfelsPerObject(
	TEXT("r.SurfelMaxPerObject"),
	GMaxSurfelsPerObject,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

float GSurfelLODDensityFraction = .2f;
FAutoConsoleVariableRef CVarSurfelLODDensityFraction(
	TEXT("r.SurfelLODDensityFraction"),
	GSurfelLODDensityFraction,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

class FComputeTriangleAreasCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FComputeTriangleAreasCS,Global)
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters,OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), GDistanceFieldAOTileSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), GDistanceFieldAOTileSizeY);
	}

	FComputeTriangleAreasCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		NumTriangles.Bind(Initializer.ParameterMap, TEXT("NumTriangles"));
		TriangleVertexData.Bind(Initializer.ParameterMap, TEXT("TriangleVertexData"));
		TriangleAreas.Bind(Initializer.ParameterMap, TEXT("TriangleAreas"));
	}

	FComputeTriangleAreasCS()
	{
	}

	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FSceneView& View, 
		int32 NumTrianglesValue,
		const FUniformMeshBuffers& UniformMeshBuffers
		)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		SetShaderValue(RHICmdList, ShaderRHI, NumTriangles, NumTrianglesValue);
		SetSRVParameter(RHICmdList, ShaderRHI, TriangleVertexData, UniformMeshBuffers.TriangleDataSRV);

		TriangleAreas.SetBuffer(RHICmdList, ShaderRHI, UniformMeshBuffers.TriangleAreas);
	}

	void UnsetParameters(FRHICommandList& RHICmdList)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();
		TriangleAreas.UnsetUAV(RHICmdList, ShaderRHI);
		// RHISetStreamOutTargets doesn't unbind existing uses like render targets do 
		SetSRVParameter(RHICmdList, ShaderRHI, TriangleVertexData, NULL);
	}

private:

	LAYOUT_FIELD(FShaderParameter, NumTriangles);
	LAYOUT_FIELD(FShaderResourceParameter, TriangleVertexData);
	LAYOUT_FIELD(FRWShaderParameter, TriangleAreas);
};

IMPLEMENT_SHADER_TYPE(,FComputeTriangleAreasCS,TEXT("/Engine/Private/SurfelTree.usf"),TEXT("ComputeTriangleAreasCS"),SF_Compute);


class FComputeTriangleCDFsCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FComputeTriangleCDFsCS,Global)
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters,OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), GDistanceFieldAOTileSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), GDistanceFieldAOTileSizeY);
	}

	FComputeTriangleCDFsCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		NumTriangles.Bind(Initializer.ParameterMap, TEXT("NumTriangles"));
		TriangleAreas.Bind(Initializer.ParameterMap, TEXT("TriangleAreas"));
		TriangleCDFs.Bind(Initializer.ParameterMap, TEXT("TriangleCDFs"));
	}

	FComputeTriangleCDFsCS()
	{
	}

	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FSceneView& View, 
		int32 NumTrianglesValue,
		const FUniformMeshBuffers& UniformMeshBuffers
		)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		SetShaderValue(RHICmdList, ShaderRHI, NumTriangles, NumTrianglesValue);
		SetSRVParameter(RHICmdList, ShaderRHI, TriangleAreas, UniformMeshBuffers.TriangleAreas.SRV);

		TriangleCDFs.SetBuffer(RHICmdList, ShaderRHI, UniformMeshBuffers.TriangleCDFs);
	}

	void UnsetParameters(FRHICommandList& RHICmdList)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();
		TriangleCDFs.UnsetUAV(RHICmdList, ShaderRHI);
	}

private:

	LAYOUT_FIELD(FShaderParameter, NumTriangles);
	LAYOUT_FIELD(FShaderResourceParameter, TriangleAreas);
	LAYOUT_FIELD(FRWShaderParameter, TriangleCDFs);
};

IMPLEMENT_SHADER_TYPE(,FComputeTriangleCDFsCS,TEXT("/Engine/Private/SurfelTree.usf"),TEXT("ComputeTriangleCDFsCS"),SF_Compute);


class FSampleTrianglesCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FSampleTrianglesCS,Global)
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters,OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), GDistanceFieldAOTileSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), GDistanceFieldAOTileSizeY);
	}

	FSampleTrianglesCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		SurfelBufferParameters.Bind(Initializer.ParameterMap);
		SurfelStartIndex.Bind(Initializer.ParameterMap, TEXT("SurfelStartIndex"));
		NumSurfelsToGenerate.Bind(Initializer.ParameterMap, TEXT("NumSurfelsToGenerate"));
		NumTriangles.Bind(Initializer.ParameterMap, TEXT("NumTriangles"));
		TriangleVertexData.Bind(Initializer.ParameterMap, TEXT("TriangleVertexData"));
		TriangleCDFs.Bind(Initializer.ParameterMap, TEXT("TriangleCDFs"));
	}

	FSampleTrianglesCS()
	{
	}

	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FSceneView& View, 
		int32 SurfelStartIndexValue,
		int32 NumSurfelsToGenerateValue,
		int32 NumTrianglesValue,
		const FUniformMeshBuffers& UniformMeshBuffers
		)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		const FScene* Scene = (const FScene*)View.Family->Scene;

		FRHIUnorderedAccessView* UniformMeshUAVs[1];
		UniformMeshUAVs[0] = Scene->DistanceFieldSceneData.SurfelBuffers->InterpolatedVertexData.UAV;
		RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, UniformMeshUAVs, UE_ARRAY_COUNT(UniformMeshUAVs));

		SurfelBufferParameters.Set(RHICmdList, ShaderRHI, *Scene->DistanceFieldSceneData.SurfelBuffers, *Scene->DistanceFieldSceneData.InstancedSurfelBuffers);
		
		SetShaderValue(RHICmdList, ShaderRHI, SurfelStartIndex, SurfelStartIndexValue);
		SetShaderValue(RHICmdList, ShaderRHI, NumSurfelsToGenerate, NumSurfelsToGenerateValue);
		SetShaderValue(RHICmdList, ShaderRHI, NumTriangles, NumTrianglesValue);

		SetSRVParameter(RHICmdList, ShaderRHI, TriangleVertexData, UniformMeshBuffers.TriangleDataSRV);
		SetSRVParameter(RHICmdList, ShaderRHI, TriangleCDFs, UniformMeshBuffers.TriangleCDFs.SRV);
	}

	void UnsetParameters(FRHICommandList& RHICmdList, const FSceneView& View)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		const FScene* Scene = (const FScene*)View.Family->Scene;
		SurfelBufferParameters.UnsetParameters(RHICmdList, ShaderRHI);
		// RHISetStreamOutTargets doesn't unbind existing uses like render targets do 
		SetSRVParameter(RHICmdList, ShaderRHI, TriangleVertexData, NULL);

		FRHIUnorderedAccessView* UniformMeshUAVs[1];
		UniformMeshUAVs[0] = Scene->DistanceFieldSceneData.SurfelBuffers->InterpolatedVertexData.UAV;
		RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, UniformMeshUAVs, UE_ARRAY_COUNT(UniformMeshUAVs));
	}

private:

	LAYOUT_FIELD(FSurfelBufferParameters, SurfelBufferParameters);
	LAYOUT_FIELD(FShaderParameter, SurfelStartIndex);
	LAYOUT_FIELD(FShaderParameter, NumSurfelsToGenerate);
	LAYOUT_FIELD(FShaderParameter, NumTriangles);
	LAYOUT_FIELD(FShaderResourceParameter, TriangleVertexData);
	LAYOUT_FIELD(FShaderResourceParameter, TriangleCDFs);
};

IMPLEMENT_SHADER_TYPE(,FSampleTrianglesCS,TEXT("/Engine/Private/SurfelTree.usf"),TEXT("SampleTrianglesCS"),SF_Compute);

// In float4's, must match usf
int32 FSurfelBuffers::SurfelDataStride = 4;

void ComputeNumSurfels(float BoundsSurfaceArea, int32& PrimitiveNumSurfels, int32& PrimitiveLOD0Surfels)
{
	//@todo - allocate based on actual triangle surface area.  
	// Unfortunately this would make surfel count only known on the GPU which means surfel allocation would have to happen on the GPU.
	const int32 NumSurfels = FMath::Clamp(FMath::TruncToInt(BoundsSurfaceArea * GSurfelDensity / 1000.0f), 10, GMaxSurfelsPerObject);
	// Don't attempt to represent huge meshes
	PrimitiveLOD0Surfels = NumSurfels == GMaxSurfelsPerObject ? 0 : NumSurfels;
	const int32 LOD1Surfels = FMath::Clamp(FMath::TruncToInt(PrimitiveLOD0Surfels * GSurfelLODDensityFraction), 10, GMaxSurfelsPerObject);
	PrimitiveNumSurfels = PrimitiveLOD0Surfels + LOD1Surfels;
}

void GenerateSurfelRepresentation(FRHICommandListImmediate& RHICmdList, FSceneRenderer& Renderer, FViewInfo& View, FPrimitiveSceneInfo* PrimitiveSceneInfo, const FMatrix& Instance0Transform, FPrimitiveSurfelAllocation& Allocation)
{
	if (Allocation.NumSurfels > 0)
	{
		FUniformMeshBuffers* UniformMeshBuffers = NULL;
		const FMaterialRenderProxy* MaterialRenderProxy = NULL;
		FRHIUniformBuffer* PrimitiveUniformBuffer = NULL;
		const int32 NumUniformTriangles = FUniformMeshConverter::Convert(RHICmdList, Renderer, View, PrimitiveSceneInfo, 0, UniformMeshBuffers, MaterialRenderProxy, PrimitiveUniformBuffer);

		if (NumUniformTriangles > 0 && MaterialRenderProxy && PrimitiveUniformBuffer)
		{
			FRHIUnorderedAccessView* UniformMeshUAVs[2];
			UniformMeshUAVs[0] = UniformMeshBuffers->TriangleAreas.UAV;
			UniformMeshUAVs[1] = UniformMeshBuffers->TriangleCDFs.UAV;
			RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, UniformMeshUAVs, UE_ARRAY_COUNT(UniformMeshUAVs));

			{
				TShaderMapRef<FComputeTriangleAreasCS> ComputeShader(GetGlobalShaderMap(View.GetFeatureLevel()));

				RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());
				ComputeShader->SetParameters(RHICmdList, View, NumUniformTriangles, *UniformMeshBuffers);
				DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), FMath::DivideAndRoundUp(NumUniformTriangles, GDistanceFieldAOTileSizeX * GDistanceFieldAOTileSizeY), 1, 1);

				ComputeShader->UnsetParameters(RHICmdList);
				RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, UniformMeshBuffers->TriangleAreas.UAV);
			}			

			{
				TShaderMapRef<FComputeTriangleCDFsCS> ComputeShader(GetGlobalShaderMap(View.GetFeatureLevel()));

				RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());
				ComputeShader->SetParameters(RHICmdList, View, NumUniformTriangles, *UniformMeshBuffers);
				DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), FMath::DivideAndRoundUp(NumUniformTriangles, GDistanceFieldAOTileSizeX * GDistanceFieldAOTileSizeY), 1, 1);

				ComputeShader->UnsetParameters(RHICmdList);
				RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, UniformMeshBuffers->TriangleCDFs.UAV);
			}			

			{
				TShaderMapRef<FSampleTrianglesCS> ComputeShader(GetGlobalShaderMap(View.GetFeatureLevel()));

				RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());
				ComputeShader->SetParameters(RHICmdList, View, Allocation.Offset, Allocation.NumLOD0, NumUniformTriangles, *UniformMeshBuffers);
				DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), FMath::DivideAndRoundUp(Allocation.NumLOD0, GDistanceFieldAOTileSizeX * GDistanceFieldAOTileSizeY), 1, 1);

				ComputeShader->UnsetParameters(RHICmdList, View);
			}

			FUniformMeshConverter::GenerateSurfels(RHICmdList, View, PrimitiveSceneInfo, MaterialRenderProxy, PrimitiveUniformBuffer, Instance0Transform, Allocation.Offset, Allocation.NumLOD0);

			const int32 NumLOD1 = Allocation.NumSurfels - Allocation.NumLOD0;

			if (NumLOD1 > 0)
			{
				{
					TShaderMapRef<FSampleTrianglesCS> ComputeShader(GetGlobalShaderMap(View.GetFeatureLevel()));

					RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());
					ComputeShader->SetParameters(RHICmdList, View, Allocation.Offset + Allocation.NumLOD0, NumLOD1, NumUniformTriangles, *UniformMeshBuffers);
					DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), FMath::DivideAndRoundUp(NumLOD1, GDistanceFieldAOTileSizeX * GDistanceFieldAOTileSizeY), 1, 1);

					ComputeShader->UnsetParameters(RHICmdList, View);
				}

				FUniformMeshConverter::GenerateSurfels(RHICmdList, View, PrimitiveSceneInfo, MaterialRenderProxy, PrimitiveUniformBuffer, Instance0Transform, Allocation.Offset + Allocation.NumLOD0, NumLOD1);
			}
		}
		else
		{
			Allocation.NumSurfels = 0;
			Allocation.NumLOD0 = 0;
			Allocation.NumInstances = 0;
		}
	}
}
