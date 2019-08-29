// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class AvfMedia : ModuleRules
	{
		public AvfMedia(ReadOnlyTargetRules Target) : base(Target)
		{
			DynamicallyLoadedModuleNames.AddRange(
				new string[] {
					"Media",
				});

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"AvfMediaFactory",
					"Core",
					"ApplicationCore",
					"ShaderCore",
					"MediaUtils",
					"RenderCore",
					"RHI",
					"UtilityShaders",
				});

			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"UtilityShaders",
				});

			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"Media",
				});

			PrivateIncludePaths.AddRange(
				new string[] {
					"AvfMedia/Private",
					"AvfMedia/Private/Player",
					"AvfMedia/Private/Shared",
				});

			PublicFrameworks.AddRange(
				new string[] {
					"CoreMedia",
					"CoreVideo",
					"AVFoundation",
					"AudioToolbox",
					"MediaToolbox",
					"QuartzCore"
				});
		}
	}
}
