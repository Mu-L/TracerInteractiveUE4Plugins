// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessDeferredDecals.cpp: Deferred Decals implementation.
=============================================================================*/

#include "CompositionLighting/PostProcessDeferredDecals.h"
#include "SceneUtils.h"
#include "ScenePrivate.h"
#include "DecalRenderingShared.h"
#include "ClearQuad.h"
#include "PipelineStateCache.h"
#include "VisualizeTexture.h"
#include "RendererUtils.h"


static TAutoConsoleVariable<float> CVarStencilSizeThreshold(
	TEXT("r.Decal.StencilSizeThreshold"),
	0.1f,
	TEXT("Control a per decal stencil pass that allows to large (screen space) decals faster. It adds more overhead per decals so this\n")
	TEXT("  <0: optimization is disabled\n")
	TEXT("   0: optimization is enabled no matter how small (screen space) the decal is\n")
	TEXT("0..1: optimization is enabled, value defines the minimum size (screen space) to trigger the optimization (default 0.1)")
);

enum EDecalDepthInputState
{
	DDS_Undefined,
	DDS_Always,
	DDS_DepthTest,
	DDS_DepthAlways_StencilEqual1,
	DDS_DepthAlways_StencilEqual1_IgnoreMask,
	DDS_DepthAlways_StencilEqual0,
	DDS_DepthTest_StencilEqual1,
	DDS_DepthTest_StencilEqual1_IgnoreMask,
	DDS_DepthTest_StencilEqual0,
};

struct FDecalDepthState
{
	EDecalDepthInputState DepthTest;
	bool bDepthOutput;

	FDecalDepthState()
		: DepthTest(DDS_Undefined)
		, bDepthOutput(false)
	{
	}

	bool operator !=(const FDecalDepthState &rhs) const
	{
		return DepthTest != rhs.DepthTest || bDepthOutput != rhs.bDepthOutput;
	}
};

// @param RenderState 0:before BasePass, 1:before lighting, (later we could add "after lighting" and multiply)
FRHIBlendState* GetDecalBlendState(const ERHIFeatureLevel::Type SMFeatureLevel, EDecalRenderStage InDecalRenderStage, EDecalBlendMode DecalBlendMode, bool bHasNormal)
{
	if (InDecalRenderStage == DRS_BeforeBasePass)
	{
		// before base pass (for DBuffer decals)
		{
			// As we set the opacity in the shader we don't need to set different frame buffer blend modes but we like to hint to the driver that we
			// don't need to output there. We also could replace this with many SetRenderTarget calls but it might be slower (needs to be tested).

			switch (DecalBlendMode)
			{
			case DBM_DBuffer_AlphaComposite:
				return TStaticBlendState<
					CW_RGBA, BO_Add, BF_One,  BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha,
					CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,
					CW_RGBA, BO_Add, BF_One,  BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha,
					CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One // DBuffer mask
				>::GetRHI();

			case DBM_DBuffer_ColorNormalRoughness:
				return TStaticBlendState<
					CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha,
					CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha,
					CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha,
					CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One // DBuffer mask
				>::GetRHI();

			case DBM_DBuffer_Color:
				// we can optimize using less MRT later
				return TStaticBlendState<
					CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha,
					CW_RGBA, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,
					CW_RGBA, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,
					CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One // DBuffer mask
				>::GetRHI();

			case DBM_DBuffer_ColorNormal:
				// we can optimize using less MRT later
				return TStaticBlendState<
					CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha,
					CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha,
					CW_RGBA, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,
					CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One // DBuffer mask
				>::GetRHI();

			case DBM_DBuffer_ColorRoughness:
				// we can optimize using less MRT later
				return TStaticBlendState<
					CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha,
					CW_RGBA, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,
					CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha,
					CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One // DBuffer mask
				>::GetRHI();

			case DBM_DBuffer_Normal:
				// we can optimize using less MRT later
				return TStaticBlendState<
					CW_RGBA, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,
					CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha,
					CW_RGBA, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,
					CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One // DBuffer mask
				>::GetRHI();

			case DBM_DBuffer_NormalRoughness:
				// we can optimize using less MRT later
				return TStaticBlendState<
					CW_RGBA, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,
					CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha,
					CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha,
					CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One // DBuffer mask
				>::GetRHI();

			case DBM_DBuffer_Roughness:
				// we can optimize using less MRT later
				return TStaticBlendState<
					CW_RGBA, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,
					CW_RGBA, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,
					CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha,
					CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One // DBuffer mask
				>::GetRHI();

			default:
				// the decal type should not be rendered in this pass - internal error
				check(0);
				return nullptr;
			}
		}
	}
	else if (InDecalRenderStage == DRS_AfterBasePass)
	{
		ensure(DecalBlendMode == DBM_Volumetric_DistanceFunction);

		return TStaticBlendState<>::GetRHI();
	}
	else if (InDecalRenderStage == DRS_AmbientOcclusion)
	{
		ensure(DecalBlendMode == DBM_AmbientOcclusion);

		return TStaticBlendState<CW_RED, BO_Add, BF_DestColor, BF_Zero>::GetRHI();
	}
	else
	{
		// before lighting (for non DBuffer decals)

		switch (DecalBlendMode)
		{
		case DBM_Translucent:
			// @todo: Feature Level 10 does not support separate blends modes for each render target. This could result in the
			// translucent and stain blend modes looking incorrect when running in this mode.
			if (GSupportsSeparateRenderTargetBlendState)
			{
				if (bHasNormal)
				{
					return TStaticBlendState<
						CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,	// Emissive
						CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Normal
						CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
						CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One		// BaseColor
					>::GetRHI();
				}
				else
				{
					return TStaticBlendState<
						CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,	// Emissive
						CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
						CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One		// BaseColor
					>::GetRHI();
				}
			}

		case DBM_Stain:
			if (GSupportsSeparateRenderTargetBlendState)
			{
				if (bHasNormal)
				{
					return TStaticBlendState<
						CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,	// Emissive
						CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Normal
						CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
						CW_RGB, BO_Add, BF_DestColor, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One		// BaseColor
					>::GetRHI();
				}
				else
				{
					return TStaticBlendState<
						CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,	// Emissive
						CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
						CW_RGB, BO_Add, BF_DestColor, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One		// BaseColor
					>::GetRHI();
				}
			}

		case DBM_Normal:
			return TStaticBlendState< CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha >::GetRHI();


		case DBM_Emissive:
		case DBM_DBuffer_Emissive:
			return TStaticBlendState< CW_RGB, BO_Add, BF_SourceAlpha, BF_One >::GetRHI();

		case DBM_DBuffer_EmissiveAlphaComposite:
			return TStaticBlendState< CW_RGB, BO_Add, BF_One, BF_One >::GetRHI();

		case DBM_AlphaComposite:
			if (GSupportsSeparateRenderTargetBlendState)
			{
				return TStaticBlendState<
					CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Emissive
					CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,				// Normal
					CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
					CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
				>::GetRHI();
			}
		default:
			// the decal type should not be rendered in this pass - internal error
			check(0);
			return nullptr;
		}
	}
}

