// Copyright Epic Games, Inc. All Rights Reserved.
using System;
using System.IO;
using System.Diagnostics;
using System.Collections.Generic;
using UnrealBuildTool;

public class Python : ModuleRules
{
	public Python(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);

		PythonSDKPaths PythonSDK = null;

		if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Mac || Target.Platform == UnrealTargetPlatform.Linux)
		{
			// Check for an explicit version before using the auto-detection logic
			var PythonRoot = System.Environment.GetEnvironmentVariable("UE_PYTHON_DIR");
			if (PythonRoot != null)
			{
				PythonSDK = DiscoverPythonSDK(PythonRoot);
				if (!PythonSDK.IsValid())
				{
					PythonSDK = null;
				}
			}
		}

		// Perform auto-detection to try and find the Python SDK
		if (PythonSDK == null)
		{
			var PotentialSDKs = GetPotentialPythonSDKs(Target);

			foreach (var PotentialSDK in PotentialSDKs)
			{
				if (PotentialSDK.IsValid())
				{
					PythonSDK = PotentialSDK;
					break;
				}
			}
		}

		// Make sure the Python SDK is the correct architecture
		if (PythonSDK != null)
		{
			string ExpectedPointerSizeResult = Target.Platform == UnrealTargetPlatform.Win32 ? "4" : "8";

			// Invoke Python to query the pointer size of the interpreter so we can work out whether it's 32-bit or 64-bit
			// todo: We probably need to do this for all platforms, but right now it's only an issue on Windows
			if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
			{
				string Result = InvokePython(PythonSDK.PythonRoot, "-c \"import struct; print(struct.calcsize('P'))\"");
				Result = Result != null ? Result.Replace("\r", "").Replace("\n", "") : null;
				if (Result == null || Result != ExpectedPointerSizeResult)
				{
					PythonSDK = null;
				}
			}
		}

		if (PythonSDK == null)
		{
			PublicDefinitions.Add("WITH_PYTHON=0");
			Console.WriteLine("Python SDK not found");
		}
		else
		{
			// If the Python install we're using is within the Engine directory, make the path relative so that it's portable
			string EngineRelativePythonRoot = PythonSDK.PythonRoot;
			var IsEnginePython = EngineRelativePythonRoot.StartsWith(EngineDir);
			if (IsEnginePython)
			{
				// Strip the Engine directory and then combine the path with the placeholder to ensure the path is delimited correctly
				EngineRelativePythonRoot = EngineRelativePythonRoot.Remove(0, EngineDir.Length);
				foreach(string FileName in Directory.EnumerateFiles(PythonSDK.PythonRoot, "*", SearchOption.AllDirectories))
				{
					if(!FileName.EndsWith(".pyc", System.StringComparison.OrdinalIgnoreCase))
					{
						RuntimeDependencies.Add(FileName);
					}
				}
				EngineRelativePythonRoot = Path.Combine("{ENGINE_DIR}", EngineRelativePythonRoot); // Can't use $(EngineDir) as the placeholder here as UBT is eating it
			}

			PublicDefinitions.Add("WITH_PYTHON=1");
			PublicDefinitions.Add(string.Format("UE_PYTHON_DIR=\"{0}\"", EngineRelativePythonRoot.Replace('\\', '/')));

			// Some versions of Python need this define set when building on MSVC
			if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
			{
				PublicDefinitions.Add("HAVE_ROUND=1");
			}

			PublicSystemIncludePaths.AddRange(PythonSDK.PythonIncludePaths);
			PublicAdditionalLibraries.AddRange(PythonSDK.PythonLibs);
			AppendPythonRuntimeDependencies(Target, IsEnginePython);
		}
	}

	private List<PythonSDKPaths> GetPotentialPythonSDKs(ReadOnlyTargetRules Target)
	{
		var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
		
		var PythonBinaryTPSDir = Path.Combine(EngineDir, "Binaries", "ThirdParty", "Python");
		var PythonSourceTPSDir = Path.Combine(EngineDir, "Source", "ThirdParty", "Python");

		var PotentialSDKs = new List<PythonSDKPaths>();

		// todo: This isn't correct for cross-compilation, we need to consider the host platform too
		if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
		{
			var PlatformDir = Target.Platform == UnrealTargetPlatform.Win32 ? "Win32" : "Win64";

			PotentialSDKs.AddRange(
				new PythonSDKPaths[] {
					new PythonSDKPaths(Path.Combine(PythonBinaryTPSDir, PlatformDir), new List<string>() { Path.Combine(PythonSourceTPSDir, PlatformDir, "include") }, new List<string>() { Path.Combine(PythonSourceTPSDir, PlatformDir, "libs", "python27.lib") }),
					//DiscoverPythonSDK("C:/Program Files/Python37"),
					DiscoverPythonSDK("C:/Python27"),
				}
			);
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PotentialSDKs.AddRange(
				new PythonSDKPaths[] {
					new PythonSDKPaths(Path.Combine(PythonBinaryTPSDir, "Mac"), new List<string>() { Path.Combine(PythonSourceTPSDir, "Mac", "include") }, new List<string>() { Path.Combine(PythonBinaryTPSDir, "Mac", "libpython2.7.dylib") }),
				}
			);
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			if (Target.Architecture.StartsWith("x86_64"))
			{
				var PlatformDir = Target.Platform.ToString();

				PotentialSDKs.AddRange(
					new PythonSDKPaths[] {
						new PythonSDKPaths(
							Path.Combine(PythonBinaryTPSDir, PlatformDir),
							new List<string>() {
								Path.Combine(PythonSourceTPSDir, PlatformDir, "include", "python2.7"),
								Path.Combine(PythonSourceTPSDir, PlatformDir, "include", Target.Architecture)
							},
							new List<string>() { Path.Combine(PythonSourceTPSDir, PlatformDir, "lib", "libpython2.7.a") }),
				});
				PublicSystemLibraries.Add("util");	// part of libc
			}
		}
		
		return PotentialSDKs;
	}

	private void AppendPythonRuntimeDependencies(ReadOnlyTargetRules Target, bool IsEnginePython)
	{
		if (Target.Platform == UnrealTargetPlatform.Linux && IsEnginePython)
		{
			RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/Python/Linux/lib/libpython2.7.so.1.0");
		}
	}

	private string InvokePython(string InPythonRoot, string InPythonArgs)
	{
		ProcessStartInfo ProcStartInfo = new ProcessStartInfo();
		ProcStartInfo.FileName = Path.Combine(InPythonRoot, "python");
		ProcStartInfo.WorkingDirectory = InPythonRoot;
		ProcStartInfo.Arguments = InPythonArgs;
		ProcStartInfo.UseShellExecute = false;
		ProcStartInfo.RedirectStandardOutput = true;

		try
		{
			using (Process Proc = Process.Start(ProcStartInfo))
			{
				using (StreamReader StdOutReader = Proc.StandardOutput)
				{
					return StdOutReader.ReadToEnd();
				}
			}
		}
		catch
		{
			return null;
		}
	}

	private PythonSDKPaths DiscoverPythonSDK(string InPythonRoot)
	{
		string PythonRoot = InPythonRoot;
		List<string> PythonIncludePaths = null;
		List<string> PythonLibs = null;

		// Work out the include path
		if (PythonRoot != null)
		{
			var PythonIncludePath = Path.Combine(PythonRoot, "include");
			if (Target.Platform == UnrealTargetPlatform.Mac)
			{
				// On Mac the actual headers are inside a "pythonxy" directory, where x and y are the version number
				if (Directory.Exists(PythonIncludePath))
				{
					string[] MatchingIncludePaths = Directory.GetDirectories(PythonIncludePath, "python*");
					if (MatchingIncludePaths.Length > 0)
					{
						PythonIncludePath = Path.Combine(PythonIncludePath, Path.GetFileName(MatchingIncludePaths[0]));
					}
				}
			}
			if (Directory.Exists(PythonIncludePath))
			{
				PythonIncludePaths = new List<string> { PythonIncludePath };
			}
			else
			{
				PythonRoot = null;
			}
		}

		// Work out the lib path
		if (PythonRoot != null)
		{
			string LibFolder = null;
			string LibNamePattern = null;
			if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
			{
				LibFolder = "libs";
				LibNamePattern = "python*.lib";
			}
			else if (Target.Platform == UnrealTargetPlatform.Mac)
			{
				LibFolder = "lib";
				LibNamePattern = "libpython*.dylib";
			}
			else if (Target.Platform == UnrealTargetPlatform.Linux)
			{
				LibFolder = "lib";
				LibNamePattern = "libpython*.so";
			}

			if (LibFolder != null && LibNamePattern != null)
			{
				var PythonLibPath = Path.Combine(PythonRoot, LibFolder);

				if (Directory.Exists(PythonLibPath))
				{
					string[] MatchingLibFiles = Directory.GetFiles(PythonLibPath, LibNamePattern);
					if (MatchingLibFiles.Length > 0)
					{
						PythonLibs = new List<string>();
						foreach (var MatchingLibFile in MatchingLibFiles)
						{
							PythonLibs.Add(MatchingLibFile);
						}
					}
				}
			}

			if (PythonLibs == null)
			{
				PythonRoot = null;
			}
		}

		return new PythonSDKPaths(PythonRoot, PythonIncludePaths, PythonLibs);
	}

	private class PythonSDKPaths
	{
		public PythonSDKPaths(string InPythonRoot, List<string> InPythonIncludePaths, List<string> InPythonLibs)
		{
			PythonRoot = InPythonRoot;
			PythonIncludePaths = InPythonIncludePaths;
			PythonLibs = InPythonLibs;
		}

		public bool IsValid()
		{
			return PythonRoot != null && Directory.Exists(PythonRoot);
		}

		public string PythonRoot;
		public List<string> PythonIncludePaths;
		public List<string> PythonLibs;
	};
}
