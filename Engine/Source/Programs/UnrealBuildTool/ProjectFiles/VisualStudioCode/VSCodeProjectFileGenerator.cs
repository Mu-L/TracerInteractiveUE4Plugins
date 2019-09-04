﻿// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.IO;
using Tools.DotNETCommon;
using System.Diagnostics;

namespace UnrealBuildTool
{
	class VSCodeProjectFolder : MasterProjectFolder
	{
		public VSCodeProjectFolder(ProjectFileGenerator InitOwnerProjectFileGenerator, string InitFolderName)
			: base(InitOwnerProjectFileGenerator, InitFolderName)
		{
		}
	}

	class VSCodeProject : ProjectFile
	{
		public VSCodeProject(FileReference InitFilePath)
			: base(InitFilePath)
		{
		}

		public override bool WriteProjectFile(List<UnrealTargetPlatform> InPlatforms, List<UnrealTargetConfiguration> InConfigurations, PlatformProjectGeneratorCollection PlatformProjectGenerators)
		{
			return true;
		}
	}

	class VSCodeProjectFileGenerator : ProjectFileGenerator
	{
		private DirectoryReference VSCodeDir;
		private UnrealTargetPlatform HostPlatform;
		private bool bForeignProject;
		private DirectoryReference UE4ProjectRoot;
		private bool bBuildingForDotNetCore;
		private string FrameworkExecutableExtension;
		private string FrameworkLibraryExtension = ".dll";

		private enum EPathType
		{
			Absolute,
			Relative,
		}

		private enum EQuoteType
		{
			Single,	// can be ignored on platforms that don't need it (windows atm)
			Double,
		}

		private string CommonMakePathString(FileSystemReference InRef, EPathType InPathType, DirectoryReference InRelativeRoot)
		{
			if (InRelativeRoot == null)
			{
				InRelativeRoot = UE4ProjectRoot;
			}

			string Processed = InRef.ToString();
			
			switch (InPathType)
			{
				case EPathType.Relative:
				{
					if (InRef.IsUnderDirectory(InRelativeRoot))
					{
						Processed = InRef.MakeRelativeTo(InRelativeRoot).ToString();
					}

					break;
				}

				default:
				{
					break;
				}
			}

			if (HostPlatform == UnrealTargetPlatform.Win64)
			{
				Processed = Processed.Replace("\\", "\\\\");
				Processed = Processed.Replace("/", "\\\\");
			}
			else
			{
				Processed = Processed.Replace('\\', '/');
			}

			return Processed;
		}

		private string MakeQuotedPathString(FileSystemReference InRef, EPathType InPathType, DirectoryReference InRelativeRoot = null, EQuoteType InQuoteType = EQuoteType.Double)
		{
			string Processed = CommonMakePathString(InRef, InPathType, InRelativeRoot);

			if (Processed.Contains(" "))
			{
				if (HostPlatform == UnrealTargetPlatform.Win64 && InQuoteType == EQuoteType.Double)
				{
					Processed = "\\\"" + Processed + "\\\"";
				}
				else
				{
					Processed = "'" + Processed + "'";
				}
 			}

			return Processed;
		}

		private string MakeUnquotedPathString(FileSystemReference InRef, EPathType InPathType, DirectoryReference InRelativeRoot = null)
		{
			return CommonMakePathString(InRef, InPathType, InRelativeRoot);
		}

		private string MakePathString(FileSystemReference InRef, bool bInAbsolute = false, bool bForceSkipQuotes = false)
		{
			if (bForceSkipQuotes)
			{
				return MakeUnquotedPathString(InRef, bInAbsolute ? EPathType.Absolute : EPathType.Relative, UE4ProjectRoot);
			}
			else
			{
				return MakeQuotedPathString(InRef, bInAbsolute ? EPathType.Absolute : EPathType.Relative, UE4ProjectRoot);
			}
		}

		public VSCodeProjectFileGenerator(FileReference InOnlyGameProject)
			: base(InOnlyGameProject)
		{
			bBuildingForDotNetCore = Environment.CommandLine.Contains("-dotnetcore");
			FrameworkExecutableExtension = bBuildingForDotNetCore ? ".dll" : ".exe";
		}

		class JsonFile
		{
			public JsonFile()
			{
			}

			public void BeginRootObject()
			{
				BeginObject();
			}

			public void EndRootObject()
			{
				EndObject();
				if (TabString.Length > 0)
				{
					throw new Exception("Called EndRootObject before all objects and arrays have been closed");
				}
			}

			public void BeginObject(string Name = null)
			{
				string Prefix = Name == null ? "" : Quoted(Name) + ": ";
				Lines.Add(TabString + Prefix + "{");
				TabString += "\t";
			}

			public void EndObject()
			{
				Lines[Lines.Count - 1] = Lines[Lines.Count - 1].TrimEnd(',');
				TabString = TabString.Remove(TabString.Length - 1);
				Lines.Add(TabString + "},");
			}

			public void BeginArray(string Name = null)
			{
				string Prefix = Name == null ? "" : Quoted(Name) + ": ";
				Lines.Add(TabString + Prefix + "[");
				TabString += "\t";
			}

			public void EndArray()
			{
				Lines[Lines.Count - 1] = Lines[Lines.Count - 1].TrimEnd(',');
				TabString = TabString.Remove(TabString.Length - 1);
				Lines.Add(TabString + "],");
			}

			public void AddField(string Name, bool Value)
			{
				Lines.Add(TabString + Quoted(Name) + ": " + Value.ToString().ToLower() + ",");
			}

