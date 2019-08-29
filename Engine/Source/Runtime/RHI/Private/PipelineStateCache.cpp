// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
PipelineStateCache.cpp: Pipeline state cache implementation.
=============================================================================*/

#include "PipelineStateCache.h"
#include "PipelineFileCache.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/ScopeLock.h"
#include "Misc/CoreDelegates.h"
#include "CoreGlobals.h"
#include "Misc/TimeGuard.h"
#include "Containers/DiscardableKeyValueCache.h"

// perform cache eviction each frame, used to stress the system and flush out bugs
#define PSO_DO_CACHE_EVICT_EACH_FRAME 0

// Log event and info about cache eviction
#define PSO_LOG_CACHE_EVICT 0

// Stat tracking
#define PSO_TRACK_CACHE_STATS 0


#define PIPELINESTATECACHE_VERIFYTHREADSAFE (!UE_BUILD_SHIPPING && !UE_BUILD_TEST)

static inline uint32 GetTypeHash(const FBoundShaderStateInput& Input)
{
	return GetTypeHash(Input.VertexDeclarationRHI) ^
		GetTypeHash(Input.VertexShaderRHI) ^
		GetTypeHash(Input.PixelShaderRHI) ^
		GetTypeHash(Input.HullShaderRHI) ^
		GetTypeHash(Input.DomainShaderRHI) ^
		GetTypeHash(Input.GeometryShaderRHI);
}

static inline uint32 GetTypeHash(const FGraphicsPipelineStateInitializer& Initializer)
{
	//#todo-rco: Hash!
	return (GetTypeHash(Initializer.BoundShaderState) | (Initializer.NumSamples << 28)) ^ ((uint32)Initializer.PrimitiveType << 24) ^ GetTypeHash(Initializer.BlendState)
		^ Initializer.RenderTargetsEnabled ^ GetTypeHash(Initializer.RasterizerState) ^ GetTypeHash(Initializer.DepthStencilState);
}

