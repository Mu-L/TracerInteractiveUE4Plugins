// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LinuxTargetPlatform : ModuleRules
{
    public LinuxTargetPlatform(ReadOnlyTargetRules Target) : base(Target)
	{
        BinariesSubFolder = "Linux";

		PrivateDependencyModuleNames.AddRange(
            new string[] {
				"Core",
				"CoreUObject",
				"TargetPlatform",
				"DesktopPlatform",
				"Projects"
			}
        );

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Settings",
			}
		);

        PrivateIncludePaths.AddRange(
            new string[] {
			}
        );

		if (Target.bCompileAgainstEngine)
		{
			PrivateDependencyModuleNames.Add("Engine");
			PrivateIncludePathModuleNames.Add("TextureCompressor");
		}
	}
}
