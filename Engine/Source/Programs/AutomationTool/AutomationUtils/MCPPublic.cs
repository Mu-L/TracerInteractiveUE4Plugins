// Copyright Epic Games, Inc. All Rights Reserved.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.IO;
using AutomationTool;
using System.Runtime.Serialization;
using System.Net;
using System.Reflection;
using System.Text.RegularExpressions;
using UnrealBuildTool;
using EpicGames.MCP.Automation;

namespace EpicGames.MCP.Automation
{
	using EpicGames.MCP.Config;
	using System.Threading.Tasks;
	using Tools.DotNETCommon;

	public static class Extensions
	{
		public static Type[] SafeGetLoadedTypes(this Assembly Dll)
		{
			Type[] AllTypes;
			try
			{
				AllTypes = Dll.GetTypes();
			}
			catch (ReflectionTypeLoadException e)
			{
				AllTypes = e.Types.Where(x => x != null).ToArray();
			}
			return AllTypes;
		}
	}

    /// <summary>
    /// Utility class to provide commit/rollback functionality via an RAII-like functionality.
    /// Usage is to provide a rollback action that will be called on Dispose if the Commit() method is not called.
    /// This is expected to be used from within a using() ... clause.
    /// </summary>
    public class CommitRollbackTransaction : IDisposable
    {
        /// <summary>
        /// Track whether the transaction will be committed.
        /// </summary>
        private bool IsCommitted = false;

        /// <summary>
        /// 
        /// </summary>
        private System.Action RollbackAction;

        /// <summary>
        /// Call when you want to commit your transaction. Ensures the Rollback action is not called on Dispose().
        /// </summary>
        public void Commit()
        {
            IsCommitted = true;
        }

        /// <summary>
        /// Constructor
        /// </summary>
        /// <param name="RollbackAction">Action to be executed to rollback the transaction.</param>
        public CommitRollbackTransaction(System.Action InRollbackAction)
        {
            RollbackAction = InRollbackAction;
        }

        /// <summary>
        /// Rollback the transaction if its not committed on Dispose.
        /// </summary>
        public void Dispose()
        {
            if (!IsCommitted)
            {
                RollbackAction();
            }
        }
    }

	/// <summary>
	/// Enum that defines the MCP backend-compatible platform
	/// </summary>
	public enum MCPPlatform
	{
		/// <summary>
		/// MCP uses Windows for Win64
		/// </summary>
		Windows,

		/// <summary>
		/// 32 bit Windows
		/// </summary>
		Win32,

		/// <summary>
		/// Mac platform.
		/// </summary>
		Mac,

		/// <summary>
		/// Linux platform.
		/// </summary>
		Linux,

		/// <summary>
		/// IOS platform.
		/// </summary>
		IOS,

		/// <summary>
		/// Android platform.
		/// </summary>
		Android,

		/// <summary>
		/// WindowsCN Platform.
		/// </summary>
		WindowsCN,

		/// <summary>
		/// IOSCN Platform.
		/// </summary>
		IOSCN,

		/// <summary>
		/// AndroidCN Platform.
		/// </summary>
		AndroidCN,

		/// <summary>
		/// PS4 platform
		/// </summary>
		PS4,

		/// <summary>
		/// PS5 platform
		/// </summary>
		PS5,

		/// <summary>
		/// Switch platform
		/// </summary>
		Switch,

		/// <summary>
		/// Xbox One Platform
		/// </summary>
		XboxOne,

		/// <summary>
		/// Xbox One with GDK Platform
		/// </summary>
		XboxOneGDK,

		/// <summary>
		/// XSX platform
		/// </summary>
		XSX,
	}

	/// <summary>
	/// Enum that defines CDN types
	/// </summary>
	public enum CDNType
    {
        /// <summary>
        /// Internal HTTP CDN server
        /// </summary>
        Internal,

        /// <summary>
        /// Production HTTP CDN server
        /// </summary>
        Production,
    }

    /// <summary>
    /// Class that holds common state used to control the BuildPatchTool build commands that chunk and create patching manifests and publish build info to the BuildInfoService.
    /// </summary>
    public class BuildPatchToolStagingInfo
    {
        /// <summary>
        /// The currently running command, used to get command line overrides
        /// </summary>
        public BuildCommand OwnerCommand;
        /// <summary>
        /// name of the app. Can't always use this to define the staging dir because some apps are not staged to a place that matches their AppName.
        /// </summary>
        public readonly string AppName;
        /// <summary>
        /// Usually the base name of the app. Used to get the MCP key from a branch dictionary. 
        /// </summary>
        public readonly string McpConfigKey;
        /// <summary>
        /// ID of the app (needed for the BuildPatchTool)
        /// </summary>
        public readonly int AppID;
        /// <summary>
        /// BuildVersion of the App we are staging.
        /// </summary>
        public readonly string BuildVersion;
		/// <summary>
		/// Metadata for the build consisting of arbitrary json data. Will be null if no metadata exists.
		/// </summary>
		public BuildMetadataBase Metadata;
        /// <summary>
        /// Directory where builds will be staged. Rooted at the BuildRootPath, using a subfolder passed in the ctor, 
        /// and using BuildVersion/PlatformName to give each builds their own home.
        /// </summary>
        public readonly string StagingDir;
        /// <summary>
        /// Path to the CloudDir where chunks will be written (relative to the BuildRootPath)
        /// This is used to copy to the web server, so it can use the same relative path to the root build directory.
        /// This allows file to be either copied from the local file system or the webserver using the same relative paths.
        /// </summary>
        public readonly string CloudDirRelativePath;
        /// <summary>
        /// full path to the CloudDir where chunks and manifests should be staged. Rooted at the BuildRootPath, using a subfolder pass in the ctor.
        /// </summary>
        public readonly string CloudDir;
        /// <summary>
        /// Platform we are staging for.
        /// </summary>
        public readonly MCPPlatform Platform;

        /// <summary>
        /// Gets the base filename of the manifest that would be created by invoking the BuildPatchTool with the given parameters.
		/// Note that unless ManifestFilename was provided when constructing this instance, it is strongly recommended to call RetrieveManifestFilename()
		/// for existing builds to ensure that the correct filename is used.
		/// The legacy behavior of constructing of a manifest filename from appname, buildversion and platform should be considered unreliable, and will emit a warning.
        /// </summary>
        public virtual string ManifestFilename
        {
            get
            {
				if (!string.IsNullOrEmpty(_ManifestFilename))
				{
					return _ManifestFilename;
				}

				CommandUtils.LogInformation("Using legacy behavior of constructing manifest filename from appname, build version and platform. Update your code to specify manifest filename when constructing BuildPatchToolStagingInfo or call RetrieveManifestFilename to query it.");
				var BaseFilename = string.Format("{0}{1}-{2}.manifest",
					AppName,
					BuildVersion,
					Platform.ToString());
                return Regex.Replace(BaseFilename, @"\s+", ""); // Strip out whitespace in order to be compatible with BuildPatchTool
            }
        }

        /// <summary>
		/// If set, this allows us to over-ride the automatically constructed ManifestFilename
		/// </summary>
		protected string _ManifestFilename;

        /// <summary>
		/// Determine the platform name
        /// </summary>
        static public MCPPlatform ToMCPPlatform(UnrealTargetPlatform TargetPlatform)
        {
			if (TargetPlatform == UnrealTargetPlatform.Win64)
			{
				return MCPPlatform.Windows;
			}
			else if (TargetPlatform == UnrealTargetPlatform.Win32)
			{
				return MCPPlatform.Win32;
			}
			else if (TargetPlatform == UnrealTargetPlatform.Mac)
			{
				return MCPPlatform.Mac;
			}
			else if (TargetPlatform == UnrealTargetPlatform.Linux)
			{
				return MCPPlatform.Linux;
			}
			else if (TargetPlatform == UnrealTargetPlatform.IOS)
			{
				return MCPPlatform.IOS;
			}
			else if (TargetPlatform == UnrealTargetPlatform.Android)
			{
				return MCPPlatform.Android;
			}
			else if (TargetPlatform == UnrealTargetPlatform.PS4)
			{
				return MCPPlatform.PS4;
			}
			else if (TargetPlatform.ToString() == "PS5")
			{
				return MCPPlatform.PS5;
			}
			else if (TargetPlatform == UnrealTargetPlatform.XboxOne)
			{
				return MCPPlatform.XboxOne;
			}
			else if (TargetPlatform.ToString() == "XboxOneGDK")
			{
				return MCPPlatform.XboxOneGDK;
			}
			else if (TargetPlatform.ToString() == "XSX")
			{
				return MCPPlatform.XSX;
			}
			else if (TargetPlatform == UnrealTargetPlatform.Switch)
			{
				return MCPPlatform.Switch;
			}
			throw new AutomationException("Platform {0} is not properly supported by the MCP backend yet", TargetPlatform);
        }

        /// <summary>
		/// Determine the platform name
        /// </summary>
        static public UnrealTargetPlatform FromMCPPlatform(MCPPlatform TargetPlatform)
        {
			if (TargetPlatform == MCPPlatform.Windows)
			{
				return UnrealTargetPlatform.Win64;
			}
			else if (TargetPlatform == MCPPlatform.Win32)
			{
				return UnrealTargetPlatform.Win32;
			}
			else if (TargetPlatform == MCPPlatform.Mac)
			{
				return UnrealTargetPlatform.Mac;
			}
			else if (TargetPlatform == MCPPlatform.Linux)
			{
				return UnrealTargetPlatform.Linux;
			}
			else if (TargetPlatform == MCPPlatform.IOS)
			{
				return UnrealTargetPlatform.IOS;
			}
			else if (TargetPlatform == MCPPlatform.Android)
			{
				return UnrealTargetPlatform.Android;
			}
			else if (TargetPlatform == MCPPlatform.PS4)
			{
				return UnrealTargetPlatform.PS4;
			}
			else if (TargetPlatform == MCPPlatform.XboxOne)
			{
				return UnrealTargetPlatform.XboxOne;
			}
			else if (TargetPlatform == MCPPlatform.Switch)
			{
				return UnrealTargetPlatform.Switch;
			}
			else if (TargetPlatform == MCPPlatform.XboxOneGDK)
			{
				UnrealTargetPlatform ReturnValue;
				UnrealTargetPlatform.TryParse("XboxOneGDK", out ReturnValue);
				return ReturnValue;
			}
			else if (TargetPlatform == MCPPlatform.XSX)
			{
				UnrealTargetPlatform ReturnValue;
				UnrealTargetPlatform.TryParse("XSX", out ReturnValue);
				return ReturnValue;
			}
			else if (TargetPlatform == MCPPlatform.PS5)
			{
				UnrealTargetPlatform ReturnValue;
				UnrealTargetPlatform.TryParse("PS5", out ReturnValue);
				return ReturnValue;
			}
			throw new AutomationException("Platform {0} is not properly supported by the MCP backend yet", TargetPlatform);
        }

