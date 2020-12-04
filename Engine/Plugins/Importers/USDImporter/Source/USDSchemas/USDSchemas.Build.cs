// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class USDSchemas : ModuleRules
	{
		public USDSchemas(ReadOnlyTargetRules Target) : base(Target)
		{
			bUseRTTI = true;

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"CinematicCamera",
					"Core",
					"CoreUObject",
					"EditorStyle",
					"Engine",
                    "GeometryCache",
                    "GeometryCacheUSD",
					"MaterialEditor",
					"MeshDescription",
					"MeshUtilities",
					"MessageLog",
					"PropertyEditor",
					"RenderCore",
					"Slate",
					"SlateCore",
					"StaticMeshDescription",
					"UnrealUSDWrapper",
					"USDClasses",
					"USDUtilities",
					"UnrealEd",
				}
				);
		}
	}
}
