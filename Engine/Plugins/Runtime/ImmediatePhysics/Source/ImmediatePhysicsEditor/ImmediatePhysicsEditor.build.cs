// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ImmediatePhysicsEditor: ModuleRules
{
	public ImmediatePhysicsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
                "Engine",
                "AnimGraph",
                "BlueprintGraph",
                "ImmediatePhysics",
                "UnrealEd"
			}
		);

        PrivateIncludePaths.AddRange(
            new string[] {
            });
    }
}
