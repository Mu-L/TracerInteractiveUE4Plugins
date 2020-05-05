// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DistanceFieldVisualization.cpp
=============================================================================*/

#include "DistanceFieldAmbientOcclusion.h"
#include "DeferredShadingRenderer.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/SceneFilterRendering.h"
#include "DistanceFieldLightingShared.h"
#include "ScreenRendering.h"
#include "DistanceFieldLightingPost.h"
#include "OneColorShader.h"
#include "GlobalDistanceField.h"
#include "FXSystem.h"
#include "PostProcess/PostProcessSubsurface.h"
#include "PipelineStateCache.h"

template<bool bUseGlobalDistanceField>
class TVisualizeMeshDistanceFieldCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TVisualizeMeshDistanceFieldCS, Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldAO(Parameters.Platform) && IsUsingDistanceFields(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("DOWNSAMPLE_FACTOR"), GAODownsampleFactor);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), GDistanceFieldAOTileSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), GDistanceFieldAOTileSizeY);
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_DISTANCE_FIELD"), bUseGlobalDistanceField);
	}

	/** Default constructor. */
	TVisualizeMeshDistanceFieldCS() {}

	/** Initialization constructor. */
	TVisualizeMeshDistanceFieldCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		VisualizeMeshDistanceFields.Bind(Initializer.ParameterMap, TEXT("VisualizeMeshDistanceFields"));
		NumGroups.Bind(Initializer.ParameterMap, TEXT("NumGroups"));
		ObjectParameters.Bind(Initializer.ParameterMap);
		SceneTextureParameters.Bind(Initializer);
		AOParameters.Bind(Initializer.ParameterMap);
		GlobalDistanceFieldParameters.Bind(Initializer.ParameterMap);
	}

	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FSceneView& View, 
		FSceneRenderTargetItem& VisualizeMeshDistanceFieldsValue, 
		FVector2D NumGroupsValue,
		const FDistanceFieldAOParameters& Parameters,
		const FGlobalDistanceFieldInfo& GlobalDistanceFieldInfo)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, VisualizeMeshDistanceFieldsValue.UAV);
		VisualizeMeshDistanceFields.SetTexture(RHICmdList, ShaderRHI, VisualizeMeshDistanceFieldsValue.ShaderResourceTexture, VisualizeMeshDistanceFieldsValue.UAV);

		FRHITexture* TextureAtlas;
		int32 AtlasSizeX;
		int32 AtlasSizeY;
		int32 AtlasSizeZ;

		TextureAtlas = GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI;
		AtlasSizeX = GDistanceFieldVolumeTextureAtlas.GetSizeX();
		AtlasSizeY = GDistanceFieldVolumeTextureAtlas.GetSizeY();
		AtlasSizeZ = GDistanceFieldVolumeTextureAtlas.GetSizeZ();

		ObjectParameters.Set(RHICmdList, ShaderRHI, GAOCulledObjectBuffers.Buffers, TextureAtlas, FIntVector(AtlasSizeX, AtlasSizeY, AtlasSizeZ));

		AOParameters.Set(RHICmdList, ShaderRHI, Parameters);
		SceneTextureParameters.Set(RHICmdList, ShaderRHI, View.FeatureLevel, ESceneTextureSetupMode::All);

		if (bUseGlobalDistanceField)
		{
			GlobalDistanceFieldParameters.Set(RHICmdList, ShaderRHI, GlobalDistanceFieldInfo.ParameterData);
		}

		SetShaderValue(RHICmdList, ShaderRHI, NumGroups, NumGroupsValue);
	}

	void UnsetParameters(FRHICommandList& RHICmdList, FSceneRenderTargetItem& VisualizeMeshDistanceFieldsValue)
	{
		RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, VisualizeMeshDistanceFieldsValue.UAV);
		VisualizeMeshDistanceFields.UnsetUAV(RHICmdList, RHICmdList.GetBoundComputeShader());
	}

private:

	LAYOUT_FIELD(FRWShaderParameter, VisualizeMeshDistanceFields);
	LAYOUT_FIELD(FShaderParameter, NumGroups);
	LAYOUT_FIELD((TDistanceFieldCulledObjectBufferParameters<DFPT_SignedDistanceField>), ObjectParameters);
	LAYOUT_FIELD(FSceneTextureShaderParameters, SceneTextureParameters);
	LAYOUT_FIELD(FAOParameters, AOParameters);
	LAYOUT_FIELD(FGlobalDistanceFieldParameters, GlobalDistanceFieldParameters);
};

