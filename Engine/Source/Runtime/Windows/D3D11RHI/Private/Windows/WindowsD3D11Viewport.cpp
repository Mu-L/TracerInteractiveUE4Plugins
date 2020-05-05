// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D11Viewport.cpp: D3D viewport RHI implementation.
=============================================================================*/

#include "D3D11RHIPrivate.h"
#include "RenderCore.h"
#include "Misc/CommandLine.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include <dwmapi.h>

THIRD_PARTY_INCLUDES_START
#include "dxgi1_6.h"
THIRD_PARTY_INCLUDES_END

#ifndef DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
#define DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING  2048
#endif

static bool GSwapFlagsInitialized = false;
static DXGI_SWAP_EFFECT GSwapEffect = DXGI_SWAP_EFFECT_DISCARD;
static uint32 GSwapChainFlags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
static uint32 GSwapChainBufferCount = 1;

uint32 D3D11GetSwapChainFlags()
{
	return GSwapChainFlags;
}

static int32 GD3D11UseAllowTearing = 1;
static FAutoConsoleVariableRef CVarD3DUseAllowTearing(
	TEXT("r.D3D11.UseAllowTearing"),
	GD3D11UseAllowTearing,
	TEXT("Enable new dxgi flip mode with d3d11"),
	ECVF_RenderThreadSafe| ECVF_ReadOnly
);


