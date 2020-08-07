// Copyright Epic Games, Inc. All Rights Reserved.
#include "AudioDeviceManager.h"

#include "Audio.h"
#include "Audio/AudioDebug.h"
#include "AudioDefines.h"
#include "AudioDevice.h"
#include "AudioMixerDevice.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundWave.h"
#include "GameFramework/GameUserSettings.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "UObject/UObjectIterator.h"
#include "Audio/AudioDebug.h"

#if INSTRUMENT_AUDIODEVICE_HANDLES
#include "Containers/StringConv.h"
#include "HAL/PlatformStackWalk.h"
#endif


#if WITH_EDITOR
#include "AudioEditorModule.h"
#include "Settings/LevelEditorMiscSettings.h"
#endif

static int32 GCVarEnableAudioThreadWait = 1;
TAutoConsoleVariable<int32> CVarEnableAudioThreadWait(
	TEXT("AudioThread.EnableAudioThreadWait"),
	GCVarEnableAudioThreadWait,
	TEXT("Enables waiting on the audio thread to finish its commands.\n")
	TEXT("0: Not Enabled, 1: Enabled"),
	ECVF_Default);

static int32 GCvarIsUsingAudioMixer = 0;
FAutoConsoleVariableRef CVarIsUsingAudioMixer(
	TEXT("au.IsUsingAudioMixer"),
	GCvarIsUsingAudioMixer,
	TEXT("Whether or not we're currently using the audio mixer. Change to dynamically toggle on/off. Note: sounds will stop. Looping sounds won't automatically resume. \n")
	TEXT("0: Not Using Audio Mixer, 1: Using Audio Mixer"),
	ECVF_Default);


static int32 CVarIsVisualizeEnabled = 0;
FAutoConsoleVariableRef CVarAudioVisualizeEnabled(
	TEXT("au.3dVisualize.Enabled"),
	CVarIsVisualizeEnabled,
	TEXT("Whether or not audio visualization is enabled. \n")
	TEXT("0: Not Enabled, 1: Enabled"),
	ECVF_Default);

static int32 GCVarFlushAudioRenderCommandsOnSuspend = 0;
FAutoConsoleVariableRef CVarFlushAudioRenderCommandsOnSuspend(
	TEXT("au.FlushAudioRenderCommandsOnSuspend"),
	GCVarFlushAudioRenderCommandsOnSuspend,
	TEXT("When set to 1, ensures that we pump through all pending commands to the audio thread and audio render thread on app suspension.\n")
	TEXT("0: Not Disabled, 1: Disabled"),
	ECVF_Default);

static FAutoConsoleCommand GReportAudioDevicesCommand(
	TEXT("au.ReportAudioDevices"),
	TEXT("This will log any active audio devices (instances of the audio engine) alive right now."),
	FConsoleCommandDelegate::CreateStatic(
		[]()
	{
		FAudioDeviceManager::Get()->LogListOfAudioDevices();
	})
);

// Some stress tests:
#if INSTRUMENT_AUDIODEVICE_HANDLES
static TArray<FAudioDeviceHandle> IntentionallyLeakedHandles;

static FAutoConsoleCommand GLeakAudioDeviceCommand(
	TEXT("au.stresstest.LeakAnAudioDevice"),
	TEXT("This will intentionally leak a new audio device. Obviously, should only be used for testing."),
	FConsoleCommandDelegate::CreateStatic(
		[]()
{
	FAudioDeviceParams Params;
	Params.Scope = EAudioDeviceScope::Unique;
	IntentionallyLeakedHandles.Add(FAudioDeviceManager::Get()->RequestAudioDevice(Params));
})
);

static FAutoConsoleCommand GLeakAudioDeviceHandleCommand(
	TEXT("au.stresstest.LeakAnAudioDeviceHandle"),
	TEXT("This will intentionally leak a new handle to an audio device. Obviously, should only be used for testing."),
	FConsoleCommandDelegate::CreateStatic(
		[]()
{
	FAudioDeviceParams Params;
	Params.Scope = EAudioDeviceScope::Shared;
	IntentionallyLeakedHandles.Add(FAudioDeviceManager::Get()->RequestAudioDevice(Params));
})
);

static FAutoConsoleCommand GCleanUpAudioDeviceLeaksCommand(
	TEXT("au.stresstest.CleanUpAudioDeviceLeaks"),
	TEXT("Clean up any audio devices created through a leak command."),
	FConsoleCommandDelegate::CreateStatic(
		[]()
{
	IntentionallyLeakedHandles.Reset();
})
);
#endif

/*-----------------------------------------------------------------------------
FAudioDeviceManager implementation.
-----------------------------------------------------------------------------*/

FAudioDeviceManager::FAudioDeviceManager()
	: AudioDeviceModule(nullptr)
	, DeviceIDCounter(0)
	, NextResourceID(1)
	, SoloDeviceHandle(INDEX_NONE)
	, ActiveAudioDeviceID(INDEX_NONE)
	, bUsingAudioMixer(false)
	, bPlayAllDeviceAudio(false)
	, bOnlyToggleAudioMixerOnce(false)
	, bToggledAudioMixer(false)
{

#if ENABLE_AUDIO_DEBUG
	AudioDebugger = TUniquePtr<FAudioDebugger>(new FAudioDebugger());

	// Check for a command line debug sound argument.
	FString DebugSound;
	if (FParse::Value(FCommandLine::Get(), TEXT("DebugSound="), DebugSound))
	{
		GetDebugger().SetAudioDebugSound(*DebugSound);
	}

#endif //ENABLE_AUDIO_DEBUG
}

FAudioDeviceManager::~FAudioDeviceManager()
{
	MainAudioDeviceHandle.Reset();

	// Notify anyone listening to the device manager that we are about to destroy the audio device.
	for (auto& Device : Devices)
	{
		FAudioDeviceManagerDelegates::OnAudioDeviceDestroyed.Broadcast(Device.Key);
	}

	Devices.Reset();

	// Release any loaded buffers - this calls stop on any sources that need it
	for (int32 Index = Buffers.Num() - 1; Index >= 0; Index--)
	{
		FreeBufferResource(Buffers[Index]);
	}
}

