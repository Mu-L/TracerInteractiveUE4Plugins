// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using AutomationTool;
using System;
using System.Collections.Generic;
using System.IO;
using System.Net.NetworkInformation;
using System.Reflection;
using System.Text.RegularExpressions;
using System.Threading;
using System.Linq;
using UnrealBuildTool;
using System.Text;
using Tools.DotNETCommon;

/// <summary>
/// Helper command used for cooking.
/// </summary>
/// <remarks>
/// Command line parameters used by this command:
/// -clean
/// </remarks>
public partial class Project : CommandUtils
{

	#region Utilities

	private static readonly object SyncLock = new object();

	/// <returns>The path for the BuildPatchTool executable depending on host platform.</returns>
	private static string GetBuildPatchToolExecutable()
	{
		switch (UnrealBuildTool.BuildHostPlatform.Current.Platform)
		{
			case UnrealTargetPlatform.Win32:
				return CommandUtils.CombinePaths(CommandUtils.CmdEnv.LocalRoot, "Engine/Binaries/Win32/BuildPatchTool.exe");
			case UnrealTargetPlatform.Win64:
				return CommandUtils.CombinePaths(CommandUtils.CmdEnv.LocalRoot, "Engine/Binaries/Win64/BuildPatchTool.exe");
			case UnrealTargetPlatform.Mac:
				return CommandUtils.CombinePaths(CommandUtils.CmdEnv.LocalRoot, "Engine/Binaries/Mac/BuildPatchTool");
			case UnrealTargetPlatform.Linux:
				return CommandUtils.CombinePaths(CommandUtils.CmdEnv.LocalRoot, "Engine/Binaries/Linux/BuildPatchTool");
		}
		throw new AutomationException(string.Format("Unknown host platform for BuildPatchTool - {0}", UnrealBuildTool.BuildHostPlatform.Current.Platform));
	}

	/// <summary>
	/// Checks the existence of the BuildPatchTool executable exists and builds it if it is missing
	/// </summary>
	private static void EnsureBuildPatchToolExists()
	{
		string BuildPatchToolExe = GetBuildPatchToolExecutable();
		if (!CommandUtils.FileExists_NoExceptions(BuildPatchToolExe))
		{
			lock (SyncLock)
			{
				if (!CommandUtils.FileExists_NoExceptions(BuildPatchToolExe))
				{
					UE4BuildUtils.BuildBuildPatchTool(null, UnrealBuildTool.BuildHostPlatform.Current.Platform);
				}
			}
		}
	}

	/// <summary>
	/// Writes a pak response file to disk
	/// </summary>
	/// <param name="Filename"></param>
	/// <param name="ResponseFile"></param>
	private static void WritePakResponseFile(string Filename, Dictionary<string, string> ResponseFile, bool Compressed, EncryptionAndSigning.CryptoSettings CryptoSettings)
	{
		using (var Writer = new StreamWriter(Filename, false, new System.Text.UTF8Encoding(true)))
		{
			foreach (var Entry in ResponseFile)
			{
				string Line = String.Format("\"{0}\" \"{1}\"", Entry.Key, Entry.Value);
				if (Compressed)
				{
					Line += " -compress";
				}

				if(CryptoSettings != null)
				{
					bool bEncryptFile = CryptoSettings.bEnablePakFullAssetEncryption;
					bEncryptFile = bEncryptFile || (CryptoSettings.bEnablePakUAssetEncryption && Path.GetExtension(Entry.Key).Contains(".uasset"));
					bEncryptFile = bEncryptFile || (CryptoSettings.bEnablePakIniEncryption && Path.GetExtension(Entry.Key).Contains(".ini"));

					if (bEncryptFile)
					{
						Line += " -encrypt";
					}
				}

				Writer.WriteLine(Line);
			}
		}
	}

	/// <summary>
	/// Loads streaming install chunk manifest file from disk
	/// </summary>
	/// <param name="Filename"></param>
	/// <returns></returns>
	private static HashSet<string> ReadPakChunkManifest(string Filename)
	{
		var ResponseFile = ReadAllLines(Filename);
		var Result = new HashSet<string>(ResponseFile, StringComparer.InvariantCultureIgnoreCase);
		return Result;
	}

	static public void RunUnrealPak(Dictionary<string, string> UnrealPakResponseFile, FileReference OutputLocation, FileReference PakOrderFileLocation, string PlatformOptions, bool Compressed, EncryptionAndSigning.CryptoSettings CryptoSettings, FileReference CryptoKeysCacheFilename, String PatchSourceContentPath)
	{
		if (UnrealPakResponseFile.Count < 1)
		{
			return;
		}
		string PakName = Path.GetFileNameWithoutExtension(OutputLocation.FullName);
		string UnrealPakResponseFileName = CombinePaths(CmdEnv.LogFolder, "PakList_" + PakName + ".txt");
		WritePakResponseFile(UnrealPakResponseFileName, UnrealPakResponseFile, Compressed, CryptoSettings);

		var UnrealPakExe = CombinePaths(CmdEnv.LocalRoot, "Engine/Binaries/Win64/UnrealPak.exe");
		Log("Running UnrealPak *******");
		string CmdLine = CommandUtils.MakePathSafeToUseWithCommandLine(OutputLocation.FullName) + " -create=" + CommandUtils.MakePathSafeToUseWithCommandLine(UnrealPakResponseFileName);
		string LogFileName = CombinePaths(CmdEnv.LogFolder, "PakLog_" + PakName + ".log");

		CmdLine += String.Format(" -abslog=\"{0}\"", LogFileName);

		if(CryptoKeysCacheFilename != null)
		{
			CmdLine += String.Format(" -cryptokeys=\"{0}\"", CryptoKeysCacheFilename.ToString());
		}
		if (GlobalCommandLine.Installed)
		{
			CmdLine += " -installed";
		}
		if(PakOrderFileLocation != null)
		{
			CmdLine += " -order=" + CommandUtils.MakePathSafeToUseWithCommandLine(PakOrderFileLocation.FullName);
		}
		if (GlobalCommandLine.UTF8Output)
		{
			CmdLine += " -UTF8Output";
		}
		if (!String.IsNullOrEmpty(PatchSourceContentPath))
		{
			CmdLine += " -generatepatch=" + CommandUtils.MakePathSafeToUseWithCommandLine(PatchSourceContentPath) + " -tempfiles=" + CommandUtils.MakePathSafeToUseWithCommandLine(CommandUtils.CombinePaths(CommandUtils.CmdEnv.LocalRoot, "TempFiles" + PakName));
		}
		if (CryptoSettings != null && CryptoSettings.bEnablePakIndexEncryption)
		{
			CmdLine += " -encryptindex";
		}
		CmdLine += " -multiprocess"; // Prevents warnings about being unable to write to config files
		CmdLine += PlatformOptions;
		string UnrealPakLogFileName = "UnrealPak_" + PakName;
		RunAndLog(CmdEnv, UnrealPakExe, CmdLine, LogName: UnrealPakLogFileName, Options: ERunOptions.Default | ERunOptions.UTF8Output);
		Log("UnrealPak Done *******");
	}

	static public void LogDeploymentContext(DeploymentContext SC)
	{
		LogLog("Deployment Context **************");
		LogLog("ProjectFile = {0}", SC.RawProjectPath);
		LogLog("ArchiveDir = {0}", SC.ArchiveDirectory);
		LogLog("IsCodeBasedUprojectFile = {0}", SC.IsCodeBasedProject);
		LogLog("DedicatedServer = {0}", SC.DedicatedServer);
		LogLog("Stage = {0}", SC.Stage);
		LogLog("StageTargetPlatform = {0}", SC.StageTargetPlatform.PlatformType.ToString());
		LogLog("InputRootDir = {0}", SC.LocalRoot);
		LogLog("InputProjectDir = {0}", SC.ProjectRoot);
		LogLog("PlatformDir = {0}", SC.PlatformDir);
		LogLog("StagedOutputDir = {0}", SC.StageDirectory);
		LogLog("ShortProjectName = {0}", SC.ShortProjectName);
		LogLog("ProjectArgForCommandLines = {0}", SC.ProjectArgForCommandLines);
		LogLog("RunRootDir = {0}", SC.RuntimeRootDir);
		LogLog("RunProjectDir = {0}", SC.RuntimeProjectRootDir);
		LogLog("PakFileInternalRoot = {0}", SC.PakFileInternalRoot);
		LogLog("PlatformUsesChunkManifests = {0}", SC.PlatformUsesChunkManifests);
		LogLog("End Deployment Context **************");
	}

	private static void StageLocalizationDataForTarget(DeploymentContext SC, List<string> CulturesToStage, DirectoryReference SourceDirectory)
	{
		foreach (FileReference SourceFile in DirectoryReference.EnumerateFiles(SourceDirectory, "*.locmeta"))
		{
			SC.StageFile(StagedFileType.UFS, SourceFile);
		}
		foreach (string Culture in CulturesToStage)
		{
			StageLocalizationDataForCulture(SC, Culture, SourceDirectory);
		}
	}

	private static void StageLocalizationDataForCulture(DeploymentContext SC, string CultureName, DirectoryReference SourceDirectory)
	{
		CultureName = CultureName.Replace('-', '_');

		string[] LocaleTags = CultureName.Replace('-', '_').Split('_');

		List<string> PotentialParentCultures = new List<string>();

		if (LocaleTags.Length > 0)
		{
			if (LocaleTags.Length > 1 && LocaleTags.Length > 2)
			{
				PotentialParentCultures.Add(string.Join("_", LocaleTags[0], LocaleTags[1], LocaleTags[2]));
			}
			if (LocaleTags.Length > 2)
			{
				PotentialParentCultures.Add(string.Join("_", LocaleTags[0], LocaleTags[2]));
			}
			if (LocaleTags.Length > 1)
			{
				PotentialParentCultures.Add(string.Join("_", LocaleTags[0], LocaleTags[1]));
			}
			PotentialParentCultures.Add(LocaleTags[0]);
		}

		foreach (DirectoryReference FoundDirectory in DirectoryReference.EnumerateDirectories(SourceDirectory, "*", SearchOption.TopDirectoryOnly))
		{
			string DirectoryName = CommandUtils.GetLastDirectoryName(FoundDirectory.FullName);
			string CanonicalizedPotentialCulture = DirectoryName.Replace('-', '_');

			if (PotentialParentCultures.Contains(CanonicalizedPotentialCulture))
			{
				foreach (FileReference SourceFile in DirectoryReference.EnumerateFiles(FoundDirectory, "*.locres", SearchOption.AllDirectories))
				{
					SC.StageFile(StagedFileType.UFS, SourceFile);
				}
			}
		}
	}

	private static void StageAdditionalDirectoriesFromConfig(DeploymentContext SC, DirectoryReference ProjectContentRoot, StagedDirectoryReference StageContentRoot, ConfigHierarchy PlatformGameConfig, bool bUFS, string ConfigKeyName)
	{
		List<string> ExtraDirs;
		if (PlatformGameConfig.GetArray("/Script/UnrealEd.ProjectPackagingSettings", ConfigKeyName, out ExtraDirs))
		{
			// Each string has the format '(Path="TheDirToStage")'
			foreach (var PathStr in ExtraDirs)
			{
				string RelativePath = null;
				var PathParts = PathStr.Split('"');
				if (PathParts.Length == 3)
				{
					RelativePath = PathParts[1];
				}
				else if (PathParts.Length == 1)
				{
					RelativePath = PathParts[0];
				}
				if (RelativePath != null)
				{
					DirectoryReference InputDir = DirectoryReference.Combine(ProjectContentRoot, RelativePath);
					StagedDirectoryReference OutputDir = StagedDirectoryReference.Combine(StageContentRoot, RelativePath);
					if (bUFS)
					{
						List<FileReference> Files = SC.FindFilesToStage(InputDir, StageFilesSearch.AllDirectories);
						Files.RemoveAll(x => x.HasExtension(".uasset") || x.HasExtension(".umap"));

						SC.StageFiles(StagedFileType.UFS, InputDir, Files, OutputDir);
					}
					else
					{
						SC.StageFiles(StagedFileType.NonUFS, InputDir, StageFilesSearch.AllDirectories, OutputDir);
					}
				}
			}
		}
	}

