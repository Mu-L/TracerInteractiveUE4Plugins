﻿// Copyright Epic Games, Inc. All Rights Reserved.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;
using AutomationTool;
using UnrealBuildTool;

namespace AutomationUtils.Automation
{
	public class BundleUtils
	{
		public interface IReadOnlyBundleSettings
		{
			string Name { get; }
			List<string> Tags { get; }
			bool bContainsShaderLibrary { get; }
		}

		public class BundleSettings : IReadOnlyBundleSettings
		{
			public string Name { get; set; }
			public List<string> Tags { get; set; }
			public List<BundleSettings> Children { get; set; }
			public List<string> FileRegex { get; set; }
			public bool bFoundParent { get; set; }
			public bool bContainsShaderLibrary { get; set; }
			public int Order { get; set; }
			public string ExecFileName { get; set; }
		}

		public static void LoadBundleConfig(DirectoryReference ProjectDir, UnrealTargetPlatform Platform, out List<BundleSettings> Bundles)
		{
			LoadBundleConfig<BundleSettings>(ProjectDir, Platform, out Bundles, delegate (BundleSettings Settings, ConfigHierarchy BundleConfig, string Section) { });
		}

		public static void LoadBundleConfig<TPlatformBundleSettings>(DirectoryReference ProjectDir, UnrealTargetPlatform Platform, 
			out List<TPlatformBundleSettings> Bundles, 
			Action<TPlatformBundleSettings, ConfigHierarchy, string> GetPlatformSettings) 
			where TPlatformBundleSettings : BundleSettings, new()
		{
			Bundles = new List<TPlatformBundleSettings>();

			ConfigHierarchy BundleConfig = ConfigCache.ReadHierarchy(ConfigHierarchyType.InstallBundle, ProjectDir, Platform);

			const string BundleDefinitionPrefix = "InstallBundleDefinition ";

			foreach (string SectionName in BundleConfig.SectionNames)
			{
				if (!SectionName.StartsWith(BundleDefinitionPrefix))
					continue;

				TPlatformBundleSettings Bundle = new TPlatformBundleSettings();
				Bundle.Name = SectionName.Substring(BundleDefinitionPrefix.Length);
				{
					int Order;
					if(BundleConfig.GetInt32(SectionName, "Order", out Order))
					{
						Bundle.Order = Order;
					}
					else
					{
						Bundle.Order = int.MaxValue;
					}
				}
				{
					string ExecFileName;
					BundleConfig.GetString(SectionName, "ExecFileName", out ExecFileName);
					Bundle.ExecFileName = ExecFileName;
				}
				{
					List<string> Tags;
					BundleConfig.GetArray(SectionName, "Tags", out Tags);
					Bundle.Tags = Tags;
				}
				{
					List<string> FileRegex;
					BundleConfig.GetArray(SectionName, "FileRegex", out FileRegex);
					Bundle.FileRegex = FileRegex;
				}
				{
					bool bContainsShaderLibrary;
					BundleConfig.GetBool(SectionName, "ContainsShaderLibrary", out bContainsShaderLibrary);
					Bundle.bContainsShaderLibrary = bContainsShaderLibrary;
				}
				if (Bundle.Tags == null)
				{
					Bundle.Tags = new List<string>();
				}

				GetPlatformSettings(Bundle, BundleConfig, BundleDefinitionPrefix + Bundle.Name);

				Bundles.Add(Bundle);
			}

			// Use OrderBy and not Sort because OrderBy is stable
			Bundles = Bundles.OrderBy(Bundle => Bundle.Order).ToList();
		}
	}
}
