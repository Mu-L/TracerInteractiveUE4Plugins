// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

/**
 * Keyframe reduction algorithm that removes keys which are linear interpolations of surrounding keys, as
 * well as choosing the best bitwise compression for each track independently.
 *
 */

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimSequence.h"
#include "AnimationUtils.h"
#include "Animation/AnimCompress_RemoveLinearKeys.h"
#include "AnimCompress_PerTrackCompression.generated.h"

UCLASS(hidecategories=AnimCompress)
class UAnimCompress_PerTrackCompression : public UAnimCompress_RemoveLinearKeys
{
	GENERATED_UCLASS_BODY()

	/** Maximum threshold to use when replacing a component with zero. Lower values retain more keys, but yield less compression. */
	UPROPERTY(EditAnywhere, Category=PerTrack)
	float MaxZeroingThreshold;

	/** Maximum position difference to use when testing if an animation key may be removed. Lower values retain more keys, but yield less compression. */
	UPROPERTY(EditAnywhere, Category=PerTrack)
	float MaxPosDiffBitwise;

	/** Maximum angle difference to use when testing if an animation key may be removed. Lower values retain more keys, but yield less compression. */
	UPROPERTY(EditAnywhere, Category=PerTrack)
	float MaxAngleDiffBitwise;

	/** Maximum position difference to use when testing if an animation key may be removed. Lower values retain more keys, but yield less compression. */
	UPROPERTY(EditAnywhere, Category=PerTrack)
	float MaxScaleDiffBitwise;

	/** Which encoding formats is the per-track compressor allowed to try on rotation keys */
	UPROPERTY(EditAnywhere, Category=PerTrack)
	TArray<TEnumAsByte<enum AnimationCompressionFormat> > AllowedRotationFormats;

	/** Which encoding formats is the per-track compressor allowed to try on translation keys */
	UPROPERTY(EditAnywhere, Category=PerTrack)
	TArray<TEnumAsByte<enum AnimationCompressionFormat> > AllowedTranslationFormats;

	/** Which encoding formats is the per-track compressor allowed to try on scale keys */
	UPROPERTY(EditAnywhere, Category=PerTrack)
	TArray<TEnumAsByte<enum AnimationCompressionFormat> > AllowedScaleFormats;

	/** If true, resample the animation to ResampleFramerate frames per second */
	UPROPERTY(EditAnywhere, Category=Resampling)
	uint32 bResampleAnimation:1;

	/** When bResampleAnimation is true, this defines the desired framerate */
	UPROPERTY(EditAnywhere, Category=Resampling, meta=(ClampMin = "1.0", ClampMax = "30.0", editcondition = "bResampleAnimation"))
	float ResampledFramerate;

	/** Animations with fewer keys than MinKeysForResampling will not be resampled. */
	UPROPERTY(EditAnywhere, Category=Resampling)
	int32 MinKeysForResampling;

	/** If true, adjust the error thresholds based on the 'height' within the skeleton */
	UPROPERTY(EditAnywhere, Category=AdaptiveError)
	uint32 bUseAdaptiveError:1;

	/** If true, uses MinEffectorDiff as the threhsold for end effectors */
	UPROPERTY(EditAnywhere, Category=AdaptiveError)
	uint32 bUseOverrideForEndEffectors:1;

	/** A bias added to the track height before using it to calculate the adaptive error */
	UPROPERTY(EditAnywhere, Category=AdaptiveError)
	int32 TrackHeightBias;

	/**
	 * Reduces the error tolerance the further up the tree that a key occurs
	 * EffectiveErrorTolerance = Max(BaseErrorTolerance / Power(ParentingDivisor, Max(Height+Bias,0) * ParentingDivisorExponent), ZeroingThreshold)
	 * Only has an effect bUseAdaptiveError is true
	 */
	UPROPERTY(EditAnywhere, Category=AdaptiveError, meta=(ClampMin = "1.0"))
	float ParentingDivisor;

	/**
	 * Reduces the error tolerance the further up the tree that a key occurs
	 * EffectiveErrorTolerance = Max(BaseErrorTolerance / Power(ParentingDivisor, Max(Height+Bias,0) * ParentingDivisorExponent), ZeroingThreshold)
	 * Only has an effect bUseAdaptiveError is true
	 */
	UPROPERTY(EditAnywhere, Category=AdaptiveError, meta=(ClampMin = "0.1"))
	float ParentingDivisorExponent;

	/**
	 * If true, the adaptive error system will determine how much error to allow for each track, based on the
	 * error introduced in end effectors due to errors in the track.
	 */
	UPROPERTY(EditAnywhere, Category=AdaptiveError2)
	uint32 bUseAdaptiveError2:1;

