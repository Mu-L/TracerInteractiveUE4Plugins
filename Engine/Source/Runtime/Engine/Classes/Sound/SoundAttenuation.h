// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Attenuation.h"
#include "IAudioExtensionPlugin.h"
#include "SoundAttenuation.generated.h"

// This enumeration is deprecated
UENUM()
enum ESoundDistanceCalc
{
	SOUNDDISTANCE_Normal,
	SOUNDDISTANCE_InfiniteXYPlane,
	SOUNDDISTANCE_InfiniteXZPlane,
	SOUNDDISTANCE_InfiniteYZPlane,
	SOUNDDISTANCE_MAX,
};

UENUM()
enum ESoundSpatializationAlgorithm
{
	// Standard panning method for spatialization (linear or equal power method defined in project settings)
	SPATIALIZATION_Default UMETA(DisplayName = "Panning"),

	// Binaural spatialization method if available (requires headphones, enabled by plugins)
	SPATIALIZATION_HRTF UMETA(DisplayName = "Binaural"),
};

UENUM(BlueprintType)
enum class EAirAbsorptionMethod : uint8
{
	// The air absorption conform to a linear distance function
	Linear,

	// The air absorption conforms to a custom distance curve.
	CustomCurve,
};


UENUM(BlueprintType)
enum class EReverbSendMethod : uint8
{
	// A reverb send based on linear interpolation between a distance range and send-level range
	Linear,

	// A reverb send based on a supplied curve
	CustomCurve,

	// A manual reverb send level (Uses the specified constant send level value. Useful for 2D sounds.)
	Manual,
};


USTRUCT(BlueprintType)
struct ENGINE_API FSoundAttenuationPluginSettings
{
	GENERATED_USTRUCT_BODY()

	/** Settings to use with spatialization audio plugin. These are defined by the plugin creator. Not all audio plugins utilize this feature. This is an array so multiple plugins can have settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationSpatialization, meta = (DisplayName = "Spatialization Plugin Settings"))
	TArray<USpatializationPluginSourceSettingsBase*> SpatializationPluginSettingsArray;

	/** Settings to use with occlusion audio plugin. These are defined by the plugin creator. Not all audio plugins utilize this feature. This  is an array so multiple plugins can have settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationOcclusion, meta = (DisplayName = "Occlusion Plugin Settings"))
	TArray<UOcclusionPluginSourceSettingsBase*> OcclusionPluginSettingsArray;

	/** Settings to use with reverb audio plugin. These are defined by the plugin creator. Not all audio plugins utilize this feature. This  is an array so multiple plugins can have settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationReverbSend, meta = (DisplayName = "Reverb Plugin Settings"))
	TArray<UReverbPluginSourceSettingsBase*> ReverbPluginSettingsArray;
};

/*
The settings for attenuating.
*/
USTRUCT(BlueprintType)
struct ENGINE_API FSoundAttenuationSettings : public FBaseAttenuationSettings
{
	GENERATED_USTRUCT_BODY()

