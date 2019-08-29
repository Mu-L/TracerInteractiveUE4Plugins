// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	TranslucentRendering.cpp: Translucent rendering implementation.
=============================================================================*/

#include "TranslucentRendering.h"
#include "DeferredShadingRenderer.h"
#include "BasePassRendering.h"
#include "DynamicPrimitiveDrawing.h"
#include "RendererModule.h"
#include "LightPropagationVolume.h"
#include "ScenePrivate.h"
#include "ScreenRendering.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PipelineStateCache.h"

DECLARE_CYCLE_STAT(TEXT("TranslucencyTimestampQueryFence Wait"), STAT_TranslucencyTimestampQueryFence_Wait, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("TranslucencyTimestampQuery Wait"), STAT_TranslucencyTimestampQuery_Wait, STATGROUP_SceneRendering);

DECLARE_FLOAT_COUNTER_STAT(TEXT("Translucency GPU Time (MS)"), STAT_TranslucencyGPU, STATGROUP_SceneRendering);

DECLARE_GPU_STAT(Translucency);

static TAutoConsoleVariable<float> CVarSeparateTranslucencyScreenPercentage(
	TEXT("r.SeparateTranslucencyScreenPercentage"),
	100.0f,
	TEXT("Render separate translucency at this percentage of the full resolution.\n")
	TEXT("in percent, >0 and <=100, larger numbers are possible (supersampling).")
	TEXT("<0 is treated like 100."),
	ECVF_Scalability | ECVF_Default);

static TAutoConsoleVariable<int32> CVarSeparateTranslucencyAutoDownsample(
	TEXT("r.SeparateTranslucencyAutoDownsample"),
	0,
	TEXT("Whether to automatically downsample separate translucency based on last frame's GPU time.\n")
	TEXT("Automatic downsampling is only used when r.SeparateTranslucencyScreenPercentage is 100"),
	ECVF_Scalability | ECVF_Default);

static TAutoConsoleVariable<float> CVarSeparateTranslucencyDurationDownsampleThreshold(
	TEXT("r.SeparateTranslucencyDurationDownsampleThreshold"),
	1.5f,
	TEXT("When smoothed full-res translucency GPU duration is larger than this value (ms), the entire pass will be downsampled by a factor of 2 in each dimension."),
	ECVF_Scalability | ECVF_Default);

static TAutoConsoleVariable<float> CVarSeparateTranslucencyDurationUpsampleThreshold(
	TEXT("r.SeparateTranslucencyDurationUpsampleThreshold"),
	.5f,
	TEXT("When smoothed half-res translucency GPU duration is smaller than this value (ms), the entire pass will be restored to full resolution.\n")
	TEXT("This should be around 1/4 of r.SeparateTranslucencyDurationDownsampleThreshold to avoid toggling downsampled state constantly."),
	ECVF_Scalability | ECVF_Default);

static TAutoConsoleVariable<float> CVarSeparateTranslucencyMinDownsampleChangeTime(
	TEXT("r.SeparateTranslucencyMinDownsampleChangeTime"),
	1.0f,
	TEXT("Minimum time in seconds between changes to automatic downsampling state, used to prevent rapid swapping between half and full res."),
	ECVF_Scalability | ECVF_Default);

static TAutoConsoleVariable<int32> CVarSeparateTranslucencyUpsampleMode(
	TEXT("r.SeparateTranslucencyUpsampleMode"),
	1,
	TEXT("Upsample method to use on separate translucency.  These are only used when r.SeparateTranslucencyScreenPercentage is less than 100.\n")
	TEXT("0: bilinear 1: Nearest-Depth Neighbor (only when r.SeparateTranslucencyScreenPercentage is 50)"),
	ECVF_Scalability | ECVF_Default);

int32 GAllowDownsampledStandardTranslucency = 0;

static FAutoConsoleVariableRef CVarAllowDownsampledStandardTranslucency(
	TEXT("r.AllowDownsampledStandardTranslucency"),
	GAllowDownsampledStandardTranslucency,
	TEXT("Allow standard translucency to be rendered in smaller resolution as an optimization\n")
	TEXT("This is incompatible with materials using blend modulate. Use 2 to ignore those. \n")
	TEXT(" <0: off\n")
	TEXT(" 0: on unless a material using blend modulate is used (default)")
	TEXT(" >0: on and ignores any material using blend modulate"),
	ECVF_RenderThreadSafe
	);

/** Mostly used to know if debug rendering should be drawn in this pass */
FORCEINLINE bool IsMainTranslucencyPass(ETranslucencyPass::Type TranslucencyPass)
{
	return TranslucencyPass == ETranslucencyPass::TPT_AllTranslucency || TranslucencyPass == ETranslucencyPass::TPT_StandardTranslucency;
}

static bool RenderInSeparateTranslucency(const FSceneRenderTargets& SceneContext, ETranslucencyPass::Type TranslucencyPass, bool bPrimitiveDisablesOffscreenBuffer)
{
	// Currently AfterDOF is rendered earlier in the frame and must be rendered in a separate (offscreen) buffer.
	if (TranslucencyPass == ETranslucencyPass::TPT_TranslucencyAfterDOF)
	{
		// If bPrimitiveDisablesOffscreenBuffer, that will trigger an ensure call
		return true;
	}
	
	// Otherwise it only gets rendered in the separate buffer if it is downsampled
	if (bPrimitiveDisablesOffscreenBuffer ? (GAllowDownsampledStandardTranslucency > 0) : (GAllowDownsampledStandardTranslucency >= 0))
	{
		FIntPoint ScaledSize;
		float DownsamplingScale = 1.f;
		SceneContext.GetSeparateTranslucencyDimensions(ScaledSize, DownsamplingScale);

		if (DownsamplingScale < 1.f)
		{
			return true;
		}
	}

	return false;
}

bool FTranslucencyDrawingPolicyFactory::ContextType::ShouldDraw(const FMaterial* Material, bool bIsSeparateTranslucency) const
{
	bool bShouldDraw = false;

	if (Material)
	{
		// Only render translucent materials
		const EBlendMode BlendMode = Material->GetBlendMode();
		if (IsTranslucentBlendMode(BlendMode) && ShouldIncludeDomainInMeshPass(Material->GetMaterialDomain()))
		{
			if (TranslucencyPass == ETranslucencyPass::TPT_AllTranslucency)
			{
				bShouldDraw = true;
			}
			// Only draw meshes in the relevant pass
			const ETranslucencyPass::Type MaterialPass = Material->IsTranslucencyAfterDOFEnabled() ? ETranslucencyPass::TPT_TranslucencyAfterDOF : ETranslucencyPass::TPT_StandardTranslucency;
			if (TranslucencyPass == MaterialPass)
			{
				bShouldDraw = true;
			}
		}

		if (bShouldDraw && BlendMode == BLEND_Modulate && bIsSeparateTranslucency)
		{
			// < 0 : never downsample, = 0 downsample only if no blend modulate, > 0 ignore
			ensure(GAllowDownsampledStandardTranslucency > 0);
	#if !UE_BUILD_SHIPPING
			if (GAllowDownsampledStandardTranslucency > 0)
			{
				static bool bOnce = false;
				if (!bOnce)
				{
					UE_LOG(LogRenderer, Warning, TEXT("Blend modulate materials (%s) are not supported when r.AllowDownsampledStandardTranslucency > 0."), *Material->GetFriendlyName());
					bOnce = true;
				}
			}
	#endif
		}

	}

	return bShouldDraw;
}

