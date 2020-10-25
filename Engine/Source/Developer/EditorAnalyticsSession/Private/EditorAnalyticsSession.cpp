// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorAnalyticsSession.h"
#include "Modules/ModuleManager.h"
#include "Internationalization/Regex.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

IMPLEMENT_MODULE(FEditorAnalyticsSessionModule, EditorAnalyticsSession);

namespace EditorAnalyticsDefs
{
	static const FString FalseValueString(TEXT("0"));
	static const FString TrueValueString(TEXT("1"));

	static const FString DefaultUserActivity(TEXT("Unknown"));
	static const FString UnknownProjectValueString(TEXT("UnknownProject"));

	static const FString UnknownAppIdString(TEXT("UnknownAppId"));
	static const FString UnknownAppVersionString(TEXT("UnknownAppVersion"));
	static const FString UnknownUserIdString(TEXT("UnknownUserID"));

	// The storage location is used to version the different data format. This is to prevent one version of the Editor/CRC to send sessions produced by another incompatible version.
	//   Version 1_0 : Used from creation up to 4.25.0 release (included).
	//   Version 1_1 : Changed for 4.25.1. To avoid public API changes, TotalUserInactivitySeconds was repurposed to contain the SessionDuration read from FPlatformTime::Seconds() to detect cases where the user system date time is unreliable.
	static const FString StoreId(TEXT("Epic Games"));
	static const FString SessionSummarySection(TEXT("Unreal Engine/Session Summary/1_1"));
	static const FString GlobalLockName(TEXT("UE4_SessionSummary_Lock"));
	static const FString SessionListStoreKey(TEXT("SessionList"));

	// capture context
	static const FString AppIdStoreKey(TEXT("AppId"));
	static const FString AppVersionStoreKey(TEXT("AppVersion"));
	static const FString UserIdStoreKey(TEXT("UserId"));

	// general values
	static const FString ProjectNameStoreKey(TEXT("ProjectName"));
	static const FString ProjectIDStoreKey(TEXT("ProjectID"));
	static const FString ProjectDescriptionStoreKey(TEXT("ProjectDescription"));
	static const FString ProjectVersionStoreKey(TEXT("ProjectVersion"));
	static const FString EngineVersionStoreKey(TEXT("EngineVersion"));
	static const FString PlatformProcessIDStoreKey(TEXT("PlatformProcessID"));
	static const FString MonitorProcessIDStoreKey(TEXT("MonitorProcessID"));
	static const FString ExitCodeStoreKey(TEXT("ExitCode"));
	static const FString MonitorExceptCodeStoreKey(TEXT("MonitorExceptCode"));

	// timestamps
	static const FString StartupTimestampStoreKey(TEXT("StartupTimestamp"));
	static const FString TimestampStoreKey(TEXT("Timestamp"));
	static const FString SessionDurationStoreKey(TEXT("SessionDuration"));
	static const FString Idle1MinStoreKey(TEXT("Idle1Min"));
	static const FString Idle5MinStoreKey(TEXT("Idle5Min"));
	static const FString Idle30MinStoreKey(TEXT("Idle30Min"));
	static const FString TotalUserInactivitySecondsStoreKey(TEXT("TotalUserInactivitySecs"));
	static const FString TotalEditorInactivitySecondsStoreKey(TEXT("TotalEditorInactivitySecs"));
	static const FString CurrentUserActivityStoreKey(TEXT("CurrentUserActivity"));
	static const FString PluginsStoreKey(TEXT("Plugins"));
	static const FString AverageFPSStoreKey(TEXT("AverageFPS"));

	// GPU details
	static const FString DesktopGPUAdapterStoreKey(TEXT("DesktopGPUAdapter"));
	static const FString RenderingGPUAdapterStoreKey(TEXT("RenderingGPUAdapter"));
	static const FString GPUVendorIDStoreKey(TEXT("GPUVendorID"));
	static const FString GPUDeviceIDStoreKey(TEXT("GPUDeviceID"));
	static const FString GRHIDeviceRevisionStoreKey(TEXT("GRHIDeviceRevision"));
	static const FString GRHIAdapterInternalDriverVersionStoreKey(TEXT("GRHIAdapterInternalDriverVersion"));
	static const FString GRHIAdapterUserDriverVersionStoreKey(TEXT("GRHIAdapterUserDriverVersion"));

