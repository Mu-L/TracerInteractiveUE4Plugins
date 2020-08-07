// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanMemory.cpp: Vulkan memory RHI implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "VulkanMemory.h"
#include "Misc/OutputDeviceRedirector.h"
#include "HAL/PlatformStackWalk.h"
#include "VulkanLLM.h"

// This 'frame number' should only be used for the deletion queue
uint32 GVulkanRHIDeletionFrameNumber = 0;
const uint32 NUM_FRAMES_TO_WAIT_FOR_RESOURCE_DELETE = 2;

#define VULKAN_MAX_SUB_ALLOCATION (64llu << 20llu) // set to 0 to disable
#define VULKAN_FAKE_MEMORY_LIMIT 0llu /// set to # of GB to fake out of memory when hitting limit.


DECLARE_STATS_GROUP(TEXT("Vulkan Memory"), STATGROUP_VulkanMemory, STATCAT_Advanced);
DECLARE_MEMORY_STAT_EXTERN(TEXT("Dedicated Memory"), STAT_VulkanDedicatedMemory, STATGROUP_VulkanMemory, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("NonDedicated Memory"), STAT_VulkanNonDedicatedMemory, STATGROUP_VulkanMemory, );
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("FOldResourceHeap Pages"), STAT_VulkanOldResourceHeapPages, STATGROUP_VulkanMemory);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("FOldResourceHeap Allocations"), STAT_VulkanOldResourceHeapAllocations, STATGROUP_VulkanMemory);
DEFINE_STAT(STAT_VulkanDedicatedMemory);
DEFINE_STAT(STAT_VulkanNonDedicatedMemory);



#if UE_BUILD_DEBUG
RENDERCORE_API	void DumpRenderTargetPoolMemory(FOutputDevice& OutputDevice);
#endif

#if VULKAN_MEMORY_TRACK_CALLSTACK
static FCriticalSection GStackTraceMutex;
static char GStackTrace[65536];
static void CaptureCallStack(FString& OutCallstack, int32 Delta = 3)
{
	FScopeLock ScopeLock(&GStackTraceMutex);
	GStackTrace[0] = 0;
	FPlatformStackWalk::StackWalkAndDump(GStackTrace, 65535, Delta);
	OutCallstack = ANSI_TO_TCHAR(GStackTrace);
}
#endif

#if (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT)
static int32 GForceCoherent = 0;
static FAutoConsoleVariableRef CVarForceCoherentOperations(
	TEXT("r.Vulkan.ForceCoherentOperations"),
	GForceCoherent,
	TEXT("1 forces memory invalidation and flushing of coherent memory\n"),
	ECVF_ReadOnly
);
#else
constexpr int32 GForceCoherent = 0;
#endif

namespace VulkanRHI
{
	struct FVulkanMemoryAllocation
	{
		const TCHAR* Name;
		FName ResourceName;
		void* Address;
		void* RHIResouce;
		uint32 Size;
		uint32 Width;
		uint32 Height;
		uint32 Depth;
		uint32 BytesPerPixel;
	};

	struct FVulkanMemoryBucket
	{
		TArray<FVulkanMemoryAllocation> Allocations;
	};



	enum
	{
		GPU_ONLY_HEAP_PAGE_SIZE = 256 * 1024 * 1024,
		STAGING_HEAP_PAGE_SIZE = 32 * 1024 * 1024,
		ANDROID_MAX_HEAP_PAGE_SIZE = 16 * 1024 * 1024,
#if VULKAN_FREEPAGE_FOR_TYPE
		ANDROID_MAX_HEAP_IMAGE_PAGE_SIZE = 16 * 1024 * 1024,
		ANDROID_MAX_HEAP_BUFFER_PAGE_SIZE = 4 * 1024 * 1024,
#endif
	};

	int32 GVulkanUseBufferBinning = 0;
	static FAutoConsoleVariableRef CVarVulkanUseBufferBinning(
		TEXT("r.Vulkan.UseBufferBinning"),
		GVulkanUseBufferBinning,
		TEXT("Enable binning sub-allocations within buffers to help reduce fragmentation at the expense of higher high watermark [read-only]\n"),
		ECVF_ReadOnly
	);

	constexpr uint32 FResourceHeapManager::PoolSizes[(int32)FResourceHeapManager::EPoolSizes::SizesCount];
	constexpr uint32 FResourceHeapManager::BufferSizes[(int32)FResourceHeapManager::EPoolSizes::SizesCount + 1];

	static FCriticalSection GOldResourcePageLock;
	static FCriticalSection GOldResourceLock;
	static FCriticalSection GStagingLock;
	static FCriticalSection GDeviceMemLock;
	static FCriticalSection GFenceLock;
	static FCriticalSection GResourceHeapLock;

#if VULKAN_FAKE_MEMORY_LIMIT
	static uint64 GDeviceMemAllocated = 0;
#endif
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

