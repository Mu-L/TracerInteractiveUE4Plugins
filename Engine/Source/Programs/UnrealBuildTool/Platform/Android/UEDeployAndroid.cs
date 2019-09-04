// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Xml;
using System.Diagnostics;
using System.IO;
using Microsoft.Win32;
using System.Xml.Linq;
using Tools.DotNETCommon;
using System.Security.Cryptography;

namespace UnrealBuildTool
{
	class UEDeployAndroid : UEBuildDeploy, IAndroidDeploy
	{
		private const string XML_HEADER = "<?xml version=\"1.0\" encoding=\"utf-8\"?>";

		// Minimum Android SDK that must be used for Java compiling
		readonly int MinimumSDKLevel = 28;

		// Minimum SDK version needed for Gradle based on active plugins (14 is for Google Play Services 11.0.4)
		private int MinimumSDKLevelForGradle = 14;

		// Reserved Java keywords not allowed in package names without modification
		static private string[] JavaReservedKeywords = new string[] {
			"abstract", "assert", "boolean", "break", "byte", "case", "catch", "char", "class", "const", "continue", "default", "do",
			"double", "else", "enum", "extends", "final", "finally", "float", "for", "goto", "if", "implements", "import", "instanceof",
			"int", "interface", "long", "native", "new", "package", "private", "protected", "public", "return", "short", "static",
			"strictfp", "super", "switch", "sychronized", "this", "throw", "throws", "transient", "try", "void", "volatile", "while",
			"false", "null", "true"
		};

		/// <summary>
		/// Internal usage for GetApiLevel
		/// </summary>
		private List<string> PossibleApiLevels = null;

		protected FileReference ProjectFile;

		/// <summary>
		/// Determines whether we package data inside the APK. Based on and  OR of "-ForcePackageData" being
		/// false and bPackageDataInsideApk in /Script/AndroidRuntimeSettings.AndroidRuntimeSettings being true
		/// </summary>
		protected bool bPackageDataInsideApk = false;

		public UEDeployAndroid(FileReference InProjectFile, bool InForcePackageData)
		{
			ProjectFile = InProjectFile;

			// read the ini value and OR with the command line value
			bool IniValue = ReadPackageDataInsideApkFromIni(null);
			bPackageDataInsideApk = InForcePackageData || IniValue == true;
		}

		// Enable Gradle instead of Ant (project-level setting for now)
		private bool bGradleEnabled = false;

		private UnrealPluginLanguage UPL = null;
		private string UPLHashCode = null;
		private bool ARCorePluginEnabled = false;
		private bool FacebookPluginEnabled = false;
		private bool OculusMobilePluginEnabled = false;
		private bool GoogleVRPluginEnabled = false;
		private bool CrashlyticsPluginEnabled = false;

		public void SetAndroidPluginData(List<string> Architectures, List<string> inPluginExtraData)
		{
			List<string> NDKArches = new List<string>();
			foreach (string Arch in Architectures)
			{
				NDKArches.Add(GetNDKArch(Arch));
			}

			// check if certain plugins are enabled
			ARCorePluginEnabled = false;
			FacebookPluginEnabled = false;
			GoogleVRPluginEnabled = false;
			OculusMobilePluginEnabled = false;
			CrashlyticsPluginEnabled = false;
			foreach (string Plugin in inPluginExtraData)
			{
				// check if the Facebook plugin was enabled
				if (Plugin.Contains("OnlineSubsystemFacebook_UPL"))
				{
					FacebookPluginEnabled = true;
					continue;
				}

				// check if the ARCore plugin was enabled
				if (Plugin.Contains("GoogleARCoreBase_APL"))
				{
					ARCorePluginEnabled = true;
					continue;
				}

				// check if the Oculus Mobile plugin was enabled
				if (Plugin.Contains("OculusMobile_APL"))
				{
					OculusMobilePluginEnabled = true;
					continue;
				}

				// check if the GoogleVR plugin was enabled
				if (Plugin.Contains("GoogleVRHMD"))
				{
					GoogleVRPluginEnabled = true;
					continue;
				}

				// check if Crashlytics plugin was enabled
				// NOTE: There is a thirdparty plugin using Crashlytics_UPL_Android.xml which shouldn't use this code so check full name
				if (Plugin.Contains("Crashlytics_UPL.xml"))
				{
					CrashlyticsPluginEnabled = true;
					continue;
				}
			}

			UPL = new UnrealPluginLanguage(ProjectFile, inPluginExtraData, NDKArches, "http://schemas.android.com/apk/res/android", "xmlns:android=\"http://schemas.android.com/apk/res/android\"", UnrealTargetPlatform.Android);
			UPLHashCode = UPL.GetUPLHash();
//			APL.SetTrace();
		}

		private void SetMinimumSDKLevelForGradle()
		{
			if (FacebookPluginEnabled)
			{
				MinimumSDKLevelForGradle = Math.Max(MinimumSDKLevelForGradle, 15);
			}
			if (ARCorePluginEnabled)
			{
				MinimumSDKLevelForGradle = Math.Max(MinimumSDKLevelForGradle, 19);
			}
		}

		/// <summary>
		/// Simple function to pipe output asynchronously
		/// </summary>
		private void ParseApiLevel(object Sender, DataReceivedEventArgs Event)
		{
			// DataReceivedEventHandler is fired with a null string when the output stream is closed.  We don't want to
			// print anything for that event.
			if (!String.IsNullOrEmpty(Event.Data))
			{
				string Line = Event.Data;
				if (Line.StartsWith("id:"))
				{
					// the line should look like: id: 1 or "android-19"
					string[] Tokens = Line.Split("\"".ToCharArray());
					if (Tokens.Length >= 2)
					{
						PossibleApiLevels.Add(Tokens[1]);
					}
				}
			}
		}

		private ConfigHierarchy GetConfigCacheIni(ConfigHierarchyType Type)
		{
			return ConfigCache.ReadHierarchy(Type, DirectoryReference.FromFile(ProjectFile), UnrealTargetPlatform.Android);
		}

		private string GetLatestSDKApiLevel(AndroidToolChain ToolChain, string PlatformsDir)
		{
			// get a list of SDK platforms
			if (!Directory.Exists(PlatformsDir))
			{
				throw new BuildException("No platforms found in {0}", PlatformsDir);
			}

			// return the latest of them
			string[] PlatformDirectories = Directory.GetDirectories(PlatformsDir);
			if (PlatformDirectories != null && PlatformDirectories.Length > 0)
			{
				return ToolChain.GetLargestApiLevel(PlatformDirectories);
			}

			throw new BuildException("Can't make an APK without an API installed ({0} does not contain any SDKs)", PlatformsDir);
		}

		private bool ValidateSDK(string PlatformsDir, string ApiString)
		{
			if (!Directory.Exists(PlatformsDir))
			{
				return false;
			}

			string SDKPlatformDir = Path.Combine(PlatformsDir, ApiString);
			return Directory.Exists(SDKPlatformDir);
		}

		private int GetApiLevelInt(string ApiString)
		{
			int VersionInt = 0;
			if (ApiString.Contains("-"))
			{
				int Version;
				if (int.TryParse(ApiString.Substring(ApiString.LastIndexOf('-') + 1), out Version))
				{
					VersionInt = Version;
				}
			}
			return VersionInt;
		}

		private string CachedSDKLevel = null;
		private string GetSdkApiLevel(AndroidToolChain ToolChain, bool bGradleEnabled = false)
		{
			if (CachedSDKLevel == null)
			{
				// ask the .ini system for what version to use
				ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
				string SDKLevel;
				Ini.GetString("/Script/AndroidPlatformEditor.AndroidSDKSettings", "SDKAPILevel", out SDKLevel);

				// check for project override of SDK API level
				string ProjectSDKLevel;
				Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "SDKAPILevelOverride", out ProjectSDKLevel);
				ProjectSDKLevel = ProjectSDKLevel.Trim();
				if (ProjectSDKLevel != "")
				{
					SDKLevel = ProjectSDKLevel;
				}

				// if we want to use whatever version the ndk uses, then use that
				if (SDKLevel == "matchndk")
				{
					SDKLevel = ToolChain.GetNdkApiLevel();
				}

				string PlatformsDir = Environment.ExpandEnvironmentVariables("%ANDROID_HOME%/platforms");

				// run a command and capture output
				if (SDKLevel == "latest")
				{
					SDKLevel = GetLatestSDKApiLevel(ToolChain, PlatformsDir);
				}

				// make sure it is at least android-23
				int SDKLevelInt = GetApiLevelInt(SDKLevel);
				if (SDKLevelInt < MinimumSDKLevel)
				{
					Log.TraceInformation("Requires at least SDK API level {0}, currently set to '{1}'", MinimumSDKLevel, SDKLevel);
					SDKLevel = GetLatestSDKApiLevel(ToolChain, PlatformsDir);

					SDKLevelInt = GetApiLevelInt(SDKLevel);
					if (SDKLevelInt < MinimumSDKLevel)
					{
						if (bGradleEnabled)
						{
							SDKLevelInt = MinimumSDKLevel;
							SDKLevel = "android-" + MinimumSDKLevel.ToString();
							Log.TraceInformation("Gradle will attempt to download SDK API level {0}", SDKLevelInt);
						}
						else
						{
							throw new BuildException("Can't make an APK without SDK API 'android-" + MinimumSDKLevel.ToString() + "' minimum installed");
						}
					}
				}

				// validate the platform SDK is installed
				if (!ValidateSDK(PlatformsDir, SDKLevel))
				{
					if (bGradleEnabled)
					{
						Log.TraceWarning("The SDK API requested '{0}' not installed in {1}; Gradle will attempt to download it.", SDKLevel, PlatformsDir);
					}
					else
					{
						throw new BuildException("The SDK API requested '{0}' not installed in {1}", SDKLevel, PlatformsDir);
					}
				}

				Log.TraceInformation("Building Java with SDK API level '{0}'", SDKLevel);
				CachedSDKLevel = SDKLevel;
			}

			return CachedSDKLevel;
		}

		private string CachedBuildToolsVersion = null;
		private string LastAndroidHomePath = null;

		private uint GetRevisionValue(string VersionString)
		{
			// read up to 4 sections (ie. 20.0.3.5), first section most significant
			// each section assumed to be 0 to 255 range
			uint Value = 0;
			try
			{
				string[] Sections = VersionString.Split(".".ToCharArray());
				Value |= (Sections.Length > 0) ? (uint.Parse(Sections[0]) << 24) : 0;
				Value |= (Sections.Length > 1) ? (uint.Parse(Sections[1]) << 16) : 0;
				Value |= (Sections.Length > 2) ? (uint.Parse(Sections[2]) << 8) : 0;
				Value |= (Sections.Length > 3) ? uint.Parse(Sections[3]) : 0;
			}
			catch (Exception)
			{
				// ignore poorly formed version
			}
			return Value;
		}

		private string GetBuildToolsVersion(bool bGradle)
		{
			// return cached path if ANDROID_HOME has not changed
			string HomePath = Environment.ExpandEnvironmentVariables("%ANDROID_HOME%");
			if (CachedBuildToolsVersion != null && LastAndroidHomePath == HomePath)
			{
				return CachedBuildToolsVersion;
			}

			// get a list of the directories in build-tools.. may be more than one set installed (or none which is bad)
			string[] Subdirs = Directory.GetDirectories(Path.Combine(HomePath, "build-tools"));
			if (Subdirs.Length == 0)
			{
				throw new BuildException("Failed to find %ANDROID_HOME%/build-tools subdirectory. Run SDK manager and install build-tools.");
			}

			// valid directories will have a source.properties with the Pkg.Revision (there is no guarantee we can use the directory name as revision)
			string BestVersionString = null;
			uint BestVersion = 0;
			foreach (string CandidateDir in Subdirs)
			{
				string AaptFilename = Path.Combine(CandidateDir, Utils.IsRunningOnMono ? "aapt" : "aapt.exe");
				string RevisionString = "";
				uint RevisionValue = 0;

				if (File.Exists(AaptFilename))
				{
					string SourcePropFilename = Path.Combine(CandidateDir, "source.properties");
					if (File.Exists(SourcePropFilename))
					{
						string[] PropertyContents = File.ReadAllLines(SourcePropFilename);
						foreach (string PropertyLine in PropertyContents)
						{
							if (PropertyLine.StartsWith("Pkg.Revision="))
							{
								RevisionString = PropertyLine.Substring(13);
								RevisionValue = GetRevisionValue(RevisionString);
								break;
							}
						}
					}
				}

				// remember it if newer version or haven't found one yet
				if (RevisionValue > BestVersion || BestVersionString == null)
				{
					BestVersion = RevisionValue;
					BestVersionString = RevisionString;
				}
			}

			if (BestVersionString == null)
			{
				throw new BuildException("Failed to find %ANDROID_HOME%/build-tools subdirectory with aapt. Run SDK manager and install build-tools.");
			}

			// with Gradle enabled use at least 24.0.2 (will be installed by Gradle if missing)
			if (bGradle && (BestVersion < ((24 << 24) | (0 << 16) | (2 << 8))))
			{
				BestVersionString = "24.0.2";
			}

			CachedBuildToolsVersion = BestVersionString;
			LastAndroidHomePath = HomePath;

			Log.TraceInformation("Building with Build Tools version '{0}'", CachedBuildToolsVersion);

			return CachedBuildToolsVersion;
		}

		public static string GetOBBVersionNumber(int PackageVersion)
		{
			string VersionString = PackageVersion.ToString("0");
			return VersionString;
		}

		public bool GetPackageDataInsideApk()
		{
			return bPackageDataInsideApk;
		}

		/// <summary>
		/// Reads the bPackageDataInsideApk from AndroidRuntimeSettings
		/// </summary>
		/// <param name="Ini"></param>
		protected bool ReadPackageDataInsideApkFromIni(ConfigHierarchy Ini)
		{		
			// make a new one if one wasn't passed in
			if (Ini == null)
			{
				Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			}

			// we check this a lot, so make it easy 
			bool bIniPackageDataInsideApk;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bPackageDataInsideApk", out bIniPackageDataInsideApk);

			return bIniPackageDataInsideApk;
		}

		public bool UseExternalFilesDir(bool bDisallowExternalFilesDir, ConfigHierarchy Ini = null)
		{
			if (bDisallowExternalFilesDir)
			{
				return false;
			}

			// make a new one if one wasn't passed in
			if (Ini == null)
			{
				Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			}

			// we check this a lot, so make it easy 
			bool bUseExternalFilesDir;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bUseExternalFilesDir", out bUseExternalFilesDir);

			return bUseExternalFilesDir;
		}

