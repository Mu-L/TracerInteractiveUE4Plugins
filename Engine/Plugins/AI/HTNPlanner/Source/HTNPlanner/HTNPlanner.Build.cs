// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    public class HTNPlanner : ModuleRules
    {
        public HTNPlanner(ReadOnlyTargetRules Target) : base(Target)
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
                new string[] {
                        "Core",
                        "CoreUObject",
                        "Engine",
                        "GameplayTags",
                        "GameplayTasks",
                        "AIModule"
                }
                );

            DynamicallyLoadedModuleNames.AddRange(
                new string[] {
                    // ... add any modules that your module loads dynamically here ...
                }
                );

            if (Target.bBuildEditor == true)
            {
                PrivateDependencyModuleNames.Add("UnrealEd");
            }

            if (Target.bBuildDeveloperTools || (Target.Configuration != UnrealTargetConfiguration.Shipping && Target.Configuration != UnrealTargetConfiguration.Test))
            {
                PrivateDependencyModuleNames.Add("GameplayDebugger");
                PublicDefinitions.Add("WITH_GAMEPLAY_DEBUGGER=1");
            }
            else
            {
                PublicDefinitions.Add("WITH_GAMEPLAY_DEBUGGER=0");
            }
        }
    }
}
