// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Audio.cpp: Unreal base audio.
=============================================================================*/

#include "Audio.h"
#include "ActiveSound.h"
#include "AnalyticsEventAttribute.h"
#include "Audio/AudioDebug.h"
#include "AudioDevice.h"
#include "AudioPluginUtilities.h"
#include "AudioThread.h"
#include "Components/AudioComponent.h"
#include "Components/SynthComponent.h"
#include "ContentStreaming.h"
#include "DrawDebugHelpers.h"
#include "EngineAnalytics.h"
#include "Interfaces/IAnalyticsProvider.h"
#include "Misc/Paths.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "Sound/SoundWave.h"
#include "Sound/QuartzQuantizationUtilities.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY(LogAudio);

DEFINE_LOG_CATEGORY(LogAudioDebug);

/** Audio stats */

DEFINE_STAT(STAT_AudioMemorySize);
DEFINE_STAT(STAT_ActiveSounds);
DEFINE_STAT(STAT_AudioSources);
DEFINE_STAT(STAT_AudioVirtualLoops);
DEFINE_STAT(STAT_WaveInstances);
DEFINE_STAT(STAT_WavesDroppedDueToPriority);
DEFINE_STAT(STAT_AudioMaxChannels);
DEFINE_STAT(STAT_AudioMaxStoppingSources);
DEFINE_STAT(STAT_AudibleWavesDroppedDueToPriority);
DEFINE_STAT(STAT_AudioFinishedDelegatesCalled);
DEFINE_STAT(STAT_AudioFinishedDelegates);
DEFINE_STAT(STAT_AudioBufferTime);
DEFINE_STAT(STAT_AudioBufferTimeChannels);

DEFINE_STAT(STAT_VorbisDecompressTime);
DEFINE_STAT(STAT_VorbisPrepareDecompressionTime);
DEFINE_STAT(STAT_AudioDecompressTime);
DEFINE_STAT(STAT_AudioPrepareDecompressionTime);
DEFINE_STAT(STAT_AudioStreamedDecompressTime);

DEFINE_STAT(STAT_AudioUpdateEffects);
DEFINE_STAT(STAT_AudioEvaluateConcurrency);
DEFINE_STAT(STAT_AudioUpdateSources);
DEFINE_STAT(STAT_AudioResourceCreationTime);
DEFINE_STAT(STAT_AudioSourceInitTime);
DEFINE_STAT(STAT_AudioSourceCreateTime);
DEFINE_STAT(STAT_AudioSubmitBuffersTime);
DEFINE_STAT(STAT_AudioStartSources);
DEFINE_STAT(STAT_AudioGatherWaveInstances);
DEFINE_STAT(STAT_AudioFindNearestLocation);

/** CVars */
static int32 DisableStereoSpreadCvar = 0;
FAutoConsoleVariableRef CVarDisableStereoSpread(
	TEXT("au.DisableStereoSpread"),
	DisableStereoSpreadCvar,
	TEXT("When set to 1, ignores the 3D Stereo Spread property in attenuation settings and instead renders audio from a singular point.\n")
	TEXT("0: Not Disabled, 1: Disabled"),
	ECVF_Default);

static int32 AllowAudioSpatializationCVar = 1;
FAutoConsoleVariableRef CVarAllowAudioSpatializationCVar(
	TEXT("au.AllowAudioSpatialization"),
	AllowAudioSpatializationCVar,
	TEXT("Controls if we allow spatialization of audio, normally this is enabled.  If disabled all audio won't be spatialized, but will have attenuation.\n")
	TEXT("0: Disable, >0: Enable"),
	ECVF_Default);

static int32 OcclusionFilterScaleEnabledCVar = 0;
FAutoConsoleVariableRef CVarOcclusionFilterScaleEnabled(
	TEXT("au.EnableOcclusionFilterScale"),
	OcclusionFilterScaleEnabledCVar,
	TEXT("Whether or not we scale occlusion by 0.25f to compensate for change in filter cutoff frequencies in audio mixer. \n")
	TEXT("0: Not Enabled, 1: Enabled"),
	ECVF_Default);

static int32 BypassPlayWhenSilentCVar = 0;
FAutoConsoleVariableRef CVarBypassPlayWhenSilent(
	TEXT("au.BypassPlayWhenSilent"),
	BypassPlayWhenSilentCVar,
	TEXT("When set to 1, ignores the Play When Silent flag for non-procedural sources.\n")
	TEXT("0: Honor the Play When Silent flag, 1: stop all silent non-procedural sources."),
	ECVF_Default);

static int32 AllowReverbForMultichannelSources = 1;
FAutoConsoleVariableRef CvarAllowReverbForMultichannelSources(
	TEXT("au.AllowReverbForMultichannelSources"),
	AllowReverbForMultichannelSources,
	TEXT("Controls if we allow Reverb processing for sources with channel counts > 2.\n")
	TEXT("0: Disable, >0: Enable"),
	ECVF_Default);


bool IsAudioPluginEnabled(EAudioPlugin PluginType)
{
	switch (PluginType)
	{
	case EAudioPlugin::SPATIALIZATION:
		return AudioPluginUtilities::GetDesiredSpatializationPlugin() != nullptr;
	case EAudioPlugin::REVERB:
		return AudioPluginUtilities::GetDesiredReverbPlugin() != nullptr;
	case EAudioPlugin::OCCLUSION:
		return AudioPluginUtilities::GetDesiredOcclusionPlugin() != nullptr;
	case EAudioPlugin::MODULATION:
		return AudioPluginUtilities::GetDesiredModulationPlugin() != nullptr;
	default:
		return false;
		break;
	}
}

UClass* GetAudioPluginCustomSettingsClass(EAudioPlugin PluginType)
{
	switch (PluginType)
	{
		case EAudioPlugin::SPATIALIZATION:
		{
			if (IAudioSpatializationFactory* Factory = AudioPluginUtilities::GetDesiredSpatializationPlugin())
			{
				return Factory->GetCustomSpatializationSettingsClass();
			}
		}
		break;

		case EAudioPlugin::REVERB:
		{
			if (IAudioReverbFactory* Factory = AudioPluginUtilities::GetDesiredReverbPlugin())
			{
				return Factory->GetCustomReverbSettingsClass();
			}
		}
		break;

		case EAudioPlugin::OCCLUSION:
		{
			if (IAudioOcclusionFactory* Factory = AudioPluginUtilities::GetDesiredOcclusionPlugin())
			{
				return Factory->GetCustomOcclusionSettingsClass();
			}
		}
		break;

		case EAudioPlugin::MODULATION:
		{
			return nullptr;
		}
		break;

		default:
			static_assert(static_cast<uint32>(EAudioPlugin::COUNT) == 4, "Possible missing audio plugin type case coverage");
		break;
	}

	return nullptr;
}

bool IsSpatializationCVarEnabled()
{
	return AllowAudioSpatializationCVar != 0;
}

/*-----------------------------------------------------------------------------
	FSoundBuffer implementation.
-----------------------------------------------------------------------------*/

FSoundBuffer::~FSoundBuffer()
{
	// remove ourselves from the set of waves that are tracked by the audio device
	if (ResourceID && GEngine && GEngine->GetAudioDeviceManager())
	{
		GEngine->GetAudioDeviceManager()->RemoveSoundBufferForResourceID(ResourceID);
	}
}

/**
 * This will return the name of the SoundClass of the Sound that this buffer(SoundWave) belongs to.
 * NOTE: This will find the first cue in the ObjectIterator list.  So if we are using SoundWaves in multiple
 * places we will pick up the first one only.
 **/