			public void AddField(string Name, string Value)
			{
				Lines.Add(TabString + Quoted(Name) + ": " + Quoted(Value) + ",");
			}

			public void AddUnnamedField(string Value)
			{
				Lines.Add(TabString + Quoted(Value) + ",");
			}

			public void Write(FileReference File)
			{
				Lines[Lines.Count - 1] = Lines[Lines.Count - 1].TrimEnd(',');
				FileReference.WriteAllLines(File, Lines.ToArray());
			}

			private string Quoted(string Value)
			{
				return "\"" + Value + "\"";
			}

			private List<string> Lines = new List<string>();
			private string TabString = "";
		}

		override public string ProjectFileExtension
		{
			get
			{
				return ".vscode";
			}
		}

		public override void CleanProjectFiles(DirectoryReference InMasterProjectDirectory, string InMasterProjectName, DirectoryReference InIntermediateProjectFilesPath)
		{
		}

		public override bool ShouldGenerateIntelliSenseData()
		{
			return true;
		}

		protected override ProjectFile AllocateProjectFile(FileReference InitFilePath)
		{
			return new VSCodeProject(InitFilePath);
		}

		public override MasterProjectFolder AllocateMasterProjectFolder(ProjectFileGenerator InitOwnerProjectFileGenerator, string InitFolderName)
		{
			return new VSCodeProjectFolder(InitOwnerProjectFileGenerator, InitFolderName);
		} 

		protected override bool WriteMasterProjectFile(ProjectFile UBTProject, PlatformProjectGeneratorCollection PlatformProjectGenerators)
		{
			VSCodeDir = DirectoryReference.Combine(MasterProjectPath, ".vscode");
			DirectoryReference.CreateDirectory(VSCodeDir);

			UE4ProjectRoot = UnrealBuildTool.RootDirectory;
			bForeignProject = !VSCodeDir.IsUnderDirectory(UE4ProjectRoot);
			HostPlatform = BuildHostPlatform.Current.Platform;

			List<ProjectFile> Projects;

			if (bForeignProject)
			{
				Projects = new List<ProjectFile>();
				foreach (var Project in AllProjectFiles)
				{
					if (GameProjectName == Project.ProjectFilePath.GetFileNameWithoutAnyExtensions())
					{
						Projects.Add(Project);
						break;
					}
				}
			}
			else
			{
				Projects = new List<ProjectFile>(AllProjectFiles);
			}
			Projects.Sort((A, B) => { return A.ProjectFilePath.GetFileName().CompareTo(B.ProjectFilePath.GetFileName()); });

			ProjectData ProjectData = GatherProjectData(Projects, PlatformProjectGenerators);

			WriteTasksFile(ProjectData);
			WriteLaunchFile(ProjectData);
			WriteWorkspaceSettingsFile(Projects);
			WriteCppPropertiesFile(ProjectData);
			WriteWorkspaceFile(ProjectData);
			//WriteProjectDataFile(ProjectData);

			return true;
		}

		private class ProjectData
		{
			public enum EOutputType
			{
				Library,
				Exe,

				WinExe, // some projects have this so we need to read it, but it will be converted across to Exe so no code should handle it!
			}

			public class BuildProduct
			{
				public FileReference OutputFile { get; set; }
				public FileReference UProjectFile { get; set; }
				public UnrealTargetConfiguration Config { get; set; }
				public UnrealTargetPlatform Platform { get; set; }
				public EOutputType OutputType { get; set; }
		
				public CsProjectInfo CSharpInfo { get; set; }

				public override string ToString()
				{
					return Platform.ToString() + " " + Config.ToString(); 
				}

			}

			public class Target
			{
				public string Name;
				public TargetType Type;
				public List<BuildProduct> BuildProducts = new List<BuildProduct>();
				public List<string> Defines = new List<string>();

				public Target(Project InParentProject, string InName, TargetType InType)
				{
					Name = InName;
					Type = InType;
					InParentProject.Targets.Add(this);
				}

				public override string ToString()
				{
					return Name.ToString() + " " + Type.ToString(); 
				}
			}

			public class Project
			{
				public string Name;
				public ProjectFile SourceProject;
				public List<Target> Targets = new List<Target>();
				public List<string> IncludePaths;

				public override string ToString()
				{
					return Name;
				}
			}

			public List<Project> NativeProjects = new List<Project>();
			public List<Project> CSharpProjects = new List<Project>();
			public List<Project> AllProjects = new List<Project>();
			public List<string> CombinedIncludePaths = new List<string>();
			public List<string> CombinedPreprocessorDefinitions = new List<string>();
		}

