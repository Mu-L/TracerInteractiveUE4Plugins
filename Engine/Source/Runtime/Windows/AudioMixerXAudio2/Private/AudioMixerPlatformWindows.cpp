// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/**
	Concrete implementation of FAudioDevice for XAudio2

	See https://msdn.microsoft.com/en-us/library/windows/desktop/hh405049%28v=vs.85%29.aspx
*/

#include "AudioMixerPlatformXAudio2.h"
#include "AudioMixer.h"
#include "AudioMixerDevice.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/AllowWindowsPlatformAtomics.h"

#define INITGUID
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>

class FWindowsMMNotificationClient final : public IMMNotificationClient
{
public:
	FWindowsMMNotificationClient()
		: Ref(1)
		, DeviceEnumerator(nullptr)
	{
		bComInitialized = FWindowsPlatformMisc::CoInitialize();
		HRESULT Result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (void**)&DeviceEnumerator);
		if (Result == S_OK)
		{
			DeviceEnumerator->RegisterEndpointNotificationCallback(this);
		}
	}

	virtual ~FWindowsMMNotificationClient()
	{
		if (DeviceEnumerator)
		{
			DeviceEnumerator->UnregisterEndpointNotificationCallback(this);
			SAFE_RELEASE(DeviceEnumerator);
		}

		if (bComInitialized)
		{
			FWindowsPlatformMisc::CoUninitialize();
		}
	}

	HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow InFlow, ERole InRole, LPCWSTR pwstrDeviceId) override
	{
		if (Audio::IAudioMixer::ShouldLogDeviceSwaps())
		{
			UE_LOG(LogAudioMixer, Warning, TEXT("OnDefaultDeviceChanged: %d, %d, %s"), InFlow, InRole, pwstrDeviceId);
		}

		Audio::EAudioDeviceRole AudioDeviceRole;

		if (Audio::IAudioMixer::ShouldIgnoreDeviceSwaps())
		{
			return S_OK;
		}

		if (InRole == eConsole)
		{
			AudioDeviceRole = Audio::EAudioDeviceRole::Console;
		}
		else if (InRole == eMultimedia)
		{
			AudioDeviceRole = Audio::EAudioDeviceRole::Multimedia;
		}
		else
		{
			AudioDeviceRole = Audio::EAudioDeviceRole::Communications;
		}

		if (InFlow == eRender)
		{
			for (Audio::IAudioMixerDeviceChangedLister* Listener : Listeners)
			{
				Listener->OnDefaultRenderDeviceChanged(AudioDeviceRole, FString(pwstrDeviceId));
			}
		}
		else if (InFlow == eCapture)
		{
			for (Audio::IAudioMixerDeviceChangedLister* Listener : Listeners)
			{
				Listener->OnDefaultCaptureDeviceChanged(AudioDeviceRole, FString(pwstrDeviceId));
			}
		}
		else
		{
			for (Audio::IAudioMixerDeviceChangedLister* Listener : Listeners)
			{
				Listener->OnDefaultCaptureDeviceChanged(AudioDeviceRole, FString(pwstrDeviceId));
				Listener->OnDefaultRenderDeviceChanged(AudioDeviceRole, FString(pwstrDeviceId));
			}
		}


		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId) override
	{
		if (Audio::IAudioMixer::ShouldLogDeviceSwaps())
		{
			UE_LOG(LogAudioMixer, Warning, TEXT("OnDeviceAdded: %s"), pwstrDeviceId);
		}
		
		if (Audio::IAudioMixer::ShouldIgnoreDeviceSwaps())
		{
			return S_OK;
		}

		for (Audio::IAudioMixerDeviceChangedLister* Listener : Listeners)
		{
			Listener->OnDeviceAdded(FString(pwstrDeviceId));
		}
		return S_OK;
	};

	HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId) override
	{
		if (Audio::IAudioMixer::ShouldLogDeviceSwaps())
		{
			UE_LOG(LogAudioMixer, Warning, TEXT("OnDeviceRemoved: %s"), pwstrDeviceId);
		}

		if (Audio::IAudioMixer::ShouldIgnoreDeviceSwaps())
		{
			return S_OK;
		}

		for (Audio::IAudioMixerDeviceChangedLister* Listener : Listeners)
		{
			Listener->OnDeviceRemoved(FString(pwstrDeviceId));
		}
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) override
	{
		if (Audio::IAudioMixer::ShouldLogDeviceSwaps())
		{
			UE_LOG(LogAudioMixer, Warning, TEXT("OnDeviceStateChanged: %s, %d"), pwstrDeviceId, dwNewState);
		}

		if (Audio::IAudioMixer::ShouldIgnoreDeviceSwaps())
		{
			return S_OK;
		}

		if (dwNewState == DEVICE_STATE_DISABLED || dwNewState == DEVICE_STATE_UNPLUGGED || dwNewState == DEVICE_STATE_NOTPRESENT)
		{
			for (Audio::IAudioMixerDeviceChangedLister* Listener : Listeners)
			{
				switch (dwNewState)
				{
				case DEVICE_STATE_DISABLED:
					Listener->OnDeviceStateChanged(FString(pwstrDeviceId), Audio::EAudioDeviceState::Disabled);
					break;

				case DEVICE_STATE_UNPLUGGED:
					Listener->OnDeviceStateChanged(FString(pwstrDeviceId), Audio::EAudioDeviceState::Unplugged);
					break;
				case DEVICE_STATE_NOTPRESENT:
					Listener->OnDeviceStateChanged(FString(pwstrDeviceId), Audio::EAudioDeviceState::NotPresent);
					break;
				default:
					break;
				}
			}
		}
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key)
	{
		if (Audio::IAudioMixer::ShouldLogDeviceSwaps())
		{
			UE_LOG(LogAudioMixer, Warning, TEXT("OnPropertyValueChanged: %s, %d"), pwstrDeviceId, key.pid);
		}

		if (Audio::IAudioMixer::ShouldIgnoreDeviceSwaps())
		{
			return S_OK;
		}

		FString ChangedId = FString(pwstrDeviceId);

		// look for ids we care about!
		if (key.fmtid == PKEY_AudioEndpoint_PhysicalSpeakers.fmtid || 
			key.fmtid == PKEY_AudioEngine_DeviceFormat.fmtid ||
			key.fmtid == PKEY_AudioEngine_OEMFormat.fmtid)
		{
			for (Audio::IAudioMixerDeviceChangedLister* Listener : Listeners)
			{
				Listener->OnDeviceRemoved(ChangedId);
			}
		}

		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(const IID &, void **) override
	{
		return S_OK;
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return InterlockedIncrement(&Ref);
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		ULONG ulRef = InterlockedDecrement(&Ref);
		if (0 == ulRef)
		{
			delete this;
		}
		return ulRef;
	}

	void RegisterDeviceChangedListener(Audio::IAudioMixerDeviceChangedLister* DeviceChangedListener)
	{
		Listeners.Add(DeviceChangedListener);
	}

	void UnRegisterDeviceDeviceChangedListener(Audio::IAudioMixerDeviceChangedLister* DeviceChangedListener)
	{
		Listeners.Remove(DeviceChangedListener);
	}

private:
	LONG Ref;
	TSet<Audio::IAudioMixerDeviceChangedLister*> Listeners;
	IMMDeviceEnumerator* DeviceEnumerator;
	bool bComInitialized;
};