	public static void CreateStagingManifest(ProjectParams Params, DeploymentContext SC)
	{
		if (!Params.Stage)
		{
			return;
		}
		var ThisPlatform = SC.StageTargetPlatform;

		Log("Creating Staging Manifest...");

		if (Params.HasIterateSharedCookedBuild)
		{
			// can't do shared cooked builds with DLC that's madness!!
			//check( Params.HasDLCName == false );

			// stage all the previously staged files
			SC.StageFiles(StagedFileType.NonUFS, DirectoryReference.Combine(SC.ProjectRoot, "Saved", "SharedIterativeBuild", SC.CookPlatform, "Staged"), StageFilesSearch.AllDirectories, StagedDirectoryReference.Root); // remap to the root directory
		}
		if (Params.HasDLCName)
		{
			// Making a plugin
			DirectoryReference DLCRoot = Params.DLCFile.Directory;

			// The .uplugin file is staged differently for different DLC
			// The .uplugin file doesn't actually exist for mobile DLC
			if (FileReference.Exists(Params.DLCFile))
			{
				if (Params.DLCPakPluginFile)
				{
					SC.StageFile(StagedFileType.UFS, Params.DLCFile);
				}
				else
				{
					SC.StageFile(StagedFileType.NonUFS, Params.DLCFile);
				}
			}

			// Put the binaries into the staged dir
			DirectoryReference BinariesDir = DirectoryReference.Combine(DLCRoot, "Binaries");
			if (DirectoryReference.Exists(BinariesDir))
			{
				SC.StageFiles(StagedFileType.NonUFS, BinariesDir, "libUE4-*.so", StageFilesSearch.AllDirectories);
				SC.StageFiles(StagedFileType.NonUFS, BinariesDir, "UE4-*.dll", StageFilesSearch.AllDirectories);
				SC.StageFiles(StagedFileType.NonUFS, BinariesDir, "libUE4Server-*.so", StageFilesSearch.AllDirectories);
				SC.StageFiles(StagedFileType.NonUFS, BinariesDir, "UE4Server-*.dll", StageFilesSearch.AllDirectories);
			}

			// Put all of the cooked dir into the staged dir
			DirectoryReference PlatformCookDir = String.IsNullOrEmpty(Params.CookOutputDir) ? DirectoryReference.Combine(DLCRoot, "Saved", "Cooked", SC.CookPlatform) : DirectoryReference.Combine(new DirectoryReference(Params.CookOutputDir), SC.CookPlatform);
			DirectoryReference PlatformEngineDir = DirectoryReference.Combine(PlatformCookDir, "Engine");
			SC.MetadataDir = DirectoryReference.Combine(PlatformCookDir, SC.ShortProjectName, "Metadata");

			// Put the config files into the staged dir
			DirectoryReference ConfigDir = DirectoryReference.Combine(DLCRoot, "Config");
			if (DirectoryReference.Exists(ConfigDir))
			{
				SC.StageFiles(StagedFileType.UFS, ConfigDir, "*.ini", StageFilesSearch.AllDirectories);
			}


			if (Params.DLCActLikePatch)
			{
				DirectoryReference DLCContent = DirectoryReference.Combine(DLCRoot, "Content");
				string RelativeDLCContentPath = DLCContent.MakeRelativeTo(SC.LocalRoot);
				string RelativeRootContentPath = DirectoryReference.Combine(SC.ProjectRoot, "Content").MakeRelativeTo(SC.LocalRoot);

				SC.RemapDirectories.Add(Tuple.Create(new StagedDirectoryReference(RelativeDLCContentPath), new StagedDirectoryReference(RelativeRootContentPath)));
			}

			// Stage all the cooked data, this is the same rule as normal stage except we may skip Engine
			List<FileReference> CookedFiles = DirectoryReference.EnumerateFiles(PlatformCookDir, "*", SearchOption.AllDirectories).ToList();
			foreach (FileReference CookedFile in CookedFiles)
			{
				// Skip metadata directory
				if (CookedFile.Directory.IsUnderDirectory(SC.MetadataDir))
				{
					continue;
				}

				// Dedicated server cook doesn't save shaders so no Engine dir is created
				if ((!SC.DedicatedServer) && (!Params.DLCIncludeEngineContent) && CookedFile.Directory.IsUnderDirectory(PlatformEngineDir))
				{
					continue;
				}

				// json files have never been staged
				// metallib files cannot *currently* be staged as UFS as the Metal API needs to mmap them from files on disk in order to function efficiently
				if (!CookedFile.HasExtension(".json") && !CookedFile.HasExtension(".metallib"))
				{
					SC.StageFile(StagedFileType.UFS, CookedFile, new StagedFileReference(CookedFile.MakeRelativeTo(PlatformCookDir)));
				}
			}

			FileReference PluginSettingsFile = FileReference.Combine(DLCRoot, "Config", "PluginSettings.ini");
			if (FileReference.Exists(PluginSettingsFile))
			{
				ConfigFile File = new ConfigFile(PluginSettingsFile);
				ConfigFileSection StageSettings;
				if (File.TryGetSection("StageSettings", out StageSettings))
				{
					foreach (ConfigLine Line in StageSettings.Lines)
					{
						if (Line.Key == "AdditionalNonUSFDirectories")
						{
							SC.StageFiles(StagedFileType.NonUFS, DirectoryReference.Combine(DLCRoot, Line.Value), "*.*", StageFilesSearch.AllDirectories);
						}
					}
				}

				if (SC.DedicatedServer)
				{
					ConfigFileSection StageSettingsServer;
					if (File.TryGetSection("StageSettingsServer", out StageSettingsServer))
					{
						foreach (ConfigLine Line in StageSettingsServer.Lines)
						{
							if (Line.Key == "AdditionalNonUSFDirectories")
							{
								SC.StageFiles(StagedFileType.NonUFS, DirectoryReference.Combine(DLCRoot, Line.Value), "*.*", StageFilesSearch.AllDirectories);
							}
						}
					}
				}
			}

			if (Params.UsePak(SC.StageTargetPlatform))
			{
				CreatePluginManifest(SC, SC.FilesToStage.UFSFiles, StagedFileType.UFS, Params.DLCFile.GetFileNameWithoutAnyExtensions());
			}
		}
		else
		{

			if (!Params.IterateSharedBuildUsePrecompiledExe)
			{
				ThisPlatform.GetFilesToDeployOrStage(Params, SC);

				// Stage any extra runtime dependencies from the receipts
				foreach (StageTarget Target in SC.StageTargets)
				{
					SC.StageRuntimeDependenciesFromReceipt(Target.Receipt, Target.RequireFilesExist, Params.UsePak(SC.StageTargetPlatform));
				}
			}

			// move the UE4Commandline.txt file to the root of the stage
			// this file needs to be treated as a UFS file for casing, but NonUFS for being put into the .pak file
			// @todo: Maybe there should be a new category - UFSNotForPak
			FileReference CommandLineFile = FileReference.Combine(GetIntermediateCommandlineDir(SC), "UE4CommandLine.txt");
			if (FileReference.Exists(CommandLineFile))
			{
				StagedFileReference StagedCommandLineFile = new StagedFileReference("UE4CommandLine.txt");
				if (SC.StageTargetPlatform.DeployLowerCaseFilenames())
				{
					StagedCommandLineFile = StagedCommandLineFile.ToLowerInvariant();
				}
				SC.StageFile(StagedFileType.SystemNonUFS, CommandLineFile, StagedCommandLineFile);
			}

			ConfigHierarchy PlatformGameConfig = ConfigCache.ReadHierarchy(ConfigHierarchyType.Game, DirectoryReference.FromFile(Params.RawProjectPath), SC.StageTargetPlatform.IniPlatformType);
			DirectoryReference ProjectContentRoot = DirectoryReference.Combine(SC.ProjectRoot, "Content");
			StagedDirectoryReference StageContentRoot = StagedDirectoryReference.Combine(SC.RelativeProjectRootForStage, "Content");

			if (!Params.CookOnTheFly && !Params.SkipCookOnTheFly) // only stage the UFS files if we are not using cook on the fly
			{


				// Initialize internationalization preset.
				string InternationalizationPreset = Params.InternationalizationPreset;

				// Use configuration if otherwise lacking an internationalization preset.
				if (string.IsNullOrEmpty(InternationalizationPreset))
				{
					if (PlatformGameConfig != null)
					{
						PlatformGameConfig.GetString("/Script/UnrealEd.ProjectPackagingSettings", "InternationalizationPreset", out InternationalizationPreset);
					}
				}

				// Error if no preset has been provided.
				if (string.IsNullOrEmpty(InternationalizationPreset))
				{
					throw new AutomationException("No internationalization preset was specified for packaging. This will lead to fatal errors when launching. Specify preset via commandline (-I18NPreset=) or project packaging settings (InternationalizationPreset).");
				}

				// Initialize cultures to stage.
				List<string> CulturesToStage = null;

				// Use parameters if provided.
				if (Params.CulturesToCook != null && Params.CulturesToCook.Count > 0)
				{
					CulturesToStage = Params.CulturesToCook;
				}

				// Use configuration if otherwise lacking cultures to stage.
				if (CulturesToStage == null || CulturesToStage.Count == 0)
				{
					if (PlatformGameConfig != null)
					{
						PlatformGameConfig.GetArray("/Script/UnrealEd.ProjectPackagingSettings", "CulturesToStage", out CulturesToStage);
					}
				}

				// Error if no cultures have been provided.
				if (CulturesToStage == null || CulturesToStage.Count == 0)
				{
					throw new AutomationException("No cultures were specified for cooking and packaging. This will lead to fatal errors when launching. Specify culture codes via commandline (-CookCultures=) or using project packaging settings (+CulturesToStage).");
				}

				// Stage ICU internationalization data from Engine.
				SC.StageFiles(StagedFileType.UFS, DirectoryReference.Combine(SC.LocalRoot, "Engine", "Content", "Internationalization", InternationalizationPreset), StageFilesSearch.AllDirectories, new StagedDirectoryReference("Engine/Content/Internationalization"));

				// Engine ufs (content)
				StageConfigFiles(SC, DirectoryReference.Combine(SC.LocalRoot, "Engine", "Config"));

				StageLocalizationDataForTarget(SC, CulturesToStage, DirectoryReference.Combine(SC.LocalRoot, "Engine", "Content", "Localization", "Engine"));

				// Game ufs (content)
				SC.StageFile(StagedFileType.UFS, SC.RawProjectPath);

				StageConfigFiles(SC, DirectoryReference.Combine(SC.ProjectRoot, "Config"));


				// Stage plugin config files
				List<KeyValuePair<StagedFileReference, FileReference>> StagedPlugins = SC.FilesToStage.UFSFiles.Where(x => x.Value.HasExtension(".uplugin")).ToList();
				foreach (KeyValuePair<StagedFileReference, FileReference> StagedPlugin in StagedPlugins)
				{
					//PluginDescriptor Descriptor = PluginDescriptor.FromFile(StagedPlugin.Value);
					DirectoryReference PluginConfigDirectory = DirectoryReference.Combine(StagedPlugin.Value.Directory, "Config");
					if (DirectoryReference.Exists(PluginConfigDirectory))
					{
						SC.StageFiles(StagedFileType.UFS, PluginConfigDirectory, "*.ini", StageFilesSearch.AllDirectories);
					}
				}

				// Stage all project localization targets
				{
					DirectoryReference ProjectLocRootDirectory = DirectoryReference.Combine(SC.ProjectRoot, "Content", "Localization");
					if (DirectoryReference.Exists(ProjectLocRootDirectory))
					{
						foreach (DirectoryReference ProjectLocTargetDirectory in DirectoryReference.EnumerateDirectories(ProjectLocRootDirectory))
						{
							StageLocalizationDataForTarget(SC, CulturesToStage, ProjectLocTargetDirectory);
						}
					}
				}

				// Stage all plugin localization targets
				foreach (KeyValuePair<StagedFileReference, FileReference> StagedPlugin in StagedPlugins)
				{
					PluginDescriptor Descriptor = PluginDescriptor.FromFile(StagedPlugin.Value);
					if (Descriptor.LocalizationTargets != null)
					{
						foreach (LocalizationTargetDescriptor LocalizationTarget in Descriptor.LocalizationTargets)
						{
							if (LocalizationTarget.LoadingPolicy == LocalizationTargetDescriptorLoadingPolicy.Always || LocalizationTarget.LoadingPolicy == LocalizationTargetDescriptorLoadingPolicy.Game)
							{
								DirectoryReference PluginLocTargetDirectory = DirectoryReference.Combine(StagedPlugin.Value.Directory, "Content", "Localization", LocalizationTarget.Name);
								if (DirectoryReference.Exists(PluginLocTargetDirectory))
								{
									StageLocalizationDataForTarget(SC, CulturesToStage, PluginLocTargetDirectory);
								}
							}
						}
					}
				}

				// Stage any additional UFS and NonUFS paths specified in the project ini files; these dirs are relative to the game content directory
				if (PlatformGameConfig != null)
				{
					StageAdditionalDirectoriesFromConfig(SC, ProjectContentRoot, StageContentRoot, PlatformGameConfig, true, "DirectoriesToAlwaysStageAsUFS");
					// NonUFS files are never in pak files and should always be remapped
					StageAdditionalDirectoriesFromConfig(SC, ProjectContentRoot, StageContentRoot, PlatformGameConfig, false, "DirectoriesToAlwaysStageAsNonUFS");

					if (SC.DedicatedServer)
					{
						StageAdditionalDirectoriesFromConfig(SC, ProjectContentRoot, StageContentRoot, PlatformGameConfig, true, "DirectoriesToAlwaysStageAsUFSServer");
						// NonUFS files are never in pak files and should always be remapped
						StageAdditionalDirectoriesFromConfig(SC, ProjectContentRoot, StageContentRoot, PlatformGameConfig, false, "DirectoriesToAlwaysStageAsNonUFSServer");
					}
				}

				// Per-project, per-platform setting to skip all movies. By default this is false.
				bool bSkipMovies = false;
				PlatformGameConfig.GetBool("/Script/UnrealEd.ProjectPackagingSettings", "bSkipMovies", out bSkipMovies);

				if (!bSkipMovies && !SC.DedicatedServer)
				{
					// UFS is required when using a file server
					StagedFileType MovieFileType = Params.FileServer ? StagedFileType.UFS : StagedFileType.NonUFS;

					DirectoryReference EngineMoviesDir = DirectoryReference.Combine(SC.EngineRoot, "Content", "Movies");
					if (DirectoryReference.Exists(EngineMoviesDir))
					{
						List<FileReference> MovieFiles = SC.FindFilesToStage(EngineMoviesDir, StageFilesSearch.AllDirectories);
						SC.StageFiles(MovieFileType, MovieFiles.Where(x => !x.HasExtension(".uasset") && !x.HasExtension(".umap")));
					}

					DirectoryReference ProjectMoviesDir = DirectoryReference.Combine(SC.ProjectRoot, "Content", "Movies");
					if (DirectoryReference.Exists(ProjectMoviesDir))
					{
						List<FileReference> MovieFiles = SC.FindFilesToStage(ProjectMoviesDir, StageFilesSearch.AllDirectories);
						SC.StageFiles(MovieFileType, MovieFiles.Where(x => !x.HasExtension(".uasset") && !x.HasExtension(".umap")));
					}
				}
				else if (!SC.DedicatedServer)
				{
					// check to see if we have any specific movies we want to stage for non ufs and ufs files
					// we still use the movie directories to find the paths to the movies and the list in the ini file is just the substring filename you wish to have staged
					List<string> NonUFSMovieList;
					List<string> UFSMovieList;
					PlatformGameConfig.GetArray("/Script/UnrealEd.ProjectPackagingSettings", "NonUFSMovies", out NonUFSMovieList);
					PlatformGameConfig.GetArray("/Script/UnrealEd.ProjectPackagingSettings", "UFSMovies", out UFSMovieList);
					DirectoryReference EngineMoviesDir = DirectoryReference.Combine(SC.EngineRoot, "Content", "Movies");
					if (DirectoryReference.Exists(EngineMoviesDir))
					{
						List<FileReference> MovieFiles = SC.FindFilesToStage(EngineMoviesDir, StageFilesSearch.AllDirectories);
                        if (NonUFSMovieList != null)
                        {
						    SC.StageFiles(StagedFileType.NonUFS, MovieFiles.Where(x => !x.HasExtension(".uasset") && !x.HasExtension(".umap") && NonUFSMovieList.Where(y => x.GetFileNameWithoutExtension().Contains(y)).Any()));
                        }
                        if (UFSMovieList != null)
                        {
						    SC.StageFiles(StagedFileType.UFS, MovieFiles.Where(x => !x.HasExtension(".uasset") && !x.HasExtension(".umap") && UFSMovieList.Where(y => x.GetFileNameWithoutExtension().Contains(y)).Any()));
                        }
					}

					DirectoryReference ProjectMoviesDir = DirectoryReference.Combine(SC.ProjectRoot, "Content", "Movies");
					if (DirectoryReference.Exists(ProjectMoviesDir))
					{
						List<FileReference> MovieFiles = SC.FindFilesToStage(ProjectMoviesDir, StageFilesSearch.AllDirectories);
                        if (NonUFSMovieList != null)
                        {
                            SC.StageFiles(StagedFileType.NonUFS, MovieFiles.Where(x => !x.HasExtension(".uasset") && !x.HasExtension(".umap") && NonUFSMovieList.Where(y => x.GetFileNameWithoutExtension().Contains(y)).Any()));
                        }
                        if (UFSMovieList != null)
                        {
                            SC.StageFiles(StagedFileType.UFS, MovieFiles.Where(x => !x.HasExtension(".uasset") && !x.HasExtension(".umap") && UFSMovieList.Where(y => x.GetFileNameWithoutExtension().Contains(y)).Any()));
                        }
					}
				}

				// shader cache
				{
					DirectoryReference ShaderCacheRoot = DirectoryReference.Combine(SC.ProjectRoot, "Content");
					List<FileReference> ShaderCacheFiles = SC.FindFilesToStage(ShaderCacheRoot, "DrawCache-*.ushadercache", StageFilesSearch.TopDirectoryOnly);
					SC.StageFiles(StagedFileType.UFS, ShaderCacheFiles);
				}
                // pipeline cache
                {
                    DirectoryReference ShaderCacheRoot = DirectoryReference.Combine(SC.ProjectRoot, "Content", "PipelineCaches", SC.PlatformDir);
                    if (DirectoryReference.Exists(ShaderCacheRoot))
                    {
                        List<FileReference> ShaderCacheFiles = SC.FindFilesToStage(ShaderCacheRoot, "*.upipelinecache", StageFilesSearch.TopDirectoryOnly);
                        SC.StageFiles(StagedFileType.UFS, ShaderCacheFiles);
                    }
                }

				// Get the final output directory for cooked data
				DirectoryReference CookOutputDir;
				if (!String.IsNullOrEmpty(Params.CookOutputDir))
				{
					CookOutputDir = DirectoryReference.Combine(new DirectoryReference(Params.CookOutputDir), SC.CookPlatform);
				}
				else if (Params.CookInEditor)
				{
					CookOutputDir = DirectoryReference.Combine(SC.ProjectRoot, "Saved", "EditorCooked", SC.CookPlatform);
				}
				else
				{
					CookOutputDir = DirectoryReference.Combine(SC.ProjectRoot, "Saved", "Cooked", SC.CookPlatform);
				}

				SC.MetadataDir = DirectoryReference.Combine(CookOutputDir, SC.ShortProjectName, "Metadata");

				// Stage all the cooked data. Currently not filtering this by restricted folders, since we shouldn't mask invalid references by filtering them out.
				List<FileReference> CookedFiles = DirectoryReference.EnumerateFiles(CookOutputDir, "*", SearchOption.AllDirectories).ToList();
				foreach (FileReference CookedFile in CookedFiles)
				{
					// Skip metadata directory
					if (CookedFile.Directory.IsUnderDirectory(SC.MetadataDir))
					{
						continue;
					}

					// json files have never been staged
					// metallib files cannot *currently* be staged as UFS as the Metal API needs to mmap them from files on disk in order to function efficiently
					if (!CookedFile.HasExtension(".json") && !CookedFile.HasExtension(".metallib"))
					{
						SC.StageFile(StagedFileType.UFS, CookedFile, new StagedFileReference(CookedFile.MakeRelativeTo(CookOutputDir)));
					}
				}

				// CrashReportClient is a standalone slate app that does not look in the generated pak file, so it needs the Content/Slate and Shaders/StandaloneRenderer folders Non-UFS
				// @todo Make CrashReportClient more portable so we don't have to do this
				if (SC.bStageCrashReporter && PlatformSupportsCrashReporter(SC.StageTargetPlatform.PlatformType) && (Params.IterateSharedBuildUsePrecompiledExe == false))
				{
					SC.StageCrashReporterFiles(StagedFileType.UFS, DirectoryReference.Combine(SC.EngineRoot, "Content", "Slate"), StageFilesSearch.AllDirectories);
					SC.StageCrashReporterFiles(StagedFileType.UFS, DirectoryReference.Combine(SC.EngineRoot, "Shaders", "StandaloneRenderer"), StageFilesSearch.AllDirectories);

					SC.StageCrashReporterFiles(StagedFileType.UFS, DirectoryReference.Combine(SC.EngineRoot, "Content", "Internationalization", InternationalizationPreset), StageFilesSearch.AllDirectories, new StagedDirectoryReference("Engine/Content/Internationalization"));

					// Get the architecture in use
					string Architecture = Params.SpecifiedArchitecture;
					if (string.IsNullOrEmpty(Architecture))
					{
						Architecture = "";
						if (PlatformExports.IsPlatformAvailable(SC.StageTargetPlatform.PlatformType))
						{
							Architecture = PlatformExports.GetDefaultArchitecture(SC.StageTargetPlatform.PlatformType, Params.RawProjectPath);
						}
					}

					// Get the target receipt path for CrashReportClient
					FileReference ReceiptFileName = TargetReceipt.GetDefaultPath(SC.EngineRoot, "CrashReportClient", SC.StageTargetPlatform.PlatformType, UnrealTargetConfiguration.Shipping, Architecture);
					if (!FileReference.Exists(ReceiptFileName))
					{
						throw new AutomationException(ExitCode.Error_MissingExecutable, "Stage Failed. Missing receipt '{0}'. Check that this target has been built.", ReceiptFileName);
					}

					// Read the receipt for this target
					TargetReceipt Receipt;
					if (!TargetReceipt.TryRead(ReceiptFileName, SC.EngineRoot, null, out Receipt))
					{
						throw new AutomationException("Missing or invalid target receipt ({0})", ReceiptFileName);
					}

					foreach(RuntimeDependency RuntimeDependency in Receipt.RuntimeDependencies)
					{
						StagedFileReference StagedFile = new StagedFileReference(RuntimeDependency.Path.MakeRelativeTo(RootDirectory));
						SC.StageCrashReporterFile(RuntimeDependency.Type, RuntimeDependency.Path, StagedFile);
					}

					// Add config files.
					SC.StageCrashReporterFiles(StagedFileType.UFS, DirectoryReference.Combine(SC.EngineRoot, "Programs", "CrashReportClient", "Config"), StageFilesSearch.AllDirectories);
				}

				// check if the game will be verifying ssl connections - if not, we can skip staging files that won't be needed
				bool bStageSSLCertificates = false;
				ConfigHierarchy PlatformEngineConfig = ConfigCache.ReadHierarchy(ConfigHierarchyType.Engine, DirectoryReference.FromFile(Params.RawProjectPath), SC.StageTargetPlatform.IniPlatformType);
				if (PlatformEngineConfig != null)
				{
					PlatformEngineConfig.GetBool("/Script/Engine.NetworkSettings", "n.VerifyPeer", out bStageSSLCertificates);
				}

				if (bStageSSLCertificates)
				{
					// Game's SSL certs
					FileReference ProjectCertFile = FileReference.Combine(SC.ProjectRoot, "Content", "Certificates", "cacert.pem");
					if (FileReference.Exists(ProjectCertFile))
					{
						SC.StageFile(StagedFileType.UFS, ProjectCertFile);
					}
					else
					{
						// if the game had any files to be staged, then we don't need to stage the engine one - it will just added hundreds of kb of data that is never used
						FileReference EngineCertFile = FileReference.Combine(SC.EngineRoot, "Content", "Certificates", "ThirdParty", "cacert.pem");
						if (FileReference.Exists(EngineCertFile))
						{
							SC.StageFile(StagedFileType.UFS, EngineCertFile);
						}
					}

					// now stage any other game certs besides cacert
					DirectoryReference CertificatesDir = DirectoryReference.Combine(SC.ProjectRoot, "Certificates");
					if (DirectoryReference.Exists(CertificatesDir))
					{
						SC.StageFiles(StagedFileType.UFS, CertificatesDir, "*.pem", StageFilesSearch.AllDirectories);
					}
				}

				// Generate a plugin manifest if we're using a pak file and not creating a mod. Mods can be enumerated independently by users copying them into the Mods directory.
				if (Params.UsePak(SC.StageTargetPlatform))
				{
					if (Params.HasDLCName)
					{
						CreatePluginManifest(SC, SC.FilesToStage.NonUFSFiles, StagedFileType.NonUFS, Params.DLCFile.GetFileNameWithoutExtension());
					}
					else
					{
						CreatePluginManifest(SC, SC.FilesToStage.UFSFiles, StagedFileType.UFS, Params.ShortProjectName);
					}
				}
			}
			else
			{
				if (PlatformGameConfig != null)
				{
					List<string> ExtraNonUFSDirs;
					if (PlatformGameConfig.GetArray("/Script/UnrealEd.ProjectPackagingSettings", "DirectoriesToAlwaysStageAsNonUFS", out ExtraNonUFSDirs))
					{
						// Each string has the format '(Path="TheDirToStage")'
						foreach (var PathStr in ExtraNonUFSDirs)
						{
							var PathParts = PathStr.Split('"');
							if (PathParts.Length == 3)
							{
								var RelativePath = PathParts[1];
								SC.StageFiles(StagedFileType.NonUFS, DirectoryReference.Combine(ProjectContentRoot, RelativePath), StageFilesSearch.AllDirectories);
							}
							else if (PathParts.Length == 1)
							{
								var RelativePath = PathParts[0];
								SC.StageFiles(StagedFileType.UFS, DirectoryReference.Combine(ProjectContentRoot, RelativePath), StageFilesSearch.AllDirectories, StagedDirectoryReference.Combine(StageContentRoot, RelativePath));
							}
						}
					}
				}

				// UE-58423
				DirectoryReference CookOutputDir;
				if (!String.IsNullOrEmpty(Params.CookOutputDir))
				{
					CookOutputDir = DirectoryReference.Combine(new DirectoryReference(Params.CookOutputDir), SC.CookPlatform);
				}
				else if (Params.CookInEditor)
				{
					CookOutputDir = DirectoryReference.Combine(SC.ProjectRoot, "Saved", "EditorCooked", SC.CookPlatform);
				}
				else
				{
					CookOutputDir = DirectoryReference.Combine(SC.ProjectRoot, "Saved", "Cooked", SC.CookPlatform);
				}
				SC.MetadataDir = DirectoryReference.Combine(CookOutputDir, SC.ShortProjectName, "Metadata");
			}
		}

		// Apply all the directory mappings
		SC.FilesToStage.UFSFiles = SC.FilesToStage.UFSFiles.ToDictionary(x => ApplyDirectoryRemap(SC, x.Key), x => x.Value);
		SC.FilesToStage.NonUFSFiles = SC.FilesToStage.NonUFSFiles.ToDictionary(x => ApplyDirectoryRemap(SC, x.Key), x => x.Value);
		SC.FilesToStage.NonUFSDebugFiles = SC.FilesToStage.NonUFSDebugFiles.ToDictionary(x => ApplyDirectoryRemap(SC, x.Key), x => x.Value);
		SC.FilesToStage.NonUFSSystemFiles = SC.FilesToStage.NonUFSSystemFiles.ToDictionary(x => ApplyDirectoryRemap(SC, x.Key), x => x.Value);

		// Make all the filenames lowercase
		if (SC.StageTargetPlatform.DeployLowerCaseFilenames())
		{
			SC.FilesToStage.NonUFSFiles = SC.FilesToStage.NonUFSFiles.ToDictionary(x => x.Key.ToLowerInvariant(), x => x.Value);
			if (!Params.UsePak(SC.StageTargetPlatform))
			{
				SC.FilesToStage.UFSFiles = SC.FilesToStage.UFSFiles.ToDictionary(x => x.Key.ToLowerInvariant(), x => x.Value);
			}
		}

		// Remap all the non-ufs files if not using a PAK file
		SC.FilesToStage.NonUFSFiles = SC.FilesToStage.NonUFSFiles.ToDictionary(x => SC.StageTargetPlatform.Remap(x.Key), x => x.Value);
		if (!Params.UsePak(SC.StageTargetPlatform))
		{
			SC.FilesToStage.UFSFiles = SC.FilesToStage.UFSFiles.ToDictionary(x => SC.StageTargetPlatform.Remap(x.Key), x => x.Value);
		}

		// Merge all the NonUFS system files back into the NonUFS list. Deployment is currently only set up to read from that.
		foreach (KeyValuePair<StagedFileReference, FileReference> Pair in SC.FilesToStage.NonUFSSystemFiles)
		{
			SC.FilesToStage.NonUFSFiles[Pair.Key] = Pair.Value;
		}

		// Make sure there are no restricted folders in the output
		HashSet<StagedFileReference> RestrictedFiles = new HashSet<StagedFileReference>();
		foreach (FileSystemName RestrictedName in SC.RestrictedFolderNames)
		{
			RestrictedFiles.UnionWith(SC.FilesToStage.UFSFiles.Keys.Where(x => x.ContainsName(RestrictedName)));
			RestrictedFiles.UnionWith(SC.FilesToStage.NonUFSFiles.Keys.Where(x => x.ContainsName(RestrictedName)));
			RestrictedFiles.UnionWith(SC.FilesToStage.NonUFSDebugFiles.Keys.Where(x => x.ContainsName(RestrictedName)));
			RestrictedFiles.UnionWith(SC.CrashReporterUFSFiles.Keys.Where(x => x.ContainsName(RestrictedName)));
		}
		RestrictedFiles.RemoveWhere(RestrictedFile => SC.WhitelistDirectories.Any(WhitelistDirectory => RestrictedFile.Directory.IsUnderDirectory(WhitelistDirectory)));

		if (RestrictedFiles.Count > 0)
		{
			List<string> RestrictedNames = new List<string>();
			foreach(FileSystemName RestrictedFolderName in SC.RestrictedFolderNames)
			{
				if(RestrictedFiles.Any(x => x.ContainsName(RestrictedFolderName)))
				{
					RestrictedNames.Add(String.Format("\"{0}\"", RestrictedFolderName));
				}
			}

			StringBuilder Message = new StringBuilder();
			Message.AppendFormat("The following files are set to be staged, but contain restricted folder names ({0}):", String.Join(", ", RestrictedNames));
			foreach (StagedFileReference RestrictedFile in RestrictedFiles.OrderBy(x => x.Name))
			{
				Message.AppendFormat("\n{0}", RestrictedFile);
			}
			Message.Append("\n[Restrictions]");
			foreach (FileSystemName RestrictedName in SC.RestrictedFolderNames)
			{
				Message.AppendFormat("\n{0}", RestrictedName);
			}
			Message.Append("\nIf these files are intended to be distributed in packaged builds, move the source files out of a restricted folder, or remap them during staging using the following syntax in DefaultGame.ini:");
			Message.Append("\n[Staging]");
			Message.Append("\n+RemapDirectories=(From=\"Foo/NoRedist\", To=\"Foo\")");
			if(RestrictedNames.Any(x => x != "NotForLicensees" && x != "NoRedist")) // We don't ever want internal stuff white-listing folders like this
			{
				Message.Append("\nAlternatively, whitelist them using this syntax in DefaultGame.ini:");
				Message.Append("\n[Staging]");
				Message.Append("\n+WhitelistDirectories=MyGame/Content/Foo");
			}
			throw new AutomationException(Message.ToString());
		}
	}