		private ProjectData GatherProjectData(List<ProjectFile> InProjects, PlatformProjectGeneratorCollection PlatformProjectGenerators)
		{
			ProjectData ProjectData = new ProjectData();

			foreach (ProjectFile Project in InProjects)
			{
				// Create new project record
				ProjectData.Project NewProject = new ProjectData.Project();
				NewProject.Name = Project.ProjectFilePath.GetFileNameWithoutExtension();
				NewProject.SourceProject = Project;

				ProjectData.AllProjects.Add(NewProject);

				// Add into the correct easy-access list
				if (Project is VSCodeProject)
				{
					foreach (ProjectTarget Target in Project.ProjectTargets)
					{
						Array Configs = Enum.GetValues(typeof(UnrealTargetConfiguration));
						List<UnrealTargetPlatform> Platforms = new List<UnrealTargetPlatform>(Target.TargetRules.GetSupportedPlatforms());

						ProjectData.Target NewTarget = new ProjectData.Target(NewProject, Target.TargetRules.Name, Target.TargetRules.Type);

						if (HostPlatform != UnrealTargetPlatform.Win64)
						{
							Platforms.Remove(UnrealTargetPlatform.Win64);
							Platforms.Remove(UnrealTargetPlatform.Win32);
						}

						foreach (UnrealTargetPlatform Platform in Platforms)
						{
							var BuildPlatform = UEBuildPlatform.GetBuildPlatform(Platform, true);
							if (SupportedPlatforms.Contains(Platform) && (BuildPlatform != null) && (BuildPlatform.HasRequiredSDKsInstalled() == SDKStatus.Valid))
							{
								foreach (UnrealTargetConfiguration Config in Configs)
								{
									if (MSBuildProjectFile.IsValidProjectPlatformAndConfiguration(Target, Platform, Config, PlatformProjectGenerators))
									{
										NewTarget.BuildProducts.Add(new ProjectData.BuildProduct
										{
											Platform = Platform,
											Config = Config,
											UProjectFile = Target.UnrealProjectFilePath,
											OutputType = ProjectData.EOutputType.Exe,
											OutputFile = GetExecutableFilename(Project, Target, Platform, Config),
											CSharpInfo = null
										});
									}
								}
							}
						}

						NewTarget.Defines.AddRange(Target.TargetRules.GlobalDefinitions);
					}

					NewProject.IncludePaths = new List<string>();
					
					List<string> RawIncludes = new List<string>(Project.IntelliSenseIncludeSearchPaths);
					RawIncludes.AddRange(Project.IntelliSenseSystemIncludeSearchPaths);

					if (HostPlatform == UnrealTargetPlatform.Win64)
					{
						RawIncludes.AddRange(VCToolChain.GetVCIncludePaths(UnrealTargetPlatform.Win64, WindowsPlatform.GetDefaultCompiler(null), null).Trim(';').Split(';'));
					}
					else
					{
						RawIncludes.Add("/usr/include");
						RawIncludes.Add("/usr/local/include");
					}

					foreach (string IncludePath in RawIncludes)
					{
						DirectoryReference AbsPath = DirectoryReference.Combine(Project.ProjectFilePath.Directory, IncludePath);
						if (DirectoryReference.Exists(AbsPath))
						{
							string Processed = MakeUnquotedPathString(AbsPath, EPathType.Absolute);
							if (!NewProject.IncludePaths.Contains(Processed))
							{
								NewProject.IncludePaths.Add(Processed);
							}

							if (!ProjectData.CombinedIncludePaths.Contains(Processed))
							{
								ProjectData.CombinedIncludePaths.Add(Processed);
							}
							
						}
					}

					foreach (string Definition in Project.IntelliSensePreprocessorDefinitions)
					{
						string Processed = Definition.Replace("\"", "\\\"");
						// removing trailing spaces on preprocessor definitions as these can be added as empty defines confusing vscode
						Processed = Processed.TrimEnd(' ');
						if (!ProjectData.CombinedPreprocessorDefinitions.Contains(Processed))
						{
							ProjectData.CombinedPreprocessorDefinitions.Add(Processed);
						}
					}

					ProjectData.NativeProjects.Add(NewProject);
				}
				else
				{
					VCSharpProjectFile VCSharpProject = Project as VCSharpProjectFile;

					if (VCSharpProject.IsDotNETCoreProject() == bBuildingForDotNetCore)
					{
						string ProjectName = Project.ProjectFilePath.GetFileNameWithoutExtension();

						ProjectData.Target Target = new ProjectData.Target(NewProject, ProjectName, TargetType.Program);

						UnrealTargetConfiguration[] Configs = { UnrealTargetConfiguration.Debug, UnrealTargetConfiguration.Development };

						foreach (UnrealTargetConfiguration Config in Configs)
						{
							CsProjectInfo Info = VCSharpProject.GetProjectInfo(Config);

							if (!Info.IsDotNETCoreProject() && Info.Properties.ContainsKey("OutputPath"))
							{
								ProjectData.EOutputType OutputType;
								string OutputTypeName;
								if (Info.Properties.TryGetValue("OutputType", out OutputTypeName))
								{
									OutputType = (ProjectData.EOutputType)Enum.Parse(typeof(ProjectData.EOutputType), OutputTypeName);
								}
								else
								{
									OutputType = ProjectData.EOutputType.Library;
								}

								if (OutputType == ProjectData.EOutputType.WinExe)
								{
									OutputType = ProjectData.EOutputType.Exe;
								}

								FileReference OutputFile = null;
								HashSet<FileReference> ProjectBuildProducts = new HashSet<FileReference>();
								Info.FindCompiledBuildProducts(DirectoryReference.Combine(VCSharpProject.ProjectFilePath.Directory, Info.Properties["OutputPath"]), ProjectBuildProducts);
								foreach (FileReference ProjectBuildProduct in ProjectBuildProducts)
								{
									if ((OutputType == ProjectData.EOutputType.Exe && ProjectBuildProduct.GetExtension() == FrameworkExecutableExtension) ||
										(OutputType == ProjectData.EOutputType.Library && ProjectBuildProduct.GetExtension() == FrameworkLibraryExtension))
									{
										OutputFile = ProjectBuildProduct;
										break;
									}
								}

								if (OutputFile != null)
								{
									Target.BuildProducts.Add(new ProjectData.BuildProduct
									{
										Platform = HostPlatform,
										Config = Config,
										OutputFile = OutputFile,
										OutputType = OutputType,
										CSharpInfo = Info
									});
								}
							}
						}

						ProjectData.CSharpProjects.Add(NewProject);
					}
				}
			}

			return ProjectData;
		}

