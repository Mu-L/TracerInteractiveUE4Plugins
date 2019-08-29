// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "SceneUtils.h"
#include "CsvProfiler.h"

DEFINE_LOG_CATEGORY_STATIC(LogSceneUtils,All,All);

// Only exposed for debugging. Disabling this carries a severe performance penalty
#define RENDER_QUERY_POOLING_ENABLED 1

#if HAS_GPU_STATS 

// If this is enabled, the child stat timings will be included in their parents' times.
// This presents problems for non-hierarchical stats if we're expecting them to add up
// to the total GPU time, so we probably want this disabled
#define GPU_STATS_CHILD_TIMES_INCLUDED 0

CSV_DEFINE_CATEGORY_MODULE(ENGINE_API, GPU, true);

static TAutoConsoleVariable<int> CVarGPUStatsEnabled(
	TEXT("r.GPUStatsEnabled"),
	1,
	TEXT("Enables or disables GPU stat recording"));


static TAutoConsoleVariable<int> CVarGPUStatsMaxQueriesPerFrame(
	TEXT("r.GPUStatsMaxQueriesPerFrame"),
	-1,
	TEXT("Limits the number of timestamps allocated per frame. -1 = no limit"), 
	ECVF_RenderThreadSafe);


static TAutoConsoleVariable<int> CVarGPUCsvStatsEnabled(
	TEXT("r.GPUCsvStatsEnabled"),
	0,
	TEXT("Enables or disables GPU stat recording to CSVs"));

DECLARE_GPU_STAT_NAMED( Total, TEXT("[TOTAL]") );

#endif //HAS_GPU_STATS


#if WANTS_DRAW_MESH_EVENTS

template<typename TRHICmdList>
void TDrawEvent<TRHICmdList>::Start(TRHICmdList& InRHICmdList, FColor Color, const TCHAR* Fmt, ...)
{
	check(IsInParallelRenderingThread() || IsInRHIThread());
	{
		va_list ptr;
		va_start(ptr, Fmt);
		TCHAR TempStr[256];
		// Build the string in the temp buffer
		FCString::GetVarArgs(TempStr, ARRAY_COUNT(TempStr), ARRAY_COUNT(TempStr) - 1, Fmt, ptr);
		InRHICmdList.PushEvent(TempStr, Color);
		RHICmdList = &InRHICmdList;
	}
}

template<typename TRHICmdList>
void TDrawEvent<TRHICmdList>::Stop()
{
	if (RHICmdList)
	{
		RHICmdList->PopEvent();
		RHICmdList = NULL;
	}
}
template struct TDrawEvent<FRHICommandList>;
template struct TDrawEvent<FRHIAsyncComputeCommandList>;

void FDrawEventRHIExecute::Start(IRHIComputeContext& InRHICommandContext, FColor Color, const TCHAR* Fmt, ...)
{
	check(IsInParallelRenderingThread() || IsInRHIThread() || (!IsRunningRHIInSeparateThread() && IsInRenderingThread()));
	{
		va_list ptr;
		va_start(ptr, Fmt);
		TCHAR TempStr[256];
		// Build the string in the temp buffer
		FCString::GetVarArgs(TempStr, ARRAY_COUNT(TempStr), ARRAY_COUNT(TempStr) - 1, Fmt, ptr);
		RHICommandContext = &InRHICommandContext;
		RHICommandContext->RHIPushEvent(TempStr, Color);
	}
}

void FDrawEventRHIExecute::Stop()
{
	RHICommandContext->RHIPopEvent();
}

#endif // WANTS_DRAW_MESH_EVENTS


bool IsMobileHDR()
{
	static auto* MobileHDRCvar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileHDR"));
	return MobileHDRCvar->GetValueOnAnyThread() == 1;
}

