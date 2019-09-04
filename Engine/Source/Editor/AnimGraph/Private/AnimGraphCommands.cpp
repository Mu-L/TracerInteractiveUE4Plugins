// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AnimGraphCommands.h"

#define LOCTEXT_NAMESPACE "AnimGraphCommands"

void FAnimGraphCommands::RegisterCommands()
{
	UI_COMMAND(TogglePoseWatch, "Toggle Pose Watch", "Toggle Pose Watching on this node", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND( ToggleHideUnrelatedNodes, "Hide Unrelated", "Toggles automatically hiding nodes which are unrelated to the selected nodes.", EUserInterfaceActionType::ToggleButton, FInputChord() );
}

#undef LOCTEXT_NAMESPACE
