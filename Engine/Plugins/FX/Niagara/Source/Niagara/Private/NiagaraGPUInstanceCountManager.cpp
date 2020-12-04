// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraGPUInstanceCountManager.h"
#include "NiagaraStats.h"
#include "Containers/DynamicRHIResourceArray.h"
#include "GPUSortManager.h" // CopyUIntBufferToTargets
#include "ProfilingDebugging/RealtimeGPUProfiler.h"
#include "NiagaraRenderer.h"
#include "ClearQuad.h"

int32 GNiagaraMinGPUInstanceCount = 2048;
static FAutoConsoleVariableRef CVarNiagaraMinGPUInstanceCount(
	TEXT("Niagara.MinGPUInstanceCount"),
	GNiagaraMinGPUInstanceCount,
	TEXT("Minimum number of instance count entries allocated in the global buffer. (default=2048)"),
	ECVF_Default
);

int32 GNiagaraMinCulledGPUInstanceCount = 2048;
static FAutoConsoleVariableRef CVarNiagaraMinCulledGPUInstanceCount(
	TEXT("Niagara.MinCulledGPUInstanceCount"),
	GNiagaraMinCulledGPUInstanceCount,
	TEXT("Minimum number of culled (per-view) instance count entries allocated in the global buffer. (default=2048)"),
	ECVF_Default
);

int32 GNiagaraMinGPUDrawIndirectArgs = 256;
static FAutoConsoleVariableRef CVarNiagaraMinGPUDrawIndirectArgs(
	TEXT("Niagara.MinGPUDrawIndirectArgs"),
	GNiagaraMinGPUDrawIndirectArgs,
	TEXT("Minimum number of draw indirect argsallocated in the global buffer. (default=256)"),
	ECVF_Default
);

float GNiagaraGPUCountBufferSlack = 1.5f;
static FAutoConsoleVariableRef CVarNiagaraGPUCountBufferSlack(
	TEXT("Niagara.GPUCountBufferSlack"),
	GNiagaraGPUCountBufferSlack,
	TEXT("Multiplier of the GPU count buffer size to prevent frequent re-allocation."),
	ECVF_Default
);

float GNiagaraGPUDrawIndirectArgsBufferSlack = 2.0f;
static FAutoConsoleVariableRef CVarNiagaraGPUDrawIndirectArgsBufferSlack(
	TEXT("Niagara.GPUDrawIndirectArgsBufferSlack"),
	GNiagaraGPUDrawIndirectArgsBufferSlack,
	TEXT("Multiplier on the required indirect args buffer size to account for culling and multiple mesh sections on mesh renderers."),
	ECVF_Default
);

DECLARE_DWORD_COUNTER_STAT(TEXT("Used GPU Instance Counters"), STAT_NiagaraUsedGPUInstanceCounters, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("Max Num GPU Renderers"), STAT_NiagaraMaxNumGPURenderers, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("GPU Readback Lock"), STAT_NiagaraGPUReadbackLock, STATGROUP_Niagara);


//*****************************************************************************

FNiagaraGPUInstanceCountManager::FNiagaraGPUInstanceCountManager() 
{
	NumRegisteredGPURenderers = new FNiagaraGPURendererCount();
}

FNiagaraGPUInstanceCountManager::~FNiagaraGPUInstanceCountManager()
{
	ReleaseRHI();
}

void FNiagaraGPUInstanceCountManager::InitRHI()
{
}

void FNiagaraGPUInstanceCountManager::ReleaseRHI()
{
	CountBuffer.Release();
	CulledCountBuffer.Release();
	DrawIndirectBuffer.Release();

	AllocatedInstanceCounts = 0;
	AllocatedCulledCounts = 0;
	AllocatedDrawIndirectArgs = 0;

	if (CountReadback)
	{
		delete CountReadback;
		CountReadback = nullptr;
		CountReadbackSize = 0;
	}
}

