// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LocalizationService : ModuleRules
{
	public LocalizationService(ReadOnlyTargetRules Target) : base(Target)
	{
        PrivateIncludePaths.Add("Developer/LocalizationService/Private");

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
                "Engine",
				"InputCore",
			}
		);

        if (!Target.IsInPlatformGroup(UnrealPlatformGroup.Linux))
        {
            PrivateDependencyModuleNames.AddRange(
                new string[] {
                    "Slate",
                    "SlateCore",
                    "EditorStyle"
                }
            );
        }

        if (Target.bBuildEditor)
        {
			PrivateDependencyModuleNames.AddRange(
                new string[] {
					"UnrealEd",
                    "PropertyEditor",
            }
            );
        }

		if (Target.bBuildDeveloperTools)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"MessageLog",
				}
			);
		}
	}
}
