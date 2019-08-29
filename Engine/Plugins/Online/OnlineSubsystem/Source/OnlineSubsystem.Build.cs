// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class OnlineSubsystem : ModuleRules
{
	public OnlineSubsystem(ReadOnlyTargetRules Target) : base(Target)
    {
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Json",
			}
		);

		PublicIncludePaths.Add(ModuleDirectory);

        PublicDefinitions.Add("ONLINESUBSYSTEM_PACKAGE=1");
		PublicDefinitions.Add("DEBUG_LAN_BEACON=0");

		PrivateDependencyModuleNames.AddRange(
			new string[] { 
				"Core", 
				"CoreUObject",
				"ImageCore",
				"Sockets",
				"JsonUtilities"
			}
		);
	}
}
