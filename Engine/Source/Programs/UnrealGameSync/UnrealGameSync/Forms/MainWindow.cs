// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Data;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows.Forms;
using System.Threading;

namespace UnrealGameSync
{
	interface IMainWindowTabPanel : IDisposable
	{
		void Activate();
		void Deactivate();
		void Hide();
		void Show();
		bool IsBusy();
		bool CanClose();
		bool CanSyncNow();
		void SyncLatestChange();
		bool CanLaunchEditor();
		void LaunchEditor();

		Tuple<TaskbarState, float> DesiredTaskbarState
		{
			get;
		}

		UserSelectedProjectSettings SelectedProject
		{
			get;
		}
	}

	partial class MainWindow : Form, IWorkspaceControlOwner
	{
		[Flags]
		enum OpenProjectOptions
		{
			None,
			Quiet,
		}

		[DllImport("uxtheme.dll", CharSet = CharSet.Unicode)]
		static extern int SetWindowTheme(IntPtr hWnd, string pszSubAppName, string pszSubIdList);

		[DllImport("user32.dll")]
		public static extern int SendMessage(IntPtr hWnd, Int32 wMsg, Int32 wParam, Int32 lParam);

		private const int WM_SETREDRAW = 11; 

		SynchronizationContext MainThreadSynchronizationContext;

		string ApiUrl;
		string DataFolder;
		LineBasedTextWriter Log;
		UserSettings Settings;
		int TabMenu_TabIdx = -1;
		int ChangingWorkspacesRefCount;

		bool bAllowClose = false;

		bool bRestoreStateOnLoad;

		System.Threading.Timer ScheduleTimer;
		System.Threading.Timer ScheduleSettledTimer;

		string OriginalExecutableFileName;

		IMainWindowTabPanel CurrentTabPanel;

		public MainWindow(string InApiUrl, string InDataFolder, bool bInRestoreStateOnLoad, string InOriginalExecutableFileName, List<DetectProjectSettingsResult> StartupProjects, LineBasedTextWriter InLog, UserSettings InSettings)
		{
			InitializeComponent();

			MainThreadSynchronizationContext = SynchronizationContext.Current;
			ApiUrl = InApiUrl;
			DataFolder = InDataFolder;
			bRestoreStateOnLoad = bInRestoreStateOnLoad;
			OriginalExecutableFileName = InOriginalExecutableFileName;
			Log = InLog;
			Settings = InSettings;

			TabControl.OnTabChanged += TabControl_OnTabChanged;
			TabControl.OnNewTabClick += TabControl_OnNewTabClick;
			TabControl.OnTabClicked += TabControl_OnTabClicked;
			TabControl.OnTabClosing += TabControl_OnTabClosing;
			TabControl.OnTabClosed += TabControl_OnTabClosed;
			TabControl.OnTabReorder += TabControl_OnTabReorder;
			TabControl.OnButtonClick += TabControl_OnButtonClick;

			SetupDefaultControl();

			int SelectTabIdx = -1;
			foreach(DetectProjectSettingsResult StartupProject in StartupProjects)
			{
				int TabIdx = -1;
				if(StartupProject.bSucceeded)
				{
					TabIdx = TryOpenProject(StartupProject.Task, -1, OpenProjectOptions.Quiet);
				}
				else if(StartupProject.ErrorMessage != null)
				{
					CreateErrorPanel(-1, StartupProject.Task.SelectedProject, StartupProject.ErrorMessage);
				}

				if (TabIdx != -1 && Settings.LastProject != null && StartupProject.Task.SelectedProject.Equals(Settings.LastProject))
				{
					SelectTabIdx = TabIdx;
				}
			}

			if(SelectTabIdx != -1)
			{
				TabControl.SelectTab(SelectTabIdx);
			}
			else if(TabControl.GetTabCount() > 0)
			{
				TabControl.SelectTab(0);
			}

			StartScheduleTimer();
		}

		protected override void OnHandleCreated(EventArgs e)
		{
			base.OnHandleCreated(e);
		}

