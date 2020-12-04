// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneSequenceTickManager.h"
#include "Engine/World.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "ProfilingDebugging/CountersTrace.h"

DECLARE_CYCLE_STAT(TEXT("Sequence Tick Manager"), MovieSceneEval_SequenceTickManager, STATGROUP_MovieSceneEval);

TRACE_DECLARE_INT_COUNTER(MovieSceneEval_SequenceTickManagerLatentActionRuns, TEXT("MovieScene/LatentActionRuns"));

static TAutoConsoleVariable<int32> CVarMovieSceneMaxLatentActionLoops(
	TEXT("Sequencer.MaxLatentActionLoops"),
	100,
	TEXT("Defines the maximum number of latent action loops that can be run in one frame.\n"),
	ECVF_Default
);

UMovieSceneSequenceTickManager::UMovieSceneSequenceTickManager(const FObjectInitializer& Init)
	: Super(Init)
{
}

void UMovieSceneSequenceTickManager::BeginDestroy()
{
	if (WorldTickDelegateHandle.IsValid())
	{
		UWorld* World = GetTypedOuter<UWorld>();
		if (ensure(World != nullptr))
		{
			World->RemoveMovieSceneSequenceTickHandler(WorldTickDelegateHandle);
			WorldTickDelegateHandle.Reset();
		}
	}

	Super::BeginDestroy();
}

void UMovieSceneSequenceTickManager::TickSequenceActors(float DeltaSeconds)
{
	SCOPE_CYCLE_COUNTER(MovieSceneEval_SequenceTickManager);

	TRACE_COUNTER_SET(MovieSceneEval_SequenceTickManagerLatentActionRuns, 0);

	// Let all level sequence actors update. Some of them won't do anything, others will do synchronous
	// things (e.g. start/stop, loop, etc.), but in 95% of cases, they will just queue up a normal evaluation
	// request...
	//
	bool bHasTasks = Runner.HasQueuedUpdates();
	UWorld* World = GetTypedOuter<UWorld>();

	check(World != nullptr);
	check(LatentActionManager.IsEmpty());
	
	for (int32 i = SequenceActors.Num() - 1; i >= 0; --i)
	{
		if (AActor* SequenceActor = SequenceActors[i])
		{
			check(SequenceActor->GetWorld() == World);
			SequenceActor->Tick(DeltaSeconds);
			bHasTasks = true;
		}
	}

	// If we have nothing to do, we can early-out.
	if (!bHasTasks)
	{
		return;
	}

	// Now we execute all those "normal evaluation requests" we mentioned above. All running level sequences
	// will therefore be evaluated in a gloriously parallelized way.
	//
	if (ensure(Runner.IsAttachedToLinker()))
	{
		Runner.Flush();
		LatentActionManager.RunLatentActions(Runner);
	}
}

void UMovieSceneSequenceTickManager::ClearLatentActions(UObject* Object)
{
	LatentActionManager.ClearLatentActions(Object);
}

void UMovieSceneSequenceTickManager::AddLatentAction(FMovieSceneSequenceLatentActionDelegate Delegate)
{
	LatentActionManager.AddLatentAction(Delegate);
}

void UMovieSceneSequenceTickManager::RunLatentActions(const UObject* Object, FMovieSceneEntitySystemRunner& InRunner)
{
	LatentActionManager.RunLatentActions(InRunner, Object);
}

UMovieSceneSequenceTickManager* UMovieSceneSequenceTickManager::Get(UObject* PlaybackContext)
{
	check(PlaybackContext != nullptr && PlaybackContext->GetWorld() != nullptr);
	UWorld* World = PlaybackContext->GetWorld();

	UMovieSceneSequenceTickManager* TickManager = FindObject<UMovieSceneSequenceTickManager>(World, TEXT("GlobalMovieSceneSequenceTickManager"));
	if (!TickManager)
	{
		TickManager = NewObject<UMovieSceneSequenceTickManager>(World, TEXT("GlobalMovieSceneSequenceTickManager"));

		TickManager->Linker = UMovieSceneEntitySystemLinker::FindOrCreateLinker(World, TEXT("MovieSceneSequencePlayerEntityLinker"));
		check(TickManager->Linker);
		TickManager->Runner.AttachToLinker(TickManager->Linker);

		FDelegateHandle Handle = World->AddMovieSceneSequenceTickHandler(
				FOnMovieSceneSequenceTick::FDelegate::CreateUObject(TickManager, &UMovieSceneSequenceTickManager::TickSequenceActors));
		check(Handle.IsValid());
		TickManager->WorldTickDelegateHandle = Handle;
	}
	return TickManager;
}