	// CPU details
	static const FString TotalPhysicalRAMStoreKey(TEXT("TotalPhysicalRAM"));
	static const FString CPUPhysicalCoresStoreKey(TEXT("CPUPhysicalCores"));
	static const FString CPULogicalCoresStoreKey(TEXT("CPULogicalCores"));
	static const FString CPUVendorStoreKey(TEXT("CPUVendor"));
	static const FString CPUBrandStoreKey(TEXT("CPUBrand"));

	// OS details
	static const FString OSMajorStoreKey(TEXT("OSMajor"));
	static const FString OSMinorStoreKey(TEXT("OSMinor"));
	static const FString OSVersionStoreKey(TEXT("OSVersion"));
	static const FString bIs64BitOSStoreKey(TEXT("bIs64BitOS"));

	// boolean flags
	static const FString IsCrashStoreKey(TEXT("IsCrash"));
	static const FString IsGPUCrashStoreKey(TEXT("IsGPUCrash"));
	static const FString IsDebuggerStoreKey(TEXT("IsDebugger"));
	static const FString WasDebuggerStoreKey(TEXT("WasEverDebugger"));
	static const FString IsVanillaStoreKey(TEXT("IsVanilla"));
	static const FString IsTerminatingKey(TEXT("Terminating"));
	static const FString WasShutdownStoreKey(TEXT("WasShutdown"));
	static const FString IsInPIEStoreKey(TEXT("IsInPIE"));
	static const FString IsInEnterpriseStoreKey(TEXT("IsInEnterprise"));
	static const FString IsInVRModeStoreKey(TEXT("IsInVRMode"));
	static const FString IsLowDriveSpaceStoreKey(TEXT("IsLowDriveSpace"));
}

// Utilities for writing to stored values
namespace EditorAnalyticsUtils
{
	static FString TimestampToString(FDateTime InTimestamp)
	{
		return LexToString(InTimestamp.ToUnixTimestamp());
	}

	static FDateTime StringToTimestamp(FString InString)
	{
		int64 TimestampUnix;
		if (LexTryParseString(TimestampUnix, *InString))
		{
			return FDateTime::FromUnixTimestamp(TimestampUnix);
		}
		return FDateTime::MinValue();
	}

	static FString BoolToStoredString(bool bInValue)
	{
		return bInValue ? EditorAnalyticsDefs::TrueValueString : EditorAnalyticsDefs::FalseValueString;
	}

	static bool GetStoredBool(const FString& SectionName, const FString& StoredKey)
	{
		FString StoredString = EditorAnalyticsDefs::FalseValueString;
		FPlatformMisc::GetStoredValue(EditorAnalyticsDefs::StoreId, SectionName, StoredKey, StoredString);

		return StoredString == EditorAnalyticsDefs::TrueValueString;
	}

	static FString GetSessionStorageLocation(const FString& SessionID)
	{
		return EditorAnalyticsDefs::SessionSummarySection + TEXT("/") + SessionID;
	}

	static FString GetSessionEventLogDir()
	{
		return FString::Printf(TEXT("%sAnalytics"), FPlatformProcess::ApplicationSettingsDir());
	}

	static void LogSessionEvent(FEditorAnalyticsSession& Session, FEditorAnalyticsSession::EEventType InEventType, const FDateTime& InTimestamp)
	{
		// The logger uses the robustness of the file system to log events. It creates a file (a directory) for each event and encodes the event payload in the file name.
		// This doesn't require any fancy synchronization or complicated concurrent file IO implementation. Since the number of events is low (0 to 5 per session), that's
		// a straight forward working solution. The files are deleted when the session is deleted. Also avoid memory allocation, this can be called when the heap is corrupted.
		TCHAR TimestampStr[256];
		FCString::Sprintf(TimestampStr, TFormatSpecifier<decltype(InTimestamp.ToUnixTimestamp())>::GetFormatSpecifier(), InTimestamp.ToUnixTimestamp());

		TCHAR Pathname[512];
		FCString::Sprintf(Pathname, TEXT("%s/%s_%d_%d_%d_%d_%d_%s"),
			*EditorAnalyticsUtils::GetSessionEventLogDir(),
			*Session.SessionId,
			static_cast<int32>(InEventType),
			FPlatformAtomics::AtomicRead(&Session.Idle1Min),
			FPlatformAtomics::AtomicRead(&Session.Idle5Min),
			FPlatformAtomics::AtomicRead(&Session.Idle30Min),
			FPlatformAtomics::AtomicRead(&Session.TotalUserInactivitySeconds), // To avoid changing public API in 4.25.1, TotalUserInactivitySeconds was repurposed to holds the session duration. Should be fixed in 4.26.
			TimestampStr);

		IFileManager::Get().MakeDirectory(Pathname, /*Tree*/true);
	}

