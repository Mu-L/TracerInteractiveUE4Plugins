// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class WorldBrowser : ModuleRules
{
    public WorldBrowser(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicIncludePaths.Add("Editor/WorldBrowser/Public");

        PrivateIncludePaths.Add("Editor/WorldBrowser/Private");	// For PCH includes (because they don't work with relative paths, yet)

        PrivateIncludePathModuleNames.AddRange(
            new string[] {
                "AssetRegistry",
				"AssetTools",
                "ContentBrowser",
				"Landscape",
                "MeshUtilities",
                "MaterialUtilities",
                "MeshMergeUtilities",
            }
        );
     
        PrivateDependencyModuleNames.AddRange(
            new string[] {
				"ApplicationCore",
                "AppFramework",
                "Core", 
                "CoreUObject",
                "RenderCore",
                "InputCore",
                "Engine",
				"Landscape",
                "Slate",
				"SlateCore",
                "EditorStyle",
                "UnrealEd",
                "GraphEditor",
                "LevelEditor",
                "PropertyEditor",
                "DesktopPlatform",
                "MainFrame",
                "SourceControl",
				"SourceControlWindows",
                "MeshDescription",
				"StaticMeshDescription",
				"NewLevelDialog",
				"LandscapeEditor",
                "FoliageEdit",
                "ImageWrapper",
                "Foliage",
                "MaterialUtilities",
                "RHI",
                "Json",
				"ToolMenus",
            }
		);

        DynamicallyLoadedModuleNames.AddRange(
            new string[] {
                "AssetRegistry",
				"AssetTools",
				"SceneOutliner",
                "MeshUtilities",
                "ContentBrowser",
                "MeshMergeUtilities",
            }
		);
    }
}
