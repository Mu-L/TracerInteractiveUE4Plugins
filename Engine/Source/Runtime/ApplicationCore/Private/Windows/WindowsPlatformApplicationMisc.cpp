// Copyright Epic Games, Inc. All Rights Reserved.

#include "Windows/WindowsPlatformApplicationMisc.h"
#include "Windows/WindowsApplication.h"
#include "Windows/WindowsApplicationErrorOutputDevice.h"
#include "Windows/WindowsConsoleOutputDevice.h"
#include "Windows/WindowsFeedbackContext.h"
#include "HAL/FeedbackContextAnsi.h"
#include "Misc/App.h"
#include "Math/Color.h"
#include "Windows/WindowsHWrapper.h"
#include "Modules/ModuleManager.h"
#include "Misc/CoreDelegates.h"
#include "Windows/WindowsPlatformOutputDevices.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"

// Resource includes.
#include "Runtime/Launch/Resources/Windows/Resource.h"

typedef HRESULT(STDAPICALLTYPE *GetDpiForMonitorProc)(HMONITOR Monitor, int32 DPIType, uint32 *DPIX, uint32 *DPIY);
APPLICATIONCORE_API GetDpiForMonitorProc GetDpiForMonitor;

void FWindowsPlatformApplicationMisc::LoadPreInitModules()
{
	// D3D11 is not supported on WinXP, so in this case we use the OpenGL RHI
	if(FPlatformMisc::VerifyWindowsVersion(6, 0))
	{
		//#todo-rco: Only try on Win10
		const bool bForceD3D12 = FParse::Param(FCommandLine::Get(), TEXT("d3d12")) || FParse::Param(FCommandLine::Get(), TEXT("dx12"));
		if (bForceD3D12)
		{
			FModuleManager::Get().LoadModule(TEXT("D3D12RHI"));
		}
		FModuleManager::Get().LoadModule(TEXT("D3D11RHI"));
	}
	FModuleManager::Get().LoadModule(TEXT("OpenGLDrv"));
}

void FWindowsPlatformApplicationMisc::LoadStartupModules()
{
#if !UE_SERVER
	FModuleManager::Get().LoadModule(TEXT("XAudio2"));
	FModuleManager::Get().LoadModule(TEXT("HeadMountedDisplay"));
#endif // !UE_SERVER

#if WITH_EDITOR
	FModuleManager::Get().LoadModule(TEXT("SourceCodeAccess"));
#endif	//WITH_EDITOR
}

class FOutputDeviceConsole* FWindowsPlatformApplicationMisc::CreateConsoleOutputDevice()
{
	// this is a slightly different kind of singleton that gives ownership to the caller and should not be called more than once
	return new FWindowsConsoleOutputDevice();
}

class FOutputDeviceError* FWindowsPlatformApplicationMisc::GetErrorOutputDevice()
{
	static FWindowsApplicationErrorOutputDevice Singleton;
	return &Singleton;
}

class FFeedbackContext* FWindowsPlatformApplicationMisc::GetFeedbackContext()
{
#if WITH_EDITOR
	static FWindowsFeedbackContext Singleton;
	return &Singleton;
#else
	return FPlatformOutputDevices::GetFeedbackContext();
#endif
}

GenericApplication* FWindowsPlatformApplicationMisc::CreateApplication()
{
	HICON AppIconHandle = LoadIcon( hInstance, MAKEINTRESOURCE( GetAppIcon() ) );
	if( AppIconHandle == NULL )
	{
		AppIconHandle = LoadIcon( (HINSTANCE)NULL, IDI_APPLICATION ); 
	}

	return FWindowsApplication::CreateWindowsApplication( hInstance, AppIconHandle );
}

void FWindowsPlatformApplicationMisc::RequestMinimize()
{
	::ShowWindow(::GetActiveWindow(), SW_MINIMIZE);
}

bool FWindowsPlatformApplicationMisc::IsThisApplicationForeground()
{
	uint32 ForegroundProcess;
	::GetWindowThreadProcessId(GetForegroundWindow(), (::DWORD *)&ForegroundProcess);
	return (ForegroundProcess == GetCurrentProcessId());
}

