// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class OnlineSubsystemUtils : ModuleRules
{
	public OnlineSubsystemUtils(ReadOnlyTargetRules Target) : base(Target)
    {
		PublicDefinitions.Add("ONLINESUBSYSTEMUTILS_PACKAGE=1");
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateIncludePaths.Add("OnlineSubsystemUtils/Private");

        string EnginePath = Path.GetFullPath(Target.RelativeEnginePath);
        string RuntimePath = EnginePath + "Source/Runtime/";

        bool bIsWindowsPlatformBuild = Target.Platform.IsInGroup(UnrealPlatformGroup.Windows);

        if (bIsWindowsPlatformBuild)
        {
            AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11Audio");
            PrivateIncludePaths.Add(RuntimePath + "Windows/XAudio2/Public");
            PrivateIncludePaths.Add(RuntimePath + "Windows/XAudio2/Private");
        }


		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"ImageCore",
				"Sockets",
				"Voice",
				"PacketHandler",
				"Json",
				"AudioMixer",
				"SignalProcessing",
				"AudioMixerCore",
				"DeveloperSettings"
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"NetCore",
				"Engine"
			}
		);

		PublicDependencyModuleNames.Add("OnlineSubsystem");
	}
}
