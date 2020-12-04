// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"

#define DEBUG_FENCES 0

extern int32 GCommandListBatchingMode;
extern int32 GEmitRgpFrameMarkers;

enum ECommandListBatchMode
{
	CLB_NormalBatching = 1,			// Submits work on explicit Flush and at the end of a context container batch
	CLB_AggressiveBatching = 2,		// Submits work on explicit Flush (after Occlusion queries, and before Present) - Least # of submits.
};

enum class CommandListState
{
	kOpen,
	kQueued,
	kFinished
};


struct FD3D12CommandListPayload
{
	FD3D12CommandListPayload() : NumCommandLists(0)
	{
		FMemory::Memzero(CommandLists);
		FMemory::Memzero(ResidencySets);
	}

	void Reset();
	void Append(ID3D12CommandList* CL, FD3D12ResidencySet* Set);

	static const uint32 MaxCommandListsPerPayload = 256;
	ID3D12CommandList* CommandLists[MaxCommandListsPerPayload];
	FD3D12ResidencySet* ResidencySets[MaxCommandListsPerPayload];
	uint32 NumCommandLists;
};

class FD3D12FenceCore : public FD3D12AdapterChild
{
public:
	FD3D12FenceCore(FD3D12Adapter* Parent, uint64 InitialValue, uint32 GPUIndex);
	~FD3D12FenceCore();

	inline ID3D12Fence* GetFence() const { return Fence.GetReference(); }
	inline HANDLE GetCompletionEvent() const { return hFenceCompleteEvent; }
	inline bool IsAvailable() const { return FenceValueAvailableAt <= Fence->GetCompletedValue(); }
	inline uint32 GetGPUIndex() const { return GPUIndex;  }

	uint64 FenceValueAvailableAt;

private:
	uint32 GPUIndex;

	TRefCountPtr<ID3D12Fence> Fence;
	HANDLE hFenceCompleteEvent;
};

class FD3D12FenceCorePool : public FD3D12AdapterChild
{
public:

	FD3D12FenceCorePool(FD3D12Adapter* Parent) : FD3D12AdapterChild(Parent) {};

	FD3D12FenceCore* ObtainFenceCore(uint32 GPUIndex);
	void ReleaseFenceCore(FD3D12FenceCore* Fence, uint64 CurrentFenceValue);
	void Destroy();

private:
	FCriticalSection CS;
	TQueue<FD3D12FenceCore*> AvailableFences[MAX_NUM_GPUS];
};

// Automatically increments the current fence value after Signal.
class FD3D12Fence : public FRefCountedObject, public FD3D12AdapterChild, public FD3D12MultiNodeGPUObject, public FNoncopyable
{
public:
	FD3D12Fence(FD3D12Adapter* InParent, FRHIGPUMask InGPUMask, const FName& InName = L"<unnamed>");
	virtual ~FD3D12Fence();

	virtual void CreateFence();
	virtual uint64 Signal(ED3D12CommandQueueType InQueueType);
	void GpuWait(uint32 DeviceGPUIndex, ED3D12CommandQueueType InQueueType, uint64 FenceValue, uint32 FenceGPUIndex);
	void GpuWait(ED3D12CommandQueueType InQueueType, uint64 FenceValue);
	bool IsFenceComplete(uint64 FenceValue);
	void WaitForFence(uint64 FenceValue);

	// Avoids calling GetCompletedValue().
	bool IsFenceCompleteFast(uint64 FenceValue) const { return FenceValue <= LastCompletedFence; }

	virtual uint64 GetCurrentFence() const { return CurrentFence; }
	uint64 GetLastSignaledFence() const { return LastSignaledFence; }

	uint64 PeekLastCompletedFence() const;
	uint64 PeekLastCompletedFence(FRHIGPUMask InGPUMask) const;
	uint64 UpdateLastCompletedFence();

	// Might not be the most up to date value but avoids calling GetCompletedValue().
	uint64 GetLastCompletedFenceFast() const { return LastCompletedFence; };

	void Destroy();

protected:
	
	void InternalSignal(ED3D12CommandQueueType InQueueType, uint64 FenceToSignal);

protected:

	uint64 CurrentFence;
	uint64 LastSignaledFence; // 0 when not yet issued, otherwise the last value signaled to all GPU
	uint64 LastCompletedFence; // The min value completed between all LastCompletedFences.
	FCriticalSection WaitForFenceCS;

	uint64 LastCompletedFences[MAX_NUM_GPUS];
	FD3D12FenceCore* FenceCores[MAX_NUM_GPUS];

	FName Name;
};

// Fence value must be incremented manually. Useful when you need incrementing and signaling to happen at different times, e.g. for a FrameFence,
// where the RenderThread increments and the RHIThread signals, so that both are in agreement on which frame they are on.
class FD3D12ManualFence : public FD3D12Fence
{
public:
	explicit FD3D12ManualFence(FD3D12Adapter* InParent, FRHIGPUMask InGPUMask, const FName& InName = L"<unnamed>")
		: FD3D12Fence(InParent, InGPUMask, InName)
	{}

