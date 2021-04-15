// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Xml;
using Tools.DotNETCommon;
using Microsoft.Win32;

namespace UnrealBuildTool
{
	/// <summary>
	/// IOS-specific target settings
	/// </summary>
	public partial class IOSTargetRules
	{
		/// <summary>
		/// Whether to strip iOS symbols or not (implied by Shipping config).
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		[CommandLine("-stripsymbols", Value = "true")]
		public bool bStripSymbols = false;

		/// <summary>
		///
		/// </summary>
		public bool bShipForBitcode = false;

		/// <summary>
		/// If true, then a stub IPA will be generated when compiling is done (minimal files needed for a valid IPA).
		/// </summary>
		[CommandLine("-CreateStub", Value = "true")]
		public bool bCreateStubIPA = false;

		/// <summary>
		/// Whether to generate a native Xcode project as a wrapper for the framework.
		/// </summary>
		public bool bGenerateFrameworkWrapperProject = false;

		/// <summary>
		/// Don't generate crashlytics data
		/// </summary>
		[CommandLine("-alwaysgeneratedsym", Value = "true")]
		public bool bGeneratedSYM = false;

		/// <summary>
		/// Don't generate crashlytics data
		/// </summary>
		[CommandLine("-skipcrashlytics")]
		public bool bSkipCrashlytics = false;

		/// <summary>
		/// Mark the build for distribution
		/// </summary>
		[CommandLine("-distribution")]
		public bool bForDistribution = false;

		/// <summary>
		/// Manual override for the provision to use. Should be a full path.
		/// </summary>
		[CommandLine("-ImportProvision=")]
		public string ImportProvision = null;

		/// <summary>
		/// Imports the given certificate (inc private key) into a temporary keychain before signing.
		/// </summary>
		[CommandLine("-ImportCertificate=")]
		public string ImportCertificate = null;

		/// <summary>
		/// Password for the imported certificate
		/// </summary>
		[CommandLine("-ImportCertificatePassword=")]
		public string ImportCertificatePassword = null;

		/// <summary>
		/// Cached project settings for the target (set in ResetTarget)
		/// </summary>
		public IOSProjectSettings ProjectSettings = null;
	}

	/// <summary>
	/// Read-only wrapper for IOS-specific target settings
	/// </summary>
	public partial class ReadOnlyIOSTargetRules
	{
		/// <summary>
		/// The private mutable settings object
		/// </summary>
		private IOSTargetRules Inner;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Inner">The settings object to wrap</param>
		public ReadOnlyIOSTargetRules(IOSTargetRules Inner)
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
		public bool bStripSymbols
		{
			get { return Inner.bStripSymbols; }
		}
			
		public bool bShipForBitcode
		{
			get { return Inner.ProjectSettings.bShipForBitcode; }
		}

		public bool bGenerateFrameworkWrapperProject
		{
			get { return Inner.bGenerateFrameworkWrapperProject; }
		}

		public bool bGeneratedSYM
		{
			get { return Inner.bGeneratedSYM; }
		}

		public bool bCreateStubIPA
		{
			get { return Inner.bCreateStubIPA; }
		}

		public bool bSkipCrashlytics
		{
			get { return Inner.bSkipCrashlytics; }
		}

		public bool bForDistribution
		{
			get { return Inner.bForDistribution; }
		}

		public string ImportProvision
		{
			get { return Inner.ImportProvision; }
		}

		public string ImportCertificate
		{
			get { return Inner.ImportCertificate; }
		}

		public string ImportCertificatePassword
		{
			get { return Inner.ImportCertificatePassword; }
		}

		public float RuntimeVersion
		{
			get { return float.Parse(Inner.ProjectSettings.RuntimeVersion, System.Globalization.CultureInfo.InvariantCulture); }
		}
		
#if !__MonoCS__
#pragma warning restore CS1591
#endif
		#endregion
	}

	/// <summary>
	/// Stores project-specific IOS settings. Instances of this object are cached by IOSPlatform.
	/// </summary>
	public class IOSProjectSettings
	{
		/// <summary>
		/// The cached project file location
		/// </summary>
		public readonly FileReference ProjectFile;

		/// <summary>
		/// Whether to build the iOS project as a framework.
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bBuildAsFramework")]
		[CommandLine("-build-as-framework")]
		public readonly bool bBuildAsFramework = false;

		/// <summary>
		/// Whether to generate a native Xcode project as a wrapper for the framework.
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bGenerateFrameworkWrapperProject")]
		public readonly bool bGenerateFrameworkWrapperProject = false;

		/// <summary>
		/// Whether to generate a dSYM file or not.
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bGeneratedSYMFile")]
		[CommandLine("-generatedsymfile")]
		public readonly bool bGeneratedSYMFile = false;
		
		/// <summary>
		/// Whether to generate a dSYM bundle (as opposed to single file dSYM)
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bGeneratedSYMBundle")]
		[CommandLine("-generatedsymbundle")]
		public readonly bool bGeneratedSYMBundle = false;

        /// <summary>
        /// Whether to generate a dSYM file or not.
        /// </summary>
        [ConfigFile(ConfigHierarchyType.Engine, "/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bGenerateCrashReportSymbols")]
        public readonly bool bGenerateCrashReportSymbols = false;

        /// <summary>
        /// The minimum supported version
        /// </summary>
        [ConfigFile(ConfigHierarchyType.Engine, "/Script/IOSRuntimeSettings.IOSRuntimeSettings", "MinimumiOSVersion")]
		private readonly string MinimumIOSVersion = null;