uint32 FNiagaraGPUInstanceCountManager::AcquireEntry()
{
	checkSlow(IsInRenderingThread());

	if (FreeEntries.Num())
	{
		return FreeEntries.Pop();
	}
	else if (UsedInstanceCounts < AllocatedInstanceCounts)
	{
		// We can't reallocate on the fly, the buffer must be correctly resized before any tick gets scheduled.
		return UsedInstanceCounts++;
	}
	else
	{
		// @TODO : add realloc the buffer and copy the current content to it. Might require reallocating the readback in FNiagaraGPUInstanceCountManager::EnqueueGPUReadback()
		ensure(UsedInstanceCounts < AllocatedInstanceCounts);
		//UE_LOG(LogNiagara, Error, TEXT("Niagara.MinGPUInstanceCount too small. UsedInstanceCounts: %d < AllocatedInstanceCounts: %d"), UsedInstanceCounts, AllocatedInstanceCounts);
		return INDEX_NONE;
	}
}

void FNiagaraGPUInstanceCountManager::FreeEntry(uint32& BufferOffset)
{
	checkSlow(IsInRenderingThread());

	if (BufferOffset != INDEX_NONE)
	{
		checkf(!FreeEntries.Contains(BufferOffset), TEXT("BufferOffset %u exists in FreeEntries"), BufferOffset);
		checkf(!InstanceCountClearTasks.Contains(BufferOffset), TEXT("BufferOffset %u exists in InstanceCountClearTasks"), BufferOffset);

		//UE_LOG(LogNiagara, Warning, TEXT("FNiagaraGPUInstanceCountManager::FreeEntry %d"), BufferOffset);
		// Add a reset to 0 task.
		// The entry will only become available/reusable after being reset to 0 in UpdateDrawIndirectBuffer()
		//if (InstanceCountClearTasks.Find(BufferOffset) != -1)
		//{
		//	UE_LOG(LogNiagara, Warning, TEXT("FNiagaraGPUInstanceCountManager::FreeEntry DUPLICATED!!!!!!!!!!!!!! %d"), BufferOffset);
		//}
		InstanceCountClearTasks.Add(BufferOffset);
		BufferOffset = INDEX_NONE;
	}
}

void FNiagaraGPUInstanceCountManager::FreeEntryArray(TConstArrayView<uint32> EntryArray)
{
	checkSlow(IsInRenderingThread());

	const int32 NumToFree = EntryArray.Num();
	if (NumToFree > 0)
	{
#if DO_CHECK
		for (uint32 BufferOffset : EntryArray)
		{
			checkf(!FreeEntries.Contains(BufferOffset), TEXT("BufferOffset %u exists in FreeEntries"), BufferOffset);
			checkf(!InstanceCountClearTasks.Contains(BufferOffset), TEXT("BufferOffset %u exists in InstanceCountClearTasks"), BufferOffset);
		}
#endif
		InstanceCountClearTasks.Append(EntryArray.GetData(), NumToFree);
	}
}

FRWBuffer* FNiagaraGPUInstanceCountManager::AcquireCulledCountsBuffer(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel)
{
	if (RequiredCulledCounts > 0)
	{
		if (!bAcquiredCulledCounts)
		{
			const int32 RecommendedCulledCounts = FMath::Max(GNiagaraMinCulledGPUInstanceCount, (int32)(RequiredCulledCounts * GNiagaraGPUCountBufferSlack));
			if (RecommendedCulledCounts > AllocatedCulledCounts)
			{
				// We need a bigger buffer
				CulledCountBuffer.Release();

				AllocatedCulledCounts = RecommendedCulledCounts;
				CulledCountBuffer.Initialize(sizeof(uint32), AllocatedCulledCounts, EPixelFormat::PF_R32_UINT, BUF_Transient, TEXT("NiagaraCulledGPUInstanceCounts"));
				// The clear pass expect to find this as readable.
				RHICmdList.Transition(FRHITransitionInfo(CulledCountBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute));
			}

			CulledCountBuffer.AcquireTransientResource();

			// Initialize the buffer by clearing it to zero then transition it to be ready to write to
			RHICmdList.Transition(FRHITransitionInfo(CulledCountBuffer.UAV, ERHIAccess::SRVCompute, ERHIAccess::UAVCompute));
			RHICmdList.ClearUAVUint(CulledCountBuffer.UAV, FUintVector4(EForceInit::ForceInitToZero));
			RHICmdList.Transition(FRHITransitionInfo(CulledCountBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));
		}

		return &CulledCountBuffer;
	}	

	bAcquiredCulledCounts = true;
	return nullptr;
}

