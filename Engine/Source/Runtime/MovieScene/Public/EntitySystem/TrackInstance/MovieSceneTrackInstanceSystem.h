// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntityInstantiatorSystem.h"
#include "EntitySystem/MovieScenePropertySystemTypes.h"
#include "EntitySystem/MovieSceneEntitySystemTypes.h"
#include "EntitySystem/MovieSceneEntityIDs.h"

#include "MovieSceneTrackInstanceSystem.generated.h"

class UMovieSceneSection;
class UMovieSceneTrackInstance;

USTRUCT()
struct FMovieSceneTrackInstanceEntry
{
	GENERATED_BODY()

	UPROPERTY()
	UObject* BoundObject;

	UPROPERTY()
	UMovieSceneTrackInstance* TrackInstance;
};


UCLASS()
class MOVIESCENE_API UMovieSceneTrackInstanceInstantiator : public UMovieSceneEntityInstantiatorSystem
{
	GENERATED_BODY()

public:

	UMovieSceneTrackInstanceInstantiator(const FObjectInitializer& ObjInit);

	int32 MakeOutput(UObject* BoundObject, UClass* TrackInstanceClass);

	int32 FindOutput(UObject* BoundObject, UClass* TrackInstanceClass) const;

	const TSparseArray<FMovieSceneTrackInstanceEntry>& GetTrackInstances() const
	{
		return TrackInstances;
	}

	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

private:

	virtual void OnLink() override final;
	virtual void OnUnlink() override final;
	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override final;

	virtual void OnTagGarbage() override;

	virtual void Serialize(FArchive& Ar) override;

	TSparseArray<FMovieSceneTrackInstanceEntry> TrackInstances;
	TMultiMap<UObject*, int32> BoundObjectToInstances;

	TBitArray<> InvalidatedOutputs;

	int32 ChildInitializerIndex;
};


UCLASS()
class MOVIESCENE_API UMovieSceneTrackInstanceSystem : public UMovieSceneEntitySystem
{
	GENERATED_BODY()

	UMovieSceneTrackInstanceSystem(const FObjectInitializer& ObjInit);

private:

	virtual void OnLink() override final;
	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override final;

	UPROPERTY()
	UMovieSceneTrackInstanceInstantiator* Instantiator;
};


