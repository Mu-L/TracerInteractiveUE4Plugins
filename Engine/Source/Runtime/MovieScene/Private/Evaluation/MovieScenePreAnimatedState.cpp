// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluation/MovieScenePreAnimatedState.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"

DECLARE_CYCLE_STAT(TEXT("Save Pre Animated State"), MovieSceneEval_SavePreAnimatedState, STATGROUP_MovieSceneEval);

namespace MovieSceneImpl
{
	void InitializeForAnimation(const IMovieScenePreAnimatedTokenProducer& Producer, UObject* Object)
	{
		checkSlow(Object);
		Producer.InitializeObjectForAnimation(*Object);
	}

	void InitializeForAnimation(const IMovieScenePreAnimatedGlobalTokenProducer& Producer, FNull)
	{
		Producer.InitializeForAnimation();
	}

	IMovieScenePreAnimatedTokenPtr CacheExistingState(const IMovieScenePreAnimatedTokenProducer& Producer, UObject* Object)
	{
		checkSlow(Object);
		return Producer.CacheExistingState(*Object);
	}

	IMovieScenePreAnimatedGlobalTokenPtr CacheExistingState(const IMovieScenePreAnimatedGlobalTokenProducer& Producer, FNull)
	{
		return Producer.CacheExistingState();
	}

	void RestorePreAnimatedToken(TPreAnimatedToken<IMovieScenePreAnimatedTokenPtr>& Token, IMovieScenePlayer& Player, UObject* Object)
	{
		if (Object)
		{
			if (AActor* Actor = Cast<AActor>(Object))
			{
				if (Actor->IsActorBeingDestroyed())
				{
					return;
				}
			}
			else if (UActorComponent* Component = Cast<UActorComponent>(Object))
			{
				if (Component->IsBeingDestroyed())
				{
					return;
				}
			}

			if (Token.OptionalEntityToken.IsValid())
			{
				Token.OptionalEntityToken->RestoreState(*Object, Player);
			}
			else
			{
				Token.Token->RestoreState(*Object, Player);
			}
		}
	}

	void RestorePreAnimatedToken(TPreAnimatedToken<IMovieScenePreAnimatedGlobalTokenPtr>& Token, IMovieScenePlayer& Player, FNull)
	{
		if (Token.OptionalEntityToken.IsValid())
		{
			Token.OptionalEntityToken->RestoreState(Player);
		}
		else
		{
			Token.Token->RestoreState(Player);
		}
	}

	void EntityHasAnimated(FMovieSceneEvaluationKey AssociatedKey, FMovieScenePreAnimatedState& Parent, FNull)
	{
		Parent.EntityHasAnimatedMaster(AssociatedKey);
	}

	void EntityHasAnimated(FMovieSceneEvaluationKey AssociatedKey, FMovieScenePreAnimatedState& Parent, UObject* InObject)
	{
		if (InObject)
		{
			Parent.EntityHasAnimatedObject(AssociatedKey, FObjectKey(InObject));
		}
	}
}

template<typename TokenType>
TPreAnimatedToken<TokenType>::TPreAnimatedToken(TokenType&& InToken)
	: EntityRefCount(0)
	, Token(MoveTemp(InToken))
{}

