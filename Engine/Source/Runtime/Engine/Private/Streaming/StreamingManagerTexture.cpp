// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	TextureStreamingManager.cpp: Implementation of content streaming classes.
=============================================================================*/

#include "Streaming/StreamingManagerTexture.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Engine/TextureStreamingTypes.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/App.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "Streaming/AsyncTextureStreaming.h"
#include "Components/PrimitiveComponent.h"
#include "Misc/CoreDelegates.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/ITargetPlatform.h"

CSV_DECLARE_CATEGORY_MODULE_EXTERN(CORE_API, Basic);

CSV_DEFINE_CATEGORY(TextureStreaming, true);

static TAutoConsoleVariable<int32> CVarStreamingOverlapAssetAndLevelTicks(
	TEXT("r.Streaming.OverlapAssetAndLevelTicks"),
	!WITH_EDITOR && (PLATFORM_PS4 || PLATFORM_XBOXONE),
	TEXT("Ticks render asset streaming info on a high priority task thread while ticking levels on GT"),
	ECVF_Default);

bool TrackRenderAsset( const FString& AssetName );
bool UntrackRenderAsset( const FString& AssetName );
void ListTrackedRenderAssets( FOutputDevice& Ar, int32 NumTextures );

/**
 * Helper function to clamp the mesh to camera distance
 */
FORCEINLINE float ClampMeshToCameraDistanceSquared(float MeshToCameraDistanceSquared)
{
	// called from streaming thread, maybe even main thread
	return FMath::Max<float>(MeshToCameraDistanceSquared, 0.0f);
}

/*-----------------------------------------------------------------------------
	FRenderAssetStreamingManager implementation.
-----------------------------------------------------------------------------*/

/** Constructor, initializing all members and  */
FRenderAssetStreamingManager::FRenderAssetStreamingManager()
:	CurrentUpdateStreamingRenderAssetIndex(0)
,	bTriggerDumpTextureGroupStats( false )
,	bDetailedDumpTextureGroupStats( false )
,	AsyncWork( nullptr )
,	CurrentPendingMipCopyRequestIdx(0)
,	ProcessingStage( 0 )
,	NumRenderAssetProcessingStages(5)
,	bUseDynamicStreaming( false )
,	BoostPlayerTextures( 3.0f )
,	MemoryMargin(0)
,	EffectiveStreamingPoolSize(0)
,	MemoryOverBudget(0)
,	MaxEverRequired(0)
,	bPauseRenderAssetStreaming(false)
,	LastWorldUpdateTime(GIsEditor ? -FLT_MAX : 0) // In editor, visibility is not taken into consideration.
{
	// Read settings from ini file.
	int32 TempInt;
	verify( GConfig->GetInt( TEXT("TextureStreaming"), TEXT("MemoryMargin"),				TempInt,						GEngineIni ) );
	MemoryMargin = TempInt;

	verify( GConfig->GetFloat( TEXT("TextureStreaming"), TEXT("LightmapStreamingFactor"),			GLightmapStreamingFactor,		GEngineIni ) );
	verify( GConfig->GetFloat( TEXT("TextureStreaming"), TEXT("ShadowmapStreamingFactor"),			GShadowmapStreamingFactor,		GEngineIni ) );

	int32 PoolSizeIniSetting = 0;
	GConfig->GetInt(TEXT("TextureStreaming"), TEXT("PoolSize"), PoolSizeIniSetting, GEngineIni);
	GConfig->GetBool(TEXT("TextureStreaming"), TEXT("UseDynamicStreaming"), bUseDynamicStreaming, GEngineIni);
	GConfig->GetFloat( TEXT("TextureStreaming"), TEXT("BoostPlayerTextures"), BoostPlayerTextures, GEngineIni );
	GConfig->GetBool(TEXT("TextureStreaming"), TEXT("NeverStreamOutRenderAssets"), GNeverStreamOutRenderAssets, GEngineIni);

	// -NeverStreamOutRenderAssets
	if (FParse::Param(FCommandLine::Get(), TEXT("NeverStreamOutRenderAssets")))
	{
		GNeverStreamOutRenderAssets = true;
	}
	if (GIsEditor)
	{
		// this would not be good or useful in the editor
		GNeverStreamOutRenderAssets = false;
	}
	if (GNeverStreamOutRenderAssets)
	{
		UE_LOG(LogContentStreaming, Log, TEXT("Textures will NEVER stream out!"));
	}

	// Convert from MByte to byte.
	MemoryMargin *= 1024 * 1024;

#if STATS_FAST
	MaxStreamingTexturesSize = 0;
	MaxOptimalTextureSize = 0;
	MaxStreamingOverBudget = MIN_int64;
	MaxTexturePoolAllocatedSize = 0;
	MaxNumWantingTextures = 0;
#endif

	for ( int32 LODGroup=0; LODGroup < TEXTUREGROUP_MAX; ++LODGroup )
	{
		const FTextureLODGroup& TexGroup = UDeviceProfileManager::Get().GetActiveProfile()->GetTextureLODSettings()->GetTextureLODGroup(TextureGroup(LODGroup));
		NumStreamedMips_Texture[LODGroup] = TexGroup.NumStreamedMips;
	}

	// TODO: NumStreamedMips_StaticMesh, NumStreamedMips_SkeletalMesh
	NumStreamedMips_StaticMesh.Empty(1);
	NumStreamedMips_StaticMesh.Add(INT32_MAX);

	// setup the streaming resource flush function pointer
	GFlushStreamingFunc = &FlushResourceStreaming;

	ProcessingStage = 0;
	AsyncWork = new FAsyncTask<FRenderAssetStreamingMipCalcTask>(this);

	RenderAssetInstanceAsyncWork = new RenderAssetInstanceTask::FDoWorkAsyncTask();
	DynamicComponentManager.RegisterTasks(RenderAssetInstanceAsyncWork->GetTask());

	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddRaw(this, &FRenderAssetStreamingManager::OnPreGarbageCollect);

	FCoreDelegates::PakFileMountedCallback.AddRaw(this, &FRenderAssetStreamingManager::OnPakFileMounted);
}

FRenderAssetStreamingManager::~FRenderAssetStreamingManager()
{
	AsyncWork->EnsureCompletion();
	delete AsyncWork;

	RenderAssetInstanceAsyncWork->EnsureCompletion();
	
	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().RemoveAll(this);

	// Clear the stats
	DisplayedStats.Reset();
	STAT(DisplayedStats.Apply();)
}

void FRenderAssetStreamingManager::OnPreGarbageCollect()
{
	FScopeLock ScopeLock(&CriticalSection);
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FRenderAssetStreamingManager_OnPreGarbageCollect);

	FRemovedRenderAssetArray RemovedRenderAssets;

	// Check all levels for pending kills.
	for (int32 Index = 0; Index < LevelRenderAssetManagers.Num(); ++Index)
	{
		if (LevelRenderAssetManagers[Index] == nullptr)
		{
			continue;
		}

		FLevelRenderAssetManager& LevelManager = *LevelRenderAssetManagers[Index];
		if (LevelManager.GetLevel()->IsPendingKill())
		{
			LevelManager.Remove(&RemovedRenderAssets);

			// Remove the level entry. The async task view will still be valid as it uses a shared ptr.
			delete LevelRenderAssetManagers[Index];
			LevelRenderAssetManagers[Index] = nullptr;
		}
	}

	DynamicComponentManager.OnPreGarbageCollect(RemovedRenderAssets);

	SetRenderAssetsRemovedTimestamp(RemovedRenderAssets);
}



void FRenderAssetStreamingManager::OnPakFileMounted(const TCHAR* PakFilename)
{
	// clear the cached file exists checks which failed as they may now be loaded
	bNewFilesLoaded = true;
}

/**
 * Cancels the timed Forced resources (i.e used the Kismet action "Stream In Textures").
 */
void FRenderAssetStreamingManager::CancelForcedResources()
{
	FScopeLock ScopeLock(&CriticalSection);

	// Update textures/meshes that are Forced on a timer.
	for ( int32 Idx=0; Idx < StreamingRenderAssets.Num(); ++Idx )
	{
		FStreamingRenderAsset& StreamingRenderAsset = StreamingRenderAssets[ Idx ];

		// Make sure this streaming texture/mesh hasn't been marked for removal.
		if (StreamingRenderAsset.RenderAsset)
		{
			// Remove any prestream requests from textures/meshes
			float TimeLeft = (float)(StreamingRenderAsset.RenderAsset->ForceMipLevelsToBeResidentTimestamp - FApp::GetCurrentTime());
			if ( TimeLeft >= 0.0f )
			{
				StreamingRenderAsset.RenderAsset->SetForceMipLevelsToBeResident( -1.0f );
				StreamingRenderAsset.InstanceRemovedTimestamp = -FLT_MAX;
				StreamingRenderAsset.RenderAsset->InvalidateLastRenderTimeForStreaming();
#if STREAMING_LOG_CANCELFORCED
				UE_LOG(LogContentStreaming, Log, TEXT("Canceling forced texture: %s (had %.1f seconds left)"), *StreamingRenderAsset.Texture->GetFullName(), TimeLeft );
#endif
			}
		}
	}

	// Reset the streaming system, so it picks up any changes to UTexture2D right away.
	ProcessingStage = 0;
}

/**
 * Notifies manager of "level" change so it can prioritize character textures for a few frames.
 */
void FRenderAssetStreamingManager::NotifyLevelChange()
{
}

/** Don't stream world resources for the next NumFrames. */
void FRenderAssetStreamingManager::SetDisregardWorldResourcesForFrames( int32 NumFrames )
{
	//@TODO: We could perhaps increase the priority factor for character textures...
}

/**
 *	Try to stream out texture/mesh mip-levels to free up more memory.
 *	@param RequiredMemorySize	- Additional texture memory required
 *	@return						- Whether it succeeded or not
 **/
bool FRenderAssetStreamingManager::StreamOutRenderAssetData( int64 RequiredMemorySize )
{
	FScopeLock ScopeLock(&CriticalSection);

	const int64 MaxTempMemoryAllowed = Settings.MaxTempMemoryAllowed * 1024 * 1024;
	const bool CachedPauseTextureStreaming = bPauseRenderAssetStreaming;

	// Pause texture streaming to prevent sending load requests.
	bPauseRenderAssetStreaming = true;
	SyncStates(true);

	// Sort texture/mesh, having those that should be dropped first.
	TArray<int32> PrioritizedRenderAssets;
	PrioritizedRenderAssets.Empty(StreamingRenderAssets.Num());
	for (int32 Idx = 0; Idx < StreamingRenderAssets.Num(); ++Idx)
	{
		FStreamingRenderAsset& StreamingRenderAsset = StreamingRenderAssets[Idx];
		// Only texture for which we can drop mips.
		if (StreamingRenderAsset.IsMaxResolutionAffectedByGlobalBias())
		{
			PrioritizedRenderAssets.Add(Idx);
		}
	}
	PrioritizedRenderAssets.Sort(FCompareRenderAssetByRetentionPriority(StreamingRenderAssets));

	int64 TempMemoryUsed = 0;
	int64 MemoryDropped = 0;

	// Process all texture/mesh, starting with the ones we least want to keep
	for (int32 PriorityIndex = PrioritizedRenderAssets.Num() - 1; PriorityIndex >= 0 && MemoryDropped < RequiredMemorySize; --PriorityIndex)
	{
		int32 RenderAssetIndex = PrioritizedRenderAssets[PriorityIndex];
		if (!StreamingRenderAssets.IsValidIndex(RenderAssetIndex)) continue;

		FStreamingRenderAsset& StreamingRenderAsset = StreamingRenderAssets[RenderAssetIndex];
		if (!StreamingRenderAsset.RenderAsset) continue;

		const int32 MinimalSize = StreamingRenderAsset.GetSize(StreamingRenderAsset.MinAllowedMips);
		const int32 CurrentSize = StreamingRenderAsset.GetSize(StreamingRenderAsset.ResidentMips);

		if (StreamingRenderAsset.RenderAsset->StreamOut(StreamingRenderAsset.MinAllowedMips))
		{
			MemoryDropped += CurrentSize - MinimalSize; 
			TempMemoryUsed += MinimalSize;

			StreamingRenderAsset.UpdateStreamingStatus(false);

			if (TempMemoryUsed >= MaxTempMemoryAllowed)
			{
				// Queue up the process on the render thread and wait for everything to complete.
				ENQUEUE_RENDER_COMMAND(FlushResourceCommand)(
					[](FRHICommandList& RHICmdList)
					{				
						FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
						RHIFlushResources();
					});
				FlushRenderingCommands();
				TempMemoryUsed = 0;
			}
		}
	}

	bPauseRenderAssetStreaming = CachedPauseTextureStreaming;
	UE_LOG(LogContentStreaming, Log, TEXT("Streaming out texture memory! Saved %.2f MB."), float(MemoryDropped)/1024.0f/1024.0f);
	return true;
}

void FRenderAssetStreamingManager::IncrementalUpdate(float Percentage, bool bUpdateDynamicComponents)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FRenderAssetStreamingManager_IncrementalUpdate);
	FRemovedRenderAssetArray RemovedRenderAssets;

	int64 NumStepsLeftForIncrementalBuild = CVarStreamingNumStaticComponentsProcessedPerFrame.GetValueOnGameThread();
	if (NumStepsLeftForIncrementalBuild <= 0) // When 0, don't allow incremental updates.
	{
		NumStepsLeftForIncrementalBuild = MAX_int64;
	}

	for (FLevelRenderAssetManager* LevelManager : LevelRenderAssetManagers)
	{
		if (LevelManager != nullptr)
		{
			LevelManager->IncrementalUpdate(DynamicComponentManager, RemovedRenderAssets, NumStepsLeftForIncrementalBuild, Percentage, bUseDynamicStreaming); // Complete the incremental update.
		}
	}

	// Dynamic component are only udpated when it is useful for the dynamic async view.
	if (bUpdateDynamicComponents && bUseDynamicStreaming)
	{
		DynamicComponentManager.IncrementalUpdate(RemovedRenderAssets, Percentage);
	}

	SetRenderAssetsRemovedTimestamp(RemovedRenderAssets);
}

