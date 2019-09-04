// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Data;
using System.Data.SqlClient;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;

namespace UnrealGameSync
{
	enum EventType
	{
		Syncing,

		// Reviews
		Compiles,
		DoesNotCompile,
		Good,
		Bad,
		Unknown,
		
		// Starred builds
		Starred,
		Unstarred,

		// Investigating events
		Investigating,
		Resolved,
	}

	class LatestData
	{
		public long LastEventId { get; set; }
		public long LastCommentId { get; set; }
		public long LastBuildId { get; set; }
	}

	class EventData
	{
		public long Id { get; set; }
		public int Change { get; set; }
		public string UserName { get; set; }
		public EventType Type { get; set; }
		public string Project { get; set; }
	}

	class CommentData
	{
		public long Id { get; set; }
		public int ChangeNumber { get; set; }
		public string UserName { get; set; }
		public string Text { get; set; }
		public string Project { get; set; }
	}

	enum BadgeResult
	{
		Starting,
		Failure,
		Warning,
		Success,
		Skipped,
	}

	class BadgeData
	{
		public long Id { get; set; }
		public int ChangeNumber { get; set; }
		public string BuildType { get; set; }
		public BadgeResult Result { get; set; }
		public string Url { get; set; }
		public string Project { get; set; }

		public bool IsSuccess
		{
			get { return Result == BadgeResult.Success || Result == BadgeResult.Warning; }
		}

		public bool IsFailure
		{
			get { return Result == BadgeResult.Failure; }
		}

		public string BadgeName
		{
			get
			{
				int Idx = BuildType.IndexOf(':');
				if(Idx == -1)
				{
					return BuildType;
				}
				else
				{
					return BuildType.Substring(0, Idx);
				}
			}
		}

		public string BadgeLabel
		{
			get
			{
				int Idx = BuildType.IndexOf(':');
				if(Idx == -1)
				{
					return BuildType;
				}
				else
				{
					return BuildType.Substring(Idx + 1);
				}
			}
		}
	}

	enum ReviewVerdict
	{
		Unknown,
		Good,
		Bad,
		Mixed,
	}

	class EventSummary
	{
		public int ChangeNumber;
		public ReviewVerdict Verdict;
		public List<EventData> SyncEvents = new List<EventData>();
		public List<EventData> Reviews = new List<EventData>();
		public List<string> CurrentUsers = new List<string>();
		public EventData LastStarReview;
		public List<BadgeData> Badges = new List<BadgeData>();
		public List<CommentData> Comments = new List<CommentData>();
	}

	class EventMonitor : IDisposable
	{
		string ApiUrl;
		string Project;
		string CurrentUserName;
		Thread WorkerThread;
		AutoResetEvent RefreshEvent = new AutoResetEvent(false);
		ConcurrentQueue<EventData> OutgoingEvents = new ConcurrentQueue<EventData>();
		ConcurrentQueue<EventData> IncomingEvents = new ConcurrentQueue<EventData>();
		ConcurrentQueue<CommentData> OutgoingComments = new ConcurrentQueue<CommentData>();
		ConcurrentQueue<CommentData> IncomingComments = new ConcurrentQueue<CommentData>();
		ConcurrentQueue<BadgeData> IncomingBadges = new ConcurrentQueue<BadgeData>();
		Dictionary<int, EventSummary> ChangeNumberToSummary = new Dictionary<int, EventSummary>();
		Dictionary<string, EventData> UserNameToLastSyncEvent = new Dictionary<string, EventData>(StringComparer.InvariantCultureIgnoreCase);
		Dictionary<string, BadgeData> BadgeNameToLatestData = new Dictionary<string, BadgeData>();
		BoundedLogWriter LogWriter;
		bool bDisposing;
		LatestData LatestIds;
		HashSet<int> FilterChangeNumbers = new HashSet<int>();
		List<EventData> InvestigationEvents = new List<EventData>();
		List<EventData> ActiveInvestigations = new List<EventData>();

		public event Action OnUpdatesReady;