void FAudioDeviceManager::ToggleAudioMixer()
{
	// Only need to toggle if we have 2 device module names loaded at init
	if (AudioDeviceModule && AudioDeviceModuleName.Len() > 0 && AudioMixerModuleName.Len() > 0)
	{
		// Suspend the audio thread
		FAudioThread::SuspendAudioThread();

		// If using audio mixer, we need to toggle back to non-audio mixer
		FString ModuleToUnload;

		// If currently using the audio mixer, we need to toggle to the old audio engine module
		if (bUsingAudioMixer)
		{
			// Unload the previous module
			ModuleToUnload = AudioMixerModuleName;

			AudioDeviceModule = FModuleManager::LoadModulePtr<IAudioDeviceModule>(*AudioDeviceModuleName);

			bUsingAudioMixer = false;
		}
		// If we're currently using old audio engine module, we toggle to the audio mixer module
		else
		{
			// Unload the previous module
			ModuleToUnload = AudioDeviceModuleName;

			// Load the audio mixer engine module
			AudioDeviceModule = FModuleManager::LoadModulePtr<IAudioDeviceModule>(*AudioMixerModuleName);

			bUsingAudioMixer = true;
		}

		// If we succeeded in loading a new module, create a new main audio device.
		if (AudioDeviceModule)
		{
			// Shutdown and create new audio devices
			const UAudioSettings* AudioSettings = GetDefault<UAudioSettings>();
			const int32 QualityLevel = GEngine->GetGameUserSettings()->GetAudioQualityLevel(); // -V595
			const int32 QualityLevelMaxChannels = AudioSettings->GetQualityLevelSettings(QualityLevel).MaxChannels; //-V595

			// We could have multiple audio devices, so loop through them and patch them up best we can to
			// get parity. E.g. we need to pass the handle from the odl to the new, set whether or not its active
			// and try and get the mix-states to be the same.
			for (auto& DeviceContainer : Devices)
			{
				FAudioDevice*& AudioDevice = DeviceContainer.Value.Device;

				check(AudioDevice);

				// Get the audio device handle and whether it is active
				uint32 DeviceID = AudioDevice->DeviceID;
				check(DeviceContainer.Key == DeviceID);
				bool bIsActive = (DeviceID == ActiveAudioDeviceID);

				// To transfer mix states, we need to re-base the absolute clocks on the mix states
				// so the target audio device timing won't result in the mixes suddenly stopping.
				TMap<USoundMix*, FSoundMixState> MixModifiers = AudioDevice->GetSoundMixModifiers();
				TArray<USoundMix*> PrevPassiveSoundMixModifiers = AudioDevice->GetPrevPassiveSoundMixModifiers();
				USoundMix* BaseSoundMix = AudioDevice->GetDefaultBaseSoundMixModifier();
				double AudioClock = AudioDevice->GetAudioClock();

				for (TPair<USoundMix*, FSoundMixState>& SoundMixPair : MixModifiers)
				{
					// Rebase so that a new clock starting from 0.0 won't cause mixes to stop.
					SoundMixPair.Value.StartTime -= AudioClock;
					SoundMixPair.Value.FadeInStartTime -= AudioClock;
					SoundMixPair.Value.FadeInEndTime -= AudioClock;

					if (SoundMixPair.Value.EndTime > 0.0f)
					{
						SoundMixPair.Value.EndTime -= AudioClock;
					}

					if (SoundMixPair.Value.FadeOutStartTime > 0.0f)
					{
						SoundMixPair.Value.FadeOutStartTime -= AudioClock;
					}
				}

				// Tear it down and delete the old audio device. This does a bunch of cleanup.
				AudioDevice->Teardown();
				delete AudioDevice;

				// Make a new audio device using the new audio device module
				AudioDevice = AudioDeviceModule->CreateAudioDevice();

				// Some AudioDeviceModules override CreteAudioMixerPlatformInterface, which means we can create a Audio::FMixerDevice.
				if (AudioDevice == nullptr)
				{
					checkf(AudioDeviceModule->IsAudioMixerModule(), TEXT("Please override AudioDeviceModule->CreateAudioDevice()"))
						AudioDevice = new Audio::FMixerDevice(AudioDeviceModule->CreateAudioMixerPlatformInterface());
				}

				check(AudioDevice);

				// Re-init the new audio device using appropriate settings so it behaves the same
				if (AudioDevice->Init(DeviceID, AudioSettings->GetHighestMaxChannels()))
				{
					AudioDevice->SetMaxChannels(QualityLevelMaxChannels);
				}

				// Transfer the sound mix modifiers to the new audio engine
				AudioDevice->SetSoundMixModifiers(MixModifiers, PrevPassiveSoundMixModifiers, BaseSoundMix);
				// Setup the mute state of the audio device to be the same that it was
				if (bIsActive)
				{
					AudioDevice->SetDeviceMuted(false);
				}
				else
				{
					AudioDevice->SetDeviceMuted(true);
				}

				// Fade in the new audio device (used only in audio mixer to prevent pops on startup/shutdown)
				AudioDevice->FadeIn();
			}

			// We now must free any resources that have been cached with the old audio engine
			// This will result in re-caching of sound waves, but we're forced to do this because FSoundBuffer pointers
			// are cached and each AudioDevice backend has a derived implementation of this so once we
			// switch to a new audio engine the FSoundBuffer pointers are totally invalid.
			for (TObjectIterator<USoundWave> SoundWaveIt; SoundWaveIt; ++SoundWaveIt)
			{
				USoundWave* SoundWave = *SoundWaveIt;
				FreeResource(SoundWave);
			}

			// Unload the previous audio device module
			FModuleManager::Get().UnloadModule(*ModuleToUnload);

			// Resume the audio thread
			FAudioThread::ResumeAudioThread();
		}
	}
}

bool FAudioDeviceManager::IsUsingAudioMixer() const
{
	return bUsingAudioMixer;
}

IAudioDeviceModule* FAudioDeviceManager::GetAudioDeviceModule()
{
	return AudioDeviceModule;
}



FAudioDeviceParams FAudioDeviceManager::GetDefaultParamsForNewWorld()
{
	bool bCreateNewAudioDeviceForPlayInEditor = false;

#if WITH_EDITOR
	// GIsEditor is necessary here to ignore this setting for -game situations.
	if (GIsEditor)
	{
		bCreateNewAudioDeviceForPlayInEditor = GetDefault<ULevelEditorMiscSettings>()->bCreateNewAudioDeviceForPlayInEditor;
	}
#endif

	FAudioDeviceParams Params;
	Params.Scope = bCreateNewAudioDeviceForPlayInEditor ? EAudioDeviceScope::Unique : EAudioDeviceScope::Shared;

	return Params;
}

FAudioDeviceHandle FAudioDeviceManager::RequestAudioDevice(const FAudioDeviceParams& InParams)
{
	FScopeLock ScopeLock(&DeviceMapCriticalSection);
	if (InParams.Scope == EAudioDeviceScope::Unique)
	{
		return CreateNewDevice(InParams);
	}
	else
	{
		// See if we already have a device we can use.
		for (auto& Device : Devices)
		{
			if (CanUseAudioDevice(InParams, Device.Value))
			{
				if (InParams.AssociatedWorld != nullptr)
				{
					
					Device.Value.WorldsUsingThisDevice.AddUnique(InParams.AssociatedWorld);
					FAudioDeviceManagerDelegates::OnWorldRegisteredToAudioDevice.Broadcast(InParams.AssociatedWorld, Device.Key);
				}

				return BuildNewHandle(Device.Value, Device.Key, InParams);
			}
		}

		// If we did not find a suitable device, build one.
		return CreateNewDevice(InParams);
	}
}