        /// <summary>
        /// Returns the build root path (P:\Builds on build machines usually)
        /// </summary>
        /// <returns></returns>
        static public string GetBuildRootPath()
        {
            return CommandUtils.P4Enabled && CommandUtils.AllowSubmit
                ? CommandUtils.RootBuildStorageDirectory()
                : CommandUtils.CombinePaths(CommandUtils.CmdEnv.LocalRoot, "LocalBuilds");
        }

        /// <summary>
        /// Basic constructor. 
        /// </summary>
        /// <param name="InAppName"></param>
        /// <param name="InAppID"></param>
        /// <param name="InBuildVersion"></param>
        /// <param name="platform"></param>
        /// <param name="stagingDirRelativePath">Relative path from the BuildRootPath where files will be staged. Commonly matches the AppName.</param>
		public BuildPatchToolStagingInfo(BuildCommand InOwnerCommand, string InAppName, string InMcpConfigKey, int InAppID, string InBuildVersion, MCPPlatform platform, string stagingDirRelativePath)
        {
            OwnerCommand = InOwnerCommand;
            AppName = InAppName;
			_ManifestFilename = null;
            McpConfigKey = InMcpConfigKey;
            AppID = InAppID;
            BuildVersion = InBuildVersion;
            Platform = platform;
			string BuildRootPath = GetBuildRootPath();
            StagingDir = CommandUtils.CombinePaths(BuildRootPath, stagingDirRelativePath, BuildVersion, Platform.ToString());
            CloudDirRelativePath = CommandUtils.CombinePaths(stagingDirRelativePath, "CloudDir");
            CloudDir = CommandUtils.CombinePaths(BuildRootPath, CloudDirRelativePath);
			Metadata = null;
        }

		/// <summary>
		/// Basic constructor with staging dir suffix override, basically to avoid having platform concatenated
		/// </summary>
		public BuildPatchToolStagingInfo(BuildCommand InOwnerCommand, string InAppName, string InMcpConfigKey, int InAppID, string InBuildVersion, UnrealTargetPlatform InPlatform, string StagingDirRelativePath, string StagingDirSuffix, string InManifestFilename)
			: this(InOwnerCommand, InAppName, InMcpConfigKey, InAppID, InBuildVersion, ToMCPPlatform(InPlatform), StagingDirRelativePath, StagingDirSuffix, InManifestFilename)
		{
		}

		/// <summary>
		/// Basic constructor with staging dir suffix override, basically to avoid having platform concatenated
		/// </summary>
		/// <param name="InOwnerCommand">The automation tool BuildCommand that is currently executing.</param>
		/// <param name="InAppName">The name of the app we're working with</param>
		/// <param name="InMcpConfigKey">An identifier for the back-end environment to allow for test deployments, QA, production etc.</param>
		/// <param name="InAppID">An identifier for the app. This is deprecated, and can safely be set to zero for all apps.</param>
		/// <param name="InBuildVersion">The build version for this build.</param>
		/// <param name="InPlatform">The platform the build will be deployed to.</param>
		/// <param name="StagingDirRelativePath">Relative path from the BuildRootPath where files will be staged. Commonly matches the AppName.</param>
		/// <param name="StagingDirSuffix">By default, we assumed source builds are at build_root/stagingdirrelativepath/buildversion. If they're in a subfolder of this path, specify it here.</param>
		/// <param name="InManifestFilename">If specified, will override the value returned by the ManifestFilename property</param>
		public BuildPatchToolStagingInfo(BuildCommand InOwnerCommand, string InAppName, string InMcpConfigKey, int InAppID, string InBuildVersion, MCPPlatform InPlatform, string StagingDirRelativePath, string StagingDirSuffix, string InManifestFilename)
		{
			OwnerCommand = InOwnerCommand;
			AppName = InAppName;
			McpConfigKey = InMcpConfigKey;
			AppID = InAppID;
			BuildVersion = InBuildVersion;
			Platform = InPlatform;
			_ManifestFilename = InManifestFilename;

			string BuildRootPath = GetBuildRootPath();
			StagingDir = CommandUtils.CombinePaths(BuildRootPath, StagingDirRelativePath, BuildVersion, StagingDirSuffix);
			CloudDirRelativePath = CommandUtils.CombinePaths(StagingDirRelativePath, "CloudDir");
			CloudDir = CommandUtils.CombinePaths(BuildRootPath, CloudDirRelativePath);
			Metadata = null;
		}

		/// <summary>
		/// Constructor which supports being able to just simply call BuildPatchToolBase.Get().Execute
		/// </summary>
		public BuildPatchToolStagingInfo(BuildCommand InOwnerCommand, string InAppName, int InAppID, string InBuildVersion, MCPPlatform InPlatform, string InCloudDir)
		{
			OwnerCommand = InOwnerCommand;
			AppName = InAppName;
			AppID = InAppID;
			BuildVersion = InBuildVersion;
			Platform = InPlatform;
			CloudDir = InCloudDir;
			Metadata = null;
		}

		/// <summary>
		/// Constructor which sets all values directly, without assuming any default paths.
		/// </summary>
		public BuildPatchToolStagingInfo(BuildCommand InOwnerCommand, string InAppName, int InAppID, string InBuildVersion, MCPPlatform InPlatform, DirectoryReference InStagingDir, DirectoryReference InCloudDir, string InManifestFilename = null)
		{
			OwnerCommand = InOwnerCommand;
			AppName = InAppName;
			AppID = InAppID;
			BuildVersion = InBuildVersion;
			Platform = InPlatform;
			Metadata = null;
			if (InStagingDir != null)
			{
				StagingDir = InStagingDir.FullName;
			}
			if (InCloudDir != null)
			{
				DirectoryReference BuildRootDir = new DirectoryReference(GetBuildRootPath());
				if(!InCloudDir.IsUnderDirectory(BuildRootDir))
				{
					throw new AutomationException("Cloud directory must be under build root path ({0})", BuildRootDir.FullName);
				}
				CloudDir = InCloudDir.FullName;
				CloudDirRelativePath = InCloudDir.MakeRelativeTo(BuildRootDir).Replace('\\', '/');
			}
			if (!string.IsNullOrEmpty(InManifestFilename))
			{
				_ManifestFilename = InManifestFilename;
			}
		}

		/// <summary>
		/// Associates a piece of metadata (in the form of a key value pair) with the build info.
		/// </summary>
		/// <param name="Key">The key to store the metadata against.</param>
		/// <param name="Value">The value of the metadata to associate.</param>
		/// <param name="bClobber">Optional, specifies whether to overwrite the existing key if it exists, default true.</param>
		/// <returns>The BuildPatchToolStagingInfo to facilitate fluent syntax.</returns>
		public BuildPatchToolStagingInfo WithMetadata(string Key, string Value, bool bClobber = true)
		{
			BuildMetadataBase NewMeta = BuildInfoPublisherBase.Get().CreateBuildMetadata(string.Format(@"{{""{0}"":""{1}""}}", Key, Value));
			return WithMetadata(NewMeta, bClobber);
		}

		/// <summary>
		/// Associates new metadata with the build info.
		/// </summary>
		/// <param name="NewMetadata">The metadata object to merge in.</param>
		/// <param name="bClobber">Optional, specifies whether to overwrite the existing key if it exists, default true.</param>
		/// <returns>The BuildPatchToolStagingInfo to facilitate fluent syntax.</returns>
		public BuildPatchToolStagingInfo WithMetadata(BuildMetadataBase NewMetadata, bool bClobber = true)
		{
			if (Metadata != null)
			{
				Metadata.MergeWith(NewMetadata, bClobber);
			}
			else
			{
				Metadata = NewMetadata;
			}
			return this;
		}

		/// <summary>
		/// Returns the manifest filename, querying build info for it if necessary.
		/// If ManifestFilename has already set (either during construction or by a previous call to this method) the cached value will be returned unless bForce is specified.
		/// Otherwise, a query will be made to the specified build info service to retrieve the correct manifest filename.
		/// </summary>
		/// <param name="McpConfigName">Name of which MCP config to check against.</param>
		/// <param name="bForce">If specified, a call will be made to build info even if we have a locally cached version.</param>
		/// <returns>The manifest filename</returns>
		public string RetrieveManifestFilename(string McpConfigName, bool bForce = false)
		{
			if (bForce || string.IsNullOrEmpty(_ManifestFilename))
			{
				BuildInfoPublisherBase BI = BuildInfoPublisherBase.Get();
				string ManifestUrl = BI.GetBuildManifestUrl(this, McpConfigName);
				if (string.IsNullOrEmpty(ManifestUrl))
				{
					throw new AutomationException("Could not determine manifest Url for {0} version {1} from {2} environment.", this.AppName, this.BuildVersion, McpConfigName);
				}
				_ManifestFilename = ManifestUrl.Split(new char[] { '/', '\\' }).Last();
			}

			return _ManifestFilename;
		}

		/// <summary>
		/// Adds and returns the metadata for this build, querying build info for any additional fields.
		/// </summary>
		/// <param name="McpConfigName">Optional, name of which MCP config to check against, default null which will use the one stored in this BuildPatchToolStagingInfo.</param>
		/// <param name="bClobber">Optional, specifies whether to overwrite existing metadata keys with those received, default false.</param>
		/// <returns>The Metadata dictionary</returns>
		public BuildMetadataBase RetrieveBuildMetadata(string McpConfigName = null, bool bClobber = false)
		{
			BuildInfoPublisherBase BI = BuildInfoPublisherBase.Get();
			return BI.GetBuildMetaData(this, McpConfigName ?? McpConfigKey, bClobber);
		}
	}

	/// <summary>
	/// Class that provides programmatic access to the BuildPatchTool
	/// </summary>
	public abstract class BuildPatchToolBase
	{
		/// <summary>
		/// Controls which version of BPT to use when executing.
		/// </summary>
		public enum ToolVersion
		{
			/// <summary>
			/// The current live, tested build.
			/// </summary>
			Live,
			/// <summary>
			/// The latest published build, may be untested.
			/// </summary>
			Next,
			/// <summary>
			/// An experimental build, for use when one project needs early access or unique changes.
			/// </summary>
			Experimental,
			/// <summary>
			/// Use local build from source of BuildPatchTool.
			/// </summary>
			Source
		}

