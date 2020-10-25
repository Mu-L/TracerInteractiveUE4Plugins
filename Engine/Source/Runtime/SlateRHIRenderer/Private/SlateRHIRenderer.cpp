// Copyright Epic Games, Inc. All Rights Reserved.

#include "SlateRHIRenderer.h"
#include "Fonts/FontCache.h"
#include "SlateRHIRenderingPolicy.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "EngineGlobals.h"
#include "RendererInterface.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "RHIStaticStates.h"
#include "UnrealEngine.h"
#include "GlobalShader.h"
#include "ScreenRendering.h"
#include "SlateShaders.h"
#include "Rendering/ElementBatcher.h"
#include "StereoRendering.h"
#include "SlateNativeTextureResource.h"
#include "SceneUtils.h"
#include "Runtime/Renderer/Public/VolumeRendering.h"
#include "ShaderCompiler.h"
#include "PipelineStateCache.h"
#include "EngineModule.h"
#include "Interfaces/ISlate3DRenderer.h"
#include "Slate/SlateTextureAtlasInterface.h"
#include "CommonRenderResources.h"
#include "RenderTargetPool.h"
#include "RendererUtils.h"
#include "HAL/LowLevelMemTracker.h"
#include "Rendering/RenderingCommon.h"

DECLARE_CYCLE_STAT(TEXT("Slate RT: Rendering"), STAT_SlateRenderingRTTime, STATGROUP_Slate);

DECLARE_CYCLE_STAT(TEXT("Slate RT: Draw Batches"), STAT_SlateRTDrawBatches, STATGROUP_Slate);

DECLARE_GPU_STAT_NAMED(SlateUI, TEXT("Slate UI"));

// Defines the maximum size that a slate viewport will create
#define MIN_VIEWPORT_SIZE 8
#define MAX_VIEWPORT_SIZE 16384

