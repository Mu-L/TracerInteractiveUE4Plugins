// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "MediaCapture.h"

#include "Engine/GameEngine.h"
#include "Engine/RendererSettings.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineModule.h"
#include "GenericPlatform/GenericPlatformAtomics.h"
#include "MediaIOCoreModule.h"
#include "MediaOutput.h"
#include "Misc/App.h"
#include "RendererInterface.h"
#include "RenderUtils.h"
#include "Slate/SceneViewport.h"
#include "Misc/ScopeLock.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/Engine.h"
#include "Engine/EngineTypes.h"
#endif //WITH_EDITOR

/* namespace MediaCaptureDetails definition
*****************************************************************************/

namespace MediaCaptureDetails
{
	bool FindSceneViewportAndLevel(TSharedPtr<FSceneViewport>& OutSceneViewport);

	//Validation for the source of a capture
	bool ValidateSceneViewport(const TSharedPtr<FSceneViewport>& SceneViewport, const FIntPoint& DesiredSize, const EPixelFormat DesiredPixelFormat, const bool bCurrentlyCapturing);
	bool ValidateTextureRenderTarget2D(const UTextureRenderTarget2D* RenderTarget, const FIntPoint& DesiredSize, const EPixelFormat DesiredPixelFormat, const bool bCurrentlyCapturing);

	//Validation that there is a capture 
	bool ValidateIsCapturing(const UMediaCapture& CaptureToBeValidated);
}


/* UMediaCapture::FCaptureBaseData
*****************************************************************************/
UMediaCapture::FCaptureBaseData::FCaptureBaseData()
	: SourceFrameNumberRenderThread(0)
{

}

/* UMediaCapture::FCaptureFrame
*****************************************************************************/
UMediaCapture::FCaptureFrame::FCaptureFrame()
	: bResolvedTargetRequested(false)
{

}


/* UMediaCapture
*****************************************************************************/

UMediaCapture::UMediaCapture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, MediaState(EMediaCaptureState::Stopped)
	, CurrentResolvedTargetIndex(0)
	, NumberOfCaptureFrame(2)
	, DesiredSize(1280, 720)
	, DesiredPixelFormat(EPixelFormat::PF_A2B10G10R10)
	, bResolvedTargetInitialized(false)
	, bWaitingForResolveCommandExecution(false)
{
}

void UMediaCapture::BeginDestroy()
{
	if (MediaState == EMediaCaptureState::Capturing || MediaState == EMediaCaptureState::Preparing)
	{
		UE_LOG(LogMediaIOCore, Warning, TEXT("%s will be destroyed and the capture was not stopped."), *GetName());
	}
	StopCapture(false);

	Super::BeginDestroy();
}

FString UMediaCapture::GetDesc()
{
	if (MediaOutput)
	{
		return FString::Printf(TEXT("%s [%s]"), *Super::GetDesc(), *MediaOutput->GetDesc());
	}
	return FString::Printf(TEXT("%s [none]"), *Super::GetDesc());
}

bool UMediaCapture::CaptureActiveSceneViewport()
{
	StopCapture(false);

	check(IsInGameThread());

	TSharedPtr<FSceneViewport> FoundSceneViewport;
	if (!MediaCaptureDetails::FindSceneViewportAndLevel(FoundSceneViewport) || !FoundSceneViewport.IsValid())
	{
		UE_LOG(LogMediaIOCore, Warning, TEXT("Can not start the capture. No viewport could be found. Play in 'Standalone' or in 'New Editor Window PIE'."));
		return false;
	}

	return CaptureSceneViewport(FoundSceneViewport);
}