		public EventMonitor(string InApiUrl, string InProject, string InCurrentUserName, string InLogFileName)
		{
			ApiUrl = InApiUrl;
			Project = InProject;
			CurrentUserName = InCurrentUserName;
			LatestIds = new LatestData { LastBuildId = 0, LastCommentId = 0, LastEventId = 0 };

			LogWriter = new BoundedLogWriter(InLogFileName);
			if(ApiUrl == null)
			{
				LastStatusMessage = "Database functionality disabled due to empty ApiUrl.";
			}
			else
			{
				LogWriter.WriteLine("Using connection string: {0}", ApiUrl);
			}
		}

		public void Start()
		{
			WorkerThread = new Thread(() => PollForUpdates());
			WorkerThread.Start();
		}

		public void Dispose()
		{
			bDisposing = true;
			if(WorkerThread != null)
			{
				RefreshEvent.Set();
				if(!WorkerThread.Join(100))
				{
					WorkerThread.Abort();
					WorkerThread.Join();
				}
				WorkerThread = null;
			}
			if(LogWriter != null)
			{
				LogWriter.Dispose();
				LogWriter = null;
			}
		}

		public void FilterChanges(IEnumerable<int> ChangeNumbers)
		{
			// Build a lookup for all the change numbers
			FilterChangeNumbers = new HashSet<int>(ChangeNumbers);

			// Clear out the list of active users for each review we have
			UserNameToLastSyncEvent.Clear();
			foreach(EventSummary Summary in ChangeNumberToSummary.Values)
			{
				Summary.CurrentUsers.Clear();
			}

			// Add all the user reviews back in again
			foreach(EventSummary Summary in ChangeNumberToSummary.Values)
			{
				foreach(EventData SyncEvent in Summary.SyncEvents)
				{
					ApplyFilteredUpdate(SyncEvent);
				}
			}

			// Clear the list of active investigations, since this depends on the changes we're showing
			ActiveInvestigations = null;
		}

		protected EventSummary FindOrAddSummary(int ChangeNumber)
		{
			EventSummary Summary;
			if(!ChangeNumberToSummary.TryGetValue(ChangeNumber, out Summary))
			{
				Summary = new EventSummary();
				Summary.ChangeNumber = ChangeNumber;
				ChangeNumberToSummary.Add(ChangeNumber, Summary);
			}
			return Summary;
		}

		public string LastStatusMessage
		{
			get;
			private set;
		}

		public void ApplyUpdates()
		{
			EventData Event;
			while(IncomingEvents.TryDequeue(out Event))
			{
				ApplyEventUpdate(Event);
			}

			BadgeData Badge;
			while(IncomingBadges.TryDequeue(out Badge))
			{
				ApplyBadgeUpdate(Badge);
			}

			CommentData Comment;
			while(IncomingComments.TryDequeue(out Comment))
			{
				ApplyCommentUpdate(Comment);
			}
		}

		void ApplyEventUpdate(EventData Event)
		{
			EventSummary Summary = FindOrAddSummary(Event.Change);
			if(Event.Type == EventType.Starred || Event.Type == EventType.Unstarred)
			{
				// If it's a star or un-star review, process that separately
				if(Summary.LastStarReview == null || Event.Id > Summary.LastStarReview.Id)
				{
					Summary.LastStarReview = Event;
				}
			}
			else if(Event.Type == EventType.Investigating || Event.Type == EventType.Resolved)
			{
				// Insert it sorted in the investigation list
				int InsertIdx = 0;
				while(InsertIdx < InvestigationEvents.Count && InvestigationEvents[InsertIdx].Id < Event.Id)
				{
					InsertIdx++;
				}
				if(InsertIdx == InvestigationEvents.Count || InvestigationEvents[InsertIdx].Id != Event.Id)
				{
					InvestigationEvents.Insert(InsertIdx, Event);
				}
				ActiveInvestigations = null;
			}
			else if(Event.Type == EventType.Syncing)
			{
				Summary.SyncEvents.RemoveAll(x => String.Compare(x.UserName, Event.UserName, true) == 0);
				Summary.SyncEvents.Add(Event);
				ApplyFilteredUpdate(Event);
			}
			else if(IsReview(Event.Type))
			{
				// Try to find an existing review by this user. If we already have a newer review, ignore this one. Otherwise remove it.
				EventData ExistingReview = Summary.Reviews.Find(x => String.Compare(x.UserName, Event.UserName, true) == 0);
				if(ExistingReview != null)
				{
					if(ExistingReview.Id <= Event.Id)
					{
						Summary.Reviews.Remove(ExistingReview);
					}
					else
					{
						return;
					}
				}

				// Add the new review, and find the new verdict for this change
				Summary.Reviews.Add(Event);
				Summary.Verdict = GetVerdict(Summary.Reviews, Summary.Badges);
			}
			else
			{
				// Unknown type
			}
		}

