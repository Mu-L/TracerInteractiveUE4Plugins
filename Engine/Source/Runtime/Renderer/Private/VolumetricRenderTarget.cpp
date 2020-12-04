// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VolumetricRenderTarget.cpp
=============================================================================*/

#include "VolumetricRenderTarget.h"
#include "DeferredShadingRenderer.h"
#include "RenderCore/Public/RenderGraphUtils.h"
#include "RenderCore/Public/PixelShaderUtils.h"
#include "ScenePrivate.h"
#include "SceneTextureParameters.h"
#include "SingleLayerWaterRendering.h"

//PRAGMA_DISABLE_OPTIMIZATION

static TAutoConsoleVariable<int32> CVarVolumetricRenderTarget(
	TEXT("r.VolumetricRenderTarget"), 1,
	TEXT(""),
	ECVF_SetByScalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarVolumetricRenderTargetUvNoiseScale(
	TEXT("r.VolumetricRenderTarget.UvNoiseScale"), 0.5f,
	TEXT("Used when r.VolumetricRenderTarget.UpsamplingMode is in a mode using jitter - this value scales the amount of jitter."),
	ECVF_SetByScalability);

static TAutoConsoleVariable<int32> CVarVolumetricRenderTargetMode(
	TEXT("r.VolumetricRenderTarget.Mode"), 0,
	TEXT("[0] trace quarter resolution + reconstruct at half resolution + upsample [1] trace half res + reconstruct full res + upsample [2] trace at quarter resolution + reconstruct full resolution (cannot intersect with opaque meshes and forces UpsamplingMode=2)"),
	ECVF_SetByScalability);

static TAutoConsoleVariable<int32> CVarVolumetricRenderTargetUpsamplingMode(
	TEXT("r.VolumetricRenderTarget.UpsamplingMode"), 4,
	TEXT("Used in compositing volumetric RT over the scene. [0] bilinear [1] bilinear + jitter [2] nearest + depth test [3] bilinear + jitter + keep closest [4] bilaterial upsampling"),
	ECVF_SetByScalability);


static bool ShouldPipelineCompileVolumetricRenderTargetShaders(EShaderPlatform ShaderPlatform)
{
	return GetMaxSupportedFeatureLevel(ShaderPlatform) >= ERHIFeatureLevel::SM5;
}

bool ShouldViewRenderVolumetricCloudRenderTarget(const FViewInfo& ViewInfo)
{
	return CVarVolumetricRenderTarget.GetValueOnRenderThread() && ShouldPipelineCompileVolumetricRenderTargetShaders(ViewInfo.GetShaderPlatform())
		&& (ViewInfo.ViewState != nullptr) && !(ViewInfo.bIsReflectionCapture || ViewInfo.bIsSceneCapture);
}

bool IsVolumetricRenderTargetEnabled()
{
	return CVarVolumetricRenderTarget.GetValueOnRenderThread() > 0;
}

static bool ShouldViewComposeVolumetricRenderTarget(const FViewInfo& ViewInfo)
{
	return ShouldViewRenderVolumetricCloudRenderTarget(ViewInfo);
}

static uint32 GetMainDownsampleFactor(int32 Mode)
{
	switch (Mode)
	{
	case 0:
		return 2; // Reconstruct at half resolution of view
		break;
	case 1:
	case 2:
		return 1; // Reconstruct at full resolution of view
		break;
	}
	check(false); // unhandled mode
	return 2;
}

static uint32 GetTraceDownsampleFactor(int32 Mode)
{
	switch (Mode)
	{
	case 0:
		return 2; // Trace at half resolution of the view
		break;
	case 1:
		return 2; // Trace at quarter resolution of view (see GetMainDownsampleFactor)
		break;
	case 2:
		return 4; // Trace at quarter resolution of view (see GetMainDownsampleFactor)
		break;
	}
	check(false); // unhandled mode
	return 2;
}

static void GetTextureSafeUvCoordBound(FRDGTextureRef Texture, FUintVector4& TextureValidCoordRect, FVector4& TextureValidUvRect)
{
	FIntVector TexSize = Texture->Desc.GetSize();
	TextureValidCoordRect.X = 0;
	TextureValidCoordRect.Y = 0;
	TextureValidCoordRect.Z = TexSize.X - 1;
	TextureValidCoordRect.W = TexSize.Y - 1;
	TextureValidUvRect.X = 0.51f / float(TexSize.X);
	TextureValidUvRect.Y = 0.51f / float(TexSize.Y);
	TextureValidUvRect.Z = (float(TexSize.X) - 0.51f) / float(TexSize.X);
	TextureValidUvRect.W = (float(TexSize.Y) - 0.51f) / float(TexSize.Y);
};

static bool AnyViewRequiresProcessing(TArray<FViewInfo>& Views)
{
	bool bAnyViewRequiresProcessing = false;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& ViewInfo = Views[ViewIndex];
		bAnyViewRequiresProcessing |= ShouldViewComposeVolumetricRenderTarget(ViewInfo);
	}
	return bAnyViewRequiresProcessing;
}


/*=============================================================================
	UVolumetricCloudComponent implementation.
=============================================================================*/

