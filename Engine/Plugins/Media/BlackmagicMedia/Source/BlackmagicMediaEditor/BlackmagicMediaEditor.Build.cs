// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class BlackmagicMediaEditor : ModuleRules
	{
		public BlackmagicMediaEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"MediaIOCore",
					"UnrealEd"
				});

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"BlackmagicMedia",
					"BlackmagicMediaOutput",
					"MediaAssets",
					"MediaIOEditor",
					"Projects",
					"SlateCore",
				});

			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"AssetTools",
				});

			PrivateIncludePaths.Add("BlackmagicMediaEditor/Private");
		}
	}
}
