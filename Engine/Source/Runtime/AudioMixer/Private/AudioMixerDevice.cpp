// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "AudioMixerDevice.h"
#include "AudioMixerSource.h"
#include "AudioMixerSubmix.h"
#include "AudioMixerSourceVoice.h"
#include "AudioPluginUtilities.h"
#include "UObject/UObjectHash.h"
#include "AudioMixerEffectsManager.h"
#include "SubmixEffects/AudioMixerSubmixEffectReverb.h"
#include "SubmixEffects/AudioMixerSubmixEffectEQ.h"
#include "SubmixEffects/AudioMixerSubmixEffectDynamicsProcessor.h"
#include "DSP/Noise.h"
#include "DSP/SinOsc.h"
#include "UObject/UObjectIterator.h"
#include "Runtime/HeadMountedDisplay/Public/IHeadMountedDisplayModule.h"
#include "Misc/App.h"
#include "Sound/AudioSettings.h"

#if WITH_EDITOR
#include "AudioEditorModule.h"
#endif

static int32 DisableSubmixReverbCVar = 0;
FAutoConsoleVariableRef CVarDisableSubmixReverb(
	TEXT("au.DisableReverbSubmix"),
	DisableSubmixReverbCVar,
	TEXT("Disables the reverb submix.\n")
	TEXT("0: Not Disabled, 1: Disabled"),
	ECVF_Default);

static int32 DisableSubmixEffectEQCvar = 0;
FAutoConsoleVariableRef CVarDisableSubmixEQ(
	TEXT("au.DisableSubmixEffectEQ"),
	DisableSubmixEffectEQCvar,
	TEXT("Disables the eq submix.\n")
	TEXT("0: Not Disabled, 1: Disabled"),
	ECVF_Default);


namespace Audio
{
	TArray<USoundSubmix*> FMixerDevice::MasterSubmixes;

	FMixerDevice::FMixerDevice(IAudioMixerPlatformInterface* InAudioMixerPlatform)
		: AudioMixerPlatform(InAudioMixerPlatform)
		, AudioClockDelta(0.0)
		, AudioClock(0.0)
		, SourceManager(this)
		, GameOrAudioThreadId(INDEX_NONE)
		, AudioPlatformThreadId(INDEX_NONE)
		, bDebugOutputEnabled(false)
	{
		// This audio device is the audio mixer
		bAudioMixerModuleLoaded = true;
	}

	FMixerDevice::~FMixerDevice()
	{
		AUDIO_MIXER_CHECK_GAME_THREAD(this);

		if (AudioMixerPlatform != nullptr)
		{
			delete AudioMixerPlatform;
		}
	}

	void FMixerDevice::CheckAudioThread()
	{
#if AUDIO_MIXER_ENABLE_DEBUG_MODE
		// "Audio Thread" is the game/audio thread ID used above audio rendering thread.
		AUDIO_MIXER_CHECK(IsInAudioThread());
#endif
	}

	void FMixerDevice::ResetAudioRenderingThreadId()
	{
#if AUDIO_MIXER_ENABLE_DEBUG_MODE
		AudioPlatformThreadId = INDEX_NONE;
		CheckAudioRenderingThread();
#endif
	}

	void FMixerDevice::CheckAudioRenderingThread()
	{
#if AUDIO_MIXER_ENABLE_DEBUG_MODE
		if (AudioPlatformThreadId == INDEX_NONE)
		{
			AudioPlatformThreadId = FPlatformTLS::GetCurrentThreadId();
		}
		int32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
		AUDIO_MIXER_CHECK(CurrentThreadId == AudioPlatformThreadId);
#endif
	}

	bool FMixerDevice::IsAudioRenderingThread()
	{
		int32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
		return CurrentThreadId == AudioPlatformThreadId;
	}

	void FMixerDevice::GetAudioDeviceList(TArray<FString>& OutAudioDeviceNames) const
	{
		if (AudioMixerPlatform && AudioMixerPlatform->IsInitialized())
		{
			uint32 NumOutputDevices;
			if (AudioMixerPlatform->GetNumOutputDevices(NumOutputDevices))
			{
				for (uint32 i = 0; i < NumOutputDevices; ++i)
				{
					FAudioPlatformDeviceInfo DeviceInfo;
					if (AudioMixerPlatform->GetOutputDeviceInfo(i, DeviceInfo))
					{
						OutAudioDeviceNames.Add(DeviceInfo.Name);
					}
				}
			}
		}
	}

