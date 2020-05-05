// Copyright Epic Games, Inc. All Rights Reserved.
#include "AudioDerivedData.h"
#include "Interfaces/IAudioFormat.h"
#include "Misc/CommandLine.h"
#include "Stats/Stats.h"
#include "Async/AsyncWork.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Misc/ScopedSlowTask.h"
#include "Audio.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundEffectBase.h"
#include "DerivedDataCacheInterface.h"
#include "ProfilingDebugging/CookStats.h"
#include "AudioResampler.h"
#include "AudioCompressionSettingsUtils.h"
#include "Sound/SoundSourceBus.h"
#include "Sound/SoundWave.h"

DEFINE_LOG_CATEGORY_STATIC(LogAudioDerivedData, Log, All);

#define FORCE_RESAMPLE 0

#if ENABLE_COOK_STATS
namespace AudioCookStats
{
	static FCookStats::FDDCResourceUsageStats UsageStats;
	static FCookStats::FDDCResourceUsageStats StreamingChunkUsageStats;
	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
	{
		UsageStats.LogStats(AddStat, TEXT("Audio.Usage"), TEXT("Inline"));
		StreamingChunkUsageStats.LogStats(AddStat, TEXT("Audio.Usage"), TEXT("Streaming"));
	});
}
#endif

#if WITH_EDITORONLY_DATA

/*------------------------------------------------------------------------------
Derived data key generation.
------------------------------------------------------------------------------*/

// If you want to bump this version, generate a new guid using
// VS->Tools->Create GUID and paste it here. https://www.guidgen.com works too.
#define STREAMEDAUDIO_DERIVEDDATA_VER		TEXT("BC6E92FBBD314E3B9B9EC6778749EB5E")

/**
 * Computes the derived data key suffix for a SoundWave's Streamed Audio.
 * @param SoundWave - The SoundWave for which to compute the derived data key.
 * @param AudioFormatName - The audio format we're creating the key for
 * @param OutKeySuffix - The derived data key suffix.
 */
static void GetStreamedAudioDerivedDataKeySuffix(
	const USoundWave& SoundWave,
	FName AudioFormatName,
	const FPlatformAudioCookOverrides* CompressionOverrides,
	FString& OutKeySuffix
	)
{
	uint16 Version = 0;

	// get the version for this soundwave's platform format
	ITargetPlatformManagerModule* TPM = GetTargetPlatformManager();
	if (TPM)
	{
		const IAudioFormat* AudioFormat = TPM->FindAudioFormat(AudioFormatName);
		if (AudioFormat)
		{
			Version = AudioFormat->GetVersion(AudioFormatName);
		}
	}

	FString AudioFormatNameString = AudioFormatName.ToString();

	// If we have compression overrides for this target platform, append them to this string.
	if (CompressionOverrides)
	{
		FPlatformAudioCookOverrides::GetHashSuffix(CompressionOverrides, AudioFormatNameString);
	}

	// build the key
	OutKeySuffix = FString::Printf(TEXT("%s_%d_%s"),
		*AudioFormatNameString,
		Version,
		*SoundWave.CompressedDataGuid.ToString()
		);
}

/**
 * Constructs a derived data key from the key suffix.
 * @param KeySuffix - The key suffix.
 * @param OutKey - The full derived data key.
 */
static void GetStreamedAudioDerivedDataKeyFromSuffix(const FString& KeySuffix, FString& OutKey)
{
	OutKey = FDerivedDataCacheInterface::BuildCacheKey(
		TEXT("STREAMEDAUDIO"),
		STREAMEDAUDIO_DERIVEDDATA_VER,
		*KeySuffix
		);
}

/**
 * Constructs the derived data key for an individual audio chunk.
 * @param KeySuffix - The key suffix.
 * @param ChunkIndex - The chunk index.
 * @param OutKey - The full derived data key for the audio chunk.
 */
static void GetStreamedAudioDerivedChunkKey(
	int32 ChunkIndex,
	const FStreamedAudioChunk& Chunk,
	const FString& KeySuffix,
	FString& OutKey
	)
{
	OutKey = FDerivedDataCacheInterface::BuildCacheKey(
		TEXT("STREAMEDAUDIO"),
		STREAMEDAUDIO_DERIVEDDATA_VER,
		*FString::Printf(TEXT("%s_CHUNK%u_%d"), *KeySuffix, ChunkIndex, Chunk.DataSize)
		);
}

/**
 * Computes the derived data key for Streamed Audio.
 * @param SoundWave - The soundwave for which to compute the derived data key.
 * @param AudioFormatName - The audio format we're creating the key for
 * @param OutKey - The derived data key.
 */
static void GetStreamedAudioDerivedDataKey(
	const USoundWave& SoundWave,
	FName AudioFormatName,
	const FPlatformAudioCookOverrides* CompressionOverrides,
	FString& OutKey
	)
{
	FString KeySuffix;
	GetStreamedAudioDerivedDataKeySuffix(SoundWave, AudioFormatName, CompressionOverrides, KeySuffix);
	GetStreamedAudioDerivedDataKeyFromSuffix(KeySuffix, OutKey);
}

static ITargetPlatform* GetRunningTargetPlatform(ITargetPlatformManagerModule* TPM)
{
	ITargetPlatform* CurrentPlatform = NULL;
	const TArray<ITargetPlatform*>& Platforms = TPM->GetActiveTargetPlatforms();

	check(Platforms.Num());

	CurrentPlatform = Platforms[0];

	for (int32 Index = 1; Index < Platforms.Num(); Index++)
	{
		if (Platforms[Index]->IsRunningPlatform())
		{
			CurrentPlatform = Platforms[Index];
			break;
		}
	}

	check(CurrentPlatform != NULL);
	return CurrentPlatform;
}

/**
 * Gets Wave format for a SoundWave on the current running platform
 * @param SoundWave - The SoundWave to get format for.
 */
static FName GetWaveFormatForRunningPlatform(USoundWave& SoundWave)
{
	// Compress to whatever format the active target platform wants
	ITargetPlatformManagerModule* TPM = GetTargetPlatformManager();
	if (TPM)
	{
		ITargetPlatform* CurrentPlatform = GetRunningTargetPlatform(TPM);


		return CurrentPlatform->GetWaveFormat(&SoundWave);
	}

	return NAME_None;
}

static const FPlatformAudioCookOverrides* GetCookOverridesForRunningPlatform()
{
	return FPlatformCompressionUtilities::GetCookOverrides(ANSI_TO_TCHAR(FPlatformProperties::IniPlatformName()));
}

/**
 * Stores derived data in the DDC.
 * After this returns, all bulk data from streaming chunks will be sent separately to the DDC and the BulkData for those chunks removed.
 * @param DerivedData - The data to store in the DDC.
 * @param DerivedDataKeySuffix - The key suffix at which to store derived data.
 * @return number of bytes put to the DDC (total, including all chunks)
 */
static uint32 PutDerivedDataInCache(
	FStreamedAudioPlatformData* DerivedData,
	const FString& DerivedDataKeySuffix,
	const FStringView& SoundWaveName
	)
{
	TArray<uint8> RawDerivedData;
	FString DerivedDataKey;
	uint32 TotalBytesPut = 0;

	// Build the key with which to cache derived data.
	GetStreamedAudioDerivedDataKeyFromSuffix(DerivedDataKeySuffix, DerivedDataKey);

	FString LogString;
	if (UE_LOG_ACTIVE(LogAudio,Verbose))
	{
		LogString = FString::Printf(
			TEXT("Storing Streamed Audio in DDC:\n  Key: %s\n  Format: %s\n"),
			*DerivedDataKey,
			*DerivedData->AudioFormat.ToString()
			);
	}

	// Write out individual chunks to the derived data cache.
	const int32 ChunkCount = DerivedData->Chunks.Num();
	for (int32 ChunkIndex = 0; ChunkIndex < ChunkCount; ++ChunkIndex)
	{
		FString ChunkDerivedDataKey;
		FStreamedAudioChunk& Chunk = DerivedData->Chunks[ChunkIndex];
		GetStreamedAudioDerivedChunkKey(ChunkIndex, Chunk, DerivedDataKeySuffix, ChunkDerivedDataKey);

		if (UE_LOG_ACTIVE(LogAudio,Verbose))
		{
			LogString += FString::Printf(TEXT("  Chunk%d %d bytes %s\n"),
				ChunkIndex,
				Chunk.BulkData.GetBulkDataSize(),
				*ChunkDerivedDataKey
				);
		}

		TotalBytesPut += Chunk.StoreInDerivedDataCache(ChunkDerivedDataKey, SoundWaveName);
	}

	// Store derived data.
	// At this point we've stored all the non-inline data in the DDC, so this will only serialize and store the metadata and any inline chunks
	FMemoryWriter Ar(RawDerivedData, /*bIsPersistent=*/ true);
	DerivedData->Serialize(Ar, NULL);
	GetDerivedDataCacheRef().Put(*DerivedDataKey, RawDerivedData, SoundWaveName);
	TotalBytesPut += RawDerivedData.Num();
	UE_LOG(LogAudio, Verbose, TEXT("%s  Derived Data: %d bytes"), *LogString, RawDerivedData.Num());
	return TotalBytesPut;
}