bool UMediaCapture::CaptureSceneViewport(TSharedPtr<FSceneViewport>& InSceneViewport)
{
	StopCapture(false);

	check(IsInGameThread());

	if (!ValidateMediaOutput())
	{
		return false;
	}

	DesiredSize = MediaOutput->GetRequestedSize();
	DesiredPixelFormat = MediaOutput->GetRequestedPixelFormat();

	const bool bCurrentlyCapturing = false;
	if (!MediaCaptureDetails::ValidateSceneViewport(InSceneViewport, DesiredSize, DesiredPixelFormat, bCurrentlyCapturing))
	{
		return false;
	}

	MediaState = EMediaCaptureState::Preparing;
	if (!CaptureSceneViewportImpl(InSceneViewport))
	{
		MediaState = EMediaCaptureState::Stopped;
		return false;
	}

	//no lock required the command on the render thread is not active
	CapturingSceneViewport = InSceneViewport;

	InitializeResolveTarget(MediaOutput->NumberOfTextureBuffers);
	CurrentResolvedTargetIndex = 0;
	FCoreDelegates::OnEndFrame.AddUObject(this, &UMediaCapture::OnEndFrame_GameThread);

	return true;
}

bool UMediaCapture::CaptureTextureRenderTarget2D(UTextureRenderTarget2D* InRenderTarget2D)
{
	StopCapture(false);

	check(IsInGameThread());

	DesiredSize = MediaOutput->GetRequestedSize();
	DesiredPixelFormat = MediaOutput->GetRequestedPixelFormat();

	const bool bCurrentlyCapturing = false;
	if (!MediaCaptureDetails::ValidateTextureRenderTarget2D(InRenderTarget2D, DesiredSize, DesiredPixelFormat, bCurrentlyCapturing))
	{
		return false;
	}

	if (!CaptureRenderTargetImpl(InRenderTarget2D))
	{
		return false;
	}

	//no lock required the command on the render thread is not active yet
	CapturingRenderTarget = InRenderTarget2D;

	InitializeResolveTarget(MediaOutput->NumberOfTextureBuffers);
	CurrentResolvedTargetIndex = 0;
	FCoreDelegates::OnEndFrame.AddUObject(this, &UMediaCapture::OnEndFrame_GameThread);
	MediaState = EMediaCaptureState::Preparing;

	return true;
}

bool UMediaCapture::UpdateSceneViewport(TSharedPtr<FSceneViewport>& InSceneViewport)
{
	if (!MediaCaptureDetails::ValidateIsCapturing(*this))
	{
		StopCapture(false);
		return false;
	}

	check(IsInGameThread());

	const bool bCurrentlyCapturing = true;

	if (!MediaCaptureDetails::ValidateSceneViewport(InSceneViewport, DesiredSize, DesiredPixelFormat, bCurrentlyCapturing))
	{
		StopCapture(false);
		return false;
	}

	if (!UpdateSceneViewportImpl(InSceneViewport))
	{
		StopCapture(false);
		return false;
	}

	{
		FScopeLock Lock(&AccessingCapturingSource);
		CapturingSceneViewport = InSceneViewport;
		CapturingRenderTarget = nullptr;
	}

	return true;
}

bool UMediaCapture::UpdateTextureRenderTarget2D(UTextureRenderTarget2D * InRenderTarget2D)
{
	if (!MediaCaptureDetails::ValidateIsCapturing(*this))
	{
		StopCapture(false);
		return false;
	}

	check(IsInGameThread());

	const bool bCurrentlyCapturing = true;
	if (!MediaCaptureDetails::ValidateTextureRenderTarget2D(InRenderTarget2D, DesiredSize, DesiredPixelFormat, bCurrentlyCapturing))
	{
		StopCapture(false);
		return false;
	}

	if (!UpdateRenderTargetImpl(InRenderTarget2D))
	{
		StopCapture(false);
		return false;
	}

	{
		FScopeLock Lock(&AccessingCapturingSource);
		CapturingRenderTarget = InRenderTarget2D;
		CapturingSceneViewport.Reset();
	}

	return true;
}

