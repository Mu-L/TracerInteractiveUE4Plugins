// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "PostProcess/PostProcessCompositeEditorPrimitives.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "PostProcess/SceneRenderTargets.h"
#include "SceneRenderTargetParameters.h"
#include "BasePassRendering.h"
#include "MobileBasePassRendering.h"
#include "RenderTargetPool.h"
#include "DynamicPrimitiveDrawing.h"
#include "ClearQuad.h"
#include "PipelineStateCache.h"
#include "VisualizeTexture.h"
#include "MeshPassProcessor.inl"
#include "EditorPrimitivesRendering.h"

#if WITH_EDITOR

#include "PostProcess/PostProcessing.h"
#include "PostProcess/SceneFilterRendering.h"


// temporary
static TAutoConsoleVariable<float> CVarEditorOpaqueGizmo(
	TEXT("r.Editor.OpaqueGizmo"),
	0.0f,
	TEXT("0..1\n0: occluded gizmo is partly transparent (default), 1:gizmo is never occluded"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarEditorMovingPattern(
	TEXT("r.Editor.MovingPattern"),
	1.0f,
	TEXT("0:animation over time is off (default is 1)"),
	ECVF_RenderThreadSafe);

/**
 * Pixel shader to populate the editor primitive depth buffer with the scene color depths.
 */
template<uint32 MSAASampleCount>
class FPostProcessPopulateEditorDepthPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE( FPostProcessPopulateEditorDepthPS, Global )

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		if(!IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && MSAASampleCount > 1)
		{
			return false;
		}
		return IsPCPlatform(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine( TEXT("MSAA_SAMPLE_COUNT"), MSAASampleCount);
	}

	FPostProcessPopulateEditorDepthPS() {}

public:


	/** FPostProcessPassParameters constructor. */
	FPostProcessPopulateEditorDepthPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostProcessParameters.Bind(Initializer.ParameterMap);
		FilteredSceneDepthTexture.Bind(Initializer.ParameterMap, TEXT("FilteredSceneDepthTexture"));
		FilteredSceneDepthTextureSampler.Bind(Initializer.ParameterMap, TEXT("FilteredSceneDepthTextureSampler"));
	}

	void SetParameters(const FRenderingCompositePassContext& Context)
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(Context.RHICmdList);
		FRHIPixelShader* ShaderRHI = GetPixelShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);

		FSamplerStateRHIRef SamplerStateRHIRef = TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI();
		PostProcessParameters.SetPS(Context.RHICmdList, ShaderRHI, Context, SamplerStateRHIRef);


		if (FilteredSceneDepthTexture.IsBound())
		{
			const FTexture2DRHIRef* DepthTexture = SceneContext.GetActualDepthTexture();
			SetTextureParameter(
				Context.RHICmdList,
				ShaderRHI,
				FilteredSceneDepthTexture,
				FilteredSceneDepthTextureSampler,
				TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),
				*DepthTexture
			);
		}
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << PostProcessParameters;
		Ar << FilteredSceneDepthTexture;
		Ar << FilteredSceneDepthTextureSampler;

		return bShaderHasOutdatedParameters;
	}

private:
	FPostProcessPassParameters PostProcessParameters;
	FShaderResourceParameter FilteredSceneDepthTexture;
	FShaderResourceParameter FilteredSceneDepthTextureSampler;

	static const TCHAR* GetSourceFilename()
	{
		return TEXT("/Engine/Private/PostProcessCompositeEditorPrimitives.usf");
	}

	static const TCHAR* GetFunctionName()
	{
		return TEXT("MainPopulateSceneDepthPS");
	}
};

