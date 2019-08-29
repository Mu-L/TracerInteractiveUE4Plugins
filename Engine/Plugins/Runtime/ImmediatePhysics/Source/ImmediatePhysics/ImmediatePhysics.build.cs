// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ImmediatePhysics: ModuleRules
{
	public ImmediatePhysics(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
                "Engine",
                "AnimGraphRuntime",
                "PhysX",
                "APEX"  //This is not really needed, but the includes are all coupled. Not really an issue at the moment, could be broken out in the future

			}
		);

        PrivateIncludePaths.AddRange(
            new string[] {
            });
    }
}
