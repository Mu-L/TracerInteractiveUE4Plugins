// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class TimeSynth : ModuleRules
	{
        public TimeSynth(ReadOnlyTargetRules Target) : base(Target)
		{
            PrivateDependencyModuleNames.AddRange(
				new string[] {
                    "Core",
					"CoreUObject",
					"Engine",
					"AudioMixer",
					"SignalProcessing",
                    "UMG",
                    "Slate",
                    "SlateCore",
                    "InputCore",
                    "Projects"
                }
            );
		}
	}
}