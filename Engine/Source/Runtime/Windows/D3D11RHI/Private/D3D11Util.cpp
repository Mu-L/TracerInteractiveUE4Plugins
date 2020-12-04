// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D11Util.h: D3D RHI utility implementation.
=============================================================================*/

#include "D3D11RHIPrivate.h"
#include "EngineModule.h"
#include "RendererInterface.h"

#define D3DERR(x) case x: ErrorCodeText = TEXT(#x); break;
#define LOCTEXT_NAMESPACE "Developer.MessageLog"

#ifndef _FACD3D 
	#define _FACD3D  0x876
#endif	//_FACD3D 
#ifndef MAKE_D3DHRESULT
	#define _FACD3D  0x876
	#define MAKE_D3DHRESULT( code )  MAKE_HRESULT( 1, _FACD3D, code )
#endif	//MAKE_D3DHRESULT

#if WITH_D3DX_LIBS
	#ifndef D3DERR_INVALIDCALL
		#define D3DERR_INVALIDCALL MAKE_D3DHRESULT(2156)
	#endif//D3DERR_INVALIDCALL
	#ifndef D3DERR_WASSTILLDRAWING
		#define D3DERR_WASSTILLDRAWING MAKE_D3DHRESULT(540)
	#endif//D3DERR_WASSTILLDRAWING
#endif

static FString GetD3D11DeviceHungErrorString(HRESULT ErrorCode)
{
	FString ErrorCodeText;

	switch(ErrorCode)
	{
		D3DERR(DXGI_ERROR_DEVICE_HUNG)
		D3DERR(DXGI_ERROR_DEVICE_REMOVED)
		D3DERR(DXGI_ERROR_DEVICE_RESET)
		D3DERR(DXGI_ERROR_DRIVER_INTERNAL_ERROR)
		D3DERR(DXGI_ERROR_INVALID_CALL)
		default: ErrorCodeText = FString::Printf(TEXT("%08X"),(int32)ErrorCode);
	}

	return ErrorCodeText;
}

FString GetD3D11ErrorString(HRESULT ErrorCode, ID3D11Device* Device)
{
	FString ErrorCodeText;

	switch(ErrorCode)
	{
		D3DERR(S_OK);
		D3DERR(D3D11_ERROR_FILE_NOT_FOUND)
		D3DERR(D3D11_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS)
#if WITH_D3DX_LIBS
		D3DERR(D3DERR_INVALIDCALL)
		D3DERR(D3DERR_WASSTILLDRAWING)
#endif	//WITH_D3DX_LIBS
		D3DERR(E_FAIL)
		D3DERR(E_INVALIDARG)
		D3DERR(E_OUTOFMEMORY)
		D3DERR(DXGI_ERROR_INVALID_CALL)
		D3DERR(E_NOINTERFACE)
		D3DERR(DXGI_ERROR_DEVICE_REMOVED)
		default: ErrorCodeText = FString::Printf(TEXT("%08X"),(int32)ErrorCode);
	}

	if(ErrorCode == DXGI_ERROR_DEVICE_REMOVED && Device)
	{
		HRESULT hResDeviceRemoved = Device->GetDeviceRemovedReason();
		ErrorCodeText += FString(TEXT(" ")) + GetD3D11DeviceHungErrorString(hResDeviceRemoved);
	}

	return ErrorCodeText;
}

#undef D3DERR

