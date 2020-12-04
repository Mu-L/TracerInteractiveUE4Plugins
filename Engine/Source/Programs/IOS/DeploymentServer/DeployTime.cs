/**
 * Copyright Epic Games, Inc. All Rights Reserved.
 */

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using Microsoft.Win32;
using Manzana;
using System.Linq;
using iPhonePackager;
using System.Net.Sockets;

namespace DeploymentServer
{
    class DummyDeployTimeReporter : DeployTimeReportingInterface
    {
        public void Log(string Line)
        {
            Console.WriteLine("[DD] " + Line);
        }

        public void Error(string Line)
        {
            Console.WriteLine("[DD] Error: " + Line);
        }

        public void Warning(string Line)
        {
            Console.WriteLine("[DD] Warning: " + Line);
        }

        public void SetProgressIndex(int Progress)
        {
        }

        public int GetTransferProgressDivider()
        {
            return 25;
        }
    }

	class DeploymentProxy : MarshalByRefObject, DeploymentInterface
	{
		static public DeploymentImplementation Deployer;
		public DeploymentProxy()
		{
		}

		public string DeviceId { get { return Deployer.DeviceId; } set { Deployer.DeviceId = value; } }

		public void SetReportingInterface(DeployTimeReportingInterface InReporter)
		{
			Deployer.SetReportingInterface(InReporter);
		}

		public bool UninstallIPAOnDevice(string ApplicationIdentifier)
		{
			return Deployer.UninstallIPAOnDevice(ApplicationIdentifier);
		}

		public ConnectedDeviceInfo[] EnumerateConnectedDevices()
		{
			return Deployer.EnumerateConnectedDevices();
		}

		public bool InstallIPAOnDevice(string IPAPath)
		{
			return Deployer.InstallIPAOnDevice(IPAPath);
		}

		public bool InstallFilesOnDevice(string BundleIdentifier, string ManifestFile)
		{
			return Deployer.InstallFilesOnDevice(BundleIdentifier, ManifestFile);
		}
        public bool CopyFileToDevice(string BundleIdentifier, string SourceFile, string DestFile)
        {
            return Deployer.CopyFileToDevice(BundleIdentifier, SourceFile, DestFile);
        }
        public bool BackupDocumentsDirectory(string BundleIdentifier, string DestinationDocumentsDirectory)
		{
			return Deployer.BackupDocumentsDirectory(BundleIdentifier, DestinationDocumentsDirectory);
		}

		public bool BackupFiles(string BundleIdentifier, string[] Files)
		{
			return Deployer.BackupFiles(BundleIdentifier, Files);
		}

		public bool ListApplications()
		{
			return Deployer.ListApplications();
		}
	};

    class DeploymentImplementation : DeploymentInterface
    {
        bool bHaveRegisteredHandlers = false;

        /// <summary>
        /// Delay for 1 second after finding the first connected device, to wait for others to enumerate.
        /// </summary>
        const uint StandardEnumerationDelayMS = 1000;

        const uint SleepAfterFailedDeviceCallMS = 500;

        /// <summary>
        /// Mappings from device type string to a more friendly device name
        /// </summary>
        Dictionary<string, string> DeviceTypeMapping = new Dictionary<string, string>();

        /// <summary>
        /// The reporting interface used to talk back to iPhonePackager
        /// </summary>
        static DeployTimeReportingInterface ReportIF = new DummyDeployTimeReporter();

		public string DeviceId { get; set; }

        public DeploymentImplementation()
        {
            // Setup the device type mapping
            DeviceTypeMapping.Add("iPhone1,1", "iPhone 1G");
            DeviceTypeMapping.Add("iPhone1,2", "iPhone 3G");
            DeviceTypeMapping.Add("iPhone2,1", "iPhone 3GS");
            DeviceTypeMapping.Add("iPhone3,1", "iPhone 4");  // AT&T
            DeviceTypeMapping.Add("iPhone3,3", "iPhone 4");  // CDMA/Verizon
            DeviceTypeMapping.Add("iPhone4,1", "iPhone 4S");
            DeviceTypeMapping.Add("iPod1,1", "iPod Touch 1G");
            DeviceTypeMapping.Add("iPod2,1", "iPod Touch 2G");
            DeviceTypeMapping.Add("iPod3,1", "iPod Touch 3G");
            DeviceTypeMapping.Add("iPod4,1", "iPod Touch 4G");
            DeviceTypeMapping.Add("iPad1,1", "iPad");
            DeviceTypeMapping.Add("iPad2,1", "iPad 2 (Wifi)");    // Wifi only
            DeviceTypeMapping.Add("iPad2,2", "iPad 2 (with 3G)"); // AT&T
            DeviceTypeMapping.Add("iPad2,3", "iPad 2 (with 3G)"); // CDMA/Verizon

            Console.WriteLine("[deploy] Created deployment server.");

			// Initialize the mobile device manager
			if (!bHaveRegisteredHandlers)
			{
				Manzana.MobileDeviceInstanceManager.Initialize(MobileDeviceConnected, MobileDeviceDisconnected);
				bHaveRegisteredHandlers = true;
			}
        }

