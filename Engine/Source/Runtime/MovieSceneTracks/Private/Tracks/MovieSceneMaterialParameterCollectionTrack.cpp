// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "Tracks/MovieSceneMaterialParameterCollectionTrack.h"
#include "Materials/MaterialParameterCollection.h"
#include "Evaluation/MovieSceneMaterialParameterCollectionTemplate.h"

#define LOCTEXT_NAMESPACE "MovieSceneMaterialParameterCollectionTrack"

UMovieSceneMaterialParameterCollectionTrack::UMovieSceneMaterialParameterCollectionTrack(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	TrackTint = FColor(64,192,64,65);
#endif
}

UMovieSceneSection* UMovieSceneMaterialParameterCollectionTrack::CreateNewSection()
{
	UMovieSceneSection* NewSection =  NewObject<UMovieSceneParameterSection>(this, UMovieSceneParameterSection::StaticClass(), NAME_None, RF_Transactional);
	NewSection->SetRange(TRange<FFrameNumber>::All());
	return NewSection;
}

FMovieSceneEvalTemplatePtr UMovieSceneMaterialParameterCollectionTrack::CreateTemplateForSection(const UMovieSceneSection& InSection) const
{
	return FMovieSceneMaterialParameterCollectionTemplate(*CastChecked<UMovieSceneParameterSection>(&InSection), *this);
}

#if WITH_EDITORONLY_DATA
FText UMovieSceneMaterialParameterCollectionTrack::GetDefaultDisplayName() const
{
	return LOCTEXT("DefaultTrackName", "Material Parameter Collection");
}
#endif

#undef LOCTEXT_NAMESPACE