	/// <summary>
	/// Stage the appropriate config files from the given input directory
	/// </summary>
	/// <param name="SC">The staging context</param>
	/// <param name="ConfigDir">Directory containing the config files</param>
	static void StageConfigFiles(DeploymentContext SC, DirectoryReference ConfigDir)
	{
		List<FileReference> ConfigFiles = SC.FindFilesToStage(ConfigDir, "*.ini", StageFilesSearch.AllDirectories);
		foreach(FileReference ConfigFile in ConfigFiles)
		{
			Nullable<bool> ShouldStage = ShouldStageConfigFile(SC, ConfigDir, ConfigFile);
			if(ShouldStage == null)
			{
				CommandUtils.LogWarning("The config file '{0}' will be staged, but is not whitelisted or blacklisted. Add +WhitelistConfigFiles={0} or +BlacklistConfigFiles={0} to the [Staging] section of DefaultGame.ini", SC.GetStagedFileLocation(ConfigFile));
			}

			if(ShouldStage ?? true)
			{
				SC.StageFile(StagedFileType.UFS, ConfigFile);
			}
			else
			{
				CommandUtils.Log("Excluding config file {0}", ConfigFile);
			}
		}
	}

	/// <summary>
	/// Determines if an individual config file should be staged
	/// </summary>
	/// <param name="SC">The staging context</param>
	/// <param name="ConfigDir">Directory containing the config files</param>
	/// <param name="ConfigFile">The config file to check</param>
	/// <returns>True if the file should be staged, false otherwise</returns>
	static Nullable<bool> ShouldStageConfigFile(DeploymentContext SC, DirectoryReference ConfigDir, FileReference ConfigFile)
	{
		StagedFileReference StagedConfigFile = SC.GetStagedFileLocation(ConfigFile);
		if(SC.WhitelistConfigFiles.Contains(StagedConfigFile))
	{
			return true;
		}
		if(SC.BlacklistConfigFiles.Contains(StagedConfigFile))
		{
			return false;
		}

		string NormalizedPath = ConfigFile.MakeRelativeTo(ConfigDir).ToLowerInvariant().Replace('\\', '/');

		int DirectoryIdx = NormalizedPath.IndexOf('/');
		if(DirectoryIdx == -1)
		{
			const string BasePrefix = "base";
			if(NormalizedPath.StartsWith(BasePrefix))
			{
				return ShouldStageConfigSuffix(SC, ConfigFile, NormalizedPath.Substring(BasePrefix.Length));
			}

			const string DefaultPrefix = "default";
			if(NormalizedPath.StartsWith(DefaultPrefix))
			{
				return ShouldStageConfigSuffix(SC, ConfigFile, NormalizedPath.Substring(DefaultPrefix.Length));
			}

			const string DedicatedServerPrefix = "dedicatedserver";
			if(NormalizedPath.StartsWith(DedicatedServerPrefix))
			{
				return SC.DedicatedServer? ShouldStageConfigSuffix(SC, ConfigFile, NormalizedPath.Substring(DedicatedServerPrefix.Length)) : false;
			}

			if(NormalizedPath == "consolevariables.ini")
			{
				return SC.StageTargetConfigurations.Any(x => x != UnrealTargetConfiguration.Test && x != UnrealTargetConfiguration.Shipping);
			}

			if(NormalizedPath == "locgatherconfig.ini")
			{
				return false;
			}

			if(NormalizedPath == "designertoolsconfig.ini")
			{
				return false;
			}
		}
		else
		{
			if (NormalizedPath.StartsWith("localization/"))
			{
				return false;
			}

			string PlatformPrefix = String.Format("{0}/{0}", NormalizedPath.Substring(0, DirectoryIdx));
			if(NormalizedPath.StartsWith(PlatformPrefix))
			{
				return ShouldStageConfigSuffix(SC, ConfigFile, NormalizedPath.Substring(PlatformPrefix.Length));
			}

			string PlatformBasePrefix = String.Format("{0}/base{0}", NormalizedPath.Substring(0, DirectoryIdx));
			if(NormalizedPath.StartsWith(PlatformBasePrefix))
			{
				return ShouldStageConfigSuffix(SC, ConfigFile, NormalizedPath.Substring(PlatformBasePrefix.Length));
			}

			if(NormalizedPath.EndsWith("/confidentialplatform.ini"))
			{
				return false;
			}

			if (NormalizedPath.EndsWith("/datadrivenplatforminfo.ini"))
			{
				return true;
			}

		}
		return null;
	}