FD3D11Viewport::FD3D11Viewport(FD3D11DynamicRHI* InD3DRHI,HWND InWindowHandle,uint32 InSizeX,uint32 InSizeY,bool bInIsFullscreen, EPixelFormat InPreferredPixelFormat):
	D3DRHI(InD3DRHI),
	LastFlipTime(0),
	LastFrameComplete(0),
	LastCompleteTime(0),
	SyncCounter(0),
	bSyncedLastFrame(false),
	WindowHandle(InWindowHandle),
	MaximumFrameLatency(3),
	SizeX(InSizeX),
	SizeY(InSizeY),
	PresentFailCount(0),
	ValidState(0),
	PixelFormat(InPreferredPixelFormat),
	PixelColorSpace(EColorSpaceAndEOTF::ERec709_sRGB),
	bIsFullscreen(bInIsFullscreen),
	FrameSyncEvent(InD3DRHI)
{
	check(IsInGameThread());
	D3DRHI->Viewports.Add(this);

	// Ensure that the D3D device has been created.
	D3DRHI->InitD3DDevice();

	// Create a backbuffer/swapchain for each viewport
	TRefCountPtr<IDXGIDevice> DXGIDevice;
	VERIFYD3D11RESULT_EX(D3DRHI->GetDevice()->QueryInterface(IID_IDXGIDevice, (void**)DXGIDevice.GetInitReference()), D3DRHI->GetDevice());

	if(!GSwapFlagsInitialized)
	{
		IDXGIFactory1* Factory1 = D3DRHI->GetFactory();
		TRefCountPtr<IDXGIFactory5> Factory5;

		if(GD3D11UseAllowTearing)
		{
			if (S_OK == Factory1->QueryInterface(__uuidof(IDXGIFactory5), (void**)Factory5.GetInitReference()))
			{
				UINT AllowTearing = 0;
				if (S_OK == Factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &AllowTearing, sizeof(UINT)) && AllowTearing != 0)
				{
					GSwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
					GSwapChainFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
					GSwapChainBufferCount = 2;
				}
			}
		}
		GSwapFlagsInitialized = true;
	}
	uint32 BufferCount = GSwapChainBufferCount;
	// If requested, keep a handle to a DXGIOutput so we can force that display on fullscreen swap
	uint32 DisplayIndex = D3DRHI->GetHDRDetectedDisplayIndex();
	bForcedFullscreenDisplay = FParse::Value(FCommandLine::Get(), TEXT("FullscreenDisplay="), DisplayIndex);

	if (bForcedFullscreenDisplay || GRHISupportsHDROutput)
	{
		TRefCountPtr<IDXGIAdapter> DXGIAdapter;
		DXGIDevice->GetAdapter((IDXGIAdapter**)DXGIAdapter.GetInitReference());

		if (S_OK != DXGIAdapter->EnumOutputs(DisplayIndex, ForcedFullscreenOutput.GetInitReference()))
		{
			UE_LOG(LogD3D11RHI, Log, TEXT("Failed to find requested output display (%i)."), DisplayIndex);
			ForcedFullscreenOutput = nullptr;
			bForcedFullscreenDisplay = false;
		}
	}
	else
	{
		ForcedFullscreenOutput = nullptr;
	}

	if (PixelFormat == PF_FloatRGBA && bIsFullscreen)
	{
		// Send HDR meta data to enable
		D3DRHI->EnableHDR();
	}

	// Skip swap chain creation in off-screen rendering mode
	bNeedSwapChain = !FParse::Param(FCommandLine::Get(), TEXT("RenderOffScreen"));
	if (bNeedSwapChain)
	{
		// Create the swapchain.
		if (InD3DRHI->IsQuadBufferStereoEnabled())
		{
			IDXGIFactory2* Factory2 = (IDXGIFactory2*)D3DRHI->GetFactory();

			BOOL stereoEnabled = Factory2->IsWindowedStereoEnabled();
			if (stereoEnabled)
			{
				DXGI_SWAP_CHAIN_DESC1 SwapChainDesc1;
				FMemory::Memzero(&SwapChainDesc1, sizeof(DXGI_SWAP_CHAIN_DESC1));

				// Enable stereo 
				SwapChainDesc1.Stereo = true;
				// MSAA Sample count
				SwapChainDesc1.SampleDesc.Count = 1;
				SwapChainDesc1.SampleDesc.Quality = 0;

				SwapChainDesc1.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
				SwapChainDesc1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
				// Double buffering required to create stereo swap chain
				SwapChainDesc1.BufferCount = 2;
				SwapChainDesc1.Scaling = DXGI_SCALING_NONE;
				SwapChainDesc1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
				SwapChainDesc1.Flags = GSwapChainFlags;

				IDXGISwapChain1* SwapChain1 = nullptr;
				VERIFYD3D11RESULT_EX((Factory2->CreateSwapChainForHwnd(D3DRHI->GetDevice(), WindowHandle, &SwapChainDesc1, nullptr, nullptr, &SwapChain1)), D3DRHI->GetDevice());
				SwapChain = SwapChain1;
			}
			else
			{
				UE_LOG(LogD3D11RHI, Log, TEXT("FD3D11Viewport::FD3D11Viewport was not able to create stereo SwapChain; Please enable stereo in driver settings."));
				InD3DRHI->DisableQuadBufferStereo();
			}
		}

		// Try and create a swapchain capable of being used on HDR monitors
		if ((SwapChain == nullptr) && (InD3DRHI->bDXGISupportsHDR))
		{
			// Create the swapchain.
			DXGI_SWAP_CHAIN_DESC1 SwapChainDesc;
			FMemory::Memzero(&SwapChainDesc, sizeof(DXGI_SWAP_CHAIN_DESC1));
			SwapChainDesc.Width = SizeX;
			SwapChainDesc.Height = SizeY;
			SwapChainDesc.SampleDesc.Count = 1;
			SwapChainDesc.SampleDesc.Quality = 0;
			SwapChainDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
			SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;

			DXGI_SWAP_CHAIN_FULLSCREEN_DESC FSSwapChainDesc = {};
			FSSwapChainDesc.Windowed = !bIsFullscreen;

			// Needed for HDR
			BackBufferCount = 2;
			SwapChainDesc.SwapEffect = GSwapEffect;
			SwapChainDesc.BufferCount = BackBufferCount;
			SwapChainDesc.Flags = GSwapChainFlags;
			SwapChainDesc.Scaling = DXGI_SCALING_NONE;

			IDXGISwapChain1* SwapChain1 = nullptr;
			IDXGIFactory2* Factory2 = (IDXGIFactory2*)D3DRHI->GetFactory();

			if(!FAILED(Factory2->CreateSwapChainForHwnd(D3DRHI->GetDevice(), WindowHandle, &SwapChainDesc, &FSSwapChainDesc, nullptr, &SwapChain1)))
			{
				SwapChain1->QueryInterface(__uuidof(IDXGISwapChain1), (void**)SwapChain.GetInitReference());

				// See if we are running on a HDR monitor 
				CheckHDRMonitorStatus();
			}
		}

		if (SwapChain == nullptr)
		{
			BackBufferCount = GSwapChainBufferCount;

			// Create the swapchain.
			DXGI_SWAP_CHAIN_DESC SwapChainDesc;
			FMemory::Memzero(&SwapChainDesc, sizeof(DXGI_SWAP_CHAIN_DESC));

			SwapChainDesc.BufferDesc = SetupDXGI_MODE_DESC();
			// MSAA Sample count
			SwapChainDesc.SampleDesc.Count = 1;
			SwapChainDesc.SampleDesc.Quality = 0;
			SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
			// 1:single buffering, 2:double buffering, 3:triple buffering
			SwapChainDesc.BufferCount = BackBufferCount;
			SwapChainDesc.OutputWindow = WindowHandle;
			SwapChainDesc.Windowed = !bIsFullscreen;
			// DXGI_SWAP_EFFECT_DISCARD / DXGI_SWAP_EFFECT_SEQUENTIAL
			SwapChainDesc.SwapEffect = GSwapEffect;
			SwapChainDesc.Flags = GSwapChainFlags;
			VERIFYD3D11RESULT_EX(D3DRHI->GetFactory()->CreateSwapChain(DXGIDevice, &SwapChainDesc, SwapChain.GetInitReference()), D3DRHI->GetDevice());
		}

		// Set the DXGI message hook to not change the window behind our back.
		D3DRHI->GetFactory()->MakeWindowAssociation(WindowHandle,DXGI_MWA_NO_WINDOW_CHANGES);
	}
	// Create a RHI surface to represent the viewport's back buffer.
	BackBuffer = GetSwapChainSurface(D3DRHI, PixelFormat, SizeX, SizeY, SwapChain);

	// Tell the window to redraw when they can.
	// @todo: For Slate viewports, it doesn't make sense to post WM_PAINT messages (we swallow those.)
	::PostMessageW( WindowHandle, WM_PAINT, 0, 0 );

	BeginInitResource(&FrameSyncEvent);
}