bool IsMobileHDR32bpp()
{
	static auto* MobileHDR32bppModeCvar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileHDR32bppMode"));
	return IsMobileHDR() && (GSupportsRenderTargetFormat_PF_FloatRGBA == false || MobileHDR32bppModeCvar->GetValueOnAnyThread() != 0);
}

bool IsMobileHDRMosaic()
{
	if (!IsMobileHDR32bpp())
		return false;

	static auto* MobileHDR32bppMode = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileHDR32bppMode"));
	switch (MobileHDR32bppMode->GetValueOnAnyThread())
	{
		case 1:
			return true;
		case 2:
		case 3:
			return false;
		default:
			return !(GSupportsHDR32bppEncodeModeIntrinsic && GSupportsShaderFramebufferFetch);
	}
}

ENGINE_API EMobileHDRMode GetMobileHDRMode()
{
	EMobileHDRMode HDRMode = EMobileHDRMode::EnabledFloat16;

	if (IsMobileHDR() == false)
	{
		HDRMode = EMobileHDRMode::Disabled;
	}
	
	if (IsMobileHDR32bpp())
	{
		static auto* MobileHDR32bppMode = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileHDR32bppMode"));
		switch (MobileHDR32bppMode->GetValueOnAnyThread())
		{
			case 1:
				HDRMode = EMobileHDRMode::EnabledMosaic;
				break;
			case 2:
				HDRMode = EMobileHDRMode::EnabledRGBE;
				break;
			case 3:
				HDRMode = EMobileHDRMode::EnabledRGBA8;
				break;
			default:
				HDRMode = (GSupportsHDR32bppEncodeModeIntrinsic && GSupportsShaderFramebufferFetch) ? EMobileHDRMode::EnabledRGBE : EMobileHDRMode::EnabledMosaic;
				break;
		}
	}

	return HDRMode;
}

#if HAS_GPU_STATS
static const int32 NumGPUProfilerBufferedFrames = 4;

/*-----------------------------------------------------------------------------
FRealTimeGPUProfilerEvent class
-----------------------------------------------------------------------------*/
class FRealtimeGPUProfilerEvent
{
public:
	static const uint64 InvalidQueryResult = 0xFFFFFFFFFFFFFFFFull;

public:
	FRealtimeGPUProfilerEvent(const FName& InName, const FName& InStatName, FRenderQueryPool* RenderQueryPool)
		: StartResultMicroseconds(InvalidQueryResult)
		, EndResultMicroseconds(InvalidQueryResult)
		, FrameNumber(-1)
		, bInsideQuery(false)
		, bBeginQueryInFlight(false)
		, bEndQueryInFlight(false)
	{
#if STATS
		StatName = InStatName;
#endif
		Name = InName;

		const int MaxGPUQueries = CVarGPUStatsMaxQueriesPerFrame.GetValueOnRenderThread();
		if ( MaxGPUQueries == -1 || RenderQueryPool->GetAllocatedQueryCount() < MaxGPUQueries )
		{
			StartQuery = RenderQueryPool->AllocateQuery();
			EndQuery = RenderQueryPool->AllocateQuery();
		}
	}

	bool HasQueriesAllocated() const 
	{ 
		return IsValidRef(StartQuery); 
	}

	void ReleaseQueries(FRenderQueryPool* RenderQueryPool, FRHICommandListImmediate* RHICmdListPtr)
	{
		if ( HasQueriesAllocated() )
		{
			if (RHICmdListPtr)
			{
				// If we have queries in flight then get results before releasing back to the pool to avoid an ensure fail in the gnm RHI
				uint64 Temp;
				if (bBeginQueryInFlight)
				{
					RHICmdListPtr->GetRenderQueryResult(StartQuery, Temp, false);
				}

				if (bEndQueryInFlight)
				{
					RHICmdListPtr->GetRenderQueryResult(EndQuery, Temp, false);
				}
			}
			RenderQueryPool->ReleaseQuery(StartQuery);
			RenderQueryPool->ReleaseQuery(EndQuery);
		}
	}

