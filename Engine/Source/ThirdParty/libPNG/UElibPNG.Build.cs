// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UElibPNG : ModuleRules
{
	public UElibPNG(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string libPNGPath = Target.UEThirdPartySourceDirectory + "libPNG/libPNG-1.5.2";

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string LibPath = libPNGPath + "/lib/Win64/VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName();
			PublicLibraryPaths.Add(LibPath);

			string LibFileName = "libpng" + (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT ? "d" : "") + "_64.lib";
			PublicAdditionalLibraries.Add(LibFileName);
		}
		else if (Target.Platform == UnrealTargetPlatform.Win32)
		{
			libPNGPath = libPNGPath + "/lib/Win32/VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName();
			PublicLibraryPaths.Add(libPNGPath);

			string LibFileName = "libpng" + (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT ? "d" : "") + ".lib";
			PublicAdditionalLibraries.Add(LibFileName);
		}
		else if (Target.Platform == UnrealTargetPlatform.HoloLens)
        {
            string PlatformSubpath = Target.Platform.ToString();
            if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64)
            {
                PublicLibraryPaths.Add(System.String.Format("{0}/lib/{1}/VS{2}/{3}/", libPNGPath, PlatformSubpath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), Target.WindowsPlatform.GetArchitectureSubpath()));
            }
            else
            {
                PublicLibraryPaths.Add(System.String.Format("{0}/lib/{1}/VS{2}/", libPNGPath, PlatformSubpath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName()));
            }

            string LibFileName = "libpng";
			if(Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
            {
                LibFileName += "d";
            }
            if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64 || Target.WindowsPlatform.Architecture == WindowsArchitecture.x64)
            {
                LibFileName += "_64";
            }
            PublicAdditionalLibraries.Add(LibFileName + ".lib");
        }
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicAdditionalLibraries.Add(libPNGPath + "/lib/Mac/libpng.a");
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			if (Target.Architecture == "-simulator")
			{
				PublicLibraryPaths.Add(libPNGPath + "/lib/ios/Simulator");
			}
			else
			{
				PublicLibraryPaths.Add(libPNGPath + "/lib/ios/Device");
			}

			PublicAdditionalLibraries.Add("png152");
		}
		else if (Target.Platform == UnrealTargetPlatform.TVOS)
		{
			if (Target.Architecture == "-simulator")
			{
				PublicLibraryPaths.Add(libPNGPath + "/lib/TVOS/Simulator");
			}
			else
			{
				PublicLibraryPaths.Add(libPNGPath + "/lib/TVOS/Device");
			}

			PublicAdditionalLibraries.Add("png152");
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
			libPNGPath = Target.UEThirdPartySourceDirectory + "libPNG/libPNG-1.5.27";

			PublicLibraryPaths.Add(libPNGPath + "/lib/Android/ARMv7");
			PublicLibraryPaths.Add(libPNGPath + "/lib/Android/ARM64");
			PublicLibraryPaths.Add(libPNGPath + "/lib/Android/x86");
			PublicLibraryPaths.Add(libPNGPath + "/lib/Android/x64");

			PublicAdditionalLibraries.Add("png");
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			// migrate all architectures to the newer binary
			if (Target.Architecture.StartsWith("aarch64") || Target.Architecture.StartsWith("i686"))
			{
				libPNGPath = Target.UEThirdPartySourceDirectory + "libPNG/libPNG-1.5.27";
			}

			PublicAdditionalLibraries.Add(libPNGPath + "/lib/Linux/" + Target.Architecture + "/libpng.a");
		}
		else if (Target.Platform == UnrealTargetPlatform.HTML5)
		{
			PublicLibraryPaths.Add(libPNGPath + "/lib/HTML5");
			string OpimizationSuffix = "";
			if (Target.bCompileForSize)
			{
				OpimizationSuffix = "_Oz";
			}
			else
			{
				if (Target.Configuration == UnrealTargetConfiguration.Development)
				{
					OpimizationSuffix = "_O2";
				}
				else if (Target.Configuration == UnrealTargetConfiguration.Shipping)
				{
					OpimizationSuffix = "_O3";
				}
			}
			PublicAdditionalLibraries.Add(libPNGPath + "/lib/HTML5/libpng" + OpimizationSuffix + ".bc");
		}
		else if (Target.Platform == UnrealTargetPlatform.PS4)
		{
			PublicLibraryPaths.Add(libPNGPath + "/lib/PS4");
			PublicAdditionalLibraries.Add("png152");
		}
		else if (Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			// Use reflection to allow type not to exist if console code is not present
			System.Type XboxOnePlatformType = System.Type.GetType("UnrealBuildTool.XboxOnePlatform,UnrealBuildTool");
			if (XboxOnePlatformType != null)
			{
				System.Object VersionName = XboxOnePlatformType.GetMethod("GetVisualStudioCompilerVersionName").Invoke(null, null);
				PublicLibraryPaths.Add(libPNGPath + "/lib/XboxOne/VS" + VersionName.ToString());
				PublicAdditionalLibraries.Add("libpng125_XboxOne.lib");
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Switch)
		{
			PublicAdditionalLibraries.Add(System.IO.Path.Combine(libPNGPath, "lib/Switch/libPNG.a"));
		}

		PublicIncludePaths.Add(libPNGPath);
	}
}