bool FAudioDeviceManager::Initialize()
{
	if (LoadDefaultAudioDeviceModule())
	{
		check(AudioDeviceModule);

		const bool bIsAudioMixerEnabled = AudioDeviceModule->IsAudioMixerModule();
		GetMutableDefault<UAudioSettings>()->SetAudioMixerEnabled(bIsAudioMixerEnabled);

#if WITH_EDITOR
		if (bIsAudioMixerEnabled)
		{
			IAudioEditorModule* AudioEditorModule = &FModuleManager::LoadModuleChecked<IAudioEditorModule>("AudioEditor");
			AudioEditorModule->RegisterAudioMixerAssetActions();
			AudioEditorModule->RegisterEffectPresetAssetActions();
		}
#endif

		FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddRaw(this, &FAudioDeviceManager::AppWillEnterBackground);

		// Initialize the main audio device.
		FAudioDeviceParams MainDeviceParams;
		MainDeviceParams.Scope = EAudioDeviceScope::Shared;
		MainDeviceParams.bIsNonRealtime = false;

		MainAudioDeviceHandle = RequestAudioDevice(MainDeviceParams);
		
		if (!MainAudioDeviceHandle)
		{
			UE_LOG(LogAudio, Display, TEXT("Audio device could not be initialized. Please check the value for AudioDeviceModuleName and AudioMixerModuleName in [Platform]Engine.ini."));
			return false;
		}

		FAudioThread::StartAudioThread();

		return true;
	}

	// Failed to initialize
	return false;
}

bool FAudioDeviceManager::LoadDefaultAudioDeviceModule()
{
	check(!AudioDeviceModule);

	// Check if we're going to try to force loading the audio mixer from the command line
	bool bForceAudioMixer = FParse::Param(FCommandLine::Get(), TEXT("AudioMixer"));

	bool bForceNoAudioMixer = FParse::Param(FCommandLine::Get(), TEXT("NoAudioMixer"));

	bool bForceNonRealtimeRenderer = FParse::Param(FCommandLine::Get(), TEXT("DeterministicAudio"));

	// If not using command line switch to use audio mixer, check the game platform engine ini file (e.g. WindowsEngine.ini) which enables it for player
	bUsingAudioMixer = bForceAudioMixer;
	if (!bForceAudioMixer && !bForceNoAudioMixer)
	{
		GConfig->GetBool(TEXT("Audio"), TEXT("UseAudioMixer"), bUsingAudioMixer, GEngineIni);
		// Get the audio mixer and non-audio mixer device module names
		GConfig->GetString(TEXT("Audio"), TEXT("AudioDeviceModuleName"), AudioDeviceModuleName, GEngineIni);
		GConfig->GetString(TEXT("Audio"), TEXT("AudioMixerModuleName"), AudioMixerModuleName, GEngineIni);
	}
	else if (bForceNoAudioMixer)
	{
		GConfig->GetString(TEXT("Audio"), TEXT("AudioDeviceModuleName"), AudioDeviceModuleName, GEngineIni);

		// Allow no audio mixer override from command line
		bUsingAudioMixer = false;
	}
	else if(bForceAudioMixer)
	{
		GConfig->GetString(TEXT("Audio"), TEXT("AudioMixerModuleName"), AudioMixerModuleName, GEngineIni);
	}

	// Check for config bool that restricts audio mixer toggle to only once. This will allow us to patch audio mixer on or off after initial login.
	GConfig->GetBool(TEXT("Audio"), TEXT("OnlyToggleAudioMixerOnce"), bOnlyToggleAudioMixerOnce, GEngineIni);

	if (bForceNonRealtimeRenderer)
	{
		AudioDeviceModule = FModuleManager::LoadModulePtr<IAudioDeviceModule>(TEXT("NonRealtimeAudioRenderer"));

		static IConsoleVariable* IsUsingAudioMixerCvar = IConsoleManager::Get().FindConsoleVariable(TEXT("au.IsUsingAudioMixer"));
		check(IsUsingAudioMixerCvar);
		IsUsingAudioMixerCvar->Set(2, ECVF_SetByConstructor);

		bUsingAudioMixer = true;

		return AudioDeviceModule != nullptr;
	}

	if (bUsingAudioMixer && AudioMixerModuleName.Len() > 0)
	{
		AudioDeviceModule = FModuleManager::LoadModulePtr<IAudioDeviceModule>(*AudioMixerModuleName);
		if (AudioDeviceModule)
		{
			static IConsoleVariable* IsUsingAudioMixerCvar = IConsoleManager::Get().FindConsoleVariable(TEXT("au.IsUsingAudioMixer"));
			check(IsUsingAudioMixerCvar);
			IsUsingAudioMixerCvar->Set(1, ECVF_SetByConstructor);
		}
		else
		{
			bUsingAudioMixer = false;
		}
	}

	if (!AudioDeviceModule && AudioDeviceModuleName.Len() > 0)
	{
		AudioDeviceModule = FModuleManager::LoadModulePtr<IAudioDeviceModule>(*AudioDeviceModuleName);

		static IConsoleVariable* IsUsingAudioMixerCvar = IConsoleManager::Get().FindConsoleVariable(TEXT("au.IsUsingAudioMixer"));
		check(IsUsingAudioMixerCvar);
		IsUsingAudioMixerCvar->Set(0, ECVF_SetByConstructor);
	}

	return AudioDeviceModule != nullptr;
}

FAudioDeviceHandle FAudioDeviceManager::CreateNewDevice(const FAudioDeviceParams& InParams)
{
	Audio::FDeviceId DeviceID = GetNewDeviceID();
	Devices.Emplace(DeviceID, FAudioDeviceContainer(InParams, DeviceID, this));
	FAudioDeviceContainer* ContainerPtr = Devices.Find(DeviceID);
	check(ContainerPtr);
	if (!ContainerPtr->Device)
	{
		UE_LOG(LogAudio, Display, TEXT("Audio device could not be initialized. Please check the value for AudioDeviceModuleName and AudioMixerModuleName in [Platform]Engine.ini."));

		// Initializing the audio device failed. Remove the device container and return an empty handle.
		Devices.Remove(DeviceID);
		return FAudioDeviceHandle();
	}
	else
	{
		FAudioDeviceHandle Handle = BuildNewHandle(*ContainerPtr, DeviceID, InParams);
		FAudioDeviceManagerDelegates::OnAudioDeviceCreated.Broadcast(DeviceID);
		return Handle;
	}
}

