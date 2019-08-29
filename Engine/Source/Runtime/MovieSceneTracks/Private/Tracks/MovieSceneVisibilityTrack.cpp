// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "Tracks/MovieSceneVisibilityTrack.h"
#include "Sections/MovieSceneBoolSection.h"
#include "Evaluation/MovieSceneVisibilityTemplate.h"

#define LOCTEXT_NAMESPACE "MovieSceneVisibilityTrack"


UMovieSceneVisibilityTrack::UMovieSceneVisibilityTrack(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{}

UMovieSceneSection* UMovieSceneVisibilityTrack::CreateNewSection()
{
	UMovieSceneBoolSection* NewBoolSection = Cast<UMovieSceneBoolSection>(Super::CreateNewSection());

#if WITH_EDITORONLY_DATA
	if (NewBoolSection)
	{
		NewBoolSection->SetIsExternallyInverted(true);
	}
#endif

	return NewBoolSection;
}

FMovieSceneEvalTemplatePtr UMovieSceneVisibilityTrack::CreateTemplateForSection(const UMovieSceneSection& InSection) const
{
	return FMovieSceneVisibilitySectionTemplate(*CastChecked<const UMovieSceneBoolSection>(&InSection), *this);
}

void UMovieSceneVisibilityTrack::PostLoad()
{
#if WITH_EDITORONLY_DATA
	for (UMovieSceneSection* Section : GetAllSections())
	{
		CastChecked<UMovieSceneBoolSection>(Section)->SetIsExternallyInverted(true);
	}
#endif

	Super::PostLoad();
}

#if WITH_EDITORONLY_DATA

FText UMovieSceneVisibilityTrack::GetDisplayName() const
{
	return LOCTEXT("DisplayName", "Visibility");
}

#endif


#undef LOCTEXT_NAMESPACE
