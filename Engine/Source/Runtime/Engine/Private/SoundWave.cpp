// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "Sound/SoundWave.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/FrameworkObjectVersion.h"
#include "UObject/Package.h"
#include "EngineDefines.h"
#include "Components/AudioComponent.h"
#include "ContentStreaming.h"
#include "ActiveSound.h"
#include "AudioThread.h"
#include "AudioDevice.h"
#include "AudioDecompress.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "AudioDerivedData.h"
#include "SubtitleManager.h"
#include "DerivedDataCacheInterface.h"
#include "EditorFramework/AssetImportData.h"
#include "ProfilingDebugging/CookStats.h"
#include "HAL/LowLevelMemTracker.h"
#include "AudioCompressionSettingsUtils.h"

#if ENABLE_COOK_STATS
namespace SoundWaveCookStats
{
	static FCookStats::FDDCResourceUsageStats UsageStats;
	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
	{
		UsageStats.LogStats(AddStat, TEXT("SoundWave.Usage"), TEXT(""));
	});
}
#endif

ITargetPlatform* USoundWave::GetRunningPlatform()
{
	ITargetPlatformManagerModule* TPM = GetTargetPlatformManager();
	if (TPM)
	{
		return TPM->GetRunningTargetPlatform();
	}
	else
	{
		return nullptr;
	}
}

/*-----------------------------------------------------------------------------
	FStreamedAudioChunk
-----------------------------------------------------------------------------*/

void FStreamedAudioChunk::Serialize(FArchive& Ar, UObject* Owner, int32 ChunkIndex)
{
	DECLARE_SCOPE_CYCLE_COUNTER( TEXT("FStreamedAudioChunk::Serialize"), STAT_StreamedAudioChunk_Serialize, STATGROUP_LoadTime );

	bool bCooked = Ar.IsCooking();
	Ar << bCooked;

	// ChunkIndex 0 is always inline payload, all other chunks are streamed.
	if (ChunkIndex == 0)
	{
		BulkData.SetBulkDataFlags(BULKDATA_ForceInlinePayload);
	}
	else
	{
		BulkData.SetBulkDataFlags(BULKDATA_Force_NOT_InlinePayload);
	}
	BulkData.Serialize(Ar, Owner, ChunkIndex);
	Ar << DataSize;
	Ar << AudioDataSize;

#if WITH_EDITORONLY_DATA
	if (!bCooked)
	{
		Ar << DerivedDataKey;
	}
#endif // #if WITH_EDITORONLY_DATA
}

#if WITH_EDITORONLY_DATA
uint32 FStreamedAudioChunk::StoreInDerivedDataCache(const FString& InDerivedDataKey)
{
	int32 BulkDataSizeInBytes = BulkData.GetBulkDataSize();
	check(BulkDataSizeInBytes > 0);

	TArray<uint8> DerivedData;
	FMemoryWriter Ar(DerivedData, /*bIsPersistent=*/ true);
	Ar << BulkDataSizeInBytes;
	{
		void* BulkChunkData = BulkData.Lock(LOCK_READ_ONLY);
		Ar.Serialize(BulkChunkData, BulkDataSizeInBytes);
		BulkData.Unlock();
	}

	const uint32 Result = DerivedData.Num();
	GetDerivedDataCacheRef().Put(*InDerivedDataKey, DerivedData);
	DerivedDataKey = InDerivedDataKey;
	BulkData.RemoveBulkData();
	return Result;
}
#endif // #if WITH_EDITORONLY_DATA

USoundWave::USoundWave(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Volume = 1.0;
	Pitch = 1.0;
	CompressionQuality = 40;
	SubtitlePriority = DEFAULT_SUBTITLE_PRIORITY;
	ResourceState = ESoundWaveResourceState::NeedsFree;
	SetPrecacheState(ESoundWavePrecacheState::NotStarted);

#if !WITH_EDITOR
	bCachedSampleRateFromPlatformSettings = false;
	bSampleRateManuallyReset = false;
	CachedSampleRateOverride = 0.0f;
#endif //!WITH_EDITOR
}

void USoundWave::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);

	if (!GEngine)
	{
		return;
	}

	if (FAudioDevice* LocalAudioDevice = GEngine->GetMainAudioDevice())
	{
		if (LocalAudioDevice->HasCompressedAudioInfoClass(this) && DecompressionType == DTYPE_Native)
		{
			CumulativeResourceSize.AddDedicatedSystemMemoryBytes(RawPCMDataSize);
		}
		else 
		{
			if (DecompressionType == DTYPE_RealTime && CachedRealtimeFirstBuffer)
			{
				CumulativeResourceSize.AddDedicatedSystemMemoryBytes(MONO_PCM_BUFFER_SIZE * NumChannels);
			}
			
			if (!FPlatformProperties::SupportsAudioStreaming() || !IsStreaming())
			{
				CumulativeResourceSize.AddDedicatedSystemMemoryBytes(GetCompressedDataSize(LocalAudioDevice->GetRuntimeFormat(this)));
			}
		}
	}
}


int32 USoundWave::GetResourceSizeForFormat(FName Format)
{
	return GetCompressedDataSize(Format);
}


FName USoundWave::GetExporterName()
{
#if WITH_EDITORONLY_DATA
	if( ChannelOffsets.Num() > 0 && ChannelSizes.Num() > 0 )
	{
		return( FName( TEXT( "SoundSurroundExporterWAV" ) ) );
	}
#endif // WITH_EDITORONLY_DATA

	return( FName( TEXT( "SoundExporterWAV" ) ) );
}


FString USoundWave::GetDesc()
{
	FString Channels;

	if( NumChannels == 0 )
	{
		Channels = TEXT( "Unconverted" );
	}
#if WITH_EDITORONLY_DATA
	else if( ChannelSizes.Num() == 0 )
	{
		Channels = ( NumChannels == 1 ) ? TEXT( "Mono" ) : TEXT( "Stereo" );
	}
#endif // WITH_EDITORONLY_DATA
	else
	{
		Channels = FString::Printf( TEXT( "%d Channels" ), NumChannels );
	}

	return FString::Printf( TEXT( "%3.2fs %s" ), Duration, *Channels );
}

