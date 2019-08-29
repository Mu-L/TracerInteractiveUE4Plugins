// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class OVRPlugin : ModuleRules
{
    public OVRPlugin(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

		string SourceDirectory = Target.UEThirdPartySourceDirectory + "Oculus/OVRPlugin/OVRPlugin/";

		PublicIncludePaths.Add(SourceDirectory + "Include");

        if (Target.Platform == UnrealTargetPlatform.Android)
		{
			PublicLibraryPaths.Add(SourceDirectory + "Lib/armeabi-v7a/");
            PublicLibraryPaths.Add(SourceDirectory + "ExtLibs/armeabi-v7a/");
            
            PublicLibraryPaths.Add(SourceDirectory + "Lib/arm64-v8a/");
            PublicLibraryPaths.Add(SourceDirectory + "ExtLibs/arm64-v8a/");

            PublicAdditionalLibraries.Add("OVRPlugin");
            PublicAdditionalLibraries.Add("vrapi");
            PublicAdditionalLibraries.Add("vrintegrationloader");
        }
        if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicLibraryPaths.Add(SourceDirectory + "Lib/Win64/");
			PublicAdditionalLibraries.Add("OVRPlugin.lib");
		}
		else if (Target.Platform == UnrealTargetPlatform.Win32 )
		{
			PublicLibraryPaths.Add(SourceDirectory + "Lib/Win32/");
			PublicAdditionalLibraries.Add("OVRPlugin.lib");
		}
    }
}