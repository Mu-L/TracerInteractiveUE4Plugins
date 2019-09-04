// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Sound/SampleBuffer.h"
#include "SoundEffectSubmix.h"
#include "IAmbisonicsMixer.h"
#include "Curves/CurveFloat.h"
#include "SoundSubmix.generated.h"


class UEdGraph;
class USoundEffectSubmixPreset;
class USoundSubmix;
class ISubmixBufferListener;

/* Submix channel format.
 Allows submixes to have sources mix to a particular channel configuration for potential effect chain requirements.
 Master submix will always render at the device channel count. All child submixes will be down-mixed (or up-mixed) to
 the device channel count. This feature exists to allow specific submix effects to do their work on multi-channel mixes
 of audio.
*/
UENUM(BlueprintType)
enum class ESubmixChannelFormat : uint8
{
	// Sets the submix channels to the output device channel count
	Device UMETA(DisplayName = "Device"),

	// Sets the submix mix to stereo (FL, FR)
	Stereo UMETA(DisplayName = "Stereo"),

	// Sets the submix to mix to quad (FL, FR, SL, SR)
	Quad UMETA(DisplayName = "Quad"),

	// Sets the submix to mix 5.1 (FL, FR, FC, LF, SL, SR)
	FiveDotOne UMETA(DisplayName = "5.1"),

	// Sets the submix to mix audio to 7.1 (FL, FR, FC, LF, BL, BR, SL, SR)
	SevenDotOne UMETA(DisplayName = "7.1"),

	// Sets the submix to render audio as an ambisonics bed.
	Ambisonics UMETA(DisplayName = "Ambisonics"),

	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EAudioRecordingExportType : uint8
{
	// Exports a USoundWave.
	SoundWave,

	// Exports a WAV file.
	WavFile
};

UENUM(BlueprintType)
enum class ESendLevelControlMethod : uint8
{
	// A send based on linear interpolation between a distance range and send-level range
	Linear,

	// A send based on a supplied curve
	CustomCurve,

	// A manual send level (Uses the specified constant send level value. Useful for 2D sounds.)
	Manual,
};


// Class used to send audio to submixes from USoundBase
USTRUCT(BlueprintType)
struct ENGINE_API FSoundSubmixSendInfo
{
	GENERATED_USTRUCT_BODY()

	/* 
		Manual: Use Send Level only
		Linear: Interpolate between Min and Max Send Levels based on listener distance (between Distance Min and Distance Max)
		Custom Curve: Use the float curve to map Send Level to distance (0.0-1.0 on curve maps to Distance Min - Distance Max)
	*/
	UPROPERTY(EditAnywhere, Category = SubmixSend)
	ESendLevelControlMethod SendLevelControlMethod;

	// The submix to send the audio to
	UPROPERTY(EditAnywhere, Category = SubmixSend)
	USoundSubmix* SoundSubmix;

	// The amount of audio to send
	UPROPERTY(EditAnywhere, Category = SubmixSend)
	float SendLevel;

	// The amount to send to master when sound is located at a distance equal to value specified in the min send distance.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SubmixSend)
	float MinSendLevel;

	// The amount to send to master when sound is located at a distance equal to value specified in the max send distance.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SubmixSend)
	float MaxSendLevel;

	// The min distance to send to the master
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SubmixSend)
	float MinSendDistance;

	// The max distance to send to the master
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SubmixSend)
	float MaxSendDistance;

	// The custom reverb send curve to use for distance-based send level.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SubmixSend)
	FRuntimeFloatCurve CustomSendLevelCurve;

	FSoundSubmixSendInfo()
		: SendLevelControlMethod(ESendLevelControlMethod::Manual)
		, SoundSubmix(nullptr)
		, SendLevel(0.0f)
		, MinSendLevel(0.0f)
		, MaxSendLevel(1.0f)
		, MinSendDistance(100.0f)
		, MaxSendDistance(1000.0f)
		{
		}
};

/**
* Called when a recorded file has finished writing to disk.
*
*/
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSubmixRecordedFileDone, const USoundWave*, ResultingSoundWave);

/**
* Called when a new submix envelope value is generated on the given audio device id (different for multiple PIE). Array is an envelope value for each channel.
*/
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSubmixEnvelope, const TArray<float>&, Envelope);

DECLARE_DYNAMIC_DELEGATE_OneParam(FOnSubmixEnvelopeBP, const TArray<float>&, Envelope);


#if WITH_EDITOR

/** Interface for sound submix graph interaction with the AudioEditor module. */
class ISoundSubmixAudioEditor
{
public:
	virtual ~ISoundSubmixAudioEditor() {}

	/** Refreshes the sound class graph links. */
	virtual void RefreshGraphLinks(UEdGraph* SoundClassGraph) = 0;
};
#endif

UCLASS(config = Engine, hidecategories = Object, editinlinenew, BlueprintType)
class ENGINE_API USoundSubmix : public UObject
{
	GENERATED_UCLASS_BODY()

	// Child submixes to this sound mix
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = SoundSubmix)
	TArray<USoundSubmix*> ChildSubmixes;

	UPROPERTY()
	USoundSubmix* ParentSubmix;

#if WITH_EDITORONLY_DATA
	/** EdGraph based representation of the SoundSubmix */
	class UEdGraph* SoundSubmixGraph;
