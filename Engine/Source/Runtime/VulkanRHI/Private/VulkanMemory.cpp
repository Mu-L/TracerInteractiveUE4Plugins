// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanMemory.cpp: Vulkan memory RHI implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "VulkanMemory.h"
#include "Misc/OutputDeviceRedirector.h"
#include "HAL/PlatformStackWalk.h"


// This 'frame number' should only be used for the deletion queue
uint32 GVulkanRHIDeletionFrameNumber = 0;
const uint32 NUM_FRAMES_TO_WAIT_FOR_RESOURCE_DELETE = 2;


#if VULKAN_MEMORY_TRACK_CALLSTACK
static FCriticalSection GStackTraceMutex;
static char GStackTrace[65536];
static void CaptureCallStack(FString& OutCallstack)
{
	FScopeLock ScopeLock(&GStackTraceMutex);
	GStackTrace[0] = 0;
	FPlatformStackWalk::StackWalkAndDump(GStackTrace, 65535, 3);
	OutCallstack = ANSI_TO_TCHAR(GStackTrace);
}
#endif

namespace VulkanRHI
{
	enum
	{
		GPU_ONLY_HEAP_PAGE_SIZE = 256 * 1024 * 1024,
		STAGING_HEAP_PAGE_SIZE = 32 * 1024 * 1024,
	};

	static FCriticalSection GOldResourcePageLock;
	static FCriticalSection GOldResourceLock;
	static FCriticalSection GStagingLock;
	static FCriticalSection GDeviceMemLock;
	static FCriticalSection GFenceLock;
	static FCriticalSection GResourceHeapLock;

	FDeviceMemoryManager::FDeviceMemoryManager() :
		DeviceHandle(VK_NULL_HANDLE),
		bHasUnifiedMemory(false),
		Device(nullptr),
		NumAllocations(0),
		PeakNumAllocations(0)
	{
		FMemory::Memzero(MemoryProperties);
	}

	FDeviceMemoryManager::~FDeviceMemoryManager()
	{
		Deinit();
	}

	void FDeviceMemoryManager::Init(FVulkanDevice* InDevice)
	{
		check(Device == nullptr);
		Device = InDevice;
		NumAllocations = 0;
		PeakNumAllocations = 0;

		DeviceHandle = Device->GetInstanceHandle();
		VulkanRHI::vkGetPhysicalDeviceMemoryProperties(InDevice->GetPhysicalHandle(), &MemoryProperties);

		HeapInfos.AddDefaulted(MemoryProperties.memoryHeapCount);

		SetupAndPrintMemInfo();
	}

