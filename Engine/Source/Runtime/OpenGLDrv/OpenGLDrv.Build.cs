// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

[SupportedPlatformsAttribute(new string[] {"Win32", "Win64", "Linux", "Android", "LinuxAArch64"})]
public class OpenGLDrv : ModuleRules
{
	public OpenGLDrv(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.Add("Runtime/OpenGLDrv/Private");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"ApplicationCore",
				"Engine",
				"RHI",
				"RenderCore",
				"PreLoadScreen"
			}
			);

		PrivateIncludePathModuleNames.Add("ImageWrapper");
		DynamicallyLoadedModuleNames.Add("ImageWrapper");

		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenGL");

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Linux))
		{
			string GLPath = Target.UEThirdPartySourceDirectory + "OpenGL/";
			PublicIncludePaths.Add(GLPath);
		}

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Linux))
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "SDL2");
		}

		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrivateIncludePathModuleNames.AddRange(
				new string[]
				{
					"TaskGraph"
				}
			);
		}

		if ((Target.Platform == UnrealTargetPlatform.Android) || (Target.Platform == UnrealTargetPlatform.Lumin))
		{
			PrivateDependencyModuleNames.Add("detex");
		}

        if (Target.Platform == UnrealTargetPlatform.Android)
        {
            // for Swappy
            PublicDefinitions.Add("USE_ANDROID_OPENGL_SWAPPY=1");

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
					"GoogleGameSDK"
                }
            );

			PrivateIncludePathModuleNames.AddRange(
				new string[]
				{
					"Launch"
				}
			);
		}

        if (Target.Platform != UnrealTargetPlatform.Win32 && Target.Platform != UnrealTargetPlatform.Win64
			&& Target.Platform != UnrealTargetPlatform.IOS && Target.Platform != UnrealTargetPlatform.Android
			&& !Target.IsInPlatformGroup(UnrealPlatformGroup.Linux)
			&& Target.Platform != UnrealTargetPlatform.TVOS && Target.Platform != UnrealTargetPlatform.Lumin)
		{
			PrecompileForTargets = PrecompileTargetsType.None;
		}

		PublicDefinitions.Add(Target.Platform == UnrealTargetPlatform.Android ? "USE_ANDROID_OPENGL=1" : "USE_ANDROID_OPENGL=0");
	}
}