static TAutoConsoleVariable<float> CVarUILevel(
	TEXT("r.HDR.UI.Level"),
	1.0f,
	TEXT("Luminance level for UI elements when compositing into HDR framebuffer (default: 1.0)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarUICompositeMode(
	TEXT("r.HDR.UI.CompositeMode"),
	1,
	TEXT("Mode used when compositing the UI layer:\n")
	TEXT("0: Standard compositing\n")
	TEXT("1: Shader pass to improve HDR blending\n"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarDrawToVRRenderTarget(
	TEXT("Slate.DrawToVRRenderTarget"),
	1,
	TEXT("If enabled while in VR. Slate UI will be drawn into the render target texture where the VR imagery for either eye was rendered, allow the viewer of the HMD to see the UI (for better or worse.)  This render target will then be cropped/scaled into the back buffer, if mirroring is enabled.  When disabled, Slate UI will be drawn on top of the backbuffer (not to the HMD) after the mirror texture has been cropped/scaled into the backbuffer."),
	ECVF_RenderThreadSafe);

#if WITH_SLATE_VISUALIZERS

TAutoConsoleVariable<int32> CVarShowSlateOverdraw(
	TEXT("Slate.ShowOverdraw"),
	0,
	TEXT("0: Don't show overdraw, 1: Show Overdraw"),
	ECVF_Default
);

TAutoConsoleVariable<int32> CVarShowSlateBatching(
	TEXT("Slate.ShowBatching"),
	0,
	TEXT("0: Don't show batching, 1: Show Batching"),
	ECVF_Default
);
#endif

struct FSlateDrawWindowCommandParams
{
	FSlateRHIRenderer* Renderer;
	FSlateWindowElementList* WindowElementList;
	SWindow* Window;
#if WANTS_DRAW_MESH_EVENTS
	FString WindowTitle;
#endif
	float WorldTimeSeconds;
	float DeltaTimeSeconds;
	float RealTimeSeconds;
	bool bLockToVsync;
	bool bClear;
};

void FViewportInfo::InitRHI()
{
	// Viewport RHI is created on the game thread
	// Create the depth-stencil surface if needed.
	RecreateDepthBuffer_RenderThread();
}

void FViewportInfo::ReleaseRHI()
{
	DepthStencil.SafeRelease();
	ViewportRHI.SafeRelease();
}

void FViewportInfo::ReleaseResource()
{
	FRenderResource::ReleaseResource();
	UITargetRT.SafeRelease();
	UITargetRTMask.SafeRelease();
	HDRSourceRT.SafeRelease();
}

void FViewportInfo::ConditionallyUpdateDepthBuffer(bool bInRequiresStencilTest, uint32 InWidth, uint32 InHeight)
{
	check(IsInRenderingThread());

	bool bDepthStencilStale =
		bInRequiresStencilTest &&
		(!bRequiresStencilTest ||
		(DepthStencil.IsValid() && (DepthStencil->GetSizeX() != InWidth || DepthStencil->GetSizeY() != InHeight)));

	bRequiresStencilTest = bInRequiresStencilTest;

	// Allocate a stencil buffer if needed and not already allocated
	if (bDepthStencilStale)
	{
		RecreateDepthBuffer_RenderThread();
	}
}

void FViewportInfo::RecreateDepthBuffer_RenderThread()
{
	check(IsInRenderingThread());
	DepthStencil.SafeRelease();
	if (bRequiresStencilTest)
	{
		FTexture2DRHIRef ShaderResourceUnused;
		FRHIResourceCreateInfo CreateInfo(FClearValueBinding::DepthZero);
		RHICreateTargetableShaderResource2D(Width, Height, PF_DepthStencil, 1, TexCreate_None, TexCreate_DepthStencilTargetable, false, CreateInfo, DepthStencil, ShaderResourceUnused);
		check(IsValidRef(DepthStencil));
	}
}



FSlateRHIRenderer::FSlateRHIRenderer(TSharedRef<FSlateFontServices> InSlateFontServices, TSharedRef<FSlateRHIResourceManager> InResourceManager)
	: FSlateRenderer(InSlateFontServices)
	, EnqueuedWindowDrawBuffer(NULL)
	, FreeBufferIndex(0)
	, FastPathRenderingDataCleanupList(nullptr)
	, CurrentSceneIndex(-1)
	, ResourceVersion(0)
{
	ResourceManager = InResourceManager;

	ViewMatrix = FMatrix(FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 1, 0),
		FPlane(0, 0, 0, 1));

	bTakingAScreenShot = false;
	OutScreenshotData = NULL;
}

FSlateRHIRenderer::~FSlateRHIRenderer()
{
}

FMatrix FSlateRHIRenderer::CreateProjectionMatrix(uint32 Width, uint32 Height)
{
	// Create ortho projection matrix
	const float Left = 0;
	const float Right = Left + Width;
	const float Top = 0;
	const float Bottom = Top + Height;
	const float ZNear = -100.0f;
	const float ZFar = 100.0f;
	return AdjustProjectionMatrixForRHI(
		FMatrix(
			FPlane(2.0f / (Right - Left), 0, 0, 0),
			FPlane(0, 2.0f / (Top - Bottom), 0, 0),
			FPlane(0, 0, 1 / (ZNear - ZFar), 0),
			FPlane((Left + Right) / (Left - Right), (Top + Bottom) / (Bottom - Top), ZNear / (ZNear - ZFar), 1)
		)
	);
}

bool FSlateRHIRenderer::Initialize()
{
	LoadUsedTextures();

	RenderingPolicy = MakeShareable(new FSlateRHIRenderingPolicy(SlateFontServices.ToSharedRef(), ResourceManager.ToSharedRef()));

	ElementBatcher = MakeUnique<FSlateElementBatcher>(RenderingPolicy.ToSharedRef());

	CurrentSceneIndex = -1;
	ActiveScenes.Empty();
	return true;
}

void FSlateRHIRenderer::Destroy()
{
	RenderingPolicy->ReleaseResources();
	ResourceManager->ReleaseResources();
	SlateFontServices->ReleaseResources();

	for (TMap< const SWindow*, FViewportInfo*>::TIterator It(WindowToViewportInfo); It; ++It)
	{
		BeginReleaseResource(It.Value());
	}

	if (FastPathRenderingDataCleanupList)
	{
		FastPathRenderingDataCleanupList->Cleanup();
		FastPathRenderingDataCleanupList = nullptr;
	}

	FlushRenderingCommands();

	ElementBatcher.Reset();
	RenderingPolicy.Reset();
	ResourceManager.Reset();
	SlateFontServices.Reset();

	DeferredUpdateContexts.Empty();

	for (TMap< const SWindow*, FViewportInfo*>::TIterator It(WindowToViewportInfo); It; ++It)
	{
		FViewportInfo* ViewportInfo = It.Value();
		delete ViewportInfo;
	}

	WindowToViewportInfo.Empty();
	CurrentSceneIndex = -1;
	ActiveScenes.Empty();
}

/** Returns a draw buffer that can be used by Slate windows to draw window elements */
FSlateDrawBuffer& FSlateRHIRenderer::GetDrawBuffer()
{
	FreeBufferIndex = (FreeBufferIndex + 1) % NumDrawBuffers;

	FSlateDrawBuffer* Buffer = &DrawBuffers[FreeBufferIndex];

	while (!Buffer->Lock())
	{
		// If the buffer cannot be locked then the buffer is still in use.  If we are here all buffers are in use
		// so wait until one is free.
		if (IsInSlateThread())
		{
			// We can't flush commands on the slate thread, so simply spinlock until we're done
			// this happens if the render thread becomes completely blocked by expensive tasks when the Slate thread is running
			// in this case we cannot tick Slate.
			FPlatformProcess::Sleep(0.001f);
		}
		else
		{
			FlushCommands();
			UE_LOG(LogSlate, Warning, TEXT("Slate: Had to block on waiting for a draw buffer"));
			FreeBufferIndex = (FreeBufferIndex + 1) % NumDrawBuffers;
		}


		Buffer = &DrawBuffers[FreeBufferIndex];
	}

	// Safely remove brushes by emptying the array and releasing references
	DynamicBrushesToRemove[FreeBufferIndex].Empty();

	Buffer->ClearBuffer();
	Buffer->UpdateResourceVersion(ResourceVersion);
	return *Buffer;
}

void FSlateRHIRenderer::CreateViewport(const TSharedRef<SWindow> Window)
{
	FlushRenderingCommands();

	if (!WindowToViewportInfo.Contains(&Window.Get()))
	{
		const FVector2D WindowSize = Window->GetViewportSize();

		// Clamp the window size to a reasonable default anything below 8 is a d3d warning and 8 is used anyway.
		// @todo Slate: This is a hack to work around menus being summoned with 0,0 for window size until they are ticked.
		int32 Width = FMath::Max(MIN_VIEWPORT_SIZE, FMath::CeilToInt(WindowSize.X));
		int32 Height = FMath::Max(MIN_VIEWPORT_SIZE, FMath::CeilToInt(WindowSize.Y));

		// Sanity check dimensions
		if (!ensureMsgf(Width <= MAX_VIEWPORT_SIZE && Height <= MAX_VIEWPORT_SIZE, TEXT("Invalid window with Width=%u and Height=%u"), Width, Height))
		{
			Width = FMath::Clamp(Width, MIN_VIEWPORT_SIZE, MAX_VIEWPORT_SIZE);
			Height = FMath::Clamp(Height, MIN_VIEWPORT_SIZE, MAX_VIEWPORT_SIZE);
		}


		FViewportInfo* NewInfo = new FViewportInfo();
		// Create Viewport RHI if it doesn't exist (this must be done on the game thread)
		TSharedRef<FGenericWindow> NativeWindow = Window->GetNativeWindow().ToSharedRef();
		NewInfo->OSWindow = NativeWindow->GetOSWindowHandle();
		NewInfo->Width = Width;
		NewInfo->Height = Height;
		NewInfo->DesiredWidth = Width;
		NewInfo->DesiredHeight = Height;
		NewInfo->ProjectionMatrix = CreateProjectionMatrix( Width, Height );
		if (FPlatformMisc::IsStandaloneStereoOnlyDevice())
		{
			NewInfo->PixelFormat = PF_B8G8R8A8;
		}
#if ALPHA_BLENDED_WINDOWS		
		if (Window->GetTransparencySupport() == EWindowTransparency::PerPixel)
		{
			NewInfo->PixelFormat = PF_B8G8R8A8;
		}
#endif

		// SDR format holds the requested format in non HDR mode
		NewInfo->SDRPixelFormat = NewInfo->PixelFormat;
		if (IsHDREnabled())
		{
			NewInfo->PixelFormat = GRHIHDRDisplayOutputFormat;
		}

		// Sanity check dimensions
		checkf(Width <= MAX_VIEWPORT_SIZE && Height <= MAX_VIEWPORT_SIZE, TEXT("Invalid window with Width=%u and Height=%u"), Width, Height);

		bool bFullscreen = IsViewportFullscreen( *Window );
		NewInfo->ViewportRHI = RHICreateViewport( NewInfo->OSWindow, Width, Height, bFullscreen, NewInfo->PixelFormat );
		NewInfo->bFullscreen = bFullscreen;

		// Was the window created on a HDR compatible display?
		NewInfo->bHDREnabled = RHIGetColorSpace(NewInfo->ViewportRHI) != EColorSpaceAndEOTF::ERec709_sRGB ;
		Window->SetIsHDR(NewInfo->bHDREnabled);

		WindowToViewportInfo.Add(&Window.Get(), NewInfo);

		BeginInitResource(NewInfo);
	}
}

void FSlateRHIRenderer::ConditionalResizeViewport(FViewportInfo* ViewInfo, uint32 Width, uint32 Height, bool bFullscreen)
{
	checkSlow(IsThreadSafeForSlateRendering());

	// Force update if HDR output state changes
	static const auto CVarHDRColorGamut = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.HDR.Display.ColorGamut"));
	static const auto CVarHDROutputDevice = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.HDR.Display.OutputDevice"));

	bool bHDREnabled = IsHDREnabled();
	int32 HDRColorGamut = CVarHDRColorGamut ? CVarHDRColorGamut->GetValueOnAnyThread() : 0;
	int32 HDROutputDevice = CVarHDROutputDevice ? CVarHDROutputDevice->GetValueOnAnyThread() : 0;

	bool bHDRStale = ViewInfo && (
		((ViewInfo->PixelFormat == GRHIHDRDisplayOutputFormat) != bHDREnabled)	// HDR toggled
#if PLATFORM_WINDOWS
		|| ((IsRHIDeviceNVIDIA() || IsRHIDeviceAMD()) &&						// Vendor-specific mastering data updates
		((bHDREnabled && ViewInfo->HDRColorGamut != HDRColorGamut)			// Color gamut changed
			|| (bHDREnabled && ViewInfo->HDROutputDevice != HDROutputDevice)))	// Output device changed
#endif
		);

	if (IsInGameThread() && !IsInSlateThread() && ViewInfo && (bHDRStale || ViewInfo->Height != Height || ViewInfo->Width != Width || ViewInfo->bFullscreen != bFullscreen || !IsValidRef(ViewInfo->ViewportRHI)))
	{
		// The viewport size we have doesn't match the requested size of the viewport.
		// Resize it now.

		// Prevent the texture update logic to use the RHI while the viewport is resized. 
		// This could happen if a streaming IO request completes and throws a callback.
		SuspendTextureStreamingRenderTasks();

		// cannot resize the viewport while potentially using it.
		FlushRenderingCommands();

		// Windows are allowed to be zero sized ( sometimes they are animating to/from zero for example)
		// but viewports cannot be zero sized.  Use 8x8 as a reasonably sized viewport in this case.
		uint32 NewWidth = FMath::Max<uint32>(8, Width);
		uint32 NewHeight = FMath::Max<uint32>(8, Height);

		// Sanity check dimensions
		if (NewWidth > MAX_VIEWPORT_SIZE)
		{
			UE_LOG(LogSlate, Warning, TEXT("Tried to set viewport width size to %d.  Clamping size to max allowed size of %d instead."), NewWidth, MAX_VIEWPORT_SIZE);
			NewWidth = MAX_VIEWPORT_SIZE;
		}

		if (NewHeight > MAX_VIEWPORT_SIZE)
		{
			UE_LOG(LogSlate, Warning, TEXT("Tried to set viewport height size to %d.  Clamping size to max allowed size of %d instead."), NewHeight, MAX_VIEWPORT_SIZE);
			NewHeight = MAX_VIEWPORT_SIZE;
		}

		ViewInfo->Width = NewWidth;
		ViewInfo->Height = NewHeight;
		ViewInfo->DesiredWidth = NewWidth;
		ViewInfo->DesiredHeight = NewHeight;
		ViewInfo->ProjectionMatrix = CreateProjectionMatrix(NewWidth, NewHeight);
		ViewInfo->bFullscreen = bFullscreen;

		ViewInfo->PixelFormat = bHDREnabled ? GRHIHDRDisplayOutputFormat : ViewInfo->SDRPixelFormat;
		ViewInfo->HDRColorGamut = HDRColorGamut;
		ViewInfo->HDROutputDevice = HDROutputDevice;

		PreResizeBackBufferDelegate.Broadcast(&ViewInfo->ViewportRHI);
		if (IsValidRef(ViewInfo->ViewportRHI))
		{
			ensureMsgf(ViewInfo->ViewportRHI->GetRefCount() == 1, TEXT("Viewport backbuffer was not properly released"));
			RHIResizeViewport(ViewInfo->ViewportRHI, NewWidth, NewHeight, bFullscreen, ViewInfo->PixelFormat);
		}
		else
		{
			ViewInfo->ViewportRHI = RHICreateViewport(ViewInfo->OSWindow, NewWidth, NewHeight, bFullscreen, ViewInfo->PixelFormat);
		}

		PostResizeBackBufferDelegate.Broadcast(&ViewInfo->ViewportRHI);

		// Reset texture streaming texture updates.
		ResumeTextureStreamingRenderTasks();
	}
}

void FSlateRHIRenderer::UpdateFullscreenState(const TSharedRef<SWindow> Window, uint32 OverrideResX, uint32 OverrideResY)
{
	FViewportInfo* ViewInfo = WindowToViewportInfo.FindRef(&Window.Get());

	if (!ViewInfo)
	{
		CreateViewport(Window);
	}

	ViewInfo = WindowToViewportInfo.FindRef(&Window.Get());

	if (ViewInfo)
	{
		const bool bFullscreen = IsViewportFullscreen(*Window);

		uint32 ResX = OverrideResX ? OverrideResX : GSystemResolution.ResX;
		uint32 ResY = OverrideResY ? OverrideResY : GSystemResolution.ResY;

		bool bIsRenderingStereo = GEngine && GEngine->XRSystem.IsValid() && GEngine->StereoRenderingDevice.IsValid() && GEngine->StereoRenderingDevice->IsStereoEnabled();
		if ((GIsEditor && Window->IsViewportSizeDrivenByWindow()) || (Window->GetWindowMode() == EWindowMode::WindowedFullscreen) || bIsRenderingStereo)
		{
			ResX = ViewInfo->DesiredWidth;
			ResY = ViewInfo->DesiredHeight;
		}

		ConditionalResizeViewport(ViewInfo, ResX, ResY, bFullscreen);
	}
}

void FSlateRHIRenderer::SetSystemResolution(uint32 Width, uint32 Height)
{
	FSystemResolution::RequestResolutionChange(Width, Height, FPlatformProperties::HasFixedResolution() ? EWindowMode::Fullscreen : GSystemResolution.WindowMode);
	IConsoleManager::Get().CallAllConsoleVariableSinks();
}

void FSlateRHIRenderer::RestoreSystemResolution(const TSharedRef<SWindow> InWindow)
{
	if (!GIsEditor && InWindow->GetWindowMode() == EWindowMode::Fullscreen)
	{
		// Force the window system to resize the active viewport, even though nothing might have appeared to change.
		// On windows, DXGI might change the window resolution behind our backs when we alt-tab out. This will make
		// sure that we are actually in the resolution we think we are.
		GSystemResolution.ForceRefresh();
	}
}

/** Called when a window is destroyed to give the renderer a chance to free resources */
void FSlateRHIRenderer::OnWindowDestroyed(const TSharedRef<SWindow>& InWindow)
{
	checkSlow(IsThreadSafeForSlateRendering());

	FViewportInfo** ViewportInfoPtr = WindowToViewportInfo.Find(&InWindow.Get());
	if (ViewportInfoPtr)
	{
		OnSlateWindowDestroyedDelegate.Broadcast(&(*ViewportInfoPtr)->ViewportRHI);

		// Need to flush rendering commands as the viewport may be in use by the render thread
		// and the rendering resources must be released on the render thread before the viewport can be deleted
		FlushRenderingCommands();

		BeginReleaseResource(*ViewportInfoPtr);

		// Need to flush rendering commands as the viewport may be in use by the render thread
		// and the rendering resources must be released on the render thread before the viewport can be deleted
		FlushRenderingCommands(true /* bFlushDeferredDeletes */);

		delete *ViewportInfoPtr;
	}

	WindowToViewportInfo.Remove(&InWindow.Get());
}

/** Called when a window is Finished being Reshaped - Currently need to check if its HDR status has changed */
void FSlateRHIRenderer::OnWindowFinishReshaped(const TSharedPtr<SWindow>& InWindow)
{
	FViewportInfo* ViewInfo = WindowToViewportInfo.FindRef(InWindow.Get());
	RHICheckViewportHDRStatus(ViewInfo->ViewportRHI);
}

// Limited platform support for HDR UI composition
bool SupportsUICompositionRendering(const EShaderPlatform Platform)
{
	return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5) && (RHISupportsGeometryShaders(Platform) || RHISupportsVertexShaderLayer(Platform));
}

// Pixel shader to generate LUT for HDR UI composition
class FCompositeLUTGenerationPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FCompositeLUTGenerationPS, Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return SupportsUICompositionRendering(Parameters.Platform);
	}

	FCompositeLUTGenerationPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		OutputDevice.Bind(Initializer.ParameterMap, TEXT("OutputDevice"));
		OutputGamut.Bind(Initializer.ParameterMap, TEXT("OutputGamut"));
	}
	FCompositeLUTGenerationPS() {}

	void SetParameters(FRHICommandList& RHICmdList)
	{
		static const auto CVarOutputDevice = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.HDR.Display.OutputDevice"));
		static const auto CVarOutputGamut = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.HDR.Display.ColorGamut"));
		static const auto CVarOutputGamma = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.TonemapperGamma"));

		int32 OutputDeviceValue = CVarOutputDevice->GetValueOnRenderThread();
		int32 OutputGamutValue = CVarOutputGamut->GetValueOnRenderThread();
		float Gamma = CVarOutputGamma->GetValueOnRenderThread();

		if (PLATFORM_APPLE && Gamma == 0.0f)
		{
			Gamma = 2.2f;
		}

		if (Gamma > 0.0f)
		{
			// Enforce user-controlled ramp over sRGB or Rec709
			OutputDeviceValue = FMath::Max(OutputDeviceValue, 2);
		}

		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), OutputDevice, OutputDeviceValue);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), OutputGamut, OutputGamutValue);
	}

	static const TCHAR* GetSourceFilename()
	{
		return TEXT("/Engine/Private/CompositeUIPixelShader.usf");
	}

	static const TCHAR* GetFunctionName()
	{
		return TEXT("Main");
	}

