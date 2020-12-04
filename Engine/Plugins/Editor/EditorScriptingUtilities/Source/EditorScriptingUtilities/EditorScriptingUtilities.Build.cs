// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

namespace UnrealBuildTool.Rules
{
	public class EditorScriptingUtilities : ModuleRules
	{
		public EditorScriptingUtilities(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[] {
					"AssetRegistry",
					"Core",
					"CoreUObject",
					"Engine",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"AssetTools",
					"EditorStyle",
					"MainFrame",
					"MeshDescription",
					"RawMesh",
					"Slate",
					"SlateCore",
					"StaticMeshDescription",
					"UnrealEd",
                    "SkeletalMeshUtilitiesCommon",
                    "LevelEditor",
					"PhysicsCore"
                }
            );
		}
	}
}
