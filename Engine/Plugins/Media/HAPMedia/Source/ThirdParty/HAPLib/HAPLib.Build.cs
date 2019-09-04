// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using System;
using System.IO;
using UnrealBuildTool;

public class HAPLib : ModuleRules
{
    public HAPLib(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string IncPath = Path.Combine(ModuleDirectory, "include");
            PublicSystemIncludePaths.Add(IncPath);

            string LibPath = Path.Combine(ModuleDirectory, "lib");
            PublicLibraryPaths.Add(LibPath);
            PublicAdditionalLibraries.Add("HAP.lib");
        }
    }
}
	