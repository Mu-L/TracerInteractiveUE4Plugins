// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
PipelineStateCache.cpp: Pipeline state cache implementation.
=============================================================================*/

#include "PipelineStateCache.h"
#include "PipelineFileCache.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/ScopeLock.h"
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
	return GetTypeHash(Input.VertexDeclarationRHI)
		^ GetTypeHash(Input.VertexShaderRHI)
		^ GetTypeHash(Input.PixelShaderRHI)
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
		^ GetTypeHash(Input.HullShaderRHI)
		^ GetTypeHash(Input.DomainShaderRHI)
#endif
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
		^ GetTypeHash(Input.GeometryShaderRHI)
#endif
		;
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

#if RHI_RAYTRACING
static TAutoConsoleVariable<int32> CVarRTPSOCacheSize(
	TEXT("r.RayTracing.PSOCacheSize"),
	50,
	TEXT("Number of ray tracing pipelines to keep in the cache (default = 50). Set to 0 to disable eviction.\n"),
	ECVF_ReadOnly | ECVF_RenderThreadSafe
);
#endif // RHI_RAYTRACING

extern void DumpPipelineCacheStats();

static FAutoConsoleCommand DumpPipelineCmd(
	TEXT("r.DumpPipelineCache"),
	TEXT("Dump current cache stats."),
	FConsoleCommandDelegate::CreateStatic(DumpPipelineCacheStats)
);

void SetComputePipelineState(FRHICommandList& RHICmdList, FRHIComputeShader* ComputeShader)
{
	RHICmdList.SetComputePipelineState(PipelineStateCache::GetAndOrCreateComputePipelineState(RHICmdList, ComputeShader), ComputeShader);
}

extern RHI_API FRHIComputePipelineState* ExecuteSetComputePipelineState(FComputePipelineState* ComputePipelineState);
extern RHI_API FRHIGraphicsPipelineState* ExecuteSetGraphicsPipelineState(FGraphicsPipelineState* GraphicsPipelineState);

// Prints out information about a failed compilation from Init.
// This is fatal unless the compilation request is from the PSO cache preload.
static void HandlePipelineCreationFailure(const FGraphicsPipelineStateInitializer& Init)
{
	UE_LOG(LogRHI, Error, TEXT("Failed to create GraphicsPipeline"));
	// Failure to compile is Fatal unless this is from the PSO file cache preloading.
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if(Init.BoundShaderState.VertexShaderRHI)
	{
		UE_LOG(LogRHI, Error, TEXT("Vertex: %s"), *Init.BoundShaderState.VertexShaderRHI->ShaderName);
	}
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	if(Init.BoundShaderState.HullShaderRHI)
	{
		UE_LOG(LogRHI, Error, TEXT("Hull: %s"), *Init.BoundShaderState.HullShaderRHI->ShaderName);
	}
	if(Init.BoundShaderState.DomainShaderRHI)
	{
		UE_LOG(LogRHI, Error, TEXT("Domain: %s"), *Init.BoundShaderState.DomainShaderRHI->ShaderName);
	}
#endif
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
	if(Init.BoundShaderState.GeometryShaderRHI)
	{
		UE_LOG(LogRHI, Error, TEXT("Geometry: %s"), *Init.BoundShaderState.GeometryShaderRHI->ShaderName);
	}
#endif
	if(Init.BoundShaderState.PixelShaderRHI)
	{
		UE_LOG(LogRHI, Error, TEXT("Pixel: %s"), *Init.BoundShaderState.PixelShaderRHI->ShaderName);
	}
	
	UE_LOG(LogRHI, Error, TEXT("Render Targets: (%u)"), Init.RenderTargetFormats.Num());
	for(int32 i = 0; i < Init.RenderTargetFormats.Num(); ++i)
	{
		//#todo-mattc GetPixelFormatString is not available in scw. Need to move it so we can print more info here.
		UE_LOG(LogRHI, Error, TEXT("0x%x"), (uint32)Init.RenderTargetFormats[i]);
	}
	
	UE_LOG(LogRHI, Error, TEXT("Depth Stencil Format:"));
	UE_LOG(LogRHI, Error, TEXT("0x%x"), Init.DepthStencilTargetFormat);
#endif
	
	if(Init.bFromPSOFileCache)
	{
		// Let the cache know so it hopefully won't give out this one again
		FPipelineFileCache::RegisterPSOCompileFailure(GetTypeHash(Init), Init);
	}
	else
	{
		UE_LOG(LogRHI, Fatal, TEXT("Shader compilation failures are Fatal."));
	}
}

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
	
	void WaitCompletion()
	{
		if(CompletionEvent.IsValid() && !CompletionEvent->IsComplete())
		{
			UE_LOG(LogRHI, Log, TEXT("FTaskGraphInterface Waiting on FPipelineState completionEvent"));
			FTaskGraphInterface::Get().WaitUntilTaskCompletes( CompletionEvent );
			CompletionEvent = nullptr;
		}
	}

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