FName FSoundBuffer::GetSoundClassName()
{
	// need to look in all cues
	for (TObjectIterator<USoundBase> It; It; ++It)
	{
		USoundCue* Cue = Cast<USoundCue>(*It);
		if (Cue)
		{
			// get all the waves this cue uses
			TArray<USoundNodeWavePlayer*> WavePlayers;
			Cue->RecursiveFindNode<USoundNodeWavePlayer>(Cue->FirstNode, WavePlayers);

			// look through them to see if this cue uses a wave this buffer is bound to, via ResourceID
			for (int32 WaveIndex = 0; WaveIndex < WavePlayers.Num(); ++WaveIndex)
			{
				USoundWave* WaveNode = WavePlayers[WaveIndex]->GetSoundWave();
				if (WaveNode != NULL)
				{
					if (WaveNode->ResourceID == ResourceID)
					{
						if (Cue->GetSoundClass())
						{
							return Cue->GetSoundClass()->GetFName();
						}
						else
						{
							return NAME_None;
						}
					}
				}
			}
		}
		else
		{
			USoundWave* Wave = Cast<USoundWave>(*It);
			if (Wave && Wave->ResourceID == ResourceID)
			{
				if (Wave->GetSoundClass())
				{
					return Wave->GetSoundClass()->GetFName();
				}
				else
				{
					return NAME_None;
				}
			}
		}
	}

	return NAME_None;
}

FString FSoundBuffer::GetChannelsDesc()
{
	switch (NumChannels)
	{
		case 1:
			return FString("Mono");
		case 2:
			return FString("Stereo");
		case 6:
			return FString("5.1");
		case 8:
			return FString("7.1");
		default:
			return FString::Printf(TEXT("%d Channels"), NumChannels);
	}
}

FString FSoundBuffer::Describe(bool bUseLongName)
{
	// format info string
	const FName SoundClassName = GetSoundClassName();
	FString AllocationString = bAllocationInPermanentPool ? TEXT("Permanent, ") : TEXT("");
	FString ChannelsDesc = GetChannelsDesc();
	FString SoundName = bUseLongName ? ResourceName : FPaths::GetExtension(ResourceName);

	return FString::Printf(TEXT("%8.2fkb, %s%s, '%s', Class: %s"), GetSize() / 1024.0f, *AllocationString, *ChannelsDesc, *ResourceName, *SoundClassName.ToString());
}

/*-----------------------------------------------------------------------------
	FSoundSource implementation.
-----------------------------------------------------------------------------*/

FString FSoundSource::Describe(bool bUseLongName)
{
	return FString::Printf(TEXT("Wave: %s, Volume: %6.2f, Owner: %s"),
		bUseLongName ? *WaveInstance->WaveData->GetPathName() : *WaveInstance->WaveData->GetName(),
		WaveInstance->GetVolume(),
		WaveInstance->ActiveSound ? *WaveInstance->ActiveSound->GetOwnerName() : TEXT("None"));
}

void FSoundSource::Stop()
{
	if (WaveInstance)
	{
		// The sound is stopping, so set the envelope value to 0.0f
		WaveInstance->SetEnvelopeValue(0.0f);
		NotifyPlaybackData();

		check(AudioDevice);
		AudioDevice->WaveInstanceSourceMap.Remove(WaveInstance);
		WaveInstance->NotifyFinished(true);
		WaveInstance = nullptr;
	}

	// Remove this source from free list regardless of if this had a wave instance created
	AudioDevice->FreeSources.AddUnique(this);
}

void FSoundSource::SetPauseByGame(bool bInIsPauseByGame)
{
	bIsPausedByGame = bInIsPauseByGame;
	UpdatePause();
}

void FSoundSource::SetPauseManually(bool bInIsPauseManually)
{
	bIsManuallyPaused = bInIsPauseManually;
	UpdatePause();
}

void FSoundSource::UpdatePause()
{
	if (IsPaused() && !bIsPausedByGame && !bIsManuallyPaused)
	{
		Play();
	}
	else if (!IsPaused() && (bIsManuallyPaused || bIsPausedByGame))
	{
		Pause();
	}
}

bool FSoundSource::IsGameOnly() const
{
	return (WaveInstance && !WaveInstance->bIsUISound);
}

bool FSoundSource::SetReverbApplied(bool bHardwareAvailable)
{
	// Do not apply reverb if it is explicitly disallowed
	bReverbApplied = WaveInstance->bReverb && bHardwareAvailable;

	// Do not apply reverb to music
	if (WaveInstance->bIsMusic)
	{
		bReverbApplied = false;
	}

	// Do not apply reverb to multichannel sounds
	if (!AllowReverbForMultichannelSources && (WaveInstance->WaveData->NumChannels > 2))
	{
		bReverbApplied = false;
	}

	return(bReverbApplied);
}

float FSoundSource::SetStereoBleed()
{
	return 0.f;
}

float FSoundSource::SetLFEBleed()
{
	LFEBleed = WaveInstance->LFEBleed;

	if (AudioDevice->GetMixDebugState() == DEBUGSTATE_TestLFEBleed)
	{
		LFEBleed = 10.0f;
	}

	return LFEBleed;
}

void FSoundSource::SetFilterFrequency()
{
	// HPF is only available with audio mixer enabled
	switch (AudioDevice->GetMixDebugState())
	{
		case DEBUGSTATE_TestLPF:
		{
			LPFFrequency = MIN_FILTER_FREQUENCY;
		}
		break;

		case DEBUGSTATE_DisableLPF:
		{
			LPFFrequency = MAX_FILTER_FREQUENCY;
		}
		break;

		default:
		{
			// compensate for filter coefficient calculation error for occlusion
			float OcclusionFilterScale = 1.0f;
			if (AudioDevice->IsAudioMixerEnabled() && OcclusionFilterScaleEnabledCVar == 1 && !FMath::IsNearlyEqual(WaveInstance->OcclusionFilterFrequency, MAX_FILTER_FREQUENCY))
			{
				OcclusionFilterScale = 0.25f;
			}

			// Set the LPFFrequency to lowest provided value
			LPFFrequency = FMath::Min(WaveInstance->OcclusionFilterFrequency * OcclusionFilterScale, WaveInstance->LowPassFilterFrequency);
			LPFFrequency = FMath::Min(LPFFrequency, WaveInstance->AmbientZoneFilterFrequency);
			LPFFrequency = FMath::Min(LPFFrequency, WaveInstance->AttenuationLowpassFilterFrequency);
			LPFFrequency = FMath::Min(LPFFrequency, WaveInstance->SoundClassFilterFrequency);
		}
		break;
	}

	// HPF is only available with audio mixer enabled
	switch (AudioDevice->GetMixDebugState())
	{
		case DEBUGSTATE_TestHPF:
		{
			HPFFrequency = MAX_FILTER_FREQUENCY;
		}
		break;

		case DEBUGSTATE_DisableHPF:
		{
			HPFFrequency = MIN_FILTER_FREQUENCY;
		}
		break;

		default:
		{
			// Set the HPFFrequency to highest provided value
			HPFFrequency = WaveInstance->AttenuationHighpassFilterFrequency;
		}
		break;
	}
}