	/// <summary>
	/// Determines if the given config file suffix ("engine", "game", etc...) should be staged for the given context.
	/// </summary>
	/// <param name="SC">The staging context</param>
	/// <param name="ConfigFile">Full path to the config file</param>
	/// <param name="InvariantSuffix">Suffix for the config file, as a lowercase invariant string</param>
	/// <returns>True if the suffix should be staged, false if not, null if unknown</returns>
	static Nullable<bool> ShouldStageConfigSuffix(DeploymentContext SC, FileReference ConfigFile, string InvariantSuffix)
	{
		switch(InvariantSuffix)
		{
			case ".ini":
			case "compat.ini":
			case "deviceprofiles.ini":
			case "engine.ini":
			case "enginechunkoverrides.ini":
			case "game.ini":
			case "gameplaytags.ini":
			case "gameusersettings.ini":
			case "hardware.ini":
			case "input.ini":
			case "scalability.ini":
			case "runtimeoptions.ini":
				return true;
			case "encryption.ini":
			case "crypto.ini":
			case "editor.ini":
			case "editorgameagnostic.ini":
			case "editorkeybindings.ini":
			case "editorlayout.ini":
			case "editorperprojectusersettings.ini":
			case "editorsettings.ini":
			case "editorusersettings.ini":
			case "lightmass.ini":
				return false;
			default:
				return null;
		}
	}

	static StagedFileReference ApplyDirectoryRemap(DeploymentContext SC, StagedFileReference InputFile)
	{
		StagedFileReference CurrentFile = InputFile;
		foreach (Tuple<StagedDirectoryReference, StagedDirectoryReference> RemapDirectory in SC.RemapDirectories)
		{
			StagedFileReference NewFile;
			if (StagedFileReference.TryRemap(CurrentFile, RemapDirectory.Item1, RemapDirectory.Item2, out NewFile))
			{
				CurrentFile = NewFile;
			}
		}
		return CurrentFile;
	}

	static List<string> ParseInputPaths(List<string> ConfigLines)
	{
		// Each string has the format '(Path="TheDirToStage")'
		List<string> InputPaths = new List<string>();
		foreach (string ConfigLine in ConfigLines)
		{
			string[] PathParts = ConfigLine.Split('"');
			if (PathParts.Length == 3)
			{
				InputPaths.Add(PathParts[1]);
			}
			else if (PathParts.Length == 1)
			{
				InputPaths.Add(PathParts[0]);
			}
		}
		return InputPaths;
	}

	static void CreatePluginManifest(DeploymentContext SC, Dictionary<StagedFileReference, FileReference> FileMapping, StagedFileType FileType, string ManifestName)
	{
		// Get the path to the project's mods directory. We wont include anything under here in the manifest
		DirectoryReference ModsDir = DirectoryReference.Combine(SC.ProjectRoot, "Mods");

		// Find all the plugins that are being staged
		Dictionary<StagedFileReference, PluginDescriptor> StagedPlugins = new Dictionary<StagedFileReference, PluginDescriptor>();
		foreach (KeyValuePair<StagedFileReference, FileReference> File in FileMapping)
		{
			if (!File.Value.IsUnderDirectory(ModsDir) && File.Value.HasExtension(".uplugin"))
			{
				PluginDescriptor Descriptor = PluginDescriptor.FromFile(File.Value);
				StagedPlugins[File.Key] = Descriptor;
			}
		}

		// If we have plugins, write out the manifest
		if (StagedPlugins.Count > 0)
		{
			string PluginManifestName = String.Format("{0}.upluginmanifest", ManifestName);

			FileReference PluginManifestFile = FileReference.Combine(SC.ProjectRoot, "Intermediate", "Staging", PluginManifestName);
			DirectoryReference.CreateDirectory(PluginManifestFile.Directory);

			using (JsonWriter Writer = new JsonWriter(PluginManifestFile.FullName))
			{
				Writer.WriteObjectStart();
				Writer.WriteArrayStart("Contents");
				foreach (KeyValuePair<StagedFileReference, PluginDescriptor> StagedPlugin in StagedPlugins)
				{
					Writer.WriteObjectStart();
					Writer.WriteValue("File", String.Format("../../../{0}", StagedPlugin.Key.Name));
					Writer.WriteObjectStart("Descriptor");
					StagedPlugin.Value.Write(Writer);
					Writer.WriteObjectEnd();
					Writer.WriteObjectEnd();
				}
				Writer.WriteArrayEnd();
				Writer.WriteObjectEnd();
			}

			SC.StageFile(FileType, PluginManifestFile, StagedFileReference.Combine(SC.RelativeProjectRootForStage, "Plugins", PluginManifestName));
		}
	}

	public static void DumpTargetManifest(Dictionary<StagedFileReference, FileReference> Mapping, FileReference Filename, DirectoryReference StageDir, HashSet<StagedFileReference> CRCFiles)
	{
		// const string Iso8601DateTimeFormat = "yyyy-MM-ddTHH:mm:ssZ"; // probably should work
		// const string Iso8601DateTimeFormat = "o"; // predefined universal Iso standard format (has too many millisecond spaces for our read code in FDateTime.ParseISO8601
		const string Iso8601DateTimeFormat = "yyyy'-'MM'-'dd'T'HH':'mm':'ss'.'fffZ";


		if (Mapping.Count > 0)
		{
			var Lines = new List<string>();
			foreach (var Pair in Mapping)
			{
				string TimeStamp = FileReference.GetLastWriteTimeUtc(Pair.Value).ToString(Iso8601DateTimeFormat);
				if (CRCFiles.Contains(Pair.Key))
				{
					byte[] FileData = File.ReadAllBytes(StageDir + "/" + Pair.Key);
					TimeStamp = BitConverter.ToString(System.Security.Cryptography.MD5.Create().ComputeHash(FileData)).Replace("-", string.Empty);
				}
				string Dest = Pair.Key.Name + "\t" + TimeStamp;

				Lines.Add(Dest);
			}

			DirectoryReference.CreateDirectory(Filename.Directory);
			WriteAllLines(Filename.FullName, Lines.ToArray());
		}
	}

	public static void CopyManifestFilesToStageDir(Dictionary<StagedFileReference, FileReference> Mapping, DirectoryReference StageDir, DirectoryReference ManifestDir, string ManifestName, HashSet<StagedFileReference> CRCFiles, string PlatformName)
	{
		Log("Copying {0} to staging directory: {1}", ManifestName, StageDir);
		FileReference ManifestPath = null;
		string ManifestFile = "";
		if (!String.IsNullOrEmpty(ManifestName))
		{
			ManifestFile = "Manifest_" + ManifestName + "_" + PlatformName + ".txt";
			ManifestPath = FileReference.Combine(ManifestDir, ManifestFile);
			DeleteFile(ManifestPath.FullName);
		}
		foreach (KeyValuePair<StagedFileReference, FileReference> Pair in Mapping)
		{
			FileReference Src = Pair.Value;
			FileReference Dest = FileReference.Combine(StageDir, Pair.Key.Name);
			if (Src != Dest)  // special case for things created in the staging directory, like the pak file
			{
				CopyFileIncremental(Src, Dest, bFilterSpecialLinesFromIniFiles: true);
			}
		}
		if (ManifestPath != null && Mapping.Count > 0)
		{
			DumpTargetManifest(Mapping, ManifestPath, StageDir, CRCFiles);
			if (!FileReference.Exists(ManifestPath))
			{
				throw new AutomationException("Failed to write manifest {0}", ManifestPath);
			}
			CopyFile(ManifestPath.FullName, CombinePaths(CmdEnv.LogFolder, ManifestFile));
		}
	}

	public static void DumpManifest(Dictionary<StagedFileReference, FileReference> Mapping, string Filename)
	{
		if (Mapping.Count > 0)
		{
			List<string> Lines = new List<string>();
			foreach (KeyValuePair<StagedFileReference, FileReference> Pair in Mapping)
			{
				Lines.Add("\"" + Pair.Value + "\" \"" + Pair.Key + "\"");
			}
			WriteAllLines(Filename, Lines.ToArray());
		}
	}

	public static void DumpManifest(DeploymentContext SC, string BaseFilename, bool DumpUFSFiles = true)
	{
		DumpManifest(SC.FilesToStage.NonUFSFiles, BaseFilename + "_NonUFSFiles.txt");
		if (DumpUFSFiles)
		{
			DumpManifest(SC.FilesToStage.NonUFSDebugFiles, BaseFilename + "_NonUFSFilesDebug.txt");
		}
		DumpManifest(SC.FilesToStage.UFSFiles, BaseFilename + "_UFSFiles.txt");
	}

	public static void CopyUsingStagingManifest(ProjectParams Params, DeploymentContext SC)
	{
		CopyManifestFilesToStageDir(SC.FilesToStage.NonUFSFiles, SC.StageDirectory, SC.DebugStageDirectory, "NonUFSFiles", SC.StageTargetPlatform.GetFilesForCRCCheck(), SC.StageTargetPlatform.PlatformType.ToString());

		if (SC.DebugStageDirectory != null)
		{
			CopyManifestFilesToStageDir(SC.FilesToStage.NonUFSDebugFiles, SC.DebugStageDirectory, SC.DebugStageDirectory, "DebugFiles", SC.StageTargetPlatform.GetFilesForCRCCheck(), SC.StageTargetPlatform.PlatformType.ToString());
		}

		bool bStageUnrealFileSystemFiles = !Params.CookOnTheFly && !Params.UsePak(SC.StageTargetPlatform) && !Params.FileServer;
		if (bStageUnrealFileSystemFiles)
		{
			Dictionary<StagedFileReference, FileReference> UFSFiles = new Dictionary<StagedFileReference, FileReference>(SC.FilesToStage.UFSFiles);
			foreach(KeyValuePair<StagedFileReference, FileReference> Pair in SC.CrashReporterUFSFiles)
			{
				FileReference ExistingLocation;
				if(!UFSFiles.TryGetValue(Pair.Key, out ExistingLocation))
				{
					UFSFiles.Add(Pair.Key, Pair.Value);
				}
				else if(ExistingLocation != Pair.Value)
				{
					throw new AutomationException("File '{0}' is set to be staged from '{1}' for project and '{2}' for crash reporter", Pair.Key, ExistingLocation, Pair.Value);
				}
			}
			CopyManifestFilesToStageDir(UFSFiles, SC.StageDirectory, SC.DebugStageDirectory, "UFSFiles", SC.StageTargetPlatform.GetFilesForCRCCheck(), SC.StageTargetPlatform.PlatformType.ToString());
		}
	}

	/// <summary>
	/// Creates a pak file using staging context (single manifest)
	/// </summary>
	/// <param name="Params"></param>
	/// <param name="SC"></param>
	private static void CreatePakUsingStagingManifest(ProjectParams Params, DeploymentContext SC)
	{
		Log("Creating pak using staging manifest.");

		DumpManifest(SC, CombinePaths(CmdEnv.LogFolder, "PrePak" + (SC.DedicatedServer ? "_Server" : "")));

		var UnrealPakResponseFile = CreatePakResponseFileFromStagingManifest(SC, SC.FilesToStage.UFSFiles);

        EncryptionAndSigning.CryptoSettings PakCryptoSettings = EncryptionAndSigning.ParseCryptoSettings(DirectoryReference.FromFile(Params.RawProjectPath), SC.StageTargetPlatform.IniPlatformType);
		FileReference CryptoKeysCacheFilename = FileReference.Combine(SC.MetadataDir, "Crypto.json");
		PakCryptoSettings.Save(CryptoKeysCacheFilename);

		CreatePak(Params, SC, UnrealPakResponseFile, SC.ShortProjectName, PakCryptoSettings, CryptoKeysCacheFilename, Params.Compressed);
	}

	/// <summary>
	/// Creates a standalone pak file for crash reporter
	/// </summary>
	/// <param name="Params">The packaging parameters</param>
	/// <param name="SC">Staging context</param>
	private static void CreatePakForCrashReporter(ProjectParams Params, DeploymentContext SC)
	{
		Log("Creating pak for crash reporter.");

		Dictionary<string, string> PakResponseFile = CreatePakResponseFileFromStagingManifest(SC, SC.CrashReporterUFSFiles);
		FileReference OutputLocation = FileReference.Combine(SC.RuntimeRootDir, "Engine", "Programs", "CrashReportClient", "Content", "Paks", "CrashReportClient.pak");

		RunUnrealPak(PakResponseFile, OutputLocation, null, null, Params.Compressed, null, null, null);
	}

