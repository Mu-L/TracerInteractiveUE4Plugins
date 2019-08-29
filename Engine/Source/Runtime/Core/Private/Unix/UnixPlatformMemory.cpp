// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	UnixPlatformMemory.cpp: Unix platform memory functions
=============================================================================*/

#include "Unix/UnixPlatformMemory.h"
#include "Misc/AssertionMacros.h"
#include "Math/NumericLimits.h"
#include "Math/UnrealMathUtility.h"
#include "Templates/UnrealTemplate.h"
#include "Containers/UnrealString.h"
#include "Logging/LogMacros.h"
#include "HAL/MallocAnsi.h"
#include "HAL/MallocJemalloc.h"
#include "HAL/MallocBinned.h"
#include "HAL/MallocBinned2.h"
#include "HAL/MallocReplayProxy.h"
#include "HAL/MallocStomp.h"
#if PLATFORM_FREEBSD
	#include <kvm.h>
#else
	#include <sys/sysinfo.h>
#endif
#include <sys/file.h>
#include <sys/mman.h>

#include "GenericPlatform/OSAllocationPool.h"
#include "Misc/ScopeLock.h"

// do not do a root privilege check on non-x86-64 platforms (assume an embedded device)
#if defined(_M_X64) || defined(__x86_64__) || defined (__amd64__) 
	#define UE4_DO_ROOT_PRIVILEGE_CHECK	 1
#else
	#define UE4_DO_ROOT_PRIVILEGE_CHECK	 0
#endif // defined(_M_X64) || defined(__x86_64__) || defined (__amd64__) 

// Set rather to use BinnedMalloc2 for binned malloc, can be overridden below
#define USE_MALLOC_BINNED2 (1)

// Used in UnixPlatformStackwalk for now. Once the old crash symbolicator is gone remove this
bool CORE_API GUseNewCrashSymbolicator = true;
bool CORE_API GSuppressDwarfParsing    = false;

// Used in UnixPlatformStackwalk to skip the crash handling callstack frames.
bool CORE_API GFullCrashCallstack = false;

// Used to set the maximum number of file mappings.
#if UE_EDITOR
int32 CORE_API GMaxNumberFileMappingCache = 10000;
#else
int32 CORE_API GMaxNumberFileMappingCache = 100;
#endif
namespace
{
	// The max allowed to be set for the caching
	const int32 MaximumAllowedMaxNumFileMappingCache = 1000000;
}

void FUnixPlatformMemory::Init()
{
	FGenericPlatformMemory::Init();

	const FPlatformMemoryConstants& MemoryConstants = FPlatformMemory::GetConstants();
	UE_LOG(LogInit, Log, TEXT(" - Physical RAM available (not considering process quota): %d GB (%lu MB, %lu KB, %lu bytes)"), 
		MemoryConstants.TotalPhysicalGB, 
		MemoryConstants.TotalPhysical / ( 1024ULL * 1024ULL ), 
		MemoryConstants.TotalPhysical / 1024ULL, 
		MemoryConstants.TotalPhysical);
}

class FMalloc* FUnixPlatformMemory::BaseAllocator()
{
#if UE4_DO_ROOT_PRIVILEGE_CHECK
	// This function gets executed very early, way before main() (because global constructors will allocate memory).
	// This makes it ideal, if unobvious, place for a root privilege check.
	if (geteuid() == 0)
	{
		fprintf(stderr, "Refusing to run with the root privileges.\n");
		FPlatformMisc::RequestExit(true);
		// unreachable
		return nullptr;
	}
#endif // UE4_DO_ROOT_PRIVILEGE_CHECK

#if UE_USE_MALLOC_REPLAY_PROXY
	bool bAddReplayProxy = false;
#endif // UE_USE_MALLOC_REPLAY_PROXY

	if (USE_MALLOC_BINNED2)
	{
		AllocatorToUse = EMemoryAllocatorToUse::Binned2;
	}
	else 
	{
		AllocatorToUse = EMemoryAllocatorToUse::Binned;
	}
	