		/// <summary>
		/// Whether to support iPhone
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bSupportsIPhone")]
		private readonly bool bSupportsIPhone = true;

		/// <summary>
		/// Whether to support iPad
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bSupportsIPad")]
		private readonly bool bSupportsIPad = true;

		/// <summary>
		/// additional linker flags for shipping
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/IOSRuntimeSettings.IOSRuntimeSettings", "AdditionalShippingLinkerFlags")]
		public readonly string AdditionalShippingLinkerFlags = "";

		/// <summary>
		/// additional linker flags for non-shipping
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/IOSRuntimeSettings.IOSRuntimeSettings", "AdditionalLinkerFlags")]
		public readonly string AdditionalLinkerFlags = "";

		/// <summary>
		/// mobile provision to use for code signing
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/IOSRuntimeSettings.IOSRuntimeSettings", "MobileProvision")]
		public readonly string MobileProvision = "";

        /// <summary>
        /// signing certificate to use for code signing
        /// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/IOSRuntimeSettings.IOSRuntimeSettings", "SigningCertificate")]
        public readonly string SigningCertificate = "";


		/// <summary>
		/// true if bit code should be embedded
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bShipForBitcode")]
		public readonly bool bShipForBitcode = false;

        /// <summary>
        /// true if notifications are enabled
        /// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bEnableRemoteNotificationsSupport")]
        public readonly bool bNotificationsEnabled = false;

        /// <summary>
        /// true if notifications are enabled
        /// </summary>
        [ConfigFile(ConfigHierarchyType.Engine, "/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bEnableBackgroundFetch")]
        public readonly bool bBackgroundFetchEnabled = false;

		/// <summary>
		/// true if iTunes file sharing support is enabled
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bSupportsITunesFileSharing")]
		public readonly bool bFileSharingEnabled = false;

		/// <summary>
		/// The bundle identifier
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/IOSRuntimeSettings.IOSRuntimeSettings", "BundleIdentifier")]
		public readonly string BundleIdentifier = "";

		/// <summary>
		/// true if using Xcode managed provisioning, else false
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bAutomaticSigning")]
		public readonly bool bAutomaticSigning = false;

		/// <summary>
		/// The IOS Team ID
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/IOSRuntimeSettings.IOSRuntimeSettings", "IOSTeamID")]
		public readonly string TeamID = "";

		/// <summary>
		/// true to change FORCEINLINE to a regular INLINE.
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bDisableForceInline")]
		public readonly bool bDisableForceInline = false;
		
		/// <summary>
		/// true if IDFA are enabled
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bEnableAdvertisingIdentifier")]
		public readonly bool bEnableAdvertisingIdentifier = false;

