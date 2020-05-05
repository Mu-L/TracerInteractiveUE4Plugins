// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluation/MovieSceneSpawnTemplate.h"
#include "MovieSceneSequence.h"
#include "Sections/MovieSceneSpawnSection.h"
#include "Evaluation/MovieSceneEvaluation.h"
#include "IMovieScenePlayer.h"
#include "IMovieScenePlaybackClient.h"

DECLARE_CYCLE_STAT(TEXT("Spawn Track Evaluate"), MovieSceneEval_SpawnTrack_Evaluate, STATGROUP_MovieSceneEval);
DECLARE_CYCLE_STAT(TEXT("Spawn Track Token Execute"), MovieSceneEval_SpawnTrack_TokenExecute, STATGROUP_MovieSceneEval);

/** A movie scene pre-animated token that stores a pre-animated transform */
struct FSpawnTrackPreAnimatedTokenProducer : IMovieScenePreAnimatedTokenProducer
{
	FMovieSceneEvaluationOperand Operand;
	FSpawnTrackPreAnimatedTokenProducer(FMovieSceneEvaluationOperand InOperand) : Operand(InOperand) {}

	virtual IMovieScenePreAnimatedTokenPtr CacheExistingState(UObject& Object) const
	{
		struct FToken : IMovieScenePreAnimatedToken
		{
			FMovieSceneEvaluationOperand OperandToDestroy;
			FToken(FMovieSceneEvaluationOperand InOperand) : OperandToDestroy(InOperand) {}

			virtual void RestoreState(UObject& InObject, IMovieScenePlayer& Player) override
			{
				Player.GetSpawnRegister().DestroySpawnedObject(OperandToDestroy.ObjectBindingID, OperandToDestroy.SequenceID, Player);
			}
		};
		
		return FToken(Operand);
	}
};

struct FSpawnObjectToken : IMovieSceneExecutionToken
{
	FSpawnObjectToken(bool bInSpawned)
		: bSpawned(bInSpawned)
	{}

	virtual void Execute(const FMovieSceneContext& Context, const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) override
	{
		MOVIESCENE_DETAILED_SCOPE_CYCLE_COUNTER(MovieSceneEval_SpawnTrack_TokenExecute)

		if (const FMovieSceneEvaluationOperand* OperandOverride = Player.BindingOverrides.Find(Operand))
		{
			// Don't do anything if this operand was overriden... someone else will take care of it (either another spawn track, or
			// some possessable).
			return;
		}

		bool bHasSpawnedObject = Player.GetSpawnRegister().FindSpawnedObject(Operand.ObjectBindingID, Operand.SequenceID).Get() != nullptr;
		
		// Check binding overrides to see if this spawnable has been overridden, and whether it allows the default spawnable to exist
		const IMovieScenePlaybackClient* PlaybackClient = Player.GetPlaybackClient();
		if (!bHasSpawnedObject && PlaybackClient)
		{
			TArray<UObject*, TInlineAllocator<1>> FoundObjects;
			bool bUseDefaultBinding = PlaybackClient->RetrieveBindingOverrides(Operand.ObjectBindingID, Operand.SequenceID, FoundObjects);
			if (!bUseDefaultBinding)
			{
				bHasSpawnedObject = true;
			}
		}

		if (bSpawned)
		{
			// If it's not spawned, spawn it
			if (!bHasSpawnedObject)
			{
				const UMovieSceneSequence* Sequence = Player.State.FindSequence(Operand.SequenceID);
				if (Sequence)
				{
					UObject* SpawnedObject = Player.GetSpawnRegister().SpawnObject(Operand.ObjectBindingID, *Sequence->GetMovieScene(), Operand.SequenceID, Player);

					if (SpawnedObject)
					{
						Player.OnObjectSpawned(SpawnedObject, Operand);
					}
				}
			}

			// ensure that pre animated state is saved
			for (TWeakObjectPtr<> Object : Player.FindBoundObjects(Operand))
			{
				if (UObject* ObjectPtr = Object.Get())
				{
					Player.SavePreAnimatedState(*ObjectPtr, FMovieSceneSpawnSectionTemplate::GetAnimTypeID(), FSpawnTrackPreAnimatedTokenProducer(Operand));
				}
			}
		}
		else if (!bSpawned && bHasSpawnedObject)
		{
			Player.GetSpawnRegister().DestroySpawnedObject(Operand.ObjectBindingID, Operand.SequenceID, Player);
		}
	}

	bool bSpawned;
};

FMovieSceneSpawnSectionTemplate::FMovieSceneSpawnSectionTemplate(const UMovieSceneSpawnSection& SpawnSection)
	: Curve(SpawnSection.GetChannel())
{
}

void FMovieSceneSpawnSectionTemplate::Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	MOVIESCENE_DETAILED_SCOPE_CYCLE_COUNTER(MovieSceneEval_SpawnTrack_Evaluate)

	bool SpawnValue = false;
	if (Curve.Evaluate(Context.GetTime(), SpawnValue))
	{
		ExecutionTokens.Add(FSpawnObjectToken(SpawnValue));
	}
}

FMovieSceneAnimTypeID FMovieSceneSpawnSectionTemplate::GetAnimTypeID()
{
	return TMovieSceneAnimTypeID<FMovieSceneSpawnSectionTemplate>();
}
