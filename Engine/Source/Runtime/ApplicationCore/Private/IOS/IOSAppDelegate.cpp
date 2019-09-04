// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "IOS/IOSAppDelegate.h"
#include "IOS/IOSCommandLineHelper.h"
#include "HAL/ExceptionHandling.h"
#include "Modules/Boilerplate/ModuleBoilerplate.h"
#include "Misc/CallbackDevice.h"
#include "IOS/IOSView.h"
#include "IOS/IOSWindow.h"
#include "Async/TaskGraphInterfaces.h"
#include "GenericPlatform/GenericPlatformChunkInstall.h"
#include "IOS/IOSPlatformMisc.h"
#include "IOS/IOSBackgroundURLSessionHandler.h"
#include "HAL/PlatformStackWalk.h"
#include "HAL/PlatformProcess.h"
#include "Misc/OutputDeviceError.h"
#include "Misc/CommandLine.h"
#include "IOS/IOSPlatformFramePacer.h"
#include "IOS/IOSApplication.h"
#include "IOS/IOSAsyncTask.h"
#include "Misc/ConfigCacheIni.h"
#include "IOS/IOSPlatformCrashContext.h"
#include "Misc/OutputDeviceError.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/FeedbackContext.h"
#include "Misc/App.h"
#include "Algo/AllOf.h"
#include "Misc/App.h"
#include "Misc/EmbeddedCommunication.h"
#if USE_MUTE_SWITCH_DETECTION
#include "SharkfoodMuteSwitchDetector.h"
#include "SharkfoodMuteSwitchDetector.m"
#endif

#include <AudioToolbox/AudioToolbox.h>
#include <AVFoundation/AVAudioSession.h>
#include "HAL/IConsoleManager.h"

#if WITH_ACCESSIBILITY
#include "IOS/Accessibility/IOSAccessibilityCache.h"
#endif

// this is the size of the game thread stack, it must be a multiple of 4k
#if (UE_BUILD_SHIPPING || UE_BUILD_TEST)
#define GAME_THREAD_STACK_SIZE 2 * 1024 * 1024
#else
#define GAME_THREAD_STACK_SIZE 16 * 1024 * 1024
#endif

DEFINE_LOG_CATEGORY(LogIOSAudioSession);

int GAudio_ForceAmbientCategory = 1;
//static FAutoConsoleVariableRef CVar_ForceAmbientCategory(
//															  TEXT("audio.ForceAmbientCategory"),
//															  GAudio_ForceAmbientCategory,
//															  TEXT("Force the iOS AVAudioSessionCategoryAmbient category over AVAudioSessionCategorySoloAmbient")
//															  );

extern bool GShowSplashScreen;

FIOSCoreDelegates::FOnOpenURL FIOSCoreDelegates::OnOpenURL;
TArray<FIOSCoreDelegates::FFilterDelegateAndHandle> FIOSCoreDelegates::PushNotificationFilters;

static bool GEnabledAudioFeatures[(uint8)EAudioFeature::NumFeatures];

/*
	From: https://developer.apple.com/library/ios/documentation/UIKit/Reference/UIApplicationDelegate_Protocol/#//apple_ref/occ/intfm/UIApplicationDelegate/applicationDidEnterBackground:
	"In practice, you should return from applicationDidEnterBackground: as quickly as possible. If the method does not return before time runs out your app is terminated and purged from memory."
*/

static float GOverrideThreadWaitTime = 0.0;
static float GMaxThreadWaitTime = 2.0;	// Setting this to be 2 seconds since this wait has to be done twice (once for sending the enter background event to the game thread, and another for waiting on the suspend msg
										// I could not find a reference for this but in the past I believe the timeout was 5 seconds
FAutoConsoleVariableRef CVarThreadBlockTime(
		TEXT("ios.lifecycleblocktime"),
		GMaxThreadWaitTime,
		TEXT("How long to block main IOS thread to make sure gamethread gets time.\n"),
		ECVF_Default);


static void SignalHandler(int32 Signal, struct __siginfo* Info, void* Context)
{
	static int32 bHasEntered = 0;
	if (FPlatformAtomics::InterlockedCompareExchange(&bHasEntered, 1, 0) == 0)
	{
		const SIZE_T StackTraceSize = 65535;
		ANSICHAR* StackTrace = (ANSICHAR*)FMemory::Malloc(StackTraceSize);
		StackTrace[0] = 0;
		
		// Walk the stack and dump it to the allocated memory.
		FPlatformStackWalk::StackWalkAndDump(StackTrace, StackTraceSize, 0, (ucontext_t*)Context);
		UE_LOG(LogIOS, Error, TEXT("%s"), ANSI_TO_TCHAR(StackTrace));
		FMemory::Free(StackTrace);
		
		GError->HandleError();
		FPlatformMisc::RequestExit(true);
	}
}

void InstallSignalHandlers()
{
	struct sigaction Action;
	FMemory::Memzero(&Action, sizeof(struct sigaction));
	
	Action.sa_sigaction = SignalHandler;
	sigemptyset(&Action.sa_mask);
	Action.sa_flags = SA_SIGINFO | SA_RESTART | SA_ONSTACK;
	
	sigaction(SIGQUIT, &Action, NULL);
	sigaction(SIGILL, &Action, NULL);
	sigaction(SIGEMT, &Action, NULL);
	sigaction(SIGFPE, &Action, NULL);
	sigaction(SIGBUS, &Action, NULL);
	sigaction(SIGSEGV, &Action, NULL);
	sigaction(SIGSYS, &Action, NULL);
}

FDelegateHandle FIOSCoreDelegates::AddPushNotificationFilter(const FPushNotificationFilter& FilterDel)
{
	FDelegateHandle NewHandle(FDelegateHandle::EGenerateNewHandleType::GenerateNewHandle);
	PushNotificationFilters.Push({FilterDel, NewHandle});
	return NewHandle;
}

void FIOSCoreDelegates::RemovePushNotificationFilter(FDelegateHandle Handle)
{
	PushNotificationFilters.RemoveAll([Handle](const FFilterDelegateAndHandle& Entry) {
		return Entry.Handle == Handle;
	});
}

bool FIOSCoreDelegates::PassesPushNotificationFilters(NSDictionary* Payload)
{
	return Algo::AllOf(PushNotificationFilters, [Payload](const FFilterDelegateAndHandle& Entry) {
		return Entry.Filter.Execute(Payload);
	});
}

@interface IOSAppDelegate()

// move private things from header to here

@end

@implementation IOSAppDelegate

#if !UE_BUILD_SHIPPING && !PLATFORM_TVOS
#if __IPHONE_OS_VERSION_MIN_REQUIRED < __IPHONE_9_0
	@synthesize ConsoleAlert;
#endif
#ifdef __IPHONE_8_0
    @synthesize ConsoleAlertController;
#endif
	@synthesize ConsoleHistoryValues;
	@synthesize ConsoleHistoryValuesIndex;
#endif

@synthesize AlertResponse;
@synthesize bDeviceInPortraitMode;
@synthesize bEngineInit;
@synthesize OSVersion;

@synthesize Window;
@synthesize IOSView;
@synthesize SlateController;
@synthesize timer;
@synthesize IdleTimerEnableTimer;
@synthesize IdleTimerEnablePeriod;
#if WITH_ACCESSIBILITY
@synthesize AccessibilityCacheTimer;
#endif
@synthesize savedOpenUrlParameters;
@synthesize BackgroundSessionEventCompleteDelegate;


static IOSAppDelegate* CachedDelegate = nil;

+ (IOSAppDelegate*)GetDelegate
{
#if BUILD_EMBEDDED_APP
	if (CachedDelegate == nil)
	{
		UE_LOG(LogIOS, Fatal, TEXT("Currently, a native embedding UE4 must have the AppDelegate subclass from IOSAppDelegate."));

		// if we are embedded, but CachedDelegate is nil, then that means the delegate was not an IOSAppDelegate subclass,
		// so we need to do a switcheroo - but this is unlikely to work well
#if 0 // ALLOW_NATIVE_APP_DELEGATE
		SCOPED_BOOT_TIMING("Delegate Switcheroo");

		id<UIApplicationDelegate> ExistingDelegate = [UIApplication sharedApplication].delegate;
		
		CachedDelegate = [[IOSAppDelegate alloc] init];
		CachedDelegate.Window = ExistingDelegate.window;
		
		// @todo critical: The IOSAppDelegate needs to save the old delegate, override EVERY UIApplication function, and call the old delegate's functions for each one
		[UIApplication sharedApplication].delegate = CachedDelegate;
		
		// we will have missd the didFinishLaunchingWithOptions call, so we need to call it now, but we don't have the original launchOptions, nothing we can do now!
		[CachedDelegate application:[UIApplication sharedApplication] didFinishLaunchingWithOptions:nil];
#endif
	}
#endif

	return CachedDelegate;
}

-(id)init
{
	self = [super init];
	CachedDelegate = self;
	// default to old style
	memset(GEnabledAudioFeatures, 0, sizeof(GEnabledAudioFeatures));
	return self;
}