void FDeferredShadingSceneRenderer::UpdateTranslucencyTimersAndSeparateTranslucencyBufferSize(FRHICommandListImmediate& RHICmdList)
{
	bool bAnyViewWantsDownsampledSeparateTranslucency = false;
	bool bCVarSeparateTranslucencyAutoDownsample = CVarSeparateTranslucencyAutoDownsample.GetValueOnRenderThread() != 0;
#if (!STATS)
	if (bCVarSeparateTranslucencyAutoDownsample)
#endif
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			const FViewInfo& View = Views[ViewIndex];
			FSceneViewState* ViewState = View.ViewState;

			if (ViewState)
			{
				//We always tick the separate trans timer but only need the other timer for stats
				bool bSeparateTransTimerSuccess = ViewState->SeparateTranslucencyTimer.Tick(RHICmdList);
				if (STATS)
				{
					ViewState->TranslucencyTimer.Tick(RHICmdList);
					//Stats are fed the most recent available time and so are lagged a little. 
					float MostRecentTotalTime = ViewState->TranslucencyTimer.GetTimeMS() + ViewState->SeparateTranslucencyTimer.GetTimeMS();
					SET_FLOAT_STAT(STAT_TranslucencyGPU, MostRecentTotalTime);
				}
			
				if (bCVarSeparateTranslucencyAutoDownsample && bSeparateTransTimerSuccess)
				{
					float LastFrameTranslucencyDurationMS = ViewState->SeparateTranslucencyTimer.GetTimeMS();
					const bool bOriginalShouldAutoDownsampleTranslucency = ViewState->bShouldAutoDownsampleTranslucency;

					if (ViewState->bShouldAutoDownsampleTranslucency)
					{
						ViewState->SmoothedFullResTranslucencyGPUDuration = 0;
						const float LerpAlpha = ViewState->SmoothedHalfResTranslucencyGPUDuration == 0 ? 1.0f : .1f;
						ViewState->SmoothedHalfResTranslucencyGPUDuration = FMath::Lerp(ViewState->SmoothedHalfResTranslucencyGPUDuration, LastFrameTranslucencyDurationMS, LerpAlpha);

						// Don't re-asses switching for some time after the last switch
						if (View.Family->CurrentRealTime - ViewState->LastAutoDownsampleChangeTime > CVarSeparateTranslucencyMinDownsampleChangeTime.GetValueOnRenderThread())
						{
							// Downsample if the smoothed time is larger than the threshold
							ViewState->bShouldAutoDownsampleTranslucency = ViewState->SmoothedHalfResTranslucencyGPUDuration > CVarSeparateTranslucencyDurationUpsampleThreshold.GetValueOnRenderThread();

							if (!ViewState->bShouldAutoDownsampleTranslucency)
							{
								// Do 'log LogRenderer verbose' to get these
								UE_LOG(LogRenderer, Verbose, TEXT("Upsample: %.1fms < %.1fms"), ViewState->SmoothedHalfResTranslucencyGPUDuration, CVarSeparateTranslucencyDurationUpsampleThreshold.GetValueOnRenderThread());
							}
						}
					}
					else
					{
						ViewState->SmoothedHalfResTranslucencyGPUDuration = 0;
						const float LerpAlpha = ViewState->SmoothedFullResTranslucencyGPUDuration == 0 ? 1.0f : .1f;
						ViewState->SmoothedFullResTranslucencyGPUDuration = FMath::Lerp(ViewState->SmoothedFullResTranslucencyGPUDuration, LastFrameTranslucencyDurationMS, LerpAlpha);

						if (View.Family->CurrentRealTime - ViewState->LastAutoDownsampleChangeTime > CVarSeparateTranslucencyMinDownsampleChangeTime.GetValueOnRenderThread())
						{
							// Downsample if the smoothed time is larger than the threshold
							ViewState->bShouldAutoDownsampleTranslucency = ViewState->SmoothedFullResTranslucencyGPUDuration > CVarSeparateTranslucencyDurationDownsampleThreshold.GetValueOnRenderThread();

							if (ViewState->bShouldAutoDownsampleTranslucency)
							{
								UE_LOG(LogRenderer, Verbose, TEXT("Downsample: %.1fms > %.1fms"), ViewState->SmoothedFullResTranslucencyGPUDuration, CVarSeparateTranslucencyDurationDownsampleThreshold.GetValueOnRenderThread());
							}
						}
					}

					if (bOriginalShouldAutoDownsampleTranslucency != ViewState->bShouldAutoDownsampleTranslucency)
					{
						ViewState->LastAutoDownsampleChangeTime = View.Family->CurrentRealTime;
					}

					bAnyViewWantsDownsampledSeparateTranslucency = bAnyViewWantsDownsampledSeparateTranslucency || ViewState->bShouldAutoDownsampleTranslucency;
				}
			}
		}
	}

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	SceneContext.SetSeparateTranslucencyBufferSize(bAnyViewWantsDownsampledSeparateTranslucency);
}

void FDeferredShadingSceneRenderer::BeginTimingSeparateTranslucencyPass(FRHICommandListImmediate& RHICmdList, const FViewInfo& View)
{
	if (View.ViewState && GSupportsTimestampRenderQueries
#if !STATS
		&& (CVarSeparateTranslucencyAutoDownsample.GetValueOnRenderThread() != 0)
#endif
	)
	{
		View.ViewState->SeparateTranslucencyTimer.Begin(RHICmdList);
	}
}

void FDeferredShadingSceneRenderer::EndTimingSeparateTranslucencyPass(FRHICommandListImmediate& RHICmdList, const FViewInfo& View)
{
	if (View.ViewState && GSupportsTimestampRenderQueries
#if !STATS
		&& (CVarSeparateTranslucencyAutoDownsample.GetValueOnRenderThread() != 0)
#endif
	)
	{
		View.ViewState->SeparateTranslucencyTimer.End(RHICmdList);
	}
}

const FProjectedShadowInfo* FDeferredShadingSceneRenderer::PrepareTranslucentShadowMap(FRHICommandList& RHICmdList, const FViewInfo& View, FPrimitiveSceneInfo* PrimitiveSceneInfo, ETranslucencyPass::Type TranslucencyPass)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_PrepareTranslucentShadowMap);
	const FVisibleLightInfo* VisibleLightInfo = NULL;
	FProjectedShadowInfo* TranslucentSelfShadow = NULL;

	// Find this primitive's self shadow if there is one
	if (PrimitiveSceneInfo->Proxy && PrimitiveSceneInfo->Proxy->CastsVolumetricTranslucentShadow())
	{		
		for (FLightPrimitiveInteraction* Interaction = PrimitiveSceneInfo->LightList;
			Interaction && !TranslucentSelfShadow;
			Interaction = Interaction->GetNextLight()
			)
		{
			const FLightSceneInfo* LightSceneInfo = Interaction->GetLight();

			// Note: applying shadowmap from first directional light found
			if (LightSceneInfo->Proxy->GetLightType() == LightType_Directional)
			{
				VisibleLightInfo = &VisibleLightInfos[LightSceneInfo->Id];

				for (int32 ShadowIndex = 0, Count = VisibleLightInfo->AllProjectedShadows.Num(); ShadowIndex < Count; ShadowIndex++)
				{
					FProjectedShadowInfo* CurrentShadowInfo = VisibleLightInfo->AllProjectedShadows[ShadowIndex];

					if (CurrentShadowInfo 
						&& CurrentShadowInfo->bTranslucentShadow 
						&& CurrentShadowInfo->GetParentSceneInfo() == PrimitiveSceneInfo
						&& CurrentShadowInfo->ShadowDepthView)
					{
						check(CurrentShadowInfo->RenderTargets.ColorTargets.Num() > 0);
						TranslucentSelfShadow = CurrentShadowInfo;
						break;
					}
				}
			}
		}
	}

	return TranslucentSelfShadow;
}

/** Pixel shader used to copy scene color into another texture so that materials can read from scene color with a node. */
class FCopySceneColorPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FCopySceneColorPS,Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4); }

	FCopySceneColorPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
		SceneTextureParameters.Bind(Initializer);
	}
	FCopySceneColorPS() {}

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View)
	{
		SceneTextureParameters.Set(RHICmdList, GetPixelShader(), View.FeatureLevel, ESceneTextureSetupMode::All);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << SceneTextureParameters;
		return bShaderHasOutdatedParameters;
	}

private:
	FSceneTextureShaderParameters SceneTextureParameters;
};

IMPLEMENT_SHADER_TYPE(,FCopySceneColorPS,TEXT("/Engine/Private/TranslucentLightingShaders.usf"),TEXT("CopySceneColorMain"),SF_Pixel);

/** The parameters used to draw a translucent mesh. */
class FDrawTranslucentMeshAction
{
public:

	const FViewInfo& View;
	FDrawingPolicyRenderState DrawRenderState;
	const FProjectedShadowInfo* TranslucentSelfShadow;
	FHitProxyId HitProxyId;
	bool bUseTranslucentSelfShadowing;

	/** Initialization constructor. */
	FDrawTranslucentMeshAction(
		FRHICommandList& InRHICmdList,
		const FViewInfo& InView,
		const FDrawingPolicyRenderState& InDrawRenderState,
		FHitProxyId InHitProxyId,
		const FProjectedShadowInfo* InTranslucentSelfShadow,
		bool bInUseTranslucentSelfShadowing
		) :
		View(InView),
		DrawRenderState(InDrawRenderState),
		TranslucentSelfShadow(InTranslucentSelfShadow),
		HitProxyId(InHitProxyId),
		bUseTranslucentSelfShadowing(bInUseTranslucentSelfShadowing)
	{}

	bool UseTranslucentSelfShadowing() const 
	{ 
		return bUseTranslucentSelfShadowing;
	}

	const FProjectedShadowInfo* GetTranslucentSelfShadow() const
	{
		return TranslucentSelfShadow;
	}

	bool AllowIndirectLightingCache() const
	{
		const FScene* Scene = (const FScene*)View.Family->Scene;
		return View.Family->EngineShowFlags.IndirectLightingCache && Scene && Scene->PrecomputedLightVolumes.Num() > 0;
	}

	bool AllowIndirectLightingCacheVolumeTexture() const
	{
		// This will force the cheaper single sample interpolated GI path
		return false;
	}
	
	bool UseVolumetricLightmap() const
	{
		const FScene* Scene = (const FScene*)View.Family->Scene;
		return View.Family->EngineShowFlags.VolumetricLightmap && Scene && Scene->VolumetricLightmapSceneData.HasData();
	}