void FNiagaraGPUInstanceCountManager::ResizeBuffers(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, int32 ReservedInstanceCounts)
{
	const int32 RequiredInstanceCounts = UsedInstanceCounts + FMath::Max<int32>(ReservedInstanceCounts - FreeEntries.Num(), 0);
	const int32 ExpectedDrawIndirectArgs = NumRegisteredGPURenderers->Value; // can be more with multiple mesh sections
	if (RequiredInstanceCounts > 0 || ExpectedDrawIndirectArgs > 0)
	{
		const int32 RecommendedInstanceCounts = FMath::Max(GNiagaraMinGPUInstanceCount, (int32)(RequiredInstanceCounts * GNiagaraGPUCountBufferSlack));
		// If the buffer is not allocated, allocate it to the recommended size.
		if (!AllocatedInstanceCounts)
		{
			AllocatedInstanceCounts = RecommendedInstanceCounts;
			TResourceArray<uint32> InitData;
			InitData.AddZeroed(AllocatedInstanceCounts);
			CountBuffer.Initialize(sizeof(uint32), AllocatedInstanceCounts, EPixelFormat::PF_R32_UINT, BUF_Static | BUF_SourceCopy, TEXT("NiagaraGPUInstanceCounts"), &InitData);
			// NiagaraEmitterInstanceBatcher expects the count buffer to be readable and copyable before running the sim.
			RHICmdList.Transition(FRHITransitionInfo(CountBuffer.UAV, ERHIAccess::UAVCompute, kCountBufferDefaultState));
			//UE_LOG(LogNiagara, Log, TEXT("FNiagaraGPUInstanceCountManager::ResizeBuffers Alloc AllocatedInstanceCounts: %d ReservedInstanceCounts: %d"), AllocatedInstanceCounts, ReservedInstanceCounts);
		}
		// If we need to increase the buffer size to RecommendedInstanceCounts because the buffer is too small.
		else if (RequiredInstanceCounts > AllocatedInstanceCounts)
		{
			SCOPED_DRAW_EVENT(RHICmdList, ResizeNiagaraGPUCounts);

			// Init a bigger buffer filled with 0.
			TResourceArray<uint32> InitData;
			InitData.AddZeroed(RecommendedInstanceCounts);
			FRWBuffer NextCountBuffer;
			NextCountBuffer.Initialize(sizeof(uint32), RecommendedInstanceCounts, EPixelFormat::PF_R32_UINT, BUF_Static | BUF_SourceCopy, TEXT("NiagaraGPUInstanceCounts"), &InitData);

			// Copy the current buffer in the next buffer. We don't need to transition any of the buffers, because the current buffer is transitioned to readable after
			// the simulation, and the new buffer is created in the UAVCompute state.
			FRHIUnorderedAccessView* UAVs[] = { NextCountBuffer.UAV };
			int32 UsedIndexCounts[] = { AllocatedInstanceCounts };
			CopyUIntBufferToTargets(RHICmdList, FeatureLevel, CountBuffer.SRV, UAVs, UsedIndexCounts, 0, UE_ARRAY_COUNT(UAVs)); 

			// NiagaraEmitterInstanceBatcher expects the count buffer to be readable and copyable before running the sim.
			RHICmdList.Transition(FRHITransitionInfo(NextCountBuffer.UAV, ERHIAccess::UAVCompute, kCountBufferDefaultState));

			// Swap the buffers
			AllocatedInstanceCounts = RecommendedInstanceCounts;
			FMemory::Memswap(&NextCountBuffer, &CountBuffer, sizeof(NextCountBuffer));
			//UE_LOG(LogNiagara, Log, TEXT("FNiagaraGPUInstanceCountManager::ResizeBuffers Resize AllocatedInstanceCounts: %d ReservedInstanceCounts: %d"), AllocatedInstanceCounts, ReservedInstanceCounts);
		}
		// If we need to shrink the buffer size because use way to much buffer size.
		else if ((int32)(RecommendedInstanceCounts * GNiagaraGPUCountBufferSlack) < AllocatedInstanceCounts)
		{
			// possibly shrink but hard to do because of sparse array allocation.
		}

		// We expect more slots to be needed due to multiple mesh draws per renderer, and for culled draws per view
		const int32 RecommendedDrawIndirectArgsCount = FMath::Max(GNiagaraMinGPUDrawIndirectArgs, (int32)(ExpectedDrawIndirectArgs * GNiagaraGPUDrawIndirectArgsBufferSlack));
		if (RecommendedDrawIndirectArgsCount > AllocatedDrawIndirectArgs || (int32)(RecommendedDrawIndirectArgsCount * GNiagaraGPUDrawIndirectArgsBufferSlack) < AllocatedDrawIndirectArgs)
		{
			DrawIndirectBuffer.Release();
			AllocatedDrawIndirectArgs = RecommendedDrawIndirectArgsCount;
			TResourceArray<uint32> InitData;
			InitData.AddZeroed(RecommendedDrawIndirectArgsCount * NIAGARA_DRAW_INDIRECT_ARGS_SIZE);
			DrawIndirectBuffer.Initialize(sizeof(uint32), RecommendedDrawIndirectArgsCount * NIAGARA_DRAW_INDIRECT_ARGS_SIZE, EPixelFormat::PF_R32_UINT, BUF_Static | BUF_DrawIndirect, TEXT("NiagaraGPUDrawIndirectArgs"), &InitData);
			// The rest of the code expects this to be in the IndirectArgs state.
			RHICmdList.Transition(FRHITransitionInfo(DrawIndirectBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::IndirectArgs));
		}
	}
	else
	{
		ReleaseRHI();
	}

	INC_DWORD_STAT_BY(STAT_NiagaraUsedGPUInstanceCounters, RequiredInstanceCounts);
	INC_DWORD_STAT_BY(STAT_NiagaraMaxNumGPURenderers, ExpectedDrawIndirectArgs);
}

