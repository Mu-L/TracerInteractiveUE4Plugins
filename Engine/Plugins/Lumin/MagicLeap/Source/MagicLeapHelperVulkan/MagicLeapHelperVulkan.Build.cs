// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MagicLeapHelperVulkan : ModuleRules
	{
		public MagicLeapHelperVulkan(ReadOnlyTargetRules Target) : base(Target)
		{
			// Include headers to be public to other modules.
			PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"Engine",
					"MLSDK",
					"RHI",
					"RenderCore",
					"HeadMountedDisplay"
				});

			// TODO: Explore linking Unreal modules against a commong header and
			// having a runtime dll linking against the library according to the platform.
			if (Target.Platform != UnrealTargetPlatform.Mac && Target.Platform != UnrealTargetPlatform.IOS)
			{
				PrivateDependencyModuleNames.Add("VulkanRHI");
				PrivateDefinitions.Add("MLSDK_API_USE_VULKAN=1");
				string EngineSourceDirectory = "../../../../Source";

				if (Target.Platform == UnrealTargetPlatform.Linux)
				{
					PrivateIncludePaths.AddRange(
						new string[] {
							Path.Combine(EngineSourceDirectory, "ThirdParty/SDL2/SDL-gui-backend/include"),
						}
					);
				}

				PrivateIncludePaths.AddRange(
					new string[] {
						"MagicLeapHelperVulkan/Private",
						Path.Combine(EngineSourceDirectory, "Runtime/VulkanRHI/Private"),
						Path.Combine(EngineSourceDirectory, "Runtime/VulkanRHI/Private", ((Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64) ? "Windows" : Target.Platform.ToString()))
					});

				AddEngineThirdPartyPrivateStaticDependencies(Target, "Vulkan");
			}

		}
	}
}
