// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExtensionLibraries/MovieScenePropertyTrackExtensions.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "Tracks/MovieSceneObjectPropertyTrack.h"

void UMovieScenePropertyTrackExtensions::SetPropertyNameAndPath(UMovieScenePropertyTrack* Track, const FName& InPropertyName, const FString& InPropertyPath)
{
	Track->SetPropertyNameAndPath(InPropertyName, InPropertyPath);
}

FName UMovieScenePropertyTrackExtensions::GetPropertyName(UMovieScenePropertyTrack* Track)
{
	return Track->GetPropertyName();
}

FString UMovieScenePropertyTrackExtensions::GetPropertyPath(UMovieScenePropertyTrack* Track)
{
	return Track->GetPropertyPath();
}

FName UMovieScenePropertyTrackExtensions::GetUniqueTrackName(UMovieScenePropertyTrack* Track)
{
#if WITH_EDITORONLY_DATA
	return Track->UniqueTrackName;
#else
	return NAME_None;
#endif
}

void UMovieScenePropertyTrackExtensions::SetObjectPropertyClass(UMovieSceneObjectPropertyTrack* Track, UClass* PropertyClass)
{
	Track->PropertyClass = PropertyClass;
}

UClass* UMovieScenePropertyTrackExtensions::GetObjectPropertyClass(UMovieSceneObjectPropertyTrack* Track)
{
	return Track->PropertyClass;
}


