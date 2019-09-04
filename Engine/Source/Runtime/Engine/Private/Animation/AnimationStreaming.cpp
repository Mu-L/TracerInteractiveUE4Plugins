// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
AnimationStreaming.cpp: Manager to handle streaming animation data
=============================================================================*/

#include "Animation/AnimationStreaming.h"
#include "Misc/CoreStats.h"
#include "Animation/AnimStreamable.h"
#include "Algo/Find.h"

static int32 SpoofFailedAnimationChunkLoad = 0;
FAutoConsoleVariableRef CVarSpoofFailedAnimationChunkLoad(
	TEXT("a.Streaming.SpoofFailedChunkLoad"),
	SpoofFailedAnimationChunkLoad,
	TEXT("Forces failing to load streamed animation chunks.\n")
	TEXT("0: Not Enabled, 1: Enabled"),
	ECVF_Default);



void FLoadedAnimationChunk::CleanUpIORequest()
{
	if (IORequest)
	{
		IORequest->WaitCompletion();
		delete IORequest;
		IORequest = nullptr;
	}
}

////////////////////////
// FStreamingAnimationData //
////////////////////////

FStreamingAnimationData::FStreamingAnimationData()
	: StreamableAnim(NULL)
	, IORequestHandle(nullptr)
	, AnimationStreamingManager(nullptr)
{
	ResetRequestedChunks();
}

FStreamingAnimationData::~FStreamingAnimationData()
{
	check(IORequestHandle == nullptr);
}

void FStreamingAnimationData::FreeResources()
{
	// Make sure there are no pending requests in flight.
	for (int32 Pass = 0; Pass < 3; Pass++)
	{
		BlockTillAllRequestsFinished();
		if (!UpdateStreamingStatus())
		{
			break;
		}
		check(Pass < 2); // we should be done after two passes. Pass 0 will start anything we need and pass 1 will complete those requests
	}

	for (FLoadedAnimationChunk& LoadedChunk : LoadedChunks)
	{
		FreeLoadedChunk(LoadedChunk);
	}

	if (IORequestHandle)
	{
		delete IORequestHandle;
		IORequestHandle = nullptr;
	}
}

bool FStreamingAnimationData::Initialize(UAnimStreamable* InStreamableAnim, FAnimationStreamingManager* InAnimationStreamingManager)
{
	check(!IORequestHandle);

	check(InStreamableAnim && InStreamableAnim->HasRunningPlatformData());

	FStreamableAnimPlatformData& RunningAnimPlatformData = InStreamableAnim->GetRunningPlatformData();

	if (!RunningAnimPlatformData.Chunks.Num())
	{
		UE_LOG(LogAnimation, Error, TEXT("Failed to initialize streaming animation due to lack of anim or serialized stream chunks. '%s'"), *InStreamableAnim->GetFullName());
		return false;
	}

	StreamableAnim = InStreamableAnim;
	AnimationStreamingManager = InAnimationStreamingManager;

	// Always get the first chunk of data so we can play immediately
	check(LoadedChunks.Num() == 0);
	check(LoadedChunkIndices.Num() == 0);

	AddNewLoadedChunk(0, RunningAnimPlatformData.Chunks[0].CompressedAnimSequence);
	LoadedChunkIndices.Add(0);

	return true;
}

bool FStreamingAnimationData::UpdateStreamingStatus()
{
	if (!StreamableAnim)
	{
		return false;
	}

	bool bHasPendingRequestInFlight = false;

	TArray<uint32> IndicesToLoad;
	TArray<uint32> IndicesToFree;

	if (HasPendingRequests(IndicesToLoad, IndicesToFree))
	{
		// could maybe iterate over the things we know are done, but I couldn't tell if that was IndicesToLoad or not.
		for (FLoadedAnimationChunk& LoadedChunk : LoadedChunks)
		{
			if (LoadedChunk.IORequest)
			{
				const bool bRequestFinished = LoadedChunk.IORequest->PollCompletion();
				bHasPendingRequestInFlight |= !bRequestFinished;
				if (bRequestFinished)
				{
					LoadedChunk.CleanUpIORequest();
				}
			}
		}

		LoadedChunkIndices = RequestedChunks;

		BeginPendingRequests(IndicesToLoad, IndicesToFree);
	}

	ResetRequestedChunks();

	return bHasPendingRequestInFlight;
}