	/** Analyze the events logged with LogEvent() and update the session fields to reflect the last state of the session. */
	static void UpdateSessionFromLogAnalysis(FEditorAnalyticsSession& Session)
	{
		// Read and aggregate the log events. The event data is encoded in the directory names created by the logger
		FRegexPattern Pattern(TEXT(R"((^[a-fA-F0-9-]+)_([0-9]+)_([0-9]+)_([0-9]+)_([0-9]+)_([0-9]+)_([0-9]+))")); // Need help with regex? Try https://regex101.com/
		IFileManager::Get().IterateDirectoryRecursively(*EditorAnalyticsUtils::GetSessionEventLogDir(), [&Session, &Pattern](const TCHAR* Pathname, bool bIsDir)
		{
			if (bIsDir)
			{
				FRegexMatcher Matcher(Pattern, FPaths::GetCleanFilename(Pathname));
				if (Matcher.FindNext() && Matcher.GetCaptureGroup(1) == Session.SessionId)
				{
					FEditorAnalyticsSession::EEventType EventType = static_cast<FEditorAnalyticsSession::EEventType>(FCString::Atoi(*Matcher.GetCaptureGroup(2))); // Event
					switch (EventType)
					{
						case FEditorAnalyticsSession::EEventType::Crashed:     Session.bCrashed = true;       break;
						case FEditorAnalyticsSession::EEventType::GpuCrashed : Session.bGPUCrashed = true;    break;
						case FEditorAnalyticsSession::EEventType::Terminated:  Session.bIsTerminating = true; break;
						case FEditorAnalyticsSession::EEventType::Shutdown:    Session.bWasShutdown = true;   break;
						default: break;
					}

					int32 ParsedTime = FCString::Atoi(*Matcher.GetCaptureGroup(3)); // Idle1Min.
					if (ParsedTime > Session.Idle1Min)
					{
						Session.Idle1Min = ParsedTime; // No concurrency expected when reloading (no need for atomic compare exchange)
					}

					ParsedTime = FCString::Atoi(*Matcher.GetCaptureGroup(4)); // Idle5Min.
					if (ParsedTime > Session.Idle5Min)
					{
						Session.Idle5Min = ParsedTime;
					}

					ParsedTime = FCString::Atoi(*Matcher.GetCaptureGroup(5)); // Idle30Min.
					if (ParsedTime > Session.Idle30Min)
					{
						Session.Idle30Min = ParsedTime;
					}

					ParsedTime = FCString::Atoi(*Matcher.GetCaptureGroup(6)); // SessionDuration. (To avoid breaking public API, TotalUserInactivitySeconds was repurposed to hold session duration in 4.25.x branch)
					if (ParsedTime > Session.TotalUserInactivitySeconds)
					{
						Session.TotalUserInactivitySeconds = ParsedTime;
					}

					FDateTime ParsedTimestamp = FDateTime::FromUnixTimestamp(FCString::Atoi64(*Matcher.GetCaptureGroup(6))); // Unix timestamp (UTC)
					if (ParsedTimestamp > Session.Timestamp)
					{
						Session.Timestamp = ParsedTimestamp;
					}
				}
			}
			return true;
		});
	}

