// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Misc/MediaTextureResource.h"
#include "MediaAssetsPrivate.h"

#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "ExternalTexture.h"
#include "IMediaPlayer.h"
#include "IMediaSamples.h"
#include "IMediaTextureSample.h"
#include "IMediaTextureSampleConverter.h"
#include "MediaPlayerFacade.h"
#include "MediaSampleSource.h"
#include "MediaShaders.h"
#include "PipelineStateCache.h"
#include "SceneUtils.h"
#include "Shader.h"
#include "StaticBoundShaderState.h"
#include "RenderUtils.h"
#include "RHIStaticStates.h"

#include "MediaTexture.h"


#define MEDIATEXTURERESOURCE_TRACE_RENDER 0


/** Time spent in media player facade closing media. */
DECLARE_CYCLE_STAT(TEXT("MediaAssets MediaTextureResource Render"), STAT_MediaAssets_MediaTextureResourceRender, STATGROUP_Media);

/** Sample time of texture last rendered. */
DECLARE_FLOAT_COUNTER_STAT(TEXT("MediaAssets MediaTextureResource Sample"), STAT_MediaUtils_TextureSampleTime, STATGROUP_Media);


static int32 CachedSamplesQueueDepth = 1;
static FAutoConsoleVariableRef CVarEncoderSaveVideoToFile(
	TEXT("media.CachedSamplesQueueDepth"),
	CachedSamplesQueueDepth,
	TEXT("How many frames to hold samples before release (default = 1)."),
	ECVF_Default);

/* Local helpers
 *****************************************************************************/

namespace MediaTextureResource
{
	/**
	 * Get the pixel format for a given sample.
	 *
	 * @param Sample The sample.
	 * @return The sample's pixel format.
	 */
	EPixelFormat GetPixelFormat(const TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& Sample)
	{
		switch (Sample->GetFormat())
		{
		case EMediaTextureSampleFormat::CharAYUV:
		case EMediaTextureSampleFormat::CharBGRA:
		case EMediaTextureSampleFormat::CharBMP:
		case EMediaTextureSampleFormat::CharUYVY:
		case EMediaTextureSampleFormat::CharYUY2:
		case EMediaTextureSampleFormat::CharYVYU:
			return PF_B8G8R8A8;

		case EMediaTextureSampleFormat::CharNV12:
		case EMediaTextureSampleFormat::CharNV21:
			return PF_G8;

		case EMediaTextureSampleFormat::FloatRGB:
			return PF_FloatRGB;

		case EMediaTextureSampleFormat::FloatRGBA:
			return PF_FloatRGBA;

		case EMediaTextureSampleFormat::CharBGR10A2:
			return PF_A2B10G10R10;

		case EMediaTextureSampleFormat::YUVv210:
			return PF_R32G32B32A32_UINT;

		case EMediaTextureSampleFormat::Y416:
			return PF_A16B16G16R16;

		default:
			return PF_Unknown;
		}
	}


	EPixelFormat GetConvertedPixelFormat(const TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& Sample)
	{
		switch (Sample->GetFormat())
		{
		case EMediaTextureSampleFormat::CharBGR10A2:
		case EMediaTextureSampleFormat::YUVv210:
			return PF_A2B10G10R10;
		case EMediaTextureSampleFormat::Y416:
			return PF_B8G8R8A8;
		default:
			return PF_B8G8R8A8;
		}
	}


	/**
	 * Check whether the given sample requires a conversion shader.
	 *
	 * @param Sample The sample to check.
	 * @param SrgbOutput Whether the output is expected in sRGB color space.
	 * @return true if conversion is required, false otherwise.
	 * @see RequiresOutputResource
	 */
	bool RequiresConversion(const TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& Sample, bool SrgbOutput)
	{
		// If the output color space is expected to be sRGB, but the
		// sample is not, a color space conversion on the GPU is required.

		if (Sample->IsOutputSrgb() != SrgbOutput)
		{
			return true;
		}

		// If the output dimensions are not the same as the sample's
		// dimensions, a resizing conversion on the GPU is required.

		if (Sample->GetDim() != Sample->GetOutputDim())
		{
			return true;
		}

		// Only the following pixel formats are supported natively.
		// All other formats require a conversion on the GPU.

		const EMediaTextureSampleFormat Format = Sample->GetFormat();

		return ((Format != EMediaTextureSampleFormat::CharBGRA) &&
				(Format != EMediaTextureSampleFormat::FloatRGB) &&
				(Format != EMediaTextureSampleFormat::FloatRGBA));
	}