#endif // #if WITH_EDITORONLY_DATA

#if WITH_EDITORONLY_DATA

namespace EStreamedAudioCacheFlags
{
	enum Type
	{
		None			= 0x0,
		Async			= 0x1,
		ForceRebuild	= 0x2,
		InlineChunks	= 0x4,
		AllowAsyncBuild	= 0x8,
		ForDDCBuild		= 0x10,
	};
};

/**
 * Worker used to cache streamed audio derived data.
 */
class FStreamedAudioCacheDerivedDataWorker : public FNonAbandonableTask
{
	/** Where to store derived data. */
	FStreamedAudioPlatformData* DerivedData;
	/** The SoundWave for which derived data is being cached. */
	USoundWave& SoundWave;
	/** Audio Format Name */
	FName AudioFormatName;
	/** Derived data key suffix. */
	FString KeySuffix;
	/** Streamed Audio cache flags. */
	uint32 CacheFlags;
	/** Have many bytes were loaded from DDC or built (for telemetry) */
	uint32 BytesCached = 0;

	/** Sample rate override specified for this sound wave. */
	const FPlatformAudioCookOverrides* CompressionOverrides;

	/** true if caching has succeeded. */
	bool bSucceeded;
	/** true if the derived data was pulled from DDC */
	bool bLoadedFromDDC = false;

	/** Build the streamed audio. This function is safe to call from any thread. */
	void BuildStreamedAudio()
	{
		GetStreamedAudioDerivedDataKeySuffix(SoundWave, AudioFormatName, CompressionOverrides, KeySuffix);

		DerivedData->Chunks.Empty();

		ITargetPlatformManagerModule* TPM = GetTargetPlatformManager();
		const IAudioFormat* AudioFormat = NULL;
		if (TPM)
		{
			AudioFormat = TPM->FindAudioFormat(AudioFormatName);
		}

		if (AudioFormat)
		{
			DerivedData->AudioFormat = AudioFormatName;

			FByteBulkData* CompressedData = SoundWave.GetCompressedData(AudioFormatName, CompressionOverrides);
			if (CompressedData)
			{
				TArray<uint8> CompressedBuffer;
				CompressedBuffer.Empty(CompressedData->GetBulkDataSize());
				CompressedBuffer.AddUninitialized(CompressedData->GetBulkDataSize());
				void* BufferData = CompressedBuffer.GetData();
				CompressedData->GetCopy(&BufferData, false);
				TArray<TArray<uint8>> ChunkBuffers;

				// Set the ideal chunk size to be 256k to optimize for data reads on console.
				int32 MaxChunkSize = 256 * 1024;
				
				// By default, the first chunk's max size is the same as the other chunks.
				int32 FirstChunkSize = MaxChunkSize;

				const int32 MinimumChunkSize = AudioFormat->GetMinimumSizeForInitialChunk(AudioFormatName, CompressedBuffer);
				const bool bUseStreamCaching = CompressionOverrides && CompressionOverrides->bUseStreamCaching;
				const bool bForceLegacyStreamChunking = SoundWave.bStreaming && CompressionOverrides && CompressionOverrides->StreamCachingSettings.bForceLegacyStreamChunking;

				// If the initial chunk  for this sound wave was overridden, use that:
				if (SoundWave.InitialChunkSize > 0)
				{
					FirstChunkSize = FMath::Max(MinimumChunkSize, SoundWave.InitialChunkSize);
				}
				else if (bUseStreamCaching)
				{
					// Ensure that the minimum chunk size is nonzero if our compressed buffer is not empty.
					checkf(CompressedBuffer.Num() == 0 || MinimumChunkSize != 0, TEXT("To use Load On Demand, please override GetMinimumSizeForInitialChunk"));

					//
					if (bForceLegacyStreamChunking)
					{
						int32 LegacyZerothChunkSize = CompressionOverrides->StreamCachingSettings.ZerothChunkSizeForLegacyStreamChunkingKB * 1024;
						if (LegacyZerothChunkSize == 0)
						{
							LegacyZerothChunkSize = MaxChunkSize;
						}

						FirstChunkSize = LegacyZerothChunkSize;
					}
					else
					{
						// Otherwise if we're using Audio Stream Caching, the first chunk should be as small as possible:
						FirstChunkSize = MinimumChunkSize;
					}
				}

				if (bUseStreamCaching)
				{
					// Use the chunk size for this duration:
					MaxChunkSize = FPlatformCompressionUtilities::GetMaxChunkSizeForCookOverrides(CompressionOverrides);
					UE_LOG(LogAudio, Display, TEXT("Chunk size for %s: %d"), *SoundWave.GetFullName(), MaxChunkSize);
				}
				
				check(FirstChunkSize != 0 && MaxChunkSize != 0);

				if (AudioFormat->SplitDataForStreaming(CompressedBuffer, ChunkBuffers, FirstChunkSize, MaxChunkSize))
				{
					if (ChunkBuffers.Num() > 32)
					{
						UE_LOG(LogAudio, Display, TEXT("Sound Wave %s is very large, requiring %d chunks."), *SoundWave.GetFullName(), ChunkBuffers.Num());
					}

					if (ChunkBuffers.Num() > 0)
					{
						// The zeroth chunk should not be zero-padded.
						const int32 AudioDataSize = ChunkBuffers[0].Num();

						//FStreamedAudioChunk* NewChunk = new(DerivedData->Chunks) FStreamedAudioChunk();
						int32 ChunkIndex = DerivedData->Chunks.Add(new FStreamedAudioChunk());
						FStreamedAudioChunk* NewChunk = &(DerivedData->Chunks[ChunkIndex]);
						// Store both the audio data size and the data size so decoders will know what portion of the bulk data is real audio
						NewChunk->AudioDataSize = AudioDataSize;
						NewChunk->DataSize = AudioDataSize;

#if WITH_EDITORONLY_DATA
						if (NewChunk->BulkData.IsLocked())
						{
							UE_LOG(LogAudioDerivedData, Warning, TEXT("While building split chunk for streaming: Raw PCM data already being written to. Chunk Index: 0 SoundWave: %s "), *SoundWave.GetFullName());
						}
#endif

						NewChunk->BulkData.Lock(LOCK_READ_WRITE);
						void* NewChunkData = NewChunk->BulkData.Realloc(NewChunk->AudioDataSize);
						FMemory::Memcpy(NewChunkData, ChunkBuffers[0].GetData(), AudioDataSize);
						NewChunk->BulkData.Unlock();
					}

					// Zero-pad the rest of the chunks here:
					for (int32 ChunkIndex = 1; ChunkIndex < ChunkBuffers.Num(); ++ChunkIndex)
					{
						// Zero pad the reallocation if the chunk isn't precisely the max chunk size to keep the reads aligned to MaxChunkSize
						const int32 AudioDataSize = ChunkBuffers[ChunkIndex].Num();
						check(AudioDataSize != 0 && AudioDataSize <= MaxChunkSize);
						const int32 ZeroPadBytes = FMath::Max(MaxChunkSize - AudioDataSize, 0);

						FStreamedAudioChunk* NewChunk = new FStreamedAudioChunk();
						DerivedData->Chunks.Add(NewChunk);

						// Store both the audio data size and the data size so decoders will know what portion of the bulk data is real audio
						NewChunk->AudioDataSize = AudioDataSize;
						NewChunk->DataSize = AudioDataSize + ZeroPadBytes;

#if WITH_EDITORONLY_DATA
						if (NewChunk->BulkData.IsLocked())
						{
							UE_LOG(LogAudioDerivedData, Warning, TEXT("While building split chunk for streaming: Raw PCM data already being written to. Chunk Index: %d SoundWave: %s "), ChunkIndex, *SoundWave.GetFullName());
						}
#endif

						NewChunk->BulkData.Lock(LOCK_READ_WRITE);

						void* NewChunkData = NewChunk->BulkData.Realloc(NewChunk->DataSize);
						FMemory::Memcpy(NewChunkData, ChunkBuffers[ChunkIndex].GetData(), AudioDataSize);
						FMemory::Memzero((uint8*)NewChunkData + AudioDataSize, ZeroPadBytes);

						NewChunk->BulkData.Unlock();
					}
				}
				else
				{
					// Could not split so copy compressed data into a single chunk
					FStreamedAudioChunk* NewChunk = new FStreamedAudioChunk();
					DerivedData->Chunks.Add(NewChunk);
					NewChunk->DataSize = CompressedBuffer.Num();
					NewChunk->AudioDataSize = NewChunk->DataSize;

#if WITH_EDITORONLY_DATA
					if (NewChunk->BulkData.IsLocked())
					{
						UE_LOG(LogAudioDerivedData, Warning, TEXT("While building single-chunk streaming SoundWave: Raw PCM data already being written to. SoundWave: %s "), *SoundWave.GetFullName());
					}
#endif

					NewChunk->BulkData.Lock(LOCK_READ_WRITE);
					void* NewChunkData = NewChunk->BulkData.Realloc(CompressedBuffer.Num());
					FMemory::Memcpy(NewChunkData, CompressedBuffer.GetData(), CompressedBuffer.Num());
					NewChunk->BulkData.Unlock();
				}

				DerivedData->NumChunks = DerivedData->Chunks.Num();

				// Store it in the cache.
				// @todo: This will remove the streaming bulk data, which we immediately reload below!
				// Should ideally avoid this redundant work, but it only happens when we actually have
				// to build the compressed audio, which should only ever be once.
				this->BytesCached = PutDerivedDataInCache(DerivedData, KeySuffix, SoundWave.GetPathName());

				check(this->BytesCached != 0);
			}
			else
			{
				UE_LOG(LogAudio, Display, TEXT("Failed to retrieve compressed data for format %s and soundwave %s."),
					   *AudioFormatName.GetPlainNameString(),
					   *SoundWave.GetPathName()
					);
			}
		}

		if (DerivedData->Chunks.Num())
		{
			bool bInlineChunks = (CacheFlags & EStreamedAudioCacheFlags::InlineChunks) != 0;
			bSucceeded = !bInlineChunks || DerivedData->TryInlineChunkData();
		}
		else
		{
			UE_LOG(LogAudio, Display, TEXT("Failed to build %s derived data for %s"),
				*AudioFormatName.GetPlainNameString(),
				*SoundWave.GetPathName()
				);
		}
	}

public:
	/** Initialization constructor. */
	FStreamedAudioCacheDerivedDataWorker(
		FStreamedAudioPlatformData* InDerivedData,
		USoundWave* InSoundWave,
		const FPlatformAudioCookOverrides* InCompressionOverrides,
		FName InAudioFormatName,
		uint32 InCacheFlags
		)
		: DerivedData(InDerivedData)
		, SoundWave(*InSoundWave)
		, AudioFormatName(InAudioFormatName)
		, CacheFlags(InCacheFlags)
		, CompressionOverrides(InCompressionOverrides)
		, bSucceeded(false)
		, bLoadedFromDDC(false)
	{
	}