#if RHI_RAYTRACING
/* State for ray tracing */
class FRayTracingPipelineState : public FPipelineState
{
public:
	FRayTracingPipelineState(const FRayTracingPipelineStateInitializer& Initializer)
	{
		int32 Index = 0;
		for (FRHIRayTracingShader* Shader : Initializer.GetHitGroupTable())
		{
			HitGroupShaderMap.Add(Shader->GetHash(), Index++);
		}
	}

	virtual bool IsCompute() const
	{
		return false;
	}

	inline void AddHit()
	{
		if (LastFrameHit != GFrameCounter)
		{
			LastFrameHit = GFrameCounter;
			HitsAcrossFrames++;
		}

		FPipelineState::AddHit();
	}

	bool operator < (const FRayTracingPipelineState& Other)
	{
		if (LastFrameHit != Other.LastFrameHit)
		{
			return LastFrameHit < Other.LastFrameHit;
		}
		return HitsAcrossFrames < Other.HitsAcrossFrames;
	}

	bool IsCompilationComplete() const 
	{
		return !CompletionEvent.IsValid() || CompletionEvent->IsComplete();
	}

	FRayTracingPipelineStateRHIRef RHIPipeline;

	uint64 HitsAcrossFrames = 0;
	uint64 LastFrameHit = 0;

	TMap<FSHAHash, int32> HitGroupShaderMap;

#if PIPELINESTATECACHE_VERIFYTHREADSAFE
	FThreadSafeCounter InUseCount;
#endif
};

//extern RHI_API FRHIRayTracingPipelineState* GetRHIRayTracingPipelineState(FRayTracingPipelineState* PipelineState);
RHI_API FRHIRayTracingPipelineState* GetRHIRayTracingPipelineState(FRayTracingPipelineState* PipelineState)
{
	ensure(PipelineState->RHIPipeline);
	PipelineState->CompletionEvent = nullptr;
	return PipelineState->RHIPipeline;
}

#endif // RHI_RAYTRACING

int32 FindRayTracingHitGroupIndex(FRayTracingPipelineState* Pipeline, FRHIRayTracingShader* HitGroupShader, bool bRequired)
{
#if RHI_RAYTRACING
	if (int32* FoundIndex = Pipeline->HitGroupShaderMap.Find(HitGroupShader->GetHash()))
	{
		return *FoundIndex;
	}
	checkf(!bRequired, TEXT("Required hit group shader was not found in the ray tracing pipeline."));
#endif // RHI_RAYTRACING

	return INDEX_NONE;
}