bool RenderPreStencil(FRenderingCompositePassContext& Context, const FMatrix& ComponentToWorldMatrix, const FMatrix& FrustumComponentToClip)
{
	const FViewInfo& View = Context.View;

	float Distance = (View.ViewMatrices.GetViewOrigin() - ComponentToWorldMatrix.GetOrigin()).Size();
	float Radius = ComponentToWorldMatrix.GetMaximumAxisScale();

	// if not inside
	if (Distance > Radius)
	{
		float EstimatedDecalSize = Radius / Distance;

		float StencilSizeThreshold = CVarStencilSizeThreshold.GetValueOnRenderThread();

		// Check if it's large enough on screen
		if (EstimatedDecalSize < StencilSizeThreshold)
		{
			return false;
		}
	}

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	// Set states, the state cache helps us avoiding redundant sets
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();

	// all the same to have DX10 working
	GraphicsPSOInit.BlendState = TStaticBlendState<
		CW_NONE, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Emissive
		CW_NONE, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Normal
		CW_NONE, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
		CW_NONE, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One		// BaseColor
	>::GetRHI();

	// Carmack's reverse the sandbox stencil bit on the bounds
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
		false, CF_LessEqual,
		true, CF_Always, SO_Keep, SO_Keep, SO_Invert,
		true, CF_Always, SO_Keep, SO_Keep, SO_Invert,
		STENCIL_SANDBOX_MASK, STENCIL_SANDBOX_MASK
	>::GetRHI();

	FDecalRendering::SetVertexShaderOnly(Context.RHICmdList, GraphicsPSOInit, View, FrustumComponentToClip);
	Context.RHICmdList.SetStencilRef(0);

	// Set stream source after updating cached strides
	Context.RHICmdList.SetStreamSource(0, GetUnitCubeVertexBuffer(), 0);

	// Render decal mask
	Context.RHICmdList.DrawIndexedPrimitive(GetUnitCubeIndexBuffer(), 0, 0, 8, 0, UE_ARRAY_COUNT(GCubeIndices) / 3, 1);

	return true;
}

static EDecalRasterizerState ComputeDecalRasterizerState(bool bInsideDecal, bool bIsInverted, const FViewInfo& View)
{
	bool bClockwise = bInsideDecal;

	if (View.bReverseCulling)
	{
		bClockwise = !bClockwise;
	}

	if (bIsInverted)
	{
		bClockwise = !bClockwise;
	}
	return bClockwise ? DRS_CW : DRS_CCW;
}

FRCPassPostProcessDeferredDecals::FRCPassPostProcessDeferredDecals(EDecalRenderStage InDecalRenderStage)
	: CurrentStage(InDecalRenderStage)
{
}

static FDecalDepthState ComputeDecalDepthState(EDecalRenderStage LocalDecalStage, bool bInsideDecal, bool bThisDecalUsesStencil)
{
	FDecalDepthState Ret;

	Ret.bDepthOutput = (LocalDecalStage == DRS_AfterBasePass);

	if (Ret.bDepthOutput)
	{
		// can be made one enum
		Ret.DepthTest = DDS_DepthTest;
		return Ret;
	}

	const bool bUseDecalMask = 
		LocalDecalStage == DRS_BeforeLighting || 
		LocalDecalStage == DRS_Emissive || 
		LocalDecalStage == DRS_AmbientOcclusion;

	if (bInsideDecal)
	{
		if (bThisDecalUsesStencil)
		{
			Ret.DepthTest = bUseDecalMask ? DDS_DepthAlways_StencilEqual1 : DDS_DepthAlways_StencilEqual1_IgnoreMask;
		}
		else
		{
			Ret.DepthTest = bUseDecalMask ? DDS_DepthAlways_StencilEqual0 : DDS_Always;
		}
	}
	else
	{
		if (bThisDecalUsesStencil)
		{
			Ret.DepthTest = bUseDecalMask ? DDS_DepthTest_StencilEqual1 : DDS_DepthTest_StencilEqual1_IgnoreMask;
		}
		else
		{
			Ret.DepthTest = bUseDecalMask ? DDS_DepthTest_StencilEqual0 : DDS_DepthTest;
		}
	}

	return Ret;
}

