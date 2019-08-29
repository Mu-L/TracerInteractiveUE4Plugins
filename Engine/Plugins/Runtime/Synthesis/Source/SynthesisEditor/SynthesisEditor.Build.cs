// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
				}
			);

            PrivateIncludePathModuleNames.AddRange(
            new string[] {
                    "AssetTools"
            });
        }
	}
}