// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetalRHIPrivate.h"
#include "MetalCommandQueue.h"
#include "GPUProfiler.h"

#if METAL_STATISTICS
#include "NotForLicensees/MetalStatistics.h"
#endif

// Stats
DECLARE_CYCLE_STAT_EXTERN(TEXT("MakeDrawable time"),STAT_MetalMakeDrawableTime,STATGROUP_MetalRHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Draw call time"),STAT_MetalDrawCallTime,STATGROUP_MetalRHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("PrepareDraw time"),STAT_MetalPrepareDrawTime,STATGROUP_MetalRHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("PipelineState time"),STAT_MetalPipelineStateTime,STATGROUP_MetalRHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Buffer Page-Off time"), STAT_MetalBufferPageOffTime, STATGROUP_MetalRHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Texture Page-Off time"), STAT_MetalTexturePageOffTime, STATGROUP_MetalRHI, );

DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Uniform Memory Allocated Per-Frame"), STAT_MetalUniformMemAlloc, STATGROUP_MetalRHI, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Uniform Memory Freed Per-Frame"), STAT_MetalUniformMemFreed, STATGROUP_MetalRHI, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Vertex Memory Allocated Per-Frame"), STAT_MetalVertexMemAlloc, STATGROUP_MetalRHI, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Vertex Memory Freed Per-Frame"), STAT_MetalVertexMemFreed, STATGROUP_MetalRHI, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Index Memory Allocated Per-Frame"), STAT_MetalIndexMemAlloc, STATGROUP_MetalRHI, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Index Memory Freed Per-Frame"), STAT_MetalIndexMemFreed, STATGROUP_MetalRHI, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Texture Memory Updated Per-Frame"), STAT_MetalTextureMemUpdate, STATGROUP_MetalRHI, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Buffer Memory"), STAT_MetalBufferMemory, STATGROUP_MetalRHI, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Texture Memory"), STAT_MetalTextureMemory, STATGROUP_MetalRHI, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Unused Buffer Memory"), STAT_MetalBufferUnusedMemory, STATGROUP_MetalRHI, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Unused Texture Memory"), STAT_MetalTextureUnusedMemory, STATGROUP_MetalRHI, );

DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Buffer Count"), STAT_MetalBufferCount, STATGROUP_MetalRHI, );
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Texture Count"), STAT_MetalTextureCount, STATGROUP_MetalRHI, );

DECLARE_CYCLE_STAT_EXTERN(TEXT("Texture Page-On time"), STAT_MetalTexturePageOnTime, STATGROUP_MetalRHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("GPU Work time"), STAT_MetalGPUWorkTime, STATGROUP_MetalRHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("GPU Idle time"), STAT_MetalGPUIdleTime, STATGROUP_MetalRHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Present time"), STAT_MetalPresentTime, STATGROUP_MetalRHI, );
#if STATS
extern int64 volatile GMetalTexturePageOnTime;
extern int64 volatile GMetalGPUWorkTime;
extern int64 volatile GMetalGPUIdleTime;
extern int64 volatile GMetalPresentTime;
#endif

DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Number Command Buffers Created Per-Frame"), STAT_MetalCommandBufferCreatedPerFrame, STATGROUP_MetalRHI, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Number Command Buffers Committed Per-Frame"), STAT_MetalCommandBufferCommittedPerFrame, STATGROUP_MetalRHI, );

/** A single perf event node, which tracks information about a appBeginDrawEvent/appEndDrawEvent range. */
class FMetalEventNode : public FGPUProfilerEventNode
{
public:
	
	FMetalEventNode(FMetalContext* InContext, const TCHAR* InName, FGPUProfilerEventNode* InParent, bool bIsRoot, bool bInFullProfiling)
	: FGPUProfilerEventNode(InName, InParent)
	, StartTime(0)
	, EndTime(0)
	, Context(InContext)
	, bRoot(bIsRoot)
    , bFullProfiling(bInFullProfiling)
	{
	}
	
	virtual ~FMetalEventNode();
	