void FRenderAssetStreamingManager::ProcessRemovedRenderAssets()
{
	for (int32 AssetIndex : RemovedRenderAssetIndices)
	{
		// Remove swap all elements, until this entry has a valid texture/mesh.
		// This handles the case where the last element was also removed.
		while (StreamingRenderAssets.IsValidIndex(AssetIndex) && !StreamingRenderAssets[AssetIndex].RenderAsset)
		{
			StreamingRenderAssets.RemoveAtSwap(AssetIndex);
		}

		if (StreamingRenderAssets.IsValidIndex(AssetIndex))
		{
			// Update the texture with its new index.
			StreamingRenderAssets[AssetIndex].RenderAsset->StreamingIndex = AssetIndex;
		}
	}
	RemovedRenderAssetIndices.Empty();
}

void FRenderAssetStreamingManager::ProcessAddedRenderAssets()
{
	// Add new textures or meshes.
	StreamingRenderAssets.Reserve(StreamingRenderAssets.Num() + PendingStreamingRenderAssets.Num());
	for (int32 Idx = 0; Idx < PendingStreamingRenderAssets.Num(); ++Idx)
	{
		UStreamableRenderAsset* Asset = PendingStreamingRenderAssets[Idx];
		// Could be null if it was removed after being added.
		if (Asset)
		{
			Asset->StreamingIndex = StreamingRenderAssets.Num();
			const FStreamingRenderAsset::EAssetType Type = PendingStreamingRenderAssetTypes[Idx];
			const int32* NumStreamedMips;
			const int32 NumLODGroups = GetNumStreamedMipsArray(Type, NumStreamedMips);
			new (StreamingRenderAssets) FStreamingRenderAsset(Asset, NumStreamedMips, NumLODGroups, Type, Settings);
		}
	}
	PendingStreamingRenderAssets.Empty();
	PendingStreamingRenderAssetTypes.Empty();
}

void FRenderAssetStreamingManager::ConditionalUpdateStaticData()
{
	static float PreviousLightmapStreamingFactor = GLightmapStreamingFactor;
	static float PreviousShadowmapStreamingFactor = GShadowmapStreamingFactor;
	static FRenderAssetStreamingSettings PreviousSettings = Settings;

	if (PreviousLightmapStreamingFactor != GLightmapStreamingFactor || 
		PreviousShadowmapStreamingFactor != GShadowmapStreamingFactor || 
		PreviousSettings != Settings)
	{
		STAT(GatheredStats.SetupAsyncTaskCycles += FPlatformTime::Cycles();)
		// Update each texture static data.
		for (FStreamingRenderAsset& StreamingRenderAsset : StreamingRenderAssets)
		{
			StreamingRenderAsset.UpdateStaticData(Settings);

			// When the material quality changes, some textures could stop being used.
			// Refreshing their removed timestamp ensures not texture ends up in the unkwown 
			// ref heuristic (which would force load them).
			if (PreviousSettings.MaterialQualityLevel != Settings.MaterialQualityLevel)
			{
				StreamingRenderAsset.InstanceRemovedTimestamp = FApp::GetCurrentTime();
			}
		}
		STAT(GatheredStats.SetupAsyncTaskCycles -= (int32)FPlatformTime::Cycles();)

#if !UE_BUILD_SHIPPING
		// Those debug settings are config that are not expected to change in-game.
		const bool bDebugSettingsChanged = 
			PreviousSettings.bUseMaterialData != Settings.bUseMaterialData ||
			PreviousSettings.bUseNewMetrics != Settings.bUseNewMetrics ||
			PreviousSettings.bUsePerTextureBias != Settings.bUsePerTextureBias || 
			PreviousSettings.MaxTextureUVDensity != Settings.MaxTextureUVDensity;
#else
		const bool bDebugSettingsChanged = false;
#endif

		// If the material quality changes, everything needs to be updated.
		if (bDebugSettingsChanged || PreviousSettings.MaterialQualityLevel != Settings.MaterialQualityLevel)
		{
			TArray<ULevel*, TInlineAllocator<32> > Levels;

			// RemoveLevel data
			for (FLevelRenderAssetManager* LevelManager : LevelRenderAssetManagers)
			{
				if (LevelManager!=nullptr)
				{
					Levels.Push(LevelManager->GetLevel());
					LevelManager->Remove(nullptr);
				}
			}
			LevelRenderAssetManagers.Empty();

			for (ULevel* Level : Levels)
			{
				AddLevel(Level);
			}

			// Reinsert dynamic components
			TArray<const UPrimitiveComponent*> DynamicComponents;
			DynamicComponentManager.GetReferencedComponents(DynamicComponents);
			for (const UPrimitiveComponent* Primitive : DynamicComponents)
			{
				NotifyPrimitiveUpdated_Concurrent(Primitive);
			}
		}

		// Update the cache variables.
		PreviousLightmapStreamingFactor = GLightmapStreamingFactor;
		PreviousShadowmapStreamingFactor = GShadowmapStreamingFactor;
		PreviousSettings = Settings;
	}
}

void FRenderAssetStreamingManager::ProcessLevelsToReferenceToStreamedTextures()
{
	// Iterate through levels and reference Levels to StreamedTexture if needed
	for (int32 LevelIndex = 0; LevelIndex < LevelRenderAssetManagers.Num(); ++LevelIndex)
	{
		if (LevelRenderAssetManagers[LevelIndex] == nullptr)
		{
			continue;
		}

		FLevelRenderAssetManager& LevelRenderAssetManager = *LevelRenderAssetManagers[LevelIndex];
		if (LevelRenderAssetManager.HasBeenReferencedToStreamedTextures())
		{
			continue;
		}

		const FRenderAssetInstanceView* View = LevelRenderAssetManager.GetRawAsyncView();
		if (View == nullptr)
		{
			continue;
		}

		LevelRenderAssetManager.SetReferencedToStreamedTextures();

		FRenderAssetInstanceView::FRenderAssetIterator RenderAssetIterator = LevelRenderAssetManager.GetRawAsyncView()->GetRenderAssetIterator();

		for (; RenderAssetIterator; ++RenderAssetIterator)
		{
			const UStreamableRenderAsset* RenderAsset = *RenderAssetIterator;
			if (RenderAsset == nullptr || !ReferencedRenderAssets.Contains(RenderAsset) || !StreamingRenderAssets.IsValidIndex(RenderAsset->StreamingIndex))
			{
				continue;
			}

			FStreamingRenderAsset& StreamingRenderAsset = StreamingRenderAssets[RenderAsset->StreamingIndex];

			check(StreamingRenderAsset.RenderAsset == RenderAsset);

			TBitArray<>& LevelIndexUsage = StreamingRenderAsset.LevelIndexUsage;

			if (LevelIndex >= LevelIndexUsage.Num())
			{
				uint32 NumBits = LevelIndex + 1 - LevelIndexUsage.Num();
				for (uint32 Index = 0; Index < NumBits; ++Index)
				{
					LevelIndexUsage.Add(false);
				}
			}

			LevelIndexUsage[LevelIndex] = true;
		}
	}
}

void FRenderAssetStreamingManager::UpdatePendingStates(bool bUpdateDynamicComponents)
{
	CheckUserSettings();

	ProcessRemovedRenderAssets();
	ProcessAddedRenderAssets();

	Settings.Update();
	ConditionalUpdateStaticData();

	// Fully complete all pending update static data (newly loaded levels).
	// Dynamic bounds are not updated here since the async task uses the async view generated from the last frame.
	// this makes the current dynamic data fully dirty, and it will get refreshed iterativelly for the next full update.
	IncrementalUpdate(1.f, bUpdateDynamicComponents);
	if (bUpdateDynamicComponents)
	{
		DynamicComponentManager.PrepareAsyncView();
	}

	ProcessLevelsToReferenceToStreamedTextures();
}

/**
 * Adds new textures/meshes and level data on the gamethread (while the worker thread isn't active).
 */
void FRenderAssetStreamingManager::PrepareAsyncTask(bool bProcessEverything)
{
	FRenderAssetStreamingMipCalcTask& AsyncTask = AsyncWork->GetTask();
	FTextureMemoryStats Stats;
	RHIGetTextureMemoryStats(Stats);

	// TODO: Track memory allocated by mesh LODs

	// When processing all textures, we need unlimited budget so that textures get all at their required states.
	// Same when forcing stream-in, for which we want all used textures to be fully loaded 
	if (Stats.IsUsingLimitedPoolSize() && !bProcessEverything && !Settings.bFullyLoadUsedTextures)
	{
		const int64 TempMemoryBudget = Settings.MaxTempMemoryAllowed * 1024 * 1024;
		AsyncTask.Reset(Stats.TotalGraphicsMemory, Stats.AllocatedMemorySize, Stats.TexturePoolSize, TempMemoryBudget, MemoryMargin);
	}
	else
	{
		// Temp must be smaller since membudget only updates if it has a least temp memory available.
		AsyncTask.Reset(0, Stats.AllocatedMemorySize, MAX_int64, MAX_int64 / 2, 0);
	}
	AsyncTask.StreamingData.Init(CurrentViewInfos, LastWorldUpdateTime, LevelRenderAssetManagers, DynamicComponentManager);
}

/**
 * Temporarily boosts the streaming distance factor by the specified number.
 * This factor is automatically reset to 1.0 after it's been used for mip-calculations.
 */
void FRenderAssetStreamingManager::BoostTextures( AActor* Actor, float BoostFactor )
{
	FScopeLock ScopeLock(&CriticalSection);

	if ( Actor )
	{
		TArray<UTexture*> Textures;
		Textures.Empty( 32 );

		for (UActorComponent* Component : Actor->GetComponents())
		{
			UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
			if (Primitive && Primitive->IsRegistered())
			{
				Textures.Reset();
				Primitive->GetUsedTextures( Textures, EMaterialQualityLevel::Num );
				for ( UTexture* Texture : Textures )
				{
					FStreamingRenderAsset* StreamingTexture = GetStreamingRenderAsset(Texture);
					if ( StreamingTexture )
					{
						StreamingTexture->DynamicBoostFactor = FMath::Max( StreamingTexture->DynamicBoostFactor, BoostFactor );
					}
				}
			}
		}
	}
}

/** Adds a ULevel to the streaming manager. This is called from 2 paths : after PostPostLoad and after AddToWorld */
void FRenderAssetStreamingManager::AddLevel( ULevel* Level )
{
	FScopeLock ScopeLock(&CriticalSection);

	check(Level);

	if (GIsEditor)
	{
		// In editor, we want to rebuild everything from scratch as the data could be changing.
		// To do so, we remove the level and reinsert it.
		RemoveLevel(Level);
	}
	else
	{
		// In game, because static components can not be changed, the level static data is computed and kept as long as the level is not destroyed.
		for (const FLevelRenderAssetManager* LevelManager : LevelRenderAssetManagers)
		{
			if (LevelManager!=nullptr && LevelManager->GetLevel() == Level)
			{
				// Nothing to do, since the incremental update automatically manages what needs to be done.
				return;
			}
		}
	}

	// If the level was not already there, create a new one, find an available slot or add a new one.
	RenderAssetInstanceAsyncWork->EnsureCompletion();
	FLevelRenderAssetManager* LevelRenderAssetManager = new FLevelRenderAssetManager(Level, RenderAssetInstanceAsyncWork->GetTask());

	uint32 LevelIndex = LevelRenderAssetManagers.FindLastByPredicate([](FLevelRenderAssetManager* Ptr) { return (Ptr == nullptr); });
	if (LevelIndex != INDEX_NONE)
	{
		LevelRenderAssetManagers[LevelIndex] = LevelRenderAssetManager;
	}
	else
	{
		LevelRenderAssetManagers.Add(LevelRenderAssetManager);
	}
}

/** Removes a ULevel from the streaming manager. */
void FRenderAssetStreamingManager::RemoveLevel( ULevel* Level )
{
	FScopeLock ScopeLock(&CriticalSection);

	check(Level);

	// In editor we remove levels when visibility changes, while in game we want to kept the static data as long as possible.
	// FLevelRenderAssetManager::IncrementalUpdate will remove dynamic components and mark textures/meshes timestamps.
	if (GIsEditor || Level->IsPendingKill() || Level->HasAnyFlags(RF_BeginDestroyed|RF_FinishDestroyed))
	{
		for (int32 Index = 0; Index < LevelRenderAssetManagers.Num(); ++Index)
		{
			FLevelRenderAssetManager* LevelManager = LevelRenderAssetManagers[Index];
			if (LevelManager!=nullptr && LevelManager->GetLevel() == Level)
			{
				FRemovedRenderAssetArray RemovedRenderAssets;
				LevelManager->Remove(&RemovedRenderAssets);
				SetRenderAssetsRemovedTimestamp(RemovedRenderAssets);

				// Remove the level entry. The async task view will still be valid as it uses a shared ptr.
				LevelRenderAssetManagers[Index] = nullptr;
				delete LevelManager;
				break;
			}
		}
	}
}

void FRenderAssetStreamingManager::NotifyLevelOffset(ULevel* Level, const FVector& Offset)
{
	FScopeLock ScopeLock(&CriticalSection);

	for (FLevelRenderAssetManager* LevelManager : LevelRenderAssetManagers)
	{
		if (LevelManager!=nullptr && LevelManager->GetLevel() == Level)
		{
			LevelManager->NotifyLevelOffset(Offset);
			break;
		}
	}
}

