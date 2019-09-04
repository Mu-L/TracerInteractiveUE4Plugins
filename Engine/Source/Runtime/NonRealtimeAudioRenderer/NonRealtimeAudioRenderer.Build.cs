// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class NonRealtimeAudioRenderer : ModuleRules
	{
		public NonRealtimeAudioRenderer(ReadOnlyTargetRules Target) : base(Target)
		{
            PrivateIncludePathModuleNames.Add("TargetPlatform");

            PrivateIncludePaths.AddRange(
				new string[]
				{
					"Runtime/AudioMixer/Private",
				}
			);

            PublicIncludePaths.Add("Runtime/AudioMixer/Public");

            PrivateDependencyModuleNames.AddRange(
            new string[] {
                    "Core",
                    "CoreUObject",
                    "Engine",
                    "AudioMixer",
                }
			);

            AddEngineThirdPartyPrivateStaticDependencies(Target,
					"UEOgg",
					"Vorbis",
					"VorbisFile",
					"libOpus",
					"UELibSampleRate"
					);

            if (Target.Platform == UnrealTargetPlatform.XboxOne)
            {
                PrivateDependencyModuleNames.Add("XMA2");
            }
        }
	}
}
