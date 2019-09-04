// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

public class DX11 : ModuleRules
{
	public DX11(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string DirectXSDKDir = "";
		if (Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			DirectXSDKDir = Target.WindowsPlatform.bUseWindowsSDK10 ?
			Target.UEThirdPartySourceDirectory + "Windows/DirectXLegacy" :
			Target.UEThirdPartySourceDirectory + "Windows/DirectX";
		}
		else
		{
			DirectXSDKDir = Target.UEThirdPartySourceDirectory + "Windows/DirectX";
		}
		PublicSystemIncludePaths.Add(DirectXSDKDir + "/Include");

		if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Win32)
		{
			PublicDefinitions.Add("WITH_D3DX_LIBS=1");

			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				PublicLibraryPaths.Add(DirectXSDKDir + "/Lib/x64");
			}
			else if (Target.Platform == UnrealTargetPlatform.Win32)
			{
				PublicLibraryPaths.Add(DirectXSDKDir + "/Lib/x86");
			}

			PublicAdditionalLibraries.AddRange(
				new string[] {
				"dxgi.lib",
				"d3d9.lib",
				"d3d11.lib",
				"dxguid.lib",
				"d3dcompiler.lib",
				(Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT) ? "d3dx11d.lib" : "d3dx11.lib",
				"dinput8.lib",
				"X3DAudio.lib",
				"xapobase.lib",
				"XAPOFX.lib"
				}
				);
		}
		else if (Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			PublicDefinitions.Add("WITH_D3DX_LIBS=0");
		}

		else if (Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			PublicDefinitions.Add("WITH_D3DX_LIBS=0");
			PublicAdditionalLibraries.AddRange(
				new string[] {
				"dxguid.lib",
				}
				);
		}

	}
}