	/// <summary>
	/// Creates a pak response file using stage context
	/// </summary>
	/// <param name="SC"></param>
	/// <returns></returns>
	private static Dictionary<string, string> CreatePakResponseFileFromStagingManifest(DeploymentContext SC, Dictionary<StagedFileReference, FileReference> FilesToStage)
	{
		// look for optional packaging blacklist if only one config active
		List<string> Blacklist = null;
		if (SC.StageTargetConfigurations.Count == 1)
		{
			// automatically add DefaultBloomKernel to blacklist for mobile platforms (hotfix version)
			if (SC.PlatformDir == "Android" || SC.PlatformDir == "IOS" || SC.PlatformDir == "TVOS" || SC.PlatformDir == "HTML5")
			{
				Blacklist = new List<string>();
				Blacklist.Add("../../../Engine/Content/EngineMaterials/DefaultBloomKernel.uasset");
				Blacklist.Add("../../../Engine/Content/EngineMaterials/DefaultBloomKernel.uexp");
			}

			FileReference PakBlacklistFilename = FileReference.Combine(SC.ProjectRoot, "Build", SC.PlatformDir, string.Format("PakBlacklist-{0}.txt", SC.StageTargetConfigurations[0].ToString()));
			if (FileReference.Exists(PakBlacklistFilename))
			{
				Log("Applying PAK blacklist file {0}", PakBlacklistFilename);
				string[] BlacklistContents = FileReference.ReadAllLines(PakBlacklistFilename);
				foreach (string Candidate in BlacklistContents)
				{
					if (Candidate.Trim().Length > 0)
					{
						if (Blacklist == null)
						{
							Blacklist = new List<string>();
						}
						Blacklist.Add(Candidate);
					}
				}
			}
		}

		var UnrealPakResponseFile = new Dictionary<string, string>(StringComparer.InvariantCultureIgnoreCase);
		foreach (KeyValuePair<StagedFileReference, FileReference> Pair in FilesToStage)
		{
			FileReference Src = Pair.Value;
			string Dest = Pair.Key.Name;

			Dest = CombinePaths(PathSeparator.Slash, SC.PakFileInternalRoot, Dest);

			if (Blacklist != null)
			{
				bool bExcludeFile = false;
				foreach (string ExcludePath in Blacklist)
				{
					if (Dest.StartsWith(ExcludePath))
					{
						bExcludeFile = true;
						break;
					}
				}

				if (bExcludeFile)
				{
					Log("Excluding {0}", Src);
					continue;
				}
			}

			// Do a filtered copy of all ini files to allow stripping of values that we don't want to distribute
			if (Src.HasExtension(".ini"))
			{
				string SubFolder = Pair.Key.Name.Replace('/', Path.DirectorySeparatorChar);
				FileReference NewIniFilename = FileReference.Combine(SC.ProjectRoot, "Saved", "Temp", SC.PlatformDir, SubFolder);
				InternalUtils.SafeCreateDirectory(NewIniFilename.Directory.FullName, true);
				InternalUtils.SafeCopyFile(Src.FullName, NewIniFilename.FullName, bFilterSpecialLinesFromIniFiles: true);
				Src = NewIniFilename;
			}

			// there can be files that only differ in case only, we don't support that in paks as paks are case-insensitive
			if (UnrealPakResponseFile.ContainsKey(Src.FullName))
			{
				if (UnrealPakResponseFile[Src.FullName] != Dest)
				{
					throw new AutomationException("Staging manifest already contains {0} (or a file that differs in case only)", Src);
				}
				LogWarning("Tried to add duplicate file to stage " + Src + " ignoring second attempt pls fix");
				continue;
			}

			UnrealPakResponseFile.Add(Src.FullName, Dest);
		}

		return UnrealPakResponseFile;
	}


	/// <summary>
	/// Creates a pak file using response file.
	/// </summary>
	/// <param name="Params"></param>
	/// <param name="SC"></param>
	/// <param name="UnrealPakResponseFile"></param>
	/// <param name="PakName"></param>
	private static void CreatePak(ProjectParams Params, DeploymentContext SC, Dictionary<string, string> UnrealPakResponseFile, string PakName, EncryptionAndSigning.CryptoSettings CryptoSettings, FileReference CryptoKeysCacheFilename, bool bCompressed)
	{
		bool bShouldGeneratePatch = Params.IsGeneratingPatch && SC.StageTargetPlatform.GetPlatformPatchesWithDiffPak(Params, SC);

		if (bShouldGeneratePatch && !Params.HasBasedOnReleaseVersion)
		{
			Log("Generating patch required a based on release version flag");
		}

		string PostFix = "";
		string OutputFilename = PakName + "-" + SC.FinalCookPlatform;
		string OutputFilenameExtension = ".pak";
		if (bShouldGeneratePatch)
		{
			PostFix += "_P";
			int TargetPatchIndex = 0;
			string ExistingPatchSearchPath = SC.StageTargetPlatform.GetReleasePakFilePath(SC, Params, null);
			if (Directory.Exists(ExistingPatchSearchPath))
			{
				IEnumerable<string> PakFileSet = Directory.EnumerateFiles(ExistingPatchSearchPath, OutputFilename + "*" + PostFix + OutputFilenameExtension);
				foreach (string PakFilePath in PakFileSet)
				{
					string PakFileName = Path.GetFileName(PakFilePath);
					int StartIndex = OutputFilename.Length + 1;
					int LengthVar = PakFileName.Length - (OutputFilename.Length + 1 + PostFix.Length + OutputFilenameExtension.Length);
					if (LengthVar > 0)
					{
						string PakFileIndex = PakFileName.Substring(StartIndex, LengthVar);
						int ChunkIndex;
						if (int.TryParse(PakFileIndex, out ChunkIndex))
						{
							if (ChunkIndex > TargetPatchIndex)
							{
								TargetPatchIndex = ChunkIndex;
							}
						}
					}
				}
				if (Params.ShouldAddPatchLevel && PakFileSet.Count() > 0)
				{
					TargetPatchIndex++;
				}
			}
			OutputFilename = OutputFilename + "_" + TargetPatchIndex + PostFix;
		}
		else if (Params.HasIterateSharedCookedBuild)
		{
			// shared cooked builds will produce a patch
			// then be combined with the shared cooked build
			PostFix += "_S_P";
			OutputFilename = OutputFilename + PostFix;
		}

		StagedFileReference OutputRelativeLocation;
		if (Params.HasDLCName)
		{
			OutputRelativeLocation = StagedFileReference.Combine(SC.RelativeProjectRootForStage, Params.DLCFile.Directory.MakeRelativeTo(SC.ProjectRoot), "Content", "Paks", SC.FinalCookPlatform, Params.DLCFile.GetFileNameWithoutExtension() + ".pak");
		}
		else
		{
			OutputRelativeLocation = StagedFileReference.Combine(SC.RelativeProjectRootForStage, "Content", "Paks", OutputFilename + OutputFilenameExtension);
		}
		if (SC.StageTargetPlatform.DeployLowerCaseFilenames())
		{
			OutputRelativeLocation = OutputRelativeLocation.ToLowerInvariant();
		}
		OutputRelativeLocation = SC.StageTargetPlatform.Remap(OutputRelativeLocation);

		FileReference OutputLocation = FileReference.Combine(SC.RuntimeRootDir, OutputRelativeLocation.Name);
		// Add input file to control order of file within the pak
		DirectoryReference PakOrderFileLocationBase = DirectoryReference.Combine(SC.ProjectRoot, "Build", SC.FinalCookPlatform, "FileOpenOrder");

		FileReference PakOrderFileLocation = null;

		string[] OrderFileNames = new string[] { "GameOpenOrder.log", "CookerOpenOrder.log", "EditorOpenOrder.log" };
		foreach (string OrderFileName in OrderFileNames)
		{
			PakOrderFileLocation = FileReference.Combine(PakOrderFileLocationBase, OrderFileName);

			if (FileExists_NoExceptions(PakOrderFileLocation.FullName))
			{
				break;
			}
		}

		bool bCopiedExistingPak = false;

		if (SC.StageTargetPlatform != SC.CookSourcePlatform)
		{
			// Check to see if we have an existing pak file we can use

			StagedFileReference SourceOutputRelativeLocation = StagedFileReference.Combine(SC.RelativeProjectRootForStage, "Content/Paks/", PakName + "-" + SC.CookPlatform + PostFix + ".pak");
			if (SC.CookSourcePlatform.DeployLowerCaseFilenames())
			{
				SourceOutputRelativeLocation = SourceOutputRelativeLocation.ToLowerInvariant();
			}
			SourceOutputRelativeLocation = SC.CookSourcePlatform.Remap(SourceOutputRelativeLocation);

			FileReference SourceOutputLocation = FileReference.Combine(SC.CookSourceRuntimeRootDir, SourceOutputRelativeLocation.Name);
			if (FileExists_NoExceptions(SourceOutputLocation.FullName))
			{
				InternalUtils.SafeCreateDirectory(Path.GetDirectoryName(OutputLocation.FullName), true);

				if (InternalUtils.SafeCopyFile(SourceOutputLocation.FullName, OutputLocation.FullName))
				{
					Log("Copying source pak from {0} to {1} instead of creating new pak", SourceOutputLocation, OutputLocation);
					bCopiedExistingPak = true;

					FileReference InSigFile = SourceOutputLocation.ChangeExtension(".sig");
					if (FileReference.Exists(InSigFile))
					{
						FileReference OutSigFile = OutputLocation.ChangeExtension(".sig");

						Log("Copying pak sig from {0} to {1}", InSigFile, OutSigFile);

						if (!InternalUtils.SafeCopyFile(InSigFile.FullName, OutSigFile.FullName))
						{
							Log("Failed to copy pak sig {0} to {1}, creating new pak", InSigFile, InSigFile);
							bCopiedExistingPak = false;
						}
					}

				}
			}
			if (!bCopiedExistingPak)
			{
				Log("Failed to copy source pak from {0} to {1}, creating new pak", SourceOutputLocation, OutputLocation);
			}
		}

		string PatchSourceContentPath = null;
		if (bShouldGeneratePatch)
		{
			// don't include the post fix in this filename because we are looking for the source pak path
			string PakFilename = PakName + "-" + SC.FinalCookPlatform + "*.pak";
			PatchSourceContentPath = SC.StageTargetPlatform.GetReleasePakFilePath(SC, Params, PakFilename);
		}
		
		if (!bCopiedExistingPak)
		{
			if (FileReference.Exists(OutputLocation))
			{
				string UnrealPakResponseFileName = CombinePaths(CmdEnv.LogFolder, "PakList_" + OutputLocation.GetFileNameWithoutExtension() + ".txt");
				if (File.Exists(UnrealPakResponseFileName) && FileReference.GetLastWriteTimeUtc(OutputLocation) > File.GetLastWriteTimeUtc(UnrealPakResponseFileName))
				{
					bCopiedExistingPak = true;
				}
			}
			if (!bCopiedExistingPak)
			{
				RunUnrealPak(UnrealPakResponseFile, OutputLocation, PakOrderFileLocation, SC.StageTargetPlatform.GetPlatformPakCommandLine(Params, SC) + " " + Params.AdditionalPakOptions, bCompressed, CryptoSettings, CryptoKeysCacheFilename, PatchSourceContentPath);
			}
		}

		if (Params.HasCreateReleaseVersion)
		{
			// copy the created pak to the release version directory we might need this later if we want to generate patches
			//string ReleaseVersionPath = CombinePaths( SC.ProjectRoot, "Releases", Params.CreateReleaseVersion, SC.StageTargetPlatform.GetCookPlatform(Params.DedicatedServer, false), Path.GetFileName(OutputLocation) );
			string ReleaseVersionPath = SC.StageTargetPlatform.GetReleasePakFilePath(SC, Params, OutputLocation.GetFileName());

			InternalUtils.SafeCreateDirectory(Path.GetDirectoryName(ReleaseVersionPath));
			InternalUtils.SafeCopyFile(OutputLocation.FullName, ReleaseVersionPath);
		}

		if (Params.CreateChunkInstall)
		{
			var RegEx = new Regex("pakchunk(\\d+)", RegexOptions.IgnoreCase | RegexOptions.Compiled);
			var Matches = RegEx.Matches(PakName);

			int ChunkID = 0;
			if (Matches.Count != 0 && Matches[0].Groups.Count > 1)
			{
				ChunkID = Convert.ToInt32(Matches[0].Groups[1].ToString());
			}
			else if (Params.HasDLCName)
			{
				// Assuming DLC is a single pack file
				ChunkID = 1;
			}
			else
			{
				throw new AutomationException(String.Format("Failed Creating Chunk Install Data, Unable to parse chunk id from {0}", PakName));
			}

			if (ChunkID != 0)
			{
				var BPTExe = GetBuildPatchToolExecutable();
				EnsureBuildPatchToolExists();

				string VersionString = Params.ChunkInstallVersionString;
				string ChunkInstallBasePath = CombinePaths(Params.ChunkInstallDirectory, SC.FinalCookPlatform);
				string RawDataPath = CombinePaths(ChunkInstallBasePath, VersionString, PakName);
				string RawDataPakPath = CombinePaths(RawDataPath, PakName + "-" + SC.FinalCookPlatform + PostFix + ".pak");
				//copy the pak chunk to the raw data folder
				if (InternalUtils.SafeFileExists(RawDataPakPath, true))
				{
					InternalUtils.SafeDeleteFile(RawDataPakPath, true);
				}
				InternalUtils.SafeCreateDirectory(RawDataPath, true);
				InternalUtils.SafeCopyFile(OutputLocation.FullName, RawDataPakPath);
				InternalUtils.SafeDeleteFile(OutputLocation.FullName, true);

				if (Params.IsGeneratingPatch)
				{
					if (String.IsNullOrEmpty(PatchSourceContentPath))
					{
						throw new AutomationException(String.Format("Failed Creating Chunk Install Data. No source pak for patch pak {0} given", OutputLocation));
					}
					// If we are generating a patch, then we need to copy the original pak across
					// for distribution.
					string SourceRawDataPakPath = CombinePaths(RawDataPath, PakName + "-" + SC.FinalCookPlatform + ".pak");
					InternalUtils.SafeCopyFile(PatchSourceContentPath, SourceRawDataPakPath);
				}

				string BuildRoot = MakePathSafeToUseWithCommandLine(RawDataPath);
				string CloudDir = MakePathSafeToUseWithCommandLine(CombinePaths(ChunkInstallBasePath, "CloudDir"));
				string ManifestDir = CombinePaths(ChunkInstallBasePath, "ManifestDir");
				var AppID = 1; // For a chunk install this value doesn't seem to matter
				string AppName = String.Format("{0}_{1}", SC.ShortProjectName, PakName);
				string AppLaunch = ""; // For a chunk install this value doesn't seem to matter
				string ManifestFilename = AppName + VersionString + ".manifest";
				string SourceManifestPath = CombinePaths(CloudDir, ManifestFilename);
				string DestManifestPath = CombinePaths(ManifestDir, ManifestFilename);
				InternalUtils.SafeCreateDirectory(ManifestDir, true);

				string CmdLine = String.Format("-BuildRoot=\"{0}\" -CloudDir=\"{1}\" -AppID={2} -AppName=\"{3}\" -BuildVersion=\"{4}\" -AppLaunch=\"{5}\"", BuildRoot, CloudDir, AppID, AppName, VersionString, AppLaunch);
				CmdLine += " -AppArgs=\"\"";
				CmdLine += " -custom=\"bIsPatch=false\"";
				CmdLine += String.Format(" -customint=\"ChunkID={0}\"", ChunkID);
				CmdLine += " -customint=\"PakReadOrdering=0\"";
				CmdLine += " -stdout";

				string UnrealPakLogFileName = "UnrealPak_" + PakName;
				RunAndLog(CmdEnv, BPTExe, CmdLine, UnrealPakLogFileName, Options: ERunOptions.Default | ERunOptions.UTF8Output);

				InternalUtils.SafeCopyFile(SourceManifestPath, DestManifestPath);
			}
			else
			{
				// add the first pak file as needing deployment and convert to lower case again if needed
				SC.FilesToStage.UFSFiles.Add(OutputRelativeLocation, OutputLocation);
			}
		}
		else
		{
			// add the pak file as needing deployment and convert to lower case again if needed
			SC.FilesToStage.UFSFiles.Add(OutputRelativeLocation, OutputLocation);

			// add the base pak files to deployment as well
			if (bShouldGeneratePatch)
			{
				string ExistingPatchSearchPath = SC.StageTargetPlatform.GetReleasePakFilePath(SC, Params, null);
				if (Directory.Exists(ExistingPatchSearchPath))
				{
					IEnumerable<string> PakFileSet = Directory.EnumerateFiles(ExistingPatchSearchPath, PakName + "-" + SC.FinalCookPlatform + "*" + OutputFilenameExtension);
					foreach (string PakFilePath in PakFileSet)
					{
						FileReference OutputDestinationPath = FileReference.Combine(OutputLocation.Directory, Path.GetFileName(PakFilePath));
						if (!File.Exists(OutputDestinationPath.FullName))
						{
							InternalUtils.SafeCopyFile(PakFilePath, OutputDestinationPath.FullName);
							StagedFileReference OutputDestinationRelativeLocation = StagedFileReference.Combine(SC.RelativeProjectRootForStage, "Content/Paks/", Path.GetFileName(PakFilePath));
							if (SC.StageTargetPlatform.DeployLowerCaseFilenames())
							{
								OutputDestinationRelativeLocation = OutputDestinationRelativeLocation.ToLowerInvariant();
							}
							OutputDestinationRelativeLocation = SC.StageTargetPlatform.Remap(OutputDestinationRelativeLocation);
							SC.FilesToStage.UFSFiles.Add(OutputDestinationRelativeLocation, OutputDestinationPath);
						}
					}
				}
			}
		}
	}