	/**
	 * Returns the time in ms that the GPU spent in this draw event.
	 * This blocks the CPU if necessary, so can cause hitching.
	 */
	virtual float GetTiming() override;
	
	virtual void StartTiming() override;
	
	virtual void StopTiming() override;
	
	mtlpp::CommandBufferHandler Start(void);
	mtlpp::CommandBufferHandler Stop(void);

	bool Wait() const { return bRoot && bFullProfiling; }
	bool IsRoot() const { return bRoot; }
	
	uint64 GetCycles() { return EndTime - StartTime; }
	
	uint64 StartTime;
	uint64 EndTime;
private:
	FMetalContext* Context;
	bool bRoot;
    bool bFullProfiling;
};

/** An entire frame of perf event nodes, including ancillary timers. */
class FMetalEventNodeFrame : public FGPUProfilerEventNodeFrame
{
public:
	FMetalEventNodeFrame(FMetalContext* InContext, bool bInFullProfiling)
	: RootNode(new FMetalEventNode(InContext, TEXT("Frame"), nullptr, true, bInFullProfiling))
    , bFullProfiling(bInFullProfiling)
	{
	}
	
	virtual ~FMetalEventNodeFrame()
	{
        if(bFullProfiling)
        {
            delete RootNode;
        }
	}
	
	/** Start this frame of per tracking */
	virtual void StartFrame() override;
	
	/** End this frame of per tracking, but do not block yet */
	virtual void EndFrame() override;
	
	/** Calculates root timing base frequency (if needed by this RHI) */
	virtual float GetRootTimingResults() override;
	
	virtual void LogDisjointQuery() override;
	
	FMetalEventNode* RootNode;
    bool bFullProfiling;
};

// This class has multiple inheritance but really FGPUTiming is a static class
class FMetalGPUTiming : public FGPUTiming
{
public:
	
	/**
	 * Constructor.
	 */
	FMetalGPUTiming()
	{
		StaticInitialize(nullptr, PlatformStaticInitialize);
	}
	
	void SetCalibrationTimestamp(uint64 GPU, uint64 CPU)
	{
		GCalibrationTimestamp.GPUMicroseconds = GPU;
		GCalibrationTimestamp.CPUMicroseconds = CPU;
	}
	
private:
	
	/**
	 * Initializes the static variables, if necessary.
	 */
	static void PlatformStaticInitialize(void* UserData)
	{
		// Are the static variables initialized?
		if ( !GAreGlobalsInitialized )
		{
			GIsSupported = true;
			GTimingFrequency = 1000 * 1000 * 1000;
			GAreGlobalsInitialized = true;
		}
	}
};

struct IMetalStatsScope
{
	FString Name;
	TArray<IMetalStatsScope*> Children;
	
	uint64 CPUStartTime;
	uint64 CPUEndTime;
	
	uint64 GPUStartTime;
	uint64 GPUEndTime;
	
	uint64 CPUThreadIndex;
	uint64 GPUThreadIndex;
	
	virtual ~IMetalStatsScope();
	
	virtual void Start(mtlpp::CommandBuffer const& Buffer) = 0;
	virtual void End(mtlpp::CommandBuffer const& Buffer) = 0;
#if METAL_STATISTICS
	virtual void GetStats(FMetalPipelineStats& PipelineStats) = 0;
#endif
	
	FString GetJSONRepresentation(uint32 PID);
};

struct FMetalCPUStats : public IMetalStatsScope
{
	FMetalCPUStats(FString const& Name);
	virtual ~FMetalCPUStats();
	
	void Start(void);
	void End(void);
	
	virtual void Start(mtlpp::CommandBuffer const& Buffer) final override;
	virtual void End(mtlpp::CommandBuffer const& Buffer) final override;
#if METAL_STATISTICS
	virtual void GetStats(FMetalPipelineStats& PipelineStats) final override;
#endif
};

struct FMetalDisplayStats : public IMetalStatsScope
{
	FMetalDisplayStats(uint32 DisplayID, double OutputSeconds, double Duration);
	virtual ~FMetalDisplayStats();
	
