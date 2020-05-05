// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IRenderDocPlugin.h"

#include "RenderDocPluginLoader.h"
#include "RenderDocPluginSettings.h"

#if WITH_EDITOR
#include "Editor/LevelEditor/Public/LevelEditor.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "RenderDocPluginStyle.h"
#include "RenderDocPluginCommands.h"
#include "SRenderDocPluginEditorExtension.h"
#endif

#include "Templates/SharedPointer.h"

DECLARE_LOG_CATEGORY_EXTERN(RenderDocPlugin, Log, All);

class FRenderDocPluginModule : public IRenderDocPlugin
{
public:
	enum class ELaunchAfterCapture
	{
		No,
		Yes,
	};

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	void Tick(float DeltaTime);
	void CaptureFrame(FViewport* Viewport, const FString& DestPath, ELaunchAfterCapture LaunchOption);
	void CaptureFrame() { CaptureFrame(nullptr, FString(), ELaunchAfterCapture::Yes); }
	void CapturePIE(const TArray<FString>& Args);
	void StartRenderDoc(FString CapturePath);
	FString GetNewestCapture();

private:
	virtual TSharedPtr<class IInputDevice> CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override;

	void BeginCapture();
	void EndCapture(void* HWnd, const FString& DestPath, ELaunchAfterCapture LaunchOption);
	void DoCaptureCurrentViewport(FViewport* Viewport, const FString& DestPath, ELaunchAfterCapture LaunchOption);

	/** Injects a debug key bind into the local player so that the hot key works the same in game */
	void InjectDebugExecKeybind();

	bool ShouldCaptureAllActivity() const;

	void ShowNotification(const FText& Message, bool bForceNewNotification);

	/** Bind/Unbind RenderDoc to CaptureInterface for capturing single draw calls or render passes */
	void BeginCaptureBracket(FRHICommandListImmediate* RHICommandList);
	void EndCaptureBracket(FRHICommandListImmediate* RHICommandList);
	void BindCaptureCallbacks();
	void UnBindCaptureCallbacks();

private:
	FRenderDocPluginLoader Loader;
	FRenderDocPluginLoader::RENDERDOC_API_CONTEXT* RenderDocAPI;
	uint64 DelayedCaptureTick; // Tracks on which frame a delayed capture should trigger, if any (when bCaptureDelayInSeconds == false)
	double DelayedCaptureSeconds; // Tracks at which time a delayed capture should trigger, if any (when bCaptureDelayInSeconds == true)
	uint64 CaptureFrameCount; // Tracks how many frames should be captured
	uint64 CaptureEndTick; // Tracks the tick at which the capture currently in progress should end
	bool bCaptureDelayInSeconds:1; // Is the capture delay in seconds or ticks?
	bool bShouldCaptureAllActivity : 1; // true if all the whole frame should be captured, not just the active viewport
	bool bPendingCapture : 1; // true when a delayed capture has been triggered but hasn't started yet
	bool bCaptureInProgress:1; // true after BeginCapture() has been called and we're waiting for the end of the capture

#if WITH_EDITOR
	FRenderDocPluginEditorExtension* EditorExtensions;
	int StartPIEDelayFrames = -1;
#endif
};
