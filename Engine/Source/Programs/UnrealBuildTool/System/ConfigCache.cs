// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;
using UnrealBuildTool;

namespace UnrealBuildTool
{
	/// <summary>
	/// Caches config files and config file hierarchies
	/// </summary>
	public static class ConfigCache
	{
		/// <summary>
		/// Delegate to add a value to an ICollection in a target object
		/// </summary>
		/// <param name="TargetObject">The object containing the field to be modified</param>
		/// <param name="ValueObject">The value to add</param>
		delegate void AddElementDelegate(object TargetObject, object ValueObject); 

		/// <summary>
		/// Caches information about a field with a [ConfigFile] attribute in a type
		/// </summary>
		class ConfigField
		{
			/// <summary>
			/// The field with the config attribute
			/// </summary>
			public FieldInfo FieldInfo;

			/// <summary>
			/// The attribute instance
			/// </summary>
			public ConfigFileAttribute Attribute;

			/// <summary>
			/// For fields implementing ICollection, specifies the element type
			/// </summary>
			public Type ElementType;

			/// <summary>
			/// For fields implementing ICollection, a callback to add an element type.
			/// </summary>
			public AddElementDelegate AddElement;
		}

		/// <summary>
		/// Stores information identifying a unique config hierarchy
		/// </summary>
		class ConfigHierarchyKey
		{
			/// <summary>
			/// The hierarchy type
			/// </summary>
			public ConfigHierarchyType Type;

			/// <summary>
			/// The project directory to read from
			/// </summary>
			public DirectoryReference ProjectDir;

			/// <summary>
			/// Which platform-specific files to read
			/// </summary>
			public UnrealTargetPlatform Platform;

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="Type">The hierarchy type</param>
			/// <param name="ProjectDir">The project directory to read from</param>
			/// <param name="Platform">Which platform-specific files to read</param>
			public ConfigHierarchyKey(ConfigHierarchyType Type, DirectoryReference ProjectDir, UnrealTargetPlatform Platform)
			{
				this.Type = Type;
				this.ProjectDir = ProjectDir;
				this.Platform = Platform;
			}

			/// <summary>
			/// Test whether this key is equal to another object
			/// </summary>
			/// <param name="Other">The object to compare against</param>
			/// <returns>True if the objects match, false otherwise</returns>
			public override bool Equals(object Other)
			{
				ConfigHierarchyKey OtherKey = Other as ConfigHierarchyKey;
				return (OtherKey != null && OtherKey.Type == Type && OtherKey.ProjectDir == ProjectDir && OtherKey.Platform == Platform);
			}

			/// <summary>
			/// Returns a stable hash of this object
			/// </summary>
			/// <returns>Hash value for this object</returns>
			public override int GetHashCode()
			{
				return ((ProjectDir != null) ? ProjectDir.GetHashCode() : 0) + ((int)Type * 123) + (Platform.GetHashCode() * 345);
			}
		}

		/// <summary>
		/// Cache of individual config files
		/// </summary>
		static Dictionary<FileReference, ConfigFile> LocationToConfigFile = new Dictionary<FileReference, ConfigFile>();

		/// <summary>
		/// Cache of config hierarchies by project
		/// </summary>
		static Dictionary<ConfigHierarchyKey, ConfigHierarchy> HierarchyKeyToHierarchy = new Dictionary<ConfigHierarchyKey, ConfigHierarchy>();

		/// <summary>
		/// Cache of config fields by type
		/// </summary>
		static Dictionary<Type, List<ConfigField>> TypeToConfigFields = new Dictionary<Type, List<ConfigField>>();

		/// <summary>
		/// Attempts to read a config file (or retrieve it from the cache)
		/// </summary>
		/// <param name="Location">Location of the file to read</param>
		/// <param name="ConfigFile">On success, receives the parsed config file</param>
		/// <returns>True if the file exists and was read, false otherwise</returns>
		internal static bool TryReadFile(FileReference Location, out ConfigFile ConfigFile)
		{
			lock (LocationToConfigFile)
			{
				if (!LocationToConfigFile.TryGetValue(Location, out ConfigFile))
				{
					if (FileReference.Exists(Location))
					{
						ConfigFile = new ConfigFile(Location);
					}

					if (ConfigFile != null)
					{
						LocationToConfigFile.Add(Location, ConfigFile);
					}
				}
			}

			return ConfigFile != null;
		}