	void Begin(FRHICommandListImmediate& RHICmdList)
	{
		check(IsInRenderingThread());
		check(!bInsideQuery);
		bInsideQuery = true;

		if (IsValidRef(StartQuery))
		{
			RHICmdList.EndRenderQuery(StartQuery);
			bBeginQueryInFlight = true;
		}
		StartResultMicroseconds = InvalidQueryResult;
		EndResultMicroseconds = InvalidQueryResult;
		FrameNumber = GFrameNumberRenderThread;
	}

	void End(FRHICommandListImmediate& RHICmdList)
	{
		check(IsInRenderingThread());
		check(bInsideQuery);
		bInsideQuery = false;

		if ( HasQueriesAllocated() )
		{
			RHICmdList.EndRenderQuery(EndQuery);
			bEndQueryInFlight = true;
		}
	}

	bool GatherQueryResults(FRHICommandListImmediate& RHICmdList)
	{
		// Get the query results which are still outstanding
		check(GFrameNumberRenderThread != FrameNumber);
		if ( HasQueriesAllocated() )
		{
			if (StartResultMicroseconds == InvalidQueryResult)
			{
				if (!RHICmdList.GetRenderQueryResult(StartQuery, StartResultMicroseconds, true))
				{
					StartResultMicroseconds = InvalidQueryResult;
				}
				bBeginQueryInFlight = false;
			}
			if (EndResultMicroseconds == InvalidQueryResult)
			{
				if (!RHICmdList.GetRenderQueryResult(EndQuery, EndResultMicroseconds, true))
				{
					EndResultMicroseconds = InvalidQueryResult;
				}
				bEndQueryInFlight = false;
			}
		}
		else
		{
			// If we don't have a query allocated, just set the results to zero
			EndResultMicroseconds = StartResultMicroseconds = 0;
		}
		return HasValidResult();
	}

	float GetResultMS() const
	{
		check(HasValidResult());
		if (EndResultMicroseconds < StartResultMicroseconds)
		{
			// This should never happen...
			return 0.0f;
		}
		return float(EndResultMicroseconds - StartResultMicroseconds) / 1000.0f;
	}

	bool HasValidResult() const
	{
		return StartResultMicroseconds != FRealtimeGPUProfilerEvent::InvalidQueryResult && EndResultMicroseconds != FRealtimeGPUProfilerEvent::InvalidQueryResult;
	}

#if STATS
	const FName& GetStatName() const
	{
		return StatName;
	}
#endif
	const FName& GetName() const
	{
		return Name;
	}

private:
	FRenderQueryRHIRef StartQuery;
	FRenderQueryRHIRef EndQuery;
#if STATS
	FName StatName;
#endif
	FName Name;
	uint64 StartResultMicroseconds;
	uint64 EndResultMicroseconds;
	uint32 FrameNumber;

	bool bInsideQuery;
	bool bBeginQueryInFlight;
	bool bEndQueryInFlight;
};

/*-----------------------------------------------------------------------------
FRealtimeGPUProfilerFrame class
Container for a single frame's GPU stats
-----------------------------------------------------------------------------*/
class FRealtimeGPUProfilerFrame
{
public:
	FRealtimeGPUProfilerFrame(FRenderQueryPool* InRenderQueryPool)
		: FrameNumber(-1)
		, RenderQueryPool(InRenderQueryPool)
	{}

	~FRealtimeGPUProfilerFrame()
	{
		Clear(nullptr);
	}

	void PushEvent(FRHICommandListImmediate& RHICmdList, const FName& Name, const FName& StatName)
	{
#if GPU_STATS_CHILD_TIMES_INCLUDED == 0
		if (EventStack.Num() > 0)
		{
			// GPU Stats are not hierarchical. If we already have an event in the stack, 
			// we need end it and resume it once the child event completes 
			FRealtimeGPUProfilerEvent* ParentEvent = EventStack.Last();
			ParentEvent->End(RHICmdList);
		}
#endif
		FRealtimeGPUProfilerEvent* Event = CreateNewEvent(StatName,Name);
		EventStack.Push(Event);
		Event->Begin(RHICmdList);
	}