const TCHAR* GetD3D11TextureFormatString(DXGI_FORMAT TextureFormat)
{
	static const TCHAR* EmptyString = TEXT("");
	const TCHAR* TextureFormatText = EmptyString;
#define D3DFORMATCASE(x) case x: TextureFormatText = TEXT(#x); break;
	switch(TextureFormat)
	{
		D3DFORMATCASE(DXGI_FORMAT_R8G8B8A8_UNORM)
		D3DFORMATCASE(DXGI_FORMAT_B8G8R8A8_UNORM)
		D3DFORMATCASE(DXGI_FORMAT_B8G8R8X8_UNORM)
		D3DFORMATCASE(DXGI_FORMAT_BC1_UNORM)
		D3DFORMATCASE(DXGI_FORMAT_BC2_UNORM)
		D3DFORMATCASE(DXGI_FORMAT_BC3_UNORM)
		D3DFORMATCASE(DXGI_FORMAT_BC4_UNORM)
		D3DFORMATCASE(DXGI_FORMAT_R16G16B16A16_FLOAT)
		D3DFORMATCASE(DXGI_FORMAT_R32G32B32A32_FLOAT)
		D3DFORMATCASE(DXGI_FORMAT_UNKNOWN)
		D3DFORMATCASE(DXGI_FORMAT_R8_UNORM)
		D3DFORMATCASE(DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
		D3DFORMATCASE(DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS)
		D3DFORMATCASE(DXGI_FORMAT_R32G8X24_TYPELESS)
		D3DFORMATCASE(DXGI_FORMAT_D24_UNORM_S8_UINT)
		D3DFORMATCASE(DXGI_FORMAT_R24_UNORM_X8_TYPELESS)
		D3DFORMATCASE(DXGI_FORMAT_R32_FLOAT)
		D3DFORMATCASE(DXGI_FORMAT_R16G16_UINT)
		D3DFORMATCASE(DXGI_FORMAT_R16G16_UNORM)
		D3DFORMATCASE(DXGI_FORMAT_R16G16_SNORM)
		D3DFORMATCASE(DXGI_FORMAT_R16G16_FLOAT)
		D3DFORMATCASE(DXGI_FORMAT_R32G32_FLOAT)
		D3DFORMATCASE(DXGI_FORMAT_R10G10B10A2_UNORM)
		D3DFORMATCASE(DXGI_FORMAT_R16G16B16A16_UINT)
		D3DFORMATCASE(DXGI_FORMAT_R8G8_SNORM)
		D3DFORMATCASE(DXGI_FORMAT_BC5_UNORM)
		D3DFORMATCASE(DXGI_FORMAT_R1_UNORM)
		D3DFORMATCASE(DXGI_FORMAT_R8G8B8A8_TYPELESS)
		D3DFORMATCASE(DXGI_FORMAT_B8G8R8A8_TYPELESS)
		D3DFORMATCASE(DXGI_FORMAT_BC7_UNORM)
		D3DFORMATCASE(DXGI_FORMAT_BC6H_UF16)
		default: TextureFormatText = EmptyString;
	}
#undef D3DFORMATCASE
	return TextureFormatText;
}

static FString GetD3D11TextureFlagString(uint32 TextureFlags)
{
	FString TextureFormatText = TEXT("");

	if (TextureFlags & D3D11_BIND_RENDER_TARGET)
	{
		TextureFormatText += TEXT("D3D11_BIND_RENDER_TARGET ");
	}

	if (TextureFlags & D3D11_BIND_DEPTH_STENCIL)
	{
		TextureFormatText += TEXT("D3D11_BIND_DEPTH_STENCIL ");
	}

	if (TextureFlags & D3D11_BIND_SHADER_RESOURCE)
	{
		TextureFormatText += TEXT("D3D11_BIND_SHADER_RESOURCE ");
	}

	if (TextureFlags & D3D11_BIND_UNORDERED_ACCESS)
	{
		TextureFormatText += TEXT("D3D11_BIND_UNORDERED_ACCESS ");
	}

	return TextureFormatText;
}

extern CORE_API bool GIsGPUCrashed;
static void TerminateOnDeviceRemoved(HRESULT D3DResult, ID3D11Device* Direct3DDevice)
{
	if (GDynamicRHI)
	{
		GDynamicRHI->CheckGpuHeartbeat();
	}

	if (D3DResult == DXGI_ERROR_DEVICE_REMOVED)
	{
#if NV_AFTERMATH
		uint32 Result = 0xffffffff;
		uint32 bDeviceActive = 0;
		if (GDX11NVAfterMathEnabled)
		{
			GFSDK_Aftermath_Device_Status Status;
			auto Res = GFSDK_Aftermath_GetDeviceStatus(&Status);
			Result = uint32(Res);
			if (Res == GFSDK_Aftermath_Result_Success)
			{
				bDeviceActive = Status == GFSDK_Aftermath_Device_Status_Active ? 1 : 0;
			}
		}
		UE_LOG(LogD3D11RHI, Log, TEXT("[Aftermath] GDynamicRHI=%p, GDX11NVAfterMathEnabled=%d, Result=0x%08X, bDeviceActive=%d"), GDynamicRHI, GDX11NVAfterMathEnabled, Result, bDeviceActive);
#else
		UE_LOG(LogD3D11RHI, Log, TEXT("[Aftermath] NV_AFTERMATH is not set"));
#endif

		GIsGPUCrashed = true;		
		if (Direct3DDevice)
		{
			HRESULT hRes = Direct3DDevice->GetDeviceRemovedReason();

			const TCHAR* Reason = TEXT("?");
			switch (hRes)
			{
			case DXGI_ERROR_DEVICE_HUNG:			Reason = TEXT("HUNG"); break;
			case DXGI_ERROR_DEVICE_REMOVED:			Reason = TEXT("REMOVED"); break;
			case DXGI_ERROR_DEVICE_RESET:			Reason = TEXT("RESET"); break;
			case DXGI_ERROR_DRIVER_INTERNAL_ERROR:	Reason = TEXT("INTERNAL_ERROR"); break;
			case DXGI_ERROR_INVALID_CALL:			Reason = TEXT("INVALID_CALL"); break;
			case S_OK:								Reason = TEXT("S_OK"); break;
			}
			
			// We currently don't support removed devices because FTexture2DResource can't recreate its RHI resources from scratch.
			// We would also need to recreate the viewport swap chains from scratch.			
			UE_LOG(LogD3D11RHI, Fatal, TEXT("Unreal Engine is exiting due to D3D device being lost. (Error: 0x%X - '%s')"), hRes, Reason);
		}
		else
		{
			UE_LOG(LogD3D11RHI, Fatal, TEXT("Unreal Engine is exiting due to D3D device being lost. D3D device was not available to assertain DXGI cause."));
		}

		// Workaround for the fact that in non-monolithic builds the exe gets into a weird state and exception handling fails. 
		// @todo investigate why non-monolithic builds fail to capture the exception when graphics driver crashes.
#if !IS_MONOLITHIC
		FPlatformMisc::RequestExit(true);
#endif
	}
}

static void TerminateOnOutOfMemory(HRESULT D3DResult, bool bCreatingTextures)
{
	if (D3DResult == E_OUTOFMEMORY)
	{
		if (bCreatingTextures)
		{
			FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *LOCTEXT("OutOfVideoMemoryTextures", "Out of video memory trying to allocate a texture! Make sure your video card has the minimum required memory, try lowering the resolution and/or closing other applications that are running. Exiting...").ToString(), TEXT("Error"));
		}
		else
		{
			FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *NSLOCTEXT("D3D11RHI", "OutOfMemory", "Out of video memory trying to allocate a rendering resource. Make sure your video card has the minimum required memory, try lowering the resolution and/or closing other applications that are running. Exiting...").ToString(), TEXT("Error"));
		}
#if STATS
		GetRendererModule().DebugLogOnCrash();
#endif
		FPlatformMisc::RequestExit(true);
	}
}


