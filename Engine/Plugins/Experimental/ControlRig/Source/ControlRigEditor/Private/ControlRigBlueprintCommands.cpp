// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigBlueprintCommands.h"

#define LOCTEXT_NAMESPACE "ControlRigBlueprintCommands"

void FControlRigBlueprintCommands::RegisterCommands()
{
	UI_COMMAND(DeleteItem, "Delete", "Deletes the selected items and removes their nodes from the graph.", EUserInterfaceActionType::Button, FInputChord(EKeys::Delete));
	UI_COMMAND(ExecuteGraph, "Execute", "Execute the rig graph if On.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(AutoCompileGraph, "AutoCompile", "Auto-compile the rig graph if On.", EUserInterfaceActionType::ToggleButton, FInputChord());
}

#undef LOCTEXT_NAMESPACE