private:
	LAYOUT_FIELD(FShaderParameter, OutputDevice);
	LAYOUT_FIELD(FShaderParameter, OutputGamut);
};

IMPLEMENT_SHADER_TYPE(, FCompositeLUTGenerationPS, TEXT("/Engine/Private/CompositeUIPixelShader.usf"), TEXT("GenerateLUTPS"), SF_Pixel);

// Pixel shader to composite UI over HDR buffer
template<uint32 EncodingType>
class FCompositePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FCompositePS, Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return SupportsUICompositionRendering(Parameters.Platform);
	}

	FCompositePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		UITexture.Bind(Initializer.ParameterMap, TEXT("UITexture"));
		UIWriteMaskTexture.Bind(Initializer.ParameterMap, TEXT("UIWriteMaskTexture"));
		UISampler.Bind(Initializer.ParameterMap, TEXT("UISampler"));
		SceneTexture.Bind(Initializer.ParameterMap, TEXT("SceneTexture"));
		SceneSampler.Bind(Initializer.ParameterMap, TEXT("SceneSampler"));
		ColorSpaceLUT.Bind(Initializer.ParameterMap, TEXT("ColorSpaceLUT"));
		ColorSpaceLUTSampler.Bind(Initializer.ParameterMap, TEXT("ColorSpaceLUTSampler"));
		UILevel.Bind(Initializer.ParameterMap, TEXT("UILevel"));
		OutputDevice.Bind(Initializer.ParameterMap, TEXT("OutputDevice"));
	}
	FCompositePS() {}

	void SetParameters(FRHICommandList& RHICmdList, FRHITexture* UITextureRHI, FRHITexture* UITextureWriteMaskRHI, FRHITexture* SceneTextureRHI, FRHITexture* ColorSpaceLUTRHI)
	{
		static const auto CVarOutputDevice = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.HDR.Display.OutputDevice"));

		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), UITexture, UISampler, TStaticSamplerState<SF_Point>::GetRHI(), UITextureRHI);
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), SceneTexture, SceneSampler, TStaticSamplerState<SF_Point>::GetRHI(), SceneTextureRHI);
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), ColorSpaceLUT, ColorSpaceLUTSampler, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), ColorSpaceLUTRHI);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), UILevel, CVarUILevel.GetValueOnRenderThread());
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), OutputDevice, CVarOutputDevice->GetValueOnRenderThread());
		
		if (RHISupportsRenderTargetWriteMask(GMaxRHIShaderPlatform))
		{
			SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), UIWriteMaskTexture, UITextureWriteMaskRHI);
		}
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SCRGB_ENCODING"), EncodingType);
	}

	static const TCHAR* GetSourceFilename()
	{
		return TEXT("/Engine/Private/CompositeUIPixelShader.usf");
	}

	static const TCHAR* GetFunctionName()
	{
		return TEXT("Main");
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, UITexture);
	LAYOUT_FIELD(FShaderResourceParameter, UIWriteMaskTexture);
	LAYOUT_FIELD(FShaderResourceParameter, UISampler);
	LAYOUT_FIELD(FShaderResourceParameter, SceneTexture);
	LAYOUT_FIELD(FShaderResourceParameter, SceneSampler);
	LAYOUT_FIELD(FShaderResourceParameter, ColorSpaceLUT);
	LAYOUT_FIELD(FShaderResourceParameter, ColorSpaceLUTSampler);
	LAYOUT_FIELD(FShaderParameter, UILevel);
	LAYOUT_FIELD(FShaderParameter, OutputDevice);
};

