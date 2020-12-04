// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
 
=============================================================================*/
#include "MobileReflectionEnvironmentCapture.h"
#include "ReflectionEnvironmentCapture.h"
#include "ShaderParameterUtils.h"
#include "RHIStaticStates.h"
#include "SceneRenderTargets.h"
#include "SceneUtils.h"
#include "ScreenRendering.h"
#include "PipelineStateCache.h"
#include "SceneFilterRendering.h"
#include "OneColorShader.h"

extern int32 GDiffuseIrradianceCubemapSize;
extern float ComputeSingleAverageBrightnessFromCubemap(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, int32 TargetSize, FSceneRenderTargetItem& Cubemap);
extern void FullyResolveReflectionScratchCubes(FRHICommandListImmediate& RHICmdList);

static TAutoConsoleVariable<int32> CVarMobileUseHighQualitySkyCaptureFiltering(
	TEXT("r.Mobile.HighQualitySkyCaptureFiltering"),
	1,
	TEXT("1: (default) use high quality filtering when generating mobile sky captures.")
	TEXT("0: use simple bilinear filtering when generating mobile sky captures."),
	ECVF_RenderThreadSafe);

class FMobileDownsamplePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FMobileDownsamplePS, Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsMobilePlatform(Parameters.Platform);
	}

	FMobileDownsamplePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		CubeFace.Bind(Initializer.ParameterMap, TEXT("CubeFace"));
		SourceMipIndex.Bind(Initializer.ParameterMap, TEXT("SourceMipIndex"));
		SourceCubemapTexture.Bind(Initializer.ParameterMap, TEXT("SourceCubemapTexture"));
		SourceCubemapSampler.Bind(Initializer.ParameterMap, TEXT("SourceCubemapSampler"));
	}
	FMobileDownsamplePS() {}

	void SetParameters(FRHICommandList& RHICmdList, int32 CubeFaceValue, int32 SourceMipIndexValue, FSceneRenderTargetItem& SourceTextureValue)
	{
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), CubeFace, CubeFaceValue);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), SourceMipIndex, SourceMipIndexValue);

		SetTextureParameter(
			RHICmdList,
			RHICmdList.GetBoundPixelShader(),
			SourceCubemapTexture,
			SourceCubemapSampler,
			TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),
			SourceTextureValue.ShaderResourceTexture);
	}

private:
	LAYOUT_FIELD(FShaderParameter, CubeFace)
	LAYOUT_FIELD(FShaderParameter, SourceMipIndex)
	LAYOUT_FIELD(FShaderResourceParameter, SourceCubemapTexture)
	LAYOUT_FIELD(FShaderResourceParameter, SourceCubemapSampler)
};

IMPLEMENT_SHADER_TYPE(, FMobileDownsamplePS, TEXT("/Engine/Private/ReflectionEnvironmentShaders.usf"), TEXT("DownsamplePS_Mobile"), SF_Pixel);
namespace MobileReflectionEnvironmentCapture
{
	/** Encapsulates render target picking logic for cubemap mip generation. */
	FSceneRenderTargetItem& GetEffectiveRenderTarget(FSceneRenderTargets& SceneContext, bool bDownsamplePass, int32 TargetMipIndex)
	{
		int32 ScratchTextureIndex = TargetMipIndex % 2;

		if (!bDownsamplePass)
		{
			ScratchTextureIndex = 1 - ScratchTextureIndex;
		}

		return SceneContext.ReflectionColorScratchCubemap[ScratchTextureIndex]->GetRenderTargetItem();
	}

	/** Encapsulates source texture picking logic for cubemap mip generation. */
	FSceneRenderTargetItem& GetEffectiveSourceTexture(FSceneRenderTargets& SceneContext, bool bDownsamplePass, int32 TargetMipIndex)
	{
		int32 ScratchTextureIndex = TargetMipIndex % 2;

		if (bDownsamplePass)
		{
			ScratchTextureIndex = 1 - ScratchTextureIndex;
		}

		return SceneContext.ReflectionColorScratchCubemap[ScratchTextureIndex]->GetRenderTargetItem();
	}