		void TabControl_OnButtonClick(int ButtonIdx, Point Location, MouseButtons Buttons)
		{
			if(ButtonIdx == 0)
			{
				EditSelectedProject(TabControl.GetSelectedTabIndex());
			}
		}

		void TabControl_OnTabClicked(object TabData, Point Location, MouseButtons Buttons)
		{
			if(Buttons == System.Windows.Forms.MouseButtons.Right)
			{
				Activate();

				int InsertIdx = 0;

				while(TabMenu_RecentProjects.DropDownItems[InsertIdx] != TabMenu_Recent_Separator)
				{
					TabMenu_RecentProjects.DropDownItems.RemoveAt(InsertIdx);
				}

				TabMenu_TabIdx = -1;
				for(int Idx = 0; Idx < TabControl.GetTabCount(); Idx++)
				{
					if(TabControl.GetTabData(Idx) == TabData)
					{
						TabMenu_TabIdx = Idx;
						break;
					}
				}

				foreach(UserSelectedProjectSettings RecentProject in Settings.RecentProjects)
				{
					ToolStripMenuItem Item = new ToolStripMenuItem(RecentProject.ToString(), null, new EventHandler((o, e) => TryOpenProject(RecentProject, TabMenu_TabIdx)));
					TabMenu_RecentProjects.DropDownItems.Insert(InsertIdx, Item);
					InsertIdx++;
				}

				TabMenu_RecentProjects.Visible = (Settings.RecentProjects.Count > 0);

				TabMenu_TabNames_Stream.Checked = Settings.TabLabels == TabLabels.Stream;
				TabMenu_TabNames_WorkspaceName.Checked = Settings.TabLabels == TabLabels.WorkspaceName;
				TabMenu_TabNames_WorkspaceRoot.Checked = Settings.TabLabels == TabLabels.WorkspaceRoot;
				TabMenu_TabNames_ProjectFile.Checked = Settings.TabLabels == TabLabels.ProjectFile;
				TabMenu.Show(TabControl, Location);

				TabControl.LockHover();
			}
		}

		void TabControl_OnTabReorder()
		{
			SaveTabSettings();
		}

		void TabControl_OnTabClosed(object Data)
		{
			IMainWindowTabPanel TabPanel = (IMainWindowTabPanel)Data;
			if(CurrentTabPanel == TabPanel)
			{
				CurrentTabPanel = null;
			}
			TabPanel.Dispose();

			SaveTabSettings();
		}

		bool TabControl_OnTabClosing(object TabData)
		{
			IMainWindowTabPanel TabPanel = (IMainWindowTabPanel)TabData;
			return TabPanel.CanClose();
		}

		/// <summary>
		/// Clean up any resources being used.
		/// </summary>
		/// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
		protected override void Dispose(bool disposing)
		{
			if (disposing && (components != null))
			{
				components.Dispose();
			}

			for(int Idx = 0; Idx < TabControl.GetTabCount(); Idx++)
			{
				((IMainWindowTabPanel)TabControl.GetTabData(Idx)).Dispose();
			}

			StopScheduleTimer();

			base.Dispose(disposing);
		}

		private void MainWindow_FormClosing(object Sender, FormClosingEventArgs EventArgs)
		{
			if(!bAllowClose && Settings.bKeepInTray)
			{
				Hide();
				EventArgs.Cancel = true; 
			}
			else
			{
				for(int Idx = 0; Idx < TabControl.GetTabCount(); Idx++)
				{
					IMainWindowTabPanel TabPanel = (IMainWindowTabPanel)TabControl.GetTabData(Idx);
					if(!TabPanel.CanClose())
					{
						EventArgs.Cancel = true;
						return;
					}
				}

				StopScheduleTimer();

				Settings.bWindowVisible = Visible;
				Settings.Save();
			}
		}

		private void SetupDefaultControl()
		{
			List<StatusLine> Lines = new List<StatusLine>();

			StatusLine SummaryLine = new StatusLine();
			SummaryLine.AddText("To get started, open an existing Unreal project file on your hard drive.");
			Lines.Add(SummaryLine);

			StatusLine OpenLine = new StatusLine();
			OpenLine.AddLink("Open project...", FontStyle.Bold | FontStyle.Underline, () => { OpenNewProject(); });
			Lines.Add(OpenLine);

			DefaultControl.Set(Lines, null, null, null);
		}