int32 FWindowsPlatformApplicationMisc::GetAppIcon()
{
	return IDICON_UE4Game;
}

static void WinPumpMessages()
{
	{
		MSG Msg;
		while( PeekMessage(&Msg,NULL,0,0,PM_REMOVE) )
		{
			TranslateMessage( &Msg );
			DispatchMessage( &Msg );
		}
	}
}


void FWindowsPlatformApplicationMisc::PumpMessages(bool bFromMainLoop)
{
	TSharedPtr<void> RevertGlobalFlag;
	if (!GPumpingMessages)
	{
		GPumpingMessages = true;
		RevertGlobalFlag = MakeShareable<void>(nullptr, [](auto) {GPumpingMessages = false; });
	}

	if (!bFromMainLoop)
	{
		FPlatformMisc::PumpMessagesOutsideMainLoop();
		return;
	}

	GPumpingMessagesOutsideOfMainLoop = false;
	WinPumpMessages();

	// Determine if application has focus
	bool HasFocus = FApp::UseVRFocus() ? FApp::HasVRFocus() : FWindowsPlatformApplicationMisc::IsThisApplicationForeground();
	static bool HadFocus = false;

#if WITH_EDITOR
	// If editor thread doesn't have the focus, don't suck up too much CPU time.
	if( GIsEditor )
	{
		if( HadFocus && !HasFocus )
		{
			// Drop our priority to speed up whatever is in the foreground.
			SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL );
		}
		else if( HasFocus && !HadFocus )
		{
			// Boost our priority back to normal.
			SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_NORMAL );
		}
		if( !HasFocus )
		{
			// Sleep for a bit to not eat up all CPU time.
			FPlatformProcess::Sleep(0.005f);
		}
		HadFocus = HasFocus;
	}
#endif

#if !UE_SERVER
	// For non-editor clients, record if the active window is in focus
	if( HadFocus != HasFocus )
	{
		FGenericCrashContext::SetEngineData(TEXT("Platform.AppHasFocus"), HasFocus ? TEXT("true") : TEXT("false"));
	}
#endif

	HadFocus = HasFocus;

	// if its our window, allow sound, otherwise apply multiplier
	FApp::SetVolumeMultiplier( HasFocus ? 1.0f : FApp::GetUnfocusedVolumeMultiplier() );
}

void FWindowsPlatformApplicationMisc::PreventScreenSaver()
{
	INPUT Input = { 0 };
	Input.type = INPUT_MOUSE;
	Input.mi.dx = 0;
	Input.mi.dy = 0;	
	Input.mi.mouseData = 0;
	Input.mi.dwFlags = MOUSEEVENTF_MOVE;
	Input.mi.time = 0;
	Input.mi.dwExtraInfo = 0; 	
	SendInput(1,&Input,sizeof(INPUT));
}

FLinearColor FWindowsPlatformApplicationMisc::GetScreenPixelColor(const FVector2D& InScreenPos, float /*InGamma*/)
{
	COLORREF PixelColorRef = GetPixel(GetDC(HWND_DESKTOP), InScreenPos.X, InScreenPos.Y);

	FColor sRGBScreenColor(
		(PixelColorRef & 0xFF),
		((PixelColorRef & 0xFF00) >> 8),
		((PixelColorRef & 0xFF0000) >> 16),
		255);

	// Assume the screen color is coming in as sRGB space
	return FLinearColor(sRGBScreenColor);
}