		public bool IsPackagingForDaydream(ConfigHierarchy Ini = null)
		{
			// always false if the GoogleVR plugin wasn't enabled
			if (!GoogleVRPluginEnabled)
			{
				return false;
			}

			// make a new one if one wasn't passed in
			if (Ini == null)
			{
				Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			}

			List<string> GoogleVRCaps = new List<string>();
			if(Ini.GetArray("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "GoogleVRCaps", out GoogleVRCaps))
			{
				return GoogleVRCaps.Contains("Daydream33") || GoogleVRCaps.Contains("Daydream63") || GoogleVRCaps.Contains("Daydream66");
			}
			else
			{
				// the default values for the VRCaps are Cardboard and Daydream33, so unless the
				// developer changes the mode, there will be no setting string to look up here
				return true;
			}
		}

		public List<string> GetTargetOculusMobileDevices(ConfigHierarchy Ini = null)
		{
			// always false if the Oculus Mobile plugin wasn't enabled
			if (!OculusMobilePluginEnabled)
			{
				return new List<string>();
			}

			// make a new one if one wasn't passed in
			if (Ini == null)
			{
				Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			}

			List<string> OculusMobileDevices;
			bool result = Ini.GetArray("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "PackageForOculusMobile", out OculusMobileDevices);
			if (!result || OculusMobileDevices == null)
			{
				OculusMobileDevices = new List<string>();
			}

			// Handle bPackageForGearVR for backwards compatibility
			bool bPackageForGearVR = false;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bPackageForGearVR", out bPackageForGearVR);
			if (bPackageForGearVR && !OculusMobileDevices.Contains("GearGo"))
			{
				OculusMobileDevices.Add("GearGo");
			}

			return OculusMobileDevices;
		}

		public bool IsPackagingForOculusMobile(ConfigHierarchy Ini = null)
		{
			List<string> TargetOculusDevices = GetTargetOculusMobileDevices(Ini);
			bool bTargetOculusDevices = (TargetOculusDevices != null && TargetOculusDevices.Count() > 0);

			return bTargetOculusDevices;
		}

		public bool DisableVerifyOBBOnStartUp(ConfigHierarchy Ini = null)
		{
			// make a new one if one wasn't passed in
			if (Ini == null)
			{
				Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			}

			// we check this a lot, so make it easy 
			bool bDisableVerifyOBBOnStartUp;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bDisableVerifyOBBOnStartUp", out bDisableVerifyOBBOnStartUp);

			return bDisableVerifyOBBOnStartUp;
		}

		private static string GetAntPath()
		{
			// look up an ANT_HOME environment variable
			string AntHome = Environment.GetEnvironmentVariable("ANT_HOME");
			if (!string.IsNullOrEmpty(AntHome) && Directory.Exists(AntHome))
			{
				string AntPath = AntHome + "/bin/ant" + (Utils.IsRunningOnMono ? "" : ".bat");
				// use it if found
				if (File.Exists(AntPath))
				{
					return AntPath;
				}
			}

			// otherwise, look in the eclipse install for the ant plugin (matches the unzipped Android ADT from Google)
			string PluginDir = Environment.ExpandEnvironmentVariables("%ANDROID_HOME%/../eclipse/plugins");
			if (Directory.Exists(PluginDir))
			{
				string[] Plugins = Directory.GetDirectories(PluginDir, "org.apache.ant*");
				// use the first one with ant.bat
				if (Plugins.Length > 0)
				{
					foreach (string PluginName in Plugins)
					{
						string AntPath = PluginName + "/bin/ant" + (Utils.IsRunningOnMono ? "" : ".bat");
						// use it if found
						if (File.Exists(AntPath))
						{
							return AntPath;
						}
					}
				}
			}

			throw new BuildException("Unable to find ant.bat (via %ANT_HOME% or %ANDROID_HOME%/../eclipse/plugins/org.apache.ant*");
		}

		private static bool SafeDeleteFile(string Filename, bool bCheckExists = true)
		{
			if (!bCheckExists || File.Exists(Filename))
			{
				try
				{
					File.SetAttributes(Filename, FileAttributes.Normal);
					File.Delete(Filename);
					return true;
				}
				catch (System.UnauthorizedAccessException)
				{
					throw new BuildException("File '{0}' is in use; unable to modify it.", Filename);
				}
				catch (System.Exception)
				{
					return false;
				}
			}
			return true;
		}

		private static void CopyFileDirectory(string SourceDir, string DestDir, Dictionary<string, string> Replacements = null, string[] Excludes = null)
		{
			if (!Directory.Exists(SourceDir))
			{
				return;
			}

			string[] Files = Directory.GetFiles(SourceDir, "*.*", SearchOption.AllDirectories);
			foreach (string Filename in Files)
			{
				if (Excludes != null)
				{
					// skip files in excluded directories
					string DirectoryName = Path.GetFileName(Path.GetDirectoryName(Filename));
					bool bExclude = false;
					foreach (string Exclude in Excludes)
					{
						if (DirectoryName == Exclude)
						{
							bExclude = true;
							break;
						}
					}
					if (bExclude)
					{
						continue;
					}
				}

				// skip template files
				if (Path.GetExtension(Filename) == ".template")
				{
					continue;
				}

				// make the dst filename with the same structure as it was in SourceDir
				string DestFilename = Path.Combine(DestDir, Utils.MakePathRelativeTo(Filename, SourceDir)).Replace('\\', Path.DirectorySeparatorChar).Replace('/', Path.DirectorySeparatorChar); 

				// make the subdirectory if needed
				string DestSubdir = Path.GetDirectoryName(DestFilename);
				if (!Directory.Exists(DestSubdir))
				{
					Directory.CreateDirectory(DestSubdir);
				}

				// some files are handled specially
				string Ext = Path.GetExtension(Filename);
				if (Ext == ".xml" && Replacements != null)
				{
					string Contents = File.ReadAllText(Filename);

					// replace some variables
					foreach (KeyValuePair<string, string> Pair in Replacements)
					{
						Contents = Contents.Replace(Pair.Key, Pair.Value);
					}

					bool bWriteFile = true;
					if (File.Exists(DestFilename))
					{
						string OriginalContents = File.ReadAllText(DestFilename);
						if (Contents == OriginalContents)
						{
							bWriteFile = false;
						}
					}

					// write out file if different
					if (bWriteFile)
					{
						SafeDeleteFile(DestFilename);
						File.WriteAllText(DestFilename, Contents);
					}
				}
				else
				{
					SafeDeleteFile(DestFilename);
					File.Copy(Filename, DestFilename);

					// preserve timestamp and clear read-only flags
					FileInfo DestFileInfo = new FileInfo(DestFilename);
					DestFileInfo.Attributes = DestFileInfo.Attributes & ~FileAttributes.ReadOnly;
					File.SetLastWriteTimeUtc(DestFilename, File.GetLastWriteTimeUtc(Filename));
				}
			}
		}

		private static void DeleteDirectory(string InPath, string SubDirectoryToKeep = "")
		{
			// skip the dir we want to
			if (String.Compare(Path.GetFileName(InPath), SubDirectoryToKeep, true) == 0)
			{
				return;
			}

			// delete all files in here
			string[] Files;
			try
			{
				Files = Directory.GetFiles(InPath);
			}
			catch (Exception)
			{
				// directory doesn't exist so all is good
				return;
			}
			foreach (string Filename in Files)
			{
				try
				{
					// remove any read only flags
					FileInfo FileInfo = new FileInfo(Filename);
					FileInfo.Attributes = FileInfo.Attributes & ~FileAttributes.ReadOnly;
					FileInfo.Delete();
				}
				catch (Exception)
				{
					Log.TraceInformation("Failed to delete all files in directory {0}. Continuing on...", InPath);
				}
			}

			string[] Dirs = Directory.GetDirectories(InPath, "*.*", SearchOption.TopDirectoryOnly);
			foreach (string Dir in Dirs)
			{
				DeleteDirectory(Dir, SubDirectoryToKeep);
				// try to delete the directory, but allow it to fail (due to SubDirectoryToKeep still existing)
				try
				{
					Directory.Delete(Dir);
				}
				catch (Exception)
				{
					// do nothing
				}
			}
		}

		private void CleanCopyDirectory(string SourceDir, string DestDir, string[] Excludes = null)
		{
			if (!Directory.Exists(SourceDir))
			{
				return;
			}
			if (!Directory.Exists(DestDir))
			{
				CopyFileDirectory(SourceDir, DestDir, null, Excludes);
				return;
			}

			// copy files that are different and make a list of ones to keep
			string[] StartingSourceFiles = Directory.GetFiles(SourceDir, "*.*", SearchOption.AllDirectories);
			List<string> FilesToKeep = new List<string>();
			foreach (string Filename in StartingSourceFiles)
			{
				if (Excludes != null)
				{
					// skip files in excluded directories
					string DirectoryName = Path.GetFileName(Path.GetDirectoryName(Filename));
					bool bExclude = false;
					foreach (string Exclude in Excludes)
					{
						if (DirectoryName == Exclude)
						{
							bExclude = true;
							break;
						}
					}
					if (bExclude)
					{
						continue;
					}
				}

				// make the dest filename with the same structure as it was in SourceDir
				string DestFilename = Path.Combine(DestDir, Utils.MakePathRelativeTo(Filename, SourceDir));

				// remember this file to keep
				FilesToKeep.Add(DestFilename);

				// only copy files that are new or different
				if (FilesAreDifferent(Filename, DestFilename))
				{
					if (File.Exists(DestFilename))
					{
						// xml files may have been rewritten but contents still the same so check contents also
						string Ext = Path.GetExtension(Filename);
						if (Ext == ".xml")
						{
							if (File.ReadAllText(Filename) == File.ReadAllText(DestFilename))
							{
								continue;
							}
						}

						// delete it so can copy over it
						SafeDeleteFile(DestFilename);
					}

					// make the subdirectory if needed
					string DestSubdir = Path.GetDirectoryName(DestFilename);
					if (!Directory.Exists(DestSubdir))
					{
						Directory.CreateDirectory(DestSubdir);
					}

					// copy it
					File.Copy(Filename, DestFilename);

					// preserve timestamp and clear read-only flags
					FileInfo DestFileInfo = new FileInfo(DestFilename);
					DestFileInfo.Attributes = DestFileInfo.Attributes & ~FileAttributes.ReadOnly;
					File.SetLastWriteTimeUtc(DestFilename, File.GetLastWriteTimeUtc(Filename));

					Log.TraceInformation("Copied file {0}.", DestFilename);
				}
			}

			// delete any files not in the keep list
			string[] StartingDestFiles = Directory.GetFiles(DestDir, "*.*", SearchOption.AllDirectories);
			foreach (string Filename in StartingDestFiles)
			{
				if (!FilesToKeep.Contains(Filename))
				{
					Log.TraceInformation("Deleting unneeded file {0}.", Filename);
					SafeDeleteFile(Filename);
				}
			}

			// delete any empty directories
			try
			{
				IEnumerable<string> BaseDirectories = Directory.EnumerateDirectories(DestDir, "*", SearchOption.AllDirectories).OrderByDescending(x => x);
				foreach (string directory in BaseDirectories)
				{
					if (Directory.Exists(directory) && Directory.GetFiles(directory, "*.*", SearchOption.AllDirectories).Count() == 0)
					{
						Log.TraceInformation("Cleaning Directory {0} as empty.", directory);
						Directory.Delete(directory, true);
					}
				}
			}
			catch (Exception)
			{
				// likely System.IO.DirectoryNotFoundException, ignore it
			}
		}

		public string GetUE4BuildFilePath(String EngineDirectory)
		{
			return Path.GetFullPath(Path.Combine(EngineDirectory, "Build/Android/Java"));
		}

		public string GetUE4JavaSrcPath()
		{
			return Path.Combine("src", "com", "epicgames", "ue4");
		}

		public string GetUE4JavaFilePath(String EngineDirectory)
		{
			return Path.GetFullPath(Path.Combine(GetUE4BuildFilePath(EngineDirectory), GetUE4JavaSrcPath()));
		}

		public string GetUE4JavaBuildSettingsFileName(String EngineDirectory)
		{
			return Path.Combine(GetUE4JavaFilePath(EngineDirectory), "JavaBuildSettings.java");
		}

		public string GetUE4JavaDownloadShimFileName(string Directory)
		{
			return Path.Combine(Directory, "DownloadShim.java");
		}

		public string GetUE4TemplateJavaSourceDir(string Directory)
		{
			return Path.Combine(GetUE4BuildFilePath(Directory), "JavaTemplates");
		}

		public string GetUE4TemplateJavaDestination(string Directory, string FileName)
		{
			return Path.Combine(Directory, FileName);
		}

		public string GetUE4JavaOBBDataFileName(string Directory)
		{
			return Path.Combine(Directory, "OBBData.java");
		}

		public class TemplateFile
		{
			public string SourceFile;
			public string DestinationFile;
		}

		private void MakeDirectoryIfRequired(string DestFilename)
		{
			string DestSubdir = Path.GetDirectoryName(DestFilename);
			if (!Directory.Exists(DestSubdir))
			{
				Directory.CreateDirectory(DestSubdir);
			}
		}

		private int CachedStoreVersion = -1;
		private int CachedStoreVersionOffsetArmV7 = 0;
		private int CachedStoreVersionOffsetArm64 = 0;
		private int CachedStoreVersionOffsetX8664= 0;

		public int GetStoreVersion(string UE4Arch)
		{
			if (CachedStoreVersion < 1)
			{
				ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
				int StoreVersion = 1;
				Ini.GetInt32("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "StoreVersion", out StoreVersion);

				bool bUseChangeListAsStoreVersion = false;
				Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bUseChangeListAsStoreVersion", out bUseChangeListAsStoreVersion);

				bool IsBuildMachine = Environment.GetEnvironmentVariable("IsBuildMachine") == "1";
				// override store version with changelist if enabled and is build machine
				if (bUseChangeListAsStoreVersion && IsBuildMachine)
				{
					// make sure changelist is cached
					string EngineVersion = ReadEngineVersion();
					
					int Changelist = 0;
					if (int.TryParse(EngineChangelist, out Changelist))
					{
						if (Changelist != 0)
						{
							StoreVersion = Changelist;
						}
					}
				}

				Log.TraceInformation("GotStoreVersion found v{0}. (bUseChangeListAsStoreVersion={1} IsBuildMachine={2} EngineChangeList={3})", StoreVersion, bUseChangeListAsStoreVersion, IsBuildMachine, EngineChangelist);

				CachedStoreVersion = StoreVersion;

				Ini.GetInt32("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "StoreVersionOffsetArmV7", out CachedStoreVersionOffsetArmV7);
				Ini.GetInt32("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "StoreVersionOffsetArm64", out CachedStoreVersionOffsetArm64);
				Ini.GetInt32("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "StoreVersionOffsetX8664", out CachedStoreVersionOffsetX8664);
			}

			switch (UE4Arch)
			{
				case "-armv7": return CachedStoreVersion + CachedStoreVersionOffsetArmV7;
				case "-arm64": return CachedStoreVersion + CachedStoreVersionOffsetArm64;
				case "-x64": return CachedStoreVersion + CachedStoreVersionOffsetX8664;
			}

			return CachedStoreVersion;
		}

		private string CachedVersionDisplayName;

		public string GetVersionDisplayName(bool bIsEmbedded)
		{
			if (string.IsNullOrEmpty(CachedVersionDisplayName))
			{
				ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
				string VersionDisplayName = "";
				Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "VersionDisplayName", out VersionDisplayName);

				if (Environment.GetEnvironmentVariable("IsBuildMachine") == "1")
				{
					bool bAppendChangeListToVersionDisplayName = false;
					Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bAppendChangeListToVersionDisplayName", out bAppendChangeListToVersionDisplayName);
					if (bAppendChangeListToVersionDisplayName)
					{
						VersionDisplayName = string.Format("{0}-{1}", VersionDisplayName, EngineChangelist);
					}

					bool bAppendPlatformToVersionDisplayName = false;
					Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bAppendPlatformToVersionDisplayName", out bAppendPlatformToVersionDisplayName);
					if (bAppendPlatformToVersionDisplayName)
					{
						VersionDisplayName = string.Format("{0}-Android", VersionDisplayName);
					}

					// append optional text to version name if embedded build
					if (bIsEmbedded)
					{
						string EmbeddedAppendDisplayName = "";
						if (Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "EmbeddedAppendDisplayName", out EmbeddedAppendDisplayName))
						{
							VersionDisplayName = VersionDisplayName + EmbeddedAppendDisplayName;
						}
					}
				}

				CachedVersionDisplayName = VersionDisplayName;
			}

			return CachedVersionDisplayName;
		}

		public void WriteJavaOBBDataFile(string FileName, string PackageName, List<string> ObbSources, string CookFlavor, bool bPackageDataInsideApk, string UE4Arch)
		{
			Log.TraceInformation("\n==== Writing to OBB data file {0} ====", FileName);

			// always must write if file does not exist
			bool bFileExists = File.Exists(FileName);
			bool bMustWriteFile = !bFileExists;

			string AppType = "";
			if (CookFlavor.EndsWith("Client"))
			{
//				AppType = ".Client";		// should always be empty now; fix up the name in batch file instead
			}

			int StoreVersion = GetStoreVersion(UE4Arch);

			StringBuilder obbData = new StringBuilder("package " + PackageName + ";\n\n");
			obbData.Append("public class OBBData\n{\n");
			obbData.Append("public static final String AppType = \"" + AppType + "\";\n\n");
			obbData.Append("public static class XAPKFile {\npublic final boolean mIsMain;\npublic final String mFileVersion;\n");
			obbData.Append("public final long mFileSize;\nXAPKFile(boolean isMain, String fileVersion, long fileSize) {\nmIsMain = isMain;\nmFileVersion = fileVersion;\nmFileSize = fileSize;\n");
			obbData.Append("}\n}\n\n");

			// write the data here
			obbData.Append("public static final XAPKFile[] xAPKS = {\n");
			// For each obb file... but we only have one... for now anyway.
			bool first = ObbSources.Count > 1;
			bool AnyOBBExists = false;
			foreach (string ObbSource in ObbSources)
			{
				bool bOBBExists = File.Exists(ObbSource);
				AnyOBBExists |= bOBBExists;

				obbData.Append("new XAPKFile(\n" + (ObbSource.Contains(".patch.") ? "false, // false signifies a patch file\n" : "true, // true signifies a main file\n"));
				obbData.AppendFormat("\"{0}\", // the version of the APK that the file was uploaded against\n", GetOBBVersionNumber(StoreVersion));
				obbData.AppendFormat("{0}L // the length of the file in bytes\n", bOBBExists ? new FileInfo(ObbSource).Length : 0);
				obbData.AppendFormat("){0}\n", first ? "," : "");
				first = false;
			}
			obbData.Append("};\n"); // close off data
			obbData.Append("};\n"); // close class definition off

			// see if we need to replace the file if it exists
			if (!bMustWriteFile && bFileExists)
			{
				string[] obbDataFile = File.ReadAllLines(FileName);

				// Must always write if AppType not defined
				bool bHasAppType = false;
				foreach (string FileLine in obbDataFile)
				{
					if (FileLine.Contains("AppType ="))
					{
						bHasAppType = true;
						break;
					}
				}
				if (!bHasAppType)
				{
					bMustWriteFile = true;
				}

				// OBB must exist, contents must be different, and not packaging in APK to require replacing
				if (!bMustWriteFile && AnyOBBExists && !bPackageDataInsideApk && !obbDataFile.SequenceEqual((obbData.ToString()).Split('\n')))
				{
					bMustWriteFile = true;
				}
			}

			if (bMustWriteFile)
			{
				MakeDirectoryIfRequired(FileName);
				using (StreamWriter outputFile = new StreamWriter(FileName, false))
				{
					string[] obbSrc = obbData.ToString().Split('\n');
					foreach (string line in obbSrc)
					{
						outputFile.WriteLine(line);
					}
				}
			}
			else
			{
				Log.TraceInformation("\n==== OBB data file up to date so not writing. ====");
			}
		}

		public void WriteJavaDownloadSupportFiles(string ShimFileName, IEnumerable<TemplateFile> TemplateFiles, Dictionary<string, string> replacements)
		{
			// Deal with the Shim first as that is a known target and is easy to deal with
			// If it exists then read it
			string[] DestFileContent = File.Exists(ShimFileName) ? File.ReadAllLines(ShimFileName) : null;

			StringBuilder ShimFileContent = new StringBuilder("package com.epicgames.ue4;\n\n");

			ShimFileContent.AppendFormat("import {0}.OBBDownloaderService;\n", replacements["$$PackageName$$"]);
			ShimFileContent.AppendFormat("import {0}.DownloaderActivity;\n", replacements["$$PackageName$$"]);

			// Do OBB file checking without using DownloadActivity to avoid transit to another activity
				ShimFileContent.Append("import android.app.Activity;\n");
				ShimFileContent.Append("import com.google.android.vending.expansion.downloader.Helpers;\n");
				ShimFileContent.AppendFormat("import {0}.OBBData;\n", replacements["$$PackageName$$"]);

			ShimFileContent.Append("\n\npublic class DownloadShim\n{\n");
			ShimFileContent.Append("\tpublic static OBBDownloaderService DownloaderService;\n");
			ShimFileContent.Append("\tpublic static DownloaderActivity DownloadActivity;\n");
			ShimFileContent.Append("\tpublic static Class<DownloaderActivity> GetDownloaderType() { return DownloaderActivity.class; }\n");

			// Do OBB file checking without using DownloadActivity to avoid transit to another activity
			ShimFileContent.Append("\tpublic static boolean expansionFilesDelivered(Activity activity, int version) {\n");
			ShimFileContent.Append("\t\tfor (OBBData.XAPKFile xf : OBBData.xAPKS) {\n");
			ShimFileContent.Append("\t\t\tString fileName = Helpers.getExpansionAPKFileName(activity, xf.mIsMain, Integer.toString(version), OBBData.AppType);\n");
			ShimFileContent.Append("\t\t\tGameActivity.Log.debug(\"Checking for file : \" + fileName);\n");
			ShimFileContent.Append("\t\t\tString fileForNewFile = Helpers.generateSaveFileName(activity, fileName);\n");
			ShimFileContent.Append("\t\t\tString fileForDevFile = Helpers.generateSaveFileNameDevelopment(activity, fileName);\n");
			ShimFileContent.Append("\t\t\tGameActivity.Log.debug(\"which is really being resolved to : \" + fileForNewFile + \"\\n Or : \" + fileForDevFile);\n");
			ShimFileContent.Append("\t\t\tif (Helpers.doesFileExist(activity, fileName, xf.mFileSize, false)) {\n");
			ShimFileContent.Append("\t\t\t\tGameActivity.Log.debug(\"Found OBB here: \" + fileForNewFile);\n");
			ShimFileContent.Append("\t\t\t}\n");
			ShimFileContent.Append("\t\t\telse if (Helpers.doesFileExistDev(activity, fileName, xf.mFileSize, false)) {\n");
			ShimFileContent.Append("\t\t\t\tGameActivity.Log.debug(\"Found OBB here: \" + fileForDevFile);\n");
				ShimFileContent.Append("\t\t\t}\n");
			ShimFileContent.Append("\t\t\telse return false;\n");
			ShimFileContent.Append("\t\t}\n");
				ShimFileContent.Append("\t\treturn true;\n");
				ShimFileContent.Append("\t}\n");

			ShimFileContent.Append("}\n");
			Log.TraceInformation("\n==== Writing to shim file {0} ====", ShimFileName);

			// If they aren't the same then dump out the settings
			if (DestFileContent == null || !DestFileContent.SequenceEqual((ShimFileContent.ToString()).Split('\n')))
			{
				MakeDirectoryIfRequired(ShimFileName);
				using (StreamWriter outputFile = new StreamWriter(ShimFileName, false))
				{
					string[] shimSrc = ShimFileContent.ToString().Split('\n');
					foreach (string line in shimSrc)
					{
						outputFile.WriteLine(line);
					}
				}
			}
			else
			{
				Log.TraceInformation("\n==== Shim data file up to date so not writing. ====");
			}

			// Now we move on to the template files
			foreach (TemplateFile template in TemplateFiles)
			{
				string[] templateSrc = File.ReadAllLines(template.SourceFile);
				string[] templateDest = File.Exists(template.DestinationFile) ? File.ReadAllLines(template.DestinationFile) : null;

				for (int i = 0; i < templateSrc.Length; ++i)
				{
					string srcLine = templateSrc[i];
					bool changed = false;
					foreach (KeyValuePair<string, string> kvp in replacements)
					{
						if (srcLine.Contains(kvp.Key))
						{
							srcLine = srcLine.Replace(kvp.Key, kvp.Value);
							changed = true;
						}
					}
					if (changed)
					{
						templateSrc[i] = srcLine;
					}
				}

				Log.TraceInformation("\n==== Writing to template target file {0} ====", template.DestinationFile);

				if (templateDest == null || templateSrc.Length != templateDest.Length || !templateSrc.SequenceEqual(templateDest))
				{
					MakeDirectoryIfRequired(template.DestinationFile);
					using (StreamWriter outputFile = new StreamWriter(template.DestinationFile, false))
					{
						foreach (string line in templateSrc)
						{
							outputFile.WriteLine(line);
						}
					}
				}
				else
				{
					Log.TraceInformation("\n==== Template target file up to date so not writing. ====");
				}
			}
		}

		public void WriteCrashlyticsResources(string UEBuildPath, string PackageName, string ApplicationDisplayName, bool bIsEmbedded, string UE4Arch)
		{
			System.DateTime CurrentDateTime = System.DateTime.Now;
			string BuildID = Guid.NewGuid().ToString();

			string VersionDisplayName = GetVersionDisplayName(bIsEmbedded);

			StringBuilder CrashPropertiesContent = new StringBuilder("");
			CrashPropertiesContent.Append("# This file is automatically generated by Crashlytics to uniquely\n");
			CrashPropertiesContent.Append("# identify individual builds of your Android application.\n");
			CrashPropertiesContent.Append("#\n");
			CrashPropertiesContent.Append("# Do NOT modify, delete, or commit to source control!\n");
			CrashPropertiesContent.Append("#\n");
			CrashPropertiesContent.Append("# " + CurrentDateTime.ToString("D") + "\n");
			CrashPropertiesContent.Append("version_name=" + VersionDisplayName + "\n");
			CrashPropertiesContent.Append("package_name=" + PackageName + "\n");
			CrashPropertiesContent.Append("build_id=" + BuildID + "\n");
			CrashPropertiesContent.Append("version_code=" + GetStoreVersion(UE4Arch).ToString() + "\n");

			string CrashPropertiesFileName = Path.Combine(UEBuildPath, "assets", "crashlytics-build.properties");
			MakeDirectoryIfRequired(CrashPropertiesFileName);
			File.WriteAllText(CrashPropertiesFileName, CrashPropertiesContent.ToString());
			Log.TraceInformation("==== Write {0}  ====", CrashPropertiesFileName);

			StringBuilder BuildIDContent = new StringBuilder("");
			BuildIDContent.Append("<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"no\"?>\n");
			BuildIDContent.Append("<resources xmlns:tools=\"http://schemas.android.com/tools\">\n");
			BuildIDContent.Append("<!--\n");
			BuildIDContent.Append("  This file is automatically generated by Crashlytics to uniquely\n");
			BuildIDContent.Append("  identify individual builds of your Android application.\n");
			BuildIDContent.Append("\n");
			BuildIDContent.Append("  Do NOT modify, delete, or commit to source control!\n");
			BuildIDContent.Append("-->\n");
			BuildIDContent.Append("<string tools:ignore=\"UnusedResources, TypographyDashes\" name=\"com.crashlytics.android.build_id\" translatable=\"false\">" + BuildID + "</string>\n");
			BuildIDContent.Append("</resources>\n");

			string BuildIDFileName = Path.Combine(UEBuildPath, "res", "values", "com_crashlytics_build_id.xml");
			MakeDirectoryIfRequired(BuildIDFileName);
			File.WriteAllText(BuildIDFileName, BuildIDContent.ToString());
			Log.TraceInformation("==== Write {0}  ====", BuildIDFileName);
		}

		private static string GetNDKArch(string UE4Arch)
		{
			switch (UE4Arch)
			{
				case "-armv7":	return "armeabi-v7a";
				case "-arm64":  return "arm64-v8a";
				case "-x64":	return "x86_64";
				case "-x86":	return "x86";

				default: throw new BuildException("Unknown UE4 architecture {0}", UE4Arch);
			}
		}

		public static string GetUE4Arch(string NDKArch)
		{
			switch (NDKArch)
			{
				case "armeabi-v7a": return "-armv7";
				case "arm64-v8a":   return "-arm64";
				case "x86":         return "-x86";
				case "arm64":       return "-arm64";
				case "x86_64":
				case "x64":			return "-x64";
					
	//				default: throw new BuildException("Unknown NDK architecture '{0}'", NDKArch);
				// future-proof by returning armv7 for unknown
				default:            return "-armv7";
			}
		}

		private static void StripDebugSymbols(string SourceFileName, string TargetFileName, string UE4Arch, bool bStripAll = false)
		{
			// Copy the file and remove read-only if necessary
			File.Copy(SourceFileName, TargetFileName, true);
			FileAttributes Attribs = File.GetAttributes(TargetFileName);
			if (Attribs.HasFlag(FileAttributes.ReadOnly))
			{
				File.SetAttributes(TargetFileName, Attribs & ~FileAttributes.ReadOnly);
			}

			ProcessStartInfo StartInfo = new ProcessStartInfo();
			StartInfo.FileName = AndroidToolChain.GetStripExecutablePath(UE4Arch).Trim('"');
			if (bStripAll)
			{
				StartInfo.Arguments = "--strip-unneeded \"" + TargetFileName + "\"";
			}
			else
			{
				StartInfo.Arguments = "--strip-debug \"" + TargetFileName + "\"";
			}
			StartInfo.UseShellExecute = false;
			StartInfo.CreateNoWindow = true;
			Utils.RunLocalProcessAndLogOutput(StartInfo);
		}

		private static void CopySTL(AndroidToolChain ToolChain, string UE4BuildPath, string UE4Arch, string NDKArch, bool bForDistribution, bool bGradleEnabled)
		{
			string GccVersion = "4.6";
			if (Directory.Exists(Environment.ExpandEnvironmentVariables("%NDKROOT%/sources/cxx-stl/gnu-libstdc++/4.9")))
			{
				GccVersion = "4.9";
			}
			else if (Directory.Exists(Environment.ExpandEnvironmentVariables("%NDKROOT%/sources/cxx-stl/gnu-libstdc++/4.8")))
			{
				GccVersion = "4.8";
			}

			if (bGradleEnabled)
			{
				// copy it in!
				string SourceSTLSOName = Environment.ExpandEnvironmentVariables("%NDKROOT%/sources/cxx-stl/gnu-libstdc++/") + GccVersion + "/libs/" + NDKArch + "/libgnustl_shared.so";
				string FinalSTLSOName = UE4BuildPath + "/jni/" + NDKArch + "/libgnustl_shared.so";

				// remove from location for Ant if previously copied
				string WrongSTLSOName = UE4BuildPath + "/libs/" + NDKArch + "/libgnustl_shared.so";
				SafeDeleteFile(WrongSTLSOName);

				// check to see if libgnustl_shared.so is newer than last time we copied
				bool bFileExists = File.Exists(FinalSTLSOName);
				TimeSpan Diff = File.GetLastWriteTimeUtc(FinalSTLSOName) - File.GetLastWriteTimeUtc(SourceSTLSOName);
				if (!bFileExists || Diff.TotalSeconds < -1 || Diff.TotalSeconds > 1)
				{
					SafeDeleteFile(FinalSTLSOName);
					Directory.CreateDirectory(Path.GetDirectoryName(FinalSTLSOName));
					File.Copy(SourceSTLSOName, FinalSTLSOName, true);
				}
			}
			else
			{
				// copy it in!
				string SourceSTLSOName = Environment.ExpandEnvironmentVariables("%NDKROOT%/sources/cxx-stl/gnu-libstdc++/") + GccVersion + "/libs/" + NDKArch + "/libgnustl_shared.so";
				string FinalSTLSOName = UE4BuildPath + "/libs/" + NDKArch + "/libgnustl_shared.so";

				// remove from location for Gradle if previously copied
				string WrongSTLSOName = UE4BuildPath + "/jni/" + NDKArch + "/libgnustl_shared.so";
				SafeDeleteFile(WrongSTLSOName);

				// check to see if libgnustl_shared.so is newer than last time we copied (or needs stripping for distribution)
				bool bFileExists = File.Exists(FinalSTLSOName);
				TimeSpan Diff = File.GetLastWriteTimeUtc(FinalSTLSOName) - File.GetLastWriteTimeUtc(SourceSTLSOName);
				if (bForDistribution || !bFileExists || Diff.TotalSeconds < -1 || Diff.TotalSeconds > 1)
				{
					SafeDeleteFile(FinalSTLSOName);
					Directory.CreateDirectory(Path.GetDirectoryName(FinalSTLSOName));
					if (bForDistribution)
					{
						// Strip debug symbols for distribution builds
						StripDebugSymbols(SourceSTLSOName, FinalSTLSOName, UE4Arch, true);
					}
					else
					{
						File.Copy(SourceSTLSOName, FinalSTLSOName, true);
					}
				}
			}
		}

		private void CopyGfxDebugger(string UE4BuildPath, string UE4Arch, string NDKArch)
		{
			string AndroidGraphicsDebugger;
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "AndroidGraphicsDebugger", out AndroidGraphicsDebugger);

			switch (AndroidGraphicsDebugger.ToLower())
			{
				case "mali":
					{
						string MaliGraphicsDebuggerPath;
						AndroidPlatformSDK.GetPath(Ini, "/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "MaliGraphicsDebuggerPath", out MaliGraphicsDebuggerPath);
						if (Directory.Exists(MaliGraphicsDebuggerPath))
						{
							Directory.CreateDirectory(Path.Combine(UE4BuildPath, "libs", NDKArch));
							string MaliLibSrcPath = Path.Combine(MaliGraphicsDebuggerPath, "target", "android-non-root", "arm", NDKArch, "libMGD.so");
							if (!File.Exists(MaliLibSrcPath))
							{
								// in v4.3.0 library location was changed
								MaliLibSrcPath = Path.Combine(MaliGraphicsDebuggerPath, "target", "android", "arm", "unrooted", NDKArch, "libMGD.so");
							}
							string MaliLibDstPath = Path.Combine(UE4BuildPath, "libs", NDKArch, "libMGD.so");

							Log.TraceInformation("Copying {0} to {1}", MaliLibSrcPath, MaliLibDstPath);
							File.Copy(MaliLibSrcPath, MaliLibDstPath, true); 
						}
					}
					break;

				// @TODO: Add NVIDIA Gfx Debugger
				/*
				case "nvidia":
					{
						Directory.CreateDirectory(UE4BuildPath + "/libs/" + NDKArch);
						File.Copy("F:/NVPACK/android-kk-egl-t124-a32/Stripped_libNvPmApi.Core.so", UE4BuildPath + "/libs/" + NDKArch + "/libNvPmApi.Core.so", true);
						File.Copy("F:/NVPACK/android-kk-egl-t124-a32/Stripped_libNvidia_gfx_debugger.so", UE4BuildPath + "/libs/" + NDKArch + "/libNvidia_gfx_debugger.so", true);
					}
					break;
				*/
				default:
					break;
			}
		}

		void LogBuildSetup()
		{
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			bool bBuildForES2 = false;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bBuildForES2", out bBuildForES2);
			bool bBuildForES31 = false;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bBuildForES31", out bBuildForES31);
			bool bSupportsVulkan = false;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bSupportsVulkan", out bSupportsVulkan);

			Log.TraceInformation("bBuildForES2: {0}", (bBuildForES2 ? "true" : "false"));
			Log.TraceInformation("bBuildForES31: {0}", (bBuildForES31 ? "true" : "false"));
			Log.TraceInformation("bSupportsVulkan: {0}", (bSupportsVulkan ? "true" : "false"));
		}
		
		void CopyVulkanValidationLayers(string UE4BuildPath, string UE4Arch, string NDKArch, string Configuration)
		{
			bool bSupportsVulkan = false;
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bSupportsVulkan", out bSupportsVulkan);

			bool bCopyVulkanLayers = bSupportsVulkan && (Configuration == "Debug" || Configuration == "Development");
			if (bCopyVulkanLayers)
			{
				string VulkanLayersDir = Environment.ExpandEnvironmentVariables("%NDKROOT%/sources/third_party/vulkan/src/build-android/jniLibs/") + NDKArch;
				if (Directory.Exists(VulkanLayersDir))
				{
					Log.TraceInformation("Copying vulkan layers from {0}", VulkanLayersDir);
					string DestDir = Path.Combine(UE4BuildPath, "libs", NDKArch);
					Directory.CreateDirectory(DestDir);
					CopyFileDirectory(VulkanLayersDir, DestDir);
				}
			}
		}

		private static int RunCommandLineProgramAndReturnResult(string WorkingDirectory, string Command, string Params, string OverrideDesc = null, bool bUseShellExecute = false)
		{
			if (OverrideDesc == null)
			{
				Log.TraceInformation("\nRunning: " + Command + " " + Params);
			}
			else if (OverrideDesc != "")
			{
				Log.TraceInformation(OverrideDesc);
				Log.TraceVerbose("\nRunning: " + Command + " " + Params);
			}

			ProcessStartInfo StartInfo = new ProcessStartInfo();
			StartInfo.WorkingDirectory = WorkingDirectory;
			StartInfo.FileName = Command;
			StartInfo.Arguments = Params;
			StartInfo.UseShellExecute = bUseShellExecute;
			StartInfo.WindowStyle = ProcessWindowStyle.Minimized;

			Process Proc = new Process();
			Proc.StartInfo = StartInfo;
			Proc.Start();
			Proc.WaitForExit();

			return Proc.ExitCode;
		}

		private static void RunCommandLineProgramWithException(string WorkingDirectory, string Command, string Params, string OverrideDesc = null, bool bUseShellExecute = false)
		{
			if (OverrideDesc == null)
			{
				Log.TraceInformation("\nRunning: " + Command + " " + Params);
			}
			else if (OverrideDesc != "")
			{
				Log.TraceInformation(OverrideDesc);
				Log.TraceVerbose("\nRunning: " + Command + " " + Params);
			}

			ProcessStartInfo StartInfo = new ProcessStartInfo();
			StartInfo.WorkingDirectory = WorkingDirectory;
			StartInfo.FileName = Command;
			StartInfo.Arguments = Params;
			StartInfo.UseShellExecute = bUseShellExecute;
			StartInfo.WindowStyle = ProcessWindowStyle.Minimized;

			Process Proc = new Process();
			Proc.StartInfo = StartInfo;
			Proc.Start();
			Proc.WaitForExit();

			// android bat failure
			if (Proc.ExitCode != 0)
			{
				throw new BuildException("{0} failed with args {1}", Command, Params);
			}
		}

		static void FilterStdOutErr(object sender, DataReceivedEventArgs e)
		{
			if (e.Data != null)
			{
				// apply filtering of the warnings we want to ignore
				if (e.Data.Contains("WARNING: The option 'android.enableD8' is deprecated and should not be used anymore."))
				{
					Log.TraceInformation("{0}", e.Data.Replace("WARNING: ", ">> "));
					return;
				}
				if (e.Data.Contains("WARNING: The specified Android SDK Build Tools version"))
				{
					Log.TraceInformation("{0}", e.Data.Replace("WARNING: ", ">> "));
					return;
				}
				if (e.Data.Contains("Warning: Resigning with jarsigner."))
				{
					Log.TraceInformation("{0}", e.Data.Replace("Warning: ", ">> "));
					return;
				}
				if (e.Data.Contains("Unable to strip library"))
				{
					Log.TraceInformation("{0}", e.Data.Replace("due to error", ""));
					return;
				}
				if (e.Data.Contains("To suppress this warning,"))
				{
					Log.TraceInformation("{0}", e.Data.Replace(" warning,", ","));
				}
				Log.TraceInformation("{0}", e.Data);
			}
		}

		private static void RunCommandLineProgramWithExceptionAndFiltering(string WorkingDirectory, string Command, string Params, string OverrideDesc = null, bool bUseShellExecute = false)
		{
			if (OverrideDesc == null)
			{
				Log.TraceInformation("\nRunning: " + Command + " " + Params);
			}
			else if (OverrideDesc != "")
			{
				Log.TraceInformation(OverrideDesc);
				Log.TraceVerbose("\nRunning: " + Command + " " + Params);
			}

			ProcessStartInfo StartInfo = new ProcessStartInfo();
			StartInfo.WorkingDirectory = WorkingDirectory;
			StartInfo.FileName = Command;
			StartInfo.Arguments = Params;
			StartInfo.UseShellExecute = bUseShellExecute;
			StartInfo.WindowStyle = ProcessWindowStyle.Minimized;
			StartInfo.RedirectStandardOutput = true;
			StartInfo.RedirectStandardError = true;

			Process Proc = new Process();
			Proc.StartInfo = StartInfo;
			Proc.OutputDataReceived += FilterStdOutErr;
			Proc.ErrorDataReceived += FilterStdOutErr;
			Proc.Start();
			Proc.BeginOutputReadLine();
			Proc.BeginErrorReadLine();
			Proc.WaitForExit();

			// android bat failure
			if (Proc.ExitCode != 0)
			{
				throw new BuildException("{0} failed with args {1}", Command, Params);
			}
		}

		private bool CheckApplicationName(string UE4BuildPath, string ProjectName, out string ApplicationDisplayName)
		{
			string StringsXMLPath = Path.Combine(UE4BuildPath, "res/values/strings.xml");

			ApplicationDisplayName = null;
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "ApplicationDisplayName", out ApplicationDisplayName);

			// use project name if display name is left blank
			if (String.IsNullOrWhiteSpace(ApplicationDisplayName))
			{
				ApplicationDisplayName = ProjectName;
			}

			// replace escaped characters (note: changes &# pattern before &, then patches back to allow escaped character codes in the string)
			ApplicationDisplayName = ApplicationDisplayName.Replace("&#", "$@#$").Replace("&", "&amp;").Replace("'", "\\'").Replace("\"", "\\\"").Replace("<", "&lt;").Replace(">", "&gt;").Replace("$@#$", "&#");

			// if it doesn't exist, need to repackage
			if (!File.Exists(StringsXMLPath))
			{
				return true;
			}

			// read it and see if needs to be updated
			string Contents = File.ReadAllText(StringsXMLPath);

			// find the key
			string AppNameTag = "<string name=\"app_name\">";
			int KeyIndex = Contents.IndexOf(AppNameTag);

			// if doesn't exist, need to repackage
			if (KeyIndex < 0)
			{
				return true;
			}

			// get the current value
			KeyIndex += AppNameTag.Length;
			int TagEnd = Contents.IndexOf("</string>", KeyIndex);
			if (TagEnd < 0)
			{
				return true;
			}
			string CurrentApplicationName = Contents.Substring(KeyIndex, TagEnd - KeyIndex);

			// no need to do anything if matches
			if (CurrentApplicationName == ApplicationDisplayName)
			{
				// name matches, no need to force a repackage
				return false;
			}

			// need to repackage
			return true;
		}

		private void UpdateProjectProperties(AndroidToolChain ToolChain, string UE4BuildPath, string ProjectName)
		{
			Log.TraceInformation("\n===={0}====UPDATING BUILD CONFIGURATION FILES====================================================", DateTime.Now.ToString());

			// get all of the libs (from engine + project)
			string JavaLibsDir = Path.Combine(UE4BuildPath, "JavaLibs");
			string[] LibDirs = Directory.GetDirectories(JavaLibsDir);

			// get existing project.properties lines (if any)
			string ProjectPropertiesFile = Path.Combine(UE4BuildPath, "project.properties");
			string[] PropertiesLines = new string[] { };
			if (File.Exists(ProjectPropertiesFile))
			{
				PropertiesLines = File.ReadAllLines(ProjectPropertiesFile);
			}

			// figure out how many libraries were already listed (if there were more than this listed, then we need to start the file over, because we need to unreference a library)
			int NumOutstandingAlreadyReferencedLibs = 0;
			foreach (string Line in PropertiesLines)
			{
				if (Line.StartsWith("android.library.reference."))
				{
					NumOutstandingAlreadyReferencedLibs++;
				}
			}

			// now go through each one and verify they are listed in project properties, and if not, add them
			List<string> LibsToBeAdded = new List<string>();
			foreach (string LibDir in LibDirs)
			{
				// put it in terms of the subdirectory that would be in the project.properties
				string RelativePath = "JavaLibs/" + Path.GetFileName(LibDir);

				// now look for this in the existing file
				bool bWasReferencedAlready = false;
				foreach (string Line in PropertiesLines)
				{
					if (Line.StartsWith("android.library.reference.") && Line.EndsWith(RelativePath))
					{
						// this lib was already referenced, don't need to readd
						bWasReferencedAlready = true;
						break;
					}
				}

				if (bWasReferencedAlready)
				{
					// if it was, no further action needed, and count it off
					NumOutstandingAlreadyReferencedLibs--;
				}
				else
				{
					// otherwise, we need to add it to the project properties
					LibsToBeAdded.Add(RelativePath);
				}
			}

			// now at this point, if there are any outstanding already referenced libs, we have too many, so we have to start over
			if (NumOutstandingAlreadyReferencedLibs > 0)
			{
				// @todo android: If a user had a project.properties in the game, NEVER do this
				Log.TraceInformation("There were too many libs already referenced in project.properties, tossing it");
				SafeDeleteFile(ProjectPropertiesFile);

				LibsToBeAdded.Clear();
				foreach (string LibDir in LibDirs)
				{
					// put it in terms of the subdirectory that would be in the project.properties
					LibsToBeAdded.Add("JavaLibs/" + Path.GetFileName(LibDir));
				}
			}

			// now update the project for each library
			string AndroidCommandPath = Environment.ExpandEnvironmentVariables("%ANDROID_HOME%/tools/android" + (Utils.IsRunningOnMono ? "" : ".bat"));
			string UpdateCommandLine = "--silent update project --subprojects --name " + ProjectName + " --path . --target " + GetSdkApiLevel(ToolChain);
			foreach (string Lib in LibsToBeAdded)
			{
				string LocalUpdateCommandLine = UpdateCommandLine + " --library " + Lib;

				// make sure each library has a build.xml - --subprojects doesn't create build.xml files, but it will create project.properties
				// and later code needs each lib to have a build.xml
				RunCommandLineProgramWithException(UE4BuildPath, AndroidCommandPath, "--silent update lib-project --path " + Lib + " --target " + GetSdkApiLevel(ToolChain), "");
				RunCommandLineProgramWithException(UE4BuildPath, AndroidCommandPath, LocalUpdateCommandLine, "Updating project.properties, local.properties, and build.xml for " + Path.GetFileName(Lib) + "...");
			}

		}


		private string GetAllBuildSettings(AndroidToolChain ToolChain, string BuildPath, bool bForDistribution, bool bMakeSeparateApks, bool bPackageDataInsideApk, bool bDisableVerifyOBBOnStartUp, bool bUseExternalFilesDir, bool bGradleEnabled, string TemplatesHashCode)
		{
			// make the settings string - this will be char by char compared against last time
			StringBuilder CurrentSettings = new StringBuilder();
			CurrentSettings.AppendLine(string.Format("NDKROOT={0}", Environment.GetEnvironmentVariable("NDKROOT")));
			CurrentSettings.AppendLine(string.Format("ANDROID_HOME={0}", Environment.GetEnvironmentVariable("ANDROID_HOME")));
			CurrentSettings.AppendLine(string.Format("ANT_HOME={0}", Environment.GetEnvironmentVariable("ANT_HOME")));
			CurrentSettings.AppendLine(string.Format("JAVA_HOME={0}", Environment.GetEnvironmentVariable("JAVA_HOME")));
			CurrentSettings.AppendLine(string.Format("NDKVersion={0}", ToolChain.GetNdkApiLevel()));
			CurrentSettings.AppendLine(string.Format("SDKVersion={0}", GetSdkApiLevel(ToolChain, bGradleEnabled)));
			CurrentSettings.AppendLine(string.Format("bForDistribution={0}", bForDistribution));
			CurrentSettings.AppendLine(string.Format("bMakeSeparateApks={0}", bMakeSeparateApks));
			CurrentSettings.AppendLine(string.Format("bPackageDataInsideApk={0}", bPackageDataInsideApk));
			CurrentSettings.AppendLine(string.Format("bDisableVerifyOBBOnStartUp={0}", bDisableVerifyOBBOnStartUp));
			CurrentSettings.AppendLine(string.Format("bUseExternalFilesDir={0}", bUseExternalFilesDir));
			CurrentSettings.AppendLine(string.Format("UPLHashCode={0}", UPLHashCode));
			CurrentSettings.AppendLine(string.Format("TemplatesHashCode={0}", TemplatesHashCode));

			// all AndroidRuntimeSettings ini settings in here
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			ConfigHierarchySection Section = Ini.FindSection("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings");
			if (Section != null)
			{
				foreach (string Key in Section.KeyNames)
				{
					// filter out NDK and SDK override since actual resolved versions already written above
					if (Key.Equals("SDKAPILevelOverride") || Key.Equals("NDKAPILevelOverride"))
					{
						continue;
					}

					IReadOnlyList<string> Values;
					Section.TryGetValues(Key, out Values);

					foreach (string Value in Values)
					{
						CurrentSettings.AppendLine(string.Format("{0}={1}", Key, Value));
					}
				}
			}

			Section = Ini.FindSection("/Script/AndroidPlatformEditor.AndroidSDKSettings");
			if (Section != null)
			{
				foreach (string Key in Section.KeyNames)
				{
					// filter out NDK and SDK levels since actual resolved versions already written above
					if (Key.Equals("SDKAPILevel") || Key.Equals("NDKAPILevel"))
					{
						continue;
					}

					IReadOnlyList<string> Values;
					Section.TryGetValues(Key, out Values);
					foreach (string Value in Values)
					{
						CurrentSettings.AppendLine(string.Format("{0}={1}", Key, Value));
					}
				}
			}

			List<string> Arches = ToolChain.GetAllArchitectures();
			foreach (string Arch in Arches)
			{
				CurrentSettings.AppendFormat("Arch={0}{1}", Arch, Environment.NewLine);
			}

			List<string> GPUArchitectures = ToolChain.GetAllGPUArchitectures();
			foreach (string GPUArch in GPUArchitectures)
			{
				CurrentSettings.AppendFormat("GPUArch={0}{1}", GPUArch, Environment.NewLine);
			}

			return CurrentSettings.ToString();
		}

		private bool CheckDependencies(AndroidToolChain ToolChain, string ProjectName, string ProjectDirectory, string UE4BuildFilesPath, string GameBuildFilesPath, string EngineDirectory, List<string> SettingsFiles,
			string CookFlavor, string OutputPath, string UE4BuildPath, bool bMakeSeparateApks, bool bPackageDataInsideApk)
		{
			List<string> Arches = ToolChain.GetAllArchitectures();
			List<string> GPUArchitectures = ToolChain.GetAllGPUArchitectures();

			// check all input files (.so, java files, .ini files, etc)
			bool bAllInputsCurrent = true;
			foreach (string Arch in Arches)
			{
				foreach (string GPUArch in GPUArchitectures)
				{
					string SourceSOName = AndroidToolChain.InlineArchName(OutputPath, Arch, GPUArch);
					// if the source binary was UE4Game, replace it with the new project name, when re-packaging a binary only build
					string ApkFilename = Path.GetFileNameWithoutExtension(OutputPath).Replace("UE4Game", ProjectName);
					string DestApkName = Path.Combine(ProjectDirectory, "Binaries/Android/") + ApkFilename + ".apk";

					// if we making multiple Apks, we need to put the architecture into the name
					if (bMakeSeparateApks)
					{
						DestApkName = AndroidToolChain.InlineArchName(DestApkName, Arch, GPUArch);
					}

					// check to see if it's out of date before trying the slow make apk process (look at .so and all Engine and Project build files to be safe)
					List<String> InputFiles = new List<string>();
					InputFiles.Add(SourceSOName);
					InputFiles.AddRange(Directory.EnumerateFiles(UE4BuildFilesPath, "*.*", SearchOption.AllDirectories));
					if (Directory.Exists(GameBuildFilesPath))
					{
						InputFiles.AddRange(Directory.EnumerateFiles(GameBuildFilesPath, "*.*", SearchOption.AllDirectories));
					}

					// make sure changed java files will rebuild apk
					InputFiles.AddRange(SettingsFiles);

					// rebuild if .pak files exist for OBB in APK case
					if (bPackageDataInsideApk)
					{
						string PAKFileLocation = ProjectDirectory + "/Saved/StagedBuilds/Android" + CookFlavor + "/" + ProjectName + "/Content/Paks";
						if (Directory.Exists(PAKFileLocation))
						{
							IEnumerable<string> PakFiles = Directory.EnumerateFiles(PAKFileLocation, "*.pak", SearchOption.TopDirectoryOnly);
							foreach (string Name in PakFiles)
							{
								InputFiles.Add(Name);
							}
						}
					}

					// look for any newer input file
					DateTime ApkTime = File.GetLastWriteTimeUtc(DestApkName);
					foreach (string InputFileName in InputFiles)
					{
						if (File.Exists(InputFileName))
						{
							// skip .log files
							if (Path.GetExtension(InputFileName) == ".log")
							{
								continue;
							}
							DateTime InputFileTime = File.GetLastWriteTimeUtc(InputFileName);
							if (InputFileTime.CompareTo(ApkTime) > 0)
							{
								bAllInputsCurrent = false;
								Log.TraceInformation("{0} is out of date due to newer input file {1}", DestApkName, InputFileName);
								break;
							}
						}
					}
				}
			}

			return bAllInputsCurrent;
		}

		private int ConvertDepthBufferIniValue(string IniValue)
		{
			switch (IniValue.ToLower())
			{
				case "bits16":
					return 16;
				case "bits24":
					return 24;
				case "bits32":
					return 32;
				default:
					return 0;
			}
		}

		private string ConvertOrientationIniValue(string IniValue)
		{
			switch (IniValue.ToLower())
			{
				case "portrait":
					return "portrait";
				case "reverseportrait":
					return "reversePortrait";
				case "sensorportrait":
					return "sensorPortrait";
				case "landscape":
					return "landscape";
				case "reverselandscape":
					return "reverseLandscape";
				case "sensorlandscape":
					return "sensorLandscape";
				case "sensor":
					return "sensor";
				case "fullsensor":
					return "fullSensor";
				default:
					return "landscape";
			}
		}

		private string GetOrientation(string NDKArch)
		{
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			string Orientation;
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "Orientation", out Orientation);

			// check for UPL override
			string OrientationOverride = UPL.ProcessPluginNode(NDKArch, "orientationOverride", "");
			if (!String.IsNullOrEmpty(OrientationOverride))
			{
				Orientation = OrientationOverride;
			}

			return ConvertOrientationIniValue(Orientation);
		}

		private void DetermineScreenOrientationRequirements(string Arch, out bool bNeedPortrait, out bool bNeedLandscape)
		{
			bNeedLandscape = false;
			bNeedPortrait = false;

			switch (GetOrientation(Arch).ToLower())
			{
				case "portrait":
					bNeedPortrait = true;
					break;
				case "reverseportrait":
					bNeedPortrait = true;
					break;
				case "sensorportrait":
					bNeedPortrait = true;
					break;

				case "landscape":
					bNeedLandscape = true;
					break;
				case "reverselandscape":
					bNeedLandscape = true;
					break;
				case "sensorlandscape":
					bNeedLandscape = true;
					break;

				case "sensor":
					bNeedPortrait = true;
					bNeedLandscape = true;
					break;
				case "fullsensor":
					bNeedPortrait = true;
					bNeedLandscape = true;
					break;

				default:
					bNeedPortrait = true;
					bNeedLandscape = true;
					break;
			}
		}

		private void PickDownloaderScreenOrientation(string UE4BuildPath, bool bNeedPortrait, bool bNeedLandscape)
		{
			// Remove unused downloader_progress.xml to prevent missing resource
			if (!bNeedPortrait)
			{
				string LayoutPath = UE4BuildPath + "/res/layout-port/downloader_progress.xml";
				SafeDeleteFile(LayoutPath);
			}
			if (!bNeedLandscape)
			{
				string LayoutPath = UE4BuildPath + "/res/layout-land/downloader_progress.xml";
				SafeDeleteFile(LayoutPath);
			}

			// Loop through each of the resolutions (only /res/drawable/ is required, others are optional)
			string[] Resolutions = new string[] { "/res/drawable/", "/res/drawable-ldpi/", "/res/drawable-mdpi/", "/res/drawable-hdpi/", "/res/drawable-xhdpi/" };
			foreach (string ResolutionPath in Resolutions)
			{
				string PortraitFilename = UE4BuildPath + ResolutionPath + "downloadimagev.png";
				if (bNeedPortrait)
				{
					if (!File.Exists(PortraitFilename) && (ResolutionPath == "/res/drawable/"))
					{
						Log.TraceWarning("Warning: Downloader screen source image {0} not available, downloader screen will not function properly!", PortraitFilename);
					}
				}
				else
				{
					// Remove unused image
					SafeDeleteFile(PortraitFilename);
				}

				string LandscapeFilename = UE4BuildPath + ResolutionPath + "downloadimageh.png";
				if (bNeedLandscape)
				{
					if (!File.Exists(LandscapeFilename) && (ResolutionPath == "/res/drawable/"))
					{
						Log.TraceWarning("Warning: Downloader screen source image {0} not available, downloader screen will not function properly!", LandscapeFilename);
					}
				}
				else
				{
					// Remove unused image
					SafeDeleteFile(LandscapeFilename);
				}
			}
		}
	
		private void PackageForDaydream(string UE4BuildPath)
		{
			bool bPackageForDaydream = IsPackagingForDaydream();

			if (!bPackageForDaydream)
			{
				// If this isn't a Daydream App, we need to make sure to remove
				// Daydream specific assets.

				// Remove the Daydream app  tile background.
				string AppTileBackgroundPath = UE4BuildPath + "/res/drawable-nodpi/vr_icon_background.png";
				SafeDeleteFile(AppTileBackgroundPath);

				// Remove the Daydream app tile icon.
				string AppTileIconPath = UE4BuildPath + "/res/drawable-nodpi/vr_icon.png";
				SafeDeleteFile(AppTileIconPath);
			}
		}

		private void PickSplashScreenOrientation(string UE4BuildPath, bool bNeedPortrait, bool bNeedLandscape)
		{
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			bool bShowLaunchImage = false;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bShowLaunchImage", out bShowLaunchImage);
			bool bPackageForOculusMobile = IsPackagingForOculusMobile(Ini); ;
			bool bPackageForDaydream = IsPackagingForDaydream(Ini);
			
			//override the parameters if we are not showing a launch image or are packaging for Oculus Mobile and Daydream
			if (bPackageForOculusMobile || bPackageForDaydream || !bShowLaunchImage)
			{
				bNeedPortrait = bNeedLandscape = false;
			}

			// Remove unused styles.xml to prevent missing resource
			if (!bNeedPortrait)
			{
				string StylesPath = UE4BuildPath + "/res/values-port/styles.xml";
				SafeDeleteFile(StylesPath);
			}
			if (!bNeedLandscape)
			{
				string StylesPath = UE4BuildPath + "/res/values-land/styles.xml";
				SafeDeleteFile(StylesPath);
			}

			// Loop through each of the resolutions (only /res/drawable/ is required, others are optional)
			string[] Resolutions = new string[] { "/res/drawable/", "/res/drawable-ldpi/", "/res/drawable-mdpi/", "/res/drawable-hdpi/", "/res/drawable-xhdpi/" };
			foreach (string ResolutionPath in Resolutions)
			{
				string PortraitFilename = UE4BuildPath + ResolutionPath + "splashscreen_portrait.png";
				if (bNeedPortrait)
				{
					if (!File.Exists(PortraitFilename) && (ResolutionPath == "/res/drawable/"))
					{
						Log.TraceWarning("Warning: Splash screen source image {0} not available, splash screen will not function properly!", PortraitFilename);
					}
				}
				else
				{
					// Remove unused image
					SafeDeleteFile(PortraitFilename);

					// Remove optional extended resource
					string PortraitXmlFilename = UE4BuildPath + ResolutionPath + "splashscreen_p.xml";
					SafeDeleteFile(PortraitXmlFilename);
				}

				string LandscapeFilename = UE4BuildPath + ResolutionPath + "splashscreen_landscape.png";
				if (bNeedLandscape)
				{
					if (!File.Exists(LandscapeFilename) && (ResolutionPath == "/res/drawable/"))
					{
						Log.TraceWarning("Warning: Splash screen source image {0} not available, splash screen will not function properly!", LandscapeFilename);
					}
				}
				else
				{
					// Remove unused image
					SafeDeleteFile(LandscapeFilename);

					// Remove optional extended resource
					string LandscapeXmlFilename = UE4BuildPath + ResolutionPath + "splashscreen_l.xml";
					SafeDeleteFile(LandscapeXmlFilename);
				}
			}
		}

		private string CachedPackageName = null;

		private bool IsLetter(char Input)
		{
			return (Input >= 'A' && Input <= 'Z') || (Input >= 'a' && Input <= 'z');
		}

		private bool IsDigit(char Input)
		{
			return (Input >= '0' && Input <= '9');
		}

		private string GetPackageName(string ProjectName)
		{
			if (CachedPackageName == null)
			{
				ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
				string PackageName;
				Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "PackageName", out PackageName);

				if (PackageName.Contains("[PROJECT]"))
				{
					// project name must start with a letter
					if (!IsLetter(ProjectName[0]))
					{
						throw new BuildException("Package name segments must all start with a letter. Please replace [PROJECT] with a valid name");
					}

					// hyphens not allowed so change them to underscores in project name
					if (ProjectName.Contains("-"))
					{
						Trace.TraceWarning("Project name contained hyphens, converted to underscore");
						ProjectName = ProjectName.Replace("-", "_");
					}

					// check for special characters
					for (int Index = 0; Index < ProjectName.Length; Index++)
					{
						char c = ProjectName[Index];
						if (c != '.' && c != '_' && !IsDigit(c) && !IsLetter(c))
						{
							throw new BuildException("Project name contains illegal characters (only letters, numbers, and underscore allowed); please replace [PROJECT] with a valid name");
						}
					}

					PackageName = PackageName.Replace("[PROJECT]", ProjectName);
				}

				// verify minimum number of segments
				string[] PackageParts = PackageName.Split('.');
				int SectionCount = PackageParts.Length;
				if (SectionCount < 2)
				{
					throw new BuildException("Package name must have at least 2 segments separated by periods (ex. com.projectname, not projectname); please change in Android Project Settings. Currently set to '" + PackageName + "'");
				}

				// hyphens not allowed
				if (PackageName.Contains("-"))
				{
					throw new BuildException("Package names may not contain hyphens; please change in Android Project Settings. Currently set to '" + PackageName + "'");
				}

				// do not allow special characters
				for (int Index = 0; Index < PackageName.Length; Index++)
				{
					char c = PackageName[Index];
					if (c != '.' && c != '_' && !IsDigit(c) && !IsLetter(c))
					{
						throw new BuildException("Package name contains illegal characters (only letters, numbers, and underscore allowed); please change in Android Project Settings. Currently set to '" + PackageName + "'");
					}
				}

				// validate each segment
				for (int Index = 0; Index < SectionCount; Index++)
				{
					if (PackageParts[Index].Length < 1)
					{
						throw new BuildException("Package name segments must have at least one letter; please change in Android Project Settings. Currently set to '" + PackageName + "'");
					}

					if (!IsLetter(PackageParts[Index][0]))
					{
						throw new BuildException("Package name segments must start with a letter; please change in Android Project Settings. Currently set to '" + PackageName + "'");
					}

					// cannot use Java reserved keywords
					foreach (string Keyword in JavaReservedKeywords)
					{
						if (PackageParts[Index] == Keyword)
						{
							throw new BuildException("Package name segments must not be a Java reserved keyword (" + Keyword + "); please change in Android Project Settings. Currently set to '" + PackageName + "'");
						}
					}
				}

				Log.TraceInformation("Using package name: '{0}'", PackageName);
				CachedPackageName = PackageName;
			}

			return CachedPackageName;
		}

		private string GetPublicKey()
		{
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			string PlayLicenseKey = "";
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "GooglePlayLicenseKey", out PlayLicenseKey);
			return PlayLicenseKey;
		}

		private bool bHaveReadEngineVersion = false;
		private string EngineMajorVersion = "4";
		private string EngineMinorVersion = "0";
		private string EnginePatchVersion = "0";
		private string EngineChangelist = "0";
		private string EngineBranch = "UE4";

		private string ReadEngineVersion()
		{
			if (!bHaveReadEngineVersion)
			{
				ReadOnlyBuildVersion Version = ReadOnlyBuildVersion.Current;

				EngineMajorVersion = Version.MajorVersion.ToString();
				EngineMinorVersion = Version.MinorVersion.ToString();
				EnginePatchVersion = Version.PatchVersion.ToString();
				EngineChangelist = Version.Changelist.ToString();
				EngineBranch = Version.BranchName;

				bHaveReadEngineVersion = true;
			}

			return EngineMajorVersion + "." + EngineMinorVersion + "." + EnginePatchVersion;
		}


		private string GenerateManifest(AndroidToolChain ToolChain, string ProjectName, TargetType InTargetType, string EngineDirectory, bool bIsForDistribution, bool bPackageDataInsideApk, string GameBuildFilesPath, bool bHasOBBFiles, bool bDisableVerifyOBBOnStartUp, string UE4Arch, string GPUArch, string CookFlavor, bool bUseExternalFilesDir, string Configuration, int SDKLevelInt, bool bIsEmbedded)
		{
			// Read the engine version
			string EngineVersion = ReadEngineVersion();

			int StoreVersion = GetStoreVersion(UE4Arch);

			string Arch = GetNDKArch(UE4Arch);
			int NDKLevelInt = 0;
			int MinSDKVersion = 0;
			int TargetSDKVersion = 0;
			GetMinTargetSDKVersions(ToolChain, UE4Arch, UPL, Arch, out MinSDKVersion, out TargetSDKVersion, out NDKLevelInt);

			// get project version from ini
			ConfigHierarchy GameIni = GetConfigCacheIni(ConfigHierarchyType.Game);
			string ProjectVersion;
			GameIni.GetString("/Script/EngineSettings.GeneralProjectSettings", "ProjectVersion", out ProjectVersion);

			// ini file to get settings from
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			string PackageName = GetPackageName(ProjectName);
			string VersionDisplayName = GetVersionDisplayName(bIsEmbedded);
			bool bEnableGooglePlaySupport;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bEnableGooglePlaySupport", out bEnableGooglePlaySupport);
			bool bUseGetAccounts;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bUseGetAccounts", out bUseGetAccounts);
			string DepthBufferPreference;
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "DepthBufferPreference", out DepthBufferPreference);
			float MaxAspectRatioValue;
			if (!Ini.TryGetValue("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "MaxAspectRatio", out MaxAspectRatioValue))
			{
				MaxAspectRatioValue = 2.1f;
			}
			string Orientation = ConvertOrientationIniValue(GetOrientation(Arch));
			bool EnableFullScreen;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bFullScreen", out EnableFullScreen);
			bool bUseDisplayCutout;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bUseDisplayCutout", out bUseDisplayCutout);
			List<string> ExtraManifestNodeTags;
			Ini.GetArray("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "ExtraManifestNodeTags", out ExtraManifestNodeTags);
			List<string> ExtraApplicationNodeTags;
			Ini.GetArray("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "ExtraApplicationNodeTags", out ExtraApplicationNodeTags);
			List<string> ExtraActivityNodeTags;
			Ini.GetArray("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "ExtraActivityNodeTags", out ExtraActivityNodeTags);
			string ExtraActivitySettings;
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "ExtraActivitySettings", out ExtraActivitySettings);
			string ExtraApplicationSettings;
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "ExtraApplicationSettings", out ExtraApplicationSettings);
			List<string> ExtraPermissions;
			Ini.GetArray("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "ExtraPermissions", out ExtraPermissions);
			bool bPackageForOculusMobile = IsPackagingForOculusMobile(Ini);
			bool bEnableIAP = false;
			Ini.GetBool("OnlineSubsystemGooglePlay.Store", "bSupportsInAppPurchasing", out bEnableIAP);
			bool bShowLaunchImage = false;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bShowLaunchImage", out bShowLaunchImage);
			string AndroidGraphicsDebugger;
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "AndroidGraphicsDebugger", out AndroidGraphicsDebugger);
			bool bSupportAdMob = true;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bSupportAdMob", out bSupportAdMob);
			bool bValidateTextureFormats;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bValidateTextureFormats", out bValidateTextureFormats);
			bool bUseNEONForArmV7 = false;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bUseNEONForArmV7", out bUseNEONForArmV7);
			bool bBuildForES2 = false;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bBuildForES2", out bBuildForES2);
			bool bBuildForES31 = false;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bBuildForES31", out bBuildForES31);
			bool bSupportsVulkan = false;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bSupportsVulkan", out bSupportsVulkan);

			bool bAllowIMU = true;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bAllowIMU", out bAllowIMU);
			if (IsPackagingForDaydream(Ini) && bAllowIMU)
			{
				Log.TraceInformation("Daydream and IMU both enabled, recommend disabling IMU if not needed.");
			}

			string InstallLocation;
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "InstallLocation", out InstallLocation);
			switch (InstallLocation.ToLower())
			{
				case "preferexternal":
					InstallLocation = "preferExternal";
					break;
				case "auto":
					InstallLocation = "auto";
					break;
				default:
					InstallLocation = "internalOnly";
					break;
			}

			// only apply density to configChanges if using android-24 or higher and minimum sdk is 17
			bool bAddDensity = (SDKLevelInt >= 24) && (MinSDKVersion >= 17);

			// disable Oculus Mobile if not supported platform (in this case only armv7 for now)
			if (UE4Arch != "-armv7" && UE4Arch != "-arm64")
			{
				if (bPackageForOculusMobile)
				{
					Log.TraceInformation("Disabling Package For Oculus Mobile for unsupported architecture {0}", UE4Arch);
					bPackageForOculusMobile = false;
				}
			}

			// disable splash screen for Oculus Mobile (for now)
			if (bPackageForOculusMobile)
			{
				if (bShowLaunchImage)
				{
					Log.TraceInformation("Disabling Show Launch Image for Oculus Mobile enabled application");
					bShowLaunchImage = false;
				}
			}

			bool bPackageForDaydream = IsPackagingForDaydream(Ini);
			// disable splash screen for daydream
			if (bPackageForDaydream)
			{
				if (bShowLaunchImage)
				{
					Log.TraceInformation("Disabling Show Launch Image for Daydream enabled application");
					bShowLaunchImage = false;
				}
			}

			//figure out the app type
			string AppType = InTargetType == TargetType.Game ? "" : InTargetType.ToString();
			if (CookFlavor.EndsWith("Client"))
			{
				CookFlavor = CookFlavor.Substring(0, CookFlavor.Length - 6);
			}
			if (CookFlavor.EndsWith("Server"))
			{
				CookFlavor = CookFlavor.Substring(0, CookFlavor.Length - 6);
			}

			//figure out which texture compressions are supported
			bool bETC1Enabled, bETC1aEnabled, bETC2Enabled, bDXTEnabled, bATCEnabled, bPVRTCEnabled, bASTCEnabled;
			bETC1Enabled = bETC1aEnabled = bETC2Enabled = bDXTEnabled = bATCEnabled = bPVRTCEnabled = bASTCEnabled = false;
			if (CookFlavor.Length < 1)
			{
				//All values supported
				bETC1Enabled = bETC2Enabled = bDXTEnabled = bATCEnabled = bPVRTCEnabled = bASTCEnabled = true;
			}
			else
			{
				switch (CookFlavor)
				{
					case "_Multi":
						//need to check ini to determine which are supported
						Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bMultiTargetFormat_ETC1", out bETC1Enabled);
						Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bMultiTargetFormat_ETC1a", out bETC1aEnabled);
						Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bMultiTargetFormat_ETC2", out bETC2Enabled);
						Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bMultiTargetFormat_DXT", out bDXTEnabled);
						Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bMultiTargetFormat_ATC", out bATCEnabled);
						Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bMultiTargetFormat_PVRTC", out bPVRTCEnabled);
						Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bMultiTargetFormat_ASTC", out bASTCEnabled);
						break;
					case "_ETC1":
						bETC1Enabled = true;
						break;
					case "_ETC1a":
						bETC1aEnabled = true;
						break;
					case "_ETC2":
						bETC2Enabled = true;
						break;
					case "_DXT":
						bDXTEnabled = true;
						break;
					case "_ATC":
						bATCEnabled = true;
						break;
					case "_PVRTC":
						bPVRTCEnabled = true;
						break;
					case "_ASTC":
						bASTCEnabled = true;
						break;
					default:
						Log.TraceWarning("Invalid or unknown CookFlavor used in GenerateManifest: {0}", CookFlavor);
						break;
				}
			}
			bool bSupportingAllTextureFormats = bETC1Enabled && bETC2Enabled && bDXTEnabled && bATCEnabled && bPVRTCEnabled && bASTCEnabled;

			// If it is only ETC2 we need to skip adding the texture format filtering and instead use ES 3.0 as minimum version (it requires ETC2)
			bool bOnlyETC2Enabled = (bETC2Enabled && !(bETC1aEnabled || bETC1Enabled || bDXTEnabled || bATCEnabled || bPVRTCEnabled || bASTCEnabled));

			// Store cooked flavors in metadata (ETC1a treated as ETC1)
			string CookedFlavors = ((bETC1Enabled || bETC1aEnabled) ? "ETC1," : "") +
									(bETC2Enabled ? "ETC2," : "") +
									(bDXTEnabled ? "DXT," : "") +
									(bATCEnabled ? "ATC," : "") +
									(bPVRTCEnabled ? "PVRTC," : "") +
									(bASTCEnabled ? "ASTC," : "");
			CookedFlavors = (CookedFlavors == "") ? "" : CookedFlavors.Substring(0, CookedFlavors.Length - 1);

			StringBuilder Text = new StringBuilder();
			Text.AppendLine(XML_HEADER);
			Text.AppendLine("<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"");
			Text.AppendLine(string.Format("          package=\"{0}\"", PackageName));
			if (ExtraManifestNodeTags != null)
			{
				foreach (string Line in ExtraManifestNodeTags)
				{
					Text.AppendLine("          " + Line);
				}
			}
			Text.AppendLine(string.Format("          android:installLocation=\"{0}\"", InstallLocation));
			Text.AppendLine(string.Format("          android:versionCode=\"{0}\"", StoreVersion));
			Text.AppendLine(string.Format("          android:versionName=\"{0}\">", VersionDisplayName));

			Text.AppendLine("");

			Text.AppendLine("\t<!-- Application Definition -->");
			Text.AppendLine("\t<application android:label=\"@string/app_name\"");
			Text.AppendLine("\t             android:icon=\"@drawable/icon\"");
			if (ExtraApplicationNodeTags != null)
			{
				foreach (string Line in ExtraApplicationNodeTags)
				{
					Text.AppendLine("\t             " + Line);
				}
			}
			Text.AppendLine("\t             android:hardwareAccelerated=\"true\"");
			if (bGradleEnabled)
			{
				Text.AppendLine("\t				android:name=\"com.epicgames.ue4.GameApplication\"");
			}
			Text.AppendLine("\t             android:hasCode=\"true\">");
			if (bShowLaunchImage)
			{
				// normal application settings
				Text.AppendLine("\t\t<activity android:name=\"com.epicgames.ue4.SplashActivity\"");
				Text.AppendLine("\t\t          android:label=\"@string/app_name\"");
				Text.AppendLine("\t\t          android:theme=\"@style/UE4SplashTheme\"");
				Text.AppendLine("\t\t          android:launchMode=\"singleTask\"");
				Text.AppendLine(string.Format("\t\t          android:screenOrientation=\"{0}\"", Orientation));
				Text.AppendLine(string.Format("\t\t          android:debuggable=\"{0}\">", bIsForDistribution ? "false" : "true"));
				Text.AppendLine("\t\t\t<intent-filter>");
				Text.AppendLine("\t\t\t\t<action android:name=\"android.intent.action.MAIN\" />");
				Text.AppendLine(string.Format("\t\t\t\t<category android:name=\"android.intent.category.LAUNCHER\" />"));
				Text.AppendLine("\t\t\t</intent-filter>");
				Text.AppendLine("\t\t</activity>");
				Text.AppendLine("\t\t<activity android:name=\"com.epicgames.ue4.GameActivity\"");
				Text.AppendLine("\t\t          android:label=\"@string/app_name\"");
				Text.AppendLine("\t\t          android:theme=\"@style/UE4SplashTheme\"");
				Text.AppendLine(bAddDensity ? "\t\t          android:configChanges=\"mcc|mnc|uiMode|density|screenSize|smallestScreenSize|screenLayout|orientation|keyboardHidden|keyboard\""
											: "\t\t          android:configChanges=\"mcc|mnc|uiMode|screenSize|smallestScreenSize|screenLayout|orientation|keyboardHidden|keyboard\"");
			}
			else
			{
				Text.AppendLine("\t\t<activity android:name=\"com.epicgames.ue4.GameActivity\"");
				Text.AppendLine("\t\t          android:label=\"@string/app_name\"");
				Text.AppendLine("\t\t          android:theme=\"@android:style/Theme.Black.NoTitleBar.Fullscreen\"");
				Text.AppendLine(bAddDensity ? "\t\t          android:configChanges=\"mcc|mnc|uiMode|density|screenSize|smallestScreenSize|screenLayout|orientation|keyboardHidden|keyboard\""
											: "\t\t          android:configChanges=\"mcc|mnc|uiMode|screenSize|smallestScreenSize|screenLayout|orientation|keyboardHidden|keyboard\"");

			}
			if (SDKLevelInt >= 24)
			{
				Text.AppendLine("\t\t          android:resizeableActivity=\"false\"");
			}
			Text.AppendLine("\t\t          android:launchMode=\"singleTask\"");
			Text.AppendLine(string.Format("\t\t          android:screenOrientation=\"{0}\"", Orientation));
			if (ExtraActivityNodeTags != null)
			{
				foreach (string Line in ExtraActivityNodeTags)
				{
					Text.AppendLine("\t\t          " + Line);
				}
			}
			Text.AppendLine(string.Format("\t\t          android:debuggable=\"{0}\">", bIsForDistribution ? "false" : "true"));
			Text.AppendLine("\t\t\t<meta-data android:name=\"android.app.lib_name\" android:value=\"UE4\"/>");
			if (!bShowLaunchImage)
			{
				Text.AppendLine("\t\t\t<intent-filter>");
				Text.AppendLine("\t\t\t\t<action android:name=\"android.intent.action.MAIN\" />");
				Text.AppendLine(string.Format("\t\t\t\t<category android:name=\"android.intent.category.LAUNCHER\" />"));
				Text.AppendLine("\t\t\t</intent-filter>");
			}
			if (!string.IsNullOrEmpty(ExtraActivitySettings))
			{
				ExtraActivitySettings = ExtraActivitySettings.Replace("\\n", "\n");
				foreach (string Line in ExtraActivitySettings.Split("\r\n".ToCharArray()))
				{
					Text.AppendLine("\t\t\t" + Line);
				}
			}
			string ActivityAdditionsFile = Path.Combine(GameBuildFilesPath, "ManifestActivityAdditions.txt");
			if (File.Exists(ActivityAdditionsFile))
			{
				foreach (string Line in File.ReadAllLines(ActivityAdditionsFile))
				{
					Text.AppendLine("\t\t\t" + Line);
				}
			}
			Text.AppendLine("\t\t</activity>");

			// For OBB download support
			if (bShowLaunchImage)
			{
				Text.AppendLine("\t\t<activity android:name=\".DownloaderActivity\"");
				Text.AppendLine(string.Format("\t\t          android:screenOrientation=\"{0}\"", Orientation));
				Text.AppendLine(bAddDensity ? "\t\t          android:configChanges=\"mcc|mnc|uiMode|density|screenSize|orientation|keyboardHidden|keyboard\""
											: "\t\t          android:configChanges=\"mcc|mnc|uiMode|screenSize|orientation|keyboardHidden|keyboard\"");
				Text.AppendLine("\t\t          android:theme=\"@style/UE4SplashTheme\" />");
			}
			else
			{
				Text.AppendLine("\t\t<activity android:name=\".DownloaderActivity\" />");
			}

			// Figure out the required startup permissions if targetting devices supporting runtime permissions
			String StartupPermissions = "";
			if (TargetSDKVersion >= 23)
			{
				if (Configuration != "Shipping" || !bUseExternalFilesDir)
				{
					StartupPermissions = StartupPermissions + (StartupPermissions.Length > 0 ? "," : "") + "android.permission.WRITE_EXTERNAL_STORAGE";
				}
				if (bEnableGooglePlaySupport && bUseGetAccounts)
				{
					StartupPermissions = StartupPermissions + (StartupPermissions.Length > 0 ? "," : "") + "android.permission.GET_ACCOUNTS";
				}
			}

			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.EngineVersion\" android:value=\"{0}\"/>", EngineVersion));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.EngineBranch\" android:value=\"{0}\"/>", EngineBranch));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.ProjectVersion\" android:value=\"{0}\"/>", ProjectVersion));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.DepthBufferPreference\" android:value=\"{0}\"/>", ConvertDepthBufferIniValue(DepthBufferPreference)));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.bPackageDataInsideApk\" android:value=\"{0}\"/>", bPackageDataInsideApk ? "true" : "false"));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.bVerifyOBBOnStartUp\" android:value=\"{0}\"/>", (bIsForDistribution && !bDisableVerifyOBBOnStartUp) ? "true" : "false"));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.bShouldHideUI\" android:value=\"{0}\"/>", EnableFullScreen ? "true" : "false"));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.ProjectName\" android:value=\"{0}\"/>", ProjectName));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.AppType\" android:value=\"{0}\"/>", AppType));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.bHasOBBFiles\" android:value=\"{0}\"/>", bHasOBBFiles ? "true" : "false"));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.BuildConfiguration\" android:value=\"{0}\"/>", Configuration));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.CookedFlavors\" android:value=\"{0}\"/>", CookedFlavors));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.bValidateTextureFormats\" android:value=\"{0}\"/>", bValidateTextureFormats ? "true" : "false"));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.bUseExternalFilesDir\" android:value=\"{0}\"/>", bUseExternalFilesDir ? "true" : "false"));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.bUseDisplayCutout\" android:value=\"{0}\"/>", bUseDisplayCutout ? "true" : "false"));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.bAllowIMU\" android:value=\"{0}\"/>", bAllowIMU ? "true" : "false"));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.bSupportsVulkan\" android:value=\"{0}\"/>", bSupportsVulkan ? "true" : "false"));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.StartupPermissions\" android:value=\"{0}\"/>", StartupPermissions));
			if (bUseNEONForArmV7)
			{
				Text.AppendLine("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.bUseNEONForArmV7\" android:value=\"{true}\"/>");
			}
			if (bPackageForDaydream)
			{
				Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.bDaydream\" android:value=\"true\"/>"));
			}
			Text.AppendLine("\t\t<meta-data android:name=\"com.google.android.gms.games.APP_ID\"");
			Text.AppendLine("\t\t           android:value=\"@string/app_id\" />");
			Text.AppendLine("\t\t<meta-data android:name=\"com.google.android.gms.version\"");
			Text.AppendLine("\t\t           android:value=\"@integer/google_play_services_version\" />");
			if (bSupportAdMob)
			{
			Text.AppendLine("\t\t<activity android:name=\"com.google.android.gms.ads.AdActivity\"");
			Text.AppendLine("\t\t          android:configChanges=\"keyboard|keyboardHidden|orientation|screenLayout|uiMode|screenSize|smallestScreenSize\"/>");
			}
			if (!string.IsNullOrEmpty(ExtraApplicationSettings))
			{
				ExtraApplicationSettings = ExtraApplicationSettings.Replace("\\n", "\n");
				foreach (string Line in ExtraApplicationSettings.Split("\r\n".ToCharArray()))
				{
					Text.AppendLine("\t\t" + Line);
				}
			}
			string ApplicationAdditionsFile = Path.Combine(GameBuildFilesPath, "ManifestApplicationAdditions.txt");
			if (File.Exists(ApplicationAdditionsFile))
			{
				foreach (string Line in File.ReadAllLines(ApplicationAdditionsFile))
				{
					Text.AppendLine("\t\t" + Line);
				}
			}

			// Required for OBB download support
			Text.AppendLine("\t\t<service android:name=\"OBBDownloaderService\" />");
			Text.AppendLine("\t\t<receiver android:name=\"AlarmReceiver\" />");

			Text.AppendLine("\t\t<receiver android:name=\"com.epicgames.ue4.LocalNotificationReceiver\" />");

			Text.AppendLine("\t\t<receiver android:name=\"com.epicgames.ue4.MulticastBroadcastReceiver\" android:exported=\"true\">");
			Text.AppendLine("\t\t\t<intent-filter>");
			Text.AppendLine("\t\t\t\t<action android:name=\"com.android.vending.INSTALL_REFERRER\" />");
			Text.AppendLine("\t\t\t</intent-filter>");
			Text.AppendLine("\t\t</receiver>");

			// Max supported aspect ratio
			string MaxAspectRatioString = MaxAspectRatioValue.ToString("f", System.Globalization.CultureInfo.InvariantCulture);
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"android.max_aspect\" android:value=\"{0}\" />", MaxAspectRatioString));
					
			Text.AppendLine("\t</application>");

			Text.AppendLine("");
			Text.AppendLine("\t<!-- Requirements -->");

			// check for an override for the requirements section of the manifest
			string RequirementsOverrideFile = Path.Combine(GameBuildFilesPath, "ManifestRequirementsOverride.txt");
			if (File.Exists(RequirementsOverrideFile))
			{
				foreach (string Line in File.ReadAllLines(RequirementsOverrideFile))
				{
					Text.AppendLine("\t" + Line);
				}
			}
			else
			{
				if (!bGradleEnabled)
				{
					// need just the number part of the sdk
					Text.AppendLine(string.Format("\t<uses-sdk android:minSdkVersion=\"{0}\" android:targetSdkVersion=\"{1}\"/>", MinSDKVersion, TargetSDKVersion));
				}
				Text.AppendLine("\t<uses-feature android:glEsVersion=\"" + AndroidToolChain.GetGLESVersionFromGPUArch(GPUArch, bOnlyETC2Enabled, bBuildForES2, bBuildForES31) + "\" android:required=\"true\" />");
				Text.AppendLine("\t<uses-permission android:name=\"android.permission.INTERNET\"/>");
				Text.AppendLine("\t<uses-permission android:name=\"android.permission.WRITE_EXTERNAL_STORAGE\"/>");
				Text.AppendLine("\t<uses-permission android:name=\"android.permission.ACCESS_NETWORK_STATE\"/>");
				Text.AppendLine("\t<uses-permission android:name=\"android.permission.WAKE_LOCK\"/>");
			//	Text.AppendLine("\t<uses-permission android:name=\"android.permission.READ_PHONE_STATE\"/>");
				Text.AppendLine("\t<uses-permission android:name=\"com.android.vending.CHECK_LICENSE\"/>");
				Text.AppendLine("\t<uses-permission android:name=\"android.permission.ACCESS_WIFI_STATE\"/>");

				if (bEnableGooglePlaySupport && bUseGetAccounts)
				{
					Text.AppendLine("\t<uses-permission android:name=\"android.permission.GET_ACCOUNTS\"/>");
				}

				if(!bPackageForOculusMobile)
				{
					Text.AppendLine("\t<uses-permission android:name=\"android.permission.MODIFY_AUDIO_SETTINGS\"/>");
					Text.AppendLine("\t<uses-permission android:name=\"android.permission.VIBRATE\"/>");
				}

				//			Text.AppendLine("\t<uses-permission android:name=\"android.permission.DISABLE_KEYGUARD\"/>");

				if (bEnableIAP)
				{
					Text.AppendLine("\t<uses-permission android:name=\"com.android.vending.BILLING\"/>");
				}
				if (ExtraPermissions != null)
				{
					foreach (string Permission in ExtraPermissions)
					{
						string TrimmedPermission = Permission.Trim(' ');
						if (TrimmedPermission != "")
						{
							string PermissionString = string.Format("\t<uses-permission android:name=\"{0}\"/>", TrimmedPermission);
							if (!Text.ToString().Contains(PermissionString))
							{
								Text.AppendLine(PermissionString);
							}
						}
					}
				}
				string RequirementsAdditionsFile = Path.Combine(GameBuildFilesPath, "ManifestRequirementsAdditions.txt");
				if (File.Exists(RequirementsAdditionsFile))
				{
					foreach (string Line in File.ReadAllLines(RequirementsAdditionsFile))
					{
						Text.AppendLine("\t" + Line);
					}
				}
				if (AndroidGraphicsDebugger.ToLower() == "adreno")
				{
					string PermissionString = "\t<uses-permission android:name=\"com.qti.permission.PROFILER\"/>";
					if (!Text.ToString().Contains(PermissionString))
					{
						Text.AppendLine(PermissionString);
					}
				}

				if (!bSupportingAllTextureFormats)
				{
					Text.AppendLine("\t<!-- Supported texture compression formats (cooked) -->");
					if (bETC1Enabled || bETC1aEnabled)
					{
						Text.AppendLine("\t<supports-gl-texture android:name=\"GL_OES_compressed_ETC1_RGB8_texture\" />");
					}
					if (bETC2Enabled && !bOnlyETC2Enabled)
					{
						Text.AppendLine("\t<supports-gl-texture android:name=\"GL_COMPRESSED_RGB8_ETC2\" />");
						Text.AppendLine("\t<supports-gl-texture android:name=\"GL_COMPRESSED_RGBA8_ETC2_EAC\" />");
					}
					if (bATCEnabled)
					{
						Text.AppendLine("\t<supports-gl-texture android:name=\"GL_AMD_compressed_ATC_texture\" />");
						Text.AppendLine("\t<supports-gl-texture android:name=\"GL_ATI_texture_compression_atitc\" />");
					}
					if (bDXTEnabled)
					{
						Text.AppendLine("\t<supports-gl-texture android:name=\"GL_EXT_texture_compression_dxt1\" />");
						Text.AppendLine("\t<supports-gl-texture android:name=\"GL_EXT_texture_compression_s3tc\" />");
						Text.AppendLine("\t<supports-gl-texture android:name=\"GL_NV_texture_compression_s3tc\" />");
					}
					if (bPVRTCEnabled)
					{
						Text.AppendLine("\t<supports-gl-texture android:name=\"GL_IMG_texture_compression_pvrtc\" />");
					}
					if (bASTCEnabled)
					{
						Text.AppendLine("\t<supports-gl-texture android:name=\"GL_KHR_texture_compression_astc_ldr\" />");
					}
				}
			}

			Text.AppendLine("</manifest>");

			// allow plugins to modify final manifest HERE
			XDocument XDoc;
			try
			{
				XDoc = XDocument.Parse(Text.ToString());
			}
			catch (Exception e)
			{
				throw new BuildException("AndroidManifest.xml is invalid {0}\n{1}", e, Text.ToString());
			}

			UPL.ProcessPluginNode(Arch, "androidManifestUpdates", "", ref XDoc);
			return XDoc.ToString();
		}

		private string GenerateProguard(string Arch, string EngineSourcePath, string GameBuildFilesPath)
		{
			StringBuilder Text = new StringBuilder();

			string ProguardFile = Path.Combine(EngineSourcePath, "proguard-project.txt");
			if (File.Exists(ProguardFile))
			{
				foreach (string Line in File.ReadAllLines(ProguardFile))
				{
					Text.AppendLine(Line);
				}
			}

			string ProguardAdditionsFile = Path.Combine(GameBuildFilesPath, "ProguardAdditions.txt");
			if (File.Exists(ProguardAdditionsFile))
			{
				foreach (string Line in File.ReadAllLines(ProguardAdditionsFile))
				{
					Text.AppendLine(Line);
				}
			}

			// add plugin additions
			return UPL.ProcessPluginNode(Arch, "proguardAdditions", Text.ToString());
		}

		private void ValidateGooglePlay(string UE4BuildPath)
		{
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			bool bEnableGooglePlaySupport;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bEnableGooglePlaySupport", out bEnableGooglePlaySupport);

			if (!bEnableGooglePlaySupport)
			{
				// do not need to do anything; it is fine
				return;
			}

			string IniAppId;
			bool bInvalidIniAppId = false;
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "GamesAppID", out IniAppId);

			//validate the value found in the AndroidRuntimeSettings
			Int64 Value;
			if (IniAppId.Length == 0 || !Int64.TryParse(IniAppId, out Value))
			{
				bInvalidIniAppId = true;
			}

			bool bInvalid = false;
			string ReplacementId = "";
			String Filename = Path.Combine(UE4BuildPath, "res", "values", "GooglePlayAppID.xml");
			if (File.Exists(Filename))
			{
				string[] FileContent = File.ReadAllLines(Filename);
				int LineIndex = -1;
				foreach (string Line in FileContent)
				{
					++LineIndex;

					int StartIndex = Line.IndexOf("\"app_id\">");
					if (StartIndex < 0)
						continue;

					StartIndex += 9;
					int EndIndex = Line.IndexOf("</string>");
					if (EndIndex < 0)
						continue;

					string XmlAppId = Line.Substring(StartIndex, EndIndex - StartIndex);

					//validate that the AppId matches the .ini value for the GooglePlay AppId, assuming it's valid
					if (!bInvalidIniAppId &&  IniAppId.CompareTo(XmlAppId) != 0)
					{
						Log.TraceInformation("Replacing Google Play AppID in GooglePlayAppID.xml with AndroidRuntimeSettings .ini value");

						bInvalid = true;
						ReplacementId = IniAppId;
						
					}					
					else if(XmlAppId.Length == 0 || !Int64.TryParse(XmlAppId, out Value))
					{
						Log.TraceWarning("\nWARNING: GooglePlay Games App ID is invalid! Replacing it with \"1\"");

						//write file with something which will fail but not cause an exception if executed
						bInvalid = true;
						ReplacementId = "1";
					}	

					if(bInvalid)
					{
						// remove any read only flags if invalid so it can be replaced
						FileInfo DestFileInfo = new FileInfo(Filename);
						DestFileInfo.Attributes = DestFileInfo.Attributes & ~FileAttributes.ReadOnly;

						//preserve the rest of the file, just fix up this line
						string NewLine = Line.Replace("\"app_id\">" + XmlAppId + "</string>", "\"app_id\">" + ReplacementId + "</string>");
						FileContent[LineIndex] = NewLine;

						File.WriteAllLines(Filename, FileContent);
					}

					break;
				}
			}
			else
			{
				string NewAppId;
				// if we don't have an appID to use from the config, write file with something which will fail but not cause an exception if executed
				if (bInvalidIniAppId)
				{
					Log.TraceWarning("\nWARNING: Creating GooglePlayAppID.xml using a Google Play AppID of \"1\" because there was no valid AppID in AndroidRuntimeSettings!");
					NewAppId = "1";
				}
				else
				{
					Log.TraceInformation("Creating GooglePlayAppID.xml with AndroidRuntimeSettings .ini value");
					NewAppId = IniAppId;
				}

				File.WriteAllText(Filename, XML_HEADER + "\n<resources>\n\t<string name=\"app_id\">" + NewAppId + "</string>\n</resources>\n");
			}
		}

		private bool FilesAreDifferent(string SourceFilename, string DestFilename)
		{
			// source must exist
			FileInfo SourceInfo = new FileInfo(SourceFilename);
			if (!SourceInfo.Exists)
			{
				throw new BuildException("Can't make an APK without file [{0}]", SourceFilename);
			}

			// different if destination doesn't exist
			FileInfo DestInfo = new FileInfo(DestFilename);
			if (!DestInfo.Exists)
			{
				return true;
			}

			// file lengths differ?
			if (SourceInfo.Length != DestInfo.Length)
			{
				return true;
			}

			// validate timestamps
			TimeSpan Diff = DestInfo.LastWriteTimeUtc - SourceInfo.LastWriteTimeUtc;
			if (Diff.TotalSeconds < -1 || Diff.TotalSeconds > 1)
			{
				return true;
			}

			// could check actual bytes just to be sure, but good enough
			return false;
		}

		private bool RequiresOBB(bool bDisallowPackageInAPK, string OBBLocation)
		{
			if (bDisallowPackageInAPK)
			{
				Log.TraceInformation("APK contains data.");
				return false;
			}
			else if (!String.IsNullOrEmpty(Environment.GetEnvironmentVariable("uebp_LOCAL_ROOT")))
			{
				Log.TraceInformation("On build machine.");
				return true;
			}
			else
			{
				Log.TraceInformation("Looking for OBB.");
				return File.Exists(OBBLocation);
			}
		}

		private void PatchAntBatIfNeeded()
		{
			// only need to do this for Windows (other platforms are Mono so use that for check)
			if (Utils.IsRunningOnMono)
			{
				return;
			}

			string AntBinPath = Environment.ExpandEnvironmentVariables("%ANT_HOME%/bin");
			string AntBatFilename = Path.Combine(AntBinPath, "ant.bat");
			string AntOrigBatFilename = Path.Combine(AntBinPath, "ant.orig.bat");

			// check for an unused drive letter
			string UnusedDriveLetter = "";
			bool bFound = true;
			DriveInfo[] AllDrives = DriveInfo.GetDrives();
			for (char DriveLetter = 'Z'; DriveLetter >= 'A'; DriveLetter--)
			{
				UnusedDriveLetter = Char.ToString(DriveLetter) + ":";
				bFound = false;
				for (int DriveIndex = AllDrives.Length-1; DriveIndex >= 0; DriveIndex--)
				{
					if (AllDrives[DriveIndex].Name.ToUpper().StartsWith(UnusedDriveLetter))
					{
						bFound = true;
						break;
					}
				}
				if (!bFound)
				{
					break;
				}
			}

			if (bFound)
			{
				Log.TraceInformation("\nUnable to apply fixed ant.bat (all drive letters in use!)");
				return;
			}

			Log.TraceInformation("\nPatching ant.bat to work around commandline length limit (using unused drive letter {0})", UnusedDriveLetter);

			if (!File.Exists(AntOrigBatFilename))
			{
				// copy the existing ant.bat to ant.orig.bat
				File.Copy(AntBatFilename, AntOrigBatFilename, true);
			}

			// make sure ant.bat isn't read-only
			FileAttributes Attribs = File.GetAttributes(AntBatFilename);
			if (Attribs.HasFlag(FileAttributes.ReadOnly))
			{
				File.SetAttributes(AntBatFilename, Attribs & ~FileAttributes.ReadOnly);
			}

			// generate new ant.bat with an unused drive letter for subst
			string AntBatText =
					"@echo off\n" +
					"setlocal\n" +
					"set ANTPATH=%~dp0\n" +
					"set ANT_CMD_LINE_ARGS=\n" +
					":setupArgs\n" +
					"if \"\"%1\"\"==\"\"\"\" goto doneStart\n" +
					"set ANT_CMD_LINE_ARGS=%ANT_CMD_LINE_ARGS% %1\n" +
					"shift\n" +
					"goto setupArgs\n\n" +
					":doneStart\n" +
					"subst " + UnusedDriveLetter + " \"%CD%\"\n" +
					"pushd " + UnusedDriveLetter + "\n" +
					"call \"%ANTPATH%\\ant.orig.bat\" %ANT_CMD_LINE_ARGS%\n" +
					"set ANTERROR=%ERRORLEVEL%\n" +
					"popd\n" +
					"subst " + UnusedDriveLetter + " /d\n" +
					"exit /b %ANTERROR%\n";

			File.WriteAllText(AntBatFilename, AntBatText);
		}

		private bool CreateRunGradle(string GradlePath)
		{
			string RunGradleBatFilename = Path.Combine(GradlePath, "rungradle.bat");

			// check for an unused drive letter
			string UnusedDriveLetter = "";
			bool bFound = true;
			DriveInfo[] AllDrives = DriveInfo.GetDrives();
			for (char DriveLetter = 'Z'; DriveLetter >= 'A'; DriveLetter--)
			{
				UnusedDriveLetter = Char.ToString(DriveLetter) + ":";
				bFound = false;
				for (int DriveIndex = AllDrives.Length - 1; DriveIndex >= 0; DriveIndex--)
				{
					if (AllDrives[DriveIndex].Name.ToUpper().StartsWith(UnusedDriveLetter))
					{
						bFound = true;
						break;
					}
				}
				if (!bFound)
				{
					break;
				}
			}

			if (bFound)
			{
				Log.TraceInformation("\nUnable to apply subst, using gradlew.bat directly (all drive letters in use!)");
				return false;
			}

			Log.TraceInformation("\nCreating rungradle.bat to work around commandline length limit (using unused drive letter {0})", UnusedDriveLetter);

			// make sure rungradle.bat isn't read-only
			if (File.Exists(RunGradleBatFilename))
			{
				FileAttributes Attribs = File.GetAttributes(RunGradleBatFilename);
				if (Attribs.HasFlag(FileAttributes.ReadOnly))
				{
					File.SetAttributes(RunGradleBatFilename, Attribs & ~FileAttributes.ReadOnly);
				}
			}

			// generate new ant.bat with an unused drive letter for subst
			string RunGradleBatText =
					"@echo off\n" +
					"setlocal\n" +
					"set GRADLEPATH=%~dp0\n" +
					"set GRADLE_CMD_LINE_ARGS=\n" +
					":setupArgs\n" +
					"if \"\"%1\"\"==\"\"\"\" goto doneStart\n" +
					"set GRADLE_CMD_LINE_ARGS=%GRADLE_CMD_LINE_ARGS% %1\n" +
					"shift\n" +
					"goto setupArgs\n\n" +
					":doneStart\n" +
					"subst " + UnusedDriveLetter + " \"%CD%\"\n" +
					"pushd " + UnusedDriveLetter + "\n" +
					"call \"%GRADLEPATH%\\gradlew.bat\" %GRADLE_CMD_LINE_ARGS%\n" +
					"set GRADLEERROR=%ERRORLEVEL%\n" +
					"popd\n" +
					"subst " + UnusedDriveLetter + " /d\n" +
					"exit /b %GRADLEERROR%\n";

			File.WriteAllText(RunGradleBatFilename, RunGradleBatText);

			return true;
		}

		private bool GradleEnabled()
		{
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			bool bEnableGradle = false;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bEnableGradle", out bEnableGradle);
			return bEnableGradle;
		}

		private bool IsLicenseAgreementValid()
		{
			string LicensePath = Environment.ExpandEnvironmentVariables("%ANDROID_HOME%/licenses");

			// directory must exist
			if (!Directory.Exists(LicensePath))
			{
				Log.TraceInformation("Directory doesn't exist {0}", LicensePath);
				return false;
			}

			// license file must exist
			string LicenseFilename = Path.Combine(LicensePath, "android-sdk-license");
			if (!File.Exists(LicenseFilename))
			{
				Log.TraceInformation("File doesn't exist {0}", LicenseFilename);
				return false;
			}

			// ignore contents of hash for now (Gradle will report if it isn't valid)
			return true;
		}

		private void GetMinTargetSDKVersions(AndroidToolChain ToolChain, string Arch, UnrealPluginLanguage UPL, string NDKArch, out int MinSDKVersion, out int TargetSDKVersion, out int NDKLevelInt)
		{
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			Ini.GetInt32("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "MinSDKVersion", out MinSDKVersion);
			TargetSDKVersion = MinSDKVersion;
			Ini.GetInt32("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "TargetSDKVersion", out TargetSDKVersion);

			// Check for targetSDKOverride from UPL
			string TargetOverride = UPL.ProcessPluginNode(NDKArch, "targetSDKOverride", "");
			if (!String.IsNullOrEmpty(TargetOverride))
			{
				int OverrideInt = 0;
				if (int.TryParse(TargetOverride, out OverrideInt))
				{
					TargetSDKVersion = OverrideInt;
				}
			}

			// Make sure minSdkVersion is at least 13 (need this for appcompat-v13 used by AndroidPermissions)
			// this may be changed by active plugins (Google Play Services 11.0.4 needs 14 for example)
			if (bGradleEnabled && MinSDKVersion < MinimumSDKLevelForGradle)
			{
				MinSDKVersion = MinimumSDKLevelForGradle;
				Log.TraceInformation("Fixing minSdkVersion; requires minSdkVersion of {0} with Gradle based on active plugins", MinimumSDKLevelForGradle);
			}

			// 64-bit targets must be android-21 or higher
			NDKLevelInt = ToolChain.GetNdkApiLevelInt();
			if (NDKLevelInt < 21)
			{
				if (Arch == "-arm64" || Arch == "-x64")
				{
					NDKLevelInt = 21;
				}
			}

			// fix up the MinSdkVersion
			if (NDKLevelInt > 19)
			{
				if (MinSDKVersion < 21)
				{
					MinSDKVersion = 21;
					Log.TraceInformation("Fixing minSdkVersion; NDK level above 19 requires minSdkVersion of 21 (arch={0})", Arch.Substring(1));
				}
			}

			if (TargetSDKVersion < MinSDKVersion)
			{
				TargetSDKVersion = MinSDKVersion;
			}
		}

		private void CreateGradlePropertiesFiles(string Arch, int MinSDKVersion, int TargetSDKVersion, string CompileSDKVersion, string BuildToolsVersion, string PackageName,
			string DestApkName, string NDKArch,	string UE4BuildFilesPath, string GameBuildFilesPath, string UE4BuildGradleAppPath, string UE4BuildPath, string UE4BuildGradlePath, bool bForDistribution, bool bIsEmbedded)
		{
			// Create gradle.properties
			StringBuilder GradleProperties = new StringBuilder();

			int StoreVersion = GetStoreVersion(GetUE4Arch(NDKArch));
			string VersionDisplayName = GetVersionDisplayName(bIsEmbedded);

			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);

			GradleProperties.AppendLine("org.gradle.daemon=false");
			GradleProperties.AppendLine("org.gradle.jvmargs=-XX:MaxHeapSize=4096m -Xmx9216m");
			GradleProperties.AppendLine(string.Format("COMPILE_SDK_VERSION={0}", CompileSDKVersion));
			GradleProperties.AppendLine(string.Format("BUILD_TOOLS_VERSION={0}", BuildToolsVersion));
			GradleProperties.AppendLine(string.Format("PACKAGE_NAME={0}", PackageName));
			GradleProperties.AppendLine(string.Format("MIN_SDK_VERSION={0}", MinSDKVersion.ToString()));
			GradleProperties.AppendLine(string.Format("TARGET_SDK_VERSION={0}", TargetSDKVersion.ToString()));
			GradleProperties.AppendLine(string.Format("STORE_VERSION={0}", StoreVersion.ToString()));
			GradleProperties.AppendLine(string.Format("VERSION_DISPLAY_NAME={0}", VersionDisplayName));

			if (DestApkName != null)
			{
				GradleProperties.AppendLine(string.Format("OUTPUT_PATH={0}", Path.GetDirectoryName(DestApkName).Replace("\\", "/")));
				GradleProperties.AppendLine(string.Format("OUTPUT_FILENAME={0}", Path.GetFileName(DestApkName)));
			}

			// add any Gradle properties from UPL
			string GradlePropertiesUPL = UPL.ProcessPluginNode(NDKArch, "gradleProperties", "");
			GradleProperties.AppendLine(GradlePropertiesUPL);

			StringBuilder GradleBuildAdditionsContent = new StringBuilder();
			GradleBuildAdditionsContent.AppendLine("apply from: 'aar-imports.gradle'");
			GradleBuildAdditionsContent.AppendLine("apply from: 'projects.gradle'");

			GradleBuildAdditionsContent.AppendLine("android {");
			GradleBuildAdditionsContent.AppendLine("\tdefaultConfig {");
			GradleBuildAdditionsContent.AppendLine("\t\tndk {");
			GradleBuildAdditionsContent.AppendLine(string.Format("\t\t\tabiFilter \"{0}\"", NDKArch));
			GradleBuildAdditionsContent.AppendLine("\t\t}");
			GradleBuildAdditionsContent.AppendLine("\t}");

			if (bForDistribution)
			{
				bool bDisableV2Signing = false;

				if (GetTargetOculusMobileDevices().Contains("GearGo"))
				{
					bDisableV2Signing = true;
					Log.TraceInformation("Disabling v2Signing for Oculus Go / Gear VR APK");
				}

				string KeyAlias, KeyStore, KeyStorePassword, KeyPassword;
				Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "KeyStore", out KeyStore);
				Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "KeyAlias", out KeyAlias);
				Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "KeyStorePassword", out KeyStorePassword);
				Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "KeyPassword", out KeyPassword);

				if (string.IsNullOrEmpty(KeyStore) || string.IsNullOrEmpty(KeyAlias) || string.IsNullOrEmpty(KeyStorePassword))
				{
					throw new BuildException("DistributionSigning settings are not all set. Check the DistributionSettings section in the Android tab of Project Settings");
				}

				if (string.IsNullOrEmpty(KeyPassword) || KeyPassword == "_sameaskeystore_")
				{
					KeyPassword = KeyStorePassword;
				}

				// Make sure the keystore file exists
				string KeyStoreFilename = Path.Combine(UE4BuildPath, KeyStore);
				if (!File.Exists(KeyStoreFilename))
				{
					throw new BuildException("Keystore file is missing. Check the DistributionSettings section in the Android tab of Project Settings");
				}

				GradleProperties.AppendLine(string.Format("STORE_FILE={0}", KeyStoreFilename.Replace("\\", "/")));
				GradleProperties.AppendLine(string.Format("STORE_PASSWORD={0}", KeyStorePassword));
				GradleProperties.AppendLine(string.Format("KEY_ALIAS={0}", KeyAlias));
				GradleProperties.AppendLine(string.Format("KEY_PASSWORD={0}", KeyPassword));

				GradleBuildAdditionsContent.AppendLine("\tsigningConfigs {");
				GradleBuildAdditionsContent.AppendLine("\t\trelease {");
				GradleBuildAdditionsContent.AppendLine(string.Format("\t\t\tstoreFile file('{0}')", KeyStoreFilename.Replace("\\", "/")));
				GradleBuildAdditionsContent.AppendLine(string.Format("\t\t\tstorePassword '{0}'", KeyStorePassword));
				GradleBuildAdditionsContent.AppendLine(string.Format("\t\t\tkeyAlias '{0}'", KeyAlias));
				GradleBuildAdditionsContent.AppendLine(string.Format("\t\t\tkeyPassword '{0}'", KeyPassword));
				if (bDisableV2Signing)
				{
					GradleBuildAdditionsContent.AppendLine("\t\t\tv2SigningEnabled false");
				}
				GradleBuildAdditionsContent.AppendLine("\t\t}");
				GradleBuildAdditionsContent.AppendLine("\t}");

				// Generate the Proguard file contents and write it
				string ProguardContents = GenerateProguard(NDKArch, UE4BuildFilesPath, GameBuildFilesPath);
				string ProguardFilename = Path.Combine(UE4BuildGradleAppPath, "proguard-rules.pro");
				SafeDeleteFile(ProguardFilename);
				File.WriteAllText(ProguardFilename, ProguardContents);
			}
			else
			{
				// empty just for Gradle not to complain
				GradleProperties.AppendLine("STORE_FILE=");
				GradleProperties.AppendLine("STORE_PASSWORD=");
				GradleProperties.AppendLine("KEY_ALIAS=");
				GradleProperties.AppendLine("KEY_PASSWORD=");

				// empty just for Gradle not to complain
				GradleBuildAdditionsContent.AppendLine("\tsigningConfigs {");
				GradleBuildAdditionsContent.AppendLine("\t\trelease {");
				GradleBuildAdditionsContent.AppendLine("\t\t}");
				GradleBuildAdditionsContent.AppendLine("\t}");
			}

			GradleBuildAdditionsContent.AppendLine("\tbuildTypes {");
			GradleBuildAdditionsContent.AppendLine("\t\trelease {");
			GradleBuildAdditionsContent.AppendLine("\t\t\tsigningConfig signingConfigs.release");
			if (GradlePropertiesUPL.Contains("DISABLE_MINIFY=1"))
			{
				GradleBuildAdditionsContent.AppendLine("\t\t\tminifyEnabled false");
			}
			else
			{
				GradleBuildAdditionsContent.AppendLine("\t\t\tminifyEnabled true");
			}
			if (GradlePropertiesUPL.Contains("DISABLE_PROGUARD=1"))
			{
				GradleBuildAdditionsContent.AppendLine("\t\t\tuseProguard false");
			}
			else
			{
				GradleBuildAdditionsContent.AppendLine("\t\t\tproguardFiles getDefaultProguardFile('proguard-android.txt'), 'proguard-rules.pro'");
			}
			GradleBuildAdditionsContent.AppendLine("\t\t}");
			GradleBuildAdditionsContent.AppendLine("\t\tdebug {");
			GradleBuildAdditionsContent.AppendLine("\t\t\tdebuggable true");
			GradleBuildAdditionsContent.AppendLine("\t\t}");
			GradleBuildAdditionsContent.AppendLine("\t}");
			GradleBuildAdditionsContent.AppendLine("}");

			// Add any UPL app buildGradleAdditions
			GradleBuildAdditionsContent.Append(UPL.ProcessPluginNode(NDKArch, "buildGradleAdditions", ""));

			string GradleBuildAdditionsFilename = Path.Combine(UE4BuildGradleAppPath, "buildAdditions.gradle");
			File.WriteAllText(GradleBuildAdditionsFilename, GradleBuildAdditionsContent.ToString());

			string GradlePropertiesFilename = Path.Combine(UE4BuildGradlePath, "gradle.properties");
			File.WriteAllText(GradlePropertiesFilename, GradleProperties.ToString());

			// Add lint if requested (note depreciation warnings can be suppressed with @SuppressWarnings("deprecation")
			string GradleBaseBuildAdditionsContents = "";
			bool bEnableLint = false;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bEnableLint", out bEnableLint);
			if (bEnableLint)
			{
				GradleBaseBuildAdditionsContents =
					"allprojects {\n" +
					"\ttasks.withType(JavaCompile) {\n" +
					"\t\toptions.compilerArgs << \"-Xlint:unchecked\" << \"-Xlint:deprecation\"\n" +
					"\t}\n" +
					"}\n\n";
			}

			// Create baseBuildAdditions.gradle from plugins baseBuildGradleAdditions
			string GradleBaseBuildAdditionsFilename = Path.Combine(UE4BuildGradlePath, "baseBuildAdditions.gradle");
			File.WriteAllText(GradleBaseBuildAdditionsFilename, UPL.ProcessPluginNode(NDKArch, "baseBuildGradleAdditions", GradleBaseBuildAdditionsContents));

			// Create buildscriptAdditions.gradle from plugins buildscriptGradleAdditions
			string GradleBuildScriptAdditionsFilename = Path.Combine(UE4BuildGradlePath, "buildscriptAdditions.gradle");
			File.WriteAllText(GradleBuildScriptAdditionsFilename, UPL.ProcessPluginNode(NDKArch, "buildscriptGradleAdditions", ""));
		}

		private void MakeApk(AndroidToolChain ToolChain, string ProjectName, TargetType InTargetType, string ProjectDirectory, string OutputPath, string EngineDirectory, bool bForDistribution, string CookFlavor, 
			UnrealTargetConfiguration Configuration, bool bMakeSeparateApks, bool bIncrementalPackage, bool bDisallowPackagingDataInApk, bool bDisallowExternalFilesDir, bool bSkipGradleBuild)
		{
			Log.TraceInformation("\n===={0}====PREPARING TO MAKE APK=================================================================", DateTime.Now.ToString());

			if (UPL.GetLastError() != null)
			{
				throw new BuildException("Cannot make APK with UPL errors");
			}

			// make sure it is cached
			string EngineVersion = ReadEngineVersion();

			SetMinimumSDKLevelForGradle();

			// check for Gradle enabled for this project
			bGradleEnabled = GradleEnabled();

			if (bGradleEnabled)
			{
				if (!IsLicenseAgreementValid())
				{
					throw new BuildException("Android SDK license file not found.  Please agree to license in Android project settings in the editor.");
				}
			}

			LogBuildSetup();

			bool bIsBuildMachine = Environment.GetEnvironmentVariable("IsBuildMachine") == "1";

			// do this here so we'll stop early if there is a problem with the SDK API level (cached so later calls will return the same)
			string SDKAPILevel = GetSdkApiLevel(ToolChain, bGradleEnabled);
			int SDKLevelInt = GetApiLevelInt(SDKAPILevel);
			string BuildToolsVersion = GetBuildToolsVersion(bGradleEnabled);

			if (!bGradleEnabled)
			{
				PatchAntBatIfNeeded();
			}

			// cache some tools paths
			string NDKBuildPath = Environment.ExpandEnvironmentVariables("%NDKROOT%/ndk-build" + (Utils.IsRunningOnMono ? "" : ".cmd"));

			// set up some directory info
			string IntermediateAndroidPath = Path.Combine(ProjectDirectory, "Intermediate", "Android");
			string UE4BuildPath = Path.Combine(IntermediateAndroidPath, "APK");
			string UE4JavaFilePath = Path.Combine(ProjectDirectory, "Build", "Android", GetUE4JavaSrcPath());
			string UE4BuildFilesPath = GetUE4BuildFilePath(EngineDirectory);
			string GameBuildFilesPath = Path.Combine(ProjectDirectory, "Build", "Android");
			string UE4BuildAssetsPath = Path.Combine(UE4BuildPath, "assets");
			string UE4BuildResourcesPath = Path.Combine(UE4BuildPath, "res");

			// force create from scratch if on build machine
			bool bCreateFromScratch = bIsBuildMachine;

			// see if last time matches the skipGradle setting
			string BuildTypeFilename = Path.Combine(IntermediateAndroidPath, "BuildType.txt");
			string BuildTypeID = bSkipGradleBuild ? "Embedded" : "Standalone";
			if (File.Exists(BuildTypeFilename))
			{
				string BuildTypeContents = File.ReadAllText(BuildTypeFilename);
				if (BuildTypeID != BuildTypeContents)
				{
					bCreateFromScratch = true;
				}
			}

			if (bCreateFromScratch)
			{
				Log.TraceInformation("Cleaning {0}", IntermediateAndroidPath);
				DeleteDirectory(IntermediateAndroidPath);
				Directory.CreateDirectory(IntermediateAndroidPath);
			}
			
			if (!System.IO.Directory.Exists(IntermediateAndroidPath))
			{
				System.IO.Directory.CreateDirectory(IntermediateAndroidPath);
			}

			// write build type
			File.WriteAllText(BuildTypeFilename, BuildTypeID);

			// cache if we want data in the Apk
			bool bPackageDataInsideApk = bDisallowPackagingDataInApk ? false : GetPackageDataInsideApk();
			bool bDisableVerifyOBBOnStartUp = DisableVerifyOBBOnStartUp();
			bool bUseExternalFilesDir = UseExternalFilesDir(bDisallowExternalFilesDir);

			// Generate Java files
			string PackageName = GetPackageName(ProjectName);
			string TemplateDestinationBase = Path.Combine(ProjectDirectory, "Build", "Android", "src", PackageName.Replace('.', Path.DirectorySeparatorChar));
			MakeDirectoryIfRequired(TemplateDestinationBase);

			// We'll be writing the OBB data into the same location as the download service files
			string UE4OBBDataFileName = GetUE4JavaOBBDataFileName(TemplateDestinationBase);
			string UE4DownloadShimFileName = GetUE4JavaDownloadShimFileName(UE4JavaFilePath);

			// Get list of all architecture and GPU targets for build
			List<string> Arches = ToolChain.GetAllArchitectures();
			List<string> GPUArchitectures = ToolChain.GetAllGPUArchitectures();

			// Template generated files
			string JavaTemplateSourceDir = GetUE4TemplateJavaSourceDir(EngineDirectory);
			IEnumerable<TemplateFile> templates = from template in Directory.EnumerateFiles(JavaTemplateSourceDir, "*.template")
							let RealName = Path.GetFileNameWithoutExtension(template)
							select new TemplateFile { SourceFile = template, DestinationFile = GetUE4TemplateJavaDestination(TemplateDestinationBase, RealName) };

			// Generate the OBB and Shim files here
			string ObbFileLocation = ProjectDirectory + "/Saved/StagedBuilds/Android" + CookFlavor + ".obb";
			string PatchFileLocation = ProjectDirectory + "/Saved/StagedBuilds/Android" + CookFlavor + ".patch.obb";
			List<string> RequiredOBBFiles = new List<String> { ObbFileLocation };
			if (File.Exists(PatchFileLocation))
			{
				RequiredOBBFiles.Add(PatchFileLocation);
			}

			// Generate the OBBData.java file if out of date (can skip rewriting it if packaging inside Apk in some cases)
			WriteJavaOBBDataFile(UE4OBBDataFileName, PackageName, RequiredOBBFiles, CookFlavor, bPackageDataInsideApk, Arches[0]);

			// Make sure any existing proguard file in project is NOT used (back it up)
			string ProjectBuildProguardFile = Path.Combine(GameBuildFilesPath, "proguard-project.txt");
			if (File.Exists(ProjectBuildProguardFile))
			{
				string ProjectBackupProguardFile = Path.Combine(GameBuildFilesPath, "proguard-project.backup");
				File.Move(ProjectBuildProguardFile, ProjectBackupProguardFile);
			}

			WriteJavaDownloadSupportFiles(UE4DownloadShimFileName, templates, new Dictionary<string, string>{
				{ "$$GameName$$", ProjectName },
				{ "$$PublicKey$$", GetPublicKey() }, 
				{ "$$PackageName$$",PackageName }
			});

			// Sometimes old files get left behind if things change, so we'll do a clean up pass
			{
				string CleanUpBaseDir = Path.Combine(ProjectDirectory, "Build", "Android", "src");
				string ImmediateBaseDir = Path.Combine(UE4BuildPath, "src");
				IEnumerable<string> files = Directory.EnumerateFiles(CleanUpBaseDir, "*.java", SearchOption.AllDirectories);

				Log.TraceInformation("Cleaning up files based on template dir {0}", TemplateDestinationBase);

				// Make a set of files that are okay to clean up
				HashSet<string> cleanFiles = new HashSet<string>();
				cleanFiles.Add("OBBData.java");
				foreach (TemplateFile template in templates)
				{
					cleanFiles.Add(Path.GetFileName(template.DestinationFile));
				}

				foreach (string filename in files)
				{
					if (filename == UE4DownloadShimFileName)  // we always need the shim, and it'll get rewritten if needed anyway
						continue;

					string filePath = Path.GetDirectoryName(filename);  // grab the file's path
					if (filePath != TemplateDestinationBase)             // and check to make sure it isn't the same as the Template directory we calculated earlier
					{
						// Only delete the files in the cleanup set
						if (!cleanFiles.Contains(Path.GetFileName(filename)))
							continue;

						Log.TraceInformation("Cleaning up file {0}", filename);
						SafeDeleteFile(filename, false);

						// Check to see if this file also exists in our target destination, and if so nuke it too
						string DestFilename = Path.Combine(ImmediateBaseDir, Utils.MakePathRelativeTo(filename, CleanUpBaseDir));
						if (File.Exists(DestFilename))
						{
							Log.TraceInformation("Cleaning up file {0}", DestFilename);
							SafeDeleteFile(DestFilename, false);
						}
					}
				}

				// Directory clean up code (Build/Android/src)
				try
				{
					IEnumerable<string> BaseDirectories = Directory.EnumerateDirectories(CleanUpBaseDir, "*", SearchOption.AllDirectories).OrderByDescending(x => x);
					foreach (string directory in BaseDirectories)
					{
						if (Directory.Exists(directory) && Directory.GetFiles(directory, "*.*", SearchOption.AllDirectories).Count() == 0)
						{
							Log.TraceInformation("Cleaning Directory {0} as empty.", directory);
							Directory.Delete(directory, true);
						}
					}
				}
				catch (Exception)
				{
					// likely System.IO.DirectoryNotFoundException, ignore it
				}

				// Directory clean up code (Intermediate/APK/src)
				try
				{
					IEnumerable<string> ImmediateDirectories = Directory.EnumerateDirectories(ImmediateBaseDir, "*", SearchOption.AllDirectories).OrderByDescending(x => x);
					foreach (string directory in ImmediateDirectories)
					{
						if (Directory.Exists(directory) && Directory.GetFiles(directory, "*.*", SearchOption.AllDirectories).Count() == 0)
						{
							Log.TraceInformation("Cleaning Directory {0} as empty.", directory);
							Directory.Delete(directory, true);
						}
					}
				}
				catch (Exception)
				{
					// likely System.IO.DirectoryNotFoundException, ignore it
				}
			}


			// check to see if any "meta information" is newer than last time we build
			string TemplatesHashCode = GenerateTemplatesHashCode(EngineDirectory);
			string CurrentBuildSettings = GetAllBuildSettings(ToolChain, UE4BuildPath, bForDistribution, bMakeSeparateApks, bPackageDataInsideApk, bDisableVerifyOBBOnStartUp, bUseExternalFilesDir, bGradleEnabled, TemplatesHashCode);
			string BuildSettingsCacheFile = Path.Combine(UE4BuildPath, "UEBuildSettings.txt");

			// do we match previous build settings?
			bool bBuildSettingsMatch = true;

			// get application name and whether it changed, needing to force repackage
			string ApplicationDisplayName;
			if (CheckApplicationName(UE4BuildPath, ProjectName, out ApplicationDisplayName))
			{
				bBuildSettingsMatch = false;
				Log.TraceInformation("Application display name is different than last build, forcing repackage.");
			}

			// if the manifest matches, look at other settings stored in a file
			if (bBuildSettingsMatch)
			{
				if (File.Exists(BuildSettingsCacheFile))
				{
					string PreviousBuildSettings = File.ReadAllText(BuildSettingsCacheFile);
					if (PreviousBuildSettings != CurrentBuildSettings)
					{
						bBuildSettingsMatch = false;
						Log.TraceInformation("Previous .apk file(s) were made with different build settings, forcing repackage.");
					}
				}
			}

			// only check input dependencies if the build settings already match (if we don't run gradle, there is no Apk file to check against)
			if (bBuildSettingsMatch && !bSkipGradleBuild)
			{
				// check if so's are up to date against various inputs
				List<string> JavaFiles = new List<string>{
                                                    UE4OBBDataFileName,
                                                    UE4DownloadShimFileName
                                                };
				// Add the generated files too
				JavaFiles.AddRange(from t in templates select t.SourceFile);
				JavaFiles.AddRange(from t in templates select t.DestinationFile);

				bBuildSettingsMatch = CheckDependencies(ToolChain, ProjectName, ProjectDirectory, UE4BuildFilesPath, GameBuildFilesPath,
					EngineDirectory, JavaFiles, CookFlavor, OutputPath, UE4BuildPath, bMakeSeparateApks, bPackageDataInsideApk);

			}

			// Initialize UPL contexts for each architecture enabled
			List<string> NDKArches = new List<string>();
			foreach (string Arch in Arches)
			{
				string NDKArch = GetNDKArch(Arch);
				if (!NDKArches.Contains(NDKArch))
				{
					NDKArches.Add(NDKArch);
				}
			}

			UPL.Init(NDKArches, bForDistribution, EngineDirectory, UE4BuildPath, ProjectDirectory, Configuration.ToString());

			IEnumerable<Tuple<string, string, string>> BuildList = null;

			if (!bBuildSettingsMatch)
			{
				BuildList = from Arch in Arches
							from GPUArch in GPUArchitectures
							let manifest = GenerateManifest(ToolChain, ProjectName, InTargetType, EngineDirectory, bForDistribution, bPackageDataInsideApk, GameBuildFilesPath, RequiresOBB(bDisallowPackagingDataInApk, ObbFileLocation), bDisableVerifyOBBOnStartUp, Arch, GPUArch, CookFlavor, bUseExternalFilesDir, Configuration.ToString(), SDKLevelInt, bSkipGradleBuild)
							select Tuple.Create(Arch, GPUArch, manifest);
			}
			else
			{
				BuildList = from Arch in Arches
							from GPUArch in GPUArchitectures
							let manifestFile = Path.Combine(IntermediateAndroidPath, Arch + "_" + GPUArch + "_AndroidManifest.xml")
							let manifest = GenerateManifest(ToolChain, ProjectName, InTargetType, EngineDirectory, bForDistribution, bPackageDataInsideApk, GameBuildFilesPath, RequiresOBB(bDisallowPackagingDataInApk, ObbFileLocation), bDisableVerifyOBBOnStartUp, Arch, GPUArch, CookFlavor, bUseExternalFilesDir, Configuration.ToString(), SDKLevelInt, bSkipGradleBuild)
							let OldManifest = File.Exists(manifestFile) ? File.ReadAllText(manifestFile) : ""
							where manifest != OldManifest
							select Tuple.Create(Arch, GPUArch, manifest);
			}

			// Now we have to spin over all the arch/gpu combinations to make sure they all match
			int BuildListComboTotal = BuildList.Count();
			if (BuildListComboTotal == 0)
			{
				Log.TraceInformation("Output .apk file(s) are up to date (dependencies and build settings are up to date)");
				return;
			}


			// Once for all arches code:

			// make up a dictionary of strings to replace in xml files (strings.xml)
			Dictionary<string, string> Replacements = new Dictionary<string, string>();
			Replacements.Add("${EXECUTABLE_NAME}", ApplicationDisplayName);

			if (!bIncrementalPackage)
			{
				// Wipe the Intermediate/Build/APK directory first, except for dexedLibs, because Google Services takes FOREVER to predex, and it almost never changes
				// so allow the ANT checking to win here - if this grows a bit with extra libs, it's fine, it _should_ only pull in dexedLibs it needs
				Log.TraceInformation("Performing complete package - wiping {0}, except for predexedLibs", UE4BuildPath);
				DeleteDirectory(UE4BuildPath, "dexedLibs");
			}

			// If we are packaging for Amazon then we need to copy the  file to the correct location
			Log.TraceInformation("bPackageDataInsideApk = {0}", bPackageDataInsideApk);
			if (bPackageDataInsideApk)
			{
				Log.TraceInformation("Obb location {0}", ObbFileLocation);
				string ObbFileDestination = UE4BuildPath + "/assets";
				Log.TraceInformation("Obb destination location {0}", ObbFileDestination);
				if (File.Exists(ObbFileLocation))
				{
					Directory.CreateDirectory(UE4BuildPath);
					Directory.CreateDirectory(ObbFileDestination);
					Log.TraceInformation("Obb file exists...");
					string DestFileName = Path.Combine(ObbFileDestination, "main.obb.png"); // Need a rename to turn off compression
					string SrcFileName = ObbFileLocation;
					if (!File.Exists(DestFileName) || File.GetLastWriteTimeUtc(DestFileName) < File.GetLastWriteTimeUtc(SrcFileName))
					{
						Log.TraceInformation("Copying {0} to {1}", SrcFileName, DestFileName);
						File.Copy(SrcFileName, DestFileName);
					}
				}
			}
			else // try to remove the file it we aren't packaging inside the APK
			{
				string ObbFileDestination = UE4BuildPath + "/assets";
				string DestFileName = Path.Combine(ObbFileDestination, "main.obb.png");
				SafeDeleteFile(DestFileName);
			}

			// See if we need to stage a UE4CommandLine.txt file in assets
			string CommandLineSourceFileName = Path.Combine(Path.GetDirectoryName(ObbFileLocation), Path.GetFileNameWithoutExtension(ObbFileLocation), "UE4CommandLine.txt");
			string CommandLineDestFileName = Path.Combine(UE4BuildAssetsPath, "UE4CommandLine.txt");
			if (File.Exists(CommandLineSourceFileName))
			{
				Directory.CreateDirectory(UE4BuildPath);
				Directory.CreateDirectory(UE4BuildAssetsPath);
				Console.WriteLine("UE4CommandLine.txt exists...");
				bool bDestFileAlreadyExists = File.Exists(CommandLineDestFileName);
				if (!bDestFileAlreadyExists || File.GetLastWriteTimeUtc(CommandLineDestFileName) < File.GetLastWriteTimeUtc(CommandLineSourceFileName))
				{
					Console.WriteLine("Copying {0} to {1}", CommandLineSourceFileName, CommandLineDestFileName);
					if (bDestFileAlreadyExists)
					{
						SafeDeleteFile(CommandLineDestFileName, false);
					}
					File.Copy(CommandLineSourceFileName, CommandLineDestFileName);
				}
			}
			else // try to remove the file if we aren't packaging one
			{
				SafeDeleteFile(CommandLineDestFileName);
			}

			string AARExtractListFilename = Path.Combine(UE4BuildPath, "JavaLibs", "AARExtractList.txt");
			if (bGradleEnabled)
			{
				//Need to clear out JavaLibs if last run was with Ant
				if (File.Exists(AARExtractListFilename))
				{
					Log.TraceInformation("Cleanup up JavaLibs from previous Ant packaging");
					DeleteDirectory(Path.Combine(UE4BuildPath, "JavaLibs"));
				}
			}

			//Copy build files to the intermediate folder in this order (later overrides earlier):
			//	- Shared Engine
			//  - Shared Engine NoRedist (for Epic secret files)
			//  - Game
			//  - Game NoRedist (for Epic secret files)
			CopyFileDirectory(UE4BuildFilesPath, UE4BuildPath, Replacements);
			CopyFileDirectory(UE4BuildFilesPath + "/NotForLicensees", UE4BuildPath, Replacements);
			CopyFileDirectory(UE4BuildFilesPath + "/NoRedist", UE4BuildPath, Replacements);
			if (!bGradleEnabled)
			{
				CopyFileDirectory(Path.Combine(EngineDirectory, "Build", "Android", "Legacy"), UE4BuildPath, Replacements);
			}
			CopyFileDirectory(GameBuildFilesPath, UE4BuildPath, Replacements);
			CopyFileDirectory(GameBuildFilesPath + "/NotForLicensees", UE4BuildPath, Replacements);
			CopyFileDirectory(GameBuildFilesPath + "/NoRedist", UE4BuildPath, Replacements);

			if (!bGradleEnabled)
			{
				//Extract AAR and Jar files with dependencies if not using Gradle
				ExtractAARAndJARFiles(EngineDirectory, UE4BuildPath, NDKArches, PackageName, AARExtractListFilename);
			}
			else
			{
				//Generate Gradle AAR dependencies
				GenerateGradleAARImports(EngineDirectory, UE4BuildPath, NDKArches);
			}

			//Now validate GooglePlay app_id if enabled
			ValidateGooglePlay(UE4BuildPath);

			//determine which orientation requirements this app has
			bool bNeedLandscape = false;
			bool bNeedPortrait = false;
			DetermineScreenOrientationRequirements(NDKArches[0], out bNeedPortrait, out bNeedLandscape);

			//Now keep the splash screen images matching orientation requested
			PickSplashScreenOrientation(UE4BuildPath, bNeedPortrait, bNeedLandscape);
			
			//Now package the app based on Daydream packaging settings 
			PackageForDaydream(UE4BuildPath);
			
			//Similarly, keep only the downloader screen image matching the orientation requested
			PickDownloaderScreenOrientation(UE4BuildPath, bNeedPortrait, bNeedLandscape);

			// at this point, we can write out the cached build settings to compare for a next build
			File.WriteAllText(BuildSettingsCacheFile, CurrentBuildSettings);

			// at this point, we can write out the cached build settings to compare for a next build
			File.WriteAllText(BuildSettingsCacheFile, CurrentBuildSettings);

			///////////////
			// in case the game had an AndroidManifest.xml file, we overwrite it now with the generated one
			//File.WriteAllText(ManifestFile, NewManifest);
			///////////////

			Log.TraceInformation("\n===={0}====PREPARING NATIVE CODE=================================================================", DateTime.Now.ToString());
			bool HasNDKPath = File.Exists(NDKBuildPath);

			// get Ant verbosity level
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			string AntVerbosity;
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "AntVerbosity", out AntVerbosity);

			// use Gradle for compile/package
			string UE4BuildGradlePath = Path.Combine(UE4BuildPath, "gradle");
			string UE4BuildGradleAppPath = Path.Combine(UE4BuildGradlePath, "app");
			string UE4BuildGradleMainPath = Path.Combine(UE4BuildGradleAppPath, "src", "main");
			string CompileSDKVersion = SDKAPILevel.Replace("android-", "");

			foreach (Tuple<string, string, string> build in BuildList)
			{
				string Arch = build.Item1;
				string GPUArchitecture = build.Item2;
				string Manifest = build.Item3;
				string NDKArch = GetNDKArch(Arch);

				// Write the manifest to the correct locations (cache and real)
				String ManifestFile = Path.Combine(IntermediateAndroidPath, Arch + "_" + GPUArchitecture + "_AndroidManifest.xml");
				File.WriteAllText(ManifestFile, Manifest);
				ManifestFile = Path.Combine(UE4BuildPath, "AndroidManifest.xml");
				File.WriteAllText(ManifestFile, Manifest);

				// copy prebuild plugin files
				UPL.ProcessPluginNode(NDKArch, "prebuildCopies", "");

				XDocument AdditionalBuildPathFilesDoc = new XDocument(new XElement("files"));
				UPL.ProcessPluginNode(NDKArch, "additionalBuildPathFiles", "", ref AdditionalBuildPathFilesDoc);

				if (!bGradleEnabled)
				{
					// update metadata files (like project.properties, build.xml) if we are missing a build.xml or if we just overwrote project.properties with a bad version in it (from game/engine dir)
					UpdateProjectProperties(ToolChain, UE4BuildPath, ProjectName);

					// modify the generated build.xml before the final include
					UpdateBuildXML(Arch, NDKArch, EngineDirectory, UE4BuildPath);

					// Write Crashlytics data if enabled (not needed for Gradle)
					if (CrashlyticsPluginEnabled)
					{
						Trace.TraceInformation("Writing Crashlytics resources");
						WriteCrashlyticsResources(Path.Combine(ProjectDirectory, "Build", "Android"), PackageName, ApplicationDisplayName, bSkipGradleBuild, Arch);
					}
				}

				// Generate the OBBData.java file again in case architecture has different store version
				WriteJavaOBBDataFile(UE4OBBDataFileName, PackageName, RequiredOBBFiles, CookFlavor, bPackageDataInsideApk, Arch);

				// update GameActivity.java and GameApplication.java if out of date
				UpdateGameActivity(Arch, NDKArch, EngineDirectory, UE4BuildPath);
				UpdateGameApplication(Arch, NDKArch, EngineDirectory, UE4BuildPath, bGradleEnabled);

				// we don't actually need the SO for the bSkipGradleBuild case
				string FinalSOName = null;
				string DestApkDirectory = Path.Combine(ProjectDirectory, "Binaries/Android");
				string DestApkName = null;
				if (bSkipGradleBuild)
				{
					FinalSOName = OutputPath;
					if (!File.Exists(FinalSOName))
					{
						Log.TraceWarning("Did not find compiled .so [{0}]", FinalSOName);
					}
				}
				else
				{
					string SourceSOName = AndroidToolChain.InlineArchName(OutputPath, Arch, GPUArchitecture);
					// if the source binary was UE4Game, replace it with the new project name, when re-packaging a binary only build
					string ApkFilename = Path.GetFileNameWithoutExtension(OutputPath).Replace("UE4Game", ProjectName);
					DestApkName = Path.Combine(DestApkDirectory, ApkFilename + ".apk");

					// As we are always making seperate APKs we need to put the architecture into the name
					DestApkName = AndroidToolChain.InlineArchName(DestApkName, Arch, GPUArchitecture);

					if (!File.Exists(SourceSOName))
					{
						throw new BuildException("Can't make an APK without the compiled .so [{0}]", SourceSOName);
					}
					if (!Directory.Exists(UE4BuildPath + "/jni"))
					{
						throw new BuildException("Can't make an APK without the jni directory [{0}/jni]", UE4BuildFilesPath);
					}

					if (bGradleEnabled)
					{
						string JniDir = UE4BuildPath + "/jni/" + NDKArch;
						FinalSOName = JniDir + "/libUE4.so";

						// clear out libs directory like ndk-build would have
						string LibsDir = Path.Combine(UE4BuildPath, "libs");
						DeleteDirectory(LibsDir);
						MakeDirectoryIfRequired(LibsDir);

						// check to see if libUE4.so needs to be copied
						if (BuildListComboTotal > 1 || FilesAreDifferent(SourceSOName, FinalSOName))
						{
							Log.TraceInformation("\nCopying new .so {0} file to jni folder...", SourceSOName);
							Directory.CreateDirectory(JniDir);
							// copy the binary to the standard .so location
							File.Copy(SourceSOName, FinalSOName, true);
							File.SetLastWriteTimeUtc(FinalSOName, File.GetLastWriteTimeUtc(SourceSOName));
						}
					}
					else
					{
						if (HasNDKPath)
						{
							string LibDir = UE4BuildPath + "/jni/" + NDKArch;
							FinalSOName = LibDir + "/libUE4.so";

							// check to see if libUE4.so needs to be copied
							if (BuildListComboTotal > 1 || FilesAreDifferent(SourceSOName, FinalSOName))
							{
								Log.TraceInformation("\nCopying new .so {0} file to jni folder...", SourceSOName);
								Directory.CreateDirectory(LibDir);
								// copy the binary to the standard .so location
								File.Copy(SourceSOName, FinalSOName, true);
							}

							// remove any read only flags
							FileInfo DestFileInfo2 = new FileInfo(FinalSOName);
							DestFileInfo2.Attributes = DestFileInfo2.Attributes & ~FileAttributes.ReadOnly;
							File.SetLastWriteTimeUtc(FinalSOName, File.GetLastWriteTimeUtc(SourceSOName));

							// run ndk-build for Ant (will stage libUE4.so into libs)
							string LibSOName = UE4BuildPath + "/libs/" + NDKArch + "/libUE4.so";

							// always delete libs up to this point so fat binaries and incremental builds work together (otherwise we might end up with multiple
							// so files in an apk that doesn't want them)
							// note that we don't want to delete all libs, just the ones we copied
							TimeSpan Diff = File.GetLastWriteTimeUtc(LibSOName) - File.GetLastWriteTimeUtc(FinalSOName);
							if (!File.Exists(LibSOName) || Diff.TotalSeconds < -1 || Diff.TotalSeconds > 1)
							{
								foreach (string Lib in Directory.EnumerateFiles(UE4BuildPath + "/libs", "libUE4*.so", SearchOption.AllDirectories))
								{
									File.Delete(Lib);
								}

								string CommandLine = "APP_ABI=\"" + NDKArch + " " + "\"";
								if (!bForDistribution)
								{
									CommandLine += " NDK_DEBUG=1";
								}
								RunCommandLineProgramWithException(UE4BuildPath, NDKBuildPath, CommandLine, "Preparing native code for debugging...", true);

								File.SetLastWriteTimeUtc(LibSOName, File.GetLastWriteTimeUtc(FinalSOName));
							}
						}
						else
						{
							// if no NDK, we don't need any of the debugger stuff, so we just copy the .so to where it will end up
							FinalSOName = UE4BuildPath + "/libs/" + NDKArch + "/libUE4.so";

							// check to see if libUE4.so needs to be copied
							if (BuildListComboTotal > 1 || FilesAreDifferent(SourceSOName, FinalSOName))
							{
								Log.TraceInformation("\nCopying .so {0} file to jni folder...", SourceSOName);
								Directory.CreateDirectory(Path.GetDirectoryName(FinalSOName));
								File.Copy(SourceSOName, FinalSOName, true);
							}
						}
					}

					// remove any read only flags
					FileInfo DestFileInfo = new FileInfo(FinalSOName);
					DestFileInfo.Attributes = DestFileInfo.Attributes & ~FileAttributes.ReadOnly;
					File.SetLastWriteTimeUtc(FinalSOName, File.GetLastWriteTimeUtc(SourceSOName));
				}

				// after ndk-build is called, we can now copy in the stl .so (ndk-build deletes old files)
				// copy libgnustl_shared.so to library (use 4.8 if possible, otherwise 4.6)
				CopySTL(ToolChain, UE4BuildPath, Arch, NDKArch, bForDistribution, bGradleEnabled);
				CopyGfxDebugger(UE4BuildPath, Arch, NDKArch);
				CopyVulkanValidationLayers(UE4BuildPath, Arch, NDKArch, Configuration.ToString());

				// copy postbuild plugin files
				UPL.ProcessPluginNode(NDKArch, "resourceCopies", "");

				CreateAdditonalBuildPathFiles(NDKArch, UE4BuildPath, AdditionalBuildPathFilesDoc);

				Log.TraceInformation("\n===={0}====PERFORMING FINAL APK PACKAGE OPERATION================================================", DateTime.Now.ToString());


				if (!bGradleEnabled)
				{
					// use Ant for compile/package
					string AntBuildType = "debug";
					string AntOutputSuffix = "-debug";
					if (bForDistribution)
					{
						// Generate the Proguard file contents and write it
						string ProguardContents = GenerateProguard(NDKArch, UE4BuildFilesPath, GameBuildFilesPath);
						string ProguardFilename = Path.Combine(UE4BuildPath, "proguard-project.txt");
						SafeDeleteFile(ProguardFilename);
						File.WriteAllText(ProguardFilename, ProguardContents);

						// this will write out ant.properties with info needed to sign a distribution build
						PrepareToSignApk(UE4BuildPath);
						AntBuildType = "release";
						AntOutputSuffix = "-release";
					}

					// Use ant to build the .apk file
					string AntOptions = AntBuildType + " -Djava.source=1.7 -Djava.target=1.7";
					string ShellExecutable = BuildHostPlatform.Current.Shell.FullName;
					string ShellParametersBegin = (BuildHostPlatform.Current.ShellType == ShellType.Sh) ? "-c '" : "/c ";
					string ShellParametersEnd = (BuildHostPlatform.Current.ShellType == ShellType.Sh) ? "'" : "";
					switch (AntVerbosity.ToLower())
					{
						default:
						case "quiet":
							if (RunCommandLineProgramAndReturnResult(UE4BuildPath, ShellExecutable, ShellParametersBegin + "\"" + GetAntPath() + "\" -quiet " + AntOptions + ShellParametersEnd, "Making .apk with Ant... (note: it's safe to ignore javac obsolete warnings)") != 0)
							{
								RunCommandLineProgramWithException(UE4BuildPath, ShellExecutable, ShellParametersBegin + "\"" + GetAntPath() + "\" " + AntOptions + ShellParametersEnd, "Making .apk with Ant again to show errors");
							}
							break;

						case "normal":
							RunCommandLineProgramWithException(UE4BuildPath, ShellExecutable, ShellParametersBegin + "\"" + GetAntPath() + "\" " + AntOptions + ShellParametersEnd, "Making .apk with Ant again to show errors");
							break;

						case "verbose":
							RunCommandLineProgramWithException(UE4BuildPath, ShellExecutable, ShellParametersBegin + "\"" + GetAntPath() + "\" -verbose " + AntOptions + ShellParametersEnd, "Making .apk with Ant again to show errors");
							break;
					}

					// upload Crashlytics symbols if plugin enabled and using build machine
					if (CrashlyticsPluginEnabled && bIsBuildMachine)
					{
						AntOptions = "crashlytics-upload-symbols";
						RunCommandLineProgramWithException(UE4BuildPath, ShellExecutable, ShellParametersBegin + "\"" + GetAntPath() + "\" " + AntOptions + ShellParametersEnd, "Uploading Crashlytics symbols");
					}

					// make sure destination exists
					Directory.CreateDirectory(Path.GetDirectoryName(DestApkName));

					// now copy to the final location
					File.Copy(UE4BuildPath + "/bin/" + ProjectName + AntOutputSuffix + ".apk", DestApkName, true);
				}
				else
				{
					// check if any plugins want to increase the required compile SDK version
					string CompileSDKMin = UPL.ProcessPluginNode(NDKArch, "minimumSDKAPI", "");
					if (CompileSDKMin != "")
					{
						int CompileSDKVersionInt;
						if (!int.TryParse(CompileSDKVersion, out CompileSDKVersionInt))
						{
							CompileSDKVersionInt = 23;
						}

						bool bUpdatedCompileSDK = false;
						string[] CompileSDKLines = CompileSDKMin.Split(new[] { "\r\n", "\r", "\n" }, StringSplitOptions.None);
						foreach (string CompileLine in CompileSDKLines)
						{
							string VersionString = CompileLine.Replace("android-", "");
							int VersionInt;
							if (int.TryParse(CompileLine, out VersionInt))
							{
								if (VersionInt > CompileSDKVersionInt)
								{
									CompileSDKVersionInt = VersionInt;
									bUpdatedCompileSDK = true;
								}
							}
						}

						if (bUpdatedCompileSDK)
						{
							CompileSDKVersion = CompileSDKVersionInt.ToString();
							Log.TraceInformation("Building Java with SDK API Level 'android-{0}' due to enabled plugin requirements", CompileSDKVersion);
						}
					}

					// stage files into gradle app directory
					string GradleManifest = Path.Combine(UE4BuildGradleMainPath, "AndroidManifest.xml");
					MakeDirectoryIfRequired(GradleManifest);
					File.Copy(Path.Combine(UE4BuildPath, "AndroidManifest.xml"), GradleManifest, true);

					string[] Excludes;
					switch (NDKArch)
					{
						default:
						case "armeabi-v7a":
							Excludes = new string[] { "arm64-v8a", "x86", "x86-64" };
							break;

						case "arm64-v8a":
							Excludes = new string[] { "armeabi-v7a", "x86", "x86-64" };
							break;

						case "x86":
							Excludes = new string[] { "armeabi-v7a", "arm64-v8a", "x86-64" };
							break;

						case "x86_64":
							Excludes = new string[] { "armeabi-v7a", "arm64-v8a", "x86" };
							break;
					}

					CleanCopyDirectory(Path.Combine(UE4BuildPath, "jni"), Path.Combine(UE4BuildGradleMainPath, "jniLibs"), Excludes);  // has debug symbols
					CleanCopyDirectory(Path.Combine(UE4BuildPath, "libs"), Path.Combine(UE4BuildGradleMainPath, "libs"), Excludes);

					CleanCopyDirectory(Path.Combine(UE4BuildPath, "assets"), Path.Combine(UE4BuildGradleMainPath, "assets"));
					CleanCopyDirectory(Path.Combine(UE4BuildPath, "res"), Path.Combine(UE4BuildGradleMainPath, "res"));
					CleanCopyDirectory(Path.Combine(UE4BuildPath, "src"), Path.Combine(UE4BuildGradleMainPath, "java"));

					// do any plugin requested copies
					UPL.ProcessPluginNode(NDKArch, "gradleCopies", "");

					// get min and target SDK versions
					int MinSDKVersion = 0;
					int TargetSDKVersion = 0;
					int NDKLevelInt = 0;
					GetMinTargetSDKVersions(ToolChain, Arch, UPL, NDKArch, out MinSDKVersion, out TargetSDKVersion, out NDKLevelInt);
					
					// move JavaLibs into subprojects
					string JavaLibsDir = Path.Combine(UE4BuildPath, "JavaLibs");
					PrepareJavaLibsForGradle(JavaLibsDir, UE4BuildGradlePath, MinSDKVersion.ToString(), TargetSDKVersion.ToString(), CompileSDKVersion, BuildToolsVersion);

					// Create local.properties
					String LocalPropertiesFilename = Path.Combine(UE4BuildGradlePath, "local.properties");
					StringBuilder LocalProperties = new StringBuilder();
					LocalProperties.AppendLine(string.Format("ndk.dir={0}", Environment.GetEnvironmentVariable("NDKROOT").Replace("\\", "/")));
					LocalProperties.AppendLine(string.Format("sdk.dir={0}", Environment.GetEnvironmentVariable("ANDROID_HOME").Replace("\\", "/")));
					File.WriteAllText(LocalPropertiesFilename, LocalProperties.ToString());

					CreateGradlePropertiesFiles(Arch, MinSDKVersion, TargetSDKVersion, CompileSDKVersion, BuildToolsVersion, PackageName, DestApkName, NDKArch,
						UE4BuildFilesPath, GameBuildFilesPath, UE4BuildGradleAppPath, UE4BuildPath, UE4BuildGradlePath, bForDistribution, bSkipGradleBuild);

					if (!bSkipGradleBuild)
					{
						string GradleScriptPath = Path.Combine(UE4BuildGradlePath, "gradlew");
						if (Utils.IsRunningOnMono)
						{
							// fix permissions for Mac/Linux
							RunCommandLineProgramWithException(UE4BuildGradlePath, "/bin/sh", string.Format("-c 'chmod 0755 \"{0}\"'", GradleScriptPath.Replace("'", "'\"'\"'")), "Fix gradlew permissions");
						}
						else
						{
							if (CreateRunGradle(UE4BuildGradlePath))
							{
								GradleScriptPath = Path.Combine(UE4BuildGradlePath, "rungradle.bat");
							}
							else
							{
								GradleScriptPath = Path.Combine(UE4BuildGradlePath, "gradlew.bat");
							}
						}

						string GradleBuildType = bForDistribution ? ":app:assembleRelease" : ":app:assembleDebug";

						// collect optional additional Gradle parameters from plugins
						string GradleOptions = UPL.ProcessPluginNode(NDKArch, "gradleParameters", GradleBuildType); //  "--stacktrace --debug " + GradleBuildType);
						string GradleSecondCallOptions = UPL.ProcessPluginNode(NDKArch, "gradleSecondCallParameters", "");

						// check for Android Studio project, call Gradle if doesn't exist (assume user will build with Android Studio)
						string GradleAppImlFilename = Path.Combine(UE4BuildGradlePath, "app.iml");
						if (!File.Exists(GradleAppImlFilename))
						{
							// make sure destination exists
							Directory.CreateDirectory(Path.GetDirectoryName(DestApkName));

							// Use gradle to build the .apk file
							string ShellExecutable = Utils.IsRunningOnMono ? "/bin/sh" : "cmd.exe";
							string ShellParametersBegin = Utils.IsRunningOnMono ? "-c '" : "/c ";
							string ShellParametersEnd = Utils.IsRunningOnMono ? "'" : "";
							RunCommandLineProgramWithExceptionAndFiltering(UE4BuildGradlePath, ShellExecutable, ShellParametersBegin + "\"" + GradleScriptPath + "\" " + GradleOptions + ShellParametersEnd, "Making .apk with Gradle...");

							if (GradleSecondCallOptions != "")
							{
								RunCommandLineProgramWithExceptionAndFiltering(UE4BuildGradlePath, ShellExecutable, ShellParametersBegin + "\"" + GradleScriptPath + "\" " + GradleSecondCallOptions + ShellParametersEnd, "Additional Gradle steps...");
							}
							
							// For build machine run a clean afterward to clean up intermediate files (does not remove final APK)
							if (bIsBuildMachine)
							{
								//GradleOptions = "tasks --all";
								//RunCommandLineProgramWithException(UE4BuildGradlePath, ShellExecutable, ShellParametersBegin + "\"" + GradleScriptPath + "\" " + GradleOptions + ShellParametersEnd, "Listing all tasks...");

								GradleOptions = "clean";
								RunCommandLineProgramWithExceptionAndFiltering(UE4BuildGradlePath, ShellExecutable, ShellParametersBegin + "\"" + GradleScriptPath + "\" " + GradleOptions + ShellParametersEnd, "Cleaning Gradle intermediates...");
							}
						}
						else
						{
							Log.TraceInformation("=============================================================================================");
							Log.TraceInformation("Android Studio project found, skipping Gradle; complete creation of APK in Android Studio!!!!");
							Log.TraceInformation("Delete '{0} if you want to have UnrealBuildTool run Gradle for future runs.", GradleAppImlFilename);
							Log.TraceInformation("=============================================================================================");
						}
					}

					bool bBuildWithHiddenSymbolVisibility = false;
					bool bSaveSymbols = false;
					Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bBuildWithHiddenSymbolVisibility", out bBuildWithHiddenSymbolVisibility);
					Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bSaveSymbols", out bSaveSymbols);
					bSaveSymbols = true;
					if (bSaveSymbols || (Configuration == UnrealTargetConfiguration.Shipping && bBuildWithHiddenSymbolVisibility))
					{
						// Copy .so with symbols to 
						int StoreVersion = GetStoreVersion(Arch);
						string SymbolSODirectory = Path.Combine(DestApkDirectory, ProjectName + "_Symbols_v" + StoreVersion + "/" + ProjectName + Arch + GPUArchitecture);
						string SymbolifiedSOPath = Path.Combine(SymbolSODirectory, Path.GetFileName(FinalSOName));
						MakeDirectoryIfRequired(SymbolifiedSOPath);
						Log.TraceInformation("Writing symbols to {0}", SymbolifiedSOPath);

						File.Copy(FinalSOName, SymbolifiedSOPath, true);
					}
				}
			}
		}

		private void PrepareToSignApk(string BuildPath)
		{
			// ini file to get settings from
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			string KeyAlias, KeyStore, KeyStorePassword, KeyPassword;
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "KeyAlias", out KeyAlias);
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "KeyStore", out KeyStore);
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "KeyStorePassword", out KeyStorePassword);
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "KeyPassword", out KeyPassword);

			if (string.IsNullOrEmpty(KeyAlias) || string.IsNullOrEmpty(KeyStore) || string.IsNullOrEmpty(KeyStorePassword))
			{
				throw new BuildException("DistributionSigning settings are not all set. Check the DistributionSettings section in the Android tab of Project Settings");
			}

			string[] AntPropertiesLines = new string[4];
			AntPropertiesLines[0] = "key.store=" + KeyStore;
			AntPropertiesLines[1] = "key.alias=" + KeyAlias;
			AntPropertiesLines[2] = "key.store.password=" + KeyStorePassword;
			AntPropertiesLines[3] = "key.alias.password=" + ((string.IsNullOrEmpty(KeyPassword) || KeyPassword == "_sameaskeystore_") ? KeyStorePassword : KeyPassword);

			// now write out the properties
			File.WriteAllLines(Path.Combine(BuildPath, "ant.properties"), AntPropertiesLines);
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
			IEnumerable<ReceiptProperty> Results = Receipt.AdditionalProperties.Where(x => x.Name == "AndroidPlugin");
			foreach (ReceiptProperty Property in Results)
			{
				// Keep only unique paths
				string PluginPath = Property.Value;
				if (PluginExtras.FirstOrDefault(x => x == PluginPath) == null)
				{
					PluginExtras.Add(PluginPath);
					Log.TraceInformation("AndroidPlugin: {0}", PluginPath);
				}
			}
			return PluginExtras;
		}

		public override bool PrepTargetForDeployment(TargetReceipt Receipt)
		{
			DirectoryReference ProjectDirectory = DirectoryReference.FromFile(Receipt.ProjectFile) ?? UnrealBuildTool.EngineDirectory;
			string TargetName = (Receipt.ProjectFile == null ? Receipt.TargetName : Receipt.ProjectFile.GetFileNameWithoutAnyExtensions());

			AndroidToolChain ToolChain = ((AndroidPlatform)UEBuildPlatform.GetBuildPlatform(Receipt.Platform)).CreateTempToolChainForProject(Receipt.ProjectFile) as AndroidToolChain;

			// get the receipt
			SetAndroidPluginData(ToolChain.GetAllArchitectures(), CollectPluginDataPaths(Receipt));

			bool bShouldCompileAsDll = Receipt.HasValueForAdditionalProperty("CompileAsDll", "true");

			SavePackageInfo(TargetName, ProjectDirectory.FullName, Receipt.TargetType, bShouldCompileAsDll);

			// Get the output paths
			BuildProductType ProductType = bShouldCompileAsDll ? BuildProductType.DynamicLibrary : BuildProductType.Executable;
			List<FileReference> OutputPaths = Receipt.BuildProducts.Where(x => x.Type == ProductType).Select(x => x.Path).ToList();
			if (OutputPaths.Count < 1)
			{
				throw new BuildException("Target file does not contain either executable or dynamic library .so");
			}

			// we need to strip architecture from any of the output paths
			string BaseSoName = ToolChain.RemoveArchName(OutputPaths[0].FullName);

			// make an apk at the end of compiling, so that we can run without packaging (debugger, cook on the fly, etc)
			string RelativeEnginePath = UnrealBuildTool.EngineDirectory.MakeRelativeTo(DirectoryReference.GetCurrentDirectory());

			MakeApk(ToolChain, TargetName, Receipt.TargetType, ProjectDirectory.FullName, BaseSoName, RelativeEnginePath, bForDistribution: false, CookFlavor: "", Configuration: Receipt.Configuration,
				bMakeSeparateApks: ShouldMakeSeparateApks(), bIncrementalPackage: true, bDisallowPackagingDataInApk: false, bDisallowExternalFilesDir: true, bSkipGradleBuild: bShouldCompileAsDll);

			// if we made any non-standard .apk files, the generated debugger settings may be wrong
			if (ShouldMakeSeparateApks() && (OutputPaths.Count > 1 || !OutputPaths[0].FullName.Contains("-armv7-es2")))
			{
				Log.TraceInformation("================================================================================================================================");
				Log.TraceInformation("Non-default apk(s) have been made: If you are debugging, you will need to manually select one to run in the debugger properties!");
				Log.TraceInformation("================================================================================================================================");
			}
			return true;
		}

		// Store generated package name in a text file for builds that do not generate an apk file 
		public bool SavePackageInfo(string TargetName, string ProjectDirectory, TargetType InTargetType, bool bIsEmbedded)
		{
			string PackageName = GetPackageName(TargetName);
			string DestPackageNameFileName = Path.Combine(ProjectDirectory, "Binaries", "Android", "packageInfo.txt");

			string[] PackageInfoSource = new string[4];
			PackageInfoSource[0] = PackageName;
			PackageInfoSource[1] = GetStoreVersion("").ToString();
			PackageInfoSource[2] = GetVersionDisplayName(bIsEmbedded);
			PackageInfoSource[3] = string.Format("name='com.epicgames.ue4.GameActivity.AppType' value='{0}'", InTargetType == TargetType.Game ? "" : InTargetType.ToString());

			Log.TraceInformation("Writing packageInfo pkgName:{0} storeVersion:{1} versionDisplayName:{2} to {3}", PackageInfoSource[0], PackageInfoSource[1], PackageInfoSource[2], DestPackageNameFileName);

			string DestDirectory = Path.GetDirectoryName(DestPackageNameFileName);
			if (!Directory.Exists(DestDirectory))
			{
				Directory.CreateDirectory(DestDirectory);
			}

			File.WriteAllLines(DestPackageNameFileName, PackageInfoSource);

			return true;
		}

		public static bool ShouldMakeSeparateApks()
		{
			// @todo android fat binary: Currently, there isn't much utility in merging multiple .so's into a single .apk except for debugging,
			// but we can't properly handle multiple GPU architectures in a single .apk, so we are disabling the feature for now
			// The user will need to manually select the apk to run in their Visual Studio debugger settings (see Override APK in TADP, for instance)
			// If we change this, pay attention to <OverrideAPKPath> in AndroidProjectGenerator
			return true;

			// check to see if the project wants separate apks
			// 			ConfigCacheIni Ini = nGetConfigCacheIni("Engine");
			// 			bool bSeparateApks = false;
			// 			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bSplitIntoSeparateApks", out bSeparateApks);
			// 
			// 			return bSeparateApks;
		}

		public bool PrepForUATPackageOrDeploy(FileReference ProjectFile, string ProjectName, DirectoryReference ProjectDirectory, string ExecutablePath, string EngineDirectory, bool bForDistribution, string CookFlavor, UnrealTargetConfiguration Configuration, bool bIsDataDeploy, bool bSkipGradleBuild)
		{
			//Log.TraceInformation("$$$$$$$$$$$$$$ PrepForUATPackageOrDeploy $$$$$$$$$$$$$$$$$");

			TargetType Type = TargetType.Game;
			if (CookFlavor.EndsWith("Client"))
			{
				Type = TargetType.Client;			
			}
			else if (CookFlavor.EndsWith("Server"))
			{
				Type = TargetType.Server;
			}

			// note that we cannot allow the data packaged into the APK if we are doing something like Launch On that will not make an obb
			// file and instead pushes files directly via deploy
			AndroidToolChain ToolChain = new AndroidToolChain(ProjectFile, false, null, null);

			SavePackageInfo(ProjectName, ProjectDirectory.FullName, Type, bSkipGradleBuild);

			MakeApk(ToolChain, ProjectName, Type, ProjectDirectory.FullName, ExecutablePath, EngineDirectory, bForDistribution: bForDistribution, CookFlavor: CookFlavor, Configuration: Configuration,
				bMakeSeparateApks: ShouldMakeSeparateApks(), bIncrementalPackage: false, bDisallowPackagingDataInApk: bIsDataDeploy, bDisallowExternalFilesDir: !bForDistribution || bIsDataDeploy, bSkipGradleBuild:bSkipGradleBuild);
			return true;
		}

		public static void OutputReceivedDataEventHandler(Object Sender, DataReceivedEventArgs Line)
		{
			if ((Line != null) && (Line.Data != null))
			{
				Log.TraceInformation(Line.Data);
			}
		}

		private void UpdateBuildXML(string UE4Arch, string NDKArch, string EngineDir, string UE4BuildPath)
		{
			string SourceFilename = Path.Combine(UE4BuildPath, "build.xml");
			string DestFilename = SourceFilename;

			Dictionary<string, string> Replacements = new Dictionary<string, string>{
			  { "<import file=\"${sdk.dir}/tools/ant/build.xml\" />", UPL.ProcessPluginNode(NDKArch, "buildXmlPropertyAdditions", "")}
			};

			string[] TemplateSrc = File.ReadAllLines(SourceFilename);
			string[] TemplateDest = File.Exists(DestFilename) ? File.ReadAllLines(DestFilename) : null;

			for (int LineIndex = 0; LineIndex < TemplateSrc.Length; ++LineIndex)
			{
				string SrcLine = TemplateSrc[LineIndex];
				bool Changed = false;
				foreach (KeyValuePair<string, string> KVP in Replacements)
				{
					if (SrcLine.Contains(KVP.Key))
					{
						// insert replacement before the <import>
						SrcLine = SrcLine.Replace(KVP.Key, KVP.Value);
						// then add the <import>
						SrcLine += KVP.Key;
						Changed = true;
					}
				}
				if (Changed)
				{
					TemplateSrc[LineIndex] = SrcLine;
				}
			}

			if (TemplateDest == null || TemplateSrc.Length != TemplateDest.Length || !TemplateSrc.SequenceEqual(TemplateDest))
			{
				Log.TraceInformation("\n==== Writing new build.xml file to {0} ====", DestFilename);
				File.WriteAllLines(DestFilename, TemplateSrc);
			}
		}

		private string GenerateTemplatesHashCode(string EngineDir)
		{
			string SourceDirectory = Path.Combine(EngineDir, "Build", "Android", "Java");

			if (!Directory.Exists(SourceDirectory))
			{
				return "badpath";
			}

			MD5 md5 = MD5.Create();
			byte[] TotalHashBytes = null;

			string[] SourceFiles = Directory.GetFiles(SourceDirectory, "*.*", SearchOption.AllDirectories);
			foreach (string Filename in SourceFiles)
			{
				using (FileStream stream = File.OpenRead(Filename))
				{
					byte[] FileHashBytes = md5.ComputeHash(stream);
					if (TotalHashBytes != null)
					{
						int index = 0;
						foreach (byte b in FileHashBytes)
						{
							TotalHashBytes[index] ^= b;
							index++;
						}
					}
					else
					{
						TotalHashBytes = FileHashBytes;
					}
				}
			}

			if (TotalHashBytes != null)
			{
				string HashCode = "";
				foreach (byte b in TotalHashBytes)
				{
					HashCode += b.ToString("x2");
				}
				return HashCode;
			}

			return "empty";
		}

		private void UpdateGameActivity(string UE4Arch, string NDKArch, string EngineDir, string UE4BuildPath)
		{
			string SourceFilename = Path.Combine(EngineDir, "Build", "Android", "Java", "src", "com", "epicgames", "ue4", "GameActivity.java.template");
			string DestFilename = Path.Combine(UE4BuildPath, "src", "com", "epicgames", "ue4", "GameActivity.java");

			// check for GameActivity.java.template override
			SourceFilename = UPL.ProcessPluginNode(NDKArch, "gameActivityReplacement", SourceFilename);

			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);

			string LoadLibraryDefaults = "";

			string SuperClassDefault;
			if (!Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "GameActivitySuperClass", out SuperClassDefault))
			{
				SuperClassDefault = UPL.ProcessPluginNode(NDKArch, "gameActivitySuperClass", "");
				if (String.IsNullOrEmpty(SuperClassDefault))
				{
					SuperClassDefault = "NativeActivity";
				}
			}

			string AndroidGraphicsDebugger;
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "AndroidGraphicsDebugger", out AndroidGraphicsDebugger);

			switch (AndroidGraphicsDebugger.ToLower())
			{
				case "mali":
					LoadLibraryDefaults += "\t\ttry\n" +
											"\t\t{\n" +
											"\t\t\tSystem.loadLibrary(\"MGD\");\n" +
											"\t\t}\n" +
											"\t\tcatch (java.lang.UnsatisfiedLinkError e)\n" +
											"\t\t{\n" +
											"\t\t\tLog.debug(\"libMGD.so not loaded.\");\n" +
											"\t\t}\n";
					break;
			}

			Dictionary<string, string> Replacements = new Dictionary<string, string>{
				{ "//$${gameActivityImportAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityImportAdditions", "")},
				{ "//$${gameActivityPostImportAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityPostImportAdditions", "")},
				{ "//$${gameActivityImplementsAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityImplementsAdditions", "")},
				{ "//$${gameActivityClassAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityClassAdditions", "")},
				{ "//$${gameActivityReadMetadataAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityReadMetadataAdditions", "")},
				{ "//$${gameActivityOnCreateAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityOnCreateAdditions", "")},
				{ "//$${gameActivityOnCreateFinalAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityOnCreateFinalAdditions", "")},
				{ "//$${gameActivityOnDestroyAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityOnDestroyAdditions", "")},
				{ "//$${gameActivityOnStartAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityOnStartAdditions", "")},
				{ "//$${gameActivityOnStopAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityOnStopAdditions", "")},
				{ "//$${gameActivityOnPauseAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityOnPauseAdditions", "")},
				{ "//$${gameActivityOnResumeAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityOnResumeAdditions", "")},
				{ "//$${gameActivityOnNewIntentAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityOnNewIntentAdditions", "")},
  				{ "//$${gameActivityOnActivityResultAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityOnActivityResultAdditions", "")},
  				{ "//$${gameActivityPreConfigRulesParseAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityPreConfigRulesParseAdditions", "")},
  				{ "//$${gameActivityPostConfigRulesAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityPostConfigRulesAdditions", "")},
  				{ "//$${gameActivityFinalizeConfigRulesAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityFinalizeConfigRulesAdditions", "")},
				{ "//$${gameActivityBeforeConfigRulesAppliedAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityBeforeConfigRulesAppliedAdditions", "")},
				{ "//$${gameActivityAfterMainViewCreatedAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityAfterMainViewCreatedAdditions", "")},
				{ "//$${gameActivityResizeKeyboardAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityResizeKeyboardAdditions", "")},
				{ "//$${gameActivityLoggerCallbackAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityLoggerCallbackAdditions", "")},
				{ "//$${soLoadLibrary}$$", UPL.ProcessPluginNode(NDKArch, "soLoadLibrary", LoadLibraryDefaults)},
				{ "$${gameActivitySuperClass}$$", SuperClassDefault},
			};

			string[] TemplateSrc = File.ReadAllLines(SourceFilename);
			string[] TemplateDest = File.Exists(DestFilename) ? File.ReadAllLines(DestFilename) : null;

			bool TemplateChanged = false;
			for (int LineIndex = 0; LineIndex < TemplateSrc.Length; ++LineIndex)
			{
				string SrcLine = TemplateSrc[LineIndex];
				bool Changed = false;
				foreach (KeyValuePair<string, string> KVP in Replacements)
				{
					if(SrcLine.Contains(KVP.Key))
					{
						SrcLine = SrcLine.Replace(KVP.Key, KVP.Value);
						Changed = true;
					}
				}
				if (Changed)
				{
					TemplateSrc[LineIndex] = SrcLine;
					TemplateChanged = true;
				}
			}

			if (TemplateChanged)
			{
				// deal with insertions of newlines
				TemplateSrc = string.Join("\n", TemplateSrc).Split(new[] { "\r\n", "\r", "\n" }, StringSplitOptions.None);
			}

			if (TemplateDest == null || TemplateSrc.Length != TemplateDest.Length || !TemplateSrc.SequenceEqual(TemplateDest))
			{
				Log.TraceInformation("\n==== Writing new GameActivity.java file to {0} ====", DestFilename);
				File.WriteAllLines(DestFilename, TemplateSrc);
			}
		}

		private void UpdateGameApplication(string UE4Arch, string NDKArch, string EngineDir, string UE4BuildPath, bool bGradleEnabled)
		{
			string SourceFilename = Path.Combine(EngineDir, "Build", "Android", "Java", "src", "com", "epicgames", "ue4", "GameApplication.java.template");
			string DestFilename = Path.Combine(UE4BuildPath, "src", "com", "epicgames", "ue4", "GameApplication.java");

			if (!bGradleEnabled)
			{
				// do not use GameApplication for Ant
				SafeDeleteFile(DestFilename);
				return;
			}
			
			Dictionary<string, string> Replacements = new Dictionary<string, string>{
				{ "//$${gameApplicationImportAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameApplicationImportAdditions", "")},
				{ "//$${gameApplicationOnCreateAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameApplicationOnCreateAdditions", "")},
			};

			string[] TemplateSrc = File.ReadAllLines(SourceFilename);
			string[] TemplateDest = File.Exists(DestFilename) ? File.ReadAllLines(DestFilename) : null;

			bool TemplateChanged = false;
			for (int LineIndex = 0; LineIndex < TemplateSrc.Length; ++LineIndex)
			{
				string SrcLine = TemplateSrc[LineIndex];
				bool Changed = false;
				foreach (KeyValuePair<string, string> KVP in Replacements)
				{
					if (SrcLine.Contains(KVP.Key))
					{
						SrcLine = SrcLine.Replace(KVP.Key, KVP.Value);
						Changed = true;
					}
				}
				if (Changed)
				{
					TemplateSrc[LineIndex] = SrcLine;
					TemplateChanged = true;
				}
			}

			if (TemplateChanged)
			{
				// deal with insertions of newlines
				TemplateSrc = string.Join("\n", TemplateSrc).Split(new[] { "\r\n", "\r", "\n" }, StringSplitOptions.None);
			}

			if (TemplateDest == null || TemplateSrc.Length != TemplateDest.Length || !TemplateSrc.SequenceEqual(TemplateDest))
			{
				Log.TraceInformation("\n==== Writing new GameApplication.java file to {0} ====", DestFilename);
				File.WriteAllLines(DestFilename, TemplateSrc);
			}
		}

		private void CreateAdditonalBuildPathFiles(string NDKArch, string UE4BuildPath, XDocument FilesToAdd)
		{
			Dictionary<string, string> PathsAndRootEls = new Dictionary<string, string>();

			foreach (XElement Element in FilesToAdd.Root.Elements())
			{
				string RelPath = Element.Value;
				if (RelPath != null)
				{
					XAttribute TypeAttr = Element.Attribute("rootEl");
					PathsAndRootEls[RelPath] = TypeAttr == null ? null : TypeAttr.Value;
				}
			}

			foreach (KeyValuePair<string, string> Entry in PathsAndRootEls)
			{
				string UPLNodeName = Entry.Key.Replace("/", "__").Replace(".", "__");
				string Content;
				if (Entry.Value == null)
				{
					// no root element, assume not XML
					Content = UPL.ProcessPluginNode(NDKArch, UPLNodeName, "");
				}
				else
				{
					XDocument ContentDoc = new XDocument(new XElement(Entry.Value));
					UPL.ProcessPluginNode(NDKArch, UPLNodeName, "", ref ContentDoc);
					Content = XML_HEADER + "\n" + ContentDoc.ToString();
				}

				string DestPath = Path.Combine(UE4BuildPath, Entry.Key);
				if (!File.Exists(DestPath) || File.ReadAllText(DestPath) != Content)
				{
					File.WriteAllText(DestPath, Content);
				}
			}
		}

		private AndroidAARHandler CreateAARHandler(string EngineDir, string UE4BuildPath, List<string> NDKArches, bool bGradleEnabled, bool HandleDependencies=true)
		{
			AndroidAARHandler AARHandler = new AndroidAARHandler();
			string ImportList = "";

			// Get some common paths
			string AndroidHome = Environment.ExpandEnvironmentVariables("%ANDROID_HOME%").TrimEnd('/', '\\');
			EngineDir = EngineDir.TrimEnd('/', '\\');

			// Add the AARs from the default aar-imports.txt
			// format: Package,Name,Version
			string ImportsAntFile = Path.Combine(UE4BuildPath, "aar-imports-ant.txt");
			if (!bGradleEnabled && File.Exists(ImportsAntFile))
			{
				ImportList = File.ReadAllText(ImportsAntFile);
			}
			else
			{
				string ImportsFile = Path.Combine(UE4BuildPath, "aar-imports.txt");
				if (File.Exists(ImportsFile))
				{
					ImportList = File.ReadAllText(ImportsFile);
				}
			}

			// Run the UPL imports section for each architecture and add any new imports (duplicates will be removed)
			foreach (string NDKArch in NDKArches)
			{
				ImportList = UPL.ProcessPluginNode(NDKArch, "AARImports", ImportList);
			}

			// Add the final list of imports and get dependencies
			foreach (string Line in ImportList.Split('\n'))
			{
				string Trimmed = Line.Trim(' ', '\r');

				if (Trimmed.StartsWith("repository "))
				{
					string DirectoryPath = Trimmed.Substring(11).Trim(' ').TrimEnd('/', '\\');
					DirectoryPath = DirectoryPath.Replace("$(ENGINEDIR)", EngineDir);
					DirectoryPath = DirectoryPath.Replace("$(ANDROID_HOME)", AndroidHome);
					DirectoryPath = DirectoryPath.Replace('\\', Path.DirectorySeparatorChar).Replace('/', Path.DirectorySeparatorChar);
					AARHandler.AddRepository(DirectoryPath);
				}
				else if (Trimmed.StartsWith("repositories "))
				{
					string DirectoryPath = Trimmed.Substring(13).Trim(' ').TrimEnd('/', '\\');
					DirectoryPath = DirectoryPath.Replace("$(ENGINEDIR)", EngineDir);
					DirectoryPath = DirectoryPath.Replace("$(ANDROID_HOME)", AndroidHome);
					DirectoryPath = DirectoryPath.Replace('\\', Path.DirectorySeparatorChar).Replace('/', Path.DirectorySeparatorChar);
					AARHandler.AddRepositories(DirectoryPath, "m2repository");
				}
				else
				{
					string[] Sections = Trimmed.Split(',');
					if (Sections.Length == 3)
					{
						string PackageName = Sections[0].Trim(' ');
						string BaseName = Sections[1].Trim(' ');
						string Version = Sections[2].Trim(' ');
						Log.TraceInformation("AARImports: {0}, {1}, {2}", PackageName, BaseName, Version);
						AARHandler.AddNewAAR(PackageName, BaseName, Version, HandleDependencies);
					}
				}
			}

			return AARHandler;
		}

		private void PrepareJavaLibsForGradle(string JavaLibsDir, string UE4BuildGradlePath, string InMinSdkVersion, string InTargetSdkVersion, string CompileSDKVersion, string BuildToolsVersion)
		{
			StringBuilder SettingsGradleContent = new StringBuilder();
			StringBuilder ProjectDependencyContent = new StringBuilder();

			SettingsGradleContent.AppendLine("rootProject.name='app'");
			SettingsGradleContent.AppendLine("include ':app'");
			ProjectDependencyContent.AppendLine("dependencies {");

			string[] LibDirs = Directory.GetDirectories(JavaLibsDir);
			foreach (string LibDir in LibDirs)
			{
				string RelativePath = Path.GetFileName(LibDir);

				SettingsGradleContent.AppendLine(string.Format("include ':{0}'", RelativePath));
				ProjectDependencyContent.AppendLine(string.Format("\timplementation project(':{0}')", RelativePath));

				string GradleProjectPath = Path.Combine(UE4BuildGradlePath, RelativePath);
				string GradleProjectMainPath = Path.Combine(GradleProjectPath, "src", "main");

				string ManifestFilename = Path.Combine(LibDir, "AndroidManifest.xml");
				string GradleManifest = Path.Combine(GradleProjectMainPath, "AndroidManifest.xml");
				MakeDirectoryIfRequired(GradleManifest);

				// Copy parts were they need to be
				CleanCopyDirectory(Path.Combine(LibDir, "assets"), Path.Combine(GradleProjectPath, "assets"));
				CleanCopyDirectory(Path.Combine(LibDir, "libs"), Path.Combine(GradleProjectPath, "libs"));
				CleanCopyDirectory(Path.Combine(LibDir, "res"), Path.Combine(GradleProjectMainPath, "res"));

				// If our lib already has a src/main/java folder, don't put things into a java folder
				string SrcDirectory = Path.Combine(LibDir, "src", "main");
				if (Directory.Exists(Path.Combine(SrcDirectory, "java")))
				{
					CleanCopyDirectory(SrcDirectory, GradleProjectMainPath);
				}
				else
				{
					CleanCopyDirectory(Path.Combine(LibDir, "src"), Path.Combine(GradleProjectMainPath, "java"));
				}

				// Now generate a build.gradle from the manifest
				StringBuilder BuildGradleContent = new StringBuilder();
				BuildGradleContent.AppendLine("apply plugin: 'com.android.library'");
				BuildGradleContent.AppendLine("android {");
				BuildGradleContent.AppendLine(string.Format("\tcompileSdkVersion {0}", CompileSDKVersion));
				BuildGradleContent.AppendLine("\tdefaultConfig {");

				// Try to get the SDK target from the AndroidManifest.xml
				string VersionCode = "";
				string VersionName = "";
				string MinSdkVersion = InMinSdkVersion;
				string TargetSdkVersion = InTargetSdkVersion;
				XDocument ManifestXML;
				if (File.Exists(ManifestFilename))
				{
					try
					{
						ManifestXML = XDocument.Load(ManifestFilename);

						XAttribute VersionCodeAttr = ManifestXML.Root.Attribute(XName.Get("versionCode", "http://schemas.android.com/apk/res/android"));
						if (VersionCodeAttr != null)
						{
							VersionCode = VersionCodeAttr.Value;
						}

						XAttribute VersionNameAttr = ManifestXML.Root.Attribute(XName.Get("versionName", "http://schemas.android.com/apk/res/android"));
						if (VersionNameAttr != null)
						{
							VersionName = VersionNameAttr.Value;
						}

						XElement UseSDKNode = null;
						foreach (XElement WorkNode in ManifestXML.Elements().First().Descendants("uses-sdk"))
						{
							UseSDKNode = WorkNode;

							XAttribute MinSdkVersionAttr = WorkNode.Attribute(XName.Get("minSdkVersion", "http://schemas.android.com/apk/res/android"));
							if (MinSdkVersionAttr != null)
							{
								MinSdkVersion = MinSdkVersionAttr.Value;
							}

							XAttribute TargetSdkVersionAttr = WorkNode.Attribute(XName.Get("targetSdkVersion", "http://schemas.android.com/apk/res/android"));
							if (TargetSdkVersionAttr != null)
							{
								TargetSdkVersion = TargetSdkVersionAttr.Value;
							}
						}

						if (UseSDKNode != null)
						{
							UseSDKNode.Remove();
						}

						// rewrite the manifest if different
						String NewManifestText = ManifestXML.ToString();
						String OldManifestText = "";
						if (File.Exists(GradleManifest))
						{
							OldManifestText = File.ReadAllText(GradleManifest);
						}
						if (NewManifestText != OldManifestText)
						{
							File.WriteAllText(GradleManifest, NewManifestText);
						}
					}
					catch (Exception e)
					{
						Log.TraceError("AAR Manifest file {0} parsing error! {1}", ManifestFilename, e);
					}
				}

				if (VersionCode != "")
				{
					BuildGradleContent.AppendLine(string.Format("\t\tversionCode {0}", VersionCode));
				}
				if (VersionName != "")
				{
					BuildGradleContent.AppendLine(string.Format("\t\tversionName \"{0}\"", VersionName));
				}
				if (MinSdkVersion != "")
				{
					BuildGradleContent.AppendLine(string.Format("\t\tminSdkVersion = {0}", MinSdkVersion));
				}
				if (TargetSdkVersion != "")
				{
					BuildGradleContent.AppendLine(string.Format("\t\ttargetSdkVersion = {0}", TargetSdkVersion));
				}
				BuildGradleContent.AppendLine("\t}");
				BuildGradleContent.AppendLine("}");

				string AdditionsGradleFilename = Path.Combine(LibDir, "additions.gradle");
				if (File.Exists(AdditionsGradleFilename))
				{
					string[] AdditionsLines = File.ReadAllLines(AdditionsGradleFilename);
					foreach (string LineContents in AdditionsLines)
					{
						BuildGradleContent.AppendLine(LineContents);
					}
				}

				// rewrite the build.gradle if different
				string BuildGradleFilename = Path.Combine(GradleProjectPath, "build.gradle");
				String NewBuildGradleText = BuildGradleContent.ToString();
				String OldBuildGradleText = "";
				if (File.Exists(BuildGradleFilename))
				{
					OldBuildGradleText = File.ReadAllText(BuildGradleFilename);
				}
				if (NewBuildGradleText != OldBuildGradleText)
				{
					File.WriteAllText(BuildGradleFilename, NewBuildGradleText);
				}
			}
			ProjectDependencyContent.AppendLine("}");

			string SettingsGradleFilename = Path.Combine(UE4BuildGradlePath, "settings.gradle");
			File.WriteAllText(SettingsGradleFilename, SettingsGradleContent.ToString());

			string ProjectsGradleFilename = Path.Combine(UE4BuildGradlePath, "app", "projects.gradle");
			File.WriteAllText(ProjectsGradleFilename, ProjectDependencyContent.ToString());
		}

		private void GenerateGradleAARImports(string EngineDir, string UE4BuildPath, List<string> NDKArches)
		{
			AndroidAARHandler AARHandler = CreateAARHandler(EngineDir, UE4BuildPath, NDKArches, true, false);
			StringBuilder AARImportsContent = new StringBuilder();

			// Add repositories
			AARImportsContent.AppendLine("repositories {");
			foreach (string Repository in AARHandler.Repositories)
			{
				string RepositoryPath = Path.GetFullPath(Repository).Replace('\\', '/');
				AARImportsContent.AppendLine("\tmaven { url uri('" + RepositoryPath + "') }");
			}
			AARImportsContent.AppendLine("}");

			// Add dependencies
			AARImportsContent.AppendLine("dependencies {");
			foreach (AndroidAARHandler.AndroidAAREntry Dependency in AARHandler.AARList)
			{
				AARImportsContent.AppendLine(string.Format("\timplementation '{0}:{1}:{2}'", Dependency.Filename, Dependency.BaseName, Dependency.Version));
			}
			AARImportsContent.AppendLine("}");

			string AARImportsFilename = Path.Combine(UE4BuildPath, "gradle", "app", "aar-imports.gradle");
			File.WriteAllText(AARImportsFilename, AARImportsContent.ToString());
		}

		private void ExtractAARAndJARFiles(string EngineDir, string UE4BuildPath, List<string> NDKArches, string AppPackageName, string AARExtractListFilename)
		{
			AndroidAARHandler AARHandler = CreateAARHandler(EngineDir, UE4BuildPath, NDKArches, false, true);

			// Finally, extract the AARs and copy the JARs
			AARHandler.ExtractAARs(UE4BuildPath, AppPackageName);
			AARHandler.CopyJARs(UE4BuildPath);

			// Write list of AAR files extracted
			StringBuilder AARListContents = new StringBuilder();
			foreach (AndroidAARHandler.AndroidAAREntry Dependency in AARHandler.AARList)
			{
				AARListContents.AppendLine(Dependency.Filename);
			}
			File.WriteAllText(AARExtractListFilename, AARListContents.ToString());
		}
	}
}
