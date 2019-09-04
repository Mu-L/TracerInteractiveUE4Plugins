// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Android/AndroidEventManager.h"

#if USE_ANDROID_EVENTS
#include "Android/AndroidApplication.h"
#include "AudioDevice.h"
#include "Misc/CallbackDevice.h"
#include <android/native_window.h> 
#include <android/native_window_jni.h> 
#include "IHeadMountedDisplay.h"
#include "IXRTrackingSystem.h"
#include "RenderingThread.h"
#include "UnrealEngine.h"

DEFINE_LOG_CATEGORY(LogAndroidEvents);

FAppEventManager* FAppEventManager::sInstance = NULL;


FAppEventManager* FAppEventManager::GetInstance()
{
	if(!sInstance)
	{
		sInstance = new FAppEventManager();
	}

	return sInstance;
}

static const TCHAR* GetAppEventName(EAppEventState State)
{
	const TCHAR* Names[] = {
		TEXT("APP_EVENT_STATE_WINDOW_CREATED"),
		TEXT("APP_EVENT_STATE_WINDOW_RESIZED"),
		TEXT("APP_EVENT_STATE_WINDOW_CHANGED"),
		TEXT("APP_EVENT_STATE_WINDOW_DESTROYED"),
		TEXT("APP_EVENT_STATE_WINDOW_REDRAW_NEEDED"),
		TEXT("APP_EVENT_STATE_ON_DESTROY"),
		TEXT("APP_EVENT_STATE_ON_PAUSE"),
		TEXT("APP_EVENT_STATE_ON_RESUME"),
		TEXT("APP_EVENT_STATE_ON_STOP"),
		TEXT("APP_EVENT_STATE_ON_START"),
		TEXT("APP_EVENT_STATE_WINDOW_LOST_FOCUS"),
		TEXT("APP_EVENT_STATE_WINDOW_GAINED_FOCUS"),
		TEXT("APP_EVENT_STATE_SAVE_STATE")
		};


	if (State == APP_EVENT_STATE_INVALID)
	{
		return TEXT("APP_EVENT_STATE_INVALID");
	}
	else if (State > APP_EVENT_STATE_SAVE_STATE || State < 0)
	{
		return TEXT("UnknownEAppEventStateValue");
	}
	else
	{
		return Names[State];
	}
}


