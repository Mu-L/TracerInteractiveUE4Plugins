﻿// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;
using Tools.DotNETCommon;
using System.Linq;
using System.Xml;
using System.Xml.Linq;

namespace UnrealBuildTool
{
	class UEDeployLumin : UEBuildDeploy, ILuminDeploy
	{
		private FileReference ProjectFile;

		protected UnrealPluginLanguage UPL;

		public void SetLuminPluginData(List<string> Architectures, List<string> inPluginExtraData)
		{
			UPL = new UnrealPluginLanguage(ProjectFile, inPluginExtraData, Architectures, "", "", UnrealTargetPlatform.Lumin);
			UPL.SetTrace();
		}

		public UEDeployLumin(FileReference InProjectFile)
		{
			ProjectFile = InProjectFile;
		}

		private ConfigHierarchy GetConfigCacheIni(ConfigHierarchyType Type)
		{
			// @todo Lumin: So - this is the problem with subclassing a platform currently - ini files. Lumin will use Android ini files
			// until I finish and get code over from another branch (key letter Q) that allows for insertion of a subclassed ini platform thing
			return ConfigCache.ReadHierarchy(Type, DirectoryReference.FromFile(ProjectFile), UnrealTargetPlatform.Android);
		}

		private string GetRuntimeSetting(string Key)
		{
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			string Value;
			Ini.GetString("/Script/LuminRuntimeSettings.LuminRuntimeSettings", Key, out Value);
			return Value;
		}

		public string GetPackageName(string ProjectName)
		{
			string PackageName = GetRuntimeSetting("PackageName");
			// replace some variables
			PackageName = PackageName.Replace("[PROJECT]", ProjectName);
			PackageName = PackageName.Replace("-", "_");
			// Package names are required to be all lower case.
			return PackageName.ToLower();
		}

		private string GetApplicationType()
		{
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			bool Value = false;
			Ini.GetBool("/Script/LuminRuntimeSettings.LuminRuntimeSettings", "bIsScreensApp", out Value);
			if (Value)
			{
				return "ScreensImmersive";
			}
			return "Fullscreen";
		}

		private string GetApplicationDisplayName(string ProjectName)
		{
			string ApplicationDisplayName = GetRuntimeSetting("ApplicationDisplayName");
			if (String.IsNullOrWhiteSpace(ApplicationDisplayName))
			{
				return ProjectName;
			}
			return ApplicationDisplayName;
		}

		private string CleanFilePath(string FilePath)
		{
			// Removes the extra characters from a FFilePath parameter.
			// This functionality is required in the automation file to avoid having duplicate variables stored in the settings file.
			// Potentially this could be replaced with FParse::Value("IconForegroundModelPath="(FilePath="", Value).
			int startIndex = FilePath.IndexOf('"') + 1;
			int length = FilePath.LastIndexOf('"') - startIndex;
			return FilePath.Substring(startIndex, length);
		}

		public string GetIconModelStagingPath()
		{
			return "Icon/Model";
		}

		public string GetIconPortalStagingPath()
		{
			return "Icon/Portal";
		}