-(void)dealloc
{
#if !UE_BUILD_SHIPPING && !PLATFORM_TVOS
#if __IPHONE_OS_VERSION_MIN_REQUIRED < __IPHONE_9_0
	[ConsoleAlert release];
#endif
#ifdef __IPHONE_8_0
	[ConsoleAlertController release];
#endif
	[ConsoleHistoryValues release];
#endif
	[Window release];
	[IOSView release];
	[SlateController release];
	[timer release];
#if WITH_ACCESSIBILITY
	if (AccessibilityCacheTimer != nil)
	{
		[AccessibilityCacheTimer invalidate];
		[AccessibilityCacheTimer release];
	}
#endif
	[super dealloc];
}

-(void)MainAppThread:(NSDictionary*)launchOptions
{
	// make sure this thread has an auto release pool setup
	NSAutoreleasePool* AutoreleasePool = [[NSAutoreleasePool alloc] init];

	{
		SCOPED_BOOT_TIMING("[IOSAppDelegate MainAppThread setup]");

		self.bHasStarted = true;
		GIsGuarded = false;
		GStartTime = FPlatformTime::Seconds();


		while(!self.bCommandLineReady)
		{
			usleep(100);
		}
	}

	FAppEntry::Init();
	
	[self InitIdleTimerSettings];

	bEngineInit = true;
    
    // put a render thread job to turn off the splash screen after the first render flip
    if (GShowSplashScreen)
    {
        FGraphEventRef SplashTask = FFunctionGraphTask::CreateAndDispatchWhenReady([]()
        {
            GShowSplashScreen = false;
        }, TStatId(), NULL, ENamedThreads::ActualRenderingThread);
    }

	for (NSDictionary* openUrlParameter in self.savedOpenUrlParameters)
	{
		UIApplication* application = [openUrlParameter valueForKey : @"application"];
		NSURL* url = [openUrlParameter valueForKey : @"url"];
		NSString * sourceApplication = [openUrlParameter valueForKey : @"sourceApplication"];
		id annotation = [openUrlParameter valueForKey : @"annotation"];
		FIOSCoreDelegates::OnOpenURL.Broadcast(application, url, sourceApplication, annotation);
	}
	self.savedOpenUrlParameters = nil; // clear after saved openurl delegate running

#if BUILD_EMBEDDED_APP
	// tell the embedded app that the while 1 loop is going
	FEmbeddedCallParamsHelper Helper;
	Helper.Command = TEXT("engineisrunning");
	FEmbeddedDelegates::GetEmbeddedToNativeParamsDelegateForSubsystem(TEXT("native")).Broadcast(Helper);
#endif

#if WITH_ACCESSIBILITY
	// Initialize accessibility code if VoiceOver is enabled. This must happen after Slate has been initialized.
	dispatch_async(dispatch_get_main_queue(), ^{
		if (UIAccessibilityIsVoiceOverRunning())
		{
			[[IOSAppDelegate GetDelegate] OnVoiceOverStatusChanged];
		}
	});
#endif

    while( !GIsRequestingExit )
	{
        if (self.bIsSuspended)
        {
            FAppEntry::SuspendTick();
            
            self.bHasSuspended = true;
        }
        else
        {
			bool bOtherAudioPlayingNow = [self IsBackgroundAudioPlaying];
			if (bOtherAudioPlayingNow != self.bLastOtherAudioPlaying || self.bForceEmitOtherAudioPlaying)
			{
				FGraphEventRef UserMusicInterruptTask = FFunctionGraphTask::CreateAndDispatchWhenReady([bOtherAudioPlayingNow]()
				   {
					   //NSLog(@"UserMusicInterrupt Change: %s", bOtherAudioPlayingNow ? "playing" : "paused");
					   FCoreDelegates::UserMusicInterruptDelegate.Broadcast(bOtherAudioPlayingNow);
				   }, TStatId(), NULL, ENamedThreads::GameThread);
				
				self.bLastOtherAudioPlaying = bOtherAudioPlayingNow;
				self.bForceEmitOtherAudioPlaying = false;
			}
			
			int OutputVolume = [self GetAudioVolume];
			bool bMuted = false;
			
#if USE_MUTE_SWITCH_DETECTION
			SharkfoodMuteSwitchDetector* MuteDetector = [SharkfoodMuteSwitchDetector shared];
			bMuted = MuteDetector.isMute;
			if (bMuted != self.bLastMutedState || self.bForceEmitMutedState)
			{
				FGraphEventRef AudioMuteTask = FFunctionGraphTask::CreateAndDispatchWhenReady([bMuted, OutputVolume]()
					{
						//NSLog(@"Audio Session %s", bMuted ? "MUTED" : "UNMUTED");
						FCoreDelegates::AudioMuteDelegate.Broadcast(bMuted, OutputVolume);
					}, TStatId(), NULL, ENamedThreads::GameThread);
				
				self.bLastMutedState = bMuted;
				self.bForceEmitMutedState = false;
			}
#endif

			if (OutputVolume != self.LastVolume || self.bForceEmitVolume)
			{
				FGraphEventRef AudioMuteTask = FFunctionGraphTask::CreateAndDispatchWhenReady([bMuted, OutputVolume]()
					{
						//NSLog(@"Audio Volume: %d", OutputVolume);
						FCoreDelegates::AudioMuteDelegate.Broadcast(bMuted, OutputVolume);
					}, TStatId(), NULL, ENamedThreads::GameThread);

				self.LastVolume = OutputVolume;
				self.bForceEmitVolume = false;
			}

            FAppEntry::Tick();
        
            // free any autoreleased objects every once in awhile to keep memory use down (strings, splash screens, etc)
            if (((GFrameCounter) & 31) == 0)
            {
				// If you crash upon release, turn on Zombie Objects (Edit Scheme... | Diagnostics | Zombie Objects)
				// This will list the last object sent the release message, which will help identify the double free
                [AutoreleasePool release];
                AutoreleasePool = [[NSAutoreleasePool alloc] init];
            }
        }

        // drain the async task queue from the game thread
        [FIOSAsyncTask ProcessAsyncTasks];
	}

	[UIApplication sharedApplication].idleTimerDisabled = NO;

	[AutoreleasePool release];
	FAppEntry::Shutdown();
    
    self.bHasStarted = false;
    
    if(bForceExit || FApp::IsUnattended())
    {
        _Exit(0);
        //exit(0);  // As far as I can tell we run into a lot of trouble trying to run static destructors, so this is a no go :(
    }
}

-(void)timerForSplashScreen
{
    if (!GShowSplashScreen)
    {
        if ([self.Window viewWithTag:200] != nil)
        {
            [[self.Window viewWithTag:200] removeFromSuperview];
        }
        [timer invalidate];
    }
}

-(void)RecordPeakMemory
{
    FIOSPlatformMemory::GetStats();
}

-(void)InitIdleTimerSettings
{
	float TimerDuration = 0.0F;
	GConfig->GetFloat(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("IdleTimerEnablePeriod"), TimerDuration, GEngineIni);
    IdleTimerEnablePeriod = TimerDuration;
	self.IdleTimerEnableTimer = nil;
	bool bEnableTimer = YES;
	GConfig->GetBool(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("bEnableIdleTimer"), bEnableTimer, GEngineIni);
	[self EnableIdleTimer : bEnableTimer];
}

-(bool)IsIdleTimerEnabled
{
	return ([UIApplication sharedApplication].idleTimerDisabled == NO);
}

-(void)DeferredEnableIdleTimer
{
	[UIApplication sharedApplication].idleTimerDisabled = NO;
	self.IdleTimerEnableTimer = nil;
}

-(void)EnableIdleTimer:(bool)bEnabled
{
	dispatch_async(dispatch_get_main_queue(),^
	{
		if (bEnabled)
		{
			// Nothing needs to be done, if the enable timer is already running.
			if (self.IdleTimerEnableTimer == nil)
			{
				self.IdleTimerEnableTimer = [NSTimer scheduledTimerWithTimeInterval:IdleTimerEnablePeriod target:self selector:@selector(DeferredEnableIdleTimer) userInfo:nil repeats:NO];
			}
		}
		else
		{
			// Ensure pending attempts to enable the idle timer are cancelled.
			if (self.IdleTimerEnableTimer != nil)
			{
				[self.IdleTimerEnableTimer invalidate];
				self.IdleTimerEnableTimer = nil;
			}

			[UIApplication sharedApplication].idleTimerDisabled = NO;
			[UIApplication sharedApplication].idleTimerDisabled = YES;
		}
	});
}

-(void)NoUrlCommandLine
{
	//Since it is non-repeating, the timer should kill itself.
	self.bCommandLineReady = true;
}