void FRenderAssetStreamingManager::AddStreamingRenderAsset_Internal(UStreamableRenderAsset* InAsset, FStreamingRenderAsset::EAssetType InType)
{
	FScopeLock ScopeLock(&CriticalSection);

	STAT(GatheredStats.CallbacksCycles = -(int32)FPlatformTime::Cycles();)

	// Adds the new texture/mesh to the Pending list, to avoid reallocation of the thread-safe StreamingRenderAssets array.
	check(InAsset->StreamingIndex == INDEX_NONE);
	InAsset->StreamingIndex = PendingStreamingRenderAssets.Add(InAsset);
	PendingStreamingRenderAssetTypes.Add(InType);

	// Mark as pending update while the streamer has not determined the required resolution (unless paused)
	InAsset->bHasStreamingUpdatePending = !bPauseRenderAssetStreaming;

	// Notify that this texture/mesh ptr is valid.
	ReferencedRenderAssets.Add(InAsset);

	STAT(GatheredStats.CallbacksCycles += FPlatformTime::Cycles();)
}

/**
 * Adds a new texture/mesh to the streaming manager.
 */
void FRenderAssetStreamingManager::AddStreamingRenderAsset( UTexture2D* Texture )
{
	AddStreamingRenderAsset_Internal(Texture, FStreamingRenderAsset::AT_Texture);
}

void FRenderAssetStreamingManager::AddStreamingRenderAsset(UStaticMesh* StaticMesh)
{
	AddStreamingRenderAsset_Internal(StaticMesh, FStreamingRenderAsset::AT_StaticMesh);
}

void FRenderAssetStreamingManager::AddStreamingRenderAsset(USkeletalMesh* SkeletalMesh)
{
	// TODO
	LowLevelFatalError(TEXT("FRenderAssetStreamingManager::AddStreamingRenderAsset(USkeletalMesh* SkeletalMesh) is not implemented"));
}

/**
 * Removes a texture/mesh from the streaming manager.
 */
void FRenderAssetStreamingManager::RemoveStreamingRenderAsset( UStreamableRenderAsset* RenderAsset )
{
	FScopeLock ScopeLock(&CriticalSection);

	STAT(GatheredStats.CallbacksCycles = -(int32)FPlatformTime::Cycles();)

	const int32	Idx = RenderAsset->StreamingIndex;

	// Remove it from the Pending list if it is there.
	if (PendingStreamingRenderAssets.IsValidIndex(Idx) && PendingStreamingRenderAssets[Idx] == RenderAsset)
	{
		PendingStreamingRenderAssets[Idx] = nullptr;
	}
	else if (StreamingRenderAssets.IsValidIndex(Idx) && StreamingRenderAssets[Idx].RenderAsset == RenderAsset)
	{
		StreamingRenderAssets[Idx].RenderAsset = nullptr;
		RemovedRenderAssetIndices.Add(Idx);
	}

	RenderAsset->StreamingIndex = INDEX_NONE;
	RenderAsset->bHasStreamingUpdatePending = false;

	// Remove reference to this texture/mesh.
	ReferencedRenderAssets.Remove(RenderAsset);

	STAT(GatheredStats.CallbacksCycles += FPlatformTime::Cycles();)
}

/** Called when a spawned primitive is deleted, or when an actor is destroyed in the editor. */
void FRenderAssetStreamingManager::NotifyActorDestroyed( AActor* Actor )
{
	FScopeLock ScopeLock(&CriticalSection);

	STAT(GatheredStats.CallbacksCycles = -(int32)FPlatformTime::Cycles();)
	FRemovedRenderAssetArray RemovedRenderAssets;
	check(Actor);

	TInlineComponentArray<UPrimitiveComponent*> Components;
	Actor->GetComponents(Components);
	Components.Remove(nullptr);

	// Here we assume that level can not be changed in game, to allow an optimized path.
	ULevel* Level = !GIsEditor ? Actor->GetLevel() : nullptr;

	// Remove any reference in the level managers.
	for (FLevelRenderAssetManager* LevelManager : LevelRenderAssetManagers)
	{
		if (LevelManager!=nullptr && (!Level || LevelManager->GetLevel() == Level))
		{
			LevelManager->RemoveActorReferences(Actor);
			for (UPrimitiveComponent* Component : Components)
			{
				LevelManager->RemoveComponentReferences(Component, RemovedRenderAssets);
			}
		}
	}

	for (UPrimitiveComponent* Component : Components)
	{
		// Remove any references in the dynamic component manager.
		DynamicComponentManager.Remove(Component, &RemovedRenderAssets);

		// Reset this now as we have finished iterating over the levels
		Component->bAttachedToStreamingManagerAsStatic = false;
	}

	SetRenderAssetsRemovedTimestamp(RemovedRenderAssets);
	STAT(GatheredStats.CallbacksCycles += FPlatformTime::Cycles();)
}

void FRenderAssetStreamingManager::RemoveStaticReferences(const UPrimitiveComponent* Primitive)
{
	FScopeLock ScopeLock(&CriticalSection);

	check(Primitive);

	if (Primitive->bAttachedToStreamingManagerAsStatic)
	{
		FRemovedRenderAssetArray RemovedRenderAssets;
		ULevel* Level = Primitive->GetComponentLevel();
		for (FLevelRenderAssetManager* LevelManager : LevelRenderAssetManagers)
		{
			if (LevelManager != nullptr && (!Level || LevelManager->GetLevel() == Level))
			{
				LevelManager->RemoveComponentReferences(Primitive, RemovedRenderAssets);
			}
		}
		Primitive->bAttachedToStreamingManagerAsStatic = false;
		// Nothing to do with removed textures/meshes as we are about to reinsert
	}
}

/**
 * Called when a primitive is detached from an actor or another component.
 * Note: We should not be accessing the primitive or the UTexture2D after this call!
 */
void FRenderAssetStreamingManager::NotifyPrimitiveDetached( const UPrimitiveComponent* Primitive )
{
	FScopeLock ScopeLock(&CriticalSection);

	if (!Primitive || !Primitive->IsAttachedToStreamingManager())
	{
		return;
	}

	STAT(GatheredStats.CallbacksCycles = -(int32)FPlatformTime::Cycles();)
	FRemovedRenderAssetArray RemovedRenderAssets;

#if STREAMING_LOG_DYNAMIC
		UE_LOG(LogContentStreaming, Log, TEXT("NotifyPrimitiveDetached(0x%08x \"%s\"), IsRegistered=%d"), SIZE_T(Primitive), *Primitive->GetReadableName(), Primitive->IsRegistered());
#endif

	if (Primitive->bAttachedToStreamingManagerAsStatic)
	{
		// Here we assume that level can not be changed in game, to allow an optimized path.
		// If there is not level, then we assume it could be in any level.
		ULevel* Level = !GIsEditor ? Primitive->GetComponentLevel() : nullptr;
		if (Level && (Level->IsPendingKill() || Level->HasAnyFlags(RF_BeginDestroyed|RF_FinishDestroyed)))
		{
			// Do a batch remove to prevent handling each component individually.
			RemoveLevel(Level);
		}
		// Unless in editor, we don't want to remove reference in static level data when toggling visibility.
		else if (GIsEditor || Primitive->IsPendingKill() || Primitive->HasAnyFlags(RF_BeginDestroyed|RF_FinishDestroyed))
		{
			for (FLevelRenderAssetManager* LevelManager : LevelRenderAssetManagers)
			{
				if (LevelManager != nullptr && (!Level || LevelManager->GetLevel() == Level))
				{
					LevelManager->RemoveComponentReferences(Primitive, RemovedRenderAssets);
				}
			}
			Primitive->bAttachedToStreamingManagerAsStatic = false;
		}
	}
	
	// Dynamic component must be removed when visibility changes.
	DynamicComponentManager.Remove(Primitive, &RemovedRenderAssets);

	SetRenderAssetsRemovedTimestamp(RemovedRenderAssets);
	STAT(GatheredStats.CallbacksCycles += FPlatformTime::Cycles();)
}

/**
* Mark the textures/meshes with a timestamp. They're about to lose their location-based heuristic and we don't want them to
* start using LastRenderTime heuristic for a few seconds until they are garbage collected!
*
* @param RemovedRenderAssets	List of removed textures or meshes.
*/
void FRenderAssetStreamingManager::SetRenderAssetsRemovedTimestamp(const FRemovedRenderAssetArray& RemovedRenderAssets)
{
	const double CurrentTime = FApp::GetCurrentTime();
	for ( int32 Idx=0; Idx < RemovedRenderAssets.Num(); ++Idx )
	{
		// When clearing references to textures/meshes, those textures/meshes could be already deleted.
		// This happens because we don't clear texture/mesh references in RemoveStreamingRenderAsset.
		const UStreamableRenderAsset* Asset = RemovedRenderAssets[Idx];
		if (!ReferencedRenderAssets.Contains(Asset)) continue;

		FStreamingRenderAsset* StreamingRenderAsset = GetStreamingRenderAsset(Asset);
		if (StreamingRenderAsset)
		{
			StreamingRenderAsset->InstanceRemovedTimestamp = CurrentTime;
		}
	}
}


void FRenderAssetStreamingManager::NotifyPrimitiveUpdated( const UPrimitiveComponent* Primitive )
{
	STAT(GatheredStats.CallbacksCycles = -(int32)FPlatformTime::Cycles();)

	// This can sometime be called from async threads if actor constructor ends up calling SetStaticMesh, for example.
	// When this happens, the states will be initialized when the components render states will be set.
	if (IsInGameThread() && bUseDynamicStreaming && Primitive && !Primitive->bIgnoreStreamingManagerUpdate)
	{
		FScopeLock ScopeLock(&CriticalSection);

		// Check if there is a pending renderstate update, useful since streaming data can be updated in UPrimitiveComponent::CreateRenderState_Concurrent().
		// We handle this here to prevent the primitive from being updated twice in the same frame.
		const bool bHasRenderStateUpdateScheduled = !Primitive->IsRegistered() || !Primitive->IsRenderStateCreated() || Primitive->IsRenderStateDirty();
		bool bUpdatePrimitive = false;

		if (Primitive->bHandledByStreamingManagerAsDynamic)
		{
			// If an update is already scheduled and it is already handled as dynamic, nothing to do.
			bUpdatePrimitive = !bHasRenderStateUpdateScheduled;
		}
		else if (Primitive->bAttachedToStreamingManagerAsStatic)
		{
			// Change this primitive from being handled as static to being handled as dynamic.
			// This is required because the static data can not be updated.
			RemoveStaticReferences(Primitive);

			Primitive->bHandledByStreamingManagerAsDynamic = true;
			bUpdatePrimitive = !bHasRenderStateUpdateScheduled;
		}
		else
		{
			// If neither flag are set, NotifyPrimitiveUpdated() was called on a new primitive, which will be updated correctly when its render state gets created.
			// Don't force a dynamic update here since a static primitive can still go through the static path at this point.
		}

		if (bUpdatePrimitive)
		{
			FStreamingTextureLevelContext LevelContext(EMaterialQualityLevel::Num, Primitive);
			DynamicComponentManager.Add(Primitive, LevelContext);
		}
	}

	STAT(GatheredStats.CallbacksCycles += FPlatformTime::Cycles();)
}

/**
 * Called when a primitive has had its textures/mesh changed.
 * Only affects primitives that were already attached.
 * Replaces previous info.
 */
void FRenderAssetStreamingManager::NotifyPrimitiveUpdated_Concurrent( const UPrimitiveComponent* Primitive )
{
	STAT(int32 CallbackCycle = -(int32)FPlatformTime::Cycles();)

	// The level context is not used currently.
	if (bUseDynamicStreaming && Primitive)
	{
		FScopeLock ScopeLock(&CriticalSection);
		FStreamingTextureLevelContext LevelContext(EMaterialQualityLevel::Num);
		DynamicComponentManager.Add(Primitive, LevelContext);
	}

	STAT(CallbackCycle += (int32)FPlatformTime::Cycles();)
	STAT(FPlatformAtomics::InterlockedAdd(&GatheredStats.CallbacksCycles, CallbackCycle));
}

void FRenderAssetStreamingManager::SyncStates(bool bCompleteFullUpdateCycle)
{
	// Finish the current update cycle. 
	while (ProcessingStage != 0 && bCompleteFullUpdateCycle)
	{
		UpdateResourceStreaming(0, false);
	}

	// Wait for async tasks
	AsyncWork->EnsureCompletion();
	RenderAssetInstanceAsyncWork->EnsureCompletion();

	// Update any pending states, including added/removed textures/meshes.
	// Doing so when ProcessingStage != 0 risk invalidating the indices in the async task used in StreamRenderAssets().
	// This would in practice postpone some of the load and cancel requests.
	UpdatePendingStates(false);
}

/**
 * Returns the corresponding FStreamingRenderAsset for a texture or mesh.
 */
FStreamingRenderAsset* FRenderAssetStreamingManager::GetStreamingRenderAsset( const UStreamableRenderAsset* RenderAsset )
{
	FScopeLock ScopeLock(&CriticalSection);

	if (RenderAsset && StreamingRenderAssets.IsValidIndex(RenderAsset->StreamingIndex))
	{
		FStreamingRenderAsset* StreamingRenderAsset = &StreamingRenderAssets[RenderAsset->StreamingIndex];

		// If the texture/mesh don't match, this means the texture/mesh is pending in PendingStreamingRenderAssets, for which no FStreamingRenderAsset* is yet allocated.
		// If this is not acceptable, the caller should first synchronize everything through SyncStates
		return StreamingRenderAsset->RenderAsset == RenderAsset ? StreamingRenderAsset : nullptr;
	}
	else
	{
		return nullptr;
	}
}