	/** Does the work to cache derived data. Safe to call from any thread. */
	void DoWork()
	{
		TArray<uint8> RawDerivedData;
		bool bForceRebuild = (CacheFlags & EStreamedAudioCacheFlags::ForceRebuild) != 0;
		bool bInlineChunks = (CacheFlags & EStreamedAudioCacheFlags::InlineChunks) != 0;
		bool bForDDC = (CacheFlags & EStreamedAudioCacheFlags::ForDDCBuild) != 0;
		bool bAllowAsyncBuild = (CacheFlags & EStreamedAudioCacheFlags::AllowAsyncBuild) != 0;

		if (!bForceRebuild && GetDerivedDataCacheRef().GetSynchronous(*DerivedData->DerivedDataKey, RawDerivedData, SoundWave.GetPathName()))
		{
			BytesCached = RawDerivedData.Num();
			FMemoryReader Ar(RawDerivedData, /*bIsPersistent=*/ true);
			DerivedData->Serialize(Ar, NULL);
			bSucceeded = true;
			// Load any streaming (not inline) chunks that are necessary for our platform.
			if (bForDDC)
			{
				for (int32 Index = 0; Index < DerivedData->NumChunks; ++Index)
				{
					if (!DerivedData->GetChunkFromDDC(Index, NULL))
					{
						bSucceeded = false;
						break;
					}
				}
			}
			else if (bInlineChunks)
			{
				bSucceeded = DerivedData->TryInlineChunkData();
			}
			else
			{
				bSucceeded = DerivedData->AreDerivedChunksAvailable();
			}
			bLoadedFromDDC = true;
		}
		else if (bAllowAsyncBuild)
		{
			BuildStreamedAudio();
		}
	}

	/** Finalize work. Must be called ONLY by the thread that started this task! */
	bool Finalize()
	{
		// if we couldn't get from the DDC or didn't build synchronously, then we have to build now.
		// This is a super edge case that should rarely happen.
		if (!bSucceeded)
		{
			BuildStreamedAudio();
		}
		return bLoadedFromDDC;
	}

	/** Expose bytes cached for telemetry. */
	uint32 GetBytesCached() const
	{
		return BytesCached;
	}

	/** Expose how the resource was returned for telemetry. */
	bool WasLoadedFromDDC() const
	{
		return bLoadedFromDDC;
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FStreamedAudioCacheDerivedDataWorker, STATGROUP_ThreadPoolAsyncTasks);
	}
};

struct FStreamedAudioAsyncCacheDerivedDataTask : public FAsyncTask<FStreamedAudioCacheDerivedDataWorker>
{
	FStreamedAudioAsyncCacheDerivedDataTask(
		FStreamedAudioPlatformData* InDerivedData,
		USoundWave* InSoundWave,
		const FPlatformAudioCookOverrides* CompressionSettings,
		FName InAudioFormatName,
		uint32 InCacheFlags
		)
		: FAsyncTask<FStreamedAudioCacheDerivedDataWorker>(
			InDerivedData,
			InSoundWave,
			CompressionSettings,
			InAudioFormatName,
			InCacheFlags
			)
	{
	}
};

void FStreamedAudioPlatformData::Cache(USoundWave& InSoundWave, const FPlatformAudioCookOverrides* CompressionOverrides, FName AudioFormatName,  uint32 InFlags)
{
	// Flush any existing async task and ignore results.
	FinishCache();

	uint32 Flags = InFlags;

	static bool bForDDC = FString(FCommandLine::Get()).Contains(TEXT("Run=DerivedDataCache"));
	if (bForDDC)
	{
		Flags |= EStreamedAudioCacheFlags::ForDDCBuild;
	}

	bool bForceRebuild = (Flags & EStreamedAudioCacheFlags::ForceRebuild) != 0;
	bool bAsync = !bForDDC && (Flags & EStreamedAudioCacheFlags::Async) != 0;
	GetStreamedAudioDerivedDataKey(InSoundWave, AudioFormatName, CompressionOverrides, DerivedDataKey);

	if (bAsync && !bForceRebuild)
	{
		AsyncTask = new FStreamedAudioAsyncCacheDerivedDataTask(this, &InSoundWave, CompressionOverrides, AudioFormatName, Flags);
		AsyncTask->StartBackgroundTask();
	}
	else
	{
		FStreamedAudioCacheDerivedDataWorker Worker(this, &InSoundWave, CompressionOverrides, AudioFormatName, Flags);
		{
			COOK_STAT(auto Timer = AudioCookStats::UsageStats.TimeSyncWork());
			Worker.DoWork();
			Worker.Finalize();
			COOK_STAT(Timer.AddHitOrMiss(Worker.WasLoadedFromDDC() ? FCookStats::CallStats::EHitOrMiss::Hit : FCookStats::CallStats::EHitOrMiss::Miss, Worker.GetBytesCached()));
		}
	}
}

bool FStreamedAudioPlatformData::IsFinishedCache() const
{
	return AsyncTask == NULL ? true : false;
}