void SetGraphicsPipelineState(FRHICommandList& RHICmdList, const FGraphicsPipelineStateInitializer& Initializer, EApplyRendertargetOption ApplyFlags, bool bApplyAdditionalState)
{
	FGraphicsPipelineState* PipelineState = PipelineStateCache::GetAndOrCreateGraphicsPipelineState(RHICmdList, Initializer, ApplyFlags);
	if (PipelineState && (PipelineState->RHIPipeline || !Initializer.bFromPSOFileCache))
	{
#if PIPELINESTATECACHE_VERIFYTHREADSAFE
		int32 Result = PipelineState->InUseCount.Increment();
		check(Result >= 1);
#endif
		check(IsInRenderingThread() || IsInParallelRenderingThread());
		RHICmdList.SetGraphicsPipelineState(PipelineState, Initializer.BoundShaderState, bApplyAdditionalState);
	}
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
		checkfSlow(LocalCache.Contains(InKey), TEXT("PSO not found immediately after adding.  Likely cause is an uninitialized field in a constructor or copy constructor"));
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
	
	void WaitTasksComplete()
	{
		FScopeLock S(&AllThreadsLock);
		
		for ( FPipelineStateCacheType* PipelineStateCache : AllThreadsPipelineStateCache )
		{
			WaitTasksComplete(PipelineStateCache);
		}
		
		WaitTasksComplete(BackfillMap);
		WaitTasksComplete(CurrentMap);
	}

private:

	void WaitTasksComplete(FPipelineStateCacheType* PipelineStateCache)
	{
		FScopeLock S(&AllThreadsLock);
		for (auto PipelineStateCacheIterator = PipelineStateCache->CreateIterator(); PipelineStateCacheIterator; ++PipelineStateCacheIterator)
		{
			FGraphicsPipelineState* pGPipelineState = PipelineStateCacheIterator->Value;
			if(pGPipelineState != nullptr)
			{
				pGPipelineState->WaitCompletion();
			}
		}
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

FAutoConsoleTaskPriority CPrio_FCompilePipelineStateTask(
	TEXT("TaskGraph.TaskPriorities.CompilePipelineStateTask"),
	TEXT("Task and thread priority for FCompilePipelineStateTask."),
	ENamedThreads::HighThreadPriority,		// if we have high priority task threads, then use them...
	ENamedThreads::NormalTaskPriority,		// .. at normal task priority
	ENamedThreads::HighTaskPriority		// if we don't have hi pri threads, then use normal priority threads at high task priority instead
);
#if RHI_RAYTRACING

// Simple thread-safe pipeline state cache that's designed for low-frequency pipeline creation operations.
// The expected use case is a very infrequent (i.e. startup / load / streaming time) creation of ray tracing PSOs.
// This cache uses a single internal lock and therefore is not designed for highly concurrent operations.
class FRayTracingPipelineCache
{
public:
	FRayTracingPipelineCache()
	{}

	~FRayTracingPipelineCache()
	{}

	bool FindBase(const FRayTracingPipelineStateInitializer& Initializer, FRayTracingPipelineState*& OutPipeline) const
	{
		FScopeLock ScopeLock(&CriticalSection);

		// Find the most recently used pipeline with compatible configuration

		FRayTracingPipelineState* BestPipeline = nullptr;

		for (const auto& It : FullPipelines)
		{
			const FRayTracingPipelineStateSignature& CandidateInitializer = It.Key;
			FRayTracingPipelineState* CandidatePipeline = It.Value;

			if (!CandidatePipeline->RHIPipeline.IsValid()
				|| CandidateInitializer.bAllowHitGroupIndexing != Initializer.bAllowHitGroupIndexing
				|| CandidateInitializer.MaxPayloadSizeInBytes != Initializer.MaxPayloadSizeInBytes
				|| CandidateInitializer.GetRayGenHash() != Initializer.GetRayGenHash()
				|| CandidateInitializer.GetRayMissHash() != Initializer.GetRayMissHash()
				|| CandidateInitializer.GetCallableHash() != Initializer.GetCallableHash())
			{
				continue;
			}

			if (BestPipeline == nullptr || *BestPipeline < *CandidatePipeline)
			{
				BestPipeline = CandidatePipeline;
			}
		}

		if (BestPipeline)
		{
			OutPipeline = BestPipeline;
			return true;
		}
		else
		{
			return false;
		}
	}

	bool FindBySignature(const FRayTracingPipelineStateSignature& Signature, FRayTracingPipelineState*& OutCachedState) const
	{
		FScopeLock ScopeLock(&CriticalSection);

		FRayTracingPipelineState* const* FoundState = FullPipelines.Find(Signature);
		if (FoundState)
		{
			OutCachedState = *FoundState;
			return true;
		}
		else
		{
			return false;
		}
	}

	bool Find(const FRayTracingPipelineStateInitializer& Initializer, FRayTracingPipelineState*& OutCachedState) const
	{
		FScopeLock ScopeLock(&CriticalSection);

		const FPipelineMap& Cache = Initializer.bPartial ? PartialPipelines : FullPipelines;

		FRayTracingPipelineState* const* FoundState = Cache.Find(Initializer);
		if (FoundState)
		{
			OutCachedState = *FoundState;
			return true;
		}
		else
		{
			return false;
		}
	}

	// Creates and returns a new pipeline state object, adding it to internal cache.
	// The cache itself owns the object and is responsible for destroying it.
	FRayTracingPipelineState* Add(const FRayTracingPipelineStateInitializer& Initializer)
	{
		FRayTracingPipelineState* Result = new FRayTracingPipelineState(Initializer);

		FScopeLock ScopeLock(&CriticalSection);

		FPipelineMap& Cache = Initializer.bPartial ? PartialPipelines : FullPipelines;

		Cache.Add(Initializer, Result);
		Result->AddHit();

		return Result;
	}

	void Shutdown()
	{
		FScopeLock ScopeLock(&CriticalSection);
		for (auto& It : FullPipelines)
		{
			delete It.Value;
		}
		for (auto& It : PartialPipelines)
		{
			delete It.Value;
		}
	}

	void Trim(int32 TargetNumEntries)
	{
		FScopeLock ScopeLock(&CriticalSection);

		// Only full pipeline cache is automatically trimmed.
		FPipelineMap& Cache = FullPipelines;

		if (Cache.Num() < TargetNumEntries)
		{
			return;
		}

		struct FEntry
		{
			FRayTracingPipelineStateSignature Key;
			uint64 LastFrameHit;
			uint64 HitsAcrossFrames;
			FRayTracingPipelineState* Pipeline;
		};
		TArray<FEntry, TMemStackAllocator<>> Entries;
		Entries.Reserve(Cache.Num());

		const uint64 CurrentFrame = GFrameCounter;
		const uint32 NumLatencyFrames = 10;

		// Find all pipelines that were not used in the last 10 frames

		for (const auto& It : Cache)
		{
			if (It.Value->LastFrameHit + NumLatencyFrames <= CurrentFrame
				&& It.Value->IsCompilationComplete())
			{
				FEntry Entry;
				Entry.Key = It.Key;
				Entry.HitsAcrossFrames = It.Value->HitsAcrossFrames;
				Entry.LastFrameHit = It.Value->LastFrameHit;
				Entry.Pipeline = It.Value;
				Entries.Add(Entry);
			}
		}

		Entries.Sort([](const FEntry& A, const FEntry& B)
		{
			if (A.LastFrameHit == B.LastFrameHit)
			{
				return B.HitsAcrossFrames < A.HitsAcrossFrames;
			}
			else
			{
				return B.LastFrameHit < A.LastFrameHit;
			}
		});

		// Remove least useful pipelines

		while (Cache.Num() > TargetNumEntries && Entries.Num())
		{
			FEntry& LastEntry = Entries.Last();
			check(LastEntry.Pipeline->RHIPipeline);
			check(LastEntry.Pipeline->IsCompilationComplete());
			delete LastEntry.Pipeline;
			Cache.Remove(LastEntry.Key);
			Entries.Pop(false);
		}

		LastTrimFrame = CurrentFrame;
	}

	uint64 GetLastTrimFrame() const { return LastTrimFrame; }

private:

	mutable FCriticalSection CriticalSection;
	using FPipelineMap = TMap<FRayTracingPipelineStateSignature, FRayTracingPipelineState*>;
	FPipelineMap FullPipelines;
	FPipelineMap PartialPipelines;
	uint64 LastTrimFrame = 0;
};

FRayTracingPipelineCache GRayTracingPipelineCache;
#endif

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
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
		if (Initializer.BoundShaderState.GeometryShaderRHI)
			Initializer.BoundShaderState.GeometryShaderRHI->AddRef();
#endif
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
		if (Initializer.BoundShaderState.DomainShaderRHI)
			Initializer.BoundShaderState.DomainShaderRHI->AddRef();
		if (Initializer.BoundShaderState.HullShaderRHI)
			Initializer.BoundShaderState.HullShaderRHI->AddRef();
#endif
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
			if (!Initializer.BoundShaderState.VertexShaderRHI)
			{
				UE_LOG(LogRHI, Fatal, TEXT("Tried to create a Gfx Pipeline State without Vertex Shader"));
			}

			FGraphicsPipelineState* GfxPipeline = static_cast<FGraphicsPipelineState*>(Pipeline);
			GfxPipeline->RHIPipeline = RHICreateGraphicsPipelineState(Initializer);
			
			if(!GfxPipeline->RHIPipeline)
			{
				HandlePipelineCreationFailure(Initializer);
			}
			
			if (Initializer.BoundShaderState.VertexDeclarationRHI)
				Initializer.BoundShaderState.VertexDeclarationRHI->Release();
			if (Initializer.BoundShaderState.VertexShaderRHI)
				Initializer.BoundShaderState.VertexShaderRHI->Release();
			if (Initializer.BoundShaderState.PixelShaderRHI)
				Initializer.BoundShaderState.PixelShaderRHI->Release();
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
			if (Initializer.BoundShaderState.GeometryShaderRHI)
				Initializer.BoundShaderState.GeometryShaderRHI->Release();
#endif
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
			if (Initializer.BoundShaderState.DomainShaderRHI)
				Initializer.BoundShaderState.DomainShaderRHI->Release();
			if (Initializer.BoundShaderState.HullShaderRHI)
				Initializer.BoundShaderState.HullShaderRHI->Release();
#endif
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
		// On Mac the compilation is handled using external processes, so engine threads have very little work to do
		// and it's better to leave more CPU time to these extrenal processes and other engine threads.
		return PLATFORM_MAC ? ENamedThreads::AnyBackgroundThreadNormalTask : CPrio_FCompilePipelineStateTask.Get();
	}
};

