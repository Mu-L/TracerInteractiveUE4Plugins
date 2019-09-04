// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;
using System.IO;

public class libWebSockets : ModuleRules
{
	public libWebSockets(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		string WebsocketPath = Path.Combine(Target.UEThirdPartySourceDirectory, "libWebSockets", "libwebsockets");
		string PlatformSubdir = Target.Platform.ToString();

        bool bUseDebugBuild = (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT);
        string ConfigurationSubdir = bUseDebugBuild ? "Debug" : "Release";
		if (Target.Platform == UnrealTargetPlatform.HTML5)
		{
			return;
		}
		else if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Win32)
		{
			PlatformSubdir = Path.Combine(PlatformSubdir, "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
			PublicAdditionalLibraries.Add("websockets_static.lib");
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac || Target.Platform == UnrealTargetPlatform.IOS ||
			Target.Platform == UnrealTargetPlatform.PS4 || Target.Platform == UnrealTargetPlatform.Switch)
		{
			PublicAdditionalLibraries.Add(Path.Combine(WebsocketPath, "lib", Target.Platform.ToString(), ConfigurationSubdir, "libwebsockets.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Android)
		{
			PublicIncludePaths.Add(Path.Combine(WebsocketPath, "include", PlatformSubdir, "ARMv7"));
			PublicLibraryPaths.Add(Path.Combine(WebsocketPath, "lib", Target.Platform.ToString(), "ARMv7", ConfigurationSubdir));
			PublicIncludePaths.Add(Path.Combine(WebsocketPath, "include", PlatformSubdir, "ARM64"));
			PublicLibraryPaths.Add(Path.Combine(WebsocketPath, "lib", Target.Platform.ToString(), "ARM64", ConfigurationSubdir));
			PublicIncludePaths.Add(Path.Combine(WebsocketPath, "include", PlatformSubdir, "x86"));
			PublicLibraryPaths.Add(Path.Combine(WebsocketPath, "lib", Target.Platform.ToString(), "x86", ConfigurationSubdir));
			PublicIncludePaths.Add(Path.Combine(WebsocketPath, "include", PlatformSubdir, "x64"));
			PublicLibraryPaths.Add(Path.Combine(WebsocketPath, "lib", Target.Platform.ToString(), "x64", ConfigurationSubdir));
			PublicAdditionalLibraries.Add("websockets");
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			PlatformSubdir = "Linux/" + Target.Architecture;
			PublicAdditionalLibraries.Add(Path.Combine(WebsocketPath, "lib", PlatformSubdir, ConfigurationSubdir, "libwebsockets.a"));
		}
		else
		{ 
			// unsupported
			return;
		}

		if(Target.Platform != UnrealTargetPlatform.Android)
		{
			PublicLibraryPaths.Add(Path.Combine(WebsocketPath, "lib", PlatformSubdir, ConfigurationSubdir));
		}
		PublicIncludePaths.Add(Path.Combine(WebsocketPath, "include", PlatformSubdir));

		if (Target.Platform != UnrealTargetPlatform.Switch)
		{
			PublicDependencyModuleNames.Add("OpenSSL");
		}
	}
}