	void PopEvent(FRHICommandListImmediate& RHICmdList)
	{
		FRealtimeGPUProfilerEvent* Event = EventStack.Pop();
		Event->End(RHICmdList);

#if GPU_STATS_CHILD_TIMES_INCLUDED == 0
		if (EventStack.Num() > 0)
		{
			// Resume the parent event (requires creation of a new FRealtimeGPUProfilerEvent)
#if STATS
			FName PrevStatName = EventStack.Last()->GetStatName();
#else
			FName PrevStatName = FName();
#endif
			FRealtimeGPUProfilerEvent* ResumedEvent = CreateNewEvent(
				PrevStatName, EventStack.Last()->GetName());
			EventStack.Last() = ResumedEvent;
			ResumedEvent->Begin(RHICmdList);
		}
#endif

	}

	void Clear(FRHICommandListImmediate* RHICommandListPtr )
	{
		EventStack.Empty();

		for (int Index = 0; Index < GpuProfilerEvents.Num(); Index++)
		{
			if (GpuProfilerEvents[Index])
			{
				GpuProfilerEvents[Index]->ReleaseQueries(RenderQueryPool, RHICommandListPtr);
				delete GpuProfilerEvents[Index];
			}
		}
		GpuProfilerEvents.Empty();
	}

	bool UpdateStats(FRHICommandListImmediate& RHICmdList)
	{
		bool bCsvStatsEnabled = !!CVarGPUCsvStatsEnabled.GetValueOnRenderThread();

		// Gather any remaining results and check all the results are ready
		bool bAllQueriesAllocated = true;
		for (int Index = 0; Index < GpuProfilerEvents.Num(); Index++)
		{
			FRealtimeGPUProfilerEvent* Event = GpuProfilerEvents[Index];
			check(Event != nullptr);
			if (!Event->HasValidResult())
			{
				Event->GatherQueryResults(RHICmdList);
			}
			if (!Event->HasValidResult())
			{
				// The frame isn't ready yet. Don't update stats - we'll try again next frame. 
				return false;
			}
			if (!Event->HasQueriesAllocated())
			{
				bAllQueriesAllocated = false;
			}
		}

		if (!bAllQueriesAllocated)
		{
			static bool bWarned = false;

			if (!bWarned)
			{
				bWarned = true;
				UE_LOG(LogSceneUtils, Warning, TEXT("Ran out of GPU queries! Results for this frame will be incomplete"));
			}
		}

		float TotalMS = 0.0f;
		// Update the stats
		TMap<FName, bool> StatSeenMap;
		for (int Index = 0; Index < GpuProfilerEvents.Num(); Index++)
		{
			FRealtimeGPUProfilerEvent* Event = GpuProfilerEvents[Index];
			check(Event != nullptr);
			check(Event->HasValidResult());
			const FName& StatName = Event->GetName();

			// Check if we've seen this stat yet 
			bool bIsNew = true;
			if (StatSeenMap.Find(StatName) == nullptr)
			{
				StatSeenMap.Add(StatName, true);
				bIsNew = false;
			}

			float ResultMS = Event->GetResultMS();
#if STATS
			EStatOperation::Type StatOp = bIsNew ? EStatOperation::Set : EStatOperation::Add;
			FThreadStats::AddMessage(Event->GetStatName(), StatOp, double(ResultMS));
#endif

			if (bCsvStatsEnabled)
			{
				ECsvCustomStatOp CsvStatOp = bIsNew ? ECsvCustomStatOp::Set : ECsvCustomStatOp::Accumulate;
				FCsvProfiler::Get()->RecordCustomStat(Event->GetName(), CSV_CATEGORY_INDEX(GPU), ResultMS, CsvStatOp);
			}
			TotalMS += ResultMS;
		}

#if STATS
		FThreadStats::AddMessage( GET_STATFNAME(Stat_GPU_Total), EStatOperation::Set, double(TotalMS) );
#endif 

#if CSV_PROFILER
		if (bCsvStatsEnabled)
		{
			FCsvProfiler::Get()->RecordCustomStat(CSV_STAT_FNAME(Total), CSV_CATEGORY_INDEX(GPU), TotalMS, ECsvCustomStatOp::Set);
		}
#endif
		return true;
	}

private:
	FRealtimeGPUProfilerEvent* CreateNewEvent(const FName& StatName, const FName& Name)
	{
		FRealtimeGPUProfilerEvent* NewEvent = new FRealtimeGPUProfilerEvent(Name, StatName, RenderQueryPool);
		GpuProfilerEvents.Add(NewEvent);
		return NewEvent;
	}