uint32 FNiagaraGPUInstanceCountManager::AddDrawIndirect(uint32 InstanceCountBufferOffset, uint32 NumIndicesPerInstance, uint32 StartIndexLocation,
	bool bIsInstancedStereoEnabled, bool bCulled)
{
	checkSlow(IsInRenderingThread());

	const FArgGenTaskInfo Info(InstanceCountBufferOffset, NumIndicesPerInstance, StartIndexLocation, bIsInstancedStereoEnabled, bCulled);

	uint32& CachedOffset = DrawIndirectArgMap.FindOrAdd(Info, INDEX_NONE);
	if (CachedOffset != INDEX_NONE)
	{
		return CachedOffset;
	}
	else if (DrawIndirectArgGenTasks.Num() < AllocatedDrawIndirectArgs)
	{
		DrawIndirectArgGenTasks.Add(Info);
		CachedOffset = (DrawIndirectArgGenTasks.Num() - 1) * NIAGARA_DRAW_INDIRECT_ARGS_SIZE * sizeof(uint32);
		return CachedOffset;
	}
	else
	{
		UE_LOG(LogNiagara, Error, TEXT("Ran out of space in the GPU Draw Indirect Args buffer. Increase Niagara.GPUDrawIndirectArgsBufferSlack"));
		return INDEX_NONE;
	}
}