	uint64 GetCurrentFence() const final override
	{
		return IsInRHIThread()? LastSignaledFence + 1 : CurrentFence;
	}

	// Signals the specified fence value.
	uint64 ManualSignal(ED3D12CommandQueueType InQueueType, uint64 FenceToSignal);

	// Increments the current fence and returns the previous value.
	inline uint64 IncrementCurrentFence()
	{
		check(IsInRenderingThread());
		return CurrentFence++;
	}
};

// Special fence for the command allocator which can be advanced already before internal signal has happened because
// execute can be done via task
class FD3D12CommandListFence : public FD3D12Fence
{
public:
	explicit FD3D12CommandListFence(FD3D12Adapter* InParent, FRHIGPUMask InGPUMask, const FName& InName = L"<unnamed>")
		: FD3D12Fence(InParent, InGPUMask, InName), CurrentOrPendingFenceValue(CurrentFence)
	{}

	virtual void CreateFence() final override
	{
		FD3D12Fence::CreateFence();
		CurrentOrPendingFenceValue = CurrentFence;
	}

	virtual uint64 GetCurrentFence() const final override { check(CurrentOrPendingFenceValue == CurrentFence || CurrentOrPendingFenceValue == CurrentFence + 1); return CurrentOrPendingFenceValue; }
	void AdvancePendingFenceValue()
	{
		check(CurrentOrPendingFenceValue == CurrentFence);
		CurrentOrPendingFenceValue++;
	}
	virtual uint64 Signal(ED3D12CommandQueueType InQueueType) final override
	{
		check(CurrentOrPendingFenceValue == CurrentFence || CurrentOrPendingFenceValue == CurrentFence + 1);
		uint64 Result = FD3D12Fence::Signal(InQueueType);
		CurrentOrPendingFenceValue = CurrentFence;
		return Result;
	}

protected:

	uint64 CurrentOrPendingFenceValue;
};

class FD3D12CommandAllocatorManager : public FD3D12DeviceChild
{
public:
	FD3D12CommandAllocatorManager(FD3D12Device* InParent, const D3D12_COMMAND_LIST_TYPE& InType);

	~FD3D12CommandAllocatorManager()
	{
		// Go through all command allocators owned by this manager and delete them.
		for (auto Iter = CommandAllocators.CreateIterator(); Iter; ++Iter)
		{
			FD3D12CommandAllocator* pCommandAllocator = *Iter;
			delete pCommandAllocator;
		}
	}

	FD3D12CommandAllocator* ObtainCommandAllocator();
	void ReleaseCommandAllocator(FD3D12CommandAllocator* CommandAllocator);

private:
	TArray<FD3D12CommandAllocator*> CommandAllocators;		// List of all command allocators owned by this manager
	TQueue<FD3D12CommandAllocator*> CommandAllocatorQueue;	// Queue of available allocators. Note they might still be in use by the GPU.
	FCriticalSection CS;	// Must be thread-safe because multiple threads can obtain/release command allocators
	const D3D12_COMMAND_LIST_TYPE Type;
};

class FD3D12CommandListManager : public FD3D12DeviceChild, public FD3D12SingleNodeGPUObject
{
public:
	struct FResolvedCmdListExecTime
	{
		uint64 StartTimestamp;
		uint64 EndTimestamp;

		FResolvedCmdListExecTime() = default;

		FResolvedCmdListExecTime(uint64 InStart, uint64 InEnd)
			: StartTimestamp(InStart)
			, EndTimestamp(InEnd)
		{}
	};

	FD3D12CommandListManager(FD3D12Device* InParent, D3D12_COMMAND_LIST_TYPE InCommandListType, ED3D12CommandQueueType InQueueType);
	virtual ~FD3D12CommandListManager();

	void Create(const TCHAR* Name, uint32 NumCommandLists = 0, uint32 Priority = 0);
	void Destroy();

	inline bool IsReady()
	{
		return D3DCommandQueue.GetReference() != nullptr;
	}

	// This use to also take an optional PSO parameter so that we could pass this directly to Create/Reset command lists,
	// however this was removed as we generally can't actually predict what PSO we'll need until draw due to frequent
	// state changes. We leave PSOs to always be resolved in ApplyState().
	FD3D12CommandListHandle ObtainCommandList(FD3D12CommandAllocator& CommandAllocator);
	void ReleaseCommandList(FD3D12CommandListHandle& hList);

	void ExecuteCommandList(FD3D12CommandListHandle& hList, bool WaitForCompletion = false);
	virtual void ExecuteCommandLists(TArray<FD3D12CommandListHandle>& Lists, bool WaitForCompletion = false);

	void WaitOnExecuteTask();

	CommandListState GetCommandListState(const FD3D12CLSyncPoint& hSyncPoint);

	bool IsComplete(const FD3D12CLSyncPoint& hSyncPoint, uint64 FenceOffset = 0);
	void WaitForCompletion(const FD3D12CLSyncPoint& hSyncPoint)
	{
		hSyncPoint.WaitForCompletion();
	}

