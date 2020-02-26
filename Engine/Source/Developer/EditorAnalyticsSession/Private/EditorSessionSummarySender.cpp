// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EditorSessionSummarySender.h"

#include "AnalyticsEventAttribute.h"
#include "Algo/Transform.h"
#include "Interfaces/IAnalyticsProvider.h"
#include "EditorAnalyticsSession.h"
#include "HAL/PlatformProcess.h"
#include "IAnalyticsProviderET.h"

DEFINE_LOG_CATEGORY_STATIC(LogEditorSessionSummary, Verbose, All);

/* FEditorSessionSummarySender */

namespace EditorSessionSenderDefs
{
	static const FTimespan SessionExpiration = FTimespan::FromDays(30.0);
	static const float HeartbeatPeriodSeconds = 60;

	// shutdown types
	static const FString RunningSessionToken(TEXT("Running"));
	static const FString ShutdownSessionToken(TEXT("Shutdown"));
	static const FString CrashSessionToken(TEXT("Crashed"));
	static const FString TerminatedSessionToken(TEXT("Terminated"));
	static const FString DebuggerSessionToken(TEXT("Debugger"));
	static const FString AbnormalSessionToken(TEXT("AbnormalShutdown"));
}

namespace EditorSessionSenderUtil_4_24_3
{
	static const FString StoreId(TEXT("Epic Games"));
	static const FString SessionSummarySection(TEXT("Unreal Engine/Session Summary/1_0"));

	static const FString AppIdStoreKey( TEXT("AppId"));
	static const FString AppVersionStoreKey(TEXT("AppVersion"));
	static const FString UserIdStoreKey(TEXT("UserId"));

	void DeleteExtraSessionKeys(const FString& SessionId)
	{
		const FString SectionName = SessionSummarySection + TEXT("/") + SessionId;
		FPlatformMisc::DeleteStoredValue(StoreId, SectionName, AppIdStoreKey);
		FPlatformMisc::DeleteStoredValue(StoreId, SectionName, AppVersionStoreKey);
		FPlatformMisc::DeleteStoredValue(StoreId, SectionName, UserIdStoreKey);
	}

	void ReadAndDeleteExtraSessionKey(const FString& SessionId, const FString& InKey, FString& OutValue)
	{
		const FString SectionName = SessionSummarySection + TEXT("/") + SessionId;
		FPlatformMisc::GetStoredValue(StoreId, SectionName, InKey, OutValue);
		FPlatformMisc::DeleteStoredValue(StoreId, SectionName, InKey);
	}
}

FEditorSessionSummarySender::FEditorSessionSummarySender(IAnalyticsProvider& InAnalyticsProvider, const FString& InSenderName, const int32 InCurrentSessionProcessId)
	: HeartbeatTimeElapsed(0.0f)
	, AnalyticsProvider(InAnalyticsProvider)
	, Sender(InSenderName)
	, CurrentSessionProcessId(InCurrentSessionProcessId)
{
}

FEditorSessionSummarySender::~FEditorSessionSummarySender()
{
}

void FEditorSessionSummarySender::Tick(float DeltaTime)
{
	HeartbeatTimeElapsed += DeltaTime;

	if (HeartbeatTimeElapsed > EditorSessionSenderDefs::HeartbeatPeriodSeconds)
	{
		HeartbeatTimeElapsed = 0.0f;

		SendStoredSessions();
	}
}

void FEditorSessionSummarySender::Shutdown()
{
	SendStoredSessions(/*bForceSendCurrentSession*/true);
}

void FEditorSessionSummarySender::SetCurrentSessionExitCode(const int32 InCurrentSessionProcessId, const int32 InExitCode)
{
	check(CurrentSessionProcessId == InCurrentSessionProcessId);
	CurrentSessionExitCode = InExitCode;
}