#define VARIATION1(A) typedef FPostProcessPopulateEditorDepthPS<A> FPostProcessPopulateEditorDepthPS##A; \
	IMPLEMENT_SHADER_TYPE2(FPostProcessPopulateEditorDepthPS##A, SF_Pixel);
VARIATION1(1)
VARIATION1(2)
#undef VARIATION1

/**
 * Pixel shader to composite editor primitive within the scene color.
 */
template<uint32 MSAASampleCount>
class FPostProcessComposeEditorPrimitivesPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE( FPostProcessComposeEditorPrimitivesPS, Global )

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		if(!IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && MSAASampleCount > 1)
		{
			return false;
		}

		return IsPCPlatform(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine( TEXT("MSAA_SAMPLE_COUNT"), MSAASampleCount);
		OutEnvironment.SetDefine( TEXT("OUTPUT_SRGB_BUFFER"), IsMobileColorsRGB() && IsMobilePlatform(Parameters.Platform));
	}

	FPostProcessComposeEditorPrimitivesPS() {}

public:


	/** FPostProcessPassParameters constructor. */
	FPostProcessComposeEditorPrimitivesPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostProcessParameters.Bind(Initializer.ParameterMap);
		SceneTextureParameters.Bind(Initializer);
		EditorPrimitivesDepth.Bind(Initializer.ParameterMap,TEXT("EditorPrimitivesDepth"));
		EditorPrimitivesColor.Bind(Initializer.ParameterMap,TEXT("EditorPrimitivesColor"));
		EditorPrimitivesColorSampler.Bind(Initializer.ParameterMap,TEXT("EditorPrimitivesColorSampler"));
		EditorRenderParams.Bind(Initializer.ParameterMap,TEXT("EditorRenderParams"));
		FilteredSceneDepthTexture.Bind(Initializer.ParameterMap,TEXT("FilteredSceneDepthTexture"));
		FilteredSceneDepthTextureSampler.Bind(Initializer.ParameterMap,TEXT("FilteredSceneDepthTextureSampler"));
	}

	void SetParameters(const FRenderingCompositePassContext& Context, bool bComposeAnyNonNullDepth)
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(Context.RHICmdList);
		FRHIPixelShader* ShaderRHI = GetPixelShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);

		SceneTextureParameters.Set(Context.RHICmdList, ShaderRHI, Context.View.FeatureLevel, ESceneTextureSetupMode::All);

		FSamplerStateRHIRef SamplerStateRHIRef = TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI();
		PostProcessParameters.SetPS(Context.RHICmdList, ShaderRHI, Context, SamplerStateRHIRef);
		if(MSAASampleCount > 1)
		{
			SetTextureParameter(Context.RHICmdList, ShaderRHI, EditorPrimitivesColor, SceneContext.EditorPrimitivesColor->GetRenderTargetItem().TargetableTexture);
			SetTextureParameter(Context.RHICmdList, ShaderRHI, EditorPrimitivesDepth, SceneContext.EditorPrimitivesDepth->GetRenderTargetItem().TargetableTexture);
		}
		else
		{
			SetTextureParameter(Context.RHICmdList, ShaderRHI, EditorPrimitivesColor, EditorPrimitivesColorSampler, SamplerStateRHIRef, SceneContext.EditorPrimitivesColor->GetRenderTargetItem().ShaderResourceTexture);
			SetTextureParameter(Context.RHICmdList, ShaderRHI, EditorPrimitivesDepth, SceneContext.EditorPrimitivesDepth->GetRenderTargetItem().ShaderResourceTexture);
		}

		{
			FLinearColor Value(CVarEditorOpaqueGizmo.GetValueOnRenderThread(), CVarEditorMovingPattern.GetValueOnRenderThread(), bComposeAnyNonNullDepth ? 1.0f : 0.0f, 0);

			const FSceneViewFamily& ViewFamily = *(Context.View.Family);

			if(ViewFamily.EngineShowFlags.Wireframe)
			{
				// no occlusion in wire frame rendering
				Value.R = 1;
			}

			if(!ViewFamily.bRealtimeUpdate)
			{
				// no animation if realtime update is disabled
				Value.G = 0;
			}

			SetShaderValue(Context.RHICmdList, ShaderRHI, EditorRenderParams, Value);
		}

		if(FilteredSceneDepthTexture.IsBound())
		{
			const FTexture2DRHIRef* DepthTexture = SceneContext.GetActualDepthTexture();
			SetTextureParameter(
				Context.RHICmdList,
				ShaderRHI,
				FilteredSceneDepthTexture,
				FilteredSceneDepthTextureSampler,
				TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
				*DepthTexture
				);
		}
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << PostProcessParameters << EditorPrimitivesColor << EditorPrimitivesColorSampler << EditorPrimitivesDepth << SceneTextureParameters << EditorRenderParams;
		Ar << FilteredSceneDepthTexture;
		Ar << FilteredSceneDepthTextureSampler;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderResourceParameter EditorPrimitivesColor;
	FShaderResourceParameter EditorPrimitivesColorSampler;
	FShaderResourceParameter EditorPrimitivesDepth;
	FPostProcessPassParameters PostProcessParameters;
	FSceneTextureShaderParameters SceneTextureParameters;
	FShaderParameter EditorRenderParams;
	/** Parameter for reading filtered depth values */
	FShaderResourceParameter FilteredSceneDepthTexture; 
	FShaderResourceParameter FilteredSceneDepthTextureSampler; 

	static const TCHAR* GetSourceFilename()
	{
		return TEXT("/Engine/Private/PostProcessCompositeEditorPrimitives.usf");
	}

	static const TCHAR* GetFunctionName()
	{
		return TEXT("MainComposeEditorPrimitivesPS");
	}
};

