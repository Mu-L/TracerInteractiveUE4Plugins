// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D12RHI.cpp: Unreal D3D RHI library implementation.
	=============================================================================*/

#include "D3D12RHIPrivate.h"
#include "RHIStaticStates.h"
#include "OneColorShader.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "amd_ags.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#if !UE_BUILD_SHIPPING
#include "STaskGraph.h"
#endif

DEFINE_LOG_CATEGORY(LogD3D12RHI);


static TAutoConsoleVariable<int32> CVarD3D12UseD24(
	TEXT("r.D3D12.Depth24Bit"),
	0,
	TEXT("0: Use 32-bit float depth buffer\n1: Use 24-bit fixed point depth buffer(default)\n"),
	ECVF_ReadOnly
);


TAutoConsoleVariable<int32> CVarD3D12ZeroBufferSizeInMB(
	TEXT("D3D12.ZeroBufferSizeInMB"),
	4,
	TEXT("The D3D12 RHI needs a static allocation of zeroes to use when streaming textures asynchronously. It should be large enough to support the largest mipmap you need to stream. The default is 4MB."),
	ECVF_ReadOnly
	);

/// @cond DOXYGEN_WARNINGS

__declspec(thread) FD3D12FastAllocator* FD3D12DynamicRHI::HelperThreadDynamicHeapAllocator = nullptr;

/// @endcond

FD3D12DynamicRHI* FD3D12DynamicRHI::SingleD3DRHI = nullptr;

using namespace D3D12RHI;