void FSoundSource::UpdateStereoEmitterPositions()
{
	// Only call this function if we're told to use spatialization
	check(WaveInstance->GetUseSpatialization());
	check(Buffer->NumChannels == 2);

	if (!DisableStereoSpreadCvar && WaveInstance->StereoSpread > 0.0f)
	{
		// We need to compute the stereo left/right channel positions using the audio component position and the spread
		FVector ListenerPosition;

		const bool bAllowAttenuationOverride = false;
		const int32 ListenerIndex = WaveInstance->ActiveSound ? WaveInstance->ActiveSound->GetClosestListenerIndex() : 0;
		AudioDevice->GetListenerPosition(ListenerIndex, ListenerPosition, bAllowAttenuationOverride);
		FVector ListenerToSourceDir = (WaveInstance->Location - ListenerPosition).GetSafeNormal();

		float HalfSpread = 0.5f * WaveInstance->StereoSpread;

		// Get direction of left emitter from true emitter position (left hand rule)
		FVector LeftEmitterDir = FVector::CrossProduct(ListenerToSourceDir, FVector::UpVector);
		FVector LeftEmitterOffset = LeftEmitterDir * HalfSpread;

		// Get position vector of left emitter by adding to true emitter the dir scaled by half the spread
		LeftChannelSourceLocation = WaveInstance->Location + LeftEmitterOffset;

		// Right emitter position is same as right but opposite direction
		RightChannelSourceLocation = WaveInstance->Location - LeftEmitterOffset;
	}
	else
	{
		LeftChannelSourceLocation = WaveInstance->Location;
		RightChannelSourceLocation = WaveInstance->Location;
	}
}

float FSoundSource::GetDebugVolume(const float InVolume)
{
	float OutVolume = InVolume;

#if ENABLE_AUDIO_DEBUG

	// Bail if we don't have a device manager.
	if (!GEngine || !GEngine->GetAudioDeviceManager() || !WaveInstance || !DebugInfo.IsValid() )
	{
		return OutVolume;
	}

	// Solos/Mutes (dev only).
	Audio::FAudioDebugger& Debugger = GEngine->GetAudioDeviceManager()->GetDebugger();	
	FDebugInfo Info;
				
	// SoundWave Solo/Mutes.
	if (OutVolume != 0.0f)
	{
		Debugger.QuerySoloMuteSoundWave(WaveInstance->GetName(), Info.bIsSoloed, Info.bIsMuted, Info.MuteSoloReason);
		if (Info.bIsMuted)
		{
			OutVolume = 0.0f;
		}
	}

	// SoundCues mutes/solos (not strictly just cues but any SoundBase)
	if (OutVolume != 0.0f && WaveInstance->ActiveSound)
	{						
		if (USoundBase* ActiveSound= WaveInstance->ActiveSound->GetSound())
		{
			Debugger.QuerySoloMuteSoundCue(ActiveSound->GetName(), Info.bIsSoloed, Info.bIsMuted, Info.MuteSoloReason);
			if (Info.bIsMuted)
			{
				OutVolume = 0.0f;
			}
		}
	}

	// SoundClass mutes/solos.
	if (OutVolume != 0.0f && WaveInstance->SoundClass)
	{
		FString SoundClassName;
		WaveInstance->SoundClass->GetName(SoundClassName);
		Debugger.QuerySoloMuteSoundClass(SoundClassName, Info.bIsSoloed, Info.bIsMuted, Info.MuteSoloReason);
		if (Info.bIsMuted)
		{
			OutVolume = 0.0f;
		}
	}

	// Update State. 
	FScopeLock Lock(&DebugInfo->CS);
	{
		DebugInfo->bIsMuted = Info.bIsMuted;
		DebugInfo->bIsSoloed = Info.bIsSoloed;
		DebugInfo->MuteSoloReason = MoveTemp(Info.MuteSoloReason);
	}

#endif //ENABLE_AUDIO_DEBUG

	return OutVolume;
}

FSpatializationParams FSoundSource::GetSpatializationParams()
{
	FSpatializationParams Params;

	if (WaveInstance->GetUseSpatialization())
	{
		FVector EmitterPosition = AudioDevice->GetListenerTransformedDirection(WaveInstance->Location, &Params.Distance);

		// If we are using the OmniRadius feature
		if (WaveInstance->OmniRadius > 0.0f)
		{
			// Initialize to full omni-directionality (bigger value, more omni)
			static const float MaxNormalizedRadius = 1000000.0f;
			Params.NormalizedOmniRadius = MaxNormalizedRadius;

			if (Params.Distance > 0)
			{
				Params.NormalizedOmniRadius = FMath::Clamp(WaveInstance->OmniRadius / Params.Distance, 0.0f, MaxNormalizedRadius);
			}
		}
		else
		{
			Params.NormalizedOmniRadius = 0.0f;
		}

		if (Buffer->NumChannels == 2)
		{
			Params.LeftChannelPosition = AudioDevice->GetListenerTransformedDirection(LeftChannelSourceLocation, nullptr);
			Params.RightChannelPosition = AudioDevice->GetListenerTransformedDirection(RightChannelSourceLocation, nullptr);
			Params.EmitterPosition = FVector::ZeroVector;
		}
		else
		{
			Params.EmitterPosition = EmitterPosition;
		}
	}
	else
	{
		Params.NormalizedOmniRadius = 0.0f;
		Params.Distance = 0.0f;
		Params.EmitterPosition = FVector::ZeroVector;
	}
	Params.EmitterWorldPosition = WaveInstance->Location;

	int32 ListenerIndex = 0;
	if (WaveInstance->ActiveSound != nullptr)
	{
		Params.EmitterWorldRotation = WaveInstance->ActiveSound->Transform.GetRotation();
		ListenerIndex = WaveInstance->ActiveSound->GetClosestListenerIndex();
	}
	else
	{
		Params.EmitterWorldRotation = FQuat::Identity;
	}

	// Pass the actual listener orientation and position
	FTransform ListenerTransform;
	AudioDevice->GetListenerTransform(ListenerIndex, ListenerTransform);
	Params.ListenerOrientation = ListenerTransform.GetRotation();
	Params.ListenerPosition = ListenerTransform.GetLocation();

	return Params;
}

void FSoundSource::InitCommon()
{
	PlaybackTime = 0.0f;
	TickCount = 0;

	// Reset pause state
	bIsPausedByGame = false;
	bIsManuallyPaused = false;
	
#if ENABLE_AUDIO_DEBUG
	DebugInfo = MakeShared<FDebugInfo, ESPMode::ThreadSafe>();
#endif //ENABLE_AUDIO_DEBUG
}

void FSoundSource::UpdateCommon()
{
	check(WaveInstance);

	Pitch = WaveInstance->GetPitch();

	// Don't apply global pitch scale to UI sounds
	if (!WaveInstance->bIsUISound)
	{
		Pitch *= AudioDevice->GetGlobalPitchScale().GetValue();
	}

	Pitch = AudioDevice->ClampPitch(Pitch);

	// Track playback time even if the voice is not virtual, it can flip to being virtual while playing.
	const float DeviceDeltaTime = AudioDevice->GetDeviceDeltaTime();

	// Scale the playback time based on the pitch of the sound
	PlaybackTime += DeviceDeltaTime * Pitch;
}

float FSoundSource::GetPlaybackPercent() const
{
	const float Percentage = PlaybackTime / WaveInstance->WaveData->GetDuration();
	if (WaveInstance->LoopingMode == LOOP_Never)
	{
		return FMath::Clamp(Percentage, 0.0f, 1.0f);
	}
	else
	{
		// Wrap the playback percent for looping sounds
		return FMath::Fmod(Percentage, 1.0f);
	}

}

void FSoundSource::GetChannelLocations(FVector& Left, FVector&Right) const
{
	Left = LeftChannelSourceLocation;
	Right = RightChannelSourceLocation;
}


