// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Misc/EnumClassFlags.h"
#include "Misc/ConfigCacheIni.h"
#include "Logging/LogMacros.h"
#include "Internationalization/Text.h"
//#include "IAnalyticsProviderET.h"

class IAnalyticsProviderET;

enum class EInstallBundleModuleInitResult : int
{
	OK,
	BuildMetaDataNotFound,
	BuildMetaDataParsingError,
	DistributionRootParseError,
	DistributionRootDownloadError,
	ManifestArchiveError,
	ManifestCreationError,
	ManifestDownloadError,
	BackgroundDownloadsIniDownloadError,
	NoInternetConnectionError,
	Count
};

inline const TCHAR* LexToString(EInstallBundleModuleInitResult Result)
{
	using UnderType = __underlying_type(EInstallBundleModuleInitResult);
	static const TCHAR* Strings[] =
	{
		TEXT("OK"),
		TEXT("BuildMetaDataNotFound"),
		TEXT("BuildMetaDataParsingError"),
		TEXT("DistributionRootParseError"),
		TEXT("DistributionRootDownloadError"),
		TEXT("ManifestArchiveError"),
		TEXT("ManifestCreationError"),
		TEXT("ManifestDownloadError"),
		TEXT("BackgroundDownloadsIniDownloadError"),
		TEXT("NoInternetConnectionError"),
	};
	static_assert(static_cast<UnderType>(EInstallBundleModuleInitResult::Count) == ARRAY_COUNT(Strings), "");

	return Strings[static_cast<UnderType>(Result)];
}

enum class EInstallBundleResult : int
{
	OK,
	FailedPrereqRequiresLatestClient,
	InstallError,
	InstallerOutOfDiskSpaceError,
	ManifestArchiveError,
	UserCancelledError,
	InitializationError,
	Count,
};

inline const TCHAR* LexToString(EInstallBundleResult Result)
{
	using UnderType = __underlying_type(EInstallBundleResult);
	static const TCHAR* Strings[] =
	{
		TEXT("OK"),
		TEXT("FailedPrereqRequiresLatestClient"),
		TEXT("InstallError"),
		TEXT("InstallerOutOfDiskSpaceError"),
		TEXT("ManifestArchiveError"),
		TEXT("UserCancelledError"),
		TEXT("InitializationError"),
	};
	static_assert(static_cast<UnderType>(EInstallBundleResult::Count) == ARRAY_COUNT(Strings), "");

	return Strings[static_cast<UnderType>(Result)];
}

enum class EInstallBundlePauseFlags : uint32
{
	None = 0,
	OnCellularNetwork = (1 << 0),
	NoInternetConnection = (1 << 1),
	UserPaused	= (1 << 2)
};
ENUM_CLASS_FLAGS(EInstallBundlePauseFlags)

inline const TCHAR* GetInstallBundlePauseReason(EInstallBundlePauseFlags Flags)
{
	// Return the most appropriate reason given the flags

	if (EnumHasAnyFlags(Flags, EInstallBundlePauseFlags::UserPaused))
		return TEXT("UserPaused");

	if (EnumHasAnyFlags(Flags, EInstallBundlePauseFlags::NoInternetConnection))
		return TEXT("NoInternetConnection");
	
	if (EnumHasAnyFlags(Flags, EInstallBundlePauseFlags::OnCellularNetwork))
		return TEXT("OnCellularNetwork");

	return TEXT("");
}

enum class EInstallBundleRequestFlags : uint32
{
	None = 0,
	CheckForCellularDataUsage = (1 << 0),
	UseBackgroundDownloads = (1 << 1),
	Defaults = UseBackgroundDownloads,
};
ENUM_CLASS_FLAGS(EInstallBundleRequestFlags)

enum class EInstallBundleStatus : int
{
	QueuedForDownload,
	Downloading,
	QueuedForInstall,
	Installing,
	QueuedForFinish,
	Finishing,
	Installed,
	Count,
};

inline const TCHAR* LexToString(EInstallBundleStatus Status)
{
	using UnderType = __underlying_type(EInstallBundleStatus);
	static const TCHAR* Strings[] =
	{
		TEXT("QueuedForDownload"),
		TEXT("Downloading"),
		TEXT("QueuedForInstall"),
		TEXT("Installing"),
		TEXT("QueuedForFinish"),
		TEXT("Finishing"),
		TEXT("Installed"),
	};

	static_assert(static_cast<UnderType>(EInstallBundleStatus::Count) == ARRAY_COUNT(Strings), "");

	return Strings[static_cast<UnderType>(Status)];
}