/**
 * Updates streaming for an individual texture/mesh, taking into account all view infos.
 *
 * @param RenderAsset	Texture or mesh to update
 */
void FRenderAssetStreamingManager::UpdateIndividualRenderAsset( UStreamableRenderAsset* RenderAsset )
{
	FScopeLock ScopeLock(&CriticalSection);

	if (!IStreamingManager::Get().IsStreamingEnabled() || !RenderAsset) return;

	// Because we want to priorize loading of this texture, 
	// don't process everything as this would send load requests for all textures.
	SyncStates(false);

	FStreamingRenderAsset* StreamingRenderAsset = GetStreamingRenderAsset(RenderAsset);
	if (!StreamingRenderAsset) return;

	const int32* NumStreamedMips;
	const int32 NumLODGroups = GetNumStreamedMipsArray(StreamingRenderAsset->RenderAssetType, NumStreamedMips);

	StreamingRenderAsset->UpdateDynamicData(NumStreamedMips, NumLODGroups, Settings, false);

	if (StreamingRenderAsset->bForceFullyLoad) // Somewhat expected at this point.
	{
		StreamingRenderAsset->WantedMips = StreamingRenderAsset->BudgetedMips = StreamingRenderAsset->MaxAllowedMips;
	}

	StreamingRenderAsset->StreamWantedMips(*this);
}

/**
 * Not thread-safe: Updates a portion (as indicated by 'StageIndex') of all streaming textures,
 * allowing their streaming state to progress.
 *
 * @param Context			Context for the current stage (frame)
 * @param StageIndex		Current stage index
 * @param NumUpdateStages	Number of texture update stages
 */
void FRenderAssetStreamingManager::UpdateStreamingRenderAssets( int32 StageIndex, int32 NumUpdateStages, bool bWaitForMipFading )
{
	if ( StageIndex == 0 )
	{
		CurrentUpdateStreamingRenderAssetIndex = 0;
		InflightRenderAssets.Reset();
	}

	int32 StartIndex = CurrentUpdateStreamingRenderAssetIndex;
	int32 EndIndex = StreamingRenderAssets.Num() * (StageIndex + 1) / NumUpdateStages;
	for ( int32 Index=StartIndex; Index < EndIndex; ++Index )
	{
		FStreamingRenderAsset& StreamingRenderAsset = StreamingRenderAssets[Index];
		FPlatformMisc::Prefetch( &StreamingRenderAsset + 1 );

		// Is this texture/mesh marked for removal? Will get cleanup once the async task is done.
		if (!StreamingRenderAsset.RenderAsset) continue;

		STAT(int32 PreviousResidentMips = StreamingRenderAsset.ResidentMips;)

		const int32* NumStreamedMips;
		const int32 NumLODGroups = GetNumStreamedMipsArray(StreamingRenderAsset.RenderAssetType, NumStreamedMips);

		StreamingRenderAsset.UpdateDynamicData(NumStreamedMips, NumLODGroups, Settings, bWaitForMipFading);

		// Make a list of each texture/mesh that can potentially require additional UpdateStreamingStatus
		if (StreamingRenderAsset.bInFlight)
		{
			InflightRenderAssets.Add(Index);
		}

#if STATS
		if (StreamingRenderAsset.ResidentMips > PreviousResidentMips)
		{
			GatheredStats.MipIOBandwidth += StreamingRenderAsset.GetSize(StreamingRenderAsset.ResidentMips) - StreamingRenderAsset.GetSize(PreviousResidentMips);
		}
#endif
	}
	CurrentUpdateStreamingRenderAssetIndex = EndIndex;
}

static TAutoConsoleVariable<int32> CVarTextureStreamingAmortizeCPUToGPUCopy(
	TEXT("r.Streaming.AmortizeCPUToGPUCopy"),
	0,
	TEXT("If set and r.Streaming.MaxNumTexturesToStreamPerFrame > 0, limit the number of 2D textures ")
	TEXT("streamed from CPU memory to GPU memory each frame"),
	ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarTextureStreamingMaxNumTexturesToStreamPerFrame(
	TEXT("r.Streaming.MaxNumTexturesToStreamPerFrame"),
	0,
	TEXT("Maximum number of 2D textures allowed to stream from CPU memory to GPU memory each frame. ")
	TEXT("<= 0 means no limit. This has no effect if r.Streaming.AmortizeCPUToGPUCopy is not set"),
	ECVF_Scalability);

static FORCEINLINE bool ShouldAmortizeMipCopies()
{
	return CVarTextureStreamingAmortizeCPUToGPUCopy.GetValueOnGameThread()
		&& CVarTextureStreamingMaxNumTexturesToStreamPerFrame.GetValueOnGameThread() > 0;
}

/**
 * Stream textures/meshes in/out, based on the priorities calculated by the async work.
 * @param bProcessEverything	Whether we're processing all textures in one go
 */
void FRenderAssetStreamingManager::StreamRenderAssets( bool bProcessEverything )
{
	const FRenderAssetStreamingMipCalcTask& AsyncTask = AsyncWork->GetTask();

	// Note that render asset indices referred by the async task could be outdated if UpdatePendingStates() was called between the
	// end of the async task work, and this call to StreamRenderAssets(). This happens when SyncStates(false) is called.

	if (!bPauseRenderAssetStreaming || bProcessEverything)
	{
		for (int32 AssetIndex : AsyncTask.GetCancelationRequests())
		{
			if (StreamingRenderAssets.IsValidIndex(AssetIndex))
			{
				StreamingRenderAssets[AssetIndex].CancelPendingMipChangeRequest();
			}
		}

		if (!bProcessEverything && ShouldAmortizeMipCopies())
		{
			// Ignore remaining requests since they may be outdated already
			PendingMipCopyRequests.Reset();
			CurrentPendingMipCopyRequestIdx = 0;

			// Make copies of the requests so that they can be processed later
			for (int32 AssetIndex : AsyncTask.GetLoadRequests())
			{
				if (StreamingRenderAssets.IsValidIndex(AssetIndex)
					&& StreamingRenderAssets[AssetIndex].RenderAsset)
				{
					FStreamingRenderAsset& StreamingRenderAsset = StreamingRenderAssets[AssetIndex];
					StreamingRenderAsset.CacheStreamingMetaData();
					new (PendingMipCopyRequests) FPendingMipCopyRequest(StreamingRenderAsset.RenderAsset, AssetIndex);
				}
			}
		}
		else
		{
			for (int32 AssetIndex : AsyncTask.GetLoadRequests())
			{
				if (StreamingRenderAssets.IsValidIndex(AssetIndex))
				{
					StreamingRenderAssets[AssetIndex].StreamWantedMips(*this);
				}
			}
		}
	}
	
	for (int32 AssetIndex : AsyncTask.GetPendingUpdateDirties())
	{
		if (StreamingRenderAssets.IsValidIndex(AssetIndex))
		{
			FStreamingRenderAsset& StreamingRenderAsset = StreamingRenderAssets[AssetIndex];
			const bool bNewState = StreamingRenderAsset.HasUpdatePending(bPauseRenderAssetStreaming, AsyncTask.HasAnyView());

			// Always update the texture/mesh and the streaming texture/mesh together to make sure they are in sync.
			StreamingRenderAsset.bHasUpdatePending = bNewState;
			if (StreamingRenderAsset.RenderAsset)
			{
				StreamingRenderAsset.RenderAsset->bHasStreamingUpdatePending = bNewState;
			}
		}
	}
}

void FRenderAssetStreamingManager::ProcessPendingMipCopyRequests()
{
	if (!ShouldAmortizeMipCopies())
	{
		return;
	}

	int32 NumRemainingRequests = CVarTextureStreamingMaxNumTexturesToStreamPerFrame.GetValueOnGameThread();

	while (NumRemainingRequests
		&& CurrentPendingMipCopyRequestIdx < PendingMipCopyRequests.Num())
	{
		const FPendingMipCopyRequest& Request = PendingMipCopyRequests[CurrentPendingMipCopyRequestIdx++];

		if (Request.RenderAsset)
		{
			FStreamingRenderAsset* StreamingRenderAsset = nullptr;

			if (StreamingRenderAssets.IsValidIndex(Request.CachedIdx)
				&& StreamingRenderAssets[Request.CachedIdx].RenderAsset == Request.RenderAsset)
			{
				StreamingRenderAsset = &StreamingRenderAssets[Request.CachedIdx];
			}
			else if (ReferencedRenderAssets.Contains(Request.RenderAsset))
			{
				// Texture is still valid but its index has been changed
				check(StreamingRenderAssets.IsValidIndex(Request.RenderAsset->StreamingIndex));
				StreamingRenderAsset = &StreamingRenderAssets[Request.RenderAsset->StreamingIndex];
			}

			if (StreamingRenderAsset)
			{
				StreamingRenderAsset->StreamWantedMipsUsingCachedData(*this);
				--NumRemainingRequests;
			}
		}
	}
}

void FRenderAssetStreamingManager::CheckUserSettings()
{	
	if (CVarStreamingUseFixedPoolSize.GetValueOnGameThread() == 0)
	{
		const int32 PoolSizeSetting = CVarStreamingPoolSize.GetValueOnGameThread();

		int64 TexturePoolSize = GTexturePoolSize;
		if (PoolSizeSetting == -1)
		{
			FTextureMemoryStats Stats;
			RHIGetTextureMemoryStats(Stats);
			if (GPoolSizeVRAMPercentage > 0 && Stats.TotalGraphicsMemory > 0)
			{
				TexturePoolSize = Stats.TotalGraphicsMemory * GPoolSizeVRAMPercentage / 100;
			}
		}
		else
		{
			TexturePoolSize = int64(PoolSizeSetting) * 1024ll * 1024ll;
		}

		if (TexturePoolSize != GTexturePoolSize)
		{
			UE_LOG(LogContentStreaming,Log,TEXT("Texture pool size now %d MB"), int32(TexturePoolSize/1024/1024));
			GTexturePoolSize = TexturePoolSize;
		}
	}
}

void FRenderAssetStreamingManager::SetLastUpdateTime()
{
	// Update the last update time.
	float WorldTime = 0;

	for (int32 LevelIndex = 0; LevelIndex < LevelRenderAssetManagers.Num(); ++LevelIndex)
	{
		if (LevelRenderAssetManagers[LevelIndex] == nullptr)
		{
			continue;
		}

		// Update last update time only if there is a reasonable threshold to define visibility.
		WorldTime = LevelRenderAssetManagers[LevelIndex]->GetWorldTime();
		if (WorldTime > 0)
		{
			break;
		}
	}

	if (WorldTime> 0)
	{
		LastWorldUpdateTime = WorldTime - .5f;
	}
	else if (GIsEditor)
	{
		LastWorldUpdateTime = -FLT_MAX; // In editor, visibility is not taken into consideration unless in PIE.
	}
}

void FRenderAssetStreamingManager::UpdateStats()
{
	float DeltaStatTime = (float)(GatheredStats.Timestamp - DisplayedStats.Timestamp);
	if (DeltaStatTime > SMALL_NUMBER)
	{
		GatheredStats.MipIOBandwidth = DeltaStatTime > SMALL_NUMBER ? GatheredStats.MipIOBandwidth / DeltaStatTime : 0;
	}
	DisplayedStats = GatheredStats;
	GatheredStats.CallbacksCycles = 0;
	GatheredStats.MipIOBandwidth = 0;
	MemoryOverBudget = DisplayedStats.OverBudget;
	MaxEverRequired = FMath::Max<int64>(MaxEverRequired, DisplayedStats.RequiredPool);
}

void FRenderAssetStreamingManager::UpdateCSVOnlyStats()
{
	DisplayedStats = GatheredStats;
}

void FRenderAssetStreamingManager::LogViewLocationChange()
{
#if STREAMING_LOG_VIEWCHANGES
	static bool bWasLocationOveridden = false;
	bool bIsLocationOverridden = false;
	for ( int32 ViewIndex=0; ViewIndex < CurrentViewInfos.Num(); ++ViewIndex )
	{
		FStreamingViewInfo& ViewInfo = CurrentViewInfos[ViewIndex];
		if ( ViewInfo.bOverrideLocation )
		{
			bIsLocationOverridden = true;
			break;
		}
	}
	if ( bIsLocationOverridden != bWasLocationOveridden )
	{
		UE_LOG(LogContentStreaming, Log, TEXT("Texture streaming view location is now %s."), bIsLocationOverridden ? TEXT("OVERRIDDEN") : TEXT("normal") );
		bWasLocationOveridden = bIsLocationOverridden;
	}
#endif
}

/**
 * Main function for the texture streaming system, based on texture priorities and asynchronous processing.
 * Updates streaming, taking into account all view infos.
 *
 * @param DeltaTime				Time since last call in seconds
 * @param bProcessEverything	[opt] If true, process all resources with no throttling limits
 */

static TAutoConsoleVariable<int32> CVarUseBackgroundThreadPool(
	TEXT("r.Streaming.UseBackgroundThreadPool"),
	1,
	TEXT("If true, use the background thread pool for mip calculations."));

class FUpdateStreamingRenderAssetsTask
{
	FEvent* CompletionEvent;
	FRenderAssetStreamingManager* Manager;
	int32 StageIdx;
	int32 NumUpdateStages;
	bool bWaitForMipFading;
public:
	FUpdateStreamingRenderAssetsTask(
		FEvent* InCompletionEvent,
		FRenderAssetStreamingManager* InManager,
		int32 InStageIdx,
		int32 InNumUpdateStages,
		bool bInWaitForMipFading)
		: CompletionEvent(InCompletionEvent)
		, Manager(InManager)
		, StageIdx(InStageIdx)
		, NumUpdateStages(InNumUpdateStages)
		, bWaitForMipFading(bInWaitForMipFading)
	{
	}
	static FORCEINLINE TStatId GetStatId()
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FUpdateStreamingRenderAssetsTask, STATGROUP_TaskGraphTasks);
	}
	static FORCEINLINE ENamedThreads::Type GetDesiredThread()
	{
		return ENamedThreads::AnyHiPriThreadHiPriTask;
	}
	static FORCEINLINE ESubsequentsMode::Type GetSubsequentsMode()
	{
		return ESubsequentsMode::FireAndForget;
	}
	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		Manager->UpdateStreamingRenderAssets(StageIdx, NumUpdateStages, bWaitForMipFading);
		CompletionEvent->Trigger();
	}
};

