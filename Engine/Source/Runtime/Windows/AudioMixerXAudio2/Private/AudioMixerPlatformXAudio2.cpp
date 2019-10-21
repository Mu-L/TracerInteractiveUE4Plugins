// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/**
	Concrete implementation of FAudioDevice for XAudio2

	See https://msdn.microsoft.com/en-us/library/windows/desktop/hh405049%28v=vs.85%29.aspx
*/

#include "AudioMixerPlatformXAudio2.h"
#include "AudioMixer.h"
#include "AudioMixerDevice.h"
#include "HAL/PlatformAffinity.h"

#ifndef WITH_XMA2
#define WITH_XMA2 0
#endif

#if WITH_XMA2
#include "XMAAudioInfo.h"
#endif  //#if WITH_XMA2
#include "OpusAudioInfo.h"
#include "VorbisAudioInfo.h"
#include "ADPCMAudioInfo.h"

#include "CoreGlobals.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/MessageDialog.h"
#include "AudioCompressionSettingsUtils.h"

// Macro to check result code for XAudio2 failure, get the string version, log, and goto a cleanup
#define XAUDIO2_CLEANUP_ON_FAIL(Result)						\
	if (FAILED(Result))										\
	{														\
		const TCHAR* ErrorString = GetErrorString(Result);	\
		AUDIO_PLATFORM_ERROR(ErrorString);					\
		goto Cleanup;										\
	}

// Macro to check result for XAudio2 failure, get string version, log, and return false
#define XAUDIO2_RETURN_ON_FAIL(Result)						\
	if (FAILED(Result))										\
	{														\
		const TCHAR* ErrorString = GetErrorString(Result);	\
		AUDIO_PLATFORM_ERROR(ErrorString);					\
		return false;										\
	}


namespace Audio
{
#if PLATFORM_HOLOLENS
	static Windows::Devices::Enumeration::DeviceInformationCollection^ AllAudioDevices = nullptr;
#endif

	void FXAudio2VoiceCallback::OnBufferEnd(void* BufferContext)
	{
		check(BufferContext);
		IAudioMixerPlatformInterface* MixerPlatform = (IAudioMixerPlatformInterface*)BufferContext;
		MixerPlatform->ReadNextBuffer();
	}

	FMixerPlatformXAudio2::FMixerPlatformXAudio2()
		: bDeviceChanged(false)
		, XAudio2System(nullptr)
		, OutputAudioStreamMasteringVoice(nullptr)
		, OutputAudioStreamSourceVoice(nullptr)
		, LastDeviceSwapTime(0.0)
		, TimeSinceNullDeviceWasLastChecked(0.0f)
		, bIsComInitialized(false)
		, bIsInitialized(false)
		, bIsDeviceOpen(false)
	{
		// Build the channel map. Index corresponds to unreal audio mixer channel enumeration.
		ChannelTypeMap.Add(SPEAKER_FRONT_LEFT);
		ChannelTypeMap.Add(SPEAKER_FRONT_RIGHT);
		ChannelTypeMap.Add(SPEAKER_FRONT_CENTER);
		ChannelTypeMap.Add(SPEAKER_LOW_FREQUENCY);
		ChannelTypeMap.Add(SPEAKER_BACK_LEFT);
		ChannelTypeMap.Add(SPEAKER_BACK_RIGHT);
		ChannelTypeMap.Add(SPEAKER_FRONT_LEFT_OF_CENTER);
		ChannelTypeMap.Add(SPEAKER_FRONT_RIGHT_OF_CENTER);
		ChannelTypeMap.Add(SPEAKER_BACK_CENTER);
		ChannelTypeMap.Add(SPEAKER_SIDE_LEFT);
		ChannelTypeMap.Add(SPEAKER_SIDE_RIGHT);
		ChannelTypeMap.Add(SPEAKER_TOP_CENTER);
		ChannelTypeMap.Add(SPEAKER_TOP_FRONT_LEFT);
		ChannelTypeMap.Add(SPEAKER_TOP_FRONT_CENTER);
		ChannelTypeMap.Add(SPEAKER_TOP_FRONT_RIGHT);
		ChannelTypeMap.Add(SPEAKER_TOP_BACK_LEFT);
		ChannelTypeMap.Add(SPEAKER_TOP_BACK_CENTER);
		ChannelTypeMap.Add(SPEAKER_TOP_BACK_RIGHT);
		ChannelTypeMap.Add(SPEAKER_RESERVED);			// Speaker type EAudioMixerChannel::Unused, 

		// Make sure the above mappings line up with our enumerations since we loop below
		check(ChannelTypeMap.Num() == EAudioMixerChannel::ChannelTypeCount);
	}

	FMixerPlatformXAudio2::~FMixerPlatformXAudio2()
	{
	}