	/* Allows distance-based volume attenuation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationDistance, meta = (DisplayName = "Enable Volume Attenuation"))
	uint8 bAttenuate : 1;

	/* Allows the source to be 3D spatialized. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationSpatialization, meta = (DisplayName = "Enable Spatialization"))
	uint8 bSpatialize : 1;

	/** Allows simulation of air absorption by applying a filter with a cutoff frequency as a function of distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationAirAbsorption, meta = (DisplayName = "Enable Air Absorption"))
	uint8 bAttenuateWithLPF : 1;

	/** Enable listener focus-based adjustments. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationListenerFocus)
	uint8 bEnableListenerFocus : 1;

	/** Enables focus interpolation to smooth transition in and and of focus. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationListenerFocus)
	uint8 bEnableFocusInterpolation : 1;

	/** Enables realtime occlusion tracing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationOcclusion)
	uint8 bEnableOcclusion : 1;

	/** Enables tracing against complex collision when doing occlusion traces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationOcclusion)
	uint8 bUseComplexCollisionForOcclusion : 1;

	/** Enables adjusting reverb sends based on distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationReverbSend, meta = (DisplayName = "Enable Reverb Send"))
	uint8 bEnableReverbSend : 1;

	/** Enables applying a -6 dB attenuation to stereo assets which are 3d spatialized. Avoids clipping when assets have spread of 0.0 due to channel summing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationSpatialization, meta = (DisplayName = "Normalize 3D Stereo Sounds"))
	uint8 bApplyNormalizationToStereoSounds : 1;

	/** Enables applying a log scale to frequency values (so frequency sweeping is perceptually linear). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationAirAbsorption, meta = (DisplayName = "Enable Log Frequency Scaling"))
	uint8 bEnableLogFrequencyScaling : 1;

	/** What method we use to spatialize the sound. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationSpatialization, meta = (ClampMin = "0", EditCondition = "bSpatialize", DisplayName = "Spatialization Method"))
	TEnumAsByte<enum ESoundSpatializationAlgorithm> SpatializationAlgorithm;

	/** What method to use to map distance values to frequency absorption values. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationAirAbsorption)
	EAirAbsorptionMethod AbsorptionMethod;

	/* Which trace channel to use for audio occlusion checks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationOcclusion)
	TEnumAsByte<enum ECollisionChannel> OcclusionTraceChannel;

	/** What method to use to control master reverb sends */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationReverbSend)
	EReverbSendMethod ReverbSendMethod;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TEnumAsByte<enum ESoundDistanceCalc> DistanceType_DEPRECATED;
#endif

	/** The distance below which a sound is non-spatialized (2D). This prevents near-field audio from flipping as audio crosses the listener's position. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationSpatialization, meta=(ClampMin = "0", EditCondition="bSpatialize", DisplayName="Non-Spatialized Radius"))
	float OmniRadius;

	/** The world-space distance between left and right stereo channels when stereo assets are 3D spatialized. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationSpatialization, meta = (ClampMin = "0", EditCondition = "bSpatialize", DisplayName = "3D Stereo Spread"))
	float StereoSpread;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	USpatializationPluginSourceSettingsBase* SpatializationPluginSettings_DEPRECATED;

	UPROPERTY()
	float RadiusMin_DEPRECATED;

	UPROPERTY()
	float RadiusMax_DEPRECATED;
#endif

	/* The distance min range at which to apply an absorption LPF filter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationAirAbsorption, meta = (DisplayName = "Min Distance Range"))
	float LPFRadiusMin;

	/* The max distance range at which to apply an absorption LPF filter. Absorption freq cutoff interpolates between filter frequency ranges between these distance values. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationAirAbsorption, meta = (DisplayName = "Max Distance Range"))
	float LPFRadiusMax;

	/* The normalized custom curve to use for the air absorption lowpass frequency values. Does a mapping from defined distance values (x-axis) and defined frequency values (y-axis) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationAirAbsorption)
	FRuntimeFloatCurve CustomLowpassAirAbsorptionCurve;

	/* The normalized custom curve to use for the air absorption highpass frequency values. Does a mapping from defined distance values (x-axis) and defined frequency values (y-axis) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationAirAbsorption)
	FRuntimeFloatCurve CustomHighpassAirAbsorptionCurve;

	/* The range of the cutoff frequency (in hz) of the lowpass absorption filter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationAirAbsorption, meta = (DisplayName = "Low Pass Cutoff Frequency Min"))
	float LPFFrequencyAtMin;

	/* The range of the cutoff frequency (in hz) of the lowpass absorption filter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationAirAbsorption, meta = (DisplayName = "Low Pass Cutoff Frequency Max"))
	float LPFFrequencyAtMax;

	/* The range of the cutoff frequency (in hz) of the highpass absorption filter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationAirAbsorption, meta = (DisplayName = "High Pass Cutoff Frequency Min"))
	float HPFFrequencyAtMin;

	/* The range of the cutoff frequency (in hz) of the highpass absorption filter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationAirAbsorption, meta = (DisplayName = "High Pass Cutoff Frequency Max"))
	float HPFFrequencyAtMax;

	/** Azimuth angle (in degrees) relative to the listener forward vector which defines the focus region of sounds. Sounds playing at an angle less than this will be in focus. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationListenerFocus)
	float FocusAzimuth;

	/** Azimuth angle (in degrees) relative to the listener forward vector which defines the non-focus region of sounds. Sounds playing at an angle greater than this will be out of focus. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationListenerFocus)
	float NonFocusAzimuth;

	/** Amount to scale the distance calculation of sounds that are in-focus. Can be used to make in-focus sounds appear to be closer or further away than they actually are. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationListenerFocus, meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableListenerFocus"))
	float FocusDistanceScale;

	/** Amount to scale the distance calculation of sounds that are not in-focus. Can be used to make in-focus sounds appear to be closer or further away than they actually are.  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationListenerFocus, meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableListenerFocus"))
	float NonFocusDistanceScale;

	/** Amount to scale the priority of sounds that are in focus. Can be used to boost the priority of sounds that are in focus. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationListenerFocus, meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableListenerFocus"))
	float FocusPriorityScale;

	/** Amount to scale the priority of sounds that are not in-focus. Can be used to reduce the priority of sounds that are not in focus. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationListenerFocus, meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableListenerFocus"))
	float NonFocusPriorityScale;

	/** Amount to attenuate sounds that are in focus. Can be overridden at the sound-level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationListenerFocus, meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableListenerFocus"))
	float FocusVolumeAttenuation;

	/** Amount to attenuate sounds that are not in focus. Can be overridden at the sound-level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationListenerFocus, meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableListenerFocus"))
	float NonFocusVolumeAttenuation;

	/** Scalar used to increase interpolation speed upwards to the target Focus value */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationListenerFocus, meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableFocusInterpolation"))
	float FocusAttackInterpSpeed;