void FRenderAssetStreamingManager::UpdateResourceStreaming( float DeltaTime, bool bProcessEverything/*=false*/ )
{
	FScopeLock ScopeLock(&CriticalSection);

	SCOPE_CYCLE_COUNTER(STAT_RenderAssetStreaming_GameThreadUpdateTime);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderAssetStreaming);
	CSV_SCOPED_SET_WAIT_STAT(RenderAssetStreaming);

	const bool bUseThreadingForPerf = FApp::ShouldUseThreadingForPerformance();

	LogViewLocationChange();
	STAT(DisplayedStats.Apply();)

	CSV_CUSTOM_STAT(TextureStreaming, StreamingPool, ((float)(DisplayedStats.RequiredPool + (GPoolSizeVRAMPercentage > 0 ? 0 : DisplayedStats.NonStreamingMips))) / (1024.0f * 1024.0f), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(TextureStreaming, SafetyPool, ((float)DisplayedStats.SafetyPool) / (1024.0f * 1024.0f), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(TextureStreaming, TemporaryPool, ((float)DisplayedStats.TemporaryPool) / (1024.0f * 1024.0f), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(TextureStreaming, CachedMips, ((float)DisplayedStats.CachedMips) / (1024.0f * 1024.0f), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(TextureStreaming, WantedMips, ((float)DisplayedStats.WantedMips) / (1024.0f * 1024.0f), ECsvCustomStatOp::Set);

	RenderAssetInstanceAsyncWork->EnsureCompletion();

	if (NumRenderAssetProcessingStages <= 0 || bProcessEverything)
	{
		if (!AsyncWork->IsDone())
		{	// Is the AsyncWork is running for some reason? (E.g. we reset the system by simply setting ProcessingStage to 0.)
			AsyncWork->EnsureCompletion();
		}

		ProcessingStage = 0;
		NumRenderAssetProcessingStages = Settings.FramesForFullUpdate;

		// Update Thread Data
		SetLastUpdateTime();
		UpdateStreamingRenderAssets(0, 1, false);

		UpdatePendingStates(true);
		PrepareAsyncTask(bProcessEverything || Settings.bStressTest);
		AsyncWork->StartSynchronousTask();

		StreamRenderAssets(bProcessEverything);

		STAT(GatheredStats.SetupAsyncTaskCycles = 0);
		STAT(GatheredStats.UpdateStreamingDataCycles = 0);
		STAT(GatheredStats.StreamTexturesCycles = 0);
		STAT(GatheredStats.CallbacksCycles = 0);
#if STATS
		UpdateStats();
#elif UE_BUILD_TEST
		UpdateCSVOnlyStats();
#endif // STATS
	}
	else if (ProcessingStage == 0)
	{
		STAT(GatheredStats.SetupAsyncTaskCycles = -(int32)FPlatformTime::Cycles();)

		NumRenderAssetProcessingStages = Settings.FramesForFullUpdate;

		if (!AsyncWork->IsDone())
		{	// Is the AsyncWork is running for some reason? (E.g. we reset the system by simply setting ProcessingStage to 0.)
			AsyncWork->EnsureCompletion();
		}

		// Here we rely on dynamic components to be updated on the last stage, in order to split the workload. 
		UpdatePendingStates(false);
		PrepareAsyncTask(bProcessEverything || Settings.bStressTest);
		AsyncWork->StartBackgroundTask(CVarUseBackgroundThreadPool.GetValueOnGameThread() ? GBackgroundPriorityThreadPool : GThreadPool);
		++ProcessingStage;

		STAT(GatheredStats.SetupAsyncTaskCycles += FPlatformTime::Cycles();)
	}
	else if (ProcessingStage <= NumRenderAssetProcessingStages)
	{
		STAT(int32 StartTime = (int32)FPlatformTime::Cycles();)

		if (ProcessingStage == 1)
		{
			SetLastUpdateTime();
		}

		FEvent* SyncEvent = nullptr;
		// Optimization: overlapping UpdateStreamingRenderAssets() and IncrementalUpdate();
		// Restrict this optimization to platforms tested to have a win;
		// Platforms tested and results (ave exec time of UpdateResourceStreaming):
		//   PS4 Pro - from ~0.55 ms/frame to ~0.15 ms/frame
		//   XB1 X - from ~0.45 ms/frame to ~0.17 ms/frame
		const bool bOverlappedExecution = bUseThreadingForPerf && CVarStreamingOverlapAssetAndLevelTicks.GetValueOnGameThread();
		if (bOverlappedExecution)
		{
			SyncEvent = FPlatformProcess::GetSynchEventFromPool(false);
			check(SyncEvent);
			TGraphTask<FUpdateStreamingRenderAssetsTask>::CreateTask(nullptr, ENamedThreads::GameThread)
				.ConstructAndDispatchWhenReady(SyncEvent, this, ProcessingStage - 1, NumRenderAssetProcessingStages, DeltaTime > 0.f);
		}
		else
		{
			UpdateStreamingRenderAssets(ProcessingStage - 1, NumRenderAssetProcessingStages, DeltaTime > 0.f);
		}

		IncrementalUpdate(1.f / (float)FMath::Max(NumRenderAssetProcessingStages - 1, 1), true); // -1 since we don't want to do anything at stage 0.
		++ProcessingStage;

		if (bOverlappedExecution)
		{
			SyncEvent->Wait();
			FPlatformProcess::ReturnSynchEventToPool(SyncEvent);
		}
		STAT(GatheredStats.UpdateStreamingDataCycles = FMath::Max<uint32>(ProcessingStage > 2 ? GatheredStats.UpdateStreamingDataCycles : 0, FPlatformTime::Cycles() - StartTime);)
	}
	else if (AsyncWork->IsDone())
	{
		STAT(GatheredStats.StreamTexturesCycles = -(int32)FPlatformTime::Cycles();)

		// Since this step is lightweight, tick each texture inflight here, to accelerate the state changes.
		for (int32 TextureIndex : InflightRenderAssets)
		{
			StreamingRenderAssets[TextureIndex].UpdateStreamingStatus(DeltaTime > 0);
		}

		StreamRenderAssets(bProcessEverything);
		// Release the old view now as the destructors can be expensive. Now only the dynamic manager holds a ref.
		AsyncWork->GetTask().ReleaseAsyncViews();
		IncrementalUpdate(1.f / (float)FMath::Max(NumRenderAssetProcessingStages - 1, 1), true); // Just in case continue any pending update.
		DynamicComponentManager.PrepareAsyncView();

		ProcessingStage = 0;

		STAT(GatheredStats.StreamTexturesCycles += FPlatformTime::Cycles();)
#if STATS
			UpdateStats();
#elif UE_BUILD_TEST
			UpdateCSVOnlyStats();
#endif // STATS
	}

	if (!bProcessEverything)
	{
		ProcessPendingMipCopyRequests();
	}

	if (bUseThreadingForPerf)
	{
		RenderAssetInstanceAsyncWork->StartBackgroundTask(GThreadPool);
	}
	else
	{
		RenderAssetInstanceAsyncWork->StartSynchronousTask();
	}
}

/**
 * Blocks till all pending requests are fulfilled.
 *
 * @param TimeLimit		Optional time limit for processing, in seconds. Specifying 0 means infinite time limit.
 * @param bLogResults	Whether to dump the results to the log.
 * @return				Number of streaming requests still in flight, if the time limit was reached before they were finished.
 */
int32 FRenderAssetStreamingManager::BlockTillAllRequestsFinished( float TimeLimit /*= 0.0f*/, bool bLogResults /*= false*/ )
{
	FScopeLock ScopeLock(&CriticalSection);
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FRenderAssetStreamingManager_BlockTillAllRequestsFinished);

	double StartTime = FPlatformTime::Seconds();

	while (ensure(!IsAssetStreamingSuspended()))
	{
		int32 NumOfInFlights = 0;

		for (FStreamingRenderAsset& StreamingRenderAsset : StreamingRenderAssets)
		{
			StreamingRenderAsset.UpdateStreamingStatus(false);
			if (StreamingRenderAsset.bInFlight)
			{
				++NumOfInFlights;
			}
		}

		if (NumOfInFlights && (TimeLimit == 0 || (float)(FPlatformTime::Seconds() - StartTime) < TimeLimit))
		{
			FlushRenderingCommands();
			FPlatformProcess::Sleep(RENDER_ASSET_STREAMING_SLEEP_DT);
		}
		else
		{
			if (bLogResults)
			{
				UE_LOG(LogContentStreaming, Log, TEXT("Blocking on texture streaming: %.1f ms (%d still in flight)"), (float)(FPlatformTime::Seconds() - StartTime) * 1000, NumOfInFlights);

			}
			return NumOfInFlights;
		}
	}
	return 0;
}

void FRenderAssetStreamingManager::GetObjectReferenceBounds(const UObject* RefObject, TArray<FBox>& AssetBoxes)
{
	FScopeLock ScopeLock(&CriticalSection);
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FRenderAssetStreamingManager_GetObjectReferenceBounds);

	const UStreamableRenderAsset* RenderAsset = Cast<const UStreamableRenderAsset>(RefObject);
	if (RenderAsset)
	{
		for (FLevelRenderAssetManager *LevelManager : LevelRenderAssetManagers)
		{
			if (LevelManager == nullptr)
			{
				continue;
			}

			const FRenderAssetInstanceView* View = LevelManager->GetRawAsyncView();
			if (View)
			{
				for (auto It = View->GetElementIterator(RenderAsset); It; ++It)
				{
					AssetBoxes.Add(It.GetBounds().GetBox());
				}
			}
		}

		const FRenderAssetInstanceView* View = DynamicComponentManager.GetAsyncView(false);
		if (View)
		{
			for (auto It = View->GetElementIterator(RenderAsset); It; ++It)
			{
				AssetBoxes.Add(It.GetBounds().GetBox());
			}
		}
	}
}

void FRenderAssetStreamingManager::PropagateLightingScenarioChange()
{
	FScopeLock ScopeLock(&CriticalSection);
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FRenderAssetStreamingManager_PropagateLightingScenarioChange);

	// Note that dynamic components don't need to be handled because their renderstates are updated, which triggers and update.
	
	TArray<ULevel*, TInlineAllocator<32> > Levels;
	for (FLevelRenderAssetManager* LevelManager : LevelRenderAssetManagers)
	{
		if (LevelManager!=nullptr)
		{
			Levels.Push(LevelManager->GetLevel());
			LevelManager->Remove(nullptr);
		}
	}

	LevelRenderAssetManagers.Empty();

	for (ULevel* Level : Levels)
	{
		AddLevel(Level);
	}
}

#if STATS_FAST
bool FRenderAssetStreamingManager::HandleDumpTextureStreamingStatsCommand( const TCHAR* Cmd, FOutputDevice& Ar )
{
	FScopeLock ScopeLock(&CriticalSection);

	Ar.Logf( TEXT("Current Texture Streaming Stats") );
	Ar.Logf( TEXT("  Textures In Memory, Current (KB) = %f"), MaxStreamingTexturesSize / 1024.0f);
	Ar.Logf( TEXT("  Textures In Memory, Target (KB) =  %f"), MaxOptimalTextureSize / 1024.0f );
	Ar.Logf( TEXT("  Over Budget (KB) =                 %f"), MaxStreamingOverBudget / 1024.0f );
	Ar.Logf( TEXT("  Pool Memory Used (KB) =            %f"), MaxTexturePoolAllocatedSize / 1024.0f );
	Ar.Logf( TEXT("  Num Wanting Textures =             %d"), MaxNumWantingTextures );
	MaxStreamingTexturesSize = 0;
	MaxOptimalTextureSize = 0;
	MaxStreamingOverBudget = MIN_int64;
	MaxTexturePoolAllocatedSize = 0;
	MaxNumWantingTextures = 0;
	return true;
}
#endif // STATS_FAST

#if !UE_BUILD_SHIPPING

