// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ALAudio : ModuleRules
{
    public ALAudio(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateIncludePathModuleNames.Add("TargetPlatform");

        PrivateDependencyModuleNames.AddRange(new string[]
        {
			"Core",
			"CoreUObject",
			"Engine",
		});

        AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenAL");
        AddEngineThirdPartyPrivateStaticDependencies(Target,
            "OpenAL",
            "UEOgg",
            "Vorbis",
            "VorbisFile"
        );

		if(Target.Platform != UnrealTargetPlatform.HTML5)
		{
			PrecompileForTargets = PrecompileTargetsType.None;
		}
    }
}