bool FAudioDeviceManager::IsValidAudioDevice(Audio::FDeviceId Handle) const
{
	return Devices.Contains(Handle);
}

bool FAudioDeviceManager::ShutdownAudioDevice(Audio::FDeviceId Handle)
{
	// Make sure we have a non-null device ptr in the index slot, then delete it
	Devices.Remove(Handle);
	return true;
}

void FAudioDeviceManager::IncrementDevice(Audio::FDeviceId DeviceID)
{
	FScopeLock ScopeLock(&DeviceMapCriticalSection);

	// If there is an FAudioDeviceHandle out in the world
	check(Devices.Contains(DeviceID));

	FAudioDeviceContainer& Container = Devices[DeviceID];
	Container.NumberOfHandlesToThisDevice++;
}

void FAudioDeviceManager::DecrementDevice(Audio::FDeviceId DeviceID, UWorld* InWorld)
{
	FAudioDevice* DeviceToTearDown = nullptr;

	{
		FScopeLock ScopeLock(&DeviceMapCriticalSection);

		// If there is an FAudioDeviceHandle out in the world
		check(Devices.Contains(DeviceID));

		FAudioDeviceContainer& Container = Devices[DeviceID];
		check(Container.NumberOfHandlesToThisDevice > 0);
		Container.NumberOfHandlesToThisDevice--;

		// If there is no longer anyone using this device, shut it down.
		if (!Container.NumberOfHandlesToThisDevice)
		{
			// If this is the active device and being destroyed, set the main device as the active device.
			if (DeviceID == ActiveAudioDeviceID)
			{
				SetActiveDevice(MainAudioDeviceHandle.GetDeviceID());
			}

			FAudioDeviceManagerDelegates::OnAudioDeviceDestroyed.Broadcast(DeviceID);
			Swap(DeviceToTearDown, Container.Device);
			Devices.Remove(DeviceID);
		}
		else if (InWorld)
		{
			Container.WorldsUsingThisDevice.Remove(InWorld);
		}
	}

	if (DeviceToTearDown)
	{
		DeviceToTearDown->FadeOut();
		DeviceToTearDown->Teardown();
		delete DeviceToTearDown;
	}
}

bool FAudioDeviceManager::ShutdownAllAudioDevices()
{
	FCoreDelegates::ApplicationWillEnterBackgroundDelegate.RemoveAll(this);

	MainAudioDeviceHandle.Reset();

	Devices.Reset();
	return true;
}

FAudioDeviceHandle FAudioDeviceManager::BuildNewHandle(FAudioDeviceContainer&Container, Audio::FDeviceId DeviceID, const FAudioDeviceParams &InParams)
{
	FAudioDeviceManager::Get()->IncrementDevice(DeviceID);
	return FAudioDeviceHandle(Container.Device, DeviceID, InParams.AssociatedWorld);
}

bool FAudioDeviceManager::CanUseAudioDevice(const FAudioDeviceParams& InParams, const FAudioDeviceContainer& InContainer)
{
	return InContainer.Scope == EAudioDeviceScope::Shared
		&& InParams.AudioModule == InContainer.SpecifiedModule
		&& InParams.bIsNonRealtime == InContainer.bIsNonRealtime;
}

#if INSTRUMENT_AUDIODEVICE_HANDLES
uint32 FAudioDeviceManager::CreateUniqueStackWalkID()
{
	static uint32 UniqueStackWalkID = 0;
	return UniqueStackWalkID++;
}
#endif

FAudioDeviceHandle FAudioDeviceManager::GetAudioDevice(Audio::FDeviceId Handle)
{
	FScopeLock ScopeLock(&DeviceMapCriticalSection);
	FAudioDeviceContainer* Container = Devices.Find(Handle);
	if (Container)
	{
		FAudioDeviceParams Params = FAudioDeviceParams();
		return BuildNewHandle(*Container, Handle, Params);
	}
	else
	{
		return FAudioDeviceHandle();
	}
}

FAudioDevice* FAudioDeviceManager::GetAudioDeviceRaw(Audio::FDeviceId Handle)
{
	FScopeLock ScopeLock(&DeviceMapCriticalSection);
	if (!IsValidAudioDevice(Handle))
	{
		return nullptr;
	}

	FAudioDevice* AudioDevice = Devices[Handle].Device;
	check(AudioDevice != nullptr);

	return AudioDevice;
}

FAudioDeviceManager* FAudioDeviceManager::Get()
{
	if (GEngine)
	{
		return GEngine->GetAudioDeviceManager();
	}

	return nullptr;
}

FAudioDeviceHandle FAudioDeviceManager::GetActiveAudioDevice()
{
	if (ActiveAudioDeviceID != INDEX_NONE)
	{
		return GetAudioDevice(ActiveAudioDeviceID);
	}
	return GEngine->GetMainAudioDevice();
}

void FAudioDeviceManager::UpdateActiveAudioDevices(bool bGameTicking)
{
	// Before we kick off the next update make sure that we've finished the previous frame's update (this should be extremely rare)
	if (GCVarEnableAudioThreadWait)
	{
		SyncFence.Wait();
	}


	for (auto& DeviceContainer : Devices)
	{
		DeviceContainer.Value.Device->Update(bGameTicking);
	}

	if (GCVarEnableAudioThreadWait)
	{
		SyncFence.BeginFence();
	}
}

void FAudioDeviceManager::IterateOverAllDevices(TFunction<void(Audio::FDeviceId, FAudioDevice*)> ForEachDevice)
{
	FScopeLock ScopeLock(&DeviceMapCriticalSection);
	for (auto& DeviceContainer : Devices)
	{
		ForEachDevice(DeviceContainer.Key, DeviceContainer.Value.Device);
	}
}

void FAudioDeviceManager::IterateOverAllDevices(TFunction<void(Audio::FDeviceId, const FAudioDevice*)> ForEachDevice) const
{
	// We have to cheat a little to make this safe: we cast our crit section to a mutable pointer in order to scope lock.
	FCriticalSection* ConstCastCritSection = const_cast<FCriticalSection*>(&DeviceMapCriticalSection);
	FScopeLock ScopeLock(ConstCastCritSection);

	for (const auto& DeviceContainer : Devices)
	{
		ForEachDevice(DeviceContainer.Key, DeviceContainer.Value.Device);
	}
}

void FAudioDeviceManager::AddReferencedObjects(FReferenceCollector& Collector)
{
	FScopeLock ScopeLock(&DeviceMapCriticalSection);
	for (auto& DeviceContainer : Devices)
	{
		check(DeviceContainer.Value.Device);
		DeviceContainer.Value.Device->AddReferencedObjects(Collector);
	}
}

