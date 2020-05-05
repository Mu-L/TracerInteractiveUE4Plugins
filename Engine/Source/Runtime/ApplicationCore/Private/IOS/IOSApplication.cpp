// Copyright Epic Games, Inc. All Rights Reserved.

#include "IOS/IOSApplication.h"
#include "IOS/IOSInputInterface.h"
#include "IOS/IOSCursor.h"
#include "IOSWindow.h"
#include "Misc/CoreDelegates.h"
#include "IOS/IOSAppDelegate.h"
#include "IInputDeviceModule.h"
#include "GenericPlatform/IInputInterface.h"
#include "IInputDevice.h"
#include "Misc/ScopeLock.h"
#include "HAL/IConsoleManager.h"
#include "IOS/IOSAsyncTask.h"
#include "Stats/Stats.h"
#if WITH_ACCESSIBILITY
#include "IOS/Accessibility/IOSAccessibilityCache.h"
#include "IOS/Accessibility/IOSAccessibilityElement.h"
#endif

FIOSApplication* FIOSApplication::CreateIOSApplication()
{
	SCOPED_BOOT_TIMING("FIOSApplication::CreateIOSApplication");
	return new FIOSApplication();
}

FIOSApplication::FIOSApplication()
	: GenericApplication(MakeShareable(new FIOSCursor()))
	, InputInterface( FIOSInputInterface::Create( MessageHandler ) )
	, bHasLoadedInputPlugins(false)
{
	[IOSAppDelegate GetDelegate].IOSApplication = this;
}

void FIOSApplication::InitializeWindow( const TSharedRef< FGenericWindow >& InWindow, const TSharedRef< FGenericWindowDefinition >& InDefinition, const TSharedPtr< FGenericWindow >& InParent, const bool bShowImmediately )
{
	const TSharedRef< FIOSWindow > Window = StaticCastSharedRef< FIOSWindow >( InWindow );
	const TSharedPtr< FIOSWindow > ParentWindow = StaticCastSharedPtr< FIOSWindow >( InParent );

	Windows.Add( Window );
	Window->Initialize( this, InDefinition, ParentWindow, bShowImmediately );
}

void FIOSApplication::SetMessageHandler( const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler )
{
	GenericApplication::SetMessageHandler(InMessageHandler);
	InputInterface->SetMessageHandler(InMessageHandler);

	TArray<IInputDeviceModule*> PluginImplementations = IModularFeatures::Get().GetModularFeatureImplementations<IInputDeviceModule>(IInputDeviceModule::GetModularFeatureName());
	for (auto DeviceIt = ExternalInputDevices.CreateIterator(); DeviceIt; ++DeviceIt)
	{
		(*DeviceIt)->SetMessageHandler(InMessageHandler);
	}
}

#if WITH_ACCESSIBILITY
void FIOSApplication::SetAccessibleMessageHandler(const TSharedRef<FGenericAccessibleMessageHandler>& InAccessibleMessageHandler)
{
	GenericApplication::SetAccessibleMessageHandler(InAccessibleMessageHandler);
	InAccessibleMessageHandler->SetAccessibleEventDelegate(FGenericAccessibleMessageHandler::FAccessibleEvent::CreateRaw(this, &FIOSApplication::OnAccessibleEventRaised));
	InAccessibleMessageHandler->SetActive(UIAccessibilityIsVoiceOverRunning());
}
#endif

void FIOSApplication::AddExternalInputDevice(TSharedPtr<IInputDevice> InputDevice)
{
	if (InputDevice.IsValid())
	{
		ExternalInputDevices.Add(InputDevice);
	}
}

void FIOSApplication::PollGameDeviceState( const float TimeDelta )
{
	// initialize any externally-implemented input devices (we delay load initialize the array so any plugins have had time to load)
	if (!bHasLoadedInputPlugins)
	{
		TArray<IInputDeviceModule*> PluginImplementations = IModularFeatures::Get().GetModularFeatureImplementations<IInputDeviceModule>(IInputDeviceModule::GetModularFeatureName());
		for (auto InputPluginIt = PluginImplementations.CreateIterator(); InputPluginIt; ++InputPluginIt)
		{
			TSharedPtr<IInputDevice> Device = (*InputPluginIt)->CreateInputDevice(MessageHandler);
			AddExternalInputDevice(Device);
		}

		bHasLoadedInputPlugins = true;
	}

	// Poll game device state and send new events
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_IOSApplication_InputInterface_Tick);
		InputInterface->Tick(TimeDelta);
	}
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_IOSApplication_InputInterface_SendControllerEvents);
		InputInterface->SendControllerEvents();
	}

	// Poll externally-implemented devices
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_IOSApplication_ExternalInputDevice);
		for (auto DeviceIt = ExternalInputDevices.CreateIterator(); DeviceIt; ++DeviceIt)
		{
			(*DeviceIt)->Tick(TimeDelta);
			(*DeviceIt)->SendControllerEvents();
		}
	}
}

