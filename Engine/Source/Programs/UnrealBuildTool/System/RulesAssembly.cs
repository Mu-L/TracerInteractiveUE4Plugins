// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.Serialization;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// Stores information about a compiled rules assembly and the types it contains
	/// </summary>
	public class RulesAssembly
	{
		/// <summary>
		/// Outers scope for items created by this assembly. Used for chaining assemblies together.
		/// </summary>
		internal readonly RulesScope Scope;

		/// <summary>
		/// The compiled assembly
		/// </summary>
		private Assembly CompiledAssembly;

		/// <summary>
		/// The base directories for this assembly
		/// </summary>
		private List<DirectoryReference> BaseDirs;

		/// <summary>
		/// All the plugins included in this assembly
		/// </summary>
		private IReadOnlyList<PluginInfo> Plugins;

		/// <summary>
		/// Maps module names to their actual xxx.Module.cs file on disk
		/// </summary>
		private Dictionary<string, FileReference> ModuleNameToModuleFile = new Dictionary<string, FileReference>(StringComparer.InvariantCultureIgnoreCase);

		/// <summary>
		/// Maps target names to their actual xxx.Target.cs file on disk
		/// </summary>
		private Dictionary<string, FileReference> TargetNameToTargetFile = new Dictionary<string, FileReference>(StringComparer.InvariantCultureIgnoreCase);

		/// <summary>
		/// Mapping from module file to its context.
		/// </summary>
		private Dictionary<FileReference, ModuleRulesContext> ModuleFileToContext;

		/// <summary>
		/// Whether this assembly contains engine modules. Used to set default values for bTreatAsEngineModule.
		/// </summary>
		private bool bContainsEngineModules;

		/// <summary>
		/// Whether to use backwards compatible default settings for module and target rules. This is enabled by default for game projects to support a simpler migration path, but
		/// is disabled for engine modules.
		/// </summary>
		private BuildSettingsVersion? DefaultBuildSettings;

		/// <summary>
		/// Whether the modules and targets in this assembly are read-only
		/// </summary>
		private bool bReadOnly;

		/// <summary>
		/// The parent rules assembly that this assembly inherits. Game assemblies inherit the engine assembly, and the engine assembly inherits nothing.
		/// </summary>
		private RulesAssembly Parent;

		/// <summary>
		/// Constructor. Compiles a rules assembly from the given source files.
		/// </summary>
		/// <param name="Scope">The scope of items created by this assembly</param>
		/// <param name="BaseDirs">The base directories for this assembly</param>
		/// <param name="Plugins">All the plugins included in this assembly</param>
		/// <param name="ModuleFileToContext">List of module files to compile</param>
		/// <param name="TargetFiles">List of target files to compile</param>
		/// <param name="AssemblyFileName">The output path for the compiled assembly</param>
		/// <param name="bContainsEngineModules">Whether this assembly contains engine modules. Used to initialize the default value for ModuleRules.bTreatAsEngineModule.</param>
		/// <param name="DefaultBuildSettings">Optional override for the default build settings version for modules created from this assembly.</param>
		/// <param name="bReadOnly">Whether the modules and targets in this assembly are installed, and should be created with the bUsePrecompiled flag set</param> 
		/// <param name="bSkipCompile">Whether to skip compiling this assembly</param>
		/// <param name="Parent">The parent rules assembly</param>
		internal RulesAssembly(RulesScope Scope, List<DirectoryReference> BaseDirs, IReadOnlyList<PluginInfo> Plugins, Dictionary<FileReference, ModuleRulesContext> ModuleFileToContext, List<FileReference> TargetFiles, FileReference AssemblyFileName, bool bContainsEngineModules, BuildSettingsVersion? DefaultBuildSettings, bool bReadOnly, bool bSkipCompile, RulesAssembly Parent)
		{
			this.Scope = Scope;
			this.BaseDirs = BaseDirs;
			this.Plugins = Plugins;
			this.ModuleFileToContext = ModuleFileToContext;
			this.bContainsEngineModules = bContainsEngineModules;
			this.DefaultBuildSettings = DefaultBuildSettings;
			this.bReadOnly = bReadOnly;
			this.Parent = Parent;

			// Find all the source files
			HashSet<FileReference> AssemblySourceFiles = new HashSet<FileReference>();
			AssemblySourceFiles.UnionWith(ModuleFileToContext.Keys);
			AssemblySourceFiles.UnionWith(TargetFiles);

			// Compile the assembly
			if (AssemblySourceFiles.Count > 0)
			{
				List<string> PreprocessorDefines = GetPreprocessorDefinitions();
				CompiledAssembly = DynamicCompilation.CompileAndLoadAssembly(AssemblyFileName, AssemblySourceFiles, PreprocessorDefines: PreprocessorDefines, DoNotCompile: bSkipCompile);
			}

			// Setup the module map
			foreach (FileReference ModuleFile in ModuleFileToContext.Keys)
			{
				string ModuleName = ModuleFile.GetFileNameWithoutAnyExtensions();
				if (!ModuleNameToModuleFile.ContainsKey(ModuleName))
				{
					ModuleNameToModuleFile.Add(ModuleName, ModuleFile);
				}
			}

			// Setup the target map
			foreach (FileReference TargetFile in TargetFiles)
			{
				string TargetName = TargetFile.GetFileNameWithoutAnyExtensions();
				if (!TargetNameToTargetFile.ContainsKey(TargetName))
				{
					TargetNameToTargetFile.Add(TargetName, TargetFile);
				}
			}

			// Write any deprecation warnings for methods overriden from a base with the [ObsoleteOverride] attribute. Unlike the [Obsolete] attribute, this ensures the message
			// is given because the method is implemented, not because it's called.
			if (CompiledAssembly != null)
			{
				foreach (Type CompiledType in CompiledAssembly.GetTypes())
				{
					foreach (MethodInfo Method in CompiledType.GetMethods(BindingFlags.Instance | BindingFlags.Public | BindingFlags.DeclaredOnly))
					{
						ObsoleteOverrideAttribute Attribute = Method.GetCustomAttribute<ObsoleteOverrideAttribute>(true);
						if (Attribute != null)
						{
							FileReference Location;
							if (!TryGetFileNameFromType(CompiledType, out Location))
							{
								Location = new FileReference(CompiledAssembly.Location);
							}
							Log.TraceWarning("{0}: warning: {1}", Location, Attribute.Message);
						}
					}
					if(CompiledType.BaseType == typeof(ModuleRules))
					{
						ConstructorInfo Constructor = CompiledType.GetConstructor(new Type[] { typeof(TargetInfo) });
						if(Constructor != null)
						{
							FileReference Location;
							if (!TryGetFileNameFromType(CompiledType, out Location))
							{
								Location = new FileReference(CompiledAssembly.Location);
							}
							Log.TraceWarning("{0}: warning: Module constructors should take a ReadOnlyTargetRules argument (rather than a TargetInfo argument) and pass it to the base class constructor from 4.15 onwards. Please update the method signature.", Location);
						}
					}
				}
			}
		}

		/// <summary>
		/// Determines if the given path is read-only
		/// </summary>
		/// <param name="Location">The location to check</param>
		/// <returns>True if the path is read-only, false otherwise</returns>
		public bool IsReadOnly(FileSystemReference Location)
		{
			if (BaseDirs.Any(x => Location.IsUnderDirectory(x)))
			{
				return bReadOnly;
			}
			else if (Parent != null)
			{
				return Parent.IsReadOnly(Location);
			}
			else
			{
				return false;
			}
		}

		/// <summary>
		/// Finds all the preprocessor definitions that need to be set for the current engine.
		/// </summary>
		/// <returns>List of preprocessor definitions that should be set</returns>
		public static List<string> GetPreprocessorDefinitions()
		{
			List<string> PreprocessorDefines = new List<string>();
			PreprocessorDefines.Add("WITH_FORWARDED_MODULE_RULES_CTOR");
			PreprocessorDefines.Add("WITH_FORWARDED_TARGET_RULES_CTOR");

			// Define macros for the UE4 version, starting with 4.17
			BuildVersion Version;
			if (BuildVersion.TryRead(BuildVersion.GetDefaultFileName(), out Version))
			{
				for(int MinorVersion = 17; MinorVersion <= Version.MinorVersion; MinorVersion++)
				{
					PreprocessorDefines.Add(String.Format("UE_4_{0}_OR_LATER", MinorVersion));
				}
			}
			return PreprocessorDefines;
		}

		/// <summary>
		/// Fills a list with all the module names in this assembly (or its parent)
		/// </summary>
		/// <param name="ModuleNames">List to receive the module names</param>
		public void GetAllModuleNames(List<string> ModuleNames)
		{
			if (Parent != null)
			{
				Parent.GetAllModuleNames(ModuleNames);
			}
			if (CompiledAssembly != null)
			{
				ModuleNames.AddRange(CompiledAssembly.GetTypes().Where(x => x.IsClass && x.IsSubclassOf(typeof(ModuleRules)) && ModuleNameToModuleFile.ContainsKey(x.Name)).Select(x => x.Name));
			}
		}

		/// <summary>
		/// Fills a list with all the target names in this assembly
		/// </summary>
		/// <param name="TargetNames">List to receive the target names</param>
		/// <param name="bIncludeParentAssembly">Whether to include targets in the parent assembly</param>
		public void GetAllTargetNames(List<string> TargetNames, bool bIncludeParentAssembly)
		{
			if(Parent != null && bIncludeParentAssembly)
			{
				Parent.GetAllTargetNames(TargetNames, true);
			}
			TargetNames.AddRange(TargetNameToTargetFile.Keys);
		}

		/// <summary>
		/// Tries to get the filename that declared the given type
		/// </summary>
		/// <param name="ExistingType"></param>
		/// <param name="File"></param>
		/// <returns>True if the type was found, false otherwise</returns>
		public bool TryGetFileNameFromType(Type ExistingType, out FileReference File)
		{
			if (ExistingType.Assembly == CompiledAssembly)
			{
				string Name = ExistingType.Name;
				if (ModuleNameToModuleFile.TryGetValue(Name, out File))
				{
					return true;
				}

				string NameWithoutTarget = Name;
				if (NameWithoutTarget.EndsWith("Target"))
				{
					NameWithoutTarget = NameWithoutTarget.Substring(0, NameWithoutTarget.Length - 6);
				}
				if (TargetNameToTargetFile.TryGetValue(NameWithoutTarget, out File))
				{
					return true;
				}
			}
			else
			{
				if (Parent != null && Parent.TryGetFileNameFromType(ExistingType, out File))
				{
					return true;
				}
			}

			File = null;
			return false;
		}

		/// <summary>
		/// Gets the source file containing rules for the given module
		/// </summary>
		/// <param name="ModuleName">The name of the module</param>
		/// <returns>The filename containing rules for this module, or an empty string if not found</returns>
		public FileReference GetModuleFileName(string ModuleName)
		{
			FileReference ModuleFile;
			if (ModuleNameToModuleFile.TryGetValue(ModuleName, out ModuleFile))
			{
				return ModuleFile;
			}
			else
			{
				return (Parent == null) ? null : Parent.GetModuleFileName(ModuleName);
			}
		}

		/// <summary>
		/// Gets the type defining rules for the given module
		/// </summary>
		/// <param name="ModuleName">The name of the module</param>
		/// <returns>The rules type for this module, or null if not found</returns>
		public Type GetModuleRulesType(string ModuleName)
		{
			if (ModuleNameToModuleFile.ContainsKey(ModuleName))
			{
				return GetModuleRulesTypeInternal(ModuleName);
			}
			else
			{
				return (Parent == null) ? null : Parent.GetModuleRulesType(ModuleName);
			}
		}

		/// <summary>
		/// Gets the type defining rules for the given module within this assembly
		/// </summary>
		/// <param name="ModuleName">The name of the module</param>
		/// <returns>The rules type for this module, or null if not found</returns>
		private Type GetModuleRulesTypeInternal(string ModuleName)
		{
			// The build module must define a type named 'Rules' that derives from our 'ModuleRules' type.  
			Type RulesObjectType = CompiledAssembly.GetType(ModuleName);
			if (RulesObjectType == null)
			{
				// Temporary hack to avoid System namespace collisions
				// @todo projectfiles: Make rules assemblies require namespaces.
				RulesObjectType = CompiledAssembly.GetType("UnrealBuildTool.Rules." + ModuleName);
			}
			return RulesObjectType;
		}

		/// <summary>
		/// Gets the source file containing rules for the given target
		/// </summary>
		/// <param name="TargetName">The name of the target</param>
		/// <returns>The filename containing rules for this target, or an empty string if not found</returns>
		public FileReference GetTargetFileName(string TargetName)
		{
			FileReference TargetFile;
			if (TargetNameToTargetFile.TryGetValue(TargetName, out TargetFile))
			{
				return TargetFile;
			}
			else
			{
				return (Parent == null) ? null : Parent.GetTargetFileName(TargetName);
			}
		}

		/// <summary>
		/// Creates an instance of a module rules descriptor object for the specified module name
		/// </summary>
		/// <param name="ModuleName">Name of the module</param>
		/// <param name="Target">Information about the target associated with this module</param>
		/// <param name="ReferenceChain">Chain of references leading to this module</param>
		/// <returns>Compiled module rule info</returns>
		public ModuleRules CreateModuleRules(string ModuleName, ReadOnlyTargetRules Target, string ReferenceChain)
		{
			// Currently, we expect the user's rules object type name to be the same as the module name
			string ModuleTypeName = ModuleName;

			// Make sure the base module file is known to us
			FileReference ModuleFileName;
			if (!ModuleNameToModuleFile.TryGetValue(ModuleTypeName, out ModuleFileName))
			{
				if (Parent == null)
				{
					throw new BuildException("Could not find definition for module '{0}', (referenced via {1})", ModuleTypeName, ReferenceChain);
				}
				else
				{
					return Parent.CreateModuleRules(ModuleName, Target, ReferenceChain);
				}
			}

			// get the standard Rules object class from the assembly
			Type BaseRulesObjectType = GetModuleRulesTypeInternal(ModuleTypeName);

			// look around for platform/group modules that we will use instead of the basic module
			Type PlatformRulesObjectType = GetModuleRulesTypeInternal(ModuleTypeName + "_" + Target.Platform.ToString());
			if (PlatformRulesObjectType == null)
			{
				foreach (UnrealPlatformGroup Group in UEBuildPlatform.GetPlatformGroups(Target.Platform))
				{
					// look to see if the group has an override
					Type GroupRulesObjectType = GetModuleRulesTypeInternal(ModuleName + "_" + Group.ToString());

					// we expect only one platform group to be found in the extensions
					if (GroupRulesObjectType != null && PlatformRulesObjectType != null)
					{
						throw new BuildException("Found multiple platform group overrides ({0} and {1}) for module {2} without a platform specific override. Create a platform override with the class hierarchy as needed.", 
							GroupRulesObjectType.Name, PlatformRulesObjectType.Name, ModuleName);
					}
					// remember the platform group if we found it, but keep searching to verify there isn't more than one
					if (GroupRulesObjectType != null)
					{
						PlatformRulesObjectType = GroupRulesObjectType;
					}
				}
			}



			// Figure out the best rules object to use
			Type RulesObjectType = PlatformRulesObjectType != null ? PlatformRulesObjectType : BaseRulesObjectType;
			if (RulesObjectType == null)
			{
				throw new BuildException("Expecting to find a type to be declared in a module rules named '{0}' in {1}.  This type must derive from the 'ModuleRules' type defined by Unreal Build Tool.", ModuleTypeName, CompiledAssembly.FullName);
			}

			// Create an instance of the module's rules object
			try
			{
				// Create an uninitialized ModuleRules object and set some defaults.
				ModuleRules RulesObject = (ModuleRules)FormatterServices.GetUninitializedObject(RulesObjectType);
				// even if we created a platform-extension version of the module rules, we are pretending to be
				// the base type, so that no one else needs to manage this
				RulesObject.Name = ModuleName;
				RulesObject.File = ModuleFileName;
				RulesObject.Directory = ModuleFileName.Directory;
				RulesObject.Context = ModuleFileToContext[RulesObject.File];
				RulesObject.Plugin = RulesObject.Context.Plugin;
				RulesObject.bTreatAsEngineModule = bContainsEngineModules;
				if(DefaultBuildSettings.HasValue)
				{
					RulesObject.DefaultBuildSettings = DefaultBuildSettings.Value;
				}
				RulesObject.bPrecompile = (RulesObject.bTreatAsEngineModule || ModuleName.Equals("UE4Game", StringComparison.OrdinalIgnoreCase)) && Target.bPrecompile;
				RulesObject.bUsePrecompiled = bReadOnly;

				// go up the type hierarchy (if there is a hierarchy), looking for any extra directories for the module
				if (RulesObjectType != BaseRulesObjectType && RulesObjectType != typeof(ModuleRules))
				{
					Type SubType = RulesObjectType;

					RulesObject.DirectoriesForModuleSubClasses = new Dictionary<Type, DirectoryReference>();
					RulesObject.SubclassRules = new List<string>();
					while (SubType != null && SubType != BaseRulesObjectType)
					{
						FileReference SubTypeFileName;
						if (TryGetFileNameFromType(SubType, out SubTypeFileName))
						{
							RulesObject.DirectoriesForModuleSubClasses.Add(SubType, SubTypeFileName.Directory);
							RulesObject.SubclassRules.Add(SubTypeFileName.FullName);
						}
						if (SubType.BaseType == null)
						{
							throw new BuildException("{0} is not derived from {1}", RulesObjectType.Name, BaseRulesObjectType.Name);
						}
						SubType = SubType.BaseType;
					}
				}

				// Call the constructor
				ConstructorInfo Constructor = RulesObjectType.GetConstructor(new Type[] { typeof(ReadOnlyTargetRules) });
				if(Constructor == null)
				{
					throw new BuildException("No valid constructor found for {0}.", ModuleName);
				}
				Constructor.Invoke(RulesObject, new object[] { Target });

				return RulesObject;
			}
			catch (Exception Ex)
			{
				Exception MessageEx = (Ex is TargetInvocationException && Ex.InnerException != null)? Ex.InnerException : Ex;
				throw new BuildException(Ex, "Unable to instantiate module '{0}': {1}\n(referenced via {2})", ModuleName, MessageEx.ToString(), ReferenceChain);
			}
		}

		/// <summary>
		/// Construct an instance of the given target rules
		/// </summary>
		/// <param name="TypeName">Type name of the target rules</param>
		/// <param name="TargetInfo">Target configuration information to pass to the constructor</param>
		/// <returns>Instance of the corresponding TargetRules</returns>
		protected TargetRules CreateTargetRulesInstance(string TypeName, TargetInfo TargetInfo)
		{
			// The build module must define a type named '<TargetName>Target' that derives from our 'TargetRules' type.  
			Type BaseRulesType = CompiledAssembly.GetType(TypeName);
			if (BaseRulesType == null)
			{
				throw new BuildException("Expecting to find a type to be declared in a target rules named '{0}'.  This type must derive from the 'TargetRules' type defined by Unreal Build Tool.", TypeName);
			}

			// Look for platform/group rules that we will use instead of the base rules
			string PlatformRulesName = TargetInfo.Name + "_" + TargetInfo.Platform.ToString();
			Type PlatformRulesType = CompiledAssembly.GetType(TypeName + "_" + TargetInfo.Platform.ToString());
			if (PlatformRulesType == null)
			{
				foreach (UnrealPlatformGroup Group in UEBuildPlatform.GetPlatformGroups(TargetInfo.Platform))
				{
					// look to see if the group has an override
					string GroupRulesName = TargetInfo.Name + "_" + Group.ToString();
					Type GroupRulesObjectType = CompiledAssembly.GetType(TypeName + "_" + Group.ToString());

					// we expect only one platform group to be found in the extensions
					if (GroupRulesObjectType != null && PlatformRulesType != null)
					{
						throw new BuildException("Found multiple platform group overrides ({0} and {1}) for rules {2} without a platform specific override. Create a platform override with the class hierarchy as needed.", 
							GroupRulesObjectType.Name, PlatformRulesType.Name, TypeName);
					}
					// remember the platform group if we found it, but keep searching to verify there isn't more than one
					if (GroupRulesObjectType != null)
					{
						PlatformRulesName = GroupRulesName;
						PlatformRulesType = GroupRulesObjectType;
					}
				}
			}
			if (PlatformRulesType != null && !PlatformRulesType.IsSubclassOf(BaseRulesType))
			{
				throw new BuildException("Expecting {0} to be a specialization of {1}", PlatformRulesType, BaseRulesType);
			}

			// Create an instance of the module's rules object, and set some defaults before calling the constructor.
			Type RulesType = PlatformRulesType ?? BaseRulesType;
			TargetRules Rules = (TargetRules)FormatterServices.GetUninitializedObject(RulesType);
			if (DefaultBuildSettings.HasValue)
			{
				Rules.DefaultBuildSettings = DefaultBuildSettings.Value;
			}

			// Return the base target file name to the caller. This affects where the resulting build product is created so the platform/group is not desired in this case.
			Rules.File = TargetNameToTargetFile[TargetInfo.Name];

			// Find the constructor
			ConstructorInfo Constructor = RulesType.GetConstructor(new Type[] { typeof(TargetInfo) });
			if(Constructor == null)
			{
				throw new BuildException("No constructor found on {0} which takes an argument of type TargetInfo.", RulesType.Name);
			}

			// Invoke the regular constructor
			try
			{
				Constructor.Invoke(Rules, new object[] { TargetInfo });
			}
			catch (Exception Ex)
			{
				throw new BuildException(Ex, "Unable to instantiate instance of '{0}' object type from compiled assembly '{1}'.  Unreal Build Tool creates an instance of your module's 'Rules' object in order to find out about your module's requirements.  The CLR exception details may provide more information:  {2}", TypeName, Path.GetFileNameWithoutExtension(CompiledAssembly.Location), Ex.ToString());
			}

			// Set the default overriddes for the configured target type
			Rules.SetOverridesForTargetType();

			// Set the final value for the link type in the target rules
			if(Rules.LinkType == TargetLinkType.Default)
			{
				throw new BuildException("TargetRules.LinkType should be inferred from TargetType");
			}

			// Set the default value for whether to use the shared build environment
			if(Rules.BuildEnvironment == TargetBuildEnvironment.Unique && UnrealBuildTool.IsEngineInstalled())
			{
				throw new BuildException("Targets with a unique build environment cannot be built an installed engine.");
			}

			// Automatically include CoreUObject
			if (Rules.bCompileAgainstEngine)
			{
				Rules.bCompileAgainstCoreUObject = true;
			}

			// Must have editor only data if building the editor.
			if (Rules.bBuildEditor)
			{
				Rules.bBuildWithEditorOnlyData = true;
			}

			// Apply the override to force debug info to be enabled
			if (Rules.bForceDebugInfo)
			{
				Rules.bDisableDebugInfo = false;
				Rules.bOmitPCDebugInfoInDevelopment = false;
			}

			// Setup the malloc profiler
			if (Rules.bUseMallocProfiler)
			{
				Rules.bOmitFramePointers = false;
				Rules.GlobalDefinitions.Add("USE_MALLOC_PROFILER=1");
			}

			// Set a macro if we allow using generated inis
			if (!Rules.bAllowGeneratedIniWhenCooked)
			{
				Rules.GlobalDefinitions.Add("DISABLE_GENERATED_INI_WHEN_COOKED=1");
			}

			if (!Rules.bAllowNonUFSIniWhenCooked)
			{
				Rules.GlobalDefinitions.Add("DISABLE_NONUFS_INI_WHEN_COOKED=1");
			}

			if (Rules.bDisableUnverifiedCertificates)
			{
				Rules.GlobalDefinitions.Add("DISABLE_UNVERIFIED_CERTIFICATE_LOADING=1");
			}

			// Allow the platform to finalize the settings
			UEBuildPlatform Platform = UEBuildPlatform.GetBuildPlatform(Rules.Platform);
			Platform.ValidateTarget(Rules);

			// Some platforms may *require* monolithic compilation...
			if (Rules.LinkType != TargetLinkType.Monolithic && UEBuildPlatform.PlatformRequiresMonolithicBuilds(Rules.Platform, Rules.Configuration))
			{
				throw new BuildException(String.Format("{0}: {1} does not support modular builds", Rules.Name, Rules.Platform));
			}

			return Rules;
		}

		/// <summary>
		/// Creates a target rules object for the specified target name.
		/// </summary>
		/// <param name="TargetName">Name of the target</param>
		/// <param name="Platform">Platform being compiled</param>
		/// <param name="Configuration">Configuration being compiled</param>
		/// <param name="Architecture">Architecture being built</param>
		/// <param name="ProjectFile">Path to the project file for this target</param>
		/// <param name="Arguments">Command line arguments for this target</param>
		/// <returns>The build target rules for the specified target</returns>
		public TargetRules CreateTargetRules(string TargetName, UnrealTargetPlatform Platform, UnrealTargetConfiguration Configuration, string Architecture, FileReference ProjectFile, CommandLineArguments Arguments)
		{
			bool bFoundTargetName = TargetNameToTargetFile.ContainsKey(TargetName);
			if (bFoundTargetName == false)
			{
				if (Parent == null)
				{
					//				throw new BuildException("Couldn't find target rules file for target '{0}' in rules assembly '{1}'.", TargetName, RulesAssembly.FullName);
					string ExceptionMessage = "Couldn't find target rules file for target '";
					ExceptionMessage += TargetName;
					ExceptionMessage += "' in rules assembly '";
					ExceptionMessage += CompiledAssembly.FullName;
					ExceptionMessage += "'." + Environment.NewLine;

					ExceptionMessage += "Location: " + CompiledAssembly.Location + Environment.NewLine;

					ExceptionMessage += "Target rules found:" + Environment.NewLine;
					foreach (KeyValuePair<string, FileReference> entry in TargetNameToTargetFile)
					{
						ExceptionMessage += "\t" + entry.Key + " - " + entry.Value + Environment.NewLine;
					}

					throw new BuildException(ExceptionMessage);
				}
				else
				{
					return Parent.CreateTargetRules(TargetName, Platform, Configuration, Architecture, ProjectFile, Arguments);
				}
			}

			// Currently, we expect the user's rules object type name to be the same as the module name + 'Target'
			string TargetTypeName = TargetName + "Target";

			// The build module must define a type named '<TargetName>Target' that derives from our 'TargetRules' type.  
			return CreateTargetRulesInstance(TargetTypeName, new TargetInfo(TargetName, Platform, Configuration, Architecture, ProjectFile, Arguments));
		}

		/// <summary>
		/// Determines a target name based on the type of target we're trying to build
		/// </summary>
		/// <param name="Type">The type of target to look for</param>
		/// <param name="Platform">The platform being built</param>
		/// <param name="Configuration">The configuration being built</param>
		/// <param name="Architecture">The architecture being built</param>
		/// <param name="ProjectFile">Project file for the target being built</param>
		/// <returns>Name of the target for the given type</returns>
		public string GetTargetNameByType(TargetType Type, UnrealTargetPlatform Platform, UnrealTargetConfiguration Configuration, string Architecture, FileReference ProjectFile)
		{
			// Create all the targets in this assembly 
			List<string> Matches = new List<string>();
			foreach(KeyValuePair<string, FileReference> TargetPair in TargetNameToTargetFile)
			{
				TargetRules Rules = CreateTargetRulesInstance(TargetPair.Key + "Target", new TargetInfo(TargetPair.Key, Platform, Configuration, Architecture, ProjectFile, null));
				if(Rules.Type == Type)
				{
					Matches.Add(TargetPair.Key);
				}
			}

			// If we got a result, return it. If there were multiple results, fail.
			if(Matches.Count == 0)
			{
				if(Parent == null)
				{
					throw new BuildException("Unable to find target of type '{0}' for project '{1}'", Type, ProjectFile);
				}
				else
				{
					return Parent.GetTargetNameByType(Type, Platform, Configuration, Architecture, ProjectFile);
				}
			}
			else
			{
				if(Matches.Count == 1)
				{
					return Matches[0];
				}
				else
				{
					throw new BuildException("Found multiple targets with TargetType={0}: {1}", Type, String.Join(", ", Matches));
				}
			}
		}

		/// <summary>
		/// Enumerates all the plugins that are available
		/// </summary>
		/// <returns></returns>
		public IEnumerable<PluginInfo> EnumeratePlugins()
		{
			return global::UnrealBuildTool.Plugins.FilterPlugins(EnumeratePluginsInternal());
		}

		/// <summary>
		/// Enumerates all the plugins that are available
		/// </summary>
		/// <returns></returns>
		protected IEnumerable<PluginInfo> EnumeratePluginsInternal()
		{
			if (Parent == null)
			{
				return Plugins;
			}
			else
			{
				return Plugins.Concat(Parent.EnumeratePluginsInternal());
			}
		}

		/// <summary>
		/// Tries to find the PluginInfo associated with a given module file
		/// </summary>
		/// <param name="ModuleFile">The module to search for</param>
		/// <param name="Plugin">The matching plugin info, or null.</param>
		/// <returns>True if the module belongs to a plugin</returns>
		public bool TryGetPluginForModule(FileReference ModuleFile, out PluginInfo Plugin)
		{
			ModuleRulesContext Context;
			if (ModuleFileToContext.TryGetValue(ModuleFile, out Context))
			{
				Plugin = Context.Plugin;
				return Plugin != null;
			}
			if(Parent == null)
			{
				Plugin = null;
				return false;
			}
			return Parent.TryGetPluginForModule(ModuleFile, out Plugin);
		}
	}
}