static TAutoConsoleVariable<int32> GCVarAsyncPipelineCompile(
	TEXT("r.AsyncPipelineCompile"),
	1,
	TEXT("0 to Create PSOs at the moment they are requested\n")\
	TEXT("1 to Create Pipeline State Objects asynchronously(default)"),
	ECVF_ReadOnly | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarPSOEvictionTime(
	TEXT("r.pso.evictiontime"),
	60,
	TEXT("Time between checks to remove stale objects from the cache. 0 = no eviction (which may eventually OOM...)"),
	ECVF_ReadOnly | ECVF_RenderThreadSafe
);

extern void DumpPipelineCacheStats();

static FAutoConsoleCommand DumpPipelineCmd(
	TEXT("r.DumpPipelineCache"),
	TEXT("Dump current cache stats."),
	FConsoleCommandDelegate::CreateStatic(DumpPipelineCacheStats)
);

void SetComputePipelineState(FRHICommandList& RHICmdList, FRHIComputeShader* ComputeShader)
{
	RHICmdList.SetComputePipelineState(PipelineStateCache::GetAndOrCreateComputePipelineState(RHICmdList, ComputeShader));
}

extern RHI_API FRHIComputePipelineState* ExecuteSetComputePipelineState(FComputePipelineState* ComputePipelineState);
extern RHI_API FRHIGraphicsPipelineState* ExecuteSetGraphicsPipelineState(FGraphicsPipelineState* GraphicsPipelineState);

/**
 * Base class to hold pipeline state (and optionally stats)
 */
class FPipelineState
{
public:

	FPipelineState()
	: Stats(nullptr)
	{
		InitStats();
	}

	virtual ~FPipelineState() 
	{
	}

	virtual bool IsCompute() const = 0;

	FGraphEventRef CompletionEvent;

	inline void AddUse()
	{
		FPipelineStateStats::UpdateStats(Stats);
	}
	
#if PSO_TRACK_CACHE_STATS
	
	void InitStats()
	{
		FirstUsedTime = LastUsedTime = FPlatformTime::Seconds();
		FirstFrameUsed = LastFrameUsed = 0;
		Hits = HitsAcrossFrames = 0;
	}
	
	void AddHit()
	{
		LastUsedTime = FPlatformTime::Seconds();
		Hits++;

		if (LastFrameUsed != GFrameCounter)
		{
			LastFrameUsed = GFrameCounter;
			HitsAcrossFrames++;
		}
	}

	double			FirstUsedTime;
	double			LastUsedTime;
	uint64			FirstFrameUsed;
	uint64			LastFrameUsed;
	int				Hits;
	int				HitsAcrossFrames;

#else
	void InitStats() {}
	void AddHit() {}
#endif // PSO_TRACK_CACHE_STATS

	FPipelineStateStats* Stats;
};

/* State for compute  */
class FComputePipelineState : public FPipelineState
{
public:
	FComputePipelineState(FRHIComputeShader* InComputeShader)
		: ComputeShader(InComputeShader)
	{
		ComputeShader->AddRef();
	}
	
	~FComputePipelineState()
	{
		ComputeShader->Release();
	}

	virtual bool IsCompute() const
	{
		return true;
	}

	FRHIComputeShader* ComputeShader;
	TRefCountPtr<FRHIComputePipelineState> RHIPipeline;
};

/* State for graphics */
class FGraphicsPipelineState : public FPipelineState
{
public:
	FGraphicsPipelineState() 
	{
	}

	virtual bool IsCompute() const
	{
		return false;
	}

	TRefCountPtr<FRHIGraphicsPipelineState> RHIPipeline;
#if PIPELINESTATECACHE_VERIFYTHREADSAFE
	FThreadSafeCounter InUseCount;
#endif
};

void SetGraphicsPipelineState(FRHICommandList& RHICmdList, const FGraphicsPipelineStateInitializer& Initializer, EApplyRendertargetOption ApplyFlags)
{
	FGraphicsPipelineState* PipelineState = PipelineStateCache::GetAndOrCreateGraphicsPipelineState(RHICmdList, Initializer, ApplyFlags);
#if PIPELINESTATECACHE_VERIFYTHREADSAFE
	int32 Result = PipelineState->InUseCount.Increment();
	check(Result >= 1);
#endif
	check(IsInRenderingThread() || IsInParallelRenderingThread());
	RHICmdList.SetGraphicsPipelineState(PipelineState);
}

/* TSharedPipelineStateCache
 * This is a cache of the * pipeline states
 * there is a local thread cache which is consolidated with the global thread cache
 * global thread cache is read only until the end of the frame when the local thread caches are consolidated
 */
template<class TMyKey,class TMyValue>
class TSharedPipelineStateCache
{
private:

	TMap<TMyKey, TMyValue>& GetLocalCache()
	{
		void* TLSValue = FPlatformTLS::GetTlsValue(TLSSlot);
		if (TLSValue == nullptr)
		{
			FPipelineStateCacheType* PipelineStateCache = new FPipelineStateCacheType;
			FPlatformTLS::SetTlsValue(TLSSlot, (void*)(PipelineStateCache) );

			FScopeLock S(&AllThreadsLock);
			AllThreadsPipelineStateCache.Add(PipelineStateCache);
			return *PipelineStateCache;
		}
		return *((FPipelineStateCacheType*)TLSValue);
	}

#if PIPELINESTATECACHE_VERIFYTHREADSAFE
	struct FScopeVerifyIncrement
	{
		volatile int32 &VerifyMutex;
		FScopeVerifyIncrement(volatile int32& InVerifyMutex) : VerifyMutex(InVerifyMutex)
		{
			int32 Result = FPlatformAtomics::InterlockedIncrement(&VerifyMutex);
			if (Result <= 0)
			{
				UE_LOG(LogRHI, Fatal, TEXT("Find was hit while Consolidate was running"));
			}
		}

		~FScopeVerifyIncrement()
		{
			int32 Result = FPlatformAtomics::InterlockedDecrement(&VerifyMutex);
			if (Result < 0)
			{
				UE_LOG(LogRHI, Fatal, TEXT("Find was hit while Consolidate was running"));
			}
		}
	};

	struct FScopeVerifyDecrement
	{
		volatile int32 &VerifyMutex;
		FScopeVerifyDecrement(volatile int32& InVerifyMutex) : VerifyMutex(InVerifyMutex)
		{
			int32 Result = FPlatformAtomics::InterlockedDecrement(&VerifyMutex);
			if (Result >= 0)
			{
				UE_LOG(LogRHI, Fatal, TEXT("Consolidate was hit while Get/SetPSO was running"));
			}
		}

		~FScopeVerifyDecrement()
		{
			int32 Result = FPlatformAtomics::InterlockedIncrement(&VerifyMutex);
			if (Result != 0)
			{
				UE_LOG(LogRHI, Fatal, TEXT("Consolidate was hit while Get/SetPSO was running"));
			}
		}
	};
#endif

public:
	typedef TMap<TMyKey,TMyValue> FPipelineStateCacheType;

	TSharedPipelineStateCache()
	{
		CurrentMap = &Map1;
		BackfillMap = &Map2;
		DuplicateStateGenerated = 0;
		TLSSlot = FPlatformTLS::AllocTlsSlot();
	}

	bool Find( const TMyKey& InKey, TMyValue& OutResult )
	{
#if PIPELINESTATECACHE_VERIFYTHREADSAFE
		FScopeVerifyIncrement S(VerifyMutex);
#endif
		// safe because we only ever find when we don't add
		TMyValue* Result = CurrentMap->Find(InKey);

		if ( Result )
		{
			OutResult = *Result;
			return true;
		}

		// check the local cahce which is safe because only this thread adds to it
		TMap<TMyKey, TMyValue> &LocalCache = GetLocalCache();
		// if it's not in the local cache then it will rebuild
		Result = LocalCache.Find(InKey);
		if (Result)
		{
			OutResult = *Result;
			return true;
		}

		Result = BackfillMap->Find(InKey);

		if ( Result )
		{
			LocalCache.Add(InKey, *Result);
			OutResult = *Result;
			return true;
		}


		return false;
		
		
	}

	bool Add(const TMyKey& InKey, const TMyValue& InValue)
	{
#if PIPELINESTATECACHE_VERIFYTHREADSAFE
		FScopeVerifyIncrement S(VerifyMutex);
#endif
		// everything is added to the local cache then at end of frame we consoldate them all
		TMap<TMyKey, TMyValue> &LocalCache = GetLocalCache();

		check( LocalCache.Contains(InKey) == false );
		LocalCache.Add(InKey, InValue);
		return true;
	}

	void ConsolidateThreadedCaches()
	{

		SCOPE_TIME_GUARD_MS(TEXT("ConsolidatePipelineCache"), 0.1);
		check(IsInRenderingThread());
#if PIPELINESTATECACHE_VERIFYTHREADSAFE
		FScopeVerifyDecrement S(VerifyMutex);
#endif
		
		// consolidate all the local threads keys with the current thread
		// No one is allowed to call GetLocalCache while this is running
		// this is verified by the VerifyMutex.
		for ( FPipelineStateCacheType* PipelineStateCache : AllThreadsPipelineStateCache)
		{
			for (auto PipelineStateCacheIterator = PipelineStateCache->CreateIterator(); PipelineStateCacheIterator; ++PipelineStateCacheIterator)
			{
				const TMyKey& ThreadKey = PipelineStateCacheIterator->Key;
				const TMyValue& ThreadValue = PipelineStateCacheIterator->Value;

				// All events should be complete because we are running this code after the RHI Flush
				if(!ThreadValue->CompletionEvent.IsValid() || ThreadValue->CompletionEvent->IsComplete())
				{
					ThreadValue->CompletionEvent = nullptr;

					BackfillMap->Remove(ThreadKey);

					TMyValue* CurrentValue = CurrentMap->Find(ThreadKey);
					if (CurrentValue)
					{
						// if two threads get from the backfill map then we might just be dealing with one pipelinestate, in which case we have already added it to the currentmap and don't need to do anything else
						if ( *CurrentValue != ThreadValue )
						{
							++DuplicateStateGenerated;
							DeleteArray.Add(ThreadValue);
						}
					}
					else
					{
						CurrentMap->Add(ThreadKey, ThreadValue);
					}
					PipelineStateCacheIterator.RemoveCurrent();
				}
			}
		}

	}

	void ProcessDelayedCleanup()
	{
		check(IsInRenderingThread());

		for (TMyValue& OldPipelineState : DeleteArray)
		{
			//once in the delayed list this object should not be findable anymore, so the 0 should remain, making this safe
#if PIPELINESTATECACHE_VERIFYTHREADSAFE
			check(OldPipelineState->InUseCount.GetValue() == 0);
#endif
			delete OldPipelineState;
		}
		DeleteArray.Empty();
	}


	int32 DiscardAndSwap()
	{
		// the consolidate should always be run before the DiscardAndSwap.
		// there should be no inuse pipeline states in the backfill map (because they should have been moved into the CurrentMap).
		int32 Discarded = BackfillMap->Num();


		for ( const auto& DiscardIterator : *BackfillMap )
		{
#if PIPELINESTATECACHE_VERIFYTHREADSAFE
			check( DiscardIterator.Value->InUseCount.GetValue() == 0);
#endif
			delete DiscardIterator.Value;
		}

		BackfillMap->Empty();


		if ( CurrentMap == &Map1 )
		{
			CurrentMap = &Map2;
			BackfillMap = &Map1;
		}
		else
		{
			CurrentMap = &Map1;
			BackfillMap = &Map2;
		}
		return Discarded;
	}
private:
	uint32 TLSSlot;
	FPipelineStateCacheType *CurrentMap;
	FPipelineStateCacheType *BackfillMap;

	FPipelineStateCacheType Map1;
	FPipelineStateCacheType Map2;

	TArray<TMyValue> DeleteArray;

	FCriticalSection AllThreadsLock;
	TArray<FPipelineStateCacheType*> AllThreadsPipelineStateCache;

	uint32 DuplicateStateGenerated;

#if PIPELINESTATECACHE_VERIFYTHREADSAFE
	volatile int32 VerifyMutex;
#endif

};

// Typed caches for compute and graphics
typedef TDiscardableKeyValueCache< FRHIComputeShader*, FComputePipelineState*> FComputePipelineCache;
typedef TSharedPipelineStateCache<FGraphicsPipelineStateInitializer, FGraphicsPipelineState*> FGraphicsPipelineCache;

// These are the actual caches for both pipelines
FComputePipelineCache GComputePipelineCache;
FGraphicsPipelineCache GGraphicsPipelineCache;

/**
 *  Compile task
 */
class FCompilePipelineStateTask
{
public:
	FPipelineState* Pipeline;
	FGraphicsPipelineStateInitializer Initializer;

	// InInitializer is only used for non-compute tasks, a default can just be used otherwise
	FCompilePipelineStateTask(FPipelineState* InPipeline, const FGraphicsPipelineStateInitializer& InInitializer)
		: Pipeline(InPipeline)
		, Initializer(InInitializer)
	{
		if (Initializer.BoundShaderState.VertexDeclarationRHI)
			Initializer.BoundShaderState.VertexDeclarationRHI->AddRef();
		if (Initializer.BoundShaderState.VertexShaderRHI)
			Initializer.BoundShaderState.VertexShaderRHI->AddRef();
		if (Initializer.BoundShaderState.PixelShaderRHI)
			Initializer.BoundShaderState.PixelShaderRHI->AddRef();
		if (Initializer.BoundShaderState.GeometryShaderRHI)
			Initializer.BoundShaderState.GeometryShaderRHI->AddRef();
		if (Initializer.BoundShaderState.DomainShaderRHI)
			Initializer.BoundShaderState.DomainShaderRHI->AddRef();
		if (Initializer.BoundShaderState.HullShaderRHI)
			Initializer.BoundShaderState.HullShaderRHI->AddRef();
		if (Initializer.BlendState)
			Initializer.BlendState->AddRef();
		if (Initializer.RasterizerState)
			Initializer.RasterizerState->AddRef();
		if (Initializer.DepthStencilState)
			Initializer.DepthStencilState->AddRef();
	}

	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		LLM_SCOPE(ELLMTag::PSO);

		if (Pipeline->IsCompute())
		{
			FComputePipelineState* ComputePipeline = static_cast<FComputePipelineState*>(Pipeline);
			ComputePipeline->RHIPipeline = RHICreateComputePipelineState(ComputePipeline->ComputeShader);
		}
		else
		{
			FGraphicsPipelineState* GfxPipeline = static_cast<FGraphicsPipelineState*>(Pipeline);
			GfxPipeline->RHIPipeline = RHICreateGraphicsPipelineState(Initializer);
			
			if (Initializer.BoundShaderState.VertexDeclarationRHI)
				Initializer.BoundShaderState.VertexDeclarationRHI->Release();
			if (Initializer.BoundShaderState.VertexShaderRHI)
				Initializer.BoundShaderState.VertexShaderRHI->Release();
			if (Initializer.BoundShaderState.PixelShaderRHI)
				Initializer.BoundShaderState.PixelShaderRHI->Release();
			if (Initializer.BoundShaderState.GeometryShaderRHI)
				Initializer.BoundShaderState.GeometryShaderRHI->Release();
			if (Initializer.BoundShaderState.DomainShaderRHI)
				Initializer.BoundShaderState.DomainShaderRHI->Release();
			if (Initializer.BoundShaderState.HullShaderRHI)
				Initializer.BoundShaderState.HullShaderRHI->Release();
			if (Initializer.BlendState)
				Initializer.BlendState->Release();
			if (Initializer.RasterizerState)
				Initializer.RasterizerState->Release();
			if (Initializer.DepthStencilState)
				Initializer.DepthStencilState->Release();
		}
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FCompilePipelineStateTask, STATGROUP_TaskGraphTasks);
	}

	ENamedThreads::Type GetDesiredThread()
	{
		return ENamedThreads::AnyThread;
	}
};