void UMediaCapture::StopCapture(bool bAllowPendingFrameToBeProcess)
{
	check(IsInGameThread());

	if (MediaState != EMediaCaptureState::StopRequested && MediaState != EMediaCaptureState::Capturing)
	{
		bAllowPendingFrameToBeProcess = false;
	}

	if (bAllowPendingFrameToBeProcess)
	{
		if (MediaState != EMediaCaptureState::Stopped && MediaState != EMediaCaptureState::StopRequested)
		{
			MediaState = EMediaCaptureState::StopRequested;
		}
	}
	else
	{
		if (MediaState != EMediaCaptureState::Stopped)
		{
			MediaState = EMediaCaptureState::Stopped;

			FCoreDelegates::OnEndFrame.RemoveAll(this);

			while (bWaitingForResolveCommandExecution || !bResolvedTargetInitialized)
			{
				FlushRenderingCommands();
			}
			StopCaptureImpl(bAllowPendingFrameToBeProcess);

			CapturingRenderTarget = nullptr;
			CapturingSceneViewport.Reset();
			CaptureFrames.Reset();
			DesiredSize = FIntPoint(1280, 720);
			DesiredPixelFormat = EPixelFormat::PF_A2B10G10R10;
		}
	}
}

void UMediaCapture::SetMediaOutput(UMediaOutput* InMediaOutput)
{
	if (GetState() == EMediaCaptureState::Stopped)
	{
		MediaOutput = InMediaOutput;
	}
}

bool UMediaCapture::HasFinishedProcessing() const
{
	return bWaitingForResolveCommandExecution == false
		|| MediaState == EMediaCaptureState::Error
		|| MediaState == EMediaCaptureState::Stopped;
}

void UMediaCapture::InitializeResolveTarget(int32 InNumberOfBuffers)
{
	NumberOfCaptureFrame = InNumberOfBuffers;
	check(CaptureFrames.Num() == 0);
	CaptureFrames.AddDefaulted(InNumberOfBuffers);

	auto RenderCommand = [this](FRHICommandListImmediate& RHICmdList)
	{
		FRHIResourceCreateInfo CreateInfo;
		for (int32 Index = 0; Index < NumberOfCaptureFrame; ++Index)
		{
			CaptureFrames[Index].ReadbackTexture = RHICreateTexture2D(
				DesiredSize.X,
				DesiredSize.Y,
				DesiredPixelFormat,
				1,
				1,
				TexCreate_CPUReadback,
				CreateInfo
			);
		}
		bResolvedTargetInitialized = true;
	};

	ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER(
		MediaOutputCaptureFrameCreateTexture,
		decltype(RenderCommand), InRenderCommand, RenderCommand,
		{
			InRenderCommand(RHICmdList);
		});
}

bool UMediaCapture::ValidateMediaOutput() const
{
	if (MediaOutput == nullptr)
	{
		UE_LOG(LogMediaIOCore, Error, TEXT("Can not start the capture. The Media Output is invalid."));
		return false;
	}

	FString FailureReason;
	if (!MediaOutput->Validate(FailureReason))
	{
		UE_LOG(LogMediaIOCore, Error, TEXT("Can not start the capture. %s."), *FailureReason);
		return false;
	}

	return true;
}

