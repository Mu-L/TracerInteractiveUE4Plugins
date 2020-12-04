// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExtensionLibraries/MovieSceneVectorTrackExtensions.h"
#include "Tracks/MovieSceneVectorTrack.h"

void UMovieSceneVectorTrackExtensions::SetNumChannelsUsed(UMovieSceneVectorTrack* Track, int32 InNumChannelsUsed)
{
	Track->Modify();

	Track->SetNumChannelsUsed(InNumChannelsUsed);
}


int32 UMovieSceneVectorTrackExtensions::GetNumChannelsUsed(UMovieSceneVectorTrack* Track)
{
	return Track->GetNumChannelsUsed();
}