namespace Audio
{
	TSharedPtr<FWindowsMMNotificationClient> WindowsNotificationClient;

	void FMixerPlatformXAudio2::RegisterDeviceChangedListener()
	{
		if (!WindowsNotificationClient.IsValid())
		{
			WindowsNotificationClient = TSharedPtr<FWindowsMMNotificationClient>(new FWindowsMMNotificationClient);
		}

		WindowsNotificationClient->RegisterDeviceChangedListener(this);
	}

	void FMixerPlatformXAudio2::UnregisterDeviceChangedListener() 
	{
		if (WindowsNotificationClient.IsValid())
		{
			WindowsNotificationClient->UnRegisterDeviceDeviceChangedListener(this);
		}
	}

	void FMixerPlatformXAudio2::OnDefaultCaptureDeviceChanged(const EAudioDeviceRole InAudioDeviceRole, const FString& DeviceId)
	{
	}

	void FMixerPlatformXAudio2::OnDefaultRenderDeviceChanged(const EAudioDeviceRole InAudioDeviceRole, const FString& DeviceId)
	{
		if (!AllowDeviceSwap())
		{
			return;
		}

		if (AudioDeviceSwapCriticalSection.TryLock())
		{
			UE_LOG(LogAudioMixer, Warning, TEXT("Changing default audio render device to new device: %s."), *DeviceId);

			NewAudioDeviceId = "";
			bMoveAudioStreamToNewAudioDevice = true;

			AudioDeviceSwapCriticalSection.Unlock();
		}

	}

