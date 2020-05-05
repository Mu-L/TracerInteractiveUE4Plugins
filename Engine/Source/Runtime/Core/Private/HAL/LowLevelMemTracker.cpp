// Copyright Epic Games, Inc. All Rights Reserved.

#include "HAL/LowLevelMemTracker.h"
#include "HAL/LowLevelMemStats.h"
#include "Misc/ScopeLock.h"
#include "LowLevelMemoryUtils.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/CString.h"
#include "HAL/IConsoleManager.h"

#if ENABLE_LOW_LEVEL_MEM_TRACKER
#include "MemPro/MemProProfiler.h"

// There is a little memory and cpu overhead in tracking peak memory but it is generally more useful than current memory.
// Disable if you need a little more memory or speed
#define LLM_TRACK_PEAK_MEMORY 0		// currently disabled because there was a problem with tracking peaks from multiple threads.

TAutoConsoleVariable<int32> CVarLLMWriteInterval(
	TEXT("LLM.LLMWriteInterval"),
	5,
	TEXT("The number of seconds between each line in the LLM csv (zero to write every frame)")
);

DECLARE_LLM_MEMORY_STAT(TEXT("LLM Overhead"), STAT_LLMOverheadTotal, STATGROUP_LLMOverhead);

DEFINE_STAT(STAT_EngineSummaryLLM);
DEFINE_STAT(STAT_ProjectSummaryLLM);

/*
 * LLM stats referenced by ELLMTagNames
 */
DECLARE_LLM_MEMORY_STAT(TEXT("Total"), STAT_TotalLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Untracked"), STAT_UntrackedLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Total"), STAT_PlatformTotalLLM, STATGROUP_LLMPlatform);
DECLARE_LLM_MEMORY_STAT(TEXT("Tracked Total"), STAT_TrackedTotalLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Untagged"), STAT_UntaggedTotalLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("WorkingSetSize"), STAT_WorkingSetSizeLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("PagefileUsed"), STAT_PagefileUsedLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Tracked Total"), STAT_PlatformTrackedTotalLLM, STATGROUP_LLMPlatform);
DECLARE_LLM_MEMORY_STAT(TEXT("Untagged"), STAT_PlatformUntaggedTotalLLM, STATGROUP_LLMPlatform);
DECLARE_LLM_MEMORY_STAT(TEXT("Untracked"), STAT_PlatformUntrackedLLM, STATGROUP_LLMPlatform);
DECLARE_LLM_MEMORY_STAT(TEXT("Overhead"), STAT_PlatformOverheadLLM, STATGROUP_LLMPlatform);
DECLARE_LLM_MEMORY_STAT(TEXT("FMalloc"), STAT_FMallocLLM, STATGROUP_LLMPlatform);
DECLARE_LLM_MEMORY_STAT(TEXT("FMalloc Unused"), STAT_FMallocUnusedLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("ThreadStack"), STAT_ThreadStackLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("ThreadStackPlatform"), STAT_ThreadStackPlatformLLM, STATGROUP_LLMPlatform);
DECLARE_LLM_MEMORY_STAT(TEXT("Program Size"), STAT_ProgramSizePlatformLLM, STATGROUP_LLMPlatform);
DECLARE_LLM_MEMORY_STAT(TEXT("Program Size"), STAT_ProgramSizeLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("OOM Backup Pool"), STAT_OOMBackupPoolPlatformLLM, STATGROUP_LLMPlatform);
DECLARE_LLM_MEMORY_STAT(TEXT("OOM Backup Pool"), STAT_OOMBackupPoolLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("GenericPlatformMallocCrash"), STAT_GenericPlatformMallocCrashLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("GenericPlatformMallocCrash"), STAT_GenericPlatformMallocCrashPlatformLLM, STATGROUP_LLMPlatform);
DECLARE_LLM_MEMORY_STAT(TEXT("Engine Misc"), STAT_EngineMiscLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("TaskGraph Misc Tasks"), STAT_TaskGraphTasksMiscLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Audio"), STAT_AudioLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("AudioMisc"), STAT_AudioMiscLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("AudioSoundWaves"), STAT_AudioSoundWavesLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("AudioMixer"), STAT_AudioMixerLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("AudioPrecache"), STAT_AudioPrecacheLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("AudioDecompress"), STAT_AudioDecompressLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("AudioRealtimePrecache"), STAT_AudioRealtimePrecacheLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("AudioFullDecompress"), STAT_AudioFullDecompressLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("AudioVoiceChat"), STAT_AudioVoiceChatLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("FName"), STAT_FNameLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Networking"), STAT_NetworkingLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Meshes"), STAT_MeshesLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Stats"), STAT_StatsLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Shaders"), STAT_ShadersLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("PSO"), STAT_PSOLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Textures"), STAT_TexturesLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("TextureMetaData"), STAT_TextureMetaDataLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("VirtualTextureSystem"), STAT_VirtualTextureSystemLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Render Targets"), STAT_RenderTargetsLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("SceneRender"), STAT_SceneRenderLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("RHIMisc"), STAT_RHIMiscLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("PhysX TriMesh"), STAT_PhysXTriMeshLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("PhysX ConvexMesh"), STAT_PhysXConvexMeshLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("AsyncLoading"), STAT_AsyncLoadingLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("UObject"), STAT_UObjectLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Animation"), STAT_AnimationLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("StaticMesh"), STAT_StaticMeshLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Materials"), STAT_MaterialsLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Particles"), STAT_ParticlesLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Niagara"), STAT_NiagaraLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("GPUSort"), STAT_GPUSortLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("GC"), STAT_GCLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("UI"), STAT_UILLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("NavigationRecast"), STAT_NavigationRecastLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Physics"), STAT_PhysicsLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("PhysX"), STAT_PhysXLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("PhysXGeometry"), STAT_PhysXGeometryLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("PhysXLandscape"), STAT_PhysXLandscapeLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("PhysXTrimesh"), STAT_PhysXTrimeshLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("PhysXConvex"), STAT_PhysXConvexLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("PhysXAllocator"), STAT_PhysXAllocatorLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Chaos"), STAT_ChaosLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("ChaosGeometry"), STAT_ChaosGeometryLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("ChaosAcceleration"), STAT_ChaosAccelerationLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("ChaosParticles"), STAT_ChaosParticlesLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("ChaosLandscape"), STAT_ChaosLandscapeLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("ChaosTrimesh"), STAT_ChaosTrimeshLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("ChaosConvex"), STAT_ChaosConvexLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("EnginePreInit"), STAT_EnginePreInitLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("EngineInit"), STAT_EngineInitLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Rendering Thread"), STAT_RenderingThreadLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("LoadMap Misc"), STAT_LoadMapMiscLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("StreamingManager"), STAT_StreamingManagerLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Graphics"), STAT_GraphicsPlatformLLM, STATGROUP_LLMPlatform);
DECLARE_LLM_MEMORY_STAT(TEXT("FileSystem"), STAT_FileSystemLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Localization"), STAT_LocalizationLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("AssetRegistry"), STAT_AssetRegistryLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("ConfigSystem"), STAT_ConfigSystemLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("InitUObject"), STAT_InitUObjectLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("VideoRecording"), STAT_VideoRecordingLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Replays"), STAT_ReplaysLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("CsvProfiler"), STAT_CsvProfilerLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("MaterialInstance"), STAT_MaterialInstanceLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("SkeletalMesh"), STAT_SkeletalMeshLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("InstancedMesh"), STAT_InstancedMeshLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Landscape"), STAT_LandscapeLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("MediaStreaming"), STAT_MediaStreamingLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("ElectraPlayer"), STAT_ElectraPlayerLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("WMFPlayer"), STAT_WMFPlayerLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("MMIO"), STAT_PlatformMMIOLLM, STATGROUP_LLMPlatform);
DECLARE_LLM_MEMORY_STAT(TEXT("VirtualMemory"), STAT_PlatformVMLLM, STATGROUP_LLMPlatform);

/*
* LLM Summary stats referenced by ELLMTagNames
*/
DECLARE_LLM_MEMORY_STAT(TEXT("Total"), STAT_TrackedTotalSummaryLLM, STATGROUP_LLM);
DECLARE_LLM_MEMORY_STAT(TEXT("Audio"), STAT_AudioSummaryLLM, STATGROUP_LLM);
DECLARE_LLM_MEMORY_STAT(TEXT("Meshes"), STAT_MeshesSummaryLLM, STATGROUP_LLM);
DECLARE_LLM_MEMORY_STAT(TEXT("Physics"), STAT_PhysicsSummaryLLM, STATGROUP_LLM);
DECLARE_LLM_MEMORY_STAT(TEXT("PhysX"), STAT_PhysXSummaryLLM, STATGROUP_LLM);
DECLARE_LLM_MEMORY_STAT(TEXT("Chaos"), STAT_ChaosSummaryLLM, STATGROUP_LLM);
DECLARE_LLM_MEMORY_STAT(TEXT("UObject"), STAT_UObjectSummaryLLM, STATGROUP_LLM);
DECLARE_LLM_MEMORY_STAT(TEXT("Animation"), STAT_AnimationSummaryLLM, STATGROUP_LLM);
DECLARE_LLM_MEMORY_STAT(TEXT("StaticMesh"), STAT_StaticMeshSummaryLLM, STATGROUP_LLM);
DECLARE_LLM_MEMORY_STAT(TEXT("Materials"), STAT_MaterialsSummaryLLM, STATGROUP_LLM);
DECLARE_LLM_MEMORY_STAT(TEXT("Particles"), STAT_ParticlesSummaryLLM, STATGROUP_LLM);
DECLARE_LLM_MEMORY_STAT(TEXT("Niagara"), STAT_NiagaraSummaryLLM, STATGROUP_LLM);
DECLARE_LLM_MEMORY_STAT(TEXT("UI"), STAT_UISummaryLLM, STATGROUP_LLM);
DECLARE_LLM_MEMORY_STAT(TEXT("Navigation"), STAT_NavigationSummaryLLM, STATGROUP_LLM);
DECLARE_LLM_MEMORY_STAT(TEXT("Textures"), STAT_TexturesSummaryLLM, STATGROUP_LLM);
DECLARE_LLM_MEMORY_STAT(TEXT("MediaStreaming"), STAT_MediaStreamingSummaryLLM, STATGROUP_LLM);