    /// <summary>
    /// Creates pak files using streaming install chunk manifests.
    /// </summary>
    /// <param name="Params"></param>
    /// <param name="SC"></param>
    private static void CopyPaksFromNetwork(ProjectParams Params, DeploymentContext SC)
    {
        Log("Copying paks from network.");

        if (!CommandUtils.P4Enabled)
        {
            throw new AutomationException("-PrePak requires -P4");
        }
        if (CommandUtils.P4Env.Changelist < 1000)
        {
            throw new AutomationException("-PrePak requires a CL from P4 and we have {0}", CommandUtils.P4Env.Changelist);
        }

        string BuildRoot = CombinePaths(CommandUtils.RootBuildStorageDirectory());
        string CachePath = InternalUtils.GetEnvironmentVariable("UE-BuildCachePath", "");

        string SrcBuildPath = CombinePaths(BuildRoot, Params.ShortProjectName);
        string SrcBuildPath2 = CombinePaths(BuildRoot, Params.ShortProjectName.Replace("Game", "").Replace("game", ""));

        string SrcBuildPath_Cache = CombinePaths(CachePath, Params.ShortProjectName);
        string SrcBuildPath2_Cache = CombinePaths(CachePath, Params.ShortProjectName.Replace("Game", "").Replace("game", ""));

        if (!InternalUtils.SafeDirectoryExists(SrcBuildPath))
        {
            if (!InternalUtils.SafeDirectoryExists(SrcBuildPath2))
            {
                throw new AutomationException("PrePak: Neither {0} nor {1} exists.", SrcBuildPath, SrcBuildPath2);
            }
            SrcBuildPath = SrcBuildPath2;
            SrcBuildPath_Cache = SrcBuildPath2_Cache;
        }
        string SrcCLPath = CombinePaths(SrcBuildPath, CommandUtils.EscapePath(CommandUtils.P4Env.Branch) + "-CL-" + CommandUtils.P4Env.Changelist.ToString());
        string SrcCLPath_Cache = CombinePaths(SrcBuildPath_Cache, CommandUtils.EscapePath(CommandUtils.P4Env.Branch) + "-CL-" + CommandUtils.P4Env.Changelist.ToString());
        if (!InternalUtils.SafeDirectoryExists(SrcCLPath))
        {
            throw new AutomationException("PrePak: {0} does not exist.", SrcCLPath);
        }
        string PlatformPath = CombinePaths(SrcCLPath, SC.FinalCookPlatform);
        string PlatformPath_Cache = CombinePaths(SrcCLPath_Cache, SC.FinalCookPlatform);
        if (!InternalUtils.SafeDirectoryExists(PlatformPath))
        {
            throw new AutomationException("PrePak: {0} does not exist.", PlatformPath);
        }
        string PakPath = CombinePaths(PlatformPath, "Staged", SC.ShortProjectName, "Content", "Paks");
        string PakPath_Cache = CombinePaths(PlatformPath_Cache, "Staged", SC.ShortProjectName, "Content", "Paks");
        if (!InternalUtils.SafeDirectoryExists(PakPath))
        {
            throw new AutomationException("PrePak: {0} does not exist.", PakPath);
        }

        string DestPath = CombinePaths(SC.StageDirectory.FullName, SC.ShortProjectName, "Content", "Paks");

        {
            var PakFiles = CommandUtils.FindFiles("*.pak", false, PakPath);
            var SigFiles = CommandUtils.FindFiles("*.sig", false, PakPath);

            var Files = new List<string>();

            Files.AddRange(PakFiles);
            Files.AddRange(SigFiles);

            if (Files.Count < 1)
            {
                throw new AutomationException("PrePak: {0} exists but does not have any paks in it.", PakPath);
            }


            foreach (var SrcFile in Files)
            {
                string DestFileName = CombinePaths(DestPath, new FileReference(SrcFile).GetFileName());
                if (!string.IsNullOrEmpty(CachePath))
                {
                    string CacheSrcFile = CombinePaths(PakPath_Cache, new FileReference(SrcFile).GetFileName());
                    try
                    {
                        if (InternalUtils.SafeFileExists(CacheSrcFile))
                        {
                            var Info = new System.IO.FileInfo(SrcFile);
                            var InfoCache = new System.IO.FileInfo(CacheSrcFile);
                            if (Info.Exists && InfoCache.Exists && Info.Length == InfoCache.Length)
                            {
                                Log("Copying from cache {0} -> {1}", CacheSrcFile, DestFileName);
                                CopyFileIncremental(new FileReference(CacheSrcFile), new FileReference(DestFileName));
                                continue;
                            }
                        }
                    }
                    catch (Exception)
                    {

                    }
                }
                Log("Copying {0} -> {1}", SrcFile, DestFileName);
                CopyFileIncremental(new FileReference(SrcFile), new FileReference(DestFileName));
            }
        }
    }

	private class ChunkDefinition
	{
		public ChunkDefinition(string InChunkName)
		{
			ChunkName = InChunkName;
			ResponseFile = new Dictionary<string, string>();
			Manifest = null;
			bCompressed = false;
		}

		public string ChunkName;
		public Dictionary<string, string> ResponseFile;
		public HashSet<string> Manifest;
		public bool bCompressed;
	}

	/// <summary>
	/// Creates pak files using streaming install chunk manifests.
	/// </summary>
	/// <param name="Params"></param>
	/// <param name="SC"></param>
	private static void CreatePaksUsingChunkManifests(ProjectParams Params, DeploymentContext SC)
	{
		Log("Creating pak using streaming install manifests.");
		DumpManifest(SC, CombinePaths(CmdEnv.LogFolder, "PrePak" + (SC.DedicatedServer ? "_Server" : "")));


		List<ChunkDefinition> ChunkDefinitions = new List<ChunkDefinition>();

		var TmpPackagingPath = GetTmpPackagingPath(Params, SC);


		// We still want to have a list of all files to stage. We will use the chunk manifests
		// to put the files from staging manifest into the right chunk
		var StagingManifestResponseFile = CreatePakResponseFileFromStagingManifest(SC, SC.FilesToStage.UFSFiles);

		{
			// DefaultChunkIndex assumes 0 is the 'base' chunk
			const int DefaultChunkIndex = 0;

			var ChunkListFilename = GetChunkPakManifestListFilename(Params, SC);
			List<string> ChunkList = new List<string>(ReadAllLines(ChunkListFilename));
			
			for (int Index = 0; Index < ChunkList.Count; ++Index)
			{
				ChunkDefinition CD = new ChunkDefinition(ChunkList[Index]);
				string[] ChunkOptions = ChunkList[Index].Split(' ');
				var ChunkManifestFilename = CombinePaths(TmpPackagingPath, ChunkOptions[0]);
				for ( int I = 1; I < ChunkOptions.Length; ++I )
				{
					if ( string.Compare(ChunkOptions[I], "compressed", true) == 0)
					{
						CD.bCompressed = true;
					}
				}
				CD.Manifest = ReadPakChunkManifest(ChunkManifestFilename);
				ChunkDefinitions.Add(CD);
			}

			const string OptionalBulkDataFileExtension = ".uptnl";
			Dictionary<string, ChunkDefinition> OptionalChunks = new Dictionary<string, ChunkDefinition>();
			
			
			ChunkDefinition DefaultChunk = ChunkDefinitions[DefaultChunkIndex];

			foreach (var StagingFile in StagingManifestResponseFile)
			{
				bool bAddedToChunk = false;
				for (int ChunkIndex = 0; !bAddedToChunk && ChunkIndex < ChunkDefinitions.Count; ++ChunkIndex)
				{
					ChunkDefinition Chunk = ChunkDefinitions[ChunkIndex];
					string OriginalFilename = StagingFile.Key;
					string NoExtension = CombinePaths(Path.GetDirectoryName(OriginalFilename), Path.GetFileNameWithoutExtension(OriginalFilename));
					string OriginalReplaceSlashes = OriginalFilename.Replace('/', '\\');
					string NoExtensionReplaceSlashes = NoExtension.Replace('/', '\\');

					if (Chunk.Manifest.Contains(OriginalFilename) ||
								Chunk.Manifest.Contains(OriginalReplaceSlashes) ||
								Chunk.Manifest.Contains(NoExtension) ||
								Chunk.Manifest.Contains(NoExtensionReplaceSlashes))
					{
						string OrigExt = Path.GetExtension(OriginalFilename);
						if (OrigExt.Equals(OptionalBulkDataFileExtension))
						{
							// any optional files encountered we want to put in a separate pak file
							string OptionalChunkName = Path.GetFileNameWithoutExtension(Chunk.ChunkName) + "optional.txt";
							ChunkDefinition OptionalChunk = null;
							if ( !OptionalChunks.TryGetValue(OptionalChunkName, out OptionalChunk) )
							{
								OptionalChunk = new ChunkDefinition(OptionalChunkName);
								OptionalChunks.Add(OptionalChunkName, OptionalChunk);
							}
							OptionalChunk.ResponseFile.Add(StagingFile.Key, StagingFile.Value);
						}
						else
						{
							Chunk.ResponseFile.Add(StagingFile.Key, StagingFile.Value);
						}
						bAddedToChunk = true;
					}
				}
				if (!bAddedToChunk)
				{
					//Log("No chunk assigned found for {0}. Using default chunk.", StagingFile.Key);
					DefaultChunk.ResponseFile.Add(StagingFile.Key, StagingFile.Value);
				}
			}

			foreach ( var OptionalChunkIt in OptionalChunks )
			{
				ChunkDefinitions.Add(OptionalChunkIt.Value);
			}
		}

		ConfigHierarchy PlatformGameConfig = ConfigCache.ReadHierarchy(ConfigHierarchyType.Game, DirectoryReference.FromFile(Params.RawProjectPath), SC.StageTargetPlatform.IniPlatformType);
		bool bShouldGenerateEarlyDownloaderPakFile = false;
		const string EarlyChunkFilename = "pakChunkEarly.txt";
		PlatformGameConfig.GetBool("/Script/UnrealEd.ProjectPackagingSettings", "GenerateEarlyDownloaderPakFile", out bShouldGenerateEarlyDownloaderPakFile);


		// chunk downloader pak file is a minimal pak file which contains no content.  It can be provided with as a minimal download so that the game can download all the content from another source.
		if ( bShouldGenerateEarlyDownloaderPakFile)
		{
			ChunkDefinition EarlyChunk = new ChunkDefinition(EarlyChunkFilename);

			EarlyChunk.bCompressed = true;

			Dictionary<string,string> EarlyPakFile = EarlyChunk.ResponseFile;

			// find the list of files to put in the early downloader pak file
			List<string> FilesInEarlyPakFile = new List<string>();
			PlatformGameConfig.GetArray("/Script/UnrealEd.ProjectPackagingSettings", "EarlyDownloaderPakFileFiles", out FilesInEarlyPakFile);

			/*for (int Index = 0; Index < FilesInEarlyPakFile.Count; ++Index)
			{
				// config file reader adds extra slashes...
				FilesInEarlyPakFile[Index] = FilesInEarlyPakFile[Index].Replace("\\\\", "\\");
			}*/

			FileFilter EarlyPakFileFilter = new FileFilter();
			foreach (string FileFilter in FilesInEarlyPakFile)
			{
				EarlyPakFileFilter.AddRule(FileFilter);
			}

			List<string> FilesToFilter = new List<string>();
			foreach (var ResponseFile in StagingManifestResponseFile)
			{
				FilesToFilter.Add(ResponseFile.Key);
			}

			foreach ( string FilteredFile in EarlyPakFileFilter.ApplyTo(FilesToFilter) )
			{
				EarlyPakFile.Add(FilteredFile, StagingManifestResponseFile[FilteredFile]);
			}

			ChunkDefinitions.Add(EarlyChunk);
		}




		if (Params.CreateChunkInstall)
		{
			string ManifestDir = CombinePaths(Params.ChunkInstallDirectory, SC.FinalCookPlatform, "ManifestDir");
			if (InternalUtils.SafeDirectoryExists(ManifestDir))
			{
				foreach (string ManifestFile in Directory.GetFiles(ManifestDir, "*.manifest"))
				{
					InternalUtils.SafeDeleteFile(ManifestFile, true);
				}
			}
			string DestDir = CombinePaths(Params.ChunkInstallDirectory, SC.FinalCookPlatform, Params.ChunkInstallVersionString);
			if (InternalUtils.SafeDirectoryExists(DestDir))
			{
				InternalUtils.SafeDeleteDirectory(DestDir);
			}
		}

        // Parse and cache crypto settings from INI file
        EncryptionAndSigning.CryptoSettings PakCryptoSettings = EncryptionAndSigning.ParseCryptoSettings(DirectoryReference.FromFile(Params.RawProjectPath), SC.StageTargetPlatform.IniPlatformType);
		FileReference CryptoKeysCacheFilename = FileReference.Combine(SC.MetadataDir, "Crypto.json");
		PakCryptoSettings.Save(CryptoKeysCacheFilename);

		
        System.Threading.Tasks.ParallelOptions Options = new System.Threading.Tasks.ParallelOptions();

        Log("Creating Pak files utilizing {0} cores", Environment.ProcessorCount);
        Options.MaxDegreeOfParallelism = Environment.ProcessorCount;        

        System.Threading.Tasks.Parallel.ForEach(ChunkDefinitions, Options, (Chunk) =>
		{
			bool bCompression = false;
			bCompression |= Params.Compressed;
			bCompression |= Chunk.bCompressed;

			var ChunkName = Path.GetFileNameWithoutExtension(Chunk.ChunkName);
            if (Chunk.ResponseFile.Count > 0)
            {
			    CreatePak(Params, SC, Chunk.ResponseFile, ChunkName, PakCryptoSettings, CryptoKeysCacheFilename, bCompression);
            }
		});

		if (Params.CreateChunkInstall)
		{
			// generate the master manifest
			string ChunkInstallBasePath = CombinePaths(Params.ChunkInstallDirectory, SC.FinalCookPlatform);
			string CloudDir = MakePathSafeToUseWithCommandLine(CombinePaths(ChunkInstallBasePath, "CloudDir"));
			GenerateMasterChunkManifest(CloudDir, Params.ChunkInstallVersionString, SC.FinalCookPlatform);
		}

		String ChunkLayerFilename = CombinePaths(GetTmpPackagingPath(Params, SC), GetChunkPakLayerListName());
		String OutputChunkLayerFilename = Path.Combine(SC.ProjectRoot.FullName, "Build", SC.FinalCookPlatform, "ChunkLayerInfo", GetChunkPakLayerListName());
		Directory.CreateDirectory(Path.GetDirectoryName(OutputChunkLayerFilename));
		File.Copy(ChunkLayerFilename, OutputChunkLayerFilename, true);


	}