// #define avoids a lot of code duplication
#define VARIATION1(A) typedef FPostProcessComposeEditorPrimitivesPS<A> FPostProcessComposeEditorPrimitivesPS##A; \
	IMPLEMENT_SHADER_TYPE2(FPostProcessComposeEditorPrimitivesPS##A, SF_Pixel);

VARIATION1(0) // Mobile.
VARIATION1(1)
VARIATION1(2)
VARIATION1(4)
VARIATION1(8)
#undef VARIATION1


template <uint32 MSAASampleCount>
static void SetPopulateSceneDepthForEditorPrimitivesShaderTempl(const FRenderingCompositePassContext& Context)
{
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	// set the state
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_Always>::GetRHI();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	const auto FeatureLevel = Context.GetFeatureLevel();
	auto ShaderMap = Context.GetShaderMap();

	TShaderMapRef<FPostProcessVS> VertexShader(ShaderMap);
	TShaderMapRef<FPostProcessPopulateEditorDepthPS<MSAASampleCount> > PixelShader(ShaderMap);

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
	SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

	VertexShader->SetParameters(Context);
	PixelShader->SetParameters(Context);
}

static void RenderEditorPrimitives(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, FMeshPassProcessorRenderState& DrawRenderState)
{
	// Always depth test against other editor primitives
	DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<
		true, CF_DepthNearOrEqual,
		true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
		false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
		0xFF, GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1) | STENCIL_LIGHTING_CHANNELS_MASK(0x7)>::GetRHI());

	DrawDynamicMeshPass(View, RHICmdList,
		[&View, &DrawRenderState](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
	{
		FEditorPrimitivesBasePassMeshProcessor PassMeshProcessor(
			View.Family->Scene->GetRenderScene(),
			View.GetFeatureLevel(),
			&View,
			DrawRenderState,
			false,
			DynamicMeshPassContext);

		const uint64 DefaultBatchElementMask = ~0ull;
		const int32 NumDynamicEditorMeshBatches = View.DynamicEditorMeshElements.Num();

		for (int32 MeshIndex = 0; MeshIndex < NumDynamicEditorMeshBatches; MeshIndex++)
		{
			const FMeshBatchAndRelevance& MeshAndRelevance = View.DynamicEditorMeshElements[MeshIndex];
			check(!MeshAndRelevance.Mesh->bRequiresPerElementVisibility);

			if (MeshAndRelevance.GetHasOpaqueOrMaskedMaterial() || View.Family->EngineShowFlags.Wireframe)
			{
				PassMeshProcessor.AddMeshBatch(*MeshAndRelevance.Mesh, DefaultBatchElementMask, MeshAndRelevance.PrimitiveSceneProxy);
			}
		}

		for (int32 MeshIndex = 0; MeshIndex < View.ViewMeshElements.Num(); MeshIndex++)
		{
			const FMeshBatch& MeshBatch = View.ViewMeshElements[MeshIndex];
			PassMeshProcessor.AddMeshBatch(MeshBatch, DefaultBatchElementMask, nullptr);
		}
	});

	View.EditorSimpleElementCollector.DrawBatchedElements(RHICmdList, DrawRenderState, View, EBlendModeFilter::OpaqueAndMasked, SDPG_World);

	const auto FeatureLevel = View.GetFeatureLevel();
	const auto ShaderPlatform = GShaderPlatformForFeatureLevel[FeatureLevel];
	const bool bNeedToSwitchVerticalAxis = RHINeedsToSwitchVerticalAxis(ShaderPlatform);

	// Draw the view's batched simple elements(lines, sprites, etc).
	View.BatchedViewElements.Draw(RHICmdList, DrawRenderState, FeatureLevel, bNeedToSwitchVerticalAxis, View, false, 1.0f);
}