	/**
	 * Check whether the given sample requires an sRGB texture.
	 *
	 * @param Sample The sample to check.
	 * @return true if an sRGB texture is required, false otherwise.
	 */
	bool RequiresSrgbTexture(const TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& Sample)
	{
		if (!Sample->IsOutputSrgb())
		{
			return false;
		}

		const EMediaTextureSampleFormat Format = Sample->GetFormat();

		return ((Format == EMediaTextureSampleFormat::CharBGRA) ||
				(Format == EMediaTextureSampleFormat::CharBMP) ||
				(Format == EMediaTextureSampleFormat::FloatRGB) ||
				(Format == EMediaTextureSampleFormat::FloatRGBA));
	}
}


/* FMediaTextureResource structors
 *****************************************************************************/

FMediaTextureResource::FMediaTextureResource(UMediaTexture& InOwner, FIntPoint& InOwnerDim, SIZE_T& InOwnerSize, FLinearColor InClearColor, FGuid InTextureGuid)
	: Cleared(false)
	, CurrentClearColor(InClearColor)
	, InitialTextureGuid(InTextureGuid)
	, Owner(InOwner)
	, OwnerDim(InOwnerDim)
	, OwnerSize(InOwnerSize)
{
	// preset the CachedSamples. makes things easier in the long run
	if (CachedSamplesQueueDepth > 0)
	{
		CachedSamples.AddDefaulted(CachedSamplesQueueDepth);
	}
}


/* FMediaTextureResource interface
 *****************************************************************************/

