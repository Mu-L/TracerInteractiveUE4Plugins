// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tracks/MovieSceneCameraAnimTrack.h"
#include "Sections/MovieSceneCameraAnimSection.h"
#include "Templates/Casts.h"
#include "Camera/CameraAnim.h"
#include "Evaluation/PersistentEvaluationData.h"
#include "Evaluation/MovieSceneCameraAnimTemplate.h"
#include "Evaluation/MovieSceneEvaluationTrack.h"
#include "Evaluation/MovieSceneEvaluationTemplate.h"
#include "Compilation/MovieSceneCompilerRules.h"
#include "MovieScene.h"

#define LOCTEXT_NAMESPACE "MovieSceneCameraAnimTrack"


UMovieSceneSection* UMovieSceneCameraAnimTrack::AddNewCameraAnim(FFrameNumber KeyTime, UCameraAnim* CameraAnim)
{
	Modify();

	UMovieSceneCameraAnimSection* const NewSection = Cast<UMovieSceneCameraAnimSection>(CreateNewSection());
	if (NewSection)
	{
		FFrameTime AnimDurationFrames = CameraAnim->AnimLength * GetTypedOuter<UMovieScene>()->GetTickResolution();
		NewSection->InitialPlacement(CameraAnimSections, KeyTime, AnimDurationFrames.FrameNumber.Value, SupportsMultipleRows());
		NewSection->AnimData.CameraAnim = CameraAnim;

		AddSection(*NewSection);
	}

	return NewSection;
}

/* UMovieSceneTrack interface
*****************************************************************************/

FMovieSceneEvalTemplatePtr UMovieSceneCameraAnimTrack::CreateTemplateForSection(const UMovieSceneSection& InSection) const
{
	const UMovieSceneCameraAnimSection* CameraAnimSection = CastChecked<const UMovieSceneCameraAnimSection>(&InSection);
	if (CameraAnimSection->AnimData.CameraAnim)
	{
		return FMovieSceneCameraAnimSectionTemplate(*CameraAnimSection);
	}
	return FMovieSceneEvalTemplatePtr();
}

const TArray<UMovieSceneSection*>& UMovieSceneCameraAnimTrack::GetAllSections() const
{
	return CameraAnimSections;
}

bool UMovieSceneCameraAnimTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UMovieSceneCameraAnimSection::StaticClass();
}

UMovieSceneSection* UMovieSceneCameraAnimTrack::CreateNewSection()
{
	return NewObject<UMovieSceneCameraAnimSection>(this, NAME_None, RF_Transactional);
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

void UMovieSceneCameraAnimTrack::RemoveSectionAt(int32 SectionIndex)
{
	CameraAnimSections.RemoveAt(SectionIndex);
}


bool UMovieSceneCameraAnimTrack::IsEmpty() const
{
	return CameraAnimSections.Num() == 0;
}




#if WITH_EDITORONLY_DATA
FText UMovieSceneCameraAnimTrack::GetDisplayName() const
{
	return LOCTEXT("TrackName", "Camera Anim");
}
#endif

void UMovieSceneCameraAnimTrack::GetCameraAnimSectionsAtTime(FFrameNumber Time, TArray<UMovieSceneCameraAnimSection*>& OutSections)
{
	OutSections.Empty();

	for (auto Section : CameraAnimSections)
	{
		UMovieSceneCameraAnimSection* const CASection = dynamic_cast<UMovieSceneCameraAnimSection*>(Section);
		if (CASection && CASection->GetRange().Contains(Time))
		{
			OutSections.Add(CASection);
		}
	}
}


#undef LOCTEXT_NAMESPACE
