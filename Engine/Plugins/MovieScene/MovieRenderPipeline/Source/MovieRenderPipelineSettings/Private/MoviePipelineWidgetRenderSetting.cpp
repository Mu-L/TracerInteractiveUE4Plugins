// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineWidgetRenderSetting.h"
#include "Slate/WidgetRenderer.h"
#include "MovieRenderPipelineDataTypes.h"
#include "MoviePipelineBurnInWidget.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelineMasterConfig.h"
#include "MoviePipeline.h"
#include "Engine/TextureRenderTarget2D.h"
#include "MoviePipelineOutputBuilder.h"
#include "ImagePixelData.h"
#include "Widgets/SViewport.h"
#include "Slate/SGameLayerManager.h"

void UMoviePipelineWidgetRenderer::GatherOutputPassesImpl(TArray<FMoviePipelinePassIdentifier>& ExpectedRenderPasses)
{
	// If this was transiently added, don't render.
	if (!GetIsUserCustomized() || !IsEnabled())
	{
		return;
	}

	ExpectedRenderPasses.Add(FMoviePipelinePassIdentifier(TEXT("ViewportUI")));
}

void UMoviePipelineWidgetRenderer::RenderSample_GameThreadImpl(const FMoviePipelineRenderPassMetrics& InSampleState)
{
	// If this was transiently added, don't make a burn-in.
	if (!GetIsUserCustomized() || !IsEnabled())
	{
		return;
	}

	if (InSampleState.bDiscardResult)
	{
		return;
	}

	const bool bFirstTile = InSampleState.GetTileIndex() == 0;
	const bool bFirstSpatial = InSampleState.SpatialSampleIndex == (InSampleState.SpatialSampleCount - 1);
	const bool bFirstTemporal = InSampleState.TemporalSampleIndex == (InSampleState.TemporalSampleCount - 1);

	if (bFirstTile && bFirstSpatial && bFirstTemporal)
	{
		// Draw the widget to the render target
		FRenderTarget* BackbufferRenderTarget = RenderTarget->GameThread_GetRenderTargetResource();
		
		ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();

		// Cast the interface to a widget is a little yucky but the implementation is unlikely to change.
		TSharedPtr<SGameLayerManager> GameLayerManager = StaticCastSharedPtr<SGameLayerManager>(LocalPlayer->ViewportClient->GetGameLayerManager());

		WidgetRenderer->DrawWidget(BackbufferRenderTarget, GameLayerManager.ToSharedRef(), 1.f, FVector2D(RenderTarget->SizeX, RenderTarget->SizeY), (float)InSampleState.OutputState.TimeData.FrameDeltaTime);

		TSharedPtr<FMoviePipelineOutputMerger, ESPMode::ThreadSafe> OutputBuilder = GetPipeline()->OutputBuilder;

		ENQUEUE_RENDER_COMMAND(BurnInRenderTargetResolveCommand)(
			[InSampleState, BackbufferRenderTarget, OutputBuilder](FRHICommandListImmediate& RHICmdList)
		{
			FIntRect SourceRect = FIntRect(0, 0, BackbufferRenderTarget->GetSizeXY().X, BackbufferRenderTarget->GetSizeXY().Y);

			// Read the data back to the CPU
			TArray<FColor> RawPixels;
			RawPixels.SetNum(SourceRect.Width() * SourceRect.Height());

			FReadSurfaceDataFlags ReadDataFlags(ERangeCompressionMode::RCM_MinMax);
			ReadDataFlags.SetLinearToGamma(false);

			RHICmdList.ReadSurfaceData(BackbufferRenderTarget->GetRenderTargetTexture(), SourceRect, RawPixels, ReadDataFlags);

			TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> FrameData = MakeShared<FImagePixelDataPayload, ESPMode::ThreadSafe>();
			FrameData->OutputState = InSampleState.OutputState;
			FrameData->PassIdentifier = FMoviePipelinePassIdentifier(TEXT("ViewportUI"));
			FrameData->SampleState = InSampleState;
			FrameData->bRequireTransparentOutput = true;

			TUniquePtr<FImagePixelData> PixelData = MakeUnique<TImagePixelData<FColor>>(InSampleState.BackbufferSize, TArray64<FColor>(MoveTemp(RawPixels)), FrameData);

			OutputBuilder->OnCompleteRenderPassDataAvailable_AnyThread(MoveTemp(PixelData), FrameData);
		});
	}
}

void UMoviePipelineWidgetRenderer::SetupImpl(TArray<TSharedPtr<MoviePipeline::FMoviePipelineEnginePass>>& InEnginePasses, const MoviePipeline::FMoviePipelineRenderPassInitSettings& InPassInitSettings)
{
	// If this was transiently added, don't render.
	if (!GetIsUserCustomized() || !IsEnabled())
	{
		return;
	}

	RenderTarget = NewObject<UTextureRenderTarget2D>();
	RenderTarget->ClearColor = FLinearColor::Transparent;

	bool bInForceLinearGamma = false;
	FIntPoint OutputResolution = GetPipeline()->GetPipelineMasterConfig()->FindSetting<UMoviePipelineOutputSetting>()->OutputResolution;
	RenderTarget->InitCustomFormat(OutputResolution.X, OutputResolution.Y, EPixelFormat::PF_B8G8R8A8, bInForceLinearGamma);

	bool bApplyGammaCorrection = false;
	WidgetRenderer = MakeShared<FWidgetRenderer>(bApplyGammaCorrection);
}

void UMoviePipelineWidgetRenderer::TeardownImpl() 
{
	// If this was transiently added, don't render.
	if (!GetIsUserCustomized() || !IsEnabled())
	{
		return;
	}
	
	FlushRenderingCommands();

	WidgetRenderer = nullptr;
	RenderTarget = nullptr;
}

FText UMoviePipelineWidgetRenderer::GetFooterText(UMoviePipelineExecutorJob* InJob) const
{ 
	return NSLOCTEXT("MovieRenderPipeline", "WidgetRenderSetting_NoCompositeWarning", "This will render widgets added to the Viewport to a separate texture with alpha. This is currently not composited onto the final image, and will need to be combined in post.");
}