bool FStreamingAnimationData::HasPendingRequests(TArray<uint32>& IndicesToLoad, TArray<uint32>& IndicesToFree) const
{
	IndicesToLoad.Reset();
	IndicesToFree.Reset();

	// Find indices that aren't loaded
	for (auto NeededIndex : RequestedChunks)
	{
		if (!LoadedChunkIndices.Contains(NeededIndex))
		{
			IndicesToLoad.AddUnique(NeededIndex);
		}
	}

	// Find indices that aren't needed anymore
	for (auto CurrentIndex : LoadedChunkIndices)
	{
		if (!RequestedChunks.Contains(CurrentIndex))
		{
			IndicesToFree.AddUnique(CurrentIndex);
		}
	}

	return IndicesToLoad.Num() > 0 || IndicesToFree.Num() > 0;
}

void FStreamingAnimationData::BeginPendingRequests(const TArray<uint32>& IndicesToLoad, const TArray<uint32>& IndicesToFree)
{
	TArray<uint32> FreeChunkIndices;

	// Mark Chunks for removal in case they can be reused
	{
		for (auto Index : IndicesToFree)
		{
			for (int32 ChunkIndex = 0; ChunkIndex < LoadedChunks.Num(); ++ChunkIndex)
			{
				check(Index != 0);
				if (LoadedChunks[ChunkIndex].Index == Index)
				{
					FreeLoadedChunk(LoadedChunks[ChunkIndex]);
					LoadedChunks.RemoveAtSwap(ChunkIndex,1,false);
					break;
				}
			}
		}
	}

	// Set off all IO Requests

	const EAsyncIOPriorityAndFlags AsyncIOPriority = AIOP_High;

	for (auto ChunkIndex : IndicesToLoad)
	{
		const FAnimStreamableChunk& Chunk = StreamableAnim->GetRunningPlatformData().Chunks[ChunkIndex];

		FCompressedAnimSequence* ExistingCompressedData = Chunk.CompressedAnimSequence;

		FLoadedAnimationChunk& ChunkStorage = AddNewLoadedChunk(ChunkIndex, ExistingCompressedData);

		if(!ExistingCompressedData)
		{
			check(!ChunkStorage.CompressedAnimData);
			check(Chunk.BulkData.GetFilename().Len());
			UE_CLOG(Chunk.BulkData.IsStoredCompressedOnDisk(), LogAnimation, Fatal, TEXT("Package level compression is not supported for streaming animation."));
			check(!ChunkStorage.IORequest);
			
			if (!IORequestHandle)
			{
				IORequestHandle = FPlatformFileManager::Get().GetPlatformFile().OpenAsyncRead(*Chunk.BulkData.GetFilename());
				check(IORequestHandle); // this generally cannot fail because it is async
			}

			int64 ChunkSize = Chunk.BulkData.GetBulkDataSize();
			ChunkStorage.RequestStart = FPlatformTime::Seconds();
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Request Started %.2f\n"), ChunkStorage.RequestStart);
			FAsyncFileCallBack AsyncFileCallBack =
				[this, ChunkIndex, ChunkSize](bool bWasCancelled, IAsyncReadRequest* Req)
			{
				AnimationStreamingManager->OnAsyncFileCallback(this, ChunkIndex, ChunkSize, Req);
			};

			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Loading Stream Chunk %s Chunk:%i Length: %.3f Offset:%i Size:%i File:%i\n"), *StreamableAnim->GetName(), ChunkIndex, Chunk.SequenceLength, Chunk.BulkData.GetBulkDataOffsetInFile(), Chunk.BulkData.GetBulkDataSize(), *Chunk.BulkData.GetFilename());
			ChunkStorage.IORequest = IORequestHandle->ReadRequest(Chunk.BulkData.GetBulkDataOffsetInFile(), Chunk.BulkData.GetBulkDataSize(), AsyncIOPriority, &AsyncFileCallBack);
			if (!ChunkStorage.IORequest)
			{
				UE_LOG(LogAnimation, Error, TEXT("Animation streaming read request failed."));
			}
		}
	}
}