FD3D12DynamicRHI::FD3D12DynamicRHI(const TArray<TSharedPtr<FD3D12Adapter>>& ChosenAdaptersIn) :
	NumThreadDynamicHeapAllocators(0),
	ChosenAdapters(ChosenAdaptersIn),
	AmdAgsContext(nullptr),
	FlipEvent(INVALID_HANDLE_VALUE)
{
	// The FD3D12DynamicRHI must be a singleton
	check(SingleD3DRHI == nullptr);
	SingleD3DRHI = this;

	ThreadDynamicHeapAllocatorArray.AddZeroed(FPlatformMisc::NumberOfCoresIncludingHyperthreads());

	// This should be called once at the start 
	check(IsInGameThread());
	check(!GIsThreadedRendering);

	// Adapter must support FL11+
	FeatureLevel = GetAdapter().GetFeatureLevel();
	check(FeatureLevel >= D3D_FEATURE_LEVEL_11_0);

#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
	// Allocate a buffer of zeroes. This is used when we need to pass D3D memory
	// that we don't care about and will overwrite with valid data in the future.
	ZeroBufferSize = FMath::Max(CVarD3D12ZeroBufferSizeInMB.GetValueOnAnyThread(), 0) * (1 << 20);
	ZeroBuffer = FMemory::Malloc(ZeroBufferSize);
	FMemory::Memzero(ZeroBuffer, ZeroBufferSize);
#else
	ZeroBufferSize = 0;
	ZeroBuffer = nullptr;
#endif // PLATFORM_WINDOWS

	GPoolSizeVRAMPercentage = 0;
	GTexturePoolSize = 0;
	GConfig->GetInt(TEXT("TextureStreaming"), TEXT("PoolSizeVRAMPercentage"), GPoolSizeVRAMPercentage, GEngineIni);

	// Initialize the platform pixel format map.
	GPixelFormats[PF_Unknown		].PlatformFormat = DXGI_FORMAT_UNKNOWN;
	GPixelFormats[PF_A32B32G32R32F	].PlatformFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
	GPixelFormats[PF_B8G8R8A8		].PlatformFormat = DXGI_FORMAT_B8G8R8A8_TYPELESS;
	GPixelFormats[PF_G8				].PlatformFormat = DXGI_FORMAT_R8_UNORM;
	GPixelFormats[PF_G16			].PlatformFormat = DXGI_FORMAT_R16_UNORM;
	GPixelFormats[PF_DXT1			].PlatformFormat = DXGI_FORMAT_BC1_TYPELESS;
	GPixelFormats[PF_DXT3			].PlatformFormat = DXGI_FORMAT_BC2_TYPELESS;
	GPixelFormats[PF_DXT5			].PlatformFormat = DXGI_FORMAT_BC3_TYPELESS;
	GPixelFormats[PF_BC4			].PlatformFormat = DXGI_FORMAT_BC4_UNORM;
	GPixelFormats[PF_UYVY			].PlatformFormat = DXGI_FORMAT_UNKNOWN;		// TODO: Not supported in D3D11
	if (CVarD3D12UseD24.GetValueOnAnyThread())
	{
		GPixelFormats[PF_DepthStencil].PlatformFormat = DXGI_FORMAT_R24G8_TYPELESS;
		GPixelFormats[PF_DepthStencil].BlockBytes = 4;
		GPixelFormats[PF_DepthStencil].Supported = true;
		GPixelFormats[PF_X24_G8].PlatformFormat = DXGI_FORMAT_X24_TYPELESS_G8_UINT;
		GPixelFormats[PF_X24_G8].BlockBytes = 4;
	}
	else
	{
		GPixelFormats[PF_DepthStencil].PlatformFormat = DXGI_FORMAT_R32G8X24_TYPELESS;
		GPixelFormats[PF_DepthStencil].BlockBytes = 5;
		GPixelFormats[PF_DepthStencil].Supported = true;
		GPixelFormats[PF_X24_G8].PlatformFormat = DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;
		GPixelFormats[PF_X24_G8].BlockBytes = 5;
	}
	GPixelFormats[PF_ShadowDepth	].PlatformFormat = DXGI_FORMAT_R16_TYPELESS;
	GPixelFormats[PF_ShadowDepth	].BlockBytes = 2;
	GPixelFormats[PF_ShadowDepth	].Supported = true;
	GPixelFormats[PF_R32_FLOAT		].PlatformFormat = DXGI_FORMAT_R32_FLOAT;
	GPixelFormats[PF_G16R16			].PlatformFormat = DXGI_FORMAT_R16G16_UNORM;
	GPixelFormats[PF_G16R16F		].PlatformFormat = DXGI_FORMAT_R16G16_FLOAT;
	GPixelFormats[PF_G16R16F_FILTER	].PlatformFormat = DXGI_FORMAT_R16G16_FLOAT;
	GPixelFormats[PF_G32R32F		].PlatformFormat = DXGI_FORMAT_R32G32_FLOAT;
	GPixelFormats[PF_A2B10G10R10	].PlatformFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
	GPixelFormats[PF_A16B16G16R16	].PlatformFormat = DXGI_FORMAT_R16G16B16A16_UNORM;
	GPixelFormats[PF_D24			].PlatformFormat = DXGI_FORMAT_R24G8_TYPELESS;
	GPixelFormats[PF_R16F			].PlatformFormat = DXGI_FORMAT_R16_FLOAT;
	GPixelFormats[PF_R16F_FILTER	].PlatformFormat = DXGI_FORMAT_R16_FLOAT;

	GPixelFormats[PF_FloatRGB		].PlatformFormat = DXGI_FORMAT_R11G11B10_FLOAT;
	GPixelFormats[PF_FloatRGB		].BlockBytes = 4;
	GPixelFormats[PF_FloatRGBA		].PlatformFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	GPixelFormats[PF_FloatRGBA		].BlockBytes = 8;
	GPixelFormats[PF_FloatR11G11B10	].PlatformFormat = DXGI_FORMAT_R11G11B10_FLOAT;
	GPixelFormats[PF_FloatR11G11B10	].Supported = true;
	GPixelFormats[PF_FloatR11G11B10	].BlockBytes = 4;

	GPixelFormats[PF_V8U8			].PlatformFormat = DXGI_FORMAT_R8G8_SNORM;
	GPixelFormats[PF_BC5			].PlatformFormat = DXGI_FORMAT_BC5_UNORM;
	GPixelFormats[PF_A1				].PlatformFormat = DXGI_FORMAT_R1_UNORM; // Not supported for rendering.
	GPixelFormats[PF_A8				].PlatformFormat = DXGI_FORMAT_A8_UNORM;
	GPixelFormats[PF_R32_UINT		].PlatformFormat = DXGI_FORMAT_R32_UINT;
	GPixelFormats[PF_R32_SINT		].PlatformFormat = DXGI_FORMAT_R32_SINT;

	GPixelFormats[PF_R16_UINT		].PlatformFormat = DXGI_FORMAT_R16_UINT;
	GPixelFormats[PF_R16_SINT		].PlatformFormat = DXGI_FORMAT_R16_SINT;
	GPixelFormats[PF_R16G16B16A16_UINT].PlatformFormat = DXGI_FORMAT_R16G16B16A16_UINT;
	GPixelFormats[PF_R16G16B16A16_SINT].PlatformFormat = DXGI_FORMAT_R16G16B16A16_SINT;

	GPixelFormats[PF_R5G6B5_UNORM	].PlatformFormat = DXGI_FORMAT_B5G6R5_UNORM;
	GPixelFormats[PF_R8G8B8A8		].PlatformFormat = DXGI_FORMAT_R8G8B8A8_TYPELESS;
	GPixelFormats[PF_R8G8B8A8_UINT	].PlatformFormat = DXGI_FORMAT_R8G8B8A8_UINT;
	GPixelFormats[PF_R8G8B8A8_SNORM	].PlatformFormat = DXGI_FORMAT_R8G8B8A8_SNORM;

	GPixelFormats[PF_R8G8			].PlatformFormat = DXGI_FORMAT_R8G8_UNORM;
	GPixelFormats[PF_R32G32B32A32_UINT].PlatformFormat = DXGI_FORMAT_R32G32B32A32_UINT;
	GPixelFormats[PF_R16G16_UINT	].PlatformFormat = DXGI_FORMAT_R16G16_UINT;
	GPixelFormats[PF_R32G32_UINT	].PlatformFormat = DXGI_FORMAT_R32G32_UINT;

	GPixelFormats[PF_BC6H			].PlatformFormat = DXGI_FORMAT_BC6H_UF16;
	GPixelFormats[PF_BC7			].PlatformFormat = DXGI_FORMAT_BC7_TYPELESS;
	GPixelFormats[PF_R8_UINT		].PlatformFormat = DXGI_FORMAT_R8_UINT;

	GPixelFormats[PF_R16G16B16A16_UNORM].PlatformFormat = DXGI_FORMAT_R16G16B16A16_UNORM;
	GPixelFormats[PF_R16G16B16A16_SNORM].PlatformFormat = DXGI_FORMAT_R16G16B16A16_SNORM;

	// MS - Not doing any feature level checks. D3D12 currently supports these limits.
	// However this may need to be revisited if new feature levels are introduced with different HW requirement
	GSupportsSeparateRenderTargetBlendState = true;
	GMaxTextureDimensions = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
	GMaxCubeTextureDimensions = D3D12_REQ_TEXTURECUBE_DIMENSION;
	GMaxTextureArrayLayers = D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;
	GRHISupportsMSAADepthSampleAccess = true;

	GMaxTextureMipCount = FMath::CeilLogTwo(GMaxTextureDimensions) + 1;
	GMaxTextureMipCount = FMath::Min<int32>(MAX_TEXTURE_MIP_COUNT, GMaxTextureMipCount);
	GMaxShadowDepthBufferSizeX = GMaxTextureDimensions;
	GMaxShadowDepthBufferSizeY = GMaxTextureDimensions;
	GRHISupportsResolveCubemapFaces = true;

	GRHISupportsRHIThread = true;
#if PLATFORM_XBOXONE
	GRHISupportsRHIOnTaskThread = true;
#endif

	GRHISupportsParallelRHIExecute = D3D12_SUPPORTS_PARALLEL_RHI_EXECUTE;

	GSupportsTimestampRenderQueries = true;
	GSupportsParallelOcclusionQueries = true;

	{
		// Workaround for 4.14. Limit the number of GPU stats on D3D12 due to an issue with high memory overhead with render queries (Jira UE-38139)
		//@TODO: Remove this when render query issues are fixed
		static IConsoleVariable* GPUStatsEnabledCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.GPUStatsMaxQueriesPerFrame"));
		if (GPUStatsEnabledCVar)
		{
			GPUStatsEnabledCVar->Set(1024); // 1024*64KB = 64MB
		}
	}

	// Enable async compute by default
	GEnableAsyncCompute = true;
}