void FWindowsPlatformApplicationMisc::SetHighDPIMode()
{
	if (IsHighDPIAwarenessEnabled())
	{
		if (void* ShCoreDll = FPlatformProcess::GetDllHandle(TEXT("shcore.dll")))
		{
			typedef enum _PROCESS_DPI_AWARENESS {
				PROCESS_DPI_UNAWARE = 0,
				PROCESS_SYSTEM_DPI_AWARE = 1,
				PROCESS_PER_MONITOR_DPI_AWARE = 2
			} PROCESS_DPI_AWARENESS;

			typedef HRESULT(STDAPICALLTYPE *SetProcessDpiAwarenessProc)(PROCESS_DPI_AWARENESS Value);
			SetProcessDpiAwarenessProc SetProcessDpiAwareness = (SetProcessDpiAwarenessProc)FPlatformProcess::GetDllExport(ShCoreDll, TEXT("SetProcessDpiAwareness"));
			GetDpiForMonitor = (GetDpiForMonitorProc)FPlatformProcess::GetDllExport(ShCoreDll, TEXT("GetDpiForMonitor"));

			typedef HRESULT(STDAPICALLTYPE *GetProcessDpiAwarenessProc)(HANDLE hProcess, PROCESS_DPI_AWARENESS* Value);
			GetProcessDpiAwarenessProc GetProcessDpiAwareness = (GetProcessDpiAwarenessProc)FPlatformProcess::GetDllExport(ShCoreDll, TEXT("GetProcessDpiAwareness"));

			if (SetProcessDpiAwareness && GetProcessDpiAwareness && !IsRunningCommandlet() && !FApp::IsUnattended())
			{
				PROCESS_DPI_AWARENESS CurrentAwareness = PROCESS_DPI_UNAWARE;

				GetProcessDpiAwareness(nullptr, &CurrentAwareness);

				if (CurrentAwareness != PROCESS_PER_MONITOR_DPI_AWARE)
				{
					UE_LOG(LogInit, Log, TEXT("Setting process to per monitor DPI aware"));
					HRESULT Hr = SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE); // PROCESS_PER_MONITOR_DPI_AWARE_VALUE
																						// We dont care about this warning if we are in any kind of headless mode
					if (Hr != S_OK)
					{
						UE_LOG(LogInit, Warning, TEXT("SetProcessDpiAwareness failed.  Error code %x"), Hr);
					}
				}
			}

			FPlatformProcess::FreeDllHandle(ShCoreDll);
		}
		else if (void* User32Dll = FPlatformProcess::GetDllHandle(TEXT("user32.dll")))
		{
			typedef BOOL(WINAPI *SetProcessDpiAwareProc)(void);
			SetProcessDpiAwareProc SetProcessDpiAware = (SetProcessDpiAwareProc)FPlatformProcess::GetDllExport(User32Dll, TEXT("SetProcessDPIAware"));

			if (SetProcessDpiAware && !IsRunningCommandlet() && !FApp::IsUnattended())
			{
				UE_LOG(LogInit, Log, TEXT("Setting process to DPI aware"));

				BOOL Result = SetProcessDpiAware();
				if (Result == 0)
				{
					UE_LOG(LogInit, Warning, TEXT("SetProcessDpiAware failed"));
				}
			}

			FPlatformProcess::FreeDllHandle(User32Dll);
		}
	}
}

bool FWindowsPlatformApplicationMisc::GetWindowTitleMatchingText(const TCHAR* TitleStartsWith, FString& OutTitle)
{
	bool bWasFound = false;
	WCHAR Buffer[8192];
	// Get the first window so we can start walking the window chain
	HWND hWnd = FindWindowW(NULL,NULL);
	if (hWnd != NULL)
	{
		size_t TitleStartsWithLen = _tcslen(TitleStartsWith);
		do
		{
			GetWindowText(hWnd,Buffer,8192);
			// If this matches, then grab the full text
			if (_tcsnccmp(TitleStartsWith, Buffer, TitleStartsWithLen) == 0)
			{
				OutTitle = Buffer;
				hWnd = NULL;
				bWasFound = true;
			}
			else
			{
				// Get the next window to interrogate
				hWnd = GetWindow(hWnd, GW_HWNDNEXT);
			}
		}
		while (hWnd != NULL);
	}
	return bWasFound;
}

int32 FWindowsPlatformApplicationMisc::GetMonitorDPI(const FMonitorInfo& MonitorInfo)
{
	int32 DisplayDPI = 96;

	if (IsHighDPIAwarenessEnabled())
	{
		if (GetDpiForMonitor)
		{
			RECT MonitorDim;
			MonitorDim.left = MonitorInfo.DisplayRect.Left;
			MonitorDim.top = MonitorInfo.DisplayRect.Top;
			MonitorDim.right = MonitorInfo.DisplayRect.Right;
			MonitorDim.bottom = MonitorInfo.DisplayRect.Bottom;

			HMONITOR Monitor = MonitorFromRect(&MonitorDim, MONITOR_DEFAULTTONEAREST);
			if (Monitor)
			{
				uint32 DPIX = 0;
				uint32 DPIY = 0;
				if (SUCCEEDED(GetDpiForMonitor(Monitor, 0 /*MDT_EFFECTIVE_DPI*/, &DPIX, &DPIY)))
				{
					DisplayDPI = DPIX;
				}
			}
		}
		else
		{
			HDC Context = GetDC(nullptr);
			DisplayDPI = GetDeviceCaps(Context, LOGPIXELSX);
			ReleaseDC(nullptr, Context);
		}
	}

	return DisplayDPI;
}