FVolumetricRenderTargetViewStateData::FVolumetricRenderTargetViewStateData()
	: CurrentRT(1)
	, bFirstTimeUsed(true)
	, bHistoryValid(false)
	, FullResolution(FIntPoint::ZeroValue)
	, VolumetricReconstructRTResolution(FIntPoint::ZeroValue)
	, VolumetricTracingRTResolution(FIntPoint::ZeroValue)
{
	VolumetricReconstructRTDownsampleFactor = 0;
	VolumetricTracingRTDownsampleFactor = 0;
}

FVolumetricRenderTargetViewStateData::~FVolumetricRenderTargetViewStateData()
{
}

void FVolumetricRenderTargetViewStateData::Initialise(
	FIntPoint& ViewRectResolutionIn,
	float InUvNoiseScale,
	int32 InMode,
	int32 InUpsamplingMode)
{
	// Update internal settings
	UvNoiseScale = InUvNoiseScale;
	Mode = FMath::Clamp(InMode, 0, 2);
	UpsamplingMode = Mode == 2 ? 2 : FMath::Clamp(InUpsamplingMode, 0, 4); // if we are using mode 2 then we cannot intersect with depth and upsampling should be 2 (simple on/off intersection)

	if (bFirstTimeUsed)
	{
		bFirstTimeUsed = false;
		bHistoryValid = false;
		FrameId = 0;
		NoiseFrameIndex = 0;
		NoiseFrameIndexModPattern = 0;
		CurrentPixelOffset = FIntPoint::ZeroValue;
	}

	{
		CurrentRT = 1 - CurrentRT;
		const uint32 PreviousRT = 1 - CurrentRT;

		// We always reallocate on a resolution change to adapt to dynamic resolution scaling.
		// TODO allocate once at max resolution and change source and destination coord/uvs/rect.
		if (FullResolution != ViewRectResolutionIn || GetMainDownsampleFactor(Mode) != VolumetricReconstructRTDownsampleFactor || GetTraceDownsampleFactor(Mode) != VolumetricTracingRTDownsampleFactor)
		{
			VolumetricReconstructRTDownsampleFactor = GetMainDownsampleFactor(Mode);
			VolumetricTracingRTDownsampleFactor = GetTraceDownsampleFactor(Mode);

			FullResolution = ViewRectResolutionIn;
			VolumetricReconstructRTResolution = FIntPoint::DivideAndRoundUp(FullResolution, VolumetricReconstructRTDownsampleFactor);							// Half resolution
			VolumetricTracingRTResolution = FIntPoint::DivideAndRoundUp(VolumetricReconstructRTResolution, VolumetricTracingRTDownsampleFactor);	// Half resolution of the volumetric buffer

			// Need a new size so release the low resolution trace buffer
			VolumetricTracingRT.SafeRelease();
			VolumetricTracingRTDepth.SafeRelease();
		}

		FIntVector CurrentTargetResVec = VolumetricReconstructRT[CurrentRT].IsValid() ? VolumetricReconstructRT[CurrentRT]->GetDesc().GetSize() : FIntVector::ZeroValue;
		FIntPoint CurrentTargetRes = FIntPoint::DivideAndRoundUp(FullResolution, VolumetricReconstructRTDownsampleFactor);
		if (VolumetricReconstructRT[CurrentRT].IsValid() && FIntPoint(CurrentTargetResVec.X, CurrentTargetResVec.Y) != CurrentTargetRes)
		{
			// Resolution does not match so release target we are going to render in
			VolumetricReconstructRT[CurrentRT].SafeRelease();
			VolumetricReconstructRTDepth[CurrentRT].SafeRelease();
		}

		// Regular every frame update
		{
			// Do not mark history as valid if the half resolution buffer is not valid. That means nothing has been rendered last frame.
			// That can happen when cloud is used to render into that buffer
			bHistoryValid = VolumetricReconstructRT[PreviousRT].IsValid();

			NoiseFrameIndex += FrameId == 0 ? 1 : 0;
			NoiseFrameIndexModPattern = NoiseFrameIndex % (VolumetricTracingRTDownsampleFactor * VolumetricTracingRTDownsampleFactor);

			FrameId++;
			FrameId = FrameId % (VolumetricTracingRTDownsampleFactor * VolumetricTracingRTDownsampleFactor);

			if (VolumetricTracingRTDownsampleFactor == 2)
			{
				static int32 OrderDithering2x2[4] = { 0, 2, 3, 1 };
				int32 LocalFrameId = OrderDithering2x2[FrameId];
				CurrentPixelOffset = FIntPoint(LocalFrameId % VolumetricTracingRTDownsampleFactor, LocalFrameId / VolumetricTracingRTDownsampleFactor);
			}
			else if (VolumetricTracingRTDownsampleFactor == 4)
			{
				static int32 OrderDithering4x4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
				int32 LocalFrameId = OrderDithering4x4[FrameId];
				CurrentPixelOffset = FIntPoint(LocalFrameId % VolumetricTracingRTDownsampleFactor, LocalFrameId / VolumetricTracingRTDownsampleFactor);
			}
			else
			{
				// Default linear parse
				CurrentPixelOffset = FIntPoint(FrameId % VolumetricTracingRTDownsampleFactor, FrameId / VolumetricTracingRTDownsampleFactor);
			}
		}
	}
}

