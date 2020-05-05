// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.Diagnostics;
using System.IO;
using Microsoft.Win32;
using System.Linq;
using Tools.DotNETCommon;
using Microsoft.VisualStudio.Setup.Configuration;
using System.Runtime.InteropServices;

namespace UnrealBuildTool
{
	/// <summary>
	/// Available compiler toolchains on Windows platform
	/// </summary>
	public enum WindowsCompiler
	{
		/// <summary>
		/// Use the default compiler. A specific value will always be used outside of configuration classes.
		/// </summary>
		Default,

		/// <summary>
		/// Use Clang for Windows, using the clang-cl driver.
		/// </summary>
		Clang,

		/// <summary>
		/// Use the Intel C++ compiler
		/// </summary>
		Intel,

		/// <summary>
		/// Visual Studio 2015 (Visual C++ 14.0)
		/// </summary>
		VisualStudio2015_DEPRECATED,

		/// <summary>
		/// Visual Studio 2015 (Visual C++ 14.0)
		/// </summary>
		[Obsolete("UE4 does not support building Visual Studio 2015 targets from the 4.22 release onwards.")]
		VisualStudio2015 = VisualStudio2015_DEPRECATED,

		/// <summary>
		/// Visual Studio 2017 (Visual C++ 15.0)
		/// </summary>
		VisualStudio2017,

		/// <summary>
		/// Visual Studio 2019 (Visual C++ 16.0)
		/// </summary>
		VisualStudio2019,
	}

	/// <summary>
	/// Which static analyzer to use
	/// </summary>
	public enum WindowsStaticAnalyzer
	{
		/// <summary>
		/// Do not perform static analysis
		/// </summary>
		None,

		/// <summary>
		/// Use the built-in Visual C++ static analyzer
		/// </summary>
		VisualCpp,

		/// <summary>
		/// Use PVS-Studio for static analysis
		/// </summary>
		PVSStudio,
	}

	/// <summary>
	/// Available architectures on Windows platform
	/// </summary>
	public enum WindowsArchitecture
	{
		/// <summary>
		/// x86
		/// </summary>
		x86,
		/// <summary>
		/// x64
		/// </summary>
		x64,
		/// <summary>
		/// ARM
		/// </summary>
		ARM32,
		/// <summary>
		/// ARM64
		/// </summary>
		ARM64,
	}

	/// <summary>
	/// Windows-specific target settings
	/// </summary>
	public class WindowsTargetRules
	{
		/// <summary>
		/// The target rules which owns this object. Used to resolve some properties.
		/// </summary>
		TargetRules Target;

		/// <summary>
		/// Version of the compiler toolchain to use on Windows platform. A value of "default" will be changed to a specific version at UBT start up.
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/WindowsTargetPlatform.WindowsTargetSettings", "CompilerVersion")]
		[XmlConfigFile(Category = "WindowsPlatform")]
		[CommandLine("-2015", Value = "VisualStudio2015")]
		[CommandLine("-2017", Value = "VisualStudio2017")]
		[CommandLine("-2019", Value = "VisualStudio2019")]
		public WindowsCompiler Compiler = WindowsCompiler.Default;

		/// <summary>
		/// Architecture of Target.
		/// </summary>
		public WindowsArchitecture Architecture
		{
			get;
			internal set;
		}
		= WindowsArchitecture.x64;

		/// <summary>
		/// The specific toolchain version to use. This may be a specific version number (for example, "14.13.26128"), or the string "Latest", to select the newest available version. By default, and if it is available, we use the
		/// toolchain version indicated by WindowsPlatform.DefaultToolChainVersion (otherwise, we use the latest version).
		/// </summary>
		[XmlConfigFile(Category = "WindowsPlatform")]
		public string CompilerVersion = null;

		/// <summary>
		/// The specific Windows SDK version to use. This may be a specific version number (for example, "8.1", "10.0" or "10.0.10150.0"), or the string "Latest", to select the newest available version.
		/// By default, and if it is available, we use the Windows SDK version indicated by WindowsPlatform.DefaultWindowsSdkVersion (otherwise, we use the latest version).
		/// </summary>
		[XmlConfigFile(Category = "WindowsPlatform")]
		public string WindowsSdkVersion = null;

		/// <summary>
		/// Value for the WINVER macro, defining the minimum supported Windows version.
		/// </summary>
		public int TargetWindowsVersion = 0x601;

		/// <summary>
		/// Enable PIX debugging (automatically disabled in Shipping and Test configs)
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/WindowsTargetPlatform.WindowsTargetSettings", "bEnablePIXProfiling")]
		public bool bPixProfilingEnabled = true;

		/// <summary>
		/// Enable building with the Win10 SDK instead of the older Win8.1 SDK 
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/WindowsTargetPlatform.WindowsTargetSettings", "bUseWindowsSDK10")]
		public bool bUseWindowsSDK10 = false;

		/// <summary>
		/// Enables runtime ray tracing support.
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/WindowsTargetPlatform.WindowsTargetSettings", "bEnableRayTracing")]
		public bool bEnableRayTracing = false;