void FStreamedAudioPlatformData::FinishCache()
{
	if (AsyncTask)
	{
		{
			COOK_STAT(auto Timer = AudioCookStats::UsageStats.TimeAsyncWait());
			AsyncTask->EnsureCompletion();
			FStreamedAudioCacheDerivedDataWorker& Worker = AsyncTask->GetTask();
			Worker.Finalize();
			COOK_STAT(Timer.AddHitOrMiss(Worker.WasLoadedFromDDC() ? FCookStats::CallStats::EHitOrMiss::Hit : FCookStats::CallStats::EHitOrMiss::Miss, Worker.GetBytesCached()));
		}
		delete AsyncTask;
		AsyncTask = NULL;
	}
}

/**
 * Executes async DDC gets for chunks stored in the derived data cache.
 * @param Chunks - Chunks to retrieve.
 * @param FirstChunkToLoad - Index of the first chunk to retrieve.
 * @param OutHandles - Handles to the asynchronous DDC gets.
 */
static void BeginLoadDerivedChunks(TIndirectArray<FStreamedAudioChunk>& Chunks, int32 FirstChunkToLoad, TArray<uint32>& OutHandles)
{
	FDerivedDataCacheInterface& DDC = GetDerivedDataCacheRef();
	OutHandles.AddZeroed(Chunks.Num());
	for (int32 ChunkIndex = FirstChunkToLoad; ChunkIndex < Chunks.Num(); ++ChunkIndex)
	{
		const FStreamedAudioChunk& Chunk = Chunks[ChunkIndex];
		if (!Chunk.DerivedDataKey.IsEmpty())
		{
			OutHandles[ChunkIndex] = DDC.GetAsynchronous(*Chunk.DerivedDataKey, TEXT("Unknown SoundWave"_SV));
		}
	}
}

bool FStreamedAudioPlatformData::TryInlineChunkData()
{
	TArray<uint32> AsyncHandles;
	TArray<uint8> TempData;
	FDerivedDataCacheInterface& DDC = GetDerivedDataCacheRef();

	BeginLoadDerivedChunks(Chunks, 0, AsyncHandles);
	for (int32 ChunkIndex = 0; ChunkIndex < Chunks.Num(); ++ChunkIndex)
	{
		FStreamedAudioChunk& Chunk = Chunks[ChunkIndex];
		if (Chunk.DerivedDataKey.IsEmpty() == false)
		{
			uint32 AsyncHandle = AsyncHandles[ChunkIndex];
			bool bLoadedFromDDC = false;
			COOK_STAT(auto Timer = AudioCookStats::StreamingChunkUsageStats.TimeAsyncWait());
			DDC.WaitAsynchronousCompletion(AsyncHandle);
			bLoadedFromDDC = DDC.GetAsynchronousResults(AsyncHandle, TempData);
			COOK_STAT(Timer.AddHitOrMiss(bLoadedFromDDC ? FCookStats::CallStats::EHitOrMiss::Hit : FCookStats::CallStats::EHitOrMiss::Miss, TempData.Num()));
			if (bLoadedFromDDC)
			{
				int32 ChunkSize = 0;
				int32 AudioDataSize = 0;
				FMemoryReader Ar(TempData, /*bIsPersistent=*/ true);
				Ar << ChunkSize;
				Ar << AudioDataSize; // Unused for the purposes of this function.

#if WITH_EDITORONLY_DATA
				if (Chunk.BulkData.IsLocked())
				{
					UE_LOG(LogAudioDerivedData, Warning, TEXT("In TryInlineChunkData: Raw PCM data already being written to. Chunk: %d DDC Key: %s "), ChunkIndex, *DerivedDataKey);
				}
#endif

				Chunk.BulkData.Lock(LOCK_READ_WRITE);
				void* ChunkData = Chunk.BulkData.Realloc(ChunkSize);
				Ar.Serialize(ChunkData, ChunkSize);
				Chunk.BulkData.Unlock();
				Chunk.DerivedDataKey.Empty();
			}
			else
			{
				return false;
			}
			TempData.Reset();
		}
	}
	return true;
}

#endif //WITH_EDITORONLY_DATA

FStreamedAudioPlatformData::FStreamedAudioPlatformData()
	: NumChunks(0)
#if WITH_EDITORONLY_DATA
	, AsyncTask(NULL)
#endif // #if WITH_EDITORONLY_DATA
{
}

FStreamedAudioPlatformData::~FStreamedAudioPlatformData()
{
#if WITH_EDITORONLY_DATA
	if (AsyncTask)
	{
		AsyncTask->EnsureCompletion();
		delete AsyncTask;
		AsyncTask = NULL;
	}
#endif
}

int32 FStreamedAudioPlatformData::DeserializeChunkFromDDC(TArray<uint8> TempData, FStreamedAudioChunk &Chunk, int32 ChunkIndex, uint8** &OutChunkData)
{
	int32 ChunkSize = 0;
	FMemoryReader Ar(TempData, /*bIsPersistent=*/ true);
	int32 AudioDataSize = 0;
	Ar << ChunkSize;
	Ar << AudioDataSize;

#if WITH_EDITORONLY_DATA
	ensureAlwaysMsgf((ChunkSize == Chunk.DataSize && AudioDataSize == Chunk.AudioDataSize),
		TEXT("Chunk %d of %s SoundWave has invalid data in the DDC. Got %d bytes, expected %d. Audio Data was %d bytes but we expected %d bytes. Key=%s"),
		ChunkIndex,
		*AudioFormat.ToString(),
		ChunkSize,
		Chunk.DataSize,
		AudioDataSize,
		Chunk.AudioDataSize,
		*Chunk.DerivedDataKey
	);
#endif

	if (OutChunkData)
	{
		if (*OutChunkData == NULL)
		{
			*OutChunkData = static_cast<uint8*>(FMemory::Malloc(ChunkSize));
		}
		Ar.Serialize(*OutChunkData, ChunkSize);
	}

	if (FPlatformCompressionUtilities::IsCurrentPlatformUsingStreamCaching())
	{
		return AudioDataSize;
	}
	else
	{
		return ChunkSize;
	}
}

int32 FStreamedAudioPlatformData::GetChunkFromDDC(int32 ChunkIndex, uint8** OutChunkData, bool bMakeSureChunkIsLoaded /* = false */)
{
	if (Chunks.Num() == 0)
	{
		UE_LOG(LogAudioDerivedData, Warning, TEXT("No streamed audio chunks found!"));
		return 0;
	}

	// if bMakeSureChunkIsLoaded is true, we don't actually know the size of the chunk's bulk data,
	// so it will need to be allocated in GetCopy.
	check(!bMakeSureChunkIsLoaded || (OutChunkData && (*OutChunkData == nullptr)));

	bool bCachedChunk = false;
	check(ChunkIndex < Chunks.Num());
	FStreamedAudioChunk& Chunk = Chunks[ChunkIndex];

	int32 ChunkDataSize = 0;

#if WITH_EDITORONLY_DATA
	TArray<uint8> TempData;

	// Begin async DDC retrieval
	FDerivedDataCacheInterface& DDC = GetDerivedDataCacheRef();
	uint32 AsyncHandle = 0;
	if (!Chunk.DerivedDataKey.IsEmpty())
	{
		if (bMakeSureChunkIsLoaded)
		{
			if (DDC.GetSynchronous(*Chunk.DerivedDataKey, TempData, TEXT("Unknown SoundWave"_SV)))
			{
				ChunkDataSize = DeserializeChunkFromDDC(TempData, Chunk, ChunkIndex, OutChunkData);
			}
		}
		else
		{
			AsyncHandle = DDC.GetAsynchronous(*Chunk.DerivedDataKey, TEXT("Unknown SoundWave"_SV));
		}
	}

	// Wait for async DDC to complete
	// TODO: Not necessary since bMakeSureChunkIsLoaded was introduced. 
	// Also, if you do this- why not just call DDC.GetSynchronous?
// 	if (Chunk.DerivedDataKey.IsEmpty() == false)
// 	{
// 		DDC.WaitAsynchronousCompletion(AsyncHandle);
// 		if (DDC.GetAsynchronousResults(AsyncHandle, TempData))
// 		{
// 			ChunkDataSize = DeserializeChunkFromDDC(TempData, Chunk, ChunkIndex, OutChunkData);
// 		}
// 	}
#else // #if WITH_EDITORONLY_DATA
	// Load chunk from bulk data if available. If the chunk is not loaded, GetCopy will load it synchronously.
	if (Chunk.BulkData.IsBulkDataLoaded() || bMakeSureChunkIsLoaded)
	{
		if (OutChunkData)
		{
			ChunkDataSize = Chunk.BulkData.GetBulkDataSize();
			Chunk.BulkData.GetCopy((void**)OutChunkData, true);
		}
	}
#endif // #if WITH_EDITORONLY_DATA
	return ChunkDataSize;
}