void USoundWave::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	Super::GetAssetRegistryTags(OutTags);
	
#if WITH_EDITORONLY_DATA
	if (AssetImportData)
	{
		OutTags.Add( FAssetRegistryTag(SourceFileTagName(), AssetImportData->GetSourceData().ToJson(), FAssetRegistryTag::TT_Hidden) );
	}
#endif
}

void USoundWave::Serialize( FArchive& Ar )
{
	LLM_SCOPE(ELLMTag::Audio);

	DECLARE_SCOPE_CYCLE_COUNTER( TEXT("USoundWave::Serialize"), STAT_SoundWave_Serialize, STATGROUP_LoadTime );

	Super::Serialize( Ar );

	bool bCooked = Ar.IsCooking();
	Ar << bCooked;

	if (FPlatformProperties::RequiresCookedData() && !bCooked && Ar.IsLoading())
	{
		UE_LOG(LogAudio, Fatal, TEXT("This platform requires cooked packages, and audio data was not cooked into %s."), *GetFullName());
	}

	Ar.UsingCustomVersion(FFrameworkObjectVersion::GUID);

	if (Ar.IsLoading() && (Ar.UE4Ver() >= VER_UE4_SOUND_COMPRESSION_TYPE_ADDED) && (Ar.CustomVer(FFrameworkObjectVersion::GUID) < FFrameworkObjectVersion::RemoveSoundWaveCompressionName))
	{
		FName DummyCompressionName;
		Ar << DummyCompressionName;
	}

	if (Ar.IsSaving() || Ar.IsCooking())
	{
		bHasVirtualizeWhenSilent = bVirtualizeWhenSilent;
	}

	bool bSupportsStreaming = false;
	if (Ar.IsLoading() && FPlatformProperties::SupportsAudioStreaming())
	{
		bSupportsStreaming = true;
	}
	else if (Ar.IsCooking() && Ar.CookingTarget()->SupportsFeature(ETargetPlatformFeatures::AudioStreaming))
	{
		bSupportsStreaming = true;
	}

	if (bCooked)
	{
		// Only want to cook/load full data if we don't support streaming
		if (!IsStreaming() || !bSupportsStreaming)
		{
			if (Ar.IsCooking())
			{
#if WITH_ENGINE
				TArray<FName> ActualFormatsToSave;
				const ITargetPlatform* CookingTarget = Ar.CookingTarget();
				if (!CookingTarget->IsServerOnly())
				{
					// for now we only support one format per wav
					FName Format = CookingTarget->GetWaveFormat(this);
					const FPlatformAudioCookOverrides* CompressionOverrides = CookingTarget->GetAudioCompressionSettings();

					GetCompressedData(Format, CompressionOverrides); // Get the data from the DDC or build it
					if (CompressionOverrides)
					{
						FString HashedString = *Format.ToString();
						FPlatformAudioCookOverrides::GetHashSuffix(CompressionOverrides, HashedString);
						FName PlatformSpecificFormat = *HashedString;
						ActualFormatsToSave.Add(PlatformSpecificFormat);
					}
					else
					{
						ActualFormatsToSave.Add(Format);
					}
				}
				CompressedFormatData.Serialize(Ar, this, &ActualFormatsToSave);
#endif
			}
			else
			{
				CompressedFormatData.Serialize(Ar, this);
			}
		}
	}
	else
	{
		// only save the raw data for non-cooked packages
		RawData.Serialize( Ar, this );
	}

	Ar << CompressedDataGuid;

	if (IsStreaming())
	{
		if (bCooked)
		{
			// only cook/load streaming data if it's supported
			if (bSupportsStreaming)
			{
				SerializeCookedPlatformData(Ar);
			}
		}

#if WITH_EDITORONLY_DATA	
		if (Ar.IsLoading() && !Ar.IsTransacting() && !bCooked && !GetOutermost()->HasAnyPackageFlags(PKG_ReloadingForCooker))
		{
			BeginCachePlatformData();
		}
#endif // #if WITH_EDITORONLY_DATA

// For non-editor builds, we can immediately cache the sample rate.
#if !WITH_EDITOR
		if (Ar.IsLoading())
		{
			SampleRate = GetSampleRateForCurrentPlatform();
		}
#endif // !WITH_EDITOR
	}
}

/**
 * Prints the subtitle associated with the SoundWave to the console
 */
void USoundWave::LogSubtitle( FOutputDevice& Ar )
{
	FString Subtitle = "";
	for( int32 i = 0; i < Subtitles.Num(); i++ )
	{
		Subtitle += Subtitles[ i ].Text.ToString();
	}

	if( Subtitle.Len() == 0 )
	{
		Subtitle = SpokenText;
	}

	if( Subtitle.Len() == 0 )
	{
		Subtitle = "<NO SUBTITLE>";
	}

	Ar.Logf( TEXT( "Subtitle:  %s" ), *Subtitle );
#if WITH_EDITORONLY_DATA
	Ar.Logf( TEXT( "Comment:   %s" ), *Comment );
#endif // WITH_EDITORONLY_DATA
	Ar.Logf( TEXT("Mature:    %s"), bMature ? TEXT( "Yes" ) : TEXT( "No" ) );
}

float USoundWave::GetSubtitlePriority() const
{ 
	return SubtitlePriority;
};

bool USoundWave::IsAllowedVirtual() const
{
	return bVirtualizeWhenSilent || (Subtitles.Num() > 0);
}

void USoundWave::PostInitProperties()
{
	Super::PostInitProperties();

	if(!IsTemplate())
	{
		InvalidateCompressedData();
	}

#if WITH_EDITORONLY_DATA
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
#endif
}