void UMediaCapture::OnEndFrame_GameThread()
{
	if (!bResolvedTargetInitialized)
	{
		FlushRenderingCommands();
	}

	if (!MediaOutput)
	{
		return;
	}

	if (GetState() == EMediaCaptureState::Error)
	{
		StopCapture(false);
	}

	if (GetState() != EMediaCaptureState::Capturing && GetState() != EMediaCaptureState::StopRequested)
	{
		return;
	}

	int32 ReadyFrameIndex = (CurrentResolvedTargetIndex) % NumberOfCaptureFrame; // Next one in the buffer queue
	CurrentResolvedTargetIndex = (CurrentResolvedTargetIndex + 1) % NumberOfCaptureFrame;

	FCaptureFrame* ReadyFrame = (CaptureFrames[ReadyFrameIndex].bResolvedTargetRequested) ? &CaptureFrames[ReadyFrameIndex] : nullptr;
	FCaptureFrame* CapturingFrame = (GetState() != EMediaCaptureState::StopRequested) ? &CaptureFrames[CurrentResolvedTargetIndex] : nullptr;

	if (ReadyFrame == nullptr && GetState() == EMediaCaptureState::StopRequested)
	{
		// All the requested frames have been captured.
		StopCapture(false);
		return;
	}

	if (CapturingFrame)
	{
		//Verify if game thread is overrunning the render thread. 
		if (CapturingFrame->bResolvedTargetRequested)
		{
			FlushRenderingCommands();
		}

		CapturingFrame->CaptureBaseData.SourceFrameTimecode = FApp::GetTimecode();
		CapturingFrame->CaptureBaseData.SourceFrameNumberRenderThread = GFrameNumber;
		CapturingFrame->UserData = GetCaptureFrameUserData_GameThread();
	}

	bWaitingForResolveCommandExecution = true;

	// RenderCommand to be executed on the RenderThread
	auto RenderCommand = [this](FRHICommandListImmediate& RHICmdList, FCaptureFrame* InCapturingFrame, FCaptureFrame* InReadyFrame)
	{
		FTexture2DRHIRef SourceTexture;
		{
			FScopeLock Lock(&AccessingCapturingSource);

			UTextureRenderTarget2D* InCapturingRenderTarget = CapturingRenderTarget;
			TSharedPtr<FSceneViewport> InSceneViewportPtr = CapturingSceneViewport.Pin();

			if (InSceneViewportPtr.IsValid())
			{
				SourceTexture = InSceneViewportPtr->GetRenderTargetTexture();
				if (!SourceTexture.IsValid() && InSceneViewportPtr->GetViewportRHI())
				{
					SourceTexture = RHICmdList.GetViewportBackBuffer(InSceneViewportPtr->GetViewportRHI());
				}
			}
			else if (InCapturingRenderTarget)
			{
				if (InCapturingRenderTarget->GetRenderTargetResource() != nullptr && InCapturingRenderTarget->GetRenderTargetResource()->GetTextureRenderTarget2DResource() != nullptr)
				{
					SourceTexture = InCapturingRenderTarget->GetRenderTargetResource()->GetTextureRenderTarget2DResource()->GetTextureRHI();
				}
			}
		}

		if (!SourceTexture.IsValid())
		{
			MediaState = EMediaCaptureState::Error;
			UMediaOutput* InMediaOutput = nullptr;
			FPlatformAtomics::InterlockedExchangePtr((void**)(&InMediaOutput), MediaOutput);
			UE_LOG(LogMediaIOCore, Error, TEXT("Can't grab the Texture to capture for '%s'."), InMediaOutput ? *InMediaOutput->GetName() : TEXT("[undefined]"));
		}
		else if (InCapturingFrame)
		{
			if (InCapturingFrame->ReadbackTexture->GetSizeX() != SourceTexture->GetSizeX()
				|| InCapturingFrame->ReadbackTexture->GetSizeY() != SourceTexture->GetSizeY())
			{
				MediaState = EMediaCaptureState::Error;
				UMediaOutput* InMediaOutput = nullptr;
				FPlatformAtomics::InterlockedExchangePtr((void**)(&InMediaOutput), MediaOutput);
				UE_LOG(LogMediaIOCore, Error, TEXT("The capture will stop for '%s'. The Source size doesn't match with the user requested size. Requested: %d,%d  Source: %d,%d")
					, InMediaOutput ? *InMediaOutput->GetName() : TEXT("[undefined]")
					, InCapturingFrame->ReadbackTexture->GetSizeX(), InCapturingFrame->ReadbackTexture->GetSizeY()
					, SourceTexture->GetSizeX(), SourceTexture->GetSizeY());
			}
			else if (InCapturingFrame->ReadbackTexture->GetFormat() != SourceTexture->GetFormat())
			{
				MediaState = EMediaCaptureState::Error;
				UMediaOutput* InMediaOutput = nullptr;
				FPlatformAtomics::InterlockedExchangePtr((void**)(&InMediaOutput), MediaOutput);
				UE_LOG(LogMediaIOCore, Error, TEXT("The capture will stop for '%s'. The Source pixel format doesn't match with the user requested pixel format. Requested: %s Source: %s")
					, InMediaOutput ? *InMediaOutput->GetName() : TEXT("[undefined]")
					, GetPixelFormatString(InCapturingFrame->ReadbackTexture->GetFormat())
					, GetPixelFormatString(SourceTexture->GetFormat()));
			}
		}

		if (InCapturingFrame && MediaState != EMediaCaptureState::Error)
		{
			FPooledRenderTargetDesc OutputDesc = FPooledRenderTargetDesc::Create2DDesc(
				FIntPoint(SourceTexture->GetSizeX(), SourceTexture->GetSizeY()),
				SourceTexture->GetFormat(),
				FClearValueBinding::None,
				TexCreate_None,
				TexCreate_RenderTargetable,
				false);
			TRefCountPtr<IPooledRenderTarget> ResampleTexturePooledRenderTarget;
			GetRendererModule().RenderTargetPoolFindFreeElement(RHICmdList, OutputDesc, ResampleTexturePooledRenderTarget, TEXT("MediaCapture"));
			const FSceneRenderTargetItem& DestRenderTarget = ResampleTexturePooledRenderTarget->GetRenderTargetItem();

			// Asynchronously copy target from GPU to GPU
			RHICmdList.CopyToResolveTarget(SourceTexture, DestRenderTarget.TargetableTexture, FResolveParams());

			// Asynchronously copy duplicate target from GPU to System Memory
			RHICmdList.CopyToResolveTarget(DestRenderTarget.TargetableTexture, InCapturingFrame->ReadbackTexture, FResolveParams());

			InCapturingFrame->bResolvedTargetRequested = true;
		}

		if (InReadyFrame && MediaState != EMediaCaptureState::Error)
		{
			check(InReadyFrame->ReadbackTexture.IsValid());

			// Lock & read
			void* ColorDataBuffer = nullptr;
			int32 Width = 0, Height = 0;
			RHICmdList.MapStagingSurface(InReadyFrame->ReadbackTexture, ColorDataBuffer, Width, Height);

			OnFrameCaptured_RenderingThread(InReadyFrame->CaptureBaseData, InReadyFrame->UserData, ColorDataBuffer, Width, Height);
			InReadyFrame->bResolvedTargetRequested = false;

			RHICmdList.UnmapStagingSurface(InReadyFrame->ReadbackTexture);
		}

		bWaitingForResolveCommandExecution = false;
	};


	ENQUEUE_UNIQUE_RENDER_COMMAND_THREEPARAMETER(
		MediaOutputCaptureFrameCreateTexture,
		FCaptureFrame*, InCapturingFrame, CapturingFrame,
		FCaptureFrame*, InPreviousFrame, ReadyFrame,
		decltype(RenderCommand), InRenderCommand, RenderCommand,
		{
			InRenderCommand(RHICmdList, InCapturingFrame, InPreviousFrame);
		});
}