		/// <summary>
		/// An interface which provides the required Perforce access implementations
		/// </summary>
		public interface IPerforce
		{
			/// <summary>
			/// Property to say whether Perforce access is enabled in the environment.
			/// </summary>
			bool bIsEnabled { get; }

			/// <summary>
			/// Check a file exists in Perforce.
			/// </summary>
			/// <param name="Filename">Filename to check.</param>
			/// <returns>True if the file exists in P4.</returns>
			bool FileExists(string Filename);

			/// <summary>
			/// Gets the contents of a particular file in the depot and writes it to a local file without syncing it.
			/// </summary>
			/// <param name="DepotPath">Depot path to the file (with revision/range if necessary).</param>
			/// <param name="Filename">Output file to write to.</param>
			/// <returns>True if successful.</returns>
			bool PrintToFile(string DepotPath, string Filename);

			/// <summary>
			/// Retrieve the latest CL number for the given file.
			/// </summary>
			/// <param name="Filename">The filename for the file to check.</param>
			/// <param name="ChangeList">Receives the CL number.</param>
			/// <returns>True if the file exists and ChangeList was set.</returns>
			bool GetLatestChange(string Filename, out int ChangeList);
		}

		public class PatchGenerationOptions
		{
			/// <summary>
			/// By default, we will only consider data referenced from manifests modified within five days to be reusable.
			/// </summary>
			private const int DEFAULT_DATA_AGE_THRESHOLD = 5;

			public PatchGenerationOptions()
			{
				DataAgeThreshold = DEFAULT_DATA_AGE_THRESHOLD;
				ChunkWindowSize = 1048576;
			}

			/// <summary>
			/// A unique integer for this product.
			/// Deprecated. Can be safely left as 0.
			/// </summary>
			public int AppId;
			/// <summary>
			/// The app name for this build, which will be embedded inside the generated manifest to identify the application.
			/// </summary>
			public string AppName;
			/// <summary>
			/// The build version being generated.
			/// </summary>
			public string BuildVersion;
			/// <summary>
			/// Used as part of the build version string.
			/// </summary>
			public MCPPlatform Platform;
			/// <summary>
			/// The directory containing the build image to be read.
			/// </summary>
			public string BuildRoot;
			/// <summary>
			/// The directory which will receive the generated manifest and chunks.
			/// </summary>
			public string CloudDir;
			/// <summary>
			/// The name of the manifest file that will be produced.
			/// </summary>
			public string ManifestFilename;
			/// <summary>
			/// A path to a text file containing BuildRoot relative files to be included in the build.
			/// </summary>
			public string FileInputList;
			/// <summary>
			/// A path to a text file containing BuildRoot relative files to be excluded from the build.
			/// </summary>
			public string FileIgnoreList;
			/// <summary>
			/// A path to a text file containing quoted BuildRoot relative files followed by optional attributes such as readonly compressed executable tag:mytag, separated by \r\n line endings.
			/// These attribute will be applied when build is installed client side.
			/// </summary>
			public string FileAttributeList;
			/// <summary>
			/// The path to the app executable, must be relative to, and inside of BuildRoot.
			/// </summary>
			public string AppLaunchCmd;
			/// <summary>
			/// The commandline to send to the app on launch.
			/// </summary>
			public string AppLaunchCmdArgs;
			/// <summary>
			/// The list of prerequisite Ids that this prerequisite installer satisfies.
			/// </summary>
			public List<string> PrereqIds;
			/// <summary>
			/// The prerequisites installer to launch on successful product install, must be relative to, and inside of BuildRoot.
			/// </summary>
			public string PrereqPath;
			/// <summary>
			/// The commandline to send to prerequisites installer on launch.
			/// </summary>
			public string PrereqArgs;
			/// <summary>
			/// When identifying existing patch data to reuse in this build, only
			/// files referenced from a manifest file modified within this number of days will be considered for reuse.
			/// IMPORTANT: This should always be smaller than the minimum age at which manifest files can be deleted by any cleanup process, to ensure
			/// that we do not reuse any files which could be deleted by a concurrently running compactify. It is recommended that this number be at least
			/// two days less than the cleanup data age threshold.
			/// </summary>
			public int DataAgeThreshold;
			/// <summary>
			/// Specifies in bytes, the data window size that should be used when saving new chunks. Default is 1048576 (1MiB).
			/// </summary>
			public int ChunkWindowSize;
			/// <summary>
			/// Specifies the desired output FeatureLevel of BuildPatchTool, if this is not provided BPT will warn and default to LatestJson so that project scripts can be updated.
			/// </summary>
			public string FeatureLevel;
			/// <summary>
			/// Contains a list of custom string arguments to be embedded in the generated manifest file.
			/// </summary>
			public List<KeyValuePair<string, string>> CustomStringArgs;
			/// <summary>
			/// Contains a list of custom integer arguments to be embedded in the generated manifest file.
			/// </summary>
			public List<KeyValuePair<string, int>> CustomIntArgs;
			/// <summary>
			/// Contains a list of custom float arguments to be embedded in the generated manifest file.
			/// </summary>
			public List<KeyValuePair<string, float>> CustomFloatArgs;
		}

		/// <summary>
		/// Represents the options passed to the compactify process
		/// </summary>
		public class CompactifyOptions
		{
			private const int DEFAULT_DATA_AGE_THRESHOLD = 2;

			public CompactifyOptions()
			{
				DataAgeThreshold = DEFAULT_DATA_AGE_THRESHOLD;
			}

			/// <summary>
			/// BuildPatchTool will run a compactify on this directory.
			/// </summary>
			public string CompactifyDirectory;
			/// <summary>
			/// Corresponds to the -preview parameter
			/// </summary>
			public bool bPreviewCompactify;
			/// <summary>
			/// Patch data files modified within this number of days will *not* be deleted, to ensure that any patch files being written out by a.
			/// patch generation process are not deleted before their corresponding manifest file(s) can be written out.
			/// NOTE: this should be set to a value larger than the expected maximum time that a build could take.
			/// </summary>
			public int DataAgeThreshold;
		}

		public class DataEnumerationOptions
		{
			/// <summary>
			/// The file path to the manifest to enumerate from.
			/// </summary>
			public string ManifestFile;
			/// <summary>
			/// The file path to where the list will be saved out, containing \r\n separated cloud relative file paths.
			/// </summary>
			public string OutputFile;
			/// <summary>
			/// When true, the output will include the size of each file on each line, separated by \t
			/// </summary>
			public bool bIncludeSize;
		}

		public class ManifestMergeOptions
		{
			/// <summary>
			/// The file path to the base manifest.
			/// </summary>
			public string ManifestA;
			/// <summary>
			/// The file path to the update manifest.
			/// </summary>
			public string ManifestB;
			/// <summary>
			/// The file path to the output manifest.
			/// </summary>
			public string ManifestC;
			/// <summary>
			/// The new version string for the build being produced.
			/// </summary>
			public string BuildVersion;
			/// <summary>
			/// Used as part of the build version string.
			/// </summary>
			public MCPPlatform Platform;
			/// <summary>
			/// Optional. The set of files that should be kept from ManifestA.
			/// </summary>
			public HashSet<string> FilesToKeepFromA;
			/// <summary>
			/// Optional. The set of files that should be kept from ManifestB.
			/// </summary>
			public HashSet<string> FilesToKeepFromB;
			
		}

		public class ManifestDiffOptions
		{
			/// <summary>
			/// The file path to the base manifest.
			/// </summary>
			public string ManifestA;
			/// <summary>
			/// The install tags to use for ManifestA.
			/// </summary
			public HashSet<string> InstallTagsA;
			/// <summary>
			/// The file path to the update manifest.
			/// </summary>
			public string ManifestB;
			/// <summary>
			/// The install tags to use for ManifestB.
			/// </summary>
			public HashSet<string> InstallTagsB;
			/// <summary>
			/// Tag sets to be compared between manifests 
			/// </summary>
			public List<HashSet<string>> CompareTagSets;
		}

		public class ManifestDiffOutput
		{
			public class ManifestSummary
			{
				/// <summary>
				/// The AppName field from the manifest file.
				/// </summary>
				public string AppName;
				/// <summary>
				/// The AppId field from the manifest file.
				/// </summary>
				public uint AppId;
				/// <summary>
				/// The VersionString field from the manifest file.
				/// </summary>
				public string VersionString;
				/// <summary>
				/// The total size of chunks in the build.
				/// </summary>
				public ulong DownloadSize;
				/// <summary>
				/// The total size of disk space required for the build.
				/// </summary>
				public ulong BuildSize;
				/// <summary>
				/// The list of download sizes for each individual install tag that was used.
				/// Note that the sum of these can be higher than the actual total due to possibility of shares files.
				/// </summary>
				public Dictionary<string, ulong> IndividualTagDownloadSizes;
				/// <summary>
				/// The list of download sizes for each tag set that was in the list to be analyzed.
				/// </summary>
				public Dictionary<string, ulong> CompareTagSetDownloadSizes;
				/// <summary>
				/// The list of build sizes for each individual install tag that was used.
				/// Note that the sum of these can be higher than the actual total due to possibility of shares files.
				/// </summary>
				public Dictionary<string, ulong> IndividualTagBuildSizes;
				/// <summary>
				/// The list of build sizes for each tag set that was in the list to be analyzed.
				/// </summary>
				public Dictionary<string, ulong> CompareTagSetBuildSizes;
			}
			public class ManifestDiff
			{
				/// <summary>
				/// The list of build relative paths for files which were added by the patch from ManifestA to ManifestB, subject to using the tags that were provided.
				/// </summary>
				public List<string> NewFilePaths;
				/// <summary>
				/// The list of build relative paths for files which were removed by the patch from ManifestA to ManifestB, subject to using the tags that were provided.
				/// </summary>
				public List<string> RemovedFilePaths;
				/// <summary>
				/// The list of build relative paths for files which were changed between ManifestA and ManifestB, subject to using the tags that were provided.
				/// </summary>
				public List<string> ChangedFilePaths;
				/// <summary>
				/// The list of build relative paths for files which were unchanged between ManifestA and ManifestB, subject to using the tags that were provided.
				/// </summary>
				public List<string> UnchangedFilePaths;
				/// <summary>
				/// The list of cloud directory relative paths for all new chunks required by the patch from ManifestA to ManifestB, subject to using the tags that were provided.
				/// </summary>
				public List<string> NewChunkPaths;
				/// <summary>
				/// The required download size for the patch from ManifestA to ManifestB, subject to using the tags that were provided.
				/// </summary>
				public ulong DeltaDownloadSize;
				/// <summary>
				/// The required disk space to apply the patch from ManifestA to ManifestB, subject to using the tags that were provided.
				/// </summary>
				public ulong TempDiskSpaceReq;
				/// <summary>
				/// The list of delta sizes for each individual install tag that was used.
				/// Note that the sum of these can be higher than the actual total due to possibility of shares files.
				/// </summary>
				public Dictionary<string, ulong> IndividualTagDeltaSizes;
				/// <summary>
				/// The list of delta sizes for each tag set that was in the list to be analyzed.
				/// </summary>
				public Dictionary<string, ulong> CompareTagSetDeltaSizes;
				/// <summary>
				/// The list of disk space requirements to apply the patch for each tag set that was in the list to be analyzed.
				/// </summary>
				public Dictionary<string, ulong> CompareTagSetTempDiskSpaceReqs;
				/// <summary>
				/// Install time coefficients represent an estimation for time to install the patch. These are not accurate timing representations, but are comparable between runs with different versions.
				/// They can be used to spot out of the ordinary time requirements for installing an update.
				/// The list if non-null will contain 6 entries as follows:
				///   InstallTimeCoefficients[0] - Low-Spec using DestructiveInstall.
				///   InstallTimeCoefficients[1] - Low-Spec using NonDestructiveInstall.
				///   InstallTimeCoefficients[2] - Mid-Spec using DestructiveInstall.
				///   InstallTimeCoefficients[3] - Mid-Spec using NonDestructiveInstall.
				///   InstallTimeCoefficients[4] - High-Spec using DestructiveInstall.
				///   InstallTimeCoefficients[5] - High-Spec using NonDestructiveInstall.
				/// Low-Spec was taken from 25 percentile as of July 2019.
				/// Mid-Spec was taken from 50 percentile as of July 2019.
				/// High-Spec was taken from 75 percentile as of July 2019.
				/// If the BPT version being used does not support this feature, or a problem occurred, InstallTimeCoefficients will be null.
				/// </summary>
				public List<float> InstallTimeCoefficients;
			}
			/// <summary>
			/// The manifest detail for the source build of the differential.
			/// </summary>
			public ManifestSummary ManifestA;
			/// <summary>
			/// The manifest detail for the target build of the differential.
			/// </summary>
			public ManifestSummary ManifestB;
			/// <summary>
			/// The differential details for the patch from ManifestA's build to ManifestB's build.
			/// </summary>
			public ManifestDiff Differential;
		}

