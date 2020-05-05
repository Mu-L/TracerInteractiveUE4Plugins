// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TP_Rolling : ModuleRules
{
    public TP_Rolling(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });
	}
}
