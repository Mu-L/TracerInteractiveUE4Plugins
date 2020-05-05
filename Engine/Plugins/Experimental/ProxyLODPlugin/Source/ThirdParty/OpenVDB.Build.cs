// Copyright Epic Games, Inc. All Rights Reserved.

// djh using System.IO;
using UnrealBuildTool;
using System.Diagnostics;
using System.Collections.Generic;
using System.IO; // This is for the directory check.

public class OpenVDB : ModuleRules
{
    public OpenVDB(ReadOnlyTargetRules Target) : base(Target)
    {
        // We are just setting up paths for pre-compiled binaries.
        Type = ModuleType.External;

        // For boost:: and TBB:: code
        bEnableUndefinedIdentifierWarnings = false;
        bUseRTTI = true;


        PublicDefinitions.Add("OPENVDB_STATICLIB");
        PublicDefinitions.Add("OPENVDB_OPENEXR_STATICLIB");
        PublicDefinitions.Add("OPENVDB_2_ABI_COMPATIBLE");

		PublicIncludePaths.Add(ModuleDirectory);

        // For testing during developement 
        bool bDebugPaths = true;

        // Only building for Windows

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string OpenVDBHeaderDir = ModuleDirectory + "/openvdb";

            
            if (bDebugPaths)
            {
                if (!Directory.Exists(OpenVDBHeaderDir))
                {
                    string Err = string.Format("OpenVDB SDK not found in {0}", OpenVDBHeaderDir);
                    System.Console.WriteLine(Err);
                    throw new BuildException(Err);
                }
            }

            PublicIncludePaths.Add(OpenVDBHeaderDir);

            // Construct the OpenVDB directory name
            string LibDirName = ModuleDirectory + "/Deploy/";
            LibDirName += "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName() + "/lib/x64/";

            bool bDebug = (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT);

            if (bDebug)
            {
                LibDirName += "Debug/";
            }
            else
            {
                LibDirName += "Release/";
            }

            if (bDebugPaths)
            {
                // Look for the file
                if (!File.Exists(LibDirName + "OpenVDB.lib"))
                {
                    string Err = string.Format("OpenVDB.lib not found in {0}", LibDirName);
                    System.Console.WriteLine(Err);
                    throw new BuildException(Err);
                }
            }
            PublicAdditionalLibraries.Add(LibDirName + "OpenVDB.lib");

            // Add openexr
            PublicIncludePaths.Add(Target.UEThirdPartySourceDirectory);// + "openexr/Deploy/include");

        

            // Add TBB
            {
                // store the compiled tbb library in the same area as the rest of the third party code
                string TBBPath = Target.UEThirdPartySourceDirectory + "IntelTBB/IntelTBB-2019u8";
                PublicIncludePaths.Add(Path.Combine(TBBPath, "include"));
                string TBBLibPath = TBBPath + "/lib/Win64/vc14";
                if (bDebug)
                {
                    PublicAdditionalLibraries.Add(Path.Combine(TBBLibPath, "tbb_debug.lib"));
                }
                else
                {
                    PublicAdditionalLibraries.Add(Path.Combine(TBBLibPath, "tbb.lib"));
                }
            }
            // Add LibZ
            {
                string ZLibPath = Target.UEThirdPartySourceDirectory + "zlib/zlib-1.2.5/Lib/Win64";
                if (bDebug)
                {
                    PublicAdditionalLibraries.Add(Path.Combine(ZLibPath, "zlibd_64.lib"));
                }
                else
                {
                    PublicAdditionalLibraries.Add(Path.Combine(ZLibPath, "zlib_64.lib"));
                }
            }
        }
        else
        {
            string Err = "Wrong build env!";
            System.Console.WriteLine(Err);
            throw new BuildException(Err);
        }
    }
}