bool FStreamingAnimationData::BlockTillAllRequestsFinished(float TimeLimit)
{
	QUICK_SCOPE_CYCLE_COUNTER(FStreamingAnimData_BlockTillAllRequestsFinished);
	if (TimeLimit == 0.0f)
	{
		for (FLoadedAnimationChunk& LoadedChunk : LoadedChunks)
		{
			LoadedChunk.CleanUpIORequest();
		}
	}
	else
	{
		double EndTime = FPlatformTime::Seconds() + TimeLimit;
		for (FLoadedAnimationChunk& LoadedChunk : LoadedChunks)
		{
			if (LoadedChunk.IORequest)
			{
				float ThisTimeLimit = EndTime - FPlatformTime::Seconds();
				if (ThisTimeLimit < .001f || // one ms is the granularity of the platform event system
					!LoadedChunk.IORequest->WaitCompletion(ThisTimeLimit))
				{
					return false;
				}

				LoadedChunk.CleanUpIORequest();
			}
		}
	}
	return true;
}

FLoadedAnimationChunk& FStreamingAnimationData::AddNewLoadedChunk(uint32 ChunkIndex, FCompressedAnimSequence* ExistingData)
{
	int32 NewIndex = LoadedChunks.Num();
	LoadedChunks.AddDefaulted();

	LoadedChunks[NewIndex].CompressedAnimData = ExistingData;
	LoadedChunks[NewIndex].bOwnsCompressedData = false;
	LoadedChunks[NewIndex].Index = ChunkIndex;
	return LoadedChunks[NewIndex];
}

void FStreamingAnimationData::FreeLoadedChunk(FLoadedAnimationChunk& LoadedChunk)
{
	if (LoadedChunk.IORequest)
	{
		LoadedChunk.IORequest->Cancel();
		LoadedChunk.IORequest->WaitCompletion();
		delete LoadedChunk.IORequest;
		LoadedChunk.IORequest = nullptr;
	}

	if (LoadedChunk.bOwnsCompressedData)
	{
		delete LoadedChunk.CompressedAnimData;
	}

	LoadedChunk.CompressedAnimData = NULL;
	LoadedChunk.bOwnsCompressedData = false;
	LoadedChunk.Index = 0;
}

void FStreamingAnimationData::ResetRequestedChunks()
{
	RequestedChunks.Reset();
	RequestedChunks.Add(0); //Always want 1
}

////////////////////////////
// FAnimationStreamingManager //
////////////////////////////

FAnimationStreamingManager::FAnimationStreamingManager()
{
}

FAnimationStreamingManager::~FAnimationStreamingManager()
{
}

void FAnimationStreamingManager::OnAsyncFileCallback(FStreamingAnimationData* StreamingAnimData, int32 ChunkIndex, int64 ReadSize, IAsyncReadRequest* ReadRequest)
{
	// Check to see if we successfully managed to load anything
	uint8* Mem = ReadRequest->GetReadResults();

	if (Mem)
	{
		int32 LoadedChunkIndex = StreamingAnimData->LoadedChunks.IndexOfByPredicate([ChunkIndex](const FLoadedAnimationChunk& Chunk) { return Chunk.Index == ChunkIndex; });
		FLoadedAnimationChunk& ChunkStorage = StreamingAnimData->LoadedChunks[LoadedChunkIndex];

		checkf(!ChunkStorage.CompressedAnimData, TEXT("Chunk storage already has data. (0x%p)"), ChunkStorage.CompressedAnimData);

		ChunkStorage.CompressedAnimData = new FCompressedAnimSequence();

		TArrayView<const uint8> MemView(Mem, ReadSize);
		FMemoryReaderView Reader(MemView);

		UAnimStreamable* Anim = StreamingAnimData->StreamableAnim;
		ChunkStorage.CompressedAnimData->SerializeCompressedData(Reader, false, Anim, Anim->GetSkeleton(), Anim->CurveCompressionSettings);

		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Request Finished %.2f\nAnim Chunk Streamed %.4f\n"), FPlatformTime::Seconds(), FPlatformTime::Seconds() - ChunkStorage.RequestStart);
	}
}

