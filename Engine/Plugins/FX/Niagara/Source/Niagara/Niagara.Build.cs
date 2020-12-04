// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Niagara : ModuleRules
{
    public Niagara(ReadOnlyTargetRules Target) : base(Target)
    {
		PrivateIncludePaths.Add("../../../../Shaders/Shared");

        PrivateDependencyModuleNames.AddRange(
            new string[] {
                "NiagaraCore",
                "NiagaraShader",
                "Core",
                "Engine",
                "TimeManagement",
                "TraceLog",
                "Renderer",
                "JsonUtilities",
				"Landscape",
				"Json",
				"AudioPlatformConfiguration",
				"SignalProcessing",
				"ApplicationCore",
				"DeveloperSettings"
			}
        );


        PublicDependencyModuleNames.AddRange(
            new string[] {
                "NiagaraCore",
                "NiagaraShader",
                "MovieScene",
				"MovieSceneTracks",
				"CoreUObject",
                "VectorVM",
                "RHI",
                "NiagaraVertexFactories",
                "RenderCore",
                "IntelISPC",
            }
        );


        PrivateIncludePaths.AddRange(
            new string[] {
                "Niagara/Private",
            })
        ;

        // If we're compiling with the engine, then add Core's engine dependencies
        if (Target.bCompileAgainstEngine == true)
        {
            if (!Target.bBuildRequiresCookedData)
            {
                DynamicallyLoadedModuleNames.AddRange(new string[] { "DerivedDataCache" });
            }
        }


        if (Target.bBuildEditor == true)
        {
            PrivateDependencyModuleNames.AddRange(
                new string[] {
                "TargetPlatform",
                "UnrealEd",
				"SlateCore",
				"Slate"
            });
        }
    }
}