struct FInstallBundleDownloadProgress
{
	// Num bytes received
	uint64 BytesDownloaded = 0;
	// Num bytes written to storage (<= BytesDownloaded)
	uint64 BytesDownloadedAndWritten = 0;
	// Num bytes needed
	uint64 TotalBytesToDownload = 0;
	// Num bytes that failed to download
	uint64 TotalBytesFailedToDownload = 0;
	float PercentComplete = 0;
};

struct FInstallBundleStatus
{
	FName BundleName;

	EInstallBundleStatus Status = EInstallBundleStatus::QueuedForDownload;

	EInstallBundlePauseFlags PauseFlags = EInstallBundlePauseFlags::None;

	FText StatusText;

	// Download progress of EInstallBundleStatus::Downloading
	// Will be set if Status >= EInstallBundleStatus::Downloading
	TOptional<FInstallBundleDownloadProgress> BackgroundDownloadProgress;

	// Download progress of EInstallBundleStatus::Install
	// Will be set if Status >= EInstallBundleStatus::Install
	// We may download during install if background downloads are turned off or fail
	// We may also do small downloads during install as a normal part of installation
	TOptional<FInstallBundleDownloadProgress> InstallDownloadProgress;

	float Install_Percent = 0;

	float Finishing_Percent = 0;
};

struct FInstallBundleResultInfo
{
	FName BundleName;
	EInstallBundleResult Result = EInstallBundleResult::OK;
	bool bIsStartup = false;

	// Currently, these just forward BPT Error info
	FText OptionalErrorText;
	FString OptionalErrorCode;
};

struct FInstallBundlePauseInfo
{
	FName BundleName;
	EInstallBundlePauseFlags PauseFlags = EInstallBundlePauseFlags::None;
};

enum class EInstallBundleContentState : int
{
	InitializationError,
	NotInstalled,
	NeedsUpdate,
	UpToDate,
	Count,
};
inline const TCHAR* LexToString(EInstallBundleContentState State)
{
	using UnderType = __underlying_type(EInstallBundleContentState);
	static const TCHAR* Strings[] =
	{
		TEXT("InitializationError"),
		TEXT("NotInstalled"),
		TEXT("NeedsUpdate"),
		TEXT("UpToDate"),
	};
	static_assert(static_cast<UnderType>(EInstallBundleContentState::Count) == ARRAY_COUNT(Strings), "");

	return Strings[static_cast<UnderType>(State)];
}

struct FInstallBundleContentState
{
	EInstallBundleContentState State = EInstallBundleContentState::InitializationError;
	TMap<FName, EInstallBundleContentState> IndividualBundleStates;
	uint64 DownloadSize = 0;
	uint64 InstallSize = 0;	
	uint64 FreeSpace = 0;
};

enum class EInstallBundleRequestInfoFlags : int32
{
	None							= 0,
	EnqueuedBundlesForInstall		= (1 << 0),
	EnqueuedBundlesForRemoval		= (1 << 1),
	SkippedAlreadyMountedBundles	= (1 << 2),
	SkippedBundlesQueuedForRemoval	= (1 << 3),
	SkippedBundlesQueuedForInstall  = (1 << 4), // Only valid for removal requests
	SkippedUnknownBundles			= (1 << 5),
	InitializationError				= (1 << 6), // Can't enqueue because the bundle manager failed to initialize
};
ENUM_CLASS_FLAGS(EInstallBundleRequestInfoFlags);

struct FInstallBundleRequestInfo
{
	EInstallBundleRequestInfoFlags InfoFlags = EInstallBundleRequestInfoFlags::None;
	TArray<FName> BundlesQueuedForInstall;
	TArray<FName> BundlesQueuedForRemoval;
};

enum class EInstallBundleCancelFlags : int32
{
	None		= 0,
	Resumable	= (1 << 0),
};
ENUM_CLASS_FLAGS(EInstallBundleCancelFlags);

