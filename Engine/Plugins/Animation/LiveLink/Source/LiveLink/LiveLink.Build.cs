// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    public class LiveLink : ModuleRules
    {
        public LiveLink(ReadOnlyTargetRules Target) : base(Target)
        {
            PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "MovieScene",
                "MovieSceneTracks",
                "Projects",

                "Messaging",
                "LiveLinkInterface",
                "LiveLinkMessageBusFramework",
				"HeadMountedDisplay",
				"TimeManagement"
            }
            );
        }
    }
}
