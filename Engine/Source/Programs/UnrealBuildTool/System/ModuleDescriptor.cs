// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Diagnostics;
using System.Linq;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// The type of host that can load a module
	/// </summary>
	public enum ModuleHostType
	{
		/// <summary>
		/// 
		/// </summary>
		Default,

		/// <summary>
		/// Any target using the UE4 runtime
		/// </summary>
		Runtime,

		/// <summary>
		/// Any target except for commandlet
		/// </summary>
		RuntimeNoCommandlet,

        /// <summary>
        /// Any target or program
        /// </summary>
        RuntimeAndProgram,

        /// <summary>
		/// Loaded only in cooked builds
		/// </summary>
		CookedOnly,

        /// <summary>
        /// Loaded only when the engine has support for developer tools enabled
        /// </summary>
        Developer,

		/// <summary>
		/// Loaded only by the editor
		/// </summary>
		Editor,

		/// <summary>
		/// Loaded only by the editor, except when running commandlets
		/// </summary>
		EditorNoCommandlet,

		/// <summary>
		/// Loaded only by programs
		/// </summary>
		Program,

		/// <summary>
		/// Loaded only by servers
		/// </summary>
        ServerOnly,

		/// <summary>
		/// Loaded only by clients
		/// </summary>
        ClientOnly,
	}

	/// <summary>
	/// Indicates when the engine should attempt to load this module
	/// </summary>
	public enum ModuleLoadingPhase
	{
		/// <summary>
		/// Loaded at the default loading point during startup (during engine init, after game modules are loaded.)
		/// </summary>
		Default,

		/// <summary>
		/// Right after the default phase
		/// </summary>
		PostDefault,

		/// <summary>
		/// Right before the default phase
		/// </summary>
		PreDefault,

		/// <summary>
		/// Loaded as soon as plugins can possibly be loaded (need GConfig)
		/// </summary>
		EarliestPossible,

		/// <summary>
		/// Loaded before the engine is fully initialized, immediately after the config system has been initialized.  Necessary only for very low-level hooks
		/// </summary>
		PostConfigInit,

		/// <summary>
		/// After PostConfigInit and before coreUobject initialized. used for early boot loading screens before the uobjects are initialized
		/// </summary>
		PreEarlyLoadingScreen,

		/// <summary>
		/// Loaded before the engine is fully initialized for modules that need to hook into the loading screen before it triggers
		/// </summary>
		PreLoadingScreen,

		/// <summary>
		/// After the engine has been initialized
		/// </summary>
		PostEngineInit,

		/// <summary>
		/// Do not automatically load this module
		/// </summary>
		None,
	}

	/// <summary>
	/// Class containing information about a code module
	/// </summary>
	[DebuggerDisplay("Name={Name}")]
	public class ModuleDescriptor
	{
		/// <summary>
		/// Name of this module
		/// </summary>
		public readonly string Name;

		/// <summary>
		/// Usage type of module
		/// </summary>
		public ModuleHostType Type;

		/// <summary>
		/// When should the module be loaded during the startup sequence?  This is sort of an advanced setting.
		/// </summary>
		public ModuleLoadingPhase LoadingPhase = ModuleLoadingPhase.Default;

		/// <summary>
		/// List of allowed platforms
		/// </summary>
		public UnrealTargetPlatform[] WhitelistPlatforms;

		/// <summary>
		/// List of disallowed platforms
		/// </summary>
		public UnrealTargetPlatform[] BlacklistPlatforms;

		/// <summary>
		/// List of allowed targets
		/// </summary>
		public TargetType[] WhitelistTargets;

		/// <summary>
		/// List of disallowed targets
		/// </summary>
		public TargetType[] BlacklistTargets;

		/// <summary>
		/// List of allowed target configurations
		/// </summary>
		public UnrealTargetConfiguration[] WhitelistTargetConfigurations;

		/// <summary>
		/// List of disallowed target configurations
		/// </summary>
		public UnrealTargetConfiguration[] BlacklistTargetConfigurations;

		/// <summary>
		/// List of allowed programs
		/// </summary>
		public string[] WhitelistPrograms;

		/// <summary>
		/// List of disallowed programs
		/// </summary>
		public string[] BlacklistPrograms;

		/// <summary>
		/// List of additional dependencies for building this module.
		/// </summary>
		public string[] AdditionalDependencies;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="InName">Name of the module</param>
		/// <param name="InType">Type of target that can host this module</param>
		public ModuleDescriptor(string InName, ModuleHostType InType)
		{
			Name = InName;
			Type = InType;
		}

		/// <summary>
		/// Constructs a ModuleDescriptor from a Json object
		/// </summary>
		/// <param name="InObject"></param>
		/// <returns>The new module descriptor</returns>
		public static ModuleDescriptor FromJsonObject(JsonObject InObject)
		{
			ModuleDescriptor Module = new ModuleDescriptor(InObject.GetStringField("Name"), InObject.GetEnumField<ModuleHostType>("Type"));

			ModuleLoadingPhase LoadingPhase;
			if (InObject.TryGetEnumField<ModuleLoadingPhase>("LoadingPhase", out LoadingPhase))
			{
				Module.LoadingPhase = LoadingPhase;
			}

			try
			{
				string[] WhitelistPlatforms;
				if (InObject.TryGetStringArrayField("WhitelistPlatforms", out WhitelistPlatforms))
				{
					Module.WhitelistPlatforms = Array.ConvertAll(WhitelistPlatforms, x => UnrealTargetPlatform.Parse(x));
				}

				string[] BlacklistPlatforms;
				if (InObject.TryGetStringArrayField("BlacklistPlatforms", out BlacklistPlatforms))
				{
					Module.BlacklistPlatforms = Array.ConvertAll(BlacklistPlatforms, x => UnrealTargetPlatform.Parse(x));
				}
			}
			catch (BuildException Ex)
			{
				ExceptionUtils.AddContext(Ex, "while parsing module descriptor '{0}'", Module.Name);
				throw;
			}

			TargetType[] WhitelistTargets;
			if (InObject.TryGetEnumArrayField<TargetType>("WhitelistTargets", out WhitelistTargets))
			{
				Module.WhitelistTargets = WhitelistTargets;
			}

			TargetType[] BlacklistTargets;
			if (InObject.TryGetEnumArrayField<TargetType>("BlacklistTargets", out BlacklistTargets))
			{
				Module.BlacklistTargets = BlacklistTargets;
			}

			UnrealTargetConfiguration[] WhitelistTargetConfigurations;
			if (InObject.TryGetEnumArrayField<UnrealTargetConfiguration>("WhitelistTargetConfigurations", out WhitelistTargetConfigurations))
			{
				Module.WhitelistTargetConfigurations = WhitelistTargetConfigurations;
			}

			UnrealTargetConfiguration[] BlacklistTargetConfigurations;
			if (InObject.TryGetEnumArrayField<UnrealTargetConfiguration>("BlacklistTargetConfigurations", out BlacklistTargetConfigurations))
			{
				Module.BlacklistTargetConfigurations = BlacklistTargetConfigurations;
			}

			string[] WhitelistPrograms;
			if (InObject.TryGetStringArrayField("WhitelistPrograms", out WhitelistPrograms))
			{
				Module.WhitelistPrograms = WhitelistPrograms;
			}

			string[] BlacklistPrograms;
			if (InObject.TryGetStringArrayField("BlacklistPrograms", out BlacklistPrograms))
			{
				Module.BlacklistPrograms = BlacklistPrograms;
			}

			string[] AdditionalDependencies;
			if (InObject.TryGetStringArrayField("AdditionalDependencies", out AdditionalDependencies))
			{
				Module.AdditionalDependencies = AdditionalDependencies;
			}

			return Module;
		}

		/// <summary>
		/// Write this module to a JsonWriter
		/// </summary>
		/// <param name="Writer">Writer to output to</param>
		void Write(JsonWriter Writer)
		{
			Writer.WriteObjectStart();
			Writer.WriteValue("Name", Name);
			Writer.WriteValue("Type", Type.ToString());
			Writer.WriteValue("LoadingPhase", LoadingPhase.ToString());
			if (WhitelistPlatforms != null && WhitelistPlatforms.Length > 0)
			{
				Writer.WriteArrayStart("WhitelistPlatforms");
				foreach (UnrealTargetPlatform WhitelistPlatform in WhitelistPlatforms)
				{
					Writer.WriteValue(WhitelistPlatform.ToString());
				}
				Writer.WriteArrayEnd();
			}
			if (BlacklistPlatforms != null && BlacklistPlatforms.Length > 0)
			{
				Writer.WriteArrayStart("BlacklistPlatforms");
				foreach (UnrealTargetPlatform BlacklistPlatform in BlacklistPlatforms)
				{
					Writer.WriteValue(BlacklistPlatform.ToString());
				}
				Writer.WriteArrayEnd();
			}
			if (WhitelistTargets != null && WhitelistTargets.Length > 0)
			{
				Writer.WriteArrayStart("WhitelistTargets");
				foreach (TargetType WhitelistTarget in WhitelistTargets)
				{
					Writer.WriteValue(WhitelistTarget.ToString());
				}
				Writer.WriteArrayEnd();
			}
			if (BlacklistTargets != null && BlacklistTargets.Length > 0)
			{
				Writer.WriteArrayStart("BlacklistTargets");
				foreach (TargetType BlacklistTarget in BlacklistTargets)
				{
					Writer.WriteValue(BlacklistTarget.ToString());
				}
				Writer.WriteArrayEnd();
			}
			if (WhitelistTargetConfigurations != null && WhitelistTargetConfigurations.Length > 0)
			{
				Writer.WriteArrayStart("WhitelistTargetConfigurations");
				foreach (UnrealTargetConfiguration WhitelistTargetConfiguration in WhitelistTargetConfigurations)
				{
					Writer.WriteValue(WhitelistTargetConfiguration.ToString());
				}
				Writer.WriteArrayEnd();
			}
			if (BlacklistTargetConfigurations != null && BlacklistTargetConfigurations.Length > 0)
			{
				Writer.WriteArrayStart("BlacklistTargetConfigurations");
				foreach (UnrealTargetConfiguration BlacklistTargetConfiguration in BlacklistTargetConfigurations)
				{
					Writer.WriteValue(BlacklistTargetConfiguration.ToString());
				}
				Writer.WriteArrayEnd();
			}
			if(WhitelistPrograms != null && WhitelistPrograms.Length > 0)
			{
				Writer.WriteStringArrayField("WhitelistPrograms", WhitelistPrograms);
			}
			if(BlacklistPrograms != null && BlacklistPrograms.Length > 0)
			{
				Writer.WriteStringArrayField("BlacklistPrograms", BlacklistPrograms);
			}
			if (AdditionalDependencies != null && AdditionalDependencies.Length > 0)
			{
				Writer.WriteArrayStart("AdditionalDependencies");
				foreach (string AdditionalDependency in AdditionalDependencies)
				{
					Writer.WriteValue(AdditionalDependency);
				}
				Writer.WriteArrayEnd();
			}
			Writer.WriteObjectEnd();
		}

		/// <summary>
		/// Write an array of module descriptors
		/// </summary>
		/// <param name="Writer">The Json writer to output to</param>
		/// <param name="Name">Name of the array</param>
		/// <param name="Modules">Array of modules</param>
		public static void WriteArray(JsonWriter Writer, string Name, ModuleDescriptor[] Modules)
		{
			if (Modules != null && Modules.Length > 0)
			{
				Writer.WriteArrayStart(Name);
				foreach (ModuleDescriptor Module in Modules)
				{
					Module.Write(Writer);
				}
				Writer.WriteArrayEnd();
			}
		}

		/// <summary>
		/// Determines whether the given plugin module is part of the current build.
		/// </summary>
		/// <param name="Platform">The platform being compiled for</param>
		/// <param name="TargetConfiguration">The target configuration being compiled for</param>
		/// <param name="TargetName">Name of the target being built</param>
		/// <param name="TargetType">The type of the target being compiled</param>
		/// <param name="bBuildDeveloperTools">Whether the configuration includes developer tools (typically UEBuildConfiguration.bBuildDeveloperTools for UBT callers)</param>
		/// <param name="bBuildEditor">Whether the configuration includes the editor (typically UEBuildConfiguration.bBuildEditor for UBT callers)</param>
		/// <param name="bBuildRequiresCookedData">Whether the configuration requires cooked content (typically UEBuildConfiguration.bBuildRequiresCookedData for UBT callers)</param>
		public bool IsCompiledInConfiguration(UnrealTargetPlatform Platform, UnrealTargetConfiguration TargetConfiguration, string TargetName, TargetType TargetType, bool bBuildDeveloperTools, bool bBuildEditor, bool bBuildRequiresCookedData)
		{
			// Check the platform is whitelisted
			if (WhitelistPlatforms != null && WhitelistPlatforms.Length > 0 && !WhitelistPlatforms.Contains(Platform))
			{
				return false;
			}

			// Check the platform is not blacklisted
			if (BlacklistPlatforms != null && BlacklistPlatforms.Contains(Platform))
			{
				return false;
			}

			// Check the target is whitelisted
			if (WhitelistTargets != null && WhitelistTargets.Length > 0 && !WhitelistTargets.Contains(TargetType))
			{
				return false;
			}

			// Check the target is not blacklisted
			if (BlacklistTargets != null && BlacklistTargets.Contains(TargetType))
			{
				return false;
			}

			// Check the target configuration is whitelisted
			if (WhitelistTargetConfigurations != null && WhitelistTargetConfigurations.Length > 0 && !WhitelistTargetConfigurations.Contains(TargetConfiguration))
			{
				return false;
			}

			// Check the target configuration is not blacklisted
			if (BlacklistTargetConfigurations != null && BlacklistTargetConfigurations.Contains(TargetConfiguration))
			{
				return false;
			}

			// Special checks just for programs
			if(TargetType == TargetType.Program)
			{
				// Check the program name is whitelisted. Note that this behavior is slightly different to other whitelist/blacklist checks; we will whitelist a module of any type if it's explicitly allowed for this program.
				if(WhitelistPrograms != null && WhitelistPrograms.Length > 0)
				{
					return WhitelistPrograms.Contains(TargetName);
				}
				
				// Check the program name is not blacklisted
				if(BlacklistPrograms != null && BlacklistPrograms.Contains(TargetName))
				{
					return false;
				}
			}

			// Check the module is compatible with this target.
			switch (Type)
			{
				case ModuleHostType.Runtime:
				case ModuleHostType.RuntimeNoCommandlet:
                    return TargetType != TargetType.Program;
                case ModuleHostType.CookedOnly:
                    return bBuildRequiresCookedData;
                case ModuleHostType.RuntimeAndProgram:
					return true;
                case ModuleHostType.Developer:
					return bBuildDeveloperTools;
				case ModuleHostType.Editor:
				case ModuleHostType.EditorNoCommandlet:
					return TargetType == TargetType.Editor || bBuildEditor;
				case ModuleHostType.Program:
					return TargetType == TargetType.Program;
                case ModuleHostType.ServerOnly:
                    return TargetType != TargetType.Program && TargetType != TargetType.Client;
                case ModuleHostType.ClientOnly:
                    return TargetType != TargetType.Program && TargetType != TargetType.Server;
            }

			return false;
		}
	}
}
