// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// ModuleRules is a data structure that contains the rules for defining a module
	/// </summary>
	public class ModuleRules
	{
		/// <summary>
		/// Type of module
		/// </summary>
		public enum ModuleType
		{
			/// <summary>
			/// C++
			/// </summary>
			CPlusPlus,

			/// <summary>
			/// External (third-party)
			/// </summary>
			External,
		}

		/// <summary>
		/// Code optimization settings
		/// </summary>
		public enum CodeOptimization
		{
			/// <summary>
			/// Code should never be optimized if possible.
			/// </summary>
			Never,

			/// <summary>
			/// Code should only be optimized in non-debug builds (not in Debug).
			/// </summary>
			InNonDebugBuilds,

			/// <summary>
			/// Code should only be optimized in shipping builds (not in Debug, DebugGame, Development)
			/// </summary>
			InShippingBuildsOnly,

			/// <summary>
			/// Code should always be optimized if possible.
			/// </summary>
			Always,

			/// <summary>
			/// Default: 'InNonDebugBuilds' for game modules, 'Always' otherwise.
			/// </summary>
			Default,
		}

		/// <summary>
		/// What type of PCH to use for this module.
		/// </summary>
		public enum PCHUsageMode
		{
			/// <summary>
			/// Default: Engine modules use shared PCHs, game modules do not
			/// </summary>
			Default,

			/// <summary>
			/// Never use any PCHs.
			/// </summary>
			NoPCHs,

			/// <summary>
			/// Never use shared PCHs.  Always generate a unique PCH for this module if appropriate
			/// </summary>
			NoSharedPCHs,

			/// <summary>
			/// Shared PCHs are OK!
			/// </summary>
			UseSharedPCHs,

			/// <summary>
			/// Shared PCHs may be used if an explicit private PCH is not set through PrivatePCHHeaderFile. In either case, none of the source files manually include a module PCH, and should include a matching header instead.
			/// </summary>
			UseExplicitOrSharedPCHs,
		}

		/// <summary>
		/// Which type of targets this module should be precompiled for
		/// </summary>
		public enum PrecompileTargetsType
		{
			/// <summary>
			/// Never precompile this module.
			/// </summary>
			None,

			/// <summary>
			/// Inferred from the module's directory. Engine modules under Engine/Source/Runtime will be compiled for games, those under Engine/Source/Editor will be compiled for the editor, etc...
			/// </summary>
			Default,

			/// <summary>
			/// Any game targets.
			/// </summary>
			Game,

			/// <summary>
			/// Any editor targets.
			/// </summary>
			Editor,

			/// <summary>
			/// Any targets.
			/// </summary>
			Any,
		}

		/// <summary>
		/// Control visibility of symbols in this module for special cases
		/// </summary>
		public enum SymbolVisibility
		{
			/// <summary>
			/// Standard visibility rules
			/// </summary>
			Default,

			/// <summary>
			/// Make sure symbols in this module are visible in Dll builds
			/// </summary>
			VisibileForDll,
		}


		/// <summary>
		/// Information about a file which is required by the target at runtime, and must be moved around with it.
		/// </summary>
		[Serializable]
		public class RuntimeDependency
		{
			/// <summary>
			/// The file that should be staged. Should use $(EngineDir) and $(ProjectDir) variables as a root, so that the target can be relocated to different machines.
			/// </summary>
			public string Path;

			/// <summary>
			/// The initial location for this file. It will be copied to Path at build time, ready for staging.
			/// </summary>
			public string SourcePath;

			/// <summary>
			/// How to stage this file.
			/// </summary>
			public StagedFileType Type;

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="InPath">Path to the runtime dependency</param>
			/// <param name="InType">How to stage the given path</param>
			public RuntimeDependency(string InPath, StagedFileType InType = StagedFileType.NonUFS)
			{
				Path = InPath;
				Type = InType;
			}

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="InPath">Path to the runtime dependency</param>
			/// <param name="InSourcePath">Source path for the file in the working tree</param>
			/// <param name="InType">How to stage the given path</param>
			public RuntimeDependency(string InPath, string InSourcePath, StagedFileType InType = StagedFileType.NonUFS)
			{
				Path = InPath;
				SourcePath = InSourcePath;
				Type = InType;
			}
		}

		/// <summary>
		/// List of runtime dependencies, with convenience methods for adding new items
		/// </summary>
		[Serializable]
		public class RuntimeDependencyList
		{
			/// <summary>
			/// Inner list of runtime dependencies
			/// </summary>
			internal List<RuntimeDependency> Inner = new List<RuntimeDependency>();

			/// <summary>
			/// Default constructor
			/// </summary>
			public RuntimeDependencyList()
			{
			}

			/// <summary>
			/// Add a runtime dependency to the list
			/// </summary>
			/// <param name="InPath">Path to the runtime dependency. May include wildcards.</param>
			public void Add(string InPath)
			{
				Inner.Add(new RuntimeDependency(InPath));
			}

			/// <summary>
			/// Add a runtime dependency to the list
			/// </summary>
			/// <param name="InPath">Path to the runtime dependency. May include wildcards.</param>
			/// <param name="InType">How to stage this file</param>
			public void Add(string InPath, StagedFileType InType)
			{
				Inner.Add(new RuntimeDependency(InPath, InType));
			}

			/// <summary>
			/// Add a runtime dependency to the list
			/// </summary>
			/// <param name="InPath">Path to the runtime dependency. May include wildcards.</param>
			/// <param name="InSourcePath">Source path for the file to be added as a dependency. May include wildcards.</param>
			/// <param name="InType">How to stage this file</param>
			public void Add(string InPath, string InSourcePath, StagedFileType InType = StagedFileType.NonUFS)
			{
				Inner.Add(new RuntimeDependency(InPath, InSourcePath, InType));
			}

			/// <summary>
			/// Add a runtime dependency to the list
			/// </summary>
			/// <param name="InRuntimeDependency">RuntimeDependency instance</param>
			[Obsolete("Constructing a RuntimeDependency object is deprecated. Call RuntimeDependencies.Add() with the path to the file to stage.")]
			public void Add(RuntimeDependency InRuntimeDependency)
			{
				Inner.Add(InRuntimeDependency);
			}
		}

		/// <summary>
		/// List of runtime dependencies, with convenience methods for adding new items
		/// </summary>
		[Serializable]
		public class ReceiptPropertyList
		{
			/// <summary>
			/// Inner list of runtime dependencies
			/// </summary>
			internal List<ReceiptProperty> Inner = new List<ReceiptProperty>();

			/// <summary>
			/// Default constructor
			/// </summary>
			public ReceiptPropertyList()
			{
			}

			/// <summary>
			/// Add a receipt property to the list
			/// </summary>
			/// <param name="Name">Name of the property</param>
			/// <param name="Value">Value for the property</param>
			public void Add(string Name, string Value)
			{
				Inner.Add(new ReceiptProperty(Name, Value));
			}

			/// <summary>
			/// Add a receipt property to the list
			/// </summary>
			/// <param name="InReceiptProperty">ReceiptProperty instance</param>
			[Obsolete("Constructing a ReceiptProperty object is deprecated. Call ReceiptProperties.Add() with the path to the file to stage.")]
			public void Add(ReceiptProperty InReceiptProperty)
			{
				Inner.Add(InReceiptProperty);
			}
		}

		/// <summary>
		/// Stores information about a framework on IOS or MacOS
		/// </summary>
		public class Framework
		{
			/// <summary>
			/// Name of the framework
			/// </summary>
			internal string Name;

			/// <summary>
			/// For non-system frameworks, specifies the path to a zip file that contains it.
			/// </summary>
			internal string ZipPath;

			/// <summary>
			/// 
			/// </summary>
			internal string CopyBundledAssets = null;

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="Name">Name of the framework</param>
			/// <param name="ZipPath">Path to a zip file containing the framework. May be null.</param>
			/// <param name="CopyBundledAssets"></param>
			public Framework(string Name, string ZipPath = null, string CopyBundledAssets = null)
			{
				this.Name = Name;
				this.ZipPath = ZipPath;
				this.CopyBundledAssets = CopyBundledAssets;
			}
		}

		/// <summary>
		/// Deprecated; wrapper for Framework.
		/// </summary>
		[Obsolete("The UEBuildFramework class has been deprecated in UE 4.22. Please use the Framework class instead.")]
		public class UEBuildFramework : Framework
		{
			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="Name">Name of the framework</param>
			/// <param name="ZipPath">Path to a zip file containing the framework. May be null.</param>
			/// <param name="CopyBundledAssets"></param>
			public UEBuildFramework(string Name, string ZipPath = null, string CopyBundledAssets = null)
				: base(Name, ZipPath, CopyBundledAssets)
			{
			}
		}

		/// <summary>
		/// 
		/// </summary>
		public class BundleResource
		{
			/// <summary>
			/// 
			/// </summary>
			public string ResourcePath = null;

			/// <summary>
			/// 
			/// </summary>
			public string BundleContentsSubdir = null;

			/// <summary>
			/// 
			/// </summary>
			public bool bShouldLog = true;

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="ResourcePath"></param>
			/// <param name="BundleContentsSubdir"></param>
			/// <param name="bShouldLog"></param>
			public BundleResource(string ResourcePath, string BundleContentsSubdir = "Resources", bool bShouldLog = true)
			{
				this.ResourcePath = ResourcePath;
				this.BundleContentsSubdir = BundleContentsSubdir;
				this.bShouldLog = bShouldLog;
			}
		}

		/// <summary>
		/// Deprecated; wrapper for BundleResource.
		/// </summary>
		[Obsolete("The UEBuildBundleResource class has been deprecated in UE 4.22. Please use the BundleResource class instead.")]
		public class UEBuildBundleResource : BundleResource
		{
			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="InResourcePath"></param>
			/// <param name="InBundleContentsSubdir"></param>
			/// <param name="bInShouldLog"></param>
			public UEBuildBundleResource(string InResourcePath, string InBundleContentsSubdir = "Resources", bool bInShouldLog = true)
				: base(InResourcePath, InBundleContentsSubdir, bInShouldLog)
			{
			}
		}

		/// <summary>
		/// Name of this module
		/// </summary>
		public string Name
		{
			get;
			internal set;
		}

		/// <summary>
		/// File containing this module
		/// </summary>
		internal FileReference File;

		/// <summary>
		/// Directory containing this module
		/// </summary>
		internal DirectoryReference Directory;

		/// <summary>
		/// Additional directories that contribute to this module (likely in UnrealBuildTool.PlatformExtensionsDirectory). 
		/// The dictionary tracks module subclasses 
		/// </summary>
		internal Dictionary<Type, DirectoryReference> DirectoriesForModuleSubClasses;

		/// <summary>
		/// Plugin containing this module
		/// </summary>
		internal PluginInfo Plugin;

		/// <summary>
		/// The rules context for this instance
		/// </summary>
		internal ModuleRulesContext Context;

		/// <summary>
		/// Rules for the target that this module belongs to
		/// </summary>
		public readonly ReadOnlyTargetRules Target;

		/// <summary>
		/// Type of module
		/// </summary>
		public ModuleType Type = ModuleType.CPlusPlus;

		/// <summary>
		/// Subfolder of Binaries/PLATFORM folder to put this module in when building DLLs. This should only be used by modules that are found via searching like the
		/// TargetPlatform or ShaderFormat modules. If FindModules is not used to track them down, the modules will not be found.
		/// </summary>
		public string BinariesSubFolder = "";

		/// <summary>
		/// When this module's code should be optimized.
		/// </summary>
		public CodeOptimization OptimizeCode = CodeOptimization.Default;

		/// <summary>
		/// Explicit private PCH for this module. Implies that this module will not use a shared PCH.
		/// </summary>
		public string PrivatePCHHeaderFile;

		/// <summary>
		/// Header file name for a shared PCH provided by this module.  Must be a valid relative path to a public C++ header file.
		/// This should only be set for header files that are included by a significant number of other C++ modules.
		/// </summary>
		public string SharedPCHHeaderFile;
		
		/// <summary>
		/// Specifies an alternate name for intermediate directories and files for intermediates of this module. Useful when hitting path length limitations.
		/// </summary>
		public string ShortName = null;

		/// <summary>
		/// Precompiled header usage for this module
		/// </summary>
		public PCHUsageMode PCHUsage = PCHUsageMode.Default;

		/// <summary>
		/// Whether this module should be treated as an engine module (eg. using engine definitions, PCHs, compiled with optimizations enabled in DebugGame configurations, etc...).
		/// Initialized to a default based on the rules assembly it was created from.
		/// </summary>
		public bool bTreatAsEngineModule;

		/// <summary>
		/// Whether to use backwards compatible defaults for this module. By default, engine modules always use the latest default settings, while project modules do not (to support
		/// an easier migration path).
		/// </summary>
		public bool bUseBackwardsCompatibleDefaults;

		/// <summary>
		/// Use run time type information
		/// </summary>
		public bool bUseRTTI = false;

		/// <summary>
		/// Use AVX instructions
		/// </summary>
		public bool bUseAVX = false;

		/// <summary>
		/// Enable buffer security checks.  This should usually be enabled as it prevents severe security risks.
		/// </summary>
		public bool bEnableBufferSecurityChecks = true;

		/// <summary>
		/// Enable exception handling
		/// </summary>
		public bool bEnableExceptions = false;

		/// <summary>
		/// Enable objective C exception handling
		/// </summary>
		public bool bEnableObjCExceptions = false;

		/// <summary>
		/// Enable warnings for shadowed variables
		/// </summary>
		public bool bEnableShadowVariableWarnings = true;

		/// <summary>
		/// Enable warnings for using undefined identifiers in #if expressions
		/// </summary>
		public bool bEnableUndefinedIdentifierWarnings = true;

		/// <summary>
		/// If true and unity builds are enabled, this module will build without unity.
		/// </summary>
		public bool bFasterWithoutUnity = false;

		/// <summary>
		/// The number of source files in this module before unity build will be activated for that module.  If set to
		/// anything besides -1, will override the default setting which is controlled by MinGameModuleSourceFilesForUnityBuild
		/// </summary>
		public int MinSourceFilesForUnityBuildOverride = 0;

		/// <summary>
		/// Overrides BuildConfiguration.MinFilesUsingPrecompiledHeader if non-zero.
		/// </summary>
		public int MinFilesUsingPrecompiledHeaderOverride = 0;

		/// <summary>
		/// Module uses a #import so must be built locally when compiling with SN-DBS
		/// </summary>
		public bool bBuildLocallyWithSNDBS = false;

		/// <summary>
		/// Redistribution override flag for this module.
		/// </summary>
		public bool? IsRedistributableOverride = null;

		/// <summary>
		/// Whether the output from this module can be publicly distributed, even if it has code/
		/// dependencies on modules that are not (i.e. CarefullyRedist, NotForLicensees, NoRedist).
		/// This should be used when you plan to release binaries but not source.
		/// </summary>
		public bool bOutputPubliclyDistributable = false;

		/// <summary>
		/// List of folders which are whitelisted to be referenced when compiling this binary, without propagating restricted folder names
		/// </summary>
		public List<string> WhitelistRestrictedFolders = new List<string>();

		/// <summary>
		/// Enforce "include what you use" rules when PCHUsage is set to ExplicitOrSharedPCH; warns when monolithic headers (Engine.h, UnrealEd.h, etc...) 
		/// are used, and checks that source files include their matching header first.
		/// </summary>
		public bool bEnforceIWYU = true;

		/// <summary>
		/// Whether to add all the default include paths to the module (eg. the Source/Classes folder, subfolders under Source/Public).
		/// </summary>
		public bool bAddDefaultIncludePaths = true;

		/// <summary>
		/// Whether to ignore dangling (i.e. unresolved external) symbols in modules
		/// </summary>
		public bool bIgnoreUnresolvedSymbols = false;

		/// <summary>
		/// Whether this module should be precompiled. Defaults to the bPrecompile flag from the target. Clear this flag to prevent a module being precompiled.
		/// </summary>
		public bool bPrecompile;

		/// <summary>
		/// Whether this module should use precompiled data. Always true for modules created from installed assemblies.
		/// </summary>
		public bool bUsePrecompiled;

		/// <summary>
		/// Whether this module can use PLATFORM_XXXX style defines, where XXXX is a confidential platform name. This is used to ensure engine or other 
		/// shared code does not reveal confidential information inside an #if PLATFORM_XXXX block. Licensee game code may want to allow for them, however.
		/// Note: this is future looking, and previous confidential platforms (like PS4) are unlikely to be restricted
		/// </summary>
		public bool bAllowConfidentialPlatformDefines = false;

		/// <summary>
		/// List of modules names (no path needed) with header files that our module's public headers needs access to, but we don't need to "import" or link against.
		/// </summary>
		public List<string> PublicIncludePathModuleNames = new List<string>();

		/// <summary>
		/// List of public dependency module names (no path needed) (automatically does the private/public include). These are modules that are required by our public source files.
		/// </summary>
		public List<string> PublicDependencyModuleNames = new List<string>();

		/// <summary>
		/// List of modules name (no path needed) with header files that our module's private code files needs access to, but we don't need to "import" or link against.
		/// </summary>
		public List<string> PrivateIncludePathModuleNames = new List<string>();

		/// <summary>
		/// List of private dependency module names.  These are modules that our private code depends on but nothing in our public
		/// include files depend on.
		/// </summary>
		public List<string> PrivateDependencyModuleNames = new List<string>();

		/// <summary>
		/// Only for legacy reason, should not be used in new code. List of module dependencies that should be treated as circular references.  This modules must have already been added to
		/// either the public or private dependent module list.
		/// </summary>
		public List<string> CircularlyReferencedDependentModules = new List<string>();

		/// <summary>
		/// List of system/library include paths - typically used for External (third party) modules.  These are public stable header file directories that are not checked when resolving header dependencies.
		/// </summary>
		public List<string> PublicSystemIncludePaths = new List<string>();

		/// <summary>
		/// (This setting is currently not need as we discover all files from the 'Public' folder) List of all paths to include files that are exposed to other modules
		/// </summary>
		public List<string> PublicIncludePaths = new List<string>();

		/// <summary>
		/// List of all paths to this module's internal include files, not exposed to other modules (at least one include to the 'Private' path, more if we want to avoid relative paths)
		/// </summary>
		public List<string> PrivateIncludePaths = new List<string>();

		/// <summary>
		/// List of system/library paths (directory of .lib files) - typically used for External (third party) modules
		/// </summary>
		public List<string> PublicLibraryPaths = new List<string>();

		/// <summary>
		/// List of search paths for libraries at runtime (eg. .so files)
		/// </summary>
		public List<string> PrivateRuntimeLibraryPaths = new List<string>();

		/// <summary>
		/// List of search paths for libraries at runtime (eg. .so files)
		/// </summary>
		public List<string> PublicRuntimeLibraryPaths = new List<string>();

		/// <summary>
		/// List of additional libraries (names of the .lib files including extension) - typically used for External (third party) modules
		/// </summary>
		public List<string> PublicAdditionalLibraries = new List<string>();

		/// <summary>
		/// List of XCode frameworks (iOS and MacOS)
		/// </summary>
		public List<string> PublicFrameworks = new List<string>();

		/// <summary>
		/// List of weak frameworks (for OS version transitions)
		/// </summary>
		public List<string> PublicWeakFrameworks = new List<string>();

		/// <summary>
		/// List of addition frameworks - typically used for External (third party) modules on Mac and iOS
		/// </summary>
		public List<Framework> PublicAdditionalFrameworks = new List<Framework>();

		/// <summary>
		/// List of addition resources that should be copied to the app bundle for Mac or iOS
		/// </summary>
		public List<BundleResource> AdditionalBundleResources = new List<BundleResource>();

		/// <summary>
		/// For builds that execute on a remote machine (e.g. iOS), this list contains additional files that
		/// need to be copied over in order for the app to link successfully.  Source/header files and PCHs are
		/// automatically copied.  Usually this is simply a list of precompiled third party library dependencies.
		/// </summary>
		[Obsolete("To specify files to be transferred to a remote Mac for compilation, create a [Project]/Build/Rsync/RsyncProject.txt file. See https://linux.die.net/man/1/rsync for more information about Rsync filter rules.")]
		public List<string> PublicAdditionalShadowFiles = new List<string>();

		/// <summary>
		/// List of delay load DLLs - typically used for External (third party) modules
		/// </summary>
		public List<string> PublicDelayLoadDLLs = new List<string>();

		/// <summary>
		/// Accessor for the PublicDefinitions list
		/// </summary>
		[Obsolete("The 'Definitions' property has been deprecated. Please use 'PublicDefinitions' instead.")]
		public List<string> Definitions
		{
			get { return PublicDefinitions; }
		}

		/// <summary>
		/// Private compiler definitions for this module
		/// </summary>
		public List<string> PrivateDefinitions = new List<string>();

		/// <summary>
		/// Public compiler definitions for this module
		/// </summary>
		public List<string> PublicDefinitions = new List<string>();

		/// <summary>
		/// Append (or create)
		/// </summary>
		/// <param name="Definition"></param>
		/// <param name="Text"></param>
		public void AppendStringToPublicDefinition(string Definition, string Text)
		{
			string WithEquals = Definition + "=";
			for (int Index=0; Index < PublicDefinitions.Count; Index++)
			{
				if (PublicDefinitions[Index].StartsWith(WithEquals))
				{
					PublicDefinitions[Index] = PublicDefinitions[Index] + Text;
					return;
				}
			}

			// if we get here, we need to make a new entry
			PublicDefinitions.Add(Definition + "=" + Text);
		}

		/// <summary>
		/// Addition modules this module may require at run-time 
		/// </summary>
		public List<string> DynamicallyLoadedModuleNames = new List<string>();

		/// <summary>
		/// Extra modules this module may require at run time, that are on behalf of another platform (i.e. shader formats and the like)
		/// </summary>
		[Obsolete("PlatformSpecificDynamicallyLoadedModuleNames is deprecated; use DynamicallyLoadedModuleNames instead")]
		public List<string> PlatformSpecificDynamicallyLoadedModuleNames
		{
			get { return DynamicallyLoadedModuleNames; }
		}

		/// <summary>
		/// List of files which this module depends on at runtime. These files will be staged along with the target.
		/// </summary>
		public RuntimeDependencyList RuntimeDependencies = new RuntimeDependencyList();

		/// <summary>
		/// List of additional properties to be added to the build receipt
		/// </summary>
		public ReceiptPropertyList AdditionalPropertiesForReceipt = new ReceiptPropertyList();

		/// <summary>
		/// Which targets this module should be precompiled for
		/// </summary>
		public PrecompileTargetsType PrecompileForTargets = PrecompileTargetsType.Default;

		/// <summary>
		/// External files which invalidate the makefile if modified. Relative paths are resolved relative to the .build.cs file.
		/// </summary>
		public List<string> ExternalDependencies = new List<string>();

		/// <summary>
		/// Whether this module requires the IMPLEMENT_MODULE macro to be implemented. Most UE4 modules require this, since we use the IMPLEMENT_MODULE macro
		/// to do other global overloads (eg. operator new/delete forwarding to GMalloc).
		/// </summary>
		public bool? bRequiresImplementModule;

		/// <summary>
		/// Whether this module qualifies included headers from other modules relative to the root of their 'Public' folder. This reduces the number
		/// of search paths that have to be passed to the compiler, improving performance and reducing the length of the compiler command line.
		/// </summary>
		public bool? bLegacyPublicIncludePaths;

		/// <summary>
		/// Which stanard to use for compiling this module
		/// </summary>
		public CppStandardVersion CppStandard = CppStandardVersion.Default;

		/// <summary>
		///  Control visibility of symbols
		/// </summary>
		public SymbolVisibility ModuleSymbolVisibility = ModuleRules.SymbolVisibility.Default;

		/// <summary>
		/// The current engine directory
		/// </summary>
		public string EngineDirectory
		{
			get
			{
				return UnrealBuildTool.EngineDirectory.FullName;
			}
		}

		/// <summary>
		/// Property for the directory containing this plugin. Useful for adding paths to third party dependencies.
		/// </summary>
		public string PluginDirectory
		{
			get
			{
				if(Plugin == null)
				{
					throw new BuildException("Module '{0}' does not belong to a plugin; PluginDirectory property is invalid.", Name);
				}
				else
				{
					return Plugin.Directory.FullName;
				}
			}
		}

		/// <summary>
		/// Property for the directory containing this module. Useful for adding paths to third party dependencies.
		/// </summary>
		public string ModuleDirectory
		{
			get
			{
				return Directory.FullName;
			}
		}

		/// <summary>
		/// Constructor. For backwards compatibility while the parameterless constructor is being phased out, initialization which would happen here is done by 
		/// RulesAssembly.CreateModulRules instead.
		/// </summary>
		/// <param name="Target">Rules for building this target</param>
		public ModuleRules(ReadOnlyTargetRules Target)
		{
			this.Target = Target;
		}

		/// <summary>
		/// Add the given Engine ThirdParty modules as static private dependencies
		///	Statically linked to this module, meaning they utilize exports from the other module
		///	Private, meaning the include paths for the included modules will not be exposed when giving this modules include paths
		///	NOTE: There is no AddThirdPartyPublicStaticDependencies function.
		/// </summary>
		/// <param name="Target">The target this module belongs to</param>
		/// <param name="ModuleNames">The names of the modules to add</param>
		public void AddEngineThirdPartyPrivateStaticDependencies(ReadOnlyTargetRules Target, params string[] ModuleNames)
		{
			if (!bUsePrecompiled || Target.LinkType == TargetLinkType.Monolithic)
			{
				PrivateDependencyModuleNames.AddRange(ModuleNames);
			}
		}

		/// <summary>
		/// Add the given Engine ThirdParty modules as dynamic private dependencies
		///	Dynamically linked to this module, meaning they do not utilize exports from the other module
		///	Private, meaning the include paths for the included modules will not be exposed when giving this modules include paths
		///	NOTE: There is no AddThirdPartyPublicDynamicDependencies function.
		/// </summary>
		/// <param name="Target">Rules for the target being built</param>
		/// <param name="ModuleNames">The names of the modules to add</param>
		public void AddEngineThirdPartyPrivateDynamicDependencies(ReadOnlyTargetRules Target, params string[] ModuleNames)
		{
			if (!bUsePrecompiled || Target.LinkType == TargetLinkType.Monolithic)
			{
				PrivateIncludePathModuleNames.AddRange(ModuleNames);
				DynamicallyLoadedModuleNames.AddRange(ModuleNames);
			}
		}

		/// <summary>
		/// Setup this module for physics support (based on the settings in UEBuildConfiguration)
		/// </summary>
		public void EnableMeshEditorSupport(ReadOnlyTargetRules Target)
		{
			if (Target.bEnableMeshEditor == true)
			{
				PublicDefinitions.Add("ENABLE_MESH_EDITOR=1");
			}
			else
			{
				PublicDefinitions.Add("ENABLE_MESH_EDITOR=0");
			}
		}

		/// <summary>
		/// Setup this module for physics support (based on the settings in UEBuildConfiguration)
		/// </summary>
		public void SetupModulePhysicsSupport(ReadOnlyTargetRules Target)
		{
			PublicIncludePathModuleNames.Add("PhysicsCore");
			PublicDependencyModuleNames.Add("PhysicsCore");

			bool bUseNonPhysXInterface = Target.bUseChaos == true || Target.bCompileImmediatePhysics == true;

            // 
            if (Target.bCompileChaos == true || Target.bUseChaos == true)
            {
                PublicDefinitions.Add("INCLUDE_CHAOS=1");
                PublicIncludePathModuleNames.AddRange(
                    new string[] {
                        "Chaos",
						"FieldSystemCore"
                    }
                );
                PublicDependencyModuleNames.AddRange(
                  new string[] {
                        "Chaos",
						"FieldSystemCore"
                  }
                );
            }
            else
            {
                PublicDefinitions.Add("INCLUDE_CHAOS=0");
            }
            // definitions used outside of PhysX/APEX need to be set here, not in PhysX.Build.cs or APEX.Build.cs, 
            // since we need to make sure we always set it, even to 0 (because these are Private dependencies, the
            // defines inside their Build.cs files won't leak out)
            if (Target.bCompilePhysX == true)
			{
				PrivateDependencyModuleNames.Add("PhysX");
				PublicDefinitions.Add("WITH_PHYSX=1");
			}
			else
			{
				PublicDefinitions.Add("WITH_PHYSX=0");
			}

			if(!bUseNonPhysXInterface)
			{
				// Disable non-physx interfaces
				PublicDefinitions.Add("WITH_CHAOS=0");

				// 
				// WITH_CHAOS_NEEDS_TO_BE_FIXED
				//
				// Anything wrapped in this define needs to be fixed
				// in one of the build targets. This define was added
				// to help identify complier failures between the
				// the three build targets( UseChaos, PhysX, WithChaos )
				// This defaults to off , and will be enabled for bUseChaos. 
				// This define should be removed when all the references 
				// have been fixed across the different builds. 
				//
				PublicDefinitions.Add("WITH_CHAOS_NEEDS_TO_BE_FIXED=0");

				PublicDefinitions.Add("PHYSICS_INTERFACE_LLIMMEDIATE=0");

				if (Target.bCompilePhysX)
				{
					PublicDefinitions.Add("PHYSICS_INTERFACE_PHYSX=1");
				}
				else
				{
					PublicDefinitions.Add("PHYSICS_INTERFACE_PHYSX=0");
				}

				if (Target.bCompileAPEX == true)
				{
					if (!Target.bCompilePhysX)
					{
						throw new BuildException("APEX is enabled, without PhysX. This is not supported!");
					}
					PrivateDependencyModuleNames.Add("APEX");
					PublicDefinitions.Add("WITH_APEX=1");

// @MIXEDREALITY_CHANGE : BEGIN - Do not use Apex Cloth for HoloLens.  TODO: can we enable this in the future?
				if (Target.Platform == UnrealTargetPlatform.HoloLens)
				{
					PublicDefinitions.Add("WITH_APEX_CLOTHING=0");
					PublicDefinitions.Add("WITH_CLOTH_COLLISION_DETECTION=0");
				}
				else
				{
					PublicDefinitions.Add("WITH_APEX_CLOTHING=1");
					PublicDefinitions.Add("WITH_CLOTH_COLLISION_DETECTION=1");
				}
// @MIXEDREALITY_CHANGE : END

				PublicDefinitions.Add("WITH_PHYSX_COOKING=1");  // APEX currently relies on cooking even at runtime

				}
				else
				{
					PublicDefinitions.Add("WITH_APEX=0");
					PublicDefinitions.Add("WITH_APEX_CLOTHING=0");
					PublicDefinitions.Add("WITH_CLOTH_COLLISION_DETECTION=0");
					PublicDefinitions.Add(string.Format("WITH_PHYSX_COOKING={0}", Target.bBuildEditor && Target.bCompilePhysX ? 1 : 0));  // without APEX, we only need cooking in editor builds
				}

				if (Target.bCompileNvCloth == true)
				{
					if (!Target.bCompilePhysX)
					{
						throw new BuildException("NvCloth is enabled, without PhysX. This is not supported!");
					}

					PrivateDependencyModuleNames.Add("NvCloth");
					PublicDefinitions.Add("WITH_NVCLOTH=1");

				}
				else
				{
					PublicDefinitions.Add("WITH_NVCLOTH=0");
				}
			}
			else
			{
				// Disable apex/cloth/physx interface
				PublicDefinitions.Add("PHYSICS_INTERFACE_PHYSX=0");
				PublicDefinitions.Add("WITH_APEX=0");
				PublicDefinitions.Add("WITH_APEX_CLOTHING=0");
				PublicDefinitions.Add("WITH_CLOTH_COLLISION_DETECTION=0");
				PublicDefinitions.Add(string.Format("WITH_PHYSX_COOKING={0}", Target.bBuildEditor && Target.bCompilePhysX ? 1 : 0));  // without APEX, we only need cooking in editor builds
				PublicDefinitions.Add("WITH_NVCLOTH=0");

				if(Target.bUseChaos)
				{
					PublicDefinitions.Add("WITH_CHAOS=1");
					PublicDefinitions.Add("WITH_CHAOS_NEEDS_TO_BE_FIXED=1");
					PublicDefinitions.Add("COMPILE_ID_TYPES_AS_INTS=0");
					
					PublicIncludePathModuleNames.AddRange(
						new string[] {
						"Chaos",
						}
					);

					PublicDependencyModuleNames.AddRange(
						new string[] {
						"Chaos",
						}
					);
				}
				else
				{
					PublicDefinitions.Add("WITH_CHAOS=0");
					PublicDefinitions.Add("WITH_CHAOS_NEEDS_TO_BE_FIXED=0");
				}

				if(Target.bCompileImmediatePhysics)
				{
					PublicDefinitions.Add("PHYSICS_INTERFACE_LLIMMEDIATE=1");
					PublicDependencyModuleNames.Add("PhysX");
				}
				else
				{
					PublicDefinitions.Add("PHYSICS_INTERFACE_LLIMMEDIATE=0");
				}
			}

			if(Target.bCustomSceneQueryStructure)
			{
				PublicDefinitions.Add("WITH_CUSTOM_SQ_STRUCTURE=1");
			}
			else
			{
				PublicDefinitions.Add("WITH_CUSTOM_SQ_STRUCTURE=0");
			}

			// Unused interface
			PublicDefinitions.Add("WITH_IMMEDIATE_PHYSX=0");
		}

		/// <summary>
		/// Determines if this module can be precompiled for the current target.
		/// </summary>
		/// <param name="RulesFile">Path to the module rules file</param>
		/// <returns>True if the module can be precompiled, false otherwise</returns>
		internal bool IsValidForTarget(FileReference RulesFile)
		{
			if(Type == ModuleRules.ModuleType.CPlusPlus)
			{
				switch (PrecompileForTargets)
				{
					case ModuleRules.PrecompileTargetsType.None:
						return false;
					case ModuleRules.PrecompileTargetsType.Default:
						return (Target.Type == TargetType.Editor || !RulesFile.IsUnderDirectory(UnrealBuildTool.EngineSourceDeveloperDirectory) || Plugin != null);
					case ModuleRules.PrecompileTargetsType.Game:
						return (Target.Type == TargetType.Client || Target.Type == TargetType.Server || Target.Type == TargetType.Game);
					case ModuleRules.PrecompileTargetsType.Editor:
						return (Target.Type == TargetType.Editor);
					case ModuleRules.PrecompileTargetsType.Any:
						return true;
				}
			}
			return false;
		}

		/// <summary>
		/// Returns the module directory for a given subclass of the module (platform extensions add subclasses of ModuleRules to add in platform-specific settings)
		/// </summary>
		/// <param name="Type">typeof the subclass</param>
		/// <returns>Directory where the subclass's .Build.cs lives, or null if not found</returns>
		public DirectoryReference GetModuleDirectoryForSubClass(Type Type)
		{
			if (DirectoriesForModuleSubClasses == null)
			{
				return null;
			}

			DirectoryReference Directory;
			if (DirectoriesForModuleSubClasses.TryGetValue(Type, out Directory))
			{
				return Directory;
			}
			return null;
		}

		/// <summary>
		/// Returns the directories for all subclasses of this module
		/// </summary>
		/// <returns>List of directories, or null if none were added</returns>
		public DirectoryReference[] GetModuleDirectoriesForAllSubClasses()
		{
			return DirectoriesForModuleSubClasses == null ? null : DirectoriesForModuleSubClasses.Values.ToArray();
		}
	}
}
