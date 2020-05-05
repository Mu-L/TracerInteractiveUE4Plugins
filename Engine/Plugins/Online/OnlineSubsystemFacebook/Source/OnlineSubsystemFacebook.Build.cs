// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class OnlineSubsystemFacebook : ModuleRules
{
	public OnlineSubsystemFacebook(ReadOnlyTargetRules Target) : base(Target)
	{
		bool bUsesRestfulImpl = false;
		PrivateDefinitions.Add("ONLINESUBSYSTEMFACEBOOK_PACKAGE=1");
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateIncludePaths.Add("Private");

		PrivateDependencyModuleNames.AddRange(
			new string[] { 
				"Core",
				"CoreUObject",
				"ApplicationCore",
				"HTTP",
				"ImageCore",
				"Json",
				"OnlineSubsystem", 
			}
			);

		AddEngineThirdPartyPrivateStaticDependencies(Target, "Facebook");

		if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			PublicDefinitions.Add("WITH_FACEBOOK=1");
			PrivateIncludePaths.Add("Private/IOS");
		}
		else if (Target.Platform == UnrealTargetPlatform.Android)
		{
			bool bHasFacebookSDK = false;
			string FacebookNFLDir = "";
			try
			{
				FacebookNFLDir = System.IO.Path.Combine(ModuleDirectory, "ThirdParty", "Android", "NotForLicensees", "FacebookSDK");
				bHasFacebookSDK = System.IO.Directory.Exists(FacebookNFLDir);
			}
			catch (System.Exception)
			{
			}

			PrivateIncludePaths.Add("Private/Android");

			if (bHasFacebookSDK)
			{
				string Err = string.Format("Facebook SDK found in {0}", FacebookNFLDir);
				System.Console.WriteLine(Err);

				PublicDefinitions.Add("WITH_FACEBOOK=1");
				PublicDefinitions.Add("UE4_FACEBOOK_VER=4.39.0");

				PrivateDependencyModuleNames.AddRange(
				new string[] {
				"Launch",
				}
				);

				string PluginPath = Utils.MakePathRelativeTo(ModuleDirectory, Target.RelativeEnginePath);
				AdditionalPropertiesForReceipt.Add("AndroidPlugin", Path.Combine(PluginPath, "OnlineSubsystemFacebook_UPL.xml"));
			}
			else
			{
				string Err = string.Format("Facebook SDK not found in {0}", FacebookNFLDir);
				System.Console.WriteLine(Err);
				PublicDefinitions.Add("WITH_FACEBOOK=0");
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
		{
			bUsesRestfulImpl = true;
			PublicDefinitions.Add("WITH_FACEBOOK=1");
		}
		else if (Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			PublicDefinitions.Add("WITH_FACEBOOK=1");
			PrivateIncludePaths.Add("Private/XboxOne");
		}
		else if (Target.Platform == UnrealTargetPlatform.PS4)
		{
			PublicDefinitions.Add("WITH_FACEBOOK=1");
			PrivateIncludePaths.Add("Private/PS4");
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicDefinitions.Add("WITH_FACEBOOK=1");
			bUsesRestfulImpl = true;
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			PublicDefinitions.Add("WITH_FACEBOOK=1");
			bUsesRestfulImpl = true;
		}
        else if (Target.Platform == UnrealTargetPlatform.Switch)
        {
            PublicDefinitions.Add("WITH_FACEBOOK=1");
            bUsesRestfulImpl = true;
        }
        else
        {
			PublicDefinitions.Add("WITH_FACEBOOK=0");
			PrecompileForTargets = PrecompileTargetsType.None;
		}

		if (bUsesRestfulImpl)
		{
			PublicDefinitions.Add("USES_RESTFUL_FACEBOOK=1");
			PrivateIncludePaths.Add("Private/Rest");
		}
		else
		{
			PublicDefinitions.Add("USES_RESTFUL_FACEBOOK=0");
		}
	}
}
