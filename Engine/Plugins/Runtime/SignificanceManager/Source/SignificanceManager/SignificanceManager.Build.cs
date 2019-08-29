// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SignificanceManager : ModuleRules
{
	public SignificanceManager(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"EngineSettings"
			}
		);

        PrivateIncludePaths.AddRange(
            new string[] {
                "SignificanceManager/Private"
            })
		;
	}
}
