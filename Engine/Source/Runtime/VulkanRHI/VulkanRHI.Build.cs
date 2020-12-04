// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;

public class VulkanRHI : ModuleRules
{
	public VulkanRHI(ReadOnlyTargetRules Target) : base(Target)
	{
		bLegalToDistributeObjectCode = true;

		PrivateIncludePaths.Add("Runtime/VulkanRHI/Private");
		if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateIncludePaths.Add("Runtime/VulkanRHI/Private/Windows");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "AMD_AGS");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			if (Target.IsInPlatformGroup(UnrealPlatformGroup.Linux))
			{
				PrivateIncludePaths.Add("Runtime/VulkanRHI/Private/Linux");
			}
		}
		else
		{
			PrivateIncludePaths.Add("Runtime/VulkanRHI/Private/" + Target.Platform);
		}

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core", 
				"CoreUObject",
				"ApplicationCore",
				"Engine", 
				"RHI", 
				"RenderCore", 
				"HeadMountedDisplay",
                "PreLoadScreen",
				"BuildSettings"
            }
        );

		if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64
			|| Target.Platform == UnrealTargetPlatform.Android || Target.Platform == UnrealTargetPlatform.Lumin)
		{
            AddEngineThirdPartyPrivateStaticDependencies(Target, "Vulkan");
        }
        else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			if (Target.IsInPlatformGroup(UnrealPlatformGroup.Linux))
			{
				PrivateDependencyModuleNames.Add("ApplicationCore");
				AddEngineThirdPartyPrivateStaticDependencies(Target, "SDL2");

				string VulkanSDKPath = Environment.GetEnvironmentVariable("VULKAN_SDK");
				bool bSDKInstalled = !String.IsNullOrEmpty(VulkanSDKPath);
				if (BuildHostPlatform.Current.Platform != UnrealTargetPlatform.Linux || !bSDKInstalled)
				{
					AddEngineThirdPartyPrivateStaticDependencies(Target, "Vulkan");
				}
				else
				{
					PrivateIncludePaths.Add(VulkanSDKPath + "/include");
					PrivateIncludePaths.Add(VulkanSDKPath + "/include/vulkan");
					PublicAdditionalLibraries.Add(Path.Combine(VulkanSDKPath, "lib", "vulkan"));
				}
			}
			else
			{
				AddEngineThirdPartyPrivateStaticDependencies(Target, "VkHeadersExternal");
			}
		}
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
			string VulkanSDKPath = Environment.GetEnvironmentVariable("VULKAN_SDK");

			bool bHaveVulkan = false;
			if (!String.IsNullOrEmpty(VulkanSDKPath))
			{
				bHaveVulkan = true;
				PrivateIncludePaths.Add(VulkanSDKPath + "/Include");
			}

			if (bHaveVulkan)
			{
				if (Target.Configuration != UnrealTargetConfiguration.Shipping)
				{
					PrivateIncludePathModuleNames.AddRange(
						new string[]
						{
							"TaskGraph",
						}
					);
				}
			}
			else
			{
				PrecompileForTargets = PrecompileTargetsType.None;
			}
		}
		else
		{
			PrecompileForTargets = PrecompileTargetsType.None;
		}
	}
}