extern const TCHAR* LLMGetTagName(ELLMTag Tag)
{
#define LLM_TAG_NAME_ARRAY(Enum,Str,Stat,Group,ParentTag) TEXT(Str),
	static TCHAR const* Names[] = { LLM_ENUM_GENERIC_TAGS(LLM_TAG_NAME_ARRAY) };
#undef LLM_TAG_NAME_ARRAY

	int32 Index = (int32)Tag;
	if (Index >= 0 && Index < UE_ARRAY_COUNT(Names))
	{
		return Names[Index];
	}
	else
	{
		return nullptr;
	}
}

extern const ANSICHAR* LLMGetTagNameANSI(ELLMTag Tag)
{
#define LLM_TAG_NAME_ARRAY(Enum,Str,Stat,Group,ParentTag) Str,
	static ANSICHAR const* Names[] = { LLM_ENUM_GENERIC_TAGS(LLM_TAG_NAME_ARRAY) };
#undef LLM_TAG_NAME_ARRAY

	int32 Index = (int32)Tag;
	if (Index >= 0 && Index < UE_ARRAY_COUNT(Names))
	{
		return Names[Index];
	}
	else
	{
		return nullptr;
	}
}

extern FName LLMGetTagStat(ELLMTag Tag)
{
#define LLM_TAG_STAT_ARRAY(Enum,Str,Stat,Group,ParentTag) Stat,
	static FName Names[] = { LLM_ENUM_GENERIC_TAGS(LLM_TAG_STAT_ARRAY) };
#undef LLM_TAG_STAT_ARRAY

	int32 Index = (int32)Tag;
	if (Index >= 0 && Index < UE_ARRAY_COUNT(Names))
	{
		return Names[Index];
	}
	else
	{
		return NAME_None;
	}
}

extern FName LLMGetTagStatGroup(ELLMTag Tag)
{
#define LLM_TAG_STATGROUP_ARRAY(Enum,Str,Stat,Group,ParentTag) Group,
	static FName Names[] = { LLM_ENUM_GENERIC_TAGS(LLM_TAG_STATGROUP_ARRAY) };
#undef LLM_TAG_STAT_ARRAY

	int32 Index = (int32)Tag;
	if (Index >= 0 && Index < UE_ARRAY_COUNT(Names))
	{
		return Names[Index];
	}
	else
	{
		return NAME_None;
	}
}

extern int32 LLMGetTagParent(ELLMTag Tag)
{
#define LLM_TAG_NAME_ARRAY(Enum,Str,Stat,Group,ParentTag) (int32)ParentTag,
	static const int32 ParentTags [] = { LLM_ENUM_GENERIC_TAGS(LLM_TAG_NAME_ARRAY) };
#undef LLM_TAG_NAME_ARRAY

	int32 Index = (int32)Tag;
	if( Index >= 0 && Index < UE_ARRAY_COUNT(ParentTags))
	{
		return ParentTags[Index];
	}
	else
	{
		return -1;
	}
}

#if DO_CHECK

bool LLMPrivate::HandleAssert(bool bLog, const TCHAR* Format, ...)
{
	if (bLog)
	{
		TCHAR DescriptionString[4096];
		GET_VARARGS(DescriptionString, UE_ARRAY_COUNT(DescriptionString), UE_ARRAY_COUNT(DescriptionString) - 1, Format, Format);

		FPlatformMisc::LowLevelOutputDebugString(DescriptionString);

		if (FPlatformMisc::IsDebuggerPresent())
			FPlatformMisc::PromptForRemoteDebugging(true);

		UE_DEBUG_BREAK();
	}
	return false;
}

#endif

/**
 * FLLMCsvWriter: class for writing out the LLM stats to a csv file every few seconds
 */
class FLLMCsvWriter
{
public:
	FLLMCsvWriter();

	~FLLMCsvWriter();

	void SetAllocator(FLLMAllocator* Allocator)
	{
		StatValues.SetAllocator(Allocator);
		StatValuesForWrite.SetAllocator(Allocator);
	}

	void SetTracker(ELLMTracker InTracker) { Tracker = InTracker; }

	void Clear();

#if LLM_TRACK_PEAK_MEMORY
	void AddStat(int64 Tag, int64 Value, int64 Peak);
	void SetStat(int64 Tag, int64 Value, int64 Peak);
#else
	void AddStat(int64 Tag, int64 Value);
	void SetStat(int64 Tag, int64 Value);
#endif

	void Update(FLLMCustomTag* CustomTags, const int32* ParentTags);

	void SetEnabled(bool value) { Enabled = value; }

private:
	void WriteGraph(FLLMCustomTag* CustomTags, const int32* ParentTags);

	void Write(const FString& Text);

	static FString GetTagName(int64 Tag, FLLMCustomTag* CustomTags, const int32* ParentTags);

	static const TCHAR* GetTrackerCsvName(ELLMTracker InTracker);

	struct StatValue
	{
		int64 Tag;
		int64 Value;
#if LLM_TRACK_PEAK_MEMORY
		int64 Peak;
#endif
	};

	bool Enabled;

	ELLMTracker Tracker;

	FLLMArray<StatValue> StatValues;
	FLLMArray<StatValue> StatValuesForWrite;

	int32 WriteCount;

	FCriticalSection StatValuesLock;

	double LastWriteTime;

	FArchive* Archive;

	int32 LastWriteStatValueCount;
};

/*
 * this is really the main LLM class. It owns the thread state objects.
 */
class FLLMTracker
{
public:

	FLLMTracker();
	~FLLMTracker();

	void Initialise(ELLMTracker Tracker, FLLMAllocator* InAllocator);

	void PushTag(int64 Tag);
	void PopTag();
#if LLM_ALLOW_ASSETS_TAGS
	void PushAssetTag(int64 Tag);
	void PopAssetTag();
#endif
	void TrackAllocation(const void* Ptr, uint64 Size, ELLMTag DefaultTag, ELLMTracker Tracker, ELLMAllocType AllocType);
	void TrackFree(const void* Ptr, ELLMTracker Tracker, ELLMAllocType AllocType);
	void OnAllocMoved(const void* Dest, const void* Source, uint64& OutSize, int64& OutTag);

	void TrackMemory(int64 Tag, int64 Amount);

	// This will pause/unpause tracking, and also manually increment a given tag
	void PauseAndTrackMemory(int64 Tag, int64 Amount, ELLMAllocType AllocType);
	void Pause(ELLMAllocType AllocType);
	void Unpause(ELLMAllocType AllocType);
	bool IsPaused(ELLMAllocType AllocType);
	
	void Clear();

	void SetCSVEnabled(bool Value);

	void WriteCsv(FLLMCustomTag* CustomTags, const int32* ParentTags);

#define LLM_USE_ALLOC_INFO_STRUCT (LLM_STAT_TAGS_ENABLED || LLM_ALLOW_ASSETS_TAGS)

#if LLM_USE_ALLOC_INFO_STRUCT
	struct FLowLevelAllocInfo
	{
		int64 Tag;
		
		#if LLM_ALLOW_ASSETS_TAGS
			int64 AssetTag;
		#endif
	};
#else
	typedef ELLMTag FLowLevelAllocInfo;
#endif

	typedef LLMMap<PointerKey, uint32, FLowLevelAllocInfo> LLMMap;	// pointer, size, info

	LLMMap& GetAllocationMap()
	{
		return AllocationMap;
	}

	void SetTotalTags(ELLMTag Untagged, ELLMTag Tracked);
	void Update(FLLMCustomTag* CustomTags, const int32* ParentTags);
	void UpdateTotals();

	int64 GetTagAmount(ELLMTag Tag) const;
	void SetTagAmount(ELLMTag Tag, int64 Amount, bool AddToTotal);
    int64 GetActiveTag();
	int64 FindTagForPtr( void* Ptr );

	int64 GetAllocTypeAmount(ELLMAllocType AllocType);

	int64 GetTrackedMemoryOverFrames() const
	{
		return TrackedMemoryOverFrames;
	}
    
protected:

	// per thread state
	class FLLMThreadState
	{
	public:
		FLLMThreadState();

		void SetAllocator(FLLMAllocator* InAllocator);

		void Clear();

		void PushTag(int64 Tag);
		void PopTag();
		int64 GetTopTag();
#if LLM_ALLOW_ASSETS_TAGS
		void PushAssetTag(int64 Tag);
		void PopAssetTag();
		int64 GetTopAssetTag();
#endif
		void TrackAllocation(const void* Ptr, uint64 Size, ELLMTag DefaultTag, ELLMTracker Tracker, ELLMAllocType AllocType);
		void TrackFree(const void* Ptr, int64 Tag, uint64 Size, bool bTrackedUntagged, ELLMTracker Tracker, ELLMAllocType AllocType);
		void IncrTag(int64 Tag, int64 Amount, bool bTrackUntagged);

		void GetFrameStatTotals(
			ELLMTag InUntaggedTotalTag,
			FLLMThreadState& InStateCopy,
			FLLMCsvWriter& CsvWriter,
			FLLMCustomTag* CustomTags,
			int64* EnumTagAmounts,
			int64* OutAllocTypeAmounts);

		void UpdateFrameStatGroups( FLLMCustomTag* CustomTags, const int32* ParentTags );

		static void IncMemoryStatByFName(FName Name, int64 Amount);

		void ClearAllocTypeAmounts();

		FLLMAllocator* Allocator;

		FCriticalSection TagSection;
		FLLMArray<int64> TagStack;
#if LLM_ALLOW_ASSETS_TAGS
		FLLMArray<int64> AssetTagStack;
#endif
		FLLMArray<int64> TaggedAllocs;
#if LLM_TRACK_PEAK_MEMORY
		FLLMArray<int64> TaggedAllocPeaks;
#endif
		FLLMArray<int64> TaggedAllocTags;
		int64 UntaggedAllocs;
#if LLM_TRACK_PEAK_MEMORY
		int64 UntaggedAllocsPeak;
#endif
		int8 PausedCounter[(int32)ELLMAllocType::Count];

		int64 AllocTypeAmounts[(int32)ELLMAllocType::Count];
	};

	FLLMThreadState* GetOrCreateState();
	FLLMThreadState* GetState();

