// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class PhysXVehiclesEditor : ModuleRules
	{
        public PhysXVehiclesEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
                    "Slate",
                    "SlateCore",
                    "Engine",
                    "UnrealEd",
                    "PropertyEditor",
                    "AnimGraphRuntime",
                    "AnimGraph",
                    "BlueprintGraph",
                    "PhysXVehicles",
					"PhysicsCore"
                }
			);

            PrivateDependencyModuleNames.AddRange(
                new string[] 
                {
                    "EditorStyle",
                    "AssetRegistry"
                }
            );
        }
	}
}
