// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TP_2DSideScroller : ModuleRules
{
	public TP_2DSideScroller(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "Paper2D" });
	}
}
