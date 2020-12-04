// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tracks/MovieSceneTransformTrack.h"
#include "MovieSceneCommonHelpers.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Compilation/MovieSceneSegmentCompiler.h"
#include "Compilation/MovieSceneTemplateInterrogation.h"
#include "Evaluation/MovieScenePropertyTemplates.h"


UMovieSceneTransformTrack::UMovieSceneTransformTrack(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	TrackTint = FColor(65, 173, 164, 65);
#endif

	SupportedBlendTypes = FMovieSceneBlendTypeField::All();

	EvalOptions.bEvaluateNearestSection_DEPRECATED = EvalOptions.bCanEvaluateNearestSection = true;
}

bool UMovieSceneTransformTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UMovieScene3DTransformSection::StaticClass();
}

UMovieSceneSection* UMovieSceneTransformTrack::CreateNewSection()
{
	return NewObject<UMovieScene3DTransformSection>(this, NAME_None, RF_Transactional);
}
