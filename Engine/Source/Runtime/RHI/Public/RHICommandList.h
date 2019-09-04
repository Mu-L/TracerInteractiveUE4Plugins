// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RHICommandList.h: RHI Command List definitions for queueing up & executing later.
=============================================================================*/

#pragma once
#include "CoreTypes.h"
#include "Misc/AssertionMacros.h"
#include "HAL/UnrealMemory.h"
#include "Templates/UnrealTemplate.h"
#include "Math/Color.h"
#include "Math/IntPoint.h"
#include "Math/IntRect.h"
#include "Math/Box2D.h"
#include "Math/PerspectiveMatrix.h"
#include "Math/TranslationMatrix.h"
#include "Math/ScaleMatrix.h"
#include "Math/Float16Color.h"
#include "HAL/ThreadSafeCounter.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "Misc/MemStack.h"
#include "Misc/App.h"
#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/LowLevelMemTracker.h"


// Set to 1 to capture the callstack for every RHI command. Cheap & memory efficient representation: Use the 
// value in FRHICommand::StackFrames to get the pointer to the code (ie paste on a disassembly window)
#define RHICOMMAND_CALLSTACK		0
#if RHICOMMAND_CALLSTACK
#include "HAL/PlatformStackwalk.h"
#endif

class FApp;
class FBlendStateInitializerRHI;
class FGraphicsPipelineStateInitializer;
class FLastRenderTimeContainer;
class FRHICommandListBase;
class FRHIComputeShader;
class IRHICommandContext;
class IRHIComputeContext;
struct FDepthStencilStateInitializerRHI;
struct FRasterizerStateInitializerRHI;
struct FRHIResourceCreateInfo;
struct FRHIResourceInfo;
struct FRHIUniformBufferLayout;
struct FSamplerStateInitializerRHI;
struct FTextureMemoryStats;
class FComputePipelineState;
class FGraphicsPipelineState;
class FRayTracingPipelineState;

DECLARE_STATS_GROUP(TEXT("RHICmdList"), STATGROUP_RHICMDLIST, STATCAT_Advanced);


// set this one to get a stat for each RHI command 
#define RHI_STATS 0

#if RHI_STATS
DECLARE_STATS_GROUP(TEXT("RHICommands"),STATGROUP_RHI_COMMANDS, STATCAT_Advanced);
#define RHISTAT(Method)	DECLARE_SCOPE_CYCLE_COUNTER(TEXT(#Method), STAT_RHI##Method, STATGROUP_RHI_COMMANDS)
#else
#define RHISTAT(Method)
#endif

extern RHI_API bool GUseRHIThread_InternalUseOnly;
extern RHI_API bool GUseRHITaskThreads_InternalUseOnly;
extern RHI_API bool GIsRunningRHIInSeparateThread_InternalUseOnly;
extern RHI_API bool GIsRunningRHIInDedicatedThread_InternalUseOnly;
extern RHI_API bool GIsRunningRHIInTaskThread_InternalUseOnly;

/** private accumulator for the RHI thread. */
extern RHI_API uint32 GWorkingRHIThreadTime;
extern RHI_API uint32 GWorkingRHIThreadStallTime;
extern RHI_API uint32 GWorkingRHIThreadStartCycles;

/** How many cycles the from sampling input to the frame being flipped. */
extern RHI_API uint64 GInputLatencyTime;

/**
* Whether the RHI commands are being run in a thread other than the render thread
*/
bool FORCEINLINE IsRunningRHIInSeparateThread()
{
	return GIsRunningRHIInSeparateThread_InternalUseOnly;
}

/**
* Whether the RHI commands are being run on a dedicated thread other than the render thread
*/
bool FORCEINLINE IsRunningRHIInDedicatedThread()
{
	return GIsRunningRHIInDedicatedThread_InternalUseOnly;
}

/**
* Whether the RHI commands are being run on a dedicated thread other than the render thread
*/
bool FORCEINLINE IsRunningRHIInTaskThread()
{
	return GIsRunningRHIInTaskThread_InternalUseOnly;
}


extern RHI_API bool GEnableAsyncCompute;
extern RHI_API TAutoConsoleVariable<int32> CVarRHICmdWidth;
extern RHI_API TAutoConsoleVariable<int32> CVarRHICmdFlushRenderThreadTasks;

#if RHI_RAYTRACING
struct FRayTracingShaderBindings
{
	FRHITexture* Textures[32] = {};
	FRHIShaderResourceView* SRVs[32] = {};
	FRHIUniformBuffer* UniformBuffers[8] = {};
	FRHISamplerState* Samplers[16] = {};
	FRHIUnorderedAccessView* UAVs[8] = {};
};

// C++ counter-part of FBasicRayData declared in RayTracingCommon.ush
struct FBasicRayData
{
	float Origin[3];
	uint32 Mask;
	float Direction[3];
	float TFar;
};

// C++ counter-part of FIntersectionPayload declared in RayTracingCommon.ush
struct FIntersectionPayload
{
	float  HitT;            // Distance from ray origin to the intersection point in the ray direction. Negative on miss.
	uint32 PrimitiveIndex;  // Index of the primitive within the geometry inside the bottom-level acceleration structure instance. Undefined on miss.
	uint32 InstanceIndex;   // Index of the current instance in the top-level structure. Undefined on miss.
	float  Barycentrics[2]; // Primitive barycentric coordinates of the intersection point. Undefined on miss.
};
#endif // RHI_RAYTRACING

struct RHI_API FLockTracker
{
	struct FLockParams
	{
		void* RHIBuffer;
		void* Buffer;
		uint32 BufferSize;
		uint32 Offset;
		EResourceLockMode LockMode;

		FORCEINLINE_DEBUGGABLE FLockParams(void* InRHIBuffer, void* InBuffer, uint32 InOffset, uint32 InBufferSize, EResourceLockMode InLockMode)
			: RHIBuffer(InRHIBuffer)
			, Buffer(InBuffer)
			, BufferSize(InBufferSize)
			, Offset(InOffset)
			, LockMode(InLockMode)
		{
		}
	};
	TArray<FLockParams, TInlineAllocator<16> > OutstandingLocks;
	uint32 TotalMemoryOutstanding;

	FLockTracker()
	{
		TotalMemoryOutstanding = 0;
	}

	FORCEINLINE_DEBUGGABLE void Lock(void* RHIBuffer, void* Buffer, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode)
	{
#if DO_CHECK
		for (auto& Parms : OutstandingLocks)
		{
			check(Parms.RHIBuffer != RHIBuffer);
		}
#endif
		OutstandingLocks.Add(FLockParams(RHIBuffer, Buffer, Offset, SizeRHI, LockMode));
		TotalMemoryOutstanding += SizeRHI;
	}
	FORCEINLINE_DEBUGGABLE FLockParams Unlock(void* RHIBuffer)
	{
		for (int32 Index = 0; Index < OutstandingLocks.Num(); Index++)
		{
			if (OutstandingLocks[Index].RHIBuffer == RHIBuffer)
			{
				FLockParams Result = OutstandingLocks[Index];
				OutstandingLocks.RemoveAtSwap(Index, 1, false);
				return Result;
			}
		}
		check(!"Mismatched RHI buffer locks.");
		return FLockParams(nullptr, nullptr, 0, 0, RLM_WriteOnly);
	}
};

#ifdef CONTINUABLE_PSO_VERIFY
#define PSO_VERIFY ensure
#else
#define PSO_VERIFY	check
#endif

enum class ECmdList
{
	EGfx,
	ECompute,	
};

class IRHICommandContextContainer
{
public:
	virtual ~IRHICommandContextContainer()
	{
	}

	virtual IRHICommandContext* GetContext()
	{
		return nullptr;
	}

	virtual void SubmitAndFreeContextContainer(int32 Index, int32 Num)
	{
		check(0);
	}

	virtual void FinishContext()
	{
		check(0);
	}
};

struct FRHICommandListDebugContext
{
	FRHICommandListDebugContext()
	{
#if RHI_COMMAND_LIST_DEBUG_TRACES
		DebugStringStore[MaxDebugStoreSize] = 1337;
#endif
	}

	void PushMarker(const TCHAR* Marker)
	{
#if RHI_COMMAND_LIST_DEBUG_TRACES
		//allocate a new slot for the stack of pointers
		//and preserve the top of the stack in case we reach the limit
		if (++DebugMarkerStackIndex >= MaxDebugMarkerStackDepth)
		{
			for (uint32 i = 1; i < MaxDebugMarkerStackDepth; i++)
			{
				DebugMarkerStack[i - 1] = DebugMarkerStack[i];
				DebugMarkerSizes[i - 1] = DebugMarkerSizes[i];
			}
			DebugMarkerStackIndex = MaxDebugMarkerStackDepth - 1;
		}

		//try and copy the sting into the debugstore on the stack
		TCHAR* Offset = &DebugStringStore[DebugStoreOffset];
		uint32 MaxLength = MaxDebugStoreSize - DebugStoreOffset;
		uint32 Length = TryCopyString(Offset, Marker, MaxLength) + 1;

		//if we reached the end reset to the start and try again
		if (Length >= MaxLength)
		{
			DebugStoreOffset = 0;
			Offset = &DebugStringStore[DebugStoreOffset];
			MaxLength = MaxDebugStoreSize;
			Length = TryCopyString(Offset, Marker, MaxLength) + 1;

			//if the sting was bigger than the size of the store just terminate what we have
			if (Length >= MaxDebugStoreSize)
			{
				DebugStringStore[MaxDebugStoreSize - 1] = TEXT('\0');
			}
		}

		//add the string to the stack
		DebugMarkerStack[DebugMarkerStackIndex] = Offset;
		DebugStoreOffset += Length;
		DebugMarkerSizes[DebugMarkerStackIndex] = Length;

		check(DebugStringStore[MaxDebugStoreSize] == 1337);
#endif
	}

	void PopMarker()
	{
#if RHI_COMMAND_LIST_DEBUG_TRACES
		//clean out the debug stack if we have valid data
		if (DebugMarkerStackIndex >= 0 && DebugMarkerStackIndex < MaxDebugMarkerStackDepth)
		{
			DebugMarkerStack[DebugMarkerStackIndex] = nullptr;
			//also free the data in the store to postpone wrapping as much as possibler
			DebugStoreOffset -= DebugMarkerSizes[DebugMarkerStackIndex];

			//in case we already wrapped in the past just assume we start allover again
			if (DebugStoreOffset >= MaxDebugStoreSize)
			{
				DebugStoreOffset = 0;
			}
		}

		//pop the stack pointer
		if (--DebugMarkerStackIndex == (~0u) - 1)
		{
			//in case we wrapped in the past just restart
			DebugMarkerStackIndex = ~0u;
		}
#endif
	}

#if RHI_COMMAND_LIST_DEBUG_TRACES
private:

	//Tries to copy a string and early exits if it hits the limit. 
	//Returns the size of the string or the limit when reached.
	uint32 TryCopyString(TCHAR* Dest, const TCHAR* Source, uint32 MaxLength)
	{
		uint32 Length = 0;
		while(Source[Length] != TEXT('\0') && Length < MaxLength)
		{
			Dest[Length] = Source[Length];
			Length++;
		}

		if (Length < MaxLength)
		{
			Dest[Length] = TEXT('\0');
		}
		return Length;
	}

	uint32 DebugStoreOffset = 0;
	static constexpr int MaxDebugStoreSize = 1023;
	TCHAR DebugStringStore[MaxDebugStoreSize + 1];

	uint32 DebugMarkerStackIndex = ~0u;
	static constexpr int MaxDebugMarkerStackDepth = 32;
	const TCHAR* DebugMarkerStack[MaxDebugMarkerStackDepth] = {};
	uint32 DebugMarkerSizes[MaxDebugMarkerStackDepth] = {};
#endif
};

struct FRHICommandBase
{
	FRHICommandBase* Next = nullptr;
	virtual void ExecuteAndDestruct(FRHICommandListBase& CmdList, FRHICommandListDebugContext& DebugContext) = 0;
};

// Thread-safe allocator for GPU fences used in deferred command list execution
// Fences are stored in a ringbuffer
class RHI_API FRHICommandListFenceAllocator
{
public:
	static const int MAX_FENCE_INDICES		= 4096;
	FRHICommandListFenceAllocator()
	{
		CurrentFenceIndex = 0;
		for ( int i=0; i<MAX_FENCE_INDICES; i++)
		{
			FenceIDs[i] = 0xffffffffffffffffull;
			FenceFrameNumber[i] = 0xffffffff;
		}
	}

	uint32 AllocFenceIndex()
	{
		check(IsInRenderingThread());
		uint32 FenceIndex = ( FPlatformAtomics::InterlockedIncrement(&CurrentFenceIndex)-1 ) % MAX_FENCE_INDICES;
		check(FenceFrameNumber[FenceIndex] != GFrameNumberRenderThread);
		FenceFrameNumber[FenceIndex] = GFrameNumberRenderThread;

		return FenceIndex;
	}

	volatile uint64& GetFenceID( int32 FenceIndex )
	{
		check( FenceIndex < MAX_FENCE_INDICES );
		return FenceIDs[ FenceIndex ];
	}

private:
	volatile int32 CurrentFenceIndex;
	uint64 FenceIDs[MAX_FENCE_INDICES];
	uint32 FenceFrameNumber[MAX_FENCE_INDICES];
};

extern RHI_API FRHICommandListFenceAllocator GRHIFenceAllocator;

class RHI_API FRHICommandListBase : public FNoncopyable
{
public:
	FRHICommandListBase(FRHIGPUMask InGPUMask);
	~FRHICommandListBase();

	/** Custom new/delete with recycling */
	void* operator new(size_t Size);
	void operator delete(void *RawMemory);

	inline void Flush();
	inline bool IsImmediate();
	inline bool IsImmediateAsyncCompute();

	const int32 GetUsedMemory() const;
	void QueueAsyncCommandListSubmit(FGraphEventRef& AnyThreadCompletionEvent, class FRHICommandList* CmdList);
	void QueueParallelAsyncCommandListSubmit(FGraphEventRef* AnyThreadCompletionEvents, bool bIsPrepass, class FRHICommandList** CmdLists, int32* NumDrawsIfKnown, int32 Num, int32 MinDrawsPerTranslate, bool bSpewMerge);
	void QueueRenderThreadCommandListSubmit(FGraphEventRef& RenderThreadCompletionEvent, class FRHICommandList* CmdList);
	void QueueCommandListSubmit(class FRHICommandList* CmdList);
	void AddDispatchPrerequisite(const FGraphEventRef& Prereq);
	void WaitForTasks(bool bKnownToBeComplete = false);
	void WaitForDispatch();
	void WaitForRHIThreadTasks();
	void HandleRTThreadTaskCompletion(const FGraphEventRef& MyCompletionGraphEvent);

	FORCEINLINE_DEBUGGABLE void* Alloc(int32 AllocSize, int32 Alignment)
	{
		checkSlow(!Bypass() && "Can't use RHICommandList in bypass mode.");
		return MemManager.Alloc(AllocSize, Alignment);
	}

	template <typename T>
	FORCEINLINE_DEBUGGABLE void* Alloc()
	{
		return Alloc(sizeof(T), alignof(T));
	}

	template <typename T>
	FORCEINLINE_DEBUGGABLE const TArrayView<T> AllocArray(const TArrayView<T> InArray)
	{
		void* NewArray = Alloc(InArray.Num() * sizeof(T), alignof(T));
		FMemory::Memcpy(NewArray, InArray.GetData(), InArray.Num() * sizeof(T));
		return TArrayView<T>((T*) NewArray, InArray.Num());
	}

	FORCEINLINE_DEBUGGABLE TCHAR* AllocString(const TCHAR* Name)
	{
		int32 Len = FCString::Strlen(Name) + 1;
		TCHAR* NameCopy  = (TCHAR*)Alloc(Len * (int32)sizeof(TCHAR), (int32)sizeof(TCHAR));
		FCString::Strcpy(NameCopy, Len, Name);
		return NameCopy;
	}

	FORCEINLINE_DEBUGGABLE void* AllocCommand(int32 AllocSize, int32 Alignment)
	{
		checkSlow(!IsExecuting());
		FRHICommandBase* Result = (FRHICommandBase*) MemManager.Alloc(AllocSize, Alignment);
		++NumCommands;
		*CommandLink = Result;
		CommandLink = &Result->Next;
		return Result;
	}
	template <typename TCmd>
	FORCEINLINE void* AllocCommand()
	{
		return AllocCommand(sizeof(TCmd), alignof(TCmd));
	}

	FORCEINLINE uint32 GetUID()
	{
		return UID;
	}
	FORCEINLINE bool HasCommands() const
	{
		return (NumCommands > 0);
	}
	FORCEINLINE bool IsExecuting() const
	{
		return bExecuting;
	}

	bool Bypass();

	FORCEINLINE void ExchangeCmdList(FRHICommandListBase& Other)
	{
		check(!RTTasks.Num() && !Other.RTTasks.Num());
		FMemory::Memswap(this, &Other, sizeof(FRHICommandListBase));
		if (CommandLink == &Other.Root)
		{
			CommandLink = &Root;
		}
		if (Other.CommandLink == &Root)
		{
			Other.CommandLink = &Other.Root;
		}
	}

	void SetContext(IRHICommandContext* InContext)
	{
		check(InContext);
		Context = InContext;
	}

	FORCEINLINE IRHICommandContext& GetContext()
	{
		checkSlow(Context);
		return *Context;
	}

	void SetComputeContext(IRHIComputeContext* InContext)
	{
		check(InContext);
		ComputeContext = InContext;
	}

	IRHIComputeContext& GetComputeContext()
	{
		checkSlow(ComputeContext);
		return *ComputeContext;
	}

	void CopyContext(FRHICommandListBase& ParentCommandList)
	{
		check(Context);
		ensure(GPUMask == ParentCommandList.GPUMask);
		Context = ParentCommandList.Context;
		ComputeContext = ParentCommandList.ComputeContext;
	}

	void MaybeDispatchToRHIThread()
	{
		if (IsImmediate() && HasCommands() && GRHIThreadNeedsKicking && IsRunningRHIInSeparateThread())
		{
			MaybeDispatchToRHIThreadInner();
		}
	}
	void MaybeDispatchToRHIThreadInner();

	FORCEINLINE const FRHIGPUMask& GetGPUMask() const { return GPUMask; }

private:
	FRHICommandBase* Root;
	FRHICommandBase** CommandLink;
	bool bExecuting;
	uint32 NumCommands;
	uint32 UID;
	IRHICommandContext* Context;
	IRHIComputeContext* ComputeContext;
	FMemStackBase MemManager; 
	FGraphEventArray RTTasks;

	friend class FRHICommandListExecutor;
	friend class FRHICommandListIterator;

protected:
	bool bAsyncPSOCompileAllowed;
	FRHIGPUMask GPUMask;
	void Reset();

public:
	TStatId	ExecuteStat;
	enum class ERenderThreadContext
	{
		SceneRenderTargets,
		Num
	};
	void *RenderThreadContexts[(int32)ERenderThreadContext::Num];

protected:
	//the values of this struct must be copied when the commandlist is split 
	struct FPSOContext
	{
		uint32 CachedNumSimultanousRenderTargets = 0;
		TStaticArray<FRHIRenderTargetView, MaxSimultaneousRenderTargets> CachedRenderTargets;
		FRHIDepthRenderTargetView CachedDepthStencilTarget;
		
		ESubpassHint SubpassHint = ESubpassHint::None;
		uint8 SubpassIndex = 0;

	} PSOContext;

	void CacheActiveRenderTargets(
		uint32 NewNumSimultaneousRenderTargets,
		const FRHIRenderTargetView* NewRenderTargetsRHI,
		const FRHIDepthRenderTargetView* NewDepthStencilTargetRHI
		)
	{
		PSOContext.CachedNumSimultanousRenderTargets = NewNumSimultaneousRenderTargets;

		for (uint32 RTIdx = 0; RTIdx < PSOContext.CachedNumSimultanousRenderTargets; ++RTIdx)
		{
			PSOContext.CachedRenderTargets[RTIdx] = NewRenderTargetsRHI[RTIdx];
		}

		PSOContext.CachedDepthStencilTarget = (NewDepthStencilTargetRHI) ? *NewDepthStencilTargetRHI : FRHIDepthRenderTargetView();
	}

	void CacheActiveRenderTargets(const FRHIRenderPassInfo& Info)
	{
		FRHISetRenderTargetsInfo RTInfo;
		Info.ConvertToRenderTargetsInfo(RTInfo);
		CacheActiveRenderTargets(RTInfo.NumColorRenderTargets, RTInfo.ColorRenderTarget, &RTInfo.DepthStencilRenderTarget);
	}

	void IncrementSubpass()
	{
		PSOContext.SubpassIndex++;
	}
	
	void ResetSubpass(ESubpassHint SubpassHint)
	{
		PSOContext.SubpassHint = SubpassHint;
		PSOContext.SubpassIndex = 0;
	}
	
public:
	void CopyRenderThreadContexts(const FRHICommandListBase& ParentCommandList)
	{
		for (int32 Index = 0; ERenderThreadContext(Index) < ERenderThreadContext::Num; Index++)
		{
			RenderThreadContexts[Index] = ParentCommandList.RenderThreadContexts[Index];
		}
	}
	void SetRenderThreadContext(void* InContext, ERenderThreadContext Slot)
	{
		RenderThreadContexts[int32(Slot)] = InContext;
	}
	FORCEINLINE void* GetRenderThreadContext(ERenderThreadContext Slot)
	{
		return RenderThreadContexts[int32(Slot)];
	}

	struct FCommonData
	{
		class FRHICommandListBase* Parent = nullptr;

		enum class ECmdListType
		{
			Immediate = 1,
			Regular,
		};
		ECmdListType Type = ECmdListType::Regular;
		bool bInsideRenderPass = false;
		bool bInsideComputePass = false;
	};

	bool DoValidation() const
	{
		static auto* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RenderPass.Validation"));
		return CVar && CVar->GetInt() != 0;
	}

	inline bool IsOutsideRenderPass() const
	{
		return !Data.bInsideRenderPass;
	}

	inline bool IsInsideRenderPass() const
	{
		return Data.bInsideRenderPass;
	}

	inline bool IsInsideComputePass() const
	{
		return Data.bInsideComputePass;
	}

	FCommonData Data;
};

template<typename TCmd>
struct FRHICommand : public FRHICommandBase
{
#if RHICOMMAND_CALLSTACK
	uint64 StackFrames[16];

	FRHICommand()
	{
		FPlatformStackWalk::CaptureStackBackTrace(StackFrames, 16);
	}
#endif

	void ExecuteAndDestruct(FRHICommandListBase& CmdList, FRHICommandListDebugContext& Context) override final
	{
		TCmd *ThisCmd = static_cast<TCmd*>(this);
#if RHI_COMMAND_LIST_DEBUG_TRACES
		ThisCmd->StoreDebugInfo(Context);
#endif
		ThisCmd->Execute(CmdList);
		ThisCmd->~TCmd();
	}

	virtual void StoreDebugInfo(FRHICommandListDebugContext& Context) {};
};

