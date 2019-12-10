// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MovieSceneSequence.h"
#include "MovieScene.h"
#include "UObject/SoftObjectPtr.h"
#include "TemplateSequence.generated.h"

/*
 * Movie scene animation that can be instanced multiple times inside a level sequence.
 */
UCLASS(BlueprintType)
class TEMPLATESEQUENCE_API UTemplateSequence : public UMovieSceneSequence
{
public:
	GENERATED_BODY()

	UTemplateSequence(const FObjectInitializer& ObjectInitializer);

	void Initialize();

	//~ UMovieSceneSequence interface
	virtual void BindPossessableObject(const FGuid& ObjectId, UObject& PossessedObject, UObject* Context) override;
	virtual bool CanPossessObject(UObject& Object, UObject* InPlaybackContext) const override;
	virtual void LocateBoundObjects(const FGuid& ObjectId, UObject* Context, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const override;
	virtual UMovieScene* GetMovieScene() const override;
	virtual UObject* GetParentObject(UObject* Object) const override;
	virtual void UnbindPossessableObjects(const FGuid& ObjectId) override;
	virtual void UnbindObjects(const FGuid& ObjectId, const TArray<UObject*>& InObjects, UObject* Context) override;
	virtual void UnbindInvalidObjects(const FGuid& ObjectId, UObject* Context) override;

	virtual bool AllowsSpawnableObjects() const override;

#if WITH_EDITOR
	virtual FText GetDisplayName() const override;
#endif

public:

	UPROPERTY()
	UMovieScene* MovieScene;

	UPROPERTY()
	TSoftClassPtr<AActor> BoundActorClass;

	UPROPERTY()
	TSoftObjectPtr<AActor> BoundPreviewActor;

	UPROPERTY()
	TMap<FGuid, FName> BoundActorComponents;
};