		/// <summary>
		/// Reads a config hierarchy (or retrieve it from the cache)
		/// </summary>
		/// <param name="Type">The type of hierarchy to read</param>
		/// <param name="ProjectDir">The project directory to read the hierarchy for</param>
		/// <param name="Platform">Which platform to read platform-specific config files for</param>
		/// <returns>The requested config hierarchy</returns>
		public static ConfigHierarchy ReadHierarchy(ConfigHierarchyType Type, DirectoryReference ProjectDir, UnrealTargetPlatform Platform)
		{
			// Get the key to use for the cache. It cannot be null, so we use the engine directory if a project directory is not given.
			ConfigHierarchyKey Key = new ConfigHierarchyKey(Type, ProjectDir, Platform);

			// Try to get the cached hierarchy with this key
			ConfigHierarchy Hierarchy;
			lock (HierarchyKeyToHierarchy)
			{
				if (!HierarchyKeyToHierarchy.TryGetValue(Key, out Hierarchy))
				{
					// Find all the input files
					List<ConfigFile> Files = new List<ConfigFile>();
					foreach (FileReference IniFileName in ConfigHierarchy.EnumerateConfigFileLocations(Type, ProjectDir, Platform))
					{
						ConfigFile File;
						if (TryReadFile(IniFileName, out File))
						{
							Files.Add(File);
						}
					}

					// Handle command line overrides
					string[] CmdLine = Environment.GetCommandLineArgs();
					string IniConfigArgPrefix = "-ini:" + Enum.GetName(typeof(ConfigHierarchyType), Type) + ":";
					foreach (string CmdLineArg in CmdLine)
					{
						if (CmdLineArg.StartsWith(IniConfigArgPrefix))
						{
							ConfigFile OverrideFile = new ConfigFile(CmdLineArg.Substring(IniConfigArgPrefix.Length));
							Files.Add(OverrideFile);
						}
					}

					// Create the hierarchy
					Hierarchy = new ConfigHierarchy(Files);
					HierarchyKeyToHierarchy.Add(Key, Hierarchy);
				}
			}
			return Hierarchy;
		}
	
