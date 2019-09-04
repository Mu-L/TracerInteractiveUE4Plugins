// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Blutility : ModuleRules
{
	public Blutility(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.Add("Editor/Blutility/Private");

		PrivateIncludePathModuleNames.Add("AssetTools");

        PublicDependencyModuleNames.AddRange(new string[] {
			"EditorSubsystem",
			"MainFrame"
		});

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
                "InputCore",
				"Slate",
				"SlateCore",
                "EditorStyle",
				"UnrealEd",
				"Kismet",
				"AssetRegistry",
				"AssetTools",
				"WorkspaceMenuStructure",
				"ContentBrowser",
				"ClassViewer",
				"CollectionManager",
                "PropertyEditor",
                "BlueprintGraph",
				"UMG",
                "UMGEditor",
                "KismetCompiler"
            }
			);
	}
}