static FRHIDepthStencilState* GetDecalDepthState(uint32& StencilRef, FDecalDepthState DecalDepthState)
{
	switch (DecalDepthState.DepthTest)
	{
	case DDS_DepthAlways_StencilEqual1:
		check(!DecalDepthState.bDepthOutput);			// todo
		StencilRef = STENCIL_SANDBOX_MASK | GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1);
		return TStaticDepthStencilState<
			false, CF_Always,
			true, CF_Equal, SO_Zero, SO_Zero, SO_Zero,
			true, CF_Equal, SO_Zero, SO_Zero, SO_Zero,
			STENCIL_SANDBOX_MASK | GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1), STENCIL_SANDBOX_MASK>::GetRHI();

	case DDS_DepthAlways_StencilEqual1_IgnoreMask:
		check(!DecalDepthState.bDepthOutput);			// todo
		StencilRef = STENCIL_SANDBOX_MASK;
		return TStaticDepthStencilState<
			false, CF_Always,
			true, CF_Equal, SO_Zero, SO_Zero, SO_Zero,
			true, CF_Equal, SO_Zero, SO_Zero, SO_Zero,
			STENCIL_SANDBOX_MASK, STENCIL_SANDBOX_MASK>::GetRHI();

	case DDS_DepthAlways_StencilEqual0:
		check(!DecalDepthState.bDepthOutput);			// todo
		StencilRef = GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1);
		return TStaticDepthStencilState<
			false, CF_Always,
			true, CF_Equal, SO_Keep, SO_Keep, SO_Keep,
			false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
			STENCIL_SANDBOX_MASK | GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1), 0x00>::GetRHI();

	case DDS_Always:
		check(!DecalDepthState.bDepthOutput);			// todo 
		StencilRef = 0;
		return TStaticDepthStencilState<false, CF_Always>::GetRHI();

	case DDS_DepthTest_StencilEqual1:
		check(!DecalDepthState.bDepthOutput);			// todo
		StencilRef = STENCIL_SANDBOX_MASK | GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1);
		return TStaticDepthStencilState<
			false, CF_DepthNearOrEqual,
			true, CF_Equal, SO_Zero, SO_Zero, SO_Zero,
			true, CF_Equal, SO_Zero, SO_Zero, SO_Zero,
			STENCIL_SANDBOX_MASK | GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1), STENCIL_SANDBOX_MASK>::GetRHI();

	case DDS_DepthTest_StencilEqual1_IgnoreMask:
		check(!DecalDepthState.bDepthOutput);			// todo
		StencilRef = STENCIL_SANDBOX_MASK;
		return TStaticDepthStencilState<
			false, CF_DepthNearOrEqual,
			true, CF_Equal, SO_Zero, SO_Zero, SO_Zero,
			true, CF_Equal, SO_Zero, SO_Zero, SO_Zero,
			STENCIL_SANDBOX_MASK, STENCIL_SANDBOX_MASK>::GetRHI();

	case DDS_DepthTest_StencilEqual0:
		check(!DecalDepthState.bDepthOutput);			// todo
		StencilRef = GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1);
		return TStaticDepthStencilState<
			false, CF_DepthNearOrEqual,
			true, CF_Equal, SO_Keep, SO_Keep, SO_Keep,
			false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
			STENCIL_SANDBOX_MASK | GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1), 0x00>::GetRHI();

	case DDS_DepthTest:
		if (DecalDepthState.bDepthOutput)
		{
			StencilRef = 0;
			return TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI();
		}
		else
		{
			StencilRef = 0;
			return TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI();
		}

	default:
		check(0);
		return nullptr;
	}
}

FRHIRasterizerState* GetDecalRasterizerState(EDecalRasterizerState DecalRasterizerState)
{
	switch (DecalRasterizerState)
	{
	case DRS_CW: return TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI();
	case DRS_CCW: return TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI();
	default: check(0); return nullptr;
	}
}

static inline bool IsStencilOptimizationAvailable(EDecalRenderStage RenderStage)
{
	return RenderStage == DRS_BeforeLighting || RenderStage == DRS_BeforeBasePass || RenderStage == DRS_Emissive;
}

const TCHAR* GetStageName(EDecalRenderStage Stage)
{
	// could be implemented with enum reflections as well

	switch (Stage)
	{
	case DRS_BeforeBasePass: return TEXT("DRS_BeforeBasePass");
	case DRS_AfterBasePass: return TEXT("DRS_AfterBasePass");
	case DRS_BeforeLighting: return TEXT("DRS_BeforeLighting");
	case DRS_Mobile: return TEXT("DRS_Mobile");
	case DRS_AmbientOcclusion: return TEXT("DRS_AmbientOcclusion");
	case DRS_Emissive: return TEXT("DRS_Emissive");
	}
	return TEXT("<UNKNOWN>");
}


