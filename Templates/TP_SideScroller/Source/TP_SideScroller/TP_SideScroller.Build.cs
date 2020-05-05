// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TP_SideScroller : ModuleRules
{
	public TP_SideScroller(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });
	}
}
