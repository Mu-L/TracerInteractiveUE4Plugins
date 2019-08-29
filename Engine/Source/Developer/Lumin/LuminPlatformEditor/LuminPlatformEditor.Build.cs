// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LuminPlatformEditor : ModuleRules
{
	public LuminPlatformEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		BinariesSubFolder = "Lumin";

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"InputCore",
				"Engine",
				"Slate",
				"SlateCore",
				"EditorStyle",
				"EditorWidgets",
				"DesktopWidgets",
				"PropertyEditor",
				"SharedSettingsWidgets",
				"SourceControl",
				"LuminRuntimeSettings",
				"AndroidDeviceDetection",
				"TargetPlatform",
				"RenderCore",
				"MaterialShaderQualitySettings",
				"AudioSettingsEditor",
				"UnrealEd",
				"PropertyPath",
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Settings",
			}
		);

		// this is listed above, so it isn't really dynamically loaded, this just marks it as being platform specific.
		//PlatformSpecificDynamicallyLoadedModuleNames.Add("LuminRuntimeSettings");
	}
}