void FRCPassPostProcessDeferredDecals::Process(FRenderingCompositePassContext& Context)
{
	FRHICommandListImmediate& RHICmdList = Context.RHICmdList;
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	const bool bShaderComplexity = Context.View.Family->EngineShowFlags.ShaderComplexity;
	const bool bDBuffer = IsUsingDBuffers(Context.View.GetShaderPlatform());
	const bool bPerPixelDBufferMask = IsUsingPerPixelDBufferMask(Context.View.GetShaderPlatform());
	const bool bStencilSizeThreshold = CVarStencilSizeThreshold.GetValueOnRenderThread() >= 0;

	SCOPED_DRAW_EVENTF(RHICmdList, DeferredDecals, TEXT("DeferredDecals %s"), GetStageName(CurrentStage));

	RHICmdList.TransitionResource(FExclusiveDepthStencil::DepthNop_StencilWrite, SceneContext.GetSceneDepthSurface());

	// this cast is safe as only the dedicated server implements this differently and this pass should not be executed on the dedicated server
	const FViewInfo& View = Context.View;
	const FSceneViewFamily& ViewFamily = *(View.Family);
	bool bNeedsDBufferTargets = false;
	bool bDidClearDBuffer = false;
	// Debug view framework does not yet support decals.
	bool bRenderDecals = ViewFamily.EngineShowFlags.Decals && !ViewFamily.UseDebugViewPS();

	if (CurrentStage == DRS_BeforeBasePass)
	{
		// before BasePass, only if DBuffer is enabled
		check(bDBuffer);

		// If we're rendering dbuffer decals but there are no decals in the scene, we avoid the 
		// clears/decompresses and set the targets to NULL		
		// The DBufferA-C will be replaced with dummy textures in FSceneTextureShaderParameters
		if (bRenderDecals)
		{
			FScene& Scene = *(FScene*)ViewFamily.Scene;
			if (Scene.Decals.Num() > 0 || Context.View.MeshDecalBatches.Num() > 0)
			{
				bNeedsDBufferTargets = true;
			}
		}

		// If we need dbuffer targets, initialize them
		if (bNeedsDBufferTargets)
		{
			uint32 BaseFlags = RHISupportsRenderTargetWriteMask(GMaxRHIShaderPlatform) ? TexCreate_NoFastClearFinalize | TexCreate_DisableDCC : TexCreate_None;

			// DBuffer: Decal buffer
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(
				SceneContext.GetBufferSizeXY(),
				PF_B8G8R8A8,
				FClearValueBinding::None,
				BaseFlags | GFastVRamConfig.DBufferA,
				TexCreate_ShaderResource | TexCreate_RenderTargetable,
				false,
				1,
				true,
				true));
			
			if (!SceneContext.DBufferA)
			{
				Desc.ClearValue = FClearValueBinding::Black;
				GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SceneContext.DBufferA, TEXT("DBufferA"));
			}

			if (!SceneContext.DBufferB)
			{
				Desc.Flags = BaseFlags | GFastVRamConfig.DBufferB;
				Desc.ClearValue = FClearValueBinding(FLinearColor(128.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f, 1));
				GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SceneContext.DBufferB, TEXT("DBufferB"));
			}

			if (!SceneContext.DBufferC)
			{
				Desc.Flags = BaseFlags | GFastVRamConfig.DBufferC;
				Desc.ClearValue = FClearValueBinding(FLinearColor(0, 0, 0, 1));
				GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SceneContext.DBufferC, TEXT("DBufferC"));
			}

			if (bPerPixelDBufferMask)
			{
				// Note: 32bpp format is used here to utilize color compression hardware (same as other DBuffer targets).
				// This significantly reduces bandwidth for clearing, writing and reading on some GPUs.
				// While a smaller format, such as R8_UINT, will use less video memory, it will result in slower clears and higher bandwidth requirements.
				check(Desc.Format == PF_B8G8R8A8);
				Desc.Flags = TexCreate_None;
#if SUPPORTS_VISUALIZE_TEXTURE
				Desc.TargetableFlags |= TexCreate_ShaderResource;
#endif
				Desc.ClearValue = FClearValueBinding::Transparent;
				GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SceneContext.DBufferMask, TEXT("DBufferMask"));
			}

			// we assume views are non overlapping, then we need to clear only once in the beginning, otherwise we would need to set scissor rects
			// and don't get FastClear any more.
			bool bFirstView = Context.View.Family->Views[0] == &Context.View || ViewFamily.bMultiGPUForkAndJoin;

			if (bFirstView)
			{
				SCOPED_DRAW_EVENT(RHICmdList, DBufferClear);

				FRHITexture* RenderTargets[4];

				RenderTargets[0] = SceneContext.DBufferA->GetRenderTargetItem().TargetableTexture;
				RenderTargets[1] = SceneContext.DBufferB->GetRenderTargetItem().TargetableTexture;
				RenderTargets[2] = SceneContext.DBufferC->GetRenderTargetItem().TargetableTexture;
				int32 RTCount = 3;

				if (bPerPixelDBufferMask)
				{
					RenderTargets[3] = SceneContext.DBufferMask->GetRenderTargetItem().TargetableTexture;
					RTCount = 4;
				}

				FRHIRenderPassInfo RPInfo(RTCount, RenderTargets, ERenderTargetActions::Clear_Store);
				RPInfo.DepthStencilRenderTarget.DepthStencilTarget = SceneContext.GetSceneDepthTexture();
				RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(ERenderTargetActions::Load_DontStore, ERenderTargetActions::Load_Store);
				RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthRead_StencilWrite;
				RHICmdList.BeginRenderPass(RPInfo, TEXT("InitialDeferredDecals"));
				bDidClearDBuffer = true;
			}
		} // if ( bNeedsDBufferTargets )
	}

	bool bHasValidDBufferMask = false;

	if (bRenderDecals)
	{
		bool bShouldResolveTargets = false;

		if (CurrentStage == DRS_BeforeBasePass || CurrentStage == DRS_BeforeLighting || CurrentStage == DRS_Emissive)
		{
			if (Context.View.MeshDecalBatches.Num() > 0)
			{
				check(bNeedsDBufferTargets || CurrentStage != DRS_BeforeBasePass);
				RenderMeshDecals(Context, CurrentStage);

				// Note: There will be an open renderpass at this point.
				// We are not ending it here in case the next decal uses the same rendertarget.
				// There is a catch-all to end an active renderpass after the scene decal rendering.
				
				bShouldResolveTargets = true;
			}
		}

		FScene& Scene = *(FScene*)ViewFamily.Scene;
		FDecalRenderTargetManager RenderTargetManager(RHICmdList, Context.GetShaderPlatform(), Context.GetFeatureLevel(), CurrentStage);

		//don't early return. Resolves must be run for fast clears to work.
		if (Scene.Decals.Num() || Context.View.MeshDecalBatches.Num() > 0)
		{
			check(bNeedsDBufferTargets || CurrentStage != DRS_BeforeBasePass)

			// Build a list of decals that need to be rendered for this view
			FTransientDecalRenderDataList SortedDecals;

			if (Scene.Decals.Num())
			{
				FDecalRendering::BuildVisibleDecalList(Scene, View, CurrentStage, &SortedDecals);
			}

			if (SortedDecals.Num() > 0)
			{
				SCOPED_DRAW_EVENTF(RHICmdList, DeferredDecalsInner, TEXT("DeferredDecalsInner %d/%d"), SortedDecals.Num(), Scene.Decals.Num());

				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				if (bDidClearDBuffer)
				{
					// If we cleared the Dbuffer above we'll be inside a renderpass here.
					check(RHICmdList.IsInsideRenderPass());
					RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
				}
				
				// Disable UAV cache flushing so we have optimal VT feedback performance.
				RHICmdList.BeginUAVOverlap();

				// optimization to have less state changes
				EDecalRasterizerState LastDecalRasterizerState = DRS_Undefined;
				FDecalDepthState LastDecalDepthState;
				int32 LastDecalBlendMode = -1;
				int32 LastDecalHasNormal = -1; // Decal state can change based on its normal property.(SM5)
				uint32 StencilRef = 0;

				FDecalRenderingCommon::ERenderTargetMode LastRenderTargetMode = FDecalRenderingCommon::RTM_Unknown;
				const ERHIFeatureLevel::Type SMFeatureLevel = Context.GetFeatureLevel();

				SCOPED_DRAW_EVENT(RHICmdList, Decals);
				INC_DWORD_STAT_BY(STAT_Decals, SortedDecals.Num());

				for (int32 DecalIndex = 0, DecalCount = SortedDecals.Num(); DecalIndex < DecalCount; DecalIndex++)
				{
					const FTransientDecalRenderData& DecalData = SortedDecals[DecalIndex];
					const FDeferredDecalProxy& DecalProxy = *DecalData.DecalProxy;
					const FMatrix ComponentToWorldMatrix = DecalProxy.ComponentTrans.ToMatrixWithScale();
					const FMatrix FrustumComponentToClip = FDecalRendering::ComputeComponentToClipMatrix(View, ComponentToWorldMatrix);

					EDecalBlendMode DecalBlendMode = FDecalRenderingCommon::ComputeDecalBlendModeForRenderStage(DecalData.FinalDecalBlendMode, CurrentStage);

					EDecalRenderStage LocalDecalStage = FDecalRenderingCommon::ComputeRenderStage(View.GetShaderPlatform(), DecalBlendMode);
					bool bStencilThisDecal = IsStencilOptimizationAvailable(LocalDecalStage);

					FDecalRenderingCommon::ERenderTargetMode CurrentRenderTargetMode = FDecalRenderingCommon::ComputeRenderTargetMode(View.GetShaderPlatform(), DecalBlendMode, DecalData.bHasNormal);

					if (bShaderComplexity)
					{
						CurrentRenderTargetMode = FDecalRenderingCommon::RTM_SceneColor;
						// we want additive blending for the ShaderComplexity mode
						DecalBlendMode = DBM_Emissive;
					}

					// Here we assume that GBuffer can only be WorldNormal since it is the only GBufferTarget handled correctly.
					if (RenderTargetManager.bGufferADirty && DecalData.MaterialResource->NeedsGBuffer())
					{
						RHICmdList.CopyToResolveTarget(SceneContext.GBufferA->GetRenderTargetItem().TargetableTexture, SceneContext.GBufferA->GetRenderTargetItem().TargetableTexture, FResolveParams());
						RenderTargetManager.TargetsToResolve[FDecalRenderTargetManager::GBufferAIndex] = nullptr;
						RenderTargetManager.bGufferADirty = false;
					}

					// fewer rendertarget switches if possible
					if (CurrentRenderTargetMode != LastRenderTargetMode)
					{
						LastRenderTargetMode = CurrentRenderTargetMode;

						RenderTargetManager.SetRenderTargetMode(CurrentRenderTargetMode, DecalData.bHasNormal, bPerPixelDBufferMask);
						Context.SetViewportAndCallRHI(Context.View.ViewRect);
						RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
					}

					check(RHICmdList.IsInsideRenderPass());

					bool bThisDecalUsesStencil = false;

					if (bStencilThisDecal && bStencilSizeThreshold)
					{
						// note this is after a SetStreamSource (in if CurrentRenderTargetMode != LastRenderTargetMode) call as it needs to get the VB input
						bThisDecalUsesStencil = RenderPreStencil(Context, ComponentToWorldMatrix, FrustumComponentToClip);

						LastDecalRasterizerState = DRS_Undefined;
						LastDecalDepthState = FDecalDepthState();
						LastDecalBlendMode = -1;
					}

					const bool bBlendStateChange = DecalBlendMode != LastDecalBlendMode;// Has decal mode changed.
					const bool bDecalNormalChanged = GSupportsSeparateRenderTargetBlendState && // has normal changed for SM5 stain/translucent decals?
						(DecalBlendMode == DBM_Translucent || DecalBlendMode == DBM_Stain) &&
						(int32)DecalData.bHasNormal != LastDecalHasNormal;

					// fewer blend state changes if possible
					if (bBlendStateChange || bDecalNormalChanged)
					{
						LastDecalBlendMode = DecalBlendMode;
						LastDecalHasNormal = (int32)DecalData.bHasNormal;

						GraphicsPSOInit.BlendState = GetDecalBlendState(SMFeatureLevel, CurrentStage, (EDecalBlendMode)LastDecalBlendMode, DecalData.bHasNormal);
					}

					// todo
					const float ConservativeRadius = DecalData.ConservativeRadius;
					//			const int32 IsInsideDecal = ((FVector)View.ViewMatrices.ViewOrigin - ComponentToWorldMatrix.GetOrigin()).SizeSquared() < FMath::Square(ConservativeRadius * 1.05f + View.NearClippingDistance * 2.0f) + ( bThisDecalUsesStencil ) ? 2 : 0;
					const bool bInsideDecal = ((FVector)View.ViewMatrices.GetViewOrigin() - ComponentToWorldMatrix.GetOrigin()).SizeSquared() < FMath::Square(ConservativeRadius * 1.05f + View.NearClippingDistance * 2.0f);
					//			const bool bInsideDecal =  !(IsInsideDecal & 1);

					// update rasterizer state if needed
					{
						bool bReverseHanded = false;
						{
							// Account for the reversal of handedness caused by negative scale on the decal
							const auto& Scale3d = DecalProxy.ComponentTrans.GetScale3D();
							bReverseHanded = Scale3d[0] * Scale3d[1] * Scale3d[2] < 0.f;
						}
						EDecalRasterizerState DecalRasterizerState = FDecalRenderingCommon::ComputeDecalRasterizerState(bInsideDecal, bReverseHanded, View.bReverseCulling);

						if (LastDecalRasterizerState != DecalRasterizerState)
						{
							LastDecalRasterizerState = DecalRasterizerState;
							GraphicsPSOInit.RasterizerState = GetDecalRasterizerState(DecalRasterizerState);
						}
					}

					// update DepthStencil state if needed
					{
						FDecalDepthState DecalDepthState = ComputeDecalDepthState(LocalDecalStage, bInsideDecal, bThisDecalUsesStencil);

						if (LastDecalDepthState != DecalDepthState)
						{
							LastDecalDepthState = DecalDepthState;
							GraphicsPSOInit.DepthStencilState = GetDecalDepthState(StencilRef, DecalDepthState);
						}
					}

					GraphicsPSOInit.PrimitiveType = PT_TriangleList;

					FDecalRendering::SetShader(RHICmdList, GraphicsPSOInit, View, DecalData, CurrentStage, FrustumComponentToClip);
					RHICmdList.SetStencilRef(StencilRef);

					RHICmdList.DrawIndexedPrimitive(GetUnitCubeIndexBuffer(), 0, 0, 8, 0, UE_ARRAY_COUNT(GCubeIndices) / 3, 1);
					RenderTargetManager.bGufferADirty |= (RenderTargetManager.TargetsToResolve[FDecalRenderTargetManager::GBufferAIndex] != nullptr);
				}

				check(RHICmdList.IsInsideRenderPass());
				// Finished rendering sorted decals, so end the renderpass.
				RHICmdList.EndRenderPass();
				
				RHICmdList.EndUAVOverlap();
			}

			if (RHICmdList.IsInsideRenderPass())
			{
				// If the SortedDecals list is empty we may have started a renderpass to clear the dbuffer.
				// If we only draw mesh decals we'll have an active renderpass here as well.
				RHICmdList.EndRenderPass();
			}

			// This stops the targets from being resolved and decoded until the last view is rendered.
			// This is done so as to not run eliminate fast clear on the views before the end.
			bool bLastView = Context.View.Family->Views.Last() == &Context.View;
			if ((Scene.Decals.Num() > 0) && bLastView && CurrentStage == DRS_AmbientOcclusion)
			{
				// we don't modify stencil but if out input was having stencil for us (after base pass - we need to clear)
				// Clear stencil to 0, which is the assumed default by other passes

				FRHIRenderPassInfo RPInfo;
				RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(ERenderTargetActions::DontLoad_DontStore, ERenderTargetActions::Clear_Store);
				RPInfo.DepthStencilRenderTarget.DepthStencilTarget = SceneContext.GetSceneDepthSurface();
				RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthNop_StencilWrite;
				RPInfo.DepthStencilRenderTarget.ResolveTarget = nullptr;

				RHICmdList.TransitionResource(
					RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil,
					RPInfo.DepthStencilRenderTarget.DepthStencilTarget);

				RHICmdList.BeginRenderPass(RPInfo, TEXT("ClearStencil"));
				RHICmdList.EndRenderPass();
			}

			if (CurrentStage == DRS_BeforeBasePass)
			{
				if (bLastView)
				{
					if (RHISupportsRenderTargetWriteMask(GMaxRHIShaderPlatform))
					{
						SCOPED_DRAW_EVENTF(RHICmdList, DeferredDecals, TEXT("Combine DBuffer WriteMasks"));

						// combine DBuffer RTWriteMasks; will end up in one texture we can load from in the base pass PS and decide whether to do the actual work or not
						IPooledRenderTarget* Textures[3] =
						{
							SceneContext.DBufferA,
							SceneContext.DBufferB,
							SceneContext.DBufferC
						};
						FRenderTargetWriteMask::Decode<3>(Context.RHICmdList, Context.GetShaderMap(), Textures, SceneContext.DBufferMask, GFastVRamConfig.DBufferMask, TEXT("DBufferMask"));
					}

					if (SceneContext.DBufferMask)
					{
						GVisualizeTexture.SetCheckPoint(RHICmdList, SceneContext.DBufferMask);
						bHasValidDBufferMask = true;
					}
				}
			}

			if (bLastView || !RHISupportsRenderTargetWriteMask(GMaxRHIShaderPlatform))
			{
				bShouldResolveTargets = true;
			}
		}

		if (bShouldResolveTargets)
		{
			RenderTargetManager.ResolveTargets();
		}

		if (CurrentStage == DRS_BeforeBasePass && bNeedsDBufferTargets)
		{
			// before BasePass
			GVisualizeTexture.SetCheckPoint(RHICmdList, SceneContext.DBufferA);
			GVisualizeTexture.SetCheckPoint(RHICmdList, SceneContext.DBufferB);
			GVisualizeTexture.SetCheckPoint(RHICmdList, SceneContext.DBufferC);
		}
	}

	if (CurrentStage == DRS_BeforeBasePass && !bHasValidDBufferMask)
	{
		// Return the DBufferMask to the render target pool.
		// FSceneTextureShaderParameters will fall back to setting a white dummy mask texture.
		// This allows us to ignore the DBufferMask on frames without decals, without having to explicitly clear the texture.
		SceneContext.DBufferMask = nullptr;
	}

	check(RHICmdList.IsOutsideRenderPass());
}