#define SHADER_VARIATION(A) typedef FCompositePS<A> FCompositePS##A; \
	IMPLEMENT_SHADER_TYPE2(FCompositePS##A, SF_Pixel);
SHADER_VARIATION(0)  SHADER_VARIATION(1)
#undef SHADER_VARIATION

int32 SlateWireFrame = 0;
static FAutoConsoleVariableRef CVarSlateWireframe(TEXT("Slate.ShowWireFrame"), SlateWireFrame, TEXT(""), ECVF_Default);

/** Draws windows from a FSlateDrawBuffer on the render thread */
void FSlateRHIRenderer::DrawWindow_RenderThread(FRHICommandListImmediate& RHICmdList, FViewportInfo& ViewportInfo, FSlateWindowElementList& WindowElementList, const struct FSlateDrawWindowCommandParams& DrawCommandParams)
{
	LLM_SCOPE(ELLMTag::SceneRender);
	
	bool bRenderOffscreen = false;	// Render to an offscreen texture which can then be finally color converted at the end.
	
#if WITH_EDITOR
	if (RHIGetColorSpace(ViewportInfo.ViewportRHI) != EColorSpaceAndEOTF::ERec709_sRGB)
	{
		bRenderOffscreen = true;
	}
#endif

	FMemMark MemMark(FMemStack::Get());

	static uint32 LastTimestamp = FPlatformTime::Cycles();
	{
		SCOPED_DRAW_EVENTF(RHICmdList, SlateUI, TEXT("SlateUI Title = %s"), DrawCommandParams.WindowTitle.IsEmpty() ? TEXT("<none>") : *DrawCommandParams.WindowTitle);
		SCOPED_GPU_STAT(RHICmdList, SlateUI);
		SCOPED_NAMED_EVENT_TEXT("Slate::DrawWindow_RenderThread", FColor::Magenta);

		// Should only be called by the rendering thread
		check(IsInRenderingThread());

		FMaterialRenderProxy::UpdateDeferredCachedUniformExpressions();
		GetRendererModule().InitializeSystemTextures(RHICmdList);

		// Optional off-screen UI composition during HDR rendering
		static const auto CVarCompositeMode = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.HDR.UI.CompositeMode"));

		const bool bSupportsUIComposition = GRHISupportsHDROutput && GSupportsVolumeTextureRendering && SupportsUICompositionRendering(GetFeatureLevelShaderPlatform(GMaxRHIFeatureLevel));
		const bool bCompositeUI = bSupportsUIComposition
			&& CVarCompositeMode && CVarCompositeMode->GetValueOnRenderThread() != 0
			&& IsHDREnabled();

		const int32 CompositionLUTSize = 32;

		// Only need to update LUT on settings change
		static const auto CVarHDROutputDevice = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.HDR.Display.OutputDevice"));
		static const auto CVarHDROutputGamut = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.HDR.Display.ColorGamut"));

		const int32 HDROutputDevice = CVarHDROutputDevice ? CVarHDROutputDevice->GetValueOnRenderThread() : 0;
		const int32 HDROutputGamut = CVarHDROutputGamut ? CVarHDROutputGamut->GetValueOnRenderThread() : 0;

		bool bLUTStale = ViewportInfo.ColorSpaceLUTOutputDevice != HDROutputDevice || ViewportInfo.ColorSpaceLUTOutputGamut != HDROutputGamut;

		ViewportInfo.ColorSpaceLUTOutputDevice = HDROutputDevice;
		ViewportInfo.ColorSpaceLUTOutputGamut = HDROutputGamut;

		bool bRenderedStereo = false;
		if (CVarDrawToVRRenderTarget->GetInt() == 0 && GEngine && IsValidRef(ViewportInfo.GetRenderTargetTexture()) && GEngine->StereoRenderingDevice.IsValid())
		{
			const FVector2D WindowSize = WindowElementList.GetWindowSize();
			GEngine->StereoRenderingDevice->RenderTexture_RenderThread(RHICmdList, RHICmdList.GetViewportBackBuffer(ViewportInfo.ViewportRHI), ViewportInfo.GetRenderTargetTexture(), WindowSize);
			bRenderedStereo = true;
		}

		{
			SCOPED_GPU_STAT(RHICmdList, SlateUI);
			SCOPE_CYCLE_COUNTER(STAT_SlateRenderingRTTime);
			CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Slate);

			FSlateBatchData& BatchData = WindowElementList.GetBatchData();

			// Update the vertex and index buffer	
			RenderingPolicy->BuildRenderingBuffers(RHICmdList, BatchData);

			// This must happen after rendering buffers are created
			ViewportInfo.ConditionallyUpdateDepthBuffer(BatchData.IsStencilClippingRequired(), ViewportInfo.DesiredWidth, ViewportInfo.DesiredHeight);

			// should have been created by the game thread
			check(IsValidRef(ViewportInfo.ViewportRHI));

			FTexture2DRHIRef ViewportRT = bRenderedStereo ? nullptr : ViewportInfo.GetRenderTargetTexture();
			FTexture2DRHIRef BackBuffer = (ViewportRT) ? ViewportRT : RHICmdList.GetViewportBackBuffer(ViewportInfo.ViewportRHI);
			FTexture2DRHIRef PostProcessBuffer = BackBuffer;	// If compositing UI then this will be different to the back buffer

			const uint32 ViewportWidth = (ViewportRT) ? ViewportRT->GetSizeX() : ViewportInfo.Width;
			const uint32 ViewportHeight = (ViewportRT) ? ViewportRT->GetSizeY() : ViewportInfo.Height;

			// Check to see that targets are up-to-date
			if (bCompositeUI && (!ViewportInfo.UITargetRT || ViewportInfo.UITargetRT->GetRenderTargetItem().TargetableTexture->GetTexture2D()->GetSizeX() != ViewportWidth || ViewportInfo.UITargetRT->GetRenderTargetItem().TargetableTexture->GetTexture2D()->GetSizeY() != ViewportHeight
				|| !ViewportInfo.HDRSourceRT || ViewportInfo.HDRSourceRT->GetRenderTargetItem().TargetableTexture->GetFormat() != BackBuffer->GetFormat()))
			{
				// Composition buffers
				uint32 BaseFlags = RHISupportsRenderTargetWriteMask(GMaxRHIShaderPlatform) ? TexCreate_NoFastClearFinalize : TexCreate_None;

				FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(ViewportWidth, ViewportHeight),
					PF_B8G8R8A8,
					FClearValueBinding::Transparent,
					BaseFlags,
					TexCreate_ShaderResource | TexCreate_RenderTargetable,
					false,
					1,
					true,
					true));

				GRenderTargetPool.FindFreeElement(RHICmdList, Desc, ViewportInfo.UITargetRT, TEXT("UITargetRT"));

				Desc.Format = BackBuffer->GetFormat();
				GRenderTargetPool.FindFreeElement(RHICmdList, Desc, ViewportInfo.HDRSourceRT, TEXT("HDRSourceRT"));

				// LUT
				ViewportInfo.ColorSpaceLUTRT.SafeRelease();
				ViewportInfo.ColorSpaceLUTSRV.SafeRelease();

				FRHIResourceCreateInfo CreateInfo;
				RHICreateTargetableShaderResource3D(CompositionLUTSize, CompositionLUTSize, CompositionLUTSize, PF_A2B10G10R10, 1, TexCreate_None, TexCreate_RenderTargetable, false, CreateInfo, ViewportInfo.ColorSpaceLUTRT, ViewportInfo.ColorSpaceLUTSRV);
				bLUTStale = true;
			}

			FTexture2DRHIRef FinalBuffer = BackBuffer;

			bool bClear = DrawCommandParams.bClear;
			if (bCompositeUI)
			{
				bClear = true; // Force a clear of the UI buffer to black

				// Grab HDR backbuffer
				FResolveParams ResolveParams;
				RHICmdList.CopyToResolveTarget(FinalBuffer, ViewportInfo.HDRSourceRT->GetRenderTargetItem().TargetableTexture, ResolveParams);

				// UI backbuffer is temp target
				BackBuffer = ViewportInfo.UITargetRT->GetRenderTargetItem().TargetableTexture->GetTexture2D();
			}

#if WITH_EDITOR
			TRefCountPtr<IPooledRenderTarget> HDRRenderRT;

			if (bRenderOffscreen )
			{
				FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(ViewportWidth, ViewportHeight),
					PF_FloatRGBA,
					FClearValueBinding::Transparent,
					TexCreate_None,
					TexCreate_ShaderResource | TexCreate_RenderTargetable,
					false,
					1,
					true,
					true));

				GRenderTargetPool.FindFreeElement(RHICmdList, Desc, HDRRenderRT, TEXT("HDRTargetRT"));

				FResolveParams ResolveParams;
				RHICmdList.CopyToResolveTarget(FinalBuffer, FinalBuffer, ResolveParams);
				BackBuffer = HDRRenderRT->GetRenderTargetItem().TargetableTexture->GetTexture2D();
			}