struct  FRHICommandBeginUpdateMultiFrameResource final : public FRHICommand<FRHICommandBeginUpdateMultiFrameResource>
{
	FRHITexture* Texture;
	FORCEINLINE_DEBUGGABLE FRHICommandBeginUpdateMultiFrameResource(FRHITexture* InTexture)
		: Texture(InTexture)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct  FRHICommandEndUpdateMultiFrameResource final : public FRHICommand<FRHICommandEndUpdateMultiFrameResource>
{
	FRHITexture* Texture;
	FORCEINLINE_DEBUGGABLE FRHICommandEndUpdateMultiFrameResource(FRHITexture* InTexture)
		: Texture(InTexture)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct  FRHICommandBeginUpdateMultiFrameUAV final : public FRHICommand<FRHICommandBeginUpdateMultiFrameResource>
{
	FRHIUnorderedAccessView* UAV;
	FORCEINLINE_DEBUGGABLE FRHICommandBeginUpdateMultiFrameUAV(FRHIUnorderedAccessView* InUAV)
		: UAV(InUAV)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct  FRHICommandEndUpdateMultiFrameUAV final : public FRHICommand<FRHICommandEndUpdateMultiFrameResource>
{
	FRHIUnorderedAccessView* UAV;
	FORCEINLINE_DEBUGGABLE FRHICommandEndUpdateMultiFrameUAV(FRHIUnorderedAccessView* InUAV)
		: UAV(InUAV)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandSetStencilRef final : public FRHICommand<FRHICommandSetStencilRef>
{
	uint32 StencilRef;
	FORCEINLINE_DEBUGGABLE FRHICommandSetStencilRef(uint32 InStencilRef)
		: StencilRef(InStencilRef)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

template <typename TRHIShader, ECmdList CmdListType>
struct FRHICommandSetShaderParameter final : public FRHICommand<FRHICommandSetShaderParameter<TRHIShader, CmdListType> >
{
	TRHIShader* Shader;
	const void* NewValue;
	uint32 BufferIndex;
	uint32 BaseIndex;
	uint32 NumBytes;
	FORCEINLINE_DEBUGGABLE FRHICommandSetShaderParameter(TRHIShader* InShader, uint32 InBufferIndex, uint32 InBaseIndex, uint32 InNumBytes, const void* InNewValue)
		: Shader(InShader)
		, NewValue(InNewValue)
		, BufferIndex(InBufferIndex)
		, BaseIndex(InBaseIndex)
		, NumBytes(InNumBytes)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

template <typename TRHIShader, ECmdList CmdListType>
struct FRHICommandSetShaderUniformBuffer final : public FRHICommand<FRHICommandSetShaderUniformBuffer<TRHIShader, CmdListType> >
{
	TRHIShader* Shader;
	uint32 BaseIndex;
	FRHIUniformBuffer* UniformBuffer;
	FORCEINLINE_DEBUGGABLE FRHICommandSetShaderUniformBuffer(TRHIShader* InShader, uint32 InBaseIndex, FRHIUniformBuffer* InUniformBuffer)
		: Shader(InShader)
		, BaseIndex(InBaseIndex)
		, UniformBuffer(InUniformBuffer)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

template <typename TRHIShader, ECmdList CmdListType>
struct FRHICommandSetShaderTexture final : public FRHICommand<FRHICommandSetShaderTexture<TRHIShader, CmdListType> >
{
	TRHIShader* Shader;
	uint32 TextureIndex;
	FRHITexture* Texture;
	FORCEINLINE_DEBUGGABLE FRHICommandSetShaderTexture(TRHIShader* InShader, uint32 InTextureIndex, FRHITexture* InTexture)
		: Shader(InShader)
		, TextureIndex(InTextureIndex)
		, Texture(InTexture)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

template <typename TRHIShader, ECmdList CmdListType>
struct FRHICommandSetShaderResourceViewParameter final : public FRHICommand<FRHICommandSetShaderResourceViewParameter<TRHIShader, CmdListType> >
{
	TRHIShader* Shader;
	uint32 SamplerIndex;
	FRHIShaderResourceView* SRV;
	FORCEINLINE_DEBUGGABLE FRHICommandSetShaderResourceViewParameter(TRHIShader* InShader, uint32 InSamplerIndex, FRHIShaderResourceView* InSRV)
		: Shader(InShader)
		, SamplerIndex(InSamplerIndex)
		, SRV(InSRV)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

template <typename TRHIShader, ECmdList CmdListType>
struct FRHICommandSetUAVParameter final : public FRHICommand<FRHICommandSetUAVParameter<TRHIShader, CmdListType> >
{
	TRHIShader* Shader;
	uint32 UAVIndex;
	FRHIUnorderedAccessView* UAV;
	FORCEINLINE_DEBUGGABLE FRHICommandSetUAVParameter(TRHIShader* InShader, uint32 InUAVIndex, FRHIUnorderedAccessView* InUAV)
		: Shader(InShader)
		, UAVIndex(InUAVIndex)
		, UAV(InUAV)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

template <typename TRHIShader, ECmdList CmdListType>
struct FRHICommandSetUAVParameter_IntialCount final : public FRHICommand<FRHICommandSetUAVParameter_IntialCount<TRHIShader, CmdListType> >
{
	TRHIShader* Shader;
	uint32 UAVIndex;
	FRHIUnorderedAccessView* UAV;
	uint32 InitialCount;
	FORCEINLINE_DEBUGGABLE FRHICommandSetUAVParameter_IntialCount(TRHIShader* InShader, uint32 InUAVIndex, FRHIUnorderedAccessView* InUAV, uint32 InInitialCount)
		: Shader(InShader)
		, UAVIndex(InUAVIndex)
		, UAV(InUAV)
		, InitialCount(InInitialCount)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

template <typename TRHIShader, ECmdList CmdListType>
struct FRHICommandSetShaderSampler final : public FRHICommand<FRHICommandSetShaderSampler<TRHIShader, CmdListType> >
{
	TRHIShader* Shader;
	uint32 SamplerIndex;
	FRHISamplerState* Sampler;
	FORCEINLINE_DEBUGGABLE FRHICommandSetShaderSampler(TRHIShader* InShader, uint32 InSamplerIndex, FRHISamplerState* InSampler)
		: Shader(InShader)
		, SamplerIndex(InSamplerIndex)
		, Sampler(InSampler)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandDrawPrimitive final : public FRHICommand<FRHICommandDrawPrimitive>
{
	uint32 BaseVertexIndex;
	uint32 NumPrimitives;
	uint32 NumInstances;
	FORCEINLINE_DEBUGGABLE FRHICommandDrawPrimitive(uint32 InBaseVertexIndex, uint32 InNumPrimitives, uint32 InNumInstances)
		: BaseVertexIndex(InBaseVertexIndex)
		, NumPrimitives(InNumPrimitives)
		, NumInstances(InNumInstances)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandDrawIndexedPrimitive final : public FRHICommand<FRHICommandDrawIndexedPrimitive>
{
	FRHIIndexBuffer* IndexBuffer;
	int32 BaseVertexIndex;
	uint32 FirstInstance;
	uint32 NumVertices;
	uint32 StartIndex;
	uint32 NumPrimitives;
	uint32 NumInstances;
	FORCEINLINE_DEBUGGABLE FRHICommandDrawIndexedPrimitive(FRHIIndexBuffer* InIndexBuffer, int32 InBaseVertexIndex, uint32 InFirstInstance, uint32 InNumVertices, uint32 InStartIndex, uint32 InNumPrimitives, uint32 InNumInstances)
		: IndexBuffer(InIndexBuffer)
		, BaseVertexIndex(InBaseVertexIndex)
		, FirstInstance(InFirstInstance)
		, NumVertices(InNumVertices)
		, StartIndex(InStartIndex)
		, NumPrimitives(InNumPrimitives)
		, NumInstances(InNumInstances)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandSetBlendFactor final : public FRHICommand<FRHICommandSetBlendFactor>
{
	FLinearColor BlendFactor;
	FORCEINLINE_DEBUGGABLE FRHICommandSetBlendFactor(const FLinearColor& InBlendFactor)
		: BlendFactor(InBlendFactor)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandSetStreamSource final : public FRHICommand<FRHICommandSetStreamSource>
{
	uint32 StreamIndex;
	FRHIVertexBuffer* VertexBuffer;
	uint32 Offset;
	FORCEINLINE_DEBUGGABLE FRHICommandSetStreamSource(uint32 InStreamIndex, FRHIVertexBuffer* InVertexBuffer, uint32 InOffset)
		: StreamIndex(InStreamIndex)
		, VertexBuffer(InVertexBuffer)
		, Offset(InOffset)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandSetViewport final : public FRHICommand<FRHICommandSetViewport>
{
	uint32 MinX;
	uint32 MinY;
	float MinZ;
	uint32 MaxX;
	uint32 MaxY;
	float MaxZ;
	FORCEINLINE_DEBUGGABLE FRHICommandSetViewport(uint32 InMinX, uint32 InMinY, float InMinZ, uint32 InMaxX, uint32 InMaxY, float InMaxZ)
		: MinX(InMinX)
		, MinY(InMinY)
		, MinZ(InMinZ)
		, MaxX(InMaxX)
		, MaxY(InMaxY)
		, MaxZ(InMaxZ)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandSetStereoViewport final : public FRHICommand<FRHICommandSetStereoViewport>
{
	uint32 LeftMinX;
	uint32 RightMinX;
	uint32 LeftMinY;
	uint32 RightMinY;
	float MinZ;
	uint32 LeftMaxX;
	uint32 RightMaxX;
	uint32 LeftMaxY;
	uint32 RightMaxY;
	float MaxZ;
	FORCEINLINE_DEBUGGABLE FRHICommandSetStereoViewport(uint32 InLeftMinX, uint32 InRightMinX, uint32 InLeftMinY, uint32 InRightMinY, float InMinZ, uint32 InLeftMaxX, uint32 InRightMaxX, uint32 InLeftMaxY, uint32 InRightMaxY, float InMaxZ)
		: LeftMinX(InLeftMinX)
		, RightMinX(InRightMinX)
		, LeftMinY(InLeftMinY)
		, RightMinY(InRightMinY)
		, MinZ(InMinZ)
		, LeftMaxX(InLeftMaxX)
		, RightMaxX(InRightMaxX)
		, LeftMaxY(InLeftMaxY)
		, RightMaxY(InRightMaxY)
		, MaxZ(InMaxZ)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandSetScissorRect final : public FRHICommand<FRHICommandSetScissorRect>
{
	bool bEnable;
	uint32 MinX;
	uint32 MinY;
	uint32 MaxX;
	uint32 MaxY;
	FORCEINLINE_DEBUGGABLE FRHICommandSetScissorRect(bool InbEnable, uint32 InMinX, uint32 InMinY, uint32 InMaxX, uint32 InMaxY)
		: bEnable(InbEnable)
		, MinX(InMinX)
		, MinY(InMinY)
		, MaxX(InMaxX)
		, MaxY(InMaxY)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandSetRenderTargets final : public FRHICommand<FRHICommandSetRenderTargets>
{
	uint32 NewNumSimultaneousRenderTargets;
	FRHIRenderTargetView NewRenderTargetsRHI[MaxSimultaneousRenderTargets];
	FRHIDepthRenderTargetView NewDepthStencilTarget;
	uint32 NewNumUAVs;
	FRHIUnorderedAccessView* UAVs[MaxSimultaneousUAVs];

	FORCEINLINE_DEBUGGABLE FRHICommandSetRenderTargets(
		uint32 InNewNumSimultaneousRenderTargets,
		const FRHIRenderTargetView* InNewRenderTargetsRHI,
		const FRHIDepthRenderTargetView* InNewDepthStencilTargetRHI,
		uint32 InNewNumUAVs,
		FRHIUnorderedAccessView* const* InUAVs
		)
		: NewNumSimultaneousRenderTargets(InNewNumSimultaneousRenderTargets)
		, NewNumUAVs(InNewNumUAVs)

	{
		check(InNewNumSimultaneousRenderTargets <= MaxSimultaneousRenderTargets && InNewNumUAVs <= MaxSimultaneousUAVs);
		for (uint32 Index = 0; Index < NewNumSimultaneousRenderTargets; Index++)
		{
			NewRenderTargetsRHI[Index] = InNewRenderTargetsRHI[Index];
		}
		for (uint32 Index = 0; Index < NewNumUAVs; Index++)
		{
			UAVs[Index] = InUAVs[Index];
		}
		if (InNewDepthStencilTargetRHI)
		{
			NewDepthStencilTarget = *InNewDepthStencilTargetRHI;
		}		
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandBeginRenderPass final : public FRHICommand<FRHICommandBeginRenderPass>
{
	FRHIRenderPassInfo Info;
	const TCHAR* Name;

	FRHICommandBeginRenderPass(const FRHIRenderPassInfo& InInfo, const TCHAR* InName)
		: Info(InInfo)
		, Name(InName)
	{
	}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandEndRenderPass final : public FRHICommand<FRHICommandEndRenderPass>
{
	FRHICommandEndRenderPass()
	{
	}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandNextSubpass final : public FRHICommand<FRHICommandNextSubpass>
{
	FRHICommandNextSubpass()
	{
	}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FLocalCmdListParallelRenderPass
{
	TRefCountPtr<struct FRHIParallelRenderPass> RenderPass;
};

struct FRHICommandBeginParallelRenderPass final : public FRHICommand<FRHICommandBeginParallelRenderPass>
{
	FRHIRenderPassInfo Info;
	FLocalCmdListParallelRenderPass* LocalRenderPass;
	const TCHAR* Name;

	FRHICommandBeginParallelRenderPass(const FRHIRenderPassInfo& InInfo, FLocalCmdListParallelRenderPass* InLocalRenderPass, const TCHAR* InName)
		: Info(InInfo)
		, LocalRenderPass(InLocalRenderPass)
		, Name(InName)
	{
	}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandEndParallelRenderPass final : public FRHICommand<FRHICommandEndParallelRenderPass>
{
	FLocalCmdListParallelRenderPass* LocalRenderPass;

	FRHICommandEndParallelRenderPass(FLocalCmdListParallelRenderPass* InLocalRenderPass)
		: LocalRenderPass(InLocalRenderPass)
	{
	}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FLocalCmdListRenderSubPass
{
	TRefCountPtr<struct FRHIRenderSubPass> RenderSubPass;
};

struct FRHICommandBeginRenderSubPass final : public FRHICommand<FRHICommandBeginRenderSubPass>
{
	FLocalCmdListParallelRenderPass* LocalRenderPass;
	FLocalCmdListRenderSubPass* LocalRenderSubPass;

	FRHICommandBeginRenderSubPass(FLocalCmdListParallelRenderPass* InLocalRenderPass, FLocalCmdListRenderSubPass* InLocalRenderSubPass)
		: LocalRenderPass(InLocalRenderPass)
		, LocalRenderSubPass(InLocalRenderSubPass)
	{
	}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandEndRenderSubPass final : public FRHICommand<FRHICommandEndRenderSubPass>
{
	FLocalCmdListParallelRenderPass* LocalRenderPass;
	FLocalCmdListRenderSubPass* LocalRenderSubPass;

	FRHICommandEndRenderSubPass(FLocalCmdListParallelRenderPass* InLocalRenderPass, FLocalCmdListRenderSubPass* InLocalRenderSubPass)
		: LocalRenderPass(InLocalRenderPass)
		, LocalRenderSubPass(InLocalRenderSubPass)
	{
	}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandBeginComputePass final : public FRHICommand<FRHICommandBeginComputePass>
{
	const TCHAR* Name;

	FRHICommandBeginComputePass(const TCHAR* InName)
		: Name(InName)
	{
	}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandEndComputePass final : public FRHICommand<FRHICommandEndComputePass>
{
	FRHICommandEndComputePass()
	{
	}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandBindClearMRTValues final : public FRHICommand<FRHICommandBindClearMRTValues>
{
	bool bClearColor;
	bool bClearDepth;
	bool bClearStencil;

	FORCEINLINE_DEBUGGABLE FRHICommandBindClearMRTValues(
		bool InbClearColor,
		bool InbClearDepth,
		bool InbClearStencil
		) 
		: bClearColor(InbClearColor)
		, bClearDepth(InbClearDepth)
		, bClearStencil(InbClearStencil)
	{
	}	

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

template<ECmdList CmdListType>
struct FRHICommandSetComputeShader final : public FRHICommand<FRHICommandSetComputeShader<CmdListType>>
{
	FRHIComputeShader* ComputeShader;
	FORCEINLINE_DEBUGGABLE FRHICommandSetComputeShader(FRHIComputeShader* InComputeShader)
		: ComputeShader(InComputeShader)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

template<ECmdList CmdListType>
struct FRHICommandSetComputePipelineState final : public FRHICommand<FRHICommandSetComputePipelineState<CmdListType>>
{
	FComputePipelineState* ComputePipelineState;
	FORCEINLINE_DEBUGGABLE FRHICommandSetComputePipelineState(FComputePipelineState* InComputePipelineState)
		: ComputePipelineState(InComputePipelineState)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandSetGraphicsPipelineState final : public FRHICommand<FRHICommandSetGraphicsPipelineState>
{
	FGraphicsPipelineState* GraphicsPipelineState;
	FORCEINLINE_DEBUGGABLE FRHICommandSetGraphicsPipelineState(FGraphicsPipelineState* InGraphicsPipelineState)
		: GraphicsPipelineState(InGraphicsPipelineState)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

template<ECmdList CmdListType>
struct FRHICommandDispatchComputeShader final : public FRHICommand<FRHICommandDispatchComputeShader<CmdListType>>
{
	uint32 ThreadGroupCountX;
	uint32 ThreadGroupCountY;
	uint32 ThreadGroupCountZ;
	FORCEINLINE_DEBUGGABLE FRHICommandDispatchComputeShader(uint32 InThreadGroupCountX, uint32 InThreadGroupCountY, uint32 InThreadGroupCountZ)
		: ThreadGroupCountX(InThreadGroupCountX)
		, ThreadGroupCountY(InThreadGroupCountY)
		, ThreadGroupCountZ(InThreadGroupCountZ)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

template<ECmdList CmdListType>
struct FRHICommandDispatchIndirectComputeShader final : public FRHICommand<FRHICommandDispatchIndirectComputeShader<CmdListType>>
{
	FRHIVertexBuffer* ArgumentBuffer;
	uint32 ArgumentOffset;
	FORCEINLINE_DEBUGGABLE FRHICommandDispatchIndirectComputeShader(FRHIVertexBuffer* InArgumentBuffer, uint32 InArgumentOffset)
		: ArgumentBuffer(InArgumentBuffer)
		, ArgumentOffset(InArgumentOffset)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandAutomaticCacheFlushAfterComputeShader final : public FRHICommand<FRHICommandAutomaticCacheFlushAfterComputeShader>
{
	bool bEnable;
	FORCEINLINE_DEBUGGABLE FRHICommandAutomaticCacheFlushAfterComputeShader(bool InbEnable)
		: bEnable(InbEnable)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandFlushComputeShaderCache final : public FRHICommand<FRHICommandFlushComputeShaderCache>
{
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandDrawPrimitiveIndirect final : public FRHICommand<FRHICommandDrawPrimitiveIndirect>
{
	FRHIVertexBuffer* ArgumentBuffer;
	uint32 ArgumentOffset;
	FORCEINLINE_DEBUGGABLE FRHICommandDrawPrimitiveIndirect(FRHIVertexBuffer* InArgumentBuffer, uint32 InArgumentOffset)
		: ArgumentBuffer(InArgumentBuffer)
		, ArgumentOffset(InArgumentOffset)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandDrawIndexedIndirect final : public FRHICommand<FRHICommandDrawIndexedIndirect>
{
	FRHIIndexBuffer* IndexBufferRHI;
	FRHIStructuredBuffer* ArgumentsBufferRHI;
	uint32 DrawArgumentsIndex;
	uint32 NumInstances;

	FORCEINLINE_DEBUGGABLE FRHICommandDrawIndexedIndirect(FRHIIndexBuffer* InIndexBufferRHI, FRHIStructuredBuffer* InArgumentsBufferRHI, uint32 InDrawArgumentsIndex, uint32 InNumInstances)
		: IndexBufferRHI(InIndexBufferRHI)
		, ArgumentsBufferRHI(InArgumentsBufferRHI)
		, DrawArgumentsIndex(InDrawArgumentsIndex)
		, NumInstances(InNumInstances)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandDrawIndexedPrimitiveIndirect final : public FRHICommand<FRHICommandDrawIndexedPrimitiveIndirect>
{
	FRHIIndexBuffer* IndexBuffer;
	FRHIVertexBuffer* ArgumentsBuffer;
	uint32 ArgumentOffset;

	FORCEINLINE_DEBUGGABLE FRHICommandDrawIndexedPrimitiveIndirect(FRHIIndexBuffer* InIndexBuffer, FRHIVertexBuffer* InArgumentsBuffer, uint32 InArgumentOffset)
		: IndexBuffer(InIndexBuffer)
		, ArgumentsBuffer(InArgumentsBuffer)
		, ArgumentOffset(InArgumentOffset)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandSetDepthBounds final : public FRHICommand<FRHICommandSetDepthBounds>
{
	float MinDepth;
	float MaxDepth;

	FORCEINLINE_DEBUGGABLE FRHICommandSetDepthBounds(float InMinDepth, float InMaxDepth)
		: MinDepth(InMinDepth)
		, MaxDepth(InMaxDepth)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandClearTinyUAV final : public FRHICommand<FRHICommandClearTinyUAV>
{
	FRHIUnorderedAccessView* UnorderedAccessViewRHI;
	uint32 Values[4];

	FORCEINLINE_DEBUGGABLE FRHICommandClearTinyUAV(FRHIUnorderedAccessView* InUnorderedAccessViewRHI, const uint32* InValues)
		: UnorderedAccessViewRHI(InUnorderedAccessViewRHI)
	{
		Values[0] = InValues[0];
		Values[1] = InValues[1];
		Values[2] = InValues[2];
		Values[3] = InValues[3];
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandCopyToResolveTarget final : public FRHICommand<FRHICommandCopyToResolveTarget>
{
	FResolveParams ResolveParams;
	FRHITexture* SourceTexture;
	FRHITexture* DestTexture;

	FORCEINLINE_DEBUGGABLE FRHICommandCopyToResolveTarget(FRHITexture* InSourceTexture, FRHITexture* InDestTexture, const FResolveParams& InResolveParams)
		: ResolveParams(InResolveParams)
		, SourceTexture(InSourceTexture)
		, DestTexture(InDestTexture)
	{
		ensure(SourceTexture);
		ensure(DestTexture);
		ensure(SourceTexture->GetTexture2D() || SourceTexture->GetTexture3D() || SourceTexture->GetTextureCube() || SourceTexture->GetTexture2DArray());
		ensure(DestTexture->GetTexture2D() || DestTexture->GetTexture3D() || DestTexture->GetTextureCube() || DestTexture->GetTexture2DArray());
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandCopyTexture final : public FRHICommand<FRHICommandCopyTexture>
{
	FRHICopyTextureInfo CopyInfo;
	FRHITexture* SourceTexture;
	FRHITexture* DestTexture;

	FORCEINLINE_DEBUGGABLE FRHICommandCopyTexture(FRHITexture* InSourceTexture, FRHITexture* InDestTexture, const FRHICopyTextureInfo& InCopyInfo)
		: CopyInfo(InCopyInfo)
		, SourceTexture(InSourceTexture)
		, DestTexture(InDestTexture)
	{
		ensure(SourceTexture);
		ensure(DestTexture);
		ensure(SourceTexture->GetTexture2D() || SourceTexture->GetTexture2DArray() || SourceTexture->GetTexture3D() || SourceTexture->GetTextureCube());
		ensure(DestTexture->GetTexture2D() || DestTexture->GetTexture2DArray() || DestTexture->GetTexture3D() || DestTexture->GetTextureCube());
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandTransitionTextures final : public FRHICommand<FRHICommandTransitionTextures>
{
	int32 NumTextures;
	FRHITexture** Textures; // Pointer to an array of textures, allocated inline with the command list
	EResourceTransitionAccess TransitionType;
	FORCEINLINE_DEBUGGABLE FRHICommandTransitionTextures(EResourceTransitionAccess InTransitionType, FRHITexture** InTextures, int32 InNumTextures)
		: NumTextures(InNumTextures)
		, Textures(InTextures)
		, TransitionType(InTransitionType)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandTransitionTexturesArray final : public FRHICommand<FRHICommandTransitionTexturesArray>
{	
	TArray<FRHITexture*>& Textures;
	EResourceTransitionAccess TransitionType;
	FORCEINLINE_DEBUGGABLE FRHICommandTransitionTexturesArray(EResourceTransitionAccess InTransitionType, TArray<FRHITexture*>& InTextures)
		: Textures(InTextures)
		, TransitionType(InTransitionType)
	{		
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

template<ECmdList CmdListType>
struct FRHICommandTransitionUAVs final : public FRHICommand<FRHICommandTransitionUAVs<CmdListType>>
{
	int32 NumUAVs;
	FRHIUnorderedAccessView** UAVs; // Pointer to an array of UAVs, allocated inline with the command list
	EResourceTransitionAccess TransitionType;
	EResourceTransitionPipeline TransitionPipeline;
	FRHIComputeFence* WriteFence;

	FORCEINLINE_DEBUGGABLE FRHICommandTransitionUAVs(EResourceTransitionAccess InTransitionType, EResourceTransitionPipeline InTransitionPipeline, FRHIUnorderedAccessView** InUAVs, int32 InNumUAVs, FRHIComputeFence* InWriteFence)
		: NumUAVs(InNumUAVs)
		, UAVs(InUAVs)
		, TransitionType(InTransitionType)
		, TransitionPipeline(InTransitionPipeline)
		, WriteFence(InWriteFence)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

template<ECmdList CmdListType>
struct FRHICommandSetAsyncComputeBudget final : public FRHICommand<FRHICommandSetAsyncComputeBudget<CmdListType>>
{
	EAsyncComputeBudget Budget;

	FORCEINLINE_DEBUGGABLE FRHICommandSetAsyncComputeBudget(EAsyncComputeBudget InBudget)
		: Budget(InBudget)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

template<ECmdList CmdListType>
struct FRHICommandWaitComputeFence final : public FRHICommand<FRHICommandWaitComputeFence<CmdListType>>
{
	FRHIComputeFence* WaitFence;

	FORCEINLINE_DEBUGGABLE FRHICommandWaitComputeFence(FRHIComputeFence* InWaitFence)
		: WaitFence(InWaitFence)
	{		
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

template<ECmdList CmdListType>
struct FRHICommandCopyToStagingBuffer final : public FRHICommand<FRHICommandCopyToStagingBuffer<CmdListType>>
{
	FRHIVertexBuffer* SourceBuffer;
	FRHIStagingBuffer* DestinationStagingBuffer;
	uint32 Offset;
	uint32 NumBytes;

	FORCEINLINE_DEBUGGABLE FRHICommandCopyToStagingBuffer(FRHIVertexBuffer* InSourceBuffer, FRHIStagingBuffer* InDestinationStagingBuffer, uint32 InOffset, uint32 InNumBytes)
		: SourceBuffer(InSourceBuffer)
		, DestinationStagingBuffer(InDestinationStagingBuffer)
		, Offset(InOffset)
		, NumBytes(InNumBytes)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

template<ECmdList CmdListType>
struct FRHICommandWriteGPUFence final : public FRHICommand<FRHICommandWriteGPUFence<CmdListType>>
{
	FRHIGPUFence* Fence;

	FORCEINLINE_DEBUGGABLE FRHICommandWriteGPUFence(FRHIGPUFence* InFence)
		: Fence(InFence)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandClearColorTexture final : public FRHICommand<FRHICommandClearColorTexture>
{
	FRHITexture* Texture;
	FLinearColor Color;

	FORCEINLINE_DEBUGGABLE FRHICommandClearColorTexture(
		FRHITexture* InTexture,
		const FLinearColor& InColor
		)
		: Texture(InTexture)
		, Color(InColor)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandClearDepthStencilTexture final : public FRHICommand<FRHICommandClearDepthStencilTexture>
{
	FRHITexture* Texture;
	float Depth;
	uint32 Stencil;
	EClearDepthStencil ClearDepthStencil;

	FORCEINLINE_DEBUGGABLE FRHICommandClearDepthStencilTexture(
		FRHITexture* InTexture,
		EClearDepthStencil InClearDepthStencil,
		float InDepth,
		uint32 InStencil
	)
		: Texture(InTexture)
		, Depth(InDepth)
		, Stencil(InStencil)
		, ClearDepthStencil(InClearDepthStencil)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandClearColorTextures final : public FRHICommand<FRHICommandClearColorTextures>
{
	FLinearColor ColorArray[MaxSimultaneousRenderTargets];
	FRHITexture* Textures[MaxSimultaneousRenderTargets];
	int32 NumClearColors;

	FORCEINLINE_DEBUGGABLE FRHICommandClearColorTextures(
		int32 InNumClearColors,
		FRHITexture** InTextures,
		const FLinearColor* InColorArray
		)
		: NumClearColors(InNumClearColors)
	{
		check(InNumClearColors <= MaxSimultaneousRenderTargets);
		for (int32 Index = 0; Index < InNumClearColors; Index++)
		{
			ColorArray[Index] = InColorArray[Index];
			Textures[Index] = InTextures[Index];
		}
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FComputedGraphicsPipelineState
{
	FGraphicsPipelineStateRHIRef GraphicsPipelineState;
	int32 UseCount;
	FComputedGraphicsPipelineState()
		: UseCount(0)
	{
	}
};

struct FComputedUniformBuffer
{
	FUniformBufferRHIRef UniformBuffer;
	mutable int32 UseCount;
	FComputedUniformBuffer()
		: UseCount(0)
	{
	}
};

struct FLocalUniformBufferWorkArea
{
	void* Contents;
	const FRHIUniformBufferLayout* Layout;
	FComputedUniformBuffer* ComputedUniformBuffer;
#if DO_CHECK // the below variables are used in check(), which can be enabled in Shipping builds (see Build.h)
	FRHICommandListBase* CheckCmdList;
	int32 UID;
#endif

	FLocalUniformBufferWorkArea(FRHICommandListBase* InCheckCmdList, const void* InContents, uint32 ContentsSize, const FRHIUniformBufferLayout* InLayout)
		: Layout(InLayout)
#if DO_CHECK
		, CheckCmdList(InCheckCmdList)
		, UID(InCheckCmdList->GetUID())
#endif
	{
		check(ContentsSize);
		Contents = InCheckCmdList->Alloc(ContentsSize, SHADER_PARAMETER_STRUCT_ALIGNMENT);
		FMemory::Memcpy(Contents, InContents, ContentsSize);
		ComputedUniformBuffer = new (InCheckCmdList->Alloc<FComputedUniformBuffer>()) FComputedUniformBuffer;
	}
};

struct FLocalUniformBuffer
{
	FLocalUniformBufferWorkArea* WorkArea;
	FUniformBufferRHIRef BypassUniform; // this is only used in the case of Bypass, should eventually be deleted
	FLocalUniformBuffer()
		: WorkArea(nullptr)
	{
	}
	FLocalUniformBuffer(const FLocalUniformBuffer& Other)
		: WorkArea(Other.WorkArea)
		, BypassUniform(Other.BypassUniform)
	{
	}
	FORCEINLINE_DEBUGGABLE bool IsValid() const
	{
		return WorkArea || IsValidRef(BypassUniform);
	}
};

struct FRHICommandBuildLocalUniformBuffer final : public FRHICommand<FRHICommandBuildLocalUniformBuffer>
{
	FLocalUniformBufferWorkArea WorkArea;
	FORCEINLINE_DEBUGGABLE FRHICommandBuildLocalUniformBuffer(
		FRHICommandListBase* CheckCmdList,
		const void* Contents,
		uint32 ContentsSize,
		const FRHIUniformBufferLayout& Layout
		)
		: WorkArea(CheckCmdList, Contents, ContentsSize, &Layout)

	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

template <typename TRHIShader>
struct FRHICommandSetLocalUniformBuffer final : public FRHICommand<FRHICommandSetLocalUniformBuffer<TRHIShader> >
{
	TRHIShader* Shader;
	uint32 BaseIndex;
	FLocalUniformBuffer LocalUniformBuffer;
	FORCEINLINE_DEBUGGABLE FRHICommandSetLocalUniformBuffer(FRHICommandListBase* CheckCmdList, TRHIShader* InShader, uint32 InBaseIndex, const FLocalUniformBuffer& InLocalUniformBuffer)
		: Shader(InShader)
		, BaseIndex(InBaseIndex)
		, LocalUniformBuffer(InLocalUniformBuffer)

	{
		check(CheckCmdList == LocalUniformBuffer.WorkArea->CheckCmdList && CheckCmdList->GetUID() == LocalUniformBuffer.WorkArea->UID); // this uniform buffer was not built for this particular commandlist
		LocalUniformBuffer.WorkArea->ComputedUniformBuffer->UseCount++;
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandBeginRenderQuery final : public FRHICommand<FRHICommandBeginRenderQuery>
{
	FRHIRenderQuery* RenderQuery;

	FORCEINLINE_DEBUGGABLE FRHICommandBeginRenderQuery(FRHIRenderQuery* InRenderQuery)
		: RenderQuery(InRenderQuery)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandEndRenderQuery final : public FRHICommand<FRHICommandEndRenderQuery>
{
	FRHIRenderQuery* RenderQuery;

	FORCEINLINE_DEBUGGABLE FRHICommandEndRenderQuery(FRHIRenderQuery* InRenderQuery)
		: RenderQuery(InRenderQuery)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

template<ECmdList CmdListType>
struct FRHICommandSubmitCommandsHint final : public FRHICommand<FRHICommandSubmitCommandsHint<CmdListType>>
{
	FORCEINLINE_DEBUGGABLE FRHICommandSubmitCommandsHint()
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandPollOcclusionQueries final : public FRHICommand<FRHICommandPollOcclusionQueries>
{
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandBeginScene final : public FRHICommand<FRHICommandBeginScene>
{
	FORCEINLINE_DEBUGGABLE FRHICommandBeginScene()
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandEndScene final : public FRHICommand<FRHICommandEndScene>
{
	FORCEINLINE_DEBUGGABLE FRHICommandEndScene()
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandBeginFrame final : public FRHICommand<FRHICommandBeginFrame>
{
	FORCEINLINE_DEBUGGABLE FRHICommandBeginFrame()
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandEndFrame final : public FRHICommand<FRHICommandEndFrame>
{
	FORCEINLINE_DEBUGGABLE FRHICommandEndFrame()
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandBeginDrawingViewport final : public FRHICommand<FRHICommandBeginDrawingViewport>
{
	FRHIViewport* Viewport;
	FRHITexture* RenderTargetRHI;

	FORCEINLINE_DEBUGGABLE FRHICommandBeginDrawingViewport(FRHIViewport* InViewport, FRHITexture* InRenderTargetRHI)
		: Viewport(InViewport)
		, RenderTargetRHI(InRenderTargetRHI)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandEndDrawingViewport final : public FRHICommand<FRHICommandEndDrawingViewport>
{
	FRHIViewport* Viewport;
	bool bPresent;
	bool bLockToVsync;

	FORCEINLINE_DEBUGGABLE FRHICommandEndDrawingViewport(FRHIViewport* InViewport, bool InbPresent, bool InbLockToVsync)
		: Viewport(InViewport)
		, bPresent(InbPresent)
		, bLockToVsync(InbLockToVsync)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

template<ECmdList CmdListType>
struct FRHICommandPushEvent final : public FRHICommand<FRHICommandPushEvent<CmdListType>>
{
	const TCHAR *Name;
	FColor Color;

	FORCEINLINE_DEBUGGABLE FRHICommandPushEvent(const TCHAR *InName, FColor InColor)
		: Name(InName)
		, Color(InColor)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);

	virtual void StoreDebugInfo(FRHICommandListDebugContext& Context)
	{
		Context.PushMarker(Name);
	};
};

template<ECmdList CmdListType>
struct FRHICommandPopEvent final : public FRHICommand<FRHICommandPopEvent<CmdListType>>
{
	RHI_API void Execute(FRHICommandListBase& CmdList);

	virtual void StoreDebugInfo(FRHICommandListDebugContext& Context)
	{
		Context.PopMarker();
	};
};

struct FRHICommandInvalidateCachedState final : public FRHICommand<FRHICommandInvalidateCachedState>
{
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandDiscardRenderTargets final : public FRHICommand<FRHICommandDiscardRenderTargets>
{
	uint32 ColorBitMask;
	bool Depth;
	bool Stencil;

	FORCEINLINE_DEBUGGABLE FRHICommandDiscardRenderTargets(bool InDepth, bool InStencil, uint32 InColorBitMask)
		: ColorBitMask(InColorBitMask)
		, Depth(InDepth)
		, Stencil(InStencil)
	{
	}
	
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandDebugBreak final : public FRHICommand<FRHICommandDebugBreak>
{
	void Execute(FRHICommandListBase& CmdList)
	{
		if (FPlatformMisc::IsDebuggerPresent())
		{
			UE_DEBUG_BREAK();
		}
	}
};

struct FRHICommandUpdateTextureReference final : public FRHICommand<FRHICommandUpdateTextureReference>
{
	FRHITextureReference* TextureRef;
	FRHITexture* NewTexture;
	FORCEINLINE_DEBUGGABLE FRHICommandUpdateTextureReference(FRHITextureReference* InTextureRef, FRHITexture* InNewTexture)
		: TextureRef(InTextureRef)
		, NewTexture(InNewTexture)
	{
	}
	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHIShaderResourceViewUpdateInfo_VB
{
	FRHIShaderResourceView* SRV;
	FRHIVertexBuffer* VertexBuffer;
	uint32 Stride;
	uint8 Format;
};

struct FRHIVertexBufferUpdateInfo
{
	FRHIVertexBuffer* DestBuffer;
	FRHIVertexBuffer* SrcBuffer;
};

struct FRHIIndexBufferUpdateInfo
{
	FRHIIndexBuffer* DestBuffer;
	FRHIIndexBuffer* SrcBuffer;
};

struct FRHIResourceUpdateInfo
{
	enum EUpdateType
	{
		/** Take over underlying resource from an intermediate vertex buffer */
		UT_VertexBuffer,
		/** Take over underlying resource from an intermediate index buffer */
		UT_IndexBuffer,
		/** Update an SRV to view on a different vertex buffer */
		UT_VertexBufferSRV,
		/** Update an SRV to view on a different index buffer */
		UT_IndexBufferSRV,
		/** Number of update types */
		UT_Num
	};

	EUpdateType Type;
	union
	{
		FRHIVertexBufferUpdateInfo VertexBuffer;
		FRHIIndexBufferUpdateInfo IndexBuffer;
		FRHIShaderResourceViewUpdateInfo_VB VertexBufferSRV;
	};

	void ReleaseRefs();
};

struct FRHICommandUpdateRHIResources final : public FRHICommand<FRHICommandUpdateRHIResources>
{
	FRHIResourceUpdateInfo* UpdateInfos;
	int32 Num;
	bool bNeedReleaseRefs;

	FRHICommandUpdateRHIResources(FRHIResourceUpdateInfo* InUpdateInfos, int32 InNum, bool bInNeedReleaseRefs)
		: UpdateInfos(InUpdateInfos)
		, Num(InNum)
		, bNeedReleaseRefs(bInNeedReleaseRefs)
	{}

	~FRHICommandUpdateRHIResources();

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

#if RHI_RAYTRACING
struct FRHICommandCopyBufferRegion final : public FRHICommand<FRHICommandCopyBufferRegion>
{
	FRHIVertexBuffer* DestBuffer;
	uint64 DstOffset;
	FRHIVertexBuffer* SourceBuffer;
	uint64 SrcOffset;
	uint64 NumBytes;

	explicit FRHICommandCopyBufferRegion(FRHIVertexBuffer* InDestBuffer, uint64 InDstOffset, FRHIVertexBuffer* InSourceBuffer, uint64 InSrcOffset, uint64 InNumBytes)
		: DestBuffer(InDestBuffer)
		, DstOffset(InDstOffset)
		, SourceBuffer(InSourceBuffer)
		, SrcOffset(InSrcOffset)
		, NumBytes(InNumBytes)
	{}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandCopyBufferRegions final : public FRHICommand<FRHICommandCopyBufferRegions>
{
	const TArrayView<const FCopyBufferRegionParams> Params;

	explicit FRHICommandCopyBufferRegions(const TArrayView<const FCopyBufferRegionParams> InParams)
		: Params(InParams)
	{}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandBuildAccelerationStructure final : public FRHICommand<FRHICommandBuildAccelerationStructure>
{
	FRHIRayTracingGeometry* Geometry;
	FRHIRayTracingScene* Scene;

	explicit FRHICommandBuildAccelerationStructure(FRHIRayTracingGeometry* InGeometry)
		: Geometry(InGeometry)
		, Scene(nullptr)
	{}

	explicit FRHICommandBuildAccelerationStructure(FRHIRayTracingScene* InScene)
		: Geometry(nullptr)
		, Scene(InScene)
	{}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandClearRayTracingBindings final : public FRHICommand<FRHICommandClearRayTracingBindings>
{
	FRHIRayTracingScene* Scene;

	explicit FRHICommandClearRayTracingBindings(FRHIRayTracingScene* InScene)
		: Scene(InScene)
	{}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};


struct FRHICommandUpdateAccelerationStructures final : public FRHICommand<FRHICommandUpdateAccelerationStructures>
{
	const TArrayView<const FAccelerationStructureUpdateParams> UpdateParams;

	explicit FRHICommandUpdateAccelerationStructures(const TArrayView<const FAccelerationStructureUpdateParams> InParams)
		: UpdateParams(InParams)
	{}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandBuildAccelerationStructures final : public FRHICommand<FRHICommandBuildAccelerationStructures>
{
	const TArrayView<const FAccelerationStructureUpdateParams> UpdateParams;

	explicit FRHICommandBuildAccelerationStructures(const TArrayView<const FAccelerationStructureUpdateParams> InParams)
		: UpdateParams(InParams)
	{}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandRayTraceOcclusion final : public FRHICommand<FRHICommandRayTraceOcclusion>
{
	FRHIRayTracingScene* Scene;
	FRHIShaderResourceView* Rays;
	FRHIUnorderedAccessView* Output;
	uint32 NumRays;

	FRHICommandRayTraceOcclusion(FRHIRayTracingScene* InScene,
		FRHIShaderResourceView* InRays,
		FRHIUnorderedAccessView* InOutput,
		uint32 InNumRays)
		: Scene(InScene)
		, Rays(InRays)
		, Output(InOutput)
		, NumRays(InNumRays)
	{}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandRayTraceIntersection final : public FRHICommand<FRHICommandRayTraceIntersection>
{
	FRHIRayTracingScene* Scene;
	FRHIShaderResourceView* Rays;
	FRHIUnorderedAccessView* Output;
	uint32 NumRays;

	FRHICommandRayTraceIntersection(FRHIRayTracingScene* InScene,
		FRHIShaderResourceView* InRays,
		FRHIUnorderedAccessView* InOutput,
		uint32 InNumRays)
		: Scene(InScene)
		, Rays(InRays)
		, Output(InOutput)
		, NumRays(InNumRays)
	{}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandRayTraceDispatch final : public FRHICommand<FRHICommandRayTraceDispatch>
{
	FRayTracingPipelineState* Pipeline;
	FRHIRayTracingScene* Scene;
	FRayTracingShaderBindings GlobalResourceBindings;
	FRHIRayTracingShader* RayGenShader;
	uint32 Width;
	uint32 Height;

	FRHICommandRayTraceDispatch(FRayTracingPipelineState* InPipeline, FRHIRayTracingShader* InRayGenShader, FRHIRayTracingScene* InScene, const FRayTracingShaderBindings& InGlobalResourceBindings, uint32 InWidth, uint32 InHeight)
		: Pipeline(InPipeline)
		, Scene(InScene)
		, GlobalResourceBindings(InGlobalResourceBindings)
		, RayGenShader(InRayGenShader)
		, Width(InWidth)
		, Height(InHeight)
	{}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};

struct FRHICommandSetRayTracingBindings final : public FRHICommand<FRHICommandSetRayTracingBindings>
{
	enum EBindingType
	{
		EBindingType_HitGroup,
		EBindingType_CallableShader,
	};

	FRHIRayTracingScene* Scene;
	uint32 InstanceIndex;
	uint32 SegmentIndex;
	uint32 ShaderSlot;
	FRayTracingPipelineState* Pipeline;
	uint32 ShaderIndex;
	uint32 NumUniformBuffers;
	FRHIUniformBuffer* const* UniformBuffers; // Pointer to an array of uniform buffers, allocated inline within the command list
	uint32 LooseParameterDataSize;
	const void* LooseParameterData;
	uint32 UserData;
	EBindingType BindingType;

	// Hit group bindings
	FRHICommandSetRayTracingBindings(FRHIRayTracingScene* InScene, uint32 InInstanceIndex, uint32 InSegmentIndex, uint32 InShaderSlot,
		FRayTracingPipelineState* InPipeline, uint32 InHitGroupIndex, uint32 InNumUniformBuffers, FRHIUniformBuffer* const* InUniformBuffers,
		uint32 InLooseParameterDataSize, const void* InLooseParameterData,
		uint32 InUserData)
		: Scene(InScene)
		, InstanceIndex(InInstanceIndex)
		, SegmentIndex(InSegmentIndex)
		, ShaderSlot(InShaderSlot)
		, Pipeline(InPipeline)
		, ShaderIndex(InHitGroupIndex)
		, NumUniformBuffers(InNumUniformBuffers)
		, UniformBuffers(InUniformBuffers)
		, LooseParameterDataSize(InLooseParameterDataSize)
		, LooseParameterData(InLooseParameterData)
		, UserData(InUserData)
		, BindingType(EBindingType_HitGroup)
	{
	}

	// Callable shader bindings
	FRHICommandSetRayTracingBindings(FRHIRayTracingScene* InScene, uint32 InShaderSlot,
		FRayTracingPipelineState* InPipeline, uint32 InShaderIndex,
		uint32 InNumUniformBuffers, FRHIUniformBuffer* const* InUniformBuffers,
		uint32 InUserData)
		: Scene(InScene)
		, InstanceIndex(0)
		, SegmentIndex(0)
		, ShaderSlot(InShaderSlot)
		, Pipeline(InPipeline)
		, ShaderIndex(InShaderIndex)
		, NumUniformBuffers(InNumUniformBuffers)
		, UniformBuffers(InUniformBuffers)
		, UserData(InUserData)
		, BindingType(EBindingType_CallableShader)
	{
	}

	RHI_API void Execute(FRHICommandListBase& CmdList);
};
#endif // RHI_RAYTRACING

// Using variadic macro because some types are fancy template<A,B> stuff, which gets broken off at the comma and interpreted as multiple arguments. 
#define ALLOC_COMMAND(...) new ( AllocCommand(sizeof(__VA_ARGS__), alignof(__VA_ARGS__)) ) __VA_ARGS__
#define ALLOC_COMMAND_CL(RHICmdList, ...) new ( (RHICmdList).AllocCommand(sizeof(__VA_ARGS__), alignof(__VA_ARGS__)) ) __VA_ARGS__

template<> void FRHICommandSetShaderParameter<FRHIComputeShader, ECmdList::ECompute>::Execute(FRHICommandListBase& CmdList);
template<> void FRHICommandSetShaderUniformBuffer<FRHIComputeShader, ECmdList::ECompute>::Execute(FRHICommandListBase& CmdList);
template<> void FRHICommandSetShaderTexture<FRHIComputeShader, ECmdList::ECompute>::Execute(FRHICommandListBase& CmdList);
template<> void FRHICommandSetShaderResourceViewParameter<FRHIComputeShader, ECmdList::ECompute>::Execute(FRHICommandListBase& CmdList);
template<> void FRHICommandSetShaderSampler<FRHIComputeShader, ECmdList::ECompute>::Execute(FRHICommandListBase& CmdList);


class RHI_API FRHICommandList : public FRHICommandListBase
{
public:

	FORCEINLINE FRHICommandList(FRHIGPUMask GPUMask) : FRHICommandListBase(GPUMask) {}

	bool AsyncPSOCompileAllowed() const
	{
		return bAsyncPSOCompileAllowed;
	}

	/** Custom new/delete with recycling */
	void* operator new(size_t Size);
	void operator delete(void *RawMemory);
	
	FORCEINLINE_DEBUGGABLE void BeginUpdateMultiFrameResource(FRHITexture* Texture)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHIBeginUpdateMultiFrameResource( Texture);
			return;
		}
		ALLOC_COMMAND(FRHICommandBeginUpdateMultiFrameResource)(Texture);
	}

	FORCEINLINE_DEBUGGABLE void EndUpdateMultiFrameResource(FRHITexture* Texture)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHIEndUpdateMultiFrameResource(Texture);
			return;
		}
		ALLOC_COMMAND(FRHICommandEndUpdateMultiFrameResource)(Texture);
	}

	FORCEINLINE_DEBUGGABLE void BeginUpdateMultiFrameResource(FRHIUnorderedAccessView* UAV)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHIBeginUpdateMultiFrameResource(UAV);
			return;
		}
		ALLOC_COMMAND(FRHICommandBeginUpdateMultiFrameUAV)(UAV);
	}

	FORCEINLINE_DEBUGGABLE void EndUpdateMultiFrameResource(FRHIUnorderedAccessView* UAV)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHIEndUpdateMultiFrameResource(UAV);
			return;
		}
		ALLOC_COMMAND(FRHICommandEndUpdateMultiFrameUAV)(UAV);
	}

	FORCEINLINE_DEBUGGABLE FLocalUniformBuffer BuildLocalUniformBuffer(const void* Contents, uint32 ContentsSize, const FRHIUniformBufferLayout& Layout)
	{
		//check(IsOutsideRenderPass());
		FLocalUniformBuffer Result;
		if (Bypass())
		{
			Result.BypassUniform = RHICreateUniformBuffer(Contents, Layout, UniformBuffer_SingleFrame);
		}
		else
		{
			check(Contents && ContentsSize && (&Layout != nullptr));
			auto* Cmd = ALLOC_COMMAND(FRHICommandBuildLocalUniformBuffer)(this, Contents, ContentsSize, Layout);
			Result.WorkArea = &Cmd->WorkArea;
		}
		return Result;
	}

	template <typename TRHIShader>
	FORCEINLINE_DEBUGGABLE void SetLocalShaderUniformBuffer(TRHIShader* Shader, uint32 BaseIndex, const FLocalUniformBuffer& UniformBuffer)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHISetShaderUniformBuffer(Shader, BaseIndex, UniformBuffer.BypassUniform);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetLocalUniformBuffer<TRHIShader>)(this, Shader, BaseIndex, UniformBuffer);
	}

	template <typename TShaderRHI>
	FORCEINLINE_DEBUGGABLE void SetLocalShaderUniformBuffer(TRefCountPtr<TShaderRHI>& Shader, uint32 BaseIndex, const FLocalUniformBuffer& UniformBuffer)
	{
		SetLocalShaderUniformBuffer(Shader.GetReference(), BaseIndex, UniformBuffer);
	}

	template <typename TShaderRHI>
	FORCEINLINE_DEBUGGABLE void SetShaderUniformBuffer(TShaderRHI* Shader, uint32 BaseIndex, FRHIUniformBuffer* UniformBuffer)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHISetShaderUniformBuffer(Shader, BaseIndex, UniformBuffer);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetShaderUniformBuffer<TShaderRHI, ECmdList::EGfx>)(Shader, BaseIndex, UniformBuffer);
	}
	template <typename TShaderRHI>
	FORCEINLINE void SetShaderUniformBuffer(TRefCountPtr<TShaderRHI>& Shader, uint32 BaseIndex, FRHIUniformBuffer* UniformBuffer)
	{
		SetShaderUniformBuffer(Shader.GetReference(), BaseIndex, UniformBuffer);
	}

	template <typename TShaderRHI>
	FORCEINLINE_DEBUGGABLE void SetShaderParameter(TShaderRHI* Shader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHISetShaderParameter(Shader, BufferIndex, BaseIndex, NumBytes, NewValue);
			return;
		}
		void* UseValue = Alloc(NumBytes, 16);
		FMemory::Memcpy(UseValue, NewValue, NumBytes);
		ALLOC_COMMAND(FRHICommandSetShaderParameter<TShaderRHI, ECmdList::EGfx>)(Shader, BufferIndex, BaseIndex, NumBytes, UseValue);
	}
	template <typename TShaderRHI>
	FORCEINLINE void SetShaderParameter(TRefCountPtr<TShaderRHI>& Shader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue)
	{
		SetShaderParameter(Shader.GetReference(), BufferIndex, BaseIndex, NumBytes, NewValue);
	}

	template <typename TRHIShader>
	FORCEINLINE_DEBUGGABLE void SetShaderTexture(TRHIShader* Shader, uint32 TextureIndex, FRHITexture* Texture)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHISetShaderTexture(Shader, TextureIndex, Texture);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetShaderTexture<TRHIShader, ECmdList::EGfx>)(Shader, TextureIndex, Texture);
	}

	template <typename TShaderRHI>
	FORCEINLINE_DEBUGGABLE void SetShaderTexture(TRefCountPtr<TShaderRHI>& Shader, uint32 TextureIndex, FRHITexture* Texture)
	{
		SetShaderTexture(Shader.GetReference(), TextureIndex, Texture);
	}

	template <typename TShaderRHI>
	FORCEINLINE_DEBUGGABLE void SetShaderTexture(const TShaderRHI* Shader, uint32 TextureIndex, FRHITexture* Texture)
	{
		SetShaderTexture((TShaderRHI*)Shader, TextureIndex, Texture);
	}

	template <typename TRHIShader>
	FORCEINLINE_DEBUGGABLE void SetShaderResourceViewParameter(TRHIShader* Shader, uint32 SamplerIndex, FRHIShaderResourceView* SRV)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHISetShaderResourceViewParameter(Shader, SamplerIndex, SRV);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetShaderResourceViewParameter<TRHIShader, ECmdList::EGfx>)(Shader, SamplerIndex, SRV);
	}

	template <typename TShaderRHI>
	FORCEINLINE_DEBUGGABLE void SetShaderResourceViewParameter(TRefCountPtr<TShaderRHI>& Shader, uint32 SamplerIndex, FRHIShaderResourceView* SRV)
	{
		SetShaderResourceViewParameter(Shader.GetReference(), SamplerIndex, SRV);
	}

	template <typename TRHIShader>
	FORCEINLINE_DEBUGGABLE void SetShaderSampler(TRHIShader* Shader, uint32 SamplerIndex, FRHISamplerState* State)
	{
		//check(IsOutsideRenderPass());
		
		// Immutable samplers can't be set dynamically
		check(!State->IsImmutable());
		if (State->IsImmutable())
		{
			return;
		}

		if (Bypass())
		{
			GetContext().RHISetShaderSampler(Shader, SamplerIndex, State);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetShaderSampler<TRHIShader, ECmdList::EGfx>)(Shader, SamplerIndex, State);
	}

	template <typename TShaderRHI>
	FORCEINLINE_DEBUGGABLE void SetShaderSampler(TRefCountPtr<TShaderRHI>& Shader, uint32 SamplerIndex, FRHISamplerState* State)
	{
		SetShaderSampler(Shader.GetReference(), SamplerIndex, State);
	}

	FORCEINLINE_DEBUGGABLE void SetUAVParameter(FRHIComputeShader* Shader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV)
	{
		if (Bypass())
		{
			GetContext().RHISetUAVParameter(Shader, UAVIndex, UAV);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetUAVParameter<FRHIComputeShader, ECmdList::EGfx>)(Shader, UAVIndex, UAV);
	}

	FORCEINLINE_DEBUGGABLE void SetUAVParameter(TRefCountPtr<FRHIComputeShader>& Shader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV)
	{
		SetUAVParameter(Shader.GetReference(), UAVIndex, UAV);
	}

	FORCEINLINE_DEBUGGABLE void SetUAVParameter(FRHIComputeShader* Shader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV, uint32 InitialCount)
	{
		if (Bypass())
		{
			GetContext().RHISetUAVParameter(Shader, UAVIndex, UAV, InitialCount);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetUAVParameter_IntialCount<FRHIComputeShader, ECmdList::EGfx>)(Shader, UAVIndex, UAV, InitialCount);
	}

	FORCEINLINE_DEBUGGABLE void SetUAVParameter(TRefCountPtr<FRHIComputeShader>& Shader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV, uint32 InitialCount)
	{
		SetUAVParameter(Shader.GetReference(), UAVIndex, UAV, InitialCount);
	}

	FORCEINLINE_DEBUGGABLE void SetBlendFactor(const FLinearColor& BlendFactor = FLinearColor::White)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHISetBlendFactor(BlendFactor);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetBlendFactor)(BlendFactor);
	}

	FORCEINLINE_DEBUGGABLE void DrawPrimitive(uint32 BaseVertexIndex, uint32 NumPrimitives, uint32 NumInstances)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHIDrawPrimitive(BaseVertexIndex, NumPrimitives, NumInstances);
			return;
		}
		ALLOC_COMMAND(FRHICommandDrawPrimitive)(BaseVertexIndex, NumPrimitives, NumInstances);
	}

	FORCEINLINE_DEBUGGABLE void DrawIndexedPrimitive(FRHIIndexBuffer* IndexBuffer, int32 BaseVertexIndex, uint32 FirstInstance, uint32 NumVertices, uint32 StartIndex, uint32 NumPrimitives, uint32 NumInstances)
	{
		if (!IndexBuffer)
		{
			UE_LOG(LogRHI, Fatal, TEXT("Tried to call DrawIndexedPrimitive with null IndexBuffer!"));
		}

		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHIDrawIndexedPrimitive(IndexBuffer, BaseVertexIndex, FirstInstance, NumVertices, StartIndex, NumPrimitives, NumInstances);
			return;
		}
		ALLOC_COMMAND(FRHICommandDrawIndexedPrimitive)(IndexBuffer, BaseVertexIndex, FirstInstance, NumVertices, StartIndex, NumPrimitives, NumInstances);
	}

	FORCEINLINE_DEBUGGABLE void SetStreamSource(uint32 StreamIndex, FRHIVertexBuffer* VertexBuffer, uint32 Offset)
	{
		if (Bypass())
		{
			GetContext().RHISetStreamSource(StreamIndex, VertexBuffer, Offset);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetStreamSource)(StreamIndex, VertexBuffer, Offset);
	}

	FORCEINLINE_DEBUGGABLE void SetStencilRef(uint32 StencilRef)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHISetStencilRef(StencilRef);
			return;
		}

		ALLOC_COMMAND(FRHICommandSetStencilRef)(StencilRef);
	}

	FORCEINLINE_DEBUGGABLE void SetViewport(uint32 MinX, uint32 MinY, float MinZ, uint32 MaxX, uint32 MaxY, float MaxZ)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHISetViewport(MinX, MinY, MinZ, MaxX, MaxY, MaxZ);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetViewport)(MinX, MinY, MinZ, MaxX, MaxY, MaxZ);
	}

	FORCEINLINE_DEBUGGABLE void SetStereoViewport(uint32 LeftMinX, uint32 RightMinX, uint32 LeftMinY, uint32 RightMinY, float MinZ, uint32 LeftMaxX, uint32 RightMaxX, uint32 LeftMaxY, uint32 RightMaxY, float MaxZ)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHISetStereoViewport(LeftMinX, RightMinX, LeftMinY, RightMinY, MinZ, LeftMaxX, RightMaxX, LeftMaxY, RightMaxY, MaxZ);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetStereoViewport)(LeftMinX, RightMinX, LeftMinY, RightMinY, MinZ, LeftMaxX, RightMaxX, LeftMaxY, RightMaxY, MaxZ);
	}

	FORCEINLINE_DEBUGGABLE void SetScissorRect(bool bEnable, uint32 MinX, uint32 MinY, uint32 MaxX, uint32 MaxY)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHISetScissorRect(bEnable, MinX, MinY, MaxX, MaxY);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetScissorRect)(bEnable, MinX, MinY, MaxX, MaxY);
	}

	void ApplyCachedRenderTargets(
		FGraphicsPipelineStateInitializer& GraphicsPSOInit
		)
	{
		GraphicsPSOInit.RenderTargetsEnabled = PSOContext.CachedNumSimultanousRenderTargets;

		for (uint32 i = 0; i < GraphicsPSOInit.RenderTargetsEnabled; ++i)
		{
			if (PSOContext.CachedRenderTargets[i].Texture)
			{
				GraphicsPSOInit.RenderTargetFormats[i] = PSOContext.CachedRenderTargets[i].Texture->GetFormat();
				GraphicsPSOInit.RenderTargetFlags[i] = PSOContext.CachedRenderTargets[i].Texture->GetFlags();
				const FRHITexture2DArray* TextureArray = PSOContext.CachedRenderTargets[i].Texture->GetTexture2DArray();
				GraphicsPSOInit.bMultiView = TextureArray && TextureArray->GetSizeZ() > 1;
			}
			else
			{
				GraphicsPSOInit.RenderTargetFormats[i] = PF_Unknown;
			}

			if (GraphicsPSOInit.RenderTargetFormats[i] != PF_Unknown)
			{
				GraphicsPSOInit.NumSamples = PSOContext.CachedRenderTargets[i].Texture->GetNumSamples();
			}
		}

		if (PSOContext.CachedDepthStencilTarget.Texture)
		{
			GraphicsPSOInit.DepthStencilTargetFormat = PSOContext.CachedDepthStencilTarget.Texture->GetFormat();
			GraphicsPSOInit.DepthStencilTargetFlag = PSOContext.CachedDepthStencilTarget.Texture->GetFlags();
			const FRHITexture2DArray* TextureArray = PSOContext.CachedDepthStencilTarget.Texture->GetTexture2DArray();
			GraphicsPSOInit.bMultiView = TextureArray && TextureArray->GetSizeZ() > 1;
		}
		else
		{
			GraphicsPSOInit.DepthStencilTargetFormat = PF_Unknown;
		}

		GraphicsPSOInit.DepthTargetLoadAction = PSOContext.CachedDepthStencilTarget.DepthLoadAction;
		GraphicsPSOInit.DepthTargetStoreAction = PSOContext.CachedDepthStencilTarget.DepthStoreAction;
		GraphicsPSOInit.StencilTargetLoadAction = PSOContext.CachedDepthStencilTarget.StencilLoadAction;
		GraphicsPSOInit.StencilTargetStoreAction = PSOContext.CachedDepthStencilTarget.GetStencilStoreAction();
		GraphicsPSOInit.DepthStencilAccess = PSOContext.CachedDepthStencilTarget.GetDepthStencilAccess();

		if (GraphicsPSOInit.DepthStencilTargetFormat != PF_Unknown)
		{
			GraphicsPSOInit.NumSamples = PSOContext.CachedDepthStencilTarget.Texture->GetNumSamples();
		}

		GraphicsPSOInit.SubpassHint = PSOContext.SubpassHint;
		GraphicsPSOInit.SubpassIndex = PSOContext.SubpassIndex;
	}

	UE_DEPRECATED(4.22, "SetRenderTargets API is deprecated; please use RHIBegin/EndRenderPass instead.")
	FORCEINLINE_DEBUGGABLE void SetRenderTargets(
		uint32 NewNumSimultaneousRenderTargets,
		const FRHIRenderTargetView* NewRenderTargetsRHI,
		const FRHIDepthRenderTargetView* NewDepthStencilTargetRHI,
		uint32 NewNumUAVs,
		FRHIUnorderedAccessView* const* UAVs
		)
	{
		check(IsOutsideRenderPass());
		CacheActiveRenderTargets(
			NewNumSimultaneousRenderTargets, 
			NewRenderTargetsRHI, 
			NewDepthStencilTargetRHI
			);

		if (Bypass())
		{
			GetContext().RHISetRenderTargets(
				NewNumSimultaneousRenderTargets,
				NewRenderTargetsRHI,
				NewDepthStencilTargetRHI,
				NewNumUAVs,
				UAVs);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetRenderTargets)(
			NewNumSimultaneousRenderTargets,
			NewRenderTargetsRHI,
			NewDepthStencilTargetRHI,
			NewNumUAVs,
			UAVs);
	}

	FORCEINLINE_DEBUGGABLE void BindClearMRTValues(bool bClearColor, bool bClearDepth, bool bClearStencil)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHIBindClearMRTValues(bClearColor, bClearDepth, bClearStencil);
			return;
		}
		ALLOC_COMMAND(FRHICommandBindClearMRTValues)(bClearColor, bClearDepth, bClearStencil);
	}	

	FORCEINLINE_DEBUGGABLE void SetComputeShader(FRHIComputeShader* ComputeShader)
	{
		ComputeShader->UpdateStats();
		if (Bypass())
		{
			GetContext().RHISetComputeShader(ComputeShader);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetComputeShader<ECmdList::EGfx>)(ComputeShader);
	}

	FORCEINLINE_DEBUGGABLE void SetComputePipelineState(class FComputePipelineState* ComputePipelineState)
	{
		if (Bypass())
		{
			extern RHI_API FRHIComputePipelineState* ExecuteSetComputePipelineState(class FComputePipelineState* ComputePipelineState);
			FRHIComputePipelineState* RHIComputePipelineState = ExecuteSetComputePipelineState(ComputePipelineState);
			GetContext().RHISetComputePipelineState(RHIComputePipelineState);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetComputePipelineState<ECmdList::EGfx>)(ComputePipelineState);
	}

	FORCEINLINE_DEBUGGABLE void SetGraphicsPipelineState(class FGraphicsPipelineState* GraphicsPipelineState)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			extern RHI_API FRHIGraphicsPipelineState* ExecuteSetGraphicsPipelineState(class FGraphicsPipelineState* GraphicsPipelineState);
			FRHIGraphicsPipelineState* RHIGraphicsPipelineState = ExecuteSetGraphicsPipelineState(GraphicsPipelineState);
			GetContext().RHISetGraphicsPipelineState(RHIGraphicsPipelineState);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetGraphicsPipelineState)(GraphicsPipelineState);
	}

	FORCEINLINE_DEBUGGABLE void DispatchComputeShader(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ)
	{
		if (Bypass())
		{
			GetContext().RHIDispatchComputeShader(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
			return;
		}
		ALLOC_COMMAND(FRHICommandDispatchComputeShader<ECmdList::EGfx>)(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
	}

	FORCEINLINE_DEBUGGABLE void DispatchIndirectComputeShader(FRHIVertexBuffer* ArgumentBuffer, uint32 ArgumentOffset)
	{
		if (Bypass())
		{
			GetContext().RHIDispatchIndirectComputeShader(ArgumentBuffer, ArgumentOffset);
			return;
		}
		ALLOC_COMMAND(FRHICommandDispatchIndirectComputeShader<ECmdList::EGfx>)(ArgumentBuffer, ArgumentOffset);
	}

	FORCEINLINE_DEBUGGABLE void AutomaticCacheFlushAfterComputeShader(bool bEnable)
	{
		if (Bypass())
		{
			GetContext().RHIAutomaticCacheFlushAfterComputeShader(bEnable);
			return;
		}
		ALLOC_COMMAND(FRHICommandAutomaticCacheFlushAfterComputeShader)(bEnable);
	}

	FORCEINLINE_DEBUGGABLE void FlushComputeShaderCache()
	{
		if (Bypass())
		{
			GetContext().RHIFlushComputeShaderCache();
			return;
		}
		ALLOC_COMMAND(FRHICommandFlushComputeShaderCache)();
	}

	FORCEINLINE_DEBUGGABLE void DrawPrimitiveIndirect(FRHIVertexBuffer* ArgumentBuffer, uint32 ArgumentOffset)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHIDrawPrimitiveIndirect(ArgumentBuffer, ArgumentOffset);
			return;
		}
		ALLOC_COMMAND(FRHICommandDrawPrimitiveIndirect)(ArgumentBuffer, ArgumentOffset);
	}

	FORCEINLINE_DEBUGGABLE void DrawIndexedIndirect(FRHIIndexBuffer* IndexBufferRHI, FRHIStructuredBuffer* ArgumentsBufferRHI, uint32 DrawArgumentsIndex, uint32 NumInstances)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHIDrawIndexedIndirect(IndexBufferRHI, ArgumentsBufferRHI, DrawArgumentsIndex, NumInstances);
			return;
		}
		ALLOC_COMMAND(FRHICommandDrawIndexedIndirect)(IndexBufferRHI, ArgumentsBufferRHI, DrawArgumentsIndex, NumInstances);
	}

	FORCEINLINE_DEBUGGABLE void DrawIndexedPrimitiveIndirect(FRHIIndexBuffer* IndexBuffer, FRHIVertexBuffer* ArgumentsBuffer, uint32 ArgumentOffset)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHIDrawIndexedPrimitiveIndirect(IndexBuffer, ArgumentsBuffer, ArgumentOffset);
			return;
		}
		ALLOC_COMMAND(FRHICommandDrawIndexedPrimitiveIndirect)(IndexBuffer, ArgumentsBuffer, ArgumentOffset);
	}

	FORCEINLINE_DEBUGGABLE void SetDepthBounds(float MinDepth, float MaxDepth)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHISetDepthBounds(MinDepth, MaxDepth);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetDepthBounds)(MinDepth, MaxDepth);
	}

	FORCEINLINE_DEBUGGABLE void CopyToResolveTarget(FRHITexture* SourceTextureRHI, FRHITexture* DestTextureRHI, const FResolveParams& ResolveParams)
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHICopyToResolveTarget(SourceTextureRHI, DestTextureRHI, ResolveParams);
			return;
		}
		ALLOC_COMMAND(FRHICommandCopyToResolveTarget)(SourceTextureRHI, DestTextureRHI, ResolveParams);
	}

	FORCEINLINE_DEBUGGABLE void CopyTexture(FRHITexture* SourceTextureRHI, FRHITexture* DestTextureRHI, const FRHICopyTextureInfo& CopyInfo)
	{
		check(IsOutsideRenderPass());
		if (GRHISupportsCopyToTextureMultipleMips)
		{
			if (Bypass())
			{
				GetContext().RHICopyTexture(SourceTextureRHI, DestTextureRHI, CopyInfo);
				return;
			}
			ALLOC_COMMAND(FRHICommandCopyTexture)(SourceTextureRHI, DestTextureRHI, CopyInfo);
		}
		else
		{
			FRHICopyTextureInfo PerMipInfo = CopyInfo;
			PerMipInfo.NumMips = 1;
			for (uint32 MipIndex = 0; MipIndex < CopyInfo.NumMips; MipIndex++)
			{
				if (Bypass())
				{
					GetContext().RHICopyTexture(SourceTextureRHI, DestTextureRHI, PerMipInfo);
				}
				else
				{
					ALLOC_COMMAND(FRHICommandCopyTexture)(SourceTextureRHI, DestTextureRHI, PerMipInfo);
				}

				++PerMipInfo.SourceMipIndex;
				++PerMipInfo.DestMipIndex;
				PerMipInfo.Size.X = FMath::Max(1, PerMipInfo.Size.X / 2);
				PerMipInfo.Size.Y = FMath::Max(1, PerMipInfo.Size.Y / 2);
			}
		}
	}

	FORCEINLINE_DEBUGGABLE void ClearTinyUAV(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const uint32(&Values)[4])
	{
		//check(IsOutsideRenderPass());
		if (Bypass())
		{
			GetContext().RHIClearTinyUAV(UnorderedAccessViewRHI, Values);
			return;
		}
		ALLOC_COMMAND(FRHICommandClearTinyUAV)(UnorderedAccessViewRHI, Values);
	}

	FORCEINLINE_DEBUGGABLE void BeginRenderQuery(FRHIRenderQuery* RenderQuery)
	{
		if (Bypass())
		{
			GetContext().RHIBeginRenderQuery(RenderQuery);
			return;
		}
		ALLOC_COMMAND(FRHICommandBeginRenderQuery)(RenderQuery);
	}
	FORCEINLINE_DEBUGGABLE void EndRenderQuery(FRHIRenderQuery* RenderQuery)
	{
		if (Bypass())
		{
			GetContext().RHIEndRenderQuery(RenderQuery);
			return;
		}
		ALLOC_COMMAND(FRHICommandEndRenderQuery)(RenderQuery);
	}

	FORCEINLINE_DEBUGGABLE void SubmitCommandsHint()
	{
		if (Bypass())
		{
			GetContext().RHISubmitCommandsHint();
			return;
		}
		ALLOC_COMMAND(FRHICommandSubmitCommandsHint<ECmdList::EGfx>)();
	}

	FORCEINLINE_DEBUGGABLE void PollOcclusionQueries()
	{
		if (Bypass())
		{
			GetContext().RHIPollOcclusionQueries();
			return;
		}
		ALLOC_COMMAND(FRHICommandPollOcclusionQueries)();
	}

	FORCEINLINE_DEBUGGABLE void TransitionResource(EResourceTransitionAccess TransitionType, FRHITexture* InTexture)
	{
		FRHITexture* Texture = InTexture;
		check(Texture == nullptr || Texture->IsCommitted());
		if (Bypass())
		{
			GetContext().RHITransitionResources(TransitionType, &Texture, 1);
			return;
		}

		// Allocate space to hold the single texture pointer inline in the command list itself.
		FRHITexture** TextureArray = (FRHITexture**)Alloc(sizeof(FRHITexture*), alignof(FRHITexture*));
		TextureArray[0] = Texture;
		ALLOC_COMMAND(FRHICommandTransitionTextures)(TransitionType, TextureArray, 1);
	}

	FORCEINLINE_DEBUGGABLE void TransitionResources(EResourceTransitionAccess TransitionType, FRHITexture** InTextures, int32 NumTextures)
	{
		if (Bypass())
		{
			GetContext().RHITransitionResources(TransitionType, InTextures, NumTextures);
			return;
		}

		// Allocate space to hold the list of textures inline in the command list itself.
		FRHITexture** InlineTextureArray = (FRHITexture**)Alloc(sizeof(FRHITexture*) * NumTextures, alignof(FRHITexture*));
		for (int32 Index = 0; Index < NumTextures; ++Index)
		{
			InlineTextureArray[Index] = InTextures[Index];
		}

		ALLOC_COMMAND(FRHICommandTransitionTextures)(TransitionType, InlineTextureArray, NumTextures);
	}

	FORCEINLINE_DEBUGGABLE void TransitionResourceArrayNoCopy(EResourceTransitionAccess TransitionType, TArray<FRHITexture*>& InTextures)
	{
		if (Bypass())
		{
			GetContext().RHITransitionResources(TransitionType, &InTextures[0], InTextures.Num());
			return;
		}
		ALLOC_COMMAND(FRHICommandTransitionTexturesArray)(TransitionType, InTextures);
	}

	FORCEINLINE_DEBUGGABLE void TransitionResource(EResourceTransitionAccess TransitionType, EResourceTransitionPipeline TransitionPipeline, FRHIUnorderedAccessView* InUAV, FRHIComputeFence* WriteFence)
	{
		check(InUAV == nullptr || InUAV->IsCommitted());
		FRHIUnorderedAccessView* UAV = InUAV;
		if (Bypass())
		{
			GetContext().RHITransitionResources(TransitionType, TransitionPipeline, &UAV, 1, WriteFence);
			return;
		}

		// Allocate space to hold the single UAV pointer inline in the command list itself.
		FRHIUnorderedAccessView** UAVArray = (FRHIUnorderedAccessView**)Alloc(sizeof(FRHIUnorderedAccessView*), alignof(FRHIUnorderedAccessView*));
		UAVArray[0] = UAV;
		ALLOC_COMMAND(FRHICommandTransitionUAVs<ECmdList::EGfx>)(TransitionType, TransitionPipeline, UAVArray, 1, WriteFence);
	}

	FORCEINLINE_DEBUGGABLE void TransitionResource(EResourceTransitionAccess TransitionType, EResourceTransitionPipeline TransitionPipeline, FRHIUnorderedAccessView* InUAV)
	{
		TransitionResource(TransitionType, TransitionPipeline, InUAV, nullptr);
	}

	FORCEINLINE_DEBUGGABLE void TransitionResources(EResourceTransitionAccess TransitionType, EResourceTransitionPipeline TransitionPipeline, FRHIUnorderedAccessView** InUAVs, int32 NumUAVs, FRHIComputeFence* WriteFence)
	{
		if (Bypass())
		{
			GetContext().RHITransitionResources(TransitionType, TransitionPipeline, InUAVs, NumUAVs, WriteFence);
			return;
		}

		// Allocate space to hold the list UAV pointers inline in the command list itself.
		FRHIUnorderedAccessView** UAVArray = (FRHIUnorderedAccessView**)Alloc(sizeof(FRHIUnorderedAccessView*) * NumUAVs, alignof(FRHIUnorderedAccessView*));
		for (int32 Index = 0; Index < NumUAVs; ++Index)
		{
			UAVArray[Index] = InUAVs[Index];
		}

		ALLOC_COMMAND(FRHICommandTransitionUAVs<ECmdList::EGfx>)(TransitionType, TransitionPipeline, UAVArray, NumUAVs, WriteFence);
	}

	FORCEINLINE_DEBUGGABLE void TransitionResources(EResourceTransitionAccess TransitionType, EResourceTransitionPipeline TransitionPipeline, FRHIUnorderedAccessView** InUAVs, int32 NumUAVs)
	{
		TransitionResources(TransitionType, TransitionPipeline, InUAVs, NumUAVs, nullptr);
	}

	FORCEINLINE_DEBUGGABLE void WaitComputeFence(FRHIComputeFence* WaitFence)
	{
		if (Bypass())
		{
			GetContext().RHIWaitComputeFence(WaitFence);
			return;
		}
		ALLOC_COMMAND(FRHICommandWaitComputeFence<ECmdList::EGfx>)(WaitFence);
	}

	FORCEINLINE_DEBUGGABLE void CopyToStagingBuffer(FRHIVertexBuffer* SourceBuffer, FRHIStagingBuffer* DestinationStagingBuffer, uint32 Offset, uint32 NumBytes)
	{
		if (Bypass())
		{
			GetContext().RHICopyToStagingBuffer(SourceBuffer, DestinationStagingBuffer, Offset, NumBytes);
			return;
		}
		ALLOC_COMMAND(FRHICommandCopyToStagingBuffer<ECmdList::EGfx>)(SourceBuffer, DestinationStagingBuffer, Offset, NumBytes);
	}

	FORCEINLINE_DEBUGGABLE void WriteGPUFence(FRHIGPUFence* Fence)
	{
		if (Bypass())
		{
			GetContext().RHIWriteGPUFence(Fence);
			return;
		}
		ALLOC_COMMAND(FRHICommandWriteGPUFence<ECmdList::EGfx>)(Fence);
	}

	FORCEINLINE_DEBUGGABLE void BeginRenderPass(const FRHIRenderPassInfo& InInfo, const TCHAR* Name)
	{
		check(!IsInsideRenderPass());
		check(!IsInsideComputePass());

		if (InInfo.bTooManyUAVs)
		{
			UE_LOG(LogRHI, Warning, TEXT("RenderPass %s has too many UAVs"));
		}
		InInfo.Validate();

		if (Bypass())
		{
			GetContext().RHIBeginRenderPass(InInfo, Name);
		}
		else
		{
			TCHAR* NameCopy  = AllocString(Name);
			ALLOC_COMMAND(FRHICommandBeginRenderPass)(InInfo, NameCopy);
		}
		Data.bInsideRenderPass = true;

		CacheActiveRenderTargets(InInfo);
		ResetSubpass(InInfo.SubpassHint);
		Data.bInsideRenderPass = true;
	}

	void EndRenderPass()
	{
		check(IsInsideRenderPass());
		check(!IsInsideComputePass());
		if (Bypass())
		{
			GetContext().RHIEndRenderPass();
		}
		else
		{
			ALLOC_COMMAND(FRHICommandEndRenderPass)();
		}
		Data.bInsideRenderPass = false;
		ResetSubpass(ESubpassHint::None);
	}

	FORCEINLINE_DEBUGGABLE void NextSubpass()
	{
		check(IsInsideRenderPass());
		if (Bypass())
		{
			GetContext().RHINextSubpass();
		}
		else
		{
			ALLOC_COMMAND(FRHICommandNextSubpass)();
		}
		IncrementSubpass();
	}

	FORCEINLINE_DEBUGGABLE void BeginComputePass(const TCHAR* Name)
	{
		check(!IsInsideRenderPass());
		check(!IsInsideComputePass());

		if (Bypass())
		{
			GetContext().RHIBeginComputePass(Name);
		}
		else
		{
			TCHAR* NameCopy  = AllocString(Name);
			ALLOC_COMMAND(FRHICommandBeginComputePass)(NameCopy);
		}
		Data.bInsideComputePass = true;

		Data.bInsideComputePass = true;
	}

	void EndComputePass()
	{
		check(IsInsideComputePass());
		check(!IsInsideRenderPass());
		if (Bypass())
		{
			GetContext().RHIEndComputePass();
		}
		else
		{
			ALLOC_COMMAND(FRHICommandEndComputePass)();
		}
		Data.bInsideComputePass = false;
	}

	// These 6 are special in that they must be called on the immediate command list and they force a flush only when we are not doing RHI thread
	void BeginScene();
	void EndScene();
	void BeginDrawingViewport(FRHIViewport* Viewport, FRHITexture* RenderTargetRHI);
	void EndDrawingViewport(FRHIViewport* Viewport, bool bPresent, bool bLockToVsync);
	void BeginFrame();
	void EndFrame();

	FORCEINLINE_DEBUGGABLE void PushEvent(const TCHAR* Name, FColor Color)
	{
		if (Bypass())
		{
			GetContext().RHIPushEvent(Name, Color);
			return;
		}
		TCHAR* NameCopy  = AllocString(Name);
		ALLOC_COMMAND(FRHICommandPushEvent<ECmdList::EGfx>)(NameCopy, Color);
	}

	FORCEINLINE_DEBUGGABLE void PopEvent()
	{
		if (Bypass())
		{
			GetContext().RHIPopEvent();
			return;
		}
		ALLOC_COMMAND(FRHICommandPopEvent<ECmdList::EGfx>)();
	}
	
	FORCEINLINE_DEBUGGABLE void RHIInvalidateCachedState()
	{
		if (Bypass())
		{
			GetContext().RHIInvalidateCachedState();
			return;
		}
		ALLOC_COMMAND(FRHICommandInvalidateCachedState)();
	}

	FORCEINLINE void DiscardRenderTargets(bool Depth, bool Stencil, uint32 ColorBitMask)
	{
		if (Bypass())
		{
			GetContext().RHIDiscardRenderTargets(Depth, Stencil, ColorBitMask);
			return;
		}
		ALLOC_COMMAND(FRHICommandDiscardRenderTargets)(Depth, Stencil, ColorBitMask);
	}
	
	FORCEINLINE_DEBUGGABLE void BreakPoint()
	{
#if !UE_BUILD_SHIPPING
		if (Bypass())
		{
			if (FPlatformMisc::IsDebuggerPresent())
			{
				UE_DEBUG_BREAK();
			}
			return;
		}
		ALLOC_COMMAND(FRHICommandDebugBreak)();
#endif
	}

#if RHI_RAYTRACING
	// Ray tracing API
	FORCEINLINE_DEBUGGABLE void CopyBufferRegion(FRHIVertexBuffer* DestBuffer, uint64 DstOffset, FRHIVertexBuffer* SourceBuffer, uint64 SrcOffset, uint64 NumBytes)
	{
		// No copy/DMA operation inside render passes
		check(IsOutsideRenderPass());

		if (Bypass())
		{
			GetContext().RHICopyBufferRegion(DestBuffer, DstOffset, SourceBuffer, SrcOffset, NumBytes);
		}
		else
		{
			ALLOC_COMMAND(FRHICommandCopyBufferRegion)(DestBuffer, DstOffset, SourceBuffer, SrcOffset, NumBytes);
		}
	}

	FORCEINLINE_DEBUGGABLE void CopyBufferRegions(const TArrayView<const FCopyBufferRegionParams> Params)
	{
		// No copy/DMA operation inside render passes
		check(IsOutsideRenderPass());

		if (Bypass())
		{
			GetContext().RHICopyBufferRegions(Params);
		}
		else
		{
			ALLOC_COMMAND(FRHICommandCopyBufferRegions)(AllocArray(Params));
		}
	}

	FORCEINLINE_DEBUGGABLE void BuildAccelerationStructure(FRHIRayTracingGeometry* Geometry)
	{
		if (Bypass())
		{
			GetContext().RHIBuildAccelerationStructure(Geometry);
		}
		else
		{
			ALLOC_COMMAND(FRHICommandBuildAccelerationStructure)(Geometry);
		}
	}

	FORCEINLINE_DEBUGGABLE void UpdateAccelerationStructures(const TArrayView<const FAccelerationStructureUpdateParams> Params)
	{
		if (Bypass())
		{
			GetContext().RHIUpdateAccelerationStructures(Params);
		}
		else
		{
			ALLOC_COMMAND(FRHICommandUpdateAccelerationStructures)(AllocArray(Params));
		}
	}

	FORCEINLINE_DEBUGGABLE void BuildAccelerationStructures(const TArrayView<const FAccelerationStructureUpdateParams> Params)
	{
		if (Bypass())
		{
			GetContext().RHIBuildAccelerationStructures(Params);
		}
		else
		{
			ALLOC_COMMAND(FRHICommandBuildAccelerationStructures)(AllocArray(Params));
		}
	}

	FORCEINLINE_DEBUGGABLE void BuildAccelerationStructure(FRHIRayTracingScene* Scene)
	{
		if (Bypass())
		{
			GetContext().RHIBuildAccelerationStructure(Scene);
		}
		else
		{
			ALLOC_COMMAND(FRHICommandBuildAccelerationStructure)(Scene);
		}
	}

	FORCEINLINE_DEBUGGABLE void ClearRayTracingBindings(FRHIRayTracingScene* Scene)
	{
		if (Bypass())
		{
			GetContext().RHIClearRayTracingBindings(Scene);
		}
		else
		{
			ALLOC_COMMAND(FRHICommandClearRayTracingBindings)(Scene);
		}
	}

	/**
	 * Trace rays from an input buffer of FBasicRayData.
	 * Binary intersection results are written to output buffer as R32_UINTs.
	 * 0xFFFFFFFF is written if ray intersects any scene triangle, 0 otherwise.
	 */
	FORCEINLINE_DEBUGGABLE void RayTraceOcclusion(FRHIRayTracingScene* Scene,
		FRHIShaderResourceView* Rays,
		FRHIUnorderedAccessView* Output,
		uint32 NumRays)
	{
		if (Bypass())
		{
			GetContext().RHIRayTraceOcclusion(Scene, Rays, Output, NumRays);
		}
		else
		{
			ALLOC_COMMAND(FRHICommandRayTraceOcclusion)(Scene, Rays, Output, NumRays);
		}
	}

	/**
	 * Trace rays from an input buffer of FBasicRayData.
	 * Primitive intersection results are written to output buffer as FIntersectionPayload.
	 */
	FORCEINLINE_DEBUGGABLE void RayTraceIntersection(FRHIRayTracingScene* Scene,
		FRHIShaderResourceView* Rays,
		FRHIUnorderedAccessView* Output,
		uint32 NumRays)
	{
		if (Bypass())
		{
			GetContext().RHIRayTraceIntersection(Scene, Rays, Output, NumRays);
		}
		else
		{
			ALLOC_COMMAND(FRHICommandRayTraceIntersection)(Scene, Rays, Output, NumRays);
		}
	}

	FORCEINLINE_DEBUGGABLE void RayTraceDispatch(FRayTracingPipelineState* Pipeline, FRHIRayTracingShader* RayGenShader, FRHIRayTracingScene* Scene, const FRayTracingShaderBindings& GlobalResourceBindings, uint32 Width, uint32 Height)
	{
		if (Bypass())
		{
			extern RHI_API FRHIRayTracingPipelineState* GetRHIRayTracingPipelineState(FRayTracingPipelineState*);
			GetContext().RHIRayTraceDispatch(GetRHIRayTracingPipelineState(Pipeline), RayGenShader, Scene, GlobalResourceBindings, Width, Height);
		}
		else
		{
			ALLOC_COMMAND(FRHICommandRayTraceDispatch)(Pipeline, RayGenShader, Scene, GlobalResourceBindings, Width, Height);
		}
	}

	FORCEINLINE_DEBUGGABLE void SetRayTracingHitGroup(
		FRHIRayTracingScene* Scene, uint32 InstanceIndex, uint32 SegmentIndex, uint32 ShaderSlot,
		FRayTracingPipelineState* Pipeline, uint32 HitGroupIndex,
		uint32 NumUniformBuffers, FRHIUniformBuffer* const* UniformBuffers,
		uint32 LooseParameterDataSize, const void* LooseParameterData,
		uint32 UserData)
	{
		if (Bypass())
		{
			extern RHI_API FRHIRayTracingPipelineState* GetRHIRayTracingPipelineState(FRayTracingPipelineState*);
			GetContext().RHISetRayTracingHitGroup(Scene, InstanceIndex, SegmentIndex, ShaderSlot, GetRHIRayTracingPipelineState(Pipeline), HitGroupIndex, 
				NumUniformBuffers, UniformBuffers,
				LooseParameterDataSize, LooseParameterData,
				UserData);
		}
		else
		{
			FRHIUniformBuffer** InlineUniformBuffers = nullptr;
			if (NumUniformBuffers)
			{
				InlineUniformBuffers = (FRHIUniformBuffer**)Alloc(sizeof(FRHIUniformBuffer*) * NumUniformBuffers, alignof(FRHIUniformBuffer*));
				for (uint32 Index = 0; Index < NumUniformBuffers; ++Index)
				{
					InlineUniformBuffers[Index] = UniformBuffers[Index];
				}
			}

			void* InlineLooseParameterData = nullptr;
			if (LooseParameterDataSize)
			{
				InlineLooseParameterData = Alloc(LooseParameterDataSize, 16);
				FMemory::Memcpy(InlineLooseParameterData, LooseParameterData, LooseParameterDataSize);
			}

			ALLOC_COMMAND(FRHICommandSetRayTracingBindings)(Scene, InstanceIndex, SegmentIndex, ShaderSlot, Pipeline, HitGroupIndex, 
				NumUniformBuffers, InlineUniformBuffers, 
				LooseParameterDataSize, InlineLooseParameterData,
				UserData);
		}
	}

	FORCEINLINE_DEBUGGABLE void SetRayTracingCallableShader(
		FRHIRayTracingScene* Scene, uint32 ShaderSlotInScene,
		FRayTracingPipelineState* Pipeline, uint32 ShaderIndexInPipeline,
		uint32 NumUniformBuffers, FRHIUniformBuffer* const* UniformBuffers,
		uint32 UserData)
	{
		if (Bypass())
		{
			extern RHI_API FRHIRayTracingPipelineState* GetRHIRayTracingPipelineState(FRayTracingPipelineState*);
			GetContext().RHISetRayTracingCallableShader(Scene, ShaderSlotInScene, GetRHIRayTracingPipelineState(Pipeline), ShaderIndexInPipeline, NumUniformBuffers, UniformBuffers, UserData);
		}
		else
		{
			FRHIUniformBuffer** InlineUniformBuffers = nullptr;
			if (NumUniformBuffers)
			{
				InlineUniformBuffers = (FRHIUniformBuffer**)Alloc(sizeof(FRHIUniformBuffer*) * NumUniformBuffers, alignof(FRHIUniformBuffer*));
				for (uint32 Index = 0; Index < NumUniformBuffers; ++Index)
				{
					InlineUniformBuffers[Index] = UniformBuffers[Index];
				}
			}

			ALLOC_COMMAND(FRHICommandSetRayTracingBindings)(Scene, ShaderSlotInScene, Pipeline, ShaderIndexInPipeline, NumUniformBuffers, InlineUniformBuffers, UserData);
		}
	}

#endif // RHI_RAYTRACING
};

class RHI_API FRHIAsyncComputeCommandList : public FRHICommandListBase
{
public:

	FRHIAsyncComputeCommandList() : FRHICommandListBase(FRHIGPUMask::All()) {}

	/** Custom new/delete with recycling */
	void* operator new(size_t Size);
	void operator delete(void *RawMemory);

	FORCEINLINE_DEBUGGABLE void SetShaderUniformBuffer(FRHIComputeShader* Shader, uint32 BaseIndex, FRHIUniformBuffer* UniformBuffer)
	{
		if (Bypass())
		{
			GetComputeContext().RHISetShaderUniformBuffer(Shader, BaseIndex, UniformBuffer);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetShaderUniformBuffer<FRHIComputeShader, ECmdList::ECompute>)(Shader, BaseIndex, UniformBuffer);
	}
	
	FORCEINLINE void SetShaderUniformBuffer(FComputeShaderRHIRef& Shader, uint32 BaseIndex, FRHIUniformBuffer* UniformBuffer)
	{
		SetShaderUniformBuffer(Shader.GetReference(), BaseIndex, UniformBuffer);
	}

	FORCEINLINE_DEBUGGABLE void SetShaderParameter(FRHIComputeShader* Shader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue)
	{
		if (Bypass())
		{
			GetComputeContext().RHISetShaderParameter(Shader, BufferIndex, BaseIndex, NumBytes, NewValue);
			return;
		}
		void* UseValue = Alloc(NumBytes, 16);
		FMemory::Memcpy(UseValue, NewValue, NumBytes);
		ALLOC_COMMAND(FRHICommandSetShaderParameter<FRHIComputeShader, ECmdList::ECompute>)(Shader, BufferIndex, BaseIndex, NumBytes, UseValue);
	}
	
	FORCEINLINE void SetShaderParameter(FComputeShaderRHIRef& Shader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue)
	{
		SetShaderParameter(Shader.GetReference(), BufferIndex, BaseIndex, NumBytes, NewValue);
	}
	
	FORCEINLINE_DEBUGGABLE void SetShaderTexture(FRHIComputeShader* Shader, uint32 TextureIndex, FRHITexture* Texture)
	{
		if (Bypass())
		{
			GetComputeContext().RHISetShaderTexture(Shader, TextureIndex, Texture);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetShaderTexture<FRHIComputeShader, ECmdList::ECompute>)(Shader, TextureIndex, Texture);
	}
	
	FORCEINLINE_DEBUGGABLE void SetShaderResourceViewParameter(FRHIComputeShader* Shader, uint32 SamplerIndex, FRHIShaderResourceView* SRV)
	{
		if (Bypass())
		{
			GetComputeContext().RHISetShaderResourceViewParameter(Shader, SamplerIndex, SRV);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetShaderResourceViewParameter<FRHIComputeShader, ECmdList::ECompute>)(Shader, SamplerIndex, SRV);
	}
	
	FORCEINLINE_DEBUGGABLE void SetShaderSampler(FRHIComputeShader* Shader, uint32 SamplerIndex, FRHISamplerState* State)
	{
		// Immutable samplers can't be set dynamically
		check(!State->IsImmutable());
		if (State->IsImmutable())
		{
			return;
		}
		
		if (Bypass())
		{
			GetComputeContext().RHISetShaderSampler(Shader, SamplerIndex, State);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetShaderSampler<FRHIComputeShader, ECmdList::ECompute>)(Shader, SamplerIndex, State);
	}

	FORCEINLINE_DEBUGGABLE void SetUAVParameter(FRHIComputeShader* Shader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV)
	{
		if (Bypass())
		{
			GetComputeContext().RHISetUAVParameter(Shader, UAVIndex, UAV);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetUAVParameter<FRHIComputeShader, ECmdList::ECompute>)(Shader, UAVIndex, UAV);
	}

	FORCEINLINE_DEBUGGABLE void SetUAVParameter(FRHIComputeShader* Shader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV, uint32 InitialCount)
	{
		if (Bypass())
		{
			GetComputeContext().RHISetUAVParameter(Shader, UAVIndex, UAV, InitialCount);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetUAVParameter_IntialCount<FRHIComputeShader, ECmdList::ECompute>)(Shader, UAVIndex, UAV, InitialCount);
	}
	
	FORCEINLINE_DEBUGGABLE void SetComputeShader(FRHIComputeShader* ComputeShader)
	{
		ComputeShader->UpdateStats();
		if (Bypass())
		{
			GetComputeContext().RHISetComputeShader(ComputeShader);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetComputeShader<ECmdList::ECompute>)(ComputeShader);
	}

	FORCEINLINE_DEBUGGABLE void SetComputePipelineState(FComputePipelineState* ComputePipelineState)
	{
		if (Bypass())
		{
			extern FRHIComputePipelineState* ExecuteSetComputePipelineState(FComputePipelineState* ComputePipelineState);
			FRHIComputePipelineState* RHIComputePipelineState = ExecuteSetComputePipelineState(ComputePipelineState);
			GetComputeContext().RHISetComputePipelineState(RHIComputePipelineState);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetComputePipelineState<ECmdList::ECompute>)(ComputePipelineState);
	}

	FORCEINLINE_DEBUGGABLE void SetAsyncComputeBudget(EAsyncComputeBudget Budget)
	{
		if (Bypass())
		{
			GetComputeContext().RHISetAsyncComputeBudget(Budget);
			return;
		}
		ALLOC_COMMAND(FRHICommandSetAsyncComputeBudget<ECmdList::ECompute>)(Budget);
	}

	FORCEINLINE_DEBUGGABLE void DispatchComputeShader(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ)
	{
		if (Bypass())
		{
			GetComputeContext().RHIDispatchComputeShader(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
			return;
		}
		ALLOC_COMMAND(FRHICommandDispatchComputeShader<ECmdList::ECompute>)(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
	}

	FORCEINLINE_DEBUGGABLE void DispatchIndirectComputeShader(FRHIVertexBuffer* ArgumentBuffer, uint32 ArgumentOffset)
	{
		if (Bypass())
		{
			GetComputeContext().RHIDispatchIndirectComputeShader(ArgumentBuffer, ArgumentOffset);
			return;
		}
		ALLOC_COMMAND(FRHICommandDispatchIndirectComputeShader<ECmdList::ECompute>)(ArgumentBuffer, ArgumentOffset);
	}

	FORCEINLINE_DEBUGGABLE void TransitionResource(EResourceTransitionAccess TransitionType, EResourceTransitionPipeline TransitionPipeline, FRHIUnorderedAccessView* InUAV, FRHIComputeFence* WriteFence)
	{
		FRHIUnorderedAccessView* UAV = InUAV;
		if (Bypass())
		{
			GetComputeContext().RHITransitionResources(TransitionType, TransitionPipeline, &UAV, 1, WriteFence);
			return;
		}

		// Allocate space to hold the single UAV pointer inline in the command list itself.
		FRHIUnorderedAccessView** UAVArray = (FRHIUnorderedAccessView**)Alloc(sizeof(FRHIUnorderedAccessView*), alignof(FRHIUnorderedAccessView*));
		UAVArray[0] = UAV;
		ALLOC_COMMAND(FRHICommandTransitionUAVs<ECmdList::ECompute>)(TransitionType, TransitionPipeline, UAVArray, 1, WriteFence);
	}

	FORCEINLINE_DEBUGGABLE void TransitionResource(EResourceTransitionAccess TransitionType, EResourceTransitionPipeline TransitionPipeline, FRHIUnorderedAccessView* InUAV)
	{
		TransitionResource(TransitionType, TransitionPipeline, InUAV, nullptr);
	}

	FORCEINLINE_DEBUGGABLE void TransitionResources(EResourceTransitionAccess TransitionType, EResourceTransitionPipeline TransitionPipeline, FRHIUnorderedAccessView** InUAVs, int32 NumUAVs, FRHIComputeFence* WriteFence)
	{
		if (Bypass())
		{
			GetComputeContext().RHITransitionResources(TransitionType, TransitionPipeline, InUAVs, NumUAVs, WriteFence);
			return;
		}

		// Allocate space to hold the list UAV pointers inline in the command list itself.
		FRHIUnorderedAccessView** UAVArray = (FRHIUnorderedAccessView**)Alloc(sizeof(FRHIUnorderedAccessView*) * NumUAVs, alignof(FRHIUnorderedAccessView*));
		for (int32 Index = 0; Index < NumUAVs; ++Index)
		{
			UAVArray[Index] = InUAVs[Index];
		}

		ALLOC_COMMAND(FRHICommandTransitionUAVs<ECmdList::ECompute>)(TransitionType, TransitionPipeline, UAVArray, NumUAVs, WriteFence);
	}

	FORCEINLINE_DEBUGGABLE void TransitionResources(EResourceTransitionAccess TransitionType, EResourceTransitionPipeline TransitionPipeline, FRHIUnorderedAccessView** InUAVs, int32 NumUAVs)
	{
		TransitionResources(TransitionType, TransitionPipeline, InUAVs, NumUAVs, nullptr);
	}
	
	FORCEINLINE_DEBUGGABLE void PushEvent(const TCHAR* Name, FColor Color)
	{
		if (Bypass())
		{
			GetComputeContext().RHIPushEvent(Name, Color);
			return;
		}
		TCHAR* NameCopy  = AllocString(Name);
		ALLOC_COMMAND(FRHICommandPushEvent<ECmdList::ECompute>)(NameCopy, Color);
	}

	FORCEINLINE_DEBUGGABLE void PopEvent()
	{
		if (Bypass())
		{
			GetComputeContext().RHIPopEvent();
			return;
		}
		ALLOC_COMMAND(FRHICommandPopEvent<ECmdList::ECompute>)();
	}

	FORCEINLINE_DEBUGGABLE void BreakPoint()
	{
#if !UE_BUILD_SHIPPING
		if (Bypass())
		{
			if (FPlatformMisc::IsDebuggerPresent())
			{
				UE_DEBUG_BREAK();
			}
			return;
		}
		ALLOC_COMMAND(FRHICommandDebugBreak)();
#endif
	}

	FORCEINLINE_DEBUGGABLE void SubmitCommandsHint()
	{
		if (Bypass())
		{
			GetComputeContext().RHISubmitCommandsHint();
			return;
		}
		ALLOC_COMMAND(FRHICommandSubmitCommandsHint<ECmdList::ECompute>)();
	}

	FORCEINLINE_DEBUGGABLE void WaitComputeFence(FRHIComputeFence* WaitFence)
	{
		if (Bypass())
		{
			GetComputeContext().RHIWaitComputeFence(WaitFence);
			return;
		}
		ALLOC_COMMAND(FRHICommandWaitComputeFence<ECmdList::ECompute>)(WaitFence);
	}

	FORCEINLINE_DEBUGGABLE void CopyToStagingBuffer(FRHIVertexBuffer* SourceBuffer, FRHIStagingBuffer* DestinationStagingBuffer, uint32 Offset, uint32 NumBytes)
	{
		if (Bypass())
		{
			GetComputeContext().RHICopyToStagingBuffer(SourceBuffer, DestinationStagingBuffer, Offset, NumBytes);
			return;
		}
		ALLOC_COMMAND(FRHICommandCopyToStagingBuffer<ECmdList::ECompute>)(SourceBuffer, DestinationStagingBuffer, Offset, NumBytes);
	}

	FORCEINLINE_DEBUGGABLE void WriteGPUFence(FRHIGPUFence* Fence)
	{
		if (Bypass())
		{
			GetComputeContext().RHIWriteGPUFence(Fence);
			return;
		}
		ALLOC_COMMAND(FRHICommandWriteGPUFence<ECmdList::ECompute>)(Fence);
	}
};

namespace EImmediateFlushType
{
	enum Type
	{ 
		WaitForOutstandingTasksOnly = 0, 
		DispatchToRHIThread, 
		WaitForDispatchToRHIThread,
		FlushRHIThread,
		FlushRHIThreadFlushResources,
		FlushRHIThreadFlushResourcesFlushDeferredDeletes
	};
};

class FScopedRHIThreadStaller
{
	class FRHICommandListImmediate* Immed; // non-null if we need to unstall
public:
	FScopedRHIThreadStaller(class FRHICommandListImmediate& InImmed);
	~FScopedRHIThreadStaller();
};

class RHI_API FRHICommandListImmediate : public FRHICommandList
{
	template <typename LAMBDA>
	struct TRHILambdaCommand final : public FRHICommandBase
	{
		LAMBDA Lambda;

		TRHILambdaCommand(LAMBDA&& InLambda)
			: Lambda(Forward<LAMBDA>(InLambda))
		{}

		void ExecuteAndDestruct(FRHICommandListBase& CmdList, FRHICommandListDebugContext&) override final
		{
			Lambda(*static_cast<FRHICommandListImmediate*>(&CmdList));
			Lambda.~LAMBDA();
		}
	};

	friend class FRHICommandListExecutor;
	FRHICommandListImmediate()
		: FRHICommandList(FRHIGPUMask::All())
	{
		Data.Type = FRHICommandListBase::FCommonData::ECmdListType::Immediate;
	}
	~FRHICommandListImmediate()
	{
		check(!HasCommands());
	}
public:

	void ImmediateFlush(EImmediateFlushType::Type FlushType);
	bool StallRHIThread();
	void UnStallRHIThread();
	static bool IsStalled();

	void SetCurrentStat(TStatId Stat);

	/** Dispatch current work and change the GPUMask. */
	void SetGPUMask(FRHIGPUMask InGPUMask);

	static FGraphEventRef RenderThreadTaskFence();
	static FGraphEventArray& GetRenderThreadTaskArray();
	static void WaitOnRenderThreadTaskFence(FGraphEventRef& Fence);
	static bool AnyRenderThreadTasksOutstanding();
	FGraphEventRef RHIThreadFence(bool bSetLockFence = false);

	//Queue the given async compute commandlists in order with the current immediate commandlist
	void QueueAsyncCompute(FRHIAsyncComputeCommandList& RHIComputeCmdList);

	template <typename LAMBDA>
	FORCEINLINE_DEBUGGABLE bool EnqueueLambda(bool bRunOnCurrentThread, LAMBDA&& Lambda)
	{
		if (bRunOnCurrentThread)
		{
			Lambda(*this);
			return false;
		}
		else
		{
			ALLOC_COMMAND(TRHILambdaCommand<LAMBDA>)(Forward<LAMBDA>(Lambda));
			return true;
		}
	}

	template <typename LAMBDA>
	FORCEINLINE_DEBUGGABLE bool EnqueueLambda(LAMBDA&& Lambda)
	{
		return EnqueueLambda(Bypass(), Forward<LAMBDA>(Lambda));
	}

	FORCEINLINE FSamplerStateRHIRef CreateSamplerState(const FSamplerStateInitializerRHI& Initializer)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		return RHICreateSamplerState(Initializer);
	}
	
	FORCEINLINE FRasterizerStateRHIRef CreateRasterizerState(const FRasterizerStateInitializerRHI& Initializer)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		return RHICreateRasterizerState(Initializer);
	}
	
	FORCEINLINE FDepthStencilStateRHIRef CreateDepthStencilState(const FDepthStencilStateInitializerRHI& Initializer)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		return RHICreateDepthStencilState(Initializer);
	}
	
	FORCEINLINE FBlendStateRHIRef CreateBlendState(const FBlendStateInitializerRHI& Initializer)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		return RHICreateBlendState(Initializer);
	}
	
	FORCEINLINE FPixelShaderRHIRef CreatePixelShader(const TArray<uint8>& Code)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		return GDynamicRHI->CreatePixelShader_RenderThread(*this, Code);
	}
	
	FORCEINLINE FPixelShaderRHIRef CreatePixelShader(FRHIShaderLibrary* Library, FSHAHash Hash)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		return GDynamicRHI->CreatePixelShader_RenderThread(*this, Library, Hash);
	}
	
	FORCEINLINE FVertexShaderRHIRef CreateVertexShader(const TArray<uint8>& Code)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		return GDynamicRHI->CreateVertexShader_RenderThread(*this, Code);
	}
	
	FORCEINLINE FVertexShaderRHIRef CreateVertexShader(FRHIShaderLibrary* Library, FSHAHash Hash)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		return GDynamicRHI->CreateVertexShader_RenderThread(*this, Library, Hash);
	}
	
	FORCEINLINE FHullShaderRHIRef CreateHullShader(const TArray<uint8>& Code)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		return GDynamicRHI->CreateHullShader_RenderThread(*this, Code);
	}
	
	FORCEINLINE FHullShaderRHIRef CreateHullShader(FRHIShaderLibrary* Library, FSHAHash Hash)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		return GDynamicRHI->CreateHullShader_RenderThread(*this, Library, Hash);
	}
	
	FORCEINLINE FDomainShaderRHIRef CreateDomainShader(const TArray<uint8>& Code)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		return GDynamicRHI->CreateDomainShader_RenderThread(*this, Code);
	}
	
	FORCEINLINE FDomainShaderRHIRef CreateDomainShader(FRHIShaderLibrary* Library, FSHAHash Hash)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		return GDynamicRHI->CreateDomainShader_RenderThread(*this, Library, Hash);
	}
	
	FORCEINLINE FGeometryShaderRHIRef CreateGeometryShader(const TArray<uint8>& Code)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		return GDynamicRHI->CreateGeometryShader_RenderThread(*this, Code);
	}
	
	FORCEINLINE FGeometryShaderRHIRef CreateGeometryShader(FRHIShaderLibrary* Library, FSHAHash Hash)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		return GDynamicRHI->CreateGeometryShader_RenderThread(*this, Library, Hash);
	}
	
	UE_DEPRECATED(4.23, "Geometry Stream out is deprecated.")
	FORCEINLINE FGeometryShaderRHIRef CreateGeometryShaderWithStreamOutput(const TArray<uint8>& Code, const FStreamOutElementList& ElementList, uint32 NumStrides, const uint32* Strides, int32 RasterizedStream)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		return GDynamicRHI->CreateGeometryShaderWithStreamOutput_RenderThread(*this, Code, ElementList, NumStrides, Strides, RasterizedStream);
	}
	
	UE_DEPRECATED(4.23, "Geometry Stream out is deprecated.")
	FORCEINLINE FGeometryShaderRHIRef CreateGeometryShaderWithStreamOutput(const FStreamOutElementList& ElementList, uint32 NumStrides, const uint32* Strides, int32 RasterizedStream, FRHIShaderLibrary* Library, FSHAHash Hash)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		return GDynamicRHI->CreateGeometryShaderWithStreamOutput_RenderThread(*this, ElementList, NumStrides, Strides, RasterizedStream, Library, Hash);
	}
	
	FORCEINLINE FComputeShaderRHIRef CreateComputeShader(const TArray<uint8>& Code)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		return GDynamicRHI->CreateComputeShader_RenderThread(*this, Code);
	}
	
	FORCEINLINE FComputeShaderRHIRef CreateComputeShader(FRHIShaderLibrary* Library, FSHAHash Hash)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		return GDynamicRHI->CreateComputeShader_RenderThread(*this, Library, Hash);
	}
	
	FORCEINLINE FComputeFenceRHIRef CreateComputeFence(const FName& Name)
	{		
		return GDynamicRHI->RHICreateComputeFence(Name);
	}	

	FORCEINLINE FGPUFenceRHIRef CreateGPUFence(const FName& Name)
	{
		return GDynamicRHI->RHICreateGPUFence(Name);
	}

	FORCEINLINE FStagingBufferRHIRef CreateStagingBuffer()
	{
		return GDynamicRHI->RHICreateStagingBuffer();
	}

	FORCEINLINE FBoundShaderStateRHIRef CreateBoundShaderState(FRHIVertexDeclaration* VertexDeclaration, FRHIVertexShader* VertexShader, FRHIHullShader* HullShader, FRHIDomainShader* DomainShader, FRHIPixelShader* PixelShader, FRHIGeometryShader* GeometryShader)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		return RHICreateBoundShaderState(VertexDeclaration, VertexShader, HullShader, DomainShader, PixelShader, GeometryShader);
	}

	FORCEINLINE FGraphicsPipelineStateRHIRef CreateGraphicsPipelineState(const FGraphicsPipelineStateInitializer& Initializer)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		return RHICreateGraphicsPipelineState(Initializer);
	}

	FORCEINLINE TRefCountPtr<FRHIComputePipelineState> CreateComputePipelineState(FRHIComputeShader* ComputeShader)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		return RHICreateComputePipelineState(ComputeShader);
	}

	FORCEINLINE FUniformBufferRHIRef CreateUniformBuffer(const void* Contents, const FRHIUniformBufferLayout& Layout, EUniformBufferUsage Usage)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		return RHICreateUniformBuffer(Contents, Layout, Usage);
	}
	
	FORCEINLINE FIndexBufferRHIRef CreateAndLockIndexBuffer(uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo, void*& OutDataBuffer)
	{
		return GDynamicRHI->CreateAndLockIndexBuffer_RenderThread(*this, Stride, Size, InUsage, CreateInfo, OutDataBuffer);
	}
	
	FORCEINLINE FIndexBufferRHIRef CreateIndexBuffer(uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
	{
		return GDynamicRHI->CreateIndexBuffer_RenderThread(*this, Stride, Size, InUsage, CreateInfo);
	}
	
	FORCEINLINE void* LockIndexBuffer(FRHIIndexBuffer* IndexBuffer, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode)
	{
		return GDynamicRHI->LockIndexBuffer_RenderThread(*this, IndexBuffer, Offset, SizeRHI, LockMode);
	}
	
	FORCEINLINE void UnlockIndexBuffer(FRHIIndexBuffer* IndexBuffer)
	{
		GDynamicRHI->UnlockIndexBuffer_RenderThread(*this, IndexBuffer);
	}
	
	FORCEINLINE void* LockStagingBuffer(FRHIStagingBuffer* StagingBuffer, uint32 Offset, uint32 SizeRHI)
	{
		return GDynamicRHI->LockStagingBuffer_RenderThread(*this, StagingBuffer, Offset, SizeRHI);
	}
	
	FORCEINLINE void UnlockStagingBuffer(FRHIStagingBuffer* StagingBuffer)
	{
		GDynamicRHI->UnlockStagingBuffer_RenderThread(*this, StagingBuffer);
	}
	
	FORCEINLINE FVertexBufferRHIRef CreateAndLockVertexBuffer(uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo, void*& OutDataBuffer)
	{
		return GDynamicRHI->CreateAndLockVertexBuffer_RenderThread(*this, Size, InUsage, CreateInfo, OutDataBuffer);
	}

	FORCEINLINE FVertexBufferRHIRef CreateVertexBuffer(uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
	{
		return GDynamicRHI->CreateVertexBuffer_RenderThread(*this, Size, InUsage, CreateInfo);
	}
	
	FORCEINLINE void* LockVertexBuffer(FRHIVertexBuffer* VertexBuffer, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode)
	{
		return GDynamicRHI->LockVertexBuffer_RenderThread(*this, VertexBuffer, Offset, SizeRHI, LockMode);
	}
	
	FORCEINLINE void UnlockVertexBuffer(FRHIVertexBuffer* VertexBuffer)
	{
		GDynamicRHI->UnlockVertexBuffer_RenderThread(*this, VertexBuffer);
	}
	
	FORCEINLINE void CopyVertexBuffer(FRHIVertexBuffer* SourceBuffer, FRHIVertexBuffer* DestBuffer)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_CopyVertexBuffer_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread);  
		GDynamicRHI->RHICopyVertexBuffer(SourceBuffer,DestBuffer);
	}

	FORCEINLINE FStructuredBufferRHIRef CreateStructuredBuffer(uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		return GDynamicRHI->CreateStructuredBuffer_RenderThread(*this, Stride, Size, InUsage, CreateInfo);
	}
	
	FORCEINLINE void* LockStructuredBuffer(FRHIStructuredBuffer* StructuredBuffer, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_LockStructuredBuffer_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread); 
		 
		return GDynamicRHI->RHILockStructuredBuffer(StructuredBuffer, Offset, SizeRHI, LockMode);
	}
	
	FORCEINLINE void UnlockStructuredBuffer(FRHIStructuredBuffer* StructuredBuffer)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_UnlockStructuredBuffer_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread); 
		  
		GDynamicRHI->RHIUnlockStructuredBuffer(StructuredBuffer);
	}
	
	FORCEINLINE FUnorderedAccessViewRHIRef CreateUnorderedAccessView(FRHIStructuredBuffer* StructuredBuffer, bool bUseUAVCounter, bool bAppendBuffer)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		return GDynamicRHI->RHICreateUnorderedAccessView_RenderThread(*this, StructuredBuffer, bUseUAVCounter, bAppendBuffer);
	}
	
	FORCEINLINE FUnorderedAccessViewRHIRef CreateUnorderedAccessView(FRHITexture* Texture, uint32 MipLevel)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		return GDynamicRHI->RHICreateUnorderedAccessView_RenderThread(*this, Texture, MipLevel);
	}
	
	FORCEINLINE FUnorderedAccessViewRHIRef CreateUnorderedAccessView(FRHIVertexBuffer* VertexBuffer, uint8 Format)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		return GDynamicRHI->RHICreateUnorderedAccessView_RenderThread(*this, VertexBuffer, Format);
	}

	FORCEINLINE FUnorderedAccessViewRHIRef CreateUnorderedAccessView(FRHIIndexBuffer* IndexBuffer, uint8 Format)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		return GDynamicRHI->RHICreateUnorderedAccessView_RenderThread(*this, IndexBuffer, Format);
	}

	FORCEINLINE FShaderResourceViewRHIRef CreateShaderResourceView(FRHIStructuredBuffer* StructuredBuffer)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		return GDynamicRHI->RHICreateShaderResourceView_RenderThread(*this, StructuredBuffer);
	}
	
	FORCEINLINE FShaderResourceViewRHIRef CreateShaderResourceView(FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		return GDynamicRHI->CreateShaderResourceView_RenderThread(*this, VertexBuffer, Stride, Format);
	}
	
	FORCEINLINE FShaderResourceViewRHIRef CreateShaderResourceView(FRHIIndexBuffer* Buffer)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		return GDynamicRHI->CreateShaderResourceView_RenderThread(*this, Buffer);
	}
	
	FORCEINLINE uint64 CalcTexture2DPlatformSize(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, uint32 Flags, uint32& OutAlign)
	{
		return RHICalcTexture2DPlatformSize(SizeX, SizeY, Format, NumMips, NumSamples, Flags, OutAlign);
	}
	
	FORCEINLINE uint64 CalcTexture3DPlatformSize(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, uint32 Flags, uint32& OutAlign)
	{
		return RHICalcTexture3DPlatformSize(SizeX, SizeY, SizeZ, Format, NumMips, Flags, OutAlign);
	}
	
	FORCEINLINE uint64 CalcTextureCubePlatformSize(uint32 Size, uint8 Format, uint32 NumMips, uint32 Flags, uint32& OutAlign)
	{
		return RHICalcTextureCubePlatformSize(Size, Format, NumMips, Flags, OutAlign);
	}
	
	FORCEINLINE void GetTextureMemoryStats(FTextureMemoryStats& OutStats)
	{
		RHIGetTextureMemoryStats(OutStats);
	}
	
	FORCEINLINE bool GetTextureMemoryVisualizeData(FColor* TextureData,int32 SizeX,int32 SizeY,int32 Pitch,int32 PixelSize)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_GetTextureMemoryVisualizeData_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread); 
		return GDynamicRHI->RHIGetTextureMemoryVisualizeData(TextureData,SizeX,SizeY,Pitch,PixelSize);
	}
	
	FORCEINLINE FTextureReferenceRHIRef CreateTextureReference(FLastRenderTimeContainer* LastRenderTime)
	{
		LLM_SCOPE(ELLMTag::Textures);
		return GDynamicRHI->RHICreateTextureReference_RenderThread(*this, LastRenderTime);
	}
	
	FORCEINLINE FTexture2DRHIRef CreateTexture2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, uint32 Flags, FRHIResourceCreateInfo& CreateInfo)
	{
		LLM_SCOPE((Flags & (TexCreate_RenderTargetable | TexCreate_DepthStencilTargetable)) != 0 ? ELLMTag::RenderTargets : ELLMTag::Textures);
		return GDynamicRHI->RHICreateTexture2D_RenderThread(*this, SizeX, SizeY, Format, NumMips, NumSamples, Flags, CreateInfo);
	}

	FORCEINLINE FTexture2DRHIRef CreateTextureExternal2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, uint32 Flags, FRHIResourceCreateInfo& CreateInfo)
	{
		return GDynamicRHI->RHICreateTextureExternal2D_RenderThread(*this, SizeX, SizeY, Format, NumMips, NumSamples, Flags, CreateInfo);
	}

	FORCEINLINE FStructuredBufferRHIRef CreateRTWriteMaskBuffer(FTexture2DRHIRef RenderTarget)
	{
		LLM_SCOPE(ELLMTag::RenderTargets);
		return GDynamicRHI->RHICreateRTWriteMaskBuffer(RenderTarget);
	}

	FORCEINLINE FTexture2DRHIRef AsyncCreateTexture2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 Flags, void** InitialMipData, uint32 NumInitialMips)
	{
		LLM_SCOPE((Flags & (TexCreate_RenderTargetable | TexCreate_DepthStencilTargetable)) != 0 ? ELLMTag::RenderTargets : ELLMTag::Textures);
		return GDynamicRHI->RHIAsyncCreateTexture2D(SizeX, SizeY, Format, NumMips, Flags, InitialMipData, NumInitialMips);
	}
	
	FORCEINLINE void CopySharedMips(FRHITexture2D* DestTexture2D, FRHITexture2D* SrcTexture2D)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_CopySharedMips_Flush);
		DestTexture2D->AddRef();
		SrcTexture2D->AddRef();
		EnqueueLambda([DestTexture2D, SrcTexture2D](FRHICommandList&)
		{
			LLM_SCOPE(ELLMTag::Textures);
			GDynamicRHI->RHICopySharedMips(DestTexture2D, SrcTexture2D);
			DestTexture2D->Release();
			SrcTexture2D->Release();
		});
	}

	FORCEINLINE void TransferTexture(FRHITexture2D* Texture, FIntRect Rect, uint32 SrcGPUIndex, uint32 DestGPUIndex, bool PullData)
	{
		LLM_SCOPE(ELLMTag::Textures);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread);

		return GDynamicRHI->RHITransferTexture(Texture, Rect, SrcGPUIndex, DestGPUIndex, PullData);
	}

	FORCEINLINE FTexture2DArrayRHIRef CreateTexture2DArray(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, uint32 NumSamples, uint32 Flags, FRHIResourceCreateInfo& CreateInfo)
	{
		LLM_SCOPE((Flags & (TexCreate_RenderTargetable | TexCreate_DepthStencilTargetable)) != 0 ? ELLMTag::RenderTargets : ELLMTag::Textures);
		return GDynamicRHI->RHICreateTexture2DArray_RenderThread(*this, SizeX, SizeY, SizeZ, Format, NumMips, NumSamples, Flags, CreateInfo);
	}

	UE_DEPRECATED(4.23, "CreateTexture2DArray now takes NumSamples")
	FORCEINLINE FTexture2DArrayRHIRef CreateTexture2DArray(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, uint32 Flags, FRHIResourceCreateInfo& CreateInfo)
	{
		return CreateTexture2DArray(SizeX, SizeY, SizeZ, Format, NumMips, 1, Flags, CreateInfo);
	}
	
	FORCEINLINE FTexture3DRHIRef CreateTexture3D(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, uint32 Flags, FRHIResourceCreateInfo& CreateInfo)
	{
		LLM_SCOPE((Flags & (TexCreate_RenderTargetable | TexCreate_DepthStencilTargetable)) != 0 ? ELLMTag::RenderTargets : ELLMTag::Textures);
		return GDynamicRHI->RHICreateTexture3D_RenderThread(*this, SizeX, SizeY, SizeZ, Format, NumMips, Flags, CreateInfo);
	}
	
	FORCEINLINE void GetResourceInfo(FRHITexture* Ref, FRHIResourceInfo& OutInfo)
	{
		return RHIGetResourceInfo(Ref, OutInfo);
	}
	
	FORCEINLINE FShaderResourceViewRHIRef CreateShaderResourceView(FRHITexture* Texture, const FRHITextureSRVCreateInfo& CreateInfo)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		return GDynamicRHI->RHICreateShaderResourceView_RenderThread(*this, Texture, CreateInfo);
	}

	FORCEINLINE FShaderResourceViewRHIRef CreateShaderResourceView(FRHITexture* Texture, uint8 MipLevel)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		const FRHITextureSRVCreateInfo CreateInfo(MipLevel, 1, Texture->GetFormat());
		return GDynamicRHI->RHICreateShaderResourceView_RenderThread(*this, Texture, CreateInfo);
	}
	
	FORCEINLINE FShaderResourceViewRHIRef CreateShaderResourceView(FRHITexture* Texture, uint8 MipLevel, uint8 NumMipLevels, uint8 Format)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		const FRHITextureSRVCreateInfo CreateInfo(MipLevel, NumMipLevels, Format);
		return GDynamicRHI->RHICreateShaderResourceView_RenderThread(*this, Texture, CreateInfo);
	}

	//UE_DEPRECATED(4.23, "This function is deprecated and will be removed in future releases. Renderer version implemented.")
	FORCEINLINE void GenerateMips(FRHITexture* Texture)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_GenerateMips_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread); return GDynamicRHI->RHIGenerateMips(Texture);
	}
	