	bool FMixerDevice::InitializeHardware()
	{
		AUDIO_MIXER_CHECK_GAME_THREAD(this);
	
		// Log that we're inside the audio mixer
		UE_LOG(LogAudioMixer, Display, TEXT("Initializing audio mixer."));

		if (AudioMixerPlatform && AudioMixerPlatform->InitializeHardware())
		{
			const UAudioSettings* AudioSettings = GetDefault<UAudioSettings>();
			MonoChannelUpmixMethod = AudioSettings->MonoChannelUpmixMethod;
			PanningMethod = AudioSettings->PanningMethod;

			// Set whether we're the main audio mixer
			bIsMainAudioMixer = IsMainAudioDevice();

			AUDIO_MIXER_CHECK(SampleRate != 0.0f);

			AudioMixerPlatform->RegisterDeviceChangedListener();

			// Allow platforms to override the platform settings callback buffer frame size (i.e. restrict to particular values, etc)
			PlatformSettings.CallbackBufferFrameSize = AudioMixerPlatform->GetNumFrames(PlatformSettings.CallbackBufferFrameSize);

			OpenStreamParams.NumBuffers = PlatformSettings.NumBuffers;
			OpenStreamParams.NumFrames = PlatformSettings.CallbackBufferFrameSize;
			OpenStreamParams.OutputDeviceIndex = AUDIO_MIXER_DEFAULT_DEVICE_INDEX; // TODO: Support overriding which audio device user wants to open, not necessarily default.
			OpenStreamParams.SampleRate = SampleRate;
			OpenStreamParams.AudioMixer = this;
			OpenStreamParams.MaxChannels = GetMaxChannels();

			FString DefaultDeviceName = AudioMixerPlatform->GetDefaultDeviceName();

			// Allow HMD to specify audio device, if one was not specified in settings
			if (DefaultDeviceName.IsEmpty() && FAudioDevice::CanUseVRAudioDevice() && IHeadMountedDisplayModule::IsAvailable())
			{
				DefaultDeviceName = IHeadMountedDisplayModule::Get().GetAudioOutputDevice();
			}

			if (!DefaultDeviceName.IsEmpty())
			{
				uint32 NumOutputDevices = 0;
				AudioMixerPlatform->GetNumOutputDevices(NumOutputDevices);

				for (uint32 i = 0; i < NumOutputDevices; ++i)
				{
					FAudioPlatformDeviceInfo DeviceInfo;
					AudioMixerPlatform->GetOutputDeviceInfo(i, DeviceInfo);

					if (DeviceInfo.Name == DefaultDeviceName || DeviceInfo.DeviceId == DefaultDeviceName)
					{
						OpenStreamParams.OutputDeviceIndex = i;

						// If we're intentionally selecting an audio device (and not just using the default device) then 
						// lets try to restore audio to that device if it's removed and then later is restored
						OpenStreamParams.bRestoreIfRemoved = true;
						break;
					}
				}
			}

			if (AudioMixerPlatform->OpenAudioStream(OpenStreamParams))
			{
				// Get the platform device info we're using
				PlatformInfo = AudioMixerPlatform->GetPlatformDeviceInfo();

				// Initialize some data that depends on speaker configuration, etc.
				InitializeChannelAzimuthMap(PlatformInfo.NumChannels);

				// We initialize the number of sources to be 2 times the max channels
				// This extra source count is used for "stopping sources", which are sources
				// which are fading out (very quickly) to avoid discontinuities when stopping sounds
				FSourceManagerInitParams SourceManagerInitParams;
				SourceManagerInitParams.NumSources = GetMaxChannels() + NumStoppingVoices;
				SourceManagerInitParams.NumSourceWorkers = 4;

				SourceManager.Init(SourceManagerInitParams);

				AudioClock = 0.0;
				AudioClockDelta = (double)OpenStreamParams.NumFrames / OpenStreamParams.SampleRate;

				FAudioPluginInitializationParams PluginInitializationParams;
				PluginInitializationParams.NumSources = MaxChannels;
				PluginInitializationParams.SampleRate = SampleRate;
				PluginInitializationParams.BufferLength = OpenStreamParams.NumFrames;
				PluginInitializationParams.AudioDevicePtr = this;

				// Initialize any plugins if they exist
				if (SpatializationPluginInterface.IsValid())
				{
					SpatializationPluginInterface->Initialize(PluginInitializationParams);
				}

				// Create a new ambisonics mixer.
				IAudioSpatializationFactory* SpatializationPluginFactory = AudioPluginUtilities::GetDesiredSpatializationPlugin(AudioPluginUtilities::CurrentPlatform);
				if (SpatializationPluginFactory != nullptr)
				{
					AmbisonicsMixer = SpatializationPluginFactory->CreateNewAmbisonicsMixer(this);
					if (AmbisonicsMixer.IsValid())
					{
						AmbisonicsMixer->Initialize(PluginInitializationParams);
					}
				}

				if (OcclusionInterface.IsValid())
				{
					OcclusionInterface->Initialize(PluginInitializationParams);
				}

				if (ReverbPluginInterface.IsValid())
				{
					ReverbPluginInterface->Initialize(PluginInitializationParams);
				}

				// Need to set these up before we start the audio stream.
				InitSoundSubmixes();

				AudioMixerPlatform->PostInitializeHardware();

				// Initialize the data used for audio thread sub-frame timing.
				AudioThreadTimingData.StartTime = FPlatformTime::Seconds();
				AudioThreadTimingData.AudioThreadTime = 0.0;
				AudioThreadTimingData.AudioRenderThreadTime = 0.0;

				// Start streaming audio
				return AudioMixerPlatform->StartAudioStream();
			}
		}
		return false;
	}

	void FMixerDevice::FadeIn()
	{
		AudioMixerPlatform->FadeIn();
	}

	void FMixerDevice::FadeOut()
	{
		// In editor builds, we aren't going to fade out the main audio device.
#if WITH_EDITOR
		if (!IsMainAudioDevice())
#endif
		{
			AudioMixerPlatform->FadeOut();
		}
	}

	void FMixerDevice::TeardownHardware()
	{
		if (AudioMixerPlatform)
		{
			SourceManager.Update();

			AudioMixerPlatform->UnregisterDeviceChangedListener();
			AudioMixerPlatform->StopAudioStream();
			AudioMixerPlatform->CloseAudioStream();
			AudioMixerPlatform->TeardownHardware();
		}

		if (AmbisonicsMixer.IsValid())
		{
			AmbisonicsMixer->Shutdown();
		}
	}

	void FMixerDevice::UpdateHardwareTiming()
	{
		// Get the relative audio thread time (from start of audio engine)
		// Add some jitter delta to account for any audio thread timing jitter.
		const double AudioThreadJitterDelta = AudioClockDelta;
		AudioThreadTimingData.AudioThreadTime = FPlatformTime::Seconds() - AudioThreadTimingData.StartTime + AudioThreadJitterDelta;
	}

	void FMixerDevice::UpdateHardware()
	{
		LLM_SCOPE(ELLMTag::AudioMixer);

		// If we're in editor, re-query these in case they changed. 
		if (GIsEditor)
		{
			const UAudioSettings* AudioSettings = GetDefault<UAudioSettings>();
			MonoChannelUpmixMethod = AudioSettings->MonoChannelUpmixMethod;
			PanningMethod = AudioSettings->PanningMethod;
		}

		SourceManager.Update();

		AudioMixerPlatform->OnHardwareUpdate();

		if (AudioMixerPlatform->CheckAudioDeviceChange())
		{
			// Get the platform device info we're using
			PlatformInfo = AudioMixerPlatform->GetPlatformDeviceInfo();

			// Initialize some data that depends on speaker configuration, etc.
			InitializeChannelAzimuthMap(PlatformInfo.NumChannels);

			// Update the channel device count in case it changed
			SourceManager.UpdateDeviceChannelCount(PlatformInfo.NumChannels);

			// Audio rendering was suspended in CheckAudioDeviceChange if it changed.
			AudioMixerPlatform->ResumePlaybackOnNewDevice();
		}

		ListenerTransforms.Reset();
		for (FListener& Listener : Listeners)
		{
			ListenerTransforms.Add(Listener.Transform);
		}

		// Update listener transforms, some effects use the listener transform data
		SourceManager.SetListenerTransforms(ListenerTransforms);
	}

