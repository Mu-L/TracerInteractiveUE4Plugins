// Copyright Epic Games, Inc. All Rights Reserved.

#include "EntitySystem/MovieSceneEntityInstantiatorSystem.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/MovieSceneEntityManager.h"
#include "EntitySystem/BuiltInComponentTypes.h"

#include "MovieSceneObjectBindingID.h"

UMovieSceneEntityInstantiatorSystem::UMovieSceneEntityInstantiatorSystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	Phase = UE::MovieScene::ESystemPhase::Instantiation;
}

void UMovieSceneEntityInstantiatorSystem::UnlinkStaleObjectBindings(UE::MovieScene::TComponentTypeID<FGuid> BindingType)
{
	using namespace UE::MovieScene;

	check(Linker);

	FInstanceRegistry* InstanceRegistry = Linker->GetInstanceRegistry();
	if (!InstanceRegistry->HasInvalidatedBindings())
	{
		return;
	}

	FBuiltInComponentTypes* Components = FBuiltInComponentTypes::Get();

	TArray<FMovieSceneEntityID> StaleEntities;

	auto GatherStaleBindings = [InstanceRegistry, &StaleEntities](FMovieSceneEntityID EntityID, FInstanceHandle InstanceHandle, const FGuid& InObjectBindingID)
	{
		if (InstanceRegistry->IsBindingInvalidated(InObjectBindingID, InstanceHandle))
		{
			StaleEntities.Add(EntityID);
		}
	};

	// Gather all newly instanced entities with an object binding ID
	FEntityTaskBuilder()
	.ReadEntityIDs()
	.Read(Components->InstanceHandle)
	.Read(BindingType)
	.FilterAll({ Components->Tags.ImportedEntity })
	.FilterNone({ Components->Tags.NeedsUnlink })
	.Iterate_PerEntity(&Linker->EntityManager, GatherStaleBindings);

	for (FMovieSceneEntityID Entity : StaleEntities)
	{
		// Tag this as needs link, and all children as needs unlink
		Linker->EntityManager.AddComponent(Entity, Components->Tags.NeedsLink);
		Linker->EntityManager.AddComponent(Entity, Components->Tags.NeedsUnlink, EEntityRecursion::Children);
	}
}


void UMovieSceneEntityInstantiatorSystem::UnlinkStaleObjectBindings(UE::MovieScene::TComponentTypeID<FMovieSceneObjectBindingID> BindingType)
{
	using namespace UE::MovieScene;

	check(Linker);

	FInstanceRegistry* InstanceRegistry = Linker->GetInstanceRegistry();
	if (!InstanceRegistry->HasInvalidatedBindings())
	{
		return;
	}

	FBuiltInComponentTypes* Components = FBuiltInComponentTypes::Get();

	TArray<FMovieSceneEntityID> StaleEntities;

	auto GatherStaleBindings = [InstanceRegistry, &StaleEntities](FMovieSceneEntityID EntityID, FInstanceHandle InstanceHandle, FMovieSceneObjectBindingID InObjectBindingID)
	{
		const FSequenceInstance* TargetInstance = &InstanceRegistry->GetInstance(InstanceHandle);

		// Is the binding in the same instance as the entity?
		bool bBindingInThisSequence = InObjectBindingID.GetBindingSpace() == EMovieSceneObjectBindingSpace::Local && InObjectBindingID.GetSequenceID() == MovieSceneSequenceID::Root;
		bBindingInThisSequence |= InObjectBindingID.GetBindingSpace() == EMovieSceneObjectBindingSpace::Root && TargetInstance->GetSequenceID() == InObjectBindingID.GetSequenceID();

		if (bBindingInThisSequence)
		{
			if (InstanceRegistry->IsBindingInvalidated(InObjectBindingID.GetGuid(), InstanceHandle))
			{
				StaleEntities.Add(EntityID);
			}
		}
		else
		{
			if (!TargetInstance->IsRootSequence())
			{
				if (InObjectBindingID.GetBindingSpace() == EMovieSceneObjectBindingSpace::Local)
				{
					InObjectBindingID = InObjectBindingID.ResolveLocalToRoot(TargetInstance->GetSequenceID(), *TargetInstance->GetPlayer());
				}

				TargetInstance = &InstanceRegistry->GetInstance(TargetInstance->GetRootInstanceHandle());
			}


			FInstanceHandle SubInstance = TargetInstance->FindSubInstance(InObjectBindingID.GetSequenceID());
			if (!InstanceRegistry->IsHandleValid(SubInstance) || InstanceRegistry->IsBindingInvalidated(InObjectBindingID.GetGuid(), SubInstance))
			{
				StaleEntities.Add(EntityID);
			}
		}
	};

	// Gather all newly instanced entities with an object binding ID
	FEntityTaskBuilder()
	.ReadEntityIDs()
	.Read(Components->InstanceHandle)
	.Read(BindingType)
	.FilterAll({ Components->Tags.ImportedEntity })
	.FilterNone({ Components->Tags.NeedsUnlink })
	.Iterate_PerEntity(&Linker->EntityManager, GatherStaleBindings);

	for (FMovieSceneEntityID Entity : StaleEntities)
	{
		// Tag this as needs link, and all children as needs unlink
		Linker->EntityManager.AddComponent(Entity, Components->Tags.NeedsLink);
		Linker->EntityManager.AddComponent(Entity, Components->Tags.NeedsUnlink, EEntityRecursion::Children);
	}
}