void FSoundSource::NotifyPlaybackData()
{
	const uint64 AudioComponentID = WaveInstance->ActiveSound->GetAudioComponentID();
	if (AudioComponentID > 0)
	{
		const USoundWave* SoundWave = WaveInstance->WaveData;

		if (WaveInstance->ActiveSound->bUpdatePlayPercentage)
		{
			const float PlaybackPercent = GetPlaybackPercent();
			FAudioThread::RunCommandOnGameThread([AudioComponentID, SoundWave, PlaybackPercent]()
			{
				if (UAudioComponent* AudioComponent = UAudioComponent::GetAudioComponentFromID(AudioComponentID))
				{
					if (AudioComponent->OnAudioPlaybackPercent.IsBound())
					{
						AudioComponent->OnAudioPlaybackPercent.Broadcast(SoundWave, PlaybackPercent);
					}

					if (AudioComponent->OnAudioPlaybackPercentNative.IsBound())
					{
						AudioComponent->OnAudioPlaybackPercentNative.Broadcast(AudioComponent, SoundWave, PlaybackPercent);
					}
				}
			});
		}

		if (WaveInstance->ActiveSound->bUpdateSingleEnvelopeValue)
		{
			const float EnvelopeValue = GetEnvelopeValue();
			FAudioThread::RunCommandOnGameThread([AudioComponentID, SoundWave, EnvelopeValue]()
			{
				if (UAudioComponent* AudioComponent = UAudioComponent::GetAudioComponentFromID(AudioComponentID))
				{
					if (AudioComponent->OnAudioSingleEnvelopeValue.IsBound())
					{
						AudioComponent->OnAudioSingleEnvelopeValue.Broadcast(SoundWave, EnvelopeValue);
					}

					if (AudioComponent->OnAudioSingleEnvelopeValueNative.IsBound())
					{
						AudioComponent->OnAudioSingleEnvelopeValueNative.Broadcast(AudioComponent, SoundWave, EnvelopeValue);
					}
				}
			});
		}

		// We do a broadcast from the active sound in this case, just update the envelope value of the wave instance here
		if (WaveInstance->ActiveSound->bUpdateMultiEnvelopeValue)
		{
			const float EnvelopeValue = GetEnvelopeValue();
			WaveInstance->SetEnvelopeValue(EnvelopeValue);
		}
	}
}

/*-----------------------------------------------------------------------------
	FNotifyBufferFinishedHooks implementation.
-----------------------------------------------------------------------------*/

void FNotifyBufferFinishedHooks::AddNotify(USoundNode* NotifyNode, UPTRINT WaveInstanceHash)
{
	Notifies.Add(FNotifyBufferDetails(NotifyNode, WaveInstanceHash));
}

UPTRINT FNotifyBufferFinishedHooks::GetHashForNode(USoundNode* NotifyNode) const
{
	for (const FNotifyBufferDetails& NotifyDetails : Notifies)
	{
		if (NotifyDetails.NotifyNode == NotifyNode)
		{
			return NotifyDetails.NotifyNodeWaveInstanceHash;
		}
	}

	return 0;
}

void FNotifyBufferFinishedHooks::DispatchNotifies(FWaveInstance* WaveInstance, const bool bStopped)
{
	for (int32 NotifyIndex = Notifies.Num() - 1; NotifyIndex >= 0; --NotifyIndex)
	{
		// All nodes get an opportunity to handle the notify if we're forcefully stopping the sound
		if (Notifies[NotifyIndex].NotifyNode)
		{
			if (Notifies[NotifyIndex].NotifyNode->NotifyWaveInstanceFinished(WaveInstance) && !bStopped)
			{
				break;
			}
		}
	}

}

void FNotifyBufferFinishedHooks::AddReferencedObjects( FReferenceCollector& Collector )
{
	for (FNotifyBufferDetails& NotifyDetails : Notifies)
	{
		Collector.AddReferencedObject( NotifyDetails.NotifyNode );
	}
}

FArchive& operator<<( FArchive& Ar, FNotifyBufferFinishedHooks& NotifyHook )
{
	if( !Ar.IsLoading() && !Ar.IsSaving() )
	{
		for (FNotifyBufferFinishedHooks::FNotifyBufferDetails& NotifyDetails : NotifyHook.Notifies)
		{
			Ar << NotifyDetails.NotifyNode;
		}
	}
	return( Ar );
}


/*-----------------------------------------------------------------------------
	FWaveInstance implementation.
-----------------------------------------------------------------------------*/

/** Helper to create good unique type hashs for FWaveInstance instances */
uint32 FWaveInstance::TypeHashCounter = 0;

/**
 * Constructor, initializing all member variables.
 *
 * @param InActiveSound		ActiveSound this wave instance belongs to.
 */
FWaveInstance::FWaveInstance(const UPTRINT InWaveInstanceHash, FActiveSound& InActiveSound)
	: WaveData(nullptr)
	, SoundClass(nullptr)
	, SoundSubmix(nullptr)
	, SourceEffectChain(nullptr)
	, ActiveSound(&InActiveSound)
	, Volume(0.0f)
	, DistanceAttenuation(1.0f)
	, VolumeMultiplier(1.0f)
	, EnvelopValue(0.0f)
	, EnvelopeFollowerAttackTime(10)
	, EnvelopeFollowerReleaseTime(100)
	, Priority(1.0f)
	, VoiceCenterChannelVolume(0.0f)
	, RadioFilterVolume(0.0f)
	, RadioFilterVolumeThreshold(0.0f)
	, LFEBleed(0.0f)
	, LoopingMode(LOOP_Never)
	, StartTime(-1.f)
	, bOutputToBusOnly(false)
	, bApplyRadioFilter(false)
	, bIsStarted(false)
	, bIsFinished(false)
	, bAlreadyNotifiedHook(false)
	, bUseSpatialization(false)
	, bEnableLowPassFilter(false)
	, bIsOccluded(false)
	, bIsUISound(false)
	, bIsMusic(false)
	, bReverb(true)
	, bCenterChannelOnly(false)
	, bIsPaused(false)
	, bReportedSpatializationWarning(false)
	, bIsAmbisonics(false)
	, bIsStopping(false)
	, SpatializationMethod(ESoundSpatializationAlgorithm::SPATIALIZATION_Default)
	, SpatializationPluginSettings(nullptr)
	, OcclusionPluginSettings(nullptr)
	, ReverbPluginSettings(nullptr)
	, OutputTarget(EAudioOutputTarget::Speaker)
	, LowPassFilterFrequency(MAX_FILTER_FREQUENCY)
	, SoundClassFilterFrequency(MAX_FILTER_FREQUENCY)
	, OcclusionFilterFrequency(MAX_FILTER_FREQUENCY)
	, AmbientZoneFilterFrequency(MAX_FILTER_FREQUENCY)
	, AttenuationLowpassFilterFrequency(MAX_FILTER_FREQUENCY)
	, AttenuationHighpassFilterFrequency(MIN_FILTER_FREQUENCY)
	, Pitch(0.0f)
	, Location(FVector::ZeroVector)
	, OmniRadius(0.0f)
	, StereoSpread(0.0f)
	, AttenuationDistance(0.0f)
	, ListenerToSoundDistance(0.0f)
	, ListenerToSoundDistanceForPanning(0.0f)
	, AbsoluteAzimuth(0.0f)
	, PlaybackTime(0.0f)
	, ReverbSendMethod(EReverbSendMethod::Linear)
	, ReverbSendLevelRange(0.0f, 0.0f)
	, ReverbSendLevelDistanceRange(0.0f, 0.0f)
	, ManualReverbSendLevel(0.0f)
	, TypeHash(0)
	, WaveInstanceHash(InWaveInstanceHash)
	, UserIndex(0)
{
	TypeHash = ++TypeHashCounter;
}

