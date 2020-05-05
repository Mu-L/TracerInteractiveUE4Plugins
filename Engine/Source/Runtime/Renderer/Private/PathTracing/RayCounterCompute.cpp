// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SkyLightCdfBuild.cpp: SkyLight CDF build algorithm.
=============================================================================*/

#include "RendererPrivate.h"

#if RHI_RAYTRACING

#include "GlobalShader.h"
#include "DeferredShadingRenderer.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/SceneFilterRendering.h"
#include "RHIGPUReadback.h"


class FRayCounterCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FRayCounterCS, Global)
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 64;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}

	FRayCounterCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		RayCountPerPixelParameter.Bind(Initializer.ParameterMap, TEXT("RayCountPerPixel"));
		ViewSizeParameter.Bind(Initializer.ParameterMap, TEXT("ViewSize"));
		TotalRayCountParameter.Bind(Initializer.ParameterMap, TEXT("TotalRayCount"));
	}

	FRayCounterCS()
	{
	}

	void SetParameters(
		FRHICommandList& RHICmdList,
		FRHITexture* RayCountPerPixelBuffer,
		const FIntPoint& ViewSize,
		FRHIUnorderedAccessView* TotalRayCountBuffer)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		SetTextureParameter(RHICmdList, ShaderRHI, RayCountPerPixelParameter, RayCountPerPixelBuffer);
		SetShaderValue(RHICmdList, ShaderRHI, ViewSizeParameter, ViewSize);
		SetUAVParameter(RHICmdList, ShaderRHI, TotalRayCountParameter, TotalRayCountBuffer);
	}

	void UnsetParameters(
		FRHICommandList& RHICmdList,
		EResourceTransitionAccess TransitionAccess,
		EResourceTransitionPipeline TransitionPipeline,
		FRWBuffer& TotalRayCountBuffer,
		FRHIComputeFence* Fence)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		RHICmdList.TransitionResource(TransitionAccess, TransitionPipeline, TotalRayCountBuffer.UAV, Fence);
	}

private:
	// Input parameters
	LAYOUT_FIELD(FShaderResourceParameter, RayCountPerPixelParameter)
	LAYOUT_FIELD(FShaderParameter, ViewSizeParameter)

	// Output parameters
	LAYOUT_FIELD(FShaderResourceParameter, TotalRayCountParameter)
};

IMPLEMENT_SHADER_TYPE(, FRayCounterCS, TEXT("/Engine/Private/PathTracing/PathTracingRayCounterComputeShader.usf"), TEXT("RayCounterCS"), SF_Compute)


void FDeferredShadingSceneRenderer::ComputeRayCount(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, FRHITexture* RayCountPerPixelTexture)
{
	FSceneViewState* ViewState = (FSceneViewState*)View.State;
	RHICmdList.ClearUAVUint(ViewState->TotalRayCountBuffer->UAV, FUintVector4(0, 0, 0, 0));

	const auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FRayCounterCS> RayCounterComputeShader(ShaderMap);
	RHICmdList.SetComputeShader(RayCounterComputeShader.GetComputeShader());

	FIntPoint ViewSize = View.ViewRect.Size();
	RayCounterComputeShader->SetParameters(RHICmdList, RayCountPerPixelTexture, ViewSize, ViewState->TotalRayCountBuffer->UAV);

	int32 NumGroups = FMath::DivideAndRoundUp<int32>(ViewSize.Y, FRayCounterCS::GetGroupSize());
	DispatchComputeShader(RHICmdList, RayCounterComputeShader.GetShader(), NumGroups, 1, 1);

	FRHIGPUBufferReadback* RayCountGPUReadback = ViewState->RayCountGPUReadback;

	// Read read count data from the GPU using a stage buffer to avoid stalls
	if (!ViewState->bReadbackInitialized)
	{
		RayCountGPUReadback->EnqueueCopy(RHICmdList, ViewState->TotalRayCountBuffer->Buffer);
		ViewState->bReadbackInitialized = true;
	}
	else if (RayCountGPUReadback->IsReady())
	{
		uint32* RayCountResultBuffer = static_cast<uint32*>(RayCountGPUReadback->Lock(1 * sizeof(uint32)));
		ViewState->TotalRayCount = RayCountResultBuffer[0];
		extern ENGINE_API float GAveragePathTracedMRays;
		GAveragePathTracedMRays = ViewState->TotalRayCount / 1000000.f;

		RayCountGPUReadback->Unlock();

		// Enqueue another copy so we can get the data out.
		RayCountGPUReadback->EnqueueCopy(RHICmdList, ViewState->TotalRayCountBuffer->Buffer);
	}
}

#endif // RHI_RAYTRACING