bool USoundWave::HasCompressedData(FName Format, ITargetPlatform* TargetPlatform) const
{
	if (IsTemplate() || IsRunningDedicatedServer())
	{
		return false;
	}

#if WITH_EDITOR
	const FPlatformAudioCookOverrides* CompressionOverrides = (TargetPlatform) ? TargetPlatform->GetAudioCompressionSettings() : nullptr;
#else
	// TargetPlatform is not available on consoles/mobile, so we have to grab it ourselves:
	const FPlatformAudioCookOverrides* CompressionOverrides = FPlatformCompressionUtilities::GetCookOverridesForCurrentPlatform();
#endif // WITH_EDITOR

	if (CompressionOverrides)
	{
#if WITH_EDITOR
		FName PlatformSpecificFormat;
		FString HashedString = *Format.ToString();
		FPlatformAudioCookOverrides::GetHashSuffix(CompressionOverrides, HashedString);
		PlatformSpecificFormat = *HashedString;
#else
		// on non-editor builds, we cache the concatenated format in a static FName.
		static FName PlatformSpecificFormat;
		static FName CachedFormat;
		if (!Format.IsEqual(CachedFormat))
		{
			FString HashedString = *Format.ToString();
			FPlatformAudioCookOverrides::GetHashSuffix(CompressionOverrides, HashedString);
			PlatformSpecificFormat = *HashedString;

			CachedFormat = Format;
		}
#endif // WITH_EDITOR
		return CompressedFormatData.Contains(PlatformSpecificFormat);
	}
	else
	{
		return CompressedFormatData.Contains(Format);
	}

}

const FPlatformAudioCookOverrides* USoundWave::GetPlatformCompressionOverridesForCurrentPlatform()
{
	return FPlatformCompressionUtilities::GetCookOverridesForCurrentPlatform();
}

FName USoundWave::GetPlatformSpecificFormat(FName Format, const FPlatformAudioCookOverrides* CompressionOverrides)
{
	// Platforms that require compression overrides get concatenated formats.
#if WITH_EDITOR
	FName PlatformSpecificFormat;
	if (CompressionOverrides)
	{
		FString HashedString = *Format.ToString();
		FPlatformAudioCookOverrides::GetHashSuffix(CompressionOverrides, HashedString);
		PlatformSpecificFormat = *HashedString;
	}
	else
	{
		PlatformSpecificFormat = Format;
	}
#else
	if (CompressionOverrides == nullptr)
	{
		CompressionOverrides = GetPlatformCompressionOverridesForCurrentPlatform();
	}

	// Cache the concatenated hash:
	static FName PlatformSpecificFormat;
	static FName CachedFormat;
	if (!Format.IsEqual(CachedFormat))
	{
		if (CompressionOverrides)
		{
			FString HashedString = *Format.ToString();
			FPlatformAudioCookOverrides::GetHashSuffix(CompressionOverrides, HashedString);
			PlatformSpecificFormat = *HashedString;
		}
		else
		{
			PlatformSpecificFormat = Format;
		}

		CachedFormat = Format;
	}

#endif // WITH_EDITOR

	return PlatformSpecificFormat;
}

void USoundWave::BeginGetCompressedData(FName Format, const FPlatformAudioCookOverrides* CompressionOverrides)
{
#if WITH_EDITOR
	if (IsTemplate() || IsRunningDedicatedServer())
	{
		return;
	}

	FName PlatformSpecificFormat = GetPlatformSpecificFormat(Format, CompressionOverrides);

	if (!CompressedFormatData.Contains(PlatformSpecificFormat) && !AsyncLoadingDataFormats.Contains(PlatformSpecificFormat))
	{
		if (GetDerivedDataCache())
		{
			FDerivedAudioDataCompressor* DeriveAudioData = new FDerivedAudioDataCompressor(this, Format, PlatformSpecificFormat, CompressionOverrides);
			uint32 GetHandle = GetDerivedDataCacheRef().GetAsynchronous(DeriveAudioData);
			AsyncLoadingDataFormats.Add(PlatformSpecificFormat, GetHandle);
		}
		else
		{
			UE_LOG(LogAudio, Error, TEXT("Attempt to access the DDC when there is none available on sound '%s', format = %s."), *GetFullName(), *PlatformSpecificFormat.ToString());
		}
	}
#else
	// No async DDC read in non-editor, nothing to precache
#endif
}

FByteBulkData* USoundWave::GetCompressedData(FName Format, const FPlatformAudioCookOverrides* CompressionOverrides)
{
	if (IsTemplate() || IsRunningDedicatedServer())
	{
		return nullptr;
	}
	
	FName PlatformSpecificFormat = GetPlatformSpecificFormat(Format, CompressionOverrides);

	bool bContainedValidData = CompressedFormatData.Contains(PlatformSpecificFormat);
	FByteBulkData* Result = &CompressedFormatData.GetFormat(PlatformSpecificFormat);
	if (!bContainedValidData)
	{
		if (!FPlatformProperties::RequiresCookedData() && GetDerivedDataCache())
		{
			TArray<uint8> OutData;
			bool bDataWasBuilt = false;
			bool bGetSuccessful = false;

			COOK_STAT(auto Timer = SoundWaveCookStats::UsageStats.TimeSyncWork());
#if WITH_EDITOR
			uint32* AsyncHandle = AsyncLoadingDataFormats.Find(PlatformSpecificFormat);
			if (AsyncHandle)
			{
				GetDerivedDataCacheRef().WaitAsynchronousCompletion(*AsyncHandle);
				bGetSuccessful = GetDerivedDataCacheRef().GetAsynchronousResults(*AsyncHandle, OutData, &bDataWasBuilt);
				AsyncLoadingDataFormats.Remove(PlatformSpecificFormat);
			}
			else
#endif
			{
				FDerivedAudioDataCompressor* DeriveAudioData = new FDerivedAudioDataCompressor(this, Format, PlatformSpecificFormat, CompressionOverrides);
				bGetSuccessful = GetDerivedDataCacheRef().GetSynchronous(DeriveAudioData, OutData, &bDataWasBuilt);
			}

			if (bGetSuccessful)
			{
				COOK_STAT(Timer.AddHitOrMiss(bDataWasBuilt ? FCookStats::CallStats::EHitOrMiss::Miss : FCookStats::CallStats::EHitOrMiss::Hit, OutData.Num()));
				Result->Lock(LOCK_READ_WRITE);
				FMemory::Memcpy(Result->Realloc(OutData.Num()), OutData.GetData(), OutData.Num());
				Result->Unlock();
			}
		}
		else
		{
			UE_LOG(LogAudio, Error, TEXT("Attempt to access the DDC when there is none available on sound '%s', format = %s. Should have been cooked."), *GetFullName(), *PlatformSpecificFormat.ToString());
		}
	}
	check(Result);
	return Result->GetBulkDataSize() > 0 ? Result : NULL; // we don't return empty bulk data...but we save it to avoid thrashing the DDC
}