		void ApplyBadgeUpdate(BadgeData Badge)
		{
			EventSummary Summary = FindOrAddSummary(Badge.ChangeNumber);

			BadgeData ExistingBadge = Summary.Badges.Find(x => x.ChangeNumber == Badge.ChangeNumber && x.BuildType == Badge.BuildType);
			if(ExistingBadge != null)
			{
				if(ExistingBadge.Id <= Badge.Id)
				{
					Summary.Badges.Remove(ExistingBadge);
				}
				else
				{
					return;
				}
			}

			Summary.Badges.Add(Badge);
			Summary.Verdict = GetVerdict(Summary.Reviews, Summary.Badges);

			BadgeData LatestBadge;
			if(!BadgeNameToLatestData.TryGetValue(Badge.BadgeName, out LatestBadge) || Badge.ChangeNumber > LatestBadge.ChangeNumber || (Badge.ChangeNumber == LatestBadge.ChangeNumber && Badge.Id > LatestBadge.Id))
			{
				BadgeNameToLatestData[Badge.BadgeName] = Badge;
			}
		}

		void ApplyCommentUpdate(CommentData Comment)
		{
			EventSummary Summary = FindOrAddSummary(Comment.ChangeNumber);
			if(String.Compare(Comment.UserName, CurrentUserName, true) == 0 && Summary.Comments.Count > 0 && Summary.Comments.Last().Id == long.MaxValue)
			{
				// This comment was added by PostComment(), to mask the latency of a round trip to the server. Remove it now we have the sorted comment.
				Summary.Comments.RemoveAt(Summary.Comments.Count - 1);
			}
			AddPerUserItem(Summary.Comments, Comment, x => x.Id, x => x.UserName);
		}

		static bool AddPerUserItem<T>(List<T> Items, T NewItem, Func<T, long> IdSelector, Func<T, string> UserSelector)
		{
			int InsertIdx = Items.Count;

			for(; InsertIdx > 0 && IdSelector(Items[InsertIdx - 1]) >= IdSelector(NewItem); InsertIdx--)
			{
				if(String.Compare(UserSelector(Items[InsertIdx - 1]), UserSelector(NewItem), true) == 0)
				{
					return false;
				}
			}

			Items.Insert(InsertIdx, NewItem);

			for(; InsertIdx > 0; InsertIdx--)
			{
				if(String.Compare(UserSelector(Items[InsertIdx - 1]), UserSelector(NewItem), true) == 0)
				{
					Items.RemoveAt(InsertIdx - 1);
				}
			}

			return true;
		}

		static ReviewVerdict GetVerdict(IEnumerable<EventData> Events, IEnumerable<BadgeData> Badges)
		{
			int NumPositiveReviews = Events.Count(x => x.Type == EventType.Good);
			int NumNegativeReviews = Events.Count(x => x.Type == EventType.Bad);
			if(NumPositiveReviews > 0 || NumNegativeReviews > 0)
			{
				return GetVerdict(NumPositiveReviews, NumNegativeReviews);
			}

			int NumCompiles = Events.Count(x => x.Type == EventType.Compiles);
			int NumFailedCompiles = Events.Count(x => x.Type == EventType.DoesNotCompile);
			if(NumCompiles > 0 || NumFailedCompiles > 0)
			{
				return GetVerdict(NumCompiles, NumFailedCompiles);
			}

			int NumBadges = Badges.Count(x => x.BuildType == "Editor" && x.IsSuccess);
			int NumFailedBadges = Badges.Count(x => x.BuildType == "Editor" && x.IsFailure);
			if(NumBadges > 0 || NumFailedBadges > 0)
			{
				return GetVerdict(NumBadges, NumFailedBadges);
			}

			return ReviewVerdict.Unknown;
		}

		static ReviewVerdict GetVerdict(int NumPositive, int NumNegative)
		{
			if(NumPositive > (int)(NumNegative * 1.5))
			{
				return ReviewVerdict.Good;
			}
			else if(NumPositive >= NumNegative)
			{
				return ReviewVerdict.Mixed;
			}
			else
			{
				return ReviewVerdict.Bad;
			}
		}

