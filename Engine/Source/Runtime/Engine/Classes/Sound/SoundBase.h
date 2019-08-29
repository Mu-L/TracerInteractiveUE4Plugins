// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

/** 
 * The base class for a playable sound object 
 */

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Audio.h"
#include "Sound/SoundConcurrency.h"
#include "SoundSubmix.h"
#include "Sound/SoundSourceBusSend.h"
#include "SoundBase.generated.h"

class USoundEffectSourcePreset;
class USoundSubmix;
class USoundSourceBus;
class USoundEffectSourcePresetChain;

struct FSoundSubmixSendInfo;
struct FSoundSourceBusSendInfo;
struct FActiveSound;
struct FSoundParseParameters;

UCLASS(config=Engine, hidecategories=Object, abstract, editinlinenew, BlueprintType)
class ENGINE_API USoundBase : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	static USoundClass* DefaultSoundClassObject;
	static USoundConcurrency* DefaultSoundConcurrencyObject;

protected:
	/** Sound class this sound belongs to */
	UPROPERTY(EditAnywhere, Category=Sound, meta=(DisplayName = "Sound Class"), AssetRegistrySearchable)
	USoundClass* SoundClassObject;

public:

	/** When "stat sounds -debug" has been specified, draw this sound's attenuation shape when the sound is audible. For debugging purpose only. */
	UPROPERTY(EditAnywhere, Category = Debug)
	uint8 bDebug:1;

	/** Whether or not to override the sound concurrency object with local concurrency settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Concurrency)
	uint8 bOverrideConcurrency:1;

	/** Whether or not to only send this audio's output to a bus. If true, will not be this sound won't be audible except through bus sends. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Effects)
	uint8 bOutputToBusOnly : 1;

	UPROPERTY()
	uint8 bIgnoreFocus_DEPRECATED:1;

	/** Whether or not to only send this audio's output to a bus. If true, will not be this sound won't be audible except through bus sends. */
	UPROPERTY()
	uint8 bHasDelayNode : 1;

	/** Whether or not this sound has a concatenator node. If it does, we have to allow the sound to persist even though it may not have generate audible audio in a given audio thread frame. */
	UPROPERTY()
	uint8 bHasConcatenatorNode : 1;

	/** Whether a sound has virtualize when silent enabled (i.e. for a sound cue, if any sound wave player has it enabled). */
	UPROPERTY()
	uint8 bHasVirtualizeWhenSilent:1;

	/** Allows this sound to bypass volume-weighting for the max channel resolution. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Concurrency)
	uint8 bBypassVolumeScaleForPriority : 1;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TEnumAsByte<enum EMaxConcurrentResolutionRule::Type> MaxConcurrentResolutionRule_DEPRECATED;
#endif

	/** Number of times this cue is currently being played. */
	int32 CurrentPlayCount;

	/** If Override Concurrency is false, the sound concurrency settings to use for this sound. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Concurrency, meta = (EditCondition = "!bOverrideConcurrency"))
	class USoundConcurrency* SoundConcurrencySettings;

	/** If Override Concurrency is true, concurrency settings to use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Concurrency, meta = (EditCondition = "bOverrideConcurrency"))
	struct FSoundConcurrencySettings ConcurrencyOverrides;

#if WITH_EDITORONLY_DATA
	/** Maximum number of times this sound can be played concurrently. */
	UPROPERTY()
	int32 MaxConcurrentPlayCount_DEPRECATED;