FRDGTextureRef FVolumetricRenderTargetViewStateData::GetOrCreateVolumetricTracingRT(FRDGBuilder& GraphBuilder)
{
	check(FullResolution != FIntPoint::ZeroValue); // check that initialization has been done at least once

	if (!VolumetricTracingRT.IsValid())
	{
		FPooledRenderTargetDesc Desc = FPooledRenderTargetDesc::Create2DDesc(
			VolumetricTracingRTResolution, PF_FloatRGBA, FClearValueBinding(FLinearColor(0.0f, 0.0f, 0.0f, 1.0f)),
			TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable, false);
		GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, Desc, VolumetricTracingRT, TEXT("RDGVolumetricTracingRT"), ERenderTargetTransience::NonTransient);
	}

	return GraphBuilder.RegisterExternalTexture(VolumetricTracingRT);
}

FRDGTextureRef FVolumetricRenderTargetViewStateData::GetOrCreateVolumetricTracingRTDepth(FRDGBuilder& GraphBuilder)
{
	check(FullResolution != FIntPoint::ZeroValue); // check that initialization has been done at least once

	if (!VolumetricTracingRTDepth.IsValid())
	{
		FPooledRenderTargetDesc Desc = FPooledRenderTargetDesc::Create2DDesc(
			VolumetricTracingRTResolution, PF_G16R16F, FClearValueBinding(FLinearColor(63000.0f, 63000.0f, 63000.0f, 63000.0f)),
			TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable, false);
		GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, Desc, VolumetricTracingRTDepth, TEXT("RDGVolumetricTracingRTDepth"), ERenderTargetTransience::NonTransient);
	}

	return GraphBuilder.RegisterExternalTexture(VolumetricTracingRTDepth);
}


FRDGTextureRef FVolumetricRenderTargetViewStateData::GetOrCreateDstVolumetricReconstructRT(FRDGBuilder& GraphBuilder)
{
	check(VolumetricReconstructRTResolution != FIntPoint::ZeroValue); // check that initialization has been done at least once

	if (!VolumetricReconstructRT[CurrentRT].IsValid())
	{
		FPooledRenderTargetDesc Desc = FPooledRenderTargetDesc::Create2DDesc(
			VolumetricReconstructRTResolution, PF_FloatRGBA, FClearValueBinding(FLinearColor(0.0f, 0.0f, 0.0f, 1.0f)),
			TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable, false);
		GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, Desc, VolumetricReconstructRT[CurrentRT], TEXT("RDGVolumetricVolumetricReconstructRTRT"), ERenderTargetTransience::NonTransient);
	}

	return GraphBuilder.RegisterExternalTexture(VolumetricReconstructRT[CurrentRT]);
}


FRDGTextureRef FVolumetricRenderTargetViewStateData::GetOrCreateDstVolumetricReconstructRTDepth(FRDGBuilder& GraphBuilder)
{
	check(VolumetricReconstructRTResolution != FIntPoint::ZeroValue); // check that initialization has been done at least once

	if (!VolumetricReconstructRTDepth[CurrentRT].IsValid())
	{
		FPooledRenderTargetDesc Desc = FPooledRenderTargetDesc::Create2DDesc(
			VolumetricReconstructRTResolution, PF_G16R16F, FClearValueBinding(FLinearColor(63000.0f, 63000.0f, 63000.0f, 63000.0f)),
			TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable, false);
		GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, Desc, VolumetricReconstructRTDepth[CurrentRT], TEXT("RDGVolumetricVolumetricReconstructRTRTDepth"), ERenderTargetTransience::NonTransient);
	}

	return GraphBuilder.RegisterExternalTexture(VolumetricReconstructRTDepth[CurrentRT]);
}

TRefCountPtr<IPooledRenderTarget> FVolumetricRenderTargetViewStateData::GetDstVolumetricReconstructRT()
{
	return VolumetricReconstructRT[CurrentRT];
}
TRefCountPtr<IPooledRenderTarget> FVolumetricRenderTargetViewStateData::GetDstVolumetricReconstructRTDepth()
{
	return VolumetricReconstructRTDepth[CurrentRT];
}

FRDGTextureRef FVolumetricRenderTargetViewStateData::GetOrCreateSrcVolumetricReconstructRT(FRDGBuilder& GraphBuilder)
{
	check(VolumetricReconstructRTResolution != FIntPoint::ZeroValue); // check that initialization has been done at least once

	check(VolumetricReconstructRT[1u - CurrentRT].IsValid());
	return GraphBuilder.RegisterExternalTexture(VolumetricReconstructRT[1u - CurrentRT]);
}

FRDGTextureRef FVolumetricRenderTargetViewStateData::GetOrCreateSrcVolumetricReconstructRTDepth(FRDGBuilder& GraphBuilder)
{
	check(VolumetricReconstructRTResolution != FIntPoint::ZeroValue); // check that initialization has been done at least once

	check(VolumetricReconstructRT[1u - CurrentRT].IsValid());
	return GraphBuilder.RegisterExternalTexture(VolumetricReconstructRTDepth[1u - CurrentRT]);
}

