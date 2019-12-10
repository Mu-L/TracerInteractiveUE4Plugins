// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class DatasmithWireTranslator : ModuleRules
{
	public DatasmithWireTranslator(ReadOnlyTargetRules Target) : base(Target)
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
                "DatasmithContent",
				"DatasmithCore",
				"DatasmithCoreTechExtension",
				"DatasmithImporter",
				"Engine",
				"MeshDescription",
                "StaticMeshDescription",
            }
        );

		if (System.Type.GetType("OpenModel") != null)
		{
			PrivateDependencyModuleNames.Add("OpenModel");
		}

		if (System.Type.GetType("CoreTech") != null)
		{
			PrivateDependencyModuleNames.Add("CoreTech");
		}
	}
}
