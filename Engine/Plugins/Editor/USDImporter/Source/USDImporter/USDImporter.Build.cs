// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class USDImporter : ModuleRules
	{
		public USDImporter(ReadOnlyTargetRules Target) : base(Target)
        {

			PublicIncludePaths.AddRange(
				new string[] {
				}
				);

			PrivateIncludePaths.AddRange(
				new string[] {
					ModuleDirectory + "/../UnrealUSDWrapper/Source/Public",
				}
				);

		
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"UnrealEd",
					"InputCore",
					"SlateCore",
                    "PropertyEditor",
					"Slate",
                    "EditorStyle",
                    "RawMesh",
                    "GeometryCache",
					"MeshDescription",
					"MeshUtilities",
                    "PythonScriptPlugin",
                    "RenderCore",
                    "RHI",
                    "MessageLog",
					"JsonUtilities",
                }
				);

			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"MeshDescription"
				}
			);

			string BaseLibDir = ModuleDirectory + "/../UnrealUSDWrapper/Lib/";

			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				string LibraryPath = BaseLibDir + "x64/Release/";
				PublicAdditionalLibraries.Add(LibraryPath+"/UnrealUSDWrapper.lib");

                foreach (string FilePath in Directory.EnumerateFiles(Path.Combine(ModuleDirectory, "../../Binaries/Win64/"), "*.dll", SearchOption.AllDirectories))
                {
                    RuntimeDependencies.Add(FilePath);
                }
            }
            else if (Target.Platform == UnrealTargetPlatform.Linux && Target.Architecture.StartsWith("x86_64"))
			{
                // link directly to runtime libs on Linux, as this also puts them into rpath
				string RuntimeLibraryPath = Path.Combine(ModuleDirectory, "../../Binaries", Target.Platform.ToString(), Target.Architecture.ToString());
				PrivateRuntimeLibraryPaths.Add(RuntimeLibraryPath);
				PublicAdditionalLibraries.Add(RuntimeLibraryPath +"/libUnrealUSDWrapper.so");

                foreach (string FilePath in Directory.EnumerateFiles(RuntimeLibraryPath, "*.so*", SearchOption.AllDirectories))
                {
                    RuntimeDependencies.Add(FilePath);
                }
            }
		}
	}
}
