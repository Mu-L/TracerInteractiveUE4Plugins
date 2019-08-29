// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "Tracks/MovieSceneVectorTrack.h"
#include "Sections/MovieSceneVectorSection.h"
#include "Evaluation/MovieScenePropertyTemplates.h"


UMovieSceneVectorTrack::UMovieSceneVectorTrack( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{
	NumChannelsUsed = 0;
	SupportedBlendTypes = FMovieSceneBlendTypeField::All();
}


UMovieSceneSection* UMovieSceneVectorTrack::CreateNewSection()
{
	UMovieSceneVectorSection* NewSection = NewObject<UMovieSceneVectorSection>(this, UMovieSceneVectorSection::StaticClass(), NAME_None, RF_Transactional);
	NewSection->SetChannelsUsed(NumChannelsUsed);
	return NewSection;
}


FMovieSceneEvalTemplatePtr UMovieSceneVectorTrack::CreateTemplateForSection(const UMovieSceneSection& InSection) const
{
	return FMovieSceneVectorPropertySectionTemplate(*CastChecked<UMovieSceneVectorSection>(&InSection), *this);
}
