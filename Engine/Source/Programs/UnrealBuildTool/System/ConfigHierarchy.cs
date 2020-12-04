// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;
using System.IO;

namespace UnrealBuildTool
{
	/// <summary>
	/// Types of config file hierarchy
	/// </summary>
	public enum ConfigHierarchyType
	{
		/// <summary>
		/// BaseGame.ini, DefaultGame.ini, etc...
		/// </summary>
		Game,

		/// <summary>
		/// BaseEngine.ini, DefaultEngine.ini, etc...
		/// </summary>
		Engine,

		/// <summary>
		/// BaseEditorPerProjectUserSettings.ini, DefaultEditorPerProjectUserSettings.ini, etc..
		/// </summary>
		EditorPerProjectUserSettings,

		/// <summary>
		/// BaseEncryption.ini, DefaultEncryption.ini, etc..
		/// </summary>
		Encryption,

		/// <summary>
		/// BaseCrypto.ini, DefaultCrypto.ini, etc..
		/// </summary>
		Crypto,

		/// <summary>
		/// BaseEditorSettings.ini, DefaultEditorSettings.ini, etc...
		/// </summary>
		EditorSettings,

		/// <summary>
		/// BaseInstallBundle.ini, DefaultInstallBundle.ini, etc...
		/// </summary>
		InstallBundle,
	}

	/// <summary>
	/// Stores a set of merged key/value pairs for a config section
	/// </summary>
	public class ConfigHierarchySection
	{
		/// <summary>
		/// Map of key names to their values
		/// </summary>
		Dictionary<string, List<string>> KeyToValue = new Dictionary<string, List<string>>(StringComparer.InvariantCultureIgnoreCase);

		/// <summary>
		/// Construct a merged config section from the given per-file config sections
		/// </summary>
		/// <param name="FileSections">Config sections from individual files</param>
		internal ConfigHierarchySection(IEnumerable<ConfigFileSection> FileSections)
		{
			foreach(ConfigFileSection FileSection in FileSections)
			{
				foreach(ConfigLine Line in FileSection.Lines)
				{
                    if (Line.Action == ConfigLineAction.RemoveKey)
                    {
                        KeyToValue.Remove(Line.Key);
                        continue;
                    }

                    // Find or create the values for this key
                    List<string> Values;

					if(KeyToValue.TryGetValue(Line.Key, out Values))
					{
						// Update the existing list
						if(Line.Action == ConfigLineAction.Set)
						{
							Values.Clear();
							Values.Add(Line.Value);
						}
						else if(Line.Action == ConfigLineAction.Add)
						{
							Values.Add(Line.Value);
						}
                        else if (Line.Action == ConfigLineAction.RemoveKeyValue)
						{
							Values.RemoveAll(x => x.Equals(Line.Value, StringComparison.InvariantCultureIgnoreCase));
						}
					}
					else
					{
						// If it's a set or add action, create and add a new list
						if(Line.Action == ConfigLineAction.Set || Line.Action == ConfigLineAction.Add)
						{
							Values = new List<string>();
							Values.Add(Line.Value);
							KeyToValue.Add(Line.Key, Values);
						}
					}
				}
			}
		}

		/// <summary>
		/// Returns a list of key names
		/// </summary>
		public IEnumerable<string> KeyNames
		{
			get { return KeyToValue.Keys; }
		}

		/// <summary>
		/// Tries to find the value for a given key
		/// </summary>
		/// <param name="KeyName">The key name to search for</param>
		/// <param name="Value">On success, receives the corresponding value</param>
		/// <returns>True if the key was found, false otherwise</returns>
		public bool TryGetValue(string KeyName, out string Value)
		{
			List<string> ValuesList;
			if(KeyToValue.TryGetValue(KeyName, out ValuesList) && ValuesList.Count > 0)
			{
				Value = ValuesList[0];
				return true;
			}
			else
			{
				Value = null;
				return false;
			}
		}

		/// <summary>
		/// Tries to find the values for a given key
		/// </summary>
		/// <param name="KeyName">The key name to search for</param>
		/// <param name="Values">On success, receives a list of the corresponding values</param>
		/// <returns>True if the key was found, false otherwise</returns>
		public bool TryGetValues(string KeyName, out IReadOnlyList<string> Values)
		{
			List<string> ValuesList;
			if(KeyToValue.TryGetValue(KeyName, out ValuesList))
			{
				Values = ValuesList;
				return true;
			}
			else
			{
				Values = null;
				return false;
			}
		}
	}