		private void WriteProjectDataFile(ProjectData ProjectData)
		{
			JsonFile OutFile = new JsonFile();

			OutFile.BeginRootObject();
			OutFile.BeginArray("Projects");

			foreach (ProjectData.Project Project in ProjectData.AllProjects)
			{
				foreach (ProjectData.Target Target in Project.Targets)
				{
					OutFile.BeginObject();
					{
						OutFile.AddField("Name", Project.Name);
						OutFile.AddField("Type", Target.Type.ToString());

						if (Project.SourceProject is VCSharpProjectFile)
						{
							OutFile.AddField("ProjectFile", MakePathString(Project.SourceProject.ProjectFilePath));
						}
						else
						{
							OutFile.BeginArray("IncludePaths");
							{
								foreach (string IncludePath in Project.IncludePaths)
								{
									OutFile.AddUnnamedField(IncludePath);
								}
							}
							OutFile.EndArray();
						}

						OutFile.BeginArray("Defines");
						{
							foreach (string Define in Target.Defines)
							{
								OutFile.AddUnnamedField(Define);
							}
						}
						OutFile.EndArray();

						OutFile.BeginArray("BuildProducts");
						{
							foreach (ProjectData.BuildProduct BuildProduct in Target.BuildProducts)
							{
								OutFile.BeginObject();
								{
									OutFile.AddField("Platform", BuildProduct.Platform.ToString());
									OutFile.AddField("Config", BuildProduct.Config.ToString());
									OutFile.AddField("OutputFile", MakePathString(BuildProduct.OutputFile));
									OutFile.AddField("OutputType", BuildProduct.OutputType.ToString());
								}
								OutFile.EndObject();
							}
						}
						OutFile.EndArray();
					}
					OutFile.EndObject();
				}
			}

			OutFile.EndArray();
			OutFile.EndRootObject();
			OutFile.Write(FileReference.Combine(VSCodeDir, "unreal.json"));
		}

		private void WriteCppPropertiesFile(ProjectData Projects)
		{
			JsonFile OutFile = new JsonFile();

			OutFile.BeginRootObject();
			{
				OutFile.BeginArray("configurations");
				{
					OutFile.BeginObject();
					{
						OutFile.AddField("name", "UnrealEngine");

						OutFile.BeginArray("includePath");
						{
							foreach (var Path in Projects.CombinedIncludePaths)
							{
								OutFile.AddUnnamedField(Path);
							}
						}
						OutFile.EndArray();

						if (HostPlatform == UnrealTargetPlatform.Win64)
						{
							OutFile.AddField("intelliSenseMode", "msvc-x64");
						}
						else
						{
							OutFile.AddField("intelliSenseMode", "clang-x64");
						}

						if (HostPlatform == UnrealTargetPlatform.Mac)
						{
							OutFile.BeginArray("macFrameworkPath");
							{
								OutFile.AddUnnamedField("/System/Library/Frameworks");
								OutFile.AddUnnamedField("/Library/Frameworks");
							}
							OutFile.EndArray();
						}

						OutFile.BeginArray("defines");
						{
							foreach (string Definition in Projects.CombinedPreprocessorDefinitions)
							{
								OutFile.AddUnnamedField(Definition);
							}
						}
						OutFile.EndArray();
					}
					OutFile.EndObject();
				}
				OutFile.EndArray();
			}
			OutFile.EndRootObject();

			OutFile.Write(FileReference.Combine(VSCodeDir, "c_cpp_properties.json"));
		}

		private void WriteNativeTask(ProjectData.Project InProject, JsonFile OutFile)
		{
			string[] Commands = { "Build", "Rebuild", "Clean" };

			foreach (ProjectData.Target Target in InProject.Targets)
			{
				foreach (ProjectData.BuildProduct BuildProduct in Target.BuildProducts)
				{
					foreach (string BaseCommand in Commands)
					{
						string Command = BaseCommand == "Rebuild" ? "Build" : BaseCommand;
						string TaskName = String.Format("{0} {1} {2} {3}", Target.Name, BuildProduct.Platform.ToString(), BuildProduct.Config, BaseCommand);
						string CleanTaskName = String.Format("{0} {1} {2} {3}", Target.Name, BuildProduct.Platform.ToString(), BuildProduct.Config, "Clean");
						List<string> ExtraParams = new List<string>();

						OutFile.BeginObject();
						{
							OutFile.AddField("label", TaskName);
							OutFile.AddField("group", "build");

							string CleanParam = Command == "Clean" ? "-clean" : null;

							if (bBuildingForDotNetCore)
							{
								OutFile.AddField("command", "dotnet");
							}
							else
							{
								if (HostPlatform == UnrealTargetPlatform.Win64)
								{
									OutFile.AddField("command", MakePathString(FileReference.Combine(UE4ProjectRoot, "Engine", "Build", "BatchFiles", Command + ".bat")));
									CleanParam = null;
								}
								else
								{
									OutFile.AddField("command", MakePathString(FileReference.Combine(UE4ProjectRoot, "Engine", "Build", "BatchFiles", HostPlatform.ToString(), "Build.sh")));

									if (Command == "Clean")
									{
										CleanParam = "-clean";
									}
								}
							}

							OutFile.BeginArray("args");
							{
								if (bBuildingForDotNetCore)
								{
									OutFile.AddUnnamedField(MakeUnquotedPathString(FileReference.Combine(UE4ProjectRoot, "Engine", "Binaries", "DotNET", "UnrealBuildTool_NETCore.dll"), EPathType.Relative));
								}

								OutFile.AddUnnamedField(Target.Name);
								OutFile.AddUnnamedField(BuildProduct.Platform.ToString());
								OutFile.AddUnnamedField(BuildProduct.Config.ToString());
								if (bForeignProject)
								{
									OutFile.AddUnnamedField(MakeUnquotedPathString(BuildProduct.UProjectFile, EPathType.Relative, null));
								}
								OutFile.AddUnnamedField("-waitmutex");

								if (!string.IsNullOrEmpty(CleanParam))
								{
									OutFile.AddUnnamedField(CleanParam);
								}
							}
							OutFile.EndArray();
							OutFile.AddField("problemMatcher", "$msCompile");
							if (!bForeignProject || BaseCommand == "Rebuild")
							{
								OutFile.BeginArray("dependsOn");
								{
									if (!bForeignProject)
									{
										if (Command == "Build" && Target.Type == TargetType.Editor)
										{
											OutFile.AddUnnamedField("ShaderCompileWorker " + HostPlatform.ToString() + " Development Build");
										}
										else
										{
											OutFile.AddUnnamedField("UnrealBuildTool " + HostPlatform.ToString() + " Development Build");
										}
									}

									if (BaseCommand == "Rebuild")
									{
										OutFile.AddUnnamedField(CleanTaskName);
									}
								}
								OutFile.EndArray();
							}

							OutFile.AddField("type", "shell");

							OutFile.BeginObject("options");
							{
								OutFile.AddField("cwd", MakeUnquotedPathString(UE4ProjectRoot, EPathType.Absolute));
							}
							OutFile.EndObject();
						}
						OutFile.EndObject();
					}
				}
			}
		}