- (void)InitializeAudioSession
{
	[[NSNotificationCenter defaultCenter] addObserverForName:AVAudioSessionInterruptionNotification object:nil queue:nil usingBlock:^(NSNotification *notification)
	{
		// the audio context should resume immediately after interrupt, if suspended
		FAppEntry::ResetAudioContextResumeTime();

		switch ([[[notification userInfo] objectForKey:AVAudioSessionInterruptionTypeKey] unsignedIntegerValue])
		{
			case AVAudioSessionInterruptionTypeBegan:
				self.bAudioActive = false;
				FAppEntry::Suspend(true);
				break;

			case AVAudioSessionInterruptionTypeEnded:

				NSNumber * interruptionOption = [[notification userInfo] objectForKey:AVAudioSessionInterruptionOptionKey];
				if (interruptionOption != nil && interruptionOption.unsignedIntegerValue > 0)
				{
					FAppEntry::RestartAudio();
				}

				FAppEntry::Resume(true);
				[self ToggleAudioSession:true force:true];
				break;
		}
	}];

	[[NSNotificationCenter defaultCenter] addObserverForName:AVAudioSessionRouteChangeNotification object : nil queue : nil usingBlock : ^ (NSNotification *notification)
	{
		switch ([[[notification userInfo] objectForKey:AVAudioSessionRouteChangeReasonKey] unsignedIntegerValue])
		{
			case AVAudioSessionRouteChangeReasonNewDeviceAvailable:
				// headphones plugged in
				FCoreDelegates::AudioRouteChangedDelegate.Broadcast(true);
				break;

			case AVAudioSessionRouteChangeReasonOldDeviceUnavailable:
				// headphones unplugged
				FCoreDelegates::AudioRouteChangedDelegate.Broadcast(false);
				break;
		}
	}];

	self.bUsingBackgroundMusic = [self IsBackgroundAudioPlaying];
	self.bForceEmitOtherAudioPlaying = true;

#if USE_MUTE_SWITCH_DETECTION
	// Initialize the mute switch detector.
	[SharkfoodMuteSwitchDetector shared];
	self.bForceEmitMutedState = true;
#endif
	
	self.bForceEmitVolume = true;
	
	FCoreDelegates::ApplicationRequestAudioState.AddLambda([self]()
		{
			self.bForceEmitOtherAudioPlaying = true;
#if USE_MUTE_SWITCH_DETECTION
			self.bForceEmitMutedState = true;
#endif
			self.bForceEmitVolume = true;
		});
	
	[self ToggleAudioSession:true force:true];
}

- (void)ToggleAudioSession:(bool)bActive force:(bool)bForce
{
	// @todo kairos: resolve old vs new before we go to main
	if (false)
	{
		// we can actually override bActive based on backgrounding behavior, as that's the only time we actually deactivate the session
		// @todo kairos: is this a valid check?
		bool bIsBackground = GIsSuspended;
		bActive = !bIsBackground || [self IsFeatureActive:EAudioFeature::BackgroundAudio];
		
		// @todo maybe check the active states, not bForce?
		if (self.bAudioActive != bActive || bForce)
		{
			// enable or disable audio
			NSError* ActiveError = nil;
			[[AVAudioSession sharedInstance] setActive:bActive error:&ActiveError];
			if (ActiveError)
			{
				UE_LOG(LogIOSAudioSession, Error, TEXT("Failed to set audio session to active = %d [Error = %s]"), bActive, *FString([ActiveError description]));
			}
			else
			{
				self.bAudioActive = bActive;
			}
			
			if (self.bAudioActive)
			{
				// get the category and settings to use
				/*AVAudioSessionCategory*/NSString* Category = AVAudioSessionCategorySoloAmbient;
				/*AVAudioSessionMode*/NSString* Mode = AVAudioSessionModeDefault;
				AVAudioSessionCategoryOptions Options = 0;

				// attempt to mix with other apps if desired
				if ([self IsFeatureActive:EAudioFeature::BackgroundAudio])
				{
					Options |= AVAudioSessionCategoryOptionMixWithOthers;
				}

				if ([self IsFeatureActive:EAudioFeature::VoiceChat] || ([self IsFeatureActive:EAudioFeature::Playback] && [self IsFeatureActive:EAudioFeature::Record]))
				{
					Category = AVAudioSessionCategoryPlayAndRecord;
					
					if ([self IsFeatureActive:EAudioFeature::VoiceChat])
					{
						Mode = self.bHighQualityVoiceChatEnabled ? AVAudioSessionModeVoiceChat : AVAudioSessionModeDefault;
#if !PLATFORM_TVOS
						// allow bluetooth for chatting and such
						Options |= AVAudioSessionCategoryOptionDefaultToSpeaker | AVAudioSessionCategoryOptionAllowBluetooth | AVAudioSessionCategoryOptionAllowBluetoothA2DP;
#endif
					}
				}
				else if ([self IsFeatureActive:EAudioFeature::Playback])
				{
					Category = AVAudioSessionCategoryPlayback;
				}
				else if ([self IsFeatureActive:EAudioFeature::Record])
				{
					Category = AVAudioSessionCategoryRecord;
				}
				else if ([self IsFeatureActive:EAudioFeature::BackgroundAudio])
				{
					Category = AVAudioSessionCategoryRecord;
				}

				// set the category (the most important part here)
				[[AVAudioSession sharedInstance] setCategory:Category mode:Mode options:Options error:&ActiveError];
				if (ActiveError)
				{
					UE_LOG(LogIOSAudioSession, Error, TEXT("Failed to set audio session category to %s! [Error = %s]"), *FString(Category), *FString([ActiveError description]));
				}
			}
		}
	}
	// old style below
	else if (bActive)
	{
        if (bForce || !self.bAudioActive)
        {
			bool bWasUsingBackgroundMusic = self.bUsingBackgroundMusic;
			self.bUsingBackgroundMusic = [self IsBackgroundAudioPlaying];
	
			if (bWasUsingBackgroundMusic != self.bUsingBackgroundMusic || GAudio_ForceAmbientCategory)
			{
				if (!self.bUsingBackgroundMusic || GAudio_ForceAmbientCategory)
				{
					NSError* ActiveError = nil;
					[[AVAudioSession sharedInstance] setActive:YES error:&ActiveError];
					if (ActiveError)
					{
						UE_LOG(LogIOSAudioSession, Error, TEXT("Failed to set audio session as active! [Error = %s]"), *FString([ActiveError description]));
					}
					ActiveError = nil;
	
                    if (!self.bVoiceChatEnabled)
                    {
						if (!GAudio_ForceAmbientCategory)
						{
        					[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategorySoloAmbient error:&ActiveError];
        					if (ActiveError)
        					{
        						UE_LOG(LogIOSAudioSession, Error, TEXT("Failed to set audio session category to AVAudioSessionCategorySoloAmbient! [Error = %s]"), *FString([ActiveError description]));
        					}
						}
						else
						{
							[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryAmbient error:&ActiveError];
							if (ActiveError)
							{
								UE_LOG(LogIOSAudioSession, Error, TEXT("Failed to set audio session category to AVAudioSessionCategoryAmbient! [Error = %s]"), *FString([ActiveError description]));
							}
						}
    					ActiveError = nil;
                    }
					else
					{
						AVAudioSessionCategoryOptions opts =
							AVAudioSessionCategoryOptionAllowBluetoothA2DP |
#if !PLATFORM_TVOS
							AVAudioSessionCategoryOptionDefaultToSpeaker |
#endif
							AVAudioSessionCategoryOptionMixWithOthers;
						
						if (@available(iOS 10, *))
						{
							NSString* VoiceChatMode = self.bHighQualityVoiceChatEnabled ? AVAudioSessionModeVoiceChat : AVAudioSessionModeDefault;
							[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayAndRecord mode:VoiceChatMode options:opts error:&ActiveError];
						}
						else
						{
							[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayAndRecord withOptions:opts error:&ActiveError];
						}
						if (ActiveError)
						{
							UE_LOG(LogIOSAudioSession, Error, TEXT("Failed to set audio session category!"));
						}
						ActiveError = nil;
					}

					/* TODO::JTM - Jan 16, 2013 05:05PM - Music player support */
				}
				else
				{
					/* TODO::JTM - Jan 16, 2013 05:05PM - Music player support */
	
                    if (!self.bVoiceChatEnabled)
                    {
    					// Allow iPod music to continue playing in the background
    					NSError* ActiveError = nil;
    					[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryAmbient error:&ActiveError];
    					if (ActiveError)
    					{
    						UE_LOG(LogIOSAudioSession, Error, TEXT("Failed to set audio session category to AVAudioSessionCategoryAmbient! [Error = %s]"), *FString([ActiveError description]));
    					}
    					ActiveError = nil;
                    }
				}
			}
			else if (!self.bUsingBackgroundMusic)
			{
				NSError* ActiveError = nil;
				[[AVAudioSession sharedInstance] setActive:YES error:&ActiveError];
				if (ActiveError)
				{
					UE_LOG(LogIOSAudioSession, Error, TEXT("Failed to set audio session as active! [Error = %s]"), *FString([ActiveError description]));
				}
				ActiveError = nil;
                
                if (!self.bVoiceChatEnabled)
                {
					if (!GAudio_ForceAmbientCategory)
					{
						[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategorySoloAmbient error:&ActiveError];
						if (ActiveError)
						{
							UE_LOG(LogIOSAudioSession, Error, TEXT("Failed to set audio session category to AVAudioSessionCategorySoloAmbient! [Error = %s]"), *FString([ActiveError description]));
						}
					}
					else
					{
						[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryAmbient error:&ActiveError];
						if (ActiveError)
						{
							UE_LOG(LogIOSAudioSession, Error, TEXT("Failed to set audio session category to AVAudioSessionCategoryAmbient! [Error = %s]"), *FString([ActiveError description]));
						}
					}
    				ActiveError = nil;
                }
				else
				{
					AVAudioSessionCategoryOptions opts =
						AVAudioSessionCategoryOptionAllowBluetoothA2DP |
#if !PLATFORM_TVOS
						AVAudioSessionCategoryOptionDefaultToSpeaker |
#endif
						AVAudioSessionCategoryOptionMixWithOthers;
					
					if (@available(iOS 10, *))
					{
						NSString* VoiceChatMode = self.bHighQualityVoiceChatEnabled ? AVAudioSessionModeVoiceChat : AVAudioSessionModeDefault;
						[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayAndRecord mode:VoiceChatMode options:opts error:&ActiveError];
					}
					else
					{
						[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayAndRecord withOptions:opts error:&ActiveError];
					}
					if (ActiveError)
					{
						UE_LOG(LogIOSAudioSession, Error, TEXT("Failed to set audio session category!"));
					}
					ActiveError = nil;
				}
			}
        }
	}
	else if ((bForce || self.bAudioActive) && !self.bUsingBackgroundMusic)
	{
		NSError* ActiveError = nil;
        if (self.bVoiceChatEnabled)
		{
			AVAudioSessionCategoryOptions opts =
				AVAudioSessionCategoryOptionAllowBluetoothA2DP |
#if !PLATFORM_TVOS
				AVAudioSessionCategoryOptionDefaultToSpeaker |
#endif
				AVAudioSessionCategoryOptionMixWithOthers;
			
			// Necessary for voice chat if audio is not active
			if (@available(iOS 10, *))
			{
				NSString* VoiceChatMode = self.bHighQualityVoiceChatEnabled ? AVAudioSessionModeVoiceChat : AVAudioSessionModeDefault;
				[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayAndRecord mode:VoiceChatMode options:opts error:&ActiveError];
			}
			else
			{
				[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayAndRecord withOptions:opts error:&ActiveError];
			}
			if (ActiveError)
			{
				UE_LOG(LogIOSAudioSession, Error, TEXT("Failed to set audio session category!"));
			}
			ActiveError = nil;
		}
		else
        {
    		// Necessary to prevent audio from getting killing when setup for background iPod audio playback
    		[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryAmbient error:&ActiveError];
    		if (ActiveError)
    		{
    			UE_LOG(LogIOSAudioSession, Error, TEXT("Failed to set audio session category to AVAudioSessionCategoryAmbient! [Error = %s]"), *FString([ActiveError description]));
    		}
    		ActiveError = nil;
        }
 	}
    self.bAudioActive = bActive;
}