void FAudioDeviceManager::StopSoundsUsingResource(USoundWave* InSoundWave, TArray<UAudioComponent*>* StoppedComponents)
{
	FScopeLock ScopeLock(&DeviceMapCriticalSection);
	for (auto& DeviceContainer : Devices)
	{
		DeviceContainer.Value.Device->StopSoundsUsingResource(InSoundWave, StoppedComponents);
	}
}

void FAudioDeviceManager::RegisterSoundClass(USoundClass* SoundClass)
{
	FScopeLock ScopeLock(&DeviceMapCriticalSection);
	for (auto& DeviceContainer : Devices)
	{
		DeviceContainer.Value.Device->RegisterSoundClass(SoundClass);
	}
}

void FAudioDeviceManager::UnregisterSoundClass(USoundClass* SoundClass)
{
	FScopeLock ScopeLock(&DeviceMapCriticalSection);
	for (auto& DeviceContainer : Devices)
	{
		DeviceContainer.Value.Device->UnregisterSoundClass(SoundClass);
	}
}

void FAudioDeviceManager::InitSoundClasses()
{
	FScopeLock ScopeLock(&DeviceMapCriticalSection);
	for (auto& DeviceContainer : Devices)
	{
		DeviceContainer.Value.Device->InitSoundClasses();
	}
}

void FAudioDeviceManager::RegisterSoundSubmix(const USoundSubmixBase* SoundSubmix)
{
	FScopeLock ScopeLock(&DeviceMapCriticalSection);
	for (auto& DeviceContainer : Devices)
	{
		DeviceContainer.Value.Device->RegisterSoundSubmix(SoundSubmix, true);
	}
}

void FAudioDeviceManager::UnregisterSoundSubmix(const USoundSubmixBase* SoundSubmix)
{
	FScopeLock ScopeLock(&DeviceMapCriticalSection);
	for (auto& DeviceContainer : Devices)
	{
		DeviceContainer.Value.Device->UnregisterSoundSubmix(SoundSubmix);
	}
}

void FAudioDeviceManager::InitSoundSubmixes()
{
	FScopeLock ScopeLock(&DeviceMapCriticalSection);
	for (auto& DeviceContainer : Devices)
	{
		DeviceContainer.Value.Device->InitSoundSubmixes();
	}
}

void FAudioDeviceManager::InitSoundEffectPresets()
{
	FScopeLock ScopeLock(&DeviceMapCriticalSection);
	for (auto& DeviceContainer : Devices)
	{
		DeviceContainer.Value.Device->InitSoundEffectPresets();
	}
}

void FAudioDeviceManager::UpdateSourceEffectChain(const uint32 SourceEffectChainId, const TArray<FSourceEffectChainEntry>& SourceEffectChain, const bool bPlayEffectChainTails)
{
	FScopeLock ScopeLock(&DeviceMapCriticalSection);
	for (auto& DeviceContainer : Devices)
	{
		DeviceContainer.Value.Device->UpdateSourceEffectChain(SourceEffectChainId, SourceEffectChain, bPlayEffectChainTails);
	}
}

void FAudioDeviceManager::UpdateSubmix(USoundSubmixBase* SoundSubmix)
{
	FScopeLock ScopeLock(&DeviceMapCriticalSection);
	for (auto& DeviceContainer : Devices)
	{
		DeviceContainer.Value.Device->UpdateSubmixProperties(SoundSubmix);
	}
}

void FAudioDeviceManager::SetActiveDevice(uint32 InAudioDeviceHandle)
{
	// Only change the active device if there are no solo'd audio devices
	if (SoloDeviceHandle == INDEX_NONE)
	{
		FScopeLock ScopeLock(&DeviceMapCriticalSection);
		// Iterate over all of our devices and mute every device except for InAudioDeviceHandle:
		for (auto& DeviceContainer : Devices)
		{
			check(DeviceContainer.Value.Device);
			FAudioDevice* AudioDevice = DeviceContainer.Value.Device;

			if (DeviceContainer.Key == InAudioDeviceHandle)
			{
				ActiveAudioDeviceID = InAudioDeviceHandle;
				AudioDevice->SetDeviceMuted(false);
			}
			else
			{
				AudioDevice->SetDeviceMuted(true);
			}
		}
	}
}

void FAudioDeviceManager::SetSoloDevice(Audio::FDeviceId InAudioDeviceHandle)
{
	SoloDeviceHandle = InAudioDeviceHandle;
	if (SoloDeviceHandle != INDEX_NONE)
	{
		FScopeLock ScopeLock(&DeviceMapCriticalSection);
		for (auto& DeviceContainer : Devices)
		{
			check(DeviceContainer.Value.Device);
			check(DeviceContainer.Key == DeviceContainer.Value.Device->DeviceID);
			FAudioDevice*& AudioDevice = DeviceContainer.Value.Device;

			// Un-mute the active audio device and mute non-active device, as long as its not the main audio device (which is used to play UI sounds)
			if (AudioDevice->DeviceID == InAudioDeviceHandle)
			{
				ActiveAudioDeviceID = InAudioDeviceHandle;
				AudioDevice->SetDeviceMuted(false);
			}
			else
			{
				AudioDevice->SetDeviceMuted(true);
			}
		}
	}
}


uint8 FAudioDeviceManager::GetNumActiveAudioDevices() const
{
	return Devices.Num();
}

uint8 FAudioDeviceManager::GetNumMainAudioDeviceWorlds() const
{
	const Audio::FDeviceId MainDeviceID = MainAudioDeviceHandle.GetDeviceID();
	if (Devices.Contains(MainDeviceID))
	{
		return Devices[MainDeviceID].WorldsUsingThisDevice.Num();
	}
	else
	{
		return 0;
	}
}

TArray<FAudioDevice*> FAudioDeviceManager::GetAudioDevices()
{
	TArray<FAudioDevice*> DeviceList;
	FScopeLock ScopeLock(&DeviceMapCriticalSection);
	for (auto& Device : Devices)
	{
		DeviceList.Add(Device.Value.Device);
	}

	return DeviceList;
}

TArray<UWorld*> FAudioDeviceManager::GetWorldsUsingAudioDevice(const Audio::FDeviceId& InID)
{
	FScopeLock ScopeLock(&DeviceMapCriticalSection);
	if (Devices.Contains(InID))
	{
		return Devices[InID].WorldsUsingThisDevice;
	}
	else
	{
		return TArray<UWorld*>();
	}
}

#if INSTRUMENT_AUDIODEVICE_HANDLES
void FAudioDeviceManager::AddStackWalkForContainer(Audio::FDeviceId InId, uint32 StackWalkID, FString&& InStackWalk)
{
	check(Devices.Contains(InId));
	check(!Devices[InId].HandleCreationStackWalks.Contains(StackWalkID));
	FAudioDeviceContainer& Container = Devices[InId];
	Container.HandleCreationStackWalks.Add(StackWalkID, MoveTemp(InStackWalk));
}

