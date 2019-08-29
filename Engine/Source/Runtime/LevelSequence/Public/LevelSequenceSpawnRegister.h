// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneSpawnRegister.h"
#include "UObject/Class.h"

class IMovieScenePlayer;
class IMovieSceneObjectSpawner;

/** Movie scene spawn register that knows how to handle spawning objects (actors) for a level sequence  */
class LEVELSEQUENCE_API FLevelSequenceSpawnRegister : public FMovieSceneSpawnRegister
{
public:
	FLevelSequenceSpawnRegister();

protected:
	/** ~ FMovieSceneSpawnRegister interface */
	virtual UObject* SpawnObject(FMovieSceneSpawnable& Spawnable, FMovieSceneSequenceIDRef TemplateID, IMovieScenePlayer& Player) override;
	virtual void DestroySpawnedObject(UObject& Object) override;

#if WITH_EDITOR
	virtual bool CanSpawnObject(UClass* InClass) const override;
#endif

protected:
	/** Extension object spawners */
	TArray<TSharedRef<IMovieSceneObjectSpawner>> MovieSceneObjectSpawners;
};
