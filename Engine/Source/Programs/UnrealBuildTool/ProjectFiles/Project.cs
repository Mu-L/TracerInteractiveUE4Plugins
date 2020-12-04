// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Xml;
using System.Xml.XPath;
using System.Xml.Linq;
using System.Linq;
using System.Text;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// A single target within a project.  A project may have any number of targets within it, which are basically compilable projects
	/// in themselves that the project wraps up.
	/// </summary>
	class ProjectTarget
	{
		/// The target rules file path on disk, if we have one
		public FileReference TargetFilePath;

		/// The project file path on disk
		public FileReference ProjectFilePath;

		/// <summary>
		/// Path to the .uproject file on disk
		/// </summary>
		public FileReference UnrealProjectFilePath;

		/// Optional target rules for this target.  If the target came from a *.Target.cs file on disk, then it will have one of these.
		/// For targets that are synthetic (like UnrealBuildTool or other manually added project files) we won't have a rules object for those.
		public TargetRules TargetRules;

		/// Platforms supported by the target
		public UnrealTargetPlatform[] SupportedPlatforms;

		/// Extra supported build platforms.  Normally the target rules determines these, but for synthetic targets we'll add them here.
		public List<UnrealTargetPlatform> ExtraSupportedPlatforms = new List<UnrealTargetPlatform>();

		/// Extra supported build configurations.  Normally the target rules determines these, but for synthetic targets we'll add them here.
		public List<UnrealTargetConfiguration> ExtraSupportedConfigurations = new List<UnrealTargetConfiguration>();

		/// If true, forces Development configuration regardless of which configuration is set as the Solution Configuration
		public bool ForceDevelopmentConfiguration = false;

		/// Whether the project requires 'Deploy' option set (VC projects)
		public bool ProjectDeploys = false;

		/// Delegate for creating a rules instance for a given platform/configuration
		public Func<UnrealTargetPlatform, UnrealTargetConfiguration, TargetRules> CreateRulesDelegate = null;

		public string Name
		{
			get { return TargetFilePath.GetFileNameWithoutAnyExtensions(); }
		}

		public override string ToString()
		{
			return TargetFilePath.GetFileNameWithoutExtension();
		}
	}

	/// <summary>
	/// Class that stores info about aliased file.
	/// </summary>
	struct AliasedFile
	{
		public AliasedFile(FileReference Location, string FileSystemPath, string ProjectPath)
		{
			this.Location = Location;
			this.FileSystemPath = FileSystemPath;
			this.ProjectPath = ProjectPath;
		}

		// Full location on disk.
		public readonly FileReference Location;

		// File system path.
		public readonly string FileSystemPath;

		// Project path.
		public readonly string ProjectPath;
	}

	abstract class ProjectFile
	{
		/// <summary>
		/// Represents a single source file (or other type of file) in a project
		/// </summary>
		public class SourceFile
		{
			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="InReference">Path to the source file on disk</param>
			/// <param name="InBaseFolder">The directory on this the path within the project will be relative to</param>
			public SourceFile(FileReference InReference, DirectoryReference InBaseFolder)
			{
				Reference = InReference;
				BaseFolder = InBaseFolder;
			}

			public SourceFile()
			{
			}

			/// <summary>
			/// File path to file on disk
			/// </summary>
			public FileReference Reference
			{
				get;
				private set;
			}

			/// <summary>
			/// Optional directory that overrides where files in this project are relative to when displayed in the IDE.  If null, will default to the project's BaseFolder.
			/// </summary>
			public DirectoryReference BaseFolder
			{
				get;
				private set;
			}

			/// <summary>
			/// Define ToString() so the debugger can show the name in watch windows
			/// </summary>
			public override string ToString()
			{
				return Reference.ToString();
			}
		}


		/// <summary>
		/// Constructs a new project file object
		/// </summary>
		/// <param name="InProjectFilePath">The path to the project file, relative to the master project file</param>
		protected ProjectFile(FileReference InProjectFilePath)
		{
			ProjectFilePath = InProjectFilePath;
			ShouldBuildByDefaultForSolutionTargets = true;
			IntelliSenseCppVersion = CppStandardVersion.Default;
		}


		/// Project file path
		public FileReference ProjectFilePath
		{
			get;
			protected set;
		}

		/// <summary>
		/// The base directory for files within this project
		/// </summary>
		public DirectoryReference BaseDir
		{
			get;
			set;
		}

		/// Returns true if this is a generated project (as opposed to an imported project)
		public bool IsGeneratedProject
		{
			get;
			set;
		}


		/// Returns true if this is a "stub" project.  Stub projects function as dumb containers for source files
		/// and are never actually "built" by the master project.  Stub projects are always "generated" projects.
		public bool IsStubProject
		{
			get;
			set;
		}

		/// Returns true if this is a foreign project, and requires UBT to be passed the path to the .uproject file 
		/// on the command line.
		public bool IsForeignProject
		{
			get;
			set;
		}

		/// <summary>
		/// For mod projects, contains the path to the plugin file
		/// </summary>
		public FileReference PluginFilePath
		{
			get;
			set;
		}

		/// Whether this project should be built for all solution targets
		public bool ShouldBuildForAllSolutionTargets
		{
			get;
			set;
		}

		/// Whether this project should be built by default. Can still be built from the IDE through the context menu.
		public bool ShouldBuildByDefaultForSolutionTargets
		{
			get;
			set;
		}

		/// <summary>
		/// C++ version which is used in this project.
		/// </summary>
		public CppStandardVersion IntelliSenseCppVersion
		{
			get;
			protected set;
		}

		/// All of the targets in this project.  All non-stub projects must have at least one target.
		public readonly List<ProjectTarget> ProjectTargets = new List<ProjectTarget>();



		/// <summary>
		/// Adds a list of files to this project, ignoring dupes
		/// </summary>
		/// <param name="FilesToAdd">Files to add</param>
		/// <param name="BaseFolder">The directory the path within the project will be relative to</param>
		public void AddFilesToProject(List<FileReference> FilesToAdd, DirectoryReference BaseFolder)
		{
			foreach (FileReference CurFile in FilesToAdd)
			{
				AddFileToProject(CurFile, BaseFolder);
			}
		}

		/// <summary>
		/// Set of all files that have been added
		/// </summary>
		HashSet<FileReference> AliasedFilesSet = new HashSet<FileReference>();

		/// Aliased (i.e. files is custom filter tree) in this project
		public readonly List<AliasedFile> AliasedFiles = new List<AliasedFile>();

		/// <summary>
		/// Adds aliased file to the project.
		/// </summary>
		/// <param name="File">Aliased file.</param>
		public void AddAliasedFileToProject(AliasedFile File)
		{
			if (AliasedFilesSet.Add(File.Location))
			{
				AliasedFiles.Add(File);
			}
		}

		/// <summary>
		/// Adds a file to this project, ignoring dupes
		/// </summary>
		/// <param name="FilePath">Path to the file on disk</param>
		/// <param name="BaseFolder">The directory the path within the project will be relative to</param>
		public void AddFileToProject(FileReference FilePath, DirectoryReference BaseFolder)
		{
			// Check if hasn't already been added as an aliased file
			if (AliasedFilesSet.Contains(FilePath))
			{
				return;
			}

			// Don't add duplicates
			SourceFile ExistingFile = null;
			if (SourceFileMap.TryGetValue(FilePath, out ExistingFile))
			{
				if (ExistingFile.BaseFolder != BaseFolder)
				{
					throw new BuildException("Trying to add file '" + FilePath + "' to project '" + ProjectFilePath + "' when the file already exists, but with a different relative base folder '" + BaseFolder + "' is different than the current file's '" + ExistingFile.BaseFolder + "'!");
				}
			}
			else
			{
				SourceFile File = AllocSourceFile(FilePath, BaseFolder);
				if (File != null)
				{
					SourceFileMap[FilePath] = File;
					SourceFiles.Add(File);
				}
			}
		}

		/// <summary>
		/// Splits the definition text into macro name and value (if any).
		/// </summary>
		/// <param name="Definition">Definition text</param>
		/// <param name="Key">Out: The definition name</param>
		/// <param name="Value">Out: The definition value or null if it has none</param>
		/// <returns>Pair representing macro name and value.</returns>
		private void SplitDefinitionAndValue(string Definition, out String Key, out String Value)
		{
			int EqualsIndex = Definition.IndexOf('=');
			if (EqualsIndex >= 0)
			{
				Key = Definition.Substring(0, EqualsIndex);
				Value = Definition.Substring(EqualsIndex + 1);
			}
			else
			{
				Key = Definition;
				Value = "";
			}
		}

		/// <summary>
		/// Adds information about a module to this project file
		/// </summary>
		/// <param name="Module">The module to add</param>
		/// <param name="CompileEnvironment">Compile environment for this module</param>
		public virtual void AddModule(UEBuildModuleCPP Module, CppCompileEnvironment CompileEnvironment)
		{
			AddIntelliSensePreprocessorDefinitions(CompileEnvironment.Definitions);
			AddIntelliSenseIncludePaths(SystemIncludePaths, CompileEnvironment.SystemIncludePaths);
			AddIntelliSenseIncludePaths(UserIncludePaths, CompileEnvironment.UserIncludePaths);

			foreach (DirectoryReference BaseDir in Module.ModuleDirectories)
			{
				AddIntelliSenseIncludePaths(BaseDir, BaseDirToSystemIncludePaths, CompileEnvironment.SystemIncludePaths);
				AddIntelliSenseIncludePaths(BaseDir, BaseDirToUserIncludePaths, CompileEnvironment.UserIncludePaths);
			}

			SetIntelliSenseCppVersion(Module.Rules.CppStandard);
		}

		/// <summary>
		/// Adds all of the specified preprocessor definitions to this VCProject's list of preprocessor definitions for all modules in the project
		/// </summary>
		/// <param name="NewPreprocessorDefinitions">List of preprocessor definitons to add</param>
		public void AddIntelliSensePreprocessorDefinitions(List<string> NewPreprocessorDefinitions)
		{
			foreach (string NewPreprocessorDefinition in NewPreprocessorDefinitions)
			{
				// Don't add definitions and value combinations that have already been added for this project
				string CurDef = NewPreprocessorDefinition;
				if (KnownIntelliSensePreprocessorDefinitions.Add(CurDef))
				{
					// Go ahead and check to see if the definition already exists, but the value is different
					bool AlreadyExists = false;

					string Def, Value;
					SplitDefinitionAndValue(CurDef, out Def, out Value);

					// Ignore any API macros being import/export; we'll assume they're valid across the whole project
					if(Def.EndsWith("_API", StringComparison.Ordinal))
					{
                        CurDef = Def + "=";
						Value = "";
					}

					for (int DefineIndex = 0; DefineIndex < IntelliSensePreprocessorDefinitions.Count; ++DefineIndex)
					{
						string ExistingDef, ExistingValue;
						SplitDefinitionAndValue(IntelliSensePreprocessorDefinitions[DefineIndex], out ExistingDef, out ExistingValue);
						if (ExistingDef == Def)
						{
							// Already exists, but the value is changing.  We don't bother clobbering values for existing defines for this project.
							AlreadyExists = true;
							break;
						}
					}

					if (!AlreadyExists)
					{
						IntelliSensePreprocessorDefinitions.Add(CurDef);
					}
				}
			}
		}

		/// <summary>
		/// Adds all of the specified include paths to this VCProject's list of include paths for all modules in the project
		/// </summary>
		/// <param name="BaseDir">The base directory to which the include paths apply</param>
		/// <param name="BaseDirToCollection">Map of base directory to include paths</param>
		/// <param name="NewIncludePaths">List of include paths to add</param>
		public void AddIntelliSenseIncludePaths(DirectoryReference BaseDir, Dictionary<DirectoryReference, IncludePathsCollection> BaseDirToCollection, IEnumerable<DirectoryReference> NewIncludePaths)
		{
			IncludePathsCollection ModuleUserPaths;
			if (!BaseDirToCollection.TryGetValue(BaseDir, out ModuleUserPaths))
			{
				ModuleUserPaths = new IncludePathsCollection();
				BaseDirToCollection.Add(BaseDir, ModuleUserPaths);
			}
			AddIntelliSenseIncludePaths(ModuleUserPaths, NewIncludePaths);
		}

		/// <summary>
		/// Adds all of the specified include paths to this VCProject's list of include paths for all modules in the project
		/// </summary>
		/// <param name="Collection">The collection to add to</param>
		/// <param name="NewIncludePaths">List of include paths to add</param>
		public void AddIntelliSenseIncludePaths(IncludePathsCollection Collection, IEnumerable<DirectoryReference> NewIncludePaths)
		{
			foreach (DirectoryReference CurPath in NewIncludePaths)
			{
				if(Collection.AbsolutePaths.Add(CurPath))
				{
					// Incoming include paths are relative to the solution directory, but we need these paths to be
					// relative to the project file's directory
					string PathRelativeToProjectFile = NormalizeProjectPath(CurPath);
					Collection.RelativePaths.Add(PathRelativeToProjectFile);
				}
			}
		}

		/// <summary>
		/// Sets highest C++ version which is used in this project
		/// </summary>
		/// <param name="CppVersion">Version</param>
		public void SetIntelliSenseCppVersion(CppStandardVersion CppVersion)
		{
			if (CppVersion != CppStandardVersion.Default)
			{
				if (CppVersion > IntelliSenseCppVersion)
				{
					IntelliSenseCppVersion = CppVersion;
				}
			}
		}

		/// <summary>
		/// Add the given project to the DepondsOn project list.
		/// </summary>
		/// <param name="InProjectFile">The project this project is dependent on</param>
		public void AddDependsOnProject(ProjectFile InProjectFile)
		{
			// Make sure that it doesn't exist already
			bool AlreadyExists = false;
			foreach (ProjectFile ExistingDependentOn in DependsOnProjects)
			{
				if (ExistingDependentOn == InProjectFile)
				{
					AlreadyExists = true;
					break;
				}
			}

			if (AlreadyExists == false)
			{
				DependsOnProjects.Add(InProjectFile);
			}
		}

		/// <summary>
		/// Writes a project file to disk
		/// </summary>
		/// <param name="InPlatforms">The platforms to write the project files for</param>
		/// <param name="InConfigurations">The configurations to add to the project files</param>
		/// <param name="PlatformProjectGenerators">The registered platform project generators</param>
		/// <returns>True on success</returns>
		public virtual bool WriteProjectFile(List<UnrealTargetPlatform> InPlatforms, List<UnrealTargetConfiguration> InConfigurations, PlatformProjectGeneratorCollection PlatformProjectGenerators)
		{
			throw new BuildException("UnrealBuildTool cannot automatically generate this project type because WriteProjectFile() was not overridden.");
		}

		/// <summary>
		/// If found writes a debug project file to disk
		/// </summary>
		/// <param name="InPlatforms">The platforms to write the project files for</param>
		/// <param name="InConfigurations">The configurations to add to the project files</param>
		/// <param name="PlatformProjectGenerators">The registered platform project generators</param>
		/// <returns>List of project files written</returns>
		public virtual List<Tuple<ProjectFile, string>> WriteDebugProjectFiles(List<UnrealTargetPlatform> InPlatforms, List<UnrealTargetConfiguration> InConfigurations, PlatformProjectGeneratorCollection PlatformProjectGenerators)
		{
			return null;
		}


		public virtual void LoadGUIDFromExistingProject()
		{
		}

		/// <summary>
		/// Allocates a generator-specific source file object
		/// </summary>
		/// <param name="InitFilePath">Path to the source file on disk</param>
		/// <param name="InitProjectSubFolder">Optional sub-folder to put the file in.  If empty, this will be determined automatically from the file's path relative to the project file</param>
		/// <returns>The newly allocated source file object</returns>
		public virtual SourceFile AllocSourceFile(FileReference InitFilePath, DirectoryReference InitProjectSubFolder = null)
		{
			return new SourceFile(InitFilePath, InitProjectSubFolder);
		}

		/// <summary>
		/// Takes the given path and tries to rebase it relative to the project or solution directory variables.
		/// </summary>
		public static string NormalizeProjectPath(string InputPath)
		{
			// If the path is rooted in an environment variable, leave it be.
			if (InputPath.StartsWith("$("))
			{
				return InputPath;
			}
			else if(InputPath.EndsWith("\\") || InputPath.EndsWith("/"))
			{
				return NormalizeProjectPath(new DirectoryReference(InputPath));
			}
			else
			{
				return NormalizeProjectPath(new FileReference(InputPath));
			}
		}

		/// <summary>
		/// Takes the given path and tries to rebase it relative to the project.
		/// </summary>
		public static string NormalizeProjectPath(FileSystemReference InputPath)
		{
			// Try to make it relative to the solution directory.
			if (InputPath.IsUnderDirectory(ProjectFileGenerator.MasterProjectPath))
			{
				return InputPath.MakeRelativeTo(ProjectFileGenerator.IntermediateProjectFilesPath);
			}
			else
			{
				return InputPath.FullName;
			}
		}

		/// <summary>
		/// Takes the given path, normalizes it, and quotes it if necessary.
		/// </summary>
		public string EscapePath(string InputPath)
		{
			string Result = InputPath;
			if (Result.Contains(' '))
			{
				Result = "\"" + Result + "\"";
			}
			return Result;
		}

		/// <summary>
		/// Visualizer for the debugger
		/// </summary>
		public override string ToString()
		{
			return ProjectFilePath.ToString();
		}

		/// <summary>
		/// Map of file paths to files in the project.
		/// </summary>
		private readonly Dictionary<FileReference, SourceFile> SourceFileMap = new Dictionary<FileReference, SourceFile>();

		/// <summary>
		/// Files in this project
		/// </summary>
		public readonly List<SourceFile> SourceFiles = new List<SourceFile>();

		/// <summary>
		/// Collection of include paths
		/// </summary>
		public class IncludePathsCollection
		{
			public List<string> RelativePaths = new List<string>();
			public HashSet<DirectoryReference> AbsolutePaths = new HashSet<DirectoryReference>();
		}

		/// <summary>
		/// Merged list of include paths for the project
		/// </summary>
		IncludePathsCollection UserIncludePaths = new IncludePathsCollection();

		/// <summary>
		/// Merged list of include paths for the project
		/// </summary>
		IncludePathsCollection SystemIncludePaths = new IncludePathsCollection();

		/// <summary>
		/// Map of base directory to user include paths
		/// </summary>
		protected Dictionary<DirectoryReference, IncludePathsCollection> BaseDirToUserIncludePaths = new Dictionary<DirectoryReference, IncludePathsCollection>();

		/// <summary>
		/// Map of base directory to system include paths
		/// </summary>
		protected Dictionary<DirectoryReference, IncludePathsCollection> BaseDirToSystemIncludePaths = new Dictionary<DirectoryReference, IncludePathsCollection>();

		/// <summary>
		/// Legacy accessor for user search paths
		/// </summary>
		public List<string> IntelliSenseIncludeSearchPaths
		{
			get { return UserIncludePaths.RelativePaths; }
		}

		/// <summary>
		/// Legacy accessor for system include paths
		/// </summary>
		public List<string> IntelliSenseSystemIncludeSearchPaths
		{
			get { return SystemIncludePaths.RelativePaths; }
		}

		/// <summary>
		/// List of preprocessor definitions for the project
		/// </summary>
		public readonly List<string> IntelliSensePreprocessorDefinitions = new List<string>();

		/// <summary>
		/// Set of unique preprocessor definitions
		/// </summary>
		HashSet<string> KnownIntelliSensePreprocessorDefinitions = new HashSet<string>(StringComparer.InvariantCultureIgnoreCase);

		/// <summary>
		/// Projects that this project is dependent on
		/// </summary>
		public readonly List<ProjectFile> DependsOnProjects = new List<ProjectFile>();
	}

}
