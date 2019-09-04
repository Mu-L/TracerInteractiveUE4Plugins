// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace UnrealGameSync
{
	partial class OpenProjectWindow : Form
	{
		string ServerAndPortOverride;
		string UserNameOverride;
		DetectProjectSettingsTask DetectedProjectSettings;
		string DataFolder;
		string CacheFolder;
		PerforceConnection DefaultConnection;
		TextWriter Log;
		UserSettings Settings;

		private OpenProjectWindow(UserSelectedProjectSettings Project, UserSettings Settings, string DataFolder, string CacheFolder, PerforceConnection DefaultConnection, TextWriter Log)
		{
			InitializeComponent();

			this.Settings = Settings;
			this.DetectedProjectSettings = null;
			this.DataFolder = DataFolder;
			this.CacheFolder = CacheFolder;
			this.DefaultConnection = DefaultConnection;
			this.Log = Log;

			if(Project == null)
			{
				LocalFileRadioBtn.Checked = true;
			}
			else
			{
				if(!String.IsNullOrWhiteSpace(Project.ServerAndPort))
				{
					ServerAndPortOverride = Project.ServerAndPort;
				}
				if(!String.IsNullOrWhiteSpace(Project.UserName))
				{
					UserNameOverride = Project.UserName;
				}

				if(Project.ClientPath != null && Project.ClientPath.StartsWith("//"))
				{
					int SlashIdx = Project.ClientPath.IndexOf('/', 2);
					if(SlashIdx != -1)
					{
						WorkspaceNameTextBox.Text = Project.ClientPath.Substring(2, SlashIdx - 2);
						WorkspacePathTextBox.Text = Project.ClientPath.Substring(SlashIdx);
					}
				}

				if(Project.LocalPath != null)
				{
					LocalFileTextBox.Text = Project.LocalPath;
				}

				if(Project.Type == UserSelectedProjectType.Client)
				{
					WorkspaceRadioBtn.Checked = true;
				}
				else
				{
					LocalFileRadioBtn.Checked = true;
				}
			}

			UpdateEnabledControls();
			UpdateServerLabel();
			UpdateWorkspacePathBrowseButton();
			UpdateOkButton();
		}

		private PerforceConnection Perforce
		{
			get { return Utility.OverridePerforceSettings(DefaultConnection, ServerAndPortOverride, UserNameOverride); }
		}

		public static bool ShowModal(IWin32Window Owner, UserSelectedProjectSettings Project, out DetectProjectSettingsTask NewDetectedProjectSettings, UserSettings Settings, string DataFolder, string CacheFolder, PerforceConnection DefaultConnection, TextWriter Log)
		{
			OpenProjectWindow Window = new OpenProjectWindow(Project, Settings, DataFolder, CacheFolder, DefaultConnection, Log);
			if(Window.ShowDialog(Owner) == DialogResult.OK)
			{
				NewDetectedProjectSettings = Window.DetectedProjectSettings;
				return true;
			}
			else
			{
				NewDetectedProjectSettings = null;
				return false;
			}
		}

		private void UpdateEnabledControls()
		{
			Color WorkspaceTextColor = WorkspaceRadioBtn.Checked? SystemColors.ControlText : SystemColors.GrayText;
			WorkspaceNameLabel.ForeColor = WorkspaceTextColor;
			WorkspaceNameTextBox.ForeColor = WorkspaceTextColor;
			WorkspaceNameNewBtn.ForeColor = WorkspaceTextColor;
			WorkspaceNameBrowseBtn.ForeColor = WorkspaceTextColor;
			WorkspacePathLabel.ForeColor = WorkspaceTextColor;
			WorkspacePathTextBox.ForeColor = WorkspaceTextColor;
			WorkspacePathBrowseBtn.ForeColor = WorkspaceTextColor;

			Color LocalFileTextColor = LocalFileRadioBtn.Checked? SystemColors.ControlText : SystemColors.GrayText;
			LocalFileLabel.ForeColor = LocalFileTextColor;
			LocalFileTextBox.ForeColor = LocalFileTextColor;
			LocalFileBrowseBtn.ForeColor = LocalFileTextColor;

			UpdateWorkspacePathBrowseButton();
		}

		public static string GetServerLabelText(PerforceConnection DefaultConnection, string ServerAndPort, string UserName)
		{
			if(ServerAndPort == null && UserName == null)
			{
				return String.Format("Using default connection settings (user '{0}' on server '{1}').", DefaultConnection.UserName, DefaultConnection.ServerAndPort);
			}
			else
			{
				StringBuilder Text = new StringBuilder("Connecting as ");
				if(UserName == null)
				{
					Text.Append("default user");
				}
				else
				{
					Text.AppendFormat("user '{0}'", UserName);
				}
				Text.Append(" on ");
				if(ServerAndPort == null)
				{
					Text.Append("default server.");
				}
				else
				{
					Text.AppendFormat("server '{0}'.", ServerAndPort);
				}
				return Text.ToString();
			}
		}

		private void UpdateServerLabel()
		{
			ServerLabel.Text = GetServerLabelText(DefaultConnection, ServerAndPortOverride, UserNameOverride);
		}

		private void UpdateWorkspacePathBrowseButton()
		{
			string WorkspaceName;
			WorkspacePathBrowseBtn.Enabled = TryGetWorkspaceName(out WorkspaceName);
		}

		private void UpdateOkButton()
		{
			string ProjectPath;
			OkBtn.Enabled = WorkspaceRadioBtn.Checked? TryGetClientPath(out ProjectPath) : TryGetLocalPath(out ProjectPath);
		}

		private void WorkspaceNewBtn_Click(object sender, EventArgs e)
		{
			WorkspaceRadioBtn.Checked = true;
			
			string WorkspaceName;
			if(NewWorkspaceWindow.ShowModal(this, Perforce, null, WorkspaceNameTextBox.Text, Log, out WorkspaceName))
			{
				WorkspaceNameTextBox.Text = WorkspaceName;
				UpdateOkButton();
			}
		}

		private void WorkspaceBrowseBtn_Click(object sender, EventArgs e)
		{
			WorkspaceRadioBtn.Checked = true;

			string WorkspaceName = WorkspaceNameTextBox.Text;
			if(SelectWorkspaceWindow.ShowModal(this, Perforce, WorkspaceName, Log, out WorkspaceName))
			{
				WorkspaceNameTextBox.Text = WorkspaceName;
			}
		}

		private void WorkspacePathBrowseBtn_Click(object sender, EventArgs e)
		{
			WorkspaceRadioBtn.Checked = true;

			string WorkspaceName;
			if(TryGetWorkspaceName(out WorkspaceName))
			{
				string WorkspacePath = WorkspacePathTextBox.Text.Trim();
				if(SelectProjectFromWorkspaceWindow.ShowModal(this, Perforce, WorkspaceName, WorkspacePath, Log, out WorkspacePath))
				{
					WorkspacePathTextBox.Text = WorkspacePath;
					UpdateOkButton();
				}
			}
		}

		private bool TryGetWorkspaceName(out string WorkspaceName)
		{
			string Text = WorkspaceNameTextBox.Text.Trim();
			if(Text.Length == 0)
			{
				WorkspaceName = null;
				return false;
			}

			WorkspaceName = Text;
			return true;
		}

		private bool TryGetClientPath(out string ClientPath)
		{
			string WorkspaceName;
			if(!TryGetWorkspaceName(out WorkspaceName))
			{
				ClientPath = null;
				return false;
			}

			string WorkspacePath = WorkspacePathTextBox.Text.Trim();
			if(WorkspacePath.Length == 0 || WorkspacePath[0] != '/')
			{
				ClientPath = null;
				return false;
			}

			ClientPath = String.Format("//{0}{1}", WorkspaceName, WorkspacePath);
			return true;
		}

		private bool TryGetLocalPath(out string LocalPath)
		{
			string LocalFile = LocalFileTextBox.Text.Trim();
			if(LocalFile.Length == 0)
			{
				LocalPath = null;
				return false;
			}

			LocalPath = Path.GetFullPath(LocalFile);
			return true;
		}

		private bool TryGetSelectedProject(out UserSelectedProjectSettings Project)
		{
			if(WorkspaceRadioBtn.Checked)
			{
				string ClientPath;
				if(TryGetClientPath(out ClientPath))
				{
					Project = new UserSelectedProjectSettings(ServerAndPortOverride, UserNameOverride, UserSelectedProjectType.Client, ClientPath, null);
					return true;
				}
			}
			else
			{
				string LocalPath;
				if(TryGetLocalPath(out LocalPath))
				{
					Project = new UserSelectedProjectSettings(ServerAndPortOverride, UserNameOverride, UserSelectedProjectType.Local, null, LocalPath);
					return true;
				}
			}

			Project = null;
			return false;
		}

		private void OkBtn_Click(object sender, EventArgs e)
		{
			UserSelectedProjectSettings SelectedProject;
			if(TryGetSelectedProject(out SelectedProject))
			{
				DetectProjectSettingsTask NewDetectedProjectSettings = new DetectProjectSettingsTask(SelectedProject, DataFolder, CacheFolder, Log);
				try
				{
					string ErrorMessage;
					if(PerforceModalTask.Execute(this, Perforce, NewDetectedProjectSettings, "Opening Project", "Opening project, please wait...", Log, out ErrorMessage) != ModalTaskResult.Succeeded)
					{
						if(!String.IsNullOrEmpty(ErrorMessage))
						{
							MessageBox.Show(ErrorMessage);
						}
						return;
					}

					DetectedProjectSettings = NewDetectedProjectSettings;
					NewDetectedProjectSettings = null;
					DialogResult = DialogResult.OK;
					Close();
				}
				finally
				{
					if(NewDetectedProjectSettings != null)
					{
						NewDetectedProjectSettings.Dispose();
						NewDetectedProjectSettings = null;
					}
				}
			}
		}

		private void ChangeLink_LinkClicked(object sender, LinkLabelLinkClickedEventArgs e)
		{
			if(ConnectWindow.ShowModal(this, DefaultConnection, ref ServerAndPortOverride, ref UserNameOverride, Log))
			{
				UpdateServerLabel();
			}
		}

		private void LocalFileBrowseBtn_Click(object sender, EventArgs e)
		{
			LocalFileRadioBtn.Checked = true;

			OpenFileDialog Dialog = new OpenFileDialog();
			Dialog.Filter = "Project files (*.uproject)|*.uproject|Project directory lists (*.uprojectdirs)|*.uprojectdirs|All supported files (*.uproject;*.uprojectdirs)|*.uproject;*.uprojectdirs|All files (*.*)|*.*" ;
			Dialog.FilterIndex = Settings.FilterIndex;
			
			if(!String.IsNullOrEmpty(LocalFileTextBox.Text))
			{
				try
				{
					Dialog.InitialDirectory = Path.GetDirectoryName(LocalFileTextBox.Text);
				}
				catch
				{
				}
			}

			if(Dialog.ShowDialog(this) == System.Windows.Forms.DialogResult.OK)
			{
				string FullName = Path.GetFullPath(Dialog.FileName);

				Settings.FilterIndex = Dialog.FilterIndex;
				Settings.Save();

				LocalFileTextBox.Text = FullName;
				UpdateOkButton();
			}
		}

		private void WorkspaceNameTextBox_TextChanged(object sender, EventArgs e)
		{
			UpdateOkButton();
			UpdateWorkspacePathBrowseButton();
		}

		private void WorkspacePathTextBox_TextChanged(object sender, EventArgs e)
		{
			UpdateOkButton();
		}

		private void LocalFileTextBox_TextChanged(object sender, EventArgs e)
		{
			UpdateOkButton();
		}

		private void WorkspaceRadioBtn_CheckedChanged(object sender, EventArgs e)
		{
			UpdateEnabledControls();
		}

		private void LocalFileRadioBtn_CheckedChanged(object sender, EventArgs e)
		{
			UpdateEnabledControls();
		}

		private void LocalFileTextBox_Enter(object sender, EventArgs e)
		{
			LocalFileRadioBtn.Checked = true;
		}

		private void WorkspaceNameTextBox_Enter(object sender, EventArgs e)
		{
			WorkspaceRadioBtn.Checked = true;
		}

		private void WorkspacePathTextBox_Enter(object sender, EventArgs e)
		{
			WorkspaceRadioBtn.Checked = true;
		}
	}
}