	void ComputeAverageBrightness(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, int32 CubmapSize, float& OutAverageBrightness)
	{
		SCOPED_DRAW_EVENT(RHICmdList, ComputeAverageBrightness);

		const int32 EffectiveTopMipSize = CubmapSize;
		const int32 NumMips = FMath::CeilLogTwo(EffectiveTopMipSize) + 1;

		// necessary to resolve the clears which touched all the mips.  scene rendering only resolves mip 0.
		FullyResolveReflectionScratchCubes(RHICmdList);

		auto ShaderMap = GetGlobalShaderMap(FeatureLevel);

		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

		{
			SCOPED_DRAW_EVENT(RHICmdList, DownsampleCubeMips);

			// Downsample all the mips, each one reads from the mip above it
			for (int32 MipIndex = 1; MipIndex < NumMips; MipIndex++)
			{
				const int32 SourceMipIndex = FMath::Max(MipIndex - 1, 0);
				const int32 MipSize = 1 << (NumMips - MipIndex - 1);

				FSceneRenderTargetItem& EffectiveRT = GetEffectiveRenderTarget(SceneContext, true, MipIndex);
				FSceneRenderTargetItem& EffectiveSource = GetEffectiveSourceTexture(SceneContext, true, MipIndex);
				check(EffectiveRT.TargetableTexture != EffectiveSource.ShaderResourceTexture);

				for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
				{
					FRHIRenderPassInfo RPInfo(EffectiveRT.TargetableTexture, ERenderTargetActions::Load_Store);
					RPInfo.ColorRenderTargets[0].ArraySlice = CubeFace;
					RPInfo.ColorRenderTargets[0].MipIndex = MipIndex;
					TransitionRenderPassTargets(RHICmdList, RPInfo);
					RHICmdList.BeginRenderPass(RPInfo, TEXT("AverageBrightness"));
					{
						FGraphicsPipelineStateInitializer GraphicsPSOInit;
						RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
						GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
						GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
						GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();

						const FIntRect ViewRect(0, 0, MipSize, MipSize);
						RHICmdList.SetViewport(0, 0, 0.0f, MipSize, MipSize, 1.0f);

						TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
						TShaderMapRef<FMobileDownsamplePS> PixelShader(ShaderMap);

						GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
						GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
						GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
						GraphicsPSOInit.PrimitiveType = PT_TriangleList;
						SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

						PixelShader->SetParameters(RHICmdList, CubeFace, SourceMipIndex, EffectiveSource);

						DrawRectangle(
							RHICmdList,
							ViewRect.Min.X, ViewRect.Min.Y,
							ViewRect.Width(), ViewRect.Height(),
							ViewRect.Min.X, ViewRect.Min.Y,
							ViewRect.Width(), ViewRect.Height(),
							FIntPoint(ViewRect.Width(), ViewRect.Height()),
							FIntPoint(MipSize, MipSize),
							VertexShader);
					}
					RHICmdList.EndRenderPass();

					FResolveParams ResolveParams(FResolveRect(), (ECubeFace)CubeFace, MipIndex);
					ResolveParams.SourceAccessFinal = ERHIAccess::SRVMask;
					ResolveParams.DestAccessFinal = ERHIAccess::SRVMask;
					RHICmdList.CopyToResolveTarget(EffectiveRT.TargetableTexture, EffectiveRT.ShaderResourceTexture, ResolveParams);
				}
			}
		}

		OutAverageBrightness = ComputeSingleAverageBrightnessFromCubemap(RHICmdList, FeatureLevel, CubmapSize, GetEffectiveRenderTarget(FSceneRenderTargets::Get(RHICmdList), true, NumMips - 1));
	}