FD3D12DynamicRHI::~FD3D12DynamicRHI()
{
	UE_LOG(LogD3D12RHI, Log, TEXT("~FD3D12DynamicRHI"));

	check(ChosenAdapters.Num() == 0);
}

void FD3D12DynamicRHI::Shutdown()
{
	check(IsInGameThread() && IsInRenderingThread());  // require that the render thread has been shut down

#if PLATFORM_WINDOWS
	if (AmdAgsContext)
	{
		// Clean up the AMD extensions and shut down the AMD AGS utility library
		agsDriverExtensionsDX12_DeInit(AmdAgsContext);
		agsDeInit(AmdAgsContext);
		AmdAgsContext = nullptr;
	}
#endif

	RHIShutdownFlipTracking();

	// Cleanup All of the Adapters
	for (TSharedPtr<FD3D12Adapter>& Adapter : ChosenAdapters)
	{
		// Take a reference on the ID3D12Device so that we can delete the FD3D12Device
		// and have it's children correctly release ID3D12* objects via RAII
		TRefCountPtr<ID3D12Device> Direct3DDevice = Adapter->GetD3DDevice();

		Adapter->Cleanup();

#if PLATFORM_WINDOWS
		const bool bWithD3DDebug = D3D12RHI_ShouldCreateWithD3DDebug();
		if (bWithD3DDebug)
		{
			TRefCountPtr<ID3D12DebugDevice> Debug;

			if (SUCCEEDED(Direct3DDevice->QueryInterface(IID_PPV_ARGS(Debug.GetInitReference()))))
			{
				D3D12_RLDO_FLAGS rldoFlags = D3D12_RLDO_DETAIL;

				Debug->ReportLiveDeviceObjects(rldoFlags);
			}
		}
#endif
		// The lifetime of the adapter is managed by the FD3D12DynamicRHIModule
	}

	ChosenAdapters.Empty();

	// Release the buffer of zeroes.
	FMemory::Free(ZeroBuffer);
	ZeroBuffer = NULL;
	ZeroBufferSize = 0;
}