	double FMixerDevice::GetAudioTime() const
	{
		return AudioClock;
	}

	FAudioEffectsManager* FMixerDevice::CreateEffectsManager()
	{
		return new FAudioMixerEffectsManager(this);
	}

	FSoundSource* FMixerDevice::CreateSoundSource()
	{
		return new FMixerSource(this);
	}

	FName FMixerDevice::GetRuntimeFormat(USoundWave* InSoundWave)
	{
		check(AudioMixerPlatform);
		return AudioMixerPlatform->GetRuntimeFormat(InSoundWave);
	}

	bool FMixerDevice::HasCompressedAudioInfoClass(USoundWave* InSoundWave)
	{
		check(InSoundWave);
		check(AudioMixerPlatform);
		return AudioMixerPlatform->HasCompressedAudioInfoClass(InSoundWave);
	}

	bool FMixerDevice::SupportsRealtimeDecompression() const
	{
		return AudioMixerPlatform->SupportsRealtimeDecompression();
	}

	bool FMixerDevice::DisablePCMAudioCaching() const
	{
		return AudioMixerPlatform->DisablePCMAudioCaching();
	}

	class ICompressedAudioInfo* FMixerDevice::CreateCompressedAudioInfo(USoundWave* InSoundWave)
	{
		check(InSoundWave);
		check(AudioMixerPlatform);
		return AudioMixerPlatform->CreateCompressedAudioInfo(InSoundWave);
	}

	bool FMixerDevice::ValidateAPICall(const TCHAR* Function, uint32 ErrorCode)
	{
		return false;
	}

	bool FMixerDevice::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
	{
		if (FAudioDevice::Exec(InWorld, Cmd, Ar))
		{
			return true;
		}

		return false;
	}

	void FMixerDevice::CountBytes(FArchive& InArchive)
	{
		FAudioDevice::CountBytes(InArchive);
	}

	bool FMixerDevice::IsExernalBackgroundSoundActive()
	{
		return false;
	}

	void FMixerDevice::ResumeContext()
	{
        AudioMixerPlatform->ResumeContext();
	}

	void FMixerDevice::SuspendContext()
	{
        AudioMixerPlatform->SuspendContext();
	}

	void FMixerDevice::EnableDebugAudioOutput()
	{
		bDebugOutputEnabled = true;
	}

	bool FMixerDevice::OnProcessAudioStream(AlignedFloatBuffer& Output)
	{
#if WITH_EDITOR
		// Turn on to only hear PIE audio
		bool bBypassMainAudioDevice = FParse::Param(FCommandLine::Get(), TEXT("AudioPIEOnly"));
		if (bBypassMainAudioDevice && IsMainAudioDevice())
		{
			return true;
		}
#endif
		// This function could be called in a task manager, which means the thread ID may change between calls.
		ResetAudioRenderingThreadId();

		// Update the audio render thread time at the head of the render
		AudioThreadTimingData.AudioRenderThreadTime = FPlatformTime::Seconds() - AudioThreadTimingData.StartTime;

		// Pump the command queue to the audio render thread
		PumpCommandQueue();

		// Compute the next block of audio in the source manager
		SourceManager.ComputeNextBlockOfSamples();

		FMixerSubmixPtr MasterSubmix = GetMasterSubmix();

		{
			SCOPE_CYCLE_COUNTER(STAT_AudioMixerSubmixes);

			// Process the audio output from the master submix
			MasterSubmix->ProcessAudio(ESubmixChannelFormat::Device, Output);
		}

		// Reset stopping sounds and clear their state after submixes have been mixed
		SourceManager.ClearStoppingSounds();

		// Do any debug output performing
		if (bDebugOutputEnabled)
		{
			SineOscTest(Output);
		}

		// Update the audio clock
		AudioClock += AudioClockDelta;

		return true;
	}

	void FMixerDevice::OnAudioStreamShutdown()
	{
		// Make sure the source manager pumps any final commands on shutdown. These allow for cleaning up sources, interfacing with plugins, etc.
		// Because we double buffer our command queues, we call this function twice to ensure all commands are successfully pumped.
		SourceManager.PumpCommandQueue();
		SourceManager.PumpCommandQueue();

		// Make sure we force any pending release data to happen on shutdown
		SourceManager.UpdatePendingReleaseData(true);
	}