void FAppEventManager::Tick()
{
	static const bool bIsDaydreamApp = FAndroidMisc::IsDaydreamApplication();
	bool bWindowCreatedThisTick = false;
	
	while (!Queue.IsEmpty())
	{
		bool bDestroyWindow = false;
		bool bShuttingDown = false;

		FAppEventData Event = DequeueAppEvent();

		switch (Event.State)
		{
		case APP_EVENT_STATE_WINDOW_CREATED:
			// if we have a "destroy window" event pending, the data has been invalidated
			if (!bDestroyWindowPending)
			{
				bCreateWindow = true;
				PendingWindow = (ANativeWindow*)Event.Data;
				FPlatformMisc::LowLevelOutputDebugStringf(TEXT("APP_EVENT_STATE_WINDOW_CREATED %d, %d, %d, %d"), int(bDestroyWindowPending), int(bRunning), int(bHaveWindow), int(bHaveGame));

			}
			else
			{
				// If we skip a create window because it is destroyed before we created the window *something* gets out of sync
				// resulting in either one buffer still in the wrong orienation or just a black screen.
				// So reset everything here. When the new window is created we will recover successfully.

				FAndroidAppEntry::DestroyWindow();
				FAndroidWindow::SetHardwareWindow(NULL);

				PauseRendering();
				PauseAudio();
				FPlatformMisc::LowLevelOutputDebugStringf(TEXT("APP_EVENT_STATE_WINDOW_CREATED window creation skipped because a destroy is pending %d, %d, %d, %d"), int(bDestroyWindowPending), int(bRunning), int(bHaveWindow), int(bHaveGame));

			}
			break;
		
		case APP_EVENT_STATE_WINDOW_RESIZED:
		case APP_EVENT_STATE_WINDOW_CHANGED:
			// React on device orientation/windowSize changes only when application has window
			// In case window was created this tick it should already has correct size
			if (bHaveWindow && !bWindowCreatedThisTick)
			{
				ExecWindowResized();
			}
			break;
		case APP_EVENT_STATE_SAVE_STATE:
			bSaveState = true; //todo android: handle save state.
			break;
		case APP_EVENT_STATE_WINDOW_DESTROYED:
			// only if precedeed by a a successfull "create window" event 
			if (bHaveWindow)
			{
				if (bIsDaydreamApp)
				{
					bCreateWindow = false;
				}
				else
				{
					if (GEngine != nullptr && GEngine->IsInitialized() && GEngine->XRSystem.IsValid() && GEngine->XRSystem->GetHMDDevice() && GEngine->XRSystem->GetHMDDevice()->IsHMDConnected())
					{
						// delay the destruction until after the renderer teardown on Oculus Mobile
						bDestroyWindow = true;
					}
					else
					{
						//FAndroidAppEntry::DestroyWindow();
						//FAndroidWindow::SetHardwareWindow(NULL);
						bDestroyWindow = true;
					}
				}
			}

			bHaveWindow = false;

			// allow further "create window" events to be processed 
			bDestroyWindowPending = false;
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("APP_EVENT_STATE_WINDOW_DESTROYED, %d, %d, %d"), int(bRunning), int(bHaveWindow), int(bHaveGame));
			break;
		case APP_EVENT_STATE_ON_START:
			//doing nothing here
			break;
		case APP_EVENT_STATE_ON_DESTROY:
			if (FTaskGraphInterface::IsRunning())
			{
				FGraphEventRef WillTerminateTask = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
				{
					FCoreDelegates::ApplicationWillTerminateDelegate.Broadcast();
				}, TStatId(), NULL, ENamedThreads::GameThread);
				FTaskGraphInterface::Get().WaitUntilTaskCompletes(WillTerminateTask);
			}
			GIsRequestingExit = true; //destroy immediately. Game will shutdown.
			FirstInitialized = false;
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("APP_EVENT_STATE_ON_DESTROY"));
			break;
		case APP_EVENT_STATE_ON_STOP:
			bShuttingDown = true;
			bHaveGame = false;
			break;
		case APP_EVENT_STATE_ON_PAUSE:
			FAndroidAppEntry::OnPauseEvent();
			bHaveGame = false;
			break;
		case APP_EVENT_STATE_ON_RESUME:
			bHaveGame = true;
			break;

		// window focus events that follow their own hierarchy, and might or might not respect App main events hierarchy

		case APP_EVENT_STATE_WINDOW_GAINED_FOCUS: 
			bWindowInFocus = true;
			break;
		case APP_EVENT_STATE_WINDOW_LOST_FOCUS:
			bWindowInFocus = false;
			break;

		default:
			UE_LOG(LogAndroidEvents, Display, TEXT("Application Event : %u  not handled. "), Event.State);
		}

		if (bCreateWindow)
		{
			// wait until activity is in focus.
			if (bWindowInFocus) 
			{
				ExecWindowCreated();
				bCreateWindow = false;
				bHaveWindow = true;
				bWindowCreatedThisTick = true;

				FPlatformMisc::LowLevelOutputDebugStringf(TEXT("ExecWindowCreated, %d, %d, %d"), int(bRunning), int(bHaveWindow), int(bHaveGame));
			}
		}

		if (!bRunning && bHaveWindow && bHaveGame)
		{
			ResumeRendering();
			ResumeAudio();

			// broadcast events after the rendering thread has resumed
			if (FTaskGraphInterface::IsRunning())
			{
				FGraphEventRef EnterForegroundTask = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
				{
					FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Broadcast();
				}, TStatId(), NULL, ENamedThreads::GameThread);

				FGraphEventRef ReactivateTask = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
				{
					FCoreDelegates::ApplicationHasReactivatedDelegate.Broadcast();
				}, TStatId(), EnterForegroundTask, ENamedThreads::GameThread);
				FTaskGraphInterface::Get().WaitUntilTaskCompletes(ReactivateTask);

				extern void AndroidThunkCpp_ShowHiddenAlertDialog();
				AndroidThunkCpp_ShowHiddenAlertDialog();
			}

			bRunning = true;
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Execution has been resumed!"));
		}
		else if (bRunning && (!bHaveWindow || !bHaveGame))
		{
			// broadcast events before rendering thread suspends
			if (FTaskGraphInterface::IsRunning())
			{
				FGraphEventRef DeactivateTask = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
				{
					FCoreDelegates::ApplicationWillDeactivateDelegate.Broadcast();
				}, TStatId(), NULL, ENamedThreads::GameThread);
				FGraphEventRef EnterBackgroundTask = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
				{
					FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Broadcast();
				}, TStatId(), DeactivateTask, ENamedThreads::GameThread);
				FTaskGraphInterface::Get().WaitUntilTaskCompletes(EnterBackgroundTask);
			}

			PauseRendering();
			PauseAudio();
			ReleaseMicrophone(bShuttingDown);

			bRunning = false;
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Execution has been paused..."));
		}

		if (bDestroyWindow)
		{
			FAndroidAppEntry::DestroyWindow();
			FAndroidWindow::SetHardwareWindow(NULL);
			bDestroyWindow = false;

			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("FAndroidAppEntry::DestroyWindow() called"));
		}
	}

	if (EmptyQueueHandlerEvent)
	{
		EmptyQueueHandlerEvent->Trigger();
	}
	if (bIsDaydreamApp)
	{
		if (!bRunning && FAndroidWindow::GetHardwareWindow() != NULL)
		{
			EventHandlerEvent->Wait();
		}
	}
	else
	{
		if (!bRunning && FirstInitialized)
		{
			EventHandlerEvent->Wait();
		}
	}
}