		public string GenerateManifest(string ProjectName, bool bForDistribution, string Architecture)
		{
			ConfigHierarchy GameIni = GetConfigCacheIni(ConfigHierarchyType.Game);
			string ProjectVersion = string.Empty;
			GameIni.GetString("/Script/EngineSettings.GeneralProjectSettings", "ProjectVersion", out ProjectVersion);
			if (string.IsNullOrEmpty(ProjectVersion))
			{
				ProjectVersion = "1.0.0.0";
			}

			ConfigHierarchy EngineIni = GetConfigCacheIni(ConfigHierarchyType.Engine);
			int VersionCode;
			EngineIni.GetInt32("/Script/LuminRuntimeSettings.LuminRuntimeSettings", "VersionCode", out VersionCode);

			bool bInternetRequired;
			EngineIni.GetBool("/Script/LuminRuntimeSettings.LuminRuntimeSettings", "bInternetRequired", out bInternetRequired);

			StringBuilder Text = new StringBuilder();

			string PackageName = GetPackageName(ProjectName);
			string ApplicationDisplayName = GetApplicationDisplayName(ProjectName);
			string TargetExecutableName = "bin/" + ProjectName;

			Text.AppendLine(string.Format("<manifest xmlns:ml=\"magicleap\" ml:package=\"{0}\" ml:version_name=\"{1}\" ml:version_code=\"{2}\">", PackageName, ProjectVersion, VersionCode));
			// @mltodo: query sdk_version
			Text.AppendLine(string.Format("\t<application ml:visible_name=\"{0}\" ml:is_debuggable=\"{1}\" ml:sdk_version=\"1.0\" ml:minimum_os=\"mlos_1.0\" ml:internet_required=\"{2}\">", ApplicationDisplayName, bForDistribution ? "false" : "true", bInternetRequired ? "true" : "false"));
			Text.AppendLine(string.Format("\t\t<component ml:name=\".fullscreen\" ml:visible_name=\"{0}\" ml:binary_name=\"{1}\" ml:type=\"{2}\">", ApplicationDisplayName, TargetExecutableName, GetApplicationType()));

			List<string> AppPrivileges;
			EngineIni.GetArray("/Script/LuminRuntimeSettings.LuminRuntimeSettings", "AppPrivileges", out AppPrivileges);
			if (AppPrivileges != null)
			{
				foreach (string Privilege in AppPrivileges)
				{
					string TrimmedPrivilege = Privilege.Trim(' ');
					if (TrimmedPrivilege != "")
					{
						string PrivilegeString = string.Format("\t\t\t<uses-privilege ml:name=\"{0}\"/>", TrimmedPrivilege);
						if (!Text.ToString().Contains(PrivilegeString))
						{
							Text.AppendLine(PrivilegeString);
						}
					}
				}
			}

			string IconTag = string.Format("<icon ml:name=\"fullscreen\" ml:model_folder=\"{0}\" ml:portal_folder=\"{1}\"/>", GetIconModelStagingPath(), GetIconPortalStagingPath());
			Text.AppendLine(string.Format("\t\t\t{0}", IconTag));

			List<string> ExtraComponentNodes;
			EngineIni.GetArray("/Script/LuminRuntimeSettings.LuminRuntimeSettings", "ExtraComponentNodes", out ExtraComponentNodes);
			if (ExtraComponentNodes != null)
			{
				foreach (string ComponentNode in ExtraComponentNodes)
				{
					Text.AppendLine(string.Format("\t\t\t{0}", ComponentNode));
				}
			}

			Text.AppendLine("\t\t</component>");

			List<string> ExtraApplicationNodes;
			EngineIni.GetArray("/Script/LuminRuntimeSettings.LuminRuntimeSettings", "ExtraApplicationNodes", out ExtraApplicationNodes);
			if (ExtraApplicationNodes != null)
			{
				foreach (string ApplicationNode in ExtraApplicationNodes)
				{
					Text.AppendLine(string.Format("\t\t{0}", ApplicationNode));
				}
			}

			Text.AppendLine(string.Format("\t\t{0}", IconTag));
			Text.AppendLine("\t</application>");
			Text.AppendLine("</manifest>");

			// allow plugins to modify final manifest HERE
			XDocument XDoc;
			try
			{
				XDoc = XDocument.Parse(Text.ToString());
			}
			catch (Exception e)
			{
				throw new BuildException("LuminManifest.xml is invalid {0}\n{1}", e, Text.ToString());
			}

			UPL.ProcessPluginNode(Architecture, "luminManifestUpdates", "", ref XDoc);
			return XDoc.ToString();
		}


		private List<string> CollectPluginDataPaths(TargetReceipt Receipt)
		{
			List<string> PluginExtras = new List<string>();
			if (Receipt == null)
			{
				Log.TraceInformation("Receipt is NULL");
				return PluginExtras;
			}

			// collect plugin extra data paths from target receipt
			var Results = Receipt.AdditionalProperties.Where(x => x.Name == "LuminPlugin");
			foreach (var Property in Results)
			{
				// Keep only unique paths
				string PluginPath = Property.Value;
				if (PluginExtras.FirstOrDefault(x => x == PluginPath) == null)
				{
					PluginExtras.Add(PluginPath);
					Log.TraceInformation("LuminPlugin: {0}", PluginPath);
				}
			}
			return PluginExtras;
		}

		public void InitUPL(string ProjectName, DirectoryReference ProjectDirectory, UnrealTargetConfiguration Configuration)
		{
			string UE4BuildPath = Path.Combine(ProjectDirectory.FullName, "Intermediate/Lumin/Mabu");
			string RelativeEnginePath = UnrealBuildTool.EngineDirectory.MakeRelativeTo(DirectoryReference.GetCurrentDirectory());
			string RelativeProjectPath = ProjectDirectory.MakeRelativeTo(DirectoryReference.GetCurrentDirectory());//.MakeRelativeTo(ProjectDirectory);

			UnrealTargetPlatform Platform = UnrealTargetPlatform.Lumin;
			string ConfigurationString = Configuration.ToString();

			string Architecture = "arm64-v8a";
			List<string> MLSDKArches = new List<string>();
			MLSDKArches.Add(Architecture);

			// get the receipt
			FileReference ReceiptFilename = TargetReceipt.GetDefaultPath(ProjectDirectory, ProjectName, Platform, Configuration, "");
			if (!File.Exists(ReceiptFilename.ToString()))
			{
				ReceiptFilename = TargetReceipt.GetDefaultPath(UnrealBuildTool.EngineDirectory, "UE4Game", Platform, Configuration, "");
			}
			Log.TraceInformation("Receipt Filename: {0}", ReceiptFilename);
			SetLuminPluginData(MLSDKArches, CollectPluginDataPaths(TargetReceipt.Read(ReceiptFilename, UnrealBuildTool.EngineDirectory, ProjectDirectory)));

			//gather all of the xml
			UPL.Init(MLSDKArches, true, RelativeEnginePath, UE4BuildPath, RelativeProjectPath, ConfigurationString);
		}