	/**
	 * This ratio determines how much error in end effector rotation can come from a given track's rotation error or translation error.
	 * If 1, all of it must come from rotation error, if 0.5, half can come from each, and if 0.0, all must come from translation error.
	 */
	UPROPERTY(EditAnywhere, Category=AdaptiveError2, meta=(ClampMin = "0.0", ClampMax = "1.0"))
	float RotationErrorSourceRatio;

	/**
	 * This ratio determines how much error in end effector translation can come from a given track's rotation error or translation error.
	 * If 1, all of it must come from rotation error, if 0.5, half can come from each, and if 0.0, all must come from translation error.
	 */
	UPROPERTY(EditAnywhere, Category=AdaptiveError2, meta=(ClampMin = "0.0", ClampMax = "1.0"))
	float TranslationErrorSourceRatio;

	/**
	 * This ratio determines how much error in end effector scale can come from a given track's rotation error or scale error.
	 * If 1, all of it must come from rotation error, if 0.5, half can come from each, and if 0.0, all must come from scale error.
	 */
	UPROPERTY(EditAnywhere, Category=AdaptiveError2, meta=(ClampMin = "0.0", ClampMax = "1.0"))
	float ScaleErrorSourceRatio;

	/**
	 * A fraction that determines how much of the total error budget can be introduced by any particular track
	 */
	UPROPERTY(EditAnywhere, Category=AdaptiveError2, meta=(ClampMin = "0.0", ClampMax = "1.0"))
	float MaxErrorPerTrackRatio;

	/**
	 * How big of a perturbation should be made when probing error propagation
	 */
	UPROPERTY()
	float PerturbationProbeSize;


public:
	/**
	 * Cached metastructures used within DoReduction, tied to a particular sequence and mesh
	 */
	struct FPerTrackCachedInfo* PerReductionCachedData;

protected:
	//~ Begin UAnimCompress Interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void DoReduction(const FCompressibleAnimData& CompressibleAnimData, FCompressibleAnimDataResult& OutResult) override;
	virtual void PopulateDDCKey(FArchive& Ar) override;
#endif // WITH_EDITOR
	//~ Begin UAnimCompress Interface

#if WITH_EDITOR
	//~ Begin UAnimCompress_RemoveLinearKeys Interface
	virtual void CompressUsingUnderlyingCompressor(
		const FCompressibleAnimData& CompressibleAnimData,
		FCompressibleAnimDataResult& OutCompressedData,
		const TArray<FTranslationTrack>& TranslationData,
		const TArray<FRotationTrack>& RotationData,
		const TArray<FScaleTrack>& ScaleData,
		const bool bFinalPass) override;

#if USE_SEGMENTING_CONTEXT
	virtual void CompressUsingUnderlyingCompressor(
		UAnimSequence& AnimSeq,
		const TArray<FBoneData>& BoneData,
		TArray<FAnimSegmentContext>& RawSegments,
		const bool bFinalPass) override;
#endif

	/**
	 * Performs the per track compression optimization for a single segment.
	 * This can be called from multiple threads concurrently.
	 */
	void OptimizeSegmentTracks(struct FOptimizeSegmentTracksContext& Context) const;

	virtual void FilterBeforeMainKeyRemoval(
		const FCompressibleAnimData& CompressibleAnimData,
		TArray<FTranslationTrack>& TranslationData,
		TArray<FRotationTrack>& RotationData,
		TArray<FScaleTrack>& ScaleData) override;
	//~ End UAnimCompress_RemoveLinearKeys Interface

	/**
	* Structure to hold the track format information that we calculate is most optimal.
	*/
	struct FPerTrackFormat
	{
		AnimationCompressionFormat RotationFormat;
		AnimationCompressionFormat TranslationFormat;
		AnimationCompressionFormat ScaleFormat;

		bool bHasRotationTimeMarkers;
		bool bHasTranslationTimeMarkers;
		bool bHasScaleTimeMarkers;

		FTrackKeyFlags RotationKeyFlags;
		FTrackKeyFlags TranslationKeyFlags;
		FTrackKeyFlags ScaleKeyFlags;
	};

	static void PackTranslationKey(TArray<uint8>& ByteStream, AnimationCompressionFormat Format, const FVector& Key, const float* Mins, const float* Ranges, const struct FPerTrackFormat& TrackFormat);
	static void PackRotationKey(TArray<uint8>& ByteStream, AnimationCompressionFormat Format, const FQuat& Key, const float* Mins, const float* Ranges, const struct FPerTrackFormat& TrackFormat);
	static void PackScaleKey(TArray<uint8>& ByteStream, AnimationCompressionFormat Format, const FVector& Key, const float* Mins, const float* Ranges, const struct FPerTrackFormat& TrackFormat);

	friend struct FAsyncOptimizeSegmentTracksTaskGroupContext;
#endif // WITH_EDITOR
};