void USoundWave::InvalidateCompressedData()
{
	CompressedDataGuid = FGuid::NewGuid();
	CompressedFormatData.FlushData();
}

void USoundWave::PostLoad()
{
	LLM_SCOPE(ELLMTag::Audio);

	Super::PostLoad();

	if (GetOutermost()->HasAnyPackageFlags(PKG_ReloadingForCooker))
	{
		return;
	}

	bHasVirtualizeWhenSilent = bVirtualizeWhenSilent;

#if WITH_EDITORONLY_DATA
	// Log a warning after loading if the source has effect chains but has channels greater than 2.
	if (SourceEffectChain && SourceEffectChain->Chain.Num() > 0 && NumChannels > 2)
	{
		UE_LOG(LogAudio, Warning, TEXT("Sound Wave '%s' has defined an effect chain but is not mono or stereo."), *GetName());
	}
#endif

	// Don't need to do anything in post load if this is a source bus
	if (this->IsA(USoundSourceBus::StaticClass()))
	{
		return;
	}

	// Compress to whatever formats the active target platforms want
	// static here as an optimization
	ITargetPlatformManagerModule* TPM = GetTargetPlatformManager();
	if (TPM)
	{
		const TArray<ITargetPlatform*>& Platforms = TPM->GetActiveTargetPlatforms();

		for (int32 Index = 0; Index < Platforms.Num(); Index++)
		{
			BeginGetCompressedData(Platforms[Index]->GetWaveFormat(this), Platforms[Index]->GetAudioCompressionSettings());
		}
	}

	// We don't precache default objects and we don't precache in the Editor as the latter will
	// most likely cause us to run out of memory.
	if (!GIsEditor && !IsTemplate( RF_ClassDefaultObject ) && GEngine)
	{
		FAudioDevice* AudioDevice = GEngine->GetMainAudioDevice();
		if (AudioDevice)
		{
			// Upload the data to the hardware, but only if we've precached startup sounds already
			AudioDevice->Precache(this);
		}
		// remove bulk data if no AudioDevice is used and no sounds were initialized
		else if(IsRunningGame())
		{
			RawData.RemoveBulkData();
		}
	}

	// Only add this streaming sound if the platform supports streaming
	if (IsStreaming() && FPlatformProperties::SupportsAudioStreaming())
	{
#if WITH_EDITORONLY_DATA
		FinishCachePlatformData();
#endif // #if WITH_EDITORONLY_DATA
		IStreamingManager::Get().GetAudioStreamingManager().AddStreamingSoundWave(this);
	}

#if WITH_EDITORONLY_DATA
	if (!SourceFilePath_DEPRECATED.IsEmpty() && AssetImportData)
	{
		FAssetImportInfo Info;
		Info.Insert(FAssetImportInfo::FSourceFile(SourceFilePath_DEPRECATED));
		AssetImportData->SourceData = MoveTemp(Info);
	}

	bNeedsThumbnailGeneration = true;
#endif // #if WITH_EDITORONLY_DATA

	INC_FLOAT_STAT_BY( STAT_AudioBufferTime, Duration );
	INC_FLOAT_STAT_BY( STAT_AudioBufferTimeChannels, NumChannels * Duration );
}

void USoundWave::BeginDestroy()
{
	Super::BeginDestroy();

	// Flag that this sound wave is beginning destroying. For procedural sound waves, this will ensure
	// the audio render thread stops the sound before GC hits.
	bIsBeginDestroy = true;

#if WITH_EDITOR
	// Flush any async results so we dont leak them in the DDC
	if (GetDerivedDataCache() && AsyncLoadingDataFormats.Num() > 0)
	{
		TArray<uint8> OutData;
		for (auto AsyncLoadIt = AsyncLoadingDataFormats.CreateConstIterator(); AsyncLoadIt; ++AsyncLoadIt)
		{
			uint32 AsyncHandle = AsyncLoadIt.Value();
			GetDerivedDataCacheRef().WaitAsynchronousCompletion(AsyncHandle);
			GetDerivedDataCacheRef().GetAsynchronousResults(AsyncHandle, OutData);
		}

		AsyncLoadingDataFormats.Empty();
	}
#endif
}

void USoundWave::InitAudioResource( FByteBulkData& CompressedData )
{
	if( !ResourceSize )
	{
		// Grab the compressed vorbis data from the bulk data
		ResourceSize = CompressedData.GetBulkDataSize();
		if( ResourceSize > 0 )
		{
			check(!ResourceData);
			CompressedData.GetCopy( ( void** )&ResourceData, true );
		}
	}
}

bool USoundWave::InitAudioResource(FName Format)
{
	if( !ResourceSize && (!FPlatformProperties::SupportsAudioStreaming() || !IsStreaming()) )
	{
		FByteBulkData* Bulk = GetCompressedData(Format, GetPlatformCompressionOverridesForCurrentPlatform());
		if (Bulk)
		{
			ResourceSize = Bulk->GetBulkDataSize();
			check(ResourceSize > 0);
			check(!ResourceData);
			Bulk->GetCopy((void**)&ResourceData, true);
		}
	}

	return ResourceSize > 0;
}

void USoundWave::RemoveAudioResource()
{
	if(ResourceData)
	{
		FMemory::Free(ResourceData);
		ResourceSize = 0;
		ResourceData = NULL;
	}
}

#if WITH_EDITOR

float USoundWave::GetSampleRateForTargetPlatform(const ITargetPlatform* TargetPlatform)
{
	const FPlatformAudioCookOverrides* Overrides = TargetPlatform->GetAudioCompressionSettings();
	if (Overrides)
	{
		return GetSampleRateForCompressionOverrides(Overrides);
	}
	else
	{
		return -1.0f;
	}
}

