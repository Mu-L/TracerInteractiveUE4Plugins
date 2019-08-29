﻿// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public abstract class MayaLiveLinkPluginBase : ModuleRules
{
	public MayaLiveLinkPluginBase(ReadOnlyTargetRules Target) : base(Target)
	{
		// For LaunchEngineLoop.cpp include.  You shouldn't need to add anything else to this line.
		PrivateIncludePaths.AddRange( new string[] { "Runtime/Launch/Public", "Runtime/Launch/Private" }  );

		// Unreal dependency modules
		PrivateDependencyModuleNames.AddRange( new string[] 
		{
			"Core",
            "CoreUObject",
			"ApplicationCore",
			"Projects",
            "UdpMessaging",
            "LiveLinkInterface",
            "LiveLinkMessageBusFramework",
		} );


		//
		// Maya SDK setup
		//

		{
			string MayaVersionString = GetMayaVersion();
			string MayaInstallFolder = @"C:\Program Files\Autodesk\Maya" + MayaVersionString;

			// Make sure this version of Maya is actually installed
			if( Directory.Exists( MayaInstallFolder ) )
			{
                //throw new BuildException( "Couldn't find Autodesk Maya " + MayaVersionString + " in folder '" + MayaInstallFolder + "'.  This version of Maya must be installed for us to find the Maya SDK files." );

                // These are required for Maya headers to compile properly as a DLL
                PublicDefinitions.Add("NT_PLUGIN=1");
                PublicDefinitions.Add("REQUIRE_IOSTREAM=1");

                PrivateIncludePaths.Add(Path.Combine(MayaInstallFolder, "include"));

                if (Target.Platform == UnrealTargetPlatform.Win64)  // @todo: Support other platforms?
                {
                    PublicLibraryPaths.Add(Path.Combine(MayaInstallFolder, "lib"));

                    // Maya libraries we're depending on
                    PublicAdditionalLibraries.AddRange(new string[]
                        {
                            "Foundation.lib",
                            "OpenMaya.lib",
                            "OpenMayaAnim.lib",
                            "OpenMayaUI.lib"}
                    );
                }
            }
		}
	}
	
	public abstract string GetMayaVersion();
}

public class MayaLiveLinkPlugin2016 : MayaLiveLinkPluginBase
{
	public MayaLiveLinkPlugin2016(ReadOnlyTargetRules Target) : base(Target)
	{
	}
	
	public override string GetMayaVersion() { return "2016"; }
}
