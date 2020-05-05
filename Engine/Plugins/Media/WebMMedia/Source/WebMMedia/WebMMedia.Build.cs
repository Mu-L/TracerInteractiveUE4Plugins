// Copyright Epic Games, Inc. All Rights Reserved.


namespace UnrealBuildTool.Rules
{
	public class WebMMedia : ModuleRules
	{
		public WebMMedia(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"WebMMediaFactory",
					"Core",
					"Engine",
					"RenderCore",
					"RHI",
				});

			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Media",
					"MediaUtils",
					"libOpus",
					"UEOgg",
					"Vorbis",
				});

			// Some Linux architectures don't have the libs built yet
			bool bHaveWebMlibs = (!Target.IsInPlatformGroup(UnrealPlatformGroup.Unix) || Target.Architecture.StartsWith("x86_64"));
			if (bHaveWebMlibs)
			{
				PublicDependencyModuleNames.AddRange(
					new string[] {
					"LibVpx",
					"LibWebM",
					});
			}
			PublicDefinitions.Add("WITH_WEBM_LIBS=" + (bHaveWebMlibs ? "1" : "0"));
		}
	}
}