	// Performs a GPU and CPU timestamp at nearly the same time.
	// This allows aligning GPU and CPU events on the same timeline in profile visualization.
	FGPUTimingCalibrationTimestamp GetCalibrationTimestamp();

	FORCEINLINE HRESULT GetTimestampFrequency(uint64* Frequency) { return D3DCommandQueue->GetTimestampFrequency(Frequency); }
	FORCEINLINE ID3D12CommandQueue* GetD3DCommandQueue() { return D3DCommandQueue.GetReference();}
	FORCEINLINE ED3D12CommandQueueType GetQueueType() const { return QueueType; }

	FORCEINLINE FD3D12Fence& GetFence() { check(CommandListFence); return *CommandListFence; }

	/** Get the breadcrumb resource which is written during command context recording */
	FD3D12Resource* GetBreadCrumbResource() { return BreadCrumbResource.GetReference(); }

	/** Get the CPU readable data from the breadcrumb data - this data is still valid after the Device is Lost */
	const void* GetBreadCrumbResourceAddress() const { return BreadCrumbResourceAddress; }

	void WaitForCommandQueueFlush();

	void ReleaseResourceBarrierCommandListAllocator();

	/** Command lists opened after this returns will track their execution time */
	void StartTrackingCommandListTime();

	/** Command lists opened after this returns won't track execution time */
	void EndTrackingCommandListTime();

	/** Get the start/end timestamps of all tracked commandlists obtained from this manager */
	void GetCommandListTimingResults(TArray<FResolvedCmdListExecTime>& OutTimingPairs, bool bUseBlockingCall = true);

	/** Get the start/end timestamps of all tracked command lists obtained from this manager */
	void SortTimingResults();

	/** Resolve all commandlist start/end timestamp queries and get results. Results will be 2-frame old if bWait is false */
	void FlushPendingTimingPairs(bool bWait);

	TArray<uint64> &GetStartTimestamps() { return CmdListStartTimestamps; }
	TArray<uint64> &GetEndTimestamps() { return CmdListEndTimestamps; }
	TArray<uint64> &GetIdleTime() { return IdleTimeCDF; }

	bool GetShouldTrackCmdListTime() const { return bShouldTrackCmdListTime; }
	void SetShouldTrackCmdListTime(bool val) { bShouldTrackCmdListTime = val; }

protected:
	struct FCmdListExecTime
	{
		int32 StartTimeQueryIdx;
		int32 EndTimeQueryIdx;

		FCmdListExecTime() = default;

		FCmdListExecTime(int32 InStart, int32 InEnd)
			: StartTimeQueryIdx(InStart)
			, EndTimeQueryIdx(InEnd)
		{}
	};

	void ExecuteCommandListInteral(TArray<FD3D12CommandListHandle>& Lists, bool WaitForCompletion);
	uint32 GetResourceBarrierCommandList(FD3D12CommandListHandle& hList, FD3D12CommandListHandle& hResourceBarrierList);

	// Returns signaled Fence
	uint64 ExecuteAndIncrementFence(FD3D12CommandListPayload& Payload, FD3D12Fence &Fence);
	FD3D12CommandListHandle CreateCommandListHandle(FD3D12CommandAllocator& CommandAllocator);

	/** Should this commandlist track its execution time */
	bool ShouldTrackCommandListTime() const;

	TRefCountPtr<ID3D12CommandQueue>		D3DCommandQueue;

	FThreadsafeQueue<FD3D12CommandListHandle> ReadyLists;

	// Command allocators used exclusively for resource barrier command lists.
	FD3D12CommandAllocatorManager ResourceBarrierCommandAllocatorManager;
	FD3D12CommandAllocator* ResourceBarrierCommandAllocator;

	TRefCountPtr<FD3D12CommandListFence>	CommandListFence;

	D3D12_COMMAND_LIST_TYPE					CommandListType;
	ED3D12CommandQueueType					QueueType;
	FCriticalSection						ResourceStateCS;
	FCriticalSection						FenceCS;

	// Current possible active execute task to offload RHI thread
	FGraphEventRef							ExecuteTask;
	TArray<FD3D12CommandListHandle>			ExecuteCommandListHandles;

	// Helper data used to track GPU progress on this command queue
	void* BreadCrumbResourceAddress;
	TRefCountPtr<FD3D12Heap> BreadCrumbHeap;
	TRefCountPtr<FD3D12Resource> BreadCrumbResource;
	
#if WITH_PROFILEGPU || D3D12_SUBMISSION_GAP_RECORDER
	uint64 CmdListTimingQueryBatchTokens[2];
	TArray<FResolvedCmdListExecTime> ResolvedTimingPairs;
#endif

	bool bShouldTrackCmdListTime;
	/** Timstamps marking the beginning of tracked command lists */
	TArray<uint64> CmdListStartTimestamps;
	/** Timstamps marking the end of tracked command lists */
	TArray<uint64> CmdListEndTimestamps;
	/** Accumulated idle GPU ticks before each corresponding command list */
	TArray<uint64> IdleTimeCDF;
};
