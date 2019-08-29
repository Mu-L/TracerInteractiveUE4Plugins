// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ControlRigSequenceFactory.h"
#include "Sequencer/ControlRigSequence.h"
#include "MovieScene.h"
#include "MovieSceneToolsProjectSettings.h"

#define LOCTEXT_NAMESPACE "MovieSceneFactory"

UControlRigSequenceFactory::UControlRigSequenceFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UControlRigSequence::StaticClass();
}

UObject* UControlRigSequenceFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UControlRigSequence* NewSequence = NewObject<UControlRigSequence>(InParent, Name, Flags | RF_Transactional);
	NewSequence->Initialize();
	
	// Set up some sensible defaults
	const UMovieSceneToolsProjectSettings* ProjectSettings = GetDefault<UMovieSceneToolsProjectSettings>();

	FFrameRate TickResolution = NewSequence->GetMovieScene()->GetTickResolution();
	NewSequence->GetMovieScene()->SetPlaybackRange((ProjectSettings->DefaultStartTime*TickResolution).FloorToFrame(), (ProjectSettings->DefaultDuration*TickResolution).FloorToFrame().Value);

	return NewSequence;
}

bool UControlRigSequenceFactory::ShouldShowInNewMenu() const
{
	return true;
}

#undef LOCTEXT_NAMESPACE
