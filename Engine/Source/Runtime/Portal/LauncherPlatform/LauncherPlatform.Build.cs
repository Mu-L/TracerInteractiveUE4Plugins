// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LauncherPlatform : ModuleRules
{
    public LauncherPlatform(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateIncludePaths.Add("Runtime/Portal/LauncherPlatform/Private");

        PrivateDependencyModuleNames.AddRange(
            new string[] {
                "Core",
            }
        );

		if (!Target.IsInPlatformGroup(UnrealPlatformGroup.Unix) && Target.Platform != UnrealTargetPlatform.Win32 && Target.Platform != UnrealTargetPlatform.Win64 && Target.Platform != UnrealTargetPlatform.Mac)
        {
            PrecompileForTargets = PrecompileTargetsType.None;
        }
    }
}