bool FEditorSessionSummarySender::FindCurrentSession(FEditorAnalyticsSession& OutSession) const
{
	if (FPlatformProcess::IsApplicationRunning(CurrentSessionProcessId))
	{
		// still running, can't be abnormal termination
		return false;
	}

	bool bFound = false;

	if (FEditorAnalyticsSession::Lock(FTimespan::FromMilliseconds(100)))
	{
		TArray<FEditorAnalyticsSession> ExistingSessions;
		FEditorAnalyticsSession::LoadAllStoredSessions(ExistingSessions);

		const int32 ProcessID = CurrentSessionProcessId;
		FEditorAnalyticsSession* CurrentSession = ExistingSessions.FindByPredicate(
			[ProcessID](const FEditorAnalyticsSession& Session)
			{
				return Session.PlatformProcessID == ProcessID;
			});

		if (CurrentSession != nullptr)
		{
			OutSession = *CurrentSession;
			bFound = true;
		}

		FEditorAnalyticsSession::Unlock();
	}

	return bFound;
}

void FEditorSessionSummarySender::SendStoredSessions(const bool bForceSendCurrentSession) const
{
	TArray<FEditorAnalyticsSession> SessionsToReport;

	if (FEditorAnalyticsSession::Lock(FTimespan::FromMilliseconds(100)))
	{
		// Get list of sessions in storage
		TArray<FEditorAnalyticsSession> ExistingSessions;
		FEditorAnalyticsSession::LoadAllStoredSessions(ExistingSessions);

		TArray<FEditorAnalyticsSession> SessionsToDelete;

		// Check each stored session to see if they should be sent or not 
		for (FEditorAnalyticsSession& Session : ExistingSessions)
		{
			const bool bForceSendSession = bForceSendCurrentSession && (Session.PlatformProcessID == CurrentSessionProcessId);
			if (!bForceSendSession && FPlatformProcess::IsApplicationRunning(Session.PlatformProcessID))
			{
				// Skip processes that are still running
				continue;
			}

			const FTimespan SessionAge = FDateTime::UtcNow() - Session.Timestamp;
			if (SessionAge < EditorSessionSenderDefs::SessionExpiration)
			{
				SessionsToReport.Add(Session);
			}
			else // Session is expired (and will not be sent)
			{
				// Hack 4.24.3: Normally, the extra keys are deleted once the summary event is sent, but this session summary will not be sent. Delete the extra keys now to avoid accumulating.
				EditorSessionSenderUtil_4_24_3::DeleteExtraSessionKeys(Session.SessionId);
			}
			
			SessionsToDelete.Add(Session);
		}

		for (const FEditorAnalyticsSession& ToDelete : SessionsToDelete)
		{
			ToDelete.Delete();
			ExistingSessions.RemoveAll([&ToDelete](const FEditorAnalyticsSession& Session)
				{
					return Session.SessionId == ToDelete.SessionId;
				});
		}

		TArray<FString> SessionIDs;
		SessionIDs.Reserve(ExistingSessions.Num());
		Algo::Transform(ExistingSessions, SessionIDs, &FEditorAnalyticsSession::SessionId);

		FEditorAnalyticsSession::SaveStoredSessionIDs(SessionIDs);

		FEditorAnalyticsSession::Unlock();
	}

	for (const FEditorAnalyticsSession& Session : SessionsToReport)
	{
		SendSessionSummaryEvent(Session);
	}
}