	const TCHAR* FMixerPlatformXAudio2::GetErrorString(HRESULT Result)
	{
		switch (Result)
		{
			case HRESULT(XAUDIO2_E_INVALID_CALL):			return TEXT("XAUDIO2_E_INVALID_CALL");
			case HRESULT(XAUDIO2_E_XMA_DECODER_ERROR):		return TEXT("XAUDIO2_E_XMA_DECODER_ERROR");
			case HRESULT(XAUDIO2_E_XAPO_CREATION_FAILED):	return TEXT("XAUDIO2_E_XAPO_CREATION_FAILED");
			case HRESULT(XAUDIO2_E_DEVICE_INVALIDATED):		return TEXT("XAUDIO2_E_DEVICE_INVALIDATED");
#if PLATFORM_WINDOWS
			case REGDB_E_CLASSNOTREG:						return TEXT("REGDB_E_CLASSNOTREG");
			case CLASS_E_NOAGGREGATION:						return TEXT("CLASS_E_NOAGGREGATION");
			case E_NOINTERFACE:								return TEXT("E_NOINTERFACE");
			case E_POINTER:									return TEXT("E_POINTER");
			case E_INVALIDARG:								return TEXT("E_INVALIDARG");
			case E_OUTOFMEMORY:								return TEXT("E_OUTOFMEMORY");
#endif
			default:										return TEXT("UKNOWN");
		}
	}

	bool FMixerPlatformXAudio2::AllowDeviceSwap()
	{
		double CurrentTime = FPlatformTime::Seconds();

		// If we're already in the process of swapping, we do not want to "double-trigger" a swap
		if (bMoveAudioStreamToNewAudioDevice)
		{
			LastDeviceSwapTime = CurrentTime;
			return false;
		}

		// Some devices spam device swap notifications, so we want to rate-limit them to prevent double/triple triggering.
		static const int32 MinSwapTimeMs = 10;
		if (CurrentTime - LastDeviceSwapTime > (double)MinSwapTimeMs / 1000.0)
		{
			LastDeviceSwapTime = CurrentTime;
			return true;
		}
		return false;
	}

	bool FMixerPlatformXAudio2::ResetXAudio2System()
	{
		SAFE_RELEASE(XAudio2System);

		uint32 Flags = 0;

#if WITH_XMA2
		// We need to raise this flag explicitly to prevent initializing SHAPE twice, because we are allocating SHAPE in FXMAAudioInfo
		Flags |= XAUDIO2_DO_NOT_USE_SHAPE;
#endif

		if (FAILED(XAudio2Create(&XAudio2System, Flags, (XAUDIO2_PROCESSOR)FPlatformAffinity::GetAudioThreadMask())))
		{
			XAudio2System = nullptr;
			return false;
		}

		return true;
	}

	bool FMixerPlatformXAudio2::InitializeHardware()
	{
		if (bIsInitialized)
		{
			AUDIO_PLATFORM_ERROR(TEXT("XAudio2 already initialized."));
			return false;

		}

#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
		bIsComInitialized = FPlatformMisc::CoInitialize();
#if PLATFORM_64BITS && !PLATFORM_HOLOLENS
		// Work around the fact the x64 version of XAudio2_7.dll does not properly ref count
		// by forcing it to be always loaded

		// Load the xaudio2 library and keep a handle so we can free it on teardown
		// Note: windows internally ref-counts the library per call to load library so 
		// when we call FreeLibrary, it will only free it once the refcount is zero
		XAudio2Dll = (HMODULE)FPlatformProcess::GetDllHandle(TEXT("XAudio2_7.dll"));

		// returning null means we failed to load XAudio2, which means everything will fail
		if (XAudio2Dll == nullptr)
		{
			UE_LOG(LogInit, Warning, TEXT("Failed to load XAudio2 dll"));
			FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("Audio", "XAudio2Missing", "XAudio2.7 is not installed. Make sure you have XAudio 2.7 installed. XAudio 2.7 is available in the DirectX End-User Runtime (June 2010)."));
			return false;
		}
#endif // #if PLATFORM_64BITS && !PLATFORM_HOLOLENS
#endif // #if PLATFORM_WINDOWS || PLATFORM_HOLOLENS

		uint32 Flags = 0;

#if WITH_XMA2
		// We need to raise this flag explicitly to prevent initializing SHAPE twice, because we are allocating SHAPE in FXMAAudioInfo
		Flags |= XAUDIO2_DO_NOT_USE_SHAPE;
#endif

		if (!XAudio2System && FAILED(XAudio2Create(&XAudio2System, Flags, (XAUDIO2_PROCESSOR)FPlatformAffinity::GetAudioThreadMask())))
		{
			FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("Audio", "XAudio2Error", "Failed to initialize audio. This may be an issue with your installation of XAudio 2.7. XAudio2 is available in the DirectX End-User Runtime (June 2010)."));
			return false;
		}

#if PLATFORM_HOLOLENS
		using namespace Windows::Foundation;
		using namespace Windows::Devices::Enumeration;
		IAsyncOperation<DeviceInformationCollection^>^ EnumerationOp = DeviceInformation::FindAllAsync(DeviceClass::AudioRender);
		while (EnumerationOp->Status == AsyncStatus::Started)
		{
			// Spin
		}

		if (EnumerationOp->Status == AsyncStatus::Completed)
		{
			AllAudioDevices = EnumerationOp->GetResults();
		}
#endif

#if WITH_XMA2
		//Initialize our XMA2 decoder context
		FXMAAudioInfo::Initialize();