	static void DeleteLogEvents(const FEditorAnalyticsSession& Session)
	{
		// Gather the list of files
		TArray<FString> SessionEventPaths;
		IFileManager::Get().IterateDirectoryRecursively(*EditorAnalyticsUtils::GetSessionEventLogDir(), [&Session, &SessionEventPaths](const TCHAR* Pathname, bool bIsDir)
		{
			if (bIsDir)
			{
				if (FPaths::GetCleanFilename(Pathname).StartsWith(Session.SessionId))
				{
					SessionEventPaths.Emplace(Pathname);
				}
			}
			return true; // Continue
		});

		// Delete the session files.
		for (const FString& EventPathname : SessionEventPaths)
		{
			IFileManager::Get().DeleteDirectory(*EventPathname, /*RequiredExist*/false, /*Tree*/false);
		}
	}

// Utility macros to make it easier to check that all fields are being written to.
#define GET_STORED_STRING(FieldName) FPlatformMisc::GetStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs:: FieldName ## StoreKey, Session.FieldName)
#define GET_STORED_INT(FieldName) \
	{ \
		FString FieldName ## Temp; \
		FPlatformMisc::GetStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs:: FieldName ## StoreKey, FieldName ## Temp); \
		Session.FieldName = FCString::Atoi(*FieldName ## Temp); \
	}

	static void LoadInternal(FEditorAnalyticsSession& Session, const FString& InSessionId)
	{
		Session.SessionId = InSessionId;

		FString SectionName = EditorAnalyticsUtils::GetSessionStorageLocation(Session.SessionId);

		GET_STORED_STRING(AppId);
		GET_STORED_STRING(AppVersion);
		GET_STORED_STRING(UserId);

		GET_STORED_STRING(ProjectName);
		GET_STORED_STRING(ProjectID);
		GET_STORED_STRING(ProjectDescription);
		GET_STORED_STRING(ProjectVersion);
		GET_STORED_STRING(EngineVersion);
		GET_STORED_INT(PlatformProcessID);
		GET_STORED_INT(MonitorProcessID);

		{
			FString ExitCodeString;
			if (FPlatformMisc::GetStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::ExitCodeStoreKey, ExitCodeString))
			{
				Session.ExitCode.Emplace(FCString::Atoi(*ExitCodeString));
			}
		}

		{
			FString MonitorExceptCodeString;
			if (FPlatformMisc::GetStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::MonitorExceptCodeStoreKey, MonitorExceptCodeString))
			{
				Session.MonitorExceptCode.Emplace(FCString::Atoi(*MonitorExceptCodeString));
			}
		}

		// scope is just to isolate the temporary value
		{
			FString StartupTimestampString;
			FPlatformMisc::GetStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::StartupTimestampStoreKey, StartupTimestampString);
			Session.StartupTimestamp = EditorAnalyticsUtils::StringToTimestamp(StartupTimestampString);
		}

		{
			FString TimestampString;
			FPlatformMisc::GetStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::TimestampStoreKey, TimestampString);
			Session.Timestamp = EditorAnalyticsUtils::StringToTimestamp(TimestampString);
		}

		GET_STORED_INT(Idle1Min);
		GET_STORED_INT(Idle5Min);
		GET_STORED_INT(Idle30Min);
		GET_STORED_INT(TotalUserInactivitySeconds);
		GET_STORED_INT(TotalEditorInactivitySeconds);

		GET_STORED_STRING(CurrentUserActivity);

		{
			FString PluginsString;
			FPlatformMisc::GetStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::PluginsStoreKey, PluginsString);
			PluginsString.ParseIntoArray(Session.Plugins, TEXT(","));
		}

		{
			FString AverageFPSString;
			FPlatformMisc::GetStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::AverageFPSStoreKey, AverageFPSString);
			Session.AverageFPS = FCString::Atof(*AverageFPSString);
		}

		GET_STORED_STRING(DesktopGPUAdapter);
		GET_STORED_STRING(RenderingGPUAdapter);

		GET_STORED_INT(GPUVendorID);
		GET_STORED_INT(GPUDeviceID);
		GET_STORED_INT(GRHIDeviceRevision);

		GET_STORED_STRING(GRHIAdapterInternalDriverVersion);
		GET_STORED_STRING(GRHIAdapterUserDriverVersion);

		{
			FString TotalPhysicalRAMString;
			FPlatformMisc::GetStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::TotalPhysicalRAMStoreKey, TotalPhysicalRAMString);
			Session.TotalPhysicalRAM = FCString::Atoi64(*TotalPhysicalRAMString);
		}

		GET_STORED_INT(CPUPhysicalCores);
		GET_STORED_INT(CPULogicalCores);

		GET_STORED_STRING(CPUVendor);
		GET_STORED_STRING(CPUBrand);

		GET_STORED_STRING(OSMajor);
		GET_STORED_STRING(OSMinor);
		GET_STORED_STRING(OSVersion);

		Session.bIs64BitOS = EditorAnalyticsUtils::GetStoredBool(SectionName, EditorAnalyticsDefs::bIs64BitOSStoreKey);
		Session.bCrashed = EditorAnalyticsUtils::GetStoredBool(SectionName, EditorAnalyticsDefs::IsCrashStoreKey);
		Session.bGPUCrashed = EditorAnalyticsUtils::GetStoredBool(SectionName, EditorAnalyticsDefs::IsGPUCrashStoreKey);
		Session.bIsDebugger = EditorAnalyticsUtils::GetStoredBool(SectionName, EditorAnalyticsDefs::IsDebuggerStoreKey);
		Session.bWasEverDebugger = EditorAnalyticsUtils::GetStoredBool(SectionName, EditorAnalyticsDefs::WasDebuggerStoreKey);
		Session.bIsVanilla = EditorAnalyticsUtils::GetStoredBool(SectionName, EditorAnalyticsDefs::IsVanillaStoreKey);
		Session.bIsTerminating = EditorAnalyticsUtils::GetStoredBool(SectionName, EditorAnalyticsDefs::IsTerminatingKey);
		Session.bWasShutdown = EditorAnalyticsUtils::GetStoredBool(SectionName, EditorAnalyticsDefs::WasShutdownStoreKey);
		Session.bIsInPIE = EditorAnalyticsUtils::GetStoredBool(SectionName, EditorAnalyticsDefs::IsInPIEStoreKey);
		Session.bIsInVRMode = EditorAnalyticsUtils::GetStoredBool(SectionName, EditorAnalyticsDefs::IsInVRModeStoreKey);
		Session.bIsInEnterprise = EditorAnalyticsUtils::GetStoredBool(SectionName, EditorAnalyticsDefs::IsInEnterpriseStoreKey);
		Session.bIsLowDriveSpace = EditorAnalyticsUtils::GetStoredBool(SectionName, EditorAnalyticsDefs::IsLowDriveSpaceStoreKey);

		// Analyze the logged events and update corresponding fields in the session.
		UpdateSessionFromLogAnalysis(Session);
	}