	/// <summary>
	/// Encapsulates a hierarchy of config files, merging sections from them together on request 
	/// </summary>
	public class ConfigHierarchy
	{
		/// <summary>
		/// Array of 
		/// </summary>
		ConfigFile[] Files;

		/// <summary>
		/// Cache of requested config sections
		/// </summary>
		Dictionary<string, ConfigHierarchySection> NameToSection = new Dictionary<string, ConfigHierarchySection>(StringComparer.InvariantCultureIgnoreCase);

        /// <summary>
        /// Lock for NameToSection
        /// </summary>
        System.Threading.ReaderWriterLockSlim NameToSectionLock = new System.Threading.ReaderWriterLockSlim();

        /// <summary>
        /// Construct a config hierarchy from the given files
        /// </summary>
        /// <param name="Files">Set of files to include (in order)</param>
        public ConfigHierarchy(IEnumerable<ConfigFile> Files)
		{
			this.Files = Files.ToArray();
		}

		/// <summary>
		/// Names of all sections in all config files
		/// </summary>
		/// <returns></returns>
		public HashSet<string> SectionNames
		{
			get
			{
				HashSet<string> Result = new HashSet<string>();
				foreach (ConfigFile File in Files)
				{
					foreach (string SectionName in File.SectionNames)
					{
						if ( !Result.Contains(SectionName) )
						{
							Result.Add(SectionName);
						}
					}
				}
				return Result;
			}
		}

		/// <summary>
		/// Finds a config section with the given name
		/// </summary>
		/// <param name="SectionName">Name of the section to look for</param>
		/// <returns>The merged config section</returns>
		public ConfigHierarchySection FindSection(string SectionName)
		{
            ConfigHierarchySection Section;
            try
            {
                // Acquire a read lock and do a quick check for the config section
                NameToSectionLock.EnterUpgradeableReadLock();
                if (!NameToSection.TryGetValue(SectionName, out Section))
                {
                    try
                    {
                        // Acquire a write lock and add the config section if another thread didn't just complete it
                        NameToSectionLock.EnterWriteLock();
                        if (!NameToSection.TryGetValue(SectionName, out Section))
                        {
                            // Find all the raw sections from the file hierarchy
                            List<ConfigFileSection> RawSections = new List<ConfigFileSection>();
                            foreach (ConfigFile File in Files)
                            {
                                ConfigFileSection RawSection;
                                if (File.TryGetSection(SectionName, out RawSection))
                                {
                                    RawSections.Add(RawSection);
                                }
                            }

                            // Merge them together and add it to the cache
                            Section = new ConfigHierarchySection(RawSections);
                            NameToSection.Add(SectionName, Section);
                        }                        
                    }
                    finally
                    {
                        NameToSectionLock.ExitWriteLock();
                    }
                }
            }
            finally
            {
                NameToSectionLock.ExitUpgradeableReadLock();
            }
            return Section;
        }

		/// <summary>
		/// Legacy function for ease of transition from ConfigCacheIni to ConfigHierarchy. Gets a bool with the given key name.
		/// </summary>
		/// <param name="SectionName">Section name</param>
		/// <param name="KeyName">Key name</param>
		/// <param name="Value">Value associated with the specified key. If the key has more than one value, only the first one is returned</param>
		/// <returns>True if the key exists</returns>
		public bool GetBool(string SectionName, string KeyName, out bool Value)
		{
			return TryGetValue(SectionName, KeyName, out Value);
		}

		/// <summary>
		/// Legacy function for ease of transition from ConfigCacheIni to ConfigHierarchy. Gets an array with the given key name, returning null on failure.
		/// </summary>
		/// <param name="SectionName">Section name</param>
		/// <param name="KeyName">Key name</param>
		/// <param name="Values">Value associated with the specified key. If the key has more than one value, only the first one is returned</param>
		/// <returns>True if the key exists</returns>
		public bool GetArray(string SectionName, string KeyName, out List<string> Values)
		{
			IReadOnlyList<string> ValuesEnumerable;
			if(TryGetValues(SectionName, KeyName, out ValuesEnumerable))
			{
				Values = ValuesEnumerable.ToList();
				return true;
			}
			else
			{
				Values = null;
				return false;
			}
		}