bool FWaveInstance::IsPlaying() const
{
	check(ActiveSound);

	if (!WaveData)
	{
		return false;
	}

	// TODO: move out of audio.  Subtitle system should be separate and just set VirtualizationMode to PlayWhenSilent
	const bool bHasSubtitles = ActiveSound->bHandleSubtitles && (ActiveSound->bHasExternalSubtitles || WaveData->Subtitles.Num() > 0);
	if (bHasSubtitles)
	{
		return true;
	}

	if (ActiveSound->IsPlayWhenSilent() && (!BypassPlayWhenSilentCVar || WaveData->bProcedural))
	{
		return true;
	}

	const float WaveInstanceVolume = Volume * VolumeMultiplier * DistanceAttenuation * GetDynamicVolume();
	if (WaveInstanceVolume > KINDA_SMALL_NUMBER)
	{
		return true;
	}

	if (ActiveSound->ComponentVolumeFader.IsFadingIn())
	{
		return true;
	}

	return false;
}

/**
 * Notifies the wave instance that it has finished.
 */
void FWaveInstance::NotifyFinished( const bool bStopped )
{
	if( !bAlreadyNotifiedHook )
	{
		// Can't have a source finishing that hasn't started
		if( !bIsStarted )
		{
			UE_LOG(LogAudio, Warning, TEXT( "Received finished notification from waveinstance that hasn't started!" ) );
		}

		// We are finished.
		bIsFinished = true;

		// Avoid double notifications.
		bAlreadyNotifiedHook = true;

		NotifyBufferFinishedHooks.DispatchNotifies(this, bStopped);
	}
}

/**
 * Stops the wave instance without notifying NotifyWaveInstanceFinishedHook. This will NOT stop wave instance
 * if it is set up to loop indefinitely or set to remain active.
 */
void FWaveInstance::StopWithoutNotification( void )
{
	if( LoopingMode == LOOP_Forever || ActiveSound->bShouldRemainActiveIfDropped )
	{
		// We don't finish if we're either indefinitely looping or the audio component explicitly mandates that we should
		// remain active which is e.g. used for engine sounds and such.
		bIsFinished = false;
	}
	else
	{
		// We're finished.
		bIsFinished = true;
	}
}

FArchive& operator<<( FArchive& Ar, FWaveInstance* WaveInstance )
{
	if( !Ar.IsLoading() && !Ar.IsSaving() )
	{
		Ar << WaveInstance->WaveData;
		Ar << WaveInstance->SoundClass;
		Ar << WaveInstance->NotifyBufferFinishedHooks;
	}
	return( Ar );
}

void FWaveInstance::AddReferencedObjects( FReferenceCollector& Collector )
{
	Collector.AddReferencedObject( WaveData );

	if (USynthSound* SynthSound = Cast<USynthSound>(WaveData))
	{
		if (USynthComponent* SynthComponent = SynthSound->GetOwningSynthComponent())
		{
			Collector.AddReferencedObject(SynthComponent);
		}
	}

	for (FAttenuationSubmixSendSettings& SubmixSend : SubmixSendSettings)
	{
		if (SubmixSend.Submix)
		{
			Collector.AddReferencedObject(SubmixSend.Submix);
		}
	}

	Collector.AddReferencedObject( SoundClass );
	NotifyBufferFinishedHooks.AddReferencedObjects( Collector );
}

float FWaveInstance::GetActualVolume() const
{
	// Include all volumes
	float ActualVolume = GetVolume() * DistanceAttenuation;
	if (ActualVolume != 0.0f)
	{
		ActualVolume *= GetDynamicVolume();

		check(ActiveSound);
		if (!ActiveSound->bIsPreviewSound)
		{
			check(ActiveSound->AudioDevice);
			ActualVolume *= ActiveSound->AudioDevice->GetMasterVolume();
		}
	}

	return ActualVolume;
}

float FWaveInstance::GetDistanceAttenuation() const
{
	// Only includes volume attenuation due do distance
	return DistanceAttenuation;
}

float FWaveInstance::GetDynamicVolume() const
{
	float OutVolume = 1.0f;

	if (GEngine)
	{
		if (FAudioDeviceManager* DeviceManager = GEngine->GetAudioDeviceManager())
		{
			if (WaveData)
			{
				OutVolume *= DeviceManager->GetDynamicSoundVolume(ESoundType::Wave, WaveData->GetFName());
			}

			if (ActiveSound)
			{
				if (const USoundCue* Sound = Cast<USoundCue>(ActiveSound->GetSound()))
				{
					OutVolume *= DeviceManager->GetDynamicSoundVolume(ESoundType::Cue, Sound->GetFName());
				}
			}

			if (SoundClass)
			{
				OutVolume *= DeviceManager->GetDynamicSoundVolume(ESoundType::Class, SoundClass->GetFName());
			}
		}
	}

	return OutVolume;
}

float FWaveInstance::GetVolumeWithDistanceAttenuation() const
{
	return GetVolume() * DistanceAttenuation;
}

float FWaveInstance::GetPitch() const
{
	return Pitch;
}

float FWaveInstance::GetVolume() const
{
	// Only includes non-attenuation and non-app volumes
	return Volume * VolumeMultiplier;
}

bool FWaveInstance::ShouldStopDueToMaxConcurrency() const
{
	check(ActiveSound);
	return ActiveSound->bShouldStopDueToMaxConcurrency;
}

float FWaveInstance::GetVolumeWeightedPriority() const
{
	// If priority has been set via bAlwaysPlay, it will have a priority larger than MAX_SOUND_PRIORITY. If that's the case, we should ignore volume weighting.
	if (Priority > MAX_SOUND_PRIORITY)
	{
		return Priority;
	}

	// This will result in zero-volume sounds still able to be sorted due to priority but give non-zero volumes higher priority than 0 volumes
	float ActualVolume = GetVolumeWithDistanceAttenuation();
	if (ActualVolume > 0.0f)
	{
		// Only check for bypass if the actual volume is greater than 0.0
		if (WaveData && WaveData->bBypassVolumeScaleForPriority)
		{
			return Priority;
		}
		else
		{
			return ActualVolume * Priority;
		}
	}
	else if (IsStopping())
	{
		// Stopping sounds will be sorted above 0-volume sounds
		return ActualVolume * Priority - MAX_SOUND_PRIORITY - 1.0f;
	}
	else
	{
		return Priority - 2.0f * MAX_SOUND_PRIORITY - 1.0f;
	}
}

bool FWaveInstance::IsSeekable() const
{
	check(WaveData);

	if (StartTime == 0.0f)
	{
		return false;
	}

	if (WaveData->bIsSourceBus || WaveData->bProcedural)
	{
		return false;
	}

	if (IsStreaming() && !WaveData->IsSeekableStreaming())
	{
		return false;
	}

	return true;
}

bool FWaveInstance::IsStreaming() const
{
	return FPlatformProperties::SupportsAudioStreaming() && WaveData != nullptr && WaveData->IsStreaming(nullptr);
}

bool FWaveInstance::GetUseSpatialization() const
{
	return AllowAudioSpatializationCVar && bUseSpatialization;
}

FString FWaveInstance::GetName() const
{
	if (WaveData)
	{
		return WaveData->GetName();
	}
	return TEXT("Null");
}


/*-----------------------------------------------------------------------------
	WaveModInfo implementation - downsampling of wave files.
-----------------------------------------------------------------------------*/

//  Macros to convert 4 bytes to a Riff-style ID uint32.
//  Todo: make these endian independent !!!

