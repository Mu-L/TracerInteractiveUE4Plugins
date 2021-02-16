// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using AutomationTool;
using System.Threading;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Reflection;
using ImageMagick;
using UnrealBuildTool;
using Tools.DotNETCommon;
using System.Security.Cryptography;
using System.Text;

namespace Gauntlet
{
	public static class Globals
	{
		static Params InnerParams = new Params(Environment.GetCommandLineArgs());

		public static Params Params
		{
			get { return InnerParams; }
			set { InnerParams = value; }
		}

		static string InnerTempDir;
		static string InnerLogDir;
		static string InnerUE4RootDir;
		static object InnerLockObject = new object();
		static List<Action> InnerAbortHandlers;
		static List<Action> InnerPostAbortHandlers = new List<Action>();
		public static bool CancelSignalled { get; private set; }

		/// <summary>
		/// Get the worker id of this Gauntlet instance
		/// returns -1 if instance is not a member of a worker group
		/// </summary>
		public static int WorkerID
		{
			get
			{
				int Default = -1;
				return Params.ParseValue("workerid", Default);
			}

		}

		/// <summary>
		/// Get the worker pool id of the host worker, pools are assigned to teams such as QA, Automation, etc
		/// returns -1 if instance is not running on a worker pool
		/// </summary>
		public static int WorkerPoolID
		{
			get
			{
				int Default = -1;
				return Params.ParseValue("workerpoolid", Default);
			}

		}


		/// <summary>
		/// Returns true if Gauntlet instance is a member of a worker group
		/// </summary>
		public static bool IsWorker
		{
			get
			{
				return WorkerID != -1;
			}
		}


		public static string TempDir
		{
			get
			{
				if (String.IsNullOrEmpty(InnerTempDir))
				{
					InnerTempDir = Path.Combine(Environment.CurrentDirectory, "GauntletTemp");
				}

				return InnerTempDir;
			}
			set
			{
				InnerTempDir = value;
			}
		}

		public static string LogDir
		{
			get
			{
				if (String.IsNullOrEmpty(InnerLogDir))
				{
					InnerLogDir = Path.Combine(TempDir, "Logs");
				}

				return InnerLogDir;
			}
			set
			{
				InnerLogDir = value;
			}
		}

		public static string UE4RootDir
		{
			get
			{
				if (String.IsNullOrEmpty(InnerUE4RootDir))
				{
					InnerUE4RootDir = Path.GetFullPath(Path.Combine(Path.GetDirectoryName(Assembly.GetEntryAssembly().GetOriginalLocation()), "..", "..", ".."));
				}

				return InnerUE4RootDir;
			}

		}

		/// <summary>
		/// Return @"\\?\"; adding the prefix to a path string tells the Windows APIs to disable all string parsing and to send the string that follows it straight to the file system.
		/// Allowing for longer than 260 characters path.
		/// </summary>
		public static string LongPathPrefix
		{
			get { return Path.DirectorySeparatorChar == '\\' ? @"\\?\" : ""; }
		}

		/// <summary>
		/// Acquired and released during the main Tick of the Gauntlet systems. Use this before touchung anything global scope from a 
		/// test thread.
		/// </summary>
		public static object MainLock
		{
			get { return InnerLockObject; }
		}

		/// <summary>
		/// Allows classes to register for notification of system-wide abort request. On an abort (e.g. ctrl+c) all handlers will 
		/// be called and then shutdown will continue
		/// </summary>
		public static List<Action> AbortHandlers
		{
			get
			{
				if (InnerAbortHandlers == null)
				{
					InnerAbortHandlers = new List<Action>();

					Console.CancelKeyPress += new ConsoleCancelEventHandler((obj, args) =>
					{
						CancelSignalled = true;

						// fire all abort handlers
						foreach (var Handler in AbortHandlers)
						{
							Handler();
						}

						// file all post-abort handlers
						foreach (var Handler in PostAbortHandlers)
						{
							Handler();
						}
					});
				}

				return InnerAbortHandlers;
			}
		}

		/// <summary>
		/// Allows classes to register for post-abort handlers. These are called after all abort handlers have returned
		/// so is the place to perform final cleanup.
		/// </summary>
		public static List<Action> PostAbortHandlers { get { return InnerPostAbortHandlers; } }


	}

	/// <summary>
	/// Enable/disable verbose logging
	/// </summary>
	public enum LogLevel
	{
		Normal,
		Verbose,
		VeryVerbose
	};

	/// <summary>
	/// Gauntlet Logging helper
	/// </summary>
	public class Log
	{
		public static LogLevel Level = LogLevel.Normal;

		public static bool IsVerbose
		{
			get
			{
				return Level >= LogLevel.Verbose;
			}
		}

		public static bool IsVeryVerbose
		{
			get
			{
				return Level >= LogLevel.VeryVerbose;
			}
		}

		static StreamWriter LogFile = null;

		static List<Action<string>> Callbacks;

		static int ECSuspendCount = 0;

		static int SanitizationSuspendCount = 0;


		public static void AddCallback(Action<string> CB)
		{
			if (Callbacks == null)
			{
				Callbacks = new List<Action<string>>();
			}

			Callbacks.Add(CB);
		}

