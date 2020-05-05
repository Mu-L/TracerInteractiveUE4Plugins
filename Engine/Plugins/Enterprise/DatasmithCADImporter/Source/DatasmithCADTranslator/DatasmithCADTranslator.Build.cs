// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class DatasmithCADTranslator : ModuleRules
	{
		public DatasmithCADTranslator(ReadOnlyTargetRules Target) : base(Target)
		{
            PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"CADInterfaces",
					"CADLibrary",
					"CADTools",
					"DatasmithCore",
					"DatasmithCoreTechParametricSurfaceData",
					"DatasmithContent",
					"DatasmithDispatcher",
					"DatasmithTranslator",
					"Engine",
					"MeshDescription",
					"Sockets",
				}
			);

			if (System.Type.GetType("CoreTech") != null)
			{
				PublicDependencyModuleNames.Add("CoreTech");
			}
		}
	}
}