		public string StageFiles()
		{
			string Architecture = "arm64-v8a";

			//hard code for right now until we have multiple architectures
			return UPL.ProcessPluginNode(Architecture, "stageFiles", "");
		}

		public void MakeMabuPackage(string ProjectName, DirectoryReference ProjectDirectory, string ExePath, bool bForDistribution, string EngineDir)
		{
			string UE4BuildPath = Path.Combine(ProjectDirectory.FullName, "Intermediate/Lumin/Mabu");
			string MabuOutputPath = Path.Combine(UE4BuildPath, "Packaged");
			// note this must match LuminPlatform.Automation:Package
			string MabuFile = Path.Combine(UE4BuildPath, GetPackageName(ProjectName) + ".package");
			string ManifestFile = Path.Combine(UE4BuildPath, "manifest.xml");


			LuminToolChain ToolChain = new LuminToolChain(ProjectFile);

			// Generate manifest (after UPL is setup
			string Architecture = "arm64-v8a";
			var Manifest = GenerateManifest(ProjectName, bForDistribution, Architecture);
			File.WriteAllText(ManifestFile, Manifest);

			string Certificate = GetRuntimeSetting("Certificate");
			Certificate = CleanFilePath(Certificate);
			if (!string.IsNullOrEmpty(Certificate))
			{
				Certificate = Path.GetFullPath(Path.Combine(EngineDir, "Binaries/Lumin", Certificate));

				if (File.Exists(Certificate))
				{
					ToolChain.RunMabuWithException(Path.GetDirectoryName(MabuFile), String.Format("-t device -s \"{0}\" -o \"{1}\" \"{2}\"", Certificate, MabuOutputPath, Path.GetFileName(MabuFile)), "Building signed mabu package....");
				}
				else
				{
					throw new BuildException(string.Format("Certificate file does not exist at path {0}. Please enter a valid certificate file path in Project Settings > Magic Leap or clear the field if you do not intend to sign the package.", Certificate));
				}
			}
			else
			{
				if (bForDistribution)
				{
					// Certificate required for a distribution package.
					throw new BuildException("Packaging for distribution, however no certificate file has been chosen. Please enter a certificate file in Project Settings > Magic Leap.");
				}
				// If a certificate file is not present but the package is not for distribution, package without signing.
				else
				{
					ToolChain.RunMabuWithException(Path.GetDirectoryName(MabuFile), String.Format("-t device --allow-unsigned -o \"{0}\" \"{1}\"", MabuOutputPath, Path.GetFileName(MabuFile)), "Building mabu package....");
				}
			}

			// copy the .mpk into binaries
			// @todo Lumin: Move this logic into a function in this class, and have AndroidAutomation call into it in GetFinalMpkName
			// @todo Lumin: Handle the whole Prebuilt thing, it may need to go somwehere else, or maybe this isn't even called?
			// @todo Lumin: This is losing the -Debug-Lumin stuff :|
			string SourceMpkPath = Path.Combine(MabuOutputPath, GetPackageName(ProjectName) + ".mpk");

			string MpkName = Path.GetFileNameWithoutExtension(ExePath) + ".mpk";
			if (MpkName.StartsWith("UE4Game"))
			{
				MpkName = MpkName.Replace("UE4Game", ProjectName);
			}
			string DestMpkPath = Path.Combine(ProjectDirectory.FullName, "Binaries/Lumin", MpkName);
			if (!Directory.Exists(Path.GetDirectoryName(DestMpkPath)))
			{
				Directory.CreateDirectory(Path.GetDirectoryName(DestMpkPath));
			}
			if (File.Exists(DestMpkPath))
			{
				File.Delete(DestMpkPath);
			}
			File.Copy(SourceMpkPath, DestMpkPath);
		}

		public bool PrepForUATPackageOrDeploy(FileReference ProjectFile, string InProjectName, DirectoryReference InProjectDirectory, string InExecutablePath, string InEngineDir, bool bForDistribution, string CookFlavor, bool bIsDataDeploy)
		{
			if (!bIsDataDeploy)
			{
				MakeMabuPackage(InProjectName, InProjectDirectory, InExecutablePath, bForDistribution, InEngineDir);
			}
			return true;
		}

		public override bool PrepTargetForDeployment(UEBuildDeployTarget InTarget)
		{
			// @todo Lumin: Need to create a MabuFile with no data files - including the executable!!
			return true;
		}


	}
}