#endif

	/** Duration of sound in seconds. */
	UPROPERTY(Category=Info, AssetRegistrySearchable, VisibleAnywhere, BlueprintReadOnly)
	float Duration;

	/** The max distance of the asset, as determined by attenuation settings. */
	UPROPERTY(Category = Info, AssetRegistrySearchable, VisibleAnywhere, BlueprintReadOnly)
	float MaxDistance;

	/** Total number of samples (in the thousands). Useful as a metric to analyze the relative size of a given sound asset in content browser. */
	UPROPERTY(Category = Info, AssetRegistrySearchable, VisibleAnywhere, BlueprintReadOnly)
	float TotalSamples;

	/** Sound priority (higher value is higher priority) used for concurrency resolution. This priority value is weighted against the final volume of the sound. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Concurrency, meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "100.0", UIMax = "100.0") )
	float Priority;

	/** Attenuation settings package for the sound */
	UPROPERTY(EditAnywhere, Category=Attenuation)
	USoundAttenuation* AttenuationSettings;

	/** Sound submix this sound belongs to. 
	  * Audio will play here and traverse through the submix graph. 
	  * A null entry will make the sound obey the default master effects graph.
	  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Effects, meta = (DisplayName = "Sound Submix"))
	USoundSubmix* SoundSubmixObject;

	/** An array of submix sends. Audio from this sound will send a portion of its audio to these effects.  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Effects)
	TArray<FSoundSubmixSendInfo> SoundSubmixSends;

	/** The source effect chain to use for this sound. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Effects)
	USoundEffectSourcePresetChain* SourceEffectChain;

	/** This sound will send its audio output to this list of buses if there are bus instances playing after source effects are processed.  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Effects, meta = (DisplayName = "Post-Effect Bus Sends"))
	TArray<FSoundSourceBusSendInfo> BusSends;

	/** This sound will send its audio output to this list of buses if there are bus instances playing before source effects are processed.  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Effects, meta = (DisplayName = "Pre-Effect Bus Sends"))
	TArray<FSoundSourceBusSendInfo> PreEffectBusSends;

public:	

	//~ Begin UObject Interface.
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	//~ End UObject interface.
	
	/** Returns whether the sound base is set up in a playable manner */
	virtual bool IsPlayable() const;

	/** Returns whether a sound is allowed to be virtualized. */
	virtual bool IsAllowedVirtual() const;

	/** Returns whether or not this sound base has an attenuation node. */
	virtual bool HasAttenuationNode() const;

	/** Returns a pointer to the attenuation settings that are to be applied for this node */
	virtual const FSoundAttenuationSettings* GetAttenuationSettingsToApply() const;

	/**
	 * Returns the farthest distance at which the sound could be heard
	 */
	virtual float GetMaxDistance() const;

	/** 
	 * Returns the length of the sound
	 */
	virtual float GetDuration();

	/** Returns whether or not this sound has a delay node, which means it's possible for the sound to not generate audio for a while. */
	bool HasDelayNode() const;

	/** Returns whether or not this sound has a sequencer node, which means it's possible for the owning active sound to persist even though it's not generating audio. */
	bool HasConcatenatorNode() const;

	/** Returns true if any of the sounds in the sound have "virtualize when silent" enabled. */
	bool IsVirtualizeWhenSilent() const;

	virtual float GetVolumeMultiplier();
	virtual float GetPitchMultiplier();

	/** Returns the subtitle priority */
	virtual float GetSubtitlePriority() const { return DEFAULT_SUBTITLE_PRIORITY; };
	
	/** Returns whether or not any part of this sound wants interior volumes applied to it */
	virtual bool ShouldApplyInteriorVolumes();

	/** Returns curves associated with this sound if it has any. By default returns nullptr, but types
	*	supporting curves can return a corresponding curve table.
	*/
	virtual class UCurveTable* GetCurveData() const { return nullptr; }

	/** Returns whether or not this sound is looping. */
	bool IsLooping();

	/** Parses the Sound to generate the WaveInstances to play. */
	virtual void Parse( class FAudioDevice* AudioDevice, const UPTRINT NodeWaveInstanceHash, FActiveSound& ActiveSound, const FSoundParseParameters& ParseParams, TArray<FWaveInstance*>& WaveInstances ) { }

	/** Returns the SoundClass used for this sound. */
	virtual USoundClass* GetSoundClass() const;

	/** Returns the SoundSubmix used for this sound. */
	virtual USoundSubmix* GetSoundSubmix() const;

	/** Returns the sound submix sends for this sound. */
	void GetSoundSubmixSends(TArray<FSoundSubmixSendInfo>& OutSends) const;

	/** Returns the sound source sends for this sound. */
	void GetSoundSourceBusSends(EBusSendType BusSendType, TArray<FSoundSourceBusSendInfo>& OutSends) const;

	/** Returns the FSoundConcurrencySettings struct to use. */
	const FSoundConcurrencySettings* GetSoundConcurrencySettingsToApply();

	/** Returns the priority to use when evaluating concurrency. */
	float GetPriority() const;

	/** Returns the sound concurrency object ID if it exists. If it doesn't exist, returns 0. */
	uint32 GetSoundConcurrencyObjectID() const;

};