		public static void SaveToFile(string InPath)
		{
			int Attempts = 0;

			if (LogFile != null)
			{
				Console.WriteLine("Logfile already open for writing");
				return;
			}

			do
			{
				string Outpath = InPath;

				if (Attempts > 0)
				{
					string Ext = Path.GetExtension(InPath);
					string BaseName = Path.GetFileNameWithoutExtension(InPath);

					Outpath = Path.Combine(Path.GetDirectoryName(InPath), string.Format("{0}_{1}.{2}", BaseName, Attempts, Ext));
				}

				try
				{
					LogFile = new StreamWriter(Outpath);
				}
				catch (UnauthorizedAccessException Ex)
				{
					Console.WriteLine("Could not open {0} for writing. {1}", Outpath, Ex);
					Attempts++;
				}

			} while (LogFile == null && Attempts < 10);
		}

		static void Flush()
		{
			if (LogFile != null)
			{
				LogFile.Flush();
			}
		}

		public static void SuspendECErrorParsing()
		{
			if (ECSuspendCount++ == 0)
			{
				if (CommandUtils.IsBuildMachine)
				{
					OutputMessage("<-- Suspend Log Parsing -->");
				}
			}
		}

		public static void ResumeECErrorParsing()
		{
			if (--ECSuspendCount == 0)
			{
				if (CommandUtils.IsBuildMachine)
				{
					OutputMessage("<-- Resume Log Parsing -->");
				}
			}
		}

		public static void SuspendSanitization()
		{
			SanitizationSuspendCount++;
		}

		public static void ResumeSanitization()
		{
			--SanitizationSuspendCount;
		}

		/// <summary>
		/// Santi
		/// </summary>
		/// <param name="Input"></param>
		/// <param name="Sanitize"></param>
		/// <returns></returns>
		static private string SanitizeInput(string Input, string[] Sanitize)
		{
			foreach (string San in Sanitize)
			{
				string CleanSan = San;
				if (San.Length > 1)
				{
					CleanSan.Insert(San.Length - 1, "-");
				}
				Input = Regex.Replace(Input, "Warning", CleanSan, RegexOptions.IgnoreCase);
			}

			return Input;
		}

		/// <summary>
		/// Outputs the message to the console with an optional prefix and sanitization. Sanitizing
		/// allows errors and exceptions to be passed through to logs without triggering CIS warnings
		/// about out log
		/// </summary>
		/// <param name="Message"></param>
		/// <param name="Prefix"></param>
		/// <param name="Sanitize"></param>
		/// <returns></returns>
		static private void OutputMessage(string Message, string Prefix="", bool Sanitize=true)
		{
			// EC detects error statements in the log as a failure. Need to investigate best way of 
			// reporting errors, but not errors we've handled from tools.
			// Probably have Log.Error which does not sanitize?
			if (Sanitize && SanitizationSuspendCount == 0)
			{
				string[] Triggers = { "Warning:", "Error:", "Exception:" };

				foreach (string Trigger in Triggers)
				{
					if (Message.IndexOf(Trigger, StringComparison.OrdinalIgnoreCase) != -1)
					{
						string SafeTrigger = Regex.Replace(Trigger, "i", "1", RegexOptions.IgnoreCase);
						SafeTrigger = Regex.Replace(SafeTrigger, "o", "0", RegexOptions.IgnoreCase);

						Message = Regex.Replace(Message, Trigger, SafeTrigger, RegexOptions.IgnoreCase);
					}
				}		
			}

			if (string.IsNullOrEmpty(Prefix) == false)
			{
				Message = Prefix + ": " + Message;
			}

			// TODO - Remove all Gauntlet logging and switch to UBT log?
			CommandUtils.LogInformation(Message);

			if (LogFile != null)
			{
				LogFile.WriteLine(Message);
			}

			if (Callbacks != null)
			{
				Callbacks.ForEach(A => A(Message));
			}
		}	

		static public void Verbose(string Format, params object[] Args)
		{
			if (IsVerbose)
			{
				Verbose(string.Format(Format, Args));
			}
		}

		static public void Verbose(string Message)
		{
			if (IsVerbose)
			{
				OutputMessage(Message);
			}
		}

		static public void VeryVerbose(string Format, params object[] Args)
		{
			if (IsVeryVerbose)
			{
				VeryVerbose(string.Format(Format, Args));
			}
		}

		static public void VeryVerbose(string Message)
		{
			if (IsVeryVerbose)
			{
				OutputMessage(Message);
			}
		}

		static public void Info(string Format, params object[] Args)
		{
			Info(string.Format(Format, Args));
		}

		static public void Info(string Message)
		{
			OutputMessage(Message);
		}

		static public void Warning(string Format, params object[] Args)
		{
			Warning(string.Format(Format, Args));
		}

		static public void Warning(string Message)
		{
			OutputMessage(Message, "Warning");
		}
		static public void Error(string Format, params object[] Args)
		{
			Error(string.Format(Format, Args));
		}
		static public void Error(string Message)
		{		
			OutputMessage(Message, "Error", false);
		}
	}

	public class Hasher
	{
		public static string ComputeHash(string Input)
		{
			if (string.IsNullOrEmpty(Input))
			{
				return "0";
			}

			HashAlgorithm Hasher = MD5.Create();  //or use SHA256.Create();

			byte[] Hash = Hasher.ComputeHash(Encoding.UTF8.GetBytes(Input));

			string HashString = "";

			foreach (byte b in Hash)
			{
				HashString += (b.ToString("X2"));
			}

			return HashString;
		}
	}