		private void CreateErrorPanel(int ReplaceTabIdx, UserSelectedProjectSettings Project, string Message)
		{
			Log.WriteLine(Message);

			ErrorPanel ErrorPanel = new ErrorPanel(Project);
			ErrorPanel.Parent = TabPanel;
			ErrorPanel.BorderStyle = BorderStyle.FixedSingle;
			ErrorPanel.BackColor = System.Drawing.Color.FromArgb(((int)(((byte)(250)))), ((int)(((byte)(250)))), ((int)(((byte)(250)))));
			ErrorPanel.Location = new Point(0, 0);
			ErrorPanel.Size = new Size(TabPanel.Width, TabPanel.Height);
			ErrorPanel.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right | AnchorStyles.Bottom;
			ErrorPanel.Hide();

			string SummaryText = String.Format("Unable to open '{0}'.", Project.ToString());

			int NewContentWidth = Math.Max(Math.Max(TextRenderer.MeasureText(SummaryText, ErrorPanel.Font).Width, TextRenderer.MeasureText(Message, ErrorPanel.Font).Width), 400);
			ErrorPanel.SetContentWidth(NewContentWidth);

			List<StatusLine> Lines = new List<StatusLine>();

			StatusLine SummaryLine = new StatusLine();
			SummaryLine.AddText(SummaryText);
			Lines.Add(SummaryLine);

			Lines.Add(new StatusLine(){ LineHeight = 0.5f });

			StatusLine ErrorLine = new StatusLine();
			ErrorLine.AddText(Message);
			Lines.Add(ErrorLine);

			Lines.Add(new StatusLine(){ LineHeight = 0.5f });

			StatusLine ActionLine = new StatusLine();
			ActionLine.AddLink("Open...", FontStyle.Bold | FontStyle.Underline, () => { BeginInvoke(new MethodInvoker(() => { EditSelectedProject(ErrorPanel); })); });
			ActionLine.AddText(" | ");
			ActionLine.AddLink("Retry", FontStyle.Bold | FontStyle.Underline, () => { BeginInvoke(new MethodInvoker(() => { TryOpenProject(Project, TabControl.FindTabIndex(ErrorPanel)); })); });
			ActionLine.AddText(" | ");
			ActionLine.AddLink("Close", FontStyle.Bold | FontStyle.Underline, () => { BeginInvoke(new MethodInvoker(() => { TabControl.RemoveTab(TabControl.FindTabIndex(ErrorPanel)); })); });
			Lines.Add(ActionLine);

			ErrorPanel.Set(Lines, null, null, null);

			string NewProjectName = "Unknown";
			if(Project.Type == UserSelectedProjectType.Client && Project.ClientPath != null)
			{
				NewProjectName = Project.ClientPath.Substring(Project.ClientPath.LastIndexOf('/') + 1);
			}
			if(Project.Type == UserSelectedProjectType.Local && Project.LocalPath != null)
			{
				NewProjectName = Project.LocalPath.Substring(Project.LocalPath.LastIndexOfAny(new char[]{ '/', '\\' }) + 1);
			}

			string NewTabName = String.Format("Error: {0}", NewProjectName);
			if (ReplaceTabIdx == -1)
			{
				int TabIdx = TabControl.InsertTab(-1, NewTabName, ErrorPanel);
				TabControl.SelectTab(TabIdx);
			}
			else
			{
				TabControl.InsertTab(ReplaceTabIdx + 1, NewTabName, ErrorPanel);
				TabControl.RemoveTab(ReplaceTabIdx);
				TabControl.SelectTab(ReplaceTabIdx);
			}

			UpdateProgress();
		}

		public void ShowAndActivate()
		{
			Show();
			Activate();
		}

