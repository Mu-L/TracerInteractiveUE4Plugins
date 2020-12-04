// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class USDStageImporter : ModuleRules
	{
		public USDStageImporter(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"InputCore",
					"JsonUtilities",
					"MainFrame",
					"MessageLog",
					"RenderCore", // So that we can release resources of reimported meshes
					"Slate",
					"SlateCore",
					"UnrealEd",
					"UnrealUSDWrapper",
					"USDClasses",
					"USDStage",
					"USDSchemas",
					"USDUtilities",
				}
			);
		}
	}
}
