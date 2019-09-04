// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "IOSVivoxVoiceChat.h"

#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "IOS/IOSAppDelegate.h"

TUniquePtr<FVivoxVoiceChat> CreateVivoxObject()
{
	return MakeUnique<FIOSVivoxVoiceChat>();
}

FIOSVivoxVoiceChat::FIOSVivoxVoiceChat()
	: BGTask(UIBackgroundTaskInvalid)
	, bDisconnectInBackground(true)
	, bInBackground(false)
	, bShouldReconnect(false)
	, bIsRecording(false)
	, BackgroundDelayedDisconnectTime(0.0f)
	, DelayedDisconnectTimer(nullptr)
{
}

FIOSVivoxVoiceChat::~FIOSVivoxVoiceChat()
{
}

bool FIOSVivoxVoiceChat::Initialize()
{
	bool bResult = FVivoxVoiceChat::Initialize();

	if (bResult)
	{
		GConfig->GetBool(TEXT("VoiceChat.Vivox"), TEXT("bDisconnectInBackground"), bDisconnectInBackground, GEngineIni);
		GConfig->GetFloat(TEXT("VoiceChat.Vivox"), TEXT("BackgroundDelayedDisconnectTime"), BackgroundDelayedDisconnectTime, GEngineIni);
		
		if (!ApplicationWillEnterBackgroundHandle.IsValid())
		{
			ApplicationWillEnterBackgroundHandle = FCoreDelegates::ApplicationWillDeactivateDelegate.AddRaw(this, &FIOSVivoxVoiceChat::HandleApplicationWillEnterBackground);
		}
		if (!ApplicationDidEnterForegroundHandle.IsValid())
		{
			ApplicationDidEnterForegroundHandle = FCoreDelegates::ApplicationHasReactivatedDelegate.AddRaw(this, &FIOSVivoxVoiceChat::HandleApplicationHasEnteredForeground);
		}
	}

	vx_set_platform_aec_enabled(1);

	bInBackground = false;
	bShouldReconnect = false;
	bIsRecording = false;

	return bResult;
}

bool FIOSVivoxVoiceChat::Uninitialize()
{
	if (ApplicationWillEnterBackgroundHandle.IsValid())
	{
		FCoreDelegates::ApplicationWillDeactivateDelegate.Remove(ApplicationWillEnterBackgroundHandle);
		ApplicationWillEnterBackgroundHandle.Reset();
	}
	if (ApplicationDidEnterForegroundHandle.IsValid())
	{
		FCoreDelegates::ApplicationHasReactivatedDelegate.Remove(ApplicationDidEnterForegroundHandle);
		ApplicationDidEnterForegroundHandle.Reset();
	}

	return FVivoxVoiceChat::Uninitialize();
}

FDelegateHandle FIOSVivoxVoiceChat::StartRecording(const FOnVoiceChatRecordSamplesAvailableDelegate::FDelegate& Delegate)
{
	FPlatformMisc::EnableVoiceChat(true);
	return FVivoxVoiceChat::StartRecording(Delegate);
}

void FIOSVivoxVoiceChat::StopRecording(FDelegateHandle Handle)
{
	FVivoxVoiceChat::StopRecording(Handle);
	if (ConnectionState < EConnectionState::Connecting)
	{
		FPlatformMisc::EnableVoiceChat(false);
	}
}

void FIOSVivoxVoiceChat::onConnectCompleted(const VivoxClientApi::Uri& Server)
{
	FPlatformMisc::EnableVoiceChat(true);
	FVivoxVoiceChat::onConnectCompleted(Server);
}

void FIOSVivoxVoiceChat::onDisconnected(const VivoxClientApi::Uri& Server, const VivoxClientApi::VCSStatus& Status)
{
	FVivoxVoiceChat::onDisconnected(Server, Status);
	if (!bIsRecording)
	{
		FPlatformMisc::EnableVoiceChat(false);
	}
}

void FIOSVivoxVoiceChat::OnVoiceChatConnectComplete(const FVoiceChatResult& Result)
{
	if (Result.bSuccess)
	{
		OnVoiceChatReconnectedDelegate.Broadcast();
	}
	else
	{
		OnVoiceChatDisconnectedDelegate.Broadcast(Result);
	}
}