        /// <summary>
        /// Returns a pretty device type name
        /// </summary>
        string GetPrettyDeviceType(string DeviceType)
        {
            string OutName;
            if (!DeviceTypeMapping.TryGetValue(DeviceType, out OutName))
            {
                OutName = DeviceType;
            }
            return OutName;
        }

        void MobileDeviceConnected(object sender, Manzana.ConnectEventArgs args)
        {
            string DeviceName = "(unknown name)";
            MobileDeviceInstance Inst = MobileDeviceInstanceManager.ConnectedDevices[args.Device];
            if (Inst != null)
            {
                DeviceName = Inst.DeviceName;
            }

            ReportIF.Log(String.Format("Mobile Device '{0}' connected", DeviceName));
            Inst.OnGenericProgress = MobileDeviceProgressCallback;

            Inst.TransferProgressDivisor = ReportIF.GetTransferProgressDivider();
        }

        void MobileDeviceDisconnected(object sender, Manzana.ConnectEventArgs args)
        {
            ReportIF.Error("Mobile Device disconnected during run!");
        }

        void MobileDeviceProgressCallback(string Msg, int PercentDone)
        {
            ReportIF.SetProgressIndex(PercentDone);
            ReportIF.Log(String.Format(" ... {0}", Msg));
        }

        /// <summary>
        /// Tries to connect to all devices
        /// </summary>
        /// <param name="DelayAfterFirstDeviceMS"></param>
        void ConnectToDevices(uint DelayAfterFirstDeviceMS)
        {
            {
                ReportIF.Log("Trying to connect to mobile device running iOS ...");

                try
                {
                    // Initialize the mobile device manager
                    if (!bHaveRegisteredHandlers)
                    {
                        Manzana.MobileDeviceInstanceManager.Initialize(MobileDeviceConnected, MobileDeviceDisconnected);
                        bHaveRegisteredHandlers = true;
                    }

                    // Wait for connections to roll in
                    int SleepDurationMS = 100;
                    int TotalDurationMS = 5000;
                    while (!MobileDeviceInstanceManager.AreAnyDevicesConnected() && (TotalDurationMS > 0))
                    {
						System.Threading.Thread.Sleep(SleepDurationMS);
                        TotalDurationMS -= SleepDurationMS;
                    }

                    // Wait one additional tick in case any new devices come online
                    //@TODO: Is there a better way to determine if all devices have been enumerated?
                    System.Threading.Thread.Sleep((int)DelayAfterFirstDeviceMS);

                    if (!MobileDeviceInstanceManager.AreAnyDevicesConnected())
                    {
                        ReportIF.Error("Timed out while trying to connect to a mobile device.  Make sure one is connected.");
                    }
                }
				catch (Exception ex)
                {
                    ReportIF.Error(String.Format("Error encountered ('{0}') while trying to connect to a mobile device.  Please verify that iTunes is installed", ex.Message));
                }
            }
        }

        /// <summary>
        /// A delegate to do work on a device, that knows if it has succeeded or failed
        /// </summary>
        delegate bool PerformDeviceActionDelegate(MobileDeviceInstance Device);

