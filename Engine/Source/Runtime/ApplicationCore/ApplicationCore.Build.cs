// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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

		if ((Target.Platform == UnrealTargetPlatform.Win64) ||
			(Target.Platform == UnrealTargetPlatform.Win32))
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, 
				"XInput"
				);
            if (Target.bCompileWithAccessibilitySupport && !Target.bIsBuildingConsoleApplication)
            {
                PublicAdditionalLibraries.Add("uiautomationcore.lib");
            }
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, 
				"OpenGL"
				);
			if (Target.bBuildEditor == true)
			{
				PublicAdditionalLibraries.Add("/System/Library/PrivateFrameworks/MultitouchSupport.framework/Versions/Current/MultitouchSupport");
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
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
		else if (Target.Platform == UnrealTargetPlatform.HTML5)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "SDL2");
			PrivateDependencyModuleNames.Add("HTML5JS");
			PrivateDependencyModuleNames.Add("MapPakDownloader");
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS || Target.Platform == UnrealTargetPlatform.TVOS)
		{
			PublicIncludePaths.AddRange(new string[] {"Runtime/ApplicationCore/Public/IOS"});
			AddEngineThirdPartyPrivateStaticDependencies(Target, "SoundSwitch");

			// export ApplicationCore symbols for embedded Dlls
			ModuleSymbolVisibility = ModuleRules.SymbolVisibility.VisibileForDll;
		}
        else if (Target.Platform == UnrealTargetPlatform.Android || Target.Platform == UnrealTargetPlatform.Lumin)
        {
            PrivateDependencyModuleNames.AddRange(
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