FD3D12CommandContext* FD3D12DynamicRHI::CreateCommandContext(FD3D12Device* InParent, FD3D12SubAllocatedOnlineHeap::SubAllocationDesc& SubHeapDesc, bool InIsDefaultContext, bool InIsAsyncComputeContext)
{
	FD3D12CommandContext* NewContext = new FD3D12CommandContext(InParent, SubHeapDesc, InIsDefaultContext, InIsAsyncComputeContext);
	return NewContext;
}

ID3D12CommandQueue* FD3D12DynamicRHI::CreateCommandQueue(FD3D12Device* Device, const D3D12_COMMAND_QUEUE_DESC& Desc)
{
	ID3D12CommandQueue* pCommandQueue = nullptr;
	VERIFYD3D12RESULT(Device->GetDevice()->CreateCommandQueue(&Desc, IID_PPV_ARGS(&pCommandQueue)));
	return pCommandQueue;
}

IRHICommandContext* FD3D12DynamicRHI::RHIGetDefaultContext()
{
	FD3D12Adapter& Adapter = GetAdapter();

	IRHICommandContext* DefaultCommandContext = nullptr;	
	if (GNumExplicitGPUsForRendering > 1)
	{
		DefaultCommandContext = static_cast<IRHICommandContext*>(&Adapter.GetDefaultContextRedirector());
	}
	else // Single GPU path
	{
		FD3D12Device* Device = Adapter.GetDevice(0);
		DefaultCommandContext = static_cast<IRHICommandContext*>(&Device->GetDefaultCommandContext());
	}

	check(DefaultCommandContext);
	return DefaultCommandContext;
}

#if WITH_MGPU
IRHICommandContext* FD3D12DynamicRHI::RHIGetDefaultContext(FRHIGPUMask GPUMask)
{
	FD3D12Adapter& Adapter = GetAdapter();

	if (GNumExplicitGPUsForRendering > 1 && GPUMask == FRHIGPUMask::All())
	{
		return static_cast<IRHICommandContext*>(&Adapter.GetDefaultContextRedirector());
	}
	else // The next code assumes a single index.
	{
		return static_cast<IRHICommandContext*>(&Adapter.GetDevice(GPUMask.ToIndex())->GetDefaultCommandContext());
	}
}
#endif // WITH_MGPU

