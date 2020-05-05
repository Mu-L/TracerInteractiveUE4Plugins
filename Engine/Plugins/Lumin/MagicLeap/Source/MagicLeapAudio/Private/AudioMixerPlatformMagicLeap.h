// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioMixer.h"
#include "Lumin/CAPIShims/LuminAPI.h"
#include "FakeDeviceCallbackRunnable.h"
#include "Lumin/CAPIShims/LuminAPIAudio.h"

// Any platform defines
namespace Audio
{

	class FMixerPlatformMagicLeap : public IAudioMixerPlatformInterface
	{

	public:

		FMixerPlatformMagicLeap();
		~FMixerPlatformMagicLeap();

		//~ Begin IAudioMixerPlatformInterface
		virtual EAudioMixerPlatformApi::Type GetPlatformApi() const override { return EAudioMixerPlatformApi::Null; }
		virtual bool InitializeHardware() override;
		virtual bool TeardownHardware() override;
		virtual bool IsInitialized() const override;
		virtual bool GetNumOutputDevices(uint32& OutNumOutputDevices) override;
		virtual bool GetOutputDeviceInfo(const uint32 InDeviceIndex, FAudioPlatformDeviceInfo& OutInfo) override;
		virtual bool GetDefaultOutputDeviceIndex(uint32& OutDefaultDeviceIndex) const override;
		virtual bool OpenAudioStream(const FAudioMixerOpenStreamParams& Params) override;
		virtual bool CloseAudioStream() override;
		virtual bool StartAudioStream() override;
		virtual bool StopAudioStream() override;
		virtual FAudioPlatformDeviceInfo GetPlatformDeviceInfo() const override;
		virtual void SubmitBuffer(const uint8* Buffer) override;
		virtual FName GetRuntimeFormat(USoundWave* InSoundWave) override;
		virtual bool HasCompressedAudioInfoClass(USoundWave* InSoundWave) override;
		virtual ICompressedAudioInfo* CreateCompressedAudioInfo(USoundWave* InSoundWave) override;
		virtual FString GetDefaultDeviceName() override;
		virtual FAudioPlatformSettings GetPlatformSettings() const override;
		virtual void SuspendContext() override;
		virtual void ResumeContext() override;
		virtual void OnHardwareUpdate() override;

		//~ End IAudioMixerPlatformInterface

		uint8* CachedBufferHandle;


		virtual int32 GetNumFrames(const int32 InNumReqestedFrames) override;

		void DeviceStandby();
		void DevicePausedStandby();
		void DeviceActive();

	private:
#if WITH_MLSDK
		static const TCHAR* GetErrorString(MLResult Result);
#endif //WITH_MLSDK

		bool bSuspended;
		bool bInitialized;
		bool bInCallback;

		FCriticalSection CallbackCriticalSection;
		FCriticalSection SuspendedCriticalSection;

#if WITH_MLSDK
		MLHandle StreamHandle;
		// Static callback used for MLAudio:
		static void MLAudioCallback(MLHandle Handle, void* CallbackContext);
		static void MLAudioEventImplCallback(MLHandle Handle, MLAudioEvent Event, void* CallbackContext);
#endif //WITH_MLSDK
	};

}
