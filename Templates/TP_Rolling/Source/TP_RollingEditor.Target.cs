// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class TP_RollingEditorTarget : TargetRules
{
    public TP_RollingEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		ExtraModuleNames.Add("TP_Rolling");
	}
}
