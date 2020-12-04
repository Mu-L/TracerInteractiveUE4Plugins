// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class UnrealPakTarget : TargetRules
{
	public UnrealPakTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Program;
		LinkType = TargetLinkType.Modular;
		LaunchModuleName = "UnrealPak";

		bBuildDeveloperTools = false;
		bUseMallocProfiler = false;
		bCompileWithPluginSupport = true;
		bIncludePluginsForTargetPlatforms = true;

		// Editor-only data, however, is needed
		bBuildWithEditorOnlyData = true;

		// Currently this app is not linking against the engine, so we'll compile out references from Core to the rest of the engine
		bCompileAgainstEngine = false;
		bCompileAgainstCoreUObject = false;

		// ICU is not needed
		bCompileICU = false;

		// UnrealPak is a console application, not a Windows app (sets entry point to main(), instead of WinMain())
		bCompileAgainstApplicationCore = false;
		bIsBuildingConsoleApplication = true;
	}
}