        /// <summary>
        /// Calls a delegate on all connected devices.  This re-evaluates the currently connected devices
        /// after each go, so if some devices take a while to enumerate, they will probably still be caught
        /// after the first device work finishes (since the work is typically long).
        /// </summary>
        /// <param name="DelayEnumerationPeriodMS">The initial number of ms to wait after the first enumerated device is found</param>
        /// <param name="PerDeviceWork">The delegate to perform on each connected device</param>
        /// <returns>True if all devices had the work successfully performed, and false if any failed or none were found</returns>
        bool PerformActionOnAllDevices(uint DelayEnumerationPeriodMS, PerformDeviceActionDelegate PerDeviceWork)
        {
            // Start enumerating the devices
            ConnectToDevices(DelayEnumerationPeriodMS);

            // Keep looking at the device list and executing on new ones as long as we keep finding them
			Dictionary<string, bool> Runs = new Dictionary<string, bool>();
            bool bFoundNewDevices = true;
            while (bFoundNewDevices)
            {
                IEnumerable<MobileDeviceInstance> Devices = MobileDeviceInstanceManager.GetSnapshotInstanceList();

                bFoundNewDevices = false;
                foreach (MobileDeviceInstance PotentialDevice in Devices)
                {
					PotentialDevice.Reconnect();
					string DeviceId = PotentialDevice.DeviceId;
					if (DeviceId == String.Empty)
					{
						System.Threading.Thread.Sleep((int)SleepAfterFailedDeviceCallMS);
						DeviceId = PotentialDevice.DeviceId;
					}

					if (!Runs.ContainsKey(DeviceId) && PotentialDevice.IsConnected)
                    {
                        // New device, do the work on it
						Runs.Add(DeviceId, PerDeviceWork(PotentialDevice));
                        bFoundNewDevices = true;
                    }
                }
            }

            // Determine if all succeeded
            bool bAllSucceeded = true;
            bool bAnySucceeded = Runs.Count > 0;
            foreach (var KVP in Runs)
            {
                bAllSucceeded = bAllSucceeded && KVP.Value;
            }

            return bAllSucceeded && bAnySucceeded;
        }


        /// <summary>
        /// Uninstalls all application bundles with the specified application bundle ID on all connected devices
        /// </summary>
        public bool UninstallIPAOnDevice(string ApplicationIdentifier)
        {
            ReportIF.Log("Uninstalling IPA on device ... ");

            // Connect to each device and issue the uninstall
            return PerformActionOnAllDevices(StandardEnumerationDelayMS, delegate(MobileDeviceInstance Device)
            {
                ReportIF.Log(String.Format(" ... Uninstalling application with bundle identifier '{0}' on device '{1}'", ApplicationIdentifier, Device.DeviceName));
                return Device.TryUninstall(ApplicationIdentifier);
            });
        }

        /// <summary>
        /// Makes a dictionary of UDID->DeviceName for all connected devices
        /// </summary>
        public ConnectedDeviceInfo [] EnumerateConnectedDevices()
        {
            List<ConnectedDeviceInfo> Results = new List<ConnectedDeviceInfo>();

            PerformActionOnAllDevices(2 * StandardEnumerationDelayMS, delegate(MobileDeviceInstance Device)
            {
                string DeviceName = Device.DeviceName;
                string UDID = Device.DeviceId;
                string DeviceType = Device.ProductType;
                if (UDID != "")
                {
                    Results.Add(new ConnectedDeviceInfo(DeviceName, UDID, GetPrettyDeviceType(DeviceType)));
                    ReportIF.Log(String.Format("  Connected device '{0}' has UDID '{1}' and type '{2}'... ", DeviceName, UDID, DeviceType));
                }
                else
                {
                    ReportIF.Warning(String.Format("  Failed to query device for it's name or UDID"));
                }
                return true;
            });

            return Results.ToArray();
        }

        /// <summary>
        /// return the list of devices to stdout
        /// </summary>
        public void ListDevices()
        {
            PerformActionOnAllDevices(2 * StandardEnumerationDelayMS, delegate(MobileDeviceInstance Device)
            {
				string ProductType = Device.ProductType;
				string DeviceName = Device.DeviceName;
                string UDID = Device.DeviceId;
				ReportIF.Log(String.Format("FOUND: TYPE: {0} ID: {1} NAME: {2}", ProductType, UDID, DeviceName));

                return true;
            });
        }

		public bool ListenToDevice(string inDeviceID, TextWriter Writer)
		{
			MobileDeviceInstance	targetDevice = null;
            PerformActionOnAllDevices(2 * StandardEnumerationDelayMS, delegate(MobileDeviceInstance Device)
            {
				if(inDeviceID == Device.DeviceId)
				{
					targetDevice = Device;
				}
				return true;
            });

			if(targetDevice != null)
			{
				targetDevice.StartSyslogService();
				
				// This never returns, the process must be killed to stop logging
				while(targetDevice.IsConnected)
				{
					string	curLog = targetDevice.GetSyslogData();
					if(curLog.Trim().Length > 0)
					{
						Writer.Write(curLog);
					}
					else
					{
						System.Threading.Thread.Sleep(50);
					}
				}

				Writer.WriteLine("Device " + inDeviceID + " is no longer connected");
				return true;
			}
			else
			{
				Writer.WriteLine("Could not find device " + inDeviceID);
				return false;
			}
		}