/**
* Called at the end of each frame during the RHI . Evicts all items left in the backfill cached based on time
*/
void PipelineStateCache::FlushResources()
{
	static bool PerformedOneTimeInit = false;

	check(IsInRenderingThread());

	GGraphicsPipelineCache.ConsolidateThreadedCaches();
	GGraphicsPipelineCache.ProcessDelayedCleanup();
	// Thread-safe one-time initialization of things we need to set up
	if (PerformedOneTimeInit == false)
	{
		PerformedOneTimeInit = true;

		// We don't trim, but we will dump out how much memory we're using..
		FCoreDelegates::GetMemoryTrimDelegate().AddLambda([]()
		{
#if PSO_TRACK_CACHE_STATS
			DumpPipelineCacheStats();
#endif
		});

		PerformedOneTimeInit = true;

	}

	static double LastEvictionTime = FPlatformTime::Seconds();
	double CurrentTime = FPlatformTime::Seconds();

#if PSO_DO_CACHE_EVICT_EACH_FRAME
	LastEvictionTime = 0;
#endif
	
	// because it takes two cycles for an object to move from main->backfill->gone we check
	// at half the desired eviction time
	int32 EvictionPeriod = CVarPSOEvictionTime.GetValueOnAnyThread();

	if (EvictionPeriod == 0 || CurrentTime - LastEvictionTime < EvictionPeriod)
	{
		return;
	}

	// This should be very fast, if not it's likely eviction time is too high and too 
	// many items are building up.
	SCOPE_TIME_GUARD_MS(TEXT("TrimPiplelineCache"), 0.1);

#if PSO_TRACK_CACHE_STATS
	DumpPipelineCacheStats();
#endif

	LastEvictionTime = CurrentTime;

	int ReleasedComputeEntries = 0;
	int ReleasedGraphicsEntries = 0;

	ReleasedComputeEntries = GComputePipelineCache.Discard([](FComputePipelineState* CacheItem) {
		delete CacheItem;
	});

	ReleasedGraphicsEntries = GGraphicsPipelineCache.DiscardAndSwap();

#if PSO_TRACK_CACHE_STATS
	UE_LOG(LogRHI, Log, TEXT("Cleared state cache in %.02f ms. %d ComputeEntries, %d Graphics entries")
		, (FPlatformTime::Seconds() - CurrentTime) / 1000
		, ReleasedComputeEntries, ReleasedGraphicsEntries);
#endif // PSO_TRACK_CACHE_STATS

}