	FLLMAllocator* Allocator;

	uint32 TlsSlot;

	FLLMObjectAllocator<FLLMThreadState> ThreadStateAllocator;
	FLLMArray<FLLMThreadState*> ThreadStates;

	FCriticalSection PendingThreadStatesGuard;
	FLLMArray<FLLMThreadState*> PendingThreadStates;

	int64 TrackedMemoryOverFrames GCC_ALIGN(8);

	LLMMap AllocationMap;

	ELLMTag UntaggedTotalTag;
	ELLMTag TrackedTotalTag;

	// a temporary used to take a copy of the thread state values in a thread safe way.
	FLLMThreadState StateCopy;

	FLLMCsvWriter CsvWriter;

	double LastTrimTime;

	int64 EnumTagAmounts[LLM_TAG_COUNT];

	int64 AllocTypeAmounts[(int32)ELLMAllocType::Count];
};

static int64 FNameToTag(FName Name)
{
	if (Name == NAME_None)
	{
		return (int64)ELLMTag::Untagged;
	}

	// get the bits out of the FName we need
	int64 NameIndex = Name.GetComparisonIndex().ToUnstableInt();
	int64 NameNumber = Name.GetNumber();
	int64 tag = (NameNumber << 32) | NameIndex;
	LLMCheckf(tag > LLM_TAG_COUNT, TEXT("Passed with a name index [%d - %s] that was less than MemTracker_MaxUserAllocation"), NameIndex, *Name.ToString());

	// convert it to a tag, but you can actually convert this to an FMinimalName in the debugger to view it - *((FMinimalName*)&Tag)
	return tag;
}

static FName TagToFName(int64 Tag)
{
	// pull the bits back out of the tag
	FNameEntryId NameIndex = FNameEntryId::FromUnstableInt((int32)(Tag & 0xFFFFFFFF));
	int32 NameNumber = (int32)(Tag >> 32);
	return FName(NameIndex, NameIndex, NameNumber);
}

FLowLevelMemTracker& FLowLevelMemTracker::Construct()
{
	static FLowLevelMemTracker Tracker;
	TrackerInstance = &Tracker;
	return Tracker;
}

bool FLowLevelMemTracker::IsEnabled()
{
	return !bIsDisabled;
}

FLowLevelMemTracker* FLowLevelMemTracker::TrackerInstance;
bool FLowLevelMemTracker::bIsDisabled; // must start off enabled because allocations happen before the command line enables/disables us

static const TCHAR* InvalidLLMTagName = TEXT("?");

FLowLevelMemTracker::FLowLevelMemTracker()
	: bFirstTimeUpdating(true)
	, bCanEnable(true)
	, bCsvWriterEnabled(false)
	, bInitialisedTrackers(false)
{
	// set the LLMMap alloc functions
	LLMAllocFunction PlatformLLMAlloc = NULL;
	LLMFreeFunction PlatformLLMFree = NULL;
	int32 Alignment = 0;
	if (!FPlatformMemory::GetLLMAllocFunctions(PlatformLLMAlloc, PlatformLLMFree, Alignment))
	{
		bIsDisabled = true;
		bCanEnable = false;
	}

	Allocator.Initialise(PlatformLLMAlloc, PlatformLLMFree, Alignment);

	// only None is on by default
	for (int32 Index = 0; Index < (int32)ELLMTagSet::Max; Index++)
	{
		ActiveSets[Index] = Index == (int32)ELLMTagSet::None;
	}

	for (int32 Index = 0; Index < LLM_CUSTOM_TAG_COUNT; Index++ )
	{
		CustomTags[Index].Name = InvalidLLMTagName;
	}

	for (int32 Index = 0; Index < LLM_TAG_COUNT; Index++ )
	{
		ParentTags[Index] = LLMGetTagParent((ELLMTag)Index);
	}
	for (int32 Index = 0; Index < LLM_TAG_COUNT; Index++ )
	{
		if (ParentTags[Index] != -1)
		{
			int32 GrandparentTag = ParentTags[ParentTags[Index]];
			LLMCheckf( GrandparentTag == -1, TEXT("can only have one level of tag parent") );
		}
	}
}

FLowLevelMemTracker::~FLowLevelMemTracker()
{
	bIsDisabled = true; // tracking must stop at this point or it will crash while tracking its own destruction
	for (int32 TrackerIndex = 0; TrackerIndex < (int32)ELLMTracker::Max; ++TrackerIndex)
	{
		Trackers[TrackerIndex]->~FLLMTracker();
		Allocator.Free(Trackers[TrackerIndex], sizeof(FLowLevelMemTracker));
	}
}

void FLowLevelMemTracker::InitialiseTrackers()
{
	for (int32 TrackerIndex = 0; TrackerIndex < (int32)ELLMTracker::Max; ++TrackerIndex)
	{
		FLLMTracker* Tracker = (FLLMTracker*)Allocator.Alloc(sizeof(FLLMTracker));
		new (Tracker)FLLMTracker();

		Trackers[TrackerIndex] = Tracker;

		Tracker->Initialise((ELLMTracker)TrackerIndex, &Allocator);
	}

	// calculate program size early on... the platform can call update the program size later if it sees fit
	InitialiseProgramSize();
}

void FLowLevelMemTracker::UpdateStatsPerFrame(const TCHAR* LogName)
{
	if (bIsDisabled && !bCanEnable)
		return;

	// let some stats get through even if we've disabled LLM - this shows up some overhead that is always there even when disabled
	// (unless the #define completely removes support, of course)
	if (bIsDisabled && !bFirstTimeUpdating)
	{
		return;
	}

	// delay init
	if (bFirstTimeUpdating)
	{
		static_assert((uint8)ELLMTracker::Max == 2, "You added a tracker, without updating FLowLevelMemTracker::UpdateStatsPerFrame (and probably need to update macros)");

		GetTracker(ELLMTracker::Platform)->SetTotalTags(ELLMTag::PlatformUntaggedTotal, ELLMTag::PlatformTrackedTotal);
		GetTracker(ELLMTracker::Default)->SetTotalTags(ELLMTag::UntaggedTotal, ELLMTag::TrackedTotal);

		bFirstTimeUpdating = false;

#if MEMPRO_ENABLED
		FMemProProfiler::PostInit();
#endif

	}

	// update the trackers
	for (int32 TrackerIndex = 0; TrackerIndex < (int32)ELLMTracker::Max; TrackerIndex++)
	{
		GetTracker((ELLMTracker)TrackerIndex)->Update(CustomTags,ParentTags);
	}

	// calculate FMalloc unused stat and set it in the Default tracker
	int64 FMallocAmount = Trackers[(int32)ELLMTracker::Default]->GetAllocTypeAmount(ELLMAllocType::FMalloc);
	int64 FMallocPlatformAmount = Trackers[(int32)ELLMTracker::Platform]->GetTagAmount(ELLMTag::FMalloc);
	int64 FMallocUnused = FMallocPlatformAmount - FMallocAmount;
	Trackers[(int32)ELLMTracker::Default]->SetTagAmount(ELLMTag::FMallocUnused, FMallocUnused, true);

	// update totals for all trackers
	for (int32 TrackerIndex = 0; TrackerIndex < (int32)ELLMTracker::Max; TrackerIndex++)
	{
		GetTracker((ELLMTracker)TrackerIndex)->UpdateTotals();
	}

	// set overhead stats
	int64 StaticOverhead = sizeof(FLowLevelMemTracker) + sizeof(FLLMTracker) * (int32)ELLMTracker::Max;
	int64 Overhead = StaticOverhead + Allocator.GetTotal();
	SET_MEMORY_STAT(STAT_LLMOverheadTotal, Overhead);

	// get the platform to update any custom tags
	FPlatformMemory::UpdateCustomLLMTags();

	// calculate memory the platform thinks we have allocated, compared to what we have tracked, including the program memory
	FPlatformMemoryStats PlatformStats = FPlatformMemory::GetStats();
#if PLATFORM_ANDROID || PLATFORM_IOS || WITH_SERVER_CODE
	uint64 PlatformProcessMemory = PlatformStats.UsedPhysical;
#else
	uint64 PlatformProcessMemory = PlatformStats.TotalPhysical - PlatformStats.AvailablePhysical;
#endif
	int64 PlatformTrackedTotal = GetTracker(ELLMTracker::Platform)->GetTagAmount(ELLMTag::PlatformTrackedTotal);
	int64 PlatformTotalUntracked = PlatformProcessMemory - PlatformTrackedTotal;

	GetTracker(ELLMTracker::Platform)->SetTagAmount(ELLMTag::PlatformTotal, PlatformProcessMemory, false);
	GetTracker(ELLMTracker::Platform)->SetTagAmount(ELLMTag::PlatformUntracked, PlatformTotalUntracked, false);
	GetTracker(ELLMTracker::Platform)->SetTagAmount(ELLMTag::PlatformOverhead, Overhead, true);

	int64 TrackedTotal = GetTracker(ELLMTracker::Default)->GetTagAmount(ELLMTag::TrackedTotal);
	// remove the Overhead from the default LLM as it's not something anyone needs to investigate when finding what to reduce
	// the platform LLM will have the info 
	GetTracker(ELLMTracker::Default)->SetTagAmount(ELLMTag::Total, PlatformProcessMemory - Overhead, false);
	GetTracker(ELLMTracker::Default)->SetTagAmount(ELLMTag::Untracked, PlatformProcessMemory - (TrackedTotal + Overhead), false);

#if PLATFORM_WINDOWS
	GetTracker(ELLMTracker::Default)->SetTagAmount(ELLMTag::WorkingSetSize, PlatformStats.UsedPhysical, false);
	GetTracker(ELLMTracker::Default)->SetTagAmount(ELLMTag::PagefileUsed, PlatformStats.UsedVirtual, false);
#endif

	if (bCsvWriterEnabled)
	{
		GetTracker(ELLMTracker::Default)->WriteCsv(CustomTags,ParentTags);
		GetTracker(ELLMTracker::Platform)->WriteCsv(CustomTags,ParentTags);
	}

	if (LogName != nullptr)
	{
		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("---> Untracked memory at %s = %.2f mb\n"), LogName, (double)PlatformTotalUntracked / (1024.0 * 1024.0));
	}
}

