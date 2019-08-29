// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "Sections/MovieSceneCameraCutSection.h"

#include "MovieScene.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneTransformTrack.h"
#include "MovieScene.h"
#include "Evaluation/MovieSceneEvaluationTrack.h"
#include "Evaluation/MovieSceneCameraCutTemplate.h"
#include "Compilation/MovieSceneTemplateInterrogation.h"

/* UMovieSceneCameraCutSection interface
 *****************************************************************************/

FMovieSceneEvalTemplatePtr UMovieSceneCameraCutSection::GenerateTemplate() const
{
	TOptional<FTransform> CutTransform;

	UMovieScene* MovieScene = GetTypedOuter<UMovieScene>();
	check(MovieScene);

	for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
	{
		if (Binding.GetObjectGuid() == CameraBindingID.GetGuid())
		{
			for (UMovieSceneTrack* Track : Binding.GetTracks())
			{
				UMovieScene3DTransformTrack* TransformTrack = Cast<UMovieScene3DTransformTrack>(Track);
				if (TransformTrack)
				{
					// Extract the transform
					FMovieSceneEvaluationTrack TransformTrackTemplate = TransformTrack->GenerateTrackTemplate();
					FMovieSceneContext Context = FMovieSceneEvaluationRange(GetInclusiveStartFrame(), MovieScene->GetTickResolution());

					FMovieSceneInterrogationData Container;
					TransformTrackTemplate.Interrogate(Context, Container);

					for (auto It = Container.Iterate<FTransform>(UMovieScene3DTransformTrack::GetInterrogationKey()); It; ++It)
					{
						CutTransform = *It;
						break;
					}
				}
			}
		}
	}

	return FMovieSceneCameraCutSectionTemplate(*this, CutTransform);
}

void UMovieSceneCameraCutSection::OnBindingsUpdated(const TMap<FGuid, FGuid>& OldGuidToNewGuidMap)
{
	if (OldGuidToNewGuidMap.Contains(CameraBindingID.GetGuid()))
	{
		CameraBindingID.SetGuid(OldGuidToNewGuidMap[CameraBindingID.GetGuid()]);
	}
}

void UMovieSceneCameraCutSection::PostLoad()
{
	Super::PostLoad();

	if (CameraGuid_DEPRECATED.IsValid())
	{
		if (!CameraBindingID.IsValid())
		{
			CameraBindingID = FMovieSceneObjectBindingID(CameraGuid_DEPRECATED, MovieSceneSequenceID::Root, EMovieSceneObjectBindingSpace::Local);
		}
		CameraGuid_DEPRECATED.Invalidate();
	}
}