FPlatformRect FIOSApplication::GetWorkArea( const FPlatformRect& CurrentWindow ) const
{
	return FIOSWindow::GetScreenRect();
}

static TAutoConsoleVariable<float> CVarSafeZone_Landscape_Left(TEXT("SafeZone.Landscape.Left"), -1.0f, TEXT("Safe Zone - Landscape - Left"));
static TAutoConsoleVariable<float> CVarSafeZone_Landscape_Top(TEXT("SafeZone.Landscape.Top"), -1.0f, TEXT("Safe Zone - Landscape - Top"));
static TAutoConsoleVariable<float> CVarSafeZone_Landscape_Right(TEXT("SafeZone.Landscape.Right"), -1.0f, TEXT("Safe Zone - Landscape - Right"));
static TAutoConsoleVariable<float> CVarSafeZone_Landscape_Bottom(TEXT("SafeZone.Landscape.Bottom"), -1.0f, TEXT("Safe Zone - Landscape - Bottom"));

#if !PLATFORM_TVOS
UIInterfaceOrientation CachedOrientation = UIInterfaceOrientationPortrait;
UIEdgeInsets CachedInsets;
#endif

void FDisplayMetrics::RebuildDisplayMetrics(FDisplayMetrics& OutDisplayMetrics)
{
	// Get screen rect
	OutDisplayMetrics.PrimaryDisplayWorkAreaRect = FIOSWindow::GetScreenRect();
	OutDisplayMetrics.VirtualDisplayRect = OutDisplayMetrics.PrimaryDisplayWorkAreaRect;

	// Total screen size of the primary monitor
	OutDisplayMetrics.PrimaryDisplayWidth = OutDisplayMetrics.PrimaryDisplayWorkAreaRect.Right - OutDisplayMetrics.PrimaryDisplayWorkAreaRect.Left;
	OutDisplayMetrics.PrimaryDisplayHeight = OutDisplayMetrics.PrimaryDisplayWorkAreaRect.Bottom - OutDisplayMetrics.PrimaryDisplayWorkAreaRect.Top;

	// Get ui window rect
	OutDisplayMetrics.IosUiWindowAreaRect = FIOSWindow::GetUIWindowRect();

#if !PLATFORM_TVOS
	if (@available(iOS 11, *))
	{
		const float RequestedContentScaleFactor = [[IOSAppDelegate GetDelegate].IOSView contentScaleFactor];

		//we need to set these according to the orientation
		TAutoConsoleVariable<float>* CVar_Left = nullptr;
		TAutoConsoleVariable<float>* CVar_Top = nullptr;
		TAutoConsoleVariable<float>* CVar_Right = nullptr;
		TAutoConsoleVariable<float>* CVar_Bottom = nullptr;

		//making an assumption that the "normal" landscape mode is Landscape right
		if (CachedOrientation == UIInterfaceOrientationLandscapeLeft)
		{
			CVar_Left = &CVarSafeZone_Landscape_Left;
			CVar_Right = &CVarSafeZone_Landscape_Right;
            CVar_Top = &CVarSafeZone_Landscape_Top;
            CVar_Bottom = &CVarSafeZone_Landscape_Bottom;
		}
		else if (CachedOrientation == UIInterfaceOrientationLandscapeRight)
		{
			CVar_Left = &CVarSafeZone_Landscape_Right;
			CVar_Right = &CVarSafeZone_Landscape_Left;
            CVar_Top = &CVarSafeZone_Landscape_Top;
            CVar_Bottom = &CVarSafeZone_Landscape_Bottom;
		}
        
		// of the CVars are set, use their values. If not, use what comes from iOS
		const float Inset_Left = (!CVar_Left || CVar_Left->AsVariable()->GetFloat() < 0.0f) ? CachedInsets.left : CVar_Left->AsVariable()->GetFloat();
		const float Inset_Top = (!CVar_Top || CVar_Top->AsVariable()->GetFloat() < 0.0f) ? CachedInsets.top : CVar_Top->AsVariable()->GetFloat();
		const float Inset_Right = (!CVar_Right || CVar_Right->AsVariable()->GetFloat() < 0.0f) ? CachedInsets.right : CVar_Right->AsVariable()->GetFloat();
		const float Inset_Bottom = (!CVar_Bottom || CVar_Bottom->AsVariable()->GetFloat() < 0.0f) ? CachedInsets.bottom : CVar_Bottom->AsVariable()->GetFloat();

		//setup the asymmetrical padding
		OutDisplayMetrics.TitleSafePaddingSize.X = Inset_Left;
		OutDisplayMetrics.TitleSafePaddingSize.Y = Inset_Top;
		OutDisplayMetrics.TitleSafePaddingSize.Z = Inset_Right;
		OutDisplayMetrics.TitleSafePaddingSize.W = Inset_Bottom;

		//scale the thing
		OutDisplayMetrics.TitleSafePaddingSize *= RequestedContentScaleFactor;

		OutDisplayMetrics.ActionSafePaddingSize = OutDisplayMetrics.TitleSafePaddingSize;
	}
	else
#endif
	{
		OutDisplayMetrics.ApplyDefaultSafeZones();
	}
}