		public void TunnelToDevice(string inDeviceID, String Command)
		{
			MobileDeviceInstance TargetDevice = null;
			
			PerformActionOnAllDevices(2 * StandardEnumerationDelayMS, delegate (MobileDeviceInstance Device)
			{
				if (inDeviceID == Device.DeviceId)
				{
					TargetDevice = Device;
				}
				return true;
			});
			IntPtr TCPService = new IntPtr();
			if (TargetDevice != null)
			{
				TargetDevice.StartTCPRelayService(ref TCPService);

				int SentDat = TargetDevice.TunnelData(Command, TCPService);

				TargetDevice.StopSyslogService();
			}
			else
			{
				ReportIF.Error("Could not find device " + inDeviceID);
			}
		}

		public MobileDeviceInstance StartTCPTunnel(string inDeviceID, ref IntPtr TCPService, short Port = 8888)
		{
			MobileDeviceInstance targetDevice = null;

			PerformActionOnAllDevices(2 * StandardEnumerationDelayMS, delegate (MobileDeviceInstance Device)
			{
				if (inDeviceID == Device.DeviceId)
				{
					targetDevice = Device;
				}
				return true;
			});
			if (targetDevice != null)
			{
				if (targetDevice.StartTCPRelayService(ref TCPService))
				{
					return targetDevice;
				}
				ReportIF.Error("Could not start TCP relay to " + inDeviceID);
			}
			else
			{
				ReportIF.Error("Could not find device " + inDeviceID);
			}
			return null;
		}

		/// <summary>
		/// Installs an IPA to all connected devices
		/// </summary>
		public bool InstallIPAOnDevice(string IPAPath)
        {
            if ((IPAPath == null) || (IPAPath.Length == 0))
            {
                return false;
            }

			// Transfer to all connected devices
			return PerformActionOnAllDevices(StandardEnumerationDelayMS, delegate(MobileDeviceInstance Device)
            {
                // Transfer the file to the device
                string DeviceName = Device.DeviceName;
                string UDID = Device.DeviceId;
                string DeviceType = Device.ProductType;

                if (!string.IsNullOrEmpty(DeviceId))
                {
                    Console.WriteLine("Checking '{0}' with id {1} of type {2} against -device '{3}'.", DeviceName, UDID, DeviceType, DeviceId);
                }

                bool IsIOS = DeviceType.Contains("iPhone") || DeviceType.Contains("iPad");
                bool IsTV = DeviceType.Contains("TV");

                // Match anything if no device was specifie
                bool IsMatch = string.IsNullOrEmpty(DeviceId)
                            || (DeviceId.IndexOf("All_tvOS", StringComparison.CurrentCultureIgnoreCase) >= 0 && IsTV)
                            || (DeviceId.IndexOf("All_iOS", StringComparison.CurrentCultureIgnoreCase) >= 0 && IsIOS)
                            || Device.DeviceId.IndexOf(DeviceId, StringComparison.CurrentCultureIgnoreCase) >= 0
                            || Device.DeviceName.IndexOf(DeviceId, StringComparison.CurrentCultureIgnoreCase) >= 0
                            ;

                if (IsMatch)
				{
					ReportIF.Log(String.Format("Transferring IPA to device '{0}' ... ", DeviceName));
					Device.CopyFileToPublicStaging(IPAPath);

					// Request that the device install it
					ReportIF.Log(String.Format("Installing IPA on device '{0}' ... ", DeviceName));

					// Upgrade will function as install if the app isn't already installed, and has the added benefit of killing a running
					// app rather than failing if the user is still running the app to upgrade
					//return ConnectedDevice.TryInstall(IPAPath);
					bool bResult = Device.TryUpgrade(IPAPath);

					ReportIF.Log("");

					return bResult;
				}
                else
				{
                    Console.WriteLine(String.Format("Ignoring device '{0}' ... ", DeviceName));
                }

				return true;
            });
        }

		/// <summary>
		/// Installs an IPA to all connected devices
		/// </summary>
		public bool InstallFilesOnDevice(string BundleIdentifier, string Manifest)
		{
			// Transfer to all connected devices
			return PerformActionOnAllDevices(StandardEnumerationDelayMS, delegate(MobileDeviceInstance Device)
			{
				bool bResult = true;
                string DeviceType = Device.ProductType;
                if (String.IsNullOrEmpty(DeviceId) || Device.DeviceId == DeviceId ||
                    (DeviceId.Contains("All_tvOS_On") && DeviceType.Contains("TV")) ||
                    (DeviceId.Contains("All_iOS_On") && !DeviceType.Contains("TV")))
                {
                    bResult = Device.TryCopy(BundleIdentifier, Manifest);
					ReportIF.Log("");
				}
				return bResult;
			});
        }

