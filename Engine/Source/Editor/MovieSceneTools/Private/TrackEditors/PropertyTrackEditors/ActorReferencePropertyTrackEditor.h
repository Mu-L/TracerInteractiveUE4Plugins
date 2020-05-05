// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "ISequencer.h"
#include "ISequencerSection.h"
#include "ISequencerTrackEditor.h"
#include "PropertyTrackEditor.h"
#include "GameFramework/Actor.h"
#include "Tracks/MovieSceneActorReferenceTrack.h"
#include "Sections/MovieSceneActorReferenceSection.h"

/**
 * A property track editor for actor references.
 */
class FActorReferencePropertyTrackEditor
	: public FPropertyTrackEditor<UMovieSceneActorReferenceTrack>
{
public:

	/**
	 * Constructor.
	 *
	 * @param InSequencer The sequencer instance to be used by this tool.
	 */
	FActorReferencePropertyTrackEditor(TSharedRef<ISequencer> InSequencer)
		: FPropertyTrackEditor(InSequencer, GetAnimatedPropertyTypes())
	{
	}

	/**
	 * Retrieve a list of all property types that this track editor animates
	 */
	static TArray<FAnimatedPropertyKey, TInlineAllocator<1>> GetAnimatedPropertyTypes()
	{
		FAnimatedPropertyKey Key = FAnimatedPropertyKey::FromPropertyType(FSoftObjectProperty::StaticClass());
		Key.ObjectTypeName = AActor::StaticClass()->GetFName();

		return TArray<FAnimatedPropertyKey, TInlineAllocator<1>>({ Key, FAnimatedPropertyKey::FromObjectType(AActor::StaticClass()) });
	}

	/**
	 * Creates an instance of this class (called by a sequencer).
	 *
	 * @param OwningSequencer The sequencer instance to be used by this tool.
	 * @return The new instance of this class.
	 */
	static TSharedRef<ISequencerTrackEditor> CreateTrackEditor(TSharedRef<ISequencer> OwningSequencer);

protected:

	//~ FPropertyTrackEditor interface

	virtual void GenerateKeysFromPropertyChanged(const FPropertyChangedParams& PropertyChangedParams, FGeneratedTrackKeys& OutGeneratedKeys) override;
};