#if WITH_EDITORONLY_DATA
bool FStreamedAudioPlatformData::AreDerivedChunksAvailable() const
{
	bool bChunksAvailable = true;
	FDerivedDataCacheInterface& DDC = GetDerivedDataCacheRef();
	for (int32 ChunkIndex = 0; bChunksAvailable && ChunkIndex < Chunks.Num(); ++ChunkIndex)
	{
		const FStreamedAudioChunk& Chunk = Chunks[ChunkIndex];
		if (Chunk.DerivedDataKey.IsEmpty() == false)
		{
			bChunksAvailable = DDC.CachedDataProbablyExists(*Chunk.DerivedDataKey);
		}
	}
	return bChunksAvailable;
}
#endif // #if WITH_EDITORONLY_DATA

void FStreamedAudioPlatformData::Serialize(FArchive& Ar, USoundWave* Owner)
{
#if WITH_EDITORONLY_DATA
	if (Owner)
	{
		Owner->RawDataCriticalSection.Lock();
	}
#endif

	Ar << NumChunks;
	Ar << AudioFormat;

	if (Ar.IsLoading())
	{
		Chunks.Empty(NumChunks);
		for (int32 ChunkIndex = 0; ChunkIndex < NumChunks; ++ChunkIndex)
		{
			Chunks.Add(new FStreamedAudioChunk());
		}
	}

	for (int32 ChunkIndex = 0; ChunkIndex < Chunks.Num(); ++ChunkIndex)
	{
		Chunks[ChunkIndex].Serialize(Ar, Owner, ChunkIndex);
	}
	

#if WITH_EDITORONLY_DATA
	if (Owner)
	{
		Owner->RawDataCriticalSection.Unlock();
	}
#endif
}

/**
 * Helper class to display a status update message in the editor.
 */
class FAudioStatusMessageContext : FScopedSlowTask
{
public:

	/**
	 * Updates the status message displayed to the user.
	 */
	explicit FAudioStatusMessageContext( const FText& InMessage )
	 : FScopedSlowTask(1, InMessage, GIsEditor && !IsRunningCommandlet())
	{
		UE_LOG(LogAudioDerivedData, Display, TEXT("%s"), *InMessage.ToString());
	}
};

/**
* Function used for resampling a USoundWave's WaveData, which is assumed to be int16 here:
*/
static void ResampleWaveData(TArray<uint8>& WaveData, size_t& NumBytes, int32 NumChannels, float SourceSampleRate, float DestinationSampleRate)
{
	double StartTime = FPlatformTime::Seconds();

	// Set up temporary output buffers:
	Audio::AlignedFloatBuffer ResamplerInputData;
	Audio::AlignedFloatBuffer ResamplerOutputData;

	int32 NumSamples = NumBytes / sizeof(int16);

	check(WaveData.Num() == NumBytes);
	check(NumSamples == NumBytes / 2);

	// Convert wav data from int16 to float:
	ResamplerInputData.AddUninitialized(NumSamples);
	int16* InputData = (int16*) WaveData.GetData();

	for (int32 Index = 0; Index < NumSamples; Index++)
	{
		ResamplerInputData[Index] = ((float)InputData[Index]) / 32767.0f;
	}

	// set up converter input params:
	Audio::FResamplingParameters ResamplerParams = {
		Audio::EResamplingMethod::BestSinc,
		NumChannels,
		SourceSampleRate,
		DestinationSampleRate,
		ResamplerInputData
	};

	// Allocate enough space in output buffer for the resulting audio:
	ResamplerOutputData.AddUninitialized(Audio::GetOutputBufferSize(ResamplerParams));
	Audio::FResamplerResults ResamplerResults;
	ResamplerResults.OutBuffer = &ResamplerOutputData;

	// Resample:
	if (Audio::Resample(ResamplerParams, ResamplerResults))
	{
		// resize WaveData buffer and convert back to int16:
		int32 NumSamplesGenerated = ResamplerResults.OutputFramesGenerated * NumChannels;
		WaveData.SetNum(NumSamplesGenerated * sizeof(int16));
		InputData = (int16*) WaveData.GetData();

		// Detect if the output will clip:
		float MaxValue = 0.0f;
		for (int32 Index = 0; Index < NumSamplesGenerated; Index++)
		{
			const float AbsSample = FMath::Abs(ResamplerOutputData[Index]);
			if (AbsSample > MaxValue)
			{
				MaxValue = AbsSample;
			}
		}

		// If the output will clip, normalize it.
		if (MaxValue > 1.0f)
		{
			UE_LOG(LogAudioDerivedData, Display, TEXT("Audio clipped during resampling: This asset will be normalized by a factor of 1/%f. Consider attenuating the above asset."), MaxValue);

			for (int32 Index = 0; Index < NumSamplesGenerated; Index++)
			{
				ResamplerOutputData[Index] /= MaxValue;
			}
		}

		for (int32 Index = 0; Index < NumSamplesGenerated; Index++)
		{
			InputData[Index] = (int16)(ResamplerOutputData[Index] * 32767.0f);
		}

		NumBytes = NumSamplesGenerated * sizeof(int16);
	}
	else
	{
		UE_LOG(LogAudioDerivedData, Error, TEXT("Resampling operation failed."));
	}

	double TimeDelta = FPlatformTime::Seconds() - StartTime;
	UE_LOG(LogAudioDerivedData, Display, TEXT("Resampling file from %f to %f took %f seconds."), SourceSampleRate, DestinationSampleRate, TimeDelta);
}

/**
 * Cook a simple mono or stereo wave
 */