#undef GET_STORED_INT
#undef GET_STORED_STRING

	static TArray<FString> GetSessionList()
	{
		FString SessionListString;
		FPlatformMisc::GetStoredValue(EditorAnalyticsDefs::StoreId, EditorAnalyticsDefs::SessionSummarySection, EditorAnalyticsDefs::SessionListStoreKey, SessionListString);

		TArray<FString> SessionIDs;
		SessionListString.ParseIntoArray(SessionIDs, TEXT(","));

		return MoveTemp(SessionIDs);
	}
}

FSystemWideCriticalSection* FEditorAnalyticsSession::StoredValuesLock = nullptr;

FEditorAnalyticsSession::FEditorAnalyticsSession()
{
	AppId = EditorAnalyticsDefs::UnknownAppIdString;
	AppVersion = EditorAnalyticsDefs::UnknownAppVersionString;
	UserId = EditorAnalyticsDefs::UnknownUserIdString;

	ProjectName = EditorAnalyticsDefs::UnknownProjectValueString;
	PlatformProcessID = 0;
	MonitorProcessID = 0;
	StartupTimestamp = FDateTime::MinValue();
	Timestamp = FDateTime::MinValue();
	CurrentUserActivity = EditorAnalyticsDefs::DefaultUserActivity;
	AverageFPS = 0;
	GPUVendorID = 0;
	GPUDeviceID = 0;
	GRHIDeviceRevision = 0;
	TotalPhysicalRAM = 0;
	CPUPhysicalCores = 0;
	CPULogicalCores = 0;

	bIs64BitOS = false;
	bCrashed = false;
	bGPUCrashed = false;
	bIsDebugger = false;
	bWasEverDebugger = false;
	bIsVanilla = false;
	bIsTerminating = false;
	bWasShutdown = false;
	bIsInPIE = false;
	bIsInEnterprise = false;
	bIsInVRMode = false;
	bAlreadySaved = false;
	bIsLowDriveSpace = false;
}

bool FEditorAnalyticsSession::Lock(FTimespan Timeout)
{
	if (!ensure(!IsLocked()))
	{
		return true;
	}
	
	StoredValuesLock = new FSystemWideCriticalSection(EditorAnalyticsDefs::GlobalLockName, Timeout);

	if (!IsLocked())
	{
		delete StoredValuesLock;
		StoredValuesLock = nullptr;

		return false;
	}

	return true;
}

