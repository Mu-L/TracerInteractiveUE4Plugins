// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class FreeType2 : ModuleRules
{
	public FreeType2(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicDefinitions.Add("WITH_FREETYPE=1");

		string FreeType2Path;
		string FreeType2LibPath;

		if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.XboxOne ||
			Target.Platform == UnrealTargetPlatform.Switch || Target.Platform == UnrealTargetPlatform.PS4 || Target.Platform == UnrealTargetPlatform.Linux ||
			Target.Platform == UnrealTargetPlatform.HTML5 || Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			FreeType2Path = Target.UEThirdPartySourceDirectory + "FreeType2/FreeType2-2.6/";
			PublicSystemIncludePaths.Add(FreeType2Path + "Include");
		}
		else
		{
			if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
			{
				FreeType2Path = Target.UEThirdPartySourceDirectory + "FreeType2/FreeType2-2.6/";
				PublicSystemIncludePaths.Add(FreeType2Path + "Include");
			}
			else
			{
				FreeType2Path = Target.UEThirdPartySourceDirectory + "FreeType2/FreeType2-2.4.12/";
				PublicSystemIncludePaths.Add(FreeType2Path + "include");
			}
		}
		
		FreeType2LibPath = FreeType2Path + "Lib/";

		if (Target.Platform == UnrealTargetPlatform.Win32 ||
			Target.Platform == UnrealTargetPlatform.Win64)
		{

			FreeType2LibPath += (Target.Platform == UnrealTargetPlatform.Win64) ? "Win64/" : "Win32/";
			FreeType2LibPath += "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName();

			PublicLibraryPaths.Add(FreeType2LibPath);
			PublicAdditionalLibraries.Add("freetype26MT.lib");
		}
		else if (Target.Platform == UnrealTargetPlatform.HoloLens)
		{

            string PlatformSubpath = Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.x86 ? "Win32" : "Win64";

            if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32 || Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64)
            {
                PublicLibraryPaths.Add(System.String.Format("{0}Lib/{1}/VS{2}/{3}/", FreeType2Path, PlatformSubpath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), Target.WindowsPlatform.GetArchitectureSubpath()));
            }
			else
            {
                PublicLibraryPaths.Add(System.String.Format("{0}Lib/{1}/VS{2}/", FreeType2Path, PlatformSubpath, Target.WindowsPlatform.GetVisualStudioCompilerVersionName()));
            }

            PublicAdditionalLibraries.Add("freetype26MT.lib");
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicAdditionalLibraries.Add(FreeType2LibPath + "Mac/libfreetype2412.a");
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			if (Target.Architecture == "-simulator")
			{
				PublicLibraryPaths.Add(FreeType2LibPath + "ios/Simulator");
			}
			else
			{
				PublicLibraryPaths.Add(FreeType2LibPath + "ios/Device");
			}

			PublicAdditionalLibraries.Add("freetype2412");
		}
		else if (Target.Platform == UnrealTargetPlatform.TVOS)
		{
			if (Target.Architecture == "-simulator")
			{
				PublicLibraryPaths.Add(FreeType2LibPath + "TVOS/Simulator");
			}
			else
			{
				PublicLibraryPaths.Add(FreeType2LibPath + "TVOS/Device");
			}

			PublicAdditionalLibraries.Add("freetype2412");
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
			// filtered out in the toolchain
			PublicLibraryPaths.Add(FreeType2LibPath + "Android/ARMv7");
			PublicLibraryPaths.Add(FreeType2LibPath + "Android/ARM64");
			PublicLibraryPaths.Add(FreeType2LibPath + "Android/x86");
			PublicLibraryPaths.Add(FreeType2LibPath + "Android/x64");

			PublicAdditionalLibraries.Add("freetype2412");
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			if (Target.Type == TargetType.Server)
			{
				string Err = string.Format("{0} dedicated server is made to depend on {1}. We want to avoid this, please correct module dependencies.", Target.Platform.ToString(), this.ToString());
				System.Console.WriteLine(Err);
				throw new BuildException(Err);
			}

			PublicSystemIncludePaths.Add(FreeType2Path + "Include");

			if (Target.LinkType == TargetLinkType.Monolithic)
			{
				PublicAdditionalLibraries.Add(FreeType2LibPath + "Linux/" + Target.Architecture + "/libfreetype.a");
			}
			else
			{
				PublicAdditionalLibraries.Add(FreeType2LibPath + "Linux/" + Target.Architecture + "/libfreetype_fPIC.a");
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.HTML5)
		{
			PublicLibraryPaths.Add(FreeType2Path + "Lib/HTML5");
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
			PublicAdditionalLibraries.Add(FreeType2Path + "Lib/HTML5/libfreetype260" + OpimizationSuffix + ".bc");
		}
		else if (Target.Platform == UnrealTargetPlatform.PS4)
		{
			PublicLibraryPaths.Add(FreeType2LibPath + "PS4");
			PublicAdditionalLibraries.Add("freetype26");
		}
		else if (Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			// Use reflection to allow type not to exist if console code is not present
			System.Type XboxOnePlatformType = System.Type.GetType("UnrealBuildTool.XboxOnePlatform,UnrealBuildTool");
			if (XboxOnePlatformType != null)
			{
				System.Object VersionName = XboxOnePlatformType.GetMethod("GetVisualStudioCompilerVersionName").Invoke(null, null);
				PublicLibraryPaths.Add(FreeType2LibPath + "XboxOne/VS" + VersionName.ToString());
				PublicAdditionalLibraries.Add("freetype26.lib");
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Switch)
		{
			PublicAdditionalLibraries.Add(System.IO.Path.Combine(FreeType2LibPath, "Switch/libFreetype.a"));
		}
	}
}