	void FMixerDevice::InitSoundSubmixes()
	{
		if (!IsInAudioThread())
		{
			DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.InitSoundSubmixes"), STAT_InitSoundSubmixes, STATGROUP_AudioThreadCommands);

			FAudioThread::RunCommandOnAudioThread([this]()
			{
				InitSoundSubmixes();
			}, GET_STATID(STAT_InitSoundSubmixes));
			return;
		}

		// Create the master, master reverb, and master eq sound submixes
		if (!FMixerDevice::MasterSubmixes.Num())
		{
			// Master
			USoundSubmix* MasterSubmix = NewObject<USoundSubmix>(USoundSubmix::StaticClass(), TEXT("Master Submix"));
			MasterSubmix->AddToRoot();
			FMixerDevice::MasterSubmixes.Add(MasterSubmix);

			// Master Reverb Plugin
			if (DisableSubmixReverbCVar)
			{
				FMixerDevice::MasterSubmixes.Add(nullptr);
			}
			else
			{
				USoundSubmix* ReverbPluginSubmix = NewObject<USoundSubmix>(USoundSubmix::StaticClass(), TEXT("Master Reverb Plugin Submix"));
				ReverbPluginSubmix->AddToRoot();
				FMixerDevice::MasterSubmixes.Add(ReverbPluginSubmix);
			}

			// Master Reverb
			if (DisableSubmixReverbCVar)
			{
				FMixerDevice::MasterSubmixes.Add(nullptr);
			}
			else
			{
				USoundSubmix* ReverbSubmix = NewObject<USoundSubmix>(USoundSubmix::StaticClass(), TEXT("Master Reverb Submix"));
				ReverbSubmix->AddToRoot();
				FMixerDevice::MasterSubmixes.Add(ReverbSubmix);
			}

			// Master EQ
			if (DisableSubmixEffectEQCvar)
			{
				FMixerDevice::MasterSubmixes.Add(nullptr);
			}
			else
			{
				USoundSubmix* EQSubmix = NewObject<USoundSubmix>(USoundSubmix::StaticClass(), TEXT("Master EQ Submix"));
				EQSubmix->AddToRoot();
				FMixerDevice::MasterSubmixes.Add(EQSubmix);
			}

			// Master ambisonics
			USoundSubmix* AmbisonicsSubmix = NewObject<USoundSubmix>(USoundSubmix::StaticClass(), TEXT("Master Ambisonics Submix"));
			AmbisonicsSubmix->AddToRoot();
			AmbisonicsSubmix->ChannelFormat = ESubmixChannelFormat::Ambisonics;
			if (AmbisonicsMixer.IsValid())
			{
				AmbisonicsSubmix->AmbisonicsPluginSettings = AmbisonicsMixer->GetDefaultSettings();
			}
			FMixerDevice::MasterSubmixes.Add(AmbisonicsSubmix);
		}

		// Register and setup the master submixes so that the rest of the submixes can hook into these core master submixes

		if (MasterSubmixInstances.Num() == 0)
		{
			for (int32 i = 0; i < EMasterSubmixType::Count; ++i)
			{
				if (FMixerDevice::MasterSubmixes[i])
				{
					FMixerSubmixPtr MixerSubmix = FMixerSubmixPtr(new FMixerSubmix(this));
					MixerSubmix->Init(FMixerDevice::MasterSubmixes[i]);

					MasterSubmixInstances.Add(MixerSubmix);
				}
				else
				{
					MasterSubmixInstances.Add(nullptr);
				}
			}

			FMixerSubmixPtr MasterSubmixInstance = MasterSubmixInstances[EMasterSubmixType::Master];
			
			FSoundEffectSubmixInitData InitData;
			InitData.SampleRate = GetSampleRate();

	
			// Setup the master reverb plugin
			if (ReverbPluginInterface.IsValid() && MasterSubmixInstances[EMasterSubmixType::ReverbPlugin].IsValid())
			{
				auto ReverbPluginEffectSubmix = ReverbPluginInterface->GetEffectSubmix(FMixerDevice::MasterSubmixes[EMasterSubmixType::ReverbPlugin]);

				ReverbPluginEffectSubmix->Init(InitData);
				ReverbPluginEffectSubmix->SetEnabled(true);

				const uint32 ReverbPluginId = FMixerDevice::MasterSubmixes[EMasterSubmixType::ReverbPlugin]->GetUniqueID();

				FMixerSubmixPtr MasterReverbPluginSubmix = MasterSubmixInstances[EMasterSubmixType::ReverbPlugin];
				MasterReverbPluginSubmix->AddSoundEffectSubmix(ReverbPluginId, MakeShareable(ReverbPluginEffectSubmix));
				MasterReverbPluginSubmix->SetParentSubmix(MasterSubmixInstance);
				MasterSubmixInstance->AddChildSubmix(MasterReverbPluginSubmix);
			}
			else if (MasterSubmixInstances[EMasterSubmixType::Reverb].IsValid())
			{
				// Setup the master reverb only if we don't have a reverb plugin

				USoundSubmix* MasterReverbSubix = FMixerDevice::MasterSubmixes[EMasterSubmixType::Reverb];
				USubmixEffectReverbPreset* ReverbPreset = NewObject<USubmixEffectReverbPreset>(MasterReverbSubix, TEXT("Master Reverb Effect Preset"));
				ReverbPreset->AddToRoot();

				FSoundEffectSubmix* ReverbEffectSubmix = static_cast<FSoundEffectSubmix*>(ReverbPreset->CreateNewEffect());

				ReverbEffectSubmix->Init(InitData);
				ReverbEffectSubmix->SetPreset(ReverbPreset);
				ReverbEffectSubmix->SetEnabled(true);

				const uint32 ReverbPresetId = ReverbPreset->GetUniqueID();

				FMixerSubmixPtr MasterReverbSubmix = MasterSubmixInstances[EMasterSubmixType::Reverb];
				MasterReverbSubmix->AddSoundEffectSubmix(ReverbPresetId, MakeShareable(ReverbEffectSubmix));
				MasterReverbSubmix->SetParentSubmix(MasterSubmixInstance);
				MasterSubmixInstance->AddChildSubmix(MasterReverbSubmix);
			}

			// Setup the master EQ
			if (FMixerDevice::MasterSubmixes[EMasterSubmixType::EQ])
			{
				USoundSubmix* MasterEQSoundSubmix = FMixerDevice::MasterSubmixes[EMasterSubmixType::EQ];
				USubmixEffectSubmixEQPreset* EQPreset = NewObject<USubmixEffectSubmixEQPreset>(MasterEQSoundSubmix, TEXT("Master EQ Effect preset"));
				EQPreset->AddToRoot();

				FSoundEffectSubmix* EQEffectSubmix = static_cast<FSoundEffectSubmix*>(EQPreset->CreateNewEffect());
				EQEffectSubmix->Init(InitData);
				EQEffectSubmix->SetPreset(EQPreset);
				EQEffectSubmix->SetEnabled(true);

				const uint32 EQPresetId = EQPreset->GetUniqueID();

				FMixerSubmixPtr MasterEQSubmix = MasterSubmixInstances[EMasterSubmixType::EQ];
				MasterEQSubmix->AddSoundEffectSubmix(EQPresetId, MakeShareable(EQEffectSubmix));
				MasterEQSubmix->SetParentSubmix(MasterSubmixInstance);
				MasterSubmixInstance->AddChildSubmix(MasterEQSubmix);

				// Add the ambisonics master submix
				FMixerSubmixPtr MasterAmbisonicsSubmix = MasterSubmixInstances[EMasterSubmixType::Ambisonics];
				MasterAmbisonicsSubmix->SetParentSubmix(MasterEQSubmix);
				MasterEQSubmix->AddChildSubmix(MasterAmbisonicsSubmix);
			}
		}

		// Now register all the non-core submixes

		// Reset existing submixes if they exist
		Submixes.Reset();

		// Make sure all submixes are registered but not initialized
		for (TObjectIterator<USoundSubmix> It; It; ++It)
		{
			RegisterSoundSubmix(*It, false);
		}

		// Now setup the graph for all the submixes
		for (auto& Entry : Submixes)
		{
			USoundSubmix* SoundSubmix = Entry.Key;
			FMixerSubmixPtr& SubmixInstance = Entry.Value;

			// Setup up the submix instance's parent and add the submix instance as a child
			FMixerSubmixPtr ParentSubmixInstance;
			if (SoundSubmix->ParentSubmix)
			{
				ParentSubmixInstance = GetSubmixInstance(SoundSubmix->ParentSubmix);
			}
			else
			{
				ParentSubmixInstance = GetMasterSubmix();
			}
			ParentSubmixInstance->AddChildSubmix(SubmixInstance);
			SubmixInstance->ParentSubmix = ParentSubmixInstance;

			// Now add all the child submixes to this submix instance
			for (USoundSubmix* ChildSubmix : SoundSubmix->ChildSubmixes)
			{
				// ChildSubmix lists can contain null entries.
				if (ChildSubmix)
				{
					FChildSubmixInfo ChildSubmixInfo;
					ChildSubmixInfo.SubmixPtr = GetSubmixInstance(ChildSubmix);
					ChildSubmixInfo.bNeedsAmbisonicsEncoding = true;
					SubmixInstance->ChildSubmixes.Add(ChildSubmixInfo.SubmixPtr->GetId(), ChildSubmixInfo);
				}
			}

			// Perform any other initialization on the submix instance
			SubmixInstance->Init(SoundSubmix);
		}

		TArray<EAudioMixerChannel::Type> StereoChannelTypes;
		StereoChannelTypes.Add(EAudioMixerChannel::FrontLeft);
		StereoChannelTypes.Add(EAudioMixerChannel::FrontRight);
		ChannelArrays.Add(ESubmixChannelFormat::Stereo, StereoChannelTypes);

		TArray<EAudioMixerChannel::Type> QuadChannelTypes;
		QuadChannelTypes.Add(EAudioMixerChannel::FrontLeft);
		QuadChannelTypes.Add(EAudioMixerChannel::FrontRight);
		QuadChannelTypes.Add(EAudioMixerChannel::SideLeft);
		QuadChannelTypes.Add(EAudioMixerChannel::SideRight);
		ChannelArrays.Add(ESubmixChannelFormat::Quad, QuadChannelTypes);

		TArray<EAudioMixerChannel::Type> FiveDotOneTypes;
		FiveDotOneTypes.Add(EAudioMixerChannel::FrontLeft);
		FiveDotOneTypes.Add(EAudioMixerChannel::FrontRight);
		FiveDotOneTypes.Add(EAudioMixerChannel::FrontCenter);
		FiveDotOneTypes.Add(EAudioMixerChannel::LowFrequency);
		FiveDotOneTypes.Add(EAudioMixerChannel::SideLeft);
		FiveDotOneTypes.Add(EAudioMixerChannel::SideRight);
		ChannelArrays.Add(ESubmixChannelFormat::FiveDotOne, FiveDotOneTypes);

		TArray<EAudioMixerChannel::Type> SevenDotOneTypes;
		SevenDotOneTypes.Add(EAudioMixerChannel::FrontLeft);
		SevenDotOneTypes.Add(EAudioMixerChannel::FrontRight);
		SevenDotOneTypes.Add(EAudioMixerChannel::FrontCenter);
		SevenDotOneTypes.Add(EAudioMixerChannel::LowFrequency);
		SevenDotOneTypes.Add(EAudioMixerChannel::BackLeft);
		SevenDotOneTypes.Add(EAudioMixerChannel::BackRight);
		SevenDotOneTypes.Add(EAudioMixerChannel::SideLeft);
		SevenDotOneTypes.Add(EAudioMixerChannel::SideRight);
		ChannelArrays.Add(ESubmixChannelFormat::SevenDotOne, SevenDotOneTypes);

		TArray<EAudioMixerChannel::Type> FirstOrderAmbisonicsTypes;
		FirstOrderAmbisonicsTypes.Add(EAudioMixerChannel::FrontLeft);
		FirstOrderAmbisonicsTypes.Add(EAudioMixerChannel::FrontRight);
		FirstOrderAmbisonicsTypes.Add(EAudioMixerChannel::FrontCenter);
		FirstOrderAmbisonicsTypes.Add(EAudioMixerChannel::LowFrequency);
		FirstOrderAmbisonicsTypes.Add(EAudioMixerChannel::BackLeft);
		FirstOrderAmbisonicsTypes.Add(EAudioMixerChannel::BackRight);
		FirstOrderAmbisonicsTypes.Add(EAudioMixerChannel::SideLeft);
		FirstOrderAmbisonicsTypes.Add(EAudioMixerChannel::SideRight);
		ChannelArrays.Add(ESubmixChannelFormat::Ambisonics, FirstOrderAmbisonicsTypes);
	}
	