		public bool CanPerformUpdate()
		{
			if(ContainsFocus || Form.ActiveForm == this)
			{
				return false;
			}

			for (int TabIdx = 0; TabIdx < TabControl.GetTabCount(); TabIdx++)
			{
				IMainWindowTabPanel TabPanel = (IMainWindowTabPanel)TabControl.GetTabData(TabIdx);
				if(TabPanel.IsBusy())
				{
					return false;
				}
			}

			return true;
		}

		public bool CanSyncNow()
		{
			return CurrentTabPanel != null && CurrentTabPanel.CanSyncNow();
		}

		public bool CanLaunchEditor()
		{
			return CurrentTabPanel != null && CurrentTabPanel.CanLaunchEditor();
		}

		public void SyncLatestChange()
		{
			if(CurrentTabPanel != null)
			{
				CurrentTabPanel.SyncLatestChange();
			}
		}

		public void LaunchEditor()
		{
			if(CurrentTabPanel != null)
			{
				CurrentTabPanel.LaunchEditor();
			}
		}

		public void ForceClose()
		{
			bAllowClose = true;
			Close();
		}

		private void MainWindow_Activated(object sender, EventArgs e)
		{
			if(CurrentTabPanel != null)
			{
				CurrentTabPanel.Activate();
			}
		}

		private void MainWindow_Deactivate(object sender, EventArgs e)
		{
			if(CurrentTabPanel != null)
			{
				CurrentTabPanel.Deactivate();
			}
		}

		public void SetupScheduledSync()
		{
			StopScheduleTimer();

			List<UserSelectedProjectSettings> OpenProjects = new List<UserSelectedProjectSettings>();
			for(int TabIdx = 0; TabIdx < TabControl.GetTabCount(); TabIdx++)
			{
				IMainWindowTabPanel TabPanel = (IMainWindowTabPanel)TabControl.GetTabData(TabIdx);
				OpenProjects.Add(TabPanel.SelectedProject);
			}

			ScheduleWindow Schedule = new ScheduleWindow(Settings.bScheduleEnabled, Settings.ScheduleChange, Settings.ScheduleTime, Settings.ScheduleAnyOpenProject, Settings.ScheduleProjects, OpenProjects);
			if(Schedule.ShowDialog() == System.Windows.Forms.DialogResult.OK)
			{
				Schedule.CopySettings(out Settings.bScheduleEnabled, out Settings.ScheduleChange, out Settings.ScheduleTime, out Settings.ScheduleAnyOpenProject, out Settings.ScheduleProjects);
				Settings.Save();
			}

			StartScheduleTimer();
		}

		private void StartScheduleTimer()
		{
			StopScheduleTimer();

			if(Settings.bScheduleEnabled)
			{
				DateTime CurrentTime = DateTime.Now;
				DateTime NextScheduleTime = new DateTime(CurrentTime.Year, CurrentTime.Month, CurrentTime.Day, Settings.ScheduleTime.Hours, Settings.ScheduleTime.Minutes, Settings.ScheduleTime.Seconds);

				if(NextScheduleTime < CurrentTime)
				{
					NextScheduleTime = NextScheduleTime.AddDays(1.0);
				}

				TimeSpan IntervalToFirstTick = NextScheduleTime - CurrentTime;
				ScheduleTimer = new System.Threading.Timer(x => MainThreadSynchronizationContext.Post((o) => { if(!IsDisposed){ ScheduleTimerElapsed(); } }, null), null, IntervalToFirstTick, TimeSpan.FromDays(1));

				Log.WriteLine("Schedule: Started ScheduleTimer for {0} ({1} remaining)", NextScheduleTime, IntervalToFirstTick);
			}
		}

		private void StopScheduleTimer()
		{
			if(ScheduleTimer != null)
			{
				ScheduleTimer.Dispose();
				ScheduleTimer = null;
				Log.WriteLine("Schedule: Stopped ScheduleTimer");
			}
			StopScheduleSettledTimer();
		}