	/*
	 * Helper class that can be used with a using() statement to emit log entries that prevent EC triggering on Error/Warning statements
	*/
	public class ScopedSuspendECErrorParsing : IDisposable
	{
		public ScopedSuspendECErrorParsing()
		{
			Log.SuspendECErrorParsing();
		}

		~ScopedSuspendECErrorParsing()
		{
			Dispose(false);
		}

		#region IDisposable Support
		private bool disposedValue = false; // To detect redundant calls

		protected virtual void Dispose(bool disposing)
		{
			if (!disposedValue)
			{
				Log.ResumeECErrorParsing();

				disposedValue = true;
			}
		}

		// This code added to correctly implement the disposable pattern.
		public void Dispose()
		{
			Dispose(true);
		}
		#endregion
	}

	namespace Utils
	{
		
		public class TestConstructor
		{

			/// <summary>
			/// Helper function that returns the type of an object based on namespace and name
			/// </summary>
			/// <param name="Namespace"></param>
			/// <param name="TestName"></param>
			/// <returns></returns>
			private static Type GetTypeForTest(string TestName, IEnumerable<string> Namespaces)
			{
				var SearchAssemblies = AppDomain.CurrentDomain.GetAssemblies();

				// turn foo into [n1.foo, n2.foo, foo]
				IEnumerable<string> FullNames;

				if (Namespaces != null)
				{
					FullNames = Namespaces.Select(N => N + "." + TestName);
				}
				else
				{
					FullNames = new[] { TestName };
				}

				Log.VeryVerbose("Will search {0} for test {1}", string.Join(" ", FullNames), TestName);
				
				// find all types from loaded assemblies that implement testnode
					List < Type> CandidateTypes = new List<Type>();

				foreach (var Assembly in AppDomain.CurrentDomain.GetAssemblies())
				{
					foreach (var Type in Assembly.GetTypes())
					{
						if (typeof(ITestNode).IsAssignableFrom(Type))
						{
							CandidateTypes.Add(Type);
						}
					}
				}

				Log.VeryVerbose("Possible candidates for {0}: {1}", TestName, string.Join(" ", CandidateTypes));

				// check our expanded names.. need to search in namespace order
				foreach (string UserTypeName in FullNames)
				{
					// Even tho the user might have specified N1.Foo it still might be Other.N1.Foo so only
					// compare based on the number of namespaces that were specified.
					foreach (var Type in CandidateTypes)
					{
						string[] UserNameComponents = UserTypeName.Split('.');
						string[] TypeNameComponents = Type.FullName.Split('.');

						int MissingUserComponents = TypeNameComponents.Length - UserNameComponents.Length;

						if (MissingUserComponents > 0)
						{
							// 
							TypeNameComponents = TypeNameComponents.Skip(MissingUserComponents).ToArray();
						}

						var Difference = TypeNameComponents.Except(UserNameComponents, StringComparer.OrdinalIgnoreCase);

						if (Difference.Count() == 0)
						{
							Log.VeryVerbose("Considering {0} as best match for {1}", Type, TestName);
							return Type;
						}
					}
				}


				throw new AutomationException("Unable to find type {0} in assemblies. Namespaces= {1}.", TestName, Namespaces);
			}


			/// <summary>
			/// Helper function that returns all types within the specified namespace that are or derive from
			/// the specified type
			/// </summary>
			/// <param name="OfType"></param>
			/// <param name="TestName"></param>
			/// <returns></returns>
			public static IEnumerable<Type> GetTypesInNamespaces<BaseType>(IEnumerable<string> Namespaces)
				where BaseType : class
			{
				var AllTypes = AppDomain.CurrentDomain.GetAssemblies().SelectMany(S => S.GetTypes()).Where(T => typeof(BaseType).IsAssignableFrom(T));

				if (Namespaces.Count() > 0)
				{
					AllTypes = AllTypes.Where(T => T.IsAbstract == false && Namespaces.Contains(T.Namespace.ToString()));
				}

				return AllTypes;
			}

			/// <summary>
			/// Constructs by name a new test of type "TestType" that takes no construction parameters
			/// </summary>
			/// <typeparam name="TestType"></typeparam>
			/// <typeparam name="ContextType"></typeparam>
			/// <param name="Namespace"></param>
			/// <param name="TestName"></param>
			/// <param name="Context"></param>
			/// <returns></returns>
			public static TestType ConstructTest<TestType, ParamType>(string TestName, ParamType Arg, IEnumerable<string> Namespaces)
					where TestType : class
			{
				Type NodeType = GetTypeForTest(TestName, Namespaces);

				ConstructorInfo NodeConstructor = null;
				TestType NewNode = null;

				if (Arg != null)
				{
					NodeConstructor = NodeType.GetConstructor(new Type[] { Arg.GetType() });

					if (NodeConstructor != null)
					{
						NewNode = NodeConstructor.Invoke(new object[] { Arg }) as TestType;
					}
				}

				if (NodeConstructor == null)
				{
					NodeConstructor = NodeType.GetConstructor(Type.EmptyTypes);
					if (NodeConstructor != null)
					{
						NewNode = NodeConstructor.Invoke(null) as TestType;
					}
				}

				if (NodeConstructor == null)
				{
					throw new AutomationException("Unable to find constructor for type {0} with our without params", typeof(TestType));
				}

				if (NewNode == null)
				{
					throw new AutomationException("Unable to construct node of type {0}", typeof(TestType));
				}

				return NewNode;
			}