		/// <summary>
		/// Legacy function for ease of transition from ConfigCacheIni to ConfigHierarchy. Gets a string with the given key name, returning an empty string on failure.
		/// </summary>
		/// <param name="SectionName">Section name</param>
		/// <param name="KeyName">Key name</param>
		/// <param name="Value">Value associated with the specified key. If the key has more than one value, only the first one is returned</param>
		/// <returns>True if the key exists</returns>
		public bool GetString(string SectionName, string KeyName, out string Value)
		{
			if(TryGetValue(SectionName, KeyName, out Value))
			{
				return true;
			}
			else
			{
				Value = "";
				return false;
			}
		}

		/// <summary>
		/// Legacy function for ease of transition from ConfigCacheIni to ConfigHierarchy. Gets an int with the given key name.
		/// </summary>
		/// <param name="SectionName">Section name</param>
		/// <param name="KeyName">Key name</param>
		/// <param name="Value">Value associated with the specified key. If the key has more than one value, only the first one is returned</param>
		/// <returns>True if the key exists</returns>
		public bool GetInt32(string SectionName, string KeyName, out int Value)
		{
			return TryGetValue(SectionName, KeyName, out Value);
		}

		/// <summary>
		/// Gets a single string value associated with the specified key.
		/// </summary>
		/// <param name="SectionName">Section name</param>
		/// <param name="KeyName">Key name</param>
		/// <param name="Value">Value associated with the specified key. If the key has more than one value, only the first one is returned</param>
		/// <returns>True if the key exists</returns>
		public bool TryGetValue(string SectionName, string KeyName, out string Value)
		{
			return FindSection(SectionName).TryGetValue(KeyName, out Value);
		}

		/// <summary>
		/// Gets a single bool value associated with the specified key.
		/// </summary>
		/// <param name="SectionName">Section name</param>
		/// <param name="KeyName">Key name</param>
		/// <param name="Value">Value associated with the specified key. If the key has more than one value, only the first one is returned</param>
		/// <returns>True if the key exists</returns>
		public bool TryGetValue(string SectionName, string KeyName, out bool Value)
		{
			string Text;
			if(!TryGetValue(SectionName, KeyName, out Text))
			{
				Value = false;
				return false;
			}
			return TryParse(Text, out Value);
		}

		/// <summary>
		/// Gets a single Int32 value associated with the specified key.
		/// </summary>
		/// <param name="SectionName">Section name</param>
		/// <param name="KeyName">Key name</param>
		/// <param name="Value">Value associated with the specified key. If the key has more than one value, only the first one is returned</param>
		/// <returns>True if the key exists</returns>
		public bool TryGetValue(string SectionName, string KeyName, out int Value)
		{
			string Text;
			if(!TryGetValue(SectionName, KeyName, out Text))
			{
				Value = 0;
				return false;
			}
			return TryParse(Text, out Value);
		}

		/// <summary>
		/// Gets a single GUID value associated with the specified key.
		/// </summary>
		/// <param name="SectionName">Section name</param>
		/// <param name="KeyName">Key name</param>
		/// <param name="Value">Value associated with the specified key. If the key has more than one value, only the first one is returned</param>
		/// <returns>True if the key exists</returns>
		public bool TryGetValue(string SectionName, string KeyName, out Guid Value)
		{
			string Text;
			if(!TryGetValue(SectionName, KeyName, out Text))
			{
				Value = Guid.Empty;
				return false;
			}
			return TryParse(Text, out Value);
		}

		/// <summary>
		/// Gets a single-precision floating point value associated with the specified key.
		/// </summary>
		/// <param name="SectionName">Section name</param>
		/// <param name="KeyName">Key name</param>
		/// <param name="Value">Value associated with the specified key. If the key has more than one value, only the first one is returned</param>
		/// <returns>True if the key exists</returns>
		public bool TryGetValue(string SectionName, string KeyName, out float Value)
		{
			string Text;
			if(!TryGetValue(SectionName, KeyName, out Text))
			{
				Value = 0;
				return false;
			}
			return TryParse(Text, out Value);
		}