		void ApplyFilteredUpdate(EventData Event)
		{
			if(Event.Type == EventType.Syncing && FilterChangeNumbers.Contains(Event.Change))
			{
				// Update the active users list for this change
				EventData LastSync;
				if(UserNameToLastSyncEvent.TryGetValue(Event.UserName, out LastSync))
				{
					if(Event.Id > LastSync.Id)
					{
						ChangeNumberToSummary[LastSync.Change].CurrentUsers.RemoveAll(x => String.Compare(x, Event.UserName, true) == 0);
						FindOrAddSummary(Event.Change).CurrentUsers.Add(Event.UserName);
						UserNameToLastSyncEvent[Event.UserName] = Event;
					}
				}
				else
				{
					FindOrAddSummary(Event.Change).CurrentUsers.Add(Event.UserName);
					UserNameToLastSyncEvent[Event.UserName] = Event;
				}
			}
		}

		void PollForUpdates()
		{
			EventData Event = null;
			CommentData Comment = null;
			bool bUpdateThrottledRequests = true;
			double RequestThrottle = 90; // seconds to wait for throttled request;
			Stopwatch Timer = Stopwatch.StartNew();
			while (!bDisposing)
			{
				// If there's no connection string, just empty out the queue
				if (ApiUrl != null)
				{
					// Post all the reviews to the database. We don't send them out of order, so keep the review outside the queue until the next update if it fails
					while(Event != null || OutgoingEvents.TryDequeue(out Event))
					{
						SendEventToBackend(Event);
						Event = null;
					}

					// Post all the comments to the database.
					while(Comment != null || OutgoingComments.TryDequeue(out Comment))
					{
						SendCommentToBackend(Comment);
						Comment = null;
					}

					if(Timer.Elapsed > TimeSpan.FromSeconds(RequestThrottle))
					{
						bUpdateThrottledRequests = true;
						Timer.Restart();
					}

					// Read all the new reviews, pass whether or not to fire the throttled requests
					ReadEventsFromBackend(bUpdateThrottledRequests);

					// Send a notification that we're ready to update
					if((IncomingEvents.Count > 0 || IncomingBadges.Count > 0 || IncomingComments.Count > 0) && OnUpdatesReady != null)
					{
						OnUpdatesReady();
					}
				}

				// Wait for something else to do
				bUpdateThrottledRequests = RefreshEvent.WaitOne(30 * 1000);
			}
		}

		bool SendEventToBackend(EventData Event)
		{
			try
			{
				Stopwatch Timer = Stopwatch.StartNew();
				LogWriter.WriteLine("Posting event... ({0}, {1}, {2})", Event.Change, Event.UserName, Event.Type);
				RESTApi.POST(ApiUrl, "event", new JavaScriptSerializer().Serialize(Event));
				return true;
			}
			catch(Exception Ex)
			{
				LogWriter.WriteException(Ex, "Failed with exception.");
				return false;
			}
		}

		bool SendCommentToBackend(CommentData Comment)
		{
			try
			{
				Stopwatch Timer = Stopwatch.StartNew();
				LogWriter.WriteLine("Posting comment... ({0}, {1}, {2}, {3})", Comment.ChangeNumber, Comment.UserName, Comment.Text, Comment.Project);
				RESTApi.POST(ApiUrl, "comment", new JavaScriptSerializer().Serialize(Comment));
				LogWriter.WriteLine("Done in {0}ms.", Timer.ElapsedMilliseconds);
				return true;
			}
			catch(Exception Ex)
			{
				LogWriter.WriteException(Ex, "Failed with exception.");
				return false;
			}
		}