		private void ScheduleTimerElapsed()
		{
			Log.WriteLine("Schedule: Timer Elapsed");

			// Try to open any missing tabs. 
			int NumInitialTabs = TabControl.GetTabCount();
			foreach (UserSelectedProjectSettings ScheduledProject in Settings.ScheduleProjects)
			{
				Log.WriteLine("Schedule: Attempting to open {0}", ScheduledProject);
				TryOpenProject(ScheduledProject, -1, OpenProjectOptions.Quiet);
			}

			// If we did open something, leave it for a while to populate with data before trying to start the sync.
			if(TabControl.GetTabCount() > NumInitialTabs)
			{
				StartScheduleSettledTimer();
			}
			else
			{
				ScheduleSettledTimerElapsed();
			}
		}

		private void StartScheduleSettledTimer()
		{
			StopScheduleSettledTimer();
			ScheduleSettledTimer = new System.Threading.Timer(x => MainThreadSynchronizationContext.Post((o) => { if(!IsDisposed){ ScheduleSettledTimerElapsed(); } }, null), null, TimeSpan.FromSeconds(20.0), TimeSpan.FromMilliseconds(-1.0));
			Log.WriteLine("Schedule: Started ScheduleSettledTimer");
		}

		private void StopScheduleSettledTimer()
		{
			if(ScheduleSettledTimer != null)
			{
				ScheduleSettledTimer.Dispose();
				ScheduleSettledTimer = null;

				Log.WriteLine("Schedule: Stopped ScheduleSettledTimer");
			}
		}

		private void ScheduleSettledTimerElapsed()
		{
			Log.WriteLine("Schedule: Starting Sync");
			for(int Idx = 0; Idx < TabControl.GetTabCount(); Idx++)
			{
				WorkspaceControl Workspace = TabControl.GetTabData(Idx) as WorkspaceControl;
				if(Workspace != null)
				{
					Log.WriteLine("Schedule: Considering {0}", Workspace.SelectedFileName);
					if(Settings.ScheduleAnyOpenProject || Settings.ScheduleProjects.Contains(Workspace.SelectedProject))
					{
						Log.WriteLine("Schedule: Starting Sync");
						Workspace.ScheduleTimerElapsed();
					}
				}
			}
		}

		void TabControl_OnTabChanged(object NewTabData)
		{
			if(IsHandleCreated)
			{
				SendMessage(Handle, WM_SETREDRAW, 0, 0);
			}

			SuspendLayout();

			if(CurrentTabPanel != null)
			{
				CurrentTabPanel.Deactivate();
				CurrentTabPanel.Hide();
			}

			if(NewTabData == null)
			{
				CurrentTabPanel = null;
				Settings.LastProject = null;
				DefaultControl.Show();
			}
			else
			{
				CurrentTabPanel = (IMainWindowTabPanel)NewTabData;
				Settings.LastProject = CurrentTabPanel.SelectedProject;
				DefaultControl.Hide();
			}

			Settings.Save();

			if(CurrentTabPanel != null)
			{
				CurrentTabPanel.Activate();
				CurrentTabPanel.Show();
			}

			ResumeLayout();

			if(IsHandleCreated)
			{
				SendMessage(Handle, WM_SETREDRAW, 1, 0);
			}

			Refresh();
		}

		public void RequestProjectChange(WorkspaceControl Workspace, UserSelectedProjectSettings Project)
		{
			int TabIdx = TabControl.FindTabIndex(Workspace);
			if(TabIdx != -1)
			{
				TryOpenProject(Project, TabIdx);
			}
		}

		public void OpenNewProject()
		{
			DetectProjectSettingsTask DetectedProjectSettings;
			if(OpenProjectWindow.ShowModal(this, null, out DetectedProjectSettings, Settings, DataFolder, Log))
			{
				int NewTabIdx = TryOpenProject(DetectedProjectSettings, -1, OpenProjectOptions.None);
				if(NewTabIdx != -1)
				{
					TabControl.SelectTab(NewTabIdx);
					SaveTabSettings();

					Settings.RecentProjects.RemoveAll(x => x.LocalPath == DetectedProjectSettings.NewSelectedFileName);
					Settings.RecentProjects.Insert(0, DetectedProjectSettings.SelectedProject);
					Settings.Save();
				}
				DetectedProjectSettings.Dispose();
			}
		}