void FNiagaraGPUInstanceCountManager::UpdateDrawIndirectBuffer(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel)
{
	if (DrawIndirectArgGenTasks.Num() || InstanceCountClearTasks.Num())
	{
		if (FNiagaraUtilities::AllowGPUParticles(GShaderPlatformForFeatureLevel[FeatureLevel]))
		{
			FReadBuffer TaskInfosBuffer;

			//for (int32 i = 0; i < InstanceCountClearTasks.Num(); i++)
			//{
			//		UE_LOG(LogNiagara, Log, TEXT("InstanceCountClearTasks[%d] = %d"), i, InstanceCountClearTasks[i]);
			//}
			{
				// All draw indirect args task are run first because of the binding between the task ID and arg write offset.
				const uint32 ArgGenSize = DrawIndirectArgGenTasks.Num() * sizeof(FArgGenTaskInfo);
				const uint32 InstanceCountClearSize = InstanceCountClearTasks.Num() * sizeof(uint32);
				const uint32 TaskBufferSize = ArgGenSize + InstanceCountClearSize;
				TaskInfosBuffer.Initialize(sizeof(uint32), TaskBufferSize / sizeof(uint32), EPixelFormat::PF_R32_UINT, BUF_Volatile, TEXT("NiagaraTaskInfosBuffer"));
				uint8* TaskBufferData = (uint8*)RHILockVertexBuffer(TaskInfosBuffer.Buffer, 0, TaskBufferSize, RLM_WriteOnly);
				FMemory::Memcpy(TaskBufferData, DrawIndirectArgGenTasks.GetData(), ArgGenSize);
				FMemory::Memcpy(TaskBufferData + ArgGenSize, InstanceCountClearTasks.GetData(), InstanceCountClearSize);
				RHIUnlockVertexBuffer(TaskInfosBuffer.Buffer);
			}

			FRHIShaderResourceView* CulledCountsSRV;

			FRHITransitionInfo TransitionsBefore[3];
			int32 NumTransitionsBefore = 0;
			TransitionsBefore[NumTransitionsBefore] = FRHITransitionInfo(DrawIndirectBuffer.UAV, ERHIAccess::IndirectArgs, ERHIAccess::UAVCompute);
			++NumTransitionsBefore;
			TransitionsBefore[NumTransitionsBefore] = FRHITransitionInfo(CountBuffer.UAV, kCountBufferDefaultState, ERHIAccess::UAVCompute);
			++NumTransitionsBefore;
			if (CulledCountBuffer.SRV.IsValid())
			{
				TransitionsBefore[NumTransitionsBefore] = FRHITransitionInfo(CulledCountBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVCompute);
				++NumTransitionsBefore;
				CulledCountsSRV = CulledCountBuffer.SRV.GetReference();
			}
			else
			{
				CulledCountsSRV = FNiagaraRenderer::GetDummyUIntBuffer();
			}

			RHICmdList.Transition(MakeArrayView(TransitionsBefore, NumTransitionsBefore));

			FNiagaraDrawIndirectArgsGenCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FNiagaraDrawIndirectArgsGenCS::FSupportsTextureRW>(GRHISupportsRWTextureBuffers ? 1 : 0);
			TShaderMapRef<FNiagaraDrawIndirectArgsGenCS> DrawIndirectArgsGenCS(GetGlobalShaderMap(FeatureLevel), PermutationVector);
			RHICmdList.SetComputeShader(DrawIndirectArgsGenCS.GetComputeShader());
			DrawIndirectArgsGenCS->SetOutput(RHICmdList, DrawIndirectBuffer.UAV, CountBuffer.UAV);
			DrawIndirectArgsGenCS->SetParameters(RHICmdList, TaskInfosBuffer.SRV, CulledCountsSRV, DrawIndirectArgGenTasks.Num(), InstanceCountClearTasks.Num());

			// If the device supports RW Texture buffers then we can use a single compute pass, otherwise we need to split into two passes
			if (GRHISupportsRWTextureBuffers)
			{
				DispatchComputeShader(RHICmdList, DrawIndirectArgsGenCS.GetShader(), FMath::DivideAndRoundUp(DrawIndirectArgGenTasks.Num() + InstanceCountClearTasks.Num(), NIAGARA_DRAW_INDIRECT_ARGS_GEN_THREAD_COUNT), 1, 1);
				DrawIndirectArgsGenCS->UnbindBuffers(RHICmdList);
			}
			else
			{
				DispatchComputeShader(RHICmdList, DrawIndirectArgsGenCS.GetShader(), FMath::DivideAndRoundUp(DrawIndirectArgGenTasks.Num(), NIAGARA_DRAW_INDIRECT_ARGS_GEN_THREAD_COUNT), 1, 1);
				DrawIndirectArgsGenCS->UnbindBuffers(RHICmdList);

				FNiagaraDrawIndirectResetCountsCS::FPermutationDomain PermutationVectorResetCounts;
				TShaderMapRef<FNiagaraDrawIndirectResetCountsCS> DrawIndirectResetCountsArgsGenCS(GetGlobalShaderMap(FeatureLevel), PermutationVectorResetCounts);
				RHICmdList.SetComputeShader(DrawIndirectResetCountsArgsGenCS.GetComputeShader());
				DrawIndirectResetCountsArgsGenCS->SetOutput(RHICmdList, CountBuffer.UAV);
				DrawIndirectResetCountsArgsGenCS->SetParameters(RHICmdList, TaskInfosBuffer.SRV, DrawIndirectArgGenTasks.Num(), InstanceCountClearTasks.Num());
				DispatchComputeShader(RHICmdList, DrawIndirectResetCountsArgsGenCS.GetShader(), FMath::DivideAndRoundUp(InstanceCountClearTasks.Num(), NIAGARA_DRAW_INDIRECT_ARGS_GEN_THREAD_COUNT), 1, 1);
				DrawIndirectResetCountsArgsGenCS->UnbindBuffers(RHICmdList);
			}

			FRHITransitionInfo TransitionsAfter[] = {
				FRHITransitionInfo(DrawIndirectBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::IndirectArgs),
				FRHITransitionInfo(CountBuffer.UAV, ERHIAccess::UAVCompute, kCountBufferDefaultState)
			};
			RHICmdList.Transition(MakeArrayView(TransitionsAfter, UE_ARRAY_COUNT(TransitionsAfter)));
		}
		// Once cleared to 0, the count are reusable.
		FreeEntries.Append(InstanceCountClearTasks);

		//for (int32 i = 0; i < FreeEntries.Num(); i++)
		//{
		//	UE_LOG(LogNiagara, Log, TEXT("FreeEntries[%d] = %d"), i, FreeEntries[i]);
		//}

		DrawIndirectArgGenTasks.Empty();
		DrawIndirectArgMap.Empty();
		InstanceCountClearTasks.Empty();

		if (bAcquiredCulledCounts && RequiredCulledCounts > 0)
		{
			CulledCountBuffer.DiscardTransientResource();
		}
		bAcquiredCulledCounts = false;
		RequiredCulledCounts = 0; // reset this counter now that we're done with them
	}
}