 	FAudioPlatformSettings FMixerDevice::GetPlatformSettings() const
 	{
		FAudioPlatformSettings Settings = AudioMixerPlatform->GetPlatformSettings();

		UE_LOG(LogAudioMixer, Display, TEXT("Audio Mixer Platform Settings:"));
		UE_LOG(LogAudioMixer, Display, TEXT("	Sample Rate:						  %d"), Settings.SampleRate);
		UE_LOG(LogAudioMixer, Display, TEXT("	Callback Buffer Frame Size Requested: %d"), Settings.CallbackBufferFrameSize);
		UE_LOG(LogAudioMixer, Display, TEXT("	Callback Buffer Frame Size To Use:	  %d"), AudioMixerPlatform->GetNumFrames(PlatformSettings.CallbackBufferFrameSize));
		UE_LOG(LogAudioMixer, Display, TEXT("	Number of buffers to queue:			  %d"), Settings.NumBuffers);
		UE_LOG(LogAudioMixer, Display, TEXT("	Max Channels (voices):				  %d"), Settings.MaxChannels);
		UE_LOG(LogAudioMixer, Display, TEXT("	Number of Async Source Workers:		  %d"), Settings.NumSourceWorkers);

 		return Settings;
 	}

	FMixerSubmixPtr FMixerDevice::GetMasterSubmix()
	{
		return MasterSubmixInstances[EMasterSubmixType::Master];
	}