#define UE_MAKEFOURCC(ch0, ch1, ch2, ch3)\
	((uint32)(uint8)(ch0) | ((uint32)(uint8)(ch1) << 8) |\
	((uint32)(uint8)(ch2) << 16) | ((uint32)(uint8)(ch3) << 24 ))

#define UE_mmioFOURCC(ch0, ch1, ch2, ch3)\
	UE_MAKEFOURCC(ch0, ch1, ch2, ch3)

#if PLATFORM_SUPPORTS_PRAGMA_PACK
#pragma pack(push, 2)
#endif

// Main Riff-Wave header.
struct FRiffWaveHeaderChunk
{
	uint32	rID;			// Contains 'RIFF'
	uint32	ChunkLen;		// Remaining length of the entire riff chunk (= file).
	uint32	wID;			// Form type. Contains 'WAVE' for .wav files.
};

// General chunk header format.
struct FRiffChunkOld
{
	uint32	ChunkID;		  // General data chunk ID like 'data', or 'fmt '
	uint32	ChunkLen;		  // Length of the rest of this chunk in bytes.
};

// ChunkID: 'fmt ' ("WaveFormatEx" structure )
struct FRiffFormatChunk
{
	uint16   wFormatTag;        // Format type: 1 = PCM
	uint16   nChannels;         // Number of channels (i.e. mono, stereo...).
	uint32   nSamplesPerSec;    // Sample rate. 44100 or 22050 or 11025  Hz.
	uint32   nAvgBytesPerSec;   // For buffer estimation  = sample rate * BlockAlign.
	uint16   nBlockAlign;       // Block size of data = Channels times BYTES per sample.
	uint16   wBitsPerSample;    // Number of bits per sample of mono data.
	uint16   cbSize;            // The count in bytes of the size of extra information (after cbSize).
};

// FExtendedFormatChunk subformat GUID.
struct FSubformatGUID
{
	uint32 Data1;				// Format type, corresponds to a wFormatTag in WaveFormatEx.

								// Fixed values for all extended wave formats.
	uint16 Data2 = 0x0000;
	uint16 Data3 = 0x0010;
	uint8 Data4[8];

	FSubformatGUID()
	{
		Data4[0] = 0x80;
		Data4[1] = 0x00;
		Data4[2] = 0x00;
		Data4[3] = 0xaa;
		Data4[4] = 0x00;
		Data4[5] = 0x38;
		Data4[6] = 0x9b;
		Data4[7] = 0x71;
	}
};

// ChunkID: 'fmt ' ("WaveFormatExtensible" structure)
struct FExtendedFormatChunk
{
	FRiffFormatChunk Format;			// Standard WaveFormatEx ('fmt ') chunk, with
									// wFormatTag == WAVE_FORMAT_EXTENSIBLE and cbSize == 22
	union
	{
		uint16 wValidBitsPerSample;	// Actual bits of precision. Can be less than wBitsPerSample.
		uint16 wSamplesPerBlock;	// Valid if wValidBitsPerSample == 0. Used by compressed formats.
		uint16 wReserved;			// If neither applies, set to 0.
	} Samples;
	uint32 dwChannelMask;			// Which channels are present in the stream.
	FSubformatGUID SubFormat;		// Subformat identifier.
};

#if PLATFORM_SUPPORTS_PRAGMA_PACK
#pragma pack(pop)
#endif

