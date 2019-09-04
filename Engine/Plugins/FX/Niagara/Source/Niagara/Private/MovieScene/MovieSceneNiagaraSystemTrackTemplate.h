// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Evaluation/MovieSceneEvalTemplate.h"
#include "Evaluation/MovieSceneTrackImplementation.h"
#include "MovieSceneNiagaraSystemTrackTemplate.generated.h"

USTRUCT()
struct FMovieSceneNiagaraSystemTrackTemplate : public FMovieSceneEvalTemplate
{
	GENERATED_BODY()

public:
	FMovieSceneNiagaraSystemTrackTemplate() { }

private:
	virtual UScriptStruct& GetScriptStructImpl() const override { return *StaticStruct(); }
};

USTRUCT()
struct FMovieSceneNiagaraSystemTrackImplementation : public FMovieSceneTrackImplementation
{
	GENERATED_BODY()

public:
	FMovieSceneNiagaraSystemTrackImplementation() { }
	FMovieSceneNiagaraSystemTrackImplementation(FFrameNumber InSpawnSectionStartFrame, FFrameNumber InSpawnSectionEndFrame);
	
	virtual void SetupOverrides() override
	{
		EnableOverrides(CustomEvaluateFlag);
	}

private:
	virtual UScriptStruct& GetScriptStructImpl() const override { return *StaticStruct(); }
	virtual void Evaluate(const FMovieSceneEvaluationTrack& Track, FMovieSceneSegmentIdentifier SegmentID, const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const override;

private:
	UPROPERTY()
	FFrameNumber SpawnSectionStartFrame;
	UPROPERTY()
	FFrameNumber SpawnSectionEndFrame;
};