		/// <summary>
		/// Copies a single file to a specified location on the device for the given bundle
		/// </summary>
        public bool CopyFileToDevice(string BundleIdentifier, string SourceFile, string DestFile)
        {
            return PerformActionOnAllDevices(StandardEnumerationDelayMS, delegate (MobileDeviceInstance Device)
            {
                bool bResult = true;
                string DeviceType = Device.ProductType;
                if (String.IsNullOrEmpty(DeviceId) || Device.DeviceId == DeviceId ||
                    (DeviceId.Contains("All_tvOS_On") && DeviceType.Contains("TV")) ||
                    (DeviceId.Contains("All_iOS_On") && !DeviceType.Contains("TV")))
                {
                    bResult = Device.TryCopyFile(BundleIdentifier, SourceFile, DestFile);
                    ReportIF.Log("");
                }
                return bResult;
            });
        }

		/// <summary>
		/// Copies a single file from a specified location on the device for the given bundle
		/// </summary>
		public bool CopyFileFromDevice(string BundleIdentifier, string SourceFile, string DestFile)
		{
			return PerformActionOnAllDevices(StandardEnumerationDelayMS, delegate (MobileDeviceInstance Device)
			{
				bool bResult = true;
				string DeviceType = Device.ProductType;
				if (String.IsNullOrEmpty(DeviceId) || Device.DeviceId == DeviceId ||
					(DeviceId.Contains("All_tvOS_On") && DeviceType.Contains("TV")) ||
					(DeviceId.Contains("All_iOS_On") && !DeviceType.Contains("TV")))
				{
					bResult = Device.TryCopyFileOut(BundleIdentifier, SourceFile, DestFile);
					ReportIF.Log("");
				}
				return bResult;
			});
		}

		public bool BackupDocumentsDirectory(string BundleIdentifier, string DestinationDocumentsDirectory)
        {
            return PerformActionOnAllDevices(StandardEnumerationDelayMS, delegate(MobileDeviceInstance Device)
            {
				bool bResult = true;
                string DeviceType = Device.ProductType;
                if (String.IsNullOrEmpty(DeviceId) || Device.DeviceId == DeviceId ||
                    (DeviceId.Contains("All_tvOS_On") && DeviceType.Contains("TV")) ||
                    (DeviceId.Contains("All_iOS_On") && !DeviceType.Contains("TV")))
                {
                    string SafeDeviceName = MobileDeviceInstance.SanitizePathNoFilename(Device.DeviceName);

					// Destination folder
					string TargetFolder = Path.Combine(DestinationDocumentsDirectory, SafeDeviceName);

					// Source folder
					string SourceFolder = "/Documents/";

                    bResult = Device.TryBackup(BundleIdentifier, SourceFolder, TargetFolder + SourceFolder);
                    SourceFolder = "/Library/Caches/";
                    bResult = Device.TryBackup(BundleIdentifier, SourceFolder, TargetFolder + SourceFolder);

                    ReportIF.Log("");
				}
                return bResult;
            });
        }

		public bool BackupFiles(string BundleIdentifier, string[] DestionationFiles)
		{
			return PerformActionOnAllDevices(StandardEnumerationDelayMS, delegate(MobileDeviceInstance Device)
			{
				bool bResult = true;
                string DeviceType = Device.ProductType;
                if (String.IsNullOrEmpty(DeviceId) || Device.DeviceId == DeviceId ||
                    (DeviceId.Contains("All_tvOS_On") && DeviceType.Contains("TV")) ||
                    (DeviceId.Contains("All_iOS_On") && !DeviceType.Contains("TV")))
                {
                    bResult = Device.TryBackup(BundleIdentifier, DestionationFiles);
					ReportIF.Log("");
				}

				return bResult;
			});
		}

		public bool ListApplications()
		{
			return PerformActionOnAllDevices(StandardEnumerationDelayMS, delegate(MobileDeviceInstance Device)
			{
					Console.WriteLine("Device '{0}' has the following applications:", Device.DeviceName);
				Device.DumpInstalledApplications();

				ReportIF.Log("");

				return true;
			});
		}

        public void SetReportingInterface(DeployTimeReportingInterface InReporter)
        {
//            System.Threading.Thread.Sleep(10000);

            ReportIF = InReporter;
        }
    }
}
