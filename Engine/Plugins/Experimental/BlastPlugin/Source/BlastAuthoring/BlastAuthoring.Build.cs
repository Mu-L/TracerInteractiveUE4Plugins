// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class BlastAuthoring : ModuleRules
	{
		public BlastAuthoring(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
                    "Core",
                    "CoreUObject",
                    "EditableMesh",
                    "MeshDescription",
					"StaticMeshDescription",
                    "Blast",
                    "BlastCore",
                    "GeometryCollectionCore",
                    "GeometryCollectionEngine"

					// ... add other public dependencies that you statically link with here ...
				}
                );

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
                    "Core",
                    "CoreUObject",
					"Engine",
                    "Blast",
                    "BlastCore"

					// ... add private dependencies that you statically link with here ...
				}
                );

			DynamicallyLoadedModuleNames.AddRange(
				new string[]
				{
					// ... add any modules that your module loads dynamically here ...
				}
				);
		}
	}
}
