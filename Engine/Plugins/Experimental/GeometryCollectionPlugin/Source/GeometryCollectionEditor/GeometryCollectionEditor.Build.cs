// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class GeometryCollectionEditor : ModuleRules
	{
        public GeometryCollectionEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add("GeometryCollectionEditor/Private");
            PublicIncludePaths.Add(ModuleDirectory + "/Public");

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
				    "Slate",
				    "SlateCore",
				    "Engine",
				    "UnrealEd",
				    "PropertyEditor",
				    "RenderCore",
				    "RHI",
                    "GeometryCollectionEngine",
                    "RawMesh",
				    "AssetTools",
				    "AssetRegistry",
				    "SceneOutliner",
					"EditorStyle",
					"ToolMenus",
					"Chaos"
				}
			);

			PrivateDefinitions.Add("CHAOS_INCLUDE_LEVEL_1=1");
		}
	}
}
