// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetalRHI : ModuleRules
{	
	public MetalRHI(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"ApplicationCore",
				"Engine",
				"RHI",
				"RenderCore",
			}
			);

		AddEngineThirdPartyPrivateStaticDependencies(Target,
			"MTLPP"
		);
			
		PublicWeakFrameworks.Add("Metal");

		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicFrameworks.Add("QuartzCore");
		}

		var StatsModule = "../Plugins/NotForLicensees/MetalStatistics/MetalStatistics.uplugin";
		bool bMetalStats = System.IO.File.Exists(StatsModule);
		if ( bMetalStats && Target.Configuration != UnrealTargetConfiguration.Shipping )
		{
			PublicDefinitions.Add("METAL_STATISTICS=1");
			WhitelistRestrictedFolders.Add("Public/NotForLicensees");
		}
	}
}