void FAppEventManager::ReleaseMicrophone(bool shuttingDown)
{
	if (FModuleManager::Get().IsModuleLoaded("Voice"))
	{
		UE_LOG(LogTemp, Log, TEXT("Android release microphone"));
		FModuleManager::Get().UnloadModule("Voice", shuttingDown);
	}
}

void FAppEventManager::TriggerEmptyQueue()
{
	if (EmptyQueueHandlerEvent)
	{
		EmptyQueueHandlerEvent->Trigger();
	}
}

FAppEventManager::FAppEventManager():
	EventHandlerEvent(nullptr)
	,EmptyQueueHandlerEvent(nullptr)
	,FirstInitialized(false)
	,bCreateWindow(false)
	,bWindowInFocus(true)
	,bSaveState(false)
	,bAudioPaused(false)
	,PendingWindow(NULL)
	,bHaveWindow(false)
	,bHaveGame(false)
	,bRunning(false)
	,bDestroyWindowPending(false)
{
	pthread_mutex_init(&MainMutex, NULL);
	pthread_mutex_init(&QueueMutex, NULL);

	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MobileContentScaleFactor"));
	check(CVar);
	CVar->SetOnChangedCallback(FConsoleVariableDelegate::CreateStatic(&FAppEventManager::OnScaleFactorChanged));
}

void FAppEventManager::OnScaleFactorChanged(IConsoleVariable* CVar)
{
	if ((CVar->GetFlags() & ECVF_SetByMask) == ECVF_SetByConsole)
	{
		FAppEventManager::GetInstance()->ExecWindowResized();
	}
}

void FAppEventManager::HandleWindowCreated(void* InWindow)
{
	static const bool bIsDaydreamApp = FAndroidMisc::IsDaydreamApplication();
	if (bIsDaydreamApp)
	{
		// We must ALWAYS set the hardware window immediately,
		// Otherwise we will temporarily end up with an abandoned Window
		// when the application is pausing/resuming. This is likely
		// to happen in a Gvr app due to the DON flow pushing an activity
		// during initialization.

		int rc = pthread_mutex_lock(&MainMutex);
		check(rc == 0);

		// If we already have a window, destroy it
		ExecDestroyWindow();

		FAndroidWindow::SetHardwareWindow(InWindow);

		rc = pthread_mutex_unlock(&MainMutex);
		check(rc == 0);

		// Make sure window will not be deleted until event is processed
		// Window could be deleted by OS while event queue stuck at game start-up phase
		FAndroidWindow::AcquireWindowRef((ANativeWindow*)InWindow);

		EnqueueAppEvent(APP_EVENT_STATE_WINDOW_CREATED, InWindow);
		return;
	}

	int rc = pthread_mutex_lock(&MainMutex);
	check(rc == 0);
	bool AlreadyInited = FirstInitialized;
	rc = pthread_mutex_unlock(&MainMutex);
	check(rc == 0);

	// Make sure window will not be deleted until event is processed
	// Window could be deleted by OS while event queue stuck at game start-up phase
	FAndroidWindow::AcquireWindowRef((ANativeWindow*)InWindow);

	if (AlreadyInited)
	{
		EnqueueAppEvent(APP_EVENT_STATE_WINDOW_CREATED, InWindow);
	}
	else
	{
		//This cannot wait until first tick. 

		rc = pthread_mutex_lock(&MainMutex);
		check(rc == 0);

		check(FAndroidWindow::GetHardwareWindow() == NULL);
		FAndroidWindow::SetHardwareWindow(InWindow);
		FirstInitialized = true;

		rc = pthread_mutex_unlock(&MainMutex);
		check(rc == 0);

		EnqueueAppEvent(APP_EVENT_STATE_WINDOW_CREATED, InWindow);
	}
}

