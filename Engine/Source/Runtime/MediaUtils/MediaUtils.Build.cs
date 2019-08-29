// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MediaUtils : ModuleRules
	{
		public MediaUtils(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"Media",
				});

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"ImageWrapper",
				});

			PrivateIncludePaths.AddRange(
				new string[] {
					"Runtime/MediaUtils/Private",
				});
		}
	}
}
