// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.IO;
using System.Linq;
using System.Diagnostics;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	class Unity
	{
		/// <summary>
		/// Prefix used for all dynamically created Unity modules
		/// </summary>
		public const string ModulePrefix = "Module.";

		/// <summary>
		/// A class which represents a list of files and the sum of their lengths.
		/// </summary>
		public class FileCollection
		{
			public List<FileItem> Files { get; private set; }
			public List<FileItem> VirtualFiles { get; private set; }
			public long TotalLength { get; private set; }

			/// The length of this file collection, plus any additional virtual space needed for bUseAdapativeUnityBuild.
			/// See the comment above AddVirtualFile() below for more information.
			public long VirtualLength { get; private set; }

			public FileCollection()
			{
				Files = new List<FileItem>();
				VirtualFiles = new List<FileItem>();
				TotalLength = 0;
				VirtualLength = 0;
			}

			public void AddFile(FileItem File)
			{
				Files.Add(File);

				long FileLength = File.Length;
				TotalLength += FileLength;
				VirtualLength += FileLength;
			}

			/// <summary>
			/// Doesn't actually add a file, but instead reserves space.  This is used with "bUseAdaptiveUnityBuild", to prevent
			/// other compiled unity blobs in the module's numbered set from having to be recompiled after we eject source files
			/// one of that module's unity blobs.  Basically, it can prevent dozens of files from being recompiled after the first
			/// time building after your working set of source files changes
			/// </summary>
			/// <param name="File">The virtual file to add to the collection</param>
			public void AddVirtualFile(FileItem File)
			{
				VirtualFiles.Add(File);
				VirtualLength += File.Length;
			}
		}

		/// <summary>
		/// A class for building up a set of unity files.  You add files one-by-one using AddFile then call EndCurrentUnityFile to finish that one and
		/// (perhaps) begin a new one.
		/// </summary>
		public class UnityFileBuilder
		{
			private List<FileCollection> UnityFiles;
			private FileCollection CurrentCollection;
			private int SplitLength;

			/// <summary>
			/// Constructs a new UnityFileBuilder.
			/// </summary>
			/// <param name="InSplitLength">The accumulated length at which to automatically split a unity file, or -1 to disable automatic splitting.</param>
			public UnityFileBuilder(int InSplitLength)
			{
				UnityFiles = new List<FileCollection>();
				CurrentCollection = new FileCollection();
				SplitLength = InSplitLength;
			}

			/// <summary>
			/// Adds a file to the current unity file.  If splitting is required and the total size of the
			/// unity file exceeds the split limit, then a new file is automatically started.
			/// </summary>
			/// <param name="File">The file to add.</param>
			public void AddFile(FileItem File)
			{
				CurrentCollection.AddFile(File);
				if (SplitLength != -1 && CurrentCollection.VirtualLength > SplitLength)
				{
					EndCurrentUnityFile();
				}
			}

			/// <summary>
			/// Doesn't actually add a file, but instead reserves space, then splits the unity blob normally as if it
			/// was a real file that was added.  See the comment above FileCollection.AddVirtualFile() for more info.
			/// </summary>
			/// <param name="File">The file to add virtually.  Only the size of the file is tracked.</param>
			public void AddVirtualFile(FileItem File)
			{
				CurrentCollection.AddVirtualFile(File);
				if (SplitLength != -1 && CurrentCollection.VirtualLength > SplitLength)
				{
					EndCurrentUnityFile();
				}
			}


			/// <summary>
			/// Starts a new unity file.  If the current unity file contains no files, this function has no effect, i.e. you will not get an empty unity file.
			/// </summary>
			public void EndCurrentUnityFile()
			{
				if (CurrentCollection.Files.Count == 0)
					return;

				UnityFiles.Add(CurrentCollection);
				CurrentCollection = new FileCollection();
			}

			/// <summary>
			/// Returns the list of built unity files.  The UnityFileBuilder is unusable after this.
			/// </summary>
			/// <returns></returns>
			public List<FileCollection> GetUnityFiles()
			{
				EndCurrentUnityFile();

				List<FileCollection> Result = UnityFiles;

				// Null everything to ensure that failure will occur if you accidentally reuse this object.
				CurrentCollection = null;
				UnityFiles = null;

				return Result;
			}
		}

		/// <summary>
		/// Given a set of C++ files, generates another set of C++ files that #include all the original
		/// files, the goal being to compile the same code in fewer translation units.
		/// The "unity" files are written to the CompileEnvironment's OutputDirectory.
		/// </summary>
		/// <param name="Target">The target we're building</param>
		/// <param name="CPPFiles">The C++ files to #include.</param>
		/// <param name="CompileEnvironment">The environment that is used to compile the C++ files.</param>
		/// <param name="WorkingSet">Interface to query files which belong to the working set</param>
		/// <param name="BaseName">Base name to use for the Unity files</param>
		/// <param name="IntermediateDirectory">Intermediate directory for unity cpp files</param>
		/// <param name="Graph">The makefile being built</param>
		/// <param name="SourceFileToUnityFile">Receives a mapping of source file to unity file</param>
		/// <returns>The "unity" C++ files.</returns>
		public static List<FileItem> GenerateUnityCPPs(
			ReadOnlyTargetRules Target,
			List<FileItem> CPPFiles,
			CppCompileEnvironment CompileEnvironment,
			ISourceFileWorkingSet WorkingSet,
			string BaseName,
			DirectoryReference IntermediateDirectory,
			IActionGraphBuilder Graph,
			Dictionary<FileItem, FileItem> SourceFileToUnityFile
			)
		{
			List<FileItem> NewCPPFiles = new List<FileItem>();

			//UEBuildPlatform BuildPlatform = UEBuildPlatform.GetBuildPlatform(CompileEnvironment.Platform);

			// Figure out size of all input files combined. We use this to determine whether to use larger unity threshold or not.
			long TotalBytesInCPPFiles = CPPFiles.Sum(F => F.Length);

			// We have an increased threshold for unity file size if, and only if, all files fit into the same unity file. This
			// is beneficial when dealing with PCH files. The default PCH creation limit is X unity files so if we generate < X 
			// this could be fairly slow and we'd rather bump the limit a bit to group them all into the same unity file.


			// When enabled, UnrealBuildTool will try to determine source files that you are actively iteratively changing, and break those files
			// out of their unity blobs so that you can compile them as individual translation units, much faster than recompiling the entire
			// unity blob each time.
			bool bUseAdaptiveUnityBuild = Target.bUseAdaptiveUnityBuild && !Target.bStressTestUnity;

			// Optimization only makes sense if PCH files are enabled.
			bool bForceIntoSingleUnityFile = Target.bStressTestUnity || (TotalBytesInCPPFiles < Target.NumIncludedBytesPerUnityCPP * 2 && Target.bUsePCHFiles);

			// Build the list of unity files.
			List<FileCollection> AllUnityFiles;
			{
				// Sort the incoming file paths alphabetically, so there will be consistency in unity blobs across multiple machines.
				// Note that we're relying on this not only sorting files within each directory, but also the directories
				// themselves, so the whole list of file paths is the same across computers.
				List<FileItem> SortedCPPFiles = CPPFiles.GetRange(0, CPPFiles.Count);
				{
					// Case-insensitive file path compare, because you never know what is going on with local file systems
					Comparison<FileItem> FileItemComparer = (FileA, FileB) => { return FileA.AbsolutePath.ToLowerInvariant().CompareTo(FileB.AbsolutePath.ToLowerInvariant()); };
					SortedCPPFiles.Sort(FileItemComparer);
				}


				// Figure out whether we REALLY want to use adaptive unity for this module.  If nearly every file in the module appears in the working
				// set, we'll just go ahead and let unity build do its thing.
				HashSet<FileItem> FilesInWorkingSet = new HashSet<FileItem>();
				if (bUseAdaptiveUnityBuild)
				{
					int CandidateWorkingSetSourceFileCount = 0;
					int WorkingSetSourceFileCount = 0;
					foreach (FileItem CPPFile in SortedCPPFiles)
					{
						++CandidateWorkingSetSourceFileCount;

						// Don't include writable source files into unity blobs
						if (WorkingSet.Contains(CPPFile))
						{
							++WorkingSetSourceFileCount;

							// Mark this file as part of the working set.  This will be saved into the UBT Makefile so that
							// the assembler can automatically invalidate the Makefile when the working set changes (allowing this
							// code to run again, to build up new unity blobs.)
							FilesInWorkingSet.Add(CPPFile);
							Graph.AddFileToWorkingSet(CPPFile);
						}
					}

					if (WorkingSetSourceFileCount >= CandidateWorkingSetSourceFileCount)
					{
						// Every single file in the module appears in the working set, so don't bother using adaptive unity for this
						// module.  Otherwise it would make full builds really slow.
						bUseAdaptiveUnityBuild = false;
					}
				}

				UnityFileBuilder CPPUnityFileBuilder = new UnityFileBuilder(bForceIntoSingleUnityFile ? -1 : Target.NumIncludedBytesPerUnityCPP);
				StringBuilder AdaptiveUnityBuildInfoString = new StringBuilder();
				foreach (FileItem CPPFile in SortedCPPFiles)
				{
					if (!bForceIntoSingleUnityFile && CPPFile.AbsolutePath.IndexOf(".GeneratedWrapper.", StringComparison.InvariantCultureIgnoreCase) != -1)
					{
						NewCPPFiles.Add(CPPFile);
					}

					// When adaptive unity is enabled, go ahead and exclude any source files that we're actively working with
					if (bUseAdaptiveUnityBuild && FilesInWorkingSet.Contains(CPPFile))
					{
						// Just compile this file normally, not as part of the unity blob
						NewCPPFiles.Add(CPPFile);

						// Let the unity file builder know about the file, so that we can retain the existing size of the unity blobs.
						// This won't actually make the source file part of the unity blob, but it will keep track of how big the
						// file is so that other existing unity blobs from the same module won't be invalidated.  This prevents much
						// longer compile times the first time you build after your working file set changes.
						CPPUnityFileBuilder.AddVirtualFile(CPPFile);

						string CPPFileName = Path.GetFileName(CPPFile.AbsolutePath);
						if (AdaptiveUnityBuildInfoString.Length == 0)
						{
							AdaptiveUnityBuildInfoString.Append(String.Format("[Adaptive unity build] Excluded from {0} unity file: {1}", BaseName, CPPFileName));
						}
						else
						{
							AdaptiveUnityBuildInfoString.Append(", " + CPPFileName);
						}
					}
					else
					{
						// If adaptive unity build is enabled for this module, add this source file to the set that will invalidate the makefile
						if(bUseAdaptiveUnityBuild)
						{
							Graph.AddCandidateForWorkingSet(CPPFile);
						}

						// Compile this file as part of the unity blob
						CPPUnityFileBuilder.AddFile(CPPFile);
					}
				}

				if (AdaptiveUnityBuildInfoString.Length > 0)
				{
					if (Target.bAdaptiveUnityCreatesDedicatedPCH)
					{
						Graph.AddDiagnostic("[Adaptive unity build] Creating dedicated PCH for each excluded file. Set bAdaptiveUnityCreatesDedicatedPCH to false in BuildConfiguration.xml to change this behavior.");
					}
					else if (Target.bAdaptiveUnityDisablesPCH)
					{
						Graph.AddDiagnostic("[Adaptive unity build] Disabling PCH for excluded files. Set bAdaptiveUnityDisablesPCH to false in BuildConfiguration.xml to change this behavior.");
					}

					if (Target.bAdaptiveUnityDisablesOptimizations)
					{
						Graph.AddDiagnostic("[Adaptive unity build] Disabling optimizations for excluded files. Set bAdaptiveUnityDisablesOptimizations to false in BuildConfiguration.xml to change this behavior.");
					}
					if (Target.bAdaptiveUnityEnablesEditAndContinue)
					{
						Graph.AddDiagnostic("[Adaptive unity build] Enabling Edit & Continue for excluded files. Set bAdaptiveUnityEnablesEditAndContinue to false in BuildConfiguration.xml to change this behavior.");
					}

					Graph.AddDiagnostic(AdaptiveUnityBuildInfoString.ToString());
				}

				AllUnityFiles = CPPUnityFileBuilder.GetUnityFiles();
			}

			// Create a set of CPP files that combine smaller CPP files into larger compilation units, along with the corresponding 
			// actions to compile them.
			int CurrentUnityFileCount = 0;
			foreach (FileCollection UnityFile in AllUnityFiles)
			{
				++CurrentUnityFileCount;

				StringWriter OutputUnityCPPWriter = new StringWriter();

				OutputUnityCPPWriter.WriteLine("// This file is automatically generated at compile-time to include some subset of the user-created cpp files.");

				// Add source files to the unity file
				foreach (FileItem CPPFile in UnityFile.Files)
				{
					OutputUnityCPPWriter.WriteLine("#include \"{0}\"", CPPFile.AbsolutePath.Replace('\\', '/'));
				}

				// Determine unity file path name
				string UnityCPPFileName;
				if (AllUnityFiles.Count > 1)
				{
					UnityCPPFileName = string.Format("{0}{1}.{2}_of_{3}.cpp", ModulePrefix, BaseName, CurrentUnityFileCount, AllUnityFiles.Count);
				}
				else
				{
					UnityCPPFileName = string.Format("{0}{1}.cpp", ModulePrefix, BaseName);
				}
				FileReference UnityCPPFilePath = FileReference.Combine(IntermediateDirectory, UnityCPPFileName);

				// Write the unity file to the intermediate folder.
				FileItem UnityCPPFile = Graph.CreateIntermediateTextFile(UnityCPPFilePath, OutputUnityCPPWriter.ToString());
				NewCPPFiles.Add(UnityCPPFile);

				// Store the mapping of source files to unity files in the makefile
				foreach(FileItem SourceFile in UnityFile.Files)
				{
					SourceFileToUnityFile[SourceFile] = UnityCPPFile;
				}
				foreach (FileItem SourceFile in UnityFile.VirtualFiles)
				{
					SourceFileToUnityFile[SourceFile] = UnityCPPFile;
				}
			}

			return NewCPPFiles;
		}

		static void AddUniqueDiagnostic(TargetMakefile Makefile, string Message)
		{
			if(!Makefile.Diagnostics.Contains(Message, StringComparer.Ordinal))
			{
				Makefile.Diagnostics.Add(Message);
			}
		}
	}
}