FUintVector4 FVolumetricRenderTargetViewStateData::GetTracingCoordToZbufferCoordScaleBias() const
{
	if (Mode == 2 || Mode == 1)
	{
		// In this case, the source depth buffer full resolution depth buffer is the full resolution scene one
		const uint32 CombinedDownsampleFactor = VolumetricReconstructRTDownsampleFactor * VolumetricTracingRTDownsampleFactor;
		return FUintVector4(CombinedDownsampleFactor, CombinedDownsampleFactor,																// Scale is the combined downsample factor
			CurrentPixelOffset.X * VolumetricReconstructRTDownsampleFactor, CurrentPixelOffset.Y * VolumetricReconstructRTDownsampleFactor);// Each sample will then sample from full res according to reconstructed RT offset times its downsample factor
	}

	// Otherwise, a half resolution depth buffer is used
	const uint32 SourceDepthBufferRTDownsampleFactor = 2;
	const uint32 CombinedDownsampleFactor = VolumetricReconstructRTDownsampleFactor * VolumetricTracingRTDownsampleFactor / SourceDepthBufferRTDownsampleFactor;
	return FUintVector4( CombinedDownsampleFactor, CombinedDownsampleFactor,									// Scale is the combined downsample factor
		CurrentPixelOffset.X * VolumetricReconstructRTDownsampleFactor / VolumetricReconstructRTDownsampleFactor,	// Each sample will then sample from full res according to reconstructed RT offset times its downsample factor
		CurrentPixelOffset.Y * VolumetricReconstructRTDownsampleFactor / VolumetricReconstructRTDownsampleFactor);
}


/*=============================================================================
	FSceneRenderer implementation.
=============================================================================*/

void FSceneRenderer::InitVolumetricRenderTargetForViews(FRDGBuilder& GraphBuilder)
{
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& ViewInfo = Views[ViewIndex];
		if (!ShouldViewRenderVolumetricCloudRenderTarget(ViewInfo))
		{
			continue;
		}
		FVolumetricRenderTargetViewStateData& VolumetricCloudRT = ViewInfo.ViewState->VolumetricCloudRenderTarget;


		//FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		FIntPoint ViewRect = ViewInfo.ViewRect.Size();
		VolumetricCloudRT.Initialise(	// TODO this is going to reallocate a buffer each time dynamic resolution scaling is applied 
			ViewRect,
			CVarVolumetricRenderTargetUvNoiseScale.GetValueOnAnyThread(), 
			CVarVolumetricRenderTargetMode.GetValueOnRenderThread(),
			CVarVolumetricRenderTargetUpsamplingMode.GetValueOnAnyThread());

		FViewUniformShaderParameters ViewVolumetricCloudRTParameters = *ViewInfo.CachedViewUniformShaderParameters;
		{
			const FIntPoint& VolumetricReconstructResolution = VolumetricCloudRT.GetCurrentVolumetricReconstructRTResolution();
			const FIntPoint& VolumetricTracingResolution = VolumetricCloudRT.GetCurrentVolumetricTracingRTResolution();
			const FIntPoint& CurrentPixelOffset = VolumetricCloudRT.GetCurrentTracingPixelOffset();
			const uint32 VolumetricReconstructRTDownSample = VolumetricCloudRT.GetVolumetricReconstructRTDownsampleFactor();
			const uint32 VolumetricTracingRTDownSample = VolumetricCloudRT.GetVolumetricTracingRTDownsampleFactor();

			// We jitter and reconstruct the volumetric view before TAA so we do not want any of its jitter.
			// We do use TAA remove bilinear artifact at up sampling time.
			FViewMatrices ViewMatrices = ViewInfo.ViewMatrices;
			ViewMatrices.HackRemoveTemporalAAProjectionJitter();

			float DownSampleFactor = float(VolumetricReconstructRTDownSample * VolumetricTracingRTDownSample);

			// Offset to the correct half resolution pixel
			FVector2D CenterCoord = FVector2D(VolumetricReconstructRTDownSample / 2.0f);
			FVector2D TargetCoord = FVector2D(CurrentPixelOffset) + FVector2D(0.5f, 0.5f);
			FVector2D OffsetCoord = (TargetCoord - CenterCoord) * (FVector2D(-2.0f, 2.0f) / FVector2D(VolumetricReconstructResolution));
			ViewMatrices.HackAddTemporalAAProjectionJitter(OffsetCoord);

			ViewInfo.SetupViewRectUniformBufferParameters(
				ViewVolumetricCloudRTParameters,
				VolumetricTracingResolution,
				FIntRect(0, 0, VolumetricTracingResolution.X, VolumetricTracingResolution.Y),
				ViewMatrices,
				ViewInfo.PrevViewInfo.ViewMatrices // This could also be changed if needed
			);
		}
		ViewInfo.VolumetricRenderTargetViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(ViewVolumetricCloudRTParameters, UniformBuffer_SingleFrame);
	}
}

//////////////////////////////////////////////////////////////////////////

class FReconstructVolumetricRenderTargetPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FReconstructVolumetricRenderTargetPS);
	SHADER_USE_PARAMETER_STRUCT(FReconstructVolumetricRenderTargetPS, FGlobalShader);

	class FHistoryAvailable : SHADER_PERMUTATION_BOOL("PERMUTATION_HISTORY_AVAILABLE");
	using FPermutationDomain = TShaderPermutationDomain<FHistoryAvailable>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TracingVolumetricTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TracingVolumetricDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PreviousFrameVolumetricTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PreviousFrameVolumetricDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HalfResDepthTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearTextureSampler)
		RENDER_TARGET_BINDING_SLOTS()
		SHADER_PARAMETER(FVector4, DstVolumetricTextureSizeAndInvSize)
		SHADER_PARAMETER(FVector4, PreviousVolumetricTextureSizeAndInvSize)
		SHADER_PARAMETER(FIntPoint, CurrentTracingPixelOffset)
		SHADER_PARAMETER(int32, DownSampleFactor)
		SHADER_PARAMETER(int32, VolumetricRenderTargetMode)
		SHADER_PARAMETER(FUintVector4, TracingVolumetricTextureValidCoordRect)
		SHADER_PARAMETER(FVector4, TracingVolumetricTextureValidUvRect)
		SHADER_PARAMETER(FUintVector4, PreviousFrameVolumetricTextureValidCoordRect)
		SHADER_PARAMETER(FVector4, PreviousFrameVolumetricTextureValidUvRect)
		SHADER_PARAMETER(float, TemporalFactor)
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldPipelineCompileVolumetricRenderTargetShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_RECONSTRUCT_VOLUMETRICRT"), TEXT("1"));
	}
};