	void CopyToSkyTexture(FRHICommandList& RHICmdList, FScene* Scene, FTexture* ProcessedTexture)
	{
		SCOPED_DRAW_EVENT(RHICmdList, CopyToSkyTexture);
		if (ProcessedTexture->TextureRHI)
		{
			const bool bUseHQFiltering = CVarMobileUseHighQualitySkyCaptureFiltering.GetValueOnRenderThread() == 1;
			const int32 EffectiveTopMipSize = ProcessedTexture->GetSizeX();
			const int32 NumMips = FMath::CeilLogTwo(EffectiveTopMipSize) + 1;
			FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

			RHICmdList.Transition(FRHITransitionInfo(ProcessedTexture->TextureRHI, ERHIAccess::Unknown, ERHIAccess::CopyDest));

			FRHICopyTextureInfo CopyInfo;
			CopyInfo.Size = FIntVector(ProcessedTexture->GetSizeX(), ProcessedTexture->GetSizeY(), 1);
			CopyInfo.NumSlices = 6;

			// GPU copy back to the skylight's texture, which is not a render target
			for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
			{
				// For simple mobile bilin filtering the source for this copy is the dest from the filtering pass.
				// In the HQ case, the full image is contained in GetEffectiveRenderTarget(.., false,0).
				FSceneRenderTargetItem& EffectiveSource = GetEffectiveRenderTarget(SceneContext, false, bUseHQFiltering ? 0 : MipIndex);
				RHICmdList.Transition(FRHITransitionInfo(EffectiveSource.ShaderResourceTexture, ERHIAccess::Unknown, ERHIAccess::CopySrc));
				RHICmdList.CopyTexture(EffectiveSource.ShaderResourceTexture, ProcessedTexture->TextureRHI, CopyInfo);

				++CopyInfo.SourceMipIndex;
				++CopyInfo.DestMipIndex;
				CopyInfo.Size.X = FMath::Max(1, CopyInfo.Size.X / 2);
				CopyInfo.Size.Y = FMath::Max(1, CopyInfo.Size.Y / 2);
			}

			RHICmdList.Transition(FRHITransitionInfo(ProcessedTexture->TextureRHI, ERHIAccess::CopyDest, ERHIAccess::SRVMask));
		}
	}