	/** Scalar used to increase interpolation speed downwards to the target Focus value */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationListenerFocus, meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableFocusInterpolation"))
	float FocusReleaseInterpSpeed;

	/** The low pass filter frequency (in hertz) to apply if the sound playing in this audio component is occluded. This will override the frequency set in LowPassFilterFrequency. A frequency of 0.0 is the device sample rate and will bypass the filter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationOcclusion, meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableOcclusion"))
	float OcclusionLowPassFilterFrequency;

	/** The amount of volume attenuation to apply to sounds which are occluded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationOcclusion, meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableOcclusion"))
	float OcclusionVolumeAttenuation;

	/** The amount of time in seconds to interpolate to the target OcclusionLowPassFilterFrequency when a sound is occluded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationOcclusion, meta = (ClampMin = "0", UIMin = "0.0", EditCondition = "bEnableOcclusion"))
	float OcclusionInterpolationTime;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	UOcclusionPluginSourceSettingsBase* OcclusionPluginSettings_DEPRECATED;

	UPROPERTY()
	UReverbPluginSourceSettingsBase* ReverbPluginSettings_DEPRECATED;
#endif

	/** The amount to send to master reverb when sound is located at a distance equal to value specified in the reverb min send distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationReverbSend, meta = (DisplayName = "Reverb Min Send Level"))
	float ReverbWetLevelMin;

	/** The amount to send to master reverb when sound is located at a distance equal to value specified in the reverb max send distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationReverbSend, meta = (DisplayName = "Reverb Max Send Level"))
	float ReverbWetLevelMax;

	/** The min distance to send to the master reverb. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationReverbSend, meta = (DisplayName = "Reverb Min Send Distance"))
	float ReverbDistanceMin;

	/** The max distance to send to the master reverb. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationReverbSend, meta = (DisplayName = "Reverb Max Send Distance"))
	float ReverbDistanceMax;

	/* The manual master reverb send level to use. Doesn't change as a function of distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationReverbSend)
	float ManualReverbSendLevel;

	/* The custom reverb send curve to use for distance-based send level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationReverbSend)
	FRuntimeFloatCurve CustomReverbSendCurve;

	/** Sound attenuation plugin settings to use with sounds that play with this attenuation setting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AttenuationPluginSettings, meta = (ShowOnlyInnerProperties))
	FSoundAttenuationPluginSettings PluginSettings;

	FSoundAttenuationSettings()
		: bAttenuate(true)
		, bSpatialize(true)
		, bAttenuateWithLPF(false)
		, bEnableListenerFocus(false)
		, bEnableFocusInterpolation(false)
		, bEnableOcclusion(false)
		, bUseComplexCollisionForOcclusion(false)
		, bEnableReverbSend(true)
		, bApplyNormalizationToStereoSounds(false)
		, bEnableLogFrequencyScaling(false)
		, SpatializationAlgorithm(ESoundSpatializationAlgorithm::SPATIALIZATION_Default)
		, AbsorptionMethod(EAirAbsorptionMethod::Linear)
		, OcclusionTraceChannel(ECC_Visibility)
		, ReverbSendMethod(EReverbSendMethod::Linear)
#if WITH_EDITORONLY_DATA
		, DistanceType_DEPRECATED(SOUNDDISTANCE_Normal)
#endif
		, OmniRadius(0.0f)
		, StereoSpread(200.0f)
#if WITH_EDITORONLY_DATA
		, SpatializationPluginSettings_DEPRECATED(nullptr)
		, RadiusMin_DEPRECATED(400.f)
		, RadiusMax_DEPRECATED(4000.f)
#endif
		, LPFRadiusMin(3000.f)
		, LPFRadiusMax(6000.f)
		, LPFFrequencyAtMin(20000.f)
		, LPFFrequencyAtMax(20000.f)
		, HPFFrequencyAtMin(0.0f)
		, HPFFrequencyAtMax(0.0f)
		, FocusAzimuth(30.0f)
		, NonFocusAzimuth(60.0f)
		, FocusDistanceScale(1.0f)
		, NonFocusDistanceScale(1.0f)
		, FocusPriorityScale(1.0f)
		, NonFocusPriorityScale(1.0f)
		, FocusVolumeAttenuation(1.0f)
		, NonFocusVolumeAttenuation(1.0f)
		, FocusAttackInterpSpeed(1.0f)
		, FocusReleaseInterpSpeed(1.0f)
		, OcclusionLowPassFilterFrequency(20000.f)
		, OcclusionVolumeAttenuation(1.0f)
		, OcclusionInterpolationTime(0.1f)
#if WITH_EDITORONLY_DATA
		, OcclusionPluginSettings_DEPRECATED(nullptr)
		, ReverbPluginSettings_DEPRECATED(nullptr)
#endif
		, ReverbWetLevelMin(0.3f)
		, ReverbWetLevelMax(0.95f)
		, ReverbDistanceMin(AttenuationShapeExtents.X)
		, ReverbDistanceMax(AttenuationShapeExtents.X + FalloffDistance)
		, ManualReverbSendLevel(0.0f)
	{
	}

	bool operator==(const FSoundAttenuationSettings& Other) const;
	void PostSerialize(const FArchive& Ar);

	virtual void CollectAttenuationShapesForVisualization(TMultiMap<EAttenuationShape::Type, FBaseAttenuationSettings::AttenuationShapeDetails>& ShapeDetailsMap) const override;
	float GetFocusPriorityScale(const struct FGlobalFocusSettings& FocusSettings, float FocusFactor) const;
	float GetFocusAttenuation(const struct FGlobalFocusSettings& FocusSettings, float FocusFactor) const;
	float GetFocusDistanceScale(const struct FGlobalFocusSettings& FocusSettings, float FocusFactor) const;
};

UE_DEPRECATED(4.15, "FAttenuationSettings has been renamed FSoundAttenuationSettings")
typedef FSoundAttenuationSettings FAttenuationSettings;

template<>
struct TStructOpsTypeTraits<FSoundAttenuationSettings> : public TStructOpsTypeTraitsBase2<FSoundAttenuationSettings>
{
	enum 
	{
		WithPostSerialize = true,
	};
};

/** 
 * Defines how a sound changes volume with distance to the listener
 */
UCLASS(BlueprintType, hidecategories=Object, editinlinenew, MinimalAPI)
class USoundAttenuation : public UObject
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Settings, meta = (CustomizeProperty))
	FSoundAttenuationSettings Attenuation;
};