static bool IsAsyncCompilationAllowed(FRHICommandList& RHICmdList)
{
	return GCVarAsyncPipelineCompile.GetValueOnAnyThread() && !RHICmdList.Bypass() && IsRunningRHIInSeparateThread();
}

FComputePipelineState* PipelineStateCache::GetAndOrCreateComputePipelineState(FRHICommandList& RHICmdList, FRHIComputeShader* ComputeShader)
{
	SCOPE_CYCLE_COUNTER(STAT_GetOrCreatePSO);
	
	bool DoAsyncCompile = IsAsyncCompilationAllowed(RHICmdList);

	FComputePipelineState* OutCachedState = nullptr;

	uint32 LockFlags = GComputePipelineCache.ApplyLock(0, FComputePipelineCache::LockFlags::ReadLock);

	bool WasFound = GComputePipelineCache.Find(ComputeShader, OutCachedState, LockFlags | FComputePipelineCache::LockFlags::WriteLockOnAddFail, LockFlags);

	if (WasFound == false)
	{
		FPipelineFileCache::CacheComputePSO(GetTypeHash(ComputeShader), ComputeShader);

		// create new graphics state
		OutCachedState = new FComputePipelineState(ComputeShader);
		OutCachedState->Stats = FPipelineFileCache::RegisterPSOStats(GetTypeHash(ComputeShader));

		// create a compilation task, or just do it now...
		if (DoAsyncCompile)
		{
			OutCachedState->CompletionEvent = TGraphTask<FCompilePipelineStateTask>::CreateTask().ConstructAndDispatchWhenReady(OutCachedState, FGraphicsPipelineStateInitializer());
			RHICmdList.QueueAsyncPipelineStateCompile(OutCachedState->CompletionEvent);
		}
		else
		{
			OutCachedState->RHIPipeline = RHICreateComputePipelineState(OutCachedState->ComputeShader);
		}

		GComputePipelineCache.Add(ComputeShader, OutCachedState, LockFlags);
	}
	else
	{
		if (DoAsyncCompile)
		{
			FGraphEventRef& CompletionEvent = OutCachedState->CompletionEvent;
			if ( CompletionEvent.IsValid() && !CompletionEvent->IsComplete() )
			{
				RHICmdList.QueueAsyncPipelineStateCompile(CompletionEvent);
			}
		}

#if PSO_TRACK_CACHE_STATS
		OutCachedState->AddHit();
#endif
	}

	GComputePipelineCache.Unlock(LockFlags);

#if 0
	bool DoAsyncCompile = IsAsyncCompilationAllowed(RHICmdList);


	TSharedPtr<FComputePipelineState, ESPMode::Fast> OutCachedState;

	// Find or add an entry for this initializer
	bool WasFound = GComputePipelineCache.FindOrAdd(ComputeShader, OutCachedState, [&RHICmdList, &ComputeShader, &DoAsyncCompile] {
			// create new graphics state
			TSharedPtr<FComputePipelineState, ESPMode::Fast> PipelineState(new FComputePipelineState(ComputeShader));
			PipelineState->Stats = FPipelineFileCache::RegisterPSOStats(GetTypeHash(ComputeShader));

			// create a compilation task, or just do it now...
			if (DoAsyncCompile)
			{
				PipelineState->CompletionEvent = TGraphTask<FCompilePipelineStateTask>::CreateTask().ConstructAndDispatchWhenReady(PipelineState.Get(), FGraphicsPipelineStateInitializer());
				RHICmdList.QueueAsyncPipelineStateCompile(PipelineState->CompletionEvent);
			}
			else
			{
				PipelineState->RHIPipeline = RHICreateComputePipelineState(PipelineState->ComputeShader);
			}

			// wrap it and return it
			return PipelineState;
		});

	check(OutCachedState.IsValid());

	// if we found an entry the block above wasn't executed
	if (WasFound)
	{
		if (DoAsyncCompile)
		{
			FRWScopeLock ScopeLock(GComputePipelineCache.RWLock(), SLT_ReadOnly);
			FGraphEventRef& CompletionEvent = OutCachedState->CompletionEvent;
			if (CompletionEvent.IsValid() && !CompletionEvent->IsComplete())
			{
				RHICmdList.QueueAsyncPipelineStateCompile(CompletionEvent);
			}
		}
#if PSO_TRACK_CACHE_STATS
		OutCachedState->AddHit();
#endif
	}
#endif
	// return the state pointer
	return OutCachedState;
}