		public void EditSelectedProject(int TabIdx)
		{
			object TabData = TabControl.GetTabData(TabIdx);
			if(TabData is WorkspaceControl)
			{
				WorkspaceControl Workspace = (WorkspaceControl)TabData;
				EditSelectedProject(TabIdx, Workspace.SelectedProject);
			}
			else if(TabData is ErrorPanel)
			{
				ErrorPanel Error = (ErrorPanel)TabData;
				EditSelectedProject(TabIdx, Error.SelectedProject);
			}
		}

		public void EditSelectedProject(WorkspaceControl Workspace)
		{
			int TabIdx = TabControl.FindTabIndex(Workspace);
			if(TabIdx != -1)
			{
				EditSelectedProject(TabIdx, Workspace.SelectedProject);
			}
		}

		public void EditSelectedProject(ErrorPanel Panel)
		{
			int TabIdx = TabControl.FindTabIndex(Panel);
			if(TabIdx != -1)
			{
				EditSelectedProject(TabIdx, Panel.SelectedProject);
			}
		}

		public void EditSelectedProject(int TabIdx, UserSelectedProjectSettings SelectedProject)
		{
			DetectProjectSettingsTask DetectedProjectSettings;
			if(OpenProjectWindow.ShowModal(this, SelectedProject, out DetectedProjectSettings, Settings, DataFolder, Log))
			{
				int NewTabIdx = TryOpenProject(DetectedProjectSettings, TabIdx, OpenProjectOptions.None);
				if(NewTabIdx != -1)
				{
					TabControl.SelectTab(NewTabIdx);
					SaveTabSettings();

					Settings.RecentProjects.RemoveAll(x => x.LocalPath == DetectedProjectSettings.NewSelectedFileName);
					Settings.RecentProjects.Insert(0, DetectedProjectSettings.SelectedProject);
					Settings.Save();
				}
			}
		}

		int TryOpenProject(UserSelectedProjectSettings Project, int ReplaceTabIdx, OpenProjectOptions Options = OpenProjectOptions.None)
		{
			Log.WriteLine("Detecting settings for {0}", Project);
			using(DetectProjectSettingsTask DetectProjectSettings = new DetectProjectSettingsTask(Project, DataFolder, new PrefixedTextWriter("  ", Log)))
			{
				string ErrorMessage;
				if(ModalTask.Execute(this, DetectProjectSettings, "Opening Project", "Opening project, please wait...", out ErrorMessage) != ModalTaskResult.Succeeded)
				{
					if(!String.IsNullOrEmpty(ErrorMessage) && (Options & OpenProjectOptions.Quiet) == 0)
					{
						CreateErrorPanel(ReplaceTabIdx, Project, ErrorMessage);
					}
					return -1;
				}
				return TryOpenProject(DetectProjectSettings, ReplaceTabIdx, Options);
			}
		}

