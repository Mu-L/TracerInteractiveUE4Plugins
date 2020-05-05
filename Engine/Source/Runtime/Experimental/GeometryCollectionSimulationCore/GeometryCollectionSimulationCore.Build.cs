// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class GeometryCollectionSimulationCore : ModuleRules
	{
        public GeometryCollectionSimulationCore(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add("Runtime/Experimental/GeometryCollectionSimulationCore/Private");
            PublicIncludePaths.Add(ModuleDirectory + "/Public");

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
                    "GeometryCollectionCore",
                    "Chaos",
                    "FieldSystemCore"
                }
				);
		}
	}
}