	if (FORCE_ANSI_ALLOCATOR)
	{
		AllocatorToUse = EMemoryAllocatorToUse::Ansi;
	}
	else
	{
		// Allow overriding on the command line.
		// We get here before main due to global ctors, so need to do some hackery to get command line args
		if (FILE* CmdLineFile = fopen("/proc/self/cmdline", "r"))
		{
			char * Arg = nullptr;
			size_t Size = 0;
			while(getdelim(&Arg, &Size, 0, CmdLineFile) != -1)
			{
#if PLATFORM_SUPPORTS_JEMALLOC
				if (FCStringAnsi::Stricmp(Arg, "-jemalloc") == 0)
				{
					AllocatorToUse = EMemoryAllocatorToUse::Jemalloc;
					break;
				}
#endif // PLATFORM_SUPPORTS_JEMALLOC
				if (FCStringAnsi::Stricmp(Arg, "-ansimalloc") == 0)
				{
					AllocatorToUse = EMemoryAllocatorToUse::Ansi;
					break;
				}

				if (FCStringAnsi::Stricmp(Arg, "-binnedmalloc") == 0)
				{
					AllocatorToUse = EMemoryAllocatorToUse::Binned;
					break;
				}

				if (FCStringAnsi::Stricmp(Arg, "-binnedmalloc2") == 0)
				{
					AllocatorToUse = EMemoryAllocatorToUse::Binned2;
					break;
				}
				if (FCStringAnsi::Stricmp(Arg, "-oldcrashsymbolicator") == 0)
				{
					GUseNewCrashSymbolicator = false;
				}

				if (FCStringAnsi::Stricmp(Arg, "-nodwarf") == 0)
				{
					GSuppressDwarfParsing = true;
				}

				if (FCStringAnsi::Stricmp(Arg, "-fullcrashcallstack") == 0)
				{
					GFullCrashCallstack = true;
				}

				const char FileMapCacheCmd[] = "-filemapcachesize=";
				if (const char* Cmd = FCStringAnsi::Stristr(Arg, FileMapCacheCmd))
				{
					int32 Max = FCStringAnsi::Atoi(Cmd + sizeof(FileMapCacheCmd) - 1);
					GMaxNumberFileMappingCache = FMath::Clamp(Max, 0, MaximumAllowedMaxNumFileMappingCache);
				}

#if UE_USE_MALLOC_REPLAY_PROXY
				if (FCStringAnsi::Stricmp(Arg, "-mallocsavereplay") == 0)
				{
					bAddReplayProxy = true;
				}
#endif // UE_USE_MALLOC_REPLAY_PROXY
#if WITH_MALLOC_STOMP
				if (FCStringAnsi::Stricmp(Arg, "-stompmalloc") == 0)
				{
					AllocatorToUse = EMemoryAllocatorToUse::Stomp;
					break;
				}
#endif // WITH_MALLOC_STOMP
			}
			free(Arg);
			fclose(CmdLineFile);
		}
	}

	FMalloc * Allocator = NULL;

	switch (AllocatorToUse)
	{
	case EMemoryAllocatorToUse::Ansi:
		Allocator = new FMallocAnsi();
		break;

#if WITH_MALLOC_STOMP
	case EMemoryAllocatorToUse::Stomp:
		Allocator = new FMallocStomp();
		break;
#endif

#if PLATFORM_SUPPORTS_JEMALLOC
	case EMemoryAllocatorToUse::Jemalloc:
		Allocator = new FMallocJemalloc();
		break;
#endif // PLATFORM_SUPPORTS_JEMALLOC

	case EMemoryAllocatorToUse::Binned2:
		Allocator = new FMallocBinned2();
		break;

	default:	// intentional fall-through
	case EMemoryAllocatorToUse::Binned:
		Allocator = new FMallocBinned(FPlatformMemory::GetConstants().BinnedPageSize & MAX_uint32, 0x100000000);
		break;
	}

#if UE_BUILD_DEBUG
	printf("Using %ls.\n", Allocator ? Allocator->GetDescriptiveName() : TEXT("NULL allocator! We will probably crash right away"));
#endif // UE_BUILD_DEBUG

#if UE_USE_MALLOC_REPLAY_PROXY
	if (bAddReplayProxy)
	{
		Allocator = new FMallocReplayProxy(Allocator);
	}
#endif // UE_USE_MALLOC_REPLAY_PROXY

	return Allocator;
}

bool FUnixPlatformMemory::PageProtect(void* const Ptr, const SIZE_T Size, const bool bCanRead, const bool bCanWrite)
{
	int32 ProtectMode;
	if (bCanRead && bCanWrite)
	{
		ProtectMode = PROT_READ | PROT_WRITE;
	}
	else if (bCanRead)
	{
		ProtectMode = PROT_READ;
	}
	else if (bCanWrite)
	{
		ProtectMode = PROT_WRITE;
	}
	else
	{
		ProtectMode = PROT_NONE;
	}
	return mprotect(Ptr, Size, ProtectMode) == 0;
}