void FMovieSceneLatentActionManager::AddLatentAction(FMovieSceneSequenceLatentActionDelegate Delegate)
{
	check(Delegate.GetUObject() != nullptr);
	LatentActions.Add(Delegate);
}

void FMovieSceneLatentActionManager::ClearLatentActions(UObject* Object)
{
	check(Object);

	for (FMovieSceneSequenceLatentActionDelegate& Action : LatentActions)
	{
		// Rather than remove actions, we simply unbind them, to ensure that we do not
		// shuffle the array if it is already being processed higher up the call-stack
		if (Action.IsBound() && Action.GetUObject() == Object)
		{
			Action.Unbind();
		}
	}
}

void FMovieSceneLatentActionManager::RunLatentActions(FMovieSceneEntitySystemRunner& Runner)
{
	if (bIsRunningLatentActions)
	{
		// We're already running latent actions... no need to run them again in a nested loop.
		return;
	}

	TGuardValue<bool> IsRunningLatentActionsGuard(bIsRunningLatentActions, true);

	int32 NumLoopsLeft = CVarMovieSceneMaxLatentActionLoops.GetValueOnGameThread();
	while (LatentActions.Num() > 0)
	{
		TRACE_COUNTER_INCREMENT(MovieSceneEval_SequenceTickManagerLatentActionRuns);

		// We can run *one* latent action per sequence player before having to flush the linker again.
		// This way, if we have 42 sequence players with 2 latent actions each, we only flush the linker
		// twice, as opposed to 42*2=84 times.
		int32 Index = 0;
		TSet<UObject*> ExecutedDelegateOwners;
		while (Index < LatentActions.Num())
		{
			const FMovieSceneSequenceLatentActionDelegate& Delegate = LatentActions[Index];
			if (!Delegate.IsBound())
			{
				LatentActions.RemoveAt(Index);
				continue;
			}

			UObject* BoundObject = Delegate.GetUObject();
			if (ensure(BoundObject) && !ExecutedDelegateOwners.Contains(BoundObject))
			{
				Delegate.ExecuteIfBound();
				ExecutedDelegateOwners.Add(BoundObject);
				LatentActions.RemoveAt(Index);
			}
			else
			{
				++Index;
			}
		}

		Runner.Flush();

		--NumLoopsLeft;
		if (!ensureMsgf(NumLoopsLeft > 0,
					TEXT("Detected possible infinite loop! Are you requeuing the same latent action over and over?")))
		{
			break;
		}
	}
}

void FMovieSceneLatentActionManager::RunLatentActions(FMovieSceneEntitySystemRunner& Runner, const UObject* Object)
{
	check(Object);

	if (bIsRunningLatentActions)
	{
		// We are already running latent actions for all players. We'll get to this one soon.
		return;
	}

	int32 Index = 0;
	int32 NumLoopsLeft = CVarMovieSceneMaxLatentActionLoops.GetValueOnGameThread();
	while (NumLoopsLeft > 0)
	{
		// Look for the first latent action that is bound to the given object.
		bool bFoundMatching = false;
		while (Index < LatentActions.Num())
		{
			const FMovieSceneSequenceLatentActionDelegate& Delegate = LatentActions[Index];
			UObject* BoundObject = Delegate.GetUObject();
			if (ensure(BoundObject) && (BoundObject == Object))
			{
				Delegate.ExecuteIfBound();
				bFoundMatching = true;
				LatentActions.RemoveAt(Index);
				break;
			}

			++Index;
		}

		// If we didn't find anything, we're done!
		if (!bFoundMatching)
		{
			break;
		}

		Runner.Flush();

		// Now loop over and keep searching for more matching latent actions. We leave our
		// index to where it was, since we know that delegates bound to the given object
		// are appended at the end of the array, so we don't have to search the beginning
		// again.
	}
	ensureMsgf(NumLoopsLeft > 0,
			TEXT("Detected possible infinite loop! Are you requeuing the same latent action over and over?"));
}