#endif

	// Experimental! Specifies the channel format for the submix. Sources will be mixed at the specified format. Useful for specific effects that need to operate on a specific format.
	UPROPERTY()
	ESubmixChannelFormat ChannelFormat;

	/** Mute this submix when the application is muted or in the background. Used to prevent submix effect tails from continuing when tabbing out of application or if application is muted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = SoundSubmix)
	uint8 bMuteWhenBackgrounded : 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = SoundSubmix)
	TArray<USoundEffectSubmixPreset*> SubmixEffectChain;

	/** Optional settings used by plugins which support ambisonics file playback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SoundSubmix)
	UAmbisonicsSubmixSettingsBase* AmbisonicsPluginSettings;

	/** The attack time in milliseconds for the envelope follower. Delegate callbacks can be registered to get the envelope value of sounds played with this submix. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = EnvelopeFollower, meta = (ClampMin = "0", UIMin = "0"))
	int32 EnvelopeFollowerAttackTime;

	/** The release time in milliseconds for the envelope follower. Delegate callbacks can be registered to get the envelope value of sounds played with this submix. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = EnvelopeFollower, meta = (ClampMin = "0", UIMin = "0"))
	int32 EnvelopeFollowerReleaseTime;

	/** The output volume of the submix. Applied after submix effects and analysis are performed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SoundSubmix, meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float OutputVolume;

	// Blueprint delegate for when a recorded file is finished exporting.
	UPROPERTY(BlueprintAssignable)
	FOnSubmixRecordedFileDone OnSubmixRecordedFileDone;

	// Start recording the audio from this submix.
	UFUNCTION(BlueprintCallable, Category = "Audio|Bounce", meta = (WorldContext = "WorldContextObject", DisplayName = "Start Recording Submix Output"))
	void StartRecordingOutput(const UObject* WorldContextObject, float ExpectedDuration);

	void StartRecordingOutput(FAudioDevice* InDevice, float ExpectedDuration);

	// Finish recording the audio from this submix and export it as a wav file or a USoundWave.
	UFUNCTION(BlueprintCallable, Category = "Audio|Bounce", meta = (WorldContext = "WorldContextObject", DisplayName = "Finish Recording Submix Output"))
	void StopRecordingOutput(const UObject* WorldContextObject, EAudioRecordingExportType ExportType, const FString& Name, FString Path, USoundWave* ExistingSoundWaveToOverwrite = nullptr);

	void StopRecordingOutput(FAudioDevice* InDevice, EAudioRecordingExportType ExportType, const FString& Name, FString Path, USoundWave* ExistingSoundWaveToOverwrite = nullptr);

	// Start envelope following the submix output. Register with OnSubmixEnvelope to receive envelope follower data in BP.
	UFUNCTION(BlueprintCallable, Category = "Audio|EnvelopeFollowing", meta = (WorldContext = "WorldContextObject"))
	void StartEnvelopeFollowing(const UObject* WorldContextObject);

	void StartEnvelopeFollowing(FAudioDevice* InDevice);

	// Start envelope following the submix output. Register with OnSubmixEnvelope to receive envelope follower data in BP.
	UFUNCTION(BlueprintCallable, Category = "Audio|EnvelopeFollowing", meta = (WorldContext = "WorldContextObject"))
	void StopEnvelopeFollowing(const UObject* WorldContextObject);

	void StopEnvelopeFollowing(FAudioDevice* InDevice);

	UFUNCTION(BlueprintCallable, Category = "Audio|EnvelopeFollowing", meta = (WorldContext = "WorldContextObject"))
	void AddEnvelopeFollowerDelegate(const UObject* WorldContextObject, const FOnSubmixEnvelopeBP& OnSubmixEnvelopeBP);

	/** Sets the output volume of the submix. This dynamic volume scales with the OutputVolume property of this submix. */
	UFUNCTION(BlueprintCallable, Category = "Audio", meta = (WorldContext = "WorldContextObject"))
	void SetSubmixOutputVolume(const UObject* WorldContextObject, float InOutputVolume);

	// Registers and unregisters buffer listeners with the submix
	void RegisterSubmixBufferListener(ISubmixBufferListener* InBufferListener);
	void UnregisterSubmixBufferListener(ISubmixBufferListener* InBufferListener);

protected:

	//~ Begin UObject Interface.
	virtual FString GetDesc() override;
	virtual void BeginDestroy() override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PreEditChange(UProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject Interface.

	// State handling for bouncing output.
	TUniquePtr<Audio::FAudioRecordingData> RecordingData;

public:

	// Sound Submix Editor functionality
#if WITH_EDITOR

	/**
	* @return true if the child sound class exists in the tree
	*/
	bool RecurseCheckChild(USoundSubmix* ChildSoundSubmix);

	/**
	* Set the parent submix of this SoundSubmix, removing it as a child from its previous owner
	*
	* @param	InParentSubmix	The New Parent Submix of this
	*/
	void SetParentSubmix(USoundSubmix* InParentSubmix);

	/**
	* Add Referenced objects
	*
	* @param	InThis SoundSubmix we are adding references from.
	* @param	Collector Reference Collector
	*/
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	/**
	* Refresh all EdGraph representations of SoundSubmixes
	*
	* @param	bIgnoreThis	Whether to ignore this SoundSubmix if it's already up to date
	*/
	void RefreshAllGraphs(bool bIgnoreThis);

	/** Sets the sound submix graph editor implementation.* */
	static void SetSoundSubmixAudioEditor(TSharedPtr<ISoundSubmixAudioEditor> InSoundSubmixAudioEditor);

	/** Gets the sound submix graph editor implementation. */
	static TSharedPtr<ISoundSubmixAudioEditor> GetSoundSubmixAudioEditor();

private:

	/** Ptr to interface to sound class editor operations. */
	static TSharedPtr<ISoundSubmixAudioEditor> SoundSubmixAudioEditor;

#endif



};