		private void WriteCSharpTask(ProjectData.Project InProject, JsonFile OutFile)
		{
			VCSharpProjectFile ProjectFile = InProject.SourceProject as VCSharpProjectFile;
			bool bIsDotNetCore = ProjectFile.IsDotNETCoreProject();
			string[] Commands = { "Build", "Clean" };

			foreach (ProjectData.Target Target in InProject.Targets)
			{
				foreach (ProjectData.BuildProduct BuildProduct in Target.BuildProducts)
				{
					foreach (string Command in Commands)
					{
						string TaskName = String.Format("{0} {1} {2} {3}", Target.Name, BuildProduct.Platform, BuildProduct.Config, Command);

						OutFile.BeginObject();
						{
							OutFile.AddField("label", TaskName);
							OutFile.AddField("group", "build");
							if (bIsDotNetCore)
							{
								OutFile.AddField("command", "dotnet");
							}
							else
							{
								if (Utils.IsRunningOnMono)
								{
									OutFile.AddField("command", MakePathString(FileReference.Combine(UE4ProjectRoot, "Engine", "Build", "BatchFiles", HostPlatform.ToString(), "RunXBuild.sh")));
								}
								else
								{
									OutFile.AddField("command", MakePathString(FileReference.Combine(UE4ProjectRoot, "Engine", "Build", "BatchFiles", "MSBuild.bat")));
								}
							}
							OutFile.BeginArray("args");
							{
								if (bIsDotNetCore)
								{
									OutFile.AddUnnamedField(Command.ToLower());
								}
								else
								{
									OutFile.AddUnnamedField("/t:" + Command.ToLower());
								}
								
								DirectoryReference BuildRoot = HostPlatform == UnrealTargetPlatform.Win64 ? UE4ProjectRoot : DirectoryReference.Combine(UE4ProjectRoot, "Engine");
								OutFile.AddUnnamedField(MakeUnquotedPathString(InProject.SourceProject.ProjectFilePath, EPathType.Relative, BuildRoot));

								OutFile.AddUnnamedField("/p:GenerateFullPaths=true");
								if (HostPlatform == UnrealTargetPlatform.Win64)
								{
									OutFile.AddUnnamedField("/p:DebugType=portable");
								}
								OutFile.AddUnnamedField("/verbosity:minimal");

								if (bIsDotNetCore)
								{
									OutFile.AddUnnamedField("--configuration");
									OutFile.AddUnnamedField(BuildProduct.Config.ToString());
									OutFile.AddUnnamedField("--output");
									OutFile.AddUnnamedField(MakePathString(BuildProduct.OutputFile.Directory));
								}
								else
								{
									OutFile.AddUnnamedField("/p:Configuration=" + BuildProduct.Config.ToString());
								}
							}
							OutFile.EndArray();
						}
						OutFile.AddField("problemMatcher", "$msCompile");

						if (!bBuildingForDotNetCore)
						{
							OutFile.AddField("type", "shell");
						}

						OutFile.BeginObject("options");
						{
							OutFile.AddField("cwd", MakeUnquotedPathString(UE4ProjectRoot, EPathType.Absolute));
						}

						OutFile.EndObject();
						OutFile.EndObject();
					}
				}
			}
		}

		private void WriteTasksFile(ProjectData ProjectData)
		{
			JsonFile OutFile = new JsonFile();

			OutFile.BeginRootObject();
			{
				OutFile.AddField("version", "2.0.0");

				OutFile.BeginArray("tasks");
				{
					foreach (ProjectData.Project NativeProject in ProjectData.NativeProjects)
					{
						WriteNativeTask(NativeProject, OutFile);
					}

					foreach (ProjectData.Project CSharpProject in ProjectData.CSharpProjects)
					{
						WriteCSharpTask(CSharpProject, OutFile);
					}

					OutFile.EndArray();
				}
			}
			OutFile.EndRootObject();

			OutFile.Write(FileReference.Combine(VSCodeDir, "tasks.json"));
		}
		