	/** Draws the translucent mesh with a specific light-map type, and fog volume type */
	template<typename LightMapPolicyType>
	void Process(
		FRHICommandList& RHICmdList, 
		const FProcessBasePassMeshParameters& Parameters,
		const LightMapPolicyType& LightMapPolicy,
		const typename LightMapPolicyType::ElementDataType& LightMapElementData
		)
	{
		const bool bIsLitMaterial = Parameters.ShadingModel != MSM_Unlit;

		const FScene* Scene = Parameters.PrimitiveSceneProxy ? Parameters.PrimitiveSceneProxy->GetPrimitiveSceneInfo()->Scene : NULL;

		const bool bRenderSkylight = Scene && Scene->ShouldRenderSkylightInBasePass(Parameters.BlendMode) && bIsLitMaterial;
		const bool bRenderAtmosphericFog =(Scene && Scene->HasAtmosphericFog() && Scene->ReadOnlyCVARCache.bEnableAtmosphericFog) && View.Family->EngineShowFlags.AtmosphericFog && View.Family->EngineShowFlags.Fog;

		TBasePassDrawingPolicy<LightMapPolicyType> DrawingPolicy(
			Parameters.Mesh.VertexFactory,
			Parameters.Mesh.MaterialRenderProxy,
			*Parameters.Material,
			Parameters.FeatureLevel,
			LightMapPolicy,
			Parameters.BlendMode,			
			bRenderSkylight,
			bRenderAtmosphericFog,
			ComputeMeshOverrideSettings(Parameters.Mesh),
			View.Family->GetDebugViewShaderMode(),
			false);

		DrawingPolicy.SetupPipelineState(DrawRenderState, View);
		CommitGraphicsPipelineState(RHICmdList, DrawingPolicy, DrawRenderState, DrawingPolicy.GetBoundShaderStateInput(View.GetFeatureLevel()));
		DrawingPolicy.SetSharedState(RHICmdList, DrawRenderState, &View, typename TBasePassDrawingPolicy<LightMapPolicyType>::ContextDataType(Parameters.bIsInstancedStereo));

		int32 BatchElementIndex = 0;
		uint64 BatchElementMask = Parameters.BatchElementMask;
		do
		{
			if (BatchElementMask & 1)
			{
				const bool bIsInstanceMesh = Parameters.Mesh.Elements[BatchElementIndex].bIsInstancedMesh;
				const uint32 InstancedStereoDrawCount = (Parameters.bIsInstancedStereo && bIsInstanceMesh) ? 2 : 1;
				for (uint32 DrawCountIter = 0; DrawCountIter < InstancedStereoDrawCount; ++DrawCountIter)
				{
					DrawingPolicy.SetInstancedEyeIndex(RHICmdList, DrawCountIter);

					TDrawEvent<FRHICommandList> MeshEvent;
					BeginMeshDrawEvent(RHICmdList, Parameters.PrimitiveSceneProxy, Parameters.Mesh, MeshEvent, EnumHasAnyFlags(EShowMaterialDrawEventTypes(GShowMaterialDrawEventTypes), EShowMaterialDrawEventTypes::Translucent));

					DrawingPolicy.SetMeshRenderState(
						RHICmdList,
						View,
						Parameters.PrimitiveSceneProxy,
						Parameters.Mesh,
						BatchElementIndex,
						DrawRenderState,
						typename TBasePassDrawingPolicy<LightMapPolicyType>::ElementDataType(LightMapElementData),
						typename TBasePassDrawingPolicy<LightMapPolicyType>::ContextDataType()
					);

					DrawingPolicy.DrawMesh(RHICmdList, View, Parameters.Mesh, BatchElementIndex, Parameters.bIsInstancedStereo);
				}
			}

			BatchElementMask >>= 1;
			BatchElementIndex++;
		} while (BatchElementMask);
	}
};

/**
* Render a dynamic or static mesh using a translucent draw policy
* @return true if the mesh rendered
*/
bool FTranslucencyDrawingPolicyFactory::DrawMesh(
	FRHICommandList& RHICmdList,
	const FViewInfo& View,
	ContextType DrawingContext,
	const FMeshBatch& Mesh,
	const uint64& BatchElementMask,
	const FDrawingPolicyRenderState& DrawRenderState,
	bool bPreFog,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	FHitProxyId HitProxyId
	)
{
	bool bDirty = false;
	const auto FeatureLevel = View.GetFeatureLevel();

	const FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	// Determine the mesh's material and blend mode.
	const FMaterial* Material = Mesh.MaterialRenderProxy->GetMaterial(FeatureLevel);

	// Only render relevant materials
	if (DrawingContext.ShouldDraw(Material, SceneContext.IsSeparateTranslucencyPass()))
	{
		FDrawingPolicyRenderState DrawRenderStateLocal(DrawRenderState);

		const bool bDisableDepthTest = Material->ShouldDisableDepthTest();
		const bool bEnableResponsiveAA = Material->ShouldEnableResponsiveAA();

		// if this draw is coming postAA then there is probably no depth buffer (it's canvas) and bEnableResponsiveAA wont' do anything anyway.
		if (bEnableResponsiveAA && !DrawingContext.bPostAA)
		{
			if( bDisableDepthTest )
			{
				DrawRenderStateLocal.SetDepthStencilState(TStaticDepthStencilState<
					false, CF_Always,
					true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
					false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
					STENCIL_TEMPORAL_RESPONSIVE_AA_MASK, STENCIL_TEMPORAL_RESPONSIVE_AA_MASK
					>::GetRHI());
				DrawRenderStateLocal.SetStencilRef(STENCIL_TEMPORAL_RESPONSIVE_AA_MASK);
			}
			else
			{
				DrawRenderStateLocal.SetDepthStencilState(TStaticDepthStencilState<
					false, CF_DepthNearOrEqual,
					true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
					false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
					STENCIL_TEMPORAL_RESPONSIVE_AA_MASK, STENCIL_TEMPORAL_RESPONSIVE_AA_MASK
					>::GetRHI());
				DrawRenderStateLocal.SetStencilRef(STENCIL_TEMPORAL_RESPONSIVE_AA_MASK);
			}
		}
		else if( bDisableDepthTest )
		{
			DrawRenderStateLocal.SetDepthStencilState(TStaticDepthStencilState<false,CF_Always>::GetRHI());
		}

		FIntPoint OutScaledSize;
		float OutScale;
		SceneContext.GetSeparateTranslucencyDimensions(OutScaledSize, OutScale);

		ProcessBasePassMesh(
			RHICmdList, 
			FProcessBasePassMeshParameters(
				Mesh,
				BatchElementMask,
				Material,
				PrimitiveSceneProxy, 
				!bPreFog,
				FeatureLevel,
				View.IsInstancedStereoPass()
			),
			FDrawTranslucentMeshAction(
				RHICmdList,
				View,
				DrawRenderStateLocal,
				HitProxyId,
				DrawingContext.TranslucentSelfShadow,
				PrimitiveSceneProxy && PrimitiveSceneProxy->CastsVolumetricTranslucentShadow()
			)
		);

		bDirty = true;
	}
	return bDirty;
}


/**
 * Render a dynamic mesh using a translucent draw policy
 * @return true if the mesh rendered
 */
bool FTranslucencyDrawingPolicyFactory::DrawDynamicMesh(
	FRHICommandList& RHICmdList, 
	const FViewInfo& View,
	ContextType DrawingContext,
	const FMeshBatch& Mesh,
	bool bPreFog,
	const FDrawingPolicyRenderState& DrawRenderState,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	FHitProxyId HitProxyId
	)
{
	FDrawingPolicyRenderState DrawRenderStateLocal(DrawRenderState);
	DrawRenderStateLocal.SetDitheredLODTransitionAlpha(Mesh.DitheredLODTransitionAlpha);

	return DrawMesh(
		RHICmdList,
		View,
		DrawingContext,
		Mesh,
		Mesh.Elements.Num() == 1 ? 1 : (1 << Mesh.Elements.Num()) - 1,	// 1 bit set for each mesh element
		DrawRenderStateLocal,
		bPreFog,
		PrimitiveSceneProxy,
		HitProxyId
		);
}

/**
 * Render a static mesh using a translucent draw policy
 * @return true if the mesh rendered
 */
bool FTranslucencyDrawingPolicyFactory::DrawStaticMesh(
	FRHICommandList& RHICmdList, 
	const FViewInfo& View,
	ContextType DrawingContext,
	const FStaticMesh& StaticMesh,
	const uint64& BatchElementMask,
	bool bPreFog,
	const FDrawingPolicyRenderState& DrawRenderState,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	FHitProxyId HitProxyId
	)
{
	FDrawingPolicyRenderState DrawRenderStateLocal(DrawRenderState);
	FMeshDrawingPolicy::OnlyApplyDitheredLODTransitionState(DrawRenderStateLocal, View, StaticMesh, false);

	return DrawMesh(
		RHICmdList,
		View,
		DrawingContext,
		StaticMesh,
		BatchElementMask,
		DrawRenderStateLocal,
		bPreFog,
		PrimitiveSceneProxy,
		HitProxyId
		);
}

/*-----------------------------------------------------------------------------
FTranslucentPrimSet
-----------------------------------------------------------------------------*/

void FTranslucentPrimSet::DrawAPrimitive(
	FRHICommandList& RHICmdList,
	const FViewInfo& View,
	const FDrawingPolicyRenderState& DrawRenderState,
	FDeferredShadingSceneRenderer& Renderer,
	ETranslucencyPass::Type TranslucencyPass,
	int32 PrimIdx
	) const
{
	FPrimitiveSceneInfo* PrimitiveSceneInfo = SortedPrims[PrimIdx].PrimitiveSceneInfo;
	int32 PrimitiveId = PrimitiveSceneInfo->GetIndex();
	const FPrimitiveViewRelevance& ViewRelevance = View.PrimitiveViewRelevanceMap[PrimitiveId];

	checkSlow(ViewRelevance.HasTranslucency());

	const FProjectedShadowInfo* TranslucentSelfShadow = Renderer.PrepareTranslucentShadowMap(RHICmdList, View, PrimitiveSceneInfo, TranslucencyPass);

	RenderPrimitive(RHICmdList, View, DrawRenderState, PrimitiveSceneInfo, ViewRelevance, TranslucentSelfShadow, TranslucencyPass);
}