		/// <summary>
		/// Gets a list of ConfigFields for the given type
		/// </summary>
		/// <param name="TargetObjectType">Type to get configurable fields for</param>
		/// <returns>List of config fields for the given type</returns>
		static List<ConfigField> FindConfigFieldsForType(Type TargetObjectType)
		{
			List<ConfigField> Fields;
			lock(TypeToConfigFields)
			{
				if (!TypeToConfigFields.TryGetValue(TargetObjectType, out Fields))
				{
					Fields = new List<ConfigField>();
					if(TargetObjectType.BaseType != null)
					{
						Fields.AddRange(FindConfigFieldsForType(TargetObjectType.BaseType));
					}
					foreach (FieldInfo FieldInfo in TargetObjectType.GetFields(BindingFlags.Instance | BindingFlags.GetField | BindingFlags.GetProperty | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
					{
						IEnumerable<ConfigFileAttribute> Attributes = FieldInfo.GetCustomAttributes<ConfigFileAttribute>();
						foreach (ConfigFileAttribute Attribute in Attributes)
						{
							// Copy the field 
							ConfigField Setter = new ConfigField();
							Setter.FieldInfo = FieldInfo;
							Setter.Attribute = Attribute;

							// Check if the field type implements ICollection<>. If so, we can take multiple values.
							foreach (Type InterfaceType in FieldInfo.FieldType.GetInterfaces())
							{
								if (InterfaceType.IsGenericType && InterfaceType.GetGenericTypeDefinition() == typeof(ICollection<>))
								{
									MethodInfo MethodInfo = InterfaceType.GetRuntimeMethod("Add", new Type[] { InterfaceType.GenericTypeArguments[0] });
									Setter.AddElement = (Target, Value) => { MethodInfo.Invoke(Setter.FieldInfo.GetValue(Target), new object[] { Value }); };
									Setter.ElementType = InterfaceType.GenericTypeArguments[0];
									break;
								}
							}

							// Add it to the output list
							Fields.Add(Setter);
						}
					}
					TypeToConfigFields.Add(TargetObjectType, Fields);
				}
			}
			return Fields;
		}

		/// <summary>
		/// Read config settings for the given object
		/// </summary>
		/// <param name="ProjectDir">Path to the project directory</param>
		/// <param name="Platform">The platform being built</param>
		/// <param name="TargetObject">Object to receive the settings</param>
		public static void ReadSettings(DirectoryReference ProjectDir, UnrealTargetPlatform Platform, object TargetObject)
		{
			ReadSettings(ProjectDir, Platform, TargetObject, null);
		}

		/// <summary>
		/// Read config settings for the given object
		/// </summary>
		/// <param name="ProjectDir">Path to the project directory</param>
		/// <param name="Platform">The platform being built</param>
		/// <param name="TargetObject">Object to receive the settings</param>
		/// <param name="Tracker">Tracks the set of config values that were retrieved. May be null.</param>
		internal static void ReadSettings(DirectoryReference ProjectDir, UnrealTargetPlatform Platform, object TargetObject, ConfigValueTracker Tracker)
		{
			List<ConfigField> Fields = FindConfigFieldsForType(TargetObject.GetType());
			foreach(ConfigField Field in Fields)
			{
				// Read the hierarchy listed
				ConfigHierarchy Hierarchy = ReadHierarchy(Field.Attribute.ConfigType, ProjectDir, Platform);

				// Get the key name
				string KeyName = Field.Attribute.KeyName ?? Field.FieldInfo.Name;

				// Get the value(s) associated with this key
				IReadOnlyList<string> Values;
				Hierarchy.TryGetValues(Field.Attribute.SectionName, KeyName, out Values);

				// Parse the values from the config files and update the target object
				if (Field.AddElement == null)
				{
					if(Values != null && Values.Count == 1)
					{
						object Value;
						if(TryParseValue(Values[0], Field.FieldInfo.FieldType, out Value))
						{
							Field.FieldInfo.SetValue(TargetObject, Value);
						}
					}
				}
				else
				{
					if(Values != null)
					{
						foreach(string Item in Values)
						{
							object Value;
							if(TryParseValue(Item, Field.ElementType, out Value))
							{
								Field.AddElement(TargetObject, Value);
							}
						}
					}
				}

				// Save the dependency
				if (Tracker != null)
				{
					Tracker.Add(Field.Attribute.ConfigType, ProjectDir, Platform, Field.Attribute.SectionName, KeyName, Values);
				}
			}
		}

		/// <summary>
		/// Attempts to parse the given text into an object which matches a specific field type
		/// </summary>
		/// <param name="Text">The text to parse</param>
		/// <param name="FieldType">The type of field to parse</param>
		/// <param name="Value">If successful, a value of type 'FieldType'</param>
		/// <returns>True if the value could be parsed, false otherwise</returns>
		public static bool TryParseValue(string Text, Type FieldType, out object Value)
		{
			if(FieldType == typeof(string))
			{
				Value = Text;
				return true;
			}
			else if(FieldType == typeof(bool))
			{
				bool BoolValue;
				if(ConfigHierarchy.TryParse(Text, out BoolValue))
				{
					Value = BoolValue;
					return true;
				}
				else
				{
					Value = null;
					return false;
				}
			}
			else if(FieldType == typeof(int))
			{
				int IntValue;
				if(ConfigHierarchy.TryParse(Text, out IntValue))
				{
					Value = IntValue;
					return true;
				}
				else
				{
					Value = null;
					return false;
				}
			}
			else if(FieldType == typeof(float))
			{
				float FloatValue;
				if(ConfigHierarchy.TryParse(Text, out FloatValue))
				{
					Value = FloatValue;
					return true;
				}
				else
				{
					Value = null;
					return false;
				}
			}
			else if(FieldType == typeof(double))
			{
				double DoubleValue;
				if(ConfigHierarchy.TryParse(Text, out DoubleValue))
				{
					Value = DoubleValue;
					return true;
				}
				else
				{
					Value = null;
					return false;
				}
			}
			else if(FieldType == typeof(Guid))
			{
				Guid GuidValue;
				if(ConfigHierarchy.TryParse(Text, out GuidValue))
				{
					Value = GuidValue;
					return true;
				}
				else
				{
					Value = null;
					return false;
				}
			}
			else if(FieldType.IsEnum)
			{
				try
				{
					Value = Enum.Parse(FieldType, Text);
					return true;
				}
				catch
				{
					Value = null;
					return false;
				}
			}
			else if(FieldType.GetGenericTypeDefinition() == typeof(Nullable<>))
			{
				return TryParseValue(Text, FieldType.GetGenericArguments()[0], out Value);
			}
			else
			{
				throw new Exception("Unsupported type for [ConfigFile] attribute");
			}
		}
	}
}