		public string EscapePath(string InputPath)
		{
			string Result = InputPath;
			if (Result.Contains(" "))
			{
				Result = "\"" + Result + "\"";
			}
			return Result;
		}

		private FileReference GetExecutableFilename(ProjectFile Project, ProjectTarget Target, UnrealTargetPlatform Platform, UnrealTargetConfiguration Configuration)
		{
			TargetRules TargetRulesObject = Target.TargetRules;
			FileReference TargetFilePath = Target.TargetFilePath;
			string TargetName = TargetFilePath == null ? Project.ProjectFilePath.GetFileNameWithoutExtension() : TargetFilePath.GetFileNameWithoutAnyExtensions();
			string UBTPlatformName = Platform.ToString();
			string UBTConfigurationName = Configuration.ToString();

			string ProjectName = Project.ProjectFilePath.GetFileNameWithoutExtension();

			// Setup output path
			UEBuildPlatform BuildPlatform = UEBuildPlatform.GetBuildPlatform(Platform);

			// Figure out if this is a monolithic build
			bool bShouldCompileMonolithic = BuildPlatform.ShouldCompileMonolithicBinary(Platform);

			if (TargetRulesObject != null)
			{
				bShouldCompileMonolithic |= (Target.CreateRulesDelegate(Platform, Configuration).LinkType == TargetLinkType.Monolithic);
			}

			TargetType TargetRulesType = Target.TargetRules == null ? TargetType.Program : Target.TargetRules.Type;

			// Get the output directory
			DirectoryReference RootDirectory = UnrealBuildTool.EngineDirectory;
			if (TargetRulesType != TargetType.Program && (bShouldCompileMonolithic || TargetRulesObject.BuildEnvironment == TargetBuildEnvironment.Unique))
			{
				if(Target.UnrealProjectFilePath != null)
				{
					RootDirectory = Target.UnrealProjectFilePath.Directory;
				}
			}

			if (TargetRulesType == TargetType.Program)
			{
				if(Target.UnrealProjectFilePath != null)
				{
					RootDirectory = Target.UnrealProjectFilePath.Directory;
				}
			}

			// Get the output directory
			DirectoryReference OutputDirectory = DirectoryReference.Combine(RootDirectory, "Binaries", UBTPlatformName);

			// Get the executable name (minus any platform or config suffixes)
			string BinaryName;
			if(Target.TargetRules.BuildEnvironment == TargetBuildEnvironment.Shared && TargetRulesType != TargetType.Program)
			{
				BinaryName = UEBuildTarget.GetAppNameForTargetType(TargetRulesType);
			}
			else
			{
				BinaryName = TargetName;
			}

			// Make the output file path
			string BinaryFileName = UEBuildTarget.MakeBinaryFileName(BinaryName, Platform, Configuration, TargetRulesObject.Architecture, TargetRulesObject.UndecoratedConfiguration, UEBuildBinaryType.Executable);
			string ExecutableFilename = FileReference.Combine(OutputDirectory, BinaryFileName).FullName;

			// Include the path to the actual executable for a Mac app bundle
			if (Platform == UnrealTargetPlatform.Mac && !Target.TargetRules.bIsBuildingConsoleApplication)
			{
				ExecutableFilename += ".app/Contents/MacOS/" + Path.GetFileName(ExecutableFilename);
			}

			return new FileReference(ExecutableFilename);
		}

		private void WriteNativeLaunchConfig(ProjectData.Project InProject, JsonFile OutFile)
		{
			foreach (ProjectData.Target Target in InProject.Targets)
			{
				foreach (ProjectData.BuildProduct BuildProduct in Target.BuildProducts)
				{
					if (BuildProduct.Platform == HostPlatform)
					{
						string LaunchTaskName = String.Format("{0} {1} {2} Build", Target.Name, BuildProduct.Platform, BuildProduct.Config);

						OutFile.BeginObject();
						{
							OutFile.AddField("name", Target.Name + " (" + BuildProduct.Config.ToString() + ")");
							OutFile.AddField("request", "launch");
							OutFile.AddField("preLaunchTask", LaunchTaskName);
							OutFile.AddField("program", MakeUnquotedPathString(BuildProduct.OutputFile, EPathType.Absolute));								
							
							OutFile.BeginArray("args");
							{
								if (Target.Type == TargetRules.TargetType.Editor)
								{
									if (InProject.Name != "UE4")
									{
										if (bForeignProject)
										{
											OutFile.AddUnnamedField(MakePathString(BuildProduct.UProjectFile, false, true));
										}
										else
										{
											OutFile.AddUnnamedField(InProject.Name);
										}
									}
								}

							}
							OutFile.EndArray();

/*
							DirectoryReference CWD = BuildProduct.OutputFile.Directory;
							while (HostPlatform == UnrealTargetPlatform.Mac && CWD != null && CWD.ToString().Contains(".app"))
							{
								CWD = CWD.ParentDirectory;
							}
							if (CWD != null)
							{
								OutFile.AddField("cwd", MakePathString(CWD, true, true));
							}
 */
							OutFile.AddField("cwd", MakeUnquotedPathString(UE4ProjectRoot, EPathType.Absolute));

							if (HostPlatform == UnrealTargetPlatform.Win64)
							{
								OutFile.AddField("stopAtEntry", false);
								OutFile.AddField("externalConsole", true);

								OutFile.AddField("type", "cppvsdbg");
								OutFile.AddField("visualizerFile", MakeUnquotedPathString(FileReference.Combine(UE4ProjectRoot, "Engine", "Extras", "VisualStudioDebugging", "UE4.natvis"), EPathType.Absolute));
							}
							else
							{
								OutFile.AddField("type", "lldb");
							}
						}
						OutFile.EndObject();
					}
				}
			}
		}

