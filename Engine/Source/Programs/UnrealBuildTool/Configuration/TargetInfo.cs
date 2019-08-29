// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.Serialization;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// Information about a target, passed along when creating a module descriptor
	/// </summary>
	public class TargetInfo
	{
		/// <summary>
		/// Name of the target
		/// </summary>
		public readonly string Name;

		/// <summary>
		/// The platform that the target is being built for
		/// </summary>
		public readonly UnrealTargetPlatform Platform;

		/// <summary>
		/// The configuration being built
		/// </summary>
		public readonly UnrealTargetConfiguration Configuration;

		/// <summary>
		/// Architecture that the target is being built for (or an empty string for the default)
		/// </summary>
		public readonly string Architecture;

		/// <summary>
		/// The project containing the target
		/// </summary>
		public readonly FileReference ProjectFile;

		/// <summary>
		/// The current build version
		/// </summary>
		public ReadOnlyBuildVersion Version
		{
			get { return ReadOnlyBuildVersion.Current; }
		}

		/// <summary>
		/// Constructs a TargetInfo for passing to the TargetRules constructor.
		/// </summary>
		/// <param name="Name">Name of the target being built</param>
		/// <param name="Platform">The platform that the target is being built for</param>
		/// <param name="Configuration">The configuration being built</param>
		/// <param name="Architecture">The architecture being built for</param>
		/// <param name="ProjectFile">Path to the project file containing the target</param>
		public TargetInfo(string Name, UnrealTargetPlatform Platform, UnrealTargetConfiguration Configuration, string Architecture, FileReference ProjectFile)
		{
			this.Name = Name;
			this.Platform = Platform;
			this.Configuration = Configuration;
			this.Architecture = Architecture;
			this.ProjectFile = ProjectFile;
		}
	}
}
