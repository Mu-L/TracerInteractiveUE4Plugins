// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MagicLeapHandTracking : ModuleRules
	{
		public MagicLeapHandTracking(ReadOnlyTargetRules Target)
				: base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"InputDevice"
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"InputCore",
					"Slate",
					"HeadMountedDisplay",
					"LuminRuntimeSettings",
					"MagicLeap",
					"MLSDK",
					"SlateCore",
					"LiveLinkInterface",
				}
			);

			// This is not ideal but needs to be done in order to expose the private MagicLeapHMD header to this module.
			PrivateIncludePaths.Add(Path.Combine(new string[] { ModuleDirectory, "..", "MagicLeap", "Private" }));
		}
	}
}