void USoundWave::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	static FName CompressionQualityFName = FName( TEXT( "CompressionQuality" ) );
	static FName StreamingFName = GET_MEMBER_NAME_CHECKED(USoundWave, bStreaming);

	// Prevent constant re-compression of SoundWave while properties are being changed interactively
	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		UProperty* PropertyThatChanged = PropertyChangedEvent.Property;
		// Regenerate on save any compressed sound formats
		if ( PropertyThatChanged && PropertyThatChanged->GetFName() == CompressionQualityFName )
		{
			InvalidateCompressedData();
			FreeResources();
			UpdatePlatformData();
			MarkPackageDirty();
		}
		else if (PropertyThatChanged && PropertyThatChanged->GetFName() == StreamingFName)
		{
			FreeResources();
			UpdatePlatformData();
			MarkPackageDirty();
		}
	}
}
#endif // WITH_EDITOR

void USoundWave::FreeResources()
{
	check(IsInAudioThread());

	// Housekeeping of stats
	DEC_FLOAT_STAT_BY( STAT_AudioBufferTime, Duration );
	DEC_FLOAT_STAT_BY( STAT_AudioBufferTimeChannels, NumChannels * Duration );

	// GEngine is NULL during script compilation and GEngine->Client and its audio device might be
	// destroyed first during the exit purge.
	if( GEngine && !GExitPurge )
	{
		// Notify the audio device to free the bulk data associated with this wave.
		FAudioDeviceManager* AudioDeviceManager = GEngine->GetAudioDeviceManager();
		if (AudioDeviceManager)
		{
			AudioDeviceManager->StopSoundsUsingResource(this);
			AudioDeviceManager->FreeResource(this);
		}
	}

	if (CachedRealtimeFirstBuffer)
	{
		FMemory::Free(CachedRealtimeFirstBuffer);
		CachedRealtimeFirstBuffer = nullptr;
	}

	// Just in case the data was created but never uploaded
	if (RawPCMData)
	{
		FMemory::Free(RawPCMData);
		RawPCMData = nullptr;
	}

	// Remove the compressed copy of the data
	RemoveAudioResource();

	// Stat housekeeping
	DEC_DWORD_STAT_BY(STAT_AudioMemorySize, TrackedMemoryUsage);
	DEC_DWORD_STAT_BY(STAT_AudioMemory, TrackedMemoryUsage);
	TrackedMemoryUsage = 0;

	ResourceID = 0;
	bDynamicResource = false;
	DecompressionType = DTYPE_Setup;
	bDecompressedFromOgg = false;

	USoundWave* SoundWave = this;
	FAudioThread::RunCommandOnGameThread([SoundWave]()
	{
		if (SoundWave->ResourceState == ESoundWaveResourceState::Freeing)
		{
			SoundWave->ResourceState = ESoundWaveResourceState::Freed;
		}
	}, TStatId());
}

bool USoundWave::CleanupDecompressor(bool bForceWait)
{
	check(IsInAudioThread());

	if (!AudioDecompressor)
	{
		check(GetPrecacheState() == ESoundWavePrecacheState::Done);
		return true;
	}

	if (AudioDecompressor->IsDone())
	{
		delete AudioDecompressor;
		AudioDecompressor = nullptr;
		SetPrecacheState(ESoundWavePrecacheState::Done);
		return true;
	}

	if (bForceWait)
	{
		AudioDecompressor->EnsureCompletion();
		delete AudioDecompressor;
		AudioDecompressor = nullptr;
		SetPrecacheState(ESoundWavePrecacheState::Done);
		return true;
	}

	return false;
}

FWaveInstance* USoundWave::HandleStart( FActiveSound& ActiveSound, const UPTRINT WaveInstanceHash ) const
{
	// Create a new wave instance and associate with the ActiveSound
	FWaveInstance* WaveInstance = new FWaveInstance( &ActiveSound );
	WaveInstance->WaveInstanceHash = WaveInstanceHash;
	ActiveSound.WaveInstances.Add( WaveInstanceHash, WaveInstance );

	// Add in the subtitle if they exist
	if (ActiveSound.bHandleSubtitles && Subtitles.Num() > 0)
	{
		FQueueSubtitleParams QueueSubtitleParams(Subtitles);
		{
			QueueSubtitleParams.AudioComponentID = ActiveSound.GetAudioComponentID();
			QueueSubtitleParams.WorldPtr = ActiveSound.GetWeakWorld();
			QueueSubtitleParams.WaveInstance = (PTRINT)WaveInstance;
			QueueSubtitleParams.SubtitlePriority = ActiveSound.SubtitlePriority;
			QueueSubtitleParams.Duration = Duration;
			QueueSubtitleParams.bManualWordWrap = bManualWordWrap;
			QueueSubtitleParams.bSingleLine = bSingleLine;
			QueueSubtitleParams.RequestedStartTime = ActiveSound.RequestedStartTime;
		}

		FSubtitleManager::QueueSubtitles(QueueSubtitleParams);
	}

	return WaveInstance;
}

int32 USoundWave::GetNumSoundsActive()
{
	return NumSoundsActive.GetValue();
}

void USoundWave::IncrementNumSounds()
{
	NumSoundsActive.Increment();
}

void USoundWave::DecrementNumSounds()
{
	int32 NewValue = NumSoundsActive.Decrement();
	check(NewValue >= 0);
}

bool USoundWave::IsReadyForFinishDestroy()
{
	const bool bIsStreamingInProgress = IStreamingManager::Get().GetAudioStreamingManager().IsStreamingInProgress(this);

	check(GetPrecacheState() != ESoundWavePrecacheState::InProgress);

	// Wait till streaming and decompression finishes before deleting resource.
	if (!bIsStreamingInProgress && ResourceState == ESoundWaveResourceState::NeedsFree)
	{
		DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.FreeResources"), STAT_AudioFreeResources, STATGROUP_AudioThreadCommands);

		USoundWave* SoundWave = this;
		ResourceState = ESoundWaveResourceState::Freeing;
		FAudioThread::RunCommandOnAudioThread([SoundWave]()
		{
			SoundWave->FreeResources();
		}, GET_STATID(STAT_AudioFreeResources));
	}
	
	// bIsSoundActive is set in audio mixer when decoding sound waves or generating PCM data
	return ResourceState == ESoundWaveResourceState::Freed && NumSoundsActive.GetValue() == 0;
}


