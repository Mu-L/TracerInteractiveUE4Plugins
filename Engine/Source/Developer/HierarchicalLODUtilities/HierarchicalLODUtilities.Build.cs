// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

public class HierarchicalLODUtilities : ModuleRules
{
    public HierarchicalLODUtilities(ReadOnlyTargetRules Target) : base(Target)
	{
        PublicIncludePaths.Add("Developer/HierarchicalLODUtilities/Public");

        PublicDependencyModuleNames.AddRange(
            new string[]
			{
				"Core",
				"CoreUObject"
			}
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
			{
				"Engine",
				"UnrealEd",
                "Projects",
			}
        );

        PrivateIncludePaths.AddRange(
            new string[]
            {
            }
        );

        DynamicallyLoadedModuleNames.AddRange(
            new string[]
            {
                "MeshUtilities",
                "MeshMergeUtilities",
                "MeshReductionInterface",
            }
        );
	}
}