		/// <summary>
		/// The name of the company (author, provider) that created the project.
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Game, "/Script/EngineSettings.GeneralProjectSettings", "CompanyName")]
		public string CompanyName;

		/// <summary>
		/// The project's copyright and/or trademark notices.
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Game, "/Script/EngineSettings.GeneralProjectSettings", "CopyrightNotice")]
		public string CopyrightNotice;

		/// <summary>
		/// The product name.
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Game, "/Script/EngineSettings.GeneralProjectSettings", "ProjectName")]
		public string ProductName;

		/// <summary>
		/// The static analyzer to use.
		/// </summary>
		[XmlConfigFile(Category = "WindowsPlatform")]
		[CommandLine("-StaticAnalyzer")]
		public WindowsStaticAnalyzer StaticAnalyzer = WindowsStaticAnalyzer.None;

		/// <summary>
		/// Whether we should export a file containing .obj to source file mappings.
		/// </summary>
		[XmlConfigFile]
		[CommandLine("-ObjSrcMap")]
		public string ObjSrcMapFile = null;

		/// <summary>
		/// Provides a Module Definition File (.def) to the linker to describe various attributes of a DLL.
		/// Necessary when exporting functions by ordinal values instead of by name.
		/// </summary>
		public string ModuleDefinitionFile;

		/// <summary>
		/// Specifies the path to a manifest file for the linker to embed. Defaults to the manifest in Engine/Build/Windows/Resources. Can be assigned to null
		/// if the target wants to specify its own manifest.
		/// </summary>
		public string ManifestFile;

		/// <summary>
		/// Enables strict standard conformance mode (/permissive-) in VS2017+.
		/// </summary>
		[XmlConfigFile(Category = "WindowsPlatform")]
		[CommandLine("-Strict")]
		public bool bStrictConformanceMode = false; 

		/// VS2015 updated some of the CRT definitions but not all of the Windows SDK has been updated to match.
		/// Microsoft provides legacy_stdio_definitions library to enable building with VS2015 until they fix everything up.
		public bool bNeedsLegacyStdioDefinitionsLib
		{
			get { return Compiler == WindowsCompiler.VisualStudio2015_DEPRECATED || Compiler == WindowsCompiler.VisualStudio2017 || Compiler == WindowsCompiler.VisualStudio2019 || Compiler == WindowsCompiler.Clang; }
		}

		/// <summary>
		/// The stack size when linking
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/WindowsTargetPlatform.WindowsTargetSettings")]
		public int DefaultStackSize = 12000000;

		/// <summary>
		/// The stack size to commit when linking
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/WindowsTargetPlatform.WindowsTargetSettings")]
		public int DefaultStackSizeCommit;

		/// <summary>
		/// Determines the amount of memory that the compiler allocates to construct precompiled headers (/Zm).
		/// </summary>
		[XmlConfigFile(Category = "WindowsPlatform")]
		public int PCHMemoryAllocationFactor = 0;

		/// <summary>
		/// True if we allow using addresses larger than 2GB on 32 bit builds
		/// </summary>
		public bool bBuildLargeAddressAwareBinary = true;

		/// <summary>
		/// Create an image that can be hot patched (/FUNCTIONPADMIN)
		/// </summary>
		public bool bCreateHotPatchableImage
		{
			get { return bCreateHotPatchableImagePrivate ?? Target.bWithLiveCoding; }
			set { bCreateHotPatchableImagePrivate = true; }
		}
		private bool? bCreateHotPatchableImagePrivate;

		/// <summary>
		/// Strip unreferenced symbols (/OPT:REF)
		/// </summary>
		public bool bStripUnreferencedSymbols
		{
			get { return bStripUnreferencedSymbolsPrivate ?? ((Target.Configuration == UnrealTargetConfiguration.Test || Target.Configuration == UnrealTargetConfiguration.Shipping) && !Target.bWithLiveCoding); }
			set { bStripUnreferencedSymbolsPrivate = value; }
		}
		private bool? bStripUnreferencedSymbolsPrivate;
			
		/// <summary>
		/// Merge identical COMDAT sections together (/OPT:ICF)
		/// </summary>
		public bool bMergeIdenticalCOMDATs
		{
			get { return bMergeIdenticalCOMDATsPrivate ?? ((Target.Configuration == UnrealTargetConfiguration.Test || Target.Configuration == UnrealTargetConfiguration.Shipping) && !Target.bWithLiveCoding); }
			set { bMergeIdenticalCOMDATsPrivate = value; }
		}
		private bool? bMergeIdenticalCOMDATsPrivate;

		/// <summary>
		/// Whether to put global symbols in their own sections (/Gw), allowing the linker to discard any that are unused.
		/// </summary>
		public bool bOptimizeGlobalData = true;

		/// <summary>
		/// (Experimental) Appends the -ftime-trace argument to the command line for Clang to output a JSON file containing a timeline for the compile. 
		/// See http://aras-p.info/blog/2019/01/16/time-trace-timeline-flame-chart-profiler-for-Clang/ for more info.
		/// </summary>
		[XmlConfigFile(Category = "WindowsPlatform")]
		public bool bClangTimeTrace = false;

		/// <summary>
		/// Outputs compile timing information so that it can be analyzed.
		/// </summary>
		[XmlConfigFile(Category = "WindowsPlatform")]
		public bool bCompilerTrace = false;

		/// <summary>
		/// Print out files that are included by each source file
		/// </summary>
		[CommandLine("-ShowIncludes")]
		[XmlConfigFile(Category = "WindowsPlatform")]
		public bool bShowIncludes = false;

		/// <summary>
		/// Bundle a working version of dbghelp.dll with the application, and use this to generate minidumps. This works around a bug with the Windows 10 Fall Creators Update (1709)
		/// where rich PE headers larger than a certain size would result in corrupt minidumps.
		/// </summary>
		public bool bUseBundledDbgHelp = true;

		/// <summary>
		/// Settings for PVS studio
		/// </summary>
		public PVSTargetSettings PVS = new PVSTargetSettings();

		/// <summary>
		/// The Visual C++ environment to use for this target. Only initialized after all the target settings are finalized, in ValidateTarget().
		/// </summary>
		internal VCEnvironment Environment;

		/// <summary>
		/// Directory containing the toolchain
		/// </summary>
		public string ToolChainDir
		{
			get { return (Environment == null)? null : Environment.ToolChainDir.FullName; }
		}

		/// <summary>
		/// The version number of the toolchain
		/// </summary>
		public string ToolChainVersion
		{
			get { return (Environment == null)? null : Environment.ToolChainVersion.ToString(); }
		}

		/// <summary>
		/// Root directory containing the Windows Sdk
		/// </summary>
		public string WindowsSdkDir
		{
			get { return (Environment == null)? null : Environment.WindowsSdkDir.FullName; }
		}

		/// <summary>
		/// Directory containing the DIA SDK
		/// </summary>
		public string DiaSdkDir
		{
			get { return WindowsPlatform.FindDiaSdkDirs(Environment.Compiler).Select(x => x.FullName).FirstOrDefault(); }
		}

		/// <summary>
		/// When using a Visual Studio compiler, returns the version name as a string
		/// </summary>
		/// <returns>The Visual Studio compiler version name (e.g. "2015")</returns>
		public string GetVisualStudioCompilerVersionName()
		{
			switch (Compiler)
			{
				case WindowsCompiler.Clang:
				case WindowsCompiler.Intel:
				case WindowsCompiler.VisualStudio2015_DEPRECATED:
				case WindowsCompiler.VisualStudio2017:
				case WindowsCompiler.VisualStudio2019:
					return "2015"; // VS2017 is backwards compatible with VS2015 compiler

				default:
					throw new BuildException("Unexpected WindowsCompiler version for GetVisualStudioCompilerVersionName().  Either not using a Visual Studio compiler or switch block needs to be updated");
			}
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Target">The target rules which owns this object</param>
		internal WindowsTargetRules(TargetRules Target)
		{
			this.Target = Target;

			ManifestFile = FileReference.Combine(UnrealBuildTool.EngineDirectory, "Build", "Windows", "Resources", String.Format("Default-{0}.manifest", Target.Platform)).FullName;
		}
	}

	/// <summary>
	/// Read-only wrapper for Windows-specific target settings
	/// </summary>
	public class ReadOnlyWindowsTargetRules
	{
		/// <summary>
		/// The private mutable settings object
		/// </summary>
		private WindowsTargetRules Inner;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Inner">The settings object to wrap</param>
		public ReadOnlyWindowsTargetRules(WindowsTargetRules Inner)
		{
			this.Inner = Inner;
			this.PVS = new ReadOnlyPVSTargetSettings(Inner.PVS);
		}

		/// <summary>
		/// Accessors for fields on the inner TargetRules instance
		/// </summary>
		#region Read-only accessor properties 
		#if !__MonoCS__
		#pragma warning disable CS1591
		#endif

		public WindowsCompiler Compiler
		{
			get { return Inner.Compiler; }
		}
		
		public WindowsArchitecture Architecture
		{
			get { return Inner.Architecture; }
		}

		public string CompilerVersion
		{
			get { return Inner.CompilerVersion; }
		}

		public string WindowsSdkVersion
		{
			get { return Inner.WindowsSdkVersion; }
		}

		public int TargetWindowsVersion
		{
			get { return Inner.TargetWindowsVersion; }
		}

		public bool bPixProfilingEnabled
		{
			get { return Inner.bPixProfilingEnabled; }
		}

		public bool bUseWindowsSDK10
		{
			get { return Inner.bUseWindowsSDK10; }
		}

		public bool bEnableRayTracing
		{
			get { return Inner.bEnableRayTracing; }
		}

		public string CompanyName
		{
			get { return Inner.CompanyName; }
		}

		public string CopyrightNotice
		{
			get { return Inner.CopyrightNotice; }
		}

		public string ProductName
		{
			get { return Inner.ProductName; }
		}

		public WindowsStaticAnalyzer StaticAnalyzer
		{
			get { return Inner.StaticAnalyzer; }
		}

		public string ObjSrcMapFile
		{
			get { return Inner.ObjSrcMapFile; }
		}

		public string ModuleDefinitionFile
		{
			get { return Inner.ModuleDefinitionFile; }
		}

		public string ManifestFile
		{
			get { return Inner.ManifestFile; }
		}

		public bool bNeedsLegacyStdioDefinitionsLib
		{
			get { return Inner.bNeedsLegacyStdioDefinitionsLib; }
		}

		public bool bStrictConformanceMode
		{
			get { return Inner.bStrictConformanceMode; }
		}

		public int DefaultStackSize
		{
			get { return Inner.DefaultStackSize; }
		}

		public int DefaultStackSizeCommit
		{
			get { return Inner.DefaultStackSizeCommit; }
		}

		public int PCHMemoryAllocationFactor
		{
			get { return Inner.PCHMemoryAllocationFactor; }
		}

		public bool bBuildLargeAddressAwareBinary
		{
			get { return Inner.bBuildLargeAddressAwareBinary; }
		}

		public bool bCreateHotpatchableImage
		{
			get { return Inner.bCreateHotPatchableImage; }
		}

		public bool bStripUnreferencedSymbols
		{
			get { return Inner.bStripUnreferencedSymbols; }
		}

		public bool bMergeIdenticalCOMDATs
		{
			get { return Inner.bMergeIdenticalCOMDATs; }
		}

		public bool bOptimizeGlobalData
		{
			get { return Inner.bOptimizeGlobalData; }
		}

		public bool bClangTimeTrace
		{
			get { return Inner.bClangTimeTrace; }
		}

		public bool bCompilerTrace
		{
			get { return Inner.bCompilerTrace; }
		}

		public bool bShowIncludes
		{
			get { return Inner.bShowIncludes; }
		}

		public string GetVisualStudioCompilerVersionName()
		{
			return Inner.GetVisualStudioCompilerVersionName();
		}

		public bool bUseBundledDbgHelp
		{
			get { return Inner.bUseBundledDbgHelp; }
		}

		public ReadOnlyPVSTargetSettings PVS
		{
			get; private set;
		}

		internal VCEnvironment Environment
		{
			get { return Inner.Environment; }
		}

		public string ToolChainDir
		{
			get { return Inner.ToolChainDir; }
		}

		public string ToolChainVersion
		{
			get { return Inner.ToolChainVersion; }
		}

		public string WindowsSdkDir
		{
			get { return Inner.WindowsSdkDir; }
		}

		public string DiaSdkDir
		{
			get { return Inner.DiaSdkDir; }
		}
		
		public string GetArchitectureSubpath()
		{
			return WindowsExports.GetArchitectureSubpath(Architecture);
		}

		#if !__MonoCS__
		#pragma warning restore CS1591
		#endif
		#endregion
	}

	class WindowsPlatform : UEBuildPlatform
	{
		/// <summary>
		/// The default compiler version to be used, if installed. 
		/// </summary>
		static readonly VersionNumber DefaultClangToolChainVersion = VersionNumber.Parse("9.0.0");

		/// <summary>
		/// The compiler toolchains to be used if installed, the first installed in the list will be used.
		/// </summary>
		static readonly VersionNumber[] PreferredVisualStudioToolChainVersion = new VersionNumber[]
		{
			VersionNumber.Parse("14.24.28315"), // VS2019 v16.4.3 (installed to 14.24.28314 folder)
			VersionNumber.Parse("14.22.27905"), // VS2019 v16.2.3
			VersionNumber.Parse("14.16.27023.2"), // VS2017 v15.9.15
			VersionNumber.Parse("14.16.27023"), // fallback to VS2017 15.9 toolchain, microsoft updates these in places so for local installs only this version number is present
		};

		/// <summary>
		/// The default Windows SDK version to be used, if installed.
		/// </summary>
		static readonly VersionNumber[] PreferredWindowsSdkVersions = new VersionNumber[]
		{
			VersionNumber.Parse("10.0.18362.0"),
			VersionNumber.Parse("10.0.16299.0")
		};

		/// <summary>
		/// Cache of Visual Studio installation directories
		/// </summary>
		private static Dictionary<WindowsCompiler, List<DirectoryReference>> CachedVSInstallDirs = new Dictionary<WindowsCompiler, List<DirectoryReference>>();

		/// <summary>
		/// Cache of Visual C++ installation directories
		/// </summary>
		private static Dictionary<WindowsCompiler, Dictionary<VersionNumber, DirectoryReference>> CachedToolChainDirs = new Dictionary<WindowsCompiler, Dictionary<VersionNumber, DirectoryReference>>();

		/// <summary>
		/// Cache of DIA SDK installation directories
		/// </summary>
		private static Dictionary<WindowsCompiler, List<DirectoryReference>> CachedDiaSdkDirs = new Dictionary<WindowsCompiler, List<DirectoryReference>>();

		/// <summary>
		/// Cache of Windows SDK installation directories
		/// </summary>
		private static IReadOnlyDictionary<VersionNumber, DirectoryReference> CachedWindowsSdkDirs;

		/// <summary>
		/// Cache of Universal CRT installation directories
		/// </summary>
		private static IReadOnlyDictionary<VersionNumber, DirectoryReference> CachedUniversalCrtDirs;

		/// <summary>
		/// True if we should use the Clang linker (LLD) when we are compiling with Clang, otherwise we use the MSVC linker
		/// </summary>
		public static readonly bool bAllowClangLinker = false;

		/// <summary>
		/// True if we should use the Intel linker (xilink) when we are compiling with ICL, otherwise we use the MSVC linker
		/// </summary>
		public static readonly bool bAllowICLLinker = true;

		/// <summary>
		/// True if we allow using addresses larger than 2GB on 32 bit builds
		/// </summary>
		public static readonly bool bBuildLargeAddressAwareBinary = true;

		WindowsPlatformSDK SDK;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="InPlatform">Creates a windows platform with the given enum value</param>
		/// <param name="InSDK">The installed Windows SDK</param>
		public WindowsPlatform(UnrealTargetPlatform InPlatform, WindowsPlatformSDK InSDK)
			: base(InPlatform)
		{
			SDK = InSDK;
		}

		/// <summary>
		/// Whether the required external SDKs are installed for this platform. Could be either a manual install or an AutoSDK.
		/// </summary>
		public override SDKStatus HasRequiredSDKsInstalled()
		{
			return SDK.HasRequiredSDKsInstalled();
		}

		/// <summary>
		/// Reset a target's settings to the default
		/// </summary>
		/// <param name="Target"></param>
		public override void ResetTarget(TargetRules Target)
		{
			base.ResetTarget(Target);
		}

		/// <summary>
		/// Creates the VCEnvironment object used to control compiling and other tools. Virtual to allow other platforms to override behavior
		/// </summary>
		/// <param name="Target">Stanard target object</param>
		/// <returns></returns>
		protected virtual VCEnvironment CreateVCEnvironment(TargetRules Target)
		{
			return VCEnvironment.Create(Target.WindowsPlatform.Compiler, Platform, Target.WindowsPlatform.Architecture, Target.WindowsPlatform.CompilerVersion, Target.WindowsPlatform.WindowsSdkVersion, null);
		}

		/// <summary>
		/// Validate a target's settings
		/// </summary>
		public override void ValidateTarget(TargetRules Target)
		{
			if (Platform == UnrealTargetPlatform.HoloLens && Target.Architecture.ToLower() == "arm64")
			{
				Target.WindowsPlatform.Architecture = WindowsArchitecture.ARM64;
				Log.TraceInformation("Using Windows ARM64 architecture");
			}
			else if (Platform == UnrealTargetPlatform.Win64)
			{
				Target.WindowsPlatform.Architecture = WindowsArchitecture.x64;
			}
			else if (Platform == UnrealTargetPlatform.Win32)
			{
				Target.WindowsPlatform.Architecture = WindowsArchitecture.x86;
			}

			// Disable Simplygon support if compiling against the NULL RHI.
			if (Target.GlobalDefinitions.Contains("USE_NULL_RHI=1"))
			{				
				Target.bCompileCEF3 = false;
			}

			// Set the compiler version if necessary
			if (Target.WindowsPlatform.Compiler == WindowsCompiler.Default)
			{
				if (Target.WindowsPlatform.StaticAnalyzer == WindowsStaticAnalyzer.PVSStudio && HasCompiler(WindowsCompiler.VisualStudio2017))
				{
					Target.WindowsPlatform.Compiler = WindowsCompiler.VisualStudio2017;
				}
				else
				{
					Target.WindowsPlatform.Compiler = GetDefaultCompiler(Target.ProjectFile);
				}
			}

			// Disable linking if we're using a static analyzer
			if(Target.WindowsPlatform.StaticAnalyzer != WindowsStaticAnalyzer.None)
			{
				Target.bDisableLinking = true;
			}

			// Disable PCHs for PVS studio
			if(Target.WindowsPlatform.StaticAnalyzer == WindowsStaticAnalyzer.PVSStudio)
			{
				Target.bUsePCHFiles = false;
			}

			// Override PCH settings
			if (Target.WindowsPlatform.Compiler == WindowsCompiler.Intel)
			{
				Target.NumIncludedBytesPerUnityCPP = Math.Min(Target.NumIncludedBytesPerUnityCPP, 256 * 1024);

				Target.bUseSharedPCHs = false;

				Target.bUsePCHFiles = false;
			}

			// E&C support.
			if (Target.bSupportEditAndContinue || Target.bAdaptiveUnityEnablesEditAndContinue)
			{
				Target.bUseIncrementalLinking = true;
			}
			if (Target.bAdaptiveUnityEnablesEditAndContinue && !Target.bAdaptiveUnityDisablesPCH && !Target.bAdaptiveUnityCreatesDedicatedPCH)
			{
				throw new BuildException("bAdaptiveUnityEnablesEditAndContinue requires bAdaptiveUnityDisablesPCH or bAdaptiveUnityCreatesDedicatedPCH");
			}

			// If we're using PDB files and PCHs, the generated code needs to be compiled with the same options as the PCH.
			if ((Target.bUsePDBFiles || Target.bSupportEditAndContinue) && Target.bUsePCHFiles)
			{
				Target.bDisableDebugInfoForGeneratedCode = false;
			}

			Target.bCompileISPC = true;

			// Initialize the VC environment for the target, and set all the version numbers to the concrete values we chose
			Target.WindowsPlatform.Environment = CreateVCEnvironment(Target);

			// pull some things from it
			Target.WindowsPlatform.Compiler = Target.WindowsPlatform.Environment.Compiler;
			Target.WindowsPlatform.CompilerVersion = Target.WindowsPlatform.Environment.CompilerVersion.ToString();
			Target.WindowsPlatform.WindowsSdkVersion = Target.WindowsPlatform.Environment.WindowsSdkVersion.ToString();

//			@Todo: Still getting reports of frequent OOM issues with this enabled as of 15.7.
//			// Enable fast PDB linking if we're on VS2017 15.7 or later. Previous versions have OOM issues with large projects.
//			if(!Target.bFormalBuild && !Target.bUseFastPDBLinking.HasValue && Target.WindowsPlatform.Compiler >= WindowsCompiler.VisualStudio2017)
//			{
//				VersionNumber Version;
//				DirectoryReference ToolChainDir;
//				if(TryGetVCToolChainDir(Target.WindowsPlatform.Compiler, Target.WindowsPlatform.CompilerVersion, out Version, out ToolChainDir) && Version >= new VersionNumber(14, 14, 26316))
//				{
//					Target.bUseFastPDBLinking = true;
//				}
//			}
		}

		/// <summary>
		/// Gets the default compiler which should be used, if it's not set explicitly by the target, command line, or config file.
		/// </summary>
		/// <returns>The default compiler version</returns>
		internal static WindowsCompiler GetDefaultCompiler(FileReference ProjectFile)
		{
			// If there's no specific compiler set, try to pick the matching compiler for the selected IDE
			if(ProjectFileGeneratorSettings.Format != null)
			{
				foreach(ProjectFileFormat Format in ProjectFileGeneratorSettings.ParseFormatList(ProjectFileGeneratorSettings.Format))
				{
					if (Format == ProjectFileFormat.VisualStudio2019)
					{
						return WindowsCompiler.VisualStudio2019;
					}
					else if (Format == ProjectFileFormat.VisualStudio2017)
					{
						return WindowsCompiler.VisualStudio2017;
					}
				} 
			}

			// Also check the default format for the Visual Studio project generator
			object ProjectFormatObject;
			if (XmlConfig.TryGetValue(typeof(VCProjectFileGenerator), "Version", out ProjectFormatObject))
			{
				VCProjectFileFormat ProjectFormat = (VCProjectFileFormat)ProjectFormatObject;
				if (ProjectFormat == VCProjectFileFormat.VisualStudio2019)
				{
					return WindowsCompiler.VisualStudio2019;
				}
				else if (ProjectFormat == VCProjectFileFormat.VisualStudio2017)
				{
					return WindowsCompiler.VisualStudio2017;
				}
			}

			// Check the editor settings too
			ProjectFileFormat PreferredAccessor;
			if(ProjectFileGenerator.GetPreferredSourceCodeAccessor(ProjectFile, out PreferredAccessor))
			{
				if(PreferredAccessor == ProjectFileFormat.VisualStudio2019)
			    {
				    return WindowsCompiler.VisualStudio2019;
			    }
				else if(PreferredAccessor == ProjectFileFormat.VisualStudio2017)
			    {
				    return WindowsCompiler.VisualStudio2017;
			    }
			}

			// Second, default based on what's installed, test for 2015 first
			if (HasCompiler(WindowsCompiler.VisualStudio2019))
			{
				return WindowsCompiler.VisualStudio2019;
			}
			if (HasCompiler(WindowsCompiler.VisualStudio2017))
			{
				return WindowsCompiler.VisualStudio2017;
			}

			// If we do have a Visual Studio installation, but we're missing just the C++ parts, warn about that.
			DirectoryReference VSInstallDir;
			if (TryGetVSInstallDir(WindowsCompiler.VisualStudio2019, out VSInstallDir))
			{
				Log.TraceWarning("Visual Studio 2019 is installed, but is missing the C++ toolchain. Please verify that the \"VC++ 2019 toolset\" component is selected in the Visual Studio 2019 installation options.");
			}
			else if (TryGetVSInstallDir(WindowsCompiler.VisualStudio2017, out VSInstallDir))
			{
				Log.TraceWarning("Visual Studio 2017 is installed, but is missing the C++ toolchain. Please verify that the \"VC++ 2017 toolset\" component is selected in the Visual Studio 2017 installation options.");
			}
			else
			{
				Log.TraceWarning("No Visual C++ installation was found. Please download and install Visual Studio 2017 with C++ components.");
			}

			// Finally, default to VS2019 anyway
			return WindowsCompiler.VisualStudio2019;
		}

		/// <summary>
		/// Returns the human-readable name of the given compiler
		/// </summary>
		/// <param name="Compiler">The compiler value</param>
		/// <returns>Name of the compiler</returns>
		public static string GetCompilerName(WindowsCompiler Compiler)
		{
			switch (Compiler)
			{
				case WindowsCompiler.VisualStudio2015_DEPRECATED:
					return "Visual Studio 2015";
				case WindowsCompiler.VisualStudio2017:
					return "Visual Studio 2017";
				case WindowsCompiler.VisualStudio2019:
					return "Visual Studio 2019";
				default:
					return Compiler.ToString();
			}
		}

		/// <summary>
		/// Get the first Visual Studio install directory for the given compiler version. Note that it is possible for the compiler toolchain to be installed without
		/// Visual Studio.
		/// </summary>
		/// <param name="Compiler">Version of the toolchain to look for.</param>
		/// <param name="InstallDir">On success, the directory that Visual Studio is installed to.</param>
		/// <returns>True if the directory was found, false otherwise.</returns>
		public static bool TryGetVSInstallDir(WindowsCompiler Compiler, out DirectoryReference InstallDir)
		{
			if (BuildHostPlatform.Current.Platform != UnrealTargetPlatform.Win64 && BuildHostPlatform.Current.Platform != UnrealTargetPlatform.Win32)
			{
				InstallDir = null;
				return false;
			}

			List<DirectoryReference> InstallDirs = FindVSInstallDirs(Compiler);
			if(InstallDirs.Count == 0)
			{
				InstallDir = null;
				return false;
			}
			else
			{
				InstallDir = InstallDirs[0];
				return true;
			}
		}

		/// <summary>
		/// Read the Visual Studio install directory for the given compiler version. Note that it is possible for the compiler toolchain to be installed without
		/// Visual Studio.
		/// </summary>
		/// <param name="Compiler">Version of the toolchain to look for.</param>
		/// <returns>List of directories containing Visual Studio installations</returns>
		public static List<DirectoryReference> FindVSInstallDirs(WindowsCompiler Compiler)
		{
			List<DirectoryReference> InstallDirs;
			if(!CachedVSInstallDirs.TryGetValue(Compiler, out InstallDirs))
			{
				InstallDirs = new List<DirectoryReference>();
			    if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64 || BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win32)
			    {
				    if(Compiler == WindowsCompiler.VisualStudio2015_DEPRECATED)
				    {
					    // VS2015 just installs one toolchain; use that.
					    DirectoryReference InstallDir;
					    if(TryReadInstallDirRegistryKey32("Microsoft\\VisualStudio\\SxS\\VS7", "14.0", out InstallDir))
					    {
						    InstallDirs.Add(InstallDir);
					    }
				    }
				    else if(Compiler == WindowsCompiler.VisualStudio2017 || Compiler == WindowsCompiler.VisualStudio2019)
				    {
						SortedDictionary<int, DirectoryReference> SortedInstallDirs = new SortedDictionary<int, DirectoryReference>(); 
						try
						{
							SetupConfiguration Setup = new SetupConfiguration();
							IEnumSetupInstances Enumerator = Setup.EnumAllInstances();

							ISetupInstance[] Instances = new ISetupInstance[1];
							for(;;)
							{
								int NumFetched;
								Enumerator.Next(1, Instances, out NumFetched);

								if(NumFetched == 0)
								{
									break;
								}

								ISetupInstance2 Instance = (ISetupInstance2)Instances[0];
								if((Instance.GetState() & InstanceState.Local) == InstanceState.Local)
								{
									string VersionString = Instance.GetInstallationVersion();

									VersionNumber Version;
									if (VersionNumber.TryParse(VersionString, out Version))
									{
										VersionNumber Version2019 = new VersionNumber(16);
										if(Compiler == WindowsCompiler.VisualStudio2019 && Version < Version2019)
										{
											continue;
										}
										else if(Compiler == WindowsCompiler.VisualStudio2017 && Version >= Version2019)
										{
											continue;
										}
									}

									int SortOrder = SortedInstallDirs.Count;

									ISetupInstanceCatalog Catalog = Instance as ISetupInstanceCatalog;
									if (Catalog != null && Catalog.IsPrerelease())
									{
										SortOrder |= (1 << 16);
									}

									string ProductId = Instance.GetProduct().GetId();
									if (ProductId.Equals("Microsoft.VisualStudio.Product.WDExpress", StringComparison.Ordinal))
									{
										SortOrder |= (1 << 17);
									}

									Log.TraceLog("Found Visual Studio installation: {0} (Product={1}, Version={2}, Sort={3})", Instance.GetInstallationPath(), ProductId, VersionString, SortOrder);

									SortedInstallDirs.Add(SortOrder, new DirectoryReference(Instance.GetInstallationPath()));
								}
							}
						}
						catch
						{
						}
						InstallDirs.AddRange(SortedInstallDirs.Values);
					}
					else
				    {
					    throw new BuildException("Unsupported compiler version ({0})", Compiler);
				    }
				}
				CachedVSInstallDirs.Add(Compiler, InstallDirs);
			}
			return InstallDirs;
		}

		/// <summary>
		/// Determines the directory containing the MSVC toolchain
		/// </summary>
		/// <param name="Compiler">Major version of the compiler to use</param>
		/// <returns>Map of version number to directories</returns>
		public static Dictionary<VersionNumber, DirectoryReference> FindToolChainDirs(WindowsCompiler Compiler)
		{
			Dictionary<VersionNumber, DirectoryReference> ToolChainVersionToDir;
			if(!CachedToolChainDirs.TryGetValue(Compiler, out ToolChainVersionToDir))
			{
				ToolChainVersionToDir = new Dictionary<VersionNumber, DirectoryReference>();
			    if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64 || BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win32)
			    {
					if(Compiler == WindowsCompiler.Clang)
					{
						// Check for a manual installation
						DirectoryReference InstallDir = DirectoryReference.Combine(DirectoryReference.GetSpecialFolder(Environment.SpecialFolder.ProgramFiles), "LLVM");
						if(IsValidToolChainDirClang(InstallDir))
						{
							FileReference CompilerFile = FileReference.Combine(InstallDir, "bin", "clang-cl.exe");
							if(FileReference.Exists(CompilerFile))
							{
								FileVersionInfo VersionInfo = FileVersionInfo.GetVersionInfo(CompilerFile.FullName);
								VersionNumber Version = new VersionNumber(VersionInfo.FileMajorPart, VersionInfo.FileMinorPart, VersionInfo.FileBuildPart);
								ToolChainVersionToDir[Version] = InstallDir;
							}
						}

						// Check for AutoSDK paths
						DirectoryReference AutoSdkDir;
						if(UEBuildPlatformSDK.TryGetHostPlatformAutoSDKDir(out AutoSdkDir))
						{
							DirectoryReference ClangBaseDir = DirectoryReference.Combine(AutoSdkDir, "Win64", "LLVM");
							if(DirectoryReference.Exists(ClangBaseDir))
							{
								foreach(DirectoryReference ToolChainDir in DirectoryReference.EnumerateDirectories(ClangBaseDir))
								{
									VersionNumber Version;
									if(VersionNumber.TryParse(ToolChainDir.GetDirectoryName(), out Version) && IsValidToolChainDirClang(ToolChainDir))
									{
										ToolChainVersionToDir[Version] = ToolChainDir;
									}
								}
							}
						}
					}
					else if(Compiler == WindowsCompiler.Intel)
					{
						// Just check for a manual installation
						DirectoryReference InstallDir = DirectoryReference.Combine(DirectoryReference.GetSpecialFolder(Environment.SpecialFolder.ProgramFilesX86), "IntelSWTools", "compilers_and_libraries", "windows");
						if(DirectoryReference.Exists(InstallDir))
						{
							FileReference IclPath = FileReference.Combine(InstallDir, "bin", "intel64", "icl.exe");
							if(FileReference.Exists(IclPath))
							{
								FileVersionInfo VersionInfo = FileVersionInfo.GetVersionInfo(IclPath.FullName);
								VersionNumber Version = new VersionNumber(VersionInfo.FileMajorPart, VersionInfo.FileMinorPart, VersionInfo.FileBuildPart);
								ToolChainVersionToDir[Version] = InstallDir;
							}
						}
					}
				    else if(Compiler == WindowsCompiler.VisualStudio2015_DEPRECATED)
				    {
					    // VS2015 just installs one toolchain; use that.
					    List<DirectoryReference> InstallDirs = FindVSInstallDirs(Compiler);
					    foreach(DirectoryReference InstallDir in InstallDirs)
					    {
							DirectoryReference ToolChainBaseDir = DirectoryReference.Combine(InstallDir, "VC");
							if(IsValidToolChainDir2015(ToolChainBaseDir))
							{
								ToolChainVersionToDir[new VersionNumber(14, 0)] = ToolChainBaseDir;
							}
					    }
				    }
				    else if(Compiler == WindowsCompiler.VisualStudio2017 || Compiler == WindowsCompiler.VisualStudio2019)
				    {
						// Enumerate all the manually installed toolchains
						List<DirectoryReference> InstallDirs = FindVSInstallDirs(Compiler);
					    foreach(DirectoryReference InstallDir in InstallDirs)
					    {
						    DirectoryReference ToolChainBaseDir = DirectoryReference.Combine(InstallDir, "VC", "Tools", "MSVC");
						    if(DirectoryReference.Exists(ToolChainBaseDir))
						    {
							    foreach(DirectoryReference ToolChainDir in DirectoryReference.EnumerateDirectories(ToolChainBaseDir))
							    {
								    VersionNumber Version;
								    if(IsValidToolChainDir2017or2019(ToolChainDir, out Version))
									{
										Log.TraceLog("Found Visual Studio toolchain: {0} (Version={1})", ToolChainDir, Version);
										if(!ToolChainVersionToDir.ContainsKey(Version))
										{
											ToolChainVersionToDir[Version] = ToolChainDir;
										}
								    }
							    }
						    }
					    }

						// Enumerate all the AutoSDK toolchains
						DirectoryReference PlatformDir;
						if(UEBuildPlatformSDK.TryGetHostPlatformAutoSDKDir(out PlatformDir))
						{
							DirectoryReference ToolChainBaseDir = DirectoryReference.Combine(PlatformDir, "Win64", (Compiler == WindowsCompiler.VisualStudio2019)? "VS2019" : "VS2017");
							if(DirectoryReference.Exists(ToolChainBaseDir))
							{
								foreach(DirectoryReference ToolChainDir in DirectoryReference.EnumerateDirectories(ToolChainBaseDir))
								{
									VersionNumber Version;
									if(IsValidToolChainDir2017or2019(ToolChainDir, out Version))
									{
										Log.TraceLog("Found Visual Studio toolchain: {0} (Version={1})", ToolChainDir, Version);
										if (!ToolChainVersionToDir.ContainsKey(Version))
										{
											ToolChainVersionToDir[Version] = ToolChainDir;
										}
									}
								}
							}
						}
					}
					else
				    {
					    throw new BuildException("Unsupported compiler version ({0})", Compiler);
				    }
				}
				CachedToolChainDirs.Add(Compiler, ToolChainVersionToDir);
			}
			return ToolChainVersionToDir;
		}

		/// <summary>
		/// Checks if the given directory contains a valid Clang toolchain
		/// </summary>
		/// <param name="ToolChainDir">Directory to check</param>
		/// <returns>True if the given directory is valid</returns>
		static bool IsValidToolChainDirClang(DirectoryReference ToolChainDir)
		{
			return FileReference.Exists(FileReference.Combine(ToolChainDir, "bin", "clang-cl.exe"));
		}

		/// <summary>
		/// Checks if the given directory contains a valid Visual Studio 2015 toolchain
		/// </summary>
		/// <param name="ToolChainDir">Directory to check</param>
		/// <returns>True if the given directory is valid</returns>
		static bool IsValidToolChainDir2015(DirectoryReference ToolChainDir)
		{
			return FileReference.Exists(FileReference.Combine(ToolChainDir, "bin", "amd64", "cl.exe")) || FileReference.Exists(FileReference.Combine(ToolChainDir, "bin", "x86_amd64", "cl.exe"));
		}

		/// <summary>
		/// Determines if the given path is a valid Visual C++ version number
		/// </summary>
		/// <param name="ToolChainDir">The toolchain directory</param>
		/// <param name="Version">The version number for the toolchain</param>
		/// <returns>True if the path is a valid version</returns>
		static bool IsValidToolChainDir2017or2019(DirectoryReference ToolChainDir, out VersionNumber Version)
		{
			FileReference CompilerExe = FileReference.Combine(ToolChainDir, "bin", "Hostx86", "x64", "cl.exe");
			if(!FileReference.Exists(CompilerExe))
			{
				CompilerExe = FileReference.Combine(ToolChainDir, "bin", "Hostx64", "x64", "cl.exe");
				if(!FileReference.Exists(CompilerExe))
				{
					Version = null;
					return false;
				}
			}

			FileVersionInfo VersionInfo = FileVersionInfo.GetVersionInfo(CompilerExe.FullName);
			if (VersionInfo.ProductMajorPart != 0)
			{
				Version = new VersionNumber(VersionInfo.ProductMajorPart, VersionInfo.ProductMinorPart, VersionInfo.ProductBuildPart);
				return true;
			}

			return VersionNumber.TryParse(ToolChainDir.GetDirectoryName(), out Version);
		}

		/// <summary>
		/// Checks if the given directory contains a 64-bit toolchain. Used to prefer regular Visual Studio versions over express editions.
		/// </summary>
		/// <param name="ToolChainDir">Directory to check</param>
		/// <returns>True if the given directory contains a 64-bit toolchain</returns>
		static bool Has64BitToolChain(DirectoryReference ToolChainDir)
		{
			return FileReference.Exists(FileReference.Combine(ToolChainDir, "bin", "amd64", "cl.exe")) || FileReference.Exists(FileReference.Combine(ToolChainDir, "bin", "Hostx64", "x64", "cl.exe"));
		}

		/// <summary>
		/// Determines if an IDE for the given compiler is installed.
		/// </summary>
		/// <param name="Compiler">Compiler to check for</param>
		/// <returns>True if the given compiler is installed</returns>
		public static bool HasIDE(WindowsCompiler Compiler)
		{
			return FindVSInstallDirs(Compiler).Count > 0;
		}

		/// <summary>
		/// Determines if a given compiler is installed
		/// </summary>
		/// <param name="Compiler">Compiler to check for</param>
		/// <returns>True if the given compiler is installed</returns>
		public static bool HasCompiler(WindowsCompiler Compiler)
		{
			return FindToolChainDirs(Compiler).Count > 0;
		}

		/// <summary>
		/// Checks if a given Visual C++ toolchain version is compatible with UE4
		/// </summary>
		/// <param name="Version">The version number to check</param>
		/// <returns>True if the toolchain is compatible with UE4</returns>
		static bool IsCompatibleVisualCppToolChain(VersionNumber Version)
		{
			string Message;
			return IsCompatibleVisualCppToolChain(Version, out Message);
		}

		/// <summary>
		/// Checks if a given Visual C++ toolchain version is compatible with UE4
		/// </summary>
		/// <param name="Version">The version number to check</param>
		/// <param name="Message">Receives a message describing why the toolchain is not compatible</param>
		/// <returns>True if the toolchain is compatible with UE4</returns>
		static bool IsCompatibleVisualCppToolChain(VersionNumber Version, out string Message)
		{
			if (Version >= new VersionNumber(14, 23, 0) && Version < new VersionNumber(14, 23, 28107))
			{
				Message = String.Format("The Visual C++ 14.23 toolchain is known to have code-generation issues with UE4. Please install an earlier or later toolchain from the Visual Studio installer. See here for more information: https://developercommunity.visualstudio.com/content/problem/734585/msvc-142328019-compilation-bug.html");
				return false;
			}
			else
			{
				Message = null;
				return true;
			}
		}

		/// <summary>
		/// Determines the directory containing the MSVC toolchain
		/// </summary>
		/// <param name="Compiler">Major version of the compiler to use</param>
		/// <param name="CompilerVersion">The minimum compiler version to use</param>
		/// <param name="OutToolChainVersion">Receives the chosen toolchain version</param>
		/// <param name="OutToolChainDir">Receives the directory containing the toolchain</param>
		/// <returns>True if the toolchain directory was found correctly</returns>
		public static bool TryGetToolChainDir(WindowsCompiler Compiler, string CompilerVersion, out VersionNumber OutToolChainVersion, out DirectoryReference OutToolChainDir)
		{
			// Find all the installed toolchains
			Dictionary<VersionNumber, DirectoryReference> ToolChainVersionToDir = FindToolChainDirs(Compiler);

			// Figure out the actual version number that we want
			VersionNumber ToolChainVersion = null;
			if(CompilerVersion != null)
			{
				if(String.Compare(CompilerVersion, "Latest", StringComparison.InvariantCultureIgnoreCase) == 0 && ToolChainVersionToDir.Count > 0)
				{
					ToolChainVersion = ToolChainVersionToDir.OrderBy(x => IsCompatibleVisualCppToolChain(x.Key)).ThenBy(x => Has64BitToolChain(x.Value)).ThenBy(x => x.Key).Last().Key;
				}
				else if(!VersionNumber.TryParse(CompilerVersion, out ToolChainVersion))
				{
					throw new BuildException("Unable to find {0} toolchain; '{1}' is an invalid version", GetCompilerName(Compiler), CompilerVersion);
				}
			}
			else
			{
				VersionNumber[] PotentialToolchains;
				if(Compiler == WindowsCompiler.Clang)
				{
					PotentialToolchains = new VersionNumber[] { DefaultClangToolChainVersion};
				}
				else
				{
					PotentialToolchains = PreferredVisualStudioToolChainVersion;
				}

				foreach (VersionNumber PreferredToolchain in PotentialToolchains)
				{
					DirectoryReference DefaultToolChainDir;
					if (ToolChainVersionToDir.TryGetValue(PreferredToolchain, out DefaultToolChainDir) && (Compiler == WindowsCompiler.Clang || Has64BitToolChain(DefaultToolChainDir)))
					{
						ToolChainVersion = PreferredToolchain;
						break;
					}
				}

				// if we failed to find any of our preferred toolchains we pick the newest (highest version number)
				if (ToolChainVersion == null && ToolChainVersionToDir.Count > 0)
				{
					ToolChainVersion = ToolChainVersionToDir.OrderBy(x => IsCompatibleVisualCppToolChain(x.Key)).ThenBy(x => Has64BitToolChain(x.Value)).ThenBy(x => x.Key).Last().Key;
				}
			}

			// Check it's valid
			string Message;
			if (ToolChainVersion != null && !IsCompatibleVisualCppToolChain(ToolChainVersion, out Message))
			{
				throw new BuildException(Message);
			}

			// Get the actual directory for this version
			if(ToolChainVersion != null && ToolChainVersionToDir.TryGetValue(ToolChainVersion, out OutToolChainDir))
			{
				OutToolChainVersion = ToolChainVersion;
				return true;
			}

			// Otherwise fail
			OutToolChainVersion = null;
			OutToolChainDir = null;
			return false;
		}

		static readonly KeyValuePair<RegistryKey, string>[] InstallDirRoots = {
			new KeyValuePair<RegistryKey, string>(Registry.CurrentUser, "SOFTWARE\\"),
			new KeyValuePair<RegistryKey, string>(Registry.LocalMachine, "SOFTWARE\\"),
			new KeyValuePair<RegistryKey, string>(Registry.CurrentUser, "SOFTWARE\\Wow6432Node\\"),
			new KeyValuePair<RegistryKey, string>(Registry.LocalMachine, "SOFTWARE\\Wow6432Node\\")
		};

		/// <summary>
		/// Reads an install directory for a 32-bit program from a registry key. This checks for per-user and machine wide settings, and under the Wow64 virtual keys (HKCU\SOFTWARE, HKLM\SOFTWARE, HKCU\SOFTWARE\Wow6432Node, HKLM\SOFTWARE\Wow6432Node).
		/// </summary>
		/// <param name="KeySuffix">Path to the key to read, under one of the roots listed above.</param>
		/// <param name="ValueName">Value to be read.</param>
		/// <param name="InstallDir">On success, the directory corresponding to the value read.</param>
		/// <returns>True if the key was read, false otherwise.</returns>
		static bool TryReadInstallDirRegistryKey32(string KeySuffix, string ValueName, out DirectoryReference InstallDir)
		{
			foreach (KeyValuePair<RegistryKey, string> InstallRoot in InstallDirRoots)
			{
				using (RegistryKey Key = InstallRoot.Key.OpenSubKey(InstallRoot.Value + KeySuffix))
				{
					if (Key != null && TryReadDirRegistryKey(Key.Name, ValueName, out InstallDir))
					{
						return true;
					}
				}
			}
			InstallDir = null;
			return false;
		}

		/// <summary>
		/// For each root location relevant to install dirs, look for the given key and add its subkeys to the set of subkeys to return.
		/// This checks for per-user and machine wide settings, and under the Wow64 virtual keys (HKCU\SOFTWARE, HKLM\SOFTWARE, HKCU\SOFTWARE\Wow6432Node, HKLM\SOFTWARE\Wow6432Node).
		/// </summary>
		/// <param name="KeyName">The subkey to look for under each root location</param>
		/// <returns>A list of unique subkeys found under any of the existing subkeys</returns>
		static string[] ReadInstallDirSubKeys32(string KeyName)
		{
			HashSet<string> AllSubKeys = new HashSet<string>(StringComparer.Ordinal);
			foreach (KeyValuePair<RegistryKey, string> Root in InstallDirRoots)
			{
				using (RegistryKey Key = Root.Key.OpenSubKey(Root.Value + KeyName))
				{
					if (Key == null)
					{
						continue;
					}

					foreach (string SubKey in Key.GetSubKeyNames())
					{
						AllSubKeys.Add(SubKey);
					}
				}
			}
			return AllSubKeys.ToArray();
		}

		/// <summary>
		/// Attempts to reads a directory name stored in a registry key
		/// </summary>
		/// <param name="KeyName">Key to read from</param>
		/// <param name="ValueName">Value within the key to read</param>
		/// <param name="Value">The directory read from the registry key</param>
		/// <returns>True if the key was read, false if it was missing or empty</returns>
		static bool TryReadDirRegistryKey(string KeyName, string ValueName, out DirectoryReference Value)
		{
			string StringValue = Registry.GetValue(KeyName, ValueName, null) as string;
			if (String.IsNullOrEmpty(StringValue))
			{
				Value = null;
				return false;
			}
			else
			{
				Value = new DirectoryReference(StringValue);
				return true;
			}
		}

		/// <summary>
		/// Gets the path to MSBuild. This mirrors the logic in GetMSBuildPath.bat.
		/// </summary>
		/// <param name="OutLocation">On success, receives the path to the MSBuild executable.</param>
		/// <returns>True on success.</returns>
		public static bool TryGetMsBuildPath(out FileReference OutLocation)
		{
			// Get the Visual Studio 2019 install directory
			List<DirectoryReference> InstallDirs2019 = WindowsPlatform.FindVSInstallDirs(WindowsCompiler.VisualStudio2019);
			foreach (DirectoryReference InstallDir in InstallDirs2019)
			{
				FileReference MsBuildLocation = FileReference.Combine(InstallDir, "MSBuild", "Current", "Bin", "MSBuild.exe");
				if (FileReference.Exists(MsBuildLocation))
				{
					OutLocation = MsBuildLocation;
					return true;
				}
			}

			// Get the Visual Studio 2017 install directory
			List<DirectoryReference> InstallDirs2017 = WindowsPlatform.FindVSInstallDirs(WindowsCompiler.VisualStudio2017);
			foreach (DirectoryReference InstallDir in InstallDirs2017)
			{
				FileReference MsBuildLocation = FileReference.Combine(InstallDir, "MSBuild", "15.0", "Bin", "MSBuild.exe");
				if(FileReference.Exists(MsBuildLocation))
				{
					OutLocation = MsBuildLocation;
					return true;
				}
			}

			// Try to get the MSBuild 14.0 path directly (see https://msdn.microsoft.com/en-us/library/hh162058(v=vs.120).aspx)
			FileReference ToolPath = FileReference.Combine(DirectoryReference.GetSpecialFolder(Environment.SpecialFolder.ProgramFilesX86), "MSBuild", "14.0", "bin", "MSBuild.exe");
			if(FileReference.Exists(ToolPath))
			{
				OutLocation = ToolPath;
				return true;
			} 

			// Check for older versions of MSBuild. These are registered as separate versions in the registry.
			if (TryReadMsBuildInstallPath("Microsoft\\MSBuild\\ToolsVersions\\14.0", "MSBuildToolsPath", "MSBuild.exe", out ToolPath))
			{
				OutLocation = ToolPath;
				return true;
			}
			if (TryReadMsBuildInstallPath("Microsoft\\MSBuild\\ToolsVersions\\12.0", "MSBuildToolsPath", "MSBuild.exe", out ToolPath))
			{
				OutLocation = ToolPath;
				return true;
			}
			if (TryReadMsBuildInstallPath("Microsoft\\MSBuild\\ToolsVersions\\4.0", "MSBuildToolsPath", "MSBuild.exe", out ToolPath))
			{
				OutLocation = ToolPath;
				return true;
			}

			OutLocation = null;
			return false;
		}

		/// <summary>
		/// Gets the MSBuild path, and throws an exception on failure.
		/// </summary>
		/// <returns>Path to MSBuild</returns>
		public static FileReference GetMsBuildToolPath()
		{
			FileReference Location;
			if(!TryGetMsBuildPath(out Location))
			{
				throw new BuildException("Unable to find installation of MSBuild.");
			}
 			return Location;
		}

		public static string GetArchitectureSubpath(WindowsArchitecture arch)
		{
			string archPath = "Unknown";
			if (arch == WindowsArchitecture.x86)
			{
				archPath = "x86";
			}
			else if (arch == WindowsArchitecture.ARM32)
			{
				archPath = "arm";
			}
			else if (arch == WindowsArchitecture.x64)
			{
				archPath = "x64";
			}
			else if (arch == WindowsArchitecture.ARM64)
			{
				archPath = "arm64";
			}
			return archPath;
		}

		/// <summary>
		/// Function to query the registry under HKCU/HKLM Win32/Wow64 software registry keys for a certain install directory.
		/// This mirrors the logic in GetMSBuildPath.bat.
		/// </summary>
		/// <returns></returns>
		static bool TryReadMsBuildInstallPath(string KeyRelativePath, string KeyName, string MsBuildRelativePath, out FileReference OutMsBuildPath)
		{
			string[] KeyBasePaths =
			{
				@"HKEY_CURRENT_USER\SOFTWARE\",
				@"HKEY_LOCAL_MACHINE\SOFTWARE\",
				@"HKEY_CURRENT_USER\SOFTWARE\Wow6432Node\",
				@"HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\"
			};

			foreach (string KeyBasePath in KeyBasePaths)
			{
				string Value = Registry.GetValue(KeyBasePath + KeyRelativePath, KeyName, null) as string;
				if (Value != null)
				{
					FileReference MsBuildPath = FileReference.Combine(new DirectoryReference(Value), MsBuildRelativePath);
					if (FileReference.Exists(MsBuildPath))
					{
						OutMsBuildPath = MsBuildPath;
						return true;
					}
				}
			}

			OutMsBuildPath = null;
			return false;
		}

		/// <summary>
		/// Determines the directory containing the MSVC toolchain
		/// </summary>
		/// <param name="Compiler">Major version of the compiler to use</param>
		/// <returns>Map of version number to directories</returns>
		public static List<DirectoryReference> FindDiaSdkDirs(WindowsCompiler Compiler)
		{
			List<DirectoryReference> DiaSdkDirs;
			if(!CachedDiaSdkDirs.TryGetValue(Compiler, out DiaSdkDirs))
			{
				DiaSdkDirs = new List<DirectoryReference>();

				DirectoryReference PlatformDir;
				if(UEBuildPlatformSDK.TryGetHostPlatformAutoSDKDir(out PlatformDir))
				{
					DirectoryReference DiaSdkDir = DirectoryReference.Combine(PlatformDir, "Win64", "DIA SDK", (Compiler == WindowsCompiler.VisualStudio2019)? "VS2019" : "VS2017");
					if(IsValidDiaSdkDir(DiaSdkDir))
					{
						DiaSdkDirs.Add(DiaSdkDir);
					}
				}
			
				List<DirectoryReference> VisualStudioDirs = FindVSInstallDirs(Compiler);
				foreach(DirectoryReference VisualStudioDir in VisualStudioDirs)
				{
					DirectoryReference DiaSdkDir = DirectoryReference.Combine(VisualStudioDir, "DIA SDK");
					if(IsValidDiaSdkDir(DiaSdkDir))
					{
						DiaSdkDirs.Add(DiaSdkDir);
					}
				}
			}
			return DiaSdkDirs;
		}

		/// <summary>
		/// Determines if a directory contains a valid DIA SDK
		/// </summary>
		/// <param name="DiaSdkDir">The directory to check</param>
		/// <returns>True if it contains a valid DIA SDK</returns>
		static bool IsValidDiaSdkDir(DirectoryReference DiaSdkDir)
		{
			return FileReference.Exists(FileReference.Combine(DiaSdkDir, "bin", "amd64", "msdia140.dll"));
		}

		/// <summary>
		/// Updates the CachedWindowsSdkDirs and CachedUniversalCrtDirs variables
		/// </summary>
		private static void UpdateCachedWindowsSdks()
		{
			Dictionary<VersionNumber, DirectoryReference> WindowsSdkDirs = new Dictionary<VersionNumber, DirectoryReference>();
			Dictionary<VersionNumber, DirectoryReference> UniversalCrtDirs = new Dictionary<VersionNumber, DirectoryReference>();

			// Enumerate the Windows 8.1 SDK, if present
			DirectoryReference InstallDir_8_1;
			if(TryReadInstallDirRegistryKey32("Microsoft\\Microsoft SDKs\\Windows\\v8.1", "InstallationFolder", out InstallDir_8_1))
			{
				if(FileReference.Exists(FileReference.Combine(InstallDir_8_1, "Include", "um", "windows.h")))
				{
					Log.TraceLog("Found Windows 8.1 SDK at {0}", InstallDir_8_1);
					VersionNumber Version_8_1 = new VersionNumber(8, 1);
					WindowsSdkDirs[Version_8_1] = InstallDir_8_1;
				}
			}

			// Find all the root directories for Windows 10 SDKs
			List<DirectoryReference> InstallDirs_10 = new List<DirectoryReference>();
			EnumerateSdkRootDirs(InstallDirs_10);

			// Enumerate all the Windows 10 SDKs
			foreach(DirectoryReference InstallDir_10 in InstallDirs_10.Distinct())
			{
				DirectoryReference IncludeRootDir = DirectoryReference.Combine(InstallDir_10, "Include");
				if(DirectoryReference.Exists(IncludeRootDir))
				{
					foreach(DirectoryReference IncludeDir in DirectoryReference.EnumerateDirectories(IncludeRootDir))
					{
						VersionNumber IncludeVersion;
						if(VersionNumber.TryParse(IncludeDir.GetDirectoryName(), out IncludeVersion))
						{
							if(FileReference.Exists(FileReference.Combine(IncludeDir, "um", "windows.h")))
							{
								Log.TraceLog("Found Windows 10 SDK version {0} at {1}", IncludeVersion, InstallDir_10);
								WindowsSdkDirs[IncludeVersion] = InstallDir_10;
							}
							if(FileReference.Exists(FileReference.Combine(IncludeDir, "ucrt", "corecrt.h")))
							{
								Log.TraceLog("Found Universal CRT version {0} at {1}", IncludeVersion, InstallDir_10);
								UniversalCrtDirs[IncludeVersion] = InstallDir_10;
							}
						}
					}
				}
			}

			CachedWindowsSdkDirs = WindowsSdkDirs;
			CachedUniversalCrtDirs = UniversalCrtDirs;
		}

		/// <summary>
		/// Finds all the installed Windows SDK versions
		/// </summary>
		/// <returns>Map of version number to Windows SDK directories</returns>
		public static IReadOnlyDictionary<VersionNumber, DirectoryReference> FindWindowsSdkDirs()
		{
			// Update the cache of install directories, if it's not set
			if(CachedWindowsSdkDirs == null)
			{
				UpdateCachedWindowsSdks();
			}
			return CachedWindowsSdkDirs;
		}

		/// <summary>
		/// Finds all the installed Universal CRT versions
		/// </summary>
		/// <returns>Map of version number to universal CRT directories</returns>
		public static IReadOnlyDictionary<VersionNumber, DirectoryReference> FindUniversalCrtDirs()
		{
			if(CachedUniversalCrtDirs == null)
			{
				UpdateCachedWindowsSdks();
			}
			return CachedUniversalCrtDirs;
		}

		/// <summary>
		/// Enumerates all the Windows 10 SDK root directories
		/// </summary>
		/// <param name="RootDirs">Receives all the Windows 10 sdk root directories</param>
		private static void EnumerateSdkRootDirs(List<DirectoryReference> RootDirs)
		{
			DirectoryReference RootDir;
			if(TryReadInstallDirRegistryKey32("Microsoft\\Windows Kits\\Installed Roots", "KitsRoot10", out RootDir))
			{
				Log.TraceLog("Found Windows 10 SDK root at {0} (1)", RootDir);
				RootDirs.Add(RootDir);
			}
			if(TryReadInstallDirRegistryKey32("Microsoft\\Microsoft SDKs\\Windows\\v10.0", "InstallationFolder", out RootDir))
			{
				Log.TraceLog("Found Windows 10 SDK root at {0} (2)", RootDir);
				RootDirs.Add(RootDir);
			}

			DirectoryReference HostAutoSdkDir;
			if(UEBuildPlatformSDK.TryGetHostPlatformAutoSDKDir(out HostAutoSdkDir))
			{
				DirectoryReference RootDirAutoSdk = DirectoryReference.Combine(HostAutoSdkDir, "Win64", "Windows Kits", "10");
				if(DirectoryReference.Exists(RootDirAutoSdk))
				{
					Log.TraceLog("Found Windows 10 AutoSDK root at {0}", RootDirAutoSdk);
					RootDirs.Add(RootDirAutoSdk);
				}
			}
		}

		/// <summary>
		/// Determines the directory containing the Windows SDK toolchain
		/// </summary>
		/// <param name="DesiredVersion">The desired Windows SDK version. This may be "Latest", a specific version number, or null. If null, the function will look for DefaultWindowsSdkVersion. Failing that, it will return the latest version.</param>
		/// <param name="OutSdkVersion">Receives the version number of the selected Windows SDK</param>
		/// <param name="OutSdkDir">Receives the root directory for the selected SDK</param>
		/// <returns>True if the toolchain directory was found correctly</returns>
		public static bool TryGetWindowsSdkDir(string DesiredVersion, out VersionNumber OutSdkVersion, out DirectoryReference OutSdkDir)
		{
			// Get a map of Windows SDK versions to their root directories
			/*IReadOnlyDictionary<VersionNumber, DirectoryReference> WindowsSdkDirs =*/ FindWindowsSdkDirs();

			// Figure out which version number to look for
			VersionNumber WindowsSdkVersion = null;
			if(!string.IsNullOrEmpty(DesiredVersion))
			{
				if(String.Compare(DesiredVersion, "Latest", StringComparison.InvariantCultureIgnoreCase) == 0 && CachedWindowsSdkDirs.Count > 0)
				{
					WindowsSdkVersion = CachedWindowsSdkDirs.OrderBy(x => x.Key).Last().Key;
				}
				else if(!VersionNumber.TryParse(DesiredVersion, out WindowsSdkVersion))
				{
					throw new BuildException("Unable to find requested Windows SDK; '{0}' is an invalid version", DesiredVersion);
				}
			}
			else
			{
				WindowsSdkVersion = PreferredWindowsSdkVersions.FirstOrDefault(x => CachedWindowsSdkDirs.ContainsKey(x));
				if(WindowsSdkVersion == null && CachedWindowsSdkDirs.Count > 0)
				{
					WindowsSdkVersion = CachedWindowsSdkDirs.OrderBy(x => x.Key).Last().Key;
				}
			}

			// Get the actual directory for this version
			DirectoryReference SdkDir;
			if(WindowsSdkVersion != null && CachedWindowsSdkDirs.TryGetValue(WindowsSdkVersion, out SdkDir))
			{
				OutSdkDir = SdkDir;
				OutSdkVersion = WindowsSdkVersion;
				return true;
			}
			else
			{
				OutSdkDir = null;
				OutSdkVersion = null;
				return false;
			}
		}

		/// <summary>
		/// Gets the installation directory for the NETFXSDK
		/// </summary>
		/// <param name="OutInstallDir">Receives the installation directory on success</param>
		/// <returns>True if the directory was found, false otherwise</returns>
		public static bool TryGetNetFxSdkInstallDir(out DirectoryReference OutInstallDir)
		{
			DirectoryReference HostAutoSdkDir;
			if(UEBuildPlatformSDK.TryGetHostPlatformAutoSDKDir(out HostAutoSdkDir))
			{
				DirectoryReference NetFxDir_4_6 = DirectoryReference.Combine(HostAutoSdkDir, "Win64", "Windows Kits", "NETFXSDK", "4.6");
				if(FileReference.Exists(FileReference.Combine(NetFxDir_4_6, "Include", "um", "mscoree.h")))
				{
					OutInstallDir = NetFxDir_4_6;
					return true;
				}

				DirectoryReference NetFxDir_4_6_1 = DirectoryReference.Combine(HostAutoSdkDir, "Win64", "Windows Kits", "NETFXSDK", "4.6.1");
				if(FileReference.Exists(FileReference.Combine(NetFxDir_4_6_1, "Include", "um", "mscoree.h")))
				{
					OutInstallDir = NetFxDir_4_6_1;
					return true;
				}
			}

			string NetFxSDKKeyName = "Microsoft\\Microsoft SDKs\\NETFXSDK";
			string[] PreferredVersions = new string[] { "4.6.2", "4.6.1", "4.6" };
			foreach (string PreferredVersion in PreferredVersions)
			{
				if (TryReadInstallDirRegistryKey32(NetFxSDKKeyName + "\\" + PreferredVersion, "KitsInstallationFolder", out OutInstallDir))
				{
					return true;
				}
			}

			// If we didn't find one of our preferred versions for NetFXSDK, use the max version present on the system
			Version MaxVersion = null;
			string MaxVersionString = null;
			foreach (string ExistingVersionString in ReadInstallDirSubKeys32(NetFxSDKKeyName))
			{
				Version ExistingVersion;
				if (!Version.TryParse(ExistingVersionString, out ExistingVersion))
				{
					continue;
				}
				if (MaxVersion == null || ExistingVersion.CompareTo(MaxVersion) > 0)
				{
					MaxVersion = ExistingVersion;
					MaxVersionString = ExistingVersionString;
				}
			}

			if (MaxVersionString != null)
			{
				return TryReadInstallDirRegistryKey32(NetFxSDKKeyName + "\\" + MaxVersionString, "KitsInstallationFolder", out OutInstallDir);
			}

			OutInstallDir = null;
			return false;
		}

		/// <summary>
		/// Gets the platform name that should be used.
		/// </summary>
		public override string GetPlatformName()
		{
			return "Windows";
		}

		/// <summary>
		/// If this platform can be compiled with SN-DBS
		/// </summary>
		public override bool CanUseSNDBS()
		{
			return true;
		}

		/// <summary>
		/// Determines if the given name is a build product for a target.
		/// </summary>
		/// <param name="FileName">The name to check</param>
		/// <param name="NamePrefixes">Target or application names that may appear at the start of the build product name (eg. "UE4Editor", "ShooterGameEditor")</param>
		/// <param name="NameSuffixes">Suffixes which may appear at the end of the build product name</param>
		/// <returns>True if the string matches the name of a build product, false otherwise</returns>
		public override bool IsBuildProduct(string FileName, string[] NamePrefixes, string[] NameSuffixes)
		{
			return IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".exe")
				|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".dll")
				|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".dll.response")
				|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".lib")
				|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".pdb")
				|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".exp")
				|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".obj")
				|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".map")
				|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".objpaths");
		}

		/// <summary>
		/// Get the extension to use for the given binary type
		/// </summary>
		/// <param name="InBinaryType"> The binrary type being built</param>
		/// <returns>string    The binary extenstion (ie 'exe' or 'dll')</returns>
		public override string GetBinaryExtension(UEBuildBinaryType InBinaryType)
		{
			switch (InBinaryType)
			{
				case UEBuildBinaryType.DynamicLinkLibrary:
					return ".dll";
				case UEBuildBinaryType.Executable:
					return ".exe";
				case UEBuildBinaryType.StaticLibrary:
					return ".lib";
			}
			return base.GetBinaryExtension(InBinaryType);
		}

		/// <summary>
		/// Get the extensions to use for debug info for the given binary type
		/// </summary>
		/// <param name="Target">The target being built</param>
		/// <param name="InBinaryType"> The binary type being built</param>
		/// <returns>string[]    The debug info extensions (i.e. 'pdb')</returns>
		public override string[] GetDebugInfoExtensions(ReadOnlyTargetRules Target, UEBuildBinaryType InBinaryType)
		{
			switch (InBinaryType)
			{
				case UEBuildBinaryType.DynamicLinkLibrary:
				case UEBuildBinaryType.Executable:
					return new string[] {".pdb"};
			}
			return new string [] {};
		}

		public override bool HasDefaultBuildConfig(UnrealTargetPlatform Platform, DirectoryReference ProjectPath)
		{
			if (Platform == UnrealTargetPlatform.Win32)
			{
				string[] StringKeys = new string[] {
					"MinimumOSVersion"
				};

				// look up OS specific settings
				if (!DoProjectSettingsMatchDefault(Platform, ProjectPath, "/Script/WindowsTargetPlatform.WindowsTargetSettings",
					null, null, StringKeys))
				{
					return false;
				}
			}

			// check the base settings
			return base.HasDefaultBuildConfig(Platform, ProjectPath);
		}

		/// <summary>
		/// Modify the rules for a newly created module, where the target is a different host platform.
		/// This is not required - but allows for hiding details of a particular platform.
		/// </summary>
		/// <param name="ModuleName">The name of the module</param>
		/// <param name="Rules">The module rules</param>
		/// <param name="Target">The target being build</param>
		public override void ModifyModuleRulesForOtherPlatform(string ModuleName, ModuleRules Rules, ReadOnlyTargetRules Target)
		{
		}

		/// <summary>
		/// Gets the application icon for a given project
		/// </summary>
		/// <param name="ProjectFile">The project file</param>
		/// <returns>The icon to use for this project</returns>
		public static FileReference GetApplicationIcon(FileReference ProjectFile)
		{
			// Check if there's a custom icon
			if(ProjectFile != null)
			{
				FileReference IconFile = FileReference.Combine(ProjectFile.Directory, "Build", "Windows", "Application.ico");
				if(FileReference.Exists(IconFile))
				{
					return IconFile;
				}
			}

			// Otherwise use the default
			return FileReference.Combine(UnrealBuildTool.EngineDirectory, "Build", "Windows", "Resources", "Default.ico");
		}

		/// <summary>
		/// Modify the rules for a newly created module, in a target that's being built for this platform.
		/// This is not required - but allows for hiding details of a particular platform.
		/// </summary>
		/// <param name="ModuleName">The name of the module</param>
		/// <param name="Rules">The module rules</param>
		/// <param name="Target">The target being build</param>
		public override void ModifyModuleRulesForActivePlatform(string ModuleName, ModuleRules Rules, ReadOnlyTargetRules Target)
		{
			bool bBuildShaderFormats = Target.bForceBuildShaderFormats;

			if (!Target.bBuildRequiresCookedData)
			{
				if (ModuleName == "TargetPlatform")
				{
					bBuildShaderFormats = true;
				}
			}

			// allow standalone tools to use target platform modules, without needing Engine
			if (ModuleName == "TargetPlatform")
			{
				if (Target.bForceBuildTargetPlatforms)
				{
					Rules.DynamicallyLoadedModuleNames.Add("WindowsTargetPlatform");
					Rules.DynamicallyLoadedModuleNames.Add("WindowsNoEditorTargetPlatform");
					Rules.DynamicallyLoadedModuleNames.Add("WindowsServerTargetPlatform");
					Rules.DynamicallyLoadedModuleNames.Add("WindowsClientTargetPlatform");
					Rules.DynamicallyLoadedModuleNames.Add("AllDesktopTargetPlatform");
				}

				if (bBuildShaderFormats)
				{
					Rules.DynamicallyLoadedModuleNames.Add("ShaderFormatD3D");
					Rules.DynamicallyLoadedModuleNames.Add("ShaderFormatOpenGL");
					Rules.DynamicallyLoadedModuleNames.Add("ShaderFormatVectorVM");

					Rules.DynamicallyLoadedModuleNames.Remove("VulkanRHI");
					Rules.DynamicallyLoadedModuleNames.Add("VulkanShaderFormat");
				}
			}

			if (ModuleName == "D3D11RHI")
			{
				// To enable platform specific D3D11 RHI Types
				Rules.PrivateIncludePaths.Add("Runtime/Windows/D3D11RHI/Private/Windows");
			}

			if (ModuleName == "D3D12RHI")
			{
				if (Target.WindowsPlatform.bPixProfilingEnabled && Target.Platform != UnrealTargetPlatform.Win32 && Target.Configuration != UnrealTargetConfiguration.Shipping)
				{
					// Define to indicate profiling enabled (64-bit only)
					Rules.PublicDefinitions.Add("D3D12_PROFILING_ENABLED=1");
					Rules.PublicDefinitions.Add("PROFILE");
					Rules.PublicDependencyModuleNames.Add("WinPixEventRuntime");
				}
				else
				{
					Rules.PublicDefinitions.Add("D3D12_PROFILING_ENABLED=0");
				}
			}

			// Delay-load D3D12 so we can use the latest features and still run on downlevel versions of the OS
			Rules.PublicDelayLoadDLLs.Add("d3d12.dll");
		}

		/// <summary>
		/// Setup the target environment for building
		/// </summary>
		/// <param name="Target">Settings for the target being compiled</param>
		/// <param name="CompileEnvironment">The compile environment for this target</param>
		/// <param name="LinkEnvironment">The link environment for this target</param>
		public override void SetUpEnvironment(ReadOnlyTargetRules Target, CppCompileEnvironment CompileEnvironment, LinkEnvironment LinkEnvironment)
		{
			// @todo Remove this hack to work around broken includes
			CompileEnvironment.Definitions.Add("NDIS_MINIPORT_MAJOR_VERSION=0");

			CompileEnvironment.Definitions.Add("WIN32=1");
			if (Target.WindowsPlatform.bUseWindowsSDK10)
			{
				CompileEnvironment.Definitions.Add(String.Format("_WIN32_WINNT=0x{0:X4}", 0x0602));
				CompileEnvironment.Definitions.Add(String.Format("WINVER=0x{0:X4}", 0x0602));

			}
			else
			{
				CompileEnvironment.Definitions.Add(String.Format("_WIN32_WINNT=0x{0:X4}", Target.WindowsPlatform.TargetWindowsVersion));
				CompileEnvironment.Definitions.Add(String.Format("WINVER=0x{0:X4}", Target.WindowsPlatform.TargetWindowsVersion));
			}
			
			CompileEnvironment.Definitions.Add("PLATFORM_WINDOWS=1");
			CompileEnvironment.Definitions.Add("PLATFORM_MICROSOFT=1");

			// both Win32 and Win64 use Windows headers, so we enforce that here
			CompileEnvironment.Definitions.Add(String.Format("OVERRIDE_PLATFORM_HEADER_NAME={0}", GetPlatformName()));

			// Ray tracing only supported on 64-bit Windows.
			if (Target.Platform == UnrealTargetPlatform.Win64 && Target.WindowsPlatform.bEnableRayTracing)
			{
				CompileEnvironment.Definitions.Add("RHI_RAYTRACING=1");
			}

			// Add path to Intel math libraries when using ICL based on target platform
			if (Target.WindowsPlatform.Compiler == WindowsCompiler.Intel)
			{
				string Result = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "IntelSWTools", "compilers_and_libraries", "windows", "compiler", "lib", Target.Platform == UnrealTargetPlatform.Win32 ? "ia32" : "intel64");
				if (!Directory.Exists(Result))
				{
					throw new BuildException("ICL was selected but the required math libraries were not found.  Could not find: " + Result);
				}

				LinkEnvironment.LibraryPaths.Add(new DirectoryReference(Result));
			}

			// Explicitly exclude the MS C++ runtime libraries we're not using, to ensure other libraries we link with use the same
			// runtime library as the engine.
			bool bUseDebugCRT = Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT;
			if (!Target.bUseStaticCRT || bUseDebugCRT)
			{
				LinkEnvironment.ExcludedLibraries.Add("LIBCMT");
				LinkEnvironment.ExcludedLibraries.Add("LIBCPMT");
			}
			if (!Target.bUseStaticCRT || !bUseDebugCRT)
			{
				LinkEnvironment.ExcludedLibraries.Add("LIBCMTD");
				LinkEnvironment.ExcludedLibraries.Add("LIBCPMTD");
			}
			if (Target.bUseStaticCRT || bUseDebugCRT)
			{
				LinkEnvironment.ExcludedLibraries.Add("MSVCRT");
				LinkEnvironment.ExcludedLibraries.Add("MSVCPRT");
			}
			if (Target.bUseStaticCRT || !bUseDebugCRT)
			{
				LinkEnvironment.ExcludedLibraries.Add("MSVCRTD");
				LinkEnvironment.ExcludedLibraries.Add("MSVCPRTD");
			}
			LinkEnvironment.ExcludedLibraries.Add("LIBC");
			LinkEnvironment.ExcludedLibraries.Add("LIBCP");
			LinkEnvironment.ExcludedLibraries.Add("LIBCD");
			LinkEnvironment.ExcludedLibraries.Add("LIBCPD");

			//@todo ATL: Currently, only VSAccessor requires ATL (which is only used in editor builds)
			// When compiling games, we do not want to include ATL - and we can't when compiling games
			// made with Launcher build due to VS 2012 Express not including ATL.
			// If more modules end up requiring ATL, this should be refactored into a BuildTarget flag (bNeedsATL)
			// that is set by the modules the target includes to allow for easier tracking.
			// Alternatively, if VSAccessor is modified to not require ATL than we should always exclude the libraries.
			if (Target.LinkType == TargetLinkType.Monolithic &&
				(Target.Type == TargetType.Game || Target.Type == TargetType.Client || Target.Type == TargetType.Server))
			{
				LinkEnvironment.ExcludedLibraries.Add("atl");
				LinkEnvironment.ExcludedLibraries.Add("atls");
				LinkEnvironment.ExcludedLibraries.Add("atlsd");
				LinkEnvironment.ExcludedLibraries.Add("atlsn");
				LinkEnvironment.ExcludedLibraries.Add("atlsnd");
			}

			// Add the library used for the delayed loading of DLLs.
			LinkEnvironment.AdditionalLibraries.Add("delayimp.lib");

			//@todo - remove once FB implementation uses Http module
			if (Target.bCompileAgainstEngine)
			{
				// link against wininet (used by FBX and Facebook)
				LinkEnvironment.AdditionalLibraries.Add("wininet.lib");
			}

			// Compile and link with Win32 API libraries.
			LinkEnvironment.AdditionalLibraries.Add("rpcrt4.lib");
			//LinkEnvironment.AdditionalLibraries.Add("wsock32.lib");
			LinkEnvironment.AdditionalLibraries.Add("ws2_32.lib");
			LinkEnvironment.AdditionalLibraries.Add("dbghelp.lib");
			LinkEnvironment.AdditionalLibraries.Add("comctl32.lib");
			LinkEnvironment.AdditionalLibraries.Add("Winmm.lib");
			LinkEnvironment.AdditionalLibraries.Add("kernel32.lib");
			LinkEnvironment.AdditionalLibraries.Add("user32.lib");
			LinkEnvironment.AdditionalLibraries.Add("gdi32.lib");
			LinkEnvironment.AdditionalLibraries.Add("winspool.lib");
			LinkEnvironment.AdditionalLibraries.Add("comdlg32.lib");
			LinkEnvironment.AdditionalLibraries.Add("advapi32.lib");
			LinkEnvironment.AdditionalLibraries.Add("shell32.lib");
			LinkEnvironment.AdditionalLibraries.Add("ole32.lib");
			LinkEnvironment.AdditionalLibraries.Add("oleaut32.lib");
			LinkEnvironment.AdditionalLibraries.Add("uuid.lib");
			LinkEnvironment.AdditionalLibraries.Add("odbc32.lib");
			LinkEnvironment.AdditionalLibraries.Add("odbccp32.lib");
			LinkEnvironment.AdditionalLibraries.Add("netapi32.lib");
			LinkEnvironment.AdditionalLibraries.Add("iphlpapi.lib");
			LinkEnvironment.AdditionalLibraries.Add("setupapi.lib"); //  Required for access monitor device enumeration

			// Windows Vista/7 Desktop Windows Manager API for Slate Windows Compliance
			LinkEnvironment.AdditionalLibraries.Add("dwmapi.lib");

			// IME
			LinkEnvironment.AdditionalLibraries.Add("imm32.lib");

			// For 64-bit builds, we'll forcibly ignore a linker warning with DirectInput.  This is
			// Microsoft's recommended solution as they don't have a fixed .lib for us.
			if (Target.Platform != UnrealTargetPlatform.Win32)
			{
				LinkEnvironment.AdditionalArguments += " /ignore:4078";
			}

			// Set up default stack size
			LinkEnvironment.DefaultStackSize = Target.WindowsPlatform.DefaultStackSize;
			LinkEnvironment.DefaultStackSizeCommit = Target.WindowsPlatform.DefaultStackSizeCommit;

			LinkEnvironment.ModuleDefinitionFile = Target.WindowsPlatform.ModuleDefinitionFile;
		}

		/// <summary>
		/// Setup the configuration environment for building
		/// </summary>
		/// <param name="Target"> The target being built</param>
		/// <param name="GlobalCompileEnvironment">The global compile environment</param>
		/// <param name="GlobalLinkEnvironment">The global link environment</param>
		public override void SetUpConfigurationEnvironment(ReadOnlyTargetRules Target, CppCompileEnvironment GlobalCompileEnvironment, LinkEnvironment GlobalLinkEnvironment)
		{
			base.SetUpConfigurationEnvironment(Target, GlobalCompileEnvironment, GlobalLinkEnvironment);

			// NOTE: Even when debug info is turned off, we currently force the linker to generate debug info
			//       anyway on Visual C++ platforms.  This will cause a PDB file to be generated with symbols
			//       for most of the classes and function/method names, so that crashes still yield somewhat
			//       useful call stacks, even though compiler-generate debug info may be disabled.  This gives
			//       us much of the build-time savings of fully-disabled debug info, without giving up call
			//       data completely.
			GlobalLinkEnvironment.bCreateDebugInfo = true;
		}

		/// <summary>
		/// Whether this platform should create debug information or not
		/// </summary>
		/// <param name="Target">The target being built</param>
		/// <returns>bool    true if debug info should be generated, false if not</returns>
		public override bool ShouldCreateDebugInfo(ReadOnlyTargetRules Target)
		{
			switch (Target.Configuration)
			{
				case UnrealTargetConfiguration.Development:
				case UnrealTargetConfiguration.Shipping:
				case UnrealTargetConfiguration.Test:
					return !Target.bOmitPCDebugInfoInDevelopment;
				case UnrealTargetConfiguration.DebugGame:
				case UnrealTargetConfiguration.Debug:
				default:
					return true;
			};
		}

		/// <summary>
		/// Creates a toolchain instance for the given platform.
		/// </summary>
		/// <param name="Target">The target being built</param>
		/// <returns>New toolchain instance.</returns>
		public override UEToolChain CreateToolChain(ReadOnlyTargetRules Target)
		{
			if (Target.WindowsPlatform.StaticAnalyzer == WindowsStaticAnalyzer.PVSStudio)
			{
				return new PVSToolChain(Target);
			}
			else
			{
				return new VCToolChain(Target);
			}
		}

		/// <summary>
		/// Allows the platform to return various build metadata that is not tracked by other means. If the returned string changes, the makefile will be invalidated.
		/// </summary>
		/// <param name="ProjectFile">The project file being built</param>
		/// <param name="Metadata">String builder to contain build metadata</param>
		public override void GetExternalBuildMetadata(FileReference ProjectFile, StringBuilder Metadata)
		{
			base.GetExternalBuildMetadata(ProjectFile, Metadata);

			if(ProjectFile != null)
			{
				Metadata.AppendLine("ICON: {0}", GetApplicationIcon(ProjectFile));
			}
		}

		/// <summary>
		/// Deploys the given target
		/// </summary>
		/// <param name="Receipt">Receipt for the target being deployed</param>
		public override void Deploy(TargetReceipt Receipt)
		{
		}
	}

	class WindowsPlatformSDK : UEBuildPlatformSDK
	{
		protected override SDKStatus HasRequiredManualSDKInternal()
		{
			return SDKStatus.Valid;
		}
	}

	class WindowsPlatformFactory : UEBuildPlatformFactory
	{
		public override UnrealTargetPlatform TargetPlatform
		{
			get { return UnrealTargetPlatform.Win64; }
		}

		/// <summary>
		/// Register the platform with the UEBuildPlatform class
		/// </summary>
		public override void RegisterBuildPlatforms()
		{
			WindowsPlatformSDK SDK = new WindowsPlatformSDK();
			SDK.ManageAndValidateSDK();

			// Register this build platform for both Win64 and Win32
			UEBuildPlatform.RegisterBuildPlatform(new WindowsPlatform(UnrealTargetPlatform.Win64, SDK));
			UEBuildPlatform.RegisterPlatformWithGroup(UnrealTargetPlatform.Win64, UnrealPlatformGroup.Windows);
			UEBuildPlatform.RegisterPlatformWithGroup(UnrealTargetPlatform.Win64, UnrealPlatformGroup.Microsoft);
			UEBuildPlatform.RegisterPlatformWithGroup(UnrealTargetPlatform.Win64, UnrealPlatformGroup.Desktop);

			UEBuildPlatform.RegisterBuildPlatform(new WindowsPlatform(UnrealTargetPlatform.Win32, SDK));
			UEBuildPlatform.RegisterPlatformWithGroup(UnrealTargetPlatform.Win32, UnrealPlatformGroup.Windows);
			UEBuildPlatform.RegisterPlatformWithGroup(UnrealTargetPlatform.Win32, UnrealPlatformGroup.Microsoft);
			UEBuildPlatform.RegisterPlatformWithGroup(UnrealTargetPlatform.Win32, UnrealPlatformGroup.Desktop);
		}
	}
}
