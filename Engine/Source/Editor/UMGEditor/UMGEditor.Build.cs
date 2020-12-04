// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UMGEditor : ModuleRules
{
	public UMGEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.AddRange(
			new string[] {
				"Editor/UMGEditor/Private", // For PCH includes (because they don't work with relative paths, yet)
				"Editor/UMGEditor/Private/Templates",
				"Editor/UMGEditor/Private/Extensions",
				"Editor/UMGEditor/Private/Customizations",
				"Editor/UMGEditor/Private/BlueprintModes",
				"Editor/UMGEditor/Private/TabFactory",
				"Editor/UMGEditor/Private/Designer",
				"Editor/UMGEditor/Private/Hierarchy",
				"Editor/UMGEditor/Private/Palette",
				"Editor/UMGEditor/Private/Details",
				"Editor/UMGEditor/Private/DragDrop",
                "Editor/UMGEditor/Private/Utility",
			});

		OverridePackageType = PackageOverrideType.EngineDeveloper;

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"AssetTools",
				"UMG",
			});

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Sequencer",
            });

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"ApplicationCore",
				"InputCore",
				"Engine",
				"AssetTools",
				"UnrealEd", // for FAssetEditorManager
				"KismetWidgets",
				"KismetCompiler",
				"BlueprintGraph",
				"GraphEditor",
				"Kismet",  // for FWorkflowCentricApplication
				"PropertyPath",
				"PropertyEditor",
				"UMG",
				"EditorStyle",
				"Slate",
				"SlateCore",
				"SlateRHIRenderer",
				"MessageLog",
				"MovieScene",
				"MovieSceneTools",
                "MovieSceneTracks",
				"DetailCustomizations",
                "Settings",
				"RenderCore",
                "TargetPlatform",
				"TimeManagement",
				"GameProjectGeneration",
				"PropertyPath",
				"ToolMenus",
				"SlateReflector",
				"DeveloperSettings",
			}
			);
	}
}
