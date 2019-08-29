//
// Copyright (C) Valve Corporation. All rights reserved.
//

#include "PhononPluginManager.h"
#include "SteamAudioModule.h"
#include "AudioDevice.h"

namespace SteamAudio
{
	FPhononPluginManager::FPhononPluginManager()
		: bEnvironmentCreated(false)
		, ReverbPtr(nullptr)
		, OcclusionPtr(nullptr)
	{
	}

	FPhononPluginManager::~FPhononPluginManager()
	{
		// Perform cleanup here instead of in OnListenerShutdown, because plugins will still be active and may be using them
		if (bEnvironmentCreated)
		{
			Environment.Shutdown();
			bEnvironmentCreated = false;
		}
	}

	void FPhononPluginManager::OnListenerInitialize(FAudioDevice* AudioDevice, UWorld* ListenerWorld)
	{
		if (ListenerWorld->WorldType == EWorldType::Editor)
		{
			return;
		}

		if (Environment.Initialize(ListenerWorld, AudioDevice))
		{
			if (IsUsingSteamAudioPlugin(EAudioPlugin::REVERB))
			{
				ReverbPtr = static_cast<FPhononReverb*>(AudioDevice->ReverbPluginInterface.Get());
				ReverbPtr->SetEnvironment(&Environment);
				ReverbPtr->CreateReverbEffect();
			}

			if (IsUsingSteamAudioPlugin(EAudioPlugin::OCCLUSION))
			{
				OcclusionPtr = static_cast<FPhononOcclusion*>(AudioDevice->OcclusionInterface.Get());
				OcclusionPtr->SetEnvironment(&Environment);
			}

			bEnvironmentCreated = true;
		}
	}

	void FPhononPluginManager::OnListenerUpdated(FAudioDevice* AudioDevice, const int32 ViewportIndex, const FTransform& ListenerTransform, const float InDeltaSeconds)
	{
		if (!bEnvironmentCreated)
		{
			return;
		}

		FVector Position = ListenerTransform.GetLocation();
		FVector Forward = ListenerTransform.GetUnitAxis(EAxis::Y);
		FVector Up = ListenerTransform.GetUnitAxis(EAxis::Z);

		if (OcclusionPtr)
		{
			OcclusionPtr->UpdateDirectSoundSources(Position, Forward, Up);
		}

		if (ReverbPtr)
		{
			ReverbPtr->UpdateListener(Position, Forward, Up);
		}
	}

	void FPhononPluginManager::OnListenerShutdown(FAudioDevice* AudioDevice)
	{
		FSteamAudioModule* Module = &FModuleManager::GetModuleChecked<FSteamAudioModule>("SteamAudio");
		if (Module != nullptr)
		{
			Module->UnregisterAudioDevice(AudioDevice);
		}
	}

	bool FPhononPluginManager::IsUsingSteamAudioPlugin(EAudioPlugin PluginType)
	{
		FSteamAudioModule* Module = &FModuleManager::GetModuleChecked<FSteamAudioModule>("SteamAudio");

		// If we can't get the module from the module manager, then we don't have any of these plugins loaded.
		if (Module == nullptr)
		{
			return false;
		}

		FString SteamPluginName = Module->GetPluginFactory(PluginType)->GetDisplayName();
		FString CurrentPluginName = AudioPluginUtilities::GetDesiredPluginName(PluginType, AudioPluginUtilities::CurrentPlatform);
		return CurrentPluginName.Equals(SteamPluginName);
	}
}
