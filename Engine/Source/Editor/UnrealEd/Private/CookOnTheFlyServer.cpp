// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	CookOnTheFlyServer.cpp: handles polite cook requests via network ;)
=============================================================================*/

#include "CookOnTheSide/CookOnTheFlyServer.h"
#include "Cooker/PackageNameCache.h"

#include "HAL/PlatformFilemanager.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Stats/Stats.h"
#include "Stats/StatsMisc.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/LocalTimestampDirectoryVisitor.h"
#include "Serialization/CustomVersion.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "Serialization/ArchiveUObject.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/MetaData.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectArray.h"
#include "Misc/PackageName.h"
#include "Misc/RedirectCollector.h"
#include "Engine/Level.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Engine/AssetManager.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/Texture.h"
#include "SceneUtils.h"
#include "Settings/ProjectPackagingSettings.h"
#include "EngineGlobals.h"
#include "Editor.h"
#include "UnrealEdGlobals.h"
#include "FileServerMessages.h"
#include "LocalizationChunkDataGenerator.h"
#include "Internationalization/Culture.h"
#include "Serialization/ArrayReader.h"
#include "Serialization/ArrayWriter.h"
#include "PackageHelperFunctions.h"
#include "DerivedDataCacheInterface.h"
#include "GlobalShader.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/IAudioFormat.h"
#include "Interfaces/IShaderFormat.h"
#include "Interfaces/ITextureFormat.h"
#include "IMessageContext.h"
#include "MessageEndpoint.h"
#include "MessageEndpointBuilder.h"
#include "INetworkFileServer.h"
#include "INetworkFileSystemModule.h"
#include "PlatformInfo.h"
#include "Serialization/ArchiveStackTrace.h"
#include "DistanceFieldAtlas.h"
#include "Cooker/AsyncIODelete.h"
#include "Serialization/BulkDataManifest.h"

#include "AssetRegistryModule.h"
#include "AssetRegistryState.h"
#include "CookerSettings.h"
#include "BlueprintNativeCodeGenModule.h"

#include "GameDelegates.h"
#include "IPAddress.h"

#include "Interfaces/IPluginManager.h"
#include "ProjectDescriptor.h"
#include "Interfaces/IProjectManager.h"

// cook by the book requirements
#include "Commandlets/AssetRegistryGenerator.h"
#include "Engine/WorldComposition.h"

// error message log
#include "Logging/TokenizedMessage.h"
#include "Logging/MessageLog.h"

// shader compiler processAsyncResults
#include "ShaderCompiler.h"
#include "ShaderCodeLibrary.h"
#include "Engine/LevelStreaming.h"
#include "Engine/TextureLODSettings.h"
#include "ProfilingDebugging/CookStats.h"

#include "Misc/NetworkVersion.h"

#include "Async/ParallelFor.h"

#include "Commandlets/ShaderPipelineCacheToolsCommandlet.h"

#define LOCTEXT_NAMESPACE "Cooker"
#define REMAPPED_PLUGGINS TEXT("RemappedPlugins")

DEFINE_LOG_CATEGORY(LogCook);

#define DEBUG_COOKONTHEFLY 0
#define OUTPUT_TIMING 1

#if OUTPUT_TIMING || ENABLE_COOK_STATS
#include "ProfilingDebugging/ScopedTimers.h"
#endif

#define PROFILE_NETWORK 0

int32 GCookProgressDisplay = (int32)ECookProgressDisplayMode::RemainingPackages;
static FAutoConsoleVariableRef CVarCookDisplayMode(
	TEXT("cook.displaymode"),
	GCookProgressDisplay,
	TEXT("Controls the display for cooker logging of packages:\n")
	TEXT("  0: No display\n")
	TEXT("  1: Display packages remaining\n")
	TEXT("  2: Display each package by name\n")
	TEXT("  3: Both\n"),
	ECVF_Default);

float GCookProgressRepeatTime = 5.0f;
static FAutoConsoleVariableRef CVarCookDisplayRepeatTime(
	TEXT("cook.display.repeattime"),
	GCookProgressRepeatTime,
	TEXT("Controls the time before the cooker will repeat the same progress message.\n"),
	ECVF_Default);

#if OUTPUT_TIMING
#include <Containers/AllocatorFixedSizeFreeList.h>

struct FHierarchicalTimerInfo
{
public:
	FHierarchicalTimerInfo(const FHierarchicalTimerInfo& InTimerInfo) = delete;
	FHierarchicalTimerInfo(FHierarchicalTimerInfo&& InTimerInfo) = delete;

	explicit FHierarchicalTimerInfo(const char* InName, uint16 InId)
	:	Id(InId)
	,	Name(InName)
	{
	}

	~FHierarchicalTimerInfo()
	{
		ClearChildren();
	}

	void							ClearChildren();
	FHierarchicalTimerInfo*			GetChild(int InId, const char* InName);

	uint32							HitCount = 0;
	uint16							Id = 0;
	bool							IncrementDepth = true;
	double							Length = 0;
	const char*						Name;

	FHierarchicalTimerInfo*			FirstChild = nullptr;
	FHierarchicalTimerInfo*			NextSibling = nullptr;

private:
	static FHierarchicalTimerInfo*	AllocNew(const char* InName, uint16 InId);
	static void						DestroyAndFree(FHierarchicalTimerInfo*);
};

static FHierarchicalTimerInfo RootTimerInfo("Root", 0);
static FHierarchicalTimerInfo* CurrentTimerInfo = &RootTimerInfo;
static TAllocatorFixedSizeFreeList<sizeof(FHierarchicalTimerInfo), 256> TimerInfoAllocator;

FHierarchicalTimerInfo* FHierarchicalTimerInfo::AllocNew(const char* InName, uint16 InId)
{
	return new(TimerInfoAllocator.Allocate()) FHierarchicalTimerInfo(InName, InId);
}

void FHierarchicalTimerInfo::DestroyAndFree(FHierarchicalTimerInfo* InPtr)
{
	InPtr->~FHierarchicalTimerInfo();
	TimerInfoAllocator.Free(InPtr);
}

void FHierarchicalTimerInfo::ClearChildren()
{
	for (FHierarchicalTimerInfo* Child = FirstChild; Child;)
	{
		FHierarchicalTimerInfo* NextChild = Child->NextSibling;

		DestroyAndFree(Child);

		Child = NextChild;
	}

	FirstChild = nullptr;
}

FHierarchicalTimerInfo* FHierarchicalTimerInfo::GetChild(int InId, const char* InName)
{
	for (FHierarchicalTimerInfo* Child = FirstChild; Child;)
	{
		if (Child->Id == InId)
			return Child;

		Child = Child->NextSibling;
	}

	FHierarchicalTimerInfo* Child = AllocNew(InName, InId);

	Child->NextSibling	= FirstChild;
	FirstChild			= Child;

	return Child;
}

struct FScopeTimer
{
public:
	FScopeTimer(const FScopeTimer&) = delete;
	FScopeTimer(FScopeTimer&&) = delete;

	FScopeTimer(int InId, const char* InName, bool IncrementScope = false )
	{
		checkSlow(IsInGameThread());

		HierarchyTimerInfo = CurrentTimerInfo->GetChild(InId, InName);

		HierarchyTimerInfo->IncrementDepth = IncrementScope;

		PrevTimerInfo		= CurrentTimerInfo;
		CurrentTimerInfo	= HierarchyTimerInfo;
	}

	void Start()
	{
		if (StartTime)
		{
			return;
		}

		StartTime = FPlatformTime::Cycles64();
	}

	void Stop()
	{
		if (!StartTime)
		{
			return;
		}

		HierarchyTimerInfo->Length += FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);
		++HierarchyTimerInfo->HitCount;

		StartTime = 0;
	}

	~FScopeTimer()
	{
		Stop();

		check(CurrentTimerInfo == HierarchyTimerInfo);
		CurrentTimerInfo = PrevTimerInfo;
	}

private:
	uint64					StartTime = 0;
	FHierarchicalTimerInfo* HierarchyTimerInfo;
	FHierarchicalTimerInfo* PrevTimerInfo;
};

void OutputHierarchyTimers(const FHierarchicalTimerInfo* TimerInfo, int32 Depth)
{
	FString TimerName(TimerInfo->Name);

	static const TCHAR LeftPad[] = TEXT("                                ");
	const SIZE_T PadOffset = FMath::Max<int>(UE_ARRAY_COUNT(LeftPad) - 1 - Depth * 2, 0);

	UE_LOG(LogCook, Display, TEXT("  %s%s: %.3fs (%u)"), &LeftPad[PadOffset], *TimerName, TimerInfo->Length, TimerInfo->HitCount);

	// We need to print in reverse order since the child list begins with the most recently added child

	TArray<const FHierarchicalTimerInfo*> Stack;

	for (const FHierarchicalTimerInfo* Child = TimerInfo->FirstChild; Child; Child = Child->NextSibling)
	{
		Stack.Add(Child);
	}

	const int32 ChildDepth = Depth + TimerInfo->IncrementDepth;

	for (size_t i = Stack.Num(); i > 0; --i)
	{
		OutputHierarchyTimers(Stack[i - 1], ChildDepth);
	}
}

void OutputHierarchyTimers()
{
	UE_LOG(LogCook, Display, TEXT("Hierarchy Timer Information:"));

	OutputHierarchyTimers(&RootTimerInfo, 0);
}

void ClearHierarchyTimers()
{
	RootTimerInfo.ClearChildren();
}

#define CREATE_TIMER(name, incrementScope) FScopeTimer ScopeTimer##name(__COUNTER__, #name, incrementScope); 

#define SCOPE_TIMER(name)				TRACE_CPUPROFILER_EVENT_SCOPE(name); CREATE_TIMER(name, true); ScopeTimer##name.Start();

#else
#define SCOPE_TIMER(name)

void OutputHierarchyTimers() {}
void ClearHierarchyTimers() {}
#endif


#if PROFILE_NETWORK
double TimeTillRequestStarted = 0.0;
double TimeTillRequestForfilled = 0.0;
double TimeTillRequestForfilledError = 0.0;
double WaitForAsyncFilesWrites = 0.0;
FEvent *NetworkRequestEvent = nullptr;
#endif

#if ENABLE_COOK_STATS
namespace DetailedCookStats
{
	//Externable so CookCommandlet can pick them up and merge them with it's cook stats
	double TickCookOnTheSideTimeSec = 0.0;
	double TickCookOnTheSideLoadPackagesTimeSec = 0.0;
	double TickCookOnTheSideResolveRedirectorsTimeSec = 0.0;
	double TickCookOnTheSideSaveCookedPackageTimeSec = 0.0;
	double TickCookOnTheSideBeginPackageCacheForCookedPlatformDataTimeSec = 0.0;
	double TickCookOnTheSideFinishPackageCacheForCookedPlatformDataTimeSec = 0.0;
	double GameCookModificationDelegateTimeSec = 0.0;
}
#endif

//////////////////////////////////////////////////////////////////////////
// FCookTimer
// used as a helper to timeslice cooker functions
//////////////////////////////////////////////////////////////////////////

struct FCookerTimer
{
	const bool bIsRealtimeMode;
	const double StartTime;
	const float &TimeSlice;
	const int MaxNumPackagesToSave; // maximum packages to save before exiting tick (this should never really hit unless we are not using realtime mode)
	int NumPackagesSaved;

	FCookerTimer(const float &InTimeSlice, bool bInIsRealtimeMode, int InMaxNumPackagesToSave = 50) :
		bIsRealtimeMode(bInIsRealtimeMode), StartTime(FPlatformTime::Seconds()), TimeSlice(InTimeSlice),
		MaxNumPackagesToSave(InMaxNumPackagesToSave), NumPackagesSaved(0)
	{
	}
	inline double GetTimeTillNow()
	{
		return FPlatformTime::Seconds() - StartTime;
	}
	bool IsTimeUp()
	{
		if (bIsRealtimeMode)
		{
			if ((FPlatformTime::Seconds() - StartTime) > TimeSlice)
			{
				return true;
			}
		}
		if (NumPackagesSaved >= MaxNumPackagesToSave)
		{
			return true;
		}
		return false;
	}

	inline void SavedPackage()
	{
		++NumPackagesSaved;
	}

	inline double GetTimeRemain()
	{
		return TimeSlice - (FPlatformTime::Seconds() - StartTime);
	}
};

////////////////////////////////////////////////////////////////
/// Cook on the fly server
///////////////////////////////////////////////////////////////

DECLARE_STATS_GROUP(TEXT("Cooking"), STATGROUP_Cooking, STATCAT_Advanced);

DECLARE_CYCLE_STAT(TEXT("Precache Derived data for platform"), STAT_TickPrecacheCooking, STATGROUP_Cooking);
DECLARE_CYCLE_STAT(TEXT("Tick cooking"), STAT_TickCooker, STATGROUP_Cooking);

constexpr uint32 ExpectedMaxNumPlatforms = 32;


/* helper structs functions
 *****************************************************************************/

/** Helper to pass a recompile request to game thread */
struct FRecompileRequest
{
	struct FShaderRecompileData RecompileData;
	volatile bool bComplete = false;
};


/** Helper to assign to any variable for a scope period */
template<class T>
struct FScopeAssign
{
private:
	T* Setting;
	T OriginalValue;
public:
	FScopeAssign(T& InSetting, const T NewValue)
	{
		Setting = &InSetting;
		OriginalValue = *Setting;
		*Setting = NewValue;
	}
	~FScopeAssign()
	{
		*Setting = OriginalValue;
	}
};


class FPackageSearchVisitor : public IPlatformFile::FDirectoryVisitor
{
	TArray<FString>& FoundFiles;
public:
	FPackageSearchVisitor(TArray<FString>& InFoundFiles)
		: FoundFiles(InFoundFiles)
	{}
	virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory)
	{
		if (bIsDirectory == false)
		{
			FString Filename(FilenameOrDirectory);
			if (Filename.EndsWith(TEXT(".uasset")) || Filename.EndsWith(TEXT(".umap")))
			{
				FoundFiles.Add(Filename);
			}
		}
		return true;
	}
};

class FAdditionalPackageSearchVisitor: public IPlatformFile::FDirectoryVisitor
{
	TSet<FString>& FoundMapFilesNoExt;
	TArray<FString>& FoundOtherFiles;
public:
	FAdditionalPackageSearchVisitor(TSet<FString>& InFoundMapFiles, TArray<FString>& InFoundOtherFiles)
		: FoundMapFilesNoExt(InFoundMapFiles), FoundOtherFiles(InFoundOtherFiles)
	{}
	virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory)
	{
		if (bIsDirectory == false)
		{
			FString Filename(FilenameOrDirectory);
			if (Filename.EndsWith(TEXT(".uasset")) || Filename.EndsWith(TEXT(".umap")))
			{
				FoundMapFilesNoExt.Add(FPaths::SetExtension(Filename, ""));
			}
			else if ( Filename.EndsWith(TEXT(".uexp")) || Filename.EndsWith(TEXT(".ubulk")) )
			{
				FoundOtherFiles.Add(Filename);
			}
		}
		return true;
	}
};

const FString& GetAssetRegistryPath()
{
	static const FString AssetRegistryPath = FPaths::ProjectDir();
	return AssetRegistryPath;
}

/**
 * Return the release asset registry filename for the release version supplied
 */
FString GetReleaseVersionAssetRegistryPath(const FString& ReleaseVersion, const FString& PlatformName )
{
	// cache the part of the path which is static because getting the ProjectDir is really slow and also string manipulation
	const static FString ProjectDirectory = FPaths::ProjectDir() / FString(TEXT("Releases"));
	return  ProjectDirectory / ReleaseVersion / PlatformName;
}

const FString& GetAssetRegistryFilename()
{
	static const FString AssetRegistryFilename = FString(TEXT("AssetRegistry.bin"));
	return AssetRegistryFilename;
}

const FString& GetDevelopmentAssetRegistryFilename()
{
	static const FString DevelopmentAssetRegistryFilename = FString(TEXT("DevelopmentAssetRegistry.bin"));
	return DevelopmentAssetRegistryFilename;
}

/**
 * Uses the FMessageLog to log a message
 * 
 * @param Message to log
 * @param Severity of the message
 */
void LogCookerMessage( const FString& MessageText, EMessageSeverity::Type Severity)
{
	FMessageLog MessageLog("CookResults");

	TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(Severity);

	Message->AddToken( FTextToken::Create( FText::FromString(MessageText) ) );
	// Message->AddToken(FTextToken::Create(MessageLogTextDetail)); 
	// Message->AddToken(FDocumentationToken::Create(TEXT("https://docs.unrealengine.com/latest/INT/Platforms/iOS/QuickStart/6/index.html"))); 
	MessageLog.AddMessage(Message);

	MessageLog.Notify(FText(), EMessageSeverity::Warning, false);
}

//////////////////////////////////////////////////////////////////////////

/** A BaseKeyFuncs for Maps and Sets with a quicker hash function for pointers than TDefaultMapKeyFuncs */
template<typename KeyType, typename ValueType, bool bInAllowDuplicateKeys>
struct TFastPointerKeyFuncs : public TDefaultMapKeyFuncs<KeyType, ValueType, bInAllowDuplicateKeys>
{
	using typename TDefaultMapKeyFuncs<KeyType, ValueType, bInAllowDuplicateKeys>::KeyInitType;
	static FORCEINLINE uint32 GetKeyHash(KeyInitType Key)
	{
#if PLATFORM_64BITS
		static_assert(sizeof(UPTRINT) == sizeof(uint64), "Expected pointer size to be 64 bits");
		// Ignoring the lower 4 bits since they are likely zero anyway.
		const uint64 ImportantBits = reinterpret_cast<uint64>(Key) >> 4;
		return GetTypeHash(ImportantBits);
#else
		static_assert(sizeof(UPTRINT) == sizeof(uint32), "Expected pointer size to be 32 bits");
		return static_cast<uint32>(reinterpret_cast<uint32>(Key));
#endif
	}
};

/** A TMap which uses TFastPointerKeyFuncs instead of TDefaultMapKeyFuncs */
template<typename KeyType, typename ValueType, typename SetAllocator = FDefaultSetAllocator>
class TFastPointerMap : public TMap<KeyType, ValueType, FDefaultSetAllocator, TFastPointerKeyFuncs<KeyType, ValueType, false>>
{};

//////////////////////////////////////////////////////////////////////////

/*
 * Struct to hold data about each platform we have encountered in the cooker.
 * Fields in this struct persist across multiple CookByTheBook sessions.
 * Fields on this struct are used on multiple threads; see variable comments for thread synchronization rules.
 */
struct UCookOnTheFlyServer::FPlatformData
{
	/* Name of the Platform, a cache of FName(ITargetPlatform->PlatformName()) */
	FName PlatformName;

	/* Pointer to the platform-specific RegistryGenerator for this platform.  If already constructed we can take a faster refresh path on future sessions.
	 * Read/Write on TickCookOnTheSide thread only
	 */
	TUniquePtr<FAssetRegistryGenerator> RegistryGenerator;

	/*
	 * Whether InitializeSandbox has been called for this platform.  If we have already initialized the sandbox we can take a faster refresh path on future sessions.
	 * Threadsafe due to write-once.  Written only once after construction on the game thread.
	 */
	bool bIsSandboxInitialized = false;

	/*
	 * The last FPlatformTime::GetSeconds() at which this platform was requested in a CookOnTheFly request.
	 * If equal to 0, the platform was not requested in a CookOnTheFly since the last clear.
	 * Written only when SessionLock critical section is held.
	 */
	double LastReferenceTime = 0.0;

	/*
	 * The count of how many active CookOnTheFly requests are currently using the Platform.
	 * Read/Write only when the SessionLock critical section is held.
	 */
	uint32 ReferenceCount = 0;
};

/* Information about the platforms (a) known and (b) active for the current cook session in the UCookOnTheFlyServer. */
struct UCookOnTheFlyServer::FPlatformManager
{
public:
	FPlatformManager(FCriticalSection& InSessionLock)
		:SessionLock(InSessionLock)
	{}

	FCriticalSection& GetSessionLock()
	{
		return SessionLock;
	}

	/*
	 * Returns the set of TargetPlatforms that is active for the CurrentCookByTheBook session or CookOnTheFly request.
	 * This function can be called and its return value referenced only from the TickCookOnTheSide thread or when SessionLock is held.
	 */
	const TArray<const ITargetPlatform*>& GetSessionPlatforms() const
	{
		checkf(bHasSelectedSessionPlatforms, TEXT("Calling GetSessionPlatforms or (any of the top level cook functions that call it) without first calling SelectSessionPlatforms is invalid"));
		return SessionPlatforms;
	}

	/*
	 * Return whether the platforms have been selected for the current session (may be empty if current session is a null session).
	 * This function can be called from any thread, but is guaranteed thread-accurate only from the TickCookOnTheSide thread or when SessionLock is held.
	*/
	bool HasSelectedSessionPlatforms() const
	{
		return bHasSelectedSessionPlatforms;
	}

	/*
	 * Return whether the given platform is already in the list of platforms for the current session.
	 * This function can be called only from the TickCookOnTheSide thread or when SessionLock is held.
	 */
	bool HasSessionPlatform(const ITargetPlatform* TargetPlatform) const
	{
		return SessionPlatforms.Contains(TargetPlatform);
	}

	/*
	 * Specify the set of TargetPlatforms to use for the currently-initializing CookByTheBook session or CookOnTheFly request.
	 * This function can be called only from the TickCookOnTheSide thread.
	 */
	void SelectSessionPlatforms(const TArrayView<const ITargetPlatform* const>& TargetPlatforms, FPackageTracker* PackageTracker);

	/*
	 * Mark that the list of TargetPlatforms for the session has no longer been set; it will be an error to try to read them until SelectSessionPlatforms is called.
	 * This function can be called only from the TickCookOnTheSide thread.
	 */
	void ClearSessionPlatforms()
	{
		FScopeLock Lock(&SessionLock);

		SessionPlatforms.Empty();
		bHasSelectedSessionPlatforms = false;
	}

	/*
	 * Add @param TargetPlatform to the session platforms if not already present.
	 * This function can be called only from the TickCookOnTheSide thread.
	 *
	 * @param PackageTracker If the TargetPlatform does not already exist, we notify the PackageTracker that it was added.
	 */
	void AddSessionPlatform(const ITargetPlatform* TargetPlatform, FPackageTracker* PackageTracker);

	/*
	 * Get The PlatformData for the given Platform.
	 * Guaranteed to return non-null for any Platform in the current list of SessionPlatforms.
	 * Can be called from any thread.
	 */
	UCookOnTheFlyServer::FPlatformData* GetPlatformData(const ITargetPlatform* Platform)
	{
		return PlatformDatas.Find(Platform);
	}

	/*
	 * Create if not already created the necessary platform-specific data for the given platform.
	 * This function can be called with new platforms only before multithreading begins (e.g. in StartNetworkFileServer or StartCookByTheBook).
	 */
	UCookOnTheFlyServer::FPlatformData& CreatePlatformData(const ITargetPlatform* Platform)
	{
		check(Platform != nullptr);
		FPlatformData& PlatformData = PlatformDatas.FindOrAdd(Platform);
		if (PlatformData.PlatformName.IsNone())
		{
			check(!bPlatformDataFrozen); // It is not legal to add new platforms to this map when running CookOnTheFlyServer because we read this map from threads handling network requests, and mutating the map is not threadsafe when it is being read

			// Newly added, construct
			PlatformData.PlatformName = FName(Platform->PlatformName());
			checkf(!PlatformData.PlatformName.IsNone(), TEXT("Invalid ITargetPlatform with an empty name"));
		}
		return PlatformData;
	}

	/* 
	 * Return whether platform-specific setup steps have been executed for the given platform in the current UCookOnTheFlyServer.
	 * This function can be called from any thread, but is guaranteed thread-accurate only from the TickCookOnTheSide thread.
	 */
	bool IsPlatformInitialized(const ITargetPlatform* Platform) const
	{
		const FPlatformData* PlatformData = PlatformDatas.Find(Platform);
		if (!PlatformData)
		{
			return false;
		}
		return PlatformData->bIsSandboxInitialized;
	}

	/* If and only if bFrozen is set to true, it is invalid to call CreatePlatformData with a Platform that does not already exist. */
	void SetPlatformDataFrozen(bool bFrozen)
	{
		bPlatformDataFrozen = bFrozen;
	}

	/*
	 * Platforms requested in CookOnTheFly requests are added to the list of SessionPlatforms, and some packages (e.g. unsolicited packages) are cooked against all session packages.
	 * To have good performance in the case where multiple targetplatforms are being requested over time from the same CookOnTheFly server, we prune platforms from the list of 
	 * active session platforms if they haven't been requested in a while.
	 * This function can be called only from the TickCookOnTheSide thread.
	 * 
	 * @param PackageTracker If a platform is removed, we remove it from the PackageTracker as well.
	 */
	void PruneUnreferencedSessionPlatforms(FPackageTracker& PackageTracker);

	/*
	 * Increment the counter indicating the current platform is requested in an active CookOnTheFly request.  Add it to the SessionPlatforms if not already present.
	 * This function can be called only when the SessionLock is held.
	 * 
	 * @param PackageTracker - PackageTracker that provides the queue of tick commands to execute AddSessionPlatform on the game thread, and to be notified of the new platform.
	*/
	void AddRefCookOnTheFlyPlatform(const ITargetPlatform* TargetPlatform, FPackageTracker& PackageTracker);

	/*
	 * Decrement the counter indicating the current platform is being used in a CookOnTheFly request.
	 * This function can be called only when the SessionLock is held.
	 */
	void ReleaseCookOnTheFlyPlatform(const ITargetPlatform* TargetPlatform)
	{
		check(TargetPlatform != nullptr);
		FPlatformData* PlatformData = GetPlatformData(TargetPlatform);
		checkf(PlatformData != nullptr, TEXT("Unrecognized Platform %s"), *TargetPlatform->PlatformName());
		check(PlatformData->ReferenceCount > 0);
		--PlatformData->ReferenceCount;
		PlatformData->LastReferenceTime = FPlatformTime::Seconds();
	}

private:
	/* A collection of initialization flags and other data we maintain for each platform we have encountered in any session. */
	TFastPointerMap<const ITargetPlatform*, UCookOnTheFlyServer::FPlatformData> PlatformDatas;

	/*
	 * A collection of Platforms that are active for the current CookByTheBook session or CookOnTheFly request.  Used so we can refer to "all platforms" without having to store a list on every FileRequest.
	 * Writing to the list of active session platforms requires a CriticalSection, because it is read (under critical section) on NetworkFileServer threads.
	 */
	TArray<const ITargetPlatform*> SessionPlatforms;

	/* A reference to the critical section used to guard SessionPlatforms. */
	FCriticalSection& SessionLock;

	/* If PlatformData is frozen, it is invalid to add new PlatformDatas. */
	bool bPlatformDataFrozen = false;

	/* It is invalid to attempt to cook if session platforms have not been selected. */
	bool bHasSelectedSessionPlatforms = false;
};

//////////////////////////////////////////////////////////////////////////

/** cooked file requests which includes platform which file is requested for */
struct FFilePlatformRequest
{
protected:
	FName			Filename;
	TArray<const ITargetPlatform*>	Platforms;

public:
	// yes we have some friends
	friend uint32 GetTypeHash(const FFilePlatformRequest& Key);

	FFilePlatformRequest() = default;

	FFilePlatformRequest(const FName& InFileName, const ITargetPlatform* InPlatform) : Filename(InFileName)
	{
		Platforms.Add(InPlatform);
	}

	FFilePlatformRequest(const FName& InFilename, const TArrayView<const ITargetPlatform* const>& InPlatforms)
		: Filename(InFilename)
		, Platforms(InPlatforms.GetData(), InPlatforms.Num())
	{}

	FFilePlatformRequest(const FName& InFilename, TArray<const ITargetPlatform*>&& InPlatforms) : Filename(InFilename), Platforms(MoveTemp(InPlatforms)) { }
	FFilePlatformRequest(const FFilePlatformRequest& InFilePlatformRequest) : Filename(InFilePlatformRequest.Filename), Platforms(InFilePlatformRequest.Platforms) { }
	FFilePlatformRequest(FFilePlatformRequest&& InFilePlatformRequest) : Filename(MoveTemp(InFilePlatformRequest.Filename)), Platforms(MoveTemp(InFilePlatformRequest.Platforms)) { }

	void SetFilename(const FString& InFilename)
	{
		Filename = FName(*InFilename);
	}

	const FName& GetFilename() const
	{
		return Filename;
	}

	const TArray<const ITargetPlatform*>& GetPlatforms() const
	{
		return Platforms;
	}

	void RemovePlatform(const ITargetPlatform* Platform)
	{
		Platforms.Remove(Platform);
	}

	void AddPlatform(const ITargetPlatform* Platform)
	{
		check(Platform != nullptr);
		Platforms.Add(Platform);
	}

	bool HasPlatform(const ITargetPlatform* Platform) const
	{
		return Platforms.Find(Platform) != INDEX_NONE;
	}

	bool IsValid()  const
	{
		return Filename != NAME_None;
	}

	void Clear()
	{
		Filename = TEXT("");
		Platforms.Empty();
	}

	FFilePlatformRequest& operator=(FFilePlatformRequest&& InFileRequest)
	{
		Filename = MoveTemp(InFileRequest.Filename);
		Platforms = MoveTemp(InFileRequest.Platforms);
		return *this;
	}

	bool operator==(const FFilePlatformRequest& InFileRequest) const
	{
		if (InFileRequest.Filename == Filename)
		{
			if (InFileRequest.Platforms == Platforms)
			{
				return true;
			}
		}
		return false;
	}

	FString ToString() const
	{
		FString Result = FString::Printf(TEXT("%s;"), *Filename.ToString());

		for (const ITargetPlatform* Platform : Platforms)
		{
			Result += FString::Printf(TEXT("%s,"), *Platform->PlatformName());
		}
		return Result;
	}
};

uint32 GetTypeHash(const FFilePlatformRequest& Key)
{
	uint32 Hash = GetTypeHash(Key.Filename);

	for (const ITargetPlatform* Platform : Key.Platforms)
	{
		Hash += Hash << 2 ^ GetTypeHash(reinterpret_cast<UPTRINT>(Platform));
	}

	return Hash;
}

struct FFilePlatformCookedPackage
{
public:
	FFilePlatformCookedPackage(const FFilePlatformRequest& InFilePlatformRequest, const TArray<bool>& bInSucceededSavePackage)
	: Filename(InFilePlatformRequest.GetFilename())
	, Platforms(InFilePlatformRequest.GetPlatforms())
	, bSucceededSavePackage(bInSucceededSavePackage)
	{ 
		check(Platforms.Num() == bSucceededSavePackage.Num());
	}

	FFilePlatformCookedPackage(const FName& InFilename, const TArrayView<const ITargetPlatform* const>& InPlatforms) 
	: Filename(InFilename)
	, Platforms(InPlatforms.GetData(), InPlatforms.Num())
	{
		// only use this constructor to short hand when packages fail
		for (int32 I = 0; I < InPlatforms.Num(); ++I)
		{
			bSucceededSavePackage.Add(false);
		}

		check(Platforms.Num() == bSucceededSavePackage.Num());
	}

	FFilePlatformCookedPackage(const FName& InFilename, const TArrayView<const ITargetPlatform* const>& InPlatforms, const TArray<bool>& bInSucceededSavePackage)
	: Filename(InFilename)
	, Platforms(InPlatforms.GetData(), InPlatforms.Num())
	, bSucceededSavePackage(bInSucceededSavePackage) 
	{ 
		check(Platforms.Num() == bSucceededSavePackage.Num()); 
	}

	FFilePlatformCookedPackage(const FName& InFilename, TArray<const ITargetPlatform*>&& InPlatforms, TArray<bool>&& bInSucceededSavePackage)
	: Filename(InFilename)
	, Platforms(MoveTemp(InPlatforms))
	, bSucceededSavePackage(MoveTemp(bInSucceededSavePackage))
	{ 
		check(Platforms.Num() == bSucceededSavePackage.Num());
	}

	FFilePlatformCookedPackage(const FFilePlatformCookedPackage& InFilePlatformRequest) = default;
	FFilePlatformCookedPackage(FFilePlatformCookedPackage&& InFilePlatformRequest) = default;

	bool IsValid() const
	{
		return Filename != NAME_None;
	}

	const FName& GetFilename() const
	{
		return Filename;
	}

	const TArray<const ITargetPlatform*>& GetPlatforms() const
	{
		return Platforms;
	}

	bool HasPlatform(const ITargetPlatform* Platform) const
	{
		return Platforms.Find(Platform) != INDEX_NONE;
	}

	bool HasPlatforms(const TArrayView<const ITargetPlatform* const>& QueryPlatforms, bool bIncludeFailed = true) const
	{
		if (bIncludeFailed == false)
		{
			bool bAllFailed = true;

			for (const ITargetPlatform* Platform : QueryPlatforms)
			{
				if (HasSucceededSavePackage(Platform))
				{
					bAllFailed = false;
					break;
				}
			}

			if (bAllFailed)
			{
				return false;
			}
		}

		// make sure all the platforms are completed
		for (const ITargetPlatform* Platform : QueryPlatforms)
		{
			if (!Platforms.Contains(Platform))
			{
				return false;
			}
		}

		return true;
	}

	const void AddPlatform(const ITargetPlatform* Platform, bool bSucceeded)
	{
		check(Platforms.Num() == bSucceededSavePackage.Num());
		check(Platform != nullptr);
		Platforms.Add(Platform);
		bSucceededSavePackage.Add(bSucceeded);
		check(Platforms.Num() == bSucceededSavePackage.Num());
	}

	void RemovePlatform(const ITargetPlatform* Platform)
	{
		check(Platforms.Num() == bSucceededSavePackage.Num());
		const int32 Index = Platforms.IndexOfByKey(Platform);

		if (Index != -1)
		{
			Platforms.RemoveAt(Index);
			bSucceededSavePackage.RemoveAt(Index);
		}

		check(Platforms.Num() == bSucceededSavePackage.Num());
	}

	const bool HasSucceededSavePackage(const ITargetPlatform* Platform) const
	{
		const int32 Index = Platforms.IndexOfByKey(Platform);

		if ((Index != -1) && (Index < bSucceededSavePackage.Num()))
		{
			return bSucceededSavePackage[Index];
		}
		return false;
	}

private:
	FName			Filename;
	TArray<const ITargetPlatform*>	Platforms;
	TArray<bool>	bSucceededSavePackage; // one bool for each platform
};

struct CookedPackageSet
{
	friend struct FPackageTracker;

private:
	mutable FCriticalSection				SynchronizationObject;
	TMap<FName, FFilePlatformCookedPackage> FilesProcessed;

public:
	int32 Num()
	{
		return FilesProcessed.Num();
	}

	FFilePlatformCookedPackage& Add(const FFilePlatformCookedPackage& InRequest)
	{
		check(InRequest.IsValid());

		FScopeLock ScopeLock(&SynchronizationObject);

		// see if it's already in the requests list
		FFilePlatformCookedPackage* ExistingRequest = FilesProcessed.Find(InRequest.GetFilename());

		if (ExistingRequest)
		{
			check(ExistingRequest->GetFilename() == InRequest.GetFilename());

			for (const ITargetPlatform* Platform : InRequest.GetPlatforms())
			{
				const bool bSucceeded = InRequest.HasSucceededSavePackage(Platform);
				ExistingRequest->AddPlatform(Platform, bSucceeded);
			}
			return *ExistingRequest;
		}
		else
		{
			return FilesProcessed.Add(InRequest.GetFilename(), InRequest);
		}
	}

	bool Exists(const FFilePlatformRequest& Request) const
	{
		return Exists(Request.GetFilename(), Request.GetPlatforms(), /* bIncludeFailed */ true);
	}

	bool Exists(const FName& Filename, const TArrayView<const ITargetPlatform* const>& Platforms, bool bIncludeFailed = true) const
	{
		FScopeLock ScopeLock(&SynchronizationObject);

		const FFilePlatformCookedPackage* OurRequest = FilesProcessed.Find(Filename);

		if (!OurRequest)
		{
			return false;
		}

		return OurRequest->HasPlatforms(Platforms, bIncludeFailed);
	}

	void RemoveAllFilesForPlatform(const ITargetPlatform* Platform)
	{
		FScopeLock ScopeLock(&SynchronizationObject);

		for (auto& Request : FilesProcessed)
		{
			Request.Value.RemovePlatform(Platform);
		}
	}

	bool GetCookedPlatforms(const FName& Filename, TArray<const ITargetPlatform*>& OutPlatformList) const
	{
		FScopeLock ScopeLock(&SynchronizationObject);

		if (const FFilePlatformCookedPackage* Request = FilesProcessed.Find(Filename))
		{
			OutPlatformList = Request->GetPlatforms();
			return true;
		}

		return false;
	}

	int RemoveFile(const FName& Filename)
	{
		FScopeLock ScopeLock(&SynchronizationObject);

		return FilesProcessed.Remove(Filename);
	}

	void GetCookedFilesForPlatform(const ITargetPlatform* Platform, TArray<FName>& CookedFiles, bool bGetFailedCookedPackages, bool bGetSuccessfulCookedPackages)
	{
		FScopeLock ScopeLock(&SynchronizationObject);
		for (const auto& CookedFile : FilesProcessed)
		{
			if (CookedFile.Value.HasPlatform(Platform))
			{
				const bool bHasSucceededSavePackage = CookedFile.Value.HasSucceededSavePackage(Platform);

				if (	(bHasSucceededSavePackage && bGetSuccessfulCookedPackages)
					|| ((bHasSucceededSavePackage == false) && bGetFailedCookedPackages))
				{
					CookedFiles.Add(CookedFile.Value.GetFilename());
				}
			}
		}
	}

	void Empty(int32 ExpectedNumElements = 0)
	{
		FScopeLock ScopeLock(&SynchronizationObject);
		FilesProcessed.Empty(ExpectedNumElements);
	}
};

/* 
 * Holds a queue of requests for cooking packages on specified platforms.
 * Not threadsafe; caller must handle locking a CriticalSection around use when used on multiple threads.
 */
struct CookRequestQueue
{
	template <class PREDICATE_CLASS>
	void Sort(const PREDICATE_CLASS& Predicate)
	{
		Queue.Sort(Predicate);
	}

	const TArray<FName>& GetQueue() const 
	{ 
		return Queue;
	}

	void EnqueueUnique(const FFilePlatformRequest& Request, bool ForceEnqueFront = false)
	{
		TArray<const ITargetPlatform*>* Platforms = PlatformList.Find(Request.GetFilename());
		if (Platforms == NULL)
		{
			PlatformList.Add(Request.GetFilename(), Request.GetPlatforms());
			Queue.Add(Request.GetFilename());
		}
		else
		{
			// add the requested platforms to the platform list
			for (const ITargetPlatform* Platform : Request.GetPlatforms())
			{
				Platforms->AddUnique(Platform);
			}
		}

		if (ForceEnqueFront)
		{
			int32 Index = Queue.Find(Request.GetFilename());
			check(Index != INDEX_NONE);
			if (Index != 0)
			{
				Queue.Swap(0, Index);
			}
		}
	}

	bool Dequeue(FFilePlatformRequest& OutResult)
	{
		if (Queue.Num())
		{
			FName Filename = Queue[0];
			Queue.RemoveAt(0);
			TArray<const ITargetPlatform*> Platforms = PlatformList.FindChecked(Filename);
			PlatformList.Remove(Filename);

			OutResult = FFilePlatformRequest(MoveTemp(Filename), MoveTemp(Platforms));
			return true;
		}
		return false;
	}

	void DequeueAllRequests(TArray<FFilePlatformRequest>& RequestArray)
	{
		if (Queue.Num())
		{
			for (const auto& Request : PlatformList)
			{
				RequestArray.Add(FFilePlatformRequest(Request.Key, Request.Value));
			}
			PlatformList.Empty();
			Queue.Empty();
		}
	}

	bool Exists(const FName& Filename, const TArray<const ITargetPlatform*>& Platforms) const
	{
		const TArray<const ITargetPlatform*>* ExistingPlatforms = PlatformList.Find(Filename);
		if (ExistingPlatforms == nullptr)
		{
			return false;
		}
		for (const ITargetPlatform* Platform : Platforms)
		{
			if (!ExistingPlatforms->Contains(Platform))
			{
				return false;
			}
		}
		return true;
	}

	bool Exists(const FName& Filename)
	{
		const TArray<const ITargetPlatform*>* ExistingPlatforms = PlatformList.Find(Filename);
		if (ExistingPlatforms == nullptr)
		{
			return false;
		}
		return true;
	}

	bool HasItems() const
	{
		return Queue.Num() > 0;
	}

	int Num() const
	{
		return Queue.Num();
	}

	void Empty()
	{
		Queue.Empty();
		PlatformList.Empty();
	}

	void RemovePlatform(const ITargetPlatform* TargetPlatform)
	{
		for (auto& kvpair : PlatformList)
		{
			TArray<const ITargetPlatform*>& RequestPlatforms = kvpair.Value;
			RequestPlatforms.Remove(TargetPlatform);
			if (RequestPlatforms.Num() == 0)
			{
				UE_LOG(LogCook, Error, TEXT("RemovePlatform call has left an empty list of platforms requested in CookOnTheSide request."));
			}
		}
	}

private:
	TArray<FName>				Queue;
	TMap<FName, TArray<const ITargetPlatform*>>	PlatformList;
};

struct FThreadSafeUnsolicitedPackagesList
{
	void AddCookedPackage(const FFilePlatformRequest& PlatformRequest)
	{
		FScopeLock S(&SyncObject);
		CookedPackages.Add(PlatformRequest);
	}

	void GetPackagesForPlatformAndRemove(const ITargetPlatform* Platform, TArray<FName> PackageNames)
	{
		FScopeLock _(&SyncObject);

		for (int I = CookedPackages.Num() - 1; I >= 0; --I)
		{
			FFilePlatformRequest &Request = CookedPackages[I];

			if (Request.GetPlatforms().Contains(Platform))
			{
				// remove the platform
				Request.RemovePlatform(Platform);

				if (Request.GetPlatforms().Num() == 0)
				{
					CookedPackages.RemoveAt(I);
				}
			}
		}
	}

	void Empty()
	{
		FScopeLock _(&SyncObject);
		CookedPackages.Empty();
	}

private:
	FCriticalSection				SyncObject;
	TArray<FFilePlatformRequest>	CookedPackages;
};

template<typename Type, typename SynchronizationObjectType, typename ScopeLockType>
struct FUnsynchronizedQueue
{
private:
	mutable SynchronizationObjectType	SynchronizationObject; // made this mutable so this class can have const functions and still be thread safe
	TArray<Type>		Items;
public:
	void Enqueue(const Type& Item)
	{
		ScopeLockType ScopeLock(&SynchronizationObject);
		Items.Add(Item);
	}
	void EnqueueUnique(const Type& Item)
	{
		ScopeLockType ScopeLock(&SynchronizationObject);
		Items.AddUnique(Item);
	}
	bool Dequeue(Type* Result)
	{
		ScopeLockType ScopeLock(&SynchronizationObject);
		if (Items.Num())
		{
			*Result = Items[0];
			Items.RemoveAt(0);
			return true;
		}
		return false;
	}
	void DequeueAll(TArray<Type>& Results)
	{
		ScopeLockType ScopeLock(&SynchronizationObject);
		Results += Items;
		Items.Empty();
	}

	bool HasItems() const
	{
		ScopeLockType ScopeLock(&SynchronizationObject);
		return Items.Num() > 0;
	}

	void Remove(const Type& Item)
	{
		ScopeLockType ScopeLock(&SynchronizationObject);
		Items.Remove(Item);
	}

	void CopyItems(TArray<Type> &InItems) const
	{
		ScopeLockType ScopeLock(&SynchronizationObject);
		InItems = Items;
	}

	int Num() const
	{
		ScopeLockType ScopeLock(&SynchronizationObject);
		return Items.Num();
	}

	void Empty()
	{
		ScopeLockType ScopeLock(&SynchronizationObject);
		Items.Empty();
	}
};


template<typename Type>
struct FThreadSafeQueue : public FUnsynchronizedQueue<Type, FCriticalSection, FScopeLock>
{
	/**
	* Don't add any functions here, this is just a overqualified typedef
	* Add functions / functionality to the FUnsynchronizedQueue
	*/
};

/** Simple thread safe proxy for TSet<FName> */
template <typename T>
class FThreadSafeSet
{
	TSet<T> InnerSet;
	FCriticalSection SetCritical;
public:
	void Add(T InValue)
	{
		FScopeLock SetLock(&SetCritical);
		InnerSet.Add(InValue);
	}
	bool AddUnique(T InValue)
	{
		FScopeLock SetLock(&SetCritical);
		if (!InnerSet.Contains(InValue))
		{
			InnerSet.Add(InValue);
			return true;
		}
		return false;
	}
	bool Contains(T InValue)
	{
		FScopeLock SetLock(&SetCritical);
		return InnerSet.Contains(InValue);
	}
	void Remove(T InValue)
	{
		FScopeLock SetLock(&SetCritical);
		InnerSet.Remove(InValue);
	}
	void Empty()
	{
		FScopeLock SetLock(&SetCritical);
		InnerSet.Empty();
	}

	void GetValues(TSet<T>& OutSet)
	{
		FScopeLock SetLock(&SetCritical);
		OutSet.Append(InnerSet);
	}
};

struct FPackageTracker : public FUObjectArray::FUObjectCreateListener, public FUObjectArray::FUObjectDeleteListener
{
	enum class ERequestType
	{
		None,
		TickCommand,
		Cook
	};
	typedef TFunction<void()> FTickCommand;

	FPackageTracker(FPackageNameCache* InPackageNameCache, FCriticalSection& InRequestLock, UCookOnTheFlyServer::FPlatformManager& InPlatformManager)
		: RequestLock(InRequestLock)
		, PackageNameCache(InPackageNameCache)
		, PlatformManager(InPlatformManager)
	{
		for (TObjectIterator<UPackage> It; It; ++It)
		{
			UPackage* Package = *It;

			if (Package->GetOuter() == nullptr)
			{
				LoadedPackages.Add(Package);
			}
		}

		NewPackages = LoadedPackages;

		GUObjectArray.AddUObjectDeleteListener(this);
		GUObjectArray.AddUObjectCreateListener(this);

		// We will call FilterLoadedPackage on every package in LoadedPackages the next time GetPackagesPendingSave is called
		bPackagesPendingSaveDirty = true;
	}

	~FPackageTracker()
	{
		GUObjectArray.RemoveUObjectDeleteListener(this);
		GUObjectArray.RemoveUObjectCreateListener(this);
	}

	TArray<UPackage*> GetNewPackages()
	{
		return MoveTemp(NewPackages);
	}

	virtual void NotifyUObjectCreated(const class UObjectBase *Object, int32 Index) override
	{
		if (Object->GetClass() == UPackage::StaticClass())
		{
			auto Package = const_cast<UPackage*>(static_cast<const UPackage*>(Object));

			if (Package->GetOuter() == nullptr)
			{
				LoadedPackages.Add(Package);
				NewPackages.Add(Package);

				if (PlatformManager.HasSelectedSessionPlatforms())
				{
					FilterLoadedPackage(Package);
				}
			}
		}
	}

	virtual void NotifyUObjectDeleted(const class UObjectBase *Object, int32 Index) override
	{
		if (Object->GetClass() == UPackage::StaticClass())
		{
			auto Package = const_cast<UPackage*>(static_cast<const UPackage*>(Object));

			LoadedPackages.Remove(Package);
			NewPackages.Remove(Package);
			PostLoadFixupPackages.Remove(Package);
			PackagesPendingSave.Remove(Package);
		}
	}

	virtual void OnUObjectArrayShutdown() override
	{
		GUObjectArray.RemoveUObjectDeleteListener(this);
		GUObjectArray.RemoveUObjectCreateListener(this);
	}

	void AddTickCommand(const FTickCommand& TickCommand)
	{
		FScopeLock ScopeLock(&RequestLock);
		TickCommands.Add(TickCommand);
	}

	bool HasRequests()
	{
		FScopeLock ScopeLock(&RequestLock);
		return TickCommands.Num() > 0 || CookRequests.HasItems();
	}

	void EnqueueUniqueCookRequest(const FFilePlatformRequest& FileRequest, bool bForceFrontOfQueue = false)
	{
		FScopeLock ScopeLock(&RequestLock);
		CookRequests.EnqueueUnique(FileRequest, bForceFrontOfQueue);
	}

	int32 GetCookRequestsNum()
	{
		FScopeLock ScopeLock(&RequestLock);
		return CookRequests.Num();
	}

	ERequestType DequeueRequest(TArray<FTickCommand>& OutTickCommands, FFilePlatformRequest& OutToBuild)
	{
		FScopeLock ScopeLock(&RequestLock);
		if (TickCommands.Num() > 0)
		{
			OutTickCommands = MoveTemp(TickCommands);
			TickCommands.Empty();
			return ERequestType::TickCommand;
		}
		else if (CookRequests.Dequeue(OutToBuild))
		{
			return ERequestType::Cook;
		}
		else
		{
			return ERequestType::None;
		}
	}

	void EmptyRequests()
	{
		FScopeLock ScopeLock(&RequestLock);
		CookRequests.Empty();
		TickCommands.Empty();
	}

	void DequeueAllRequests(TArray<FPackageTracker::FTickCommand>& OutTickCommands, TArray<FFilePlatformRequest>& RequestArray)
	{
		FScopeLock ScopeLock(&RequestLock);
		OutTickCommands = MoveTemp(TickCommands);
		TickCommands.Empty();
		CookRequests.DequeueAllRequests(RequestArray);
	}

	void RemoveSessionPlatform(const ITargetPlatform* TargetPlatform)
	{
		FScopeLock Lock(&RequestLock);

		TArray<FName> UnusedPackageNames;
		UnsolicitedCookedPackages.GetPackagesForPlatformAndRemove(TargetPlatform, UnusedPackageNames);

		// The caller should not be removing platforms if we have an active request referencing that platform, but in case they did, remove the platform
		// from all pending requests
		CookRequests.RemovePlatform(TargetPlatform);

		// Keep information about packages cooked for the platform in the CookedPackages set, in case the platform is requested again in the future
	}


	// This is the set of packages which have already had PostLoadFixup called 
	TSet<UPackage*>			PostLoadFixupPackages;

	// This is a complete list of currently loaded UPackages
	TArray<UPackage*>		LoadedPackages;

	// This list contains the UPackages loaded since last call to GetNewPackages
	TArray<UPackage*>		NewPackages;

	// Set of files which have been cooked 
	// when needing to recook a file the entry will need to be removed from here
	CookedPackageSet		CookedPackages;

	// List of requested files
private:
	CookRequestQueue		CookRequests;
	TArray<FTickCommand> TickCommands;
	FCriticalSection&		RequestLock;
public:
	// These functions exposing the RequestLock and CookRequests are important in a few places for performance, but will likely be removed in the future and should not be widely used
	FCriticalSection& GetRequestLock() { return RequestLock; }
	CookRequestQueue& GetThreadUnsafeCookRequests(){ return CookRequests; }
public:

	FEvent*					CookRequestEvent = nullptr;

	FThreadSafeUnsolicitedPackagesList			UnsolicitedCookedPackages;
	FThreadSafeQueue<struct FRecompileRequest*> RecompileRequests;

	FThreadSafeSet<FName>						NeverCookPackageList;
	FThreadSafeSet<FName>						UncookedEditorOnlyPackages; // set of packages that have been rejected due to being referenced by editor-only properties
	TFastPointerMap<const ITargetPlatform*, TSet<FName>> 	PlatformSpecificNeverCookPackages;

private:
	FPackageNameCache*		PackageNameCache;
	UCookOnTheFlyServer::FPlatformManager& PlatformManager;

	// Set of packages pending save.

	typedef TSet<UPackage*> PendingPackageSet;
	PendingPackageSet		PackagesPendingSave;

	void					FilterLoadedPackage(UPackage* Package);

	bool					bPackagesPendingSaveDirty = true;

	void					UpdatePackagesPendingSave();

public:
	void					OnPlatformAdded() { bPackagesPendingSaveDirty = true; }
	const PendingPackageSet&	GetPackagesPendingSave()			{ UpdatePackagesPendingSave(); return PackagesPendingSave; }
	
	void						DirtyPackage(const FName& CookedPackageName, UPackage* Package);

	void						OnPackageCooked(const FFilePlatformCookedPackage& CookedPackage, UPackage* Package);
	void						RemovePendingSavePackage(UPackage* Package);
};

void FPackageTracker::DirtyPackage(const FName& CookedPackageName, UPackage* Package)
{
	if (CookedPackages.RemoveFile(CookedPackageName))
	{
		PackagesPendingSave.Add(Package);
	}
}

void FPackageTracker::OnPackageCooked(const FFilePlatformCookedPackage& CookedPackage, UPackage* Package)
{
	FFilePlatformCookedPackage& CurrentStatus = CookedPackages.Add(CookedPackage);

	if (Package)
	{
		if (CurrentStatus.HasPlatforms(PlatformManager.GetSessionPlatforms()))
		{
			PackagesPendingSave.Remove(Package);
		}
	}
}

void FPackageTracker::RemovePendingSavePackage(UPackage* Package)
{
	PackagesPendingSave.Remove(Package);
}

void FPackageTracker::FilterLoadedPackage(UPackage* Package)
{
	check(Package != nullptr);

	const FName StandardPackageFName = PackageNameCache->GetCachedStandardPackageFileFName(Package);

	if (StandardPackageFName == NAME_None)
	{
		return;	// if we have name none that means we are in core packages or something...
	}

	if (CookedPackages.Exists(StandardPackageFName, PlatformManager.GetSessionPlatforms()))
	{
		// All SessionPlatforms have already been cooked for the package, so we don't need to save it again
		return;
	}

	PackagesPendingSave.Add(Package);
}

void FPackageTracker::UpdatePackagesPendingSave()
{
	if (bPackagesPendingSaveDirty == false)
	{
		return;
	}
	check(PlatformManager.HasSelectedSessionPlatforms()); // It is not valid to call UpdatePackagesPendingSave until after sessions have been selected

	PackagesPendingSave.Empty(PackagesPendingSave.Num());

	for (UPackage* Package : LoadedPackages)
	{
		FilterLoadedPackage(Package);
	}

	bPackagesPendingSaveDirty = false;
}

void UCookOnTheFlyServer::FPlatformManager::SelectSessionPlatforms(const TArrayView<const ITargetPlatform* const>& TargetPlatforms, FPackageTracker* InPackageTracker)
{
	FScopeLock Lock(&SessionLock);

	SessionPlatforms.Empty(TargetPlatforms.Num());
	SessionPlatforms.Append(TargetPlatforms.GetData(), TargetPlatforms.Num());
	for (const ITargetPlatform* TargetPlatform : TargetPlatforms)
	{
		CreatePlatformData(TargetPlatform);
	}
	bHasSelectedSessionPlatforms = true;
	if (InPackageTracker)
	{
		InPackageTracker->OnPlatformAdded();
	}
}

void UCookOnTheFlyServer::FPlatformManager::AddSessionPlatform(const ITargetPlatform* TargetPlatform, FPackageTracker* InPackageTracker)
{
	FScopeLock Lock(&SessionLock);

	if (!SessionPlatforms.Contains(TargetPlatform))
	{
		SessionPlatforms.Add(TargetPlatform);
		CreatePlatformData(TargetPlatform);
		bHasSelectedSessionPlatforms = true;
		if (InPackageTracker)
		{
			InPackageTracker->OnPlatformAdded();
		}
	}
}

void UCookOnTheFlyServer::FPlatformManager::PruneUnreferencedSessionPlatforms(FPackageTracker& InPackageTracker)
{
	const double SecondsToLive = 5.0 * 60;

	double OldestKeepTime = -1.0e10; // Constructed to something smaller than 0 - SecondsToLive, so we can robustly detect not-yet-initialized
	TArray<const ITargetPlatform*, TInlineAllocator<1>> RemovePlatforms;

	for (auto& kvpair : PlatformDatas)
	{
		UCookOnTheFlyServer::FPlatformData& PlatformData = kvpair.Value;
		if (PlatformData.LastReferenceTime > 0. && PlatformData.ReferenceCount == 0)
		{
			// We have a platform that we need to check for pruning.  Initialize the OldestKeepTime so we can check whether the platform has aged out.
			if (OldestKeepTime < -SecondsToLive)
			{
				const double CurrentTimeSeconds = FPlatformTime::Seconds();
				OldestKeepTime = CurrentTimeSeconds - SecondsToLive;
			}

			// Note that this loop is outside of the critical section, for performance.
			// If we find any candidates for pruning we have to check them again once inside the critical section.
			if (kvpair.Value.LastReferenceTime < OldestKeepTime)
			{
				RemovePlatforms.Add(kvpair.Key);
			}
		}
	}

	if (RemovePlatforms.Num() > 0)
	{
		FScopeLock Lock(&SessionLock);

		for (const ITargetPlatform* TargetPlatform : RemovePlatforms)
		{
			UCookOnTheFlyServer::FPlatformData* PlatformData = PlatformDatas.Find(TargetPlatform);
			if (PlatformData->LastReferenceTime > 0. && PlatformData->ReferenceCount == 0 && PlatformData->LastReferenceTime < OldestKeepTime)
			{
				// Mark that the platform no longer needs to be inspected for pruning because we have removed it from CookOnTheFly's SessionPlatforms
				PlatformData->LastReferenceTime = 0.;

				// Remove the SessionPlatform
				InPackageTracker.RemoveSessionPlatform(TargetPlatform);

				SessionPlatforms.Remove(TargetPlatform);
				if (SessionPlatforms.Num() == 0)
				{
					bHasSelectedSessionPlatforms = false;
				}
			}
		}
	}
}

void UCookOnTheFlyServer::FPlatformManager::AddRefCookOnTheFlyPlatform(const ITargetPlatform* TargetPlatform, FPackageTracker& InPackageTracker)
{
	check(TargetPlatform != nullptr);
	FPlatformData* PlatformData = GetPlatformData(TargetPlatform);
	checkf(PlatformData != nullptr, TEXT("Unrecognized Platform %s"), *TargetPlatform->PlatformName());
	++PlatformData->ReferenceCount;

	if (!HasSessionPlatform(TargetPlatform))
	{
		InPackageTracker.AddTickCommand([this, TargetPlatform, &InPackageTracker]()
			{
				AddSessionPlatform(TargetPlatform, &InPackageTracker);
			});
	}
}

//////////////////////////////////////////////////////////////////////////
// Cook by the book options

struct UCookOnTheFlyServer::FCookByTheBookOptions
{
public:
	/** Should we generate streaming install manifests (only valid option in cook by the book) */
	bool							bGenerateStreamingInstallManifests = false;

	/** Should we generate a seperate manifest for map dependencies */
	bool							bGenerateDependenciesForMaps = false;

	/** Is cook by the book currently running */
	bool							bRunning = false;

	/** Cancel has been queued will be processed next tick */
	bool							bCancel = false;

	/** DlcName setup if we are cooking dlc will be used as the directory to save cooked files to */
	FString							DlcName;

	/** Create a release from this manifest and store it in the releases directory for this cgame */
	FString							CreateReleaseVersion;

	/** Dependency graph of maps as root objects. */
	TFastPointerMap<const ITargetPlatform*,TMap<FName,TSet<FName>>> MapDependencyGraphs;

	/** If a cook is cancelled next cook will need to resume cooking */
	TArray<FFilePlatformRequest>		PreviousCookRequests;

	/** If we are based on a release version of the game this is the set of packages which were cooked in that release. Map from platform name to list of uncooked package filenames */
	TMap<FName, TArray<FName>>			BasedOnReleaseCookedPackages;

	/** Timing information about cook by the book */
	double							CookTime = 0.0;
	double							CookStartTime = 0.0;

	/** error when detecting engine content being used in this cook */
	bool							bErrorOnEngineContentUse = false;
	bool							bDisableUnsolicitedPackages = false;
	bool							bFullLoadAndSave = false;
	bool							bPackageStore = false;
	TArray<FName>					StartupPackages;

	/** Mapping from source packages to their localized variants (based on the culture list in FCookByTheBookStartupOptions) */
	TMap<FName, TArray<FName>>		SourceToLocalizedPackageVariants;
};

/* UCookOnTheFlyServer functions
 *****************************************************************************/

UCookOnTheFlyServer::UCookOnTheFlyServer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer),
	CurrentCookMode(ECookMode::CookOnTheFly),
	CookByTheBookOptions(nullptr),
	CookFlags(ECookInitializationFlags::None),
	bIsInitializingSandbox(false),
	bIgnoreMarkupPackageAlreadyLoaded(false),
	bIsSavingPackage(false),
	AssetRegistry(nullptr)
{
	PlatformManager = MakeUnique<FPlatformManager>(RequestLock);
}

UCookOnTheFlyServer::UCookOnTheFlyServer(FVTableHelper& Helper) :Super(Helper) {}

UCookOnTheFlyServer::~UCookOnTheFlyServer()
{
	ClearPackageStoreContexts();

	FCoreDelegates::OnFConfigCreated.RemoveAll(this);
	FCoreDelegates::OnFConfigDeleted.RemoveAll(this);

	delete CookByTheBookOptions;
	CookByTheBookOptions = nullptr;

	delete PackageTracker;
	PackageTracker = nullptr;

	delete PackageNameCache;
	PackageNameCache = nullptr;

	ClearHierarchyTimers();
}

// This tick only happens in the editor.  The cook commandlet directly calls tick on the side.
void UCookOnTheFlyServer::Tick(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UCookOnTheFlyServer::Tick);

	check(IsCookingInEditor());

	if (IsCookByTheBookMode() && !IsCookByTheBookRunning() && !GIsSlowTask)
	{
		// if we are in the editor then precache some stuff ;)
		TArray<const ITargetPlatform*> CacheTargetPlatforms;
		const ULevelEditorPlaySettings* PlaySettings = GetDefault<ULevelEditorPlaySettings>();
		if (PlaySettings && (PlaySettings->LastExecutedLaunchModeType == LaunchMode_OnDevice))
		{
			FString DeviceName = PlaySettings->LastExecutedLaunchDevice.Left(PlaySettings->LastExecutedLaunchDevice.Find(TEXT("@")));
			CacheTargetPlatforms.Add(GetTargetPlatformManager()->FindTargetPlatform(DeviceName));
		}
		if (CacheTargetPlatforms.Num() > 0)
		{
			// early out all the stuff we don't care about 
			if (!IsCookFlagSet(ECookInitializationFlags::BuildDDCInBackground))
			{
				return;
			}
			TickPrecacheObjectsForPlatforms(0.001, CacheTargetPlatforms);
		}
	}

	uint32 CookedPackagesCount = 0;
	const static float CookOnTheSideTimeSlice = 0.1f; // seconds
	TickCookOnTheSide( CookOnTheSideTimeSlice, CookedPackagesCount);
	TickRecompileShaderRequests();
}

bool UCookOnTheFlyServer::IsTickable() const 
{ 
	return IsCookFlagSet(ECookInitializationFlags::AutoTick); 
}

TStatId UCookOnTheFlyServer::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCookServer, STATGROUP_Tickables);
}

void UCookOnTheFlyServer::ConstructPackageTracker()
{
	delete PackageNameCache;
	delete PackageTracker;

	if (AssetRegistry)
	{
		PackageNameCache = new FPackageNameCache(AssetRegistry);
	}
	else
	{
		PackageNameCache = new FPackageNameCache();
	}

	PackageTracker = new FPackageTracker(PackageNameCache, RequestLock, *PlatformManager.Get());
}

bool UCookOnTheFlyServer::StartNetworkFileServer(const bool BindAnyPort, const TArray<ITargetPlatform*>& TargetPlatforms)
{
	check(IsCookOnTheFlyMode());
	//GetDerivedDataCacheRef().WaitForQuiescence(false);

#if PROFILE_NETWORK
	NetworkRequestEvent = FPlatformProcess::GetSynchEventFromPool();
#endif

	// Precreate the map of all possible target platforms so we can access the collection of existing platforms in a threadsafe manner
	// Each PlatformData in the map will be uninitialized until we call AddCookOnTheFlyPlatform for the platform
	ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();
	for (const ITargetPlatform* TargetPlatform : TPM.GetTargetPlatforms())
	{
		PlatformManager->CreatePlatformData(TargetPlatform);
	}
	PlatformManager->SetPlatformDataFrozen(true);

	CreateSandboxFile();
	GenerateAssetRegistry();

	for (ITargetPlatform* TargetPlatform: TargetPlatforms)
	{
		AddCookOnTheFlyPlatform(TargetPlatform);
	}

	// start the listening thread
	FNewConnectionDelegate NewConnectionDelegate(FNewConnectionDelegate::CreateUObject(this, &UCookOnTheFlyServer::HandleNetworkFileServerNewConnection));
	FFileRequestDelegate FileRequestDelegate(FFileRequestDelegate::CreateUObject(this, &UCookOnTheFlyServer::HandleNetworkFileServerFileRequest));
	FRecompileShadersDelegate RecompileShadersDelegate(FRecompileShadersDelegate::CreateUObject(this, &UCookOnTheFlyServer::HandleNetworkFileServerRecompileShaders));
	FSandboxPathDelegate SandboxPathDelegate(FSandboxPathDelegate::CreateUObject(this, &UCookOnTheFlyServer::HandleNetworkGetSandboxPath));
	FInitialPrecookedListDelegate InitialPrecookedListDelegate(FInitialPrecookedListDelegate::CreateUObject(this, &UCookOnTheFlyServer::HandleNetworkGetPrecookedList));


	FNetworkFileDelegateContainer NetworkFileDelegateContainer;
	NetworkFileDelegateContainer.NewConnectionDelegate = NewConnectionDelegate;
	NetworkFileDelegateContainer.InitialPrecookedListDelegate = InitialPrecookedListDelegate;
	NetworkFileDelegateContainer.FileRequestDelegate = FileRequestDelegate;
	NetworkFileDelegateContainer.RecompileShadersDelegate = RecompileShadersDelegate;
	NetworkFileDelegateContainer.SandboxPathOverrideDelegate = SandboxPathDelegate;
	
	NetworkFileDelegateContainer.OnFileModifiedCallback = &FileModifiedDelegate;


	INetworkFileServer *TcpFileServer = FModuleManager::LoadModuleChecked<INetworkFileSystemModule>("NetworkFileSystem")
		.CreateNetworkFileServer(true, BindAnyPort ? 0 : -1, NetworkFileDelegateContainer, ENetworkFileServerProtocol::NFSP_Tcp);
	if ( TcpFileServer )
	{
		NetworkFileServers.Add(TcpFileServer);
	}

#if 0 // cookonthefly server via http
	INetworkFileServer *HttpFileServer = FModuleManager::LoadModuleChecked<INetworkFileSystemModule>("NetworkFileSystem")
		.CreateNetworkFileServer(true, BindAnyPort ? 0 : -1, NetworkFileDelegateContainer, ENetworkFileServerProtocol::NFSP_Http);
	if ( HttpFileServer )
	{
		NetworkFileServers.Add( HttpFileServer );
	}
#endif

	PackageTracker->CookRequestEvent = FPlatformProcess::GetSynchEventFromPool();

	// loop while waiting for requests
	return true;
}

ITargetPlatform* UCookOnTheFlyServer::AddCookOnTheFlyPlatform(const FString& PlatformName)
{
	ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();
	ITargetPlatform* TargetPlatform = TPM.FindTargetPlatform(PlatformName);
	if (TargetPlatform == nullptr)
	{
		UE_LOG(LogCook, Warning, TEXT("Target platform %s wasn't found."), *PlatformName);
		return nullptr;
	}
	if (!AddCookOnTheFlyPlatform(TargetPlatform))
	{
		return nullptr;
	}
	return TargetPlatform;
}

bool UCookOnTheFlyServer::AddCookOnTheFlyPlatform(ITargetPlatform* TargetPlatform)
{
	check(IsCookOnTheFlyMode());

	check(TargetPlatform != nullptr);
	const FPlatformData* PlatformData = PlatformManager->GetPlatformData(TargetPlatform);
	if (PlatformData == nullptr)
	{
		UE_LOG(LogCook, Warning, TEXT("Target platform %s wasn't found in TargetPlatformManager."), *TargetPlatform->PlatformName());
		return false;
	}

	if (PlatformData->bIsSandboxInitialized)
	{
		// Platform has already been added by this function or by StartCookByTheBook
		return true;
	}

	if (IsInGameThread())
	{
		AddCookOnTheFlyPlatformFromGameThread(TargetPlatform);
	}
	else
	{
		// Registering a new platform is not thread safe; queue the command for TickCookOnTheSide to execute
		PackageTracker->AddTickCommand([this, TargetPlatform]() { AddCookOnTheFlyPlatformFromGameThread(TargetPlatform); });
		if (PackageTracker->CookRequestEvent)
		{
			PackageTracker->CookRequestEvent->Trigger();
		}
	}

	return true;
}

void UCookOnTheFlyServer::AddCookOnTheFlyPlatformFromGameThread(ITargetPlatform* TargetPlatform)
{
	check(!!(CookFlags & ECookInitializationFlags::GeneratedAssetRegistry)); // GenerateAssetRegistry should have been called in StartNetworkFileServer

	FPlatformData* PlatformData = PlatformManager->GetPlatformData(TargetPlatform);
	check(PlatformData != nullptr); // should have been checked by the caller
	if (PlatformData->bIsSandboxInitialized)
	{
		return;
	}

	TArrayView<ITargetPlatform* const> NewTargetPlatforms{ TargetPlatform };

	RefreshPlatformAssetRegistries(NewTargetPlatforms);
	InitializeSandbox(NewTargetPlatforms);
	InitializeTargetPlatforms(NewTargetPlatforms);

	// When cooking on the fly the full registry is saved at the beginning
	// in cook by the book asset registry is saved after the cook is finished
	FAssetRegistryGenerator* Generator = PlatformData->RegistryGenerator.Get();
	if (Generator)
	{
		Generator->SaveAssetRegistry(GetSandboxAssetRegistryFilename(), true);
	}
	check(PlatformData->bIsSandboxInitialized); // This should have been set by InitializeSandbox, and it is what we use to determine whether a platform has been initialized
}

bool UCookOnTheFlyServer::BroadcastFileserverPresence( const FGuid &InstanceId )
{
	
	TArray<FString> AddressStringList;

	for ( int i = 0; i < NetworkFileServers.Num(); ++i )
	{
		TArray<TSharedPtr<FInternetAddr> > AddressList;
		INetworkFileServer *NetworkFileServer = NetworkFileServers[i];
		if ((NetworkFileServer == NULL || !NetworkFileServer->IsItReadyToAcceptConnections() || !NetworkFileServer->GetAddressList(AddressList)))
		{
			LogCookerMessage( FString(TEXT("Failed to create network file server")), EMessageSeverity::Error );
			UE_LOG(LogCook, Error, TEXT("Failed to create network file server"));
			continue;
		}

		// broadcast our presence
		if (InstanceId.IsValid())
		{
			for (int32 AddressIndex = 0; AddressIndex < AddressList.Num(); ++AddressIndex)
			{
				AddressStringList.Add(FString::Printf( TEXT("%s://%s"), *NetworkFileServer->GetSupportedProtocol(),  *AddressList[AddressIndex]->ToString(true)));
			}

		}
	}

	TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> MessageEndpoint = FMessageEndpoint::Builder("UCookOnTheFlyServer").Build();

	if (MessageEndpoint.IsValid())
	{
		MessageEndpoint->Publish(new FFileServerReady(AddressStringList, InstanceId), EMessageScope::Network);
	}		
	
	return true;
}

/*----------------------------------------------------------------------------
	FArchiveFindReferences.
----------------------------------------------------------------------------*/
/**
 * Archive for gathering all the object references to other objects
 */
class FArchiveFindReferences : public FArchiveUObject
{
private:
	/**
	 * I/O function.  Called when an object reference is encountered.
	 *
	 * @param	Obj		a pointer to the object that was encountered
	 */
	FArchive& operator<<( UObject*& Obj ) override
	{
		if( Obj )
		{
			FoundObject( Obj );
		}
		return *this;
	}

	virtual FArchive& operator<< (struct FSoftObjectPtr& Value) override
	{
		if ( Value.Get() )
		{
			Value.Get()->Serialize( *this );
		}
		return *this;
	}
	virtual FArchive& operator<< (struct FSoftObjectPath& Value) override
	{
		if ( Value.ResolveObject() )
		{
			Value.ResolveObject()->Serialize( *this );
		}
		return *this;
	}


	void FoundObject( UObject* Object )
	{
		if ( RootSet.Find(Object) == NULL )
		{
			if ( Exclude.Find(Object) == INDEX_NONE )
			{
				// remove this check later because don't want this happening in development builds
				//check(RootSetArray.Find(Object)==INDEX_NONE);

				RootSetArray.Add( Object );
				RootSet.Add(Object);
				Found.Add(Object);
			}
		}
	}


	/**
	 * list of Outers to ignore;  any objects encountered that have one of
	 * these objects as an Outer will also be ignored
	 */
	TArray<UObject*> &Exclude;

	/** list of objects that have been found */
	TSet<UObject*> &Found;
	
	/** the objects to display references to */
	TArray<UObject*> RootSetArray;
	/** Reflection of the rootsetarray */
	TSet<UObject*> RootSet;

public:

	/**
	 * Constructor
	 * 
	 * @param	inOutputAr		archive to use for logging results
	 * @param	inOuter			only consider objects that do not have this object as its Outer
	 * @param	inSource		object to show references for
	 * @param	inExclude		list of objects that should be ignored if encountered while serializing SourceObject
	 */
	FArchiveFindReferences( TSet<UObject*> InRootSet, TSet<UObject*> &inFound, TArray<UObject*> &inExclude )
		: Exclude(inExclude)
		, Found(inFound)
		, RootSet(InRootSet)
	{
		ArIsObjectReferenceCollector = true;
		this->SetIsSaving(true);

		for ( UObject* Object : RootSet )
		{
			RootSetArray.Add( Object );
		}
		
		// loop through all the objects in the root set and serialize them
		for ( int RootIndex = 0; RootIndex < RootSetArray.Num(); ++RootIndex )
		{
			UObject* SourceObject = RootSetArray[RootIndex];

			// quick sanity check
			check(SourceObject);
			check(SourceObject->IsValidLowLevel());

			SourceObject->Serialize( *this );
		}

	}

	/**
	 * Returns the name of the Archive.  Useful for getting the name of the package a struct or object
	 * is in when a loading error occurs.
	 *
	 * This is overridden for the specific Archive Types
	 **/
	virtual FString GetArchiveName() const override { return TEXT("FArchiveFindReferences"); }
};

void UCookOnTheFlyServer::GetDependentPackages(const TSet<UPackage*>& RootPackages, TSet<FName>& FoundPackages)
{
	TSet<FName> RootPackageFNames;
	for (const UPackage* RootPackage : RootPackages)
	{
		RootPackageFNames.Add(RootPackage->GetFName());
	}


	GetDependentPackages(RootPackageFNames, FoundPackages);

}


void UCookOnTheFlyServer::GetDependentPackages( const TSet<FName>& RootPackages, TSet<FName>& FoundPackages )
{
	TArray<FName> FoundPackagesArray;
	for (const FName& RootPackage : RootPackages)
	{
		FoundPackagesArray.Add(RootPackage);
		FoundPackages.Add(RootPackage);
	}

	int FoundPackagesCounter = 0;
	while ( FoundPackagesCounter < FoundPackagesArray.Num() )
	{
		TArray<FName> PackageDependencies;
		if (AssetRegistry->GetDependencies(FoundPackagesArray[FoundPackagesCounter], PackageDependencies) == false)
		{
			// this could happen if we are in the editor and the dependency list is not up to date

			if (IsCookingInEditor() == false)
			{
				UE_LOG(LogCook, Fatal, TEXT("Unable to find package %s in asset registry.  Can't generate cooked asset registry"), *FoundPackagesArray[FoundPackagesCounter].ToString());
			}
			else
			{
				UE_LOG(LogCook, Warning, TEXT("Unable to find package %s in asset registry, cooked asset registry information may be invalid "), *FoundPackagesArray[FoundPackagesCounter].ToString());
			}
		}
		++FoundPackagesCounter;
		for ( const FName& OriginalPackageDependency : PackageDependencies )
		{
			// check(PackageDependency.ToString().StartsWith(TEXT("/")));
			FName PackageDependency = OriginalPackageDependency;
			FString PackageDependencyString = PackageDependency.ToString();

			FText OutReason;
			const bool bIncludeReadOnlyRoots = true; // Dependency packages are often script packages (read-only)
			if (!FPackageName::IsValidLongPackageName(PackageDependencyString, bIncludeReadOnlyRoots, &OutReason))
			{
				const FText FailMessage = FText::Format(LOCTEXT("UnableToGeneratePackageName", "Unable to generate long package name for {0}. {1}"),
					FText::FromString(PackageDependencyString), OutReason);

				LogCookerMessage(FailMessage.ToString(), EMessageSeverity::Warning);
				UE_LOG(LogCook, Warning, TEXT("%s"), *( FailMessage.ToString() ));
				continue;
			}
			else if (FPackageName::IsScriptPackage(PackageDependencyString) || FPackageName::IsMemoryPackage(PackageDependencyString))
			{
				continue;
			}

			if ( FoundPackages.Contains(PackageDependency) == false )
			{
				FoundPackages.Add(PackageDependency);
				FoundPackagesArray.Add( PackageDependency );
			}
		}
	}

}

void UCookOnTheFlyServer::GetDependencies( const TSet<UPackage*>& Packages, TSet<UObject*>& Found)
{
	TSet<UObject*> RootSet;

	for (UPackage* Package : Packages)
	{
		TArray<UObject*> ObjectsInPackage;
		GetObjectsWithOuter(Package, ObjectsInPackage, true);
		for (UObject* Obj : ObjectsInPackage)
		{
			RootSet.Add(Obj);
			Found.Add(Obj);
		}
	}

	TArray<UObject*> Exclude;
	FArchiveFindReferences ArFindReferences( RootSet, Found, Exclude );
}

bool UCookOnTheFlyServer::ContainsMap(const FName& PackageName) const
{
	TArray<FAssetData> Assets;
	ensure(AssetRegistry->GetAssetsByPackageName(PackageName, Assets, true));

	for (const FAssetData& Asset : Assets)
	{
		if (Asset.GetClass()->IsChildOf(UWorld::StaticClass()) || Asset.GetClass()->IsChildOf(ULevel::StaticClass()))
		{
			return true;
		}
	}
	return false;
}

bool UCookOnTheFlyServer::ContainsRedirector(const FName& PackageName, TMap<FName, FName>& RedirectedPaths) const
{
	bool bFoundRedirector = false;
	TArray<FAssetData> Assets;
	ensure(AssetRegistry->GetAssetsByPackageName(PackageName, Assets, true));

	for (const FAssetData& Asset : Assets)
	{
		if (Asset.IsRedirector())
		{
			FName RedirectedPath;
			FString RedirectedPathString;
			if (Asset.GetTagValue("DestinationObject", RedirectedPathString))
			{
				ConstructorHelpers::StripObjectClass(RedirectedPathString);
				RedirectedPath = FName(*RedirectedPathString);
				FAssetData DestinationData = AssetRegistry->GetAssetByObjectPath(RedirectedPath, true);
				TSet<FName> SeenPaths;

				SeenPaths.Add(RedirectedPath);

				// Need to follow chain of redirectors
				while (DestinationData.IsRedirector())
				{
					if (DestinationData.GetTagValue("DestinationObject", RedirectedPathString))
					{
						ConstructorHelpers::StripObjectClass(RedirectedPathString);
						RedirectedPath = FName(*RedirectedPathString);

						if (SeenPaths.Contains(RedirectedPath))
						{
							// Recursive, bail
							DestinationData = FAssetData();
						}
						else
						{
							SeenPaths.Add(RedirectedPath);
							DestinationData = AssetRegistry->GetAssetByObjectPath(RedirectedPath, true);
						}
					}
					else
					{
						// Can't extract
						DestinationData = FAssetData();						
					}
				}

				// DestinationData may be invalid if this is a subobject, check package as well
				bool bDestinationValid = DestinationData.IsValid();

				if (!bDestinationValid)
				{
					// we can;t call GetCachedStandardPackageFileFName with None
					if (RedirectedPath != NAME_None)
					{
						FName StandardPackageName = PackageNameCache->GetCachedStandardPackageFileFName(FName(*FPackageName::ObjectPathToPackageName(RedirectedPathString)));
						if (StandardPackageName != NAME_None)
						{
							bDestinationValid = true;
						}
					}
				}

				if (bDestinationValid)
				{
					RedirectedPaths.Add(Asset.ObjectPath, RedirectedPath);
				}
				else
				{
					RedirectedPaths.Add(Asset.ObjectPath, NAME_None);
					UE_LOG(LogCook, Log, TEXT("Found redirector in package %s pointing to deleted object %s"), *PackageName.ToString(), *RedirectedPathString);
				}

				bFoundRedirector = true;
			}
		}
	}
	return bFoundRedirector;
}

bool UCookOnTheFlyServer::IsCookingInEditor() const
{
	return CurrentCookMode == ECookMode::CookByTheBookFromTheEditor || CurrentCookMode == ECookMode::CookOnTheFlyFromTheEditor;
}

bool UCookOnTheFlyServer::IsRealtimeMode() const 
{
	return CurrentCookMode == ECookMode::CookByTheBookFromTheEditor || CurrentCookMode == ECookMode::CookOnTheFlyFromTheEditor;
}

bool UCookOnTheFlyServer::IsCookByTheBookMode() const
{
	return CurrentCookMode == ECookMode::CookByTheBookFromTheEditor || CurrentCookMode == ECookMode::CookByTheBook;
}

bool UCookOnTheFlyServer::IsUsingShaderCodeLibrary() const
{
	return IsCookByTheBookMode();
}

bool UCookOnTheFlyServer::IsUsingPackageStore() const
{
	return IsCookByTheBookMode() && CookByTheBookOptions->bPackageStore;
}

bool UCookOnTheFlyServer::IsCookOnTheFlyMode() const
{
	return CurrentCookMode == ECookMode::CookOnTheFly || CurrentCookMode == ECookMode::CookOnTheFlyFromTheEditor; 
}

bool UCookOnTheFlyServer::IsCreatingReleaseVersion()
{
	if (CookByTheBookOptions)
	{
		return !CookByTheBookOptions->CreateReleaseVersion.IsEmpty();
	}

	return false;
}

bool UCookOnTheFlyServer::IsCookingDLC() const
{
	// can only cook DLC in cook by the book
	// we are cooking DLC when the DLC name is setup
	
	if (CookByTheBookOptions)
	{
		return !CookByTheBookOptions->DlcName.IsEmpty();
	}

	return false;
}

FString UCookOnTheFlyServer::GetBaseDirectoryForDLC() const
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(CookByTheBookOptions->DlcName);
	if (Plugin.IsValid())
	{
		return Plugin->GetBaseDir();
	}

	return FPaths::ProjectPluginsDir() / CookByTheBookOptions->DlcName;
}

FString UCookOnTheFlyServer::GetContentDirecctoryForDLC() const
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(CookByTheBookOptions->DlcName);
	check(Plugin.IsValid());
	return Plugin->GetContentDir();
}

COREUOBJECT_API extern bool GOutputCookingWarnings;

void UCookOnTheFlyServer::WaitForRequests(int TimeoutMs)
{
	if (PackageTracker->CookRequestEvent)
	{
		PackageTracker->CookRequestEvent->Wait(TimeoutMs, true);
	}
}

bool UCookOnTheFlyServer::HasCookRequests() const
{ 
	return PackageTracker->HasRequests();
}

bool UCookOnTheFlyServer::RequestPackage(const FName& StandardPackageFName, const TArray<const ITargetPlatform*>& TargetPlatforms, const bool bForceFrontOfQueue)
{
	FFilePlatformRequest FileRequest(StandardPackageFName, TargetPlatforms);
	PackageTracker->EnqueueUniqueCookRequest(FileRequest, bForceFrontOfQueue);
	return true;
}

bool UCookOnTheFlyServer::RequestPackage(const FName& StandardPackageFName, const TArray<FName>& TargetPlatformNames, const bool bForceFrontOfQueue)
{
	TArray<const ITargetPlatform*> TargetPlatforms;
	ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();
	for (const FName& TargetPlatformName : TargetPlatformNames)
	{
		const ITargetPlatform* TargetPlatform = TPM.FindTargetPlatform(TargetPlatformName.ToString());
		if (TargetPlatform)
		{
			TargetPlatforms.Add(TargetPlatform);
		}
	}
	return RequestPackage(StandardPackageFName, TargetPlatforms, bForceFrontOfQueue);
}

bool UCookOnTheFlyServer::RequestPackage(const FName& StandardPackageFName, const bool bForceFrontOfQueue)
{
	check(IsCookByTheBookMode()); // Invalid to call RequestPackage without a list of TargetPlatforms unless we are in cook by the book mode
	return RequestPackage(StandardPackageFName, PlatformManager->GetSessionPlatforms(), bForceFrontOfQueue);
}

// callback just before the garbage collector gets called.
void UCookOnTheFlyServer::PreGarbageCollect()
{
	PackageReentryData.Empty();
}

UCookOnTheFlyServer::FReentryData& UCookOnTheFlyServer::GetReentryData(const UPackage* Package) const
{
	FReentryData& CurrentReentryData = PackageReentryData.FindOrAdd(Package->GetFName());

	if ((CurrentReentryData.bIsValid == false) && (Package->IsFullyLoaded() == true))
	{
		CurrentReentryData.bIsValid = true;
		CurrentReentryData.FileName = Package->GetFName();
		GetObjectsWithOuter(Package, CurrentReentryData.CachedObjectsInOuter);
	}
	return CurrentReentryData;
}


uint32 UCookOnTheFlyServer::TickCookOnTheSide(const float TimeSlice, uint32 &CookedPackageCount, ECookTickFlags TickFlags)
{
	TickNetwork();

	if (IsCookByTheBookMode() && CookByTheBookOptions->bRunning && CookByTheBookOptions->bFullLoadAndSave)
	{
		uint32 Result = FullLoadAndSave(CookedPackageCount);

		CookByTheBookFinished();

		return Result;
	}

	COOK_STAT(FScopedDurationTimer TickTimer(DetailedCookStats::TickCookOnTheSideTimeSec));
	FCookerTimer Timer(TimeSlice, IsRealtimeMode());

	uint32 Result = 0;

	{
		if (AssetRegistry == nullptr || AssetRegistry->IsLoadingAssets())
		{
			// early out
			return Result;
		}
	}

	while (!IsEngineExitRequested() || CurrentCookMode == ECookMode::CookByTheBook)
	{
		// if we just cooked a map then don't process anything the rest of this tick
		if (Result & COSR_RequiresGC)
		{
			break;
		}

		if (IsCookByTheBookMode())
		{
			check(CookByTheBookOptions);
			if (CookByTheBookOptions->bCancel)
			{
				CancelCookByTheBook();
			}
		}

		FFilePlatformRequest ToBuild;
		TArray<FPackageTracker::FTickCommand> TickCommands;
		FPackageTracker::ERequestType RequestType;
		while (true)
		{
			RequestType = PackageTracker->DequeueRequest(TickCommands, ToBuild);
			if (RequestType == FPackageTracker::ERequestType::Cook && this->PackageTracker->CookedPackages.Exists(ToBuild))
			{
#if DEBUG_COOKONTHEFLY
				UE_LOG(LogCook, Display, TEXT("Package for platform already cooked %s, discarding request"), *OutToBuild.GetFilename().ToString());
#endif
				continue;
			}
			break;
		}

		if (RequestType == FPackageTracker::ERequestType::None)
		{
			// no more real work to do this tick, break out and do some other stuff
			break;
		}
		else if (RequestType == FPackageTracker::ERequestType::TickCommand)
		{
			// An array of TickCommands to process; execute through them all and then continue
			for (FPackageTracker::FTickCommand& TickCommand : TickCommands)
			{
				TickCommand();
			}
			continue;
		}
		check(RequestType == FPackageTracker::ERequestType::Cook && ToBuild.IsValid());

		const float CurrentProgressDisplayTime = FPlatformTime::Seconds();

		const int32 CookRequestsNum = PackageTracker->GetCookRequestsNum() + 1; // + 1 to include the request we just dequeued
		if (LastCookedPackagesCount != PackageTracker->CookedPackages.Num()
			|| LastCookRequestsCount != CookRequestsNum
			|| (CurrentProgressDisplayTime - LastProgressDisplayTime) > GCookProgressRepeatTime)
		{
			UE_CLOG(!(TickFlags & ECookTickFlags::HideProgressDisplay) && (GCookProgressDisplay & (int32)ECookProgressDisplayMode::RemainingPackages),
				LogCook,
				Display,
				TEXT("Cooked packages %d Packages Remain %d Total %d"),
				PackageTracker->CookedPackages.Num(),
				CookRequestsNum,
				PackageTracker->CookedPackages.Num() + CookRequestsNum);

			LastCookedPackagesCount = PackageTracker->CookedPackages.Num();
			LastCookRequestsCount = CookRequestsNum;
			LastProgressDisplayTime = CurrentProgressDisplayTime;
		}

#if PROFILE_NETWORK
		if (NetworkRequestEvent)
		{
			NetworkRequestEvent->Trigger();
		}
#endif

		// prevent autosave from happening until we are finished cooking
		// causes really bad hitches
		if (GUnrealEd)
		{
			const static float SecondsWarningTillAutosave = 10.0f;
			GUnrealEd->GetPackageAutoSaver().ForceMinimumTimeTillAutoSave(SecondsWarningTillAutosave);
		}

#if DEBUG_COOKONTHEFLY
		UE_LOG(LogCook, Display, TEXT("Processing package %s"), *ToBuild.GetFilename().ToString());
#endif
		SCOPE_TIMER(TickCookOnTheSide);

		check(ToBuild.IsValid());
		const TArray<const ITargetPlatform*>& TargetPlatforms = ToBuild.GetPlatforms();
		if (TargetPlatforms.Num() == 0)
		{
			UE_LOG(LogCook, Error, TEXT("Empty list of platforms requested in CookOnTheSide request."));
			continue;
		}

#if OUTPUT_TIMING
		//FScopeTimer PackageManualTimer( ToBuild.GetFilename().ToString(), false );
#endif

		const FString BuildFilename = ToBuild.GetFilename().ToString();

		bool bShouldCook = true;

		if (CookByTheBookOptions && CookByTheBookOptions->bErrorOnEngineContentUse)
		{
			check(IsCookingDLC());
			FString DLCPath = FPaths::Combine(*GetBaseDirectoryForDLC(), TEXT("Content"));
			FPaths::MakeStandardFilename(DLCPath);
			if (ToBuild.GetFilename().ToString().StartsWith(DLCPath) == false) // if we don't start with the dlc path then we shouldn't be cooking this data 
			{
				UE_LOG(LogCook, Error, TEXT("Engine or Game content %s is being referenced by DLC!"), *ToBuild.GetFilename().ToString());
				bShouldCook = false;
			}
		}

		check(IsInGameThread());
		if (PackageTracker->NeverCookPackageList.Contains(ToBuild.GetFilename()))
		{
#if DEBUG_COOKONTHEFLY
			UE_LOG(LogCook, Display, TEXT("Package %s requested but is in the never cook package list, discarding request"), *ToBuild.GetFilename().ToString());
#endif
			bShouldCook = false;
		}

		UPackage* LoadedPackage = nullptr;
		UPackage* PackageForCooking = nullptr;

		if (bShouldCook) // if we should cook the package then cook it otherwise add it to the list of already cooked packages below
		{
			bool bLoadFullySuccessful = LoadPackageForCooking(BuildFilename, LoadedPackage);
			if (bLoadFullySuccessful)
			{
				FString Name = LoadedPackage->GetPathName();
				FString PackageFilename(PackageNameCache->GetCachedStandardPackageFilename(LoadedPackage));
				if (PackageFilename != BuildFilename)
				{
					// we have saved something which we didn't mean to load 
					//  sounds unpossible.... but it is due to searching for files and such
					//  mark the original request as processed (if this isn't actually the file they were requesting then it will fail)
					//	and then also save our new request as processed so we don't do it again
					UE_LOG(LogCook, Verbose, TEXT("Request for %s received going to save %s"), *BuildFilename, *PackageFilename);

					PackageTracker->OnPackageCooked(FFilePlatformCookedPackage(ToBuild.GetFilename(), TargetPlatforms), LoadedPackage);

					ToBuild.SetFilename(PackageFilename);
				}

				PackageForCooking = LoadedPackage;
			}
			else
			{
				Result |= COSR_ErrorLoadingPackage;
			}
		}


		if (PackageForCooking == nullptr)
		{
			// if we are iterative cooking the package might already be cooked
			// so just add the package to the cooked packages list
			// this could also happen if the source file doesn't exist which is often as we request files with different extensions when we are searching for files
			// just return that we processed the cook request
			// the network file manager will then handle the missing file and search somewhere else
			UE_LOG(LogCook, Verbose, TEXT("Not cooking package %s"), *ToBuild.GetFilename().ToString());

			// did not cook this package 
#if DO_CHECK
			// make sure this package doesn't exist
			for (const ITargetPlatform* TargetPlatform: ToBuild.GetPlatforms())
			{
				const FString SandboxFilename = ConvertToFullSandboxPath(ToBuild.GetFilename().ToString(), true, TargetPlatform->PlatformName());
				if (IFileManager::Get().FileExists(*SandboxFilename))
				{
					// if we find the file this means it was cooked on a previous cook, however source package can't be found now. 
					// this could be because the source package was deleted or renamed, and we are using iterative cooking
					// perhaps in this case we should delete it?
					UE_LOG(LogCook, Warning, TEXT("Found cooked file which shouldn't exist as it failed loading %s"), *SandboxFilename);
					IFileManager::Get().Delete(*SandboxFilename);
				}
			}
#endif
			PackageTracker->OnPackageCooked(FFilePlatformCookedPackage(ToBuild.GetFilename(), TargetPlatforms), LoadedPackage);
			continue;
		}

		bool bIsAllDataCached = true;

		GShaderCompilingManager->ProcessAsyncResults(true, false);


		if (PackageForCooking)
		{
			SCOPE_TIMER(CallBeginCacheForCookedPlatformData);
			// cache the resources for this package for each platform

			bIsAllDataCached &= BeginPackageCacheForCookedPlatformData(PackageForCooking, TargetPlatforms, Timer);

			if (bIsAllDataCached)
			{
				bIsAllDataCached &= FinishPackageCacheForCookedPlatformData(PackageForCooking, TargetPlatforms, Timer);
			}
		}


		bool ShouldTickPrecache = true;

		// if we are ready to save then don't waste time precaching other stuff
		if (bIsAllDataCached == true)
		{
			ShouldTickPrecache = false;
		}
		// don't do this if we are in a commandlet because the save section will prefetch 
		if (!IsRealtimeMode())
		{
			ShouldTickPrecache = false;
		}
		else
		{
			// if we are doing no shader compilation right now try and precache something so that we load up the cpu
			if (GShaderCompilingManager->GetNumRemainingJobs() == 0)
			{
				ShouldTickPrecache = true;
			}
		}

		// cook on the fly mode we don't want to precache here because save package is going to stall on this package, we don't want to flood the system with precache requests before we stall
		if (IsCookOnTheFlyMode())
		{
			ShouldTickPrecache = false;
		}

		// if we are in the cook commandlet then this data will get cached in the save package section
		// if ( (bIsAllDataCached == false) && IsRealtimeMode())
		if (ShouldTickPrecache)
		{
			double PrecacheTimeSlice = Timer.GetTimeRemain();
			if (PrecacheTimeSlice > 0.0f)
			{
				TickPrecacheObjectsForPlatforms(PrecacheTimeSlice, TargetPlatforms);
			}
		}

		ProcessUnsolicitedPackages();

		if (!bIsAllDataCached)
		{
			// If we are waiting on asynchronous bulk data builds (e.g. shader compile) then if we proceed to SaveCookedPackages we will have to wastefully wait for that asynchronous work to complete.
			// Instead, when possible, requeue the package, working on other packages before we get back to it
			if (IsCookByTheBookMode() && // We only allow reordering if we're doing a CookByTheBook; in CookOnTheFly we care urgently about the currently requested package
				!IsRealtimeMode() && // In RealTimeMode (e.g. running in editor) we don't requeue.  TODO Daniel Lamb: Why do we not requeue in realtime?
				!(Result & COSR_RequiresGC) && // GC1 Don't requeue if we are about to garbage collect, we would then waste work reloading this package
				!HasExceededMaxMemory() && // GC2
				Timer.NumPackagesSaved + PackageTracker->GetPackagesPendingSave().Num() < Timer.MaxNumPackagesToSave // GC3
				)
			{
				// Given the list of constraints we just checked, we know we're not in a rush on any particular package, and we have some async results to process, so take the time to prcoess them now
				GShaderCompilingManager->ProcessAsyncResults(true, false); 
				// If all of our requested packages are missing cached data we might keep requeueing them forever.
				// To prevent an infinite loop, call Timer.SavedPackage so that we eventually fail the Timer.NumPackagesSaved < Timer.MaxNumPackagesToSave check above and break out of the loop.
				Timer.SavedPackage();  
				// Requeue the new package and move on the next one in the queue
				PackageTracker->EnqueueUniqueCookRequest(ToBuild, false);
				continue;
			}
		}

		SaveCookedPackages(PackageForCooking, TargetPlatforms, Timer, /* out */ CookedPackageCount, /* out */ Result);

		if (Timer.IsTimeUp())
		{
			break;
		}
	}


	if (IsCookOnTheFlyMode() && (IsCookingInEditor() == false))
	{
		static int32 TickCounter = 0;
		++TickCounter;
		if (TickCounter > 50)
		{
			// dump stats every 50 ticks or so
			DumpStats();
			TickCounter = 0;
		}
	}

	if (CookByTheBookOptions)
	{
		CookByTheBookOptions->CookTime += Timer.GetTimeTillNow();
	}

	if (IsCookByTheBookRunning() && !HasCookRequests())
	{
		check(IsCookByTheBookMode());

		// if we are out of stuff and we are in cook by the book from the editor mode then we finish up
		UE_CLOG(!(TickFlags & ECookTickFlags::HideProgressDisplay) && (GCookProgressDisplay & (int32)ECookProgressDisplayMode::RemainingPackages),
			LogCook, Display, TEXT("Cooked packages %d Packages Remain %d Total %d"),
			PackageTracker->CookedPackages.Num(), 0, PackageTracker->CookedPackages.Num());
		CookByTheBookFinished();
	}

	return Result;
}

void UCookOnTheFlyServer::TickNetwork()
{
	if (!PackageTracker)
		return;

	if (!IsCookByTheBookMode()) // StartCookByTheBook does not AddRef its session platforms, so it is not safe to prune unreferenced session platforms when supporting CookByTheBook
	{
		PlatformManager->PruneUnreferencedSessionPlatforms(*PackageTracker);
	}
}

bool UCookOnTheFlyServer::BeginPackageCacheForCookedPlatformData(UPackage* Package, const TArrayView<const ITargetPlatform* const>& TargetPlatforms, FCookerTimer& Timer) const
{
	COOK_STAT(FScopedDurationTimer DurationTimer(DetailedCookStats::TickCookOnTheSideBeginPackageCacheForCookedPlatformDataTimeSec));

#if DEBUG_COOKONTHEFLY 
	UE_LOG(LogCook, Display, TEXT("Caching objects for package %s"), *Package->GetFName().ToString());
#endif
	MakePackageFullyLoaded(Package);
	FReentryData& CurrentReentryData = GetReentryData(Package);

	if (CurrentReentryData.bIsValid == false)
	{
		return true;
	}

	if (CurrentReentryData.bBeginCacheFinished)
	{
		return true;
	}

	for (; CurrentReentryData.BeginCacheCount < CurrentReentryData.CachedObjectsInOuter.Num(); ++CurrentReentryData.BeginCacheCount)
	{
		UObject* Obj = CurrentReentryData.CachedObjectsInOuter[CurrentReentryData.BeginCacheCount];
		for (const ITargetPlatform* TargetPlatform : TargetPlatforms)
		{
			const FName ClassFName = Obj->GetClass()->GetFName();
			int32* CurrentAsyncCache = CurrentAsyncCacheForType.Find(ClassFName);
			if ( CurrentAsyncCache != nullptr )
			{
				if ( *CurrentAsyncCache <= 0 )
				{
					return false;
				}

				int32* Value = CurrentReentryData.BeginCacheCallCount.Find(ClassFName);
				if ( !Value )
				{
					CurrentReentryData.BeginCacheCallCount.Add(ClassFName,1);
				}
				else
				{
					*Value += 1;
				}
				*CurrentAsyncCache -= 1;
			}

			if (Obj->IsA(UMaterialInterface::StaticClass()))
			{
				if (GShaderCompilingManager->GetNumRemainingJobs() > MaxConcurrentShaderJobs)
				{
#if DEBUG_COOKONTHEFLY
					UE_LOG(LogCook, Display, TEXT("Delaying shader compilation of material %s"), *Obj->GetFullName());
#endif
					return false;
				}
			}
			Obj->BeginCacheForCookedPlatformData(TargetPlatform);
		}

		if (Timer.IsTimeUp())
		{
#if DEBUG_COOKONTHEFLY
			UE_LOG(LogCook, Display, TEXT("Object %s took too long to cache"), *Obj->GetFullName());
#endif
			return false;
		}
	}

	CurrentReentryData.bBeginCacheFinished = true;
	return true;

}

bool UCookOnTheFlyServer::FinishPackageCacheForCookedPlatformData(UPackage* Package, const TArrayView<const ITargetPlatform* const>& TargetPlatforms, FCookerTimer& Timer) const
{
	COOK_STAT(FScopedDurationTimer DurationTimer(DetailedCookStats::TickCookOnTheSideFinishPackageCacheForCookedPlatformDataTimeSec));

	MakePackageFullyLoaded(Package);
	FReentryData& CurrentReentryData = GetReentryData(Package);

	if (CurrentReentryData.bIsValid == false)
	{
		return true;
	}

	if (CurrentReentryData.bFinishedCacheFinished)
	{
		return true;
	}

	for (UObject* Obj : CurrentReentryData.CachedObjectsInOuter)
	{
		for (const ITargetPlatform* TargetPlatform : TargetPlatforms)
		{
			COOK_STAT(double CookerStatSavedValue = DetailedCookStats::TickCookOnTheSideBeginPackageCacheForCookedPlatformDataTimeSec);

			if (Obj->IsA(UMaterialInterface::StaticClass()))
			{
				if (Obj->IsCachedCookedPlatformDataLoaded(TargetPlatform) == false)
				{
					if (GShaderCompilingManager->GetNumRemainingJobs() > MaxConcurrentShaderJobs)
					{
						return false;
					}
				}
			}

			// These begin cache calls should be quick 
			// because they will just be checking that the data is already cached and kicking off new multithreaded requests if not
			// all sync requests should have been caught in the first begincache call above
			Obj->BeginCacheForCookedPlatformData(TargetPlatform);
			// We want to measure inclusive time for this function, but not accumulate into the BeginXXX timer, so subtract these times out of the BeginTimer.
			COOK_STAT(DetailedCookStats::TickCookOnTheSideBeginPackageCacheForCookedPlatformDataTimeSec = CookerStatSavedValue);
			if (Obj->IsCachedCookedPlatformDataLoaded(TargetPlatform) == false)
			{
#if DEBUG_COOKONTHEFLY
				UE_LOG(LogCook, Display, TEXT("Object %s isn't cached yet"), *Obj->GetFullName());
#endif
				/*if ( Obj->IsA(UMaterial::StaticClass()) )
				{
				if (GShaderCompilingManager->HasShaderJobs() == false)
				{
				UE_LOG(LogCook, Warning, TEXT("Shader compiler is in a bad state!  Shader %s is finished compile but shader compiling manager did not notify shader.  "), *Obj->GetPathName());
				}
				}*/
				return false;
			}
		}
	}

	if (CurrentCookMode == ECookMode::CookByTheBook)
	{
		// For each object for which data is cached we can call FinishedCookedPlatformDataCache
		// we can only safely call this when we are finished caching the object completely.
		// this doesn't ever happen for cook in editor or cook on the fly mode
		for (UObject* Obj : CurrentReentryData.CachedObjectsInOuter)
		{
			check(!IsCookingInEditor());
			// this might be run multiple times for a single object
			Obj->WillNeverCacheCookedPlatformDataAgain();
		}
	}

	// all these objects have finished so release their async begincache back to the pool
	for (const auto& FinishedCached : CurrentReentryData.BeginCacheCallCount )
	{
		int32* Value = CurrentAsyncCacheForType.Find( FinishedCached.Key );
		check( Value);
		*Value += FinishedCached.Value;
	}
	CurrentReentryData.BeginCacheCallCount.Empty();

	CurrentReentryData.bFinishedCacheFinished = true;
	return true;
}

bool UCookOnTheFlyServer::LoadPackageForCooking(const FString& BuildFilename, UPackage*& OutPackage)
{
	COOK_STAT(FScopedDurationTimer LoadPackagesTimer(DetailedCookStats::TickCookOnTheSideLoadPackagesTimeSec));
	OutPackage = NULL;
	FString PackageNameString;
	if (FPackageName::TryConvertFilenameToLongPackageName(BuildFilename, PackageNameString))
	{
		OutPackage = FindObject<UPackage>(ANY_PACKAGE, *PackageNameString);
	}

#if DEBUG_COOKONTHEFLY
	UE_LOG(LogCook, Display, TEXT("Processing request %s"), *BuildFilename);
#endif
	static TSet<FString> CookWarningsList;
	if (CookWarningsList.Contains(BuildFilename) == false)
	{
		CookWarningsList.Add(BuildFilename);
		GOutputCookingWarnings = IsCookFlagSet(ECookInitializationFlags::OutputVerboseCookerWarnings);
	}

	bool bSuccess = true;
	//  if the package is not yet fully loaded then fully load it
	if (OutPackage == nullptr || !OutPackage->IsFullyLoaded())
	{
		GIsCookerLoadingPackage = true;
		SCOPE_TIMER(LoadPackage);
		UPackage* LoadedPackage = LoadPackage(NULL, *BuildFilename, LOAD_None);
		if (LoadedPackage)
		{
			OutPackage = LoadedPackage;
		}
		else
		{
			bSuccess = false;
			if (OutPackage == nullptr)
			{
				// FindObject failed and LoadPackage failed, but there are some Packages that can exist in our PackageTracker that are not returned from FindPackage e.g. because they are async loading
				// If we are reporting an error on a Package, we are going to mark it as already cooked, and so if it is present in PendingSavePackages we need to be sure to remove it
				// So look for it in all PendingSavePackages if we are reporting an error and didn't find it in FindPackage
				FName PackageName = FName(PackageNameString);
				for (UPackage* PendingSavePackage : PackageTracker->GetPackagesPendingSave())
				{
					if (PendingSavePackage->GetFName() == PackageName)
					{
						OutPackage = PendingSavePackage;
						break;
					}
				}
			}
		}

		++this->StatLoadedPackageCount;

		GIsCookerLoadingPackage = false;
	}
#if DEBUG_COOKONTHEFLY
	else
	{
		UE_LOG(LogCook, Display, TEXT("Package already loaded %s avoiding reload"), *BuildFilename);
	}
#endif

	if (!bSuccess)
	{
		if ((!IsCookOnTheFlyMode()) || (!IsCookingInEditor()))
		{
			LogCookerMessage(FString::Printf(TEXT("Error loading %s!"), *BuildFilename), EMessageSeverity::Error);
			UE_LOG(LogCook, Error, TEXT("Error loading %s!"), *BuildFilename);
		}
	}
	GOutputCookingWarnings = false;
	return bSuccess;
}


void UCookOnTheFlyServer::ProcessUnsolicitedPackages()
{
	if (IsCookByTheBookMode() && CookByTheBookOptions->bDisableUnsolicitedPackages)
	{
		return;
	}
	// Ensure sublevels are loaded by iterating all recently loaded packages and invoking
	// PostLoadPackageFixup

	{
		SCOPE_TIMER(PostLoadPackageFixup);

		TArray<UPackage*> NewPackages = PackageTracker->GetNewPackages();

		for (UPackage* Package : NewPackages)
		{
			PostLoadPackageFixup(Package);
		}
	}
}

void UCookOnTheFlyServer::SaveCookedPackages(
	UPackage*								PackageToSave, 
	const TArray<const ITargetPlatform*>&	InTargetPlatforms,
	FCookerTimer&							Timer,
	uint32&									CookedPackageCount,
	uint32&									Result)
{
	check(IsInGameThread());

	bool bIsAllDataCached = true;

	// To make transitioning from the old way of managing unsolicited assets
	// easier, we construct a temporary array here with the PackageToSave as 
	// the first entry

	auto& PendingSet = PackageTracker->GetPackagesPendingSave();

	constexpr uint32 ExpectedMaxNumUnsolicitedPlatforms = 10;
	TArray<UPackage*, TInlineAllocator<ExpectedMaxNumUnsolicitedPlatforms>> PackagesToSave;
	PackagesToSave.Reserve(PendingSet.Num() + 1);
	int32 FirstUnsolicitedPackage = 0;
	if (PackageToSave)
	{
		PackagesToSave.Add(PackageToSave);
		FirstUnsolicitedPackage = 1;
	}
	for (UPackage* PendingPackage : PendingSet)
	{
		if (PendingPackage != PackageToSave)
		{
			PackagesToSave.Add(PendingPackage);
		}
	}

	// Loop over array and save as many packages as we can during our
	// time slice

	const int32 OriginalPackagesToSaveCount = PackagesToSave.Num();
	TArray<const ITargetPlatform*, TInlineAllocator<ExpectedMaxNumPlatforms>> PlatformsForPackage;
	TArray<const ITargetPlatform*> AlreadyCookedPlatformsForPackage;

	{
		SCOPE_TIMER(SavingPackages);

		for (int32 I = 0; I < PackagesToSave.Num(); ++I)
		{
			UPackage* Package = PackagesToSave[I];
			if (Package->IsLoadedByEditorPropertiesOnly() && PackageTracker->UncookedEditorOnlyPackages.Contains(Package->GetFName()))
			{
				// We already attempted to cook this package and it's still not referenced by any non editor-only properties.
				continue;
			}

			// This package is valid, so make sure it wasn't previously marked as being an uncooked editor only package or it would get removed from the
			// asset registry at the end of the cook
			PackageTracker->UncookedEditorOnlyPackages.Remove(Package->GetFName());

			const FName PackageFName = PackageNameCache->GetCachedStandardPackageFileFName(Package);
			if (PackageTracker->NeverCookPackageList.Contains(PackageFName))
			{
				// refuse to save this package, it's clearly one of the undesirables
				continue;
			}

			// For passed-in packages, consider cooking only the passed-in target platforms
			// For pending save packages, consider cooking all platforms in the cook-by-the-book session
			const bool bProcessingUnsolicitedPackages = Package != PackageToSave;
			const TArray<const ITargetPlatform*>& PossiblePlatformsForPackage{ bProcessingUnsolicitedPackages ? PlatformManager->GetSessionPlatforms() : InTargetPlatforms };

			// Cook only the platforms that have not yet been cooked for the given package
			AlreadyCookedPlatformsForPackage.Reset();
			PackageTracker->CookedPackages.GetCookedPlatforms(PackageFName, AlreadyCookedPlatformsForPackage);
			PlatformsForPackage.Reset();
			for (const ITargetPlatform* TargetPlatform : PossiblePlatformsForPackage)
			{
				if (!AlreadyCookedPlatformsForPackage.Contains(TargetPlatform))
				{
					PlatformsForPackage.Add(TargetPlatform);
				}
			}

			if (PlatformsForPackage.Num() == 0)
			{
				// We've already saved all possible platforms for this package; this should not be possible.
				// It should not be possible for the passed-in packages because they should never have been added to CookRequests if the remaining packages to cook was empty.
				// It should not be possible for pendingsave packages because they should not have been queued if they had completed all of their possible platforms, and should be removed from pendingsave if they are ever cooked
				UE_LOG(LogCook, Warning, TEXT("%s package '%s' in SaveCookedPackages has no more platforms left to cook; this should not be possible!"),
					(bProcessingUnsolicitedPackages ? TEXT("Unsolicited") : TEXT("Passed-in")), *PackageFName.ToString());
				if (bProcessingUnsolicitedPackages)
				{
					PackageTracker->RemovePendingSavePackage(Package);
				}
				continue;
			}

			// if we are processing unsolicited packages we can optionally not save these right now
			// the unsolicited packages which we missed now will be picked up on next run
			// we want to do this in cook on the fly also, if there is a new network package request instead of saving unsolicited packages we can process the requested package

			bool bShouldFinishTick = false;

			if (Timer.IsTimeUp() && IsCookByTheBookMode() )
			{
				// our timeslice is up
				bShouldFinishTick = true;
			}

			// if we are cook the fly then save the package which was requested as fast as we can because the client is waiting on it

			bool bForceSavePackage = false;

			if (IsCookOnTheFlyMode())
			{
				if (bProcessingUnsolicitedPackages)
				{
					SCOPE_TIMER(WaitingForCachedCookedPlatformData);
					if (HasCookRequests())
					{
						bShouldFinishTick = true;
					}
					if (Timer.IsTimeUp())
					{
						bShouldFinishTick = true;
						// our timeslice is up
					}
					bool bFinishedCachingCookedPlatformData = false;
					// if we are in realtime mode then don't wait forever for the package to be ready
					while ((!Timer.IsTimeUp()) && IsRealtimeMode() && (bShouldFinishTick == false))
					{
						if (FinishPackageCacheForCookedPlatformData(Package, PlatformsForPackage, Timer) == true)
						{
							bFinishedCachingCookedPlatformData = true;
							break;
						}

						GShaderCompilingManager->ProcessAsyncResults(true, false);
						// sleep for a bit
						FPlatformProcess::Sleep(0.0f);
					}
					bShouldFinishTick |= !bFinishedCachingCookedPlatformData;
				}
				else
				{
					if (!IsRealtimeMode())
					{
						bForceSavePackage = true;
					}
				}
			}

			bool AllObjectsCookedDataCached = true;
			bool HasCheckedAllPackagesAreCached = (I >= OriginalPackagesToSaveCount);

			MakePackageFullyLoaded(Package);

			if (IsCookOnTheFlyMode())
			{
				// never want to requeue packages
				HasCheckedAllPackagesAreCached = true;
			}

			// if we are forcing save the package then it doesn't matter if we call FinishPackageCacheForCookedPlatformData
			if (!bShouldFinishTick && !bForceSavePackage)
			{
				AllObjectsCookedDataCached = FinishPackageCacheForCookedPlatformData(Package, PlatformsForPackage, Timer);
				if (AllObjectsCookedDataCached == false)
				{
					GShaderCompilingManager->ProcessAsyncResults(true, false);
					AllObjectsCookedDataCached = FinishPackageCacheForCookedPlatformData(Package, PlatformsForPackage, Timer);
				}
			}

			// if we are in realtime mode and this package isn't ready to be saved then we should exit the tick here so we don't save it while in launch on
			if (IsRealtimeMode() &&
				(AllObjectsCookedDataCached == false) &&
				HasCheckedAllPackagesAreCached)
			{
				bShouldFinishTick = true;
			}

			if (bShouldFinishTick && (!bForceSavePackage))
			{
				SCOPE_TIMER(EnqueueUnsavedPackages);
				// enqueue all the packages which we were about to save
				Timer.SavedPackage();  // this is a special case to prevent infinite loop, if we only have one package we might fall through this and could loop forever.  
				int32 NumPackagesToRequeue = PackagesToSave.Num();

				if (IsCookOnTheFlyMode())
				{
					NumPackagesToRequeue = FirstUnsolicitedPackage;
				}
				
				for (int32 RemainingIndex = I; RemainingIndex < NumPackagesToRequeue; ++RemainingIndex)
				{
					FName StandardFilename = PackageNameCache->GetCachedStandardPackageFileFName(PackagesToSave[RemainingIndex]);
					PackageTracker->EnqueueUniqueCookRequest(FFilePlatformRequest(StandardFilename, PlatformsForPackage));
				}
				Result |= COSR_WaitingOnCache;

				// break out of the loop
				return;
			}

			// don't precache other packages if our package isn't ready but we are going to save it.   This will fill up the worker threads with extra shaders which we may need to flush on 
			if ((!IsCookOnTheFlyMode()) &&
				(!IsRealtimeMode() || AllObjectsCookedDataCached == true))
			{
				// precache platform data for next package 
				UPackage *NextPackage = PackagesToSave[FMath::Min(PackagesToSave.Num() - 1, I + 1)];
				UPackage *NextNextPackage = PackagesToSave[FMath::Min(PackagesToSave.Num() - 1, I + 2)];
				if (NextPackage != Package)
				{
					SCOPE_TIMER(PrecachePlatformDataForNextPackage);
					BeginPackageCacheForCookedPlatformData(NextPackage, PlatformsForPackage, Timer);
				}
				if (NextNextPackage != NextPackage)
				{
					SCOPE_TIMER(PrecachePlatformDataForNextNextPackage);
					BeginPackageCacheForCookedPlatformData(NextNextPackage, PlatformsForPackage, Timer);
				}
			}

			// if we are running the cook commandlet
			// if we already went through the entire package list then don't keep requeuing requests
			if ((HasCheckedAllPackagesAreCached == false) &&
				(AllObjectsCookedDataCached == false) &&
				(bForceSavePackage == false) &&
				IsCookByTheBookMode() )
			{
				// check(IsCookByTheBookMode() || ProcessingUnsolicitedPackages == true);
				// add to back of queue
				PackagesToSave.Add(Package);
				// UE_LOG(LogCook, Display, TEXT("Delaying save for package %s"), *PackageFName.ToString());
				continue;
			}

			if ( HasCheckedAllPackagesAreCached && (AllObjectsCookedDataCached == false) )
			{
				UE_LOG(LogCook, Verbose, TEXT("Forcing save package %s because was already requeued once"), *PackageFName.ToString());
			}


			bool bShouldSaveAsync = true;
			FString Temp;
			if (FParse::Value(FCommandLine::Get(), TEXT("-diffagainstcookdirectory="), Temp) || FParse::Value(FCommandLine::Get(), TEXT("-breakonfile="), Temp))
			{
				// async save doesn't work with this flags
				bShouldSaveAsync = false;
			}

			TArray<bool> SucceededSavePackage;
			TArray<FSavePackageResultStruct> SavePackageResults;
			{
				COOK_STAT(FScopedDurationTimer TickCookOnTheSideSaveCookedPackageTimer(DetailedCookStats::TickCookOnTheSideSaveCookedPackageTimeSec));
				SCOPE_TIMER(SaveCookedPackage);
				uint32 SaveFlags = SAVE_KeepGUID | (bShouldSaveAsync ? SAVE_Async : SAVE_None) | (IsCookFlagSet(ECookInitializationFlags::Unversioned) ? SAVE_Unversioned : 0);

				bool KeepEditorOnlyPackages = false;
				// removing editor only packages only works when cooking in commandlet and non iterative cooking
				// also doesn't work in multiprocess cooking
				KeepEditorOnlyPackages = !(IsCookByTheBookMode() && !IsCookingInEditor());
				KeepEditorOnlyPackages |= IsCookFlagSet(ECookInitializationFlags::Iterative);
				SaveFlags |= KeepEditorOnlyPackages ? SAVE_KeepEditorOnlyCookedPackages : SAVE_None;
				SaveFlags |= CookByTheBookOptions ? SAVE_ComputeHash : SAVE_None;

				GOutputCookingWarnings = IsCookFlagSet(ECookInitializationFlags::OutputVerboseCookerWarnings);
				try
				{
					SaveCookedPackage(Package, SaveFlags, PlatformsForPackage, SavePackageResults);
				}
				catch (std::exception&)
				{
					FString TargetPlatforms;
					for ( const ITargetPlatform* Platform : PlatformsForPackage)
					{
						TargetPlatforms += FString::Printf( TEXT("%s, "), *Platform->PlatformName());
					}
					UE_LOG(LogCook, Warning, TEXT("Tried to save package %s for target platforms %s but threw an exception"), *Package->GetPathName(), *TargetPlatforms);
					throw;
				}
				
				GOutputCookingWarnings = false;
				check(PlatformsForPackage.Num() == SavePackageResults.Num());
				for (int iResultIndex = 0; iResultIndex < SavePackageResults.Num(); iResultIndex++)
				{
					FSavePackageResultStruct& SavePackageResult = SavePackageResults[iResultIndex];

					if (SavePackageResult == ESavePackageResult::Success || SavePackageResult == ESavePackageResult::GenerateStub || SavePackageResult == ESavePackageResult::ReplaceCompletely)
					{
						SucceededSavePackage.Add(true);
						// Update flags used to determine garbage collection.
						if (Package->ContainsMap())
						{
							Result |= COSR_CookedMap;
						}
						else
						{
							++CookedPackageCount;
							Result |= COSR_CookedPackage;
						}

						// Update asset registry
						if (CookByTheBookOptions)
						{
							FAssetRegistryGenerator* Generator = PlatformManager->GetPlatformData(PlatformsForPackage[iResultIndex])->RegistryGenerator.Get();
							UpdateAssetRegistryPackageData(Generator, Package->GetFName(), SavePackageResult);

						}

					}
					else
					{
						SucceededSavePackage.Add(false);
					}
				}
				check(SavePackageResults.Num() == SucceededSavePackage.Num());
				Timer.SavedPackage();
			}

			if (IsCookingInEditor() == false)
			{
				SCOPE_TIMER(ClearAllCachedCookedPlatformData);
				TArray<UObject*> ObjectsInPackage;
				GetObjectsWithOuter(Package, ObjectsInPackage);
				for (UObject* Object : ObjectsInPackage)
				{
					Object->ClearAllCachedCookedPlatformData();
				}
			}

			//@todo ResetLoaders outside of this (ie when Package is NULL) causes problems w/ default materials
			FName StandardFilename = PackageNameCache->GetCachedStandardPackageFileFName(Package);

			// We always want to mark package as processed unless it wasn't saved because it was referenced by editor-only data
			// in which case we may still need to save it later when new content loads it through non editor-only references
			if (StandardFilename != NAME_None)
			{
				// mark the package as cooked
				FFilePlatformCookedPackage FileRequest(StandardFilename, PlatformsForPackage, SucceededSavePackage);
				bool bWasReferencedOnlyByEditorOnlyData = false;
				for (const FSavePackageResultStruct& SavePackageResult : SavePackageResults)
				{
					if (SavePackageResult == ESavePackageResult::ReferencedOnlyByEditorOnlyData)
					{
						bWasReferencedOnlyByEditorOnlyData = true;
						// if this is the case all of the packages should be referenced only by editor only data
					}
				}
				if (!bWasReferencedOnlyByEditorOnlyData)
				{
					PackageTracker->OnPackageCooked(FileRequest, Package);

					if ((CurrentCookMode == ECookMode::CookOnTheFly) && (I >= FirstUnsolicitedPackage))
					{
						// this is an unsolicited package
						if (FPaths::FileExists(FileRequest.GetFilename().ToString()) == true)
						{
							PackageTracker->UnsolicitedCookedPackages.AddCookedPackage(FFilePlatformRequest(FileRequest.GetFilename(), FileRequest.GetPlatforms()));

#if DEBUG_COOKONTHEFLY
							UE_LOG(LogCook, Display, TEXT("UnsolicitedCookedPackages: %s"), *FileRequest.GetFilename().ToString());
#endif
						}
					}
				}
				else
				{
					PackageTracker->UncookedEditorOnlyPackages.AddUnique(Package->GetFName());
				}
			}
			else
			{
				for (const bool bSucceededSavePackage : SucceededSavePackage)
				{
					check(bSucceededSavePackage == false);
				}
			}
		}
	}
}

void UCookOnTheFlyServer::UpdateAssetRegistryPackageData(FAssetRegistryGenerator* Generator, const FName& PackageName, FSavePackageResultStruct& SavePackageResult)
{
	if (!Generator)
		return;

	FAssetPackageData* PackageData = Generator->GetAssetPackageData(PackageName);
	PackageData->DiskSize = SavePackageResult.TotalFileSize;
	// If there is no hash (e.g.: when SavePackageResult == ESavePackageResult::ReplaceCompletely), don't attempt to setup a continuation to update
	// the AssetRegistry entry with it later.  Just leave the asset registry entry with a default constructed FMD5Hash which is marked as invalid.
	if (SavePackageResult.CookedHash.IsValid())
	{
		SavePackageResult.CookedHash.Next([PackageData](const FMD5Hash& CookedHash)
		{
			// Store the cooked hash in the Asset Registry when it is done computing in another thread.
			// NOTE: For this to work, we rely on:
			// 1) UPackage::WaitForAsyncFileWrites to have been called before any use of the CookedHash - it is called in CookByTheBookFinished before the registry does any work with the registries
			// 2) PackageData must continue to be a valid pointer - the asset registry allocates the FAssetPackageData individually and doesn't relocate or delete them until pruning, which happens after WaitForAsyncFileWrites
			PackageData->CookedHash = CookedHash;
		});
	}
}

void UCookOnTheFlyServer::PostLoadPackageFixup(UPackage* Package)
{
	if (Package->ContainsMap() == false)
	{
		return;
	}
	UWorld* World = UWorld::FindWorldInPackage(Package);
	if (!World)
	{
		return;
	}

	// Ensure we only process the package once
	if (PackageTracker->PostLoadFixupPackages.Find(Package) != nullptr)
	{
		return;
	}
	PackageTracker->PostLoadFixupPackages.Add(Package);

	// Perform special processing for UWorld
	World->PersistentLevel->HandleLegacyMapBuildData();

	if (IsCookByTheBookMode() == false)
	{
		return;
	}

	GIsCookerLoadingPackage = true;
	if (World->GetStreamingLevels().Num())
	{
		TSet<FName> NeverCookPackageNames;
		PackageTracker->NeverCookPackageList.GetValues(NeverCookPackageNames);

		UE_LOG(LogCook, Display, TEXT("Loading secondary levels for package '%s'"), *World->GetName());

		World->LoadSecondaryLevels(true, &NeverCookPackageNames);
	}
	GIsCookerLoadingPackage = false;

	TArray<FString> NewPackagesToCook;

	// Collect world composition tile packages to cook
	if (World->WorldComposition)
	{
		World->WorldComposition->CollectTilesToCook(NewPackagesToCook);
	}

	for (const FString& PackageName : NewPackagesToCook)
	{
		FName StandardPackageFName = PackageNameCache->GetCachedStandardPackageFileFName(FName(*PackageName));

		if (StandardPackageFName != NAME_None)
		{
			RequestPackage(StandardPackageFName, false);
		}
	}
}

void UCookOnTheFlyServer::TickPrecacheObjectsForPlatforms(const float TimeSlice, const TArray<const ITargetPlatform*>& TargetPlatforms) 
{
	
	SCOPE_CYCLE_COUNTER(STAT_TickPrecacheCooking);

	
	FCookerTimer Timer(TimeSlice, true);

	if (LastUpdateTick > 50 ||
		((CachedMaterialsToCacheArray.Num() == 0) && (CachedTexturesToCacheArray.Num() == 0)))
	{
		LastUpdateTick = 0;
		TArray<UObject*> Materials;
		GetObjectsOfClass(UMaterial::StaticClass(), Materials, true);
		for (UObject* Material : Materials)
		{
			if ( Material->GetOutermost() == GetTransientPackage())
				continue;

			CachedMaterialsToCacheArray.Add(Material);
		}
		TArray<UObject*> Textures;
		GetObjectsOfClass(UTexture::StaticClass(), Textures, true);
		for (UObject* Texture : Textures)
		{
			if (Texture->GetOutermost() == GetTransientPackage())
				continue;

			CachedTexturesToCacheArray.Add(Texture);
		}
	}
	++LastUpdateTick;

	if (Timer.IsTimeUp())
	{
		return;
	}

	bool AllMaterialsCompiled = true;
	// queue up some shaders for compilation

	while (CachedMaterialsToCacheArray.Num() > 0)
	{
		UMaterial* Material = (UMaterial*)(CachedMaterialsToCacheArray[0].Get());
		CachedMaterialsToCacheArray.RemoveAtSwap(0, 1, false);

		if (Material == nullptr)
		{
			continue;
		}

		for (const ITargetPlatform* TargetPlatform : TargetPlatforms)
		{
			if (!TargetPlatform)
			{
				continue;
			}
			if (!Material->IsCachedCookedPlatformDataLoaded(TargetPlatform))
			{
				Material->BeginCacheForCookedPlatformData(TargetPlatform);
				AllMaterialsCompiled = false;
			}
		}

		if (Timer.IsTimeUp())
		{
			return;
		}

		if (GShaderCompilingManager->GetNumRemainingJobs() > MaxPrecacheShaderJobs)
		{
			return;
		}
	}


	if (!AllMaterialsCompiled)
	{
		return;
	}

	while (CachedTexturesToCacheArray.Num() > 0)
	{
		UTexture* Texture = (UTexture*)(CachedTexturesToCacheArray[0].Get());
		CachedTexturesToCacheArray.RemoveAtSwap(0, 1, false);

		if (Texture == nullptr)
		{
			continue;
		}

		for (const ITargetPlatform* TargetPlatform : TargetPlatforms)
		{
			if (!TargetPlatform)
			{
				continue;
			}
			Texture->BeginCacheForCookedPlatformData(TargetPlatform);
		}
		if (Timer.IsTimeUp())
		{
			return;
		}
	}

}

bool UCookOnTheFlyServer::HasExceededMaxMemory() const
{
	if (IsCookByTheBookMode() && CookByTheBookOptions->bFullLoadAndSave)
	{
		// FullLoadAndSave does the entire cook in one tick, so there is no need to GC after
		return false;
	}

	const FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();

	//  if we have less emmory free then we should have then gc some stuff
	if ((MemStats.AvailablePhysical < MinFreeMemory) && 
		(MinFreeMemory != 0) )
	{
		UE_LOG(LogCook, Display, TEXT("Available physical memory low %d kb, exceeded max memory"), MemStats.AvailablePhysical / 1024);
		return true;
	}

#if UE_GC_TRACK_OBJ_AVAILABLE
	if (GUObjectArray.GetObjectArrayEstimatedAvailable() < MinFreeUObjectIndicesBeforeGC)
	{
		UE_LOG(LogCook, Display, TEXT("Running out of available UObject indices (%d remaining)"), GUObjectArray.GetObjectArrayEstimatedAvailable());
		return true;
	}
#endif // UE_GC_TRACK_OBJ_AVAILABLE

	// don't gc if we haven't reached our min gc level yet
	if (MemStats.UsedVirtual < MinMemoryBeforeGC)
	{
		return false;
	}

	//uint64 UsedMemory = MemStats.UsedVirtual; 
	uint64 UsedMemory = MemStats.UsedPhysical; //should this be used virtual?
	if ((UsedMemory >= MaxMemoryAllowance) &&
		(MaxMemoryAllowance > 0u))
	{
		UE_LOG(LogCook, Display, TEXT("Used memory high %d kb, exceeded max memory"), MemStats.UsedPhysical / 1024);
		return true;
	}

	return false;
}

TArray<UPackage*> UCookOnTheFlyServer::GetUnsolicitedPackages(const TArray<const ITargetPlatform*>& TargetPlatforms) const
{
	SCOPE_TIMER(GeneratePackageNames);

	TArray<UPackage*> PackagesToSave;

	for (UPackage* Package : PackageTracker->LoadedPackages)
	{
		check(Package != nullptr);

		const FName StandardPackageFName = PackageNameCache->GetCachedStandardPackageFileFName(Package);

		if (StandardPackageFName == NAME_None)
			continue;	// if we have name none that means we are in core packages or something...

		if (PackageTracker->CookedPackages.Exists(StandardPackageFName, TargetPlatforms))
			continue;

		PackagesToSave.Add(Package);

		UE_LOG(LogCook, Verbose, TEXT("Found unsolicited package to cook '%s'"), *Package->GetName());
	}

	return PackagesToSave;
}

TArray<UPackage*> UCookOnTheFlyServer::GetUnsolicitedPackages(const TArray<FName>& TargetPlatformNames) const
{
	TArray<const ITargetPlatform*> TargetPlatforms;
	ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();
	for (const FName& TargetPlatformName : TargetPlatformNames)
	{
		const ITargetPlatform* TargetPlatform = TPM.FindTargetPlatform(TargetPlatformName.ToString());
		if (TargetPlatform)
		{
			TargetPlatforms.Add(TargetPlatform);
		}
	}
	return GetUnsolicitedPackages(TargetPlatforms);
}

void UCookOnTheFlyServer::OnObjectModified( UObject *ObjectMoving )
{
	if (IsGarbageCollecting())
	{
		return;
	}
	OnObjectUpdated( ObjectMoving );
}

void UCookOnTheFlyServer::OnObjectPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent)
{
	if (IsGarbageCollecting())
	{
		return;
	}
	if ( PropertyChangedEvent.Property == nullptr && 
		PropertyChangedEvent.MemberProperty == nullptr )
	{
		// probably nothing changed... 
		return;
	}

	OnObjectUpdated( ObjectBeingModified );
}

void UCookOnTheFlyServer::OnObjectSaved( UObject* ObjectSaved )
{
	if (GIsCookerLoadingPackage)
	{
		// This is the cooker saving a cooked package, ignore
		return;
	}

	UPackage* Package = ObjectSaved->GetOutermost();
	if (Package == nullptr || Package == GetTransientPackage())
	{
		return;
	}

	MarkPackageDirtyForCooker(Package);

	// Register the package filename as modified. We don't use the cache because the file may not exist on disk yet at this point
	const FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), Package->ContainsMap() ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension());
	ModifiedAssetFilenames.Add(FName(*PackageFilename));
}

void UCookOnTheFlyServer::OnObjectUpdated( UObject *Object )
{
	// get the outer of the object
	UPackage *Package = Object->GetOutermost();

	MarkPackageDirtyForCooker( Package );
}

void UCookOnTheFlyServer::MarkPackageDirtyForCooker( UPackage *Package )
{
	if ( Package->RootPackageHasAnyFlags(PKG_PlayInEditor) )
	{
		return;
	}

	if (Package->HasAnyPackageFlags(PKG_PlayInEditor | PKG_ContainsScript | PKG_InMemoryOnly) == true && !GetClass()->HasAnyClassFlags(CLASS_DefaultConfig | CLASS_Config))
	{
		return;
	}

	if (Package == GetTransientPackage())
	{
		return;
	}

	if ( FPackageName::IsMemoryPackage(Package->GetName()))
	{
		return;
	}

	if ( !bIsSavingPackage )
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MarkPackageDirtyForCooker);

		// could have just cooked a file which we might need to write
		UPackage::WaitForAsyncFileWrites();

		// force that package to be recooked
		const FString Name = Package->GetPathName();

		FName PackageFFileName = PackageNameCache->GetCachedStandardPackageFileFName(Package);

		if ( PackageFFileName == NAME_None )
		{
			PackageNameCache->ClearPackageFilenameCacheForPackage(Package);

			return;
		}

		UE_LOG(LogCook, Verbose, TEXT("Modification detected to package %s"), *PackageFFileName.ToString());

		if ( IsCookingInEditor() )
		{
			if ( IsCookByTheBookMode() )
			{
				TArray<const ITargetPlatform*> CookedPlatforms;
				// if we have already cooked this package and we have made changes then recook ;)
				PackageTracker->CookedPackages.GetCookedPlatforms(PackageFFileName, CookedPlatforms);
				bool bHadCookedPackages = CookedPlatforms.Num() != 0;
				PackageTracker->CookedPackages.RemoveFile(PackageFFileName);
				if (bHadCookedPackages)
				{
					if (IsCookByTheBookRunning())
					{
						// if this package was previously cooked and we are doing a cook by the book 
						// we need to recook this package before finishing cook by the book

						// Note - SessionPlatforms here instead of CookedPlatforms. The lock is not needed 
						// because we have checked IsCookByTheBook and therefore we are single threaded.
						PackageTracker->EnqueueUniqueCookRequest(FFilePlatformRequest(PackageFFileName, PlatformManager->GetSessionPlatforms()));
					}
					else
					{
						CookByTheBookOptions->PreviousCookRequests.Add(FFilePlatformRequest(PackageFFileName, CookedPlatforms));
					}
				}
			}
			else if ( IsCookOnTheFlyMode() )
			{
				if ( FileModifiedDelegate.IsBound() ) 
				{
					const FString PackageName = PackageFFileName.ToString();
					FileModifiedDelegate.Broadcast(PackageName);
					if ( PackageName.EndsWith(".uasset") || PackageName.EndsWith(".umap"))
					{
						FileModifiedDelegate.Broadcast( FPaths::ChangeExtension(PackageName, TEXT(".uexp")) );
						FileModifiedDelegate.Broadcast( FPaths::ChangeExtension(PackageName, TEXT(".ubulk")) );
						FileModifiedDelegate.Broadcast( FPaths::ChangeExtension(PackageName, TEXT(".ufont")) );
					}
				}
			}
			else
			{
				// this is here if we add a new mode and don't implement this it will crash instead of doing undesireable behaviour 
				check( true);
			}
		}

		PackageTracker->DirtyPackage(PackageFFileName, Package);
	}
}

void UCookOnTheFlyServer::EndNetworkFileServer()
{
	for ( int i = 0; i < NetworkFileServers.Num(); ++i )
	{
		INetworkFileServer *NetworkFileServer = NetworkFileServers[i];
		// shutdown the server
		NetworkFileServer->Shutdown();
		delete NetworkFileServer;
		NetworkFileServer = NULL;
	}
	NetworkFileServers.Empty();
	PlatformManager->SetPlatformDataFrozen(false);
}

uint32 UCookOnTheFlyServer::GetPackagesPerGC() const
{
	return PackagesPerGC;
}

uint32 UCookOnTheFlyServer::GetPackagesPerPartialGC() const
{
	return MaxNumPackagesBeforePartialGC;
}


double UCookOnTheFlyServer::GetIdleTimeToGC() const
{
	return IdleTimeToGC;
}

uint64 UCookOnTheFlyServer::GetMaxMemoryAllowance() const
{
	return MaxMemoryAllowance;
}

const TArray<FName>& UCookOnTheFlyServer::GetFullPackageDependencies(const FName& PackageName ) const
{
	TArray<FName>* PackageDependencies = CachedFullPackageDependencies.Find(PackageName);
	if ( !PackageDependencies )
	{
		static const FName NAME_CircularReference(TEXT("CircularReference"));
		static int32 UniqueArrayCounter = 0;
		++UniqueArrayCounter;
		FName CircularReferenceArrayName = FName(NAME_CircularReference,UniqueArrayCounter);
		{
			// can't initialize the PackageDependencies array here because we call GetFullPackageDependencies below and that could recurse and resize CachedFullPackageDependencies
			TArray<FName>& TempPackageDependencies = CachedFullPackageDependencies.Add(PackageName); // IMPORTANT READ ABOVE COMMENT
			// initialize TempPackageDependencies to a dummy dependency so that we can detect circular references
			TempPackageDependencies.Add(CircularReferenceArrayName);
			// when someone finds the circular reference name they look for this array name in the CachedFullPackageDependencies map
			// and add their own package name to it, so that they can get fixed up 
			CachedFullPackageDependencies.Add(CircularReferenceArrayName);
		}

		TArray<FName> ChildDependencies;
		if ( AssetRegistry->GetDependencies(PackageName, ChildDependencies, EAssetRegistryDependencyType::All) )
		{
			TArray<FName> Dependencies = ChildDependencies;
			Dependencies.AddUnique(PackageName);
			for ( const FName& ChildDependency : ChildDependencies)
			{
				const TArray<FName>& ChildPackageDependencies = GetFullPackageDependencies(ChildDependency);
				for ( const FName& ChildPackageDependency : ChildPackageDependencies )
				{
					if ( ChildPackageDependency == CircularReferenceArrayName )
					{
						continue;
					}

					if ( ChildPackageDependency.GetComparisonIndex() == NAME_CircularReference.GetComparisonIndex() )
					{
						// add our self to the package which we are circular referencing
						TArray<FName>& TempCircularReference = CachedFullPackageDependencies.FindChecked(ChildPackageDependency);
						TempCircularReference.AddUnique(PackageName); // add this package name so that it's dependencies get fixed up when the outer loop returns
					}

					Dependencies.AddUnique(ChildPackageDependency);
				}
			}

			// all these packages referenced us apparently so fix them all up
			const TArray<FName>& PackagesForFixup = CachedFullPackageDependencies.FindChecked(CircularReferenceArrayName);
			for ( const FName FixupPackage : PackagesForFixup )
			{
				TArray<FName> &FixupList = CachedFullPackageDependencies.FindChecked(FixupPackage);
				// check( FixupList.Contains( CircularReferenceArrayName) );
				ensure( FixupList.Remove(CircularReferenceArrayName) == 1 );
				for( const FName AdditionalDependency : Dependencies )
				{
					FixupList.AddUnique(AdditionalDependency);
					if ( AdditionalDependency.GetComparisonIndex() == NAME_CircularReference.GetComparisonIndex() )
					{
						// add our self to the package which we are circular referencing
						TArray<FName>& TempCircularReference = CachedFullPackageDependencies.FindChecked(AdditionalDependency);
						TempCircularReference.AddUnique(FixupPackage); // add this package name so that it's dependencies get fixed up when the outer loop returns
					}
				}
			}
			CachedFullPackageDependencies.Remove(CircularReferenceArrayName);

			PackageDependencies = CachedFullPackageDependencies.Find(PackageName);
			check(PackageDependencies);

			Swap(*PackageDependencies, Dependencies);
		}
		else
		{
			PackageDependencies = CachedFullPackageDependencies.Find(PackageName);
			PackageDependencies->Add(PackageName);
		}
	}

	return *PackageDependencies;
}

void UCookOnTheFlyServer::MarkGCPackagesToKeepForCooker()
{
	// just saved this package will the cooker need this package again this cook?
	for (TObjectIterator<UObject> It; It; ++It)
	{
		UObject* Object = *It;
		Object->ClearFlags(RF_KeepForCooker);
	}

	TSet<FName> KeepPackages;
	// first see if the package is in the required to be saved list
	// then see if the package is needed by any of the packages which are required to be saved

	TMap<FName, int32> PackageDependenciesCount;
	FScopeLock RequestScopeLock(&PackageTracker->GetRequestLock());
	for (const FName& QueuedPackage : PackageTracker->GetThreadUnsafeCookRequests().GetQueue())
	{
		const FName* PackageName = PackageNameCache->GetCachedPackageFilenameToPackageFName(QueuedPackage);
		if ( !PackageName )
		{
			PackageDependenciesCount.Add(QueuedPackage, 0);
			continue;
		}
		const TArray<FName>& NeededPackages = GetFullPackageDependencies(*PackageName);
		const FName StandardFName = QueuedPackage;
		PackageDependenciesCount.Add(StandardFName, NeededPackages.Num());
		KeepPackages.Append(NeededPackages);
	}

	TSet<FName> LoadedPackages;
	for ( TObjectIterator<UPackage> It; It; ++It )
	{
		UPackage* Package = (UPackage*)(*It);
		if ( KeepPackages.Contains(Package->GetFName()) )
		{
			LoadedPackages.Add(PackageNameCache->GetCachedStandardPackageFileFName(Package->GetFName()) );
			const FReentryData& ReentryData = GetReentryData(Package);
			Package->SetFlags(RF_KeepForCooker);
			for (UObject* Obj : ReentryData.CachedObjectsInOuter)
			{
				Obj->SetFlags(RF_KeepForCooker);
			}
		}
	}

	// Sort the cook requests by the packages which are loaded first
	// then sort by the number of dependencies which are referenced by the package
	// we want to process the packages with the highest dependencies so that they can
	// be evicted from memory and are likely to be able to be released on next GC pass
	PackageTracker->GetThreadUnsafeCookRequests().Sort([&PackageDependenciesCount, &LoadedPackages](const FName& A, const FName& B)
	{
		int32 ADependencies = PackageDependenciesCount.FindChecked(A);
		int32 BDependencies = PackageDependenciesCount.FindChecked(B);
		bool ALoaded = LoadedPackages.Contains(A);
		bool BLoaded = LoadedPackages.Contains(B);
		return (ALoaded == BLoaded) ? (ADependencies > BDependencies) : ALoaded > BLoaded;
	}
	);
}

void UCookOnTheFlyServer::BeginDestroy()
{
	EndNetworkFileServer();

	Super::BeginDestroy();
}

void UCookOnTheFlyServer::TickRecompileShaderRequests()
{
	// try to pull off a request
	FRecompileRequest* Request = NULL;

	PackageTracker->RecompileRequests.Dequeue(&Request);

	// process it
	if (Request)
	{
		HandleNetworkFileServerRecompileShaders(Request->RecompileData);

		// all done! other thread can unblock now
		Request->bComplete = true;
	}
}

bool UCookOnTheFlyServer::HasRecompileShaderRequests() const 
{ 
	return PackageTracker->RecompileRequests.HasItems();
}

bool UCookOnTheFlyServer::MakePackageFullyLoaded(UPackage* Package) const
{
	if (Package->IsFullyLoaded())
	{
		return true;
	}

	bool bPackageFullyLoaded = false;
	GIsCookerLoadingPackage = true;
	Package->FullyLoad();
	//LoadPackage(NULL, *Package->GetName(), LOAD_None);
	GIsCookerLoadingPackage = false;
	if (!Package->IsFullyLoaded())
	{
		LogCookerMessage(FString::Printf(TEXT("Package %s supposed to be fully loaded but isn't. RF_WasLoaded is %s"),
			*Package->GetName(), Package->HasAnyFlags(RF_WasLoaded) ? TEXT("set") : TEXT("not set")), EMessageSeverity::Warning);

		UE_LOG(LogCook, Warning, TEXT("Package %s supposed to be fully loaded but isn't. RF_WasLoaded is %s"),
			*Package->GetName(), Package->HasAnyFlags(RF_WasLoaded) ? TEXT("set") : TEXT("not set"));
	}
	else
	{
		bPackageFullyLoaded = true;
	}
	// If fully loading has caused a blueprint to be regenerated, make sure we eliminate all meta data outside the package
	UMetaData* MetaData = Package->GetMetaData();
	MetaData->RemoveMetaDataOutsidePackage();

	return bPackageFullyLoaded;
}

class FDiffModeCookServerUtils
{
	/** Misc / common settings */
	bool bDiffEnabled;
	FString PackageFilter;

	/** DumpObjList settings */
	bool bDumpObjList;
	FString DumpObjListParams;

	/** DumpObjects settings */
	bool bDumpObjects;
	bool bDumpObjectsSorted;

public:

	FDiffModeCookServerUtils()
	{
		bDiffEnabled = FParse::Param(FCommandLine::Get(), TEXT("DIFFONLY"));
		bDumpObjList = false;
		bDumpObjects = false;
		bDumpObjectsSorted = false;

		ParseCmds();
	}

	bool IsRunningCookDiff() const
	{
		return bDiffEnabled;
	}

	void ProcessPackage(UPackage* InPackage)
	{
		ConditionallyDumpObjList(InPackage);
		ConditionallyDumpObjects(InPackage);
	}

private:

	void RemoveParam(FString& InOutParams, const TCHAR* InParamToRemove)
	{
		int32 ParamIndex = InOutParams.Find(InParamToRemove);
		if (ParamIndex >= 0)
		{
			int32 NextParamIndex = InOutParams.Find(TEXT(" -"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ParamIndex + 1);
			if (NextParamIndex < ParamIndex)
			{
				NextParamIndex = InOutParams.Len();
			}
			InOutParams = InOutParams.Mid(0, ParamIndex) + InOutParams.Mid(NextParamIndex);
		}
	}
	void ParseDumpObjList(FString InParams)
	{
		const TCHAR* PackageFilterParam = TEXT("-packagefilter=");
		FParse::Value(*InParams, PackageFilterParam, PackageFilter);
		RemoveParam(InParams, PackageFilterParam);

		// Add support for more parameters here
		// After all parameters have been parsed and removed, pass the remaining string as objlist params
		DumpObjListParams = InParams;
	}
	void ParseDumpObjects(FString InParams)
	{
		const TCHAR* PackageFilterParam = TEXT("-packagefilter=");
		FParse::Value(*InParams, PackageFilterParam, PackageFilter);
		RemoveParam(InParams, PackageFilterParam);

		const TCHAR* SortParam = TEXT("sort");
		bDumpObjectsSorted = FParse::Param(*InParams, SortParam);
		RemoveParam(InParams, SortParam);
	}

	void ParseCmds()
	{
		const TCHAR* DumpObjListParam = TEXT("dumpobjlist");
		const TCHAR* DumpObjectsParam = TEXT("dumpobjects");

		FString CmdsText;
		if (FParse::Value(FCommandLine::Get(), TEXT("-diffcmds="), CmdsText, false))
		{
			CmdsText = CmdsText.TrimQuotes();
			TArray<FString> CmdsList;
			CmdsText.ParseIntoArray(CmdsList, TEXT(","));
			for (FString Cmd : CmdsList)
			{
				if (Cmd.StartsWith(DumpObjListParam))
				{
					bDumpObjList = true;
					ParseDumpObjList(*Cmd + FCString::Strlen(DumpObjListParam));
				}
				else if (Cmd.StartsWith(DumpObjectsParam))
				{
					bDumpObjects = true;
					ParseDumpObjects(*Cmd + FCString::Strlen(DumpObjectsParam));
				}
			}
		}
	}
	bool FilterPackageName(UPackage* InPackage, const FString& InWildcard)
	{
		bool bInclude = false;
		FString PackageName = InPackage->GetName();
		if (PackageName.MatchesWildcard(InWildcard))
		{
			bInclude = true;
		}
		else if (FPackageName::GetShortName(PackageName).MatchesWildcard(InWildcard))
		{
			bInclude = true;
		}
		else if (InPackage->LinkerLoad && InPackage->LinkerLoad->Filename.MatchesWildcard(InWildcard))
		{
			bInclude = true;
		}
		return bInclude;
	}
	void ConditionallyDumpObjList(UPackage* InPackage)
	{
		if (bDumpObjList)
		{
			if (FilterPackageName(InPackage, PackageFilter))
			{
				FString ObjListExec = TEXT("OBJ LIST ");
				ObjListExec += DumpObjListParams;

				TGuardValue<ELogTimes::Type> GuardLogTimes(GPrintLogTimes, ELogTimes::None);
				TGuardValue<bool> GuardLogCategory(GPrintLogCategory, false);
				TGuardValue<bool> GuardPrintLogVerbosity(GPrintLogVerbosity, false);

				GEngine->Exec(nullptr, *ObjListExec);
			}
		}
	}
	void ConditionallyDumpObjects(UPackage* InPackage)
	{
		if (bDumpObjects)
		{
			if (FilterPackageName(InPackage, PackageFilter))
			{
				TArray<FString> AllObjects;
				for (FObjectIterator It; It; ++It)
				{
					AllObjects.Add(*It->GetFullName());
				}
				if (bDumpObjectsSorted)
				{
					AllObjects.Sort();
				}

				TGuardValue<ELogTimes::Type> GuardLogTimes(GPrintLogTimes, ELogTimes::None);
				TGuardValue<bool> GuardLogCategory(GPrintLogCategory, false);
				TGuardValue<bool> GuardPrintLogVerbosity(GPrintLogVerbosity, false);

				for (const FString& Obj : AllObjects)
				{
					UE_LOG(LogCook, Display, TEXT("%s"), *Obj);
				}
			}
		}
	}
};

void UCookOnTheFlyServer::SaveCookedPackage(UPackage* Package, uint32 SaveFlags, const TArrayView<const ITargetPlatform* const>& TargetPlatforms, TArray<FSavePackageResultStruct>& SavePackageResults)
{
	check( SavePackageResults.Num() == 0);
	check( bIsSavingPackage == false );
	bIsSavingPackage = true;

	const FString PackagePathName = Package->GetPathName();
	FString Filename(PackageNameCache->GetCachedPackageFilename(Package));

	// Also request any localized variants of this package
	if (IsCookByTheBookMode() && !CookByTheBookOptions->bDisableUnsolicitedPackages && !FPackageName::IsLocalizedPackage(PackagePathName))
	{
		const TArray<FName>* LocalizedVariants = CookByTheBookOptions->SourceToLocalizedPackageVariants.Find(Package->GetFName());
		if (LocalizedVariants)
		{
			for (const FName LocalizedPackageName : *LocalizedVariants)
			{
				const FName LocalizedPackageFile = PackageNameCache->GetCachedStandardPackageFileFName(LocalizedPackageName);
				RequestPackage(LocalizedPackageFile, false);
			}
		}
	}

	// Don't resolve, just add to request list as needed
	TSet<FName> SoftObjectPackages;

	GRedirectCollector.ProcessSoftObjectPathPackageList(Package->GetFName(), false, SoftObjectPackages);
	
	for (FName SoftObjectPackage : SoftObjectPackages)
	{
		TMap<FName, FName> RedirectedPaths;

		// If this is a redirector, extract destination from asset registry
		if (ContainsRedirector(SoftObjectPackage, RedirectedPaths))
		{
			for (TPair<FName, FName>& RedirectedPath : RedirectedPaths)
			{
				GRedirectCollector.AddAssetPathRedirection(RedirectedPath.Key, RedirectedPath.Value);
			}
		}

		// Verify package actually exists
		FName StandardPackageName = PackageNameCache->GetCachedStandardPackageFileFName(SoftObjectPackage);

		if (StandardPackageName != NAME_None && IsCookByTheBookMode() && !CookByTheBookOptions->bDisableUnsolicitedPackages)
		{
			// Add to front of request queue as an unsolicited package
			RequestPackage(StandardPackageName, true);
		}
	}

	if (Filename.Len() != 0 )
	{
		if (Package->HasAnyPackageFlags(PKG_ReloadingForCooker))
		{
			UE_LOG(LogCook, Warning, TEXT("Package %s marked as reloading for cook by was requested to save"), *Package->GetPathName());
			UE_LOG(LogCook, Fatal, TEXT("Package %s marked as reloading for cook by was requested to save"), *Package->GetPathName());
		}

		// Use SandboxFile to do path conversion to properly handle sandbox paths (outside of standard paths in particular).
		Filename = ConvertToFullSandboxPath(*Filename, true);

		uint32 OriginalPackageFlags = Package->GetPackageFlags();
		UWorld* World = nullptr;
		EObjectFlags FlagsToCook = RF_Public;

		ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();

		for (int32 PlatformIndex = 0; PlatformIndex < TargetPlatforms.Num(); ++PlatformIndex)
		{
			SavePackageResults.Add(FSavePackageResultStruct(ESavePackageResult::Success));
			const ITargetPlatform* Target = TargetPlatforms[PlatformIndex];
			FString PlatFilename = Filename.Replace(TEXT("[Platform]"), *Target->PlatformName());

			FSavePackageResultStruct& Result = SavePackageResults[PlatformIndex];

			bool bCookPackage = true;

			// don't save Editor resources from the Engine if the target doesn't have editoronly data
			if (IsCookFlagSet(ECookInitializationFlags::SkipEditorContent) &&
				(PackagePathName.StartsWith(TEXT("/Engine/Editor")) || PackagePathName.StartsWith(TEXT("/Engine/VREditor"))) &&
				!Target->HasEditorOnlyData())
			{
				Result = ESavePackageResult::ContainsEditorOnlyData;
				bCookPackage = false;
			}
			// Check whether or not game-specific behaviour should prevent this package from being cooked for the target platform
			else if (UAssetManager::IsValid() && !UAssetManager::Get().ShouldCookForPlatform(Package, Target))
			{
				Result = ESavePackageResult::ContainsEditorOnlyData;
				bCookPackage = false;
				UE_LOG(LogCook, Display, TEXT("Excluding %s -> %s"), *Package->GetName(), *PlatFilename);
			}
			// check if this package is unsupported for the target platform (typically plugin content)
			else 
			{
				TSet<FName>* NeverCookPackages = PackageTracker->PlatformSpecificNeverCookPackages.Find(Target);
				if (NeverCookPackages && NeverCookPackages->Find(FName(*PackagePathName)))
				{
					Result = ESavePackageResult::ContainsEditorOnlyData;
					bCookPackage = false;
					UE_LOG(LogCook, Display, TEXT("Excluding %s -> %s"), *Package->GetName(), *PlatFilename);
				}
			}

			if (bCookPackage == true)
			{
				bool bPackageFullyLoaded = false;
				if (bPackageFullyLoaded == false) //-V547
				{
					SCOPE_TIMER(LoadPackage);

					bPackageFullyLoaded = MakePackageFullyLoaded(Package);

					// look for a world object in the package (if there is one, there's a map)
					World = UWorld::FindWorldInPackage(Package);

					if (World)
					{
						FlagsToCook = RF_NoFlags;
					}
				}

				if (bPackageFullyLoaded)
				{
					UE_CLOG(GCookProgressDisplay & (int32)ECookProgressDisplayMode::PackageNames, LogCook, Display, TEXT("Cooking %s -> %s"), *Package->GetName(), *PlatFilename);

					bool bSwap = (!Target->IsLittleEndian()) ^ (!PLATFORM_LITTLE_ENDIAN);


					if (!Target->HasEditorOnlyData())
					{
						Package->SetPackageFlags(PKG_FilterEditorOnly);
					}
					else
					{
						Package->ClearPackageFlags(PKG_FilterEditorOnly);
					}

					if (World)
					{
						// Fixup legacy lightmaps before saving
						// This should be done after loading, but Core loads UWorlds with LoadObject so there's no opportunity to handle this fixup on load
						World->PersistentLevel->HandleLegacyMapBuildData();
					}

					const FString FullFilename = FPaths::ConvertRelativePathToFull(PlatFilename);
					if (FullFilename.Len() >= FPlatformMisc::GetMaxPathLength())
					{
						LogCookerMessage(FString::Printf(TEXT("Couldn't save package, filename is too long (%d >= %d): %s"), FullFilename.Len(), FPlatformMisc::GetMaxPathLength(), *PlatFilename), EMessageSeverity::Error);
						UE_LOG(LogCook, Error, TEXT("Couldn't save package, filename is too long (%d >= %d): %s"), FullFilename.Len(), FPlatformMisc::GetMaxPathLength(), *PlatFilename);
						Result = ESavePackageResult::Error;
					}
					else
					{
						static FDiffModeCookServerUtils DiffModeHelper;
						SCOPE_TIMER(GEditorSavePackage);
						GIsCookerLoadingPackage = true;

						if (DiffModeHelper.IsRunningCookDiff())
						{
							DiffModeHelper.ProcessPackage(Package);

							// When looking for deterministic cook issues, first serialize the package to memory and do a simple diff with the existing package
							uint32 DiffSaveFlags = SaveFlags | SAVE_DiffOnly;
							FArchiveDiffMap DiffMap;
							Result = GEditor->Save(Package, World, FlagsToCook, *PlatFilename, GError, NULL, bSwap, false, DiffSaveFlags, Target, FDateTime::MinValue(), false, &DiffMap);
							if (Result == ESavePackageResult::DifferentContent)
							{
								// If the simple memory diff was not identical, collect callstacks for all Serialize calls and dump differences to log
								DiffSaveFlags = SaveFlags | SAVE_DiffCallstack;
								Result = GEditor->Save(Package, World, FlagsToCook, *PlatFilename, GError, NULL, bSwap, false, DiffSaveFlags, Target, FDateTime::MinValue(), false, &DiffMap);
							}
						}
						else
						{
							FSavePackageContext* const SavePackageContext = (IsCookByTheBookMode() && SavePackageContexts.Num() > 0) ? SavePackageContexts[PlatformIndex] : nullptr;

							Result = GEditor->Save(	Package, World, FlagsToCook, *PlatFilename, 
													GError, nullptr, bSwap, false, SaveFlags, Target, 
													FDateTime::MinValue(), false, /*DiffMap*/ nullptr, 
													SavePackageContext);
						}
						GIsCookerLoadingPackage = false;
						{
							SCOPE_TIMER(ConvertingBlueprints);
							IBlueprintNativeCodeGenModule::Get().Convert(Package, Result.Result, *(Target->PlatformName()));
						}

						++this->StatSavedPackageCount;

						// If package was actually saved check with asset manager to make sure it wasn't excluded for being a development or never cook package. We do this after Editor Only filtering
						if (Result == ESavePackageResult::Success && UAssetManager::IsValid())
						{
							SCOPE_TIMER(VerifyCanCookPackage);
							if (!UAssetManager::Get().VerifyCanCookPackage(Package->GetFName()))
							{
								Result = ESavePackageResult::Error;
							}
						}
					}
				}
				else
				{
					LogCookerMessage(FString::Printf(TEXT("Unable to cook package for platform because it is unable to be loaded: %s"), *PlatFilename), EMessageSeverity::Error);
					UE_LOG(LogCook, Display, TEXT("Unable to cook package for platform because it is unable to be loaded %s -> %s"), *Package->GetName(), *PlatFilename);
					Result = ESavePackageResult::Error;
				}
			}
		}

		Package->SetPackageFlagsTo(OriginalPackageFlags);
	}
	else
	{
		for (int32 PlatformIndex = 0; PlatformIndex < TargetPlatforms.Num(); ++PlatformIndex)
		{
			SavePackageResults.Add(FSavePackageResultStruct(ESavePackageResult::MissingFile));
		}
	}

	check(bIsSavingPackage == true);
	bIsSavingPackage = false;

}

void UCookOnTheFlyServer::Initialize( ECookMode::Type DesiredCookMode, ECookInitializationFlags InCookFlags, const FString &InOutputDirectoryOverride )
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UCookOnTheFlyServer::Initialize);

	OutputDirectoryOverride = InOutputDirectoryOverride;
	CurrentCookMode = DesiredCookMode;
	CookFlags = InCookFlags;

	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddUObject(this, &UCookOnTheFlyServer::PreGarbageCollect);

	if (CurrentCookMode != ECookMode::CookByTheBook)
	{
		// For standalone CookByTheBook the PackageNameCache and PackageTracker are initialized 
		// later to benefit from a fully scanned and initialized AssetRegistry.
		// For IsCookingInEditor() these objects are required for the FCoreUObjectDelegates below
		ConstructPackageTracker();
	}

	if (IsCookingInEditor())
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(this, &UCookOnTheFlyServer::OnObjectPropertyChanged);
		FCoreUObjectDelegates::OnObjectModified.AddUObject(this, &UCookOnTheFlyServer::OnObjectModified);
		FCoreUObjectDelegates::OnObjectSaved.AddUObject(this, &UCookOnTheFlyServer::OnObjectSaved);

		FCoreDelegates::OnTargetPlatformChangedSupportedFormats.AddUObject(this, &UCookOnTheFlyServer::OnTargetPlatformChangedSupportedFormats);
	}

	FCoreDelegates::OnFConfigCreated.AddUObject(this, &UCookOnTheFlyServer::OnFConfigCreated);
	FCoreDelegates::OnFConfigDeleted.AddUObject(this, &UCookOnTheFlyServer::OnFConfigDeleted);

	MaxPrecacheShaderJobs = FPlatformMisc::NumberOfCores() - 1; // number of cores -1 is a good default allows the editor to still be responsive to other shader requests and allows cooker to take advantage of multiple processors while the editor is running
	GConfig->GetInt(TEXT("CookSettings"), TEXT("MaxPrecacheShaderJobs"), MaxPrecacheShaderJobs, GEditorIni);

	MaxConcurrentShaderJobs = FPlatformMisc::NumberOfCores() * 4; // TODO: document why number of cores * 4 is a good default
	GConfig->GetInt(TEXT("CookSettings"), TEXT("MaxConcurrentShaderJobs"), MaxConcurrentShaderJobs, GEditorIni);

	PackagesPerGC = 500;
	int32 ConfigPackagesPerGC = 0;
	if (GConfig->GetInt( TEXT("CookSettings"), TEXT("PackagesPerGC"), ConfigPackagesPerGC, GEditorIni ))
	{
		// Going unsigned. Make negative values 0
		PackagesPerGC = ConfigPackagesPerGC > 0 ? ConfigPackagesPerGC : 0;
	}

	IdleTimeToGC = 20.0;
	GConfig->GetDouble( TEXT("CookSettings"), TEXT("IdleTimeToGC"), IdleTimeToGC, GEditorIni );

	int32 MaxMemoryAllowanceInMB = 8 * 1024;
	GConfig->GetInt( TEXT("CookSettings"), TEXT("MaxMemoryAllowance"), MaxMemoryAllowanceInMB, GEditorIni );
	MaxMemoryAllowanceInMB = FMath::Max(MaxMemoryAllowanceInMB, 0);
	MaxMemoryAllowance = MaxMemoryAllowanceInMB * 1024LL * 1024LL;
	
	int32 MinMemoryBeforeGCInMB = 0; // 6 * 1024;
	GConfig->GetInt(TEXT("CookSettings"), TEXT("MinMemoryBeforeGC"), MinMemoryBeforeGCInMB, GEditorIni);
	MinMemoryBeforeGCInMB = FMath::Max(MinMemoryBeforeGCInMB, 0);
	MinMemoryBeforeGC = MinMemoryBeforeGCInMB * 1024LL * 1024LL;
	MinMemoryBeforeGC = FMath::Min(MaxMemoryAllowance, MinMemoryBeforeGC);

	MinFreeUObjectIndicesBeforeGC = 100000;
	GConfig->GetInt(TEXT("CookSettings"), TEXT("MinFreeUObjectIndicesBeforeGC"), MinFreeUObjectIndicesBeforeGC, GEditorIni);
	MinFreeUObjectIndicesBeforeGC = FMath::Max(MinFreeUObjectIndicesBeforeGC, 0);

	int32 MinFreeMemoryInMB = 0;
	GConfig->GetInt(TEXT("CookSettings"), TEXT("MinFreeMemory"), MinFreeMemoryInMB, GEditorIni);
	MinFreeMemoryInMB = FMath::Max(MinFreeMemoryInMB, 0);
	MinFreeMemory = MinFreeMemoryInMB * 1024LL * 1024LL;

	// check the amount of OS memory and use that number minus the reserved memory number
	int32 MinReservedMemoryInMB = 0;
	GConfig->GetInt(TEXT("CookSettings"), TEXT("MinReservedMemory"), MinReservedMemoryInMB, GEditorIni);
	MinReservedMemoryInMB = FMath::Max(MinReservedMemoryInMB, 0);
	int64 MinReservedMemory = MinReservedMemoryInMB * 1024LL * 1024LL;
	if ( MinReservedMemory )
	{
		int64 TotalRam = FPlatformMemory::GetPhysicalGBRam() * 1024LL * 1024LL * 1024LL;
		MaxMemoryAllowance = FMath::Min<int64>( MaxMemoryAllowance, TotalRam - MinReservedMemory );
	}

	MaxNumPackagesBeforePartialGC = 400;
	GConfig->GetInt(TEXT("CookSettings"), TEXT("MaxNumPackagesBeforePartialGC"), MaxNumPackagesBeforePartialGC, GEditorIni);
	
	GConfig->GetArray(TEXT("CookSettings"), TEXT("ConfigSettingBlacklist"), ConfigSettingBlacklist, GEditorIni);

	UE_LOG(LogCook, Display, TEXT("Max memory allowance for cook %dmb min free memory %dmb"), MaxMemoryAllowanceInMB, MinFreeMemoryInMB);


	{
		const FConfigSection* CacheSettings = GConfig->GetSectionPrivate(TEXT("CookPlatformDataCacheSettings"), false, true, GEditorIni);
		if ( CacheSettings )
		{
			for ( const auto& CacheSetting : *CacheSettings )
			{
				
				const FString& ReadString = CacheSetting.Value.GetValue();
				int32 ReadValue = FCString::Atoi(*ReadString);
				int32 Count = FMath::Max( 2,  ReadValue );
				MaxAsyncCacheForType.Add( CacheSetting.Key,  Count );
			}
		}
		CurrentAsyncCacheForType = MaxAsyncCacheForType;
	}


	if (IsCookByTheBookMode())
	{
		CookByTheBookOptions = new FCookByTheBookOptions();
		for (TObjectIterator<UPackage> It; It; ++It)
		{
			if ((*It) != GetTransientPackage())
			{
				CookByTheBookOptions->StartupPackages.Add(It->GetFName());
				UE_LOG(LogCook, Verbose, TEXT("Cooker startup package %s"), *It->GetName());
			}
		}
	}
	
	UE_LOG(LogCook, Display, TEXT("Mobile HDR setting %d"), IsMobileHDR());

	// See if there are any plugins that need to be remapped for the sandbox
	const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject();
	if (Project != nullptr)
	{
		PluginsToRemap = IPluginManager::Get().GetEnabledPlugins();
		TArray<FString> AdditionalPluginDirs = Project->GetAdditionalPluginDirectories();
		// Remove any plugin that is in the additional directories since they are handled normally and don't need remapping
		for (int32 Index = PluginsToRemap.Num() - 1; Index >= 0; Index--)
		{
			bool bRemove = true;
			for (const FString& PluginDir : AdditionalPluginDirs)
			{
				// If this plugin is in a directory that needs remapping
				if (PluginsToRemap[Index]->GetBaseDir().StartsWith(PluginDir))
				{
					bRemove = false;
					break;
				}
			}
			if (bRemove)
			{
				PluginsToRemap.RemoveAt(Index);
			}
		}
	}

	bool bDisableEDLWarning = false;
	GConfig->GetBool(TEXT("/Script/Engine.StreamingSettings"), TEXT("s.DisableEDLDeprecationWarnings"), /* out */ bDisableEDLWarning, GEngineIni);
	if (!IsEventDrivenLoaderEnabledInCookedBuilds() && !bDisableEDLWarning)
	{
		UE_LOG(LogCook, Warning, TEXT("Cooking with Event Driven Loader disabled. Loading code will use deprecated path which will be removed in future release."));
	}
}

bool UCookOnTheFlyServer::Exec(class UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	if (FParse::Command(&Cmd, TEXT("package")))
	{
		FString PackageName;
		if (!FParse::Value(Cmd, TEXT("name="), PackageName))
		{
			Ar.Logf(TEXT("Required package name for cook package function. \"cook package name=<name> platform=<platform>\""));
			return true;
		}

		FString PlatformName;
		if (!FParse::Value(Cmd, TEXT("platform="), PlatformName))
		{
			Ar.Logf(TEXT("Required package name for cook package function. \"cook package name=<name> platform=<platform>\""));
			return true;
		}

		if (FPackageName::IsShortPackageName(PackageName))
		{
			FString OutFilename;
			if (FPackageName::SearchForPackageOnDisk(PackageName, NULL, &OutFilename))
			{
				PackageName = OutFilename;
			}
		}

		FName RawPackageName(*PackageName);
		TArray<FName> PackageNames;
		PackageNames.Add(RawPackageName);

		GenerateLongPackageNames(PackageNames);
		

		ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();
		ITargetPlatform* TargetPlatform = TPM.FindTargetPlatform(PlatformName);
		if (TargetPlatform == nullptr)
		{
			Ar.Logf(TEXT("Target platform %s wasn't found."), *PlatformName);
			return true;
		}

		FCookByTheBookStartupOptions StartupOptions;

		StartupOptions.TargetPlatforms.Add(TargetPlatform);
		for (const FName& StandardPackageName : PackageNames)
		{
			FName PackageFileFName = PackageNameCache->GetCachedStandardPackageFileFName(StandardPackageName);
			StartupOptions.CookMaps.Add(StandardPackageName.ToString());
		}
		StartupOptions.CookOptions = ECookByTheBookOptions::NoAlwaysCookMaps | ECookByTheBookOptions::NoDefaultMaps | ECookByTheBookOptions::NoGameAlwaysCookPackages | ECookByTheBookOptions::NoInputPackages | ECookByTheBookOptions::NoSlatePackages | ECookByTheBookOptions::DisableUnsolicitedPackages | ECookByTheBookOptions::ForceDisableSaveGlobalShaders;
		
		StartCookByTheBook(StartupOptions);
	}
	else if (FParse::Command(&Cmd, TEXT("clearall")))
	{
		StopAndClearCookedData();
	}
	else if (FParse::Command(&Cmd, TEXT("stats")))
	{
		DumpStats();
	}

	return false;
}

void UCookOnTheFlyServer::DumpStats()
{
	UE_LOG(LogCook, Display, TEXT("IntStats:"));
	UE_LOG(LogCook, Display, TEXT("  %s=%d"), L"LoadPackage", this->StatLoadedPackageCount);
	UE_LOG(LogCook, Display, TEXT("  %s=%d"), L"SavedPackage", this->StatSavedPackageCount);

	OutputHierarchyTimers();
#if PROFILE_NETWORK
	UE_LOG(LogCook, Display, TEXT("Network Stats \n"
		"TimeTillRequestStarted %f\n"
		"TimeTillRequestForfilled %f\n"
		"TimeTillRequestForfilledError %f\n"
		"WaitForAsyncFilesWrites %f\n"),
		TimeTillRequestStarted,
		TimeTillRequestForfilled,
		TimeTillRequestForfilledError,

		WaitForAsyncFilesWrites);
#endif
}

uint32 UCookOnTheFlyServer::NumConnections() const
{
	int Result= 0;
	for ( int i = 0; i < NetworkFileServers.Num(); ++i )
	{
		INetworkFileServer *NetworkFileServer = NetworkFileServers[i];
		if ( NetworkFileServer )
		{
			Result += NetworkFileServer->NumConnections();
		}
	}
	return Result;
}

FString UCookOnTheFlyServer::GetOutputDirectoryOverride() const
{
	FString OutputDirectory = OutputDirectoryOverride;
	// Output directory override.	
	if (OutputDirectory.Len() <= 0)
	{
		if ( IsCookingDLC() )
		{
			check( IsCookByTheBookMode() );
			OutputDirectory = FPaths::Combine(*GetBaseDirectoryForDLC(), TEXT("Saved"), TEXT("Cooked"), TEXT("[Platform]"));
		}
		else if ( IsCookingInEditor() )
		{
			// Full path so that the sandbox wrapper doesn't try to re-base it under Sandboxes
			OutputDirectory = FPaths::Combine(*FPaths::ProjectDir(), TEXT("Saved"), TEXT("EditorCooked"), TEXT("[Platform]"));
		}
		else
		{
			// Full path so that the sandbox wrapper doesn't try to re-base it under Sandboxes
			OutputDirectory = FPaths::Combine(*FPaths::ProjectDir(), TEXT("Saved"), TEXT("Cooked"), TEXT("[Platform]"));
		}
		
		OutputDirectory = FPaths::ConvertRelativePathToFull(OutputDirectory);
	}
	else if (!OutputDirectory.Contains(TEXT("[Platform]"), ESearchCase::IgnoreCase, ESearchDir::FromEnd) )
	{
		// Output directory needs to contain [Platform] token to be able to cook for multiple targets.
		if ( IsCookByTheBookMode() )
		{
			checkf(PlatformManager->GetSessionPlatforms().Num() == 1,
				TEXT("If OutputDirectoryOverride is provided when cooking multiple platforms, it must include [Platform] in the text, to be replaced with the name of each of the requested Platforms.") );
		}
		else
		{
			// In cook on the fly mode we always add a [Platform] subdirectory rather than requiring the command-line user to include it in their path it because we assume they 
			// don't know which platforms they are cooking for up front
			OutputDirectory = FPaths::Combine(*OutputDirectory, TEXT("[Platform]"));
		}
	}
	FPaths::NormalizeDirectoryName(OutputDirectory);

	return OutputDirectory;
}

template<class T>
void GetVersionFormatNumbersForIniVersionStrings( TArray<FString>& IniVersionStrings, const FString& FormatName, const TArray<const T> &FormatArray )
{
	for ( const T& Format : FormatArray )
	{
		TArray<FName> SupportedFormats;
		Format->GetSupportedFormats(SupportedFormats);
		for ( const FName& SupportedFormat : SupportedFormats )
		{
			int32 VersionNumber = Format->GetVersion(SupportedFormat);
			FString IniVersionString = FString::Printf( TEXT("%s:%s:VersionNumber%d"), *FormatName, *SupportedFormat.ToString(), VersionNumber);
			IniVersionStrings.Emplace( IniVersionString );
		}
	}
}




template<class T>
void GetVersionFormatNumbersForIniVersionStrings(TMap<FString, FString>& IniVersionMap, const FString& FormatName, const TArray<T> &FormatArray)
{
	for (const T& Format : FormatArray)
	{
		TArray<FName> SupportedFormats;
		Format->GetSupportedFormats(SupportedFormats);
		for (const FName& SupportedFormat : SupportedFormats)
		{
			int32 VersionNumber = Format->GetVersion(SupportedFormat);
			FString IniVersionString = FString::Printf(TEXT("%s:%s:VersionNumber"), *FormatName, *SupportedFormat.ToString());
			IniVersionMap.Add(IniVersionString, FString::Printf(TEXT("%d"), VersionNumber));
		}
	}
}


void GetAdditionalCurrentIniVersionStrings( const ITargetPlatform* TargetPlatform, TMap<FString, FString>& IniVersionMap )
{
	FConfigFile EngineSettings;
	FConfigCacheIni::LoadLocalIniFile(EngineSettings, TEXT("Engine"), true, *TargetPlatform->PlatformName());

	TArray<FString> VersionedRValues;
	EngineSettings.GetArray(TEXT("/Script/UnrealEd.CookerSettings"), TEXT("VersionedIntRValues"), VersionedRValues);

	for (const FString& RValue : VersionedRValues)
	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(*RValue);
		if (CVar)
		{
			IniVersionMap.Add(*RValue, FString::Printf(TEXT("%d"), CVar->GetValueOnGameThread()));
		}
	}

	// save off the ddc version numbers also
	ITargetPlatformManagerModule* TPM = GetTargetPlatformManager();
	check(TPM);

	{
		TArray<FName> AllWaveFormatNames;
		TargetPlatform->GetAllWaveFormats(AllWaveFormatNames);
		TArray<const IAudioFormat*> SupportedWaveFormats;
		for ( const auto& WaveName : AllWaveFormatNames )
		{
			const IAudioFormat* AudioFormat = TPM->FindAudioFormat(WaveName);
			if (AudioFormat)
			{
				SupportedWaveFormats.Add(AudioFormat);
			}
			else
			{
				UE_LOG(LogCook, Warning, TEXT("Unable to find audio format \"%s\" which is required by \"%s\""), *WaveName.ToString(), *TargetPlatform->PlatformName());
			}
			
		}
		GetVersionFormatNumbersForIniVersionStrings(IniVersionMap, TEXT("AudioFormat"), SupportedWaveFormats);
	}

	{
		TArray<FName> AllTextureFormats;
		TargetPlatform->GetAllTextureFormats(AllTextureFormats);
		TArray<const ITextureFormat*> SupportedTextureFormats;
		for (const auto& TextureName : AllTextureFormats)
		{
			const ITextureFormat* TextureFormat = TPM->FindTextureFormat(TextureName);
			if ( TextureFormat )
			{
				SupportedTextureFormats.Add(TextureFormat);
			}
			else
			{
				UE_LOG(LogCook, Warning, TEXT("Unable to find texture format \"%s\" which is required by \"%s\""), *TextureName.ToString(), *TargetPlatform->PlatformName());
			}
		}
		GetVersionFormatNumbersForIniVersionStrings(IniVersionMap, TEXT("TextureFormat"), SupportedTextureFormats);
	}

	{
		TArray<FName> AllFormatNames;
		TargetPlatform->GetAllTargetedShaderFormats(AllFormatNames);
		TArray<const IShaderFormat*> SupportedFormats;
		for (const auto& FormatName : AllFormatNames)
		{
			const IShaderFormat* Format = TPM->FindShaderFormat(FormatName);
			if ( Format )
			{
				SupportedFormats.Add(Format);
			}
			else
			{
				UE_LOG(LogCook, Warning, TEXT("Unable to find shader \"%s\" which is required by format \"%s\""), *FormatName.ToString(), *TargetPlatform->PlatformName());
			}
		}
		GetVersionFormatNumbersForIniVersionStrings(IniVersionMap, TEXT("ShaderFormat"), SupportedFormats);
	}


	// TODO: Add support for physx version tracking, currently this happens so infrequently that invalidating a cook based on it is not essential
	//GetVersionFormatNumbersForIniVersionStrings(IniVersionMap, TEXT("PhysXCooking"), TPM->GetPhysXCooking());


	if ( FParse::Param( FCommandLine::Get(), TEXT("fastcook") ) )
	{
		IniVersionMap.Add(TEXT("fastcook"));
	}

	FCustomVersionContainer AllCurrentVersions = FCurrentCustomVersions::GetAll();
	for (const FCustomVersion& CustomVersion : AllCurrentVersions.GetAllVersions())
	{
		FString CustomVersionString = FString::Printf(TEXT("%s:%s"), *CustomVersion.GetFriendlyName().ToString(), *CustomVersion.Key.ToString());
		FString CustomVersionValue = FString::Printf(TEXT("%d"), CustomVersion.Version);
		IniVersionMap.Add(CustomVersionString, CustomVersionValue);
	}

	FString UE4Ver = FString::Printf(TEXT("PackageFileVersions:%d"), GPackageFileUE4Version);
	FString UE4Value = FString::Printf(TEXT("%d"), GPackageFileLicenseeUE4Version);
	IniVersionMap.Add(UE4Ver, UE4Value);

	/*FString UE4EngineVersionCompatibleName = TEXT("EngineVersionCompatibleWith");
	FString UE4EngineVersionCompatible = FEngineVersion::CompatibleWith().ToString();
	
	if ( UE4EngineVersionCompatible.Len() )
	{
		IniVersionMap.Add(UE4EngineVersionCompatibleName, UE4EngineVersionCompatible);
	}*/

	IniVersionMap.Add(TEXT("MaterialShaderMapDDCVersion"), *GetMaterialShaderMapDDCKey());
	IniVersionMap.Add(TEXT("GlobalDDCVersion"), *GetGlobalShaderMapDDCKey());
}



bool UCookOnTheFlyServer::GetCurrentIniVersionStrings( const ITargetPlatform* TargetPlatform, FIniSettingContainer& IniVersionStrings ) const
{
	IniVersionStrings = AccessedIniStrings;

	// this should be called after the cook is finished
	TArray<FString> IniFiles;
	GConfig->GetConfigFilenames(IniFiles);

	TMap<FString, int32> MultiMapCounter;

	for ( const FString& ConfigFilename : IniFiles )
	{
		if ( ConfigFilename.Contains(TEXT("CookedIniVersion.txt")) )
		{
			continue;
		}

		const FConfigFile *ConfigFile = GConfig->FindConfigFile(ConfigFilename);
		ProcessAccessedIniSettings(ConfigFile, IniVersionStrings);
		
	}

	for (const FConfigFile* ConfigFile : OpenConfigFiles)
	{
		ProcessAccessedIniSettings(ConfigFile, IniVersionStrings);
	}


	// remove any which are filtered out
	for ( const FString& Filter : ConfigSettingBlacklist )
	{
		TArray<FString> FilterArray;
		Filter.ParseIntoArray( FilterArray, TEXT(":"));

		FString *ConfigFileName = nullptr;
		FString *SectionName = nullptr;
		FString *ValueName = nullptr;
		switch ( FilterArray.Num() )
		{
		case 3:
			ValueName = &FilterArray[2];
		case 2:
			SectionName = &FilterArray[1];
		case 1:
			ConfigFileName = &FilterArray[0];
			break;
		default:
			continue;
		}

		if ( ConfigFileName )
		{
			for ( auto ConfigFile = IniVersionStrings.CreateIterator(); ConfigFile; ++ConfigFile )
			{
				if ( ConfigFile.Key().ToString().MatchesWildcard(*ConfigFileName) )
				{
					if ( SectionName )
					{
						for ( auto Section = ConfigFile.Value().CreateIterator(); Section; ++Section )
						{
							if ( Section.Key().ToString().MatchesWildcard(*SectionName))
							{
								if (ValueName)
								{
									for ( auto Value = Section.Value().CreateIterator(); Value; ++Value )
									{
										if ( Value.Key().ToString().MatchesWildcard(*ValueName))
										{
											Value.RemoveCurrent();
										}
									}
								}
								else
								{
									Section.RemoveCurrent();
								}
							}
						}
					}
					else
					{
						ConfigFile.RemoveCurrent();
					}
				}
			}
		}
	}
	return true;
}


bool UCookOnTheFlyServer::GetCookedIniVersionStrings(const ITargetPlatform* TargetPlatform, FIniSettingContainer& OutIniSettings, TMap<FString,FString>& OutAdditionalSettings) const
{
	const FString EditorIni = FPaths::ProjectDir() / TEXT("Metadata") / TEXT("CookedIniVersion.txt");
	const FString SandboxEditorIni = ConvertToFullSandboxPath(*EditorIni, true);


	const FString PlatformSandboxEditorIni = SandboxEditorIni.Replace(TEXT("[Platform]"), *TargetPlatform->PlatformName());

	TArray<FString> SavedIniVersionedParams;

	FConfigFile ConfigFile;
	ConfigFile.Read(*PlatformSandboxEditorIni);

	

	const static FName NAME_UsedSettings(TEXT("UsedSettings")); 
	const FConfigSection* UsedSettings = ConfigFile.Find(NAME_UsedSettings.ToString());
	if (UsedSettings == nullptr)
	{
		return false;
	}


	const static FName NAME_AdditionalSettings(TEXT("AdditionalSettings"));
	const FConfigSection* AdditionalSettings = ConfigFile.Find(NAME_AdditionalSettings.ToString());
	if (AdditionalSettings == nullptr)
	{
		return false;
	}


	for (const auto& UsedSetting : *UsedSettings )
	{
		FName Key = UsedSetting.Key;
		const FConfigValue& UsedValue = UsedSetting.Value;

		TArray<FString> SplitString;
		Key.ToString().ParseIntoArray(SplitString, TEXT(":"));

		if (SplitString.Num() != 4)
		{
			UE_LOG(LogCook, Warning, TEXT("Found unparsable ini setting %s for platform %s, invalidating cook."), *Key.ToString(), *TargetPlatform->PlatformName());
			return false;
		}


		check(SplitString.Num() == 4); // We generate this ini file in SaveCurrentIniSettings
		const FString& Filename = SplitString[0];
		const FString& SectionName = SplitString[1];
		const FString& ValueName = SplitString[2];
		const int32 ValueIndex = FCString::Atoi(*SplitString[3]);

		auto& OutFile = OutIniSettings.FindOrAdd(FName(*Filename));
		auto& OutSection = OutFile.FindOrAdd(FName(*SectionName));
		auto& ValueArray = OutSection.FindOrAdd(FName(*ValueName));
		if ( ValueArray.Num() < (ValueIndex+1) )
		{
			ValueArray.AddZeroed( ValueIndex - ValueArray.Num() +1 );
		}
		ValueArray[ValueIndex] = UsedValue.GetSavedValue();
	}



	for (const auto& AdditionalSetting : *AdditionalSettings)
	{
		const FName& Key = AdditionalSetting.Key;
		const FString& Value = AdditionalSetting.Value.GetSavedValue();
		OutAdditionalSettings.Add(Key.ToString(), Value);
	}

	return true;
}



void UCookOnTheFlyServer::OnFConfigCreated(const FConfigFile* Config)
{
	FScopeLock Lock(&ConfigFileCS);
	if (IniSettingRecurse)
	{
		return;
	}

	OpenConfigFiles.Add(Config);
}

void UCookOnTheFlyServer::OnFConfigDeleted(const FConfigFile* Config)
{
	FScopeLock Lock(&ConfigFileCS);
	if (IniSettingRecurse)
	{
		return;
	}

	ProcessAccessedIniSettings(Config, AccessedIniStrings);

	OpenConfigFiles.Remove(Config);
}


void UCookOnTheFlyServer::ProcessAccessedIniSettings(const FConfigFile* Config, FIniSettingContainer& OutAccessedIniStrings) const
{	
	if (Config->Name == NAME_None)
	{
		return;
	}
	// try figure out if this config file is for a specific platform 
	ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();
	const TArray<ITargetPlatform*>& Platforms = TPM.GetTargetPlatforms();
	FString PlatformName;
	bool bFoundPlatformName = false;
	for (const ITargetPlatform* Platform : Platforms )
	{
		FString CurrentPlatformName = Platform->IniPlatformName();
		for ( const auto& SourceIni : Config->SourceIniHierarchy )
		{
			if ( SourceIni.Value.Filename.Contains(CurrentPlatformName) )
			{
				PlatformName = CurrentPlatformName;
				bFoundPlatformName = true;
				break;
			}
		}
		if ( bFoundPlatformName )
		{
			break;
		}
	}

	


	FString ConfigName = bFoundPlatformName ? FString::Printf(TEXT("%s.%s"),*PlatformName, *Config->Name.ToString()) : Config->Name.ToString();
	const FName& ConfigFName = FName(*ConfigName);
	
	for ( auto& ConfigSection : *Config )
	{
		TSet<FName> ProcessedValues; 
		const FName SectionName = FName(*ConfigSection.Key);

		if ( SectionName.GetPlainNameString().Contains(TEXT(":")) )
		{
			UE_LOG(LogCook, Verbose, TEXT("Ignoring ini section checking for section name %s because it contains ':'"), *SectionName.ToString() );
			continue;
		}

		for ( auto& ConfigValue : ConfigSection.Value )
		{
			const FName& ValueName = ConfigValue.Key;
			if ( ProcessedValues.Contains(ValueName) )
				continue;

			ProcessedValues.Add(ValueName);

			if (ValueName.GetPlainNameString().Contains(TEXT(":")))
			{
				UE_LOG(LogCook, Verbose, TEXT("Ignoring ini section checking for section name %s because it contains ':'"), *ValueName.ToString());
				continue;
			}

			
			TArray<FConfigValue> ValueArray;
			ConfigSection.Value.MultiFind( ValueName, ValueArray, true );

			bool bHasBeenAccessed = false;
			for (const auto& ValueArrayEntry : ValueArray)
			{
				if (ValueArrayEntry.HasBeenRead())
				{
					bHasBeenAccessed = true;
					break;
				}
			}

			if ( bHasBeenAccessed )
			{
				auto& AccessedConfig = OutAccessedIniStrings.FindOrAdd(ConfigFName);
				auto& AccessedSection = AccessedConfig.FindOrAdd(SectionName);
				auto& AccessedKey = AccessedSection.FindOrAdd(ValueName);
				AccessedKey.Empty();
				for ( const auto& ValueArrayEntry : ValueArray )
				{
					FString RemovedColon = ValueArrayEntry.GetSavedValue().Replace(TEXT(":"), TEXT(""));
					AccessedKey.Add(RemovedColon);
				}
			}
			
		}
	}
}




bool UCookOnTheFlyServer::IniSettingsOutOfDate(const ITargetPlatform* TargetPlatform) const
{
	FScopeAssign<bool> A = FScopeAssign<bool>(IniSettingRecurse, true);

	FIniSettingContainer OldIniSettings;
	TMap<FString, FString> OldAdditionalSettings;
	if ( GetCookedIniVersionStrings(TargetPlatform, OldIniSettings, OldAdditionalSettings) == false)
	{
		UE_LOG(LogCook, Display, TEXT("Unable to read previous cook inisettings for platform %s invalidating cook"), *TargetPlatform->PlatformName());
		return true;
	}

	// compare against current settings
	TMap<FString, FString> CurrentAdditionalSettings;
	GetAdditionalCurrentIniVersionStrings(TargetPlatform, CurrentAdditionalSettings);

	for ( const auto& OldIniSetting : OldAdditionalSettings)
	{
		const FString* CurrentValue = CurrentAdditionalSettings.Find(OldIniSetting.Key);
		if ( !CurrentValue )
		{
			UE_LOG(LogCook, Display, TEXT("Previous cook had additional ini setting: %s current cook is missing this setting."), *OldIniSetting.Key);
			return true;
		}

		if ( *CurrentValue != OldIniSetting.Value )
		{
			UE_LOG(LogCook, Display, TEXT("Additional Setting from previous cook %s doesn't match %s %s"), *OldIniSetting.Key, **CurrentValue, *OldIniSetting.Value );
			return true;
		}
	}

	for ( const auto& OldIniFile : OldIniSettings )
	{
		const FName& ConfigNameKey = OldIniFile.Key;

		TArray<FString> ConfigNameArray;
		ConfigNameKey.ToString().ParseIntoArray(ConfigNameArray, TEXT("."));
		FString Filename;
		FString PlatformName;
		bool bFoundPlatformName = false;
		if ( ConfigNameArray.Num() <= 1 )
		{
			Filename = ConfigNameKey.ToString();
		}
		else if ( ConfigNameArray.Num() == 2 )
		{
			PlatformName = ConfigNameArray[0];
			Filename = ConfigNameArray[1];
			bFoundPlatformName = true;
		}
		else
		{
			UE_LOG( LogCook, Warning, TEXT("Found invalid file name in old ini settings file Filename %s settings file %s"), *ConfigNameKey.ToString(), *TargetPlatform->PlatformName() );
			return true;
		}
		
		const FConfigFile* ConfigFile = nullptr;
		FConfigFile Temp;
		if ( bFoundPlatformName)
		{
			GConfig->LoadLocalIniFile(Temp, *Filename, true, *PlatformName );
			ConfigFile = &Temp;
		}
		else
		{
			ConfigFile = GConfig->Find(Filename, false);
		}
		FName FileFName = FName(*Filename);
		if ( !ConfigFile )
		{
			for( const auto& File : *GConfig )
			{
				if (File.Value.Name == FileFName)
				{
					ConfigFile = &File.Value;
					break;
				}
			}
			if ( !ConfigFile )
			{
				UE_LOG(LogCook, Display, TEXT("Unable to find config file %s invalidating inisettings"), *FString::Printf(TEXT("%s %s"), *PlatformName, *Filename));
				return true;
			}
		}
		for ( const auto& OldIniSection : OldIniFile.Value )
		{
			
			const FName& SectionName = OldIniSection.Key;
			const FConfigSection* IniSection = ConfigFile->Find( SectionName.ToString() );

			const FString BlackListSetting = *FString::Printf(TEXT("%s.%s:%s"), *PlatformName, *Filename, *SectionName.ToString());

			if ( IniSection == nullptr )
			{
				UE_LOG(LogCook, Display, TEXT("Inisetting is different for %s, Current section doesn't exist"), *FString::Printf(TEXT("%s %s %s"), *PlatformName, *Filename, *SectionName.ToString()));
				UE_LOG(LogCook, Display, TEXT("To avoid this add blacklist setting to DefaultEditor.ini [CookSettings] %s"), *BlackListSetting);
				return true;
			}

			for ( const auto& OldIniValue : OldIniSection.Value )
			{
				const FName& ValueName = OldIniValue.Key;

				TArray<FConfigValue> CurrentValues;
				IniSection->MultiFind( ValueName, CurrentValues, true );

				if ( CurrentValues.Num() != OldIniValue.Value.Num() )
				{
					UE_LOG(LogCook, Display, TEXT("Inisetting is different for %s, missmatched num array elements %d != %d "), *FString::Printf(TEXT("%s %s %s %s"), *PlatformName, *Filename, *SectionName.ToString(), *ValueName.ToString()), CurrentValues.Num(), OldIniValue.Value.Num());
					UE_LOG(LogCook, Display, TEXT("To avoid this add blacklist setting to DefaultEditor.ini [CookSettings] %s"), *BlackListSetting);
					return true;
				}
				for ( int Index = 0; Index < CurrentValues.Num(); ++Index )
				{
					const FString FilteredCurrentValue = CurrentValues[Index].GetSavedValue().Replace(TEXT(":"), TEXT(""));
					if ( FilteredCurrentValue != OldIniValue.Value[Index] )
					{
						UE_LOG(LogCook, Display, TEXT("Inisetting is different for %s, value %s != %s invalidating cook"),  *FString::Printf(TEXT("%s %s %s %s %d"),*PlatformName, *Filename, *SectionName.ToString(), *ValueName.ToString(), Index), *CurrentValues[Index].GetSavedValue(), *OldIniValue.Value[Index] );
						UE_LOG(LogCook, Display, TEXT("To avoid this add blacklist setting to DefaultEditor.ini [CookSettings] %s"), *BlackListSetting);
						return true;
					}
				}
				
			}
		}
	}

	return false;
}

bool UCookOnTheFlyServer::SaveCurrentIniSettings(const ITargetPlatform* TargetPlatform) const
{
	FScopeAssign<bool> S = FScopeAssign<bool>(IniSettingRecurse, true);

	TMap<FString, FString> AdditionalIniSettings;
	GetAdditionalCurrentIniVersionStrings(TargetPlatform, AdditionalIniSettings);

	FIniSettingContainer CurrentIniSettings;
	GetCurrentIniVersionStrings(TargetPlatform, CurrentIniSettings);

	const FString EditorIni = FPaths::ProjectDir() / TEXT("Metadata") / TEXT("CookedIniVersion.txt");
	const FString SandboxEditorIni = ConvertToFullSandboxPath(*EditorIni, true);


	const FString PlatformSandboxEditorIni = SandboxEditorIni.Replace(TEXT("[Platform]"), *TargetPlatform->PlatformName());


	FConfigFile ConfigFile;
	// ConfigFile.Read(*PlatformSandboxEditorIni);

	ConfigFile.Dirty = true;
	const static FName NAME_UsedSettings(TEXT("UsedSettings"));
	ConfigFile.Remove(NAME_UsedSettings.ToString());
	FConfigSection& UsedSettings = ConfigFile.FindOrAdd(NAME_UsedSettings.ToString());


	{
		SCOPE_TIMER(ProcessingAccessedStrings)
		for (const auto& CurrentIniFilename : CurrentIniSettings)
		{
			const FName& Filename = CurrentIniFilename.Key;
			for ( const auto& CurrentSection : CurrentIniFilename.Value )
			{
				const FName& Section = CurrentSection.Key;
				for ( const auto& CurrentValue : CurrentSection.Value )
				{
					const FName& ValueName = CurrentValue.Key;
					const TArray<FString>& Values = CurrentValue.Value;

					for ( int Index = 0; Index < Values.Num(); ++Index )
					{
						FString NewKey = FString::Printf(TEXT("%s:%s:%s:%d"), *Filename.ToString(), *Section.ToString(), *ValueName.ToString(), Index);
						UsedSettings.Add(FName(*NewKey), Values[Index]);
					}
				}
			}
		}
	}


	const static FName NAME_AdditionalSettings(TEXT("AdditionalSettings"));
	ConfigFile.Remove(NAME_AdditionalSettings.ToString());
	FConfigSection& AdditionalSettings = ConfigFile.FindOrAdd(NAME_AdditionalSettings.ToString());

	for (const auto& AdditionalIniSetting : AdditionalIniSettings)
	{
		AdditionalSettings.Add( FName(*AdditionalIniSetting.Key), AdditionalIniSetting.Value );
	}

	ConfigFile.Write(PlatformSandboxEditorIni);


	return true;

}

FName UCookOnTheFlyServer::ConvertCookedPathToUncookedPath(
	const FString& SandboxRootDir, const FString& RelativeRootDir,
	const FString& SandboxProjectDir, const FString& RelativeProjectDir,
	const FString& CookedPath, FString& OutUncookedPath) const
{
	OutUncookedPath.Reset();

	// Check for remapped plugins' cooked content
	if (PluginsToRemap.Num() > 0 && CookedPath.Contains(REMAPPED_PLUGGINS))
	{
		int32 RemappedIndex = CookedPath.Find(REMAPPED_PLUGGINS);
		check(RemappedIndex >= 0);
		static uint32 RemappedPluginStrLen = FCString::Strlen(REMAPPED_PLUGGINS);
		// Snip everything up through the RemappedPlugins/ off so we can find the plugin it corresponds to
		FString PluginPath = CookedPath.RightChop(RemappedIndex + RemappedPluginStrLen + 1);
		// Find the plugin that owns this content
		for (TSharedRef<IPlugin> Plugin : PluginsToRemap)
		{
			if (PluginPath.StartsWith(Plugin->GetName()))
			{
				OutUncookedPath = Plugin->GetContentDir();
				static uint32 ContentStrLen = FCString::Strlen(TEXT("Content/"));
				// Chop off the pluginName/Content since it's part of the full path
				OutUncookedPath /= PluginPath.RightChop(Plugin->GetName().Len() + ContentStrLen);
				break;
			}
		}

		if (OutUncookedPath.Len() > 0)
		{
			return FName(*OutUncookedPath);
		}
		// Otherwise fall through to sandbox handling
	}

	auto BuildUncookedPath = 
		[&OutUncookedPath](const FString& CookedPath, const FString& CookedRoot, const FString& UncookedRoot)
	{
		OutUncookedPath.AppendChars(*UncookedRoot, UncookedRoot.Len());
		OutUncookedPath.AppendChars(*CookedPath + CookedRoot.Len(), CookedPath.Len() - CookedRoot.Len());
	};

	if (CookedPath.StartsWith(SandboxRootDir))
	{
		// Optimized CookedPath.StartsWith(SandboxProjectDir) that does not compare all of SandboxRootDir again
		if (CookedPath.Len() >= SandboxProjectDir.Len() && 
			0 == FCString::Strnicmp(
				*CookedPath + SandboxRootDir.Len(),
				*SandboxProjectDir + SandboxRootDir.Len(),
				SandboxProjectDir.Len() - SandboxRootDir.Len()))
		{
			BuildUncookedPath(CookedPath, SandboxProjectDir, RelativeProjectDir);
		}
		else
		{
			BuildUncookedPath(CookedPath, SandboxRootDir, RelativeRootDir);
		}
	}
	else
	{
		FString FullCookedFilename = FPaths::ConvertRelativePathToFull(CookedPath);
		BuildUncookedPath(FullCookedFilename, SandboxRootDir, RelativeRootDir);
	}

	// Convert to a standard filename as required by FPackageNameCache where this path is used.
	FPaths::MakeStandardFilename(OutUncookedPath);

	return FName(*OutUncookedPath);
}

void UCookOnTheFlyServer::GetAllCookedFiles(TMap<FName, FName>& UncookedPathToCookedPath, const FString& SandboxRootDir)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UCookOnTheFlyServer::GetAllCookedFiles);

	TArray<FString> CookedFiles;
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		FPackageSearchVisitor PackageSearch(CookedFiles);
		PlatformFile.IterateDirectoryRecursively(*SandboxRootDir, PackageSearch);
	}

	const FString SandboxProjectDir = FPaths::Combine(*SandboxRootDir, FApp::GetProjectName()) + TEXT("/");
	const FString RelativeRootDir = FPaths::GetRelativePathToRoot();
	const FString RelativeProjectDir = FPaths::ProjectDir();
	FString UncookedFilename;
	UncookedFilename.Reserve(1024);

	for (const FString& CookedFile : CookedFiles)
	{
		const FName CookedFName(*CookedFile);
		const FName UncookedFName = ConvertCookedPathToUncookedPath(
			SandboxRootDir, RelativeRootDir,
			SandboxProjectDir, RelativeProjectDir,
			CookedFile, UncookedFilename);

		UncookedPathToCookedPath.Add(UncookedFName, CookedFName);
	}
}

void UCookOnTheFlyServer::DeleteSandboxDirectory(const FString& PlatformName)
{
	FString SandboxDirectory = GetSandboxDirectory(PlatformName);
	FPaths::NormalizeDirectoryName(SandboxDirectory);
	FString AsyncDeleteDirectory = GetAsyncDeleteDirectory(PlatformName, &SandboxDirectory);

	FAsyncIODelete& LocalAsyncIODelete = GetAsyncIODelete(PlatformName, &AsyncDeleteDirectory);
	LocalAsyncIODelete.DeleteDirectory(SandboxDirectory);

	// Part of Deleting the sandbox includes deleting the old AsyncDelete directory for the sandbox, in case a previous cooker crashed before cleaning it up.
	// The AsyncDelete directory is associated with a sandbox but is necessarily outside of it since it is used to delete the sandbox.
	// Note that for the Platform we used to create the AsyncIODelete, this Delete will fail because AsyncIODelete refuses to delete its own temproot; this is okay because it will delete the temproot on exit.
	LocalAsyncIODelete.DeleteDirectory(AsyncDeleteDirectory);

	// UE_DEPRECATED(4.25, "Delete the old location for AsyncDeleteDirectory until all users have cooked at least once")
	LocalAsyncIODelete.DeleteDirectory(SandboxDirectory + TEXT("AsyncDelete"));
}

FAsyncIODelete& UCookOnTheFlyServer::GetAsyncIODelete(const FString& PlatformName, const FString* AsyncDeleteDirectory)
{
	FAsyncIODelete* AsyncIODeletePtr = AsyncIODelete.Get();
	if (!AsyncIODeletePtr)
	{
		FString Buffer;
		if (!AsyncDeleteDirectory)
		{
			Buffer = GetAsyncDeleteDirectory(PlatformName);
			AsyncDeleteDirectory = &Buffer;
		}
		AsyncIODelete = MakeUnique<FAsyncIODelete>(*AsyncDeleteDirectory);
		AsyncIODeletePtr = AsyncIODelete.Get();
	}
	// If we have already created the AsyncIODelete, we ignore the input PlatformName and use the existing AsyncIODelete initialized from whatever platform we used before
	// The PlatformName is used only to construct a directory that we can be sure no other process is using (because a sandbox can only be cooked by one process at a time)
	return *AsyncIODeletePtr;
}

FString UCookOnTheFlyServer::GetAsyncDeleteDirectory(const FString& PlatformName, const FString* SandboxDirectory) const
{
	// The TempRoot we will delete into is a sibling of the the Platform-specific sandbox directory, with name [PlatformDir]AsyncDelete
	// Note that two UnrealEd-Cmd processes cooking to the same sandbox at the same time will therefore cause an error since FAsyncIODelete doesn't handle multiple processes sharing TempRoots.
	// That simultaneous-cook behavior is also not supported in other assumptions throughout the cooker.
	FString Buffer;
	if (!SandboxDirectory)
	{
		// Avoid recalculating SandboxDirectory if caller supplied it, but if they didn't, calculate it here
		Buffer = GetSandboxDirectory(PlatformName);
		FPaths::NormalizeDirectoryName(Buffer);
		SandboxDirectory = &Buffer;
	}
	return (*SandboxDirectory) + TEXT("_Del");
}

void UCookOnTheFlyServer::PopulateCookedPackagesFromDisk(const TArrayView<const ITargetPlatform* const>& Platforms)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UCookOnTheFlyServer::PopulateCookedPackagesFromDisk);

	// See what files are out of date in the sandbox folder
	for (int32 Index = 0; Index < Platforms.Num(); Index++)
	{
		TArray<FString> CookedPackagesToDelete;

		const ITargetPlatform* Target = Platforms[Index];
		FPlatformData* PlatformData = PlatformManager->GetPlatformData(Target);
		FString SandboxPath = GetSandboxDirectory(Target->PlatformName());

		FString EngineSandboxPath = SandboxFile->ConvertToSandboxPath(*FPaths::EngineDir()) + TEXT("/");
		EngineSandboxPath.ReplaceInline(TEXT("[Platform]"), *Target->PlatformName());

		FString GameSandboxPath = SandboxFile->ConvertToSandboxPath(*(FPaths::ProjectDir() + TEXT("a.txt")));
		GameSandboxPath.ReplaceInline(TEXT("a.txt"), TEXT(""));
		GameSandboxPath.ReplaceInline(TEXT("[Platform]"), *Target->PlatformName());

		FString LocalGamePath = FPaths::ProjectDir();
		if (FPaths::IsProjectFilePathSet())
		{
			LocalGamePath = FPaths::GetPath(FPaths::GetProjectFilePath()) + TEXT("/");
		}

		FString LocalEnginePath = FPaths::EngineDir();

		static bool bFindCulprit = false; //debugging setting if we want to find culprit for iterative cook issues

		// Registry generator already exists
		FAssetRegistryGenerator* PlatformAssetRegistry = PlatformData->RegistryGenerator.Get();
		check(PlatformAssetRegistry);

		// Load the platform cooked asset registry file
		const FString CookedAssetRegistry = FPaths::ProjectDir() / TEXT("Metadata") / GetDevelopmentAssetRegistryFilename();
		const FString SandboxCookedAssetRegistryFilename = ConvertToFullSandboxPath(*CookedAssetRegistry, true, Target->PlatformName());

		bool bIsIterateSharedBuild = IsCookFlagSet(ECookInitializationFlags::IterateSharedBuild);

		if (bIsIterateSharedBuild)
		{
			// see if the shared build is newer then the current cooked content in the local directory
			FDateTime CurrentLocalCookedBuild = IFileManager::Get().GetTimeStamp(*SandboxCookedAssetRegistryFilename);

			// iterate on the shared build if the option is set
			FString SharedCookedAssetRegistry = FPaths::Combine(*FPaths::ProjectSavedDir(), TEXT("SharedIterativeBuild"), *Target->PlatformName(), TEXT("Metadata"), GetDevelopmentAssetRegistryFilename());

			FDateTime CurrentIterativeCookedBuild = IFileManager::Get().GetTimeStamp(*SharedCookedAssetRegistry);

			if ( (CurrentIterativeCookedBuild >= CurrentLocalCookedBuild) && 
				(CurrentIterativeCookedBuild != FDateTime::MinValue()) )
			{
				// clean the sandbox
				ClearPlatformCookedData(Target);

				// SaveCurrentIniSettings(Target); // use this if we don't care about ini safty.
				// copy the ini settings from the shared cooked build. 
				const FString PlatformName = Target->PlatformName();
				const FString SharedCookedIniFile = FPaths::Combine(*FPaths::ProjectSavedDir(), TEXT("SharedIterativeBuild"), *PlatformName, TEXT("Metadata"), TEXT("CookedIniVersion.txt"));
				const FString SandboxCookedIniFile = ConvertToFullSandboxPath(*(FPaths::ProjectDir() / TEXT("Metadata") / TEXT("CookedIniVersion.txt")), true).Replace(TEXT("[Platform]"), *PlatformName);

				IFileManager::Get().Copy(*SandboxCookedIniFile, *SharedCookedIniFile);

				bool bIniSettingsOutOfDate = IniSettingsOutOfDate(Target);
				if (bIniSettingsOutOfDate && !IsCookFlagSet(ECookInitializationFlags::IgnoreIniSettingsOutOfDate))
				{
					UE_LOG(LogCook, Display, TEXT("Shared iterative build ini settings out of date, not using shared cooked build"));
				}
				else
				{
					if (bIniSettingsOutOfDate)
					{
						UE_LOG(LogCook, Display, TEXT("Shared iterative build ini settings out of date, but we don't care"));
					}

					UE_LOG(LogCook, Display, TEXT("Shared iterative build is newer then local cooked build, iteratively cooking from shared build "));
					PlatformAssetRegistry->LoadPreviousAssetRegistry(SharedCookedAssetRegistry);
				}
			}
			else
			{
				UE_LOG(LogCook, Display, TEXT("Local cook is newer then shared cooked build, iterativly cooking from local build"));
				PlatformAssetRegistry->LoadPreviousAssetRegistry(SandboxCookedAssetRegistryFilename);
			}
		}
		else
		{
			PlatformAssetRegistry->LoadPreviousAssetRegistry(SandboxCookedAssetRegistryFilename);
		}

		// Get list of changed packages
		TSet<FName> ModifiedPackages, NewPackages, RemovedPackages, IdenticalCookedPackages, IdenticalUncookedPackages;

		// We recurse modifications up the reference chain because it is safer, if this ends up being a significant issue in some games we can add a command line flag
		bool bRecurseModifications = true;
		bool bRecurseScriptModifications = !IsCookFlagSet(ECookInitializationFlags::IgnoreScriptPackagesOutOfDate);
		PlatformAssetRegistry->ComputePackageDifferences(ModifiedPackages, NewPackages, RemovedPackages, IdenticalCookedPackages, IdenticalUncookedPackages, bRecurseModifications, bRecurseScriptModifications);

		// check the files on disk 
		TMap<FName, FName> UncookedPathToCookedPath;
		// get all the on disk cooked files
		GetAllCookedFiles(UncookedPathToCookedPath, SandboxPath);

		const static FName NAME_DummyCookedFilename(TEXT("DummyCookedFilename")); // pls never name a package dummycookedfilename otherwise shit might go wonky
		if (bIsIterateSharedBuild)
		{
			check(IFileManager::Get().FileExists(*NAME_DummyCookedFilename.ToString()) == false);

			TSet<FName> ExistingPackages = ModifiedPackages;
			ExistingPackages.Append(RemovedPackages);
			ExistingPackages.Append(IdenticalCookedPackages);
			ExistingPackages.Append(IdenticalUncookedPackages);

			// if we are iterating of a shared build the cooked files might not exist in the cooked directory because we assume they are packaged in the pak file (which we don't want to extract)
			for (FName PackageName : ExistingPackages)
			{
				FString Filename;
				if (FPackageName::DoesPackageExist(PackageName.ToString(), nullptr, &Filename))
				{
					UncookedPathToCookedPath.Add(FName(*Filename), NAME_DummyCookedFilename);
				}
			}
		}

		uint32 NumPackagesConsidered = UncookedPathToCookedPath.Num();
		uint32 NumPackagesUnableToFindCookedPackageInfo = 0;
		uint32 NumPackagesFileHashMismatch = 0;
		uint32 NumPackagesKept = 0;
		uint32 NumMarkedFailedSaveKept = 0;
		uint32 NumPackagesRemoved = 0;

		TArray<FName> KeptPackages;

		for (const auto& CookedPaths : UncookedPathToCookedPath)
		{
			const FName CookedFile = CookedPaths.Value;
			const FName UncookedFilename = CookedPaths.Key;
			const FName* FoundPackageName = PackageNameCache->GetCachedPackageFilenameToPackageFName(UncookedFilename);
			bool bShouldKeep = true;
			const FName SourcePackageName = FoundPackageName ? *FoundPackageName : NAME_None;
			if ( !FoundPackageName )
			{
				// Source file no longer exists
				++NumPackagesRemoved;
				bShouldKeep = false;
			}
			else
			{
				if (ModifiedPackages.Contains(SourcePackageName))
				{
					++NumPackagesFileHashMismatch;
					bShouldKeep = false;
				}
				else if (NewPackages.Contains(SourcePackageName) || RemovedPackages.Contains(SourcePackageName))
				{
					++NumPackagesUnableToFindCookedPackageInfo;
					bShouldKeep = false;
				}
				else if (IdenticalUncookedPackages.Contains(SourcePackageName))
				{
					// These are packages which failed to save the first time 
					// most likely because they are editor only packages
					bShouldKeep = false;
				}
			}

			TArray<const ITargetPlatform*> PlatformsForPackage;
			PlatformsForPackage.Add(Target);

			if (bShouldKeep)
			{
				// Add this package to the CookedPackages list so that we don't try cook it again
				TArray<bool> Succeeded;
				Succeeded.Add(true);

				if (IdenticalCookedPackages.Contains(SourcePackageName))
				{
					PackageTracker->CookedPackages.Add(FFilePlatformCookedPackage(UncookedFilename, MoveTemp(PlatformsForPackage), MoveTemp(Succeeded)));
					KeptPackages.Add(SourcePackageName);
					++NumPackagesKept;
				}
			}
			else
			{
				if (SourcePackageName != NAME_None && IsCookByTheBookMode()) // cook on the fly will requeue this package when it wants it 
				{
					// Force cook the modified file
					PackageTracker->EnqueueUniqueCookRequest(FFilePlatformRequest(UncookedFilename, PlatformsForPackage));
				}
				if (CookedFile != NAME_DummyCookedFilename)
				{
					// delete the old package 
					const FString CookedFullPath = FPaths::ConvertRelativePathToFull(CookedFile.ToString());
					UE_LOG(LogCook, Verbose, TEXT("Deleting cooked package %s failed filehash test"), *CookedFullPath);
					CookedPackagesToDelete.Add(CookedFullPath);
				}
				else
				{
					// the cooker should rebuild this package because it's not in the cooked package list
					// the new package will have higher priority then the package in the original shared cooked build
					const FString UncookedFilenameString = UncookedFilename.ToString();
					UE_LOG(LogCook, Verbose, TEXT("Shared cooked build: Detected package is out of date %s"), *UncookedFilenameString);
				}
			}
		}

		// Register identical uncooked packages from previous run
		for (FName UncookedPackage : IdenticalUncookedPackages)
		{
			const FName UncookedFilename = PackageNameCache->GetCachedStandardPackageFileFName(UncookedPackage);

			TArray<const ITargetPlatform*> PlatformsForPackage;
			PlatformsForPackage.Add(Target);

			ensure(PackageTracker->CookedPackages.Exists(UncookedFilename, PlatformsForPackage, false) == false);

			PackageTracker->CookedPackages.Add(FFilePlatformCookedPackage(UncookedFilename, MoveTemp(PlatformsForPackage)));
			KeptPackages.Add(UncookedPackage);
			++NumMarkedFailedSaveKept;
		}

		PlatformAssetRegistry->UpdateKeptPackages(KeptPackages);

		UE_LOG(LogCook, Display, TEXT("Iterative cooking summary for %s, \nConsidered: %d, \nFile Hash missmatch: %d, \nPackages Kept: %d, \nPackages failed save kept: %d, \nMissing Cooked Info(expected 0): %d"),
			*Target->PlatformName(),
			NumPackagesConsidered, NumPackagesFileHashMismatch,
			NumPackagesKept, NumMarkedFailedSaveKept,
			NumPackagesUnableToFindCookedPackageInfo);

		auto DeletePackageLambda = [&CookedPackagesToDelete](int32 PackageIndex)
		{
			const FString& CookedFullPath = CookedPackagesToDelete[PackageIndex];
			IFileManager::Get().Delete(*CookedFullPath, true, true, true);
		};
		ParallelFor(CookedPackagesToDelete.Num(), DeletePackageLambda);
	}
}

const FString ExtractPackageNameFromObjectPath( const FString ObjectPath )
{
	// get the path 
	int32 Beginning = ObjectPath.Find(TEXT("'"), ESearchCase::CaseSensitive);
	if ( Beginning == INDEX_NONE )
	{
		return ObjectPath;
	}
	int32 End = ObjectPath.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromStart, Beginning + 1);
	if (End == INDEX_NONE )
	{
		End = ObjectPath.Find(TEXT("'"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Beginning + 1);
	}
	if ( End == INDEX_NONE )
	{
		// one more use case is that the path is "Class'Path" example "OrionBoostItemDefinition'/Game/Misc/Boosts/XP_1Win" dunno why but this is actually dumb
		if ( ObjectPath[Beginning+1] == '/' )
		{
			return ObjectPath.Mid(Beginning+1);
		}
		return ObjectPath;
	}
	return ObjectPath.Mid(Beginning + 1, End - Beginning - 1);
}

void UCookOnTheFlyServer::GenerateAssetRegistry()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UCookOnTheFlyServer::GenerateAssetRegistry);

	// Cache asset registry for later
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetRegistry = &AssetRegistryModule.Get();

	// Mark package as dirty for the last ones saved
	if (PackageNameCache != nullptr)
	{
		for (FName AssetFilename : ModifiedAssetFilenames)
		{
			const FString AssetPathOnDisk = AssetFilename.ToString();
			if (FPaths::FileExists(AssetPathOnDisk))
			{
				const FString PackageName = FPackageName::FilenameToLongPackageName(AssetPathOnDisk);
				FSoftObjectPath SoftPackage(PackageName);
				if (UPackage* Package = Cast<UPackage>(SoftPackage.ResolveObject()))
				{
					MarkPackageDirtyForCooker(Package);
				}
			}
		}
	}

	if (!!(CookFlags & ECookInitializationFlags::GeneratedAssetRegistry))
	{
		UE_LOG(LogCook, Display, TEXT("Updating asset registry"));

		// Force a rescan of modified package files
		TArray<FString> ModifiedPackageFileList;

		for (FName ModifiedPackage : ModifiedAssetFilenames)
		{
			ModifiedPackageFileList.Add(ModifiedPackage.ToString());
		}

		AssetRegistry->ScanModifiedAssetFiles(ModifiedPackageFileList);
	}
	else
	{
		CookFlags |= ECookInitializationFlags::GeneratedAssetRegistry;
		UE_LOG(LogCook, Display, TEXT("Creating asset registry"));

		ModifiedAssetFilenames.Reset();

		// Perform a synchronous search of any .ini based asset paths (note that the per-game delegate may
		// have already scanned paths on its own)
		// We want the registry to be fully initialized when generating streaming manifests too.

		// editor will scan asset registry automagically 
		bool bCanDelayAssetregistryProcessing = IsRealtimeMode();

		// if we are running in the editor we need the asset registry to be finished loaded before we process any iterative cook requests
		bCanDelayAssetregistryProcessing &= !IsCookFlagSet(ECookInitializationFlags::Iterative);

		if (!bCanDelayAssetregistryProcessing)
		{
			TArray<FString> ScanPaths;
			if (GConfig->GetArray(TEXT("AssetRegistry"), TEXT("PathsToScanForCook"), ScanPaths, GEngineIni) > 0 && !AssetRegistry->IsLoadingAssets())
			{
				AssetRegistry->ScanPathsSynchronous(ScanPaths);
			}
			else
			{
				// This will flush the background gather if we're in the editor
				AssetRegistry->SearchAllAssets(true);
			}
		}
	}
}

void UCookOnTheFlyServer::RefreshPlatformAssetRegistries(const TArrayView<const ITargetPlatform* const>& TargetPlatforms)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UCookOnTheFlyServer::RefreshPlatformAssetRegistries);

	for (const ITargetPlatform* TargetPlatform : TargetPlatforms)
	{
		FName PlatformName = FName(*TargetPlatform->PlatformName());

		FPlatformData* PlatformData = PlatformManager->GetPlatformData(TargetPlatform);
		FAssetRegistryGenerator* RegistryGenerator = PlatformData->RegistryGenerator.Get();
		if (!RegistryGenerator)
		{
			RegistryGenerator = new FAssetRegistryGenerator(TargetPlatform);
			PlatformData->RegistryGenerator = TUniquePtr<FAssetRegistryGenerator>(RegistryGenerator);
			RegistryGenerator->CleanManifestDirectories();
		}
		RegistryGenerator->Initialize(CookByTheBookOptions ? CookByTheBookOptions->StartupPackages : TArray<FName>());
	}
}

void UCookOnTheFlyServer::GenerateLongPackageNames(TArray<FName>& FilesInPath)
{
	TSet<FName> FilesInPathSet;
	TArray<FName> FilesInPathReverse;
	FilesInPathSet.Reserve(FilesInPath.Num());
	FilesInPathReverse.Reserve(FilesInPath.Num());

	for (int32 FileIndex = 0; FileIndex < FilesInPath.Num(); FileIndex++)
	{
		const FName& FileInPathFName = FilesInPath[FilesInPath.Num() - FileIndex - 1];
		const FString& FileInPath = FileInPathFName.ToString();
		if (FPackageName::IsValidLongPackageName(FileInPath))
		{
			bool bIsAlreadyAdded;
			FilesInPathSet.Add(FileInPathFName, &bIsAlreadyAdded);
			if (!bIsAlreadyAdded)
			{
				FilesInPathReverse.Add(FileInPathFName);
			}
		}
		else
		{
			FString LongPackageName;
			FString FailureReason;
			if (FPackageName::TryConvertFilenameToLongPackageName(FileInPath, LongPackageName, &FailureReason))
			{
				const FName LongPackageFName(*LongPackageName);
				bool bIsAlreadyAdded;
				FilesInPathSet.Add(LongPackageFName, &bIsAlreadyAdded);
				if (!bIsAlreadyAdded)
				{
					FilesInPathReverse.Add(LongPackageFName);
				}
			}
			else
			{
				LogCookerMessage(FString::Printf(TEXT("Unable to generate long package name for %s because %s"), *FileInPath, *FailureReason), EMessageSeverity::Warning);
				UE_LOG(LogCook, Warning, TEXT("Unable to generate long package name for %s because %s"), *FileInPath, *FailureReason);
			}
		}
	}
	FilesInPath.Empty(FilesInPathReverse.Num());
	FilesInPath.Append(FilesInPathReverse);
}

void UCookOnTheFlyServer::AddFileToCook( TArray<FName>& InOutFilesToCook, const FString &InFilename ) const
{ 
	if (!FPackageName::IsScriptPackage(InFilename) && !FPackageName::IsMemoryPackage(InFilename))
	{
		FName InFilenameName = FName(*InFilename );
		if ( InFilenameName == NAME_None)
		{
			return;
		}

		InOutFilesToCook.AddUnique(InFilenameName);
	}
}

void UCookOnTheFlyServer::CollectFilesToCook(TArray<FName>& FilesInPath, const TArray<FString>& CookMaps, const TArray<FString>& InCookDirectories,
	const TArray<FString> &IniMapSections, ECookByTheBookOptions FilesToCookFlags, const TArrayView<const ITargetPlatform* const>& TargetPlatforms)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UCookOnTheFlyServer::CollectFilesToCook);

#if OUTPUT_TIMING
	SCOPE_TIMER(CollectFilesToCook);
#endif
	UProjectPackagingSettings* PackagingSettings = Cast<UProjectPackagingSettings>(UProjectPackagingSettings::StaticClass()->GetDefaultObject());

	bool bCookAll = (!!(FilesToCookFlags & ECookByTheBookOptions::CookAll)) || PackagingSettings->bCookAll;
	bool bMapsOnly = (!!(FilesToCookFlags & ECookByTheBookOptions::MapsOnly)) || PackagingSettings->bCookMapsOnly;
	bool bNoDev = !!(FilesToCookFlags & ECookByTheBookOptions::NoDevContent);

	TArray<FName> InitialPackages = FilesInPath;


	TArray<FString> CookDirectories = InCookDirectories;
	
	if (!IsCookingDLC() && 
		!(FilesToCookFlags & ECookByTheBookOptions::NoAlwaysCookMaps))
	{

		{
			TArray<FString> MapList;
			// Add the default map section
			GEditor->LoadMapListFromIni(TEXT("AlwaysCookMaps"), MapList);

			for (int32 MapIdx = 0; MapIdx < MapList.Num(); MapIdx++)
			{
				UE_LOG(LogCook, Verbose, TEXT("Maplist contains has %s "), *MapList[MapIdx]);
				AddFileToCook(FilesInPath, MapList[MapIdx]);
			}
		}


		bool bFoundMapsToCook = CookMaps.Num() > 0;

		{
			TArray<FString> MapList;
			for (const auto& IniMapSection : IniMapSections)
			{
				UE_LOG(LogCook, Verbose, TEXT("Loading map ini section %s "), *IniMapSection);
				GEditor->LoadMapListFromIni(*IniMapSection, MapList);
			}
			for (int32 MapIdx = 0; MapIdx < MapList.Num(); MapIdx++)
			{
				UE_LOG(LogCook, Verbose, TEXT("Maplist contains has %s "), *MapList[MapIdx]);
				AddFileToCook(FilesInPath, MapList[MapIdx]);
				bFoundMapsToCook = true;
			}
		}

		// If we didn't find any maps look in the project settings for maps
		for (const FFilePath& MapToCook : PackagingSettings->MapsToCook)
		{
			UE_LOG(LogCook, Verbose, TEXT("Maps to cook list contains %s "), *MapToCook.FilePath);
			FilesInPath.Add(FName(*MapToCook.FilePath));
			bFoundMapsToCook = true;
		}



		// if we didn't find maps to cook, and we don't have any commandline maps (CookMaps), then cook the allmaps section
		if (bFoundMapsToCook == false && CookMaps.Num() == 0)
		{
			UE_LOG(LogCook, Verbose, TEXT("Loading default map ini section AllMaps "));
			TArray<FString> AllMapsSection;
			GEditor->LoadMapListFromIni(TEXT("AllMaps"), AllMapsSection);
			for (const FString& MapName : AllMapsSection)
			{
				AddFileToCook(FilesInPath, MapName);
			}
		}

		// Also append any cookdirs from the project ini files; these dirs are relative to the game content directory or start with a / root
		{
			const FString AbsoluteGameContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
			for (const FDirectoryPath& DirToCook : PackagingSettings->DirectoriesToAlwaysCook)
			{
				UE_LOG(LogCook, Verbose, TEXT("Loading directory to always cook %s"), *DirToCook.Path);

				if (DirToCook.Path.StartsWith(TEXT("/"), ESearchCase::CaseSensitive))
				{
					// If this starts with /, this includes a root like /engine
					FString RelativePath = FPackageName::LongPackageNameToFilename(DirToCook.Path / TEXT(""));
					CookDirectories.Add(FPaths::ConvertRelativePathToFull(RelativePath));
				}
				else
				{
					// This is relative to /game
					CookDirectories.Add(AbsoluteGameContentDir / DirToCook.Path);
				}
			}
		}
	}

	if (!(FilesToCookFlags & ECookByTheBookOptions::NoGameAlwaysCookPackages))
	{
		COOK_STAT(FScopedDurationTimer TickTimer(DetailedCookStats::GameCookModificationDelegateTimeSec));
		SCOPE_TIMER(CookModificationDelegate);
#define DEBUG_COOKMODIFICATIONDELEGATE 0
#if DEBUG_COOKMODIFICATIONDELEGATE
		TSet<UPackage*> LoadedPackages;
		for ( TObjectIterator<UPackage> It; It; ++It)
		{
			LoadedPackages.Add(*It);
		}
#endif

		// allow the game to fill out the asset registry, as well as get a list of objects to always cook
		TArray<FString> FilesInPathStrings;
		FGameDelegates::Get().GetCookModificationDelegate().ExecuteIfBound(FilesInPathStrings);

		for (const FString& FileString : FilesInPathStrings)
		{
			FilesInPath.Add(FName(*FileString));
		}

		if (UAssetManager::IsValid())
		{
			TArray<FName> PackagesToNeverCook;

			UAssetManager::Get().ModifyCook(FilesInPath, PackagesToNeverCook);

			for (FName NeverCookPackage : PackagesToNeverCook)
			{
				const FName StandardPackageFilename = PackageNameCache->GetCachedStandardPackageFileFName(NeverCookPackage);

				if (StandardPackageFilename != NAME_None)
				{
					PackageTracker->NeverCookPackageList.Add(StandardPackageFilename);
				}
			}
		}
#if DEBUG_COOKMODIFICATIONDELEGATE
		for (TObjectIterator<UPackage> It; It; ++It)
		{
			if ( !LoadedPackages.Contains(*It) )
			{
				UE_LOG(LogCook, Display, TEXT("CookModificationDelegate loaded %s"), *It->GetName());
			}
		}
#endif

		if (UE_LOG_ACTIVE(LogCook, Verbose) )
		{
			for ( const FString& FileName : FilesInPathStrings )
			{
				UE_LOG(LogCook, Verbose, TEXT("Cook modification delegate requested package %s"), *FileName);
			}
		}
	}

	for ( const FString& CurrEntry : CookMaps )
	{
		SCOPE_TIMER(SearchForPackageOnDisk);
		if (FPackageName::IsShortPackageName(CurrEntry))
		{
			FString OutFilename;
			if (FPackageName::SearchForPackageOnDisk(CurrEntry, NULL, &OutFilename) == false)
			{
				LogCookerMessage( FString::Printf(TEXT("Unable to find package for map %s."), *CurrEntry), EMessageSeverity::Warning);
				UE_LOG(LogCook, Warning, TEXT("Unable to find package for map %s."), *CurrEntry);
			}
			else
			{
				AddFileToCook( FilesInPath, OutFilename);
			}
		}
		else
		{
			AddFileToCook( FilesInPath,CurrEntry);
		}
	}



	const FString ExternalMountPointName(TEXT("/Game/"));
	if (IsCookingDLC())
	{
		// get the dlc and make sure we cook that directory 
		FString DLCPath = FPaths::Combine(*GetBaseDirectoryForDLC(), TEXT("Content"));

		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *DLCPath, *(FString(TEXT("*")) + FPackageName::GetAssetPackageExtension()), true, false, false);
		IFileManager::Get().FindFilesRecursive(Files, *DLCPath, *(FString(TEXT("*")) + FPackageName::GetMapPackageExtension()), true, false, false);
		for (int32 Index = 0; Index < Files.Num(); Index++)
		{
			FString StdFile = Files[Index];
			FPaths::MakeStandardFilename(StdFile);
			AddFileToCook(FilesInPath, StdFile);

			// this asset may not be in our currently mounted content directories, so try to mount a new one now
			FString LongPackageName;
			if (!FPackageName::IsValidLongPackageName(StdFile) && !FPackageName::TryConvertFilenameToLongPackageName(StdFile, LongPackageName))
			{
				FPackageName::RegisterMountPoint(ExternalMountPointName, DLCPath);
			}
		}
	}


	if (!(FilesToCookFlags & ECookByTheBookOptions::DisableUnsolicitedPackages))
	{
		for (const FString& CurrEntry : CookDirectories)
		{
			TArray<FString> Files;
			IFileManager::Get().FindFilesRecursive(Files, *CurrEntry, *(FString(TEXT("*")) + FPackageName::GetAssetPackageExtension()), true, false);
			for (int32 Index = 0; Index < Files.Num(); Index++)
			{
				FString StdFile = Files[Index];
				FPaths::MakeStandardFilename(StdFile);
				AddFileToCook(FilesInPath, StdFile);

				// this asset may not be in our currently mounted content directories, so try to mount a new one now
				FString LongPackageName;
				if (!FPackageName::IsValidLongPackageName(StdFile) && !FPackageName::TryConvertFilenameToLongPackageName(StdFile, LongPackageName))
				{
					FPackageName::RegisterMountPoint(ExternalMountPointName, CurrEntry);
				}
			}
		}

		// If no packages were explicitly added by command line or game callback, add all maps
		if (FilesInPath.Num() == InitialPackages.Num() || bCookAll)
		{
			TArray<FString> Tokens;
			Tokens.Empty(2);
			Tokens.Add(FString("*") + FPackageName::GetAssetPackageExtension());
			Tokens.Add(FString("*") + FPackageName::GetMapPackageExtension());

			uint8 PackageFilter = NORMALIZE_DefaultFlags | NORMALIZE_ExcludeEnginePackages | NORMALIZE_ExcludeLocalizedPackages;
			if (bMapsOnly)
			{
				PackageFilter |= NORMALIZE_ExcludeContentPackages;
			}

			if (bNoDev)
			{
				PackageFilter |= NORMALIZE_ExcludeDeveloperPackages;
			}

			// assume the first token is the map wildcard/pathname
			TArray<FString> Unused;
			for (int32 TokenIndex = 0; TokenIndex < Tokens.Num(); TokenIndex++)
			{
				TArray<FString> TokenFiles;
				if (!NormalizePackageNames(Unused, TokenFiles, Tokens[TokenIndex], PackageFilter))
				{
					UE_LOG(LogCook, Display, TEXT("No packages found for parameter %i: '%s'"), TokenIndex, *Tokens[TokenIndex]);
					continue;
				}

				for (int32 TokenFileIndex = 0; TokenFileIndex < TokenFiles.Num(); ++TokenFileIndex)
				{
					AddFileToCook(FilesInPath, TokenFiles[TokenFileIndex]);
				}
			}
		}
	}

	if (!(FilesToCookFlags & ECookByTheBookOptions::NoDefaultMaps))
	{
		// make sure we cook the default maps
		// Collect the default maps for all requested platforms.  Our additions are potentially wasteful if different platforms in the requested list have different default maps.
		// In that case we will wastefully cook maps for platforms that don't require them.
		for (const ITargetPlatform* TargetPlatform : TargetPlatforms)
		{
			// load the platform specific ini to get its DefaultMap
			FConfigFile PlatformEngineIni;
			FConfigCacheIni::LoadLocalIniFile(PlatformEngineIni, TEXT("Engine"), true, *TargetPlatform->IniPlatformName());

			// get the server and game default maps and cook them
			FString Obj;
			if (PlatformEngineIni.GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GameDefaultMap"), Obj))
			{
				if (Obj != FName(NAME_None).ToString())
				{
					AddFileToCook(FilesInPath, Obj);
				}
			}
			if (IsCookFlagSet(ECookInitializationFlags::IncludeServerMaps))
			{
				if (PlatformEngineIni.GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("ServerDefaultMap"), Obj))
				{
					if (Obj != FName(NAME_None).ToString())
					{
						AddFileToCook(FilesInPath, Obj);
					}
				}
			}
			if (PlatformEngineIni.GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GlobalDefaultGameMode"), Obj))
			{
				if (Obj != FName(NAME_None).ToString())
				{
					AddFileToCook(FilesInPath, Obj);
				}
			}
			if (PlatformEngineIni.GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GlobalDefaultServerGameMode"), Obj))
			{
				if (Obj != FName(NAME_None).ToString())
				{
					AddFileToCook(FilesInPath, Obj);
				}
			}
			if (PlatformEngineIni.GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GameInstanceClass"), Obj))
			{
				if (Obj != FName(NAME_None).ToString())
				{
					AddFileToCook(FilesInPath, Obj);
				}
			}
		}
	}

	if (!(FilesToCookFlags & ECookByTheBookOptions::NoInputPackages))
	{
		// make sure we cook any extra assets for the default touch interface
		// @todo need a better approach to cooking assets which are dynamically loaded by engine code based on settings
		FConfigFile InputIni;
		FString InterfaceFile;
		FConfigCacheIni::LoadLocalIniFile(InputIni, TEXT("Input"), true);
		if (InputIni.GetString(TEXT("/Script/Engine.InputSettings"), TEXT("DefaultTouchInterface"), InterfaceFile))
		{
			if (InterfaceFile != TEXT("None") && InterfaceFile != TEXT(""))
			{
				AddFileToCook(FilesInPath, InterfaceFile);
			}
		}
	}
	//@todo SLATE: This is a hack to ensure all slate referenced assets get cooked.
	// Slate needs to be refactored to properly identify required assets at cook time.
	// Simply jamming everything in a given directory into the cook list is error-prone
	// on many levels - assets not required getting cooked/shipped; assets not put under 
	// the correct folder; etc.
	if ( !(FilesToCookFlags & ECookByTheBookOptions::NoSlatePackages))
	{
		TArray<FString> UIContentPaths;
		TSet <FName> ContentDirectoryAssets; 
		if (GConfig->GetArray(TEXT("UI"), TEXT("ContentDirectories"), UIContentPaths, GEditorIni) > 0)
		{
			for (int32 DirIdx = 0; DirIdx < UIContentPaths.Num(); DirIdx++)
			{
				FString ContentPath = FPackageName::LongPackageNameToFilename(UIContentPaths[DirIdx]);

				TArray<FString> Files;
				IFileManager::Get().FindFilesRecursive(Files, *ContentPath, *(FString(TEXT("*")) + FPackageName::GetAssetPackageExtension()), true, false);
				for (int32 Index = 0; Index < Files.Num(); Index++)
				{
					FString StdFile = Files[Index];
					FName PackageName = FName(*FPackageName::FilenameToLongPackageName(StdFile));
					ContentDirectoryAssets.Add(PackageName);
					FPaths::MakeStandardFilename(StdFile);
					AddFileToCook( FilesInPath, StdFile);
				}
			}
		}

		if (CookByTheBookOptions && CookByTheBookOptions->bGenerateDependenciesForMaps) 
		{
			for (auto& MapDependencyGraph : CookByTheBookOptions->MapDependencyGraphs)
			{
				MapDependencyGraph.Value.Add(FName(TEXT("ContentDirectoryAssets")), ContentDirectoryAssets);
			}
		}
	}

	if (CookByTheBookOptions && !(FilesToCookFlags & ECookByTheBookOptions::DisableUnsolicitedPackages))
	{
		// Gather initial unsolicited package list, this is needed in iterative mode because all explicitly requested packages may have already been cooked
		// and so the code inside the TIckCookOnTheSide build loop might never run and never get a chance to call GetUnsolicitedPackages
		UE_LOG(LogCook, Verbose, TEXT("Finding initial unsolicited packages"));

		TArray<UPackage*> UnsolicitedPackages = GetUnsolicitedPackages(PlatformManager->GetSessionPlatforms());

		for (UPackage* UnsolicitedPackage : UnsolicitedPackages)
		{
			AddFileToCook(FilesInPath, UnsolicitedPackage->GetName());
		}
	}
}

bool UCookOnTheFlyServer::IsCookByTheBookRunning() const
{
	return CookByTheBookOptions && CookByTheBookOptions->bRunning;
}


void UCookOnTheFlyServer::SaveGlobalShaderMapFiles(const TArrayView<const ITargetPlatform* const>& Platforms)
{
	// we don't support this behavior
	check( !IsCookingDLC() );
	for (int32 Index = 0; Index < Platforms.Num(); Index++)
	{
		// make sure global shaders are up to date!
		TArray<FString> Files;
		FShaderRecompileData RecompileData;
		RecompileData.PlatformName = Platforms[Index]->PlatformName();
		// Compile for all platforms
		RecompileData.ShaderPlatform = -1;
		RecompileData.ModifiedFiles = &Files;
		RecompileData.MeshMaterialMaps = NULL;

		check( IsInGameThread() );

		FString OutputDir = GetSandboxDirectory(RecompileData.PlatformName);

		RecompileShadersForRemote
			(RecompileData.PlatformName, 
			RecompileData.ShaderPlatform == -1 ? SP_NumPlatforms : (EShaderPlatform)RecompileData.ShaderPlatform, //-V547
			OutputDir, 
			RecompileData.MaterialsToLoad, 
			RecompileData.MeshMaterialMaps, 
			RecompileData.ModifiedFiles);
	}
}

FString UCookOnTheFlyServer::GetSandboxDirectory( const FString& PlatformName ) const
{
	FString Result;
	Result = SandboxFile->GetSandboxDirectory();

	Result.ReplaceInline(TEXT("[Platform]"), *PlatformName);

	/*if ( IsCookingDLC() )
	{
		check( IsCookByTheBookRunning() );
		Result.ReplaceInline(TEXT("/Cooked/"), *FString::Printf(TEXT("/CookedDLC_%s/"), *CookByTheBookOptions->DlcName) );
	}*/
	return Result;
}

FString UCookOnTheFlyServer::ConvertToFullSandboxPath( const FString &FileName, bool bForWrite ) const
{
	check( SandboxFile );

	FString Result;
	if (bForWrite)
	{
		// Ideally this would be in the Sandbox File but it can't access the project or plugin
		if (PluginsToRemap.Num() > 0)
		{
			// Handle remapping of plugins
			for (TSharedRef<IPlugin> Plugin : PluginsToRemap)
			{
				// If these match, then this content is part of plugin that gets remapped when packaged/staged
				if (FileName.StartsWith(Plugin->GetContentDir()))
				{
					FString SearchFor;
					SearchFor /= Plugin->GetName() / TEXT("Content");
					int32 FoundAt = FileName.Find(SearchFor, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
					check(FoundAt != -1);
					// Strip off everything but <PluginName/Content/<remaing path to file>
					FString SnippedOffPath = FileName.RightChop(FoundAt);
					// Put this is in <sandbox path>/RemappedPlugins/<PluginName>/Content/<remaing path to file>
					FString RemappedPath = SandboxFile->GetSandboxDirectory();
					RemappedPath /= REMAPPED_PLUGGINS;
					Result = RemappedPath / SnippedOffPath;
					return Result;
				}
			}
		}
		Result = SandboxFile->ConvertToAbsolutePathForExternalAppForWrite(*FileName);
	}
	else
	{
		Result = SandboxFile->ConvertToAbsolutePathForExternalAppForRead(*FileName);
	}

	/*if ( IsCookingDLC() )
	{
		check( IsCookByTheBookRunning() );
		Result.ReplaceInline(TEXT("/Cooked/"), *FString::Printf(TEXT("/CookedDLC_%s/"), *CookByTheBookOptions->DlcName) );
	}*/
	return Result;
}

FString UCookOnTheFlyServer::ConvertToFullSandboxPath( const FString &FileName, bool bForWrite, const FString& PlatformName ) const
{
	FString Result = ConvertToFullSandboxPath( FileName, bForWrite );
	Result.ReplaceInline(TEXT("[Platform]"), *PlatformName);
	return Result;
}

const FString UCookOnTheFlyServer::GetSandboxAssetRegistryFilename()
{
	static const FString RegistryFilename = FPaths::ProjectDir() / GetAssetRegistryFilename();

	if (IsCookingDLC())
	{
		check(IsCookByTheBookMode());
		const FString DLCRegistryFilename = FPaths::Combine(*GetBaseDirectoryForDLC(), GetAssetRegistryFilename());
		return ConvertToFullSandboxPath(*DLCRegistryFilename, true);
	}

	const FString SandboxRegistryFilename = ConvertToFullSandboxPath(*RegistryFilename, true);
	return SandboxRegistryFilename;
}

const FString UCookOnTheFlyServer::GetCookedAssetRegistryFilename(const FString& PlatformName )
{
	const FString CookedAssetRegistryFilename = GetSandboxAssetRegistryFilename().Replace(TEXT("[Platform]"), *PlatformName);
	return CookedAssetRegistryFilename;
}

void UCookOnTheFlyServer::InitShaderCodeLibrary(void)
{
    const UProjectPackagingSettings* const PackagingSettings = GetDefault<UProjectPackagingSettings>();
	bool const bCacheShaderLibraries = IsUsingShaderCodeLibrary();
    if (bCacheShaderLibraries && PackagingSettings->bShareMaterialShaderCode)
    {
        FShaderCodeLibrary::InitForCooking(PackagingSettings->bSharedMaterialNativeLibraries);
        
        for (const ITargetPlatform* TargetPlatform : PlatformManager->GetSessionPlatforms())
        {
			// Find out if this platform requires stable shader keys, by reading the platform setting file.
			bool bNeedShaderStableKeys = false;
			FConfigFile PlatformIniFile;
			FConfigCacheIni::LoadLocalIniFile(PlatformIniFile, TEXT("Engine"), true, *TargetPlatform->IniPlatformName());
			PlatformIniFile.GetBool(TEXT("DevOptions.Shaders"), TEXT("NeedsShaderStableKeys"), bNeedShaderStableKeys);
            
            TArray<FName> ShaderFormats;
            TargetPlatform->GetAllTargetedShaderFormats(ShaderFormats);
			TArray<TPair<FName, bool>> ShaderFormatsWithStableKeys;
			for (FName& Format : ShaderFormats)
			{
				ShaderFormatsWithStableKeys.Push(MakeTuple(Format, bNeedShaderStableKeys));
			}

            if (ShaderFormats.Num() > 0)
			{
				FShaderCodeLibrary::CookShaderFormats(ShaderFormatsWithStableKeys);
			}
        }
    }
}

static FString GenerateShaderCodeLibraryName(FString const& Name, bool bIsIterateSharedBuild)
{
	FString ActualName = (!bIsIterateSharedBuild) ? Name : Name + TEXT("_SC");
	return ActualName;
}

void UCookOnTheFlyServer::OpenShaderCodeLibrary(FString const& Name)
{
	const UProjectPackagingSettings* const PackagingSettings = GetDefault<UProjectPackagingSettings>();
	bool const bCacheShaderLibraries = IsUsingShaderCodeLibrary();
    if (bCacheShaderLibraries && PackagingSettings->bShareMaterialShaderCode)
	{
		FString ActualName = GenerateShaderCodeLibraryName(Name, IsCookFlagSet(ECookInitializationFlags::IterateSharedBuild));
		
		// The shader code library directory doesn't matter while cooking
		FShaderCodeLibrary::OpenLibrary(ActualName, TEXT(""));
	}
}

void UCookOnTheFlyServer::ProcessShaderCodeLibraries(const FString& LibraryName)
{
	for (const ITargetPlatform* TargetPlatform : PlatformManager->GetSessionPlatforms())
	{
		// make sure we have a registry generated for all the platforms 
		const FString TargetPlatformName = TargetPlatform->PlatformName();
		TArray<FString>* SCLCSVPaths = OutSCLCSVPaths.Find(FName(TargetPlatformName));
		if (SCLCSVPaths && SCLCSVPaths->Num())
		{
			TArray<FName> ShaderFormats;
			TargetPlatform->GetAllTargetedShaderFormats(ShaderFormats);
			for (FName ShaderFormat : ShaderFormats)
			{
				// *stablepc.csv or *stablepc.csv.compressed
				const FString Filename = FString::Printf(TEXT("*%s_%s.stablepc.csv"), *LibraryName, *ShaderFormat.ToString());
				const FString StablePCPath = FPaths::ProjectDir() / TEXT("Build") / TargetPlatform->IniPlatformName() / TEXT("PipelineCaches") / Filename;
				const FString StablePCPathCompressed = StablePCPath + TEXT(".compressed");

				TArray<FString> ExpandedFiles;
				IFileManager::Get().FindFilesRecursive(ExpandedFiles, *FPaths::GetPath(StablePCPath), *FPaths::GetCleanFilename(StablePCPath), true, false, false);
				IFileManager::Get().FindFilesRecursive(ExpandedFiles, *FPaths::GetPath(StablePCPathCompressed), *FPaths::GetCleanFilename(StablePCPathCompressed), true, false, false);
				if (!ExpandedFiles.Num())
				{
					UE_LOG(LogCook, Display, TEXT("---- NOT Running UShaderPipelineCacheToolsCommandlet for platform %s  shader format %s, no files found at %s"), *TargetPlatformName, *ShaderFormat.ToString(), *StablePCPath);
				}
				else
				{
					UE_LOG(LogCook, Display, TEXT("---- Running UShaderPipelineCacheToolsCommandlet for platform %s  shader format %s"), *TargetPlatformName, *ShaderFormat.ToString());

					const FString OutFilename = FString::Printf(TEXT("%s_%s.stable.upipelinecache"), *LibraryName, *ShaderFormat.ToString());
					const FString PCUncookedPath = FPaths::ProjectDir() / TEXT("Content") / TEXT("PipelineCaches") / TargetPlatform->IniPlatformName() / OutFilename;

					if (IFileManager::Get().FileExists(*PCUncookedPath))
					{
						UE_LOG(LogCook, Warning, TEXT("Deleting %s, cooked data doesn't belong here."), *PCUncookedPath);
						IFileManager::Get().Delete(*PCUncookedPath, false, true);
					}

					const FString PCCookedPath = ConvertToFullSandboxPath(*PCUncookedPath, true);
					const FString PCPath = PCCookedPath.Replace(TEXT("[Platform]"), *TargetPlatformName);


					FString Args(TEXT("build "));
					Args += TEXT("\"");
					Args += StablePCPath;
					Args += TEXT("\"");

					int32 NumMatched = 0;
					for (int32 Index = 0; Index < SCLCSVPaths->Num(); Index++)
					{
						if (!(*SCLCSVPaths)[Index].Contains(ShaderFormat.ToString()))
						{
							continue;
						}
						NumMatched++;
						Args += TEXT(" ");
						Args += TEXT("\"");
						Args += (*SCLCSVPaths)[Index];
						Args += TEXT("\"");
					}
					if (!NumMatched)
					{
						UE_LOG(LogCook, Warning, TEXT("Shader format %s for platform %s had this file %s, but no .scl.csv files."), *ShaderFormat.ToString(), *TargetPlatformName, *StablePCPath);
						for (int32 Index = 0; Index < SCLCSVPaths->Num(); Index++)
						{
							UE_LOG(LogCook, Warning, TEXT("    .scl.csv file: %s"), *((*SCLCSVPaths)[Index]));
						}							
						continue;
					}

					Args += TEXT(" ");
					Args += TEXT("\"");
					Args += PCPath;
					Args += TEXT("\"");
					UE_LOG(LogCook, Display, TEXT("  With Args: %s"), *Args);

					int32 Result = UShaderPipelineCacheToolsCommandlet::StaticMain(Args);

					if (Result)
					{
						LogCookerMessage(FString::Printf(TEXT("UShaderPipelineCacheToolsCommandlet failed %d"), Result), EMessageSeverity::Error);
					}
					else
					{
						UE_LOG(LogCook, Display, TEXT("---- Done running UShaderPipelineCacheToolsCommandlet for platform %s"), *TargetPlatformName);
					}
				}
			}
		}
	}
}


void UCookOnTheFlyServer::SaveShaderCodeLibrary(FString const& Name)
{
	const UProjectPackagingSettings* const PackagingSettings = GetDefault<UProjectPackagingSettings>();
    bool const bCacheShaderLibraries = IsUsingShaderCodeLibrary();
	if (bCacheShaderLibraries && PackagingSettings->bShareMaterialShaderCode)
	{
		FString ActualName = GenerateShaderCodeLibraryName(Name, IsCookFlagSet(ECookInitializationFlags::IterateSharedBuild));
		
		// Save shader code map - cleaning directories is deliberately a separate loop here as we open the cache once per shader platform and we don't assume that they can't be shared across target platforms.
		for (const ITargetPlatform* TargetPlatform : PlatformManager->GetSessionPlatforms())
		{
		   
			FString BasePath = !IsCookingDLC() ? FPaths::ProjectContentDir() : GetContentDirecctoryForDLC();
			
			FString ShaderCodeDir = ConvertToFullSandboxPath(*BasePath, true, TargetPlatform->PlatformName());

			const FString RootMetaDataPath = FPaths::ProjectDir() / TEXT("Metadata") / TEXT("PipelineCaches");
			const FString MetaDataPathSB = ConvertToFullSandboxPath(*RootMetaDataPath, true);
			const FString MetaDataPath = MetaDataPathSB.Replace(TEXT("[Platform]"), *TargetPlatform->PlatformName());

			TArray<FName> ShaderFormats;
			TargetPlatform->GetAllTargetedShaderFormats(ShaderFormats);
			if (ShaderFormats.Num() > 0)
			{
				FString TargetPlatformName = TargetPlatform->PlatformName();
				TArray<FString>& PlatformSCLCSVPaths = OutSCLCSVPaths.FindOrAdd(FName(TargetPlatformName));
				bool bSaved = FShaderCodeLibrary::SaveShaderCodeMaster(ShaderCodeDir, MetaDataPath, ShaderFormats, PlatformSCLCSVPaths);
				
				if(!bSaved)
				{
					LogCookerMessage(FString::Printf(TEXT("Shared Material Shader Code Library failed for %s."),*TargetPlatformName), EMessageSeverity::Error);
				}
				else
				{
					if (PackagingSettings->bSharedMaterialNativeLibraries)
					{
						bSaved = FShaderCodeLibrary::PackageNativeShaderLibrary(ShaderCodeDir, ShaderFormats);
						if (!bSaved)
						{
							// This is fatal - In this case we should cancel any launch on device operation or package write but we don't want to assert and crash the editor
							LogCookerMessage(FString::Printf(TEXT("Package Native Shader Library failed for %s."), *TargetPlatformName), EMessageSeverity::Error);
						}
					}
					for (const FString& Item : PlatformSCLCSVPaths)
					{
						UE_LOG(LogCook, Display, TEXT("Saved scl.csv %s for platform %s"), *Item, *TargetPlatformName);
					}

				}
			}
		}
		
		FShaderCodeLibrary::CloseLibrary(ActualName);
	}
}

void UCookOnTheFlyServer::CleanShaderCodeLibraries()
{
	const UProjectPackagingSettings* const PackagingSettings = GetDefault<UProjectPackagingSettings>();
    bool const bCacheShaderLibraries = IsUsingShaderCodeLibrary();
	ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();
	bool bIterativeCook = IsCookFlagSet(ECookInitializationFlags::Iterative) ||	PackageTracker->CookedPackages.Num() != 0;

	// If not iterative then clean up our temporary files
	if (bCacheShaderLibraries && PackagingSettings->bShareMaterialShaderCode && !bIterativeCook)
	{
		for (const ITargetPlatform* TargetPlatform : PlatformManager->GetSessionPlatforms())
		{
			TArray<FName> ShaderFormats;
			TargetPlatform->GetAllTargetedShaderFormats(ShaderFormats);
			if (ShaderFormats.Num() > 0)
			{
				FShaderCodeLibrary::CleanDirectories(ShaderFormats);
			}
		}
	}
}

void UCookOnTheFlyServer::CookByTheBookFinished()
{
	check( IsInGameThread() );
	check( IsCookByTheBookMode() );
	check( CookByTheBookOptions->bRunning == true );

	UE_LOG(LogCook, Display, TEXT("Finishing up..."));

	UPackage::WaitForAsyncFileWrites();
	
	FinalizePackageStore();

	GetDerivedDataCacheRef().WaitForQuiescence(true);
	
	UCookerSettings const* CookerSettings = GetDefault<UCookerSettings>();

	const UProjectPackagingSettings* const PackagingSettings = GetDefault<UProjectPackagingSettings>();
	bool const bCacheShaderLibraries = IsUsingShaderCodeLibrary();

	{
		if (IBlueprintNativeCodeGenModule::IsNativeCodeGenModuleLoaded())
		{
			SCOPE_TIMER(GeneratingBlueprintAssets)
			IBlueprintNativeCodeGenModule& CodeGenModule = IBlueprintNativeCodeGenModule::Get();

			CodeGenModule.GenerateFullyConvertedClasses(); // While generating fully converted classes the list of necessary stubs is created.
			CodeGenModule.GenerateStubs();

			CodeGenModule.FinalizeManifest();

			// Unload the module as we only need it while cooking. This will also clear the current module's state in order to allow a new cooker pass to function properly.
			FModuleManager::Get().UnloadModule(CodeGenModule.GetModuleName());
		}

		// Save modified asset registry with all streaming chunk info generated during cook
		const FString& SandboxRegistryFilename = GetSandboxAssetRegistryFilename();

	   	if (bCacheShaderLibraries && PackagingSettings->bShareMaterialShaderCode)
		{
			// Save shader code map
			FString LibraryName = !IsCookingDLC() ? FApp::GetProjectName() : CookByTheBookOptions->DlcName;
			SaveShaderCodeLibrary(LibraryName);
			
			// Don't clean Saved/Shaders/<LibraryPlatform(s)>/ at the end as we might iterate next time - Next cook at startup will decide if clean on iterate flag
            // /*CleanShaderCodeLibraries();*/
			ProcessShaderCodeLibraries(LibraryName);
            
			FShaderCodeLibrary::Shutdown();
		}				
		
		{
			SCOPE_TIMER(SavingCurrentIniSettings)
			for (const ITargetPlatform* TargetPlatform : PlatformManager->GetSessionPlatforms() )
			{
				SaveCurrentIniSettings(TargetPlatform);
			}
		}

		{
			SCOPE_TIMER(SavingAssetRegistry);
			for (const ITargetPlatform* TargetPlatform : PlatformManager->GetSessionPlatforms())
			{
				FPlatformData* PlatformData = PlatformManager->GetPlatformData(TargetPlatform);
				FAssetRegistryGenerator& Generator = *PlatformData->RegistryGenerator.Get();
				TArray<FName> CookedPackagesFilenames;
				TArray<FName> IgnorePackageFilenames;

				const FName& PlatformName = FName(*TargetPlatform->PlatformName());
				FString PlatformNameString = PlatformName.ToString();

				PackageTracker->CookedPackages.GetCookedFilesForPlatform(TargetPlatform, CookedPackagesFilenames, false, /* include successful */ true);

				// ignore any packages which failed to cook
				PackageTracker->CookedPackages.GetCookedFilesForPlatform(TargetPlatform, IgnorePackageFilenames, /* include failed */ true, false);

				bool bForceNoFilterAssetsFromAssetRegistry = false;

				if (IsCookingDLC())
				{
					bForceNoFilterAssetsFromAssetRegistry = true;
					// remove the previous release cooked packages from the new asset registry, add to ignore list
					SCOPE_TIMER(RemovingOldManifestEntries);
					
					const TArray<FName>* PreviousReleaseCookedPackages = CookByTheBookOptions->BasedOnReleaseCookedPackages.Find(PlatformName);
					if (PreviousReleaseCookedPackages)
					{
						for (FName PreviousReleaseCookedPackage : *PreviousReleaseCookedPackages)
						{
							CookedPackagesFilenames.Remove(PreviousReleaseCookedPackage);
							IgnorePackageFilenames.Add(PreviousReleaseCookedPackage);
						}
					}
				}

				// convert from filenames to package names
				TSet<FName> CookedPackageNames;
				for (FName PackageFilename : CookedPackagesFilenames)
				{
					const FName *FoundLongPackageFName = PackageNameCache->GetCachedPackageFilenameToPackageFName(PackageFilename);
					CookedPackageNames.Add(*FoundLongPackageFName);
				}

				TSet<FName> IgnorePackageNames;
				for (FName PackageFilename : IgnorePackageFilenames)
				{
					const FName *FoundLongPackageFName = PackageNameCache->GetCachedPackageFilenameToPackageFName(PackageFilename);
					IgnorePackageNames.Add(*FoundLongPackageFName);
				}

				// ignore packages that weren't cooked because they were only referenced by editor-only properties
				TSet<FName> UncookedEditorOnlyPackageNames;
				PackageTracker->UncookedEditorOnlyPackages.GetValues(UncookedEditorOnlyPackageNames);
				for (FName UncookedEditorOnlyPackage : UncookedEditorOnlyPackageNames)
				{
					IgnorePackageNames.Add(UncookedEditorOnlyPackage);
				}
				{
					Generator.PreSave(CookedPackageNames);
				}
				{
					SCOPE_TIMER(BuildChunkManifest);
					Generator.BuildChunkManifest(CookedPackageNames, IgnorePackageNames, SandboxFile.Get(), CookByTheBookOptions->bGenerateStreamingInstallManifests);
				}
				{
					SCOPE_TIMER(SaveManifests);
					// Always try to save the manifests, this is required to make the asset registry work, but doesn't necessarily write a file
					Generator.SaveManifests(SandboxFile.Get());

					int64 ExtraFlavorChunkSize;
					if (FParse::Value(FCommandLine::Get(), TEXT("ExtraFlavorChunkSize="), ExtraFlavorChunkSize))
					{
						if (ExtraFlavorChunkSize > 0)
						{
							Generator.SaveManifests(SandboxFile.Get(), ExtraFlavorChunkSize);
						}
					}
				}
				{
					SCOPE_TIMER(SaveRealAssetRegistry);
					Generator.SaveAssetRegistry(SandboxRegistryFilename, true, bForceNoFilterAssetsFromAssetRegistry);
				}
				{
					Generator.PostSave();
				}
				{
					SCOPE_TIMER(WriteCookerOpenOrder);
					if (!IsCookFlagSet(ECookInitializationFlags::Iterative))
					{
						Generator.WriteCookerOpenOrder();
					}
				}
				{
					if (FParse::Param(FCommandLine::Get(), TEXT("fastcook")))
					{
						FFileHelper::SaveStringToFile(FString(), *(GetSandboxDirectory(PlatformNameString) / TEXT("fastcook.txt")));
					}
				}
				if (IsCreatingReleaseVersion())
				{
					const FString VersionedRegistryPath = GetReleaseVersionAssetRegistryPath(CookByTheBookOptions->CreateReleaseVersion, PlatformNameString);
					IFileManager::Get().MakeDirectory(*VersionedRegistryPath, true);
					const FString VersionedRegistryFilename = VersionedRegistryPath / GetAssetRegistryFilename();
					const FString CookedAssetRegistryFilename = SandboxRegistryFilename.Replace(TEXT("[Platform]"), *PlatformNameString);
					IFileManager::Get().Copy(*VersionedRegistryFilename, *CookedAssetRegistryFilename, true, true);

					// Also copy development registry if it exists
					const FString DevVersionedRegistryFilename = VersionedRegistryFilename.Replace(TEXT("AssetRegistry.bin"), TEXT("Metadata/DevelopmentAssetRegistry.bin"));
					const FString DevCookedAssetRegistryFilename = CookedAssetRegistryFilename.Replace(TEXT("AssetRegistry.bin"), TEXT("Metadata/DevelopmentAssetRegistry.bin"));
					IFileManager::Get().Copy(*DevVersionedRegistryFilename, *DevCookedAssetRegistryFilename, true, true);
				}
			}
		}
	}

	if (CookByTheBookOptions->bGenerateDependenciesForMaps)
	{
		SCOPE_TIMER(GenerateMapDependencies);
		for (auto& MapDependencyGraphIt : CookByTheBookOptions->MapDependencyGraphs)
		{
			BuildMapDependencyGraph(MapDependencyGraphIt.Key);
			WriteMapDependencyGraph(MapDependencyGraphIt.Key);
		}
	}

	const float TotalCookTime = (float)(FPlatformTime::Seconds() - CookByTheBookOptions->CookStartTime);
	UE_LOG(LogCook, Display, TEXT("Cook by the book total time in tick %fs total time %f"), CookByTheBookOptions->CookTime, TotalCookTime);

	CookByTheBookOptions->BasedOnReleaseCookedPackages.Empty();

	CookByTheBookOptions->bRunning = false;
	CookByTheBookOptions->bFullLoadAndSave = false;

	const FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();

	PlatformManager->ClearSessionPlatforms();

	UE_LOG(LogCook, Display, TEXT("Peak Used virtual %uMB Peak Used physical %uMB"), MemStats.PeakUsedVirtual / 1024 / 1024, MemStats.PeakUsedPhysical / 1024 / 1024 );

	OutputHierarchyTimers();
	ClearHierarchyTimers();

	UE_LOG(LogCook, Display, TEXT("Done!"));
}

void UCookOnTheFlyServer::BuildMapDependencyGraph(const ITargetPlatform* TargetPlatform)
{
	auto& MapDependencyGraph = CookByTheBookOptions->MapDependencyGraphs.FindChecked(TargetPlatform);

	TArray<FName> PlatformCookedPackages;
	PackageTracker->CookedPackages.GetCookedFilesForPlatform(TargetPlatform, PlatformCookedPackages, /* include failed */ true, /* include successful */ true);

	// assign chunks for all the map packages
	for (const FName& CookedPackage : PlatformCookedPackages)
	{
		TArray<FAssetData> PackageAssets;
		FName Name = FName(*FPackageName::FilenameToLongPackageName(CookedPackage.ToString()));

		if (!ContainsMap(Name))
		{
			continue;
		}

		TSet<FName> DependentPackages;
		TSet<FName> Roots; 

		Roots.Add(Name);

		GetDependentPackages(Roots, DependentPackages);

		MapDependencyGraph.Add(Name, DependentPackages);
	}
}

void UCookOnTheFlyServer::WriteMapDependencyGraph(const ITargetPlatform* TargetPlatform)
{
	auto& MapDependencyGraph = CookByTheBookOptions->MapDependencyGraphs.FindChecked(TargetPlatform);

	FString MapDependencyGraphFile = FPaths::ProjectDir() / TEXT("MapDependencyGraph.json");
	// dump dependency graph. 
	FString DependencyString;
	DependencyString += "{";
	for (auto& Ele : MapDependencyGraph)
	{
		TSet<FName>& Deps = Ele.Value;
		FName MapName = Ele.Key;
		DependencyString += TEXT("\t\"") + MapName.ToString() + TEXT("\" : \n\t[\n ");
		for (FName& Val : Deps)
		{
			DependencyString += TEXT("\t\t\"") + Val.ToString() + TEXT("\",\n");
		}
		DependencyString.RemoveFromEnd(TEXT(",\n"));
		DependencyString += TEXT("\n\t],\n");
	}
	DependencyString.RemoveFromEnd(TEXT(",\n"));
	DependencyString += "\n}";

	FString CookedMapDependencyGraphFilePlatform = ConvertToFullSandboxPath(MapDependencyGraphFile, true).Replace(TEXT("[Platform]"), *TargetPlatform->PlatformName());
	FFileHelper::SaveStringToFile(DependencyString, *CookedMapDependencyGraphFilePlatform, FFileHelper::EEncodingOptions::ForceUnicode);
}

void UCookOnTheFlyServer::QueueCancelCookByTheBook()
{
	if ( IsCookByTheBookMode() )
	{
		check( CookByTheBookOptions != NULL );
		CookByTheBookOptions->bCancel = true;
	}
}

void UCookOnTheFlyServer::CancelCookByTheBook()
{
	if ( IsCookByTheBookMode() && CookByTheBookOptions->bRunning )
	{
		check(CookByTheBookOptions);
		check( IsInGameThread() );

		// save the cook requests and immediately execute any pending TickCommands
		TArray<FPackageTracker::FTickCommand> TickCommands;
		PackageTracker->DequeueAllRequests(TickCommands, CookByTheBookOptions->PreviousCookRequests);
		for (FPackageTracker::FTickCommand& TickCommand : TickCommands)
		{
			TickCommand();
		}

		CookByTheBookOptions->bRunning = false;

		SandboxFile = nullptr;
	}	
}

void UCookOnTheFlyServer::StopAndClearCookedData()
{
	if ( IsCookByTheBookMode() )
	{
		check( CookByTheBookOptions != NULL );
		check( CookByTheBookOptions->bRunning == false );
		CancelCookByTheBook();
		CookByTheBookOptions->PreviousCookRequests.Empty();
	}

	PackageTracker->RecompileRequests.Empty();
	PackageTracker->EmptyRequests();
	PackageTracker->UnsolicitedCookedPackages.Empty();
	PackageTracker->CookedPackages.Empty(); // set of files which have been cooked when needing to recook a file the entry will need to be removed from here
}

void UCookOnTheFlyServer::ClearAllCookedData()
{
	// if we are going to clear the cooked packages it is conceivable that we will recook the packages which we just cooked 
	// that means it's also conceivable that we will recook the same package which currently has an outstanding async write request
	UPackage::WaitForAsyncFileWrites();

	PackageTracker->UnsolicitedCookedPackages.Empty();
	PackageTracker->CookedPackages.Empty(); // set of files which have been cooked when needing to recook a file the entry will need to be removed from here
}

void UCookOnTheFlyServer::ClearPlatformCookedData(const ITargetPlatform* TargetPlatform)
{
	if (!TargetPlatform)
	{
		return;
	}

	// if we are going to clear the cooked packages it is conceivable that we will recook the packages which we just cooked 
	// that means it's also conceivable that we will recook the same package which currently has an outstanding async write request
	UPackage::WaitForAsyncFileWrites();

	PackageTracker->CookedPackages.RemoveAllFilesForPlatform(TargetPlatform);

	TArray<FName> PackageNames;
	PackageTracker->UnsolicitedCookedPackages.GetPackagesForPlatformAndRemove(TargetPlatform, PackageNames);

	DeleteSandboxDirectory(TargetPlatform->PlatformName());
}

void UCookOnTheFlyServer::ClearPlatformCookedData(const FString& PlatformName)
{
	ClearPlatformCookedData(GetTargetPlatformManagerRef().FindTargetPlatform(PlatformName));
}

void UCookOnTheFlyServer::ClearCachedCookedPlatformDataForPlatform( const ITargetPlatform* TargetPlatform )
{
	if (TargetPlatform)
	{
		for ( TObjectIterator<UObject> It; It; ++It )
		{
			It->ClearCachedCookedPlatformData(TargetPlatform);
		}
	}
}

void UCookOnTheFlyServer::ClearCachedCookedPlatformDataForPlatform(const FName& PlatformName)
{
	ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();
	const ITargetPlatform* TargetPlatform = TPM.FindTargetPlatform(PlatformName.ToString());
	return ClearCachedCookedPlatformDataForPlatform(TargetPlatform);
}

void UCookOnTheFlyServer::OnTargetPlatformChangedSupportedFormats(const ITargetPlatform* TargetPlatform)
{
	for (TObjectIterator<UObject> It; It; ++It)
	{
		It->ClearCachedCookedPlatformData(TargetPlatform);
	}
}

void UCookOnTheFlyServer::CreateSandboxFile()
{
	// initialize the sandbox file after determining if we are cooking dlc
	// Local sandbox file wrapper. This will be used to handle path conversions,
	// but will not be used to actually write/read files so we can safely
	// use [Platform] token in the sandbox directory name and then replace it
	// with the actual platform name.
	check( SandboxFile == nullptr );
	SandboxFile = MakeUnique<FSandboxPlatformFile>(false);

	// Output directory override.	
	FString OutputDirectory = GetOutputDirectoryOverride();

	// Use SandboxFile to do path conversion to properly handle sandbox paths (outside of standard paths in particular).
	SandboxFile->Initialize(&FPlatformFileManager::Get().GetPlatformFile(), *FString::Printf(TEXT("-sandbox=\"%s\""), *OutputDirectory));
}

void UCookOnTheFlyServer::InitializeSandbox(const TArrayView<const ITargetPlatform* const>& TargetPlatforms)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UCookOnTheFlyServer::CleanSandbox);
#if OUTPUT_TIMING
	double CleanSandboxTime = 0.0;
#endif
	{
#if OUTPUT_TIMING
		SCOPE_SECONDS_COUNTER(CleanSandboxTime);
		SCOPE_TIMER(CleanSandbox);
#endif

		if (SandboxFile == nullptr)
		{
			CreateSandboxFile();
		}

		// before we can delete any cooked files we need to make sure that we have finished writing them
		UPackage::WaitForAsyncFileWrites();

		// Skip markup of packages for reload during sandbox initialization.  TODO: Is this no longer required because it is no longer possible to load packages in this function?
		bIsInitializingSandbox = true;
		ON_SCOPE_EXIT
		{
			bIsInitializingSandbox = false;
		};

		TArray<const ITargetPlatform*, TInlineAllocator<ExpectedMaxNumPlatforms>> RefreshPlatforms;
		const bool bIsDiffOnly = FParse::Param(FCommandLine::Get(), TEXT("DIFFONLY"));
		const bool bIsIterativeCook = IsCookFlagSet(ECookInitializationFlags::Iterative);

		for (const ITargetPlatform* Target : TargetPlatforms)
		{
			FPlatformData* PlatformData = PlatformManager->GetPlatformData(Target);
			const bool bIsIniSettingsOutOfDate = IniSettingsOutOfDate(Target); // needs to be executed for side effects even if non-iterative

			bool bShouldClearCookedContent = true;
			if (bIsDiffOnly)
			{
				// When looking for deterministic cooking differences in cooked packages, don't delete the packages on disk
				bShouldClearCookedContent = false;
			}
			else if (bIsIterativeCook || PlatformData->bIsSandboxInitialized)
			{
				if (!bIsIniSettingsOutOfDate)
				{
					// We have constructed the sandbox in an earlier cook in this process (e.g. in the editor) and should not clear it again
					bShouldClearCookedContent = false;
				}
				else
				{
					if (!IsCookFlagSet(ECookInitializationFlags::IgnoreIniSettingsOutOfDate))
					{
						UE_LOG(LogCook, Display, TEXT("Cook invalidated for platform %s ini settings don't match from last cook, clearing all cooked content"), *Target->PlatformName());
						bShouldClearCookedContent = true;
					}
					else
					{
						UE_LOG(LogCook, Display, TEXT("Inisettings were out of date for platform %s but we are going with it anyway because IgnoreIniSettingsOutOfDate is set"), *Target->PlatformName());
						bShouldClearCookedContent = false;
					}
				}
			}
			else
			{
				// In non-iterative cooks we will be replacing every cooked package and so should wipe the cooked directory
				UE_LOG(LogCook, Display, TEXT("Clearing all cooked content for platform %s"), *Target->PlatformName());
				bShouldClearCookedContent = true;
			}

			if (bShouldClearCookedContent)
			{
				ClearPlatformCookedData(Target);
				SaveCurrentIniSettings(Target);
			}
			else
			{
				RefreshPlatforms.Add(Target);
			}

			PlatformData->bIsSandboxInitialized = true;
		}

		if (RefreshPlatforms.Num() != 0)
		{
			for (const ITargetPlatform* Target : RefreshPlatforms)
			{
				PackageTracker->CookedPackages.RemoveAllFilesForPlatform(Target);
			}
			// The asset registry makes populating cooked packages from disk fast, and populating is a performance benefit
			// Don't populate however if we are looking for deterministic cooking differences; start from an empty list of cooked packages
			if (!bIsDiffOnly) 
			{
				PopulateCookedPackagesFromDisk(RefreshPlatforms);
			}
		}
	}

#if OUTPUT_TIMING
	FString PlatformNames;
	for (const ITargetPlatform* Target : TargetPlatforms)
	{
		PlatformNames += Target->PlatformName() + TEXT(" ");
	}
	UE_LOG(LogCook, Display, TEXT("Sandbox cleanup took %5.3f seconds for platforms %s"), CleanSandboxTime, *PlatformNames);
#endif
}

void UCookOnTheFlyServer::InitializePackageStore(const TArrayView<const ITargetPlatform* const>& TargetPlatforms)
{
	const FString RootPath = FPaths::RootDir();
	const FString RootPathSandbox = ConvertToFullSandboxPath(*RootPath, true);

	const FString ProjectPath = FPaths::ProjectDir();
	const FString ProjectPathSandbox = ConvertToFullSandboxPath(*ProjectPath, true);

	SavePackageContexts.Reserve(TargetPlatforms.Num());

	for (const ITargetPlatform* TargetPlatform: TargetPlatforms)
	{
		const FString PlatformString = TargetPlatform->PlatformName();

		const FString ResolvedRootPath = RootPathSandbox.Replace(TEXT("[Platform]"), *PlatformString);
		const FString ResolvedProjectPath = ProjectPathSandbox.Replace(TEXT("[Platform]"), *PlatformString);

		// just leak all memory for now
		FPackageStoreBulkDataManifest* BulkDataManifest	= new FPackageStoreBulkDataManifest(ResolvedProjectPath);
		FLooseFileWriter* LooseFileWriter				= IsUsingPackageStore() ? new FLooseFileWriter() : nullptr;

		FSavePackageContext* SavePackageContext			= new FSavePackageContext(LooseFileWriter, BulkDataManifest);
		SavePackageContexts.Add(SavePackageContext);
	}
}

void UCookOnTheFlyServer::FinalizePackageStore()
{
	SCOPE_TIMER(FinalizePackageStore);

	UE_LOG(LogCook, Display, TEXT("Saving BulkData manifest(s)..."));
	for (FSavePackageContext* PackageContext : SavePackageContexts)
	{
		if (PackageContext != nullptr && PackageContext->BulkDataManifest != nullptr)
		{
			PackageContext->BulkDataManifest->Save();
		}
	}
	UE_LOG(LogCook, Display, TEXT("Done saving BulkData manifest(s)"));

	ClearPackageStoreContexts();
}

void UCookOnTheFlyServer::ClearPackageStoreContexts()
{
	for (FSavePackageContext* Context : SavePackageContexts)
	{
		delete Context;
	}

	SavePackageContexts.Empty();
}

void UCookOnTheFlyServer::InitializeTargetPlatforms(const TArrayView<ITargetPlatform* const>& NewTargetPlatforms)
{
	//allow each platform to update its internals before cooking
	for (ITargetPlatform* TargetPlatform : NewTargetPlatforms)
	{
		TargetPlatform->RefreshSettings();
	}
}

void UCookOnTheFlyServer::DiscoverPlatformSpecificNeverCookPackages(
	const TArrayView<const ITargetPlatform* const>& TargetPlatforms, const TArray<FString>& UBTPlatformStrings)
{
	TArray<const ITargetPlatform*> PluginUnsupportedTargetPlatforms;
	TArray<FAssetData> PluginAssets;
	FARFilter PluginARFilter;
	FString PluginPackagePath;

	TArray<TSharedRef<IPlugin>> AllContentPlugins = IPluginManager::Get().GetEnabledPluginsWithContent();
	for (TSharedRef<IPlugin> Plugin : AllContentPlugins)
	{
		const FPluginDescriptor& Descriptor = Plugin->GetDescriptor();

		// we are only interested in plugins that does not support all platforms
		if (Descriptor.SupportedTargetPlatforms.Num() == 0)
		{
			continue;
		}

		// find any unsupported target platforms for this plugin
		PluginUnsupportedTargetPlatforms.Reset();
		for (int32 I = 0, Count = TargetPlatforms.Num(); I < Count; ++I)
		{
			if (!Descriptor.SupportedTargetPlatforms.Contains(UBTPlatformStrings[I]))
			{
				PluginUnsupportedTargetPlatforms.Add(TargetPlatforms[I]);
			}
		}

		// if there are unsupported target platforms,
		// then add all packages for this plugin for these platforms to the PlatformSpecificNeverCookPackages map
		if (PluginUnsupportedTargetPlatforms.Num() > 0)
		{
			PluginPackagePath.Reset(127);
			PluginPackagePath.AppendChar(TEXT('/'));
			PluginPackagePath.Append(Plugin->GetName());

			PluginARFilter.bRecursivePaths = true;
			PluginARFilter.bIncludeOnlyOnDiskAssets = true;
			PluginARFilter.PackagePaths.Reset(1);
			PluginARFilter.PackagePaths.Emplace(*PluginPackagePath);

			PluginAssets.Reset();
			AssetRegistry->GetAssets(PluginARFilter, PluginAssets);

			for (const ITargetPlatform* TargetPlatform: PluginUnsupportedTargetPlatforms)
			{
				TSet<FName>& NeverCookPackages = PackageTracker->PlatformSpecificNeverCookPackages.FindOrAdd(TargetPlatform);
				for (const FAssetData& Asset : PluginAssets)
				{
					NeverCookPackages.Add(Asset.PackageName);
				}
			}
		}
	}
}

void UCookOnTheFlyServer::TermSandbox()
{
	ClearAllCookedData();
	PackageNameCache->ClearPackageFilenameCache(nullptr);
	SandboxFile = nullptr;
}

void UCookOnTheFlyServer::StartCookByTheBook( const FCookByTheBookStartupOptions& CookByTheBookStartupOptions )
{
	SCOPE_TIMER(StartCookByTheBookTime);

	const TArray<FString>& CookMaps = CookByTheBookStartupOptions.CookMaps;
	const TArray<FString>& CookDirectories = CookByTheBookStartupOptions.CookDirectories;
	const TArray<FString>& IniMapSections = CookByTheBookStartupOptions.IniMapSections;
	const ECookByTheBookOptions& CookOptions = CookByTheBookStartupOptions.CookOptions;
	const FString& DLCName = CookByTheBookStartupOptions.DLCName;

	const FString& CreateReleaseVersion = CookByTheBookStartupOptions.CreateReleaseVersion;
	const FString& BasedOnReleaseVersion = CookByTheBookStartupOptions.BasedOnReleaseVersion;

	check( IsInGameThread() );
	check( IsCookByTheBookMode() );

	//force precache objects to refresh themselves before cooking anything
	LastUpdateTick = INT_MAX;

	CookByTheBookOptions->bCancel = false;
	CookByTheBookOptions->CookTime = 0.0f;
	CookByTheBookOptions->CookStartTime = FPlatformTime::Seconds();
	CookByTheBookOptions->bGenerateStreamingInstallManifests = CookByTheBookStartupOptions.bGenerateStreamingInstallManifests;
	CookByTheBookOptions->bGenerateDependenciesForMaps = CookByTheBookStartupOptions.bGenerateDependenciesForMaps;
	CookByTheBookOptions->CreateReleaseVersion = CreateReleaseVersion;
	CookByTheBookOptions->bDisableUnsolicitedPackages = !!(CookOptions & ECookByTheBookOptions::DisableUnsolicitedPackages);
	CookByTheBookOptions->bFullLoadAndSave = !!(CookOptions & ECookByTheBookOptions::FullLoadAndSave);
	CookByTheBookOptions->bPackageStore = !!(CookOptions & ECookByTheBookOptions::PackageStore);
	CookByTheBookOptions->bErrorOnEngineContentUse = CookByTheBookStartupOptions.bErrorOnEngineContentUse;

	GenerateAssetRegistry();

	// SelectSessionPlatforms does not check for uniqueness and non-null, and we rely on those properties for performance, so ensure it here before calling SelectSessionPlatforms
	TArray<ITargetPlatform*> TargetPlatforms;
	TargetPlatforms.Reserve(CookByTheBookStartupOptions.TargetPlatforms.Num());
	for (ITargetPlatform* TargetPlatform : CookByTheBookStartupOptions.TargetPlatforms)
	{
		if (TargetPlatform)
		{
			TargetPlatforms.AddUnique(TargetPlatform);
		}
	}
	PlatformManager->SelectSessionPlatforms(TargetPlatforms, PackageTracker);
	check(PlatformManager->GetSessionPlatforms().Num() == TargetPlatforms.Num());

	// We want to set bRunning = true as early as possible, but it implies that session platforms have been selected so this is the earliest point we can set it
	CookByTheBookOptions->bRunning = true;

	RefreshPlatformAssetRegistries(TargetPlatforms);

	if (CurrentCookMode == ECookMode::CookByTheBook)
	{
		check(!PackageNameCache);
		check(!PackageTracker);
		ConstructPackageTracker();
		FCoreUObjectDelegates::PackageCreatedForLoad.AddUObject(this, &UCookOnTheFlyServer::MaybeMarkPackageAsAlreadyLoaded);
	}

	const UProjectPackagingSettings* const PackagingSettings = GetDefault<UProjectPackagingSettings>();

	// Find all the localized packages and map them back to their source package
	{
		TArray<FString> AllCulturesToCook = CookByTheBookStartupOptions.CookCultures;
		for (const FString& CultureName : CookByTheBookStartupOptions.CookCultures)
		{
			const TArray<FString> PrioritizedCultureNames = FInternationalization::Get().GetPrioritizedCultureNames(CultureName);
			for (const FString& PrioritizedCultureName : PrioritizedCultureNames)
			{
				AllCulturesToCook.AddUnique(PrioritizedCultureName);
			}
		}
		AllCulturesToCook.Sort();

		UE_LOG(LogCook, Display, TEXT("Discovering localized assets for cultures: %s"), *FString::Join(AllCulturesToCook, TEXT(", ")));

		TArray<FString> RootPaths;
		FPackageName::QueryRootContentPaths(RootPaths);

		FARFilter Filter;
		Filter.bRecursivePaths = true;
		Filter.bIncludeOnlyOnDiskAssets = false;
		Filter.PackagePaths.Reserve(AllCulturesToCook.Num() * RootPaths.Num());
		for (const FString& RootPath : RootPaths)
		{
			for (const FString& CultureName : AllCulturesToCook)
			{
				FString LocalizedPackagePath = RootPath / TEXT("L10N") / CultureName;
				Filter.PackagePaths.Add(*LocalizedPackagePath);
			}
		}

		TArray<FAssetData> AssetDataForCultures;
		AssetRegistry->GetAssets(Filter, AssetDataForCultures);

		for (const FAssetData& AssetData : AssetDataForCultures)
		{
			const FName LocalizedPackageName = AssetData.PackageName;
			const FName SourcePackageName = *FPackageName::GetSourcePackagePath(LocalizedPackageName.ToString());

			TArray<FName>& LocalizedPackageNames = CookByTheBookOptions->SourceToLocalizedPackageVariants.FindOrAdd(SourcePackageName);
			LocalizedPackageNames.AddUnique(LocalizedPackageName);
		}

		// Get the list of localization targets to chunk, and remove any targets that we've been asked not to stage
		TArray<FString> LocalizationTargetsToChunk = PackagingSettings->LocalizationTargetsToChunk;
		{
			TArray<FString> BlacklistLocalizationTargets;
			GConfig->GetArray(TEXT("Staging"), TEXT("BlacklistLocalizationTargets"), BlacklistLocalizationTargets, GGameIni);
			if (BlacklistLocalizationTargets.Num() > 0)
			{
				LocalizationTargetsToChunk.RemoveAll([&BlacklistLocalizationTargets](const FString& InLocalizationTarget)
				{
					return BlacklistLocalizationTargets.Contains(InLocalizationTarget);
				});
			}
		}

		if (LocalizationTargetsToChunk.Num() > 0 && AllCulturesToCook.Num() > 0)
		{
			for (const ITargetPlatform* TargetPlatform : TargetPlatforms)
			{
				FAssetRegistryGenerator* RegistryGenerator = PlatformManager->GetPlatformData(TargetPlatform)->RegistryGenerator.Get();
				RegistryGenerator->RegisterChunkDataGenerator(MakeShared<FLocalizationChunkDataGenerator>(PackagingSettings->LocalizationTargetCatchAllChunkId, LocalizationTargetsToChunk, AllCulturesToCook));
			}
		}
	}

	PackageTracker->NeverCookPackageList.Empty();
	{
		const FString AbsoluteGameContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());

		TArray<FString> NeverCookDirectories = CookByTheBookStartupOptions.NeverCookDirectories;

		for (const FDirectoryPath& DirToNotCook : PackagingSettings->DirectoriesToNeverCook)
		{
			if (DirToNotCook.Path.StartsWith(TEXT("/"), ESearchCase::CaseSensitive))
			{
				// If this starts with /, this includes a root like /engine
				FString RelativePath = FPackageName::LongPackageNameToFilename(DirToNotCook.Path / TEXT(""));
				NeverCookDirectories.Add(FPaths::ConvertRelativePathToFull(RelativePath));
			}
			else
			{
				// This is relative to /game
				NeverCookDirectories.Add(AbsoluteGameContentDir / DirToNotCook.Path);
			}

		}

		for (const FString& NeverCookDirectory : NeverCookDirectories)
		{
			// add the packages to the never cook package list
			struct FNeverCookDirectoryWalker : public IPlatformFile::FDirectoryVisitor
			{
			private:
				FThreadSafeSet<FName>& NeverCookPackageList;
			public:
				FNeverCookDirectoryWalker(FThreadSafeSet<FName> &InNeverCookPackageList) : NeverCookPackageList(InNeverCookPackageList) { }

				// IPlatformFile::FDirectoryVisitor interface
				virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory)
				{
					if (bIsDirectory)
					{
						return true;
					}
					FString StandardFilename = FString(FilenameOrDirectory);
					FPaths::MakeStandardFilename(StandardFilename);

					NeverCookPackageList.Add(FName(*StandardFilename));
					return true;
				}

			} NeverCookDirectoryWalker(PackageTracker->NeverCookPackageList);

			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

			PlatformFile.IterateDirectoryRecursively(*NeverCookDirectory, NeverCookDirectoryWalker);
		}

	}

	// use temp list of UBT platform strings to discover PlatformSpecificNeverCookPackages
	{
		TArray<FString> UBTPlatformStrings;
		UBTPlatformStrings.Reserve(TargetPlatforms.Num());
		for (const ITargetPlatform* Platform : TargetPlatforms)
		{
			FString UBTPlatformName;
			Platform->GetPlatformInfo().UBTTargetId.ToString(UBTPlatformName);
			UBTPlatformStrings.Emplace(MoveTemp(UBTPlatformName));
		}

		DiscoverPlatformSpecificNeverCookPackages(TargetPlatforms, UBTPlatformStrings);
	}

	if ( CookByTheBookOptions->DlcName != DLCName )
	{
		// we are going to change the state of dlc we need to clean out our package filename cache (the generated filename cache is dependent on this key)
		CookByTheBookOptions->DlcName = DLCName;

		TermSandbox();
	}

	// This will either delete the sandbox or iteratively clean it
	InitializeSandbox(TargetPlatforms);
	InitializeTargetPlatforms(TargetPlatforms);

	InitializePackageStore(TargetPlatforms);

	if (CurrentCookMode == ECookMode::CookByTheBook && !IsCookFlagSet(ECookInitializationFlags::Iterative))
	{
		StartSavingEDLCookInfoForVerification();
	}

	// Note: Nativization only works with "cook by the book" mode and not from within the current editor process.
	if (CurrentCookMode == ECookMode::CookByTheBook
		&& PackagingSettings->BlueprintNativizationMethod != EProjectPackagingBlueprintNativizationMethod::Disabled)
	{
		FNativeCodeGenInitData CodeGenData;
		for (const ITargetPlatform* Entry : CookByTheBookStartupOptions.TargetPlatforms)
		{
			FPlatformNativizationDetails PlatformNativizationDetails;
			IBlueprintNativeCodeGenModule::Get().FillPlatformNativizationDetails(Entry, PlatformNativizationDetails);
			CodeGenData.CodegenTargets.Push(PlatformNativizationDetails);
		}
		CodeGenData.ManifestIdentifier = -1;
		IBlueprintNativeCodeGenModule::InitializeModule(CodeGenData);
	}

	{
		if (CookByTheBookOptions->bGenerateDependenciesForMaps)
		{
			for (const ITargetPlatform* Platform : TargetPlatforms)
			{
				CookByTheBookOptions->MapDependencyGraphs.Add(Platform);
			}
		}
	}
	
	// start shader code library cooking
	InitShaderCodeLibrary();
    CleanShaderCodeLibraries();
	
	if ( IsCookingDLC() )
	{
		// if we are cooking dlc we must be based on a release version cook
		check( !BasedOnReleaseVersion.IsEmpty() );

		for ( const ITargetPlatform* TargetPlatform: TargetPlatforms )
		{
			FString PlatformNameString = TargetPlatform->PlatformName();
			FName PlatformName(*PlatformNameString);
			FString OriginalSandboxRegistryFilename = GetReleaseVersionAssetRegistryPath(BasedOnReleaseVersion, PlatformNameString ) / TEXT("Metadata") / GetDevelopmentAssetRegistryFilename();

			TArray<FName> PackageList;
			// if this check fails probably because the asset registry can't be found or read
			bool bSucceeded = GetAllPackageFilenamesFromAssetRegistry(OriginalSandboxRegistryFilename, PackageList);
			if (!bSucceeded)
			{
				OriginalSandboxRegistryFilename = GetReleaseVersionAssetRegistryPath(BasedOnReleaseVersion, PlatformNameString) / GetAssetRegistryFilename();
				bSucceeded = GetAllPackageFilenamesFromAssetRegistry(OriginalSandboxRegistryFilename, PackageList);
			}

			if (!bSucceeded)
			{
				using namespace PlatformInfo;
				// Check all possible flavors 
				// For example release version could be cooked as Android_ASTC flavor, but DLC can be made as Android_ETC2
				FVanillaPlatformEntry VanillaPlatfromEntry = BuildPlatformHierarchy(PlatformName, EPlatformFilter::CookFlavor);
				for (const FPlatformInfo* PlatformFlaworInfo : VanillaPlatfromEntry.PlatformFlavors)
				{
					OriginalSandboxRegistryFilename = GetReleaseVersionAssetRegistryPath(BasedOnReleaseVersion, PlatformFlaworInfo->PlatformInfoName.ToString()) / GetAssetRegistryFilename();
					bSucceeded = GetAllPackageFilenamesFromAssetRegistry(OriginalSandboxRegistryFilename, PackageList);
					if (bSucceeded)
					{
						break;
					}
				}
			}
			check( bSucceeded );

			if ( bSucceeded )
			{
				TArray<const ITargetPlatform*> ResultPlatforms;
				ResultPlatforms.Add(TargetPlatform);
				TArray<bool> Succeeded;
				Succeeded.Add(true);
				for (const FName& PackageFilename : PackageList)
				{
					PackageTracker->CookedPackages.Add( FFilePlatformCookedPackage( PackageFilename, ResultPlatforms, Succeeded) );
				}
			}
			CookByTheBookOptions->BasedOnReleaseCookedPackages.Add(PlatformName, MoveTemp(PackageList));
		}
	}
	
	// don't resave the global shader map files in dlc
	if (!IsCookingDLC() && !(CookByTheBookStartupOptions.CookOptions & ECookByTheBookOptions::ForceDisableSaveGlobalShaders))
	{
		OpenShaderCodeLibrary(TEXT("Global"));

		SaveGlobalShaderMapFiles(TargetPlatforms);

		SaveShaderCodeLibrary(TEXT("Global"));
	}
	
	// Open the shader code library for the current project or the current DLC pack, depending on which we are cooking
    {
		FString LibraryName = !IsCookingDLC() ? FApp::GetProjectName() : CookByTheBookOptions->DlcName;
		OpenShaderCodeLibrary(LibraryName);
	}

	TArray<FName> FilesInPath;
	TSet<FName> StartupSoftObjectPackages;

	// Get the list of soft references, for both empty package and all startup packages
	GRedirectCollector.ProcessSoftObjectPathPackageList(NAME_None, false, StartupSoftObjectPackages);

	for (const FName& StartupPackage : CookByTheBookOptions->StartupPackages)
	{
		GRedirectCollector.ProcessSoftObjectPathPackageList(StartupPackage, false, StartupSoftObjectPackages);
	}

	CollectFilesToCook(FilesInPath, CookMaps, CookDirectories, IniMapSections, CookOptions, TargetPlatforms);

	// Add string asset packages after collecting files, to avoid accidentally activating the behavior to cook all maps if none are specified
	for (FName SoftObjectPackage : StartupSoftObjectPackages)
	{
		TMap<FName, FName> RedirectedPaths;

		// If this is a redirector, extract destination from asset registry
		if (ContainsRedirector(SoftObjectPackage, RedirectedPaths))
		{
			for (TPair<FName, FName>& RedirectedPath : RedirectedPaths)
			{
				GRedirectCollector.AddAssetPathRedirection(RedirectedPath.Key, RedirectedPath.Value);
			}
		}

		if (!CookByTheBookOptions->bDisableUnsolicitedPackages)
		{
			AddFileToCook(FilesInPath, SoftObjectPackage.ToString());
		}
	}
	
	if (FilesInPath.Num() == 0)
	{
		LogCookerMessage(FString::Printf(TEXT("No files found to cook.")), EMessageSeverity::Warning);
		UE_LOG(LogCook, Warning, TEXT("No files found."));
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("RANDOMPACKAGEORDER")) || 
		(FParse::Param(FCommandLine::Get(), TEXT("DIFFONLY")) && !FParse::Param(FCommandLine::Get(), TEXT("DIFFNORANDCOOK"))))
	{
		UE_LOG(LogCook, Log, TEXT("Randomizing package order."));
		//randomize the array, taking the Array_Shuffle approach, in order to help bring cooking determinism issues to the surface.
		for (int32 FileIndex = 0; FileIndex < FilesInPath.Num(); ++FileIndex)
		{
			FilesInPath.Swap(FileIndex, FMath::RandRange(0, FilesInPath.Num() - 1));
		}
	}

	{
#if OUTPUT_TIMING
		SCOPE_TIMER(GenerateLongPackageName);
#endif
		GenerateLongPackageNames(FilesInPath);
	}
	// add all the files for the requested platform to the cook list
	for ( const FName& FileFName : FilesInPath )
	{
		if (FileFName == NAME_None)
		{
			continue;
		}

		const FName PackageFileFName = PackageNameCache->GetCachedStandardPackageFileFName(FileFName);
		
		if (PackageFileFName != NAME_None)
		{
			PackageTracker->EnqueueUniqueCookRequest( FFilePlatformRequest( PackageFileFName, TargetPlatforms ) );
		}
		else if (!FLinkerLoad::IsKnownMissingPackage(FileFName))
		{
			const FString FileName = FileFName.ToString();
			LogCookerMessage( FString::Printf(TEXT("Unable to find package for cooking %s"), *FileName), EMessageSeverity::Warning );
			UE_LOG(LogCook, Warning, TEXT("Unable to find package for cooking %s"), *FileName)
		}	
	}


	if (!IsCookingDLC())
	{
		// if we are not cooking dlc then basedOnRelease version just needs to make sure that we cook all the packages which are in the previous release (as well as the new ones)
		if ( !BasedOnReleaseVersion.IsEmpty() )
		{
			// if we are based of a release and we are not cooking dlc then we should always be creating a new one (note that we could be creating the same one we are based of).
			// note that we might erroneously enter here if we are generating a patch instead and we accidentally passed in BasedOnReleaseVersion to the cooker instead of to unrealpak
			check( !CreateReleaseVersion.IsEmpty() );

			for ( const ITargetPlatform* TargetPlatform : TargetPlatforms )
			{
				// if we are based of a cook and we are creating a new one we need to make sure that at least all the old packages are cooked as well as the new ones
				FString OriginalAssetRegistryPath = GetReleaseVersionAssetRegistryPath( BasedOnReleaseVersion, TargetPlatform->PlatformName() ) / GetAssetRegistryFilename();

				TArray<FName> PackageFiles;
				verify( GetAllPackageFilenamesFromAssetRegistry(OriginalAssetRegistryPath, PackageFiles) );

				TArray<const ITargetPlatform*, TInlineAllocator<1>> RequestPlatforms;
				RequestPlatforms.Add(TargetPlatform);
				for ( const FName& PackageFilename : PackageFiles )
				{
					PackageTracker->EnqueueUniqueCookRequest( FFilePlatformRequest( PackageFilename, RequestPlatforms) );
				}
			}
		}
	}


	// this is to support canceling cooks from the editor
	// this is required to make sure that the cooker is in a good state after cancel occurs
	// if too many packages are being recooked after resume then we may need to figure out a different way to do this
	if (IsCookingInEditor())
	{
		for (const FFilePlatformRequest& PreviousRequest : CookByTheBookOptions->PreviousCookRequests)
		{
			// do not queue previous requests that targeted a different platform
			const TArray<const ITargetPlatform*>& PreviousPlatforms = PreviousRequest.GetPlatforms();
			if (TargetPlatforms.Num() == 1 && PreviousPlatforms.Num() == TargetPlatforms.Num() && PreviousPlatforms[0] == TargetPlatforms[0])
			{
				PackageTracker->EnqueueUniqueCookRequest(PreviousRequest);
			}
		}
	}
	CookByTheBookOptions->PreviousCookRequests.Empty();

}

bool UCookOnTheFlyServer::RecompileChangedShaders(const TArray<const ITargetPlatform*>& TargetPlatforms)
{
	bool bShadersRecompiled = false;
	for (const ITargetPlatform* TargetPlatform : TargetPlatforms)
	{
		bShadersRecompiled |= RecompileChangedShadersForPlatform(TargetPlatform->PlatformName());
	}
	return bShadersRecompiled;
}

bool UCookOnTheFlyServer::RecompileChangedShaders(const TArray<FName>& TargetPlatformNames)
{
	bool bShadersRecompiled = false;
	for (const FName TargetPlatformName : TargetPlatformNames)
	{
		bShadersRecompiled |= RecompileChangedShadersForPlatform(TargetPlatformName.ToString());
	}
	return bShadersRecompiled;
}

/* UCookOnTheFlyServer callbacks
 *****************************************************************************/

void UCookOnTheFlyServer::MaybeMarkPackageAsAlreadyLoaded(UPackage *Package)
{
	// can't use this optimization while cooking in editor
	check(IsCookingInEditor()==false);
	check(IsCookByTheBookMode());

	if (bIgnoreMarkupPackageAlreadyLoaded == true)
	{
		return;
	}

	if (bIsInitializingSandbox)
	{
		return;
	}

	// if the package is already fully loaded then we are not going to mark it up anyway
	if ( Package->IsFullyLoaded() )
	{
		return;
	}

	FName StandardName = PackageNameCache->GetCachedStandardPackageFileFName(Package);

	// UE_LOG(LogCook, Display, TEXT("Loading package %s"), *StandardName.ToString());

	bool bShouldMarkAsAlreadyProcessed = false;

	TArray<const ITargetPlatform*> CookedPlatforms;
	if (PackageTracker->CookedPackages.GetCookedPlatforms(StandardName, CookedPlatforms))
	{
		bShouldMarkAsAlreadyProcessed = true;
		for (const ITargetPlatform* TargetPlatform : PlatformManager->GetSessionPlatforms())
		{
			if (!CookedPlatforms.Contains(TargetPlatform))
			{
				bShouldMarkAsAlreadyProcessed = false;
				break;
			}
		}

		FString Platforms;
		for (const ITargetPlatform* CookedPlatform : CookedPlatforms)
		{
			Platforms += TEXT(" ");
			Platforms += CookedPlatform->PlatformName();
		}
		if (IsCookFlagSet(ECookInitializationFlags::LogDebugInfo))
		{
			if (!bShouldMarkAsAlreadyProcessed)
			{
				UE_LOG(LogCook, Display, TEXT("Reloading package %s slowly because it wasn't cooked for all platforms %s."), *StandardName.ToString(), *Platforms);
			}
			else
			{
				UE_LOG(LogCook, Display, TEXT("Marking %s as reloading for cooker because it's been cooked for platforms %s."), *StandardName.ToString(), *Platforms);
			}
		}
	}

	check(IsInGameThread());
	if (PackageTracker->NeverCookPackageList.Contains(StandardName))
	{
		bShouldMarkAsAlreadyProcessed = true;
		UE_LOG(LogCook, Verbose, TEXT("Marking %s as reloading for cooker because it was requested as never cook package."), *StandardName.ToString());
	}

	if (bShouldMarkAsAlreadyProcessed)
	{
		if (Package->IsFullyLoaded() == false)
		{
			Package->SetPackageFlags(PKG_ReloadingForCooker);
		}
	}
}


bool UCookOnTheFlyServer::HandleNetworkFileServerNewConnection(const FString& VersionInfo, const FString& PlatformName)
{
	const uint32 CL = FEngineVersion::CompatibleWith().GetChangelist();
	const FString Branch = FEngineVersion::CompatibleWith().GetBranch();

	const FString LocalVersionInfo = FString::Printf(TEXT("%s %d"), *Branch, CL);

	if (!AddCookOnTheFlyPlatform(PlatformName))
	{
		UE_LOG(LogCook, Warning, TEXT("Unrecognized PlatformName '%s', CookOnTheFly requests for this platform will fail."), *PlatformName);
		return false;
	}

	UE_LOG(LogCook, Display, TEXT("Connection received of version %s local version %s"), *VersionInfo, *LocalVersionInfo);

	if (LocalVersionInfo != VersionInfo)
	{
		UE_LOG(LogCook, Warning, TEXT("Connection tried to connect with incompatible version"));
		// return false;
	}
	return true;
}

void UCookOnTheFlyServer::GetCookOnTheFlyUnsolicitedFiles(const ITargetPlatform* TargetPlatform, TArray<FString>& UnsolicitedFiles, const FString& Filename)
{
	TArray<FName> UnsolicitedFilenames;
	PackageTracker->UnsolicitedCookedPackages.GetPackagesForPlatformAndRemove(TargetPlatform, UnsolicitedFilenames);

	for (const FName& UnsolicitedFile : UnsolicitedFilenames)
	{
		FString StandardFilename = UnsolicitedFile.ToString();
		FPaths::MakeStandardFilename(StandardFilename);

		// check that the sandboxed file exists... if it doesn't then don't send it back
		// this can happen if the package was saved but the async writer thread hasn't finished writing it to disk yet

		FString SandboxFilename = ConvertToFullSandboxPath(*Filename, true);
		SandboxFilename.ReplaceInline(TEXT("[Platform]"), *TargetPlatform->PlatformName());
		if (IFileManager::Get().FileExists(*SandboxFilename))
		{
			UnsolicitedFiles.Add(StandardFilename);
		}
		else
		{
			UE_LOG(LogCook, Warning, TEXT("Unsolicited file doesn't exist in sandbox, ignoring %s"), *Filename);
		}
	}

	UPackage::WaitForAsyncFileWrites();
}

void UCookOnTheFlyServer::HandleNetworkFileServerFileRequest(const FString& Filename, const FString& PlatformName, TArray<FString>& UnsolicitedFiles)
{
	check(IsCookOnTheFlyMode());

	ITargetPlatform* TargetPlatform = AddCookOnTheFlyPlatform(PlatformName);
	if (!TargetPlatform)
	{
		UE_LOG(LogCook, Warning, TEXT("Unrecognized PlatformName '%s', CookOnTheFly FileServerRequest requests for this platform will fail."), *PlatformName);
		return;
	}

	const bool bIsCookable = FPackageName::IsPackageExtension(*FPaths::GetExtension(Filename, true));
	if (!bIsCookable)
	{
		// Wait for the Platform to be added if this is the first time; it is not legal to call GetCookOnTheFlyUnsolicitedFiles until after the platform has been added
		while (!PlatformManager->IsPlatformInitialized(TargetPlatform))
		{
			FPlatformProcess::Sleep(0.001f);
		}
		GetCookOnTheFlyUnsolicitedFiles(TargetPlatform, UnsolicitedFiles, Filename);
		return;
	}

	FString StandardFileName = Filename;
	FPaths::MakeStandardFilename( StandardFileName );
	FName StandardFileFname = FName(*StandardFileName);
	TArray<const ITargetPlatform*, TInlineAllocator<1>> TargetPlatforms;
	TargetPlatforms.Add(TargetPlatform);
	FFilePlatformRequest FileRequest( StandardFileFname, TargetPlatforms);

#if PROFILE_NETWORK
	double StartTime = FPlatformTime::Seconds();
	check(NetworkRequestEvent);
	NetworkRequestEvent->Reset();
#endif
	
	UE_LOG(LogCook, Display, TEXT("Requesting file from cooker %s"), *StandardFileName);

	{
		check(&PlatformManager->GetSessionLock() == &RequestLock && &PackageTracker->GetRequestLock() == &RequestLock);
		FScopeLock ScopeLock(&RequestLock);
		PlatformManager->AddRefCookOnTheFlyPlatform(TargetPlatform, *PackageTracker);
		PackageTracker->GetThreadUnsafeCookRequests().EnqueueUnique(FileRequest, true);
	}
	
	if (PackageTracker->CookRequestEvent)
	{
		PackageTracker->CookRequestEvent->Trigger();
	}

#if PROFILE_NETWORK
	bool bFoundNetworkEventWait = true;
	while (NetworkRequestEvent->Wait(1) == false)
	{
		// for some reason we missed the stat
		if (PackageTracker->CookedPackages.Exists(FileRequest))
		{
			double DeltaTimeTillRequestForfilled = FPlatformTime::Seconds() - StartTime;
			TimeTillRequestForfilled += DeltaTimeTillRequestForfilled;
			TimeTillRequestForfilledError += DeltaTimeTillRequestForfilled;
			StartTime = FPlatformTime::Seconds();
			bFoundNetworkEventWait = false;
			break;
		}
	}

	// wait for tick entry here
	TimeTillRequestStarted += FPlatformTime::Seconds() - StartTime;
	StartTime = FPlatformTime::Seconds();
#endif

	while (!PackageTracker->CookedPackages.Exists(FileRequest))
	{
		FPlatformProcess::Sleep(0.001f);
	}


	{
		FScopeLock ScopeLock(&PlatformManager->GetSessionLock());
		PlatformManager->ReleaseCookOnTheFlyPlatform(TargetPlatform);
	}


#if PROFILE_NETWORK
	if ( bFoundNetworkEventWait )
	{
		TimeTillRequestForfilled += FPlatformTime::Seconds() - StartTime;
		StartTime = FPlatformTime::Seconds();
	}
#endif
	UE_LOG( LogCook, Display, TEXT("Cook complete %s"), *FileRequest.GetFilename().ToString())

	GetCookOnTheFlyUnsolicitedFiles(TargetPlatform, UnsolicitedFiles, Filename);

#if PROFILE_NETWORK
	WaitForAsyncFilesWrites += FPlatformTime::Seconds() - StartTime;
	StartTime = FPlatformTime::Seconds();
#endif
#if DEBUG_COOKONTHEFLY
	UE_LOG( LogCook, Display, TEXT("Processed file request %s"), *Filename );
#endif

}


FString UCookOnTheFlyServer::HandleNetworkGetSandboxPath()
{
	return SandboxFile->GetSandboxDirectory();
}

void UCookOnTheFlyServer::HandleNetworkGetPrecookedList(const FString& PlatformName, TMap<FString, FDateTime>& PrecookedFileList)
{
	ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();
	const ITargetPlatform* TargetPlatform = TPM.FindTargetPlatform(PlatformName);
	if (!TargetPlatform)
	{
		UE_LOG(LogCook, Warning, TEXT("Unrecognized PlatformName '%s' in HandleNetworkGetPrrequests, returning 0 files."), *PlatformName);
		return;
	}

	TArray<FName> CookedPlatformFiles;
	PackageTracker->CookedPackages.GetCookedFilesForPlatform(TargetPlatform, CookedPlatformFiles, /* include failed */ true, /* include successful */ true);


	for ( const FName& CookedFile : CookedPlatformFiles)
	{
		const FString SandboxFilename = ConvertToFullSandboxPath(CookedFile.ToString(), true, PlatformName);
		if (IFileManager::Get().FileExists(*SandboxFilename))
		{
			continue;
		}

		PrecookedFileList.Add(CookedFile.ToString(),FDateTime::MinValue());
	}
}

void UCookOnTheFlyServer::HandleNetworkFileServerRecompileShaders(const FShaderRecompileData& RecompileData)
{
	// shouldn't receive network requests unless we are in cook on the fly mode
	check( IsCookOnTheFlyMode() );
	check( !IsCookingDLC() );
	// if we aren't in the game thread, we need to push this over to the game thread and wait for it to finish
	if (!IsInGameThread())
	{
		UE_LOG(LogCook, Display, TEXT("Got a recompile request on non-game thread"));

		// make a new request
		FRecompileRequest* Request = new FRecompileRequest;
		Request->RecompileData = RecompileData;
		Request->bComplete = false;

		// push the request for the game thread to process
		PackageTracker->RecompileRequests.Enqueue(Request);

		// wait for it to complete (the game thread will pull it out of the TArray, but I will delete it)
		while (!Request->bComplete)
		{
			FPlatformProcess::Sleep(0);
		}
		delete Request;
		UE_LOG(LogCook, Display, TEXT("Completed recompile..."));

		// at this point, we are done on the game thread, and ModifiedFiles will have been filled out
		return;
	}

	FString OutputDir = GetSandboxDirectory(RecompileData.PlatformName);

	RecompileShadersForRemote
		(RecompileData.PlatformName, 
		RecompileData.ShaderPlatform == -1 ? SP_NumPlatforms : (EShaderPlatform)RecompileData.ShaderPlatform,
		OutputDir, 
		RecompileData.MaterialsToLoad, 
		RecompileData.MeshMaterialMaps, 
		RecompileData.ModifiedFiles,
		RecompileData.bCompileChangedShaders);
}

bool UCookOnTheFlyServer::GetAllPackageFilenamesFromAssetRegistry( const FString& AssetRegistryPath, TArray<FName>& OutPackageFilenames ) const
{
	FArrayReader SerializedAssetData;
	if (FFileHelper::LoadFileToArray(SerializedAssetData, *AssetRegistryPath))
	{
		FAssetRegistryState TempState;
		FAssetRegistrySerializationOptions LoadOptions;
		LoadOptions.bSerializeDependencies = false;
		LoadOptions.bSerializePackageData = false;

		TempState.Serialize(SerializedAssetData, LoadOptions);

		const TMap<FName, const FAssetData*>& RegistryDataMap = TempState.GetObjectPathToAssetDataMap();

		for (const TPair<FName, const FAssetData*>& RegistryData : RegistryDataMap)
		{
			const FAssetData* NewAssetData = RegistryData.Value;
			FName CachedPackageFileFName = PackageNameCache->GetCachedStandardPackageFileFName(NewAssetData->ObjectPath);
			if (CachedPackageFileFName != NAME_None)
			{
				OutPackageFilenames.Add(CachedPackageFileFName);
			}
			else
			{
				UE_LOG(LogCook, Warning, TEXT("Could not resolve package %s from %s"), *NewAssetData->ObjectPath.ToString(), *AssetRegistryPath);
			}
		}
		return true;
	}

	return false;
}

uint32 UCookOnTheFlyServer::FullLoadAndSave(uint32& CookedPackageCount)
{
	SCOPE_TIMER(FullLoadAndSave);
	check(CurrentCookMode == ECookMode::CookByTheBook);
	check(CookByTheBookOptions);
	check(IsInGameThread());

	uint32 Result = 0;

	const TArray<const ITargetPlatform*>& TargetPlatforms = PlatformManager->GetSessionPlatforms();

	{
		UE_LOG(LogCook, Display, TEXT("Loading requested packages..."));
		SCOPE_TIMER(FullLoadAndSave_RequestedLoads);
		while (HasCookRequests())
		{
			FFilePlatformRequest ToBuild;
			TArray<FPackageTracker::FTickCommand> TickCommands;
			FPackageTracker::ERequestType RequestType = PackageTracker->DequeueRequest(TickCommands, /* out */ ToBuild);
			if (RequestType == FPackageTracker::ERequestType::TickCommand)
			{
				for (FPackageTracker::FTickCommand& TickCommand : TickCommands)
				{
					TickCommand();
				}
				continue;
			}
			check(RequestType == FPackageTracker::ERequestType::Cook && ToBuild.IsValid());

			const FName BuildFilenameFName = ToBuild.GetFilename();
			if (!PackageTracker->NeverCookPackageList.Contains(BuildFilenameFName))
			{
				const FString BuildFilename = BuildFilenameFName.ToString();
				GIsCookerLoadingPackage = true;
				SCOPE_TIMER(LoadPackage);
				LoadPackage(nullptr, *BuildFilename, LOAD_None);
				if (GShaderCompilingManager)
				{
					GShaderCompilingManager->ProcessAsyncResults(true, false);
				}
				GIsCookerLoadingPackage = false;
			}
		}
	}

	const bool bSaveConcurrent = FParse::Param(FCommandLine::Get(), TEXT("ConcurrentSave"));
	uint32 SaveFlags = SAVE_KeepGUID | SAVE_Async | SAVE_ComputeHash | (IsCookFlagSet(ECookInitializationFlags::Unversioned) ? SAVE_Unversioned : 0);
	if (bSaveConcurrent)
	{
		SaveFlags |= SAVE_Concurrent;
	}
	TArray<UPackage*> PackagesToSave;
	PackagesToSave.Reserve(65536);

	TSet<UPackage*> ProcessedPackages;
	ProcessedPackages.Reserve(65536);

	TMap<UWorld*, bool> WorldsToPostSaveRoot;
	WorldsToPostSaveRoot.Reserve(1024);

	TArray<UObject*> ObjectsToWaitForCookedPlatformData;
	ObjectsToWaitForCookedPlatformData.Reserve(65536);

	TArray<FString> PackagesToLoad;
	do
	{
		PackagesToLoad.Reset();

		{
			UE_LOG(LogCook, Display, TEXT("Caching platform data and discovering string referenced assets..."));
			SCOPE_TIMER(FullLoadAndSave_CachePlatformDataAndDiscoverNewAssets);
			for (TObjectIterator<UPackage> It; It; ++It)
			{
				UPackage* Package = *It;
				check(Package);

				if (ProcessedPackages.Contains(Package))
				{
					continue;
				}

				ProcessedPackages.Add(Package);

				if (Package->HasAnyPackageFlags(PKG_CompiledIn | PKG_ForDiffing | PKG_EditorOnly | PKG_Compiling | PKG_PlayInEditor | PKG_ContainsScript | PKG_ReloadingForCooker))
				{
					continue;
				}

				if (Package == GetTransientPackage())
				{
					continue;
				}

				FName PackageName = Package->GetFName();
				FName StandardPackageName = PackageNameCache->GetCachedStandardPackageFileFName(PackageName);
				if (PackageTracker->NeverCookPackageList.Contains(StandardPackageName))
				{
					// refuse to save this package
					continue;
				}

				if (!FPackageName::IsValidLongPackageName(PackageName.ToString()))
				{
					continue;
				}

				if (Package->GetOuter() != nullptr)
				{
					UE_LOG(LogCook, Warning, TEXT("Skipping package %s with outermost %s"), *Package->GetName(), *Package->GetOutermost()->GetName());
					continue;
				}

				PackagesToSave.Add(Package);

				{
					SCOPE_TIMER(FullLoadAndSave_PerObjectLogic);
					TSet<UObject*> ProcessedObjects;
					ProcessedObjects.Reserve(64);
					bool bObjectsMayHaveBeenCreated = false;
					do
					{
						bObjectsMayHaveBeenCreated = false;
						TArray<UObject*> ObjsInPackage;
						{
							SCOPE_TIMER(FullLoadAndSave_GetObjectsWithOuter);
							GetObjectsWithOuter(Package, ObjsInPackage, true);
						}
						for (UObject* Obj : ObjsInPackage)
						{
							if (Obj->HasAnyFlags(RF_Transient))
							{
								continue;
							}

							if (ProcessedObjects.Contains(Obj))
							{
								continue;
							}

							bObjectsMayHaveBeenCreated = true;
							ProcessedObjects.Add(Obj);

							UWorld* World = Cast<UWorld>(Obj);
							bool bInitializedPhysicsSceneForSave = false;
							bool bForceInitializedWorld = false;
							if (World && bSaveConcurrent)
							{
								SCOPE_TIMER(FullLoadAndSave_SettingUpWorlds);
								// We need a physics scene at save time in case code does traces during onsave events.
								bInitializedPhysicsSceneForSave = GEditor->InitializePhysicsSceneForSaveIfNecessary(World, bForceInitializedWorld);

								GIsCookerLoadingPackage = true;
								{
									SCOPE_TIMER(FullLoadAndSave_PreSaveWorld);
									GEditor->OnPreSaveWorld(SaveFlags, World);
								}
								{
									SCOPE_TIMER(FullLoadAndSave_PreSaveRoot);
									bool bCleanupIsRequired = World->PreSaveRoot(TEXT(""));
									WorldsToPostSaveRoot.Add(World, bCleanupIsRequired);
								}
								GIsCookerLoadingPackage = false;
							}

							bool bAllPlatformDataLoaded = true;
							bool bIsTexture = Obj->IsA(UTexture::StaticClass());
							for (const ITargetPlatform* TargetPlatform : TargetPlatforms)
							{
								if (bSaveConcurrent)
								{
									GIsCookerLoadingPackage = true;
									{
										SCOPE_TIMER(FullLoadAndSave_PreSave);
										Obj->PreSave(TargetPlatform);
									}
									GIsCookerLoadingPackage = false;
								}

								if (!bIsTexture || bSaveConcurrent)
								{
									SCOPE_TIMER(FullLoadAndSave_BeginCache);
									Obj->BeginCacheForCookedPlatformData(TargetPlatform);
									if (!Obj->IsCachedCookedPlatformDataLoaded(TargetPlatform))
									{
										bAllPlatformDataLoaded = false;
									}
								}
							}

							if (!bAllPlatformDataLoaded)
							{
								ObjectsToWaitForCookedPlatformData.Add(Obj);
							}

							if (World && bInitializedPhysicsSceneForSave)
							{
								SCOPE_TIMER(FullLoadAndSave_CleaningUpWorlds);
								GEditor->CleanupPhysicsSceneThatWasInitializedForSave(World, bForceInitializedWorld);
							}
						}
					} while (bObjectsMayHaveBeenCreated);

					if (bSaveConcurrent)
					{
						SCOPE_TIMER(FullLoadAndSave_MiscPrep);
						// Precache the metadata so we don't risk rehashing the map in the parallelfor below
						Package->GetMetaData();
					}
				}

				{
					SCOPE_TIMER(ResolveStringReferences);
					TSet<FName> StringAssetPackages;
					GRedirectCollector.ProcessSoftObjectPathPackageList(PackageName, false, StringAssetPackages);

					for (FName StringAssetPackage : StringAssetPackages)
					{
						TMap<FName, FName> RedirectedPaths;

						// If this is a redirector, extract destination from asset registry
						if (ContainsRedirector(StringAssetPackage, RedirectedPaths))
						{
							for (TPair<FName, FName>& RedirectedPath : RedirectedPaths)
							{
								GRedirectCollector.AddAssetPathRedirection(RedirectedPath.Key, RedirectedPath.Value);
								PackagesToLoad.Add(FPackageName::ObjectPathToPackageName(RedirectedPath.Value.ToString()));
							}
						}
						else
						{
							PackagesToLoad.Add(StringAssetPackage.ToString());
						}
					}
				}
			}
		}

		{
			UE_LOG(LogCook, Display, TEXT("Loading string referenced assets..."));
			SCOPE_TIMER(FullLoadAndSave_LoadStringReferencedAssets);
			GIsCookerLoadingPackage = true;
			for (const FString& ToLoad : PackagesToLoad)
			{
				FName BuildFilenameFName = PackageNameCache->GetCachedStandardPackageFileFName(FName(*ToLoad));
				if (!PackageTracker->NeverCookPackageList.Contains(BuildFilenameFName))
				{
					LoadPackage(nullptr, *ToLoad, LOAD_None);
					if (GShaderCompilingManager)
					{
						GShaderCompilingManager->ProcessAsyncResults(true, false);
					}
				}
			}
			GIsCookerLoadingPackage = false;
		}
	} while (PackagesToLoad.Num() > 0);

	ProcessedPackages.Empty();

	// When saving concurrently, flush async loading since that is normally done internally in SavePackage
	if (bSaveConcurrent)
	{
		UE_LOG(LogCook, Display, TEXT("Flushing async loading..."));
		SCOPE_TIMER(FullLoadAndSave_FlushAsyncLoading);
		FlushAsyncLoading();
	}

	if (bSaveConcurrent)
	{
		UE_LOG(LogCook, Display, TEXT("Waiting for async tasks..."));
		SCOPE_TIMER(FullLoadAndSave_ProcessThreadUntilIdle);
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
	}

	// Wait for all shaders to finish compiling
	if (GShaderCompilingManager)
	{
		UE_LOG(LogCook, Display, TEXT("Waiting for shader compilation..."));
		SCOPE_TIMER(FullLoadAndSave_WaitForShaderCompilation);
		while(GShaderCompilingManager->IsCompiling())
		{
			GShaderCompilingManager->ProcessAsyncResults(false, false);
			FPlatformProcess::Sleep(0.5f);
		}

		// One last process to get the shaders that were compiled at the very end
		GShaderCompilingManager->ProcessAsyncResults(false, false);
	}

	if (GDistanceFieldAsyncQueue)
	{
		UE_LOG(LogCook, Display, TEXT("Waiting for distance field async operations..."));
		SCOPE_TIMER(FullLoadAndSave_WaitForDistanceField);
		GDistanceFieldAsyncQueue->BlockUntilAllBuildsComplete();
	}

	// Wait for all platform data to be loaded
	{
		UE_LOG(LogCook, Display, TEXT("Waiting for cooked platform data..."));
		SCOPE_TIMER(FullLoadAndSave_WaitForCookedPlatformData);
		while (ObjectsToWaitForCookedPlatformData.Num() > 0)
		{
			for (int32 ObjIdx = ObjectsToWaitForCookedPlatformData.Num() - 1; ObjIdx >= 0; --ObjIdx)
			{
				UObject* Obj = ObjectsToWaitForCookedPlatformData[ObjIdx];
				bool bAllPlatformDataLoaded = true;
				for (const ITargetPlatform* TargetPlatform : TargetPlatforms)
				{
					if (!Obj->IsCachedCookedPlatformDataLoaded(TargetPlatform))
					{
						bAllPlatformDataLoaded = false;
						break;
					}
				}

				if (bAllPlatformDataLoaded)
				{
					ObjectsToWaitForCookedPlatformData.RemoveAtSwap(ObjIdx, 1, false);
				}
			}

			FPlatformProcess::Sleep(0.001f);
		}

		ObjectsToWaitForCookedPlatformData.Empty();
	}

	{
		UE_LOG(LogCook, Display, TEXT("Saving packages..."));
		SCOPE_TIMER(FullLoadAndSave_Save);
		check(bIsSavingPackage == false);
		bIsSavingPackage = true;

		if (bSaveConcurrent)
		{
			GIsSavingPackage = true;
		}

		int64 ParallelSavedPackages = 0;
		ParallelFor(PackagesToSave.Num(), [this, &PackagesToSave, &TargetPlatforms ,&ParallelSavedPackages, SaveFlags, bSaveConcurrent](int32 PackageIdx)
		{
			UPackage* Package = PackagesToSave[PackageIdx];
			check(Package);

			// when concurrent saving is supported, precaching will need to be refactored for concurrency
			if (!bSaveConcurrent)
			{
				// precache texture platform data ahead of save
				const int32 PrecacheOffset = 512;
				UPackage* PrecachePackage = PackageIdx + PrecacheOffset < PackagesToSave.Num() ? PackagesToSave[PackageIdx + PrecacheOffset] : nullptr;
				if (PrecachePackage)
				{
					TArray<UObject*> ObjsInPackage;
					{
						GetObjectsWithOuter(PrecachePackage, ObjsInPackage, false);
					}

					for (UObject* Obj : ObjsInPackage)
					{
						if (Obj->HasAnyFlags(RF_Transient) || !Obj->IsA(UTexture::StaticClass()))
						{
							continue;
						}

						for (const ITargetPlatform* TargetPlatform : TargetPlatforms)
						{
							Obj->BeginCacheForCookedPlatformData(TargetPlatform);
						}
					}
				}
			}

			FName PackageName = Package->GetFName();
			FCachedPackageFilename* CachedPackageFilename = PackageNameCache->PackageFilenameCache.Find(PackageName);
			check(CachedPackageFilename);
	
			if (CachedPackageFilename->PackageFilename.Len())
			{
				// Use SandboxFile to do path conversion to properly handle sandbox paths (outside of standard paths in particular).
				FString Filename = ConvertToFullSandboxPath(*CachedPackageFilename->PackageFilename, true);

				// look for a world object in the package (if there is one, there's a map)
				EObjectFlags FlagsToCook = RF_Public;
				TArray<UObject*> ObjsInPackage;
				UWorld* World = nullptr;
				{
					//SCOPE_TIMER(SaveCookedPackage_FindWorldInPackage);
					GetObjectsWithOuter(Package, ObjsInPackage, false);
					for (UObject* Obj : ObjsInPackage)
					{
						World = Cast<UWorld>(Obj);
						if (World)
						{
							FlagsToCook = RF_NoFlags;
							break;
						}
					}
				}

				FString PackageNameStr = PackageName.ToString();
				bool bExcludeFromNonEditorTargets = IsCookFlagSet(ECookInitializationFlags::SkipEditorContent) && (PackageNameStr.StartsWith(TEXT("/Engine/Editor")) || PackageNameStr.StartsWith(TEXT("/Engine/VREditor")));

				uint32 OriginalPackageFlags = Package->GetPackageFlags();

				TArray<bool> SavePackageSuccessPerPlatform;
				SavePackageSuccessPerPlatform.SetNum(TargetPlatforms.Num());
				for (int32 PlatformIndex = 0; PlatformIndex < TargetPlatforms.Num(); ++PlatformIndex)
				{
					const ITargetPlatform* Target = TargetPlatforms[PlatformIndex];

					// don't save Editor resources from the Engine if the target doesn't have editoronly data
					bool bCookPackage = (!bExcludeFromNonEditorTargets || Target->HasEditorOnlyData());
					if (UAssetManager::IsValid() && !UAssetManager::Get().ShouldCookForPlatform(Package, Target))
					{
						bCookPackage = false;
					}

					if (bCookPackage)
					{
						FString PlatFilename = Filename.Replace(TEXT("[Platform]"), *Target->PlatformName());

						UE_CLOG(GCookProgressDisplay & (int32)ECookProgressDisplayMode::PackageNames, LogCook, Display, TEXT("Cooking %s -> %s"), *Package->GetName(), *PlatFilename);

						bool bSwap = (!Target->IsLittleEndian()) ^ (!PLATFORM_LITTLE_ENDIAN);
						if (!Target->HasEditorOnlyData())
						{
							Package->SetPackageFlags(PKG_FilterEditorOnly);
						}
						else
						{
							Package->ClearPackageFlags(PKG_FilterEditorOnly);
						}
								
						GIsCookerLoadingPackage = true;
						FSavePackageResultStruct SaveResult = GEditor->Save(Package, World, FlagsToCook, *PlatFilename, GError, NULL, bSwap, false, SaveFlags, Target, FDateTime::MinValue(), false);
						GIsCookerLoadingPackage = false;

						if (SaveResult == ESavePackageResult::Success && UAssetManager::IsValid())
						{
							if (!UAssetManager::Get().VerifyCanCookPackage(Package->GetFName()))
							{
								SaveResult = ESavePackageResult::Error;
							}
						}

						const bool bSucceededSavePackage = (SaveResult == ESavePackageResult::Success || SaveResult == ESavePackageResult::GenerateStub || SaveResult == ESavePackageResult::ReplaceCompletely);
						if (bSucceededSavePackage)
						{
							FAssetRegistryGenerator* Generator = PlatformManager->GetPlatformData(Target)->RegistryGenerator.Get();
							UpdateAssetRegistryPackageData(Generator, Package->GetFName(), SaveResult);

							FPlatformAtomics::InterlockedIncrement(&ParallelSavedPackages);
						}

						if (SaveResult != ESavePackageResult::ReferencedOnlyByEditorOnlyData)
						{
							SavePackageSuccessPerPlatform[PlatformIndex] = true;
						}
						else
						{
							SavePackageSuccessPerPlatform[PlatformIndex] = false;
						}
					}
					else
					{
						SavePackageSuccessPerPlatform[PlatformIndex] = false;
					}
				}

				check(CachedPackageFilename->StandardFileFName != NAME_None);

				FFilePlatformCookedPackage FileRequest(CachedPackageFilename->StandardFileFName, TargetPlatforms, SavePackageSuccessPerPlatform);
				PackageTracker->CookedPackages.Add(FileRequest);

				if (SavePackageSuccessPerPlatform.Contains(false))
				{
					PackageTracker->UncookedEditorOnlyPackages.Add(PackageName);
				}

				Package->SetPackageFlagsTo(OriginalPackageFlags);
			}
		}, !bSaveConcurrent);

		if (bSaveConcurrent)
		{
			GIsSavingPackage = false;
		}

		CookedPackageCount += ParallelSavedPackages;
		if (ParallelSavedPackages > 0)
		{
			Result |= COSR_CookedPackage;
		}

		check(bIsSavingPackage == true);
		bIsSavingPackage = false;
	}

	if (bSaveConcurrent)
	{
		UE_LOG(LogCook, Display, TEXT("Calling PostSaveRoot on worlds..."));
		SCOPE_TIMER(FullLoadAndSave_PostSaveRoot);
		for (auto WorldIt = WorldsToPostSaveRoot.CreateConstIterator(); WorldIt; ++WorldIt)
		{
			UWorld* World = WorldIt.Key();
			check(World);
			World->PostSaveRoot(WorldIt.Value());
		}
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
