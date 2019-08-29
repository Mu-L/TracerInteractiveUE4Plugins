// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Xml;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// A unit of code compilation and linking.
	/// </summary>
	abstract class UEBuildModule
	{
		/// <summary>
		/// The name that uniquely identifies the module.
		/// </summary>
		public readonly string Name;

		/// <summary>
		/// The type of module being built. Used to switch between debug/development and precompiled/source configurations.
		/// </summary>
		public UHTModuleType Type;

		/// <summary>
		/// The rules for this module
		/// </summary>
		public ModuleRules Rules;

		/// <summary>
		/// Path to the module directory
		/// </summary>
		public readonly DirectoryReference ModuleDirectory;

		/// <summary>
		/// Is this module allowed to be redistributed.
		/// </summary>
		private readonly bool? IsRedistributableOverride;

		/// <summary>
		/// The name of the .Build.cs file this module was created from, if any
		/// </summary>
		public FileReference RulesFile;

		/// <summary>
		/// The binary the module will be linked into for the current target.  Only set after UEBuildBinary.BindModules is called.
		/// </summary>
		public UEBuildBinary Binary = null;

		/// <summary>
		/// The name of the _API define for this module
		/// </summary>
		protected readonly string ModuleApiDefine;

		/// <summary>
		/// Set of all the public definitions
		/// </summary>
		protected readonly HashSet<string> PublicDefinitions;

		/// <summary>
		/// Set of all public include paths
		/// </summary>
		protected readonly HashSet<DirectoryReference> PublicIncludePaths;

		/// <summary>
		/// Nested public include paths which used to be added automatically, but are now only added for modules with bNestedPublicIncludePaths set.
		/// </summary>
		protected readonly HashSet<DirectoryReference> LegacyPublicIncludePaths = new HashSet<DirectoryReference>();

		/// <summary>
		/// Set of all private include paths
		/// </summary>
		protected readonly HashSet<DirectoryReference> PrivateIncludePaths;

		/// <summary>
		/// Set of all system include paths
		/// </summary>
		protected readonly HashSet<DirectoryReference> PublicSystemIncludePaths;

		/// <summary>
		/// Set of all public library paths
		/// </summary>
		protected readonly HashSet<DirectoryReference> PublicLibraryPaths;

		/// <summary>
		/// Set of all additional libraries
		/// </summary>
		protected readonly HashSet<string> PublicAdditionalLibraries;

		/// <summary>
		/// Set of additional frameworks
		/// </summary>
		protected readonly HashSet<string> PublicFrameworks;

		/// <summary>
		/// 
		/// </summary>
		protected readonly HashSet<string> PublicWeakFrameworks;

		/// <summary>
		/// 
		/// </summary>
		protected readonly HashSet<UEBuildFramework> PublicAdditionalFrameworks;

		/// <summary>
		/// 
		/// </summary>
		protected readonly HashSet<string> PublicAdditionalShadowFiles;

		/// <summary>
		/// 
		/// </summary>
		protected readonly HashSet<UEBuildBundleResource> PublicAdditionalBundleResources;

		/// <summary>
		/// Names of modules with header files that this module's public interface needs access to.
		/// </summary>
		protected List<UEBuildModule> PublicIncludePathModules;

		/// <summary>
		/// Names of modules that this module's public interface depends on.
		/// </summary>
		protected List<UEBuildModule> PublicDependencyModules;

		/// <summary>
		/// Names of DLLs that this module should delay load
		/// </summary>
		protected HashSet<string> PublicDelayLoadDLLs;

		/// <summary>
		/// Names of modules with header files that this module's private implementation needs access to.
		/// </summary>
		protected List<UEBuildModule> PrivateIncludePathModules;

		/// <summary>
		/// Names of modules that this module's private implementation depends on.
		/// </summary>
		protected List<UEBuildModule> PrivateDependencyModules;

		/// <summary>
		/// Extra modules this module may require at run time
		/// </summary>
		protected List<UEBuildModule> DynamicallyLoadedModules;

		/// <summary>
		/// Files which this module depends on at runtime.
		/// </summary>
		public List<RuntimeDependency> RuntimeDependencies;

		/// <summary>
		/// Set of all whitelisted restricted folder references
		/// </summary>
		private readonly HashSet<DirectoryReference> WhitelistRestrictedFolders;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="InName">Name of the module</param>
		/// <param name="InType">Type of the module, for UHT</param>
		/// <param name="InModuleDirectory">Base directory for the module</param>
		/// <param name="InRules">Rules for this module</param>
		/// <param name="InRulesFile">Path to the rules file</param>
		/// <param name="InRuntimeDependencies">List of runtime dependencies</param>
		public UEBuildModule(string InName, UHTModuleType InType, DirectoryReference InModuleDirectory, ModuleRules InRules, FileReference InRulesFile, List<RuntimeDependency> InRuntimeDependencies)
		{
			Name = InName;
			Type = InType;
			ModuleDirectory = InModuleDirectory;
			Rules = InRules;
			RulesFile = InRulesFile;

			ModuleApiDefine = Name.ToUpperInvariant() + "_API";

			PublicDefinitions = HashSetFromOptionalEnumerableStringParameter(InRules.PublicDefinitions);
			PublicIncludePaths = CreateDirectoryHashSet(InRules.PublicIncludePaths);
			PublicSystemIncludePaths = CreateDirectoryHashSet(InRules.PublicSystemIncludePaths);
			PublicLibraryPaths = CreateDirectoryHashSet(InRules.PublicLibraryPaths);
			PublicAdditionalLibraries = HashSetFromOptionalEnumerableStringParameter(InRules.PublicAdditionalLibraries);
			PublicFrameworks = HashSetFromOptionalEnumerableStringParameter(InRules.PublicFrameworks);
			PublicWeakFrameworks = HashSetFromOptionalEnumerableStringParameter(InRules.PublicWeakFrameworks);
			PublicAdditionalFrameworks = InRules.PublicAdditionalFrameworks == null ? new HashSet<UEBuildFramework>() : new HashSet<UEBuildFramework>(InRules.PublicAdditionalFrameworks);
			PublicAdditionalShadowFiles = HashSetFromOptionalEnumerableStringParameter(InRules.PublicAdditionalShadowFiles);
			PublicAdditionalBundleResources = InRules.AdditionalBundleResources == null ? new HashSet<UEBuildBundleResource>() : new HashSet<UEBuildBundleResource>(InRules.AdditionalBundleResources);
			PublicDelayLoadDLLs = HashSetFromOptionalEnumerableStringParameter(InRules.PublicDelayLoadDLLs);
			if(Rules.bUsePrecompiled)
			{
				PrivateIncludePaths = new HashSet<DirectoryReference>();
			}
			else
			{
				PrivateIncludePaths = CreateDirectoryHashSet(InRules.PrivateIncludePaths);
			}
			RuntimeDependencies = InRuntimeDependencies;
			IsRedistributableOverride = InRules.IsRedistributableOverride;

			WhitelistRestrictedFolders = new HashSet<DirectoryReference>(InRules.WhitelistRestrictedFolders.Select(x => DirectoryReference.Combine(ModuleDirectory, x)));
		}

		/// <summary>
		/// Returns a list of this module's dependencies.
		/// </summary>
		/// <returns>An enumerable containing the dependencies of the module.</returns>
		public HashSet<UEBuildModule> GetDependencies(bool bWithIncludePathModules, bool bWithDynamicallyLoadedModules)
		{
			HashSet<UEBuildModule> Modules = new HashSet<UEBuildModule>();
			Modules.UnionWith(PublicDependencyModules);
			Modules.UnionWith(PrivateDependencyModules);
			if(bWithIncludePathModules)
			{
				Modules.UnionWith(PublicIncludePathModules);
				Modules.UnionWith(PrivateIncludePathModules);
			}
			if(bWithDynamicallyLoadedModules)
			{
				Modules.UnionWith(DynamicallyLoadedModules);
			}
			return Modules;
        }

  		/// <summary>
		/// Returns a list of this module's frameworks.
		/// </summary>
		/// <returns>A List containing the frameworks this module requires.</returns>
        public List<string> GetPublicFrameworks()
        {
            return new List<string>(PublicFrameworks);
        }

		/// <summary>
		/// Returns a list of this module's immediate dependencies.
		/// </summary>
		/// <returns>An enumerable containing the dependencies of the module.</returns>
		public IEnumerable<UEBuildModule> GetDirectDependencyModules()
		{
			return PublicDependencyModules.Concat(PrivateDependencyModules).Concat(DynamicallyLoadedModules);
		}

		/// <summary>
		/// Converts an optional string list parameter to a well-defined hash set.
		/// </summary>
		protected HashSet<DirectoryReference> CreateDirectoryHashSet(IEnumerable<string> InEnumerableStrings)
		{
			HashSet<DirectoryReference> Directories = new HashSet<DirectoryReference>();
			if(InEnumerableStrings != null)
			{
				foreach(string InputString in InEnumerableStrings)
				{
					string ExpandedString = Utils.ExpandVariables(InputString);
					if(ExpandedString.Contains("$("))
					{
						throw new BuildException("Unable to expand variable in '{0}'", InputString);
					}

					DirectoryReference Dir = new DirectoryReference(ExpandedString);
					if(DirectoryReference.Exists(Dir))
					{
						Directories.Add(Dir);
					}
					else
					{
						Log.WriteLineOnce(LogEventType.Warning, LogFormatOptions.NoSeverityPrefix, "{0}: warning: Referenced directory '{1}' does not exist.", RulesFile, Dir);
					}
				}
			}
			return Directories;
		}

		/// <summary>
		/// Converts an optional string list parameter to a well-defined hash set.
		/// </summary>
		protected static HashSet<string> HashSetFromOptionalEnumerableStringParameter(IEnumerable<string> InEnumerableStrings)
		{
			return InEnumerableStrings == null ? new HashSet<string>() : new HashSet<string>(InEnumerableStrings);
		}

		/// <summary>
		/// Determines whether this module has a circular dependency on the given module
		/// </summary>
		public bool HasCircularDependencyOn(string ModuleName)
		{
			return Rules.CircularlyReferencedDependentModules.Contains(ModuleName);
		}

		/// <summary>
		/// Enumerates additional build products which may be produced by this module. Some platforms (eg. Mac, Linux) can link directly against .so/.dylibs, but they 
		/// are also copied to the output folder by the toolchain.
		/// </summary>
		/// <param name="Libraries">List to which libraries required by this module are added</param>
		/// <param name="BundleResources">List of bundle resources required by this module</param>
		public void GatherAdditionalResources(List<string> Libraries, List<UEBuildBundleResource> BundleResources)
		{
			Libraries.AddRange(PublicAdditionalLibraries);
			BundleResources.AddRange(PublicAdditionalBundleResources);
		}

		/// <summary>
		/// Determines the distribution level of a module based on its directory and includes.
		/// </summary>
		/// <param name="ProjectDir">The project directory, if available</param>
		/// <returns>Map of the restricted folder types to the first found instance</returns>
		public Dictionary<RestrictedFolder, DirectoryReference> FindRestrictedFolderReferences(DirectoryReference ProjectDir)
		{
			Dictionary<RestrictedFolder, DirectoryReference> References = new Dictionary<RestrictedFolder, DirectoryReference>();
			if (!Rules.bOutputPubliclyDistributable)
			{
				// Find all the directories that this module references
				HashSet<DirectoryReference> ReferencedDirs = new HashSet<DirectoryReference>();
				GetReferencedDirectories(ReferencedDirs);

				// Remove all the whitelisted folders
				ReferencedDirs.ExceptWith(WhitelistRestrictedFolders);
				ReferencedDirs.ExceptWith(PublicDependencyModules.SelectMany(x => x.WhitelistRestrictedFolders));
				ReferencedDirs.ExceptWith(PrivateDependencyModules.SelectMany(x => x.WhitelistRestrictedFolders));

				// Add flags for each of them
				foreach(DirectoryReference ReferencedDir in ReferencedDirs)
				{
					// Find the base directory containing this reference
					DirectoryReference BaseDir;
					if(ReferencedDir.IsUnderDirectory(UnrealBuildTool.EngineDirectory))
					{
						BaseDir = UnrealBuildTool.EngineDirectory;
					}
					else if(ProjectDir != null && ReferencedDir.IsUnderDirectory(ProjectDir))
					{
						BaseDir = ProjectDir;
					}
					else
					{
						continue;
					}

					// Add references to each of the restricted folders
					List<RestrictedFolder> Folders = RestrictedFolders.FindRestrictedFolders(BaseDir, ReferencedDir);
					foreach(RestrictedFolder Folder in Folders)
					{
						if(!References.ContainsKey(Folder))
						{
							References.Add(Folder, ReferencedDir);
						}
					}
				}
			}
			return References;
		}

		/// <summary>
		/// Finds all the directories that this folder references when building
		/// </summary>
		/// <param name="Directories">Set of directories to add to</param>
		protected virtual void GetReferencedDirectories(HashSet<DirectoryReference> Directories)
		{
			Directories.Add(ModuleDirectory);

			foreach(DirectoryReference PublicIncludePath in PublicIncludePaths)
			{
				Directories.Add(PublicIncludePath);
			}
			foreach(DirectoryReference PrivateIncludePath in PrivateIncludePaths)
			{
				Directories.Add(PrivateIncludePath);
			}
			foreach(DirectoryReference PublicSystemIncludePath in PublicSystemIncludePaths)
			{
				Directories.Add(PublicSystemIncludePath);
			}
			foreach(DirectoryReference PublicLibraryPath in PublicLibraryPaths)
			{
				Directories.Add(PublicLibraryPath);
			}
		}

		/// <summary>
		/// Find all the modules which affect the private compile environment.
		/// </summary>
		/// <param name="ModuleToIncludePathsOnlyFlag"></param>
		protected void FindModulesInPrivateCompileEnvironment(Dictionary<UEBuildModule, bool> ModuleToIncludePathsOnlyFlag)
		{
			// Add in all the modules that are only in the private compile environment
			foreach (UEBuildModule PrivateDependencyModule in PrivateDependencyModules)
			{
				PrivateDependencyModule.FindModulesInPublicCompileEnvironment(ModuleToIncludePathsOnlyFlag);
			}
			foreach (UEBuildModule PrivateIncludePathModule in PrivateIncludePathModules)
			{
				PrivateIncludePathModule.FindIncludePathModulesInPublicCompileEnvironment(ModuleToIncludePathsOnlyFlag);
			}

			// Add the modules in the public compile environment
			FindModulesInPublicCompileEnvironment(ModuleToIncludePathsOnlyFlag);
		}

		/// <summary>
		/// Find all the modules which affect the public compile environment. 
		/// </summary>
		/// <param name="ModuleToIncludePathsOnlyFlag"></param>
		protected void FindModulesInPublicCompileEnvironment(Dictionary<UEBuildModule, bool> ModuleToIncludePathsOnlyFlag)
		{
			//
			bool bModuleIncludePathsOnly;
			if (ModuleToIncludePathsOnlyFlag.TryGetValue(this, out bModuleIncludePathsOnly) && !bModuleIncludePathsOnly)
			{
				return;
			}

			ModuleToIncludePathsOnlyFlag[this] = false;

			foreach (UEBuildModule DependencyModule in PublicDependencyModules)
			{
				DependencyModule.FindModulesInPublicCompileEnvironment(ModuleToIncludePathsOnlyFlag);
			}

			// Now add an include paths from modules with header files that we need access to, but won't necessarily be importing
			foreach (UEBuildModule IncludePathModule in PublicIncludePathModules)
			{
				IncludePathModule.FindIncludePathModulesInPublicCompileEnvironment(ModuleToIncludePathsOnlyFlag);
			}
		}

		/// <summary>
		/// Find all the modules which affect the public compile environment. Searches through 
		/// </summary>
		/// <param name="ModuleToIncludePathsOnlyFlag"></param>
		protected void FindIncludePathModulesInPublicCompileEnvironment(Dictionary<UEBuildModule, bool> ModuleToIncludePathsOnlyFlag)
		{
			if (!ModuleToIncludePathsOnlyFlag.ContainsKey(this))
			{
				// Add this module to the list
				ModuleToIncludePathsOnlyFlag.Add(this, true);

				// Include any of its public include path modules in the compile environment too
				foreach (UEBuildModule IncludePathModule in PublicIncludePathModules)
				{
					IncludePathModule.FindIncludePathModulesInPublicCompileEnvironment(ModuleToIncludePathsOnlyFlag);
				}
			}
		}

		private void AddIncludePaths(HashSet<DirectoryReference> IncludePaths, HashSet<DirectoryReference> IncludePathsToAdd)
		{
			// Need to check whether directories exist to avoid bloating compiler command line with generated code directories
			foreach(DirectoryReference IncludePathToAdd in IncludePathsToAdd)
			{
				IncludePaths.Add(IncludePathToAdd);
			}
		}

		/// <summary>
		/// Sets up the environment for compiling any module that includes the public interface of this module.
		/// </summary>
		public virtual void AddModuleToCompileEnvironment(
			UEBuildBinary SourceBinary,
			HashSet<DirectoryReference> IncludePaths,
			HashSet<DirectoryReference> SystemIncludePaths,
			List<string> Definitions,
			List<UEBuildFramework> AdditionalFrameworks,
			bool bLegacyPublicIncludePaths
			)
		{
			// Add the module's parent directory to the include path, so we can root #includes from generated source files to it
			IncludePaths.Add(ModuleDirectory.ParentDirectory);

			// Add this module's public include paths and definitions.
			AddIncludePaths(IncludePaths, PublicIncludePaths);
			if(bLegacyPublicIncludePaths)
			{
				AddIncludePaths(IncludePaths, LegacyPublicIncludePaths);
			}
			SystemIncludePaths.UnionWith(PublicSystemIncludePaths);
			Definitions.AddRange(PublicDefinitions);

			// Add the import or export declaration for the module
			if(Rules.Type == ModuleRules.ModuleType.CPlusPlus)
			{
				if(Rules.Target.LinkType == TargetLinkType.Monolithic)
				{
					if (Rules.Target.bShouldCompileAsDLL && Rules.Target.bHasExports)
					{
						Definitions.Add(ModuleApiDefine + "=DLLEXPORT");
					}
					else
					{
						Definitions.Add(ModuleApiDefine + "=");
					}
				}
				else if(Binary == null || SourceBinary != Binary)
				{
					Definitions.Add(ModuleApiDefine + "=DLLIMPORT");
				}
				else if(!Binary.bAllowExports)
				{
					Definitions.Add(ModuleApiDefine + "=");
				}
				else
				{
					Definitions.Add(ModuleApiDefine + "=DLLEXPORT");
				}
			}

			// Add the additional frameworks so that the compiler can know about their #include paths
			AdditionalFrameworks.AddRange(PublicAdditionalFrameworks);

			// Remember the module so we can refer to it when needed
			foreach (UEBuildFramework Framework in PublicAdditionalFrameworks)
			{
				Framework.OwningModule = this;
			}
		}

		static Regex VCMacroRegex = new Regex(@"\$\([A-Za-z0-9_]+\)");

		/// <summary>
		/// Checks if path contains a VC macro
		/// </summary>
		protected bool DoesPathContainVCMacro(string Path)
		{
			return VCMacroRegex.IsMatch(Path);
		}

		/// <summary>
		/// Sets up the environment for compiling this module.
		/// </summary>
		protected virtual void SetupPrivateCompileEnvironment(
			HashSet<DirectoryReference> IncludePaths,
			HashSet<DirectoryReference> SystemIncludePaths,
			List<string> Definitions,
			List<UEBuildFramework> AdditionalFrameworks,
			bool bWithLegacyPublicIncludePaths
			)
		{
			if (!Rules.bTreatAsEngineModule)
			{
				Definitions.Add("DEPRECATED_FORGAME=DEPRECATED");
			}

			// Add this module's private include paths and definitions.
			IncludePaths.UnionWith(PrivateIncludePaths);

			// Find all the modules that are part of the public compile environment for this module.
			Dictionary<UEBuildModule, bool> ModuleToIncludePathsOnlyFlag = new Dictionary<UEBuildModule, bool>();
			FindModulesInPrivateCompileEnvironment(ModuleToIncludePathsOnlyFlag);

			// Now set up the compile environment for the modules in the original order that we encountered them
			foreach (UEBuildModule Module in ModuleToIncludePathsOnlyFlag.Keys)
			{
				Module.AddModuleToCompileEnvironment(Binary, IncludePaths, SystemIncludePaths, Definitions, AdditionalFrameworks, bWithLegacyPublicIncludePaths);
			}
		}

		/// <summary>
		/// Sets up the environment for linking any module that includes the public interface of this module.
		/// </summary>
		protected virtual void SetupPublicLinkEnvironment(
			UEBuildBinary SourceBinary,
			List<DirectoryReference> LibraryPaths,
			List<string> AdditionalLibraries,
			List<FileReference> OutRuntimeDependencies,
			List<string> Frameworks,
			List<string> WeakFrameworks,
			List<UEBuildFramework> AdditionalFrameworks,
			List<string> AdditionalShadowFiles,
			List<UEBuildBundleResource> AdditionalBundleResources,
			List<string> DelayLoadDLLs,
			List<UEBuildBinary> BinaryDependencies,
			HashSet<UEBuildModule> VisitedModules
			)
		{
			// There may be circular dependencies in compile dependencies, so we need to avoid reentrance.
			if (VisitedModules.Add(this))
			{
				// Add this module's binary to the binary dependencies.
				if (Binary != null
					&& Binary != SourceBinary
					&& !BinaryDependencies.Contains(Binary))
				{
					BinaryDependencies.Add(Binary);
				}

				// If this module belongs to a static library that we are not currently building, recursively add the link environment settings for all of its dependencies too.
				// Keep doing this until we reach a module that is not part of a static library (or external module, since they have no associated binary).
				// Static libraries do not contain the symbols for their dependencies, so we need to recursively gather them to be linked into other binary types.
				bool bIsBuildingAStaticLibrary = (SourceBinary != null && SourceBinary.Type == UEBuildBinaryType.StaticLibrary);
				bool bIsModuleBinaryAStaticLibrary = (Binary != null && Binary.Type == UEBuildBinaryType.StaticLibrary);
				if (!bIsBuildingAStaticLibrary && bIsModuleBinaryAStaticLibrary)
				{
					// Gather all dependencies and recursively call SetupPublicLinkEnvironmnet
					List<UEBuildModule> AllDependencyModules = new List<UEBuildModule>();
					AllDependencyModules.AddRange(PrivateDependencyModules);
					AllDependencyModules.AddRange(PublicDependencyModules);

					foreach (UEBuildModule DependencyModule in AllDependencyModules)
					{
						bool bIsExternalModule = (DependencyModule as UEBuildModuleExternal != null);
						bool bIsInStaticLibrary = (DependencyModule.Binary != null && DependencyModule.Binary.Type == UEBuildBinaryType.StaticLibrary);
						if (bIsExternalModule || bIsInStaticLibrary)
						{
							DependencyModule.SetupPublicLinkEnvironment(SourceBinary, LibraryPaths, AdditionalLibraries, OutRuntimeDependencies, Frameworks, WeakFrameworks,

								AdditionalFrameworks, AdditionalShadowFiles, AdditionalBundleResources, DelayLoadDLLs, BinaryDependencies, VisitedModules);
						}
					}
				}

				// Add this module's public include library paths and additional libraries.
				LibraryPaths.AddRange(PublicLibraryPaths);
				AdditionalLibraries.AddRange(PublicAdditionalLibraries);
				Frameworks.AddRange(PublicFrameworks);
				WeakFrameworks.AddRange(PublicWeakFrameworks);
				AdditionalBundleResources.AddRange(PublicAdditionalBundleResources);
				// Remember the module so we can refer to it when needed
				foreach (UEBuildFramework Framework in PublicAdditionalFrameworks)
				{
					Framework.OwningModule = this;
				}
				AdditionalFrameworks.AddRange(PublicAdditionalFrameworks);
				AdditionalShadowFiles.AddRange(PublicAdditionalShadowFiles);
				DelayLoadDLLs.AddRange(PublicDelayLoadDLLs);

				foreach(RuntimeDependency RuntimeDependency in RuntimeDependencies)
				{
					OutRuntimeDependencies.Add(RuntimeDependency.Path);
				}
			}
		}

		/// <summary>
		/// Sets up the environment for linking this module.
		/// </summary>
		public virtual void SetupPrivateLinkEnvironment(
			UEBuildBinary SourceBinary,
			LinkEnvironment LinkEnvironment,
			List<UEBuildBinary> BinaryDependencies,
			HashSet<UEBuildModule> VisitedModules
			)
		{
			// Allow the module's public dependencies to add library paths and additional libraries to the link environment.
			SetupPublicLinkEnvironment(SourceBinary, LinkEnvironment.LibraryPaths, LinkEnvironment.AdditionalLibraries, LinkEnvironment.RuntimeDependencies, LinkEnvironment.Frameworks, LinkEnvironment.WeakFrameworks,
				LinkEnvironment.AdditionalFrameworks, LinkEnvironment.AdditionalShadowFiles, LinkEnvironment.AdditionalBundleResources, LinkEnvironment.DelayLoadDLLs, BinaryDependencies, VisitedModules);

			// Also allow the module's public and private dependencies to modify the link environment.
			List<UEBuildModule> AllDependencyModules = new List<UEBuildModule>();
			AllDependencyModules.AddRange(PrivateDependencyModules);
			AllDependencyModules.AddRange(PublicDependencyModules);

			foreach (UEBuildModule DependencyModule in AllDependencyModules)
			{
				DependencyModule.SetupPublicLinkEnvironment(SourceBinary, LinkEnvironment.LibraryPaths, LinkEnvironment.AdditionalLibraries, LinkEnvironment.RuntimeDependencies, LinkEnvironment.Frameworks, LinkEnvironment.WeakFrameworks,
					LinkEnvironment.AdditionalFrameworks, LinkEnvironment.AdditionalShadowFiles, LinkEnvironment.AdditionalBundleResources, LinkEnvironment.DelayLoadDLLs, BinaryDependencies, VisitedModules);
			}
		}

		/// <summary>
		/// Compiles the module, and returns a list of files output by the compiler.
		/// </summary>
		public abstract List<FileItem> Compile(ReadOnlyTargetRules Target, UEToolChain ToolChain, CppCompileEnvironment CompileEnvironment, List<PrecompiledHeaderTemplate> SharedPCHModules, ISourceFileWorkingSet WorkingSet, ActionGraph ActionGraph);

		// Object interface.
		public override string ToString()
		{
			return Name;
		}

		/// <summary>
		/// Finds the modules referenced by this module which have not yet been bound to a binary
		/// </summary>
		/// <returns>List of unbound modules</returns>
		public List<UEBuildModule> GetUnboundReferences()
		{
			List<UEBuildModule> Modules = new List<UEBuildModule>();
			Modules.AddRange(PrivateDependencyModules.Where(x => x.Binary == null));
			Modules.AddRange(PublicDependencyModules.Where(x => x.Binary == null));
			return Modules;
		}

		/// <summary>
		/// Gets all of the modules referenced by this module
		/// </summary>
		/// <param name="ReferencedModules">Hash of all referenced modules with their addition index.</param>
		/// <param name="IgnoreReferencedModules">Hashset used to ignore modules which are already added to the list</param>
		/// <param name="bIncludeDynamicallyLoaded">True if dynamically loaded modules (and all of their dependent modules) should be included.</param>
		/// <param name="bForceCircular">True if circular dependencies should be processed</param>
		/// <param name="bOnlyDirectDependencies">True to return only this module's direct dependencies</param>
		public virtual void GetAllDependencyModules(List<UEBuildModule> ReferencedModules, HashSet<UEBuildModule> IgnoreReferencedModules, bool bIncludeDynamicallyLoaded, bool bForceCircular, bool bOnlyDirectDependencies)
		{
		}

		/// <summary>
		/// Gets all of the modules precompiled along with this module
		/// </summary>
		/// <param name="Modules">Set of all the precompiled modules</param>
		public virtual void RecursivelyAddPrecompiledModules(List<UEBuildModule> Modules)
		{
		}

		public delegate UEBuildModule CreateModuleDelegate(string Name, string ReferenceChain);

		/// <summary>
		/// Creates all the modules required for this target
		/// </summary>
		/// <param name="CreateModule">Delegate to create a module with a given name</param>
		/// <param name="ReferenceChain">Chain of references before reaching this module</param>
		public void RecursivelyCreateModules(CreateModuleDelegate CreateModule, string ReferenceChain)
		{
			// Get the reference chain for anything referenced by this module
			string NextReferenceChain = String.Format("{0} -> {1}", ReferenceChain, (RulesFile == null)? Name : RulesFile.GetFileName());

			// Recursively create all the public include path modules. These modules may not be added to the target (and we don't process their referenced 
			// dependencies), but they need to be created to set up their include paths.
			RecursivelyCreateIncludePathModulesByName(Rules.PublicIncludePathModuleNames, ref PublicIncludePathModules, CreateModule, NextReferenceChain);

			// Create all the referenced modules. This path can be recursive, so we check against PrivateIncludePathModules to ensure we don't recurse through the 
			// same module twice (it produces better errors if something fails).
			if(PrivateIncludePathModules == null)
			{
				// Create the private include path modules
				RecursivelyCreateIncludePathModulesByName(Rules.PrivateIncludePathModuleNames, ref PrivateIncludePathModules, CreateModule, NextReferenceChain);

				// Create all the dependency modules
				RecursivelyCreateModulesByName(Rules.PublicDependencyModuleNames, ref PublicDependencyModules, CreateModule, NextReferenceChain);
				RecursivelyCreateModulesByName(Rules.PrivateDependencyModuleNames, ref PrivateDependencyModules, CreateModule, NextReferenceChain);
				RecursivelyCreateModulesByName(Rules.DynamicallyLoadedModuleNames, ref DynamicallyLoadedModules, CreateModule, NextReferenceChain);
			}
		}

		private static void RecursivelyCreateModulesByName(List<string> ModuleNames, ref List<UEBuildModule> Modules, CreateModuleDelegate CreateModule, string ReferenceChain)
		{
			// Check whether the module list is already set. We set this immediately (via the ref) to avoid infinite recursion.
			if (Modules == null)
			{
				Modules = new List<UEBuildModule>();
				foreach (string ModuleName in ModuleNames)
				{
					UEBuildModule Module = CreateModule(ModuleName, ReferenceChain);
					if (!Modules.Contains(Module))
					{
						Module.RecursivelyCreateModules(CreateModule, ReferenceChain);
						Modules.Add(Module);
					}
				}
			}
		}

		private static void RecursivelyCreateIncludePathModulesByName(List<string> ModuleNames, ref List<UEBuildModule> Modules, CreateModuleDelegate CreateModule, string ReferenceChain)
		{
			// Check whether the module list is already set. We set this immediately (via the ref) to avoid infinite recursion.
			if (Modules == null)
			{
				Modules = new List<UEBuildModule>();
				foreach (string ModuleName in ModuleNames)
				{
					UEBuildModule Module = CreateModule(ModuleName, ReferenceChain);
					RecursivelyCreateIncludePathModulesByName(Module.Rules.PublicIncludePathModuleNames, ref Module.PublicIncludePathModules, CreateModule, ReferenceChain);
					Modules.Add(Module);
				}
			}
		}

		/// <summary>
		/// Write information about this binary to a JSON file
		/// </summary>
		/// <param name="Writer">Writer for this binary's data</param>
		public virtual void ExportJson(JsonWriter Writer)
		{
			Writer.WriteValue("Name", Name);
			Writer.WriteValue("Type", Type.ToString());
			Writer.WriteValue("Directory", ModuleDirectory.FullName);
			Writer.WriteValue("Rules", RulesFile.FullName);
			Writer.WriteValue("PCHUsage", Rules.PCHUsage.ToString());

			if (Rules.PrivatePCHHeaderFile != null)
			{
				Writer.WriteValue("PrivatePCH", FileReference.Combine(ModuleDirectory, Rules.PrivatePCHHeaderFile).FullName);
			}

			if (Rules.SharedPCHHeaderFile != null)
			{
				Writer.WriteValue("SharedPCH", FileReference.Combine(ModuleDirectory, Rules.SharedPCHHeaderFile).FullName);
			}

			ExportJsonModuleArray(Writer, "PublicDependencyModules", PublicDependencyModules);
			ExportJsonModuleArray(Writer, "PublicIncludePathModules", PublicIncludePathModules);
			ExportJsonModuleArray(Writer, "PrivateDependencyModules", PrivateDependencyModules);
			ExportJsonModuleArray(Writer, "PrivateIncludePathModules", PrivateIncludePathModules);
			ExportJsonModuleArray(Writer, "DynamicallyLoadedModules", DynamicallyLoadedModules);

			ExportJsonStringArray(Writer, "PublicSystemIncludePaths", PublicSystemIncludePaths.Select(x => x.FullName));
			ExportJsonStringArray(Writer, "PublicIncludePaths", PublicIncludePaths.Select(x => x.FullName));
			ExportJsonStringArray(Writer, "PrivateIncludePaths", PrivateIncludePaths.Select(x => x.FullName));
			ExportJsonStringArray(Writer, "PublicLibraryPaths", PublicLibraryPaths.Select(x => x.FullName));
			ExportJsonStringArray(Writer, "PublicAdditionalLibraries", PublicAdditionalLibraries);
			ExportJsonStringArray(Writer, "PublicFrameworks", PublicFrameworks);
			ExportJsonStringArray(Writer, "PublicWeakFrameworks", PublicWeakFrameworks);
			ExportJsonStringArray(Writer, "PublicDelayLoadDLLs", PublicDelayLoadDLLs);
			ExportJsonStringArray(Writer, "PublicDefinitions", PublicDefinitions);

			Writer.WriteArrayStart("CircularlyReferencedModules");
			foreach(string ModuleName in Rules.CircularlyReferencedDependentModules)
			{
				Writer.WriteValue(ModuleName);
			}
			Writer.WriteArrayEnd();

			Writer.WriteArrayStart("RuntimeDependencies");
			foreach(RuntimeDependency RuntimeDependency in RuntimeDependencies)
			{
				Writer.WriteObjectStart();
				Writer.WriteValue("Path", RuntimeDependency.Path.FullName);
				Writer.WriteValue("Type", RuntimeDependency.Type.ToString());
				Writer.WriteObjectEnd();
			}
			Writer.WriteArrayEnd();
		}

		/// <summary>
		/// Write an array of module names to a JSON writer
		/// </summary>
		/// <param name="Writer">Writer for the array data</param>
		/// <param name="ArrayName">Name of the array property</param>
		/// <param name="Modules">Sequence of modules to write. May be null.</param>
		void ExportJsonModuleArray(JsonWriter Writer, string ArrayName, IEnumerable<UEBuildModule> Modules)
		{
			Writer.WriteArrayStart(ArrayName);
			if (Modules != null)
			{
				foreach (UEBuildModule Module in Modules)
				{
					Writer.WriteValue(Module.Name);
				}
			}
			Writer.WriteArrayEnd();
		}
		
		/// <summary>
		/// Write an array of strings to a JSON writer
		/// </summary>
		/// <param name="Writer">Writer for the array data</param>
		/// <param name="ArrayName">Name of the array property</param>
		/// <param name="Strings">Sequence of strings to write. May be null.</param>
		void ExportJsonStringArray(JsonWriter Writer, string ArrayName, IEnumerable<string> Strings)
		{
			Writer.WriteArrayStart(ArrayName);
			if (Strings != null)
			{
				foreach(string String in Strings)
				{
					Writer.WriteValue(String);
				}
			}
			Writer.WriteArrayEnd();
		}
	};
}
