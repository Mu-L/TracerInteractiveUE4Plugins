// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Evaluation/MovieSceneEvalTemplate.h"
#include "Evaluation/MovieScenePropertyTemplate.h"
#include "Misc/FrameNumber.h"
#include "MovieSceneImagePlateTemplate.generated.h"

class UMovieSceneImagePlateSection;
class UMovieSceneImagePlateTrack;
class UImagePlateFileSequence;

USTRUCT()
struct FMovieSceneImagePlateSectionParams
{
	GENERATED_BODY()

	FMovieSceneImagePlateSectionParams();

	UPROPERTY()
	FFrameNumber SectionStartTime;

	UPROPERTY()
	UImagePlateFileSequence* FileSequence;

	UPROPERTY()
	bool bReuseExistingTexture;
};

USTRUCT()
struct FMovieSceneImagePlateSectionTemplate : public FMovieSceneEvalTemplate
{
	GENERATED_BODY()

	FMovieSceneImagePlateSectionTemplate();
	FMovieSceneImagePlateSectionTemplate(const UMovieSceneImagePlateSection& InSection, const UMovieSceneImagePlateTrack& InTrack);

	virtual void SetupOverrides() override
	{
		EnableOverrides(RequiresSetupFlag);
	}
	virtual UScriptStruct& GetScriptStructImpl() const override { return *StaticStruct(); }

	virtual void Setup(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) const override;

	virtual void Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const override;

private:

	UPROPERTY()
	FMovieScenePropertySectionData PropertyData;

	UPROPERTY()
	FMovieSceneImagePlateSectionParams Params;
};
