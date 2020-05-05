// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class UE4EditorTarget : TargetRules
{
	public UE4EditorTarget( TargetInfo Target ) : base(Target)
	{
		Type = TargetType.Editor;
		BuildEnvironment = TargetBuildEnvironment.Shared;
		bBuildAllModules = true;
		ExtraModuleNames.Add("UE4Game");
	}
}