template<typename TokenType>
void TMovieSceneSavedTokens<TokenType>::OnPreAnimated(ECapturePreAnimatedState CaptureState, FMovieSceneAnimTypeID InAnimTypeID, FMovieSceneEvaluationKey AssociatedKey, const ProducerType& Producer, FMovieScenePreAnimatedState& Parent)
{
	MOVIESCENE_DETAILED_SCOPE_CYCLE_COUNTER(MovieSceneEval_SavePreAnimatedState)

	// If we're not capturing any state, return immediately
	if (CaptureState == ECapturePreAnimatedState::None)
	{
		return;
	}

	// If the entity key and anim type combination already exists in the animated entities array,
	// we've already saved state for this entity and this type, and can just return immediately
	FMovieSceneEntityAndAnimTypeID EntityAndTypeID{AssociatedKey, InAnimTypeID};
	if (CaptureState == ECapturePreAnimatedState::Entity && AnimatedEntities.Contains(EntityAndTypeID))
	{
		return;
	}

	auto ResolvedPayload = Payload.Get();

	// Attempt to locate an existing animated state token for this type ID
	int32 TokenIndex = AllAnimatedTypeIDs.IndexOfByKey(InAnimTypeID);
	if (TokenIndex == INDEX_NONE)
	{
		auto NewlyCachedState = MovieSceneImpl::CacheExistingState(Producer, ResolvedPayload);

		// If the producer returned a null state token, there's no point saving anything.
		// Return immediately without mutating anything in this class.
		if (!NewlyCachedState.IsValid())
		{
			return;
		}

		// Record this type ID as being animated, and push the new state token onto the array
		AllAnimatedTypeIDs.Add(InAnimTypeID);
		PreAnimatedTokens.Add(TokenType(MoveTemp(NewlyCachedState)));

		// If we're capturing for the entity as well, increment the ref count
		if (CaptureState == ECapturePreAnimatedState::Entity)
		{
			++PreAnimatedTokens.Last().EntityRefCount;
			MovieSceneImpl::EntityHasAnimated(AssociatedKey, Parent, ResolvedPayload);
		}

		// Never been animated, so call initialize on the producer (after we've cached the existing state)
		MovieSceneImpl::InitializeForAnimation(Producer, ResolvedPayload);
	}
	else if (CaptureState == ECapturePreAnimatedState::Entity)
	{
		// We already have a token animated, either with Restore State, or Keep State.
		TPreAnimatedToken<TokenType>& Token = PreAnimatedTokens[TokenIndex];

		if (Token.EntityRefCount == 0)
		{
			// If the ref count is 0, a previous entity must have animated, but been set to 'keep state'.
			// In this case, we need to define an additional token to ensure we restore to the correct (current) value when this entity restores.
			// Don't call InitializeForAnimation here, as we've clearly already done so (a token exists for it)
			auto NewlyCachedState = MovieSceneImpl::CacheExistingState(Producer, ResolvedPayload);

			// If the producer returned a null state token, there's no point saving anything.
			// Return immediately without mutating anything in this class.
			if (!NewlyCachedState.IsValid())
			{
				return;
			}

			Token.OptionalEntityToken = MoveTemp(NewlyCachedState);
		}

		// Increment the reference count regardless of whether we just created the token or not (we always need a reference)
		++Token.EntityRefCount;
		MovieSceneImpl::EntityHasAnimated(AssociatedKey, Parent, ResolvedPayload);
	}

	// If we're capturing at the entity level (ie, this entity is restore state), add it to the list of animated entites.
	// We know by this point in the function that the entity was not previously animated, and a valid restore-state token has been added
	if (CaptureState == ECapturePreAnimatedState::Entity)
	{
		AnimatedEntities.Add(EntityAndTypeID);
	}
}

template<typename TokenType>
void TMovieSceneSavedTokens<TokenType>::CopyFrom(TMovieSceneSavedTokens& OtherTokens)
{
	for (const FMovieSceneEntityAndAnimTypeID& Entity : OtherTokens.AnimatedEntities)
	{
		if (!AnimatedEntities.Contains(Entity))
		{
			AnimatedEntities.Add(Entity);
		}
	}

	for (int32 OtherIndex = 0; OtherIndex < OtherTokens.AllAnimatedTypeIDs.Num(); ++OtherIndex)
	{
		FMovieSceneAnimTypeID OtherTypeID = OtherTokens.AllAnimatedTypeIDs[OtherIndex];

		const int32 ExistingIndex = AllAnimatedTypeIDs.IndexOfByKey(OtherTypeID);
		if (ExistingIndex != INDEX_NONE)
		{
			PreAnimatedTokens[ExistingIndex] = MoveTemp(OtherTokens.PreAnimatedTokens[OtherIndex]);
		}
		else
		{
			AllAnimatedTypeIDs.Add(OtherTypeID);
			PreAnimatedTokens.Add(MoveTemp(OtherTokens.PreAnimatedTokens[OtherIndex]));
		}
	}
}


template<typename TokenType>
void TMovieSceneSavedTokens<TokenType>::Restore(IMovieScenePlayer& Player)
{
	auto ResolvedPayload = Payload.Get();

	// Restore in reverse
	for (int32 Index = PreAnimatedTokens.Num() - 1; Index >= 0; --Index)
	{
		MovieSceneImpl::RestorePreAnimatedToken(PreAnimatedTokens[Index], Player, ResolvedPayload);
	}

	Reset();
}

template<typename TokenType>
void TMovieSceneSavedTokens<TokenType>::Restore(IMovieScenePlayer& Player, TFunctionRef<bool(FMovieSceneAnimTypeID)> InFilter)
{
	auto ResolvedPayload = Payload.Get();
	
	for (int32 TokenIndex = AllAnimatedTypeIDs.Num() - 1; TokenIndex >= 0; --TokenIndex)
	{
		FMovieSceneAnimTypeID ThisTokenID = AllAnimatedTypeIDs[TokenIndex];
		if (InFilter(ThisTokenID))
		{
			MovieSceneImpl::RestorePreAnimatedToken(PreAnimatedTokens[TokenIndex], Player, ResolvedPayload);

			AllAnimatedTypeIDs.RemoveAt(TokenIndex, 1, false);
			PreAnimatedTokens.RemoveAt(TokenIndex, 1, false);

			AnimatedEntities.RemoveAll(
				[=](const FMovieSceneEntityAndAnimTypeID& InEntityAndAnimType)
				{
					return InEntityAndAnimType.AnimTypeID == ThisTokenID;
				}
			);
		}
	}
}