const uint32* FNiagaraGPUInstanceCountManager::GetGPUReadback() 
{
	if (CountReadback && CountReadbackSize && CountReadback->IsReady())
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraGPUReadbackLock);
		return (uint32*)(CountReadback->Lock(CountReadbackSize * sizeof(uint32)));
	}
	else
	{
		return nullptr; 
	}
}

void FNiagaraGPUInstanceCountManager::ReleaseGPUReadback() 
{
	check(CountReadback && CountReadbackSize);
	CountReadback->Unlock();
	// Readback can only ever be done once, to prevent misusage with index lifetime
	CountReadbackSize = 0; 
}

void FNiagaraGPUInstanceCountManager::EnqueueGPUReadback(FRHICommandListImmediate& RHICmdList)
{
	if (UsedInstanceCounts > 0 && (UsedInstanceCounts != FreeEntries.Num()))
	{
		if (!CountReadback)
		{
			CountReadback = new FRHIGPUBufferReadback(TEXT("Niagara GPU Instance Count Readback"));
		}
		CountReadbackSize = UsedInstanceCounts;
		// No need for a transition, NiagaraEmitterInstanceBatcher ensures that the buffer is left in the
		// correct state after the sim.
		CountReadback->EnqueueCopy(RHICmdList, CountBuffer.Buffer);
	}
}

bool FNiagaraGPUInstanceCountManager::HasPendingGPUReadback() const
{ 
	return CountReadback && CountReadbackSize;
}