void FMediaTextureResource::Render(const FRenderParams& Params)
{
	check(IsInRenderingThread());

	CycleCachedSamples();

	SCOPE_CYCLE_COUNTER(STAT_MediaAssets_MediaTextureResourceRender);

	FLinearColor Rotation(1, 0, 0, 1);
	FLinearColor Offset(0, 0, 0, 0);

	TSharedPtr<FMediaTextureSampleSource, ESPMode::ThreadSafe> SampleSource = Params.SampleSource.Pin();

	if (SampleSource.IsValid())
	{
		// get the most current sample to be rendered
		TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe> TestSample;
		TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe> Sample;
		bool UseSample = false;
		
		while (SampleSource->Peek(TestSample) && TestSample.IsValid())
		{
			const FTimespan StartTime = TestSample->GetTime();
			const FTimespan EndTime = StartTime + TestSample->GetDuration();

			if ((Params.Rate >= 0.0f) && (Params.Time < StartTime))
			{
				break; // future sample (forward play)
			}

			if ((Params.Rate <= 0.0f) && (Params.Time >= EndTime))
			{
				break; // future sample (reverse play)
			}

			UseSample = SampleSource->Dequeue(Sample);

#if MEDIATEXTURERESOURCE_TRACE_RENDER
			if (!UseSample && Sample.IsValid())
			{
				UE_LOG(LogMediaAssets, VeryVerbose, TEXT("TextureResource %p: Sample with time %s got flushed at time %s"),
					this,
					*Sample->GetTime().ToString(TEXT("%h:%m:%s.%t")),
					*Params.Time.ToString(TEXT("%h:%m:%s.%t"))
				);
			}
#endif
		}

		if (UseSample)
		{
			// render the sample
			if (Sample->GetOutputDim().GetMin() <= 0)
			{
#if MEDIATEXTURERESOURCE_TRACE_RENDER
				UE_LOG(LogMediaAssets, VeryVerbose, TEXT("TextureResource %p: Corrupt sample with time %s at time %s"),
					this,
					*Sample->GetTime().ToString(TEXT("%h:%m:%s.%t")),
					*Params.Time.ToString(TEXT("%h:%m:%s.%t"))
				);
#endif

				ClearTexture(FLinearColor::Red, Params.SrgbOutput); // mark corrupt sample
			}
			else if (Sample->GetMediaTextureSampleConverter())
			{
				CreateOutputRenderTarget(Sample, Params);
				Sample->GetMediaTextureSampleConverter()->Convert(RenderTargetTextureRHI);
			}
			else if (MediaTextureResource::RequiresConversion(Sample, Params.SrgbOutput))
			{
#if MEDIATEXTURERESOURCE_TRACE_RENDER
				UE_LOG(LogMediaAssets, VeryVerbose, TEXT("TextureResource %p: Converting sample with time %s at time %s"),
					this,
					*Sample->GetTime().ToString(TEXT("%h:%m:%s.%t")),
					*Params.Time.ToString(TEXT("%h:%m:%s.%t"))
				);
#endif

				ConvertSample(Sample, Params.ClearColor, Params.SrgbOutput);
			}
			else
			{
#if MEDIATEXTURERESOURCE_TRACE_RENDER
				UE_LOG(LogMediaAssets, VeryVerbose, TEXT("TextureResource %p: Copying sample with time %s at time %s"),
					this,
					*Sample->GetTime().ToString(TEXT("%h:%m:%s.%t")),
					*Params.Time.ToString(TEXT("%h:%m:%s.%t"))
				);
#endif

				CopySample(Sample, Params.ClearColor, Params.SrgbOutput);
			}

			Rotation = Sample->GetScaleRotation();
			Offset = Sample->GetOffset();

			SET_FLOAT_STAT(STAT_MediaUtils_TextureSampleTime, Sample->GetTime().GetTotalMilliseconds());
		}
#if MEDIATEXTURERESOURCE_TRACE_RENDER
		else if (Sample.IsValid())
		{
			UE_LOG(LogMediaAssets, VeryVerbose, TEXT("TextureResource %p: Sample with time %s cannot be used at time %s"),
				this,
				*Sample->GetTime().ToString(TEXT("%h:%m:%s.%t")),
				*Params.Time.ToString(TEXT("%h:%m:%s.%t"))
			);
		}
		else
		{
			UE_LOG(LogMediaAssets, VeryVerbose, TEXT("TextureResource %p: No valid sample available at time %s"),
				this,
				*Params.Time.ToString(TEXT("%h:%m:%s.%t"))
			);
		}
#endif

		// We're not done with `Sample` as rendering is asynchronous. 
		// Hold a reference in a member to postpone recycling `Sample` until safe to release
		if (CachedSamplesQueueDepth > 0)
		{
			CachedSamples[0] = Sample;
		}
	}
	else if (Params.CanClear)
	{
		if (!Cleared || (Params.ClearColor != CurrentClearColor))
		{
#if MEDIATEXTURERESOURCE_TRACE_RENDER
			UE_LOG(LogMediaAssets, VeryVerbose, TEXT("TextureResource %p: Clearing texture at time %s"),
				this,
				*Params.Time.ToString(TEXT("%h:%m:%s.%t"))
			);
#endif

			ClearTexture(Params.ClearColor, Params.SrgbOutput);
		}
	}
	
	//Cache next available sample time in the MediaTexture owner since we're the only one that can consume from the queue
	CacheNextAvailableSampleTime(SampleSource);

	// update external texture registration
	if (!GSupportsImageExternal)
	{
		if (Params.CurrentGuid.IsValid())
		{
			FTextureRHIRef VideoTexture = (FTextureRHIRef)Owner.TextureReference.TextureReferenceRHI;
			FExternalTextureRegistry::Get().RegisterExternalTexture(Params.CurrentGuid, VideoTexture, SamplerStateRHI, Rotation, Offset);
		}

		if (Params.PreviousGuid.IsValid() && (Params.PreviousGuid != Params.CurrentGuid))
		{
			FExternalTextureRegistry::Get().UnregisterExternalTexture(Params.PreviousGuid);
		}
	}
	
	//Update usable Guid for the RenderThread
	Owner.SetRenderedExternalTextureGuid(Params.CurrentGuid);
}

void FMediaTextureResource::CycleCachedSamples()
{
	int32 CachedSampleToChange = CachedSamplesQueueDepth - 1;

	while (CachedSampleToChange > 0)
	{
		CachedSamples[CachedSampleToChange] = CachedSamples[CachedSampleToChange - 1];
		CachedSampleToChange--;
	}
}

