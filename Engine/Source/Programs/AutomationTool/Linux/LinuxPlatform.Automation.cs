﻿// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.IO;
using System.Diagnostics;
using AutomationTool;
using UnrealBuildTool;
using Tools.DotNETCommon;

public abstract class BaseLinuxPlatform : Platform
{
	static string PScpPath = CombinePaths(CommandUtils.RootDirectory.FullName, "\\Engine\\Extras\\ThirdPartyNotUE\\putty\\PSCP.EXE");
	static string PlinkPath = CombinePaths(CommandUtils.RootDirectory.FullName, "\\Engine\\Extras\\ThirdPartyNotUE\\putty\\PLINK.EXE");
	static string LaunchOnHelperShellScriptName = "LaunchOnHelper.sh";

	public BaseLinuxPlatform(UnrealTargetPlatform P)
		: base(P)
	{
	}

	public override void GetFilesToDeployOrStage(ProjectParams Params, DeploymentContext SC)
	{
		if (SC.bStageCrashReporter)
		{
			FileReference ReceiptFileName = TargetReceipt.GetDefaultPath(CommandUtils.EngineDirectory, "CrashReportClient", SC.StageTargetPlatform.PlatformType, UnrealTargetConfiguration.Shipping, null);
			if (FileReference.Exists(ReceiptFileName))
			{
				TargetReceipt Receipt = TargetReceipt.Read(ReceiptFileName);
				SC.StageBuildProductsFromReceipt(Receipt, true, false);
			}
		}

		// Stage all the build products
		Console.WriteLine("Staging all {0} build products", SC.StageTargets.Count);
		int BuildProductIdx = 0;
		foreach (StageTarget Target in SC.StageTargets)
		{
			Console.WriteLine(" Product {0}: {1}", BuildProductIdx, Target.Receipt.TargetName);
			SC.StageBuildProductsFromReceipt(Target.Receipt, Target.RequireFilesExist, Params.bTreatNonShippingBinariesAsDebugFiles);
			++BuildProductIdx;
        }

		FileReference SplashImage = FileReference.Combine(SC.ProjectRoot, "Content", "Splash", "Splash.bmp");
		if(FileReference.Exists(SplashImage))
		{
			SC.StageFile(StagedFileType.NonUFS, SplashImage);
		}

		// Stage the bootstrap executable
		if (!Params.NoBootstrapExe)
		{
			foreach (StageTarget Target in SC.StageTargets)
			{
				BuildProduct Executable = Target.Receipt.BuildProducts.FirstOrDefault(x => x.Type == BuildProductType.Executable);
				if (Executable != null)
				{
					// only create bootstraps for executables
					string FullExecutablePath = Path.GetFullPath(Executable.Path.FullName);
					if (Executable.Path.FullName.Replace("\\", "/").Contains("/" + TargetPlatformType.ToString() + "/"))
					{
						string BootstrapArguments = "";
						if (!ShouldStageCommandLine(Params, SC))
						{
							if (!SC.IsCodeBasedProject)
							{
								BootstrapArguments = String.Format("\\\"../../../{0}/{0}.uproject\\\"", SC.ShortProjectName);
							}
							else
							{
								BootstrapArguments = SC.ShortProjectName;
							}
						}

						string BootstrapExeName;
						if (SC.StageTargetConfigurations.Count > 1)
						{
							BootstrapExeName = Path.GetFileName(Executable.Path.FullName);
						}
						else if (Params.IsCodeBasedProject)
						{
							BootstrapExeName = Target.Receipt.TargetName;
						}
						else
						{
							BootstrapExeName = SC.ShortProjectName;
						}

						List<StagedFileReference> StagePaths = SC.FilesToStage.NonUFSFiles.Where(x => x.Value == Executable.Path).Select(x => x.Key).ToList();
						foreach (StagedFileReference StagePath in StagePaths)
						{
							StageBootstrapExecutable(SC, BootstrapExeName + ".sh", FullExecutablePath, StagePath.Name, BootstrapArguments);
						}
					}
				}
			}
		}
	}

	public override bool ShouldStageCommandLine(ProjectParams Params, DeploymentContext SC)
	{
		return false;
	}

	void StageBootstrapExecutable(DeploymentContext SC, string ExeName, string TargetFile, string StagedRelativeTargetPath, string StagedArguments)
	{
		// create a temp script file location
		DirectoryReference IntermediateDir = DirectoryReference.Combine(SC.ProjectRoot, "Intermediate", "Staging");
		FileReference IntermediateFile = FileReference.Combine(IntermediateDir, ExeName);
		DirectoryReference.CreateDirectory(IntermediateDir);

		// make sure slashes are good
		StagedRelativeTargetPath = StagedRelativeTargetPath.Replace("\\", "/");

		// make contents
		StringBuilder Script = new StringBuilder();
		string EOL = "\n";
		Script.Append("#!/bin/sh" + EOL);
		// allow running from symlinks
		Script.AppendFormat("UE4_TRUE_SCRIPT_NAME=$(echo \\\"$0\\\" | xargs readlink -f)" + EOL);
		Script.AppendFormat("UE4_PROJECT_ROOT=$(dirname \"$UE4_TRUE_SCRIPT_NAME\")" + EOL);
		Script.AppendFormat("chmod +x \"$UE4_PROJECT_ROOT/{0}\"" + EOL, StagedRelativeTargetPath);
		Script.AppendFormat("\"$UE4_PROJECT_ROOT/{0}\" {1} $@ " + EOL, StagedRelativeTargetPath, StagedArguments);

		// write out the 
		FileReference.WriteAllText(IntermediateFile, Script.ToString());

		if (Utils.IsRunningOnMono)
		{
			var Result = CommandUtils.Run("sh", string.Format("-c 'chmod +x \\\"{0}\\\"'", IntermediateFile));
			if (Result.ExitCode != 0)
			{
				throw new AutomationException(string.Format("Failed to chmod \"{0}\"", IntermediateFile));
			}
		}

		SC.StageFile(StagedFileType.NonUFS, IntermediateFile, new StagedFileReference(ExeName));
	}

