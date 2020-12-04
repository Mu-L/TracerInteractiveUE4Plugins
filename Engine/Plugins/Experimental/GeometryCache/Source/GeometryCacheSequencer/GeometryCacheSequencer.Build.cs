// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    public class GeometryCacheSequencer : ModuleRules
    {
        public GeometryCacheSequencer(ReadOnlyTargetRules Target) : base(Target)
        {
            PublicIncludePathModuleNames.AddRange(
                new string[] {
                "Sequencer",
                }
            );
            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "AssetTools",
                    "Core",
                    "CoreUObject",
                    "EditorStyle",
                    "Engine",
					"LevelSequence",
                    "MovieScene",
                    "MovieSceneTools",
                    "MovieSceneTracks",
                    "RHI",
                    "Sequencer",
                    "Slate",
                    "SlateCore",
                    "TimeManagement",
                    "UnrealEd",
                    "GeometryCacheTracks",
                    "GeometryCache"
                }
            );
        }
    }
}
