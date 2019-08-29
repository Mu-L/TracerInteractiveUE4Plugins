// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"
#include "Evaluation/MovieSceneTrackImplementation.h"
#include "Evaluation/MovieSceneEvalTemplate.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieSceneCameraCutTemplate.generated.h"

class UMovieSceneCameraCutSection;

/** Camera cut track evaluation template */
USTRUCT()
struct FMovieSceneCameraCutSectionTemplate : public FMovieSceneEvalTemplate
{
	GENERATED_BODY()
	
	FMovieSceneCameraCutSectionTemplate() : bHasCutTransform(false) {}
	FMovieSceneCameraCutSectionTemplate(const UMovieSceneCameraCutSection& Section, TOptional<FTransform> CutTransform);

	/** GUID of the camera we should cut to in this sequence */
	UPROPERTY()
	FMovieSceneObjectBindingID CameraBindingID;

	UPROPERTY()
	FTransform CutTransform;

	UPROPERTY()
	bool bHasCutTransform;

private:

	virtual UScriptStruct& GetScriptStructImpl() const override { return *StaticStruct(); }
	virtual void Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const override;
};