- (bool)IsBackgroundAudioPlaying
{
	AVAudioSession* Session = [AVAudioSession sharedInstance];
	return Session.otherAudioPlaying;
}

-(bool)HasRecordPermission
{
#if PLATFORM_TVOS
	// TVOS does not have sound recording capabilities.
	return false;
#else
	return [[AVAudioSession sharedInstance] recordPermission] == AVAudioSessionRecordPermissionGranted;
#endif
}

-(void)EnableHighQualityVoiceChat:(bool)bEnable
{
	self.bHighQualityVoiceChatEnabled = bEnable;
}

- (void)EnableVoiceChat:(bool)bEnable
{
	self.bVoiceChatEnabled = false;
	
    // mobile will prompt for microphone access
    if (FApp::IsUnattended())
	{
		return;
	}
	self.bVoiceChatEnabled = bEnable;
	[self ToggleAudioSession:self.bAudioActive force:true];
	//[self SetFeature:EAudioFeature::VoiceChat Active:bEnable];
}

- (bool)IsVoiceChatEnabled
{
	return self.bVoiceChatEnabled;
	//return [self IsFeatureActive:EAudioFeature::VoiceChat];
}


- (void)SetFeature:(EAudioFeature)Feature Active:(bool)bIsActive
{
	if (GEnabledAudioFeatures[(uint8)Feature] != bIsActive)
	{
		GEnabledAudioFeatures[(uint8)Feature] = bIsActive;
		
		// actually set the session
		[self ToggleAudioSession:self.bAudioActive force:true];
	}
}

- (bool)IsFeatureActive:(EAudioFeature)Feature
{
	return GEnabledAudioFeatures[(uint8)Feature];
}


- (int)GetAudioVolume
{
	float vol = [[AVAudioSession sharedInstance] outputVolume];
	int roundedVol = (int)((vol * 100.0f) + 0.5f);
	return roundedVol;
}

- (bool)AreHeadphonesPluggedIn
{
	AVAudioSessionRouteDescription *route = [[AVAudioSession sharedInstance] currentRoute];

	bool headphonesFound = false;
	for (AVAudioSessionPortDescription *portDescription in route.outputs)
	{
		//compare to the iOS constant for headphones
		if ([portDescription.portType isEqualToString : AVAudioSessionPortHeadphones])
		{
			headphonesFound = true;
			break;
		}
	}
	return headphonesFound;
}

- (int)GetBatteryLevel
{
#if PLATFORM_TVOS
	// TVOS does not have a battery, return fully charged
	return 100;
#else
    return self.BatteryLevel;
#endif
}

- (bool)IsRunningOnBattery
{
#if PLATFORM_TVOS
	// TVOS does not have a battery, return plugged in
	return false;
#else
    return self.bBatteryState;
#endif
}

- (void)CheckForZoomAccessibility
{
#if !PLATFORM_TVOS
    // warn about zoom conflicting
    UIAccessibilityRegisterGestureConflictWithZoom();
#endif
}

- (float)GetBackgroundingMainThreadBlockTime
{
	return GOverrideThreadWaitTime > 0.0f ? GOverrideThreadWaitTime : GMaxThreadWaitTime;
}

-(void)OverrideBackgroundingMainThreadBlockTime:(float)BlockTime
{
	GOverrideThreadWaitTime = BlockTime;
}



- (NSProcessInfoThermalState)GetThermalState
{
    return self.ThermalState;
}

- (UIViewController*) IOSController
{
	// walk the responder chain until we get to a non-view, that's the VC
	UIResponder *Responder = IOSView;
	while ([Responder isKindOfClass:[UIView class]])
	{
		Responder = [Responder nextResponder];
	}
	return (UIViewController*)Responder;
}


bool GIsSuspended = 0;
- (void)ToggleSuspend:(bool)bSuspend
{
    self.bHasSuspended = !bSuspend;
    self.bIsSuspended = bSuspend;
    GIsSuspended = self.bIsSuspended;

	if (bSuspend)
	{
		FAppEntry::Suspend();
	}
	else
	{
        FIOSPlatformRHIFramePacer::Resume();
		FAppEntry::Resume();
	}
    
	if (IOSView && IOSView->bIsInitialized)
	{
		// Don't deadlock here because a msg box may appear super early blocking the game thread and then the app may go into the background
		double	startTime = FPlatformTime::Seconds();

		// don't wait for FDefaultGameMoviePlayer::WaitForMovieToFinish(), crash with 0x8badf00d if "Wait for Movies to Complete" is checked
		FEmbeddedCommunication::KeepAwake(TEXT("Background"), false);
		while(!self.bHasSuspended && !FAppEntry::IsStartupMoviePlaying() &&  (FPlatformTime::Seconds() - startTime) < [self GetBackgroundingMainThreadBlockTime])
		{
            FIOSPlatformRHIFramePacer::Suspend();
			FPlatformProcess::Sleep(0.05f);
		}
		FEmbeddedCommunication::AllowSleep(TEXT("Background"));
	}
}

- (void)ForceExit
{
    GIsRequestingExit = true;
    bForceExit = true;
}

- (BOOL)application:(UIApplication *)application willFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
	self.bDeviceInPortraitMode = false;
	bEngineInit = false;

	return YES;
}