	void FMixerPlatformXAudio2::OnDeviceAdded(const FString& DeviceId)
	{
		if (AudioDeviceSwapCriticalSection.TryLock())
		{
			// If the device that was added is our original device and our current device is NOT our original device, 
			// move our audio stream to this newly added device.
			if (AudioStreamInfo.DeviceInfo.DeviceId != OriginalAudioDeviceId && DeviceId == OriginalAudioDeviceId)
			{
				UE_LOG(LogAudioMixer, Warning, TEXT("Original audio device re-added. Moving audio back to original audio device %s."), *OriginalAudioDeviceId);

				NewAudioDeviceId = OriginalAudioDeviceId;
				bMoveAudioStreamToNewAudioDevice = true;
			}

			AudioDeviceSwapCriticalSection.Unlock();
		}
	}

	void FMixerPlatformXAudio2::OnDeviceRemoved(const FString& DeviceId)
	{
		if (AudioDeviceSwapCriticalSection.TryLock())
		{
			// If the device we're currently using was removed... then switch to the new default audio device.
			if (AudioStreamInfo.DeviceInfo.DeviceId == DeviceId)
			{
				UE_LOG(LogAudioMixer, Warning, TEXT("Audio device removed, falling back to other windows default device."));

				NewAudioDeviceId = "";
				bMoveAudioStreamToNewAudioDevice = true;
			}
			AudioDeviceSwapCriticalSection.Unlock();
		}
	}

	void FMixerPlatformXAudio2::OnDeviceStateChanged(const FString& DeviceId, const EAudioDeviceState InState)
	{
	}

	FString FMixerPlatformXAudio2::GetDeviceId() const
	{
		return AudioStreamInfo.DeviceInfo.DeviceId;
	}
}

#include "Windows/HideWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformTypes.h"

#else 
// Nothing for XBOXOne
namespace Audio
{
	void FMixerPlatformXAudio2::RegisterDeviceChangedListener() {}
	void FMixerPlatformXAudio2::UnregisterDeviceChangedListener() {}
	void FMixerPlatformXAudio2::OnDefaultCaptureDeviceChanged(const EAudioDeviceRole InAudioDeviceRole, const FString& DeviceId) {}
	void FMixerPlatformXAudio2::OnDefaultRenderDeviceChanged(const EAudioDeviceRole InAudioDeviceRole, const FString& DeviceId) {}
	void FMixerPlatformXAudio2::OnDeviceAdded(const FString& DeviceId) {}
	void FMixerPlatformXAudio2::OnDeviceRemoved(const FString& DeviceId) {}
	void FMixerPlatformXAudio2::OnDeviceStateChanged(const FString& DeviceId, const EAudioDeviceState InState){}
	FString FMixerPlatformXAudio2::GetDeviceId() const
	{
		return AudioStreamInfo.DeviceInfo.DeviceId;
	}
}
#endif