		bool ReadEventsFromBackend(bool bFireThrottledRequests)
		{
			try
			{
				Stopwatch Timer = Stopwatch.StartNew();
				LogWriter.WriteLine();
				LogWriter.WriteLine("Polling for reviews at {0}...", DateTime.Now.ToString());
				//////////////
				/// Initial Ids 
				//////////////
				if (LatestIds.LastBuildId == 0 && LatestIds.LastCommentId == 0 && LatestIds.LastEventId == 0)
				{
					LatestData InitialIds = RESTApi.GET<LatestData>(ApiUrl, "latest", string.Format("project={0}", Project));
					LatestIds.LastBuildId = InitialIds.LastBuildId;
					LatestIds.LastCommentId = InitialIds.LastCommentId;
					LatestIds.LastEventId = InitialIds.LastEventId;
				}

				//////////////
				/// Bulids
				//////////////
				List<BadgeData> Builds = RESTApi.GET<List<BadgeData>>(ApiUrl, "build", string.Format("project={0}", Project), string.Format("lastbuildid={0}", LatestIds.LastBuildId));
				foreach (BadgeData Build in Builds)
				{
					IncomingBadges.Enqueue(Build);
					LatestIds.LastBuildId = Math.Max(LatestIds.LastBuildId, Build.Id);
				}

				//////////////////////////
				/// Throttled Requests
				//////////////////////////
				if (bFireThrottledRequests)
				{
					//////////////
					/// Reviews 
					//////////////
					List<EventData> Events = RESTApi.GET<List<EventData>>(ApiUrl, "event", string.Format("project={0}", Project), string.Format("lasteventid={0}", LatestIds.LastEventId));
					foreach (EventData Review in Events)
					{
						IncomingEvents.Enqueue(Review);
						LatestIds.LastEventId = Math.Max(LatestIds.LastEventId, Review.Id);
					}

					//////////////
					/// Comments 
					//////////////
					List<CommentData> Comments = RESTApi.GET<List<CommentData>>(ApiUrl, "comment", string.Format("project={0}", Project), string.Format("lastcommentid={0}", LatestIds.LastCommentId));
					foreach (CommentData Comment in Comments)
					{
						IncomingComments.Enqueue(Comment);
						LatestIds.LastCommentId = Math.Max(LatestIds.LastCommentId, Comment.Id);
					}
				}

				LastStatusMessage = String.Format("Last update took {0}ms", Timer.ElapsedMilliseconds);
				LogWriter.WriteLine("Done in {0}ms.", Timer.ElapsedMilliseconds);
				return true;
			}
			catch(Exception Ex)
			{
				LogWriter.WriteException(Ex, "Failed with exception.");
				LastStatusMessage = String.Format("Last update failed: ({0})", Ex.ToString());
				return false;
			}
		}

		static bool MatchesWildcard(string Wildcard, string Project)
		{
			return Wildcard.EndsWith("...") && Project.StartsWith(Wildcard.Substring(0, Wildcard.Length - 3), StringComparison.InvariantCultureIgnoreCase);
		}

		public void PostEvent(int ChangeNumber, EventType Type)
		{
			if(ApiUrl != null)
			{
				EventData Event = new EventData();
				Event.Change = ChangeNumber;
				Event.UserName = CurrentUserName;
				Event.Type = Type;
				Event.Project = Project;
				OutgoingEvents.Enqueue(Event);

				ApplyEventUpdate(Event);

				RefreshEvent.Set();
			}
		}

		public void PostComment(int ChangeNumber, string Text)
		{
			if(ApiUrl != null)
			{
				CommentData Comment = new CommentData();
				Comment.Id = long.MaxValue;
				Comment.ChangeNumber = ChangeNumber;
				Comment.UserName = CurrentUserName;
				Comment.Text = Text;
				Comment.Project = Project;
				OutgoingComments.Enqueue(Comment);

				ApplyCommentUpdate(Comment);

				RefreshEvent.Set();
			}
		}

		public bool GetCommentByCurrentUser(int ChangeNumber, out string CommentText)
		{
			EventSummary Summary = GetSummaryForChange(ChangeNumber);
			if(Summary == null)
			{
				CommentText = null;
				return false;
			}

			CommentData Comment = Summary.Comments.Find(x => String.Compare(x.UserName, CurrentUserName, true) == 0);
			if(Comment == null || String.IsNullOrWhiteSpace(Comment.Text))
			{
				CommentText = null;
				return false;
			}

			CommentText = Comment.Text;
			return true;
		}

		public EventData GetReviewByCurrentUser(int ChangeNumber)
		{
			EventSummary Summary = GetSummaryForChange(ChangeNumber);
			if(Summary == null)
			{
				return null;
			}

			EventData Event = Summary.Reviews.FirstOrDefault(x => String.Compare(x.UserName, CurrentUserName, true) == 0);
			if(Event == null || Event.Type == EventType.Unknown)
			{
				return null;
			}

			return Event;
		}