static int32 GEnableThermalsReport = 0;
static FAutoConsoleVariableRef CVarGEnableThermalsReport(
	TEXT("ios.EnableThermalsReport"),
	GEnableThermalsReport,
	TEXT("When set to 1, will enable on-screen thermals debug display.")
);

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary*)launchOptions
{
	// save launch options
	self.launchOptions = launchOptions;

#if PLATFORM_TVOS
	self.bDeviceInPortraitMode = false;
#else
	// use the status bar orientation to properly determine landscape vs portrait
	self.bDeviceInPortraitMode = UIInterfaceOrientationIsPortrait([[UIApplication sharedApplication] statusBarOrientation]);
	printf("========= This app is in %s mode\n", self.bDeviceInPortraitMode ? "PORTRAIT" : "LANDSCAPE");
#endif

	// check OS version to make sure we have the API
	OSVersion = [[[UIDevice currentDevice] systemVersion] floatValue];
	if (!FPlatformMisc::IsDebuggerPresent() || GAlwaysReportCrash)
	{
//        InstallSignalHandlers();
	}

	self.savedOpenUrlParameters = [[NSMutableArray alloc] init];
	self.PeakMemoryTimer = [NSTimer scheduledTimerWithTimeInterval:0.1f target:self selector:@selector(RecordPeakMemory) userInfo:nil repeats:YES];

#if !BUILD_EMBEDDED_APP
	// create the main landscape window object
	CGRect MainFrame = [[UIScreen mainScreen] bounds];
	self.Window = [[UIWindow alloc] initWithFrame:MainFrame];
	self.Window.screen = [UIScreen mainScreen];
    
    // get the native scale
    const float NativeScale = [[UIScreen mainScreen] scale];
    
	//Make this the primary window, and show it.
	[self.Window makeKeyAndVisible];

	FAppEntry::PreInit(self, application);
    
    // add the default image as a subview
    NSMutableString* path = [[NSMutableString alloc]init];
    [path setString: [[NSBundle mainBundle] resourcePath]];
    UIImageOrientation orient = UIImageOrientationUp;
    NSMutableString* ImageString = [[NSMutableString alloc]init];
	NSMutableString* PngString = [[NSMutableString alloc] init];
    [ImageString appendString:@"Default"];


	FPlatformMisc::EIOSDevice Device = FPlatformMisc::GetIOSDeviceType();

	// iphone6 has specially named files, this seems to be needed for every iphone since, so let's see if we can find a better way to do this which isn't device specific
    if (Device == FPlatformMisc::IOS_IPhone6 || Device == FPlatformMisc::IOS_IPhone6S || Device == FPlatformMisc::IOS_IPhone7 || Device == FPlatformMisc::IOS_IPhone8)
	{
		[ImageString appendString:@"-IPhone6"];
		if (!self.bDeviceInPortraitMode)
		{
			[ImageString appendString : @"-Landscape"];
		}
	}
    else if (Device == FPlatformMisc::IOS_IPhone6Plus || Device == FPlatformMisc::IOS_IPhone6SPlus || Device == FPlatformMisc::IOS_IPhone7Plus || Device == FPlatformMisc::IOS_IPhone8Plus)
	{
		[ImageString appendString : @"-IPhone6Plus"];
		if (!self.bDeviceInPortraitMode)
		{
			[ImageString appendString : @"-Landscape"];
		}
		else
		{
			[ImageString appendString : @"-Portrait"];
		}
	}
    else if (Device == FPlatformMisc::IOS_IPhoneX || Device == FPlatformMisc::IOS_IPhoneXS)
	{
		[ImageString appendString : @"-IPhoneXS"];
		if (!self.bDeviceInPortraitMode)
		{
			[ImageString appendString : @"-Landscape"];
		}
		else
		{
			[ImageString appendString : @"-Portrait"];
		}
	}
    else if (Device == FPlatformMisc::IOS_IPhoneXSMax)
    {
        [ImageString appendString : @"-IPhoneXSMax"];
        if (!self.bDeviceInPortraitMode)
        {
            [ImageString appendString : @"-Landscape"];
        }
        else
        {
            [ImageString appendString : @"-Portrait"];
        }
    }
    else if (Device == FPlatformMisc::IOS_IPhoneXR)
    {
        [ImageString appendString : @"-IPhoneXR"];
        if (!self.bDeviceInPortraitMode)
        {
            [ImageString appendString : @"-Landscape"];
        }
        else
        {
            [ImageString appendString : @"-Portrait"];
        }
    }
	else if (Device == FPlatformMisc::IOS_AppleTV)
	{
		// @todo tvos: Make an AppleTV one?
		// use IPhone6 image for now
		[ImageString appendString : @"-IPhone6Plus-Landscape"];
	}
	else if (Device == FPlatformMisc::IOS_IPadPro_129 || Device == FPlatformMisc::IOS_IPadPro2_129 || Device == FPlatformMisc::IOS_IPadPro3_129)
	{
		if (!self.bDeviceInPortraitMode)
		{
			[ImageString appendString : @"-Landscape-1336"];
		}
		else
		{
			[ImageString appendString : @"-Portrait-1336"];
		}
        
        if (NativeScale > 1.0f)
        {
            [ImageString appendString:@"@2x"];
        }
	}
	else if (Device == FPlatformMisc::IOS_IPadPro_105)
	{
		if (!self.bDeviceInPortraitMode)
		{
			[ImageString appendString : @"-Landscape-1112"];
		}
		else
		{
			[ImageString appendString : @"-Portrait-1112"];
		}

		if (NativeScale > 1.0f)
		{
			[ImageString appendString : @"@2x"];
		}
	}
	else if (Device == FPlatformMisc::IOS_IPadPro_11)
	{
		if (!self.bDeviceInPortraitMode)
		{
			[ImageString appendString : @"-Landscape-1194"];
		}
		else
		{
			[ImageString appendString : @"-Portrait-1194"];
		}

		if (NativeScale > 1.0f)
		{
			[ImageString appendString : @"@2x"];
		}
	}
	else
	{
		if (MainFrame.size.height == 320 && MainFrame.size.width != 480 && !self.bDeviceInPortraitMode)
		{
			[ImageString appendString:@"-568h"];
			orient = UIImageOrientationRight;
		}
		else if (MainFrame.size.height == 320 && MainFrame.size.width == 480 && !self.bDeviceInPortraitMode)
		{
			orient = UIImageOrientationRight;
		}
		else if (MainFrame.size.height == 568 || Device == FPlatformMisc::IOS_IPodTouch6 || Device == FPlatformMisc::IOS_IPodTouch7)
		{
			[ImageString appendString:@"-568h"];
		}
		else if (MainFrame.size.height == 1024 && !self.bDeviceInPortraitMode)
		{
			[ImageString appendString:@"-Landscape"];
			orient = UIImageOrientationRight;
		}
		else if (MainFrame.size.height == 1024)
		{
			[ImageString appendString:@"-Portrait"];
		}
		else if (MainFrame.size.height == 768 && !self.bDeviceInPortraitMode)
		{
			[ImageString appendString:@"-Landscape"];
		}
        
        if (NativeScale > 1.0f)
        {
            [ImageString appendString:@"@2x"];
        }
	}

	[PngString appendString : ImageString];
	[PngString appendString : @".png"];
	[ImageString appendString : @".jpg"];
    [path setString: [path stringByAppendingPathComponent:ImageString]];
    UIImage* image = [[UIImage alloc] initWithContentsOfFile: path];
	if (image == nil)
	{
        [path setString: [[NSBundle mainBundle] resourcePath]];
		[path setString : [path stringByAppendingPathComponent : PngString]];
		image = [[UIImage alloc] initWithContentsOfFile:path];
	}
	[path release];
	
    UIImage* imageToDisplay = [UIImage imageWithCGImage: [image CGImage] scale: 1.0 orientation: orient];
    UIImageView* imageView = [[UIImageView alloc] initWithImage: imageToDisplay];
    imageView.frame = MainFrame;
    imageView.tag = 200;
    [self.Window addSubview: imageView];
    GShowSplashScreen = true;

	timer = [NSTimer scheduledTimerWithTimeInterval: 0.05f target:self selector:@selector(timerForSplashScreen) userInfo:nil repeats:YES];

	[self StartGameThread];

	self.CommandLineParseTimer = [NSTimer scheduledTimerWithTimeInterval:0.01f target:self selector:@selector(NoUrlCommandLine) userInfo:nil repeats:NO];

#endif
	
#if !PLATFORM_TVOS
#if __IPHONE_OS_VERSION_MIN_REQUIRED >= __IPHONE_10_0
	UNUserNotificationCenter *Center = [UNUserNotificationCenter currentNotificationCenter];
	Center.delegate = self;
#else
	// Save launch local notification so the app can check for it when it is ready
	UILocalNotification *notification = [launchOptions objectForKey:UIApplicationLaunchOptionsLocalNotificationKey];
	if ( notification != nullptr )
	{
		NSDictionary*	userInfo = [notification userInfo];
		if(userInfo != nullptr)
		{
			NSString*	activationEvent = (NSString*)[notification.userInfo objectForKey: @"ActivationEvent"];
			
			if(activationEvent != nullptr)
			{
				FAppEntry::gAppLaunchedWithLocalNotification = true;
				FAppEntry::gLaunchLocalNotificationActivationEvent = FString(activationEvent);
				FAppEntry::gLaunchLocalNotificationFireDate = [notification.fireDate timeIntervalSince1970];
			}
		}
	}
#endif

	// Register for device orientation changes
	[[UIDevice currentDevice] beginGeneratingDeviceOrientationNotifications];
	[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(didRotate:) name:UIApplicationDidChangeStatusBarOrientationNotification object:nil];

#if !UE_BUILD_SHIPPING
	// make a history buffer
	self.ConsoleHistoryValues = [[NSMutableArray alloc] init];

	// load saved history from disk
	NSArray* SavedHistory = [[NSUserDefaults standardUserDefaults] objectForKey:@"ConsoleHistory"];
	if (SavedHistory != nil)
	{
		[self.ConsoleHistoryValues addObjectsFromArray:SavedHistory];
	}
	self.ConsoleHistoryValuesIndex = -1;

	if (@available(iOS 11, *))
	{
		FCoreDelegates::OnGetOnScreenMessages.AddLambda(
			[&EnableThermalsReport = GEnableThermalsReport](TMultiMap<FCoreDelegates::EOnScreenMessageSeverity, FText >& OutMessages)
			{
				if (EnableThermalsReport)
				{
					switch ([[NSProcessInfo processInfo] thermalState])
					{
						case NSProcessInfoThermalStateNominal:	OutMessages.Add(FCoreDelegates::EOnScreenMessageSeverity::Info, FText::FromString(TEXT("Thermals are Nominal"))); break;
						case NSProcessInfoThermalStateFair:		OutMessages.Add(FCoreDelegates::EOnScreenMessageSeverity::Info, FText::FromString(TEXT("Thermals are Fair"))); break;
						case NSProcessInfoThermalStateSerious:	OutMessages.Add(FCoreDelegates::EOnScreenMessageSeverity::Warning, FText::FromString(TEXT("Thermals are Serious"))); break;
						case NSProcessInfoThermalStateCritical:	OutMessages.Add(FCoreDelegates::EOnScreenMessageSeverity::Error, FText::FromString(TEXT("Thermals are Critical"))); break;
					}
				}

				// Uncomment to view the state of the AVAudioSession category, mode, and options.
//#define VIEW_AVAUDIOSESSION_INFO
#if defined(VIEW_AVAUDIOSESSION_INFO)
				FString Message = FString::Printf(
					TEXT("Session Category: %s, Mode: %s, Options: %x"),
					UTF8_TO_TCHAR([[AVAudioSession sharedInstance].category UTF8String]),
					UTF8_TO_TCHAR([[AVAudioSession sharedInstance].mode UTF8String]),
					[AVAudioSession sharedInstance].categoryOptions);
				OutMessages.Add(FCoreDelegates::EOnScreenMessageSeverity::Info, FText::FromString(Message));
#endif // defined(VIEW_AVAUDIOSESSION_INFO)
			});
	}


#endif // UE_BUILD_SHIPPING
#endif // !TVOS

#if !PLATFORM_TVOS
	if (@available(iOS 11, *))
	{
        UIDevice* UiDevice = [UIDevice currentDevice];
        UiDevice.batteryMonitoringEnabled = YES;
        
        // Battery level is from 0.0 to 1.0, get it in terms of 0-100
        self.BatteryLevel = ((int)([UiDevice batteryLevel] * 100));
        UIDeviceBatteryState State = UiDevice.batteryState;
        self.bBatteryState = State == UIDeviceBatteryStateUnplugged || State == UIDeviceBatteryStateUnknown;
        self.ThermalState = [[NSProcessInfo processInfo] thermalState];

		[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(temperatureChanged:) name:NSProcessInfoThermalStateDidChangeNotification object:nil];
		[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(lowPowerModeChanged:) name:NSProcessInfoPowerStateDidChangeNotification object:nil];
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(batteryChanged:) name:UIDeviceBatteryLevelDidChangeNotification object:nil];
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(batteryStateChanged:) name:UIDeviceBatteryStateDidChangeNotification object:nil];
	}
