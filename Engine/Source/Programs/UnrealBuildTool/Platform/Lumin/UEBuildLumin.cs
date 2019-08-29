// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.Diagnostics;
using System.IO;
using System.Xml;
using System.Linq;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// Lumin-specific target settings
	/// </summary>	
	public class LuminTargetRules
	{
		/// <summary>
		/// Lists GPU Architectures that you want to build (mostly used for mobile etc.)
		/// </summary>
		[CommandLine("-GPUArchitectures=", ListSeparator = '+')]
		public List<string> GPUArchitectures = new List<string>();
	}

	/// <summary>
	/// Read-only wrapper for Android-specific target settings
	/// </summary>
	public class ReadOnlyLuminTargetRules
	{
		/// <summary>
		/// The private mutable settings object
		/// </summary>
		private LuminTargetRules Inner;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Inner">The settings object to wrap</param>
		public ReadOnlyLuminTargetRules(LuminTargetRules Inner)
		{
			this.Inner = Inner;
		}

		/// <summary>
		/// Accessors for fields on the inner TargetRules instance
		/// </summary>
		#region Read-only accessor properties 
#if !__MonoCS__
#pragma warning disable CS1591
#endif

		public IReadOnlyList<string> GPUArchitectures
		{
			get { return Inner.GPUArchitectures.AsReadOnly(); }
		}

#if !__MonoCS__
#pragma warning restore CS1591
#endif
		#endregion
	}
	class LuminPlatform : AndroidPlatform
	{
		public LuminPlatform(AndroidPlatformSDK InSDK)
			: base(UnrealTargetPlatform.Lumin, CppPlatform.Lumin, InSDK)
		{
		}

		public override bool CanUseXGE()
		{
			return true;
		}

		public override bool HasSpecificDefaultBuildConfig(UnrealTargetPlatform Platform, DirectoryReference ProjectPath)
		{
			string[] BoolKeys = new string[]
			{
				"bBuildWithNvTegraGfxDebugger",
				// @todo Lumin: Once we switch to Vulkan only, this will no longer be needed, since Vulkan can do both without needing to recompile
				"bUseMobileRendering",
			};

			// look up Android specific settings
			// @todo Lumin: When we subclass platform ini's, this would be Platform!!
			if (!DoProjectSettingsMatchDefault(UnrealTargetPlatform.Lumin, ProjectPath, "/Script/LuminRuntimeSettings.LuminRuntimeSettings", BoolKeys, null, null))
			{
				return false;
			}

			return true;
		}


		/// <summary>
		/// Modify the rules for a newly created module, where the target is a different host platform.
		/// This is not required - but allows for hiding details of a particular platform.
		/// </summary>
		/// <param name="ModuleName">The name of the module</param>
		/// <param name="Rules">The module rules</param>
		/// <param name="Target">The target being build</param>
		public override void ModifyModuleRulesForOtherPlatform(string ModuleName, ModuleRules Rules, ReadOnlyTargetRules Target)
		{
			if ((Target.Platform == UnrealTargetPlatform.Win32) || (Target.Platform == UnrealTargetPlatform.Win64) || (Target.Platform == UnrealTargetPlatform.Mac) || (Target.Platform == UnrealTargetPlatform.Linux))
			{
				bool bBuildShaderFormats = Target.bForceBuildShaderFormats;
				if (!Target.bBuildRequiresCookedData)
				{
					if (ModuleName == "Engine")
					{
						if (Target.bBuildDeveloperTools)
						{
							Rules.DynamicallyLoadedModuleNames.Add("LuminTargetPlatform");
						}
					}
					else if (ModuleName == "TargetPlatform")
					{
						bBuildShaderFormats = true;
						Rules.DynamicallyLoadedModuleNames.Add("TextureFormatASTC");
					}
				}

				// allow standalone tools to use targetplatform modules, without needing Engine
				if (ModuleName == "TargetPlatform")
				{
					if (Target.bForceBuildTargetPlatforms)
					{
						Rules.DynamicallyLoadedModuleNames.Add("LuminTargetPlatform");
					}
				}
			}
		}

		public override void AddExtraModules(ReadOnlyTargetRules Target, List<string> PlatformExtraModules)
		{
			// 			PlatformExtraModules.Add("VulkanRHI");
			PlatformExtraModules.Add("MagicLeapAudio");
		}

		/// <summary>
		/// Modify the rules for a newly created module, in a target that's being built for this platform.
		/// This is not required - but allows for hiding details of a particular platform.
		/// </summary>
		/// <param name="ModuleName">The name of the module</param>
		/// <param name="Rules">The module rules</param>
		/// <param name="Target">The target being build</param>
		public override void ModifyModuleRulesForActivePlatform(string ModuleName, ModuleRules Rules, ReadOnlyTargetRules Target)
		{
			// may need to put some stuff in here to keep Lumin out of other module .cs files
		}

		public override void SetUpSpecificEnvironment(ReadOnlyTargetRules Target, CppCompileEnvironment CompileEnvironment, LinkEnvironment LinkEnvironment)
		{
			CompileEnvironment.Definitions.Add("PLATFORM_LUMIN=1");
			CompileEnvironment.Definitions.Add("USE_ANDROID_JNI=0");
			CompileEnvironment.Definitions.Add("USE_ANDROID_FILE=0");
			CompileEnvironment.Definitions.Add("USE_ANDROID_INPUT=0");
			CompileEnvironment.Definitions.Add("USE_ANDROID_LAUNCH=0");
			CompileEnvironment.Definitions.Add("USE_ANDROID_EVENTS=0");
			CompileEnvironment.Definitions.Add("USE_ANDROID_OPENGL=0");
			CompileEnvironment.Definitions.Add("WITH_OGGVORBIS=1");

			DirectoryReference MLSDKDir = DirectoryReference.MakeFromNormalizedFullPath("$(MLSDK)");
			CompileEnvironment.IncludePaths.SystemIncludePaths.Add(DirectoryReference.Combine(MLSDKDir, "lumin/stl/gnu-libstdc++/include"));
			CompileEnvironment.IncludePaths.SystemIncludePaths.Add(DirectoryReference.Combine(MLSDKDir, "lumin/stl/gnu-libstdc++/include/aarch64-linux-android"));
			CompileEnvironment.IncludePaths.SystemIncludePaths.Add(DirectoryReference.Combine(MLSDKDir, "include"));

			LinkEnvironment.LibraryPaths.Add(DirectoryReference.Combine(MLSDKDir, "lib/lumin"));
			LinkEnvironment.LibraryPaths.Add(DirectoryReference.Combine(MLSDKDir, "lumin/stl/gnu-libstdc++/lib"));

			if (!UseTegraGraphicsDebugger(Target.ProjectFile) || !UseTegraDebuggerStub(Target.ProjectFile))
			{
				LinkEnvironment.AdditionalLibraries.Add("GLESv2");
				LinkEnvironment.AdditionalLibraries.Add("EGL");
			}
			if (UseTegraDebuggerStub(Target.ProjectFile))
			{
				DirectoryReference TegraDebuggerDirectoryReference = new DirectoryReference(TegraDebuggerDir);

				LinkEnvironment.LibraryPaths.Add(DirectoryReference.Combine(TegraDebuggerDirectoryReference, "/target/android-kk-egl-t124-a32"));
				LinkEnvironment.LibraryPaths.Add(DirectoryReference.Combine(TegraDebuggerDirectoryReference, "/target/android-L-egl-t132-a64"));
				LinkEnvironment.AdditionalLibraries.Add("Nvidia_gfx_debugger_stub");
			}

			LinkEnvironment.AdditionalLibraries.Add("ml_lifecycle");
			LinkEnvironment.AdditionalLibraries.Add("ml_ext_logging");
			LinkEnvironment.AdditionalLibraries.Add("ml_dispatch");
		}


        public static bool? HaveTegraGraphicsDebugger = null;
        public static string TegraDebuggerDir = null;
        public static int[] TegraDebuggerVersion = null;

        public static bool UseTegraGraphicsDebugger(FileReference ProjectFile)
        {
            if (!HaveTegraGraphicsDebugger.HasValue)
            {
                string ProgramsDir = Environment.GetEnvironmentVariable("ProgramFiles(x86)");
                string NVDir = ProgramsDir + "/NVIDIA Corporation";
                try
                {
                    string[] TegraDebuggerDirs = Directory.GetDirectories(NVDir, "Tegra Graphics Debugger *");
                    if (TegraDebuggerDirs.Length > 0)
                    {
                        TegraDebuggerDir = TegraDebuggerDirs[0].Replace('\\', '/');
                        HaveTegraGraphicsDebugger = true;
                        string[] V = TegraDebuggerDir.Split(' ').Last().Split('.');
                        TegraDebuggerVersion = new int[2];
                        TegraDebuggerVersion[0] = Int32.Parse(V[0]);
                        TegraDebuggerVersion[1] = Int32.Parse(V[1]);
                    }
                    else
                    {
                        HaveTegraGraphicsDebugger = false;
                    }
                }
                catch (System.IO.IOException)
                {
                    HaveTegraGraphicsDebugger = false;
                }
            }
            bool bBuild = false;
			// TODO: do we need this?
            ConfigHierarchy Ini = ConfigCache.ReadHierarchy(ConfigHierarchyType.Engine, DirectoryReference.FromFile(ProjectFile), UnrealTargetPlatform.Lumin);
            Ini.GetBool(
                "/Script/LuminRuntimeSettings.LuminRuntimeSettings",
                "bBuildWithNvTegraGfxDebugger",
                out bBuild);
            return HaveTegraGraphicsDebugger.Value && bBuild;
        }


		public static bool UseTegraDebuggerStub(FileReference ProjectFile)
		{
			return UseTegraGraphicsDebugger(ProjectFile) &&
				TegraDebuggerVersion[0] <= 2 && TegraDebuggerVersion[1] <= 1;
		}

		public override UEToolChain CreateToolChain(CppPlatform CppPlatform, ReadOnlyTargetRules Target)
		{
			bool bUseLdGold = Target.bUseUnityBuild;
			return new LuminToolChain(Target.ProjectFile, false, null, Target.LuminPlatform.GPUArchitectures, true);
		}
		public override UEToolChain CreateTempToolChainForProject(FileReference ProjectFile)
		{
			return new LuminToolChain(ProjectFile);
		}

		/// <summary>
		/// Deploys the given target
		/// </summary>
		/// <param name="Target">Information about the target being deployed</param>
		public override void Deploy(UEBuildDeployTarget Target)
		{
			new UEDeployLumin(Target.ProjectFile).PrepTargetForDeployment(Target);
		}

		public override void ValidateTarget(TargetRules Target)
		{
			base.ValidateTarget(Target);

			// #todo: ICU is crashing on startup, this is a workaround
			Target.bCompileICU = false;
		}
	}

	class LuminPlatformSDK : AndroidPlatformSDK
	{
		/// <summary>
		/// This is the SDK version we support
		/// </summary>
		static string ExpectedSDKVersion = "0.12";   // now unified for all the architectures

		public override string GetSDKTargetPlatformName()
		{
			return "Lumin";
		}

		protected override string GetRequiredSDKString()
		{
			return ExpectedSDKVersion;
		}

		private string FindVersionNumber(string StringToFind, string[] AllLines)
		{
			string FoundVersion = "Unknown";
			foreach (string CurrentLine in AllLines)
			{
				int Index = CurrentLine.IndexOf(StringToFind);
				if (Index != -1)
				{
					FoundVersion = CurrentLine.Substring(Index + StringToFind.Length);
					break;
				}
			}
			return FoundVersion.Trim();
		}

		/// <summary>
		/// checks if the sdk is installed or has been synced, sets environment variable
		/// </summary>
		/// <returns></returns>
		protected override bool HasAnySDK()
		{
			string EnvVarKey = "MLSDK";
			string IniVarKey = "MLSDKPath";

			string MLSDKPath = Environment.GetEnvironmentVariable(EnvVarKey);
			{
				var configCacheIni = ConfigCache.ReadHierarchy(ConfigHierarchyType.Engine, (DirectoryReference)null, UnrealTargetPlatform.Unknown);

				string path;
				if (GetPath(configCacheIni, "/Script/LuminPlatformEditor.MagicLeapSDKSettings", IniVarKey, out path) && !string.IsNullOrEmpty(path))
				{
					// if the folder specified by the config var doesn't exist, fall back to the env var.
					if (Directory.Exists(path))
					{
						MLSDKPath = path;
					}
				}

				// Set for the process
				Environment.SetEnvironmentVariable(EnvVarKey, MLSDKPath);
			}

			// we don't have an MLSDK directory specified
			if (String.IsNullOrEmpty(MLSDKPath))
			{
				return false;
			}
			// we don't have the required MLSDK setup
			String DetectedVersion = "Unknown";
			String VersionFile = string.Format("{0}/include/ml_version.h", MLSDKPath).Replace('/', Path.DirectorySeparatorChar);
			if (File.Exists(VersionFile))
			{
				string[] VersionText = File.ReadAllLines(VersionFile);

				String MajorVersion = FindVersionNumber("MLSDK_VERSION_MAJOR", VersionText);
				String MinorVersion = FindVersionNumber("MLSDK_VERSION_MINOR", VersionText);
				DetectedVersion = string.Format("{0}.{1}", MajorVersion, MinorVersion);
			}
			if (!DetectedVersion.Equals(GetRequiredSDKString()))
			{
				Console.WriteLine("*** Found installed MLSDK version {0} but require {1} ***", DetectedVersion, GetRequiredSDKString());
				return false;
			}

			return true;
		}

		protected override SDKStatus HasRequiredManualSDKInternal()
		{
			// if any autosdk setup has been done then the local process environment is suspect
			if (HasSetupAutoSDK())
			{
				return SDKStatus.Invalid;
			}

			if (HasAnySDK())
			{
				return SDKStatus.Valid;
			}

			return SDKStatus.Invalid;
		}
	}

	class LuminPlatformFactory : UEBuildPlatformFactory
	{
		protected override UnrealTargetPlatform TargetPlatform
		{
			get { return UnrealTargetPlatform.Lumin; }
		}

		protected override void RegisterBuildPlatforms(SDKOutputLevel OutputLevel)
		{
			LuminPlatformSDK SDK = new LuminPlatformSDK();
			SDK.ManageAndValidateSDK(OutputLevel);

			if ((ProjectFileGenerator.bGenerateProjectFiles == true) || (SDK.HasRequiredSDKsInstalled() == SDKStatus.Valid) || Environment.GetEnvironmentVariable("IsBuildMachine") == "1")
			{
				bool bRegisterBuildPlatform = true;

				FileReference TargetPlatformFile = FileReference.Combine(UnrealBuildTool.EngineSourceDirectory, "Developer", "Lumin", "LuminTargetPlatform", "LuminTargetPlatform.Build.cs");
				if (FileReference.Exists(TargetPlatformFile) == false)
				{
					bRegisterBuildPlatform = false;
				}

				if (bRegisterBuildPlatform == true)
				{
					// Register this build platform
					Log.TraceVerbose("        Registering for {0}", UnrealTargetPlatform.Lumin.ToString());
					UEBuildPlatform.RegisterBuildPlatform(new LuminPlatform(SDK));
					UEBuildPlatform.RegisterPlatformWithGroup(UnrealTargetPlatform.Lumin, UnrealPlatformGroup.Android);
				}
			}
		}
	}

}
