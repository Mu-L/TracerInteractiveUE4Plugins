// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Sound/SoundEffectPreset.h"
#include "Sound/SoundEffectBase.h"
#include "SoundEffectSubmix.generated.h"

class FSoundEffectSubmix;


/** Preset of a submix effect that can be shared between sounds. */
UCLASS(config = Engine, hidecategories = Object, abstract, editinlinenew, BlueprintType)
class ENGINE_API USoundEffectSubmixPreset : public USoundEffectPreset
{
	GENERATED_UCLASS_BODY()

	virtual FColor GetPresetColor() const override { return FColor(162.0f, 84.0f, 101.0f); }

};

/** Struct which has data needed to initialize the submix effect. */
struct FSoundEffectSubmixInitData
{
	void* PresetSettings;
	float SampleRate;
};

/** Struct which supplies audio data to submix effects on game thread. */
struct FSoundEffectSubmixInputData
{
	/** Ptr to preset data if new data is available. This will be nullptr if no new preset data has been set. */
	void* PresetData;
	
	/** The number of audio frames for this input data. 1 frame is an interleaved sample. */
	int32 NumFrames;

	/** The number of channels of the submix. */
	int32 NumChannels;

	/** The number of device channels. */
	int32 NumDeviceChannels;

	/** The listener transforms (one for each viewport index). */
	const TArray<FTransform>* ListenerTransforms;

	/** The raw input audio buffer. Size is NumFrames * NumChannels */
	Audio::AlignedFloatBuffer* AudioBuffer;

	/** Sample accurate audio clock. */
	double AudioClock;

	FSoundEffectSubmixInputData()
		: PresetData(nullptr)
		, NumFrames(0)
		, NumChannels(0)
		, NumDeviceChannels(0)
		, ListenerTransforms(nullptr)
		, AudioBuffer(nullptr)
	{}
};

struct FSoundEffectSubmixOutputData
{
	/** The output audio buffer. */
	Audio::AlignedFloatBuffer* AudioBuffer;

	/** The number of channels of the submix. */
	int32 NumChannels;
};

class ENGINE_API FSoundEffectSubmix : public FSoundEffectBase
{
public:
	FSoundEffectSubmix() {}
	virtual ~FSoundEffectSubmix() {}

	/** Called on an audio effect at initialization on main thread before audio processing begins. */
	virtual void Init(const FSoundEffectSubmixInitData& InSampleRate) {};

	/** Called on game thread to allow submix effect to query game data if needed. */
	virtual void Tick() {}

	/** Override to down mix input audio to a desired channel count. */
	virtual uint32 GetDesiredInputChannelCountOverride() const
	{
		return INDEX_NONE;
	}

	/** Process the input block of audio. Called on audio thread. */
	virtual void OnProcessAudio(const FSoundEffectSubmixInputData& InData, FSoundEffectSubmixOutputData& OutData) {};

	/** Allow effects to supply a drylevel. */
	virtual float GetDryLevel() const { return 0.0f; }

	/** Processes audio in the submix effect. */
	void ProcessAudio(FSoundEffectSubmixInputData& InData, FSoundEffectSubmixOutputData& OutData);
};