#endif
    
	[self InitializeAudioSession];

#if WITH_ACCESSIBILITY
	[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(OnVoiceOverStatusChanged) name:UIAccessibilityVoiceOverStatusDidChangeNotification object:nil];
#endif
    
	return YES;
}

#if WITH_ACCESSIBILITY
-(void)OnVoiceOverStatusChanged
{
	if (UIAccessibilityIsVoiceOverRunning() && self.IOSApplication->GetAccessibleMessageHandler()->ApplicationIsAccessible())
	{
		// This must happen asynchronously because when the app activates from a suspended state,
		// the IOS notification will emit before the game thread wakes up. This does mean that the
		// accessibility element tree will probably not be 100% completed when the application
		// opens for the first time. If this is a problem we can add separate branches for startup
		// vs waking up.
		FFunctionGraphTask::CreateAndDispatchWhenReady([]()
		{
			FIOSApplication* Application = [IOSAppDelegate GetDelegate].IOSApplication;
			Application->GetAccessibleMessageHandler()->SetActive(true);
			AccessibleWidgetId WindowId = Application->GetAccessibleMessageHandler()->GetAccessibleWindowId(Application->FindWindowByAppDelegateView());
			dispatch_async(dispatch_get_main_queue(), ^{
                IOSAppDelegate* Delegate = [IOSAppDelegate GetDelegate];
				[Delegate.IOSView SetAccessibilityWindow:WindowId];
				if (Delegate.AccessibilityCacheTimer == nil)
				{
					// Start caching accessibility data so that it can be returned instantly to IOS. If not cached, the data takes too long
					// to retrieve due to cross-thread waiting and IOS will timeout.
					Delegate.AccessibilityCacheTimer = [NSTimer scheduledTimerWithTimeInterval:0.25f target:[FIOSAccessibilityCache AccessibilityElementCache] selector:@selector(UpdateAllCachedProperties) userInfo:nil repeats:YES];
				}
			});
		}, TStatId(), NULL, ENamedThreads::GameThread);
	}
	else if (AccessibilityCacheTimer != nil)
	{
		[AccessibilityCacheTimer invalidate];
		AccessibilityCacheTimer = nil;
		[[IOSAppDelegate GetDelegate].IOSView SetAccessibilityWindow : IAccessibleWidget::InvalidAccessibleWidgetId];
		[[FIOSAccessibilityCache AccessibilityElementCache] Clear];
		FFunctionGraphTask::CreateAndDispatchWhenReady([]()
		{
			[IOSAppDelegate GetDelegate].IOSApplication->GetAccessibleMessageHandler()->SetActive(false);
		}, TStatId(), NULL, ENamedThreads::GameThread);
	}
}
#endif

- (void) StartGameThread
{
	// create a new thread (the pointer will be retained forever)
	NSThread* GameThread = [[NSThread alloc] initWithTarget:self selector:@selector(MainAppThread:) object:self.launchOptions];
	[GameThread setStackSize:GAME_THREAD_STACK_SIZE];
	[GameThread start];

	// this can be slow (1/3 of a second!), so don't make the game thread stall loading for it
	// check to see if we are using the network file system, if so, disable the idle timer
	FString HostIP;
//	if (FParse::Value(FCommandLine::Get(), TEXT("-FileHostIP="), HostIP))
	{
		[UIApplication sharedApplication].idleTimerDisabled = YES;
	}
}

+(bool)WaitAndRunOnGameThread:(TUniqueFunction<void()>)Function
{
	FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady(MoveTemp(Function), TStatId(), NULL, ENamedThreads::GameThread);

	const double MaxThreadWaitTime = 2.0;
	const double StartTime = FPlatformTime::Seconds();
	while ((FPlatformTime::Seconds() - StartTime) < MaxThreadWaitTime)
	{
		FPlatformProcess::Sleep(0.05f);
		if (Task->IsComplete())
		{
			return true;
		}
	}
	return false;
}

#if !PLATFORM_TVOS
extern EDeviceScreenOrientation ConvertFromUIInterfaceOrientation(UIInterfaceOrientation Orientation);
#endif

- (void) didRotate:(NSNotification *)notification
{   
#if !PLATFORM_TVOS
	// get the interfaec orientation
	NSNumber* OrientationNumber = [notification.userInfo objectForKey:UIApplicationStatusBarOrientationUserInfoKey];
	UIInterfaceOrientation Orientation = (UIInterfaceOrientation)[OrientationNumber intValue];
	
	NSLog(@"didRotate orientation = %d, statusBar = %d", (int)Orientation, (int)[[UIApplication sharedApplication] statusBarOrientation]);
	
	Orientation = [[UIApplication sharedApplication] statusBarOrientation];
	
	extern UIInterfaceOrientation GInterfaceOrientation;
	GInterfaceOrientation = Orientation;
	
    if (bEngineInit)
    {
		FFunctionGraphTask::CreateAndDispatchWhenReady([Orientation]()
		{
			FCoreDelegates::ApplicationReceivedScreenOrientationChangedNotificationDelegate.Broadcast((int32)ConvertFromUIInterfaceOrientation(Orientation));

			//we also want to fire off the safe frame event
			FCoreDelegates::OnSafeFrameChangedEvent.Broadcast();
		}, TStatId(), NULL, ENamedThreads::GameThread);
	}
#endif
}

- (BOOL)application:(UIApplication *)application openURL:(NSURL *)url sourceApplication:(NSString *)sourceApplication annotation:(id)annotation
{
#if !NO_LOGGING
	NSLog(@"%s", "IOSAppDelegate openURL\n");
#endif

	NSString* EncdodedURLString = [url absoluteString];
	NSString* URLString = [EncdodedURLString stringByRemovingPercentEncoding];
	FString CommandLineParameters(URLString);

	// Strip the "URL" part of the URL before treating this like args. It comes in looking like so:
	// "MyGame://arg1 arg2 arg3 ..."
	// So, we're going to make it look like:
	// "arg1 arg2 arg3 ..."
	int32 URLTerminator = CommandLineParameters.Find( TEXT("://"));
	if ( URLTerminator > -1 )
	{
		CommandLineParameters = CommandLineParameters.RightChop(URLTerminator + 3);
	}

	FIOSCommandLineHelper::InitCommandArgs(CommandLineParameters);
	self.bCommandLineReady = true;
	[self.CommandLineParseTimer invalidate];
	self.CommandLineParseTimer = nil;
	
	//    Save openurl infomation before engine initialize.
	//    When engine is done ready, running like previous. ( if OnOpenUrl is bound on game source. )
	if (bEngineInit)
	{
		FIOSCoreDelegates::OnOpenURL.Broadcast(application, url, sourceApplication, annotation);
	}
	else
	{
#if !NO_LOGGING
		NSLog(@"%s", "Before Engine Init receive IOSAppDelegate openURL\n");
#endif
			NSDictionary* openUrlParameter = [NSDictionary dictionaryWithObjectsAndKeys :
		application, @"application",
			url, @"url",
			sourceApplication, @"sourceApplication",
			annotation, @"annotation",
			nil];

		[savedOpenUrlParameters addObject : openUrlParameter];
	}

	return YES;
}

