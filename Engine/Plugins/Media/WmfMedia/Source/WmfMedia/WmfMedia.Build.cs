// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class WmfMedia : ModuleRules
	{
		public WmfMedia(ReadOnlyTargetRules Target) : base(Target)
		{
            bEnableExceptions = true;

            DynamicallyLoadedModuleNames.AddRange(
				new string[] {
					"Media",
				});

            PublicDependencyModuleNames.AddRange(
                new string[] {
                    "WmfMediaFactory"
                    });

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
                    "Engine",
                    "MediaUtils",
					"Projects",
					"RenderCore",
                    "RHI",
                    "WmfMediaFactory"
                });

            if (Target.Platform == UnrealTargetPlatform.XboxOne)
            {
                PrivateDependencyModuleNames.AddRange(
				  new string[] {
					"D3D12RHI",
                });
                PrivateIncludePaths.AddRange(
                    new string[] {
                    "../../../../Platforms/XboxCommon/Source/Runtime/D3D12RHI/Private",
					"../../../../Source/Runtime/D3D12RHI/Private",
				});
            }
            else
            {
                PrivateDependencyModuleNames.AddRange(
                  new string[] {
                    "D3D11RHI",
                });
            }

            PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"Media",
				});

			PrivateIncludePaths.AddRange(
				new string[] {
					"WmfMedia/Private",
					"WmfMedia/Private/Player",
					"WmfMedia/Private/Wmf",
                    "../../../../Source/Runtime/Windows/D3D11RHI/Private",
                    "../../../../Source/Runtime/Windows/D3D11RHI/Private/Windows",
                });

            AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelMetricsDiscovery");
            AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelExtensionsFramework");
            AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");

			if (Target.bCompileAgainstEngine)
			{
				PrivateDependencyModuleNames.Add("Engine");
				PrivateDependencyModuleNames.Add("HeadMountedDisplay");
			}

			if ((Target.Platform == UnrealTargetPlatform.Win64) ||
				(Target.Platform == UnrealTargetPlatform.Win32))
			{
				PublicDelayLoadDLLs.Add("mf.dll");
				PublicDelayLoadDLLs.Add("mfplat.dll");
				PublicDelayLoadDLLs.Add("mfplay.dll");
				PublicDelayLoadDLLs.Add("shlwapi.dll");
			}
		}
	}
}