//
//	Figure out the WAVE file layout.
//
bool FWaveModInfo::ReadWaveInfo( const uint8* WaveData, int32 WaveDataSize, FString* ErrorReason, bool InHeaderDataOnly, void** OutFormatHeader)
{
	FRiffFormatChunk* FmtChunk;
	FExtendedFormatChunk* FmtChunkEx = nullptr;
	FRiffWaveHeaderChunk* RiffHdr = (FRiffWaveHeaderChunk* )WaveData;
	WaveDataEnd = WaveData + WaveDataSize;

	if( WaveDataSize == 0 )
	{
		return( false );
	}

	// Verify we've got a real 'WAVE' header.
#if PLATFORM_LITTLE_ENDIAN
	if( RiffHdr->wID != UE_mmioFOURCC( 'W','A','V','E' ) )
	{
		if (ErrorReason) *ErrorReason = TEXT("Invalid WAVE file.");
		return( false );
	}
#else
	if( ( RiffHdr->wID != ( UE_mmioFOURCC( 'W','A','V','E' ) ) ) &&
	     ( RiffHdr->wID != ( UE_mmioFOURCC( 'E','V','A','W' ) ) ) )
	{
		ErrorReason = TEXT("Invalid WAVE file.")
		return( false );
	}

	bool AlreadySwapped = ( RiffHdr->wID == ( UE_mmioFOURCC('W','A','V','E' ) ) );
	if( !AlreadySwapped )
	{
		RiffHdr->rID = INTEL_ORDER32( RiffHdr->rID );
		RiffHdr->ChunkLen = INTEL_ORDER32( RiffHdr->ChunkLen );
		RiffHdr->wID = INTEL_ORDER32( RiffHdr->wID );
	}
#endif

	FRiffChunkOld* RiffChunk = ( FRiffChunkOld* )&WaveData[3 * 4];
	pMasterSize = &RiffHdr->ChunkLen;

	// Look for the 'fmt ' chunk.
	while( ( ( ( uint8* )RiffChunk + 8 ) < WaveDataEnd ) && ( INTEL_ORDER32( RiffChunk->ChunkID ) != UE_mmioFOURCC( 'f','m','t',' ' ) ) )
	{
		RiffChunk = ( FRiffChunkOld* )( ( uint8* )RiffChunk + Pad16Bit( INTEL_ORDER32( RiffChunk->ChunkLen ) ) + 8 );
	}

	if( INTEL_ORDER32( RiffChunk->ChunkID ) != UE_mmioFOURCC( 'f','m','t',' ' ) )
	{
		#if !PLATFORM_LITTLE_ENDIAN  // swap them back just in case.
			if( !AlreadySwapped )
			{
				RiffHdr->rID = INTEL_ORDER32( RiffHdr->rID );
				RiffHdr->ChunkLen = INTEL_ORDER32( RiffHdr->ChunkLen );
				RiffHdr->wID = INTEL_ORDER32( RiffHdr->wID );
			}
		#endif
		if (ErrorReason) *ErrorReason = TEXT("Invalid WAVE file.");
		return( false );
	}

	FmtChunk = ( FRiffFormatChunk* )( ( uint8* )RiffChunk + 8 );
#if !PLATFORM_LITTLE_ENDIAN
	if( !AlreadySwapped )
	{
		FmtChunk->wFormatTag = INTEL_ORDER16( FmtChunk->wFormatTag );
		FmtChunk->nChannels = INTEL_ORDER16( FmtChunk->nChannels );
		FmtChunk->nSamplesPerSec = INTEL_ORDER32( FmtChunk->nSamplesPerSec );
		FmtChunk->nAvgBytesPerSec = INTEL_ORDER32( FmtChunk->nAvgBytesPerSec );
		FmtChunk->nBlockAlign = INTEL_ORDER16( FmtChunk->nBlockAlign );
		FmtChunk->wBitsPerSample = INTEL_ORDER16( FmtChunk->wBitsPerSample );
	}
#endif
	pBitsPerSample = &FmtChunk->wBitsPerSample;
	pSamplesPerSec = &FmtChunk->nSamplesPerSec;
	pAvgBytesPerSec = &FmtChunk->nAvgBytesPerSec;
	pBlockAlign = &FmtChunk->nBlockAlign;
	pChannels = &FmtChunk->nChannels;
	pFormatTag = &FmtChunk->wFormatTag;

	if(OutFormatHeader != NULL)
	{
		*OutFormatHeader = FmtChunk;
	}

	// If we have an extended fmt chunk, the format tag won't be a wave format. Instead we need to read the subformat ID.
	if (INTEL_ORDER32(RiffChunk->ChunkLen) >= 40 && FmtChunk->wFormatTag == 0xFFFE) // WAVE_FORMAT_EXTENSIBLE
	{
		FmtChunkEx = (FExtendedFormatChunk*)((uint8*)RiffChunk + 8);

#if !PLATFORM_LITTLE_ENDIAN
		if (!AlreadySwapped)
		{
			FmtChunkEx->Samples.wValidBitsPerSample = INTEL_ORDER16(FmtChunkEx->Samples.wValidBitsPerSample);
			FmtChunkEx->SubFormat.Data1 = INTEL_ORDER32(FmtChunkEx->SubFormat.Data1);
			FmtChunkEx->SubFormat.Data2 = INTEL_ORDER16(FmtChunkEx->SubFormat.Data2);
			FmtChunkEx->SubFormat.Data3 = INTEL_ORDER16(FmtChunkEx->SubFormat.Data3);
			*((uint64*)FmtChunkEx->SubFormat.Data4) = INTEL_ORDER64(*(uint64*)FmtChunkEx->SubFormat.Data4);
		}
#endif

		bool bValid = true;
		static const FSubformatGUID GUID;

		if (FmtChunkEx->SubFormat.Data1 == 0x00000001 /* PCM */ &&
			FmtChunkEx->Samples.wValidBitsPerSample > 0 && FmtChunkEx->Samples.wValidBitsPerSample != FmtChunk->wBitsPerSample)
		{
			bValid = false;
			if (ErrorReason) *ErrorReason = TEXT("Unsupported WAVE file format: actual bit rate does not match the container size.");
		}
		else if (FMemory::Memcmp((uint8*)&FmtChunkEx->SubFormat + 4, (uint8*)&GUID + 4, sizeof(GUID) - 4) != 0)
		{
			bValid = false;
			if (ErrorReason) *ErrorReason = TEXT("Unsupported WAVE file format: subformat identifier not recognized.");
		}

		if (!bValid)
		{
#if !PLATFORM_LITTLE_ENDIAN // swap them back just in case.
			if (!AlreadySwapped)
			{
				FmtChunkEx->Samples.wValidBitsPerSample = INTEL_ORDER16(FmtChunkEx->Samples.wValidBitsPerSample);
				FmtChunkEx->SubFormat.Data1 = INTEL_ORDER32(FmtChunkEx->SubFormat.Data1);
				FmtChunkEx->SubFormat.Data2 = INTEL_ORDER16(FmtChunkEx->SubFormat.Data2);
				FmtChunkEx->SubFormat.Data3 = INTEL_ORDER16(FmtChunkEx->SubFormat.Data3);
				*((uint64*)FmtChunkEx->SubFormat.Data4) = INTEL_ORDER64(*(uint64*)FmtChunkEx->SubFormat.Data4);
			}
#endif
			return (false);
		}

		// Set the format tag pointer to the subformat GUID.
		pFormatTag = reinterpret_cast<uint16*>(&FmtChunkEx->SubFormat.Data1);
	}

	// re-initalize the RiffChunk pointer
	RiffChunk = ( FRiffChunkOld* )&WaveData[3 * 4];

	// Look for the 'data' chunk.
	while( ( ( ( uint8* )RiffChunk + 8 ) <= WaveDataEnd ) && ( INTEL_ORDER32( RiffChunk->ChunkID ) != UE_mmioFOURCC( 'd','a','t','a' ) ) )
	{
		RiffChunk = ( FRiffChunkOld* )( ( uint8* )RiffChunk + Pad16Bit( INTEL_ORDER32( RiffChunk->ChunkLen ) ) + 8 );
	}

	if( INTEL_ORDER32( RiffChunk->ChunkID ) != UE_mmioFOURCC( 'd','a','t','a' ) )
	{
		#if !PLATFORM_LITTLE_ENDIAN  // swap them back just in case.
			if( !AlreadySwapped )
			{
				RiffHdr->rID = INTEL_ORDER32( RiffHdr->rID );
				RiffHdr->ChunkLen = INTEL_ORDER32( RiffHdr->ChunkLen );
				RiffHdr->wID = INTEL_ORDER32( RiffHdr->wID );
				FmtChunk->wFormatTag = INTEL_ORDER16( FmtChunk->wFormatTag );
				FmtChunk->nChannels = INTEL_ORDER16( FmtChunk->nChannels );
				FmtChunk->nSamplesPerSec = INTEL_ORDER32( FmtChunk->nSamplesPerSec );
				FmtChunk->nAvgBytesPerSec = INTEL_ORDER32( FmtChunk->nAvgBytesPerSec );
				FmtChunk->nBlockAlign = INTEL_ORDER16( FmtChunk->nBlockAlign );
				FmtChunk->wBitsPerSample = INTEL_ORDER16( FmtChunk->wBitsPerSample );
				if (FmtChunkEx != nullptr)
				{
					FmtChunkEx->Samples.wValidBitsPerSample = INTEL_ORDER16(FmtChunkEx->Samples.wValidBitsPerSample);
					FmtChunkEx->SubFormat.Data1 = INTEL_ORDER32(FmtChunkEx->SubFormat.Data1);
					FmtChunkEx->SubFormat.Data2 = INTEL_ORDER16(FmtChunkEx->SubFormat.Data2);
					FmtChunkEx->SubFormat.Data3 = INTEL_ORDER16(FmtChunkEx->SubFormat.Data3);
					*((uint64*)FmtChunkEx->SubFormat.Data4) = INTEL_ORDER64(*(uint64*)FmtChunkEx->SubFormat.Data4);
				}
			}
		#endif
		if (ErrorReason) *ErrorReason = TEXT("Invalid WAVE file.");
		return( false );
	}

#if !PLATFORM_LITTLE_ENDIAN  // swap them back just in case.
	if( AlreadySwapped ) // swap back into Intel order for chunk search...
	{
		RiffChunk->ChunkLen = INTEL_ORDER32( RiffChunk->ChunkLen );
	}
#endif

	SampleDataStart = ( uint8* )RiffChunk + 8;
	pWaveDataSize = &RiffChunk->ChunkLen;
	SampleDataSize = INTEL_ORDER32( RiffChunk->ChunkLen );
	SampleDataEnd = SampleDataStart + SampleDataSize;

	if( !InHeaderDataOnly && ( uint8* )SampleDataEnd > ( uint8* )WaveDataEnd )
	{
		UE_LOG(LogAudio, Warning, TEXT( "Wave data chunk is too big!" ) );

		// Fix it up by clamping data chunk.
		SampleDataEnd = ( uint8* )WaveDataEnd;
		SampleDataSize = SampleDataEnd - SampleDataStart;
		RiffChunk->ChunkLen = INTEL_ORDER32( SampleDataSize );
	}

	if (   *pFormatTag != 0x0001 // WAVE_FORMAT_PCM
		&& *pFormatTag != 0x0002 // WAVE_FORMAT_ADPCM
		&& *pFormatTag != 0x0011) // WAVE_FORMAT_DVI_ADPCM
	{
		ReportImportFailure();
		if (ErrorReason) *ErrorReason = TEXT("Unsupported wave file format.  Only PCM, ADPCM, and DVI ADPCM can be imported.");
		return( false );
	}

	if(!InHeaderDataOnly)
	{
		if( ( uint8* )SampleDataEnd > ( uint8* )WaveDataEnd )
		{
			UE_LOG(LogAudio, Warning, TEXT( "Wave data chunk is too big!" ) );

			// Fix it up by clamping data chunk.
			SampleDataEnd = ( uint8* )WaveDataEnd;
			SampleDataSize = SampleDataEnd - SampleDataStart;
			RiffChunk->ChunkLen = INTEL_ORDER32( SampleDataSize );
		}

		NewDataSize = SampleDataSize;

		#if !PLATFORM_LITTLE_ENDIAN
		if( !AlreadySwapped )
		{
			if( FmtChunk->wBitsPerSample == 16 )
			{
				for( uint16* i = ( uint16* )SampleDataStart; i < ( uint16* )SampleDataEnd; i++ )
				{
					*i = INTEL_ORDER16( *i );
				}
			}
			else if( FmtChunk->wBitsPerSample == 32 )
			{
				for( uint32* i = ( uint32* )SampleDataStart; i < ( uint32* )SampleDataEnd; i++ )
				{
					*i = INTEL_ORDER32( *i );
				}
			}
		}
		#endif
	}

	// Couldn't byte swap this before, since it'd throw off the chunk search.
#if !PLATFORM_LITTLE_ENDIAN
	*pWaveDataSize = INTEL_ORDER32( *pWaveDataSize );
#endif

	return( true );
}

