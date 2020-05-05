// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IAudioExtensionPlugin.h"
#include "Sound/SoundEffectPreset.h"
#include "Sound/SoundEffectBase.h"
#include "Templates/SharedPointer.h"
#include "UObject/ObjectMacros.h"

#include "SoundEffectSource.generated.h"

class FSoundEffectSource;
class FSoundEffectBase;

/** Preset of a source effect that can be shared between chains. */
UCLASS(config = Engine, abstract, editinlinenew, BlueprintType)
class ENGINE_API USoundEffectSourcePreset : public USoundEffectPreset
{
	GENERATED_BODY()
};

USTRUCT(BlueprintType)
struct ENGINE_API FSourceEffectChainEntry
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SourceEffect")
	USoundEffectSourcePreset* Preset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SourceEffect")
	uint32 bBypass : 1;
};

/** Chain of source effect presets that can be shared between referencing sounds. */
UCLASS(BlueprintType)
class ENGINE_API USoundEffectSourcePresetChain : public UObject
{
	GENERATED_BODY()

public:

	/** Chain of source effects to use for this sound source. */
	UPROPERTY(EditAnywhere, Category = "SourceEffect")
	TArray<FSourceEffectChainEntry> Chain;

	/** Whether to keep the source alive for the duration of the effect chain tails. */
	UPROPERTY(EditAnywhere, Category = Effects)
	uint32 bPlayEffectChainTails : 1;

	void AddReferencedEffects(FReferenceCollector& Collector);

protected:

#if WITH_EDITORONLY_DATA
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

};

/** Data required to initialize the source effect. */
struct FSoundEffectSourceInitData
{
	float SampleRate;
	int32 NumSourceChannels;
	double AudioClock;

	// The object id of the parent preset
	uint32 ParentPresetUniqueId;

	FSoundEffectSourceInitData()
	: SampleRate(0.0f)
	, NumSourceChannels(0)
	, AudioClock(0.0)
	, ParentPresetUniqueId(INDEX_NONE)
	{}
};

/** Data required to update the source effect. */
struct FSoundEffectSourceInputData
{
	float CurrentVolume;
	float CurrentPitch;
	double AudioClock;
	float CurrentPlayFraction;
	FSpatializationParams SpatParams;
	float* InputSourceEffectBufferPtr;
	int32 NumSamples;

	FSoundEffectSourceInputData()
		: CurrentVolume(0.0f)
		, CurrentPitch(0.0f)
		, AudioClock(0.0)
		, SpatParams(FSpatializationParams())
		, InputSourceEffectBufferPtr(nullptr)
		, NumSamples(0)
	{
	}
};

class ENGINE_API FSoundEffectSource : public FSoundEffectBase
{
protected:
	/** Called on an audio effect at initialization on main thread before audio processing begins. */
	virtual void Init(const FSoundEffectSourceInitData& InInitData) = 0;

public:
	virtual ~FSoundEffectSource() = default;

	/** Process the input block of audio. Called on audio thread. */
	virtual void ProcessAudio(const FSoundEffectSourceInputData& InData, float* OutAudioBufferData) = 0;

	/** Process modulation controls if enabled */
	virtual void ProcessControls(const FSoundModulationControls& InControls) { }

	friend class USoundEffectPreset;
};
