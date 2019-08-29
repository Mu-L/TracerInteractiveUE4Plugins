// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "Evaluation/MovieSceneEvalTemplate.h"
#include "Evaluation/MovieSceneEvalTemplateSerializer.h"

float FMovieSceneEvalTemplate::EvaluateEasing(float CurrentTime) const
{
	return SourceSection ? SourceSection->EvaluateEasing(CurrentTime) : 1.f;
}

bool FMovieSceneEvalTemplatePtr::Serialize(FArchive& Ar)
{
	return SerializeInlineValue(*this, Ar);
}
