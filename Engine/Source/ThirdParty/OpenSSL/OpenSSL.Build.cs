// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class OpenSSL : ModuleRules
{
	public OpenSSL(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string OpenSSL101sPath = Path.Combine(Target.UEThirdPartySourceDirectory, "OpenSSL", "1_0_1s");
		string OpenSSL111Path = Path.Combine(Target.UEThirdPartySourceDirectory, "OpenSSL", "1.1.1");
		string OpenSSL111cPath = Path.Combine(Target.UEThirdPartySourceDirectory, "OpenSSL", "1.1.1c");

		string PlatformSubdir = Target.Platform.ToString();
		string ConfigFolder = (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT) ? "Debug" : "Release";

		if (Target.Platform == UnrealTargetPlatform.Mac || Target.Platform == UnrealTargetPlatform.IOS)
		{
			PublicIncludePaths.Add(Path.Combine(OpenSSL111Path, "Include", PlatformSubdir));

			string LibPath = Path.Combine(OpenSSL111Path, "Lib", PlatformSubdir);

			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "libssl.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "libcrypto.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Win32 ||
				Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			// Our OpenSSL 1.1.1 libraries are built with zlib compression support
			PrivateDependencyModuleNames.Add("zlib");

			string VSVersion = "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName();

			// Add includes
			PublicIncludePaths.Add(Path.Combine(OpenSSL111Path, "include", PlatformSubdir, VSVersion));

			// Add Libs
			string LibPath = Path.Combine(OpenSSL111Path, "lib", PlatformSubdir, VSVersion, ConfigFolder);

			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "libssl.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "libcrypto.lib"));
			PublicSystemLibraries.Add("crypt32.lib");
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			string platform = "/Linux/" + Target.Architecture;
			string IncludePath = OpenSSL111cPath + "/include" + platform;
			string LibraryPath = OpenSSL111cPath + "/lib" + platform;

			PublicIncludePaths.Add(IncludePath);
			PublicAdditionalLibraries.Add(LibraryPath + "/libssl.a");
			PublicAdditionalLibraries.Add(LibraryPath + "/libcrypto.a");

			PublicDependencyModuleNames.Add("zlib");
//			PublicAdditionalLibraries.Add("z");
		}
		else if (Target.Platform == UnrealTargetPlatform.Android || Target.Platform == UnrealTargetPlatform.Lumin)
		{
			string IncludePath = OpenSSL101sPath + "/include/Android";
			PublicIncludePaths.Add(IncludePath);

			// unneeded since included in libcurl
			// string LibPath = Path.Combine(OpenSSL101sPath, "lib", PlatformSubdir);
			//PublicLibraryPaths.Add(LibPath);
		}
	}
}
