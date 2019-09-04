// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class libOpus : ModuleRules
{
	public libOpus(ReadOnlyTargetRules Target) : base(Target)
	{
		/** Mark the current version of the library */
		string OpusVersion = "1.1";
		Type = ModuleType.External;

		PublicIncludePaths.Add(Target.UEThirdPartySourceDirectory + "libOpus/opus-" + OpusVersion + "/include");
		string LibraryPath = Target.UEThirdPartySourceDirectory + "libOpus/opus-" + OpusVersion + "/";

		if ((Target.Platform == UnrealTargetPlatform.Win64) ||
			(Target.Platform == UnrealTargetPlatform.Win32))
		{
			LibraryPath += "Windows/VS2012/";
			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				LibraryPath += "x64/";
			}
			else
			{
				LibraryPath += "win32/";
			}

			LibraryPath += "Release/";

			PublicLibraryPaths.Add(LibraryPath);

 			PublicAdditionalLibraries.Add("silk_common.lib");
 			PublicAdditionalLibraries.Add("silk_float.lib");
 			PublicAdditionalLibraries.Add("celt.lib");
			PublicAdditionalLibraries.Add("opus.lib");
			PublicAdditionalLibraries.Add("speex_resampler.lib");
		}
		else if (Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			if (Target.WindowsPlatform.Architecture == WindowsArchitecture.x64)
			{
				LibraryPath += "Windows/VS2012/";
				LibraryPath += "x64/";
			}
			else if (Target.WindowsPlatform.Architecture == WindowsArchitecture.x86)
			{
				LibraryPath += "Windows/VS2012/";
				LibraryPath += "win32/";
			}
			else if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM32)
			{
				LibraryPath += "Windows/VS" + (Target.WindowsPlatform.Compiler >= WindowsCompiler.VisualStudio2015_DEPRECATED ? "2015" : "2012");
				LibraryPath += "/ARM/";
			}
			else if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64)
			{
				LibraryPath += "Windows/VS" + (Target.WindowsPlatform.Compiler >= WindowsCompiler.VisualStudio2015_DEPRECATED ? "2015" : "2012");
				LibraryPath += "/ARM64/";
			}
			

			LibraryPath += "Release/";

			PublicLibraryPaths.Add(LibraryPath);

 			PublicAdditionalLibraries.Add("silk_common.lib");
 			PublicAdditionalLibraries.Add("silk_float.lib");
 			PublicAdditionalLibraries.Add("celt.lib");
			PublicAdditionalLibraries.Add("opus.lib");
			PublicAdditionalLibraries.Add("speex_resampler.lib");
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string OpusPath = LibraryPath + "/Mac/libopus.a";
			string SpeexPath = LibraryPath + "/Mac/libspeex_resampler.a";

			PublicAdditionalLibraries.Add(OpusPath);
			PublicAdditionalLibraries.Add(SpeexPath);
		}
        else if (Target.Platform == UnrealTargetPlatform.IOS)
        {
            string OpusPath = LibraryPath + "/IOS/libOpus.a";
            PublicAdditionalLibraries.Add(OpusPath);
        }
	else if (Target.Platform == UnrealTargetPlatform.TVOS)
        {
            string OpusPath = LibraryPath + "/TVOS/libOpus.a";
            PublicAdditionalLibraries.Add(OpusPath);
        }
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
            if (Target.LinkType == TargetLinkType.Monolithic)
            {
                PublicAdditionalLibraries.Add(LibraryPath + "Linux/" + Target.Architecture + "/libopus.a");
            }
            else
            {
                PublicAdditionalLibraries.Add(LibraryPath + "Linux/" + Target.Architecture + "/libopus_fPIC.a");
            }

			if (Target.Architecture.StartsWith("x86_64"))
			{
				if (Target.LinkType == TargetLinkType.Monolithic)
				{
					PublicAdditionalLibraries.Add(LibraryPath + "Linux/" + Target.Architecture + "/libresampler.a");
				}
				else
				{
					PublicAdditionalLibraries.Add(LibraryPath + "Linux/" + Target.Architecture + "/libresampler_fPIC.a");
				}
			}
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
			PublicLibraryPaths.Add(LibraryPath + "Android/ARMv7/");
			PublicLibraryPaths.Add(LibraryPath + "Android/ARM64/");
			PublicLibraryPaths.Add(LibraryPath + "Android/x64/");
			
			PublicAdditionalLibraries.Add("opus");
			PublicAdditionalLibraries.Add("speex_resampler");
		}
        else if (Target.Platform == UnrealTargetPlatform.XboxOne)
        {
            LibraryPath += "XboxOne/VS2015/Release/";

            PublicLibraryPaths.Add(LibraryPath);

            PublicAdditionalLibraries.Add("silk_common.lib");
            PublicAdditionalLibraries.Add("silk_float.lib");
            PublicAdditionalLibraries.Add("celt.lib");
            PublicAdditionalLibraries.Add("opus.lib");
            PublicAdditionalLibraries.Add("speex_resampler.lib");
        }
		else if (Target.Platform == UnrealTargetPlatform.PS4)
        {
            PublicAdditionalLibraries.Add(LibraryPath + "PS4/ORBIS_Release/" + "OpusLibrary.a");
        }
        else if (Target.Platform == UnrealTargetPlatform.Switch)
        {
            PublicAdditionalLibraries.Add(LibraryPath +  "Switch/libOpus-1.1/NX64/Release/" + "libOpus-1.1.a");
        }
    }
}
