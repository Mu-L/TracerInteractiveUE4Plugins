// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class SynthesisEditor : ModuleRules
	{
        public SynthesisEditor(ReadOnlyTargetRules Target) : base(Target)
		{

            PublicDependencyModuleNames.AddRange(
				new string[] {
                    "Core",
					"CoreUObject",
					"Engine",
                    "UnrealEd",
					"AudioEditor",
                    "Synthesis",
					"AudioMixer",
                    "ToolMenus",
                    "EditorStyle",
                    "Slate",
                    "SlateCore",
                    "ContentBrowser",
                }
			);

            PrivateIncludePathModuleNames.AddRange(
            new string[] {
                    "AssetTools"
            });
        }
	}
}