	/** Generates mips for glossiness and filters the cubemap for a given reflection. */
	void FilterReflectionEnvironment(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, int32 CubmapSize, FSHVectorRGB3* OutIrradianceEnvironmentMap)
	{
		SCOPED_DRAW_EVENT(RHICmdList, FilterReflectionEnvironment);

		const int32 EffectiveTopMipSize = CubmapSize;
		const int32 NumMips = FMath::CeilLogTwo(EffectiveTopMipSize) + 1;
		const bool bUseHQFiltering = CVarMobileUseHighQualitySkyCaptureFiltering.GetValueOnRenderThread() == 1;
		{
			FSceneRenderTargetItem& EffectiveColorRT = FSceneRenderTargets::Get(RHICmdList).ReflectionColorScratchCubemap[0]->GetRenderTargetItem();
			// Premultiply alpha in-place using alpha blending
			for (uint32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
			{
				FRHIRenderPassInfo RPInfo(EffectiveColorRT.TargetableTexture, ERenderTargetActions::Load_Store);
				RPInfo.ColorRenderTargets[0].ArraySlice = CubeFace;
				RPInfo.ColorRenderTargets[0].MipIndex = 0;

				TransitionRenderPassTargets(RHICmdList, RPInfo);
				RHICmdList.BeginRenderPass(RPInfo, TEXT("FilterReflectionEnvironment"));
				{
					FGraphicsPipelineStateInitializer GraphicsPSOInit;
					RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
					GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
					GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_Zero, BF_DestAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();

					const FIntPoint SourceDimensions(CubmapSize, CubmapSize);
					const FIntRect ViewRect(0, 0, EffectiveTopMipSize, EffectiveTopMipSize);
					RHICmdList.SetViewport(0, 0, 0.0f, EffectiveTopMipSize, EffectiveTopMipSize, 1.0f);

					TShaderMapRef<FScreenVS> VertexShader(GetGlobalShaderMap(FeatureLevel));
					TShaderMapRef<FOneColorPS> PixelShader(GetGlobalShaderMap(FeatureLevel));

					GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
					GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
					GraphicsPSOInit.PrimitiveType = PT_TriangleList;

					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

					FLinearColor UnusedColors[1] = { FLinearColor::Black };
					PixelShader->SetColors(RHICmdList, UnusedColors, UE_ARRAY_COUNT(UnusedColors));

					DrawRectangle(
						RHICmdList,
						ViewRect.Min.X, ViewRect.Min.Y,
						ViewRect.Width(), ViewRect.Height(),
						0, 0,
						SourceDimensions.X, SourceDimensions.Y,
						FIntPoint(ViewRect.Width(), ViewRect.Height()),
						SourceDimensions,
						VertexShader);
				}
				RHICmdList.EndRenderPass();

				FResolveParams ResolveParams(FResolveRect(), (ECubeFace)CubeFace);
				ResolveParams.SourceAccessFinal = ERHIAccess::SRVMask;
				ResolveParams.DestAccessFinal = ERHIAccess::SRVMask;
				RHICmdList.CopyToResolveTarget(EffectiveColorRT.TargetableTexture, EffectiveColorRT.ShaderResourceTexture, ResolveParams);
			} // end for
		}

		int32 DiffuseConvolutionSourceMip = INDEX_NONE;
		FSceneRenderTargetItem* DiffuseConvolutionSource = NULL;

		auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

		{
			SCOPED_DRAW_EVENT(RHICmdList, DownsampleCubeMips);
			// Downsample all the mips, each one reads from the mip above it
			for (int32 MipIndex = 1; MipIndex < NumMips; MipIndex++)
			{
				SCOPED_DRAW_EVENT(RHICmdList, DownsampleCubeMip);
				const int32 SourceMipIndex = FMath::Max(MipIndex - 1, 0);
				const int32 MipSize = 1 << (NumMips - MipIndex - 1);

				FSceneRenderTargetItem& EffectiveRT = GetEffectiveRenderTarget(SceneContext, true, MipIndex);
				FSceneRenderTargetItem& EffectiveSource = GetEffectiveSourceTexture(SceneContext, true, MipIndex);
				check(EffectiveRT.TargetableTexture != EffectiveSource.ShaderResourceTexture);

				for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
				{
					FRHIRenderPassInfo RPInfo(EffectiveRT.TargetableTexture, ERenderTargetActions::Load_Store);
					RPInfo.ColorRenderTargets[0].ArraySlice = CubeFace;
					RPInfo.ColorRenderTargets[0].MipIndex = MipIndex;
					TransitionRenderPassTargets(RHICmdList, RPInfo);

					RHICmdList.BeginRenderPass(RPInfo, TEXT("DownsampleCubemap"));
					{

						FGraphicsPipelineStateInitializer GraphicsPSOInit;
						RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
						GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
						GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
						GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();

						const FIntRect ViewRect(0, 0, MipSize, MipSize);
						RHICmdList.SetViewport(0, 0, 0.0f, MipSize, MipSize, 1.0f);

						TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
						TShaderMapRef<FMobileDownsamplePS> PixelShader(ShaderMap);

						GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
						GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
						GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
						GraphicsPSOInit.PrimitiveType = PT_TriangleList;

						SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

						PixelShader->SetParameters(RHICmdList, CubeFace, SourceMipIndex, EffectiveSource);

						DrawRectangle(
							RHICmdList,
							ViewRect.Min.X, ViewRect.Min.Y,
							ViewRect.Width(), ViewRect.Height(),
							ViewRect.Min.X, ViewRect.Min.Y,
							ViewRect.Width(), ViewRect.Height(),
							FIntPoint(ViewRect.Width(), ViewRect.Height()),
							FIntPoint(MipSize, MipSize),
							VertexShader);
					}
					RHICmdList.EndRenderPass();

					FResolveParams ResolveParams(FResolveRect(), (ECubeFace)CubeFace, MipIndex);
					ResolveParams.SourceAccessFinal = ERHIAccess::SRVMask;
					ResolveParams.DestAccessFinal = ERHIAccess::SRVMask;
					RHICmdList.CopyToResolveTarget(EffectiveRT.TargetableTexture, EffectiveRT.ShaderResourceTexture, ResolveParams);
				}

				if (DiffuseConvolutionSource == NULL && MipSize <= GDiffuseIrradianceCubemapSize)
				{
					DiffuseConvolutionSourceMip = MipIndex;
					DiffuseConvolutionSource = &EffectiveRT;
				}
			}
		}

		if (OutIrradianceEnvironmentMap)
		{
			SCOPED_DRAW_EVENT(RHICmdList, ComputeDiffuseIrradiance);
			check(DiffuseConvolutionSource != NULL);
			ComputeDiffuseIrradiance(RHICmdList, FeatureLevel, DiffuseConvolutionSource->ShaderResourceTexture, DiffuseConvolutionSourceMip, OutIrradianceEnvironmentMap);
		}

		if (bUseHQFiltering)
		{
			// When HQ filtering is enabled the filter shader requires access to all mips levels of the source cubemap.
			// Here we ensure that GetEffectiveSourceTexture(0,false) will have a complete set of mips for this process.
			SCOPED_DRAW_EVENT(RHICmdList, PrepareSourceCubemapMipsForHQFiltering);
			for (int32 MipIndex = 1; MipIndex < NumMips; MipIndex += 2)
			{
				FSceneRenderTargetItem& SourceTarget = GetEffectiveRenderTarget(SceneContext, true, MipIndex);
				FSceneRenderTargetItem& DestTarget = GetEffectiveSourceTexture(SceneContext, true, MipIndex);
				check(DestTarget.TargetableTexture != SourceTarget.ShaderResourceTexture);

				// Transition the textures once, so CopyToResolveTarget doesn't ping-pong uselessly between the copy and SRV states.
				FRHITransitionInfo TransitionsBefore[] = {
					FRHITransitionInfo(SourceTarget.ShaderResourceTexture, ERHIAccess::SRVMask, ERHIAccess::CopySrc),
					FRHITransitionInfo(DestTarget.ShaderResourceTexture, ERHIAccess::SRVMask, ERHIAccess::CopyDest)
				};
				RHICmdList.Transition(MakeArrayView(TransitionsBefore, UE_ARRAY_COUNT(TransitionsBefore)));

				// Tell CopyToResolveTarget to leave the textures in the copy state, because we'll transition them only once when we're done.
				FResolveParams ResolveParams(FResolveRect(), CubeFace_PosX, MipIndex);
				ResolveParams.SourceAccessFinal = ERHIAccess::CopySrc;
				ResolveParams.DestAccessFinal = ERHIAccess::CopyDest;

				for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
				{
					ResolveParams.CubeFace = (ECubeFace)CubeFace;
					RHICmdList.CopyToResolveTarget(SourceTarget.ShaderResourceTexture, DestTarget.ShaderResourceTexture, ResolveParams);
				}

				// We're done copying, transition the textures back to SRV.
				FRHITransitionInfo TransitionsAfter[] = {
					FRHITransitionInfo(SourceTarget.ShaderResourceTexture, ERHIAccess::CopySrc, ERHIAccess::SRVMask),
					FRHITransitionInfo(DestTarget.ShaderResourceTexture, ERHIAccess::CopyDest, ERHIAccess::SRVMask)
				};
				RHICmdList.Transition(MakeArrayView(TransitionsAfter, UE_ARRAY_COUNT(TransitionsAfter)));
			}
		}

		{
			SCOPED_DRAW_EVENT(RHICmdList, FilterCubeMap);
			// Filter all the mips.
			// in simple mobile bilin case each one reads from whichever scratch render target holds the downsampled contents, and writes to the destination cubemap.
			// HQ case, all mips are contained in GetEffectiveSourceTexture(.., false,0) as it needs a complete mip chain.
			for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
			{
				SCOPED_DRAW_EVENT(RHICmdList, FilterCubeMip);
				FSceneRenderTargetItem& EffectiveRT = GetEffectiveRenderTarget(SceneContext, false, bUseHQFiltering ? 0 : MipIndex);
				FSceneRenderTargetItem& EffectiveSource = GetEffectiveSourceTexture(SceneContext, false, bUseHQFiltering ? 0 : MipIndex);
				check(EffectiveRT.TargetableTexture != EffectiveSource.ShaderResourceTexture);
				const int32 MipSize = 1 << (NumMips - MipIndex - 1);

				for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
				{
					FRHIRenderPassInfo RPInfo(EffectiveRT.TargetableTexture, ERenderTargetActions::Load_Store);
					RPInfo.ColorRenderTargets[0].ArraySlice = CubeFace;
					RPInfo.ColorRenderTargets[0].MipIndex = MipIndex;
					TransitionRenderPassTargets(RHICmdList, RPInfo);

					RHICmdList.BeginRenderPass(RPInfo, TEXT("FilterCubeMip"));
					{
						FGraphicsPipelineStateInitializer GraphicsPSOInit;
						RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
						GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
						GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
						GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();

						const FIntRect ViewRect(0, 0, MipSize, MipSize);
						RHICmdList.SetViewport(0, 0, 0.0f, MipSize, MipSize, 1.0f);

						TShaderMapRef<FScreenVS> VertexShader(GetGlobalShaderMap(FeatureLevel));

						TShaderMapRef<TCubeFilterPS<0>> HQFilterPixelShader(ShaderMap);
						TShaderMapRef<FMobileDownsamplePS> BilinFilterPixelShader(ShaderMap);
						FRHIPixelShader* PixelShaderRHI = bUseHQFiltering ? HQFilterPixelShader.GetPixelShader() : BilinFilterPixelShader.GetPixelShader();

						GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
						GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
						GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShaderRHI;
						GraphicsPSOInit.PrimitiveType = PT_TriangleList;

						SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

						if (bUseHQFiltering)
						{
							SetShaderValue(RHICmdList, PixelShaderRHI, HQFilterPixelShader->CubeFace, CubeFace);
							SetShaderValue(RHICmdList, PixelShaderRHI, HQFilterPixelShader->MipIndex, MipIndex);
							SetShaderValue(RHICmdList, PixelShaderRHI, HQFilterPixelShader->NumMips, NumMips);
							SetTextureParameter(
								RHICmdList,
								PixelShaderRHI,
								HQFilterPixelShader->SourceCubemapTexture,
								HQFilterPixelShader->SourceCubemapSampler,
								TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),
								EffectiveSource.ShaderResourceTexture);
						}
						else
						{
							BilinFilterPixelShader->SetParameters(RHICmdList, CubeFace, MipIndex, EffectiveSource);
						}

						DrawRectangle(
							RHICmdList,
							ViewRect.Min.X, ViewRect.Min.Y,
							ViewRect.Width(), ViewRect.Height(),
							ViewRect.Min.X, ViewRect.Min.Y,
							ViewRect.Width(), ViewRect.Height(),
							FIntPoint(ViewRect.Width(), ViewRect.Height()),
							FIntPoint(MipSize, MipSize),
							VertexShader);
					}
					RHICmdList.EndRenderPass();

					FResolveParams ResolveParams(FResolveRect(), (ECubeFace)CubeFace, MipIndex);
					ResolveParams.SourceAccessFinal = ERHIAccess::SRVMask;
					ResolveParams.DestAccessFinal = ERHIAccess::SRVMask;
					RHICmdList.CopyToResolveTarget(EffectiveRT.TargetableTexture, EffectiveRT.ShaderResourceTexture, ResolveParams);
				}
			}
		}
	}
}
