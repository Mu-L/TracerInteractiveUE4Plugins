// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioMixerPlatformSDL.h"
#include "Modules/ModuleManager.h"
#include "AudioMixer.h"
#include "AudioMixerDevice.h"
#include "CoreGlobals.h"
#include "Misc/ConfigCacheIni.h"

#if WITH_ENGINE
#include "AudioPluginUtilities.h"
#include "OpusAudioInfo.h"
#include "VorbisAudioInfo.h"
#include "ADPCMAudioInfo.h"
#endif // WITH_ENGINE

DECLARE_LOG_CATEGORY_EXTERN(LogAudioMixerSDL, Log, All);
DEFINE_LOG_CATEGORY(LogAudioMixerSDL);

namespace Audio
{
	// Static callback function used in SDL
	static void OnBufferEnd(void* BufferContext, uint8* OutputBuffer, int32 OutputBufferLength)
	{
		check(BufferContext);
		FMixerPlatformSDL* MixerPlatform = (FMixerPlatformSDL*)BufferContext;
		MixerPlatform->HandleOnBufferEnd(OutputBuffer, OutputBufferLength);
	}

	FMixerPlatformSDL::FMixerPlatformSDL()
		: AudioDeviceID(INDEX_NONE)
		, OutputBuffer(nullptr)
		, OutputBufferByteLength(0)
		, bSuspended(false)
		, bInitialized(false)
	{
	}

	FMixerPlatformSDL::~FMixerPlatformSDL()
	{
		if (bInitialized)
		{
			TeardownHardware();
		}
	}

	bool FMixerPlatformSDL::InitializeHardware()
	{
		if (bInitialized)
		{
			UE_LOG(LogAudioMixerSDL, Error, TEXT("SDL Audio already initialized."));
			return false;
		}

		int32 Result = SDL_InitSubSystem(SDL_INIT_AUDIO);
		if (Result < 0)
		{
			UE_LOG(LogAudioMixerSDL, Error, TEXT("SDL_InitSubSystem create failed: %d"), Result);
			return false;
		}

		const char* DriverName = SDL_GetCurrentAudioDriver();
		UE_LOG(LogAudioMixerSDL, Display, TEXT("Initialized SDL using %s platform API backend."), ANSI_TO_TCHAR(DriverName));

		bInitialized = true;
		return true;
	}

	bool FMixerPlatformSDL::TeardownHardware()
	{
		if(!bInitialized)
		{
			return true;
		}

		StopAudioStream();
		CloseAudioStream();

		// this is refcounted
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		bInitialized = false;

		return true;
	}

	bool FMixerPlatformSDL::IsInitialized() const
	{
		return bInitialized;
	}

	bool FMixerPlatformSDL::GetNumOutputDevices(uint32& OutNumOutputDevices)
	{
		if (!bInitialized)
		{
			UE_LOG(LogAudioMixerSDL, Error, TEXT("SDL2 Audio is not initialized."));
			return false;
		}

		SDL_bool IsCapture = SDL_FALSE;
		OutNumOutputDevices = SDL_GetNumAudioDevices(IsCapture);
		return true;
	}