#endif

			if (SlateWireFrame)
			{
				bClear = true;
			}

			RHICmdList.BeginDrawingViewport(ViewportInfo.ViewportRHI, FTextureRHIRef());
			RHICmdList.SetViewport(0, 0, 0, ViewportWidth, ViewportHeight, 0.0f);
			RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, BackBuffer);

			{
				FRHIRenderPassInfo RPInfo(BackBuffer, ERenderTargetActions::Load_Store);

				if (bClear)
				{
					RPInfo.ColorRenderTargets[0].Action = ERenderTargetActions::Clear_Store;
				}

				if (ViewportInfo.bRequiresStencilTest)
				{
					check(IsValidRef(ViewportInfo.DepthStencil));

					RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(ERenderTargetActions::DontLoad_DontStore, ERenderTargetActions::DontLoad_Store);
					RPInfo.DepthStencilRenderTarget.DepthStencilTarget = ViewportInfo.DepthStencil;
					RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthNop_StencilWrite;
				}

	#if WITH_SLATE_VISUALIZERS
				if (CVarShowSlateBatching.GetValueOnRenderThread() != 0 || CVarShowSlateOverdraw.GetValueOnRenderThread() != 0)
				{
					RPInfo.ColorRenderTargets[0].Action = ERenderTargetActions::Clear_Store;
					if (ViewportInfo.bRequiresStencilTest)
					{
						// Reset the backbuffer as our color render target and also set a depth stencil buffer
						RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(ERenderTargetActions::Load_Store, ERenderTargetActions::Clear_Store);
						RPInfo.DepthStencilRenderTarget.DepthStencilTarget = ViewportInfo.DepthStencil;
						RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthWrite_StencilWrite;
					}
				}
	#endif
				{
					if (BatchData.GetRenderBatches().Num() > 0)
					{
						RHICmdList.BeginRenderPass(RPInfo, TEXT("SlateBatches"));
						SCOPE_CYCLE_COUNTER(STAT_SlateRTDrawBatches);

						FSlateBackBuffer BackBufferTarget(BackBuffer, FIntPoint(ViewportWidth, ViewportHeight));

						FSlateRenderingParams RenderParams(ViewMatrix * ViewportInfo.ProjectionMatrix, DrawCommandParams.WorldTimeSeconds, DrawCommandParams.DeltaTimeSeconds, DrawCommandParams.RealTimeSeconds);
						RenderParams.bWireFrame = !!SlateWireFrame;
						RenderParams.bIsHDR     = ViewportInfo.bHDREnabled;

						FTexture2DRHIRef EmptyTarget;

						RenderingPolicy->DrawElements
						(
							RHICmdList,
							BackBufferTarget,
							BackBuffer,
							PostProcessBuffer,
							ViewportInfo.bRequiresStencilTest ? ViewportInfo.DepthStencil : EmptyTarget,
							BatchData.GetFirstRenderBatchIndex(),
							BatchData.GetRenderBatches(),
							RenderParams
						);
					}
				}

				// @todo Could really use a refactor.
				// Kind of gross but we don't want to restart renderpasses for no reason.
				// If the color deficiency shaders are active within DrawElements there will not be a renderpass here.
				// In the general case there will be a RenderPass active at this point.
				if (RHICmdList.IsInsideRenderPass())
				{
					RHICmdList.EndRenderPass();
				}
			}

			if (bCompositeUI)
			{
				SCOPED_DRAW_EVENT(RHICmdList, SlateUI_Composition);

				static const FName RendererModuleName("Renderer");
				IRendererModule& RendererModule = FModuleManager::GetModuleChecked<IRendererModule>(RendererModuleName);

				const auto FeatureLevel = GMaxRHIFeatureLevel;
				auto ShaderMap = GetGlobalShaderMap(FeatureLevel);

				// Generate composition LUT
				if (bLUTStale)
				{
					// #todo-renderpasses will this touch every pixel? use NoAction?
					FRHIRenderPassInfo RPInfo(ViewportInfo.ColorSpaceLUTRT, ERenderTargetActions::Load_Store);
					RHICmdList.BeginRenderPass(RPInfo, TEXT("GenerateLUT"));
					{
						FGraphicsPipelineStateInitializer GraphicsPSOInit;
						RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
						GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
						GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
						GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

						TShaderMapRef<FWriteToSliceVS> VertexShader(ShaderMap);
						TOptionalShaderMapRef<FWriteToSliceGS> GeometryShader(ShaderMap);
						TShaderMapRef<FCompositeLUTGenerationPS> PixelShader(ShaderMap);
						const FVolumeBounds VolumeBounds(CompositionLUTSize);

						GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GScreenVertexDeclaration.VertexDeclarationRHI;
						GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
						GraphicsPSOInit.BoundShaderState.GeometryShaderRHI = GeometryShader.GetGeometryShader();
#endif
						GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
						GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;
						SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

						VertexShader->SetParameters(RHICmdList, VolumeBounds, FIntVector(VolumeBounds.MaxX - VolumeBounds.MinX));
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
						if (GeometryShader.IsValid())
						{
							GeometryShader->SetParameters(RHICmdList, VolumeBounds.MinZ);
						}
#endif
						PixelShader->SetParameters(RHICmdList);

						RasterizeToVolumeTexture(RHICmdList, VolumeBounds);
					}
					RHICmdList.EndRenderPass();
					RHICmdList.CopyToResolveTarget(ViewportInfo.ColorSpaceLUTRT, ViewportInfo.ColorSpaceLUTSRV, FResolveParams());
				}

				// Composition pass
				{
					FResolveParams ResolveParams;
					RHICmdList.CopyToResolveTarget(ViewportInfo.UITargetRT->GetRenderTargetItem().TargetableTexture, ViewportInfo.UITargetRT->GetRenderTargetItem().TargetableTexture, ResolveParams);

					if (RHISupportsRenderTargetWriteMask(GMaxRHIShaderPlatform))
					{
						IPooledRenderTarget* RenderTargets[] = { ViewportInfo.UITargetRT.GetReference() };
						FRenderTargetWriteMask::Decode<1>(RHICmdList, ShaderMap, RenderTargets, ViewportInfo.UITargetRTMask, 0, TEXT("UIRTWriteMask"));
					}

					RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, FinalBuffer);
					RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, ViewportInfo.HDRSourceRT->GetRenderTargetItem().TargetableTexture);
					FRHIRenderPassInfo RPInfo(FinalBuffer, ERenderTargetActions::Load_Store);
					RHICmdList.BeginRenderPass(RPInfo, TEXT("SlateComposite"));
					{
						FGraphicsPipelineStateInitializer GraphicsPSOInit;
						RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
						GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
						GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
						GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

						TShaderMapRef<FScreenVS> VertexShader(ShaderMap);

						FRHITexture* UITargetRTMaskTexture = RHISupportsRenderTargetWriteMask(GMaxRHIShaderPlatform) ? ViewportInfo.UITargetRTMask->GetRenderTargetItem().TargetableTexture : nullptr;
						if (HDROutputDevice == 5 || HDROutputDevice == 6)
						{
							// ScRGB encoding
							TShaderMapRef<FCompositePS<1>> PixelShader(ShaderMap);

							GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
							GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
							GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
							GraphicsPSOInit.PrimitiveType = PT_TriangleList;

							SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

							PixelShader->SetParameters(RHICmdList, ViewportInfo.UITargetRT->GetRenderTargetItem().TargetableTexture, UITargetRTMaskTexture, ViewportInfo.HDRSourceRT->GetRenderTargetItem().TargetableTexture, ViewportInfo.ColorSpaceLUTSRV);
						}
						else
						{
							// ST2084 (PQ) encoding
							TShaderMapRef<FCompositePS<0>> PixelShader(ShaderMap);

							GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
							GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
							GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
							GraphicsPSOInit.PrimitiveType = PT_TriangleList;

							SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

							PixelShader->SetParameters(RHICmdList, ViewportInfo.UITargetRT->GetRenderTargetItem().TargetableTexture, UITargetRTMaskTexture, ViewportInfo.HDRSourceRT->GetRenderTargetItem().TargetableTexture, ViewportInfo.ColorSpaceLUTSRV);
						}

						RendererModule.DrawRectangle(
							RHICmdList,
							0, 0,
							ViewportWidth, ViewportHeight,
							0, 0,
							ViewportWidth, ViewportHeight,
							FIntPoint(ViewportWidth, ViewportHeight),
							FIntPoint(ViewportWidth, ViewportHeight),
							VertexShader,
							EDRF_UseTriangleOptimization);
					}
					RHICmdList.EndRenderPass();
				}

				// Put the backbuffer back to the correct one.
				BackBuffer = FinalBuffer;
			} //bCompositeUI


#if WITH_EDITOR
			if (bRenderOffscreen)
			{
				const auto FeatureLevel = GMaxRHIFeatureLevel;
				auto ShaderMap = GetGlobalShaderMap(FeatureLevel);

				FRHIRenderPassInfo RPInfo(FinalBuffer, ERenderTargetActions::Load_Store);
				RHICmdList.BeginRenderPass(RPInfo, TEXT("SlateComposite"));

				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
				GraphicsPSOInit.BlendState			= TStaticBlendState<>::GetRHI();
				GraphicsPSOInit.RasterizerState		= TStaticRasterizerState<>::GetRHI();
				GraphicsPSOInit.DepthStencilState	= TStaticDepthStencilState<false, CF_Always>::GetRHI();

				// ST2084 (PQ) encoding
				TShaderMapRef<FHDREditorConvertPS> PixelShader(ShaderMap);
				TShaderMapRef<FScreenVS> VertexShader(ShaderMap);

				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				GraphicsPSOInit.PrimitiveType = PT_TriangleList;

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

				PixelShader->SetParameters(RHICmdList, HDRRenderRT->GetRenderTargetItem().TargetableTexture );

				static const FName RendererModuleName("Renderer");
				IRendererModule& RendererModule = FModuleManager::GetModuleChecked<IRendererModule>(RendererModuleName);

				RendererModule.DrawRectangle(
					RHICmdList,
					0, 0,
					ViewportWidth, ViewportHeight,
					0, 0,
					ViewportWidth, ViewportHeight,
					FIntPoint(ViewportWidth, ViewportHeight),
					FIntPoint(ViewportWidth, ViewportHeight),
					VertexShader,
					EDRF_UseTriangleOptimization);
				
				RHICmdList.EndRenderPass();
				BackBuffer = FinalBuffer;
			}