/**
* Called at the end of each frame during the RHI . Evicts all items left in the backfill cached based on time
*/
void PipelineStateCache::FlushResources()
{
	check(IsInRenderingThread());

	GGraphicsPipelineCache.ConsolidateThreadedCaches();
	GGraphicsPipelineCache.ProcessDelayedCleanup();


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
	return !IsOpenGLPlatform(GMaxRHIShaderPlatform) &&  // The PSO cache is a waste of time on OpenGL and async compilation is a double waste of time.
		!IsSwitchPlatform(GMaxRHIShaderPlatform) &&
		GCVarAsyncPipelineCompile.GetValueOnAnyThread() && !RHICmdList.Bypass() && (IsRunningRHIInSeparateThread() && !IsInRHIThread()) && RHICmdList.AsyncPSOCompileAllowed();
}

FComputePipelineState* PipelineStateCache::GetAndOrCreateComputePipelineState(FRHICommandList& RHICmdList, FRHIComputeShader* ComputeShader)
{	
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
			RHICmdList.AddDispatchPrerequisite(OutCachedState->CompletionEvent);
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
				RHICmdList.AddDispatchPrerequisite(CompletionEvent);
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

#if RHI_RAYTRACING

class FCompileRayTracingPipelineStateTask
{
public:

	UE_NONCOPYABLE(FCompileRayTracingPipelineStateTask)

	FPipelineState* Pipeline;

	FRayTracingPipelineStateInitializer Initializer;
	const bool bBackgroundTask;

	FCompileRayTracingPipelineStateTask(FPipelineState* InPipeline, const FRayTracingPipelineStateInitializer& InInitializer, bool bInBackgroundTask)
		: Pipeline(InPipeline)
		, Initializer(InInitializer)
		, bBackgroundTask(bInBackgroundTask)
	{
		// Copy all referenced shaders and AddRef them while the task is alive

		RayGenTable   = CopyShaderTable(InInitializer.GetRayGenTable());
		MissTable     = CopyShaderTable(InInitializer.GetMissTable());
		HitGroupTable = CopyShaderTable(InInitializer.GetHitGroupTable());
		CallableTable = CopyShaderTable(InInitializer.GetCallableTable());

		// Point initializer to shader tables owned by this task

		Initializer.SetRayGenShaderTable(RayGenTable, InInitializer.GetRayGenHash());
		Initializer.SetMissShaderTable(MissTable, InInitializer.GetRayMissHash());
		Initializer.SetHitGroupTable(HitGroupTable, InInitializer.GetHitGroupHash());
		Initializer.SetCallableTable(CallableTable, InInitializer.GetCallableHash());
	}

	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		FRayTracingPipelineState* RayTracingPipeline = static_cast<FRayTracingPipelineState*>(Pipeline);
		check(!RayTracingPipeline->RHIPipeline.IsValid());
		RayTracingPipeline->RHIPipeline = RHICreateRayTracingPipelineState(Initializer);

		// References to shaders no longer need to be held by this task

		ReleaseShaders(CallableTable);
		ReleaseShaders(HitGroupTable);
		ReleaseShaders(MissTable);
		ReleaseShaders(RayGenTable);

		Initializer = FRayTracingPipelineStateInitializer();
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FCompileRayTracingPipelineStateTask, STATGROUP_TaskGraphTasks);
	}

	ENamedThreads::Type GetDesiredThread()
	{
		// NOTE: RT PSO compilation internally spawns high-priority shader compilation tasks and waits on them.
		// FCompileRayTracingPipelineStateTask itself must run at lower priority to prevent deadlocks when
		// there are multiple RTPSO tasks that all wait on compilation via WaitUntilTasksComplete().
		return bBackgroundTask ? ENamedThreads::AnyBackgroundThreadNormalTask : ENamedThreads::AnyNormalThreadNormalTask;
	}