// When a window has moved or resized we need to check whether it is on a HDR monitor or not. Set the correct color space of the monitor
void FD3D11Viewport::CheckHDRMonitorStatus()
{
#if WITH_EDITOR

	DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

	static auto CVarHDREnable = IConsoleManager::Get().FindConsoleVariable(TEXT("Editor.HDRSupport"));
	if (CVarHDREnable->GetInt() != 0)
	{
		FlushRenderingCommands();

		if (SwapChain)
		{
			TRefCountPtr<IDXGIOutput> Output;
			if (SUCCEEDED(SwapChain->GetContainingOutput(Output.GetInitReference())))
			{
				TRefCountPtr<IDXGIOutput6> Output6;
				if (SUCCEEDED(Output->QueryInterface(IID_PPV_ARGS(Output6.GetInitReference()))))
				{
					DXGI_OUTPUT_DESC1 desc;
					Output6->GetDesc1(&desc);

					if (desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
					{
						// Display output is HDR10.
						colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
					}
				}
			}
		}

		TRefCountPtr<IDXGISwapChain3> swapChain3;

		if (SUCCEEDED(SwapChain->QueryInterface(IID_PPV_ARGS(swapChain3.GetInitReference()))))
		{
			UINT colorSpaceSupport = 0;
		
			if (SUCCEEDED(swapChain3->CheckColorSpaceSupport(colorSpace, &colorSpaceSupport)) && 
				(colorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
			{
				swapChain3->SetColorSpace1(colorSpace);
			}
		}
	}
	
	if (colorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
	{
		PixelColorSpace = EColorSpaceAndEOTF::ERec2020_PQ;
	}
	else
	{
		PixelColorSpace =  EColorSpaceAndEOTF::ERec709_sRGB;
	}
#else
	PixelColorSpace =  EColorSpaceAndEOTF::ERec709_sRGB;
#endif
}

void FD3D11Viewport::ConditionalResetSwapChain(bool bIgnoreFocus)
{
	uint32 Valid = ValidState;
	if(0 != (Valid & VIEWPORT_INVALID))
	{
		if(0 != (Valid & VIEWPORT_FULLSCREEN_LOST))
		{
			FlushRenderingCommands();
			ValidState &= ~(VIEWPORT_FULLSCREEN_LOST);
			Resize(SizeX, SizeY, false, PixelFormat);
		}
		else
		{
			ResetSwapChainInternal(bIgnoreFocus);
		}
	}
}

void FD3D11Viewport::ResetSwapChainInternal(bool bIgnoreFocus)
{
	if (0 != (ValidState & VIEWPORT_INVALID))
	{
		// Check if the viewport's window is focused before resetting the swap chain's fullscreen state.
		HWND FocusWindow = ::GetFocus();
		const bool bIsFocused = FocusWindow == WindowHandle;
		const bool bIsIconic = !!::IsIconic(WindowHandle);
		if (bIgnoreFocus || (bIsFocused && !bIsIconic))
		{
			FlushRenderingCommands();

			// Explicit output selection in fullscreen only (commandline or HDR enabled)
			bool bNeedsForcedDisplay = bIsFullscreen && (bForcedFullscreenDisplay || PixelFormat == PF_FloatRGBA);
			HRESULT Result = SwapChain->SetFullscreenState(bIsFullscreen, bNeedsForcedDisplay ? ForcedFullscreenOutput : nullptr);

			if (SUCCEEDED(Result))
			{
				ValidState &= ~(VIEWPORT_INVALID);
			}
			else if (Result != DXGI_ERROR_NOT_CURRENTLY_AVAILABLE && Result != DXGI_STATUS_MODE_CHANGE_IN_PROGRESS)
			{
				UE_LOG(LogD3D11RHI, Error, TEXT("IDXGISwapChain::SetFullscreenState returned %08x, unknown error status."), Result);
			}
		}
	}
}

#include "Windows/HideWindowsPlatformTypes.h"