FPooledRenderTargetDesc FRCPassPostProcessDeferredDecals::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	// This pass creates it's own output so the compositing graph output isn't needed.
	FPooledRenderTargetDesc Ret;

	Ret.DebugName = TEXT("DeferredDecals");

	return Ret;
}

//
// FDecalRenderTargetManager class
//
void FDecalRenderTargetManager::ResolveTargets()
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	// If GBuffer A is dirty, mark it as needing resolve since the content of TargetsToResolve[GBufferAIndex] could have been nullified by modes like RTM_SceneColorAndGBufferNoNormal
	if (bGufferADirty)
	{
		TargetsToResolve[FDecalRenderTargetManager::GBufferAIndex] = SceneContext.GBufferA->GetRenderTargetItem().TargetableTexture;
	}
	if (bGufferBCDirty)
	{
		TargetsToResolve[FDecalRenderTargetManager::GBufferBIndex] = SceneContext.GBufferB->GetRenderTargetItem().TargetableTexture;
		TargetsToResolve[FDecalRenderTargetManager::GBufferCIndex] = SceneContext.GBufferC->GetRenderTargetItem().TargetableTexture;
	}

	//those have been cleared or rendered to and need to be resolved
	TargetsToResolve[FDecalRenderTargetManager::DBufferAIndex] = SceneContext.DBufferA ? SceneContext.DBufferA->GetRenderTargetItem().TargetableTexture : nullptr;
	TargetsToResolve[FDecalRenderTargetManager::DBufferBIndex] = SceneContext.DBufferB ? SceneContext.DBufferB->GetRenderTargetItem().TargetableTexture : nullptr;
	TargetsToResolve[FDecalRenderTargetManager::DBufferCIndex] = SceneContext.DBufferC ? SceneContext.DBufferC->GetRenderTargetItem().TargetableTexture : nullptr;

	// resolve the targets we wrote to.
	FResolveParams ResolveParams;
	for (int32 i = 0; i < ResolveBufferMax; ++i)
	{
		if (TargetsToResolve[i])
		{
			RHICmdList.CopyToResolveTarget(TargetsToResolve[i], TargetsToResolve[i], ResolveParams);
		}
	}
}