bool FRenderAssetStreamingManager::HandleListStreamingRenderAssetsCommand( const TCHAR* Cmd, FOutputDevice& Ar )
{
	FScopeLock ScopeLock(&CriticalSection);

	SyncStates(true);

	const bool bShouldOnlyListUnkownRef = FParse::Command(&Cmd, TEXT("UNKOWNREF"));

	// Sort texture/mesh by names so that the state can be compared between runs.
	TMap<FString, int32> SortedRenderAssets;
	for ( int32 Idx=0; Idx < StreamingRenderAssets.Num(); ++Idx)
	{
		const FStreamingRenderAsset& StreamingRenderAsset = StreamingRenderAssets[Idx];
		if (!StreamingRenderAsset.RenderAsset) continue;
		if (bShouldOnlyListUnkownRef && !StreamingRenderAsset.bUseUnkownRefHeuristic) continue;

		SortedRenderAssets.Add(StreamingRenderAsset.RenderAsset->GetFullName(), Idx);
	}

	SortedRenderAssets.KeySort(TLess<FString>());

	for (TMap<FString, int32>::TConstIterator It(SortedRenderAssets); It; ++It)
	{
		const FStreamingRenderAsset& StreamingRenderAsset = StreamingRenderAssets[It.Value()];
		const UStreamableRenderAsset* RenderAsset = StreamingRenderAsset.RenderAsset;
		const typename FStreamingRenderAsset::EAssetType AssetType = StreamingRenderAsset.RenderAssetType;
		
		UE_LOG(LogContentStreaming, Log,  TEXT("%s [%d] : %s"),
			FStreamingRenderAsset::GetStreamingAssetTypeStr(AssetType),
			It.Value(),
			*RenderAsset->GetFullName() );

		int32 CurrentMipIndex = FMath::Max(RenderAsset->GetNumMipsForStreaming() - StreamingRenderAsset.ResidentMips, 0);
		int32 WantedMipIndex = FMath::Max(RenderAsset->GetNumMipsForStreaming() - StreamingRenderAsset.GetPerfectWantedMips(), 0);
		int32 MaxAllowedMipIndex = FMath::Max(RenderAsset->GetNumMipsForStreaming() - StreamingRenderAsset.MaxAllowedMips, 0);

		if (AssetType == FStreamingRenderAsset::AT_Texture)
		{
			const UTexture2D* Texture = CastChecked<UTexture2D>(RenderAsset);
			const TIndirectArray<struct FTexture2DMipMap>& Mips = Texture->PlatformData->Mips;

			if (StreamingRenderAsset.LastRenderTime != MAX_FLT)
			{
				UE_LOG(LogContentStreaming, Log, TEXT("    Current=%dx%d Wanted=%dx%d MaxAllowed=%dx%d LastRenderTime=%.3f BudgetBias=%d Group=%s"),
					Mips[CurrentMipIndex].SizeX, Mips[CurrentMipIndex].SizeY,
					Mips[WantedMipIndex].SizeX, Mips[WantedMipIndex].SizeY,
					Mips[MaxAllowedMipIndex].SizeX, Mips[MaxAllowedMipIndex].SizeY,
					StreamingRenderAsset.LastRenderTime,
					StreamingRenderAsset.BudgetMipBias,
					UTexture::GetTextureGroupString(static_cast<TextureGroup>(StreamingRenderAsset.LODGroup)));
			}
			else
			{
				UE_LOG(LogContentStreaming, Log, TEXT("    Current=%dx%d Wanted=%dx%d MaxAllowed=%dx%d BudgetBias=%d Group=%s"),
					Mips[CurrentMipIndex].SizeX, Mips[CurrentMipIndex].SizeY,
					Mips[WantedMipIndex].SizeX, Mips[WantedMipIndex].SizeY,
					Mips[MaxAllowedMipIndex].SizeX, Mips[MaxAllowedMipIndex].SizeY,
					StreamingRenderAsset.BudgetMipBias,
					UTexture::GetTextureGroupString(static_cast<TextureGroup>(StreamingRenderAsset.LODGroup)));
			}
		}
		else
		{
			const float LastRenderTime = StreamingRenderAsset.LastRenderTime;
			const UStaticMesh* StaticMesh = Cast<UStaticMesh>(RenderAsset);
			FString LODGroupName = TEXT("Unknown");
#if WITH_EDITORONLY_DATA
			if (StaticMesh)
			{
				LODGroupName = StaticMesh->LODGroup.ToString();
			}
#endif
			UE_LOG(LogContentStreaming, Log, TEXT("    CurrentLOD=%d WantedLOD=%d MaxAllowedLOD=%d LastRenderTime=%s BudgetBias=%d Group=%s"),
				CurrentMipIndex,
				WantedMipIndex,
				MaxAllowedMipIndex,
				LastRenderTime == MAX_FLT ? TEXT("NotTracked") : *FString::Printf(TEXT("%.3f"), LastRenderTime),
				StreamingRenderAsset.BudgetMipBias,
				*LODGroupName);
		}
	}
	return true;
}

bool FRenderAssetStreamingManager::HandleResetMaxEverRequiredRenderAssetMemoryCommand(const TCHAR* Cmd, FOutputDevice& Ar)
{
	FScopeLock ScopeLock(&CriticalSection);

	Ar.Logf(TEXT("OldMax: %u MaxEverRequired Reset."), MaxEverRequired);
	ResetMaxEverRequired();	
	return true;
}

bool FRenderAssetStreamingManager::HandleLightmapStreamingFactorCommand( const TCHAR* Cmd, FOutputDevice& Ar )
{
	FScopeLock ScopeLock(&CriticalSection);

	FString FactorString(FParse::Token(Cmd, 0));
	float NewFactor = ( FactorString.Len() > 0 ) ? FCString::Atof(*FactorString) : GLightmapStreamingFactor;
	if ( NewFactor >= 0.0f )
	{
		GLightmapStreamingFactor = NewFactor;
	}
	Ar.Logf( TEXT("Lightmap streaming factor: %.3f (lower values makes streaming more aggressive)."), GLightmapStreamingFactor );
	return true;
}

bool FRenderAssetStreamingManager::HandleCancelRenderAssetStreamingCommand( const TCHAR* Cmd, FOutputDevice& Ar )
{
	FScopeLock ScopeLock(&CriticalSection);

	UTexture2D::CancelPendingTextureStreaming();
	UStaticMesh::CancelAllPendingStreamingActions();
	// TODO: USkeletalMesh
	return true;
}

bool FRenderAssetStreamingManager::HandleShadowmapStreamingFactorCommand( const TCHAR* Cmd, FOutputDevice& Ar )
{
	FScopeLock ScopeLock(&CriticalSection);

	FString FactorString(FParse::Token(Cmd, 0));
	float NewFactor = ( FactorString.Len() > 0 ) ? FCString::Atof(*FactorString) : GShadowmapStreamingFactor;
	if ( NewFactor >= 0.0f )
	{
		GShadowmapStreamingFactor = NewFactor;
	}
	Ar.Logf( TEXT("Shadowmap streaming factor: %.3f (lower values makes streaming more aggressive)."), GShadowmapStreamingFactor );
	return true;
}

bool FRenderAssetStreamingManager::HandleNumStreamedMipsCommand( const TCHAR* Cmd, FOutputDevice& Ar )
{
	FScopeLock ScopeLock(&CriticalSection);

	FString NumTextureString(FParse::Token(Cmd, 0));
	FString NumMipsString(FParse::Token(Cmd, 0));
	FString LODGroupType(FParse::Token(Cmd, false));
	int32 LODGroup = ( NumTextureString.Len() > 0 ) ? FCString::Atoi(*NumTextureString) : MAX_int32;
	int32 NumMips = ( NumMipsString.Len() > 0 ) ? FCString::Atoi(*NumMipsString) : MAX_int32;
	if ((LODGroupType == TEXT("") || LODGroupType == TEXT("Texture")) && LODGroup >= 0 && LODGroup < TEXTUREGROUP_MAX)
	{
		FTextureLODGroup& TexGroup = UDeviceProfileManager::Get().GetActiveProfile()->GetTextureLODSettings()->GetTextureLODGroup(TextureGroup(LODGroup));
		if ( NumMips >= -1 && NumMips <= MAX_TEXTURE_MIP_COUNT )
		{
			TexGroup.NumStreamedMips = NumMips;
		}
		Ar.Logf( TEXT("%s.NumStreamedMips = %d"), UTexture::GetTextureGroupString(TextureGroup(LODGroup)), TexGroup.NumStreamedMips );
	}
	else if (LODGroupType == TEXT("StaticMesh"))
	{
		// TODO
		Ar.Logf(TEXT("NumStreamedMips command is not implemented for static mesh yet"));
	}
	else if (LODGroupType == TEXT("SkeletalMesh"))
	{
		// TODO
		Ar.Logf(TEXT("NumStreamedMips command is not implemented for skeletal mesh yet"));
	}
	else
	{
		Ar.Logf( TEXT("Usage: NumStreamedMips LODGroupIndex <N> [Texture|StaticMesh|SkeletalMesh]") );
	}
	return true;
}

bool FRenderAssetStreamingManager::HandleTrackRenderAssetCommand( const TCHAR* Cmd, FOutputDevice& Ar )
{
	FScopeLock ScopeLock(&CriticalSection);

	FString AssetName(FParse::Token(Cmd, 0));
	if ( TrackRenderAsset(AssetName) )
	{
		Ar.Logf(TEXT("Textures or meshes containing \"%s\" are now tracked."), *AssetName);
	}
	return true;
}

bool FRenderAssetStreamingManager::HandleListTrackedRenderAssetsCommand( const TCHAR* Cmd, FOutputDevice& Ar )
{
	FScopeLock ScopeLock(&CriticalSection);

	FString NumAssetString(FParse::Token(Cmd, 0));
	int32 NumAssets = (NumAssetString.Len() > 0) ? FCString::Atoi(*NumAssetString) : -1;
	ListTrackedRenderAssets(Ar, NumAssets);
	return true;
}

FORCEINLINE float SqrtKeepMax(float V)
{
	return V == FLT_MAX ? FLT_MAX : FMath::Sqrt(V);
}

bool FRenderAssetStreamingManager::HandleDebugTrackedRenderAssetsCommand( const TCHAR* Cmd, FOutputDevice& Ar )
{
	FScopeLock ScopeLock(&CriticalSection);

	// The ENABLE_RENDER_ASSET_TRACKING macro is defined in ContentStreaming.cpp and not available here. This code does not compile any more.
#ifdef ENABLE_TEXTURE_TRACKING_BROKEN
	int32 NumTrackedTextures = GTrackedTextureNames.Num();
	if ( NumTrackedTextures )
	{
		for (int32 StreamingIndex = 0; StreamingIndex < StreamingTextures.Num(); ++StreamingIndex)
		{
			FStreamingRenderAsset& StreamingTexture = StreamingTextures[StreamingIndex];
			if (StreamingTexture.Texture)
			{
				// See if it matches any of the texture names that we're tracking.
				FString TextureNameString = StreamingTexture.Texture->GetFullName();
				const TCHAR* TextureName = *TextureNameString;
				for ( int32 TrackedTextureIndex=0; TrackedTextureIndex < NumTrackedTextures; ++TrackedTextureIndex )
				{
					const FString& TrackedTextureName = GTrackedTextureNames[TrackedTextureIndex];
					if ( FCString::Stristr(TextureName, *TrackedTextureName) != NULL )
					{
						FTrackedTextureEvent* LastEvent = NULL;
						for ( int32 LastEventIndex=0; LastEventIndex < GTrackedTextures.Num(); ++LastEventIndex )
						{
							FTrackedTextureEvent* Event = &GTrackedTextures[LastEventIndex];
							if ( FCString::Strcmp(TextureName, Event->TextureName) == 0 )
							{
								LastEvent = Event;
								break;
							}
						}

						if (LastEvent)
						{
							Ar.Logf(
								TEXT("Texture: \"%s\", ResidentMips: %d/%d, RequestedMips: %d, WantedMips: %d, DynamicWantedMips: %d, StreamingStatus: %d, StreamType: %s, Boost: %.1f"),
								TextureName,
								LastEvent->NumResidentMips,
								StreamingTexture.Texture->GetNumMips(),
								LastEvent->NumRequestedMips,
								LastEvent->WantedMips,
								LastEvent->DynamicWantedMips.ComputeMip(&StreamingTexture, MipBias, false),
								LastEvent->StreamingStatus,
								GStreamTypeNames[StreamingTexture.GetStreamingType()],
								StreamingTexture.BoostFactor
								);
						}
						else
						{
							Ar.Logf(TEXT("Texture: \"%s\", StreamType: %s, Boost: %.1f"),
								TextureName,
								GStreamTypeNames[StreamingTexture.GetStreamingType()],
								StreamingTexture.BoostFactor
								);
						}
						for( int32 HandlerIndex=0; HandlerIndex<TextureStreamingHandlers.Num(); HandlerIndex++ )
						{
							FStreamingHandlerTextureBase* TextureStreamingHandler = TextureStreamingHandlers[HandlerIndex];
							float MaxSize = 0;
							float MaxSize_VisibleOnly = 0;
							FFloatMipLevel HandlerWantedMips = TextureStreamingHandler->GetWantedSize(*this, StreamingTexture, HandlerDistance);
							Ar.Logf(
								TEXT("    Handler %s: WantedMips: %d, PerfectWantedMips: %d, Distance: %f"),
								TextureStreamingHandler->HandlerName,
								HandlerWantedMips.ComputeMip(&StreamingTexture, MipBias, false),
								HandlerWantedMips.ComputeMip(&StreamingTexture, MipBias, true),
								HandlerDistance
								);
						}

						for ( int32 LevelIndex=0; LevelIndex < ThreadSettings.LevelData.Num(); ++LevelIndex )
						{
							FRenderAssetStreamingManager::FThreadLevelData& LevelData = ThreadSettings.LevelData[ LevelIndex ].Value;
							TArray<FStreamableTextureInstance4>* TextureInstances = LevelData.ThreadTextureInstances.Find( StreamingTexture.Texture );
							if ( TextureInstances )
							{
								for ( int32 InstanceIndex=0; InstanceIndex < TextureInstances->Num(); ++InstanceIndex )
								{
									const FStreamableTextureInstance4& TextureInstance = (*TextureInstances)[InstanceIndex];
									for (int32 i = 0; i < 4; i++)
									{
										Ar.Logf(
											TEXT("    Instance: %f,%f,%f Radius: %f Range: [%f, %f] TexelFactor: %f"),
											TextureInstance.BoundsOriginX[i],
											TextureInstance.BoundsOriginY[i],
											TextureInstance.BoundsOriginZ[i],
											TextureInstance.BoundingSphereRadius[i],
											FMath::Sqrt(TextureInstance.MinDistanceSq[i]),
											SqrtKeepMax(TextureInstance.MaxDistanceSq[i]),
											TextureInstance.TexelFactor[i]
										);
									}
								}
							}
						}
					}
				}
			}
		}
	}
#endif // ENABLE_RENDER_ASSET_TRACKING

	return true;
}