IMPLEMENT_GLOBAL_SHADER(FReconstructVolumetricRenderTargetPS, "/Engine/Private/VolumetricRenderTarget.usf", "ReconstructVolumetricRenderTargetPS", SF_Pixel);

//////////////////////////////////////////////////////////////////////////

void FSceneRenderer::ReconstructVolumetricRenderTarget(FRDGBuilder& GraphBuilder)
{
	if (!AnyViewRequiresProcessing(Views))
	{
		return;
	}

	FRDGTextureRef BlackDummy = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);
	TRefCountPtr<IPooledRenderTarget> SceneDepthZ = SceneContext.SceneDepthZ;

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& ViewInfo = Views[ViewIndex];
		if (!ShouldViewComposeVolumetricRenderTarget(ViewInfo))
		{
			continue;
		}
		FVolumetricRenderTargetViewStateData& VolumetricCloudRT = ViewInfo.ViewState->VolumetricCloudRenderTarget;

		FRDGTextureRef DstVolumetric = VolumetricCloudRT.GetOrCreateDstVolumetricReconstructRT(GraphBuilder);
		FRDGTextureRef DstVolumetricDepth = VolumetricCloudRT.GetOrCreateDstVolumetricReconstructRTDepth(GraphBuilder);
		FRDGTextureRef SrcTracingVolumetric = VolumetricCloudRT.GetOrCreateVolumetricTracingRT(GraphBuilder);
		FRDGTextureRef SrcTracingVolumetricDepth = VolumetricCloudRT.GetOrCreateVolumetricTracingRTDepth(GraphBuilder);
		FRDGTextureRef PreviousFrameVolumetricTexture = VolumetricCloudRT.GetHistoryValid() ? VolumetricCloudRT.GetOrCreateSrcVolumetricReconstructRT(GraphBuilder) : BlackDummy;
		FRDGTextureRef PreviousFrameVolumetricDepthTexture = VolumetricCloudRT.GetHistoryValid() ? VolumetricCloudRT.GetOrCreateSrcVolumetricReconstructRTDepth(GraphBuilder) : BlackDummy;

		const uint32 TracingVolumetricCloudRTDownSample = VolumetricCloudRT.GetVolumetricTracingRTDownsampleFactor();

		FReconstructVolumetricRenderTargetPS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FReconstructVolumetricRenderTargetPS::FHistoryAvailable>(VolumetricCloudRT.GetHistoryValid());
		TShaderMapRef<FReconstructVolumetricRenderTargetPS> PixelShader(ViewInfo.ShaderMap, PermutationVector);

		FReconstructVolumetricRenderTargetPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FReconstructVolumetricRenderTargetPS::FParameters>();
		PassParameters->ViewUniformBuffer = ViewInfo.VolumetricRenderTargetViewUniformBuffer;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(DstVolumetric, ERenderTargetLoadAction::ENoAction);
		PassParameters->RenderTargets[1] = FRenderTargetBinding(DstVolumetricDepth, ERenderTargetLoadAction::ENoAction);
		PassParameters->TracingVolumetricTexture = SrcTracingVolumetric;
		PassParameters->TracingVolumetricDepthTexture = SrcTracingVolumetricDepth;
		PassParameters->PreviousFrameVolumetricTexture = PreviousFrameVolumetricTexture;
		PassParameters->PreviousFrameVolumetricDepthTexture = PreviousFrameVolumetricDepthTexture;
		PassParameters->LinearTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
		PassParameters->CurrentTracingPixelOffset = VolumetricCloudRT.GetCurrentTracingPixelOffset();
		PassParameters->DownSampleFactor = TracingVolumetricCloudRTDownSample;
		PassParameters->VolumetricRenderTargetMode = VolumetricCloudRT.GetMode();
		PassParameters->HalfResDepthTexture = VolumetricCloudRT.GetMode() == 0 ? GraphBuilder.RegisterExternalTexture(ViewInfo.HalfResDepthSurfaceCheckerboardMinMax) : GraphBuilder.RegisterExternalTexture(SceneDepthZ);

		GetTextureSafeUvCoordBound(SrcTracingVolumetric, PassParameters->TracingVolumetricTextureValidCoordRect, PassParameters->TracingVolumetricTextureValidUvRect);
		GetTextureSafeUvCoordBound(PreviousFrameVolumetricTexture, PassParameters->PreviousFrameVolumetricTextureValidCoordRect, PassParameters->PreviousFrameVolumetricTextureValidUvRect);

		FIntVector DstVolumetricSize = DstVolumetric->Desc.GetSize();
		FVector2D DstVolumetricTextureSize = FVector2D(float(DstVolumetricSize.X), float(DstVolumetricSize.Y));
		FVector2D PreviousVolumetricTextureSize = FVector2D(float(PreviousFrameVolumetricTexture->Desc.GetSize().X), float(PreviousFrameVolumetricTexture->Desc.GetSize().Y));
		PassParameters->DstVolumetricTextureSizeAndInvSize = FVector4(DstVolumetricTextureSize.X, DstVolumetricTextureSize.Y, 1.0f / DstVolumetricTextureSize.X, 1.0f / DstVolumetricTextureSize.Y);
		PassParameters->PreviousVolumetricTextureSizeAndInvSize = FVector4(PreviousVolumetricTextureSize.X, PreviousVolumetricTextureSize.Y, 1.0f / PreviousVolumetricTextureSize.X, 1.0f / PreviousVolumetricTextureSize.Y);

		FPixelShaderUtils::AddFullscreenPass<FReconstructVolumetricRenderTargetPS>(
			GraphBuilder, ViewInfo.ShaderMap, RDG_EVENT_NAME("VolumetricReconstruct"), PixelShader, PassParameters, 
			FIntRect(0, 0, DstVolumetricSize.X, DstVolumetricSize.Y));
	}

}