	public override string GetCookPlatform(bool bDedicatedServer, bool bIsClientOnly)
	{
		const string NoEditorCookPlatform = "LinuxNoEditor";
		const string ServerCookPlatform = "LinuxServer";
		const string ClientCookPlatform = "LinuxClient";

		if (bDedicatedServer)
		{
			return ServerCookPlatform;
		}
		else if (bIsClientOnly)
		{
			return ClientCookPlatform;
		}
		else
		{
			return NoEditorCookPlatform;
		}
	}

	public override string GetEditorCookPlatform()
	{
		return "Linux";
	}

	/// <summary>
	/// return true if we need to change the case of filenames outside of pak files
	/// </summary>
	/// <returns></returns>
	public override bool DeployLowerCaseFilenames()
	{
		return false;
	}

	/// <summary>
	/// Deploy the application on the current platform
	/// </summary>
	/// <param name="Params"></param>
	/// <param name="SC"></param>
	public override void Deploy(ProjectParams Params, DeploymentContext SC)
	{
		if (BuildHostPlatform.Current.Platform != UnrealTargetPlatform.Linux)
		{
			foreach (string DeviceAddress in Params.DeviceNames)
			{
				string CookPlatformName = GetCookPlatform(Params.DedicatedServer, Params.Client);
				string SourcePath = CombinePaths(Params.BaseStageDirectory, CookPlatformName);
				if (!Directory.Exists(SourcePath))
				{
					throw new AutomationException(string.Format("Source directory \"{0}\" must exist.", SourcePath));
				}

				string DestPath = "./" + Params.ShortProjectName;
				List<FileReference> Exes = GetExecutableNames(SC);
				string BinaryName = "";
				if (Exes.Count > 0)
				{
					// if stage directory does not end with "\\", insert one
					string Separator = "";
					if (Params.BaseStageDirectory.Length > 0 && (Params.BaseStageDirectory.EndsWith("/") || Params.BaseStageDirectory.EndsWith("\\")))
					{
						Separator = "/";
					}
					BinaryName = Exes[0].FullName.Replace(Params.BaseStageDirectory, DestPath + Separator);
					BinaryName = BinaryName.Replace("\\", "/");
				}

				// stage a shell script that makes running easy
				// Starting from 0, finds the first valid X11 connection xset is able to query
				// if it fails to find one it'll default to 0 and fail to connect.
				string Script = String.Format(@"#!/bin/bash
VALID_DISPLAY=0

for i in `seq 0 16`; do
    DISPLAY=:$i xset -q > /dev/null 2>&1
    if [ $? -eq 0 ]
    then
        VALID_DISPLAY=$i
        break
    fi
done

export DISPLAY=:$VALID_DISPLAY
chmod +x {0}
{0} $@
", BinaryName, BinaryName);
				Script = Script.Replace("\r\n", "\n");
				string ScriptFile = Path.Combine(SourcePath, "..", LaunchOnHelperShellScriptName);
				File.WriteAllText(ScriptFile, Script);

				if (!Params.IterativeDeploy)
				{
					// non-null input is essential, since RedirectStandardInput=true is needed for PLINK, see http://stackoverflow.com/questions/1910592/process-waitforexit-on-console-vs-windows-forms
					RunAndLog(CmdEnv, PlinkPath, String.Format("-batch -l {0} -pw {1} {2} rm -rf {3} && mkdir -p {3}", Params.DeviceUsername, Params.DevicePassword, Params.DeviceNames[0], DestPath), Input: "");
				}
				else
				{
					// still make sure that the path exists
					RunAndLog(CmdEnv, PlinkPath, String.Format("-batch -l {0} -pw {1} {2} mkdir -p {3}", Params.DeviceUsername, Params.DevicePassword, Params.DeviceNames[0], DestPath), Input: "");
				}

				// copy the contents
				RunAndLog(CmdEnv, PScpPath, String.Format("-batch -pw {0} -r \"{1}\" {2}", Params.DevicePassword, SourcePath, Params.DeviceUsername + "@" + DeviceAddress + ":" + DestPath));

				// copy the helper script
				RunAndLog(CmdEnv, PScpPath, String.Format("-batch -pw {0} -r \"{1}\" {2}", Params.DevicePassword, ScriptFile, Params.DeviceUsername + "@" + DeviceAddress + ":" + DestPath));

				string RemoteScriptFile = DestPath + "/" + LaunchOnHelperShellScriptName;
				// non-null input is essential, since RedirectStandardInput=true is needed for PLINK, see http://stackoverflow.com/questions/1910592/process-waitforexit-on-console-vs-windows-forms
				RunAndLog(CmdEnv, PlinkPath, String.Format(" -batch -l {0} -pw {1} {2} chmod +x {3}", Params.DeviceUsername, Params.DevicePassword, Params.DeviceNames[0], RemoteScriptFile), Input: "");
			}
		}
	}

	public override void Package(ProjectParams Params, DeploymentContext SC, int WorkingCL)
	{
		// package up the program
		PrintRunTime();
	}

	public override bool CanHostPlatform(UnrealTargetPlatform Platform)
	{
		if (Platform == UnrealTargetPlatform.Mac || Platform == UnrealTargetPlatform.Win32 || Platform == UnrealTargetPlatform.Win64)
		{
			return false;
		}
		return true;
	}

	public override void GetTargetFile(string RemoteFilePath, string LocalFile, ProjectParams Params)
	{
		var SourceFile = FileReference.Combine(new DirectoryReference(Params.BaseStageDirectory), GetCookPlatform(Params.HasServerCookedTargets, Params.HasClientTargetDetected), RemoteFilePath);
		CommandUtils.CopyFile(SourceFile.FullName, LocalFile);
	}

	/// <summary>
	/// Allow the platform to alter the ProjectParams
	/// </summary>
	/// <param name="ProjParams"></param>
	public override void PlatformSetupParams(ref ProjectParams ProjParams)
	{
		if ((ProjParams.Deploy || ProjParams.Run) && BuildHostPlatform.Current.Platform != UnrealTargetPlatform.Linux)
		{ 
			// Prompt for username if not already set
			while (String.IsNullOrEmpty(ProjParams.DeviceUsername))
			{
				Console.Write("Username: ");
				ProjParams.DeviceUsername = Console.ReadLine();
			}

			// Prompty for password if not already set
			while (String.IsNullOrEmpty(ProjParams.DevicePassword))
			{
				ProjParams.DevicePassword = String.Empty;
				Console.Write("Password: ");
				ConsoleKeyInfo key;
				do
				{
					key = Console.ReadKey(true);
					if (key.Key != ConsoleKey.Backspace && key.Key != ConsoleKey.Enter)
					{
						ProjParams.DevicePassword += key.KeyChar;
						Console.Write("*");
					}
					else
					{
						if (key.Key == ConsoleKey.Backspace && ProjParams.DevicePassword.Length > 0)
						{
							ProjParams.DevicePassword = ProjParams.DevicePassword.Substring(0, (ProjParams.DevicePassword.Length - 1));
							Console.Write("\b \b");
						}
					}

				} while (key.Key != ConsoleKey.Enter);
				Console.WriteLine();
			}

			// try contacting the device(s) and cache the key(s)
			foreach(string DeviceAddress in ProjParams.DeviceNames)
			{
				RunAndLog(CmdEnv, "cmd.exe", String.Format("/c \"echo y | {0} -ssh -t -l {1} -pw {2} {3} echo All Ok\"", PlinkPath, ProjParams.DeviceUsername, ProjParams.DevicePassword, DeviceAddress));
			}
		}
	}
	public override IProcessResult RunClient(ERunOptions ClientRunFlags, string ClientApp, string ClientCmdLine, ProjectParams Params)
	{
		if (BuildHostPlatform.Current.Platform != UnrealTargetPlatform.Linux)
		{
			IProcessResult Result = null;
			foreach (string DeviceAddress in Params.DeviceNames)
			{
				// Use the helper script (should already be +x - see Deploy)
				string RemoteBasePath = "./" + Params.ShortProjectName;
				string RemotePathToBinary = RemoteBasePath + "/" + LaunchOnHelperShellScriptName;
				// non-null input is essential, since RedirectStandardInput=true is needed for PLINK, see http://stackoverflow.com/questions/1910592/process-waitforexit-on-console-vs-windows-forms
				Result = Run(PlinkPath, String.Format("-batch -ssh -t -l {0} -pw {1} {2}  {3} {4}", Params.DeviceUsername, Params.DevicePassword, Params.DeviceNames[0], RemotePathToBinary, ClientCmdLine), "");
			}
			return Result;
		}
		else
		{
			return base.RunClient(ClientRunFlags, ClientApp, ClientCmdLine, Params);
		}
	}
	public override List<string> GetDebugFileExtensions()
	{
		return new List<string> { };
	}

	public override bool IsSupported { get { return true; } }

	public override void StripSymbols(FileReference SourceFile, FileReference TargetFile)
	{
		LinuxExports.StripSymbols(SourceFile, TargetFile);
	}
}


public class GenericLinuxPlatform : BaseLinuxPlatform
{
	public GenericLinuxPlatform()
		: base(UnrealTargetPlatform.Linux)
	{
	}
}