class FVolumetricTranslucentShadowRenderThreadTask
{
	FRHICommandList& RHICmdList;
	const FTranslucentPrimSet &PrimSet;
	const FViewInfo& View;
	FDrawingPolicyRenderState DrawRenderState;
	FDeferredShadingSceneRenderer& Renderer;
	ETranslucencyPass::Type TranslucencyPass;
	int32 Index;

public:

	FORCEINLINE_DEBUGGABLE FVolumetricTranslucentShadowRenderThreadTask(FRHICommandList& InRHICmdList, const FTranslucentPrimSet& InPrimSet, const FViewInfo& InView, const FDrawingPolicyRenderState& InDrawRenderState, FDeferredShadingSceneRenderer& InRenderer, ETranslucencyPass::Type InTranslucencyPass, int32 InIndex)
		: RHICmdList(InRHICmdList)
		, PrimSet(InPrimSet)
		, View(InView)
		, DrawRenderState(InDrawRenderState)
		, Renderer(InRenderer)
		, TranslucencyPass(InTranslucencyPass)
		, Index(InIndex)
	{
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FVolumetricTranslucentShadowRenderThreadTask, STATGROUP_TaskGraphTasks);
	}

	ENamedThreads::Type GetDesiredThread()
	{
		return ENamedThreads::GetRenderThread_Local();
	}

	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		// Never needs clear here as it is already done in RenderTranslucency.
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		if (SceneContext.IsSeparateTranslucencyPass())
		{
			SceneContext.BeginRenderingSeparateTranslucency(RHICmdList, View, Renderer, false);
		}
		else
		{
			SceneContext.BeginRenderingTranslucency(RHICmdList, View, Renderer, false);
		}

		PrimSet.DrawAPrimitive(RHICmdList, View, DrawRenderState, Renderer, TranslucencyPass, Index);
		RHICmdList.HandleRTThreadTaskCompletion(MyCompletionGraphEvent);
	}
};

void FTranslucentPrimSet::DrawPrimitivesParallel(
	FRHICommandList& RHICmdList,
	const FViewInfo& View,
	const FDrawingPolicyRenderState& DrawRenderState,
	FDeferredShadingSceneRenderer& Renderer,
	ETranslucencyPass::Type TranslucencyPass,
	int32 FirstPrimIdx, int32 LastPrimIdx
	) const
{
	// Draw sorted scene prims
	for (int32 PrimIdx = FirstPrimIdx; PrimIdx <= LastPrimIdx; PrimIdx++)
	{
		FPrimitiveSceneInfo* PrimitiveSceneInfo = SortedPrims[PrimIdx].PrimitiveSceneInfo;
		int32 PrimitiveId = PrimitiveSceneInfo->GetIndex();
		const FPrimitiveViewRelevance& ViewRelevance = View.PrimitiveViewRelevanceMap[PrimitiveId];

		checkSlow(ViewRelevance.HasTranslucency());

		if (PrimitiveSceneInfo->Proxy && PrimitiveSceneInfo->Proxy->CastsVolumetricTranslucentShadow())
		{
			check(!IsInActualRenderingThread());
			// can't do this in parallel, defer
			FRHICommandList* CmdList = new FRHICommandList(View.GPUMask);
			CmdList->CopyRenderThreadContexts(RHICmdList);
			FGraphEventRef RenderThreadCompletionEvent = TGraphTask<FVolumetricTranslucentShadowRenderThreadTask>::CreateTask().ConstructAndDispatchWhenReady(*CmdList, *this, View, DrawRenderState, Renderer, TranslucencyPass, PrimIdx);
			RHICmdList.QueueRenderThreadCommandListSubmit(RenderThreadCompletionEvent, CmdList);
		}
		else
		{
			RenderPrimitive(RHICmdList, View, DrawRenderState, PrimitiveSceneInfo, ViewRelevance, nullptr, TranslucencyPass);
		}
	}
}

void FTranslucentPrimSet::DrawPrimitives(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	const FDrawingPolicyRenderState& DrawRenderState,
	FDeferredShadingSceneRenderer& Renderer,
	ETranslucencyPass::Type TranslucencyPass
	) const
{
	FInt32Range PassRange = SortedPrimsNum.GetPassRange(TranslucencyPass);

	// Draw sorted scene prims
	for( int32 PrimIdx = PassRange.GetLowerBoundValue(); PrimIdx < PassRange.GetUpperBoundValue(); PrimIdx++ )
	{
		FPrimitiveSceneInfo* PrimitiveSceneInfo = SortedPrims[PrimIdx].PrimitiveSceneInfo;
		int32 PrimitiveId = PrimitiveSceneInfo->GetIndex();
		const FPrimitiveViewRelevance& ViewRelevance = View.PrimitiveViewRelevanceMap[PrimitiveId];

		checkSlow(ViewRelevance.HasTranslucency());
			
		const FProjectedShadowInfo* TranslucentSelfShadow = Renderer.PrepareTranslucentShadowMap(RHICmdList, View, PrimitiveSceneInfo, TranslucencyPass);

		RenderPrimitive(RHICmdList, View, DrawRenderState, PrimitiveSceneInfo, ViewRelevance, TranslucentSelfShadow, TranslucencyPass);
	}

	View.SimpleElementCollector.DrawBatchedElements(RHICmdList, DrawRenderState, View, EBlendModeFilter::Translucent);
}

void FTranslucentPrimSet::RenderPrimitive(
	FRHICommandList& RHICmdList,
	const FViewInfo& View,
	const FDrawingPolicyRenderState& DrawRenderState,
	FPrimitiveSceneInfo* PrimitiveSceneInfo,
	const FPrimitiveViewRelevance& ViewRelevance,
	const FProjectedShadowInfo* TranslucentSelfShadow,
	ETranslucencyPass::Type TranslucencyPass) const
{
	checkSlow(ViewRelevance.HasTranslucency());
	auto FeatureLevel = View.GetFeatureLevel();

	if (ViewRelevance.bDrawRelevance)
	{
		FTranslucencyDrawingPolicyFactory::ContextType Context(TranslucentSelfShadow, TranslucencyPass);

		// Render dynamic scene prim
		{	
			// range in View.DynamicMeshElements[]
			FInt32Range range = View.GetDynamicMeshElementRange(PrimitiveSceneInfo->GetIndex());

			for (int32 MeshBatchIndex = range.GetLowerBoundValue(); MeshBatchIndex < range.GetUpperBoundValue(); MeshBatchIndex++)
			{
				const FMeshBatchAndRelevance& MeshBatchAndRelevance = View.DynamicMeshElements[MeshBatchIndex];

				checkSlow(MeshBatchAndRelevance.PrimitiveSceneProxy == PrimitiveSceneInfo->Proxy);

				const FMeshBatch& MeshBatch = *MeshBatchAndRelevance.Mesh;
				FTranslucencyDrawingPolicyFactory::DrawDynamicMesh(RHICmdList, View, Context, MeshBatch, false, DrawRenderState, MeshBatchAndRelevance.PrimitiveSceneProxy, MeshBatch.BatchHitProxyId);
			}
		}

		// Render static scene prim
		if (ViewRelevance.bStaticRelevance)
		{
			// Render static meshes from static scene prim
			for (int32 StaticMeshIdx = 0, Count = PrimitiveSceneInfo->StaticMeshes.Num(); StaticMeshIdx < Count; StaticMeshIdx++)
			{
				FStaticMesh& StaticMesh = PrimitiveSceneInfo->StaticMeshes[StaticMeshIdx];

				// Only render visible elements with relevant materials
				if (View.StaticMeshVisibilityMap[StaticMesh.Id] && Context.ShouldDraw(StaticMesh.MaterialRenderProxy->GetMaterial(FeatureLevel), FSceneRenderTargets::Get(RHICmdList).IsSeparateTranslucencyPass()))
				{
					FTranslucencyDrawingPolicyFactory::DrawStaticMesh(
						RHICmdList,
						View,
						Context,
						StaticMesh,
						StaticMesh.bRequiresPerElementVisibility ? View.StaticMeshBatchVisibility[StaticMesh.BatchVisibilityId] : ((1ull << StaticMesh.Elements.Num()) - 1),
						false,
						DrawRenderState,
						PrimitiveSceneInfo->Proxy,
						StaticMesh.BatchHitProxyId
						);
				}
			}
		}
	}
}

inline float CalculateTranslucentSortKey(FPrimitiveSceneInfo* PrimitiveSceneInfo, const FViewInfo& ViewInfo)
{
	float SortKey = 0.0f;
	if (ViewInfo.TranslucentSortPolicy == ETranslucentSortPolicy::SortByDistance)
	{
		//sort based on distance to the view position, view rotation is not a factor
		SortKey = (PrimitiveSceneInfo->Proxy->GetBounds().Origin - ViewInfo.ViewMatrices.GetViewOrigin()).Size();
		// UE4_TODO: also account for DPG in the sort key.
	}
	else if (ViewInfo.TranslucentSortPolicy == ETranslucentSortPolicy::SortAlongAxis)
	{
		// Sort based on enforced orthogonal distance
		const FVector CameraToObject = PrimitiveSceneInfo->Proxy->GetBounds().Origin - ViewInfo.ViewMatrices.GetViewOrigin();
		SortKey = FVector::DotProduct(CameraToObject, ViewInfo.TranslucentSortAxis);
	}
	else
	{
		// Sort based on projected Z distance
		check(ViewInfo.TranslucentSortPolicy == ETranslucentSortPolicy::SortByProjectedZ);
		SortKey = ViewInfo.ViewMatrices.GetViewMatrix().TransformPosition(PrimitiveSceneInfo->Proxy->GetBounds().Origin).Z;
	}

	return SortKey;
}