		int TryOpenProject(DetectProjectSettingsTask ProjectSettings, int ReplaceTabIdx, OpenProjectOptions Options)
		{
			Log.WriteLine("Trying to open project {0}", ProjectSettings.SelectedProject.ToString());

			// Check that none of the other tabs already have it open
			for(int TabIdx = 0; TabIdx < TabControl.GetTabCount(); TabIdx++)
			{
				if(ReplaceTabIdx != TabIdx)
				{
					WorkspaceControl Workspace = TabControl.GetTabData(TabIdx) as WorkspaceControl;
					if(Workspace != null)
					{
						if(Workspace.SelectedFileName.Equals(ProjectSettings.NewSelectedFileName, StringComparison.InvariantCultureIgnoreCase))
						{
							Log.WriteLine("  Already open in tab {0}", TabIdx);
							if((Options & OpenProjectOptions.Quiet) == 0)
							{
								TabControl.SelectTab(TabIdx);
							}
							return TabIdx;
						}
						else if(ProjectSettings.NewSelectedFileName.StartsWith(Workspace.BranchDirectoryName + Path.DirectorySeparatorChar, StringComparison.InvariantCultureIgnoreCase))
						{
							if((Options & OpenProjectOptions.Quiet) == 0 && MessageBox.Show(String.Format("{0} is already open under {1}.\n\nWould you like to close it?", Path.GetFileNameWithoutExtension(Workspace.SelectedFileName), Workspace.BranchDirectoryName, Path.GetFileNameWithoutExtension(ProjectSettings.NewSelectedFileName)), "Branch already open", MessageBoxButtons.YesNo) == System.Windows.Forms.DialogResult.Yes)
							{
								Log.WriteLine("  Another project already open in this workspace, tab {0}. Replacing.", TabIdx);
								TabControl.RemoveTab(TabIdx);
							}
							else
							{
								Log.WriteLine("  Another project already open in this workspace, tab {0}. Aborting.", TabIdx);
								return -1;
							}
						}
					}
				}
			}

			// Hide the default control if it's visible
			DefaultControl.Hide();

			// Remove the current tab. We need to ensure the workspace has been shut down before creating a new one with the same log files, etc...
			if(ReplaceTabIdx != -1)
			{
				WorkspaceControl OldWorkspace = TabControl.GetTabData(ReplaceTabIdx) as WorkspaceControl;
				if(OldWorkspace != null)
				{
					OldWorkspace.Hide();
					TabControl.SetTabData(ReplaceTabIdx, new ErrorPanel(ProjectSettings.SelectedProject));
					OldWorkspace.Dispose();
				}
			}

			// Now that we have the project settings, we can construct the tab
			WorkspaceControl NewWorkspace = new WorkspaceControl(this, ApiUrl, OriginalExecutableFileName, ProjectSettings, Log, Settings);
			NewWorkspace.Parent = TabPanel;
			NewWorkspace.Location = new Point(0, 0);
			NewWorkspace.Size = new Size(TabPanel.Width, TabPanel.Height);
			NewWorkspace.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right | AnchorStyles.Bottom;
			NewWorkspace.Hide();

			// Add the tab
			string NewTabName = GetTabName(NewWorkspace);
			if(ReplaceTabIdx == -1)
			{
				int NewTabIdx = TabControl.InsertTab(-1, NewTabName, NewWorkspace);
				Log.WriteLine("  Inserted tab {0}", NewTabIdx);
				return NewTabIdx;
			}
			else
			{
				Log.WriteLine("  Replacing tab {0}", ReplaceTabIdx);
				TabControl.InsertTab(ReplaceTabIdx + 1, NewTabName, NewWorkspace);
				TabControl.RemoveTab(ReplaceTabIdx);
				return ReplaceTabIdx;
			}
		}

		public void StreamChanged(WorkspaceControl Workspace)
		{
			MainThreadSynchronizationContext.Post((o) => { if(!IsDisposed) { StreamChangedCallback(Workspace); } }, null);
		}

		public void StreamChangedCallback(WorkspaceControl Workspace)
		{
			if(ChangingWorkspacesRefCount == 0)
			{
				ChangingWorkspacesRefCount++;

				for(int Idx = 0; Idx < TabControl.GetTabCount(); Idx++)
				{
					if(TabControl.GetTabData(Idx) == Workspace)
					{
						UserSelectedProjectSettings Project = Workspace.SelectedProject;
						if(TryOpenProject(Project, Idx) == -1)
						{
							TabControl.RemoveTab(Idx);
						}
						break;
					}
				}

				ChangingWorkspacesRefCount--;
			}
		}

		void SaveTabSettings()
		{
			Settings.OpenProjects.Clear();
			for(int TabIdx = 0; TabIdx < TabControl.GetTabCount(); TabIdx++)
			{
				IMainWindowTabPanel TabPanel = (IMainWindowTabPanel)TabControl.GetTabData(TabIdx);
				Settings.OpenProjects.Add(TabPanel.SelectedProject);
			}
			Settings.Save();
		}

		void TabControl_OnNewTabClick(Point Location, MouseButtons Buttons)
		{
			if(Buttons == MouseButtons.Left)
			{
				OpenNewProject();
			}
		}

