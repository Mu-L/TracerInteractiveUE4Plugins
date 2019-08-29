// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ViewportSnapping : ModuleRules
{
	public ViewportSnapping(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.Add("UnrealEd");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
// 				"RenderCore",
// 				"ShaderCore",
// 				"RHI",
				"Slate",
				"UnrealEd"
			}
			);

// 		DynamicallyLoadedModuleNames.AddRange(
// 			new string[] {
// 				"MainFrame",
// 				"WorkspaceMenuStructure",
// 				"PropertyEditor"
// 			}
// 			);
	}
}