	FORCEINLINE uint32 ComputeMemorySize(FRHITexture* TextureRHI)
	{
		return RHIComputeMemorySize(TextureRHI);
	}
	
	FORCEINLINE FTexture2DRHIRef AsyncReallocateTexture2D(FRHITexture2D* Texture2D, int32 NewMipCount, int32 NewSizeX, int32 NewSizeY, FThreadSafeCounter* RequestStatus)
	{
		LLM_SCOPE(ELLMTag::Textures);
		return GDynamicRHI->AsyncReallocateTexture2D_RenderThread(*this, Texture2D, NewMipCount, NewSizeX, NewSizeY, RequestStatus);
	}
	
	FORCEINLINE ETextureReallocationStatus FinalizeAsyncReallocateTexture2D(FRHITexture2D* Texture2D, bool bBlockUntilCompleted)
	{
		LLM_SCOPE(ELLMTag::Textures);
		return GDynamicRHI->FinalizeAsyncReallocateTexture2D_RenderThread(*this, Texture2D, bBlockUntilCompleted);
	}
	
	FORCEINLINE ETextureReallocationStatus CancelAsyncReallocateTexture2D(FRHITexture2D* Texture2D, bool bBlockUntilCompleted)
	{
		return GDynamicRHI->CancelAsyncReallocateTexture2D_RenderThread(*this, Texture2D, bBlockUntilCompleted);
	}
	