void FAudioDeviceManager::RemoveStackWalkForContainer(Audio::FDeviceId InId, uint32 StackWalkID)
{
	check(Devices.Contains(InId));
	check(Devices[InId].HandleCreationStackWalks.Contains(StackWalkID));
	FAudioDeviceContainer& Container = Devices[InId];
	Container.HandleCreationStackWalks.Remove(StackWalkID);
}
#endif

void FAudioDeviceManager::LogListOfAudioDevices()
{
	FString ListOfDevices;

	for (auto& DeviceContainer : Devices)
	{
		FString DeviceInfo = FString::Printf(TEXT(R"(
					Device %d:
					Scope: %s 
					Realtime: %s
					Number Of Owners: %d 
		)"),
			DeviceContainer.Key,
			DeviceContainer.Value.Scope == EAudioDeviceScope::Unique ? TEXT("Unique") : TEXT("Shared"),
			DeviceContainer.Value.bIsNonRealtime ? TEXT("No") : TEXT("Yes"),
			DeviceContainer.Value.NumberOfHandlesToThisDevice);

#if INSTRUMENT_AUDIODEVICE_HANDLES
		for (auto& StackWalkString : DeviceContainer.Value.HandleCreationStackWalks)
		{
			DeviceInfo += TEXT("Handle Created here still alive:\n");
			DeviceInfo += StackWalkString.Value;
			DeviceInfo += TEXT("\n\n");
		}
#endif

		ListOfDevices += DeviceInfo;
	}

	UE_LOG(LogAudio, Display, TEXT("List of devices: \n%s"), *ListOfDevices);
}

uint32 FAudioDeviceManager::GetNewDeviceID()
{
	return ++DeviceIDCounter;
}

void FAudioDeviceManager::StopSourcesUsingBuffer(FSoundBuffer* SoundBuffer)
{
	IterateOverAllDevices([SoundBuffer](Audio::FDeviceId Id, FAudioDevice* Device)
	{
		Device->StopSourcesUsingBuffer(SoundBuffer);
	});
}

void FAudioDeviceManager::TrackResource(USoundWave* SoundWave, FSoundBuffer* Buffer)
{
	// Allocate new resource ID and assign to USoundWave. A value of 0 (default) means not yet registered.
	int32 ResourceID = NextResourceID++;
	Buffer->ResourceID = ResourceID;
	SoundWave->ResourceID = ResourceID;

	Buffers.Add(Buffer);
	WaveBufferMap.Add(ResourceID, Buffer);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// Keep track of associated resource name.
	Buffer->ResourceName = SoundWave->GetPathName();
#endif
}

void FAudioDeviceManager::FreeResource(USoundWave* SoundWave)
{
	if (SoundWave->ResourceID)
	{
		FSoundBuffer* SoundBuffer = WaveBufferMap.FindRef(SoundWave->ResourceID);
		FreeBufferResource(SoundBuffer);

		// Flag that the sound wave needs to do a full decompress again
		SoundWave->DecompressionType = DTYPE_Setup;
		SoundWave->SetPrecacheState(ESoundWavePrecacheState::NotStarted);

		SoundWave->ResourceID = 0;
	}
}

void FAudioDeviceManager::FreeBufferResource(FSoundBuffer* SoundBuffer)
{
	if (SoundBuffer)
	{
		// Make sure any realtime tasks are finished that are using this buffer
		SoundBuffer->EnsureRealtimeTaskCompletion();

		Buffers.Remove(SoundBuffer);

		// Stop any sound sources on any audio device currently using this buffer before deleting
		StopSourcesUsingBuffer(SoundBuffer);

		delete SoundBuffer;
		SoundBuffer = nullptr;
	}
}

FSoundBuffer* FAudioDeviceManager::GetSoundBufferForResourceID(uint32 ResourceID)
{
	return WaveBufferMap.FindRef(ResourceID);
}

void FAudioDeviceManager::RemoveSoundBufferForResourceID(uint32 ResourceID)
{
	WaveBufferMap.Remove(ResourceID);
}

void FAudioDeviceManager::RemoveSoundMix(USoundMix* SoundMix)
{
	if (!IsInAudioThread())
	{
		DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.RemoveSoundMix"), STAT_AudioRemoveSoundMix, STATGROUP_AudioThreadCommands);

		FAudioDeviceManager* AudioDeviceManager = this;
		FAudioThread::RunCommandOnAudioThread([AudioDeviceManager, SoundMix]()
		{
			AudioDeviceManager->RemoveSoundMix(SoundMix);

		}, GET_STATID(STAT_AudioRemoveSoundMix));

		return;
	}

	IterateOverAllDevices([SoundMix](Audio::FDeviceId Id, FAudioDevice* Device)
	{
		Device->RemoveSoundMix(SoundMix);
	});
}

void FAudioDeviceManager::TogglePlayAllDeviceAudio()
{
	if (!IsInAudioThread())
	{
		DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.TogglePlayAllDeviceAudio"), STAT_TogglePlayAllDeviceAudio, STATGROUP_AudioThreadCommands);

		FAudioDeviceManager* AudioDeviceManager = this;
		FAudioThread::RunCommandOnAudioThread([AudioDeviceManager]()
		{
			AudioDeviceManager->TogglePlayAllDeviceAudio();

		}, GET_STATID(STAT_TogglePlayAllDeviceAudio));

		return;
	}

	bPlayAllDeviceAudio = !bPlayAllDeviceAudio;
}

bool FAudioDeviceManager::IsVisualizeDebug3dEnabled() const
{
#if ENABLE_AUDIO_DEBUG
	return GetDebugger().IsVisualizeDebug3dEnabled() || CVarIsVisualizeEnabled;
#else // ENABLE_AUDIO_DEBUG
	return false;
#endif // !ENABLE_AUDIO_DEBUG
}

void FAudioDeviceManager::ToggleVisualize3dDebug()
{
#if ENABLE_AUDIO_DEBUG
	if (!IsInAudioThread())
	{
		DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.ToggleVisualize3dDebug"), STAT_ToggleVisualize3dDebug, STATGROUP_AudioThreadCommands);

		FAudioDeviceManager* AudioDeviceManager = this;
		FAudioThread::RunCommandOnAudioThread([AudioDeviceManager]()
		{
			AudioDeviceManager->ToggleVisualize3dDebug();

		}, GET_STATID(STAT_ToggleVisualize3dDebug));

		return;
	}

	GetDebugger().ToggleVisualizeDebug3dEnabled();
#endif // ENABLE_AUDIO_DEBUG
}

