// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "ControlRigEditorStyle.h"

class FControlRigEditModeCommands : public TCommands<FControlRigEditModeCommands>
{

public:
	FControlRigEditModeCommands() : TCommands<FControlRigEditModeCommands>
	(
		"ControlRigEditMode",
		NSLOCTEXT("Contexts", "RigAnimation", "Rig Animation"),
		NAME_None, // "MainFrame" // @todo Fix this crash
		FControlRigEditorStyle::Get().GetStyleSetName() // Icon Style Set
	)
	{}
	
	/** Sets a key at the current time for the selected nodes */
	TSharedPtr< FUICommandInfo > SetKey;

	/** Toggles hiding all manipulators in the viewport */
	TSharedPtr< FUICommandInfo > ToggleManipulators;

	/** Toggles hiding all trajectories in the viewport */
	TSharedPtr< FUICommandInfo > ToggleTrajectories;

	/** Export this sequence to an optimized skeleton-specific animation sequence */
	TSharedPtr< FUICommandInfo > ExportAnimSequence;

	/** Re-export this sequence to an animation sequence using the previous export settings */
	TSharedPtr< FUICommandInfo > ReExportAnimSequence;

	/** Import this animation sequence from a source rig sequence */
	TSharedPtr< FUICommandInfo > ImportFromRigSequence;

	/** Re-import this animation sequence from it's source rig sequence */
	TSharedPtr< FUICommandInfo > ReImportFromRigSequence;

	/**
	 * Initialize commands
	 */
	virtual void RegisterCommands() override;
};
