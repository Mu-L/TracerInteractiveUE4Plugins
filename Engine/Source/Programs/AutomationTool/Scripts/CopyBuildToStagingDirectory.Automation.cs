// Copyright Epic Games, Inc. All Rights Reserved.

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
using System.Diagnostics;

/// <summary>
/// Helper command used for cooking.
/// </summary>
/// <remarks>
/// Command line parameters used by this command:
/// -clean
/// </remarks>
public partial class Project : CommandUtils
{

	private static readonly object SyncLock = new object();

	/// <returns>The path for the BuildPatchTool executable depending on host platform.</returns>
	private static string GetBuildPatchToolExecutable()
	{
		if (UnrealBuildTool.BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win32)
		{
			return CommandUtils.CombinePaths(CommandUtils.CmdEnv.LocalRoot, "Engine/Binaries/Win32/BuildPatchTool.exe");
		}
		if (UnrealBuildTool.BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64)
		{
			return CommandUtils.CombinePaths(CommandUtils.CmdEnv.LocalRoot, "Engine/Binaries/Win64/BuildPatchTool.exe");
		}
		if (UnrealBuildTool.BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Mac)
		{
			return CommandUtils.CombinePaths(CommandUtils.CmdEnv.LocalRoot, "Engine/Binaries/Mac/BuildPatchTool");
		}
		if (UnrealBuildTool.BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Linux)
		{
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
	private static void WritePakResponseFile(string Filename, Dictionary<string, string> ResponseFile, bool Compressed, EncryptionAndSigning.CryptoSettings CryptoSettings, bool bForceFullEncryption)
	{
		using (var Writer = new StreamWriter(Filename, false, new System.Text.UTF8Encoding(true)))
		{
			foreach (var Entry in ResponseFile)
			{
				string Line = String.Format("\"{0}\" \"{1}\"", Entry.Key, Entry.Value);
				if (Compressed && !Path.GetExtension(Entry.Key).Contains(".mp4") && !Path.GetExtension(Entry.Key).Contains("ushaderbytecode") && !Path.GetExtension(Entry.Key).Contains("upipelinecache"))
				{
					Line += " -compress";
				}

				if (CryptoSettings != null)
				{
					bool bEncryptFile = bForceFullEncryption || CryptoSettings.bEnablePakFullAssetEncryption;
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

	static public string GetUnrealPakArguments(FileReference ProjectPath, Dictionary<string, string> UnrealPakResponseFile, FileReference OutputLocation, FileReference PakOrderFileLocation, string PlatformOptions, bool Compressed, EncryptionAndSigning.CryptoSettings CryptoSettings, FileReference CryptoKeysCacheFilename, String PatchSourceContentPath, string EncryptionKeyGuid, FileReference SecondaryPakOrderFileLocation=null)
	{
		StringBuilder CmdLine = new StringBuilder(MakePathSafeToUseWithCommandLine(ProjectPath.FullName));
		CmdLine.AppendFormat(" {0}", MakePathSafeToUseWithCommandLine(OutputLocation.FullName));

		// Force encryption of ALL files if we're using specific encryption key. This should be made an option per encryption key in the settings, but for our initial
		// implementation we will just assume that we require maximum security for this data.
		bool bForceEncryption = !string.IsNullOrEmpty(EncryptionKeyGuid);
		string PakName = Path.GetFileNameWithoutExtension(OutputLocation.FullName);
		string UnrealPakResponseFileName = CombinePaths(CmdEnv.LogFolder, "PakList_" + PakName + ".txt");
		WritePakResponseFile(UnrealPakResponseFileName, UnrealPakResponseFile, Compressed, CryptoSettings, bForceEncryption);
		CmdLine.AppendFormat(" -create={0}", CommandUtils.MakePathSafeToUseWithCommandLine(UnrealPakResponseFileName));

		if (CryptoKeysCacheFilename != null)
		{
			CmdLine.AppendFormat(" -cryptokeys={0}", CommandUtils.MakePathSafeToUseWithCommandLine(CryptoKeysCacheFilename.FullName));
		}
		if (PakOrderFileLocation != null)
		{
			CmdLine.AppendFormat(" -order={0}", CommandUtils.MakePathSafeToUseWithCommandLine(PakOrderFileLocation.FullName));
		}
		if (SecondaryPakOrderFileLocation != null)
		{
			CmdLine.AppendFormat(" -secondaryOrder={0}", CommandUtils.MakePathSafeToUseWithCommandLine(SecondaryPakOrderFileLocation.FullName));
		}
		if (!String.IsNullOrEmpty(PatchSourceContentPath)) 
		{
			CmdLine.AppendFormat(" -generatepatch={0} -tempfiles={1}", CommandUtils.MakePathSafeToUseWithCommandLine(PatchSourceContentPath), CommandUtils.MakePathSafeToUseWithCommandLine(CommandUtils.CombinePaths(CommandUtils.CmdEnv.LocalRoot, "TempFiles" + Path.GetFileNameWithoutExtension(OutputLocation.FullName))));
		}
		if (CryptoSettings != null && CryptoSettings.bDataCryptoRequired)
		{
			if (CryptoSettings.bEnablePakIndexEncryption)
			{
				CmdLine.AppendFormat(" -encryptindex");
			}
			if (!string.IsNullOrEmpty(EncryptionKeyGuid))
			{
				CmdLine.AppendFormat(" -EncryptionKeyOverrideGuid={0}", EncryptionKeyGuid);
			}
			if (CryptoSettings.bDataCryptoRequired && CryptoSettings.bEnablePakSigning && CryptoSettings.SigningKey.IsValid())
			{
				CmdLine.AppendFormat(" -sign");
			}
		}
		
		CmdLine.Append(PlatformOptions);

		return CmdLine.ToString();
	}

	static private string GetIoStoreCommand(Dictionary<string, string> UnrealPakResponseFile, FileReference OutputLocation, FileReference PakOrderFileLocation, bool Compressed, EncryptionAndSigning.CryptoSettings CryptoSettings, string EncryptionKeyGuid, FileReference SecondaryPakOrderFileLocation=null)
	{
		StringBuilder CmdLine = new StringBuilder();
		CmdLine.AppendFormat("-Output={0}", MakePathSafeToUseWithCommandLine(Path.ChangeExtension(OutputLocation.FullName, null)));

		// Force encryption of ALL files if we're using specific encryption key. This should be made an option per encryption key in the settings, but for our initial
		// implementation we will just assume that we require maximum security for this data.
		bool bForceEncryption = !string.IsNullOrEmpty(EncryptionKeyGuid);
		string PakName = Path.GetFileNameWithoutExtension(OutputLocation.FullName);
		string UnrealPakResponseFileName = CombinePaths(CmdEnv.LogFolder, "PakListIoStore_" + PakName + ".txt");
		WritePakResponseFile(UnrealPakResponseFileName, UnrealPakResponseFile, Compressed, CryptoSettings, bForceEncryption);
		CmdLine.AppendFormat(" -ResponseFile={0}", CommandUtils.MakePathSafeToUseWithCommandLine(UnrealPakResponseFileName));

		return CmdLine.ToString();
	}

	public static FileReference GetUnrealPakLocation()
	{
		if (HostPlatform.Current.HostEditorPlatform == UnrealTargetPlatform.Win64)
		{
			return FileReference.Combine(CommandUtils.EngineDirectory, "Binaries", "Win64", "UnrealPak.exe");
		}
		else
		{
			return FileReference.Combine(CommandUtils.EngineDirectory, "Binaries", HostPlatform.Current.HostEditorPlatform.ToString(), "UnrealPak");
		}
	}

	static public void RunUnrealPak(ProjectParams Params, Dictionary<string, string> UnrealPakResponseFile, FileReference OutputLocation, FileReference PakOrderFileLocation, string PlatformOptions, bool Compressed, EncryptionAndSigning.CryptoSettings CryptoSettings, FileReference CryptoKeysCacheFilename, String PatchSourceContentPath, string EncryptionKeyGuid, FileReference SecondaryPakOrderFileLocation = null)
	{
		if (UnrealPakResponseFile.Count < 1)
		{
			return;
		}

		string Arguments = GetUnrealPakArguments(Params.RawProjectPath, UnrealPakResponseFile, OutputLocation, PakOrderFileLocation, PlatformOptions, Compressed, CryptoSettings, CryptoKeysCacheFilename, PatchSourceContentPath, EncryptionKeyGuid, SecondaryPakOrderFileLocation);
		RunAndLog(CmdEnv, GetUnrealPakLocation().FullName, Arguments, Options: ERunOptions.Default | ERunOptions.UTF8Output);
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

	private static string GetInternationalizationPreset(ProjectParams Params, ConfigHierarchy PlatformGameConfig, bool bMustExist = true)
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
		if (bMustExist && string.IsNullOrEmpty(InternationalizationPreset))
		{
			throw new AutomationException("No internationalization preset was specified for packaging. This will lead to fatal errors when launching. Specify preset via commandline (-I18NPreset=) or project packaging settings (InternationalizationPreset).");
		}

		return InternationalizationPreset;
	}

	private static List<string> GetCulturesToStage(ProjectParams Params, ConfigHierarchy PlatformGameConfig, bool bMustExist = true)
	{
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
		if (bMustExist && (CulturesToStage == null || CulturesToStage.Count == 0))
		{
			throw new AutomationException("No cultures were specified for cooking and packaging. This will lead to fatal errors when launching. Specify culture codes via commandline (-CookCultures=) or using project packaging settings (+CulturesToStage).");
		}

		return CulturesToStage;
	}

	private static bool ShouldStageLocalizationTarget(DeploymentContext SC, ConfigHierarchy PlatformGameConfig, string LocalizationTarget)
	{
		if (SC.BlacklistLocalizationTargets.Contains(LocalizationTarget))
		{
			return false;
		}

		// Chunked targets can be added to the manifest during cook, so skip them during staging otherwise we'll overwrite the chunk 0 data with the non-chunked version
		if (PlatformGameConfig != null)
		{
			List<string> LocalizationTargetsToChunk = null;
			PlatformGameConfig.GetArray("/Script/UnrealEd.ProjectPackagingSettings", "LocalizationTargetsToChunk", out LocalizationTargetsToChunk);

			if (LocalizationTargetsToChunk != null && LocalizationTargetsToChunk.Contains(LocalizationTarget))
			{
				var CookedLocalizationTargetDirectory = DirectoryReference.Combine(SC.PlatformCookDir, SC.ShortProjectName, "Content", "Localization", LocalizationTarget);
				return !DirectoryReference.Exists(CookedLocalizationTargetDirectory);
			}
		}

		return true;
	}

	private static void StageLocalizationDataForPlugin(DeploymentContext SC, List<string> CulturesToStage, FileReference Plugin)
	{
		PluginDescriptor Descriptor = PluginDescriptor.FromFile(Plugin);
		if (Descriptor.LocalizationTargets != null)
		{
			foreach (LocalizationTargetDescriptor LocalizationTarget in Descriptor.LocalizationTargets)
			{
				if (LocalizationTarget.LoadingPolicy == LocalizationTargetDescriptorLoadingPolicy.Always || LocalizationTarget.LoadingPolicy == LocalizationTargetDescriptorLoadingPolicy.Game)
				{
					DirectoryReference PluginLocTargetDirectory = DirectoryReference.Combine(Plugin.Directory, "Content", "Localization", LocalizationTarget.Name);
					if (ShouldStageLocalizationTarget(SC, null, LocalizationTarget.Name) && DirectoryReference.Exists(PluginLocTargetDirectory))
					{
						StageLocalizationDataForTarget(SC, CulturesToStage, PluginLocTargetDirectory);
					}
				}
			}
		}
	}

	private static void StageLocalizationDataForTarget(DeploymentContext SC, List<string> CulturesToStage, DirectoryReference SourceDirectory)
	{
		var PlatformSourceDirectory = new DirectoryReference(CombinePaths(SourceDirectory.FullName, "Platforms", ConfigHierarchy.GetIniPlatformName(SC.StageTargetPlatform.IniPlatformType)));
		if (!DirectoryReference.Exists(PlatformSourceDirectory))
		{
			PlatformSourceDirectory = null;
		}

		foreach (FileReference SourceFile in DirectoryReference.EnumerateFiles(SourceDirectory, "*.locmeta"))
		{
			SC.StageFile(StagedFileType.UFS, SourceFile);
		}

		foreach (string Culture in CulturesToStage)
		{
			StageLocalizationDataForCulture(SC, Culture, SourceDirectory);

			if (PlatformSourceDirectory != null)
			{
				StageLocalizationDataForCulture(SC, Culture, PlatformSourceDirectory);
			}
		}
	}

	private static void StageLocalizationDataForTargetsInDirectory(DeploymentContext SC, ConfigHierarchy PlatformGameConfig, List<string> CulturesToStage, DirectoryReference RootDirectory)
	{
		if (DirectoryReference.Exists(RootDirectory))
		{
			foreach (DirectoryReference LocTargetDirectory in DirectoryReference.EnumerateDirectories(RootDirectory))
			{
				if (ShouldStageLocalizationTarget(SC, PlatformGameConfig, LocTargetDirectory.MakeRelativeTo(RootDirectory)))
				{
					StageLocalizationDataForTarget(SC, CulturesToStage, LocTargetDirectory);
				}
			}
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
						Files.RemoveAll(x => x.HasExtension(".uasset") || x.HasExtension(".umap") || (SC.DedicatedServer && x.HasExtension(".mp4")));
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

	/// <summary>
	/// Figures out which plugins are enabled for a content-only plugins. This may include content-only plugins and editor plugins, which will not be in the default 
	/// target manifest, since it was compiled elsewhere.
	/// </summary>
	/// <param name="ProjectFile">The project being built</param>
	/// <param name="Targets">List of targets being staged</param>
	/// <returns>List of plugin files that should be staged</returns>
	private static List<FileReference> GetPluginsForContentProject(FileReference ProjectFile, List<TargetReceipt> Targets)
	{
		ProjectDescriptor Project = ProjectDescriptor.FromFile(ProjectFile);
		List<PluginInfo> AvailablePlugins = UnrealBuildTool.Plugins.ReadAvailablePlugins(CommandUtils.EngineDirectory, ProjectFile.Directory, null);

		HashSet<FileReference> Plugins = new HashSet<FileReference>();
		foreach (TargetReceipt Target in Targets)
		{
			// Find all the specifically enabled plugins for this target
			Dictionary<string, bool> EnabledPlugins = new Dictionary<string, bool>(StringComparer.OrdinalIgnoreCase);
			if (Project.Plugins != null)
			{
				foreach (PluginReferenceDescriptor Reference in Project.Plugins)
				{
					bool bEnabled = false;
					if (Reference.IsEnabledForPlatform(Target.Platform) && Reference.IsEnabledForTargetConfiguration(Target.Configuration) && Reference.IsEnabledForTarget(Target.TargetType))
					{
						bEnabled = true;
					}
					EnabledPlugins[Reference.Name] = bEnabled;
				}
			}

			// Add all the enabled-by-default plugins
			foreach (PluginInfo AvailablePlugin in AvailablePlugins)
			{
				if (!EnabledPlugins.ContainsKey(AvailablePlugin.Name))
				{
					EnabledPlugins[AvailablePlugin.Name] = AvailablePlugin.Descriptor.SupportsTargetPlatform(Target.Platform) && AvailablePlugin.IsEnabledByDefault(!Project.DisableEnginePluginsByDefault);
				}
			}

			// Add the enabled plugins
			foreach (PluginInfo AvailablePlugin in AvailablePlugins)
			{
				if (AvailablePlugin.Descriptor.SupportsTargetPlatform(Target.Platform) && EnabledPlugins[AvailablePlugin.Name])
				{
					Plugins.Add(AvailablePlugin.File);
				}
			}

			// Exclude all the plugins that would have been considered for the base target (editor only plugins that are enabled by default)
			foreach (PluginInfo AvailablePlugin in AvailablePlugins)
			{
				if (AvailablePlugin.LoadedFrom == PluginLoadedFrom.Engine && !AvailablePlugin.Descriptor.bInstalled && AvailablePlugin.IsEnabledByDefault(!Project.DisableEnginePluginsByDefault))
				{
					Plugins.Remove(AvailablePlugin.File);
				}
			}

			// Exclude all the existing plugin descriptors
			Plugins.ExceptWith(Target.RuntimeDependencies.Select(x => x.Path));
		}
		return Plugins.ToList();
	}

	public static void CreateStagingManifest(ProjectParams Params, DeploymentContext SC)
	{
		if (!Params.Stage)
		{
			return;
		}
		var ThisPlatform = SC.StageTargetPlatform;

		LogInformation("Creating Staging Manifest...");

		ConfigHierarchy PlatformGameConfig = ConfigCache.ReadHierarchy(ConfigHierarchyType.Game, DirectoryReference.FromFile(Params.RawProjectPath), SC.StageTargetPlatform.IniPlatformType);

		if (Params.HasIterateSharedCookedBuild)
		{
			// can't do shared cooked builds with DLC that's madness!!
			//check( Params.HasDLCName == false );

			// stage all the previously staged files
			SC.StageFiles(StagedFileType.NonUFS, DirectoryReference.Combine(SC.ProjectRoot, "Saved", "SharedIterativeBuild", SC.CookPlatform, "Staged"), StageFilesSearch.AllDirectories, StagedDirectoryReference.Root); // remap to the root directory
		}
		bool bCreatePluginManifest = false;
		if (Params.HasDLCName)
		{
			// Making a plugin
			DirectoryReference DLCRoot = Params.DLCFile.Directory;

			// Put all of the cooked dir into the staged dir
			SC.PlatformCookDir = String.IsNullOrEmpty(Params.CookOutputDir) ? DirectoryReference.Combine(DLCRoot, "Saved", "Cooked", SC.CookPlatform) : DirectoryReference.Combine(new DirectoryReference(Params.CookOutputDir), SC.CookPlatform);
			DirectoryReference PlatformEngineDir = DirectoryReference.Combine(SC.PlatformCookDir, "Engine");
			DirectoryReference ProjectMetadataDir = DirectoryReference.Combine(SC.PlatformCookDir, SC.ShortProjectName, "Metadata");
			string RelativeDLCRootPath = DLCRoot.MakeRelativeTo(SC.LocalRoot);
			SC.MetadataDir = DirectoryReference.Combine(SC.PlatformCookDir, RelativeDLCRootPath, "Metadata");

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

				// Stage DLC localization targets
				List<string> CulturesToStage = GetCulturesToStage(Params, PlatformGameConfig, false);
				if (CulturesToStage != null)
				{
					StageLocalizationDataForPlugin(SC, CulturesToStage, Params.DLCFile);
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
			List<FileReference> CookedFiles = DirectoryReference.EnumerateFiles(SC.PlatformCookDir, "*", SearchOption.AllDirectories).ToList();
			foreach (FileReference CookedFile in CookedFiles)
			{
				// Skip metadata directory
				if (CookedFile.Directory.IsUnderDirectory(SC.MetadataDir) || CookedFile.Directory.IsUnderDirectory(ProjectMetadataDir))
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
					SC.StageFile(StagedFileType.UFS, CookedFile, new StagedFileReference(CookedFile.MakeRelativeTo(SC.PlatformCookDir)));
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

			bCreatePluginManifest = true;
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

				// Stage any content-only plugins for content-only projects. We don't have a custom executable for these.
				if (!Params.IsCodeBasedProject)
				{
					List<FileReference> PluginFiles = GetPluginsForContentProject(Params.RawProjectPath, SC.StageTargets.ConvertAll(x => x.Receipt));
					SC.StageFiles(StagedFileType.UFS, PluginFiles);
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

			DirectoryReference ProjectContentRoot = DirectoryReference.Combine(SC.ProjectRoot, "Content");
			StagedDirectoryReference StageContentRoot = StagedDirectoryReference.Combine(SC.RelativeProjectRootForStage, "Content");

			if (!Params.CookOnTheFly && !Params.SkipCookOnTheFly) // only stage the UFS files if we are not using cook on the fly
			{
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

				SC.PlatformCookDir = CookOutputDir;
				SC.MetadataDir = DirectoryReference.Combine(CookOutputDir, SC.ShortProjectName, "Metadata");

				// Work out which ICU data version we use for this platform
				var ICUDataVersion = "icudt64l";
				if (SC.StageTargetPlatform.PlatformType == UnrealTargetPlatform.HoloLens || SC.StageTargetPlatform.PlatformType == UnrealTargetPlatform.TVOS)
				{
					ICUDataVersion = "icudt53l";
				}

				// Initialize internationalization preset.
				string InternationalizationPreset = GetInternationalizationPreset(Params, PlatformGameConfig);

				// Initialize cultures to stage.
				List<string> CulturesToStage = GetCulturesToStage(Params, PlatformGameConfig);

				// Stage ICU internationalization data from Engine.
				SC.StageFiles(StagedFileType.UFS, DirectoryReference.Combine(SC.LocalRoot, "Engine", "Content", "Internationalization", InternationalizationPreset, ICUDataVersion), StageFilesSearch.AllDirectories, new StagedDirectoryReference(String.Format("Engine/Content/Internationalization/{0}", ICUDataVersion)));

				// Engine ufs (content)
				StageConfigFiles(SC, DirectoryReference.Combine(SC.LocalRoot, "Engine", "Config"), null);

				// Stage the engine localization target
				if (ShouldStageLocalizationTarget(SC, null, "Engine"))
				{
					StageLocalizationDataForTarget(SC, CulturesToStage, DirectoryReference.Combine(SC.LocalRoot, "Engine", "Content", "Localization", "Engine"));
				}

				// Game ufs (content)
				SC.StageFile(StagedFileType.UFS, SC.RawProjectPath);

				StageConfigFiles(SC, DirectoryReference.Combine(SC.ProjectRoot, "Config"), null);
				
				// Stage platform extension config files
				{
					string PlatformExtensionName = SC.StageTargetPlatform.PlatformType.ToString();
					StagePlatformExtensionConfigFiles(SC, PlatformExtensionName);

					// and any ini parents it has
					DataDrivenPlatformInfo.ConfigDataDrivenPlatformInfo Info = DataDrivenPlatformInfo.GetDataDrivenInfoForPlatform(PlatformExtensionName);
					if (Info != null && Info.IniParentChain != null)
					{
						// the IniParentChain
						foreach (string ParentPlatform in Info.IniParentChain)
						{
							StagePlatformExtensionConfigFiles(SC, ParentPlatform);
						}
					}
				}

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
				StageLocalizationDataForTargetsInDirectory(SC, PlatformGameConfig, CulturesToStage, DirectoryReference.Combine(SC.ProjectRoot, "Content", "Localization"));

				// Stage all plugin localization targets
				foreach (KeyValuePair<StagedFileReference, FileReference> StagedPlugin in StagedPlugins)
				{
					StageLocalizationDataForPlugin(SC, CulturesToStage, StagedPlugin.Value);
				}

				// Stage any platform extension localization targets
				{
					string PlatformExtensionName = SC.StageTargetPlatform.PlatformType.ToString();
					StageLocalizationDataForTargetsInDirectory(SC, null, CulturesToStage, DirectoryReference.Combine(SC.LocalRoot, "Engine", "Platforms", PlatformExtensionName, "Content", "Localization"));
					StageLocalizationDataForTargetsInDirectory(SC, PlatformGameConfig, CulturesToStage, DirectoryReference.Combine(SC.ProjectRoot, "Platforms", PlatformExtensionName, "Content", "Localization"));
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
					List<string> MovieBlackList;
					PlatformGameConfig.GetArray("/Script/UnrealEd.ProjectPackagingSettings", "MovieBlacklist", out MovieBlackList);
					if (MovieBlackList == null)
					{
						// Make an empty list to avoid having to null check below
						MovieBlackList = new List<string>();
					}

					// UFS is required when using a file server
					StagedFileType MovieFileType = Params.FileServer ? StagedFileType.UFS : StagedFileType.NonUFS;

					DirectoryReference EngineMoviesDir = DirectoryReference.Combine(SC.EngineRoot, "Content", "Movies");
					if (DirectoryReference.Exists(EngineMoviesDir))
					{
						List<FileReference> MovieFiles = SC.FindFilesToStage(EngineMoviesDir, StageFilesSearch.AllDirectories);
						SC.StageFiles(MovieFileType, MovieFiles.Where(x => !x.HasExtension(".uasset") && !x.HasExtension(".umap") && !MovieBlackList.Contains(x.GetFileNameWithoutAnyExtensions())));
					}

					DirectoryReference ProjectMoviesDir = DirectoryReference.Combine(SC.ProjectRoot, "Content", "Movies");
					if (DirectoryReference.Exists(ProjectMoviesDir))
					{
						List<FileReference> MovieFiles = SC.FindFilesToStage(ProjectMoviesDir, StageFilesSearch.AllDirectories);
						SC.StageFiles(MovieFileType, MovieFiles.Where(x => !x.HasExtension(".uasset") && !x.HasExtension(".umap") && !MovieBlackList.Contains(x.GetFileNameWithoutAnyExtensions())));
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

				// Stage all the cooked data. Currently not filtering this by restricted folders, since we shouldn't mask invalid references by filtering them out.
				if (DirectoryReference.Exists(CookOutputDir))
				{
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
					if (!TargetReceipt.TryRead(ReceiptFileName, out Receipt))
					{
						throw new AutomationException("Missing or invalid target receipt ({0})", ReceiptFileName);
					}

					foreach (RuntimeDependency RuntimeDependency in Receipt.RuntimeDependencies)
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

				bCreatePluginManifest = true;
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

		// Create plugin manifests after the directory mappings
		if (bCreatePluginManifest && Params.UsePak(SC.StageTargetPlatform))
		{
			// Generate a plugin manifest if we're using a pak file and not creating a mod. Mods can be enumerated independently by users copying them into the Mods directory.
			if (Params.HasDLCName)
			{
				CreatePluginManifest(SC, SC.FilesToStage.UFSFiles, StagedFileType.UFS, Params.DLCFile.GetFileNameWithoutAnyExtensions());
			}
			else
			{
				CreatePluginManifest(SC, SC.FilesToStage.UFSFiles, StagedFileType.UFS, Params.ShortProjectName);
			}
		}

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
		if (Params.HasIterateSharedCookedBuild)
		{
			// Shared NonUFS files are staged in their remapped location, and may be duplicated in the to-stage list.
			Dictionary<StagedFileReference, FileReference> NonUFSToStage = new Dictionary<StagedFileReference, FileReference>();
			foreach (KeyValuePair<StagedFileReference, FileReference> StagedFilePair in SC.FilesToStage.NonUFSFiles)
			{
				NonUFSToStage[SC.StageTargetPlatform.Remap(StagedFilePair.Key)] = StagedFilePair.Value;
			}
			SC.FilesToStage.NonUFSFiles = NonUFSToStage;
		}
		else
		{
			SC.FilesToStage.NonUFSFiles = SC.FilesToStage.NonUFSFiles.ToDictionary(x => SC.StageTargetPlatform.Remap(x.Key), x => x.Value);
		}

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
		foreach (string RestrictedName in SC.RestrictedFolderNames)
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
			foreach (string RestrictedFolderName in SC.RestrictedFolderNames)
			{
				if (RestrictedFiles.Any(x => x.ContainsName(RestrictedFolderName)))
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
			foreach (string RestrictedName in SC.RestrictedFolderNames)
			{
				Message.AppendFormat("\n{0}", RestrictedName);
			}
			Message.Append("\nIf these files are intended to be distributed in packaged builds, move the source files out of a restricted folder, or remap them during staging using the following syntax in DefaultGame.ini:");
			Message.Append("\n[Staging]");
			Message.Append("\n+RemapDirectories=(From=\"Foo/NoRedist\", To=\"Foo\")");
			if (RestrictedNames.Any(x => x != "NotForLicensees" && x != "NoRedist")) // We don't ever want internal stuff white-listing folders like this
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
	/// <param name="PlatformExtensionName">The name of the platform when staging config files from a platform extension</param>
	static void StageConfigFiles(DeploymentContext SC, DirectoryReference ConfigDir, string PlatformExtensionName)
	{
		List<FileReference> ConfigFiles = SC.FindFilesToStage(ConfigDir, "*.ini", StageFilesSearch.AllDirectories);
		foreach (FileReference ConfigFile in ConfigFiles)
		{
			Nullable<bool> ShouldStage = ShouldStageConfigFile(SC, ConfigDir, ConfigFile, PlatformExtensionName);
			if (ShouldStage == null)
			{
				CommandUtils.LogWarning("The config file '{0}' will be staged, but is not whitelisted or blacklisted. Add +WhitelistConfigFiles={0} or +BlacklistConfigFiles={0} to the [Staging] section of DefaultGame.ini", SC.GetStagedFileLocation(ConfigFile));
			}

			if (ShouldStage ?? true)
			{
				SC.StageFile(StagedFileType.UFS, ConfigFile);
			}
			else
			{
				CommandUtils.LogInformation("Excluding config file {0}", ConfigFile);
			}
		}
	}

	/// <summary>
	/// Looks for a potential PlatformExtension config directory
	/// </summary>
	/// <param name="SC">The staging context</param>
	/// <param name="PlatformName">The name of the platform/platform parent</param>
	static void StagePlatformExtensionConfigFiles(DeploymentContext SC, string PlatformName)
	{
		DirectoryReference PlatformEngineConfigDir = DirectoryReference.Combine(SC.LocalRoot, "Engine", "Platforms", PlatformName, "Config");
		DirectoryReference PlatformProjectConfigDir = DirectoryReference.Combine(SC.ProjectRoot, "Platforms", PlatformName, "Config");

		if (DirectoryReference.Exists(PlatformEngineConfigDir))
		{
			StageConfigFiles(SC, PlatformEngineConfigDir, PlatformName);

		}
		if (DirectoryReference.Exists(PlatformProjectConfigDir))
		{
			StageConfigFiles(SC, PlatformProjectConfigDir, PlatformName);
		}
	}

	/// <summary>
	/// Determines if an individual config file should be staged
	/// </summary>
	/// <param name="SC">The staging context</param>
	/// <param name="ConfigDir">Directory containing the config files</param>
	/// <param name="ConfigFile">The config file to check</param>
	/// <returns>True if the file should be staged, false otherwise</returns>
	static Nullable<bool> ShouldStageConfigFile(DeploymentContext SC, DirectoryReference ConfigDir, FileReference ConfigFile, string PlatformExtensionName)
	{
		StagedFileReference StagedConfigFile = SC.GetStagedFileLocation(ConfigFile);
		if (SC.WhitelistConfigFiles.Contains(StagedConfigFile))
		{
			return true;
		}
		if (SC.BlacklistConfigFiles.Contains(StagedConfigFile))
		{
			return false;
		}

		string NormalizedPath = ConfigFile.MakeRelativeTo(ConfigDir).ToLowerInvariant().Replace('\\', '/');

		int DirectoryIdx = NormalizedPath.IndexOf('/');
		if (DirectoryIdx == -1)
		{
			const string BasePrefix = "base";
			if (NormalizedPath.StartsWith(BasePrefix))
			{
				string ShortName = NormalizedPath.Substring(BasePrefix.Length);
				if (PlatformExtensionName != null)
				{
					if (!ShortName.StartsWith(PlatformExtensionName, StringComparison.InvariantCultureIgnoreCase))
					{
						// Ignore config files in the platform directory that don't start with the platform name.
						return false;
					}

					ShortName = ShortName.Substring(PlatformExtensionName.Length);
				}

				return ShouldStageConfigSuffix(SC, ConfigFile, ShortName);
			}

			const string DefaultPrefix = "default";
			if (NormalizedPath.StartsWith(DefaultPrefix))
			{
				string ShortName = NormalizedPath.Substring(DefaultPrefix.Length);
				if (PlatformExtensionName != null)
				{
					if (!ShortName.StartsWith(PlatformExtensionName, StringComparison.InvariantCultureIgnoreCase))
					{
						// Ignore config files in the platform directory that don't start with the platform name.
						return false;
					}

					ShortName = ShortName.Substring(PlatformExtensionName.Length);
				}

				return ShouldStageConfigSuffix(SC, ConfigFile, ShortName);
			}

			const string DedicatedServerPrefix = "dedicatedserver";
			if (NormalizedPath.StartsWith(DedicatedServerPrefix))
			{
				return SC.DedicatedServer ? ShouldStageConfigSuffix(SC, ConfigFile, NormalizedPath.Substring(DedicatedServerPrefix.Length)) : false;
			}

			if (NormalizedPath == "consolevariables.ini")
			{
				return SC.StageTargetConfigurations.Any(x => x != UnrealTargetConfiguration.Test && x != UnrealTargetConfiguration.Shipping);
			}

			if (NormalizedPath == "locgatherconfig.ini")
			{
				return false;
			}

			if (NormalizedPath == "designertoolsconfig.ini")
			{
				return false;
			}

			if (PlatformExtensionName != null)
			{
				if (NormalizedPath.StartsWith(PlatformExtensionName, StringComparison.InvariantCultureIgnoreCase))
				{
					string ShortName = NormalizedPath.Substring(PlatformExtensionName.Length);
					return ShouldStageConfigSuffix(SC, ConfigFile, ShortName);
				}

				if (NormalizedPath == "datadrivenplatforminfo.ini")
				{
					return true;
				}
			}
		}
		else
		{
			if (NormalizedPath.StartsWith("layouts/"))
			{
				return true;
			}

			if (NormalizedPath.StartsWith("localization/"))
			{
				return false;
			}

			string PlatformPrefix = String.Format("{0}/{0}", NormalizedPath.Substring(0, DirectoryIdx));
			if (NormalizedPath.StartsWith(PlatformPrefix))
			{
				return ShouldStageConfigSuffix(SC, ConfigFile, NormalizedPath.Substring(PlatformPrefix.Length));
			}

			string PlatformBasePrefix = String.Format("{0}/base{0}", NormalizedPath.Substring(0, DirectoryIdx));
			if (NormalizedPath.StartsWith(PlatformBasePrefix))
			{
				return ShouldStageConfigSuffix(SC, ConfigFile, NormalizedPath.Substring(PlatformBasePrefix.Length));
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
		switch (InvariantSuffix)
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
			case "installbundle.ini":
				return true;
			case "crypto.ini":
			case "editor.ini":
			case "editorgameagnostic.ini":
			case "editorkeybindings.ini":
			case "editorlayout.ini":
			case "editorperprojectusersettings.ini":
			case "editorsettings.ini":
			case "editorusersettings.ini":
			case "lightmass.ini":
			case "pakfilerules.ini":
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

	public static void CopyManifestFilesToStageDir(Dictionary<StagedFileReference, FileReference> Mapping, DirectoryReference StageDir, DirectoryReference ManifestDir, string ManifestName, HashSet<StagedFileReference> CRCFiles, string PlatformName, List<string> IniKeyBlacklist, List<string> IniSectionBlacklist)
	{
		LogInformation("Copying {0} to staging directory: {1}", ManifestName, StageDir);
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
				CopyFileIncremental(Src, Dest, IniKeyBlacklist:IniKeyBlacklist, IniSectionBlacklist:IniSectionBlacklist);
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
		CopyManifestFilesToStageDir(SC.FilesToStage.NonUFSFiles, SC.StageDirectory, SC.DebugStageDirectory, "NonUFSFiles", SC.StageTargetPlatform.GetFilesForCRCCheck(), SC.StageTargetPlatform.PlatformType.ToString(), SC.IniKeyBlacklist, SC.IniSectionBlacklist);

		bool bStageUnrealFileSystemFiles = !Params.CookOnTheFly && !Params.UsePak(SC.StageTargetPlatform) && !Params.FileServer;
		if (bStageUnrealFileSystemFiles)
		{
			Dictionary<StagedFileReference, FileReference> UFSFiles = new Dictionary<StagedFileReference, FileReference>(SC.FilesToStage.UFSFiles);
			foreach (KeyValuePair<StagedFileReference, FileReference> Pair in SC.CrashReporterUFSFiles)
			{
				FileReference ExistingLocation;
				if (!UFSFiles.TryGetValue(Pair.Key, out ExistingLocation))
				{
					UFSFiles.Add(Pair.Key, Pair.Value);
				}
				else if (ExistingLocation != Pair.Value)
				{
					throw new AutomationException("File '{0}' is set to be staged from '{1}' for project and '{2}' for crash reporter", Pair.Key, ExistingLocation, Pair.Value);
				}
			}
			CopyManifestFilesToStageDir(UFSFiles, SC.StageDirectory, SC.DebugStageDirectory, "UFSFiles", SC.StageTargetPlatform.GetFilesForCRCCheck(), SC.StageTargetPlatform.PlatformType.ToString(), SC.IniKeyBlacklist, SC.IniSectionBlacklist);
		}

		// Copy debug files last
		// they do not respect the DeployLowerCaseFilenames() setting, but if copied to a case-insensitive staging directory first they determine the casing for outer directories (like Engine/Content) 
		if (!Params.NoDebugInfo)
		{
			CopyManifestFilesToStageDir(SC.FilesToStage.NonUFSDebugFiles, SC.DebugStageDirectory, SC.DebugStageDirectory, "DebugFiles", SC.StageTargetPlatform.GetFilesForCRCCheck(), SC.StageTargetPlatform.PlatformType.ToString(), SC.IniKeyBlacklist, SC.IniSectionBlacklist);
		}
	}

	// Pak file rules to apply to a list of files
	private struct PakFileRules
	{
		// Name of config section
		public string Name;

		// Combined filter from all +Files= entries
		public FileFilter Filter;

		// Rather to exclude entirely from paks
		public bool bExcludeFromPaks;

		// Rather to allow overriding the chunk manifest, if false will only modify loose files
		public bool bOverrideChunkManifest;

		// Whether pak file rule is disabled
		public bool bDisabled;

		// List of pak files to use instead of manifest
		public List<string> OverridePaks;
	};

	/// <summary>
	/// Reads Default/BasePakFileRules.ini and returns all PakFileRules objects that apply for this deployment
	/// </summary>
	/// <param name="Params"></param>
	/// <param name="SC"></param>
	private static List<PakFileRules> GetPakFileRules(ProjectParams Params, DeploymentContext SC)
	{
		bool bWarnedAboutMultipleTargets = false;
		bool bFoundConfig = false;

		List<ConfigFile> ConfigFiles = new List<ConfigFile>();
		FileReference BaseConfigFileReference = FileReference.Combine(SC.EngineRoot, "Config", "BasePakFileRules.ini");
		if (FileReference.Exists(BaseConfigFileReference))
		{
			ConfigFiles.Add(new ConfigFile(BaseConfigFileReference));
			bFoundConfig = true;
		}

		FileReference ProjectConfigFileReference = FileReference.Combine(SC.ProjectRoot, "Config", "DefaultPakFileRules.ini");
		if (FileReference.Exists(ProjectConfigFileReference))
		{
			ConfigFiles.Add(new ConfigFile(ProjectConfigFileReference));
			bFoundConfig = true;
		}

		if (!bFoundConfig)
		{
			return null;
		}

		bool bChunkedBuild = SC.PlatformUsesChunkManifests && DoesChunkPakManifestExist(Params, SC);

		ConfigHierarchy PakRulesConfig = new ConfigHierarchy(ConfigFiles);

		List<PakFileRules> RulesList = new List<PakFileRules>();
		foreach (string SectionName in PakRulesConfig.SectionNames)
		{
			//LogInformation("Building PakFileRules for Section {0}", SectionName);

			bool bOnlyChunkedBuilds = false;
			if (!PakRulesConfig.TryGetValue(SectionName, "bOnlyChunkedBuilds", out bOnlyChunkedBuilds))
			{
				bOnlyChunkedBuilds = false;
			}

			bool bOnlyNonChunkedBuilds = false;
			if (!PakRulesConfig.TryGetValue(SectionName, "bOnlyNonChunkedBuilds", out bOnlyNonChunkedBuilds))
			{
				bOnlyNonChunkedBuilds = false;
			}

			if (bChunkedBuild && bOnlyNonChunkedBuilds)
			{
				continue;
			}

			if(!bChunkedBuild && bOnlyChunkedBuilds)
			{
				continue;
			}

			string PlatformString;
			if (PakRulesConfig.TryGetValue(SectionName, "Platforms", out PlatformString))
			{
				string[] PlatformStrings = PlatformString.Split(new char[] { ',', ' ' }, StringSplitOptions.RemoveEmptyEntries);
				bool bMatches = false;

				// Check platform string
				foreach (string Platform in PlatformStrings)
				{
					if (SC.StageTargetPlatform.PlatformType.ToString().Equals(Platform, StringComparison.OrdinalIgnoreCase))
					{
						bMatches = true;
						break;
					}
					else if (SC.StageTargetPlatform.IniPlatformType.ToString().Equals(Platform, StringComparison.OrdinalIgnoreCase))
					{
						bMatches = true;
						break;
					}
				}

				if (!bMatches)
				{
					//LogInformation("No matching platform for PakFileRules for Section {0} : {1}, {2}", SectionName, SC.StageTargetPlatform.PlatformType.ToString(), SC.StageTargetPlatform.IniPlatformType.ToString());
					continue;
				}
			}

			string TargetString;
			if (PakRulesConfig.TryGetValue(SectionName, "Targets", out TargetString))
			{
				string[] TargetStrings = TargetString.Split(new char[] { ',', ' ' }, StringSplitOptions.RemoveEmptyEntries);
				bool bMatches = false;

				// Check target string
				foreach (string Target in TargetStrings)
				{
					foreach (UnrealTargetConfiguration C in SC.StageTargetConfigurations)
					{
						if (C.ToString() == Target)
						{
							bMatches = true;
							break;
						}
					}
				}

				if (!bMatches)
				{
					continue;
				}
				else if (SC.StageTargetConfigurations.Count > 0 && !bWarnedAboutMultipleTargets)
				{
					bWarnedAboutMultipleTargets = true;
					LogInformation("Staging with more than one target, PakFileRules may apply too many rules!");
				}
			}

			PakFileRules PakRules = new PakFileRules();
			PakRules.Name = SectionName;
			PakRulesConfig.TryGetValue(SectionName, "bExcludeFromPaks", out PakRules.bExcludeFromPaks);
			PakRulesConfig.TryGetValue(SectionName, "bOverrideChunkManifest", out PakRules.bOverrideChunkManifest);
			PakRulesConfig.TryGetValue(SectionName, "bDisabled", out PakRules.bDisabled);
			string PakString;
			PakRulesConfig.TryGetValue(SectionName, "OverridePaks", out PakString);

			if (PakString != null && PakString.Length > 0)
			{
				PakRules.OverridePaks = PakString.Split(new char[] { ',', ' ' }, StringSplitOptions.RemoveEmptyEntries).ToList();
			}
			else
			{
				PakRules.OverridePaks = null;
			}

			if (PakRules.bExcludeFromPaks && PakRules.OverridePaks != null)
			{
				LogWarning("Error in PakFileRules {0}, set to exclude but also sets override!", PakRules.Name);
				continue;
			}
			else if (!PakRules.bExcludeFromPaks && PakRules.OverridePaks == null)
			{
				LogWarning("Error in PakFileRules {0}, set to include but did not specify paks!", PakRules.Name);
				continue;
			}

			IReadOnlyList<string> FilesEnumberable;
			if (PakRulesConfig.TryGetValues(SectionName, "Files", out FilesEnumberable))
			{
				// Only add if we have actual files, we can end up with none due to config overriding
				PakRules.Filter = new FileFilter();
				foreach (string FileFilter in FilesEnumberable)
				{
					//LogInformation("Adding to PakFileRules for Section {0} : {1}", SectionName, FileFilter);
					PakRules.Filter.AddRule(FileFilter);
				}

				RulesList.Add(PakRules);
			}
		}

		return RulesList;
	}

	/// <summary>
	/// Attempts to apply the pak file rules to a specific staging file, returns false if it should be excluded
	/// </summary>
	/// <param name="Params"></param>
	/// <param name="SC"></param>
	private static void ApplyPakFileRules(List<PakFileRules> RulesList, KeyValuePair<string, string> StagingFile, HashSet<ChunkDefinition> ModifyPakList, Dictionary<string, ChunkDefinition> ChunkNameToDefinition, out bool bExcludeFromPaks)
	{
		bExcludeFromPaks = false;

		if (RulesList == null)
		{
			return;
		}

		// Search in order, return on first match
		foreach (var PakRules in RulesList)
		{
			if (!PakRules.bDisabled && PakRules.Filter.Matches(StagingFile.Key))
			{
				if (ModifyPakList != null && ModifyPakList.Count > 0)
				{
					// Only override the existing list if bOverrideChunkManifest is set
					if (!PakRules.bOverrideChunkManifest)
					{
						return;
					}

					if (PakRules.OverridePaks != null)
					{
						LogInformation("Overridding chunk assignment {0} to {1}, this can cause broken references", StagingFile.Key, string.Join(", ", PakRules.OverridePaks));
					}
					else if (bExcludeFromPaks)
					{
						LogInformation("Removing {0} from pak despite chunk assignment, this can cause broken references", StagingFile.Key);
					}
				}

				bExcludeFromPaks = PakRules.bExcludeFromPaks;
				if (PakRules.OverridePaks != null && ModifyPakList != null && ChunkNameToDefinition != null)
				{
					ModifyPakList.Clear();
					ModifyPakList.UnionWith(PakRules.OverridePaks.Select(x =>
					{
						if (!ChunkNameToDefinition.ContainsKey(x))
						{
							LogInformation("With pak rules, {0} is moved to {1}", StagingFile.Key, x);
							ChunkDefinition TargetChunk = new ChunkDefinition(x);
							// TODO: Need to set encryptionGuid?
							//TargetChunk.RequestedEncryptionKeyGuid = Chunk.RequestedEncryptionKeyGuid;
							//TargetChunk.EncryptionKeyGuid = Chunk.EncryptionKeyGuid;
							ChunkNameToDefinition.Add(x, TargetChunk);

							return TargetChunk;
						}

						return ChunkNameToDefinition[x];
					}));
					//LogInformation("Setting pak assignment for file {0} to {1}", StagingFile.Key, string.Join(", ", PakRules.OverridePaks));
				}
				else if (bExcludeFromPaks)
				{
					//LogInformation("Excluding {0} from pak file", StagingFile.Key);
				}

				return;
			}
		}
	}


	/// <summary>
	/// Creates a pak file using staging context (single manifest)
	/// </summary>
	/// <param name="Params"></param>
	/// <param name="SC"></param>
	private static void CreatePakUsingStagingManifest(ProjectParams Params, DeploymentContext SC)
	{
		LogInformation("Creating pak using staging manifest.");

		DumpManifest(SC, CombinePaths(CmdEnv.LogFolder, "PrePak" + (SC.DedicatedServer ? "_Server" : "") + "_" + SC.CookPlatform));

		var UnrealPakResponseFile = CreatePakResponseFileFromStagingManifest(SC, SC.FilesToStage.UFSFiles);

		List<PakFileRules> PakRulesList = GetPakFileRules(Params, SC);

		List<string> FilesToRemove = new List<string>();

		// Apply the pak file rules, this can remove things but will not override the pak file name
		foreach (var StagingFile in UnrealPakResponseFile)
		{
			bool bExcludeFromPaks = false;
			ApplyPakFileRules(PakRulesList, StagingFile, null, null, out bExcludeFromPaks);

			if (bExcludeFromPaks)
			{
				FilesToRemove.Add(StagingFile.Key);
			}
		}

		foreach (var FileToRemove in FilesToRemove)
		{
			UnrealPakResponseFile.Remove(FileToRemove);
		}

		EncryptionAndSigning.CryptoSettings PakCryptoSettings = EncryptionAndSigning.ParseCryptoSettings(DirectoryReference.FromFile(Params.RawProjectPath), SC.StageTargetPlatform.IniPlatformType);
		FileReference CryptoKeysCacheFilename = FileReference.Combine(SC.MetadataDir, "Crypto.json");
		PakCryptoSettings.Save(CryptoKeysCacheFilename);

		List<CreatePakParams> PakInputs = new List<CreatePakParams>();
		PakInputs.Add(new CreatePakParams(SC.ShortProjectName, UnrealPakResponseFile, Params.Compressed, null));
		CreatePaks(Params, SC, PakInputs, PakCryptoSettings, CryptoKeysCacheFilename);
	}

	/// <summary>
	/// Creates a standalone pak file for crash reporter
	/// </summary>
	/// <param name="Params">The packaging parameters</param>
	/// <param name="SC">Staging context</param>
	private static void CreatePakForCrashReporter(ProjectParams Params, DeploymentContext SC)
	{
		LogInformation("Creating pak for crash reporter.");

		Dictionary<string, string> PakResponseFile = CreatePakResponseFileFromStagingManifest(SC, SC.CrashReporterUFSFiles);
		FileReference OutputLocation = FileReference.Combine(SC.RuntimeRootDir, "Engine", "Programs", "CrashReportClient", "Content", "Paks", "CrashReportClient.pak");

		RunUnrealPak(Params, PakResponseFile, OutputLocation, null, null, Params.Compressed, null, null, null, null);
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
			FileReference PakBlacklistFilename = FileReference.Combine(SC.ProjectRoot, "Build", SC.PlatformDir, string.Format("PakBlacklist-{0}.txt", SC.StageTargetConfigurations[0].ToString()));
			if (FileReference.Exists(PakBlacklistFilename))
			{
				LogInformation("Applying PAK blacklist file {0}. This is deprecated in favor of DefaultPakFileRules.ini", PakBlacklistFilename);
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
					LogInformation("Excluding {0}", Src);
					continue;
				}
			}

			// Filter I/O store container files
			if (Src.HasExtension(".ucas") || Src.HasExtension(".utoc"))
			{
				LogInformation("Excluding {0}", Src);
				continue;
			}

			// Do a filtered copy of all ini files to allow stripping of values that we don't want to distribute
			if (Src.HasExtension(".ini"))
			{
				string SubFolder = Pair.Key.Name.Replace('/', Path.DirectorySeparatorChar);
				FileReference NewIniFilename = FileReference.Combine(SC.ProjectRoot, "Saved", "Temp", SC.PlatformDir, SubFolder);
				InternalUtils.SafeCreateDirectory(NewIniFilename.Directory.FullName, true);
				InternalUtils.SafeCopyFile(Src.FullName, NewIniFilename.FullName, IniKeyBlacklist:SC.IniKeyBlacklist, IniSectionBlacklist:SC.IniSectionBlacklist);
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
	/// Parameter class for CreatePaks(). Each instance stores information about a pak file to be created.
	/// </summary>
	[DebuggerDisplay("PakName")]
	class CreatePakParams
	{
		/// <summary>
		/// Path to the base output file for this pak file 
		/// </summary>
		public string PakName;

		/// <summary>
		/// Map of files within the pak file to their source file on disk
		/// </summary>
		public Dictionary<string, string> UnrealPakResponseFile;

		/// <summary>
		/// Whether to enable compression
		/// </summary>
		public bool bCompressed;

		/// <summary>
		/// GUID of the encryption key for this pak file
		/// </summary>
		public string EncryptionKeyGuid;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="PakName">Path to the base output file for this pak file</param>
		/// <param name="UnrealPakResponseFile">Map of files within the pak file to their source file on disk</param>
		/// <param name="bCompressed">Whether to enable compression</param>
		public CreatePakParams(string PakName, Dictionary<string, string> UnrealPakResponseFile, bool bCompressed, string EncryptionKeyGuid)
		{
			this.PakName = PakName;
			this.UnrealPakResponseFile = UnrealPakResponseFile;
			this.bCompressed = bCompressed;
			this.EncryptionKeyGuid = EncryptionKeyGuid;
		}
	}

	private static bool ShouldSkipGeneratingPatch(ConfigHierarchy PlatformGameConfig, string Filename)
	{
		bool bIsEarlyDownloaderPakFile = false;
		string EarlyDownloaderPakFilePrefix = "";
		if (PlatformGameConfig.GetString("/Script/UnrealEd.ProjectPackagingSettings", "EarlyDownloaderPakFilePrefix", out EarlyDownloaderPakFilePrefix))
		{
			bIsEarlyDownloaderPakFile = String.Equals(EarlyDownloaderPakFilePrefix, Filename, StringComparison.OrdinalIgnoreCase);
		}

		return bIsEarlyDownloaderPakFile;
	}

	private static string GetPakFilePostFix(bool bShouldGeneratePatch, bool bHasIterateSharedCookedBuild, ConfigHierarchy PlatformGameConfig, string Filename)
	{
		if (ShouldSkipGeneratingPatch(PlatformGameConfig, Filename))
		{
			return String.Empty;
		}
		else if(bShouldGeneratePatch)
		{
			return "_P";
		}
		else if(bHasIterateSharedCookedBuild)
		{
			// shared cooked builds will produce a patch
			// then be combined with the shared cooked build
			return "_S_P";
		}

		return String.Empty;
	}

	/// <summary>
	/// Creates a pak file using response file.
	/// </summary>
	/// <param name="Params"></param>
	/// <param name="SC"></param>
	/// <param name="UnrealPakResponseFile"></param>
	/// <param name="PakName"></param>
	private static void CreatePaks(ProjectParams Params, DeploymentContext SC, List<CreatePakParams> PakParamsList, EncryptionAndSigning.CryptoSettings CryptoSettings, FileReference CryptoKeysCacheFilename)
	{
		bool bShouldGeneratePatch = Params.IsGeneratingPatch && SC.StageTargetPlatform.GetPlatformPatchesWithDiffPak(Params, SC);

		if (bShouldGeneratePatch && !Params.HasBasedOnReleaseVersion)
		{
			LogInformation("Generating patch required a based on release version flag");
		}

		const string OutputFilenameExtension = ".pak";

		// read some compression settings from the project (once, shared across all pak commands)
		ConfigHierarchy PlatformGameConfig = ConfigCache.ReadHierarchy(ConfigHierarchyType.Game, DirectoryReference.FromFile(Params.RawProjectPath), SC.StageTargetPlatform.IniPlatformType);
		string CompressionFormats = "";
		if (PlatformGameConfig.GetString("/Script/UnrealEd.ProjectPackagingSettings", "PakFileCompressionFormats", out CompressionFormats))
		{
			CompressionFormats = " -compressionformats=" + CompressionFormats;
		}

		Func<string, string> GetPostFix = PakFilename => GetPakFilePostFix(bShouldGeneratePatch, Params.HasIterateSharedCookedBuild, PlatformGameConfig, PakFilename);

		// the game may want to control compression settings, but since it may be in a plugin that checks the commandline for the settings, we need to pass
		// the settings directly on the UnrealPak commandline, and not put it into the batch file lines (plugins can't get the unrealpak command list, and
		// there's not a great way to communicate random strings down into the plugins during plugin init time)
		string AdditionalCompressionOptionsOnCommandLine = "";
		PlatformGameConfig.GetString("/Script/UnrealEd.ProjectPackagingSettings", "PakFileAdditionalCompressionOptions", out AdditionalCompressionOptionsOnCommandLine);

		List<string> Commands = new List<string>();
		List<string> LogNames = new List<string>();
		List<string> IoStoreCommands = new List<string>();

		// Calculate target patch index by iterating all pak files in source build
		int TargetPatchIndex = 0;
		int NumPakFiles = 0;
		if (bShouldGeneratePatch)
		{
			foreach (CreatePakParams PakParams in PakParamsList)
			{
				if(ShouldSkipGeneratingPatch(PlatformGameConfig, PakParams.PakName))
				{
					continue;
				}

				string OutputFilename = PakParams.PakName + "-" + SC.FinalCookPlatform;
				string ExistingPatchSearchPath = SC.StageTargetPlatform.GetReleasePakFilePath(SC, Params, null);
				if (Directory.Exists(ExistingPatchSearchPath))
				{
					string PostFix = GetPostFix(PakParams.PakName);
					IEnumerable<string> PakFileSet = Directory.EnumerateFiles(ExistingPatchSearchPath, OutputFilename + "*" + PostFix + OutputFilenameExtension);
					NumPakFiles += PakFileSet.Count();

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
				}
			}

			if (Params.ShouldAddPatchLevel && NumPakFiles > 0)
			{
				TargetPatchIndex++;
			}
		}

		List<Tuple<FileReference, StagedFileReference, string>> Outputs = new List<Tuple<FileReference, StagedFileReference, string>>();
		foreach (CreatePakParams PakParams in PakParamsList)
		{
			string OutputFilename = PakParams.PakName + "-" + SC.FinalCookPlatform;
			string PostFix = GetPostFix(PakParams.PakName);
			if (bShouldGeneratePatch && !ShouldSkipGeneratingPatch(PlatformGameConfig, PakParams.PakName))
			{
				OutputFilename = OutputFilename + "_" + TargetPatchIndex;
			}
			OutputFilename = OutputFilename + PostFix;

			StagedFileReference OutputRelativeLocation;
			if (Params.HasDLCName)
			{
				OutputRelativeLocation = StagedFileReference.Combine(SC.RelativeProjectRootForStage, Params.DLCFile.Directory.MakeRelativeTo(SC.ProjectRoot), "Content", "Paks", SC.FinalCookPlatform, Params.DLCFile.GetFileNameWithoutExtension() + OutputFilename + ".pak");
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

			// Find primary and secondary order files if they exist
			List<FileReference> PakOrderFileLocations = new List<FileReference>();

			// preferred files
			string[] OrderFileNames = new string[] { "GameOpenOrder.log", "CookerOpenOrder.log", "EditorOpenOrder.log" };

			// search CookPlaform (e.g. IOSClient and then regular platform (e.g. IOS).
			string[] OrderLocations = new string[] { SC.FinalCookPlatform, SC.StageTargetPlatform.GetTargetPlatformDescriptor().Type.ToString() };

			for (int OrderFileIndex=0;OrderFileIndex<OrderFileNames.Length; OrderFileIndex++)
			{	
				string OrderFileName = OrderFileNames[OrderFileIndex];
				foreach (string OrderLocation in OrderLocations)
				{
					// Add input file to control order of file within the pak
					DirectoryReference PakOrderFileLocationBase = DirectoryReference.Combine(SC.ProjectRoot, "Build", OrderLocation, "FileOpenOrder");

					FileReference FileLocation = FileReference.Combine(PakOrderFileLocationBase, OrderFileName);

					if (FileExists_NoExceptions(FileLocation.FullName))
					{
						// Special case: Don't use editor open order as a secondary order file for UnrealPak
						// It's better to just use cooker order in that case
						if ( OrderFileIndex >= 2 && PakOrderFileLocations.Count >= 1 )
						{
							FileLocation = null;
						}
						PakOrderFileLocations.Add(FileLocation);
						break;
					}
				}

				if (PakOrderFileLocations.Count == 2)
				{
					break;
				}
			}

			bool bCopiedExistingPak = false;

			if (SC.StageTargetPlatform != SC.CookSourcePlatform)
			{
				// Check to see if we have an existing pak file we can use

				StagedFileReference SourceOutputRelativeLocation = StagedFileReference.Combine(SC.RelativeProjectRootForStage, "Content/Paks/", PakParams.PakName + "-" + SC.CookPlatform + PostFix + ".pak");
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
						LogInformation("Copying source pak from {0} to {1} instead of creating new pak", SourceOutputLocation, OutputLocation);
						bCopiedExistingPak = true;

						FileReference InSigFile = SourceOutputLocation.ChangeExtension(".sig");
						if (FileReference.Exists(InSigFile))
						{
							FileReference OutSigFile = OutputLocation.ChangeExtension(".sig");

							LogInformation("Copying pak sig from {0} to {1}", InSigFile, OutSigFile);

							if (!InternalUtils.SafeCopyFile(InSigFile.FullName, OutSigFile.FullName))
							{
								LogInformation("Failed to copy pak sig {0} to {1}, creating new pak", InSigFile, InSigFile);
								bCopiedExistingPak = false;
							}
						}

					}
				}
				if (!bCopiedExistingPak)
				{
					LogInformation("Failed to copy source pak from {0} to {1}, creating new pak", SourceOutputLocation, OutputLocation);
				}
			}

			string PatchSourceContentPath = null;
			if (bShouldGeneratePatch && !ShouldSkipGeneratingPatch(PlatformGameConfig, PakParams.PakName))
			{
				// don't include the post fix in this filename because we are looking for the source pak path
				string PakFilename = PakParams.PakName + "-" + SC.FinalCookPlatform + "*.pak";
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
					FileReference PrimaryOrderFile   = (PakOrderFileLocations.Count >= 1) ? PakOrderFileLocations[0] : null;
					FileReference SecondaryOrderFile = null;

					// Add a secondary order if there is one specified
					bool bUseSecondaryOrder = false;
					PlatformGameConfig.GetBool("/Script/UnrealEd.ProjectPackagingSettings", "bPakUsesSecondaryOrder", out bUseSecondaryOrder);
					if (bUseSecondaryOrder && PakOrderFileLocations.Count >= 2)
					{
						SecondaryOrderFile = PakOrderFileLocations[1];
					}

					string BulkOption = "";
                    ConfigHierarchy PlatformEngineConfig = null;
                    if (Params.EngineConfigs.TryGetValue(SC.StageTargetPlatform.PlatformType, out PlatformEngineConfig))
                    {
                        bool bMasterEnable = false;
						PlatformEngineConfig.GetBool("MemoryMappedFiles", "MasterEnable", out bMasterEnable);
                        if (bMasterEnable)
                        {
                            int Value = 0;
							PlatformEngineConfig.GetInt32("MemoryMappedFiles", "Alignment", out Value);
                            if (Value > 0)
                            {
                                BulkOption = String.Format(" -AlignForMemoryMapping={0}", Value);
                            }
                        }
                    }

					string AdditionalArgs = String.Empty;
					if (bShouldGeneratePatch && !ShouldSkipGeneratingPatch(PlatformGameConfig, PakParams.PakName))
					{
						string PatchSeekOptMode = String.Empty;
						string PatchSeekOptMaxGapSize = String.Empty;
						string PatchSeekOptMaxInflationPercent = String.Empty;
						string PatchSeekOptMaxAdjacentOrderDiff = String.Empty;
						if (PlatformGameConfig.GetString("/Script/UnrealEd.ProjectPackagingSettings", "PatchSeekOptMode", out PatchSeekOptMode))
						{
							AdditionalArgs += String.Format(" -PatchSeekOptMode={0}", PatchSeekOptMode);
						}
						if (PlatformGameConfig.GetString("/Script/UnrealEd.ProjectPackagingSettings", "PatchSeekOptMaxGapSize", out PatchSeekOptMaxGapSize))
						{
							AdditionalArgs += String.Format(" -PatchSeekOptMaxGapSize={0}", PatchSeekOptMaxGapSize);
						}
						if (PlatformGameConfig.GetString("/Script/UnrealEd.ProjectPackagingSettings", "PatchSeekOptMaxInflationPercent", out PatchSeekOptMaxInflationPercent))
						{
							AdditionalArgs += String.Format(" -PatchSeekOptMaxInflationPercent={0}", PatchSeekOptMaxInflationPercent);
						}
						if (PlatformGameConfig.GetString("/Script/UnrealEd.ProjectPackagingSettings", "PatchSeekOptMaxAdjacentOrderDiff", out PatchSeekOptMaxAdjacentOrderDiff))
						{
							AdditionalArgs += String.Format(" -PatchSeekOptMaxAdjacentOrderDiff={0}", PatchSeekOptMaxAdjacentOrderDiff);
						}
					}

					bool bPakFallbackOrderForNonUassetFiles = false;
					PlatformGameConfig.GetBool("/Script/UnrealEd.ProjectPackagingSettings", "bPakFallbackOrderForNonUassetFiles", out bPakFallbackOrderForNonUassetFiles);
					if (bPakFallbackOrderForNonUassetFiles)
					{
						AdditionalArgs += " -fallbackOrderForNonUassetFiles";
					}

					if (PlatformEngineConfig != null)
					{
						// if the runtime will want to reduce memory usage, we have to disable the pak index freezing
						bool bUnloadPakEntries = false;
						bool bShrinkPakEntries = false;
						PlatformEngineConfig.GetBool("Pak", "UnloadPakEntryFilenamesIfPossible", out bUnloadPakEntries);
						PlatformEngineConfig.GetBool("Pak", "ShrinkPakEntriesMemoryUsage", out bShrinkPakEntries);
						if (bUnloadPakEntries || bShrinkPakEntries)
						{
							AdditionalArgs += " -allowForIndexUnload";
						}
					}

					// pass the targetplatform so the index may be able to be frozen
					AdditionalArgs += " -platform=" + ConfigHierarchy.GetIniPlatformName(SC.StageTargetPlatform.IniPlatformType);

					Dictionary<string, string> UnrealPakResponseFile;
					if (ShouldCreateIoStoreContainerFiles(Params, SC))
					{
						UnrealPakResponseFile = new Dictionary<string, string>();
						Dictionary<string, string> IoStoreResponseFile = new Dictionary<string, string>();
						foreach (var Entry in PakParams.UnrealPakResponseFile)
						{
							if (Path.GetExtension(Entry.Key).Contains(".uasset") ||
								Path.GetExtension(Entry.Key).Contains(".umap") ||
								Path.GetExtension(Entry.Key).Contains(".ubulk") ||
								Path.GetExtension(Entry.Key).Contains(".uptnl"))
							{
								IoStoreResponseFile.Add(Entry.Key, Entry.Value);
							}
							else if (!Path.GetExtension(Entry.Key).Contains(".uexp"))
							{
								UnrealPakResponseFile.Add(Entry.Key, Entry.Value);
							}
						}
						IoStoreCommands.Add(GetIoStoreCommand(IoStoreResponseFile, OutputLocation, PrimaryOrderFile, PakParams.bCompressed, CryptoSettings, PakParams.EncryptionKeyGuid, SecondaryOrderFile));
					}
					else
					{
						UnrealPakResponseFile = PakParams.UnrealPakResponseFile;
					}
					Commands.Add(GetUnrealPakArguments(Params.RawProjectPath, UnrealPakResponseFile, OutputLocation, PrimaryOrderFile, SC.StageTargetPlatform.GetPlatformPakCommandLine(Params, SC) + AdditionalArgs + BulkOption + CompressionFormats + " " + Params.AdditionalPakOptions, PakParams.bCompressed, CryptoSettings, CryptoKeysCacheFilename, PatchSourceContentPath, PakParams.EncryptionKeyGuid, SecondaryOrderFile));
					LogNames.Add(OutputLocation.GetFileNameWithoutExtension());
				}
			}

			Outputs.Add(Tuple.Create(OutputLocation, OutputRelativeLocation, PatchSourceContentPath));
		}

		// Actually execute UnrealPak
		if (Commands.Count > 0)
		{
			RunUnrealPakInParallel( Commands, LogNames, AdditionalCompressionOptionsOnCommandLine);
		}

		if (IoStoreCommands.Count > 0)
		{
			string IoStoreCommandsFileName = CombinePaths(CmdEnv.LogFolder, "IoStoreCommands.txt");
			using (var Writer = new StreamWriter(IoStoreCommandsFileName, false, new System.Text.UTF8Encoding(true)))
			{
				foreach (string Command in IoStoreCommands)
				{
					Writer.WriteLine(Command);
				}
			}

			FileReference PackageOpenOrderFileLocation = null;
			FileReference CookerOpenOrderFileLocation = null;
			// search CookPlaform (e.g. IOSClient and then regular platform (e.g. IOS).
			string[] OrderLocations = new string[] { SC.FinalCookPlatform, SC.StageTargetPlatform.GetTargetPlatformDescriptor().Type.ToString() };

			foreach (string OrderLocation in OrderLocations.Reverse())
			{
				DirectoryReference IoStoreOrderFileLocationBase = DirectoryReference.Combine(SC.ProjectRoot, "Build", OrderLocation, "FileOpenOrder");
				FileReference FileLocation = FileReference.Combine(IoStoreOrderFileLocationBase, "PackageOpenOrder.log");
				if (FileExists_NoExceptions(FileLocation.FullName))
				{
					PackageOpenOrderFileLocation = FileLocation;
				}
				FileLocation = FileReference.Combine(IoStoreOrderFileLocationBase, "CookerOpenOrder.log");
				if (FileExists_NoExceptions(FileLocation.FullName))
				{
					CookerOpenOrderFileLocation = FileLocation;
				}
			}
			RunIoStore(Params, SC, IoStoreCommandsFileName, PackageOpenOrderFileLocation, CookerOpenOrderFileLocation);
		}

		// Do any additional processing on the command output
		for (int Idx = 0; Idx < PakParamsList.Count; Idx++)
		{
			string PakName = PakParamsList[Idx].PakName;
			FileReference OutputLocation = Outputs[Idx].Item1;
			StagedFileReference OutputRelativeLocation = Outputs[Idx].Item2;
			string PatchSourceContentPath = Outputs[Idx].Item3;

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
					string RawDataPakPath = CombinePaths(RawDataPath, PakName + "-" + SC.FinalCookPlatform + GetPostFix(PakName) + ".pak");
					bool bPakFilesAreSigned = InternalUtils.SafeFileExists(Path.ChangeExtension(OutputLocation.FullName, ".sig"));

					//copy the pak chunk to the raw data folder
					InternalUtils.SafeDeleteFile(RawDataPakPath, true);
					InternalUtils.SafeDeleteFile(Path.ChangeExtension(RawDataPakPath, ".sig"), true);
					InternalUtils.SafeCreateDirectory(RawDataPath, true);
					InternalUtils.SafeCopyFile(OutputLocation.FullName, RawDataPakPath);
					if (bPakFilesAreSigned)
					{
						InternalUtils.SafeCopyFile(Path.ChangeExtension(OutputLocation.FullName, ".sig"), Path.ChangeExtension(RawDataPakPath, ".sig"), true);
					}
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
						if (bPakFilesAreSigned)
						{
							InternalUtils.SafeCopyFile(PatchSourceContentPath, Path.ChangeExtension(SourceRawDataPakPath, ".sig"), true);
						}
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

					string CmdLine = String.Format("-BuildRoot={0} -CloudDir={1} -AppID={2} -AppName=\"{3}\" -BuildVersion=\"{4}\" -AppLaunch=\"{5}\"", BuildRoot, CloudDir, AppID, AppName, VersionString, AppLaunch);
					CmdLine += " -AppArgs=\"\"";
					CmdLine += " -custom=\"bIsPatch=false\"";
					CmdLine += String.Format(" -customint=\"ChunkID={0}\"", ChunkID);
					CmdLine += " -customint=\"PakReadOrdering=0\"";
					CmdLine += " -stdout";

					string BuildPatchToolLogFileName = "BuildPatchTool_" + PakName;
					RunAndLog(CmdEnv, BPTExe, CmdLine, BuildPatchToolLogFileName, Options: ERunOptions.Default | ERunOptions.UTF8Output);

					InternalUtils.SafeCopyFile(SourceManifestPath, DestManifestPath);

					// generate the master manifest
					GenerateMasterChunkManifest(CloudDir, Params.ChunkInstallVersionString, SC.FinalCookPlatform);
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
				if (bShouldGeneratePatch && !ShouldSkipGeneratingPatch(PlatformGameConfig, PakName))
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
	}

	private static void RunIoStore(ProjectParams Params, DeploymentContext SC, string CommandsFileName, FileReference PackageOrderFileLocation, FileReference CookerOpenOrderFileLocation)
	{
		FileReference OutputLocation = FileReference.Combine(SC.RuntimeRootDir, SC.RelativeProjectRootForStage.Name);
		string CommandletParams = String.Format("-OutputDirectory={0} -CookedDirectory={1} -Commands={2}", MakePathSafeToUseWithCommandLine(OutputLocation.FullName), MakePathSafeToUseWithCommandLine(SC.PlatformCookDir.ToString()), MakePathSafeToUseWithCommandLine(CommandsFileName));
		if (PackageOrderFileLocation != null)
		{
			CommandletParams += String.Format(" -PackageOrder={0}", MakePathSafeToUseWithCommandLine(PackageOrderFileLocation.FullName));
		}
		if (CookerOpenOrderFileLocation != null)
		{
			CommandletParams += String.Format(" -CookerOrder={0}", MakePathSafeToUseWithCommandLine(CookerOpenOrderFileLocation.FullName));
		}
		LogInformation("Running IoStore commandlet with arguments: {0}", CommandletParams);
		RunCommandlet(SC.RawProjectPath, Params.UE4Exe, "IoStore", CommandletParams);
	}

	private static void RunUnrealPakInParallel(List<string> Commands, List<string> LogNames, string AdditionalCompressionOptionsOnCommandLine)
	{
		LogInformation("Executing {0} UnrealPak command{1}...", Commands.Count, (Commands.Count > 1)? "s" : "");

		// Spawn tasks for each command
		IProcessResult[] Results = new IProcessResult[Commands.Count];
		using(ThreadPoolWorkQueue Queue = new ThreadPoolWorkQueue())
		{
			string UnrealPakExe = GetUnrealPakLocation().FullName;
			for(int Idx = 0; Idx < Commands.Count; Idx++)
			{
				string LogFile = LogUtils.GetUniqueLogName(CombinePaths(CmdEnv.LogFolder, String.Format("UnrealPak-{0}", LogNames[Idx])));
				string Arguments = Commands[Idx] = String.Format("{0} -multiprocess -abslog={1} {2}", Commands[Idx], MakePathSafeToUseWithCommandLine(LogFile), AdditionalCompressionOptionsOnCommandLine);

				int LocalIdx = Idx;
				Queue.Enqueue(() => Results[LocalIdx] = Run(UnrealPakExe, Arguments, Options: ERunOptions.AppMustExist | ERunOptions.NoLoggingOfRunCommand));
			}

			bool bComplete = false;
			while(!bComplete)
			{
				bComplete = Queue.Wait(10 * 1000);
				LogInformation("Waiting for child processes to complete ({0}/{1})", Results.Count(x => x != null), Results.Length);
			}
		}

		// Output all the results
		int NumFailed = 0;
		for(int Idx = 0; Idx < Results.Length; Idx++)
		{
			LogInformation("Output from: {0}", Commands[Idx]);
			using(new LogIndentScope("  "))
			{
				foreach(string Line in Results[Idx].Output.TrimEnd().Split('\n'))
				{
					LogInformation(Line);
				}
			}
			LogInformation("UnrealPak terminated with exit code {0}", Results[Idx].ExitCode);

			if(Results[Idx].ExitCode > 0)
			{
				NumFailed++;
			}
		}
			
		// Abort if any instance failed
		if(NumFailed > 0)
		{
			throw new AutomationException("UnrealPak failed");
		}
	}

	/// <summary>
	/// Creates pak files using streaming install chunk manifests.
	/// </summary>
	/// <param name="Params"></param>
	/// <param name="SC"></param>
	private static void CopyPaksFromNetwork(ProjectParams Params, DeploymentContext SC)
	{
		LogInformation("Copying paks from network.");

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
								LogInformation("Copying from cache {0} -> {1}", CacheSrcFile, DestFileName);
								CopyFileIncremental(new FileReference(CacheSrcFile), new FileReference(DestFileName));
								continue;
							}
						}
					}
					catch (Exception)
					{

					}
				}
				LogInformation("Copying {0} -> {1}", SrcFile, DestFileName);
				CopyFileIncremental(new FileReference(SrcFile), new FileReference(DestFileName));
			}
		}
	}

	// Defines contents of a specific chunk/pak file
	private class ChunkDefinition
	{
		public ChunkDefinition(string InChunkName)
		{
			ChunkName = InChunkName;
			ResponseFile = new Dictionary<string, string>();
			Manifest = null;
			bCompressed = false;
		}

		// Name of pak file without extension, ie pakchunk0
		public string ChunkName;

		// List of files to include in this chunk
		public Dictionary<string, string> ResponseFile;

		// Parsed copy of pakchunk*.txt
		public HashSet<string> Manifest;

		// Data for compression and encryption
		public bool bCompressed;
		public string EncryptionKeyGuid;
		public string RequestedEncryptionKeyGuid;
	}

	/// <summary>
	/// Creates pak files using streaming install chunk manifests.
	/// </summary>
	/// <param name="Params"></param>
	/// <param name="SC"></param>
	private static void CreatePaksUsingChunkManifests(ProjectParams Params, DeploymentContext SC)
	{
		LogInformation("Creating pak using streaming install manifests.");
		DumpManifest(SC, CombinePaths(CmdEnv.LogFolder, "PrePak" + (SC.DedicatedServer ? "_Server" : "") + "_" + SC.CookPlatform));

		ConfigHierarchy PlatformGameConfig = ConfigCache.ReadHierarchy(ConfigHierarchyType.Game, DirectoryReference.FromFile(Params.RawProjectPath), SC.StageTargetPlatform.IniPlatformType);

		bool bShouldGenerateEarlyDownloaderPakFile = false, bForceOneChunkPerFile = false;
		string EarlyDownloaderPakFilePrefix = string.Empty;
		PlatformGameConfig.GetBool("/Script/UnrealEd.ProjectPackagingSettings", "GenerateEarlyDownloaderPakFile", out bShouldGenerateEarlyDownloaderPakFile);
		PlatformGameConfig.GetBool("/Script/UnrealEd.ProjectPackagingSettings", "bForceOneChunkPerFile", out bForceOneChunkPerFile);
		PlatformGameConfig.GetString("/Script/UnrealEd.ProjectPackagingSettings", "EarlyDownloaderPakFilePrefix", out EarlyDownloaderPakFilePrefix);

		List<ChunkDefinition> ChunkDefinitions = new List<ChunkDefinition>();
		List<PakFileRules> PakRulesList = GetPakFileRules(Params, SC);

		var TmpPackagingPath = GetTmpPackagingPath(Params, SC);

		// Parse and cache crypto settings from INI file
		EncryptionAndSigning.CryptoSettings PakCryptoSettings = EncryptionAndSigning.ParseCryptoSettings(DirectoryReference.FromFile(Params.RawProjectPath), SC.StageTargetPlatform.IniPlatformType);
		FileReference CryptoKeysCacheFilename = FileReference.Combine(SC.MetadataDir, "Crypto.json");
		PakCryptoSettings.Save(CryptoKeysCacheFilename);

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
				string[] ChunkOptions = ChunkList[Index].Split(' ');

				// Set chunk name to string like "pakchunk0"
				var ChunkManifestFilename = CombinePaths(TmpPackagingPath, ChunkOptions[0]);
				ChunkDefinition CD = new ChunkDefinition(Path.GetFileNameWithoutExtension(ChunkOptions[0]));
				for (int I = 1; I < ChunkOptions.Length; ++I)
				{
					if (string.Compare(ChunkOptions[I], "compressed", true) == 0)
					{
						CD.bCompressed = true;
					}
					else if (ChunkOptions[I].StartsWith("encryptionkeyguid="))
					{
						CD.RequestedEncryptionKeyGuid = ChunkOptions[I].Substring(ChunkOptions[I].IndexOf('=') + 1); ;

						if (PakCryptoSettings.SecondaryEncryptionKeys != null)
						{
							foreach (EncryptionAndSigning.EncryptionKey Key in PakCryptoSettings.SecondaryEncryptionKeys)
							{
								if (string.Compare(Key.Guid, CD.RequestedEncryptionKeyGuid, true) == 0)
								{
									CD.EncryptionKeyGuid = CD.RequestedEncryptionKeyGuid;
									break;
								}
							}
						}
					}
				}
				CD.Manifest = ReadPakChunkManifest(ChunkManifestFilename);
				ChunkDefinitions.Add(CD);
			}

			const string OptionalBulkDataFileExtension = ".uptnl";
			Dictionary<string, ChunkDefinition> OptionalChunks = new Dictionary<string, ChunkDefinition>();
			ChunkDefinition DefaultChunk = ChunkDefinitions[DefaultChunkIndex];

			Dictionary<string, List<ChunkDefinition>> FileNameToChunks = new Dictionary<string, List<ChunkDefinition>>();
			foreach (ChunkDefinition Chunk in ChunkDefinitions)
			{
				foreach (string FileName in Chunk.Manifest)
				{
					List<ChunkDefinition> Chunks;
					if (!FileNameToChunks.TryGetValue(FileName, out Chunks))
					{
						Chunks = new List<ChunkDefinition>();
						FileNameToChunks.Add(FileName, Chunks);
					}
					Chunks.Add(Chunk);
				}
			}

			Dictionary<string, ChunkDefinition> ChunkNameToDefinition = new Dictionary<string, ChunkDefinition>();
			foreach (ChunkDefinition Chunk in ChunkDefinitions)
			{
				ChunkNameToDefinition.Add(Chunk.ChunkName, Chunk);
			}

			foreach (var StagingFile in StagingManifestResponseFile)
			{
				bool bAddedToChunk = false;
				bool bExcludeFromPaks = false;
				HashSet<ChunkDefinition> PakList = new HashSet<ChunkDefinition>();

				string OriginalFilename = StagingFile.Key;
				string NoExtension = CombinePaths(Path.GetDirectoryName(OriginalFilename), Path.GetFileNameWithoutExtension(OriginalFilename));
				if(Path.GetExtension(NoExtension) == ".m")
				{
					// Hack around .m.ubulk files having a double extension
					NoExtension = CombinePaths(Path.GetDirectoryName(OriginalFilename), Path.GetFileNameWithoutExtension(NoExtension));
				}
				string OriginalReplaceSlashes = OriginalFilename.Replace('/', '\\');
				string NoExtensionReplaceSlashes = NoExtension.Replace('/', '\\');

				// First read manifest
				List<ChunkDefinition> Chunks;
				if (FileNameToChunks.TryGetValue(OriginalFilename, out Chunks))
				{
					PakList.UnionWith(Chunks);
				}
				if (FileNameToChunks.TryGetValue(OriginalReplaceSlashes, out Chunks))
				{
					PakList.UnionWith(Chunks);
				}
				if (FileNameToChunks.TryGetValue(NoExtension, out Chunks))
				{
					PakList.UnionWith(Chunks);
				}
				if (FileNameToChunks.TryGetValue(NoExtensionReplaceSlashes, out Chunks))
				{
					PakList.UnionWith(Chunks);
				}

				// Now run through the pak rules which may override things
				ApplyPakFileRules(PakRulesList, StagingFile, PakList, ChunkNameToDefinition, out bExcludeFromPaks);

				if (bExcludeFromPaks)
				{
					continue;
				}

				// Actually add to chunk
				foreach (ChunkDefinition Chunk in PakList)
				{
					ChunkDefinition TargetChunk = Chunk;

					string OrigExt = Path.GetExtension(OriginalFilename);
					if (OrigExt.Equals(OptionalBulkDataFileExtension))
					{
						// any optional files encountered we want to put in a separate pak file
						string OptionalChunkName = Chunk.ChunkName + "optional";
						if (!OptionalChunks.TryGetValue(OptionalChunkName, out TargetChunk))
						{
							TargetChunk = new ChunkDefinition(OptionalChunkName);
							TargetChunk.RequestedEncryptionKeyGuid = Chunk.RequestedEncryptionKeyGuid;
							TargetChunk.EncryptionKeyGuid = Chunk.EncryptionKeyGuid;
							OptionalChunks.Add(OptionalChunkName, TargetChunk);
						}
					}

					TargetChunk.ResponseFile.Add(StagingFile.Key, StagingFile.Value);
					bAddedToChunk = true;

					if (bForceOneChunkPerFile)
					{
						// Files are only allowed to be in a single chunk
						break;
					}
				}
				if (!bAddedToChunk)
				{
					//LogInformation("No chunk assigned found for {0}. Using default chunk.", StagingFile.Key);
					DefaultChunk.ResponseFile.Add(StagingFile.Key, StagingFile.Value);
				}
			}

			foreach (var OptionalChunkIt in OptionalChunks)
			{
				ChunkDefinitions.Add(OptionalChunkIt.Value);
			}
		}

		// chunk downloader pak file is a minimal pak file which contains no content.  It can be provided with as a minimal download so that the game can download all the content from another source.
		// Deprecated in favor of DefaultPakFileRules.ini
		if (bShouldGenerateEarlyDownloaderPakFile)
		{
			string EarlyChunkName = EarlyDownloaderPakFilePrefix;
			ChunkDefinition EarlyChunk = new ChunkDefinition(EarlyChunkName);

			EarlyChunk.bCompressed = true;

			Dictionary<string,string> EarlyPakFile = EarlyChunk.ResponseFile;

			// find the list of files to put in the early downloader pak file
			List<string> FilesInEarlyPakFile = new List<string>();
			PlatformGameConfig.GetArray("/Script/UnrealEd.ProjectPackagingSettings", "EarlyDownloaderPakFileFiles", out FilesInEarlyPakFile);

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

		System.Threading.Tasks.ParallelOptions Options = new System.Threading.Tasks.ParallelOptions();

		LogInformation("Creating Pak files utilizing {0} cores", Environment.ProcessorCount);
		Options.MaxDegreeOfParallelism = Environment.ProcessorCount;

		// Check for chunks that requested an encryption key that we don't have. This can happen for Epic contractors who don't have access to our keychains. Emit a warning though, just in case
		// this somehow happens when the keychain is available
		foreach (ChunkDefinition CD in ChunkDefinitions)
		{
			if (!string.IsNullOrEmpty(CD.RequestedEncryptionKeyGuid) && (CD.RequestedEncryptionKeyGuid != CD.EncryptionKeyGuid))
			{
				LogWarning("Chunk '" + CD.ChunkName + "' requested encryption key '" + CD.RequestedEncryptionKeyGuid + "' but it couldn't be found in the project keychain.");
			}
		}

		List<CreatePakParams> PakInputs = new List<CreatePakParams>();
		foreach (ChunkDefinition Chunk in ChunkDefinitions)
		{
			if (Chunk.ResponseFile.Count > 0)
			{
				PakInputs.Add(new CreatePakParams(Chunk.ChunkName, Chunk.ResponseFile, Params.Compressed || Chunk.bCompressed, Chunk.EncryptionKeyGuid));
				//CreatePak(Params, SC, Chunk.ResponseFile, ChunkName, PakCryptoSettings, CryptoKeysCacheFilename, bCompression);
			}
		}

		CreatePaks(Params, SC, PakInputs, PakCryptoSettings, CryptoKeysCacheFilename);

		String ChunkLayerFilename = CombinePaths(GetTmpPackagingPath(Params, SC), GetChunkPakLayerListName());
		String OutputChunkLayerFilename = Path.Combine(SC.ProjectRoot.FullName, "Build", SC.FinalCookPlatform, "ChunkLayerInfo", GetChunkPakLayerListName());
		Directory.CreateDirectory(Path.GetDirectoryName(OutputChunkLayerFilename));
		File.Copy(ChunkLayerFilename, OutputChunkLayerFilename, true);
	}

	private static void GenerateMasterChunkManifest(string Dir, string Version, string PlatformStr)
	{
		//Create the directory if it doesn't exist
		InternalUtils.SafeCreateDirectory(Dir);

		string FileName = CombinePaths(Dir, PlatformStr.ToLower() + ".manifestlist");
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
		if (Params.bUseExtraFlavor)
		{
			TmpPackagingPath = CombinePaths(TmpPackagingPath, "ExtraFlavor");
		}

		return TmpPackagingPath;
	}

	private static bool ShouldCreateIoStoreContainerFiles(ProjectParams Params, DeploymentContext SC)
	{
		if (Params.CookOnTheFly)
		{
			return false;
		}

		return Params.IoStore && !Params.SkipIoStore;
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

	protected static void CleanDirectoryExcludingPakFiles(DirectoryInfo StagingDirectory)
	{
		foreach (DirectoryInfo SubDir in StagingDirectory.EnumerateDirectories())
		{
			if (!SubDir.Name.Equals("Paks", StringComparison.OrdinalIgnoreCase))
			{
				CleanDirectoryExcludingPakFiles(SubDir);
				try { SubDir.Delete(); } catch { }
			}
		}

		foreach (System.IO.FileInfo File in StagingDirectory.EnumerateFiles())
		{
			try { File.Delete(); } catch { }
		}
	}

	public static void CleanStagingDirectory(ProjectParams Params, DeploymentContext SC)
	{
		if (Params.NoCleanStage)
		{
			LogInformation("Skipping clean of staging directory due to -NoCleanStage argument.");
		}
		else if (SC.Stage && !Params.SkipStage)
		{
			if (Params.SkipPak)
			{
				LogInformation("Cleaning Stage Directory (exluding PAK files): {0}", SC.StageDirectory.FullName);
				CleanDirectoryExcludingPakFiles(new DirectoryInfo(SC.StageDirectory.FullName));
			}
			else
			{
				LogInformation("Cleaning Stage Directory: {0}", SC.StageDirectory.FullName);
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
		}
		else
		{
			LogInformation("Cleaning PAK files in stage directory: {0}", SC.StageDirectory.FullName);
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
			if (SC.CrashReporterUFSFiles.Count > 0)
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

		string BaseManifestFileName = CombinePaths(CmdEnv.LogFolder, "FinalCopy" + (SC.DedicatedServer ? "_Server" : ""));
		DumpManifest(SC.FilesToStage.UFSFiles, BaseManifestFileName + "_UFSFiles.txt");

		if (!SC.Stage || Params.SkipStage)
		{
			return;
		}

		DumpManifest(SC.FilesToStage.NonUFSFiles, BaseManifestFileName + "_NonUFSFiles.txt");
		DumpManifest(SC.FilesToStage.NonUFSDebugFiles, BaseManifestFileName + "_NonUFSFilesDebug.txt");

		CopyUsingStagingManifest(Params, SC);

		var ThisPlatform = SC.StageTargetPlatform;
		ThisPlatform.PostStagingFileCopy(Params, SC);
	}

	private static DirectoryReference GetIntermediateCommandlineDir(DeploymentContext SC)
	{
		return DirectoryReference.Combine(SC.ProjectRoot, "Intermediate", "UAT", SC.FinalCookPlatform);
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

		LogInformation("Creating UE4CommandLine.txt");
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
		using (var Writer = File.CreateText(ObsoleteManifest))
		{
			foreach (StagedFileReference ObsoleteFile in ObsoleteFiles)
			{
				Writer.WriteLine(ObsoleteFile);
			}
		}
	}

	protected static void WriteDeltaManifest(ProjectParams Params, DeploymentContext SC, Dictionary<StagedFileReference, string> DeployedFiles, Dictionary<StagedFileReference, string> StagedFiles, string DeltaManifest)
	{
		HashSet<StagedFileReference> CRCFiles = SC.StageTargetPlatform.GetFilesForCRCCheck();
		List<string> DeltaFiles = new List<string>();
		foreach (KeyValuePair<StagedFileReference, string> File in StagedFiles)
		{
			bool bNeedsDeploy = true;
			if (DeployedFiles.ContainsKey(File.Key))
			{
				if (CRCFiles.Contains(File.Key))
				{
					bNeedsDeploy = (File.Value != DeployedFiles[File.Key]);
				}
				else
				{
					DateTime Staged = DateTime.Parse(File.Value);
					DateTime Deployed = DateTime.Parse(DeployedFiles[File.Key]);
					bNeedsDeploy = (Staged > Deployed);
				}
			}

			if (bNeedsDeploy)
			{
				DeltaFiles.Add(File.Key.Name);
			}
		}

		// add the manifest
		if (!DeltaManifest.Contains("NonUFS"))
		{
			DeltaFiles.Add(SC.GetNonUFSDeployedManifestFileName(null));
			DeltaFiles.Add(SC.GetUFSDeployedManifestFileName(null));
		}

		// TODO: determine files which need to be removed

		// write out to the deltamanifest.json
		FileReference ManifestFile = FileReference.Combine(SC.StageDirectory, DeltaManifest);
		FileReference.WriteAllLines(ManifestFile, DeltaFiles);
	}

	//@todo move this
	public static List<DeploymentContext> CreateDeploymentContext(ProjectParams Params, bool InDedicatedServer, bool DoCleanStage = false)
	{
		ParamList<string> ListToProcess = InDedicatedServer && (Params.Cook || Params.CookOnTheFly) ? Params.ServerCookedTargets : Params.ClientCookedTargets;
		var ConfigsToProcess = InDedicatedServer && (Params.Cook || Params.CookOnTheFly) ? Params.ServerConfigsToBuild : Params.ClientConfigsToBuild;

		List<Tuple<string, UnrealTargetConfiguration>> TargetAndConfigPairs = new List<Tuple<string, UnrealTargetConfiguration>>();

		foreach (var Target in ListToProcess)
		{
			// If we are staging a client and have been asked to include editor targets, we currently only want to
			// include a single Development editor target. Ideally, we should have shipping editor configs and then
			// just include the requested configs for all targets
			if (!InDedicatedServer && Params.EditorTargets.Contains(Target))
			{
				TargetAndConfigPairs.Add(new Tuple<string, UnrealTargetConfiguration>(Target, UnrealTargetConfiguration.Development));
			}
			else
			{
				foreach (var Config in ConfigsToProcess)
				{
					TargetAndConfigPairs.Add(new Tuple<string, UnrealTargetConfiguration>(Target, Config));
				}
			}
		}

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
			foreach (var TargetAndConfig in TargetAndConfigPairs)
			{
				string Target = TargetAndConfig.Item1;
				UnrealTargetConfiguration Config = TargetAndConfig.Item2;
				string Exe = Target;
				if (Config != UnrealTargetConfiguration.Development)
				{
					Exe = Target + "-" + PlatformName + "-" + Config.ToString() + StageArchitecture;
				}
				ExecutablesToStage.Add(Exe);
			}

			string StageDirectory = ((ShouldCreatePak(Params) || (Params.Stage)) || !String.IsNullOrEmpty(Params.StageDirectoryParam)) ? Params.BaseStageDirectory : "";
			string ArchiveDirectory = (Params.Archive || !String.IsNullOrEmpty(Params.ArchiveDirectoryParam)) ? Params.BaseArchiveDirectory : "";
			DirectoryReference EngineDir = DirectoryReference.Combine(CommandUtils.RootDirectory, "Engine");
			DirectoryReference ProjectDir = DirectoryReference.FromFile(Params.RawProjectPath);

			List<StageTarget> TargetsToStage = new List<StageTarget>();
			foreach (var TargetAndConfig in TargetAndConfigPairs)
			{
				string Target = TargetAndConfig.Item1;
				UnrealTargetConfiguration Config = TargetAndConfig.Item2;
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
							throw new AutomationException(ExitCode.Error_MissingExecutable, "Stage Failed. Missing receipt '{0}'. Check that this target has been built.", ReceiptFileName);
						}
						else
						{
							// if it's multiple platforms, then allow missing receipts
							continue;
						}

					}

					// Read the receipt for this target
					TargetReceipt Receipt;
					if (!TargetReceipt.TryRead(ReceiptFileName, out Receipt))
					{
						throw new AutomationException("Missing or invalid target receipt ({0})", ReceiptFileName);
					}

					// Convert the paths to absolute
					TargetsToStage.Add(new StageTarget { Receipt = Receipt, RequireFilesExist = bRequireStagedFilesToExist });
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

			LogInformation("********** STAGE COMMAND STARTED **********");

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
					ApplyStagingManifest(Params, SC);

					if (Params.Deploy)
					{
						List<string> UFSManifests;
						List<string> NonUFSManifests;

						// get the staged file data
						Dictionary<StagedFileReference, string> StagedUFSFiles = ReadStagedManifest(Params, SC, SC.GetUFSDeployedManifestFileName(null));
						Dictionary<StagedFileReference, string> StagedNonUFSFiles = ReadStagedManifest(Params, SC, SC.GetNonUFSDeployedManifestFileName(null));

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

					if (Params.bCodeSign && !Params.SkipStage)
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
			LogInformation("********** STAGE COMMAND COMPLETED **********");
		}
	}
}
