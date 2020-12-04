// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
PathCompactionCompute.cpp: Compute path continuation shader.
=============================================================================*/

#include "RendererPrivate.h"

#if RHI_RAYTRACING

#include "GlobalShader.h"
#include "DeferredShadingRenderer.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/SceneFilterRendering.h"

class FPathCompactionCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPathCompactionCS, Global)
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 8;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}

	FPathCompactionCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		// Input
		ViewParameter.Bind(Initializer.ParameterMap, TEXT("View"));
		RadianceTextureParameter.Bind(Initializer.ParameterMap, TEXT("RadianceTexture"));
		SampleCountTextureParameter.Bind(Initializer.ParameterMap, TEXT("SampleCountTexture"));
		PixelPositionTextureParameter.Bind(Initializer.ParameterMap, TEXT("PixelPositionTexture"));

		// Output
		RadianceSortedRedUAVParameter.Bind(Initializer.ParameterMap, TEXT("RadianceSortedRedRT"));
		RadianceSortedGreenUAVParameter.Bind(Initializer.ParameterMap, TEXT("RadianceSortedGreenRT"));
		RadianceSortedBlueUAVParameter.Bind(Initializer.ParameterMap, TEXT("RadianceSortedBlueRT"));
		RadianceSortedAlphaUAVParameter.Bind(Initializer.ParameterMap, TEXT("RadianceSortedAlphaRT"));
		SampleCountSortedUAVParameter.Bind(Initializer.ParameterMap, TEXT("SampleCountSortedRT"));
	}

	FPathCompactionCS() {}

	void SetParameters(
		FRHICommandListImmediate& RHICmdList,
		const FViewInfo& View,
		FRHITexture* RadianceTexture,
		FRHITexture* SampleCountTexture,
		FRHITexture* PixelPositionTexture,
		FRHIUnorderedAccessView* RadianceSortedRedUAV,
		FRHIUnorderedAccessView* RadianceSortedGreenUAV,
		FRHIUnorderedAccessView* RadianceSortedBlueUAV,
		FRHIUnorderedAccessView* RadianceSortedAlphaUAV,
		FRHIUnorderedAccessView* SampleCountSortedUAV)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		// Input textures
		SetTextureParameter(RHICmdList, ShaderRHI, RadianceTextureParameter, RadianceTexture);
		SetTextureParameter(RHICmdList, ShaderRHI, SampleCountTextureParameter, SampleCountTexture);
		SetTextureParameter(RHICmdList, ShaderRHI, PixelPositionTextureParameter, PixelPositionTexture);

		// Output UAVs
		SetUAVParameter(RHICmdList, ShaderRHI, RadianceSortedRedUAVParameter, RadianceSortedRedUAV);
		SetUAVParameter(RHICmdList, ShaderRHI, RadianceSortedGreenUAVParameter, RadianceSortedGreenUAV);
		SetUAVParameter(RHICmdList, ShaderRHI, RadianceSortedBlueUAVParameter, RadianceSortedBlueUAV);
		SetUAVParameter(RHICmdList, ShaderRHI, RadianceSortedAlphaUAVParameter, RadianceSortedAlphaUAV);
		SetUAVParameter(RHICmdList, ShaderRHI, SampleCountSortedUAVParameter, SampleCountSortedUAV);
	}

	void UnsetParameters(
		FRHICommandList& RHICmdList,
		ERHIAccess TransitionAccess,
		FRHIUnorderedAccessView* RadianceSortedRedUAV,
		FRHIUnorderedAccessView* RadianceSortedGreenUAV,
		FRHIUnorderedAccessView* RadianceSortedBlueUAV,
		FRHIUnorderedAccessView* RadianceSortedAlphaUAV,
		FRHIUnorderedAccessView* SampleCountSortedUAV)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		SetUAVParameter(RHICmdList, ShaderRHI, RadianceSortedRedUAVParameter, FUnorderedAccessViewRHIRef());
		SetUAVParameter(RHICmdList, ShaderRHI, RadianceSortedGreenUAVParameter, FUnorderedAccessViewRHIRef());
		SetUAVParameter(RHICmdList, ShaderRHI, RadianceSortedBlueUAVParameter, FUnorderedAccessViewRHIRef());
		SetUAVParameter(RHICmdList, ShaderRHI, RadianceSortedAlphaUAVParameter, FUnorderedAccessViewRHIRef());
		SetUAVParameter(RHICmdList, ShaderRHI, SampleCountSortedUAVParameter, FUnorderedAccessViewRHIRef());
		FRHITransitionInfo TransitionInfos[] = {
			FRHITransitionInfo(RadianceSortedRedUAV, ERHIAccess::Unknown, TransitionAccess),
			FRHITransitionInfo(RadianceSortedGreenUAV, ERHIAccess::Unknown, TransitionAccess),
			FRHITransitionInfo(RadianceSortedBlueUAV, ERHIAccess::Unknown, TransitionAccess),
			FRHITransitionInfo(RadianceSortedAlphaUAV, ERHIAccess::Unknown, TransitionAccess),
			FRHITransitionInfo(SampleCountSortedUAV, ERHIAccess::Unknown, TransitionAccess)
		};
		RHICmdList.Transition(MakeArrayView(TransitionInfos, UE_ARRAY_COUNT(TransitionInfos)));
	}

