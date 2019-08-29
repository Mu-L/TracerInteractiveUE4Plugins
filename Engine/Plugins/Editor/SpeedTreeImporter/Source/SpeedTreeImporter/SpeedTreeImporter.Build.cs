// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class SpeedTreeImporter : ModuleRules
	{
		public SpeedTreeImporter(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicIncludePaths.AddRange(
				new string[] {
				}
				);

			PrivateIncludePaths.AddRange(
				new string[] {
				}
				);

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
				}
				);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
                    "Core",
				    "CoreUObject",
				    "Engine",
				    "Slate",
					"SlateCore",
                    "EditorStyle",
                    "InputCore",
                    "UnrealEd",
                    "MainFrame",
                    "MeshDescription",
                }
				);

			DynamicallyLoadedModuleNames.AddRange(
				new string[]
				{
				}
				);
				
			AddEngineThirdPartyPrivateStaticDependencies(Target, "SpeedTree");
		}
	}
}
