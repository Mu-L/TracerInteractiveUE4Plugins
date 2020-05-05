// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class BlueprintNativeCodeGen : ModuleRules
{
    public BlueprintNativeCodeGen(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
                "DesktopPlatform",
                "UnrealEd",
                "InputCore",
                "SlateCore",
                "Slate",
                "EditorStyle",
                "KismetCompiler",
                "Json",
                "JsonUtilities",
                "BlueprintCompilerCppBackend",
                "GameProjectGeneration",
                "Projects",
                "Kismet",
                "DesktopWidgets",
                "TargetPlatform"
            }
		);

        DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"AssetRegistry",
			}
		);
	}
}