FRHIComputePipelineState* ExecuteSetComputePipelineState(FComputePipelineState* ComputePipelineState)
{
	ensure(ComputePipelineState->RHIPipeline);
	FRWScopeLock ScopeLock(GComputePipelineCache.RWLock(), SLT_Write);
	ComputePipelineState->AddUse();
	ComputePipelineState->CompletionEvent = nullptr;
	return ComputePipelineState->RHIPipeline;
}

FGraphicsPipelineState* PipelineStateCache::GetAndOrCreateGraphicsPipelineState(FRHICommandList& RHICmdList, const FGraphicsPipelineStateInitializer& OriginalInitializer, EApplyRendertargetOption ApplyFlags)
{
	LLM_SCOPE(ELLMTag::PSO);
	SCOPE_CYCLE_COUNTER(STAT_GetOrCreatePSO);

	FGraphicsPipelineStateInitializer NewInitializer;
	const FGraphicsPipelineStateInitializer* Initializer = &OriginalInitializer;

	check(OriginalInitializer.DepthStencilState && OriginalInitializer.BlendState && OriginalInitializer.RasterizerState);

	if (!!(ApplyFlags & EApplyRendertargetOption::ForceApply))
	{
		// Copy original initializer first, then apply the render targets
		NewInitializer = OriginalInitializer;
		RHICmdList.ApplyCachedRenderTargets(NewInitializer);
		Initializer = &NewInitializer;
	}

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST 
	else if (!!(ApplyFlags & EApplyRendertargetOption::CheckApply))
	{
		// Catch cases where the state does not match
		NewInitializer = OriginalInitializer;
		RHICmdList.ApplyCachedRenderTargets(NewInitializer);

		int AnyFailed = 0;
		AnyFailed |= (NewInitializer.RenderTargetsEnabled != OriginalInitializer.RenderTargetsEnabled) << 0;

		if (AnyFailed == 0)
		{
			for (int i = 0; i < (int)NewInitializer.RenderTargetsEnabled; i++)
			{
				AnyFailed |= (NewInitializer.RenderTargetFormats[i] != OriginalInitializer.RenderTargetFormats[i]) << 1;
				AnyFailed |= (NewInitializer.RenderTargetFlags[i] != OriginalInitializer.RenderTargetFlags[i]) << 2;
				AnyFailed |= (NewInitializer.RenderTargetLoadActions[i] != OriginalInitializer.RenderTargetLoadActions[i]) << 3;
				AnyFailed |= (NewInitializer.RenderTargetStoreActions[i] != OriginalInitializer.RenderTargetStoreActions[i]) << 4;

				if (AnyFailed)
				{
					AnyFailed |= i << 24;
					break;
				}
			}
		}

		AnyFailed |= (NewInitializer.DepthStencilTargetFormat != OriginalInitializer.DepthStencilTargetFormat) << 5;
		AnyFailed |= (NewInitializer.DepthStencilTargetFlag != OriginalInitializer.DepthStencilTargetFlag) << 6;
		AnyFailed |= (NewInitializer.DepthTargetLoadAction != OriginalInitializer.DepthTargetLoadAction) << 7;
		AnyFailed |= (NewInitializer.DepthTargetStoreAction != OriginalInitializer.DepthTargetStoreAction) << 8;
		AnyFailed |= (NewInitializer.StencilTargetLoadAction != OriginalInitializer.StencilTargetLoadAction) << 9;
		AnyFailed |= (NewInitializer.StencilTargetStoreAction != OriginalInitializer.StencilTargetStoreAction) << 10;

		static double LastTime = 0;
		if (AnyFailed != 0 && (FPlatformTime::Seconds() - LastTime) >= 10.0f)
		{
			LastTime = FPlatformTime::Seconds();
			UE_LOG(LogRHI, Error, TEXT("GetAndOrCreateGraphicsPipelineState RenderTarget check failed with: %i !"), AnyFailed);
		}
		Initializer = (AnyFailed != 0) ? &NewInitializer : &OriginalInitializer;
	}
#endif

	bool DoAsyncCompile = IsAsyncCompilationAllowed(RHICmdList);

	FGraphicsPipelineState* OutCachedState = nullptr;

	bool bWasFound = GGraphicsPipelineCache.Find(*Initializer, OutCachedState);

	if (bWasFound == false)
	{
		FPipelineFileCache::CacheGraphicsPSO(GetTypeHash(*Initializer), *Initializer);

		// create new graphics state
		OutCachedState = new FGraphicsPipelineState();
		OutCachedState->Stats = FPipelineFileCache::RegisterPSOStats(GetTypeHash(*Initializer));

		// create a compilation task, or just do it now...
		if (DoAsyncCompile)
		{
			OutCachedState->CompletionEvent = TGraphTask<FCompilePipelineStateTask>::CreateTask().ConstructAndDispatchWhenReady(OutCachedState, *Initializer);
			RHICmdList.QueueAsyncPipelineStateCompile(OutCachedState->CompletionEvent);
		}
		else
		{
			OutCachedState->RHIPipeline = RHICreateGraphicsPipelineState(*Initializer);
		}

		// GGraphicsPipelineCache.Add(*Initializer, OutCachedState, LockFlags);
		GGraphicsPipelineCache.Add(*Initializer, OutCachedState);
	}
	else
	{
		if (DoAsyncCompile)
		{
			FGraphEventRef& CompletionEvent = OutCachedState->CompletionEvent;
			if ( CompletionEvent.IsValid() && !CompletionEvent->IsComplete() )
			{
				RHICmdList.QueueAsyncPipelineStateCompile(CompletionEvent);
			}
		}

#if PSO_TRACK_CACHE_STATS
		OutCachedState->AddHit();
#endif
	}

	// return the state pointer
	return OutCachedState;
}

