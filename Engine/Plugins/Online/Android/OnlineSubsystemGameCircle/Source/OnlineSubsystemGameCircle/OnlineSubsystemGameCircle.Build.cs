// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class OnlineSubsystemGameCircle : ModuleRules
	{
		public OnlineSubsystemGameCircle(ReadOnlyTargetRules Target) : base(Target)
        {
			PublicDefinitions.Add("ONLINESUBSYSTEMGAMECIRCLE_PACKAGE=1");
			PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

			PrivateIncludePaths.AddRange(
				new string[] {
				"OnlineSubsystemGameCircle/Private",
				"ThirdParty/jni"
				}
				);

			PrivateDependencyModuleNames.AddRange(
				new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"Sockets",
				"OnlineSubsystem",
				"Http",
				"GameCircleRuntimeSettings",
				"Launch"
				}
				);

			string LibDir = ModuleDirectory + "/../ThirdParty/jni/libs";
			PublicAdditionalLibraries.Add(Path.Combine(LibDir, "libAmazonGamesJni.so"));

			// Additional Frameworks and Libraries for Android
			if (Target.Platform == UnrealTargetPlatform.Android)
			{
				string PluginPath = Utils.MakePathRelativeTo(ModuleDirectory, Target.RelativeEnginePath);
				AdditionalPropertiesForReceipt.Add("AndroidPlugin", Path.Combine(PluginPath, "OnlineSubsystemGameCircle_UPL.xml"));
			}
		}
	}
}
