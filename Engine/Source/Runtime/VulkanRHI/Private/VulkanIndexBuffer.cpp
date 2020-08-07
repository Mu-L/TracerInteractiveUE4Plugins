// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanIndexBuffer.cpp: Vulkan Index buffer RHI implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "VulkanDevice.h"
#include "VulkanContext.h"
#include "Containers/ResourceArray.h"
#include "VulkanLLM.h"


static TMap<FVulkanResourceMultiBuffer*, VulkanRHI::FPendingBufferLock> GPendingLockIBs;
static FCriticalSection GPendingLockIBsMutex;

static FORCEINLINE void UpdateVulkanBufferStats(uint64_t Size, VkBufferUsageFlags Usage, bool Allocating)
{
	const bool bUniformBuffer = !!(Usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
	const bool bIndexBuffer = !!(Usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
	const bool bVertexBuffer = !!(Usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

	if (Allocating)
	{
		if (bUniformBuffer)
		{
			INC_MEMORY_STAT_BY(STAT_UniformBufferMemory, Size);
		}
		else if (bIndexBuffer)
		{
			INC_MEMORY_STAT_BY(STAT_IndexBufferMemory, Size);
		}
		else if (bVertexBuffer)
		{
			INC_MEMORY_STAT_BY(STAT_VertexBufferMemory, Size);
		}
		else
		{
			INC_MEMORY_STAT_BY(STAT_StructuredBufferMemory, Size);
		}
	}
	else
	{
		if (bUniformBuffer)
		{
			DEC_MEMORY_STAT_BY(STAT_UniformBufferMemory, Size);
		}
		else if (bIndexBuffer)
		{
			DEC_MEMORY_STAT_BY(STAT_IndexBufferMemory, Size);
		}
		else if (bVertexBuffer)
		{
			DEC_MEMORY_STAT_BY(STAT_VertexBufferMemory, Size);
		}
		else
		{
			DEC_MEMORY_STAT_BY(STAT_StructuredBufferMemory, Size);
		}
	}
}

FVulkanResourceMultiBuffer::FVulkanResourceMultiBuffer(FVulkanDevice* InDevice, VkBufferUsageFlags InBufferUsageFlags, uint32 InSize, uint32 InUEUsage, FRHIResourceCreateInfo& CreateInfo, class FRHICommandListImmediate* InRHICmdList)
	: VulkanRHI::FDeviceChild(InDevice)
	, UEUsage(InUEUsage)
	, BufferUsageFlags(InBufferUsageFlags)
	, NumBuffers(0)
	, DynamicBufferIndex(0)
{
	VULKAN_TRACK_OBJECT_CREATE(FVulkanResourceMultiBuffer, this);

	if (InSize > 0)
	{
		const bool bStatic = (InUEUsage & BUF_Static) != 0;
		const bool bDynamic = (InUEUsage & BUF_Dynamic) != 0;
		const bool bVolatile = (InUEUsage & BUF_Volatile) != 0;
		const bool bShaderResource = (InUEUsage & BUF_ShaderResource) != 0;
		const bool bIsUniformBuffer = (InBufferUsageFlags & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) != 0;
		const bool bUAV = (InUEUsage & BUF_UnorderedAccess) != 0;
		const bool bIndirect = (InUEUsage & BUF_DrawIndirect) == BUF_DrawIndirect;
		const bool bCPUReadable = (UEUsage & BUF_KeepCPUAccessible) != 0;
		const bool bCopySource = (UEUsage & BUF_SourceCopy) != 0;

		BufferUsageFlags |= bVolatile ? 0 : VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		BufferUsageFlags |= (bShaderResource && !bIsUniformBuffer) ? VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT : 0;
		BufferUsageFlags |= bUAV ? VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT : 0;
		BufferUsageFlags |= bIndirect ? VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT : 0;
		BufferUsageFlags |= bCPUReadable ? (VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT) : 0;
		BufferUsageFlags |= bCopySource ? VK_BUFFER_USAGE_TRANSFER_SRC_BIT : 0;

		if (bVolatile)
		{
			bool bRenderThread = IsInRenderingThread();

			// Get a dummy buffer as sometimes the high-level misbehaves and tries to use SRVs off volatile buffers before filling them in...
			void* Data = Lock(bRenderThread, RLM_WriteOnly, InSize, 0);
			FMemory::Memzero(Data, InSize);
			Unlock(bRenderThread);
		}
		else
		{
			VkDevice VulkanDevice = InDevice->GetInstanceHandle();

			VkMemoryPropertyFlags BufferMemFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			const bool bUnifiedMem = InDevice->HasUnifiedMemory();
			if (bUnifiedMem)
			{
				BufferMemFlags |= (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			}

			NumBuffers = bDynamic ? NUM_BUFFERS : 1;
			check(NumBuffers <= UE_ARRAY_COUNT(Buffers));

			for (uint32 Index = 0; Index < NumBuffers; ++Index)
			{
				Buffers[Index] = InDevice->GetResourceHeapManager().AllocateBuffer(InSize, BufferUsageFlags, BufferMemFlags, __FILE__, __LINE__);
			}

			Current.SubAlloc = Buffers[DynamicBufferIndex];
			Current.BufferAllocation = Current.SubAlloc->GetBufferAllocation();
			Current.Handle = Current.SubAlloc->GetHandle();
			Current.Offset = Current.SubAlloc->GetOffset();
			Current.Size = InSize;

			bool bRenderThread = (InRHICmdList == nullptr);
			if (bRenderThread)
			{
				ensure(IsInRenderingThread());
			}

			if (CreateInfo.ResourceArray)
			{
				uint32 CopyDataSize = FMath::Min(InSize, CreateInfo.ResourceArray->GetResourceDataSize());
				void* Data = Lock(bRenderThread, RLM_WriteOnly, CopyDataSize, 0);
				FMemory::Memcpy(Data, CreateInfo.ResourceArray->GetResourceData(), CopyDataSize);
				Unlock(bRenderThread);

				CreateInfo.ResourceArray->Discard();
			}

			UpdateVulkanBufferStats(InSize * NumBuffers, InBufferUsageFlags, true);
		}
	}
}

FVulkanResourceMultiBuffer::~FVulkanResourceMultiBuffer()
{
	VULKAN_TRACK_OBJECT_DELETE(FVulkanResourceMultiBuffer, this);
	//#todo-rco: Free VkBuffers

	uint64_t Size = 0;
	for (uint32 Index = 0; Index < NumBuffers; ++Index)
	{
		Size += Buffers[Index]->GetSize();
		Device->GetDeferredDeletionQueue().EnqueueBufferSuballocation(Buffers[Index]);
	}
	UpdateVulkanBufferStats(Size, BufferUsageFlags, false);
}

void* FVulkanResourceMultiBuffer::Lock(bool bFromRenderingThread, EResourceLockMode LockMode, uint32 Size, uint32 Offset)
{
	void* Data = nullptr;

	const bool bStatic = (UEUsage & BUF_Static) != 0;
	const bool bDynamic = (UEUsage & BUF_Dynamic) != 0;
	const bool bVolatile = (UEUsage & BUF_Volatile) != 0;
	const bool bCPUReadable = (UEUsage & BUF_KeepCPUAccessible) != 0;
	const bool bUAV = (UEUsage & BUF_UnorderedAccess) != 0;
	const bool bSR = (UEUsage & BUF_ShaderResource) != 0;

	if (bVolatile)
	{
		check(NumBuffers == 0);
		if (LockMode == RLM_ReadOnly)
		{
			checkf(0, TEXT("Volatile buffers can't be locked for read."));
		}
		else
		{
			Device->GetImmediateContext().GetTempFrameAllocationBuffer().Alloc(Size + Offset, 256, VolatileLockInfo);
			Data = VolatileLockInfo.Data;
			++VolatileLockInfo.LockCounter;
			Current.BufferAllocation = VolatileLockInfo.GetBufferAllocation();
			Current.Handle = VolatileLockInfo.GetHandle();
			Current.Offset = VolatileLockInfo.GetBindOffset();
			Current.Size = Size;
		}
	}
	else
	{
		check(bStatic || bDynamic || bUAV || bSR);

		if (LockMode == RLM_ReadOnly)
		{
			const bool bUnifiedMem = Device->HasUnifiedMemory();
			if (bUnifiedMem)
			{
				Data = (uint8*)Buffers[DynamicBufferIndex]->GetMappedPointer() + Offset;
			}
			else 
			{
				Device->PrepareForCPURead();
				FVulkanCommandListContext& ImmediateContext = Device->GetImmediateContext();
				FVulkanCmdBuffer* CmdBuffer = ImmediateContext.GetCommandBufferManager()->GetUploadCmdBuffer();
				
				// Make sure any previous tasks have finished on the source buffer.
				VkBufferMemoryBarrier Barrier;
				VulkanRHI::SetupAndZeroBufferBarrier(Barrier, VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT, Buffers[DynamicBufferIndex]->GetHandle(), Offset, Size);
				VulkanRHI::vkCmdPipelineBarrier(CmdBuffer->GetHandle(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 1, &Barrier, 0, nullptr);

				// Create a staging buffer we can use to copy data from device to cpu.
				VulkanRHI::FStagingBuffer* StagingBuffer = Device->GetStagingManager().AcquireBuffer(Size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

				// Fill the staging buffer with the data on the device.
				VkBufferCopy Regions;
				Regions.size = Size;
				Regions.srcOffset = Offset;
				Regions.dstOffset = 0;

				VulkanRHI::vkCmdCopyBuffer(CmdBuffer->GetHandle(), Buffers[DynamicBufferIndex]->GetHandle(), StagingBuffer->GetHandle(), 1, &Regions);

				// Setup barrier.
				VulkanRHI::SetupAndZeroBufferBarrier(Barrier, VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_HOST_READ_BIT, StagingBuffer->GetHandle(), 0, Size);
				VulkanRHI::vkCmdPipelineBarrier(CmdBuffer->GetHandle(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, 0, 1, &Barrier, 0, nullptr);
				
				// Force upload.
				ImmediateContext.GetCommandBufferManager()->SubmitUploadCmdBuffer();
				Device->WaitUntilIdle();

				// Flush.
				StagingBuffer->FlushMappedMemory();

				// Get mapped pointer. 
				Data = StagingBuffer->GetMappedPointer();

				// Release temp staging buffer during unlock.
				VulkanRHI::FPendingBufferLock PendingLock;
				PendingLock.Offset = 0;
				PendingLock.Size = Size;
				PendingLock.LockMode = LockMode;
				PendingLock.StagingBuffer = StagingBuffer;

				{
					FScopeLock ScopeLock(&GPendingLockIBsMutex);
					check(!GPendingLockIBs.Contains(this));
					GPendingLockIBs.Add(this, PendingLock);
				}

				ImmediateContext.GetCommandBufferManager()->PrepareForNewActiveCommandBuffer();
			}
		}
		else
		{
			check(LockMode == RLM_WriteOnly);
			DynamicBufferIndex = (DynamicBufferIndex + 1) % NumBuffers;
			Current.SubAlloc = Buffers[DynamicBufferIndex];
			Current.BufferAllocation = Current.SubAlloc->GetBufferAllocation();
			Current.Handle = Current.SubAlloc->GetHandle();
			Current.Offset = Current.SubAlloc->GetOffset();
			check(Current.SubAlloc->GetSize() >= Size);
			Current.Size = Size;

			const bool bUnifiedMem = Device->HasUnifiedMemory();
			if (bUnifiedMem)
			{
				Data = (uint8*)Buffers[DynamicBufferIndex]->GetMappedPointer() + Offset;
			}
			else
			{
				VulkanRHI::FPendingBufferLock PendingLock;
				PendingLock.Offset = Offset;
				PendingLock.Size = Size;
				PendingLock.LockMode = LockMode;

				VulkanRHI::FStagingBuffer* StagingBuffer = Device->GetStagingManager().AcquireBuffer(Size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
				PendingLock.StagingBuffer = StagingBuffer;
				Data = StagingBuffer->GetMappedPointer();

				{
					FScopeLock ScopeLock(&GPendingLockIBsMutex);
					check(!GPendingLockIBs.Contains(this));
					GPendingLockIBs.Add(this, PendingLock);
				}
			}

		}
	}

	check(Data);
	return Data;
}

inline void FVulkanResourceMultiBuffer::InternalUnlock(FVulkanCommandListContext& Context, VulkanRHI::FPendingBufferLock& PendingLock, FVulkanResourceMultiBuffer* MultiBuffer, int32 InDynamicBufferIndex)
{
	uint32 LockSize = PendingLock.Size;
	uint32 LockOffset = PendingLock.Offset;
	VulkanRHI::FStagingBuffer* StagingBuffer = PendingLock.StagingBuffer;
	PendingLock.StagingBuffer = nullptr;

	FVulkanCmdBuffer* Cmd = Context.GetCommandBufferManager()->GetUploadCmdBuffer();
	if (!Cmd->HasBegun())
	{
		Cmd->Begin();
	}
	ensure(Cmd->IsOutsideRenderPass());
	VkCommandBuffer CmdBuffer = Cmd->GetHandle();

	VulkanRHI::DebugHeavyWeightBarrier(CmdBuffer, 16);

	VkBufferCopy Region;
	FMemory::Memzero(Region);
	Region.size = LockSize;
	//Region.srcOffset = 0;
	Region.dstOffset = LockOffset + MultiBuffer->Buffers[InDynamicBufferIndex]->GetOffset();
	VulkanRHI::vkCmdCopyBuffer(CmdBuffer, StagingBuffer->GetHandle(), MultiBuffer->Buffers[InDynamicBufferIndex]->GetHandle(), 1, &Region);
	//UpdateBuffer(ResourceAllocation, IndexBuffer->GetBuffer(), LockSize, LockOffset);

	//Device->GetDeferredDeletionQueue().EnqueueResource(Cmd, StagingBuffer);
	MultiBuffer->GetParent()->GetStagingManager().ReleaseBuffer(Cmd, StagingBuffer);
}

struct FRHICommandMultiBufferUnlock final : public FRHICommand<FRHICommandMultiBufferUnlock>
{
	VulkanRHI::FPendingBufferLock PendingLock;
	FVulkanResourceMultiBuffer* MultiBuffer;
	FVulkanDevice* Device;
	int32 DynamicBufferIndex;

	FRHICommandMultiBufferUnlock(FVulkanDevice* InDevice, const VulkanRHI::FPendingBufferLock& InPendingLock, FVulkanResourceMultiBuffer* InMultiBuffer, int32 InDynamicBufferIndex)
		: PendingLock(InPendingLock)
		, MultiBuffer(InMultiBuffer)
		, Device(InDevice)
		, DynamicBufferIndex(InDynamicBufferIndex)
	{
	}

	void Execute(FRHICommandListBase& CmdList)
	{
		FVulkanResourceMultiBuffer::InternalUnlock(VulkanRHI::GetVulkanContext(CmdList.GetContext()), PendingLock, MultiBuffer, DynamicBufferIndex);
	}
};


void FVulkanResourceMultiBuffer::Unlock(bool bFromRenderingThread)
{
	const bool bStatic = (UEUsage & BUF_Static) != 0;
	const bool bDynamic = (UEUsage & BUF_Dynamic) != 0;
	const bool bVolatile = (UEUsage & BUF_Volatile) != 0;
	const bool bCPUReadable = (UEUsage & BUF_KeepCPUAccessible) != 0;
	const bool bSR = (UEUsage & BUF_ShaderResource) != 0;

	if (bVolatile)
	{
		check(NumBuffers == 0);

		// Nothing to do here...
	}
	else
	{
		check(bStatic || bDynamic || bSR);

		const bool bUnifiedMem = Device->HasUnifiedMemory();
		if (bUnifiedMem)
		{
			// Nothing to do here...
			return;
		}

		VulkanRHI::FPendingBufferLock PendingLock;
		bool bFound = false;
		{
			// Found only if it was created for Write
			FScopeLock ScopeLock(&GPendingLockIBsMutex);
			bFound = GPendingLockIBs.RemoveAndCopyValue(this, PendingLock);
		}

		PendingLock.StagingBuffer->FlushMappedMemory();

		checkf(bFound, TEXT("Mismatched lock/unlock IndexBuffer!"));
		if (PendingLock.LockMode == RLM_WriteOnly)
		{
			FRHICommandList& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
			if (!bFromRenderingThread || (RHICmdList.Bypass() || !IsRunningRHIInSeparateThread()))
			{
				FVulkanResourceMultiBuffer::InternalUnlock(Device->GetImmediateContext(), PendingLock, this, DynamicBufferIndex);
			}
			else
			{
				check(IsInRenderingThread());
				ALLOC_COMMAND_CL(RHICmdList, FRHICommandMultiBufferUnlock)(Device, PendingLock, this, DynamicBufferIndex);
			}
		}
		else if(PendingLock.LockMode == RLM_ReadOnly)
		{
			// Just remove the staging buffer here.
			Device->GetStagingManager().ReleaseBuffer(0, PendingLock.StagingBuffer);
		}
	}
}

void FVulkanResourceMultiBuffer::Swap(FVulkanResourceMultiBuffer& Other)
{
	::Swap(*this, Other);
}


FVulkanIndexBuffer::FVulkanIndexBuffer(FVulkanDevice* InDevice, uint32 InStride, uint32 InSize, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo, class FRHICommandListImmediate* InRHICmdList)
	: FRHIIndexBuffer(InStride, InSize, InUsage)
	, FVulkanResourceMultiBuffer(InDevice, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, InSize, InUsage, CreateInfo, InRHICmdList)
	, IndexType(InStride == 4 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16)
{
}

void FVulkanIndexBuffer::Swap(FVulkanIndexBuffer& Other)
{
	FRHIIndexBuffer::Swap(Other);
	FVulkanResourceMultiBuffer::Swap(Other);
	::Swap(IndexType, Other.IndexType);
}


FIndexBufferRHIRef FVulkanDynamicRHI::RHICreateIndexBuffer(uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanIndexBuffers);
	if (CreateInfo.bWithoutNativeResource)
	{
		return new FVulkanIndexBuffer(nullptr, 0, 0, 0, CreateInfo, nullptr);
	}
	return new FVulkanIndexBuffer(Device, Stride, Size, InUsage, CreateInfo, nullptr);
}

void* FVulkanDynamicRHI::LockIndexBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* IndexBufferRHI, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	FVulkanIndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);
	return IndexBuffer->Lock(false, LockMode, Size, Offset);
}

#if VULKAN_BUFFER_LOCK_THREADSAFE
void* FVulkanDynamicRHI::LockIndexBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* IndexBufferRHI, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode)
{
	return this->RHILockIndexBuffer(IndexBufferRHI, Offset, SizeRHI, LockMode);
}
#endif

void FVulkanDynamicRHI::UnlockIndexBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* IndexBufferRHI)
{
	FVulkanIndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);
	IndexBuffer->Unlock(false);
}

#if VULKAN_BUFFER_LOCK_THREADSAFE
void FVulkanDynamicRHI::UnlockIndexBuffer_RenderThread(FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* IndexBufferRHI)
{
	this->RHIUnlockIndexBuffer(IndexBufferRHI);
}
#endif

void FVulkanDynamicRHI::RHITransferIndexBufferUnderlyingResource(FRHIIndexBuffer* DestIndexBuffer, FRHIIndexBuffer* SrcIndexBuffer)
{
	check(DestIndexBuffer);
	FVulkanIndexBuffer* Dest = ResourceCast(DestIndexBuffer);
	if (!SrcIndexBuffer)
	{
		FRHIResourceCreateInfo CreateInfo;
		TRefCountPtr<FVulkanIndexBuffer> DeletionProxy = new FVulkanIndexBuffer(Dest->GetParent(), 0, 0, 0, CreateInfo, nullptr);
		Dest->Swap(*DeletionProxy);
	}
	else
	{
		FVulkanIndexBuffer* Src = ResourceCast(SrcIndexBuffer);
		Dest->Swap(*Src);
	}
}