void FEditorAnalyticsSession::Unlock()
{
	if (!ensure(IsLocked()))
	{
		return;
	}

	delete StoredValuesLock;
	StoredValuesLock = nullptr;
}

bool FEditorAnalyticsSession::IsLocked()
{
	return StoredValuesLock != nullptr && StoredValuesLock->IsValid();
}

bool FEditorAnalyticsSession::Save()
{
	if (!ensure(IsLocked()))
	{
		return false;
	}

	const FString StorageLocation = EditorAnalyticsUtils::GetSessionStorageLocation(SessionId);

	if (!bAlreadySaved)
	{
		const FString PluginsString = FString::Join(Plugins, TEXT(","));

		TMap<FString, FString> KeyValues = {
			{EditorAnalyticsDefs::EngineVersionStoreKey,       EngineVersion},
			{EditorAnalyticsDefs::PlatformProcessIDStoreKey,   FString::FromInt(PlatformProcessID)},
			{EditorAnalyticsDefs::MonitorProcessIDStoreKey,    FString::FromInt(MonitorProcessID)},
			{EditorAnalyticsDefs::DesktopGPUAdapterStoreKey,   DesktopGPUAdapter},
			{EditorAnalyticsDefs::RenderingGPUAdapterStoreKey, RenderingGPUAdapter},
			{EditorAnalyticsDefs::GPUVendorIDStoreKey,         FString::FromInt(GPUVendorID)},
			{EditorAnalyticsDefs::GPUDeviceIDStoreKey,         FString::FromInt(GPUDeviceID)},
			{EditorAnalyticsDefs::GRHIDeviceRevisionStoreKey,  FString::FromInt(GRHIDeviceRevision)},
			{EditorAnalyticsDefs::GRHIAdapterInternalDriverVersionStoreKey, GRHIAdapterUserDriverVersion},
			{EditorAnalyticsDefs::GRHIAdapterUserDriverVersionStoreKey,     GRHIAdapterUserDriverVersion},
			{EditorAnalyticsDefs::TotalPhysicalRAMStoreKey, FString::Printf(TEXT("%llu"), TotalPhysicalRAM)},
			{EditorAnalyticsDefs::CPUPhysicalCoresStoreKey, FString::FromInt(CPUPhysicalCores)},
			{EditorAnalyticsDefs::CPULogicalCoresStoreKey,  FString::FromInt(CPULogicalCores)},
			{EditorAnalyticsDefs::CPUVendorStoreKey,        CPUVendor},
			{EditorAnalyticsDefs::CPUBrandStoreKey,         CPUBrand},
			{EditorAnalyticsDefs::StartupTimestampStoreKey, EditorAnalyticsUtils::TimestampToString(StartupTimestamp)},
			{EditorAnalyticsDefs::OSMajorStoreKey,    OSMajor},
			{EditorAnalyticsDefs::OSMinorStoreKey,    OSMinor},
			{EditorAnalyticsDefs::OSVersionStoreKey,  OSVersion},
			{EditorAnalyticsDefs::bIs64BitOSStoreKey, EditorAnalyticsUtils::BoolToStoredString(bIs64BitOS)},
			{EditorAnalyticsDefs::PluginsStoreKey,    PluginsString},
			{EditorAnalyticsDefs::AppIdStoreKey,      AppId},
			{EditorAnalyticsDefs::AppVersionStoreKey, AppVersion},
			{EditorAnalyticsDefs::UserIdStoreKey,     UserId},
		};

		FPlatformMisc::SetStoredValues(EditorAnalyticsDefs::StoreId, StorageLocation, KeyValues);

		bAlreadySaved = true;
	}

	{
		TMap<FString, FString> KeyValues = {
			{EditorAnalyticsDefs::ProjectNameStoreKey, ProjectName},
			{EditorAnalyticsDefs::ProjectIDStoreKey,   ProjectID},
			{EditorAnalyticsDefs::ProjectDescriptionStoreKey,  ProjectDescription},
			{EditorAnalyticsDefs::ProjectVersionStoreKey,  ProjectVersion},
			{EditorAnalyticsDefs::TimestampStoreKey,       EditorAnalyticsUtils::TimestampToString(Timestamp)},
			{EditorAnalyticsDefs::Idle1MinStoreKey,  FString::FromInt(Idle1Min)},
			{EditorAnalyticsDefs::Idle5MinStoreKey,  FString::FromInt(Idle5Min)},
			{EditorAnalyticsDefs::Idle30MinStoreKey, FString::FromInt(Idle30Min)},
			{EditorAnalyticsDefs::TotalUserInactivitySecondsStoreKey, FString::FromInt(TotalUserInactivitySeconds)},
			{EditorAnalyticsDefs::TotalEditorInactivitySecondsStoreKey, FString::FromInt(TotalEditorInactivitySeconds)},
			{EditorAnalyticsDefs::CurrentUserActivityStoreKey, CurrentUserActivity},
			{EditorAnalyticsDefs::AverageFPSStoreKey,     FString::SanitizeFloat(AverageFPS)},
			{EditorAnalyticsDefs::IsDebuggerStoreKey,     EditorAnalyticsUtils::BoolToStoredString(bIsDebugger)},
			{EditorAnalyticsDefs::WasDebuggerStoreKey,    EditorAnalyticsUtils::BoolToStoredString(bWasEverDebugger)},
			{EditorAnalyticsDefs::IsVanillaStoreKey,      EditorAnalyticsUtils::BoolToStoredString(bIsVanilla)},
			{EditorAnalyticsDefs::WasShutdownStoreKey,    EditorAnalyticsUtils::BoolToStoredString(bWasShutdown)},
			{EditorAnalyticsDefs::IsInPIEStoreKey,        EditorAnalyticsUtils::BoolToStoredString(bIsInPIE)    },
			{EditorAnalyticsDefs::IsInEnterpriseStoreKey, EditorAnalyticsUtils::BoolToStoredString(bIsInEnterprise)},
			{EditorAnalyticsDefs::IsInVRModeStoreKey,     EditorAnalyticsUtils::BoolToStoredString(bIsInVRMode)},
			{EditorAnalyticsDefs::IsLowDriveSpaceStoreKey,  EditorAnalyticsUtils::BoolToStoredString(bIsLowDriveSpace)},
		};

		if (ExitCode.IsSet())
		{
			KeyValues.Emplace(EditorAnalyticsDefs::ExitCodeStoreKey, FString::FromInt(ExitCode.GetValue()));
		}

		if (MonitorExceptCode.IsSet())
		{
			KeyValues.Emplace(EditorAnalyticsDefs::MonitorExceptCodeStoreKey, FString::FromInt(MonitorExceptCode.GetValue()));
		}

		FPlatformMisc::SetStoredValues(EditorAnalyticsDefs::StoreId, StorageLocation, KeyValues);
	}

	return true;
}