#endif
			if (!bRenderedStereo && GEngine && IsValidRef(ViewportInfo.GetRenderTargetTexture()) && GEngine->StereoRenderingDevice.IsValid())
			{
				const FVector2D WindowSize = WindowElementList.GetWindowSize();
				GEngine->StereoRenderingDevice->RenderTexture_RenderThread(RHICmdList, RHICmdList.GetViewportBackBuffer(ViewportInfo.ViewportRHI), ViewportInfo.GetRenderTargetTexture(), WindowSize);
			}
			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, BackBuffer);

			// Fire delegate to inform bound functions the back buffer is ready to be captured.
			OnBackBufferReadyToPresentDelegate.Broadcast(*DrawCommandParams.Window, BackBuffer);
		}
	}

	if (bTakingAScreenShot)
	{
		// take screenshot before swapbuffer
		FTexture2DRHIRef BackBuffer = RHICmdList.GetViewportBackBuffer(ViewportInfo.ViewportRHI);

		// Sanity check to make sure the user specified a valid screenshot rect.
		FIntRect ClampedScreenshotRect;
		ClampedScreenshotRect.Min = ScreenshotRect.Min;
		ClampedScreenshotRect.Max = ScreenshotRect.Max.ComponentMin(BackBuffer->GetSizeXY());
		ClampedScreenshotRect.Max = ScreenshotRect.Min.ComponentMax(ClampedScreenshotRect.Max);

		if (ClampedScreenshotRect != ScreenshotRect)
		{
			UE_LOG(LogSlate, Warning, TEXT("Slate: Screenshot rect max coordinate had to be clamped from [%d, %d] to [%d, %d]"), ScreenshotRect.Max.X, ScreenshotRect.Max.Y, ClampedScreenshotRect.Max.X, ClampedScreenshotRect.Max.Y);
		}

		if (!ClampedScreenshotRect.IsEmpty())
		{
			RHICmdList.ReadSurfaceData(BackBuffer, ClampedScreenshotRect, *OutScreenshotData, FReadSurfaceDataFlags());
		}
		else
		{
			UE_LOG(LogSlate, Warning, TEXT("Slate: Screenshot rect was empty! Skipping readback of back buffer."));
		}

		bTakingAScreenShot = false;
		OutScreenshotData = NULL;
	}

	// Calculate renderthread time (excluding idle time).	
	uint32 StartTime = FPlatformTime::Cycles();

	RHICmdList.EndDrawingViewport(ViewportInfo.ViewportRHI, true, DrawCommandParams.bLockToVsync);

	uint32 EndTime = FPlatformTime::Cycles();

	GSwapBufferTime = EndTime - StartTime;
	SET_CYCLE_COUNTER(STAT_PresentTime, GSwapBufferTime);

	uint32 ThreadTime = EndTime - LastTimestamp;
	LastTimestamp = EndTime;

	uint32 RenderThreadIdle = 0;

	FThreadIdleStats& RenderThread = FThreadIdleStats::Get();
	GRenderThreadIdle[ERenderThreadIdleTypes::WaitingForAllOtherSleep] = RenderThread.Waits;
	GRenderThreadIdle[ERenderThreadIdleTypes::WaitingForGPUPresent] += GSwapBufferTime;
	GRenderThreadNumIdle[ERenderThreadIdleTypes::WaitingForGPUPresent]++;
	RenderThread.Waits = 0;

	SET_CYCLE_COUNTER(STAT_RenderingIdleTime_RenderThreadSleepTime, GRenderThreadIdle[0]);
	SET_CYCLE_COUNTER(STAT_RenderingIdleTime_WaitingForGPUQuery, GRenderThreadIdle[1]);
	SET_CYCLE_COUNTER(STAT_RenderingIdleTime_WaitingForGPUPresent, GRenderThreadIdle[2]);

	for (int32 Index = 0; Index < ERenderThreadIdleTypes::Num; Index++)
	{
		RenderThreadIdle += GRenderThreadIdle[Index];
		GRenderThreadIdle[Index] = 0;
		GRenderThreadNumIdle[Index] = 0;
	}

	SET_CYCLE_COUNTER(STAT_RenderingIdleTime, RenderThreadIdle);
	GRenderThreadTime = (ThreadTime > RenderThreadIdle) ? (ThreadTime - RenderThreadIdle) : ThreadTime;
	
	if (IsRunningRHIInSeparateThread())
	{
		RHICmdList.EnqueueLambda([](FRHICommandListImmediate&)
		{
			// Restart the RHI thread timer, so we don't count time spent in Present twice when this command list finishes.
			uint32 ThisCycles = FPlatformTime::Cycles();
			GWorkingRHIThreadTime += (ThisCycles - GWorkingRHIThreadStartCycles);
			GWorkingRHIThreadStartCycles = ThisCycles;

			uint32 NewVal = GWorkingRHIThreadTime - GWorkingRHIThreadStallTime;
			FPlatformAtomics::AtomicStore((int32*)&GRHIThreadTime, (int32)NewVal);
			GWorkingRHIThreadTime = 0;
			GWorkingRHIThreadStallTime = 0;
		});
	}
}

void FSlateRHIRenderer::DrawWindows(FSlateDrawBuffer& WindowDrawBuffer)
{
	DrawWindows_Private(WindowDrawBuffer);
}


void FSlateRHIRenderer::PrepareToTakeScreenshot(const FIntRect& Rect, TArray<FColor>* OutColorData)
{
	check(OutColorData);

	bTakingAScreenShot = true;
	ScreenshotRect = Rect;
	OutScreenshotData = OutColorData;
}

/**
* Creates necessary resources to render a window and sends draw commands to the rendering thread
*
* @param WindowDrawBuffer	The buffer containing elements to draw
*/
void FSlateRHIRenderer::DrawWindows_Private(FSlateDrawBuffer& WindowDrawBuffer)
{
	checkSlow(IsThreadSafeForSlateRendering());


	FSlateRHIRenderingPolicy* Policy = RenderingPolicy.Get();
	ENQUEUE_RENDER_COMMAND(SlateBeginDrawingWindowsCommand)(
		[Policy](FRHICommandListImmediate& RHICmdList)
	{
		Policy->BeginDrawingWindows();
	}
	);

	// Update texture atlases if needed and safe
	if (DoesThreadOwnSlateRendering())
	{
		ResourceManager->UpdateTextureAtlases();
	}

	const TSharedRef<FSlateFontCache> FontCache = SlateFontServices->GetFontCache();

	// Iterate through each element list and set up an RHI window for it if needed
	const TArray<TSharedRef<FSlateWindowElementList>>& WindowElementLists = WindowDrawBuffer.GetWindowElementLists();
	for (int32 ListIndex = 0; ListIndex < WindowElementLists.Num(); ++ListIndex)
	{
		FSlateWindowElementList& ElementList = *WindowElementLists[ListIndex];

		SWindow* Window = ElementList.GetRenderWindow();

		if (Window)
		{
			const FVector2D WindowSize = Window->GetViewportSize();
			if (WindowSize.X > 0 && WindowSize.Y > 0)
			{
				// Add all elements for this window to the element batcher
				ElementBatcher->AddElements(ElementList);

				// Update the font cache with new text after elements are batched
				FontCache->UpdateCache();

				bool bLockToVsync = ElementBatcher->RequiresVsync();

				bool bForceVsyncFromCVar = false;
				if (GIsEditor)
				{
					static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSyncEditor"));
					bForceVsyncFromCVar = (CVar->GetInt() != 0);
				}
				else
				{
					static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"));
					bForceVsyncFromCVar = (CVar->GetInt() != 0);
				}

				bLockToVsync |= bForceVsyncFromCVar;

				// All elements for this window have been batched and rendering data updated
				ElementBatcher->ResetBatches();

				// The viewport had better exist at this point  
				FViewportInfo* ViewInfo = WindowToViewportInfo.FindChecked(Window);

				// Cache off the HDR status
				ViewInfo->bHDREnabled = RHIGetColorSpace(ViewInfo->ViewportRHI) != EColorSpaceAndEOTF::ERec709_sRGB;
				Window->SetIsHDR(ViewInfo->bHDREnabled);

				if (Window->IsViewportSizeDrivenByWindow())
				{
					// Resize the viewport if needed
					ConditionalResizeViewport(ViewInfo, ViewInfo->DesiredWidth, ViewInfo->DesiredHeight, IsViewportFullscreen(*Window));
				}

				// Tell the rendering thread to draw the windows
				{
					FSlateDrawWindowCommandParams Params;

					Params.Renderer = this;
					Params.WindowElementList = &ElementList;
					Params.Window = Window;
#if WANTS_DRAW_MESH_EVENTS
					Params.WindowTitle = Window->GetTitle().ToString();
#endif
					Params.bLockToVsync = bLockToVsync;
#if ALPHA_BLENDED_WINDOWS
					Params.bClear = Window->GetTransparencySupport() == EWindowTransparency::PerPixel;
#else
					Params.bClear = false;
#endif	
					Params.WorldTimeSeconds = FApp::GetCurrentTime() - GStartTime;
					Params.DeltaTimeSeconds = FApp::GetDeltaTime();
					Params.RealTimeSeconds = FPlatformTime::Seconds() - GStartTime;

					// Skip the actual draw if we're in a headless execution environment
					bool bLocalTakingAScreenShot = bTakingAScreenShot;
					if (GIsClient && !IsRunningCommandlet() && !GUsingNullRHI)
					{
						ENQUEUE_RENDER_COMMAND(SlateDrawWindowsCommand)(
							[Params, ViewInfo](FRHICommandListImmediate& RHICmdList)
							{
								Params.Renderer->DrawWindow_RenderThread(RHICmdList, *ViewInfo, *Params.WindowElementList, Params);
							}
						);
					}

					SlateWindowRendered.Broadcast(*Window, &ViewInfo->ViewportRHI);

					if (bLocalTakingAScreenShot)
					{
						// immediately flush the RHI command list
						FlushRenderingCommands();
					}
				}
			}
		}
		else
		{
			ensureMsgf(false, TEXT("Window isnt valid but being drawn!"));
		}
	}

	FSlateDrawBuffer* DrawBuffer = &WindowDrawBuffer;
	ENQUEUE_RENDER_COMMAND(SlateEndDrawingWindowsCommand)(
		[DrawBuffer, Policy](FRHICommandListImmediate& RHICmdList)
	{
		FSlateEndDrawingWindowsCommand::EndDrawingWindows(RHICmdList, DrawBuffer, *Policy);
	}
	);

	if (DeferredUpdateContexts.Num() > 0)
	{
		// Intentionally copy the contexts to avoid contention with the game thread
		ENQUEUE_RENDER_COMMAND(DrawWidgetRendererImmediate)(
			[Contexts = DeferredUpdateContexts](FRHICommandListImmediate& RHICmdList) mutable
			{
				for (const FRenderThreadUpdateContext& Context : Contexts)
				{
					Context.Renderer->DrawWindowToTarget_RenderThread(RHICmdList, Context);
				}
			}
		);

		DeferredUpdateContexts.Empty();
	}

	if (FastPathRenderingDataCleanupList)
	{
		FastPathRenderingDataCleanupList->Cleanup();
		FastPathRenderingDataCleanupList = nullptr;
	}

	// flush the cache if needed
	FontCache->ConditionalFlushCache();
}


