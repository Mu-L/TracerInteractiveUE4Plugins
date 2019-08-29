// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class Json : ModuleRules
	{
		public Json(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
				}
			); 

			PrivateIncludePaths.AddRange(
				new string[] {
					"Runtime/Json/Private",
				}
			);
		}
	}
}
