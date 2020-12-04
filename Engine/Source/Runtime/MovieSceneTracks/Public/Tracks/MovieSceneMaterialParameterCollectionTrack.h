// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Tracks/MovieSceneMaterialTrack.h"
#include "MovieSceneMaterialParameterCollectionTrack.generated.h"

class UMaterialParameterCollection;

/**
 * Handles manipulation of material parameter collections in a movie scene.
 */
UCLASS()
class MOVIESCENETRACKS_API UMovieSceneMaterialParameterCollectionTrack
	: public UMovieSceneMaterialTrack
	, public IMovieSceneTrackTemplateProducer
{
public:

	GENERATED_BODY()

	/** The material parameter collection to manipulate */
	UPROPERTY(EditAnywhere, Category=General, DisplayName="Material Parameter Collection")
	UMaterialParameterCollection* MPC;

	UMovieSceneMaterialParameterCollectionTrack(const FObjectInitializer& ObjectInitializer);

	virtual bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	virtual UMovieSceneSection* CreateNewSection() override;
	virtual FMovieSceneEvalTemplatePtr CreateTemplateForSection(const UMovieSceneSection& InSection) const override;
#if WITH_EDITORONLY_DATA
	virtual FText GetDefaultDisplayName() const override;
#endif
};