void FTranslucentPrimSet::AppendScenePrimitives(FTranslucentSortedPrim* Elements, int32 Num, const FTranslucenyPrimCount& TranslucentPrimitiveCountPerPass)
{
	SortedPrims.Append(Elements, Num);
	SortedPrimsNum.Append(TranslucentPrimitiveCountPerPass);
}

void FTranslucentPrimSet::PlaceScenePrimitive(FPrimitiveSceneInfo* PrimitiveSceneInfo, const FViewInfo& ViewInfo, const FPrimitiveViewRelevance& ViewRelevance,
	FTranslucentPrimSet::FTranslucentSortedPrim *InArrayStart, int32& InOutArrayNum, FTranslucenyPrimCount& OutCount)
{
	const float SortKey = CalculateTranslucentSortKey(PrimitiveSceneInfo, ViewInfo);
	const auto FeatureLevel = ViewInfo.GetFeatureLevel();

	if (ViewInfo.Family->AllowTranslucencyAfterDOF())
	{
		if (ViewRelevance.bNormalTranslucencyRelevance)
		{
			new(&InArrayStart[InOutArrayNum++]) FTranslucentSortedPrim(PrimitiveSceneInfo, ETranslucencyPass::TPT_StandardTranslucency, PrimitiveSceneInfo->Proxy->GetTranslucencySortPriority(), SortKey);
			OutCount.Add(ETranslucencyPass::TPT_StandardTranslucency, ViewRelevance.bUsesSceneColorCopy, ViewRelevance.bDisableOffscreenRendering);
		}

		if (ViewRelevance.bSeparateTranslucencyRelevance)
		{
			new(&InArrayStart[InOutArrayNum++]) FTranslucentSortedPrim(PrimitiveSceneInfo, ETranslucencyPass::TPT_TranslucencyAfterDOF, PrimitiveSceneInfo->Proxy->GetTranslucencySortPriority(), SortKey);
			OutCount.Add(ETranslucencyPass::TPT_TranslucencyAfterDOF, ViewRelevance.bUsesSceneColorCopy, ViewRelevance.bDisableOffscreenRendering);
		}
	}
	else // Otherwise, everything is rendered in a single bucket. This is not related to whether DOF is currently enabled or not.
	{
		// When using all translucency, Standard and AfterDOF are sorted together instead of being rendered like 2 buckets.
		new(&InArrayStart[InOutArrayNum++]) FTranslucentSortedPrim(PrimitiveSceneInfo, ETranslucencyPass::TPT_AllTranslucency, PrimitiveSceneInfo->Proxy->GetTranslucencySortPriority(), SortKey);
		OutCount.Add(ETranslucencyPass::TPT_AllTranslucency, ViewRelevance.bUsesSceneColorCopy, ViewRelevance.bDisableOffscreenRendering);
	}
}

void FTranslucentPrimSet::SortPrimitives()
{
	// sort prims based on the specified criteria (usually depth)
	SortedPrims.Sort( FCompareFTranslucentSortedPrim() );
}

extern int32 GLightShaftRenderAfterDOF;

bool FSceneRenderer::ShouldRenderTranslucency(ETranslucencyPass::Type TranslucencyPass) const
{
	// Change this condition to control where simple elements should be rendered.
	if (IsMainTranslucencyPass(TranslucencyPass))
	{
		if (ViewFamily.EngineShowFlags.VisualizeLPV)
		{
			return true;
		}

		for (const FViewInfo& View : Views)
		{
			if (View.bHasTranslucentViewMeshElements || View.SimpleElementCollector.BatchedElements.HasPrimsToDraw())
			{
				return true;
			}
		}
	}

	// If lightshafts are rendered in low res, we must reset the offscreen buffer in case is was also used in TPT_StandardTranslucency.
	if (GLightShaftRenderAfterDOF && TranslucencyPass == ETranslucencyPass::TPT_TranslucencyAfterDOF)
	{
		return true;
	}

	for (const FViewInfo& View : Views)
	{
		if (View.TranslucentPrimSet.SortedPrimsNum.Num(TranslucencyPass) > 0)
		{
			return true;
		}
	}

	return false;
}

class FDrawSortedTransAnyThreadTask : public FRenderTask
{
	FDeferredShadingSceneRenderer& Renderer;
	FRHICommandList& RHICmdList;
	const FViewInfo& View;
	FDrawingPolicyRenderState DrawRenderState;
	ETranslucencyPass::Type TranslucencyPass;

	const int32 FirstIndex;
	const int32 LastIndex;

public:

	FDrawSortedTransAnyThreadTask(
		FDeferredShadingSceneRenderer& InRenderer,
		FRHICommandList& InRHICmdList,
		const FViewInfo& InView,
		const FDrawingPolicyRenderState& InDrawRenderState,
		ETranslucencyPass::Type InTranslucencyPass,
		int32 InFirstIndex,
		int32 InLastIndex
		)
		: Renderer(InRenderer)
		, RHICmdList(InRHICmdList)
		, View(InView)
		, DrawRenderState(InDrawRenderState)
		, TranslucencyPass(InTranslucencyPass)
		, FirstIndex(InFirstIndex)
		, LastIndex(InLastIndex)
	{
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FDrawSortedTransAnyThreadTask, STATGROUP_TaskGraphTasks);
	}

	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		FScopeCycleCounter ScopeOuter(RHICmdList.ExecuteStat);
		View.TranslucentPrimSet.DrawPrimitivesParallel(RHICmdList, View, DrawRenderState, Renderer, TranslucencyPass, FirstIndex, LastIndex);
		RHICmdList.HandleRTThreadTaskCompletion(MyCompletionGraphEvent);
	}
};

DECLARE_CYCLE_STAT(TEXT("Translucency"), STAT_CLP_Translucency, STATGROUP_ParallelCommandListMarkers);


class FTranslucencyPassParallelCommandListSet : public FParallelCommandListSet
{
	ETranslucencyPass::Type TranslucencyPass;
	bool bRenderInSeparateTranslucency;

public:
	FTranslucencyPassParallelCommandListSet(const FViewInfo& InView, const FSceneRenderer* InSceneRenderer, FRHICommandListImmediate& InParentCmdList, bool bInParallelExecute, bool bInCreateSceneContext, const FDrawingPolicyRenderState& InDrawRenderState, ETranslucencyPass::Type InTranslucencyPass, bool InRenderInSeparateTranslucency)
		: FParallelCommandListSet(GET_STATID(STAT_CLP_Translucency), InView, InSceneRenderer, InParentCmdList, bInParallelExecute, bInCreateSceneContext, InDrawRenderState)
		, TranslucencyPass(InTranslucencyPass)
		, bRenderInSeparateTranslucency(InRenderInSeparateTranslucency)
	{
		SetStateOnCommandList(ParentCmdList);
	}

	virtual ~FTranslucencyPassParallelCommandListSet()
	{
		Dispatch();
	}

	virtual void SetStateOnCommandList(FRHICommandList& CmdList) override
	{
		// Never needs clear here as it is already done in RenderTranslucency.
		FParallelCommandListSet::SetStateOnCommandList(CmdList);
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(CmdList);
		if (bRenderInSeparateTranslucency)
		{
			SceneContext.BeginRenderingSeparateTranslucency(CmdList, View, *SceneRenderer, false);
		}
		else
		{
			SceneContext.BeginRenderingTranslucency(CmdList, View, *SceneRenderer, false);
		}
	}
};

static TAutoConsoleVariable<int32> CVarRHICmdTranslucencyPassDeferredContexts(
	TEXT("r.RHICmdTranslucencyPassDeferredContexts"),
	1,
	TEXT("True to use deferred contexts to parallelize base pass command list execution."));

static TAutoConsoleVariable<int32> CVarRHICmdFlushRenderThreadTasksTranslucentPass(
	TEXT("r.RHICmdFlushRenderThreadTasksTranslucentPass"),
	0,
	TEXT("Wait for completion of parallel render thread tasks at the end of the translucent pass.  A more granular version of r.RHICmdFlushRenderThreadTasks. If either r.RHICmdFlushRenderThreadTasks or r.RHICmdFlushRenderThreadTasksTranslucentPass is > 0 we will flush."));


static TAutoConsoleVariable<int32> CVarParallelTranslucency(
	TEXT("r.ParallelTranslucency"),
	1,
	TEXT("Toggles parallel translucency rendering. Parallel rendering must be enabled for this to have an effect."),
	ECVF_RenderThreadSafe
	);

// this is a static because we let the async tasks beyond the function. Using all translucency as we want all materials to render
static FTranslucencyDrawingPolicyFactory::ContextType GParallelTranslucencyContext(nullptr, ETranslucencyPass::TPT_AllTranslucency);


