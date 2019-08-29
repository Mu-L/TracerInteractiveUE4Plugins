// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Xml;
using Tools.DotNETCommon;
using UnrealBuildTool;

namespace AutomationTool.Tasks
{
	/// <summary>
	/// Parameters for a task which runs a UE4 commandlet
	/// </summary>
	public class CommandletTaskParameters
	{
		/// <summary>
		/// The commandlet name to execute
		/// </summary>
		[TaskParameter]
		public string Name;

		/// <summary>
		/// The project to run the editor with
		/// </summary>
		[TaskParameter(Optional = true, ValidationType = TaskParameterValidationType.FileSpec)]
		public string Project;

		/// <summary>
		/// Arguments to be passed to the commandlet
		/// </summary>
		[TaskParameter(Optional = true)]
		public string Arguments;

		/// <summary>
		/// The editor executable to use. Defaults to the development UE4Editor executable for the current platform.
		/// </summary>
		[TaskParameter(Optional = true)]
		public FileReference EditorExe;
	}

	/// <summary>
	/// Spawns the editor to run a commandlet.
	/// </summary>
	[TaskElement("Commandlet", typeof(CommandletTaskParameters))]
	public class CommandletTask : CustomTask
	{
		/// <summary>
		/// Parameters for this task
		/// </summary>
		CommandletTaskParameters Parameters;

		/// <summary>
		/// Construct a new CommandletTask.
		/// </summary>
		/// <param name="InParameters">Parameters for this task</param>
		public CommandletTask(CommandletTaskParameters InParameters)
		{
			Parameters = InParameters;
		}

		/// <summary>
		/// Execute the task.
		/// </summary>
		/// <param name="Job">Information about the current job</param>
		/// <param name="BuildProducts">Set of build products produced by this node.</param>
		/// <param name="TagNameToFileSet">Mapping from tag names to the set of files they include</param>
		public override void Execute(JobContext Job, HashSet<FileReference> BuildProducts, Dictionary<string, HashSet<FileReference>> TagNameToFileSet)
		{
			// Get the full path to the project file
			FileReference ProjectFile = null;
			if(!String.IsNullOrEmpty(Parameters.Project))
			{
				if(Parameters.Project.EndsWith(".uproject", StringComparison.OrdinalIgnoreCase))
				{
					ProjectFile = ResolveFile(Parameters.Project);
				}
				else
				{
					ProjectFile = NativeProjects.EnumerateProjectFiles().FirstOrDefault(x => x.GetFileNameWithoutExtension().Equals(Parameters.Project, StringComparison.OrdinalIgnoreCase));
				}

				if(ProjectFile == null || !FileReference.Exists(ProjectFile))
				{
					throw new BuildException("Unable to resolve project '{0}'", Parameters.Project);
				}
			}

			// Get the path to the editor, and check it exists
			FileReference EditorExe;
			if(Parameters.EditorExe == null)
			{
                EditorExe = new FileReference(HostPlatform.Current.GetUE4ExePath("UE4Editor-Cmd.exe"));
			}
			else
			{
				EditorExe = Parameters.EditorExe;
			}

			// Make sure the editor exists
			if(!FileReference.Exists(EditorExe))
			{
				throw new AutomationException("{0} does not exist", EditorExe.FullName);
			}

			// Run the commandlet
			CommandUtils.RunCommandlet(ProjectFile, EditorExe.FullName, Parameters.Name, Parameters.Arguments);
		}

		/// <summary>
		/// Output this task out to an XML writer.
		/// </summary>
		public override void Write(XmlWriter Writer)
		{
			Write(Writer, Parameters);
		}

		/// <summary>
		/// Find all the tags which are used as inputs to this task
		/// </summary>
		/// <returns>The tag names which are read by this task</returns>
		public override IEnumerable<string> FindConsumedTagNames()
		{
			yield break;
		}

		/// <summary>
		/// Find all the tags which are modified by this task
		/// </summary>
		/// <returns>The tag names which are modified by this task</returns>
		public override IEnumerable<string> FindProducedTagNames()
		{
			yield break;
		}
	}
}
