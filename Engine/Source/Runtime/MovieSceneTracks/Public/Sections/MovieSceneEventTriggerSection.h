// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MovieSceneEventSectionBase.h"
#include "Channels/MovieSceneEventChannel.h"
#include "MovieSceneEventTriggerSection.generated.h"


/**
 * Event section that triggeres specific timed events.
 */
UCLASS(MinimalAPI)
class UMovieSceneEventTriggerSection
	: public UMovieSceneEventSectionBase
{
public:
	GENERATED_BODY()

	UMovieSceneEventTriggerSection(const FObjectInitializer& ObjInit);


#if WITH_EDITORONLY_DATA

	virtual TArrayView<FMovieSceneEvent> GetAllEntryPoints() override { return EventChannel.GetData().GetValues(); }

#endif

	/** The channel that defines this section's timed events */
	UPROPERTY()
	FMovieSceneEventChannel EventChannel;
};