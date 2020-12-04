// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sections/MovieSceneCameraCutSection.h"

#include "MovieScene.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "MovieScene.h"
#include "IMovieScenePlayer.h"
#include "Camera/CameraComponent.h"
#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"
#include "Evaluation/MovieSceneEvalTemplate.h"
#include "EntitySystem/MovieSceneEntityManager.h"
#include "EntitySystem/MovieSceneEntityBuilder.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/Interrogation/MovieSceneInterrogationLinker.h"
#include "EntitySystem/Interrogation/MovieSceneInterrogatedPropertyInstantiator.h"
#include "EntitySystem/TrackInstance/MovieSceneTrackInstanceSystem.h"
#include "Systems/MovieSceneComponentTransformSystem.h"
#include "TrackInstances/MovieSceneCameraCutTrackInstance.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneTransformTrack.h"
#include "UObject/LinkerLoad.h"

/* UMovieSceneCameraCutSection interface
 *****************************************************************************/

UMovieSceneCameraCutSection::UMovieSceneCameraCutSection(const FObjectInitializer& Init)
	: Super(Init)
{
	EvalOptions.EnableAndSetCompletionMode
		(GetLinkerCustomVersion(FSequencerObjectVersion::GUID) < FSequencerObjectVersion::WhenFinishedDefaultsToProjectDefault ? 
		 EMovieSceneCompletionMode::RestoreState : 
		 EMovieSceneCompletionMode::ProjectDefault);

	SetBlendType(EMovieSceneBlendType::Absolute);
}

void UMovieSceneCameraCutSection::OnBindingsUpdated(const TMap<FGuid, FGuid>& OldGuidToNewGuidMap)
{
	if (OldGuidToNewGuidMap.Contains(CameraBindingID.GetGuid()))
	{
		Modify();

		CameraBindingID.SetGuid(OldGuidToNewGuidMap[CameraBindingID.GetGuid()]);
	}
}

void UMovieSceneCameraCutSection::GetReferencedBindings(TArray<FGuid>& OutBindings)
{
	OutBindings.Add(CameraBindingID.GetGuid());
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

UCameraComponent* UMovieSceneCameraCutSection::GetFirstCamera(IMovieScenePlayer& Player, FMovieSceneSequenceID SequenceID) const
{
	if (CameraBindingID.GetSequenceID().IsValid())
	{
		// Ensure that this ID is resolvable from the root, based on the current local sequence ID
		FMovieSceneObjectBindingID RootBindingID = CameraBindingID.ResolveLocalToRoot(SequenceID, Player);
		SequenceID = RootBindingID.GetSequenceID();
	}

	for (TWeakObjectPtr<>& WeakObject : Player.FindBoundObjects(CameraBindingID.GetGuid(), SequenceID))
	{
		if (UObject* Object = WeakObject .Get())
		{
			UCameraComponent* Camera = MovieSceneHelpers::CameraComponentFromRuntimeObject(Object);
			if (Camera)
			{
				return Camera;
			}
		}
	}

	return nullptr;
}

#if WITH_EDITOR

void UMovieSceneCameraCutSection::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UMovieSceneCameraCutSection, SectionRange))
	{
		if (UMovieSceneCameraCutTrack* Track = GetTypedOuter<UMovieSceneCameraCutTrack>())
		{
			Track->OnSectionMoved(*this, EPropertyChangeType::ValueSet);
		}
	}
}

#endif

void UMovieSceneCameraCutSection::ImportEntityImpl(UMovieSceneEntitySystemLinker* EntityLinker, const FEntityImportParams& Params, FImportedEntity* OutImportedEntity)
{
	using namespace UE::MovieScene;

	FMovieSceneTrackInstanceComponent TrackInstance { this, UMovieSceneCameraCutTrackInstance::StaticClass() };

	OutImportedEntity->AddBuilder(
		FEntityBuilder()
		.AddTag(FBuiltInComponentTypes::Get()->Tags.Master)
		.Add(FBuiltInComponentTypes::Get()->TrackInstance, TrackInstance)
	);
}

void UMovieSceneCameraCutSection::ComputeInitialCameraCutTransform()
{
	using namespace UE::MovieScene;

	// Clear the compiled transform value.
	bHasInitialCameraCutTransform = false;

	// Is there even an initial time for us to compute a transform?
	if (!GetRange().HasLowerBound())
	{
		return;
	}

	// Find the transform track for our bound camera.
	UMovieScene3DTransformTrack* CameraTransformTrack = nullptr;
	if (CameraBindingID.IsValid())
	{
		UMovieScene* MovieScene = GetTypedOuter<UMovieScene>();
		check(MovieScene);

		for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
		{
			if (Binding.GetObjectGuid() == CameraBindingID.GetGuid())
			{
				for (UMovieSceneTrack* Track : Binding.GetTracks())
				{
					CameraTransformTrack = Cast<UMovieScene3DTransformTrack>(Track);
					if (CameraTransformTrack)
					{
						break;
					}
				}
			}
		}
	}

	// Does the bound camera have a transform track?
	if (CameraTransformTrack == nullptr)
	{
		return;
	}

	// Ok, let's evaluate the transform track at our start time.
	FSystemInterrogator Interrogator;

	TGuardValue<FEntityManager*> DebugVizGuard(GEntityManagerForDebuggingVisualizers, &Interrogator.GetLinker()->EntityManager);

	Interrogator.ImportTrack(CameraTransformTrack, FInterrogationChannel::Default());

	Interrogator.AddInterrogation(GetTrueRange().GetLowerBoundValue());

	Interrogator.Update();

	TArray<FTransform> TempTransforms;
	Interrogator.QueryWorldSpaceTransforms(FInterrogationChannel::Default(), TempTransforms);

	if (TempTransforms.Num() > 0)
	{
		// Store it so we can place it on our imported entities.
		InitialCameraCutTransform = TempTransforms[0];
		bHasInitialCameraCutTransform = true;
	}
}