void FDeferredShadingSceneRenderer::RenderViewTranslucency(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, const FDrawingPolicyRenderState& DrawRenderState, ETranslucencyPass::Type TranslucencyPass)
{
	SCOPED_GPU_MASK(RHICmdList, View.GPUMask);

	// Draw translucent prims
	View.TranslucentPrimSet.DrawPrimitives(RHICmdList, View, DrawRenderState, *this, TranslucencyPass);

	if (IsMainTranslucencyPass(TranslucencyPass))
	{
		View.SimpleElementCollector.DrawBatchedElements(RHICmdList, DrawRenderState, View, EBlendModeFilter::Translucent);

		// editor and debug rendering
		if (View.bHasTranslucentViewMeshElements)
		{
			FTranslucencyDrawingPolicyFactory::ContextType Context(0, TranslucencyPass);
			DrawViewElements<FTranslucencyDrawingPolicyFactory>(RHICmdList, View, DrawRenderState, Context, SDPG_World, false);
			DrawViewElements<FTranslucencyDrawingPolicyFactory>(RHICmdList, View, DrawRenderState, Context, SDPG_Foreground, false);
		}
			
		const FSceneViewState* ViewState = (const FSceneViewState*)View.State;
		if (ViewState && View.Family->EngineShowFlags.VisualizeLPV)
		{
			FLightPropagationVolume* LightPropagationVolume = ViewState->GetLightPropagationVolume(View.GetFeatureLevel());

			if (LightPropagationVolume)
			{
				LightPropagationVolume->Visualise(RHICmdList, View);
			}
		}
	}
}
void FDeferredShadingSceneRenderer::RenderViewTranslucencyParallel(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, const FDrawingPolicyRenderState& DrawRenderState, ETranslucencyPass::Type TranslucencyPass)
{
	SCOPED_GPU_MASK(RHICmdList, View.GPUMask);

	FTranslucencyPassParallelCommandListSet ParallelCommandListSet(
		View, 
		this,
		RHICmdList, 
		CVarRHICmdTranslucencyPassDeferredContexts.GetValueOnRenderThread() > 0, 
		CVarRHICmdFlushRenderThreadTasksTranslucentPass.GetValueOnRenderThread() == 0  && CVarRHICmdFlushRenderThreadTasks.GetValueOnRenderThread() == 0, 
		DrawRenderState,
		TranslucencyPass,
		FSceneRenderTargets::Get(RHICmdList).IsSeparateTranslucencyPass()
		);

	{
		QUICK_SCOPE_CYCLE_COUNTER(RenderTranslucencyParallel_Start_FDrawSortedTransAnyThreadTask);

		FInt32Range PassRange = View.TranslucentPrimSet.SortedPrimsNum.GetPassRange(TranslucencyPass);
		int32 NumPrims = PassRange.Size<int32>();
		int32 EffectiveThreads = FMath::Min<int32>(FMath::DivideAndRoundUp(NumPrims, ParallelCommandListSet.MinDrawsPerCommandList), ParallelCommandListSet.Width);

		int32 Start = PassRange.GetLowerBoundValue();
		if (EffectiveThreads)
		{
			int32 NumPer = NumPrims / EffectiveThreads;
			int32 Extra = NumPrims - NumPer * EffectiveThreads;

			for (int32 ThreadIndex = 0; ThreadIndex < EffectiveThreads; ThreadIndex++)
			{
				int32 Last = Start + (NumPer - 1) + (ThreadIndex < Extra);
				check(Last >= Start);

				{
					FRHICommandList* CmdList = ParallelCommandListSet.NewParallelCommandList();

			FGraphEventRef AnyThreadCompletionEvent = TGraphTask<FDrawSortedTransAnyThreadTask>::CreateTask(ParallelCommandListSet.GetPrereqs(), ENamedThreads::GetRenderThread())
				.ConstructAndDispatchWhenReady(*this, *CmdList, View, ParallelCommandListSet.DrawRenderState, TranslucencyPass, Start, Last);

					ParallelCommandListSet.AddParallelCommandList(CmdList, AnyThreadCompletionEvent);
				}
				Start = Last + 1;
			}
		}
	}

	if (IsMainTranslucencyPass(TranslucencyPass))
	{
		View.SimpleElementCollector.DrawBatchedElements(RHICmdList, DrawRenderState, View, EBlendModeFilter::Translucent);

		// editor and debug rendering
		if (View.bHasTranslucentViewMeshElements)
		{
			// Draw the view's mesh elements with the translucent drawing policy.
			{
				QUICK_SCOPE_CYCLE_COUNTER(RenderTranslucencyParallel_SDPG_World);
				DrawViewElementsParallel<FTranslucencyDrawingPolicyFactory>(GParallelTranslucencyContext, SDPG_World, false, ParallelCommandListSet);
			}
			// Draw the view's mesh elements with the translucent drawing policy.
			{
				QUICK_SCOPE_CYCLE_COUNTER(RenderTranslucencyParallel_SDPG_Foreground);
				DrawViewElementsParallel<FTranslucencyDrawingPolicyFactory>(GParallelTranslucencyContext, SDPG_Foreground, false, ParallelCommandListSet);
			}
		}

		const FSceneViewState* ViewState = (const FSceneViewState*)View.State;
		if (ViewState && View.Family->EngineShowFlags.VisualizeLPV)
		{
			FLightPropagationVolume* LightPropagationVolume = ViewState->GetLightPropagationVolume(View.GetFeatureLevel());

			if (LightPropagationVolume)
		{
				LightPropagationVolume->Visualise(RHICmdList, View);
		}
		}
	}
}

void FDeferredShadingSceneRenderer::SetupDownsampledTranslucencyViewUniformBuffer(
	FRHICommandListImmediate& RHICmdList, 
	const FViewInfo& View,
	TUniformBufferRef<FViewUniformShaderParameters>& DownsampledTranslucencyViewUniformBuffer)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	FIntPoint ScaledSize;
	float DownsamplingScale = 1.f;
	SceneContext.GetSeparateTranslucencyDimensions(ScaledSize, DownsamplingScale);
	ensure(DownsamplingScale < 1.f);

	SceneContext.GetDownsampledTranslucencyDepth(RHICmdList, ScaledSize);
	DownsampleDepthSurface(RHICmdList, SceneContext.GetDownsampledTranslucencyDepthSurface(), View, DownsamplingScale, false);

	FViewUniformShaderParameters DownsampledTranslucencyParameters = *View.CachedViewUniformShaderParameters;

	// Update the parts of DownsampledTranslucencyParameters which are dependent on the buffer size and view rect
	View.SetupViewRectUniformBufferParameters(
		DownsampledTranslucencyParameters,
		ScaledSize,
		FIntRect(View.ViewRect.Min.X * DownsamplingScale, View.ViewRect.Min.Y * DownsamplingScale, View.ViewRect.Max.X * DownsamplingScale, View.ViewRect.Max.Y * DownsamplingScale),
		View.ViewMatrices,
		View.PrevViewInfo.ViewMatrices
	);

	DownsampledTranslucencyViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(DownsampledTranslucencyParameters, UniformBuffer_SingleFrame);
}

void FDeferredShadingSceneRenderer::ConditionalResolveSceneColorForTranslucentMaterials(FRHICommandListImmediate& RHICmdList, TRefCountPtr<IPooledRenderTarget>& SceneColorCopy)
{
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];

		bool bNeedsResolve = false;
		for (int32 TranslucencyPass = 0; TranslucencyPass < ETranslucencyPass::TPT_MAX && !bNeedsResolve; ++TranslucencyPass)
		{
			bNeedsResolve |= View.TranslucentPrimSet.SortedPrimsNum.UseSceneColorCopy((ETranslucencyPass::Type)TranslucencyPass);
		}

		if (bNeedsResolve)
		{
			FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

			SCOPED_DRAW_EVENTF(RHICmdList, EventCopy, TEXT("CopySceneColor from SceneColor for translucency"));

			RHICmdList.CopyToResolveTarget(SceneContext.GetSceneColorSurface(), SceneContext.GetSceneColorTexture(), FResolveRect(View.ViewRect.Min.X, View.ViewRect.Min.Y, View.ViewRect.Max.X, View.ViewRect.Max.Y));

			if (!SceneColorCopy)
			{
				FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(SceneContext.GetBufferSizeXY(), PF_B8G8R8A8, FClearValueBinding::White, TexCreate_None, TexCreate_RenderTargetable, false));
				GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SceneColorCopy, TEXT("SceneColorCopy"));
			}

			SetRenderTarget(RHICmdList, SceneColorCopy->GetRenderTargetItem().TargetableTexture, nullptr, ESimpleRenderTargetMode::EUninitializedColorAndDepth, FExclusiveDepthStencil::DepthNop_StencilNop, true);

			RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();


			TShaderMapRef<FScreenVS> ScreenVertexShader(View.ShaderMap);
			TShaderMapRef<FCopySceneColorPS> PixelShader(View.ShaderMap);

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*ScreenVertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			PixelShader->SetParameters(RHICmdList, View);

			DrawRectangle(
				RHICmdList,
				0, 0, 
				View.ViewRect.Width(), View.ViewRect.Height(),
				View.ViewRect.Min.X, View.ViewRect.Min.Y, 
				View.ViewRect.Width(), View.ViewRect.Height(),
				FIntPoint(View.ViewRect.Width(), View.ViewRect.Height()),
				SceneContext.GetBufferSizeXY(),
				*ScreenVertexShader,
				EDRF_UseTriangleOptimization);

			RHICmdList.CopyToResolveTarget(SceneColorCopy->GetRenderTargetItem().TargetableTexture, SceneColorCopy->GetRenderTargetItem().ShaderResourceTexture, FResolveParams(FResolveRect()));
		}
	}
}

