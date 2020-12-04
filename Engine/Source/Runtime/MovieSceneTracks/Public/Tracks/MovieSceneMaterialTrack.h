// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Sections/MovieSceneParameterSection.h"
#include "MovieSceneNameableTrack.h"
#include "Compilation/IMovieSceneTrackTemplateProducer.h"
#include "MovieSceneMaterialTrack.generated.h"

/**
 * Handles manipulation of material parameters in a movie scene.
 */
UCLASS()
class MOVIESCENETRACKS_API UMovieSceneMaterialTrack
	: public UMovieSceneNameableTrack
{
	GENERATED_BODY()

public:

	UMovieSceneMaterialTrack(const FObjectInitializer& ObjectInitializer);

	// UMovieSceneTrack interface

	virtual bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	virtual UMovieSceneSection* CreateNewSection() override;
	virtual void RemoveAllAnimationData() override;
	virtual bool HasSection(const UMovieSceneSection& Section) const override;
	virtual void AddSection(UMovieSceneSection& Section) override;
	virtual void RemoveSection(UMovieSceneSection& Section) override;
	virtual void RemoveSectionAt(int32 SectionIndex) override;
	virtual bool IsEmpty() const override;
	virtual const TArray<UMovieSceneSection*>& GetAllSections() const override;

public:

	/**
	 * Adds a scalar parameter key to the track. 
	 * @param ParameterName The name of the parameter to add a key for.
	 * @param Time The time to add the new key.
	 * @param The value for the new key.
	 */
	void AddScalarParameterKey(FName ParameterName, FFrameNumber Position, float Value);

	/**
	* Adds a color parameter key to the track.
	* @param ParameterName The name of the parameter to add a key for.
	* @param Time The time to add the new key.
	* @param The value for the new key.
	*/
	void AddColorParameterKey(FName ParameterName, FFrameNumber Position, FLinearColor Value);

private:

	/** The sections owned by this track .*/
	UPROPERTY()
	TArray<UMovieSceneSection*> Sections;
};


/**
 * A material track which is specialized for animation materials which are owned by actor components.
 */
UCLASS(MinimalAPI)
class UMovieSceneComponentMaterialTrack
	: public UMovieSceneMaterialTrack
	, public IMovieSceneTrackTemplateProducer
{
	GENERATED_UCLASS_BODY()

public:

	// UMovieSceneTrack interface

	static const uint16 EvaluationPriority = 1000;

	virtual FMovieSceneEvalTemplatePtr CreateTemplateForSection(const UMovieSceneSection& InSection) const override;
	virtual void PostCompile(FMovieSceneEvaluationTrack& OutTrack, const FMovieSceneTrackCompilerArgs& Args) const override;
	virtual FName GetTrackName() const { return FName( *FString::FromInt(MaterialIndex) ); }

#if WITH_EDITORONLY_DATA
	virtual FText GetDefaultDisplayName() const override;
#endif

public:

	/** Gets the index of the material in the component. */
	int32 GetMaterialIndex() const { return MaterialIndex; }

	/** Sets the index of the material in the component. */
	void SetMaterialIndex(int32 InMaterialIndex) 
	{
		MaterialIndex = InMaterialIndex;
	}

private:

	/** The index of this material this track is animating. */
	UPROPERTY()
	int32 MaterialIndex;
};