template<typename TokenType>
bool TMovieSceneSavedTokens<TokenType>::RestoreEntity(IMovieScenePlayer& Player, FMovieSceneEvaluationKey EntityKey, TOptional<TFunctionRef<bool(FMovieSceneAnimTypeID)>> InFilter)
{
	TArray<FMovieSceneAnimTypeID, TInlineAllocator<8>> AnimTypesToRestore;

	bool bEntityHasBeenEntirelyRestored = true;
	for (int32 LUTIndex = AnimatedEntities.Num() - 1; LUTIndex >= 0; --LUTIndex)
	{
		FMovieSceneEntityAndAnimTypeID EntityAndAnimType = AnimatedEntities[LUTIndex];
		if (EntityAndAnimType.EntityKey == EntityKey)
		{
			if (!InFilter.IsSet() || InFilter.GetValue()(EntityAndAnimType.AnimTypeID))
			{
				// Ask that this anim type have a reference removed
				AnimTypesToRestore.Add(EntityAndAnimType.AnimTypeID);

				// This entity is no longer animating this anim type ID
				AnimatedEntities.RemoveAt(LUTIndex);
			}
			else
			{
				bEntityHasBeenEntirelyRestored = false;
			}
		}
	}

	auto ResolvedPayload = Payload.Get();
	for (int32 TokenIndex = AllAnimatedTypeIDs.Num() - 1; TokenIndex >= 0; --TokenIndex)
	{
		FMovieSceneAnimTypeID ThisTokenID = AllAnimatedTypeIDs[TokenIndex];
		if (AnimTypesToRestore.Contains(ThisTokenID) && --PreAnimatedTokens[TokenIndex].EntityRefCount == 0)
		{
			TPreAnimatedToken<TokenType>& Token = PreAnimatedTokens[TokenIndex];
			MovieSceneImpl::RestorePreAnimatedToken(Token, Player, ResolvedPayload);
			
			// Where an optiona entity token exists, the global stored stae differs from the entity saved state,
			// so we only want to null out the entity token leaving the global state still saved
			if (Token.OptionalEntityToken.IsValid())
			{
				Token.OptionalEntityToken.Reset();
			}
			else
			{
				AllAnimatedTypeIDs.RemoveAt(TokenIndex, 1, false);
				PreAnimatedTokens.RemoveAt(TokenIndex, 1, false);
			}
		}
	}

	return bEntityHasBeenEntirelyRestored;
}

template<typename TokenType>
void TMovieSceneSavedTokens<TokenType>::DiscardEntityTokens()
{
	// Order does not matter here since we are not actually applying any state change to the playback context
	for (TPreAnimatedToken<TokenType>& Token : PreAnimatedTokens)
	{
		// If Token.OptionalEntityToken exists, we throw it away since this relates to entity pre-animated state specifically
		// If Token.OptionalEntityToken does not exist, then Token.Token relates to both entity and global state, so we just reset
		// the ref count such that the token becomes global state only

		// Discard the entity token without restoring its value
		Token.OptionalEntityToken.Reset();

		// Reset the entity count on the token
		Token.EntityRefCount = 0;
	}
}

template<typename TokenType>
void TMovieSceneSavedTokens<TokenType>::Reset()
{
	AnimatedEntities.Reset();
	AllAnimatedTypeIDs.Reset();
	PreAnimatedTokens.Reset();
}

void FMovieScenePreAnimatedState::RestorePreAnimatedState(IMovieScenePlayer& Player)
{
	for (auto& Pair : ObjectTokens)
	{
		Pair.Value.Restore(Player);
	}

	MasterTokens.Restore(Player);

	ObjectTokens.Reset();
	EntityToAnimatedObjects.Reset();
}

void FMovieScenePreAnimatedState::RestorePreAnimatedState(IMovieScenePlayer& Player, UObject& Object)
{
	FObjectKey ObjectKey(&Object);

	auto* FoundObjectTokens = ObjectTokens.Find(ObjectKey);
	if (FoundObjectTokens)
	{
		FoundObjectTokens->Restore(Player);
	}

	for (auto& Pair : EntityToAnimatedObjects)
	{
		Pair.Value.Remove(ObjectKey);
	}
}