#ifndef MAKE_D3DHRESULT
	#define _FACD3D						0x876
	#define MAKE_D3DHRESULT( code)		MAKE_HRESULT( 1, _FACD3D, code )
#endif	//MAKE_D3DHRESULT

void VerifyD3D11ResultNoExit(HRESULT D3DResult, const ANSICHAR* Code, const ANSICHAR* Filename, uint32 Line, ID3D11Device* Device)
{
	check(FAILED(D3DResult));

	const FString& ErrorString = GetD3D11ErrorString(D3DResult, Device);

	UE_LOG(LogD3D11RHI, Error, TEXT("%s failed \n at %s:%u \n with error %s Error Code List: https://docs.microsoft.com/en-us/windows/desktop/direct3ddxgi/dxgi-error"), ANSI_TO_TCHAR(Code), ANSI_TO_TCHAR(Filename), Line, *ErrorString);
}

void VerifyD3D11Result(HRESULT D3DResult,const ANSICHAR* Code,const ANSICHAR* Filename,uint32 Line, ID3D11Device* Device)
{
	check(FAILED(D3DResult));

	const FString& ErrorString = GetD3D11ErrorString(D3DResult, Device);

	UE_LOG(LogD3D11RHI, Error, TEXT("%s failed \n at %s:%u \n with error %s"), ANSI_TO_TCHAR(Code), ANSI_TO_TCHAR(Filename), Line, *ErrorString);

	TerminateOnDeviceRemoved(D3DResult, Device);
	TerminateOnOutOfMemory(D3DResult, false);

	UE_LOG(LogD3D11RHI, Fatal,TEXT("%s failed \n at %s:%u \n with error %s"),ANSI_TO_TCHAR(Code),ANSI_TO_TCHAR(Filename),Line,*ErrorString);
}

void VerifyD3D11ShaderResult(FRHIShader* Shader, HRESULT D3DResult, const ANSICHAR* Code, const ANSICHAR* Filename, uint32 Line, ID3D11Device* Device)
{
	check(FAILED(D3DResult));

	const FString& ErrorString = GetD3D11ErrorString(D3DResult, Device);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (Shader->ShaderName.Len())
	{
		UE_LOG(LogD3D11RHI, Error, TEXT("%s failed trying to create shader %s\n at %s:%u \n with error %s"), ANSI_TO_TCHAR(Code), *Shader->ShaderName, ANSI_TO_TCHAR(Filename), Line, *ErrorString);
		TerminateOnDeviceRemoved(D3DResult, Device);
		TerminateOnOutOfMemory(D3DResult, false);

		UE_LOG(LogD3D11RHI, Fatal, TEXT("%s failed trying to create shader %s \n at %s:%u \n with error %s"), ANSI_TO_TCHAR(Code), *Shader->ShaderName, ANSI_TO_TCHAR(Filename), Line, *ErrorString);
	}
	else
#endif
	{
		VerifyD3D11Result(D3DResult, Code, Filename, Line, Device);
	}
}

