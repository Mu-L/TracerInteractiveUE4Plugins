// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DesktopPlatform : ModuleRules
{
	public DesktopPlatform(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.Add("Developer/DesktopPlatform/Private");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"ApplicationCore",
				"Json",
			}
		);

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Linux))
		{
			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"SlateFileDialogs",
				}
			);

			DynamicallyLoadedModuleNames.AddRange(
				new string[] {
					"SlateFileDialogs",
				}
			);

			AddEngineThirdPartyPrivateStaticDependencies(Target, "SDL2");
		}
	}
}