	private static void GenerateMasterChunkManifest(string Dir, string Version, string PlatformStr)
	{
		//Create the directory if it doesn't exist
		InternalUtils.SafeCreateDirectory(Dir);

		string FileName = CombinePaths(Dir, PlatformStr.ToLower() + ".manifest");
		using (JsonWriter Writer = new JsonWriter(FileName))
		{
			Writer.WriteObjectStart();
			Writer.WriteValue("ClientVersion", Version);
			Writer.WriteValue("BuildUrl", Version + "/" + PlatformStr);
			Writer.WriteArrayStart("files");
			// iterate over all of the files in the directory
			DirectoryInfo di = new DirectoryInfo(Dir);
			foreach (var fi in di.EnumerateFiles("*.manifest"))
			{
				if (fi.Name == PlatformStr.ToLower() + ".manifest")
					continue;
				FileStream fs = fi.OpenRead();
				byte[] hash = System.Security.Cryptography.SHA1.Create().ComputeHash(fs);
				fs.Seek(0, SeekOrigin.Begin);
				byte[] hash256 = System.Security.Cryptography.SHA256.Create().ComputeHash(fs);
				fs.Close();
				Writer.WriteObjectStart();
				Writer.WriteValue("filename", fi.Name);
				Writer.WriteValue("uniqueFilename", fi.Name);
				Writer.WriteValue("length", fi.Length);
				Writer.WriteValue("URL", fi.Name);
				Writer.WriteValue("hash", BitConverter.ToString(hash).Replace("-", ""));
				Writer.WriteValue("hash256", BitConverter.ToString(hash256).Replace("-", ""));
				Writer.WriteObjectEnd();
			}
			Writer.WriteArrayEnd();
			Writer.WriteObjectEnd();
		}
	}

	private static bool DoesChunkPakManifestExist(ProjectParams Params, DeploymentContext SC)
	{
		return FileExists_NoExceptions(GetChunkPakManifestListFilename(Params, SC));
	}

	private static string GetChunkPakManifestListFilename(ProjectParams Params, DeploymentContext SC)
	{
		return CombinePaths(GetTmpPackagingPath(Params, SC), "pakchunklist.txt");
	}

	private static string GetChunkPakLayerListName()
	{
		return "pakchunklayers.txt";
	}

	private static string GetTmpPackagingPath(ProjectParams Params, DeploymentContext SC)
	{
		string TmpPackagingPath = CombinePaths(Path.GetDirectoryName(Params.RawProjectPath.FullName), "Saved", "TmpPackaging", SC.FinalCookPlatform);
		if(Params.bUseExtraFlavor)
		{
			TmpPackagingPath = CombinePaths(TmpPackagingPath, "ExtraFlavor");
		}

		return TmpPackagingPath;
	}

	private static bool ShouldCreatePak(ProjectParams Params, DeploymentContext SC)
	{
		if (Params.CookOnTheFly)
		{
			return false;
		}

		Platform.PakType Pak = SC.StageTargetPlatform.RequiresPak(Params);

		// we may care but we don't want. 
		if (Params.SkipPak)
			return false;

		if (Pak == Platform.PakType.Always)
		{
			return true;
		}
		else if (Pak == Platform.PakType.Never)
		{
			return false;
		}
		else // DontCare
		{
			return (Params.Pak);
		}
	}

	private static bool ShouldCreatePak(ProjectParams Params)
	{
		Platform.PakType Pak = Params.ClientTargetPlatformInstances[0].RequiresPak(Params);

		// we may care but we don't want. 
		if (Params.SkipPak)
			return false;

		if (Pak == Platform.PakType.Always)
		{
			return true;
		}
		else if (Pak == Platform.PakType.Never)
		{
			return false;
		}
		else // DontCare
		{
			return (Params.Pak);
		}
	}

	protected static void DeletePakFiles(string StagingDirectory)
	{
		var StagedFilesDir = new DirectoryInfo(StagingDirectory);
		StagedFilesDir.GetFiles("*.pak", SearchOption.AllDirectories).ToList().ForEach(File => File.Delete());
	}

	public static void CleanStagingDirectory(ProjectParams Params, DeploymentContext SC)
	{
		Log("Cleaning Stage Directory: {0}", SC.StageDirectory.FullName);
		if (SC.Stage && !Params.NoCleanStage && !Params.SkipStage && !Params.IterativeDeploy)
		{
			try
			{
				DeleteDirectory(SC.StageDirectory.FullName);
			}
			catch (Exception Ex)
			{
				// Delete cooked data (if any) as it may be incomplete / corrupted.
				throw new AutomationException(ExitCode.Error_FailedToDeleteStagingDirectory, Ex, "Stage Failed. Failed to delete staging directory " + SC.StageDirectory.FullName);
			}
		}
		else
		{
			try
			{
				// delete old pak files
				DeletePakFiles(SC.StageDirectory.FullName);
			}
			catch (Exception Ex)
			{
				// Delete cooked data (if any) as it may be incomplete / corrupted.
				throw new AutomationException(ExitCode.Error_FailedToDeleteStagingDirectory, Ex, "Stage Failed. Failed to delete pak files in " + SC.StageDirectory.FullName);
			}
		}
	}

	public static void ApplyStagingManifest(ProjectParams Params, DeploymentContext SC)
	{
		if (ShouldCreatePak(Params, SC))
		{
			if(SC.CrashReporterUFSFiles.Count > 0)
			{
				CreatePakForCrashReporter(Params, SC);
			}

			if (Params.PrePak)
			{
				CopyPaksFromNetwork(Params, SC);
			}
			else if (SC.PlatformUsesChunkManifests && DoesChunkPakManifestExist(Params, SC))
			{
				CreatePaksUsingChunkManifests(Params, SC);
			}
			else
			{
				CreatePakUsingStagingManifest(Params, SC);
			}
		}
		if (!SC.Stage || Params.SkipStage)
		{
			return;
		}
		DumpManifest(SC, CombinePaths(CmdEnv.LogFolder, "FinalCopy" + (SC.DedicatedServer ? "_Server" : ""))/*, !Params.UsePak(SC.StageTargetPlatform)*/);
		CopyUsingStagingManifest(Params, SC);

		var ThisPlatform = SC.StageTargetPlatform;
		ThisPlatform.PostStagingFileCopy(Params, SC);
	}

	private static DirectoryReference GetIntermediateCommandlineDir(DeploymentContext SC)
	{
		return DirectoryReference.Combine(SC.EngineRoot, "Intermediate", "UAT", SC.FinalCookPlatform);
	}

	public static void WriteStageCommandline(FileReference IntermediateCmdLineFile, ProjectParams Params, DeploymentContext SC)
	{
		// this file needs to be treated as a UFS file for casing, but NonUFS for being put into the .pak file. 
		// @todo: Maybe there should be a new category - UFSNotForPak
		if (SC.StageTargetPlatform.DeployLowerCaseFilenames())
		{
			IntermediateCmdLineFile = new FileReference(CombinePaths(Path.GetDirectoryName(IntermediateCmdLineFile.FullName), Path.GetFileName(IntermediateCmdLineFile.FullName).ToLowerInvariant()));
		}
		if (FileReference.Exists(IntermediateCmdLineFile))
		{
			FileReference.Delete(IntermediateCmdLineFile);
		}

		if (!SC.StageTargetPlatform.ShouldStageCommandLine(Params, SC))
		{
			return;
		}

		Log("Creating UE4CommandLine.txt");
		if (!string.IsNullOrEmpty(Params.StageCommandline) || !string.IsNullOrEmpty(Params.RunCommandline))
		{
			string FileHostParams = " ";
			if (Params.CookOnTheFly || Params.FileServer)
			{
				FileHostParams += "-filehostip=";
				bool FirstParam = true;
				if (UnrealBuildTool.BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Mac)
				{
					NetworkInterface[] Interfaces = NetworkInterface.GetAllNetworkInterfaces();
					foreach (NetworkInterface adapter in Interfaces)
					{
						if (adapter.NetworkInterfaceType != NetworkInterfaceType.Loopback)
						{
							IPInterfaceProperties IP = adapter.GetIPProperties();
							for (int Index = 0; Index < IP.UnicastAddresses.Count; ++Index)
							{
								if (IP.UnicastAddresses[Index].IsDnsEligible && IP.UnicastAddresses[Index].Address.AddressFamily == System.Net.Sockets.AddressFamily.InterNetwork)
								{
									if (!IsNullOrEmpty(Params.Port))
									{
										foreach (var Port in Params.Port)
										{
											if (!FirstParam)
											{
												FileHostParams += "+";
											}
											FirstParam = false;
											string[] PortProtocol = Port.Split(new char[] { ':' });
											if (PortProtocol.Length > 1)
											{
												FileHostParams += String.Format("{0}://{1}:{2}", PortProtocol[0], IP.UnicastAddresses[Index].Address.ToString(), PortProtocol[1]);
											}
											else
											{
												FileHostParams += IP.UnicastAddresses[Index].Address.ToString();
												FileHostParams += ":";
												FileHostParams += Params.Port;
											}

										}
									}
									else
									{
										if (!FirstParam)
										{
											FileHostParams += "+";
										}
										FirstParam = false;

										// use default port
										FileHostParams += IP.UnicastAddresses[Index].Address.ToString();
									}

								}
							}
						}
					}
				}
				else
				{
					NetworkInterface[] Interfaces = NetworkInterface.GetAllNetworkInterfaces();
					foreach (NetworkInterface adapter in Interfaces)
					{
						if (adapter.OperationalStatus == OperationalStatus.Up)
						{
							IPInterfaceProperties IP = adapter.GetIPProperties();
							for (int Index = 0; Index < IP.UnicastAddresses.Count; ++Index)
							{
								if (IP.UnicastAddresses[Index].IsDnsEligible)
								{
									if (!IsNullOrEmpty(Params.Port))
									{
										foreach (var Port in Params.Port)
										{
											if (!FirstParam)
											{
												FileHostParams += "+";
											}
											FirstParam = false;
											string[] PortProtocol = Port.Split(new char[] { ':' });
											if (PortProtocol.Length > 1)
											{
												FileHostParams += String.Format("{0}://{1}:{2}", PortProtocol[0], IP.UnicastAddresses[Index].Address.ToString(), PortProtocol[1]);
											}
											else
											{
												FileHostParams += IP.UnicastAddresses[Index].Address.ToString();
												FileHostParams += ":";
												FileHostParams += Params.Port;
											}
										}
									}
									else
									{
										if (!FirstParam)
										{
											FileHostParams += "+";
										}
										FirstParam = false;

										// use default port
										FileHostParams += IP.UnicastAddresses[Index].Address.ToString();
									}

								}
							}
						}
					}
				}
				const string LocalHost = "127.0.0.1";
				if (!IsNullOrEmpty(Params.Port))
				{
					foreach (var Port in Params.Port)
					{
						if (!FirstParam)
						{
							FileHostParams += "+";
						}
						FirstParam = false;
						string[] PortProtocol = Port.Split(new char[] { ':' });
						if (PortProtocol.Length > 1)
						{
							FileHostParams += String.Format("{0}://{1}:{2}", PortProtocol[0], LocalHost, PortProtocol[1]);
						}
						else
						{
							FileHostParams += LocalHost;
							FileHostParams += ":";
							FileHostParams += Params.Port;
						}

					}
				}
				else
				{
					if (!FirstParam)
					{
						FileHostParams += "+";
					}
					FirstParam = false;

					// use default port
					FileHostParams += LocalHost;
				}
				FileHostParams += " ";
			}

			String ProjectFile = String.Format("{0} ", SC.ProjectArgForCommandLines);
			if (SC.StageTargetPlatform.PlatformType == UnrealTargetPlatform.Mac || SC.StageTargetPlatform.PlatformType == UnrealTargetPlatform.Win64 || SC.StageTargetPlatform.PlatformType == UnrealTargetPlatform.Win32 || SC.StageTargetPlatform.PlatformType == UnrealTargetPlatform.Linux)
			{
				ProjectFile = "";
			}
			DirectoryReference.CreateDirectory(GetIntermediateCommandlineDir(SC));
			string CommandLine = String.Format("{0} {1} {2} {3}\n", ProjectFile, Params.StageCommandline.Trim(new char[] { '\"' }), Params.RunCommandline.Trim(new char[] { '\"' }), FileHostParams).Trim();
			if (Params.IterativeDeploy)
			{
				CommandLine += " -iterative";
			}
			File.WriteAllText(IntermediateCmdLineFile.FullName, CommandLine);
		}
		else
		{
			String ProjectFile = String.Format("{0} ", SC.ProjectArgForCommandLines);
			if (SC.StageTargetPlatform.PlatformType == UnrealTargetPlatform.Mac || SC.StageTargetPlatform.PlatformType == UnrealTargetPlatform.Win64 || SC.StageTargetPlatform.PlatformType == UnrealTargetPlatform.Win32 || SC.StageTargetPlatform.PlatformType == UnrealTargetPlatform.Linux)
			{
				ProjectFile = "";
			}
			DirectoryReference.CreateDirectory(GetIntermediateCommandlineDir(SC));
			File.WriteAllText(IntermediateCmdLineFile.FullName, ProjectFile);
		}
	}

	private static void WriteStageCommandline(ProjectParams Params, DeploymentContext SC)
	{
		// always delete the existing commandline text file, so it doesn't reuse an old one
		FileReference IntermediateCmdLineFile = FileReference.Combine(GetIntermediateCommandlineDir(SC), "UE4CommandLine.txt");
		WriteStageCommandline(IntermediateCmdLineFile, Params, SC);
	}