IRHIComputeContext* FD3D12DynamicRHI::RHIGetDefaultAsyncComputeContext()
{
	FD3D12Adapter& Adapter = GetAdapter();

	IRHIComputeContext* DefaultAsyncComputeContext = nullptr;
	if (GNumExplicitGPUsForRendering > 1)
	{
		DefaultAsyncComputeContext = GEnableAsyncCompute ?
			static_cast<IRHIComputeContext*>(&Adapter.GetDefaultAsyncComputeContextRedirector()) :
			static_cast<IRHIComputeContext*>(&Adapter.GetDefaultContextRedirector());
	}
	else // Single GPU path.
	{
		FD3D12Device* Device = Adapter.GetDevice(0);
		DefaultAsyncComputeContext = GEnableAsyncCompute ?
			static_cast<IRHIComputeContext*>(&Device->GetDefaultAsyncComputeContext()) :
			static_cast<IRHIComputeContext*>(&Device->GetDefaultCommandContext());
	}

	check(DefaultAsyncComputeContext);
	return DefaultAsyncComputeContext;
}

#if WITH_MGPU
IRHIComputeContext* FD3D12DynamicRHI::RHIGetDefaultAsyncComputeContext(FRHIGPUMask GPUMask)
{
	if (GEnableAsyncCompute)
	{
		FD3D12Adapter& Adapter = GetAdapter();
		if (GNumExplicitGPUsForRendering > 1 && GPUMask == FRHIGPUMask::All())
		{
			return static_cast<IRHIComputeContext*>(&Adapter.GetDefaultAsyncComputeContextRedirector());
		}
		else // Single GPU path.
		{
			return static_cast<IRHIComputeContext*>(&Adapter.GetDevice(GPUMask.ToIndex())->GetDefaultAsyncComputeContext());
		}
	}
	else 
	{
		return static_cast<IRHIComputeContext*>(RHIGetDefaultContext(GPUMask));
	}
}
#endif // WITH_MGPU

void FD3D12DynamicRHI::UpdateBuffer(FD3D12Resource* Dest, uint32 DestOffset, FD3D12Resource* Source, uint32 SourceOffset, uint32 NumBytes)
{
	FD3D12Device* Device = Dest->GetParentDevice();

	FD3D12CommandContext& DefaultContext = Device->GetDefaultCommandContext();
	FD3D12CommandListHandle& hCommandList = DefaultContext.CommandListHandle;

	FConditionalScopeResourceBarrier ScopeResourceBarrierDest(hCommandList, Dest, D3D12_RESOURCE_STATE_COPY_DEST, 0);
	// Don't need to transition upload heaps

	DefaultContext.numCopies++;
	hCommandList.FlushResourceBarriers();
	hCommandList->CopyBufferRegion(Dest->GetResource(), DestOffset, Source->GetResource(), SourceOffset, NumBytes);
	hCommandList.UpdateResidency(Dest);
	hCommandList.UpdateResidency(Source);

	DEBUG_RHI_EXECUTE_COMMAND_LIST(this);
}

void FD3D12DynamicRHI::RHIFlushResources()
{
	// Nothing to do (yet!)
}

void FD3D12DynamicRHI::RHIAcquireThreadOwnership()
{
}

void FD3D12DynamicRHI::RHIReleaseThreadOwnership()
{
	// Nothing to do
}

void* FD3D12DynamicRHI::RHIGetNativeDevice()
{
	return (void*)GetAdapter().GetD3DDevice();
}

