// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluation/MovieSceneTrackImplementation.h"
#include "Evaluation/MovieSceneEvalTemplate.h"
#include "Evaluation/MovieSceneEvalTemplateSerializer.h"

bool FMovieSceneTrackImplementationPtr::Serialize(FArchive& Ar)
{
	bool bShouldWarn = !WITH_EDITORONLY_DATA;
	return SerializeInlineValue(*this, Ar, bShouldWarn);
}
