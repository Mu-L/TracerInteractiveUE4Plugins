// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class XMPP : ModuleRules
{
	public XMPP(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDefinitions.Add("XMPP_PACKAGE=1");

        PrivateIncludePaths.AddRange(
			new string[] 
			{
				"Runtime/Online/XMPP/Private"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] 
			{ 
				"Core",
				"Json"
			}
		);

		if (Target.Platform == UnrealTargetPlatform.Win64 ||
			Target.Platform == UnrealTargetPlatform.Win32 ||
			Target.Platform == UnrealTargetPlatform.Linux ||
			Target.Platform == UnrealTargetPlatform.Mac ||
			Target.Platform == UnrealTargetPlatform.PS4 )
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "WebRTC");
			PublicDefinitions.Add("WITH_XMPP_JINGLE=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_XMPP_JINGLE=0");
		}
		
		if (Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "libstrophe");
		}
		else
		{
			PublicDefinitions.Add("WITH_XMPP_STROPHE=0");
		}

		if (Target.Platform == UnrealTargetPlatform.Win64 ||
			Target.Platform == UnrealTargetPlatform.Win32 ||
			Target.Platform == UnrealTargetPlatform.Mac ||
			Target.Platform == UnrealTargetPlatform.PS4)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "libcurl");
		}
	}
}