		string GetTabName(WorkspaceControl Workspace)
		{
			switch(Settings.TabLabels)
			{
				case TabLabels.Stream:
					return Workspace.StreamName;
				case TabLabels.ProjectFile:
					return Workspace.SelectedFileName;
				case TabLabels.WorkspaceName:
					return Workspace.ClientName;
				case TabLabels.WorkspaceRoot:
				default:
					return Workspace.BranchDirectoryName;
			}
		}

		public void SetTabNames(TabLabels NewTabNames)
		{
			if(Settings.TabLabels != NewTabNames)
			{
				Settings.TabLabels = NewTabNames;
				Settings.Save();

				for(int Idx = 0; Idx < TabControl.GetTabCount(); Idx++)
				{
					WorkspaceControl Workspace = TabControl.GetTabData(Idx) as WorkspaceControl;
					if(Workspace != null)
					{
						TabControl.SetTabName(Idx, GetTabName(Workspace));
					}
				}
			}
		}

		private void TabMenu_OpenProject_Click(object sender, EventArgs e)
		{
			EditSelectedProject(TabMenu_TabIdx);
		}

		private void TabMenu_TabNames_Stream_Click(object sender, EventArgs e)
		{
			SetTabNames(TabLabels.Stream);
		}

		private void TabMenu_TabNames_WorkspaceName_Click(object sender, EventArgs e)
		{
			SetTabNames(TabLabels.WorkspaceName);
		}

		private void TabMenu_TabNames_WorkspaceRoot_Click(object sender, EventArgs e)
		{
			SetTabNames(TabLabels.WorkspaceRoot);
		}

		private void TabMenu_TabNames_ProjectFile_Click(object sender, EventArgs e)
		{
			SetTabNames(TabLabels.ProjectFile);
		}

		private void TabMenu_RecentProjects_ClearList_Click(object sender, EventArgs e)
		{
			Settings.RecentProjects.Clear();
			Settings.Save();
		}

		private void TabMenu_Closed(object sender, ToolStripDropDownClosedEventArgs e)
		{
			TabControl.UnlockHover();
		}

		private void RecentMenu_ClearList_Click(object sender, EventArgs e)
		{
			Settings.RecentProjects.Clear();
			Settings.Save();
		}

		public void UpdateProgress()
		{
			TaskbarState State = TaskbarState.NoProgress;
			float Progress = -1.0f;

			for(int Idx = 0; Idx < TabControl.GetTabCount(); Idx++)
			{
				IMainWindowTabPanel TabPanel = (IMainWindowTabPanel)TabControl.GetTabData(Idx);

				Tuple<TaskbarState, float> DesiredTaskbarState = TabPanel.DesiredTaskbarState;
				if(DesiredTaskbarState.Item1 == TaskbarState.Error)
				{
					State = TaskbarState.Error;
					TabControl.SetHighlight(Idx, Tuple.Create(Color.FromArgb(204, 64, 64), 1.0f));
				}
				else if(DesiredTaskbarState.Item1 == TaskbarState.Paused && State != TaskbarState.Error)
				{
					State = TaskbarState.Paused;
					TabControl.SetHighlight(Idx, Tuple.Create(Color.FromArgb(255, 242, 0), 1.0f));
				}
				else if(DesiredTaskbarState.Item1 == TaskbarState.Normal && State != TaskbarState.Error && State != TaskbarState.Paused)
				{
					State = TaskbarState.Normal;
					Progress = Math.Max(Progress, DesiredTaskbarState.Item2);
					TabControl.SetHighlight(Idx, Tuple.Create(Color.FromArgb(28, 180, 64), DesiredTaskbarState.Item2));
				}
				else
				{
					TabControl.SetHighlight(Idx, null);
				}
			}

			if(IsHandleCreated)
			{
				if(State == TaskbarState.Normal)
				{
					Taskbar.SetState(Handle, TaskbarState.Normal);
					Taskbar.SetProgress(Handle, (ulong)(Progress * 1000.0f), 1000);
				}
				else
				{
					Taskbar.SetState(Handle, State);
				}
			}
		}
	}
}
