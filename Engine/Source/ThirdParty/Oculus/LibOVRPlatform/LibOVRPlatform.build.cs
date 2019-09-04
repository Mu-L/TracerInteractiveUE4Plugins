// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class LibOVRPlatform : ModuleRules
{
	public LibOVRPlatform(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;		
		
		string OculusThirdPartyDirectory = Target.UEThirdPartySourceDirectory + "Oculus/LibOVRPlatform/LibOVRPlatform/";

		bool isLibrarySupported = false;
		
		if (Target.Platform == UnrealTargetPlatform.Win32)
		{
			PublicAdditionalLibraries.Add(OculusThirdPartyDirectory + "lib/LibOVRPlatform32_1.lib");
			isLibrarySupported = true;
		}
		else if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(OculusThirdPartyDirectory + "lib/LibOVRPlatform64_1.lib");
			isLibrarySupported = true;
		}
		else if (Target.Platform == UnrealTargetPlatform.Android)
		{
			PublicLibraryPaths.Add(OculusThirdPartyDirectory + "lib/armeabi-v7a/");
			PublicLibraryPaths.Add(OculusThirdPartyDirectory + "lib/arm64-v8a/");
			PublicAdditionalLibraries.Add("ovrplatformloader");
			string PluginPath = Utils.MakePathRelativeTo(ModuleDirectory, Target.RelativeEnginePath);
			AdditionalPropertiesForReceipt.Add("AndroidPlugin", Path.Combine(PluginPath, "LibOVRPlatform_APL.xml"));
			isLibrarySupported = true;
		}
		else
		{
			System.Console.WriteLine("Oculus Platform SDK not supported for this platform");
		}
		
		if (isLibrarySupported)
		{
			PublicIncludePaths.Add(Path.Combine( OculusThirdPartyDirectory, "include" ));
		}
	}
}
