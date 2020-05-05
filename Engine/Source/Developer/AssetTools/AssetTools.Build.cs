// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AssetTools : ModuleRules
{
	public AssetTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.Add("Developer/AssetTools/Private");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
                "CurveAssetEditor",
				"Engine",
                "InputCore",
				"ApplicationCore",
				"Slate",
				"SlateCore",
                "EditorStyle",
				"SourceControl",
				"TextureEditor",
				"UnrealEd",
				"PropertyEditor",
				"Kismet",
				"Landscape",
                "Foliage",
                "Projects",
				"RHI",
				"MaterialEditor",
				"ToolMenus",
            }
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Analytics",
				"AssetRegistry",
				"ContentBrowser",
				"CollectionManager",
                "CurveAssetEditor",
				"DesktopPlatform",
				"EditorWidgets",
				"GameProjectGeneration",
                "PropertyEditor",
                "ActorPickerMode",
				"Kismet",
				"MainFrame",
				"MaterialEditor",
				"MessageLog",
				"Persona",
				"FontEditor",
                "AudioEditor",
				"SourceControl",
				"Landscape",
                "SkeletonEditor",
                "SkeletalMeshEditor",
                "AnimationEditor",
                "AnimationBlueprintEditor",
                "AnimationModifiers"
            }
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"AssetRegistry",
				"ContentBrowser",
				"CollectionManager",
				"CurveTableEditor",
				"DataTableEditor",
				"DesktopPlatform",
				"EditorWidgets",
				"GameProjectGeneration",
                "ActorPickerMode",
				"MainFrame",
				"MessageLog",
				"Persona",
				"FontEditor",
                "AudioEditor",
                "SkeletonEditor",
                "SkeletalMeshEditor",
                "AnimationEditor",
                "AnimationBlueprintEditor",
                "AnimationModifiers"
            }
		);
	}
}