void VerifyD3D11CreateTextureResult(HRESULT D3DResult, int32 UEFormat,const ANSICHAR* Code,const ANSICHAR* Filename,uint32 Line,uint32 SizeX,uint32 SizeY,uint32 SizeZ,uint8 D3DFormat,uint32 NumMips,uint32 Flags,
	D3D11_USAGE Usage, uint32 CPUAccessFlags, uint32 MiscFlags, uint32 SampleCount, uint32 SampleQuality,
	const void* SubResPtr, uint32 SubResPitch, uint32 SubResSlicePitch, ID3D11Device* Device)
{
	check(FAILED(D3DResult));

	const FString ErrorString = GetD3D11ErrorString(D3DResult, 0);
	const TCHAR* D3DFormatString = GetD3D11TextureFormatString((DXGI_FORMAT)D3DFormat);

	UE_LOG(LogD3D11RHI, Error,
		TEXT("%s failed \n at %s:%u \n with error %s, \n Size=%ix%ix%i PF=%d D3DFormat=%s(0x%08X), NumMips=%i, Flags=%s, Usage:0x%x, CPUFlags:0x%x, MiscFlags:0x%x, SampleCount:0x%x, SampleQuality:0x%x, SubresPtr:0x%p, SubresPitch:%i, SubresSlicePitch:%i"),
		ANSI_TO_TCHAR(Code),
		ANSI_TO_TCHAR(Filename),
		Line,
		*ErrorString,
		SizeX,
		SizeY,
		SizeZ,
		UEFormat,
		D3DFormatString,
		D3DFormat,
		NumMips,
		*GetD3D11TextureFlagString(Flags),
		Usage,
		CPUAccessFlags,
		MiscFlags,
		SampleCount,
		SampleQuality,
		SubResPtr,
		SubResPitch,
		SubResSlicePitch);

	TerminateOnDeviceRemoved(D3DResult, Device);
	TerminateOnOutOfMemory(D3DResult, true);

	UE_LOG(LogD3D11RHI, Fatal,
		TEXT("%s failed \n at %s:%u \n with error %s, \n Size=%ix%ix%i PF=%d Format=%s(0x%08X), NumMips=%i, Flags=%s, Usage:0x%x, CPUFlags:0x%x, MiscFlags:0x%x, SampleCount:0x%x, SampleQuality:0x%x, SubresPtr:0x%p, SubresPitch:%i, SubresSlicePitch:%i"),
		ANSI_TO_TCHAR(Code),
		ANSI_TO_TCHAR(Filename),
		Line,
		*ErrorString,
		SizeX,
		SizeY,
		SizeZ,
		UEFormat,
		D3DFormatString,
		D3DFormat,
		NumMips,
		*GetD3D11TextureFlagString(Flags),
		Usage,
		CPUAccessFlags,
		MiscFlags,
		SampleCount,
		SampleQuality,
		SubResPtr,
		SubResPitch,
		SubResSlicePitch);
}

void VerifyD3D11ResizeViewportResult(
	HRESULT D3DResult,
	const ANSICHAR* Code,
	const ANSICHAR* Filename,
	uint32 Line,
	const FD3D11ResizeViewportState& OldState,
	const FD3D11ResizeViewportState& NewState,
	ID3D11Device* Device)
{
	check(FAILED(D3DResult));

	const FString ErrorString = GetD3D11ErrorString(D3DResult, 0);
	const TCHAR* OldStateFormat = GetD3D11TextureFormatString(OldState.Format);
	const TCHAR* NewStateFormat = GetD3D11TextureFormatString(NewState.Format);

	UE_LOG(LogD3D11RHI, Error,
		TEXT("%s failed \n at %s:%u \n with error %s, \n (Size=%ix%i Fullscreen=%d Format=%s(0x%08X)) -> (Size=%ix%i Fullscreen=%d Format=%s(0x%08X))"),
		ANSI_TO_TCHAR(Code),
		ANSI_TO_TCHAR(Filename),
		Line,
		*ErrorString,
		OldState.SizeX,
		OldState.SizeY,
		OldState.bIsFullscreen ? 1 : 0,
		OldStateFormat,
		OldState.Format,
		NewState.SizeX,
		NewState.SizeY,
		NewState.bIsFullscreen ? 1 : 0,
		NewStateFormat,
		NewState.Format);

	TerminateOnDeviceRemoved(D3DResult, Device);
	TerminateOnOutOfMemory(D3DResult, true);

	UE_LOG(LogD3D11RHI, Fatal,
		TEXT("%s failed \n at %s:%u \n with error %s, \n (Size=%ix%i Fullscreen=%d Format=%s(0x%08X)) -> (Size=%ix%i Fullscreen=%d Format=%s(0x%08X))"),
		ANSI_TO_TCHAR(Code),
		ANSI_TO_TCHAR(Filename),
		Line,
		*ErrorString,
		OldState.SizeX,
		OldState.SizeY,
		OldState.bIsFullscreen ? 1 : 0,
		OldStateFormat,
		OldState.Format,
		NewState.SizeX,
		NewState.SizeY,
		NewState.bIsFullscreen ? 1 : 0,
		NewStateFormat,
		NewState.Format);
}