void CreateTranslucentBasePassUniformBuffer(
	FRHICommandListImmediate& RHICmdList, 
	const FViewInfo& View, 
	IPooledRenderTarget* SceneColorCopy,
	ESceneTextureSetupMode SceneTextureSetupMode,
	TUniformBufferRef<FTranslucentBasePassUniformParameters>& BasePassUniformBuffer)
{
	FSceneRenderTargets& SceneRenderTargets = FSceneRenderTargets::Get(RHICmdList);

	FTranslucentBasePassUniformParameters BasePassParameters;
	SetupSharedBasePassParameters(RHICmdList, View, SceneRenderTargets, BasePassParameters.Shared);

	{
		SetupSceneTextureUniformParameters(SceneRenderTargets, View.FeatureLevel, SceneTextureSetupMode, BasePassParameters.SceneTextures);
		BasePassParameters.SceneTextures.EyeAdaptation = GetEyeAdaptation(View);
	}

	// Material SSR
	{
		float PrevSceneColorPreExposureInvValue = 1.0f / View.PreExposure;

		if (View.HZB)
		{
			BasePassParameters.HZBTexture = View.HZB->GetRenderTargetItem().ShaderResourceTexture;
			BasePassParameters.HZBSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

			const TRefCountPtr<IPooledRenderTarget>* PrevSceneColorRT = &GSystemTextures.BlackDummy;

			if (View.PrevViewInfo.CustomSSRInput.IsValid())
			{
				PrevSceneColorRT = &View.PrevViewInfo.CustomSSRInput;
				PrevSceneColorPreExposureInvValue = 1.0f / View.PrevViewInfo.TemporalAAHistory.SceneColorPreExposure;
			}
			else if (View.PrevViewInfo.TemporalAAHistory.IsValid())
			{
				PrevSceneColorRT = &View.PrevViewInfo.TemporalAAHistory.RT[0];
				PrevSceneColorPreExposureInvValue = 1.0f / View.PrevViewInfo.TemporalAAHistory.SceneColorPreExposure;
			}

			BasePassParameters.PrevSceneColor = (*PrevSceneColorRT)->GetRenderTargetItem().ShaderResourceTexture;
			BasePassParameters.PrevSceneColorSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

			const FVector2D HZBUvFactor(
				float(View.ViewRect.Width()) / float(2 * View.HZBMipmap0Size.X),
				float(View.ViewRect.Height()) / float(2 * View.HZBMipmap0Size.Y)
				);
			const FVector4 HZBUvFactorAndInvFactorValue(
				HZBUvFactor.X,
				HZBUvFactor.Y,
				1.0f / HZBUvFactor.X,
				1.0f / HZBUvFactor.Y
				);
			
			BasePassParameters.HZBUvFactorAndInvFactor = HZBUvFactorAndInvFactorValue;
		}
		else
		{
			BasePassParameters.HZBTexture = GBlackTexture->TextureRHI;
			BasePassParameters.HZBSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			BasePassParameters.PrevSceneColor = GBlackTexture->TextureRHI;
			BasePassParameters.PrevSceneColorSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		}

		FIntPoint ViewportOffset = View.ViewRect.Min;
		FIntPoint ViewportExtent = View.ViewRect.Size();
		FIntPoint BufferSize = SceneRenderTargets.GetBufferSizeXY();

		if (View.PrevViewInfo.TemporalAAHistory.IsValid())
		{
			ViewportOffset = View.PrevViewInfo.TemporalAAHistory.ViewportRect.Min;
			ViewportExtent = View.PrevViewInfo.TemporalAAHistory.ViewportRect.Size();
			BufferSize = View.PrevViewInfo.TemporalAAHistory.RT[0]->GetDesc().Extent;
		}
		
		FVector2D InvBufferSize(1.0f / float(BufferSize.X), 1.0f / float(BufferSize.Y));

		FVector4 ScreenPosToPixelValue(
			ViewportExtent.X * 0.5f * InvBufferSize.X,
			-ViewportExtent.Y * 0.5f * InvBufferSize.Y,
			(ViewportExtent.X * 0.5f + ViewportOffset.X) * InvBufferSize.X,
			(ViewportExtent.Y * 0.5f + ViewportOffset.Y) * InvBufferSize.Y);

		BasePassParameters.PrevScreenPositionScaleBias = ScreenPosToPixelValue;
		BasePassParameters.PrevSceneColorPreExposureInv = PrevSceneColorPreExposureInvValue;
	}

	// Translucency Lighting Volume
	{
		if (SceneRenderTargets.GetTranslucencyVolumeAmbient(TVC_Inner) != nullptr)
		{
			BasePassParameters.TranslucencyLightingVolumeAmbientInner = SceneRenderTargets.GetTranslucencyVolumeAmbient(TVC_Inner)->GetRenderTargetItem().ShaderResourceTexture;
			BasePassParameters.TranslucencyLightingVolumeAmbientOuter = SceneRenderTargets.GetTranslucencyVolumeAmbient(TVC_Outer)->GetRenderTargetItem().ShaderResourceTexture;
			BasePassParameters.TranslucencyLightingVolumeDirectionalInner = SceneRenderTargets.GetTranslucencyVolumeDirectional(TVC_Inner)->GetRenderTargetItem().ShaderResourceTexture;
			BasePassParameters.TranslucencyLightingVolumeDirectionalOuter = SceneRenderTargets.GetTranslucencyVolumeDirectional(TVC_Outer)->GetRenderTargetItem().ShaderResourceTexture;
		}
		else
		{
			const FTextureRHIRef DummyTLV = GSystemTextures.BlackDummy->GetRenderTargetItem().ShaderResourceTexture;
			BasePassParameters.TranslucencyLightingVolumeAmbientInner = DummyTLV;
			BasePassParameters.TranslucencyLightingVolumeAmbientOuter = DummyTLV;
			BasePassParameters.TranslucencyLightingVolumeDirectionalInner = DummyTLV;
			BasePassParameters.TranslucencyLightingVolumeDirectionalOuter = DummyTLV;
		}

		BasePassParameters.TranslucencyLightingVolumeAmbientInnerSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		BasePassParameters.TranslucencyLightingVolumeAmbientOuterSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		BasePassParameters.TranslucencyLightingVolumeDirectionalInnerSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		BasePassParameters.TranslucencyLightingVolumeDirectionalOuterSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	}

	BasePassParameters.SceneTextures.SceneColorCopyTexture = SceneColorCopy ? SceneColorCopy->GetRenderTargetItem().ShaderResourceTexture : GBlackTexture->TextureRHI;

	BasePassUniformBuffer = TUniformBufferRef<FTranslucentBasePassUniformParameters>::CreateUniformBufferImmediate(BasePassParameters, UniformBuffer_SingleFrame);
}

void FDeferredShadingSceneRenderer::RenderTranslucency(FRHICommandListImmediate& RHICmdList, ETranslucencyPass::Type TranslucencyPass, IPooledRenderTarget* SceneColorCopy)
{
	if (!ShouldRenderTranslucency(TranslucencyPass))
	{
		return; // Early exit if nothing needs to be done.
	}

	SCOPED_DRAW_EVENT(RHICmdList, Translucency);
	SCOPED_GPU_STAT(RHICmdList, Translucency);

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	// Support for parallel rendering.
	const bool bUseParallel = GRHICommandList.UseParallelAlgorithms() && CVarParallelTranslucency.GetValueOnRenderThread();
	if (bUseParallel)
	{
		SceneContext.AllocLightAttenuation(RHICmdList); // materials will attempt to get this texture before the deferred command to set it up executes
	}
	FScopedCommandListWaitForTasks Flusher(bUseParallel && (CVarRHICmdFlushRenderThreadTasksTranslucentPass.GetValueOnRenderThread() > 0 || CVarRHICmdFlushRenderThreadTasks.GetValueOnRenderThread() > 0), RHICmdList);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, EventView, Views.Num() > 1, TEXT("View%d"), ViewIndex);
		FViewInfo& View = Views[ViewIndex];
		if (!View.ShouldRenderView())
		{
			continue;
		}

#if STATS
		if (View.ViewState && IsMainTranslucencyPass(TranslucencyPass))
		{
			View.ViewState->TranslucencyTimer.Begin(RHICmdList);
		}
