// Copyright Epic Games, Inc. All Rights Reserved.
using System;
using System.IO;
using UnrealBuildTool;

public class OpenCV : ModuleRules
{
	public OpenCV(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.Platform == UnrealTargetPlatform.Win64 ||
			Target.Platform == UnrealTargetPlatform.Win32)
		{
			string PlatformDir = Target.Platform.ToString();
			string IncPath = Path.Combine(ModuleDirectory, "include");
			PublicSystemIncludePaths.Add(IncPath);

			string LibPath = Path.Combine(ModuleDirectory, "lib", PlatformDir);
            string BinaryPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../../Binaries/ThirdParty", PlatformDir));

			string LibName = "opencv_world331";

			if (Target.Configuration == UnrealTargetConfiguration.Debug &&
				Target.bDebugBuildsActuallyUseDebugCRT)
			{
					LibName += "d";
			}

			PublicAdditionalLibraries.Add(Path.Combine(LibPath, LibName + ".lib"));
			string DLLName = LibName + ".dll";
			PublicDelayLoadDLLs.Add(DLLName);
			RuntimeDependencies.Add(Path.Combine(BinaryPath, DLLName));
			PublicDefinitions.Add("WITH_OPENCV=1");
			PublicDefinitions.Add("OPENCV_PLATFORM_PATH=Binaries/ThirdParty/" + PlatformDir);
			PublicDefinitions.Add("OPENCV_DLL_NAME=" + DLLName);
		}
		else // unsupported platform
		{
            PublicDefinitions.Add("WITH_OPENCV=0");
		}
	}
}
