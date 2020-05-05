// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class HTTP : ModuleRules
{
	public HTTP(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDefinitions.Add("HTTP_PACKAGE=1");

		PrivateIncludePaths.AddRange(
			new string[] {
				"Runtime/Online/HTTP/Private",
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"Sockets",
				"SSL",
			}
			);

		bool bWithCurl = false;

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "WinHttp");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "libcurl");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");

			bWithCurl = true;
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix) ||
				Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "libcurl");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");

			bWithCurl = true;
		}
		else if (Target.Platform == UnrealTargetPlatform.Switch)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "libcurl");

			bWithCurl = true;
		}
		else
		{
			PublicDefinitions.Add("WITH_LIBCURL=0");
		}

		if (bWithCurl)
		{
			PublicDefinitions.Add("CURL_ENABLE_DEBUG_CALLBACK=1");
			if (Target.Configuration != UnrealTargetConfiguration.Shipping)
			{
				PublicDefinitions.Add("CURL_ENABLE_NO_TIMEOUTS_OPTION=1");
			}
		}

		if (Target.Platform == UnrealTargetPlatform.IOS || Target.Platform == UnrealTargetPlatform.TVOS || Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicFrameworks.Add("Security");
		}
	}
}
