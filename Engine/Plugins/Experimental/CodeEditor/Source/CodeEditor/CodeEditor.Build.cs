// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class CodeEditor : ModuleRules
	{
		public CodeEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.AddRange(
				new string[] {
				}
				);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"SlateCore",
					"Slate",
					"AssetTools",
					"UnrealEd",
					"EditorStyle",
					"PropertyEditor",
					"Kismet",  // for FWorkflowCentricApplication
					"InputCore",
					"DirectoryWatcher",
					"LevelEditor",
					"Engine",
					"ToolMenus",
				}
				);
		}
	}
}