bool FRenderAssetStreamingManager::HandleUntrackRenderAssetCommand( const TCHAR* Cmd, FOutputDevice& Ar )
{
	FScopeLock ScopeLock(&CriticalSection);

	FString AssetName(FParse::Token(Cmd, 0));
	if (UntrackRenderAsset(AssetName))
	{
		Ar.Logf(TEXT("Textures or meshes containing \"%s\" are no longer tracked."), *AssetName);
	}
	return true;
}

bool FRenderAssetStreamingManager::HandleStreamOutCommand( const TCHAR* Cmd, FOutputDevice& Ar )
{
	FScopeLock ScopeLock(&CriticalSection);

	FString Parameter(FParse::Token(Cmd, 0));
	int64 FreeMB = (Parameter.Len() > 0) ? FCString::Atoi(*Parameter) : 0;
	if ( FreeMB > 0 )
	{
		bool bSucceeded = StreamOutRenderAssetData( FreeMB * 1024 * 1024 );
		Ar.Logf( TEXT("Tried to stream out %llu MB of texture/mesh data: %s"), FreeMB, bSucceeded ? TEXT("Succeeded") : TEXT("Failed") );
	}
	else
	{
		Ar.Logf( TEXT("Usage: StreamOut <N> (in MB)") );
	}
	return true;
}

bool FRenderAssetStreamingManager::HandlePauseRenderAssetStreamingCommand( const TCHAR* Cmd, FOutputDevice& Ar )
{
	FScopeLock ScopeLock(&CriticalSection);

	bPauseRenderAssetStreaming = !bPauseRenderAssetStreaming;
	Ar.Logf( TEXT("Render asset streaming is now \"%s\"."), bPauseRenderAssetStreaming ? TEXT("PAUSED") : TEXT("UNPAUSED") );
	return true;
}

bool FRenderAssetStreamingManager::HandleStreamingManagerMemoryCommand( const TCHAR* Cmd, FOutputDevice& Ar, UWorld* InWorld )
{
	FScopeLock ScopeLock(&CriticalSection);

	SyncStates(true);

	uint32 MemSize = sizeof(FRenderAssetStreamingManager);
	MemSize += StreamingRenderAssets.GetAllocatedSize();
	MemSize += DynamicComponentManager.GetAllocatedSize();
	MemSize += PendingStreamingRenderAssets.GetAllocatedSize()
		+ PendingStreamingRenderAssetTypes.GetAllocatedSize()
		+ RemovedRenderAssetIndices.GetAllocatedSize();
	MemSize += LevelRenderAssetManagers.GetAllocatedSize();
	MemSize += AsyncWork->GetTask().StreamingData.GetAllocatedSize();

	for (const FLevelRenderAssetManager* LevelManager : LevelRenderAssetManagers)
	{
		if (LevelManager!=nullptr)
		{
			MemSize += LevelManager->GetAllocatedSize();
		}
	}

	Ar.Logf(TEXT("StreamingManagerTexture: %.2f KB used"), MemSize / 1024.0f);

	return true;
}

bool FRenderAssetStreamingManager::HandleLODGroupsCommand( const TCHAR* Cmd, FOutputDevice& Ar )
{
	bDetailedDumpTextureGroupStats = FParse::Param(Cmd, TEXT("Detailed"));
	bTriggerDumpTextureGroupStats = true;
	// TODO: mesh LOD groups
	return true;
}

bool FRenderAssetStreamingManager::HandleInvestigateRenderAssetCommand(const TCHAR* Cmd, FOutputDevice& Ar, UWorld* InWorld)
{
	FScopeLock ScopeLock(&CriticalSection);

	SyncStates(true);

	FString InvestigateAssetName(FParse::Token(Cmd, 0));
	if (InvestigateAssetName.Len())
	{
		FAsyncRenderAssetStreamingData& StreamingData = AsyncWork->GetTask().StreamingData;
		StreamingData.Init(CurrentViewInfos, LastWorldUpdateTime, LevelRenderAssetManagers, DynamicComponentManager);
		StreamingData.ComputeViewInfoExtras(Settings);
		StreamingData.UpdateBoundSizes_Async(Settings);

		for (int32 AssetIndex = 0; AssetIndex < StreamingRenderAssets.Num(); ++AssetIndex)
		{
			FStreamingRenderAsset& StreamingRenderAsset = StreamingRenderAssets[AssetIndex];
			FString AssetName = StreamingRenderAsset.RenderAsset->GetFullName();
			if (AssetName.Contains(InvestigateAssetName))
			{
				UStreamableRenderAsset* RenderAsset = StreamingRenderAsset.RenderAsset;
				if (!RenderAsset) continue;
				const typename FStreamingRenderAsset::EAssetType AssetType = StreamingRenderAsset.RenderAssetType;
				UTexture2D* Texture2D = Cast<UTexture2D>(RenderAsset);
				UStaticMesh* StaticMesh = Cast<UStaticMesh>(RenderAsset);
				int32 CurrentMipIndex = FMath::Max(RenderAsset->GetNumMipsForStreaming() - StreamingRenderAsset.ResidentMips, 0);
				int32 WantedMipIndex = FMath::Max(RenderAsset->GetNumMipsForStreaming() - StreamingRenderAsset.GetPerfectWantedMips(), 0);
				int32 MaxMipIndex = FMath::Max(RenderAsset->GetNumMipsForStreaming() - StreamingRenderAsset.MaxAllowedMips, 0);

				UE_LOG(LogContentStreaming, Log, TEXT("%s: %s"), FStreamingRenderAsset::GetStreamingAssetTypeStr(AssetType), *AssetName);
				FString LODGroupName = Texture2D ? UTexture::GetTextureGroupString((TextureGroup)StreamingRenderAsset.LODGroup) : TEXT("Unknown");
#if WITH_EDITORONLY_DATA
				if (StaticMesh)
				{
					LODGroupName = StaticMesh->LODGroup.ToString();
				}
#endif
				UE_LOG(LogContentStreaming, Log, TEXT("  LOD group:   %s"), *LODGroupName);

				if (RenderAsset->bGlobalForceMipLevelsToBeResident)
				{
					UE_LOG(LogContentStreaming, Log, TEXT("  Force all mips:  bGlobalForceMipLevelsToBeResident"));
				}
				else if (RenderAsset->bForceMiplevelsToBeResident)
				{
					UE_LOG(LogContentStreaming, Log, TEXT("  Force all mips:  bForceMiplevelsToBeResident"));
				}
				else if (RenderAsset->ShouldMipLevelsBeForcedResident())
				{
					float TimeLeft = (float)(RenderAsset->ForceMipLevelsToBeResidentTimestamp - FApp::GetCurrentTime());
					UE_LOG(LogContentStreaming, Log, TEXT("  Force all mips:  %.1f seconds left"), FMath::Max(TimeLeft, 0.0f));
				}
				else if (StreamingRenderAsset.bForceFullyLoadHeuristic)
				{
					UE_LOG(LogContentStreaming, Log, TEXT("  Force all mips: bForceFullyLoad"));
				}
				else if (StreamingRenderAsset.MipCount == 1)
				{
					UE_LOG(LogContentStreaming, Log, TEXT("  Force all mips:  No mip-maps"));
				}
				
				if (Texture2D)
				{
					UE_LOG(LogContentStreaming, Log, TEXT("  Current size [Mips]: %dx%d [%d]"), Texture2D->PlatformData->Mips[CurrentMipIndex].SizeX, Texture2D->PlatformData->Mips[CurrentMipIndex].SizeY, StreamingRenderAsset.ResidentMips);
					UE_LOG(LogContentStreaming, Log, TEXT("  Wanted size [Mips]:  %dx%d [%d]"), Texture2D->PlatformData->Mips[WantedMipIndex].SizeX, Texture2D->PlatformData->Mips[WantedMipIndex].SizeY, StreamingRenderAsset.GetPerfectWantedMips());
				}
				else
				{
					UE_LOG(LogContentStreaming, Log, TEXT("  Current LOD index: %d"), CurrentMipIndex);
					UE_LOG(LogContentStreaming, Log, TEXT("  Wanted LOD index: %d"), WantedMipIndex);
				}
				UE_LOG(LogContentStreaming, Log, TEXT("  Allowed mips:        %d-%d"), StreamingRenderAsset.MinAllowedMips, StreamingRenderAsset.MaxAllowedMips);
				UE_LOG(LogContentStreaming, Log, TEXT("  LoadOrder Priority:  %d"), StreamingRenderAsset.LoadOrderPriority);
				UE_LOG(LogContentStreaming, Log, TEXT("  Retention Priority:  %d"), StreamingRenderAsset.RetentionPriority);
				UE_LOG(LogContentStreaming, Log, TEXT("  Boost factor:        %.1f"), StreamingRenderAsset.BoostFactor);
				UE_LOG(LogContentStreaming, Log, TEXT("  Mip bias [Budget]:   %d [%d]"), StreamingRenderAsset.MipCount - StreamingRenderAsset.MaxAllowedMips, StreamingRenderAsset.BudgetMipBias + (Settings.bUsePerTextureBias ? 0 : Settings.GlobalMipBias));

				if (InWorld && !GIsEditor)
				{
					UE_LOG(LogContentStreaming, Log, TEXT("  Time: World=%.3f LastUpdate=%.3f "), InWorld->GetTimeSeconds(), LastWorldUpdateTime);
				}

				for (int32 ViewIndex = 0; ViewIndex < StreamingData.GetViewInfos().Num(); ViewIndex++)
				{
					// Calculate distance of viewer to bounding sphere.
					const FStreamingViewInfo& ViewInfo = StreamingData.GetViewInfos()[ViewIndex];
					UE_LOG(LogContentStreaming, Log, TEXT("  View%d: Position=(%s) ScreenSize=%f MaxEffectiveScreenSize=%f Boost=%f"), ViewIndex, *ViewInfo.ViewOrigin.ToString(), ViewInfo.ScreenSize, Settings.MaxEffectiveScreenSize, ViewInfo.BoostFactor);
				}

				StreamingData.UpdatePerfectWantedMips_Async(StreamingRenderAsset, Settings, true);
			}
		}
	}
	else
	{
		Ar.Logf(TEXT("Usage: InvestigateTexture <name>"));
	}
	return true;
}
#endif // !UE_BUILD_SHIPPING
/**
 * Allows the streaming manager to process exec commands.
 * @param InWorld	world contexxt
 * @param Cmd		Exec command
 * @param Ar		Output device for feedback
 * @return			true if the command was handled
 */
bool FRenderAssetStreamingManager::Exec( UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar )
{
#if STATS_FAST
	if (FParse::Command(&Cmd,TEXT("DumpTextureStreamingStats"))
		|| FParse::Command(&Cmd, TEXT("DumpRenderAssetStreamingStats")))
	{
		return HandleDumpTextureStreamingStatsCommand( Cmd, Ar );
	}
#endif
#if !UE_BUILD_SHIPPING
	if (FParse::Command(&Cmd,TEXT("ListStreamingTextures"))
		|| FParse::Command(&Cmd, TEXT("ListStreamingRenderAssets")))
	{
		return HandleListStreamingRenderAssetsCommand( Cmd, Ar );
	}
	if (FParse::Command(&Cmd, TEXT("ResetMaxEverRequiredTextures"))
		|| FParse::Command(&Cmd, TEXT("ResetMaxEverRequiredRenderAssetMemory")))
	{
		return HandleResetMaxEverRequiredRenderAssetMemoryCommand(Cmd, Ar);
	}
	if (FParse::Command(&Cmd,TEXT("LightmapStreamingFactor")))
	{
		return HandleLightmapStreamingFactorCommand( Cmd, Ar );
	}
	else if (FParse::Command(&Cmd,TEXT("CancelTextureStreaming"))
		|| FParse::Command(&Cmd, TEXT("CancelRenderAssetStreaming")))
	{
		return HandleCancelRenderAssetStreamingCommand( Cmd, Ar );
	}
	else if (FParse::Command(&Cmd,TEXT("ShadowmapStreamingFactor")))
	{
		return HandleShadowmapStreamingFactorCommand( Cmd, Ar );
	}
	else if (FParse::Command(&Cmd,TEXT("NumStreamedMips")))
	{
		return HandleNumStreamedMipsCommand( Cmd, Ar );
	}
	else if (FParse::Command(&Cmd,TEXT("TrackTexture"))
		|| FParse::Command(&Cmd, TEXT("TrackRenderAsset")))
	{
		return HandleTrackRenderAssetCommand( Cmd, Ar );
	}
	else if (FParse::Command(&Cmd,TEXT("ListTrackedTextures"))
		|| FParse::Command(&Cmd, TEXT("ListTrackedRenderAssets")))
	{
		return HandleListTrackedRenderAssetsCommand( Cmd, Ar );
	}
	else if (FParse::Command(&Cmd,TEXT("DebugTrackedTextures"))
		|| FParse::Command(&Cmd, TEXT("DebugTrackedRenderAssets")))
	{
		return HandleDebugTrackedRenderAssetsCommand( Cmd, Ar );
	}
	else if (FParse::Command(&Cmd,TEXT("UntrackTexture"))
		|| FParse::Command(&Cmd, TEXT("UntrackRenderAsset")))
	{
		return HandleUntrackRenderAssetCommand( Cmd, Ar );
	}
	else if (FParse::Command(&Cmd,TEXT("StreamOut")))
	{
		return HandleStreamOutCommand( Cmd, Ar );
	}
	else if (FParse::Command(&Cmd,TEXT("PauseTextureStreaming"))
		|| FParse::Command(&Cmd, TEXT("PauseRenderAssetStreaming")))
	{
		return HandlePauseRenderAssetStreamingCommand( Cmd, Ar );
	}
	else if (FParse::Command(&Cmd,TEXT("StreamingManagerMemory")))
	{
		return HandleStreamingManagerMemoryCommand( Cmd, Ar, InWorld );
	}
	else if (FParse::Command(&Cmd,TEXT("TextureGroups"))
		|| FParse::Command(&Cmd, TEXT("LODGroups")))
	{
		return HandleLODGroupsCommand( Cmd, Ar );
	}
	else if (FParse::Command(&Cmd,TEXT("InvestigateTexture"))
		|| FParse::Command(&Cmd, TEXT("InvestigateRenderAsset")))
	{
		return HandleInvestigateRenderAssetCommand( Cmd, Ar, InWorld );
	}
	else if (FParse::Command(&Cmd,TEXT("ListMaterialsWithMissingTextureStreamingData")))
	{
		Ar.Logf(TEXT("Listing all materials with not texture streaming data."));
		Ar.Logf(TEXT("Run \"BuildMaterialTextureStreamingData\" in the editor to fix the issue"));
		Ar.Logf(TEXT("Note that some materials might have no that even after rebuild."));
		for( TObjectIterator<UMaterialInterface> It; It; ++It )
		{
			UMaterialInterface* Material = *It;
			if (Material && Material->GetOutermost() != GetTransientPackage() && Material->HasAnyFlags(RF_Public) && Material->UseAnyStreamingTexture() && !Material->HasTextureStreamingData()) 
			{
				FString TextureName = Material->GetFullName();
				Ar.Logf(TEXT("%s"), *TextureName);
			}
		}
		return true;
	}
#endif // !UE_BUILD_SHIPPING

	return false;
}