		/// <summary>
		/// Gets a double-precision floating point value associated with the specified key.
		/// </summary>
		/// <param name="SectionName">Section name</param>
		/// <param name="KeyName">Key name</param>
		/// <param name="Value">Value associated with the specified key. If the key has more than one value, only the first one is returned</param>
		/// <returns>True if the key exists</returns>
		public bool TryGetValue(string SectionName, string KeyName, out double Value)
		{
			string Text;
			if(!TryGetValue(SectionName, KeyName, out Text))
			{
				Value = 0;
				return false;
			}
			return TryParse(Text, out Value);
		}

		/// <summary>
		/// Gets all values associated with the specified key
		/// </summary>
		/// <param name="SectionName">Section where the key is located</param>
		/// <param name="KeyName">Key name</param>
		/// <param name="Values">Copy of the list containing all values associated with the specified key</param>
		/// <returns>True if the key exists</returns>
		public bool TryGetValues(string SectionName, string KeyName, out IReadOnlyList<string> Values)
		{
			return FindSection(SectionName).TryGetValues(KeyName, out Values);
		}

		/// <summary>
		/// Parse a string as a boolean value
		/// </summary>
		/// <param name="Text">The text to parse</param>
		/// <param name="Value">The parsed value, if successful</param>
		/// <returns>True if the text was parsed, false otherwise</returns>
		static public bool TryParse(string Text, out bool Value)
		{
			// C# Boolean type expects "False" or "True" but since we're not case sensitive, we need to suppor that manually
			if (Text == "1" || Text.Equals("true", StringComparison.InvariantCultureIgnoreCase))
			{
				Value = true;
				return true;
			}
			else if (Text == "0" || Text.Equals("false", StringComparison.InvariantCultureIgnoreCase))
			{
				Value = false;
				return true;
			}
			else
			{
				Value = false;
				return false;
			}
		}

		/// <summary>
		/// Parse a string as an integer value
		/// </summary>
		/// <param name="Text">The text to parse</param>
		/// <param name="Value">The parsed value, if successful</param>
		/// <returns>True if the text was parsed, false otherwise</returns>
		static public bool TryParse(string Text, out int Value)
		{
			return Int32.TryParse(Text, out Value);
		}

		/// <summary>
		/// Parse a string as a GUID value
		/// </summary>
		/// <param name="Text">The text to parse</param>
		/// <param name="Value">The parsed value, if successful</param>
		/// <returns>True if the text was parsed, false otherwise</returns>
		public static bool TryParse(string Text, out Guid Value)
		{
			if (Text.Contains("A=") && Text.Contains("B=") && Text.Contains("C=") && Text.Contains("D="))
			{
				char[] Separators = new char[] { '(', ')', '=', ',', ' ', 'A', 'B', 'C', 'D' };
				string[] ComponentValues = Text.Split(Separators, StringSplitOptions.RemoveEmptyEntries);
				if (ComponentValues.Length == 4)
				{
					StringBuilder HexString = new StringBuilder();
					for (int ComponentIndex = 0; ComponentIndex < 4; ComponentIndex++)
					{
						int IntegerValue;
						if(!Int32.TryParse(ComponentValues[ComponentIndex], out IntegerValue))
						{
							Value = Guid.Empty;
							return false;
						}
						HexString.Append(IntegerValue.ToString("X8"));
					}
					Text = HexString.ToString();
				}
			}
			return Guid.TryParseExact(Text, "N", out Value);
		}

		/// <summary>
		/// Parse a string as a single-precision floating point value
		/// </summary>
		/// <param name="Text">The text to parse</param>
		/// <param name="Value">The parsed value, if successful</param>
		/// <returns>True if the text was parsed, false otherwise</returns>
		public static bool TryParse(string Text, out float Value)
		{
			if(Text.EndsWith("f"))
			{
				return Single.TryParse(Text.Substring(0, Text.Length - 1), out Value);
			}
			else
			{
				return Single.TryParse(Text, out Value);
			}
		}

		/// <summary>
		/// Parse a string as a double-precision floating point value
		/// </summary>
		/// <param name="Text">The text to parse</param>
		/// <param name="Value">The parsed value, if successful</param>
		/// <returns>True if the text was parsed, false otherwise</returns>
		public static bool TryParse(string Text, out double Value)
		{
			if(Text.EndsWith("f"))
			{
				return Double.TryParse(Text.Substring(0, Text.Length - 1), out Value);
			}
			else
			{
				return Double.TryParse(Text, out Value);
			}
		}

