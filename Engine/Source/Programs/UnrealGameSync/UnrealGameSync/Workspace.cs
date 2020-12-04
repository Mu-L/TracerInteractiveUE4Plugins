// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace UnrealGameSync
{
	[Flags]
	enum WorkspaceUpdateOptions
	{
		Sync = 0x01,
		SyncSingleChange = 0x02,
		AutoResolveChanges = 0x04,
		GenerateProjectFiles = 0x08,
		SyncArchives = 0x10,
		Build = 0x20,
		Clean = 0x40,
		ScheduledBuild = 0x80,
		RunAfterSync = 0x100,
		OpenSolutionAfterSync = 0x200,
		ContentOnly = 0x400,
		UpdateFilter = 0x800,
		SyncAllProjects = 0x1000,
		IncludeAllProjectsInSolution = 0x2000,
	}

	enum WorkspaceUpdateResult
	{
		Canceled,
		FailedToSync,
		FailedToSyncLoginExpired,
		FilesToDelete,
		FilesToResolve,
		FilesToClobber,
		FailedToCompile,
		FailedToCompileWithCleanWorkspace,
		Success,
	}

	class WorkspaceUpdateContext
	{
		public DateTime StartTime = DateTime.UtcNow;
		public int ChangeNumber;
		public WorkspaceUpdateOptions Options;
		public string[] SyncFilter;
		public Dictionary<string, string> ArchiveTypeToDepotPath = new Dictionary<string,string>();
		public Dictionary<string, bool> DeleteFiles = new Dictionary<string,bool>();
		public Dictionary<string, bool> ClobberFiles = new Dictionary<string,bool>();
		public Dictionary<Guid,ConfigObject> DefaultBuildSteps;
		public List<ConfigObject> UserBuildStepObjects;
		public HashSet<Guid> CustomBuildSteps;
		public Dictionary<string, string> Variables;
		public PerforceSyncOptions PerforceSyncOptions;
		public List<PerforceFileRecord> HaveFiles; // Cached when sync filter has changed

		public WorkspaceUpdateContext(int InChangeNumber, WorkspaceUpdateOptions InOptions, string[] InSyncFilter, Dictionary<Guid, ConfigObject> InDefaultBuildSteps, List<ConfigObject> InUserBuildSteps, HashSet<Guid> InCustomBuildSteps, Dictionary<string, string> InVariables)
		{
			ChangeNumber = InChangeNumber;
			Options = InOptions;
			SyncFilter = InSyncFilter;
			DefaultBuildSteps = InDefaultBuildSteps;
			UserBuildStepObjects = InUserBuildSteps;
			CustomBuildSteps = InCustomBuildSteps;
			Variables = InVariables;
		}
	}

	class WorkspaceSyncCategory
	{
		public Guid UniqueId;
		public bool bEnable;
		public string Name;
		public string[] Paths;
		public bool bHidden;
		public Guid[] Requires;

		public WorkspaceSyncCategory(Guid UniqueId) : this(UniqueId, "Unnamed")
		{
		}

		public WorkspaceSyncCategory(Guid UniqueId, string Name, params string[] Paths)
		{
			this.UniqueId = UniqueId;
			this.bEnable = true;
			this.Name = Name;
			this.Paths = Paths;
			this.Requires = new Guid[0];
		}

		public static Dictionary<Guid, bool> GetDefault(IEnumerable<WorkspaceSyncCategory> Categories)
		{
			return Categories.ToDictionary(x => x.UniqueId, x => x.bEnable);
		}

		public static Dictionary<Guid, bool> GetDelta(Dictionary<Guid, bool> Source, Dictionary<Guid, bool> Target)
		{
			Dictionary<Guid, bool> Changes = new Dictionary<Guid, bool>();
			foreach (KeyValuePair<Guid, bool> Pair in Target)
			{
				bool bValue;
				if (!Source.TryGetValue(Pair.Key, out bValue) || bValue != Pair.Value)
				{
					Changes[Pair.Key] = Pair.Value;
				}
			}
			return Changes;
		}

		public static void ApplyDelta(Dictionary<Guid, bool> Categories, Dictionary<Guid, bool> Delta)
		{
			foreach(KeyValuePair<Guid, bool> Pair in Delta)
			{
				Categories[Pair.Key] = Pair.Value;
			}
		}

		public override string ToString()
		{
			return Name;
		}
	}

	class Workspace : IDisposable
	{
		const string BuildVersionFileName = "/Engine/Build/Build.version";
		const string VersionHeaderFileName = "/Engine/Source/Runtime/Launch/Resources/Version.h";
		const string ObjectVersionFileName = "/Engine/Source/Runtime/Core/Private/UObject/ObjectVersion.cpp";

		static readonly string LocalVersionHeaderFileName = VersionHeaderFileName.Replace('/', '\\');
		static readonly string LocalObjectVersionFileName = ObjectVersionFileName.Replace('/', '\\');

		public readonly PerforceConnection Perforce;
		public readonly string LocalRootPath;
		public readonly string LocalRootPrefix;
		public readonly string SelectedLocalFileName;
		public readonly string ClientRootPath;
		public readonly string SelectedClientFileName;
		public readonly string TelemetryProjectPath;
		public readonly bool bIsEnterpriseProject;
		Thread WorkerThread;
		TextWriter Log;
		bool bSyncing;
		ProgressValue Progress = new ProgressValue();

		static Workspace ActiveWorkspace;

		public event Action<WorkspaceUpdateContext, WorkspaceUpdateResult, string> OnUpdateComplete;

		class RecordCounter : IDisposable
		{
			ProgressValue Progress;
			string Message;
			int Count;
			Stopwatch Timer = Stopwatch.StartNew();

			public RecordCounter(ProgressValue Progress, string Message)
			{
				this.Progress = Progress;
				this.Message = Message;

				Progress.Set(Message);
			}

			public void Dispose()
			{
				UpdateMessage();
			}

			public void Increment()
			{
				Count++;
				if(Timer.ElapsedMilliseconds > 250)
				{
					UpdateMessage();
				}
			}

			public void UpdateMessage()
			{
				Progress.Set(String.Format("{0} ({1:N0})", Message, Count));
				Timer.Restart();
			}
		}

		class SyncBatchBuilder
		{
			public int MaxCommandsPerList { get; }
			public long MaxSizePerList { get; }
			public Queue<List<string>> Batches { get; }

			List<string> Commands;
			long Size;

			public SyncBatchBuilder(int MaxCommandsPerList, long MaxSizePerList)
			{
				this.MaxCommandsPerList = MaxCommandsPerList;
				this.MaxSizePerList = MaxSizePerList;
				this.Batches = new Queue<List<string>>();
			}

			public void Add(string NewCommand, long NewSize)
			{
				if (Commands == null || Commands.Count >= MaxCommandsPerList || Size + NewSize >= MaxSizePerList)
				{
					Commands = new List<string>();
					Batches.Enqueue(Commands);
					Size = 0;
				}

				Commands.Add(NewCommand);
				Size += NewSize;
			}
		}

		class SyncTree
		{
			public bool bCanUseWildcard;
			public int TotalIncludedFiles;
			public long TotalSize;
			public int TotalExcludedFiles;
			public Dictionary<string, long> IncludedFiles = new Dictionary<string, long>();
			public Dictionary<string, SyncTree> NameToSubTree = new Dictionary<string, SyncTree>(StringComparer.OrdinalIgnoreCase);

			public SyncTree(bool bCanUseWildcard)
			{
				this.bCanUseWildcard = bCanUseWildcard;
			}

			public SyncTree FindOrAddSubTree(string Name)
			{
				SyncTree Result;
				if (!NameToSubTree.TryGetValue(Name, out Result))
				{
					Result = new SyncTree(bCanUseWildcard);
					NameToSubTree.Add(Name, Result);
				}
				return Result;
			}

			public void IncludeFile(string Path, long Size)
			{
				int Idx = Path.IndexOf('/');
				if (Idx == -1)
				{
					IncludedFiles.Add(Path, Size);
				}
				else
				{
					SyncTree SubTree = FindOrAddSubTree(Path.Substring(0, Idx));
					SubTree.IncludeFile(Path.Substring(Idx + 1), Size);
				}
				TotalIncludedFiles++;
				TotalSize += Size;
			}

			public void ExcludeFile(string Path)
			{
				int Idx = Path.IndexOf('/');
				if (Idx != -1)
				{
					SyncTree SubTree = FindOrAddSubTree(Path.Substring(0, Idx));
					SubTree.ExcludeFile(Path.Substring(Idx + 1));
				}
				TotalExcludedFiles++;
			}

			public void GetOptimizedSyncCommands(string Prefix, int ChangeNumber, SyncBatchBuilder Builder)
			{
				if (bCanUseWildcard && TotalExcludedFiles == 0 && TotalSize < Builder.MaxSizePerList)
				{
					Builder.Add(String.Format("{0}/...@{1}", Prefix, ChangeNumber), TotalSize);
				}
				else
				{
					foreach (KeyValuePair<string, long> File in IncludedFiles)
					{
						Builder.Add(String.Format("{0}/{1}@{2}", Prefix, File.Key, ChangeNumber), File.Value);
					}
					foreach (KeyValuePair<string, SyncTree> Pair in NameToSubTree)
					{
						Pair.Value.GetOptimizedSyncCommands(String.Format("{0}/{1}", Prefix, PerforceUtils.EscapePath(Pair.Key)), ChangeNumber, Builder);
					}
				}
			}
		}

		public Workspace(PerforceConnection InPerforce, string InLocalRootPath, string InSelectedLocalFileName, string InClientRootPath, string InSelectedClientFileName, int InInitialChangeNumber, string InInitialSyncFilterHash, int InLastBuiltChangeNumber, string InTelemetryProjectPath, bool bInIsEnterpriseProject, TextWriter InLog)
		{
			Perforce = InPerforce;
			LocalRootPath = InLocalRootPath;
			LocalRootPrefix = InLocalRootPath.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
			SelectedLocalFileName = InSelectedLocalFileName;
			ClientRootPath = InClientRootPath;
			SelectedClientFileName = InSelectedClientFileName;
			TelemetryProjectPath = InTelemetryProjectPath;
			CurrentChangeNumber = InInitialChangeNumber;
			CurrentSyncFilterHash = InInitialSyncFilterHash;
			PendingChangeNumber = InInitialChangeNumber;
			LastBuiltChangeNumber = InLastBuiltChangeNumber;
			bIsEnterpriseProject = bInIsEnterpriseProject;
			Log = InLog;

			ProjectConfigFile = ReadProjectConfigFile(InLocalRootPath, InSelectedLocalFileName, Log);
			ProjectStreamFilter = ReadProjectStreamFilter(Perforce, ProjectConfigFile, Log);
		}

		public void Dispose()
		{
			CancelUpdate();
		}

		public ConfigFile ProjectConfigFile
		{
			get; private set;
		}

		public IReadOnlyList<string> ProjectStreamFilter
		{
			get; private set;
		}

		public void Update(WorkspaceUpdateContext Context)
		{
			// Kill any existing sync
			CancelUpdate();

			// Set the initial progress message
			if(CurrentChangeNumber != Context.ChangeNumber)
			{
				PendingChangeNumber = Context.ChangeNumber;
				if(!Context.Options.HasFlag(WorkspaceUpdateOptions.SyncSingleChange))
				{
					CurrentChangeNumber = -1;
				}
			}
			Progress.Clear();
			bSyncing = true;

			// Spawn the new thread
			WorkerThread = new Thread(x => UpdateWorkspace(Context));
			WorkerThread.Start();
		}

		public void CancelUpdate()
		{
			if(bSyncing)
			{
				Log.WriteLine("OPERATION ABORTED");
				if(WorkerThread != null)
				{
					WorkerThread.Abort();
					WorkerThread.Join();
					WorkerThread = null;
				}
				PendingChangeNumber = CurrentChangeNumber;
				bSyncing = false;
				Interlocked.CompareExchange(ref ActiveWorkspace, null, this);
			}
		}

		void UpdateWorkspace(WorkspaceUpdateContext Context)
		{
			string StatusMessage;

			WorkspaceUpdateResult Result = WorkspaceUpdateResult.FailedToSync;
			try
			{
				Result = UpdateWorkspaceInternal(Context, out StatusMessage);
				if(Result != WorkspaceUpdateResult.Success)
				{
					Log.WriteLine("{0}", StatusMessage);
				}
			}
			catch(ThreadAbortException)
			{
				StatusMessage = "Canceled.";
				Log.WriteLine("Canceled.");
			}
			catch(Exception Ex)
			{
				StatusMessage = "Failed with exception - " + Ex.ToString();
				Log.WriteException(Ex, "Failed with exception");
			}

			bSyncing = false;
			PendingChangeNumber = CurrentChangeNumber;
			Interlocked.CompareExchange(ref ActiveWorkspace, null, this);

			if(OnUpdateComplete != null)
			{
				OnUpdateComplete(Context, Result, StatusMessage);
			}
		}

		WorkspaceUpdateResult UpdateWorkspaceInternal(WorkspaceUpdateContext Context, out string StatusMessage)
		{
			string CmdExe = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "cmd.exe");
			if(!File.Exists(CmdExe))
			{
				StatusMessage = String.Format("Missing {0}.", CmdExe);
				return WorkspaceUpdateResult.FailedToSync;
			}

			List<Tuple<string, TimeSpan>> Times = new List<Tuple<string,TimeSpan>>();

			int NumFilesSynced = 0;
			if(Context.Options.HasFlag(WorkspaceUpdateOptions.Sync) || Context.Options.HasFlag(WorkspaceUpdateOptions.SyncSingleChange))
			{
				using(TelemetryStopwatch SyncTelemetryStopwatch = new TelemetryStopwatch("Workspace_Sync", TelemetryProjectPath))
				{
					Log.WriteLine("Syncing to {0}...", PendingChangeNumber);

					// Make sure we're logged in
					bool bLoggedIn;
					if(!Perforce.GetLoggedInState(out bLoggedIn, Log))
					{
						StatusMessage = "Unable to get login status.";
						return WorkspaceUpdateResult.FailedToSync;
					}
					if(!bLoggedIn)
					{
						StatusMessage = "User is not logged in.";
						return WorkspaceUpdateResult.FailedToSyncLoginExpired;
					}

					// Figure out which paths to sync
					List<string> RelativeSyncPaths = GetRelativeSyncPaths((Context.Options & WorkspaceUpdateOptions.SyncAllProjects) != 0, Context.SyncFilter);
					List<string> SyncPaths = new List<string>(RelativeSyncPaths.Select(x => ClientRootPath + x));

					// Get the user's sync filter
					FileFilter UserFilter = new FileFilter(FileFilterType.Include);
					if(Context.SyncFilter != null)
					{
						UserFilter.AddRules(Context.SyncFilter.Select(x => x.Trim()).Where(x => x.Length > 0 && !x.StartsWith(";") && !x.StartsWith("#")));
					}

					// Check if the new sync filter matches the previous one. If not, we'll enumerate all files in the workspace and make sure there's nothing extra there.
					string NextSyncFilterHash = null;
					using (SHA1Managed SHA = new SHA1Managed())
					{
						StringBuilder CombinedFilter = new StringBuilder();
						foreach(string RelativeSyncPath in RelativeSyncPaths)
						{
							CombinedFilter.AppendFormat("{0}\n", RelativeSyncPath);
						}
						if(Context.SyncFilter != null)
						{
							CombinedFilter.Append("--FROM--\n");
							CombinedFilter.Append(String.Join("\n", Context.SyncFilter));
						}
						NextSyncFilterHash = BitConverter.ToString(SHA.ComputeHash(Encoding.UTF8.GetBytes(CombinedFilter.ToString()))).Replace("-", "");
					}

					// If the hash differs, enumerate everything in the workspace to find what needs to be removed
					if (NextSyncFilterHash != CurrentSyncFilterHash)
					{
						using (TelemetryStopwatch FilterStopwatch = new TelemetryStopwatch("Workspace_Sync_FilterChanged", TelemetryProjectPath))
						{
							Log.WriteLine("Filter has changed ({0} -> {1}); finding files in workspace that need to be removed.", (String.IsNullOrEmpty(CurrentSyncFilterHash)) ? "None" : CurrentSyncFilterHash, NextSyncFilterHash);

							// Find all the files that are in this workspace
							List<PerforceFileRecord> HaveFiles = Context.HaveFiles;
							if (HaveFiles == null)
							{
								HaveFiles = new List<PerforceFileRecord>();
								using (RecordCounter HaveCounter = new RecordCounter(Progress, "Sync filter changed; checking workspace..."))
								{
									if (!Perforce.Have("//...", Record => { HaveFiles.Add(Record); HaveCounter.Increment(); }, Log))
									{
										StatusMessage = "Unable to query files.";
										return WorkspaceUpdateResult.FailedToSync;
									}
								}
								Context.HaveFiles = HaveFiles;
							}

							// Build a filter for the current sync paths
							FileFilter SyncPathsFilter = new FileFilter(FileFilterType.Exclude);
							foreach (string RelativeSyncPath in RelativeSyncPaths)
							{
								SyncPathsFilter.Include(RelativeSyncPath);
							}

							// Remove all the files that are not included by the filter
							List<string> RemoveDepotPaths = new List<string>();
							foreach (PerforceFileRecord HaveFile in HaveFiles)
							{
								try
								{
									string FullPath = Path.GetFullPath(HaveFile.Path);
									if (MatchFilter(FullPath, SyncPathsFilter) && !MatchFilter(FullPath, UserFilter))
									{
										Log.WriteLine("  {0}", HaveFile.DepotPath);
										RemoveDepotPaths.Add(HaveFile.DepotPath);
									}
								}
								catch (PathTooLongException)
								{
									// We don't actually care about this when looking for files to remove. Perforce may think that it's synced the path, and silently failed. Just ignore it.
								}
							}

							// Check if there are any paths outside the regular sync paths
							if (RemoveDepotPaths.Count > 0)
							{
								bool bDeleteListMatches = true;

								Dictionary<string, bool> NewDeleteFiles = new Dictionary<string, bool>(StringComparer.OrdinalIgnoreCase);
								foreach (string RemoveDepotPath in RemoveDepotPaths)
								{
									bool bDelete;
									if (!Context.DeleteFiles.TryGetValue(RemoveDepotPath, out bDelete))
									{
										bDeleteListMatches = false;
										bDelete = true;
									}
									NewDeleteFiles[RemoveDepotPath] = bDelete;
								}
								Context.DeleteFiles = NewDeleteFiles;

								if (!bDeleteListMatches)
								{
									StatusMessage = String.Format("Cancelled after finding {0} files excluded by filter", NewDeleteFiles.Count);
									return WorkspaceUpdateResult.FilesToDelete;
								}

								RemoveDepotPaths.RemoveAll(x => !Context.DeleteFiles[x]);
							}

							// Actually delete any files that we don't want
							if (RemoveDepotPaths.Count > 0)
							{
								// Clear the current sync filter hash. If the sync is canceled, we'll be in an indeterminate state, and we should always clean next time round.
								CurrentSyncFilterHash = "INVALID";

								// Find all the depot paths that will be synced
								HashSet<string> RemainingDepotPathsToRemove = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
								RemainingDepotPathsToRemove.UnionWith(RemoveDepotPaths);

								// Build the list of revisions to sync
								List<string> RevisionsToRemove = new List<string>();
								RevisionsToRemove.AddRange(RemoveDepotPaths.Select(x => String.Format("{0}#0", x)));

								WorkspaceUpdateResult RemoveResult = SyncFileRevisions("Removing files...", Context, RevisionsToRemove, RemainingDepotPathsToRemove, out StatusMessage);
								if (RemoveResult != WorkspaceUpdateResult.Success)
								{
									return RemoveResult;
								}
							}

							// Update the sync filter hash. We've removed any files we need to at this point.
							CurrentSyncFilterHash = NextSyncFilterHash;
						}
					}

					// Create a filter for all the files we don't want
					FileFilter Filter = new FileFilter(UserFilter);
					Filter.Exclude(BuildVersionFileName);
					if (Context.Options.HasFlag(WorkspaceUpdateOptions.ContentOnly))
					{
						Filter.Exclude("*.usf");
						Filter.Exclude("*.ush");
					}

					// Create a tree to store the sync path
					SyncTree SyncTree = new SyncTree(false);
					if (!Context.Options.HasFlag(WorkspaceUpdateOptions.SyncSingleChange))
					{
						foreach (string RelativeSyncPath in RelativeSyncPaths)
						{
							const string WildcardSuffix = "/...";
							if (RelativeSyncPath.EndsWith(WildcardSuffix, StringComparison.Ordinal))
							{
								SyncTree Leaf = SyncTree;

								string[] Fragments = RelativeSyncPath.Split('/');
								for (int Idx = 1; Idx < Fragments.Length - 1; Idx++)
								{
									Leaf = Leaf.FindOrAddSubTree(Fragments[Idx]);
								}

								Leaf.bCanUseWildcard = true;
							}
						}
					}

					// Find all the server changes, and anything that's opened for edit locally. We need to sync files we have open to schedule a resolve.
					SyncBatchBuilder BatchBuilder = new SyncBatchBuilder(200, 100 * 1024 * 1024);
					List<string> SyncDepotPaths = new List<string>();
					using(RecordCounter Counter = new RecordCounter(Progress, "Filtering files..."))
					{
						foreach(string SyncPath in SyncPaths)
						{
							List<PerforceFileRecord> SyncRecords = new List<PerforceFileRecord>();
							if(!Perforce.SyncPreview(SyncPath, PendingChangeNumber, !Context.Options.HasFlag(WorkspaceUpdateOptions.Sync), Record => { SyncRecords.Add(Record); Counter.Increment(); }, Log))
							{
								StatusMessage = String.Format("Couldn't enumerate changes matching {0}.", SyncPath);
								return WorkspaceUpdateResult.FailedToSync;
							}

							List<PerforceFileRecord> OpenRecords;
							if (!Perforce.GetOpenFiles(SyncPath, out OpenRecords, Log))
							{
								StatusMessage = String.Format("Couldn't find open files matching {0}.", SyncPath);
								return WorkspaceUpdateResult.FailedToSync;
							}

							SyncRecords.AddRange(OpenRecords.Where(x => x.Action != "add" && x.Action != "branch" && x.Action != "move/add"));

							// Enumerate all the files to be synced. NOTE: depotPath is escaped, whereas clientPath is not.
							foreach (PerforceFileRecord SyncRecord in SyncRecords)
							{
								// If it doesn't exist locally, just add a sync command for it
								if (String.IsNullOrEmpty(SyncRecord.ClientPath))
								{
									BatchBuilder.Add(String.Format("{0}@{1}", SyncRecord.DepotPath, PendingChangeNumber), SyncRecord.FileSize);
									SyncDepotPaths.Add(SyncRecord.DepotPath);
									continue;
								}

								// Get the full local path
								string FullName;
								try
								{
									FullName = Path.GetFullPath(SyncRecord.ClientPath);
								}
								catch(PathTooLongException)
								{
									Log.WriteLine("The local path for {0} exceeds the maximum allowed by Windows. Re-sync your workspace to a directory with a shorter name, or delete the file from the server.", SyncRecord.ClientPath);
									StatusMessage = "File exceeds maximum path length allowed by Windows.";
									return WorkspaceUpdateResult.FailedToSync;
								}

								// Make sure it's under the current directory. Not sure why this would happen, just being safe.
								if (!FullName.StartsWith(LocalRootPrefix, StringComparison.OrdinalIgnoreCase))
								{
									BatchBuilder.Add(String.Format("{0}@{1}", SyncRecord.DepotPath, PendingChangeNumber), SyncRecord.FileSize);
									SyncDepotPaths.Add(SyncRecord.DepotPath);
									continue;
								}

								// Check that it matches the filter
								string RelativePath = FullName.Substring(LocalRootPrefix.Length).Replace('\\', '/');
								if (Filter.Matches(RelativePath))
								{
									SyncTree.IncludeFile(PerforceUtils.EscapePath(RelativePath), SyncRecord.FileSize);
									SyncDepotPaths.Add(SyncRecord.DepotPath);
								}
								else
								{
									SyncTree.ExcludeFile(PerforceUtils.EscapePath(RelativePath));
								}
							}
						}
					}
					SyncTree.GetOptimizedSyncCommands(ClientRootPath, PendingChangeNumber, BatchBuilder);

					// Find all the depot paths that will be synced
					HashSet<string> RemainingDepotPaths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
					RemainingDepotPaths.UnionWith(SyncDepotPaths);

					using (TelemetryStopwatch TransferStopwatch = new TelemetryStopwatch("Workspace_Sync_TransferFiles", TelemetryProjectPath))
					{
						TransferStopwatch.AddData(new { MachineName = Environment.MachineName, DomainName = Environment.UserDomainName, ServerAndPort = Perforce.ServerAndPort, UserName = Perforce.UserName, IncludedFiles = SyncTree.TotalIncludedFiles, ExcludedFiles = SyncTree.TotalExcludedFiles, Size = SyncTree.TotalSize, NumThreads = Context.PerforceSyncOptions.NumThreads });

						WorkspaceUpdateResult SyncResult = SyncFileRevisions("Syncing files...", Context, BatchBuilder.Batches, RemainingDepotPaths, out StatusMessage);
						if (SyncResult != WorkspaceUpdateResult.Success)
						{
							TransferStopwatch.AddData(new { SyncResult = SyncResult.ToString(), CompletedFilesFiles = SyncDepotPaths.Count - RemainingDepotPaths.Count });
							return SyncResult;
						}

						TransferStopwatch.Stop("Ok");
						TransferStopwatch.AddData(new { TransferRate = SyncTree.TotalSize / Math.Max(TransferStopwatch.Elapsed.TotalSeconds, 0.0001f) });
					}

					int VersionChangeNumber = -1;
					if(Context.Options.HasFlag(WorkspaceUpdateOptions.Sync) && !Context.Options.HasFlag(WorkspaceUpdateOptions.UpdateFilter))
					{
						// Read the new config file
						ProjectConfigFile = ReadProjectConfigFile(LocalRootPath, SelectedLocalFileName, Log);
						ProjectStreamFilter = ReadProjectStreamFilter(Perforce, ProjectConfigFile, Log);

						// Get the branch name
						string BranchOrStreamName;
						if(Perforce.GetActiveStream(out BranchOrStreamName, Log))
						{
							// If it's a virtual stream, take the concrete parent stream instead
							for (;;)
							{
								PerforceSpec StreamSpec;
								if (!Perforce.TryGetStreamSpec(BranchOrStreamName, out StreamSpec, Log))
								{
									StatusMessage = String.Format("Unable to get stream spec for {0}.", BranchOrStreamName);
									return WorkspaceUpdateResult.FailedToSync;
								}
								if (StreamSpec.GetField("Type") != "virtual")
								{
									break;
								}
								BranchOrStreamName = StreamSpec.GetField("Parent");
							}
						}
						else
						{
							// Otherwise use the depot path for GenerateProjectFiles.bat in the root of the workspace
							string DepotFileName;
							if(!Perforce.ConvertToDepotPath(ClientRootPath + "/GenerateProjectFiles.bat", out DepotFileName, Log))
							{
								StatusMessage = String.Format("Couldn't determine branch name for {0}.", SelectedClientFileName);
								return WorkspaceUpdateResult.FailedToSync;
							}
							BranchOrStreamName = PerforceUtils.GetClientOrDepotDirectoryName(DepotFileName);
						}

						// Find the last code change before this changelist. For consistency in versioning between local builds and precompiled binaries, we need to use the last submitted code changelist as our version number.
						List<PerforceChangeSummary> CodeChanges;
						if(!Perforce.FindChanges(new string[]{ ".cs", ".h", ".cpp", ".usf", ".ush", ".uproject", ".uplugin" }.SelectMany(x => SyncPaths.Select(y => String.Format("{0}{1}@<={2}", y, x, PendingChangeNumber))), 1, out CodeChanges, Log))
						{
							StatusMessage = String.Format("Couldn't determine last code changelist before CL {0}.", PendingChangeNumber);
							return WorkspaceUpdateResult.FailedToSync;
						}
						if(CodeChanges.Count == 0)
						{
							StatusMessage = String.Format("Could not find any code changes before CL {0}.", PendingChangeNumber);
							return WorkspaceUpdateResult.FailedToSync;
						}

						// Get the last code change
						if(ProjectConfigFile.GetValue("Options.VersionToLastCodeChange", true))
						{
							VersionChangeNumber = CodeChanges.Max(x => x.Number);
						}
						else
						{
							VersionChangeNumber = PendingChangeNumber;
						}

						// Update the version files
						if(ProjectConfigFile.GetValue("Options.UseFastModularVersioningV2", false))
						{
							bool bIsLicenseeVersion = IsLicenseeVersion();
							if (!UpdateVersionFile(ClientRootPath + BuildVersionFileName, PendingChangeNumber, Text => UpdateBuildVersion(Text, PendingChangeNumber, VersionChangeNumber, BranchOrStreamName, bIsLicenseeVersion)))
							{
								StatusMessage = String.Format("Failed to update {0}.", BuildVersionFileName);
								return WorkspaceUpdateResult.FailedToSync;
							}
						}
						else if(ProjectConfigFile.GetValue("Options.UseFastModularVersioning", false))
						{
							bool bIsLicenseeVersion = IsLicenseeVersion();
							if (!UpdateVersionFile(ClientRootPath + BuildVersionFileName, PendingChangeNumber, Text => UpdateBuildVersion(Text, PendingChangeNumber, VersionChangeNumber, BranchOrStreamName, bIsLicenseeVersion)))
							{
								StatusMessage = String.Format("Failed to update {0}.", BuildVersionFileName);
								return WorkspaceUpdateResult.FailedToSync;
							}

							Dictionary<string, string> VersionHeaderStrings = new Dictionary<string,string>();
							VersionHeaderStrings["#define ENGINE_IS_PROMOTED_BUILD"] = " (0)";
							VersionHeaderStrings["#define BUILT_FROM_CHANGELIST"] = " 0";
							VersionHeaderStrings["#define BRANCH_NAME"] = " \"" + BranchOrStreamName.Replace('/', '+') + "\"";
							if(!UpdateVersionFile(ClientRootPath + VersionHeaderFileName, VersionHeaderStrings, PendingChangeNumber))
							{
								StatusMessage = String.Format("Failed to update {0}.", VersionHeaderFileName);
								return WorkspaceUpdateResult.FailedToSync;
							}
							if(!UpdateVersionFile(ClientRootPath + ObjectVersionFileName, new Dictionary<string,string>(), PendingChangeNumber))
							{
								StatusMessage = String.Format("Failed to update {0}.", ObjectVersionFileName);
								return WorkspaceUpdateResult.FailedToSync;
							}
						}
						else
						{
							if(!UpdateVersionFile(ClientRootPath + BuildVersionFileName, new Dictionary<string, string>(), PendingChangeNumber))
							{
								StatusMessage = String.Format("Failed to update {0}", BuildVersionFileName);
								return WorkspaceUpdateResult.FailedToSync;
							}

							Dictionary<string, string> VersionStrings = new Dictionary<string,string>();
							VersionStrings["#define ENGINE_VERSION"] = " " + VersionChangeNumber.ToString();
							VersionStrings["#define ENGINE_IS_PROMOTED_BUILD"] = " (0)";
							VersionStrings["#define BUILT_FROM_CHANGELIST"] = " " + VersionChangeNumber.ToString();
							VersionStrings["#define BRANCH_NAME"] = " \"" + BranchOrStreamName.Replace('/', '+') + "\"";
							if(!UpdateVersionFile(ClientRootPath + VersionHeaderFileName, VersionStrings, PendingChangeNumber))
							{
								StatusMessage = String.Format("Failed to update {0}", VersionHeaderFileName);
								return WorkspaceUpdateResult.FailedToSync;
							}
							if(!UpdateVersionFile(ClientRootPath + ObjectVersionFileName, VersionStrings, PendingChangeNumber))
							{
								StatusMessage = String.Format("Failed to update {0}", ObjectVersionFileName);
								return WorkspaceUpdateResult.FailedToSync;
							}
						}

						// Remove all the receipts for build targets in this branch
						if(SelectedClientFileName.EndsWith(".uproject", StringComparison.OrdinalIgnoreCase))
						{
							Perforce.Sync(PerforceUtils.GetClientOrDepotDirectoryName(SelectedClientFileName) + "/Build/Receipts/...#0", Log);
						}
					}

					// Check if there are any files which need resolving
					List<PerforceFileRecord> UnresolvedFiles;
					if(!FindUnresolvedFiles(SyncPaths, out UnresolvedFiles))
					{
						StatusMessage = "Couldn't get list of unresolved files.";
						return WorkspaceUpdateResult.FailedToSync;
					}
					if(UnresolvedFiles.Count > 0 && Context.Options.HasFlag(WorkspaceUpdateOptions.AutoResolveChanges))
					{
						foreach (PerforceFileRecord UnresolvedFile in UnresolvedFiles)
						{
							Perforce.AutoResolveFile(UnresolvedFile.DepotPath, Log);
						}
						if(!FindUnresolvedFiles(SyncPaths, out UnresolvedFiles))
						{
							StatusMessage = "Couldn't get list of unresolved files.";
							return WorkspaceUpdateResult.FailedToSync;
						}
					}
					if(UnresolvedFiles.Count > 0)
					{
						Log.WriteLine("{0} files need resolving:", UnresolvedFiles.Count);
						foreach(PerforceFileRecord UnresolvedFile in UnresolvedFiles)
						{
							Log.WriteLine("  {0}", UnresolvedFile.ClientPath);
						}
						StatusMessage = "Files need resolving.";
						return WorkspaceUpdateResult.FilesToResolve;
					}

					// Continue processing sync-only actions
					if (Context.Options.HasFlag(WorkspaceUpdateOptions.Sync) && !Context.Options.HasFlag(WorkspaceUpdateOptions.UpdateFilter))
					{
						// Execute any project specific post-sync steps
						string[] PostSyncSteps = ProjectConfigFile.GetValues("Sync.Step", null);
						if (PostSyncSteps != null)
						{
							Log.WriteLine();
							Log.WriteLine("Executing post-sync steps...");

							Dictionary<string, string> PostSyncVariables = new Dictionary<string, string>(Context.Variables);
							PostSyncVariables["Change"] = PendingChangeNumber.ToString();
							PostSyncVariables["CodeChange"] = VersionChangeNumber.ToString();

							foreach (string PostSyncStep in PostSyncSteps.Select(x => x.Trim()))
							{
								ConfigObject PostSyncStepObject = new ConfigObject(PostSyncStep);

								string ToolFileName = Utility.ExpandVariables(PostSyncStepObject.GetValue("FileName", ""), PostSyncVariables);
								if (ToolFileName != null)
								{
									string ToolArguments = Utility.ExpandVariables(PostSyncStepObject.GetValue("Arguments", ""), PostSyncVariables);

									Log.WriteLine("post-sync> Running {0} {1}", ToolFileName, ToolArguments);

									int ResultFromTool = Utility.ExecuteProcess(ToolFileName, null, ToolArguments, null, new ProgressTextWriter(Progress, new PrefixedTextWriter("post-sync> ", Log)));
									if (ResultFromTool != 0)
									{
										StatusMessage = String.Format("Post-sync step terminated with exit code {0}.", ResultFromTool);
										return WorkspaceUpdateResult.FailedToSync;
									}
								}
							}
						}

						// Update the current change number. Everything else happens for the new change.
						CurrentChangeNumber = PendingChangeNumber;
					}

					// Update the timing info
					Times.Add(new Tuple<string,TimeSpan>("Sync", SyncTelemetryStopwatch.Stop("Success")));

					// Save the number of files synced
					NumFilesSynced = SyncDepotPaths.Count;
					Log.WriteLine();
				}
			}

			// Extract an archive from the depot path
			if(Context.Options.HasFlag(WorkspaceUpdateOptions.SyncArchives))
			{
				using(TelemetryStopwatch Stopwatch = new TelemetryStopwatch("Workspace_SyncArchives", TelemetryProjectPath))
				{
					// Create the directory for extracted archive manifests
					string ManifestDirectoryName;
					if(SelectedLocalFileName.EndsWith(".uproject", StringComparison.OrdinalIgnoreCase))
					{
						ManifestDirectoryName = Path.Combine(Path.GetDirectoryName(SelectedLocalFileName), "Saved", "UnrealGameSync");
					}
					else
					{
						ManifestDirectoryName = Path.Combine(Path.GetDirectoryName(SelectedLocalFileName), "Engine", "Saved", "UnrealGameSync");
					}
					Directory.CreateDirectory(ManifestDirectoryName);

					// Sync and extract (or just remove) the given archives
					foreach(KeyValuePair<string, string> ArchiveTypeAndDepotPath in Context.ArchiveTypeToDepotPath)
					{
						// Remove any existing binaries
						string ManifestFileName = Path.Combine(ManifestDirectoryName, String.Format("{0}.zipmanifest", ArchiveTypeAndDepotPath.Key));
						if(File.Exists(ManifestFileName))
						{
							Log.WriteLine("Removing {0} binaries...", ArchiveTypeAndDepotPath.Key);
							Progress.Set(String.Format("Removing {0} binaries...", ArchiveTypeAndDepotPath.Key), 0.0f);
							ArchiveUtils.RemoveExtractedFiles(LocalRootPath, ManifestFileName, Progress, Log);
							File.Delete(ManifestFileName);
							Log.WriteLine();
						}

						// If we have a new depot path, sync it down and extract it
						if(ArchiveTypeAndDepotPath.Value != null)
						{
							string TempZipFileName = Path.GetTempFileName();
							try
							{
								Log.WriteLine("Syncing {0} binaries...", ArchiveTypeAndDepotPath.Key.ToLowerInvariant());
								Progress.Set(String.Format("Syncing {0} binaries...", ArchiveTypeAndDepotPath.Key.ToLowerInvariant()), 0.0f);
								if(!Perforce.PrintToFile(ArchiveTypeAndDepotPath.Value, TempZipFileName, Log) || new FileInfo(TempZipFileName).Length == 0)
								{
									StatusMessage = String.Format("Couldn't read {0}", ArchiveTypeAndDepotPath.Value);
									return WorkspaceUpdateResult.FailedToSync;
								}
								ArchiveUtils.ExtractFiles(TempZipFileName, LocalRootPath, ManifestFileName, Progress, Log);
								Log.WriteLine();
							}
							finally
							{
								File.SetAttributes(TempZipFileName, FileAttributes.Normal);
								File.Delete(TempZipFileName);
							}
						}
					}

					// Add the finish time
					Times.Add(new Tuple<string,TimeSpan>("Archive", Stopwatch.Stop("Success")));
				}
			}

			// Take the lock before doing anything else. Building and generating project files can only be done on one workspace at a time.
			if(Context.Options.HasFlag(WorkspaceUpdateOptions.GenerateProjectFiles) || Context.Options.HasFlag(WorkspaceUpdateOptions.Build))
			{
				if(Interlocked.CompareExchange(ref ActiveWorkspace, this, null) != null)
				{
					Log.WriteLine("Waiting for other workspaces to finish...");
					while(Interlocked.CompareExchange(ref ActiveWorkspace, this, null) != null)
					{
						Thread.Sleep(100);
					}
				}
			}

			// Generate project files in the workspace
			if(Context.Options.HasFlag(WorkspaceUpdateOptions.GenerateProjectFiles))
			{
				using(TelemetryStopwatch Stopwatch = new TelemetryStopwatch("Workspace_GenerateProjectFiles", TelemetryProjectPath))
				{
					Progress.Set("Generating project files...", 0.0f);

					StringBuilder CommandLine = new StringBuilder();
					CommandLine.AppendFormat("/C \"\"{0}\"", Path.Combine(LocalRootPath, "GenerateProjectFiles.bat"));
					if((Context.Options & WorkspaceUpdateOptions.SyncAllProjects) == 0 && (Context.Options & WorkspaceUpdateOptions.IncludeAllProjectsInSolution) == 0)
					{
						if(SelectedLocalFileName.EndsWith(".uproject", StringComparison.OrdinalIgnoreCase))
						{
							CommandLine.AppendFormat(" \"{0}\"", SelectedLocalFileName);
						}
					}
					CommandLine.Append(" -progress\"");

					Log.WriteLine("Generating project files...");
					Log.WriteLine("gpf> Running {0} {1}", CmdExe, CommandLine);

					int GenerateProjectFilesResult = Utility.ExecuteProcess(CmdExe, null, CommandLine.ToString(), null, new ProgressTextWriter(Progress, new PrefixedTextWriter("gpf> ", Log)));
					if(GenerateProjectFilesResult != 0)
					{
						StatusMessage = String.Format("Failed to generate project files (exit code {0}).", GenerateProjectFilesResult);
						return WorkspaceUpdateResult.FailedToCompile;
					}

					Log.WriteLine();
					Times.Add(new Tuple<string,TimeSpan>("Prj gen", Stopwatch.Stop("Success")));
				}
			}

			// Build everything using MegaXGE
			if(Context.Options.HasFlag(WorkspaceUpdateOptions.Build))
			{
				// Compile all the build steps together
				Dictionary<Guid, ConfigObject> BuildStepObjects = Context.DefaultBuildSteps.ToDictionary(x => x.Key, x => new ConfigObject(x.Value));
				BuildStep.MergeBuildStepObjects(BuildStepObjects, ProjectConfigFile.GetValues("Build.Step", new string[0]).Select(x => new ConfigObject(x)));
				BuildStep.MergeBuildStepObjects(BuildStepObjects, Context.UserBuildStepObjects);

				// Construct build steps from them
				List<BuildStep> BuildSteps = BuildStepObjects.Values.Select(x => new BuildStep(x)).OrderBy(x => x.OrderIndex).ToList();
				if(Context.CustomBuildSteps != null && Context.CustomBuildSteps.Count > 0)
				{
					BuildSteps.RemoveAll(x => !Context.CustomBuildSteps.Contains(x.UniqueId));
				}
				else if(Context.Options.HasFlag(WorkspaceUpdateOptions.ScheduledBuild))
				{
					BuildSteps.RemoveAll(x => !x.bScheduledSync);
				}
				else
				{
					BuildSteps.RemoveAll(x => !x.bNormalSync);
				}

				// Check if the last successful build was before a change that we need to force a clean for
				bool bForceClean = false;
				if(LastBuiltChangeNumber != 0)
				{
					foreach(string CleanBuildChange in ProjectConfigFile.GetValues("ForceClean.Changelist", new string[0]))
					{
						int ChangeNumber;
						if(int.TryParse(CleanBuildChange, out ChangeNumber))
						{
							if((LastBuiltChangeNumber >= ChangeNumber) != (CurrentChangeNumber >= ChangeNumber))
							{
								Log.WriteLine("Forcing clean build due to changelist {0}.", ChangeNumber);
								Log.WriteLine();
								bForceClean = true;
								break;
							}
						}
					}
				}

				// Execute them all
				using(TelemetryStopwatch Stopwatch = new TelemetryStopwatch("Workspace_Build", TelemetryProjectPath))
				{
					Progress.Set("Starting build...", 0.0f);

					// Check we've built UBT (it should have been compiled by generating project files)
					string UnrealBuildToolPath = Path.Combine(LocalRootPath, "Engine", "Binaries", "DotNET", "UnrealBuildTool.exe");
					if(!File.Exists(UnrealBuildToolPath))
					{
						StatusMessage = String.Format("Couldn't find {0}", UnrealBuildToolPath);
						return WorkspaceUpdateResult.FailedToCompile;
					}

					// Execute all the steps
					float MaxProgressFraction = 0.0f;
					foreach (BuildStep Step in BuildSteps)
					{
						MaxProgressFraction += (float)Step.EstimatedDuration / (float)Math.Max(BuildSteps.Sum(x => x.EstimatedDuration), 1);

						Progress.Set(Step.StatusText);
						Progress.Push(MaxProgressFraction);

						Log.WriteLine(Step.StatusText);

						if(Step.IsValid())
						{
							switch(Step.Type)
							{
								case BuildStepType.Compile:
									using(TelemetryStopwatch StepStopwatch = new TelemetryStopwatch("Workspace_Execute_Compile", TelemetryProjectPath))
									{
										StepStopwatch.AddData(new { Target = Step.Target });

										string CommandLine = String.Format("{0} {1} {2} {3} -NoHotReloadFromIDE", Step.Target, Step.Platform, Step.Configuration, Utility.ExpandVariables(Step.Arguments ?? "", Context.Variables));
										if(Context.Options.HasFlag(WorkspaceUpdateOptions.Clean) || bForceClean)
										{
											Log.WriteLine("ubt> Running {0} {1} -clean", UnrealBuildToolPath, CommandLine);
											Utility.ExecuteProcess(UnrealBuildToolPath, null, CommandLine + " -clean", null, new ProgressTextWriter(Progress, new PrefixedTextWriter("ubt> ", Log)));
										}

										Log.WriteLine("ubt> Running {0} {1} -progress", UnrealBuildToolPath, CommandLine);

										int ResultFromBuild = Utility.ExecuteProcess(UnrealBuildToolPath, null, CommandLine + " -progress", null, new ProgressTextWriter(Progress, new PrefixedTextWriter("ubt> ", Log)));
										if(ResultFromBuild != 0)
										{
											StepStopwatch.Stop("Failed");
											StatusMessage = String.Format("Failed to compile {0}.", Step.Target);
											return (HasModifiedSourceFiles() || Context.UserBuildStepObjects.Count > 0)? WorkspaceUpdateResult.FailedToCompile : WorkspaceUpdateResult.FailedToCompileWithCleanWorkspace;
										}

										StepStopwatch.Stop("Success");
									}
									break;
								case BuildStepType.Cook:
									using(TelemetryStopwatch StepStopwatch = new TelemetryStopwatch("Workspace_Execute_Cook", TelemetryProjectPath))
									{
										StepStopwatch.AddData(new { Project = Path.GetFileNameWithoutExtension(Step.FileName) });

										string LocalRunUAT = Path.Combine(LocalRootPath, "Engine", "Build", "BatchFiles", "RunUAT.bat");
										string Arguments = String.Format("/C \"\"{0}\" -profile=\"{1}\"\"", LocalRunUAT, Path.Combine(LocalRootPath, Step.FileName));
										Log.WriteLine("uat> Running {0} {1}", LocalRunUAT, Arguments);

										int ResultFromUAT = Utility.ExecuteProcess(CmdExe, null, Arguments, null, new ProgressTextWriter(Progress, new PrefixedTextWriter("uat> ", Log)));
										if(ResultFromUAT != 0)
										{
											StepStopwatch.Stop("Failed");
											StatusMessage = String.Format("Cook failed. ({0})", ResultFromUAT);
											return WorkspaceUpdateResult.FailedToCompile;
										}

										StepStopwatch.Stop("Success");
									}
									break;
								case BuildStepType.Other:
									using(TelemetryStopwatch StepStopwatch = new TelemetryStopwatch("Workspace_Execute_Custom", TelemetryProjectPath))
									{
										StepStopwatch.AddData(new { FileName = Path.GetFileNameWithoutExtension(Step.FileName) });

										string ToolFileName = Path.Combine(LocalRootPath, Utility.ExpandVariables(Step.FileName, Context.Variables));
										string ToolWorkingDir = String.IsNullOrWhiteSpace(Step.WorkingDir) ? Path.GetDirectoryName(ToolFileName) : Utility.ExpandVariables(Step.WorkingDir, Context.Variables);
										string ToolArguments = Utility.ExpandVariables(Step.Arguments ?? "", Context.Variables);
										Log.WriteLine("tool> Running {0} {1}", ToolFileName, ToolArguments);

										if(Step.bUseLogWindow)
										{
											int ResultFromTool = Utility.ExecuteProcess(ToolFileName, ToolWorkingDir, ToolArguments, null, new ProgressTextWriter(Progress, new PrefixedTextWriter("tool> ", Log)));
											if(ResultFromTool != 0)
											{
												StepStopwatch.Stop("Failed");
												StatusMessage = String.Format("Tool terminated with exit code {0}.", ResultFromTool);
												return WorkspaceUpdateResult.FailedToCompile;
											}
										}
										else
										{
											ProcessStartInfo StartInfo = new ProcessStartInfo(ToolFileName, ToolArguments);
											StartInfo.WorkingDirectory = ToolWorkingDir;
											using(Process.Start(StartInfo))
											{
											}
										}

										StepStopwatch.Stop("Success");
									}
									break;
							}
						}

						Log.WriteLine();
						Progress.Pop();
					}

					Times.Add(new Tuple<string,TimeSpan>("Build", Stopwatch.Stop("Success")));
				}

				// Update the last successful build change number
				if(Context.CustomBuildSteps == null || Context.CustomBuildSteps.Count == 0)
				{
					LastBuiltChangeNumber = CurrentChangeNumber;
				}
			}

			// Write out all the timing information
			Log.WriteLine("Total time : " + FormatTime(Times.Sum(x => (long)(x.Item2.TotalMilliseconds / 1000))));
			foreach(Tuple<string, TimeSpan> Time in Times)
			{
				Log.WriteLine("   {0,-8}: {1}", Time.Item1, FormatTime((long)(Time.Item2.TotalMilliseconds / 1000)));
			}
			if(NumFilesSynced > 0)
			{
				Log.WriteLine("{0} files synced.", NumFilesSynced);
			}

			DateTime FinishTime = DateTime.Now;
			Log.WriteLine();
			Log.WriteLine("UPDATE SUCCEEDED ({0} {1})", FinishTime.ToShortDateString(), FinishTime.ToShortTimeString());

			StatusMessage = "Update succeeded";
			return WorkspaceUpdateResult.Success;
		}

		bool IsLicenseeVersion()
		{
			bool bIsEpicInternal;
			if (Perforce.FileExists(ClientRootPath + "/Engine/Build/NotForLicensees/EpicInternal.txt", out bIsEpicInternal, Log) && bIsEpicInternal)
			{
				return false;
			}
			if (Perforce.FileExists(ClientRootPath + "/Engine/Restricted/NotForLicensees/Build/EpicInternal.txt", out bIsEpicInternal, Log) && bIsEpicInternal)
			{
				return false;
			}
			return true;
		}

		public List<string> GetSyncPaths(bool bSyncAllProjects, string[] SyncFilter)
		{
			List<string> SyncPaths = GetRelativeSyncPaths(bSyncAllProjects, SyncFilter);
			return SyncPaths.Select(x => ClientRootPath + x).ToList();
		}

		public List<string> GetRelativeSyncPaths(bool bSyncAllProjects, string[] SyncFilter)
		{
			List<string> SyncPaths = new List<string>();

			// Check the client path is formatted correctly
			if (!SelectedClientFileName.StartsWith(ClientRootPath + "/"))
			{
				throw new Exception(String.Format("Expected '{0}' to start with '{1}'", SelectedClientFileName, ClientRootPath));
			}

			// Add the default project paths
			int LastSlashIdx = SelectedClientFileName.LastIndexOf('/');
			if (bSyncAllProjects || !SelectedClientFileName.EndsWith(".uproject", StringComparison.OrdinalIgnoreCase) || LastSlashIdx <= ClientRootPath.Length)
			{
				SyncPaths.Add("/...");
			}
			else
			{
				SyncPaths.Add("/*");
				SyncPaths.Add("/Engine/...");
				if(bIsEnterpriseProject)
				{
					SyncPaths.Add("/Enterprise/...");
				}
				SyncPaths.Add(SelectedClientFileName.Substring(ClientRootPath.Length, LastSlashIdx - ClientRootPath.Length) + "/...");
			}

			// Apply the sync filter to that list. We only want inclusive rules in the output list, but we can manually apply exclusions to previous entries.
			if(SyncFilter != null)
			{
				foreach(string SyncPath in SyncFilter)
				{
					string TrimSyncPath = SyncPath.Trim();
					if(TrimSyncPath.StartsWith("/"))
					{
						SyncPaths.Add(TrimSyncPath);
					}
					else if(TrimSyncPath.StartsWith("-/") && TrimSyncPath.EndsWith("..."))
					{
						SyncPaths.RemoveAll(x => x.StartsWith(TrimSyncPath.Substring(1, TrimSyncPath.Length - 4)));
					}
				}
			}

			// Sort the remaining paths by length, and remove any paths which are included twice
			SyncPaths = SyncPaths.OrderBy(x => x.Length).ToList();
			for(int Idx = 0; Idx < SyncPaths.Count; Idx++)
			{
				string SyncPath = SyncPaths[Idx];
				if(SyncPath.EndsWith("..."))
				{
					string SyncPathPrefix = SyncPath.Substring(0, SyncPath.Length - 3);
					for(int OtherIdx = SyncPaths.Count - 1; OtherIdx > Idx; OtherIdx--)
					{
						if(SyncPaths[OtherIdx].StartsWith(SyncPathPrefix))
						{
							SyncPaths.RemoveAt(OtherIdx);
						}
					}
				}
			}

			return SyncPaths;
		}

		public bool MatchFilter(string FileName, FileFilter Filter)
		{
			bool bMatch = true;
			if(FileName.StartsWith(LocalRootPath, StringComparison.OrdinalIgnoreCase))
			{
				if(!Filter.Matches(FileName.Substring(LocalRootPath.Length)))
				{
					bMatch = false;
				}
			}
			return bMatch;
		}

		class SyncState
		{
			public int TotalDepotPaths;
			public HashSet<string> RemainingDepotPaths;
			public Queue<List<string>> SyncCommandLists;
			public string StatusMessage;
			public WorkspaceUpdateResult Result = WorkspaceUpdateResult.Success;
		}

		WorkspaceUpdateResult SyncFileRevisions(string Prefix, WorkspaceUpdateContext Context, List<string> SyncCommands, HashSet<string> RemainingDepotPaths, out string StatusMessage)
		{
			Queue<List<string>> SyncCommandLists = new Queue<List<string>>();
			SyncCommandLists.Enqueue(SyncCommands);
			return SyncFileRevisions(Prefix, Context, SyncCommandLists, RemainingDepotPaths, out StatusMessage);
		}

		WorkspaceUpdateResult SyncFileRevisions(string Prefix, WorkspaceUpdateContext Context, Queue<List<string>> SyncCommandLists, HashSet<string> RemainingDepotPaths, out string StatusMessage)
		{
			// Figure out the number of additional background threads we want to run with. We can run worker on the current thread.
			int NumExtraThreads = Math.Max(Math.Min(SyncCommandLists.Count, Context.PerforceSyncOptions.NumThreads) - 1, 0);

			List<Thread> ChildThreads = new List<Thread>(NumExtraThreads);
			try
			{
				// Create the state object shared by all the worker threads
				SyncState State = new SyncState();
				State.TotalDepotPaths = RemainingDepotPaths.Count;
				State.RemainingDepotPaths = RemainingDepotPaths;
				State.SyncCommandLists = SyncCommandLists;

				// Wrapper writer around the log class to prevent multiple threads writing to it at once
				ThreadSafeTextWriter LogWrapper = new ThreadSafeTextWriter(Log);

				// Delegate for updating the sync state after a file has been synced
				Action<PerforceFileRecord, LineBasedTextWriter> SyncOutput = (Record, LocalLog) => { UpdateSyncState(Prefix, Record, State, Progress, LocalLog); };

				// Create all the child threads
				for (int ThreadIdx = 0; ThreadIdx < NumExtraThreads; ThreadIdx++)
				{
					int ThreadNumber = ThreadIdx + 2;
					Thread ChildThread = new Thread(() => StaticSyncWorker(ThreadNumber, Perforce, PendingChangeNumber, Context, State, SyncOutput, LogWrapper));
					ChildThreads.Add(ChildThread);
					ChildThread.Start();
				}

				// Run one worker on the current thread
				StaticSyncWorker(1, Perforce, PendingChangeNumber, Context, State, SyncOutput, LogWrapper);

				// Wait for all the background threads to finish
				foreach (Thread ChildThread in ChildThreads)
				{
					ChildThread.Join();
				}

				// Return the result that was set on the state object
				StatusMessage = State.StatusMessage;
				return State.Result;
			}
			finally
			{
				foreach (Thread ChildThread in ChildThreads)
				{
					ChildThread.Abort();
				}
				foreach (Thread ChildThread in ChildThreads)
				{
					ChildThread.Join();
				}
			}
		}

		static void UpdateSyncState(string Prefix, PerforceFileRecord Record, SyncState State, ProgressValue Progress, TextWriter Log)
		{
			lock (State)
			{
				State.RemainingDepotPaths.Remove(Record.DepotPath);

				string Message = String.Format("{0} ({1}/{2})", Prefix, State.TotalDepotPaths - State.RemainingDepotPaths.Count, State.TotalDepotPaths);
				float Fraction = Math.Min((float)(State.TotalDepotPaths - State.RemainingDepotPaths.Count) / (float)State.TotalDepotPaths, 1.0f);
				Progress.Set(Message, Fraction);

				Log.WriteLine("p4>   {0} {1}", Record.Action, Record.ClientPath);
			}
		}

		static void StaticSyncWorker(int ThreadNumber, PerforceConnection Perforce, int PendingChangeNumber, WorkspaceUpdateContext Context, SyncState State, Action<PerforceFileRecord, LineBasedTextWriter> SyncOutput, LineBasedTextWriter GlobalLog)
		{
			PrefixedTextWriter ThreadLog = new PrefixedTextWriter(String.Format("{0}:", ThreadNumber), GlobalLog);
			for (; ; )
			{
				// Remove the next batch that needs to be synced
				List<string> SyncCommands;
				lock (State)
				{
					if (State.Result == WorkspaceUpdateResult.Success && State.SyncCommandLists.Count > 0)
					{
						SyncCommands = State.SyncCommandLists.Dequeue();
					}
					else
					{
						break;
					}
				}

				// Sync the files
				string StatusMessage;
				WorkspaceUpdateResult Result = StaticSyncFileRevisions(Perforce, PendingChangeNumber, Context, SyncCommands, Record => SyncOutput(Record, ThreadLog), ThreadLog, out StatusMessage);

				// If it failed, try to set it on the state if nothing else has failed first
				if (Result != WorkspaceUpdateResult.Success)
				{
					lock (State)
					{
						if (State.Result == WorkspaceUpdateResult.Success)
						{
							State.Result = Result;
							State.StatusMessage = StatusMessage;
						}
					}
					break;
				}
			}
		}

		static WorkspaceUpdateResult StaticSyncFileRevisions(PerforceConnection Perforce, int PendingChangeNumber, WorkspaceUpdateContext Context, List<string> SyncCommands, Action<PerforceFileRecord> SyncOutput, TextWriter Log, out string StatusMessage)
		{
			// Sync them all
			List<string> TamperedFiles = new List<string>();
			if(!Perforce.Sync(SyncCommands, SyncOutput, TamperedFiles, false, Context.PerforceSyncOptions, Log))
			{
				StatusMessage = "Aborted sync due to errors.";
				return WorkspaceUpdateResult.FailedToSync;
			}

			// If any files need to be clobbered, defer to the main thread to figure out which ones
			if(TamperedFiles.Count > 0)
			{
				int NumNewFilesToClobber = 0;
				foreach(string TamperedFile in TamperedFiles)
				{
					if(!Context.ClobberFiles.ContainsKey(TamperedFile))
					{
						Context.ClobberFiles[TamperedFile] = true;
						if(TamperedFile.EndsWith(LocalObjectVersionFileName, StringComparison.OrdinalIgnoreCase) || TamperedFile.EndsWith(LocalVersionHeaderFileName, StringComparison.OrdinalIgnoreCase))
						{
							// Hack for UseFastModularVersioningV2; we don't need to update these files any more.
							continue;
						}
						NumNewFilesToClobber++;
					}
				}
				if(NumNewFilesToClobber > 0)
				{
					StatusMessage = String.Format("Cancelled sync after checking files to clobber ({0} new files).", NumNewFilesToClobber);
					return WorkspaceUpdateResult.FilesToClobber;
				}
				foreach(string TamperedFile in TamperedFiles)
				{
					if(Context.ClobberFiles[TamperedFile] && !Perforce.ForceSync(TamperedFile, PendingChangeNumber, Log))
					{
						StatusMessage = String.Format("Couldn't sync {0}.", TamperedFile);
						return WorkspaceUpdateResult.FailedToSync;
					}
				}
			}

			// All succeeded
			StatusMessage = null;
			return WorkspaceUpdateResult.Success;
		}

		static ConfigFile ReadProjectConfigFile(string LocalRootPath, string SelectedLocalFileName, TextWriter Log)
		{
			// Find the valid config file paths
			DirectoryInfo EngineDir = new DirectoryInfo(Path.Combine(LocalRootPath, "Engine"));
			List<FileInfo> LocalConfigFiles = Utility.GetLocalConfigPaths(EngineDir, new FileInfo(SelectedLocalFileName));

			// Read them in
			ConfigFile ProjectConfig = new ConfigFile();
			foreach(FileInfo LocalConfigFile in LocalConfigFiles)
			{
				try
				{
					string[] Lines = File.ReadAllLines(LocalConfigFile.FullName);
					ProjectConfig.Parse(Lines);
					Log.WriteLine("Read config file from {0}", LocalConfigFile.FullName);
				}
				catch(Exception Ex)
				{
					Log.WriteLine("Failed to read config file from {0}: {1}", LocalConfigFile.FullName, Ex.ToString());
				}
			}
			return ProjectConfig;
		}

		static IReadOnlyList<string> ReadProjectStreamFilter(PerforceConnection Perforce, ConfigFile ProjectConfigFile, TextWriter Log)
		{
			string StreamListDepotPath = ProjectConfigFile.GetValue("Options.QuickSelectStreamList", null);
			if(StreamListDepotPath == null)
			{
				return null;
			}

			List<string> Lines;
			if(!Perforce.Print(StreamListDepotPath, out Lines, Log))
			{
				return null;
			}

			return Lines.Select(x => x.Trim()).Where(x => x.Length > 0).ToList().AsReadOnly();
		}

		static string FormatTime(long Seconds)
		{
			if(Seconds >= 60)
			{
				return String.Format("{0,3}m {1:00}s", Seconds / 60, Seconds % 60);
			}
			else
			{
				return String.Format("     {0,2}s", Seconds);
			}
		}

		bool HasModifiedSourceFiles()
		{
			List<PerforceFileRecord> OpenFiles;
			if(!Perforce.GetOpenFiles(ClientRootPath + "/...", out OpenFiles, Log))
			{
				return true;
			}
			if(OpenFiles.Any(x => x.DepotPath.IndexOf("/Source/", StringComparison.OrdinalIgnoreCase) != -1))
			{
				return true;
			}
			return false;
		}

		bool FindUnresolvedFiles(IEnumerable<string> SyncPaths, out List<PerforceFileRecord> UnresolvedFiles)
		{
			UnresolvedFiles = new List<PerforceFileRecord>();
			foreach(string SyncPath in SyncPaths)
			{
				List<PerforceFileRecord> Records;
				if(!Perforce.GetUnresolvedFiles(SyncPath, out Records, Log))
				{
					Log.WriteLine("Couldn't find open files matching {0}", SyncPath);
					return false;
				}
				UnresolvedFiles.AddRange(Records);
			}
			return true;
		}

		bool UpdateVersionFile(string ClientPath, Dictionary<string, string> VersionStrings, int ChangeNumber)
		{
			return UpdateVersionFile(ClientPath, ChangeNumber, Text => UpdateVersionStrings(Text, VersionStrings));
		}

		bool UpdateVersionFile(string ClientPath, int ChangeNumber, Func<string, string> Update)
		{
			List<PerforceFileRecord> Records;
			if(!Perforce.Stat(ClientPath, out Records, Log))
			{
				Log.WriteLine("Failed to query records for {0}", ClientPath);
				return false;
			}
			if (Records.Count > 1)
			{
				// Attempt to remove any existing file which is synced
				Perforce.ForceSync(String.Format("{0}#0", ClientPath), Log);

				// Try to get the mapped files again
				if (!Perforce.Stat(ClientPath, out Records, Log))
				{
					Log.WriteLine("Failed to query records for {0}", ClientPath);
					return false;
				}
			}
			if (Records.Count == 0)
			{
				Log.WriteLine("Ignoring {0}; not found on server.", ClientPath);
				return true;
			}

			string LocalPath = Records[0].ClientPath; // Actually a filesystem path
			string DepotPath = Records[0].DepotPath;

			List<string> Lines;
			if(!Perforce.Print(String.Format("{0}@{1}", DepotPath, ChangeNumber), out Lines, Log))
			{
				Log.WriteLine("Couldn't get default contents of {0}", DepotPath);
				return false;
			}

			string Text = String.Join("\n", Lines);
			Text = Update(Text);
			return WriteVersionFile(LocalPath, DepotPath, Text);
		}

		string UpdateVersionStrings(string Text, Dictionary<string, string> VersionStrings)
		{
			StringWriter Writer = new StringWriter();
			foreach (string Line in Text.Split('\n'))
			{
				string NewLine = Line;
				foreach (KeyValuePair<string, string> VersionString in VersionStrings)
				{
					if (UpdateVersionLine(ref NewLine, VersionString.Key, VersionString.Value))
					{
						break;
					}
				}
				Writer.WriteLine(NewLine);
			}
			return Writer.ToString();
		}

		string UpdateBuildVersion(string Text, int Changelist, int CodeChangelist, string BranchOrStreamName, bool bIsLicenseeVersion)
		{
			Dictionary<string, object> Object = Json.Deserialize(Text);

			object PrevCompatibleChangelistObj;
			int PrevCompatibleChangelist = Object.TryGetValue("CompatibleChangelist", out PrevCompatibleChangelistObj) ? (int)Convert.ChangeType(PrevCompatibleChangelistObj, typeof(int)) : 0;

			object PrevIsLicenseeVersionObj;
			bool PrevIsLicenseeVersion = Object.TryGetValue("IsLicenseeVersion", out PrevIsLicenseeVersionObj)? ((int)Convert.ChangeType(PrevIsLicenseeVersionObj, typeof(int)) != 0) : false;

			Object["Changelist"] = Changelist;
			if(PrevCompatibleChangelist == 0 || PrevIsLicenseeVersion != bIsLicenseeVersion)
			{
				// Don't overwrite the compatible changelist if we're in a hotfix release
				Object["CompatibleChangelist"] = CodeChangelist;
			}
			Object["BranchName"] = BranchOrStreamName.Replace('/', '+');
			Object["IsPromotedBuild"] = 0;
			Object["IsLicenseeVersion"] = bIsLicenseeVersion ? 1 : 0;

			return Json.Serialize(Object, JsonSerializeOptions.PrettyPrint);
		}

		bool WriteVersionFile(string LocalPath, string DepotPath, string NewText)
		{
			try
			{
				if(File.Exists(LocalPath) && File.ReadAllText(LocalPath) == NewText)
				{
					Log.WriteLine("Ignored {0}; contents haven't changed", LocalPath);
				}
				else
				{
					Directory.CreateDirectory(Path.GetDirectoryName(LocalPath));
					Utility.ForceDeleteFile(LocalPath);
					if(DepotPath != null)
					{
						Perforce.Sync(DepotPath + "#0", Log);
					}
					File.WriteAllText(LocalPath, NewText);
					Log.WriteLine("Written {0}", LocalPath);
				}
				return true;
			}
			catch(Exception Ex)
			{
				Log.WriteException(Ex, "Failed to write to {0}.", LocalPath);
				return false;
			}
		}

		bool UpdateVersionLine(ref string Line, string Prefix, string Suffix)
		{
			int LineIdx = 0;
			int PrefixIdx = 0;
			for(;;)
			{
				string PrefixToken = ReadToken(Prefix, ref PrefixIdx);
				if(PrefixToken == null)
				{
					break;
				}

				string LineToken = ReadToken(Line, ref LineIdx);
				if(LineToken == null || LineToken != PrefixToken)
				{
					return false;
				}
			}
			Line = Line.Substring(0, LineIdx) + Suffix;
			return true;
		}

		string ReadToken(string Line, ref int LineIdx)
		{
			for(;; LineIdx++)
			{
				if(LineIdx == Line.Length)
				{
					return null;
				}
				else if(!Char.IsWhiteSpace(Line[LineIdx]))
				{
					break;
				}
			}

			int StartIdx = LineIdx++;
			if(Char.IsLetterOrDigit(Line[StartIdx]) || Line[StartIdx] == '_')
			{
				while(LineIdx < Line.Length && (Char.IsLetterOrDigit(Line[LineIdx]) || Line[LineIdx] == '_'))
				{
					LineIdx++;
				}
			}

			return Line.Substring(StartIdx, LineIdx - StartIdx);
		}

		public bool IsBusy()
		{
			return bSyncing;
		}

		public Tuple<string, float> CurrentProgress
		{
			get { return Progress.Current; }
		}

		public int CurrentChangeNumber
		{
			get;
			private set;
		}

		public int PendingChangeNumber
		{
			get;
			private set;
		}

		public int LastBuiltChangeNumber
		{
			get;
			private set;
		}

		public string CurrentSyncFilterHash
		{
			get;
			private set;
		}

		public string ClientName
		{
			get { return Perforce.ClientName; }
		}
	}
}