			/// <summary>
			/// Constructs by name a list of tests of type "TestType" that take no construction params
			/// </summary>
			/// <typeparam name="TestType"></typeparam>
			/// <param name="Namespace"></param>
			/// <param name="TestNames"></param>
			/// <returns></returns>
			public static IEnumerable<TestType> ConstructTests<TestType, ParamType>(IEnumerable<string> TestNames, ParamType Arg, IEnumerable<string> Namespaces)
					where TestType : class
			{
				List<TestType> Tests = new List<TestType>();

				foreach (var Name in TestNames)
				{
					Tests.Add(ConstructTest<TestType, ParamType>(Name, Arg, Namespaces));
				}

				return Tests;
			}

			/// <summary>
			/// Constructs by name a list of tests of type "TestType" that take no construction params
			/// </summary>
			/// <typeparam name="TestType"></typeparam>
			/// <param name="Namespace"></param>
			/// <param name="TestNames"></param>
			/// <returns></returns>
			public static IEnumerable<TestType> ConstructTests<TestType>(IEnumerable<string> TestNames, IEnumerable<string> Namespaces)
					where TestType : class
			{
				List<TestType> Tests = new List<TestType>();

				foreach (var Name in TestNames)
				{
					Tests.Add(ConstructTest<TestType, object>(Name, null, Namespaces));
				}

				return Tests;
			}

			/// <summary>
			/// Constructs by name a list of tests of type "TestType" that take no construction params
			/// </summary>
			/// <typeparam name="TestType"></typeparam>
			/// <param name="Namespace"></param>
			/// <param name="TestNames"></param>
			/// <returns></returns>
			public static IEnumerable<string> GetTestNamesByGroup<TestType>(string Group, IEnumerable<string> Namespaces)
					where TestType : class
			{
				// Get all types in these namespaces
				IEnumerable<Type> TestTypes = GetTypesInNamespaces<TestType>(Namespaces);

				IEnumerable<string> SortedTests = new string[0];

				// If no group, just return a sorted list
				if (string.IsNullOrEmpty(Group))
				{
					SortedTests = TestTypes.Select(T => T.FullName).OrderBy(S => S);
				}
				else
				{
					Dictionary<string, int> TypesToPriority = new Dictionary<string, int>();

					// Find ones that have a group attribute
					foreach (Type T in TestTypes)
					{
						foreach (object Attrib in T.GetCustomAttributes(true))
						{
							TestGroup TestAttrib = Attrib as TestGroup;

							// Store the type name as a key with the priority as a value
							if (TestAttrib != null && Group.Equals(TestAttrib.GroupName, StringComparison.OrdinalIgnoreCase))
							{
								TypesToPriority[T.FullName] = TestAttrib.Priority;
							}
						}
					}

					// sort by priority then name
					SortedTests = TypesToPriority.Keys.OrderBy(K => TypesToPriority[K]).ThenBy(K => K);
				}
			
				return SortedTests;
			}
		
		}

		public static class InterfaceHelpers
		{

			public static IEnumerable<InterfaceType> FindImplementations<InterfaceType>()
				where InterfaceType : class
			{
				var AllTypes = Assembly.GetExecutingAssembly().GetTypes().Where(T => typeof(InterfaceType).IsAssignableFrom(T));

				List<InterfaceType> ConstructedTypes = new List<InterfaceType>();

				foreach (Type FoundType in AllTypes)
				{
					ConstructorInfo TypeConstructor = FoundType.GetConstructor(Type.EmptyTypes);

					if (TypeConstructor != null)
					{
						InterfaceType NewInstance = TypeConstructor.Invoke(null) as InterfaceType;

						ConstructedTypes.Add(NewInstance);
					}
				}
				
				return ConstructedTypes;
			}

		}


		public static class SystemHelpers
		{
			/// <summary>
			/// Options for copying directories
			/// </summary>
			public enum CopyOptions
			{
				Copy = (1 << 0),        // Normal copy & combine/overwrite
				Mirror = (1 << 1),      // copy + remove files from dest if not in src
				Default = Copy
			}

			/// <summary>
			/// Options that can be specified to the CopyDirectory function.
			/// </summary>
			public class CopyDirectoryOptions
			{
				public CopyOptions Mode { get; set; }

				public int Retries { get; set; }

				public Func<string, string> Transform { get; set; }

				public string Pattern { get; set; }

				public Regex Regex { get; set; }

				public bool Recursive { get; set; }

				public bool Verbose { get; set; }

				public CopyDirectoryOptions()
				{
					Mode = CopyOptions.Copy;
					Retries = 10;
					Transform = delegate (string s)
					{
						return s;
					};
					Pattern = "*";
					Recursive = true;
					Regex = null;
					Verbose = false;
				}

				/// <summary>
				/// Returns true if the pattern indicates entire directory should be copied
				/// </summary>
				public bool IsDirectoryPattern
				{
					get
					{
						return 
							Regex == null &&
							(
								string.IsNullOrEmpty(Pattern)
								|| Pattern.Equals("*")
								|| Pattern.Equals("*.*")
								|| Pattern.Equals("...")
							);
					}
				}
			}
			