	FMixerSubmixPtr FMixerDevice::GetMasterReverbPluginSubmix()
	{
		return MasterSubmixInstances[EMasterSubmixType::ReverbPlugin];
	}

	FMixerSubmixPtr FMixerDevice::GetMasterReverbSubmix()
	{
		return MasterSubmixInstances[EMasterSubmixType::Reverb];
	}

	FMixerSubmixPtr FMixerDevice::GetMasterEQSubmix()
	{
		return MasterSubmixInstances[EMasterSubmixType::EQ];
	}

	FMixerSubmixPtr FMixerDevice::GetMasterAmbisonicsSubmix()
	{
		return MasterSubmixInstances[EMasterSubmixType::Ambisonics];
	}

	void FMixerDevice::AddMasterSubmixEffect(uint32 SubmixEffectId, FSoundEffectSubmix* SoundEffectSubmix)
	{
		AudioRenderThreadCommand([this, SubmixEffectId, SoundEffectSubmix]()
		{
			MasterSubmixInstances[EMasterSubmixType::Master]->AddSoundEffectSubmix(SubmixEffectId, MakeShareable(SoundEffectSubmix));
		});
	}

	void FMixerDevice::RemoveMasterSubmixEffect(uint32 SubmixEffectId)
	{
		AudioRenderThreadCommand([this, SubmixEffectId]()
		{
			MasterSubmixInstances[EMasterSubmixType::Master]->RemoveSoundEffectSubmix(SubmixEffectId);
		});
	}

	void FMixerDevice::ClearMasterSubmixEffects()
	{
		AudioRenderThreadCommand([this]()
		{
			MasterSubmixInstances[EMasterSubmixType::Master]->ClearSoundEffectSubmixes();
		});
	}

	void FMixerDevice::UpdateSourceEffectChain(const uint32 SourceEffectChainId, const TArray<FSourceEffectChainEntry>& SourceEffectChain, const bool bPlayEffectChainTails)
	{
		TArray<FSourceEffectChainEntry>* ExistingOverride = SourceEffectChainOverrides.Find(SourceEffectChainId);
		if (ExistingOverride)
		{
			*ExistingOverride = SourceEffectChain;
		}
		else
		{
			SourceEffectChainOverrides.Add(SourceEffectChainId, SourceEffectChain);
		}

		SourceManager.UpdateSourceEffectChain(SourceEffectChainId, SourceEffectChain, bPlayEffectChainTails);
	}

	bool FMixerDevice::GetCurrentSourceEffectChain(const uint32 SourceEffectChainId, TArray<FSourceEffectChainEntry>& OutCurrentSourceEffectChainEntries)
	{
		TArray<FSourceEffectChainEntry>* ExistingOverride = SourceEffectChainOverrides.Find(SourceEffectChainId);
		if (ExistingOverride)
		{
			OutCurrentSourceEffectChainEntries = *ExistingOverride;
			return true;
		}
		return false;
	}

	void FMixerDevice::AudioRenderThreadCommand(TFunction<void()> Command)
	{
		CommandQueue.Enqueue(MoveTemp(Command));
	}

	void FMixerDevice::PumpCommandQueue()
	{
		// Execute the pushed lambda functions
		TFunction<void()> Command;
		while (CommandQueue.Dequeue(Command))
		{
			Command();
		}
	}


	bool FMixerDevice::IsMasterSubmixType(USoundSubmix* InSubmix) const
	{
		for (int32 i = 0; i < EMasterSubmixType::Count; ++i)
		{
			if (InSubmix == FMixerDevice::MasterSubmixes[i])
			{
				return true;
			}
		}
		return false;
	}

	void FMixerDevice::RegisterSoundSubmix(USoundSubmix* InSoundSubmix, bool bInit)
	{
		if (InSoundSubmix)
		{
			if (!IsInAudioThread())
			{
				DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.RegisterSoundSubmix"), STAT_AudioRegisterSoundSubmix, STATGROUP_AudioThreadCommands);

				FMixerDevice* MixerDevice = this;
				FAudioThread::RunCommandOnAudioThread([MixerDevice, InSoundSubmix]()
				{
					MixerDevice->RegisterSoundSubmix(InSoundSubmix);
				}, GET_STATID(STAT_AudioRegisterSoundSubmix));
				return;
			}

			if (!IsMasterSubmixType(InSoundSubmix))
			{
				// If the sound submix wasn't already registered get it into the system.
				if (!Submixes.Contains(InSoundSubmix))
				{
					FMixerSubmixPtr MixerSubmix = FMixerSubmixPtr(new FMixerSubmix(this));
					Submixes.Add(InSoundSubmix, MixerSubmix);

					if (bInit)
					{
						// Setup the parent-child relationship
						FMixerSubmixPtr ParentSubmixInstance;
						if (InSoundSubmix->ParentSubmix)
						{
							ParentSubmixInstance = GetSubmixInstance(InSoundSubmix->ParentSubmix);
						}
						else
						{
							ParentSubmixInstance = GetMasterSubmix();
						}

						ParentSubmixInstance->AddChildSubmix(MixerSubmix);
						MixerSubmix->SetParentSubmix(ParentSubmixInstance);

						MixerSubmix->Init(InSoundSubmix);
					}
				}
			}
		}
	}

	void FMixerDevice::UnregisterSoundSubmix(USoundSubmix* InSoundSubmix)
	{
		if (InSoundSubmix)
		{
			if (!IsInAudioThread())
			{
				DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.UnregisterSoundSubmix"), STAT_AudioUnregisterSoundSubmix, STATGROUP_AudioThreadCommands);

				FMixerDevice* MixerDevice = this;
				FAudioThread::RunCommandOnAudioThread([MixerDevice, InSoundSubmix]()
				{
					MixerDevice->UnregisterSoundSubmix(InSoundSubmix);
				}, GET_STATID(STAT_AudioUnregisterSoundSubmix));
				return;
			}

			if (!IsMasterSubmixType(InSoundSubmix))
			{
				if (InSoundSubmix)
				{
					Submixes.Remove(InSoundSubmix);
				}
			}
		}
	}