float FWindowsPlatformApplicationMisc::GetDPIScaleFactorAtPoint(float X, float Y)
{
	float Scale = 1.0f;

	if (IsHighDPIAwarenessEnabled())
	{
		if (GetDpiForMonitor)
		{
			POINT Position = { static_cast<LONG>(X), static_cast<LONG>(Y) };
			HMONITOR Monitor = MonitorFromPoint(Position, MONITOR_DEFAULTTONEAREST);
			if (Monitor)
			{
				uint32 DPIX = 0;
				uint32 DPIY = 0;
				if (SUCCEEDED(GetDpiForMonitor(Monitor, 0 /*MDT_EFFECTIVE_DPI*/, &DPIX, &DPIY)))
				{
					Scale = (float)DPIX / 96.0f;
				}
			}
		}
		else
		{
			HDC Context = GetDC(nullptr);
			int32 DPI = GetDeviceCaps(Context, LOGPIXELSX);
			Scale = (float)DPI / 96.0f;
			ReleaseDC(nullptr, Context);
		}
	}

	return Scale;
}

// Disabling optimizations helps to reduce the frequency of OpenClipboard failing with error code 0. It still happens
// though only with really large text buffers and we worked around this by changing the editor to use an intermediate
// text buffer for internal operations.
PRAGMA_DISABLE_OPTIMIZATION 

void FWindowsPlatformApplicationMisc::ClipboardCopy(const TCHAR* Str)
{
	if( OpenClipboard(GetActiveWindow()) )
	{
		verify(EmptyClipboard());
		HGLOBAL GlobalMem;
		int32 StrLen = FCString::Strlen(Str);
		GlobalMem = GlobalAlloc( GMEM_MOVEABLE, sizeof(TCHAR)*(StrLen+1) );
		check(GlobalMem);
		TCHAR* Data = (TCHAR*) GlobalLock( GlobalMem );
		FCString::Strcpy( Data, (StrLen+1), Str );
		GlobalUnlock( GlobalMem );
		if( SetClipboardData( CF_UNICODETEXT, GlobalMem ) == NULL )
			UE_LOG(LogWindows, Fatal,TEXT("SetClipboardData failed with error code %i"), (uint32)GetLastError() );
		verify(CloseClipboard());
	}
}

void FWindowsPlatformApplicationMisc::ClipboardPaste(class FString& Result)
{
	if( OpenClipboard(GetActiveWindow()) )
	{
		HGLOBAL GlobalMem = NULL;
		bool Unicode = 0;
		GlobalMem = GetClipboardData( CF_UNICODETEXT );
		Unicode = 1;
		if( !GlobalMem )
		{
			GlobalMem = GetClipboardData( CF_TEXT );
			Unicode = 0;
		}
		if( !GlobalMem )
		{
			Result = TEXT("");
		}
		else
		{
			void* Data = GlobalLock( GlobalMem );
			check( Data );	
			if( Unicode )
				Result = (TCHAR*) Data;
			else
			{
				ANSICHAR* ACh = (ANSICHAR*) Data;
				int32 i;
				for( i=0; ACh[i]; i++ );
				TArray<TCHAR> Ch;
				Ch.AddUninitialized(i+1);
				for( i=0; i<Ch.Num(); i++ )
					Ch[i]=CharCast<TCHAR>(ACh[i]);
				Result.GetCharArray() = MoveTemp(Ch);
			}
			GlobalUnlock( GlobalMem );
		}
		verify(CloseClipboard());
	}
	else 
	{
		Result=TEXT("");
	}
}

PRAGMA_ENABLE_OPTIMIZATION 