void FMovieScenePreAnimatedState::RestorePreAnimatedState(IMovieScenePlayer& Player, UClass* GeneratedClass)
{
	for (auto& ObjectTokenPair : ObjectTokens)
	{
		UObject* Object = ObjectTokenPair.Key.ResolveObjectPtr();
		if (Object)
		{
			if (Object->IsA(GeneratedClass) || Object->GetOuter()->IsA(GeneratedClass))
			{
				ObjectTokenPair.Value.Restore(Player);

				for (auto& Pair : EntityToAnimatedObjects)
				{
					Pair.Value.Remove(ObjectTokenPair.Key);
				}
			}
		}
	}
}


void FMovieScenePreAnimatedState::RestorePreAnimatedState(IMovieScenePlayer& Player, UObject& Object, TFunctionRef<bool(FMovieSceneAnimTypeID)> InFilter)
{
	auto* FoundObjectTokens = ObjectTokens.Find(&Object);
	if (FoundObjectTokens)
	{
		FoundObjectTokens->Restore(Player, InFilter);
	}
}

void FMovieScenePreAnimatedState::RestorePreAnimatedStateImpl(IMovieScenePlayer& Player, const FMovieSceneEvaluationKey& Key, TOptional<TFunctionRef<bool(FMovieSceneAnimTypeID)>> InFilter)
{
	auto* AnimatedObjects = EntityToAnimatedObjects.Find(Key);
	if (!AnimatedObjects)
	{
		return;
	}

	bool bEntityHasBeenEntirelyRestored = true;
	for (FObjectKey ObjectKey : *AnimatedObjects)
	{
		if (ObjectKey == FObjectKey())
		{
			bEntityHasBeenEntirelyRestored = MasterTokens.RestoreEntity(Player, Key, InFilter) && bEntityHasBeenEntirelyRestored;
		}
		else if (auto* FoundState = ObjectTokens.Find(ObjectKey))
		{
			bEntityHasBeenEntirelyRestored = FoundState->RestoreEntity(Player, Key, InFilter) && bEntityHasBeenEntirelyRestored;
		}
	}

	if (bEntityHasBeenEntirelyRestored)
	{
		EntityToAnimatedObjects.Remove(Key);
	}
}

void FMovieScenePreAnimatedState::DiscardEntityTokens()
{
	for (auto& Pair : ObjectTokens)
	{
		Pair.Value.DiscardEntityTokens();
	}

	MasterTokens.DiscardEntityTokens();
}

void FMovieScenePreAnimatedState::DiscardAndRemoveEntityTokensForObject(UObject& Object)
{
	FObjectKey ObjectKey(&Object);

	auto* FoundObjectTokens = ObjectTokens.Find(ObjectKey);
	if (FoundObjectTokens)
	{
		FoundObjectTokens->DiscardEntityTokens();

		ObjectTokens.Remove(ObjectKey);
	}

	for (auto& Pair : EntityToAnimatedObjects)
	{
		Pair.Value.Remove(ObjectKey);
	}
}

void FMovieScenePreAnimatedState::OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap)
{
	for (auto Iter = ReplacementMap.CreateConstIterator(); Iter; ++Iter)
	{
		UObject* OldObject = Iter->Key;
		UObject* NewObject = Iter->Value;
		if (!OldObject || !NewObject)
		{
			continue;
		}

		FObjectKey OldKey = FObjectKey(OldObject);
		if (!ObjectTokens.Contains(OldKey))
		{
			continue;
		}

		FObjectKey NewKey = FObjectKey(NewObject);

		{
			TMovieSceneSavedTokens<IMovieScenePreAnimatedTokenPtr>& NewTokens = ObjectTokens.FindOrAdd(NewKey, TMovieSceneSavedTokens<IMovieScenePreAnimatedTokenPtr>(NewObject));
			TMovieSceneSavedTokens<IMovieScenePreAnimatedTokenPtr>& OldTokens = ObjectTokens.FindChecked(OldKey);

			NewTokens.CopyFrom(OldTokens);
			ObjectTokens.Remove(OldKey);
			// NewTokens is not invalid
		}

		for (auto& Pair : EntityToAnimatedObjects)
		{
			if (Pair.Value.Contains(OldKey))
			{
				Pair.Value.AddUnique(NewKey);
				Pair.Value.Remove(OldKey);
			}
		}
	}
}

/** Explicit, exported template instantiations */
template struct MOVIESCENE_API TMovieSceneSavedTokens<IMovieScenePreAnimatedTokenPtr>;
template struct MOVIESCENE_API TMovieSceneSavedTokens<IMovieScenePreAnimatedGlobalTokenPtr>;

template struct MOVIESCENE_API TPreAnimatedToken<IMovieScenePreAnimatedTokenPtr>;
template struct MOVIESCENE_API TPreAnimatedToken<IMovieScenePreAnimatedGlobalTokenPtr>;