void FAnimationStreamingManager::UpdateResourceStreaming(float DeltaTime, bool bProcessEverything /*= false*/)
{
	LLM_SCOPE(ELLMTag::Audio);

	FScopeLock Lock(&CriticalSection);

	for (auto& AnimData : StreamingAnimations)
	{
		AnimData.Value->UpdateStreamingStatus();
	}
}

int32 FAnimationStreamingManager::BlockTillAllRequestsFinished(float TimeLimit, bool)
{
	{
		FScopeLock Lock(&CriticalSection);

		QUICK_SCOPE_CYCLE_COUNTER(FAnimStreamingManager_BlockTillAllRequestsFinished);
		int32 Result = 0;

		if (TimeLimit == 0.0f)
		{
			for (auto& AnimPair : StreamingAnimations)
			{
				AnimPair.Value->BlockTillAllRequestsFinished();
			}
		}
		else
		{
			double EndTime = FPlatformTime::Seconds() + TimeLimit;
			for (auto& AnimPair : StreamingAnimations)
			{
				float ThisTimeLimit = EndTime - FPlatformTime::Seconds();
				if (ThisTimeLimit < .001f || // one ms is the granularity of the platform event system
					!AnimPair.Value->BlockTillAllRequestsFinished(ThisTimeLimit))
				{
					Result = 1; // we don't report the actual number, just 1 for any number of outstanding requests
					break;
				}
			}
		}

		return Result;
	}

	// Not sure yet whether this will work the same as textures - aside from just before destroying
	return 0;
}

void FAnimationStreamingManager::CancelForcedResources()
{
}

void FAnimationStreamingManager::NotifyLevelChange()
{
}

void FAnimationStreamingManager::SetDisregardWorldResourcesForFrames(int32 NumFrames)
{
}

void FAnimationStreamingManager::AddLevel(class ULevel* Level)
{
}

void FAnimationStreamingManager::RemoveLevel(class ULevel* Level)
{
}

void FAnimationStreamingManager::NotifyLevelOffset(class ULevel* Level, const FVector& Offset)
{
}

void FAnimationStreamingManager::AddStreamingAnim(UAnimStreamable* Anim)
{
	FScopeLock Lock(&CriticalSection);
	if (StreamingAnimations.FindRef(Anim) == nullptr)
	{
		FStreamingAnimationData* NewStreamingAnim = new FStreamingAnimationData;
		if (NewStreamingAnim->Initialize(Anim, this))
		{
			StreamingAnimations.Add(Anim, NewStreamingAnim);
		}
		else
		{
			// Failed to initialize, don't add to list of streaming sound waves
			delete NewStreamingAnim;
		}
	}
}

bool FAnimationStreamingManager::RemoveStreamingAnim(UAnimStreamable* Anim)
{
	FScopeLock Lock(&CriticalSection);
	FStreamingAnimationData* AnimData = StreamingAnimations.FindRef(Anim);
	if (AnimData)
	{
		StreamingAnimations.Remove(Anim);

		// Free the resources of the streaming wave data. This blocks pending IO requests
		AnimData->FreeResources();
		delete AnimData;
		return true;
	}
	return false;
}

const FCompressedAnimSequence* FAnimationStreamingManager::GetLoadedChunk(const UAnimStreamable* Anim, uint32 ChunkIndex) const
{
	// Check for the spoof of failing to load a stream chunk
	if (SpoofFailedAnimationChunkLoad > 0)
	{
		return nullptr;
	}

	// If we fail at getting the critical section here, early out.
	FScopeLock MapLock(&CriticalSection);

	FStreamingAnimationData* AnimData = StreamingAnimations.FindRef(Anim);
	if (AnimData)
	{
		AnimData->RequestedChunks.AddUnique(ChunkIndex);
		AnimData->RequestedChunks.AddUnique((ChunkIndex + 1) % Anim->GetRunningPlatformData().Chunks.Num());

		if (AnimData->LoadedChunkIndices.Contains(ChunkIndex))
		{
			const FLoadedAnimationChunk* Chunk = Algo::FindBy(AnimData->LoadedChunks, ChunkIndex, &FLoadedAnimationChunk::Index);
			return Chunk ? Chunk->CompressedAnimData : nullptr;
		}
	}

	return nullptr;
}