// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Android_ETC1aTargetPlatform : ModuleRules
{
	public Android_ETC1aTargetPlatform( ReadOnlyTargetRules Target ) : base(Target)
	{
		BinariesSubFolder = "Android";

        PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"TargetPlatform",
				"DesktopPlatform",
				"AndroidDeviceDetection",
			}
		);

		PublicIncludePaths.AddRange(
			new string[]
			{
				"Runtime/Core/Public/Android"
			}
		);

		if (Target.bCompileAgainstEngine)
		{
			PrivateDependencyModuleNames.Add("Engine");
			PrivateIncludePathModuleNames.Add("TextureCompressor");		//@todo android: AndroidTargetPlatform.Build
		}

        PublicDefinitions.Add("WITH_OGGVORBIS=1");

		PrivateIncludePaths.AddRange(
			new string[]
			{
				"Developer/Android/AndroidTargetPlatform/Private",				
			}
		);
	}
}