	FORCEINLINE void* LockTexture2D(FRHITexture2D* Texture, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail, bool bFlushRHIThread = true)
	{
		LLM_SCOPE(ELLMTag::Textures);
		return GDynamicRHI->LockTexture2D_RenderThread(*this, Texture, MipIndex, LockMode, DestStride, bLockWithinMiptail, bFlushRHIThread);
	}
	
	FORCEINLINE void UnlockTexture2D(FRHITexture2D* Texture, uint32 MipIndex, bool bLockWithinMiptail, bool bFlushRHIThread = true)
	{		
		GDynamicRHI->UnlockTexture2D_RenderThread(*this, Texture, MipIndex, bLockWithinMiptail, bFlushRHIThread);
	}
	
	FORCEINLINE void* LockTexture2DArray(FRHITexture2DArray* Texture, uint32 TextureIndex, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail)
	{
		LLM_SCOPE(ELLMTag::Textures);
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_LockTexture2DArray_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread);  
		return GDynamicRHI->RHILockTexture2DArray(Texture, TextureIndex, MipIndex, LockMode, DestStride, bLockWithinMiptail);
	}
	
	FORCEINLINE void UnlockTexture2DArray(FRHITexture2DArray* Texture, uint32 TextureIndex, uint32 MipIndex, bool bLockWithinMiptail)
	{
		LLM_SCOPE(ELLMTag::Textures);
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_UnlockTexture2DArray_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread);   
		GDynamicRHI->RHIUnlockTexture2DArray(Texture, TextureIndex, MipIndex, bLockWithinMiptail);
	}
	
	FORCEINLINE void UpdateTexture2D(FRHITexture2D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion2D& UpdateRegion, uint32 SourcePitch, const uint8* SourceData)
	{		
		checkf(UpdateRegion.DestX + UpdateRegion.Width <= Texture->GetSizeX(), TEXT("UpdateTexture2D out of bounds on X. Texture: %s, %i, %i, %i"), *Texture->GetName().ToString(), UpdateRegion.DestX, UpdateRegion.Width, Texture->GetSizeX());
		checkf(UpdateRegion.DestY + UpdateRegion.Height <= Texture->GetSizeY(), TEXT("UpdateTexture2D out of bounds on Y. Texture: %s, %i, %i, %i"), *Texture->GetName().ToString(), UpdateRegion.DestY, UpdateRegion.Height, Texture->GetSizeY());
		LLM_SCOPE(ELLMTag::Textures);
		GDynamicRHI->UpdateTexture2D_RenderThread(*this, Texture, MipIndex, UpdateRegion, SourcePitch, SourceData);
	}

	FORCEINLINE void UpdateFromBufferTexture2D(FRHITexture2D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion2D& UpdateRegion, uint32 SourcePitch, FRHIStructuredBuffer* Buffer, uint32 BufferOffset)
	{
		checkf(UpdateRegion.DestX + UpdateRegion.Width <= Texture->GetSizeX(), TEXT("UpdateFromBufferTexture2D out of bounds on X. Texture: %s, %i, %i, %i"), *Texture->GetName().ToString(), UpdateRegion.DestX, UpdateRegion.Width, Texture->GetSizeX());
		checkf(UpdateRegion.DestY + UpdateRegion.Height <= Texture->GetSizeY(), TEXT("UpdateFromBufferTexture2D out of bounds on Y. Texture: %s, %i, %i, %i"), *Texture->GetName().ToString(), UpdateRegion.DestY, UpdateRegion.Height, Texture->GetSizeY());
		LLM_SCOPE(ELLMTag::Textures);
		GDynamicRHI->UpdateFromBufferTexture2D_RenderThread(*this, Texture, MipIndex, UpdateRegion, SourcePitch, Buffer, BufferOffset);
	}

	FORCEINLINE FUpdateTexture3DData BeginUpdateTexture3D(FRHITexture3D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion3D& UpdateRegion)
	{
		checkf(UpdateRegion.DestX + UpdateRegion.Width <= Texture->GetSizeX(), TEXT("UpdateTexture3D out of bounds on X. Texture: %s, %i, %i, %i"), *Texture->GetName().ToString(), UpdateRegion.DestX, UpdateRegion.Width, Texture->GetSizeX());
		checkf(UpdateRegion.DestY + UpdateRegion.Height <= Texture->GetSizeY(), TEXT("UpdateTexture3D out of bounds on Y. Texture: %s, %i, %i, %i"), *Texture->GetName().ToString(), UpdateRegion.DestY, UpdateRegion.Height, Texture->GetSizeY());
		checkf(UpdateRegion.DestZ + UpdateRegion.Depth <= Texture->GetSizeZ(), TEXT("UpdateTexture3D out of bounds on Z. Texture: %s, %i, %i, %i"), *Texture->GetName().ToString(), UpdateRegion.DestZ, UpdateRegion.Depth, Texture->GetSizeZ());
		LLM_SCOPE(ELLMTag::Textures);
		return GDynamicRHI->BeginUpdateTexture3D_RenderThread(*this, Texture, MipIndex, UpdateRegion);
	}

	FORCEINLINE void EndUpdateTexture3D(FUpdateTexture3DData& UpdateData)
	{		
		LLM_SCOPE(ELLMTag::Textures);
		GDynamicRHI->EndUpdateTexture3D_RenderThread(*this, UpdateData);
	}

	FORCEINLINE void EndMultiUpdateTexture3D(TArray<FUpdateTexture3DData>& UpdateDataArray)
	{
		LLM_SCOPE(ELLMTag::Textures);
		GDynamicRHI->EndMultiUpdateTexture3D_RenderThread(*this, UpdateDataArray);
	}
	
	FORCEINLINE void UpdateTexture3D(FRHITexture3D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion3D& UpdateRegion, uint32 SourceRowPitch, uint32 SourceDepthPitch, const uint8* SourceData)
	{
		checkf(UpdateRegion.DestX + UpdateRegion.Width <= Texture->GetSizeX(), TEXT("UpdateTexture3D out of bounds on X. Texture: %s, %i, %i, %i"), *Texture->GetName().ToString(), UpdateRegion.DestX, UpdateRegion.Width, Texture->GetSizeX());
		checkf(UpdateRegion.DestY + UpdateRegion.Height <= Texture->GetSizeY(), TEXT("UpdateTexture3D out of bounds on Y. Texture: %s, %i, %i, %i"), *Texture->GetName().ToString(), UpdateRegion.DestY, UpdateRegion.Height, Texture->GetSizeY());
		checkf(UpdateRegion.DestZ + UpdateRegion.Depth <= Texture->GetSizeZ(), TEXT("UpdateTexture3D out of bounds on Z. Texture: %s, %i, %i, %i"), *Texture->GetName().ToString(), UpdateRegion.DestZ, UpdateRegion.Depth, Texture->GetSizeZ());
		LLM_SCOPE(ELLMTag::Textures);
		GDynamicRHI->UpdateTexture3D_RenderThread(*this, Texture, MipIndex, UpdateRegion, SourceRowPitch, SourceDepthPitch, SourceData);
	}
	
	FORCEINLINE FTextureCubeRHIRef CreateTextureCube(uint32 Size, uint8 Format, uint32 NumMips, uint32 Flags, FRHIResourceCreateInfo& CreateInfo)
	{
		LLM_SCOPE((Flags & (TexCreate_RenderTargetable | TexCreate_DepthStencilTargetable)) != 0 ? ELLMTag::RenderTargets : ELLMTag::Textures);
		return GDynamicRHI->RHICreateTextureCube_RenderThread(*this, Size, Format, NumMips, Flags, CreateInfo);
	}
	
	FORCEINLINE FTextureCubeRHIRef CreateTextureCubeArray(uint32 Size, uint32 ArraySize, uint8 Format, uint32 NumMips, uint32 Flags, FRHIResourceCreateInfo& CreateInfo)
	{
		LLM_SCOPE((Flags & (TexCreate_RenderTargetable | TexCreate_DepthStencilTargetable)) != 0 ? ELLMTag::RenderTargets : ELLMTag::Textures);
		return GDynamicRHI->RHICreateTextureCubeArray_RenderThread(*this, Size, ArraySize, Format, NumMips, Flags, CreateInfo);
	}
	
	FORCEINLINE void* LockTextureCubeFace(FRHITextureCube* Texture, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail)
	{
		LLM_SCOPE(ELLMTag::Textures);
		return GDynamicRHI->RHILockTextureCubeFace_RenderThread(*this, Texture, FaceIndex, ArrayIndex, MipIndex, LockMode, DestStride, bLockWithinMiptail);
	}
	
	FORCEINLINE void UnlockTextureCubeFace(FRHITextureCube* Texture, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, bool bLockWithinMiptail)
	{
		LLM_SCOPE(ELLMTag::Textures);
		GDynamicRHI->RHIUnlockTextureCubeFace_RenderThread(*this, Texture, FaceIndex, ArrayIndex, MipIndex, bLockWithinMiptail);
	}
	
	FORCEINLINE void BindDebugLabelName(FRHITexture* Texture, const TCHAR* Name)
	{
		RHIBindDebugLabelName(Texture, Name);
	}

	FORCEINLINE void BindDebugLabelName(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const TCHAR* Name)
	{
		RHIBindDebugLabelName(UnorderedAccessViewRHI, Name);
	}

	FORCEINLINE void ReadSurfaceData(FRHITexture* Texture,FIntRect Rect,TArray<FColor>& OutData,FReadSurfaceDataFlags InFlags)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_ReadSurfaceData_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread);  
		GDynamicRHI->RHIReadSurfaceData(Texture,Rect,OutData,InFlags);
	}

	FORCEINLINE void ReadSurfaceData(FRHITexture* Texture, FIntRect Rect, TArray<FLinearColor>& OutData, FReadSurfaceDataFlags InFlags)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_ReadSurfaceData_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread);
		GDynamicRHI->RHIReadSurfaceData(Texture, Rect, OutData, InFlags);
	}
	
	FORCEINLINE void MapStagingSurface(FRHITexture* Texture,void*& OutData,int32& OutWidth,int32& OutHeight)
	{
		GDynamicRHI->RHIMapStagingSurface_RenderThread(*this, Texture,OutData,OutWidth,OutHeight);
	}
	
	FORCEINLINE void UnmapStagingSurface(FRHITexture* Texture)
	{
		GDynamicRHI->RHIUnmapStagingSurface_RenderThread(*this, Texture);
	}
	
	FORCEINLINE void ReadSurfaceFloatData(FRHITexture* Texture,FIntRect Rect,TArray<FFloat16Color>& OutData,ECubeFace CubeFace,int32 ArrayIndex,int32 MipIndex)
	{
		LLM_SCOPE(ELLMTag::Textures);
		GDynamicRHI->RHIReadSurfaceFloatData_RenderThread(*this, Texture,Rect,OutData,CubeFace,ArrayIndex,MipIndex);
	}

	FORCEINLINE void Read3DSurfaceFloatData(FRHITexture* Texture,FIntRect Rect,FIntPoint ZMinMax,TArray<FFloat16Color>& OutData)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_Read3DSurfaceFloatData_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread);  
		GDynamicRHI->RHIRead3DSurfaceFloatData(Texture,Rect,ZMinMax,OutData);
	}
	
	UE_DEPRECATED(4.23, "CreateRenderQuery API is deprecated; use RHICreateRenderQueryPool and suballocate queries there")
	FORCEINLINE FRenderQueryRHIRef CreateRenderQuery(ERenderQueryType QueryType)
	{
		FScopedRHIThreadStaller StallRHIThread(*this);
		return GDynamicRHI->RHICreateRenderQuery(QueryType);
	}

	UE_DEPRECATED(4.23, "CreateRenderQuery API is deprecated; use RHICreateRenderQueryPool and suballocate queries there")
	FORCEINLINE FRenderQueryRHIRef CreateRenderQuery_RenderThread(ERenderQueryType QueryType)
	{
		return GDynamicRHI->RHICreateRenderQuery_RenderThread(*this, QueryType);
	}


	FORCEINLINE void AcquireTransientResource_RenderThread(FRHITexture* Texture)
	{
		if (!Texture->IsCommitted() )
		{
			if (GSupportsTransientResourceAliasing)
			{
				GDynamicRHI->RHIAcquireTransientResource_RenderThread(Texture);
			}
			Texture->SetCommitted(true);
		}
	}

	FORCEINLINE void DiscardTransientResource_RenderThread(FRHITexture* Texture)
	{
		if (Texture->IsCommitted())
		{
			if (GSupportsTransientResourceAliasing)
			{
				GDynamicRHI->RHIDiscardTransientResource_RenderThread(Texture);
			}
			Texture->SetCommitted(false);
		}
	}

	FORCEINLINE void AcquireTransientResource_RenderThread(FRHIVertexBuffer* Buffer)
	{
		if (!Buffer->IsCommitted())
		{
			if (GSupportsTransientResourceAliasing)
			{
				GDynamicRHI->RHIAcquireTransientResource_RenderThread(Buffer);
			}
			Buffer->SetCommitted(true);
		}
	}

	FORCEINLINE void DiscardTransientResource_RenderThread(FRHIVertexBuffer* Buffer)
	{
		if (Buffer->IsCommitted())
		{
			if (GSupportsTransientResourceAliasing)
			{
				GDynamicRHI->RHIDiscardTransientResource_RenderThread(Buffer);
			}
			Buffer->SetCommitted(false);
		}
	}

	FORCEINLINE void AcquireTransientResource_RenderThread(FRHIStructuredBuffer* Buffer)
	{
		if (!Buffer->IsCommitted())
		{
			if (GSupportsTransientResourceAliasing)
			{
				GDynamicRHI->RHIAcquireTransientResource_RenderThread(Buffer);
			}
			Buffer->SetCommitted(true);
		}
	}

	FORCEINLINE void DiscardTransientResource_RenderThread(FRHIStructuredBuffer* Buffer)
	{
		if (Buffer->IsCommitted())
		{
			if (GSupportsTransientResourceAliasing)
			{
				GDynamicRHI->RHIDiscardTransientResource_RenderThread(Buffer);
			}
			Buffer->SetCommitted(false);
		}
	}

	FORCEINLINE bool GetRenderQueryResult(FRHIRenderQuery* RenderQuery, uint64& OutResult, bool bWait)
	{
		return RHIGetRenderQueryResult(RenderQuery, OutResult, bWait);
	}
	
	FORCEINLINE uint32 GetViewportNextPresentGPUIndex(FRHIViewport* Viewport)
	{
		return GDynamicRHI->RHIGetViewportNextPresentGPUIndex(Viewport);
	}

	FORCEINLINE FTexture2DRHIRef GetViewportBackBuffer(FRHIViewport* Viewport)
	{
		return RHIGetViewportBackBuffer(Viewport);
	}
	
	FORCEINLINE void AdvanceFrameForGetViewportBackBuffer(FRHIViewport* Viewport)
	{
		return RHIAdvanceFrameForGetViewportBackBuffer(Viewport);
	}
	
	FORCEINLINE void AcquireThreadOwnership()
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_AcquireThreadOwnership_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread); 
		 
		return GDynamicRHI->RHIAcquireThreadOwnership();
	}
	
	FORCEINLINE void ReleaseThreadOwnership()
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_ReleaseThreadOwnership_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread); 
		 
		return GDynamicRHI->RHIReleaseThreadOwnership();
	}
	
	FORCEINLINE void FlushResources()
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_FlushResources_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread); 
		 
		return GDynamicRHI->RHIFlushResources();
	}
	
	FORCEINLINE uint32 GetGPUFrameCycles()
	{
		return RHIGetGPUFrameCycles();
	}
	
	FORCEINLINE FViewportRHIRef CreateViewport(void* WindowHandle, uint32 SizeX, uint32 SizeY, bool bIsFullscreen, EPixelFormat PreferredPixelFormat)
	{
		LLM_SCOPE(ELLMTag::RenderTargets);
		return RHICreateViewport(WindowHandle, SizeX, SizeY, bIsFullscreen, PreferredPixelFormat);
	}
	
	FORCEINLINE void ResizeViewport(FRHIViewport* Viewport, uint32 SizeX, uint32 SizeY, bool bIsFullscreen, EPixelFormat PreferredPixelFormat)
	{
		LLM_SCOPE(ELLMTag::RenderTargets);
		RHIResizeViewport(Viewport, SizeX, SizeY, bIsFullscreen, PreferredPixelFormat);
	}
	
	FORCEINLINE void Tick(float DeltaTime)
	{
		LLM_SCOPE(ELLMTag::RHIMisc);
		RHITick(DeltaTime);
	}
	
	FORCEINLINE void SetStreamOutTargets(uint32 NumTargets, FRHIVertexBuffer* const* VertexBuffers,const uint32* Offsets)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_SetStreamOutTargets_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread);  
		GDynamicRHI->RHISetStreamOutTargets(NumTargets,VertexBuffers,Offsets);
	}
	
	FORCEINLINE void BlockUntilGPUIdle()
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_BlockUntilGPUIdle_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread);  
		GDynamicRHI->RHIBlockUntilGPUIdle();
	}

	FORCEINLINE_DEBUGGABLE void SubmitCommandsAndFlushGPU()
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_SubmitCommandsAndFlushGPU_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread);
		GDynamicRHI->RHISubmitCommandsAndFlushGPU();
	}
	
	FORCEINLINE void SuspendRendering()
	{
		RHISuspendRendering();
	}
	
	FORCEINLINE void ResumeRendering()
	{
		RHIResumeRendering();
	}
	
	FORCEINLINE bool IsRenderingSuspended()
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_IsRenderingSuspended_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread); 
		return GDynamicRHI->RHIIsRenderingSuspended();
	}

	FORCEINLINE bool EnqueueDecompress(uint8_t* SrcBuffer, uint8_t* DestBuffer, int CompressedSize, void* ErrorCodeBuffer)
	{
		return GDynamicRHI->RHIEnqueueDecompress(SrcBuffer, DestBuffer, CompressedSize, ErrorCodeBuffer);
	}

	FORCEINLINE bool EnqueueCompress(uint8_t* SrcBuffer, uint8_t* DestBuffer, int UnCompressedSize, void* ErrorCodeBuffer)
	{
		return GDynamicRHI->RHIEnqueueCompress(SrcBuffer, DestBuffer, UnCompressedSize, ErrorCodeBuffer);
	}
	
	FORCEINLINE bool GetAvailableResolutions(FScreenResolutionArray& Resolutions, bool bIgnoreRefreshRate)
	{
		return RHIGetAvailableResolutions(Resolutions, bIgnoreRefreshRate);
	}
	
	FORCEINLINE void GetSupportedResolution(uint32& Width, uint32& Height)
	{
		RHIGetSupportedResolution(Width, Height);
	}
	
	FORCEINLINE void VirtualTextureSetFirstMipInMemory(FRHITexture2D* Texture, uint32 FirstMip)
	{
		GDynamicRHI->VirtualTextureSetFirstMipInMemory_RenderThread(*this, Texture, FirstMip);
	}
	
	FORCEINLINE void VirtualTextureSetFirstMipVisible(FRHITexture2D* Texture, uint32 FirstMip)
	{
		GDynamicRHI->VirtualTextureSetFirstMipVisible_RenderThread(*this, Texture, FirstMip);
	}

	UE_DEPRECATED(4.23, "CopySubTextureRegion API is deprecated; please use CopyTexture instead.")
	FORCEINLINE void CopySubTextureRegion(FRHITexture2D* SourceTexture, FRHITexture2D* DestinationTexture, FBox2D SourceBox, FBox2D DestinationBox)
	{
		GDynamicRHI->RHICopySubTextureRegion_RenderThread(*this, SourceTexture, DestinationTexture, SourceBox, DestinationBox);
	}
	
	FORCEINLINE void ExecuteCommandList(FRHICommandList* CmdList)
	{
		FScopedRHIThreadStaller StallRHIThread(*this);
		GDynamicRHI->RHIExecuteCommandList(CmdList);
	}
	
	FORCEINLINE void* GetNativeDevice()
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_GetNativeDevice_Flush);
		ImmediateFlush(EImmediateFlushType::FlushRHIThread); 
		 
		return GDynamicRHI->RHIGetNativeDevice();
	}
	
	FORCEINLINE class IRHICommandContext* GetDefaultContext()
	{
		return RHIGetDefaultContext();
	}
	
	FORCEINLINE class IRHICommandContextContainer* GetCommandContextContainer(int32 Index, int32 Num)
	{
		return RHIGetCommandContextContainer(Index, Num, GetGPUMask());
	}
	void UpdateTextureReference(FRHITextureReference* TextureRef, FRHITexture* NewTexture);

	FORCEINLINE void PollRenderQueryResults()
	{
		GDynamicRHI->RHIPollRenderQueryResults();
	}

	/**
	 * @param UpdateInfos - an array of update infos
	 * @param Num - number of update infos
	 * @param bNeedReleaseRefs - whether Release need to be called on RHI resources referenced by update infos
	 */
	void UpdateRHIResources(FRHIResourceUpdateInfo* UpdateInfos, int32 Num, bool bNeedReleaseRefs);
};

 struct FScopedGPUMask
{
	FRHICommandListImmediate& RHICmdList;
	FRHIGPUMask PrevGPUMask;
	FORCEINLINE FScopedGPUMask(FRHICommandListImmediate& InRHICmdList, FRHIGPUMask InGPUMask)
		: RHICmdList(InRHICmdList)
		, PrevGPUMask(InRHICmdList.GetGPUMask())
	{
		InRHICmdList.SetGPUMask(InGPUMask);
	}
	FORCEINLINE ~FScopedGPUMask() { RHICmdList.SetGPUMask(PrevGPUMask); }
};