void FAppEventManager::HandleWindowClosed()
{
	static const bool bIsDaydreamApp = FAndroidMisc::IsDaydreamApplication();
	if (bIsDaydreamApp)
	{
		// We must ALWAYS destroy the hardware window immediately,
		// Otherwise we will temporarily end up with an abandoned Window
		// when the application is pausing/resuming. This is likely
		// to happen in a Gvr app due to the DON flow pushing an activity
		// during initialization.

		int rc = pthread_mutex_lock(&MainMutex);
		check(rc == 0);

		ExecDestroyWindow();

		rc = pthread_mutex_unlock(&MainMutex);
		check(rc == 0);
	}

	// a "destroy window" event appears on the game preInit routine
	//     before creating a valid Android window
	// - override the "create window" data
	if (!GEngine || !GEngine->IsInitialized())
	{
		FirstInitialized = false;
		FAndroidWindow::SetHardwareWindow(NULL);
		bDestroyWindowPending = true;
	}
	EnqueueAppEvent(APP_EVENT_STATE_WINDOW_DESTROYED, NULL);
}


void FAppEventManager::SetEventHandlerEvent(FEvent* InEventHandlerEvent)
{
	EventHandlerEvent = InEventHandlerEvent;
}

void FAppEventManager::SetEmptyQueueHandlerEvent(FEvent* InEventHandlerEvent)
{
	EmptyQueueHandlerEvent = InEventHandlerEvent;
}

void FAppEventManager::PauseRendering()
{
	if(GUseThreadedRendering )
	{
		if (GIsThreadedRendering)
		{
			StopRenderingThread(); 
		}
	}
	else
	{
		RHIReleaseThreadOwnership();
	}
}


void FAppEventManager::ResumeRendering()
{
	if( GUseThreadedRendering )
	{
		if (!GIsThreadedRendering)
		{
			StartRenderingThread();
		}
	}
	else
	{
		RHIAcquireThreadOwnership();
	}
}


void FAppEventManager::ExecWindowCreated()
{
	UE_LOG(LogAndroidEvents, Display, TEXT("ExecWindowCreated"));

	static bool bIsDaydreamApp = FAndroidMisc::IsDaydreamApplication();
	if (!bIsDaydreamApp)
	{
		check(PendingWindow);
		FAndroidWindow::SetHardwareWindow(PendingWindow);
	}

	// When application launched while device is in sleep mode SystemResolution could be set to opposite orientation values
	// Force to update SystemResolution to current values whenever we create a new window
	FPlatformRect ScreenRect = FAndroidWindow::GetScreenRect();
	FSystemResolution::RequestResolutionChange(ScreenRect.Right, ScreenRect.Bottom, EWindowMode::Fullscreen);

	// ReInit with the new window handle, null for daydream case.
	FAndroidAppEntry::ReInitWindow(!bIsDaydreamApp ? PendingWindow : nullptr);

	if (!bIsDaydreamApp)
	{
		// We hold this reference to ensure that window will not be deleted while game starting up
		// release it when window is finally initialized
		FAndroidWindow::ReleaseWindowRef(PendingWindow);
		PendingWindow = nullptr;
	}

	FAndroidApplication::OnWindowSizeChanged();
}

void FAppEventManager::ExecWindowResized()
{
	if (bRunning)
	{
		FlushRenderingCommands();
	}
	FAndroidWindow::InvalidateCachedScreenRect();
	FAndroidAppEntry::ReInitWindow();
	FAndroidApplication::OnWindowSizeChanged();
}

void FAppEventManager::ExecDestroyWindow()
{
	if (FAndroidWindow::GetHardwareWindow() != NULL)
	{
		FAndroidWindow::ReleaseWindowRef((ANativeWindow*)FAndroidWindow::GetHardwareWindow());

		FAndroidAppEntry::DestroyWindow();
		FAndroidWindow::SetHardwareWindow(NULL);
	}
}