#if UE4_POOL_BAFO_ALLOCATIONS
namespace UnixMemoryPool
{
	enum
	{
	#if UE_SERVER
		LargestPoolSize = 65536,
	#else
		LargestPoolSize = 32 * 1024 * 1024,
	#endif // UE_SERVER

		RequiredAlignment = 65536,	// should match BinnedPageSize
		ExtraSizeToAllocate = 60 * 1024,	// BinnedPageSize - SystemPageSize (4KB on most platforms)
	};

	/** Table used to describe an array of pools.
	 *	Format:
	 *	  Each entry is two int32, one is block size in bytes, another is number of such blocks in pool.
	 *    Block size should be divisible by RequiredAlignment
	 *	  -1 for the first number is the end marker.
	 *	  Entries should be sorted ascending by block size.
	 */
	int32 PoolTable[] =
	{
	#if UE_SERVER
		// 1GB of 64K blocks
		65536, 16384,
	#else
		// 512 MB of 64K blocks
		65536, 8192,
		// 256 MB of 256K blocks
		262144, 1024,
		// 256 MB of 1MB blocks
		1024 * 1024, 256,
		// 192 MB of 8MB blocks
		8 * 1024 * 1024, 24,
		// 192 MB of 32MB blocks
		UnixMemoryPool::LargestPoolSize, 6,
	#endif // UE_SERVER
		-1
	};