/* FRenderTarget interface
 *****************************************************************************/

FIntPoint FMediaTextureResource::GetSizeXY() const
{
	return FIntPoint(Owner.GetWidth(), Owner.GetHeight());
}


/* FTextureResource interface
 *****************************************************************************/

FString FMediaTextureResource::GetFriendlyName() const
{
	return Owner.GetPathName();
}


uint32 FMediaTextureResource::GetSizeX() const
{
	return Owner.GetWidth();
}


uint32 FMediaTextureResource::GetSizeY() const
{
	return Owner.GetHeight();
}


void FMediaTextureResource::InitDynamicRHI()
{
	// create the sampler state
	FSamplerStateInitializerRHI SamplerStateInitializer(
		(ESamplerFilter)UDeviceProfileManager::Get().GetActiveProfile()->GetTextureLODSettings()->GetSamplerFilter(&Owner),
		(Owner.AddressX == TA_Wrap) ? AM_Wrap : ((Owner.AddressX == TA_Clamp) ? AM_Clamp : AM_Mirror),
		(Owner.AddressY == TA_Wrap) ? AM_Wrap : ((Owner.AddressY == TA_Clamp) ? AM_Clamp : AM_Mirror),
		AM_Wrap
	);

	SamplerStateRHI = RHICreateSamplerState(SamplerStateInitializer);

	// Note: set up default texture, or we can get sampler bind errors on render
	// we can't leave here without having a valid bindable resource for some RHIs.

	ClearTexture(CurrentClearColor, Owner.SRGB);

	// Make sure init has done it's job - we can't leave here without valid bindable resources for some RHI's
	check(TextureRHI.IsValid());
	check(RenderTargetTextureRHI.IsValid());
	check(OutputTarget.IsValid());

	if (!GSupportsImageExternal)
	{
		FTextureRHIRef VideoTexture = (FTextureRHIRef)Owner.TextureReference.TextureReferenceRHI;
		FExternalTextureRegistry::Get().RegisterExternalTexture(InitialTextureGuid, VideoTexture, SamplerStateRHI);
	}
}


void FMediaTextureResource::ReleaseDynamicRHI()
{
	Cleared = false;

	InputTarget.SafeRelease();
	OutputTarget.SafeRelease();
	RenderTargetTextureRHI.SafeRelease();
	TextureRHI.SafeRelease();

	UpdateTextureReference(nullptr);
}


/* FMediaTextureResource implementation
 *****************************************************************************/

void FMediaTextureResource::ClearTexture(const FLinearColor& ClearColor, bool SrgbOutput)
{
	// create output render target if we don't have one yet
	const uint32 OutputCreateFlags = TexCreate_Dynamic | (SrgbOutput ? TexCreate_SRGB : 0);
	const EPixelFormat OutputPixelFormat = PF_B8G8R8A8;

	if ((ClearColor != CurrentClearColor) || !OutputTarget.IsValid() || (OutputTarget->GetFormat() != OutputPixelFormat) || ((OutputTarget->GetFlags() & OutputCreateFlags) != OutputCreateFlags))
	{
		FString DebugName = Owner.GetName();

		FRHIResourceCreateInfo CreateInfo;
		CreateInfo.ClearValueBinding = FClearValueBinding(ClearColor);
		CreateInfo.DebugName = *DebugName;

		TRefCountPtr<FRHITexture2D> DummyTexture2DRHI;

		RHICreateTargetableShaderResource2D(
			2,
			2,
			OutputPixelFormat,
			1,
			OutputCreateFlags,
			TexCreate_RenderTargetable,
			false,
			CreateInfo,
			OutputTarget,
			DummyTexture2DRHI
		);

		CurrentClearColor = ClearColor;
		UpdateResourceSize();
	}

	if (RenderTargetTextureRHI != OutputTarget)
	{
		UpdateTextureReference(OutputTarget);
	}

	// draw the clear color
	FRHICommandListImmediate& CommandList = FRHICommandListExecutor::GetImmediateCommandList();
	{
		FRHIRenderPassInfo RPInfo(RenderTargetTextureRHI, ERenderTargetActions::Clear_Store);
		CommandList.BeginRenderPass(RPInfo, TEXT("ClearTexture"));
		CommandList.EndRenderPass();
		CommandList.SetViewport(0, 0, 0.0f, RenderTargetTextureRHI->GetSizeX(), RenderTargetTextureRHI->GetSizeY(), 1.0f);
		CommandList.TransitionResource(EResourceTransitionAccess::EReadable, RenderTargetTextureRHI);
	}

	Cleared = true;
}


