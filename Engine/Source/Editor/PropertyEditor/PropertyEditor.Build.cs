// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PropertyEditor : ModuleRules
{
	public PropertyEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"UnrealEd",
                "ActorPickerMode",
                "SceneDepthPickerMode",
			}
		);
		
        PublicIncludePathModuleNames.AddRange(
            new string[] {                
                "IntroTutorials"
            }
        );

		PrivateIncludePaths.AddRange(
			new string[] {
				"Editor/PropertyEditor/Private",
				"Editor/PropertyEditor/Private/Presentation",
				"Editor/PropertyEditor/Private/Presentation/PropertyTable",
				"Editor/PropertyEditor/Private/Presentation/PropertyEditor",
				"Editor/PropertyEditor/Private/UserInterface",
				"Editor/PropertyEditor/Private/UserInterface/PropertyTable",
				"Editor/PropertyEditor/Private/UserInterface/PropertyEditor",
				"Editor/PropertyEditor/Private/UserInterface/PropertyTree",
				"Editor/PropertyEditor/Private/UserInterface/PropertyDetails",
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"MainFrame",
                "AssetRegistry",
                "AssetTools",
				"ClassViewer",
				"StructViewer",
				"ContentBrowser",
				"ConfigEditor",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
                "AppFramework",
				"Core",
				"CoreUObject",
				"ApplicationCore",
				"EditorStyle",
				"Engine",
				"InputCore",
				"Slate",
				"SlateCore",
				"EditorWidgets",
				"Documentation",
                "RHI",
				"ConfigEditor",
                "SceneOutliner",
				"DesktopPlatform",
            }
        );

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
                "AssetRegistry",
                "AssetTools",
				"ClassViewer",
				"StructViewer",
				"ContentBrowser",
				"MainFrame",
			}
		);
	}
}