enum class EInstallBundleManagerInitErrorHandlerResult
{
	NotHandled, // Defer to the next handler
	Retry, // Try to initialize again
	StopInitialization, // Stop trying to initialize
};

DECLARE_DELEGATE_RetVal_OneParam(EInstallBundleManagerInitErrorHandlerResult, FInstallBundleManagerInitErrorHandler, EInstallBundleModuleInitResult);

DECLARE_MULTICAST_DELEGATE_OneParam(FInstallBundleCompleteMultiDelegate, FInstallBundleResultInfo);
DECLARE_MULTICAST_DELEGATE_OneParam(FInstallBundlePausedMultiDelegate, FInstallBundlePauseInfo);

DECLARE_DELEGATE_OneParam(FInstallBundleGetContentStateDelegate, FInstallBundleContentState);

class CORE_API IPlatformInstallBundleManager
{
public:
	static FInstallBundleCompleteMultiDelegate InstallBundleCompleteDelegate;
	static FInstallBundleCompleteMultiDelegate RemoveBundleCompleteDelegate;
	static FInstallBundlePausedMultiDelegate PausedBundleDelegate;

	virtual ~IPlatformInstallBundleManager() {}

	virtual void PushInitErrorCallback(FInstallBundleManagerInitErrorHandler Callback) = 0;
	virtual void PopInitErrorCallback() = 0;

	virtual bool IsInitialized() const = 0;
	virtual bool IsInitializing() const = 0;

	virtual bool IsActive() const = 0;

	virtual FInstallBundleRequestInfo RequestUpdateContent(FName BundleName, EInstallBundleRequestFlags Flags) = 0;
	virtual FInstallBundleRequestInfo RequestUpdateContent(TArrayView<FName> BundleNames, EInstallBundleRequestFlags Flags) = 0;

	virtual void GetContentState(FName BundleName, bool bAddDependencies, FInstallBundleGetContentStateDelegate Callback) = 0;
	virtual void GetContentState(TArrayView<FName> BundleNames, bool bAddDependencies, FInstallBundleGetContentStateDelegate Callback) = 0;

	virtual FInstallBundleRequestInfo RequestRemoveBundle(FName BundleName) = 0;

	virtual void RequestRemoveBundleOnNextInit(FName BundleName) = 0;

	virtual void CancelRequestRemoveBundleOnNextInit(FName BundleName) = 0;

	virtual void CancelBundle(FName BundleName, EInstallBundleCancelFlags Flags) = 0;

	virtual void CancelAllBundles(EInstallBundleCancelFlags Flags) = 0;

	virtual bool PauseBundle(FName BundleName) = 0;

	virtual void ResumeBundle(FName BundleName) = 0;

	virtual void RequestPausedBundleCallback() const = 0;

	virtual TOptional<FInstallBundleStatus> GetBundleProgress(FName BundleName) const = 0;

	virtual void UpdateContentRequestFlags(FName BundleName, EInstallBundleRequestFlags AddFlags, EInstallBundleRequestFlags RemoveFlags) = 0;

	virtual bool IsNullInterface() const = 0;

	virtual void SetErrorSimulationCommands(const FString& CommandLine) {}

	virtual TSharedPtr<IAnalyticsProviderET> GetAnalyticsProvider() const { return TSharedPtr<IAnalyticsProviderET>(); }
};

class IPlatformInstallBundleManagerModule : public IModuleInterface
{
public:
	virtual void PreUnloadCallback() override
	{
		InstallBundleManager.Reset();
	}

	IPlatformInstallBundleManager* GetInstallBundleManager()
	{
		return InstallBundleManager.Get();
	}

protected:
	TUniquePtr<IPlatformInstallBundleManager> InstallBundleManager;
};

template<class PlatformInstallBundleManagerImpl>
class TPlatformInstallBundleManagerModule : public IPlatformInstallBundleManagerModule
{
public:
	virtual void StartupModule() override
	{
		// Only instantiate the bundle manager if this is the version the game has been configured to use
		FString ModuleName;
		GConfig->GetString(TEXT("InstallBundleManager"), TEXT("ModuleName"), ModuleName, GEngineIni);

		if (FModuleManager::Get().GetModule(*ModuleName) == this)
		{
			InstallBundleManager = MakeUnique<PlatformInstallBundleManagerImpl>();
		}
	}
};