static void RenderForegroundEditorPrimitives(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, FMeshPassProcessorRenderState& DrawRenderState)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	const auto FeatureLevel = View.GetFeatureLevel();
	const auto ShaderPlatform = GShaderPlatformForFeatureLevel[FeatureLevel];
	const bool bNeedToSwitchVerticalAxis = RHINeedsToSwitchVerticalAxis(ShaderPlatform);

	// Draw a first time the foreground primitive without depth test to over right depth from non-foreground editor primitives.
	{
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_Always>::GetRHI());

		View.EditorSimpleElementCollector.DrawBatchedElements(RHICmdList, DrawRenderState, View, EBlendModeFilter::OpaqueAndMasked, SDPG_Foreground);

		DrawDynamicMeshPass(View, RHICmdList,
				[&View, &DrawRenderState](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
			{
				FEditorPrimitivesBasePassMeshProcessor PassMeshProcessor(
					View.Family->Scene->GetRenderScene(),
					View.GetFeatureLevel(),
					&View,
					DrawRenderState,
					false,
					DynamicMeshPassContext);

				const uint64 DefaultBatchElementMask = ~0ull;

				for (int32 MeshIndex = 0; MeshIndex < View.TopViewMeshElements.Num(); MeshIndex++)
				{
					const FMeshBatch& MeshBatch = View.TopViewMeshElements[MeshIndex];
					PassMeshProcessor.AddMeshBatch(MeshBatch, DefaultBatchElementMask, nullptr);
				}
			});

		View.TopBatchedViewElements.Draw(RHICmdList, DrawRenderState, FeatureLevel, bNeedToSwitchVerticalAxis, View, false);
	}

	// Draw a second time the foreground primitive with depth test to have proper depth test between foreground primitives.
	{
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());

		View.EditorSimpleElementCollector.DrawBatchedElements(RHICmdList, DrawRenderState, View, EBlendModeFilter::OpaqueAndMasked, SDPG_Foreground);

		DrawDynamicMeshPass(View, RHICmdList,
				[&View, &DrawRenderState](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
			{
				FEditorPrimitivesBasePassMeshProcessor PassMeshProcessor(
					View.Family->Scene->GetRenderScene(),
					View.GetFeatureLevel(),
					&View,
					DrawRenderState,
					false,
					DynamicMeshPassContext);

				const uint64 DefaultBatchElementMask = ~0ull;

				for (int32 MeshIndex = 0; MeshIndex < View.TopViewMeshElements.Num(); MeshIndex++)
				{
					const FMeshBatch& MeshBatch = View.TopViewMeshElements[MeshIndex];
					PassMeshProcessor.AddMeshBatch(MeshBatch, DefaultBatchElementMask, nullptr);
				}
			});

		View.TopBatchedViewElements.Draw(RHICmdList, DrawRenderState, FeatureLevel, bNeedToSwitchVerticalAxis, View, false);
	}
}