static void CookSimpleWave(USoundWave* SoundWave, FName FormatName, const IAudioFormat& Format, TArray<uint8>& Output, const FPlatformAudioCookOverrides* CompressionOverrides)
{
	FWaveModInfo WaveInfo;
	TArray<uint8> Input;
	check(!Output.Num());

	bool bWasLocked = false;

	int32 WaveSampleRate = 0;

	// Don't need to cook source buses
	if (USoundSourceBus* SourceBus = Cast<USoundSourceBus>(SoundWave))
	{
		return;
	}

#if WITH_EDITORONLY_DATA
	FScopeLock ScopeLock(&SoundWave->RawDataCriticalSection);
#endif

	SoundWave->RawData.ForceBulkDataResident();

	// check if there is any raw sound data
	if( SoundWave->RawData.GetBulkDataSize() > 0 )
	{

		// Lock raw wave data.
		const uint8* RawWaveData = ( const uint8* )SoundWave->RawData.LockReadOnly();
		bWasLocked = true;
		int32 RawDataSize = SoundWave->RawData.GetBulkDataSize();

		// parse the wave data
		if( !WaveInfo.ReadWaveHeader( RawWaveData, RawDataSize, 0 ) )
		{
			UE_LOG(LogAudioDerivedData, Warning, TEXT( "Only mono or stereo 16 bit waves allowed: %s (%d bytes)" ), *SoundWave->GetFullName(), RawDataSize );
		}
		else
		{
			Input.AddUninitialized(WaveInfo.SampleDataSize);
			FMemory::Memcpy(Input.GetData(), WaveInfo.SampleDataStart, WaveInfo.SampleDataSize);
		}
	}

	if(!Input.Num())
	{
		UE_LOG(LogAudioDerivedData, Warning, TEXT( "Can't cook %s because there is no source LPCM data" ), *SoundWave->GetFullName() );
	}
	else
	{
		WaveSampleRate = *WaveInfo.pSamplesPerSec;

		float SampleRateOverride = -1.0f;

		if (CompressionOverrides && CompressionOverrides->bResampleForDevice)
		{
			SampleRateOverride = SoundWave->GetSampleRateForCompressionOverrides(CompressionOverrides);
			// Check for a platform resample override here and resample if neccesary:
			if (SampleRateOverride > 0 && SampleRateOverride != (float)WaveSampleRate)
			{
				size_t TotalDataSize = WaveInfo.SampleDataSize;

				ResampleWaveData(Input, TotalDataSize, *WaveInfo.pChannels, WaveSampleRate, SampleRateOverride);
				WaveSampleRate = SampleRateOverride;
				WaveInfo.SampleDataSize = TotalDataSize;
			}
		}

		FSoundQualityInfo QualityInfo = { 0 };
		if (CompressionOverrides)
		{
			float ModifiedCompressionQuality = (float)SoundWave->CompressionQuality * CompressionOverrides->CompressionQualityModifier;
			if (ModifiedCompressionQuality >= 1.0f)
			{
				QualityInfo.Quality = FMath::FloorToInt(ModifiedCompressionQuality);
				UE_LOG(LogAudioDerivedData, Display, TEXT("Compression Quality for %s will be modified from %d to %d."), *SoundWave->GetFullName(), SoundWave->CompressionQuality, QualityInfo.Quality);
			}
			else
			{
				QualityInfo.Quality = SoundWave->CompressionQuality;
			}
		}
		else
		{
			QualityInfo.Quality = SoundWave->CompressionQuality;
		}

		QualityInfo.NumChannels = *WaveInfo.pChannels;
		QualityInfo.SampleRate = WaveSampleRate;
		QualityInfo.SampleDataSize = Input.Num();
		// without overrides, we don't know the target platform's name to be able to look up, and passing nullptr will use editor platform's settings, which could be wrong
		// @todo: Pass in TargetPlatform/PlatformName maybe?
		QualityInfo.bStreaming = SoundWave->IsStreaming(CompressionOverrides ? *CompressionOverrides : FPlatformAudioCookOverrides());
		QualityInfo.DebugName = SoundWave->GetFullName();

		// Cook the data.
		if(Format.Cook(FormatName, Input, QualityInfo, Output))
		{
			SoundWave->SetSampleRate(QualityInfo.SampleRate);

			if (SoundWave->NumChannels != *WaveInfo.pChannels)
			{
				UE_LOG(LogAudioDerivedData, Warning, TEXT( "Updated SoundWave->NumChannels from %d to %u while cooking %s." ), SoundWave->NumChannels, *WaveInfo.pChannels, *SoundWave->GetFullName());
				SoundWave->NumChannels = *WaveInfo.pChannels;
			}
			if (SoundWave->RawPCMDataSize != Input.Num())
			{
				UE_LOG(LogAudioDerivedData, Display, TEXT( "Updated SoundWave->RawPCMDataSize from %d to %d while cooking %s." ), SoundWave->RawPCMDataSize, Input.Num(), *SoundWave->GetFullName() );
				SoundWave->RawPCMDataSize = Input.Num();
			}

			// Update duration if it has changed > 1ms
			float NewDuration = (float)SoundWave->RawPCMDataSize / (QualityInfo.SampleRate * sizeof(int16) * SoundWave->NumChannels);
			if (!FMath::IsNearlyEqual(SoundWave->Duration, NewDuration, 0.001f))
			{
				UE_LOG(LogAudioDerivedData, Display, TEXT( "Updated SoundWave->Duration from %f to %f while cooking %s." ), SoundWave->Duration, NewDuration, *SoundWave->GetFullName());
				SoundWave->Duration = NewDuration;
			}
		}
	}
	if (bWasLocked)
	{
		SoundWave->RawData.Unlock();
	}
}

/**
 * Cook a multistream (normally 5.1) wave
 */
static void CookSurroundWave( USoundWave* SoundWave, FName FormatName, const IAudioFormat& Format, TArray<uint8>& Output, const FPlatformAudioCookOverrides* CompressionOverrides)
{
	check(!Output.Num());
#if WITH_EDITORONLY_DATA
	int32					i;
	size_t					SampleDataSize = 0;
	FWaveModInfo			WaveInfo;
	TArray<TArray<uint8> >	SourceBuffers;
	TArray<int32>			RequiredChannels;

#if WITH_EDITORONLY_DATA
	FScopeLock ScopeLock(&SoundWave->RawDataCriticalSection);
#endif

	uint8* RawWaveData = ( uint8* )SoundWave->RawData.Lock( LOCK_READ_ONLY );
	if (RawWaveData == nullptr)
	{
		SoundWave->RawData.Unlock();

		UE_LOG(LogAudioDerivedData, Display, TEXT("No raw wave data for: %s"), *SoundWave->GetFullName());
		return;
	}

	// Front left channel is the master
	static_assert(SPEAKER_FrontLeft == 0, "Front-left speaker must be first.");

	// loop through channels to find which have data and which are required
	for (i = 0; i < SPEAKER_Count; i++)
	{
		FWaveModInfo WaveInfoInner;

		// Only mono files allowed
		if (WaveInfoInner.ReadWaveHeader(RawWaveData, SoundWave->ChannelSizes[i], SoundWave->ChannelOffsets[i])
			&& *WaveInfoInner.pChannels == 1)
		{
			if (SampleDataSize == 0)
			{
				// keep wave info/size of first channel data we find
				WaveInfo = WaveInfoInner;
				SampleDataSize = WaveInfo.SampleDataSize;
			}
			switch (i)
			{
				case SPEAKER_FrontLeft:
				case SPEAKER_FrontRight:
				case SPEAKER_LeftSurround:
				case SPEAKER_RightSurround:
					// Must have quadraphonic surround channels
					RequiredChannels.AddUnique(SPEAKER_FrontLeft);
					RequiredChannels.AddUnique(SPEAKER_FrontRight);
					RequiredChannels.AddUnique(SPEAKER_LeftSurround);
					RequiredChannels.AddUnique(SPEAKER_RightSurround);
					break;
				case SPEAKER_FrontCenter:
				case SPEAKER_LowFrequency:
					// Must have 5.1 surround channels
					for (int32 Channel = SPEAKER_FrontLeft; Channel <= SPEAKER_RightSurround; Channel++)
					{
						RequiredChannels.AddUnique(Channel);
					}
					break;
				case SPEAKER_LeftBack:
				case SPEAKER_RightBack:
					// Must have all previous channels
					for (int32 Channel = 0; Channel < i; Channel++)
					{
						RequiredChannels.AddUnique(Channel);
					}
					break;
				default:
					// unsupported channel count
					break;
			}
		}
	}

	if (SampleDataSize != 0)
	{
		int32 ChannelCount = 0;
		// Extract all the info for channels or insert blank data
		for( i = 0; i < SPEAKER_Count; i++ )
		{
			FWaveModInfo WaveInfoInner;
			if( WaveInfoInner.ReadWaveHeader( RawWaveData, SoundWave->ChannelSizes[ i ], SoundWave->ChannelOffsets[ i ] )
				&& *WaveInfoInner.pChannels == 1 )
			{
				ChannelCount++;
				TArray<uint8>& Input = *new (SourceBuffers) TArray<uint8>;
				Input.AddUninitialized(WaveInfoInner.SampleDataSize);
				FMemory::Memcpy(Input.GetData(), WaveInfoInner.SampleDataStart, WaveInfoInner.SampleDataSize);
				SampleDataSize = FMath::Min<uint32>(WaveInfoInner.SampleDataSize, SampleDataSize);
			}
			else if (RequiredChannels.Contains(i))
			{
				// Add an empty channel for cooking
				ChannelCount++;
				TArray<uint8>& Input = *new (SourceBuffers) TArray<uint8>;
				Input.AddZeroed(SampleDataSize);
			}
		}

		// Only allow the formats that can be played back through
		if( ChannelCount == 4 || ChannelCount == 6 || ChannelCount == 7 || ChannelCount == 8 )
		{
			int32 WaveSampleRate = *WaveInfo.pSamplesPerSec;

			// Check for a platform resample override here and resample if neccesary:
			float SampleRateOverride = -1.0f;
			if (CompressionOverrides && CompressionOverrides->bResampleForDevice)
			{
				SampleRateOverride = SoundWave->GetSampleRateForCompressionOverrides(CompressionOverrides);
			}

			if (SampleRateOverride > 0 && SampleRateOverride != (float)WaveSampleRate)
			{
				for (int32 ChannelIndex = 0; ChannelIndex < ChannelCount; ChannelIndex++)
				{
					size_t DataSize = SourceBuffers[ChannelIndex].Num();
					ResampleWaveData(SourceBuffers[ChannelIndex], DataSize, 1, WaveSampleRate, SampleRateOverride);
				}

				// Since each channel is resampled independently, we may have slightly different sample counts in each channel.
				// To counter this, we truncate or zero-pad every non-zero channel's buffer to the zeroth channel's length.
				int32 SizeOfZerothChannel = SourceBuffers[0].Num();

				for (int32 ChannelIndex = 1; ChannelIndex < ChannelCount; ChannelIndex++)
				{
					TArray<uint8>& SourceBuffer = SourceBuffers[ChannelIndex];

					if (SourceBuffer.Num() != SizeOfZerothChannel)
					{	
						UE_LOG(LogAudioDerivedData, Display, TEXT("Fixing up channel %d from %d to %d samples."), ChannelIndex, SizeOfZerothChannel, SourceBuffer.Num());
						SourceBuffer.SetNumZeroed(SizeOfZerothChannel);
					}
				}

				WaveSampleRate = SampleRateOverride;
				SampleDataSize = SizeOfZerothChannel;
			}

			UE_LOG(LogAudioDerivedData, Display, TEXT("Cooking %d channels for: %s"), ChannelCount, *SoundWave->GetFullName());

			FSoundQualityInfo QualityInfo = { 0 };

			float CompressionQualityModifier = 1.0f;
			if (CompressionOverrides)
			{
				CompressionQualityModifier = CompressionOverrides->CompressionQualityModifier;
			}

			float ModifiedCompressionQuality = (float)SoundWave->CompressionQuality;

			if (!FMath::IsNearlyEqual(CompressionQualityModifier, 1.0f))
			{
				ModifiedCompressionQuality = (float)SoundWave->CompressionQuality * CompressionQualityModifier;
			}
			
			if (ModifiedCompressionQuality >= 1.0f)
			{
				QualityInfo.Quality = FMath::FloorToInt(ModifiedCompressionQuality);
			}
			else
			{
				QualityInfo.Quality = SoundWave->CompressionQuality;
			}

			QualityInfo.NumChannels = ChannelCount;
			QualityInfo.SampleRate = WaveSampleRate;
			QualityInfo.SampleDataSize = SampleDataSize;
			QualityInfo.bStreaming = SoundWave->IsStreaming(CompressionOverrides ? *CompressionOverrides : FPlatformAudioCookOverrides());
			QualityInfo.DebugName = SoundWave->GetFullName();
			//@todo tighten up the checking for empty results here
			if(Format.CookSurround(FormatName, SourceBuffers, QualityInfo, Output))
			{
				SoundWave->SetSampleRate(QualityInfo.SampleRate);

				if (SoundWave->NumChannels != ChannelCount)
				{
					UE_LOG(LogAudioDerivedData, Warning, TEXT( "Updated SoundWave->NumChannels during cooking %s." ), *SoundWave->GetFullName() );
					SoundWave->NumChannels = ChannelCount;
				}

				const int32 PCMDataSize = SampleDataSize * ChannelCount;
				if (SoundWave->RawPCMDataSize != PCMDataSize)
				{
					UE_LOG(LogAudioDerivedData, Display, TEXT("Updated SoundWave->RawPCMDataSize during cooking %s."), *SoundWave->GetFullName() );
					SoundWave->RawPCMDataSize = PCMDataSize;
				}

				const float NewDuration = (float)SampleDataSize / (QualityInfo.SampleRate * sizeof(int16));
				if (SoundWave->Duration != NewDuration)
				{
					UE_LOG(LogAudioDerivedData, Display, TEXT("Updated SoundWave->Duration during cooking %s."), *SoundWave->GetFullName());
					SoundWave->Duration = NewDuration;
				}
			}
			else
			{
				UE_LOG(LogAudioDerivedData, Warning, TEXT("Cooking surround sound failed: %s"), *SoundWave->GetPathName());
			}
		}
		else
		{
			UE_LOG(LogAudioDerivedData, Warning, TEXT( "No format available for a %d channel surround sound: %s" ), ChannelCount, *SoundWave->GetFullName() );
		}
	}
	else
	{
		UE_LOG(LogAudioDerivedData, Warning, TEXT( "Cooking surround sound failed: %s" ), *SoundWave->GetPathName() );
	}
	SoundWave->RawData.Unlock();
#endif
}

