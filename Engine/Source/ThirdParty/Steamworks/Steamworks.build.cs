// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;

public class Steamworks : ModuleRules
{
	public Steamworks(ReadOnlyTargetRules Target) : base(Target)
	{
		/** Mark the current version of the Steam SDK */
		string SteamVersion = "v139";
		Type = ModuleType.External;

		PublicDefinitions.Add("STEAM_SDK_VER=TEXT(\"1.39\")");
		PublicDefinitions.Add("STEAM_SDK_VER_PATH=TEXT(\"Steam" + SteamVersion + "\")");

		string SdkBase = Target.UEThirdPartySourceDirectory + "Steamworks/Steam" + SteamVersion + "/sdk";
		if (!Directory.Exists(SdkBase))
		{
			string Err = string.Format("steamworks SDK not found in {0}", SdkBase);
			System.Console.WriteLine(Err);
			throw new BuildException(Err);
		}

		PublicIncludePaths.Add(SdkBase + "/public");

		string LibraryPath = SdkBase + "/redistributable_bin/";
		string LibraryName = "steam_api";

		if(Target.Platform == UnrealTargetPlatform.Win32)
		{
			PublicLibraryPaths.Add(LibraryPath);
			PublicAdditionalLibraries.Add(LibraryName + ".lib");
			PublicDelayLoadDLLs.Add(LibraryName + ".dll");

			string SteamBinariesDir = String.Format("$(EngineDir)/Binaries/ThirdParty/Steamworks/Steam{0}/Win32/", SteamVersion);
			RuntimeDependencies.Add(SteamBinariesDir + "steam_api.dll");

			if(Target.Type == TargetType.Server)
			{
				RuntimeDependencies.Add(SteamBinariesDir + "steamclient.dll");
				RuntimeDependencies.Add(SteamBinariesDir + "tier0_s.dll");
				RuntimeDependencies.Add(SteamBinariesDir + "vstdlib_s.dll");
			}
			else
			{
				// assume SteamController is needed
				RuntimeDependencies.Add("$(EngineDir)/Config/controller.vdf");
			}
		}
		else if(Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicLibraryPaths.Add(LibraryPath + "win64");
			PublicAdditionalLibraries.Add(LibraryName + "64.lib");
			PublicDelayLoadDLLs.Add(LibraryName + "64.dll");

			string SteamBinariesDir = String.Format("$(EngineDir)/Binaries/ThirdParty/Steamworks/Steam{0}/Win64/", SteamVersion);
			RuntimeDependencies.Add(SteamBinariesDir + LibraryName + "64.dll");

			if(Target.Type == TargetType.Server)
			{
				RuntimeDependencies.Add(SteamBinariesDir + "steamclient64.dll");
				RuntimeDependencies.Add(SteamBinariesDir + "tier0_s64.dll");
				RuntimeDependencies.Add(SteamBinariesDir + "vstdlib_s64.dll");
			}
			else
			{
				// assume SteamController is needed
				RuntimeDependencies.Add("$(EngineDir)/Config/controller.vdf");
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			LibraryPath += "osx32/libsteam_api.dylib";
			PublicDelayLoadDLLs.Add(LibraryPath);
			PublicAdditionalShadowFiles.Add(LibraryPath);
			AdditionalBundleResources.Add(new UEBuildBundleResource(LibraryPath, "MacOS"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			if (Target.LinkType == TargetLinkType.Monolithic)
			{
				LibraryPath += "linux64";
				PublicLibraryPaths.Add(LibraryPath);
				PublicAdditionalLibraries.Add(LibraryName);
			}
			else
			{
				LibraryPath += "linux64/libsteam_api.so";
				PublicDelayLoadDLLs.Add(LibraryPath);
			}
			string SteamBinariesPath = String.Format(Target.UEThirdPartyBinariesDirectory + "Steamworks/Steam{0}/{1}/{2}", SteamVersion, Target.Architecture, "libsteam_api.so");
			PublicAdditionalLibraries.Add(SteamBinariesPath);
			RuntimeDependencies.Add(SteamBinariesPath);
		}
	}
}