		/// <summary>
		/// Attempts to parse the given line as a UE4 config object (eg. (Name="Foo",Number=1234)).
		/// </summary>
		/// <param name="Line">Line of text to parse</param>
		/// <param name="Properties">Receives key/value pairs for the config object</param>
		/// <returns>True if an object was parsed, false otherwise</returns>
		public static bool TryParse(string Line, out Dictionary<string, string> Properties)
		{
			// Convert the string to a zero-terminated array, to make parsing easier.
			char[] Chars = new char[Line.Length + 1];
			Line.CopyTo(0, Chars, 0, Line.Length);

			// Get the opening paren
			int Idx = 0;
			while(Char.IsWhiteSpace(Chars[Idx]))
			{
				Idx++;
			}
			if(Chars[Idx] != '(')
			{
				Properties = null;
				return false;
			}

			// Read to the next token
			Idx++;
			while(Char.IsWhiteSpace(Chars[Idx]))
			{
				Idx++;
			}

			// Create the dictionary to receive the new properties
			Dictionary<string, string> NewProperties = new Dictionary<string, string>();

			// Read a sequence of key/value pairs
			StringBuilder Value = new StringBuilder();
			if(Chars[Idx] != ')')
			{
				for (;;)
				{
					// Find the end of the name
					int NameIdx = Idx;
					while(Char.IsLetterOrDigit(Chars[Idx]) || Chars[Idx] == '_')
					{
						Idx++;
					}
					if(Idx == NameIdx)
					{
						Properties = null;
						return false;
					}

					// Extract the key string, and make sure it hasn't already been added
					string Key = new string(Chars, NameIdx, Idx - NameIdx);
					if(NewProperties.ContainsKey(Key))
					{
						Properties = null;
						return false;
					}

					// Consume the equals character
					while(Char.IsWhiteSpace(Chars[Idx]))
					{
						Idx++;
					}
					if(Chars[Idx] != '=')
					{
						Properties = null;
						return false;
					}

					// Move to the value
					Idx++;
					while (Char.IsWhiteSpace(Chars[Idx]))
					{
						Idx++;
					}

					// Parse the value
					Value.Clear();
					if (Char.IsLetterOrDigit(Chars[Idx]) || Chars[Idx] == '_' || Chars[Idx] == '-')
					{
						while (Char.IsLetterOrDigit(Chars[Idx]) || Chars[Idx] == '_' || Chars[Idx] == '-' || Chars[Idx] == '.')
						{
							Value.Append(Chars[Idx]);
							Idx++;
						}
					}
					else if (Chars[Idx] == '\"')
					{
						Idx++;
						for(; Chars[Idx] != '\"'; Idx++)
						{
							if (Chars[Idx] == '\0')
							{
								Properties = null;
								return false;
							}
							else
							{
								Value.Append(Chars[Idx]);
							}
						}
						Idx++;
					}
					else if (Chars[Idx] == '(')
					{
						Value.Append(Chars[Idx++]);

						bool bInQuotes = false;
						for (int Nesting = 1; Nesting > 0; Idx++)
						{
							if (Chars[Idx] == '\0')
							{
								Properties = null;
								return false;
							}
							else if (Chars[Idx] == '(' && !bInQuotes)
							{
								Nesting++;
							}
							else if (Chars[Idx] == ')' && !bInQuotes)
							{
								Nesting--;
							}
							else if (Chars[Idx] == '\"' || Chars[Idx] == '\'')
							{
								bInQuotes ^= true;
							}
							Value.Append(Chars[Idx]);
						}
					}
					else
					{
						Properties = null;
						return false;
					}

					// Extract the value string
					NewProperties[Key] = Value.ToString();

					// Move to the separator
					while(Char.IsWhiteSpace(Chars[Idx]))
					{
						Idx++;
					}
					if(Chars[Idx] == ')')
					{
						break;
					}
					if(Chars[Idx] != ',')
					{
						Properties = null;
						return false;
					}

					// Move to the next field
					Idx++;
					while (Char.IsWhiteSpace(Chars[Idx]))
					{
						Idx++;
					}
				}
			}

			// Make sure we're at the end of the string
			Idx++;
			while(Char.IsWhiteSpace(Chars[Idx]))
			{
				Idx++;
			}
			if(Chars[Idx] != '\0')
			{
				Properties = null;
				return false;
			}

			Properties = NewProperties;
			return true;
		}