IMPLEMENT_SHADER_TYPE(template<>,TVisualizeMeshDistanceFieldCS<true>,TEXT("/Engine/Private/DistanceFieldVisualization.usf"),TEXT("VisualizeMeshDistanceFieldCS"),SF_Compute);
IMPLEMENT_SHADER_TYPE(template<>,TVisualizeMeshDistanceFieldCS<false>,TEXT("/Engine/Private/DistanceFieldVisualization.usf"),TEXT("VisualizeMeshDistanceFieldCS"),SF_Compute);

class FVisualizeDistanceFieldUpsamplePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FVisualizeDistanceFieldUpsamplePS, Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldAO(Parameters.Platform) && IsUsingDistanceFields(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("DOWNSAMPLE_FACTOR"), GAODownsampleFactor);
	}

	/** Default constructor. */
	FVisualizeDistanceFieldUpsamplePS() {}

	/** Initialization constructor. */
	FVisualizeDistanceFieldUpsamplePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		SceneTextureParameters.Bind(Initializer);
		VisualizeDistanceFieldTexture.Bind(Initializer.ParameterMap,TEXT("VisualizeDistanceFieldTexture"));
		VisualizeDistanceFieldSampler.Bind(Initializer.ParameterMap,TEXT("VisualizeDistanceFieldSampler"));
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, TRefCountPtr<IPooledRenderTarget>& VisualizeDistanceField)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);
		SceneTextureParameters.Set(RHICmdList, ShaderRHI, View.FeatureLevel, ESceneTextureSetupMode::All);

		SetTextureParameter(RHICmdList, ShaderRHI, VisualizeDistanceFieldTexture, VisualizeDistanceFieldSampler, TStaticSamplerState<SF_Bilinear>::GetRHI(), VisualizeDistanceField->GetRenderTargetItem().ShaderResourceTexture);
	}

private:

	LAYOUT_FIELD(FSceneTextureShaderParameters, SceneTextureParameters);
	LAYOUT_FIELD(FShaderResourceParameter, VisualizeDistanceFieldTexture);
	LAYOUT_FIELD(FShaderResourceParameter, VisualizeDistanceFieldSampler);
};

IMPLEMENT_SHADER_TYPE(,FVisualizeDistanceFieldUpsamplePS,TEXT("/Engine/Private/DistanceFieldVisualization.usf"),TEXT("VisualizeDistanceFieldUpsamplePS"),SF_Pixel);