/* namespace MediaCaptureDetails implementation
*****************************************************************************/
namespace MediaCaptureDetails
{
	bool FindSceneViewportAndLevel(TSharedPtr<FSceneViewport>& OutSceneViewport)
	{
#if WITH_EDITOR
		if (GIsEditor)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::PIE)
				{
					UEditorEngine* EditorEngine = CastChecked<UEditorEngine>(GEngine);
					FSlatePlayInEditorInfo& Info = EditorEngine->SlatePlayInEditorMap.FindChecked(Context.ContextHandle);
					if (Info.SlatePlayInEditorWindowViewport.IsValid())
					{
						OutSceneViewport = Info.SlatePlayInEditorWindowViewport;
						return true;
					}
				}
			}
			return false;
		}
		else
#endif
		{
			UGameEngine* GameEngine = CastChecked<UGameEngine>(GEngine);
			OutSceneViewport = GameEngine->SceneViewport;
			return true;
		}
	}

	bool ValidateSceneViewport(const TSharedPtr<FSceneViewport>& SceneViewport, const FIntPoint& DesiredSize, const EPixelFormat DesiredPixelFormat, const bool bCurrentlyCapturing)
	{

		if (!SceneViewport.IsValid())
		{
			UE_LOG(LogMediaIOCore, Error, TEXT("Can not %s the capture. The Scene Viewport is invalid.")
				, bCurrentlyCapturing ? TEXT("continue") : TEXT("start"));
			return false;
		}

		FIntPoint SceneViewportSize = SceneViewport->GetRenderTargetTextureSizeXY();
		if (DesiredSize.X != SceneViewportSize.X || DesiredSize.Y != SceneViewportSize.Y)
		{
			UE_LOG(LogMediaIOCore, Error, TEXT("Can not %s the capture. The Render Target size doesn't match with the requested size. SceneViewport: %d,%d  MediaOutput: %d,%d")
				, bCurrentlyCapturing ? TEXT("continue") : TEXT("start")
				, SceneViewportSize.X, SceneViewportSize.Y
				, DesiredSize.X, DesiredSize.Y);
			return false;
		}

		static const auto CVarDefaultBackBufferPixelFormat = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DefaultBackBufferPixelFormat"));
		EPixelFormat SceneTargetFormat = EDefaultBackBufferPixelFormat::Convert2PixelFormat(EDefaultBackBufferPixelFormat::FromInt(CVarDefaultBackBufferPixelFormat->GetValueOnGameThread()));
		if (DesiredPixelFormat != SceneTargetFormat)
		{
			UE_LOG(LogMediaIOCore, Error, TEXT("Can not %s the capture. The Render Target pixel format doesn't match with the requested pixel format. SceneViewport: %s MediaOutput: %s")
				, bCurrentlyCapturing ? TEXT("continue") : TEXT("start")
				, GetPixelFormatString(SceneTargetFormat)
				, GetPixelFormatString(DesiredPixelFormat));
			return false;
		}

		return true;
	}

	bool ValidateTextureRenderTarget2D(const UTextureRenderTarget2D* InRenderTarget2D, const FIntPoint& DesiredSize, const EPixelFormat DesiredPixelFormat, const bool bCurrentlyCapturing)
	{
		if (InRenderTarget2D == nullptr)
		{
			UE_LOG(LogMediaIOCore, Error, TEXT("Couldn't %s the capture. The Render Target is invalid.")
				, bCurrentlyCapturing ? TEXT("continue") : TEXT("start"));
			return false;
		}

		if (DesiredSize.X != InRenderTarget2D->SizeX || DesiredSize.Y != InRenderTarget2D->SizeY)
		{
			UE_LOG(LogMediaIOCore, Error, TEXT("Can not %s the capture. The Render Target size doesn't match with the requested size. RenderTarget: %d,%d  MediaOutput: %d,%d")
				, bCurrentlyCapturing ? TEXT("continue") : TEXT("start")
				, InRenderTarget2D->SizeX, InRenderTarget2D->SizeY
				, DesiredSize.X, DesiredSize.Y);
			return false;
		}

		if (DesiredPixelFormat != InRenderTarget2D->GetFormat())
		{
			UE_LOG(LogMediaIOCore, Error, TEXT("Can not %s the capture. The Render Target pixel format doesn't match with the requested pixel format. RenderTarget: %s MediaOutput: %s")
				, bCurrentlyCapturing ? TEXT("continue") : TEXT("start")
				, GetPixelFormatString(InRenderTarget2D->GetFormat())
				, GetPixelFormatString(DesiredPixelFormat));
			return false;
		}

		return true;
	}

	bool ValidateIsCapturing(const UMediaCapture& CaptureToBeValidated)
	{
		if (CaptureToBeValidated.GetState() != EMediaCaptureState::Capturing && CaptureToBeValidated.GetState() != EMediaCaptureState::Preparing)
		{
			UE_LOG(LogMediaIOCore, Error, TEXT("Can not update the capture. There is no capture currently.\
			Only use UpdateSceneViewport or UpdateTextureRenderTarget2D when the state is Capturing or Preparing"));
			return false;
		}

		return true;
	}
}