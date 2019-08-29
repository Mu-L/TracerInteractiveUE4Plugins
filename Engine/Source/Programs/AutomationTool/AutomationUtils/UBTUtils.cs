// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using Tools.DotNETCommon;
using UnrealBuildTool;

namespace AutomationTool
{

	public partial class CommandUtils
	{
		/// <summary>
		/// True if UBT has deleted junk files
		/// </summary>
		private static bool bJunkDeleted = false;

		/// <summary>
		/// Runs UBT with the specified commandline. Automatically creates a logfile. When 
		/// no LogName is specified, the executable name is used as logfile base name.
		/// </summary>
		/// <param name="Env">Environment to use.</param>
		/// <param name="CommandLine">Commandline to pass on to UBT.</param>
		/// <param name="LogName">Optional logfile name.</param>
        public static void RunUBT(CommandEnvironment Env, string UBTExecutable, string CommandLine, Dictionary<string, string> EnvVars = null)
		{
			if (!FileExists(UBTExecutable))
			{
				throw new AutomationException("Unable to find UBT executable: " + UBTExecutable);
			}

			if (GlobalCommandLine.VS2015)
			{
				CommandLine += " -2015";
			}
			if (!IsBuildMachine && UnrealBuildTool.BuildHostPlatform.Current.Platform == UnrealBuildTool.UnrealTargetPlatform.Mac)
			{
				CommandLine += " -nocreatestub";
			}
			CommandLine += " -NoHotReload";
			if (bJunkDeleted || GlobalCommandLine.IgnoreJunk)
			{
				// UBT has already deleted junk files, make sure it doesn't do it again
				CommandLine += " -ignorejunk";
			}
			else
			{
				// UBT will delete junk on first run
				bJunkDeleted = true;
			}

			string BaseLogName = String.Format("UBT-{0}", String.Join("-", SharedUtils.ParseCommandLine(CommandLine).Where(x => !x.Contains('/') && !x.Contains('\\') && !x.StartsWith("-"))));
			string LogName;
			for(int Attempt = 1;;Attempt++)
			{
				LogName = String.Format("{0}.txt", (Attempt == 1)? BaseLogName : String.Format("{0}_{1}", BaseLogName, Attempt));

				FileReference LogLocation = FileReference.Combine(new DirectoryReference(Env.LogFolder), LogName);
				if(!FileReference.Exists(LogLocation))
				{
					CommandLine += String.Format(" -log=\"{0}\"", LogLocation);
					break;
				}

				if (Attempt >= 50)
				{
					throw new AutomationException("Unable to find name for UBT log file after {0} attempts", Attempt);
				}
			}

			IProcessResult Result = Run(UBTExecutable, CommandLine, Options: ERunOptions.AllowSpew | ERunOptions.NoStdOutCapture, Env: EnvVars);
			if(Result.ExitCode != 0)
			{
				throw new AutomationException((ExitCode)Result.ExitCode, "UnrealBuildTool failed. See log for more details. ({0})", CommandUtils.CombinePaths(Env.FinalLogFolder, LogName));
			}
		}

		/// <summary>
		/// Builds a UBT Commandline.
		/// </summary>
		/// <param name="Project">Unreal project to build (optional)</param>
		/// <param name="Target">Target to build.</param>
		/// <param name="Platform">Platform to build for.</param>
		/// <param name="Config">Configuration to build.</param>
		/// <param name="AdditionalArgs">Additional arguments to pass on to UBT.</param>
		public static string UBTCommandline(FileReference Project, string Target, string Platform, string Config, string AdditionalArgs = "")
		{
			string CmdLine;
			if (Project == null)
			{
				CmdLine = String.Format("{0} {1} {2} {3}", Target, Platform, Config, AdditionalArgs);
			}
			else
			{
				CmdLine = String.Format("{0} {1} {2} -Project={3} {4}", Target, Platform, Config, CommandUtils.MakePathSafeToUseWithCommandLine(Project.FullName), AdditionalArgs);
			}
			return CmdLine;
		}

		/// <summary>
		/// Builds a target using UBT.  Automatically creates a logfile. When 
		/// no LogName is specified, the executable name is used as logfile base name.
		/// </summary>
		/// <param name="Env">BuildEnvironment to use.</param>
		/// <param name="Project">Unreal project to build (optional)</param>
		/// <param name="Target">Target to build.</param>
		/// <param name="Platform">Platform to build for.</param>
		/// <param name="Config">Configuration to build.</param>
		/// <param name="AdditionalArgs">Additional arguments to pass on to UBT.</param>
		/// <param name="LogName">Optional logifle name.</param>
		public static void RunUBT(CommandEnvironment Env, string UBTExecutable, FileReference Project, string Target, string Platform, string Config, string AdditionalArgs = "", Dictionary<string, string> EnvVars = null)
		{
			RunUBT(Env, UBTExecutable, UBTCommandline(Project, Target, Platform, Config, AdditionalArgs), EnvVars);
		}

	}

}