FIntPoint FSlateRHIRenderer::GenerateDynamicImageResource(const FName InTextureName)
{
	check(IsInGameThread());

	uint32 Width = 0;
	uint32 Height = 0;
	TArray<uint8> RawData;

	TSharedPtr<FSlateDynamicTextureResource> TextureResource = ResourceManager->GetDynamicTextureResourceByName(InTextureName);
	if (!TextureResource.IsValid())
	{
		// Load the image from disk
		bool bSucceeded = ResourceManager->LoadTexture(InTextureName, InTextureName.ToString(), Width, Height, RawData);
		if (bSucceeded)
		{
			TextureResource = ResourceManager->MakeDynamicTextureResource(InTextureName, Width, Height, RawData);
		}
	}

	return TextureResource.IsValid() ? TextureResource->Proxy->ActualSize : FIntPoint(0, 0);
}

bool FSlateRHIRenderer::GenerateDynamicImageResource(FName ResourceName, uint32 Width, uint32 Height, const TArray< uint8 >& Bytes)
{
	check(IsInGameThread());

	TSharedPtr<FSlateDynamicTextureResource> TextureResource = ResourceManager->GetDynamicTextureResourceByName(ResourceName);
	if (!TextureResource.IsValid())
	{
		TextureResource = ResourceManager->MakeDynamicTextureResource(ResourceName, Width, Height, Bytes);
	}
	return TextureResource.IsValid();
}

bool FSlateRHIRenderer::GenerateDynamicImageResource(FName ResourceName, FSlateTextureDataRef TextureData)
{
	check(IsInGameThread());

	TSharedPtr<FSlateDynamicTextureResource> TextureResource = ResourceManager->GetDynamicTextureResourceByName(ResourceName);
	if (!TextureResource.IsValid())
	{
		TextureResource = ResourceManager->MakeDynamicTextureResource(ResourceName, TextureData);
	}
	return TextureResource.IsValid();
}

FSlateResourceHandle FSlateRHIRenderer::GetResourceHandle( const FSlateBrush& Brush )
{
	return ResourceManager->GetResourceHandle( Brush );
}

bool FSlateRHIRenderer::CanRenderResource(UObject& InResourceObject) const
{
	return Cast<UTexture>(&InResourceObject) || Cast<ISlateTextureAtlasInterface>(&InResourceObject) || Cast<UMaterialInterface>(&InResourceObject);
}

void FSlateRHIRenderer::RemoveDynamicBrushResource( TSharedPtr<FSlateDynamicImageBrush> BrushToRemove )
{
	if (BrushToRemove.IsValid())
	{
		DynamicBrushesToRemove[FreeBufferIndex].Add(BrushToRemove);
	}
}

/**
* Gives the renderer a chance to wait for any render commands to be completed before returning/
*/
void FSlateRHIRenderer::FlushCommands() const
{
	if (IsInGameThread() || IsInSlateThread())
	{
		FlushRenderingCommands();
	}
}

/**
* Gives the renderer a chance to synchronize with another thread in the event that the renderer runs
* in a multi-threaded environment.  This function does not return until the sync is complete
*/
void FSlateRHIRenderer::Sync() const
{
	// Sync game and render thread. Either total sync or allowing one frame lag.
	static FFrameEndSync FrameEndSync;
	static auto CVarAllowOneFrameThreadLag = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.OneFrameThreadLag"));
	FrameEndSync.Sync(CVarAllowOneFrameThreadLag->GetValueOnAnyThread() != 0);
}

/**
 * Inline issues a BeginFrame to the RHI.
 * This is to handle cases like Modal dialogs in the UI. The game loop stops while
 * the dialog is open but continues to issue draws. The RHI thinks there are all part of one super long
 * frame until the Modal window is closed.
 */
void FSlateRHIRenderer::BeginFrame() const
{
	ENQUEUE_RENDER_COMMAND(SlateRHIBeginFrame)(
	   [](FRHICommandListImmediate& RHICmdList)
	   {
		   RHICmdList.BeginFrame();
	   }
	);
}

void FSlateRHIRenderer::EndFrame() const
{
	ENQUEUE_RENDER_COMMAND(SlateRHIEndFrame)(
	   [](FRHICommandListImmediate& RHICmdList)
	   {
		   RHICmdList.EndFrame();
	   }
	);
}

void FSlateRHIRenderer::ReloadTextureResources()
{
	ResourceManager->ReloadTextures();
}

void FSlateRHIRenderer::LoadUsedTextures()
{
	if (ResourceManager.IsValid())
	{
		ResourceManager->LoadUsedTextures();
	}
}

void FSlateRHIRenderer::LoadStyleResources(const ISlateStyle& Style)
{
	if (ResourceManager.IsValid())
	{
		ResourceManager->LoadStyleResources(Style);
	}
}

void FSlateRHIRenderer::ReleaseDynamicResource(const FSlateBrush& InBrush)
{
	ensure(IsInGameThread());
	ResourceManager->ReleaseDynamicResource(InBrush);
}

void* FSlateRHIRenderer::GetViewportResource(const SWindow& Window)
{
	checkSlow(IsThreadSafeForSlateRendering());

	FViewportInfo** InfoPtr = WindowToViewportInfo.Find(&Window);

	if (InfoPtr)
	{
		FViewportInfo* ViewportInfo = *InfoPtr;

		// Create the viewport if it doesnt exist
		if (!IsValidRef(ViewportInfo->ViewportRHI))
		{
			// Sanity check dimensions
			checkf(ViewportInfo->Width <= MAX_VIEWPORT_SIZE && ViewportInfo->Height <= MAX_VIEWPORT_SIZE, TEXT("Invalid window with Width=%u and Height=%u"), ViewportInfo->Width, ViewportInfo->Height);

			const bool bFullscreen = IsViewportFullscreen(Window);

			ViewportInfo->ViewportRHI = RHICreateViewport(ViewportInfo->OSWindow, ViewportInfo->Width, ViewportInfo->Height, bFullscreen, ViewportInfo->PixelFormat);
		}

		return &ViewportInfo->ViewportRHI;
	}
	else
	{
		return NULL;
	}
}

void FSlateRHIRenderer::SetColorVisionDeficiencyType(EColorVisionDeficiency Type, int32 Severity, bool bCorrectDeficiency, bool bShowCorrectionWithDeficiency)
{
	GSlateColorDeficiencyType = Type;
	GSlateColorDeficiencySeverity = FMath::Clamp(Severity, 0, 10);
	GSlateColorDeficiencyCorrection = bCorrectDeficiency;
	GSlateShowColorDeficiencyCorrectionWithDeficiency = bShowCorrectionWithDeficiency;
}

FSlateUpdatableTexture* FSlateRHIRenderer::CreateUpdatableTexture(uint32 Width, uint32 Height)
{
	const bool bCreateEmptyTexture = true;
	FSlateTexture2DRHIRef* NewTexture = new FSlateTexture2DRHIRef(Width, Height, PF_B8G8R8A8, nullptr, TexCreate_Dynamic, bCreateEmptyTexture);
	if (IsInRenderingThread())
	{
		NewTexture->InitResource();
	}
	else
	{
		BeginInitResource(NewTexture);
	}
	return NewTexture;
}

void FSlateRHIRenderer::ReleaseUpdatableTexture(FSlateUpdatableTexture* Texture)
{
	if (IsInRenderingThread())
	{
		Texture->GetRenderResource()->ReleaseResource();
		delete Texture;
	}
	else
	{
		Texture->Cleanup();
	}
}

ISlateAtlasProvider* FSlateRHIRenderer::GetTextureAtlasProvider()
{
	if (ResourceManager.IsValid())
	{
		return ResourceManager->GetTextureAtlasProvider();
	}

	return nullptr;
}