//////////////////////////////////////////////////////////////////////////

class FComposeVolumetricRTOverScenePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FComposeVolumetricRTOverScenePS);
	SHADER_USE_PARAMETER_STRUCT(FComposeVolumetricRTOverScenePS, FGlobalShader);

	class FUpsamplingMode : SHADER_PERMUTATION_RANGE_INT("PERMUTATION_UPSAMPLINGMODE", 0, 5);
	class FRenderUnderWaterBuffer : SHADER_PERMUTATION_BOOL("PERMUTATION_RENDER_UNDERWATER_BUFFER");	// Render into the water scene color buffer (used when rendering from water system)
	class FRenderCameraUnderWater : SHADER_PERMUTATION_BOOL("PERMUTATION_RENDER_CAMERA_UNDERWATER");	// When water us used and the camera is under water, use that permutation (to handle camera intersection with water and double cloud composition)
	using FPermutationDomain = TShaderPermutationDomain<FUpsamplingMode, FRenderUnderWaterBuffer, FRenderCameraUnderWater>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VolumetricTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VolumetricDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, WaterLinearDepthTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearTextureSampler)
		RENDER_TARGET_BINDING_SLOTS()
		SHADER_PARAMETER(float, UvOffsetScale)
		SHADER_PARAMETER(FVector4, VolumetricTextureSizeAndInvSize)
		SHADER_PARAMETER(FVector2D, FullResolutionToVolumetricBufferResolutionScale)
		SHADER_PARAMETER(FVector2D, FullResolutionToWaterBufferScale)
		SHADER_PARAMETER(FVector4, SceneWithoutSingleLayerWaterViewRect)
		SHADER_PARAMETER(FUintVector4, VolumetricTextureValidCoordRect)
		SHADER_PARAMETER(FVector4, VolumetricTextureValidUvRect)
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldPipelineCompileVolumetricRenderTargetShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_COMPOSE_VOLUMETRICRT"), TEXT("1"));
	}
};

IMPLEMENT_GLOBAL_SHADER(FComposeVolumetricRTOverScenePS, "/Engine/Private/VolumetricRenderTarget.usf", "ComposeVolumetricRTOverScenePS", SF_Pixel);

//////////////////////////////////////////////////////////////////////////

