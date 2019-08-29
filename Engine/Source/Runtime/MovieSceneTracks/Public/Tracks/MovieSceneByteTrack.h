// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "MovieSceneByteTrack.generated.h"

class UEnum;

/**
 * Handles manipulation of byte properties in a movie scene
 */
UCLASS()
class MOVIESCENETRACKS_API UMovieSceneByteTrack : public UMovieScenePropertyTrack
{
	GENERATED_UCLASS_BODY()

public:
	/** UMovieSceneTrack interface */
	virtual void PostLoad() override;
	virtual UMovieSceneSection* CreateNewSection() override;
	virtual FMovieSceneEvalTemplatePtr CreateTemplateForSection(const UMovieSceneSection& InSection) const override;

	void SetEnum(UEnum* Enum);

	class UEnum* GetEnum() const;

protected:
	UPROPERTY()
	UEnum* Enum;
};