void FLowLevelMemTracker::InitialiseProgramSize()
{
	if (!ProgramSize)
	{
		FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
		ProgramSize = Stats.TotalPhysical - Stats.AvailablePhysical;

		Trackers[(int32)ELLMTracker::Platform]->TrackMemory((uint64)ELLMTag::ProgramSizePlatform, ProgramSize);
		Trackers[(int32)ELLMTracker::Default]->TrackMemory((uint64)ELLMTag::ProgramSize, ProgramSize);
	}
}

void FLowLevelMemTracker::SetProgramSize(uint64 InProgramSize)
{
	if (bIsDisabled)
		return;

	int64 ProgramSizeDiff = InProgramSize - ProgramSize;

	ProgramSize = InProgramSize;

	GetTracker(ELLMTracker::Platform)->TrackMemory((uint64)ELLMTag::ProgramSizePlatform, ProgramSizeDiff);
	GetTracker(ELLMTracker::Default)->TrackMemory((uint64)ELLMTag::ProgramSize, ProgramSizeDiff);
}

void FLowLevelMemTracker::ProcessCommandLine(const TCHAR* CmdLine)
{
	if (bIsDisabled && !bCanEnable)
		return;

	if (bCanEnable)
	{
#if LLM_AUTO_ENABLE
		// LLM is always on, regardless of command line
		bIsDisabled = false;
#elif LLM_COMMANDLINE_ENABLES_FUNCTIONALITY
		// if we require commandline to enable it, then we are disabled if it's not there
		bIsDisabled = FParse::Param(CmdLine, TEXT("LLM")) == false;
#else
		// if we allow commandline to disable us, then we are disabled if it's there
		bIsDisabled = FParse::Param(CmdLine, TEXT("NOLLM")) == true;
#endif
	}

	bCsvWriterEnabled = FParse::Param(CmdLine, TEXT("LLMCSV"));
	for (int32 TrackerIndex = 0; TrackerIndex < (int32)ELLMTracker::Max; ++TrackerIndex)
	{
		GetTracker((ELLMTracker)TrackerIndex)->SetCSVEnabled(bCsvWriterEnabled);
	}

	// automatically enable LLM if only LLMCSV is there
	if (bCsvWriterEnabled && bIsDisabled && bCanEnable)
	{
		bIsDisabled = false;
	}

	if (bIsDisabled)
	{
		for (int32 TrackerIndex = 0; TrackerIndex < (int32)ELLMTracker::Max; TrackerIndex++)
		{
			GetTracker((ELLMTracker)TrackerIndex)->Clear();
		}
	}

	// activate tag sets (we ignore None set, it's always on)
	FString SetList;
	static_assert((uint8)ELLMTagSet::Max == 3, "You added a tagset, without updating FLowLevelMemTracker::ProcessCommandLine");
	if (FParse::Value(CmdLine, TEXT("LLMTAGSETS="), SetList))
	{
		TArray<FString> Sets;
		SetList.ParseIntoArray(Sets, TEXT(","), true);
		for (FString& Set : Sets)
		{
			if (Set == TEXT("Assets"))
			{
#if LLM_ALLOW_ASSETS_TAGS // asset tracking has a per-thread memory overhead, so we have a #define to completely disable it - warn if we don't match
				ActiveSets[(int32)ELLMTagSet::Assets] = true;
#else
				UE_LOG(LogInit, Warning, TEXT("Attempted to use LLM to track assets, but LLM_ALLOW_ASSETS_TAGS is not defined to 1. You will need to enable the define"));
#endif
			}
			else if (Set == TEXT("AssetClasses"))
			{
				ActiveSets[(int32)ELLMTagSet::AssetClasses] = true;
			}
		}
	}
}

// Return the total amount of memory being tracked
uint64 FLowLevelMemTracker::GetTotalTrackedMemory(ELLMTracker Tracker)
{
	if (bIsDisabled)
	{
		return 0;
	}

	return GetTracker(Tracker)->GetTrackedMemoryOverFrames();
}

void FLowLevelMemTracker::OnLowLevelAlloc(ELLMTracker Tracker, const void* Ptr, uint64 Size, ELLMTag DefaultTag, ELLMAllocType AllocType)
{
	if (bIsDisabled)
	{
		return;
	}

	GetTracker(Tracker)->TrackAllocation(Ptr, Size, DefaultTag, Tracker, AllocType);
}

void FLowLevelMemTracker::OnLowLevelFree(ELLMTracker Tracker, const void* Ptr, ELLMAllocType AllocType)
{
	if (bIsDisabled)
	{
		return;
	}

	if (Ptr != nullptr)
	{
		GetTracker(Tracker)->TrackFree(Ptr, Tracker, AllocType);
	}
}

FLLMTracker* FLowLevelMemTracker::GetTracker(ELLMTracker Tracker)
{
	if (!bInitialisedTrackers)
	{
		InitialiseTrackers();
		bInitialisedTrackers = true;
	}

	return Trackers[(int32)Tracker];
}

void FLowLevelMemTracker::OnLowLevelAllocMoved(ELLMTracker Tracker, const void* Dest, const void* Source)
{
	if (bIsDisabled || IsEngineExitRequested())
	{
		return;
	}

	//update the allocation map
	uint64 Size;
	int64 Tag;
	GetTracker(Tracker)->OnAllocMoved(Dest, Source, Size, Tag);

	// update external memory trackers (ideally would want a proper 'move' option on these)
	if (Tracker == ELLMTracker::Default)
	{
		FPlatformMemory::OnLowLevelMemory_Free(Source, Size, Tag);
		FPlatformMemory::OnLowLevelMemory_Alloc(Dest, Size, Tag);
	}

#if MEMPRO_ENABLED
	if (FMemProProfiler::IsTrackingTag( (ELLMTag)Tag) )
	{
		MEMPRO_TRACK_FREE((void*)Source);
		MEMPRO_TRACK_ALLOC((void*)Dest, (size_t)Size);
	}
#endif

}

bool FLowLevelMemTracker::Exec(const TCHAR* Cmd, FOutputDevice& Ar)
{
	if (FParse::Command(&Cmd, TEXT("LLMEM")))
	{
		if (FParse::Command(&Cmd, TEXT("SPAMALLOC")))
		{
			int32 NumAllocs = 128;
			int64 MaxSize = FCString::Atoi(Cmd);
			if (MaxSize == 0)
			{
				MaxSize = 128 * 1024;
			}

			UpdateStatsPerFrame(TEXT("Before spam"));
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("----> Spamming %d allocations, from %d..%d bytes\n"), NumAllocs, MaxSize/2, MaxSize);

			TArray<void*> Spam;
			Spam.Reserve(NumAllocs);
			SIZE_T TotalSize = 0;
			for (int32 Index = 0; Index < NumAllocs; Index++)
			{
				SIZE_T Size = (FPlatformMath::Rand() % MaxSize / 2) + MaxSize / 2;
				TotalSize += Size;
				Spam.Add(FMemory::Malloc(Size));
			}
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("----> Allocated %d total bytes\n"), TotalSize);

			UpdateStatsPerFrame(TEXT("After spam"));

			for (int32 Index = 0; Index < Spam.Num(); Index++)
			{
				FMemory::Free(Spam[Index]);
			}

			UpdateStatsPerFrame(TEXT("After cleanup"));
		}
		return true;
	}

	return false;
}

bool FLowLevelMemTracker::IsTagSetActive(ELLMTagSet Set)
{
	return !bIsDisabled && ActiveSets[(int32)Set];
}

bool FLowLevelMemTracker::ShouldReduceThreads()
{
	return IsTagSetActive(ELLMTagSet::Assets) || IsTagSetActive(ELLMTagSet::AssetClasses);
}

static bool IsAssetTagForAssets(ELLMTagSet Set)
{
	return Set == ELLMTagSet::Assets || Set == ELLMTagSet::AssetClasses;
}

void FLowLevelMemTracker::RegisterCustomTagInternal(int32 Tag, const TCHAR* Name, FName StatName, FName SummaryStatName, int32 ParentTag)
{
	LLMCheckf(Tag >= LLM_CUSTOM_TAG_START && Tag <= LLM_CUSTOM_TAG_END, TEXT("Tag %d out of range"), Tag);
	LLMCheckf(Name != nullptr, TEXT("Tag %d has no name"), Tag);
	FLLMCustomTag& PlatformTag = CustomTags[Tag - LLM_CUSTOM_TAG_START];
	PlatformTag.Tag = Tag;
	PlatformTag.Name = Name ? Name : InvalidLLMTagName;
	PlatformTag.StatName = StatName;
	PlatformTag.SummaryStatName = SummaryStatName;
	ParentTags[Tag] = ParentTag;
	if (ParentTag != -1)
	{
		int32 GrandparentTag = ParentTags[ParentTag];
		LLMCheckf( GrandparentTag == -1, TEXT("can only have one level of tag parent") );
	}
}

void FLowLevelMemTracker::RegisterPlatformTag(int32 Tag, const TCHAR* Name, FName StatName, FName SummaryStatName, int32 ParentTag)
{
	LLMCheck(Tag >= (int32)ELLMTag::PlatformTagStart && Tag <= (int32)ELLMTag::PlatformTagEnd);
	RegisterCustomTagInternal( Tag, Name, StatName, SummaryStatName, ParentTag );
}

void FLowLevelMemTracker::RegisterProjectTag(int32 Tag, const TCHAR* Name, FName StatName, FName SummaryStatName, int32 ParentTag)
{
	LLMCheck(Tag >= (int32)ELLMTag::ProjectTagStart && Tag <= (int32)ELLMTag::ProjectTagEnd);
	RegisterCustomTagInternal( Tag, Name, StatName, SummaryStatName, ParentTag );
}


