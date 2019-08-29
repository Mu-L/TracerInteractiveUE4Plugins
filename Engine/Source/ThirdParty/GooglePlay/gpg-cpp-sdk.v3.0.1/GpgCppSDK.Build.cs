// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class GpgCppSDK : ModuleRules
{
	public GpgCppSDK(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			string GPGAndroidPath = Path.Combine(Target.UEThirdPartySourceDirectory, "GooglePlay/gpg-cpp-sdk.v3.0.1/gpg-cpp-sdk/android/");

			PublicIncludePaths.Add(Path.Combine(GPGAndroidPath, "include/"));
			PublicLibraryPaths.Add(Path.Combine(GPGAndroidPath, "lib/gnustl/armeabi-v7a/"));
			PublicLibraryPaths.Add(Path.Combine(GPGAndroidPath, "lib/gnustl/arm64-v8a"));
			PublicLibraryPaths.Add(Path.Combine(GPGAndroidPath, "lib/gnustl/x86/"));

			PublicAdditionalLibraries.Add("gpg");
		}
	}
}
