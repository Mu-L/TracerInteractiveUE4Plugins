// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MovieScene : ModuleRules
{
	public MovieScene(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.Add("Runtime/MovieScene/Private");

        PrivateIncludePathModuleNames.AddRange(
            new string[] {
                "TargetPlatform"
            }
        );

        PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
                "InputCore",
                "Engine",
				"TimeManagement"
			}
		);

    }
}
