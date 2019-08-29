// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SkeletonEditor : ModuleRules
{
	public SkeletonEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"ApplicationCore",
                "InputCore",
				"Slate",
				"SlateCore",
                "EditorStyle",
                "UnrealEd",
                "Persona",
                "AnimGraph",
                "AnimGraphRuntime",
                "ContentBrowser",
                "AssetRegistry",
                "BlueprintGraph",
                "Kismet",
                "PinnedCommandList",
            }
		);

        PublicIncludePathModuleNames.AddRange(
            new string[] {
                "Persona",
            }
        );

        PrivateIncludePathModuleNames.AddRange(
            new string[] {
                "PropertyEditor",
            }
        );

        DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"PropertyEditor",
			}
		);
	}
}
