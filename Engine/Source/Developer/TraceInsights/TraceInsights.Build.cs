// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TraceInsights : ModuleRules
{
	public TraceInsights(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.AddRange
		(
			new string[] {
				"Developer/TraceInsights/Private",
			}
		);

		PublicDependencyModuleNames.AddRange
		(
			new string[] {
				"Core",
				"ApplicationCore",
				"InputCore",
				"RHI",
				"RenderCore",
				"Sockets",
				"Slate",
				"EditorStyle",
				"TraceLog",
				"TraceAnalysis",
				"TraceServices",
				"DesktopPlatform",
				"WorkspaceMenuStructure",
			}
		);

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Engine",
				}
			);
		}

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"SlateCore",
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Messaging",
				"SessionServices",
			}
		);
	}
}