			/// <summary>
			/// Convenience function that removes some of the more esoteric options
			/// </summary>
			/// <param name="SourceDirPath"></param>
			/// <param name="DestDirPath"></param>
			/// <param name="Options"></param>
			/// <param name="RetryCount"></param>
			public static void CopyDirectory(string SourceDirPath, string DestDirPath, CopyOptions Mode = CopyOptions.Default, int RetryCount = 5)
			{
				CopyDirectory(SourceDirPath, DestDirPath, Mode, delegate (string s) { return s; }, RetryCount);
			}

			/// <summary>
			/// Legacy convenience function that exposes transform & Retry count
			/// </summary>
			/// <param name="SourceDirPath"></param>
			/// <param name="DestDirPath"></param>
			/// <param name="Mode"></param>
			/// <param name="Transform"></param>
			/// <param name="RetryCount"></param>
			public static void CopyDirectory(string SourceDirPath, string DestDirPath, CopyOptions Mode, Func<string, string> Transform, int RetryCount = 5)
			{
				CopyDirectoryOptions Options = new CopyDirectoryOptions();
				Options.Retries = RetryCount;
				Options.Mode = Mode;
				Options.Transform = Transform;

				CopyDirectory(SourceDirPath, DestDirPath, Options);
			}

			/// <summary>
			/// Copies src to dest by comparing files sizes and time stamps and only copying files that are different in src. Basically a more flexible
			/// robocopy
			/// </summary>
			/// <param name="SourcePath"></param>
			/// <param name="DestPath"></param>
			/// <param name="Verbose"></param>
			public static void CopyDirectory(string SourceDirPath, string DestDirPath, CopyDirectoryOptions Options)
			{
				DateTime StartTime = DateTime.Now;
				
				DirectoryInfo SourceDir = new DirectoryInfo(SourceDirPath);
				DirectoryInfo DestDir = new DirectoryInfo(DestDirPath);

				if (DestDir.Exists == false)
				{
					DestDir = Directory.CreateDirectory(DestDir.FullName);
				}
				
				bool IsMirroring = (Options.Mode & CopyOptions.Mirror) == CopyOptions.Mirror;

				if (IsMirroring && !Options.IsDirectoryPattern)
				{
					Log.Warning("Can only use mirror with pattern that includes whole directories (e.g. '*')");
					IsMirroring = false;
				}

				IEnumerable<FileInfo> SourceFiles = null;
				FileInfo[] DestFiles = null;

				// find all files. If a directory get them all, else use the pattern/regex
				if (Options.IsDirectoryPattern)
				{
					SourceFiles = SourceDir.GetFiles("*", Options.Recursive ? SearchOption.AllDirectories : SearchOption.TopDirectoryOnly);
				}
				else
				{
					if (Options.Regex == null)
					{
						SourceFiles = SourceDir.GetFiles(Options.Pattern, Options.Recursive ? SearchOption.AllDirectories : SearchOption.TopDirectoryOnly);
					}
					else
					{
						SourceFiles = SourceDir.GetFiles("*", Options.Recursive ? SearchOption.AllDirectories : SearchOption.TopDirectoryOnly);

						SourceFiles = SourceFiles.Where(F => Options.Regex.IsMatch(F.Name));
					}
				}

				// Convert dest into a map of relative paths to absolute
				Dictionary<string, System.IO.FileInfo> DestStructure = new Dictionary<string, System.IO.FileInfo>();

				if (IsMirroring)
				{
					DestFiles = DestDir.GetFiles("*", SearchOption.AllDirectories);

					foreach (FileInfo Info in DestFiles)
					{
						string RelativePath = Info.FullName.Replace(DestDir.FullName, "");

						// remove leading seperator
						if (RelativePath.First() == Path.DirectorySeparatorChar)
						{
							RelativePath = RelativePath.Substring(1);
						}

						DestStructure[RelativePath] = Info;
					}
				}

				// List of source files to copy. The first item is the full (and possibly transformed)
				// dest path, the second is the source
				List<Tuple<FileInfo, FileInfo>> CopyList = new List<Tuple<FileInfo, FileInfo>>();

				// List of relative path files in dest to delete
				List<string> DeletionList = new List<string>();

				foreach (FileInfo SourceInfo in SourceFiles)
				{
					string RelativeSourceFilePath = SourceInfo.FullName.Replace(SourceDir.FullName, "");

					// remove leading seperator
					if (RelativeSourceFilePath.First() == Path.DirectorySeparatorChar)
					{
						RelativeSourceFilePath = RelativeSourceFilePath.Substring(1);
					}

					string RelativeDestFilePath = Options.Transform(RelativeSourceFilePath);

					FileInfo DestInfo = null;

					// We may have destination info if mirroring where we prebuild it all, if not
					// grab it now
					if (DestStructure.ContainsKey(RelativeDestFilePath))
					{
						DestInfo = DestStructure[RelativeDestFilePath];
					}
					else
					{
						string FullDestPath = Path.Combine(DestDir.FullName, RelativeDestFilePath);
						DestInfo = new FileInfo(FullDestPath);						
					}

					if (DestInfo.Exists == false)
					{
						// No copy in dest, add it to the list
						CopyList.Add(new Tuple<FileInfo, FileInfo>(DestInfo, SourceInfo));
					}
					else
					{
						// Check the file is the same version

						// Difference in ticks. Even though we set the dest to the src there still appears to be minute
						// differences in ticks. 1ms is 10k ticks...
						Int64 TimeDelta = Math.Abs(DestInfo.LastWriteTime.Ticks - SourceInfo.LastWriteTime.Ticks);
						Int64 Threshhold = 100000;

						if (DestInfo.Length != SourceInfo.Length ||
							TimeDelta > Threshhold)
						{
							CopyList.Add(new Tuple<FileInfo, FileInfo>(DestInfo, SourceInfo));
						}
						else
						{
							if (Options.Verbose)
							{
								Log.Info("Will skip copy to {0}. File up to date.", DestInfo.FullName);
							}
							else
							{
								Log.Verbose("Will skip copy to {0}. File up to date.", DestInfo.FullName);
							}
						}

						// Remove it from the map
						DestStructure.Remove(RelativeDestFilePath);
					}
				}

				// If set to mirror, delete all the files that were not in source
				if (IsMirroring)
				{
					// Now go through the remaining map items and delete them
					foreach (var Pair in DestStructure)
					{
						DeletionList.Add(Pair.Key);
					}

					foreach (string RelativePath in DeletionList)
					{
						FileInfo DestInfo = new FileInfo(Path.Combine(DestDir.FullName, RelativePath));

						if (!DestInfo.Exists)
						{
							continue;
						}

						if (Options.Verbose)
						{
							Log.Info("Deleting extra file {0}", DestInfo.FullName);
						}
						else
						{
							Log.Verbose("Deleting extra file {0}", DestInfo.FullName);
						}

						try
						{
							// avoid an UnauthorizedAccessException by making sure file isn't read only
							DestInfo.IsReadOnly = false;
							DestInfo.Delete();
						}
						catch (Exception Ex)
						{
							Log.Warning("Failed to delete file {0}. {1}", DestInfo.FullName, Ex);
						}
					}

					// delete empty directories
					DirectoryInfo DestDirInfo = new DirectoryInfo(DestDirPath);

					DirectoryInfo[] AllSubDirs = DestDirInfo.GetDirectories("*", SearchOption.AllDirectories);

					foreach (DirectoryInfo SubDir in AllSubDirs)
					{
						try
						{
							if (SubDir.GetFiles().Length == 0 && SubDir.GetDirectories().Length == 0)
							{
								if (Options.Verbose)
								{
									Log.Info("Deleting empty dir {0}", SubDir.FullName);
								}
								else
								{
									Log.Verbose("Deleting empty dir {0}", SubDir.FullName);
								}

								SubDir.Delete(true);
							}
						}
						catch (Exception Ex)
						{
							// handle the case where a file is locked
							Log.Info("Failed to delete directory {0}. {1}", SubDir.FullName, Ex);
						}
					}
				}

				CancellationTokenSource CTS = new CancellationTokenSource();

				// todo - make param..
				var POptions = new ParallelOptions { MaxDegreeOfParallelism = 1, CancellationToken = CTS.Token  };

				// install a cancel handler so we can stop parallel-for gracefully
				Action CancelHandler = delegate()
				{
					CTS.Cancel();
				};

				Globals.AbortHandlers.Add(CancelHandler);

				// now do the work
				Parallel.ForEach(CopyList, POptions, FilePair =>
				{
					// ensure path exists
					FileInfo DestInfo = FilePair.Item1;
					FileInfo SrcInfo = FilePair.Item2;

					// ensure directory exists
					DestInfo.Directory.Create();

					int Tries = 0;
					bool Copied = false;

					do
					{
						try
						{
							if (Options.Verbose)
							{
								Log.Info("Copying to {0}", DestInfo.FullName);
							}
							else
							{
								Log.Verbose("Copying to {0}", DestInfo.FullName);
							}

							SrcInfo.CopyTo(DestInfo.FullName, true);

							// Clear attributes and set last write time
							DestInfo.Attributes = FileAttributes.Normal;
							DestInfo.LastWriteTime = SrcInfo.LastWriteTime;
							Copied = true;
						}
						catch (Exception ex)
						{
							if (Tries++ < Options.Retries)
							{
								Log.Info("Copy to {0} failed, retrying {1} of {2} in 30 secs..", DestInfo.FullName, Tries, Options.Retries);
								Log.Verbose("\t{0}", ex);
								Thread.Sleep(30000);
							}
							else
							{
								using (var PauseEC = new ScopedSuspendECErrorParsing())
								{
									Log.Error("File Copy failed with {0}.", ex.Message);
								}

								// Warn with message if we're exceeding long path, otherwise throw an exception
								const int MAX_PATH = 260;
								bool LongPath = BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64 && (SrcInfo.FullName.Length >= MAX_PATH || DestInfo.FullName.Length >= MAX_PATH);

								if (!LongPath)
								{
									throw new Exception(string.Format("File Copy failed with {0}.", ex.Message));
								}
								else
								{
									string LongPathMessage = (Environment.OSVersion.Version.Major > 6) ?
										"Long path detected, check that long paths are enabled." :
										"Long path detected, OS version doesn't support long paths.";

									// Break out of loop with warning
									Copied = true;

									// Filter out some known unneeded files which can cause this warning, and log the message instead
									string[] Blacklist = new string[]{ "UE4CC-XboxOne", "PersistentDownloadDir" };
									string Message = string.Format("Long path file copy failed with {0}.  Please verify that this file is not required.", ex.Message);
									if ( Blacklist.FirstOrDefault(B => { return SrcInfo.FullName.IndexOf(B, StringComparison.OrdinalIgnoreCase) >= 0; }) == null)
									{
										Log.Warning(Message); 
									}
									else
									{
										Log.Info(Message);
									}
								}
							}
						}
					} while (Copied == false);
				});

				TimeSpan Duration = DateTime.Now - StartTime;
				if (Duration.TotalSeconds > 10)
				{
					if (Options.Verbose)
					{
						Log.Info("Copied Directory in {0}", Duration.ToString(@"mm\m\:ss\s"));
					}
					else
					{
						Log.Verbose("Copied Directory in {0}", Duration.ToString(@"mm\m\:ss\s"));
					}
				}

				// remove cancel handler
				Globals.AbortHandlers.Remove(CancelHandler);
			}