#endif

		TUniformBufferRef<FTranslucentBasePassUniformParameters> BasePassUniformBuffer;
		CreateTranslucentBasePassUniformBuffer(RHICmdList, View, SceneColorCopy, ESceneTextureSetupMode::All, BasePassUniformBuffer);
		FDrawingPolicyRenderState DrawRenderState(View, BasePassUniformBuffer);

		// If downsampling we need to render in the separate buffer. Otherwise we also need to render offscreen to apply TPT_TranslucencyAfterDOF
		if (RenderInSeparateTranslucency(SceneContext, TranslucencyPass, View.TranslucentPrimSet.SortedPrimsNum.DisableOffscreenRendering(TranslucencyPass)))
		{
			FIntPoint ScaledSize;
			float DownsamplingScale = 1.f;
			SceneContext.GetSeparateTranslucencyDimensions(ScaledSize, DownsamplingScale);

			if (DownsamplingScale < 1.f)
			{
				TUniformBufferRef<FViewUniformShaderParameters> DownsampledTranslucencyViewUniformBuffer;
				SetupDownsampledTranslucencyViewUniformBuffer(RHICmdList, View, DownsampledTranslucencyViewUniformBuffer);
				DrawRenderState.SetViewUniformBuffer(DownsampledTranslucencyViewUniformBuffer);
			}
			if (TranslucencyPass == ETranslucencyPass::TPT_TranslucencyAfterDOF)
			{
				BeginTimingSeparateTranslucencyPass(RHICmdList, View);
			}
			SceneContext.BeginRenderingSeparateTranslucency(RHICmdList, View, *this, ViewIndex == 0);

			// Draw only translucent prims that are in the SeparateTranslucency pass
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());

			if (bUseParallel)
			{
				RenderViewTranslucencyParallel(RHICmdList, View, DrawRenderState, TranslucencyPass);
			}
			else
			{
				RenderViewTranslucency(RHICmdList, View, DrawRenderState, TranslucencyPass);
			}

			SceneContext.FinishRenderingSeparateTranslucency(RHICmdList, View);
			if (TranslucencyPass == ETranslucencyPass::TPT_TranslucencyAfterDOF)
			{
				EndTimingSeparateTranslucencyPass(RHICmdList, View);
			}
			if (TranslucencyPass != ETranslucencyPass::TPT_TranslucencyAfterDOF)
			{
				FTranslucencyDrawingPolicyFactory::UpsampleTranslucency(RHICmdList, View, false);
			}
		}
		else
		{
			SceneContext.BeginRenderingTranslucency(RHICmdList, View, *this, ViewIndex == 0);
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());
	
			if (bUseParallel)
			{
				RenderViewTranslucencyParallel(RHICmdList, View, DrawRenderState, TranslucencyPass);
			}
			else
			{
				RenderViewTranslucency(RHICmdList, View, DrawRenderState, TranslucencyPass);
			}
		}

#if STATS
		if (View.ViewState && IsMainTranslucencyPass(TranslucencyPass))
		{
			STAT(View.ViewState->TranslucencyTimer.End(RHICmdList));
		}
#endif
	}
}

class FTranslucencyUpsamplingPS : public FGlobalShader
{
protected:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4);
	}

	/** Default constructor. */
	FTranslucencyUpsamplingPS(bool InbUseNearestDepthNeighborUpsample)
		: bUseNearestDepthNeighborUpsample(InbUseNearestDepthNeighborUpsample)
	{
	}

	FSceneTextureShaderParameters SceneTextureParameters;
	FShaderParameter LowResColorTexelSize;
	FShaderResourceParameter LowResDepthTexture;
	FShaderResourceParameter LowResColorTexture;
	FShaderResourceParameter BilinearClampedSampler;
	FShaderResourceParameter PointClampedSampler;

public:

	/** Initialization constructor. */
	FTranslucencyUpsamplingPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer, bool InbUseNearestDepthNeighborUpsample)
		: FGlobalShader(Initializer)
		, bUseNearestDepthNeighborUpsample(InbUseNearestDepthNeighborUpsample)
	{
		SceneTextureParameters.Bind(Initializer);
		LowResColorTexelSize.Bind(Initializer.ParameterMap, TEXT("LowResColorTexelSize"));
		LowResDepthTexture.Bind(Initializer.ParameterMap, TEXT("LowResDepthTexture"));
		LowResColorTexture.Bind(Initializer.ParameterMap, TEXT("LowResColorTexture"));
		BilinearClampedSampler.Bind(Initializer.ParameterMap, TEXT("BilinearClampedSampler"));
		PointClampedSampler.Bind(Initializer.ParameterMap, TEXT("PointClampedSampler"));
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << SceneTextureParameters << LowResColorTexelSize << LowResDepthTexture << LowResColorTexture << BilinearClampedSampler << PointClampedSampler;
		return bShaderHasOutdatedParameters;
	}
			
	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View)
	{
		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		TRefCountPtr<IPooledRenderTarget>& DownsampledTranslucency = SceneContext.SeparateTranslucencyRT;

		float Width = DownsampledTranslucency->GetDesc().Extent.X;
		float Height = DownsampledTranslucency->GetDesc().Extent.Y;
		SetShaderValue(RHICmdList, ShaderRHI, LowResColorTexelSize, FVector4(Width, Height, 1.0f / Width, 1.0f / Height));

		SetTextureParameter(RHICmdList, ShaderRHI, LowResColorTexture, DownsampledTranslucency->GetRenderTargetItem().ShaderResourceTexture);
		SetTextureParameter(RHICmdList, ShaderRHI, LowResDepthTexture, SceneContext.GetDownsampledTranslucencyDepthSurface());

		SetSamplerParameter(RHICmdList, ShaderRHI, BilinearClampedSampler, TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());
		SetSamplerParameter(RHICmdList, ShaderRHI, PointClampedSampler, TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());

		SceneTextureParameters.Set(RHICmdList, GetPixelShader(), View.FeatureLevel, ESceneTextureSetupMode::All);
	}

	const bool bUseNearestDepthNeighborUpsample;
};

class FTranslucencySimpleUpsamplingPS : public FTranslucencyUpsamplingPS
{
protected:
	DECLARE_SHADER_TYPE(FTranslucencySimpleUpsamplingPS, Global);
	FTranslucencySimpleUpsamplingPS() : FTranslucencyUpsamplingPS(false) {}
public:
	FTranslucencySimpleUpsamplingPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FTranslucencyUpsamplingPS(Initializer, false) {}
};
					
IMPLEMENT_SHADER_TYPE(,FTranslucencySimpleUpsamplingPS,TEXT("/Engine/Private/TranslucencyUpsampling.usf"),TEXT("SimpleUpsamplingPS"),SF_Pixel);

class FTranslucencyNearestDepthNeighborUpsamplingPS : public FTranslucencyUpsamplingPS
{
protected:
	DECLARE_SHADER_TYPE(FTranslucencyNearestDepthNeighborUpsamplingPS, Global);
	FTranslucencyNearestDepthNeighborUpsamplingPS() : FTranslucencyUpsamplingPS(true) {}
public:
	FTranslucencyNearestDepthNeighborUpsamplingPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FTranslucencyUpsamplingPS(Initializer, true) {}
};

IMPLEMENT_SHADER_TYPE(,FTranslucencyNearestDepthNeighborUpsamplingPS,TEXT("/Engine/Private/TranslucencyUpsampling.usf"),TEXT("NearestDepthNeighborUpsamplingPS"),SF_Pixel);

bool UseNearestDepthNeighborUpsampleForSeparateTranslucency(const FSceneRenderTargets& SceneContext)
{
	FIntPoint OutScaledSize;
	float OutScale;
	SceneContext.GetSeparateTranslucencyDimensions(OutScaledSize, OutScale);

	return CVarSeparateTranslucencyUpsampleMode.GetValueOnRenderThread() != 0 && FMath::Abs(OutScale - .5f) < .001f;
}

void FTranslucencyDrawingPolicyFactory::UpsampleTranslucency(FRHICommandList& RHICmdList, const FViewInfo& View, bool bOverwrite)
{
	SCOPED_DRAW_EVENTF(RHICmdList, EventUpsampleCopy, TEXT("Upsample translucency"));

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	SceneContext.BeginRenderingSceneColor(RHICmdList, ESimpleRenderTargetMode::EExistingColorAndDepth, FExclusiveDepthStencil::DepthRead_StencilWrite);
	RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	if (bOverwrite) // When overwriting, we also need to set the alpha as other translucent primitive could accumulate into the buffer.
	{
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	}
	else
	{
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha>::GetRHI();
	}

	TShaderMapRef<FScreenVS> ScreenVertexShader(View.ShaderMap);
	FTranslucencyUpsamplingPS* UpsamplingPixelShader = nullptr;
	if (UseNearestDepthNeighborUpsampleForSeparateTranslucency(SceneContext))
	{
		TShaderMapRef<FTranslucencyNearestDepthNeighborUpsamplingPS> PixelShader(View.ShaderMap);
		UpsamplingPixelShader = *PixelShader;
	}
	else
	{
		TShaderMapRef<FTranslucencySimpleUpsamplingPS> PixelShader(View.ShaderMap);
		UpsamplingPixelShader = *PixelShader;
	}

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*ScreenVertexShader);
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(UpsamplingPixelShader);
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
	UpsamplingPixelShader->SetParameters(RHICmdList, View);

	FIntPoint OutScaledSize;
	float OutScale;
	SceneContext.GetSeparateTranslucencyDimensions(OutScaledSize, OutScale);

	TRefCountPtr<IPooledRenderTarget>& DownsampledTranslucency = SceneContext.SeparateTranslucencyRT;
	int32 TextureWidth = DownsampledTranslucency->GetDesc().Extent.X;
	int32 TextureHeight = DownsampledTranslucency->GetDesc().Extent.Y;

	DrawRectangle(
		RHICmdList,
		View.ViewRect.Min.X, View.ViewRect.Min.Y, 
		View.ViewRect.Width(), View.ViewRect.Height(),
		View.ViewRect.Min.X * OutScale, View.ViewRect.Min.Y * OutScale, 
		View.ViewRect.Width() * OutScale, View.ViewRect.Height() * OutScale,
		View.ViewRect.Size(),
		FIntPoint(TextureWidth, TextureHeight),
		*ScreenVertexShader,
		EDRF_UseTriangleOptimization);
}
