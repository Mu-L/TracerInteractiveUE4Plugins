// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.IO;
using UnrealBuildTool;

[SupportedPlatforms("Win32", "Win64", "Mac", "Linux")]
[SupportedConfigurations(UnrealTargetConfiguration.Debug, UnrealTargetConfiguration.Development, UnrealTargetConfiguration.Shipping)]
public class UnrealCEFSubProcessTarget : TargetRules
{
	public UnrealCEFSubProcessTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Program;
		LinkType = TargetLinkType.Monolithic;
		LaunchModuleName = "UnrealCEFSubProcess";

		// Change the undecorated exe name to be the shipping one on windows
		UndecoratedConfiguration = UnrealTargetConfiguration.Shipping;

		// Turn off various third party features we don't need

		// Currently we force Lean and Mean mode
		bBuildDeveloperTools = false;

		// Currently this app is not linking against the engine, so we'll compile out references from Core to the rest of the engine
		bCompileAgainstEngine = false;
		bCompileAgainstCoreUObject = false;
		bBuildWithEditorOnlyData = true;

		// Never use malloc profiling in CEFSubProcess.
		bUseMallocProfiler = false;

		// Force all shader formats to be built and included.
		//bForceBuildShaderFormats = true;

		// CEFSubProcess is a console application, not a Windows app (sets entry point to main(), instead of WinMain())
		bIsBuildingConsoleApplication = false;

		// Disable logging, as the sub processes are spawned often and logging will just slow them down
		GlobalDefinitions.Add("ALLOW_LOG_FILE=0");

		// Epic Games Launcher needs to run on OS X 10.9, so CEFSubProcess needs this as well
		bEnableOSX109Support = true;
	}
}