	void FDeviceMemoryManager::SetupAndPrintMemInfo()
	{
		const uint32 MaxAllocations = Device->GetLimits().maxMemoryAllocationCount;
		UE_LOG(LogVulkanRHI, Display, TEXT("%d Device Memory Heaps; Max memory allocations %d"), MemoryProperties.memoryHeapCount, MaxAllocations);
		for (uint32 Index = 0; Index < MemoryProperties.memoryHeapCount; ++Index)
		{
			bool bIsGPUHeap = ((MemoryProperties.memoryHeaps[Index].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == VK_MEMORY_HEAP_DEVICE_LOCAL_BIT);
			UE_LOG(LogVulkanRHI, Display, TEXT("%d: Flags 0x%x Size %llu (%.2f MB) %s"), 
				Index,
				MemoryProperties.memoryHeaps[Index].flags,
				MemoryProperties.memoryHeaps[Index].size,
				(float)((double)MemoryProperties.memoryHeaps[Index].size / 1024.0 / 1024.0),
				bIsGPUHeap ? TEXT("GPU") : TEXT(""));
			HeapInfos[Index].TotalSize = MemoryProperties.memoryHeaps[Index].size;
		}

		bHasUnifiedMemory = FVulkanPlatform::HasUnifiedMemory();
		UE_LOG(LogVulkanRHI, Display, TEXT("%d Device Memory Types (%sunified)"), MemoryProperties.memoryTypeCount, bHasUnifiedMemory ? TEXT("") : TEXT("Not "));
		for (uint32 Index = 0; Index < MemoryProperties.memoryTypeCount; ++Index)
		{
			auto GetFlagsString = [](VkMemoryPropertyFlags Flags)
				{
					FString String;
					if ((Flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
					{
						String += TEXT(" Local");
					}
					if ((Flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
					{
						String += TEXT(" HostVisible");
					}
					if ((Flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
					{
						String += TEXT(" HostCoherent");
					}
					if ((Flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) == VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
					{
						String += TEXT(" HostCached");
					}
					if ((Flags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) == VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)
					{
						String += TEXT(" Lazy");
					}
					return String;
				};
			UE_LOG(LogVulkanRHI, Display, TEXT("%d: Flags 0x%x Heap %d %s"),
				Index,
				MemoryProperties.memoryTypes[Index].propertyFlags,
				MemoryProperties.memoryTypes[Index].heapIndex,
				*GetFlagsString(MemoryProperties.memoryTypes[Index].propertyFlags));
		}

		for (uint32 Index = 0; Index < MemoryProperties.memoryHeapCount; ++Index)
		{
			const bool bIsGPUHeap = ((MemoryProperties.memoryHeaps[Index].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == VK_MEMORY_HEAP_DEVICE_LOCAL_BIT);
			if (bIsGPUHeap)
			{
				// Target using 95% of our budget to account for some fragmentation.
				HeapInfos[Index].TotalSize = (uint64)((float)HeapInfos[Index].TotalSize * 0.95f);
			}
		}
	}

	void FDeviceMemoryManager::Deinit()
	{
		for (int32 Index = 0; Index < HeapInfos.Num(); ++Index)
		{
			if (HeapInfos[Index].Allocations.Num())
			{
				UE_LOG(LogVulkanRHI, Warning, TEXT("Found %d unfreed allocations!"), HeapInfos[Index].Allocations.Num());
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
				DumpMemory();
#endif
			}
		}
		NumAllocations = 0;
	}

	bool FDeviceMemoryManager::SupportsMemoryType(VkMemoryPropertyFlags Properties) const
	{
		for (uint32 Index = 0; Index < MemoryProperties.memoryTypeCount; ++Index)
		{
			if (MemoryProperties.memoryTypes[Index].propertyFlags == Properties)
			{
				return true;
			}
		}

		return false;
	}

	FDeviceMemoryAllocation* FDeviceMemoryManager::Alloc(bool bCanFail, VkDeviceSize AllocationSize, uint32 MemoryTypeIndex, void* DedicatedAllocateInfo, const char* File, uint32 Line)
	{
		FScopeLock Lock(&GDeviceMemLock);

		check(AllocationSize > 0);
		check(MemoryTypeIndex < MemoryProperties.memoryTypeCount);

		VkMemoryAllocateInfo Info;
		ZeroVulkanStruct(Info, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
		Info.allocationSize = AllocationSize;
		Info.memoryTypeIndex = MemoryTypeIndex;

#if VULKAN_SUPPORTS_DEDICATED_ALLOCATION
		Info.pNext = DedicatedAllocateInfo;
#else
		check(!DedicatedAllocateInfo);
#endif

		FDeviceMemoryAllocation* NewAllocation = new FDeviceMemoryAllocation;
		NewAllocation->DeviceHandle = DeviceHandle;
		NewAllocation->Size = AllocationSize;
		NewAllocation->MemoryTypeIndex = MemoryTypeIndex;
		NewAllocation->bCanBeMapped = ((MemoryProperties.memoryTypes[MemoryTypeIndex].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		NewAllocation->bIsCoherent = ((MemoryProperties.memoryTypes[MemoryTypeIndex].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		NewAllocation->bIsCached = ((MemoryProperties.memoryTypes[MemoryTypeIndex].propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) == VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
#if VULKAN_MEMORY_TRACK_FILE_LINE
		NewAllocation->File = File;
		NewAllocation->Line = Line;
		static uint32 ID = 0;
		NewAllocation->UID = ++ID;
#endif
#if VULKAN_MEMORY_TRACK_CALLSTACK
		CaptureCallStack(NewAllocation->Callstack);
#endif
		VkResult Result = VulkanRHI::vkAllocateMemory(DeviceHandle, &Info, nullptr, &NewAllocation->Handle);
		if (Result == VK_ERROR_OUT_OF_DEVICE_MEMORY)
		{
			if (bCanFail)
			{
				UE_LOG(LogVulkanRHI, Warning, TEXT("Failed to allocate Device Memory, Requested=%fKb MemTypeIndex=%d"), (float)Info.allocationSize / 1024.0f, Info.memoryTypeIndex);
				return nullptr;
			}
			UE_LOG(LogVulkanRHI, Error, TEXT("Out of Device Memory, Requested=%fKb MemTypeIndex=%d"), (float)Info.allocationSize / 1024.0f, Info.memoryTypeIndex);
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
			DumpMemory();
			GLog->PanicFlushThreadedLogs();
#endif
		}
		else if (Result == VK_ERROR_OUT_OF_HOST_MEMORY)
		{
			if (bCanFail)
			{
				UE_LOG(LogVulkanRHI, Warning, TEXT("Failed to allocate Host Memory, Requested=%fKb MemTypeIndex=%d"), (float)Info.allocationSize / 1024.0f, Info.memoryTypeIndex);
				return nullptr;
			}
			UE_LOG(LogVulkanRHI, Error, TEXT("Out of Host Memory, Requested=%fKb MemTypeIndex=%d"), (float)Info.allocationSize / 1024.0f, Info.memoryTypeIndex);
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
			DumpMemory();
			GLog->PanicFlushThreadedLogs();
#endif
		}
		else
		{
			VERIFYVULKANRESULT(Result);
		}

		++NumAllocations;
		PeakNumAllocations = FMath::Max(NumAllocations, PeakNumAllocations);
#if !VULKAN_SINGLE_ALLOCATION_PER_RESOURCE
		if (NumAllocations == Device->GetLimits().maxMemoryAllocationCount)
		{
			UE_LOG(LogVulkanRHI, Warning, TEXT("Hit Maximum # of allocations (%d) reported by device!"), NumAllocations);
		}
#endif

		uint32 HeapIndex = MemoryProperties.memoryTypes[MemoryTypeIndex].heapIndex;
		HeapInfos[HeapIndex].Allocations.Add(NewAllocation);
		HeapInfos[HeapIndex].UsedSize += AllocationSize;
		HeapInfos[HeapIndex].PeakSize = FMath::Max(HeapInfos[HeapIndex].PeakSize, HeapInfos[HeapIndex].UsedSize);

		INC_DWORD_STAT(STAT_VulkanNumPhysicalMemAllocations);

		return NewAllocation;
	}

	void FDeviceMemoryManager::Free(FDeviceMemoryAllocation*& Allocation)
	{
		FScopeLock Lock(&GDeviceMemLock);

		check(Allocation);
		check(Allocation->Handle != VK_NULL_HANDLE);
		check(!Allocation->bFreedBySystem);
		VulkanRHI::vkFreeMemory(DeviceHandle, Allocation->Handle, nullptr);

		--NumAllocations;

		DEC_DWORD_STAT(STAT_VulkanNumPhysicalMemAllocations);

		uint32 HeapIndex = MemoryProperties.memoryTypes[Allocation->MemoryTypeIndex].heapIndex;

		HeapInfos[HeapIndex].UsedSize -= Allocation->Size;
		HeapInfos[HeapIndex].Allocations.RemoveSwap(Allocation);
		Allocation->bFreedBySystem = true;
		delete Allocation;
		Allocation = nullptr;
	}

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	void FDeviceMemoryManager::DumpMemory()
	{
		SetupAndPrintMemInfo();
		UE_LOG(LogVulkanRHI, Display, TEXT("Device Memory: %d allocations on %d heaps"), NumAllocations, HeapInfos.Num());
		for (int32 Index = 0; Index < HeapInfos.Num(); ++Index)
		{
			FHeapInfo& HeapInfo = HeapInfos[Index];
			UE_LOG(LogVulkanRHI, Display, TEXT("\tHeap %d, %d allocations"), Index, HeapInfo.Allocations.Num());
			uint64 TotalSize = 0;
			for (int32 SubIndex = 0; SubIndex < HeapInfo.Allocations.Num(); ++SubIndex)
			{
				FDeviceMemoryAllocation* Allocation = HeapInfo.Allocations[SubIndex];
#if VULKAN_MEMORY_TRACK_FILE_LINE
				UE_LOG(LogVulkanRHI, Display, TEXT("\t\t%d Size %d Handle %p ID %d %s(%d)"), SubIndex, Allocation->Size, (void*)Allocation->Handle, Allocation->UID, ANSI_TO_TCHAR(Allocation->File), Allocation->Line);
#else
				UE_LOG(LogVulkanRHI, Display, TEXT("\t\t%d Size %d Handle %p"), SubIndex, Allocation->Size, (void*)Allocation->Handle);
#endif
				TotalSize += Allocation->Size;
			}
			UE_LOG(LogVulkanRHI, Display, TEXT("\t\tTotal Allocated %.2f MB, Peak %.2f MB"), TotalSize / 1024.0f / 1024.0f, HeapInfo.PeakSize / 1024.0f / 1024.0f);
		}
	}
#endif

	uint64 FDeviceMemoryManager::GetTotalMemory(bool bGPU) const
	{
		uint64 TotalMemory = 0;
		for (uint32 Index = 0; Index < MemoryProperties.memoryHeapCount; ++Index)
		{
			const bool bIsGPUHeap = ((MemoryProperties.memoryHeaps[Index].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == VK_MEMORY_HEAP_DEVICE_LOCAL_BIT);

			if (bIsGPUHeap == bGPU)
			{
				TotalMemory += HeapInfos[Index].TotalSize;
			}
		}
		return TotalMemory;
	}

	FDeviceMemoryAllocation::~FDeviceMemoryAllocation()
	{
		checkf(bFreedBySystem, TEXT("Memory has to released calling FDeviceMemory::Free()!"));
	}

	void* FDeviceMemoryAllocation::Map(VkDeviceSize InSize, VkDeviceSize Offset)
	{
		check(bCanBeMapped);
		check(!MappedPointer);
		check(InSize == VK_WHOLE_SIZE || InSize + Offset <= Size);

		VERIFYVULKANRESULT(VulkanRHI::vkMapMemory(DeviceHandle, Handle, Offset, InSize, 0, &MappedPointer));
		return MappedPointer;
	}

	void FDeviceMemoryAllocation::Unmap()
	{
		check(MappedPointer);
		VulkanRHI::vkUnmapMemory(DeviceHandle, Handle);
		MappedPointer = nullptr;
	}

	void FDeviceMemoryAllocation::FlushMappedMemory(VkDeviceSize InOffset, VkDeviceSize InSize)
	{
		if (!IsCoherent())
		{
			check(IsMapped());
			check(InOffset + InSize <= Size);
			VkMappedMemoryRange Range;
			ZeroVulkanStruct(Range, VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE);
			Range.memory = Handle;
			Range.offset = InOffset;
			Range.size = InSize;
			VERIFYVULKANRESULT(VulkanRHI::vkFlushMappedMemoryRanges(DeviceHandle, 1, &Range));
		}
	}

	void FDeviceMemoryAllocation::InvalidateMappedMemory(VkDeviceSize InOffset, VkDeviceSize InSize)
	{
		if (!IsCoherent())
		{
			check(IsMapped());
			check(InOffset + InSize <= Size);
			VkMappedMemoryRange Range;
			ZeroVulkanStruct(Range, VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE);
			Range.memory = Handle;
			Range.offset = InOffset;
			Range.size = InSize;
			VERIFYVULKANRESULT(VulkanRHI::vkInvalidateMappedMemoryRanges(DeviceHandle, 1, &Range));
		}
	}

	void FRange::JoinConsecutiveRanges(TArray<FRange>& Ranges)
	{
		if (Ranges.Num() > 1)
		{
			Ranges.Sort();

			for (int32 Index = Ranges.Num() - 1; Index > 0; --Index)
			{
				FRange& Current = Ranges[Index];
				FRange& Prev = Ranges[Index - 1];
				if (Prev.Offset + Prev.Size == Current.Offset)
				{
					Prev.Size += Current.Size;
					Ranges.RemoveAt(Index, 1, false);
				}
			}
		}
	}


	FOldResourceAllocation::FOldResourceAllocation(FOldResourceHeapPage* InOwner, FDeviceMemoryAllocation* InDeviceMemoryAllocation,
		uint32 InRequestedSize, uint32 InAlignedOffset,
		uint32 InAllocationSize, uint32 InAllocationOffset, const char* InFile, uint32 InLine)
		: Owner(InOwner)
		, AllocationSize(InAllocationSize)
		, AllocationOffset(InAllocationOffset)
		, RequestedSize(InRequestedSize)
		, AlignedOffset(InAlignedOffset)
		, DeviceMemoryAllocation(InDeviceMemoryAllocation)
#if VULKAN_MEMORY_TRACK_FILE_LINE
		, File(InFile)
		, Line(InLine)
#endif
	{
#if VULKAN_MEMORY_TRACK_CALLSTACK
		CaptureCallStack(Callstack);
#endif
		//UE_LOG(LogVulkanRHI, Display, TEXT("*** OldResourceAlloc HeapType %d PageID %d Handle %p Offset %d Size %d @ %s %d"), InOwner->GetOwner()->GetMemoryTypeIndex(), InOwner->GetID(), InDeviceMemoryAllocation->GetHandle(), InAllocationOffset, InAllocationSize, ANSI_TO_TCHAR(InFile), InLine);
	}

	FOldResourceAllocation::~FOldResourceAllocation()
	{
		Owner->ReleaseAllocation(this);
	}

	void FOldResourceAllocation::BindBuffer(FVulkanDevice* Device, VkBuffer Buffer)
	{
		VkResult Result = VulkanRHI::vkBindBufferMemory(Device->GetInstanceHandle(), Buffer, GetHandle(), GetOffset());
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
		if (Result == VK_ERROR_OUT_OF_DEVICE_MEMORY || Result == VK_ERROR_OUT_OF_HOST_MEMORY)
		{
			Device->GetMemoryManager().DumpMemory();
			Device->GetResourceHeapManager().DumpMemory();
		}
#endif
		VERIFYVULKANRESULT(Result);
	}

	void FOldResourceAllocation::BindImage(FVulkanDevice* Device, VkImage Image)
	{
		VkResult Result = VulkanRHI::vkBindImageMemory(Device->GetInstanceHandle(), Image, GetHandle(), GetOffset());
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
		if (Result == VK_ERROR_OUT_OF_DEVICE_MEMORY || Result == VK_ERROR_OUT_OF_HOST_MEMORY)
		{
			Device->GetMemoryManager().DumpMemory();
			Device->GetResourceHeapManager().DumpMemory();
		}
#endif
		VERIFYVULKANRESULT(Result);
	}

	FOldResourceHeapPage::FOldResourceHeapPage(FOldResourceHeap* InOwner, FDeviceMemoryAllocation* InDeviceMemoryAllocation, uint32 InID)
		: Owner(InOwner)
		, DeviceMemoryAllocation(InDeviceMemoryAllocation)
		, MaxSize(0)
		, UsedSize(0)
		, PeakNumAllocations(0)
		, FrameFreed(0)
		, ID(InID)
	{
		MaxSize = InDeviceMemoryAllocation->GetSize();
		FRange FullRange;
		FullRange.Offset = 0;
		FullRange.Size = MaxSize;
		FreeList.Add(FullRange);
	}

	FOldResourceHeapPage::~FOldResourceHeapPage()
	{
		check(!DeviceMemoryAllocation);
	}

	FOldResourceAllocation* FOldResourceHeapPage::TryAllocate(uint32 Size, uint32 Alignment, const char* File, uint32 Line)
	{
		//const uint32 Granularity = Owner->GetOwner()->GetParent()->GetLimits().bufferImageGranularity;
		FScopeLock ScopeLock(&GOldResourcePageLock);
		for (int32 Index = 0; Index < FreeList.Num(); ++Index)
		{
			FRange& Entry = FreeList[Index];
			uint32 AllocatedOffset = Entry.Offset;
			uint32 AlignedOffset = Align(Entry.Offset, Alignment);
			uint32 AlignmentAdjustment = AlignedOffset - Entry.Offset;
			uint32 AllocatedSize = AlignmentAdjustment + Size;
			if (AllocatedSize <= Entry.Size)
			{
				if (AllocatedSize < Entry.Size)
				{
					// Modify current free entry in-place
					Entry.Size -= AllocatedSize;
					Entry.Offset += AllocatedSize;
				}
				else
				{
					// Remove this free entry
					FreeList.RemoveAtSwap(Index, 1, false);
				}

				UsedSize += AllocatedSize;

				FOldResourceAllocation* NewResourceAllocation = new FOldResourceAllocation(this, DeviceMemoryAllocation, Size, AlignedOffset, AllocatedSize, AllocatedOffset, File, Line);
				ResourceAllocations.Add(NewResourceAllocation);

				PeakNumAllocations = FMath::Max(PeakNumAllocations, ResourceAllocations.Num());
				return NewResourceAllocation;
			}
		}

		return nullptr;
	}

	void FOldResourceHeapPage::ReleaseAllocation(FOldResourceAllocation* Allocation)
	{
		{
			FScopeLock ScopeLock(&GOldResourcePageLock);
			ResourceAllocations.RemoveSingleSwap(Allocation, false);

			FRange NewFree;
			NewFree.Offset = Allocation->AllocationOffset;
			NewFree.Size = Allocation->AllocationSize;

			FreeList.Add(NewFree);
		}

		UsedSize -= Allocation->AllocationSize;
		check(UsedSize >= 0);

		if (JoinFreeBlocks())
		{
			Owner->FreePage(this);
		}
	}

	bool FOldResourceHeapPage::JoinFreeBlocks()
	{
		FScopeLock ScopeLock(&GOldResourcePageLock);
		FRange::JoinConsecutiveRanges(FreeList);

		if (FreeList.Num() == 1)
		{
			if (ResourceAllocations.Num() == 0)
			{
				check(UsedSize == 0);
				checkf(FreeList[0].Offset == 0 && FreeList[0].Size == MaxSize, TEXT("Memory leak, should have %d free, only have %d; missing %d bytes"), MaxSize, FreeList[0].Size, MaxSize - FreeList[0].Size);
				return true;
			}
		}

		return false;
	}


	FOldResourceHeap::FOldResourceHeap(FResourceHeapManager* InOwner, uint32 InMemoryTypeIndex, uint32 InPageSize)
		: Owner(InOwner)
		, MemoryTypeIndex(InMemoryTypeIndex)
		, bIsHostCachedSupported(false)
		, bIsLazilyAllocatedSupported(false)
		, DefaultPageSize(InPageSize)
		, PeakPageSize(0)
		, UsedMemory(0)
		, PageIDCounter(0)
	{
	}

	FOldResourceHeap::~FOldResourceHeap()
	{
		ReleaseFreedPages(true);
		auto DeletePages = [&](TArray<FOldResourceHeapPage*>& UsedPages, const TCHAR* Name)
		{
			bool bLeak = false;
			for (int32 Index = UsedPages.Num() - 1; Index >= 0; --Index)
			{
				FOldResourceHeapPage* Page = UsedPages[Index];
				if (!Page->JoinFreeBlocks())
				{
					UE_LOG(LogVulkanRHI, Warning, TEXT("Page allocation %p has unfreed %s resources"), (void*)Page->DeviceMemoryAllocation->GetHandle(), Name);
					bLeak = true;
				}

				Owner->GetParent()->GetMemoryManager().Free(Page->DeviceMemoryAllocation);
				delete Page;
			}

			UsedPages.Reset(0);

			return bLeak;
		};
		bool bDump = false;
		bDump = bDump || DeletePages(UsedBufferPages, TEXT("Buffer"));
		bDump = bDump || DeletePages(UsedImagePages, TEXT("Image"));
		if (bDump)
		{
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
			Owner->GetParent()->GetMemoryManager().DumpMemory();
			Owner->GetParent()->GetResourceHeapManager().DumpMemory();
			GLog->Flush();
#endif
		}

		for (int32 Index = 0; Index < FreePages.Num(); ++Index)
		{
			FOldResourceHeapPage* Page = FreePages[Index];
			Owner->GetParent()->GetMemoryManager().Free(Page->DeviceMemoryAllocation);
			delete Page;
		}
	}

	void FOldResourceHeap::FreePage(FOldResourceHeapPage* InPage)
	{
		FScopeLock ScopeLock(&GOldResourceLock);
		check(InPage->JoinFreeBlocks());
		int32 Index = -1;
		if (UsedBufferPages.Find(InPage, Index))
		{
			UsedBufferPages.RemoveAtSwap(Index, 1, false);
		}
		else if (UsedImagePages.Find(InPage, Index))
		{
			UsedImagePages.RemoveAtSwap(Index, 1, false);
		}
		else
		{
#if VULKAN_SUPPORTS_DEDICATED_ALLOCATION
			int32 Removed = UsedDedicatedImagePages.RemoveSingleSwap(InPage, false);
			check(Removed > 0);
#else
			checkf(0, TEXT("Page not found in Pool!"));
#endif
		}
		InPage->FrameFreed = GFrameNumberRenderThread;
		FreePages.Add(InPage);
	}

	void FOldResourceHeap::ReleaseFreedPages(bool bImmediately)
	{
		FOldResourceHeapPage* PageToRelease = nullptr;

		{
			FScopeLock ScopeLock(&GOldResourceLock);

			// Leave a page not freed to avoid potential hitching
			for (int32 Index = 1; Index < FreePages.Num(); ++Index)
			{
				FOldResourceHeapPage* Page = FreePages[Index];
				if (bImmediately || Page->FrameFreed + NUM_FRAMES_TO_WAIT_BEFORE_RELEASING_TO_OS < GFrameNumberRenderThread)
				{
					PageToRelease = Page;
					FreePages.RemoveAtSwap(Index, 1, false);
					break;
				}
			}
		}

		if (PageToRelease)
		{
			Owner->GetParent()->GetMemoryManager().Free(PageToRelease->DeviceMemoryAllocation);
			UsedMemory -= PageToRelease->MaxSize;
			delete PageToRelease;
		}
	}

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	void FOldResourceHeap::DumpMemory()
	{
		UE_LOG(LogVulkanRHI, Display, TEXT("%d Free Pages"), FreePages.Num());

		auto DumpPages = [&](TArray<FOldResourceHeapPage*>& UsedPages, const TCHAR* TypeName)
		{
			UE_LOG(LogVulkanRHI, Display, TEXT("\t%s Pages: %d Used, Peak Allocation Size on a Page %d"), TypeName, UsedPages.Num(), PeakPageSize);
			uint64 SubAllocUsedMemory = 0;
			uint32 NumSuballocations = 0;
			for (int32 Index = 0; Index < UsedPages.Num(); ++Index)
			{
				SubAllocUsedMemory += UsedPages[Index]->UsedSize;
				NumSuballocations += UsedPages[Index]->ResourceAllocations.Num();

				UE_LOG(LogVulkanRHI, Display, TEXT("\t\t%d: ID %4d %4d suballocs, %4d free chunks (%d used/%d free/%d max) DeviceMemory %p"), Index, UsedPages[Index]->GetID(), UsedPages[Index]->ResourceAllocations.Num(), UsedPages[Index]->FreeList.Num(), UsedPages[Index]->UsedSize, UsedPages[Index]->MaxSize - UsedPages[Index]->UsedSize, UsedPages[Index]->MaxSize, (void*)UsedPages[Index]->DeviceMemoryAllocation->GetHandle());
			}

			UE_LOG(LogVulkanRHI, Display, TEXT("\tUsed Memory %d in %d Suballocations"), SubAllocUsedMemory, NumSuballocations);
		};

		DumpPages(UsedBufferPages, TEXT("Buffer"));
		DumpPages(UsedImagePages, TEXT("Image"));
		//UE_LOG(LogVulkanRHI, Display, TEXT("\tUsed Memory %d in %d Suballocations, Free Memory in pages %d, Heap free memory %ull"), SubAllocUsedMemory, NumSuballocations, FreeMemory, TotalMemory - UsedMemory);
	}
#endif

	FOldResourceAllocation* FOldResourceHeap::AllocateResource(EType Type, uint32 Size, uint32 Alignment, bool bMapAllocation, const char* File, uint32 Line)
	{
		FScopeLock ScopeLock(&GOldResourceLock);

		TArray<FOldResourceHeapPage*>& UsedPages = (Type == EType::Image) ? UsedImagePages : UsedBufferPages;
#if VULKAN_SINGLE_ALLOCATION_PER_RESOURCE
		uint32 AllocationSize = Size;
#else
		if (Size < DefaultPageSize)
		{
			// Check Used pages to see if we can fit this in
			for (int32 Index = 0; Index < UsedPages.Num(); ++Index)
			{
				FOldResourceHeapPage* Page = UsedPages[Index];
				if (Page->DeviceMemoryAllocation->IsMapped() == bMapAllocation)
				{
					FOldResourceAllocation* ResourceAllocation = Page->TryAllocate(Size, Alignment, File, Line);
					if (ResourceAllocation)
					{
						return ResourceAllocation;
					}
				}
			}
		}

		for (int32 Index = 0; Index < FreePages.Num(); ++Index)
		{
			FOldResourceHeapPage* Page = FreePages[Index];
			if (Page->DeviceMemoryAllocation->IsMapped() == bMapAllocation)
			{
				FOldResourceAllocation* ResourceAllocation = Page->TryAllocate(Size, Alignment, File, Line);
				if (ResourceAllocation)
				{
					FreePages.RemoveSingleSwap(Page, false);
					UsedPages.Add(Page);
					return ResourceAllocation;
				}
			}
		}
		uint32 AllocationSize = FMath::Max(Size, DefaultPageSize);
#endif
		FDeviceMemoryAllocation* DeviceMemoryAllocation = Owner->GetParent()->GetMemoryManager().Alloc(true, AllocationSize, MemoryTypeIndex, nullptr, File, Line);
		if (!DeviceMemoryAllocation && Size < AllocationSize)
		{
			// Retry with a smaller size
			DeviceMemoryAllocation = Owner->GetParent()->GetMemoryManager().Alloc(false, AllocationSize, MemoryTypeIndex, nullptr, File, Line);
		}
		++PageIDCounter;
		FOldResourceHeapPage* NewPage = new FOldResourceHeapPage(this, DeviceMemoryAllocation, PageIDCounter);
		UsedPages.Add(NewPage);

		UsedMemory += AllocationSize;

		PeakPageSize = FMath::Max(PeakPageSize, AllocationSize);

		if (bMapAllocation)
		{
			DeviceMemoryAllocation->Map(AllocationSize, 0);
		}

		return NewPage->Allocate(Size, Alignment, File, Line);
	}

#if VULKAN_SUPPORTS_DEDICATED_ALLOCATION
	FOldResourceAllocation* FOldResourceHeap::AllocateDedicatedImage(VkImage Image, uint32 Size, uint32 Alignment, const char* File, uint32 Line)
	{
		FScopeLock ScopeLock(&GOldResourceLock);

		for (int32 Index = 0; Index < FreeDedicatedImagePages.Num(); ++Index)
		{
			FOldResourceHeapPage* Page = FreeDedicatedImagePages[Index];
			FOldResourceAllocation* ResourceAllocation = Page->TryAllocate(Size, Alignment, File, Line);
			if (ResourceAllocation)
			{
				FreeDedicatedImagePages.RemoveSingleSwap(Page, false);
				UsedDedicatedImagePages.Add(Page);
				return ResourceAllocation;
			}
		}
		uint32 AllocationSize = Size;

		check(Image != VK_NULL_HANDLE);
		VkMemoryDedicatedAllocateInfoKHR DedicatedAllocInfo;
		ZeroVulkanStruct(DedicatedAllocInfo, VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR);
		DedicatedAllocInfo.image = Image;
		FDeviceMemoryAllocation* DeviceMemoryAllocation = Owner->GetParent()->GetMemoryManager().Alloc(false, AllocationSize, MemoryTypeIndex, &DedicatedAllocInfo, File, Line);

		++PageIDCounter;
		FOldResourceHeapPage* NewPage = new FOldResourceHeapPage(this, DeviceMemoryAllocation, PageIDCounter);
		UsedDedicatedImagePages.Add(NewPage);

		UsedMemory += AllocationSize;

		PeakPageSize = FMath::Max(PeakPageSize, AllocationSize);

		return NewPage->Allocate(Size, Alignment, File, Line);
	}
#endif

	FResourceHeapManager::FResourceHeapManager(FVulkanDevice* InDevice)
		: FDeviceChild(InDevice)
		, DeviceMemoryManager(&InDevice->GetMemoryManager())
	{
	}

	FResourceHeapManager::~FResourceHeapManager()
	{
		Deinit();
	}

	void FResourceHeapManager::Init()
	{
		FDeviceMemoryManager& MemoryManager = Device->GetMemoryManager();
		const uint32 TypeBits = (1 << MemoryManager.GetNumMemoryTypes()) - 1;

		const VkPhysicalDeviceMemoryProperties& MemoryProperties = MemoryManager.GetMemoryProperties();

		ResourceTypeHeaps.AddZeroed(MemoryProperties.memoryTypeCount);

		auto GetMemoryTypesFromProperties = [MemoryProperties](uint32 InTypeBits, VkMemoryPropertyFlags Properties, TArray<uint32>& OutTypeIndices)
		{
			// Search memtypes to find first index with those properties
			for (uint32 i = 0; i < MemoryProperties.memoryTypeCount && InTypeBits; i++)
			{
				if ((InTypeBits & 1) == 1)
				{
					// Type is available, does it match user properties?
					if ((MemoryProperties.memoryTypes[i].propertyFlags & Properties) == Properties)
					{
						OutTypeIndices.Add(i);
					}
				}
				InTypeBits >>= 1;
			}

			for (int32 Index = OutTypeIndices.Num() - 1; Index >= 1; --Index)
			{
				if (MemoryProperties.memoryTypes[Index].propertyFlags != MemoryProperties.memoryTypes[0].propertyFlags)
				{
					OutTypeIndices.RemoveAtSwap(Index, 1, false);
				}
			}

			// No memory types matched, return failure
			return OutTypeIndices.Num() > 0;
		};

		// Setup main GPU heap
		{
			TArray<uint32> TypeIndices;
			GetMemoryTypesFromProperties(TypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, TypeIndices);
			check(TypeIndices.Num() > 0);

			for (int32 Index = 0; Index < TypeIndices.Num(); ++Index)
			{
				int32 HeapIndex = MemoryProperties.memoryTypes[TypeIndices[Index]].heapIndex;
				VkDeviceSize HeapSize = MemoryProperties.memoryHeaps[HeapIndex].size;
				VkDeviceSize PageSize = FMath::Min<VkDeviceSize>(HeapSize / 8, GPU_ONLY_HEAP_PAGE_SIZE);
				ResourceTypeHeaps[TypeIndices[Index]] = new FOldResourceHeap(this, TypeIndices[Index], PageSize);
				ResourceTypeHeaps[TypeIndices[Index]]->bIsHostCachedSupported = ((MemoryProperties.memoryTypes[Index].propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) == VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
				ResourceTypeHeaps[TypeIndices[Index]]->bIsLazilyAllocatedSupported = ((MemoryProperties.memoryTypes[Index].propertyFlags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) == VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT);
			}
		}

		// Upload heap. Spec requires this combination to exist.
		{
			uint32 TypeIndex = 0;
			VERIFYVULKANRESULT(MemoryManager.GetMemoryTypeFromProperties(TypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &TypeIndex));
			uint64 HeapSize = MemoryProperties.memoryHeaps[MemoryProperties.memoryTypes[TypeIndex].heapIndex].size;
			ResourceTypeHeaps[TypeIndex] = new FOldResourceHeap(this, TypeIndex, STAGING_HEAP_PAGE_SIZE);
		}

		// Download heap. Optional type per the spec.
		{
			uint32 TypeIndex = 0;
			{
				uint32 HostVisCachedIndex = 0;
				VkResult HostCachedResult = MemoryManager.GetMemoryTypeFromProperties(TypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, &HostVisCachedIndex);
				uint32 HostVisIndex = 0;
				VkResult HostResult = MemoryManager.GetMemoryTypeFromProperties(TypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &HostVisIndex);
				if (HostCachedResult == VK_SUCCESS)
				{
					TypeIndex = HostVisCachedIndex;
				}
				else if (HostResult == VK_SUCCESS)
				{
					TypeIndex = HostVisIndex;
				}
				else
				{
					// Redundant as it would have asserted above...
					UE_LOG(LogVulkanRHI, Fatal, TEXT("No Memory Type found supporting VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT!"));
				}
			}
			uint64 HeapSize = MemoryProperties.memoryHeaps[MemoryProperties.memoryTypes[TypeIndex].heapIndex].size;
			ResourceTypeHeaps[TypeIndex] = new FOldResourceHeap(this, TypeIndex, STAGING_HEAP_PAGE_SIZE);
		}
	}

	void FResourceHeapManager::Deinit()
	{
		DestroyResourceAllocations();

		for (int32 Index = 0; Index < ResourceTypeHeaps.Num(); ++Index)
		{
			delete ResourceTypeHeaps[Index];
			ResourceTypeHeaps[Index] = nullptr;
		}
		ResourceTypeHeaps.Empty(0);
	}

	void FResourceHeapManager::DestroyResourceAllocations()
	{
		ReleaseFreedResources(true);

		for (int32 Index = UsedBufferAllocations.Num() - 1; Index >= 0; --Index)
		{
			FBufferAllocation* BufferAllocation = UsedBufferAllocations[Index];
			if (!BufferAllocation->JoinFreeBlocks())
			{
				UE_LOG(LogVulkanRHI, Warning, TEXT("Suballocation(s) for Buffer %p were not released."), (void*)BufferAllocation->Buffer);
			}

			BufferAllocation->Destroy(GetParent());
			GetParent()->GetMemoryManager().Free(BufferAllocation->MemoryAllocation);
			delete BufferAllocation;
		}
		UsedBufferAllocations.Empty(0);

		for (int32 Index = 0; Index < FreeBufferAllocations.Num(); ++Index)
		{
			FBufferAllocation* BufferAllocation = FreeBufferAllocations[Index];
			BufferAllocation->Destroy(GetParent());
			GetParent()->GetMemoryManager().Free(BufferAllocation->MemoryAllocation);
			delete BufferAllocation;
		}
		FreeBufferAllocations.Empty(0);
	}

	void FResourceHeapManager::ReleaseFreedResources(bool bImmediately)
	{
		FBufferAllocation* BufferAllocationToRelease = nullptr;

		{
			FScopeLock ScopeLock(&GResourceHeapLock);
			for (int32 Index = 0; Index < FreeBufferAllocations.Num(); ++Index)
			{
				FBufferAllocation* BufferAllocation = FreeBufferAllocations[Index];
				if (bImmediately || BufferAllocation->FrameFreed + NUM_FRAMES_TO_WAIT_BEFORE_RELEASING_TO_OS < GFrameNumberRenderThread)
				{
					BufferAllocationToRelease = BufferAllocation;
					FreeBufferAllocations.RemoveAtSwap(Index, 1, false);
					break;
				}
			}
		}

		if (BufferAllocationToRelease)
		{
			BufferAllocationToRelease->Destroy(GetParent());
			GetParent()->GetMemoryManager().Free(BufferAllocationToRelease->MemoryAllocation);
			//UsedMemory -= BufferAllocationToRelease->MaxSize;
			delete BufferAllocationToRelease;
		}
	}

	void FResourceHeapManager::ReleaseFreedPages()
	{
		FOldResourceHeap* Heap = ResourceTypeHeaps[GFrameNumberRenderThread % ResourceTypeHeaps.Num()];
		if (Heap)
		{
			Heap->ReleaseFreedPages(false);
		}

		ReleaseFreedResources(false);
	}

	FBufferSuballocation* FResourceHeapManager::AllocateBuffer(uint32 Size, VkBufferUsageFlags BufferUsageFlags, VkMemoryPropertyFlags MemoryPropertyFlags, const char* File, uint32 Line)
	{
		const VkPhysicalDeviceLimits& Limits = Device->GetLimits();
		const bool bIsUniformBuffer = ((BufferUsageFlags & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) != 0);
		uint32 Alignment = (bIsUniformBuffer ? (uint32)Limits.minUniformBufferOffsetAlignment : 1);
		Alignment = FMath::Max(Alignment, ((BufferUsageFlags & (VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT)) != 0) ? (uint32)Limits.minTexelBufferOffsetAlignment : 1u);
		Alignment = FMath::Max(Alignment, ((BufferUsageFlags & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) != 0) ? (uint32)Limits.minStorageBufferOffsetAlignment : 1u);

		FScopeLock ScopeLock(&GResourceHeapLock);

		for (int32 Index = 0; Index < UsedBufferAllocations.Num(); ++Index)
		{
			FBufferAllocation* BufferAllocation = UsedBufferAllocations[Index];
			if ((BufferAllocation->BufferUsageFlags & BufferUsageFlags) == BufferUsageFlags &&
				(BufferAllocation->MemoryPropertyFlags & MemoryPropertyFlags) == MemoryPropertyFlags)
			{
				FBufferSuballocation* Suballocation = (FBufferSuballocation*)BufferAllocation->TryAllocateNoLocking(Size, Alignment, File, Line);
				if (Suballocation)
				{
					return Suballocation;
				}
			}
		}

		for (int32 Index = 0; Index < FreeBufferAllocations.Num(); ++Index)
		{
			FBufferAllocation* BufferAllocation = FreeBufferAllocations[Index];
			if ((BufferAllocation->BufferUsageFlags & BufferUsageFlags) == BufferUsageFlags &&
				(BufferAllocation->MemoryPropertyFlags & MemoryPropertyFlags) == MemoryPropertyFlags)
			{
				FBufferSuballocation* Suballocation = (FBufferSuballocation*)BufferAllocation->TryAllocateNoLocking(Size, Alignment, File, Line);
				if (Suballocation)
				{
					FreeBufferAllocations.RemoveAtSwap(Index, 1, false);
					UsedBufferAllocations.Add(BufferAllocation);
					return Suballocation;
				}
			}
		}

		// New Buffer
		uint32 BufferSize = FMath::Max(Size, (uint32)(bIsUniformBuffer ? UniformBufferAllocationSize : BufferAllocationSize));

		VkBuffer Buffer;
		VkBufferCreateInfo BufferCreateInfo;
		ZeroVulkanStruct(BufferCreateInfo, VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
		BufferCreateInfo.size = BufferSize;
		BufferCreateInfo.usage = BufferUsageFlags;
		VERIFYVULKANRESULT(VulkanRHI::vkCreateBuffer(Device->GetInstanceHandle(), &BufferCreateInfo, nullptr, &Buffer));

		VkMemoryRequirements MemReqs;
		VulkanRHI::vkGetBufferMemoryRequirements(Device->GetInstanceHandle(), Buffer, &MemReqs);
		Alignment = FMath::Max((uint32)MemReqs.alignment, Alignment);
		ensure(MemReqs.size >= BufferSize);

		uint32 MemoryTypeIndex;
		VERIFYVULKANRESULT(Device->GetMemoryManager().GetMemoryTypeFromProperties(MemReqs.memoryTypeBits, MemoryPropertyFlags, &MemoryTypeIndex));

		FDeviceMemoryAllocation* DeviceMemoryAllocation = Device->GetMemoryManager().Alloc(false, MemReqs.size, MemoryTypeIndex, nullptr, File, Line);
		VERIFYVULKANRESULT(VulkanRHI::vkBindBufferMemory(Device->GetInstanceHandle(), Buffer, DeviceMemoryAllocation->GetHandle(), 0));
		if (DeviceMemoryAllocation->CanBeMapped())
		{
			DeviceMemoryAllocation->Map(BufferSize, 0);
		}

		FBufferAllocation* BufferAllocation = new FBufferAllocation(this, DeviceMemoryAllocation, MemoryTypeIndex, MemoryPropertyFlags, MemReqs.alignment, Buffer, BufferUsageFlags);
		UsedBufferAllocations.Add(BufferAllocation);

		return (FBufferSuballocation*)BufferAllocation->TryAllocateNoLocking(Size, Alignment, File, Line);
	}

	void FResourceHeapManager::ReleaseBuffer(FBufferAllocation* BufferAllocation)
	{
		FScopeLock ScopeLock(&GResourceHeapLock);
		check(BufferAllocation->JoinFreeBlocks());
		UsedBufferAllocations.RemoveSingleSwap(BufferAllocation, false);
		BufferAllocation->FrameFreed = GFrameNumberRenderThread;
		FreeBufferAllocations.Add(BufferAllocation);
	}
#if VULKAN_SUPPORTS_DEDICATED_ALLOCATION
	FOldResourceAllocation* FResourceHeapManager::AllocateDedicatedImageMemory(VkImage Image, const VkMemoryRequirements& MemoryReqs, VkMemoryPropertyFlags MemoryPropertyFlags, const char* File, uint32 Line)
	{
		VkImageMemoryRequirementsInfo2KHR ImageMemoryReqs2;
		ZeroVulkanStruct(ImageMemoryReqs2, VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2_KHR);
		ImageMemoryReqs2.image = Image;

		VkMemoryDedicatedRequirementsKHR DedMemoryReqs;
		ZeroVulkanStruct(DedMemoryReqs, VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR);

		VkMemoryRequirements2KHR MemoryReqs2;
		ZeroVulkanStruct(MemoryReqs2, VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR);
		MemoryReqs2.pNext = &DedMemoryReqs;

		VulkanRHI::vkGetImageMemoryRequirements2KHR(Device->GetInstanceHandle(), &ImageMemoryReqs2, &MemoryReqs2);

		bool bUseDedicated = DedMemoryReqs.prefersDedicatedAllocation != VK_FALSE || DedMemoryReqs.requiresDedicatedAllocation != VK_FALSE;
		if (bUseDedicated)
		{
			uint32 TypeIndex = 0;
			VERIFYVULKANRESULT(DeviceMemoryManager->GetMemoryTypeFromProperties(MemoryReqs.memoryTypeBits, MemoryPropertyFlags, &TypeIndex));
			ensure((MemoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
			if (!ResourceTypeHeaps[TypeIndex])
			{
				UE_LOG(LogVulkanRHI, Fatal, TEXT("Missing memory type index %d, MemSize %d, MemPropTypeBits %u, MemPropertyFlags %u, %s(%d)"), TypeIndex, (uint32)MemoryReqs.size, (uint32)MemoryReqs.memoryTypeBits, (uint32)MemoryPropertyFlags, ANSI_TO_TCHAR(File), Line);
			}
			FOldResourceAllocation* Allocation = ResourceTypeHeaps[TypeIndex]->AllocateDedicatedImage(Image, MemoryReqs.size, MemoryReqs.alignment, File, Line);
			if (!Allocation)
			{
				VERIFYVULKANRESULT(DeviceMemoryManager->GetMemoryTypeFromPropertiesExcluding(MemoryReqs.memoryTypeBits, MemoryPropertyFlags, TypeIndex, &TypeIndex));
				ensure((MemoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
				Allocation = ResourceTypeHeaps[TypeIndex]->AllocateDedicatedImage(Image, MemoryReqs.size, MemoryReqs.alignment, File, Line);
			}
			return Allocation;
		}
		else
		{
			return AllocateImageMemory(MemoryReqs, MemoryPropertyFlags, File, Line);
		}
	}
#endif

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	void FResourceHeapManager::DumpMemory()
	{
		FScopeLock ScopeLock(&GResourceHeapLock);

		for (int32 Index = 0; Index < ResourceTypeHeaps.Num(); ++Index)
		{
			if (ResourceTypeHeaps[Index])
			{
				UE_LOG(LogVulkanRHI, Display, TEXT("Heap %d, Memory Type Index %d"), Index, ResourceTypeHeaps[Index]->MemoryTypeIndex);
				ResourceTypeHeaps[Index]->DumpMemory();
			}
			else
			{
				UE_LOG(LogVulkanRHI, Display, TEXT("Heap %d, NOT USED"), Index);
			}
		}

		UE_LOG(LogVulkanRHI, Display, TEXT("Buffer Allocations: %d Used / %d Free"), UsedBufferAllocations.Num(), FreeBufferAllocations.Num());
		if (UsedBufferAllocations.Num() > 0)
		{
			UE_LOG(LogVulkanRHI, Display, TEXT("Index  BufferHandle   DeviceMemoryHandle MemFlags BufferFlags #Suballocs #FreeChunks UsedSize/MaxSize"));
			for (int32 Index = 0; Index < UsedBufferAllocations.Num(); ++Index)
			{
				FBufferAllocation* BA = UsedBufferAllocations[Index];
				UE_LOG(LogVulkanRHI, Display, TEXT("%6d %p %p 0x%06x 0x%08x %6d   %6d    %d/%d"), Index, (void*)BA->Buffer, (void*)BA->MemoryAllocation->GetHandle(), BA->MemoryPropertyFlags, BA->BufferUsageFlags, BA->Suballocations.Num(), BA->FreeList.Num(), BA->UsedSize, BA->MaxSize);
			}
		}
	}
#endif


	FBufferSuballocation::~FBufferSuballocation()
	{
		Owner->Release(this);
	}


	FCriticalSection FSubresourceAllocator::CS;

	bool FSubresourceAllocator::JoinFreeBlocks()
	{
		FScopeLock ScopeLock(&CS);
		FRange::JoinConsecutiveRanges(FreeList);

		if (FreeList.Num() == 1)
		{
			if (Suballocations.Num() == 0)
			{
				check(UsedSize == 0);
				checkf(FreeList[0].Offset == 0 && FreeList[0].Size == MaxSize, TEXT("Resource Suballocation leak, should have %d free, only have %d; missing %d bytes"), MaxSize, FreeList[0].Size, MaxSize - FreeList[0].Size);
				return true;
			}
		}

		return false;
	}
	
	FResourceSuballocation* FSubresourceAllocator::TryAllocateNoLocking(uint32 InSize, uint32 InAlignment, const char* File, uint32 Line)
	{
		InAlignment = FMath::Max(InAlignment, Alignment);
		for (int32 Index = 0; Index < FreeList.Num(); ++Index)
		{
			FRange& Entry = FreeList[Index];
			uint32 AllocatedOffset = Entry.Offset;
			uint32 AlignedOffset = Align(Entry.Offset, InAlignment);
			uint32 AlignmentAdjustment = AlignedOffset - Entry.Offset;
			uint32 AllocatedSize = AlignmentAdjustment + InSize;
			if (AllocatedSize <= Entry.Size)
			{
				if (AllocatedSize < Entry.Size)
				{
					// Modify current free entry in-place
					Entry.Size -= AllocatedSize;
					Entry.Offset += AllocatedSize;
				}
				else
				{
					// Remove this free entry
					FreeList.RemoveAtSwap(Index, 1, false);
				}

				UsedSize += AllocatedSize;

				FResourceSuballocation* NewSuballocation = CreateSubAllocation(InSize, AlignedOffset, AllocatedSize, AllocatedOffset);
#if VULKAN_MEMORY_TRACK_FILE_LINE
				NewSuballocation->File = File;
				NewSuballocation->Line = Line;
#endif
#if VULKAN_MEMORY_TRACK_CALLSTACK
				CaptureCallStack(NewSuballocation->Callstack);
#endif
				Suballocations.Add(NewSuballocation);

				//PeakNumAllocations = FMath::Max(PeakNumAllocations, ResourceAllocations.Num());
				return NewSuballocation;
			}
		}

		return nullptr;
	}

	void FBufferAllocation::Release(FBufferSuballocation* Suballocation)
	{
		{
			FScopeLock ScopeLock(&CS);
			Suballocations.RemoveSingleSwap(Suballocation, false);

			FRange NewFree;
			NewFree.Offset = Suballocation->AllocationOffset;
			NewFree.Size = Suballocation->AllocationSize;

			FreeList.Add(NewFree);
		}

		UsedSize -= Suballocation->AllocationSize;
		check(UsedSize >= 0);

		if (JoinFreeBlocks())
		{
			Owner->ReleaseBuffer(this);
		}
	}

	void FBufferAllocation::Destroy(FVulkanDevice* Device)
	{
		// Does not need to go in the deferred deletion queue
		VulkanRHI::vkDestroyBuffer(Device->GetInstanceHandle(), Buffer, nullptr);
		Buffer = VK_NULL_HANDLE;
	}

	FStagingBuffer::~FStagingBuffer()
	{
		checkf(!ResourceAllocation, TEXT("Staging Buffer not released!"));
	}

	void FStagingBuffer::Destroy(FVulkanDevice* Device)
	{
		check(ResourceAllocation);

		// Does not need to go in the deferred deletion queue
		VulkanRHI::vkDestroyBuffer(Device->GetInstanceHandle(), Buffer, nullptr);
		Buffer = VK_NULL_HANDLE;
		ResourceAllocation = nullptr;
		//Memory.Free(Allocation);
	}

	FStagingManager::~FStagingManager()
	{
		check(UsedStagingBuffers.Num() == 0);
		check(PendingFreeStagingBuffers.Num() == 0);
		check(FreeStagingBuffers.Num() == 0);
	}

	void FStagingManager::Deinit()
	{
		ProcessPendingFree(true, true);

		check(UsedStagingBuffers.Num() == 0);
		check(PendingFreeStagingBuffers.Num() == 0);
		check(FreeStagingBuffers.Num() == 0);
	}

	FStagingBuffer* FStagingManager::AcquireBuffer(uint32 Size, VkBufferUsageFlags InUsageFlags, bool bCPURead)
	{
#if VULKAN_ENABLE_AGGRESSIVE_STATS
		SCOPE_CYCLE_COUNTER(STAT_VulkanStagingBuffer);
#endif

		//#todo-rco: Better locking!
		{
			FScopeLock Lock(&GStagingLock);
			for (int32 Index = 0; Index < FreeStagingBuffers.Num(); ++Index)
			{
				FFreeEntry& FreeBuffer = FreeStagingBuffers[Index];
				if (FreeBuffer.Buffer->GetSize() == Size && FreeBuffer.Buffer->bCPURead == bCPURead)
				{
					FStagingBuffer* Buffer = FreeBuffer.Buffer;
					FreeStagingBuffers.RemoveAtSwap(Index, 1, false);
					UsedStagingBuffers.Add(Buffer);
					return Buffer;
				}
			}
		}

		FStagingBuffer* StagingBuffer = new FStagingBuffer();

		VkBufferCreateInfo StagingBufferCreateInfo;
		ZeroVulkanStruct(StagingBufferCreateInfo, VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
		StagingBufferCreateInfo.size = Size;
		StagingBufferCreateInfo.usage = InUsageFlags;

		VkDevice VulkanDevice = Device->GetInstanceHandle();

		VERIFYVULKANRESULT(VulkanRHI::vkCreateBuffer(VulkanDevice, &StagingBufferCreateInfo, nullptr, &StagingBuffer->Buffer));

		VkMemoryRequirements MemReqs;
		VulkanRHI::vkGetBufferMemoryRequirements(VulkanDevice, StagingBuffer->Buffer, &MemReqs);
		ensure(MemReqs.size >= Size);

		// Set minimum alignment to 16 bytes, as some buffers are used with CPU SIMD instructions
		MemReqs.alignment = FMath::Max<VkDeviceSize>(16, MemReqs.alignment);

		StagingBuffer->ResourceAllocation = Device->GetResourceHeapManager().AllocateBufferMemory(MemReqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | (bCPURead ? VK_MEMORY_PROPERTY_HOST_CACHED_BIT : VK_MEMORY_PROPERTY_HOST_COHERENT_BIT), __FILE__, __LINE__);
		StagingBuffer->bCPURead = bCPURead;
		StagingBuffer->BufferSize = Size;
		StagingBuffer->ResourceAllocation->BindBuffer(Device, StagingBuffer->Buffer);

		{
			FScopeLock Lock(&GStagingLock);
			UsedStagingBuffers.Add(StagingBuffer);
			UsedMemory += StagingBuffer->GetSize();
			PeakUsedMemory = FMath::Max(UsedMemory, PeakUsedMemory);
		}
		return StagingBuffer;
	}

	inline FStagingManager::FPendingItemsPerCmdBuffer* FStagingManager::FindOrAdd(FVulkanCmdBuffer* CmdBuffer)
	{
		for (int32 Index = 0; Index < PendingFreeStagingBuffers.Num(); ++Index)
		{
			if (PendingFreeStagingBuffers[Index].CmdBuffer == CmdBuffer)
			{
				return &PendingFreeStagingBuffers[Index];
			}
		}

		FPendingItemsPerCmdBuffer* New = new(PendingFreeStagingBuffers) FPendingItemsPerCmdBuffer;
		New->CmdBuffer = CmdBuffer;
		return New;
	}

	inline FStagingManager::FPendingItemsPerCmdBuffer::FPendingItems* FStagingManager::FPendingItemsPerCmdBuffer::FindOrAddItemsForFence(uint64 Fence)
	{
		for (int32 Index = 0; Index < PendingItems.Num(); ++Index)
		{
			if (PendingItems[Index].FenceCounter == Fence)
			{
				return &PendingItems[Index];
			}
		}

		FPendingItems* New = new(PendingItems) FPendingItems;
		New->FenceCounter = Fence;
		return New;
	}

	void FStagingManager::ReleaseBuffer(FVulkanCmdBuffer* CmdBuffer, FStagingBuffer*& StagingBuffer)
	{
#if VULKAN_ENABLE_AGGRESSIVE_STATS
		SCOPE_CYCLE_COUNTER(STAT_VulkanStagingBuffer);
#endif

		FScopeLock Lock(&GStagingLock);
		UsedStagingBuffers.RemoveSingleSwap(StagingBuffer, false);

		if (CmdBuffer)
		{
			FPendingItemsPerCmdBuffer* ItemsForCmdBuffer = FindOrAdd(CmdBuffer);
			FPendingItemsPerCmdBuffer::FPendingItems* ItemsForFence = ItemsForCmdBuffer->FindOrAddItemsForFence(CmdBuffer ? CmdBuffer->GetFenceSignaledCounter() : 0);
			ItemsForFence->Resources.Add(StagingBuffer);
		}
		else
		{
			FreeStagingBuffers.Add({StagingBuffer, GFrameNumberRenderThread});
		}
		StagingBuffer = nullptr;
	}

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	void FStagingManager::DumpMemory()
	{
		UE_LOG(LogVulkanRHI, Display, TEXT("StagingManager %d Used %d Pending Free %d Free"), UsedStagingBuffers.Num(), PendingFreeStagingBuffers.Num(), FreeStagingBuffers.Num());
		UE_LOG(LogVulkanRHI, Display, TEXT("Used   BufferHandle ResourceAllocation"));
		for (int32 Index = 0; Index < UsedStagingBuffers.Num(); ++Index)
		{
			FStagingBuffer* Buffer = UsedStagingBuffers[Index];
			UE_LOG(LogVulkanRHI, Display, TEXT("%6d %p %p"), Index, (void*)Buffer->GetHandle(), (void*)Buffer->ResourceAllocation->GetHandle());
		}

		UE_LOG(LogVulkanRHI, Display, TEXT("Pending CmdBuffer   Fence   BufferHandle ResourceAllocation"));
		for (int32 Index = 0; Index < PendingFreeStagingBuffers.Num(); ++Index)
		{
			FPendingItemsPerCmdBuffer& ItemPerCmdBuffer = PendingFreeStagingBuffers[Index];
			UE_LOG(LogVulkanRHI, Display, TEXT("%6d %p"), Index, (void*)ItemPerCmdBuffer.CmdBuffer->GetHandle());
			for (int32 FenceIndex = 0; FenceIndex < ItemPerCmdBuffer.PendingItems.Num(); ++FenceIndex)
			{
				FPendingItemsPerCmdBuffer::FPendingItems& ItemsPerFence = ItemPerCmdBuffer.PendingItems[FenceIndex];
				UE_LOG(LogVulkanRHI, Display, TEXT("         Fence %p"), (void*)ItemsPerFence.FenceCounter);
				for (int32 BufferIndex = 0; BufferIndex < ItemsPerFence.Resources.Num(); ++BufferIndex)
				{
					FStagingBuffer* Buffer = ItemsPerFence.Resources[BufferIndex];
					UE_LOG(LogVulkanRHI, Display, TEXT("                   %p %p"), (void*)Buffer->GetHandle(), (void*)Buffer->ResourceAllocation->GetHandle());
				}
			}
		}

		UE_LOG(LogVulkanRHI, Display, TEXT("Free   BufferHandle ResourceAllocation"));
		for (int32 Index = 0; Index < FreeStagingBuffers.Num(); ++Index)
		{
			FFreeEntry& Entry = FreeStagingBuffers[Index];
			UE_LOG(LogVulkanRHI, Display, TEXT("%6d %p %p"), Index, (void*)Entry.Buffer->GetHandle(), (void*)Entry.Buffer->ResourceAllocation->GetHandle());
		}
	}
#endif

	void FStagingManager::ProcessPendingFreeNoLock(bool bImmediately, bool bFreeToOS)
	{
		int32 NumOriginalFreeBuffers = FreeStagingBuffers.Num();
		for (int32 Index = PendingFreeStagingBuffers.Num() - 1; Index >= 0; --Index)
		{
			FPendingItemsPerCmdBuffer& EntriesPerCmdBuffer = PendingFreeStagingBuffers[Index];
			for (int32 FenceIndex = EntriesPerCmdBuffer.PendingItems.Num() - 1; FenceIndex >= 0; --FenceIndex)
			{
				FPendingItemsPerCmdBuffer::FPendingItems& PendingItems = EntriesPerCmdBuffer.PendingItems[FenceIndex];
				if (bImmediately || PendingItems.FenceCounter < EntriesPerCmdBuffer.CmdBuffer->GetFenceSignaledCounter())
				{
					for (int32 ResourceIndex = 0; ResourceIndex < PendingItems.Resources.Num(); ++ResourceIndex)
					{
						FreeStagingBuffers.Add({PendingItems.Resources[ResourceIndex], GFrameNumberRenderThread});
					}

					EntriesPerCmdBuffer.PendingItems.RemoveAtSwap(FenceIndex, 1, false);
				}
			}

			if (EntriesPerCmdBuffer.PendingItems.Num() == 0)
			{
				PendingFreeStagingBuffers.RemoveAtSwap(Index, 1, false);
			}
		}

		if (bFreeToOS)
		{
			int32 NumFreeBuffers = bImmediately ? FreeStagingBuffers.Num() : NumOriginalFreeBuffers;
			for (int32 Index = NumFreeBuffers - 1; Index >= 0; --Index)
			{
				FFreeEntry& Entry = FreeStagingBuffers[Index];
				if (bImmediately || Entry.FrameNumber + NUM_FRAMES_TO_WAIT_BEFORE_RELEASING_TO_OS < GFrameNumberRenderThread)
				{
					UsedMemory -= Entry.Buffer->GetSize();
					Entry.Buffer->Destroy(Device);
					delete Entry.Buffer;
					FreeStagingBuffers.RemoveAtSwap(Index, 1, false);
				}
			}
		}
	}

	void FStagingManager::ProcessPendingFree(bool bImmediately, bool bFreeToOS)
	{
#if VULKAN_ENABLE_AGGRESSIVE_STATS
		SCOPE_CYCLE_COUNTER(STAT_VulkanStagingBuffer);
#endif

		FScopeLock Lock(&GStagingLock);
		ProcessPendingFreeNoLock(bImmediately, bFreeToOS);
	}

	FFence::FFence(FVulkanDevice* InDevice, FFenceManager* InOwner, bool bCreateSignaled)
		: State(bCreateSignaled ? FFence::EState::Signaled : FFence::EState::NotReady)
		, Owner(InOwner)
	{
		VkFenceCreateInfo Info;
		ZeroVulkanStruct(Info, VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
		Info.flags = bCreateSignaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;
		VERIFYVULKANRESULT(VulkanRHI::vkCreateFence(InDevice->GetInstanceHandle(), &Info, nullptr, &Handle));
	}

	FFence::~FFence()
	{
		checkf(Handle == VK_NULL_HANDLE, TEXT("Didn't get properly destroyed by FFenceManager!"));
	}

	FFenceManager::~FFenceManager()
	{
		ensure(UsedFences.Num() == 0);
	}

	inline void FFenceManager::DestroyFence(FFence* Fence)
	{
		// Does not need to go in the deferred deletion queue
		VulkanRHI::vkDestroyFence(Device->GetInstanceHandle(), Fence->GetHandle(), nullptr);
		Fence->Handle = VK_NULL_HANDLE;
		delete Fence;
	}

	void FFenceManager::Init(FVulkanDevice* InDevice)
	{
		Device = InDevice;
	}

	void FFenceManager::Deinit()
	{
		FScopeLock Lock(&GFenceLock);
		ensureMsgf(UsedFences.Num() == 0, TEXT("No all fences are done!"));
		VkDevice DeviceHandle = Device->GetInstanceHandle();
		for (FFence* Fence : FreeFences)
		{
			DestroyFence(Fence);
		}
	}

	FFence* FFenceManager::AllocateFence(bool bCreateSignaled)
	{
		FScopeLock Lock(&GFenceLock);
		if (FreeFences.Num() != 0)
		{
			FFence* Fence = FreeFences[0];
			FreeFences.RemoveAtSwap(0, 1, false);
			UsedFences.Add(Fence);

			if (bCreateSignaled)
			{
				Fence->State = FFence::EState::Signaled;
			}
			return Fence;
		}

		FFence* NewFence = new FFence(Device, this, bCreateSignaled);
		UsedFences.Add(NewFence);
		return NewFence;
	}

	// Sets it to nullptr
	void FFenceManager::ReleaseFence(FFence*& Fence)
	{
		FScopeLock Lock(&GFenceLock);
		ResetFence(Fence);
		UsedFences.RemoveSingleSwap(Fence, false);
#if VULKAN_REUSE_FENCES
		FreeFences.Add(Fence);
#else
		DestroyFence(Fence);
#endif
		Fence = nullptr;
	}

	void FFenceManager::WaitAndReleaseFence(FFence*& Fence, uint64 TimeInNanoseconds)
	{
		FScopeLock Lock(&GFenceLock);
		if (!Fence->IsSignaled())
		{
			WaitForFence(Fence, TimeInNanoseconds);
		}

		ResetFence(Fence);
		UsedFences.RemoveSingleSwap(Fence, false);
		FreeFences.Add(Fence);
		Fence = nullptr;
	}

	bool FFenceManager::CheckFenceState(FFence* Fence)
	{
		check(UsedFences.Contains(Fence));
		check(Fence->State == FFence::EState::NotReady);
		VkResult Result = VulkanRHI::vkGetFenceStatus(Device->GetInstanceHandle(), Fence->Handle);
		switch (Result)
		{
		case VK_SUCCESS:
			Fence->State = FFence::EState::Signaled;
			return true;

		case VK_NOT_READY:
			break;

		default:
			VERIFYVULKANRESULT(Result);
			break;
		}

		return false;
	}

	bool FFenceManager::WaitForFence(FFence* Fence, uint64 TimeInNanoseconds)
	{
#if VULKAN_ENABLE_AGGRESSIVE_STATS
		SCOPE_CYCLE_COUNTER(STAT_VulkanWaitFence);
#endif

		check(UsedFences.Contains(Fence));
		check(Fence->State == FFence::EState::NotReady);
		VkResult Result = VulkanRHI::vkWaitForFences(Device->GetInstanceHandle(), 1, &Fence->Handle, true, TimeInNanoseconds);
		switch (Result)
		{
		case VK_SUCCESS:
			Fence->State = FFence::EState::Signaled;
			return true;
		case VK_TIMEOUT:
			break;
		default:
			VERIFYVULKANRESULT(Result);
			break;
		}

		return false;
	}

	void FFenceManager::ResetFence(FFence* Fence)
	{
		if (Fence->State != FFence::EState::NotReady)
		{
			VERIFYVULKANRESULT(VulkanRHI::vkResetFences(Device->GetInstanceHandle(), 1, &Fence->Handle));
			Fence->State = FFence::EState::NotReady;
		}
	}


	FGPUEvent::FGPUEvent(FVulkanDevice* InDevice)
		: FDeviceChild(InDevice)
	{
		VkEventCreateInfo Info;
		ZeroVulkanStruct(Info, VK_STRUCTURE_TYPE_EVENT_CREATE_INFO);
		VERIFYVULKANRESULT(VulkanRHI::vkCreateEvent(InDevice->GetInstanceHandle(), &Info, nullptr, &Handle));
	}

	FGPUEvent::~FGPUEvent()
	{
		Device->GetDeferredDeletionQueue().EnqueueResource(VulkanRHI::FDeferredDeletionQueue::EType::Event, Handle);
	}


	FDeferredDeletionQueue::FDeferredDeletionQueue(FVulkanDevice* InDevice)
		: FDeviceChild(InDevice)
	{
	}

	FDeferredDeletionQueue::~FDeferredDeletionQueue()
	{
		check(Entries.Num() == 0);
	}

	void FDeferredDeletionQueue::EnqueueGenericResource(EType Type, uint64 Handle)
	{
		FVulkanQueue* Queue = Device->GetGraphicsQueue();

		FEntry Entry;
		Queue->GetLastSubmittedInfo(Entry.CmdBuffer, Entry.FenceCounter);
		Entry.Handle = Handle;
		Entry.StructureType = Type;
		Entry.FrameNumber = GVulkanRHIDeletionFrameNumber;

		{
			FScopeLock ScopeLock(&CS);

#if VULKAN_HAS_DEBUGGING_ENABLED
			FEntry* ExistingEntry = Entries.FindByPredicate([&](const FEntry& InEntry)
				{ 
					return InEntry.Handle == Entry.Handle; 
				});
			checkf(ExistingEntry == nullptr, TEXT("Attempt to double-delete resource, Type: %d, Handle: %llu"), (int32)Type, Handle);
#endif

			Entries.Add(Entry);
		}
	}

	void FDeferredDeletionQueue::ReleaseResources(bool bDeleteImmediately)
	{
#if VULKAN_ENABLE_AGGRESSIVE_STATS
		SCOPE_CYCLE_COUNTER(STAT_VulkanDeletionQueue);
#endif
		FScopeLock ScopeLock(&CS);

		VkDevice DeviceHandle = Device->GetInstanceHandle();

		// Traverse list backwards so the swap switches to elements already tested
		for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
		{
			FEntry* Entry = &Entries[Index];
			// #todo-rco: Had to add this check, we were getting null CmdBuffers on the first frame, or before first frame maybe
			if (bDeleteImmediately ||
				(GVulkanRHIDeletionFrameNumber > Entry->FrameNumber + NUM_FRAMES_TO_WAIT_FOR_RESOURCE_DELETE &&
				(Entry->CmdBuffer == nullptr || Entry->FenceCounter < Entry->CmdBuffer->GetFenceSignaledCounter()))
				)
			{
				switch (Entry->StructureType)
				{
#define VKSWITCH(Type)	case EType::Type: VulkanRHI::vkDestroy##Type(DeviceHandle, (Vk##Type)Entry->Handle, nullptr); break
				VKSWITCH(RenderPass);
				VKSWITCH(Buffer);
				VKSWITCH(BufferView);
				VKSWITCH(Image);
				VKSWITCH(ImageView);
				VKSWITCH(Pipeline);
				VKSWITCH(PipelineLayout);
				VKSWITCH(Framebuffer);
				VKSWITCH(DescriptorSetLayout);
				VKSWITCH(Sampler);
				VKSWITCH(Semaphore);
				VKSWITCH(ShaderModule);
				VKSWITCH(Event);
#undef VKSWITCH
				default:
					check(0);
					break;
				}
				Entries.RemoveAtSwap(Index, 1, false);
			}
		}
	}

	FTempFrameAllocationBuffer::FTempFrameAllocationBuffer(FVulkanDevice* InDevice)
		: FDeviceChild(InDevice)
		, BufferIndex(0)
	{
		for (int32 Index = 0; Index < NUM_RENDER_BUFFERS; ++Index)
		{
			Entries[Index].InitBuffer(Device, ALLOCATION_SIZE);
		}
	}

	FTempFrameAllocationBuffer::~FTempFrameAllocationBuffer()
	{
		Destroy();
	}

	void FTempFrameAllocationBuffer::FFrameEntry::InitBuffer(FVulkanDevice* InDevice, uint32 InSize)
	{
		Size = InSize;
		PeakUsed = 0;
		BufferSuballocation = InDevice->GetResourceHeapManager().AllocateBuffer(InSize,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			__FILE__, __LINE__);
		MappedData = (uint8*)BufferSuballocation->GetMappedPointer();
		CurrentData = MappedData;
	}

	void FTempFrameAllocationBuffer::Destroy()
	{
		for (int32 Index = 0; Index < NUM_RENDER_BUFFERS; ++Index)
		{
			Entries[Index].BufferSuballocation = nullptr;
		}
	}

	bool FTempFrameAllocationBuffer::FFrameEntry::TryAlloc(uint32 InSize, uint32 InAlignment, FTempAllocInfo& OutInfo)
	{
		uint8* AlignedData = (uint8*)Align((uintptr_t)CurrentData, (uintptr_t)InAlignment);
		if (AlignedData + InSize <= MappedData + Size)
		{
			OutInfo.Data = AlignedData;
			OutInfo.BufferSuballocation = BufferSuballocation;
			OutInfo.CurrentOffset = (uint32)(AlignedData - MappedData);
			CurrentData = AlignedData + InSize;
			PeakUsed = FMath::Max(PeakUsed, (uint32)(CurrentData - MappedData));
			return true;
		}

		return false;
	}

	void FTempFrameAllocationBuffer::Alloc(uint32 InSize, uint32 InAlignment, FTempAllocInfo& OutInfo)
	{
		FScopeLock ScopeLock(&CS);

		if (Entries[BufferIndex].TryAlloc(InSize, InAlignment, OutInfo))
		{
			return;
		}
		
		// Couldn't fit in the current buffers; allocate a new bigger one and schedule the current one for deletion
		uint32 NewSize = Align(ALLOCATION_SIZE + InSize + InAlignment, ALLOCATION_SIZE);
		Entries[BufferIndex].PendingDeletionList.Add(Entries[BufferIndex].BufferSuballocation);
		Entries[BufferIndex].InitBuffer(Device, NewSize);
		if (!Entries[BufferIndex].TryAlloc(InSize, InAlignment, OutInfo))
		{
			checkf(0, TEXT("Internal Error trying to allocate %d Align %d on TempFrameBuffer, size %d"), InSize, InAlignment, NewSize);
		}
	}

	void FTempFrameAllocationBuffer::Reset()
	{
		FScopeLock ScopeLock(&CS);
		BufferIndex = (BufferIndex + 1) % NUM_RENDER_BUFFERS;
		Entries[BufferIndex].Reset();
	}

	void FTempFrameAllocationBuffer::FFrameEntry::Reset()
	{
		CurrentData = MappedData;
		while (PendingDeletionList.Num() > 0)
		{
			PendingDeletionList.Pop(false);
		}
	}

	void ImagePipelineBarrier(VkCommandBuffer CmdBuffer, VkImage Image,
		EImageLayoutBarrier Source, EImageLayoutBarrier Dest, const VkImageSubresourceRange& SubresourceRange)
	{
		VkImageMemoryBarrier ImageBarrier;
		ZeroVulkanStruct(ImageBarrier, VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
		ImageBarrier.image = Image;
		ImageBarrier.subresourceRange = SubresourceRange;
		ImageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		ImageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

		VkPipelineStageFlags SourceStages = (VkPipelineStageFlags)0;
		VkPipelineStageFlags DestStages = (VkPipelineStageFlags)0;
		SetImageBarrierInfo(Source, Dest, ImageBarrier, SourceStages, DestStages);

		if (!DelayAcquireBackBuffer())
		{
			// special handling for VK_IMAGE_LAYOUT_PRESENT_SRC_KHR (otherwise Mali devices flicker)
			if (Source == EImageLayoutBarrier::Present)
			{
				SourceStages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
				DestStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			}
			else if (Dest == EImageLayoutBarrier::Present)
			{
				SourceStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				DestStages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			}
		}

		VulkanRHI::vkCmdPipelineBarrier(CmdBuffer, SourceStages, DestStages, 0, 0, nullptr, 0, nullptr, 1, &ImageBarrier);
	}

	void FPendingBarrier::InnerExecute(FVulkanCmdBuffer* CmdBuffer, bool bEnsure)
	{
		if (bEnsure)
		{
			ensure(CmdBuffer->IsOutsideRenderPass());
		}
		VulkanRHI::vkCmdPipelineBarrier(CmdBuffer->GetHandle(),
			SourceStage, DestStage, 0,
			0, nullptr,
			BufferBarriers.Num(), BufferBarriers.GetData(),
			ImageBarriers.Num(), ImageBarriers.GetData());
	}


	FSemaphore::FSemaphore(FVulkanDevice& InDevice) :
		Device(InDevice),
		SemaphoreHandle(VK_NULL_HANDLE)
	{
		// Create semaphore
		VkSemaphoreCreateInfo CreateInfo;
		ZeroVulkanStruct(CreateInfo, VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
		//CreateInfo.pNext = nullptr;
		//CreateInfo.flags = 0;
		VERIFYVULKANRESULT(VulkanRHI::vkCreateSemaphore(Device.GetInstanceHandle(), &CreateInfo, nullptr, &SemaphoreHandle));
	}

	FSemaphore::~FSemaphore()
	{
		check(SemaphoreHandle != VK_NULL_HANDLE);
		Device.GetDeferredDeletionQueue().EnqueueResource(VulkanRHI::FDeferredDeletionQueue::EType::Semaphore, SemaphoreHandle);
		SemaphoreHandle = VK_NULL_HANDLE;
	}
}


#if VULKAN_CUSTOM_MEMORY_MANAGER_ENABLED
static FCriticalSection GMemMgrCS;
static FVulkanCustomMemManager GVulkanInstrumentedMemMgr;
VkAllocationCallbacks GAllocationCallbacks;
//VkAllocationCallbacks GDescriptorAllocationCallbacks;


FVulkanCustomMemManager::FVulkanCustomMemManager()
{
	GAllocationCallbacks.pUserData = nullptr/*(void*)FVulkanCustomMemManager::Regular*/;
	GAllocationCallbacks.pfnAllocation = &FVulkanCustomMemManager::Alloc;
	GAllocationCallbacks.pfnReallocation = &FVulkanCustomMemManager::Realloc;
	GAllocationCallbacks.pfnFree = &FVulkanCustomMemManager::Free;
	GAllocationCallbacks.pfnInternalAllocation = &FVulkanCustomMemManager::InternalAllocationNotification;
	GAllocationCallbacks.pfnInternalFree = &FVulkanCustomMemManager::InternalFreeNotification;

	//GDescriptorAllocationCallbacks.pUserData = (void*)FVulkanCustomMemManager::Descriptors;
	//GDescriptorAllocationCallbacks.pfnAllocation = &FVulkanCustomMemManager::Alloc;
	//GDescriptorAllocationCallbacks.pfnReallocation = &FVulkanCustomMemManager::Realloc;
	//GDescriptorAllocationCallbacks.pfnFree = &FVulkanCustomMemManager::Free;
	//GDescriptorAllocationCallbacks.pfnInternalAllocation = &FVulkanCustomMemManager::InternalAllocationNotification;
	//GDescriptorAllocationCallbacks.pfnInternalFree = &FVulkanCustomMemManager::InternalFreeNotification;
}

inline FVulkanCustomMemManager::FType& FVulkanCustomMemManager::GetType(void* UserData, VkSystemAllocationScope AllocScope)
{
	//uint32 TypeIndex = (uint32)(size_t)UserData;
	//return GVulkanInstrumentedMemMgr.Types[TypeIndex];
	return GVulkanInstrumentedMemMgr.Types[AllocScope];
}

void* FVulkanCustomMemManager::Alloc(void* UserData, size_t Size, size_t Alignment, VkSystemAllocationScope AllocScope)
{
	FScopeLock Lock(&GMemMgrCS);
	void* Data = FMemory::Malloc(Size, Alignment);
	FType& Type = GetType(UserData, AllocScope);
	Type.MaxAllocSize = FMath::Max(Type.MaxAllocSize, Size);
	Type.UsedMemory += Size;
	Type.Allocs.Add(Data, Size);
	return Data;
}

void FVulkanCustomMemManager::Free(void* UserData, void* Mem)
{
	FScopeLock Lock(&GMemMgrCS);
	FMemory::Free(Mem);
	for (int32 Index = 0; Index < GVulkanInstrumentedMemMgr.Types.Num(); ++Index)
	{
		FType& Type = GVulkanInstrumentedMemMgr.Types[Index];
		size_t* Found = Type.Allocs.Find(Mem);
		if (Found)
		{
			Type.UsedMemory -= *Found;
			break;
		}
	}
}

void* FVulkanCustomMemManager::Realloc(void* UserData, void* Original, size_t Size, size_t Alignment, VkSystemAllocationScope AllocScope)
{
	FScopeLock Lock(&GMemMgrCS);
	void* Data = FMemory::Realloc(Original, Size, Alignment);
	FType& Type = GetType(UserData, AllocScope);
	size_t OldSize = Original ? Type.Allocs.FindAndRemoveChecked(Original) : 0;
	Type.UsedMemory -= OldSize;
	Type.Allocs.Add(Data, Size);
	Type.UsedMemory += Size;
	Type.MaxAllocSize = FMath::Max(Type.MaxAllocSize, Size);
	return Data;
}

void FVulkanCustomMemManager::InternalAllocationNotification(void* UserData, size_t Size, VkInternalAllocationType AllocationType, VkSystemAllocationScope AllocationScope)
{
}

void FVulkanCustomMemManager::InternalFreeNotification(void* UserData, size_t Size, VkInternalAllocationType AllocationType, VkSystemAllocationScope AllocationScope)
{
}
#endif