void FSceneRenderer::ComposeVolumetricRenderTargetOverScene(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneColorTexture, FRDGTextureRef SceneDepthResolveTexture, bool bShouldRenderSingleLayerWater, const FSceneWithoutWaterTextures& WaterPassData)
{
	if (!AnyViewRequiresProcessing(Views))
	{
		return;
	}

	FRHIBlendState* PreMultipliedColorTransmittanceBlend = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();

	FRDGTextureRef SceneWaterDepthTexture = GSystemTextures.GetDepthDummy(GraphBuilder);
	if (bShouldRenderSingleLayerWater)
	{
		SceneWaterDepthTexture = WaterPassData.DepthTexture;
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& ViewInfo = Views[ViewIndex];
		if (!ShouldViewRenderVolumetricCloudRenderTarget(ViewInfo))
		{
			continue;
		}
		FVolumetricRenderTargetViewStateData& VolumetricCloudRT = ViewInfo.ViewState->VolumetricCloudRenderTarget;
		FRDGTextureRef VolumetricTexture = VolumetricCloudRT.GetOrCreateDstVolumetricReconstructRT(GraphBuilder);
		FRDGTextureRef VolumetricDepthTexture = VolumetricCloudRT.GetOrCreateDstVolumetricReconstructRTDepth(GraphBuilder);

		// When reconstructed and back buffer resolution matches, force using a pixel perfect upsampling.
		const uint32 VRTMode = VolumetricCloudRT.GetMode();
		int UpsamplingMode = VolumetricCloudRT.GetUpsamplingMode();
		UpsamplingMode = UpsamplingMode == 3 && (VRTMode == 1 || VRTMode == 2) ? 2 : UpsamplingMode;

		FComposeVolumetricRTOverScenePS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FComposeVolumetricRTOverScenePS::FUpsamplingMode>(UpsamplingMode);
		PermutationVector.Set<FComposeVolumetricRTOverScenePS::FRenderUnderWaterBuffer>(0);
		PermutationVector.Set<FComposeVolumetricRTOverScenePS::FRenderCameraUnderWater>((bShouldRenderSingleLayerWater && ViewInfo.IsUnderwater()) ? 1 : 0);
		TShaderMapRef<FComposeVolumetricRTOverScenePS> PixelShader(ViewInfo.ShaderMap, PermutationVector);

		FComposeVolumetricRTOverScenePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FComposeVolumetricRTOverScenePS::FParameters>();
		PassParameters->ViewUniformBuffer = ViewInfo.ViewUniformBuffer;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColorTexture, ERenderTargetLoadAction::ELoad);
		PassParameters->VolumetricTexture = VolumetricTexture;
		PassParameters->VolumetricDepthTexture = VolumetricDepthTexture;
		PassParameters->SceneDepthTexture = SceneDepthResolveTexture;
		PassParameters->LinearTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
		PassParameters->UvOffsetScale = VolumetricCloudRT.GetUvNoiseScale();
		PassParameters->FullResolutionToVolumetricBufferResolutionScale = FVector2D(1.0f / float(GetMainDownsampleFactor(VRTMode)), float(GetMainDownsampleFactor(VRTMode)));
		GetTextureSafeUvCoordBound(PassParameters->VolumetricTexture, PassParameters->VolumetricTextureValidCoordRect, PassParameters->VolumetricTextureValidUvRect);

		PassParameters->WaterLinearDepthTexture = SceneWaterDepthTexture;
		if (bShouldRenderSingleLayerWater)
		{
			const FSceneWithoutWaterTextures::FView& WaterPassViewData = WaterPassData.Views[ViewIndex];
			PassParameters->FullResolutionToWaterBufferScale = FVector2D(1.0f / WaterPassData.RefractionDownsampleFactor, WaterPassData.RefractionDownsampleFactor);
			PassParameters->SceneWithoutSingleLayerWaterViewRect = FVector4(WaterPassViewData.ViewRect.Min.X, WaterPassViewData.ViewRect.Min.Y,
				WaterPassViewData.ViewRect.Max.X, WaterPassViewData.ViewRect.Max.Y);
		}

		FVector2D VolumetricTextureSize = FVector2D(float(VolumetricTexture->Desc.GetSize().X), float(VolumetricTexture->Desc.GetSize().Y));
		PassParameters->VolumetricTextureSizeAndInvSize = FVector4(VolumetricTextureSize.X, VolumetricTextureSize.Y, 1.0f / VolumetricTextureSize.X, 1.0f / VolumetricTextureSize.Y);

		FPixelShaderUtils::AddFullscreenPass<FComposeVolumetricRTOverScenePS>(
			GraphBuilder, ViewInfo.ShaderMap, RDG_EVENT_NAME("VolumetricComposeOverScene"), PixelShader, PassParameters, ViewInfo.ViewRect,
			PreMultipliedColorTransmittanceBlend);
	}
}

//////////////////////////////////////////////////////////////////////////

void FSceneRenderer::ComposeVolumetricRenderTargetOverSceneUnderWater(FRDGBuilder& GraphBuilder, const FSceneWithoutWaterTextures& WaterPassData)
{
	if (!AnyViewRequiresProcessing(Views))
	{
		return;
	}

	FRHIBlendState* PreMultipliedColorTransmittanceBlend = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& ViewInfo = Views[ViewIndex];
		if (!ShouldViewRenderVolumetricCloudRenderTarget(ViewInfo))
		{
			continue;
		}

		FVolumetricRenderTargetViewStateData& VolumetricCloudRT = ViewInfo.ViewState->VolumetricCloudRenderTarget;
		FRDGTextureRef VolumetricTexture = VolumetricCloudRT.GetOrCreateDstVolumetricReconstructRT(GraphBuilder);
		FRDGTextureRef VolumetricDepthTexture = VolumetricCloudRT.GetOrCreateDstVolumetricReconstructRTDepth(GraphBuilder);
		const FSceneWithoutWaterTextures::FView& WaterPassViewData = WaterPassData.Views[ViewIndex];

		// When reconstructed and back buffer resolution matches, force using a pixel perfect upsampling.
		const uint32 VRTMode = VolumetricCloudRT.GetMode();
		int UpsamplingMode = VolumetricCloudRT.GetUpsamplingMode();
		UpsamplingMode = UpsamplingMode == 3 && (VRTMode == 1 || VRTMode == 2) ? 2 : UpsamplingMode;

		FComposeVolumetricRTOverScenePS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FComposeVolumetricRTOverScenePS::FUpsamplingMode>(UpsamplingMode);
		PermutationVector.Set<FComposeVolumetricRTOverScenePS::FRenderUnderWaterBuffer>(1);
		PermutationVector.Set<FComposeVolumetricRTOverScenePS::FRenderCameraUnderWater>(0);
		TShaderMapRef<FComposeVolumetricRTOverScenePS> PixelShader(ViewInfo.ShaderMap, PermutationVector);

		FComposeVolumetricRTOverScenePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FComposeVolumetricRTOverScenePS::FParameters>();
		PassParameters->ViewUniformBuffer = ViewInfo.ViewUniformBuffer;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(WaterPassData.ColorTexture, ERenderTargetLoadAction::ELoad);
		PassParameters->VolumetricTexture = VolumetricTexture;
		PassParameters->VolumetricDepthTexture = VolumetricDepthTexture;
		PassParameters->SceneDepthTexture = nullptr;
		PassParameters->WaterLinearDepthTexture = WaterPassData.DepthTexture;
		PassParameters->LinearTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
		PassParameters->UvOffsetScale = VolumetricCloudRT.GetUvNoiseScale();
		PassParameters->FullResolutionToVolumetricBufferResolutionScale = FVector2D(1.0f / float(GetMainDownsampleFactor(VRTMode)), float(GetMainDownsampleFactor(VRTMode)));
		PassParameters->FullResolutionToWaterBufferScale = FVector2D(1.0f / WaterPassData.RefractionDownsampleFactor, WaterPassData.RefractionDownsampleFactor);
		PassParameters->SceneWithoutSingleLayerWaterViewRect = FVector4(WaterPassViewData.ViewRect.Min.X, WaterPassViewData.ViewRect.Min.Y,
																		WaterPassViewData.ViewRect.Max.X, WaterPassViewData.ViewRect.Max.Y);
		GetTextureSafeUvCoordBound(PassParameters->VolumetricTexture, PassParameters->VolumetricTextureValidCoordRect, PassParameters->VolumetricTextureValidUvRect);

		FVector2D VolumetricTextureSize = FVector2D(float(VolumetricTexture->Desc.GetSize().X), float(VolumetricTexture->Desc.GetSize().Y));
		PassParameters->VolumetricTextureSizeAndInvSize = FVector4(VolumetricTextureSize.X, VolumetricTextureSize.Y, 1.0f / VolumetricTextureSize.X, 1.0f / VolumetricTextureSize.Y);

		FPixelShaderUtils::AddFullscreenPass<FComposeVolumetricRTOverScenePS>(
			GraphBuilder, ViewInfo.ShaderMap, RDG_EVENT_NAME("VolumetricComposeOverScene"), PixelShader, PassParameters, WaterPassViewData.ViewRect,
			PreMultipliedColorTransmittanceBlend);
	}
}