FDecalRenderTargetManager::FDecalRenderTargetManager(FRHICommandList& InRHICmdList, EShaderPlatform ShaderPlatform, ERHIFeatureLevel::Type InFeatureLevel, EDecalRenderStage CurrentStage)
	: RHICmdList(InRHICmdList)
	, bGufferADirty(false)
	, bGufferBCDirty(false)
	, FeatureLevel(InFeatureLevel)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	for (uint32 i = 0; i < ResolveBufferMax; ++i)
	{
		TargetsToTransitionWritable[i] = true;
		TargetsToResolve[i] = nullptr;
	}

	if (SceneContext.DBufferA)
	{
		TargetsToResolve[DBufferAIndex] = SceneContext.DBufferA->GetRenderTargetItem().TargetableTexture;
	}
	if (SceneContext.DBufferB)
	{
		TargetsToResolve[DBufferBIndex] = SceneContext.DBufferB->GetRenderTargetItem().TargetableTexture;
	}

	if (SceneContext.DBufferC)
	{
		TargetsToResolve[DBufferCIndex] = SceneContext.DBufferC->GetRenderTargetItem().TargetableTexture;
	}

	if (!IsAnyForwardShadingEnabled(ShaderPlatform))
	{
		// Normal buffer is already dirty at this point and needs resolve before being read from (irrelevant for DBuffer).
		bGufferADirty = (CurrentStage == DRS_AfterBasePass) || (CurrentStage == DRS_BeforeLighting);
		bGufferBCDirty = (CurrentStage == DRS_BeforeLighting);
	}
}

