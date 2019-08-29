// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Sections/MovieScene3DAttachSection.h"
#include "UObject/SequencerObjectVersion.h"


UMovieScene3DAttachSection::UMovieScene3DAttachSection( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{
	AttachSocketName = NAME_None;
	AttachComponentName = NAME_None;
	AttachmentLocationRule = EAttachmentRule::KeepRelative;
	AttachmentRotationRule = EAttachmentRule::KeepRelative;
	AttachmentScaleRule = EAttachmentRule::KeepRelative;
	DetachmentLocationRule = EDetachmentRule::KeepRelative;
	DetachmentRotationRule = EDetachmentRule::KeepRelative;
	DetachmentScaleRule = EDetachmentRule::KeepRelative;

	EvalOptions.EnableAndSetCompletionMode
		(GetLinkerCustomVersion(FSequencerObjectVersion::GUID) < FSequencerObjectVersion::WhenFinishedDefaultsToProjectDefault ? 
			EMovieSceneCompletionMode::RestoreState : 
			EMovieSceneCompletionMode::ProjectDefault);
}


void UMovieScene3DAttachSection::SetAttachTargetID( const FMovieSceneObjectBindingID& InAttachBindingID )
{
	if (TryModify())
	{
		ConstraintBindingID = InAttachBindingID;
	}
}

