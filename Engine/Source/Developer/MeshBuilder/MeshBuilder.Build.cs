// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    //MeshBuilder module is a editor module
	public class MeshBuilder : ModuleRules
	{
		public MeshBuilder(ReadOnlyTargetRules Target) : base(Target)
		{
            PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"RHI",
					"Core",
					"CoreUObject",
                    "Engine",
                    "RenderCore",
                    "MeshDescription",
					"StaticMeshDescription",
                    "MeshReductionInterface",
                    "RawMesh",
					"MeshUtilities",
                    "MeshUtilitiesCommon",
                    "ClothingSystemRuntimeNv",
                    "MeshBoneReduction",
                    "SkeletalMeshUtilitiesCommon",
					"MeshBuilderCommon",
                }
			);

            AddEngineThirdPartyPrivateStaticDependencies(Target, "QuadricMeshReduction");
       }
	}
}
