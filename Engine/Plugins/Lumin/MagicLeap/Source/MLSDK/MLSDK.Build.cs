// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using Tools.DotNETCommon;

public class MLSDK : ModuleRules
{
	public MLSDK(ReadOnlyTargetRules Target) : base(Target)
	{
		// Needed for FVector, FQuat and FTransform used in MagicLeapMath.h
		PrivateDependencyModuleNames.Add("Core");
		// Include headers to be public to other modules.
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));

		Type = ModuleType.External;

		string MLSDKPath = System.Environment.GetEnvironmentVariable("MLSDK");
		bool bIsMLSDKInstalled = false;
		if (MLSDKPath != null)
		{
			string IncludePath = Path.Combine(MLSDKPath, "include");
			string LibraryPath = Path.Combine(MLSDKPath, "lib");
			string VirtualDeviceLibraryPath = Path.Combine(MLSDKPath, "VirtualDevice", "lib");
			string LibraryPlatformFolder = string.Empty;
			switch (Target.Platform)
			{
				case UnrealTargetPlatform.Win64:
					LibraryPlatformFolder = "win64";
					break;
				case UnrealTargetPlatform.Mac:
					LibraryPlatformFolder = "osx";
					break;
				case UnrealTargetPlatform.Linux:
					LibraryPlatformFolder = "linux64";
					break;
				case UnrealTargetPlatform.Lumin:
					LibraryPlatformFolder = "lumin";
					break;
			}
			LibraryPath = Path.Combine(LibraryPath, LibraryPlatformFolder);

			bIsMLSDKInstalled = Directory.Exists(IncludePath) && Directory.Exists(LibraryPath);
			if (bIsMLSDKInstalled)
			{
				string ProjectFileName = null != Target.ProjectFile ? Target.ProjectFile.FullName : "";
				DirectoryReference ProjectDir =
					string.IsNullOrEmpty(ProjectFileName) ? (DirectoryReference)null : Target.ProjectFile.Directory;
				ConfigHierarchy Ini = ConfigCache.ReadHierarchy(ConfigHierarchyType.Engine, ProjectDir, Target.Platform);

				PublicIncludePaths.Add(IncludePath);
				if (Target.Platform != UnrealTargetPlatform.Lumin)
				{
					string VirtualDeviceIncludePath = "";
					if (!Ini.TryGetValue("MLSDK", "IncludePath", out VirtualDeviceIncludePath))
					{
						VirtualDeviceIncludePath = Path.Combine(MLSDKPath, "VirtualDevice", "include");
					}
					PublicIncludePaths.Add(VirtualDeviceIncludePath);
				}
				PublicIncludePaths.Add(Path.Combine(MLSDKPath, "lumin/usr/include/vulkan"));

				string MLSDKLibraryPath = "";
				Ini.TryGetValue("MLSDK", "LibraryPath", out MLSDKLibraryPath);

				PublicLibraryPaths.Add(LibraryPath);
				if (!string.IsNullOrEmpty(MLSDKLibraryPath))
				{
					PublicLibraryPaths.Add(MLSDKLibraryPath);
				}

				if (Target.Platform != UnrealTargetPlatform.Lumin)
				{
					PublicLibraryPaths.Add(VirtualDeviceLibraryPath);
				}

				string[] MLSDKLibraryList = new string[] {
					"ml_audio",
					"ml_camera_metadata",
					"ml_camera",
					"ml_dispatch",
					"ml_ext_logging",
					"ml_graphics",
					"ml_identity",
					"ml_input",
					"ml_lifecycle",
					"ml_mediacodeclist",
					"ml_mediacodec",
					"ml_mediacrypto",
					"ml_mediadrm",
					"ml_mediaerror",
					"ml_mediaextractor",
					"ml_mediaformat",
					"ml_mediaplayer",
					"ml_musicservice",
					"ml_perception_client",
					"ml_privileges",
					"ml_screens",
					"ml_secure_storage",
					"ml_sharedfile"
				};

				if (Target.Platform == UnrealTargetPlatform.Win64)
				{
					foreach (string libname in MLSDKLibraryList)
					{
						PublicAdditionalLibraries.Add(string.Format("{0}.lib", libname));
						PublicDelayLoadDLLs.Add(string.Format("{0}.dll", libname));
					}

					PublicAdditionalLibraries.Add("ml_virtual_device.lib");
					PublicDelayLoadDLLs.Add("ml_virtual_device.dll");
				}
				else if (Target.Platform == UnrealTargetPlatform.Mac)
				{
					foreach (string libname in MLSDKLibraryList)
					{
						string lib = string.Format("lib{0}.dylib", libname);
						if (!string.IsNullOrEmpty(MLSDKLibraryPath) && File.Exists(Path.Combine(MLSDKLibraryPath, lib)))
						{
							PublicDelayLoadDLLs.Add(Path.Combine(MLSDKLibraryPath, lib));
						}
						else
						{
							PublicDelayLoadDLLs.Add(Path.Combine(LibraryPath, lib));
						}
					}

					string virtualDeviceLib = "libml_virtual_device.dylib";
					if (!string.IsNullOrEmpty(MLSDKLibraryPath) && File.Exists(Path.Combine(MLSDKLibraryPath, virtualDeviceLib)))
					{
						PublicDelayLoadDLLs.Add(Path.Combine(MLSDKLibraryPath, virtualDeviceLib));
					}
					else
					{
						PublicDelayLoadDLLs.Add(Path.Combine(VirtualDeviceLibraryPath, virtualDeviceLib));
					}
				}
				else if (Target.Platform == UnrealTargetPlatform.Linux)
				{
					foreach (string libname in MLSDKLibraryList)
					{
						string lib = string.Format("lib{0}.so", libname);
						if (!string.IsNullOrEmpty(MLSDKLibraryPath) && File.Exists(Path.Combine(MLSDKLibraryPath, lib)))
						{
							PublicDelayLoadDLLs.Add(Path.Combine(MLSDKLibraryPath, lib));
						}
						else
						{
							PublicDelayLoadDLLs.Add(Path.Combine(LibraryPath, lib));
						}
					}
					
					string virtualDeviceLib = "libml_virtual_device.so";
					if (!string.IsNullOrEmpty(MLSDKLibraryPath) && File.Exists(Path.Combine(MLSDKLibraryPath, virtualDeviceLib)))
					{
						PublicDelayLoadDLLs.Add(Path.Combine(MLSDKLibraryPath, virtualDeviceLib));
					}
					else
					{
						PublicDelayLoadDLLs.Add(Path.Combine(VirtualDeviceLibraryPath, virtualDeviceLib));
					}
				}
				else if (Target.Platform == UnrealTargetPlatform.Lumin)
				{
					foreach (string libname in MLSDKLibraryList)
					{
						PublicAdditionalLibraries.Add(libname);
						PublicDelayLoadDLLs.Add(string.Format("lib{0}.so", libname));
					}
				}
			}
		}
		PublicDefinitions.Add("WITH_MLSDK=" + (bIsMLSDKInstalled ? "1" : "0"));
	}
}