		/// <summary>
		/// true when building for distribution
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Game, "/Script/UnrealEd.ProjectPackagingSettings", "ForDistribution")]
		public readonly bool bForDistribution = false;

		/// <summary>
		/// Returns a list of all the non-shipping architectures which are supported
		/// </summary>
		public IEnumerable<string> NonShippingArchitectures
		{
			get
			{
				yield return "arm64";
			}
		}

		/// <summary>
		/// Returns a list of all the shipping architectures which are supported
		/// </summary>
		public IEnumerable<string> ShippingArchitectures
		{
			get
			{
				yield return "arm64";
			}
		}

		/// <summary>
		/// Which version of the iOS to allow at run time
		/// </summary>
		public virtual string RuntimeVersion
		{
			get
			{
				switch (MinimumIOSVersion)
				{
					case "IOS_12":
						return "12.0";
					case "IOS_13":
						return "13.0";
					case "IOS_14":
						return "14.0";
					default:
						return "12.0";
				}
			}
		}

		/// <summary>
		/// which devices the game is allowed to run on
		/// </summary>
		public virtual string RuntimeDevices
		{
			get
			{
				if (bSupportsIPad && !bSupportsIPhone)
				{
					return "2";
				}
				else if (!bSupportsIPad && bSupportsIPhone)
				{
					return "1";
				}
				else
				{
					return "1,2";
				}
			}
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="ProjectFile">The project file to read settings for</param>
		/// <param name="Bundle">Bundle identifier needed when project file is empty</param>
		public IOSProjectSettings(FileReference ProjectFile, string Bundle) 
			: this(ProjectFile, UnrealTargetPlatform.IOS, Bundle)
		{
		}

		/// <summary>
		/// Protected constructor. Used by TVOSProjectSettings.
		/// </summary>
		/// <param name="ProjectFile">The project file to read settings for</param>
		/// <param name="Platform">The platform to read settings for</param>
		/// <param name="Bundle">Bundle identifier needed when project file is empty</param>
		protected IOSProjectSettings(FileReference ProjectFile, UnrealTargetPlatform Platform, string Bundle)
		{
			this.ProjectFile = ProjectFile;
			ConfigCache.ReadSettings(DirectoryReference.FromFile(ProjectFile), Platform, this);
			if ((ProjectFile == null || string.IsNullOrEmpty(ProjectFile.FullName)) && !string.IsNullOrEmpty(Bundle))
			{
				BundleIdentifier = Bundle;
			}
			BundleIdentifier = BundleIdentifier.Replace("[PROJECT_NAME]", ((ProjectFile != null) ? ProjectFile.GetFileNameWithoutAnyExtensions() : "UE4Game")).Replace("_", "");
		}
	}

	/// <summary>
	/// IOS provisioning data
	/// </summary>
    class IOSProvisioningData
    {
		public string SigningCertificate;
		public FileReference MobileProvisionFile;
        public string MobileProvisionUUID;
        public string MobileProvisionName;
        public string TeamUUID;
		public string BundleIdentifier;
		public bool bHaveCertificate = false;

		public string MobileProvision
		{
			get { return (MobileProvisionFile == null)? null : MobileProvisionFile.GetFileName(); }
		}

		public IOSProvisioningData(IOSProjectSettings ProjectSettings, bool bForDistribution)
			: this(ProjectSettings, false, bForDistribution)
		{
		}

		protected IOSProvisioningData(IOSProjectSettings ProjectSettings, bool bIsTVOS, bool bForDistribtion)
		{
            SigningCertificate = ProjectSettings.SigningCertificate;
            string MobileProvision = ProjectSettings.MobileProvision;

			FileReference ProjectFile = ProjectSettings.ProjectFile;
			FileReference IPhonePackager = FileReference.Combine(UnrealBuildTool.EngineDirectory, "Binaries/DotNET/IOS/IPhonePackager.exe");

			if (!string.IsNullOrEmpty(SigningCertificate))
            {
                // verify the certificate
                Process IPPProcess = new Process();

				string IPPCmd = "certificates " + ((ProjectFile != null) ? ("\"" + ProjectFile.ToString() + "\"") : "Engine") + " -bundlename " + ProjectSettings.BundleIdentifier + (bForDistribtion ? " -distribution" : "");
				
				IPPProcess.StartInfo.WorkingDirectory = UnrealBuildTool.EngineDirectory.ToString();
				IPPProcess.OutputDataReceived += new DataReceivedEventHandler(IPPDataReceivedHandler);
				IPPProcess.ErrorDataReceived += new DataReceivedEventHandler(IPPDataReceivedHandler);

				if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Mac)
                {
                    IPPProcess.StartInfo.FileName = FileReference.Combine(UnrealBuildTool.EngineDirectory, "Build/BatchFiles/Mac/RunMono.sh").FullName;
					IPPProcess.StartInfo.Arguments = string.Format("\"{0}\" {1}", IPhonePackager ,IPPCmd);
				}
                else
                {
					IPPProcess.StartInfo.FileName = IPhonePackager.FullName;
					IPPProcess.StartInfo.Arguments = IPPCmd;
                }

				Log.TraceInformation("Getting certifcate information via {0} {1}", IPPProcess.StartInfo.FileName, IPPProcess.StartInfo.Arguments);
                Utils.RunLocalProcess(IPPProcess);
            }
            else
            {
                SigningCertificate = bForDistribtion ? "iPhone Distribution" : "iPhone Developer";
                bHaveCertificate = true;
            }

			if(!string.IsNullOrEmpty(MobileProvision))
			{
				DirectoryReference MobileProvisionDir;
				if(BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Mac)
				{
					MobileProvisionDir = DirectoryReference.Combine(new DirectoryReference(Environment.GetEnvironmentVariable("HOME")), "Library", "MobileDevice", "Provisioning Profiles");
				}
				else
				{
					MobileProvisionDir = DirectoryReference.Combine(DirectoryReference.GetSpecialFolder(Environment.SpecialFolder.LocalApplicationData), "Apple Computer", "MobileDevice", "Provisioning Profiles");
				}

				FileReference PossibleMobileProvisionFile = FileReference.Combine(MobileProvisionDir, MobileProvision);
				if(FileReference.Exists(PossibleMobileProvisionFile))
				{
					MobileProvisionFile = PossibleMobileProvisionFile;
				}
			}

            if (MobileProvisionFile == null || !bHaveCertificate)
            {

                SigningCertificate = "";
                MobileProvision = "";
				MobileProvisionFile = null;
                Log.TraceLog("Provision not specified or not found for " + ((ProjectFile != null) ? ProjectFile.GetFileNameWithoutAnyExtensions() : "UE4Game") + ", searching for compatible match...");
                Process IPPProcess = new Process();

				IPPProcess.OutputDataReceived += new DataReceivedEventHandler(IPPDataReceivedHandler);
				IPPProcess.ErrorDataReceived += new DataReceivedEventHandler(IPPDataReceivedHandler);
				IPPProcess.StartInfo.WorkingDirectory = UnrealBuildTool.EngineDirectory.ToString();

				string IPPCmd = "signing_match " + ((ProjectFile != null) ? ("\"" + ProjectFile.ToString() + "\"") : "Engine") + " -bundlename " + ProjectSettings.BundleIdentifier + (bIsTVOS ? " -tvos" : "") + (bForDistribtion ? " -distribution" : "");

				if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Mac)
				{
					IPPProcess.StartInfo.FileName = FileReference.Combine(UnrealBuildTool.EngineDirectory, "Build/BatchFiles/Mac/RunMono.sh").FullName;
					IPPProcess.StartInfo.Arguments = string.Format("\"{0}\" {1}", IPhonePackager, IPPCmd);
				}
				else
				{
					IPPProcess.StartInfo.FileName = IPhonePackager.FullName;
					IPPProcess.StartInfo.Arguments = IPPCmd;
				}

				Log.TraceInformation("Getting signing information via {0} {1}", IPPProcess.StartInfo.FileName, IPPProcess.StartInfo.Arguments);

				Utils.RunLocalProcess(IPPProcess);
				if(MobileProvisionFile != null)
				{
					Log.TraceLog("Provision found for " + ((ProjectFile != null) ? ProjectFile.GetFileNameWithoutAnyExtensions() : "UE4Game") + ", Provision: " + MobileProvisionFile + " Certificate: " + SigningCertificate);
				}
            }

            // add to the dictionary
            SigningCertificate = SigningCertificate.Replace("\"", "");

            // read the provision to get the UUID
			if(MobileProvisionFile == null)
			{
				Log.TraceLog("No matching provision file was discovered for {0}. Please ensure you have a compatible provision installed.", ProjectFile);
			}
			else if(!FileReference.Exists(MobileProvisionFile))
			{
				Log.TraceLog("Selected mobile provision for {0} ({1}) was not found. Please ensure you have a compatible provision installed.", ProjectFile, MobileProvisionFile);
			}
			else
            {
				byte[] AllBytes = FileReference.ReadAllBytes(MobileProvisionFile);

				uint StartIndex = (uint)AllBytes.Length;
				uint EndIndex = (uint)AllBytes.Length;

				for (uint i = 0; i + 4 < AllBytes.Length; i++)
				{
					if (AllBytes[i] == '<' && AllBytes[i+1] == '?' && AllBytes[i+ 2] == 'x' && AllBytes[i+ 3] == 'm' && AllBytes[i+ 4] == 'l')
					{
						StartIndex = i;
						break;
					}
				}

				if (StartIndex < AllBytes.Length)
				{
					for (uint i = StartIndex; i + 7 < AllBytes.Length; i++)
					{
						if(AllBytes[i] == '<' && AllBytes[i + 1] == '/' && AllBytes[i + 2] == 'p' && AllBytes[i + 3] == 'l' && AllBytes[i + 4] == 'i' && AllBytes[i + 5] == 's' && AllBytes[i + 6] == 't' && AllBytes[i + 7] == '>')
						{
							EndIndex = i+7;
							break;
						}
					}
				}

				if (StartIndex < AllBytes.Length && EndIndex < AllBytes.Length)
				{
					byte[] TextBytes = new byte[EndIndex - StartIndex];
					Buffer.BlockCopy(AllBytes, (int)StartIndex, TextBytes, 0, (int)(EndIndex - StartIndex));

					string AllText = Encoding.UTF8.GetString(TextBytes);
					int idx = AllText.IndexOf("<key>UUID</key>");
					if (idx > 0)
					{
						idx = AllText.IndexOf("<string>", idx);
						if (idx > 0)
						{
							idx += "<string>".Length;
							MobileProvisionUUID = AllText.Substring(idx, AllText.IndexOf("</string>", idx) - idx);
						}
					}
					idx = AllText.IndexOf("<key>com.apple.developer.team-identifier</key>");
					if (idx > 0)
					{
						idx = AllText.IndexOf("<string>", idx);
						if (idx > 0)
						{
							idx += "<string>".Length;
							TeamUUID = AllText.Substring(idx, AllText.IndexOf("</string>", idx) - idx);
						}
					}
					idx = AllText.IndexOf("<key>application-identifier</key>");
					if (idx > 0)
					{
						idx = AllText.IndexOf("<string>", idx);
						if (idx > 0)
						{
							idx += "<string>".Length;
							String FullID = AllText.Substring(idx, AllText.IndexOf("</string>", idx) - idx);
							BundleIdentifier = FullID.Substring(FullID.IndexOf('.') + 1);
						}
					}
					idx = AllText.IndexOf("<key>Name</key>");
                    if (idx > 0)
                    {
                        idx = AllText.IndexOf("<string>", idx);
                        if (idx > 0)
                        {
                            idx += "<string>".Length;
                            MobileProvisionName = AllText.Substring(idx, AllText.IndexOf("</string>", idx) - idx);
                        }
                    }
				}

				if (string.IsNullOrEmpty(MobileProvisionUUID) || string.IsNullOrEmpty(TeamUUID))
				{
					MobileProvision = null;
					SigningCertificate = null;
					Log.TraceLog("Failed to parse the mobile provisioning profile.");
				}
            }
		}

        void IPPDataReceivedHandler(Object Sender, DataReceivedEventArgs Line)
        {
            if ((Line != null) && (Line.Data != null))
            {
				Log.TraceLog("{0}", Line.Data);
                if (!string.IsNullOrEmpty(SigningCertificate))
                {
                    if (Line.Data.Contains("CERTIFICATE-") && Line.Data.Contains(SigningCertificate))
                    {
                        bHaveCertificate = true;
                    }
                }
                else
                {
                    int cindex = Line.Data.IndexOf("CERTIFICATE-");
                    int pindex = Line.Data.IndexOf("PROVISION-");
                    if (cindex > -1 && pindex > -1)
                    {
                        cindex += "CERTIFICATE-".Length;
                        SigningCertificate = Line.Data.Substring(cindex, pindex - cindex - 1);
                        pindex += "PROVISION-".Length;
						if(pindex < Line.Data.Length)
						{
							MobileProvisionFile = new FileReference(Line.Data.Substring(pindex));
						}
                    }
                }
            }
        }
    }

	class IOSPlatform : UEBuildPlatform
	{
		IOSPlatformSDK SDK;
		List<IOSProjectSettings> CachedProjectSettings = new List<IOSProjectSettings>();
		List<IOSProjectSettings> CachedProjectSettingsByBundle = new List<IOSProjectSettings>();
		Dictionary<string, IOSProvisioningData> ProvisionCache = new Dictionary<string, IOSProvisioningData>();

		// by default, use an empty architecture (which is really just a modifer to the platform for some paths/names)
		public static string IOSArchitecture = "";

		public IOSPlatform(IOSPlatformSDK InSDK)
			: this(InSDK, UnrealTargetPlatform.IOS)
		{
		}

		protected IOSPlatform(IOSPlatformSDK InSDK, UnrealTargetPlatform TargetPlatform)
			: base(TargetPlatform)
		{
			SDK = InSDK;
		}

		// The current architecture - affects everything about how UBT operates on IOS
		public override string GetDefaultArchitecture(FileReference ProjectFile)
		{
			return IOSArchitecture;
		}

		public override string GetFolderNameForArchitecture(string Architecture)
		{
			return IOSArchitecture;
		}

		public override List<FileReference> FinalizeBinaryPaths(FileReference BinaryName, FileReference ProjectFile, ReadOnlyTargetRules Target)
		{
			List<FileReference> BinaryPaths = new List<FileReference>();
			if(Target.bShouldCompileAsDLL)
			{
				BinaryPaths.Add(FileReference.Combine(BinaryName.Directory, Target.Configuration.ToString(), Target.Name + ".framework", Target.Name));
			}
			else
			{
				BinaryPaths.Add(BinaryName);
			}
			return BinaryPaths;
		}

		public override void ResetTarget(TargetRules Target)
		{
			// we currently don't have any simulator libs for PhysX
			if (Target.Architecture == "-simulator")
			{
				Target.bCompilePhysX = false;
			}

			Target.bCompileAPEX = false;
			Target.bCompileNvCloth = false;

			Target.bDeployAfterCompile = true;

			Target.IOSPlatform.ProjectSettings = ((IOSPlatform)GetBuildPlatform(Target.Platform)).ReadProjectSettings(Target.ProjectFile);
			
			// always strip in shipping configuration (commandline could have set it also)
			if (Target.Configuration == UnrealTargetConfiguration.Shipping)
			{
				Target.IOSPlatform.bStripSymbols = true;	
			}
			
			// if we are stripping the executable, or if the project requested it, or if it's a buildmachine, generate the dsym
			if (Target.IOSPlatform.bStripSymbols || Target.IOSPlatform.ProjectSettings.bGeneratedSYMFile || Environment.GetEnvironmentVariable("IsBuildMachine") == "1")
			{
				Target.IOSPlatform.bGeneratedSYM = true;
			}

			// Set bShouldCompileAsDLL when building as a framework
			Target.bShouldCompileAsDLL = Target.IOSPlatform.ProjectSettings.bBuildAsFramework;
		}

		public override void ValidateTarget(TargetRules Target)
		{
			// we assume now we are building with IOS8 or later
			if (Target.bCompileAgainstEngine)
			{
				Target.GlobalDefinitions.Add("HAS_METAL=1");
				Target.ExtraModuleNames.Add("MetalRHI");
			}
			else
			{
				Target.GlobalDefinitions.Add("HAS_METAL=0");
			}

			if (Target.bShouldCompileAsDLL)
			{
				int PreviousDefinition = Target.GlobalDefinitions.FindIndex(s => s.Contains("BUILD_EMBEDDED_APP"));
				if (PreviousDefinition >= 0)
				{
					Target.GlobalDefinitions.RemoveAt(PreviousDefinition);
				}

				Target.GlobalDefinitions.Add("BUILD_EMBEDDED_APP=1");
				
				if (Target.Platform == UnrealTargetPlatform.IOS)
				{
					Target.ExportPublicHeader = "Headers/PreIOSEmbeddedView.h";
				}
			}


			Target.bCheckSystemHeadersForModification = false;
		}

		public override SDKStatus HasRequiredSDKsInstalled()
		{
			return SDK.HasRequiredSDKsInstalled();
		}

		/// <summary>
		/// Determines if the given name is a build product for a target.
		/// </summary>
		/// <param name="FileName">The name to check</param>
		/// <param name="NamePrefixes">Target or application names that may appear at the start of the build product name (eg. "UE4Editor", "ShooterGameEditor")</param>
		/// <param name="NameSuffixes">Suffixes which may appear at the end of the build product name</param>
		/// <returns>True if the string matches the name of a build product, false otherwise</returns>
		public override bool IsBuildProduct(string FileName, string[] NamePrefixes, string[] NameSuffixes)
		{
			return IsBuildProductName(FileName, NamePrefixes, NameSuffixes, "")
				|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".stub")
				|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".dylib")
				|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".dSYM")
				|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".dSYM.zip")
				|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".o");
		}

		/// <summary>
		/// Get the extension to use for the given binary type
		/// </summary>
		/// <param name="InBinaryType"> The binary type being built</param>
		/// <returns>string    The binary extenstion (ie 'exe' or 'dll')</returns>
		public override string GetBinaryExtension(UEBuildBinaryType InBinaryType)
		{
			switch (InBinaryType)
			{
				case UEBuildBinaryType.DynamicLinkLibrary:
					return ".dylib";
				case UEBuildBinaryType.Executable:
					return "";
				case UEBuildBinaryType.StaticLibrary:
					return ".a";
			}
			return base.GetBinaryExtension(InBinaryType);
		}

		/// <summary>
		/// Allows the platform to override whether the architecture name should be appended to the name of binaries.
		/// </summary>
		/// <returns>True if the architecture name should be appended to the binary</returns>
		public override bool RequiresArchitectureSuffix()
		{
			// Any -architecture argument passed to UBT only affects the contents of the binaries, not their naming
			return false;
		}
		
		public IOSProjectSettings ReadProjectSettings(FileReference ProjectFile, string Bundle = "")
		{
			IOSProjectSettings ProjectSettings = null;

			// Use separate lists to prevent an overridden Bundle id polluting the standard project file. 
			bool bCacheByBundle = !string.IsNullOrEmpty(Bundle);
			if (bCacheByBundle)
			{
				ProjectSettings = CachedProjectSettingsByBundle.FirstOrDefault(x => x.ProjectFile == ProjectFile && x.BundleIdentifier == Bundle);
			}
			else
			{
				ProjectSettings = CachedProjectSettings.FirstOrDefault(x => x.ProjectFile == ProjectFile);
			}

			if(ProjectSettings == null)
			{
				ProjectSettings = CreateProjectSettings(ProjectFile, Bundle);
				if (bCacheByBundle)
				{
					CachedProjectSettingsByBundle.Add(ProjectSettings);
				}
				else
				{
					CachedProjectSettings.Add(ProjectSettings);
				}
			}
			return ProjectSettings;
		}

		protected virtual IOSProjectSettings CreateProjectSettings(FileReference ProjectFile, string Bundle)
		{
			return new IOSProjectSettings(ProjectFile, Bundle);
		}

		public IOSProvisioningData ReadProvisioningData(FileReference ProjectFile, bool bForDistribution = false, string Bundle = "")
		{
			IOSProjectSettings ProjectSettings = ReadProjectSettings(ProjectFile, Bundle);
			return ReadProvisioningData(ProjectSettings, bForDistribution);
		}

		public IOSProvisioningData ReadProvisioningData(IOSProjectSettings ProjectSettings, bool bForDistribution = false)
        {
			string ProvisionKey = ProjectSettings.BundleIdentifier + " " + bForDistribution.ToString();

            IOSProvisioningData ProvisioningData;
			if(!ProvisionCache.TryGetValue(ProvisionKey, out ProvisioningData))
            {
				ProvisioningData = CreateProvisioningData(ProjectSettings, bForDistribution);
                ProvisionCache.Add(ProvisionKey, ProvisioningData);
            }
			return ProvisioningData;
        }

		protected virtual IOSProvisioningData CreateProvisioningData(IOSProjectSettings ProjectSettings, bool bForDistribution)
		{
			return new IOSProvisioningData(ProjectSettings, bForDistribution);
		}

		public override string[] GetDebugInfoExtensions(ReadOnlyTargetRules InTarget, UEBuildBinaryType InBinaryType)
		{
			if (InTarget.IOSPlatform.bGeneratedSYM)
			{
				IOSProjectSettings ProjectSettings = ReadProjectSettings(InTarget.ProjectFile);

				// which format?
				if (ProjectSettings.bGeneratedSYMBundle)
				{
					return new string[] { ".dSYM.zip" };
				}
				else
				{
					return new string[] { ".dSYM" };
				}
			}

            return new string [] {};
		}

		public override bool CanUseXGE()
		{
			return false;
		}

		public override bool CanUseDistcc()
		{
			return true;
		}

		public bool HasCustomIcons(DirectoryReference ProjectDirectoryName)
		{
			string IconDir = Path.Combine(ProjectDirectoryName.FullName, "Build", "IOS", "Resources", "Graphics");
			if(Directory.Exists(IconDir))
			{
				foreach (string f in Directory.EnumerateFiles(IconDir))
				{
					if (f.Contains("Icon") && Path.GetExtension(f).Contains(".png"))
					{
						Log.TraceInformation("Requiring custom build because project {0} has custom icons", Path.GetFileName(ProjectDirectoryName.FullName));
						return true;
					}
				}
			}
			return false;
		}

		/// <summary>
		/// Check for the default configuration
		/// return true if the project uses the default build config
		/// </summary>
		public override bool HasDefaultBuildConfig(UnrealTargetPlatform Platform, DirectoryReference ProjectDirectoryName)
		{
			string[] BoolKeys = new string[] {
				"bShipForBitcode", "bGeneratedSYMFile",
				"bGeneratedSYMBundle", "bEnableRemoteNotificationsSupport", "bEnableCloudKitSupport",
                "bGenerateCrashReportSymbols", "bEnableBackgroundFetch"
            };
			string[] StringKeys = new string[] {
				"MinimumiOSVersion", 
				"AdditionalLinkerFlags",
				"AdditionalShippingLinkerFlags"
			};

			// check for custom icons
			if (HasCustomIcons(ProjectDirectoryName))
			{
				return false;
			}

			// look up iOS specific settings
			if (!DoProjectSettingsMatchDefault(Platform, ProjectDirectoryName, "/Script/IOSRuntimeSettings.IOSRuntimeSettings",
					BoolKeys, null, StringKeys))
			{
				return false;
			}

			// check the base settings
			return base.HasDefaultBuildConfig(Platform, ProjectDirectoryName);
		}

		/// <summary>
		/// Check for the build requirement due to platform requirements
		/// return true if the project requires a build
		/// </summary>
		public override bool RequiresBuild(UnrealTargetPlatform Platform, DirectoryReference ProjectDirectoryName)
		{
			// check for custom icons
			return HasCustomIcons(ProjectDirectoryName);
		}

		public override bool ShouldCompileMonolithicBinary(UnrealTargetPlatform InPlatform)
		{
			// This platform currently always compiles monolithic
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
			if ((Target.Platform == UnrealTargetPlatform.Win32) || (Target.Platform == UnrealTargetPlatform.Win64) || (Target.Platform == UnrealTargetPlatform.Mac))
			{
				bool bBuildShaderFormats = Target.bForceBuildShaderFormats;
				if (!Target.bBuildRequiresCookedData)
				{
					if (ModuleName == "Engine")
					{
						if (Target.bBuildDeveloperTools)
						{
							Rules.DynamicallyLoadedModuleNames.Add("IOSTargetPlatform");
							Rules.DynamicallyLoadedModuleNames.Add("TVOSTargetPlatform");
						}
					}
					else if (ModuleName == "TargetPlatform")
					{
						bBuildShaderFormats = true;
						Rules.DynamicallyLoadedModuleNames.Add("TextureFormatPVR");
						Rules.DynamicallyLoadedModuleNames.Add("TextureFormatASTC");
						Rules.DynamicallyLoadedModuleNames.Add("TextureFormatETC2");
						if (Target.bBuildDeveloperTools && Target.bCompileAgainstEngine)
						{
							Rules.DynamicallyLoadedModuleNames.Add("AudioFormatADPCM");
						}
					}
				}

				// allow standalone tools to use targetplatform modules, without needing Engine
				if (ModuleName == "TargetPlatform")
				{
					if (Target.bForceBuildTargetPlatforms)
					{
						Rules.DynamicallyLoadedModuleNames.Add("IOSTargetPlatform");
						Rules.DynamicallyLoadedModuleNames.Add("TVOSTargetPlatform");
					}

					if (bBuildShaderFormats)
					{
						Rules.DynamicallyLoadedModuleNames.Add("MetalShaderFormat");
					}
				}
			}
		}

		/// <summary>
		/// Whether this platform should create debug information or not
		/// </summary>
		/// <param name="Target">The target being built</param>
		/// <returns>bool    true if debug info should be generated, false if not</returns>
		public override bool ShouldCreateDebugInfo(ReadOnlyTargetRules Target)
		{
			return true;
		}

		/// <summary>
		/// Setup the target environment for building
		/// </summary>
		/// <param name="Target">Settings for the target being compiled</param>
		/// <param name="CompileEnvironment">The compile environment for this target</param>
		/// <param name="LinkEnvironment">The link environment for this target</param>
		public override void SetUpEnvironment(ReadOnlyTargetRules Target, CppCompileEnvironment CompileEnvironment, LinkEnvironment LinkEnvironment)
		{
			IOSProjectSettings ProjectSettings = ((IOSPlatform)UEBuildPlatform.GetBuildPlatform(Target.Platform)).ReadProjectSettings(Target.ProjectFile);
			if (!ProjectFileGenerator.bGenerateProjectFiles)
			{
				Log.TraceInformation("Compiling against OS Version {0} [minimum allowed at runtime]", ProjectSettings.RuntimeVersion);
			}

			CompileEnvironment.Definitions.Add("PLATFORM_IOS=1");
			CompileEnvironment.Definitions.Add("PLATFORM_APPLE=1");

			CompileEnvironment.Definitions.Add("WITH_TTS=0");
			CompileEnvironment.Definitions.Add("WITH_SPEECH_RECOGNITION=0");
			CompileEnvironment.Definitions.Add("WITH_EDITOR=0");
			CompileEnvironment.Definitions.Add("USE_NULL_RHI=0");

			if (ProjectSettings.bNotificationsEnabled)
			{
				CompileEnvironment.Definitions.Add("NOTIFICATIONS_ENABLED=1");
			}
			else
			{
				CompileEnvironment.Definitions.Add("NOTIFICATIONS_ENABLED=0");
			}
            if (ProjectSettings.bBackgroundFetchEnabled)
            {
                CompileEnvironment.Definitions.Add("BACKGROUNDFETCH_ENABLED=1");
            }
            else
            {
                CompileEnvironment.Definitions.Add("BACKGROUNDFETCH_ENABLED=0");
            }
			if (ProjectSettings.bFileSharingEnabled)
			{
				CompileEnvironment.Definitions.Add("FILESHARING_ENABLED=1");
			}
			else
			{
				CompileEnvironment.Definitions.Add("FILESHARING_ENABLED=0");
			}

			CompileEnvironment.Definitions.Add("UE_DISABLE_FORCE_INLINE=" + (ProjectSettings.bDisableForceInline ? "1" : "0"));

			if (Target.Architecture == "-simulator")
			{
				CompileEnvironment.Definitions.Add("WITH_SIMULATOR=1");
			}
			else
			{
				CompileEnvironment.Definitions.Add("WITH_SIMULATOR=0");
			}

			if (ProjectSettings.bEnableAdvertisingIdentifier)
			{
				CompileEnvironment.Definitions.Add("ENABLE_ADVERTISING_IDENTIFIER=1");
			}

			// if the project has an Oodle compression Dll, enable the decompressor on IOS
			if (Target.ProjectFile != null)
			{
				DirectoryReference ProjectDir = Target.ProjectFile.Directory;
				string OodleDllPath = DirectoryReference.Combine(ProjectDir, "Binaries/ThirdParty/Oodle/Mac/libUnrealPakPlugin.dylib").FullName;
				if (File.Exists(OodleDllPath))
				{
					Log.TraceVerbose("        Registering custom oodle compressor for {0}", UnrealTargetPlatform.IOS.ToString());
					CompileEnvironment.Definitions.Add("REGISTER_OODLE_CUSTOM_COMPRESSOR=1");
				}
			}

			// convert runtime version into standardized integer
			float TargetFloat = Target.IOSPlatform.RuntimeVersion;
			int IntPart = (int)TargetFloat;
			int FracPart = (int)((TargetFloat - IntPart) * 10);
			int TargetNum = IntPart * 10000 + FracPart * 100;
			CompileEnvironment.Definitions.Add("MINIMUM_UE4_COMPILED_IOS_VERSION=" + TargetNum);

			LinkEnvironment.AdditionalFrameworks.Add(new UEBuildFramework("GameKit"));
			LinkEnvironment.AdditionalFrameworks.Add(new UEBuildFramework("StoreKit"));
			LinkEnvironment.AdditionalFrameworks.Add(new UEBuildFramework("DeviceCheck"));

		}

		/// <summary>
		/// Setup the binaries for this specific platform.
		/// </summary>
		/// <param name="Target">The target being built</param>
		/// <param name="ExtraModuleNames"></param>
		public override void AddExtraModules(ReadOnlyTargetRules Target, List<string> ExtraModuleNames)
		{
			if (Target.Type != TargetType.Program)
			{
				ExtraModuleNames.Add("IOSPlatformFeatures");
			}
		}

		/// <summary>
		/// Creates a toolchain instance for the given platform.
		/// </summary>
		/// <param name="Target">The target being built</param>
		/// <returns>New toolchain instance.</returns>
		public override UEToolChain CreateToolChain(ReadOnlyTargetRules Target)
		{
			IOSProjectSettings ProjectSettings = ReadProjectSettings(Target.ProjectFile);
			return new IOSToolChain(Target, ProjectSettings);
		}

		/// <summary>
		/// Deploys the given target
		/// </summary>
		/// <param name="Receipt">Receipt for the target being deployed</param>
		public override void Deploy(TargetReceipt Receipt)
		{
			if (Receipt.HasValueForAdditionalProperty("CompileAsDll", "true"))
			{
				// IOSToolchain.PostBuildSync handles the copy, nothing else to do here
			}
			else
			{
				new UEDeployIOS().PrepTargetForDeployment(Receipt);
			}
		}
	}

	class IOSPlatformSDK : UEBuildPlatformSDK
	{
		protected override SDKStatus HasRequiredManualSDKInternal()
		{
			if (!Utils.IsRunningOnMono)
			{
				// check to see if iTunes is installed
				string dllPath = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\Apple Inc.\\Apple Mobile Device Support\\Shared", "iTunesMobileDeviceDLL", null) as string;
				if (String.IsNullOrEmpty(dllPath) || !File.Exists(dllPath))
				{
					dllPath = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\Apple Inc.\\Apple Mobile Device Support\\Shared", "MobileDeviceDLL", null) as string;
					if (String.IsNullOrEmpty(dllPath) || !File.Exists(dllPath))
					{
						// iTunes >= 12.7 doesn't have a key specifying the 32-bit DLL but it does have a ASMapiInterfaceDLL key and MobileDevice.dll is in usually in the same directory
						dllPath = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\Apple Inc.\\Apple Mobile Device Support\\Shared", "ASMapiInterfaceDLL", null) as string;
						dllPath = String.IsNullOrEmpty(dllPath) ? null : dllPath.Substring(0, dllPath.LastIndexOf('\\') + 1) + "MobileDevice.dll";

						if (String.IsNullOrEmpty(dllPath) || !File.Exists(dllPath))
						{
							dllPath = FindWindowsStoreITunesDLL();
						}

						if (String.IsNullOrEmpty(dllPath) || !File.Exists(dllPath))
						{
							return SDKStatus.Invalid;
						}
					}
				}
			}
			return SDKStatus.Valid;
		}

		static string FindWindowsStoreITunesDLL()
		{
			string InstallPath = null;

			string PackagesKeyName = "Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\PackageRepository\\Packages";

			RegistryKey PackagesKey = Registry.LocalMachine.OpenSubKey(PackagesKeyName);
			if (PackagesKey != null)
			{
				string[] PackageSubKeyNames = PackagesKey.GetSubKeyNames();

				foreach (string PackageSubKeyName in PackageSubKeyNames)
				{
					if (PackageSubKeyName.Contains("AppleInc.iTunes") && (PackageSubKeyName.Contains("_x64") || PackageSubKeyName.Contains("_x86")))
					{
						string FullPackageSubKeyName = PackagesKeyName + "\\" + PackageSubKeyName;

						RegistryKey iTunesKey = Registry.LocalMachine.OpenSubKey(FullPackageSubKeyName);
						if (iTunesKey != null)
						{
							InstallPath = (string)iTunesKey.GetValue("Path") + "\\AMDS32\\MobileDevice.dll";
							break;
						}
					}
				}
			}

			return InstallPath;
		}
	}

	class IOSPlatformFactory : UEBuildPlatformFactory
	{
		public override UnrealTargetPlatform TargetPlatform
		{
			get { return UnrealTargetPlatform.IOS; }
		}

		/// <summary>
		/// Register the platform with the UEBuildPlatform class
		/// </summary>
		public override void RegisterBuildPlatforms()
		{
			IOSPlatformSDK SDK = new IOSPlatformSDK();
			SDK.ManageAndValidateSDK();

			// Register this build platform for IOS
			UEBuildPlatform.RegisterBuildPlatform(new IOSPlatform(SDK));
			UEBuildPlatform.RegisterPlatformWithGroup(UnrealTargetPlatform.IOS, UnrealPlatformGroup.Apple);
			UEBuildPlatform.RegisterPlatformWithGroup(UnrealTargetPlatform.IOS, UnrealPlatformGroup.IOS);
		}
	}
}