	void FMixerDevice::InitSoundEffectPresets()
	{
#if WITH_EDITOR
		IAudioEditorModule* AudioEditorModule = &FModuleManager::LoadModuleChecked<IAudioEditorModule>("AudioEditor");
		AudioEditorModule->RegisterEffectPresetAssetActions();
#endif
	}

	FMixerSubmixPtr FMixerDevice::GetSubmixInstance(USoundSubmix* SoundSubmix)
	{
		LLM_SCOPE(ELLMTag::AudioMixer);

		check(SoundSubmix);
		FMixerSubmixPtr* MixerSubmix = Submixes.Find(SoundSubmix);

		// If the submix hasn't been registered yet, then register it now
		if (!MixerSubmix)
		{
			RegisterSoundSubmix(SoundSubmix, true);
			MixerSubmix = Submixes.Find(SoundSubmix);
		}

		// At this point, this should exist
		check(MixerSubmix);
		return *MixerSubmix;		
	}

	FMixerSourceVoice* FMixerDevice::GetMixerSourceVoice()
	{
		LLM_SCOPE(ELLMTag::AudioMixer);

		FMixerSourceVoice* Voice = nullptr;
		if (!SourceVoices.Dequeue(Voice))
		{
			Voice = new FMixerSourceVoice();
		}

		Voice->Reset(this);
		return Voice;
	}

	void FMixerDevice::ReleaseMixerSourceVoice(FMixerSourceVoice* InSourceVoice)
	{
		SourceVoices.Enqueue(InSourceVoice);
	}

	int32 FMixerDevice::GetNumSources() const
	{
		return Sources.Num();
	}

	int32 FMixerDevice::GetNumActiveSources() const
	{
		return SourceManager.GetNumActiveSources();
	}

	void FMixerDevice::Get3DChannelMap(const ESubmixChannelFormat InSubmixType, const FWaveInstance* InWaveInstance, float EmitterAzimith, float NormalizedOmniRadius, Audio::AlignedFloatBuffer& OutChannelMap)
	{
		// If we're center-channel only, then no need for spatial calculations, but need to build a channel map
		if (InWaveInstance->bCenterChannelOnly)
		{
			int32 NumOutputChannels = GetNumChannelsForSubmixFormat(InSubmixType);
			const TArray<EAudioMixerChannel::Type>& ChannelArray = GetChannelArrayForSubmixChannelType(InSubmixType);

			// If we are only spatializing to stereo output
			if (NumOutputChannels == 2)
			{
				// Equal volume in left + right channel with equal power panning
				static const float Pan = 1.0f / FMath::Sqrt(2.0f);
				OutChannelMap.Add(Pan);
				OutChannelMap.Add(Pan);
			}
			else
			{
				for (EAudioMixerChannel::Type Channel : ChannelArray)
				{
					float Pan = (Channel == EAudioMixerChannel::FrontCenter) ? 1.0f : 0.0f;
					OutChannelMap.Add(Pan);
				}
			}

			return;
		}

		float Azimuth = EmitterAzimith;

		const FChannelPositionInfo* PrevChannelInfo = nullptr;
		const FChannelPositionInfo* NextChannelInfo = nullptr;

		const TArray<FChannelPositionInfo>* CurrentChannelAzimuthPositionsPtr = ChannelAzimuthPositions.Find(InSubmixType);
		const TArray<FChannelPositionInfo>& CurrentChannelAzimuthPositions = *CurrentChannelAzimuthPositionsPtr;

		for (int32 i = 0; i < CurrentChannelAzimuthPositions.Num(); ++i)
		{
			const FChannelPositionInfo& ChannelPositionInfo = CurrentChannelAzimuthPositions[i];

			if (Azimuth <= ChannelPositionInfo.Azimuth)
			{
				NextChannelInfo = &CurrentChannelAzimuthPositions[i];

				int32 PrevIndex = i - 1;
				if (PrevIndex < 0)
				{
					PrevIndex = CurrentChannelAzimuthPositions.Num() - 1;
				}

				PrevChannelInfo = &CurrentChannelAzimuthPositions[PrevIndex];
				break;
			}
		}

		// If we didn't find anything, that means our azimuth position is at the top of the mapping
		if (PrevChannelInfo == nullptr)
		{
			PrevChannelInfo = &CurrentChannelAzimuthPositions[CurrentChannelAzimuthPositions.Num() - 1];
			NextChannelInfo = &CurrentChannelAzimuthPositions[0];
			AUDIO_MIXER_CHECK(PrevChannelInfo != NextChannelInfo);
		}

		float NextChannelAzimuth = NextChannelInfo->Azimuth;
		float PrevChannelAzimuth = PrevChannelInfo->Azimuth;

		if (NextChannelAzimuth < PrevChannelAzimuth)
		{
			NextChannelAzimuth += 360.0f;
		}

		if (Azimuth < PrevChannelAzimuth)
		{
			Azimuth += 360.0f;
		}

		AUDIO_MIXER_CHECK(NextChannelAzimuth > PrevChannelAzimuth);
		AUDIO_MIXER_CHECK(Azimuth > PrevChannelAzimuth);
		float Fraction = (Azimuth - PrevChannelAzimuth) / (NextChannelAzimuth - PrevChannelAzimuth);
		AUDIO_MIXER_CHECK(Fraction >= 0.0f && Fraction <= 1.0f);

		// Compute the panning values using equal-power panning law
		float PrevChannelPan; 
		float NextChannelPan;

		if (PanningMethod == EPanningMethod::EqualPower)
		{
			FMath::SinCos(&NextChannelPan, &PrevChannelPan, Fraction * 0.5f * PI);

			// Note that SinCos can return values slightly greater than 1.0 when very close to PI/2
			NextChannelPan = FMath::Clamp(NextChannelPan, 0.0f, 1.0f);
			PrevChannelPan = FMath::Clamp(PrevChannelPan, 0.0f, 1.0f);
		}
		else
		{
			NextChannelPan = Fraction;
			PrevChannelPan = 1.0f - Fraction;
		}

		float NormalizedOmniRadSquared = NormalizedOmniRadius * NormalizedOmniRadius;
		float OmniAmount = 0.0f;

		if (NormalizedOmniRadSquared > 1.0f)
		{
			OmniAmount = 1.0f - 1.0f / NormalizedOmniRadSquared;
		}

		// Build the output channel map based on the current platform device output channel array 

		int32 NumSpatialChannels = CurrentChannelAzimuthPositions.Num();
		if (CurrentChannelAzimuthPositions.Num() > 4)
		{
			NumSpatialChannels--;
		}
		float OmniPanFactor = 1.0f / NumSpatialChannels;

		float DefaultEffectivePan = !OmniAmount ? 0.0f : FMath::Lerp(0.0f, OmniPanFactor, OmniAmount);
		const TArray<EAudioMixerChannel::Type>& ChannelArray = GetChannelArrayForSubmixChannelType(InSubmixType);

		for (EAudioMixerChannel::Type Channel : ChannelArray)
		{
			float EffectivePan = DefaultEffectivePan;

			// Check for manual channel mapping parameters (LFE and Front Center)
			if (Channel == EAudioMixerChannel::LowFrequency)
			{
				EffectivePan = InWaveInstance->LFEBleed;
			}
			else if (Channel == PrevChannelInfo->Channel)
			{
				EffectivePan = !OmniAmount ? PrevChannelPan : FMath::Lerp(PrevChannelPan, OmniPanFactor, OmniAmount);
			}
			else if (Channel == NextChannelInfo->Channel)
			{
				EffectivePan = !OmniAmount ? NextChannelPan : FMath::Lerp(NextChannelPan, OmniPanFactor, OmniAmount);
			}

			if (Channel == EAudioMixerChannel::FrontCenter)
			{
				EffectivePan = FMath::Max(InWaveInstance->VoiceCenterChannelVolume, EffectivePan);
			}

			AUDIO_MIXER_CHECK(EffectivePan >= 0.0f && EffectivePan <= 1.0f);
			OutChannelMap.Add(EffectivePan);
		}
	}

