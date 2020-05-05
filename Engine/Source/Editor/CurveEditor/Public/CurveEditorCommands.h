// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "EditorStyleSet.h"
#include "Framework/Commands/Commands.h"

/**
 * Defines commands for SCurveEditorPanel (and UnrealEd::SCurveEditor)
 */
class CURVEEDITOR_API FCurveEditorCommands : public TCommands<FCurveEditorCommands>
{
public:
	FCurveEditorCommands()
		: TCommands<FCurveEditorCommands>
		(
			TEXT("GenericCurveEditor"),
			NSLOCTEXT("Contexts", "GenericCurveEditor", "Curve Editor"),
			NAME_None,
			FEditorStyle::GetStyleSetName()
		)
	{
	}

	TSharedPtr<FUICommandInfo> ZoomToFitHorizontal;
	TSharedPtr<FUICommandInfo> ZoomToFitVertical;
	TSharedPtr<FUICommandInfo> ZoomToFit;
	TSharedPtr<FUICommandInfo> ZoomToFitAll;
	TSharedPtr<FUICommandInfo> ToggleInputSnapping;
	TSharedPtr<FUICommandInfo> ToggleOutputSnapping;

	TSharedPtr<FUICommandInfo> InterpolationConstant;
	TSharedPtr<FUICommandInfo> InterpolationLinear;
	TSharedPtr<FUICommandInfo> InterpolationCubicAuto;
	TSharedPtr<FUICommandInfo> InterpolationCubicUser;
	TSharedPtr<FUICommandInfo> InterpolationCubicBreak;
	TSharedPtr<FUICommandInfo> InterpolationToggleWeighted;


	TSharedPtr<FUICommandInfo> FlattenTangents;
	TSharedPtr<FUICommandInfo> StraightenTangents;

	TSharedPtr<FUICommandInfo> BakeCurve;
	TSharedPtr<FUICommandInfo> ReduceCurve;

	TSharedPtr<FUICommandInfo> SetPreInfinityExtrapCycle;
	TSharedPtr<FUICommandInfo> SetPreInfinityExtrapCycleWithOffset;
	TSharedPtr<FUICommandInfo> SetPreInfinityExtrapOscillate;
	TSharedPtr<FUICommandInfo> SetPreInfinityExtrapLinear;
	TSharedPtr<FUICommandInfo> SetPreInfinityExtrapConstant;

	TSharedPtr<FUICommandInfo> SetPostInfinityExtrapCycle;
	TSharedPtr<FUICommandInfo> SetPostInfinityExtrapCycleWithOffset;
	TSharedPtr<FUICommandInfo> SetPostInfinityExtrapOscillate;
	TSharedPtr<FUICommandInfo> SetPostInfinityExtrapLinear;
	TSharedPtr<FUICommandInfo> SetPostInfinityExtrapConstant;

	TSharedPtr<FUICommandInfo> SetAllTangentsVisibility;
	TSharedPtr<FUICommandInfo> SetSelectedKeysTangentVisibility;
	TSharedPtr<FUICommandInfo> SetNoTangentsVisibility;

	TSharedPtr<FUICommandInfo> ToggleAutoFrameCurveEditor;
	TSharedPtr<FUICommandInfo> ToggleShowCurveEditorCurveToolTips;

	TSharedPtr<FUICommandInfo> AddKeyHovered;

	TSharedPtr<FUICommandInfo> AddKeyToAllCurves;

	TSharedPtr<FUICommandInfo> SetViewModeAbsolute;
	TSharedPtr<FUICommandInfo> SetViewModeStacked;
	TSharedPtr<FUICommandInfo> SetViewModeNormalized;

	TSharedPtr<FUICommandInfo> DeactivateCurrentTool;
	TSharedPtr<FUICommandInfo> DeselectAllKeys;

	TSharedPtr<FUICommandInfo> BufferVisibleCurves;
	TSharedPtr<FUICommandInfo> ApplyBufferedCurves;

	// User Filtering
	TSharedPtr<FUICommandInfo> OpenUserImplementableFilterWindow;

	// Axis Snapping
	TSharedPtr<FUICommandInfo> SetAxisSnappingNone;
	TSharedPtr<FUICommandInfo> SetAxisSnappingHorizontal;
	TSharedPtr<FUICommandInfo> SetAxisSnappingVertical;

	// Time Management
	TSharedPtr<FUICommandInfo> StepToNextKey;
	TSharedPtr<FUICommandInfo> StepToPreviousKey;
	TSharedPtr<FUICommandInfo> StepForward;
	TSharedPtr<FUICommandInfo> StepBackward;
	TSharedPtr<FUICommandInfo> JumpToStart;
	TSharedPtr<FUICommandInfo> JumpToEnd;

public:
	virtual void RegisterCommands() override;
};
