// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ArchVisCharacter : ModuleRules
	{
		public ArchVisCharacter(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add("ArchVisCharacter/Private");

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
                    "Engine",
//                     "RenderCore",
//                     "RHI"
				}
				);
		}
	}
}