	virtual void Start(mtlpp::CommandBuffer const& Buffer) final override;
	virtual void End(mtlpp::CommandBuffer const& Buffer) final override;
#if METAL_STATISTICS
	virtual void GetStats(FMetalPipelineStats& PipelineStats) final override;
#endif
};

#if METAL_STATISTICS
struct FMetalEventStats : public IMetalStatsScope
{
	FMetalEventStats(const TCHAR* Name, FColor Color);
	virtual ~FMetalEventStats();
	
	virtual void Start(mtlpp::CommandBuffer const& Buffer) final override;
	virtual void End(mtlpp::CommandBuffer const& Buffer) final override;
	virtual void GetStats(FMetalPipelineStats& PipelineStats) final override;
	
	id<IMetalStatisticsSamples> StartSample;
	id<IMetalStatisticsSamples> EndSample;
	
	TMap<FString, float> DriverStats;
};

struct FMetalOperationStats : public IMetalStatsScope
{
	FMetalOperationStats(char const* DrawCall, uint64 GPUThreadIndex, uint32 StartPoint, uint32 EndPoint, uint32 RHIPrimitives, uint32 RHIVertices, uint32 RHIInstances);
	FMetalOperationStats(char const* DrawCall, uint64 GPUThreadIndex, uint32 StartPoint, uint32 EndPoint);
	virtual ~FMetalOperationStats();
	
	virtual void Start(mtlpp::CommandBuffer const& Buffer) final override;
	virtual void End(mtlpp::CommandBuffer const& Buffer) final override;
	virtual void GetStats(FMetalPipelineStats& PipelineStats) final override;
	
	id<IMetalCommandBufferStats> CmdBufferStats;
	uint32 StartPoint;
	uint32 EndPoint;
	IMetalDrawStats* DrawStats;
	uint32 RHIPrimitives;
	uint32 RHIVertices;
	uint32 RHIInstances;
};

struct FMetalShaderPipelineStats : public IMetalStatsScope
{
	FMetalShaderPipelineStats(FMetalShaderPipeline* PipelineStat, uint64 GPUThreadIndex);
	virtual ~FMetalShaderPipelineStats();
	
	virtual void Start(mtlpp::CommandBuffer const& Buffer) final override;
	virtual void End(mtlpp::CommandBuffer const& Buffer) final override;
	virtual void GetStats(FMetalPipelineStats& PipelineStats) final override;
	
	id<IMetalCommandBufferStats> CmdBufferStats;
	id<IMetalStatisticsSamples> StartSample;
	FMetalShaderPipeline* Pipeline;
};

struct FMetalEncoderStats : public IMetalStatsScope
{
	FMetalEncoderStats(mtlpp::RenderCommandEncoder const& Encoder, uint64 GPUThreadIndex);
	FMetalEncoderStats(mtlpp::BlitCommandEncoder const& Encoder, uint64 GPUThreadIndex);
	FMetalEncoderStats(mtlpp::ComputeCommandEncoder const& Encoder, uint64 GPUThreadIndex);
	virtual ~FMetalEncoderStats();
	
	virtual void Start(mtlpp::CommandBuffer const& Buffer) final override;
	virtual void End(mtlpp::CommandBuffer const& Buffer) final override;
	virtual void GetStats(FMetalPipelineStats& PipelineStats) final override;
	
	void EncodeDraw(char const* DrawCall, uint32 RHIPrimitives, uint32 RHIVertices, uint32 RHIInstances);
	void EncodeBlit(char const* DrawCall);
	void EncodeDispatch(char const* DrawCall);
	void EncodePipeline(FMetalShaderPipeline* PipelineStat);
	
	id<IMetalCommandBufferStats> CmdBufferStats;
	ns::AutoReleased<mtlpp::CommandBuffer> CmdBuffer;
	uint32 StartPoint;
	uint32 EndPoint;
	id<IMetalStatisticsSamples> StartSample;
	id<IMetalStatisticsSamples> EndSample;
};
#endif