	FDeviceMemoryAllocation* FDeviceMemoryManager::Alloc(bool bCanFail, VkDeviceSize AllocationSize, uint32 MemoryTypeIndex, void* DedicatedAllocateInfo, float Priority, const char* File, uint32 Line)
	{
		FScopeLock Lock(&GDeviceMemLock);

		check(AllocationSize > 0);
		check(MemoryTypeIndex < MemoryProperties.memoryTypeCount);

		VkMemoryAllocateInfo Info;
		ZeroVulkanStruct(Info, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
		Info.allocationSize = AllocationSize;
		Info.memoryTypeIndex = MemoryTypeIndex;

		
#if VULKAN_SUPPORTS_MEMORY_PRIORITY
		VkMemoryPriorityAllocateInfoEXT Prio;
		ZeroVulkanStruct(Prio, VK_STRUCTURE_TYPE_MEMORY_PRIORITY_ALLOCATE_INFO_EXT);
		Prio.priority = Priority;
		if (Device->GetOptionalExtensions().HasMemoryPriority)
		{
			Info.pNext = &Prio;
		}
#endif

#if VULKAN_SUPPORTS_DEDICATED_ALLOCATION
		if (DedicatedAllocateInfo)
		{
			((VkMemoryDedicatedAllocateInfoKHR*)DedicatedAllocateInfo)->pNext = Info.pNext;
			Info.pNext = DedicatedAllocateInfo;
			INC_DWORD_STAT_BY(STAT_VulkanDedicatedMemory, AllocationSize);
		}
		else
#endif
		{
			INC_DWORD_STAT_BY(STAT_VulkanNonDedicatedMemory, AllocationSize);
			check(!DedicatedAllocateInfo);
		}

		VkDeviceMemory Handle;
		VkResult Result;
#if VULKAN_FAKE_MEMORY_LIMIT
		GDeviceMemAllocated += AllocationSize;
		if( (VULKAN_FAKE_MEMORY_LIMIT << 30llu)  < (int64)GDeviceMemAllocated)
		{
			Handle = 0;
			Result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("DEV MEM %6.2f/%6.2f\n"), GDeviceMemAllocated / (1024.f *1024.f), VULKAN_FAKE_MEMORY_LIMIT *1024.f);
			GDeviceMemAllocated -= AllocationSize;

		}
		else
#endif
		{
			Result = VulkanRHI::vkAllocateMemory(DeviceHandle, &Info, VULKAN_CPU_ALLOCATOR, &Handle);
		}
		
		//FPlatformMisc::LowLevelOutputDebugStringf(TEXT("(%u)(%x) VulkanRHI::vkAllocateMemory size=%u(%6.3fmb) Type=%u\n"),
		//	GFrameNumberRenderThread, this, AllocationSize, AllocationSize / (1024.f * 1024.f), MemoryTypeIndex);

		if (Result == VK_ERROR_OUT_OF_DEVICE_MEMORY)
		{
#if UE_BUILD_DEBUG
			DumpMemory();
			GLog->PanicFlushThreadedLogs();
			DumpRenderTargetPoolMemory(*GLog);
			GLog->PanicFlushThreadedLogs();
#endif
			if (bCanFail)
			{
				UE_LOG(LogVulkanRHI, Warning, TEXT("Failed to allocate Device Memory, Requested=%.2fKb MemTypeIndex=%d"), (float)Info.allocationSize / 1024.0f, Info.memoryTypeIndex);
				return nullptr;
			}
			auto Callback = [=]()
			{
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
				DumpMemory();
				GLog->PanicFlushThreadedLogs();
#endif
				return FString::Printf(TEXT("Out of Device Memory, Requested=%.2fKb MemTypeIndex=%d"), (float)Info.allocationSize / 1024.0f, Info.memoryTypeIndex);
			};
			UE_LOG(LogVulkanRHI, Fatal, TEXT("%s"), *(Callback()));
		}
		else if (Result == VK_ERROR_OUT_OF_HOST_MEMORY)
		{
			if (bCanFail)
			{
				UE_LOG(LogVulkanRHI, Warning, TEXT("Failed to allocate Host Memory, Requested=%.2fKb MemTypeIndex=%d"), (float)Info.allocationSize / 1024.0f, Info.memoryTypeIndex);
				return nullptr;
			}
			auto Callback = [=]()
			{
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
				DumpMemory();
				GLog->PanicFlushThreadedLogs();
#endif
				return FString::Printf(TEXT("Out of Host Memory, Requested=%.2fKb MemTypeIndex=%d"), (float)Info.allocationSize / 1024.0f, Info.memoryTypeIndex);
			};
			UE_LOG(LogVulkanRHI, Error, TEXT("%s"), *(Callback()));
		}
		else
		{
			VERIFYVULKANRESULT(Result);
		}
		FDeviceMemoryAllocation* NewAllocation = new FDeviceMemoryAllocation;
		NewAllocation->DeviceHandle = DeviceHandle;
		NewAllocation->Handle = Handle;
		NewAllocation->Size = AllocationSize;
		NewAllocation->MemoryTypeIndex = MemoryTypeIndex;
		NewAllocation->bCanBeMapped = ((MemoryProperties.memoryTypes[MemoryTypeIndex].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		NewAllocation->bIsCoherent = ((MemoryProperties.memoryTypes[MemoryTypeIndex].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		NewAllocation->bIsCached = ((MemoryProperties.memoryTypes[MemoryTypeIndex].propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) == VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
#if VULKAN_SUPPORTS_DEDICATED_ALLOCATION
		NewAllocation->bDedicatedMemory = DedicatedAllocateInfo != 0;
#else
		NewAllocation->bDedicatedMemory = 0;

#endif
#if VULKAN_MEMORY_TRACK_FILE_LINE
		NewAllocation->File = File;
		NewAllocation->Line = Line;
		static uint32 ID = 0;
		NewAllocation->UID = ++ID;
#endif

#if VULKAN_MEMORY_TRACK_CALLSTACK
		CaptureCallStack(NewAllocation->Callstack);
#endif
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

#if VULKAN_USE_LLM
		LLM_PLATFORM_SCOPE_VULKAN(ELLMTagVulkan::VulkanDriverMemoryGPU);
		LLM(FLowLevelMemTracker::Get().OnLowLevelAlloc(ELLMTracker::Platform, (void*)NewAllocation->Handle, AllocationSize, ELLMTag::GraphicsPlatform, ELLMAllocType::System));
		LLM_TRACK_VULKAN_SPARE_MEMORY_GPU((int64)AllocationSize);
#endif

		INC_DWORD_STAT(STAT_VulkanNumPhysicalMemAllocations);

		return NewAllocation;
	}

	void FDeviceMemoryManager::Free(FDeviceMemoryAllocation*& Allocation)
	{
		FScopeLock Lock(&GDeviceMemLock);

		check(Allocation);
		check(Allocation->Handle != VK_NULL_HANDLE);
		check(!Allocation->bFreedBySystem);
#if VULKAN_FAKE_MEMORY_LIMIT
		GDeviceMemAllocated -= Allocation->Size;
#endif
		if (Allocation->bDedicatedMemory)
		{
			DEC_DWORD_STAT_BY(STAT_VulkanDedicatedMemory, Allocation->Size);
		}
		else
		{

			DEC_DWORD_STAT_BY(STAT_VulkanNonDedicatedMemory, Allocation->Size);
		}
		VulkanRHI::vkFreeMemory(DeviceHandle, Allocation->Handle, VULKAN_CPU_ALLOCATOR);

#if VULKAN_USE_LLM
		LLM(FLowLevelMemTracker::Get().OnLowLevelFree(ELLMTracker::Platform, (void*)Allocation->Handle, ELLMAllocType::System));
		LLM_TRACK_VULKAN_SPARE_MEMORY_GPU(-(int64)Allocation->Size);
#endif

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
#if 1 //in case of debugging, it is useful to be able to log directly to LowLevelPrintf, as this is easier to diff. Please do not delete this code.
#define VULKAN_LOGMEMORY(fmt, ...) UE_LOG(LogVulkanRHI, Display, fmt, ##__VA_ARGS__)
#else 
#define VULKAN_LOGMEMORY(fmt, ...) FPlatformMisc::LowLevelOutputDebugStringf(fmt TEXT("\n"), ##__VA_ARGS__)
#endif
		VULKAN_LOGMEMORY(TEXT("Device Memory: %d allocations on %d heaps"), NumAllocations, HeapInfos.Num());
		for (int32 Index = 0; Index < HeapInfos.Num(); ++Index)
		{
			FHeapInfo& HeapInfo = HeapInfos[Index];
			VULKAN_LOGMEMORY(TEXT("\tHeap %d, %d allocations"), Index, HeapInfo.Allocations.Num());
			uint64 TotalSize = 0;

			if (HeapInfo.Allocations.Num() > 0)
			{
#if VULKAN_MEMORY_TRACK_FILE_LINE
				VULKAN_LOGMEMORY(TEXT("\t\tAlloc AllocSize(MB) TotalSize(MB)    Handle  UID  File(Line)"));
#else
				VULKAN_LOGMEMORY(TEXT("\t\tAlloc AllocSize(MB) TotalSize(MB)    Handle"));
#endif
			}

			for (int32 SubIndex = 0; SubIndex < HeapInfo.Allocations.Num(); ++SubIndex)
			{
				FDeviceMemoryAllocation* Allocation = HeapInfo.Allocations[SubIndex];
#if VULKAN_MEMORY_TRACK_FILE_LINE
				VULKAN_LOGMEMORY(TEXT("\t\t%5d %13.3f %13.3f %p %4d %s(%d)"), SubIndex, Allocation->Size / 1024.f / 1024.f, TotalSize / 1024.0f / 1024.0f, (void*)Allocation->Handle, Allocation->UID, ANSI_TO_TCHAR(Allocation->File), Allocation->Line);
#else
				VULKAN_LOGMEMORY(TEXT("\t\t%5d %13.3f %13.3f %p"), SubIndex, Allocation->Size / 1024.f / 1024.f, TotalSize / 1024.0f / 1024.0f, (void*)Allocation->Handle);
#endif
				TotalSize += Allocation->Size;
			}
			VULKAN_LOGMEMORY(TEXT("\t\tTotal Allocated %.2f MB, Peak %.2f MB"), TotalSize / 1024.0f / 1024.0f, HeapInfo.PeakSize / 1024.0f / 1024.0f);
		}
#if VULKAN_OBJECT_TRACKING
		{

			TSortedMap<uint32, FVulkanMemoryBucket> AllocationBuckets;
			auto Collector = [&](const TCHAR* Name, FName ResourceName, void* Address, void* RHIRes, uint32 Width, uint32 Height, uint32 Depth, uint32 Format)
			{
				uint32 BytesPerPixel = (Format != VK_FORMAT_UNDEFINED ? GetNumBitsPerPixel((VkFormat)Format) : 8) / 8;
				uint32 Size = FPlatformMath::Max(Width,1u) * FPlatformMath::Max(Height,1u) * FPlatformMath::Max(Depth, 1u) * BytesPerPixel;
				uint32 Bucket = Size;
				if(Bucket >= (1<<20))
				{
					Bucket = (Bucket + ((1 << 20) - 1)) & ~((1 << 20)-1);
				}
				else
				{
					Bucket = (Bucket + ((1 << 10) - 1)) & ~((1 << 10)-1);
				}
				FVulkanMemoryAllocation Allocation = 
				{
					Name, ResourceName, Address, RHIRes, Size, Width, Height, Depth, BytesPerPixel
				};
				FVulkanMemoryBucket& ActualBucket = AllocationBuckets.FindOrAdd(Bucket);
				ActualBucket.Allocations.Add(Allocation);
			};


			TVulkanTrackBase<FVulkanTextureBase>::CollectAll(Collector);
			TVulkanTrackBase<FVulkanResourceMultiBuffer>::CollectAll(Collector);
			for(auto& Itr : AllocationBuckets)
			{
				VULKAN_LOGMEMORY(TEXT("***** BUCKET < %d kb *****"), Itr.Key/1024);
				FVulkanMemoryBucket& B = Itr.Value;
				uint32 Size = 0;
				for (FVulkanMemoryAllocation& A : B.Allocations)
				{
					Size += A.Size;
				}
				VULKAN_LOGMEMORY(TEXT("\t\t%d / %d kb"), B.Allocations.Num(), Size / 1024);


				B.Allocations.Sort([](const FVulkanMemoryAllocation& L, const FVulkanMemoryAllocation& R)
				{
					return L.Address < R.Address;
				}
				);
				for(FVulkanMemoryAllocation& A : B.Allocations)
				{
					VULKAN_LOGMEMORY(TEXT("\t\t%p/%p %6.2fkb (%d) %5d/%5d/%5d %s ::: %s"), A.Address, A.RHIResouce, A.Size / 1024.f, A.Size, A.Width, A.Height, A.Depth, A.Name, *A.ResourceName.ToString());
				}
			}
		}
#endif
		Device->GetResourceHeapManager().DumpMemory();
		GLog->PanicFlushThreadedLogs();
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
		checkf(InSize == VK_WHOLE_SIZE || InSize + Offset <= Size, TEXT("Failed to Map %llu bytes, Offset %llu, AllocSize %llu bytes"), InSize, Offset, Size);

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
		if (!IsCoherent() || GForceCoherent != 0)
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
		if (!IsCoherent() || GForceCoherent != 0)
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

#define UE_VK_MEMORY_KEEP_FREELIST_SORTED					1
#define UE_VK_MEMORY_JOIN_FREELIST_ON_THE_FLY				(UE_VK_MEMORY_KEEP_FREELIST_SORTED && 1)
// debugging
#define UE_VK_MEMORY_KEEP_FREELIST_SORTED_CATCHBUGS			0

	void FRange::JoinConsecutiveRanges(TArray<FRange>& Ranges)
	{
		if (Ranges.Num() > 1)
		{
#if !UE_VK_MEMORY_KEEP_FREELIST_SORTED
			Ranges.Sort();
#else
	#if UE_VK_MEMORY_KEEP_FREELIST_SORTED_CATCHBUGS
			SanityCheck(Ranges);
	#endif
#endif

#if !UE_VK_MEMORY_JOIN_FREELIST_ON_THE_FLY
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
#endif
		}
	}

	int32 FRange::InsertAndTryToMerge(TArray<FRange>& Ranges, const FRange& Item, int32 ProposedIndex)
	{
#if !UE_VK_MEMORY_JOIN_FREELIST_ON_THE_FLY
		int32 Ret = Ranges.Insert(Item, ProposedIndex);
#else
		// there are four cases here
		// 1) nothing can be merged (distinct ranges)		 XXXX YYY ZZZZZ  =>   XXXX YYY ZZZZZ
		// 2) new range can be merged with the previous one: XXXXYYY  ZZZZZ  =>   XXXXXXX  ZZZZZ
		// 3) new range can be merged with the next one:     XXXX  YYYZZZZZ  =>   XXXX  ZZZZZZZZ
		// 4) new range perfectly fills the gap:             XXXXYYYYYZZZZZ  =>   XXXXXXXXXXXXXX

		// note: we can have a case where we're inserting at the beginning of the array (no previous element), but we won't have a case
		// where we're inserting at the end (no next element) - AppendAndTryToMerge() should be called instead
		checkf(Item.Offset < Ranges[ProposedIndex].Offset, TEXT("FRange::InsertAndTryToMerge() was called to append an element - internal logic error, FRange::AppendAndTryToMerge() should have been called instead."))
		int32 Ret = ProposedIndex;
		if (UNLIKELY(ProposedIndex == 0))
		{
			// only cases 1 and 3 apply
			FRange& NextRange = Ranges[Ret];

			if (UNLIKELY(NextRange.Offset == Item.Offset + Item.Size))
			{
				NextRange.Offset = Item.Offset;
				NextRange.Size += Item.Size;
			}
			else
			{
				Ret = Ranges.Insert(Item, ProposedIndex);
			}
		}
		else
		{
			// all cases apply
			FRange& NextRange = Ranges[ProposedIndex];
			FRange& PrevRange = Ranges[ProposedIndex - 1];

			// see if we can merge with previous
			if (UNLIKELY(PrevRange.Offset + PrevRange.Size == Item.Offset))
			{
				// case 2, can still end up being case 4
				PrevRange.Size += Item.Size;

				if (UNLIKELY(PrevRange.Offset + PrevRange.Size == NextRange.Offset))
				{
					// case 4
					PrevRange.Size += NextRange.Size;
					Ranges.RemoveAt(ProposedIndex);
					Ret = ProposedIndex - 1;
				}
			}
			else if (UNLIKELY(Item.Offset + Item.Size == NextRange.Offset))
			{
				// case 3
				NextRange.Offset = Item.Offset;
				NextRange.Size += Item.Size;
			}
			else
			{
				// case 1 - the new range is disjoint with both
				Ret = Ranges.Insert(Item, ProposedIndex);	// this can invalidate NextRange/PrevRange references, don't touch them after this
			}
		}
#endif

#if UE_VK_MEMORY_KEEP_FREELIST_SORTED_CATCHBUGS
		SanityCheck(Ranges);
#endif
		return Ret;
	}

	int32 FRange::AppendAndTryToMerge(TArray<FRange>& Ranges, const FRange& Item)
	{
#if !UE_VK_MEMORY_JOIN_FREELIST_ON_THE_FLY
		int32 Ret = Ranges.Add(Item);
#else
		int32 Ret = Ranges.Num() - 1;
		// we only get here when we have an element in front of us
		checkf(Ret >= 0, TEXT("FRange::AppendAndTryToMerge() was called on an empty array."));
		FRange& PrevRange = Ranges[Ret];
		if (UNLIKELY(PrevRange.Offset + PrevRange.Size == Item.Offset))
		{
			PrevRange.Size += Item.Size;
		}
		else
		{
			Ret = Ranges.Add(Item);
		}
#endif

#if UE_VK_MEMORY_KEEP_FREELIST_SORTED_CATCHBUGS
		SanityCheck(Ranges);
#endif
		return Ret;
	}

	void FRange::AllocateFromEntry(TArray<FRange>& Ranges, int32 Index, uint32 SizeToAllocate)
	{
		FRange& Entry = Ranges[Index];
		if (SizeToAllocate < Entry.Size)
		{
			// Modify current free entry in-place.
			Entry.Size -= SizeToAllocate;
			Entry.Offset += SizeToAllocate;
		}
		else
		{
			// Remove this free entry.
			Ranges.RemoveAt(Index, 1, false);
#if UE_VK_MEMORY_KEEP_FREELIST_SORTED_CATCHBUGS
			SanityCheck(Ranges);
#endif
		}
	}

	void FRange::SanityCheck(TArray<FRange>& Ranges)
	{
		if (UE_VK_MEMORY_KEEP_FREELIST_SORTED_CATCHBUGS)	// keeping the check code visible to the compiler
		{
			int32 Num = Ranges.Num();
			if (Num > 1)
			{
				for (int32 ChkIndex = 0; ChkIndex < Num - 1; ++ChkIndex)
				{
					checkf(Ranges[ChkIndex].Offset < Ranges[ChkIndex + 1].Offset, TEXT("Array is not sorted!"));
					// if we're joining on the fly, then there cannot be any adjoining ranges, so use < instead of <=
#if UE_VK_MEMORY_JOIN_FREELIST_ON_THE_FLY
					checkf(Ranges[ChkIndex].Offset + Ranges[ChkIndex].Size < Ranges[ChkIndex + 1].Offset, TEXT("Ranges are overlapping or adjoining!"));
#else
					checkf(Ranges[ChkIndex].Offset + Ranges[ChkIndex].Size <= Ranges[ChkIndex + 1].Offset, TEXT("Ranges are overlapping!"));
#endif
				}
			}
		}
	}


	int32 FRange::Add(TArray<FRange>& Ranges, const FRange & Item)
	{
#if UE_VK_MEMORY_KEEP_FREELIST_SORTED
		// find the right place to add
		int32 NumRanges = Ranges.Num();
		if (LIKELY(NumRanges <= 0))
		{
			return Ranges.Add(Item);
		}

		FRange* Data = Ranges.GetData();
		for (int32 Index = 0; Index < NumRanges; ++Index)
		{
			if (UNLIKELY(Data[Index].Offset > Item.Offset))
			{
				return InsertAndTryToMerge(Ranges, Item, Index);
			}
		}

		// if we got this far and still haven't inserted, we're a new element
		return AppendAndTryToMerge(Ranges, Item);
#else
		return Ranges.Add(Item);
#endif
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

		INC_DWORD_STAT(STAT_VulkanOldResourceHeapAllocations);


	}

	FOldResourceAllocation::~FOldResourceAllocation()
	{
		DEC_DWORD_STAT(STAT_VulkanOldResourceHeapAllocations);
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
		FRange::Add(FreeList, FullRange);
		INC_DWORD_STAT(STAT_VulkanOldResourceHeapPages);
	}

	FOldResourceHeapPage::~FOldResourceHeapPage()
	{
		DEC_DWORD_STAT(STAT_VulkanOldResourceHeapPages);
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
				FRange::AllocateFromEntry(FreeList, Index, AllocatedSize);

				UsedSize += AllocatedSize;

				FOldResourceAllocation* NewResourceAllocation = new FOldResourceAllocation(this, DeviceMemoryAllocation, Size, AlignedOffset, AllocatedSize, AllocatedOffset, File, Line);
				ResourceAllocations.Add(NewResourceAllocation);

				PeakNumAllocations = FMath::Max(PeakNumAllocations, ResourceAllocations.Num());

				LLM_TRACK_VULKAN_HIGH_LEVEL_ALLOC(NewResourceAllocation, Size);

				LLM_TRACK_VULKAN_SPARE_MEMORY_GPU(-(int64)Size);

				return NewResourceAllocation;
			}
		}

		return nullptr;
	}

	void FOldResourceHeapPage::ReleaseAllocation(FOldResourceAllocation* Allocation)
	{
		{
			LLM_TRACK_VULKAN_HIGH_LEVEL_FREE(Allocation);

			LLM_TRACK_VULKAN_SPARE_MEMORY_GPU((int64)Allocation->RequestedSize);

			FScopeLock ScopeLock(&GOldResourcePageLock);
			ResourceAllocations.RemoveSingleSwap(Allocation, false);

			FRange NewFree;
			NewFree.Offset = Allocation->AllocationOffset;
			NewFree.Size = Allocation->AllocationSize;

			FRange::Add(FreeList, NewFree);
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
#if !UE_VK_MEMORY_JOIN_FREELIST_ON_THE_FLY
		FRange::JoinConsecutiveRanges(FreeList);
#endif

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
#if VULKAN_FREEPAGE_FOR_TYPE
		uint32 TargetDefaultSizeImage = ANDROID_MAX_HEAP_IMAGE_PAGE_SIZE;
		uint32 TargetPageSizeForBuffer = ANDROID_MAX_HEAP_BUFFER_PAGE_SIZE;
		DefaultPageSizeForImage = FMath::Min(TargetDefaultSizeImage, DefaultPageSize);
		DefaultPageSizeForBuffer = FMath::Min(TargetPageSizeForBuffer, DefaultPageSize);
#endif
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

#if VULKAN_FREEPAGE_FOR_TYPE			
		for (int32 Index = 0; Index < FreeBufferPages.Num(); ++Index)
		{
			FOldResourceHeapPage* Page = FreeBufferPages[Index];
			Owner->GetParent()->GetMemoryManager().Free(Page->DeviceMemoryAllocation);
			delete Page;
		}

		for (int32 Index = 0; Index < FreeImagePages.Num(); ++Index)
		{
			FOldResourceHeapPage* Page = FreeImagePages[Index];
			Owner->GetParent()->GetMemoryManager().Free(Page->DeviceMemoryAllocation);
			delete Page;
		}
#else
		for (int32 Index = 0; Index < FreePages.Num(); ++Index)
		{
			FOldResourceHeapPage* Page = FreePages[Index];
			Owner->GetParent()->GetMemoryManager().Free(Page->DeviceMemoryAllocation);
			delete Page;
		}
#endif
	}

	void FOldResourceHeap::FreePage(FOldResourceHeapPage* InPage)
	{
		FScopeLock ScopeLock(&GOldResourceLock);
		check(InPage->JoinFreeBlocks());
		int32 Index = -1;

		bool UsedInImage = false;
		bool Removed = false;
#if VULKAN_FREEPAGE_FOR_TYPE
		if (UsedBufferPages.Num() > 1 && UsedBufferPages.Find(InPage, Index))
#else
		if (UsedBufferPages.Find(InPage, Index))
#endif
		{
			UsedBufferPages.RemoveAtSwap(Index, 1, false);
			Removed = true;
		}
#if VULKAN_FREEPAGE_FOR_TYPE
		else if (UsedImagePages.Num() > 1 && UsedImagePages.Find(InPage, Index))
#else
		else if (UsedImagePages.Find(InPage, Index))
#endif
		{
			UsedInImage = true;
			Removed = true;
			UsedImagePages.RemoveAtSwap(Index, 1, false);
		}
		else
		{
#if VULKAN_SUPPORTS_DEDICATED_ALLOCATION
			int32 RemovedDedicated = UsedDedicatedImagePages.RemoveSingleSwap(InPage, false);
			check(RemovedDedicated > 0);
#else
			//checkf(0, TEXT("Page not found in Pool!"));
#endif
		}
		if (Removed)
			InPage->FrameFreed = GFrameNumberRenderThread;

#if VULKAN_FREEPAGE_FOR_TYPE
		if (Removed)
		{
			if (UsedInImage)
			{
				FreeImagePages.Add(InPage);
			}
			else
			{
				FreeBufferPages.Add(InPage);
			}
		}
#else		
		if (Removed)
		FreePages.Add(InPage);
#endif
	}

	void FOldResourceHeap::ReleaseFreedPages(bool bImmediately)
	{
#if VULKAN_FREEPAGE_FOR_TYPE
		TArray<FOldResourceHeapPage*> PageToReleases;
#else
		FOldResourceHeapPage* PageToRelease = nullptr;
#endif
		{
			FScopeLock ScopeLock(&GOldResourceLock);

#if VULKAN_FREEPAGE_FOR_TYPE
			for (int32 Index = (bImmediately ? 0 : 1); Index < FreeBufferPages.Num(); ++Index)
			{
				FOldResourceHeapPage* Page = FreeBufferPages[Index];
				if (bImmediately || Page->FrameFreed + NUM_FRAMES_TO_WAIT_BEFORE_RELEASING_TO_OS < GFrameNumberRenderThread)
				{
					PageToReleases.Add(Page);
					FreeBufferPages.RemoveAtSwap(Index, 1, false);
					break;
				}
			}
			for (int32 Index = (bImmediately ? 0 : 1); Index < FreeImagePages.Num(); ++Index)
			{
				FOldResourceHeapPage* Page = FreeImagePages[Index];
				if (bImmediately || Page->FrameFreed + NUM_FRAMES_TO_WAIT_BEFORE_RELEASING_TO_OS < GFrameNumberRenderThread)
				{
					PageToReleases.Add(Page);
					FreeImagePages.RemoveAtSwap(Index, 1, false);
					break;
				}
			}
#else
#if PLATFORM_ANDROID && !PLATFORM_LUMIN
			// free all pages, as this would keep staging buffers around forever, and they are 64mb in practice
			for (int32 Index = 0; Index < FreePages.Num(); ++Index)
#else
			// Leave a page not freed to avoid potential hitching
			for (int32 Index = (bImmediately ? 0 : 1); Index < FreePages.Num(); ++Index)
#endif
			{
				FOldResourceHeapPage* Page = FreePages[Index];
				if (bImmediately || Page->FrameFreed + NUM_FRAMES_TO_WAIT_BEFORE_RELEASING_TO_OS < GFrameNumberRenderThread)
				{
					PageToRelease = Page;
					FreePages.RemoveAtSwap(Index, 1, false);
					break;
				}
			}
#endif //VULKAN_FREEPAGE_FOR_TYPE
		}

#if VULKAN_FREEPAGE_FOR_TYPE
		for (int32 n = 0; n < PageToReleases.Num(); ++n )
		{
			Owner->GetParent()->GetMemoryManager().Free(PageToReleases[n]->DeviceMemoryAllocation);
			UsedMemory -= PageToReleases[n]->MaxSize;
			delete PageToReleases[n];
		}
#else
		if (PageToRelease)
		{
			Owner->GetParent()->GetMemoryManager().Free(PageToRelease->DeviceMemoryAllocation);
			UsedMemory -= PageToRelease->MaxSize;
			delete PageToRelease;
		}
#endif
	}

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	void FOldResourceHeap::DumpMemory()
	{
#if VULKAN_FREEPAGE_FOR_TYPE
		UE_LOG(LogVulkanRHI, Display, TEXT("%d Free Buffer Pages"), FreeBufferPages.Num());
		UE_LOG(LogVulkanRHI, Display, TEXT("%d Free Image Pages"), FreeImagePages.Num());
#else
		UE_LOG(LogVulkanRHI, Display, TEXT("%d Free Pages"), FreePages.Num());
#endif
		auto DumpPages = [&](TArray<FOldResourceHeapPage*>& UsedPages, const TCHAR* TypeName)
		{
			UE_LOG(LogVulkanRHI, Display, TEXT("\t%s Pages: %d Used, Peak Allocation Size on a Page %d"), TypeName, UsedPages.Num(), PeakPageSize);
			uint64 SubAllocUsedMemory = 0;
			uint64 SubAllocAllocatedMemory = 0;
			uint32 NumSuballocations = 0;
			for (int32 Index = 0; Index < UsedPages.Num(); ++Index)
			{
				SubAllocUsedMemory += UsedPages[Index]->UsedSize;
				SubAllocAllocatedMemory += UsedPages[Index]->MaxSize;
				NumSuballocations += UsedPages[Index]->ResourceAllocations.Num();

				UE_LOG(LogVulkanRHI, Display, TEXT("\t\t%d: ID %4d %4d suballocs, %4d free chunks (%6.2fmb used/%6.2fmb free/%6.2fmb max) DeviceMemory %p"), Index, UsedPages[Index]->GetID(), UsedPages[Index]->ResourceAllocations.Num(), 
					UsedPages[Index]->FreeList.Num(), 
					UsedPages[Index]->UsedSize / (1024.f*1024.f),
					(UsedPages[Index]->MaxSize - UsedPages[Index]->UsedSize) / (1024.f*1024.f), 
					UsedPages[Index]->MaxSize / (1024.f*1024.f),
					(void*)UsedPages[Index]->DeviceMemoryAllocation->GetHandle());
			}

			UE_LOG(LogVulkanRHI, Display, TEXT("%d Suballocations for Used/Total: %.2fmb/%.2fmb = %.2f%%"), NumSuballocations, SubAllocUsedMemory / (1024.f*1024.f), SubAllocAllocatedMemory / (1024.f*1024.f), SubAllocAllocatedMemory > 0 ? 100.0f * (float)SubAllocUsedMemory / (float)SubAllocAllocatedMemory : 0.0f);
		};

		DumpPages(UsedBufferPages, TEXT("Buffer"));
		DumpPages(UsedImagePages, TEXT("Image"));
		//UE_LOG(LogVulkanRHI, Display, TEXT("\tUsed Memory %d in %d Suballocations, Free Memory in pages %d, Heap free memory %ull"), SubAllocUsedMemory, NumSuballocations, FreeMemory, TotalMemory - UsedMemory);
	}
#endif

	FOldResourceAllocation* FOldResourceHeap::AllocateResource(EType Type, uint32 Size, uint32 Alignment, bool bMapAllocation, bool bForceSeparateAllocation, const char* File, uint32 Line)
	{
		FScopeLock ScopeLock(&GOldResourceLock);

		TArray<FOldResourceHeapPage*>& UsedPages = (Type == EType::Image) ? UsedImagePages : UsedBufferPages;
#if VULKAN_SINGLE_ALLOCATION_PER_RESOURCE
		uint32 AllocationSize = Size;
#else

#if VULKAN_FREEPAGE_FOR_TYPE
		uint32 TargetDefaultPageSize = (Type == EType::Image) ? DefaultPageSizeForImage : DefaultPageSizeForBuffer;
#else
		uint32 TargetDefaultPageSize = DefaultPageSize;
#endif
		uint32 AllocationSize;
		
		if(!bForceSeparateAllocation)
		{
			if (Size < TargetDefaultPageSize)
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
							//FPlatformMisc::LowLevelOutputDebugStringf(TEXT("JYP (%u)(%x)(%s) Found Heap in UsedPage size=%u max=%u"), 
							//	GFrameNumberRenderThread, this, Type == EType::Image ? TEXT("Image") : TEXT("Buffer"), Size, TargetDefaultPageSize);
							return ResourceAllocation;
						}
					}
				}
			}

#if VULKAN_FREEPAGE_FOR_TYPE
			TArray<FOldResourceHeapPage*>& FreePages = (Type == EType::Image) ? FreeImagePages : FreeBufferPages;
			for (int32 Index = 0; Index < FreePages.Num(); ++Index)
			{
				FOldResourceHeapPage* Page = FreePages[Index];
				if (Page->DeviceMemoryAllocation->IsMapped() == bMapAllocation)
				{
					FOldResourceAllocation* ResourceAllocation = Page->TryAllocate(Size, Alignment, File, Line);
					if (ResourceAllocation)
					{
						//FPlatformMisc::LowLevelOutputDebugStringf(TEXT("JYP (%u)(%x)(%s) Found Heap in FreePages size=%u max=%u"),
						//	GFrameNumberRenderThread, this, Type == EType::Image ? TEXT("Image") : TEXT("Buffer"), Size, TargetDefaultPageSize);
						FreePages.RemoveSingleSwap(Page, false);
						UsedPages.Add(Page);
						return ResourceAllocation;
					}
				}
			}
#else
			for (int32 Index = 0; Index < FreePages.Num(); ++Index)
			{
				FOldResourceHeapPage* Page = FreePages[Index];
				if (Page->DeviceMemoryAllocation->IsMapped() == bMapAllocation)
				{
					FOldResourceAllocation* ResourceAllocation = Page->TryAllocate(Size, Alignment, File, Line);
					if (ResourceAllocation)
					{
						//FPlatformMisc::LowLevelOutputDebugStringf(TEXT("JYP (%u)(%x)(%s) Found Heap in FreePages size=%u max=%u"),
						//	GFrameNumberRenderThread, this, Type == EType::Image ? TEXT("Image") : TEXT("Buffer"), Size, TargetDefaultPageSize);
						FreePages.RemoveSingleSwap(Page, false);
						UsedPages.Add(Page);
						return ResourceAllocation;
					}
				}
			}
#endif

#if VULKAN_MAX_SUB_ALLOCATION > 0
			constexpr bool bUseMaxSubAllocation = true;
#else
			constexpr bool bUseMaxSubAllocation = false;
#endif
			if (bUseMaxSubAllocation && Size >= VULKAN_MAX_SUB_ALLOCATION)
			{
				AllocationSize = Size;
			}
			else
			{
				AllocationSize = FMath::Max(Size, TargetDefaultPageSize);
			}
		}
		else
		{
			// We get here when bForceSeparateAllocation is true, which is used for lazy allocations, since pooling those doesn't make sense.
			AllocationSize = Size;
		}
#endif // VULKAN_SINGLE_ALLOCATION_PER_RESOURCE
		FDeviceMemoryAllocation* DeviceMemoryAllocation = Owner->GetParent()->GetMemoryManager().Alloc(true, AllocationSize, MemoryTypeIndex, nullptr, VULKAN_MEMORY_HIGHEST_PRIORITY, File, Line);
		if (!DeviceMemoryAllocation && Size != AllocationSize)
		{
			// Retry with a smaller size
			DeviceMemoryAllocation = Owner->GetParent()->GetMemoryManager().Alloc(false, Size, MemoryTypeIndex, nullptr, VULKAN_MEMORY_HIGHEST_PRIORITY, File, Line);
			if (!DeviceMemoryAllocation)
			{
				UE_LOG(LogVulkanRHI, Fatal, TEXT("Out of memory on Vulkan; MemoryTypeIndex=%d, AllocSize=%0.3fMB"), MemoryTypeIndex, (float)AllocationSize / 1048576.0f);
			}
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

		//FPlatformMisc::LowLevelOutputDebugStringf(TEXT("JYP (%u)(%x)(%s) New Heap created size=%u max=%u"),
		//	GFrameNumberRenderThread, this, Type == EType::Image ? TEXT("Image") : TEXT("Buffer"), Size, TargetDefaultPageSize);
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
		FDeviceMemoryAllocation* DeviceMemoryAllocation = Owner->GetParent()->GetMemoryManager().Alloc(false, AllocationSize, MemoryTypeIndex, &DedicatedAllocInfo, VULKAN_MEMORY_HIGHEST_PRIORITY, File, Line);

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
				if (VULKAN_FAKE_MEMORY_LIMIT)
				{
					HeapSize = FMath::Min<VkDeviceSize>(VULKAN_FAKE_MEMORY_LIMIT << 30llu, HeapSize);
				}
				VkDeviceSize PageSize = FMath::Min<VkDeviceSize>(HeapSize / 8, GPU_ONLY_HEAP_PAGE_SIZE);
#if PLATFORM_ANDROID && !PLATFORM_LUMIN
				PageSize = FMath::Min<VkDeviceSize>(PageSize, ANDROID_MAX_HEAP_PAGE_SIZE);
#endif
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
		{
			ProcessPendingUBFreesNoLock(true);
			check(UBAllocations.PendingFree.Num() == 0);
		}
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

		for (auto& UsedAllocations : UsedBufferAllocations)
		{
			for (int32 Index = UsedAllocations.Num() - 1; Index >= 0; --Index)
			{
				FBufferAllocation* BufferAllocation = UsedAllocations[Index];
				if (!BufferAllocation->JoinFreeBlocks())
				{
					UE_LOG(LogVulkanRHI, Warning, TEXT("Suballocation(s) for Buffer %p were not released."), (void*)BufferAllocation->Buffer);
				}

				BufferAllocation->Destroy(GetParent());
				GetParent()->GetMemoryManager().Free(BufferAllocation->MemoryAllocation);
				delete BufferAllocation;
			}
			UsedAllocations.Empty(0);
		}

		for (auto& FreeAllocations : FreeBufferAllocations)
		{
			for (int32 Index = 0; Index < FreeAllocations.Num(); ++Index)
			{
				FBufferAllocation* BufferAllocation = FreeAllocations[Index];
				BufferAllocation->Destroy(GetParent());
				GetParent()->GetMemoryManager().Free(BufferAllocation->MemoryAllocation);
				delete BufferAllocation;
			}
			FreeAllocations.Empty(0);
		}
	}

	void FResourceHeapManager::ReleaseFreedResources(bool bImmediately)
	{
		FBufferAllocation* BufferAllocationToRelease = nullptr;

		{
			FScopeLock ScopeLock(&GResourceHeapLock);
			for (auto& FreeAllocations : FreeBufferAllocations)
			{
				for (int32 Index = 0; Index < FreeAllocations.Num(); ++Index)
				{
					FBufferAllocation* BufferAllocation = FreeAllocations[Index];
					if (bImmediately || BufferAllocation->FrameFreed + NUM_FRAMES_TO_WAIT_BEFORE_RELEASING_TO_OS < GFrameNumberRenderThread)
					{
						BufferAllocationToRelease = BufferAllocation;
						FreeAllocations.RemoveAtSwap(Index, 1, false);
						break;
					}
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
		uint32 Alignment = 1;

		float Priority = VULKAN_MEMORY_MEDIUM_PRIORITY;

		bool bIsTexelBuffer = (BufferUsageFlags & (VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT)) != 0;
		bool bIsStorageBuffer = (BufferUsageFlags & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) != 0;
		if (bIsTexelBuffer || bIsStorageBuffer)
		{
			Alignment = FMath::Max(Alignment, bIsTexelBuffer ? (uint32)Limits.minTexelBufferOffsetAlignment : 0);
			Alignment = FMath::Max(Alignment, bIsStorageBuffer ? (uint32)Limits.minStorageBufferOffsetAlignment : 0);
		}
		else
		{
			bool bIsVertexOrIndexBuffer = (BufferUsageFlags & (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) != 0;
			if (bIsVertexOrIndexBuffer)
			{
				// No alignment restrictions on Vertex or Index buffers, can live on CPU mem
				Priority = VULKAN_MEMORY_LOW_PRIORITY;
			}
			else
			{
				// Uniform buffer
				ensure((BufferUsageFlags & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) == VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
				Alignment = FMath::Max(Alignment, (uint32)Limits.minUniformBufferOffsetAlignment);

				Priority = VULKAN_MEMORY_HIGHER_PRIORITY;
			}
		}

		if (BufferUsageFlags & (VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT))
		{
			Priority = VULKAN_MEMORY_HIGHEST_PRIORITY;
		}

		int32 PoolSize = (int32)GetPoolTypeForAlloc(Size, Alignment);
		if (PoolSize != (int32)EPoolSizes::SizesCount)
		{
			Size = PoolSizes[PoolSize];
		}

		FScopeLock ScopeLock(&GResourceHeapLock);

		for (int32 Index = 0; Index < UsedBufferAllocations[PoolSize].Num(); ++Index)
		{
			FBufferAllocation* BufferAllocation = UsedBufferAllocations[PoolSize][Index];
			if ((BufferAllocation->BufferUsageFlags & BufferUsageFlags) == BufferUsageFlags &&
				(BufferAllocation->MemoryPropertyFlags & MemoryPropertyFlags) == MemoryPropertyFlags)
			{
				FBufferSuballocation* Suballocation = (FBufferSuballocation*)BufferAllocation->TryAllocate(Size, Alignment, File, Line);
				if (Suballocation)
				{
					return Suballocation;
				}
			}
		}

		for (int32 Index = 0; Index < FreeBufferAllocations[PoolSize].Num(); ++Index)
		{
			FBufferAllocation* BufferAllocation = FreeBufferAllocations[PoolSize][Index];
			if ((BufferAllocation->BufferUsageFlags & BufferUsageFlags) == BufferUsageFlags &&
				(BufferAllocation->MemoryPropertyFlags & MemoryPropertyFlags) == MemoryPropertyFlags)
			{
				FBufferSuballocation* Suballocation = (FBufferSuballocation*)BufferAllocation->TryAllocate(Size, Alignment, File, Line);
				if (Suballocation)
				{
					FreeBufferAllocations[PoolSize].RemoveAtSwap(Index, 1, false);
					UsedBufferAllocations[PoolSize].Add(BufferAllocation);
					return Suballocation;
				}
			}
		}

		// New Buffer
		uint32 BufferSize = FMath::Max(Size, BufferSizes[PoolSize]);

		VkBuffer Buffer;
		VkBufferCreateInfo BufferCreateInfo;
		ZeroVulkanStruct(BufferCreateInfo, VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
		BufferCreateInfo.size = BufferSize;
		BufferCreateInfo.usage = BufferUsageFlags;
		VERIFYVULKANRESULT(VulkanRHI::vkCreateBuffer(Device->GetInstanceHandle(), &BufferCreateInfo, VULKAN_CPU_ALLOCATOR, &Buffer));

		VkMemoryRequirements MemReqs;
		VulkanRHI::vkGetBufferMemoryRequirements(Device->GetInstanceHandle(), Buffer, &MemReqs);
		Alignment = FMath::Max((uint32)MemReqs.alignment, Alignment);
		ensure(MemReqs.size >= BufferSize);

		uint32 MemoryTypeIndex;
		VERIFYVULKANRESULT(Device->GetMemoryManager().GetMemoryTypeFromProperties(MemReqs.memoryTypeBits, MemoryPropertyFlags, &MemoryTypeIndex));

		FDeviceMemoryAllocation* DeviceMemoryAllocation = Device->GetMemoryManager().Alloc(false, MemReqs.size, MemoryTypeIndex, nullptr, Priority, File, Line);
		VERIFYVULKANRESULT(VulkanRHI::vkBindBufferMemory(Device->GetInstanceHandle(), Buffer, DeviceMemoryAllocation->GetHandle(), 0));
		if (DeviceMemoryAllocation->CanBeMapped())
		{
			DeviceMemoryAllocation->Map(BufferSize, 0);
		}

		uint32 BufferId = 0;
		if (UseVulkanDescriptorCache())
		{
			BufferId = ++GVulkanBufferHandleIdCounter;
		}

		FBufferAllocation* BufferAllocation = new FBufferAllocation(this, DeviceMemoryAllocation, MemoryTypeIndex,
			MemoryPropertyFlags, MemReqs.alignment, Buffer, BufferId, BufferUsageFlags, PoolSize);
		UsedBufferAllocations[PoolSize].Add(BufferAllocation);

		return (FBufferSuballocation*)BufferAllocation->TryAllocate(Size, Alignment, File, Line);
	}

	void FResourceHeapManager::ReleaseBuffer(FBufferAllocation* BufferAllocation)
	{
		FScopeLock ScopeLock(&GResourceHeapLock);

		if (BufferAllocation->JoinFreeBlocks())
		{
			check(BufferAllocation->JoinFreeBlocks());
			UsedBufferAllocations[BufferAllocation->PoolSizeIndex].RemoveSingleSwap(BufferAllocation, false);
			BufferAllocation->FrameFreed = GFrameNumberRenderThread;
			FreeBufferAllocations[BufferAllocation->PoolSizeIndex].Add(BufferAllocation);
		}
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

		uint64 UsedBinnedTotal = 0;
		uint64 AllocBinnedTotal = 0;
		uint64 UsedLargeTotal = 0;
		uint64 AllocLargeTotal = 0;
		for (int32 PoolSizeIndex = 0; PoolSizeIndex < UE_ARRAY_COUNT(UsedBufferAllocations); PoolSizeIndex++)
		{
			TArray<FBufferAllocation*>& UsedAllocations = UsedBufferAllocations[PoolSizeIndex];
			TArray<FBufferAllocation*>& FreeAllocations = FreeBufferAllocations[PoolSizeIndex];
			if (PoolSizeIndex == (int32)EPoolSizes::SizesCount)
			{
				UE_LOG(LogVulkanRHI, Display, TEXT("Buffer of large size Allocations: %d Used / %d Free"), UsedAllocations.Num(), FreeAllocations.Num());
			}
			else
			{
				UE_LOG(LogVulkanRHI, Display, TEXT("Buffer of %d size Allocations: %d Used / %d Free"), PoolSizes[PoolSizeIndex], UsedAllocations.Num(), FreeAllocations.Num());
			}

			if (UsedAllocations.Num() > 0)
			{
				uint64 _UsedBinnedTotal = 0;
				uint64 _AllocBinnedTotal = 0;
				uint64 _UsedLargeTotal = 0;
				uint64 _AllocLargeTotal = 0;

				UE_LOG(LogVulkanRHI, Display, TEXT("Index  BufferHandle       DeviceMemoryHandle MemFlags BufferFlags #Suballocs #FreeChunks UsedSize/MaxSize"));
				for (int32 Index = 0; Index < UsedAllocations.Num(); ++Index)
				{
					FBufferAllocation* BA = UsedAllocations[Index];
					UE_LOG(LogVulkanRHI, Display, TEXT("%6d 0x%016llx 0x%016llx 0x%06x 0x%08x %6d   %6d        %d/%d"), Index, (void*)BA->Buffer, (void*)BA->MemoryAllocation->GetHandle(), BA->MemoryPropertyFlags, BA->BufferUsageFlags, BA->Suballocations.Num(), BA->FreeList.Num(), BA->UsedSize, BA->MaxSize);

					if (PoolSizeIndex == (int32)EPoolSizes::SizesCount)
					{
						_UsedLargeTotal += BA->UsedSize;
						_AllocLargeTotal += BA->MaxSize;
						UsedLargeTotal += BA->UsedSize;
						AllocLargeTotal += BA->MaxSize;
					}
					else
					{
						_UsedBinnedTotal += BA->UsedSize;
						_AllocBinnedTotal += BA->MaxSize;
						UsedBinnedTotal += BA->UsedSize;
						AllocBinnedTotal += BA->MaxSize;
					}
				}

				if (PoolSizeIndex == (int32)EPoolSizes::SizesCount)
				{
					UE_LOG(LogVulkanRHI, Display, TEXT(" Large Alloc Used/Max %d/%d %6.2f%%"), _UsedLargeTotal, _AllocLargeTotal, 100.0f * (float)_UsedLargeTotal / (float)_AllocLargeTotal);
				}
				else
				{
					UE_LOG(LogVulkanRHI, Display, TEXT(" Binned [%d] Alloc Used/Max %d/%d %6.2f%%"), PoolSizes[PoolSizeIndex], _UsedBinnedTotal, _AllocBinnedTotal, 100.0f * (float)_UsedBinnedTotal / (float)_AllocBinnedTotal);
				}
			}
		}

		UE_LOG(LogVulkanRHI, Display, TEXT("::Totals::"));
		UE_LOG(LogVulkanRHI, Display, TEXT("Large Alloc Used/Max %d/%d %.2f%%"), UsedLargeTotal, AllocLargeTotal, 100.0f * AllocLargeTotal > 0 ? (float)UsedLargeTotal / (float)AllocLargeTotal : 0.0f);
		UE_LOG(LogVulkanRHI, Display, TEXT("Binned Alloc Used/Max %d/%d %.2f%%"), UsedBinnedTotal, AllocBinnedTotal, AllocBinnedTotal > 0 ? 100.0f * (float)UsedBinnedTotal / (float)AllocBinnedTotal : 0.0f);
	}
#endif


	FBufferSuballocation::~FBufferSuballocation()
	{
		Owner->Release(this);
	}


	void FBufferSuballocation::Flush()
	{
		Owner->Flush(AlignedOffset, AllocationSize);
	}

	FCriticalSection FSubresourceAllocator::CS;

	bool FSubresourceAllocator::JoinFreeBlocks()
	{
		FScopeLock ScopeLock(&CS);
#if !UE_VK_MEMORY_JOIN_FREELIST_ON_THE_FLY
		FRange::JoinConsecutiveRanges(FreeList);
#endif

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
	
	FResourceSuballocation* FSubresourceAllocator::TryAllocate(uint32 InSize, uint32 InAlignment, const char* File, uint32 Line)
	{
		FScopeLock ScopeLock(&CS);
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
				FRange::AllocateFromEntry(FreeList, Index, AllocatedSize);

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

				LLM_TRACK_VULKAN_HIGH_LEVEL_ALLOC(NewSuballocation, InSize);

				LLM_TRACK_VULKAN_SPARE_MEMORY_GPU(-(int64)InSize);

				//PeakNumAllocations = FMath::Max(PeakNumAllocations, ResourceAllocations.Num());
				return NewSuballocation;
			}
		}

		return nullptr;
	}

	void FSubresourceAllocator::Flush(VkDeviceSize Offset, VkDeviceSize AllocationSize)
	{
		MemoryAllocation->FlushMappedMemory(Offset, AllocationSize);
	}


	void FBufferAllocation::Release(FBufferSuballocation* Suballocation)
	{
		{
			FScopeLock ScopeLock(&CS);
			Suballocations.RemoveSingleSwap(Suballocation, false);

			LLM_TRACK_VULKAN_HIGH_LEVEL_FREE(Suballocation);

			LLM_TRACK_VULKAN_SPARE_MEMORY_GPU((int64)Suballocation->RequestedSize);

			FRange NewFree;
			NewFree.Offset = Suballocation->AllocationOffset;
			NewFree.Size = Suballocation->AllocationSize;
			check(NewFree.Offset <= GetMaxSize());
			check(NewFree.Offset + NewFree.Size <= GetMaxSize());

			FRange::Add(FreeList, NewFree);

			UsedSize -= Suballocation->AllocationSize;
			check(UsedSize >= 0);
		}

		Owner->ReleaseBuffer(this);
	}

	void FBufferAllocation::Destroy(FVulkanDevice* Device)
	{
		// Does not need to go in the deferred deletion queue
		VulkanRHI::vkDestroyBuffer(Device->GetInstanceHandle(), Buffer, VULKAN_CPU_ALLOCATOR);
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
		VulkanRHI::vkDestroyBuffer(Device->GetInstanceHandle(), Buffer, VULKAN_CPU_ALLOCATOR);
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

	FStagingBuffer* FStagingManager::AcquireBuffer(uint32 Size, VkBufferUsageFlags InUsageFlags, VkMemoryPropertyFlagBits InMemoryReadFlags)
	{
#if VULKAN_ENABLE_AGGRESSIVE_STATS
		SCOPE_CYCLE_COUNTER(STAT_VulkanStagingBuffer);
#endif
		LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanStagingBuffers);
		if (InMemoryReadFlags == VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
		{
			uint64 NonCoherentAtomSize = (uint64)Device->GetLimits().nonCoherentAtomSize;
			Size = AlignArbitrary(Size, NonCoherentAtomSize);
		}

		// Add both source and dest flags
		if ((InUsageFlags & (VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)) != 0)
		{
			InUsageFlags |= (VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
		}

		//#todo-rco: Better locking!
		{
			FScopeLock Lock(&GStagingLock);
			for (int32 Index = 0; Index < FreeStagingBuffers.Num(); ++Index)
			{
				FFreeEntry& FreeBuffer = FreeStagingBuffers[Index];
				if (FreeBuffer.StagingBuffer->GetSize() == Size && FreeBuffer.StagingBuffer->MemoryReadFlags == InMemoryReadFlags)
				{
					FStagingBuffer* Buffer = FreeBuffer.StagingBuffer;
					FreeStagingBuffers.RemoveAtSwap(Index, 1, false);
					UsedStagingBuffers.Add(Buffer);
#if VULKAN_MEMORY_TRACK_CALLSTACK
					CaptureCallStack(Buffer->Callstack, 1);
#endif
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

		VERIFYVULKANRESULT(VulkanRHI::vkCreateBuffer(VulkanDevice, &StagingBufferCreateInfo, VULKAN_CPU_ALLOCATOR, &StagingBuffer->Buffer));

		VkMemoryRequirements MemReqs;
		VulkanRHI::vkGetBufferMemoryRequirements(VulkanDevice, StagingBuffer->Buffer, &MemReqs);
		ensure(MemReqs.size >= Size);

		// Set minimum alignment to 16 bytes, as some buffers are used with CPU SIMD instructions
		MemReqs.alignment = FMath::Max<VkDeviceSize>(16, MemReqs.alignment);
		static const bool bIsAmd = Device->GetDeviceProperties().vendorID == 0x1002;
		if (InMemoryReadFlags == VK_MEMORY_PROPERTY_HOST_CACHED_BIT || bIsAmd)
		{
			uint64 NonCoherentAtomSize = (uint64)Device->GetLimits().nonCoherentAtomSize;
			MemReqs.alignment = AlignArbitrary(MemReqs.alignment, NonCoherentAtomSize);
		}

		VkMemoryPropertyFlags readTypeFlags = InMemoryReadFlags;

		StagingBuffer->ResourceAllocation = Device->GetResourceHeapManager().AllocateBufferMemory(MemReqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | readTypeFlags, __FILE__, __LINE__);
		StagingBuffer->MemoryReadFlags = InMemoryReadFlags;
		StagingBuffer->BufferSize = Size;
		StagingBuffer->ResourceAllocation->BindBuffer(Device, StagingBuffer->Buffer);

		{
			FScopeLock Lock(&GStagingLock);
			UsedStagingBuffers.Add(StagingBuffer);
			UsedMemory += StagingBuffer->GetSize();
			PeakUsedMemory = FMath::Max(UsedMemory, PeakUsedMemory);
		}

#if VULKAN_MEMORY_TRACK_CALLSTACK
		CaptureCallStack(StagingBuffer->Callstack, 1);
#endif
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
			FPendingItemsPerCmdBuffer::FPendingItems* ItemsForFence = ItemsForCmdBuffer->FindOrAddItemsForFence(CmdBuffer->GetFenceSignaledCounterA());
			check(StagingBuffer);
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
		UE_LOG(LogVulkanRHI, Display, TEXT("Used   BufferHandle       ResourceAllocation Size"));
		for (int32 Index = 0; Index < UsedStagingBuffers.Num(); ++Index)
		{
			FStagingBuffer* Buffer = UsedStagingBuffers[Index];
			UE_LOG(LogVulkanRHI, Display, TEXT("%6d 0x%016llx 0x%016llx %6d"), Index, (void*)Buffer->GetHandle(), (void*)Buffer->ResourceAllocation->GetHandle(), Buffer->BufferSize);
		}

		UE_LOG(LogVulkanRHI, Display, TEXT("Pending CmdBuffer   Fence   BufferHandle    ResourceAllocation Size"));
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
					UE_LOG(LogVulkanRHI, Display, TEXT("                   0x%016llx 0x%016llx %6d"), (void*)Buffer->GetHandle(), (void*)Buffer->ResourceAllocation->GetHandle(), Buffer->BufferSize);
				}
			}
		}

		UE_LOG(LogVulkanRHI, Display, TEXT("Free   BufferHandle     ResourceAllocation Size"));
		for (int32 Index = 0; Index < FreeStagingBuffers.Num(); ++Index)
		{
			FFreeEntry& Entry = FreeStagingBuffers[Index];
			UE_LOG(LogVulkanRHI, Display, TEXT("%6d 0x%016llx 0x%016llx %6d"), Index, (void*)Entry.StagingBuffer->GetHandle(), (void*)Entry.StagingBuffer->ResourceAllocation->GetHandle(), Entry.StagingBuffer->BufferSize);
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
				if (bImmediately || PendingItems.FenceCounter < EntriesPerCmdBuffer.CmdBuffer->GetFenceSignaledCounterB())
				{
					for (int32 ResourceIndex = 0; ResourceIndex < PendingItems.Resources.Num(); ++ResourceIndex)
					{
						check(PendingItems.Resources[ResourceIndex]);
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
					UsedMemory -= Entry.StagingBuffer->GetSize();
					Entry.StagingBuffer->Destroy(Device);
					delete Entry.StagingBuffer;
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
		VERIFYVULKANRESULT(VulkanRHI::vkCreateFence(InDevice->GetInstanceHandle(), &Info, VULKAN_CPU_ALLOCATOR, &Handle));
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
		VulkanRHI::vkDestroyFence(Device->GetInstanceHandle(), Fence->GetHandle(), VULKAN_CPU_ALLOCATOR);
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
		VERIFYVULKANRESULT(VulkanRHI::vkCreateEvent(InDevice->GetInstanceHandle(), &Info, VULKAN_CPU_ALLOCATOR, &Handle));
	}

	FGPUEvent::~FGPUEvent()
	{
		Device->GetDeferredDeletionQueue().EnqueueResource(VulkanRHI::FDeferredDeletionQueue2::EType::Event, Handle);
	}


	/// Note: FDeferredDeletionQueue is deprecated internally, and replaced by FDeferredDeletionQueue2. it is left only for patch compatibility, and should -not- be used
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
		Entry.StructureType = Type;
		Queue->GetLastSubmittedInfo(Entry.CmdBuffer, Entry.FenceCounter);
		Entry.FrameNumber = GVulkanRHIDeletionFrameNumber;

		Entry.Handle = Handle;

		{
			FScopeLock ScopeLock(&CS);

#if VULKAN_HAS_DEBUGGING_ENABLED
			FEntry* ExistingEntry = Entries.FindByPredicate([&](const FEntry& InEntry)
				{ 
					return InEntry.Handle == Entry.Handle; 
				});
			checkf(ExistingEntry == nullptr, TEXT("Attempt to double-delete resource, FDeferredDeletionQueue::EType: %d, Handle: %llu"), (int32)Type, Handle);
#endif

			Entries.Add(Entry);
		}
	}

	void FDeferredDeletionQueue::EnqueueResourceAllocation(TRefCountPtr<VulkanRHI::FOldResourceAllocation> ResourceAllocation)
	{
		FVulkanQueue* Queue = Device->GetGraphicsQueue();

		FEntry Entry;
		Entry.StructureType = EType::ResourceAllocation;
		Queue->GetLastSubmittedInfo(Entry.CmdBuffer, Entry.FenceCounter);
		Entry.FrameNumber = GVulkanRHIDeletionFrameNumber;

		Entry.Handle = VK_NULL_HANDLE;
		Entry.ResourceAllocation = ResourceAllocation;

		{
			FScopeLock ScopeLock(&CS);

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
				(Entry->CmdBuffer == nullptr || Entry->FenceCounter < Entry->CmdBuffer->GetFenceSignaledCounterC()))
				)
			{
				switch (Entry->StructureType)
				{
#define VKSWITCH(Type, ...)	case EType::Type: __VA_ARGS__; VulkanRHI::vkDestroy##Type(DeviceHandle, (Vk##Type)Entry->Handle, VULKAN_CPU_ALLOCATOR); break
				VKSWITCH(RenderPass);
				VKSWITCH(Buffer);
				VKSWITCH(BufferView);
				VKSWITCH(Image);
				VKSWITCH(ImageView);
				VKSWITCH(Pipeline, DEC_DWORD_STAT(STAT_VulkanNumPSOs));
				VKSWITCH(PipelineLayout);
				VKSWITCH(Framebuffer);
				VKSWITCH(DescriptorSetLayout);
				VKSWITCH(Sampler);
				VKSWITCH(Semaphore);
				VKSWITCH(ShaderModule);
				VKSWITCH(Event);
#undef VKSWITCH
				case EType::ResourceAllocation:
					Entry->ResourceAllocation.SafeRelease();
					break;
				default:
					check(0);
					break;
				}
				Entries.RemoveAtSwap(Index, 1, false);
			}
		}
	}

	void FDeferredDeletionQueue::OnCmdBufferDeleted(FVulkanCmdBuffer* DeletedCmdBuffer)
	{
		FScopeLock ScopeLock(&CS);
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			FEntry& Entry = Entries[Index];
			if (Entry.CmdBuffer == DeletedCmdBuffer)
			{
				Entry.CmdBuffer = nullptr;
			}
		}
	}



	FDeferredDeletionQueue2::FDeferredDeletionQueue2(FVulkanDevice* InDevice)
		: FDeviceChild(InDevice)
	{
	}

	FDeferredDeletionQueue2::~FDeferredDeletionQueue2()
	{
		check(Entries.Num() == 0);
	}

	void FDeferredDeletionQueue2::EnqueueGenericResource(EType Type, uint64 Handle)
	{
		FVulkanQueue* Queue = Device->GetGraphicsQueue();

		FEntry Entry;
		Entry.SubAllocationDirect = 0;
		Entry.StructureType = Type;
		Queue->GetLastSubmittedInfo(Entry.CmdBuffer, Entry.FenceCounter);
		Entry.FrameNumber = GVulkanRHIDeletionFrameNumber;

		Entry.Handle = Handle;
		{
			FScopeLock ScopeLock(&CS);

#if VULKAN_HAS_DEBUGGING_ENABLED
			FEntry* ExistingEntry = Entries.FindByPredicate([&](const FEntry& InEntry)
				{
					return InEntry.Handle == Entry.Handle;
				});
			checkf(ExistingEntry == nullptr, TEXT("Attempt to double-delete resource, FDeferredDeletionQueue::EType: %d, Handle: %llu"), (int32)Type, Handle);
#endif

			Entries.Add(Entry);
		}
	}

	void FDeferredDeletionQueue2::EnqueueResourceAllocation(TRefCountPtr<VulkanRHI::FOldResourceAllocation> ResourceAllocation)
	{
		FVulkanQueue* Queue = Device->GetGraphicsQueue();

		FEntry Entry;
		Entry.SubAllocationDirect = 0;
		Entry.StructureType = EType::ResourceAllocation;
		Queue->GetLastSubmittedInfo(Entry.CmdBuffer, Entry.FenceCounter);
		Entry.FrameNumber = GVulkanRHIDeletionFrameNumber;

		Entry.Handle = VK_NULL_HANDLE;
		Entry.ResourceAllocation = ResourceAllocation;

		{
			FScopeLock ScopeLock(&CS);

			Entries.Add(Entry);
		}
	}
	void FDeferredDeletionQueue2::EnqueueBufferSuballocation(TRefCountPtr<VulkanRHI::FBufferSuballocation> SubAllocation)
	{
		FVulkanQueue* Queue = Device->GetGraphicsQueue();

		FEntry Entry;
		Entry.SubAllocationDirect = 0;
		Entry.StructureType = EType::BufferSuballocation;
		Queue->GetLastSubmittedInfo(Entry.CmdBuffer, Entry.FenceCounter);
		Entry.FrameNumber = GVulkanRHIDeletionFrameNumber;

		Entry.Handle = VK_NULL_HANDLE;
		Entry.SubAllocation = SubAllocation;

		{
			FScopeLock ScopeLock(&CS);

			Entries.Add(Entry);
		}
	}
	void FDeferredDeletionQueue2::EnqueueBufferSuballocationDirect(FBufferSuballocation* SubAllocation)
	{
		FVulkanQueue* Queue = Device->GetGraphicsQueue();

		FEntry Entry;
		Entry.SubAllocationDirect = 0;
		Entry.StructureType = EType::BufferSuballocation;
		Queue->GetLastSubmittedInfo(Entry.CmdBuffer, Entry.FenceCounter);
		Entry.FrameNumber = GVulkanRHIDeletionFrameNumber;

		Entry.Handle = VK_NULL_HANDLE;
		Entry.SubAllocationDirect = SubAllocation;

		{
			FScopeLock ScopeLock(&CS);

			Entries.Add(Entry);
		}
	}


	void FDeferredDeletionQueue2::ReleaseResources(bool bDeleteImmediately)
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
					(Entry->CmdBuffer == nullptr || Entry->FenceCounter < Entry->CmdBuffer->GetFenceSignaledCounterC()))
				)
			{
				switch (Entry->StructureType)
				{
#define VKSWITCH(Type, ...)	case EType::Type: __VA_ARGS__; VulkanRHI::vkDestroy##Type(DeviceHandle, (Vk##Type)Entry->Handle, VULKAN_CPU_ALLOCATOR); break
					VKSWITCH(RenderPass);
					VKSWITCH(Buffer);
					VKSWITCH(BufferView);
					VKSWITCH(Image);
					VKSWITCH(ImageView);
					VKSWITCH(Pipeline, DEC_DWORD_STAT(STAT_VulkanNumPSOs));
					VKSWITCH(PipelineLayout);
					VKSWITCH(Framebuffer);
					VKSWITCH(DescriptorSetLayout);
					VKSWITCH(Sampler);
					VKSWITCH(Semaphore);
					VKSWITCH(ShaderModule);
					VKSWITCH(Event);
#undef VKSWITCH
				case EType::BufferSuballocation:
				case EType::ResourceAllocation:
					Entry->ResourceAllocation.SafeRelease();
					Entry->SubAllocation.SafeRelease();
					if (Entry->SubAllocationDirect)
					{
						delete Entry->SubAllocationDirect;
						Entry->SubAllocationDirect = 0;
					}
					break;


				default:
					check(0);
					break;
				}
				Entries.RemoveAtSwap(Index, 1, false);
			}
		}
	}




	void FDeferredDeletionQueue2::OnCmdBufferDeleted(FVulkanCmdBuffer* DeletedCmdBuffer)
	{
		FScopeLock ScopeLock(&CS);
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			FEntry& Entry = Entries[Index];
			if (Entry.CmdBuffer == DeletedCmdBuffer)
			{
				Entry.CmdBuffer = nullptr;
			}
		}
	}

	FTempFrameAllocationBuffer::FTempFrameAllocationBuffer(FVulkanDevice* InDevice)
		: FDeviceChild(InDevice)
		, BufferIndex(0)
	{
		for (int32 Index = 0; Index < NUM_BUFFERS; ++Index)
		{
			INC_MEMORY_STAT_BY(STAT_VulkanTempFrameAllocationBuffer, ALLOCATION_SIZE);
			Entries[Index].InitBuffer(Device, ALLOCATION_SIZE);
		}
	}

	FTempFrameAllocationBuffer::~FTempFrameAllocationBuffer()
	{
		Destroy();
	}

	void FTempFrameAllocationBuffer::FFrameEntry::InitBuffer(FVulkanDevice* InDevice, uint32 InSize)
	{
		LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanFrameTemp);
		Size = InSize;
		PeakUsed = 0;
		BufferSuballocation = InDevice->GetResourceHeapManager().AllocateBuffer(InSize,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | 
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | 
			VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			__FILE__, __LINE__);
		MappedData = (uint8*)BufferSuballocation->GetMappedPointer();
		CurrentData = MappedData;
	}

	void FTempFrameAllocationBuffer::Destroy()
	{
		for (int32 Index = 0; Index < NUM_BUFFERS; ++Index)
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
			OutInfo.Size = InSize;
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
		DEC_MEMORY_STAT_BY(STAT_VulkanTempFrameAllocationBuffer, Entries[BufferIndex].BufferSuballocation->GetSize());
		INC_MEMORY_STAT_BY(STAT_VulkanTempFrameAllocationBuffer, NewSize);
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
		BufferIndex = (BufferIndex + 1) % NUM_BUFFERS;
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

		if (GVulkanDelayAcquireImage != EDelayAcquireImageType::DelayAcquire)
		{
			// special handling for VK_IMAGE_LAYOUT_PRESENT_SRC_KHR (otherwise Mali devices flicker)
			if (Source == EImageLayoutBarrier::Present)
			{
				SourceStages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			}
			else if (Dest == EImageLayoutBarrier::Present)
			{
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
		SemaphoreHandle(VK_NULL_HANDLE),
		bExternallyOwned(false)
	{
		// Create semaphore
		VkSemaphoreCreateInfo CreateInfo;
		ZeroVulkanStruct(CreateInfo, VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
		//CreateInfo.pNext = nullptr;
		//CreateInfo.flags = 0;
		VERIFYVULKANRESULT(VulkanRHI::vkCreateSemaphore(Device.GetInstanceHandle(), &CreateInfo, VULKAN_CPU_ALLOCATOR, &SemaphoreHandle));
	}

	FSemaphore::FSemaphore(FVulkanDevice& InDevice, const VkSemaphore& InExternalSemaphore) :
		Device(InDevice),
		SemaphoreHandle(InExternalSemaphore),
		bExternallyOwned(true)
	{}

	FSemaphore::~FSemaphore()
	{
		check(SemaphoreHandle != VK_NULL_HANDLE);
		if (!bExternallyOwned)
		{
			Device.GetDeferredDeletionQueue().EnqueueResource(VulkanRHI::FDeferredDeletionQueue2::EType::Semaphore, SemaphoreHandle);
		}
		SemaphoreHandle = VK_NULL_HANDLE;
	}
}


#if VULKAN_CUSTOM_MEMORY_MANAGER_ENABLED
namespace VulkanRHI
{
	VkAllocationCallbacks GAllocationCallbacks;
}
static FCriticalSection GMemMgrCS;
static FVulkanCustomMemManager GVulkanInstrumentedMemMgr;
//VkAllocationCallbacks GDescriptorAllocationCallbacks;


FVulkanCustomMemManager::FVulkanCustomMemManager()
{
	VulkanRHI::GAllocationCallbacks.pUserData = nullptr;
	VulkanRHI::GAllocationCallbacks.pfnAllocation = (PFN_vkAllocationFunction)&FVulkanCustomMemManager::Alloc;
	VulkanRHI::GAllocationCallbacks.pfnReallocation = (PFN_vkReallocationFunction)&FVulkanCustomMemManager::Realloc;
	VulkanRHI::GAllocationCallbacks.pfnFree = (PFN_vkFreeFunction)&FVulkanCustomMemManager::Free;
	VulkanRHI::GAllocationCallbacks.pfnInternalAllocation = (PFN_vkInternalAllocationNotification)&FVulkanCustomMemManager::InternalAllocationNotification;
	VulkanRHI::GAllocationCallbacks.pfnInternalFree = (PFN_vkInternalFreeNotification)&FVulkanCustomMemManager::InternalFreeNotification;
}

inline FVulkanCustomMemManager::FType& FVulkanCustomMemManager::GetType(void* UserData, VkSystemAllocationScope AllocScope)
{
	return GVulkanInstrumentedMemMgr.Types[AllocScope];
}

void* FVulkanCustomMemManager::Alloc(void* UserData, size_t Size, size_t Alignment, VkSystemAllocationScope AllocScope)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanDriverMemoryCPU);
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
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanDriverMemoryCPU);
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
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanDriverMemoryCPU);
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