private:
	// Input parameters
	LAYOUT_FIELD(FShaderResourceParameter, ViewParameter);
	LAYOUT_FIELD(FShaderResourceParameter, RadianceTextureParameter);
	LAYOUT_FIELD(FShaderResourceParameter, SampleCountTextureParameter);
	LAYOUT_FIELD(FShaderResourceParameter, PixelPositionTextureParameter);

	// Output parameters
	LAYOUT_FIELD(FShaderResourceParameter, RadianceSortedRedUAVParameter);
	LAYOUT_FIELD(FShaderResourceParameter, RadianceSortedGreenUAVParameter);
	LAYOUT_FIELD(FShaderResourceParameter, RadianceSortedBlueUAVParameter);
	LAYOUT_FIELD(FShaderResourceParameter, RadianceSortedAlphaUAVParameter);
	LAYOUT_FIELD(FShaderResourceParameter, SampleCountSortedUAVParameter);
};

IMPLEMENT_SHADER_TYPE(, FPathCompactionCS, TEXT("/Engine/Private/PathTracing/PathCompaction.usf"), TEXT("PathCompactionCS"), SF_Compute)

void FDeferredShadingSceneRenderer::ComputePathCompaction(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	FRHITexture* RadianceTexture,
	FRHITexture* SampleCountTexture,
	FRHITexture* PixelPositionTexture,
	FRHIUnorderedAccessView* RadianceSortedRedUAV,
	FRHIUnorderedAccessView* RadianceSortedGreenUAV,
	FRHIUnorderedAccessView* RadianceSortedBlueUAV,
	FRHIUnorderedAccessView* RadianceSortedAlphaUAV,
	FRHIUnorderedAccessView* SampleCountSortedUAV)
{
	const auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FPathCompactionCS> PathCompactionComputeShader(ShaderMap);
	RHICmdList.SetComputeShader(PathCompactionComputeShader.GetComputeShader());

	PathCompactionComputeShader->SetParameters(RHICmdList, View, RadianceTexture, SampleCountTexture, PixelPositionTexture, RadianceSortedRedUAV, RadianceSortedGreenUAV, RadianceSortedBlueUAV, RadianceSortedAlphaUAV, SampleCountSortedUAV);
	FIntPoint ViewSize = View.ViewRect.Size();
	FIntVector NumGroups = FIntVector::DivideAndRoundUp(FIntVector(ViewSize.X, ViewSize.Y, 0), FPathCompactionCS::GetGroupSize());
	DispatchComputeShader(RHICmdList, PathCompactionComputeShader.GetShader(), NumGroups.X, NumGroups.Y, 1);
	PathCompactionComputeShader->UnsetParameters(RHICmdList, ERHIAccess::SRVMask, RadianceSortedRedUAV, RadianceSortedGreenUAV, RadianceSortedBlueUAV, RadianceSortedAlphaUAV, SampleCountSortedUAV);
}
#endif // RHI_RAYTRACING