void FMediaTextureResource::ConvertSample(const TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& Sample, const FLinearColor& ClearColor, bool SrgbOutput)
{
	const EPixelFormat InputPixelFormat = MediaTextureResource::GetPixelFormat(Sample);

	// get input texture
	FRHITexture2D* InputTexture = nullptr;
	{
		// If the sample already provides a texture resource, we simply use that
		// as the input texture. If the sample only provides raw data, then we
		// create our own input render target and copy the data into it.

		FRHITexture* SampleTexture = Sample->GetTexture();
		FRHITexture2D* SampleTexture2D = (SampleTexture != nullptr) ? SampleTexture->GetTexture2D() : nullptr;

		if (SampleTexture2D != nullptr)
		{
			InputTexture = SampleTexture2D;

			InputTarget.SafeRelease();
			UpdateResourceSize();
		}
		else
		{
			const bool SrgbTexture = MediaTextureResource::RequiresSrgbTexture(Sample);
			const uint32 InputCreateFlags = TexCreate_Dynamic | (SrgbTexture ? TexCreate_SRGB : 0);
			const FIntPoint SampleDim = Sample->GetDim();

			// create a new input render target if necessary
			if (!InputTarget.IsValid() || (InputTarget->GetSizeXY() != SampleDim) || (InputTarget->GetFormat() != InputPixelFormat) || ((InputTarget->GetFlags() & InputCreateFlags) != InputCreateFlags))
			{
				TRefCountPtr<FRHITexture2D> DummyTexture2DRHI;
				FRHIResourceCreateInfo CreateInfo;

				RHICreateTargetableShaderResource2D(
					SampleDim.X,
					SampleDim.Y,
					InputPixelFormat,
					1,
					InputCreateFlags,
					TexCreate_RenderTargetable,
					false,
					CreateInfo,
					InputTarget,
					DummyTexture2DRHI
				);

				UpdateResourceSize();
			}

			// copy sample data to input render target
			FUpdateTextureRegion2D Region(0, 0, 0, 0, SampleDim.X, SampleDim.Y);
			RHIUpdateTexture2D(InputTarget, 0, Region, Sample->GetStride(), (uint8*)Sample->GetBuffer());

			InputTexture = InputTarget;
		}
	}

	// create output render target if necessary
	const uint32 OutputCreateFlags = TexCreate_Dynamic | (SrgbOutput ? TexCreate_SRGB : 0);
	const FIntPoint OutputDim = Sample->GetOutputDim();
	const EPixelFormat OutputPixelFormat = MediaTextureResource::GetConvertedPixelFormat(Sample);

	if ((ClearColor != CurrentClearColor) || !OutputTarget.IsValid() || (OutputTarget->GetSizeXY() != OutputDim) || (OutputTarget->GetFormat() != OutputPixelFormat) || ((OutputTarget->GetFlags() & OutputCreateFlags) != OutputCreateFlags))
	{
		TRefCountPtr<FRHITexture2D> DummyTexture2DRHI;
		
		FRHIResourceCreateInfo CreateInfo = {
			FClearValueBinding(ClearColor)
		};

		RHICreateTargetableShaderResource2D(
			OutputDim.X,
			OutputDim.Y,
			OutputPixelFormat,
			1,
			OutputCreateFlags,
			TexCreate_RenderTargetable,
			false,
			CreateInfo,
			OutputTarget,
			DummyTexture2DRHI
		);

		CurrentClearColor = ClearColor;
		UpdateResourceSize();
	}

	if (RenderTargetTextureRHI != OutputTarget)
	{
		UpdateTextureReference(OutputTarget);
	}

	// perform the conversion
	FRHICommandListImmediate& CommandList = FRHICommandListExecutor::GetImmediateCommandList();
	{
		SCOPED_DRAW_EVENT(CommandList, MediaTextureConvertResource);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		FRHITexture* RenderTarget = RenderTargetTextureRHI.GetReference();

		FRHIRenderPassInfo RPInfo(RenderTarget, ERenderTargetActions::Load_Store);
		CommandList.BeginRenderPass(RPInfo, TEXT("ConvertMedia"));
		{
			CommandList.ApplyCachedRenderTargets(GraphicsPSOInit);
			CommandList.SetViewport(0, 0, 0.0f, OutputDim.X, OutputDim.Y, 1.0f);

			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.BlendState = TStaticBlendStateWriteMask<CW_RGBA, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE>::GetRHI();
			GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

			// configure media shaders
			auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
			TShaderMapRef<FMediaShadersVS> VertexShader(ShaderMap);

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GMediaVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);

			FMatrix YUVToRGBMatrix = Sample->GetYUVToRGBMatrix();
			FVector YUVOffset(MediaShaders::YUVOffset8bits);

			if (Sample->GetFormat() == EMediaTextureSampleFormat::YUVv210)
			{
				YUVOffset = MediaShaders::YUVOffset10bits;
			}

			bool bIsSampleOutputSrgb = Sample->IsOutputSrgb();
			if (GMaxRHIFeatureLevel == ERHIFeatureLevel::ES2 && IsSimulatedPlatform(GMaxRHIShaderPlatform) )
			{
				// simulated ES2 has no HW support for sRGB, all external textures are assumed to be in sRGB form. 
				// see FHLSLMaterialTranslator::TextureSample(), SAMPLERTYPE_External case.
				// We must not convert to linear for the ES2 case.
				bIsSampleOutputSrgb = false;
			}

			switch (Sample->GetFormat())
			{
			case EMediaTextureSampleFormat::CharAYUV:
			{
				TShaderMapRef<FAYUVConvertPS> ConvertShader(ShaderMap);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*ConvertShader);
				SetGraphicsPipelineState(CommandList, GraphicsPSOInit);
				ConvertShader->SetParameters(CommandList, InputTexture, YUVToRGBMatrix, YUVOffset, bIsSampleOutputSrgb);
			}
			break;

			case EMediaTextureSampleFormat::CharBMP:
			{
				TShaderMapRef<FBMPConvertPS> ConvertShader(ShaderMap);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*ConvertShader);
				SetGraphicsPipelineState(CommandList, GraphicsPSOInit);
				ConvertShader->SetParameters(CommandList, InputTexture, OutputDim, bIsSampleOutputSrgb && !SrgbOutput);
			}
			break;

			case EMediaTextureSampleFormat::CharNV12:
			{
				TShaderMapRef<FNV12ConvertPS> ConvertShader(ShaderMap);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*ConvertShader);
				SetGraphicsPipelineState(CommandList, GraphicsPSOInit);
				ConvertShader->SetParameters(CommandList, InputTexture, OutputDim, YUVToRGBMatrix, YUVOffset, bIsSampleOutputSrgb);
			}
			break;

			case EMediaTextureSampleFormat::CharNV21:
			{
				TShaderMapRef<FNV21ConvertPS> ConvertShader(ShaderMap);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*ConvertShader);
				SetGraphicsPipelineState(CommandList, GraphicsPSOInit);
				ConvertShader->SetParameters(CommandList, InputTexture, OutputDim, YUVToRGBMatrix, YUVOffset, bIsSampleOutputSrgb);
			}
			break;

			case EMediaTextureSampleFormat::CharUYVY:
			{
				TShaderMapRef<FUYVYConvertPS> ConvertShader(ShaderMap);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*ConvertShader);
				SetGraphicsPipelineState(CommandList, GraphicsPSOInit);
				ConvertShader->SetParameters(CommandList, InputTexture, YUVToRGBMatrix, YUVOffset, bIsSampleOutputSrgb);
			}
			break;

			case EMediaTextureSampleFormat::CharYUY2:
			{
				TShaderMapRef<FYUY2ConvertPS> ConvertShader(ShaderMap);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*ConvertShader);
				SetGraphicsPipelineState(CommandList, GraphicsPSOInit);
				ConvertShader->SetParameters(CommandList, InputTexture, OutputDim, YUVToRGBMatrix, YUVOffset, bIsSampleOutputSrgb);
			}
			break;

			case EMediaTextureSampleFormat::CharYVYU:
			{
				TShaderMapRef<FYVYUConvertPS> ConvertShader(ShaderMap);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*ConvertShader);
				SetGraphicsPipelineState(CommandList, GraphicsPSOInit);
				ConvertShader->SetParameters(CommandList, InputTexture, YUVToRGBMatrix, YUVOffset, bIsSampleOutputSrgb);
			}
			break;

			case EMediaTextureSampleFormat::YUVv210:
			{
				TShaderMapRef<FYUVv210ConvertPS> ConvertShader(ShaderMap);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*ConvertShader);
				SetGraphicsPipelineState(CommandList, GraphicsPSOInit);
				ConvertShader->SetParameters(CommandList, InputTexture, OutputDim, YUVToRGBMatrix, YUVOffset, bIsSampleOutputSrgb);
			}
			break;

			case EMediaTextureSampleFormat::CharBGR10A2:
			{
				TShaderMapRef<FRGBConvertPS> ConvertShader(ShaderMap);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*ConvertShader);
				SetGraphicsPipelineState(CommandList, GraphicsPSOInit);
				ConvertShader->SetParameters(CommandList, InputTexture, OutputDim, bIsSampleOutputSrgb);
			}
			break;

			case EMediaTextureSampleFormat::CharBGRA:
			case EMediaTextureSampleFormat::FloatRGB:
			case EMediaTextureSampleFormat::FloatRGBA:
			{
				TShaderMapRef<FRGBConvertPS> ConvertShader(ShaderMap);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*ConvertShader);
				SetGraphicsPipelineState(CommandList, GraphicsPSOInit);
				ConvertShader->SetParameters(CommandList, InputTexture, OutputDim, false);
			}
			break;

			default:
				return; // unsupported format
			}

			// draw full size quad into render target
			FVertexBufferRHIRef VertexBuffer = CreateTempMediaVertexBuffer();
			CommandList.SetStreamSource(0, VertexBuffer, 0);
			// set viewport to RT size
			CommandList.SetViewport(0, 0, 0.0f, OutputDim.X, OutputDim.Y, 1.0f);

			CommandList.DrawPrimitive(0, 2, 1);
		}
		CommandList.EndRenderPass();
		CommandList.TransitionResource(EResourceTransitionAccess::EReadable, RenderTargetTextureRHI);
	}

	Cleared = false;
}


