// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Eigen : ModuleRules
{
    public Eigen(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;
		
		PublicIncludePaths.Add(ModuleDirectory);

        if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Mac)
        {
           PublicIncludePaths.Add( ModuleDirectory + "/Eigen/" );
        }

        PublicDefinitions.Add("EIGEN_MPL2_ONLY");
        bEnableShadowVariableWarnings = false;
    }
}
