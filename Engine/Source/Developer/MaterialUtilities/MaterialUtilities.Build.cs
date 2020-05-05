// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class MaterialUtilities : ModuleRules
{
	public MaterialUtilities(ReadOnlyTargetRules Target) : base(Target)
	{
        PrivateDependencyModuleNames.AddRange(
			new string [] {
				"Core",
				"CoreUObject",
				"Engine",
				"RenderCore",
                "Renderer",
                "RHI",
                "Landscape",
                "UnrealEd",
                "MaterialBaking",
            }
		);

        DynamicallyLoadedModuleNames.AddRange(
            new string[] {
                "MeshMergeUtilities",
            }
        );

        PublicDependencyModuleNames.AddRange(
			new string [] {
                 "MeshDescription",
				 "StaticMeshDescription"
			}
		);      

        PrivateIncludePathModuleNames.AddRange(
            new string[] {
                "Landscape",
                "MeshMergeUtilities",
            }
        );

        CircularlyReferencedDependentModules.AddRange(
            new string[] {
                "Landscape"
            }
        );
	}
}