		class ConfigLayerExpansion
		{
			// a set of replacements from the source file to possible other files
			public string Before1 = null;
			public string After1 = null;
			public string Before2 = null;
			public string After2 = null;
		};

		static string [] ConfigLayers =
		{
			// Engine/Base.ini
			"{ENGINE}/Config/Base.ini",
			// Engine/Base*.ini
 			"{ENGINE}/Config/Base{TYPE}.ini",
			// Engine/Platform/BasePlatform*.ini
			"{ENGINE}/Config/{PLATFORM}/Base{PLATFORM}{TYPE}.ini",
			// Project/Default*.ini
			"{PROJECT}/Config/Default{TYPE}.ini",
			// Engine/Platform/Platform*.ini
			"{ENGINE}/Config/{PLATFORM}/{PLATFORM}{TYPE}.ini",
			// Project/Platform/Platform*.ini
			"{PROJECT}/Config/{PLATFORM}/{PLATFORM}{TYPE}.ini",

			// UserSettings/.../User*.ini
			"{USERSETTINGS}/Unreal Engine/Engine/Config/User{TYPE}.ini",
			// UserDir/.../User*.ini
			"{USER}/Unreal Engine/Engine/Config/User{TYPE}.ini",
			// Project/User*.ini
			"{PROJECT}/User{TYPE}.ini",
		};

		static ConfigLayerExpansion[] ConfigLayerExpansions =
		{
			// The base expansion (ie, no expansion)
			new ConfigLayerExpansion { }, 
			// Restricted Locations
			new ConfigLayerExpansion { Before1 = "{ENGINE}/", After1 = "{ENGINE}/Restricted/NotForLicensees/",	Before2 = "{PROJECT}/Config/", After2 = "{RESTRICTEDPROJECT_NFL}/Config/" },
			new ConfigLayerExpansion { Before1 = "{ENGINE}/", After1 = "{ENGINE}/Restricted/NoRedist/",			Before2 = "{PROJECT}/Config/", After2 = "{RESTRICTEDPROJECT_NR}/Config/" },
			// Platform Extensions
			new ConfigLayerExpansion { Before1 = "{ENGINE}/Config/{PLATFORM}/", After1 = "{EXTENGINE}/Config/",	Before2 = "{PROJECT}/Config/{PLATFORM}/", After2 = "{EXTPROJECT}/Config/" },
			// Platform Extensions in Restricted Locations
			new ConfigLayerExpansion { Before1 = "{ENGINE}/Config/{PLATFORM}/", After1 = "{ENGINE}/Restricted/NotForLicensees/Platforms/{PLATFORM}/Config/",   Before2 = "{PROJECT}/Config/{PLATFORM}/", After2 = "{RESTRICTEDPROJECT_NFL}/Platforms/{PLATFORM}/Config/" },
			new ConfigLayerExpansion { Before1 = "{ENGINE}/Config/{PLATFORM}/", After1 = "{ENGINE}/Restricted/NoRedist/Platforms/{PLATFORM}/Config/",          Before2 = "{PROJECT}/Config/{PLATFORM}/", After2 = "{RESTRICTEDPROJECT_NR}/Platforms/{PLATFORM}/Config/" },
		};

		// Match FPlatformProcess::UserDir()
		private static string GetUserDir()
		{
			// Some user accounts (eg. SYSTEM on Windows) don't have a home directory. Ignore them if Environment.GetFolderPath() returns an empty string.
			string PersonalFolder = Environment.GetFolderPath(Environment.SpecialFolder.Personal);
			string PersonalConfigFolder = null;
			if (!String.IsNullOrEmpty(PersonalFolder))
			{
				PersonalConfigFolder = PersonalFolder;
				if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Mac || Environment.OSVersion.Platform == PlatformID.Unix)
				{
					PersonalConfigFolder = System.IO.Path.Combine(PersonalConfigFolder, "Documents");
				}
			}

			return PersonalConfigFolder;
		}