#if WITH_MGPU
	#define SCOPED_GPU_MASK(RHICmdList, GPUMask) FScopedGPUMask ScopedGPUMask(RHICmdList, GPUMask);
#else
	#define SCOPED_GPU_MASK(RHICmdList, GPUMask)
#endif // WITH_MGPU

// Single commandlist for async compute generation.  In the future we may expand this to allow async compute command generation
// on multiple threads at once.
class RHI_API FRHIAsyncComputeCommandListImmediate : public FRHIAsyncComputeCommandList
{
public:

	//If RHIThread is enabled this will dispatch all current commands to the RHI Thread.  If RHI thread is disabled
	//this will immediately execute the current commands.
	//This also queues a GPU Submission command as the final command in the dispatch.
	static void ImmediateDispatch(FRHIAsyncComputeCommandListImmediate& RHIComputeCmdList);

	/** Dispatch current work and change the GPUMask. */
	void SetGPUMask(FRHIGPUMask InGPUMask);
private:
};

// typedef to mark the recursive use of commandlists in the RHI implementations

class RHI_API FRHICommandList_RecursiveHazardous : public FRHICommandList
{
	FRHICommandList_RecursiveHazardous()
		: FRHICommandList(FRHIGPUMask::All())
	{
		bAsyncPSOCompileAllowed = false;
	}
public:
	FRHICommandList_RecursiveHazardous(IRHICommandContext *Context)
		: FRHICommandList(FRHIGPUMask::All())
	{
		SetContext(Context);
		bAsyncPSOCompileAllowed = false;
	}
};