void FDecalRenderTargetManager::SetRenderTargetMode(FDecalRenderingCommon::ERenderTargetMode CurrentRenderTargetMode, bool bHasNormal, bool bPerPixelDBufferMask)
{
	// There are several situations where we do not have a renderpass active when we get here.
	// The first decal or mesh to draw, etc.
	if (RHICmdList.IsInsideRenderPass())
	{
		RHICmdList.EndRenderPass();
	}

	check(!RHICmdList.IsInsideRenderPass());

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	// If GBufferA was resolved for read, and we want to write to it again.
	if (!bGufferADirty && IsWritingToGBufferA(CurrentRenderTargetMode))
	{
		// This is required to be compliant with RHISetRenderTargets resource transition code : const bool bAccessValid = !bReadable || LastFrameWritten != CurrentFrame;
		// If the normal buffer was resolved as a texture before, then bReadable && LastFrameWritten == CurrentFrame, and an error msg will be triggered. 
		// Which is not needed here since no more read will be done at this point (at least not before any other CopyToResolvedTarget).
		RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, SceneContext.GBufferA->GetRenderTargetItem().TargetableTexture);
	}

	// Color
	ERenderTargetActions ColorTargetActions = ERenderTargetActions::Load_Store;

	// Depth
	FExclusiveDepthStencil DepthStencilAccess = FExclusiveDepthStencil::DepthRead_StencilWrite;
	ERenderTargetActions DepthTargetActions = ERenderTargetActions::Load_DontStore;
	uint32 NumColorTargets = 1;
	FRHITexture** TargetsToBind = &TargetsToResolve[0];
	FRHITexture* DepthTarget = SceneContext.GetSceneDepthSurface();

	// The SceneColorAndGBuffer modes do not actually need GBufferA bound when there's no normal.
	// The apis based on renderpasses fill fail to actually bind anything past a null entry in their RT list so we have to bind it anyway.
	// DX11 will drop writes to an unbound RT so it worked there.
	switch (CurrentRenderTargetMode)
	{
	case FDecalRenderingCommon::RTM_SceneColorAndGBufferWithNormal:
	case FDecalRenderingCommon::RTM_SceneColorAndGBufferNoNormal:
		TargetsToResolve[SceneColorIndex] = SceneContext.GetSceneColor()->GetRenderTargetItem().TargetableTexture;
		TargetsToResolve[GBufferAIndex] = bHasNormal ? SceneContext.GBufferA->GetRenderTargetItem().TargetableTexture : nullptr;
		TargetsToResolve[GBufferBIndex] = SceneContext.GBufferB->GetRenderTargetItem().TargetableTexture;
		TargetsToResolve[GBufferCIndex] = SceneContext.GBufferC->GetRenderTargetItem().TargetableTexture;

		NumColorTargets = 3 + (bHasNormal ? 1 : 0);
		break;

	case FDecalRenderingCommon::RTM_SceneColorAndGBufferDepthWriteWithNormal:
	case FDecalRenderingCommon::RTM_SceneColorAndGBufferDepthWriteNoNormal:
		TargetsToResolve[SceneColorIndex] = SceneContext.GetSceneColor()->GetRenderTargetItem().TargetableTexture;
		TargetsToResolve[GBufferAIndex] = bHasNormal ? SceneContext.GBufferA->GetRenderTargetItem().TargetableTexture : nullptr;
		TargetsToResolve[GBufferBIndex] = SceneContext.GBufferB->GetRenderTargetItem().TargetableTexture;
		TargetsToResolve[GBufferCIndex] = SceneContext.GBufferC->GetRenderTargetItem().TargetableTexture;
		TargetsToResolve[GBufferEIndex] = SceneContext.GBufferE->GetRenderTargetItem().TargetableTexture;

		NumColorTargets = 4 + (bHasNormal ? 1 : 0);
		DepthStencilAccess = FExclusiveDepthStencil::DepthWrite_StencilWrite;
		DepthTargetActions = ERenderTargetActions::Load_Store;
		break;

	case FDecalRenderingCommon::RTM_GBufferNormal:
		TargetsToResolve[GBufferAIndex] = SceneContext.GBufferA->GetRenderTargetItem().TargetableTexture;

		TargetsToBind = &TargetsToResolve[GBufferAIndex];
		break;

	case FDecalRenderingCommon::RTM_SceneColor:
		TargetsToResolve[SceneColorIndex] = SceneContext.GetSceneColor()->GetRenderTargetItem().TargetableTexture;

		TargetsToBind = &TargetsToResolve[SceneColorIndex];
		break;

	case FDecalRenderingCommon::RTM_DBuffer:
	{
		TargetsToResolve[DBufferAIndex] = SceneContext.DBufferA->GetRenderTargetItem().TargetableTexture;
		TargetsToResolve[DBufferBIndex] = SceneContext.DBufferB->GetRenderTargetItem().TargetableTexture;
		TargetsToResolve[DBufferCIndex] = SceneContext.DBufferC->GetRenderTargetItem().TargetableTexture;
		NumColorTargets = 3;

		if (bPerPixelDBufferMask)
		{
			TargetsToResolve[DBufferMaskIndex] = SceneContext.DBufferMask->GetRenderTargetItem().TargetableTexture;
			NumColorTargets = 4;
		}

		DepthTarget = SceneContext.GetSceneDepthTexture();

		TargetsToBind = &TargetsToResolve[DBufferAIndex];
		break;
	}

	case FDecalRenderingCommon::RTM_AmbientOcclusion:
	{
		TargetsToResolve[SceneColorIndex] = SceneContext.ScreenSpaceAO->GetRenderTargetItem().TargetableTexture;
		RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, TargetsToResolve[SceneColorIndex]);

		TargetsToBind = &TargetsToResolve[SceneColorIndex];

		if (!SceneContext.bScreenSpaceAOIsValid)
		{
			ColorTargetActions = ERenderTargetActions::Clear_Store;
		}

		SceneContext.bScreenSpaceAOIsValid = true;
		break;
	}

	default:
		check(0);
		break;
	}

	uint32 WriteIdx = 0;
	FRHITexture* ValidTargetsToBind[MaxSimultaneousRenderTargets] = { nullptr };

	for (uint32 i = 0; WriteIdx < NumColorTargets; ++i)
	{
		if (!TargetsToBind[i])
		{
			continue;
		}

		ValidTargetsToBind[WriteIdx] = TargetsToBind[i];
		++WriteIdx;
	}

	FRHIRenderPassInfo RPInfo(NumColorTargets, ValidTargetsToBind, ColorTargetActions);
	RPInfo.DepthStencilRenderTarget.DepthStencilTarget = DepthTarget;
	RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(DepthTargetActions, ERenderTargetActions::Load_Store);
	RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = DepthStencilAccess;

	if (UseVirtualTexturing(FeatureLevel))
	{
		SceneContext.BindVirtualTextureFeedbackUAV(RPInfo);
	}

	if (TargetsToTransitionWritable[CurrentRenderTargetMode])
	{
		TransitionRenderPassTargets(RHICmdList, RPInfo);
	}
	RHICmdList.BeginRenderPass(RPInfo, TEXT("DecalPass"));

	TargetsToTransitionWritable[CurrentRenderTargetMode] = false;
}