FDerivedAudioDataCompressor::FDerivedAudioDataCompressor(USoundWave* InSoundNode, FName InBaseFormat, FName InHashedFormat, const FPlatformAudioCookOverrides* InCompressionOverrides)
	: SoundNode(InSoundNode)
	, BaseFormat(InBaseFormat)
	, HashedFormat(InHashedFormat)
	, Compressor(NULL)
	, CompressionOverrides(InCompressionOverrides)
{
	ITargetPlatformManagerModule* TPM = GetTargetPlatformManager();
	if (TPM)
	{
		Compressor = TPM->FindAudioFormat(BaseFormat);
	}
}

FString FDerivedAudioDataCompressor::GetPluginSpecificCacheKeySuffix() const
{
	int32 FormatVersion = 0xffff; // if the compressor is NULL, this will be used as the version...and in that case we expect everything to fail anyway
	if (Compressor)
	{
		FormatVersion = (int32)Compressor->GetVersion(BaseFormat);
	}

	check(SoundNode->CompressedDataGuid.IsValid());
	FString FormatHash = HashedFormat.ToString().ToUpper();
	return FString::Printf(TEXT("%s_%04X_%s"), *FormatHash, FormatVersion, *SoundNode->CompressedDataGuid.ToString());
}


bool FDerivedAudioDataCompressor::Build(TArray<uint8>& OutData)
{
#if WITH_EDITORONLY_DATA
	if (!Compressor)
	{
		UE_LOG(LogAudioDerivedData, Warning, TEXT("Could not find audio format to cook: %s"), *BaseFormat.ToString());
		return false;
	}

	FFormatNamedArguments Args;
	Args.Add(TEXT("AudioFormat"), FText::FromName(BaseFormat));
	Args.Add(TEXT("Hash"), FText::FromName(HashedFormat));
	Args.Add(TEXT("SoundNodeName"), FText::FromString(SoundNode->GetName()));
	FAudioStatusMessageContext StatusMessage(FText::Format(NSLOCTEXT("Engine", "BuildingCompressedAudioTaskStatus", "Building compressed audio format {AudioFormat} hash {Hash} wave {SoundNodeName}..."), Args));


#if FORCE_RESAMPLE
	FPlatformAudioCookOverrides NewCompressionOverrides = FPlatformAudioCookOverrides();
	NewCompressionOverrides.bResampleForDevice = true;
	if (CompressionOverrides == nullptr)
	{
		CompressionOverrides = &NewCompressionOverrides;
	}
#endif

	if (!SoundNode->ChannelSizes.Num())
	{
		check(!SoundNode->ChannelOffsets.Num());
		CookSimpleWave(SoundNode, BaseFormat, *Compressor, OutData, CompressionOverrides);
	}
	else
	{
		check(SoundNode->ChannelOffsets.Num() == SPEAKER_Count);
		check(SoundNode->ChannelSizes.Num() == SPEAKER_Count);
		CookSurroundWave(SoundNode, BaseFormat, *Compressor, OutData, CompressionOverrides);
	}
#endif
	return OutData.Num() > 0;
}

/*---------------------------------------
	USoundWave Derived Data functions
---------------------------------------*/

void USoundWave::CleanupCachedRunningPlatformData()
{
	if (RunningPlatformData != NULL)
	{
		delete RunningPlatformData;
		RunningPlatformData = NULL;
	}
}


void USoundWave::SerializeCookedPlatformData(FArchive& Ar)
{
	if (IsTemplate())
	{
		return;
	}

	DECLARE_SCOPE_CYCLE_COUNTER( TEXT("USoundWave::SerializeCookedPlatformData"), STAT_SoundWave_SerializeCookedPlatformData, STATGROUP_LoadTime );

#if WITH_EDITORONLY_DATA
	if (Ar.IsCooking() && Ar.IsPersistent())
	{
		check(!Ar.CookingTarget()->IsServerOnly());

		FName PlatformFormat = Ar.CookingTarget()->GetWaveFormat(this);
		const FPlatformAudioCookOverrides* CompressionOverrides = FPlatformCompressionUtilities::GetCookOverrides(*Ar.CookingTarget()->IniPlatformName());
		FString DerivedDataKey;

		GetStreamedAudioDerivedDataKeySuffix(*this, PlatformFormat, CompressionOverrides, DerivedDataKey);

		FStreamedAudioPlatformData *PlatformDataToSave = CookedPlatformData.FindRef(DerivedDataKey);

		if (PlatformDataToSave == NULL)
		{
			PlatformDataToSave = new FStreamedAudioPlatformData();
			PlatformDataToSave->Cache(*this, CompressionOverrides, PlatformFormat, EStreamedAudioCacheFlags::InlineChunks | EStreamedAudioCacheFlags::Async);

			CookedPlatformData.Add(DerivedDataKey, PlatformDataToSave);
		}

		PlatformDataToSave->FinishCache();
		PlatformDataToSave->Serialize(Ar, this);
	}
	else
#endif // #if WITH_EDITORONLY_DATA
	{
		check(!FPlatformProperties::IsServerOnly());

		CleanupCachedRunningPlatformData();
		check(RunningPlatformData == NULL);

		// Don't serialize streaming data on servers, even if this platform supports streaming in theory
		RunningPlatformData = new FStreamedAudioPlatformData();
		RunningPlatformData->Serialize(Ar, this);
	}
}

