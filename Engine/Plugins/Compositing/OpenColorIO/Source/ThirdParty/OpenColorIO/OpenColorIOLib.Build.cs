// Copyright Epic Games, Inc. All Rights Reserved.
using System;
using System.IO;
using UnrealBuildTool;

public class OpenColorIOLib : ModuleRules
{
	public OpenColorIOLib(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		bool bIsPlatformAdded = false;
		if(Target.bBuildEditor == true)
		{
			if (Target.Platform == UnrealTargetPlatform.Win64 ||
						Target.Platform == UnrealTargetPlatform.Win32)
			{
				string PlatformDir = Target.Platform.ToString();
				string IncPath = Path.Combine(ModuleDirectory, "distribution", "include");
				PublicSystemIncludePaths.Add(IncPath);

				string LibPath = Path.Combine(ModuleDirectory, "distribution", "lib", PlatformDir);
				string BinaryPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../../Binaries/ThirdParty", PlatformDir));

				string LibName = "OpenColorIO";

				PublicAdditionalLibraries.Add(Path.Combine(LibPath, LibName + ".lib"));
				string DLLName = LibName + ".dll";
				PublicDelayLoadDLLs.Add(DLLName);
				RuntimeDependencies.Add(Path.Combine(BinaryPath, DLLName));
				PublicDefinitions.Add("WITH_OCIO=1");
				PublicDefinitions.Add("OCIO_PLATFORM_PATH=Binaries/ThirdParty/" + PlatformDir);
				PublicDefinitions.Add("OCIO_DLL_NAME=" + DLLName);

				bIsPlatformAdded = true;
			}
		}
		
		if(bIsPlatformAdded == false)
		{
			PublicDefinitions.Add("WITH_OCIO=0");
		}
	}
}
