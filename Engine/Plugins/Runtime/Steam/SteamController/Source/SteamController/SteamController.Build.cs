// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class SteamController : ModuleRules
{
    public SteamController(ReadOnlyTargetRules Target) : base(Target)
    {
        string SteamVersion = "Steamv142";
        bool bSteamSDKFound = Directory.Exists(Target.UEThirdPartySourceDirectory + "Steamworks/" + SteamVersion) == true;

        PublicDefinitions.Add("STEAMSDK_FOUND=" + (bSteamSDKFound ? "1" : "0"));

        PrivateIncludePathModuleNames.Add("TargetPlatform");

        PrivateDependencyModuleNames.AddRange(new string[]
        {
			"Core",
			"CoreUObject",
			"Engine",
		});

        PublicDependencyModuleNames.Add("InputDevice");
        PublicDependencyModuleNames.Add("InputCore");

        AddEngineThirdPartyPrivateStaticDependencies(Target,
            "Steamworks"
        );
    }
}
