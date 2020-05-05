// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class WinHttp : ModuleRules
{
	public WinHttp(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.Platform == UnrealTargetPlatform.Win32 ||
			Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("winhttp.lib");
		}
	}
}

