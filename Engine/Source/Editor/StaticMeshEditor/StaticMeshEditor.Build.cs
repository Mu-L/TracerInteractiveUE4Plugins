// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class StaticMeshEditor : ModuleRules
{
	public StaticMeshEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"AssetRegistry",
				"Kismet",
				"EditorWidgets",
				"MeshUtilities",
                "PropertyEditor",
                "MeshReductionInterface",
            }
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"ApplicationCore",
				"Engine",
                "InputCore",
				"Slate",
				"SlateCore",
                "EditorStyle",
				"RenderCore",
				"RHI",
				"UnrealEd",
				"TargetPlatform",
				"RawMesh",
                "PropertyEditor",
				"MeshUtilities",
                "Json",
                "JsonUtilities",
                "AdvancedPreviewScene",
                "DesktopPlatform",
                "DesktopWidgets"
            }
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"SceneOutliner",
				"ClassViewer",
				"ContentBrowser",
				"WorkspaceMenuStructure",
                "MeshReductionInterface",
            }
		);

		SetupModulePhysicsSupport(Target);
	}
}