void FEditorSessionSummarySender::SendSessionSummaryEvent(const FEditorAnalyticsSession& Session) const
{
	FGuid SessionId;
	FString SessionIdString = Session.SessionId;
	if (FGuid::Parse(SessionIdString, SessionId))
	{
		// convert session guid to one with braces for sending to analytics
		SessionIdString = SessionId.ToString(EGuidFormats::DigitsWithHyphensInBraces);
	}

	FString ShutdownTypeString = Session.bCrashed ? EditorSessionSenderDefs::CrashSessionToken :
		(Session.bWasEverDebugger ? EditorSessionSenderDefs::DebuggerSessionToken :
		(Session.bIsTerminating ? EditorSessionSenderDefs::TerminatedSessionToken :
		(Session.bWasShutdown ? EditorSessionSenderDefs::ShutdownSessionToken : EditorSessionSenderDefs::AbnormalSessionToken)));

	FString PluginsString = FString::Join(Session.Plugins, TEXT(","));

	TArray<FAnalyticsEventAttribute> AnalyticsAttributes;
	AnalyticsAttributes.Emplace(TEXT("ProjectName"), Session.ProjectName);
	AnalyticsAttributes.Emplace(TEXT("ProjectID"), Session.ProjectID);
	AnalyticsAttributes.Emplace(TEXT("ProjectDescription"), Session.ProjectDescription);
	AnalyticsAttributes.Emplace(TEXT("ProjectVersion"), Session.ProjectVersion);
	AnalyticsAttributes.Emplace(TEXT("Platform"), FPlatformProperties::PlatformName());
	AnalyticsAttributes.Emplace(TEXT("SessionId"), SessionIdString);
	AnalyticsAttributes.Emplace(TEXT("EngineVersion"), Session.EngineVersion);
	AnalyticsAttributes.Emplace(TEXT("ShutdownType"), ShutdownTypeString);
	AnalyticsAttributes.Emplace(TEXT("StartupTimestamp"), Session.StartupTimestamp.ToIso8601());
	AnalyticsAttributes.Emplace(TEXT("Timestamp"), Session.Timestamp.ToIso8601());
	AnalyticsAttributes.Emplace(TEXT("SessionDuration"), Session.SessionDuration);
	AnalyticsAttributes.Emplace(TEXT("1MinIdle"), Session.Idle1Min);
	AnalyticsAttributes.Emplace(TEXT("5MinIdle"), Session.Idle5Min);
	AnalyticsAttributes.Emplace(TEXT("30MinIdle"), Session.Idle30Min);
	AnalyticsAttributes.Emplace(TEXT("CurrentUserActivity"), Session.CurrentUserActivity);
	AnalyticsAttributes.Emplace(TEXT("AverageFPS"), Session.AverageFPS);
	AnalyticsAttributes.Emplace(TEXT("Plugins"), PluginsString);
	AnalyticsAttributes.Emplace(TEXT("DesktopGPUAdapter"), Session.DesktopGPUAdapter);
	AnalyticsAttributes.Emplace(TEXT("RenderingGPUAdapter"), Session.RenderingGPUAdapter);
	AnalyticsAttributes.Emplace(TEXT("GPUVendorID"), Session.GPUVendorID);
	AnalyticsAttributes.Emplace(TEXT("GPUDeviceID"), Session.GPUDeviceID);
	AnalyticsAttributes.Emplace(TEXT("GRHIDeviceRevision"), Session.GRHIDeviceRevision);
	AnalyticsAttributes.Emplace(TEXT("GRHIAdapterInternalDriverVersion"), Session.GRHIAdapterInternalDriverVersion);
	AnalyticsAttributes.Emplace(TEXT("GRHIAdapterUserDriverVersion"), Session.GRHIAdapterUserDriverVersion);
	AnalyticsAttributes.Emplace(TEXT("TotalPhysicalRAM"), Session.TotalPhysicalRAM);
	AnalyticsAttributes.Emplace(TEXT("CPUPhysicalCores"), Session.CPUPhysicalCores);
	AnalyticsAttributes.Emplace(TEXT("CPULogicalCores"), Session.CPULogicalCores);
	AnalyticsAttributes.Emplace(TEXT("CPUVendor"), Session.CPUVendor);
	AnalyticsAttributes.Emplace(TEXT("CPUBrand"), Session.CPUBrand);
	AnalyticsAttributes.Emplace(TEXT("OSMajor"), Session.OSMajor);
	AnalyticsAttributes.Emplace(TEXT("OSMinor"), Session.OSMinor);
	AnalyticsAttributes.Emplace(TEXT("OSVersion"), Session.OSVersion);
	AnalyticsAttributes.Emplace(TEXT("Is64BitOS"), Session.bIs64BitOS);
	AnalyticsAttributes.Emplace(TEXT("GPUCrash"), Session.bGPUCrashed);
	AnalyticsAttributes.Emplace(TEXT("WasDebugged"), Session.bWasEverDebugger);
	AnalyticsAttributes.Emplace(TEXT("IsVanilla"), Session.bIsVanilla);
	AnalyticsAttributes.Emplace(TEXT("WasShutdown"), Session.bWasShutdown);
	AnalyticsAttributes.Emplace(TEXT("IsInPIE"), Session.bIsInPIE);
	AnalyticsAttributes.Emplace(TEXT("IsInEnterprise"), Session.bIsInEnterprise);
	AnalyticsAttributes.Emplace(TEXT("IsInVRMode"), Session.bIsInVRMode);
	AnalyticsAttributes.Emplace(TEXT("SentFrom"), Sender);

	// was this sent from some other process than itself or the out-of-process monitor for that run?
	AnalyticsAttributes.Emplace(TEXT("DelayedSend"), Session.PlatformProcessID != CurrentSessionProcessId);

	if (Session.PlatformProcessID == CurrentSessionProcessId && CurrentSessionExitCode.IsSet())
	{
		AnalyticsAttributes.Emplace(TEXT("ExitCode"), CurrentSessionExitCode.GetValue());
	}

	// Hack for 4.24.3: Downcast to IAnalyticsProviderET. In 4.24, FEditorSessionSummarySender is only instantiated by the Editor or CrashReportClientEditor and in this context, the Provider is an IAnalyticsProviderET.
	IAnalyticsProviderET& ProviderET = static_cast<IAnalyticsProviderET&>(AnalyticsProvider);
	if (ProviderET.GetAppID().StartsWith(TEXT("CrashReporter"))) // Detect if this is called within the crash report client vs Editor. CrashReporter AppID is set in CrashReportAnalyticsConfiguration.cpp as CrashReporter.Release or CrashReporter.Dev
	{
		// Extract the information from the session -> Kept the public header files untouched for 4.24.3 (The member were added to FEditorAnalyticsSession in 4.25)
		FString AppId;
		FString AppVersion;
		FString UserId;
		FEditorAnalyticsSession::Lock();
		EditorSessionSenderUtil_4_24_3::ReadAndDeleteExtraSessionKey(Session.SessionId, EditorSessionSenderUtil_4_24_3::AppIdStoreKey, AppId);
		EditorSessionSenderUtil_4_24_3::ReadAndDeleteExtraSessionKey(Session.SessionId, EditorSessionSenderUtil_4_24_3::AppVersionStoreKey, AppVersion);
		EditorSessionSenderUtil_4_24_3::ReadAndDeleteExtraSessionKey(Session.SessionId, EditorSessionSenderUtil_4_24_3::UserIdStoreKey, UserId);
		FEditorAnalyticsSession::Unlock();

		FString OldSessionId = ProviderET.GetSessionID();
		FString OldAppId = ProviderET.GetAppID();
		FString OldAppVersion = ProviderET.GetAppVersion();
		FString OldUserId = ProviderET.GetUserID();

		// Impersonate the Editor sending the summary. Since it in CrashReporter, its unlikely that another thread is going to send telemetry event(s) at the same time to interfere.
		ProviderET.SetSessionID(CopyTemp(SessionIdString)); // This also flushes the current events.
		ProviderET.SetAppID(CopyTemp(AppId));
		ProviderET.SetAppVersion(CopyTemp(AppVersion));
		ProviderET.SetUserID(CopyTemp(UserId));

		// Send the event.
		ProviderET.RecordEvent(TEXT("SessionSummary"), AnalyticsAttributes);

		// Restore the provider to its original config. (This also flushes the events as side effects)
		ProviderET.SetSessionID(MoveTemp(OldSessionId));
		ProviderET.SetAppID(MoveTemp(OldAppId));
		ProviderET.SetAppVersion(MoveTemp(OldAppVersion));
		ProviderET.SetUserID(MoveTemp(OldUserId));
	}
	else
	{
		// Send the event.
		AnalyticsProvider.RecordEvent(TEXT("SessionSummary"), AnalyticsAttributes);

		// Just in case the extra keys were added.
		FEditorAnalyticsSession::Lock();
		EditorSessionSenderUtil_4_24_3::DeleteExtraSessionKeys(Session.SessionId);
		FEditorAnalyticsSession::Unlock();
	}

	UE_LOG(LogEditorSessionSummary, Log, TEXT("EditorSessionSummary sent report. Type=%s, SessionId=%s"), *ShutdownTypeString, *SessionIdString);
}
