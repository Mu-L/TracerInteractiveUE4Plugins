// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class libstrophe : ModuleRules
{
	protected virtual string StropheVersion { get { return "libstrophe-0.9.1"; } }

	protected virtual string LibRootDirectory { get { return ModuleDirectory; } }

	protected virtual string StrophePackagePath { get { return Path.Combine(LibRootDirectory, StropheVersion); } }

	protected virtual string ConfigName { get { return (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT) ? "Debug" : "Release"; } }

	protected virtual bool bRequireExpat { get { return true; } }

	public libstrophe(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PrivateDefinitions.Add("XML_STATIC");
		PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, StropheVersion));

		if (bRequireExpat)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "Expat");
		}

		if (Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			// Use reflection to allow type not to exist if console code is not present
			string ToolchainName = "VS";
			System.Type XboxOnePlatformType = System.Type.GetType("UnrealBuildTool.XboxOnePlatform,UnrealBuildTool");
			if (XboxOnePlatformType != null)
			{
				System.Object VersionName = XboxOnePlatformType.GetMethod("GetVisualStudioCompilerVersionName").Invoke(null, null);
				ToolchainName += VersionName.ToString();
			}

			string LibraryPath = Path.Combine(StrophePackagePath, "XboxOne", ToolchainName, ConfigName);
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "strophe.lib"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Android)
		{
			PublicAdditionalLibraries.Add(Path.Combine(StrophePackagePath, "Android", ConfigName, "arm64", "libstrophe.a"));
			PublicAdditionalLibraries.Add(Path.Combine(StrophePackagePath, "Android", ConfigName, "armv7", "libstrophe.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS || Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicAdditionalLibraries.Add(Path.Combine(StrophePackagePath, Target.Platform.ToString(), ConfigName, "libstrophe.a"));
			PublicSystemLibraries.Add("resolv");
		}
		else if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
		{
			string LibrayPath = Path.Combine(StrophePackagePath, Target.Platform.ToString(), "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), ConfigName) + "/";
			PublicAdditionalLibraries.Add(LibrayPath + "strophe.lib");
		}
		else if (Target.Platform == UnrealTargetPlatform.PS4)
		{
			string LibrayPath = Path.Combine(StrophePackagePath, Target.Platform.ToString(), ConfigName) + "/";
			PublicSystemLibraries.Add(LibrayPath + "libstrophe.a");
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			PublicAdditionalLibraries.Add(Path.Combine(StrophePackagePath, "Linux", Target.Architecture.ToString(), ConfigName, "libstrophe" + ((Target.LinkType != TargetLinkType.Monolithic) ? "_fPIC" : "") + ".a"));
			PublicSystemLibraries.Add("resolv");
		}
	}
}