bool FEditorAnalyticsSession::Load(const FString& InSessionID)
{
	if (!ensure(IsLocked()))
	{
		return false;
	}

	EditorAnalyticsUtils::LoadInternal(*this, InSessionID);
	bAlreadySaved = false;

	return true;
}

bool FEditorAnalyticsSession::Delete() const
{
	if (!ensure(IsLocked()))
	{
		return false;
	}

	FString SectionName = EditorAnalyticsUtils::GetSessionStorageLocation(SessionId);

	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::AppIdStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::AppVersionStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::UserIdStoreKey);

	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::ProjectNameStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::ProjectIDStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::ProjectDescriptionStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::ProjectVersionStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::EngineVersionStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::PlatformProcessIDStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::MonitorProcessIDStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::ExitCodeStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::MonitorExceptCodeStoreKey);

	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::StartupTimestampStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::TimestampStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::SessionDurationStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::Idle1MinStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::Idle5MinStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::Idle30MinStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::TotalUserInactivitySecondsStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::TotalEditorInactivitySecondsStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::CurrentUserActivityStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::PluginsStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::AverageFPSStoreKey);

	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::DesktopGPUAdapterStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::RenderingGPUAdapterStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::GPUVendorIDStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::GPUDeviceIDStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::GRHIDeviceRevisionStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::GRHIAdapterInternalDriverVersionStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::GRHIAdapterUserDriverVersionStoreKey);

	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::TotalPhysicalRAMStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::CPUPhysicalCoresStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::CPULogicalCoresStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::CPUVendorStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::CPUBrandStoreKey);

	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::OSMajorStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::OSMinorStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::OSVersionStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::bIs64BitOSStoreKey);

	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::IsCrashStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::IsGPUCrashStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::IsDebuggerStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::WasDebuggerStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::IsVanillaStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::IsTerminatingKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::WasShutdownStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::IsInPIEStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::IsInEnterpriseStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::IsInVRModeStoreKey);
	FPlatformMisc::DeleteStoredValue(EditorAnalyticsDefs::StoreId, SectionName, EditorAnalyticsDefs::IsLowDriveSpaceStoreKey);

	// Delete the log files.
	EditorAnalyticsUtils::DeleteLogEvents(*this);

	return true;
}