void FMediaTextureResource::CopySample(const TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& Sample, const FLinearColor& ClearColor, bool SrgbOutput)
{
	FRHITexture* SampleTexture = Sample->GetTexture();
	FRHITexture2D* SampleTexture2D = (SampleTexture != nullptr) ? SampleTexture->GetTexture2D() : nullptr;

	// If the sample already provides a texture resource, we simply use that
	// as the output render target. If the sample only provides raw data, then
	// we create our own output render target and copy the data into it.

	if (SampleTexture2D != nullptr)
	{
		// use sample's texture as the new render target.
		if (TextureRHI != SampleTexture2D)
		{
			UpdateTextureReference(SampleTexture2D);

			OutputTarget.SafeRelease();
			UpdateResourceSize();
		}
	}
	else
	{
		// create a new output render target if necessary
		const uint32 OutputCreateFlags = TexCreate_Dynamic | (SrgbOutput ? TexCreate_SRGB : 0);
		const EPixelFormat SampleFormat = MediaTextureResource::GetPixelFormat(Sample);
		const FIntPoint SampleDim = Sample->GetDim();

		if ((ClearColor != CurrentClearColor) || !OutputTarget.IsValid() || (OutputTarget->GetSizeXY() != SampleDim) || (OutputTarget->GetFormat() != SampleFormat) || ((OutputTarget->GetFlags() & OutputCreateFlags) != OutputCreateFlags))
		{
			TRefCountPtr<FRHITexture2D> DummyTexture2DRHI;

			FRHIResourceCreateInfo CreateInfo = {
				FClearValueBinding(ClearColor)
			};

			RHICreateTargetableShaderResource2D(
				SampleDim.X,
				SampleDim.Y,
				SampleFormat,
				1,
				OutputCreateFlags,
				TexCreate_RenderTargetable,
				false,
				CreateInfo,
				OutputTarget,
				DummyTexture2DRHI
			);

			CurrentClearColor = ClearColor;
			UpdateResourceSize();
		}

		if (RenderTargetTextureRHI != OutputTarget)
		{
			UpdateTextureReference(OutputTarget);
		}

		// copy sample data to output render target
		FUpdateTextureRegion2D Region(0, 0, 0, 0, SampleDim.X, SampleDim.Y);
		RHIUpdateTexture2D(RenderTargetTextureRHI.GetReference(), 0, Region, Sample->GetStride(), (uint8*)Sample->GetBuffer());
	}

	Cleared = false;
}


