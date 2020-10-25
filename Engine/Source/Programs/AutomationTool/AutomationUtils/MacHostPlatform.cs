// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Diagnostics;
using System.IO;
using UnrealBuildTool;
using System.Text.RegularExpressions;
using Tools.DotNETCommon;

namespace AutomationTool
{
	class MacHostPlatform : HostPlatform
	{
		static string CachedMsBuildTool = "";

		public override string GetMsBuildExe()
		{
			// As of 5.0 mono comes with msbuild which performs better. If that's installed then use it
			if (string.IsNullOrEmpty(CachedMsBuildTool))
			{
				bool CanUseMsBuild = string.IsNullOrEmpty(CommandUtils.WhichApp("msbuild")) == false;

				if (CanUseMsBuild)
				{
					CachedMsBuildTool = "msbuild";
				}
				else
				{
					Log.TraceInformation("Using xbuild. Install Mono 5.0 or greater for faster builds!");
					CachedMsBuildTool = "xbuild";
				}
			}

			return CachedMsBuildTool;
		}

		public override string RelativeBinariesFolder
		{
			get { return @"Engine/Binaries/Mac/"; }
		}

		public override string GetUE4ExePath(string UE4Exe)
		{
			if(Path.IsPathRooted(UE4Exe))
			{
				return CommandUtils.CombinePaths(UE4Exe);
			}

			int CmdExeIndex = UE4Exe.IndexOf("-Cmd.exe");
			if (CmdExeIndex != -1)
			{
				UE4Exe = UE4Exe.Substring(0, CmdExeIndex + 4);
			}
			else
			{
				CmdExeIndex = UE4Exe.IndexOf(".exe");
				if (CmdExeIndex != -1)
				{
					UE4Exe = UE4Exe.Substring(0, CmdExeIndex);
				}
			}

			if (UE4Exe.EndsWith("-Cmd", StringComparison.OrdinalIgnoreCase))
			{
				return CommandUtils.CombinePaths(CommandUtils.CmdEnv.LocalRoot, RelativeBinariesFolder, UE4Exe);
			}
			else
			{
				return CommandUtils.CombinePaths(CommandUtils.CmdEnv.LocalRoot, RelativeBinariesFolder, UE4Exe + ".app/Contents/MacOS", UE4Exe);
			}
		}

		public override string LocalBuildsLogFolder
		{
			get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Personal), "Library/Logs/Unreal Engine/LocalBuildLogs"); }
		}

		private string P4ExePath = null;

		public override string P4Exe
		{
			get
			{
				if (P4ExePath == null)
				{
					P4ExePath = "/usr/bin/p4";
					if (!File.Exists(P4ExePath))
					{
						P4ExePath = "/usr/local/bin/p4";
					}
				}
				return P4ExePath;
			}
		}

		public override Process CreateProcess(string AppName)
		{
			var NewProcess = new Process();
			if (AppName == "mono")
			{
				// Enable case-insensitive mode for Mono
				if (!NewProcess.StartInfo.EnvironmentVariables.ContainsKey("MONO_IOMAP"))
				{
					NewProcess.StartInfo.EnvironmentVariables.Add("MONO_IOMAP", "case");
				}
			}
			return NewProcess;
		}

		public override void SetupOptionsForRun(ref string AppName, ref CommandUtils.ERunOptions Options, ref string CommandLine)
		{
			if (AppName == "sh" || AppName == "xbuild" || AppName == "codesign")
			{
				Options &= ~CommandUtils.ERunOptions.AppMustExist;
			}
			if (AppName == "xbuild")
			{
				AppName = "sh";
				CommandLine = "-c 'xbuild " + (String.IsNullOrEmpty(CommandLine) ? "" : CommandLine) + " /p:DefineConstants=MONO /p:DefineConstants=__MonoCS__ /verbosity:quiet /nologo |grep -i error; if [ $? -ne 1 ]; then exit 1; else exit 0; fi'";
			}
			if (AppName.EndsWith(".exe") || ((AppName.Contains("/Binaries/Win64/") || AppName.Contains("/Binaries/Mac/")) && string.IsNullOrEmpty(Path.GetExtension(AppName))))
			{
				if (AppName.Contains("/Binaries/Win64/") || AppName.Contains("/Binaries/Mac/"))
				{
					AppName = AppName.Replace("/Binaries/Win64/", "/Binaries/Mac/");
					AppName = AppName.Replace("-cmd.exe", "");
					AppName = AppName.Replace("-Cmd.exe", "");
					AppName = AppName.Replace(".exe", "");
					string AppFilename = Path.GetFileName(AppName);
					if (!CommandUtils.FileExists(AppName) && AppName.IndexOf("/Contents/MacOS/") == -1)
					{
						AppName = AppName + ".app/Contents/MacOS/" + AppFilename;
					}
				}
				else
				{
					// It's a C# app, so run it with Mono
					CommandLine = "\"" + AppName + "\" " + (String.IsNullOrEmpty(CommandLine) ? "" : CommandLine);
					AppName = "mono";
					Options &= ~CommandUtils.ERunOptions.AppMustExist;
				}
			}
		}

		public override void SetConsoleCtrlHandler(ProcessManager.CtrlHandlerDelegate Handler)
		{
			// @todo: add mono support
		}

		public override bool IsScriptModuleSupported(string ModuleName)
		{
			// @todo: add more unsupported modules here
			List<string> UnsupportedModules = new List<string>()
			{
				"GauntletExtras", "Anvil", "WinAnvil", "XboxCommonAnvil", "XboxOneAnvil", "MPX",
				"FortniteGame",
			};
			foreach (string UnsupportedModule in UnsupportedModules)
			{
				if (ModuleName.StartsWith(UnsupportedModule, StringComparison.OrdinalIgnoreCase))
				{
					return false;
				}
			}
			return true;
		}

		public override UnrealTargetPlatform HostEditorPlatform
		{
			get { return UnrealTargetPlatform.Mac; }
		}

		public override string PdbExtension
		{
			get { return ".exe.mdb"; }
		}

		static string[] SystemServices = new string[]
		{
			// TODO: Add any system process names here
		};
		public override string[] DontKillProcessList
		{
			get
			{
				return SystemServices;
			}
		}
	}
}
