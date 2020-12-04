// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Windows.Media.Imaging;
using Autodesk.Revit.DB;
using Autodesk.Revit.DB.Events;
using Autodesk.Revit.UI;
using Autodesk.Revit.UI.Events;
using Microsoft.Win32;

namespace DatasmithRevitExporter
{
	// Add-in external application Datasmith Revit Exporter.
	public class DatasmithRevitApplication : IExternalApplication
	{
		private static DatasmithRevitExportMessages ExportMessagesDialog = null;

		private static string ExportMessages;

		private EventHandler<DocumentClosingEventArgs> DocumentClosingHandler;
		private EventHandler<ViewActivatedEventArgs> ViewActivatedHandler;

		// Implement the interface to execute some tasks when Revit starts.
		public Result OnStartup(
			UIControlledApplication InApplication // handle to the application being started
		)
		{
			// Create a custom ribbon tab
			string TabName = "Datasmith";
			InApplication.CreateRibbonTab(TabName);

			// Add a new ribbon panel
			RibbonPanel DirectLinkRibbonPanel = InApplication.CreateRibbonPanel(TabName, "Direct Link");
			RibbonPanel FileExportRibbonPanel = InApplication.CreateRibbonPanel(TabName, "File Export");
			RibbonPanel DatasmithRibbonPanel = InApplication.CreateRibbonPanel(TabName, "Datasmith");

			string AssemblyPath = Assembly.GetExecutingAssembly().Location;
			PushButtonData ExportButtonData = new PushButtonData("Export3DView", "Export 3D View", AssemblyPath, "DatasmithRevitExporter.DatasmithExportRevitCommand");
			PushButtonData SyncButtonData = new PushButtonData("Sync3DView", "Synchronize", AssemblyPath, "DatasmithRevitExporter.DatasmithSyncRevitCommand");
			PushButtonData ManageConnectionsButtonData = new PushButtonData("Connections", "Connections", AssemblyPath, "DatasmithRevitExporter.DatasmithManageConnectionsRevitCommand");
			PushButtonData LogButtonData = new PushButtonData("Messages", "Messages", AssemblyPath, "DatasmithRevitExporter.DatasmithShowMessagesRevitCommand");

			PushButton SyncPushButton = DirectLinkRibbonPanel.AddItem(SyncButtonData) as PushButton;
			PushButton ManageConnectionsButton = DirectLinkRibbonPanel.AddItem(ManageConnectionsButtonData) as PushButton;
			PushButton ExportPushButton = FileExportRibbonPanel.AddItem(ExportButtonData) as PushButton;
			PushButton ShowLogButton = DatasmithRibbonPanel.AddItem(LogButtonData) as PushButton;

			string DatasmithIconBase = Path.Combine(Path.GetDirectoryName(AssemblyPath), "DatasmithIcon");
			ExportPushButton.Image = new BitmapImage(new Uri(DatasmithIconBase + "16.png"));
			ExportPushButton.LargeImage = new BitmapImage(new Uri(DatasmithIconBase + "32.png"));
			ExportPushButton.ToolTip = "Export an active 3D View to Unreal Datasmith";

			DatasmithIconBase = Path.Combine(Path.GetDirectoryName(AssemblyPath), "DatasmithSyncIcon");
			SyncPushButton.Image = new BitmapImage(new Uri(DatasmithIconBase + "16.png"));
			SyncPushButton.LargeImage = new BitmapImage(new Uri(DatasmithIconBase + "32.png"));
			SyncPushButton.ToolTip = "Sync an active 3D View with DirectLink";

			DatasmithIconBase = Path.Combine(Path.GetDirectoryName(AssemblyPath), "DatasmithManageConnectionsIcon");
			ManageConnectionsButton.Image = new BitmapImage(new Uri(DatasmithIconBase + "16.png"));
			ManageConnectionsButton.LargeImage = new BitmapImage(new Uri(DatasmithIconBase + "32.png"));
			ManageConnectionsButton.ToolTip = "Manage connections";

			DatasmithIconBase = Path.Combine(Path.GetDirectoryName(AssemblyPath), "DatasmithLogIcon");
			ShowLogButton.Image = new BitmapImage(new Uri(DatasmithIconBase + "16.png"));
			ShowLogButton.LargeImage = new BitmapImage(new Uri(DatasmithIconBase + "32.png"));
			ShowLogButton.ToolTip = "Show messages";

			DocumentClosingHandler = new EventHandler<DocumentClosingEventArgs>(OnDocumentClosing);
			InApplication.ControlledApplication.DocumentClosing += DocumentClosingHandler;

			ViewActivatedHandler = new EventHandler<ViewActivatedEventArgs>(OnViewActivated);
			InApplication.ViewActivated += ViewActivatedHandler;
			
			// Setup Direct Link

			string RevitEngineDir = null;

			try
			{
				using (RegistryKey Key = Registry.LocalMachine.OpenSubKey("Software\\Wow6432Node\\EpicGames\\Unreal Engine"))
				{
					RevitEngineDir = Key?.GetValue("RevitEngineDir") as string;
				}
			}
			finally
			{
				if (RevitEngineDir == null)
				{
					// If we could not read the registry, fallback to hardcoded engine dir
					RevitEngineDir = "C:\\ProgramData\\Epic\\Exporter\\RevitEngine\\";
				}
			}

			bool bDirectLinkInitOk = FDatasmithFacadeDirectLink.Init(true, RevitEngineDir);

			Debug.Assert(bDirectLinkInitOk);

			return Result.Succeeded;
		}

		static void OnDocumentClosing(object sender, DocumentClosingEventArgs e)
		{
			FDirectLink.DestroyInstance(FDirectLink.FindInstance(e.Document), e.Document.Application);
		}

		static void OnViewActivated(object sender, ViewActivatedEventArgs e)
		{
			View Previous = e.PreviousActiveView;
			View Current = e.CurrentActiveView;

			if (Previous == null || !Previous.Document.Equals(Current.Document))
			{
				FDirectLink.ActivateInstance(Current.Document);
			}
		}

		// Implement the interface to execute some tasks when Revit shuts down.
		public Result OnShutdown(
			UIControlledApplication InApplication // handle to the application being shut down
		)
		{
			InApplication.ControlledApplication.DocumentClosing -= DocumentClosingHandler;
			InApplication.ViewActivated -= ViewActivatedHandler;

			DocumentClosingHandler = null;
			ViewActivatedHandler = null;

			if (ExportMessagesDialog != null && !ExportMessagesDialog.IsDisposed)
			{
				ExportMessagesDialog.Close();
			}
			FDatasmithFacadeDirectLink.Shutdown();
			return Result.Succeeded;
		}

		public static void SetExportMessages(string InMessages)
		{
			ExportMessages = InMessages;

			if (ExportMessagesDialog != null)
			{
				ExportMessagesDialog.Messages = ExportMessages;
			}
		}

		public static void ShowExportMessages()
		{
			if (ExportMessagesDialog == null || ExportMessagesDialog.IsDisposed)
			{
				ExportMessagesDialog = new DatasmithRevitExportMessages(() => ExportMessages = "");
				ExportMessagesDialog.Messages = ExportMessages;
				ExportMessagesDialog.Show();
			}
			else
			{
				ExportMessagesDialog.Focus();
			}
		}
	}
}