void VerifyComRefCount(IUnknown* Object,int32 ExpectedRefs,const TCHAR* Code,const TCHAR* Filename,int32 Line)
{
	int32 NumRefs;

	if (Object)
	{
		Object->AddRef();
		NumRefs = Object->Release();

		checkSlow(NumRefs == ExpectedRefs);

		if (NumRefs != ExpectedRefs)
		{
			UE_LOG(
				LogD3D11RHI,
				Error,
				TEXT("%s:(%d): %s has %d refs, expected %d"),
				Filename,
				Line,
				Code,
				NumRefs,
				ExpectedRefs
				);
		}
	}
}

FD3D11BoundRenderTargets::FD3D11BoundRenderTargets(ID3D11DeviceContext* InDeviceContext)
{
	FMemory::Memzero(RenderTargetViews,sizeof(RenderTargetViews));
	DepthStencilView = NULL;
	InDeviceContext->OMGetRenderTargets(
		MaxSimultaneousRenderTargets,
		&RenderTargetViews[0],
		&DepthStencilView
		);

	// Find the last non-null rendertarget to determine the max 
	// We traverse the array backwards, since they can be sparse
	for (NumActiveTargets = MaxSimultaneousRenderTargets; NumActiveTargets > 0; --NumActiveTargets)
	{
		if (RenderTargetViews[NumActiveTargets-1] != NULL)
		{
			break;
		}
	}
}

FD3D11BoundRenderTargets::~FD3D11BoundRenderTargets()
{
	// OMGetRenderTargets calls AddRef on each RTV/DSV it returns. We need
	// to make a corresponding call to Release.
	for (int32 TargetIndex = 0; TargetIndex < NumActiveTargets; ++TargetIndex)
	{
		if (RenderTargetViews[TargetIndex] != nullptr)
		{
			RenderTargetViews[TargetIndex]->Release();
		}
	}
	if (DepthStencilView)
	{
		DepthStencilView->Release();
	}
}


//
// Stat declarations.
//


DEFINE_STAT(STAT_D3D11PresentTime);
DEFINE_STAT(STAT_D3D11CustomPresentTime);
DEFINE_STAT(STAT_D3D11TexturesAllocated);
DEFINE_STAT(STAT_D3D11TexturesReleased);
DEFINE_STAT(STAT_D3D11ClearShaderResourceTime);
DEFINE_STAT(STAT_D3D11CreateTextureTime);
DEFINE_STAT(STAT_D3D11LockTextureTime);
DEFINE_STAT(STAT_D3D11UnlockTextureTime);
DEFINE_STAT(STAT_D3D11CopyTextureTime);
DEFINE_STAT(STAT_D3D11NewBoundShaderStateTime);
DEFINE_STAT(STAT_D3D11CreateBoundShaderStateTime);
DEFINE_STAT(STAT_D3D11CleanUniformBufferTime);
DEFINE_STAT(STAT_D3D11UpdateUniformBufferTime);
DEFINE_STAT(STAT_D3D11TexturePoolMemory);
DEFINE_STAT(STAT_D3D11FreeUniformBufferMemory);
DEFINE_STAT(STAT_D3D11NumFreeUniformBuffers);
DEFINE_STAT(STAT_D3D11NumImmutableUniformBuffers);
DEFINE_STAT(STAT_D3D11NumBoundShaderState);
DEFINE_STAT(STAT_D3D11RenderTargetCommits);
DEFINE_STAT(STAT_D3D11RenderTargetCommitsUAV);

#undef LOCTEXT_NAMESPACE
