// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class UndoHistory : ModuleRules
	{
		public UndoHistory(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"CoreUObject",
					"EditorStyle",
					"Engine",
					"InputCore",
					"Slate",
					"SlateCore",
					"UnrealEd",
					"ApplicationCore"
				}
			);

			PrivateIncludePaths.AddRange(
				new string[] {
					"Editor/UndoHistory/Private",
					"Editor/UndoHistory/Private/Widgets",
					"Editor/UndoHistory/Private/Classes"
				}
			);
		}
	}
}