			public static string MakePathRelative(string FullPath, string BasePath)
			{
				// base path must be correctly formed!
				if (BasePath.Last() != Path.DirectorySeparatorChar)
				{
					BasePath += Path.DirectorySeparatorChar;
				}					

				var ReferenceUri = new Uri(BasePath);
				var FullUri = new Uri(FullPath);

				return ReferenceUri.MakeRelativeUri(FullUri).ToString();
			}

			public static string CorrectDirectorySeparators(string InPath)
			{
				if (Path.DirectorySeparatorChar == '/')
				{
					return InPath.Replace('\\', Path.DirectorySeparatorChar);
				}
				else
				{
					return InPath.Replace('/', Path.DirectorySeparatorChar);
				}
			}

			public static bool ApplicationExists(string InPath)
			{
				if (File.Exists(InPath))
				{
					return true;
				}

				if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Mac)
				{
					if (InPath.EndsWith(".app", StringComparison.OrdinalIgnoreCase))
					{
						return Directory.Exists(InPath);
					}
				}

				return false;
			}

			public static bool IsNetworkPath(string InPath)
			{
				if (InPath.StartsWith("//") || InPath.StartsWith(@"\\"))
				{
					return true;
				}
				
				string RootPath = Path.GetPathRoot(InPath); // get drive's letter
				DriveInfo driveInfo = new System.IO.DriveInfo(RootPath); // get info about the drive
				return driveInfo.DriveType == DriveType.Network; // return true if a network drive
			}

