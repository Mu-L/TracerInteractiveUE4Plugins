// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

[SupportedPlatforms("Win32", "Win64")]
public class GameplayMediaEncoder : ModuleRules
{
	public GameplayMediaEncoder(ReadOnlyTargetRules Target) : base(Target)
	{
		// NOTE: General rule is not to access the private folder of another module,
		// but to use the ISubmixBufferListener interface, we  need to include some private headers
		PrivateIncludePaths.Add(System.IO.Path.Combine(Directory.GetCurrentDirectory(), "./Runtime/AudioMixer/Private"));

		PrivateDependencyModuleNames.AddRange(new string[]
		{
            "Core",
            "Engine",
			"CoreUObject",
            "ApplicationCore",
			"RenderCore",
			"RHI",
			"SlateCore",
			"Slate",
			"HTTP",
			"Json",
			"AVEncoder",
			//"IBMRTMPIngest"
        });

		if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
				{
					"D3D11RHI"
				});

			PublicDelayLoadDLLs.Add("mfplat.dll");
			PublicDelayLoadDLLs.Add("mfuuid.dll");
			PublicDelayLoadDLLs.Add("Mfreadwrite.dll");

			PublicSystemLibraries.Add("d3d11.lib");
		}

	}
}

