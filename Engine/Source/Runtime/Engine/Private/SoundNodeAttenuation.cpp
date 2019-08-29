// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.


#include "Sound/SoundNodeAttenuation.h"
#include "EngineDefines.h"
#include "ActiveSound.h"
#include "AudioDevice.h"

/*-----------------------------------------------------------------------------
	USoundNodeAttenuation implementation.
-----------------------------------------------------------------------------*/

USoundNodeAttenuation::USoundNodeAttenuation(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

float USoundNodeAttenuation::GetMaxDistance() const 
{ 
	const FSoundAttenuationSettings* Settings = GetAttenuationSettingsToApply();
	if (Settings)
	{
		return Settings->GetMaxDimension();
	}
	// if we have a sound node atten but no setting or value, treat as no attenuation value (i.e. max distance)
	return WORLD_MAX;
}

const FSoundAttenuationSettings* USoundNodeAttenuation::GetAttenuationSettingsToApply() const
{
	const FSoundAttenuationSettings* Settings = nullptr;

	if (bOverrideAttenuation)
	{
		Settings = &AttenuationOverrides;
	}
	else if (AttenuationSettings)
	{
		Settings = &AttenuationSettings->Attenuation;
	}

	return Settings;
}

void USoundNodeAttenuation::ParseNodes( FAudioDevice* AudioDevice, const UPTRINT NodeWaveInstanceHash, FActiveSound& ActiveSound, const FSoundParseParameters& ParseParams, TArray<FWaveInstance*>& WaveInstances )
{
	const FSoundAttenuationSettings* Settings = (ActiveSound.bAllowSpatialization ? GetAttenuationSettingsToApply() : NULL);

	FSoundParseParameters UpdatedParseParams = ParseParams;
	if (Settings)
	{
		const FListener& Listener = AudioDevice->GetListeners()[0];

		// Update this node's attenuation settings overrides
		ActiveSound.ApplyAttenuation(UpdatedParseParams, Listener, Settings);
	}
	else
	{
		UpdatedParseParams.bUseSpatialization = false;
	}

	Super::ParseNodes( AudioDevice, NodeWaveInstanceHash, ActiveSound, UpdatedParseParams, WaveInstances );
}