//////////////////////////////////////////////////////////////////////////



FTemporalRenderTargetState::FTemporalRenderTargetState()
	: CurrentRT(1)
	, FrameId(0)
	, bFirstTimeUsed(true)
	, bHistoryValid(false)
	, Resolution(FIntPoint::ZeroValue)
	, Format(PF_MAX)
{ 
}

FTemporalRenderTargetState::~FTemporalRenderTargetState()
{
}

void FTemporalRenderTargetState::Initialise(const FIntPoint& ResolutionIn, EPixelFormat FormatIn)
{
	// Update internal settings

	if (bFirstTimeUsed)
	{
		bFirstTimeUsed = false;
		bHistoryValid = false;
		FrameId = 0;
	}

	CurrentRT = 1 - CurrentRT;
	const uint32 PreviousRT = 1 - CurrentRT;

	FIntVector ResolutionVector = FIntVector(ResolutionIn.X, ResolutionIn.Y, 0);
	for (int32 i = 0; i < kRenderTargetCount; ++i)
	{
		if (RenderTargets[i].IsValid() && (RenderTargets[i]->GetDesc().GetSize() != ResolutionVector || Format != FormatIn))
		{
			// Resolution does not match so release target we are going to render in, keep the previous one at a different resolution.
			RenderTargets[i].SafeRelease();
		}
	}
	Resolution = ResolutionIn;
	Format = FormatIn;

	// Regular every frame update
	bHistoryValid = RenderTargets[PreviousRT].IsValid();
}

FRDGTextureRef FTemporalRenderTargetState::GetOrCreateCurrentRT(FRDGBuilder& GraphBuilder)
{
	check(Resolution.X > 0 && Resolution.Y > 0);

	if (RenderTargets[CurrentRT].IsValid())
	{
		return GraphBuilder.RegisterExternalTexture(RenderTargets[CurrentRT]);
	}

	FRDGTextureRef RDGTexture = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(Resolution, Format, FClearValueBinding(FLinearColor(0.0f, 0.0f, 0.0f, 1.0f)), 
			TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable), TEXT("TemporalRenderTarget"));
	return RDGTexture;
}
void FTemporalRenderTargetState::ExtractCurrentRT(FRDGBuilder& GraphBuilder, FRDGTextureRef RDGTexture)
{
	check(Resolution.X > 0 && Resolution.Y > 0);

	ConvertToExternalTexture(GraphBuilder, RDGTexture, RenderTargets[CurrentRT]);
}

FRDGTextureRef FTemporalRenderTargetState::GetOrCreatePreviousRT(FRDGBuilder& GraphBuilder)
{
	check(Resolution.X > 0 && Resolution.Y > 0);
	const uint32 PreviousRT = 1u - CurrentRT;
	check(RenderTargets[PreviousRT].IsValid());

	return GraphBuilder.RegisterExternalTexture(RenderTargets[PreviousRT]);
}

void FTemporalRenderTargetState::Reset()
{
	bFirstTimeUsed = false;
	bHistoryValid = false;
	FrameId = 0;
	for (int32 i = 0; i < kRenderTargetCount; ++i)
	{
		RenderTargets[i].SafeRelease();
	}
	Resolution = FIntPoint::ZeroValue;
	Format = PF_MAX;
}

