// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	internal class RiderProjectFile : ProjectFile
	{
		public DirectoryReference RootPath;
		public HashSet<TargetType> TargetTypes;
		public CommandLineArguments Arguments;

		private ToolchainInfo RootToolchainInfo = new ToolchainInfo();
		private UEBuildTarget CurrentTarget;

		public RiderProjectFile(FileReference InProjectFilePath) : base(InProjectFilePath)
		{
		}

		/// <summary>
		/// Write project file info in JSON file.
		/// For every combination of <c>UnrealTargetPlatform</c>, <c>UnrealTargetConfiguration</c> and <c>TargetType</c>
		/// will be generated separate JSON file.
		/// Project file will be stored:
		/// For UE4:  {UE4Root}/Engine/Intermediate/ProjectFiles/.Rider/{Platform}/{Configuration}/{TargetType}/{ProjectName}.json
		/// For game: {GameRoot}/Intermediate/ProjectFiles/.Rider/{Platform}/{Configuration}/{TargetType}/{ProjectName}.json
		/// </summary>
		/// <remarks>
		/// * <c>UnrealTargetPlatform.Win32</c> will be always ignored.
		/// * <c>TargetType.Editor</c> will be generated for current platform only and will ignore <c>UnrealTargetConfiguration.Test</c> and <c>UnrealTargetConfiguration.Shipping</c> configurations
		/// * <c>TargetType.Program</c>  will be generated for current platform only and <c>UnrealTargetConfiguration.Development</c> configuration only 
		/// </remarks>
		/// <param name="InPlatforms"></param>
		/// <param name="InConfigurations"></param>
		/// <param name="PlatformProjectGenerators"></param>
		/// <param name="Minimize"></param>
		/// <returns></returns>
		public bool WriteProjectFile(List<UnrealTargetPlatform> InPlatforms,
			List<UnrealTargetConfiguration> InConfigurations,
			PlatformProjectGeneratorCollection PlatformProjectGenerators, JsonWriterStyle Minimize)
		{
			string ProjectName = ProjectFilePath.GetFileNameWithoutAnyExtensions();
			DirectoryReference ProjectRootFolder = RootPath;
			List<Tuple<FileReference, UEBuildTarget>> FileToTarget = new List<Tuple<FileReference, UEBuildTarget>>();
			foreach (UnrealTargetPlatform Platform in InPlatforms)
			{
				foreach (UnrealTargetConfiguration Configuration in InConfigurations)
				{
					foreach (ProjectTarget ProjectTarget in ProjectTargets)
					{
						if (TargetTypes.Any() && !TargetTypes.Contains(ProjectTarget.TargetRules.Type)) continue;

						// Skip Programs for all configs except for current platform + Development & Debug configurations
						if (ProjectTarget.TargetRules.Type == TargetType.Program &&
						    (BuildHostPlatform.Current.Platform != Platform ||
						     !(Configuration == UnrealTargetConfiguration.Development || Configuration == UnrealTargetConfiguration.Debug)))
						{
							continue;
						}

						// Skip Editor for all platforms except for current platform
						if (ProjectTarget.TargetRules.Type == TargetType.Editor && (BuildHostPlatform.Current.Platform != Platform || (Configuration == UnrealTargetConfiguration.Test || Configuration == UnrealTargetConfiguration.Shipping)))
						{
							continue;
						}
						
						DirectoryReference ConfigurationFolder = DirectoryReference.Combine(ProjectRootFolder, Platform.ToString(), Configuration.ToString());

						DirectoryReference TargetFolder =
							DirectoryReference.Combine(ConfigurationFolder, ProjectTarget.TargetRules.Type.ToString());

						string DefaultArchitecture = UEBuildPlatform
							.GetBuildPlatform(Platform)
							.GetDefaultArchitecture(ProjectTarget.UnrealProjectFilePath);
						TargetDescriptor TargetDesc = new TargetDescriptor(ProjectTarget.UnrealProjectFilePath, ProjectTarget.Name,
							Platform, Configuration, DefaultArchitecture, Arguments);
						try
						{
							UEBuildTarget BuildTarget = UEBuildTarget.Create(TargetDesc, false, false);
						
							FileReference OutputFile = FileReference.Combine(TargetFolder, $"{ProjectName}.json");
							FileToTarget.Add(Tuple.Create(OutputFile, BuildTarget));
						}
						catch(Exception Ex)
						{
							Log.TraceWarning("Exception while generating include data for Target:{0}, Platform: {1}, Configuration: {2}", TargetDesc.Name, Platform.ToString(), Configuration.ToString());
							Log.TraceWarning(Ex.ToString());
						}
					}
				}
			}
			foreach (Tuple<FileReference,UEBuildTarget> tuple in FileToTarget)
			{
				CurrentTarget = tuple.Item2;
				CurrentTarget.PreBuildSetup();
				SerializeTarget(tuple.Item1, CurrentTarget, Minimize);
			}
			
			return true;
		}

		private void SerializeTarget(FileReference OutputFile, UEBuildTarget BuildTarget, JsonWriterStyle Minimize)
		{
			DirectoryReference.CreateDirectory(OutputFile.Directory);
			using (JsonWriter Writer = new JsonWriter(OutputFile, Minimize))
			{
				ExportTarget(BuildTarget, Writer);
			}
		}

		/// <summary>
		/// Write a Target to a JSON writer. Is array is empty, don't write anything
		/// </summary>
		/// <param name="Target"></param>
		/// <param name="Writer">Writer for the array data</param>
		private void ExportTarget(UEBuildTarget Target, JsonWriter Writer)
		{
			Writer.WriteObjectStart();

			Writer.WriteValue("Name", Target.TargetName);
			Writer.WriteValue("Configuration", Target.Configuration.ToString());
			Writer.WriteValue("Platform", Target.Platform.ToString());
			Writer.WriteValue("TargetFile", Target.TargetRulesFile.FullName );
			if (Target.ProjectFile != null)
			{
				Writer.WriteValue("ProjectFile", Target.ProjectFile.FullName );
			}
			
			ExportEnvironmentToJson(Target, Writer);
			
			if(Target.Binaries.Any())
			{
				Writer.WriteArrayStart("Binaries");
				foreach (UEBuildBinary Binary in Target.Binaries)
				{
					Writer.WriteObjectStart();
					ExportBinary(Binary, Writer);
					Writer.WriteObjectEnd();
				}
				Writer.WriteArrayEnd();
			}
			
			CppCompileEnvironment GlobalCompileEnvironment = Target.CreateCompileEnvironmentForProjectFiles();
			HashSet<string> ModuleNames = new HashSet<string>();
			Writer.WriteObjectStart("Modules");
			foreach (UEBuildBinary Binary in Target.Binaries)
			{
				CppCompileEnvironment BinaryCompileEnvironment = Binary.CreateBinaryCompileEnvironment(GlobalCompileEnvironment);
				foreach (UEBuildModule Module in Binary.Modules)
				{
					if(ModuleNames.Add(Module.Name))
					{
						Writer.WriteObjectStart(Module.Name);
						ExportModule(Module, Binary.OutputDir, Target.GetExecutableDir(), Writer);
						UEBuildModuleCPP ModuleCpp = Module as UEBuildModuleCPP;
						if (ModuleCpp != null)
						{
							CppCompileEnvironment ModuleCompileEnvironment = ModuleCpp.CreateCompileEnvironmentForIntellisense(Target.Rules, BinaryCompileEnvironment);
							ExportModuleCpp(ModuleCpp, ModuleCompileEnvironment, Writer);
						}
						Writer.WriteObjectEnd();
					}
				}
			}
			Writer.WriteObjectEnd();
			
			ExportPluginsFromTarget(Target, Writer);
			
			Writer.WriteObjectEnd();
		}

		private void ExportModuleCpp(UEBuildModuleCPP ModuleCPP, CppCompileEnvironment ModuleCompileEnvironment, JsonWriter Writer)
		{
			Writer.WriteValue("GeneratedCodeDirectory", ModuleCPP.GeneratedCodeDirectory != null ? ModuleCPP.GeneratedCodeDirectory.FullName  : string.Empty);
			
			ToolchainInfo ModuleToolchainInfo = GenerateToolchainInfo(ModuleCompileEnvironment);
			if (!ModuleToolchainInfo.Equals(RootToolchainInfo))
			{
				Writer.WriteObjectStart("ToolchainInfo");
				foreach (Tuple<string,object> Field in ModuleToolchainInfo.GetDiff(RootToolchainInfo))
				{
					WriteField(ModuleCPP.Name, Writer, Field);
				}
				Writer.WriteObjectEnd();
			}
			
			if (ModuleCompileEnvironment.PrecompiledHeaderIncludeFilename != null)
			{
				string CorrectFilePathPch;
				if(ExtractWrappedIncludeFile(ModuleCompileEnvironment.PrecompiledHeaderIncludeFilename, out CorrectFilePathPch))
					Writer.WriteValue("SharedPCHFilePath", CorrectFilePathPch);
			}
		}

		private static bool ExtractWrappedIncludeFile(FileSystemReference FileRef, out string CorrectFilePathPch)
		{
			CorrectFilePathPch = "";
			try
			{
				using (StreamReader Reader = new StreamReader(FileRef.FullName))
				{
					string Line = Reader.ReadLine();
					if (Line != null)
					{
						CorrectFilePathPch = Line.Substring("// PCH for ".Length).Trim();
						return true;
					}
				}
			}
			finally
			{
				Log.TraceVerbose("Couldn't extract path to PCH from {0}", FileRef);
			}
			return false;
		}

		/// <summary>
		/// Write a Module to a JSON writer. If array is empty, don't write anything
		/// </summary>
		/// <param name="BinaryOutputDir"></param>
		/// <param name="TargetOutputDir"></param>
		/// <param name="Writer">Writer for the array data</param>
		/// <param name="Module"></param>
		private static void ExportModule(UEBuildModule Module, DirectoryReference BinaryOutputDir, DirectoryReference TargetOutputDir, JsonWriter Writer)
		{
			Writer.WriteValue("Name", Module.Name);
			Writer.WriteValue("Directory", Module.ModuleDirectory.FullName );
			Writer.WriteValue("Rules", Module.RulesFile.FullName );
			Writer.WriteValue("PCHUsage", Module.Rules.PCHUsage.ToString());

			if (Module.Rules.PrivatePCHHeaderFile != null)
			{
				Writer.WriteValue("PrivatePCH", FileReference.Combine(Module.ModuleDirectory, Module.Rules.PrivatePCHHeaderFile).FullName );
			}

			if (Module.Rules.SharedPCHHeaderFile != null)
			{
				Writer.WriteValue("SharedPCH", FileReference.Combine(Module.ModuleDirectory, Module.Rules.SharedPCHHeaderFile).FullName );
			}

			ExportJsonModuleArray(Writer, "PublicDependencyModules", Module.PublicDependencyModules);
			ExportJsonModuleArray(Writer, "PublicIncludePathModules", Module.PublicIncludePathModules);
			ExportJsonModuleArray(Writer, "PrivateDependencyModules", Module.PrivateDependencyModules);
			ExportJsonModuleArray(Writer, "PrivateIncludePathModules", Module.PrivateIncludePathModules);
			ExportJsonModuleArray(Writer, "DynamicallyLoadedModules", Module.DynamicallyLoadedModules);

			ExportJsonStringArray(Writer, "PublicSystemIncludePaths", Module.PublicSystemIncludePaths.Select(x => x.FullName ));
			ExportJsonStringArray(Writer, "PublicIncludePaths", Module.PublicIncludePaths.Select(x => x.FullName ));
			
			ExportJsonStringArray(Writer, "LegacyPublicIncludePaths", Module.LegacyPublicIncludePaths.Select(x => x.FullName ));
			
			ExportJsonStringArray(Writer, "PrivateIncludePaths", Module.PrivateIncludePaths.Select(x => x.FullName));
			ExportJsonStringArray(Writer, "PublicLibraryPaths", Module.PublicSystemLibraryPaths.Select(x => x.FullName));
			ExportJsonStringArray(Writer, "PublicAdditionalLibraries", Module.PublicSystemLibraries.Concat(Module.PublicAdditionalLibraries));
			ExportJsonStringArray(Writer, "PublicFrameworks", Module.PublicFrameworks);
			ExportJsonStringArray(Writer, "PublicWeakFrameworks", Module.PublicWeakFrameworks);
			ExportJsonStringArray(Writer, "PublicDelayLoadDLLs", Module.PublicDelayLoadDLLs);
			ExportJsonStringArray(Writer, "PublicDefinitions", Module.PublicDefinitions);
			ExportJsonStringArray(Writer, "PrivateDefinitions", Module.Rules.PrivateDefinitions);
			ExportJsonStringArray(Writer, "ProjectDefinitions", /* TODO: Add method ShouldAddProjectDefinitions */ !Module.Rules.bTreatAsEngineModule ? Module.Rules.Target.ProjectDefinitions : new string[0]);
			ExportJsonStringArray(Writer, "ApiDefinitions", Module.GetEmptyApiMacros());
			Writer.WriteValue("ShouldAddLegacyPublicIncludePaths", Module.Rules.bLegacyPublicIncludePaths);

			if(Module.Rules.CircularlyReferencedDependentModules.Any())
			{
				Writer.WriteArrayStart("CircularlyReferencedModules");
				foreach (string ModuleName in Module.Rules.CircularlyReferencedDependentModules)
				{
					Writer.WriteValue(ModuleName);
				}
				Writer.WriteArrayEnd();
			}
			
			if(Module.Rules.RuntimeDependencies.Inner.Any())
			{
				// We don't use info from RuntimeDependencies for code analyzes (at the moment)
				// So we're OK with skipping some values if they are not presented
				Writer.WriteArrayStart("RuntimeDependencies");
				foreach (ModuleRules.RuntimeDependency RuntimeDependency in Module.Rules.RuntimeDependencies.Inner)
				{
					Writer.WriteObjectStart();

					try
					{
						Writer.WriteValue("Path",
							Module.ExpandPathVariables(RuntimeDependency.Path, BinaryOutputDir, TargetOutputDir));
					}
					catch(BuildException buildException)
					{
						Log.TraceVerbose("Value {0} for module {1} will not be stored. Reason: {2}", "Path", Module.Name, buildException);	
					}
					
					if (RuntimeDependency.SourcePath != null)
					{
						try
						{
							Writer.WriteValue("SourcePath",
								Module.ExpandPathVariables(RuntimeDependency.SourcePath, BinaryOutputDir,
									TargetOutputDir));
						}
						catch(BuildException buildException)
						{
							Log.TraceVerbose("Value {0} for module {1} will not be stored. Reason: {2}", "SourcePath", Module.Name, buildException);	
						}
					}

					Writer.WriteValue("Type", RuntimeDependency.Type.ToString());
					
					Writer.WriteObjectEnd();
				}
				Writer.WriteArrayEnd();
			}
		}
		
		/// <summary>
		/// Write an array of Modules to a JSON writer. If array is empty, don't write anything
		/// </summary>
		/// <param name="Writer">Writer for the array data</param>
		/// <param name="ArrayName">Name of the array property</param>
		/// <param name="Modules">Sequence of Modules to write. May be null.</param>
		private static void ExportJsonModuleArray(JsonWriter Writer, string ArrayName, IEnumerable<UEBuildModule> Modules)
		{
			if (Modules == null || !Modules.Any()) return;
			
			Writer.WriteArrayStart(ArrayName);
			foreach (UEBuildModule Module in Modules)
			{
				Writer.WriteValue(Module.Name);
			}
			Writer.WriteArrayEnd();
		}
		
		/// <summary>
		/// Write an array of strings to a JSON writer. Ifl array is empty, don't write anything
		/// </summary>
		/// <param name="Writer">Writer for the array data</param>
		/// <param name="ArrayName">Name of the array property</param>
		/// <param name="Strings">Sequence of strings to write. May be null.</param>
		static void ExportJsonStringArray(JsonWriter Writer, string ArrayName, IEnumerable<string> Strings)
		{
			if (Strings == null || !Strings.Any()) return;
			
			Writer.WriteArrayStart(ArrayName);
			foreach (string String in Strings)
			{
				Writer.WriteValue(String);
			}
			Writer.WriteArrayEnd();
		}
		
		/// <summary>
		/// Write uplugin content to a JSON writer
		/// </summary>
		/// <param name="Plugin">Uplugin description</param>
		/// <param name="Writer">JSON writer</param>
		private static void ExportPlugin(UEBuildPlugin Plugin, JsonWriter Writer)
		{
			Writer.WriteObjectStart(Plugin.Name);
			
			Writer.WriteValue("File", Plugin.File.FullName );
			Writer.WriteValue("Type", Plugin.Type.ToString());
			if(Plugin.Dependencies.Any())
			{
				Writer.WriteStringArrayField("Dependencies", Plugin.Dependencies.Select(it => it.Name));
			}
			if(Plugin.Modules.Any())
			{
				Writer.WriteStringArrayField("Modules", Plugin.Modules.Select(it => it.Name));
			}
			
			Writer.WriteObjectEnd();
		}
		
		/// <summary>
		/// Setup plugins for Target and write plugins to JSON writer. Don't write anything if there are no plugins 
		/// </summary>
		/// <param name="Target"></param>
		/// <param name="Writer"></param>
		private static void ExportPluginsFromTarget(UEBuildTarget Target, JsonWriter Writer)
		{
			Target.SetupPlugins();
			if (!Target.BuildPlugins.Any()) return;
			
			Writer.WriteObjectStart("Plugins");
			foreach (UEBuildPlugin plugin in Target.BuildPlugins)
			{
				ExportPlugin(plugin, Writer);
			}
			Writer.WriteObjectEnd();
		}

		/// <summary>
		/// Write information about this binary to a JSON file
		/// </summary>
		/// <param name="Binary"></param>
		/// <param name="Writer">Writer for this binary's data</param>
		private static void ExportBinary(UEBuildBinary Binary, JsonWriter Writer)
		{
			Writer.WriteValue("File", Binary.OutputFilePath.FullName );
			Writer.WriteValue("Type", Binary.Type.ToString());

			Writer.WriteArrayStart("Modules");
			foreach(UEBuildModule Module in Binary.Modules)
			{
				Writer.WriteValue(Module.Name);
			}
			Writer.WriteArrayEnd();
		}
		
		/// <summary>
		/// Write C++ toolchain information to JSON writer
		/// </summary>
		/// <param name="Target"></param>
		/// <param name="Writer"></param>
		private void ExportEnvironmentToJson(UEBuildTarget Target, JsonWriter Writer)
		{
			CppCompileEnvironment GlobalCompileEnvironment = Target.CreateCompileEnvironmentForProjectFiles();
			
			RootToolchainInfo = GenerateToolchainInfo(GlobalCompileEnvironment);
			
			Writer.WriteObjectStart("ToolchainInfo");
			foreach (Tuple<string, object> Field in RootToolchainInfo.GetFields())
			{
				WriteField(Target.TargetName, Writer, Field);
			}
			Writer.WriteObjectEnd();
			
			Writer.WriteArrayStart("EnvironmentIncludePaths");
			foreach (DirectoryReference Path in GlobalCompileEnvironment.UserIncludePaths)
			{
				Writer.WriteValue(Path.FullName );
			}
			foreach (DirectoryReference Path in GlobalCompileEnvironment.SystemIncludePaths)
			{
				Writer.WriteValue(Path.FullName );
			}
			
			// TODO: get corresponding includes for specific platforms
			if (UEBuildPlatform.IsPlatformInGroup(Target.Platform, UnrealPlatformGroup.Windows))
			{
				foreach (DirectoryReference Path in Target.Rules.WindowsPlatform.Environment.IncludePaths)
				{
					Writer.WriteValue(Path.FullName );
				}
			}
			Writer.WriteArrayEnd();
	
			Writer.WriteArrayStart("EnvironmentDefinitions");
			foreach (string Definition in GlobalCompileEnvironment.Definitions)
			{
				Writer.WriteValue(Definition);
			}
			Writer.WriteArrayEnd();
		}

		private static void WriteField(string ModuleOrTargetName, JsonWriter Writer, Tuple<string, object> Field)
		{
			if (Field.Item2 == null) return;
			string Name = Field.Item1;
			if (Field.Item2 is bool)
			{
				Writer.WriteValue(Name, (bool) Field.Item2);
			}
			else if (Field.Item2 is string)
			{
				string FieldValue = (string) Field.Item2;
				if(FieldValue != "")
					Writer.WriteValue(Name, (string) Field.Item2);
			}
			else if (Field.Item2 is int)
			{
				Writer.WriteValue(Name, (int) Field.Item2);
			}
			else if (Field.Item2 is double)
			{
				Writer.WriteValue(Name, (double) Field.Item2);
			}
			else if (Field.Item2 is Enum)
			{
				Writer.WriteValue(Name, Field.Item2.ToString());
			}
			else if (Field.Item2 is IEnumerable<string>)
			{
				IEnumerable<string> FieldValue = (IEnumerable<string>)Field.Item2;
				if(FieldValue.Any())
					Writer.WriteStringArrayField(Name, FieldValue);
			}
			else
			{
				Log.TraceWarning("Dumping incompatible ToolchainInfo field: {0} with type: {1} for: {2}",
					Name, Field.Item2, ModuleOrTargetName);
			}
		}

		private ToolchainInfo GenerateToolchainInfo(CppCompileEnvironment CompileEnvironment)
		{
			ToolchainInfo ToolchainInfo = new ToolchainInfo
			{
				CppStandard = CompileEnvironment.CppStandard,
				Configuration = CompileEnvironment.Configuration.ToString(),
				bEnableExceptions = CompileEnvironment.bEnableExceptions,
				bOptimizeCode = CompileEnvironment.bOptimizeCode,
				bUseInlining = CompileEnvironment.bUseInlining,
				bUseUnity = CompileEnvironment.bUseUnity,
				bCreateDebugInfo = CompileEnvironment.bCreateDebugInfo,
				bIsBuildingLibrary = CompileEnvironment.bIsBuildingLibrary,
				bUseAVX = CompileEnvironment.bUseAVX,
				bIsBuildingDLL = CompileEnvironment.bIsBuildingDLL,
				bUseDebugCRT = CompileEnvironment.bUseDebugCRT,
				bUseRTTI = CompileEnvironment.bUseRTTI,
				bUseStaticCRT = CompileEnvironment.bUseStaticCRT,
				PrecompiledHeaderAction = CompileEnvironment.PrecompiledHeaderAction.ToString(),
				PrecompiledHeaderFile = CompileEnvironment.PrecompiledHeaderFile?.ToString(),
				ForceIncludeFiles = CompileEnvironment.ForceIncludeFiles.Select(Item => Item.ToString()).ToList()
			};

			if (CurrentTarget.Platform.IsInGroup(UnrealPlatformGroup.Windows))
			{
				ToolchainInfo.Architecture = WindowsExports.GetArchitectureSubpath(CurrentTarget.Rules.WindowsPlatform.Architecture);
				
				WindowsCompiler WindowsPlatformCompiler = CurrentTarget.Rules.WindowsPlatform.Compiler;
				ToolchainInfo.bStrictConformanceMode = WindowsPlatformCompiler >= WindowsCompiler.VisualStudio2017 && CurrentTarget.Rules.WindowsPlatform.bStrictConformanceMode;
				ToolchainInfo.Compiler = WindowsPlatformCompiler.ToString();
			}
			else
			{
				string PlatformName = $"{CurrentTarget.Platform}Platform";
				object Value = typeof(ReadOnlyTargetRules).GetProperty(PlatformName)?.GetValue(CurrentTarget.Rules);
				object CompilerField = Value?.GetType().GetProperty("Compiler")?.GetValue(Value);
				if (CompilerField != null)
					ToolchainInfo.Compiler = CompilerField.ToString();
			}
				
			return ToolchainInfo; 
		}
	}
}