// This controls if the cmd list bypass can be toggled at runtime. It is quite expensive to have these branches in there.
#define CAN_TOGGLE_COMMAND_LIST_BYPASS (!UE_BUILD_SHIPPING && !UE_BUILD_TEST)

class RHI_API FRHICommandListExecutor
{
public:
	enum
	{
		DefaultBypass = PLATFORM_RHITHREAD_DEFAULT_BYPASS
	};
	FRHICommandListExecutor()
		: bLatchedBypass(!!DefaultBypass)
		, bLatchedUseParallelAlgorithms(false)
	{
	}
	static inline FRHICommandListImmediate& GetImmediateCommandList();
	static inline FRHIAsyncComputeCommandListImmediate& GetImmediateAsyncComputeCommandList();

	void ExecuteList(FRHICommandListBase& CmdList);
	void ExecuteList(FRHICommandListImmediate& CmdList);
	void LatchBypass();

	static void WaitOnRHIThreadFence(FGraphEventRef& Fence);

	FORCEINLINE_DEBUGGABLE bool Bypass()
	{
#if CAN_TOGGLE_COMMAND_LIST_BYPASS
		return bLatchedBypass;
#else
		return !!DefaultBypass;
#endif
	}
	FORCEINLINE_DEBUGGABLE bool UseParallelAlgorithms()
	{
#if CAN_TOGGLE_COMMAND_LIST_BYPASS
		return bLatchedUseParallelAlgorithms;
#else
		return  FApp::ShouldUseThreadingForPerformance() && !Bypass() && (GSupportsParallelRenderingTasksWithSeparateRHIThread || !IsRunningRHIInSeparateThread());
#endif
	}
	static void CheckNoOutstandingCmdLists();
	static bool IsRHIThreadActive();
	static bool IsRHIThreadCompletelyFlushed();

private:

	void ExecuteInner(FRHICommandListBase& CmdList);
	friend class FExecuteRHIThreadTask;
	static void ExecuteInner_DoExecute(FRHICommandListBase& CmdList);

	bool bLatchedBypass;
	bool bLatchedUseParallelAlgorithms;
	friend class FRHICommandListBase;
	FThreadSafeCounter UIDCounter;
	FThreadSafeCounter OutstandingCmdListCount;
	FRHICommandListImmediate CommandListImmediate;
	FRHIAsyncComputeCommandListImmediate AsyncComputeCmdListImmediate;
};

extern RHI_API FRHICommandListExecutor GRHICommandList;

extern RHI_API FAutoConsoleTaskPriority CPrio_SceneRenderingTask;

class FRenderTask
{
public:
	FORCEINLINE static ENamedThreads::Type GetDesiredThread()
	{
		return CPrio_SceneRenderingTask.Get();
	}
};


FORCEINLINE_DEBUGGABLE FRHICommandListImmediate& FRHICommandListExecutor::GetImmediateCommandList()
{
	return GRHICommandList.CommandListImmediate;
}

FORCEINLINE_DEBUGGABLE FRHIAsyncComputeCommandListImmediate& FRHICommandListExecutor::GetImmediateAsyncComputeCommandList()
{
	return GRHICommandList.AsyncComputeCmdListImmediate;
}