/**
* Returns a supported screen resolution that most closely matches the input.
* @param Width - Input: Desired resolution width in pixels. Output: A width that the platform supports.
* @param Height - Input: Desired resolution height in pixels. Output: A height that the platform supports.
*/
void FD3D12DynamicRHI::RHIGetSupportedResolution(uint32& Width, uint32& Height)
{
	uint32 InitializedMode = false;
	DXGI_MODE_DESC BestMode;
	BestMode.Width = 0;
	BestMode.Height = 0;

	{
		HRESULT HResult = S_OK;
		TRefCountPtr<IDXGIAdapter> Adapter;
		HResult = GetAdapter().GetDXGIFactory()->EnumAdapters(GetAdapter().GetAdapterIndex(), Adapter.GetInitReference());
		if (DXGI_ERROR_NOT_FOUND == HResult)
		{
			return;
		}
		if (FAILED(HResult))
		{
			return;
		}

		// get the description of the adapter
		DXGI_ADAPTER_DESC AdapterDesc;
		VERIFYD3D12RESULT(Adapter->GetDesc(&AdapterDesc));

#ifndef PLATFORM_XBOXONE // No need for display mode enumeration on console
		// Enumerate outputs for this adapter
		// TODO: Cap at 1 for default output
		for (uint32 o = 0; o < 1; o++)
		{
			TRefCountPtr<IDXGIOutput> Output;
			HResult = Adapter->EnumOutputs(o, Output.GetInitReference());
			if (DXGI_ERROR_NOT_FOUND == HResult)
			{
				break;
			}
			if (FAILED(HResult))
			{
				return;
			}

			// TODO: GetDisplayModeList is a terribly SLOW call.  It can take up to a second per invocation.
			//  We might want to work around some DXGI badness here.
			DXGI_FORMAT Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			uint32 NumModes = 0;
			HResult = Output->GetDisplayModeList(Format, 0, &NumModes, NULL);
			if (HResult == DXGI_ERROR_NOT_FOUND)
			{
				return;
			}
			else if (HResult == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE)
			{
				UE_LOG(LogD3D12RHI, Fatal,
					TEXT("This application cannot be run over a remote desktop configuration")
					);
				return;
			}
			DXGI_MODE_DESC* ModeList = new DXGI_MODE_DESC[NumModes];
			VERIFYD3D12RESULT(Output->GetDisplayModeList(Format, 0, &NumModes, ModeList));

			for (uint32 m = 0; m < NumModes; m++)
			{
				// Search for the best mode

				// Suppress static analysis warnings about a potentially out-of-bounds read access to ModeList. This is a false positive - Index is always within range.
				CA_SUPPRESS(6385);
				bool IsEqualOrBetterWidth = FMath::Abs((int32)ModeList[m].Width - (int32)Width) <= FMath::Abs((int32)BestMode.Width - (int32)Width);
				bool IsEqualOrBetterHeight = FMath::Abs((int32)ModeList[m].Height - (int32)Height) <= FMath::Abs((int32)BestMode.Height - (int32)Height);
				if (!InitializedMode || (IsEqualOrBetterWidth && IsEqualOrBetterHeight))
				{
					BestMode = ModeList[m];
					InitializedMode = true;
				}
			}

			delete[] ModeList;
		}
#endif // PLATFORM_XBOXONE
	}

	check(InitializedMode);
	Width = BestMode.Width;
	Height = BestMode.Height;
}

void FD3D12DynamicRHI::GetBestSupportedMSAASetting(DXGI_FORMAT PlatformFormat, uint32 MSAACount, uint32& OutBestMSAACount, uint32& OutMSAAQualityLevels)
{
	//  We disable MSAA for Feature level 10
	if (GMaxRHIFeatureLevel == ERHIFeatureLevel::SM4)
	{
		OutBestMSAACount = 1;
		OutMSAAQualityLevels = 0;
		return;
	}

	// start counting down from current setting (indicated the current "best" count) and move down looking for support
	for (uint32 SampleCount = MSAACount; SampleCount > 0; SampleCount--)
	{
		// The multisampleQualityLevels struct serves as both the input and output to CheckFeatureSupport.
		D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS multisampleQualityLevels = {};
		multisampleQualityLevels.SampleCount = SampleCount;

		if (SUCCEEDED(GetAdapter().GetD3DDevice()->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &multisampleQualityLevels, sizeof(multisampleQualityLevels))))
		{
			OutBestMSAACount = SampleCount;
			OutMSAAQualityLevels = multisampleQualityLevels.NumQualityLevels;
			break;
		}
	}

	return;
}

uint32 FD3D12DynamicRHI::GetDebugFlags()
{
	return GetAdapter().GetDebugFlags();
}

bool FD3D12DynamicRHI::CheckGpuHeartbeat() const
{
	return ChosenAdapters[0]->GetGPUProfiler().CheckGpuHeartbeat();
}