		public EventSummary GetSummaryForChange(int ChangeNumber)
		{
			EventSummary Summary;
			ChangeNumberToSummary.TryGetValue(ChangeNumber, out Summary);
			return Summary;
		}

		public bool TryGetLatestBadge(string BuildType, out BadgeData BadgeData)
		{
			return BadgeNameToLatestData.TryGetValue(BuildType, out BadgeData);
		}

		public static bool IsReview(EventType Type)
		{
			return IsPositiveReview(Type) || IsNegativeReview(Type) || Type == EventType.Unknown;
		}

		public static bool IsPositiveReview(EventType Type)
		{
			return Type == EventType.Good || Type == EventType.Compiles;
		}

		public static bool IsNegativeReview(EventType Type)
		{
			return Type == EventType.DoesNotCompile || Type == EventType.Bad;
		}

		public bool WasSyncedByCurrentUser(int ChangeNumber)
		{
			EventSummary Summary = GetSummaryForChange(ChangeNumber);
			return (Summary != null && Summary.SyncEvents.Any(x => x.Type == EventType.Syncing && String.Compare(x.UserName, CurrentUserName, true) == 0));
		}

		public void StartInvestigating(int ChangeNumber)
		{
			PostEvent(ChangeNumber, EventType.Investigating);
		}

		public void FinishInvestigating(int ChangeNumber)
		{
			PostEvent(ChangeNumber, EventType.Resolved);
		}

		protected void UpdateActiveInvestigations()
		{
			if(ActiveInvestigations == null)
			{
				// Insert investigation events into the active list, sorted by change number. Remove 
				ActiveInvestigations = new List<EventData>();
				foreach(EventData InvestigationEvent in InvestigationEvents)
				{
					if(FilterChangeNumbers.Contains(InvestigationEvent.Change))
					{
						if(InvestigationEvent.Type == EventType.Investigating)
						{
							int InsertIdx = 0;
							while(InsertIdx < ActiveInvestigations.Count && ActiveInvestigations[InsertIdx].Change > InvestigationEvent.Change)
							{
								InsertIdx++;
							}
							ActiveInvestigations.Insert(InsertIdx, InvestigationEvent);
						}
						else
						{
							ActiveInvestigations.RemoveAll(x => String.Compare(x.UserName, InvestigationEvent.UserName, true) == 0 && x.Change <= InvestigationEvent.Change);
						}
					}
				}

				// Remove any duplicate users
				for(int Idx = 0; Idx < ActiveInvestigations.Count; Idx++)
				{
					for(int OtherIdx = 0; OtherIdx < Idx; OtherIdx++)
					{
						if(String.Compare(ActiveInvestigations[Idx].UserName, ActiveInvestigations[OtherIdx].UserName, true) == 0)
						{
							ActiveInvestigations.RemoveAt(Idx--);
							break;
						}
					}
				}
			}
		}

		public bool IsUnderInvestigation(int ChangeNumber)
		{
			UpdateActiveInvestigations();
			return ActiveInvestigations.Any(x => x.Change <= ChangeNumber);
		}

		public bool IsUnderInvestigationByCurrentUser(int ChangeNumber)
		{
			UpdateActiveInvestigations();
			return ActiveInvestigations.Any(x => x.Change <= ChangeNumber && String.Compare(x.UserName, CurrentUserName, true) == 0);
		}

		public IEnumerable<string> GetInvestigatingUsers(int ChangeNumber)
		{
			UpdateActiveInvestigations();
			return ActiveInvestigations.Where(x => ChangeNumber >= x.Change).Select(x => x.UserName);
		}

		public int GetInvestigationStartChangeNumber(int LastChangeNumber)
		{
			UpdateActiveInvestigations();

			int StartChangeNumber = -1;
			foreach(EventData ActiveInvestigation in ActiveInvestigations)
			{
				if(String.Compare(ActiveInvestigation.UserName, CurrentUserName, true) == 0)
				{
					if(ActiveInvestigation.Change <= LastChangeNumber && (StartChangeNumber == -1 || ActiveInvestigation.Change < StartChangeNumber))
					{
						StartChangeNumber = ActiveInvestigation.Change;
					}
				}
			}
			return StartChangeNumber;
		}
	}
}