FCriticalSection RenderSuspend;
- (void)applicationWillResignActive:(UIApplication *)application
{
    
    FIOSPlatformMisc::ResetBrightness();
    
    /*
		Sent when the application is about to move from active to inactive
		state. This can occur for certain types of temporary interruptions (such
		as an incoming phone call or SMS message) or when the user quits the
		application and it begins the transition to the background state.

		Use this method to pause ongoing tasks, disable timers, and throttle
	 	down OpenGL ES frame rates. Games should use this method to pause the
		game.
	 */
    if (bEngineInit)
    {
 		FEmbeddedCommunication::KeepAwake(TEXT("Background"), false);
        FGraphEventRef ResignTask = FFunctionGraphTask::CreateAndDispatchWhenReady([]()
        {
            FCoreDelegates::ApplicationWillDeactivateDelegate.Broadcast();

			FEmbeddedCommunication::AllowSleep(TEXT("Background"));
        }, TStatId(), NULL, ENamedThreads::GameThread);
		
		// Do not wait forever for this task to complete since the game thread may be stuck on waiting for user input from a modal dialog box
		double	startTime = FPlatformTime::Seconds();
		while((FPlatformTime::Seconds() - startTime) < [self GetBackgroundingMainThreadBlockTime])
		{
			FPlatformProcess::Sleep(0.05f);
			if(ResignTask->IsComplete())
			{
				break;
			}
		}
    }
	[self ToggleSuspend:true];
	[self ToggleAudioSession:false force:true];
    
    RenderSuspend.TryLock();
    if (FTaskGraphInterface::IsRunning())
    {
        if (bEngineInit)
        {
            FGraphEventRef ResignTask = FFunctionGraphTask::CreateAndDispatchWhenReady([]()
            {
                FScopeLock ScopeLock(&RenderSuspend);
            }, TStatId(), NULL, ENamedThreads::GameThread);
        }
        else
        {
            FGraphEventRef ResignTask = FFunctionGraphTask::CreateAndDispatchWhenReady([]()
            {
                FScopeLock ScopeLock(&RenderSuspend);
            }, TStatId(), NULL, ENamedThreads::ActualRenderingThread);
        }
    }
}

- (void)applicationDidEnterBackground:(UIApplication *)application
{
	/*
	 Use this method to release shared resources, save user data, invalidate
	 timers, and store enough application state information to restore your
	 application to its current state in case it is terminated later.
	 
	 If your application supports background execution, this method is called
	 instead of applicationWillTerminate: when the user quits.
	 */

#if BUILD_EMBEDDED_APP
	FEmbeddedCommunication::KeepAwake(TEXT("Background"), false);

	[FIOSAsyncTask CreateTaskWithBlock : ^ bool(void)
	{
		// the audio context should resume immediately after interrupt, if suspended
		FAppEntry::ResetAudioContextResumeTime();

		FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Broadcast();
	
		FEmbeddedCommunication::AllowSleep(TEXT("Background"));
		return true;
	}];
#else
	FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Broadcast();
#endif //BUILD_EBMEDDED_APP
}

- (void)applicationWillEnterForeground:(UIApplication *)application
{
	FEmbeddedCommunication::KeepAwake(TEXT("Background"), false);
	/*
	 Called as part of the transition from the background to the inactive state; here you can undo many of the changes made on entering the background.
	 */
	[FIOSAsyncTask CreateTaskWithBlock : ^ bool(void)
	{
		// the audio context should resume immediately after interrupt, if suspended
		FAppEntry::ResetAudioContextResumeTime();

		FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Broadcast();

		FEmbeddedCommunication::AllowSleep(TEXT("Background"));
		return true;
	}];
}

extern double GCStartTime;
- (void)applicationDidBecomeActive:(UIApplication *)application
{
    // make sure a GC will not timeout because it was started before entering background
    GCStartTime = FPlatformTime::Seconds();
	/*
	 Restart any tasks that were paused (or not yet started) while the application was inactive. If the application was previously in the background, optionally refresh the user interface.
	 */
	RenderSuspend.Unlock();
	[self ToggleSuspend : false];
    [self ToggleAudioSession:true force:true];

    if (bEngineInit)
    {
		FEmbeddedCommunication::KeepAwake(TEXT("Background"), false);

       FGraphEventRef ResignTask = FFunctionGraphTask::CreateAndDispatchWhenReady([]()
       {
            FCoreDelegates::ApplicationHasReactivatedDelegate.Broadcast();
		
			FEmbeddedCommunication::AllowSleep(TEXT("Background"));
        }, TStatId(), NULL, ENamedThreads::GameThread);

		// Do not wait forever for this task to complete since the game thread may be stuck on waiting for user input from a modal dialog box
		double	startTime = FPlatformTime::Seconds();
 		while((FPlatformTime::Seconds() - startTime) < [self GetBackgroundingMainThreadBlockTime])
		{
			FPlatformProcess::Sleep(0.05f);
			if(ResignTask->IsComplete())
			{
				break;
			}
		}
    }
}

- (void)applicationWillTerminate:(UIApplication *)application
{
	/*
	 Called when the application is about to terminate.
	 Save data if appropriate.
	 See also applicationDidEnterBackground:.
	 */
	FCoreDelegates::ApplicationWillTerminateDelegate.Broadcast();
    
    // note that we are shutting down
    // TODO: fix the reason why we are hanging when asked to shutdown
/*    GIsRequestingExit = true;
    
    if (!bEngineInit)*/
    {
        // we haven't yet made it to the point where the engine is initialized, so just exit the app
        _Exit(0);
    }
/*    else
    {
        // wait for the game thread to shut down
        while (self.bHasStarted == true)
        {
            usleep(3);
        }
    }*/
}

- (void)applicationDidReceiveMemoryWarning:(UIApplication *)application
{
	/*
	Tells the delegate when the application receives a memory warning from the system
	*/
	FPlatformMisc::HandleLowMemoryWarning();
}

#if !PLATFORM_TVOS && BACKGROUNDFETCH_ENABLED // NOTE: TVOS can do this starting in tvOS 11
- (void)application:(UIApplication *)application performFetchWithCompletionHandler:(void(^)(UIBackgroundFetchResult result))completionHandler
{
    // NOTE: the completionHandler must be called within 30 seconds
    FCoreDelegates::ApplicationPerformFetchDelegate.Broadcast();
    completionHandler(UIBackgroundFetchResultNewData);
}

#endif

#if !PLATFORM_TVOS
- (void)application:(UIApplication *)application handleEventsForBackgroundURLSession:(NSString *)identifier completionHandler:(void(^)(void))completionHandler
{
    //Save off completionHandler so that a future call to FCoreDelegates::ApplicationBackgroundSessionEventsAllSentDelegate can execute it
    self.BackgroundSessionEventCompleteDelegate = completionHandler;
    
    //Create background session with this identifier if needed to handle these events
	FString Id(identifier);
	FBackgroundURLSessionHandler::InitBackgroundSession(Id);

	FCoreDelegates::ApplicationBackgroundSessionEventDelegate.Broadcast(Id);
}
#endif

#if !PLATFORM_TVOS && NOTIFICATIONS_ENABLED

- (void)application:(UIApplication *)application didRegisterForRemoteNotificationsWithDeviceToken:(NSData *)deviceToken
{
	if (FApp::IsUnattended())
	{
		return;
	}

	TArray<uint8> Token;
	Token.AddUninitialized([deviceToken length]);
	memcpy(Token.GetData(), [deviceToken bytes], [deviceToken length]);

	const char *data = (const char*)([deviceToken bytes]);
	NSMutableString *token = [NSMutableString string];

	for (NSUInteger i = 0; i < [deviceToken length]; i++) {
		[token appendFormat : @"%02.2hhX", data[i]];
	}

	UE_LOG(LogTemp, Display, TEXT("Device Token: %s"), *FString(token));

    FFunctionGraphTask::CreateAndDispatchWhenReady([Token]()
    {
		FCoreDelegates::ApplicationRegisteredForRemoteNotificationsDelegate.Broadcast(Token);
    }, TStatId(), NULL, ENamedThreads::GameThread);
}

-(void)application:(UIApplication *)application didFailtoRegisterForRemoteNotificationsWithError:(NSError *)error
{
	FString errorDescription([error description]);
	
    FFunctionGraphTask::CreateAndDispatchWhenReady([errorDescription]()
    {
		FCoreDelegates::ApplicationFailedToRegisterForRemoteNotificationsDelegate.Broadcast(errorDescription);
    }, TStatId(), NULL, ENamedThreads::GameThread);
}

#endif

#if !PLATFORM_TVOS