bool FLowLevelMemTracker::FindTagByName( const TCHAR* Name, uint64& OutTag ) const
{
	if( Name != nullptr )
	{
		for ( int32 GenericTagIndex = 0; GenericTagIndex < (int32)ELLMTag::GenericTagCount; GenericTagIndex++ )
		{
			if( LLMGetTagName((ELLMTag)GenericTagIndex) != nullptr && FCString::Stricmp( Name, LLMGetTagName((ELLMTag)GenericTagIndex) ) == 0 )
			{
				OutTag = GenericTagIndex;
				return true;
			}
		}
		for ( int32 PlatformTagIndex = 0; PlatformTagIndex < LLM_CUSTOM_TAG_COUNT; PlatformTagIndex++ )
		{
			if( CustomTags[PlatformTagIndex].Name != nullptr && CustomTags[PlatformTagIndex].Name != InvalidLLMTagName && FCString::Stricmp( Name, CustomTags[PlatformTagIndex].Name ) == 0 )
			{
				OutTag = LLM_CUSTOM_TAG_START + PlatformTagIndex;
				return true;
			}
		}
	}

	return false;
}

const TCHAR* FLowLevelMemTracker::FindTagName(uint64 Tag) const
{
	const TCHAR* Result = nullptr;

	if( Tag >= 0 && Tag < (int32)ELLMTag::GenericTagCount )
	{
		Result = LLMGetTagName((ELLMTag)Tag);
	}
	else if( Tag >= LLM_CUSTOM_TAG_START && Tag <= LLM_CUSTOM_TAG_END )
	{
		Result = CustomTags[Tag - LLM_CUSTOM_TAG_START].Name;
	}

	return Result;
}

int64 FLowLevelMemTracker::GetTagAmountForTracker(ELLMTracker Tracker, ELLMTag Tag)
{
	return GetTracker(Tracker)->GetTagAmount(Tag);
}

void FLowLevelMemTracker::SetTagAmountForTracker(ELLMTracker Tracker, ELLMTag Tag, int64 Amount, bool bAddToTotal )
{
	GetTracker(Tracker)->SetTagAmount( Tag, Amount, bAddToTotal );
}

int64 FLowLevelMemTracker::GetActiveTag(ELLMTracker Tracker)
{
    return GetTracker(Tracker)->GetActiveTag();
}

int64 FLowLevelMemTracker::DumpTag( ELLMTracker Tracker, const char* FileName, int LineNumber )
{
	int64 Tag = GetActiveTag(Tracker);
	const TCHAR* TagName = FindTagName(Tag);

	FPlatformMisc::LowLevelOutputDebugStringf( TEXT("LLM TAG: %s (%lld) @ %s:%d\n"), TagName ? TagName : TEXT("<unknown>"), Tag, FileName ? ANSI_TO_TCHAR(FileName) : TEXT("?"), LineNumber );

	return Tag;
}




FLLMScope::FLLMScope(FName StatIDName, ELLMTagSet Set, ELLMTracker Tracker)
{
	Init(FNameToTag(StatIDName), Set, Tracker);
}

FLLMScope::FLLMScope(ELLMTag Tag, ELLMTagSet Set, ELLMTracker Tracker)
{
	Init((int64)Tag, Set, Tracker);
}

void FLLMScope::Init(int64 Tag, ELLMTagSet Set, ELLMTracker Tracker)
{
	TagSet = Set;
	TrackerSet = Tracker;
	Enabled = Tag != (int64)ELLMTag::Untagged && !IsEngineExitRequested();

	// early out if tracking is disabled (don't do the singleton call, this is called a lot!)
	if (!Enabled)
	{
		return;
	}

	FLowLevelMemTracker& LLM = FLowLevelMemTracker::Get();
	if (!LLM.IsTagSetActive(TagSet))
	{
		return;
	}

#if LLM_ALLOW_ASSETS_TAGS
	if (IsAssetTagForAssets(TagSet))
	{
		LLM.GetTracker(Tracker)->PushAssetTag(Tag);
	}
	else
#endif
	{
		LLM.GetTracker(Tracker)->PushTag(Tag);
	}
}

FLLMScope::~FLLMScope()
{
	// early out if tracking is disabled (don't do the singleton call, this is called a lot!)
	if (!Enabled)
	{
		return;
	}

	FLowLevelMemTracker& LLM = FLowLevelMemTracker::Get();
	if (!LLM.IsTagSetActive(TagSet))
	{
		return;
	}

#if LLM_ALLOW_ASSETS_TAGS
	if (IsAssetTagForAssets(TagSet))
	{
		LLM.GetTracker(TrackerSet)->PopAssetTag();
	}
	else
#endif
	{
		LLM.GetTracker(TrackerSet)->PopTag();
	}
}


FLLMPauseScope::FLLMPauseScope(FName StatIDName, int64 Amount, ELLMTracker TrackerToPause, ELLMAllocType InAllocType)
	: PausedTracker(TrackerToPause)
	, AllocType(InAllocType)
{
	Init(FNameToTag(StatIDName), Amount, TrackerToPause, AllocType);
}

FLLMPauseScope::FLLMPauseScope(ELLMTag Tag, int64 Amount, ELLMTracker TrackerToPause, ELLMAllocType InAllocType)
	: PausedTracker(TrackerToPause)
	, AllocType(InAllocType)
{
	Init((uint64)Tag, Amount, TrackerToPause, InAllocType);
}

void FLLMPauseScope::Init(int64 Tag, int64 Amount, ELLMTracker TrackerToPause, ELLMAllocType InAllocType)
{
	FLowLevelMemTracker& LLM = FLowLevelMemTracker::Get();
	if (!LLM.IsTagSetActive(ELLMTagSet::None))
	{
		return;
	}

	for (int32 TrackerIndex = 0; TrackerIndex < (int32)ELLMTracker::Max; TrackerIndex++)
	{
		ELLMTracker Tracker = (ELLMTracker)TrackerIndex;

		if (TrackerToPause == ELLMTracker::Max || TrackerToPause == Tracker)
		{
			if (Amount == 0)
			{
				LLM.GetTracker(Tracker)->Pause(InAllocType);
			}
			else
			{
				LLM.GetTracker(Tracker)->PauseAndTrackMemory(Tag, Amount, InAllocType);
			}
		}
	}
}

FLLMPauseScope::~FLLMPauseScope()
{
	FLowLevelMemTracker& LLM = FLowLevelMemTracker::Get();
	if (!LLM.IsTagSetActive(ELLMTagSet::None))
	{
		return;
	}

	for (int32 TrackerIndex = 0; TrackerIndex < (int32)ELLMTracker::Max; TrackerIndex++)
	{
		ELLMTracker Tracker = (ELLMTracker)TrackerIndex;

		if (PausedTracker == ELLMTracker::Max || Tracker == PausedTracker)
		{
			LLM.GetTracker(Tracker)->Unpause(AllocType);
		}
	}
}


FLLMScopeFromPtr::FLLMScopeFromPtr(void* Ptr, ELLMTracker Tracker )
	: TrackerSet(Tracker)
	, Enabled(false)
{
	if(IsEngineExitRequested() || Ptr == nullptr)
	{
		return;
	}

	FLowLevelMemTracker& LLM = FLowLevelMemTracker::Get();
	if (!LLM.IsEnabled())
	{
		return;
	}

	int64 Tag = LLM.GetTracker(TrackerSet)->FindTagForPtr( Ptr );
	if( Tag != (int64)ELLMTag::Untagged )
	{
		LLM.GetTracker(TrackerSet)->PushTag(Tag);
		Enabled = true;
	}
}

FLLMScopeFromPtr::~FLLMScopeFromPtr()
{
	if (!Enabled)
	{
		return;
	}

	FLowLevelMemTracker& LLM = FLowLevelMemTracker::Get();
	if (!LLM.IsEnabled())
	{
		return;
	}

	LLM.GetTracker(TrackerSet)->PopTag();
}






FLLMTracker::FLLMTracker()
	: TrackedMemoryOverFrames(0)
	, UntaggedTotalTag(ELLMTag::Untagged)
	, TrackedTotalTag(ELLMTag::Untagged)
	, LastTrimTime(0.0)

{
	TlsSlot = FPlatformTLS::AllocTlsSlot();

	for (int32 Index = 0; Index < LLM_TAG_COUNT; ++Index)
	{
		EnumTagAmounts[Index] = 0;
	}

	for (int32 Index = 0; Index < (int32)ELLMAllocType::Count; ++Index)
	{
		AllocTypeAmounts[Index] = 0;
	}
}

FLLMTracker::~FLLMTracker()
{
	Clear();

	FPlatformTLS::FreeTlsSlot(TlsSlot);
}

void FLLMTracker::Initialise(
	ELLMTracker Tracker,
	FLLMAllocator* InAllocator)
{
	CsvWriter.SetTracker(Tracker);

	Allocator = InAllocator;

	AllocationMap.SetAllocator(InAllocator);

	StateCopy.SetAllocator(InAllocator);

	CsvWriter.SetAllocator(InAllocator);

	ThreadStateAllocator.SetAllocator(Allocator);
	ThreadStates.SetAllocator(Allocator);
	PendingThreadStates.SetAllocator(Allocator);
}

FLLMTracker::FLLMThreadState* FLLMTracker::GetOrCreateState()
{
	// look for already allocated thread state
	FLLMThreadState* State = (FLLMThreadState*)FPlatformTLS::GetTlsValue(TlsSlot);
	// get one if needed
	if (State == nullptr)
	{
		State = ThreadStateAllocator.New();
		State->SetAllocator(Allocator);

		// Add to pending thread states, these will be consumed on the GT
		{
			FScopeLock Lock(&PendingThreadStatesGuard);
			PendingThreadStates.Add(State);
		}

		// push to Tls
		FPlatformTLS::SetTlsValue(TlsSlot, State);
	}
	return State;
}

FLLMTracker::FLLMThreadState* FLLMTracker::GetState()
{
	return (FLLMThreadState*)FPlatformTLS::GetTlsValue(TlsSlot);
}

void FLLMTracker::PushTag(int64 Tag)
{
	// pass along to the state object
	GetOrCreateState()->PushTag(Tag);
}

void FLLMTracker::PopTag()
{
	// look for already allocated thread state
	FLLMThreadState* State = GetState();

	LLMCheckf(State != nullptr, TEXT("Called PopTag but PushTag was never called!"));

	State->PopTag();
}

#if LLM_ALLOW_ASSETS_TAGS
void FLLMTracker::PushAssetTag(int64 Tag)
{
	// pass along to the state object
	GetOrCreateState()->PushAssetTag(Tag);
}