#endif //#if WITH_XMA2
		// Load ogg and vorbis dlls if they haven't been loaded yet
		LoadVorbisLibraries();

		bIsInitialized = true;

		return true;
	}

	bool FMixerPlatformXAudio2::TeardownHardware()
	{
		if (!bIsInitialized)
		{
			AUDIO_PLATFORM_ERROR(TEXT("XAudio2 was already tore down."));
			return false;
		}

		SAFE_RELEASE(XAudio2System);

#if WITH_XMA2
		FXMAAudioInfo::Shutdown();
#endif

#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS

#if PLATFORM_64BITS && !PLATFORM_HOLOLENS
		if (XAudio2Dll != nullptr && GIsRequestingExit)
		{
			if (!FreeLibrary(XAudio2Dll))
			{
				UE_LOG(LogAudio, Warning, TEXT("Failed to free XAudio2 Dll"));
			}

			XAudio2Dll = nullptr;
		}
#endif

		if (bIsComInitialized)
		{
			FPlatformMisc::CoUninitialize();
		}
#endif



		bIsInitialized = false;

		return true;
	}

	bool FMixerPlatformXAudio2::IsInitialized() const
	{
		return bIsInitialized;
	}

	bool FMixerPlatformXAudio2::GetNumOutputDevices(uint32& OutNumOutputDevices)
	{
		if (!bIsInitialized)
		{
			AUDIO_PLATFORM_ERROR(TEXT("XAudio2 was not initialized."));
			return false;
		}

		// XAudio2 for HoloLens doesn't have GetDeviceCount, use Windows::Devices::Enumeration instead
		// See https://blogs.msdn.microsoft.com/chuckw/2012/04/02/xaudio2-and-windows-8/
#if PLATFORM_HOLOLENS
		if (!AllAudioDevices)
		{
			return false;
		}
		OutNumOutputDevices = AllAudioDevices->Size;
#elif PLATFORM_WINDOWS

		check(XAudio2System);
		XAUDIO2_RETURN_ON_FAIL(XAudio2System->GetDeviceCount(&OutNumOutputDevices));
#else
		OutNumOutputDevices = 1;
#endif
		return true;
	}

	bool FMixerPlatformXAudio2::GetOutputDeviceInfo(const uint32 InDeviceIndex, FAudioPlatformDeviceInfo& OutInfo)
	{
		if (!bIsInitialized)
		{
			AUDIO_PLATFORM_ERROR(TEXT("XAudio2 was not initialized."));
			return false;
		}

		// XAudio2 for HoloLens doesn't have GetDeviceDetails, use Windows::Devices::Enumeration instead
		// See https://blogs.msdn.microsoft.com/chuckw/2012/04/02/xaudio2-and-windows-8/
#if PLATFORM_HOLOLENS
		if (!AllAudioDevices)
		{
			return false;
		}

		Windows::Devices::Enumeration::DeviceInformation^ WindowsDeviceInfo = AllAudioDevices->GetAt(InDeviceIndex);
		OutInfo.Name = WindowsDeviceInfo->Name->Data();
		OutInfo.bIsSystemDefault = WindowsDeviceInfo->IsDefault;

		// No direct equivalent of OutputFormat.  If we have a voice already we can assemble what
		// we need from methods on that.  But what to do if we don't?
		WAVEFORMATEXTENSIBLE FakeWaveFormatExtensible;
		XAUDIO2_VOICE_DETAILS VoiceDetails;
		if (OutputAudioStreamMasteringVoice && InDeviceIndex == AudioStreamInfo.OutputDeviceIndex)
		{
			OutputAudioStreamMasteringVoice->GetVoiceDetails(&VoiceDetails);
			OutputAudioStreamMasteringVoice->GetChannelMask(&FakeWaveFormatExtensible.dwChannelMask);
		}
		else if (!OutputAudioStreamMasteringVoice)
		{
			// If we don't yet have a mastering voice we can create a temporary one with asking for the default channels
			// and sample rate, and then query that to discover the device's preferred format. 
			IXAudio2MasteringVoice* TempMasteringVoice;
			if (SUCCEEDED(XAudio2System->CreateMasteringVoice(&TempMasteringVoice, XAUDIO2_DEFAULT_CHANNELS, XAUDIO2_DEFAULT_SAMPLERATE, 0, AllAudioDevices->GetAt(InDeviceIndex)->Id->Data(), nullptr)))
			{
				TempMasteringVoice->GetVoiceDetails(&VoiceDetails);
				TempMasteringVoice->GetChannelMask(&FakeWaveFormatExtensible.dwChannelMask);
				TempMasteringVoice->DestroyVoice();
			}
			else
			{
				return false;
			}
		}
		else
		{
			// Can't create multiple mastering voices, so currently we can't report
			// device preferred format in this scenario.
			return false;
		}

		WAVEFORMATEX& WaveFormatEx = *(WAVEFORMATEX*)&FakeWaveFormatExtensible;
		WaveFormatEx.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
		WaveFormatEx.nSamplesPerSec = VoiceDetails.InputSampleRate;
		WaveFormatEx.nChannels = VoiceDetails.InputChannels;

#elif PLATFORM_WINDOWS

		check(XAudio2System);

		XAUDIO2_DEVICE_DETAILS DeviceDetails;
		XAUDIO2_RETURN_ON_FAIL(XAudio2System->GetDeviceDetails(InDeviceIndex, &DeviceDetails));

		OutInfo.Name = FString(DeviceDetails.DisplayName);
		OutInfo.DeviceId = FString(DeviceDetails.DeviceID);
		OutInfo.bIsSystemDefault = (InDeviceIndex == 0);

		// Get the wave format to parse there rest of the device details
		const WAVEFORMATEX& WaveFormatEx = DeviceDetails.OutputFormat.Format;
#endif
#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
		OutInfo.SampleRate = WaveFormatEx.nSamplesPerSec;

		OutInfo.NumChannels = FMath::Clamp((int32)WaveFormatEx.nChannels, 2, 8);

		// XAudio2 automatically converts the audio format to output device us so we don't need to do any format conversions
		OutInfo.Format = EAudioMixerStreamDataFormat::Float;

		OutInfo.OutputChannelArray.Reset();

		// Extensible format supports surround sound so we need to parse the channel configuration to build our channel output array
		if (WaveFormatEx.wFormatTag == WAVE_FORMAT_EXTENSIBLE)
		{
			// Cast to the extensible format to get access to extensible data
			const WAVEFORMATEXTENSIBLE* WaveFormatExtensible = (WAVEFORMATEXTENSIBLE*)&WaveFormatEx;

			// Loop through the extensible format channel flags in the standard order and build our output channel array
			// From https://msdn.microsoft.com/en-us/library/windows/hardware/dn653308(v=vs.85).aspx
			// The channels in the interleaved stream corresponding to these spatial positions must appear in the order specified above. This holds true even in the 
			// case of a non-contiguous subset of channels. For example, if a stream contains left, bass enhance and right, then channel 1 is left, channel 2 is right, 
			// and channel 3 is bass enhance. This enables the linkage of multi-channel streams to well-defined multi-speaker configurations.

			check(EAudioMixerChannel::ChannelTypeCount == ChannelTypeMap.Num());
			uint32 ChanCount = 0;
			for (uint32 ChannelTypeIndex = 0; ChannelTypeIndex < EAudioMixerChannel::ChannelTypeCount && ChanCount < (uint32)OutInfo.NumChannels; ++ChannelTypeIndex)
			{
				if (WaveFormatExtensible->dwChannelMask & ChannelTypeMap[ChannelTypeIndex])
				{
					OutInfo.OutputChannelArray.Add((EAudioMixerChannel::Type)ChannelTypeIndex);
					++ChanCount;
				}
			}

			// We didnt match channel masks for all channels, revert to a default ordering
			if (ChanCount < (uint32)OutInfo.NumChannels)
			{
				UE_LOG(LogAudioMixer, Warning, TEXT("Did not find the channel type flags for audio device '%s'. Reverting to a default channel ordering."), *OutInfo.Name);

				OutInfo.OutputChannelArray.Reset();

				static EAudioMixerChannel::Type DefaultChannelOrdering[] = {
					EAudioMixerChannel::FrontLeft,
					EAudioMixerChannel::FrontRight,
					EAudioMixerChannel::FrontCenter,
					EAudioMixerChannel::LowFrequency,
					EAudioMixerChannel::SideLeft,
					EAudioMixerChannel::SideRight,
					EAudioMixerChannel::BackLeft,
					EAudioMixerChannel::BackRight,
				};

				EAudioMixerChannel::Type* ChannelOrdering = DefaultChannelOrdering;

				// Override channel ordering for some special cases
				if (OutInfo.NumChannels == 4)
				{
					static EAudioMixerChannel::Type DefaultChannelOrderingQuad[] = {
						EAudioMixerChannel::FrontLeft,
						EAudioMixerChannel::FrontRight,
						EAudioMixerChannel::BackLeft,
						EAudioMixerChannel::BackRight,
					};

					ChannelOrdering = DefaultChannelOrderingQuad;
				}
				else if (OutInfo.NumChannels == 6)
				{
					static EAudioMixerChannel::Type DefaultChannelOrdering51[] = {
						EAudioMixerChannel::FrontLeft,
						EAudioMixerChannel::FrontRight,
						EAudioMixerChannel::FrontCenter,
						EAudioMixerChannel::LowFrequency,
						EAudioMixerChannel::BackLeft,
						EAudioMixerChannel::BackRight,
					};

					ChannelOrdering = DefaultChannelOrdering51;
				}

				check(OutInfo.NumChannels <= 8);
				for (int32 Index = 0; Index < OutInfo.NumChannels; ++Index)
				{
					OutInfo.OutputChannelArray.Add(ChannelOrdering[Index]);
				}

			}
		}
		else
		{
			// Non-extensible formats only support mono or stereo channel output
			OutInfo.OutputChannelArray.Add(EAudioMixerChannel::FrontLeft);
			if (OutInfo.NumChannels == 2)
			{
				OutInfo.OutputChannelArray.Add(EAudioMixerChannel::FrontRight);
			}
		}

		UE_LOG(LogAudioMixer, Display, TEXT("Audio Device Output Speaker Info:"));
		UE_LOG(LogAudioMixer, Display, TEXT("Name: %s"), *OutInfo.Name);
		UE_LOG(LogAudioMixer, Display, TEXT("Is Default: %s"), OutInfo.bIsSystemDefault ? TEXT("Yes") : TEXT("No"));
		UE_LOG(LogAudioMixer, Display, TEXT("Sample Rate: %d"), OutInfo.SampleRate);
		UE_LOG(LogAudioMixer, Display, TEXT("Channel Count Used: %d"), OutInfo.NumChannels);
		UE_LOG(LogAudioMixer, Display, TEXT("Device Channel Count: %d"), WaveFormatEx.nChannels);
		UE_LOG(LogAudioMixer, Display, TEXT("Channel Order:"));
		for (int32 i = 0; i < OutInfo.NumChannels; ++i)
		{
			if (i < OutInfo.OutputChannelArray.Num())
			{
				UE_LOG(LogAudioMixer, Display, TEXT("%d: %s"), i, EAudioMixerChannel::ToString(OutInfo.OutputChannelArray[i]));
			}
		}
#else // #if PLATFORM_WINDOWS

		OutInfo.bIsSystemDefault = true;
		OutInfo.SampleRate = 44100;
		OutInfo.DeviceId = 0;
		OutInfo.Format = EAudioMixerStreamDataFormat::Float;
		OutInfo.Name = TEXT("XboxOne Audio Device.");
		OutInfo.NumChannels = 8;

		OutInfo.OutputChannelArray.Add(EAudioMixerChannel::FrontLeft);
		OutInfo.OutputChannelArray.Add(EAudioMixerChannel::FrontRight);
		OutInfo.OutputChannelArray.Add(EAudioMixerChannel::FrontCenter);
		OutInfo.OutputChannelArray.Add(EAudioMixerChannel::LowFrequency);
		OutInfo.OutputChannelArray.Add(EAudioMixerChannel::BackLeft);
		OutInfo.OutputChannelArray.Add(EAudioMixerChannel::BackRight);
		OutInfo.OutputChannelArray.Add(EAudioMixerChannel::SideLeft);
		OutInfo.OutputChannelArray.Add(EAudioMixerChannel::SideRight);
#endif // #else // #if PLATFORM_WINDOWS

		return true;
	}

	bool FMixerPlatformXAudio2::GetDefaultOutputDeviceIndex(uint32& OutDefaultDeviceIndex) const
	{
		OutDefaultDeviceIndex = 0;
		return true;
	}

	bool FMixerPlatformXAudio2::OpenAudioStream(const FAudioMixerOpenStreamParams& Params)
	{
		if (!bIsInitialized)
		{
			AUDIO_PLATFORM_ERROR(TEXT("XAudio2 was not initialized."));
			return false;
		}

		if (bIsDeviceOpen)
		{
			AUDIO_PLATFORM_ERROR(TEXT("XAudio2 audio stream already opened."));
			return false;
		}

		check(XAudio2System);
		check(OutputAudioStreamMasteringVoice == nullptr);

		WAVEFORMATEX Format = { 0 };

		OpenStreamParams = Params;

#if !PLATFORM_HOLOLENS
		// On windows, default device index is 0
		if (Params.OutputDeviceIndex == AUDIO_MIXER_DEFAULT_DEVICE_INDEX)
		{
			OpenStreamParams.OutputDeviceIndex = 0;
		}
#endif
		AudioStreamInfo.Reset();

		AudioStreamInfo.OutputDeviceIndex = OpenStreamParams.OutputDeviceIndex;
		AudioStreamInfo.NumOutputFrames = OpenStreamParams.NumFrames;
		AudioStreamInfo.NumBuffers = OpenStreamParams.NumBuffers;
		AudioStreamInfo.AudioMixer = OpenStreamParams.AudioMixer;

		uint32 NumOutputDevices;
		HRESULT Result = ERROR_SUCCESS;

		if (GetNumOutputDevices(NumOutputDevices) && NumOutputDevices > 0)
		{
#if PLATFORM_HOLOLENS
			// On windows, default device index is 0
			// But if that device cannot be configured try to find one that can be.  This happens in the hololens emulator.
			if (AudioStreamInfo.OutputDeviceIndex == AUDIO_MIXER_DEFAULT_DEVICE_INDEX)
			{
				bool bFoundUsefulDevice = false;
				for (uint32 i = 0; i < NumOutputDevices; ++i)
				{
					OpenStreamParams.OutputDeviceIndex = i;
					AudioStreamInfo.OutputDeviceIndex = OpenStreamParams.OutputDeviceIndex;
					if (GetOutputDeviceInfo(AudioStreamInfo.OutputDeviceIndex, AudioStreamInfo.DeviceInfo))
					{
						bFoundUsefulDevice = true;
						break;
					}
				}
				if (!bFoundUsefulDevice)
				{
					return false;
				}
			}
			else
			{
				if (!GetOutputDeviceInfo(AudioStreamInfo.OutputDeviceIndex, AudioStreamInfo.DeviceInfo))
				{
					return false;
				}
			}
#else
			if (!GetOutputDeviceInfo(AudioStreamInfo.OutputDeviceIndex, AudioStreamInfo.DeviceInfo))
			{
				return false;
			}
#endif


			// Store the device ID here in case it is removed. We can switch back if the device comes back.
			if (Params.bRestoreIfRemoved)
			{
				OriginalAudioDeviceId = AudioStreamInfo.DeviceInfo.DeviceId;
			}

#if PLATFORM_WINDOWS
			Result = XAudio2System->CreateMasteringVoice(&OutputAudioStreamMasteringVoice, AudioStreamInfo.DeviceInfo.NumChannels, AudioStreamInfo.DeviceInfo.SampleRate, 0, AudioStreamInfo.OutputDeviceIndex, nullptr);
#elif PLATFORM_XBOXONE
			Result = XAudio2System->CreateMasteringVoice(&OutputAudioStreamMasteringVoice, AudioStreamInfo.DeviceInfo.NumChannels, AudioStreamInfo.DeviceInfo.SampleRate, 0, nullptr, nullptr);
#elif PLATFORM_HOLOLENS
		// XAudio2 for HoloLens has different parameters to CreateMasteringVoice
		// See https://blogs.msdn.microsoft.com/chuckw/2012/04/02/xaudio2-and-windows-8/
		Result = XAudio2System->CreateMasteringVoice(&OutputAudioStreamMasteringVoice, AudioStreamInfo.DeviceInfo.NumChannels, AudioStreamInfo.DeviceInfo.SampleRate, 0, AllAudioDevices->GetAt(AudioStreamInfo.OutputDeviceIndex)->Id->Data(), nullptr);
#endif // #if PLATFORM_WINDOWS

			XAUDIO2_CLEANUP_ON_FAIL(Result);

			// Start the xaudio2 engine running, which will now allow us to start feeding audio to it
			XAudio2System->StartEngine();

			// Setup the format of the output source voice
			Format.nChannels = AudioStreamInfo.DeviceInfo.NumChannels;
			Format.nSamplesPerSec = Params.SampleRate;
			Format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
			Format.nAvgBytesPerSec = Format.nSamplesPerSec * sizeof(float) * Format.nChannels;
			Format.nBlockAlign = sizeof(float) * Format.nChannels;
			Format.wBitsPerSample = sizeof(float) * 8;

			// Create the output source voice
			Result = XAudio2System->CreateSourceVoice(&OutputAudioStreamSourceVoice, &Format, XAUDIO2_VOICE_NOPITCH, 2.0f, &OutputVoiceCallback);
			XAUDIO2_RETURN_ON_FAIL(Result);


		}
		else
		{
			check(!bIsUsingNullDevice);

			AudioStreamInfo.NumOutputFrames = OpenStreamParams.NumFrames;
			AudioStreamInfo.DeviceInfo.OutputChannelArray = { EAudioMixerChannel::FrontLeft, EAudioMixerChannel::FrontRight };
			AudioStreamInfo.DeviceInfo.NumChannels = 2;
			AudioStreamInfo.DeviceInfo.SampleRate = OpenStreamParams.SampleRate;
			AudioStreamInfo.DeviceInfo.Format = EAudioMixerStreamDataFormat::Float;
		}

		AudioStreamInfo.StreamState = EAudioOutputStreamState::Open;
		bIsDeviceOpen = true;

	Cleanup:
		if (FAILED(Result))
		{
			CloseAudioStream();
		}
		return SUCCEEDED(Result);
	}

	FAudioPlatformDeviceInfo FMixerPlatformXAudio2::GetPlatformDeviceInfo() const
	{
		return AudioStreamInfo.DeviceInfo;
	}

	bool FMixerPlatformXAudio2::CloseAudioStream()
	{
		if (!bIsInitialized || AudioStreamInfo.StreamState == EAudioOutputStreamState::Closed)
		{
			return false;
		}

		if (bIsDeviceOpen && !StopAudioStream())
		{
			return false;
		}

		check(XAudio2System);
		XAudio2System->StopEngine();

		if (OutputAudioStreamSourceVoice)
		{
			OutputAudioStreamSourceVoice->DestroyVoice();
			OutputAudioStreamSourceVoice = nullptr;
		}

		check(OutputAudioStreamMasteringVoice || bIsUsingNullDevice);
		if (OutputAudioStreamMasteringVoice)
		{
			OutputAudioStreamMasteringVoice->DestroyVoice();
			OutputAudioStreamMasteringVoice = nullptr;
		}
		else
		{
			StopRunningNullDevice();
		}

		bIsDeviceOpen = false;

		AudioStreamInfo.StreamState = EAudioOutputStreamState::Closed;

		return true;
	}

	bool FMixerPlatformXAudio2::StartAudioStream()
	{
		// Start generating audio with our output source voice
		BeginGeneratingAudio();

		// If we already have a source voice, we can just restart it
		if (OutputAudioStreamSourceVoice)
		{
			AudioStreamInfo.StreamState = EAudioOutputStreamState::Running;
			OutputAudioStreamSourceVoice->Start();
			return true;
		}
		else
		{
			check(!bIsUsingNullDevice);
			StartRunningNullDevice();
			return true;
		}

		return false;
	}

	bool FMixerPlatformXAudio2::StopAudioStream()
	{
		if (!bIsInitialized)
		{
			AUDIO_PLATFORM_ERROR(TEXT("XAudio2 was not initialized."));
			return false;
		}

		check(XAudio2System);

		if (AudioStreamInfo.StreamState != EAudioOutputStreamState::Stopped && AudioStreamInfo.StreamState != EAudioOutputStreamState::Closed)
		{
			if (AudioStreamInfo.StreamState == EAudioOutputStreamState::Running)
			{
				StopGeneratingAudio();
			}

			// Signal that the thread that is running the update that we're stopping
			if (OutputAudioStreamSourceVoice)
			{
				FScopeLock ScopeLock(&DeviceSwapCriticalSection);
				OutputAudioStreamSourceVoice->DestroyVoice();
				OutputAudioStreamSourceVoice = nullptr;
			}

			check(AudioStreamInfo.StreamState == EAudioOutputStreamState::Stopped);
		}

		return true;
	}

	bool FMixerPlatformXAudio2::CheckAudioDeviceChange()
	{
		FScopeLock Lock(&AudioDeviceSwapCriticalSection);

		if (bMoveAudioStreamToNewAudioDevice)
		{
			bMoveAudioStreamToNewAudioDevice = false;

			return MoveAudioStreamToNewAudioDevice(NewAudioDeviceId);
		}
		return false;
	}

	bool FMixerPlatformXAudio2::MoveAudioStreamToNewAudioDevice(const FString& InNewDeviceId)
	{
#if PLATFORM_WINDOWS

		uint32 NumDevices = 0;
		// XAudio2 for HoloLens doesn't have GetDeviceCount, use local wrapper instead
		if (!GetNumOutputDevices(NumDevices))
		{
			return false;
		}

		// If we're running the null device, This function is called every second or so.
		// Because of this, we early exit from this function if we're running the null device
		// and there still are no devices.
		if (bIsUsingNullDevice && !NumDevices)
		{
			return true;
		}

		UE_LOG(LogTemp, Log, TEXT("Resetting audio stream to device id %s"), *InNewDeviceId);

		if (bIsUsingNullDevice)
		{
			StopRunningNullDevice();
		}
		else
		{
			// Not initialized!
			if (!bIsInitialized)
			{
				return true;
			}

			// If an XAudio2 callback is in flight,
			// we have to wait for it here.
			FScopeLock ScopeLock(&DeviceSwapCriticalSection);

			// Now that we've properly locked, raise the bIsInDeviceSwap flag
			// in case FlushSourceBuffers() calls OnBufferEnd on this thread,
			// and DeviceSwapCriticalSection.TryLock() is still returning true
			bIsInDeviceSwap = true;

			// Flush all buffers. Because we've locked DeviceSwapCriticalSection, ReadNextBuffer will early exit and we will not submit any additional buffers.
			if (OutputAudioStreamSourceVoice)
			{
				OutputAudioStreamSourceVoice->FlushSourceBuffers();
			}

			if (OutputAudioStreamSourceVoice)
			{
				// Then destroy the current audio stream source voice
				OutputAudioStreamSourceVoice->DestroyVoice();
				OutputAudioStreamSourceVoice = nullptr;
			}

			// Now destroy the mastering voice
			if (OutputAudioStreamMasteringVoice)
			{
				OutputAudioStreamMasteringVoice->DestroyVoice();
				OutputAudioStreamMasteringVoice = nullptr;
			}

			bIsInDeviceSwap = false;
		}

		if (NumDevices > 0)
		{
			if (!ResetXAudio2System())
			{
				// Reinitializing the XAudio2System failed, so we have to exit here.
				StartRunningNullDevice();
				return true;
			}

			// Now get info on the new audio device we're trying to reset to
			uint32 DeviceIndex = 0;
			if (!InNewDeviceId.IsEmpty())
			{
				// XAudio2 for HoloLens doesn't have GetDeviceDetails, use local wrapper instead
				FAudioPlatformDeviceInfo DeviceDetails;
				for (uint32 i = 0; i < NumDevices; ++i)
				{
					GetOutputDeviceInfo(i, DeviceDetails);
					if (DeviceDetails.DeviceId == InNewDeviceId)
					{
						DeviceIndex = i;
						break;
					}
				}
			}

			// Update the audio stream info to the new device info
			AudioStreamInfo.OutputDeviceIndex = DeviceIndex;
			// Get the output device info at this new index
			GetOutputDeviceInfo(AudioStreamInfo.OutputDeviceIndex, AudioStreamInfo.DeviceInfo);

			// Create a new master voice
			// XAudio2 for HoloLens has different parameters to CreateMasteringVoice
			// See https://blogs.msdn.microsoft.com/chuckw/2012/04/02/xaudio2-and-windows-8/
#if PLATFORM_HOLOLENS
			XAUDIO2_RETURN_ON_FAIL(XAudio2System->CreateMasteringVoice(&OutputAudioStreamMasteringVoice, AudioStreamInfo.DeviceInfo.NumChannels, AudioStreamInfo.DeviceInfo.SampleRate, 0, AllAudioDevices->GetAt(AudioStreamInfo.OutputDeviceIndex)->Id->Data(), nullptr));
#else
			XAUDIO2_RETURN_ON_FAIL(XAudio2System->CreateMasteringVoice(&OutputAudioStreamMasteringVoice, AudioStreamInfo.DeviceInfo.NumChannels, AudioStreamInfo.DeviceInfo.SampleRate, 0, AudioStreamInfo.OutputDeviceIndex, nullptr));
#endif

			// Setup the format of the output source voice
			WAVEFORMATEX Format = { 0 };
			Format.nChannels = AudioStreamInfo.DeviceInfo.NumChannels;
			Format.nSamplesPerSec = OpenStreamParams.SampleRate;
			Format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
			Format.nAvgBytesPerSec = Format.nSamplesPerSec * sizeof(float) * Format.nChannels;
			Format.nBlockAlign = sizeof(float) * Format.nChannels;
			Format.wBitsPerSample = sizeof(float) * 8;

			// Create the output source voice
			XAUDIO2_RETURN_ON_FAIL(XAudio2System->CreateSourceVoice(&OutputAudioStreamSourceVoice, &Format, XAUDIO2_VOICE_NOPITCH, 2.0f, &OutputVoiceCallback));

			const int32 NewNumSamples = OpenStreamParams.NumFrames * AudioStreamInfo.DeviceInfo.NumChannels;

			// Clear the output buffers with zero's and submit one
			for (int32 Index = 0; Index < OutputBuffers.Num(); ++Index)
			{
				OutputBuffers[Index].Reset(NewNumSamples);
			}
		}
		else
		{	
			// If we don't have any hardware playback devices available, use the null device callback to render buffers.
			StartRunningNullDevice();
		}

#endif // #if PLATFORM_WINDOWS

		return true;
	}

	void FMixerPlatformXAudio2::ResumePlaybackOnNewDevice()
	{
		if (OutputAudioStreamSourceVoice)
		{
			CurrentBufferReadIndex = 0;
			CurrentBufferWriteIndex = 1;

			SubmitBuffer(OutputBuffers[CurrentBufferReadIndex].GetBufferData());
			check(OpenStreamParams.NumFrames * AudioStreamInfo.DeviceInfo.NumChannels == OutputBuffers[CurrentBufferReadIndex].GetBuffer().Num());

			AudioRenderEvent->Trigger();

			// Start the voice streaming
			OutputAudioStreamSourceVoice->Start();
		}
	}

	void FMixerPlatformXAudio2::SubmitBuffer(const uint8* Buffer)
	{
		if (OutputAudioStreamSourceVoice)
		{
			// Create a new xaudio2 buffer submission
			XAUDIO2_BUFFER XAudio2Buffer = { 0 };
			XAudio2Buffer.AudioBytes = OpenStreamParams.NumFrames * AudioStreamInfo.DeviceInfo.NumChannels * sizeof(float);
			XAudio2Buffer.pAudioData = (const BYTE*)Buffer;
			XAudio2Buffer.pContext = this;

			// Submit buffer to the output streaming voice
			OutputAudioStreamSourceVoice->SubmitSourceBuffer(&XAudio2Buffer);
		}
	}

	FName FMixerPlatformXAudio2::GetRuntimeFormat(USoundWave* InSoundWave)
	{
		static FName NAME_OGG(TEXT("OGG"));
		static FName NAME_OPUS(TEXT("OPUS"));
		static FName NAME_XMA(TEXT("XMA"));
		static FName NAME_ADPCM(TEXT("ADPCM"));

		if (InSoundWave->IsStreaming())
		{
			if (InSoundWave->IsSeekableStreaming())
			{
				return NAME_ADPCM;
			}

#if WITH_XMA2 && USE_XMA2_FOR_STREAMING
			if (InSoundWave->NumChannels <= 2)
			{
				return NAME_XMA;
			}
#endif

#if USE_VORBIS_FOR_STREAMING
			return NAME_OGG;
#endif
		}

#if WITH_XMA2
		if (InSoundWave->NumChannels <= 2)
		{
			return NAME_XMA;
		}
#endif //#if WITH_XMA2

		return NAME_OGG;
	}

	bool FMixerPlatformXAudio2::HasCompressedAudioInfoClass(USoundWave* InSoundWave)
	{
		return true;
	}

	ICompressedAudioInfo* FMixerPlatformXAudio2::CreateCompressedAudioInfo(USoundWave* InSoundWave)
	{
		check(InSoundWave);

		if (InSoundWave->IsStreaming())
		{
			if (InSoundWave->IsSeekableStreaming())
			{
				return new FADPCMAudioInfo();
			}
		}

#if WITH_XMA2 && USE_XMA2_FOR_STREAMING
		if (InSoundWave->IsStreaming() && InSoundWave->NumChannels <= 2 )
		{
			return new FXMAAudioInfo();
		}
#endif

		if (InSoundWave->IsStreaming())
		{
#if USE_VORBIS_FOR_STREAMING
			return new FVorbisAudioInfo();
#else
			return new FOpusAudioInfo();
#endif
		}

		static const FName NAME_OGG(TEXT("OGG"));
		if (FPlatformProperties::RequiresCookedData() ? InSoundWave->HasCompressedData(NAME_OGG) : (InSoundWave->GetCompressedData(NAME_OGG) != nullptr))
		{
			return new FVorbisAudioInfo();
		}

#if WITH_XMA2
		static const FName NAME_XMA(TEXT("XMA"));
		if (FPlatformProperties::RequiresCookedData() ? InSoundWave->HasCompressedData(NAME_XMA) : (InSoundWave->GetCompressedData(NAME_XMA) != nullptr))
		{
			return new FXMAAudioInfo();
		}
#endif

		return nullptr;
	}

	FString FMixerPlatformXAudio2::GetDefaultDeviceName()
	{
		//GConfig->GetString(TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings"), TEXT("AudioDevice"), WindowsAudioDeviceName, GEngineIni);
		return FString();
	}

	FAudioPlatformSettings FMixerPlatformXAudio2::GetPlatformSettings() const
	{
		return FAudioPlatformSettings::GetPlatformSettings(TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings"));
	}

	void FMixerPlatformXAudio2::OnHardwareUpdate()
	{
		if (bIsUsingNullDevice)
		{
			float CurrentTime = FPlatformTime::Seconds();
			if (CurrentTime - TimeSinceNullDeviceWasLastChecked > 1.0f)
			{
				bMoveAudioStreamToNewAudioDevice = true;
				TimeSinceNullDeviceWasLastChecked = CurrentTime;
			}
		}
	}

	bool FMixerPlatformXAudio2::DisablePCMAudioCaching() const
	{
#if PLATFORM_WINDOWS
		return false;
#else
		return true;
#endif
	}
}