			/// <summary>
			/// Marks a directory for future cleanup
			/// </summary>
			/// <param name="InPath"></param>
			public static void MarkDirectoryForCleanup(string InPath)
			{
				if (Directory.Exists(InPath) == false)
				{
					Directory.CreateDirectory(InPath);
				}

				// write a token, used to detect and old gauntlet-installed builds periodically
				string TokenPath = Path.Combine(InPath, "gauntlet.tempdir");
				File.WriteAllText(TokenPath, "Created by Gauntlet");
			}

			/// <summary>
			/// Removes any directory at the specified path which has a file  matching the provided name
			/// older than the specified number of days. Used by code that writes a .token file to temp
			/// folders
			/// </summary>
			/// <param name="InPath"></param>
			/// <param name="FileName"></param>
			/// <param name="Days"></param>
			public static void CleanupMarkedDirectories(string InPath, int Days)
			{
				DirectoryInfo Di = new DirectoryInfo(InPath);

				if (Di.Exists == false)
				{
					return;
				}

				foreach (DirectoryInfo SubDir in Di.GetDirectories())
				{
					bool HasFile = 
						SubDir.GetFiles().Where(F => {
							int DaysOld = (DateTime.Now - F.LastWriteTime).Days;				
							
							if (DaysOld >= Days)
							{
								// use the old and new tokennames
								return string.Equals(F.Name, "gauntlet.tempdir", StringComparison.OrdinalIgnoreCase) ||
										string.Equals(F.Name, "gauntlet.token", StringComparison.OrdinalIgnoreCase);
							}

							return false;
						}).Count() > 0;

					if (HasFile)
					{
						Log.Info("Removing old directory {0}", SubDir.Name);
						try
						{
							SubDir.Delete(true);
						}
						catch (Exception Ex)
						{
							Log.Warning("Failed to remove old directory {0}. {1}", SubDir.FullName, Ex.Message);
						}
					}
					else
					{
						CleanupMarkedDirectories(SubDir.FullName, Days);
					}
				}				
			}
		}

		public class Image
		{
			protected static IEnumerable<FileInfo> GetSupportedFilesAtPath(string InPath)
			{
				string[] Extensions = new[] { ".jpg", ".jpeg", ".png", ".bmp" };

				DirectoryInfo Di = new DirectoryInfo(InPath);

				var Files = Di.GetFiles().Where(f => Extensions.Contains(f.Extension.ToLower()));

				return Files;
			}


