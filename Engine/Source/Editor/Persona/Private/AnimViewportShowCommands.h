// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EngineDefines.h"
#include "Framework/Commands/Commands.h"
#include "EditorStyleSet.h"

/**
 * Class containing commands for persona viewport show actions
 */
class FAnimViewportShowCommands : public TCommands<FAnimViewportShowCommands>
{
public:
	FAnimViewportShowCommands() 
		: TCommands<FAnimViewportShowCommands>
		(
			TEXT("AnimViewportShowCmd"), // Context name for fast lookup
			NSLOCTEXT("Contexts", "AnimViewportShowCmd", "Animation Viewport Show Command"), // Localized context name for displaying
			NAME_None, // Parent context name. 
			FEditorStyle::GetStyleSetName() // Icon Style Set
		)
	{
	}

	/** Option to align floor to Mesh */
	TSharedPtr< FUICommandInfo > AutoAlignFloorToMesh;

	/** Option to mute audio in the viewport */
	TSharedPtr< FUICommandInfo > MuteAudio;

	/** Option to use audio attenuation in the viewport */
	TSharedPtr< FUICommandInfo > UseAudioAttenuation;

	/** Option to show root motion in viewport */
	TSharedPtr< FUICommandInfo > ProcessRootMotion;

	/** Option to enable/disable post process anim blueprint evaluation */
	TSharedPtr< FUICommandInfo > DisablePostProcessBlueprint;

	/** Show reference pose on preview mesh */
	TSharedPtr< FUICommandInfo > ShowRetargetBasePose;
	
	/** Show Bound of preview mesh */
	TSharedPtr< FUICommandInfo > ShowBound;

	/** Use in-game Bound of preview mesh */
	TSharedPtr< FUICommandInfo > UseInGameBound;

	/** Use in-game Bound of preview mesh */
	TSharedPtr< FUICommandInfo > UseFixedBounds;

	/** Show/hide the preview mesh */
	TSharedPtr< FUICommandInfo > ShowPreviewMesh;

	/** Show Morphtarget */
	TSharedPtr< FUICommandInfo > ShowMorphTargets;

	/** Hide all bones */
	TSharedPtr< FUICommandInfo > ShowBoneDrawNone;

	/** Show only selected bones */
	TSharedPtr< FUICommandInfo > ShowBoneDrawSelected;

	/** Show only selected bones and their parents */
	TSharedPtr< FUICommandInfo > ShowBoneDrawSelectedAndParents;

	/** Show all bones */
	TSharedPtr< FUICommandInfo > ShowBoneDrawAll;

	/** Show raw animation (vs compressed) */
	TSharedPtr< FUICommandInfo > ShowRawAnimation;

	/** Show non retargeted animation. */
	TSharedPtr< FUICommandInfo > ShowNonRetargetedAnimation;

	/** Show additive base pose */
	TSharedPtr< FUICommandInfo > ShowAdditiveBaseBones;

	/** Show non retargeted animation. */
	TSharedPtr< FUICommandInfo > ShowSourceRawAnimation;

	/** Show non retargeted animation. */
	TSharedPtr< FUICommandInfo > ShowBakedAnimation;

	/** Show skeletal mesh bone names */
	TSharedPtr< FUICommandInfo > ShowBoneNames;

	/** Show skeletal mesh info */
	TSharedPtr< FUICommandInfo > ShowDisplayInfoBasic;
	TSharedPtr< FUICommandInfo > ShowDisplayInfoDetailed;
	TSharedPtr< FUICommandInfo > ShowDisplayInfoSkelControls;
	TSharedPtr< FUICommandInfo > HideDisplayInfo;

	/** Show overlay material option */
	TSharedPtr< FUICommandInfo > ShowOverlayNone;
	TSharedPtr< FUICommandInfo > ShowBoneWeight;
	TSharedPtr< FUICommandInfo > ShowMorphTargetVerts;

	/** Show mesh vertex colors */
	TSharedPtr< FUICommandInfo > ShowVertexColors;

	/** Show socket hit point diamonds */
	TSharedPtr< FUICommandInfo > ShowSockets;

	/** Hide all local axes */
	TSharedPtr< FUICommandInfo > ShowLocalAxesNone;
	
	/** Show only selected axes */
	TSharedPtr< FUICommandInfo > ShowLocalAxesSelected;
	
	/** Show all local axes */
	TSharedPtr< FUICommandInfo > ShowLocalAxesAll;

	/** Enable cloth simulation */
	TSharedPtr< FUICommandInfo > EnableClothSimulation;

	/** Reset cloth simulation */
	TSharedPtr< FUICommandInfo > ResetClothSimulation;

	/** Enables collision detection between collision primitives in the base mesh 
	  * and clothing on any attachments in the preview scene. 
	*/
	TSharedPtr< FUICommandInfo > EnableCollisionWithAttachedClothChildren;

	/** Show all sections which means the original state */
	TSharedPtr< FUICommandInfo > ShowAllSections;
	/** Show only clothing mapped sections */
	TSharedPtr< FUICommandInfo > ShowOnlyClothSections;
	/** Show all except clothing mapped sections */
	TSharedPtr< FUICommandInfo > HideOnlyClothSections;

	TSharedPtr< FUICommandInfo > PauseClothWithAnim;

public:
	/** Registers our commands with the binding system */
	virtual void RegisterCommands() override;
};
