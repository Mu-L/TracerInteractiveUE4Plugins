﻿// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.Diagnostics;
using System.IO;

namespace UnrealBuildTool
{
	/// <summary>
	/// Base class for platform-specific project generators
	/// </summary>
	class IOSProjectGenerator : UEPlatformProjectGenerator
	{
		/// <summary>
		/// Register the platform with the UEPlatformProjectGenerator class
		/// </summary>
		public override void RegisterPlatformProjectGenerator()
		{
			// Register this project generator for Mac
			Log.TraceVerbose("        Registering for {0}", UnrealTargetPlatform.IOS.ToString());
			UEPlatformProjectGenerator.RegisterPlatformProjectGenerator(UnrealTargetPlatform.IOS, this);
		}

		///
		///	VisualStudio project generation functions
		///	
		/// <summary>
		/// Whether this build platform has native support for VisualStudio
		/// </summary>
		/// <param name="InPlatform">  The UnrealTargetPlatform being built</param>
		/// <param name="InConfiguration"> The UnrealTargetConfiguration being built</param>
		/// <param name="ProjectFileFormat"></param>
		/// <returns>bool    true if native VisualStudio support (or custom VSI) is available</returns>
		public override bool HasVisualStudioSupport(UnrealTargetPlatform InPlatform, UnrealTargetConfiguration InConfiguration, VCProjectFileFormat ProjectFileFormat)
		{
			// iOS is not supported in VisualStudio
			return false;
		}
	}
}