void FIOSVivoxVoiceChat::OnVoiceChatDisconnectComplete(const FVoiceChatResult& Result)
{
	if (bInBackground)
	{
		bShouldReconnect = true;
	}
	else if (IsInitialized())
	{
		// disconnect complete delegate fired after entering foreground
		Reconnect();
	}
	
	if (BGTask != UIBackgroundTaskInvalid)
	{
		UIApplication* App = [UIApplication sharedApplication];
		[App endBackgroundTask:BGTask];
		BGTask = UIBackgroundTaskInvalid;
	}
}

void FIOSVivoxVoiceChat::OnVoiceChatDelayedDisconnectComplete(const FVoiceChatResult& Result)
{
	OnVoiceChatDisconnectedDelegate.Broadcast(Result);
}

void FIOSVivoxVoiceChat::SetVivoxSdkConfigHints(vx_sdk_config_t &Hints)
{
	Hints.dynamic_voice_processing_switching = 0;
}

void FIOSVivoxVoiceChat::HandleApplicationWillEnterBackground()
{
	UE_LOG(LogVivoxVoiceChat, Log, TEXT("OnApplicationWillEnterBackgroundDelegate"));

	const bool bIsBackgroundAudioEnabled = [[IOSAppDelegate GetDelegate] IsFeatureActive:EAudioFeature::BackgroundAudio];
	if (IsConnected() && bDisconnectInBackground && !bIsBackgroundAudioEnabled)
	{
		if (BGTask != UIBackgroundTaskInvalid)
		{
			UIApplication* App = [UIApplication sharedApplication];
			[App endBackgroundTask : BGTask];
			BGTask = UIBackgroundTaskInvalid;
		}
		UIApplication* App = [UIApplication sharedApplication];
		BGTask = [App beginBackgroundTaskWithName : @"VivoxDisconnect" expirationHandler : ^{
			UE_LOG(LogVivoxVoiceChat, Warning, TEXT("Disconnect operation never completed"));

			[App endBackgroundTask : BGTask];
			BGTask = UIBackgroundTaskInvalid;
		}];

		Disconnect(FOnVoiceChatDisconnectCompleteDelegate::CreateRaw(this, &FIOSVivoxVoiceChat::OnVoiceChatDisconnectComplete));
	}
	else
	{
		if (BackgroundDelayedDisconnectTime > 0.01)
		{
			dispatch_async(dispatch_get_main_queue(), ^
			{
				DelayedDisconnectTimer = [NSTimer scheduledTimerWithTimeInterval:BackgroundDelayedDisconnectTime repeats:NO block:^(NSTimer * _Nonnull timer) {
					[FIOSAsyncTask CreateTaskWithBlock:^bool(void){
						Disconnect(FOnVoiceChatDisconnectCompleteDelegate::CreateRaw(this, &FIOSVivoxVoiceChat::OnVoiceChatDelayedDisconnectComplete));
						return true;
					}];
					DelayedDisconnectTimer = nullptr;
				}];
			});
			
		}
		bShouldReconnect = false;
	}
	
	VivoxClientConnection.EnteredBackground();
}

void FIOSVivoxVoiceChat::HandleApplicationHasEnteredForeground()
{
	UE_LOG(LogVivoxVoiceChat, Log, TEXT("OnApplicationHasEnteredForegoundDelegate"));

	VivoxClientConnection.WillEnterForeground();

	if (BGTask != UIBackgroundTaskInvalid)
	{
		UIApplication* App = [UIApplication sharedApplication];
		[App endBackgroundTask : BGTask];
		BGTask = UIBackgroundTaskInvalid;
	}

	dispatch_async(dispatch_get_main_queue(), ^
	{
		[DelayedDisconnectTimer invalidate];
		DelayedDisconnectTimer = nullptr;
	});

	if (bShouldReconnect)
	{
		Reconnect();
	}
}

void FIOSVivoxVoiceChat::Reconnect()
{
	Connect(FOnVoiceChatConnectCompleteDelegate::CreateRaw(this, &FIOSVivoxVoiceChat::OnVoiceChatConnectComplete));
	bShouldReconnect = false;
}