struct FMetalCommandBufferStats : public IMetalStatsScope
{
	FMetalCommandBufferStats(mtlpp::CommandBuffer const& Buffer, uint64 GPUThreadIndex);
	virtual ~FMetalCommandBufferStats();
	
	virtual void Start(mtlpp::CommandBuffer const& Buffer) final override;
	virtual void End(mtlpp::CommandBuffer const& Buffer) final override;
#if METAL_STATISTICS
	virtual void GetStats(FMetalPipelineStats& PipelineStats) final override;
#endif

	ns::AutoReleased<mtlpp::CommandBuffer> CmdBuffer;

#if METAL_STATISTICS
	void BeginEncoder(mtlpp::RenderCommandEncoder const& Encoder);
	void BeginEncoder(mtlpp::BlitCommandEncoder const& Encoder);
	void BeginEncoder(mtlpp::ComputeCommandEncoder const& Encoder);
	
	void EndEncoder(mtlpp::RenderCommandEncoder const& Encoder);
	void EndEncoder(mtlpp::BlitCommandEncoder const& Encoder);
	void EndEncoder(mtlpp::ComputeCommandEncoder const& Encoder);

	id<IMetalCommandBufferStats> CmdBufferStats;
	FMetalEncoderStats* ActiveEncoderStats;
#endif
};

/**
 * Encapsulates GPU profiling logic and data.
 * There's only one global instance of this struct so it should only contain global data, nothing specific to a frame.
 */
struct FMetalGPUProfiler : public FGPUProfiler
{
	/** GPU hitch profile histories */
	TIndirectArray<FMetalEventNodeFrame> GPUHitchEventNodeFrames;
	
	FMetalGPUProfiler(FMetalContext* InContext)
	:	FGPUProfiler()
	,	Context(InContext)
	,   NumNestedFrames(0)
	{
		FMemory::Memzero((void*)&FrameStartGPU[0], sizeof(FrameStartGPU));
		FMemory::Memzero((void*)&FrameEndGPU[0], sizeof(FrameEndGPU));
		FMemory::Memzero((void*)&FrameGPUTime[0], sizeof(FrameGPUTime));
		FMemory::Memzero((void*)&FrameIdleTime[0], sizeof(FrameIdleTime));
		FMemory::Memzero((void*)&FramePresentTime[0], sizeof(FramePresentTime));
	}
	
	virtual ~FMetalGPUProfiler() {}
	
	virtual FGPUProfilerEventNode* CreateEventNode(const TCHAR* InName, FGPUProfilerEventNode* InParent) override;
	
	void Cleanup();
	
	virtual void PushEvent(const TCHAR* Name, FColor Color) override;
	virtual void PopEvent() override;
	
	void BeginFrame();
	void EndFrame();
	
	static void IncrementFrameIndex();
	static void RecordFrame(mtlpp::CommandBuffer& Buffer);
	static void RecordPresent(mtlpp::CommandBuffer& Buffer);
	static void RecordCommandBuffer(mtlpp::CommandBuffer& Buffer);
	
#define MAX_FRAME_HISTORY 3
	static volatile int32 FrameTimeGPUIndex;
	static volatile int64 FrameStartGPU[MAX_FRAME_HISTORY];
	static volatile int64 FrameEndGPU[MAX_FRAME_HISTORY];
	static volatile int64 FrameGPUTime[MAX_FRAME_HISTORY];
	static volatile int64 FrameIdleTime[MAX_FRAME_HISTORY];
	static volatile int64 FramePresentTime[MAX_FRAME_HISTORY];
	
	FMetalGPUTiming TimingSupport;
	FMetalContext* Context;
	int32 NumNestedFrames;
};

class FMetalProfiler : public FMetalGPUProfiler
{
	static FMetalProfiler* Self;
public:
	FMetalProfiler(FMetalContext* InContext);
	~FMetalProfiler();
	
	static FMetalProfiler* CreateProfiler(FMetalContext* InContext);
	static FMetalProfiler* GetProfiler();
#if METAL_STATISTICS
	static IMetalStatistics* GetStatistics();
#endif
	static void DestroyProfiler();
	