		public class AutomationTestsOptions
		{
			/// <summary>
			/// Optionally specify the tests to run.
			/// </summary>
			public string TestList;
		}

		public class PackageChunksOptions
		{
			/// <summary>
			/// Specifies the file path to the manifest to enumerate chunks from.
			/// </summary>
			public string ManifestFile;
			/// <summary>
			/// Specifies the file path to a manifest for a previous build, this will be used to filter out chunks.
			/// </summary>
			public string PrevManifestFile;
			/// <summary>
			/// Specifies the file path to the output package.  An extension of .chunkdb will be added if not present.
			/// </summary>
			public string OutputFile;
			/// <summary>
			/// An optional parameter which, if present, specifies the directory where chunks to be packaged can be found.
			/// If not specified, the manifest file's location will be used as the cloud directory.
			/// </summary>
			public string CloudDir;
			/// <summary>
			/// Optional value, to restrict the maximum size of each output file (in bytes).
			/// If not specified, then only one output file will be produced, containing all the data.
			/// If specified, then the output files will be generated as Name.part01.chunkdb, Name.part02.chunkdb etc. The part number will have the number of digits
			/// required for highest numbered part.
			/// </summary>
			public ulong? MaxOutputFileSize;
			/// <summary>
			/// Optionally provide a tagset to filter the files available from PrevManifestFile. This may increase chunks saved into the chunkdb files, in order to serve data
			/// from files that are not expected to be available. Most of the time this should be the union of all tags in TagSetSplit, unless tagging is changed between the two manifests.
			/// An empty string must be included to include untagged files.
			/// Leaving this variable null will include all files.
			/// </summary
			public HashSet<string> PrevManifestTags;
			/// <summary>
			/// Optionally provide a list of tagsets to split chunkdb files on. First all data from the tagset at index 0 will be saved, then any extra data needed
			/// for tagset at index 1, and so on. Note that this means the chunkdb files produced for tagset at index 1 will not contain some required data for that tagset if
			/// the data already got saved out as part of tagset at index 0, and thus the chunkdb files are additive with no dupes.
			/// If it is desired that each tagset's chunkdb files contain the duplicate data, then PackageChunks should be executed once per tagset rather than once will all tagsets.
			/// An empty string must be included in one of the tagsets to include untagged file data in that tagset.
			/// Leaving this variable null will include data for all files.
			/// </summary>
			public List<HashSet<string>> TagSetSplit;
			/// <summary>
			/// Specifies the desired output FeatureLevel of BuildPatchTool, if this is not provided BPT will default to before optimised deltas.
			/// </summary>
			public string FeatureLevel;
		}

		public class PackageChunksOutput
		{
			/// <summary>
			/// The list of full filepaths of all created chunkdb files.
			/// </summary>
			public List<string> ChunkDbFilePaths;
			/// <summary>
			/// If PackageChunksOptions.TagSetSplit was provided, then this variable will contain a lookup table of TagSetSplit index to List of ChunkDbFilePaths indices.
			/// e.g.
			/// TagSetLookupTable[0] = [ 0, 1, 2, 3, ..., n ]
			/// TagSetLookupTable[1] = [] an empty List would mean that all data for this tagset was already included in the chunkdb(s) for previous tagset(s).
			/// TagSetLookupTable[2] = [ n+1, n+2, ..., n+m ]
			/// </summary>
			public List<List<int>> TagSetLookupTable;
		}

		public class ChunkDeltaOptimiseOptions
		{
			/// <summary>
			/// The file path to the base manifest.
			/// </summary>
			public string SourceManifest;
			/// <summary>
			/// The file path to the update manifest. New data will be added to the cloud directory that this manifest is in.
			/// </summary>
			public string DestinationManifest;
			/// <summary>
			/// Specifies in bytes, an upper limit for original diffs to try to enhance.
			/// </summary>
			public ulong DiffAbortThreshold;
		}

		static BuildPatchToolBase Handler = null;

		public static BuildPatchToolBase Get()
		{
			if (Handler == null)
			{
				Assembly[] LoadedAssemblies = AppDomain.CurrentDomain.GetAssemblies();
				foreach (var Dll in LoadedAssemblies)
				{
					Type[] AllTypes = Dll.SafeGetLoadedTypes();
					foreach (var PotentialConfigType in AllTypes)
					{
						if (PotentialConfigType != typeof(BuildPatchToolBase) && typeof(BuildPatchToolBase).IsAssignableFrom(PotentialConfigType))
						{
							Handler = Activator.CreateInstance(PotentialConfigType) as BuildPatchToolBase;
							break;
						}
					}
				}
				if (Handler == null)
				{
					throw new AutomationException("Attempt to use BuildPatchToolBase.Get() and it doesn't appear that there are any modules that implement this class.");
				}
			}
			return Handler;
		}

		/// <summary>
		/// Runs the Build Patch Tool executable to generate patch data using the supplied parameters.
		/// </summary>
		/// <param name="Opts">Parameters which will be passed to the Build Patch Tool generation process.</param>
		/// <param name="Version">Which version of BuildPatchTool is desired.</param>
		/// <param name="bAllowManifestClobbering">If set to true, will allow an existing manifest file to be overwritten with this execution. Default is false.</param>
		public abstract void Execute(PatchGenerationOptions Opts, ToolVersion Version = ToolVersion.Live, bool bAllowManifestClobbering = false);

		/// <summary>
		/// Runs the Build Patch Tool executable to compactify a cloud directory using the supplied parameters.
		/// </summary>
		/// <param name="Opts">Parameters which will be passed to the Build Patch Tool compactify process.</param>
		/// <param name="Version">Which version of BuildPatchTool is desired.</param>
		public abstract void Execute(CompactifyOptions Opts, ToolVersion Version = ToolVersion.Live);

		/// <summary>
		/// Runs the Build Patch Tool executable to enumerate patch data files referenced by a manifest using the supplied parameters.
		/// </summary>
		/// <param name="Opts">Parameters which will be passed to the Build Patch Tool enumeration process.</param>
		/// <param name="Version">Which version of BuildPatchTool is desired.</param>
		public abstract void Execute(DataEnumerationOptions Opts, ToolVersion Version = ToolVersion.Live);

		/// <summary>
		/// Runs the Build Patch Tool executable to merge two manifest files producing a hotfix manifest.
		/// </summary>
		/// <param name="Opts">Parameters which will be passed to the Build Patch Tool manifest merge process.</param>
		/// <param name="Version">Which version of BuildPatchTool is desired.</param>
		public abstract void Execute(ManifestMergeOptions Opts, ToolVersion Version = ToolVersion.Live);

		/// <summary>
		/// Runs the Build Patch Tool executable to diff two manifest files logging out details.
		/// </summary>
		/// <param name="Opts">Parameters which will be passed to the Build Patch Tool manifest diff process.</param>
		/// <param name="Output">Will receive the data back for the diff.</param>
		/// <param name="Version">Which version of BuildPatchTool is desired.</param>
		public abstract void Execute(ManifestDiffOptions Opts, out ManifestDiffOutput Output, ToolVersion Version = ToolVersion.Live);

		/// <summary>
		/// Runs the Build Patch Tool executable to evaluate built in automation testing.
		/// </summary>
		/// <param name="Opts">Parameters which will be passed to the Build Patch Tool automation tests process.</param>
		/// <param name="Version">Which version of BuildPatchTool is desired.</param>
		public abstract void Execute(AutomationTestsOptions Opts, ToolVersion Version = ToolVersion.Live);

		/// <summary>
		/// Runs the Build Patch Tool executable to create ChunkDB file(s) consisting of multiple chunks to allow installing / patching to a specific build.
		/// </summary>
		/// <param name="Opts">Parameters which will be passed to the Build Patch Tool package chunks process.</param>
		/// <param name="Output">Will receive the data back for the packaging.</param>
		/// <param name="Version">Which version of BuildPatchTool is desired.</param>
		public abstract void Execute(PackageChunksOptions Opts, out PackageChunksOutput Output, ToolVersion Version = ToolVersion.Live);

		/// <summary>
		/// Runs the Build Patch Tool executable to create optimised delta files which reduce download size for patching between two specific builds.
		/// </summary>
		/// <param name="Opts">Parameters which will be passed to the Build Patch Tool chunk delta optimise process.</param>
		/// <param name="Version">Which version of BuildPatchTool is desired.</param>
		public abstract void Execute(ChunkDeltaOptimiseOptions Opts, ToolVersion Version = ToolVersion.Live);
	}