			public static bool SaveImagesAsGif(IEnumerable<string> FilePaths, string OutPath, int Delay=100)
			{
				Log.Verbose("Turning {0} files into {1}", FilePaths.Count(), OutPath);
				try
				{
					using (MagickImageCollection Collection = new MagickImageCollection())
					{
						foreach (string File in FilePaths)
						{
							int Index = Collection.Count();
							Collection.Add(File);
							Collection[Index].AnimationDelay = Delay;
						}

						// Optionally reduce colors
						/*QuantizeSettings settings = new QuantizeSettings();
						settings.Colors = 256;
						Collection.Quantize(settings);*/

						foreach (MagickImage image in Collection)
						{
							image.Resize(640, 0);
						}

						// Optionally optimize the images (images should have the same size).
						Collection.Optimize();

						// Save gif
						Collection.Write(OutPath);

						Log.Verbose("Saved {0}", OutPath);
					}
				}
				catch (System.Exception Ex)
				{
					Log.Warning("SaveAsGif failed: {0}", Ex);
					return false;
				}

				return true;
			}

			public static bool SaveImagesAsGif(string InDirectory, string OutPath)
			{
				string[] Extensions = new[] { ".jpg", ".jpeg", ".png", ".bmp" };

				DirectoryInfo Di = new DirectoryInfo(InDirectory);

				var Files = GetSupportedFilesAtPath(InDirectory);

				// sort by creation time
				Files = Files.OrderBy(F => F.CreationTimeUtc);

				if (Files.Count() == 0)
				{
					Log.Info("Could not find files at {0} to Gif-ify", InDirectory);
					return false;
				}

				return SaveImagesAsGif(Files.Select(F => F.FullName), OutPath);
			}

			public static bool ResizeImages(string InDirectory, int MaxWidth)
			{
				var Files = GetSupportedFilesAtPath(InDirectory);

				if (Files.Count() == 0)
				{
					Log.Warning("Could not find files at {0} to resize", InDirectory);
					return false;
				}

				Log.Verbose("Reizing {0} files at {1} to have a max width of {2}", Files.Count(), InDirectory, MaxWidth);

				try
				{
					foreach (FileInfo File in Files)
					{
						using (MagickImage Image = new MagickImage(File.FullName))
						{
							if (Image.Width > MaxWidth)
							{
								Image.Resize(MaxWidth, 0);
								Image.Write(File);
							}
						}
					}				
				}
				catch (System.Exception Ex)
				{
					Log.Warning("ResizeImages failed: {0}", Ex);
					return false;
				}

				return true;
			}


			public static bool ConvertImages(string InDirectory, string OutDirectory, string OutExtension, bool DeleteOriginals)
			{
				var Files = GetSupportedFilesAtPath(InDirectory);

				if (Files.Count() == 0)
				{
					Log.Warning("Could not find files at {0} to resize", InDirectory);
					return false;
				}

				Log.Verbose("Converting {0} files to {1}", Files.Count(), OutExtension);

				try
				{
					List<FileInfo> FilesToCleanUp = new List<FileInfo>();
					foreach (FileInfo File in Files)
					{
						using (MagickImage Image = new MagickImage(File.FullName))
						{
							string OutFile = Path.Combine(OutDirectory, File.Name);
							OutFile = Path.ChangeExtension(OutFile, OutExtension);
							// If we're trying to convert something to itself in place, skip the step.
							if (OutFile != File.FullName)
							{
								Image.Write(OutFile);
								if (DeleteOriginals)
								{
									FilesToCleanUp.Add(File);
								}
							}
						}
					}

					foreach (FileInfo File in FilesToCleanUp)
					{
						File.Delete();
					}
				}
				catch (System.Exception Ex)
				{
					Log.Warning("ConvertImages failed: {0}", Ex);
					try
					{
						if (DeleteOriginals)
						{
							Files.ToList().ForEach(F => F.Delete());
						}
					}
					catch (System.Exception e)
					{
						Log.Warning("Cleaning up original files failed: {0}", e);
					}
					return false;
				}

				return true;
			}
		}
	}

	public static class RegexUtil
	{
		public static bool MatchAndApplyGroups(string InContent, string RegEx, Action<string[]> InFunc)
		{
			return MatchAndApplyGroups(InContent, RegEx, RegexOptions.IgnoreCase, InFunc);
		}

		public static bool MatchAndApplyGroups(string InContent, string RegEx, RegexOptions Options, Action<string[]> InFunc)
		{
			Match M = Regex.Match(InContent, RegEx, Options);

			IEnumerable<string> StringMatches = null;

			if (M.Success)
			{
				StringMatches = M.Groups.Cast<Capture>().Select(G => G.ToString());
				InFunc(StringMatches.ToArray());
			}

			return M.Success;
		}
	}

	public static class DirectoryUtils
	{
		/// <summary>
		/// Enumerate files from a given directory that pass the specified regex
		/// </summary>
		/// <param name="BaseDir">Base directory to search in</param>
		/// <param name="Pattern">Pattern for matching files</param>
		/// <param name="Option">Options for the search</param>
		/// <returns>Sequence of file references</returns>
		public static IEnumerable<string> FindFiles(string BaseDir, Regex Pattern)
		{
			IEnumerable<string> Files = System.IO.Directory.EnumerateFiles(BaseDir, "*");

			Files = Files.Where(F => Pattern.IsMatch(F));

			return Files.ToArray();
		}
	}

}