	void BeginCapture(int InNumFramesToCapture = -1);
	void EndCapture();
	bool TracingEnabled() const;
	
	void BeginFrame();
	void EndFrame();
	
	void AddDisplayVBlank(uint32 DisplayID, double OutputSeconds, double OutputDuration);
	
	void EncodeDraw(FMetalCommandBufferStats* CmdBufStats, char const* DrawCall, uint32 RHIPrimitives, uint32 RHIVertices, uint32 RHIInstances);
	void EncodeBlit(FMetalCommandBufferStats* CmdBufStats, char const* DrawCall);
	void EncodeDispatch(FMetalCommandBufferStats* CmdBufStats, char const* DrawCall);
	
#if METAL_STATISTICS
	void EncodePipeline(FMetalCommandBufferStats* CmdBufStats, FMetalShaderPipeline* PipelineStat);
	
	void BeginEncoder(FMetalCommandBufferStats* CmdBufStats, mtlpp::RenderCommandEncoder const& Encoder);
	void BeginEncoder(FMetalCommandBufferStats* CmdBufStats, mtlpp::BlitCommandEncoder const& Encoder);
	void BeginEncoder(FMetalCommandBufferStats* CmdBufStats, mtlpp::ComputeCommandEncoder const& Encoder);
	
	void EndEncoder(FMetalCommandBufferStats* CmdBufStats, mtlpp::RenderCommandEncoder const& Encoder);
	void EndEncoder(FMetalCommandBufferStats* CmdBufStats, mtlpp::BlitCommandEncoder const& Encoder);
	void EndEncoder(FMetalCommandBufferStats* CmdBufStats, mtlpp::ComputeCommandEncoder const& Encoder);
	
	enum EMTLCounterType
	{
		EMTLCounterTypeStartEnd,
		EMTLCounterTypeLast,
		EMTLCounterTypeDifference
	};
	
	void AddCounter(NSString* Counter, EMTLCounterType Type);
	void RemoveCounter(NSString* Counter);
	TMap<FString, EMTLCounterType> const& GetCounterTypes() const { return CounterTypes; }
	
	void DumpPipeline(FMetalShaderPipeline* PipelineStat);
#endif
	
	FMetalCPUStats* AddCPUStat(FString const& Name);
	FMetalCommandBufferStats* AllocateCommandBuffer(mtlpp::CommandBuffer const& Buffer, uint64 GPUThreadIndex);
	void AddCommandBuffer(FMetalCommandBufferStats* CommandBuffer);
	virtual void PushEvent(const TCHAR* Name, FColor Color) final override;
	virtual void PopEvent() final override;
	
	void SaveTrace();
	
private:
	FCriticalSection Mutex;
#if METAL_STATISTICS
	NSMutableArray* NewCounters;
	TMap<FString, EMTLCounterType> CounterTypes;
	IMetalStatistics* StatisticsAPI;
	TArray<FMetalEventStats*> FrameEvents;
	TArray<FMetalEventStats*> ActiveEvents;
	TSet<FMetalShaderPipeline*> Pipelines;
#endif
	
	TArray<FMetalCommandBufferStats*> TracedBuffers;
	TArray<FMetalDisplayStats*> DisplayStats;
	TArray<FMetalCPUStats*> CPUStats;
	
	int32 NumFramesToCapture;
	int32 CaptureFrameNumber;
	
	bool bRequestStartCapture;
	bool bRequestStopCapture;
	bool bEnabled;
};

struct FScopedMetalCPUStats
{
	FScopedMetalCPUStats(FString const& Name)
	: Stats(nullptr)
	{
		FMetalProfiler* Profiler = FMetalProfiler::GetProfiler();
		if (Profiler)
		{
			Stats = Profiler->AddCPUStat(Name);
			if (Stats)
			{
				Stats->Start();
			}
		}
	}
	
	~FScopedMetalCPUStats()
	{
		if (Stats)
		{
			Stats->End();
		}
	}
	
	FMetalCPUStats* Stats;
};