	/// <summary>
	/// Class that provides programmatic access to the metadata field from build info.
	/// </summary>
	public abstract class BuildMetadataBase
	{
		/// <summary>
		/// Merges provided metadata object into this one.
		/// </summary>
		/// <param name="Other">The other metadata to merge in.</param>
		/// <param name="bClobber">Optional, specifies whether to overwrite existing metadata keys with those in Other, default true.</param>
		/// <returns>this object to facilitate fluent syntax.</returns>
		abstract public BuildMetadataBase MergeWith(BuildMetadataBase Other, bool bClobber = true);
	}

	/// <summary>
	/// Helper class
	/// </summary>
	public abstract class BuildInfoPublisherBase
	{
		static BuildInfoPublisherBase Handler = null;

		public static BuildInfoPublisherBase Get()
		{
			if (Handler == null)
			{
				Assembly[] LoadedAssemblies = AppDomain.CurrentDomain.GetAssemblies();
				foreach (var Dll in LoadedAssemblies)
				{
					Type[] AllTypes = Dll.SafeGetLoadedTypes();
					foreach (var PotentialConfigType in AllTypes)
					{
						if (PotentialConfigType != typeof(BuildInfoPublisherBase) && typeof(BuildInfoPublisherBase).IsAssignableFrom(PotentialConfigType))
						{
							Handler = Activator.CreateInstance(PotentialConfigType) as BuildInfoPublisherBase;
							break;
						}
					}
				}
				if (Handler == null)
				{
					throw new AutomationException("Attempt to use BuildInfoPublisherBase.Get() and it doesn't appear that there are any modules that implement this class.");
				}
			}
			return Handler;
		}

		/// <summary>
		/// Creates a metadata object implementation, initialized with the provided JSON object.
		/// </summary>
		/// <param name="JsonRepresentation">JSON object representation to initialize with.</param>
		/// <returns>new instance of metadata implementation.</returns>
		abstract public BuildMetadataBase CreateBuildMetadata(string JsonRepresentation);

		/// <summary>
		/// Determines whether a given build is registered in build info
		/// </summary>
		/// <param name="StagingInfo">The staging info representing the build to check.</param>
		/// <param name="McpConfigName">Name of which MCP config to check against.</param>
		/// <returns>true if the build is registered, false otherwise</returns>
		abstract public bool BuildExists(BuildPatchToolStagingInfo StagingInfo, string McpConfigName);

		/// <summary>
		/// Given a MCPStagingInfo defining our build info, posts the build to the MCP BuildInfo Service.
		/// </summary>
		/// <param name="stagingInfo">Staging Info describing the BuildInfo to post.</param>
		abstract public void PostBuildInfo(BuildPatchToolStagingInfo stagingInfo);

		/// <summary>
		/// Given a MCPStagingInfo defining our build info and a MCP config name, posts the build to the requested MCP BuildInfo Service.
		/// </summary>
		/// <param name="StagingInfo">Staging Info describing the BuildInfo to post.</param>
		/// <param name="McpConfigName">Name of which MCP config to post to.</param>
		abstract public void PostBuildInfo(BuildPatchToolStagingInfo StagingInfo, string McpConfigName);

		/// <summary>
		/// Sets a value in the key/value pair metadata associated with the specified build.
		/// </summary>
		/// <param name="StagingIfno">StagingInfo describing the build info to edit.</param>
		/// <param name="Key">The key for the metadata item.</param>
		/// <param name="Value">The value to associate with the key.</param>
		/// <param name="McpConfigName">Name of which MCP config to post to.</param>
		abstract public void SetMetadata(BuildPatchToolStagingInfo StagingInfo, string Key, string Value, string McpConfigName);

		/// <summary>
		/// Given a BuildVersion defining our a build, return the labels applied to that build
		/// </summary>
		/// <param name="BuildVersion">Build version to return labels for.</param>
		/// <param name="McpConfigName">Which BuildInfo backend to get labels from for this promotion attempt.</param>
		/// <returns>The list of build labels applied.</returns>
		abstract public List<string> GetBuildLabels(BuildPatchToolStagingInfo StagingInfo, string McpConfigName);

		/// <summary>
		/// Given a staging info defining our build, return the manifest url for that registered build
		/// </summary>
		/// <param name="StagingInfo">Staging Info describing the BuildInfo to query.</param>
		/// <param name="McpConfigName">Name of which MCP config to query.</param>
		/// <returns>The manifest url.</returns>
		abstract public string GetBuildManifestUrl(BuildPatchToolStagingInfo StagingInfo, string McpConfigName);

		/// <summary>
		/// Given a staging info defining our build, return the manifest url for that registered build
		/// </summary>
		/// <param name="AppName">Application name to check the label in</param>
		/// <param name="BuildVersion">Build version to manifest for.</param>
		/// <param name="McpConfigName">Name of which MCP config to query.</param>
		/// <returns></returns>
		abstract public string GetBuildManifestUrl(string AppName, string BuildVersionWithPlatform, string McpConfigName);

		/// <summary>
		/// Given a staging info defining our build, return the manifest hash for that registered build
		/// </summary>
		/// <param name="StagingInfo">Staging Info describing the BuildInfo to query.</param>
		/// <param name="McpConfigName">Name of which MCP config to query.</param>
		/// <returns>Manifest SHA1 hash as a hex string</returns>
		abstract public string GetBuildManifestHash(BuildPatchToolStagingInfo StagingInfo, string McpConfigName);

		/// <summary>
		/// Given a staging info defining our build, fetch and apply the metadata for that registered build.
		/// </summary>
		/// <param name="StagingInfo">Staging Info describing the BuildInfo to query.</param>
		/// <param name="McpConfigName">Name of which MCP config to query.</param>
		/// <param name="bClobber">Optional, specifies whether to overwrite existing metadata keys with those received, default false.</param>
		/// <returns>The metadata object for convenience.</returns>
		abstract public BuildMetadataBase GetBuildMetaData(BuildPatchToolStagingInfo StagingInfo, string McpConfigName, bool bClobber = false);

		/// <summary>
		/// Get a label string for the specific Platform requested.
		/// </summary>
		/// <param name="DestinationLabel">Base of label</param>
		/// <param name="Platform">Platform to add to base label.</param>
		/// <returns>The label string including platform postfix.</returns>
		abstract public string GetLabelWithPlatform(string DestinationLabel, MCPPlatform Platform);

		/// <summary>
		/// Get a BuildVersion string with the Platform concatenated on.
		/// </summary>
		/// <param name="DestinationLabel">Base of label</param>
		/// <param name="Platform">Platform to add to base label.</param>
		/// <returns>The BuildVersion string including platform postfix.</returns>
		abstract public string GetBuildVersionWithPlatform(BuildPatchToolStagingInfo StagingInfo);

		/// <summary>
		/// Get the BuildVersion for a build that is labeled under a specific appname.
		/// </summary>
		/// <param name="AppName">Application name to check the label in</param>
		/// <param name="LabelName">Label name to get the build version for</param>
		/// <param name="McpConfigName">Which BuildInfo backend to label the build in.</param>
		/// <returns>The BuildVersion or null if no build labeled.</returns>
		abstract public string GetLabeledBuildVersion(string AppName, string LabelName, string McpConfigName);

		/// <summary>
		/// Apply the requested label to the requested build in the BuildInfo backend for the requested MCP environment
		/// </summary>
		/// <param name="StagingInfo">Staging info for the build to label.</param>
		/// <param name="DestinationLabelWithPlatform">Label, including platform, to apply</param>
		/// <param name="McpConfigName">Which BuildInfo backend to label the build in.</param>
		abstract public void LabelBuild(BuildPatchToolStagingInfo StagingInfo, string DestinationLabelWithPlatform, string McpConfigName);

		/// <summary>
		/// Informs Patcher Service of a new build availability after async labeling is complete
		/// (this usually means the build was copied to a public file server before the label could be applied).
		/// </summary>
		/// <param name="Command">Parent command</param>
		/// <param name="AppName">Application name that the patcher service will use.</param>
		/// <param name="BuildVersion">BuildVersion string that the patcher service will use.</param>
		/// <param name="ManifestRelativePath">Relative path to the Manifest file relative to the global build root (which is like P:\Builds) </param>
		/// <param name="LabelName">Name of the label that we will be setting.</param>
		abstract public void BuildPromotionCompleted(BuildPatchToolStagingInfo stagingInfo, string AppName, string BuildVersion, string ManifestRelativePath, string PlatformName, string LabelName);
	}

    /// <summary>
    /// Helpers for using the MCP account service
    /// </summary>
    public abstract class McpAccountServiceBase
    {
        static McpAccountServiceBase Handler = null;

		Dictionary<string, Tuple<string, DateTime>> CachedTokens = new Dictionary<string, Tuple<string, DateTime>>();
		readonly object CachedTokensLock = new object();

        public static McpAccountServiceBase Get()
        {
            if (Handler == null)
            {
                Assembly[] LoadedAssemblies = AppDomain.CurrentDomain.GetAssemblies();
                foreach (var Dll in LoadedAssemblies)
                {
                    Type[] AllTypes = Dll.SafeGetLoadedTypes();
                    foreach (var PotentialConfigType in AllTypes)
                    {
                        if (PotentialConfigType != typeof(McpAccountServiceBase) && typeof(McpAccountServiceBase).IsAssignableFrom(PotentialConfigType))
                        {
                            Handler = Activator.CreateInstance(PotentialConfigType) as McpAccountServiceBase;
                            break;
                        }
                    }
                }
                if (Handler == null)
                {
                    throw new AutomationException("Attempt to use McpAccountServiceBase.Get() and it doesn't appear that there are any modules that implement this class.");
                }
            }
            return Handler;
        }

		/// <summary>
		/// Gets an OAuth client token for an environment using the default client id and client secret
		/// </summary>
		/// <param name="McpConfig">A descriptor for the environment we want a token for</param>
		/// <returns>An OAuth client token for the specified environment.</returns>
		public string GetClientToken(McpConfigData McpConfig)
		{
			lock(CachedTokensLock)
			{
				if (CachedTokens.ContainsKey(McpConfig.Name))
				{
					Tuple<string, DateTime> TokenWithExpiry = CachedTokens[McpConfig.Name];
					if (TokenWithExpiry.Item2 > DateTime.UtcNow)
					{
						CommandUtils.LogInformation("Reusing client token for {0} with expiry {1:yyyy-MM-dd HH:mm:ss}", McpConfig.Name, TokenWithExpiry.Item2);
						return TokenWithExpiry.Item1;
					}
				}
			}

			DateTime Expiry;
			string Result = GetClientToken(McpConfig, McpConfig.ClientId, McpConfig.ClientSecret, out Expiry);

			lock(CachedTokensLock)
			{
				if (CachedTokens.ContainsKey(McpConfig.Name))
				{
					CachedTokens[McpConfig.Name] = new Tuple<string, DateTime>(Result, Expiry);
				}
				else
				{
					CachedTokens.Add(McpConfig.Name, new Tuple<string, DateTime>(Result, Expiry));
				}
				CommandUtils.LogInformation("Obtained new client token for {0} with expiry {1:yyyy-MM-dd HH:mm:ss}", McpConfig.Name, Expiry);
			}
			return Result;
		}