void FLLMTracker::PopAssetTag()
{
	// look for already allocated thread state
	FLLMThreadState* State = GetState();

	LLMCheckf(State != nullptr, TEXT("Called PopTag but PushTag was never called!"));

	State->PopAssetTag();
}
#endif

void FLLMTracker::TrackAllocation(const void* Ptr, uint64 Size, ELLMTag DefaultTag, ELLMTracker Tracker, ELLMAllocType AllocType)
{
	if (IsPaused(AllocType))
	{
		return;
	}

	// track the total quickly
	FPlatformAtomics::InterlockedAdd(&TrackedMemoryOverFrames, (int64)Size);
	
	FLLMThreadState* State = GetOrCreateState();
	
	// track on the thread state, and get the tag
	State->TrackAllocation(Ptr, Size, DefaultTag, Tracker, AllocType);

	// tracking a nullptr with a Size is allowed, but we don't need to remember it, since we can't free it ever
	if (Ptr != nullptr)
	{
		// remember the size and tag info
		int64 Tag = State->GetTopTag();
		if (Tag == (int64)ELLMTag::Untagged)
		{
			Tag = (int64)DefaultTag;
		}

		FLLMTracker::FLowLevelAllocInfo AllocInfo;
		#if LLM_USE_ALLOC_INFO_STRUCT
		AllocInfo.Tag = Tag;
			#if LLM_ALLOW_ASSETS_TAGS
		AllocInfo.AssetTag = State->GetTopAssetTag();
			#endif
		#else
		LLMCheck(Tag >= 0 && Tag < (int64)LLM_TAG_COUNT);
		AllocInfo = (ELLMTag)Tag;
		#endif

		LLMCheck(Size <= 0xffffffffu);
		GetAllocationMap().Add(Ptr, (uint32)Size, AllocInfo);
	}
}

void FLLMTracker::TrackFree(const void* Ptr, ELLMTracker Tracker, ELLMAllocType AllocType)
{
	if (IsPaused(AllocType))
	{
		return;
	}

	// look up the pointer in the tracking map
	if (!GetAllocationMap().HasKey(Ptr))
	{
		return;
	}
	LLMMap::Values Values = GetAllocationMap().Remove(Ptr);
	uint64 Size = Values.Value1;
	FLLMTracker::FLowLevelAllocInfo AllocInfo = Values.Value2;

	// track the total quickly
	FPlatformAtomics::InterlockedAdd(&TrackedMemoryOverFrames, 0 - Size);

	FLLMThreadState* State = GetOrCreateState();

#if LLM_USE_ALLOC_INFO_STRUCT
	State->TrackFree(Ptr, AllocInfo.Tag, Size, true, Tracker, AllocType);
	#if LLM_ALLOW_ASSETS_TAGS
		State->IncrTag(AllocInfo.AssetTag, 0 - Size, false);
	#endif
#else
	State->TrackFree(Ptr, (int64)AllocInfo, Size, true, Tracker, AllocType);
#endif
}

void FLLMTracker::OnAllocMoved(const void* Dest, const void* Source, uint64& OutSize, int64& OutTag)
{
	LLMMap::Values Values = GetAllocationMap().Remove(Source);
	GetAllocationMap().Add(Dest, Values.Value1, Values.Value2);


	const FLLMTracker::FLowLevelAllocInfo& AllocInfo = Values.Value2;
#if LLM_USE_ALLOC_INFO_STRUCT
	OutTag = AllocInfo.Tag;
#else
	OutTag = (int64)AllocInfo;
#endif

	OutSize = Values.Value1;
}

void FLLMTracker::TrackMemory(int64 Tag, int64 Amount)
{
	FLLMTracker::FLLMThreadState* State = GetOrCreateState();
	FScopeLock Lock(&State->TagSection);
	State->IncrTag(Tag, Amount, true);
	FPlatformAtomics::InterlockedAdd(&TrackedMemoryOverFrames, Amount);
}

// This will pause/unpause tracking, and also manually increment a given tag
void FLLMTracker::PauseAndTrackMemory(int64 Tag, int64 Amount, ELLMAllocType AllocType)
{
	FLLMTracker::FLLMThreadState* State = GetOrCreateState();
	FScopeLock Lock(&State->TagSection);
	State->PausedCounter[(int32)AllocType]++;
	State->IncrTag(Tag, Amount, true);
	FPlatformAtomics::InterlockedAdd(&TrackedMemoryOverFrames, Amount);
}

void FLLMTracker::Pause(ELLMAllocType AllocType)
{
	FLLMTracker::FLLMThreadState* State = GetOrCreateState();
	State->PausedCounter[(int32)AllocType]++;
}

void FLLMTracker::Unpause(ELLMAllocType AllocType)
{
	FLLMTracker::FLLMThreadState* State = GetOrCreateState();
	State->PausedCounter[(int32)AllocType]--;
	LLMCheck( State->PausedCounter[(int32)AllocType] >= 0 );
}

bool FLLMTracker::IsPaused(ELLMAllocType AllocType)
{
	FLLMTracker::FLLMThreadState* State = GetState();
	// pause during shutdown, as the massive number of frees is likely to overflow some of the buffers
	return IsEngineExitRequested() || (State == nullptr ? false : (State->PausedCounter[(int32)ELLMAllocType::None]>0) || (State->PausedCounter[(int32)AllocType])>0);
}

void FLLMTracker::Clear()
{
	{
		FScopeLock Lock(&PendingThreadStatesGuard);
		for (uint32 Index = 0; Index < PendingThreadStates.Num(); ++Index)
			ThreadStateAllocator.Delete(PendingThreadStates[Index]);
		PendingThreadStates.Clear(true);
	}

	for (uint32 Index = 0; Index < ThreadStates.Num(); ++Index)
		ThreadStateAllocator.Delete(ThreadStates[Index]);
	ThreadStates.Clear(true);

	AllocationMap.Clear();
	CsvWriter.Clear();
	ThreadStateAllocator.Clear();
}

void FLLMTracker::SetCSVEnabled(bool Value)
{
	CsvWriter.SetEnabled(Value);
}

void FLLMTracker::SetTotalTags(ELLMTag InUntaggedTotalTag, ELLMTag InTrackedTotalTag)
{
	UntaggedTotalTag = InUntaggedTotalTag;
	TrackedTotalTag = InTrackedTotalTag;
}

void FLLMTracker::Update(FLLMCustomTag* CustomTags, const int32* ParentTags)
{
	int ThreadStateNum = ThreadStates.Num();

	// Consume pending thread states
	// We must be careful to do all allocations outside of the PendingThreadStatesGuard guard as that can lead to a deadlock due to contention with PendingThreadStatesGuard & Locks inside the underlying allocator (i.e. MallocBinned2 -> Mutex)
	{
		PendingThreadStatesGuard.Lock();
		const int NumPendingThreadStatesToConsume = PendingThreadStates.Num();
		if (NumPendingThreadStatesToConsume > 0 )
		{
			PendingThreadStatesGuard.Unlock();
			ThreadStates.Reserve(ThreadStateNum + NumPendingThreadStatesToConsume);
			PendingThreadStatesGuard.Lock();

			for ( int32 i=0; i < NumPendingThreadStatesToConsume; ++i )
			{
				ThreadStates.Add(PendingThreadStates.RemoveLast());
			}
			ThreadStateNum += NumPendingThreadStatesToConsume;
		}
		PendingThreadStatesGuard.Unlock();
	}

	// accumulate the totals for each thread
	for (int32 ThreadIndex = 0; ThreadIndex < ThreadStateNum; ThreadIndex++)
	{
		ThreadStates[ThreadIndex]->UpdateFrameStatGroups(CustomTags,ParentTags);
		ThreadStates[ThreadIndex]->GetFrameStatTotals(UntaggedTotalTag, StateCopy, CsvWriter, CustomTags, EnumTagAmounts, AllocTypeAmounts);
	}

	EnumTagAmounts[(int32)TrackedTotalTag] = TrackedMemoryOverFrames;
}

void FLLMTracker::UpdateTotals()
{
	FName StatName = LLMGetTagStat(TrackedTotalTag);
	if (StatName != NAME_None)
	{
		SET_MEMORY_STAT_FName(StatName, TrackedMemoryOverFrames);
	}

	FName SummaryStatName = LLMGetTagStatGroup(TrackedTotalTag);
	if (SummaryStatName != NAME_None)
	{
		SET_MEMORY_STAT_FName(SummaryStatName, TrackedMemoryOverFrames);
	}

#if LLM_TRACK_PEAK_MEMORY
	// @todo we should be keeping track of the intra-frame memory peak for the total tracked memory.
	// For now we will just use the memory at the time the update happens since there are threading implications to being accurate.
	CsvWriter.SetStat(FNameToTag(TrackedTotalTag), TrackedMemoryOverFrames, TrackedMemoryOverFrames);
#else
	CsvWriter.SetStat((int64)TrackedTotalTag, TrackedMemoryOverFrames);
#endif

	if (FPlatformTime::Seconds() - LastTrimTime > 10)
	{
		AllocationMap.Trim();
		LastTrimTime = FPlatformTime::Seconds();
	}
}

void FLLMTracker::WriteCsv(FLLMCustomTag* CustomTags, const int32* ParentTags)
{
	CsvWriter.Update(CustomTags,ParentTags);
}

int64 FLLMTracker::GetActiveTag()
{
    FLLMThreadState* State = GetOrCreateState();
    return State->GetTopTag();
}

int64 FLLMTracker::FindTagForPtr( void* Ptr )
{
#if LLM_USE_ALLOC_INFO_STRUCT
	return GetAllocationMap().GetValue(Ptr).Value2.Tag;
#else
	return (int64)GetAllocationMap().GetValue(Ptr).Value2;
#endif
}


FLLMTracker::FLLMThreadState::FLLMThreadState()
	: UntaggedAllocs(0)
{
	for (int32 Index = 0; Index < (int32)ELLMAllocType::Count; ++Index)
	{
		PausedCounter[Index] = 0;
	}

	ClearAllocTypeAmounts();
}

void FLLMTracker::FLLMThreadState::SetAllocator(FLLMAllocator* InAllocator)
{
	TagStack.SetAllocator(InAllocator);

#if LLM_ALLOW_ASSETS_TAGS
	AssetTagStack.SetAllocator(InAllocator);
#endif

	TaggedAllocs.SetAllocator(InAllocator);
#if LLM_TRACK_PEAK_MEMORY
	TaggedAllocPeaks.SetAllocator(InAllocator);
#endif
	TaggedAllocTags.SetAllocator(InAllocator);
}

