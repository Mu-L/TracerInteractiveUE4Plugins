// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "Curves/KeyHandle.h"
#include "MovieSceneSequenceID.h"
#include "MovieSceneSection.h"
#include "Evaluation/MovieSceneSectionParameters.h"
#include "Evaluation/MovieSceneSequenceHierarchy.h"
#include "Evaluation/MovieSceneEvaluationOperand.h"
#include "MovieSceneSubSection.generated.h"

class UMovieSceneSequence;
struct FMovieSceneEvaluationTemplate;
struct FMovieSceneTrackCompilerArgs;

DECLARE_DELEGATE_OneParam(FOnSequenceChanged, UMovieSceneSequence* /*Sequence*/);

struct FSubSequenceInstanceDataParams
{
	/** The ID of the sequence instance that is being generated */
	FMovieSceneSequenceID InstanceSequenceID;

	/** The object binding ID in which the section to be generated resides */
	FMovieSceneEvaluationOperand Operand;
};

/**
 * Implements a section in sub-sequence tracks.
 */
UCLASS(BlueprintType)
class MOVIESCENE_API UMovieSceneSubSection
	: public UMovieSceneSection
{
	GENERATED_BODY()

public:

	/** Default constructor. */
	UMovieSceneSubSection();

	/**
	 * Get the sequence that is assigned to this section.
	 *
	 * @return The sequence.
	 * @see SetSequence
	 */
	UFUNCTION(BlueprintPure, Category = "Movie Scene Section")
	UMovieSceneSequence* GetSequence() const;

	/**
	 * Get the path name to this sub section from the outer moviescene
	 */
	FString GetPathNameInMovieScene() const;

	/**
	 * Get this sub section's sequence ID
	 */
	FMovieSceneSequenceID GetSequenceID() const;

	/** Generate subsequence data */
	virtual FMovieSceneSubSequenceData GenerateSubSequenceData(const FSubSequenceInstanceDataParams& Params) const;

public:

	/**
	 * Get the transform that converts time from this section's time-base to its inner sequence's
	 */
	FMovieSceneSequenceTransform OuterToInnerTransform() const;

	/**
	 * Sets the sequence played by this section.
	 *
	 * @param Sequence The sequence to play.
	 * @see GetSequence
	 */
	UFUNCTION(BlueprintCallable, Category = "Movie Scene Section")
	void SetSequence(UMovieSceneSequence* Sequence);

	/** Prime this section as the one and only recording section */
	void SetAsRecording(bool bRecord);

	/** Get the section we are recording to */
	static UMovieSceneSubSection* GetRecordingSection();

	/** Get the actor we are targeting for recording */
	static AActor* GetActorToRecord();

	/** Check if we are primed for recording */
	static bool IsSetAsRecording();

	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	/** Delegate to fire when our sequence is changed in the property editor */
	FOnSequenceChanged& OnSequenceChanged() { return OnSequenceChangedDelegate; }
#endif

	/** Get the name of the sequence we are going to try to record to */
	const FString& GetTargetSequenceName() const
	{
		return TargetSequenceName;
	}

	/** Set the name of the sequence we are going to try to record to */
	void SetTargetSequenceName(const FString& Name)
	{
		TargetSequenceName = Name;
	}

	/** Get the path of the sequence we are going to try to record to */
	const FString& GetTargetPathToRecordTo() const
	{
		return TargetPathToRecordTo.Path;
	}

	/** Set the path of the sequence we are going to try to record to */
	void SetTargetPathToRecordTo(const FString& Path)
	{
		TargetPathToRecordTo.Path = Path;
	}

	/** Set the target actor to record */
	void SetActorToRecord(AActor* InActorToRecord)
	{
		ActorToRecord = InActorToRecord;
	}

	FFrameNumber MapTimeToSectionFrame(FFrameTime InPosition) const;

public:

	//~ UMovieSceneSection interface
	virtual TOptional<TRange<FFrameNumber> > GetAutoSizeRange() const override;
	virtual UMovieSceneSection* SplitSection( FQualifiedFrameTime SplitTime ) override;
	virtual void TrimSection( FQualifiedFrameTime TrimTime, bool bTrimLeft ) override;
	virtual TOptional<FFrameTime> GetOffsetTime() const override { return TOptional<FFrameTime>(FFrameTime(Parameters.StartFrameOffset)); }

public:

	UPROPERTY(BlueprintReadWrite, EditAnywhere, BlueprintReadWrite, Category="General", meta=(ShowOnlyInnerProperties))
	FMovieSceneSectionParameters Parameters;

private:

	UPROPERTY()
	float StartOffset_DEPRECATED;

	UPROPERTY()
	float TimeScale_DEPRECATED;

	UPROPERTY()
	float PrerollTime_DEPRECATED;

protected:

	/**
	 * Movie scene being played by this section.
	 *
	 * @todo Sequencer: Should this be lazy loaded?
	 */
	UPROPERTY(EditAnywhere, Category="Sequence")
	UMovieSceneSequence* SubSequence;

	/** Target actor to record */
	UPROPERTY(EditAnywhere, Category="Sequence Recording")
	TLazyObjectPtr<AActor> ActorToRecord;

	/** Target name of sequence to try to record to (will record automatically to another if this already exists) */
	UPROPERTY(EditAnywhere, Category="Sequence Recording")
	FString TargetSequenceName;

	/** Target path of sequence to record to */
	UPROPERTY(EditAnywhere, Category="Sequence Recording", meta=(ContentDir))
	FDirectoryPath TargetPathToRecordTo;

	/** Keep track of our constructed recordings */
	static TWeakObjectPtr<UMovieSceneSubSection> TheRecordingSection;

#if WITH_EDITOR
	/** Delegate to fire when our sequence is changed in the property editor */
	FOnSequenceChanged OnSequenceChangedDelegate;
#endif
};