		private void WriteSingleCSharpLaunchConfig(JsonFile OutFile, string InTaskName, string InBuildTaskName, FileReference InExecutable, string[] InArgs, bool bIsDotNetCore)
		{
			OutFile.BeginObject();
			{
				OutFile.AddField("name", InTaskName);

				if (bIsDotNetCore)
				{
					OutFile.AddField("type", "coreclr");
				}
				else
				{
					if (HostPlatform == UnrealTargetPlatform.Win64)
					{
						OutFile.AddField("type", "clr");
					}
					else
					{
						OutFile.AddField("type", "mono");
					}
				}

				OutFile.AddField("request", "launch");

				if (!string.IsNullOrEmpty(InBuildTaskName))
				{
					OutFile.AddField("preLaunchTask", InBuildTaskName);
				}
				
				DirectoryReference CWD = UE4ProjectRoot;

				if (bIsDotNetCore)
				{
					OutFile.AddField("program", "dotnet");
					OutFile.BeginArray("args");
					{
						OutFile.AddUnnamedField(MakePathString(InExecutable));

						if (InArgs != null)
						{
							foreach (string Arg in InArgs)
							{
								OutFile.AddUnnamedField(Arg);
							}
						}
					}
					OutFile.EndArray();
					OutFile.AddField("externalConsole", true);
					OutFile.AddField("stopAtEntry", false);
				}
				else
				{
					OutFile.AddField("program", MakeUnquotedPathString(InExecutable, EPathType.Absolute));

					if (HostPlatform == UnrealTargetPlatform.Win64)
					{
						OutFile.AddField("console", "externalTerminal");
					}
					else
					{
						OutFile.AddField("console", "internalConsole");
					}

					OutFile.BeginArray("args");
					if (InArgs != null)
					{
						foreach (string Arg in InArgs)
						{
							OutFile.AddUnnamedField(Arg);
						}
					}
					OutFile.EndArray();

					OutFile.AddField("internalConsoleOptions", "openOnSessionStart");
				}

				OutFile.AddField("cwd", MakeUnquotedPathString(CWD, EPathType.Absolute));
			}
			OutFile.EndObject();
		}

		private void WriteCSharpLaunchConfig(ProjectData.Project InProject, JsonFile OutFile)
		{
			VCSharpProjectFile CSharpProject = InProject.SourceProject as VCSharpProjectFile;
			bool bIsDotNetCore = CSharpProject.IsDotNETCoreProject();

			foreach (ProjectData.Target Target in InProject.Targets)
			{
				foreach (ProjectData.BuildProduct BuildProduct in Target.BuildProducts)
				{
					if (BuildProduct.OutputType == ProjectData.EOutputType.Exe)
					{
						string TaskName = String.Format("{0} ({1})", Target.Name, BuildProduct.Config);
						string BuildTaskName = String.Format("{0} {1} {2} Build", Target.Name, HostPlatform, BuildProduct.Config);

						WriteSingleCSharpLaunchConfig(OutFile, TaskName, BuildTaskName, BuildProduct.OutputFile, null, bIsDotNetCore);
					}
				}
			}
		}

		private void WriteLaunchFile(ProjectData ProjectData)
		{
			JsonFile OutFile = new JsonFile();

			OutFile.BeginRootObject();
			{
				OutFile.AddField("version", "0.2.0");
				OutFile.BeginArray("configurations");
				{
					foreach (ProjectData.Project Project in ProjectData.NativeProjects)
					{
						WriteNativeLaunchConfig(Project, OutFile);
					}

					foreach (ProjectData.Project Project in ProjectData.CSharpProjects)
					{
						WriteCSharpLaunchConfig(Project, OutFile);
					}
				}

				// Add in a special task for regenerating project files
				string PreLaunchTask = "";
				List<string> Args = new List<string>();
				Args.Add("-projectfiles");
				Args.Add("-vscode");

				if (bForeignProject)
				{
					Args.Add("-project=\\\"" + MakeUnquotedPathString(OnlyGameProject, EPathType.Absolute) + "\\\"");
					Args.Add("-game");
					Args.Add("-engine");
				}
				else
				{
					PreLaunchTask = "UnrealBuildTool " + HostPlatform.ToString() + " Development Build";
				}

				WriteSingleCSharpLaunchConfig(
					OutFile,
					"Generate Project Files",
					PreLaunchTask,
					FileReference.Combine(UE4ProjectRoot, "Engine", "Binaries", "DotNET", "UnrealBuildTool.exe"),
					Args.ToArray(),
					bBuildingForDotNetCore
				);

				OutFile.EndArray();
			}
			OutFile.EndRootObject();

			OutFile.Write(FileReference.Combine(VSCodeDir, "launch.json"));
		}

		protected override void ConfigureProjectFileGeneration(string[] Arguments, ref bool IncludeAllPlatforms)
		{
			base.ConfigureProjectFileGeneration(Arguments, ref IncludeAllPlatforms);
		}

