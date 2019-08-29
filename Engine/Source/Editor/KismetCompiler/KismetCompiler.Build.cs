// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class KismetCompiler : ModuleRules
{
	public KismetCompiler(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
                "InputCore",
				"UnrealEd",
				"MovieScene",
				"MovieSceneTools",
				"BlueprintGraph",
				"AnimGraph",
                "MessageLog",
				"Kismet",
			}
			);
	}
}
