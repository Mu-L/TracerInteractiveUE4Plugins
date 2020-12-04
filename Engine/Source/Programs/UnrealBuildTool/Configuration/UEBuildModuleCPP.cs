// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// A module that is compiled from C++ code.
	/// </summary>
	class UEBuildModuleCPP : UEBuildModule
	{
		/// <summary>
		/// Stores a list of all source files, of different types
		/// </summary>
		public class InputFileCollection
		{
			public readonly List<FileItem> HeaderFiles = new List<FileItem>();
			public readonly List<FileItem> ISPCHeaderFiles = new List<FileItem>();

			public readonly List<FileItem> CPPFiles = new List<FileItem>();
			public readonly List<FileItem> CFiles = new List<FileItem>();
			public readonly List<FileItem> CCFiles = new List<FileItem>();
			public readonly List<FileItem> MMFiles = new List<FileItem>();
			public readonly List<FileItem> RCFiles = new List<FileItem>();
			public readonly List<FileItem> ISPCFiles = new List<FileItem>();
		}

		/// <summary>
		/// The directory for this module's generated code
		/// </summary>
		public readonly DirectoryReference GeneratedCodeDirectory;

		/// <summary>
		/// Set for modules that have generated code
		/// </summary>
		public bool bAddGeneratedCodeIncludePath;

		/// <summary>
		/// Paths containing *.gen.cpp files for this module.  If this is null then this module doesn't have any generated code.
		/// </summary>
		public List<string> GeneratedCppDirectories;

		/// <summary>
		/// List of invalid include directives. These are buffered up and output before we start compiling.
		/// </summary>
		public List<string> InvalidIncludeDirectiveMessages;

		/// <summary>
		/// Set of source directories referenced during a build
		/// </summary>
		HashSet<DirectoryReference> SourceDirectories;

		protected override void GetReferencedDirectories(HashSet<DirectoryReference> Directories)
		{
			base.GetReferencedDirectories(Directories);

			if(!Rules.bUsePrecompiled)
			{
				if(SourceDirectories == null)
				{
					throw new BuildException("GetReferencedDirectories() should not be called before building.");
				}
				Directories.UnionWith(SourceDirectories);
			}
		}

		/// <summary>
		/// List of whitelisted circular dependencies. Please do NOT add new modules here; refactor to allow the modules to be decoupled instead.
		/// </summary>
		static readonly KeyValuePair<string, string>[] WhitelistedCircularDependencies =
		{
			new KeyValuePair<string, string>("AIModule", "AITestSuite"),
			new KeyValuePair<string, string>("AnimGraph", "UnrealEd"),
			new KeyValuePair<string, string>("AnimGraph", "GraphEditor"),
			new KeyValuePair<string, string>("AudioMixer", "NonRealtimeAudioRenderer"),
			new KeyValuePair<string, string>("AudioMixer", "SoundFieldRendering"),
			new KeyValuePair<string, string>("AudioEditor", "DetailCustomizations"),
			new KeyValuePair<string, string>("BlueprintGraph", "KismetCompiler"),
			new KeyValuePair<string, string>("BlueprintGraph", "UnrealEd"),
			new KeyValuePair<string, string>("BlueprintGraph", "GraphEditor"),
			new KeyValuePair<string, string>("BlueprintGraph", "Kismet"),
			new KeyValuePair<string, string>("BlueprintGraph", "CinematicCamera"),
			new KeyValuePair<string, string>("ConfigEditor", "PropertyEditor"),
			new KeyValuePair<string, string>("Documentation", "SourceControl"),
			new KeyValuePair<string, string>("Engine", "Landscape"),
			new KeyValuePair<string, string>("Engine", "UMG"),
			new KeyValuePair<string, string>("Engine", "GameplayTags"),
			new KeyValuePair<string, string>("Engine", "MaterialShaderQualitySettings"),
			new KeyValuePair<string, string>("Engine", "UnrealEd"),
			new KeyValuePair<string, string>("Engine", "AudioMixer"),
			new KeyValuePair<string, string>("Engine", "CinematicCamera"),
			new KeyValuePair<string, string>("Engine", "CollisionAnalyzer"),
			new KeyValuePair<string, string>("Engine", "LogVisualizer"),
			new KeyValuePair<string, string>("Engine", "Kismet"),
			new KeyValuePair<string, string>("FoliageEdit", "ViewportInteraction"),
			new KeyValuePair<string, string>("FoliageEdit", "VREditor"),
			new KeyValuePair<string, string>("FunctionalTesting", "UnrealEd"),
			new KeyValuePair<string, string>("GameplayAbilitiesEditor", "BlueprintGraph"),
			new KeyValuePair<string, string>("GameplayDebugger", "AIModule"),
			new KeyValuePair<string, string>("GameplayDebugger", "GameplayTasks"),
			new KeyValuePair<string, string>("GameplayTasks", "UnrealEd"),
			new KeyValuePair<string, string>("GraphEditor", "Kismet"),
			new KeyValuePair<string, string>("HierarchicalLODOutliner", "UnrealEd"),
			new KeyValuePair<string, string>("Kismet", "BlueprintGraph"),
			new KeyValuePair<string, string>("Kismet", "BlueprintNativeCodeGen"),
			new KeyValuePair<string, string>("Kismet", "UMGEditor"),
			new KeyValuePair<string, string>("Kismet", "Merge"),
			new KeyValuePair<string, string>("KismetWidgets", "BlueprintGraph"),
			new KeyValuePair<string, string>("Landscape", "UnrealEd"),
			new KeyValuePair<string, string>("Landscape", "MaterialUtilities"),
			new KeyValuePair<string, string>("LandscapeEditor", "ViewportInteraction"),
			new KeyValuePair<string, string>("LandscapeEditor", "VREditor"),
			new KeyValuePair<string, string>("LocalizationDashboard", "LocalizationService"),
			new KeyValuePair<string, string>("LocalizationDashboard", "MainFrame"),
			new KeyValuePair<string, string>("LocalizationDashboard", "TranslationEditor"),
			new KeyValuePair<string, string>("MaterialUtilities", "Landscape"),
			new KeyValuePair<string, string>("MeshPaint", "ViewportInteraction"),
			new KeyValuePair<string, string>("MeshPaint", "VREditor"),
			new KeyValuePair<string, string>("MeshPaintMode", "ViewportInteraction"),
			new KeyValuePair<string, string>("MeshPaintMode", "VREditor"),
			new KeyValuePair<string, string>("MovieSceneTools", "Sequencer"),
			new KeyValuePair<string, string>("NavigationSystem", "UnrealEd"),
			new KeyValuePair<string, string>("PacketHandler", "ReliabilityHandlerComponent"),
			new KeyValuePair<string, string>("PIEPreviewDeviceProfileSelector", "UnrealEd"),
			new KeyValuePair<string, string>("PixelInspectorModule", "UnrealEd"),
			new KeyValuePair<string, string>("Sequencer", "MovieSceneTools"),
			new KeyValuePair<string, string>("Sequencer", "ViewportInteraction"),
			new KeyValuePair<string, string>("SourceControl", "UnrealEd"),
			new KeyValuePair<string, string>("UnrealEd", "AudioEditor"),
			new KeyValuePair<string, string>("UnrealEd", "ClothingSystemEditor"),
			new KeyValuePair<string, string>("UnrealEd", "Documentation"),
			new KeyValuePair<string, string>("UnrealEd", "EditorInteractiveToolsFramework"),
			new KeyValuePair<string, string>("UnrealEd", "GraphEditor"),
			new KeyValuePair<string, string>("UnrealEd", "InputBindingEditor"),
			new KeyValuePair<string, string>("UnrealEd", "Kismet"),
			new KeyValuePair<string, string>("UnrealEd", "MeshPaint"),
			new KeyValuePair<string, string>("UnrealEd", "MeshPaintMode"),
			new KeyValuePair<string, string>("UnrealEd", "PluginWarden"),
			new KeyValuePair<string, string>("UnrealEd", "PropertyEditor"),
			new KeyValuePair<string, string>("UnrealEd", "ToolMenusEditor"),
			new KeyValuePair<string, string>("UnrealEd", "ViewportInteraction"),
			new KeyValuePair<string, string>("UnrealEd", "VREditor"),
			new KeyValuePair<string, string>("WebBrowser", "WebBrowserTexture"),
			new KeyValuePair<string, string>("WindowsMixedRealityHMD", "WindowsMixedRealityHandTracking"),
		};


		public UEBuildModuleCPP(ModuleRules Rules, DirectoryReference IntermediateDirectory, DirectoryReference GeneratedCodeDirectory)
			: base(Rules, IntermediateDirectory)
		{
			this.GeneratedCodeDirectory = GeneratedCodeDirectory;

			foreach (string Def in PublicDefinitions)
			{
				Log.TraceVerbose("Compile Env {0}: {1}", Name, Def);
			}

			foreach (string Def in Rules.PrivateDefinitions)
			{
				Log.TraceVerbose("Compile Env {0}: {1}", Name, Def);
			}

			foreach(string CircularlyReferencedModuleName in Rules.CircularlyReferencedDependentModules)
			{
				if(CircularlyReferencedModuleName != "BlueprintContext" && !WhitelistedCircularDependencies.Any(x => x.Key == Name && x.Value == CircularlyReferencedModuleName))
				{
					Log.TraceWarning("Found reference between '{0}' and '{1}'. Support for circular references is being phased out; please do not introduce new ones.", Name, CircularlyReferencedModuleName);
				}
			}

			AddDefaultIncludePaths();
		}

		/// <summary>
		/// Determines if a file is part of the given module
		/// </summary>
		/// <param name="Location">Path to the file</param>
		/// <returns>True if the file is part of this module</returns>
		public override bool ContainsFile(FileReference Location)
		{
			if (base.ContainsFile(Location))
			{
				return true;
			}
			if (GeneratedCodeDirectory != null && Location.IsUnderDirectory(GeneratedCodeDirectory))
			{
				return true;
			}
			return false;
		}

		/// <summary>
		/// Add the default include paths for this module to its settings
		/// </summary>
		private void AddDefaultIncludePaths()
		{
			// Add the module's parent directory to the public include paths, so other modules may include headers from it explicitly.
			foreach (DirectoryReference ModuleDir in ModuleDirectories)
			{
				PublicIncludePaths.Add(ModuleDir.ParentDirectory);

				// Add the base directory to the legacy include paths.
				LegacyPublicIncludePaths.Add(ModuleDir);

				// Add the 'classes' directory, if it exists
				DirectoryReference ClassesDirectory = DirectoryReference.Combine(ModuleDir, "Classes");
				if (DirectoryLookupCache.DirectoryExists(ClassesDirectory))
				{
					PublicIncludePaths.Add(ClassesDirectory);
				}

				// Add all the public directories
				DirectoryReference PublicDirectory = DirectoryReference.Combine(ModuleDir, "Public");
				if (DirectoryLookupCache.DirectoryExists(PublicDirectory))
				{
					PublicIncludePaths.Add(PublicDirectory);

					ReadOnlyHashSet<string> ExcludeNames = UEBuildPlatform.GetBuildPlatform(Rules.Target.Platform).GetExcludedFolderNames();
					EnumerateLegacyIncludePaths(DirectoryItem.GetItemByDirectoryReference(PublicDirectory), ExcludeNames, LegacyPublicIncludePaths);
				}

				// Add the base private directory for this module
				DirectoryReference PrivateDirectory = DirectoryReference.Combine(ModuleDir, "Private");
				if (DirectoryLookupCache.DirectoryExists(PrivateDirectory))
				{
					PrivateIncludePaths.Add(PrivateDirectory);
				}
			}
		}

		/// <summary>
		/// Enumerates legacy include paths under a given base directory
		/// </summary>
		/// <param name="BaseDirectory">The directory to start from. This directory is not added to the output list.</param>
		/// <param name="ExcludeNames">Set of folder names to exclude from the search.</param>
		/// <param name="LegacyPublicIncludePaths">List populated with the discovered directories</param>
		static void EnumerateLegacyIncludePaths(DirectoryItem BaseDirectory, ReadOnlyHashSet<string> ExcludeNames, HashSet<DirectoryReference> LegacyPublicIncludePaths)
		{
			foreach(DirectoryItem SubDirectory in BaseDirectory.EnumerateDirectories())
			{
				if(!ExcludeNames.Contains(SubDirectory.Name))
				{
					LegacyPublicIncludePaths.Add(SubDirectory.Location);
					EnumerateLegacyIncludePaths(SubDirectory, ExcludeNames, LegacyPublicIncludePaths);
				}
			}
		}

		/// <summary>
		/// Path to the precompiled manifest location
		/// </summary>
		public virtual FileReference PrecompiledManifestLocation
		{
			get { return FileReference.Combine(IntermediateDirectory, String.Format("{0}.precompiled", Name)); }
		}

		/// <summary>
		/// Sets up the environment for compiling any module that includes the public interface of this module.
		/// </summary>
		public override void AddModuleToCompileEnvironment(
			UEBuildBinary SourceBinary,
			HashSet<DirectoryReference> IncludePaths,
			HashSet<DirectoryReference> SystemIncludePaths,
			List<string> Definitions,
			List<UEBuildFramework> AdditionalFrameworks,
			List<FileItem> AdditionalPrerequisites,
			bool bLegacyPublicIncludePaths
			)
		{
			// This directory may not exist for this module (or ever exist, if it doesn't contain any generated headers), but we want the project files
			// to search it so we can pick up generated code definitions after UHT is run for the first time.
			if(bAddGeneratedCodeIncludePath || (ProjectFileGenerator.bGenerateProjectFiles && GeneratedCodeDirectory != null))
			{
				IncludePaths.Add(GeneratedCodeDirectory);

				if (Rules.AdditionalCodeGenDirectories != null)
				{
					foreach (string CodeGenDir in Rules.AdditionalCodeGenDirectories)
					{
						if (Directory.Exists(CodeGenDir))
						{
							IncludePaths.Add(new DirectoryReference(CodeGenDir));
						}
					}
				}
			}

			base.AddModuleToCompileEnvironment(SourceBinary, IncludePaths, SystemIncludePaths, Definitions, AdditionalFrameworks, AdditionalPrerequisites, bLegacyPublicIncludePaths);
		}

		// UEBuildModule interface.
		public override List<FileItem> Compile(ReadOnlyTargetRules Target, UEToolChain ToolChain, CppCompileEnvironment BinaryCompileEnvironment, List<FileReference> SpecificFilesToCompile, ISourceFileWorkingSet WorkingSet, IActionGraphBuilder Graph)
		{
			//UEBuildPlatform BuildPlatform = UEBuildPlatform.GetBuildPlatform(BinaryCompileEnvironment.Platform);

			List<FileItem> LinkInputFiles = base.Compile(Target, ToolChain, BinaryCompileEnvironment, SpecificFilesToCompile, WorkingSet, Graph);

			CppCompileEnvironment ModuleCompileEnvironment = CreateModuleCompileEnvironment(Target, BinaryCompileEnvironment);

			// If the module is precompiled, read the object files from the manifest
			if(Rules.bUsePrecompiled && Target.LinkType == TargetLinkType.Monolithic)
			{
				if(!FileReference.Exists(PrecompiledManifestLocation))
				{
					throw new BuildException("Missing precompiled manifest for '{0}'. This module was most likely not flagged for being included in a precompiled build - set 'PrecompileForTargets = PrecompileTargetsType.Any;' in {0}.build.cs to override.", Name);
				}

				PrecompiledManifest Manifest = PrecompiledManifest.Read(PrecompiledManifestLocation);
				foreach(FileReference OutputFile in Manifest.OutputFiles)
				{
					FileItem ObjectFile = FileItem.GetItemByFileReference(OutputFile);
					if(!ObjectFile.Exists)
					{
						throw new BuildException("Missing object file {0} listed in {1}", OutputFile, PrecompiledManifestLocation);
					}
					LinkInputFiles.Add(ObjectFile);
				}
				return LinkInputFiles;
			}

			// Add all the module source directories to the makefile
			foreach (DirectoryReference ModuleDirectory in ModuleDirectories)
			{
				DirectoryItem ModuleDirectoryItem = DirectoryItem.GetItemByDirectoryReference(ModuleDirectory);
				Graph.AddSourceDir(ModuleDirectoryItem);
			}

			// Find all the input files
			Dictionary<DirectoryItem, FileItem[]> DirectoryToSourceFiles = new Dictionary<DirectoryItem, FileItem[]>();
			InputFileCollection InputFiles = FindInputFiles(Target.Platform, DirectoryToSourceFiles);

			foreach (KeyValuePair<DirectoryItem, FileItem[]> Pair in DirectoryToSourceFiles)
			{
				Graph.AddSourceFiles(Pair.Key, Pair.Value);
			}

			// If we're compiling only specific files, strip out anything else. This prevents us clobbering response files for anything we're 
			// not going to build, triggering a larger build than necessary when we do a regular build again.
			if(SpecificFilesToCompile.Count > 0)
			{
				InputFiles.CPPFiles.RemoveAll(x => !SpecificFilesToCompile.Contains(x.Location));
				InputFiles.CCFiles.RemoveAll(x => !SpecificFilesToCompile.Contains(x.Location));
				InputFiles.CFiles.RemoveAll(x => !SpecificFilesToCompile.Contains(x.Location));

				if (InputFiles.CPPFiles.Count == 0 && InputFiles.CCFiles.Count == 0 && InputFiles.CFiles.Count == 0 &&
					!SpecificFilesToCompile.Any(x => ContainsFile(x)))
				{
					return new List<FileItem>();
				}
			}

			// Process all of the header file dependencies for this module
			CheckFirstIncludeMatchesEachCppFile(Target, ModuleCompileEnvironment, InputFiles.HeaderFiles, InputFiles.CPPFiles);

			// Should we force a precompiled header to be generated for this module?  Usually, we only bother with a
			// precompiled header if there are at least several source files in the module (after combining them for unity
			// builds.)  But for game modules, it can be convenient to always have a precompiled header to single-file
			// changes to code is really quick to compile.
			int MinFilesUsingPrecompiledHeader = Target.MinFilesUsingPrecompiledHeader;
			if (Rules.MinFilesUsingPrecompiledHeaderOverride != 0)
			{
				MinFilesUsingPrecompiledHeader = Rules.MinFilesUsingPrecompiledHeaderOverride;
			}
			else if (!Rules.bTreatAsEngineModule && Target.bForcePrecompiledHeaderForGameModules)
			{
				// This is a game module with only a small number of source files, so go ahead and force a precompiled header
				// to be generated to make incremental changes to source files as fast as possible for small projects.
				MinFilesUsingPrecompiledHeader = 1;
			}

			// Engine modules will always use unity build mode unless MinSourceFilesForUnityBuildOverride is specified in
			// the module rules file.  By default, game modules only use unity of they have enough source files for that
			// to be worthwhile.  If you have a lot of small game modules, consider specifying MinSourceFilesForUnityBuildOverride=0
			// in the modules that you don't typically iterate on source files in very frequently.
			int MinSourceFilesForUnityBuild = 2;
			if (Rules.MinSourceFilesForUnityBuildOverride != 0)
			{
				MinSourceFilesForUnityBuild = Rules.MinSourceFilesForUnityBuildOverride;
			}
			else if (Target.ProjectFile != null && RulesFile.IsUnderDirectory(DirectoryReference.Combine(Target.ProjectFile.Directory, "Source")))
			{
				// Game modules with only a small number of source files are usually better off having faster iteration times
				// on single source file changes, so we forcibly disable unity build for those modules
				MinSourceFilesForUnityBuild = Target.MinGameModuleSourceFilesForUnityBuild;
			}

			// Should we use unity build mode for this module?
			bool bModuleUsesUnityBuild = false;
	
			if (Target.bUseUnityBuild || Target.bForceUnityBuild)
			{
				if (Target.bForceUnityBuild)
				{
					Log.TraceVerbose("Module '{0}' using unity build mode (bForceUnityBuild enabled for this module)", this.Name);
					bModuleUsesUnityBuild = true;
				}
				else if (!Rules.bUseUnity)
				{
					Log.TraceVerbose("Module '{0}' not using unity build mode (bUseUnity disabled for this module)", this.Name);
					bModuleUsesUnityBuild = false;
				}
				else if (InputFiles.CPPFiles.Count < MinSourceFilesForUnityBuild)
				{
					Log.TraceVerbose("Module '{0}' not using unity build mode (module with fewer than {1} source files)", this.Name, MinSourceFilesForUnityBuild);
					bModuleUsesUnityBuild = false;
				}
				else
				{
					Log.TraceVerbose("Module '{0}' using unity build mode", this.Name);
					bModuleUsesUnityBuild = true;
				}
			}
			else
			{
				Log.TraceVerbose("Module '{0}' not using unity build mode", this.Name);
			}

			// Set up the environment with which to compile the CPP files
			CppCompileEnvironment CompileEnvironment = ModuleCompileEnvironment;

			// Generate ISPC headers first so C++ can consume them
			if (InputFiles.ISPCFiles.Count > 0)
			{
				CreateHeadersForISPC(ToolChain, CompileEnvironment, InputFiles.ISPCFiles, IntermediateDirectory, Graph);
			}

			// Configure the precompiled headers for this module
			CompileEnvironment = SetupPrecompiledHeaders(Target, ToolChain, CompileEnvironment, LinkInputFiles, Graph);

			// Write all the definitions to a separate file
			CreateHeaderForDefinitions(CompileEnvironment, IntermediateDirectory, null, Graph);

			// Mapping of source file to unity file. We output this to intermediate directories for other tools (eg. live coding) to use.
			Dictionary<FileItem, FileItem> SourceFileToUnityFile = new Dictionary<FileItem, FileItem>();

			// Compile CPP files
			List<FileItem> CPPFilesToCompile = InputFiles.CPPFiles;
			if (bModuleUsesUnityBuild)
			{
				CPPFilesToCompile = Unity.GenerateUnityCPPs(Target, CPPFilesToCompile, CompileEnvironment, WorkingSet, Rules.ShortName ?? Name, IntermediateDirectory, Graph, SourceFileToUnityFile);
				LinkInputFiles.AddRange(CompileUnityFilesWithToolChain(Target, ToolChain, CompileEnvironment, ModuleCompileEnvironment, CPPFilesToCompile, Graph).ObjectFiles);
			}
			else
			{
				LinkInputFiles.AddRange(ToolChain.CompileCPPFiles(CompileEnvironment, CPPFilesToCompile, IntermediateDirectory, Name, Graph).ObjectFiles);
			}

			// Compile all the generated CPP files
			if (GeneratedCppDirectories != null && !CompileEnvironment.bHackHeaderGenerator && SpecificFilesToCompile.Count == 0)
			{
				List<string> GeneratedFiles = new List<string>();
				foreach (string GeneratedDir in GeneratedCppDirectories)
				{
					GeneratedFiles.AddRange(Directory.GetFiles(GeneratedDir, "*.gen.cpp"));
				}

				if(GeneratedFiles.Count > 0)
				{
					// Create a compile environment for the generated files. We can disable creating debug info here to improve link times.
					CppCompileEnvironment GeneratedCPPCompileEnvironment = CompileEnvironment;
					if(GeneratedCPPCompileEnvironment.bCreateDebugInfo && Target.bDisableDebugInfoForGeneratedCode)
					{
						GeneratedCPPCompileEnvironment = new CppCompileEnvironment(GeneratedCPPCompileEnvironment);
						GeneratedCPPCompileEnvironment.bCreateDebugInfo = false;
					}

					// Always force include the PCH, even if PCHs are disabled, for generated code. Legacy code can rely on PCHs being included to compile correctly, and this used to be done by UHT manually including it.
					if(GeneratedCPPCompileEnvironment.PrecompiledHeaderFile == null && Rules.PrivatePCHHeaderFile != null && Rules.PCHUsage != ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs)
					{
						FileItem PrivatePchFileItem = FileItem.GetItemByFileReference(FileReference.Combine(ModuleDirectory, Rules.PrivatePCHHeaderFile));
						if(!PrivatePchFileItem.Exists)
						{
							throw new BuildException("Unable to find private PCH file '{0}', referenced by '{1}'", PrivatePchFileItem.Location, RulesFile);
						}

						GeneratedCPPCompileEnvironment = new CppCompileEnvironment(GeneratedCPPCompileEnvironment);
						GeneratedCPPCompileEnvironment.ForceIncludeFiles.Add(PrivatePchFileItem);
					}

					// Compile all the generated files
					List<FileItem> GeneratedFileItems = new List<FileItem>();
					foreach (string GeneratedFilename in GeneratedFiles)
					{
						FileItem GeneratedCppFileItem = FileItem.GetItemByPath(GeneratedFilename);
						if (SpecificFilesToCompile.Count == 0 || SpecificFilesToCompile.Contains(GeneratedCppFileItem.Location))
						{
							GeneratedFileItems.Add(GeneratedCppFileItem);
						}
					}

					if (bModuleUsesUnityBuild)
					{
						GeneratedFileItems = Unity.GenerateUnityCPPs(Target, GeneratedFileItems, GeneratedCPPCompileEnvironment, WorkingSet, (Rules.ShortName ?? Name) + ".gen", IntermediateDirectory, Graph, SourceFileToUnityFile);
						LinkInputFiles.AddRange(CompileUnityFilesWithToolChain(Target, ToolChain, GeneratedCPPCompileEnvironment, ModuleCompileEnvironment, GeneratedFileItems, Graph).ObjectFiles);
					}
					else
					{
						LinkInputFiles.AddRange(ToolChain.CompileCPPFiles(GeneratedCPPCompileEnvironment, GeneratedFileItems, IntermediateDirectory, Name, Graph).ObjectFiles);
					}
				}
			}

			// Compile ISPC files directly
			if (InputFiles.ISPCFiles.Count > 0)
			{
				LinkInputFiles.AddRange(ToolChain.CompileISPCFiles(CompileEnvironment, InputFiles.ISPCFiles, IntermediateDirectory, Graph).ObjectFiles);
			}

			// Compile C files directly. Do not use a PCH here, because a C++ PCH is not compatible with C source files.
			if(InputFiles.CFiles.Count > 0)
			{
				LinkInputFiles.AddRange(ToolChain.CompileCPPFiles(ModuleCompileEnvironment, InputFiles.CFiles, IntermediateDirectory, Name, Graph).ObjectFiles);
			}

			// Compile CC files directly.
			if(InputFiles.CCFiles.Count > 0)
			{
				LinkInputFiles.AddRange(ToolChain.CompileCPPFiles(CompileEnvironment, InputFiles.CCFiles, IntermediateDirectory, Name, Graph).ObjectFiles);
			}

			// Compile MM files directly.
			if(InputFiles.MMFiles.Count > 0)
			{
				LinkInputFiles.AddRange(ToolChain.CompileCPPFiles(CompileEnvironment, InputFiles.MMFiles, IntermediateDirectory, Name, Graph).ObjectFiles);
			}

			// Compile RC files. The resource compiler does not work with response files, and using the regular compile environment can easily result in the 
			// command line length exceeding the OS limit. Use the binary compile environment to keep the size down, and require that all include paths
			// must be specified relative to the resource file itself or Engine/Source.
			if(InputFiles.RCFiles.Count > 0)
			{
				CppCompileEnvironment ResourceCompileEnvironment = new CppCompileEnvironment(BinaryCompileEnvironment);
				if(Binary != null)
				{
					// @todo: This should be in some Windows code somewhere...
					ResourceCompileEnvironment.Definitions.Add("ORIGINAL_FILE_NAME=\"" + Binary.OutputFilePaths[0].GetFileName() + "\"");
				}
				LinkInputFiles.AddRange(ToolChain.CompileRCFiles(ResourceCompileEnvironment, InputFiles.RCFiles, IntermediateDirectory, Graph).ObjectFiles);
			}

			// Write the compiled manifest
			if(Rules.bPrecompile && Target.LinkType == TargetLinkType.Monolithic)
			{
				DirectoryReference.CreateDirectory(PrecompiledManifestLocation.Directory);

				PrecompiledManifest Manifest = new PrecompiledManifest();
				Manifest.OutputFiles.AddRange(LinkInputFiles.Select(x => x.Location));
				Manifest.WriteIfModified(PrecompiledManifestLocation);
			}

			// Write a mapping of unity object file to standalone object file for live coding
			if(Rules.Target.bWithLiveCoding)
			{
				FileReference UnityManifestFile = FileReference.Combine(IntermediateDirectory, "LiveCodingInfo.json");
				using (JsonWriter Writer = new JsonWriter(UnityManifestFile))
				{
					Writer.WriteObjectStart();
					Writer.WriteObjectStart("RemapUnityFiles");
					foreach (IGrouping<FileItem, KeyValuePair<FileItem, FileItem>> UnityGroup in SourceFileToUnityFile.GroupBy(x => x.Value))
					{
						Writer.WriteArrayStart(UnityGroup.Key.Location.GetFileName() + ".obj");
						foreach (FileItem SourceFile in UnityGroup.Select(x => x.Key))
						{
							Writer.WriteValue(SourceFile.Location.GetFileName() + ".obj");
						}
						Writer.WriteArrayEnd();
					}
					Writer.WriteObjectEnd();
					Writer.WriteObjectEnd();
				}
			}

			return LinkInputFiles;
		}

		/// <summary>
		/// Create a shared PCH template for this module, which allows constructing shared PCH instances in the future
		/// </summary>
		/// <param name="Target">The target which owns this module</param>
		/// <param name="BaseCompileEnvironment">Base compile environment for this target</param>
		/// <returns>Template for shared PCHs</returns>
		public PrecompiledHeaderTemplate CreateSharedPCHTemplate(UEBuildTarget Target, CppCompileEnvironment BaseCompileEnvironment)
		{
			CppCompileEnvironment CompileEnvironment = CreateSharedPCHCompileEnvironment(Target, BaseCompileEnvironment);
			FileItem HeaderFile = FileItem.GetItemByFileReference(FileReference.Combine(ModuleDirectory, Rules.SharedPCHHeaderFile));

			DirectoryReference PrecompiledHeaderDir;
			if(Rules.bUsePrecompiled)
			{
				PrecompiledHeaderDir = DirectoryReference.Combine(Target.ProjectIntermediateDirectory, Name);
			}
			else
			{
				PrecompiledHeaderDir = IntermediateDirectory;
			}

			return new PrecompiledHeaderTemplate(this, CompileEnvironment, HeaderFile, PrecompiledHeaderDir);
		}

		/// <summary>
		/// Creates a precompiled header action to generate a new pch file 
		/// </summary>
		/// <param name="ToolChain">The toolchain to generate the PCH</param>
		/// <param name="HeaderFile"></param>
		/// <param name="ModuleCompileEnvironment"></param>
		/// <param name="Graph">List of actions to be executed. Additional actions will be added to this list.</param>
		/// <returns>The created PCH instance.</returns>
		private PrecompiledHeaderInstance CreatePrivatePCH(UEToolChain ToolChain, FileItem HeaderFile, CppCompileEnvironment ModuleCompileEnvironment, IActionGraphBuilder Graph)
		{
			// Create the wrapper file, which sets all the definitions needed to compile it
			FileReference WrapperLocation = FileReference.Combine(IntermediateDirectory, String.Format("PCH.{0}.h", Name));
			FileItem WrapperFile = CreatePCHWrapperFile(WrapperLocation, ModuleCompileEnvironment.Definitions, HeaderFile, Graph);

			// Create a new C++ environment that is used to create the PCH.
			CppCompileEnvironment CompileEnvironment = new CppCompileEnvironment(ModuleCompileEnvironment);
			CompileEnvironment.Definitions.Clear();
			CompileEnvironment.PrecompiledHeaderAction = PrecompiledHeaderAction.Create;
			CompileEnvironment.PrecompiledHeaderIncludeFilename = WrapperFile.Location;
			CompileEnvironment.bOptimizeCode = ModuleCompileEnvironment.bOptimizeCode;

			// Create the action to compile the PCH file.
			CPPOutput Output;
			if (ToolChain == null)
			{
				Output = new CPPOutput();
			}
			else
			{
				Output = ToolChain.CompileCPPFiles(CompileEnvironment, new List<FileItem>() { WrapperFile }, IntermediateDirectory, Name, Graph);
			}
			return new PrecompiledHeaderInstance(WrapperFile, CompileEnvironment, Output);
		}

		/// <summary>
		/// Generates a precompiled header instance from the given template, or returns an existing one if it already exists
		/// </summary>
		/// <param name="ToolChain">The toolchain being used to build this module</param>
		/// <param name="Template">The PCH template</param>
		/// <param name="ModuleCompileEnvironment">Compile environment for the current module</param>
		/// <param name="Graph">List of actions to be executed. Additional actions will be added to this list.</param>
		/// <returns>Instance of a PCH</returns>
		public PrecompiledHeaderInstance FindOrCreateSharedPCH(UEToolChain ToolChain, PrecompiledHeaderTemplate Template, CppCompileEnvironment ModuleCompileEnvironment, IActionGraphBuilder Graph)
		{
			PrecompiledHeaderInstance Instance = Template.Instances.Find(x => IsCompatibleForSharedPCH(x.CompileEnvironment, ModuleCompileEnvironment));
			if(Instance == null)
			{
				// Create a suffix to distinguish this shared PCH variant from any others. Currently only optimized and non-optimized shared PCHs are supported.
				string Variant = GetSuffixForSharedPCH(ModuleCompileEnvironment, Template.BaseCompileEnvironment);

				// Create the wrapper file, which sets all the definitions needed to compile it
				FileReference WrapperLocation = FileReference.Combine(Template.OutputDir, String.Format("SharedPCH.{0}{1}.h", Template.Module.Name, Variant));
				FileItem WrapperFile = CreatePCHWrapperFile(WrapperLocation, Template.BaseCompileEnvironment.Definitions, Template.HeaderFile, Graph);

				// Create the compile environment for this PCH
				CppCompileEnvironment CompileEnvironment = new CppCompileEnvironment(Template.BaseCompileEnvironment);
				CompileEnvironment.Definitions.Clear();
				CompileEnvironment.PrecompiledHeaderAction = PrecompiledHeaderAction.Create;
				CompileEnvironment.PrecompiledHeaderIncludeFilename = WrapperFile.Location;
				CopySettingsForSharedPCH(ModuleCompileEnvironment, CompileEnvironment);

				// Create the PCH
				CPPOutput Output;
				if (ToolChain == null)
				{
					Output = new CPPOutput();
				}
				else
				{
					Output = ToolChain.CompileCPPFiles(CompileEnvironment, new List<FileItem>() { WrapperFile }, Template.OutputDir, "Shared", Graph);
				}
				Instance = new PrecompiledHeaderInstance(WrapperFile, CompileEnvironment, Output);
				Template.Instances.Add(Instance);
			}
			return Instance;
		}

		/// <summary>
		/// Determines if a module compile environment is compatible with the given shared PCH compile environment
		/// </summary>
		/// <param name="ModuleCompileEnvironment">The module compile environment</param>
		/// <param name="CompileEnvironment">The shared PCH compile environment</param>
		/// <returns>True if the two compile enviroments are compatible</returns>
		private bool IsCompatibleForSharedPCH(CppCompileEnvironment ModuleCompileEnvironment, CppCompileEnvironment CompileEnvironment)
		{
			if(ModuleCompileEnvironment.bOptimizeCode != CompileEnvironment.bOptimizeCode)
			{
				return false;
			}
			if(ModuleCompileEnvironment.bUseRTTI != CompileEnvironment.bUseRTTI)
			{
				return false;
			}
			if(ModuleCompileEnvironment.bEnableExceptions != CompileEnvironment.bEnableExceptions)
			{
				return false;
			}
			if(ModuleCompileEnvironment.ShadowVariableWarningLevel != CompileEnvironment.ShadowVariableWarningLevel)
			{
				return false;
			}
			if(ModuleCompileEnvironment.UnsafeTypeCastWarningLevel != CompileEnvironment.UnsafeTypeCastWarningLevel)
			{
				return false;
			}
			if(ModuleCompileEnvironment.bEnableUndefinedIdentifierWarnings != CompileEnvironment.bEnableUndefinedIdentifierWarnings)
			{
				return false;
			}
			if (ModuleCompileEnvironment.CppStandard != CompileEnvironment.CppStandard)
			{
				return false;
			}
			return true;
		}

		/// <summary>
		/// Gets the unique suffix for a shared PCH
		/// </summary>
		/// <param name="CompileEnvironment">The shared PCH compile environment</param>
		/// <param name="BaseCompileEnvironment">The base compile environment</param>
		/// <returns>The unique suffix for the shared PCH</returns>
		private string GetSuffixForSharedPCH(CppCompileEnvironment CompileEnvironment, CppCompileEnvironment BaseCompileEnvironment)
		{
			string Variant = "";
			if(CompileEnvironment.bOptimizeCode != BaseCompileEnvironment.bOptimizeCode)
			{
				if(CompileEnvironment.bOptimizeCode)
				{
					Variant += ".Optimized";
				}
				else
				{
					Variant += ".NonOptimized";
				}
			}
			if(CompileEnvironment.bUseRTTI != BaseCompileEnvironment.bUseRTTI)
			{
				if (CompileEnvironment.bUseRTTI)
				{
					Variant += ".RTTI";
				}
				else
				{
					Variant += ".NonRTTI";
				}
			}
			if (CompileEnvironment.bEnableExceptions != BaseCompileEnvironment.bEnableExceptions)
			{
				if (CompileEnvironment.bEnableExceptions)
				{
					Variant += ".Exceptions";
				}
				else
				{
					Variant += ".NoExceptions";
				}
			}
			if (CompileEnvironment.ShadowVariableWarningLevel != BaseCompileEnvironment.ShadowVariableWarningLevel)
			{
				if (CompileEnvironment.ShadowVariableWarningLevel == WarningLevel.Error)
				{
					Variant += ".ShadowErrors";
				}
				else if (CompileEnvironment.ShadowVariableWarningLevel == WarningLevel.Warning)
				{
					Variant += ".ShadowWarnings";
				}
				else
				{
					Variant += ".NoShadow";
				}
			}

			if (CompileEnvironment.UnsafeTypeCastWarningLevel != BaseCompileEnvironment.UnsafeTypeCastWarningLevel)
			{
				if (CompileEnvironment.UnsafeTypeCastWarningLevel == WarningLevel.Error)
				{
					Variant += ".TypeCastErrors";
				}
				else if (CompileEnvironment.UnsafeTypeCastWarningLevel == WarningLevel.Warning)
				{
					Variant += ".TypeCastWarnings";
				}
				else
				{
					Variant += ".NoTypeCast";
				}
			}
			
			if (CompileEnvironment.bEnableUndefinedIdentifierWarnings != BaseCompileEnvironment.bEnableUndefinedIdentifierWarnings)
			{
				if (CompileEnvironment.bEnableUndefinedIdentifierWarnings)
				{
					Variant += ".Undef";
				}
				else
				{
					Variant += ".NoUndef";
				}
			}

			if (CompileEnvironment.CppStandard != BaseCompileEnvironment.CppStandard)
			{
				Variant += String.Format(".{0}", CompileEnvironment.CppStandard);
			}

			return Variant;
		}

		/// <summary>
		/// Copy settings from the module's compile environment into the environment for the shared PCH
		/// </summary>
		/// <param name="ModuleCompileEnvironment">The module compile environment</param>
		/// <param name="CompileEnvironment">The shared PCH compile environment</param>
		private void CopySettingsForSharedPCH(CppCompileEnvironment ModuleCompileEnvironment, CppCompileEnvironment CompileEnvironment)
		{
			CompileEnvironment.bOptimizeCode = ModuleCompileEnvironment.bOptimizeCode;
			CompileEnvironment.bUseRTTI = ModuleCompileEnvironment.bUseRTTI;
			CompileEnvironment.bEnableExceptions = ModuleCompileEnvironment.bEnableExceptions;
			CompileEnvironment.ShadowVariableWarningLevel = ModuleCompileEnvironment.ShadowVariableWarningLevel;
			CompileEnvironment.UnsafeTypeCastWarningLevel = ModuleCompileEnvironment.UnsafeTypeCastWarningLevel;
			CompileEnvironment.bEnableUndefinedIdentifierWarnings = ModuleCompileEnvironment.bEnableUndefinedIdentifierWarnings;
			CompileEnvironment.CppStandard = ModuleCompileEnvironment.CppStandard;
		}

		/// <summary>
		/// Compiles the provided CPP unity files. Will
		/// </summary>
		private CPPOutput CompileUnityFilesWithToolChain(ReadOnlyTargetRules Target, UEToolChain ToolChain, CppCompileEnvironment CompileEnvironment, CppCompileEnvironment ModuleCompileEnvironment, List<FileItem> SourceFiles, IActionGraphBuilder Graph)
		{
			List<FileItem> NormalFiles = new List<FileItem>();
			List<FileItem> AdaptiveFiles = new List<FileItem>();

			bool bAdaptiveUnityDisablesPCH = false;
			if(Rules.PCHUsage == ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs)
			{
				if(Rules.bTreatAsEngineModule || Rules.PrivatePCHHeaderFile == null)
				{
					bAdaptiveUnityDisablesPCH = Target.bAdaptiveUnityDisablesPCH;
				}
				else
				{
					bAdaptiveUnityDisablesPCH = Target.bAdaptiveUnityDisablesPCHForProject;
				}
			}

			if ((Target.bAdaptiveUnityDisablesOptimizations || bAdaptiveUnityDisablesPCH || Target.bAdaptiveUnityCreatesDedicatedPCH) && !Target.bStressTestUnity)
			{
				foreach (FileItem File in SourceFiles)
				{
					// Basic check as to whether something in this module is/isn't a unity file...
					if (File.Location.GetFileName().StartsWith(Unity.ModulePrefix))
					{
						NormalFiles.Add(File);
					}
					else
					{
						AdaptiveFiles.Add(File);
					}
				}
			}
			else
			{
				NormalFiles.AddRange(SourceFiles);
			}

			CPPOutput OutputFiles = new CPPOutput();

			if (NormalFiles.Count > 0)
			{
				OutputFiles = ToolChain.CompileCPPFiles(CompileEnvironment, NormalFiles, IntermediateDirectory, Name, Graph);
			}

			if (AdaptiveFiles.Count > 0)
			{
				// Create the new compile environment. Always turn off PCH due to different compiler settings.
				CppCompileEnvironment AdaptiveUnityEnvironment = new CppCompileEnvironment(ModuleCompileEnvironment);
				if(Target.bAdaptiveUnityDisablesOptimizations)
				{
					AdaptiveUnityEnvironment.bOptimizeCode = false;
				}
				if (Target.bAdaptiveUnityEnablesEditAndContinue)
				{
					AdaptiveUnityEnvironment.bSupportEditAndContinue = true;
				}

				// Create a per-file PCH
				CPPOutput AdaptiveOutput;
				if(Target.bAdaptiveUnityCreatesDedicatedPCH)
				{
					AdaptiveOutput = CompileAdaptiveNonUnityFilesWithDedicatedPCH(ToolChain, AdaptiveUnityEnvironment, AdaptiveFiles, IntermediateDirectory, Name, Graph);
				}
				else if(bAdaptiveUnityDisablesPCH)
				{
					AdaptiveOutput = CompileAdaptiveNonUnityFilesWithoutPCH(ToolChain, AdaptiveUnityEnvironment, AdaptiveFiles, IntermediateDirectory, Name, Graph);
				}
				else if(AdaptiveUnityEnvironment.bOptimizeCode != CompileEnvironment.bOptimizeCode || AdaptiveUnityEnvironment.bSupportEditAndContinue != CompileEnvironment.bSupportEditAndContinue)
				{
					AdaptiveOutput = CompileAdaptiveNonUnityFiles(ToolChain, AdaptiveUnityEnvironment, AdaptiveFiles, IntermediateDirectory, Name, Graph);
				}
				else
				{
					AdaptiveOutput = CompileAdaptiveNonUnityFiles(ToolChain, CompileEnvironment, AdaptiveFiles, IntermediateDirectory, Name, Graph);
				}

				// Merge output
				OutputFiles.ObjectFiles.AddRange(AdaptiveOutput.ObjectFiles);
				OutputFiles.DebugDataFiles.AddRange(AdaptiveOutput.DebugDataFiles);
			}

			return OutputFiles;
		}

		static CPPOutput CompileAdaptiveNonUnityFiles(UEToolChain ToolChain, CppCompileEnvironment CompileEnvironment, List<FileItem> Files, DirectoryReference IntermediateDirectory, string ModuleName, IActionGraphBuilder Graph)
		{
			// Write all the definitions out to a separate file
			CreateHeaderForDefinitions(CompileEnvironment, IntermediateDirectory, "Adaptive", Graph);

			// Compile the files
			return ToolChain.CompileCPPFiles(CompileEnvironment, Files, IntermediateDirectory, ModuleName, Graph);
		}

		static CPPOutput CompileAdaptiveNonUnityFilesWithoutPCH(UEToolChain ToolChain, CppCompileEnvironment CompileEnvironment, List<FileItem> Files, DirectoryReference IntermediateDirectory, string ModuleName, IActionGraphBuilder Graph)
		{
			// Disable precompiled headers
			CompileEnvironment.PrecompiledHeaderAction = PrecompiledHeaderAction.None;

			// Write all the definitions out to a separate file
			CreateHeaderForDefinitions(CompileEnvironment, IntermediateDirectory, "Adaptive", Graph);

			// Compile the files
			return ToolChain.CompileCPPFiles(CompileEnvironment, Files, IntermediateDirectory, ModuleName, Graph);
		}

		static CPPOutput CompileAdaptiveNonUnityFilesWithDedicatedPCH(UEToolChain ToolChain, CppCompileEnvironment CompileEnvironment, List<FileItem> Files, DirectoryReference IntermediateDirectory, string ModuleName, IActionGraphBuilder Graph)
		{
			CPPOutput Output = new CPPOutput();
			foreach(FileItem File in Files)
			{
				// Build the contents of the wrapper file
				StringBuilder WrapperContents = new StringBuilder();
				using (StringWriter Writer = new StringWriter(WrapperContents))
				{
					Writer.WriteLine("// Dedicated PCH for {0}", File.AbsolutePath);
					Writer.WriteLine();
					WriteDefinitions(CompileEnvironment.Definitions, Writer);
					Writer.WriteLine();
					using(StreamReader Reader = new StreamReader(File.Location.FullName))
					{
						CppIncludeParser.CopyIncludeDirectives(Reader, Writer);
					}
				}

				// Write the PCH header
				FileReference DedicatedPchLocation = FileReference.Combine(IntermediateDirectory, String.Format("PCH.Dedicated.{0}.h", File.Location.GetFileNameWithoutExtension()));
				FileItem DedicatedPchFile = Graph.CreateIntermediateTextFile(DedicatedPchLocation, WrapperContents.ToString());

				// Create a new C++ environment to compile the PCH
				CppCompileEnvironment PchEnvironment = new CppCompileEnvironment(CompileEnvironment);
				PchEnvironment.Definitions.Clear();
				PchEnvironment.UserIncludePaths.Add(File.Location.Directory); // Need to be able to include headers in the same directory as the source file
				PchEnvironment.PrecompiledHeaderAction = PrecompiledHeaderAction.Create;
				PchEnvironment.PrecompiledHeaderIncludeFilename = DedicatedPchFile.Location;

				// Create the action to compile the PCH file.
				CPPOutput PchOutput = ToolChain.CompileCPPFiles(PchEnvironment, new List<FileItem>() { DedicatedPchFile }, IntermediateDirectory, ModuleName, Graph);
				Output.ObjectFiles.AddRange(PchOutput.ObjectFiles);

				// Create a new C++ environment to compile the original file
				CppCompileEnvironment FileEnvironment = new CppCompileEnvironment(CompileEnvironment);
				FileEnvironment.Definitions.Clear();
				FileEnvironment.PrecompiledHeaderAction = PrecompiledHeaderAction.Include;
				FileEnvironment.PrecompiledHeaderIncludeFilename = DedicatedPchFile.Location;
				FileEnvironment.PrecompiledHeaderFile = PchOutput.PrecompiledHeaderFile;

				// Create the action to compile the PCH file.
				CPPOutput FileOutput = ToolChain.CompileCPPFiles(FileEnvironment, new List<FileItem>() { File }, IntermediateDirectory, ModuleName, Graph);
				Output.ObjectFiles.AddRange(FileOutput.ObjectFiles);
			}
			return Output;
		}

		/// <summary>
		/// Configure precompiled headers for this module
		/// </summary>
		/// <param name="Target">The target being built</param>
		/// <param name="ToolChain">The toolchain to build with</param>
		/// <param name="CompileEnvironment">The current compile environment</param>
		/// <param name="LinkInputFiles">List of files that will be linked for the target</param>
		/// <param name="Graph">List of build actions</param>
		CppCompileEnvironment SetupPrecompiledHeaders(ReadOnlyTargetRules Target, UEToolChain ToolChain, CppCompileEnvironment CompileEnvironment, List<FileItem> LinkInputFiles, IActionGraphBuilder Graph)
		{
			if (Target.bUsePCHFiles && Rules.PCHUsage != ModuleRules.PCHUsageMode.NoPCHs)
			{
				// If this module doesn't need a shared PCH, configure that
				if(Rules.PrivatePCHHeaderFile != null && (Rules.PCHUsage == ModuleRules.PCHUsageMode.NoSharedPCHs || Rules.PCHUsage == ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs))
				{
					PrecompiledHeaderInstance Instance = CreatePrivatePCH(ToolChain, FileItem.GetItemByFileReference(FileReference.Combine(ModuleDirectory, Rules.PrivatePCHHeaderFile)), CompileEnvironment, Graph);

					CompileEnvironment = new CppCompileEnvironment(CompileEnvironment);
					CompileEnvironment.Definitions.Clear();
					CompileEnvironment.PrecompiledHeaderAction = PrecompiledHeaderAction.Include;
					CompileEnvironment.PrecompiledHeaderIncludeFilename = Instance.HeaderFile.Location;
					CompileEnvironment.PrecompiledHeaderFile = Instance.Output.PrecompiledHeaderFile;

					LinkInputFiles.AddRange(Instance.Output.ObjectFiles);
				}

				// Try to find a suitable shared PCH for this module
				if (CompileEnvironment.PrecompiledHeaderIncludeFilename == null && CompileEnvironment.SharedPCHs.Count > 0 && !CompileEnvironment.bIsBuildingLibrary && Rules.PCHUsage != ModuleRules.PCHUsageMode.NoSharedPCHs)
				{
					// Find all the dependencies of this module
					HashSet<UEBuildModule> ReferencedModules = new HashSet<UEBuildModule>();
					GetAllDependencyModules(new List<UEBuildModule>(), ReferencedModules, bIncludeDynamicallyLoaded: false, bForceCircular: false, bOnlyDirectDependencies: true);

					// Find the first shared PCH module we can use
					PrecompiledHeaderTemplate Template = CompileEnvironment.SharedPCHs.FirstOrDefault(x => ReferencedModules.Contains(x.Module));
					if(Template != null && Template.IsValidFor(CompileEnvironment))
					{
						PrecompiledHeaderInstance Instance = FindOrCreateSharedPCH(ToolChain, Template, CompileEnvironment, Graph);

						FileReference PrivateDefinitionsFile = FileReference.Combine(IntermediateDirectory, String.Format("Definitions.{0}.h", Name));

						FileItem PrivateDefinitionsFileItem;
						using (StringWriter Writer = new StringWriter())
						{
							// Remove the module _API definition for cases where there are circular dependencies between the shared PCH module and modules using it
							Writer.WriteLine("#undef {0}", ModuleApiDefine);

							// Games may choose to use shared PCHs from the engine, so allow them to change the value of these macros
							if(!Rules.bTreatAsEngineModule)
							{
								Writer.WriteLine("#undef UE_IS_ENGINE_MODULE");
								Writer.WriteLine("#undef DEPRECATED_FORGAME");
								Writer.WriteLine("#define DEPRECATED_FORGAME DEPRECATED");
								Writer.WriteLine("#undef UE_DEPRECATED_FORGAME");
								Writer.WriteLine("#define UE_DEPRECATED_FORGAME UE_DEPRECATED");
							}

							WriteDefinitions(CompileEnvironment.Definitions, Writer);
							PrivateDefinitionsFileItem = Graph.CreateIntermediateTextFile(PrivateDefinitionsFile, Writer.ToString());
						}

						CompileEnvironment = new CppCompileEnvironment(CompileEnvironment);
						CompileEnvironment.Definitions.Clear();
						CompileEnvironment.ForceIncludeFiles.Add(PrivateDefinitionsFileItem);
						CompileEnvironment.PrecompiledHeaderAction = PrecompiledHeaderAction.Include;
						CompileEnvironment.PrecompiledHeaderIncludeFilename = Instance.HeaderFile.Location;
						CompileEnvironment.PrecompiledHeaderFile = Instance.Output.PrecompiledHeaderFile;

						LinkInputFiles.AddRange(Instance.Output.ObjectFiles);
					}
				}
			}
			return CompileEnvironment;
		}

		/// <summary>
		/// Creates a header file containing all the preprocessor definitions for a compile environment, and force-include it. We allow a more flexible syntax for preprocessor definitions than
		/// is typically allowed on the command line (allowing function macros or double-quote characters, for example). Ensuring all definitions are specified in a header files ensures consistent
		/// behavior.
		/// </summary>
		/// <param name="CompileEnvironment">The compile environment</param>
		/// <param name="IntermediateDirectory">Directory to create the intermediate file</param>
		/// <param name="HeaderSuffix">Suffix for the included file</param>
		/// <param name="Graph">The action graph being built</param>
		static void CreateHeaderForDefinitions(CppCompileEnvironment CompileEnvironment, DirectoryReference IntermediateDirectory, string HeaderSuffix, IActionGraphBuilder Graph)
		{
			if(CompileEnvironment.Definitions.Count > 0)
			{
				StringBuilder PrivateDefinitionsName = new StringBuilder("Definitions");
				if(!String.IsNullOrEmpty(HeaderSuffix))
				{
					PrivateDefinitionsName.Append('.');
					PrivateDefinitionsName.Append(HeaderSuffix);
				}
				PrivateDefinitionsName.Append(".h");

				FileReference PrivateDefinitionsFile = FileReference.Combine(IntermediateDirectory, PrivateDefinitionsName.ToString());
				using (StringWriter Writer = new StringWriter())
				{
					WriteDefinitions(CompileEnvironment.Definitions, Writer);
					CompileEnvironment.Definitions.Clear();

					FileItem PrivateDefinitionsFileItem = Graph.CreateIntermediateTextFile(PrivateDefinitionsFile, Writer.ToString());
					CompileEnvironment.ForceIncludeFiles.Add(PrivateDefinitionsFileItem);
				}
			}
		}

		/// <summary>
		/// Creates header files from ISPC for inclusion and adds them as dependencies.
		/// </summary>
		/// <param name="ToolChain">The toolchain to generate the PCH</param>
		/// <param name="CompileEnvironment">Compile environment</param>
		/// <param name="InputFiles">List of ISPC source files</param>
		/// <param name="IntermediateDirectory">Directory to create the intermediate file</param>
		/// <param name="Graph">List of actions to be executed. Additional actions will be added to this list.</param>
		static void CreateHeadersForISPC(UEToolChain ToolChain, CppCompileEnvironment CompileEnvironment, List<FileItem> InputFiles, DirectoryReference IntermediateDirectory, IActionGraphBuilder Graph)
		{
			CPPOutput Output = ToolChain.GenerateISPCHeaders(CompileEnvironment, InputFiles, IntermediateDirectory, Graph);

			CompileEnvironment.AdditionalPrerequisites.AddRange(Output.GeneratedHeaderFiles);
			CompileEnvironment.UserIncludePaths.Add(IntermediateDirectory);
		}

		/// <summary>
		/// Create a header file containing the module definitions, which also includes the PCH itself. Including through another file is necessary on 
		/// Clang, since we get warnings about #pragma once otherwise, but it also allows us to consistently define the preprocessor state on all 
		/// platforms.
		/// </summary>
		/// <param name="OutputFile">The output file to create</param>
		/// <param name="Definitions">Definitions required by the PCH</param>
		/// <param name="IncludedFile">The PCH file to include</param>
		/// <param name="Graph">The action graph builder</param>
		/// <returns>FileItem for the created file</returns>
		static FileItem CreatePCHWrapperFile(FileReference OutputFile, IEnumerable<string> Definitions, FileItem IncludedFile, IActionGraphBuilder Graph)
		{
			// Build the contents of the wrapper file
			StringBuilder WrapperContents = new StringBuilder();
			using (StringWriter Writer = new StringWriter(WrapperContents))
			{
				Writer.WriteLine("// PCH for {0}", IncludedFile.AbsolutePath);
				WriteDefinitions(Definitions, Writer);
				Writer.WriteLine("#include \"{0}\"", IncludedFile.AbsolutePath.Replace('\\', '/'));
			}

			// Create the item
			FileItem WrapperFile = Graph.CreateIntermediateTextFile(OutputFile, WrapperContents.ToString());

			// Touch it if the included file is newer, to make sure our timestamp dependency checking is accurate.
			if (IncludedFile.LastWriteTimeUtc > WrapperFile.LastWriteTimeUtc)
			{
				File.SetLastWriteTimeUtc(WrapperFile.AbsolutePath, DateTime.UtcNow);
				WrapperFile.ResetCachedInfo();
			}
			return WrapperFile;
		}

		/// <summary>
		/// Write a list of macro definitions to an output file
		/// </summary>
		/// <param name="Definitions">List of definitions</param>
		/// <param name="Writer">Writer to receive output</param>
		static void WriteDefinitions(IEnumerable<string> Definitions, TextWriter Writer)
		{
			foreach(string Definition in Definitions)
			{
				int EqualsIdx = Definition.IndexOf('=');
				if(EqualsIdx == -1)
				{
					Writer.WriteLine("#define {0} 1", Definition);
				}
				else
				{
					Writer.WriteLine("#define {0} {1}", Definition.Substring(0, EqualsIdx), Definition.Substring(EqualsIdx + 1));
				}
			}
		}

		/// <summary>
		/// Checks that the first header included by the source files in this module all include the same header
		/// </summary>
		/// <param name="Target">The target being compiled</param>
		/// <param name="ModuleCompileEnvironment">Compile environment for the module</param>
		/// <param name="HeaderFiles">All header files for this module</param>
		/// <param name="CppFiles">List of C++ source files</param>
		private void CheckFirstIncludeMatchesEachCppFile(ReadOnlyTargetRules Target, CppCompileEnvironment ModuleCompileEnvironment, List<FileItem> HeaderFiles, List<FileItem> CppFiles)
		{
			if(Rules.PCHUsage == ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs)
			{
				if(InvalidIncludeDirectiveMessages == null)
				{
					// Find headers used by the source file.
					Dictionary<string, FileReference> NameToHeaderFile = new Dictionary<string, FileReference>();
					foreach(FileItem HeaderFile in HeaderFiles)
					{
						NameToHeaderFile[HeaderFile.Location.GetFileNameWithoutExtension()] = HeaderFile.Location;
					}

					// Find the directly included files for each source file, and make sure it includes the matching header if possible
					InvalidIncludeDirectiveMessages = new List<string>();
					if (Rules != null && Rules.bEnforceIWYU && Target.bEnforceIWYU)
					{
						foreach (FileItem CppFile in CppFiles)
						{
							string FirstInclude = ModuleCompileEnvironment.MetadataCache.GetFirstInclude(CppFile);
							if(FirstInclude != null)
							{
								string IncludeName = Path.GetFileNameWithoutExtension(FirstInclude);
								string ExpectedName = CppFile.Location.GetFileNameWithoutExtension();
								if (String.Compare(IncludeName, ExpectedName, StringComparison.OrdinalIgnoreCase) != 0)
								{
									FileReference HeaderFile;
									if (NameToHeaderFile.TryGetValue(ExpectedName, out HeaderFile) && !IgnoreMismatchedHeader(ExpectedName))
									{
										InvalidIncludeDirectiveMessages.Add(String.Format("{0}(1): error: Expected {1} to be first header included.", CppFile.Location, HeaderFile.GetFileName()));
									}
								}
							}
						}
					}
				}
			}
		}

		private bool IgnoreMismatchedHeader(string ExpectedName)
		{
			switch(ExpectedName)
			{
				case "DynamicRHI":
				case "RHICommandList":
				case "RHIUtilities":
					return true;
			}
			switch(Name)
			{
				case "D3D11RHI":
				case "D3D12RHI":
				case "VulkanRHI":
				case "OpenGLDrv":
				case "MetalRHI":
					return true;
			}
			return false;
		}

		/// <summary>
		/// Determine whether optimization should be enabled for a given target
		/// </summary>
		/// <param name="Setting">The optimization setting from the rules file</param>
		/// <param name="Configuration">The active target configuration</param>
		/// <param name="bIsEngineModule">Whether the current module is an engine module</param>
		/// <returns>True if optimization should be enabled</returns>
		public static bool ShouldEnableOptimization(ModuleRules.CodeOptimization Setting, UnrealTargetConfiguration Configuration, bool bIsEngineModule)
		{
			switch(Setting)
			{
				case ModuleRules.CodeOptimization.Never:
					return false;
				case ModuleRules.CodeOptimization.Default:
				case ModuleRules.CodeOptimization.InNonDebugBuilds:
					return (Configuration == UnrealTargetConfiguration.Debug)? false : (Configuration != UnrealTargetConfiguration.DebugGame || bIsEngineModule);
				case ModuleRules.CodeOptimization.InShippingBuildsOnly:
					return (Configuration == UnrealTargetConfiguration.Shipping);
				default:
					return true;
			}
		}

		public CppCompileEnvironment CreateCompileEnvironmentForIntellisense(ReadOnlyTargetRules Target, CppCompileEnvironment BaseCompileEnvironment)
		{
			CppCompileEnvironment CompileEnvironment = CreateModuleCompileEnvironment(Target, BaseCompileEnvironment);
			CompileEnvironment = SetupPrecompiledHeaders(Target, null, CompileEnvironment, new List<FileItem>(), new NullActionGraphBuilder());
			CreateHeaderForDefinitions(CompileEnvironment, IntermediateDirectory, null, new NullActionGraphBuilder());
			return CompileEnvironment;
		}

		/// <summary>
		/// Creates a compile environment from a base environment based on the module settings.
		/// </summary>
		/// <param name="Target">Rules for the target being built</param>
		/// <param name="BaseCompileEnvironment">An existing environment to base the module compile environment on.</param>
		/// <returns>The new module compile environment.</returns>
		public CppCompileEnvironment CreateModuleCompileEnvironment(ReadOnlyTargetRules Target, CppCompileEnvironment BaseCompileEnvironment)
		{
			CppCompileEnvironment Result = new CppCompileEnvironment(BaseCompileEnvironment);

			// Override compile environment
			Result.bUseUnity = Rules.bUseUnity;
			Result.bOptimizeCode = ShouldEnableOptimization(Rules.OptimizeCode, Target.Configuration, Rules.bTreatAsEngineModule);
			Result.bUseRTTI |= Rules.bUseRTTI;
			Result.bUseAVX = Rules.bUseAVX;
			Result.bEnableBufferSecurityChecks = Rules.bEnableBufferSecurityChecks;
			Result.MinSourceFilesForUnityBuildOverride = Rules.MinSourceFilesForUnityBuildOverride;
			Result.MinFilesUsingPrecompiledHeaderOverride = Rules.MinFilesUsingPrecompiledHeaderOverride;
			Result.bBuildLocallyWithSNDBS = Rules.bBuildLocallyWithSNDBS;
			Result.bEnableExceptions |= Rules.bEnableExceptions;
			Result.bEnableObjCExceptions |= Rules.bEnableObjCExceptions;
			Result.ShadowVariableWarningLevel = Rules.ShadowVariableWarningLevel;
			Result.UnsafeTypeCastWarningLevel = Rules.UnsafeTypeCastWarningLevel;
			Result.bEnableUndefinedIdentifierWarnings = Rules.bEnableUndefinedIdentifierWarnings;

			// If the module overrides the C++ language version, override it on the compile environment
			if(Rules.CppStandard != CppStandardVersion.Default)
			{
				Result.CppStandard = Rules.CppStandard;
			}

			// Set the macro used to check whether monolithic headers can be used
			if (Rules.bTreatAsEngineModule && (!Rules.bEnforceIWYU || !Target.bEnforceIWYU))
			{
				Result.Definitions.Add("SUPPRESS_MONOLITHIC_HEADER_WARNINGS=1");
			}

			// Add a macro for when we're compiling an engine module, to enable additional compiler diagnostics through code.
			if (Rules.bTreatAsEngineModule)
			{
				Result.Definitions.Add("UE_IS_ENGINE_MODULE=1");
			}
			else
			{
				Result.Definitions.Add("UE_IS_ENGINE_MODULE=0");
			}

			// For game modules, set the define for the project and target names, which will be used by the IMPLEMENT_PRIMARY_GAME_MODULE macro.
			if (!Rules.bTreatAsEngineModule)
			{
				// Make sure we don't set any define for a non-engine module that's under the engine directory (eg. UE4Game)
				if (Target.ProjectFile != null && RulesFile.IsUnderDirectory(Target.ProjectFile.Directory))
				{
					string ProjectName = Target.ProjectFile.GetFileNameWithoutExtension();
					Result.Definitions.Add(String.Format("UE_PROJECT_NAME={0}", ProjectName));
					Result.Definitions.Add(String.Format("UE_TARGET_NAME={0}", Target.Name));
				}
			}

			// Add the module's public and private definitions.
			Result.Definitions.AddRange(PublicDefinitions);
			Result.Definitions.AddRange(Rules.PrivateDefinitions);

			// Add the project definitions
			if(!Rules.bTreatAsEngineModule)
			{
				Result.Definitions.AddRange(Rules.Target.ProjectDefinitions);
			}

			// Setup the compile environment for the module.
			SetupPrivateCompileEnvironment(Result.UserIncludePaths, Result.SystemIncludePaths, Result.Definitions, Result.AdditionalFrameworks, Result.AdditionalPrerequisites, Rules.bLegacyPublicIncludePaths);

			return Result;
		}

		/// <summary>
		/// Creates a compile environment for a shared PCH from a base environment based on the module settings.
		/// </summary>
		/// <param name="Target">The target being built</param>
		/// <param name="BaseCompileEnvironment">An existing environment to base the module compile environment on.</param>
		/// <returns>The new shared PCH compile environment.</returns>
		public CppCompileEnvironment CreateSharedPCHCompileEnvironment(UEBuildTarget Target, CppCompileEnvironment BaseCompileEnvironment)
		{
			CppCompileEnvironment CompileEnvironment = new CppCompileEnvironment(BaseCompileEnvironment);

			// Use the default optimization setting for 
			CompileEnvironment.bOptimizeCode = ShouldEnableOptimization(ModuleRules.CodeOptimization.Default, Target.Configuration, Rules.bTreatAsEngineModule);

			// Override compile environment
			CompileEnvironment.bIsBuildingDLL = !Target.ShouldCompileMonolithic();
			CompileEnvironment.bIsBuildingLibrary = false;

			// Add a macro for when we're compiling an engine module, to enable additional compiler diagnostics through code.
			if (Rules.bTreatAsEngineModule)
			{
				CompileEnvironment.Definitions.Add("UE_IS_ENGINE_MODULE=1");
			}
			else
			{
				CompileEnvironment.Definitions.Add("UE_IS_ENGINE_MODULE=0");
			}

			// Add the module's private definitions.
			CompileEnvironment.Definitions.AddRange(PublicDefinitions);

			// Find all the modules that are part of the public compile environment for this module.
			Dictionary<UEBuildModule, bool> ModuleToIncludePathsOnlyFlag = new Dictionary<UEBuildModule, bool>();
			FindModulesInPublicCompileEnvironment(ModuleToIncludePathsOnlyFlag);

			// Now set up the compile environment for the modules in the original order that we encountered them
			foreach (UEBuildModule Module in ModuleToIncludePathsOnlyFlag.Keys)
			{
				Module.AddModuleToCompileEnvironment(null, CompileEnvironment.UserIncludePaths, CompileEnvironment.SystemIncludePaths, CompileEnvironment.Definitions, CompileEnvironment.AdditionalFrameworks, CompileEnvironment.AdditionalPrerequisites, Rules.bLegacyPublicIncludePaths);
			}
			return CompileEnvironment;
		}

		public override void GetAllDependencyModules(List<UEBuildModule> ReferencedModules, HashSet<UEBuildModule> IgnoreReferencedModules, bool bIncludeDynamicallyLoaded, bool bForceCircular, bool bOnlyDirectDependencies)
		{
			List<UEBuildModule> AllDependencyModules = new List<UEBuildModule>();
			AllDependencyModules.AddRange(PrivateDependencyModules);
			AllDependencyModules.AddRange(PublicDependencyModules);
			if (bIncludeDynamicallyLoaded)
			{
				AllDependencyModules.AddRange(DynamicallyLoadedModules);
			}

			foreach (UEBuildModule DependencyModule in AllDependencyModules)
			{
				if (!IgnoreReferencedModules.Contains(DependencyModule))
				{
					// Don't follow circular back-references!
					bool bIsCircular = HasCircularDependencyOn(DependencyModule.Name);
					if (bForceCircular || !bIsCircular)
					{
						IgnoreReferencedModules.Add(DependencyModule);

						if (!bOnlyDirectDependencies)
						{
							// Recurse into dependent modules first
							DependencyModule.GetAllDependencyModules(ReferencedModules, IgnoreReferencedModules, bIncludeDynamicallyLoaded, bForceCircular, bOnlyDirectDependencies);
						}

						ReferencedModules.Add(DependencyModule);
					}
				}
			}
		}

		/// <summary>
		/// Finds all the source files that should be built for this module
		/// </summary>
		/// <param name="Platform">The platform the module is being built for</param>
		/// <param name="DirectoryToSourceFiles">Map of directory to source files inside it</param>
		/// <returns>Set of source files that should be built</returns>
		public InputFileCollection FindInputFiles(UnrealTargetPlatform Platform, Dictionary<DirectoryItem, FileItem[]> DirectoryToSourceFiles)
		{
			ReadOnlyHashSet<string> ExcludedNames = UEBuildPlatform.GetBuildPlatform(Platform).GetExcludedFolderNames();

			InputFileCollection InputFiles = new InputFileCollection();

			SourceDirectories = new HashSet<DirectoryReference>();
			foreach (DirectoryReference Dir in ModuleDirectories)
			{
				DirectoryItem ModuleDirectoryItem = DirectoryItem.GetItemByDirectoryReference(Dir);
				FindInputFilesFromDirectoryRecursive(ModuleDirectoryItem, ExcludedNames, SourceDirectories, DirectoryToSourceFiles, InputFiles);
			}

			return InputFiles;
		}

		/// <summary>
		/// Finds all the source files that should be built for this module
		/// </summary>
		/// <param name="BaseDirectory">Directory to search from</param>
		/// <param name="ExcludedNames">Set of excluded directory names (eg. other platforms)</param>
		/// <param name="SourceDirectories">Set of all non-empty source directories.</param>
		/// <param name="DirectoryToSourceFiles">Map from directory to source files inside it</param>
		/// <param name="InputFiles">Collection of source files, categorized by type</param>
		static void FindInputFilesFromDirectoryRecursive(DirectoryItem BaseDirectory, ReadOnlyHashSet<string> ExcludedNames, HashSet<DirectoryReference> SourceDirectories, Dictionary<DirectoryItem, FileItem[]> DirectoryToSourceFiles, InputFileCollection InputFiles)
		{
			foreach(DirectoryItem SubDirectory in BaseDirectory.EnumerateDirectories())
			{
				if(!ExcludedNames.Contains(SubDirectory.Name))
				{
					FindInputFilesFromDirectoryRecursive(SubDirectory, ExcludedNames, SourceDirectories, DirectoryToSourceFiles, InputFiles);
				}
			}

			FileItem[] SourceFiles = FindInputFilesFromDirectory(BaseDirectory, InputFiles);
			if(SourceFiles.Length > 0)
			{
				SourceDirectories.Add(BaseDirectory.Location);
			}
			DirectoryToSourceFiles.Add(BaseDirectory, SourceFiles);
		}

		/// <summary>
		/// Finds the input files that should be built for this module, from a given directory
		/// </summary>
		/// <param name="BaseDirectory"></param>
		/// <param name="InputFiles"></param>
		/// <returns>Array of source files</returns>
		static FileItem[] FindInputFilesFromDirectory(DirectoryItem BaseDirectory, InputFileCollection InputFiles)
		{
			List<FileItem> SourceFiles = new List<FileItem>();
			foreach(FileItem InputFile in BaseDirectory.EnumerateFiles())
			{
				if (InputFile.HasExtension(".h"))
				{
					InputFiles.HeaderFiles.Add(InputFile);
				}
				else if (InputFile.HasExtension(".isph"))
				{
					InputFiles.ISPCHeaderFiles.Add(InputFile);
				}
				if (InputFile.HasExtension(".cpp"))
				{
					SourceFiles.Add(InputFile);
					InputFiles.CPPFiles.Add(InputFile);
				}
				else if (InputFile.HasExtension(".c"))
				{
					SourceFiles.Add(InputFile);
					InputFiles.CFiles.Add(InputFile);
				}
				else if (InputFile.HasExtension(".cc"))
				{
					SourceFiles.Add(InputFile);
					InputFiles.CCFiles.Add(InputFile);
				}
				else if (InputFile.HasExtension(".m") || InputFile.HasExtension(".mm"))
				{
					SourceFiles.Add(InputFile);
					InputFiles.MMFiles.Add(InputFile);
				}
				else if (InputFile.HasExtension(".rc"))
				{
					SourceFiles.Add(InputFile);
					InputFiles.RCFiles.Add(InputFile);
				}
				else if (InputFile.HasExtension(".ispc"))
				{
					SourceFiles.Add(InputFile);
					InputFiles.ISPCFiles.Add(InputFile);
				}
			}
			return SourceFiles.ToArray();
		}

		/// <summary>
		/// Gets a set of source files for the given directory. Used to detect when the makefile is out of date.
		/// </summary>
		/// <param name="Directory"></param>
		/// <returns>Array of source files</returns>
		public static FileItem[] GetSourceFiles(DirectoryItem Directory)
		{
			return FindInputFilesFromDirectory(Directory, new InputFileCollection());
		}
	}
}
