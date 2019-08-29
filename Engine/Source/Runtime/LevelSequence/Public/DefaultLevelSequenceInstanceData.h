// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"
#include "Algo/Transform.h"
#include "Tracks/IMovieSceneTransformOrigin.h"
#include "GameFramework/Actor.h"

#include "DefaultLevelSequenceInstanceData.generated.h"

/** Default instance data class that level sequences understand. Implements IMovieSceneTransformOrigin. */
UCLASS(BlueprintType)
class LEVELSEQUENCE_API UDefaultLevelSequenceInstanceData
	: public UObject
	, public IMovieSceneTransformOrigin
{
public:

	GENERATED_BODY()

	UDefaultLevelSequenceInstanceData(const FObjectInitializer& Init)
		: Super(Init)
	{
		TransformOriginActor = nullptr;
		TransformOrigin = FTransform::Identity;
	}

	virtual FTransform NativeGetTransformOrigin() const override { return TransformOriginActor ? TransformOriginActor->ActorToWorld() : TransformOrigin; }

	/** When set, this actor's world position will be used as the transform origin for all absolute transform sections */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="General")
	AActor* TransformOriginActor;

	/** Specifies a transform from which all absolute transform sections inside the sequence should be added to. Scale is ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="General")
	FTransform TransformOrigin;
};