private:

	void AddRefShaders(TArray<FRHIRayTracingShader*>& ShaderTable)
	{
		for (FRHIRayTracingShader* Ptr : ShaderTable)
		{
			Ptr->AddRef();
		}
	}

	void ReleaseShaders(TArray<FRHIRayTracingShader*>& ShaderTable)
	{
		for (FRHIRayTracingShader* Ptr : ShaderTable)
		{
			Ptr->Release();
		}
	}

	TArray<FRHIRayTracingShader*> CopyShaderTable(const TArrayView<FRHIRayTracingShader*>& Source)
	{
		TArray<FRHIRayTracingShader*> Result(Source.GetData(), Source.Num());
		AddRefShaders(Result);
		return Result;
	}

	TArray<FRHIRayTracingShader*> RayGenTable;
	TArray<FRHIRayTracingShader*> MissTable;
	TArray<FRHIRayTracingShader*> HitGroupTable;
	TArray<FRHIRayTracingShader*> CallableTable;
};
#endif // RHI_RAYTRACING

FRayTracingPipelineState* PipelineStateCache::GetAndOrCreateRayTracingPipelineState(
	FRHICommandList& RHICmdList,
	const FRayTracingPipelineStateInitializer& InInitializer,
	ERayTracingPipelineCacheFlags Flags)
{
#if RHI_RAYTRACING
	LLM_SCOPE(ELLMTag::PSO);

	check(IsInRenderingThread() || IsInParallelRenderingThread());

	const bool bDoAsyncCompile = IsAsyncCompilationAllowed(RHICmdList);
	const bool bNonBlocking = !!(Flags & ERayTracingPipelineCacheFlags::NonBlocking);

	FRayTracingPipelineState* Result = nullptr;

	bool bWasFound = GRayTracingPipelineCache.Find(InInitializer, Result);

	if (bWasFound)
	{
		if (!Result->IsCompilationComplete())
		{
			if (!bDoAsyncCompile)
			{
				// Pipeline is in cache, but compilation is not finished and async compilation is disallowed, so block here RHI pipeline is created.
				Result->WaitCompletion();
			}
			else if (bNonBlocking)
			{
				// Pipeline is in cache, but compilation has not finished yet, so it can't be used for rendering.
				// Caller must use a fallback pipeline now and try again next frame.
				Result = nullptr;
			}
			else
			{
				// Pipeline is in cache, but compilation is not finished and caller requested blocking mode.
				// RHI command list can't begin translation until this event is complete.
				RHICmdList.AddDispatchPrerequisite(Result->CompletionEvent);
			}
		}
		else
		{
			checkf(Result->RHIPipeline.IsValid(), TEXT("If pipeline is in cache and it doesn't have a completion event, then RHI pipeline is expected to be ready"));
		}
	}
	else
	{
		FPipelineFileCache::CacheRayTracingPSO(InInitializer);

		// Copy the initializer as we may want to patch it below
		FRayTracingPipelineStateInitializer Initializer = InInitializer;

		// If explicit base pipeline is not provided then find a compatible one from the cache
		if (GRHISupportsRayTracingPSOAdditions && InInitializer.BasePipeline == nullptr)
		{
			FRayTracingPipelineState* BasePipeline = nullptr;
			bool bBasePipelineFound = GRayTracingPipelineCache.FindBase(Initializer, BasePipeline);
			if (bBasePipelineFound)
			{
				Initializer.BasePipeline = BasePipeline->RHIPipeline;
			}
		}

		// Remove old pipelines once per frame
		const int32 TargetCacheSize = CVarRTPSOCacheSize.GetValueOnAnyThread();
		if (TargetCacheSize > 0 && GRayTracingPipelineCache.GetLastTrimFrame() != GFrameCounter)
		{
			GRayTracingPipelineCache.Trim(TargetCacheSize);
		}

		Result = GRayTracingPipelineCache.Add(Initializer);

		if (bDoAsyncCompile)
		{
			Result->CompletionEvent = TGraphTask<FCompileRayTracingPipelineStateTask>::CreateTask().ConstructAndDispatchWhenReady(
				Result,
				Initializer,
				bNonBlocking);

			// Partial or non-blocking pipelines can't be used for rendering, therefore this command list does not need to depend on them.

			if (bNonBlocking)
			{
				Result = nullptr;
			}
			else if (!Initializer.bPartial)
			{
				RHICmdList.AddDispatchPrerequisite(Result->CompletionEvent);
			}
		}
		else
		{
			Result->RHIPipeline = RHICreateRayTracingPipelineState(Initializer);
		}
	}

	if (Result)
	{
		Result->AddHit();
	}

	return Result;

#else // RHI_RAYTRACING
	return nullptr;
#endif // RHI_RAYTRACING
}

