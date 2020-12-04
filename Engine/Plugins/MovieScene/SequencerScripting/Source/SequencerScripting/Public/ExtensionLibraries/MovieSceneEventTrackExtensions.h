// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "UObject/ObjectMacros.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "MovieSceneEventTrackExtensions.generated.h"

class UMovieSceneEventTrack;
class UMovieSceneEventRepeaterSection;
class UMovieSceneEventTriggerSection;

/**
 * Function library containing methods that should be hoisted onto UMovieSceneEventTrack for scripting
 */
UCLASS()
class UMovieSceneEventTrackExtensions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * Create a new event repeater section for the given track
	 *
	 * @param Track        The event track to add the new event repeater section to
	 * @return The newly created event repeater section
	 */
	UFUNCTION(BlueprintCallable, Category = "Track", meta = (ScriptMethod))
	static UMovieSceneEventRepeaterSection* AddEventRepeaterSection(UMovieSceneEventTrack* InTrack);
	
	/**
	 * Create a new event trigger section for the given track
	 *
	 * @param Track        The event track to add the new event trigger section to
	 * @return The newly created event trigger section
	 */
	UFUNCTION(BlueprintCallable, Category = "Track", meta = (ScriptMethod))
	static UMovieSceneEventTriggerSection* AddEventTriggerSection(UMovieSceneEventTrack* InTrack);
	
};
