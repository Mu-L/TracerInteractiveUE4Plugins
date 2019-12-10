// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TraceServices : ModuleRules
{
	public TraceServices(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"Sockets",
				"TraceAnalysis",
			});

	}
}