		/// <summary>
		/// Gets an OAuth client token using the default client id and client secret and the environment for the specified staging info
		/// </summary>
		/// <param name="StagingInfo">The staging info for the build we're working with. This will be used to determine the correct back-end service.</param>
		/// <returns></returns>
		public string GetClientToken(BuildPatchToolStagingInfo StagingInfo)
		{
			McpConfigData McpConfig = McpConfigMapper.FromStagingInfo(StagingInfo);
			return GetClientToken(McpConfig);
		}

		/// <summary>
		/// Gets an OAuth client token for an environment using the specified client id and client secret
		/// </summary>
		/// <param name="McpConfig">A descriptor for the environment we want a token for</param>
		/// <param name="ClientId">The client id used to obtain the token</param>
		/// <param name="ClientSecret">The client secret used to obtain the token</param>
		/// <returns>An OAuth client token for the specified environment.</returns>
		public string GetClientToken(McpConfigData McpConfig, string ClientId, string ClientSecret)
		{
			DateTime ThrowAway;
			return GetClientToken(McpConfig, ClientId, ClientSecret, out ThrowAway);
		}

		/// <summary>
		/// Gets an OAuth client token for an environment using the specified client id and client secret
		/// </summary>
		/// <param name="McpConfig">A descriptor for the environment we want a token for</param>
		/// <param name="ClientId">The client id used to obtain the token</param>
		/// <param name="ClientSecret">The client secret used to obtain the token</param>
		/// <param name="Expiry">Output parameter which receives the expiry date of the generated token</param>
		/// <returns>An OAuth client token for the specified environment.</returns>
		public abstract string GetClientToken(McpConfigData McpConfig, string ClientId, string ClientSecret, out DateTime Expiry);

		public abstract string SendWebRequest(WebRequest Upload, string Method, string ContentType, byte[] Data);
    }

	/// <summary>
	/// Helper class to manage files stored in some arbitrary cloud storage system
	/// </summary>
	public abstract class CloudStorageBase
	{
		private static readonly object LockObj = new object();
		private static Dictionary<string, CloudStorageBase> Handlers = new Dictionary<string, CloudStorageBase>();
		private const string DEFAULT_INSTANCE_NAME = "DefaultInstance";

		/// <summary>
		/// Gets the default instance of CloudStorageBase
		/// </summary>
		/// <returns>A default instance of CloudStorageBase. The first time each instance is returned, it will require initialization with its Init() method.</returns>
		public static CloudStorageBase Get()
		{
			return GetByNameImpl(DEFAULT_INSTANCE_NAME); // Identifier for the default cloud storage
		}

		/// <summary>
		/// Gets an instance of CloudStorageBase.
		/// Multiple calls with the same instance name will return the same object.
		/// </summary>
		/// <param name="InstanceName">The name of the object to return</param>
		/// <returns>An instance of CloudStorageBase. The first time each instance is returned, it will require initialization with its Init() method.</returns>
		public static CloudStorageBase GetByName(string InstanceName)
		{
			if (InstanceName == DEFAULT_INSTANCE_NAME)
			{
				CommandUtils.LogWarning("CloudStorageBase.GetByName called with {0}. This will return the same instance as Get().", DEFAULT_INSTANCE_NAME);
			}
			return GetByNameImpl(InstanceName);
		}

		private  static CloudStorageBase GetByNameImpl(string InstanceName)
		{
			CloudStorageBase Result = null;
			if (!Handlers.TryGetValue(InstanceName, out Result))
			{
				lock (LockObj)
				{
					if (Handlers.ContainsKey(InstanceName))
					{
						Result = Handlers[InstanceName];
					}
					else
					{
						Assembly[] LoadedAssemblies = AppDomain.CurrentDomain.GetAssemblies();
						foreach (var Dll in LoadedAssemblies)
						{
							Type[] AllTypes = Dll.SafeGetLoadedTypes();
							foreach (var PotentialConfigType in AllTypes)
							{
								if (PotentialConfigType != typeof(CloudStorageBase) && typeof(CloudStorageBase).IsAssignableFrom(PotentialConfigType))
								{
									Result = Activator.CreateInstance(PotentialConfigType) as CloudStorageBase;
									Handlers.Add(InstanceName, Result);
									break;
								}
							}
						}
					}
				}
				if (Result == null)
				{
					throw new AutomationException("Could not find any modules which provide an implementation of CloudStorageBase.");
				}
			}
			return Result;
		}

		/// <summary>
		/// Initializes the provider.
		/// <param name="Config">Configuration data to initialize the provider. The exact format of the data is provider specific. It might, for example, contain an API key.</param>
		/// </summary>
		abstract public void Init(Dictionary<string,object> Config, bool bForce = false);

		/// <summary>
		/// Retrieves a file from the cloud storage provider
		/// </summary>
		/// <param name="Container">The name of the folder or container from which contains the file being checked.</param>
		/// <param name="Identifier">The identifier or filename of the file to check.</param>
        	/// <param name="bQuiet">If set to true, all log output for the operation is suppressed.</param>
		/// <returns>True if the file exists in cloud storage, false otherwise.</returns>
		abstract public bool FileExists(string Container, string Identifier, bool bQuiet = false);

		/// <summary>
		/// Retrieves a file from the cloud storage provider and saves it to disk.
		/// </summary>
		/// <param name="Container">The name of the folder or container from which to retrieve the file.</param>
		/// <param name="Identifier">The identifier or filename of the file to retrieve.</param>
		/// <param name="OutputFile">The full path to the name of the file to save</param>
		/// <param name="ContentType">An OUTPUT parameter containing the content's type (null if the cloud provider does not provide this information)</param>
		/// <param name="bOverwrite">If false, and the OutputFile already exists, an error will be thrown.</param>
		/// <returns>The number of bytes downloaded.</returns>
		abstract public long DownloadFile(string Container, string Identifier, string OutputFile, out string ContentType, bool bOverwrite = false);

		/// <summary>
		/// Retrieves a file from the cloud storage provider.
		/// </summary>
		/// <param name="Container">The name of the folder or container from which to retrieve the file.</param>
		/// <param name="Identifier">The identifier or filename of the file to retrieve.</param>
		/// <param name="ContentType">An OUTPUT parameter containing the content's type (null if the cloud provider does not provide this information)</param>
		/// <returns>A byte array containing the file's contents.</returns>
		abstract public byte[] GetFile(string Container, string Identifier, out string ContentType);

		/// <summary>
		/// Posts a file to the cloud storage provider.
		/// </summary>
		/// <param name="Container">The name of the folder or container in which to store the file.</param>
		/// <param name="Identifier">The identifier or filename of the file to write.</param>
		/// <param name="Contents">A byte array containing the data to write.</param>
		/// <param name="ContentType">The MIME type of the file being uploaded. If left NULL, will be determined server-side by cloud provider.</param>
		/// <param name="bOverwrite">If true, will overwrite an existing file.  If false, will throw an exception if the file exists.</param>
		/// <param name="bMakePublic">Specifies whether the file should be made publicly readable.</param>
		/// <param name="bQuiet">If set to true, all log output for the operation is supressed.</param>
		/// <param name="Metadata">If not null, key-value pairs of metadata to be applied to the object.</param>
		/// <returns>A PostFileResult indicating whether the call was successful, and the URL to the uploaded file.</returns>
		abstract public PostFileResult PostFile(string Container, string Identifier, byte[] Contents, string ContentType = null, bool bOverwrite = true, bool bMakePublic = false, bool bQuiet = false, IDictionary<string, object> Metadata = null);

		/// <summary>
		/// Posts a file to the cloud storage provider asynchronously.
		/// </summary>
		/// <param name="Container">The name of the folder or container in which to store the file.</param>
		/// <param name="Identifier">The identifier or filename of the file to write.</param>
		/// <param name="Contents">A byte array containing the data to write.</param>
		/// <param name="ContentType">The MIME type of the file being uploaded. If left NULL, will be determined server-side by cloud provider.</param>
		/// <param name="bOverwrite">If true, will overwrite an existing file.  If false, will throw an exception if the file exists.</param>
		/// <param name="bMakePublic">Specifies whether the file should be made publicly readable.</param>
        /// <param name="bQuiet">If set to true, all log output for the operation is supressed.</param>
		/// <param name="Metadata">If not null, key-value pairs of metadata to be applied to the object.</param>
		/// <returns>A PostFileResult indicating whether the call was successful, and the URL to the uploaded file.</returns>
		abstract public Task<PostFileResult> PostFileAsync(string Container, string Identifier, byte[] Contents, string ContentType = null, bool bOverwrite = true, bool bMakePublic = false, bool bQuiet = false, IDictionary<string, object> Metadata = null);

		/// <summary>
		/// Posts a file to the cloud storage provider.
		/// </summary>
		/// <param name="Container">The name of the folder or container in which to store the file.</param>
		/// <param name="Identifier">The identifier or filename of the file to write.</param>
		/// <param name="SourceFilePath">The full path of the file to upload.</param>
		/// <param name="ContentType">The MIME type of the file being uploaded. If left NULL, will be determined server-side by cloud provider.</param>
		/// <param name="bOverwrite">If true, will overwrite an existing file.  If false, will throw an exception if the file exists.</param>
		/// <param name="bMakePublic">Specifies whether the file should be made publicly readable.</param>
		/// <param name="bQuiet">If set to true, all log output for the operation is supressed.</param>
		/// <param name="Metadata">If not null, key-value pairs of metadata to be applied to the object.</param>
		/// <returns>A PostFileResult indicating whether the call was successful, and the URL to the uploaded file.</returns>
		abstract public PostFileResult PostFile(string Container, string Identifier, string SourceFilePath, string ContentType = null, bool bOverwrite = true, bool bMakePublic = false, bool bQuiet = false, IDictionary<string, object> Metadata = null);