	TArray<FRealtimeGPUProfilerEvent*> GpuProfilerEvents;
	TArray<FRealtimeGPUProfilerEvent*> EventStack;
	uint32 FrameNumber;
	FRenderQueryPool* RenderQueryPool;
};

/*-----------------------------------------------------------------------------
FRealtimeGPUProfiler
-----------------------------------------------------------------------------*/
FRealtimeGPUProfiler* FRealtimeGPUProfiler::Instance = nullptr;

FRealtimeGPUProfiler* FRealtimeGPUProfiler::Get()
{
	if (Instance == nullptr)
	{
		Instance = new FRealtimeGPUProfiler;
	}
	return Instance;
}

FRealtimeGPUProfiler::FRealtimeGPUProfiler()
	: WriteBufferIndex(0)
	, ReadBufferIndex(1) 
	, WriteFrameNumber(-1)
	, bStatGatheringPaused(false)
	, bInBeginEndBlock(false)
{
	RenderQueryPool = new FRenderQueryPool(RQT_AbsoluteTime);
	for (int Index = 0; Index < NumGPUProfilerBufferedFrames; Index++)
	{
		Frames.Add(new FRealtimeGPUProfilerFrame(RenderQueryPool));
	}
}

void FRealtimeGPUProfiler::Release()
{
	for (int Index = 0; Index < Frames.Num(); Index++)
	{
		delete Frames[Index];
	}
	Frames.Empty();
	delete RenderQueryPool;
	RenderQueryPool = nullptr;
}

void FRealtimeGPUProfiler::BeginFrame(FRHICommandListImmediate& RHICmdList)
{
	check(bInBeginEndBlock == false);
	bInBeginEndBlock = true;
}

inline bool AreGPUStatsEnabled()
{
	if (GSupportsTimestampRenderQueries == false || !CVarGPUStatsEnabled.GetValueOnRenderThread())
	{
		return false;
	}

	// If stats are off, we only enable GPU stats if the CSV profiler is enabled
#if !STATS 
#if !CSV_PROFILER
	return false;
#endif
	// If we only have CSV stats, only capture if CSV GPU stats are enabled, and we're capturing
	if (!CVarGPUCsvStatsEnabled.GetValueOnRenderThread())
	{
		return false;
	}
	if (!FCsvProfiler::Get()->IsCapturing_Renderthread())
	{
		return false;
	}
#endif
	return true;
}