	private static Dictionary<StagedFileReference, string> ReadDeployedManifest(ProjectParams Params, DeploymentContext SC, List<string> ManifestList)
	{
		Dictionary<StagedFileReference, string> DeployedFiles = new Dictionary<StagedFileReference, string>();
		HashSet<StagedFileReference> CRCFiles = SC.StageTargetPlatform.GetFilesForCRCCheck();

		// read the manifest
		bool bContinueSearch = true;
		foreach (string Manifest in ManifestList)
		{
			int FilesAdded = 0;
			if (bContinueSearch)
			{
				string Data = File.ReadAllText(Manifest);
				string[] Lines = Data.Split('\n');
				foreach (string Line in Lines)
				{
					string[] Pair = Line.Split('\t');
					if (Pair.Length > 1)
					{
						StagedFileReference Filename = new StagedFileReference(Pair[0]);
						string TimeStamp = Pair[1];
						FilesAdded++;
						if (DeployedFiles.ContainsKey(Filename))
						{
							if ((CRCFiles.Contains(Filename) && DeployedFiles[Filename] != TimeStamp) || (!CRCFiles.Contains(Filename) && DateTime.Parse(DeployedFiles[Filename]) > DateTime.Parse(TimeStamp)))
							{
								DeployedFiles[Filename] = TimeStamp;
							}
						}
						else
						{
							DeployedFiles.Add(Filename, TimeStamp);
						}
					}
				}
			}
			File.Delete(Manifest);

			if (FilesAdded == 0 && bContinueSearch)
			{
				// no files have been deployed at all to this guy, so remove all previously added files and exit the loop as this means we need to deploy everything
				DeployedFiles.Clear();
				bContinueSearch = false;
			}
		}

		return DeployedFiles;
	}

	protected static Dictionary<StagedFileReference, string> ReadStagedManifest(ProjectParams Params, DeploymentContext SC, string Manifest)
	{
		Dictionary<StagedFileReference, string> StagedFiles = new Dictionary<StagedFileReference, string>();
		HashSet<StagedFileReference> CRCFiles = SC.StageTargetPlatform.GetFilesForCRCCheck();

		// get the staged manifest from staged directory
		FileReference ManifestFile = FileReference.Combine(SC.StageDirectory, Manifest);
		if (FileReference.Exists(ManifestFile))
		{
			string[] Lines = FileReference.ReadAllLines(ManifestFile);
			foreach (string Line in Lines)
			{
				string[] Pair = Line.Split('\t');
				if (Pair.Length > 1)
				{
					StagedFileReference Filename = new StagedFileReference(Pair[0]);
					string TimeStamp = Pair[1];
					if (!StagedFiles.ContainsKey(Filename))
					{
						StagedFiles.Add(Filename, TimeStamp);
					}
					else if ((CRCFiles.Contains(Filename) && StagedFiles[Filename] != TimeStamp) || (!CRCFiles.Contains(Filename) && DateTime.Parse(StagedFiles[Filename]) > DateTime.Parse(TimeStamp)))
					{
						StagedFiles[Filename] = TimeStamp;
					}
				}
			}
		}
		return StagedFiles;
	}

	protected static void WriteObsoleteManifest(ProjectParams Params, DeploymentContext SC, Dictionary<StagedFileReference, string> DeployedFiles, Dictionary<StagedFileReference, string> StagedFiles, string ObsoleteManifest)
	{
		List<StagedFileReference> ObsoleteFiles = new List<StagedFileReference>();

		// any file that has been deployed, but is no longer in the staged files is obsolete and should be deleted.
		foreach (KeyValuePair<StagedFileReference, string> File in DeployedFiles)
		{
			if (!StagedFiles.ContainsKey(File.Key))
			{
				ObsoleteFiles.Add(File.Key);
			}
		}

		// write out to the deltamanifest.json
		FileReference ManifestFile = FileReference.Combine(SC.StageDirectory, ObsoleteManifest);
		StreamWriter Writer = System.IO.File.CreateText(ManifestFile.FullName);
		foreach (StagedFileReference ObsoleteFile in ObsoleteFiles)
		{
			Writer.WriteLine(ObsoleteFile);
		}
		Writer.Close();
	}

	protected static void WriteDeltaManifest(ProjectParams Params, DeploymentContext SC, Dictionary<StagedFileReference, string> DeployedFiles, Dictionary<StagedFileReference, string> StagedFiles, string DeltaManifest)
	{
		HashSet<StagedFileReference> CRCFiles = SC.StageTargetPlatform.GetFilesForCRCCheck();
		List<string> DeltaFiles = new List<string>();
		foreach (KeyValuePair<StagedFileReference, string> StagedFile in StagedFiles)
		{
			bool bNeedsDeploy = true;
			if (DeployedFiles.ContainsKey(StagedFile.Key))
			{
				if (CRCFiles.Contains(StagedFile.Key))
				{
					bNeedsDeploy = (StagedFile.Value != DeployedFiles[StagedFile.Key]);
				}
				else
				{
					DateTime Staged = DateTime.Parse(StagedFile.Value);
					DateTime Deployed = DateTime.Parse(DeployedFiles[StagedFile.Key]);
					bNeedsDeploy = (Staged > Deployed);
				}
			}

			if (bNeedsDeploy)
			{
				DeltaFiles.Add(StagedFile.Key.Name);
			}
		}

		// add the manifest
		if (!DeltaManifest.Contains("NonUFS"))
		{
			DeltaFiles.Add(SC.NonUFSDeployedManifestFileName);
			DeltaFiles.Add(SC.UFSDeployedManifestFileName);
		}

		// TODO: determine files which need to be removed

		// write out to the deltamanifest.json
		FileReference ManifestFile = FileReference.Combine(SC.StageDirectory, DeltaManifest);
		FileReference.WriteAllLines(ManifestFile, DeltaFiles);
	}

	#endregion

	#region Stage Command

	//@todo move this
	public static List<DeploymentContext> CreateDeploymentContext(ProjectParams Params, bool InDedicatedServer, bool DoCleanStage = false)
	{
		ParamList<string> ListToProcess = InDedicatedServer && (Params.Cook || Params.CookOnTheFly) ? Params.ServerCookedTargets : Params.ClientCookedTargets;
		var ConfigsToProcess = InDedicatedServer && (Params.Cook || Params.CookOnTheFly) ? Params.ServerConfigsToBuild : Params.ClientConfigsToBuild;

		List<TargetPlatformDescriptor> PlatformsToStage = Params.ClientTargetPlatforms;
		if (InDedicatedServer && (Params.Cook || Params.CookOnTheFly))
		{
			PlatformsToStage = Params.ServerTargetPlatforms;
		}

		List<DeploymentContext> DeploymentContexts = new List<DeploymentContext>();
		foreach (var StagePlatform in PlatformsToStage)
		{
			// Get the platform to get cooked data from, may differ from the stage platform
			TargetPlatformDescriptor CookedDataPlatform = Params.GetCookedDataPlatformForClientTarget(StagePlatform);

			if (InDedicatedServer && (Params.Cook || Params.CookOnTheFly))
			{
				CookedDataPlatform = Params.GetCookedDataPlatformForServerTarget(StagePlatform);
			}

			List<string> ExecutablesToStage = new List<string>();

			string PlatformName = StagePlatform.ToString();
			string StageArchitecture = !String.IsNullOrEmpty(Params.SpecifiedArchitecture) ? Params.SpecifiedArchitecture : "";
			foreach (var Target in ListToProcess)
			{
				foreach (var Config in ConfigsToProcess)
				{
					string Exe = Target;
					if (Config != UnrealTargetConfiguration.Development)
					{
						Exe = Target + "-" + PlatformName + "-" + Config.ToString() + StageArchitecture;
					}
					ExecutablesToStage.Add(Exe);
				}
			}

			string StageDirectory = ((ShouldCreatePak(Params) || (Params.Stage)) || !String.IsNullOrEmpty(Params.StageDirectoryParam)) ? Params.BaseStageDirectory : "";
			string ArchiveDirectory = (Params.Archive || !String.IsNullOrEmpty(Params.ArchiveDirectoryParam)) ? Params.BaseArchiveDirectory : "";
			DirectoryReference EngineDir = DirectoryReference.Combine(CommandUtils.RootDirectory, "Engine");
			DirectoryReference ProjectDir = DirectoryReference.FromFile(Params.RawProjectPath);

			List<StageTarget> TargetsToStage = new List<StageTarget>();
			foreach (string Target in ListToProcess)
			{
				foreach (UnrealTargetConfiguration Config in ConfigsToProcess)
				{
					DirectoryReference ReceiptBaseDir = Params.IsCodeBasedProject ? ProjectDir : EngineDir;

					Platform PlatformInstance = Platform.Platforms[StagePlatform];
					UnrealTargetPlatform[] SubPlatformsToStage = PlatformInstance.GetStagePlatforms();

					// if we are attempting to gathering multiple platforms, the files aren't required
					bool bJustPackaging = Params.SkipStage && Params.Package;
					bool bIsIterativeSharedCooking = Params.HasIterateSharedCookedBuild;
					bool bRequireStagedFilesToExist = SubPlatformsToStage.Length == 1 && PlatformsToStage.Count == 1 && !bJustPackaging && !bIsIterativeSharedCooking;

					foreach (UnrealTargetPlatform ReceiptPlatform in SubPlatformsToStage)
					{
						string Architecture = Params.SpecifiedArchitecture;
						if (string.IsNullOrEmpty(Architecture))
						{
							Architecture = "";
							if (PlatformExports.IsPlatformAvailable(ReceiptPlatform))
							{
								Architecture = PlatformExports.GetDefaultArchitecture(ReceiptPlatform, Params.RawProjectPath);
							}
						}

						if (Params.IterateSharedBuildUsePrecompiledExe)
						{
							continue;
						}

						FileReference ReceiptFileName = TargetReceipt.GetDefaultPath(ReceiptBaseDir, Target, ReceiptPlatform, Config, Architecture);
						if (!FileReference.Exists(ReceiptFileName))
						{
							if (bRequireStagedFilesToExist)
							{
								// if we aren't collecting multiple platforms, then it is expected to exist
								continue;
//								throw new AutomationException(ExitCode.Error_MissingExecutable, "Stage Failed. Missing receipt '{0}'. Check that this target has been built.", ReceiptFileName);
							}
							else
							{
								// if it's multiple platforms, then allow missing receipts
								continue;
							}

						}

						// Read the receipt for this target
						TargetReceipt Receipt;
						if (!TargetReceipt.TryRead(ReceiptFileName, EngineDir, ProjectDir, out Receipt))
						{
							throw new AutomationException("Missing or invalid target receipt ({0})", ReceiptFileName);
						}

						// Convert the paths to absolute
						TargetsToStage.Add(new StageTarget { Receipt = Receipt, RequireFilesExist = bRequireStagedFilesToExist });
					}
				}
			}

			//@todo should pull StageExecutables from somewhere else if not cooked
			var SC = new DeploymentContext(Params.RawProjectPath, CommandUtils.RootDirectory,
				String.IsNullOrEmpty(StageDirectory) ? null : new DirectoryReference(StageDirectory),
				String.IsNullOrEmpty(ArchiveDirectory) ? null : new DirectoryReference(ArchiveDirectory),
				Platform.Platforms[CookedDataPlatform],
				Platform.Platforms[StagePlatform],
				ConfigsToProcess,
				TargetsToStage,
				ExecutablesToStage,
				InDedicatedServer,
				Params.Cook || Params.CookOnTheFly,
				Params.CrashReporter,
				Params.Stage,
				Params.CookOnTheFly,
				Params.Archive,
				Params.IsProgramTarget,
				Params.Client,
				Params.Manifests,
				Params.SeparateDebugInfo
				);
			LogDeploymentContext(SC);

			// If we're a derived platform make sure we're at the end, otherwise make sure we're at the front

			if (!CookedDataPlatform.Equals(StagePlatform))
			{
				DeploymentContexts.Add(SC);
			}
			else
			{
				DeploymentContexts.Insert(0, SC);
			}
		}

		return DeploymentContexts;
	}

	public static void CopyBuildToStagingDirectory(ProjectParams Params)
	{
		if (ShouldCreatePak(Params) || (Params.Stage && !Params.SkipStage))
		{
			Params.ValidateAndLog();

			Log("********** STAGE COMMAND STARTED **********");

			if (!Params.NoClient)
			{
				var DeployContextList = CreateDeploymentContext(Params, false, true);

				// clean the staging directories first
				foreach (var SC in DeployContextList)
				{
                    SC.StageTargetPlatform.PreStage(Params, SC);

					// write out the commandline file now so it can go into the manifest
					WriteStageCommandline(Params, SC);
					CreateStagingManifest(Params, SC);
					CleanStagingDirectory(Params, SC);
				}
				foreach (var SC in DeployContextList)
				{
					//ensure this directory exists so these writes work
					DirectoryReference.CreateDirectory(GetIntermediateCommandlineDir(SC));

					ApplyStagingManifest(Params, SC);

					if (Params.Deploy)
					{
						List<string> UFSManifests;
						List<string> NonUFSManifests;

						// get the staged file data
						Dictionary<StagedFileReference, string> StagedUFSFiles = ReadStagedManifest(Params, SC, SC.UFSDeployedManifestFileName);
						Dictionary<StagedFileReference, string> StagedNonUFSFiles = ReadStagedManifest(Params, SC, SC.NonUFSDeployedManifestFileName);

						foreach (var DeviceName in Params.DeviceNames)
						{
							// get the deployed file data
							Dictionary<StagedFileReference, string> DeployedUFSFiles = new Dictionary<StagedFileReference, string>();
							Dictionary<StagedFileReference, string> DeployedNonUFSFiles = new Dictionary<StagedFileReference, string>();

							if (SC.StageTargetPlatform.RetrieveDeployedManifests(Params, SC, DeviceName, out UFSManifests, out NonUFSManifests))
							{
								DeployedUFSFiles = ReadDeployedManifest(Params, SC, UFSManifests);
								DeployedNonUFSFiles = ReadDeployedManifest(Params, SC, NonUFSManifests);
							}

							WriteObsoleteManifest(Params, SC, DeployedUFSFiles, StagedUFSFiles, SC.GetUFSDeploymentObsoletePath(DeviceName));
							WriteObsoleteManifest(Params, SC, DeployedNonUFSFiles, StagedNonUFSFiles, SC.GetNonUFSDeploymentObsoletePath(DeviceName));

							if (Params.IterativeDeploy)
							{
								// write out the delta file data
								WriteDeltaManifest(Params, SC, DeployedUFSFiles, StagedUFSFiles, SC.GetUFSDeploymentDeltaPath(DeviceName));
								WriteDeltaManifest(Params, SC, DeployedNonUFSFiles, StagedNonUFSFiles, SC.GetNonUFSDeploymentDeltaPath(DeviceName));
							}
						}
					}

					if (Params.bCodeSign)
					{
						SC.StageTargetPlatform.SignExecutables(SC, Params);
					}
				}
			}

			if (Params.DedicatedServer)
			{
				var DeployContextList = CreateDeploymentContext(Params, true, true);
				foreach (var SC in DeployContextList)
				{
					CreateStagingManifest(Params, SC);
					CleanStagingDirectory(Params, SC);
				}

				foreach (var SC in DeployContextList)
				{
					ApplyStagingManifest(Params, SC);
				}
			}
			Log("********** STAGE COMMAND COMPLETED **********");
		}
	}

	#endregion
}