void FRenderAssetStreamingManager::DumpTextureGroupStats( bool bDetailedStats )
{
	FScopeLock ScopeLock(&CriticalSection);

	bTriggerDumpTextureGroupStats = false;
#if !UE_BUILD_SHIPPING
	struct FTextureGroupStats
	{
		FTextureGroupStats()
		{
			FMemory::Memzero( this, sizeof(FTextureGroupStats) );
		}
		int32 NumTextures;
		int32 NumNonStreamingTextures;
		int64 CurrentTextureSize;
		int64 WantedTextureSize;
		int64 MaxTextureSize;
		int64 NonStreamingSize;
	};
	FTextureGroupStats TextureGroupStats[TEXTUREGROUP_MAX];
	FTextureGroupStats TextureGroupWaste[TEXTUREGROUP_MAX];
	int64 NumNonStreamingTextures = 0;
	int64 NonStreamingSize = 0;
	int32 NumNonStreamingPoolTextures = 0;
	int64 NonStreamingPoolSize = 0;
	int64 TotalSavings = 0;
//	int32 UITexels = 0;
	int32 NumDXT[PF_MAX];
	int32 NumNonSaved[PF_MAX];
	int32 NumOneMip[PF_MAX];
	int32 NumBadAspect[PF_MAX];
	int32 NumTooSmall[PF_MAX];
	int32 NumNonPow2[PF_MAX];
	int32 NumNULLResource[PF_MAX];
	FMemory::Memzero( &NumDXT, sizeof(NumDXT) );
	FMemory::Memzero( &NumNonSaved, sizeof(NumNonSaved) );
	FMemory::Memzero( &NumOneMip, sizeof(NumOneMip) );
	FMemory::Memzero( &NumBadAspect, sizeof(NumBadAspect) );
	FMemory::Memzero( &NumTooSmall, sizeof(NumTooSmall) );
	FMemory::Memzero( &NumNonPow2, sizeof(NumNonPow2) );
	FMemory::Memzero( &NumNULLResource, sizeof(NumNULLResource) );

	// Gather stats.
	for( TObjectIterator<UTexture> It; It; ++It )
	{
		UTexture* Texture = *It;
		UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
		FTextureGroupStats& Stat = TextureGroupStats[Texture->LODGroup];
		FTextureGroupStats& Waste = TextureGroupWaste[Texture->LODGroup];
		FStreamingRenderAsset* StreamingTexture = GetStreamingRenderAsset(Texture2D);
		uint32 TextureAlign = 0;
		if ( StreamingTexture )
		{
			Stat.NumTextures++;
			Stat.CurrentTextureSize += StreamingTexture->GetSize( StreamingTexture->ResidentMips );
			Stat.WantedTextureSize += StreamingTexture->GetSize( StreamingTexture->WantedMips );
			Stat.MaxTextureSize += StreamingTexture->GetSize( StreamingTexture->MaxAllowedMips );
			
			int64 WasteCurrent = StreamingTexture->GetSize( StreamingTexture->ResidentMips ) - RHICalcTexture2DPlatformSize(Texture2D->GetSizeX(), Texture2D->GetSizeY(), Texture2D->GetPixelFormat(), StreamingTexture->ResidentMips, 1, 0, TextureAlign);			

			int64 WasteWanted = StreamingTexture->GetSize( StreamingTexture->WantedMips ) - RHICalcTexture2DPlatformSize(Texture2D->GetSizeX(), Texture2D->GetSizeY(), Texture2D->GetPixelFormat(), StreamingTexture->WantedMips, 1, 0, TextureAlign);			

			int64 WasteMaxSize = StreamingTexture->GetSize( StreamingTexture->MaxAllowedMips ) - RHICalcTexture2DPlatformSize(Texture2D->GetSizeX(), Texture2D->GetSizeY(), Texture2D->GetPixelFormat(), StreamingTexture->MaxAllowedMips, 1, 0, TextureAlign);			

			Waste.NumTextures++;
			Waste.CurrentTextureSize += FMath::Max<int64>(WasteCurrent,0);
			Waste.WantedTextureSize += FMath::Max<int64>(WasteWanted,0);
			Waste.MaxTextureSize += FMath::Max<int64>(WasteMaxSize,0);
		}
		else
		{

			bool bIsPooledTexture = Texture->Resource && IsValidRef(Texture->Resource->TextureRHI) && appIsPoolTexture( Texture->Resource->TextureRHI );
			int64 TextureSize = Texture->CalcTextureMemorySizeEnum(TMC_ResidentMips);
			Stat.NumNonStreamingTextures++;
			Stat.NonStreamingSize += TextureSize;
			if ( Texture2D && Texture2D->Resource )
			{				
				int64 WastedSize = TextureSize - RHICalcTexture2DPlatformSize(Texture2D->GetSizeX(), Texture2D->GetSizeY(), Texture2D->GetPixelFormat(), Texture2D->GetNumMips(), 1, 0, TextureAlign);				

				Waste.NumNonStreamingTextures++;
				Waste.NonStreamingSize += FMath::Max<int64>(WastedSize, 0);
			}
			if ( bIsPooledTexture )
			{
				NumNonStreamingPoolTextures++;
				NonStreamingPoolSize += TextureSize;
			}
			else
			{
				NumNonStreamingTextures++;
				NonStreamingSize += TextureSize;
			}
		}

		if ( Texture2D && (Texture2D->GetPixelFormat() == PF_DXT1 || Texture2D->GetPixelFormat() == PF_DXT5) )
		{
			NumDXT[Texture2D->GetPixelFormat()]++;
			if ( Texture2D->Resource )
			{
				// Track the reasons we couldn't save any memory from the mip-tail.
				NumNonSaved[Texture2D->GetPixelFormat()]++;
				if ( Texture2D->GetNumMips() < 2 )
				{
					NumOneMip[Texture2D->GetPixelFormat()]++;
				}
				else if ( Texture2D->GetSizeX() > Texture2D->GetSizeY() * 2 || Texture2D->GetSizeY() > Texture2D->GetSizeX() * 2 )
				{
					NumBadAspect[Texture2D->GetPixelFormat()]++;
				}
				else if ( Texture2D->GetSizeX() < 16 || Texture2D->GetSizeY() < 16 || Texture2D->GetNumMips() < 5 )
				{
					NumTooSmall[Texture2D->GetPixelFormat()]++;
				}
				else if ( (Texture2D->GetSizeX() & (Texture2D->GetSizeX() - 1)) != 0 || (Texture2D->GetSizeY() & (Texture2D->GetSizeY() - 1)) != 0 )
				{
					NumNonPow2[Texture2D->GetPixelFormat()]++;
				}
				else
				{
					// Unknown reason
					int32 Q=0;
				}
			}
			else
			{
				NumNULLResource[Texture2D->GetPixelFormat()]++;
			}
		}
	}

	// Output stats.
	{
		UE_LOG(LogContentStreaming, Log, TEXT("Texture memory usage:"));
		FTextureGroupStats TotalStats;
		for ( int32 GroupIndex=0; GroupIndex < TEXTUREGROUP_MAX; ++GroupIndex )
		{
			FTextureGroupStats& Stat = TextureGroupStats[GroupIndex];
			TotalStats.NumTextures				+= Stat.NumTextures;
			TotalStats.NumNonStreamingTextures	+= Stat.NumNonStreamingTextures;
			TotalStats.CurrentTextureSize		+= Stat.CurrentTextureSize;
			TotalStats.WantedTextureSize		+= Stat.WantedTextureSize;
			TotalStats.MaxTextureSize			+= Stat.MaxTextureSize;
			TotalStats.NonStreamingSize			+= Stat.NonStreamingSize;
			UE_LOG(LogContentStreaming, Log, TEXT("%34s: NumTextures=%4d, Current=%8.1f KB, Wanted=%8.1f KB, OnDisk=%8.1f KB, NumNonStreaming=%4d, NonStreaming=%8.1f KB"),
				UTexture::GetTextureGroupString((TextureGroup)GroupIndex),
				Stat.NumTextures,
				Stat.CurrentTextureSize / 1024.0f,
				Stat.WantedTextureSize / 1024.0f,
				Stat.MaxTextureSize / 1024.0f,
				Stat.NumNonStreamingTextures,
				Stat.NonStreamingSize / 1024.0f );
		}
		UE_LOG(LogContentStreaming, Log, TEXT("%34s: NumTextures=%4d, Current=%8.1f KB, Wanted=%8.1f KB, OnDisk=%8.1f KB, NumNonStreaming=%4d, NonStreaming=%8.1f KB"),
			TEXT("Total"),
			TotalStats.NumTextures,
			TotalStats.CurrentTextureSize / 1024.0f,
			TotalStats.WantedTextureSize / 1024.0f,
			TotalStats.MaxTextureSize / 1024.0f,
			TotalStats.NumNonStreamingTextures,
			TotalStats.NonStreamingSize / 1024.0f );
	}
	if ( bDetailedStats )
	{
		UE_LOG(LogContentStreaming, Log, TEXT("Wasted memory due to inefficient texture storage:"));
		FTextureGroupStats TotalStats;
		for ( int32 GroupIndex=0; GroupIndex < TEXTUREGROUP_MAX; ++GroupIndex )
		{
			FTextureGroupStats& Stat = TextureGroupWaste[GroupIndex];
			TotalStats.NumTextures				+= Stat.NumTextures;
			TotalStats.NumNonStreamingTextures	+= Stat.NumNonStreamingTextures;
			TotalStats.CurrentTextureSize		+= Stat.CurrentTextureSize;
			TotalStats.WantedTextureSize		+= Stat.WantedTextureSize;
			TotalStats.MaxTextureSize			+= Stat.MaxTextureSize;
			TotalStats.NonStreamingSize			+= Stat.NonStreamingSize;
			UE_LOG(LogContentStreaming, Log, TEXT("%34s: NumTextures=%4d, Current=%8.1f KB, Wanted=%8.1f KB, OnDisk=%8.1f KB, NumNonStreaming=%4d, NonStreaming=%8.1f KB"),
				UTexture::GetTextureGroupString((TextureGroup)GroupIndex),
				Stat.NumTextures,
				Stat.CurrentTextureSize / 1024.0f,
				Stat.WantedTextureSize / 1024.0f,
				Stat.MaxTextureSize / 1024.0f,
				Stat.NumNonStreamingTextures,
				Stat.NonStreamingSize / 1024.0f );
		}
		UE_LOG(LogContentStreaming, Log, TEXT("%34s: NumTextures=%4d, Current=%8.1f KB, Wanted=%8.1f KB, OnDisk=%8.1f KB, NumNonStreaming=%4d, NonStreaming=%8.1f KB"),
			TEXT("Total Wasted"),
			TotalStats.NumTextures,
			TotalStats.CurrentTextureSize / 1024.0f,
			TotalStats.WantedTextureSize / 1024.0f,
			TotalStats.MaxTextureSize / 1024.0f,
			TotalStats.NumNonStreamingTextures,
			TotalStats.NonStreamingSize / 1024.0f );
	}

	//@TODO: Calculate memory usage for non-pool textures properly!
//	UE_LOG(LogContentStreaming, Log,  TEXT("%34s: NumTextures=%4d, Current=%7.1f KB"), TEXT("Non-streaming pool textures"), NumNonStreamingPoolTextures, NonStreamingPoolSize/1024.0f );
//	UE_LOG(LogContentStreaming, Log,  TEXT("%34s: NumTextures=%4d, Current=%7.1f KB"), TEXT("Non-streaming non-pool textures"), NumNonStreamingTextures, NonStreamingSize/1024.0f );
#endif // !UE_BUILD_SHIPPING
}