float FAudioDeviceManager::GetDynamicSoundVolume(ESoundType SoundType, const FName& SoundName) const
{
	check(IsInAudioThread());

	TTuple<ESoundType, FName> SoundKey(SoundType, SoundName);
	if (const float* Volume = DynamicSoundVolumes.Find(SoundKey))
	{
		return FMath::Max(0.0f, *Volume);
	}

	return 1.0f;
}

void FAudioDeviceManager::ResetAllDynamicSoundVolumes()
{
	if (!IsInAudioThread())
	{
		DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.ResetAllDynamicSoundVolumes"), STAT_ResetAllDynamicSoundVolumes, STATGROUP_AudioThreadCommands);

		FAudioDeviceManager* AudioDeviceManager = this;
		FAudioThread::RunCommandOnAudioThread([AudioDeviceManager]()
		{
			AudioDeviceManager->ResetAllDynamicSoundVolumes();

		}, GET_STATID(STAT_ResetAllDynamicSoundVolumes));
		return;
	}

	DynamicSoundVolumes.Reset();
	DynamicSoundVolumes.Shrink();
}

void FAudioDeviceManager::ResetDynamicSoundVolume(ESoundType SoundType, const FName& SoundName)
{
	if (!IsInAudioThread())
	{
		DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.ResetSoundCueTrimVolume"), STAT_ResetSoundCueTrimVolume, STATGROUP_AudioThreadCommands);

		FAudioDeviceManager* AudioDeviceManager = this;
		FAudioThread::RunCommandOnAudioThread([AudioDeviceManager, SoundType, SoundName]()
		{
			AudioDeviceManager->ResetDynamicSoundVolume(SoundType, SoundName);

		}, GET_STATID(STAT_ResetSoundCueTrimVolume));
		return;
	}

	TTuple<ESoundType, FName> Key(SoundType, SoundName);
	DynamicSoundVolumes.Remove(Key);
}

void FAudioDeviceManager::SetDynamicSoundVolume(ESoundType SoundType, const FName& SoundName, float Volume)
{
	if (!IsInAudioThread())
	{
		DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.SetDynamicSoundVolume"), STAT_SetDynamicSoundVolume, STATGROUP_AudioThreadCommands);

		FAudioDeviceManager* AudioDeviceManager = this;
		FAudioThread::RunCommandOnAudioThread([AudioDeviceManager, SoundType, SoundName, Volume]()
		{
			AudioDeviceManager->SetDynamicSoundVolume(SoundType, SoundName, Volume);

		}, GET_STATID(STAT_SetDynamicSoundVolume));
		return;
	}

	FMath::Clamp(Volume, 0.0f, MAX_VOLUME);
	TTuple<ESoundType, FName> Key(SoundType, SoundName);
	DynamicSoundVolumes.FindOrAdd(Key) = Volume;
}

#if ENABLE_AUDIO_DEBUG
FAudioDebugger& FAudioDeviceManager::GetDebugger()
{
	check(AudioDebugger.IsValid());

	return *AudioDebugger;
}

const FAudioDebugger& FAudioDeviceManager::GetDebugger() const
{
	check(AudioDebugger.IsValid());

	return *AudioDebugger;
}

#endif // ENABLE_AUDIO_DEBUG


void FAudioDeviceManager::AppWillEnterBackground()
{
	// Flush all commands to the audio thread and the audio render thread:
	if (GCVarFlushAudioRenderCommandsOnSuspend)
	{
		if (GEngine && GEngine->GetMainAudioDevice())
		{
			FAudioDeviceHandle AudioDevice = GEngine->GetMainAudioDevice();

			FAudioThread::RunCommandOnAudioThread([AudioDevice]()
			{
				FAudioDevice* AudioDevicePtr = const_cast<FAudioDevice*>(AudioDevice.GetAudioDevice());
				AudioDevicePtr->FlushAudioRenderingCommands(true);
			}, TStatId());
		}

		FAudioCommandFence AudioCommandFence;
		AudioCommandFence.BeginFence();
		AudioCommandFence.Wait();
	}
}

FAudioDeviceHandle::FAudioDeviceHandle()
	: World(nullptr)
	, Device(nullptr)
	, DeviceId(INDEX_NONE)
{
#if INSTRUMENT_AUDIODEVICE_HANDLES
	StackWalkID = INDEX_NONE;
#endif
}

FAudioDeviceHandle::FAudioDeviceHandle(FAudioDevice* InDevice, Audio::FDeviceId InID, UWorld* InWorld)
	: World(InWorld)
	, Device(InDevice)
	, DeviceId(InID)
{
#if INSTRUMENT_AUDIODEVICE_HANDLES
	AddStackDumpToAudioDeviceContainer();
#endif
}

FAudioDeviceHandle::FAudioDeviceHandle(const FAudioDeviceHandle& Other)
	: FAudioDeviceHandle()
{
	*this = Other;
}

FAudioDeviceHandle::FAudioDeviceHandle(FAudioDeviceHandle&& Other)
	: FAudioDeviceHandle()
{
	*this = MoveTemp(Other);
}

#if INSTRUMENT_AUDIODEVICE_HANDLES
void FAudioDeviceHandle::AddStackDumpToAudioDeviceContainer()
{
	static const int32 MaxPlatformWalkStringCount = 1024 * 4;

	ANSICHAR PlatformDump[MaxPlatformWalkStringCount];
	FMemory::Memzero(PlatformDump, MaxPlatformWalkStringCount * sizeof(ANSICHAR));

	FPlatformStackWalk::StackWalkAndDump(PlatformDump, MaxPlatformWalkStringCount - 1, 2);
	
	FString FormattedDump = TEXT("New Handle Created:\n");

	int32 DumpLength = FCStringAnsi::Strlen(PlatformDump);

	// If this hits, increase the max character length.
	ensure(DumpLength < MaxPlatformWalkStringCount - 1);

	FormattedDump.AppendChars(ANSI_TO_TCHAR(PlatformDump), DumpLength);
	FormattedDump += TEXT("\n");
	StackWalkID = FAudioDeviceManager::Get()->CreateUniqueStackWalkID();
	FAudioDeviceManager::Get()->AddStackWalkForContainer(DeviceId, StackWalkID, MoveTemp(FormattedDump));
}
#endif

FAudioDeviceHandle::~FAudioDeviceHandle()
{
	if (IsValid())
	{
		FAudioDeviceManager* AudioDeviceManager = GEngine->GetAudioDeviceManager();
		if (AudioDeviceManager)
		{
			AudioDeviceManager->DecrementDevice(DeviceId, World);

#if INSTRUMENT_AUDIODEVICE_HANDLES
			check(StackWalkID != INDEX_NONE);
			AudioDeviceManager->RemoveStackWalkForContainer(DeviceId, StackWalkID);
#endif
		}
	}
}

