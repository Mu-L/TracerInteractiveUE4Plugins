// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// Representation of a reference to a plugin from a project file
	/// </summary>
	[DebuggerDisplay("Name={Name}")]
	public class PluginReferenceDescriptor
	{
		/// <summary>
		/// Name of the plugin
		/// </summary>
		public string Name;

		/// <summary>
		/// Whether it should be enabled by default
		/// </summary>
		public bool bEnabled;

		/// <summary>
		/// Whether this plugin is optional, and the game should silently ignore it not being present
		/// </summary>
		public bool bOptional;

		/// <summary>
		/// Description of the plugin for users that do not have it installed.
		/// </summary>
		public string Description;

		/// <summary>
		/// URL for this plugin on the marketplace, if the user doesn't have it installed.
		/// </summary>
		public string MarketplaceURL;

		/// <summary>
		/// If enabled, list of platforms for which the plugin should be enabled (or all platforms if blank).
		/// </summary>
		public List<UnrealTargetPlatform> WhitelistPlatforms;

		/// <summary>
		/// If enabled, list of platforms for which the plugin should be disabled.
		/// </summary>
		public List<UnrealTargetPlatform> BlacklistPlatforms;

		/// <summary>
		/// If enabled, list of target configurations for which the plugin should be enabled (or all target configurations if blank).
		/// </summary>
		public UnrealTargetConfiguration[] WhitelistTargetConfigurations;

		/// <summary>
		/// If enabled, list of target configurations for which the plugin should be disabled.
		/// </summary>
		public UnrealTargetConfiguration[] BlacklistTargetConfigurations;

		/// <summary>
		/// If enabled, list of targets for which the plugin should be enabled (or all targets if blank).
		/// </summary>
		public TargetType[] WhitelistTargets;

		/// <summary>
		/// If enabled, list of targets for which the plugin should be disabled.
		/// </summary>
		public TargetType[] BlacklistTargets;

		/// <summary>
		/// The list of supported platforms for this plugin. This field is copied from the plugin descriptor, and supplements the user's whitelisted and blacklisted platforms.
		/// </summary>
		public List<UnrealTargetPlatform> SupportedTargetPlatforms;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="InName">Name of the plugin</param>
		/// <param name="InMarketplaceURL">The marketplace URL for plugins which are not installed</param>
		/// <param name="bInEnabled">Whether the plugin is enabled</param>
		public PluginReferenceDescriptor(string InName, string InMarketplaceURL, bool bInEnabled)
		{
			Name = InName;
			MarketplaceURL = InMarketplaceURL;
			bEnabled = bInEnabled;
		}

		/// <summary>
		/// Construct a PluginReferenceDescriptor from a Json object
		/// </summary>
		/// <param name="Writer">The writer for output fields</param>
		public void Write(JsonWriter Writer)
		{
			Writer.WriteObjectStart();
			Writer.WriteValue("Name", Name);
			Writer.WriteValue("Enabled", bEnabled);
			if(bEnabled && bOptional)
			{
				Writer.WriteValue("Optional", bOptional);
			}
			if(!String.IsNullOrEmpty(Description))
			{
				Writer.WriteValue("Description", Description);
			}
			if(!String.IsNullOrEmpty(MarketplaceURL))
			{
				Writer.WriteValue("MarketplaceURL", MarketplaceURL);
			}
			if(WhitelistPlatforms != null && WhitelistPlatforms.Count > 0)
			{
				Writer.WriteStringArrayField("WhitelistPlatforms", WhitelistPlatforms.Select(x => x.ToString()).ToArray());
			}
			if(BlacklistPlatforms != null && BlacklistPlatforms.Count > 0)
			{
				Writer.WriteStringArrayField("BlacklistPlatforms", BlacklistPlatforms.Select(x => x.ToString()).ToArray());
			}
			if (WhitelistTargetConfigurations != null && WhitelistTargetConfigurations.Length > 0)
			{
				Writer.WriteEnumArrayField("WhitelistTargetConfigurations", WhitelistTargetConfigurations);
			}
			if (BlacklistTargetConfigurations != null && BlacklistTargetConfigurations.Length > 0)
			{
				Writer.WriteEnumArrayField("BlacklistTargetConfigurations", BlacklistTargetConfigurations);
			}
			if (WhitelistTargets != null && WhitelistTargets.Length > 0)
			{
				Writer.WriteEnumArrayField("WhitelistTargets", WhitelistTargets);
			}
			if(BlacklistTargets != null && BlacklistTargets.Length > 0)
			{
				Writer.WriteEnumArrayField("BlacklistTargets", BlacklistTargets);
			}
			if(SupportedTargetPlatforms != null && SupportedTargetPlatforms.Count > 0)
			{
				Writer.WriteStringArrayField("SupportedTargetPlatforms", SupportedTargetPlatforms.Select(x => x.ToString()).ToArray());
			}
			Writer.WriteObjectEnd();
		}

		/// <summary>
		/// Write an array of module descriptors
		/// </summary>
		/// <param name="Writer">The Json writer to output to</param>
		/// <param name="Name">Name of the array</param>
		/// <param name="Plugins">Array of plugins</param>
		public static void WriteArray(JsonWriter Writer, string Name, PluginReferenceDescriptor[] Plugins)
		{
			if (Plugins != null && Plugins.Length > 0)
			{
				Writer.WriteArrayStart(Name);
				foreach (PluginReferenceDescriptor Plugin in Plugins)
				{
					Plugin.Write(Writer);
				}
				Writer.WriteArrayEnd();
			}
		}

		/// <summary>
		/// Construct a PluginReferenceDescriptor from a Json object
		/// </summary>
		/// <param name="RawObject">The Json object containing a plugin reference descriptor</param>
		/// <returns>New PluginReferenceDescriptor object</returns>
		public static PluginReferenceDescriptor FromJsonObject(JsonObject RawObject)
		{
			string[] WhitelistPlatformNames = null;
			string[] BlacklistPlatformNames = null;
			string[] SupportedTargetPlatformNames = null;

			PluginReferenceDescriptor Descriptor = new PluginReferenceDescriptor(RawObject.GetStringField("Name"), null, RawObject.GetBoolField("Enabled"));
			RawObject.TryGetBoolField("Optional", out Descriptor.bOptional);
			RawObject.TryGetStringField("Description", out Descriptor.Description);
			RawObject.TryGetStringField("MarketplaceURL", out Descriptor.MarketplaceURL);

			// Only parse platform information if enabled
			if (Descriptor.bEnabled)
			{
				RawObject.TryGetStringArrayField("WhitelistPlatforms", out WhitelistPlatformNames);
				RawObject.TryGetStringArrayField("BlacklistPlatforms", out BlacklistPlatformNames);
				RawObject.TryGetEnumArrayField<UnrealTargetConfiguration>("WhitelistTargetConfigurations", out Descriptor.WhitelistTargetConfigurations);
				RawObject.TryGetEnumArrayField<UnrealTargetConfiguration>("BlacklistTargetConfigurations", out Descriptor.BlacklistTargetConfigurations);
				RawObject.TryGetEnumArrayField<TargetType>("WhitelistTargets", out Descriptor.WhitelistTargets);
				RawObject.TryGetEnumArrayField<TargetType>("BlacklistTargets", out Descriptor.BlacklistTargets);
				RawObject.TryGetStringArrayField("SupportedTargetPlatforms", out SupportedTargetPlatformNames);
			}

			try
			{
				// convert string array to UnrealTargetPlatform arrays
				if (WhitelistPlatformNames != null)
				{
					Descriptor.WhitelistPlatforms = WhitelistPlatformNames.Select(x => UnrealTargetPlatform.Parse(x)).ToList();
				}
				if (BlacklistPlatformNames != null)
				{
					Descriptor.BlacklistPlatforms = BlacklistPlatformNames.Select(x => UnrealTargetPlatform.Parse(x)).ToList();
				}
				if (SupportedTargetPlatformNames != null)
				{
					Descriptor.SupportedTargetPlatforms = SupportedTargetPlatformNames.Select(x => UnrealTargetPlatform.Parse(x)).ToList();
				}
			}
			catch (BuildException Ex)
			{
				ExceptionUtils.AddContext(Ex, "while parsing PluginReferenceDescriptor {0}", Descriptor.Name);
				throw;
			}


			return Descriptor;
		}

		/// <summary>
		/// Determines if this reference enables the plugin for a given platform
		/// </summary>
		/// <param name="Platform">The platform to check</param>
		/// <returns>True if the plugin should be enabled</returns>
		public bool IsEnabledForPlatform(UnrealTargetPlatform Platform)
		{
			if (!bEnabled)
			{
				return false;
			}
			if (WhitelistPlatforms != null && WhitelistPlatforms.Count > 0 && !WhitelistPlatforms.Contains(Platform))
			{
				return false;
			}
			if (BlacklistPlatforms != null && BlacklistPlatforms.Contains(Platform))
			{
				return false;
			}
			return true;
		}

		/// <summary>
		/// Determines if this reference enables the plugin for a given target configuration
		/// </summary>
		/// <param name="TargetConfiguration">The target configuration to check</param>
		/// <returns>True if the plugin should be enabled</returns>
		public bool IsEnabledForTargetConfiguration(UnrealTargetConfiguration TargetConfiguration)
		{
			if (!bEnabled)
			{
				return false;
			}
			if (WhitelistTargetConfigurations != null && WhitelistTargetConfigurations.Length > 0 && !WhitelistTargetConfigurations.Contains(TargetConfiguration))
			{
				return false;
			}
			if (BlacklistTargetConfigurations != null && BlacklistTargetConfigurations.Contains(TargetConfiguration))
			{
				return false;
			}
			return true;
		}

		/// <summary>
		/// Determines if this reference enables the plugin for a given target
		/// </summary>
		/// <param name="Target">The target to check</param>
		/// <returns>True if the plugin should be enabled</returns>
		public bool IsEnabledForTarget(TargetType Target)
		{
			if (!bEnabled)
			{
				return false;
			}
			if (WhitelistTargets != null && WhitelistTargets.Length > 0 && !WhitelistTargets.Contains(Target))
			{
				return false;
			}
			if (BlacklistTargets != null && BlacklistTargets.Contains(Target))
			{
				return false;
			}
			return true;
		}

		/// <summary>
		/// Determines if this reference is valid for the given target platform.
		/// </summary>
		/// <param name="Platform">The platform to check</param>
		/// <returns>True if the plugin for this target platform</returns>
		public bool IsSupportedTargetPlatform(UnrealTargetPlatform Platform)
		{
			return SupportedTargetPlatforms == null || SupportedTargetPlatforms.Count == 0 || SupportedTargetPlatforms.Contains(Platform);
		}
	}
}
