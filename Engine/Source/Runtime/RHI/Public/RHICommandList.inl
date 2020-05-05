// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RHICommandList.inl: RHI Command List inline definitions.
=============================================================================*/

#pragma once

class FRHICommandListBase;
class FRHICommandListExecutor;
class FRHICommandListImmediate;
class FRHIResource;
class FScopedRHIThreadStaller;
struct FRHICommandBase;

FORCEINLINE_DEBUGGABLE void FRHICommandListBase::Flush()
{
	if (HasCommands())
	{
		check(!IsImmediate());
		GRHICommandList.ExecuteList(*this);
	}
}

FORCEINLINE_DEBUGGABLE bool FRHICommandListBase::IsImmediate()
{
	return this == &FRHICommandListExecutor::GetImmediateCommandList();
}

FORCEINLINE_DEBUGGABLE bool FRHICommandListBase::IsImmediateAsyncCompute()
{
	return this == &FRHICommandListExecutor::GetImmediateAsyncComputeCommandList();
}

FORCEINLINE_DEBUGGABLE bool FRHICommandListBase::Bypass()
{
	return GRHICommandList.Bypass();
}

FORCEINLINE_DEBUGGABLE FScopedRHIThreadStaller::FScopedRHIThreadStaller(class FRHICommandListImmediate& InImmed)
	: Immed(nullptr)
{
	if (IsRunningRHIInSeparateThread())
	{
		check(IsInRenderingThread());
		if (InImmed.StallRHIThread())
		{
			Immed = &InImmed;
		}
	}
}

FORCEINLINE_DEBUGGABLE FScopedRHIThreadStaller::~FScopedRHIThreadStaller()
{
	if (Immed)
	{
		Immed->UnStallRHIThread();
	}
}

namespace PipelineStateCache
{
	/* Evicts unused state entries based on r.pso.evictiontime time. Called in RHICommandList::BeginFrame */
	extern RHI_API void FlushResources();
}

FORCEINLINE_DEBUGGABLE void FRHICommandListImmediate::ImmediateFlush(EImmediateFlushType::Type FlushType)
{
	switch (FlushType)
	{
	case EImmediateFlushType::WaitForOutstandingTasksOnly:
		{
			WaitForTasks();
		}
		break;
	case EImmediateFlushType::DispatchToRHIThread:
		{
			if (HasCommands())
			{
				GRHICommandList.ExecuteList(*this);
			}
		}
		break;
	case EImmediateFlushType::WaitForDispatchToRHIThread:
		{
			if (HasCommands())
			{
				GRHICommandList.ExecuteList(*this);
			}
			WaitForDispatch();
		}
		break;
	case EImmediateFlushType::FlushRHIThread:
		{
			CSV_SCOPED_TIMING_STAT(RHITFlushes, FlushRHIThreadTotal);
			if (HasCommands())
			{
				GRHICommandList.ExecuteList(*this);
			}
			WaitForDispatch();
			if (IsRunningRHIInSeparateThread())
			{
				WaitForRHIThreadTasks();
			}
			WaitForTasks(true); // these are already done, but this resets the outstanding array
		}
		break;
	case EImmediateFlushType::FlushRHIThreadFlushResources:
	case EImmediateFlushType::FlushRHIThreadFlushResourcesFlushDeferredDeletes:
		{
			CSV_SCOPED_TIMING_STAT(RHITFlushes, FlushRHIThreadFlushResourcesTotal);
			if (HasCommands())
			{
				GRHICommandList.ExecuteList(*this);
			}
			WaitForDispatch();
			WaitForRHIThreadTasks();
			WaitForTasks(true); // these are already done, but this resets the outstanding array
			PipelineStateCache::FlushResources();
			FRHIResource::FlushPendingDeletes(FlushType == EImmediateFlushType::FlushRHIThreadFlushResourcesFlushDeferredDeletes);
		}
		break;
	default:
		check(0);
	}
}


// Helper class for traversing a FRHICommandList
class FRHICommandListIterator
{
public:
	FRHICommandListIterator(FRHICommandListBase& CmdList)
	{
		CmdPtr = CmdList.Root;
		NumCommands = 0;
		CmdListNumCommands = CmdList.NumCommands;
	}
	~FRHICommandListIterator()
	{
		checkf(CmdListNumCommands == NumCommands, TEXT("Missed %d Commands!"), CmdListNumCommands - NumCommands);
	}

	FORCEINLINE_DEBUGGABLE bool HasCommandsLeft() const
	{
		return !!CmdPtr;
	}

	FORCEINLINE_DEBUGGABLE FRHICommandBase* NextCommand()
	{
		FRHICommandBase* RHICmd = CmdPtr;
		CmdPtr = RHICmd->Next;
		NumCommands++;
		return RHICmd;
	}

private:
	FRHICommandBase* CmdPtr;
	uint32 NumCommands;
	uint32 CmdListNumCommands;
};