#if WITH_EDITORONLY_DATA
void USoundWave::CachePlatformData(bool bAsyncCache)
{
	FString DerivedDataKey;
	FName AudioFormat = GetWaveFormatForRunningPlatform(*this);
	const FPlatformAudioCookOverrides* CompressionOverrides = GetCookOverridesForRunningPlatform();
	GetStreamedAudioDerivedDataKey(*this, AudioFormat, CompressionOverrides, DerivedDataKey);

	if (RunningPlatformData == NULL || RunningPlatformData->DerivedDataKey != DerivedDataKey)
	{
		if (RunningPlatformData == NULL)
		{
			RunningPlatformData = new FStreamedAudioPlatformData();
		}

		RunningPlatformData->Cache(*this, CompressionOverrides, AudioFormat, bAsyncCache ? EStreamedAudioCacheFlags::Async : EStreamedAudioCacheFlags::None);
	}
}

void USoundWave::BeginCachePlatformData()
{
	CachePlatformData(true);

#if WITH_EDITOR
	// enable caching in postload for derived data cache commandlet and cook by the book
	ITargetPlatformManagerModule* TPM = GetTargetPlatformManager();
	if (TPM && (TPM->RestrictFormatsToRuntimeOnly() == false))
	{
		TArray<ITargetPlatform*> Platforms = TPM->GetActiveTargetPlatforms();
		// Cache for all the audio formats that the cooking target requires
		for (int32 FormatIndex = 0; FormatIndex < Platforms.Num(); FormatIndex++)
		{
			BeginCacheForCookedPlatformData(Platforms[FormatIndex]);
		}
	}
#endif
}
#if WITH_EDITOR

void USoundWave::BeginCacheForCookedPlatformData(const ITargetPlatform *TargetPlatform)
{
	const FPlatformAudioCookOverrides* CompressionOverrides = FPlatformCompressionUtilities::GetCookOverrides(*TargetPlatform->IniPlatformName());

	if (TargetPlatform->SupportsFeature(ETargetPlatformFeatures::AudioStreaming) && IsStreaming(*CompressionOverrides))
	{
		// Retrieve format to cache for targetplatform.
		FName PlatformFormat = TargetPlatform->GetWaveFormat(this);
		uint32 CacheFlags = EStreamedAudioCacheFlags::Async | EStreamedAudioCacheFlags::InlineChunks;


		// If source data is resident in memory then allow the streamed audio to be built
		// in a background thread.
		if (GetCompressedData(PlatformFormat, CompressionOverrides) && GetCompressedData(PlatformFormat, CompressionOverrides)->IsBulkDataLoaded())
		{
			CacheFlags |= EStreamedAudioCacheFlags::AllowAsyncBuild;
		}

		// find format data by comparing derived data keys.
		FString DerivedDataKey;
		GetStreamedAudioDerivedDataKeySuffix(*this, PlatformFormat, CompressionOverrides, DerivedDataKey);

		FStreamedAudioPlatformData *PlatformData = CookedPlatformData.FindRef(DerivedDataKey);

		if (PlatformData == NULL)
		{
			PlatformData = new FStreamedAudioPlatformData();
			PlatformData->Cache(
				*this,
				CompressionOverrides,
				PlatformFormat,
				CacheFlags
				);
			CookedPlatformData.Add(DerivedDataKey, PlatformData);
		}
	}

	Super::BeginCacheForCookedPlatformData(TargetPlatform);
}

bool USoundWave::IsCachedCookedPlatformDataLoaded( const ITargetPlatform* TargetPlatform )
{
	const FPlatformAudioCookOverrides* CompressionOverrides = FPlatformCompressionUtilities::GetCookOverrides(*TargetPlatform->IniPlatformName());

	if (TargetPlatform->SupportsFeature(ETargetPlatformFeatures::AudioStreaming) && IsStreaming(*CompressionOverrides))
	{
		// Retrieve format to cache for targetplatform.
		FName PlatformFormat = TargetPlatform->GetWaveFormat(this);
		// find format data by comparing derived data keys.
		FString DerivedDataKey;
		GetStreamedAudioDerivedDataKeySuffix(*this, PlatformFormat, CompressionOverrides, DerivedDataKey);

		FStreamedAudioPlatformData *PlatformData = CookedPlatformData.FindRef(DerivedDataKey);
		if (PlatformData == NULL)
		{
			// we havne't called begincache
			return false;
		}

		if (PlatformData->AsyncTask && PlatformData->AsyncTask->IsWorkDone())
		{
			PlatformData->FinishCache();
		}

		return PlatformData->IsFinishedCache();
	}
	return true;
}


/**
* Clear all the cached cooked platform data which we have accumulated with BeginCacheForCookedPlatformData calls
* The data can still be cached again using BeginCacheForCookedPlatformData again
*/
void USoundWave::ClearAllCachedCookedPlatformData()
{
	Super::ClearAllCachedCookedPlatformData();

	for (auto It : CookedPlatformData)
	{
		delete It.Value;
	}

	CookedPlatformData.Empty();
}

void USoundWave::ClearCachedCookedPlatformData( const ITargetPlatform* TargetPlatform )
{
	Super::ClearCachedCookedPlatformData(TargetPlatform);

	const FPlatformAudioCookOverrides* CompressionOverrides = FPlatformCompressionUtilities::GetCookOverrides(*TargetPlatform->IniPlatformName());

	if (TargetPlatform->SupportsFeature(ETargetPlatformFeatures::AudioStreaming) && IsStreaming(*CompressionOverrides))
	{
		// Retrieve format to cache for targetplatform.
		FName PlatformFormat = TargetPlatform->GetWaveFormat(this);

		// find format data by comparing derived data keys.
		FString DerivedDataKey;
		GetStreamedAudioDerivedDataKeySuffix(*this, PlatformFormat, CompressionOverrides, DerivedDataKey);


		if ( CookedPlatformData.Contains(DerivedDataKey) )
		{
			FStreamedAudioPlatformData *PlatformData = CookedPlatformData.FindAndRemoveChecked( DerivedDataKey );
			delete PlatformData;
		}
	}
}

void USoundWave::WillNeverCacheCookedPlatformDataAgain()
{
	// this is called after we have finished caching the platform data but before we have saved the data
	// so need to keep the cached platform data around
	Super::WillNeverCacheCookedPlatformDataAgain();

	// TODO: We can clear these arrays if we never need to cook again.
	RawData.RemoveBulkData();
	CompressedFormatData.FlushData();
}
#endif

void USoundWave::FinishCachePlatformData()
{
	if (RunningPlatformData == NULL)
	{
		// begin cache never called
		CachePlatformData();
	}
	else
	{
		// make sure async requests are finished
		RunningPlatformData->FinishCache();
	}

#if DO_CHECK
	// If we're allowing cooked data to be loaded then the derived data key will not have been serialized, so won't match and that's fine
	if (!GAllowCookedDataInEditorBuilds)
	{
		FString DerivedDataKey;
		FName AudioFormat = GetWaveFormatForRunningPlatform(*this);
		const FPlatformAudioCookOverrides* CompressionOverrides = GetCookOverridesForRunningPlatform();
		GetStreamedAudioDerivedDataKey(*this, AudioFormat, CompressionOverrides, DerivedDataKey);

		check(RunningPlatformData->DerivedDataKey == DerivedDataKey);
	}
#endif
}

void USoundWave::ForceRebuildPlatformData()
{
	if (RunningPlatformData)
	{
		const FPlatformAudioCookOverrides* CompressionOverrides = GetCookOverridesForRunningPlatform();
		RunningPlatformData->Cache(
			*this,
			CompressionOverrides,
			GetWaveFormatForRunningPlatform(*this),
			EStreamedAudioCacheFlags::ForceRebuild
			);
	}
}
#endif //WITH_EDITORONLY_DATA