void FMediaTextureResource::UpdateResourceSize()
{
	SIZE_T ResourceSize = 0;

	if (InputTarget.IsValid())
	{
		ResourceSize += CalcTextureSize(InputTarget->GetSizeX(), InputTarget->GetSizeY(), InputTarget->GetFormat(), 1);
	}

	if (OutputTarget.IsValid())
	{
		ResourceSize += CalcTextureSize(OutputTarget->GetSizeX(), OutputTarget->GetSizeY(), OutputTarget->GetFormat(), 1);
	}

	OwnerSize = ResourceSize;
}


void FMediaTextureResource::UpdateTextureReference(FRHITexture2D* NewTexture)
{
	TextureRHI = NewTexture;
	RenderTargetTextureRHI = NewTexture;

	RHIUpdateTextureReference(Owner.TextureReference.TextureReferenceRHI, NewTexture);

	if (RenderTargetTextureRHI != nullptr)
	{
		OwnerDim = FIntPoint(RenderTargetTextureRHI->GetSizeX(), RenderTargetTextureRHI->GetSizeY());
	}
	else
	{
		OwnerDim = FIntPoint::ZeroValue;
	}
}

void FMediaTextureResource::CreateOutputRenderTarget(const TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& InSample, const FRenderParams& InParams)
{
	// create output render target if necessary
	const uint32 OutputCreateFlags = TexCreate_Dynamic | (InParams.SrgbOutput ? TexCreate_SRGB : 0);
	const FIntPoint OutputDim = InSample->GetOutputDim();
	const EPixelFormat OutputPixelFormat = MediaTextureResource::GetConvertedPixelFormat(InSample);

	if ((InParams.ClearColor != CurrentClearColor) || !OutputTarget.IsValid() || (OutputTarget->GetSizeXY() != OutputDim) || (OutputTarget->GetFormat() != OutputPixelFormat) || ((OutputTarget->GetFlags() & OutputCreateFlags) != OutputCreateFlags))
	{
		TRefCountPtr<FRHITexture2D> DummyTexture2DRHI;

		FRHIResourceCreateInfo CreateInfo = {
			FClearValueBinding(InParams.ClearColor)
		};

		RHICreateTargetableShaderResource2D(
			OutputDim.X,
			OutputDim.Y,
			OutputPixelFormat,
			1,
			OutputCreateFlags,
			TexCreate_RenderTargetable,
			false,
			CreateInfo,
			OutputTarget,
			DummyTexture2DRHI
		);

		CurrentClearColor = InParams.ClearColor;
		UpdateResourceSize();
	}

	if (RenderTargetTextureRHI != OutputTarget)
	{
		UpdateTextureReference(OutputTarget);
	}
}

void FMediaTextureResource::CacheNextAvailableSampleTime(const TSharedPtr<FMediaTextureSampleSource, ESPMode::ThreadSafe>& InSampleQueue) const
{
	FTimespan SampleTime(FTimespan::MinValue());

	if (InSampleQueue.IsValid())
	{
		TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe> Sample;
		if (InSampleQueue->Peek(Sample))
		{
			SampleTime = Sample->GetTime();
		}
	}

	Owner.CacheNextAvailableSampleTime(SampleTime);
}
