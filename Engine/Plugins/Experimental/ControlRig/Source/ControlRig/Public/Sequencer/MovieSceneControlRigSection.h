// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Sections/MovieSceneSubSection.h"
#include "ControlRig.h"
#include "MovieSceneSequencePlayer.h"
#include "Animation/AnimData/BoneMaskFilter.h"
#include "MovieSceneControlRigSection.generated.h"


class UControlRigSequence;
class UMovieSceneControlRigSection;

/**
 * Movie scene section that controls animation controller animation
 */
UCLASS()
class CONTROLRIG_API UMovieSceneControlRigSection : public UMovieSceneSubSection
{
	GENERATED_BODY()

public:
	/** Blend this track in additively (using the reference pose as a base) */
	UPROPERTY(EditAnywhere, Category = "Animation")
	bool bAdditive;

	/** Only apply bones that are in the filter */
	UPROPERTY(EditAnywhere, Category = "Animation")
	bool bApplyBoneFilter;

	/** Per-bone filter to apply to our animation */
	UPROPERTY(EditAnywhere, Category = "Animation", meta=(EditCondition=bApplyBoneFilter))
	FInputBlendPose BoneFilter;

	/** The weight curve for this animation controller section */
	UPROPERTY()
	FMovieSceneFloatChannel Weight;

public:

	UMovieSceneControlRigSection();

	//~ UMovieSceneSubSection interface
	virtual void OnDilated(float DilationFactor, FFrameNumber Origin) override;
	virtual FMovieSceneSubSequenceData GenerateSubSequenceData(const FSubSequenceInstanceDataParams& Params) const override;
};
