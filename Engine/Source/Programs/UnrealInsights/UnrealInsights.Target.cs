// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

[SupportedPlatforms("Win64", "Linux")]
public class UnrealInsightsTarget : TargetRules
{
	public UnrealInsightsTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Program;
		LinkType = TargetLinkType.Modular; // TargetLinkType.Monolithic;

		LaunchModuleName = "UnrealInsights";
		ExtraModuleNames.Add("EditorStyle");

		bBuildDeveloperTools = false;
		bCompileAgainstEngine = false;
		bCompileAgainstCoreUObject = true;
		bForceBuildTargetPlatforms = true;
		bCompileWithStatsWithoutEngine = true;
		bCompileWithPluginSupport = true;

		// For UI functionality
		bBuildDeveloperTools = true;

		bHasExports = false;
	}
}