FRHIGraphicsPipelineState* ExecuteSetGraphicsPipelineState(FGraphicsPipelineState* GraphicsPipelineState)
{
	FRHIGraphicsPipelineState* RHIPipeline = GraphicsPipelineState->RHIPipeline;

	GraphicsPipelineState->AddUse();

#if PIPELINESTATECACHE_VERIFYTHREADSAFE
	int32 Result = GraphicsPipelineState->InUseCount.Decrement();
	check(Result >= 0);
#endif
	
	return RHIPipeline;
}

void DumpPipelineCacheStats()
{
#if PSO_TRACK_CACHE_STATS
	double TotalTime = 0.0;
	double MinTime = FLT_MAX;
	double MaxTime = FLT_MIN;

	int MinFrames = INT_MAX;
	int MaxFrames = INT_MIN;
	int TotalFrames = 0;

	int NumUsedLastMin = 0;
	int NumHits = 0;
	int NumHitsAcrossFrames = 0;
	int NumItemsMultipleFrameHits = 0;

	int NumCachedItems = GGraphicsPipelineCache.Current().Num();

	if (NumCachedItems == 0)
	{
		return;
	}

	for (auto GraphicsPipeLine : GGraphicsPipelineCache.Current())
	{
		TSharedPtr<FGraphicsPipelineState, ESPMode::Fast> State = GraphicsPipeLine.Value;

		// calc timestats
		double SinceUse = FPlatformTime::Seconds() - State->FirstUsedTime;

		TotalTime += SinceUse;

		if (SinceUse <= 30.0)
		{
			NumUsedLastMin++;
		}

		MinTime = FMath::Min(MinTime, SinceUse);
		MaxTime = FMath::Max(MaxTime, SinceUse);

		// calc frame stats
		int FramesUsed = State->LastFrameUsed - State->FirstFrameUsed;
		TotalFrames += FramesUsed;
		MinFrames = FMath::Min(MinFrames, FramesUsed);
		MaxFrames = FMath::Max(MaxFrames, FramesUsed);

		NumHits += State->Hits;

		if (State->HitsAcrossFrames > 0)
		{
			NumHitsAcrossFrames += State->HitsAcrossFrames;
			NumItemsMultipleFrameHits++;
		}
	}

	UE_LOG(LogRHI, Log, TEXT("Have %d GraphicsPipeline entries"), NumCachedItems);
	UE_LOG(LogRHI, Log, TEXT("Secs Used: Min=%.02f, Max=%.02f, Avg=%.02f. %d used in last 30 secs"), MinTime, MaxTime, TotalTime / NumCachedItems, NumUsedLastMin);
	UE_LOG(LogRHI, Log, TEXT("Frames Used: Min=%d, Max=%d, Avg=%d"), MinFrames, MaxFrames, TotalFrames / NumCachedItems);
	UE_LOG(LogRHI, Log, TEXT("Hits: Avg=%d, Items with hits across frames=%d, Avg Hits across Frames=%d"), NumHits / NumCachedItems, NumItemsMultipleFrameHits, NumHitsAcrossFrames / NumCachedItems);

	size_t TrackingMem = sizeof(FGraphicsPipelineStateInitializer) * GGraphicsPipelineCache.Num();
	UE_LOG(LogRHI, Log, TEXT("Tracking Mem: %d kb"), TrackingMem / 1024);
#else
	UE_LOG(LogRHI, Error, TEXT("DEfine PSO_TRACK_CACHE_STATS for state and stats!"));
#endif // PSO_VALIDATE_CACHE
}

void PipelineStateCache::Shutdown()
{
	// call discard twice to clear both the backing and main caches
	for (int i = 0; i < 2; i++)
	{
		GComputePipelineCache.Discard([](FComputePipelineState* CacheItem) {
			delete CacheItem;
		});
		
		GGraphicsPipelineCache.DiscardAndSwap();
	}
	FPipelineFileCache::Shutdown();
}
