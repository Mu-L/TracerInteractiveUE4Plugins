// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioMixer.h"

THIRD_PARTY_INCLUDES_START
#include "SDL.h"
#include "SDL_audio.h"
THIRD_PARTY_INCLUDES_END

namespace Audio
{

	class FMixerPlatformSDL : public IAudioMixerPlatformInterface
	{

	public:

		FMixerPlatformSDL();
		virtual ~FMixerPlatformSDL();

		//~ Begin IAudioMixerPlatformInterface Interface
		EAudioMixerPlatformApi::Type GetPlatformApi() const override { return EAudioMixerPlatformApi::SDL2; }
		bool InitializeHardware() override;
		bool TeardownHardware() override;
		bool IsInitialized() const override;
		bool GetNumOutputDevices(uint32& OutNumOutputDevices) override;
		bool GetOutputDeviceInfo(const uint32 InDeviceIndex, FAudioPlatformDeviceInfo& OutInfo) override;
		bool GetDefaultOutputDeviceIndex(uint32& OutDefaultDeviceIndex) const override;
		virtual bool OpenAudioStream(const FAudioMixerOpenStreamParams& Params) override;
		bool CloseAudioStream() override;
		bool StartAudioStream() override;
		bool StopAudioStream() override;
		FAudioPlatformDeviceInfo GetPlatformDeviceInfo() const override;
		void SubmitBuffer(const uint8* Buffer) override;
		FName GetRuntimeFormat(USoundWave* InSoundWave) override;
		bool HasCompressedAudioInfoClass(USoundWave* InSoundWave) override;
		ICompressedAudioInfo* CreateCompressedAudioInfo(USoundWave* InSoundWave) override;
		FString GetDefaultDeviceName() override;
		FAudioPlatformSettings GetPlatformSettings() const override;
		void ResumeContext() override;
		void SuspendContext() override;
		//~ End IAudioMixerPlatformInterface Interface

		void HandleOnBufferEnd(uint8* InOutputBuffer, int32 InOutputBufferLength);

		virtual FString GetCurrentDeviceName() const;

	protected:
		FCriticalSection OutputBufferMutex;

	private:

		SDL_AudioDeviceID AudioDeviceID;
		SDL_AudioSpec AudioSpecPrefered;
		SDL_AudioSpec AudioSpecReceived;

		uint8* OutputBuffer;
		int32 OutputBufferByteLength;

		bool bSuspended;
		bool bInitialized;
	};

}