template <uint32 MSAASampleCount>
static void SetCompositePrimitivesShaderTempl(const FRenderingCompositePassContext& Context, bool bComposeAnyNonNullDepth)
{
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	// set the state
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	const auto FeatureLevel = Context.GetFeatureLevel();
	auto ShaderMap = Context.GetShaderMap();

	TShaderMapRef<FPostProcessVS> VertexShader(ShaderMap);
	TShaderMapRef<FPostProcessComposeEditorPrimitivesPS<MSAASampleCount> > PixelShader(ShaderMap);

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
	SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

	VertexShader->SetParameters(Context);
	PixelShader->SetParameters(Context, bComposeAnyNonNullDepth);
}


void FRCPassPostProcessCompositeEditorPrimitives::Process(FRenderingCompositePassContext& Context)
{
	FIntRect ViewRect = Context.SceneColorViewRect;

	SCOPED_DRAW_EVENTF(Context.RHICmdList, TemporalAA, TEXT("EditorPrimitives %dx%d"),
		ViewRect.Width(), ViewRect.Height());

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(Context.RHICmdList);

	// If we render wirframe we already started rendering to the EditorPrimitives buffer, so we don't want to clear it.
	bool bClearIsNeeded = !IsValidRef(SceneContext.EditorPrimitivesColor);

	// Get or create the msaa depth and color buffer
	FTexture2DRHIRef EditorColorTarget = SceneContext.GetEditorPrimitivesColor(Context.RHICmdList);
	FTexture2DRHIRef EditorDepthTarget = SceneContext.GetEditorPrimitivesDepth(Context.RHICmdList);

	const uint32 MSAASampleCount = SceneContext.EditorPrimitivesColor->GetDesc().NumSamples;

	FViewInfo& EditorView = *Context.View.CreateSnapshot();

	{
		// Patch view rect.
		EditorView.ViewRect = ViewRect;

		// Override pre exposure to 1.0f, because rendering after tonemapper. 
		EditorView.PreExposure = 1.0f;

		// Kills material texture mipbias because after TAA.
		EditorView.MaterialTextureMipBias = 0.0f;

		// Disable decals so that we don't do a SetDepthStencilState() in TMobileBasePassDrawingPolicy::SetupPipelineState()
		EditorView.bSceneHasDecals = false;

		if (EditorView.AntiAliasingMethod == AAM_TemporalAA)
		{
			EditorView.ViewMatrices.HackRemoveTemporalAAProjectionJitter();
		}

		EditorView.CachedViewUniformShaderParameters = MakeUnique<FViewUniformShaderParameters>();

		FBox VolumeBounds[TVC_MAX];
		EditorView.SetupUniformBufferParameters(SceneContext, VolumeBounds, TVC_MAX,*EditorView.CachedViewUniformShaderParameters);
		EditorView.CachedViewUniformShaderParameters->NumSceneColorMSAASamples = MSAASampleCount;

		FScene* Scene = Context.View.Family->Scene->GetRenderScene();
		Scene->UniformBuffers.UpdateViewUniformBufferImmediate(*EditorView.CachedViewUniformShaderParameters);
		EditorView.ViewUniformBuffer = Scene->UniformBuffers.ViewUniformBuffer;
	}

	const FPooledRenderTargetDesc* InputDesc = GetInputDesc(ePId_Input0);
	FIntPoint SrcSize = InputDesc->Extent;

	// Editor primitive is used when rendering VMI_WIREFRAME in order to use MSAA.
	// This mean we might not actually want to render composite editor primitives here.
	if (Context.View.Family->EngineShowFlags.CompositeEditorPrimitives)
	{
		FRHIRenderPassInfo RPInfo;
		RPInfo.ColorRenderTargets[0].RenderTarget = EditorColorTarget;
		RPInfo.DepthStencilRenderTarget.DepthStencilTarget = EditorDepthTarget;
		RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthWrite_StencilWrite;
		if (bClearIsNeeded)
		{
			RPInfo.ColorRenderTargets[0].Action = ERenderTargetActions::Clear_Store;
			RPInfo.DepthStencilRenderTarget.Action = EDepthStencilTargetActions::ClearDepthStencil_StoreDepthStencil;
		}
		else
		{
			RPInfo.ColorRenderTargets[0].Action = ERenderTargetActions::Load_Store;
			RPInfo.DepthStencilRenderTarget.Action = EDepthStencilTargetActions::LoadDepthStencil_StoreDepthStencil;
		}

		// It's possible to have no depth target here.
		if (!EditorDepthTarget)
		{
			RPInfo.DepthStencilRenderTarget.Action = EDepthStencilTargetActions::DontLoad_DontStore;
			RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthNop_StencilNop;
		}

		Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("CompositeEditorPrimitives"));
		{
			Context.SetViewportAndCallRHI(ViewRect);

			// Populate depth from scene depth.
			if (bClearIsNeeded)
			{
				SCOPED_DRAW_EVENTF(Context.RHICmdList, TemporalAA, TEXT("PopulateEditorPrimitivesDepthBuffer %dx%d msaa=%d"),
					ViewRect.Width(), ViewRect.Height(), MSAASampleCount);

				if (MSAASampleCount == 1)
				{
					SetPopulateSceneDepthForEditorPrimitivesShaderTempl<1>(Context);
				}
				else
				{
					SetPopulateSceneDepthForEditorPrimitivesShaderTempl<2>(Context);
				}

				TShaderMapRef<FPostProcessVS> VertexShader(Context.GetShaderMap());

				// Draw a quad mapping our render targets to the view's render target
				DrawRectangle(
					Context.RHICmdList,
					0, 0,
					ViewRect.Width(), ViewRect.Height(),
					Context.View.ViewRect.Min.X, Context.View.ViewRect.Min.Y,
					Context.View.ViewRect.Width(), Context.View.ViewRect.Height(),
					ViewRect.Size(),
					SrcSize,
					*VertexShader,
					EDRF_UseTriangleOptimization);
			}

			TUniformBufferRef<FOpaqueBasePassUniformParameters> OpaqueBasePassUniformBuffer;
			TUniformBufferRef<FMobileBasePassUniformParameters> MobileBasePassUniformBuffer;
			FRHIUniformBuffer* BasePassUniformBuffer = nullptr;

			if (bDeferredBasePass)
			{
				CreateOpaqueBasePassUniformBuffer(Context.RHICmdList, EditorView, nullptr, OpaqueBasePassUniformBuffer);
				BasePassUniformBuffer = OpaqueBasePassUniformBuffer;
			}
			else
			{
				CreateMobileBasePassUniformBuffer(Context.RHICmdList, EditorView, true, MobileBasePassUniformBuffer);
				BasePassUniformBuffer = MobileBasePassUniformBuffer;
			}

			FMeshPassProcessorRenderState DrawRenderState(EditorView, BasePassUniformBuffer);
			DrawRenderState.SetDepthStencilAccess(FExclusiveDepthStencil::DepthWrite_StencilWrite);
			DrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_RGBA>::GetRHI());

			RenderEditorPrimitives(Context.RHICmdList, EditorView, DrawRenderState);

			// Draw editor primitives.
			{
				SCOPED_DRAW_EVENTF(Context.RHICmdList, TemporalAA, TEXT("RenderViewEditorPrimitives %dx%d msaa=%d"),
					ViewRect.Width(), ViewRect.Height(), MSAASampleCount);

				RenderEditorPrimitives(Context.RHICmdList, EditorView, DrawRenderState);
			}

			// Draw foreground editor primitives.
			{
				SCOPED_DRAW_EVENTF(Context.RHICmdList, TemporalAA, TEXT("RenderViewEditorForegroundPrimitives %dx%d msaa=%d"),
					ViewRect.Width(), ViewRect.Height(), MSAASampleCount);

				RenderForegroundEditorPrimitives(Context.RHICmdList, EditorView, DrawRenderState);
			}
		}
		Context.RHICmdList.EndRenderPass();

		GVisualizeTexture.SetCheckPoint(Context.RHICmdList, SceneContext.EditorPrimitivesColor);

		FRHITexture* EditorRenderTargets[2];
		EditorRenderTargets[0] = EditorColorTarget;
		EditorRenderTargets[1] = EditorDepthTarget;

		Context.RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EditorRenderTargets, 2);
	}

	// Compose.
	{
		SCOPED_DRAW_EVENTF(Context.RHICmdList, TemporalAA, TEXT("ComposeViewEditorPrimitives %dx%d msaa=%d"),
			ViewRect.Width(), ViewRect.Height(), MSAASampleCount);

		const FSceneRenderTargetItem& DestRenderTarget = PassOutputs[0].RequestSurface(Context);
		const FTexture2DRHIRef& DestRenderTargetSurface = (const FTexture2DRHIRef&)DestRenderTarget.TargetableTexture;


		FIntRect DestRect = Context.GetSceneColorDestRect(DestRenderTarget);

		// Set the view family's render target/viewport.
		FRHIRenderPassInfo RPInfo(DestRenderTargetSurface, ERenderTargetActions::Load_Store);
		Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("ComposeEditorPrimitives"));
		{
			Context.SetViewportAndCallRHI(DestRect);

			// If clear is not needed, that mean already have something in MSAA buffers. Because not populating scene depth buffer
			// into MSAA depth buffer, then force to compose any sample that have non null depth as if alpha channel was 1.
			bool bComposeAnyNonNullDepth = !bClearIsNeeded;

			if (!bDeferredBasePass)
			{
				SetCompositePrimitivesShaderTempl<0>(Context, bComposeAnyNonNullDepth);
			}
			else if (MSAASampleCount == 1)
			{
				SetCompositePrimitivesShaderTempl<1>(Context, bComposeAnyNonNullDepth);
			}
			else if (MSAASampleCount == 2)
			{
				SetCompositePrimitivesShaderTempl<2>(Context, bComposeAnyNonNullDepth);
			}
			else if (MSAASampleCount == 4)
			{
				SetCompositePrimitivesShaderTempl<4>(Context, bComposeAnyNonNullDepth);
			}
			else if (MSAASampleCount == 8)
			{
				SetCompositePrimitivesShaderTempl<8>(Context, bComposeAnyNonNullDepth);
			}
			else
			{
				// not supported, internal error
				check(0);
			}

			TShaderMapRef<FPostProcessVS> VertexShader(Context.GetShaderMap());

			// Draw a quad mapping our render targets to the view's render target
			DrawRectangle(
				Context.RHICmdList,
				0, 0,
				DestRect.Width(), DestRect.Height(),
				ViewRect.Min.X, ViewRect.Min.Y,
				ViewRect.Width(), ViewRect.Height(),
				DestRect.Size(),
				SrcSize,
				*VertexShader,
				EDRF_UseTriangleOptimization);
		}
		Context.RHICmdList.EndRenderPass();
		Context.RHICmdList.CopyToResolveTarget(DestRenderTargetSurface, DestRenderTarget.ShaderResourceTexture, FResolveParams());
	}

	// Clean up targets
	SceneContext.CleanUpEditorPrimitiveTargets();
}

FPooledRenderTargetDesc FRCPassPostProcessCompositeEditorPrimitives::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret = GetInput(ePId_Input0)->GetOutput()->RenderTargetDesc;

	Ret.Reset();
	Ret.DebugName = TEXT("EditorPrimitives");

	return Ret;
}

#endif