FAudioDevice* FAudioDeviceHandle::GetAudioDevice() const
{
	return Device;
}

Audio::FDeviceId FAudioDeviceHandle::GetDeviceID() const
{
	return DeviceId;
}

bool FAudioDeviceHandle::IsValid() const
{
	return GEngine && GEngine->GetAudioDeviceManager() && Device != nullptr;
}

void FAudioDeviceHandle::Reset()
{
	*this = FAudioDeviceHandle();
}

FAudioDeviceHandle& FAudioDeviceHandle::operator=(const FAudioDeviceHandle& Other)
{
	if (IsValid())
	{
		check(FAudioDeviceManager::Get());
		FAudioDeviceManager::Get()->DecrementDevice(DeviceId, World);
	}

	Device = Other.Device;
	DeviceId = Other.DeviceId;

	if (IsValid())
	{
		FAudioDeviceManager* AudioDeviceManager = FAudioDeviceManager::Get();
		if (AudioDeviceManager)
		{
			AudioDeviceManager->IncrementDevice(DeviceId);

#if INSTRUMENT_AUDIODEVICE_HANDLES
			AddStackDumpToAudioDeviceContainer();
#endif
		}
	}

	return *this;
}

FAudioDeviceHandle& FAudioDeviceHandle::operator=(FAudioDeviceHandle&& Other)
{
	if (FAudioDeviceManager::Get() && IsValid())
	{
#if INSTRUMENT_AUDIODEVICE_HANDLES
		check(StackWalkID != INDEX_NONE);
		GEngine->GetAudioDeviceManager()->RemoveStackWalkForContainer(DeviceId, StackWalkID);
#endif

		FAudioDeviceManager::Get()->DecrementDevice(DeviceId, World);
	}

	Device = Other.Device;
	DeviceId = Other.DeviceId;

	Other.Device = nullptr;
	Other.DeviceId = INDEX_NONE;

#if INSTRUMENT_AUDIODEVICE_HANDLES
	if (IsValid())
	{

		AddStackDumpToAudioDeviceContainer();
	}
#endif

	return *this;
}

FAudioDeviceManager::FAudioDeviceContainer::FAudioDeviceContainer(const FAudioDeviceParams& InParams, Audio::FDeviceId InDeviceID, FAudioDeviceManager* DeviceManager)
	: NumberOfHandlesToThisDevice(0)
	, Scope(InParams.Scope)
	, bIsNonRealtime(InParams.bIsNonRealtime)
	, SpecifiedModule(InParams.AudioModule)
{
	// Here we create an entirely new audio device.
	if (bIsNonRealtime)
	{
		IAudioDeviceModule* NonRealtimeModule = FModuleManager::LoadModulePtr<IAudioDeviceModule>(TEXT("NonRealtimeAudioRenderer"));
		check(NonRealtimeModule);
		Device = NonRealtimeModule->CreateAudioDevice();
	}
	else if (SpecifiedModule != nullptr)
	{
		Device = SpecifiedModule->CreateAudioDevice();
	}
	else
	{
		check(DeviceManager->AudioDeviceModule);
		Device = DeviceManager->AudioDeviceModule->CreateAudioDevice();

		if (!Device)
		{
			Device = new Audio::FMixerDevice(DeviceManager->AudioDeviceModule->CreateAudioMixerPlatformInterface());
		}
	}

	check(Device);

	// Set to highest max channels initially provided by any quality setting, so that
	// setting to lower quality but potentially returning to higher quality later at
	// runtime is supported.
	const UAudioSettings* AudioSettings = GetDefault<UAudioSettings>();
	const int32 HighestMaxChannels = AudioSettings ? AudioSettings->GetHighestMaxChannels() : 0;
	if (Device->Init(InDeviceID, HighestMaxChannels))
	{
		const FAudioQualitySettings& QualitySettings = Device->GetQualityLevelSettings();
		Device->SetMaxChannels(QualitySettings.MaxChannels);
		Device->FadeIn();
	}
	else
	{
		UE_LOG(LogAudio, Warning, TEXT("FAudioDevice::Init Failed!"));
		Device->Teardown();
		delete Device;
		Device = nullptr;
	}
}

FAudioDeviceManager::FAudioDeviceContainer::FAudioDeviceContainer()
{
	checkNoEntry();
}

FAudioDeviceManager::FAudioDeviceContainer::FAudioDeviceContainer(FAudioDeviceContainer&& Other)
{
	Device = Other.Device;
	Other.Device = nullptr;

	NumberOfHandlesToThisDevice = Other.NumberOfHandlesToThisDevice;
	Other.NumberOfHandlesToThisDevice = 0;

	WorldsUsingThisDevice = MoveTemp(Other.WorldsUsingThisDevice);

	Scope = Other.Scope;
	Other.Scope = EAudioDeviceScope::Default;

	bIsNonRealtime = Other.bIsNonRealtime;
	Other.bIsNonRealtime = false;

	SpecifiedModule = Other.SpecifiedModule;
	Other.SpecifiedModule = nullptr;

#if INSTRUMENT_AUDIODEVICE_HANDLES
	HandleCreationStackWalks = MoveTemp(Other.HandleCreationStackWalks);
#endif
}

FAudioDeviceManager::FAudioDeviceContainer::~FAudioDeviceContainer()
{
	// Shutdown the audio device.
	if (NumberOfHandlesToThisDevice != 0)
	{
		UE_LOG(LogAudio, Display, TEXT("Shutting down audio device while %d references to it are still alive. For more information, compile with INSTRUMENT_AUDIODEVICE_HANDLES."), NumberOfHandlesToThisDevice);

#if INSTRUMENT_AUDIODEVICE_HANDLES
		FString ActiveDeviceHandles;
		for (auto& StackWalkString : HandleCreationStackWalks)
		{
			ActiveDeviceHandles += StackWalkString.Value;
			ActiveDeviceHandles += TEXT("\n\n");
		}

		UE_LOG(LogAudio, Warning, TEXT("List Of Active Handles: \n%s"), *ActiveDeviceHandles);
#endif
	}

	if (Device)
	{
		Device->FadeOut();
		Device->Teardown();
		delete Device;
		Device = nullptr;
	}
}

FAudioDeviceManagerDelegates::FOnAudioDeviceCreated FAudioDeviceManagerDelegates::OnAudioDeviceCreated;
FAudioDeviceManagerDelegates::FOnAudioDeviceDestroyed FAudioDeviceManagerDelegates::OnAudioDeviceDestroyed;
FAudioDeviceManagerDelegates::FOnWorldRegisteredToAudioDevice FAudioDeviceManagerDelegates::OnWorldRegisteredToAudioDevice;
