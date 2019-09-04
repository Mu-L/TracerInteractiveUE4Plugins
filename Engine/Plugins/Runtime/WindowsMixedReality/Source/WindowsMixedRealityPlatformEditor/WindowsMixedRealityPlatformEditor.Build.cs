// Copyright (c) Microsoft Corporation. All rights reserved.

using UnrealBuildTool;

public class WindowsMixedRealityPlatformEditor : ModuleRules
{
	public WindowsMixedRealityPlatformEditor(ReadOnlyTargetRules Target) : base(Target)
	{
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
                "UnrealEd",
                "SharedSettingsWidgets",
				"WindowsMixedRealityRuntimeSettings",
				"WindowsMixedRealityHMD",
                "WindowsMixedRealitySpatialInput",
                "TargetPlatform",
				"RenderCore",
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Settings"
			}
		);

        AddEngineThirdPartyPrivateStaticDependencies(Target, "WindowsMixedRealityInterop");
    }
}
