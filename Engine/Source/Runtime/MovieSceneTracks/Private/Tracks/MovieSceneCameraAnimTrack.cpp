// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "Tracks/MovieSceneCameraAnimTrack.h"
#include "Sections/MovieSceneCameraAnimSection.h"
#include "Templates/Casts.h"
#include "Camera/CameraAnim.h"
#include "Evaluation/PersistentEvaluationData.h"
#include "Evaluation/MovieSceneCameraAnimTemplate.h"
#include "Evaluation/MovieSceneEvaluationTrack.h"
#include "Evaluation/MovieSceneEvaluationTemplate.h"
#include "Compilation/MovieSceneCompilerRules.h"

#define LOCTEXT_NAMESPACE "MovieSceneCameraAnimTrack"


void UMovieSceneCameraAnimTrack::AddNewCameraAnim(float KeyTime, UCameraAnim* CameraAnim)
{
	UMovieSceneCameraAnimSection* const NewSection = Cast<UMovieSceneCameraAnimSection>(CreateNewSection());
	if (NewSection)
	{
		NewSection->InitialPlacement(CameraAnimSections, KeyTime, KeyTime + CameraAnim->AnimLength, SupportsMultipleRows());
		NewSection->AnimData.CameraAnim = CameraAnim;

		AddSection(*NewSection);
	}
}

/* UMovieSceneTrack interface
*****************************************************************************/


FMovieSceneTrackSegmentBlenderPtr UMovieSceneCameraAnimTrack::GetTrackSegmentBlender() const
{
	return FMovieSceneAdditiveCameraTrackBlender();
}


const TArray<UMovieSceneSection*>& UMovieSceneCameraAnimTrack::GetAllSections() const
{
	return CameraAnimSections;
}


UMovieSceneSection* UMovieSceneCameraAnimTrack::CreateNewSection()
{
	return NewObject<UMovieSceneCameraAnimSection>(this);
}


void UMovieSceneCameraAnimTrack::RemoveAllAnimationData()
{
	CameraAnimSections.Empty();
}


bool UMovieSceneCameraAnimTrack::HasSection(const UMovieSceneSection& Section) const
{
	return CameraAnimSections.Contains(&Section);
}


void UMovieSceneCameraAnimTrack::AddSection(UMovieSceneSection& Section)
{
	CameraAnimSections.Add(&Section);
}


void UMovieSceneCameraAnimTrack::RemoveSection(UMovieSceneSection& Section)
{
	CameraAnimSections.Remove(&Section);
}


bool UMovieSceneCameraAnimTrack::IsEmpty() const
{
	return CameraAnimSections.Num() == 0;
}


TRange<float> UMovieSceneCameraAnimTrack::GetSectionBoundaries() const
{
	TArray<TRange<float>> Bounds;

	for (auto Section : CameraAnimSections)
	{
		Bounds.Add(Section->GetRange());
	}

	return TRange<float>::Hull(Bounds);
}




#if WITH_EDITORONLY_DATA
FText UMovieSceneCameraAnimTrack::GetDisplayName() const
{
	return LOCTEXT("TrackName", "Camera Anim");
}
#endif

void UMovieSceneCameraAnimTrack::GetCameraAnimSectionsAtTime(float Time, TArray<UMovieSceneCameraAnimSection*>& OutSections)
{
	OutSections.Empty();

	for (auto Section : CameraAnimSections)
	{
		UMovieSceneCameraAnimSection* const CASection = dynamic_cast<UMovieSceneCameraAnimSection*>(Section);
		if (CASection && CASection->IsTimeWithinSection(Time))
		{
			OutSections.Add(CASection);
		}
	}
}


#undef LOCTEXT_NAMESPACE