	uint32 FMixerDevice::GetNewUniqueAmbisonicsStreamID()
	{
		static uint32 AmbisonicsStreamIDCounter = 0;
		return ++AmbisonicsStreamIDCounter;
	}

	const TArray<FTransform>* FMixerDevice::GetListenerTransforms()
	{
		return SourceManager.GetListenerTransforms();
	}

	void FMixerDevice::StartRecording(USoundSubmix* InSubmix, float ExpectedRecordingDuration)
	{
		// if we can find the submix here, record that submix. Otherwise, just record the master submix.
		Audio::FMixerSubmixPtr* FoundSubmix = Submixes.Find(InSubmix);
		if (FoundSubmix)
		{
			(*FoundSubmix)->OnStartRecordingOutput(ExpectedRecordingDuration);
		}
		else
		{
			GetMasterSubmix()->OnStartRecordingOutput(ExpectedRecordingDuration);
		}
	}

	Audio::AlignedFloatBuffer& FMixerDevice::StopRecording(USoundSubmix* InSubmix, float& OutNumChannels, float& OutSampleRate)
	{
		// if we can find the submix here, record that submix. Otherwise, just record the master submix.
		Audio::FMixerSubmixPtr* FoundSubmix = Submixes.Find(InSubmix);
		if (FoundSubmix)
		{
			return (*FoundSubmix)->OnStopRecordingOutput(OutNumChannels, OutSampleRate);
		}
		else
		{
			return GetMasterSubmix()->OnStopRecordingOutput(OutNumChannels, OutSampleRate);
		}
	}

	void FMixerDevice::RegisterSubmixBufferListener(ISubmixBufferListener* InSubmixBufferListener, USoundSubmix* InSubmix)
	{
		Audio::FMixerSubmixPtr* FoundSubmix = Submixes.Find(InSubmix);
		if (FoundSubmix)
		{
			return (*FoundSubmix)->RegisterBufferListener(InSubmixBufferListener);
		}
		else
		{
			return GetMasterSubmix()->RegisterBufferListener(InSubmixBufferListener);
		}
	}

	void FMixerDevice::UnregisterSubmixBufferListener(ISubmixBufferListener* InSubmixBufferListener, USoundSubmix* InSubmix)
	{
		Audio::FMixerSubmixPtr* FoundSubmix = Submixes.Find(InSubmix);
		if (FoundSubmix)
		{
			return (*FoundSubmix)->UnregisterBufferListener(InSubmixBufferListener);
		}
		else
		{
			return GetMasterSubmix()->UnregisterBufferListener(InSubmixBufferListener);
		}
	}

	int32 FMixerDevice::GetDeviceSampleRate() const
	{
		return SampleRate;
	}

	int32 FMixerDevice::GetDeviceOutputChannels() const
	{
		return PlatformInfo.NumChannels;
	}

	FMixerSourceManager* FMixerDevice::GetSourceManager()
	{
		return &SourceManager;
	}

	bool FMixerDevice::IsMainAudioDevice() const
	{
		bool bIsMain = (this == GEngine->GetMainAudioDevice());
		return bIsMain;
	}

	void FMixerDevice::WhiteNoiseTest(AlignedFloatBuffer& Output)
	{
		const int32 NumFrames = OpenStreamParams.NumFrames;
		const int32 NumChannels = PlatformInfo.NumChannels;

		static FWhiteNoise WhiteNoise(0.2f);

		for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
		{
			for (int32 ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
			{
				int32 Index = FrameIndex * NumChannels + ChannelIndex;
				Output[Index] += WhiteNoise.Generate();
			}
		}
	}

	void FMixerDevice::SineOscTest(AlignedFloatBuffer& Output)
	{
		const int32 NumFrames = OpenStreamParams.NumFrames;
		const int32 NumChannels = PlatformInfo.NumChannels;

		check(NumChannels > 0);

		static FSineOsc SineOscLeft(PlatformInfo.SampleRate, 440.0f, 0.2f);
		static FSineOsc SineOscRight(PlatformInfo.SampleRate, 220.0f, 0.2f);

		for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
		{
			int32 Index = FrameIndex * NumChannels;

			Output[Index] += SineOscLeft.ProcessAudio();

			if (NumChannels > 1)
			{
				Output[Index + 1] += SineOscRight.ProcessAudio();
			}
		}
	}

}