		private static string PerformBasicReplacements(string InString, string BaseIniName)
		{
			string OutString = InString.Replace("{TYPE}", BaseIniName);
			OutString = OutString.Replace("{USERSETTINGS}", Utils.GetUserSettingDirectory().FullName);
			OutString = OutString.Replace("{USER}", GetUserDir());

			return OutString;
		}


		private static string PerformExpansionReplacements(ConfigLayerExpansion Expansion, string InString)
		{
			// if there's replacement to do, the output is just the output
			if (Expansion.Before1 == null)
			{
				return InString;
			}

			// if nothing to replace, then skip it entirely
			if (!InString.Contains(Expansion.Before1) && (Expansion.Before2 == null || !InString.Contains(Expansion.Before2)))
			{
				return null;
			}

			// replace the directory bits
			string OutString = InString.Replace(Expansion.Before1, Expansion.After1);
			if (Expansion.Before2 != null)
			{
				OutString = OutString.Replace(Expansion.Before2, Expansion.After2);
			}
			return OutString;
		}

		private static string PerformFinalExpansions(string InString, string PlatformName, DirectoryReference ProjectDir)
		{
			string PlatformExtensionEngineConfigDir = DirectoryReference.Combine(UnrealBuildTool.EngineDirectory, "Platforms", PlatformName).FullName;

			string OutString = InString.Replace("{ENGINE}", UnrealBuildTool.EngineDirectory.FullName);
			OutString = OutString.Replace("{EXTENGINE}", PlatformExtensionEngineConfigDir);
			OutString = OutString.Replace("{PLATFORM}", PlatformName);

			if (ProjectDir != null)
			{
				DirectoryReference NFLDir;
				DirectoryReference NRDir;
				if (ProjectDir.IsUnderDirectory(UnrealBuildTool.EngineDirectory))
				{
					string RelativeDir = ProjectDir.MakeRelativeTo(UnrealBuildTool.EngineDirectory);
					NFLDir = DirectoryReference.Combine(UnrealBuildTool.EngineDirectory, "Restricted/NotForLicensees", RelativeDir);
					NRDir = DirectoryReference.Combine(UnrealBuildTool.EngineDirectory, "Restricted/NoRedist", RelativeDir);
				}
				else
				{
					NFLDir = DirectoryReference.Combine(ProjectDir, "Restricted/NotForLicensees");
					NRDir = DirectoryReference.Combine(ProjectDir, "Restricted/NoRedist");
				}
				string PlatformExtensionProjectConfigDir = DirectoryReference.Combine(ProjectDir, "Platforms", PlatformName).FullName;

				OutString = OutString.Replace("{PROJECT}", ProjectDir.FullName);
				OutString = OutString.Replace("{EXTPROJECT}", PlatformExtensionProjectConfigDir);
				OutString = OutString.Replace("{RESTRICTEDPROJECT_NFL}", NFLDir.FullName);
				OutString = OutString.Replace("{RESTRICTEDPROJECT_NR}", NRDir.FullName);
			}

			return OutString;
		}