		private void WriteWorkspaceSettingsFile(List<ProjectFile> Projects)
		{
			JsonFile OutFile = new JsonFile();

			List<string> PathsToExclude = new List<string>();

			foreach (ProjectFile Project in Projects)
			{
				bool bFoundTarget = false;
				foreach (ProjectTarget Target in Project.ProjectTargets)
				{
					if (Target.TargetFilePath != null)
					{
						DirectoryReference ProjDir = Target.TargetFilePath.Directory.GetDirectoryName() == "Source" ? Target.TargetFilePath.Directory.ParentDirectory : Target.TargetFilePath.Directory;
						GetExcludePathsCPP(ProjDir, PathsToExclude);
						
						DirectoryReference PluginRootDir = DirectoryReference.Combine(ProjDir, "Plugins");
						WriteWorkspaceSettingsFileForPlugins(PluginRootDir, PathsToExclude);

						bFoundTarget = true;
					}
				}

				if (!bFoundTarget)
				{
					GetExcludePathsCSharp(Project.ProjectFilePath.Directory.ToString(), PathsToExclude);
				}
			}

			OutFile.BeginRootObject();
			{
				OutFile.BeginObject("files.exclude");
				{
					string WorkspaceRoot = UnrealBuildTool.RootDirectory.ToString().Replace('\\', '/') + "/";
					foreach (string PathToExclude in PathsToExclude)
					{
						OutFile.AddField(PathToExclude.Replace('\\', '/').Replace(WorkspaceRoot, ""), true);
					}
				}
				OutFile.EndObject();
			}
			OutFile.EndRootObject();

			OutFile.Write(FileReference.Combine(VSCodeDir, "settings.json"));
		}

		private void WriteWorkspaceSettingsFileForPlugins(DirectoryReference PluginBaseDir, List<string> PathsToExclude)
		{
			if (DirectoryReference.Exists(PluginBaseDir))
			{
				foreach (DirectoryReference SubDir in DirectoryReference.EnumerateDirectories(PluginBaseDir, "*", SearchOption.TopDirectoryOnly))
				{
					string[] UPluginFiles = Directory.GetFiles(SubDir.ToString(), "*.uplugin");
					if (UPluginFiles.Length == 1)
					{
						DirectoryReference PluginDir = SubDir;
						GetExcludePathsCPP(PluginDir, PathsToExclude);
					}
					else
					{
						WriteWorkspaceSettingsFileForPlugins(SubDir, PathsToExclude);
					}
				}
			}
		}
		
		private void WriteWorkspaceFile(ProjectData ProjectData)
		{
			JsonFile WorkspaceFile = new JsonFile();

			WorkspaceFile.BeginRootObject();
			{
				WorkspaceFile.BeginArray("folders");
				{
					// Add the directory in which which the code-workspace file exists.
					// This is also known as ${workspaceRoot}
					WorkspaceFile.BeginObject();
					{
						string ProjectName = bForeignProject ? GameProjectName : "UE4";
						WorkspaceFile.AddField("name", ProjectName);
						WorkspaceFile.AddField("path", ".");
					}
					WorkspaceFile.EndObject();

					// If this project is outside the engine folder, add the root engine directory
					if (bIncludeEngineSource && bForeignProject)
					{
						WorkspaceFile.BeginObject();
						{
							WorkspaceFile.AddField("name", "UE4");
							WorkspaceFile.AddField("path", MakeUnquotedPathString(UnrealBuildTool.RootDirectory, EPathType.Absolute));
						}
						WorkspaceFile.EndObject();
					}
				}
				WorkspaceFile.EndArray();
			}

			WorkspaceFile.BeginObject("settings");
			{
				WorkspaceFile.AddField("typescript.tsc.autoDetect", "off");
			}
			WorkspaceFile.EndObject();
			
			WorkspaceFile.BeginObject("extensions");
			{
				// extensions is a set of recommended extensions that a user should install.
				// Adding this section aids discovery of extensions which are helpful to have installed for Unreal development.
				WorkspaceFile.BeginArray("recommendations");
				{
					WorkspaceFile.AddUnnamedField("ms-vscode.cpptools");
					WorkspaceFile.AddUnnamedField("ms-vscode.csharp");

					// If the platform we run the generator on uses mono, there are additional debugging extensions to add.
					if (Utils.IsRunningOnMono)
					{
						WorkspaceFile.AddUnnamedField("vadimcn.vscode-lldb");
						WorkspaceFile.AddUnnamedField("ms-vscode.mono-debug");
					}
				}
				WorkspaceFile.EndArray();
			}
			WorkspaceFile.EndObject();

			WorkspaceFile.EndRootObject();

			string WorkspaceName = bForeignProject ? GameProjectName : "UE4";
			WorkspaceFile.Write(FileReference.Combine(MasterProjectPath, WorkspaceName + ".code-workspace"));
		}

		private void GetExcludePathsCPP(DirectoryReference BaseDir, List<string> PathsToExclude)
		{
			string[] DirWhiteList = { "Binaries", "Build", "Config", "Plugins", "Source", "Private", "Public", "Classes", "Resources" };
			foreach (DirectoryReference SubDir in DirectoryReference.EnumerateDirectories(BaseDir, "*", SearchOption.TopDirectoryOnly))
			{
				if (Array.Find(DirWhiteList, Dir => Dir == SubDir.GetDirectoryName()) == null)
				{
					string NewSubDir = SubDir.ToString();
					if (!PathsToExclude.Contains(NewSubDir))
					{
						PathsToExclude.Add(NewSubDir);
					}
				}
			}
		}

		private void GetExcludePathsCSharp(string BaseDir, List<string> PathsToExclude)
		{
			string[] BlackList =
			{
				"obj",
				"bin"
			};

			foreach (string BlackListDir in BlackList)
			{
				string ExcludePath = Path.Combine(BaseDir, BlackListDir);
				if (!PathsToExclude.Contains(ExcludePath))
				{
					PathsToExclude.Add(ExcludePath);
				}
			}
		}
	}
}