void FDeferredShadingSceneRenderer::RenderMeshDistanceFieldVisualization(FRHICommandListImmediate& RHICmdList, const FDistanceFieldAOParameters& Parameters)
{
	//@todo - support multiple views
	const FViewInfo& View = Views[0];

	if (UseDistanceFieldAO()
		&& FeatureLevel >= ERHIFeatureLevel::SM5
		&& DoesPlatformSupportDistanceFieldAO(View.GetShaderPlatform())
		&& Views.Num() == 1)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RenderMeshDistanceFieldVis);
		SCOPED_DRAW_EVENT(RHICmdList, VisualizeMeshDistanceFields);

		if (GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI && Scene->DistanceFieldSceneData.NumObjectsInBuffer > 0)
		{
			check(!Scene->DistanceFieldSceneData.HasPendingOperations());

			QUICK_SCOPE_CYCLE_COUNTER(STAT_AOIssueGPUWork);

			const bool bUseGlobalDistanceField = UseGlobalDistanceField(Parameters) && View.Family->EngineShowFlags.VisualizeGlobalDistanceField;

			CullObjectsToView(RHICmdList, Scene, View, Parameters, GAOCulledObjectBuffers);

			TRefCountPtr<IPooledRenderTarget> VisualizeResultRT;

			{
				const FIntPoint BufferSize = GetBufferSizeForAO();
				FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_FloatRGBA, FClearValueBinding::None, TexCreate_None, TexCreate_RenderTargetable | TexCreate_UAV, false));
				GRenderTargetPool.FindFreeElement(RHICmdList, Desc, VisualizeResultRT, TEXT("VisualizeDistanceField"));
			}

			{
				PRAGMA_DISABLE_DEPRECATION_WARNINGS
				UnbindRenderTargets(RHICmdList);
				PRAGMA_ENABLE_DEPRECATION_WARNINGS

				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					const FViewInfo& ViewInfo = Views[ViewIndex];

					uint32 GroupSizeX = FMath::DivideAndRoundUp(ViewInfo.ViewRect.Size().X / GAODownsampleFactor, GDistanceFieldAOTileSizeX);
					uint32 GroupSizeY = FMath::DivideAndRoundUp(ViewInfo.ViewRect.Size().Y / GAODownsampleFactor, GDistanceFieldAOTileSizeY);

					SCOPED_GPU_MASK(RHICmdList, View.GPUMask);
					SCOPED_DRAW_EVENT(RHICmdList, VisualizeMeshDistanceFieldCS);

					FSceneRenderTargetItem& VisualizeResultRTI = VisualizeResultRT->GetRenderTargetItem();
					if (bUseGlobalDistanceField)
					{
						check(View.GlobalDistanceFieldInfo.Clipmaps.Num() > 0);

						TShaderMapRef<TVisualizeMeshDistanceFieldCS<true> > ComputeShader(ViewInfo.ShaderMap);

						RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());
						ComputeShader->SetParameters(RHICmdList, ViewInfo, VisualizeResultRTI, FVector2D(GroupSizeX, GroupSizeY), Parameters, View.GlobalDistanceFieldInfo);
						DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), GroupSizeX, GroupSizeY, 1);

						ComputeShader->UnsetParameters(RHICmdList, VisualizeResultRTI);
					}
					else
					{
						TShaderMapRef<TVisualizeMeshDistanceFieldCS<false> > ComputeShader(ViewInfo.ShaderMap);

						RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());
						ComputeShader->SetParameters(RHICmdList, ViewInfo, VisualizeResultRTI, FVector2D(GroupSizeX, GroupSizeY), Parameters, View.GlobalDistanceFieldInfo);
						DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), GroupSizeX, GroupSizeY, 1);

						ComputeShader->UnsetParameters(RHICmdList, VisualizeResultRTI);
					}
				}
			}

			if ( IsTransientResourceBufferAliasingEnabled())
			{
				GAOCulledObjectBuffers.Buffers.DiscardTransientResource();
			}

			check(RHICmdList.IsOutsideRenderPass());

			{
				// We must specify StencilWrite or VK will lose the attachment
				FSceneRenderTargets::Get(RHICmdList).BeginRenderingSceneColor(RHICmdList, ESimpleRenderTargetMode::EExistingColorAndDepth, FExclusiveDepthStencil::DepthRead_StencilWrite);

				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					const FViewInfo& ViewInfo = Views[ViewIndex];

					SCOPED_GPU_MASK(RHICmdList, View.GPUMask);
					SCOPED_DRAW_EVENT(RHICmdList, UpsampleAO);

					RHICmdList.SetViewport(ViewInfo.ViewRect.Min.X, ViewInfo.ViewRect.Min.Y, 0.0f, ViewInfo.ViewRect.Max.X, ViewInfo.ViewRect.Max.Y, 1.0f);
					
					FGraphicsPipelineStateInitializer GraphicsPSOInit;
					RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
					GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
					GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();

					TShaderMapRef<FPostProcessVS> VertexShader( ViewInfo.ShaderMap );
					TShaderMapRef<FVisualizeDistanceFieldUpsamplePS> PixelShader( ViewInfo.ShaderMap );

					GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
					GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
					GraphicsPSOInit.PrimitiveType = PT_TriangleList;
					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

					PixelShader->SetParameters(RHICmdList, ViewInfo, VisualizeResultRT);

					DrawRectangle( 
						RHICmdList,
						0, 0, 
						ViewInfo.ViewRect.Width(), ViewInfo.ViewRect.Height(),
						ViewInfo.ViewRect.Min.X / GAODownsampleFactor, ViewInfo.ViewRect.Min.Y / GAODownsampleFactor, 
						ViewInfo.ViewRect.Width() / GAODownsampleFactor, ViewInfo.ViewRect.Height() / GAODownsampleFactor,
						FIntPoint(ViewInfo.ViewRect.Width(), ViewInfo.ViewRect.Height()),
						GetBufferSizeForAO(),
						VertexShader);
				}

				FSceneRenderTargets::Get(RHICmdList).FinishRenderingSceneColor(RHICmdList);
			}
		}
	}
}