void USoundWave::FinishDestroy()
{
	Super::FinishDestroy();

	check(GetPrecacheState() != ESoundWavePrecacheState::InProgress);
	check(AudioDecompressor == nullptr);

	CleanupCachedRunningPlatformData();
#if WITH_EDITOR
	if (!GExitPurge)
	{
		ClearAllCachedCookedPlatformData();
	}
#endif

	IStreamingManager::Get().GetAudioStreamingManager().RemoveStreamingSoundWave(this);
}

void USoundWave::Parse( FAudioDevice* AudioDevice, const UPTRINT NodeWaveInstanceHash, FActiveSound& ActiveSound, const FSoundParseParameters& ParseParams, TArray<FWaveInstance*>& WaveInstances )
{
	FWaveInstance* WaveInstance = ActiveSound.FindWaveInstance(NodeWaveInstanceHash);

	// Create a new WaveInstance if this SoundWave doesn't already have one associated with it.
	if( WaveInstance == NULL )
	{
		if( !ActiveSound.bRadioFilterSelected )
		{
			ActiveSound.ApplyRadioFilter(ParseParams);
		}

		WaveInstance = HandleStart( ActiveSound, NodeWaveInstanceHash);
	}

	// Looping sounds are never actually finished
	if (bLooping || ParseParams.bLooping)
	{
		WaveInstance->bIsFinished = false;
#if !(NO_LOGGING || UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (!ActiveSound.bWarnedAboutOrphanedLooping && ActiveSound.GetAudioComponentID() == 0)
		{
			UE_LOG(LogAudio, Warning, TEXT("Detected orphaned looping sound '%s'."), *ActiveSound.GetSound()->GetName());
			ActiveSound.bWarnedAboutOrphanedLooping = true;
		}
#endif
	}

	// Check for finished paths.
	if( !WaveInstance->bIsFinished )
	{
		// Propagate properties and add WaveInstance to outgoing array of FWaveInstances.
		WaveInstance->SetVolume(ParseParams.Volume * Volume);
		WaveInstance->SetVolumeMultiplier(ParseParams.VolumeMultiplier);
		WaveInstance->SetDistanceAttenuation(ParseParams.DistanceAttenuation);
		WaveInstance->SetVolumeApp(ParseParams.VolumeApp);
		WaveInstance->Pitch = ParseParams.Pitch * Pitch;
		WaveInstance->bEnableLowPassFilter = ParseParams.bEnableLowPassFilter;
		WaveInstance->bIsOccluded = ParseParams.bIsOccluded;
		WaveInstance->LowPassFilterFrequency = ParseParams.LowPassFilterFrequency;
		WaveInstance->OcclusionFilterFrequency = ParseParams.OcclusionFilterFrequency;
		WaveInstance->AttenuationLowpassFilterFrequency = ParseParams.AttenuationLowpassFilterFrequency;
		WaveInstance->AttenuationHighpassFilterFrequency = ParseParams.AttenuationHighpassFilterFrequency;
		WaveInstance->AmbientZoneFilterFrequency = ParseParams.AmbientZoneFilterFrequency;
		WaveInstance->bApplyRadioFilter = ActiveSound.bApplyRadioFilter;
		WaveInstance->StartTime = ParseParams.StartTime;
		WaveInstance->UserIndex = ActiveSound.UserIndex;
		WaveInstance->OmniRadius = ParseParams.OmniRadius;
		WaveInstance->StereoSpread = ParseParams.StereoSpread;
		WaveInstance->AttenuationDistance = ParseParams.AttenuationDistance;
		WaveInstance->ListenerToSoundDistance = ParseParams.ListenerToSoundDistance;
		WaveInstance->AbsoluteAzimuth = ParseParams.AbsoluteAzimuth;

		if (NumChannels <= 2)
		{
			WaveInstance->SourceEffectChain = ParseParams.SourceEffectChain;
		}

		bool bAlwaysPlay = false;

		// Ensure that a Sound Class's default reverb level is used if we enabled reverb through a sound class and not from the active sound.
		bool bUseSoundClassDefaultReverb = false;

		// Properties from the sound class
		WaveInstance->SoundClass = ParseParams.SoundClass;
		if (ParseParams.SoundClass)
		{
			FSoundClassProperties* SoundClassProperties = AudioDevice->GetSoundClassCurrentProperties(ParseParams.SoundClass);
			// Use values from "parsed/ propagated" sound class properties
			float VolumeMultiplier = WaveInstance->GetVolumeMultiplier();
			WaveInstance->SetVolumeMultiplier(VolumeMultiplier* SoundClassProperties->Volume);
			WaveInstance->Pitch *= SoundClassProperties->Pitch;
			//TODO: Add in HighFrequencyGainMultiplier property to sound classes

			WaveInstance->VoiceCenterChannelVolume = SoundClassProperties->VoiceCenterChannelVolume;
			WaveInstance->RadioFilterVolume = SoundClassProperties->RadioFilterVolume * ParseParams.VolumeMultiplier;
			WaveInstance->RadioFilterVolumeThreshold = SoundClassProperties->RadioFilterVolumeThreshold * ParseParams.VolumeMultiplier;
			WaveInstance->StereoBleed = SoundClassProperties->StereoBleed;
			WaveInstance->LFEBleed = SoundClassProperties->LFEBleed;
			
			WaveInstance->bIsUISound = ActiveSound.bIsUISound || SoundClassProperties->bIsUISound;
			WaveInstance->bIsMusic = ActiveSound.bIsMusic || SoundClassProperties->bIsMusic;
			WaveInstance->bCenterChannelOnly = ActiveSound.bCenterChannelOnly || SoundClassProperties->bCenterChannelOnly;
			WaveInstance->bEQFilterApplied = ActiveSound.bEQFilterApplied || SoundClassProperties->bApplyEffects;
			WaveInstance->bReverb = ActiveSound.bReverb || SoundClassProperties->bReverb;

			bUseSoundClassDefaultReverb = SoundClassProperties->bReverb && !ActiveSound.bReverb;

			if (bUseSoundClassDefaultReverb)
			{
				WaveInstance->ReverbSendMethod = EReverbSendMethod::Manual;
				WaveInstance->ManualReverbSendLevel = SoundClassProperties->Default2DReverbSendAmount;
			}

			WaveInstance->OutputTarget = SoundClassProperties->OutputTarget;

			if (SoundClassProperties->bApplyAmbientVolumes)
			{
				VolumeMultiplier = WaveInstance->GetVolumeMultiplier();
				WaveInstance->SetVolumeMultiplier(VolumeMultiplier * ParseParams.InteriorVolumeMultiplier);
				WaveInstance->RadioFilterVolume *= ParseParams.InteriorVolumeMultiplier;
				WaveInstance->RadioFilterVolumeThreshold *= ParseParams.InteriorVolumeMultiplier;
			}

			bAlwaysPlay = ActiveSound.bAlwaysPlay || SoundClassProperties->bAlwaysPlay;
		}
		else
		{
			WaveInstance->VoiceCenterChannelVolume = 0.f;
			WaveInstance->RadioFilterVolume = 0.f;
			WaveInstance->RadioFilterVolumeThreshold = 0.f;
			WaveInstance->StereoBleed = 0.f;
			WaveInstance->LFEBleed = 0.f;
			WaveInstance->bEQFilterApplied = ActiveSound.bEQFilterApplied;
			WaveInstance->bIsUISound = ActiveSound.bIsUISound;
			WaveInstance->bIsMusic = ActiveSound.bIsMusic;
			WaveInstance->bReverb = ActiveSound.bReverb;
			WaveInstance->bCenterChannelOnly = ActiveSound.bCenterChannelOnly;

			bAlwaysPlay = ActiveSound.bAlwaysPlay;
		}

		// If set to bAlwaysPlay, increase the current sound's priority scale by 10x. This will still result in a possible 0-priority output if the sound has 0 actual volume
		if (bAlwaysPlay)
		{
			WaveInstance->Priority = MAX_FLT;
		}
		else
		{
			WaveInstance->Priority = ParseParams.Priority;
		}

		WaveInstance->Location = ParseParams.Transform.GetTranslation();
		WaveInstance->bIsStarted = true;
		WaveInstance->bAlreadyNotifiedHook = false;
		WaveInstance->bUseSpatialization = ParseParams.bUseSpatialization;
		WaveInstance->SpatializationMethod = ParseParams.SpatializationMethod;
		WaveInstance->WaveData = this;
		WaveInstance->NotifyBufferFinishedHooks = ParseParams.NotifyBufferFinishedHooks;
		WaveInstance->LoopingMode = ((bLooping || ParseParams.bLooping) ? LOOP_Forever : LOOP_Never);
		WaveInstance->bIsPaused = ParseParams.bIsPaused;

		// If we're normalizing 3d stereo spatialized sounds, we need to scale by -6 dB
		if (WaveInstance->bUseSpatialization && ParseParams.bApplyNormalizationToStereoSounds && NumChannels == 2)
		{
			float WaveInstanceVolume = WaveInstance->GetVolume();
			WaveInstance->SetVolume(WaveInstanceVolume * 0.5f);
		}

		// Copy reverb send settings
		WaveInstance->ReverbSendMethod = ParseParams.ReverbSendMethod;
		WaveInstance->ManualReverbSendLevel = ParseParams.ManualReverbSendLevel;
		WaveInstance->CustomRevebSendCurve = ParseParams.CustomReverbSendCurve;
		WaveInstance->ReverbSendLevelRange = ParseParams.ReverbSendLevelRange;
		WaveInstance->ReverbSendLevelDistanceRange = ParseParams.ReverbSendLevelDistanceRange;

		// Get the envelope follower settings
		WaveInstance->EnvelopeFollowerAttackTime = ParseParams.EnvelopeFollowerAttackTime;
		WaveInstance->EnvelopeFollowerReleaseTime = ParseParams.EnvelopeFollowerReleaseTime;

		// Copy over the submix sends.
		WaveInstance->SoundSubmix = ParseParams.SoundSubmix;
		WaveInstance->SoundSubmixSends = ParseParams.SoundSubmixSends;

		// Copy over the source bus send and data
		if (!WaveInstance->ActiveSound->bIsPreviewSound)
		{
			WaveInstance->bOutputToBusOnly = ParseParams.bOutputToBusOnly;
		}

		for (int32 BusSendType = 0; BusSendType < (int32)EBusSendType::Count; ++BusSendType)
		{
			WaveInstance->SoundSourceBusSends[BusSendType] = ParseParams.SoundSourceBusSends[BusSendType];
		}

		if (AudioDevice->IsHRTFEnabledForAll() && ParseParams.SpatializationMethod == ESoundSpatializationAlgorithm::SPATIALIZATION_Default)
		{
			WaveInstance->SpatializationMethod = ESoundSpatializationAlgorithm::SPATIALIZATION_HRTF;
		}
		else
		{
			WaveInstance->SpatializationMethod = ParseParams.SpatializationMethod;
		}

		// Pass along plugin settings to the wave instance
		WaveInstance->SpatializationPluginSettings = ParseParams.SpatializationPluginSettings;
		WaveInstance->OcclusionPluginSettings = ParseParams.OcclusionPluginSettings;
		WaveInstance->ReverbPluginSettings = ParseParams.ReverbPluginSettings;

		WaveInstance->bIsAmbisonics = bIsAmbisonics;

		bool bAddedWaveInstance = false;

		// Recompute the virtualizability here even though we did it up-front in the active sound parse.
		// This is because an active sound can generate multiple sound waves, not all of them are necessarily virtualizable.
		bool bHasSubtitles = ActiveSound.bHandleSubtitles && (ActiveSound.bHasExternalSubtitles || (Subtitles.Num() > 0));
		if (WaveInstance->GetVolumeWithDistanceAttenuation() > KINDA_SMALL_NUMBER || ((bVirtualizeWhenSilent || bHasSubtitles) && AudioDevice->VirtualSoundsEnabled()))
		{
			bAddedWaveInstance = true;
			WaveInstances.Add(WaveInstance);
		}

		// We're still alive.
		if (bAddedWaveInstance || WaveInstance->LoopingMode == LOOP_Forever)
		{
			ActiveSound.bFinished = false;
		}

		// Sanity check
		if( NumChannels > 2 && WaveInstance->bUseSpatialization && !WaveInstance->bReportedSpatializationWarning)
		{
			static TSet<USoundWave*> ReportedSounds;
			if (!ReportedSounds.Contains(this))
			{
				FString SoundWarningInfo = FString::Printf(TEXT("Spatialisation on sounds with channels greater than 2 is not supported. SoundWave: %s"), *GetName());
				if (ActiveSound.GetSound() != this)
				{
					SoundWarningInfo += FString::Printf(TEXT(" SoundCue: %s"), *ActiveSound.GetSound()->GetName());
				}

#if !NO_LOGGING
				const uint64 AudioComponentID = ActiveSound.GetAudioComponentID();
				if (AudioComponentID > 0)
				{
					FAudioThread::RunCommandOnGameThread([AudioComponentID, SoundWarningInfo]()
					{
						if (UAudioComponent* AudioComponent = UAudioComponent::GetAudioComponentFromID(AudioComponentID))
						{
							AActor* SoundOwner = AudioComponent->GetOwner();
							UE_LOG(LogAudio, Warning, TEXT( "%s Actor: %s AudioComponent: %s" ), *SoundWarningInfo, (SoundOwner ? *SoundOwner->GetName() : TEXT("None")), *AudioComponent->GetName() );
						}
						else
						{
							UE_LOG(LogAudio, Warning, TEXT("%s"), *SoundWarningInfo );
						}
					});
				}
				else
				{
					UE_LOG(LogAudio, Warning, TEXT("%s"), *SoundWarningInfo );
				}
#endif

				ReportedSounds.Add(this);
			}
			WaveInstance->bReportedSpatializationWarning = true;
		}
	}
}