void FLLMTracker::FLLMThreadState::Clear()
{
	TagStack.Clear();

#if LLM_ALLOW_ASSETS_TAGS
	AssetTagStack.Clear();
#endif

	TaggedAllocs.Clear();
#if LLM_TRACK_PEAK_MEMORY
	TaggedAllocPeaks.Clear();
#endif
	TaggedAllocTags.Clear();

	ClearAllocTypeAmounts();
}

void FLLMTracker::FLLMThreadState::PushTag(int64 Tag)
{
	FScopeLock Lock(&TagSection);

	// push a tag
	TagStack.Add(Tag);
}

void FLLMTracker::FLLMThreadState::PopTag()
{
	FScopeLock Lock(&TagSection);

	LLMCheckf(TagStack.Num() > 0, TEXT("Called FLLMTracker::FLLMThreadState::PopTag without a matching Push (stack was empty on pop)"));
	TagStack.RemoveLast();
}

int64 FLLMTracker::FLLMThreadState::GetTopTag()
{
	// make sure we have some pushed
	if (TagStack.Num() == 0)
	{
		return (int64)ELLMTag::Untagged;
	}

	// return the top tag
	return TagStack.GetLast();
}

#if LLM_ALLOW_ASSETS_TAGS
void FLLMTracker::FLLMThreadState::PushAssetTag(int64 Tag)
{
	FScopeLock Lock(&TagSection);

	// push a tag
	AssetTagStack.Add(Tag);
}

void FLLMTracker::FLLMThreadState::PopAssetTag()
{
	FScopeLock Lock(&TagSection);

	LLMCheckf(AssetTagStack.Num() > 0, TEXT("Called FLLMTracker::FLLMThreadState::PopTag without a matching Push (stack was empty on pop)"));
	AssetTagStack.RemoveLast();
}

int64 FLLMTracker::FLLMThreadState::GetTopAssetTag()
{
	// make sure we have some pushed
	if (AssetTagStack.Num() == 0)
	{
		return (int64)ELLMTag::Untagged;
	}

	// return the top tag
	return AssetTagStack.GetLast();
}
#endif

void FLLMTracker::FLLMThreadState::IncrTag(int64 Tag, int64 Amount, bool bTrackUntagged)
{
	// track the untagged allocations
	if (Tag == (int64)ELLMTag::Untagged)
	{
		if (bTrackUntagged)
		{
			UntaggedAllocs += Amount;
#if LLM_TRACK_PEAK_MEMORY
			if (UntaggedAllocs > UntaggedAllocsPeak)
			{
				UntaggedAllocsPeak = UntaggedAllocs;
			}
#endif
		}
	}
	else
	{
		// look over existing tags on this thread for already tracking this tag
		for (uint32 TagSearch = 0; TagSearch < TaggedAllocTags.Num(); TagSearch++)
		{
			if (TaggedAllocTags[TagSearch] == Tag)
			{
				// update it if we found it, and break out
				TaggedAllocs[TagSearch] += Amount;
#if LLM_TRACK_PEAK_MEMORY
				if (TaggedAllocs[TagSearch] > TaggedAllocPeaks[TagSearch])
				{
					TaggedAllocPeaks[TagSearch] = TaggedAllocs[TagSearch];
				}
#endif
				return;
			}
		}

		// if we get here, then we need to add a new tracked tag
		TaggedAllocTags.Add(Tag);
		TaggedAllocs.Add(Amount);
#if LLM_TRACK_PEAK_MEMORY
		TaggedAllocPeaks.Add(Amount);
#endif
	}
}

int64 FLLMTracker::GetTagAmount(ELLMTag Tag) const
{
	return EnumTagAmounts[(int32)Tag];
}

void FLLMTracker::SetTagAmount(ELLMTag Tag, int64 Amount, bool AddToTotal)
{
	if (AddToTotal)
	{
		FPlatformAtomics::InterlockedAdd(&TrackedMemoryOverFrames, (int64)(Amount - EnumTagAmounts[(int32)Tag]));
	}

	FName StatName = LLMGetTagStat(Tag);
	if (StatName != NAME_None)
	{
		SET_MEMORY_STAT_FName(StatName, Amount);
	}

	EnumTagAmounts[(int32)Tag] = Amount;

	CsvWriter.SetStat((int64)Tag, Amount);
}

int64 FLLMTracker::GetAllocTypeAmount(ELLMAllocType AllocType)
{
	return AllocTypeAmounts[(int32)AllocType];
}

void FLLMTracker::FLLMThreadState::TrackAllocation(const void* Ptr, uint64 Size, ELLMTag DefaultTag, ELLMTracker Tracker, ELLMAllocType AllocType)
{
	FScopeLock Lock(&TagSection);

	AllocTypeAmounts[(int32)AllocType] += Size;

	int64 Tag = GetTopTag();
	if (Tag == (int64)ELLMTag::Untagged)
		Tag = (int64)DefaultTag;
	IncrTag(Tag, Size, true);
#if LLM_ALLOW_ASSETS_TAGS
	int64 AssetTag = GetTopAssetTag();
	IncrTag(AssetTag, Size, false);
#endif
	
	if (Tracker == ELLMTracker::Default)
	{
		FPlatformMemory::OnLowLevelMemory_Alloc(Ptr, Size, Tag);
	}
	
#if MEMPRO_ENABLED
	if (FMemProProfiler::IsTrackingTag( (ELLMTag)Tag) )
	{
		MEMPRO_TRACK_ALLOC((void*)Ptr, (size_t)Size);
	}
#endif
}

void FLLMTracker::FLLMThreadState::TrackFree(const void* Ptr, int64 Tag, uint64 Size, bool bTrackedUntagged, ELLMTracker Tracker, ELLMAllocType AllocType)
{
	FScopeLock Lock(&TagSection);

	AllocTypeAmounts[(int32)AllocType] -= Size;

	IncrTag(Tag, 0 - Size, bTrackedUntagged);

	if (Tracker == ELLMTracker::Default)
	{
		FPlatformMemory::OnLowLevelMemory_Free(Ptr, Size, Tag);
	}

#if MEMPRO_ENABLED
	if (FMemProProfiler::IsTrackingTag( (ELLMTag)Tag) )
	{
		MEMPRO_TRACK_FREE((void*)Ptr);
	}
#endif
}

/**
 * Grab totals for the frame and update stats, Tag amounts and csv.
 * InStateCopy is just passed in as a working copy to avoid having to create/destroy lots of LLMArrays
 */
void FLLMTracker::FLLMThreadState::GetFrameStatTotals(
	ELLMTag InUntaggedTotalTag,
	FLLMThreadState& InStateCopy,
	FLLMCsvWriter& InCsvWriter,
	FLLMCustomTag* CustomTags,
	int64* OutEnumTagAmounts,
	int64* OutAllocTypeAmounts)
{
	// grab the stats in a thread safe way
	{
		FScopeLock Lock(&TagSection);

		InStateCopy.UntaggedAllocs = UntaggedAllocs;

		InStateCopy.TaggedAllocTags = TaggedAllocTags;
		InStateCopy.TaggedAllocs = TaggedAllocs;
#if LLM_TRACK_PEAK_MEMORY
		InStateCopy.TaggedAllocPeaks = TaggedAllocPeaks;
#endif
		for (int32 Index = 0; Index < (int32)ELLMAllocType::Count; ++Index)
		{
			InStateCopy.AllocTypeAmounts[Index] = AllocTypeAmounts[Index];
		}

		// restart the tracking now that we've copied out, safely
		UntaggedAllocs = 0;
		TaggedAllocTags.Clear();
		TaggedAllocs.Clear();
		ClearAllocTypeAmounts();
#if LLM_TRACK_PEAK_MEMORY
		TaggedAllocPeaks.Clear();
		UntaggedAllocsPeak = 0;
#endif
	}

	IncMemoryStatByFName(LLMGetTagStat(InUntaggedTotalTag), InStateCopy.UntaggedAllocs);
	IncMemoryStatByFName(LLMGetTagStatGroup(InUntaggedTotalTag), InStateCopy.UntaggedAllocs);

#if LLM_TRACK_PEAK_MEMORY
	InCsvWriter.AddStat(FNameToTag(InUntaggedStatName), InStateCopy.UntaggedAllocs, InStateCopy.UntaggedAllocsPeak);
#else
	InCsvWriter.AddStat((int64)InUntaggedTotalTag, InStateCopy.UntaggedAllocs);
#endif

	for (int32 Index = 0; Index < (int32)ELLMAllocType::Count; ++Index)
	{
		OutAllocTypeAmounts[Index] += InStateCopy.AllocTypeAmounts[Index];
	}

	// walk over the tags for this level
	for (uint32 TagIndex = 0; TagIndex < InStateCopy.TaggedAllocTags.Num(); TagIndex++)
	{
		int64 Tag = InStateCopy.TaggedAllocTags[TagIndex];
		int64 Amount = InStateCopy.TaggedAllocs[TagIndex];

		//---------------------
		// update csv
#if LLM_TRACK_PEAK_MEMORY
		int64 Peak = InStateCopy.TaggedAllocPeaks[TagIndex];
		InCsvWriter.AddStat(Tag, Amount, Peak);
#else
		InCsvWriter.AddStat(Tag, Amount);
#endif

		//---------------------
		// update the stats
		if (Tag >= (int64)LLM_TAG_COUNT)
		{
			IncMemoryStatByFName(TagToFName(Tag), Amount);
		}
		else if (Tag >= LLM_CUSTOM_TAG_START)
		{
			IncMemoryStatByFName(CustomTags[Tag - LLM_CUSTOM_TAG_START].StatName, int64(Amount));
			IncMemoryStatByFName(CustomTags[Tag - LLM_CUSTOM_TAG_START].SummaryStatName, int64(Amount));
		}
		else
		{
			LLMCheck(Tag >= 0 && LLMGetTagName((ELLMTag)Tag) != nullptr);
			IncMemoryStatByFName(LLMGetTagStat((ELLMTag)Tag), int64(Amount));
			IncMemoryStatByFName(LLMGetTagStatGroup((ELLMTag)Tag), int64(Amount));
			OutEnumTagAmounts[Tag] += Amount;
		}
	}

	InStateCopy.Clear();
}