	bool FMixerPlatformSDL::GetOutputDeviceInfo(const uint32 InDeviceIndex, FAudioPlatformDeviceInfo& OutInfo)
	{
		// To figure out the output device info, attempt to init at 7.1, and 48k.
		// SDL_OpenAudioDevice will attempt open the audio device with that spec but return what it actually used. We'll report that in OutInfo
		FAudioPlatformSettings PlatformSettings = GetPlatformSettings();
		SDL_AudioSpec DesiredSpec;
		DesiredSpec.freq = PlatformSettings.SampleRate;

		DesiredSpec.format = GetPlatformAudioFormat();
		DesiredSpec.channels = GetPlatformChannels();

		DesiredSpec.samples = PlatformSettings.CallbackBufferFrameSize;
		DesiredSpec.callback = OnBufferEnd;
		DesiredSpec.userdata = (void*)this;

		// It's not possible with SDL to tell whether a given index is default. It only supports directly opening a device handle by passing in a nullptr to SDL_OpenAudioDevice.
		OutInfo.bIsSystemDefault = false;

		const char* AudioDeviceName = nullptr;
		FString DeviceName;

		if (InDeviceIndex != AUDIO_MIXER_DEFAULT_DEVICE_INDEX)
		{
			AudioDeviceName = SDL_GetAudioDeviceName(InDeviceIndex, SDL_FALSE);
			DeviceName = ANSI_TO_TCHAR(AudioDeviceName);
		}
		else
		{
			DeviceName = TEXT("Default Audio Device");
		}

		SDL_AudioSpec ActualSpec;
		SDL_AudioDeviceID TempAudioDeviceID = SDL_OpenAudioDevice(AudioDeviceName, SDL_FALSE, &DesiredSpec, &ActualSpec, SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
		if (!TempAudioDeviceID)
		{
			const char* ErrorText = SDL_GetError();
			UE_LOG(LogAudioMixerSDL, Error, TEXT("%s"), ANSI_TO_TCHAR(ErrorText));
		}

		// Name and Id are the same for SDL
		OutInfo.DeviceId = DeviceName;
		OutInfo.Name = OutInfo.DeviceId;
		OutInfo.SampleRate = ActualSpec.freq;
		
		OutInfo.Format = GetAudioStreamFormat();

		ensure(ActualSpec.channels <= AUDIO_MIXER_MAX_OUTPUT_CHANNELS);
		OutInfo.NumChannels = FMath::Min<int32>(static_cast<int32>(ActualSpec.channels), AUDIO_MIXER_MAX_OUTPUT_CHANNELS);

		// Assume default channel map order, SDL doesn't support us querying it directly
		OutInfo.OutputChannelArray.Reset();
		for (int32 i = 0; i < OutInfo.NumChannels; ++i)
		{
			OutInfo.OutputChannelArray.Add(EAudioMixerChannel::Type(i));
		}

		SDL_CloseAudioDevice(TempAudioDeviceID);

		return true;
	}

	bool FMixerPlatformSDL::GetDefaultOutputDeviceIndex(uint32& OutDefaultDeviceIndex) const
	{
		// It's not possible to know what index the default audio device is.
		OutDefaultDeviceIndex = AUDIO_MIXER_DEFAULT_DEVICE_INDEX;
		return true;
	}

	bool FMixerPlatformSDL::OpenAudioStream(const FAudioMixerOpenStreamParams& Params)
	{
		if (!bInitialized || AudioStreamInfo.StreamState != EAudioOutputStreamState::Closed)
		{
			return false;
		}

		OpenStreamParams = Params;

		AudioStreamInfo.Reset();
		AudioStreamInfo.OutputDeviceIndex = OpenStreamParams.OutputDeviceIndex;
		AudioStreamInfo.NumOutputFrames = OpenStreamParams.NumFrames;
		AudioStreamInfo.NumBuffers = OpenStreamParams.NumBuffers;
		AudioStreamInfo.AudioMixer = OpenStreamParams.AudioMixer;

		if (!GetOutputDeviceInfo(AudioStreamInfo.OutputDeviceIndex, AudioStreamInfo.DeviceInfo))
		{
			return false;
		}

		AudioSpecPrefered.format = GetPlatformAudioFormat();
		AudioSpecPrefered.freq = Params.SampleRate;
		AudioSpecPrefered.channels = AudioStreamInfo.DeviceInfo.NumChannels;
		AudioSpecPrefered.samples = OpenStreamParams.NumFrames;
		AudioSpecPrefered.callback = OnBufferEnd;
		AudioSpecPrefered.userdata = (void*)this;

		const char* DeviceName = nullptr;
		if (OpenStreamParams.OutputDeviceIndex != AUDIO_MIXER_DEFAULT_DEVICE_INDEX && OpenStreamParams.OutputDeviceIndex < (uint32)SDL_GetNumAudioDevices(0))
		{
			DeviceName = SDL_GetAudioDeviceName(OpenStreamParams.OutputDeviceIndex, 0);
		}

		// only the default device can be overriden
		FString CurrentDeviceName = GetCurrentDeviceName();
		if (OpenStreamParams.OutputDeviceIndex != AUDIO_MIXER_DEFAULT_DEVICE_INDEX || CurrentDeviceName.Len() <= 0)
		{
			UE_LOG(LogAudioMixerSDL, Log, TEXT("Opening %s audio device (device index %d)"), DeviceName ? ANSI_TO_TCHAR(DeviceName) : TEXT("default"), OpenStreamParams.OutputDeviceIndex);
			AudioDeviceID = SDL_OpenAudioDevice(DeviceName, 0, &AudioSpecPrefered, &AudioSpecReceived, 0);
		}
		else
		{
			UE_LOG(LogAudioMixerSDL, Log, TEXT("Opening overridden '%s' audio device (device index %d)"), *CurrentDeviceName, OpenStreamParams.OutputDeviceIndex);
			AudioDeviceID = SDL_OpenAudioDevice(TCHAR_TO_ANSI(*CurrentDeviceName), 0, &AudioSpecPrefered, &AudioSpecReceived, 0);
		}

		if (!AudioDeviceID)
		{
			const char* ErrorText = SDL_GetError();
			UE_LOG(LogAudioMixerSDL, Error, TEXT("%s"), ANSI_TO_TCHAR(ErrorText));
			return false;
		}

		// Make sure our device initialized as expected, we should have already filtered this out before this point.
		check(AudioSpecReceived.channels == AudioSpecPrefered.channels);
		check(AudioSpecReceived.samples == OpenStreamParams.NumFrames);

		// Compute the expected output byte length
		OutputBufferByteLength = OpenStreamParams.NumFrames * AudioStreamInfo.DeviceInfo.NumChannels * GetAudioStreamChannelSize();
		check(OutputBufferByteLength == AudioSpecReceived.size);

		AudioStreamInfo.StreamState = EAudioOutputStreamState::Open;

		return true;
	}

	bool FMixerPlatformSDL::CloseAudioStream()
	{
		if (AudioStreamInfo.StreamState == EAudioOutputStreamState::Closed)
		{
			return false;
		}

		if (!StopAudioStream())
		{
			return false;
		}

		if (AudioDeviceID != INDEX_NONE)
		{
			FScopeLock ScopedLock(&OutputBufferMutex);

			SDL_CloseAudioDevice(AudioDeviceID);

			OutputBuffer = nullptr;
			OutputBufferByteLength = 0;
		}

		AudioStreamInfo.StreamState = EAudioOutputStreamState::Closed;
		return true;
	}

	bool FMixerPlatformSDL::StartAudioStream()
	{
		if (!bInitialized || (AudioStreamInfo.StreamState != EAudioOutputStreamState::Open && AudioStreamInfo.StreamState != EAudioOutputStreamState::Stopped))
		{
			return false;
		}

		// Start generating audio
		BeginGeneratingAudio();

		// Unpause audio device to start it rendering audio
		SDL_PauseAudioDevice(AudioDeviceID, 0);

		AudioStreamInfo.StreamState = EAudioOutputStreamState::Running;

		return true;
	}

	bool FMixerPlatformSDL::StopAudioStream()
	{
		if (AudioStreamInfo.StreamState != EAudioOutputStreamState::Stopped && AudioStreamInfo.StreamState != EAudioOutputStreamState::Closed)
		{
			// Pause the audio device
			SDL_PauseAudioDevice(AudioDeviceID, 1);

			if (AudioStreamInfo.StreamState == EAudioOutputStreamState::Running)
			{
				StopGeneratingAudio();
			}

			check(AudioStreamInfo.StreamState == EAudioOutputStreamState::Stopped);
		}

		return true;
	}

	FAudioPlatformDeviceInfo FMixerPlatformSDL::GetPlatformDeviceInfo() const
	{
		return AudioStreamInfo.DeviceInfo;
	}

	void FMixerPlatformSDL::SubmitBuffer(const uint8* Buffer)
	{
		// Need to prevent the case in which we close down the audio stream leaving this point to potentially corrupt the free'ed pointer
		FScopeLock ScopedLock(&OutputBufferMutex);

		if (OutputBuffer)
		{
			FMemory::Memcpy(OutputBuffer, Buffer, OutputBufferByteLength);
		}
	}

	void FMixerPlatformSDL::HandleOnBufferEnd(uint8* InOutputBuffer, int32 InOutputBufferByteLength)
	{
		if (!bIsDeviceInitialized)
		{
			return;
		}

		OutputBuffer = InOutputBuffer;
		check(InOutputBufferByteLength == OutputBufferByteLength);

		ReadNextBuffer();
	}

	FName FMixerPlatformSDL::GetRuntimeFormat(USoundWave* InSoundWave)
	{
#if WITH_ENGINE
		static FName NAME_OGG(TEXT("OGG"));
		static FName NAME_OPUS(TEXT("OPUS"));
		static FName NAME_ADPCM(TEXT("ADPCM"));

		if (InSoundWave->IsStreaming(nullptr))
		{
			if (InSoundWave->IsSeekableStreaming())
			{
				return NAME_ADPCM;
			}

			return NAME_OPUS;
		}
		return NAME_OGG;
#else
		checkNoEntry();
		return FName();
#endif // WITH_ENGINE
	}

	bool FMixerPlatformSDL::HasCompressedAudioInfoClass(USoundWave* InSoundWave)
	{
		return true;
	}

	ICompressedAudioInfo* FMixerPlatformSDL::CreateCompressedAudioInfo(USoundWave* InSoundWave)
	{
#if WITH_ENGINE
		check(InSoundWave);

		if (InSoundWave->IsStreaming())
		{
			if (InSoundWave->IsSeekableStreaming())
			{
				return new FADPCMAudioInfo();
			}

			return new FOpusAudioInfo();
		}

		static FName NAME_OGG(TEXT("OGG"));
		if (InSoundWave->HasCompressedData(NAME_OGG))
		{
			return new FVorbisAudioInfo();
		}

		return new FADPCMAudioInfo();
#else
		checkNoEntry();
		return nullptr;
#endif // WITH_ENGINE
	}

	FString FMixerPlatformSDL::GetDefaultDeviceName()
	{
		static FString DefaultName(TEXT("Default SDL Audio Device."));
		return DefaultName;
	}

	void FMixerPlatformSDL::ResumeContext()
	{
		if (bSuspended)
		{
			SDL_UnlockAudioDevice(AudioDeviceID);
			UE_LOG(LogAudioMixerSDL, Display, TEXT("Resuming Audio"));
			bSuspended = false;
		}
	}

	void FMixerPlatformSDL::SuspendContext()
	{
		if (!bSuspended)
		{
			SDL_LockAudioDevice(AudioDeviceID);
			UE_LOG(LogAudioMixerSDL, Display, TEXT("Suspending Audio"));
			bSuspended = true;
		}
	}

	FAudioPlatformSettings FMixerPlatformSDL::GetPlatformSettings() const
	{
#if PLATFORM_UNIX
		return FAudioPlatformSettings::GetPlatformSettings(FPlatformProperties::GetRuntimeSettingsClassName());
#else
		// On Windows, use default parameters.
		return FAudioPlatformSettings();
#endif
	}
}