struct FScopedCommandListWaitForTasks
{
	FRHICommandListImmediate& RHICmdList;
	bool bWaitForTasks;

	FScopedCommandListWaitForTasks(bool InbWaitForTasks, FRHICommandListImmediate& InRHICmdList = FRHICommandListExecutor::GetImmediateCommandList())
		: RHICmdList(InRHICmdList)
		, bWaitForTasks(InbWaitForTasks)
	{
	}
	RHI_API ~FScopedCommandListWaitForTasks();
};


FORCEINLINE FPixelShaderRHIRef RHICreatePixelShader(const TArray<uint8>& Code)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreatePixelShader(Code);
}

FORCEINLINE FPixelShaderRHIRef RHICreatePixelShader(FRHIShaderLibrary* Library, FSHAHash Hash)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreatePixelShader(Library, Hash);
}

FORCEINLINE FVertexShaderRHIRef RHICreateVertexShader(const TArray<uint8>& Code)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateVertexShader(Code);
}

FORCEINLINE FVertexShaderRHIRef RHICreateVertexShader(FRHIShaderLibrary* Library, FSHAHash Hash)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateVertexShader(Library, Hash);
}

FORCEINLINE FHullShaderRHIRef RHICreateHullShader(const TArray<uint8>& Code)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateHullShader(Code);
}

FORCEINLINE FHullShaderRHIRef RHICreateHullShader(FRHIShaderLibrary* Library, FSHAHash Hash)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateHullShader(Library, Hash);
}

FORCEINLINE FDomainShaderRHIRef RHICreateDomainShader(const TArray<uint8>& Code)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateDomainShader(Code);
}

FORCEINLINE FDomainShaderRHIRef RHICreateDomainShader(FRHIShaderLibrary* Library, FSHAHash Hash)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateDomainShader(Library, Hash);
}

FORCEINLINE FGeometryShaderRHIRef RHICreateGeometryShader(const TArray<uint8>& Code)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateGeometryShader(Code);
}

FORCEINLINE FGeometryShaderRHIRef RHICreateGeometryShader(FRHIShaderLibrary* Library, FSHAHash Hash)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateGeometryShader(Library, Hash);
}

UE_DEPRECATED(4.23, "Geometry Stream out is deprecated.")
FORCEINLINE FGeometryShaderRHIRef RHICreateGeometryShaderWithStreamOutput(const TArray<uint8>& Code, const FStreamOutElementList& ElementList, uint32 NumStrides, const uint32* Strides, int32 RasterizedStream)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return FRHICommandListExecutor::GetImmediateCommandList().CreateGeometryShaderWithStreamOutput(Code, ElementList, NumStrides, Strides, RasterizedStream);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

UE_DEPRECATED(4.23, "Geometry Stream out is deprecated.")
FORCEINLINE FGeometryShaderRHIRef RHICreateGeometryShaderWithStreamOutput(const FStreamOutElementList& ElementList, uint32 NumStrides, const uint32* Strides, int32 RasterizedStream, FRHIShaderLibrary* Library, FSHAHash Hash)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return FRHICommandListExecutor::GetImmediateCommandList().CreateGeometryShaderWithStreamOutput(ElementList, NumStrides, Strides, RasterizedStream, Library, Hash);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

FORCEINLINE FComputeShaderRHIRef RHICreateComputeShader(const TArray<uint8>& Code)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateComputeShader(Code);
}

FORCEINLINE FComputeShaderRHIRef RHICreateComputeShader(FRHIShaderLibrary* Library, FSHAHash Hash)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateComputeShader(Library, Hash);
}

FORCEINLINE FComputeFenceRHIRef RHICreateComputeFence(const FName& Name)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateComputeFence(Name);
}

FORCEINLINE FGPUFenceRHIRef RHICreateGPUFence(const FName& Name)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateGPUFence(Name);
}

FORCEINLINE FStagingBufferRHIRef RHICreateStagingBuffer()
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateStagingBuffer();
}

FORCEINLINE FIndexBufferRHIRef RHICreateAndLockIndexBuffer(uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo, void*& OutDataBuffer)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateAndLockIndexBuffer(Stride, Size, InUsage, CreateInfo, OutDataBuffer);
}

FORCEINLINE FIndexBufferRHIRef RHICreateIndexBuffer(uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateIndexBuffer(Stride, Size, InUsage, CreateInfo);
}

FORCEINLINE FIndexBufferRHIRef RHIAsyncCreateIndexBuffer(uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
{
	return GDynamicRHI->RHICreateIndexBuffer(Stride, Size, InUsage, CreateInfo);
}

FORCEINLINE void* RHILockIndexBuffer(FRHIIndexBuffer* IndexBuffer, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	return FRHICommandListExecutor::GetImmediateCommandList().LockIndexBuffer(IndexBuffer, Offset, Size, LockMode);
}

FORCEINLINE void RHIUnlockIndexBuffer(FRHIIndexBuffer* IndexBuffer)
{
	 FRHICommandListExecutor::GetImmediateCommandList().UnlockIndexBuffer(IndexBuffer);
}

FORCEINLINE FVertexBufferRHIRef RHICreateAndLockVertexBuffer(uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo, void*& OutDataBuffer)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateAndLockVertexBuffer(Size, InUsage, CreateInfo, OutDataBuffer);
}

FORCEINLINE FVertexBufferRHIRef RHICreateVertexBuffer(uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateVertexBuffer(Size, InUsage, CreateInfo);
}

FORCEINLINE FVertexBufferRHIRef RHIAsyncCreateVertexBuffer(uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
{
	return GDynamicRHI->RHICreateVertexBuffer(Size, InUsage, CreateInfo);
}

FORCEINLINE void* RHILockVertexBuffer(FRHIVertexBuffer* VertexBuffer, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode)
{
	return FRHICommandListExecutor::GetImmediateCommandList().LockVertexBuffer(VertexBuffer, Offset, SizeRHI, LockMode);
}

FORCEINLINE void RHIUnlockVertexBuffer(FRHIVertexBuffer* VertexBuffer)
{
	 FRHICommandListExecutor::GetImmediateCommandList().UnlockVertexBuffer(VertexBuffer);
}

FORCEINLINE FStructuredBufferRHIRef RHICreateStructuredBuffer(uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateStructuredBuffer(Stride, Size, InUsage, CreateInfo);
}

FORCEINLINE void* RHILockStructuredBuffer(FRHIStructuredBuffer* StructuredBuffer, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode)
{
	return FRHICommandListExecutor::GetImmediateCommandList().LockStructuredBuffer(StructuredBuffer, Offset, SizeRHI, LockMode);
}

FORCEINLINE void RHIUnlockStructuredBuffer(FRHIStructuredBuffer* StructuredBuffer)
{
	 FRHICommandListExecutor::GetImmediateCommandList().UnlockStructuredBuffer(StructuredBuffer);
}

FORCEINLINE FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHIStructuredBuffer* StructuredBuffer, bool bUseUAVCounter, bool bAppendBuffer)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateUnorderedAccessView(StructuredBuffer, bUseUAVCounter, bAppendBuffer);
}

FORCEINLINE FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHITexture* Texture, uint32 MipLevel = 0)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateUnorderedAccessView(Texture, MipLevel);
}

FORCEINLINE FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHIVertexBuffer* VertexBuffer, uint8 Format)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateUnorderedAccessView(VertexBuffer, Format);
}

FORCEINLINE FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHIIndexBuffer* IndexBuffer, uint8 Format)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateUnorderedAccessView(IndexBuffer, Format);
}

FORCEINLINE FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHIStructuredBuffer* StructuredBuffer)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateShaderResourceView(StructuredBuffer);
}

FORCEINLINE FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateShaderResourceView(VertexBuffer, Stride, Format);
}

FORCEINLINE FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHIIndexBuffer* Buffer)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateShaderResourceView(Buffer);
}

FORCEINLINE void RHIUpdateRHIResources(FRHIResourceUpdateInfo* UpdateInfos, int32 Num, bool bNeedReleaseRefs)
{
	return FRHICommandListExecutor::GetImmediateCommandList().UpdateRHIResources(UpdateInfos, Num, bNeedReleaseRefs);
}

FORCEINLINE FTextureReferenceRHIRef RHICreateTextureReference(FLastRenderTimeContainer* LastRenderTime)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateTextureReference(LastRenderTime);
}

FORCEINLINE void RHIUpdateTextureReference(FRHITextureReference* TextureRef, FRHITexture* NewTexture)
{
	 FRHICommandListExecutor::GetImmediateCommandList().UpdateTextureReference(TextureRef, NewTexture);
}

FORCEINLINE FTexture2DRHIRef RHICreateTexture2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, uint32 Flags, FRHIResourceCreateInfo& CreateInfo)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateTexture2D(SizeX, SizeY, Format, NumMips, NumSamples, Flags, CreateInfo);
}

FORCEINLINE FStructuredBufferRHIRef RHICreateRTWriteMaskBuffer(FTexture2DRHIRef RenderTarget)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateRTWriteMaskBuffer(RenderTarget);
}

FORCEINLINE FTexture2DRHIRef RHIAsyncCreateTexture2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 Flags, void** InitialMipData, uint32 NumInitialMips)
{
	return FRHICommandListExecutor::GetImmediateCommandList().AsyncCreateTexture2D(SizeX, SizeY, Format, NumMips, Flags, InitialMipData, NumInitialMips);
}

FORCEINLINE void RHICopySharedMips(FRHITexture2D* DestTexture2D, FRHITexture2D* SrcTexture2D)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CopySharedMips(DestTexture2D, SrcTexture2D);
}

FORCEINLINE void RHITransferTexture(FRHITexture2D* Texture, FIntRect Rect, uint32 SrcGPUIndex, uint32 DestGPUIndex, bool PullData)
{
	return FRHICommandListExecutor::GetImmediateCommandList().TransferTexture(Texture, Rect, SrcGPUIndex, DestGPUIndex, PullData);
}

FORCEINLINE FTexture2DArrayRHIRef RHICreateTexture2DArray(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, uint32 NumSamples, uint32 Flags, FRHIResourceCreateInfo& CreateInfo)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateTexture2DArray(SizeX, SizeY, SizeZ, Format, NumMips, NumSamples, Flags, CreateInfo);
}

UE_DEPRECATED(4.23, "RHICreateTexture2DArray now takes NumSamples")
FORCEINLINE FTexture2DArrayRHIRef RHICreateTexture2DArray(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, uint32 Flags, FRHIResourceCreateInfo& CreateInfo)
{
	return RHICreateTexture2DArray(SizeX, SizeY, SizeZ, Format, NumMips, 1, Flags, CreateInfo);
}

FORCEINLINE FTexture3DRHIRef RHICreateTexture3D(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, uint32 Flags, FRHIResourceCreateInfo& CreateInfo)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateTexture3D(SizeX, SizeY, SizeZ, Format, NumMips, Flags, CreateInfo);
}

FORCEINLINE FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHITexture* Texture, uint8 MipLevel)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateShaderResourceView(Texture, MipLevel);
}

FORCEINLINE FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHITexture* Texture, uint8 MipLevel, uint8 NumMipLevels, uint8 Format)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateShaderResourceView(Texture, MipLevel, NumMipLevels, Format);
}

FORCEINLINE FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHITexture* Texture, const FRHITextureSRVCreateInfo& CreateInfo)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateShaderResourceView(Texture, CreateInfo);
}

FORCEINLINE FTexture2DRHIRef RHIAsyncReallocateTexture2D(FRHITexture2D* Texture2D, int32 NewMipCount, int32 NewSizeX, int32 NewSizeY, FThreadSafeCounter* RequestStatus)
{
	return FRHICommandListExecutor::GetImmediateCommandList().AsyncReallocateTexture2D(Texture2D, NewMipCount, NewSizeX, NewSizeY, RequestStatus);
}

FORCEINLINE ETextureReallocationStatus RHIFinalizeAsyncReallocateTexture2D(FRHITexture2D* Texture2D, bool bBlockUntilCompleted)
{
	return FRHICommandListExecutor::GetImmediateCommandList().FinalizeAsyncReallocateTexture2D(Texture2D, bBlockUntilCompleted);
}

FORCEINLINE ETextureReallocationStatus RHICancelAsyncReallocateTexture2D(FRHITexture2D* Texture2D, bool bBlockUntilCompleted)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CancelAsyncReallocateTexture2D(Texture2D, bBlockUntilCompleted);
}

FORCEINLINE void* RHILockTexture2D(FRHITexture2D* Texture, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail, bool bFlushRHIThread = true)
{
	return FRHICommandListExecutor::GetImmediateCommandList().LockTexture2D(Texture, MipIndex, LockMode, DestStride, bLockWithinMiptail, bFlushRHIThread);
}

FORCEINLINE void RHIUnlockTexture2D(FRHITexture2D* Texture, uint32 MipIndex, bool bLockWithinMiptail, bool bFlushRHIThread = true)
{
	 FRHICommandListExecutor::GetImmediateCommandList().UnlockTexture2D(Texture, MipIndex, bLockWithinMiptail, bFlushRHIThread);
}

FORCEINLINE void* RHILockTexture2DArray(FRHITexture2DArray* Texture, uint32 TextureIndex, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail)
{
	return FRHICommandListExecutor::GetImmediateCommandList().LockTexture2DArray(Texture, TextureIndex, MipIndex, LockMode, DestStride, bLockWithinMiptail);
}

FORCEINLINE void RHIUnlockTexture2DArray(FRHITexture2DArray* Texture, uint32 TextureIndex, uint32 MipIndex, bool bLockWithinMiptail)
{
	 FRHICommandListExecutor::GetImmediateCommandList().UnlockTexture2DArray(Texture, TextureIndex, MipIndex, bLockWithinMiptail);
}

FORCEINLINE void RHIUpdateTexture2D(FRHITexture2D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion2D& UpdateRegion, uint32 SourcePitch, const uint8* SourceData)
{
	 FRHICommandListExecutor::GetImmediateCommandList().UpdateTexture2D(Texture, MipIndex, UpdateRegion, SourcePitch, SourceData);
}

FORCEINLINE FUpdateTexture3DData RHIBeginUpdateTexture3D(FRHITexture3D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion3D& UpdateRegion)
{
	return FRHICommandListExecutor::GetImmediateCommandList().BeginUpdateTexture3D(Texture, MipIndex, UpdateRegion);
}

FORCEINLINE void RHIEndUpdateTexture3D(FUpdateTexture3DData& UpdateData)
{
	FRHICommandListExecutor::GetImmediateCommandList().EndUpdateTexture3D(UpdateData);
}

FORCEINLINE void RHIEndMultiUpdateTexture3D(TArray<FUpdateTexture3DData>& UpdateDataArray)
{
	FRHICommandListExecutor::GetImmediateCommandList().EndMultiUpdateTexture3D(UpdateDataArray);
}

FORCEINLINE void RHIUpdateTexture3D(FRHITexture3D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion3D& UpdateRegion, uint32 SourceRowPitch, uint32 SourceDepthPitch, const uint8* SourceData)
{
	 FRHICommandListExecutor::GetImmediateCommandList().UpdateTexture3D(Texture, MipIndex, UpdateRegion, SourceRowPitch, SourceDepthPitch, SourceData);
}

FORCEINLINE FTextureCubeRHIRef RHICreateTextureCube(uint32 Size, uint8 Format, uint32 NumMips, uint32 Flags, FRHIResourceCreateInfo& CreateInfo)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateTextureCube(Size, Format, NumMips, Flags, CreateInfo);
}

FORCEINLINE FTextureCubeRHIRef RHICreateTextureCubeArray(uint32 Size, uint32 ArraySize, uint8 Format, uint32 NumMips, uint32 Flags, FRHIResourceCreateInfo& CreateInfo)
{
	return FRHICommandListExecutor::GetImmediateCommandList().CreateTextureCubeArray(Size, ArraySize, Format, NumMips, Flags, CreateInfo);
}

FORCEINLINE void* RHILockTextureCubeFace(FRHITextureCube* Texture, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail)
{
	return FRHICommandListExecutor::GetImmediateCommandList().LockTextureCubeFace(Texture, FaceIndex, ArrayIndex, MipIndex, LockMode, DestStride, bLockWithinMiptail);
}

FORCEINLINE void RHIUnlockTextureCubeFace(FRHITextureCube* Texture, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, bool bLockWithinMiptail)
{
	 FRHICommandListExecutor::GetImmediateCommandList().UnlockTextureCubeFace(Texture, FaceIndex, ArrayIndex, MipIndex, bLockWithinMiptail);
}

UE_DEPRECATED(4.23, "CreateRenderQuery API is deprecated; use RHICreateRenderQueryPool and suballocate queries there")
FORCEINLINE FRenderQueryRHIRef RHICreateRenderQuery(ERenderQueryType QueryType)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return FRHICommandListExecutor::GetImmediateCommandList().CreateRenderQuery_RenderThread(QueryType);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

FORCEINLINE void RHIAcquireTransientResource(FRHITexture* Resource)
{
	FRHICommandListExecutor::GetImmediateCommandList().AcquireTransientResource_RenderThread(Resource);
}

FORCEINLINE void RHIDiscardTransientResource(FRHITexture* Resource)
{
	FRHICommandListExecutor::GetImmediateCommandList().DiscardTransientResource_RenderThread(Resource);
}

FORCEINLINE void RHIAcquireTransientResource(FRHIVertexBuffer* Resource)
{
	FRHICommandListExecutor::GetImmediateCommandList().AcquireTransientResource_RenderThread(Resource);
}

FORCEINLINE void RHIDiscardTransientResource(FRHIVertexBuffer* Resource)
{
	FRHICommandListExecutor::GetImmediateCommandList().DiscardTransientResource_RenderThread(Resource);
}

FORCEINLINE void RHIAcquireTransientResource(FRHIStructuredBuffer* Resource)
{
	FRHICommandListExecutor::GetImmediateCommandList().AcquireTransientResource_RenderThread(Resource);
}

FORCEINLINE void RHIDiscardTransientResource(FRHIStructuredBuffer* Resource)
{
	FRHICommandListExecutor::GetImmediateCommandList().DiscardTransientResource_RenderThread(Resource);
}

FORCEINLINE void RHIAcquireThreadOwnership()
{
	return FRHICommandListExecutor::GetImmediateCommandList().AcquireThreadOwnership();
}

FORCEINLINE void RHIReleaseThreadOwnership()
{
	return FRHICommandListExecutor::GetImmediateCommandList().ReleaseThreadOwnership();
}

FORCEINLINE void RHIFlushResources()
{
	return FRHICommandListExecutor::GetImmediateCommandList().FlushResources();
}

FORCEINLINE void RHIVirtualTextureSetFirstMipInMemory(FRHITexture2D* Texture, uint32 FirstMip)
{
	 FRHICommandListExecutor::GetImmediateCommandList().VirtualTextureSetFirstMipInMemory(Texture, FirstMip);
}

FORCEINLINE void RHIVirtualTextureSetFirstMipVisible(FRHITexture2D* Texture, uint32 FirstMip)
{
	 FRHICommandListExecutor::GetImmediateCommandList().VirtualTextureSetFirstMipVisible(Texture, FirstMip);
}

FORCEINLINE void RHIExecuteCommandList(FRHICommandList* CmdList)
{
	 FRHICommandListExecutor::GetImmediateCommandList().ExecuteCommandList(CmdList);
}

FORCEINLINE void* RHIGetNativeDevice()
{
	return FRHICommandListExecutor::GetImmediateCommandList().GetNativeDevice();
}

FORCEINLINE FRHIShaderLibraryRef RHICreateShaderLibrary(EShaderPlatform Platform, FString const& FilePath, FString const& Name)
{
    return GDynamicRHI->RHICreateShaderLibrary(Platform, FilePath, Name);
}

FORCEINLINE void* RHILockStagingBuffer(FRHIStagingBuffer* StagingBuffer, uint32 Offset, uint32 Size)
{
	return FRHICommandListExecutor::GetImmediateCommandList().LockStagingBuffer(StagingBuffer, Offset, Size);
}

FORCEINLINE void RHIUnlockStagingBuffer(FRHIStagingBuffer* StagingBuffer)
{
	 FRHICommandListExecutor::GetImmediateCommandList().UnlockStagingBuffer(StagingBuffer);
}

template <uint32 MaxNumUpdates>
struct TRHIResourceUpdateBatcher
{
	FRHIResourceUpdateInfo UpdateInfos[MaxNumUpdates];
	uint32 NumBatched;

	TRHIResourceUpdateBatcher()
		: NumBatched(0)
	{}

	~TRHIResourceUpdateBatcher()
	{
		Flush();
	}

	void Flush()
	{
		if (NumBatched > 0)
		{
			RHIUpdateRHIResources(UpdateInfos, NumBatched, true);
			NumBatched = 0;
		}
	}

	void QueueUpdateRequest(FRHIVertexBuffer* DestVertexBuffer, FRHIVertexBuffer* SrcVertexBuffer)
	{
		FRHIResourceUpdateInfo& UpdateInfo = GetNextUpdateInfo();
		UpdateInfo.Type = FRHIResourceUpdateInfo::UT_VertexBuffer;
		UpdateInfo.VertexBuffer = { DestVertexBuffer, SrcVertexBuffer };
		DestVertexBuffer->AddRef();
		if (SrcVertexBuffer)
		{
			SrcVertexBuffer->AddRef();
		}
	}

	void QueueUpdateRequest(FRHIIndexBuffer* DestIndexBuffer, FRHIIndexBuffer* SrcIndexBuffer)
	{
		FRHIResourceUpdateInfo& UpdateInfo = GetNextUpdateInfo();
		UpdateInfo.Type = FRHIResourceUpdateInfo::UT_IndexBuffer;
		UpdateInfo.IndexBuffer = { DestIndexBuffer, SrcIndexBuffer };
		DestIndexBuffer->AddRef();
		if (SrcIndexBuffer)
		{
			SrcIndexBuffer->AddRef();
		}
	}

	void QueueUpdateRequest(FRHIShaderResourceView* SRV, FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format)
	{
		FRHIResourceUpdateInfo& UpdateInfo = GetNextUpdateInfo();
		UpdateInfo.Type = FRHIResourceUpdateInfo::UT_VertexBufferSRV;
		UpdateInfo.VertexBufferSRV = { SRV, VertexBuffer, Stride, Format };
		SRV->AddRef();
		if (VertexBuffer)
		{
			VertexBuffer->AddRef();
		}
	}

	void QueueUpdateRequest(FRHIShaderResourceView* SRV, FRHIIndexBuffer* IndexBuffer)
	{
		// TODO
	}

private:
	FRHIResourceUpdateInfo & GetNextUpdateInfo()
	{
		check(NumBatched <= MaxNumUpdates);
		if (NumBatched >= MaxNumUpdates)
		{
			Flush();
		}
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 6385) // Access is alawys in-bound due to the Flush above
#endif
		return UpdateInfos[NumBatched++];
#ifdef _MSC_VER
#pragma warning(pop)
#endif
	}
};

#undef RHICOMMAND_CALLSTACK

#include "RHICommandList.inl"