FRayTracingPipelineState* PipelineStateCache::GetRayTracingPipelineState(const FRayTracingPipelineStateSignature& Signature)
{
#if RHI_RAYTRACING
	FRayTracingPipelineState* Result = nullptr;
	bool bWasFound = GRayTracingPipelineCache.FindBySignature(Signature, Result);
	if (bWasFound)
	{
		Result->AddHit();
	}
	return Result;
#else // RHI_RAYTRACING
	return nullptr;
#endif // RHI_RAYTRACING
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
				if (AnyFailed)
				{
					AnyFailed |= i << 24;
					break;
				}
			}
		}

		AnyFailed |= (NewInitializer.DepthStencilTargetFormat != OriginalInitializer.DepthStencilTargetFormat) << 3;
		AnyFailed |= (NewInitializer.DepthStencilTargetFlag != OriginalInitializer.DepthStencilTargetFlag) << 4;
		AnyFailed |= (NewInitializer.DepthTargetLoadAction != OriginalInitializer.DepthTargetLoadAction) << 5;
		AnyFailed |= (NewInitializer.DepthTargetStoreAction != OriginalInitializer.DepthTargetStoreAction) << 6;
		AnyFailed |= (NewInitializer.StencilTargetLoadAction != OriginalInitializer.StencilTargetLoadAction) << 7;
		AnyFailed |= (NewInitializer.StencilTargetStoreAction != OriginalInitializer.StencilTargetStoreAction) << 8;

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
			RHICmdList.AddDispatchPrerequisite(OutCachedState->CompletionEvent);
		}
		else
		{
			OutCachedState->RHIPipeline = RHICreateGraphicsPipelineState(*Initializer);
			if(!OutCachedState->RHIPipeline)
			{
				HandlePipelineCreationFailure(*Initializer);
			}
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
				RHICmdList.AddDispatchPrerequisite(CompletionEvent);
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

/** Global cache of vertex declarations. Note we don't store TRefCountPtrs, instead we AddRef() manually. */
static TMap<uint32, FRHIVertexDeclaration*> GVertexDeclarationCache;
static FCriticalSection GVertexDeclarationLock;

void PipelineStateCache::Shutdown()
{
	GGraphicsPipelineCache.WaitTasksComplete();
#if RHI_RAYTRACING
	GRayTracingPipelineCache.Shutdown();
#endif

	// call discard twice to clear both the backing and main caches
	for (int i = 0; i < 2; i++)
	{
		GComputePipelineCache.Discard([](FComputePipelineState* CacheItem)
		{
			if(CacheItem != nullptr)
			{
				CacheItem->WaitCompletion();
				delete CacheItem;
			}
		});
		
		GGraphicsPipelineCache.DiscardAndSwap();
	}
	FPipelineFileCache::Shutdown();

	for (auto Pair : GVertexDeclarationCache)
	{
		Pair.Value->Release();
	}
	GVertexDeclarationCache.Empty();
}

FRHIVertexDeclaration*	PipelineStateCache::GetOrCreateVertexDeclaration(const FVertexDeclarationElementList& Elements)
{
	// Actual locking/contention time should be close to unmeasurable
	FScopeLock ScopeLock(&GVertexDeclarationLock);
	uint32 Key = FCrc::MemCrc_DEPRECATED(Elements.GetData(), Elements.Num() * sizeof(FVertexElement));
	FRHIVertexDeclaration** Found = GVertexDeclarationCache.Find(Key);
	if (Found)
	{
		return *Found;
	}

	FVertexDeclarationRHIRef NewDeclaration = RHICreateVertexDeclaration(Elements);

	// Add an extra reference so we don't have TRefCountPtr in the maps
	NewDeclaration->AddRef();
	GVertexDeclarationCache.Add(Key, NewDeclaration);
	return NewDeclaration;
}
