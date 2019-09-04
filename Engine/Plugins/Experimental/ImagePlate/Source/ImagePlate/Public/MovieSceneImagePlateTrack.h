// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "MovieSceneImagePlateTrack.generated.h"

/**
 *
 */
UCLASS(MinimalAPI)
class UMovieSceneImagePlateTrack
	: public UMovieScenePropertyTrack
{
public:
	GENERATED_BODY()

	UMovieSceneImagePlateTrack(const FObjectInitializer& ObjectInitializer);

public:

	virtual void AddSection(UMovieSceneSection& Section) override;
	virtual bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	virtual UMovieSceneSection* CreateNewSection() override;
	virtual const TArray<UMovieSceneSection*>& GetAllSections() const override;
	virtual void RemoveSection(UMovieSceneSection& Section) override;
	virtual void RemoveSectionAt(int32 SectionIndex) override;
	virtual FMovieSceneEvalTemplatePtr CreateTemplateForSection(const UMovieSceneSection& InSection) const override;
#if WITH_EDITORONLY_DATA
	virtual FText GetDefaultDisplayName() const override;
	virtual FName GetTrackName() const { return UniqueTrackName; }
#endif
};