	/** Reserves the address space (mmap on Unix, VirtualAlloc(MEM_RESERVE) on Windows. Failure is fatal. */
	bool ReserveAddressRange(void** OutReturnedPointer, SIZE_T Size)
	{
		void* Ptr = mmap(nullptr, Size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		if (Ptr == MAP_FAILED)
		{
			const int ErrNo = errno;
			UE_LOG(LogHAL, Fatal, TEXT("mmap(len=%llu) failed with errno = %d (%s)"), (uint64)Size,
				ErrNo, StringCast< TCHAR >(strerror(ErrNo)).Get());
			// unreachable
			return false;
		}

		*OutReturnedPointer = Ptr;
		return true;
	}

	/**
	* Frees the address space (munmap on Unix, VirtualFree() on Windows.
	* To be compatible with all platforms, Size should exactly match the size requested in ReserveAddressRange,
	* and Address should be the same as the pointer returned from RAR. Number of RAR/FAR calls should match, too.
	*/
	bool FreeAddressRange(void* Address, SIZE_T Size)
	{
		if (munmap(Address, Size) != 0)
		{
			const int ErrNo = errno;
			UE_LOG(LogHAL, Fatal, TEXT("munmap(addr=%p, len=%llu) failed with errno = %d (%s)"), Address, (uint64)Size,
				ErrNo, StringCast< TCHAR >(strerror(ErrNo)).Get());
			// unreachable
			return false;
		}
		return true;
	}

	/** Let the OS know that we need this range to be backed by physical RAM. */
	bool CommitAddressRange(void* AddrStart, SIZE_T Size)
	{
		return madvise(AddrStart, Size, MADV_WILLNEED) == 0;
	}

	/** Let the OS know that we the RAM pages backing this address range can be evicted. */
	bool EvictAddressRange(void* AddrStart, SIZE_T Size)
	{
		return madvise(AddrStart, Size, MADV_DONTNEED) == 0;
	}

	typedef TMemoryPoolArray<
		&ReserveAddressRange,
		&FreeAddressRange,
		&CommitAddressRange,
		&EvictAddressRange,
		RequiredAlignment,
		ExtraSizeToAllocate >
		TUnixMemoryPoolArray;

	/**
	 * This function tries to scale the pool table according to the available memory.
	 * Why scale the pool: with new BAFO behavior, it is rather easy to run into
	 * limit of VMAs (mmaps), which is about 64k mappings by default. Pool size should thus
	 * be adequate to hold most of the actually used memory, which is specially important for the editor (and cooker).
	 */
	int32* ScalePoolTable(int32* InOutPoolTable)
	{
		uint64 PoolSize = 0;
		uint64 MaxPooledAllocs = 0;
		for (int32* PoolPtr = InOutPoolTable; *PoolPtr != -1; PoolPtr += 2)
		{
			uint64 BlockSize = static_cast<uint64>(PoolPtr[0]);
			uint64 NumBlocks = static_cast<uint64>(PoolPtr[1]);
			PoolSize += (BlockSize * NumBlocks);
			MaxPooledAllocs += NumBlocks;
		}

		uint64 TotalPhysicalMemory = FPlatformMemory::GetConstants().TotalPhysical;
		uint64 DesiredPoolSize = 0;

		// do not scale up for a non-editor
		if (UE_EDITOR && PoolSize < TotalPhysicalMemory)
		{
			// scale up it so it is roughly 25% of total physical memory
			DesiredPoolSize = TotalPhysicalMemory / 4;
		}
		else if (PoolSize >= TotalPhysicalMemory)
		{
			// scale down to try to fit roughly 50% of the total physical memory
			DesiredPoolSize = TotalPhysicalMemory / 2;
		}

		double Multiplier = FMath::Max(static_cast<double>(DesiredPoolSize) / static_cast<double>(PoolSize), 0.0);

		// if we need to scale to a desired pool size
		if (DesiredPoolSize > 0)
		{
			PoolSize = 0;
			MaxPooledAllocs = 0;
			for (int32* PoolPtr = InOutPoolTable; *PoolPtr != -1; PoolPtr += 2)
			{
				uint64 BlockSize = static_cast<uint64>(PoolPtr[0]);
				PoolPtr[1] = FMath::Max(static_cast<uint32>(PoolPtr[1] * Multiplier), 1u);
				uint64 NumBlocks = static_cast<uint64>(PoolPtr[1]);
				PoolSize += (BlockSize * NumBlocks);
				MaxPooledAllocs += NumBlocks;
			}
		}

#if UE_BUILD_DEBUG
		printf("Pooling OS allocations (pool size: %llu MB, maximum allocations: %llu).\n", PoolSize / (1024ULL * 1024ULL), MaxPooledAllocs);
#endif // UE_BUILD_DEBUG

		return InOutPoolTable;
	}

	TUnixMemoryPoolArray& GetPoolArray()
	{
		static TUnixMemoryPoolArray PoolArray(ScalePoolTable(UnixMemoryPool::PoolTable));
		return PoolArray;
	}
}


FCriticalSection* GetGlobalUnixMemPoolLock()
{
	static FCriticalSection MemPoolLock;
	return &MemPoolLock;
};

#endif // UE4_POOL_BAFO_ALLOCATIONS

void* FUnixPlatformMemory::BinnedAllocFromOS(SIZE_T Size)
{
#if UE4_POOL_BAFO_ALLOCATIONS
	FScopeLock Lock(GetGlobalUnixMemPoolLock());

	UnixMemoryPool::TUnixMemoryPoolArray& PoolArray = UnixMemoryPool::GetPoolArray();
	void* RetVal = PoolArray.Allocate(Size);
	if (LIKELY(RetVal))
	{
		LLM(FLowLevelMemTracker::Get().OnLowLevelAlloc(ELLMTracker::Platform, RetVal, Size));
		return RetVal;
	}
	// otherwise, let generic BAFO deal with it
#endif // UE4_POOL_BAFO_ALLOCATIONS

#if UE4_POOL_BAFO_ALLOCATIONS_DEBUG_OOM
	// only store BAFO allocs
	if (LIKELY(AllocationHistogram::CurAlloc < AllocationHistogram::MaxAllocs))
	{
		AllocationHistogram::Sizes[AllocationHistogram::CurAlloc++] = Size;
	}
#endif // UE4_POOL_BAFO_ALLOCATIONS_DEBUG_OOM

	void* Ret = FGenericPlatformMemory::BinnedAllocFromOS(Size);

#if UE4_POOL_BAFO_ALLOCATIONS_DEBUG_OOM
	if (Ret == nullptr)
	{
		AllocationHistogram::PrintDebugInfo();
		UnixMemoryPool::GetPoolArray().PrintDebugInfo();
		for (;;) {};	// hang on here so we can attach the debugger and inspect the details
	}
#endif // UE4_POOL_BAFO_ALLOCATIONS_DEBUG_OOM

	LLM(FLowLevelMemTracker::Get().OnLowLevelAlloc(ELLMTracker::Platform, Ret, Size));

	return Ret;
}

void FUnixPlatformMemory::BinnedFreeToOS(void* Ptr, SIZE_T Size)
{
	LLM(FLowLevelMemTracker::Get().OnLowLevelFree(ELLMTracker::Platform, Ptr));

#if UE4_POOL_BAFO_ALLOCATIONS
	FScopeLock Lock(GetGlobalUnixMemPoolLock());

	UnixMemoryPool::TUnixMemoryPoolArray& PoolArray = UnixMemoryPool::GetPoolArray();
	if (LIKELY(PoolArray.Free(Ptr, Size)))
	{
		return;
	}
	// otherwise, let generic BFTO deal with it
#endif // UE4_POOL_BAFO_ALLOCATIONS

	return FGenericPlatformMemory::BinnedFreeToOS(Ptr, Size);
}

bool FUnixPlatformMemory::BinnedPlatformHasMemoryPoolForThisSize(SIZE_T Size)
{
#if UE4_POOL_BAFO_ALLOCATIONS
	return Size <= UnixMemoryPool::LargestPoolSize;
#else
	return false;
#endif // UE4_POOL_BAFO_ALLOCATIONS
}

namespace UnixPlatformMemory
{
	/**
	 * @brief Returns value in bytes from a status line
	 * @param Line in format "Blah:  10000 kB" - needs to be writable as it will modify it
	 * @return value in bytes (10240000, i.e. 10000 * 1024 for the above example)
	 */
	uint64 GetBytesFromStatusLine(char * Line)
	{
		check(Line);
		int Len = strlen(Line);

		// Len should be long enough to hold at least " kB\n"
		const int kSuffixLength = 4;	// " kB\n"
		if (Len <= kSuffixLength)
		{
			return 0;
		}

		// let's check that this is indeed "kB"
		char * Suffix = &Line[Len - kSuffixLength];
		if (strcmp(Suffix, " kB\n") != 0)
		{
			// Unix the kernel changed the format, huh?
			return 0;
		}

		// kill the kB
		*Suffix = 0;

		// find the beginning of the number
		for (const char * NumberBegin = Suffix; NumberBegin >= Line; --NumberBegin)
		{
			if (*NumberBegin == ' ')
			{
				return static_cast< uint64 >(atol(NumberBegin + 1)) * 1024ULL;
			}
		}

		// we were unable to find whitespace in front of the number
		return 0;
	}
}

FPlatformMemoryStats FUnixPlatformMemory::GetStats()
{
	FPlatformMemoryStats MemoryStats;	// will init from constants

#if PLATFORM_FREEBSD

	const FPlatformMemoryConstants& MemoryConstants = FPlatformMemory::GetConstants();

	size_t size = sizeof(SIZE_T);

	SIZE_T SysFreeCount = 0;
	sysctlbyname("vm.stats.vm.v_free_count", &SysFreeCount, &size, NULL, 0);

	SIZE_T SysActiveCount = 0;
	sysctlbyname("vm.stats.vm.v_active_count", &SysActiveCount, &size, NULL, 0);

	// Get swap info from kvm api
	kvm_t* Kvm = kvm_open(NULL, "/dev/null", NULL, O_RDONLY, NULL);
	struct kvm_swap KvmSwap;
	kvm_getswapinfo(Kvm, &KvmSwap, 1, 0);
	kvm_close(Kvm);

	MemoryStats.AvailablePhysical = SysFreeCount * MemoryConstants.PageSize;
	MemoryStats.AvailableVirtual = (KvmSwap.ksw_total - KvmSwap.ksw_used) * MemoryConstants.PageSize;
	MemoryStats.UsedPhysical = SysActiveCount * MemoryConstants.PageSize;
	MemoryStats.UsedVirtual = KvmSwap.ksw_used * MemoryConstants.PageSize;

#else

	// open to all kind of overflows, thanks to Unix approach of exposing system stats via /proc and lack of proper C API
	// And no, sysinfo() isn't useful for this (cannot get the same value for MemAvailable through it for example).

	if (FILE* FileGlobalMemStats = fopen("/proc/meminfo", "r"))
	{
		int FieldsSetSuccessfully = 0;
		uint64 MemFree = 0, Cached = 0;
		do
		{
			char LineBuffer[256] = {0};
			char *Line = fgets(LineBuffer, ARRAY_COUNT(LineBuffer), FileGlobalMemStats);
			if (Line == nullptr)
			{
				break;	// eof or an error
			}

			// if we have MemAvailable, favor that (see http://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=34e431b0ae398fc54ea69ff85ec700722c9da773)
			if (strstr(Line, "MemAvailable:") == Line)
			{
				MemoryStats.AvailablePhysical = UnixPlatformMemory::GetBytesFromStatusLine(Line);
				++FieldsSetSuccessfully;
			}
			else if (strstr(Line, "SwapFree:") == Line)
			{
				MemoryStats.AvailableVirtual = UnixPlatformMemory::GetBytesFromStatusLine(Line);
				++FieldsSetSuccessfully;
			}
			else if (strstr(Line, "MemFree:") == Line)
			{
				MemFree = UnixPlatformMemory::GetBytesFromStatusLine(Line);
				++FieldsSetSuccessfully;
			}
			else if (strstr(Line, "Cached:") == Line)
			{
				Cached = UnixPlatformMemory::GetBytesFromStatusLine(Line);
				++FieldsSetSuccessfully;
			}
		}
		while(FieldsSetSuccessfully < 4);

		// if we didn't have MemAvailable (kernels < 3.14 or CentOS 6.x), use free + cached as a (bad) approximation
		if (MemoryStats.AvailablePhysical == 0)
		{
			MemoryStats.AvailablePhysical = FMath::Min(MemFree + Cached, MemoryStats.TotalPhysical);
		}

		fclose(FileGlobalMemStats);
	}

	// again /proc "API" :/
	if (FILE* ProcMemStats = fopen("/proc/self/status", "r"))
	{
		int FieldsSetSuccessfully = 0;
		do
		{
			char LineBuffer[256] = {0};
			char *Line = fgets(LineBuffer, ARRAY_COUNT(LineBuffer), ProcMemStats);
			if (Line == nullptr)
			{
				break;	// eof or an error
			}

			if (strstr(Line, "VmPeak:") == Line)
			{
				MemoryStats.PeakUsedVirtual = UnixPlatformMemory::GetBytesFromStatusLine(Line);
				++FieldsSetSuccessfully;
			}
			else if (strstr(Line, "VmSize:") == Line)
			{
				MemoryStats.UsedVirtual = UnixPlatformMemory::GetBytesFromStatusLine(Line);
				++FieldsSetSuccessfully;
			}
			else if (strstr(Line, "VmHWM:") == Line)
			{
				MemoryStats.PeakUsedPhysical = UnixPlatformMemory::GetBytesFromStatusLine(Line);
				++FieldsSetSuccessfully;
			}
			else if (strstr(Line, "VmRSS:") == Line)
			{
				MemoryStats.UsedPhysical = UnixPlatformMemory::GetBytesFromStatusLine(Line);
				++FieldsSetSuccessfully;
			}
		}
		while(FieldsSetSuccessfully < 4);

		fclose(ProcMemStats);
	}

#endif // PLATFORM_FREEBSD

	// sanitize stats as sometimes peak < used for some reason
	MemoryStats.PeakUsedVirtual = FMath::Max(MemoryStats.PeakUsedVirtual, MemoryStats.UsedVirtual);
	MemoryStats.PeakUsedPhysical = FMath::Max(MemoryStats.PeakUsedPhysical, MemoryStats.UsedPhysical);

	return MemoryStats;
}

FExtendedPlatformMemoryStats FUnixPlatformMemory::GetExtendedStats()
{
	FExtendedPlatformMemoryStats MemoryStats;

	// More /proc "API" :/
	MemoryStats.Shared_Clean = 0;
	MemoryStats.Shared_Dirty = 0;
	MemoryStats.Private_Clean = 0;
	MemoryStats.Private_Dirty = 0;
	if (FILE* ProcSMaps = fopen("/proc/self/smaps", "r"))
	{
		do
		{
			char LineBuffer[256] = { 0 };
			char *Line = fgets(LineBuffer, ARRAY_COUNT(LineBuffer), ProcSMaps);
			if (Line == nullptr)
			{
				break;	// eof or an error
			}

			if (strstr(Line, "Shared_Clean:") == Line)
			{
				MemoryStats.Shared_Clean += UnixPlatformMemory::GetBytesFromStatusLine(Line);
			}
			else if (strstr(Line, "Shared_Dirty:") == Line)
			{
				MemoryStats.Shared_Dirty += UnixPlatformMemory::GetBytesFromStatusLine(Line);
			}
			if (strstr(Line, "Private_Clean:") == Line)
			{
				MemoryStats.Private_Clean += UnixPlatformMemory::GetBytesFromStatusLine(Line);
			}
			else if (strstr(Line, "Private_Dirty:") == Line)
			{
				MemoryStats.Private_Dirty += UnixPlatformMemory::GetBytesFromStatusLine(Line);
			}
		} while (!feof(ProcSMaps));

		fclose(ProcSMaps);
	}

	return MemoryStats;
}

const FPlatformMemoryConstants& FUnixPlatformMemory::GetConstants()
{
	static FPlatformMemoryConstants MemoryConstants;

	if( MemoryConstants.TotalPhysical == 0 )
	{
#if PLATFORM_FREEBSD

		size_t Size = sizeof(SIZE_T);

		SIZE_T SysPageCount = 0;
		sysctlbyname("vm.stats.vm.v_page_count", &SysPageCount, &Size, NULL, 0);

		SIZE_T SysPageSize = 0;
		sysctlbyname("vm.stats.vm.v_page_size", &SysPageSize, &Size, NULL, 0);

		// Get swap info from kvm api
		kvm_t* Kvm = kvm_open(NULL, "/dev/null", NULL, O_RDONLY, NULL);
		struct kvm_swap KvmSwap;
		kvm_getswapinfo(Kvm, &KvmSwap, 1, 0);
		kvm_close(Kvm);

		MemoryConstants.TotalPhysical = SysPageCount * SysPageSize;
		MemoryConstants.TotalVirtual = KvmSwap.ksw_total * SysPageSize;
		MemoryConstants.PageSize = SysPageSize;

#else
 
		// Gather platform memory stats.
		struct sysinfo SysInfo;
		unsigned long long MaxPhysicalRAMBytes = 0;
		unsigned long long MaxVirtualRAMBytes = 0;

		if (0 == sysinfo(&SysInfo))
		{
			MaxPhysicalRAMBytes = static_cast< unsigned long long >( SysInfo.mem_unit ) * static_cast< unsigned long long >( SysInfo.totalram );
			MaxVirtualRAMBytes = static_cast< unsigned long long >( SysInfo.mem_unit ) * static_cast< unsigned long long >( SysInfo.totalswap );
		}

		MemoryConstants.TotalPhysical = MaxPhysicalRAMBytes;
		MemoryConstants.TotalVirtual = MaxVirtualRAMBytes;

#endif // PLATFORM_FREEBSD

		MemoryConstants.TotalPhysicalGB = (MemoryConstants.TotalPhysical + 1024ULL * 1024ULL * 1024ULL - 1) / 1024ULL / 1024ULL / 1024ULL;

		MemoryConstants.PageSize = sysconf(_SC_PAGESIZE);
		MemoryConstants.BinnedPageSize = FMath::Max((SIZE_T)65536, MemoryConstants.PageSize);
		MemoryConstants.BinnedAllocationGranularity = 16384;  // Binned2 malloc will allocate in increments of this, and this is the minimum constant recommended
		MemoryConstants.OsAllocationGranularity = MemoryConstants.BinnedPageSize;
	}

	return MemoryConstants;	
}

FPlatformMemory::FSharedMemoryRegion* FUnixPlatformMemory::MapNamedSharedMemoryRegion(const FString& InName, bool bCreate, uint32 AccessMode, SIZE_T Size)
{
	// expecting platform-independent name, so convert it to match platform requirements
	FString Name("/");
	Name += InName;
	FTCHARToUTF8 NameUTF8(*Name);

	// correct size to match platform constraints
	FPlatformMemoryConstants MemConstants = FPlatformMemory::GetConstants();
	check(MemConstants.PageSize > 0);	// also relying on it being power of two, which should be true in foreseeable future
	if (Size & (MemConstants.PageSize - 1))
	{
		Size = Size & ~(MemConstants.PageSize - 1);
		Size += MemConstants.PageSize;
	}

	int ShmOpenFlags = bCreate ? O_CREAT : 0;
	// note that you cannot combine O_RDONLY and O_WRONLY to get O_RDWR
	check(AccessMode != 0);
	if (AccessMode == FPlatformMemory::ESharedMemoryAccess::Read)
	{
		ShmOpenFlags |= O_RDONLY;
	}
	else if (AccessMode == FPlatformMemory::ESharedMemoryAccess::Write)
	{
		ShmOpenFlags |= O_WRONLY;
	}
	else if (AccessMode == (FPlatformMemory::ESharedMemoryAccess::Write | FPlatformMemory::ESharedMemoryAccess::Read))
	{
		ShmOpenFlags |= O_RDWR;
	}

	int ShmOpenMode = (S_IRUSR | S_IWUSR) | (S_IRGRP | S_IWGRP) | (S_IROTH | S_IWOTH );	// 0666

	// open the object
	int SharedMemoryFd = shm_open(NameUTF8.Get(), ShmOpenFlags, ShmOpenMode);
	if (SharedMemoryFd == -1)
	{
		int ErrNo = errno;
		UE_LOG(LogHAL, Warning, TEXT("shm_open(name='%s', flags=0x%x, mode=0x%x) failed with errno = %d (%s)"), *Name, ShmOpenFlags, ShmOpenMode, ErrNo, 
			StringCast< TCHAR >(strerror(ErrNo)).Get());
		return NULL;
	}

	// truncate if creating (note that we may still don't have rights to do so)
	if (bCreate)
	{
		int Res = ftruncate(SharedMemoryFd, Size);
		if (Res != 0)
		{
			int ErrNo = errno;
			UE_LOG(LogHAL, Warning, TEXT("ftruncate(fd=%d, size=%d) failed with errno = %d (%s)"), SharedMemoryFd, Size, ErrNo, 
				StringCast< TCHAR >(strerror(ErrNo)).Get());
			shm_unlink(NameUTF8.Get());
			return NULL;
		}
	}

	// map
	int MmapProtFlags = 0;
	if (AccessMode & FPlatformMemory::ESharedMemoryAccess::Read)
	{
		MmapProtFlags |= PROT_READ;
	}

	if (AccessMode & FPlatformMemory::ESharedMemoryAccess::Write)
	{
		MmapProtFlags |= PROT_WRITE;
	}

	void *Ptr = mmap(NULL, Size, MmapProtFlags, MAP_SHARED, SharedMemoryFd, 0);
	if (Ptr == MAP_FAILED)
	{
		int ErrNo = errno;
		UE_LOG(LogHAL, Warning, TEXT("mmap(addr=NULL, length=%d, prot=0x%x, flags=MAP_SHARED, fd=%d, 0) failed with errno = %d (%s)"), Size, MmapProtFlags, SharedMemoryFd, ErrNo, 
			StringCast< TCHAR >(strerror(ErrNo)).Get());

		if (bCreate)
		{
			shm_unlink(NameUTF8.Get());
		}
		return NULL;
	}

	return new FUnixSharedMemoryRegion(Name, AccessMode, Ptr, Size, SharedMemoryFd, bCreate);
}

bool FUnixPlatformMemory::UnmapNamedSharedMemoryRegion(FSharedMemoryRegion * MemoryRegion)
{
	bool bAllSucceeded = true;

	if (MemoryRegion)
	{
		FUnixSharedMemoryRegion * UnixRegion = static_cast< FUnixSharedMemoryRegion* >( MemoryRegion );

		if (munmap(UnixRegion->GetAddress(), UnixRegion->GetSize()) == -1) 
		{
			bAllSucceeded = false;

			int ErrNo = errno;
			UE_LOG(LogHAL, Warning, TEXT("munmap(addr=%p, len=%d) failed with errno = %d (%s)"), UnixRegion->GetAddress(), UnixRegion->GetSize(), ErrNo, 
				StringCast< TCHAR >(strerror(ErrNo)).Get());
		}

		if (close(UnixRegion->GetFileDescriptor()) == -1)
		{
			bAllSucceeded = false;

			int ErrNo = errno;
			UE_LOG(LogHAL, Warning, TEXT("close(fd=%d) failed with errno = %d (%s)"), UnixRegion->GetFileDescriptor(), ErrNo, 
				StringCast< TCHAR >(strerror(ErrNo)).Get());
		}

		if (UnixRegion->NeedsToUnlinkRegion())
		{
			FTCHARToUTF8 NameUTF8(UnixRegion->GetName());
			if (shm_unlink(NameUTF8.Get()) == -1)
			{
				bAllSucceeded = false;

				int ErrNo = errno;
				UE_LOG(LogHAL, Warning, TEXT("shm_unlink(name='%s') failed with errno = %d (%s)"), UnixRegion->GetName(), ErrNo, 
					StringCast< TCHAR >(strerror(ErrNo)).Get());
			}
		}

		// delete the region
		delete UnixRegion;
	}

	return bAllSucceeded;
}


/**
* LLM uses these low level functions (LLMAlloc and LLMFree) to allocate memory. It grabs
* the function pointers by calling FPlatformMemory::GetLLMAllocFunctions. If these functions
* are not implemented GetLLMAllocFunctions should return false and LLM will be disabled.
*/

#if ENABLE_LOW_LEVEL_MEM_TRACKER

void* LLMAlloc(size_t Size)
{
	void* Ptr = mmap(nullptr, Size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

	return Ptr;
}

void LLMFree(void* Addr, size_t Size)
{
	if (Addr != nullptr && munmap(Addr, Size) != 0)
	{
		const int ErrNo = errno;
		UE_LOG(LogHAL, Fatal, TEXT("munmap(addr=%p, len=%llu) failed with errno = %d (%s)"), Addr, Size,
			ErrNo, StringCast< TCHAR >(strerror(ErrNo)).Get());
	}
}

#endif

bool FUnixPlatformMemory::GetLLMAllocFunctions(void*(*&OutAllocFunction)(size_t), void(*&OutFreeFunction)(void*, size_t), int32& OutAlignment)
{
#if ENABLE_LOW_LEVEL_MEM_TRACKER
	OutAllocFunction = LLMAlloc;
	OutFreeFunction = LLMFree;
	OutAlignment = FPlatformMemory::GetConstants().PageSize;
	return true;
#else
	return false;
#endif
}