void HandleReceivedNotification(UNNotification* notification)
{
	bool IsLocal = false;
	
	if ([IOSAppDelegate GetDelegate].bEngineInit)
	{
		NSString* NotificationType = (NSString*)[notification.request.content.userInfo objectForKey: @"NotificationType"];
		if(NotificationType != nullptr)
		{
			FString LocalOrRemote(NotificationType);
			if(LocalOrRemote == FString(TEXT("Local")))
			{
				IsLocal = true;
			}
		}
		
		int AppState;
		if ([UIApplication sharedApplication].applicationState == UIApplicationStateInactive)
		{
			AppState = 1; // EApplicationState::Inactive;
		}
		else if ([UIApplication sharedApplication].applicationState == UIApplicationStateBackground)
		{
			AppState = 2; // EApplicationState::Background;
		}
		else
		{
			AppState = 3; // EApplicationState::Active;
		}
		
		if(IsLocal)
		{
			NSString*	activationEvent = (NSString*)[notification.request.content.userInfo objectForKey: @"ActivationEvent"];
			if(activationEvent != nullptr)
			{
				FString	activationEventFString(activationEvent);
				int32	fireDate = [notification.date timeIntervalSince1970];
				
				FFunctionGraphTask::CreateAndDispatchWhenReady([activationEventFString, fireDate, AppState]()
															   {
																   FCoreDelegates::ApplicationReceivedLocalNotificationDelegate.Broadcast(activationEventFString, fireDate, AppState);
															   }, TStatId(), NULL, ENamedThreads::GameThread);
			}
		}
		else
		{
			NSString* JsonString = @"{}";
			NSError* JsonError;
			NSData* JsonData = [NSJSONSerialization dataWithJSONObject : notification.request.content.userInfo
															   options : 0
																 error : &JsonError];
			
			if (JsonData)
			{
				JsonString = [[[NSString alloc] initWithData:JsonData encoding : NSUTF8StringEncoding] autorelease];
			}
			
			FString	jsonFString(JsonString);
			
			FFunctionGraphTask::CreateAndDispatchWhenReady([jsonFString, AppState]()
														   {
															   FCoreDelegates::ApplicationReceivedRemoteNotificationDelegate.Broadcast(jsonFString, AppState);
														   }, TStatId(), NULL, ENamedThreads::GameThread);
		}
	}
}

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
	   willPresentNotification:(UNNotification *)notification
		 withCompletionHandler:(void (^)(UNNotificationPresentationOptions options))completionHandler
{
	// Received notification while app is in the foreground
	HandleReceivedNotification(notification);
	
	completionHandler(UNNotificationPresentationOptionNone);
}

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
didReceiveNotificationResponse:(UNNotificationResponse *)response
		 withCompletionHandler:(void (^)())completionHandler
{
	// Received notification while app is in the background or closed
	
	// Save launch local notification so the app can check for it when it is ready
	NSDictionary* userInfo = response.notification.request.content.userInfo;
	if(userInfo != nullptr)
	{
		NSString*	activationEvent = (NSString*)[userInfo objectForKey: @"ActivationEvent"];
		
		if(activationEvent != nullptr)
		{
			FAppEntry::gAppLaunchedWithLocalNotification = true;
			FAppEntry::gLaunchLocalNotificationActivationEvent = FString(activationEvent);
			FAppEntry::gLaunchLocalNotificationFireDate = [response.notification.date timeIntervalSince1970];
		}
	}
	
	HandleReceivedNotification(response.notification);
	
	completionHandler();
}

#endif

/**
 * Shows the given Game Center supplied controller on the screen
 *
 * @param Controller The Controller object to animate onto the screen
 */
-(void)ShowController:(UIViewController*)Controller
{
	// slide it onto the screen
	[[IOSAppDelegate GetDelegate].IOSController presentViewController : Controller animated : YES completion : nil];
	
	// stop drawing the 3D world for faster UI speed
	//FViewport::SetGameRenderingEnabled(false);
}

/**
 * Hides the given Game Center supplied controller from the screen, optionally controller
 * animation (sliding off)
 *
 * @param Controller The Controller object to animate off the screen
 * @param bShouldAnimate YES to slide down, NO to hide immediately
 */
-(void)HideController:(UIViewController*)Controller Animated : (BOOL)bShouldAnimate
{
	// slide it off
	[Controller dismissViewControllerAnimated : bShouldAnimate completion : nil];

	// stop drawing the 3D world for faster UI speed
	//FViewport::SetGameRenderingEnabled(true);
}

/**
 * Hides the given Game Center supplied controller from the screen
 *
 * @param Controller The Controller object to animate off the screen
 */
-(void)HideController:(UIViewController*)Controller
{
	// call the other version with default animation of YES
	[self HideController : Controller Animated : YES];
}

-(void)gameCenterViewControllerDidFinish:(GKGameCenterViewController*)GameCenterDisplay
{
	// close the view 
	[self HideController : GameCenterDisplay];
}

/**
 * Show the leaderboard interface (call from iOS main thread)
 */
-(void)ShowLeaderboard:(NSString*)Category
{
	// create the leaderboard display object 
	GKGameCenterViewController* GameCenterDisplay = [[[GKGameCenterViewController alloc] init] autorelease];
#if !PLATFORM_TVOS
	GameCenterDisplay.viewState = GKGameCenterViewControllerStateLeaderboards;
#endif
#ifdef __IPHONE_7_0
	if ([GameCenterDisplay respondsToSelector : @selector(leaderboardIdentifier)] == YES)
	{
#if !PLATFORM_TVOS // @todo tvos: Why not??
		GameCenterDisplay.leaderboardIdentifier = Category;
#endif
	}
	else
#endif
	{
#if __IPHONE_OS_VERSION_MIN_REQUIRED < __IPHONE_7_0
		GameCenterDisplay.leaderboardCategory = Category;
#endif
	}
	GameCenterDisplay.gameCenterDelegate = self;

	// show it 
	[self ShowController : GameCenterDisplay];
}

/**
 * Show the achievements interface (call from iOS main thread)
 */
-(void)ShowAchievements
{
#if !PLATFORM_TVOS
	// create the leaderboard display object 
	GKGameCenterViewController* GameCenterDisplay = [[[GKGameCenterViewController alloc] init] autorelease];
    if (@available(iOS 7, tvOS 999, *))
    {
	GameCenterDisplay.viewState = GKGameCenterViewControllerStateAchievements;
    }
	GameCenterDisplay.gameCenterDelegate = self;

	// show it 
	[self ShowController : GameCenterDisplay];
#endif // !PLATFORM_TVOS
}

/**
 * Show the leaderboard interface (call from game thread)
 */
CORE_API bool IOSShowLeaderboardUI(const FString& CategoryName)
{
	// route the function to iOS thread, passing the category string along as the object
	NSString* CategoryToShow = [NSString stringWithFString : CategoryName];
	[[IOSAppDelegate GetDelegate] performSelectorOnMainThread:@selector(ShowLeaderboard : ) withObject:CategoryToShow waitUntilDone : NO];

	return true;
}

/**
* Show the achievements interface (call from game thread)
*/
CORE_API bool IOSShowAchievementsUI()
{

	// route the function to iOS thread
	[[IOSAppDelegate GetDelegate] performSelectorOnMainThread:@selector(ShowAchievements) withObject:nil waitUntilDone : NO];

	return true;
}

-(void)batteryChanged:(NSNotification*)notification
{
#if !PLATFORM_TVOS
    UIDevice* Device = [UIDevice currentDevice];
    
    // Battery level is from 0.0 to 1.0, get it in terms of 0-100
    self.BatteryLevel = ((int)([Device batteryLevel] * 100));
    UE_LOG(LogIOS, Display, TEXT("Battery Level Changed: %d"), self.BatteryLevel);
#endif
}

-(void)batteryStateChanged:(NSNotification*)notification
{
#if !PLATFORM_TVOS
    UIDevice* Device = [UIDevice currentDevice];
    UIDeviceBatteryState State = Device.batteryState;
    self.bBatteryState = State == UIDeviceBatteryStateUnplugged || State == UIDeviceBatteryStateUnknown;
    UE_LOG(LogIOS, Display, TEXT("Battery State Changed: %d"), self.bBatteryState);
#endif
}

-(void)temperatureChanged:(NSNotification *)notification
{
#if !PLATFORM_TVOS
	if (@available(iOS 11, *))
	{
		// send game callback with new temperature severity
		FCoreDelegates::ETemperatureSeverity Severity;
        FString Level = TEXT("Unknown");
        self.ThermalState = [[NSProcessInfo processInfo] thermalState];
		switch (self.ThermalState)
		{
			case NSProcessInfoThermalStateNominal:	Severity = FCoreDelegates::ETemperatureSeverity::Good; Level = TEXT("Good"); break;
			case NSProcessInfoThermalStateFair:		Severity = FCoreDelegates::ETemperatureSeverity::Bad; Level = TEXT("Bad"); break;
			case NSProcessInfoThermalStateSerious:	Severity = FCoreDelegates::ETemperatureSeverity::Serious; Level = TEXT("Serious"); break;
			case NSProcessInfoThermalStateCritical:	Severity = FCoreDelegates::ETemperatureSeverity::Critical; Level = TEXT("Critical"); break;
		}

        UE_LOG(LogIOS, Display, TEXT("Temperaure Changed: %s"), *Level);
		FCoreDelegates::OnTemperatureChange.Broadcast(Severity);
	}
#endif
}

-(void)lowPowerModeChanged:(NSNotification *)notification
{
#if !PLATFORM_TVOS
	if (@available(iOS 11, *))
	{	
		[FIOSAsyncTask CreateTaskWithBlock : ^ bool(void)
        {
            bool bInLowPowerMode = [[NSProcessInfo processInfo] isLowPowerModeEnabled];
            UE_LOG(LogIOS, Display, TEXT("Low Power Mode Changed: %d"), bInLowPowerMode);
            FCoreDelegates::OnLowPowerMode.Broadcast(bInLowPowerMode);
            return true;
        }];
	}
#endif
}

-(UIWindow*)window
{
	return Window;
}

@end