bool USoundWave::IsPlayable() const
{
	return true;
}

float USoundWave::GetDuration()
{
	return (bLooping ? INDEFINITELY_LOOPING_DURATION : Duration);
}

bool USoundWave::IsStreaming() const
{
	// TODO: add in check on whether it's part of a streaming SoundGroup
	return bStreaming;
}

void USoundWave::UpdatePlatformData()
{
	if (IsStreaming())
	{
		// Make sure there are no pending requests in flight.
		while (IStreamingManager::Get().GetAudioStreamingManager().IsStreamingInProgress(this))
		{
			// Give up timeslice.
			FPlatformProcess::Sleep(0);
		}

#if WITH_EDITORONLY_DATA
		// Temporarily remove from streaming manager to release currently used data chunks
		IStreamingManager::Get().GetAudioStreamingManager().RemoveStreamingSoundWave(this);
		// Recache platform data if the source has changed.
		CachePlatformData();
		// Add back to the streaming manager to reload first chunk
		IStreamingManager::Get().GetAudioStreamingManager().AddStreamingSoundWave(this);
#endif
	}
	else
	{
		IStreamingManager::Get().GetAudioStreamingManager().RemoveStreamingSoundWave(this);
	}
}

float USoundWave::GetSampleRateForCurrentPlatform()
{
#if WITH_EDITOR
	float SampleRateOverride = FPlatformCompressionUtilities::GetTargetSampleRateForPlatform(SampleRateQuality);
	return (SampleRateOverride > 0) ? FMath::Min(SampleRateOverride, (float) SampleRate) : SampleRate;
#else
	if (bCachedSampleRateFromPlatformSettings)
	{
		return CachedSampleRateOverride;
	}
	else if (bSampleRateManuallyReset)
	{
		CachedSampleRateOverride = SampleRate;
		bCachedSampleRateFromPlatformSettings = true;

		return CachedSampleRateOverride;
	}
	else
	{
		CachedSampleRateOverride = FPlatformCompressionUtilities::GetTargetSampleRateForPlatform(SampleRateQuality);
		if (CachedSampleRateOverride < 0 || SampleRate < CachedSampleRateOverride)
		{
			CachedSampleRateOverride = SampleRate;
		}

		bCachedSampleRateFromPlatformSettings = true;
		return CachedSampleRateOverride;
	}
#endif
}