		/// <summary>
		/// Posts a file to the cloud storage provider asynchronously.
		/// </summary>
		/// <param name="Container">The name of the folder or container in which to store the file.</param>
		/// <param name="Identifier">The identifier or filename of the file to write.</param>
		/// <param name="SourceFilePath">The full path of the file to upload.</param>
		/// <param name="ContentType">The MIME type of the file being uploaded. If left NULL, will be determined server-side by cloud provider.</param>
		/// <param name="bOverwrite">If true, will overwrite an existing file.  If false, will throw an exception if the file exists.</param>
		/// <param name="bMakePublic">Specifies whether the file should be made publicly readable.</param>
        /// <param name="bQuiet">If set to true, all log output for the operation is supressed.</param>
		/// <param name="Metadata">If not null, key-value pairs of metadata to be applied to the object.</param>
		/// <returns>A PostFileResult indicating whether the call was successful, and the URL to the uploaded file.</returns>
		abstract public Task<PostFileResult> PostFileAsync(string Container, string Identifier, string SourceFilePath, string ContentType = null, bool bOverwrite = true, bool bMakePublic = false, bool bQuiet = false, IDictionary<string, object> Metadata = null);

		/// <summary>
		/// Posts a file to the cloud storage provider using multiple connections.
		/// </summary>
		/// <param name="Container">The name of the folder or container in which to store the file.</param>
		/// <param name="Identifier">The identifier or filename of the file to write.</param>
		/// <param name="SourceFilePath">The full path of the file to upload.</param>
		/// <param name="NumConcurrentConnections">The number of concurrent connections to use during uploading.</param>
		/// <param name="PartSizeMegabytes">The size of each part that is uploaded. Minimum (and default) is 5 MB.</param>
		/// <param name="ContentType">The MIME type of the file being uploaded. If left NULL, will be determined server-side by cloud provider.</param>
		/// <param name="bOverwrite">If true, will overwrite an existing file. If false, will throw an exception if the file exists.</param>
		/// <param name="bMakePublic">Specifies whether the file should be made publicly readable.</param>
		/// <param name="bQuiet">If set to true, all log output for the operation is supressed.</param>
		/// <param name="Metadata">If not null, key-value pairs of metadata to be applied to the object.</param>
		/// <returns>A PostFileResult indicating whether the call was successful, and the URL to the uploaded file.</returns>
		public PostFileResult PostMultipartFile(string Container, string Identifier, string SourceFilePath, int NumConcurrentConnections, decimal PartSizeMegabytes = 5.0m, string ContentType = null, bool bOverwrite = true, bool bMakePublic = false, bool bQuiet = false, IDictionary<string, object> Metadata = null)
		{
			return PostMultipartFileAsync(Container, Identifier, SourceFilePath, NumConcurrentConnections, PartSizeMegabytes, ContentType, bOverwrite, bMakePublic, bQuiet, Metadata).Result;
		}

		/// <summary>
		/// Posts a file to the cloud storage provider using multiple connections asynchronously.
		/// </summary>
		/// <param name="Container">The name of the folder or container in which to store the file.</param>
		/// <param name="Identifier">The identifier or filename of the file to write.</param>
		/// <param name="SourceFilePath">The full path of the file to upload.</param>
		/// <param name="NumConcurrentConnections">The number of concurrent connections to use during uploading.</param>
		/// <param name="PartSizeMegabytes">The size of each part that is uploaded. Minimum (and default) is 5 MB.</param>
		/// <param name="ContentType">The MIME type of the file being uploaded. If left NULL, will be determined server-side by cloud provider.</param>
		/// <param name="bOverwrite">If true, will overwrite an existing file. If false, will throw an exception if the file exists.</param>
		/// <param name="bMakePublic">Specifies whether the file should be made publicly readable.</param>
		/// <param name="bQuiet">If set to true, all log output for the operation is supressed.</param>
		/// <param name="Metadata">If not null, key-value pairs of metadata to be applied to the object.</param>
		/// <returns>A PostFileResult indicating whether the call was successful, and the URL to the uploaded file.</returns>
		abstract public Task<PostFileResult> PostMultipartFileAsync(string Container, string Identifier, string SourceFilePath, int NumConcurrentConnections, decimal PartSizeMegabytes = 5.0m, string ContentType = null, bool bOverwrite = true, bool bMakePublic = false, bool bQuiet = false, IDictionary<string, object> Metadata = null);



		/// <summary>
		/// Deletes a file from cloud storage
		/// </summary>
		/// <param name="Container">The name of the folder or container in which to store the file.</param>
		/// <param name="Identifier">The identifier or filename of the file to write.</param>
		abstract public void DeleteFile(string Container, string Identifier);

		/// <summary>
		/// Deletes a folder from cloud storage
		/// </summary>
		/// <param name="Container">The name of the folder or container from which to delete the file.</param>
		/// <param name="FolderIdentifier">The identifier or name of the folder to delete.</param>
		abstract public void DeleteFolder(string Container, string FolderIdentifier);

		/// <summary>
		/// Retrieves a list of folders from the cloud storage provider
		/// </summary>
		/// <param name="Container">The name of the container from which to list folders.</param>
		/// <param name="Prefix">A string to specify the identifer that you want to list from. Typically used to specify a relative folder within the container to list all of its folders. Specify null to return folders in the root of the container.</param>
		/// <param name="Options">An action which acts upon an options object to configure the operation. See ListOptions for more details.</param>
		/// <returns>An array of paths to the folders in the specified container and matching the prefix constraint.</returns>
		public string[] ListFolders(string Container, string Prefix, Action<ListOptions> Options)
		{
			ListOptions Opts = new ListOptions();
			if (Options != null)
			{
				Options(Opts);
			}
			return ListFolders(Container, Prefix, Opts);
		}

		/// <summary>
		/// Retrieves a list of folders from the cloud storage provider
		/// </summary>
		/// <param name="Container">The name of the container from which to list folders.</param>
		/// <param name="Prefix">A string to specify the identifer that you want to list from. Typically used to specify a relative folder within the container to list all of its folders. Specify null to return folders in the root of the container.</param>
		/// <param name="Options">An options object to configure the operation. See ListOptions for more details.</param>
		/// <returns>An array of paths to the folders in the specified container and matching the prefix constraint.</returns>
		abstract public string[] ListFolders(string Container, string Prefix, ListOptions Options);

		/// <summary>
		/// DEPRECATED. Retrieves a list of files from the cloud storage provider.  See overload with ListOptions for non-deprecated use.
		/// </summary>
		/// <param name="Container">The name of the folder or container from which to list files.</param>
		/// <param name="Prefix">A string with which the identifier or filename should start. Typically used to specify a relative directory within the container to list all of its files recursively. Specify null to return all files.</param>
		/// <param name="Recursive">Indicates whether the list of files returned should traverse subdirectories</param>
		/// <param name="bQuiet">If set to true, all log output for the operation is supressed.</param>
		/// <returns>An array of paths to the files in the specified location and matching the prefix constraint.</returns>
		public string[] ListFiles(string Container, string Prefix = null, bool bRecursive = true, bool bQuiet = false)
		{
			return ListFiles(Container, Prefix, opts =>
			{
				opts.bRecursive = bRecursive;
				opts.bQuiet = bQuiet;
			});
		}

		/// <summary>
		/// Retrieves a list of files from the cloud storage provider
		/// </summary>
		/// <param name="Container">The name of the container from which to list folders.</param>
		/// <param name="Prefix">A string to specify the identifer that you want to list from. Typically used to specify a relative folder within the container to list all of its folders. Specify null to return folders in the root of the container.</param>
		/// <param name="Options">An action which acts upon an options object to configure the operation. See ListOptions for more details.</param>
		/// <returns>An array of paths to the folders in the specified container and matching the prefix constraint.</returns>
		public string[] ListFiles(string Container, string Prefix, Action<ListOptions> Options)
		{
			ListOptions Opts = new ListOptions();
			if (Options != null)
			{
				Options(Opts);
			}
			return ListFiles(Container, Prefix, Opts);
		}

		/// <summary>
		/// Retrieves a list of files from the cloud storage provider.
		/// </summary>
		/// <param name="Container">The name of the folder or container from which to list files.</param>
		/// <param name="Prefix">A string with which the identifier or filename should start. Typically used to specify a relative directory within the container to list all of its files recursively. Specify null to return all files.</param>
		/// <param name="Options">An options object to configure the operation. See ListOptions for more details.</param>
		/// <returns>An array of paths to the files in the specified location and matching the prefix constraint.</returns>
		abstract public string[] ListFiles(string Container, string Prefix, ListOptions Options);

		/// <summary>
		/// Retrieves a list of files together with basic metadata from the cloud storage provider
		/// </summary>
		/// <param name="Container">The name of the container from which to list folders.</param>
		/// <param name="Prefix">A string to specify the identifer that you want to list from. Typically used to specify a relative folder within the container to list all of its folders. Specify null to return folders in the root of the container.</param>
		/// <param name="Options">An action which acts upon an options object to configure the operation. See ListOptions for more details.</param>
		/// <returns>An array of metadata objects (including filenames) to the files in the specified location and matching the prefix constraint.</returns>
		public ObjectMetadata[] ListFilesWithMetadata(string Container, string Prefix, Action<ListOptions> Options)
		{
			ListOptions Opts = new ListOptions();
			if (Options != null)
			{
				Options(Opts);
			}
			return ListFilesWithMetadata(Container, Prefix, Opts);
		}

		/// <summary>
		/// Retrieves a list of files together with basic metadata from the cloud storage provider.
		/// </summary>
		/// <param name="Container">The name of the folder or container from which to list files.</param>
		/// <param name="Prefix">A string with which the identifier or filename should start. Typically used to specify a relative directory within the container to list all of its files recursively. Specify null to return all files.</param>
		/// <param name="Options">An options object to configure the operation. See ListOptions for more details.</param>
		/// <returns>An array of metadata objects (including filenames) to the files in the specified location and matching the prefix constraint.</returns>
		abstract public ObjectMetadata[] ListFilesWithMetadata(string Container, string Prefix, ListOptions Options);

		/// <summary>
		/// Sets one or more items of metadata on an object in cloud storage
		/// </summary>
		/// <param name="Container">The name of the folder or container in which the file is stored.</param>
		/// <param name="Identifier">The identifier of filename of the file to set metadata on.</param>
		/// <param name="Metadata">A dictionary containing the metadata keys and their values</param>
		/// <param name="bMerge">If true, then existing metadata will be replaced (or overwritten if the keys match). If false, no existing metadata is retained.</param>
		abstract public void SetMetadata(string Container, string Identifier, IDictionary<string, object> Metadata, bool bMerge = true);

