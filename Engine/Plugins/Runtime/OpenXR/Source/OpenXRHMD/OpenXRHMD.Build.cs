// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class OpenXRHMD : ModuleRules
	{
		public OpenXRHMD(ReadOnlyTargetRules Target) : base(Target)
        {
            var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
            PrivateIncludePaths.AddRange(
				new string[] {
					"OpenXRHMD/Private",
                    EngineDir + "/Source/ThirdParty/OpenXR/include",
                    EngineDir + "/Source/Runtime/Renderer/Private",
                    EngineDir + "/Source/Runtime/OpenGLDrv/Private",
                    EngineDir + "/Source/Runtime/VulkanRHI/Private",
					// ... add other private include paths required here ...
				}
				);

            if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
            {
                PrivateIncludePaths.Add(EngineDir + "/Source/Runtime/VulkanRHI/Private/Windows");
            }
            else if (Target.Platform != UnrealTargetPlatform.HoloLens)
            {
                PrivateIncludePaths.Add(EngineDir + "/Source/Runtime/VulkanRHI/Private/" + Target.Platform);
            }

            PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
                    "BuildSettings",
                    "InputCore",
					"RHI",
					"RenderCore",
					"Renderer",
					"RenderCore",
                    "HeadMountedDisplay",
                    "Slate",
                    "SlateCore",
					"AugmentedReality",
				}
				);

			if (Target.bBuildEditor == true)
            {
                PrivateDependencyModuleNames.Add("UnrealEd");
			}

            if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.HoloLens)
            {
                PrivateDependencyModuleNames.AddRange(new string[] {
					"D3D11RHI",
					"D3D12RHI"
				});

                // Required for some private headers needed for the rendering support.
                PrivateIncludePaths.AddRange(
                    new string[] {
                            Path.Combine(EngineDir, @"Source\Runtime\Windows\D3D11RHI\Private"),
                            Path.Combine(EngineDir, @"Source\Runtime\Windows\D3D11RHI\Private\Windows"),
							Path.Combine(EngineDir, @"Source\Runtime\D3D12RHI\Private"),
							Path.Combine(EngineDir, @"Source\Runtime\D3D12RHI\Private\Windows")
								});

                AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11");
				AddEngineThirdPartyPrivateStaticDependencies(Target, "DX12");
				AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAPI");
                AddEngineThirdPartyPrivateStaticDependencies(Target, "AMD_AGS");
                AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");
				AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelMetricsDiscovery");
				AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelExtensionsFramework");
            }

            if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
            {
                PrivateDependencyModuleNames.AddRange(new string[] {
                    "OpenGLDrv",
                });

                AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenGL");
			}

            if (Target.Platform != UnrealTargetPlatform.HoloLens)
            {
                PrivateDependencyModuleNames.AddRange(new string[] {
                    "VulkanRHI"
                });

                AddEngineThirdPartyPrivateStaticDependencies(Target, "Vulkan");
            }

            PublicDependencyModuleNames.Add("HeadMountedDisplay");
			PublicIncludePathModuleNames.Add("OpenXR");
		}
	}
}
