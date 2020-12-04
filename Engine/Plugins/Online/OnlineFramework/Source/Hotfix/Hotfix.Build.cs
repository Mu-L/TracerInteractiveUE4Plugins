// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class Hotfix : ModuleRules
{
	public Hotfix(ReadOnlyTargetRules Target) : base(Target)
    {
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
        PrivateDependencyModuleNames.AddRange(
			new string[] { 
				"Core",
				"CoreUObject",
				"Engine",
                "HTTP",
				"OnlineSubsystem",
				"OnlineSubsystemUtils"
			}
			);

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"PatchCheck",
				"InstallBundleManager",
			}
			);

		bool bHasOnlineTracing = Directory.Exists(Path.Combine(EngineDirectory, "Restricted", "NotForLicensees", "Plugins", "Online", "OnlineTracing"));
		if (bHasOnlineTracing)
		{
			PublicDefinitions.Add("WITH_ONLINETRACING=1");
			PrivateDependencyModuleNames.Add("OnlineTracing");
		}
	}
}
