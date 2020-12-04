// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ApplicationCore : ModuleRules
{
	public ApplicationCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core"
			}
		);

		PublicIncludePathModuleNames.AddRange(
			new string[] {
				"RHI"
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"InputDevice",
				"Analytics",
				"SynthBenchmark"
			}
		);

		if ((Target.IsInPlatformGroup(UnrealPlatformGroup.Windows)))
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target,
				"XInput"
			);
			if (Target.bCompileWithAccessibilitySupport && !Target.bIsBuildingConsoleApplication)
			{
				PublicSystemLibraries.Add("uiautomationcore.lib");
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target,
				"OpenGL"
			);
			if (Target.bBuildEditor == true)
			{
				string SDKROOT = Utils.RunLocalProcessAndReturnStdOut("/usr/bin/xcrun", "--sdk macosx --show-sdk-path");
				PublicAdditionalLibraries.Add(SDKROOT + "/System/Library/PrivateFrameworks/MultitouchSupport.framework/Versions/Current/MultitouchSupport.tbd");
			}
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Linux))
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target,
				"SDL2"
			);

			// We need FreeType2 and GL for the Splash, but only in the Editor
			if (Target.Type == TargetType.Editor)
			{
				AddEngineThirdPartyPrivateStaticDependencies(Target, "FreeType2");
				AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenGL");
				PrivateIncludePathModuleNames.Add("ImageWrapper");
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS || Target.Platform == UnrealTargetPlatform.TVOS)
		{
			PublicIncludePaths.AddRange(new string[] {"Runtime/ApplicationCore/Public/IOS"});
			AddEngineThirdPartyPrivateStaticDependencies(Target, "SoundSwitch");

			// export ApplicationCore symbols for embedded Dlls
			ModuleSymbolVisibility = ModuleRules.SymbolVisibility.VisibileForDll;
			
			//Need to add this as BackgroundHTTP files can end up doing work directly from our AppDelegate in iOS and thus we need acccess to correct file locations to save these very early.
			PrivateDependencyModuleNames.Add("BackgroundHTTPFileHash");
		}
		else if (Target.Platform == UnrealTargetPlatform.Android || Target.Platform == UnrealTargetPlatform.Lumin)
		{
			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"Launch"
				}
			);
		}

		if (!Target.bCompileAgainstApplicationCore)
		{
			throw new System.Exception("ApplicationCore cannot be used when Target.bCompileAgainstApplicationCore = false.");
		}
	}
}