void FAppEventManager::PauseAudio()
{
	if (!GEngine || !GEngine->IsInitialized())
	{
		UE_LOG(LogTemp, Log, TEXT("Engine not initialized, not pausing Android audio"));
		return;
	}

	bAudioPaused = true;
	UE_LOG(LogTemp, Log, TEXT("Android pause audio"));

	FAudioDevice* AudioDevice = GEngine->GetMainAudioDevice();
	if (AudioDevice)
	{
		if (AudioDevice->IsAudioMixerEnabled())
		{
			AudioDevice->SuspendContext();
		}
		else
		{
			GEngine->GetMainAudioDevice()->Suspend(false);

			// make sure the audio thread runs the pause request
			FAudioCommandFence Fence;
			Fence.BeginFence();
			Fence.Wait();
		}
	}
}


void FAppEventManager::ResumeAudio()
{
	if (!GEngine || !GEngine->IsInitialized())
	{
		UE_LOG(LogTemp, Log, TEXT("Engine not initialized, not resuming Android audio"));
		return;
	}

	bAudioPaused = false;
	UE_LOG(LogTemp, Log, TEXT("Android resume audio"));

	FAudioDevice* AudioDevice = GEngine->GetMainAudioDevice();
	if (AudioDevice)
	{
		if (AudioDevice->IsAudioMixerEnabled())
		{
			AudioDevice->ResumeContext();
		}
		else
		{
			GEngine->GetMainAudioDevice()->Suspend(true);
		}
	}
}


void FAppEventManager::EnqueueAppEvent(EAppEventState InState, void* InData)
{
	FAppEventData Event;
	Event.State = InState;
	Event.Data = InData;

	int rc = pthread_mutex_lock(&QueueMutex);
	check(rc == 0);
	Queue.Enqueue(Event);

	if (EmptyQueueHandlerEvent)
	{
		EmptyQueueHandlerEvent->Reset();
	}

	rc = pthread_mutex_unlock(&QueueMutex);
	check(rc == 0);

	FPlatformMisc::LowLevelOutputDebugStringf(TEXT("LogAndroidEvents::EnqueueAppEvent : %u, %u, tid = %d, %s"), InState, (uintptr_t)InData, gettid(), GetAppEventName(InState));
}


FAppEventData FAppEventManager::DequeueAppEvent()
{
	int rc = pthread_mutex_lock(&QueueMutex);
	check(rc == 0);

	FAppEventData OutData;
	Queue.Dequeue( OutData );

	rc = pthread_mutex_unlock(&QueueMutex);
	check(rc == 0);

	UE_LOG(LogAndroidEvents, Display, TEXT("LogAndroidEvents::DequeueAppEvent : %u, %u, %s"), OutData.State, (uintptr_t)OutData.Data, GetAppEventName(OutData.State))

	return OutData;
}


bool FAppEventManager::IsGamePaused()
{
	return !bRunning;
}


bool FAppEventManager::IsGameInFocus()
{
	return (bWindowInFocus && bHaveWindow);
}


bool FAppEventManager::WaitForEventInQueue(EAppEventState InState, double TimeoutSeconds)
{
	bool FoundEvent = false;
	double StopTime = FPlatformTime::Seconds() + TimeoutSeconds;

	TQueue<FAppEventData, EQueueMode::Spsc> HoldingQueue;
	while (!FoundEvent)
	{
		int rc = pthread_mutex_lock(&QueueMutex);
		check(rc == 0);

		// Copy the existing queue (and check for our event)
		while (!Queue.IsEmpty())
		{
			FAppEventData OutData;
			Queue.Dequeue(OutData);

			if (OutData.State == InState)
				FoundEvent = true;

			HoldingQueue.Enqueue(OutData);
		}

		if (FoundEvent)
			break;

		// Time expired?
		if (FPlatformTime::Seconds() > StopTime)
			break;

		// Unlock for new events and wait a bit before trying again
		rc = pthread_mutex_unlock(&QueueMutex);
		check(rc == 0);
		FPlatformProcess::Sleep(0.01f);
	}

	// Add events back to queue from holding
	while (!HoldingQueue.IsEmpty())
	{
		FAppEventData OutData;
		HoldingQueue.Dequeue(OutData);
		Queue.Enqueue(OutData);
	}

	int rc = pthread_mutex_unlock(&QueueMutex);
	check(rc == 0);

	return FoundEvent;
}

extern volatile bool GEventHandlerInitialized;

void FAppEventManager::WaitForEmptyQueue()
{
	if (EmptyQueueHandlerEvent && GEventHandlerInitialized && !GIsRequestingExit)
	{
		EmptyQueueHandlerEvent->Wait();
	}
}

#endif