void FIOSApplication::CacheDisplayMetrics()
	{

#if !PLATFORM_TVOS
	if (@available(iOS 11, *))
	{
		CachedInsets = [[[[UIApplication sharedApplication] delegate] window] safeAreaInsets];
		CachedOrientation = [[UIApplication sharedApplication] statusBarOrientation];
	}
#endif
}

TSharedRef< FGenericWindow > FIOSApplication::MakeWindow()
{
	return FIOSWindow::Make();
}

#if !PLATFORM_TVOS
void FIOSApplication::OrientationChanged(UIInterfaceOrientation orientation)
{
	// this is called on the IOS thread. It turns out that it's possible for the resolution to change AGAIN by the time the game
	// thread processes the resize, so we queue up the current size from the ios thread, send that to the RHI to resize to that
	// (and no longer checking if it matches current frame size, because it may not). If another resize happens, that new size
	// will get queued up here and sent to RHI, so eventually the size will be correct
	FPlatformRect WindowRect = FIOSWindow::GetScreenRect();
	int32 WindowWidth = WindowRect.Right - WindowRect.Left;
	int32 WindowHeight = WindowRect.Bottom - WindowRect.Top;

	// queue up the size as we see it now all the way to the RHI 
	[FIOSAsyncTask CreateTaskWithBlock : ^ bool(void)
	 {
	 	FIOSApplication* App = [IOSAppDelegate GetDelegate].IOSApplication;
	 
		App->GetMessageHandler()->OnSizeChanged(App->Windows[0],WindowWidth,WindowHeight, false);
		App->GetMessageHandler()->OnResizingWindow(App->Windows[0]);
		App->CacheDisplayMetrics();
		FDisplayMetrics DisplayMetrics;
		FDisplayMetrics::RebuildDisplayMetrics(DisplayMetrics);
		App->BroadcastDisplayMetricsChanged(DisplayMetrics);
		FCoreDelegates::OnSafeFrameChangedEvent.Broadcast();
	 	return true;
	 }];
}
#endif


bool FIOSApplication::IsGamepadAttached() const
{
    if (InputInterface.IsValid())
	{
		return InputInterface->IsGamepadAttached();
	}

	return false;
}

TSharedRef<FIOSWindow> FIOSApplication::FindWindowByAppDelegateView()
{
	for (const TSharedRef<FIOSWindow>& Window : Windows)
	{
		if ([IOSAppDelegate GetDelegate].Window == static_cast<UIWindow*>(Window->GetOSWindowHandle()))
		{
			return Window;
		}
	}
    check(false);
    return Windows[0];
}

#if WITH_ACCESSIBILITY
void FIOSApplication::OnAccessibleEventRaised(TSharedRef<IAccessibleWidget> Widget, EAccessibleEvent Event, FVariant OldValue, FVariant NewValue)
{
	// This should only be triggered by the accessible message handler which initiates from the Slate thread.
	check(IsInGameThread());

	const AccessibleWidgetId Id = Widget->GetId();
	switch (Event)
	{
	case EAccessibleEvent::ParentChanged:
	{
		dispatch_async(dispatch_get_main_queue(), ^{
			[[[FIOSAccessibilityCache AccessibilityElementCache] GetAccessibilityElement:Id] SetParent:NewValue.GetValue<AccessibleWidgetId>()];
		});
		// LayoutChanged is to indicate things like "a widget became visible or hidden" while
		// ScreenChanged is for large-scale UI changes. It can potentially take an NSString to read
		// to the user when this happens, if we choose to support that.
		UIAccessibilityPostNotification(UIAccessibilityLayoutChangedNotification, nil);
		break;
	}
	case EAccessibleEvent::WidgetRemoved:
		dispatch_async(dispatch_get_main_queue(), ^{
			[[FIOSAccessibilityCache AccessibilityElementCache] RemoveAccessibilityElement:Id];
		});
		break;
	}
}
#endif