		/// <summary>
		/// Gets all items of metadata on an object in cloud storage. Metadata values are all returned as strings.
		/// </summary>
		/// <param name="Container">The name of the folder or container in which the file is stored.</param>
		/// <param name="Identifier">The identifier of filename of the file to get metadata.</param>
		abstract public Dictionary<string, string> GetMetadata(string Container, string Identifier);

		/// <summary>
		/// Gets an item of metadata from an object in cloud storage. The object is casted to the specified type.
		/// </summary>
		/// <param name="Container">The name of the folder or container in which the file is stored.</param>
		/// <param name="Identifier">The identifier of filename of the file to get metadata.</param>
		/// <param name="MetadataKey">The key of the item of metadata to retrieve.</param>
		abstract public T GetMetadata<T>(string Container, string Identifier, string MetadataKey);

		/// <summary>
		/// Updates the timestamp on a particular file in cloud storage to the current time.
		/// </summary>
		/// <param name="Container">The name of the container in which the file is stored.</param>
		/// <param name="Identifier">The identifier of filename of the file to touch.</param>
		abstract public void TouchFile(string Container, string Identifier);

		/// <summary>
		/// Copies manifest and chunks from a staged location to cloud storage.
		/// </summary>
		/// <param name="Container">The name of the container in which to store files.</param>
		/// <param name="stagingInfo">Staging info used to determine where the chunks are to copy.</param>
		/// <param name="bForce">If true, will always copy the manifest and chunks to cloud storage. Otherwise, will only copy if the manifest isn't already present on cloud storage.</param>
		/// <returns>True if the build was copied to cloud storage, false otherwise.</returns>
		abstract public bool CopyChunksToCloudStorage(string Container, BuildPatchToolStagingInfo StagingInfo, bool bForce = false);

		/// <summary>
		/// Copies manifest and its chunks from a specific path to a given target folder in the cloud.
		/// </summary>
		/// <param name="Container">The name of the container in which to store files.</param>
		/// <param name="RemoteCloudDir">The path within the container that the files should be stored in.</param>
		/// <param name="ManifestFilePath">The full path of the manifest file to copy.</param>
		/// <param name="bForce">If true, will always copy the manifest and chunks to cloud storage. Otherwise, will only copy if the manifest isn't already present on cloud storage.</param>
		/// <returns>True if the build was copied to cloud storage, false otherwise.</returns>
		abstract public bool CopyChunksToCloudStorage(string Container, string RemoteCloudDir, string ManifestFilePath, bool bForce = false);

		/// <summary>
		/// Verifies whether a manifest for a given build is in cloud storage.
		/// </summary>
		/// <param name="Container">The name of the folder or container in which to store files.</param>
		/// <param name="stagingInfo">Staging info representing the build to check.</param>
		/// <returns>True if the manifest exists in cloud storage, false otherwise.</returns>
		abstract public bool IsManifestOnCloudStorage(string Container, BuildPatchToolStagingInfo StagingInfo);

		public class PostFileResult
		{
			/// <summary>
			/// Set to the URL of the uploaded file on success
			/// </summary>
			public string ObjectURL { get; set; }

			/// <summary>
			/// Set to true if the write succeeds, false otherwise.
			/// </summary>
			public bool bSuccess { get; set; }
		}

		/// <summary>
		/// Encapsulates options used when listing files or folders using ListFiles and ListFolders
		/// </summary>
		public class ListOptions
		{
			public ListOptions()
			{
				bQuiet = false;
				bRecursive = false;
				bReturnURLs = true;
			}

			/// <summary>
			/// If set to true, all log output for the operation is suppressed. Defaults to false.
			/// </summary>
			public bool bQuiet { get; set; }

			/// <summary>
			/// Indicates whether the list of files returned should traverse subfolders. Defaults to false.
			/// </summary>
			public bool bRecursive { get; set; }

			/// <summary>
			/// If true, returns the full URL to the listed objects. If false, returns their identifier within the container. Defaults to true.
			/// </summary>
			public bool bReturnURLs { get; set; }
		}

		public class ObjectMetadata
		{
			public string ETag { get; set; }
			public string Path { get; set; }
			public string Url { get; set; }
			public DateTime LastModified { get; set; }
			public long Size { get; set; }
		}
	}

	public abstract class CatalogServiceBase
	{
		static CatalogServiceBase Handler = null;

		public static CatalogServiceBase Get()
		{
			if (Handler == null)
			{
				Assembly[] LoadedAssemblies = AppDomain.CurrentDomain.GetAssemblies();
				foreach (var Dll in LoadedAssemblies)
				{
					Type[] AllTypes = Dll.GetTypes();
					foreach (var PotentialConfigType in AllTypes)
					{
						if (PotentialConfigType != typeof(CatalogServiceBase) && typeof(CatalogServiceBase).IsAssignableFrom(PotentialConfigType))
						{
							Handler = Activator.CreateInstance(PotentialConfigType) as CatalogServiceBase;
							break;
						}
					}
				}
				if (Handler == null)
				{
					throw new AutomationException("Attempt to use McpCatalogServiceBase.Get() and it doesn't appear that there are any modules that implement this class.");
				}
			}
			return Handler;
		}

		public abstract string GetAppName(string ItemId, string[] EngineVersions, string McpConfigName);
		public abstract string[] GetNamespaces(string McpConfigName);
		public abstract McpCatalogItem GetItemById(string Namespace, string ItemId, string McpConfigName);
		public abstract IEnumerable<McpCatalogItem> GetAllItems(string Namespace, string McpConfigName);

		public class McpCatalogItem
		{
			public string Id { get; set; }
			public string Title { get; set; }
			public string Description { get; set; }
			public string LongDescription { get; set; }
			public string TechnicalDetails { get; set; }
			public string Namespace { get; set; }
			public string Status { get; set; }
			public DateTime CreationDate { get; set; }
			public DateTime LastModifiedDate { get; set; }
			public ReleaseInfo[] ReleaseInfo { get; set; }
		}

		public class ReleaseInfo
		{
			public string AppId { get; set; }
			public string[] CompatibleApps { get; set; }
			public string[] Platform { get; set; }
			public DateTime DateAdded { get; set; }
		}
	}
}

namespace EpicGames.MCP.Config
{
    /// <summary>
    /// Class for retrieving MCP configuration data
    /// </summary>
    public class McpConfigHelper
    {
        // List of configs is cached off for fetching from multiple times
        private static Dictionary<string, McpConfigData> Configs;

        public static McpConfigData Find(string ConfigName)
        {
            if (Configs == null)
            {
                // Load all secret configs by trying to instantiate all classes derived from McpConfig from all loaded DLLs.
                // Note that we're using the default constructor on the secret config types.
                Configs = new Dictionary<string, McpConfigData>();
                Assembly[] LoadedAssemblies = AppDomain.CurrentDomain.GetAssemblies();
                foreach (var Dll in LoadedAssemblies)
                {
                    Type[] AllTypes = Dll.SafeGetLoadedTypes();
                    foreach (var PotentialConfigType in AllTypes)
                    {
                        if (PotentialConfigType != typeof(McpConfigData) && typeof(McpConfigData).IsAssignableFrom(PotentialConfigType))
                        {
                            try
                            {
                                McpConfigData Config = Activator.CreateInstance(PotentialConfigType) as McpConfigData;
                                if (Config != null)
                                {
                                    Configs.Add(Config.Name, Config);
                                }
                            }
                            catch
                            {
                                BuildCommand.LogWarning("Unable to create McpConfig: {0}", PotentialConfigType.Name);
                            }
                        }
                    }
                }
            }
            McpConfigData LoadedConfig;
            Configs.TryGetValue(ConfigName, out LoadedConfig);
            if (LoadedConfig == null)
            {
                throw new AutomationException("Unable to find requested McpConfig: {0}", ConfigName);
            }
            return LoadedConfig;
        }
    }

    // Class for storing mcp configuration data
    public class McpConfigData
    {
		public McpConfigData(string InName, string InAccountBaseUrl, string InFortniteBaseUrl, string InLauncherBaseUrl, string InBuildInfoV2BaseUrl, string InLauncherV2BaseUrl, string InCatalogBaseUrl, string InClientId, string InClientSecret)
        {
            Name = InName;
            AccountBaseUrl = InAccountBaseUrl;
            FortniteBaseUrl = InFortniteBaseUrl;
            LauncherBaseUrl = InLauncherBaseUrl;
			BuildInfoV2BaseUrl = InBuildInfoV2BaseUrl;
			LauncherV2BaseUrl = InLauncherV2BaseUrl;
			CatalogBaseUrl = InCatalogBaseUrl;
            ClientId = InClientId;
            ClientSecret = InClientSecret;
        }

        public string Name;
        public string AccountBaseUrl;
        public string FortniteBaseUrl;
        public string LauncherBaseUrl;
		public string BuildInfoV2BaseUrl;
		public string LauncherV2BaseUrl;
		public string CatalogBaseUrl;
        public string ClientId;
        public string ClientSecret;

        public void SpewValues()
        {
            CommandUtils.LogVerbose("Name : {0}", Name);
            CommandUtils.LogVerbose("AccountBaseUrl : {0}", AccountBaseUrl);
            CommandUtils.LogVerbose("FortniteBaseUrl : {0}", FortniteBaseUrl);
            CommandUtils.LogVerbose("LauncherBaseUrl : {0}", LauncherBaseUrl);
			CommandUtils.LogVerbose("BuildInfoV2BaseUrl : {0}", BuildInfoV2BaseUrl);
			CommandUtils.LogVerbose("LauncherV2BaseUrl : {0}", LauncherV2BaseUrl);
			CommandUtils.LogVerbose("CatalogBaseUrl : {0}", CatalogBaseUrl);
            CommandUtils.LogVerbose("ClientId : {0}", ClientId);
            // we don't really want this in logs CommandUtils.LogVerbose("ClientSecret : {0}", ClientSecret);
        }
    }

    public class McpConfigMapper
    {
        static public McpConfigData FromMcpConfigKey(string McpConfigKey)
        {
            return McpConfigHelper.Find("MainGameDevNet");
        }

        static public McpConfigData FromStagingInfo(EpicGames.MCP.Automation.BuildPatchToolStagingInfo StagingInfo)
        {
            string McpConfigNameToLookup = null;
            if (StagingInfo.OwnerCommand != null)
            {
                McpConfigNameToLookup = StagingInfo.OwnerCommand.ParseParamValue("MCPConfig");
            }
            if (String.IsNullOrEmpty(McpConfigNameToLookup))
            {
                return FromMcpConfigKey(StagingInfo.McpConfigKey);
            }
            return McpConfigHelper.Find(McpConfigNameToLookup);
        }
    }

}
