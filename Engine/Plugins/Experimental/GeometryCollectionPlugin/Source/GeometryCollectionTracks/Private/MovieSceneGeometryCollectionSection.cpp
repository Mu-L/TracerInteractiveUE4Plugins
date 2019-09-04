// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MovieSceneGeometryCollectionSection.h"
#include "MovieSceneGeometryCollectionTemplate.h"
#include "Logging/MessageLog.h"
#include "MovieScene.h"
#include "UObject/SequencerObjectVersion.h"
#include "MovieSceneTimeHelpers.h"

#define LOCTEXT_NAMESPACE "MovieSceneGeometryCollectionSection"

namespace
{
	float GeometryCollectionDeprecatedMagicNumber = TNumericLimits<float>::Lowest();
}

FMovieSceneGeometryCollectionParams::FMovieSceneGeometryCollectionParams()
{
	PlayRate = 1.f;
}

UMovieSceneGeometryCollectionSection::UMovieSceneGeometryCollectionSection( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{
	BlendType = EMovieSceneBlendType::Absolute;
	EvalOptions.EnableAndSetCompletionMode(EMovieSceneCompletionMode::RestoreState);

#if WITH_EDITOR

	PreviousPlayRate = Params.PlayRate;

#endif
}

TOptional<FFrameTime> UMovieSceneGeometryCollectionSection::GetOffsetTime() const
{
	return TOptional<FFrameTime>(Params.StartFrameOffset);
}

FMovieSceneEvalTemplatePtr UMovieSceneGeometryCollectionSection::GenerateTemplate() const
{
	return FMovieSceneGeometryCollectionSectionTemplate(*this);
}

FFrameNumber GetStartOffsetAtTrimTime(FQualifiedFrameTime TrimTime, const FMovieSceneGeometryCollectionParams& Params, FFrameNumber StartFrame, FFrameRate FrameRate)
{
	float AnimPlayRate = FMath::IsNearlyZero(Params.PlayRate) ? 1.0f : Params.PlayRate;
	float AnimPosition = (TrimTime.Time - StartFrame) / TrimTime.Rate * AnimPlayRate;
	float SeqLength = Params.GetSequenceLength() - FrameRate.AsSeconds(Params.StartFrameOffset + Params.EndFrameOffset) / AnimPlayRate;

	FFrameNumber NewOffset = FrameRate.AsFrameNumber(FMath::Fmod(AnimPosition, SeqLength));
	NewOffset += Params.StartFrameOffset;

	return NewOffset;
}


TOptional<TRange<FFrameNumber> > UMovieSceneGeometryCollectionSection::GetAutoSizeRange() const
{
	FFrameRate FrameRate = GetTypedOuter<UMovieScene>()->GetTickResolution();

	FFrameTime AnimationLength = Params.GetSequenceLength() * FrameRate;

	return TRange<FFrameNumber>(GetInclusiveStartFrame(), GetInclusiveStartFrame() + AnimationLength.FrameNumber);
}


void UMovieSceneGeometryCollectionSection::TrimSection(FQualifiedFrameTime TrimTime, bool bTrimLeft)
{
	SetFlags(RF_Transactional);

	if (TryModify())
	{
		if (bTrimLeft)
		{
			FFrameRate FrameRate = GetTypedOuter<UMovieScene>()->GetTickResolution();

			Params.StartFrameOffset = HasStartFrame() ? GetStartOffsetAtTrimTime(TrimTime, Params, GetInclusiveStartFrame(), FrameRate) : 0;
		}

		Super::TrimSection(TrimTime, bTrimLeft);
	}
}

UMovieSceneSection* UMovieSceneGeometryCollectionSection::SplitSection(FQualifiedFrameTime SplitTime)
{
	FFrameRate FrameRate = GetTypedOuter<UMovieScene>()->GetTickResolution();

	const FFrameNumber NewOffset = HasStartFrame() ? GetStartOffsetAtTrimTime(SplitTime, Params, GetInclusiveStartFrame(), FrameRate) : 0;

	UMovieSceneSection* NewSection = Super::SplitSection(SplitTime);
	if (NewSection != nullptr)
	{
		UMovieSceneGeometryCollectionSection* NewGeometrySection = Cast<UMovieSceneGeometryCollectionSection>(NewSection);
		NewGeometrySection->Params.StartFrameOffset = NewOffset;
	}
	return NewSection;
}


void UMovieSceneGeometryCollectionSection::GetSnapTimes(TArray<FFrameNumber>& OutSnapTimes, bool bGetSectionBorders) const
{
	Super::GetSnapTimes(OutSnapTimes, bGetSectionBorders);

	const FFrameRate   FrameRate  = GetTypedOuter<UMovieScene>()->GetTickResolution();
	const FFrameNumber StartFrame = GetInclusiveStartFrame();
	const FFrameNumber EndFrame   = GetExclusiveEndFrame() - 1; // -1 because we don't need to add the end frame twice

	const float AnimPlayRate     = FMath::IsNearlyZero(Params.PlayRate) ? 1.0f : Params.PlayRate;
	const float SeqLengthSeconds = Params.GetSequenceLength() - FrameRate.AsSeconds(Params.StartFrameOffset + Params.EndFrameOffset) / AnimPlayRate;

	FFrameTime SequenceFrameLength = SeqLengthSeconds * FrameRate;
	if (SequenceFrameLength.FrameNumber > 1)
	{
		// Snap to the repeat times
		FFrameTime CurrentTime = StartFrame;
		while (CurrentTime < EndFrame)
		{
			OutSnapTimes.Add(CurrentTime.FrameNumber);
			CurrentTime += SequenceFrameLength;
		}
	}
}

float UMovieSceneGeometryCollectionSection::MapTimeToAnimation(FFrameTime InPosition, FFrameRate InFrameRate) const
{
	FMovieSceneGeometryCollectionSectionTemplateParameters TemplateParams(Params, GetInclusiveStartFrame(), GetExclusiveEndFrame());
	return TemplateParams.MapTimeToAnimation(InPosition, InFrameRate);
}


#if WITH_EDITOR
void UMovieSceneGeometryCollectionSection::PreEditChange(UProperty* PropertyAboutToChange)
{
	// Store the current play rate so that we can compute the amount to compensate the section end time when the play rate changes
	PreviousPlayRate = Params.PlayRate;

	Super::PreEditChange(PropertyAboutToChange);
}

void UMovieSceneGeometryCollectionSection::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Adjust the duration automatically if the play rate changes
	if (PropertyChangedEvent.Property != nullptr &&
		PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(FMovieSceneGeometryCollectionParams, PlayRate))
	{
		float NewPlayRate = Params.PlayRate;

		if (!FMath::IsNearlyZero(NewPlayRate))
		{
			float CurrentDuration = MovieScene::DiscreteSize(GetRange());
			float NewDuration = CurrentDuration * (PreviousPlayRate / NewPlayRate);
			SetEndFrame( GetInclusiveStartFrame() + FMath::FloorToInt(NewDuration) );

			PreviousPlayRate = NewPlayRate;
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

#undef LOCTEXT_NAMESPACE 
