// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/SynthComponent.h"

#include "Containers/Array.h"
#include "HAL/CriticalSection.h"
#include "MediaSampleQueue.h"
#include "Misc/Timespan.h"
#include "Templates/Atomic.h"
#include "Templates/SharedPointer.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"

#include "MediaSoundComponent.generated.h"

class FMediaAudioResampler;
class FMediaPlayerFacade;
class IMediaAudioSample;
class IMediaPlayer;
class UMediaPlayer;
class USoundClass;


/**
 * Available media sound channel types.
 */
UENUM()
enum class EMediaSoundChannels
{
	/** Mono (1 channel). */
	Mono,

	/** Stereo (2 channels). */
	Stereo,

	/** Surround sound (7.1 channels; for UI). */
	Surround
};


/**
 * Implements a sound component for playing a media player's audio output.
 */
UCLASS(ClassGroup=Media, editinlinenew, meta=(BlueprintSpawnableComponent))
class MEDIAASSETS_API UMediaSoundComponent
	: public USynthComponent
{
	GENERATED_BODY()

public:

	/** Media sound channel type. */
	UPROPERTY(EditAnywhere, Category="Media")
	EMediaSoundChannels Channels;

	/** Dynamically adjust the sample rate if audio and media clock desynchronize. */
	UPROPERTY(EditAnywhere, Category="Media", AdvancedDisplay)
	bool DynamicRateAdjustment;

	/**
	 * Factor for calculating the sample rate adjustment.
	 *
	 * If dynamic rate adjustment is enabled, this number is multiplied with the drift
	 * between the audio and media clock (in 100ns ticks) to determine the adjustment.
	 * that is to be multiplied into the current playrate.
	 */
	UPROPERTY(EditAnywhere, Category="Media", AdvancedDisplay)
	float RateAdjustmentFactor;

	/**
	 * The allowed range of dynamic rate adjustment.
	 *
	 * If dynamic rate adjustment is enabled, and the necessary adjustment
	 * falls outside of this range, audio samples will be dropped.
	 */
	UPROPERTY(EditAnywhere, Category="Media", AdvancedDisplay)
	FFloatRange RateAdjustmentRange;

public:

	/**
	 * Create and initialize a new instance.
	 *
	 * @param ObjectInitializer Initialization parameters.
	 */
	UMediaSoundComponent(const FObjectInitializer& ObjectInitializer);

	/** Virtual destructor. */
	~UMediaSoundComponent();

public:

	/**
	 * Get the attenuation settings based on the current component settings.
	 *
	 * @param OutAttenuationSettings Will contain the attenuation settings, if available.
	 * @return true if attenuation settings were returned, false if attenuation is disabled.
	 */
	UFUNCTION(BlueprintCallable, Category="Media|MediaSoundComponent", meta=(DisplayName="Get Attenuation Settings To Apply", ScriptName="GetAttenuationSettingsToApply"))
	bool BP_GetAttenuationSettingsToApply(FSoundAttenuationSettings& OutAttenuationSettings);

	/**
	 * Get the media player that provides the audio samples.
	 *
	 * @return The component's media player, or nullptr if not set.
	 * @see SetMediaPlayer
	 */
	UFUNCTION(BlueprintCallable, Category="Media|MediaSoundComponent")
	UMediaPlayer* GetMediaPlayer() const;

	/**
	 * Set the media player that provides the audio samples.
	 *
	 * @param NewMediaPlayer The player to set.
	 * @see GetMediaPlayer
	 */
	UFUNCTION(BlueprintCallable, Category="Media|MediaSoundComponent")
	void SetMediaPlayer(UMediaPlayer* NewMediaPlayer);

public:

	void UpdatePlayer();

#if WITH_EDITOR
	/**
	 * Set the component's default media player property.
	 *
	 * @param NewMediaPlayer The player to set.
	 * @see SetMediaPlayer
	 */
	void SetDefaultMediaPlayer(UMediaPlayer* NewMediaPlayer);
#endif

public:

	//~ TAttenuatedComponentVisualizer interface

	void CollectAttenuationShapesForVisualization(TMultiMap<EAttenuationShape::Type, FBaseAttenuationSettings::AttenuationShapeDetails>& ShapeDetailsMap) const;

public:

	//~ UActorComponent interface

	virtual void OnRegister() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

public:

	//~ USceneComponent interface

	virtual void Activate(bool bReset = false) override;
	virtual void Deactivate() override;

public:

	//~ UObject interface
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:

	/**
	 * Get the attenuation settings based on the current component settings.
	 *
	 * @return Attenuation settings, or nullptr if attenuation is disabled.
	 */
	const FSoundAttenuationSettings* GetSelectedAttenuationSettings() const;

protected:

	//~ USynthComponent interface

	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

protected:

	/**
	 * The media player asset associated with this component.
	 *
	 * This property is meant for design-time convenience. To change the
	 * associated media player at run-time, use the SetMediaPlayer method.
	 *
	 * @see SetMediaPlayer
	 */
	UPROPERTY(EditAnywhere, Category="Media")
	UMediaPlayer* MediaPlayer;

private:

	/** The player's current play rate (cached for use on audio thread). */
	TAtomic<float> CachedRate;

	/** The player's current time (cached for use on audio thread). */
	TAtomic<FTimespan> CachedTime;

	/** Critical section for synchronizing access to PlayerFacadePtr. */
	FCriticalSection CriticalSection;

	/** The player that is currently associated with this component. */
	TWeakObjectPtr<UMediaPlayer> CurrentPlayer;

	/** The player facade that's currently providing texture samples. */
	TWeakPtr<FMediaPlayerFacade, ESPMode::ThreadSafe> CurrentPlayerFacade;

	/** Adjusts the output sample rate to synchronize audio and media clock. */
	float RateAdjustment;

	/** The audio resampler. */
	FMediaAudioResampler* Resampler;

	/** Audio sample queue. */
	TSharedPtr<FMediaAudioSampleQueue, ESPMode::ThreadSafe> SampleQueue;

private:

	static USoundClass* DefaultMediaSoundClassObject;
};