int32 FSlateRHIRenderer::RegisterCurrentScene(FSceneInterface* Scene)
{
	check(IsInGameThread());
	if (Scene && Scene->GetWorld())
	{
		// We only want one scene view per world, (todo per player for split screen)
		CurrentSceneIndex = ActiveScenes.IndexOfByPredicate([&Scene](const FSceneInterface* TestScene) { return TestScene->GetWorld() == Scene->GetWorld(); });
		if (CurrentSceneIndex == INDEX_NONE)
		{
			CurrentSceneIndex = ActiveScenes.Add(Scene);

			// We need to keep the ActiveScenes array synchronized with the Policy's ActiveScenes array on
			// the render thread.
			FSlateRHIRenderingPolicy* InRenderPolicy = RenderingPolicy.Get();
			int32 LocalCurrentSceneIndex = CurrentSceneIndex;
			ENQUEUE_RENDER_COMMAND(RegisterCurrentSceneOnPolicy)(
				[InRenderPolicy, Scene, LocalCurrentSceneIndex](FRHICommandListImmediate& RHICmdList)
			{
				if (LocalCurrentSceneIndex != -1)
				{
					InRenderPolicy->AddSceneAt(Scene, LocalCurrentSceneIndex);
				}
			}
			);
		}
	}
	else
	{
		CurrentSceneIndex = -1;
	}

	return CurrentSceneIndex;
}

int32 FSlateRHIRenderer::GetCurrentSceneIndex() const
{
	return CurrentSceneIndex;
}

void FSlateRHIRenderer::ClearScenes()
{
	if (!IsInSlateThread())
	{
		CurrentSceneIndex = -1;
		ActiveScenes.Empty();

		// We need to keep the ActiveScenes array synchronized with the Policy's ActiveScenes array on
		// the render thread.
		FSlateRenderingPolicy* InRenderPolicy = RenderingPolicy.Get();
		ENQUEUE_RENDER_COMMAND(ClearScenesOnPolicy)(
			[InRenderPolicy](FRHICommandListImmediate& RHICmdList)
		{
			InRenderPolicy->ClearScenes();
		}
		);
	}
}


FRHICOMMAND_MACRO(FClearCachedRenderingDataCommand)
{
public:
	FClearCachedRenderingDataCommand(FSlateCachedFastPathRenderingData* InCachedRenderingData)
		: CachedRenderingData(InCachedRenderingData)
	{
		
	}

	void Execute(FRHICommandListBase& CmdList)
	{
		delete CachedRenderingData;
	}

private:
	FSlateCachedFastPathRenderingData* CachedRenderingData;
};

FRHICOMMAND_MACRO(FClearCachedElementDataCommand)
{
public:
	FClearCachedElementDataCommand(FSlateCachedElementData* InCachedElementData)
		: CachedElementData(InCachedElementData)
	{

	}

	void Execute(FRHICommandListBase& CmdList)
	{
		delete CachedElementData;
	}

private:
	FSlateCachedElementData* CachedElementData;
};

void FSlateRHIRenderer::DestroyCachedFastPathRenderingData(FSlateCachedFastPathRenderingData* CachedRenderingData)
{
	check(CachedRenderingData);

	if (!FastPathRenderingDataCleanupList)
	{
		// This will be deleted later on the rendering thread
		FastPathRenderingDataCleanupList = new FFastPathRenderingDataCleanupList;
	}

	FastPathRenderingDataCleanupList->FastPathRenderingDataToRemove.Add(CachedRenderingData);
}

void FSlateRHIRenderer::DestroyCachedFastPathElementData(FSlateCachedElementData* CachedElementData)
{
	check(CachedElementData);

	// Cached data should be destroyed in a thread safe way.  If there is an rhi thread it could be reading from the data to copy it into a vertex buffer
	// so delete it on the rhi thread if necessary, otherwise delete it on the render thread
	ENQUEUE_RENDER_COMMAND(ClearCachedElementData)(
		[CachedElementData](FRHICommandListImmediate& RHICmdList)
	{
		if (!RHICmdList.Bypass())
		{
			new (RHICmdList.AllocCommand<FClearCachedElementDataCommand>()) FClearCachedElementDataCommand(CachedElementData);
		}
		else
		{
			FClearCachedElementDataCommand Cmd(CachedElementData);
			Cmd.Execute(RHICmdList);
		}
	});
}

bool FSlateRHIRenderer::AreShadersInitialized() const
{
#if WITH_EDITORONLY_DATA
	return IsGlobalShaderMapComplete(TEXT("SlateElement"));
#else
	return true;
#endif
}

void FSlateRHIRenderer::InvalidateAllViewports()
{
	for (TMap< const SWindow*, FViewportInfo*>::TIterator It(WindowToViewportInfo); It; ++It)
	{
		It.Value()->ViewportRHI = nullptr;
	}
}

FCriticalSection* FSlateRHIRenderer::GetResourceCriticalSection()
{
	return ResourceManager->GetResourceCriticalSection();
}

void FSlateRHIRenderer::ReleaseAccessedResources(bool bImmediatelyFlush)
{
	// We keep track of the Scene objects from SceneViewports on the SlateRenderer. Make sure that this gets refreshed every frame.
	ClearScenes();

	if (bImmediatelyFlush)
	{
		// Increment resource version to allow buffers to shrink or cached structures to clean up.
		ResourceVersion++;

		// Release resources generated specifically by the rendering policy if we are flushing.
		// This should NOT be done unless flushing
		RenderingPolicy->FlushGeneratedResources();

		//FlushCommands();
	}
}

void FSlateRHIRenderer::RequestResize(const TSharedPtr<SWindow>& Window, uint32 NewWidth, uint32 NewHeight)
{
	checkSlow(IsThreadSafeForSlateRendering());

	FViewportInfo* ViewInfo = WindowToViewportInfo.FindRef(Window.Get());

	if (ViewInfo)
	{
		ViewInfo->DesiredWidth = NewWidth;
		ViewInfo->DesiredHeight = NewHeight;
	}
}

void FSlateRHIRenderer::SetWindowRenderTarget(const SWindow& Window, IViewportRenderTargetProvider* Provider)
{
	FViewportInfo* ViewInfo = WindowToViewportInfo.FindRef(&Window);
	if (ViewInfo)
	{
		ViewInfo->RTProvider = Provider;
	}
}

void FSlateRHIRenderer::AddWidgetRendererUpdate(const struct FRenderThreadUpdateContext& Context, bool bDeferredRenderTargetUpdate)
{
	if (bDeferredRenderTargetUpdate)
	{
		DeferredUpdateContexts.Add(Context);
	}
	else
	{
		// Enqueue a command to unlock the draw buffer after all windows have been drawn
		FRenderThreadUpdateContext InContext = Context;
		ENQUEUE_RENDER_COMMAND(DrawWidgetRendererImmediate)(
			[InContext](FRHICommandListImmediate& RHICmdList)
			{
				InContext.Renderer->DrawWindowToTarget_RenderThread(RHICmdList, InContext);
			});
	}
}

FSlateEndDrawingWindowsCommand::FSlateEndDrawingWindowsCommand(FSlateRHIRenderingPolicy& InPolicy, FSlateDrawBuffer* InDrawBuffer)
	: Policy(InPolicy)
	, DrawBuffer(InDrawBuffer)
{}

void FSlateEndDrawingWindowsCommand::Execute(FRHICommandListBase& CmdList)
{
	DrawBuffer->Unlock();
	Policy.EndDrawingWindows();
}

void FSlateEndDrawingWindowsCommand::EndDrawingWindows(FRHICommandListImmediate& RHICmdList, FSlateDrawBuffer* DrawBuffer, FSlateRHIRenderingPolicy& Policy)
{
	if (!RHICmdList.Bypass())
	{
		ALLOC_COMMAND_CL(RHICmdList, FSlateEndDrawingWindowsCommand)(Policy, DrawBuffer);
	}
	else
	{
		FSlateEndDrawingWindowsCommand Cmd(Policy, DrawBuffer);
		Cmd.Execute(RHICmdList);
	}
}


struct FClearCachedRenderingDataCommand2 final : public FRHICommand < FClearCachedRenderingDataCommand2 >
{
public:
	FClearCachedRenderingDataCommand2(FFastPathRenderingDataCleanupList* InCleanupList)
		: CleanupList(InCleanupList)
	{

	}

	void Execute(FRHICommandListBase& CmdList)
	{
		delete CleanupList;
	}

private:
	FFastPathRenderingDataCleanupList* CleanupList;
};


FFastPathRenderingDataCleanupList::~FFastPathRenderingDataCleanupList()
{
	for (FSlateCachedFastPathRenderingData* Data : FastPathRenderingDataToRemove)
	{
		delete Data;
	}
}

void FFastPathRenderingDataCleanupList::Cleanup()
{
	// Cached data should be destroyed in a thread safe way.  If there is an rhi thread it could be reading from the data to copy it into a vertex buffer
	// so delete it on the rhi thread if necessary, otherwise delete it on the render thread
	ENQUEUE_RENDER_COMMAND(ClearCachedRenderingData)(
		[CleanupList = this](FRHICommandListImmediate& RHICmdList)
	{
		if (!RHICmdList.Bypass())
		{
			new (RHICmdList.AllocCommand<FClearCachedRenderingDataCommand2>()) FClearCachedRenderingDataCommand2(CleanupList);
		}
		else
		{
			FClearCachedRenderingDataCommand2 Cmd(CleanupList);
			Cmd.Execute(RHICmdList);
		}
	});
}