void FLLMTracker::FLLMThreadState::UpdateFrameStatGroups( FLLMCustomTag* CustomTags, const int32* ParentTags )
{
	FScopeLock Lock(&TagSection);

	uint32 MaxTagIndex = TaggedAllocTags.Num(); //group tags will be added at the end of the array. we don't want groups of groups so don't include them in the loop
	for (uint32 TagIndex = 0; TagIndex < MaxTagIndex; TagIndex++)
	{
		int64 Amount = TaggedAllocs[TagIndex];
		if( Amount != 0 )
		{
			int64 Tag = TaggedAllocTags[TagIndex];
			if( Tag >= 0 && Tag < LLM_TAG_COUNT)
			{
				int32 ParentTag = ParentTags[Tag];
				if( ParentTag != -1 )
				{
					IncrTag( ParentTag, Amount, false );
				}
			}
		}
	}
}



void FLLMTracker::FLLMThreadState::IncMemoryStatByFName(FName Name, int64 Amount)
{
	if (Name != NAME_None)
	{
		INC_MEMORY_STAT_BY_FName(Name, Amount);
	}
}

void FLLMTracker::FLLMThreadState::ClearAllocTypeAmounts()
{
	for (int32 Index = 0; Index < (int32)ELLMAllocType::Count; ++Index)
	{
		AllocTypeAmounts[Index] = 0;
	}
}

/*
 * FLLMCsvWriter implementation
*/

/*
* don't allocate memory in the constructor because it is called before allocators are setup
*/
FLLMCsvWriter::FLLMCsvWriter()
	: Enabled(true)
	, WriteCount(0)
	, LastWriteTime(FPlatformTime::Seconds())
	, Archive(NULL)
	, LastWriteStatValueCount(0)
{
}

FLLMCsvWriter::~FLLMCsvWriter()
{
	delete Archive;
}

void FLLMCsvWriter::Clear()
{
	StatValues.Clear(true);
	StatValuesForWrite.Clear(true);
}

/*
* don't allocate memory in this function because it is called by the allocator
*/
#if LLM_TRACK_PEAK_MEMORY
void FLLMCsvWriter::AddStat(int64 Tag, int64 Value, int64 Peak)
#else
void FLLMCsvWriter::AddStat(int64 Tag, int64 Value)
#endif
{
	FScopeLock lock(&StatValuesLock);

	if (!Enabled)
	{
		return;
	}

	int StatValueCount = StatValues.Num();
	for (int32 i = 0; i < StatValueCount; ++i)
	{
		if (StatValues[i].Tag == Tag)
		{
#if LLM_TRACK_PEAK_MEMORY
			int64 PossibleNewPeak = StatValues[i].Value + Peak;
			if (PossibleNewPeak > StatValues[i].Peak)
			{
				StatValues[i].Peak = PossibleNewPeak;
			}
#endif
			StatValues[i].Value += Value;
			return;
		}
	}

	StatValue NewStatValue;
	NewStatValue.Tag = Tag;
	NewStatValue.Value = Value;
#if LLM_TRACK_PEAK_MEMORY
	NewStatValue.Peak = Peak;
#endif
	StatValues.Add(NewStatValue);
}

/*
* don't allocate memory in this function because it is called by the allocator
*/
#if LLM_TRACK_PEAK_MEMORY
void FLLMCsvWriter::SetStat(int64 Tag, int64 Value, int64 Peak)
#else
void FLLMCsvWriter::SetStat(int64 Tag, int64 Value)
#endif
{
	FScopeLock lock(&StatValuesLock);

	int StatValueCount = StatValues.Num();
	for (int32 i = 0; i < StatValueCount; ++i)
	{
		if (StatValues[i].Tag == Tag)
		{
#if LLM_TRACK_PEAK_MEMORY
			if (Peak > StatValues[i].Peak)
			{
				StatValues[i].Peak = Peak;
			}
#endif
			StatValues[i].Value = Value;
			return;
		}
	}

	StatValue NewStatValue;
	NewStatValue.Tag = Tag;
	NewStatValue.Value = Value;
#if LLM_TRACK_PEAK_MEMORY
	NewStatValue.Peak = Peak;
#endif
	StatValues.Add(NewStatValue);
}

/*
* memory can be allocated in this function
*/
void FLLMCsvWriter::Update(FLLMCustomTag* CustomTags, const int32* ParentTags)
{
	double Now = FPlatformTime::Seconds();
	if (Now - LastWriteTime >= (double)CVarLLMWriteInterval.GetValueOnGameThread())
	{
		WriteGraph(CustomTags, ParentTags);

		LastWriteTime = Now;
	}
}

const TCHAR* FLLMCsvWriter::GetTrackerCsvName(ELLMTracker InTracker)
{
	switch (InTracker)
	{
		case ELLMTracker::Default: return TEXT("LLM");
		case ELLMTracker::Platform: return TEXT("LLMPlatform");
		default: LLMCheck(false); return TEXT("");
	}
}

/*
 * Archive is a binary stream, so we can't just serialise an FString using <<
*/
void FLLMCsvWriter::Write(const FString& Text)
{
	Archive->Serialize(TCHAR_TO_ANSI(*Text), Text.Len() * sizeof(ANSICHAR));
}

/*
 * create the csv file on the first call. When it finds a new stat name it seeks
 * back to the start of the file and re-writes the column names.
*/
void FLLMCsvWriter::WriteGraph(FLLMCustomTag* CustomTags, const int32* ParentTags)
{
	// create the csv file
	if (!Archive)
	{
		FString Directory = FPaths::ProfilingDir() + "LLM/";
		IFileManager::Get().MakeDirectory(*Directory, true);
		
		const TCHAR* TrackerName = GetTrackerCsvName(Tracker);
		const FDateTime FileDate = FDateTime::Now();
#if WITH_SERVER_CODE
		FString Filename = FString::Printf(TEXT("%s/%s_Pid%d_%s.csv"), *Directory, TrackerName, FPlatformProcess::GetCurrentProcessId(), *FileDate.ToString());
#else
		FString Filename = FString::Printf(TEXT("%s/%s_%s.csv"), *Directory, TrackerName, *FileDate.ToString());
#endif
		Archive = IFileManager::Get().CreateFileWriter(*Filename, FILEWRITE_AllowRead);
		LLMCheck(Archive);

		// create space for column titles that are filled in as we get them
		for (int32 i = 0; i < 500; ++i)
		{
			Write(TEXT("          "));
		}
		Write(TEXT("\n"));
	}

	// grab the stats (make sure that no allocations happen in this scope)
	{
		FScopeLock lock(&StatValuesLock);
		StatValuesForWrite = StatValues;
	}

	// re-write the column names if we have found a new one
	int32 StatValueCountLocal = StatValuesForWrite.Num();
	if (StatValueCountLocal != LastWriteStatValueCount)
	{
		int64 OriginalOffset = Archive->Tell();
		Archive->Seek(0);

		for (int32 i = 0; i < StatValueCountLocal; ++i)
		{
			FString StatName = GetTagName(StatValuesForWrite[i].Tag, CustomTags, ParentTags);
			FString Text = FString::Printf(TEXT("%s,"), *StatName);
			Write(Text);
		}

		Archive->Seek(OriginalOffset);

		LastWriteStatValueCount = StatValueCountLocal;
	}

	// write the actual stats
	for (int32 i = 0; i < StatValueCountLocal; ++i)
	{
#if LLM_TRACK_PEAK_MEMORY
		FString Text = FString::Printf(TEXT("%0.2f,"), StatValuesForWrite[i].Peak / 1024.0f / 1024.0f);
#else
		FString Text = FString::Printf(TEXT("%0.2f,"), StatValuesForWrite[i].Value / 1024.0f / 1024.0f);
#endif
		Write(Text);
	}
	Write(TEXT("\n"));

	WriteCount++;

	if (CVarLLMWriteInterval.GetValueOnGameThread())
	{
		UE_LOG(LogHAL, Log, TEXT("Wrote LLM csv line %d"), WriteCount);
	}

	Archive->Flush();
}

/*
 * convert a Tag to a string. If the Tag is actually a Stat then extract the name of the stat.
*/
FString FLLMCsvWriter::GetTagName(int64 Tag, FLLMCustomTag* CustomTags, const int32* ParentTags)
{
	FString Result;

	if (Tag >= (int64)LLM_TAG_COUNT)
	{
		FString Name = TagToFName(Tag).ToString();

		// if it has a trible slash assume it is a Stat string and extract the descriptive name
		int32 StartIndex = Name.Find(TEXT("///"), ESearchCase::CaseSensitive);
		if (StartIndex != -1)
		{
			StartIndex += 3;
			int32 EndIndex = Name.Find(TEXT("///"), ESearchCase::CaseSensitive, ESearchDir::FromStart, StartIndex);
			if (EndIndex != -1)
			{
				Name.MidInline(StartIndex, EndIndex - StartIndex, false);
			}
		}

		Result = Name;
	}
	else if (Tag >= LLM_CUSTOM_TAG_START && Tag <= LLM_CUSTOM_TAG_END)
	{
		if (ParentTags != nullptr && ParentTags[Tag] != -1)
		{
			Result = GetTagName( ParentTags[Tag], CustomTags, nullptr ) + TEXT("/");
		}

		LLMCheckf(CustomTags[Tag - LLM_CUSTOM_TAG_START].Name != nullptr, TEXT("Tag %lld has no name"), Tag ); 
		Result += CustomTags[Tag - LLM_CUSTOM_TAG_START].Name;
	}
	else
	{
		LLMCheck(Tag >= 0 && LLMGetTagName((ELLMTag)Tag) != nullptr);

		if (ParentTags != nullptr && ParentTags[Tag] != -1)
		{
			Result = GetTagName( ParentTags[Tag], CustomTags, nullptr ) + TEXT("/");
		}

		Result += LLMGetTagName((ELLMTag)Tag);
	}

	return Result;
}

#endif		// #if ENABLE_LOW_LEVEL_MEM_TRACKER