		/// <summary>
		/// Returns a list of INI filenames for the given project
		/// </summary>
		public static IEnumerable<FileReference> EnumerateConfigFileLocations(ConfigHierarchyType Type, DirectoryReference ProjectDir, UnrealTargetPlatform Platform)
		{
			string BaseIniName = Enum.GetName(typeof(ConfigHierarchyType), Type);
			string PlatformName = GetIniPlatformName(Platform);

			foreach (string Layer in ConfigLayers)
			{
				bool bHasPlatformTag = Layer.Contains("{PLATFORM}");
				bool bHasProjectTag = Layer.Contains("{PROJECT}");
				bool bHasUserTag = Layer.Contains("{USER}");

				// skip certain layers if we are platform-less, project-less, or userdir-less
				if ((bHasPlatformTag && PlatformName == "None") ||
					(bHasProjectTag && ProjectDir == null) ||
					(bHasUserTag && GetUserDir() == null))
				{
					continue;
				}

				string LayerPath = PerformBasicReplacements(Layer, BaseIniName);

				// we only expand engine/project inis
				if (Layer.Contains("{ENGINE}") || Layer.Contains("{PROJECT}"))
				{
					foreach (ConfigLayerExpansion Expansion in ConfigLayerExpansions)
					{
						// expansion replacements
						string ExpandedPath = PerformExpansionReplacements(Expansion, LayerPath);

						// if nothing was replaced, then skip it, as it won't change anything
						if (ExpandedPath == null)
						{
							continue;
						}

						// now go up the ini parent chain
						if (bHasPlatformTag)
						{
							DataDrivenPlatformInfo.ConfigDataDrivenPlatformInfo Info = DataDrivenPlatformInfo.GetDataDrivenInfoForPlatform(PlatformName);
							if (Info != null && Info.IniParentChain != null)
							{
								// the IniParentChain
								foreach (string ParentPlatform in Info.IniParentChain)
								{
									// @note: We are using the ParentPlatform as both PlatformExtensionName _and_ IniPlatformName. This is because the parent
									// may not even exist as a UnrealTargetPlatform, and all we have is a string to look up, and it would just get the same
									// string back, if we did look it up. This could become an issue if Win64 becomes a PlatformExtension, and wants to have 
									// a parent Platform, of ... something. This is likely to never be an issue, but leaving this note here just in case.
									yield return new FileReference(PerformFinalExpansions(ExpandedPath, ParentPlatform, ProjectDir));
								}
							}
							// always yield the active platform last 
							yield return new FileReference(PerformFinalExpansions(ExpandedPath, PlatformName, ProjectDir));
						}
						else
						{
							yield return new FileReference(PerformFinalExpansions(ExpandedPath, "", ProjectDir));
						}
					}
				}
				else
				{
					yield return new FileReference(LayerPath);
				}
			}

			// Find all the generated config files
			foreach(FileReference GeneratedConfigFile in EnumerateGeneratedConfigFileLocations(Type, ProjectDir, Platform))
			{
				yield return GeneratedConfigFile;
			}
		}

		/// <summary>
		/// Returns a list of INI filenames for the given project
		/// </summary>
		public static IEnumerable<FileReference> EnumerateGeneratedConfigFileLocations(ConfigHierarchyType Type, DirectoryReference ProjectDir, UnrealTargetPlatform Platform)
		{
			string BaseIniName = Enum.GetName(typeof(ConfigHierarchyType), Type);
			string PlatformName = GetIniPlatformName(Platform);

			// Get the generated config file too. EditorSettings overrides this from 
			if (Type == ConfigHierarchyType.EditorSettings)
			{
				yield return FileReference.Combine(GetGameAgnosticSavedDir(), "Config", PlatformName, BaseIniName + ".ini");
			}
			else
			{
				yield return FileReference.Combine(GetGeneratedConfigDir(ProjectDir), PlatformName, BaseIniName + ".ini");
			}
		}

		/// <summary>
		/// Determines the path to the generated config directory (same as FPaths::GeneratedConfigDir())
		/// </summary>
		/// <returns></returns>
		public static DirectoryReference GetGeneratedConfigDir(DirectoryReference ProjectDir)
		{
			if(ProjectDir == null)
			{
				return DirectoryReference.Combine(UnrealBuildTool.EngineDirectory, "Saved", "Config");
			}
			else
			{
				return DirectoryReference.Combine(ProjectDir, "Saved", "Config");
			}
		}

		/// <summary>
		/// Determes the path to the game-agnostic saved directory (same as FPaths::GameAgnosticSavedDir())
		/// </summary>
		/// <returns></returns>
		public static DirectoryReference GetGameAgnosticSavedDir()
		{
			if(UnrealBuildTool.IsEngineInstalled())
			{
				return DirectoryReference.Combine(Utils.GetUserSettingDirectory(), "UnrealEngine", String.Format("{0}.{1}", ReadOnlyBuildVersion.Current.MajorVersion, ReadOnlyBuildVersion.Current.MinorVersion), "Saved");
			}
			else
			{
				return DirectoryReference.Combine(UnrealBuildTool.EngineDirectory, "Saved");
			}
		}

		/// <summary>
		/// Returns the platform name to use as part of platform-specific config files
		/// </summary>
		public static string GetIniPlatformName(UnrealTargetPlatform TargetPlatform)
		{
			if (TargetPlatform == UnrealTargetPlatform.Win32 || TargetPlatform == UnrealTargetPlatform.Win64)
			{
				return "Windows";
			}
			else if (TargetPlatform == UnrealTargetPlatform.HoloLens)
			{
				return "HoloLens";
			}
			else
			{
				return TargetPlatform.ToString();
			}
		}
	}
}