void FRealtimeGPUProfiler::EndFrame(FRHICommandListImmediate& RHICmdList)
{
	// This is called at the end of the renderthread frame. Note that the RHI thread may still be processing commands for the frame at this point, however
	// The read buffer index is always 3 frames beind the write buffer index in order to prevent us reading from the frame the RHI thread is still processing. 
	// This should also ensure the GPU is done with the queries before we try to read them
	check(Frames.Num() > 0);
	check(IsInRenderingThread());
	check(bInBeginEndBlock == true);
	bInBeginEndBlock = false;
	if (!AreGPUStatsEnabled())
	{
		return;
	}

	if (Frames[ReadBufferIndex]->UpdateStats(RHICmdList))
	{
		// On a successful read, advance the ReadBufferIndex and WriteBufferIndex and clear the frame we just read
		Frames[ReadBufferIndex]->Clear(&RHICmdList);
		WriteFrameNumber = GFrameNumberRenderThread;
		WriteBufferIndex = (WriteBufferIndex + 1) % Frames.Num();
		ReadBufferIndex = (ReadBufferIndex + 1) % Frames.Num();
		bStatGatheringPaused = false;
	}
	else
	{
		// The stats weren't ready; skip the next frame and don't advance the indices. We'll try to read the stats again next frame
		bStatGatheringPaused = true;
	}
}

void FRealtimeGPUProfiler::PushEvent(FRHICommandListImmediate& RHICmdList, const FName& Name, const FName& StatName)
{
	check(IsInRenderingThread());
	if (bStatGatheringPaused || !bInBeginEndBlock)
	{
		return;
	}
	check(Frames.Num() > 0);
	if (WriteBufferIndex >= 0)
	{
		Frames[WriteBufferIndex]->PushEvent(RHICmdList, Name, StatName);
	}
}

void FRealtimeGPUProfiler::PopEvent(FRHICommandListImmediate& RHICmdList)
{
	check(IsInRenderingThread());
	if (bStatGatheringPaused || !bInBeginEndBlock)
	{
		return;
	}
	check(Frames.Num() > 0);
	if (WriteBufferIndex >= 0)
	{
		Frames[WriteBufferIndex]->PopEvent(RHICmdList);
	}
}

/*-----------------------------------------------------------------------------
FScopedGPUStatEvent
-----------------------------------------------------------------------------*/
void FScopedGPUStatEvent::Begin(FRHICommandList& InRHICmdList, const FName& Name, const FName& StatName)
{
	check(IsInRenderingThread());
	if (!AreGPUStatsEnabled())
	{
		return;
	}

	// Non-immediate commandlists are not supported (silently fail)
	if (InRHICmdList.IsImmediate())
	{
		RHICmdList = (FRHICommandListImmediate*)&InRHICmdList;
		FRealtimeGPUProfiler::Get()->PushEvent(*RHICmdList, Name, StatName);
	}
}

void FScopedGPUStatEvent::End()
{
	check(IsInRenderingThread());
	if (!AreGPUStatsEnabled())
	{
		return;
	}
	if (RHICmdList != nullptr)
	{
		FRealtimeGPUProfiler::Get()->PopEvent(*RHICmdList);
	}
}
#endif // HAS_GPU_STATS

/*-----------------------------------------------------------------------------
FRenderQueryPool
-----------------------------------------------------------------------------*/
FRenderQueryPool::~FRenderQueryPool()
{
	Release();
}

void FRenderQueryPool::Release()
{
	Queries.Empty();
	NumQueriesAllocated = 0;
}

FRenderQueryRHIRef FRenderQueryPool::AllocateQuery()
{
	// Are we out of available render queries?
	NumQueriesAllocated++;
	if (Queries.Num() == 0)
	{
		// Create a new render query.
		return RHICreateRenderQuery(QueryType);
	}

	return Queries.Pop(/*bAllowShrinking=*/ false);
}

void FRenderQueryPool::ReleaseQuery(FRenderQueryRHIRef &Query)
{
	if (IsValidRef(Query))
	{
		NumQueriesAllocated--;
#if RENDER_QUERY_POOLING_ENABLED
		// Is no one else keeping a refcount to the query?
		if (Query.GetRefCount() == 1)
		{
			// Return it to the pool.
			Queries.Add(Query);
		}
#endif
		// De-ref without deleting.
		Query = NULL;
	}
}