bool FEditorAnalyticsSession::GetStoredSessionIDs(TArray<FString>& OutSessions)
{
	if (!ensure(IsLocked()))
	{
		return false;
	}

	OutSessions = EditorAnalyticsUtils::GetSessionList();
	return true;
}

bool FEditorAnalyticsSession::LoadAllStoredSessions(TArray<FEditorAnalyticsSession>& OutSessions)
{
	if (!ensure(IsLocked()))
	{
		return false;
	}

	TArray<FString> SessionIDs = EditorAnalyticsUtils::GetSessionList();

	// Retrieve all the sessions in the list from storage
	for (const FString& Id : SessionIDs)
	{
		FEditorAnalyticsSession NewSession;
		EditorAnalyticsUtils::LoadInternal(NewSession, Id);

		OutSessions.Add(MoveTemp(NewSession));
	}

	return true;
}

bool FEditorAnalyticsSession::SaveStoredSessionIDs(const TArray<FString>& InSessions)
{
	// build up a new SessionList string
	FString SessionListString;
	for (const FString& Session : InSessions)
	{
		if (!SessionListString.IsEmpty())
		{
			SessionListString.Append(TEXT(","));
		}

		SessionListString.Append(Session);
	}

	if (!ensure(IsLocked()))
	{
		return false;
	}

	FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, EditorAnalyticsDefs::SessionSummarySection, EditorAnalyticsDefs::SessionListStoreKey, SessionListString);
	return true;
}

void FEditorAnalyticsSession::LogEvent(EEventType InEventType, const FDateTime& InTimestamp)
{
	EditorAnalyticsUtils::LogSessionEvent(*this, InEventType, InTimestamp);
}

bool FEditorAnalyticsSession::FindSession(const uint32 InSessionProcessId, FEditorAnalyticsSession& OutSession)
{
	if (!ensure(IsLocked()))
	{
		return false;
	}

	TArray<FString> SessionIDs = EditorAnalyticsUtils::GetSessionList();

	// Retrieve all the sessions in the list from storage
	for (const FString& Id : SessionIDs)
	{
		FEditorAnalyticsSession Session;
		EditorAnalyticsUtils::LoadInternal(Session, Id);
		if (Session.PlatformProcessID == InSessionProcessId)
		{
			OutSession = MoveTemp(Session);
			return true;
		}
	}

	return false;
}

void FEditorAnalyticsSession::SaveExitCode(int32 InExitCode)
{
	if (!ensure(IsLocked()))
	{
		return;
	}

	ExitCode.Emplace(InExitCode);
	FString ExitCodeStr = FString::Printf(TFormatSpecifier<decltype(InExitCode)>::GetFormatSpecifier(), InExitCode);

	const FString StorageLocation = EditorAnalyticsUtils::GetSessionStorageLocation(SessionId);
	FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, StorageLocation, EditorAnalyticsDefs::ExitCodeStoreKey, ExitCodeStr);
}

void FEditorAnalyticsSession::SaveMonitorExceptCode(int32 InExceptCode)
{
	if (!ensure(IsLocked()))
	{
		return;
	}

	MonitorExceptCode.Emplace(InExceptCode);
	FString ExceptCodeStr = FString::Printf(TFormatSpecifier<decltype(InExceptCode)>::GetFormatSpecifier(), InExceptCode);

	const FString StorageLocation = EditorAnalyticsUtils::GetSessionStorageLocation(SessionId);
	FPlatformMisc::SetStoredValue(EditorAnalyticsDefs::StoreId, StorageLocation, EditorAnalyticsDefs::MonitorExceptCodeStoreKey, ExceptCodeStr);
}