bool FWaveModInfo::ReadWaveHeader(const uint8* RawWaveData, int32 Size, int32 Offset )
{
	if( Size == 0 )
	{
		return( false );
	}

	// Parse wave info.
	if( !ReadWaveInfo( RawWaveData + Offset, Size ) )
	{
		return( false );
	}

	// Validate the info
	if( ( *pChannels != 1 && *pChannels != 2 ) || *pBitsPerSample != 16 )
	{
		return( false );
	}

	return( true );
}

void FWaveModInfo::ReportImportFailure() const
{
	if (FEngineAnalytics::IsAvailable())
	{
		TArray< FAnalyticsEventAttribute > WaveImportFailureAttributes;
		WaveImportFailureAttributes.Add(FAnalyticsEventAttribute(TEXT("Format"), *pFormatTag));
		WaveImportFailureAttributes.Add(FAnalyticsEventAttribute(TEXT("Channels"), *pChannels));
		WaveImportFailureAttributes.Add(FAnalyticsEventAttribute(TEXT("BitsPerSample"), *pBitsPerSample));

		FEngineAnalytics::GetProvider().RecordEvent(FString("Editor.Usage.WaveImportFailure"), WaveImportFailureAttributes);
	}
}

static void WriteUInt32ToByteArrayLE(TArray<uint8>& InByteArray, int32& Index, const uint32 Value)
{
	InByteArray[Index++] = (uint8)(Value >> 0);
	InByteArray[Index++] = (uint8)(Value >> 8);
	InByteArray[Index++] = (uint8)(Value >> 16);
	InByteArray[Index++] = (uint8)(Value >> 24);
}

static void WriteUInt16ToByteArrayLE(TArray<uint8>& InByteArray, int32& Index, const uint16 Value)
{
	InByteArray[Index++] = (uint8)(Value >> 0);
	InByteArray[Index++] = (uint8)(Value >> 8);
}

void SerializeWaveFile(TArray<uint8>& OutWaveFileData, const uint8* InPCMData, const int32 NumBytes, const int32 NumChannels, const int32 SampleRate)
{
	// Reserve space for the raw wave data
	OutWaveFileData.Empty(NumBytes + 44);
	OutWaveFileData.AddZeroed(NumBytes + 44);

	int32 WaveDataByteIndex = 0;

	// Wave Format Serialization ----------

	// FieldName: ChunkID
	// FieldSize: 4 bytes
	// FieldValue: RIFF (FourCC value, big-endian)
	OutWaveFileData[WaveDataByteIndex++] = 'R';
	OutWaveFileData[WaveDataByteIndex++] = 'I';
	OutWaveFileData[WaveDataByteIndex++] = 'F';
	OutWaveFileData[WaveDataByteIndex++] = 'F';

	// ChunkName: ChunkSize: 4 bytes
	// Value: NumBytes + 36. Size of the rest of the chunk following this number. Size of entire file minus 8 bytes.
	WriteUInt32ToByteArrayLE(OutWaveFileData, WaveDataByteIndex, NumBytes + 36);

	// FieldName: Format
	// FieldSize: 4 bytes
	// FieldValue: "WAVE"  (big-endian)
	OutWaveFileData[WaveDataByteIndex++] = 'W';
	OutWaveFileData[WaveDataByteIndex++] = 'A';
	OutWaveFileData[WaveDataByteIndex++] = 'V';
	OutWaveFileData[WaveDataByteIndex++] = 'E';

	// FieldName: Subchunk1ID
	// FieldSize: 4 bytes
	// FieldValue: "fmt "
	OutWaveFileData[WaveDataByteIndex++] = 'f';
	OutWaveFileData[WaveDataByteIndex++] = 'm';
	OutWaveFileData[WaveDataByteIndex++] = 't';
	OutWaveFileData[WaveDataByteIndex++] = ' ';

	// FieldName: Subchunk1Size
	// FieldSize: 4 bytes
	// FieldValue: 16 for PCM
	WriteUInt32ToByteArrayLE(OutWaveFileData, WaveDataByteIndex, 16);

	// FieldName: AudioFormat
	// FieldSize: 2 bytes
	// FieldValue: 1 for PCM
	WriteUInt16ToByteArrayLE(OutWaveFileData, WaveDataByteIndex, 1);

	// FieldName: NumChannels
	// FieldSize: 2 bytes
	// FieldValue: 1 for for mono
	WriteUInt16ToByteArrayLE(OutWaveFileData, WaveDataByteIndex, NumChannels);

	// FieldName: SampleRate
	// FieldSize: 4 bytes
	// FieldValue: Passed in sample rate
	WriteUInt32ToByteArrayLE(OutWaveFileData, WaveDataByteIndex, SampleRate);

	// FieldName: ByteRate
	// FieldSize: 4 bytes
	// FieldValue: SampleRate * NumChannels * BitsPerSample/8
	int32 ByteRate = SampleRate * NumChannels * 2;
	WriteUInt32ToByteArrayLE(OutWaveFileData, WaveDataByteIndex, ByteRate);

	// FieldName: BlockAlign
	// FieldSize: 2 bytes
	// FieldValue: NumChannels * BitsPerSample/8
	int32 BlockAlign = 2;
	WriteUInt16ToByteArrayLE(OutWaveFileData, WaveDataByteIndex, BlockAlign);

	// FieldName: BitsPerSample
	// FieldSize: 2 bytes
	// FieldValue: 16 (16 bits per sample)
	WriteUInt16ToByteArrayLE(OutWaveFileData, WaveDataByteIndex, 16);

	// FieldName: Subchunk2ID
	// FieldSize: 4 bytes
	// FieldValue: "data" (big endian)

	OutWaveFileData[WaveDataByteIndex++] = 'd';
	OutWaveFileData[WaveDataByteIndex++] = 'a';
	OutWaveFileData[WaveDataByteIndex++] = 't';
	OutWaveFileData[WaveDataByteIndex++] = 'a';

	// FieldName: Subchunk2Size
	// FieldSize: 4 bytes
	// FieldValue: number of bytes of the data
	WriteUInt32ToByteArrayLE(OutWaveFileData, WaveDataByteIndex, NumBytes);

	// Copy the raw PCM data to the audio file
	FMemory::Memcpy(&OutWaveFileData[WaveDataByteIndex], InPCMData, NumBytes);
}

