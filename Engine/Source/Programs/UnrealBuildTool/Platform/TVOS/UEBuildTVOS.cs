// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.Diagnostics;
using System.IO;
using System.Xml;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	class TVOSProjectSettings : IOSProjectSettings
	{
		/// <summary>
		/// Which version of the iOS to allow at run time
		/// </summary>
		public override string RuntimeVersion
		{
			get { return "10.0"; }
		}

		/// <summary>
		/// which devices the game is allowed to run on
		/// </summary>
		public override string RuntimeDevices
		{
			get { return "3"; }
		}

		public TVOSProjectSettings(FileReference ProjectFile, String Bundle)
			: base(ProjectFile, UnrealTargetPlatform.TVOS, Bundle)
		{
		}
	}

	class TVOSProvisioningData : IOSProvisioningData
	{
		public TVOSProvisioningData(TVOSProjectSettings ProjectSettings, bool bForDistribution)
			: base(ProjectSettings, true, bForDistribution)
		{
		}
	}

	class TVOSPlatform : IOSPlatform
    {
		public TVOSPlatform(IOSPlatformSDK InSDK)
			: base(InSDK, UnrealTargetPlatform.TVOS)
		{
		}

		// by default, use an empty architecture (which is really just a modifer to the platform for some paths/names)
		public static string TVOSArchitecture = "";

		// The current architecture - affects everything about how UBT operates on IOS
		public override string GetDefaultArchitecture(FileReference ProjectFile)
		{
			return TVOSArchitecture;
		}

		public override void ValidateTarget(TargetRules Target)
		{
			base.ValidateTarget(Target);

			// make sure we add Metal, in case base class got it wrong
			if (Target.GlobalDefinitions.Contains("HAS_METAL=0"))
			{
				Target.GlobalDefinitions.Remove("HAS_METAL=0");
				Target.GlobalDefinitions.Add("HAS_METAL=1");
				Target.ExtraModuleNames.Add("MetalRHI");
			}
		}

		public new TVOSProjectSettings ReadProjectSettings(FileReference ProjectFile, string Bundle = "")
		{
			return (TVOSProjectSettings)base.ReadProjectSettings(ProjectFile, Bundle);
		}

		protected override IOSProjectSettings CreateProjectSettings(FileReference ProjectFile, string Bundle)
		{
			return new TVOSProjectSettings(ProjectFile, Bundle);
		}

		public TVOSProvisioningData ReadProvisioningData(TVOSProjectSettings ProjectSettings, bool bForDistribution = false)
        {
			return (TVOSProvisioningData)base.ReadProvisioningData(ProjectSettings, bForDistribution);
		}

		protected override IOSProvisioningData CreateProvisioningData(IOSProjectSettings ProjectSettings, bool bForDistribution)
		{
			return new TVOSProvisioningData((TVOSProjectSettings)ProjectSettings, bForDistribution);
		}

		public override void ModifyModuleRulesForOtherPlatform(string ModuleName, ModuleRules Rules, ReadOnlyTargetRules Target)
		{
			base.ModifyModuleRulesForOtherPlatform(ModuleName, Rules, Target);

			// don't do any target platform stuff if SDK is not available
			if (!UEBuildPlatform.IsPlatformAvailable(Platform))
			{
				return;
			}

			if ((Target.Platform == UnrealTargetPlatform.Win32) || (Target.Platform == UnrealTargetPlatform.Win64) || (Target.Platform == UnrealTargetPlatform.Mac))
			{
				// allow standalone tools to use targetplatform modules, without needing Engine
				if (Target.bForceBuildTargetPlatforms)
				{
					// @todo tvos: Make the module
					// InModule.AddPlatformSpecificDynamicallyLoadedModule("TVOSTargetPlatform");
				}
			}
		}
    
		/// <summary>
		/// Setup the target environment for building
		/// </summary>
		/// <param name="Target">Settings for the target being compiled</param>
		/// <param name="CompileEnvironment">The compile environment for this target</param>
		/// <param name="LinkEnvironment">The link environment for this target</param>
		public override void SetUpEnvironment(ReadOnlyTargetRules Target, CppCompileEnvironment CompileEnvironment, LinkEnvironment LinkEnvironment)
		{
			base.SetUpEnvironment(Target, CompileEnvironment, LinkEnvironment);
			CompileEnvironment.Definitions.Add("PLATFORM_TVOS=1");

			// TVOS uses only IOS header files, so use it's platform headers
			CompileEnvironment.Definitions.Add("OVERRIDE_PLATFORM_HEADER_NAME=IOS");

		}

		/// <summary>
		/// Creates a toolchain instance for the given platform.
		/// </summary>
		/// <param name="Target">The target being built</param>
		/// <returns>New toolchain instance.</returns>
		public override UEToolChain CreateToolChain(ReadOnlyTargetRules Target)
		{
			TVOSProjectSettings ProjectSettings = ((TVOSPlatform)UEBuildPlatform.GetBuildPlatform(UnrealTargetPlatform.TVOS)).ReadProjectSettings(Target.ProjectFile);
			return new TVOSToolChain(Target, ProjectSettings);
		}

		public override void Deploy(TargetReceipt Receipt)
		{
			new UEDeployTVOS().PrepTargetForDeployment(Receipt);
		}
	}

	class TVOSPlatformFactory : UEBuildPlatformFactory
	{
		public override UnrealTargetPlatform TargetPlatform
		{
			get { return UnrealTargetPlatform.TVOS; }
		}

		/// <summary>
		/// Register the platform with the UEBuildPlatform class
		/// </summary>
		public override void RegisterBuildPlatforms()
		{
			IOSPlatformSDK SDK = new IOSPlatformSDK();
			SDK.ManageAndValidateSDK();

			// Register this build platform for IOS
			UEBuildPlatform.RegisterBuildPlatform(new TVOSPlatform(SDK));
			UEBuildPlatform.RegisterPlatformWithGroup(UnrealTargetPlatform.TVOS, UnrealPlatformGroup.Apple);
			UEBuildPlatform.RegisterPlatformWithGroup(UnrealTargetPlatform.TVOS, UnrealPlatformGroup.IOS);
		}
	}

}

