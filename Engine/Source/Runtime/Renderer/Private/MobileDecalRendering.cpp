// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MobileDecalRendering.cpp: Decals for mobile renderer
=============================================================================*/

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "RHIDefinitions.h"
#include "RHI.h"
#include "SceneUtils.h"
#include "RHIStaticStates.h"
#include "PostProcess/SceneRenderTargets.h"
#include "SceneRendering.h"
#include "ScenePrivate.h"
#include "DecalRenderingShared.h"


static FRasterizerStateRHIParamRef GetDecalRasterizerState(EDecalRasterizerState DecalRasterizerState)
{
	switch (DecalRasterizerState)
	{
	case DRS_CW: return TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI();
	case DRS_CCW: return TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI();
	default: check(0); return nullptr;
	}
}

void FMobileSceneRenderer::RenderDecals(FRHICommandListImmediate& RHICmdList)
{
	if (Scene->Decals.Num() == 0 || !IsMobileHDR())
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_DecalsDrawTime);

	EShaderPlatform ShaderPlatform = ViewFamily.GetShaderPlatform();
	if (ShaderPlatform != SP_METAL) // temporary workaround for iOS Metal to avoid restarting render-pass
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		SceneContext.BeginRenderingSceneColor(RHICmdList, ESimpleRenderTargetMode::EExistingColorAndDepth, FExclusiveDepthStencil::DepthRead_StencilRead);
	}
	
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		
		// Build a list of decals that need to be rendered for this view
		FTransientDecalRenderDataList SortedDecals;
		FDecalRendering::BuildVisibleDecalList(*Scene, View, DRS_Mobile, &SortedDecals);

		if (SortedDecals.Num())
		{
			SCOPED_DRAW_EVENT(RHICmdList, DeferredDecals);
			INC_DWORD_STAT_BY(STAT_Decals, SortedDecals.Num());
		
			RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1);
			RHICmdList.SetStreamSource(0, GetUnitCubeVertexBuffer(), 0);

			EDecalRasterizerState LastDecalRasterizerState = DRS_Undefined;
			TOptional<EDecalBlendMode> LastDecalBlendMode;
			TOptional<bool> LastDecalDepthState;
			bool bEncodedHDR = GetMobileHDRMode() == EMobileHDRMode::EnabledRGBE;
			if (bEncodedHDR)
			{
				GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			}
			for (int32 DecalIndex = 0, DecalCount = SortedDecals.Num(); DecalIndex < DecalCount; DecalIndex++)
			{
				const FTransientDecalRenderData& DecalData = SortedDecals[DecalIndex];
				const FDeferredDecalProxy& DecalProxy = *DecalData.DecalProxy;
				const FMatrix ComponentToWorldMatrix = DecalProxy.ComponentTrans.ToMatrixWithScale();
				const FMatrix FrustumComponentToClip = FDecalRendering::ComputeComponentToClipMatrix(View, ComponentToWorldMatrix);
						
				const float ConservativeRadius = DecalData.ConservativeRadius;
				const bool bInsideDecal = ((FVector)View.ViewMatrices.GetViewOrigin() - ComponentToWorldMatrix.GetOrigin()).SizeSquared() < FMath::Square(ConservativeRadius * 1.05f + View.NearClippingDistance * 2.0f);

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
				if (!LastDecalDepthState.IsSet() || LastDecalDepthState.GetValue() != bInsideDecal)
				{
					LastDecalDepthState = bInsideDecal;
					if (bInsideDecal)
					{
						GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
							false, CF_Always,
							true, CF_Equal, SO_Keep, SO_Keep, SO_Keep,
							false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
							GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1), 0x00>::GetRHI();
					}
					else
					{
						GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
							false, CF_DepthNearOrEqual,
							true, CF_Equal, SO_Keep, SO_Keep, SO_Keep,
							false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
							GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1), 0x00>::GetRHI();
					}
				}

				// update BlendMode if needed
				if (!bEncodedHDR && (!LastDecalBlendMode.IsSet() || LastDecalBlendMode.GetValue() != DecalData.DecalBlendMode))
				{
					LastDecalBlendMode = DecalData.DecalBlendMode;
					switch(DecalData.DecalBlendMode)
					{
					case DBM_Translucent:
						GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha>::GetRHI();
						break;
					case DBM_Stain:
						// Modulate
						GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_DestColor, BF_InverseSourceAlpha>::GetRHI();
						break;
					case DBM_Emissive:
						// Additive
						GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_SourceAlpha, BF_One>::GetRHI();
						break;
					case DBM_AlphaComposite:
						// Premultiplied alpha
						GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
						break;
					default:
						check(0);
					};
				}

				// Set shader params
				FDecalRendering::SetShader(RHICmdList, GraphicsPSOInit, View, DecalData, FrustumComponentToClip);
			
				RHICmdList.DrawIndexedPrimitive(GetUnitCubeIndexBuffer(), PT_TriangleList, 0, 0, 8, 0, ARRAY_COUNT(GCubeIndices) / 3, 1);
			}
		}
	}
}