float USoundWave::GetSampleRateForCompressionOverrides(const FPlatformAudioCookOverrides* CompressionOverrides)
{
	const float* SampleRatePtr = CompressionOverrides->PlatformSampleRates.Find(SampleRateQuality);
	if (SampleRatePtr && *SampleRatePtr > 0.0f)
	{
		return FMath::Min(*SampleRatePtr, static_cast<float>(SampleRate));
	}
	else
	{
		return -1.0f;
	}
}

bool USoundWave::GetChunkData(int32 ChunkIndex, uint8** OutChunkData)
{
	if (RunningPlatformData->TryLoadChunk(ChunkIndex, OutChunkData) == false)
	{
#if WITH_EDITORONLY_DATA
		// Unable to load chunks from the cache. Rebuild the sound and attempt to recache it.
		UE_LOG(LogAudio, Display, TEXT("GetChunkData failed, rebuilding %s"), *GetPathName());

		ForceRebuildPlatformData();
		if (RunningPlatformData->TryLoadChunk(ChunkIndex, OutChunkData) == false)
		{
			UE_LOG(LogAudio, Display, TEXT("Failed to build sound %s."), *GetPathName());
		}
		else
		{
			// Succeeded after rebuilding platform data
			return true;
		}
#else
		// Failed to find the SoundWave chunk in the cooked package.
		UE_LOG(LogAudio, Warning, TEXT("GetChunkData failed while streaming. Ensure the following file is cooked: %s"), *GetPathName());
#endif // #if WITH_EDITORONLY_DATA
		return false;
	}
	return true;
}
