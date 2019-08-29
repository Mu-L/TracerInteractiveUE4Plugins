// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MovieSceneCapture : ModuleRules
{
	public MovieSceneCapture(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.AddRange(
			new string[] {
				"Runtime/MovieSceneCapture/Private"
			}
		);

		if (Target.bBuildDeveloperTools)
		{
			PublicIncludePathModuleNames.Add("ImageWrapper");
			DynamicallyLoadedModuleNames.Add("ImageWrapper");
		}

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"LevelSequence",
				"TimeManagement",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
                "AssetRegistry",
				"AVIWriter",
                "Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"ImageWriteQueue",
				"Json",
				"JsonUtilities",
				"MovieScene",
                "MovieSceneTracks",
                "RenderCore",
				"RHI",
				"Slate",
				"SlateCore",
				"AudioMixer"
			}
		);
    }
}
