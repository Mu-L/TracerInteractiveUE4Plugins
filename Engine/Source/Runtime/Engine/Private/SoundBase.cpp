// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "Sound/SoundBase.h"
#include "Sound/SoundSubmix.h"
#include "Sound/AudioSettings.h"
#include "EngineDefines.h"

USoundClass* USoundBase::DefaultSoundClassObject = nullptr;
USoundConcurrency* USoundBase::DefaultSoundConcurrencyObject = nullptr;

USoundBase::USoundBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bIgnoreFocus_DEPRECATED(false)
	, Priority(1.0f)
{
#if WITH_EDITORONLY_DATA
	MaxConcurrentPlayCount_DEPRECATED = 16;
#endif
}

void USoundBase::PostInitProperties()
{
	Super::PostInitProperties();

	if (USoundBase::DefaultSoundClassObject == nullptr)
	{
		const FSoftObjectPath DefaultSoundClassName = GetDefault<UAudioSettings>()->DefaultSoundClassName;
		if (DefaultSoundClassName.IsValid())
		{
			USoundBase::DefaultSoundClassObject = LoadObject<USoundClass>(nullptr, *DefaultSoundClassName.ToString());
		}
	}
	SoundClassObject = USoundBase::DefaultSoundClassObject;

	if (USoundBase::DefaultSoundConcurrencyObject == nullptr)
	{
		const FSoftObjectPath DefaultSoundConcurrencyName = GetDefault<UAudioSettings>()->DefaultSoundConcurrencyName;
		if (DefaultSoundConcurrencyName.IsValid())
		{
			USoundBase::DefaultSoundConcurrencyObject = LoadObject<USoundConcurrency>(nullptr, *DefaultSoundConcurrencyName.ToString());
		}
	}
	SoundConcurrencySettings = USoundBase::DefaultSoundConcurrencyObject;
}

bool USoundBase::IsPlayable() const
{
	return false;
}

bool USoundBase::IsAllowedVirtual() const
{ 
	return false; 
}

bool USoundBase::HasAttenuationNode() const
{
	return false;
}

const FSoundAttenuationSettings* USoundBase::GetAttenuationSettingsToApply() const
{
	if (AttenuationSettings)
	{
		return &AttenuationSettings->Attenuation;
	}
	return nullptr;
}

float USoundBase::GetMaxDistance() const
{
	return (AttenuationSettings ? AttenuationSettings->Attenuation.GetMaxDimension() : WORLD_MAX);
}

float USoundBase::GetDuration()
{
	return Duration;
}

bool USoundBase::HasDelayNode() const
{
	return bHasDelayNode;
}

bool USoundBase::HasConcatenatorNode() const
{
	return bHasConcatenatorNode;
}

bool USoundBase::IsVirtualizeWhenSilent() const
{
	return bHasVirtualizeWhenSilent;
}

float USoundBase::GetVolumeMultiplier()
{
	return 1.f;
}

float USoundBase::GetPitchMultiplier()
{
	return 1.f;
}

bool USoundBase::IsLooping()
{ 
	return (GetDuration() >= INDEFINITELY_LOOPING_DURATION); 
}

bool USoundBase::ShouldApplyInteriorVolumes()
{
	return (SoundClassObject && SoundClassObject->Properties.bApplyAmbientVolumes);
}

USoundClass* USoundBase::GetSoundClass() const
{
	return SoundClassObject;
}

USoundSubmix* USoundBase::GetSoundSubmix() const
{
	return SoundSubmixObject;
}

void USoundBase::GetSoundSubmixSends(TArray<FSoundSubmixSendInfo>& OutSends) const
{
	OutSends = SoundSubmixSends;
}

void USoundBase::GetSoundSourceBusSends(EBusSendType BusSendType, TArray<FSoundSourceBusSendInfo>& OutSends) const
{
	if (BusSendType == EBusSendType::PreEffect)
	{
		OutSends = PreEffectBusSends;
	}
	else
	{
		OutSends = BusSends;
	}
}

const FSoundConcurrencySettings* USoundBase::GetSoundConcurrencySettingsToApply()
{
	if (bOverrideConcurrency)
	{
		return &ConcurrencyOverrides;
	}
	else if (SoundConcurrencySettings)
	{
		return &SoundConcurrencySettings->Concurrency;
	}
	return nullptr;
}

float USoundBase::GetPriority() const
{
	return FMath::Clamp(Priority, MIN_SOUND_PRIORITY, MAX_SOUND_PRIORITY);
}

uint32 USoundBase::GetSoundConcurrencyObjectID() const
{
	if (SoundConcurrencySettings != nullptr && !bOverrideConcurrency)
	{
		return SoundConcurrencySettings->GetUniqueID();
	}
	return 0;
}

void USoundBase::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	const int32 LinkerUE4Version = GetLinkerUE4Version();

	if (LinkerUE4Version < VER_UE4_SOUND_CONCURRENCY_PACKAGE)
	{
		bOverrideConcurrency = true;
		ConcurrencyOverrides.bLimitToOwner = false;
		ConcurrencyOverrides.MaxCount = FMath::Max(MaxConcurrentPlayCount_DEPRECATED, 1);
		ConcurrencyOverrides.ResolutionRule = MaxConcurrentResolutionRule_DEPRECATED;
		ConcurrencyOverrides.VolumeScale = 1.0f;
	}
#endif
}

