// Copyright Epic Games, Inc. All Rights Reserved.

#include "IPlatformFilePak.h"
#include "HAL/FileManager.h"
#include "Misc/CoreMisc.h"
#include "Misc/CommandLine.h"
#include "Async/AsyncWork.h"
#include "Serialization/MemoryReader.h"
#include "HAL/IConsoleManager.h"
#include "HAL/LowLevelMemTracker.h"
#include "Misc/CoreDelegates.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManagerGeneric.h"
#include "HAL/IPlatformFileModule.h"
#include "SignedArchiveReader.h"
#include "Misc/AES.h"
#include "GenericPlatform/GenericPlatformChunkInstall.h"
#include "Async/AsyncFileHandle.h"
#include "Templates/Greater.h"
#include "Serialization/ArchiveProxy.h"
#include "Misc/Base64.h"
#include "HAL/DiskUtilizationTracker.h"
#include "Stats/StatsMisc.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/ThreadHeartBeat.h"
#if !(IS_PROGRAM || WITH_EDITOR)
#include "Misc/ConfigCacheIni.h"
#endif
#include "ProfilingDebugging/CsvProfiler.h"
#include "Misc/Fnv.h"

#include "Async/MappedFileHandle.h"
#include "IO/IoDispatcher.h"

#include "ProfilingDebugging/LoadTimeTracker.h"

DEFINE_LOG_CATEGORY(LogPakFile);

DEFINE_STAT(STAT_PakFile_Read);
DEFINE_STAT(STAT_PakFile_NumOpenHandles);

IMPLEMENT_TYPE_LAYOUT(FPakCompressedBlock);
IMPLEMENT_TYPE_LAYOUT(FPakEntry);
IMPLEMENT_TYPE_LAYOUT(FPakFileData);

CSV_DECLARE_CATEGORY_MODULE_EXTERN(CORE_API, FileIO);

#ifndef DISABLE_NONUFS_INI_WHEN_COOKED
#define DISABLE_NONUFS_INI_WHEN_COOKED 0
#endif
#ifndef ALLOW_INI_OVERRIDE_FROM_COMMANDLINE
#define ALLOW_INI_OVERRIDE_FROM_COMMANDLINE 0
#endif
#ifndef ALL_PAKS_WILDCARD
#define ALL_PAKS_WILDCARD "*.pak"
#endif 

#ifndef MOUNT_STARTUP_PAKS_WILDCARD
#define MOUNT_STARTUP_PAKS_WILDCARD ALL_PAKS_WILDCARD
#endif

static FString GMountStartupPaksWildCard = TEXT(MOUNT_STARTUP_PAKS_WILDCARD);

int32 GetPakchunkIndexFromPakFile(const FString& InFilename)
{
	FString ChunkIdentifier(TEXT("pakchunk"));
	FString BaseFilename = FPaths::GetBaseFilename(InFilename);
	int32 ChunkNumber = INDEX_NONE;

	if (BaseFilename.StartsWith(ChunkIdentifier))
	{
		int32 StartOfNumber = ChunkIdentifier.Len();
		int32 DigitCount = 0;
		if (FChar::IsDigit(BaseFilename[StartOfNumber]))
		{
			while ((DigitCount + StartOfNumber) < BaseFilename.Len() && FChar::IsDigit(BaseFilename[StartOfNumber + DigitCount]))
			{
				DigitCount++;
			}

			if ((StartOfNumber + DigitCount) < BaseFilename.Len())
			{
				FString ChunkNumberString = BaseFilename.Mid(StartOfNumber, DigitCount);
				check(ChunkNumberString.IsNumeric());
				TTypeFromString<int32>::FromString(ChunkNumber, *ChunkNumberString);
			}
		}
	}

	return ChunkNumber;
}

// Registered encryption key cache
class FEncryptionKeyCache
{
public:

	void AddKey(const FGuid& InGuid, const FAES::FAESKey InKey)
	{
		FScopeLock Lock(&SyncObject);
		if (!Keys.Contains(InGuid))
		{
			Keys.Add(InGuid, InKey);
		}
	}

	bool GetKey(const FGuid& InGuid, FAES::FAESKey& OutKey)
	{
		FScopeLock Lock(&SyncObject);
		if (const FAES::FAESKey* Key = Keys.Find(InGuid))
		{
			OutKey = *Key;
			return true;
		}
		return false;
	}

	bool const HasKey(const FGuid& InGuid)
	{
		return Keys.Contains(InGuid);
	}

private:

	TMap<FGuid, FAES::FAESKey> Keys;
	FCriticalSection SyncObject;
};

FEncryptionKeyCache& GetRegisteredEncryptionKeys()
{
	static FEncryptionKeyCache Instance;
	return Instance;
}

#if !UE_BUILD_SHIPPING
static void TestRegisterEncryptionKey(const TArray<FString>& Args)
{
	if (Args.Num() == 2)
	{
		FGuid EncryptionKeyGuid;
		FAES::FAESKey EncryptionKey;
		if (FGuid::Parse(Args[0], EncryptionKeyGuid))
		{
			TArray<uint8> KeyBytes;
			if (FBase64::Decode(Args[1], KeyBytes))
			{
				check(KeyBytes.Num() == sizeof(FAES::FAESKey));
				FMemory::Memcpy(EncryptionKey.Key, &KeyBytes[0], sizeof(EncryptionKey.Key));
				FCoreDelegates::GetRegisterEncryptionKeyDelegate().ExecuteIfBound(EncryptionKeyGuid, EncryptionKey);
			}
		}
	}
}

static FAutoConsoleCommand CVar_TestRegisterEncryptionKey(
	TEXT("pak.TestRegisterEncryptionKey"),
	TEXT("Test dynamic encryption key registration. params: <guid> <base64key>"),
	FConsoleCommandWithArgsDelegate::CreateStatic(TestRegisterEncryptionKey));
#endif

TPakChunkHash ComputePakChunkHash(const void* InData, int64 InDataSizeInBytes)
{
#if PAKHASH_USE_CRC
	return FCrc::MemCrc32(InData, InDataSizeInBytes);
#else
	FSHAHash Hash;
	FSHA1::HashBuffer(InData, InDataSizeInBytes, Hash.Hash);
	return Hash;
#endif
}

#ifndef EXCLUDE_NONPAK_UE_EXTENSIONS
#define EXCLUDE_NONPAK_UE_EXTENSIONS 1	// Use .Build.cs file to disable this if the game relies on accessing loose files
#endif

FFilenameSecurityDelegate& FPakPlatformFile::GetFilenameSecurityDelegate()
{
	static FFilenameSecurityDelegate Delegate;
	return Delegate;
}

FPakCustomEncryptionDelegate& FPakPlatformFile::GetPakCustomEncryptionDelegate()
{
	static FPakCustomEncryptionDelegate Delegate;
	return Delegate;
}

FPakPlatformFile::FPakSigningFailureHandlerData& FPakPlatformFile::GetPakSigningFailureHandlerData()
{
	static FPakSigningFailureHandlerData Instance;
	return Instance;
}

// Needs to be a global as multiple threads may try to access the failed delegate
FPakChunkSignatureCheckFailedHandler& FPakPlatformFile::GetPakChunkSignatureCheckFailedHandler()
{
	return GetPakSigningFailureHandlerData().ChunkSignatureCheckFailedDelegate;
}

FPakMasterSignatureTableCheckFailureHandler& FPakPlatformFile::GetPakMasterSignatureTableCheckFailureHandler()
{
	return GetPakSigningFailureHandlerData().MasterSignatureTableCheckFailedDelegate;
}

void FPakPlatformFile::BroadcastPakChunkSignatureCheckFailure(const FPakChunkSignatureCheckFailedData& InData)
{
	FPakSigningFailureHandlerData& HandlerData = GetPakSigningFailureHandlerData();
	FScopeLock Lock(&HandlerData.Lock);
	HandlerData.ChunkSignatureCheckFailedDelegate.Broadcast(InData);
}

void FPakPlatformFile::BroadcastPakMasterSignatureTableCheckFailure(const FString& InFilename)
{
	FPakSigningFailureHandlerData& HandlerData = GetPakSigningFailureHandlerData();
	FScopeLock Lock(&HandlerData.Lock);
	HandlerData.MasterSignatureTableCheckFailedDelegate.Broadcast(InFilename);
}

void FPakPlatformFile::GetFilenamesInChunk(const FString& InPakFilename, const TArray<int32>& InChunkIDs, TArray<FString>& OutFileList) 
{
	TArray<FPakListEntry> Paks;
	GetMountedPaks(Paks);

	for (const FPakListEntry& Pak : Paks)
	{
		if (Pak.PakFile && Pak.PakFile->GetFilename() == InPakFilename)
		{
			Pak.PakFile->GetFilenamesInChunk(InChunkIDs, OutFileList);
			break;
		}
	}
}

void FPakPlatformFile::GetFilenamesInPakFile(const FString& InPakFilename, TArray<FString>& OutFileList)
{
	TArray<FPakListEntry> Paks;
	GetMountedPaks(Paks);

	for (const FPakListEntry& Pak : Paks)
	{
		if (Pak.PakFile && Pak.PakFile->GetFilename() == InPakFilename)
		{
			Pak.PakFile->GetFilenames(OutFileList);
			break;
		}
	}
}

#if !defined(PLATFORM_BYPASS_PAK_PRECACHE)
	#error "PLATFORM_BYPASS_PAK_PRECACHE must be defined."
#endif

#define USE_PAK_PRECACHE (!PLATFORM_BYPASS_PAK_PRECACHE && !IS_PROGRAM && !WITH_EDITOR) // you can turn this off to use the async IO stuff without the precache

/**
* Precaching
*/

void FPakPlatformFile::GetPakEncryptionKey(FAES::FAESKey& OutKey, const FGuid& InEncryptionKeyGuid)
{
	OutKey.Reset();

	if (!GetRegisteredEncryptionKeys().GetKey(InEncryptionKeyGuid, OutKey))
	{
		if (!InEncryptionKeyGuid.IsValid() && FCoreDelegates::GetPakEncryptionKeyDelegate().IsBound())
		{
			FCoreDelegates::GetPakEncryptionKeyDelegate().Execute(OutKey.Key);
		}
		else
		{
			UE_LOG(LogPakFile, Fatal, TEXT("Failed to find requested encryption key %s"), *InEncryptionKeyGuid.ToString());
		}
	}
}

TMap<FName, TSharedPtr<const struct FPakSignatureFile, ESPMode::ThreadSafe>> FPakPlatformFile::PakSignatureFileCache;
FCriticalSection FPakPlatformFile::PakSignatureFileCacheLock;
TSharedPtr<const struct FPakSignatureFile, ESPMode::ThreadSafe> FPakPlatformFile::GetPakSignatureFile(const TCHAR* InFilename)
{
	FScopeLock Lock(&PakSignatureFileCacheLock);

	FName FilenameFName(InFilename);
	if (const TSharedPtr<const struct FPakSignatureFile, ESPMode::ThreadSafe>* SignaturesFile = PakSignatureFileCache.Find(FilenameFName))
	{
		return *SignaturesFile;
	}

	static FRSAKeyHandle PublicKey = InvalidRSAKeyHandle;
	static bool bInitializedPublicKey = false;
	if (!bInitializedPublicKey)
	{
		FCoreDelegates::FPakSigningKeysDelegate& Delegate = FCoreDelegates::GetPakSigningKeysDelegate();
		if (Delegate.IsBound())
		{
			TArray<uint8> Exponent;
			TArray<uint8> Modulus;
			Delegate.Execute(Exponent, Modulus);
			PublicKey = FRSA::CreateKey(Exponent, TArray<uint8>(), Modulus);
		}
		bInitializedPublicKey = true;
	}

	TSharedPtr<FPakSignatureFile, ESPMode::ThreadSafe> SignaturesFile;

	if (PublicKey != InvalidRSAKeyHandle)
	{
		FString SignaturesFilename = FPaths::ChangeExtension(InFilename, TEXT("sig"));
		FArchive* Reader = IFileManager::Get().CreateFileReader(*SignaturesFilename);
		if (Reader != nullptr)
		{
			SignaturesFile = MakeShared<FPakSignatureFile, ESPMode::ThreadSafe>();
			SignaturesFile->Serialize(*Reader);
			delete Reader;

			if (!SignaturesFile->DecryptSignatureAndValidate(PublicKey, InFilename))
			{
				// We don't need to act on this failure as the decrypt function will already have dumped out log messages
				// and fired the signature check fail handler
				SignaturesFile.Reset();
			}

			PakSignatureFileCache.Add(FilenameFName, SignaturesFile);
		}
		else
		{
			UE_LOG(LogPakFile, Warning, TEXT("Couldn't find pak signature file '%s'"), InFilename);
			BroadcastPakMasterSignatureTableCheckFailure(InFilename);
		}
	}

	return SignaturesFile;
}

void FPakPlatformFile::RemoveCachedPakSignaturesFile(const TCHAR* InFilename)
{
	FScopeLock Lock(&PakSignatureFileCacheLock);
	FName FilenameFName(InFilename);
	if (PakSignatureFileCache.Contains(FilenameFName))
	{
		PakSignatureFileCache.Remove(FilenameFName);
	}
}

DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("PakCache Sync Decrypts (Uncompressed Path)"), STAT_PakCache_SyncDecrypts, STATGROUP_PakFile);
DECLARE_FLOAT_ACCUMULATOR_STAT(TEXT("PakCache Decrypt Time"), STAT_PakCache_DecryptTime, STATGROUP_PakFile);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("PakCache Async Decrypts (Compressed Path)"), STAT_PakCache_CompressedDecrypts, STATGROUP_PakFile);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("PakCache Async Decrypts (Uncompressed Path)"), STAT_PakCache_UncompressedDecrypts, STATGROUP_PakFile);

void DecryptData(uint8* InData, uint32 InDataSize, FGuid InEncryptionKeyGuid)
{
	if (FPakPlatformFile::GetPakCustomEncryptionDelegate().IsBound())
	{
		FPakPlatformFile::GetPakCustomEncryptionDelegate().Execute(InData, InDataSize, InEncryptionKeyGuid);
	}
	else
	{
		SCOPE_SECONDS_ACCUMULATOR(STAT_PakCache_DecryptTime);
		FAES::FAESKey Key;
		FPakPlatformFile::GetPakEncryptionKey(Key, InEncryptionKeyGuid);
		check(Key.IsValid());
		FAES::DecryptData(InData, InDataSize, Key);
	}
}

#if !UE_BUILD_SHIPPING
static int32 GPakCache_ForceDecompressionFails = 0;
static FAutoConsoleVariableRef CVar_ForceDecompressionFails(
	TEXT("ForceDecompressionFails"),
	GPakCache_ForceDecompressionFails,
	TEXT("If > 0, then force decompression failures to test the panic sync read fallback.")
);
#endif

class FPakSizeRequest : public IAsyncReadRequest
{
public:
	FPakSizeRequest(FAsyncFileCallBack* CompleteCallback, int64 InFileSize)
		: IAsyncReadRequest(CompleteCallback, true, nullptr)
	{
		Size = InFileSize;
		SetComplete();
	}

	virtual void WaitCompletionImpl(float TimeLimitSeconds) override
	{
		// Even though SetComplete called in the constructor and sets bCompleteAndCallbackCalled=true, we still need to implement WaitComplete as
		// the CompleteCallback can end up starting async tasks that can overtake the constructor execution and need to wait for the constructor to finish.
		while (!*(volatile bool*)&bCompleteAndCallbackCalled);
	}

	virtual void CancelImpl()
	{
	}
};

#if USE_PAK_PRECACHE
#include "Async/TaskGraphInterfaces.h"
#define PAK_CACHE_GRANULARITY (1024*64)
static_assert((PAK_CACHE_GRANULARITY % FPakInfo::MaxChunkDataSize) == 0, "PAK_CACHE_GRANULARITY must be set to a multiple of FPakInfo::MaxChunkDataSize");
#define PAK_CACHE_MAX_REQUESTS (8)
#define PAK_CACHE_MAX_PRIORITY_DIFFERENCE_MERGE (AIOP_Normal - AIOP_MIN)
#define PAK_EXTRA_CHECKS DO_CHECK

DECLARE_MEMORY_STAT(TEXT("PakCache Current"), STAT_PakCacheMem, STATGROUP_Memory);
DECLARE_MEMORY_STAT(TEXT("PakCache High Water"), STAT_PakCacheHighWater, STATGROUP_Memory);

#if CSV_PROFILER
volatile int64 GPreCacheHotBlocksCount = 0;
volatile int64 GPreCacheColdBlocksCount = 0;
volatile int64 GPreCacheTotalLoaded = 0;
int64 GPreCacheTotalLoadedLastTick = 0;

volatile int64 GPreCacheSeeks = 0;
volatile int64 GPreCacheBadSeeks = 0;
volatile int64 GPreCacheContiguousReads = 0;
#endif

DECLARE_FLOAT_ACCUMULATOR_STAT(TEXT("PakCache Signing Chunk Hash Time"), STAT_PakCache_SigningChunkHashTime, STATGROUP_PakFile);
DECLARE_MEMORY_STAT(TEXT("PakCache Signing Chunk Hash Size"), STAT_PakCache_SigningChunkHashSize, STATGROUP_PakFile);


static int32 GPakCache_Enable = 1;
static FAutoConsoleVariableRef CVar_Enable(
	TEXT("pakcache.Enable"),
	GPakCache_Enable,
	TEXT("If > 0, then enable the pak cache.")
);

int32 GPakCache_CachePerPakFile = 0;
static FAutoConsoleVariableRef CVar_CachePerPakFile(
	TEXT("pakcache.CachePerPakFile"),
	GPakCache_CachePerPakFile,
	TEXT("if > 0, then each pak file will have it's own cache")
);

int32 GPakCache_UseNewTrim = 0;
static FAutoConsoleVariableRef CVar_UseNewTrim(
	TEXT("pakcache.UseNewTrim"),
	GPakCache_UseNewTrim,
	TEXT("if > 0, then we'll use a round robin per pak file trim")
);

int32 GPakCache_MaxBlockMemory = 128;
static FAutoConsoleVariableRef CVar_MaxBlockMemory(
	TEXT("pakcache.MaxBlockMemory"),
	GPakCache_MaxBlockMemory,
	TEXT("A soft memory budget in MB for the max memory used for precaching, that we'll try and adhere to ")
);


int32 GPakCache_MaxRequestsToLowerLevel = 2;
static FAutoConsoleVariableRef CVar_MaxRequestsToLowerLevel(
	TEXT("pakcache.MaxRequestsToLowerLevel"),
	GPakCache_MaxRequestsToLowerLevel,
	TEXT("Controls the maximum number of IO requests submitted to the OS filesystem at one time. Limited by PAK_CACHE_MAX_REQUESTS.")
);

int32 GPakCache_MaxRequestSizeToLowerLevelKB = 1024;
static FAutoConsoleVariableRef CVar_MaxRequestSizeToLowerLevelKB(
	TEXT("pakcache.MaxRequestSizeToLowerLevellKB"),
	GPakCache_MaxRequestSizeToLowerLevelKB,
	TEXT("Controls the maximum size (in KB) of IO requests submitted to the OS filesystem.")
);

int32 GPakCache_NumUnreferencedBlocksToCache = 10;
static FAutoConsoleVariableRef CVar_NumUnreferencedBlocksToCache(
	TEXT("pakcache.NumUnreferencedBlocksToCache"),
	GPakCache_NumUnreferencedBlocksToCache,
	TEXT("Controls the maximum number of unreferenced blocks to keep. This is a classic disk cache and the maxmimum wasted memory is pakcache.MaxRequestSizeToLowerLevellKB * pakcache.NumUnreferencedBlocksToCache.")
);

float GPakCache_TimeToTrim = 0.0f;
static FAutoConsoleVariableRef CVar_PakCache_TimeToTrim(
	TEXT("pakcache.TimeToTrim"),
	GPakCache_TimeToTrim,
	TEXT("Controls how long to hold onto a cached but unreferenced block for.")
);

int32 GPakCache_EnableNoCaching = 0;
static FAutoConsoleVariableRef CVar_EnableNoCaching(
	TEXT("pakcache.EnableNoCaching"),
	GPakCache_EnableNoCaching,
	TEXT("if > 0, then we'll allow a read requests pak cache memory to be ditched early")
);

class FPakPrecacher;

typedef uint64 FJoinedOffsetAndPakIndex;
static FORCEINLINE uint16 GetRequestPakIndexLow(FJoinedOffsetAndPakIndex Joined)
{
	return uint16((Joined >> 48) & 0xffff);
}

static FORCEINLINE int64 GetRequestOffset(FJoinedOffsetAndPakIndex Joined)
{
	return int64(Joined & 0xffffffffffffll);
}

static FORCEINLINE FJoinedOffsetAndPakIndex MakeJoinedRequest(uint16 PakIndex, int64 Offset)
{
	check(Offset >= 0);
	return (FJoinedOffsetAndPakIndex(PakIndex) << 48) | Offset;
}

enum
{
	IntervalTreeInvalidIndex = 0
};


typedef uint32 TIntervalTreeIndex; // this is the arg type of TSparseArray::operator[]

static uint32 GNextSalt = 1;

// This is like TSparseArray, only a bit safer and I needed some restrictions on resizing.
template<class TItem>
class TIntervalTreeAllocator
{
	TArray<TItem> Items;
	TArray<int32> FreeItems; //@todo make this into a linked list through the existing items
	uint32 Salt;
	uint32 SaltMask;
public:
	TIntervalTreeAllocator()
	{
		check(GNextSalt < 4);
		Salt = (GNextSalt++) << 30;
		SaltMask = MAX_uint32 << 30;
		verify((Alloc() & ~SaltMask) == IntervalTreeInvalidIndex); // we want this to always have element zero so we can figure out an index from a pointer
	}
	inline TIntervalTreeIndex Alloc()
	{
		int32 Result;
		if (FreeItems.Num())
		{
			Result = FreeItems.Pop();
		}
		else
		{
			Result = Items.Num();
			Items.AddUninitialized();

		}
		new ((void*)&Items[Result]) TItem();
		return Result | Salt;;
	}
	void EnsureNoRealloc(int32 NeededNewNum)
	{
		if (FreeItems.Num() + Items.GetSlack() < NeededNewNum)
		{
			Items.Reserve(Items.Num() + NeededNewNum);
		}
	}
	FORCEINLINE TItem& Get(TIntervalTreeIndex InIndex)
	{
		TIntervalTreeIndex Index = InIndex & ~SaltMask;
		check((InIndex & SaltMask) == Salt && Index != IntervalTreeInvalidIndex && Index >= 0 && Index < (uint32)Items.Num()); //&& !FreeItems.Contains(Index));
		return Items[Index];
	}
	FORCEINLINE void Free(TIntervalTreeIndex InIndex)
	{
		TIntervalTreeIndex Index = InIndex & ~SaltMask;
		check((InIndex & SaltMask) == Salt && Index != IntervalTreeInvalidIndex && Index >= 0 && Index < (uint32)Items.Num()); //&& !FreeItems.Contains(Index));
		Items[Index].~TItem();
		FreeItems.Push(Index);
		if (FreeItems.Num() + 1 == Items.Num())
		{
			// get rid everything to restore memory coherence
			Items.Empty();
			FreeItems.Empty();
			verify((Alloc() & ~SaltMask) == IntervalTreeInvalidIndex); // we want this to always have element zero so we can figure out an index from a pointer
		}
	}
	FORCEINLINE void CheckIndex(TIntervalTreeIndex InIndex)
	{
		TIntervalTreeIndex Index = InIndex & ~SaltMask;
		check((InIndex & SaltMask) == Salt && Index != IntervalTreeInvalidIndex && Index >= 0 && Index < (uint32)Items.Num()); // && !FreeItems.Contains(Index));
	}
};

class FIntervalTreeNode
{
public:
	TIntervalTreeIndex LeftChildOrRootOfLeftList;
	TIntervalTreeIndex RootOfOnList;
	TIntervalTreeIndex RightChildOrRootOfRightList;

	FIntervalTreeNode()
		: LeftChildOrRootOfLeftList(IntervalTreeInvalidIndex)
		, RootOfOnList(IntervalTreeInvalidIndex)
		, RightChildOrRootOfRightList(IntervalTreeInvalidIndex)
	{
	}
	~FIntervalTreeNode()
	{
		check(LeftChildOrRootOfLeftList == IntervalTreeInvalidIndex && RootOfOnList == IntervalTreeInvalidIndex && RightChildOrRootOfRightList == IntervalTreeInvalidIndex); // this routine does not handle recursive destruction
	}
};

static TIntervalTreeAllocator<FIntervalTreeNode> GIntervalTreeNodeNodeAllocator;

static FORCEINLINE uint64 HighBit(uint64 x)
{
	return x & (1ull << 63);
}

static FORCEINLINE bool IntervalsIntersect(uint64 Min1, uint64 Max1, uint64 Min2, uint64 Max2)
{
	return !(Max2 < Min1 || Max1 < Min2);
}

template<typename TItem>
// this routine assume that the pointers remain valid even though we are reallocating
static void AddToIntervalTree_Dangerous(
	TIntervalTreeIndex* RootNode,
	TIntervalTreeAllocator<TItem>& Allocator,
	TIntervalTreeIndex Index,
	uint64 MinInterval,
	uint64 MaxInterval,
	uint32 CurrentShift,
	uint32 MaxShift
)
{
	while (true)
	{
		if (*RootNode == IntervalTreeInvalidIndex)
		{
			*RootNode = GIntervalTreeNodeNodeAllocator.Alloc();
		}

		int64 MinShifted = HighBit(MinInterval << CurrentShift);
		int64 MaxShifted = HighBit(MaxInterval << CurrentShift);
		FIntervalTreeNode& Root = GIntervalTreeNodeNodeAllocator.Get(*RootNode);

		if (MinShifted == MaxShifted && CurrentShift < MaxShift)
		{
			CurrentShift++;
			RootNode = (!MinShifted) ? &Root.LeftChildOrRootOfLeftList : &Root.RightChildOrRootOfRightList;
		}
		else
		{
			TItem& Item = Allocator.Get(Index);
			if (MinShifted != MaxShifted) // crosses middle
			{
				Item.Next = Root.RootOfOnList;
				Root.RootOfOnList = Index;
			}
			else // we are at the leaf
			{
				if (!MinShifted)
				{
					Item.Next = Root.LeftChildOrRootOfLeftList;
					Root.LeftChildOrRootOfLeftList = Index;
				}
				else
				{
					Item.Next = Root.RightChildOrRootOfRightList;
					Root.RightChildOrRootOfRightList = Index;
				}
			}
			return;
		}
	}
}

template<typename TItem>
static void AddToIntervalTree(
	TIntervalTreeIndex* RootNode,
	TIntervalTreeAllocator<TItem>& Allocator,
	TIntervalTreeIndex Index,
	uint32 StartShift,
	uint32 MaxShift
)
{
	GIntervalTreeNodeNodeAllocator.EnsureNoRealloc(1 + MaxShift - StartShift);
	TItem& Item = Allocator.Get(Index);
	check(Item.Next == IntervalTreeInvalidIndex);
	uint64 MinInterval = GetRequestOffset(Item.OffsetAndPakIndex);
	uint64 MaxInterval = MinInterval + Item.Size - 1;
	AddToIntervalTree_Dangerous(RootNode, Allocator, Index, MinInterval, MaxInterval, StartShift, MaxShift);

}

template<typename TItem>
static FORCEINLINE bool ScanNodeListForRemoval(
	TIntervalTreeIndex* Iter,
	TIntervalTreeAllocator<TItem>& Allocator,
	TIntervalTreeIndex Index,
	uint64 MinInterval,
	uint64 MaxInterval
)
{
	while (*Iter != IntervalTreeInvalidIndex)
	{

		TItem& Item = Allocator.Get(*Iter);
		if (*Iter == Index)
		{
			*Iter = Item.Next;
			Item.Next = IntervalTreeInvalidIndex;
			return true;
		}
		Iter = &Item.Next;
	}
	return false;
}

template<typename TItem>
static bool RemoveFromIntervalTree(
	TIntervalTreeIndex* RootNode,
	TIntervalTreeAllocator<TItem>& Allocator,
	TIntervalTreeIndex Index,
	uint64 MinInterval,
	uint64 MaxInterval,
	uint32 CurrentShift,
	uint32 MaxShift
)
{
	bool bResult = false;
	if (*RootNode != IntervalTreeInvalidIndex)
	{
		int64 MinShifted = HighBit(MinInterval << CurrentShift);
		int64 MaxShifted = HighBit(MaxInterval << CurrentShift);
		FIntervalTreeNode& Root = GIntervalTreeNodeNodeAllocator.Get(*RootNode);

		if (!MinShifted && !MaxShifted)
		{
			if (CurrentShift == MaxShift)
			{
				bResult = ScanNodeListForRemoval(&Root.LeftChildOrRootOfLeftList, Allocator, Index, MinInterval, MaxInterval);
			}
			else
			{
				bResult = RemoveFromIntervalTree(&Root.LeftChildOrRootOfLeftList, Allocator, Index, MinInterval, MaxInterval, CurrentShift + 1, MaxShift);
			}
		}
		else if (!MinShifted && MaxShifted)
		{
			bResult = ScanNodeListForRemoval(&Root.RootOfOnList, Allocator, Index, MinInterval, MaxInterval);
		}
		else
		{
			if (CurrentShift == MaxShift)
			{
				bResult = ScanNodeListForRemoval(&Root.RightChildOrRootOfRightList, Allocator, Index, MinInterval, MaxInterval);
			}
			else
			{
				bResult = RemoveFromIntervalTree(&Root.RightChildOrRootOfRightList, Allocator, Index, MinInterval, MaxInterval, CurrentShift + 1, MaxShift);
			}
		}
		if (bResult)
		{
			if (Root.LeftChildOrRootOfLeftList == IntervalTreeInvalidIndex && Root.RootOfOnList == IntervalTreeInvalidIndex && Root.RightChildOrRootOfRightList == IntervalTreeInvalidIndex)
			{
				check(&Root == &GIntervalTreeNodeNodeAllocator.Get(*RootNode));
				GIntervalTreeNodeNodeAllocator.Free(*RootNode);
				*RootNode = IntervalTreeInvalidIndex;
			}
		}
	}
	return bResult;
}

template<typename TItem>
static bool RemoveFromIntervalTree(
	TIntervalTreeIndex* RootNode,
	TIntervalTreeAllocator<TItem>& Allocator,
	TIntervalTreeIndex Index,
	uint32 StartShift,
	uint32 MaxShift
)
{
	TItem& Item = Allocator.Get(Index);
	uint64 MinInterval = GetRequestOffset(Item.OffsetAndPakIndex);
	uint64 MaxInterval = MinInterval + Item.Size - 1;
	return RemoveFromIntervalTree(RootNode, Allocator, Index, MinInterval, MaxInterval, StartShift, MaxShift);
}

template<typename TItem>
static FORCEINLINE void ScanNodeListForRemovalFunc(
	TIntervalTreeIndex* Iter,
	TIntervalTreeAllocator<TItem>& Allocator,
	uint64 MinInterval,
	uint64 MaxInterval,
	TFunctionRef<bool(TIntervalTreeIndex)> Func
)
{
	while (*Iter != IntervalTreeInvalidIndex)
	{
		TItem& Item = Allocator.Get(*Iter);
		uint64 Offset = uint64(GetRequestOffset(Item.OffsetAndPakIndex));
		uint64 LastByte = Offset + uint64(Item.Size) - 1;

		// save the value and then clear it.
		TIntervalTreeIndex NextIndex = Item.Next;
		if (IntervalsIntersect(MinInterval, MaxInterval, Offset, LastByte) && Func(*Iter))
		{
			*Iter = NextIndex; // this may have already be deleted, so cannot rely on the memory block
		}
		else
		{
			Iter = &Item.Next;
		}
	}
}

template<typename TItem>
static void MaybeRemoveOverlappingNodesInIntervalTree(
	TIntervalTreeIndex* RootNode,
	TIntervalTreeAllocator<TItem>& Allocator,
	uint64 MinInterval,
	uint64 MaxInterval,
	uint64 MinNode,
	uint64 MaxNode,
	uint32 CurrentShift,
	uint32 MaxShift,
	TFunctionRef<bool(TIntervalTreeIndex)> Func
)
{
	if (*RootNode != IntervalTreeInvalidIndex)
	{
		int64 MinShifted = HighBit(MinInterval << CurrentShift);
		int64 MaxShifted = HighBit(MaxInterval << CurrentShift);
		FIntervalTreeNode& Root = GIntervalTreeNodeNodeAllocator.Get(*RootNode);
		uint64 Center = (MinNode + MaxNode + 1) >> 1;

		//UE_LOG(LogTemp, Warning, TEXT("Exploring Node %X [%d, %d] %d%d     interval %llX %llX    node interval %llX %llX   center %llX  "), *RootNode, CurrentShift, MaxShift, !!MinShifted, !!MaxShifted, MinInterval, MaxInterval, MinNode, MaxNode, Center);


		if (!MinShifted)
		{
			if (CurrentShift == MaxShift)
			{
				//UE_LOG(LogTemp, Warning, TEXT("LeftBottom %X [%d, %d] %d%d"), *RootNode, CurrentShift, MaxShift, !!MinShifted, !!MaxShifted);
				ScanNodeListForRemovalFunc(&Root.LeftChildOrRootOfLeftList, Allocator, MinInterval, MaxInterval, Func);
			}
			else
			{
				//UE_LOG(LogTemp, Warning, TEXT("LeftRecur %X [%d, %d] %d%d"), *RootNode, CurrentShift, MaxShift, !!MinShifted, !!MaxShifted);
				MaybeRemoveOverlappingNodesInIntervalTree(&Root.LeftChildOrRootOfLeftList, Allocator, MinInterval, FMath::Min(MaxInterval, Center - 1), MinNode, Center - 1, CurrentShift + 1, MaxShift, Func);
			}
		}

		//UE_LOG(LogTemp, Warning, TEXT("Center %X [%d, %d] %d%d"), *RootNode, CurrentShift, MaxShift, !!MinShifted, !!MaxShifted);
		ScanNodeListForRemovalFunc(&Root.RootOfOnList, Allocator, MinInterval, MaxInterval, Func);

		if (MaxShifted)
		{
			if (CurrentShift == MaxShift)
			{
				//UE_LOG(LogTemp, Warning, TEXT("RightBottom %X [%d, %d] %d%d"), *RootNode, CurrentShift, MaxShift, !!MinShifted, !!MaxShifted);
				ScanNodeListForRemovalFunc(&Root.RightChildOrRootOfRightList, Allocator, MinInterval, MaxInterval, Func);
			}
			else
			{
				//UE_LOG(LogTemp, Warning, TEXT("RightRecur %X [%d, %d] %d%d"), *RootNode, CurrentShift, MaxShift, !!MinShifted, !!MaxShifted);
				MaybeRemoveOverlappingNodesInIntervalTree(&Root.RightChildOrRootOfRightList, Allocator, FMath::Max(MinInterval, Center), MaxInterval, Center, MaxNode, CurrentShift + 1, MaxShift, Func);
			}
		}

		//UE_LOG(LogTemp, Warning, TEXT("Done Exploring Node %X [%d, %d] %d%d     interval %llX %llX    node interval %llX %llX   center %llX  "), *RootNode, CurrentShift, MaxShift, !!MinShifted, !!MaxShifted, MinInterval, MaxInterval, MinNode, MaxNode, Center);

		if (Root.LeftChildOrRootOfLeftList == IntervalTreeInvalidIndex && Root.RootOfOnList == IntervalTreeInvalidIndex && Root.RightChildOrRootOfRightList == IntervalTreeInvalidIndex)
		{
			check(&Root == &GIntervalTreeNodeNodeAllocator.Get(*RootNode));
			GIntervalTreeNodeNodeAllocator.Free(*RootNode);
			*RootNode = IntervalTreeInvalidIndex;
		}
	}
}


template<typename TItem>
static FORCEINLINE bool ScanNodeList(
	TIntervalTreeIndex Iter,
	TIntervalTreeAllocator<TItem>& Allocator,
	uint64 MinInterval,
	uint64 MaxInterval,
	TFunctionRef<bool(TIntervalTreeIndex)> Func
)
{
	while (Iter != IntervalTreeInvalidIndex)
	{
		TItem& Item = Allocator.Get(Iter);
		uint64 Offset = uint64(GetRequestOffset(Item.OffsetAndPakIndex));
		uint64 LastByte = Offset + uint64(Item.Size) - 1;
		if (IntervalsIntersect(MinInterval, MaxInterval, Offset, LastByte))
		{
			if (!Func(Iter))
			{
				return false;
			}
		}
		Iter = Item.Next;
	}
	return true;
}

template<typename TItem>
static bool OverlappingNodesInIntervalTree(
	TIntervalTreeIndex RootNode,
	TIntervalTreeAllocator<TItem>& Allocator,
	uint64 MinInterval,
	uint64 MaxInterval,
	uint64 MinNode,
	uint64 MaxNode,
	uint32 CurrentShift,
	uint32 MaxShift,
	TFunctionRef<bool(TIntervalTreeIndex)> Func
)
{
	if (RootNode != IntervalTreeInvalidIndex)
	{
		int64 MinShifted = HighBit(MinInterval << CurrentShift);
		int64 MaxShifted = HighBit(MaxInterval << CurrentShift);
		FIntervalTreeNode& Root = GIntervalTreeNodeNodeAllocator.Get(RootNode);
		uint64 Center = (MinNode + MaxNode + 1) >> 1;

		if (!MinShifted)
		{
			if (CurrentShift == MaxShift)
			{
				if (!ScanNodeList(Root.LeftChildOrRootOfLeftList, Allocator, MinInterval, MaxInterval, Func))
				{
					return false;
				}
			}
			else
			{
				if (!OverlappingNodesInIntervalTree(Root.LeftChildOrRootOfLeftList, Allocator, MinInterval, FMath::Min(MaxInterval, Center - 1), MinNode, Center - 1, CurrentShift + 1, MaxShift, Func))
				{
					return false;
				}
			}
		}
		if (!ScanNodeList(Root.RootOfOnList, Allocator, MinInterval, MaxInterval, Func))
		{
			return false;
		}
		if (MaxShifted)
		{
			if (CurrentShift == MaxShift)
			{
				if (!ScanNodeList(Root.RightChildOrRootOfRightList, Allocator, MinInterval, MaxInterval, Func))
				{
					return false;
				}
			}
			else
			{
				if (!OverlappingNodesInIntervalTree(Root.RightChildOrRootOfRightList, Allocator, FMath::Max(MinInterval, Center), MaxInterval, Center, MaxNode, CurrentShift + 1, MaxShift, Func))
				{
					return false;
				}
			}
		}
	}
	return true;
}

template<typename TItem>
static bool ScanNodeListWithShrinkingInterval(
	TIntervalTreeIndex Iter,
	TIntervalTreeAllocator<TItem>& Allocator,
	uint64 MinInterval,
	uint64& MaxInterval,
	TFunctionRef<bool(TIntervalTreeIndex)> Func
)
{
	while (Iter != IntervalTreeInvalidIndex)
	{
		TItem& Item = Allocator.Get(Iter);
		uint64 Offset = uint64(GetRequestOffset(Item.OffsetAndPakIndex));
		uint64 LastByte = Offset + uint64(Item.Size) - 1;
		//UE_LOG(LogTemp, Warning, TEXT("Test Overlap %llu %llu %llu %llu"), MinInterval, MaxInterval, Offset, LastByte);
		if (IntervalsIntersect(MinInterval, MaxInterval, Offset, LastByte))
		{
			//UE_LOG(LogTemp, Warning, TEXT("Overlap %llu %llu %llu %llu"), MinInterval, MaxInterval, Offset, LastByte);
			if (!Func(Iter))
			{
				return false;
			}
		}
		Iter = Item.Next;
	}
	return true;
}

template<typename TItem>
static bool OverlappingNodesInIntervalTreeWithShrinkingInterval(
	TIntervalTreeIndex RootNode,
	TIntervalTreeAllocator<TItem>& Allocator,
	uint64 MinInterval,
	uint64& MaxInterval,
	uint64 MinNode,
	uint64 MaxNode,
	uint32 CurrentShift,
	uint32 MaxShift,
	TFunctionRef<bool(TIntervalTreeIndex)> Func
)
{
	if (RootNode != IntervalTreeInvalidIndex)
	{

		int64 MinShifted = HighBit(MinInterval << CurrentShift);
		int64 MaxShifted = HighBit(FMath::Min(MaxInterval, MaxNode) << CurrentShift); // since MaxInterval is changing, we cannot clamp it during recursion.
		FIntervalTreeNode& Root = GIntervalTreeNodeNodeAllocator.Get(RootNode);
		uint64 Center = (MinNode + MaxNode + 1) >> 1;

		if (!MinShifted)
		{
			if (CurrentShift == MaxShift)
			{
				if (!ScanNodeListWithShrinkingInterval(Root.LeftChildOrRootOfLeftList, Allocator, MinInterval, MaxInterval, Func))
				{
					return false;
				}
			}
			else
			{
				if (!OverlappingNodesInIntervalTreeWithShrinkingInterval(Root.LeftChildOrRootOfLeftList, Allocator, MinInterval, MaxInterval, MinNode, Center - 1, CurrentShift + 1, MaxShift, Func)) // since MaxInterval is changing, we cannot clamp it during recursion.
				{
					return false;
				}
			}
		}
		if (!ScanNodeListWithShrinkingInterval(Root.RootOfOnList, Allocator, MinInterval, MaxInterval, Func))
		{
			return false;
		}
		MaxShifted = HighBit(FMath::Min(MaxInterval, MaxNode) << CurrentShift); // since MaxInterval is changing, we cannot clamp it during recursion.
		if (MaxShifted)
		{
			if (CurrentShift == MaxShift)
			{
				if (!ScanNodeListWithShrinkingInterval(Root.RightChildOrRootOfRightList, Allocator, MinInterval, MaxInterval, Func))
				{
					return false;
				}
			}
			else
			{
				if (!OverlappingNodesInIntervalTreeWithShrinkingInterval(Root.RightChildOrRootOfRightList, Allocator, FMath::Max(MinInterval, Center), MaxInterval, Center, MaxNode, CurrentShift + 1, MaxShift, Func))
				{
					return false;
				}
			}
		}
	}
	return true;
}


template<typename TItem>
static void MaskInterval(
	TIntervalTreeIndex Index,
	TIntervalTreeAllocator<TItem>& Allocator,
	uint64 MinInterval,
	uint64 MaxInterval,
	uint32 BytesToBitsShift,
	uint64* Bits
)
{
	TItem& Item = Allocator.Get(Index);
	uint64 Offset = uint64(GetRequestOffset(Item.OffsetAndPakIndex));
	uint64 LastByte = Offset + uint64(Item.Size) - 1;
	uint64 InterMinInterval = FMath::Max(MinInterval, Offset);
	uint64 InterMaxInterval = FMath::Min(MaxInterval, LastByte);
	if (InterMinInterval <= InterMaxInterval)
	{
		uint32 FirstBit = uint32((InterMinInterval - MinInterval) >> BytesToBitsShift);
		uint32 LastBit = uint32((InterMaxInterval - MinInterval) >> BytesToBitsShift);
		uint32 FirstQWord = FirstBit >> 6;
		uint32 LastQWord = LastBit >> 6;
		uint32 FirstBitQWord = FirstBit & 63;
		uint32 LastBitQWord = LastBit & 63;
		if (FirstQWord == LastQWord)
		{
			Bits[FirstQWord] |= ((MAX_uint64 << FirstBitQWord) & (MAX_uint64 >> (63 - LastBitQWord)));
		}
		else
		{
			Bits[FirstQWord] |= (MAX_uint64 << FirstBitQWord);
			for (uint32 QWordIndex = FirstQWord + 1; QWordIndex < LastQWord; QWordIndex++)
			{
				Bits[QWordIndex] = MAX_uint64;
			}
			Bits[LastQWord] |= (MAX_uint64 >> (63 - LastBitQWord));
		}
	}
}



template<typename TItem>
static void OverlappingNodesInIntervalTreeMask(
	TIntervalTreeIndex RootNode,
	TIntervalTreeAllocator<TItem>& Allocator,
	uint64 MinInterval,
	uint64 MaxInterval,
	uint64 MinNode,
	uint64 MaxNode,
	uint32 CurrentShift,
	uint32 MaxShift,
	uint32 BytesToBitsShift,
	uint64* Bits
)
{
	OverlappingNodesInIntervalTree(
		RootNode,
		Allocator,
		MinInterval,
		MaxInterval,
		MinNode,
		MaxNode,
		CurrentShift,
		MaxShift,
		[&Allocator, MinInterval, MaxInterval, BytesToBitsShift, Bits](TIntervalTreeIndex Index) -> bool
	{
		MaskInterval(Index, Allocator, MinInterval, MaxInterval, BytesToBitsShift, Bits);
		return true;
	}
	);
}



class IPakRequestor
{
	friend class FPakPrecacher;
	FJoinedOffsetAndPakIndex OffsetAndPakIndex; // this is used for searching and filled in when you make the request
	uint64 UniqueID;
	TIntervalTreeIndex InRequestIndex;
public:
	IPakRequestor()
		: OffsetAndPakIndex(MAX_uint64) // invalid value
		, UniqueID(0)
		, InRequestIndex(IntervalTreeInvalidIndex)
	{
	}
	virtual ~IPakRequestor()
	{
	}
	virtual void RequestIsComplete()
	{
	}
};

static FPakPrecacher* PakPrecacherSingleton = nullptr;

class FPakPrecacher
{
	enum class EInRequestStatus
	{
		Complete,
		Waiting,
		InFlight,
		Num
	};

	enum class EBlockStatus
	{
		InFlight,
		Complete,
		Num
	};

	IPlatformFile* LowerLevel;
	FCriticalSection CachedFilesScopeLock;
	FJoinedOffsetAndPakIndex LastReadRequest;
	uint64 NextUniqueID;
	int64 BlockMemory;
	int64 BlockMemoryHighWater;
	FThreadSafeCounter RequestCounter;

	struct FCacheBlock
	{
		FJoinedOffsetAndPakIndex OffsetAndPakIndex;
		int64 Size;
		uint8 *Memory;
		uint32 InRequestRefCount;
		TIntervalTreeIndex Index;
		TIntervalTreeIndex Next;
		EBlockStatus Status;
		double TimeNoLongerReferenced;

		FCacheBlock()
			: OffsetAndPakIndex(0)
			, Size(0)
			, Memory(nullptr)
			, InRequestRefCount(0)
			, Index(IntervalTreeInvalidIndex)
			, Next(IntervalTreeInvalidIndex)
			, Status(EBlockStatus::InFlight)
			, TimeNoLongerReferenced(0)
		{
		}
	};

	struct FPakInRequest
	{
		FJoinedOffsetAndPakIndex OffsetAndPakIndex;
		int64 Size;
		IPakRequestor* Owner;
		uint64 UniqueID;
		TIntervalTreeIndex Index;
		TIntervalTreeIndex Next;
		EAsyncIOPriorityAndFlags PriorityAndFlags;
		EInRequestStatus Status;

		FPakInRequest()
			: OffsetAndPakIndex(0)
			, Size(0)
			, Owner(nullptr)
			, UniqueID(0)
			, Index(IntervalTreeInvalidIndex)
			, Next(IntervalTreeInvalidIndex)
			, PriorityAndFlags(AIOP_MIN)
			, Status(EInRequestStatus::Waiting)
		{
		}

		EAsyncIOPriorityAndFlags GetPriority() const
		{
			return PriorityAndFlags & AIOP_PRIORITY_MASK;
		}
	};

	struct FPakData
	{
		IAsyncReadFileHandle* Handle;
		FPakFile* ActualPakFile;
		int64 TotalSize;
		uint64 MaxNode;
		uint32 StartShift;
		uint32 MaxShift;
		uint32 BytesToBitsShift;
		FName Name;

		TIntervalTreeIndex InRequests[AIOP_NUM][(int32)EInRequestStatus::Num];
		TIntervalTreeIndex CacheBlocks[(int32)EBlockStatus::Num];

		TSharedPtr<const FPakSignatureFile, ESPMode::ThreadSafe> Signatures;

		FPakData(FPakFile* InActualPakFile, IAsyncReadFileHandle* InHandle, FName InName, int64 InTotalSize)
			: Handle(InHandle)
			, ActualPakFile(InActualPakFile)
			, TotalSize(InTotalSize)
			, StartShift(0)
			, MaxShift(0)
			, BytesToBitsShift(0)
			, Name(InName)
			, Signatures(nullptr)
		{
			check(Handle && TotalSize > 0 && Name != NAME_None);
			for (int32 Index = 0; Index < AIOP_NUM; Index++)
			{
				for (int32 IndexInner = 0; IndexInner < (int32)EInRequestStatus::Num; IndexInner++)
				{
					InRequests[Index][IndexInner] = IntervalTreeInvalidIndex;
				}
			}
			for (int32 IndexInner = 0; IndexInner < (int32)EBlockStatus::Num; IndexInner++)
			{
				CacheBlocks[IndexInner] = IntervalTreeInvalidIndex;
			}
			uint64 StartingLastByte = FMath::Max((uint64)TotalSize, uint64(PAK_CACHE_GRANULARITY + 1));
			StartingLastByte--;

			{
				uint64 LastByte = StartingLastByte;
				while (!HighBit(LastByte))
				{
					LastByte <<= 1;
					StartShift++;
				}
			}
			{
				uint64 LastByte = StartingLastByte;
				uint64 Block = (uint64)PAK_CACHE_GRANULARITY;

				while (Block)
				{
					Block >>= 1;
					LastByte >>= 1;
					BytesToBitsShift++;
				}
				BytesToBitsShift--;
				check(1 << BytesToBitsShift == PAK_CACHE_GRANULARITY);
				MaxShift = StartShift;
				while (LastByte)
				{
					LastByte >>= 1;
					MaxShift++;
				}
				MaxNode = MAX_uint64 >> StartShift;
				check(MaxNode >= StartingLastByte && (MaxNode >> 1) < StartingLastByte);
				//				UE_LOG(LogTemp, Warning, TEXT("Test %d %llX %llX "), MaxShift, (uint64(PAK_CACHE_GRANULARITY) << (MaxShift + 1)), (uint64(PAK_CACHE_GRANULARITY) << MaxShift));
				check(MaxShift && (uint64(PAK_CACHE_GRANULARITY) << (MaxShift + 1)) == 0 && (uint64(PAK_CACHE_GRANULARITY) << MaxShift) != 0);
			}
		}
	};
	TMap<FPakFile*, uint16> CachedPaks;
	TArray<FPakData> CachedPakData;

	TIntervalTreeAllocator<FPakInRequest> InRequestAllocator;
	TIntervalTreeAllocator<FCacheBlock> CacheBlockAllocator;
	TMap<uint64, TIntervalTreeIndex> OutstandingRequests;

	TArray < TArray<FJoinedOffsetAndPakIndex>> OffsetAndPakIndexOfSavedBlocked;

	struct FRequestToLower
	{
		IAsyncReadRequest* RequestHandle;
		TIntervalTreeIndex BlockIndex;
		int64 RequestSize;
		uint8* Memory;
		FRequestToLower()
			: RequestHandle(nullptr)
			, BlockIndex(IntervalTreeInvalidIndex)
			, RequestSize(0)
			, Memory(nullptr)
		{
		}
	};

	FRequestToLower RequestsToLower[PAK_CACHE_MAX_REQUESTS];
	TArray<IAsyncReadRequest*> RequestsToDelete;
	int32 NotifyRecursion;

	uint32 Loads;
	uint32 Frees;
	uint64 LoadSize;
	EAsyncIOPriorityAndFlags AsyncMinPriority;
	FCriticalSection SetAsyncMinimumPriorityScopeLock;
	bool bEnableSignatureChecks;
public:
	int64 GetBlockMemory() { return BlockMemory; }
	int64 GetBlockMemoryHighWater() { return BlockMemoryHighWater; }

	static void Init(IPlatformFile* InLowerLevel, bool bInEnableSignatureChecks) 
	{
		if (!PakPrecacherSingleton)
		{
			verify(!FPlatformAtomics::InterlockedCompareExchangePointer((void**)& PakPrecacherSingleton, new FPakPrecacher(InLowerLevel, bInEnableSignatureChecks), nullptr));
		}
		check(PakPrecacherSingleton);
	}

	static void Shutdown()
	{
		if (PakPrecacherSingleton)
		{
			FPakPrecacher* LocalPakPrecacherSingleton = PakPrecacherSingleton;
			if (LocalPakPrecacherSingleton && LocalPakPrecacherSingleton == FPlatformAtomics::InterlockedCompareExchangePointer((void**)&PakPrecacherSingleton, nullptr, LocalPakPrecacherSingleton))
			{
				LocalPakPrecacherSingleton->TrimCache(true);
				double StartTime = FPlatformTime::Seconds();
				while (!LocalPakPrecacherSingleton->IsProbablyIdle())
				{
					FPlatformProcess::SleepNoStats(0.001f);
					if (FPlatformTime::Seconds() - StartTime > 10.0)
					{
						UE_LOG(LogPakFile, Error, TEXT("FPakPrecacher was not idle after 10s, exiting anyway and leaking."));
						return;
					}
				}
				delete PakPrecacherSingleton;
				PakPrecacherSingleton = nullptr;
			}
		}
		check(!PakPrecacherSingleton);
	}

	static FPakPrecacher& Get()
	{
		check(PakPrecacherSingleton);
		return *PakPrecacherSingleton;
	}

	FPakPrecacher(IPlatformFile* InLowerLevel, bool bInEnableSignatureChecks) 
		: LowerLevel(InLowerLevel)
		, LastReadRequest(0)
		, NextUniqueID(1)
		, BlockMemory(0)
		, BlockMemoryHighWater(0)
		, NotifyRecursion(0)
		, Loads(0)
		, Frees(0)
		, LoadSize(0)
		, AsyncMinPriority(AIOP_MIN)
		, bEnableSignatureChecks(bInEnableSignatureChecks)
	{
		check(LowerLevel && FPlatformProcess::SupportsMultithreading());
//		GPakCache_MaxRequestsToLowerLevel = FMath::Max(FMath::Min(FPlatformMisc::NumberOfIOWorkerThreadsToSpawn(), GPakCache_MaxRequestsToLowerLevel), 1);
		check(GPakCache_MaxRequestsToLowerLevel <= PAK_CACHE_MAX_REQUESTS);
	}

	void StartSignatureCheck(bool bWasCanceled, IAsyncReadRequest* Request, int32 IndexToFill);
	void DoSignatureCheck(bool bWasCanceled, IAsyncReadRequest* Request, int32 IndexToFill);

	int32 GetRequestCount() const
	{
		return RequestCounter.GetValue();
	}

	IPlatformFile* GetLowerLevelHandle()
	{
		check(LowerLevel);
		return LowerLevel;
	}

	uint16* RegisterPakFile(FPakFile* InActualPakFile, FName File, int64 PakFileSize)
	{
		uint16* PakIndexPtr = CachedPaks.Find(InActualPakFile);

		if (!PakIndexPtr)
		{
			FString PakFilename = File.ToString();
			check(CachedPakData.Num() < MAX_uint16);
			IAsyncReadFileHandle* Handle = LowerLevel->OpenAsyncRead(*PakFilename);
			if (!Handle)
			{
				return nullptr;
			}
			CachedPakData.Add(FPakData(InActualPakFile, Handle, File, PakFileSize));
			PakIndexPtr = &CachedPaks.Add(InActualPakFile, CachedPakData.Num() - 1);
			FPakData& Pak = CachedPakData[*PakIndexPtr];


			if (OffsetAndPakIndexOfSavedBlocked.Num() == 0)
			{
				// the 1st cache must exist and is used by all sharing pak files
				OffsetAndPakIndexOfSavedBlocked.AddDefaulted(1);
			}

			static bool bFirst = true;
			if (bFirst)
			{
				if (FParse::Param(FCommandLine::Get(), TEXT("CachePerPak")))
				{
					GPakCache_CachePerPakFile = 1;
				}

				if (FParse::Param(FCommandLine::Get(), TEXT("NewTrimCache")))
				{
					GPakCache_UseNewTrim = 1;
				}
				FParse::Value(FCommandLine::Get(), TEXT("PakCacheMaxBlockMemory="), GPakCache_MaxBlockMemory);
				bFirst = false;
			}

			if (Pak.ActualPakFile->GetCacheType() == FPakFile::ECacheType::Individual || GPakCache_CachePerPakFile != 0 )
			{
				Pak.ActualPakFile->SetCacheIndex(OffsetAndPakIndexOfSavedBlocked.Num());
				OffsetAndPakIndexOfSavedBlocked.AddDefaulted(1);
			}
			else
			{
				Pak.ActualPakFile->SetCacheIndex(0);
			}

			UE_LOG(LogPakFile, Log, TEXT("New pak file %s added to pak precacher."), *PakFilename);

			// Load signature data
			Pak.Signatures = FPakPlatformFile::GetPakSignatureFile(*PakFilename);

			if (Pak.Signatures.IsValid())
			{
				// We should never get here unless the signature file exists and is validated. The original FPakFile creation
				// on the main thread would have failed and the pak would never have been mounted otherwise, and then we would
				// never have issued read requests to the pak precacher.
				check(Pak.Signatures);
				
				// Check that we have the correct match between signature and pre-cache granularity
				int64 NumPakChunks = Align(PakFileSize, FPakInfo::MaxChunkDataSize) / FPakInfo::MaxChunkDataSize;
				ensure(NumPakChunks == Pak.Signatures->ChunkHashes.Num());
			}
		}
		return PakIndexPtr;
	}

#if !UE_BUILD_SHIPPING
	void SimulatePakFileCorruption()
	{
		FScopeLock Lock(&CachedFilesScopeLock);

		for (FPakData& PakData : CachedPakData)
		{
			for (const TPakChunkHash& Hash : PakData.Signatures->ChunkHashes)
			{
				*((uint8*)&Hash) |= 0x1;
			}
		}
	}
#endif

private: // below here we assume CachedFilesScopeLock until we get to the next section

	uint16 GetRequestPakIndex(FJoinedOffsetAndPakIndex OffsetAndPakIndex)
	{
		uint16 Result = GetRequestPakIndexLow(OffsetAndPakIndex);
		check(Result < CachedPakData.Num());
		return Result;
	}

	FJoinedOffsetAndPakIndex FirstUnfilledBlockForRequest(TIntervalTreeIndex NewIndex, FJoinedOffsetAndPakIndex ReadHead = 0)
	{
		// CachedFilesScopeLock is locked
		FPakInRequest& Request = InRequestAllocator.Get(NewIndex);
		uint16 PakIndex = GetRequestPakIndex(Request.OffsetAndPakIndex);
		int64 Offset = GetRequestOffset(Request.OffsetAndPakIndex);
		int64 Size = Request.Size;
		FPakData& Pak = CachedPakData[PakIndex];
		check(Offset + Request.Size <= Pak.TotalSize && Size > 0 && Request.GetPriority() >= AIOP_MIN && Request.GetPriority() <= AIOP_MAX && Request.Status != EInRequestStatus::Complete && Request.Owner);
		if (PakIndex != GetRequestPakIndex(ReadHead))
		{
			// this is in a different pak, so we ignore the read head position
			ReadHead = 0;
		}
		if (ReadHead)
		{
			// trim to the right of the read head
			int64 Trim = FMath::Max(Offset, GetRequestOffset(ReadHead)) - Offset;
			Offset += Trim;
			Size -= Trim;
		}

		static TArray<uint64> InFlightOrDone;

		int64 FirstByte = AlignDown(Offset, PAK_CACHE_GRANULARITY);
		int64 LastByte = Align(Offset + Size, PAK_CACHE_GRANULARITY) - 1;
		uint32 NumBits = (PAK_CACHE_GRANULARITY + LastByte - FirstByte) / PAK_CACHE_GRANULARITY;
		uint32 NumQWords = (NumBits + 63) >> 6;
		InFlightOrDone.Reset();
		InFlightOrDone.AddZeroed(NumQWords);
		if (NumBits != NumQWords * 64)
		{
			uint32 Extras = NumQWords * 64 - NumBits;
			InFlightOrDone[NumQWords - 1] = (MAX_uint64 << (64 - Extras));
		}

		if (Pak.CacheBlocks[(int32)EBlockStatus::Complete] != IntervalTreeInvalidIndex)
		{
			OverlappingNodesInIntervalTreeMask<FCacheBlock>(
				Pak.CacheBlocks[(int32)EBlockStatus::Complete],
				CacheBlockAllocator,
				FirstByte,
				LastByte,
				0,
				Pak.MaxNode,
				Pak.StartShift,
				Pak.MaxShift,
				Pak.BytesToBitsShift,
				&InFlightOrDone[0]
				);
		}
		if (Request.Status == EInRequestStatus::Waiting && Pak.CacheBlocks[(int32)EBlockStatus::InFlight] != IntervalTreeInvalidIndex)
		{
			OverlappingNodesInIntervalTreeMask<FCacheBlock>(
				Pak.CacheBlocks[(int32)EBlockStatus::InFlight],
				CacheBlockAllocator,
				FirstByte,
				LastByte,
				0,
				Pak.MaxNode,
				Pak.StartShift,
				Pak.MaxShift,
				Pak.BytesToBitsShift,
				&InFlightOrDone[0]
				);
		}
		for (uint32 Index = 0; Index < NumQWords; Index++)
		{
			if (InFlightOrDone[Index] != MAX_uint64)
			{
				uint64 Mask = InFlightOrDone[Index];
				int64 FinalOffset = FirstByte + PAK_CACHE_GRANULARITY * 64 * Index;
				while (Mask & 1)
				{
					FinalOffset += PAK_CACHE_GRANULARITY;
					Mask >>= 1;
				}
				return MakeJoinedRequest(PakIndex, FinalOffset);
			}
		}
		return MAX_uint64;
	}

	bool AddRequest(FPakInRequest& Request, TIntervalTreeIndex NewIndex)
	{
		// CachedFilesScopeLock is locked
		uint16 PakIndex = GetRequestPakIndex(Request.OffsetAndPakIndex);
		int64 Offset = GetRequestOffset(Request.OffsetAndPakIndex);
		FPakData& Pak = CachedPakData[PakIndex];
		check(Offset + Request.Size <= Pak.TotalSize && Request.Size > 0 && Request.GetPriority() >= AIOP_MIN && Request.GetPriority() <= AIOP_MAX && Request.Status == EInRequestStatus::Waiting && Request.Owner);

		static TArray<uint64> InFlightOrDone;

		int64 FirstByte = AlignDown(Offset, PAK_CACHE_GRANULARITY);
		int64 LastByte = Align(Offset + Request.Size, PAK_CACHE_GRANULARITY) - 1;
		uint32 NumBits = (PAK_CACHE_GRANULARITY + LastByte - FirstByte) / PAK_CACHE_GRANULARITY;
		uint32 NumQWords = (NumBits + 63) >> 6;
		InFlightOrDone.Reset();
		InFlightOrDone.AddZeroed(NumQWords);
		if (NumBits != NumQWords * 64)
		{
			uint32 Extras = NumQWords * 64 - NumBits;
			InFlightOrDone[NumQWords - 1] = (MAX_uint64 << (64 - Extras));
		}

		if (Pak.CacheBlocks[(int32)EBlockStatus::Complete] != IntervalTreeInvalidIndex)
		{
			Request.Status = EInRequestStatus::Complete;
			OverlappingNodesInIntervalTree<FCacheBlock>(
				Pak.CacheBlocks[(int32)EBlockStatus::Complete],
				CacheBlockAllocator,
				FirstByte,
				LastByte,
				0,
				Pak.MaxNode,
				Pak.StartShift,
				Pak.MaxShift,
				[this, &Pak, FirstByte, LastByte](TIntervalTreeIndex Index) -> bool
			{
				CacheBlockAllocator.Get(Index).InRequestRefCount++;
				MaskInterval(Index, CacheBlockAllocator, FirstByte, LastByte, Pak.BytesToBitsShift, &InFlightOrDone[0]);
				return true;
			}
			);
			for (uint32 Index = 0; Index < NumQWords; Index++)
			{
				if (InFlightOrDone[Index] != MAX_uint64)
				{
					Request.Status = EInRequestStatus::Waiting;
					break;
				}
			}
		}

		if (Request.Status == EInRequestStatus::Waiting)
		{
			if (Pak.CacheBlocks[(int32)EBlockStatus::InFlight] != IntervalTreeInvalidIndex)
			{
				Request.Status = EInRequestStatus::InFlight;
				OverlappingNodesInIntervalTree<FCacheBlock>(
					Pak.CacheBlocks[(int32)EBlockStatus::InFlight],
					CacheBlockAllocator,
					FirstByte,
					LastByte,
					0,
					Pak.MaxNode,
					Pak.StartShift,
					Pak.MaxShift,
					[this, &Pak, FirstByte, LastByte](TIntervalTreeIndex Index) -> bool
				{
					CacheBlockAllocator.Get(Index).InRequestRefCount++;
					MaskInterval(Index, CacheBlockAllocator, FirstByte, LastByte, Pak.BytesToBitsShift, &InFlightOrDone[0]);
					return true;
				}
				);

				for (uint32 Index = 0; Index < NumQWords; Index++)
				{
					if (InFlightOrDone[Index] != MAX_uint64)
					{
						Request.Status = EInRequestStatus::Waiting;
						break;
					}
				}
			}
		}
		else
		{
#if PAK_EXTRA_CHECKS
			OverlappingNodesInIntervalTree<FCacheBlock>(
				Pak.CacheBlocks[(int32)EBlockStatus::InFlight],
				CacheBlockAllocator,
				FirstByte,
				LastByte,
				0,
				Pak.MaxNode,
				Pak.StartShift,
				Pak.MaxShift,
				[this, &Pak, FirstByte, LastByte](TIntervalTreeIndex Index) -> bool
			{
				check(0); // if we are complete, then how come there are overlapping in flight blocks?
				return true;
			}
			);
#endif
		}
		{
			AddToIntervalTree<FPakInRequest>(
				&Pak.InRequests[Request.GetPriority()][(int32)Request.Status],
				InRequestAllocator,
				NewIndex,
				Pak.StartShift,
				Pak.MaxShift
				);
		}
		check(&Request == &InRequestAllocator.Get(NewIndex));
		if (Request.Status == EInRequestStatus::Complete)
		{
			NotifyComplete(NewIndex);
			return true;
		}
		else if (Request.Status == EInRequestStatus::Waiting)
		{
			StartNextRequest();
		}
		return false;
	}

	void ClearBlock(FCacheBlock &Block)
	{
		UE_LOG(LogPakFile, Verbose, TEXT("FPakReadRequest[%016llX, %016llX) ClearBlock"), Block.OffsetAndPakIndex, Block.OffsetAndPakIndex + Block.Size);

		if (Block.Memory)
		{
			check(Block.Size);
			BlockMemory -= Block.Size;
			DEC_MEMORY_STAT_BY(STAT_PakCacheMem, Block.Size);
			check(BlockMemory >= 0);

			FMemory::Free(Block.Memory);
			Block.Memory = nullptr;
		}
		Block.Next = IntervalTreeInvalidIndex;
		CacheBlockAllocator.Free(Block.Index);
	}

	void ClearRequest(FPakInRequest& DoneRequest)
	{
		uint64 Id = DoneRequest.UniqueID;
		TIntervalTreeIndex Index = DoneRequest.Index;

		DoneRequest.OffsetAndPakIndex = 0;
		DoneRequest.Size = 0;
		DoneRequest.Owner = nullptr;
		DoneRequest.UniqueID = 0;
		DoneRequest.Index = IntervalTreeInvalidIndex;
		DoneRequest.Next = IntervalTreeInvalidIndex;
		DoneRequest.PriorityAndFlags = AIOP_MIN;
		DoneRequest.Status = EInRequestStatus::Num;

		verify(OutstandingRequests.Remove(Id) == 1);
		RequestCounter.Decrement();
		InRequestAllocator.Free(Index);
	}

	void TrimCache(bool bDiscardAll = false, uint16 StartPakIndex = 65535)
	{

		if (GPakCache_UseNewTrim && !bDiscardAll)
		{
			StartPakIndex = 0;
			uint16 EndPakIndex = CachedPakData.Num();

			// TODO: remove this array, add a bool to the cache object
			TArray<bool> CacheVisitedAlready;
			CacheVisitedAlready.AddDefaulted(OffsetAndPakIndexOfSavedBlocked.Num());

			int64 MemoryBudget = GPakCache_MaxBlockMemory * (1024 * 1024);
			bool AlreadyRemovedBlocksBecauseOfMemoryOverage = false;

			while (BlockMemory > MemoryBudget)
			{
				for (int i = 0; i < CacheVisitedAlready.Num(); i++)
				{
					CacheVisitedAlready[i] = false;
				}
				// if we iterate over all the pak files and caches and can't remove something then we'll break out of the while.
				bool NoneToRemove = true;
				// CachedFilesScopeLock is locked
				for (uint16 RealPakIndex = StartPakIndex; RealPakIndex < EndPakIndex; RealPakIndex++)
				{
					int32 CacheIndex = CachedPakData[RealPakIndex].ActualPakFile->GetCacheIndex();
					if (CacheVisitedAlready[CacheIndex] == true)
						continue;
					CacheVisitedAlready[CacheIndex] = true;

					int32 NumToKeep = bDiscardAll ? 0 : GPakCache_NumUnreferencedBlocksToCache;
					int32 NumToRemove = FMath::Max<int32>(0, OffsetAndPakIndexOfSavedBlocked[CacheIndex].Num() - NumToKeep);
					if (!bDiscardAll)
						NumToRemove = 1;

					if (NumToRemove && OffsetAndPakIndexOfSavedBlocked[CacheIndex].Num())
					{
						NoneToRemove = false;
						for (int32 Index = 0; Index < NumToRemove; Index++)
						{
							FJoinedOffsetAndPakIndex OffsetAndPakIndex = OffsetAndPakIndexOfSavedBlocked[CacheIndex][Index];
							uint16 PakIndex = GetRequestPakIndex(OffsetAndPakIndex);
							int64 Offset = GetRequestOffset(OffsetAndPakIndex);
							FPakData& Pak = CachedPakData[PakIndex];
							MaybeRemoveOverlappingNodesInIntervalTree<FCacheBlock>(
								&Pak.CacheBlocks[(int32)EBlockStatus::Complete],
								CacheBlockAllocator,
								Offset,
								Offset,
								0,
								Pak.MaxNode,
								Pak.StartShift,
								Pak.MaxShift,
								[this](TIntervalTreeIndex BlockIndex) -> bool
							{
								FCacheBlock &Block = CacheBlockAllocator.Get(BlockIndex);
								if (!Block.InRequestRefCount)
								{
									UE_LOG(LogPakFile, Verbose, TEXT("FPakReadRequest[%016llX, %016llX) Discard Cached"), Block.OffsetAndPakIndex, Block.OffsetAndPakIndex + Block.Size);
									ClearBlock(Block);
									return true;
								}
								return false;
							}
							);
						}
						OffsetAndPakIndexOfSavedBlocked[CacheIndex].RemoveAt(0, NumToRemove, false);
						AlreadyRemovedBlocksBecauseOfMemoryOverage = true;
					}
				}
				if (NoneToRemove)
				{
					break;
				}
			}
			if (GPakCache_TimeToTrim != 0.0f)
			{
				// we'll trim based on time rather than trying to keep within a memory budget
				double CurrentTime = FPlatformTime::Seconds();
				// CachedFilesScopeLock is locked
				for (uint16 RealPakIndex = StartPakIndex; RealPakIndex < EndPakIndex; RealPakIndex++)
				{
					int32 CacheIndex = CachedPakData[RealPakIndex].ActualPakFile->GetCacheIndex();

					int32 NumToRemove = 0;

					if (OffsetAndPakIndexOfSavedBlocked[CacheIndex].Num())
					{
						for (int32 Index = 0; Index < OffsetAndPakIndexOfSavedBlocked[CacheIndex].Num(); Index++)
						{
							FJoinedOffsetAndPakIndex OffsetAndPakIndex = OffsetAndPakIndexOfSavedBlocked[CacheIndex][Index];
							uint16 PakIndex = GetRequestPakIndex(OffsetAndPakIndex);
							int64 Offset = GetRequestOffset(OffsetAndPakIndex);
							FPakData& Pak = CachedPakData[PakIndex];
							bool bRemovedAll = true;
							MaybeRemoveOverlappingNodesInIntervalTree<FCacheBlock>(
								&Pak.CacheBlocks[(int32)EBlockStatus::Complete],
								CacheBlockAllocator,
								Offset,
								Offset,
								0,
								Pak.MaxNode,
								Pak.StartShift,
								Pak.MaxShift,
								[this, CurrentTime, &bRemovedAll](TIntervalTreeIndex BlockIndex) -> bool
							{
								FCacheBlock &Block = CacheBlockAllocator.Get(BlockIndex);
								if (!Block.InRequestRefCount && (CurrentTime - Block.TimeNoLongerReferenced >= GPakCache_TimeToTrim))
								{
									UE_LOG(LogPakFile, Verbose, TEXT("FPakReadRequest[%016llX, %016llX) Discard Cached Based on Time"), Block.OffsetAndPakIndex, Block.OffsetAndPakIndex + Block.Size);
									ClearBlock(Block);
									return true;
								}
								bRemovedAll = false;
								return false;
							}
							);
							if (!bRemovedAll)
								break;
							NumToRemove++;
						}
						if (NumToRemove)
						{
							OffsetAndPakIndexOfSavedBlocked[CacheIndex].RemoveAt(0, NumToRemove, false);
						}
					}
				}
			}
		}
		else
		{
			uint16 EndPakIndex = 65535;
			if (StartPakIndex != 65535)
			{
				EndPakIndex = StartPakIndex + 1;
			}
			else
			{
				StartPakIndex = 0;
				EndPakIndex = CachedPakData.Num();
			}

			// CachedFilesScopeLock is locked
			for (uint16 RealPakIndex = StartPakIndex; RealPakIndex < EndPakIndex; RealPakIndex++)
			{
				int32 CacheIndex = CachedPakData[RealPakIndex].ActualPakFile->GetCacheIndex();
				int32 NumToKeep = bDiscardAll ? 0 : GPakCache_NumUnreferencedBlocksToCache;
				int32 NumToRemove = FMath::Max<int32>(0, OffsetAndPakIndexOfSavedBlocked[CacheIndex].Num() - NumToKeep);
				if (NumToRemove)
				{
					for (int32 Index = 0; Index < NumToRemove; Index++)
					{
						FJoinedOffsetAndPakIndex OffsetAndPakIndex = OffsetAndPakIndexOfSavedBlocked[CacheIndex][Index];
						uint16 PakIndex = GetRequestPakIndex(OffsetAndPakIndex);
						int64 Offset = GetRequestOffset(OffsetAndPakIndex);
						FPakData& Pak = CachedPakData[PakIndex];
						MaybeRemoveOverlappingNodesInIntervalTree<FCacheBlock>(
							&Pak.CacheBlocks[(int32)EBlockStatus::Complete],
							CacheBlockAllocator,
							Offset,
							Offset,
							0,
							Pak.MaxNode,
							Pak.StartShift,
							Pak.MaxShift,
							[this](TIntervalTreeIndex BlockIndex) -> bool
						{
							FCacheBlock &Block = CacheBlockAllocator.Get(BlockIndex);
							if (!Block.InRequestRefCount)
							{
								UE_LOG(LogPakFile, Verbose, TEXT("FPakReadRequest[%016llX, %016llX) Discard Cached"), Block.OffsetAndPakIndex, Block.OffsetAndPakIndex + Block.Size);
								ClearBlock(Block);
								return true;
							}
							return false;
						}
						);
					}
					OffsetAndPakIndexOfSavedBlocked[CacheIndex].RemoveAt(0, NumToRemove, false);
				}
			}
		}
	}

	void RemoveRequest(TIntervalTreeIndex Index)
	{
		// CachedFilesScopeLock is locked
		FPakInRequest& Request = InRequestAllocator.Get(Index);
		uint16 PakIndex = GetRequestPakIndex(Request.OffsetAndPakIndex);
		int64 Offset = GetRequestOffset(Request.OffsetAndPakIndex);
		int64 Size = Request.Size;
		FPakData& Pak = CachedPakData[PakIndex];
		check(Offset + Request.Size <= Pak.TotalSize && Request.Size > 0 && Request.GetPriority() >= AIOP_MIN && Request.GetPriority() <= AIOP_MAX && int32(Request.Status) >= 0 && int32(Request.Status) < int32(EInRequestStatus::Num));

		bool RequestDontCache = (Request.PriorityAndFlags & AIOP_FLAG_DONTCACHE) != 0;


		if (RemoveFromIntervalTree<FPakInRequest>(&Pak.InRequests[Request.GetPriority()][(int32)Request.Status], InRequestAllocator, Index, Pak.StartShift, Pak.MaxShift))
		{

			int64 OffsetOfLastByte = Offset + Size - 1;
			MaybeRemoveOverlappingNodesInIntervalTree<FCacheBlock>(
				&Pak.CacheBlocks[(int32)EBlockStatus::Complete],
				CacheBlockAllocator,
				Offset,
				OffsetOfLastByte,
				0,
				Pak.MaxNode,
				Pak.StartShift,
				Pak.MaxShift,
				[this, OffsetOfLastByte, RequestDontCache](TIntervalTreeIndex BlockIndex) -> bool
			{
				FCacheBlock &Block = CacheBlockAllocator.Get(BlockIndex);
				check(Block.InRequestRefCount);
				if (!--Block.InRequestRefCount)
				{
					if (GPakCache_NumUnreferencedBlocksToCache && GetRequestOffset(Block.OffsetAndPakIndex) + Block.Size > OffsetOfLastByte) // last block
					{
						if (RequestDontCache && GPakCache_EnableNoCaching != 0)
						{
							uint16 BlocksPakIndex = GetRequestPakIndexLow(Block.OffsetAndPakIndex);
							int32 BlocksCacheIndex = CachedPakData[BlocksPakIndex].ActualPakFile->GetCacheIndex();
							Block.TimeNoLongerReferenced = 0.0;
							OffsetAndPakIndexOfSavedBlocked[BlocksCacheIndex].Remove(Block.OffsetAndPakIndex);
							ClearBlock(Block);
							return true;
						}
						else
						{
							uint16 BlocksPakIndex = GetRequestPakIndexLow(Block.OffsetAndPakIndex);
							int32 BlocksCacheIndex = CachedPakData[BlocksPakIndex].ActualPakFile->GetCacheIndex();
							Block.TimeNoLongerReferenced = FPlatformTime::Seconds();
							OffsetAndPakIndexOfSavedBlocked[BlocksCacheIndex].Remove(Block.OffsetAndPakIndex);
							OffsetAndPakIndexOfSavedBlocked[BlocksCacheIndex].Add(Block.OffsetAndPakIndex);
						}
						return false;
					}
					ClearBlock(Block);
					return true;
				}
				return false;
			}
			);
			if (!Pak.ActualPakFile->GetUnderlyingCacheTrimDisabled())
			{
				TrimCache(false, PakIndex);
			}
			OverlappingNodesInIntervalTree<FCacheBlock>(
				Pak.CacheBlocks[(int32)EBlockStatus::InFlight],
				CacheBlockAllocator,
				Offset,
				Offset + Size - 1,
				0,
				Pak.MaxNode,
				Pak.StartShift,
				Pak.MaxShift,
				[this](TIntervalTreeIndex BlockIndex) -> bool
			{
				FCacheBlock &Block = CacheBlockAllocator.Get(BlockIndex);
				check(Block.InRequestRefCount);
				Block.InRequestRefCount--;
				return true;
			}
			);
		}
		else
		{
			check(0); // not found
		}
		ClearRequest(Request);
	}

	void NotifyComplete(TIntervalTreeIndex RequestIndex)
	{
		// CachedFilesScopeLock is locked
		FPakInRequest& Request = InRequestAllocator.Get(RequestIndex);

		uint16 PakIndex = GetRequestPakIndex(Request.OffsetAndPakIndex);
		int64 Offset = GetRequestOffset(Request.OffsetAndPakIndex);
		FPakData& Pak = CachedPakData[PakIndex];
		check(Offset + Request.Size <= Pak.TotalSize && Request.Size > 0 && Request.GetPriority() >= AIOP_MIN && Request.GetPriority() <= AIOP_MAX && Request.Status == EInRequestStatus::Complete);

		check(Request.Owner && Request.UniqueID);

		if (Request.Status == EInRequestStatus::Complete && Request.UniqueID == Request.Owner->UniqueID && RequestIndex == Request.Owner->InRequestIndex &&  Request.OffsetAndPakIndex == Request.Owner->OffsetAndPakIndex)
		{
			UE_LOG(LogPakFile, Verbose, TEXT("FPakReadRequest[%016llX, %016llX) Notify complete"), Request.OffsetAndPakIndex, Request.OffsetAndPakIndex + Request.Size);
			Request.Owner->RequestIsComplete();
			return;
		}
		else
		{
			check(0); // request should have been found
		}
	}

	FJoinedOffsetAndPakIndex GetNextBlock(EAsyncIOPriorityAndFlags& OutPriority)
	{
		EAsyncIOPriorityAndFlags AsyncMinPriorityLocal = AsyncMinPriority;

		// CachedFilesScopeLock is locked
		uint16 BestPakIndex = 0;
		FJoinedOffsetAndPakIndex BestNext = MAX_uint64;

		OutPriority = AIOP_MIN;
		bool bAnyOutstanding = false;
		for (int32 Priority = AIOP_MAX;; Priority--)
		{
			if (Priority < AsyncMinPriorityLocal && bAnyOutstanding)
			{
				break;
			}
			for (int32 Pass = 0; ; Pass++)
			{
				FJoinedOffsetAndPakIndex LocalLastReadRequest = Pass ? 0 : LastReadRequest;

				uint16 PakIndex = GetRequestPakIndex(LocalLastReadRequest);
				int64 Offset = GetRequestOffset(LocalLastReadRequest);
				check(Offset <= CachedPakData[PakIndex].TotalSize);


				for (; BestNext == MAX_uint64 && PakIndex < CachedPakData.Num(); PakIndex++)
				{
					FPakData& Pak = CachedPakData[PakIndex];
					if (Pak.InRequests[Priority][(int32)EInRequestStatus::Complete] != IntervalTreeInvalidIndex)
					{
						bAnyOutstanding = true;
					}
					if (Pak.InRequests[Priority][(int32)EInRequestStatus::Waiting] != IntervalTreeInvalidIndex)
					{
						uint64 Limit = uint64(Pak.TotalSize - 1);
						if (BestNext != MAX_uint64 && GetRequestPakIndex(BestNext) == PakIndex)
						{
							Limit = GetRequestOffset(BestNext) - 1;
						}

						OverlappingNodesInIntervalTreeWithShrinkingInterval<FPakInRequest>(
							Pak.InRequests[Priority][(int32)EInRequestStatus::Waiting],
							InRequestAllocator,
							uint64(Offset),
							Limit,
							0,
							Pak.MaxNode,
							Pak.StartShift,
							Pak.MaxShift,
							[this, &Pak, &BestNext, &BestPakIndex, PakIndex, &Limit, LocalLastReadRequest](TIntervalTreeIndex Index) -> bool
						{
							FJoinedOffsetAndPakIndex First = FirstUnfilledBlockForRequest(Index, LocalLastReadRequest);
							check(LocalLastReadRequest != 0 || First != MAX_uint64); // if there was not trimming, and this thing is in the waiting list, then why was no start block found?
							if (First < BestNext)
							{
								BestNext = First;
								BestPakIndex = PakIndex;
								Limit = GetRequestOffset(BestNext) - 1;
							}
							return true; // always have to keep going because we want the smallest one
						}
						);
					}
				}
				if (!LocalLastReadRequest)
				{
					break; // this was a full pass
				}
			}

			if (Priority == AIOP_MIN || BestNext != MAX_uint64)
			{
				OutPriority = (EAsyncIOPriorityAndFlags)Priority;
				break;
			}
		}
		return BestNext;
	}

	bool AddNewBlock()
	{
		// CachedFilesScopeLock is locked
		EAsyncIOPriorityAndFlags RequestPriority;
		FJoinedOffsetAndPakIndex BestNext = GetNextBlock(RequestPriority);
		check(RequestPriority < AIOP_NUM);
		if (BestNext == MAX_uint64)
		{
			return false;
		}
		uint16 PakIndex = GetRequestPakIndex(BestNext);
		int64 Offset = GetRequestOffset(BestNext);
		FPakData& Pak = CachedPakData[PakIndex];
		check(Offset < Pak.TotalSize);
		int64 FirstByte = AlignDown(Offset, PAK_CACHE_GRANULARITY);
		int64 LastByte = FMath::Min(Align(FirstByte + (GPakCache_MaxRequestSizeToLowerLevelKB * 1024), PAK_CACHE_GRANULARITY) - 1, Pak.TotalSize - 1);
		check(FirstByte >= 0 && LastByte < Pak.TotalSize && LastByte >= 0 && LastByte >= FirstByte);

		uint32 NumBits = (PAK_CACHE_GRANULARITY + LastByte - FirstByte) / PAK_CACHE_GRANULARITY;
		uint32 NumQWords = (NumBits + 63) >> 6;

		static TArray<uint64> InFlightOrDone;
		InFlightOrDone.Reset();
		InFlightOrDone.AddZeroed(NumQWords);
		if (NumBits != NumQWords * 64)
		{
			uint32 Extras = NumQWords * 64 - NumBits;
			InFlightOrDone[NumQWords - 1] = (MAX_uint64 << (64 - Extras));
		}

		if (Pak.CacheBlocks[(int32)EBlockStatus::Complete] != IntervalTreeInvalidIndex)
		{
			OverlappingNodesInIntervalTreeMask<FCacheBlock>(
				Pak.CacheBlocks[(int32)EBlockStatus::Complete],
				CacheBlockAllocator,
				FirstByte,
				LastByte,
				0,
				Pak.MaxNode,
				Pak.StartShift,
				Pak.MaxShift,
				Pak.BytesToBitsShift,
				&InFlightOrDone[0]
				);
		}
		if (Pak.CacheBlocks[(int32)EBlockStatus::InFlight] != IntervalTreeInvalidIndex)
		{
			OverlappingNodesInIntervalTreeMask<FCacheBlock>(
				Pak.CacheBlocks[(int32)EBlockStatus::InFlight],
				CacheBlockAllocator,
				FirstByte,
				LastByte,
				0,
				Pak.MaxNode,
				Pak.StartShift,
				Pak.MaxShift,
				Pak.BytesToBitsShift,
				&InFlightOrDone[0]
				);
		}

		static TArray<uint64> Requested;
		Requested.Reset();
		Requested.AddZeroed(NumQWords);
		for (int32 Priority = AIOP_MAX;; Priority--)
		{
			if (Priority + PAK_CACHE_MAX_PRIORITY_DIFFERENCE_MERGE < RequestPriority)
			{
				break;
			}
			if (Pak.InRequests[Priority][(int32)EInRequestStatus::Waiting] != IntervalTreeInvalidIndex)
			{
				OverlappingNodesInIntervalTreeMask<FPakInRequest>(
					Pak.InRequests[Priority][(int32)EInRequestStatus::Waiting],
					InRequestAllocator,
					FirstByte,
					LastByte,
					0,
					Pak.MaxNode,
					Pak.StartShift,
					Pak.MaxShift,
					Pak.BytesToBitsShift,
					&Requested[0]
					);
			}
			if (Priority == AIOP_MIN)
			{
				break;
			}
		}


		int64 Size = PAK_CACHE_GRANULARITY * 64 * NumQWords;
		for (uint32 Index = 0; Index < NumQWords; Index++)
		{
			uint64 NotAlreadyInFlightAndRequested = ((~InFlightOrDone[Index]) & Requested[Index]);
			if (NotAlreadyInFlightAndRequested != MAX_uint64)
			{
				Size = PAK_CACHE_GRANULARITY * 64 * Index;
				while (NotAlreadyInFlightAndRequested & 1)
				{
					Size += PAK_CACHE_GRANULARITY;
					NotAlreadyInFlightAndRequested >>= 1;
				}
				break;
			}
		}
		check(Size > 0 && Size <= (GPakCache_MaxRequestSizeToLowerLevelKB * 1024));
		Size = FMath::Min(FirstByte + Size, LastByte + 1) - FirstByte;

		TIntervalTreeIndex NewIndex = CacheBlockAllocator.Alloc();

		FCacheBlock& Block = CacheBlockAllocator.Get(NewIndex);
		Block.Index = NewIndex;
		Block.InRequestRefCount = 0;
		Block.Memory = nullptr;
		Block.OffsetAndPakIndex = MakeJoinedRequest(PakIndex, FirstByte);
		Block.Size = Size;
		Block.Status = EBlockStatus::InFlight;

		AddToIntervalTree<FCacheBlock>(
			&Pak.CacheBlocks[(int32)EBlockStatus::InFlight],
			CacheBlockAllocator,
			NewIndex,
			Pak.StartShift,
			Pak.MaxShift
			);

		TArray<TIntervalTreeIndex> Inflights;

		for (int32 Priority = AIOP_MAX;; Priority--)
		{
			if (Pak.InRequests[Priority][(int32)EInRequestStatus::Waiting] != IntervalTreeInvalidIndex)
			{
				MaybeRemoveOverlappingNodesInIntervalTree<FPakInRequest>(
					&Pak.InRequests[Priority][(int32)EInRequestStatus::Waiting],
					InRequestAllocator,
					uint64(FirstByte),
					uint64(FirstByte + Size - 1),
					0,
					Pak.MaxNode,
					Pak.StartShift,
					Pak.MaxShift,
					[this, &Block, &Inflights](TIntervalTreeIndex RequestIndex) -> bool
				{
					Block.InRequestRefCount++;
					if (FirstUnfilledBlockForRequest(RequestIndex) == MAX_uint64)
					{
						InRequestAllocator.Get(RequestIndex).Next = IntervalTreeInvalidIndex;
						Inflights.Add(RequestIndex);
						return true;
					}
					return false;
				}
				);
			}
#if PAK_EXTRA_CHECKS
			OverlappingNodesInIntervalTree<FPakInRequest>(
				Pak.InRequests[Priority][(int32)EInRequestStatus::InFlight],
				InRequestAllocator,
				uint64(FirstByte),
				uint64(FirstByte + Size - 1),
				0,
				Pak.MaxNode,
				Pak.StartShift,
				Pak.MaxShift,
				[](TIntervalTreeIndex) -> bool
			{
				check(0); // if this is in flight, then why does it overlap my new block
				return false;
			}
			);
			OverlappingNodesInIntervalTree<FPakInRequest>(
				Pak.InRequests[Priority][(int32)EInRequestStatus::Complete],
				InRequestAllocator,
				uint64(FirstByte),
				uint64(FirstByte + Size - 1),
				0,
				Pak.MaxNode,
				Pak.StartShift,
				Pak.MaxShift,
				[](TIntervalTreeIndex) -> bool
			{
				check(0); // if this is complete, then why does it overlap my new block
				return false;
			}
			);
#endif
			if (Priority == AIOP_MIN)
			{
				break;
			}
		}
		for (TIntervalTreeIndex Fli : Inflights)
		{
			FPakInRequest& CompReq = InRequestAllocator.Get(Fli);
			CompReq.Status = EInRequestStatus::InFlight;
			AddToIntervalTree(&Pak.InRequests[CompReq.GetPriority()][(int32)EInRequestStatus::InFlight], InRequestAllocator, Fli, Pak.StartShift, Pak.MaxShift);
		}

		StartBlockTask(Block);
		return true;

	}

	int32 OpenTaskSlot()
	{
		int32 IndexToFill = -1;
		for (int32 Index = 0; Index < GPakCache_MaxRequestsToLowerLevel; Index++)
		{
			if (!RequestsToLower[Index].RequestHandle)
			{
				IndexToFill = Index;
				break;
			}
		}
		return IndexToFill;
	}


	bool HasRequestsAtStatus(EInRequestStatus Status)
	{
		for (uint16 PakIndex = 0; PakIndex < CachedPakData.Num(); PakIndex++)
		{
			FPakData& Pak = CachedPakData[PakIndex];
			for (int32 Priority = AIOP_MAX;; Priority--)
			{
				if (Pak.InRequests[Priority][(int32)Status] != IntervalTreeInvalidIndex)
				{
					return true;
				}
				if (Priority == AIOP_MIN)
				{
					break;
				}
			}
		}
		return false;
	}

	bool CanStartAnotherTask()
	{
		if (OpenTaskSlot() < 0)
		{
			return false;
		}
		return HasRequestsAtStatus(EInRequestStatus::Waiting);
	}
	void ClearOldBlockTasks()
	{
		if (!NotifyRecursion)
		{
			for (IAsyncReadRequest* Elem : RequestsToDelete)
			{
				Elem->WaitCompletion();
				delete Elem;
			}
			RequestsToDelete.Empty();
		}
	}
	void StartBlockTask(FCacheBlock& Block)
	{
		// CachedFilesScopeLock is locked
#define CHECK_REDUNDANT_READS (0)
#if CHECK_REDUNDANT_READS
		static struct FRedundantReadTracker
		{
			TMap<int64, double> LastReadTime;
			int32 NumRedundant;
			FRedundantReadTracker()
				: NumRedundant(0)
			{
			}

			void CheckBlock(int64 Offset, int64 Size)
			{
				double NowTime = FPlatformTime::Seconds();
				int64 StartBlock = Offset / PAK_CACHE_GRANULARITY;
				int64 LastBlock = (Offset + Size - 1) / PAK_CACHE_GRANULARITY;
				for (int64 CurBlock = StartBlock; CurBlock <= LastBlock; CurBlock++)
				{
					double LastTime = LastReadTime.FindRef(CurBlock);
					if (LastTime > 0.0 && NowTime - LastTime < 3.0)
					{
						NumRedundant++;
						FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Redundant read at block %d, %6.1fms ago       (%d total redundant blocks)\r\n"), int32(CurBlock), 1000.0f * float(NowTime - LastTime), NumRedundant);
					}
					LastReadTime.Add(CurBlock, NowTime);
				}
			}
		} RedundantReadTracker;
#else
		static struct FRedundantReadTracker
		{
			FORCEINLINE void CheckBlock(int64 Offset, int64 Size)
			{
			}
		} RedundantReadTracker;

#endif

		int32 IndexToFill = OpenTaskSlot();
		if (IndexToFill < 0)
		{
			check(0);
			return;
		}
		EAsyncIOPriorityAndFlags Priority = AIOP_Normal; // the lower level requests are not prioritized at the moment
		check(Block.Status == EBlockStatus::InFlight);
		UE_LOG(LogPakFile, Verbose, TEXT("FPakReadRequest[%016llX, %016llX) StartBlockTask"), Block.OffsetAndPakIndex, Block.OffsetAndPakIndex + Block.Size);
		uint16 PakIndex = GetRequestPakIndex(Block.OffsetAndPakIndex);
		FPakData& Pak = CachedPakData[PakIndex];
		RequestsToLower[IndexToFill].BlockIndex = Block.Index;
		RequestsToLower[IndexToFill].RequestSize = Block.Size;
		RequestsToLower[IndexToFill].Memory = nullptr;
		check(&CacheBlockAllocator.Get(RequestsToLower[IndexToFill].BlockIndex) == &Block);

#if USE_PAK_PRECACHE && CSV_PROFILER
		FPlatformAtomics::InterlockedAdd(&GPreCacheTotalLoaded, Block.Size);
#endif

        // FORT HACK
        // DO NOT BRING BACK
        // FORT HACK
        bool bDoCheck = true;
#if PLATFORM_IOS
        static const int32 Range = 100;
        static const int32 Offset = 500;
        static int32 RandomCheckCount = FMath::Rand() % Range + Offset;
        bDoCheck = --RandomCheckCount <= 0;
        if (bDoCheck)
        {
            RandomCheckCount = FMath::Rand() % Range + Offset;
        }
#endif
		FAsyncFileCallBack CallbackFromLower =
			[this, IndexToFill, bDoCheck](bool bWasCanceled, IAsyncReadRequest* Request)
		{
			if (bEnableSignatureChecks && bDoCheck)
			{
				StartSignatureCheck(bWasCanceled, Request, IndexToFill);
			}
			else
			{
				NewRequestsToLowerComplete(bWasCanceled, Request, IndexToFill);
			}
		};

		RequestsToLower[IndexToFill].RequestHandle = Pak.Handle->ReadRequest(GetRequestOffset(Block.OffsetAndPakIndex), Block.Size, Priority, &CallbackFromLower);
		RedundantReadTracker.CheckBlock(GetRequestOffset(Block.OffsetAndPakIndex), Block.Size);

#if CSV_PROFILER
		FJoinedOffsetAndPakIndex OldLastReadRequest = LastReadRequest;
		LastReadRequest = Block.OffsetAndPakIndex + Block.Size;

		if (OldLastReadRequest != Block.OffsetAndPakIndex)
		{
			if (GetRequestPakIndexLow(OldLastReadRequest) != GetRequestPakIndexLow(Block.OffsetAndPakIndex))
			{
				GPreCacheBadSeeks++;
			}
			else
			{
				GPreCacheSeeks++;
			}
		}
		else
		{
			GPreCacheContiguousReads++;
		}
#endif
		Loads++;
		LoadSize += Block.Size;
	}

	void CompleteRequest(bool bWasCanceled, uint8* Memory, TIntervalTreeIndex BlockIndex)
	{
		FCacheBlock& Block = CacheBlockAllocator.Get(BlockIndex);
		uint16 PakIndex = GetRequestPakIndex(Block.OffsetAndPakIndex);
		int64 Offset = GetRequestOffset(Block.OffsetAndPakIndex);
		FPakData& Pak = CachedPakData[PakIndex];
		check(!Block.Memory && Block.Size);
		check(!bWasCanceled); // this is doable, but we need to transition requests back to waiting, inflight etc.

		if (!RemoveFromIntervalTree<FCacheBlock>(&Pak.CacheBlocks[(int32)EBlockStatus::InFlight], CacheBlockAllocator, Block.Index, Pak.StartShift, Pak.MaxShift))
		{
			check(0);
		}

		if (Block.InRequestRefCount == 0 || bWasCanceled)
		{
			check(Block.Size > 0);
			DEC_MEMORY_STAT_BY(STAT_AsyncFileMemory, Block.Size);
			FMemory::Free(Memory);
			UE_LOG(LogPakFile, Verbose, TEXT("FPakReadRequest[%016llX, %016llX) Cancelled"), Block.OffsetAndPakIndex, Block.OffsetAndPakIndex + Block.Size);
			ClearBlock(Block);
		}
		else
		{
			Block.Memory = Memory;
			check(Block.Memory && Block.Size);
			BlockMemory += Block.Size;
			check(BlockMemory > 0);
			DEC_MEMORY_STAT_BY(STAT_AsyncFileMemory, Block.Size);
			check(Block.Size > 0);
			INC_MEMORY_STAT_BY(STAT_PakCacheMem, Block.Size);

			if (BlockMemory > BlockMemoryHighWater)
			{
				BlockMemoryHighWater = BlockMemory;
				SET_MEMORY_STAT(STAT_PakCacheHighWater, BlockMemoryHighWater);

#if 1
				static int64 LastPrint = 0;
				if (BlockMemoryHighWater / 1024 / 1024 / 16 != LastPrint)
				{
					LastPrint = BlockMemoryHighWater / 1024 / 1024 / 16;
					//FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Precache HighWater %dMB\r\n"), int32(LastPrint));
					UE_LOG(LogPakFile, Log, TEXT("Precache HighWater %dMB\r\n"), int32(LastPrint * 16));
				}
#endif
			}
			Block.Status = EBlockStatus::Complete;
			AddToIntervalTree<FCacheBlock>(
				&Pak.CacheBlocks[(int32)EBlockStatus::Complete],
				CacheBlockAllocator,
				Block.Index,
				Pak.StartShift,
				Pak.MaxShift
				);
			TArray<TIntervalTreeIndex> Completeds;
			for (int32 Priority = AIOP_MAX;; Priority--)
			{
				if (Pak.InRequests[Priority][(int32)EInRequestStatus::InFlight] != IntervalTreeInvalidIndex)
				{
					MaybeRemoveOverlappingNodesInIntervalTree<FPakInRequest>(
						&Pak.InRequests[Priority][(int32)EInRequestStatus::InFlight],
						InRequestAllocator,
						uint64(Offset),
						uint64(Offset + Block.Size - 1),
						0,
						Pak.MaxNode,
						Pak.StartShift,
						Pak.MaxShift,
						[this, &Completeds](TIntervalTreeIndex RequestIndex) -> bool
					{
						if (FirstUnfilledBlockForRequest(RequestIndex) == MAX_uint64)
						{
							InRequestAllocator.Get(RequestIndex).Next = IntervalTreeInvalidIndex;
							Completeds.Add(RequestIndex);
							return true;
						}
						return false;
					}
					);
				}
				if (Priority == AIOP_MIN)
				{
					break;
				}
			}
			for (TIntervalTreeIndex Comp : Completeds)
			{
				FPakInRequest& CompReq = InRequestAllocator.Get(Comp);
				CompReq.Status = EInRequestStatus::Complete;
				AddToIntervalTree(&Pak.InRequests[CompReq.GetPriority()][(int32)EInRequestStatus::Complete], InRequestAllocator, Comp, Pak.StartShift, Pak.MaxShift);
				NotifyComplete(Comp); // potentially scary recursion here
			}
		}

		TrimCache();
	}

	bool StartNextRequest()
	{
		if (CanStartAnotherTask())
		{
			return AddNewBlock();
		}
		return false;
	}

	bool GetCompletedRequestData(FPakInRequest& DoneRequest, uint8* Result)
	{
		// CachedFilesScopeLock is locked
		check(DoneRequest.Status == EInRequestStatus::Complete);
		uint16 PakIndex = GetRequestPakIndex(DoneRequest.OffsetAndPakIndex);
		int64 Offset = GetRequestOffset(DoneRequest.OffsetAndPakIndex);
		int64 Size = DoneRequest.Size;

		FPakData& Pak = CachedPakData[PakIndex];
		check(Offset + DoneRequest.Size <= Pak.TotalSize && DoneRequest.Size > 0 && DoneRequest.GetPriority() >= AIOP_MIN && DoneRequest.GetPriority() <= AIOP_MAX && DoneRequest.Status == EInRequestStatus::Complete);

		int64 BytesCopied = 0;

#if 0 // this path removes the block in one pass, however, this is not what we want because it wrecks precaching, if we change back GetCompletedRequest needs to maybe start a new request and the logic of the IAsyncFile read needs to change
		MaybeRemoveOverlappingNodesInIntervalTree<FCacheBlock>(
			&Pak.CacheBlocks[(int32)EBlockStatus::Complete],
			CacheBlockAllocator,
			Offset,
			Offset + Size - 1,
			0,
			Pak.MaxNode,
			Pak.StartShift,
			Pak.MaxShift,
			[this, Offset, Size, &BytesCopied, Result, &Pak](TIntervalTreeIndex BlockIndex) -> bool
		{
			FCacheBlock &Block = CacheBlockAllocator.Get(BlockIndex);
			int64 BlockOffset = GetRequestOffset(Block.OffsetAndPakIndex);
			check(Block.Memory && Block.Size && BlockOffset >= 0 && BlockOffset + Block.Size <= Pak.TotalSize);

			int64 OverlapStart = FMath::Max(Offset, BlockOffset);
			int64 OverlapEnd = FMath::Min(Offset + Size, BlockOffset + Block.Size);
			check(OverlapEnd > OverlapStart);
			BytesCopied += OverlapEnd - OverlapStart;
			FMemory::Memcpy(Result + OverlapStart - Offset, Block.Memory + OverlapStart - BlockOffset, OverlapEnd - OverlapStart);
			check(Block.InRequestRefCount);
			if (!--Block.InRequestRefCount)
			{
				ClearBlock(Block);
				return true;
			}
			return false;
		}
		);

		if (!RemoveFromIntervalTree<FPakInRequest>(&Pak.InRequests[DoneRequest.GetPriority()][(int32)EInRequestStatus::Complete], InRequestAllocator, DoneRequest.Index, Pak.StartShift, Pak.MaxShift))
		{
			check(0); // not found
		}
		ClearRequest(DoneRequest);
#else
		OverlappingNodesInIntervalTree<FCacheBlock>(
			Pak.CacheBlocks[(int32)EBlockStatus::Complete],
			CacheBlockAllocator,
			Offset,
			Offset + Size - 1,
			0,
			Pak.MaxNode,
			Pak.StartShift,
			Pak.MaxShift,
			[this, Offset, Size, &BytesCopied, Result, &Pak](TIntervalTreeIndex BlockIndex) -> bool
		{
			FCacheBlock &Block = CacheBlockAllocator.Get(BlockIndex);
			int64 BlockOffset = GetRequestOffset(Block.OffsetAndPakIndex);
			check(Block.Memory && Block.Size && BlockOffset >= 0 && BlockOffset + Block.Size <= Pak.TotalSize);

			int64 OverlapStart = FMath::Max(Offset, BlockOffset);
			int64 OverlapEnd = FMath::Min(Offset + Size, BlockOffset + Block.Size);
			check(OverlapEnd > OverlapStart);
			BytesCopied += OverlapEnd - OverlapStart;
			FMemory::Memcpy(Result + OverlapStart - Offset, Block.Memory + OverlapStart - BlockOffset, OverlapEnd - OverlapStart);
			return true;
		}
		);
#endif
		check(BytesCopied == Size);


		return true;
	}

	///// Below here are the thread entrypoints

public:

	void NewRequestsToLowerComplete(bool bWasCanceled, IAsyncReadRequest* Request, int32 Index)
	{
		LLM_SCOPE(ELLMTag::FileSystem);
		FScopeLock Lock(&CachedFilesScopeLock);
		RequestsToLower[Index].RequestHandle = Request;
		ClearOldBlockTasks();
		NotifyRecursion++;
		if (!RequestsToLower[Index].Memory) // might have already been filled in by the signature check
		{
			RequestsToLower[Index].Memory = Request->GetReadResults();
		}
		CompleteRequest(bWasCanceled, RequestsToLower[Index].Memory, RequestsToLower[Index].BlockIndex);
		RequestsToLower[Index].RequestHandle = nullptr;
		RequestsToDelete.Add(Request);
		RequestsToLower[Index].BlockIndex = IntervalTreeInvalidIndex;
		StartNextRequest();
		NotifyRecursion--;
	}

	bool QueueRequest(IPakRequestor* Owner, FPakFile* InActualPakFile, FName File, int64 PakFileSize, int64 Offset, int64 Size, EAsyncIOPriorityAndFlags PriorityAndFlags)
	{
		CSV_SCOPED_TIMING_STAT(FileIO, PakPrecacherQueueRequest);
		check(Owner && File != NAME_None && Size > 0 && Offset >= 0 && Offset < PakFileSize && (PriorityAndFlags&AIOP_PRIORITY_MASK) >= AIOP_MIN && (PriorityAndFlags&AIOP_PRIORITY_MASK) <= AIOP_MAX);
		FScopeLock Lock(&CachedFilesScopeLock);
		uint16* PakIndexPtr = RegisterPakFile(InActualPakFile, File, PakFileSize);
		if (PakIndexPtr == nullptr)
		{
			return false;
		}
		uint16 PakIndex = *PakIndexPtr;
		FPakData& Pak = CachedPakData[PakIndex];
		check(Pak.Name == File && Pak.TotalSize == PakFileSize && Pak.Handle);

		TIntervalTreeIndex RequestIndex = InRequestAllocator.Alloc();
		FPakInRequest& Request = InRequestAllocator.Get(RequestIndex);
		FJoinedOffsetAndPakIndex RequestOffsetAndPakIndex = MakeJoinedRequest(PakIndex, Offset);
		Request.OffsetAndPakIndex = RequestOffsetAndPakIndex;
		Request.Size = Size;
		Request.PriorityAndFlags = PriorityAndFlags;
		Request.Status = EInRequestStatus::Waiting;
		Request.Owner = Owner;
		Request.UniqueID = NextUniqueID++;
		Request.Index = RequestIndex;
		check(Request.Next == IntervalTreeInvalidIndex);
		Owner->OffsetAndPakIndex = Request.OffsetAndPakIndex;
		Owner->UniqueID = Request.UniqueID;
		Owner->InRequestIndex = RequestIndex;
		check(!OutstandingRequests.Contains(Request.UniqueID));
		OutstandingRequests.Add(Request.UniqueID, RequestIndex);
		RequestCounter.Increment();

		if (AddRequest(Request, RequestIndex))
		{
#if USE_PAK_PRECACHE && CSV_PROFILER
			FPlatformAtomics::InterlockedIncrement(&GPreCacheHotBlocksCount);
#endif
			UE_LOG(LogPakFile, Verbose, TEXT("FPakReadRequest[%016llX, %016llX) QueueRequest HOT"), RequestOffsetAndPakIndex, RequestOffsetAndPakIndex + Request.Size);
		}
		else
		{
#if USE_PAK_PRECACHE && CSV_PROFILER
			FPlatformAtomics::InterlockedIncrement(&GPreCacheColdBlocksCount);
#endif
			UE_LOG(LogPakFile, Verbose, TEXT("FPakReadRequest[%016llX, %016llX) QueueRequest COLD"), RequestOffsetAndPakIndex, RequestOffsetAndPakIndex + Request.Size);
		}

		TrimCache();
		return true;
	}

	void SetAsyncMinimumPriority(EAsyncIOPriorityAndFlags NewPriority)
	{
		bool bStartNewRequests = false;
		{
			FScopeLock Lock(&SetAsyncMinimumPriorityScopeLock);
			if (AsyncMinPriority != NewPriority)
			{
				if (NewPriority < AsyncMinPriority)
				{
					bStartNewRequests = true;
				}
				AsyncMinPriority = NewPriority;
			}
		}

		if (bStartNewRequests)
		{
			FScopeLock Lock(&CachedFilesScopeLock);
			StartNextRequest();
		}
	}

	bool GetCompletedRequest(IPakRequestor* Owner, uint8* UserSuppliedMemory)
	{
		check(Owner);
		FScopeLock Lock(&CachedFilesScopeLock);
		ClearOldBlockTasks();
		TIntervalTreeIndex RequestIndex = OutstandingRequests.FindRef(Owner->UniqueID);
		static_assert(IntervalTreeInvalidIndex == 0, "FindRef will return 0 for something not found");
		if (RequestIndex)
		{
			FPakInRequest& Request = InRequestAllocator.Get(RequestIndex);
			check(Owner == Request.Owner && Request.Status == EInRequestStatus::Complete && Request.UniqueID == Request.Owner->UniqueID && RequestIndex == Request.Owner->InRequestIndex &&  Request.OffsetAndPakIndex == Request.Owner->OffsetAndPakIndex);
			return GetCompletedRequestData(Request, UserSuppliedMemory);
		}
		return false; // canceled
	}

	void CancelRequest(IPakRequestor* Owner)
	{
		check(Owner);
		FScopeLock Lock(&CachedFilesScopeLock);
		ClearOldBlockTasks();
		TIntervalTreeIndex RequestIndex = OutstandingRequests.FindRef(Owner->UniqueID);
		static_assert(IntervalTreeInvalidIndex == 0, "FindRef will return 0 for something not found");
		if (RequestIndex)
		{
			FPakInRequest& Request = InRequestAllocator.Get(RequestIndex);
			check(Owner == Request.Owner && Request.UniqueID == Request.Owner->UniqueID && RequestIndex == Request.Owner->InRequestIndex &&  Request.OffsetAndPakIndex == Request.Owner->OffsetAndPakIndex);
			RemoveRequest(RequestIndex);
		}
		StartNextRequest();
	}

	bool IsProbablyIdle() // nothing to prevent new requests from being made before I return
	{
		FScopeLock Lock(&CachedFilesScopeLock);
		return !HasRequestsAtStatus(EInRequestStatus::Waiting) && !HasRequestsAtStatus(EInRequestStatus::InFlight);
	}

	void Unmount(FName PakFile)
	{
		FScopeLock Lock(&CachedFilesScopeLock);

		for (TMap<FPakFile*, uint16>::TIterator It(CachedPaks); It; ++It)
		{
			if( It->Key->GetFilenameName() == PakFile )
			{
				uint16 PakIndex = It->Value;
				TrimCache(true);
				FPakData& Pak = CachedPakData[PakIndex];
				int64 Offset = MakeJoinedRequest(PakIndex, 0);

				bool bHasOutstandingRequests = false;

				OverlappingNodesInIntervalTree<FCacheBlock>(
					Pak.CacheBlocks[(int32)EBlockStatus::Complete],
					CacheBlockAllocator,
					0,
					Offset + Pak.TotalSize - 1,
					0,
					Pak.MaxNode,
					Pak.StartShift,
					Pak.MaxShift,
					[&bHasOutstandingRequests](TIntervalTreeIndex BlockIndex) -> bool
				{
					check(!"Pak cannot be unmounted with outstanding requests");
					bHasOutstandingRequests = true;
					return false;
				}
				);
				OverlappingNodesInIntervalTree<FCacheBlock>(
					Pak.CacheBlocks[(int32)EBlockStatus::InFlight],
					CacheBlockAllocator,
					0,
					Offset + Pak.TotalSize - 1,
					0,
					Pak.MaxNode,
					Pak.StartShift,
					Pak.MaxShift,
					[&bHasOutstandingRequests](TIntervalTreeIndex BlockIndex) -> bool
				{
					check(!"Pak cannot be unmounted with outstanding requests");
					bHasOutstandingRequests = true;
					return false;
				}
				);
				for (int32 Priority = AIOP_MAX;; Priority--)
				{
					OverlappingNodesInIntervalTree<FPakInRequest>(
						Pak.InRequests[Priority][(int32)EInRequestStatus::InFlight],
						InRequestAllocator,
						0,
						Offset + Pak.TotalSize - 1,
						0,
						Pak.MaxNode,
						Pak.StartShift,
						Pak.MaxShift,
						[&bHasOutstandingRequests](TIntervalTreeIndex BlockIndex) -> bool
					{
						check(!"Pak cannot be unmounted with outstanding requests");
						bHasOutstandingRequests = true;
						return false;
					}
					);
					OverlappingNodesInIntervalTree<FPakInRequest>(
						Pak.InRequests[Priority][(int32)EInRequestStatus::Complete],
						InRequestAllocator,
						0,
						Offset + Pak.TotalSize - 1,
						0,
						Pak.MaxNode,
						Pak.StartShift,
						Pak.MaxShift,
						[&bHasOutstandingRequests](TIntervalTreeIndex BlockIndex) -> bool
					{
						check(!"Pak cannot be unmounted with outstanding requests");
						bHasOutstandingRequests = true;
						return false;
					}
					);
					OverlappingNodesInIntervalTree<FPakInRequest>(
						Pak.InRequests[Priority][(int32)EInRequestStatus::Waiting],
						InRequestAllocator,
						0,
						Offset + Pak.TotalSize - 1,
						0,
						Pak.MaxNode,
						Pak.StartShift,
						Pak.MaxShift,
						[&bHasOutstandingRequests](TIntervalTreeIndex BlockIndex) -> bool
					{
						check(!"Pak cannot be unmounted with outstanding requests");
						bHasOutstandingRequests = true;
						return false;
					}
					);
					if (Priority == AIOP_MIN)
					{
						break;
					}
				}
				if (!bHasOutstandingRequests)
				{
					UE_LOG(LogPakFile, Log, TEXT("Pak file %s removed from pak precacher."), *PakFile.ToString());
					It.RemoveCurrent();
					check(Pak.Handle);
					delete Pak.Handle;
					Pak.Handle = nullptr;
					int32 NumToTrim = 0;
					for (int32 Index = CachedPakData.Num() - 1; Index >= 0; Index--)
					{
						if (!CachedPakData[Index].Handle)
						{
							NumToTrim++;
						}
						else
						{
							break;
						}
					}
					if (NumToTrim)
					{
						CachedPakData.RemoveAt(CachedPakData.Num() - NumToTrim, NumToTrim);
						LastReadRequest = 0;
					}
				}
				else
				{
					UE_LOG(LogPakFile, Log, TEXT("Pak file %s was NOT removed from pak precacher because it had outstanding requests."), *PakFile.ToString());
				}

			}
		}

	}


	// these are not threadsafe and should only be used for synthetic testing
	uint64 GetLoadSize()
	{
		return LoadSize;
	}
	uint32 GetLoads()
	{
		return Loads;
	}
	uint32 GetFrees()
	{
		return Frees;
	}

	void DumpBlocks()
	{
		while (!FPakPrecacher::Get().IsProbablyIdle())
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_WaitDumpBlocks);
			FPlatformProcess::SleepNoStats(0.001f);
		}
		FScopeLock Lock(&CachedFilesScopeLock);
		bool bDone = !HasRequestsAtStatus(EInRequestStatus::Waiting) && !HasRequestsAtStatus(EInRequestStatus::InFlight) && !HasRequestsAtStatus(EInRequestStatus::Complete);

		if (!bDone)
		{
			UE_LOG(LogPakFile, Log, TEXT("PakCache has outstanding requests with %llu total memory."), BlockMemory);
		}
		else
		{
			UE_LOG(LogPakFile, Log, TEXT("PakCache has no outstanding requests with %llu total memory."), BlockMemory);
		}
	}
};

static void WaitPrecache(const TArray<FString>& Args)
{
	uint32 Frees = FPakPrecacher::Get().GetFrees();
	uint32 Loads = FPakPrecacher::Get().GetLoads();
	uint64 LoadSize = FPakPrecacher::Get().GetLoadSize();

	double StartTime = FPlatformTime::Seconds();

	while (!FPakPrecacher::Get().IsProbablyIdle())
	{
		check(Frees == FPakPrecacher::Get().GetFrees()); // otherwise we are discarding things, which is not what we want for this synthetic test
		QUICK_SCOPE_CYCLE_COUNTER(STAT_WaitPrecache);
		FPlatformProcess::SleepNoStats(0.001f);
	}
	Loads = FPakPrecacher::Get().GetLoads() - Loads;
	LoadSize = FPakPrecacher::Get().GetLoadSize() - LoadSize;
	float TimeSpent = FPlatformTime::Seconds() - StartTime;
	float LoadSizeMB = float(LoadSize) / (1024.0f * 1024.0f);
	float MBs = LoadSizeMB / TimeSpent;
	UE_LOG(LogPakFile, Log, TEXT("Loaded %4d blocks (align %4dKB) totalling %7.2fMB in %4.2fs   = %6.2fMB/s"), Loads, PAK_CACHE_GRANULARITY / 1024, LoadSizeMB, TimeSpent, MBs);
}

static FAutoConsoleCommand WaitPrecacheCmd(
	TEXT("pak.WaitPrecache"),
	TEXT("Debug command to wait on the pak precache."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&WaitPrecache)
);

static void DumpBlocks(const TArray<FString>& Args)
{
	FPakPrecacher::Get().DumpBlocks();
}

static FAutoConsoleCommand DumpBlocksCmd(
	TEXT("pak.DumpBlocks"),
	TEXT("Debug command to spew the outstanding blocks."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&DumpBlocks)
);

static FCriticalSection FPakReadRequestEvent;

class FPakAsyncReadFileHandle;

struct FCachedAsyncBlock
{
	class FPakReadRequest* RawRequest;
	uint8* Raw; // compressed, encrypted and/or signature not checked
	uint8* Processed; // decompressed, deencrypted and signature checked
	FGraphEventRef CPUWorkGraphEvent;
	int32 RawSize;
	int32 DecompressionRawSize;
	int32 ProcessedSize;
	int32 RefCount;
	int32 BlockIndex;
	bool bInFlight;
	bool bCPUWorkIsComplete;
	bool bCancelledBlock;
	FCachedAsyncBlock()
		: RawRequest(0)
		, Raw(nullptr)
		, Processed(nullptr)
		, RawSize(0)
		, DecompressionRawSize(0)
		, ProcessedSize(0)
		, RefCount(0)
		, BlockIndex(-1)
		, bInFlight(false)
		, bCPUWorkIsComplete(false)
		, bCancelledBlock(false)
	{
	}
};


class FPakReadRequestBase : public IAsyncReadRequest, public IPakRequestor
{
protected:

	int64 Offset;
	int64 BytesToRead;
	FEvent* WaitEvent;
	FCachedAsyncBlock* BlockPtr;
	FName PanicPakFile;
	EAsyncIOPriorityAndFlags PriorityAndFlags;
	bool bRequestOutstanding;
	bool bNeedsRemoval;
	bool bInternalRequest; // we are using this internally to deal with compressed, encrypted and signed, so we want the memory back from a precache request.

public:
	FPakReadRequestBase(FName InPakFile, int64 PakFileSize, FAsyncFileCallBack* CompleteCallback, int64 InOffset, int64 InBytesToRead, EAsyncIOPriorityAndFlags InPriorityAndFlags, uint8* UserSuppliedMemory, bool bInInternalRequest = false, FCachedAsyncBlock* InBlockPtr = nullptr)
		: IAsyncReadRequest(CompleteCallback, false, UserSuppliedMemory)
		, Offset(InOffset)
		, BytesToRead(InBytesToRead)
		, WaitEvent(nullptr)
		, BlockPtr(InBlockPtr)
		, PanicPakFile(InPakFile)
		, PriorityAndFlags(InPriorityAndFlags)
		, bRequestOutstanding(true)
		, bNeedsRemoval(true)
		, bInternalRequest(bInInternalRequest)
	{
	}

	virtual ~FPakReadRequestBase()
	{
		if (bNeedsRemoval)
		{
			FPakPrecacher::Get().CancelRequest(this);
		}
		if (Memory && !bUserSuppliedMemory)
		{
			// this can happen with a race on cancel, it is ok, they didn't take the memory, free it now
			check(BytesToRead > 0);
			DEC_MEMORY_STAT_BY(STAT_AsyncFileMemory, BytesToRead);
			FMemory::Free(Memory);
		}
		Memory = nullptr;
	}

	// IAsyncReadRequest Interface

	virtual void WaitCompletionImpl(float TimeLimitSeconds) override
	{
		{
			FScopeLock Lock(&FPakReadRequestEvent);
			if (bRequestOutstanding)
			{
				check(!WaitEvent);
				WaitEvent = FPlatformProcess::GetSynchEventFromPool(true);
			}
		}
		if (WaitEvent)
		{
			if (TimeLimitSeconds == 0.0f)
			{
				WaitEvent->Wait();
				check(!bRequestOutstanding);
			}
			else
			{
				WaitEvent->Wait(TimeLimitSeconds * 1000.0f);
			}
			FScopeLock Lock(&FPakReadRequestEvent);
			FPlatformProcess::ReturnSynchEventToPool(WaitEvent);
			WaitEvent = nullptr;
		}
	}
	virtual void CancelImpl() override
	{
		check(!WaitEvent); // you canceled from a different thread that you waited from
		FPakPrecacher::Get().CancelRequest(this);
		bNeedsRemoval = false;
		if (bRequestOutstanding)
		{
			bRequestOutstanding = false;
			SetComplete();
		}
	}

	FCachedAsyncBlock& GetBlock()
	{
		check(bInternalRequest && BlockPtr);
		return *BlockPtr;
	}
};

class FPakReadRequest : public FPakReadRequestBase
{
public:

	FPakReadRequest(FPakFile* InActualPakFile, FName InPakFile, int64 PakFileSize, FAsyncFileCallBack* CompleteCallback, int64 InOffset, int64 InBytesToRead, EAsyncIOPriorityAndFlags InPriorityAndFlags, uint8* UserSuppliedMemory, bool bInInternalRequest = false, FCachedAsyncBlock* InBlockPtr = nullptr)
		: FPakReadRequestBase(InPakFile, PakFileSize, CompleteCallback, InOffset, InBytesToRead, InPriorityAndFlags, UserSuppliedMemory, bInInternalRequest, InBlockPtr)
	{
		check(Offset >= 0 && BytesToRead > 0);
		check(bInternalRequest || ( InPriorityAndFlags & AIOP_FLAG_PRECACHE ) == 0 || !bUserSuppliedMemory); // you never get bits back from a precache request, so why supply memory?

		if (!FPakPrecacher::Get().QueueRequest(this, InActualPakFile, InPakFile, PakFileSize, Offset, BytesToRead, InPriorityAndFlags))
		{
			bRequestOutstanding = false;
			SetComplete();
		}
	}

	virtual void RequestIsComplete() override
	{
		check(bRequestOutstanding);
		if (!bCanceled && (bInternalRequest || (PriorityAndFlags & AIOP_FLAG_PRECACHE) == 0))
		{
			if (!bUserSuppliedMemory)
			{
				check(!Memory);
				Memory = (uint8*)FMemory::Malloc(BytesToRead);
				check(BytesToRead > 0);
				INC_MEMORY_STAT_BY(STAT_AsyncFileMemory, BytesToRead);
			}
			else
			{
				check(Memory);
			}
			if (!FPakPrecacher::Get().GetCompletedRequest(this, Memory))
			{
				check(bCanceled);
			}
		}
		SetDataComplete();
		{
			FScopeLock Lock(&FPakReadRequestEvent);
			bRequestOutstanding = false;
			if (WaitEvent)
			{
				WaitEvent->Trigger();
			}
			SetAllComplete();
		}
	}

	void PanicSyncRead(uint8* Buffer)
	{
		IFileHandle* Handle = IPlatformFile::GetPlatformPhysical().OpenRead(*PanicPakFile.ToString());
		UE_CLOG(!Handle, LogPakFile, Fatal, TEXT("PanicSyncRead failed to open pak file %s"), *PanicPakFile.ToString());
		if (!Handle->Seek(Offset))
		{
			UE_LOG(LogPakFile, Fatal, TEXT("PanicSyncRead failed to seek pak file %s   %d bytes at %lld "), *PanicPakFile.ToString(), BytesToRead, Offset);
		}

		if (!Handle->Read(Buffer, BytesToRead))
		{
			UE_LOG(LogPakFile, Fatal, TEXT("PanicSyncRead failed to read pak file %s   %d bytes at %lld "), *PanicPakFile.ToString(), BytesToRead, Offset);
		}	
		delete Handle;
	}
};

class FPakEncryptedReadRequest : public FPakReadRequestBase
{
	int64 OriginalOffset;
	int64 OriginalSize;
	FGuid EncryptionKeyGuid;

public:

	FPakEncryptedReadRequest(FPakFile* InActualPakFile, FName InPakFile, int64 PakFileSize, FAsyncFileCallBack* CompleteCallback, int64 InPakFileStartOffset, int64 InFileOffset, int64 InBytesToRead, EAsyncIOPriorityAndFlags InPriorityAndFlags, uint8* UserSuppliedMemory, const FGuid& InEncryptionKeyGuid, bool bInInternalRequest = false, FCachedAsyncBlock* InBlockPtr = nullptr)
		: FPakReadRequestBase(InPakFile, PakFileSize, CompleteCallback, InPakFileStartOffset + InFileOffset, InBytesToRead, InPriorityAndFlags, UserSuppliedMemory, bInInternalRequest, InBlockPtr)
		, OriginalOffset(InPakFileStartOffset + InFileOffset)
		, OriginalSize(InBytesToRead)
		, EncryptionKeyGuid(InEncryptionKeyGuid)
	{
		Offset = InPakFileStartOffset + AlignDown(InFileOffset, FAES::AESBlockSize);
		BytesToRead = Align(InFileOffset + InBytesToRead, FAES::AESBlockSize) - AlignDown(InFileOffset, FAES::AESBlockSize);

		if (!FPakPrecacher::Get().QueueRequest(this, InActualPakFile, InPakFile, PakFileSize, Offset, BytesToRead, InPriorityAndFlags))
		{
			bRequestOutstanding = false;
			SetComplete();
		}
	}

	virtual void RequestIsComplete() override
	{
		check(bRequestOutstanding);
		if (!bCanceled && (bInternalRequest || ( PriorityAndFlags & AIOP_FLAG_PRECACHE) == 0 ))
		{
			uint8* OversizedBuffer = nullptr;
			if (OriginalOffset != Offset || OriginalSize != BytesToRead)
			{
				// We've read some bytes from before the requested offset, so we need to grab that larger amount
				// from read request and then cut out the bit we want!
				OversizedBuffer = (uint8*)FMemory::Malloc(BytesToRead);
			}
			uint8* DestBuffer = Memory;

			if (!bUserSuppliedMemory)
			{
				check(!Memory);
				DestBuffer = (uint8*)FMemory::Malloc(OriginalSize);
				INC_MEMORY_STAT_BY(STAT_AsyncFileMemory, OriginalSize);
			}
			else
			{
				check(DestBuffer);
			}

			if (!FPakPrecacher::Get().GetCompletedRequest(this, OversizedBuffer != nullptr ? OversizedBuffer : DestBuffer))
			{
				check(bCanceled);
				if (!bUserSuppliedMemory)
				{
					check(!Memory && DestBuffer);
					FMemory::Free(DestBuffer);
					DEC_MEMORY_STAT_BY(STAT_AsyncFileMemory, OriginalSize);
					DestBuffer = nullptr;
				}
				if (OversizedBuffer)
				{
					FMemory::Free(OversizedBuffer);
					OversizedBuffer = nullptr;
				}
			}
			else
			{
				Memory = DestBuffer;
				check(Memory);
				INC_DWORD_STAT(STAT_PakCache_UncompressedDecrypts);

				if (OversizedBuffer)
				{
					check(IsAligned(BytesToRead, FAES::AESBlockSize));
					DecryptData(OversizedBuffer, BytesToRead, EncryptionKeyGuid);
					FMemory::Memcpy(Memory, OversizedBuffer + (OriginalOffset - Offset), OriginalSize);
					FMemory::Free(OversizedBuffer);
				}
				else
				{
					check(IsAligned(OriginalSize, FAES::AESBlockSize));
					DecryptData(Memory, OriginalSize, EncryptionKeyGuid);
				}
			}
		}
		SetDataComplete();
		{
			FScopeLock Lock(&FPakReadRequestEvent);
			bRequestOutstanding = false;
			if (WaitEvent)
			{
				WaitEvent->Trigger();
			}
			SetAllComplete();
		}
	}
};

class FPakProcessedReadRequest : public IAsyncReadRequest
{
	FPakAsyncReadFileHandle* Owner;
	int64 Offset;
	int64 BytesToRead;
	FEvent* WaitEvent;
	FThreadSafeCounter CompleteRace; // this is used to resolve races with natural completion and cancel; there can be only one.
	EAsyncIOPriorityAndFlags PriorityAndFlags;
	bool bRequestOutstanding;
	bool bHasCancelled;
	bool bHasCompleted;

	TSet<FCachedAsyncBlock*> MyCanceledBlocks;

public:
	FPakProcessedReadRequest(FPakAsyncReadFileHandle* InOwner, FAsyncFileCallBack* CompleteCallback, int64 InOffset, int64 InBytesToRead, EAsyncIOPriorityAndFlags InPriorityAndFlags, uint8* UserSuppliedMemory)
		: IAsyncReadRequest(CompleteCallback, false, UserSuppliedMemory)
		, Owner(InOwner)
		, Offset(InOffset)
		, BytesToRead(InBytesToRead)
		, WaitEvent(nullptr)
		, PriorityAndFlags(InPriorityAndFlags)
		, bRequestOutstanding(true)
		, bHasCancelled(false)
		, bHasCompleted(false)
	{
		check(Offset >= 0 && BytesToRead > 0);
		check( ( PriorityAndFlags & AIOP_FLAG_PRECACHE ) == 0 || !bUserSuppliedMemory); // you never get bits back from a precache request, so why supply memory?
	}

	virtual ~FPakProcessedReadRequest()
	{
		check(!MyCanceledBlocks.Num());
		DoneWithRawRequests();
		if (Memory && !bUserSuppliedMemory)
		{
			// this can happen with a race on cancel, it is ok, they didn't take the memory, free it now
			check(BytesToRead > 0);
			DEC_MEMORY_STAT_BY(STAT_AsyncFileMemory, BytesToRead);
			FMemory::Free(Memory);
		}
		Memory = nullptr;
	}

	virtual void WaitCompletionImpl(float TimeLimitSeconds) override
	{
		{
			FScopeLock Lock(&FPakReadRequestEvent);
			if (bRequestOutstanding)
			{
				check(!WaitEvent);
				WaitEvent = FPlatformProcess::GetSynchEventFromPool(true);
			}
		}
		if (WaitEvent)
		{
			if (TimeLimitSeconds == 0.0f)
			{
				WaitEvent->Wait();
				check(!bRequestOutstanding);
			}
			else
			{
				WaitEvent->Wait(TimeLimitSeconds * 1000.0f);
			}
			FScopeLock Lock(&FPakReadRequestEvent);
			FPlatformProcess::ReturnSynchEventToPool(WaitEvent);
			WaitEvent = nullptr;
		}
	}
	virtual void CancelImpl() override
	{
		check(!WaitEvent); // you canceled from a different thread that you waited from
		if (CompleteRace.Increment() == 1)
		{
			if (bRequestOutstanding)
			{
				CancelRawRequests();
				if (!MyCanceledBlocks.Num())
				{
					bRequestOutstanding = false;
					SetComplete();
				}
			}
		}
	}

	void RequestIsComplete()
	{
		if (CompleteRace.Increment() == 1)
		{
			check(bRequestOutstanding);
			if (!bCanceled && ( PriorityAndFlags & AIOP_FLAG_PRECACHE) == 0 )
			{
				GatherResults();
			}
			SetDataComplete();
			{
				FScopeLock Lock(&FPakReadRequestEvent);
				bRequestOutstanding = false;
				if (WaitEvent)
				{
					WaitEvent->Trigger();
				}
				SetAllComplete();
			}
		}
	}
	bool CancelBlockComplete(FCachedAsyncBlock* BlockPtr)
	{
		check(MyCanceledBlocks.Contains(BlockPtr));
		MyCanceledBlocks.Remove(BlockPtr);
		if (!MyCanceledBlocks.Num())
		{
			FScopeLock Lock(&FPakReadRequestEvent);
			bRequestOutstanding = false;
			if (WaitEvent)
			{
				WaitEvent->Trigger();
			}
			SetComplete();
			return true;
		}
		return false;
	}


	void GatherResults();
	void DoneWithRawRequests();
	bool CheckCompletion(const FPakEntry& FileEntry, int32 BlockIndex, TArray<FCachedAsyncBlock*>& Blocks);
	void CancelRawRequests();
};

FAutoConsoleTaskPriority CPrio_AsyncIOCPUWorkTaskPriority(
	TEXT("TaskGraph.TaskPriorities.AsyncIOCPUWork"),
	TEXT("Task and thread priority for decompression, decryption and signature checking of async IO from a pak file."),
	ENamedThreads::BackgroundThreadPriority, // if we have background priority task threads, then use them...
	ENamedThreads::NormalTaskPriority, // .. at normal task priority
	ENamedThreads::NormalTaskPriority // if we don't have background threads, then use normal priority threads at normal task priority instead
);

class FAsyncIOCPUWorkTask
{
	FPakAsyncReadFileHandle& Owner;
	FCachedAsyncBlock* BlockPtr;

public:
	FORCEINLINE FAsyncIOCPUWorkTask(FPakAsyncReadFileHandle& InOwner, FCachedAsyncBlock* InBlockPtr)
		: Owner(InOwner)
		, BlockPtr(InBlockPtr)
	{
	}
	static FORCEINLINE TStatId GetStatId()
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FAsyncIOCPUWorkTask, STATGROUP_TaskGraphTasks);
	}
	static FORCEINLINE ENamedThreads::Type GetDesiredThread()
	{
		return CPrio_AsyncIOCPUWorkTaskPriority.Get();
	}
	FORCEINLINE static ESubsequentsMode::Type GetSubsequentsMode()
	{
		return ESubsequentsMode::TrackSubsequents;
	}
	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent);
};

class FAsyncIOSignatureCheckTask
{
	bool bWasCanceled;
	IAsyncReadRequest* Request;
	int32 IndexToFill;

public:
	FORCEINLINE FAsyncIOSignatureCheckTask(bool bInWasCanceled, IAsyncReadRequest* InRequest, int32 InIndexToFill)
		: bWasCanceled(bInWasCanceled)
		, Request(InRequest)
		, IndexToFill(InIndexToFill)
	{
	}

	static FORCEINLINE TStatId GetStatId()
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FAsyncIOSignatureCheckTask, STATGROUP_TaskGraphTasks);
	}
	static FORCEINLINE ENamedThreads::Type GetDesiredThread()
	{
		return CPrio_AsyncIOCPUWorkTaskPriority.Get();
	}
	FORCEINLINE static ESubsequentsMode::Type GetSubsequentsMode()
	{
		return ESubsequentsMode::TrackSubsequents;
	}
	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		FPakPrecacher::Get().DoSignatureCheck(bWasCanceled, Request, IndexToFill);
	}
};

void FPakPrecacher::StartSignatureCheck(bool bWasCanceled, IAsyncReadRequest* Request, int32 Index)
{
	TGraphTask<FAsyncIOSignatureCheckTask>::CreateTask().ConstructAndDispatchWhenReady(bWasCanceled, Request, Index);
}

void FPakPrecacher::DoSignatureCheck(bool bWasCanceled, IAsyncReadRequest* Request, int32 Index)
{
	int64 SignatureIndex = -1;
	int64 NumSignaturesToCheck = 0;
	const uint8* Data = nullptr;
	int64 RequestSize = 0;
	int64 RequestOffset = 0;
	uint16 PakIndex;
	FSHAHash MasterSignatureHash;
	static const int64 MaxHashesToCache = 16;

#if PAKHASH_USE_CRC
	TPakChunkHash HashCache[MaxHashesToCache] = { 0 };
#else
	TPakChunkHash HashCache[MaxHashesToCache];
#endif

	{
		// Try and keep lock for as short a time as possible. Find our request and copy out the data we need
		FScopeLock Lock(&CachedFilesScopeLock);
		FRequestToLower& RequestToLower = RequestsToLower[Index];
		RequestToLower.RequestHandle = Request;
		RequestToLower.Memory = Request->GetReadResults();

		NumSignaturesToCheck = Align(RequestToLower.RequestSize, FPakInfo::MaxChunkDataSize) / FPakInfo::MaxChunkDataSize;
		check(NumSignaturesToCheck >= 1);

		FCacheBlock& Block = CacheBlockAllocator.Get(RequestToLower.BlockIndex);
		RequestOffset = GetRequestOffset(Block.OffsetAndPakIndex);
		check((RequestOffset % FPakInfo::MaxChunkDataSize) == 0);
		RequestSize = RequestToLower.RequestSize;
		PakIndex = GetRequestPakIndex(Block.OffsetAndPakIndex);
		Data = RequestToLower.Memory;
		SignatureIndex = RequestOffset / FPakInfo::MaxChunkDataSize;

		FPakData& PakData = CachedPakData[PakIndex];
		MasterSignatureHash = PakData.Signatures->DecryptedHash;

		for (int32 CacheIndex = 0; CacheIndex < FMath::Min(NumSignaturesToCheck, MaxHashesToCache); ++CacheIndex)
		{
			HashCache[CacheIndex] = PakData.Signatures->ChunkHashes[SignatureIndex + CacheIndex];
		}
	}

	check(Data);
	check(NumSignaturesToCheck > 0);
	check(RequestSize > 0);
	check(RequestOffset >= 0);

	// Hash the contents of the incoming buffer and check that it matches what we expected
	for (int64 SignedChunkIndex = 0; SignedChunkIndex < NumSignaturesToCheck; ++SignedChunkIndex, ++SignatureIndex)
	{
		int64 Size = FMath::Min(RequestSize, (int64)FPakInfo::MaxChunkDataSize);

		if ((SignedChunkIndex > 0) && ((SignedChunkIndex % MaxHashesToCache) == 0))
		{
			FScopeLock Lock(&CachedFilesScopeLock);
			FPakData& PakData = CachedPakData[PakIndex];
			for (int32 CacheIndex = 0; (CacheIndex < MaxHashesToCache) && ((SignedChunkIndex + CacheIndex) < NumSignaturesToCheck); ++CacheIndex)
			{
				HashCache[CacheIndex] = PakData.Signatures->ChunkHashes[SignatureIndex + CacheIndex];
			}
		}

		{
			SCOPE_SECONDS_ACCUMULATOR(STAT_PakCache_SigningChunkHashTime);

			TPakChunkHash ThisHash = ComputePakChunkHash(Data, Size);
			bool bChunkHashesMatch = (ThisHash == HashCache[SignedChunkIndex % MaxHashesToCache]);

			if (!bChunkHashesMatch)
			{
				FScopeLock Lock(&CachedFilesScopeLock);
				FPakData* PakData = &CachedPakData[PakIndex];

				UE_LOG(LogPakFile, Warning, TEXT("Pak chunk signing mismatch on chunk [%i/%i]! Expected %s, Received %s"), SignatureIndex, PakData->Signatures->ChunkHashes.Num() - 1, *ChunkHashToString(PakData->Signatures->ChunkHashes[SignatureIndex]), *ChunkHashToString(ThisHash));

				// Check the signatures are still as we expected them
				if (PakData->Signatures->DecryptedHash != PakData->Signatures->ComputeCurrentMasterHash())
				{
					UE_LOG(LogPakFile, Warning, TEXT("Master signature table has changed since initialization!"));
				}

				FPakChunkSignatureCheckFailedData FailedData(PakData->Name.ToString(), HashCache[SignedChunkIndex % MaxHashesToCache], ThisHash, SignatureIndex);
				FPakPlatformFile::BroadcastPakChunkSignatureCheckFailure(FailedData);
			}
		}

		INC_MEMORY_STAT_BY(STAT_PakCache_SigningChunkHashSize, Size);

		RequestOffset += Size;
		Data += Size;
		RequestSize -= Size;
	}

	NewRequestsToLowerComplete(bWasCanceled, Request, Index);
}

class FPakAsyncReadFileHandle final : public IAsyncReadFileHandle
{
	FName PakFile;
	FPakFile* ActualPakFile;
	int64 PakFileSize;
	int64 OffsetInPak;
	int64 UncompressedFileSize;
	FPakEntry FileEntry;
	TSet<FPakProcessedReadRequest*> LiveRequests;
	TArray<FCachedAsyncBlock*> Blocks;
	FAsyncFileCallBack ReadCallbackFunction;
	FCriticalSection CriticalSection;
	int32 NumLiveRawRequests;
	FName CompressionMethod;
	int64 CompressedChunkOffset;
	FGuid EncryptionKeyGuid;

	TMap<FCachedAsyncBlock*, FPakProcessedReadRequest*> OutstandingCancelMapBlock;

	FCachedAsyncBlock& GetBlock(int32 Index)
	{
		if (!Blocks[Index])
		{
			Blocks[Index] = new FCachedAsyncBlock;
			Blocks[Index]->BlockIndex = Index;
		}
		return *Blocks[Index];
	}


public:
	FPakAsyncReadFileHandle(const FPakEntry* InFileEntry, FPakFile* InPakFile, const TCHAR* Filename)
		: PakFile(InPakFile->GetFilenameName())
		, ActualPakFile(InPakFile)
		, PakFileSize(InPakFile->TotalSize())
		, FileEntry(*InFileEntry)
		, NumLiveRawRequests(0)
		, CompressedChunkOffset(0)
		, EncryptionKeyGuid(InPakFile->GetInfo().EncryptionKeyGuid)
	{
		OffsetInPak = FileEntry.Offset + FileEntry.GetSerializedSize(InPakFile->GetInfo().Version);
		UncompressedFileSize = FileEntry.UncompressedSize;
		int64 CompressedFileSize = FileEntry.UncompressedSize;
		CompressionMethod = InPakFile->GetInfo().GetCompressionMethod(FileEntry.CompressionMethodIndex);
		if (CompressionMethod != NAME_None && UncompressedFileSize)
		{
			check(FileEntry.CompressionBlocks.Num());
			CompressedFileSize = FileEntry.CompressionBlocks.Last().CompressedEnd - FileEntry.CompressionBlocks[0].CompressedStart;
			check(CompressedFileSize >= 0);
			const int32 CompressionBlockSize = FileEntry.CompressionBlockSize;
			check((UncompressedFileSize + CompressionBlockSize - 1) / CompressionBlockSize == FileEntry.CompressionBlocks.Num());
			Blocks.AddDefaulted(FileEntry.CompressionBlocks.Num());
			CompressedChunkOffset = InPakFile->GetInfo().HasRelativeCompressedChunkOffsets() ? FileEntry.Offset : 0;
		}
		UE_LOG(LogPakFile, Verbose, TEXT("FPakPlatformFile::OpenAsyncRead[%016llX, %016llX) %s"), OffsetInPak, OffsetInPak + CompressedFileSize, Filename);
		check(PakFileSize > 0 && OffsetInPak + CompressedFileSize <= PakFileSize && OffsetInPak >= 0);

		ReadCallbackFunction = [this](bool bWasCancelled, IAsyncReadRequest* Request)
		{
			RawReadCallback(bWasCancelled, Request);
		};

	}
	~FPakAsyncReadFileHandle()
	{
		FScopeLock ScopedLock(&CriticalSection);
		if (LiveRequests.Num() > 0 || NumLiveRawRequests > 0)
		{
			UE_LOG(LogPakFile, Fatal, TEXT("LiveRequests.Num or NumLiveRawReqeusts was > 0 in ~FPakAsyncReadFileHandle!"));
		}
		check(!LiveRequests.Num()); // must delete all requests before you delete the handle
		check(!NumLiveRawRequests); // must delete all requests before you delete the handle
		for (FCachedAsyncBlock* Block : Blocks)
		{
			if (Block)
			{
				check(Block->RefCount == 0);
				ClearBlock(*Block, true);
				delete Block;
			}
		}
	}

	virtual IAsyncReadRequest* SizeRequest(FAsyncFileCallBack* CompleteCallback = nullptr) override
	{
		return new FPakSizeRequest(CompleteCallback, UncompressedFileSize);
	}
	virtual IAsyncReadRequest* ReadRequest(int64 Offset, int64 BytesToRead, EAsyncIOPriorityAndFlags PriorityAndFlags = AIOP_Normal, FAsyncFileCallBack* CompleteCallback = nullptr, uint8* UserSuppliedMemory = nullptr) override
	{
		LLM_SCOPE(ELLMTag::FileSystem);

		if (BytesToRead == MAX_int64)
		{
			BytesToRead = UncompressedFileSize - Offset;
		}
		check(Offset + BytesToRead <= UncompressedFileSize && Offset >= 0);
		if (CompressionMethod == NAME_None)
		{
			check(Offset + BytesToRead + OffsetInPak <= PakFileSize);
			check(!Blocks.Num());

			if (FileEntry.IsEncrypted())
			{
				return new FPakEncryptedReadRequest(ActualPakFile, PakFile, PakFileSize, CompleteCallback, OffsetInPak, Offset, BytesToRead, PriorityAndFlags, UserSuppliedMemory, EncryptionKeyGuid);
			}
			else
			{
				return new FPakReadRequest(ActualPakFile, PakFile, PakFileSize, CompleteCallback, OffsetInPak + Offset, BytesToRead, PriorityAndFlags, UserSuppliedMemory);
			}
		}
		bool bAnyUnfinished = false;
		FPakProcessedReadRequest* Result;
		{
			FScopeLock ScopedLock(&CriticalSection);
			check(Blocks.Num());
			int32 FirstBlock = Offset / FileEntry.CompressionBlockSize;
			int32 LastBlock = (Offset + BytesToRead - 1) / FileEntry.CompressionBlockSize;

			check(FirstBlock >= 0 && FirstBlock < Blocks.Num() && LastBlock >= 0 && LastBlock < Blocks.Num() && FirstBlock <= LastBlock);

			Result = new FPakProcessedReadRequest(this, CompleteCallback, Offset, BytesToRead, PriorityAndFlags, UserSuppliedMemory);
			for (int32 BlockIndex = FirstBlock; BlockIndex <= LastBlock; BlockIndex++)
			{

				FCachedAsyncBlock& Block = GetBlock(BlockIndex);
				Block.RefCount++;
				if (!Block.bInFlight)
				{
					check(Block.RefCount == 1);
					StartBlock(BlockIndex, PriorityAndFlags);
					bAnyUnfinished = true;
				}
				if (!Block.Processed)
				{
					bAnyUnfinished = true;
				}
			}
			check(!LiveRequests.Contains(Result));
			LiveRequests.Add(Result);
			if (!bAnyUnfinished)
			{
				Result->RequestIsComplete();
			}
		}
		return Result;
	}

	void StartBlock(int32 BlockIndex, EAsyncIOPriorityAndFlags PriorityAndFlags)
	{
		FCachedAsyncBlock& Block = GetBlock(BlockIndex);
		Block.bInFlight = true;
		check(!Block.RawRequest && !Block.Processed && !Block.Raw && !Block.CPUWorkGraphEvent.GetReference() && !Block.ProcessedSize && !Block.RawSize && !Block.bCPUWorkIsComplete);
		Block.RawSize = FileEntry.CompressionBlocks[BlockIndex].CompressedEnd - FileEntry.CompressionBlocks[BlockIndex].CompressedStart;
		Block.DecompressionRawSize = Block.RawSize;
		if (FileEntry.IsEncrypted())
		{
			Block.RawSize = Align(Block.RawSize, FAES::AESBlockSize);
		}
		NumLiveRawRequests++;
		Block.RawRequest = new FPakReadRequest(ActualPakFile, PakFile, PakFileSize, &ReadCallbackFunction, FileEntry.CompressionBlocks[BlockIndex].CompressedStart + CompressedChunkOffset, Block.RawSize, PriorityAndFlags, nullptr, true, &Block);
	}
	void RawReadCallback(bool bWasCancelled, IAsyncReadRequest* InRequest)
	{
		// CAUTION, no lock here!
		FPakReadRequest* Request = static_cast<FPakReadRequest*>(InRequest);

		FCachedAsyncBlock& Block = Request->GetBlock();
		check((Block.RawRequest == Request || (!Block.RawRequest && Block.RawSize)) // we still might be in the constructor so the assignment hasn't happened yet
			&& !Block.Processed && !Block.Raw);

		Block.Raw = Request->GetReadResults();
		FPlatformMisc::MemoryBarrier();
		if (Block.bCancelledBlock || !Block.Raw)
		{
			check(Block.bCancelledBlock);
			if (Block.Raw)
			{
				FMemory::Free(Block.Raw);
				Block.Raw = nullptr;
				check(Block.RawSize > 0);
				DEC_MEMORY_STAT_BY(STAT_AsyncFileMemory, Block.RawSize);
				Block.RawSize = 0;
			}
		}
		else
		{
			check(Block.Raw);
			Block.ProcessedSize = FileEntry.CompressionBlockSize;
			if (Block.BlockIndex == Blocks.Num() - 1)
			{
				Block.ProcessedSize = FileEntry.UncompressedSize % FileEntry.CompressionBlockSize;
				if (!Block.ProcessedSize)
				{
					Block.ProcessedSize = FileEntry.CompressionBlockSize; // last block was a full block
				}
			}
			check(Block.ProcessedSize && !Block.bCPUWorkIsComplete);
		}
		Block.CPUWorkGraphEvent = TGraphTask<FAsyncIOCPUWorkTask>::CreateTask().ConstructAndDispatchWhenReady(*this, &Block);
	}
	void DoProcessing(FCachedAsyncBlock* BlockPtr)
	{
		FCachedAsyncBlock& Block = *BlockPtr;
		check(!Block.Processed);
		uint8* Output = nullptr;
		if (Block.Raw)
		{
			check(Block.Raw && Block.RawSize && !Block.Processed);

#if !UE_BUILD_SHIPPING
			bool bCorrupted = false;
			if (GPakCache_ForceDecompressionFails && FMath::FRand() < 0.001f)
			{
				int32 CorruptOffset = FMath::Clamp(int32(FMath::FRandRange(0, Block.RawSize - 1)), 0, Block.RawSize - 1);
				uint8 CorruptValue = uint8(FMath::Clamp(int32(FMath::FRandRange(0, 255)), 0, 255));
				if (Block.Raw[CorruptOffset] != CorruptValue)
				{
					UE_LOG(LogPakFile, Error, TEXT("Forcing corruption of decompression source data (predecryption) to verify panic read recovery.  Offset = %d, Value = 0x%x"), CorruptOffset, int32(CorruptValue));
					Block.Raw[CorruptOffset] = CorruptValue;
					bCorrupted = true;
				}
			}
#endif


			if (FileEntry.IsEncrypted())
			{
				INC_DWORD_STAT(STAT_PakCache_CompressedDecrypts);
				check(IsAligned(Block.RawSize, FAES::AESBlockSize));
				DecryptData(Block.Raw, Block.RawSize, EncryptionKeyGuid);
			}

			check(Block.ProcessedSize > 0);
			INC_MEMORY_STAT_BY(STAT_AsyncFileMemory, Block.ProcessedSize);
			Output = (uint8*)FMemory::Malloc(Block.ProcessedSize);
			if (FileEntry.IsEncrypted())
			{
				check(Align(Block.DecompressionRawSize, FAES::AESBlockSize) == Block.RawSize);
			}
			else
			{
				check(Block.DecompressionRawSize == Block.RawSize);
			}

			bool bFailed = !FCompression::UncompressMemory(CompressionMethod, Output, Block.ProcessedSize, Block.Raw, Block.DecompressionRawSize);

#if !UE_BUILD_SHIPPING
			if (bCorrupted && !bFailed)
			{
				UE_LOG(LogPakFile, Error, TEXT("The payload was corrupted, but this did not trigger a decompression failed.....pretending it failed anyway because otherwise it can crash later."));
				bFailed = true;
			}
#endif

			if (bFailed)
			{
				{
					const FString HexBytes = BytesToHex(Block.Raw, FMath::Min(Block.DecompressionRawSize, 32));
					UE_LOG(LogPakFile, Error, TEXT("Pak Decompression failed. PakFile:%s, EntryOffset:%lld, EntrySize:%lld, Method:%s, ProcessedSize:%d, RawSize:%d, Crc32:%u, BlockIndex:%d, Encrypt:%d, Delete:%d, Output:%p, Raw:%p, Processed:%p, Bytes:[%s...]"), *PakFile.ToString(), FileEntry.Offset, FileEntry.Size, *CompressionMethod.ToString(), Block.ProcessedSize, Block.DecompressionRawSize, FCrc::MemCrc32(Block.Raw, Block.DecompressionRawSize), Block.BlockIndex, FileEntry.IsEncrypted() ? 1 : 0, FileEntry.IsDeleteRecord() ? 1 : 0, Output, Block.Raw, Block.Processed, *HexBytes);
				}
				uint8* TempBuffer = (uint8*)FMemory::Malloc(Block.RawSize);
				{
					FScopeLock ScopedLock(&CriticalSection);
					UE_CLOG(!Block.RawRequest, LogPakFile, Fatal, TEXT("Cannot retry because Block.RawRequest is null."));

					Block.RawRequest->PanicSyncRead(TempBuffer);
				}

				if (FileEntry.IsEncrypted())
				{
					DecryptData(TempBuffer, Block.RawSize, EncryptionKeyGuid);
				}
				if (FMemory::Memcmp(TempBuffer, Block.Raw, Block.DecompressionRawSize) != 0)
				{
					UE_LOG(LogPakFile, Warning, TEXT("Panic re-read (and decrypt if applicable) resulted in a different buffer."));

					int32 Offset = 0;
					for (; Offset < Block.DecompressionRawSize; Offset++)
					{
						if (TempBuffer[Offset] != Block.Raw[Offset])
						{
							break;
						}
					}
					UE_CLOG(Offset >= Block.DecompressionRawSize, LogPakFile, Fatal, TEXT("Buffers were different yet all bytes were the same????"));

					UE_LOG(LogPakFile, Warning, TEXT("Buffers differ at offset %d."), Offset);
					const FString HexBytes1 = BytesToHex(Block.Raw + Offset, FMath::Min(Block.DecompressionRawSize - Offset, 64));
					UE_LOG(LogPakFile, Warning, TEXT("Original read (and decrypt) %s"), *HexBytes1);
					const FString HexBytes2 = BytesToHex(TempBuffer + Offset, FMath::Min(Block.DecompressionRawSize - Offset, 64));
					UE_LOG(LogPakFile, Warning, TEXT("Panic reread  (and decrypt) %s"), *HexBytes2);
				}
				if (!FCompression::UncompressMemory(CompressionMethod, Output, Block.ProcessedSize, TempBuffer, Block.DecompressionRawSize))
				{
					UE_LOG(LogPakFile, Fatal, TEXT("Retry was NOT sucessful."));
				}
				else
				{
					UE_LOG(LogPakFile, Warning, TEXT("Retry was sucessful."));
				}
				FMemory::Free(TempBuffer);
			}
			FMemory::Free(Block.Raw);
			Block.Raw = nullptr;
			check(Block.RawSize > 0);
			DEC_MEMORY_STAT_BY(STAT_AsyncFileMemory, Block.RawSize);
			Block.RawSize = 0;
		}
		else
		{
			check(Block.ProcessedSize == 0);
		}

		{
			FScopeLock ScopedLock(&CriticalSection);
			check(!Block.Processed);
			Block.Processed = Output;
			if (Block.RawRequest)
			{
				Block.RawRequest->WaitCompletion();
				delete Block.RawRequest;
				Block.RawRequest = nullptr;
				NumLiveRawRequests--;
			}
			if (Block.RefCount > 0)
			{
				check(&Block == Blocks[Block.BlockIndex] && !Block.bCancelledBlock);
				TArray<FPakProcessedReadRequest*, TInlineAllocator<4> > CompletedRequests;
				for (FPakProcessedReadRequest* Req : LiveRequests)
				{
					if (Req->CheckCompletion(FileEntry, Block.BlockIndex, Blocks))
					{
						CompletedRequests.Add(Req);
					}
				}
				for (FPakProcessedReadRequest* Req : CompletedRequests)
				{
					if (LiveRequests.Contains(Req))
					{
						Req->RequestIsComplete();
					}
				}
				Block.bCPUWorkIsComplete = true;
			}
			else
			{
				check(&Block != Blocks[Block.BlockIndex] && Block.bCancelledBlock);
				// must have been canceled, clean up
				FPakProcessedReadRequest* Owner;

				check(OutstandingCancelMapBlock.Contains(&Block));
				Owner = OutstandingCancelMapBlock[&Block];
				OutstandingCancelMapBlock.Remove(&Block);
				check(LiveRequests.Contains(Owner));

				if (Owner->CancelBlockComplete(&Block))
				{
					LiveRequests.Remove(Owner);
				}
				ClearBlock(Block);
				delete &Block;
			}
		}
	}
	void ClearBlock(FCachedAsyncBlock& Block, bool bForDestructorShouldAlreadyBeClear = false)
	{
		check(!Block.RawRequest);
		Block.RawRequest = nullptr;
		//check(!Block.CPUWorkGraphEvent || Block.CPUWorkGraphEvent->IsComplete());
		Block.CPUWorkGraphEvent = nullptr;
		if (Block.Raw)
		{
			check(!bForDestructorShouldAlreadyBeClear);
			// this was a cancel, clean it up now
			FMemory::Free(Block.Raw);
			Block.Raw = nullptr;
			check(Block.RawSize > 0);
			DEC_MEMORY_STAT_BY(STAT_AsyncFileMemory, Block.RawSize);
		}
		Block.RawSize = 0;
		if (Block.Processed)
		{
			check(bForDestructorShouldAlreadyBeClear == false);
			FMemory::Free(Block.Processed);
			Block.Processed = nullptr;
			check(Block.ProcessedSize > 0);
			DEC_MEMORY_STAT_BY(STAT_AsyncFileMemory, Block.ProcessedSize);
		}
		Block.ProcessedSize = 0;
		Block.bCPUWorkIsComplete = false;
		Block.bInFlight = false;
	}

	void RemoveRequest(FPakProcessedReadRequest* Req, int64 Offset, int64 BytesToRead, const bool& bAlreadyCancelled)
	{
		FScopeLock ScopedLock(&CriticalSection);
		if (bAlreadyCancelled)
		{
			check(!LiveRequests.Contains(Req));
			return;
		}

		check(LiveRequests.Contains(Req));
		LiveRequests.Remove(Req);
		int32 FirstBlock = Offset / FileEntry.CompressionBlockSize;
		int32 LastBlock = (Offset + BytesToRead - 1) / FileEntry.CompressionBlockSize;
		check(FirstBlock >= 0 && FirstBlock < Blocks.Num() && LastBlock >= 0 && LastBlock < Blocks.Num() && FirstBlock <= LastBlock);

		for (int32 BlockIndex = FirstBlock; BlockIndex <= LastBlock; BlockIndex++)
		{
			FCachedAsyncBlock& Block = GetBlock(BlockIndex);
			check(Block.RefCount > 0);
			if (!--Block.RefCount)
			{
				if (Block.RawRequest)
				{
					Block.RawRequest->Cancel();
					Block.RawRequest->WaitCompletion();
					delete Block.RawRequest;
					Block.RawRequest = nullptr;
					NumLiveRawRequests--;
				}
				ClearBlock(Block);
			}
		}
	}

	void HandleCanceledRequest(TSet<FCachedAsyncBlock*>& MyCanceledBlocks, FPakProcessedReadRequest* Req, int64 Offset, int64 BytesToRead, bool& bHasCancelledRef)
	{
		FScopeLock ScopedLock(&CriticalSection);
		check(!bHasCancelledRef);
		bHasCancelledRef = true;
		check(LiveRequests.Contains(Req));
		int32 FirstBlock = Offset / FileEntry.CompressionBlockSize;
		int32 LastBlock = (Offset + BytesToRead - 1) / FileEntry.CompressionBlockSize;
		check(FirstBlock >= 0 && FirstBlock < Blocks.Num() && LastBlock >= 0 && LastBlock < Blocks.Num() && FirstBlock <= LastBlock);

		for (int32 BlockIndex = FirstBlock; BlockIndex <= LastBlock; BlockIndex++)
		{
			FCachedAsyncBlock& Block = GetBlock(BlockIndex);
			check(Block.RefCount > 0);
			if (!--Block.RefCount)
			{
				if (Block.bInFlight && !Block.bCPUWorkIsComplete)
				{
					MyCanceledBlocks.Add(&Block);
					Blocks[BlockIndex] = nullptr;
					check(!OutstandingCancelMapBlock.Contains(&Block));
					OutstandingCancelMapBlock.Add(&Block, Req);
					Block.bCancelledBlock = true;
					FPlatformMisc::MemoryBarrier();
					Block.RawRequest->Cancel();
				}
				else
				{
					ClearBlock(Block);
				}
			}
		}

		if (!MyCanceledBlocks.Num())
		{
			LiveRequests.Remove(Req);
		}
	}


	void GatherResults(uint8* Memory, int64 Offset, int64 BytesToRead)
	{
		// no lock here, I don't think it is needed because we have a ref count.
		int32 FirstBlock = Offset / FileEntry.CompressionBlockSize;
		int32 LastBlock = (Offset + BytesToRead - 1) / FileEntry.CompressionBlockSize;
		check(FirstBlock >= 0 && FirstBlock < Blocks.Num() && LastBlock >= 0 && LastBlock < Blocks.Num() && FirstBlock <= LastBlock);

		for (int32 BlockIndex = FirstBlock; BlockIndex <= LastBlock; BlockIndex++)
		{
			FCachedAsyncBlock& Block = GetBlock(BlockIndex);
			check(Block.RefCount > 0 && Block.Processed && Block.ProcessedSize);
			int64 BlockStart = int64(BlockIndex) * int64(FileEntry.CompressionBlockSize);
			int64 BlockEnd = BlockStart + Block.ProcessedSize;

			int64 SrcOffset = 0;
			int64 DestOffset = BlockStart - Offset;
			if (DestOffset < 0)
			{
				SrcOffset -= DestOffset;
				DestOffset = 0;
			}
			int64 CopySize = Block.ProcessedSize;
			if (DestOffset + CopySize > BytesToRead)
			{
				CopySize = BytesToRead - DestOffset;
			}
			if (SrcOffset + CopySize > Block.ProcessedSize)
			{
				CopySize = Block.ProcessedSize - SrcOffset;
			}
			check(CopySize > 0 && DestOffset >= 0 && DestOffset + CopySize <= BytesToRead);
			check(SrcOffset >= 0 && SrcOffset + CopySize <= Block.ProcessedSize);
			FMemory::Memcpy(Memory + DestOffset, Block.Processed + SrcOffset, CopySize);

			check(Block.RefCount > 0);
		}
	}
};

void FPakProcessedReadRequest::CancelRawRequests()
{
	Owner->HandleCanceledRequest(MyCanceledBlocks, this, Offset, BytesToRead, bHasCancelled);
}

void FPakProcessedReadRequest::GatherResults()
{
	if (!bUserSuppliedMemory)
	{
		check(!Memory);
		Memory = (uint8*)FMemory::Malloc(BytesToRead);
		INC_MEMORY_STAT_BY(STAT_AsyncFileMemory, BytesToRead);
	}
	check(Memory);
	Owner->GatherResults(Memory, Offset, BytesToRead);
}

void FPakProcessedReadRequest::DoneWithRawRequests()
{
	Owner->RemoveRequest(this, Offset, BytesToRead, bHasCancelled);
}

bool FPakProcessedReadRequest::CheckCompletion(const FPakEntry& FileEntry, int32 BlockIndex, TArray<FCachedAsyncBlock*>& Blocks)
{
	if (!bRequestOutstanding || bHasCompleted || bHasCancelled)
	{
		return false;
	}
	{
		int64 BlockStart = int64(BlockIndex) * int64(FileEntry.CompressionBlockSize);
		int64 BlockEnd = int64(BlockIndex + 1) * int64(FileEntry.CompressionBlockSize);
		if (Offset >= BlockEnd || Offset + BytesToRead <= BlockStart)
		{
			return false;
		}
	}
	int32 FirstBlock = Offset / FileEntry.CompressionBlockSize;
	int32 LastBlock = (Offset + BytesToRead - 1) / FileEntry.CompressionBlockSize;
	check(FirstBlock >= 0 && FirstBlock < Blocks.Num() && LastBlock >= 0 && LastBlock < Blocks.Num() && FirstBlock <= LastBlock);

	for (int32 MyBlockIndex = FirstBlock; MyBlockIndex <= LastBlock; MyBlockIndex++)
	{
		check(Blocks[MyBlockIndex]);
		if (!Blocks[MyBlockIndex]->Processed)
		{
			return false;
		}
	}
	bHasCompleted = true;
	return true;
}

void FAsyncIOCPUWorkTask::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	SCOPED_NAMED_EVENT(FAsyncIOCPUWorkTask_DoTask, FColor::Cyan);
	Owner.DoProcessing(BlockPtr);
}

#endif  


#if PAK_TRACKER
TMap<FString, int32> FPakPlatformFile::GPakSizeMap;

void FPakPlatformFile::TrackPak(const TCHAR* Filename, const FPakEntry* PakEntry)
{
	FString Key(Filename);

	if (!GPakSizeMap.Find(Key))
	{
		GPakSizeMap.Add(Key, PakEntry->Size);
	}
}
#endif

class FBypassPakAsyncReadFileHandle final : public IAsyncReadFileHandle
{
	FName PakFile;
	int64 PakFileSize;
	int64 OffsetInPak;
	int64 UncompressedFileSize;
	FPakEntry FileEntry;
	IAsyncReadFileHandle* LowerHandle;

public:
	FBypassPakAsyncReadFileHandle(const FPakEntry* InFileEntry, FPakFile* InPakFile, const TCHAR* Filename)
		: PakFile(InPakFile->GetFilenameName())
		, PakFileSize(InPakFile->TotalSize())
		, FileEntry(*InFileEntry)
	{
		OffsetInPak = FileEntry.Offset + FileEntry.GetSerializedSize(InPakFile->GetInfo().Version);
		UncompressedFileSize = FileEntry.UncompressedSize;
		int64 CompressedFileSize = FileEntry.UncompressedSize;
		check(FileEntry.CompressionMethodIndex == 0);
		UE_LOG(LogPakFile, Verbose, TEXT("FPakPlatformFile::OpenAsyncRead (FBypassPakAsyncReadFileHandle)[%016llX, %016llX) %s"), OffsetInPak, OffsetInPak + CompressedFileSize, Filename);
		check(PakFileSize > 0 && OffsetInPak + CompressedFileSize <= PakFileSize && OffsetInPak >= 0);

		LowerHandle = IPlatformFile::GetPlatformPhysical().OpenAsyncRead(*InPakFile->GetFilename());
	}
	~FBypassPakAsyncReadFileHandle()
	{
		delete LowerHandle;
	}

	virtual IAsyncReadRequest* SizeRequest(FAsyncFileCallBack* CompleteCallback = nullptr) override
	{
		if (!LowerHandle)
		{
			return nullptr;
		}
		return new FPakSizeRequest(CompleteCallback, UncompressedFileSize);
	}
	virtual IAsyncReadRequest* ReadRequest(int64 Offset, int64 BytesToRead, EAsyncIOPriorityAndFlags PriorityAndFlags = AIOP_Normal, FAsyncFileCallBack* CompleteCallback = nullptr, uint8* UserSuppliedMemory = nullptr) override
	{
		if (!LowerHandle)
		{
			return nullptr;
		}
		if (BytesToRead == MAX_int64)
		{
			BytesToRead = UncompressedFileSize - Offset;
		}
		check(Offset + BytesToRead <= UncompressedFileSize && Offset >= 0);
		check(FileEntry.CompressionMethodIndex == 0);
		check(Offset + BytesToRead + OffsetInPak <= PakFileSize);

		return LowerHandle->ReadRequest(Offset + OffsetInPak, BytesToRead, PriorityAndFlags, CompleteCallback, UserSuppliedMemory);
	}
	virtual bool UsesCache() override
	{
		return LowerHandle->UsesCache();
	}
};

IAsyncReadFileHandle* FPakPlatformFile::OpenAsyncRead(const TCHAR* Filename)
{
	CSV_SCOPED_TIMING_STAT(FileIO, PakOpenAsyncRead);
//	check(GConfig);
#if USE_PAK_PRECACHE
	if (FPlatformProcess::SupportsMultithreading() && GPakCache_Enable > 0)
	{
		FPakEntry FileEntry;
		FPakFile* PakFile = NULL;
		bool bFoundEntry = FindFileInPakFiles(Filename, &PakFile, &FileEntry);
		if (bFoundEntry && PakFile && PakFile->GetFilenameName() != NAME_None)
		{
#if PAK_TRACKER
			TrackPak(Filename, &FileEntry);
#endif

			return new FPakAsyncReadFileHandle(&FileEntry, PakFile, Filename);
		}
	}
#elif PLATFORM_BYPASS_PAK_PRECACHE
	{
		FPakEntry FileEntry;
		FPakFile* PakFile = NULL;
		bool bFoundEntry = FindFileInPakFiles(Filename, &PakFile, &FileEntry);
		if (bFoundEntry && PakFile && PakFile->GetFilenameName() != NAME_None && FileEntry.CompressionMethodIndex == 0 && !FileEntry.IsEncrypted())
		{
#if PAK_TRACKER
			TrackPak(Filename, &FileEntry);
#endif
			return new FBypassPakAsyncReadFileHandle(&FileEntry, PakFile, Filename);
		}
	}
#endif
	return IPlatformFile::OpenAsyncRead(Filename);
}

void FPakPlatformFile::SetAsyncMinimumPriority(EAsyncIOPriorityAndFlags Priority)
{
#if USE_PAK_PRECACHE
	if (FPlatformProcess::SupportsMultithreading() && GPakCache_Enable > 0)
	{
		FPakPrecacher::Get().SetAsyncMinimumPriority(Priority);
	}
#elif PLATFORM_BYPASS_PAK_PRECACHE
	IPlatformFile::GetPlatformPhysical().SetAsyncMinimumPriority(Priority);
#endif
}

void FPakPlatformFile::Tick()
{
#if USE_PAK_PRECACHE && CSV_PROFILER
	if (PakPrecacherSingleton != nullptr)
	{
		CSV_CUSTOM_STAT(FileIO, PakPrecacherRequests, FPakPrecacher::Get().GetRequestCount(), ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(FileIO, PakPrecacherHotBlocksCount, (int32)GPreCacheHotBlocksCount, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(FileIO, PakPrecacherColdBlocksCount, (int32)GPreCacheColdBlocksCount, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(FileIO, PakPrecacherTotalLoadedMB, (int32)(GPreCacheTotalLoaded/(1024*1024)), ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(FileIO, PakPrecacherBlockMemoryMB, (int32)(FPakPrecacher::Get().GetBlockMemory() / (1024 * 1024)), ECsvCustomStatOp::Set);


		if (GPreCacheTotalLoadedLastTick != 0)
		{
			int64 diff = GPreCacheTotalLoaded - GPreCacheTotalLoadedLastTick;
			diff /= 1024;
			CSV_CUSTOM_STAT(FileIO, PakPrecacherPerFrameKB, (int32)diff, ECsvCustomStatOp::Set);
		}
		GPreCacheTotalLoadedLastTick = GPreCacheTotalLoaded;

		CSV_CUSTOM_STAT(FileIO, PakPrecacherSeeks, (int32)GPreCacheSeeks, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(FileIO, PakPrecacherBadSeeks, (int32)GPreCacheBadSeeks, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(FileIO, PakPrecacherContiguousReads, (int32)GPreCacheContiguousReads, ECsvCustomStatOp::Set);
		
		CSV_CUSTOM_STAT(FileIO, PakLoads, (int32)PakPrecacherSingleton->Get().GetLoads(), ECsvCustomStatOp::Set);
}
#endif
#if TRACK_DISK_UTILIZATION && CSV_PROFILER
	CSV_CUSTOM_STAT(DiskIO, OutstandingIORequests, int32(GDiskUtilizationTracker.GetOutstandingRequests()), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(DiskIO, BusyTime, float(GDiskUtilizationTracker.GetShortTermStats().GetTotalIOTimeInSeconds()), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(DiskIO, IdleTime, float(GDiskUtilizationTracker.GetShortTermStats().GetTotalIdleTimeInSeconds()), ECsvCustomStatOp::Set);
#endif
}

class FMappedFilePakProxy final : public IMappedFileHandle
{
	IMappedFileHandle* LowerLevel;
	int64 OffsetInPak;
	int64 PakSize;
	FString DebugFilename;
public:
	FMappedFilePakProxy(IMappedFileHandle* InLowerLevel, int64 InOffset, int64 InSize, int64 InPakSize, const TCHAR* InDebugFilename)
		: IMappedFileHandle(InSize)
		, LowerLevel(InLowerLevel)
		, OffsetInPak(InOffset)
		, PakSize(InPakSize)
		, DebugFilename(InDebugFilename)
	{
		check(PakSize >= 0);
	}
	virtual ~FMappedFilePakProxy()
	{
		// we don't own lower level, it is shared
	}
	virtual IMappedFileRegion* MapRegion(int64 Offset = 0, int64 BytesToMap = MAX_int64, bool bPreloadHint = false) override
	{
		check(Offset + OffsetInPak < PakSize); // don't map zero bytes and don't map off the end of the (real) file
		check(Offset < GetFileSize()); // don't map zero bytes and don't map off the end of the (virtual) file
		BytesToMap = FMath::Min<int64>(BytesToMap, GetFileSize() - Offset);
		check(BytesToMap > 0); // don't map zero bytes
		check(Offset + BytesToMap <= GetFileSize()); // don't map zero bytes and don't map off the end of the (virtual) file
		check(Offset + OffsetInPak + BytesToMap <= PakSize); // don't map zero bytes and don't map off the end of the (real) file
		return LowerLevel->MapRegion(Offset + OffsetInPak, BytesToMap, bPreloadHint);
	}
};


#if !UE_BUILD_SHIPPING

static void MappedFileTest(const TArray<FString>& Args)
{
	FString TestFile(TEXT("../../../Engine/Config/BaseDeviceProfiles.ini"));
	if (Args.Num() > 0)
	{
		TestFile = Args[0];
	}

	while (true)
	{
		IMappedFileHandle* Handle = FPlatformFileManager::Get().GetPlatformFile().OpenMapped(*TestFile);
		IMappedFileRegion *Region = Handle->MapRegion();

		int64 Size = Region->GetMappedSize();
		const char* Data = (const char *)Region->GetMappedPtr();

		delete Region;
		delete Handle;
	}


}

static FAutoConsoleCommand MappedFileTestCmd(
	TEXT("MappedFileTest"),
	TEXT("Tests the file mappings through the low level."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&MappedFileTest)
);
#endif

static int32 GMMIO_Enable = 1;
static FAutoConsoleVariableRef CVar_MMIOEnable(
	   TEXT("mmio.enable"),
	   GMMIO_Enable,
	   TEXT("If > 0, then enable memory mapped IO on platforms that support it.")
	   );


IMappedFileHandle* FPakPlatformFile::OpenMapped(const TCHAR* Filename)
{
	if (!GMMIO_Enable)
	{
		return nullptr;
	}

	// Check pak files first
	FPakEntry FileEntry;
	FPakFile* PakEntry = nullptr;
	if (FindFileInPakFiles(Filename, &PakEntry, &FileEntry) && PakEntry)
	{
		if (FileEntry.CompressionMethodIndex != 0 || (FileEntry.Flags & FPakEntry::Flag_Encrypted) != 0)
		{
			// can't map compressed or encrypted files
			return nullptr;
		}
		FScopeLock Lock(&PakEntry->MappedFileHandleCriticalSection);
		if (!PakEntry->MappedFileHandle)
		{
			PakEntry->MappedFileHandle = LowerLevel->OpenMapped(*PakEntry->GetFilename());
		}
		if (!PakEntry->MappedFileHandle)
		{
			return nullptr;
		}
		return new FMappedFilePakProxy(PakEntry->MappedFileHandle, FileEntry.Offset + FileEntry.GetSerializedSize(PakEntry->GetInfo().Version), FileEntry.UncompressedSize, PakEntry->TotalSize(), Filename);
	}
	if (IsNonPakFilenameAllowed(Filename))
	{
		return LowerLevel->OpenMapped(Filename);
	}
	return nullptr;
}


/**
 * Class to handle correctly reading from a compressed file within a compressed package
 */
class FPakSimpleEncryption
{
public:
	enum
	{
		Alignment = FAES::AESBlockSize,
	};

	static FORCEINLINE int64 AlignReadRequest(int64 Size)
	{
		return Align(Size, Alignment);
	}

	static FORCEINLINE void DecryptBlock(void* Data, int64 Size, const FGuid& EncryptionKeyGuid)
	{
		INC_DWORD_STAT(STAT_PakCache_SyncDecrypts);
		DecryptData((uint8*)Data, Size, EncryptionKeyGuid);
	}
};

/**
 * Thread local class to manage working buffers for file compression
 */
class FCompressionScratchBuffers : public TThreadSingleton<FCompressionScratchBuffers>
{
public:
	FCompressionScratchBuffers()
		: TempBufferSize(0)
		, ScratchBufferSize(0)
		, LastReader(nullptr)
		, LastDecompressedBlock(0xFFFFFFFF)
	{}

	int64				TempBufferSize;
	TUniquePtr<uint8[]>	TempBuffer;
	int64				ScratchBufferSize;
	TUniquePtr<uint8[]>	ScratchBuffer;

	void* LastReader;
	uint32 LastDecompressedBlock;

	void EnsureBufferSpace(int64 CompressionBlockSize, int64 ScrachSize)
	{
		if (TempBufferSize < CompressionBlockSize)
		{
			TempBufferSize = CompressionBlockSize;
			TempBuffer = MakeUnique<uint8[]>(TempBufferSize);
		}
		if (ScratchBufferSize < ScrachSize)
		{
			ScratchBufferSize = ScrachSize;
			ScratchBuffer = MakeUnique<uint8[]>(ScratchBufferSize);
		}
	}
};

/**
 * Class to handle correctly reading from a compressed file within a pak
 */
template< typename EncryptionPolicy = FPakNoEncryption >
class FPakCompressedReaderPolicy
{
public:
	class FPakUncompressTask : public FNonAbandonableTask
	{
	public:
		uint8*				UncompressedBuffer;
		int32				UncompressedSize;
		uint8*				CompressedBuffer;
		int32				CompressedSize;
		FName				CompressionFormat;
		void*				CopyOut;
		int64				CopyOffset;
		int64				CopyLength;
		FGuid				EncryptionKeyGuid;

		void DoWork()
		{
			// Decrypt and Uncompress from memory to memory.
			int64 EncryptionSize = EncryptionPolicy::AlignReadRequest(CompressedSize);
			EncryptionPolicy::DecryptBlock(CompressedBuffer, EncryptionSize, EncryptionKeyGuid);
			FCompression::UncompressMemory(CompressionFormat, UncompressedBuffer, UncompressedSize, CompressedBuffer, CompressedSize);
			if (CopyOut)
			{
				FMemory::Memcpy(CopyOut, UncompressedBuffer + CopyOffset, CopyLength);
			}
		}

		FORCEINLINE TStatId GetStatId() const
		{
			// TODO: This is called too early in engine startup.
			return TStatId();
			//RETURN_QUICK_DECLARE_CYCLE_STAT(FPakUncompressTask, STATGROUP_ThreadPoolAsyncTasks);
		}
	};

	FPakCompressedReaderPolicy(const FPakFile& InPakFile, const FPakEntry& InPakEntry, TAcquirePakReaderFunction& InAcquirePakReader)
		: PakFile(InPakFile)
		, PakEntry(InPakEntry)
		, AcquirePakReader(InAcquirePakReader)
	{
	}

	~FPakCompressedReaderPolicy()
	{
		FCompressionScratchBuffers& ScratchSpace = FCompressionScratchBuffers::Get();
		if(ScratchSpace.LastReader == this)
		{
			ScratchSpace.LastDecompressedBlock = 0xFFFFFFFF;
			ScratchSpace.LastReader = nullptr;
		}
	}

	/** Pak file that own this file data */
	const FPakFile&		PakFile;
	/** Pak file entry for this file. */
	FPakEntry			PakEntry;
	/** Function that gives us an FArchive to read from. The result should never be cached, but acquired and used within the function doing the serialization operation */
	TAcquirePakReaderFunction AcquirePakReader;

	FORCEINLINE int64 FileSize() const
	{
		return PakEntry.UncompressedSize;
	}

	void Serialize(int64 DesiredPosition, void* V, int64 Length)
	{
		const int32 CompressionBlockSize = PakEntry.CompressionBlockSize;
		uint32 CompressionBlockIndex = DesiredPosition / CompressionBlockSize;
		uint8* WorkingBuffers[2];
		int64 DirectCopyStart = DesiredPosition % PakEntry.CompressionBlockSize;
		FAsyncTask<FPakUncompressTask> UncompressTask;
		FCompressionScratchBuffers& ScratchSpace = FCompressionScratchBuffers::Get();
		bool bStartedUncompress = false;

		FName CompressionMethod = PakFile.GetInfo().GetCompressionMethod(PakEntry.CompressionMethodIndex);
		checkf(FCompression::IsFormatValid(CompressionMethod), 
			TEXT("Attempting to use compression format %s when loading a file from a .pak, but that compression format is not available.\n")
			TEXT("If you are running a program (like UnrealPak) you may need to pass the .uproject on the commandline so the plugin can be found.\n"),
			TEXT("It's also possible that a necessary compression plugin has not been loaded yet, and this file needs to be forced to use zlib compression.\n")
			TEXT("Unfortunately, the code that can check this does not have the context of the filename that is being read. You will need to look in the callstack in a debugger.\n")
			TEXT("See ExtensionsToNotUsePluginCompression in [Pak] section of Engine.ini to add more extensions."),
			*CompressionMethod.ToString(), TEXT("Unknown"));

		// an amount to extra allocate, in case one block's compressed size is bigger than CompressMemoryBound
		float SlopMultiplier = 1.1f;
		int64 WorkingBufferRequiredSize = FCompression::CompressMemoryBound(CompressionMethod, CompressionBlockSize) * SlopMultiplier;
		WorkingBufferRequiredSize = EncryptionPolicy::AlignReadRequest(WorkingBufferRequiredSize);
		const bool bExistingScratchBufferValid = ScratchSpace.TempBufferSize >= CompressionBlockSize;
		ScratchSpace.EnsureBufferSpace(CompressionBlockSize, WorkingBufferRequiredSize * 2);
		WorkingBuffers[0] = ScratchSpace.ScratchBuffer.Get();
		WorkingBuffers[1] = ScratchSpace.ScratchBuffer.Get() + WorkingBufferRequiredSize;

		FArchive* PakReader = AcquirePakReader();

		while (Length > 0)
		{
			const FPakCompressedBlock& Block = PakEntry.CompressionBlocks[CompressionBlockIndex];
			int64 Pos = CompressionBlockIndex * CompressionBlockSize;
			int64 CompressedBlockSize = Block.CompressedEnd - Block.CompressedStart;
			int64 UncompressedBlockSize = FMath::Min<int64>(PakEntry.UncompressedSize - Pos, PakEntry.CompressionBlockSize);

			if (CompressedBlockSize > UncompressedBlockSize)
			{
				UE_LOG(LogPakFile, Display, TEXT("Bigger compressed? Block[%d]: %d -> %d > %d [%d min %d]"), CompressionBlockIndex, Block.CompressedStart, Block.CompressedEnd, UncompressedBlockSize, PakEntry.UncompressedSize - Pos, PakEntry.CompressionBlockSize);
			}


			int64 ReadSize = EncryptionPolicy::AlignReadRequest(CompressedBlockSize);
			int64 WriteSize = FMath::Min<int64>(UncompressedBlockSize - DirectCopyStart, Length);

			const bool bCurrentScratchTempBufferValid = 
				bExistingScratchBufferValid && !bStartedUncompress
				// ensure this object was the last reader from the scratch buffer and the last thing it decompressed was this block.
				&& ScratchSpace.LastReader == this && ScratchSpace.LastDecompressedBlock == CompressionBlockIndex 
				// ensure the previous decompression destination was the scratch buffer.
				&& !(DirectCopyStart == 0 && Length >= CompressionBlockSize); 

			if (bCurrentScratchTempBufferValid)
			{
				// Reuse the existing scratch buffer to avoid repeatedly deserializing and decompressing the same block.
				FMemory::Memcpy(V, ScratchSpace.TempBuffer.Get() + DirectCopyStart, WriteSize);
			}
			else
			{
			PakReader->Seek(Block.CompressedStart + (PakFile.GetInfo().HasRelativeCompressedChunkOffsets() ? PakEntry.Offset : 0));
			PakReader->Serialize(WorkingBuffers[CompressionBlockIndex & 1], ReadSize);
			if (bStartedUncompress)
			{
				UncompressTask.EnsureCompletion();
				bStartedUncompress = false;
			}

			FPakUncompressTask& TaskDetails = UncompressTask.GetTask();
			TaskDetails.EncryptionKeyGuid = PakFile.GetInfo().EncryptionKeyGuid;

			if (DirectCopyStart == 0 && Length >= CompressionBlockSize)
			{
				// Block can be decompressed directly into output buffer
				TaskDetails.CompressionFormat = CompressionMethod;
				TaskDetails.UncompressedBuffer = (uint8*)V;
				TaskDetails.UncompressedSize = UncompressedBlockSize;
				TaskDetails.CompressedBuffer = WorkingBuffers[CompressionBlockIndex & 1];
				TaskDetails.CompressedSize = CompressedBlockSize;
				TaskDetails.CopyOut = nullptr;
					ScratchSpace.LastDecompressedBlock = 0xFFFFFFFF;
					ScratchSpace.LastReader = nullptr;
			}
			else
			{
				// Block needs to be copied from a working buffer
				TaskDetails.CompressionFormat = CompressionMethod;
				TaskDetails.UncompressedBuffer = ScratchSpace.TempBuffer.Get();
				TaskDetails.UncompressedSize = UncompressedBlockSize;
				TaskDetails.CompressedBuffer = WorkingBuffers[CompressionBlockIndex & 1];
				TaskDetails.CompressedSize = CompressedBlockSize;
				TaskDetails.CopyOut = V;
				TaskDetails.CopyOffset = DirectCopyStart;
				TaskDetails.CopyLength = WriteSize;

					ScratchSpace.LastDecompressedBlock = CompressionBlockIndex;
					ScratchSpace.LastReader = this;
			}

			if (Length == WriteSize)
			{
				UncompressTask.StartSynchronousTask();
			}
			else
			{
				UncompressTask.StartBackgroundTask();
			}
			bStartedUncompress = true;
			}
		
			V = (void*)((uint8*)V + WriteSize);
			Length -= WriteSize;
			DirectCopyStart = 0;
			++CompressionBlockIndex;
		}

		if (bStartedUncompress)
		{
			UncompressTask.EnsureCompletion();
		}
	}
};

bool FPakEntry::VerifyPakEntriesMatch(const FPakEntry& FileEntryA, const FPakEntry& FileEntryB)
{
	bool bResult = true;
	if (FileEntryA.Size != FileEntryB.Size)
	{
		UE_LOG(LogPakFile, Error, TEXT("Pak header file size mismatch, got: %lld, expected: %lld"), FileEntryB.Size, FileEntryA.Size);
		bResult = false;
	}
	if (FileEntryA.UncompressedSize != FileEntryB.UncompressedSize)
	{
		UE_LOG(LogPakFile, Error, TEXT("Pak header uncompressed file size mismatch, got: %lld, expected: %lld"), FileEntryB.UncompressedSize, FileEntryA.UncompressedSize);
		bResult = false;
	}
	if (FileEntryA.CompressionMethodIndex != FileEntryB.CompressionMethodIndex)
	{
		UE_LOG(LogPakFile, Error, TEXT("Pak header file compression method mismatch, got: %d, expected: %d"), FileEntryB.CompressionMethodIndex, FileEntryA.CompressionMethodIndex);
		bResult = false;
	}
	if (FMemory::Memcmp(FileEntryA.Hash, FileEntryB.Hash, sizeof(FileEntryA.Hash)) != 0)
	{
		UE_LOG(LogPakFile, Error, TEXT("Pak file hash does not match its index entry"));
		bResult = false;
	}
	return bResult;
}

bool FPakPlatformFile::IsNonPakFilenameAllowed(const FString& InFilename)
{
	bool bAllowed = true;

#if EXCLUDE_NONPAK_UE_EXTENSIONS
	if (PakFiles.Num() || UE_BUILD_SHIPPING)
	{
		FName Ext = FName(*FPaths::GetExtension(InFilename));
		bAllowed = !ExcludedNonPakExtensions.Contains(Ext);
	}
#endif

	bool bIsIniFile = InFilename.EndsWith(IniFileExtension);
#if DISABLE_NONUFS_INI_WHEN_COOKED
	bool bSkipIniFile = bIsIniFile && !InFilename.EndsWith(GameUserSettingsIniFilename);
	if (FPlatformProperties::RequiresCookedData() && bSkipIniFile)
	{
		bAllowed = false;
	}
#endif
#if ALLOW_INI_OVERRIDE_FROM_COMMANDLINE
	FString FileList;
	if (bIsIniFile && FParse::Value(FCommandLine::Get(), TEXT("-iniFile="), FileList, false))
	{
		TArray<FString> Files;
		FileList.ParseIntoArray(Files, TEXT(","), true);
		for (int32 Index = 0; Index < Files.Num(); Index++)
		{
			if (InFilename == Files[Index])
			{
				bAllowed = true;
				UE_LOG(LogPakFile, Log, TEXT(" Override -inifile: %s"), *InFilename);
				break;
			}
		}
	}
#endif

	FFilenameSecurityDelegate& FilenameSecurityDelegate = GetFilenameSecurityDelegate();
	if (bAllowed)
	{
		if (FilenameSecurityDelegate.IsBound())
		{
			bAllowed = FilenameSecurityDelegate.Execute(*InFilename);;
		}
	}

	return bAllowed;
}


#if IS_PROGRAM
FPakFile::FPakFile(const TCHAR* Filename, bool bIsSigned)
	: PakFilename(Filename)
	, PakFilenameName(Filename)
	, FilenameHashesIndex(nullptr)
	, FilenameHashesIndices(nullptr)
	, FilenameHashes(nullptr)
	, MiniPakEntriesOffsets(nullptr)
	, MiniPakEntries(nullptr)
	, NumEntries(0)
	, CachedTotalSize(0)
	, bSigned(bIsSigned)
	, bIsValid(false)
	, bFilenamesRemoved(false)
	, PakchunkIndex(GetPakchunkIndexFromPakFile(Filename))
	, bAttemptedPakEntryShrink(false)
	, bAttemptedPakFilenameUnload(false)
 	, MappedFileHandle(nullptr)
	, CacheType(FPakFile::ECacheType::Shared)
	, CacheIndex(-1)
	, UnderlyingCacheTrimDisabled(false)
{
	FArchive* Reader = GetSharedReader(NULL);
	if (Reader)
	{
		Timestamp = IFileManager::Get().GetTimeStamp(Filename);
		Initialize(Reader);
	}
}
#endif

FPakFile::FPakFile(IPlatformFile* LowerLevel, const TCHAR* Filename, bool bIsSigned, bool bLoadIndex)
	: PakFilename(Filename)
	, PakFilenameName(Filename)
	, FilenameHashesIndex(nullptr)
	, FilenameHashesIndices(nullptr)
	, FilenameHashes(nullptr)
	, MiniPakEntriesOffsets(nullptr)
	, MiniPakEntries(nullptr)
	, NumEntries(0)
	, CachedTotalSize(0)
	, bSigned(bIsSigned)
	, bIsValid(false)
	, bFilenamesRemoved(false)
	, PakchunkIndex(GetPakchunkIndexFromPakFile(Filename))
	, bAttemptedPakEntryShrink(false)
	, bAttemptedPakFilenameUnload(false)
	, MappedFileHandle(nullptr)
	, CacheType(FPakFile::ECacheType::Shared)
	, CacheIndex(-1)
	, UnderlyingCacheTrimDisabled(false)
{
	FArchive* Reader = GetSharedReader(LowerLevel);
	if (Reader)
	{
		Timestamp = LowerLevel->GetTimeStamp(Filename);
		Initialize(Reader, bLoadIndex);
	}
}

#if WITH_EDITOR
FPakFile::FPakFile(FArchive* Archive)
	: FilenameHashesIndex(nullptr)
	, FilenameHashesIndices(nullptr)
	, FilenameHashes(nullptr)
	, MiniPakEntriesOffsets(nullptr)
	, MiniPakEntries(nullptr)
	, NumEntries(0)
	, bSigned(false)
	, bIsValid(false)
	, bFilenamesRemoved(false)
	, PakchunkIndex(INDEX_NONE)
	, MappedFileHandle(nullptr)
	, CacheType(FPakFile::ECacheType::Shared)
	, CacheIndex(-1)
	, UnderlyingCacheTrimDisabled(false)
{
	Initialize(Archive);
}
#endif

FPakFile::~FPakFile()
{
	delete MappedFileHandle;
	delete[] MiniPakEntries;
	delete[] MiniPakEntriesOffsets;
	delete[] FilenameHashes;
	delete[] FilenameHashesIndices;
	delete[] FilenameHashesIndex;
}

bool FPakFile::PassedSignatureChecks() const
{
	return Decryptor.IsValid() && Decryptor->IsValid();
}

FArchive* FPakFile::CreatePakReader(const TCHAR* Filename)
{
	FArchive* ReaderArchive = IFileManager::Get().CreateFileReader(Filename);
	return SetupSignedPakReader(ReaderArchive, Filename);
}

FArchive* FPakFile::CreatePakReader(IFileHandle& InHandle, const TCHAR* Filename)
{
	FArchive* ReaderArchive = new FArchiveFileReaderGeneric(&InHandle, Filename, InHandle.Size());
	return SetupSignedPakReader(ReaderArchive, Filename);
}

FArchive* FPakFile::SetupSignedPakReader(FArchive* ReaderArchive, const TCHAR* Filename)
{
	if (FPlatformProperties::RequiresCookedData())
	{
		bool bShouldCheckSignature = bSigned || FParse::Param(FCommandLine::Get(), TEXT("signedpak")) || FParse::Param(FCommandLine::Get(), TEXT("signed"));
#if !UE_BUILD_SHIPPING
		bShouldCheckSignature &= !FParse::Param(FCommandLine::Get(), TEXT("FileOpenLog"));
#endif
		if (bShouldCheckSignature)
		{
			if (!Decryptor)
			{
				Decryptor = MakeUnique<FChunkCacheWorker>(ReaderArchive, Filename);
			}

			if (Decryptor->IsValid())
			{
				ReaderArchive = new FSignedArchiveReader(ReaderArchive, Decryptor.Get());
			}
			else
			{
				delete ReaderArchive;
				return nullptr;
			}
		}
	}
	return ReaderArchive;
}

void FPakFile::Initialize(FArchive* Reader, bool bLoadIndex)
{
	CachedTotalSize = Reader->TotalSize();
	bool bShouldLoad = false;
	int32 CompatibleVersion = FPakInfo::PakFile_Version_Latest;

	LLM_SCOPE(ELLMTag::FileSystem);

	// Serialize trailer and check if everything is as expected.
	// start up one to offset the -- below
	CompatibleVersion++;
	int64 FileInfoPos = -1;
	do
	{
		// try the next version down
		CompatibleVersion--;

		FileInfoPos = CachedTotalSize - Info.GetSerializedSize(CompatibleVersion);
		if (FileInfoPos >= 0)
		{
			Reader->Seek(FileInfoPos);

			SCOPED_BOOT_TIMING("PakFile_SerilizeTrailer");

			// Serialize trailer and check if everything is as expected.
			Info.Serialize(*Reader, CompatibleVersion);
			if (Info.Magic == FPakInfo::PakFile_Magic)
			{
				bShouldLoad = true;
			}
		}
	} while (!bShouldLoad && CompatibleVersion >= FPakInfo::PakFile_Version_Initial);

	if (bShouldLoad)
	{
		UE_CLOG(Info.Magic != FPakInfo::PakFile_Magic, LogPakFile, Fatal, TEXT("Trailing magic number (%ud) in '%s' is different than the expected one. Verify your installation."), Info.Magic, *PakFilename);
		UE_CLOG(!(Info.Version >= FPakInfo::PakFile_Version_Initial && Info.Version <= CompatibleVersion), LogPakFile, Fatal, TEXT("Invalid pak file version (%d) in '%s'. Verify your installation."), Info.Version, *PakFilename);
		UE_CLOG(!(Info.IndexOffset >= 0 && Info.IndexOffset < CachedTotalSize), LogPakFile, Fatal, TEXT("Index offset for pak file '%s' is invalid (%lld is bigger than file size %lld)"), *PakFilename, Info.IndexOffset, CachedTotalSize);
		UE_CLOG(!((Info.IndexOffset + Info.IndexSize) >= 0 && (Info.IndexOffset + Info.IndexSize) <= CachedTotalSize), LogPakFile, Fatal, TEXT("Index end offset for pak file '%s' is invalid (%lld)"), *PakFilename, Info.IndexOffset + Info.IndexSize);

		// If we aren't using a dynamic encryption key, process the pak file using the embedded key
		if (!Info.EncryptionKeyGuid.IsValid() || GetRegisteredEncryptionKeys().HasKey(Info.EncryptionKeyGuid))
		{
			if (bLoadIndex)
			{
				LoadIndex(Reader);
			}

			if (FParse::Param(FCommandLine::Get(), TEXT("checkpak")))
			{
				ensure(Check());
			}
		}

		// LoadIndex should crash in case of an error, so just assume everything is ok if we got here.
		bIsValid = true;
	}
}

void FPakFile::LoadIndex(FArchive* Reader)
{
	if (CachedTotalSize < (Info.IndexOffset + Info.IndexSize))
	{
		UE_LOG(LogPakFile, Fatal, TEXT("Corrupted index offset in pak file."));
	}
	else
	{
		if (Info.Version >= FPakInfo::PakFile_Version_FrozenIndex && Info.bIndexIsFrozen)
		{
			SCOPED_BOOT_TIMING("PakFile_LoadFrozen");

			// read frozen data
			Reader->Seek(Info.IndexOffset);
			int32 FrozenSize = Info.IndexSize;

			// read in the index, etc data in one lump
			void* DataMemory = FMemory::Malloc(FrozenSize);
			Reader->Serialize(DataMemory, FrozenSize);
			Data = TUniquePtr<FPakFileData>((FPakFileData*)DataMemory);

			// cache the number of entries
			NumEntries = Data->Files.Num();
			// @todo loadtime: it is nice to serialize the mountpoint right into the Data so that IndexSize is right here
			// but it takes this to copy it out, because it's too painful for the string manipulation when dealing with
			// MemoryImageString everywhere MountPoint is used
			MountPoint = Data->MountPoint;
		}
		else
		{
			// Load index into memory first.

			// If we encounter a corrupt index, try again but gather some extra debugging information so we can try and understand where it failed.
			bool bFirstPass = true;
			TArray<uint8> IndexData;

			while (true)
			{
				Reader->Seek(Info.IndexOffset);
				IndexData.Empty(); // The next SetNum makes this Empty() logically redundant, but we want to try and force a memory reallocation for the re-attempt
				IndexData.SetNum(Info.IndexSize);
				{
					SCOPED_BOOT_TIMING("PakFile_LoadIndex");
					Reader->Serialize(IndexData.GetData(), Info.IndexSize);
				}

				FSHAHash EncryptedDataHash;
				if (!bFirstPass)
				{
					FSHA1::HashBuffer(IndexData.GetData(), IndexData.Num(), EncryptedDataHash.Hash);
				}

				// Decrypt if necessary
				if (Info.bEncryptedIndex)
				{
					DecryptData(IndexData.GetData(), Info.IndexSize, Info.EncryptionKeyGuid);
				}
				// Check SHA1 value.
				FSHAHash ComputedHash;
				{
					SCOPED_BOOT_TIMING("PakFile_Hash");
					FSHA1::HashBuffer(IndexData.GetData(), IndexData.Num(), ComputedHash.Hash);
				}
				if (Info.IndexHash != ComputedHash)
				{
					if (bFirstPass)
					{
						UE_LOG(LogPakFile, Log, TEXT("Corrupt pak index detected!"));
						UE_LOG(LogPakFile, Log, TEXT(" Filename: %s"), *PakFilename);
						UE_LOG(LogPakFile, Log, TEXT(" Encrypted: %d"), Info.bEncryptedIndex);
						UE_LOG(LogPakFile, Log, TEXT(" Total Size: %d"), Reader->TotalSize());
						UE_LOG(LogPakFile, Log, TEXT(" Index Offset: %d"), Info.IndexOffset);
						UE_LOG(LogPakFile, Log, TEXT(" Index Size: %d"), Info.IndexSize);
						UE_LOG(LogPakFile, Log, TEXT(" Stored Index Hash: %s"), *Info.IndexHash.ToString());
						UE_LOG(LogPakFile, Log, TEXT(" Computed Index Hash [Pass 0]: %s"), *ComputedHash.ToString());
						bFirstPass = false;
					}
					else
					{
						UE_LOG(LogPakFile, Log, TEXT(" Computed Index Hash [Pass 1]: %s"), *ComputedHash.ToString());
						UE_LOG(LogPakFile, Log, TEXT(" Encrypted Index Hash: %s"), *EncryptedDataHash.ToString());

						// Compute an SHA1 hash of the whole file so we can tell if the file was modified on disc (obviously as long as this isn't an  IO bug that gives us the same bogus data again)
						FSHA1 FileHash;
						Reader->Seek(0);
						int64 Remaining = Reader->TotalSize();
						TArray<uint8> WorkingBuffer;
						WorkingBuffer.SetNum(64 * 1024);
						while (Remaining > 0)
						{
							int64 ToProcess = FMath::Min((int64)WorkingBuffer.Num(), Remaining);
							Reader->Serialize(WorkingBuffer.GetData(), ToProcess);
							FileHash.Update(WorkingBuffer.GetData(), ToProcess);
							Remaining -= ToProcess;
						}

						FileHash.Final();
						FSHAHash FinalFileHash;
						FileHash.GetHash(FinalFileHash.Hash);
						UE_LOG(LogPakFile, Log, TEXT(" File Hash: %s"), *FinalFileHash.ToString());

						UE_LOG(LogPakFile, Fatal, TEXT("Corrupted index in pak file (SHA hash mismatch)."));
					}
				}
				else
				{
					if (!bFirstPass)
					{
						UE_LOG(LogPakFile, Log, TEXT("Pak index corruption appears to have recovered on the second attempt!"));
					}
					break;
				}
			}

			FMemoryReader IndexReader(IndexData);

			// Read the default mount point and all entries.
			NumEntries = 0;
			IndexReader << MountPoint;
			IndexReader << NumEntries;

			MakeDirectoryFromPath(MountPoint);
			// Allocate enough memory to hold all entries (and not reallocate while they're being added to it).

			Data = MakeUnique<FPakFileData>();
			Data->Files.Empty(NumEntries);
			Data->Files.AddZeroed(NumEntries);

			for (int32 EntryIndex = 0; EntryIndex < NumEntries; EntryIndex++)
			{
				// Serialize from memory.
				FPakEntry& Entry = Data->Files[EntryIndex];
				FString Filename;
				IndexReader << Filename;
				{
					SCOPED_BOOT_TIMING("PakFile_SerializeEntry");
					Entry.Serialize(IndexReader, Info.Version);
				}

				// Construct Index of all directories in pak file.
				FString Path = FPaths::GetPath(Filename);
				MakeDirectoryFromPath(Path);
				FPakDirectory* Directory = Data->Index.Find(Path);
				if (Directory != NULL)
				{
					Directory->Add(FPaths::GetCleanFilename(Filename), EntryIndex);
				}
				else
				{
					FPakDirectory& NewDirectory = Data->Index.Add(Path);
					NewDirectory.Add(FPaths::GetCleanFilename(Filename), EntryIndex);

					// add the parent directories up to the mount point
					while (MountPoint != Path)
					{
						Path.LeftInline(Path.Len() - 1);
						int32 Offset = 0;
						if (Path.FindLastChar('/', Offset))
						{
							Path.LeftInline(Offset);
							MakeDirectoryFromPath(Path);
							if (Data->Index.Find(Path) == NULL)
							{
								Data->Index.Add(Path);
							}
						}
						else
						{
							Path = MountPoint;
						}
					}
				}
			}
		}
	}
}

bool FPakFile::Check()
{
	UE_LOG(LogPakFile, Display, TEXT("Checking pak file \"%s\". This may take a while..."), *PakFilename);
	double StartTime = FPlatformTime::Seconds();

	FArchive& PakReader = *GetSharedReader(NULL);
	int32 ErrorCount = 0;
	int32 FileCount = 0;

	bool bSuccess = true;

	// If the pak file is signed, we can do a fast check by just reading a single byte from the start of
	// each signing block. The signed archive reader will bring in that whole signing block and compare
	// against the signature table and fire the handler
	if (bSigned)
	{
		FDelegateHandle DelegateHandle;
		FPakPlatformFile::FPakSigningFailureHandlerData& HandlerData = FPakPlatformFile::GetPakSigningFailureHandlerData();

		{
			FScopeLock Lock(&HandlerData.Lock);
			DelegateHandle = HandlerData.ChunkSignatureCheckFailedDelegate.AddLambda([&bSuccess](const FPakChunkSignatureCheckFailedData&)
			{
				bSuccess = false;
			});
		}

		int64 CurrentPos = 0;
		const int64 Size = PakReader.TotalSize();
		while (CurrentPos < Size)
		{
			PakReader.Seek(CurrentPos);
			uint8 Byte = 0;
			PakReader << Byte;
			CurrentPos += FPakInfo::MaxChunkDataSize;
		}

		if (DelegateHandle.IsValid())
		{
			FScopeLock Lock(&HandlerData.Lock);
			HandlerData.ChunkSignatureCheckFailedDelegate.Remove(DelegateHandle);
		}
	}
	else
	{
		const bool bIncludeDeleted = true;
		for (FPakFile::FFileIterator It(*this, bIncludeDeleted); It; ++It, ++FileCount)
		{
			const FPakEntry& Entry = It.Info();
			if (Entry.IsDeleteRecord())
			{
				UE_LOG(LogPakFile, Verbose, TEXT("\"%s\" Deleted."), *It.Filename());
				continue;
			}

			void* FileContents = FMemory::Malloc(Entry.Size);
			PakReader.Seek(Entry.Offset);
			uint32 SerializedCrcTest = 0;
			FPakEntry EntryInfo;
			EntryInfo.Serialize(PakReader, GetInfo().Version);
			if (EntryInfo != Entry)
			{
				UE_LOG(LogPakFile, Error, TEXT("Serialized hash mismatch for \"%s\"."), *It.Filename());
				ErrorCount++;
			}
			PakReader.Serialize(FileContents, Entry.Size);

			uint8 TestHash[20];
			FSHA1::HashBuffer(FileContents, Entry.Size, TestHash);
			if (FMemory::Memcmp(TestHash, Entry.Hash, sizeof(TestHash)) != 0)
			{
				UE_LOG(LogPakFile, Error, TEXT("Hash mismatch for \"%s\"."), *It.Filename());
				ErrorCount++;
			}
			else
			{
				UE_LOG(LogPakFile, Verbose, TEXT("\"%s\" OK. [%s]"), *It.Filename(), *Info.GetCompressionMethod(Entry.CompressionMethodIndex).ToString());
			}
			FMemory::Free(FileContents);
		}
		if (ErrorCount == 0)
		{
			UE_LOG(LogPakFile, Display, TEXT("Pak file \"%s\" healthy, %d files checked."), *PakFilename, FileCount);
		}
		else
		{
			UE_LOG(LogPakFile, Display, TEXT("Pak file \"%s\" corrupted (%d errors out of %d files checked.)."), *PakFilename, ErrorCount, FileCount);
		}
	}

	double EndTime = FPlatformTime::Seconds();
	double ElapsedTime = EndTime - StartTime;
	UE_LOG(LogPakFile, Display, TEXT("Pak file \"%s\" checked in %.2fs"), *PakFilename, ElapsedTime);

	return ErrorCount == 0;
}

struct FMiniFileEntry
{
	uint64 FilenameHash;
	int32 EntryIndex;
};

static inline int32 CDECL CompareFMiniFileEntry(const void* Left, const void* Right)
{
	const FMiniFileEntry* LeftEntry = (const FMiniFileEntry*)Left;
	const FMiniFileEntry* RightEntry = (const FMiniFileEntry*)Right;
	if (LeftEntry->FilenameHash < RightEntry->FilenameHash)
	{
		return -1;
	}
	if (LeftEntry->FilenameHash > RightEntry->FilenameHash)
	{
		return 1;
	}
	return 0;
}

bool FPakFile::UnloadPakEntryFilenames(TMap<uint64, FPakEntry>& CrossPakCollisionChecker, TArray<FString>* DirectoryRootsToKeep, bool bAllowRetries)
{
	// If the process has already been done, get out of here.
	if (bAttemptedPakFilenameUnload || bFilenamesRemoved)
	{
		return true;
	}

	UE_LOG(LogPakFile, Log, TEXT("Unloading filenames for pak '%s'"), *PakFilename);

	LLM_SCOPE(ELLMTag::FileSystem);

	// Set this flag so if unloading fails, we don't try again
	bAttemptedPakFilenameUnload = true;

	if (Info.bIndexIsFrozen)
	{
		UE_LOG(LogPakFile, Warning, TEXT("FAILED unloading filenames for pak '%s' - its index was frozen and cannot be modified"), *PakFilename);
		return false;
	}

	// Variables for the filename hashing and collision detection.
	int NumRetries = 0;
	const int MAX_RETRIES = bAllowRetries ? 10 : 1;
	bool bHasCollision;
	FilenameStartHash = FCrc::StrCrc32(*GetFilename());
    
	// Allocate the temporary array for hashing filenames. The Memset is to hopefully
	// silence the Visual Studio static analyzer.
	TArray<FMiniFileEntry> MiniFileEntries;
	MiniFileEntries.AddUninitialized(NumEntries);

	do
	{
		// No collisions yet for this pass.
		bHasCollision = false;

		TMap<uint64, FPakEntry> NewCollisionCheckEntries;
		NewCollisionCheckEntries.Reserve(NumEntries);

		// Build the list of hashes from the Index based on the starting hash.
		int32 EntryIndex = 0;
		FString FinalFilename;
		FinalFilename.Reset(1024);
		for (TMemoryImageMap<FMemoryImageString, FPakDirectory>::TConstIterator It(Data->Index); (It && !bHasCollision); ++It)
		{
			for (FPakDirectory::TConstIterator DirectoryIt(It.Value()); DirectoryIt; ++DirectoryIt)
			{
				FinalFilename.Reset();
				FinalFilename.Append(It.Key());
				FinalFilename.PathAppend(*DirectoryIt.Key(), DirectoryIt.Key().Len());
				FinalFilename.ToLowerInline();
				uint64 FilenameHash = FFnv::MemFnv64(*FinalFilename, FinalFilename.Len() * sizeof(TCHAR), FilenameStartHash);
				MiniFileEntries[EntryIndex].FilenameHash = FilenameHash;
				MiniFileEntries[EntryIndex].EntryIndex = DirectoryIt.Value();
				++EntryIndex;

				const FPakEntry* EntryFromPreviousPaks = CrossPakCollisionChecker.Find(FilenameHash);
				const FPakEntry* EntryFromCurrentPak = NewCollisionCheckEntries.Find(FilenameHash);
				const FPakEntry& CurrentEntry = Data->Files[DirectoryIt.Value()];

				if (EntryFromPreviousPaks && (FMemory::Memcmp(EntryFromPreviousPaks->Hash, CurrentEntry.Hash, sizeof(CurrentEntry.Hash))) != 0)
				{
					UE_LOG(LogPakFile, Verbose, TEXT("Detected collision with previous pak while hashing %s"), *FinalFilename);
					bHasCollision = true;
					break;
				}

				if (EntryFromCurrentPak && (FMemory::Memcmp(EntryFromCurrentPak->Hash, CurrentEntry.Hash, sizeof(CurrentEntry.Hash)) != 0))
				{
					UE_LOG(LogPakFile, Verbose, TEXT("Detected collision within pak while hashing %s"), *FinalFilename);
					bHasCollision = true;
					break;
				}

				NewCollisionCheckEntries.Add(FilenameHash, CurrentEntry);
			}
		}

		if (bHasCollision)
		{
			++NumRetries;
			++FilenameStartHash;
			UE_LOG(LogPakFile, Verbose, TEXT("Collisions detected. Retrying with new seed..."));
		}
		else
		{
			CrossPakCollisionChecker.Append(NewCollisionCheckEntries);
		}

		// Sort the list to make hash collision detection easy.
		qsort(MiniFileEntries.GetData(), NumEntries, sizeof(FMiniFileEntry), CompareFMiniFileEntry);
	}
	while (bHasCollision && NumRetries < MAX_RETRIES);

	// Filenames can only be unloaded if we found a collision-free starting hash
	// within the maximum number of retries.
	if (NumRetries >= MAX_RETRIES)
	{
		//		FPlatformMisc::LowLevelOutputDebugString(TEXT("Can't unload pak filenames due to hash collision..."));
		UE_LOG(LogPakFile, Warning, TEXT("FAILED unloading filenames for pak '%s'"), *PakFilename);
		return false;
	}

	// Allocate the storage space.
	FilenameHashesIndices = new int32[NumEntries];
	FilenameHashes = new uint64[NumEntries];
	int32 LastHashMostSignificantBits = -1;

	// FilenameHashesIndex provides small 'arenas' of binary searchable filename hashes.
	// The most significant bits (MSB) of the hash, 8 in this case, are used to index into
	// the FilenameHashesIndex to get the start and end indices within FilenameHashes for the
	// search.
	//
	// An example array looks like this:
	//
	//     0   - 0       << No entries in the 0-1 MSB range.
	//     1   - 0       << Entry index 0 begins the 1-2 MSB range.
	//     2   - 103     << Entry index 103 begins the 2-3 MSB range. The 3 MSB range is 103 also, so there are no entries.
	//     3   - 103
	//     4   - 331
	//     5   - 629
	//     ...
	//     256 - 55331   << A value representing NumEntries
	const int MAX_FILENAME_HASHES_INDEX_SIZE = 257;
	FilenameHashesIndex = new uint32[MAX_FILENAME_HASHES_INDEX_SIZE];

	// Transfer the sorted hashes to FilenameHashes.
	for (int32 EntryIndex = 0; EntryIndex < NumEntries; EntryIndex++)
	{
		// If a new index entry is needed as a result of crossing over into a larger hash group
		// as specified through the 8 most significant bits of the hash, store the entry index.
		uint64 FilenameHash = MiniFileEntries[EntryIndex].FilenameHash;
		int32 HashMostSignificantBits = FilenameHash >> 56;
		if (HashMostSignificantBits != LastHashMostSignificantBits)
		{
			for (int32 BitsIndex = LastHashMostSignificantBits + 1; BitsIndex <= HashMostSignificantBits; ++BitsIndex)
			{
				FilenameHashesIndex[BitsIndex] = EntryIndex;
			}
			LastHashMostSignificantBits = HashMostSignificantBits;
		}

		FilenameHashes[EntryIndex] = FilenameHash;
		FilenameHashesIndices[EntryIndex] = MiniFileEntries[EntryIndex].EntryIndex;
	}

	// Fill out the array to the end.
	for (int32 BitsIndex = LastHashMostSignificantBits + 1; BitsIndex < MAX_FILENAME_HASHES_INDEX_SIZE; ++BitsIndex)
	{
		FilenameHashesIndex[BitsIndex] = NumEntries;
	}

	bFilenamesRemoved = true;

#if defined(FPAKFILE_UNLOADPAKENTRYFILENAMES_CHECK)
	// Build the list of hashes from the Index based on the starting hash.
	for (TMap<FString, FPakDirectory>::TConstIterator It(Index); It; ++It)
	{
		for (FPakDirectory::TConstIterator DirectoryIt(It.Value()); DirectoryIt; ++DirectoryIt)
		{
			int32 EntryIndex = DirectoryIt.Value();

			FString FinalFilename = MountPoint / It.Key() / DirectoryIt.Key();
			FPakEntry OutEntry;
			if (!Find(FinalFilename, &OutEntry))
			{
				FPlatformMisc::LowLevelOutputDebugString(*FinalFilename);
			}

			FPakEntry& InEntry = Files[EntryIndex];
			if (InEntry.Offset != OutEntry.Offset ||
					InEntry.Size != OutEntry.Size ||
					InEntry.UncompressedSize != OutEntry.UncompressedSize ||
					InEntry.CompressionMethod != OutEntry.CompressionMethod ||
					InEntry.bEncrypted != OutEntry.bEncrypted ||
					InEntry.CompressionBlockSize != OutEntry.CompressionBlockSize ||
					InEntry.CompressionBlocks != OutEntry.CompressionBlocks)
			{
				FPlatformMisc::LowLevelOutputDebugString(TEXT("!!!!!!!!!!!!!!!!!!!!!!"));
				FPlatformMisc::LowLevelOutputDebugString(*FinalFilename);
			}
		}
	}
#endif

	// Clear out those portions of the Index allowed by the user.
	if (DirectoryRootsToKeep != nullptr)
	{
		for(auto It = Data->Index.CreateIterator(); It; ++It)
		{
			const FString DirectoryName = MountPoint / It.Key();

			bool bRemoveDirectoryFromIndex = true;
			for(const FString& DirectoryRoot : *DirectoryRootsToKeep)
			{
				if (DirectoryName.MatchesWildcard(DirectoryRoot))
				{
					bRemoveDirectoryFromIndex = false;
					break;
				}
			}

			if (bRemoveDirectoryFromIndex)
			{
				It.RemoveCurrent();
			}
		}

		Data->Index.Shrink();

#if defined(FPAKFILE_UNLOADPAKENTRYFILENAMES_LOGKEPTFILENAMES)
		for (TMap<FString, FPakDirectory>::TConstIterator It(Index); It; ++It)
		{
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("FPakFile::UnloadPakEntryFilenames() %s - Keeping %s"), *PakFilename, *It.Key());
		}
#endif
	}
	else
	{
		Data->Index.Empty(0);
	}

	return true;
}

bool FPakFile::ShrinkPakEntriesMemoryUsage()
{
	// If the process has already been done, get out of here.
	if (bAttemptedPakEntryShrink || MiniPakEntries != nullptr)
	{
		return true;
	}

	LLM_SCOPE(ELLMTag::FileSystem);

	UE_LOG(LogPakFile, Log, TEXT("Shrinking entries for pak '%s'"), *PakFilename);

	// Set this flag so if shrinking fails, we don't try again
	bAttemptedPakEntryShrink = true;

	// if the index was frozen, we can't unload parts of it, so skip on out
	if (Info.bIndexIsFrozen)
	{
		UE_LOG(LogPakFile, Warning, TEXT("FAILED shrinking entries for pak file '%s' - its index was frozen and cannot be modified"), *PakFilename);
		return false;
	}

	// Wander every file entry.
	int TotalSizeOfCompressedEntries = 0;
	bool bIsPossibleToShrink = true;
	int32 EntryIndex = 0;
	for (EntryIndex = 0; EntryIndex < NumEntries; ++EntryIndex)
	{
		FPakEntry& Entry = Data->Files[EntryIndex];

		bool bIsOffset32BitSafe = Entry.Offset <= MAX_uint32;
		bool bIsSize32BitSafe = Entry.Size <= MAX_uint32;
		bool bIsUncompressedSize32BitSafe = Entry.UncompressedSize <= MAX_uint32;
		uint32 CompressedBlockAlignment = Entry.IsEncrypted() ? FAES::AESBlockSize : 1;
		int64 HeaderSize = Entry.GetSerializedSize(Info.Version);

		// This data fits into a bitfield (described below), and the data has
		// to fit within a certain range of bits.
		if (Entry.CompressionMethodIndex >= (1 << 6))
		{
			bIsPossibleToShrink = false;
			break;
		}
		if (Entry.CompressionBlocks.Num() >= (1 << 16))
		{
			bIsPossibleToShrink = false;
			break;
		}
		if (Entry.CompressionMethodIndex != 0)
		{
			if (Entry.CompressionBlockSize != Entry.UncompressedSize && ((Entry.CompressionBlockSize >> 11) > 0x3f))
			{
				bIsPossibleToShrink = false;
				break;
			}
			if (Entry.CompressionBlocks.Num() > 0 && ((Info.HasRelativeCompressedChunkOffsets() ? 0 : Entry.Offset) + HeaderSize != Entry.CompressionBlocks[0].CompressedStart))
			{
				bIsPossibleToShrink = false;
				break;
			}
			if (Entry.CompressionBlocks.Num() == 1)
			{
				uint64 Base = Info.HasRelativeCompressedChunkOffsets() ? 0 : Entry.Offset;
				uint64 AlignedBlockSize = Align(Entry.CompressionBlocks[0].CompressedEnd - Entry.CompressionBlocks[0].CompressedStart, CompressedBlockAlignment);
				if ((Base + HeaderSize + Entry.Size) != (Entry.CompressionBlocks[0].CompressedStart + AlignedBlockSize))
				{
					bIsPossibleToShrink = false;
					break;
				}
			}
			if (Entry.CompressionBlocks.Num() > 1)
			{
				for (int i = 1; i < Entry.CompressionBlocks.Num(); ++i)
				{
					uint64 PrevBlockSize = Entry.CompressionBlocks[i - 1].CompressedEnd - Entry.CompressionBlocks[i - 1].CompressedStart;
					PrevBlockSize = Align(PrevBlockSize, CompressedBlockAlignment);
					if (Entry.CompressionBlocks[i].CompressedStart != (Entry.CompressionBlocks[i - 1].CompressedStart + PrevBlockSize))
					{
						bIsPossibleToShrink = false;
						break;
					}
				}

				if (!bIsPossibleToShrink)
				{
					break;
				}
			}
		}

		TotalSizeOfCompressedEntries += sizeof(uint32)
			+ (bIsOffset32BitSafe ? sizeof(uint32) : sizeof(uint64))
			+ (bIsUncompressedSize32BitSafe ? sizeof(uint32) : sizeof(uint64));
		if (Entry.CompressionMethodIndex != 0)
		{
			TotalSizeOfCompressedEntries +=
				(bIsSize32BitSafe ? sizeof(uint32) : sizeof(uint64));
			if (Entry.CompressionBlocks.Num() > 1 || (Entry.CompressionBlocks.Num() == 1 && Entry.IsEncrypted()))
			{
				TotalSizeOfCompressedEntries += Entry.CompressionBlocks.Num() * sizeof(uint32);
			}
		}
	}

	if (!bIsPossibleToShrink)
	{
		UE_LOG(LogPakFile, Warning, TEXT("FAILED shrinking entries for pak file '%s'"), *PakFilename);
		return false;
	}

	// Allocate the buffer to hold onto all of the bit-encoded compressed FPakEntry structures.
	MiniPakEntries = new uint8[TotalSizeOfCompressedEntries];
	MiniPakEntriesOffsets = new uint32[NumEntries];

	// Walk all of the file entries.
	uint8* CurrentEntryPtr = MiniPakEntries;
	for (EntryIndex = 0; EntryIndex < NumEntries; ++EntryIndex)
	{
		FPakEntry* FullEntry = &Data->Files[EntryIndex];

		MiniPakEntriesOffsets[EntryIndex] = CurrentEntryPtr - MiniPakEntries;

		//deleted records have a magic number in the offset instead (not ideal, but there is no more space in the bit-encoded entry)
		if (FullEntry->IsDeleteRecord())
		{
			MiniPakEntriesOffsets[EntryIndex] = MAX_uint32;
		}

		// Begin building the compressed memory structure.
		//
		// The general data format for a bit-encoded entry is this:
		//
		//     uint32 - Flags
		//                Bit 31 = Offset 32-bit safe?
		//                Bit 30 = Uncompressed size 32-bit safe?
		//                Bit 29 = Size 32-bit safe?
		//                Bits 28-23 = Compression method
		//                Bit 22 = Encrypted
		//                Bits 21-6 = Compression blocks count
		//                Bits 5-0 = Compression block size
		//     uint32/uint64 - Offset (either 32-bit or 64-bit depending on bIsOffset32BitSafe)
		//     uint32/uint64 - Uncompressed Size (either 32-bit or 64-bit depending on bIsUncompressedSize32BitSafe)
		//
		//   If the CompressionMethod != COMPRESS_None:
		//     uint32/uint64 - Size (either 32-bit or 64-bit depending on bIsSize32BitSafe)
		//
		//     If the Compression blocks count is more than 1, then an array of Compression block sizes follows of:
		//         uint32    - Number of bytes in this Compression block.
		//
		bool bIsOffset32BitSafe = FullEntry->Offset <= MAX_uint32;
		bool bIsSize32BitSafe = FullEntry->Size <= MAX_uint32;
		bool bIsUncompressedSize32BitSafe = FullEntry->UncompressedSize <= MAX_uint32;

		// Build the Flags field.
		*(uint32*)CurrentEntryPtr =
			(bIsOffset32BitSafe ? (1 << 31) : 0)
			| (bIsUncompressedSize32BitSafe ? (1 << 30) : 0)
			| (bIsSize32BitSafe ? (1 << 29) : 0)
			| (FullEntry->CompressionMethodIndex << 23)
			| (FullEntry->IsEncrypted() ? (1 << 22) : 0)
			| (FullEntry->CompressionBlocks.Num() << 6)
			| (FullEntry->CompressionBlockSize >> 11)
			;
		CurrentEntryPtr += sizeof(uint32);

		// Build the Offset field.
		if (bIsOffset32BitSafe)
		{
			*(uint32*)CurrentEntryPtr = (uint32)FullEntry->Offset;
			CurrentEntryPtr += sizeof(uint32);
		}
		else
		{
			FMemory::Memcpy(CurrentEntryPtr, &FullEntry->Offset, sizeof(int64));
			CurrentEntryPtr += sizeof(int64);
		}

		// Build the Uncompressed Size field.
		if (bIsUncompressedSize32BitSafe)
		{
			*(uint32*)CurrentEntryPtr = (uint32)FullEntry->UncompressedSize;
			CurrentEntryPtr += sizeof(uint32);
		}
		else
		{
			FMemory::Memcpy(CurrentEntryPtr, &FullEntry->UncompressedSize, sizeof(int64));
			CurrentEntryPtr += sizeof(int64);
		}

		// Any additional data is for compressed file data.
		if (FullEntry->CompressionMethodIndex != 0)
		{
			// Build the Compressed Size field.
			if (bIsSize32BitSafe)
			{
				*(uint32*)CurrentEntryPtr = (uint32)FullEntry->Size;
				CurrentEntryPtr += sizeof(uint32);
			}
			else
			{
				FMemory::Memcpy(CurrentEntryPtr, &FullEntry->Size, sizeof(int64));
				CurrentEntryPtr += sizeof(int64);
			}

			// Build the Compression Blocks array.
			if (FullEntry->CompressionBlocks.Num() > 1 || (FullEntry->CompressionBlocks.Num() == 1 && FullEntry->IsEncrypted()))
			{
				for (int CompressionBlockIndex = 0; CompressionBlockIndex < FullEntry->CompressionBlocks.Num(); ++CompressionBlockIndex)
				{
					*(uint32*)CurrentEntryPtr = FullEntry->CompressionBlocks[CompressionBlockIndex].CompressedEnd - FullEntry->CompressionBlocks[CompressionBlockIndex].CompressedStart;
					CurrentEntryPtr += sizeof(uint32);
				}
			}
		}

#if !UE_BUILD_SHIPPING
		if (!FullEntry->IsDeleteRecord())
		{
			FPakEntry Test;
			DecodePakEntry(MiniPakEntries + MiniPakEntriesOffsets[EntryIndex], &Test);
			FMemory::Memcpy(Test.Hash, FullEntry->Hash, 20);
			check(Test == *FullEntry);
		}
#endif
	}

	check(CurrentEntryPtr == MiniPakEntries + TotalSizeOfCompressedEntries);

	// Clear out the Files data. We compressed it, and we don't need the wasted
	// space of the original anymore.
	Data->Files.Empty(0);

	static uint64 Total = 0;
	Total += TotalSizeOfCompressedEntries;
	UE_LOG(LogPakFile, Display, TEXT("Compressed pak entries down to %d bytes [Total = %llu bytes]"), TotalSizeOfCompressedEntries, Total);

	return true;
}

#if DO_CHECK
/**
* FThreadCheckingArchiveProxy - checks that inner archive is only used from the specified thread ID
*/
class FThreadCheckingArchiveProxy : public FArchiveProxy
{
public:

	const uint32 ThreadId;
	FArchive* InnerArchivePtr;

	FThreadCheckingArchiveProxy(FArchive* InReader, uint32 InThreadId)
		: FArchiveProxy(*InReader)
		, ThreadId(InThreadId)
		, InnerArchivePtr(InReader)
	{}

	virtual ~FThreadCheckingArchiveProxy()
	{
		if (InnerArchivePtr)
		{
			delete InnerArchivePtr;
		}
	}

	//~ Begin FArchiveProxy Interface
	virtual void Serialize(void* Data, int64 Length) override
	{
		if (FPlatformTLS::GetCurrentThreadId() != ThreadId)
		{
			UE_LOG(LogPakFile, Error, TEXT("Attempted serialize using thread-specific pak file reader on the wrong thread.  Reader for thread %d used by thread %d."), ThreadId, FPlatformTLS::GetCurrentThreadId());
		}
		InnerArchive.Serialize(Data, Length);
	}

	virtual void Seek(int64 InPos) override
	{
		if (FPlatformTLS::GetCurrentThreadId() != ThreadId)
		{
			UE_LOG(LogPakFile, Error, TEXT("Attempted seek using thread-specific pak file reader on the wrong thread.  Reader for thread %d used by thread %d."), ThreadId, FPlatformTLS::GetCurrentThreadId());
		}
		InnerArchive.Seek(InPos);
	}
	//~ End FArchiveProxy Interface
};
#endif //DO_CHECK


void FPakFile::GetFilenames(TArray<FString>& OutFileList) const
{
	for (const TMemoryImageMap<FMemoryImageString, FPakDirectory>::ElementType& DirectoryElement : Data->Index)
	{
		const  FPakDirectory& Directory = DirectoryElement.Value;
		for (const FPakDirectory::ElementType& FileElement : Directory)
		{
			OutFileList.Add(MountPoint / DirectoryElement.Key / FileElement.Key);
		}
	}
}


void FPakFile::GetFilenamesInChunk(const TArray<int32>& InChunkIDs, TArray<FString>& OutFileList)
{
	TSet<int32> OverlappingEntries;

	for (int32 LocalChunkID : InChunkIDs)
	{
		int32 ChunkStart = LocalChunkID * FPakInfo::MaxChunkDataSize;
		int32 ChunkEnd = ChunkStart + FPakInfo::MaxChunkDataSize;
		int32 FileIndex = 0;

		for (const FPakEntry& File : Data->Files)
		{
			int32 FileStart = File.Offset;
			int32 FileEnd = File.Offset + File.Size;

			// If this file is past the end of the target chunk, we're done
			if (FileStart > ChunkEnd)
			{
				break;
			}


			if (FileEnd > ChunkStart)
			{
				OverlappingEntries.Add(FileIndex);
			}

			FileIndex++;
		}
	}

	int32 Remaining = OverlappingEntries.Num();
	for (const TMemoryImageMap<FMemoryImageString, FPakDirectory>::ElementType& DirectoryElement : Data->Index)
	{
		const  FPakDirectory& Directory = DirectoryElement.Value;
		for (const FPakDirectory::ElementType& FileElement : Directory)
		{
			if (OverlappingEntries.Contains(FileElement.Value))
			{
				OutFileList.Add(FString(DirectoryElement.Key) / FString(FileElement.Key));
				if (--Remaining == 0)
				{
					break;
				}
			}
		}
	}
}

FArchive* FPakFile::GetSharedReader(IPlatformFile* LowerLevel)
{
	uint32 Thread = FPlatformTLS::GetCurrentThreadId();
	FArchive* PakReader = NULL;
	{
		FScopeLock ScopedLock(&CriticalSection);
		TUniquePtr<FArchive>* ExistingReader = ReaderMap.Find(Thread);
		if (ExistingReader)
		{
			PakReader = ExistingReader->Get();
		}

		if (!PakReader)
		{
			// Create a new FArchive reader and pass it to the new handle.
			if (LowerLevel != NULL)
			{
				IFileHandle* PakHandle = LowerLevel->OpenRead(*GetFilename());
				if (PakHandle)
				{
					PakReader = CreatePakReader(*PakHandle, *GetFilename());
				}
			}
			else
			{
				PakReader = CreatePakReader(*GetFilename());
			}
			if (!PakReader)
			{
				UE_LOG(LogPakFile, Warning, TEXT("Unable to create pak \"%s\" handle"), *GetFilename());
				return nullptr;
			}

#if DO_CHECK
			FArchive* Proxy = new FThreadCheckingArchiveProxy(PakReader, Thread);
			ReaderMap.Emplace(Thread, Proxy);
			PakReader = Proxy;
#else //DO_CHECK
			ReaderMap.Emplace(Thread, PakReader);
#endif //DO_CHECK
		}
	}
	return PakReader;
}

FPakFile::EFindResult FPakFile::Find(const FString& Filename, FPakEntry* OutEntry) const
{
	QUICK_SCOPE_CYCLE_COUNTER(PakFileFind);
	if (Filename.StartsWith(MountPoint))
	{
		FString Path(FPaths::GetPath(Filename));

		// Handle the case where the user called FPakFile::UnloadFilenames() and the filenames
		// were removed from memory.
		if (bFilenamesRemoved)
		{
			// Derived from the following:
			//     FString RelativeFilename(Filename.Mid(Path.Len() + 1));
			//     Path = Path.Mid(MountPoint.Len()) / RelativeFilename;
			// Hash the Path.
			int AdjustedMountPointLen = Path.Len() < MountPoint.Len() ? Path.Len() : MountPoint.Len();
			FString LowercaseFilename = Filename.ToLower();
			const TCHAR* SplitStartPtr = *LowercaseFilename + AdjustedMountPointLen;
			uint32 SplitLen = LowercaseFilename.Len() - AdjustedMountPointLen;
			if (*SplitStartPtr == '/')
			{
				++SplitStartPtr;
				--SplitLen;
			}
			uint64 PathHash = FFnv::MemFnv64(SplitStartPtr, SplitLen * sizeof(TCHAR), FilenameStartHash);

			// Look it up in our sorted-by-filename-hash array.
			uint32 PathHashMostSignificantBits = PathHash >> 56;
			uint32 HashEntriesCount = FilenameHashesIndex[PathHashMostSignificantBits + 1] - FilenameHashesIndex[PathHashMostSignificantBits];
			uint64* FoundHash = (uint64*)bsearch(&PathHash, FilenameHashes + FilenameHashesIndex[PathHashMostSignificantBits], HashEntriesCount, sizeof(uint64), CompareFilenameHashes);
			if (FoundHash != NULL)
			{
				bool bDeleted = false;

				int32 FoundEntryIndex = FilenameHashesIndices[FoundHash - FilenameHashes];

				if (MiniPakEntries != NULL)
				{
					uint32 MemoryOffset = MiniPakEntriesOffsets[FoundEntryIndex];

					bDeleted = (MemoryOffset == MAX_uint32); // deleted records have a magic number in the offset instead (not ideal, but there is no more space in the bit-encoded entry)

					if (OutEntry != NULL)
					{
						if (!bDeleted)
						{
							// The FPakEntry structures are bit-encoded, so decode it.
							DecodePakEntry(MiniPakEntries + MemoryOffset, OutEntry);
						}
						else
						{
							// entry was deleted and original data is inaccessible- build dummy entry
							(*OutEntry) = FPakEntry();
							OutEntry->SetDeleteRecord(true);
							OutEntry->Verified = true;		// Set Verified to true to avoid have a synchronous open fail comparing FPakEntry structures.
						}
					}
				}
				else
				{
					const FPakEntry* FoundEntry = &Data->Files[FoundEntryIndex];

					bDeleted = FoundEntry->IsDeleteRecord();

					if (OutEntry != NULL)
					{
						OutEntry->Offset = FoundEntry->Offset;
						OutEntry->Size = FoundEntry->Size;
						OutEntry->UncompressedSize = FoundEntry->UncompressedSize;
						OutEntry->CompressionMethodIndex = FoundEntry->CompressionMethodIndex;
						// NEEDED? FMemory::Memcpy(OutEntry->Hash, FoundEntry->Hash, sizeof(OutEntry->Hash));
						OutEntry->CompressionBlocks = FoundEntry->CompressionBlocks;
						OutEntry->CompressionBlockSize = FoundEntry->CompressionBlockSize;
						OutEntry->Flags = FoundEntry->Flags;
						OutEntry->Verified = true;		// Set Verified to true to avoid have a synchronous open fail comparing FPakEntry structures.
					}
				}

				return bDeleted ? EFindResult::FoundDeleted : EFindResult::Found;
			}
		}
		else
		{
			const FPakDirectory* PakDirectory = FindDirectory(*Path);
			if (PakDirectory != NULL)
			{
				FMemoryImageString RelativeFilename(Filename.Mid(Path.Len() + 1));
				int32 const* FoundEntryIndex = PakDirectory->Find(RelativeFilename);
				if (FoundEntryIndex != NULL)
				{
					bool bDeleted = false;

					if (MiniPakEntries != NULL)
					{
						// The FPakEntry structures are bit-encoded, so decode it.
						uint32 MemoryOffset = MiniPakEntriesOffsets[*FoundEntryIndex];

						bDeleted = (MemoryOffset == MAX_uint32); // deleted records have a magic number in the offset instead (not ideal, but there is no more space in the bit-encoded entry)

						if (OutEntry != NULL)
						{
							if (!bDeleted)
							{
								// The FPakEntry structures are bit-encoded, so decode it.
								uint8* FoundPtr = MiniPakEntries + MemoryOffset;
								DecodePakEntry(FoundPtr, OutEntry);
							}
							else
							{
								// entry was deleted and original data is inaccessible- build dummy entry
								(*OutEntry) = FPakEntry();
								OutEntry->SetDeleteRecord(true);
								OutEntry->Verified = true;		// Set Verified to true to avoid have a synchronous open fail comparing FPakEntry structures.
							}
						}
					}
					else
					{
						const FPakEntry* FoundEntry = &Data->Files[*FoundEntryIndex];
						bDeleted = FoundEntry->IsDeleteRecord();

						if (OutEntry != NULL)
						{
							//*OutEntry = **FoundEntry;
							OutEntry->Offset = FoundEntry->Offset;
							OutEntry->Size = FoundEntry->Size;
							OutEntry->UncompressedSize = FoundEntry->UncompressedSize;
							OutEntry->CompressionMethodIndex = FoundEntry->CompressionMethodIndex;
							FMemory::Memcpy(OutEntry->Hash, FoundEntry->Hash, sizeof(OutEntry->Hash));
							OutEntry->CompressionBlocks = FoundEntry->CompressionBlocks;
							OutEntry->CompressionBlockSize = FoundEntry->CompressionBlockSize;
							OutEntry->Flags = FoundEntry->Flags;
							OutEntry->Verified = true;		// Set Verified to true to avoid have a synchronous open fail comparing FPakEntry structures.
						}
					}

					return bDeleted ? EFindResult::FoundDeleted : EFindResult::Found;
				}
			}
		}
	}
	return EFindResult::NotFound;
}


#if !UE_BUILD_SHIPPING
class FPakExec : private FSelfRegisteringExec
{
	FPakPlatformFile& PlatformFile;

public:

	FPakExec(FPakPlatformFile& InPlatformFile)
		: PlatformFile(InPlatformFile)
	{}

	/** Console commands **/
	virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override
	{
		if (FParse::Command(&Cmd, TEXT("Mount")))
		{
			PlatformFile.HandleMountCommand(Cmd, Ar);
			return true;
		}
		if (FParse::Command(&Cmd, TEXT("Unmount")))
		{
			PlatformFile.HandleUnmountCommand(Cmd, Ar);
			return true;
		}
		else if (FParse::Command(&Cmd, TEXT("PakList")))
		{
			PlatformFile.HandlePakListCommand(Cmd, Ar);
			return true;
		}
		else if (FParse::Command(&Cmd, TEXT("PakCorrupt")))
		{
			PlatformFile.HandlePakCorruptCommand(Cmd, Ar);
			return true;
		}
		return false;
	}
};
static TUniquePtr<FPakExec> GPakExec;

void FPakPlatformFile::HandleMountCommand(const TCHAR* Cmd, FOutputDevice& Ar)
{
	const FString PakFilename = FParse::Token(Cmd, false);
	if (!PakFilename.IsEmpty())
	{
		const FString MountPoint = FParse::Token(Cmd, false);
		Mount(*PakFilename, 0, MountPoint.IsEmpty() ? NULL : *MountPoint);
	}
}

void FPakPlatformFile::HandleUnmountCommand(const TCHAR* Cmd, FOutputDevice& Ar)
{
	const FString PakFilename = FParse::Token(Cmd, false);
	if (!PakFilename.IsEmpty())
	{
		Unmount(*PakFilename);
	}
}

void FPakPlatformFile::HandlePakListCommand(const TCHAR* Cmd, FOutputDevice& Ar)
{
	TArray<FPakListEntry> Paks;
	GetMountedPaks(Paks);
	for (auto Pak : Paks)
	{
		Ar.Logf(TEXT("%s Mounted to %s"), *Pak.PakFile->GetFilename(), *Pak.PakFile->GetMountPoint());
	}
}

void FPakPlatformFile::HandlePakCorruptCommand(const TCHAR* Cmd, FOutputDevice& Ar)
{
#if USE_PAK_PRECACHE
	FPakPrecacher::Get().SimulatePakFileCorruption();
#endif
}
#endif // !UE_BUILD_SHIPPING

FPakPlatformFile::FPakPlatformFile()
	: LowerLevel(NULL)
	, bSigned(false)
{
	FCoreDelegates::GetRegisterEncryptionKeyDelegate().BindRaw(this, &FPakPlatformFile::RegisterEncryptionKey);
}

FPakPlatformFile::~FPakPlatformFile()
{
	FCoreDelegates::GetRegisterEncryptionKeyDelegate().Unbind();

	FCoreDelegates::OnFEngineLoopInitComplete.RemoveAll(this);

	FCoreDelegates::OnMountAllPakFiles.Unbind();
	FCoreDelegates::OnMountPak.Unbind();
	FCoreDelegates::OnUnmountPak.Unbind();
	FCoreDelegates::OnOptimizeMemoryUsageForMountedPaks.Unbind();

#if USE_PAK_PRECACHE
	FPakPrecacher::Shutdown();
#endif
	{
		FScopeLock ScopedLock(&PakListCritical);
		for (int32 PakFileIndex = 0; PakFileIndex < PakFiles.Num(); PakFileIndex++)
		{
			delete PakFiles[PakFileIndex].PakFile;
			PakFiles[PakFileIndex].PakFile = nullptr;
		}
	}
}

void FPakPlatformFile::FindPakFilesInDirectory(IPlatformFile* LowLevelFile, const TCHAR* Directory, const FString& WildCard, TArray<FString>& OutPakFiles)
{
	// Helper class to find all pak files.
	class FPakSearchVisitor : public IPlatformFile::FDirectoryVisitor
	{
		TArray<FString>& FoundPakFiles;
		IPlatformChunkInstall* ChunkInstall = nullptr;
		FString WildCard;
		bool bSkipOptionalPakFiles;

	public:
		FPakSearchVisitor(TArray<FString>& InFoundPakFiles, const FString& InWildCard, IPlatformChunkInstall* InChunkInstall, bool bInSkipOptionalPakFiles)
			: FoundPakFiles(InFoundPakFiles)
			, ChunkInstall(InChunkInstall)
			, WildCard(InWildCard)
			, bSkipOptionalPakFiles(bInSkipOptionalPakFiles)
		{}
		virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory)
		{
			if (bIsDirectory == false)
			{
				FString Filename(FilenameOrDirectory);
				if(Filename.MatchesWildcard(WildCard))
				{
					// if a platform supports chunk style installs, make sure that the chunk a pak file resides in is actually fully installed before accepting pak files from it
					if (ChunkInstall)
					{
						int32 PakchunkIndex = GetPakchunkIndexFromPakFile(Filename);
						if (PakchunkIndex != INDEX_NONE)
						{
							if (ChunkInstall->GetPakchunkLocation(PakchunkIndex) == EChunkLocation::NotAvailable)
							{
								return true;
							}
						}
					}

#if !UE_BUILD_SHIPPING
					if (bSkipOptionalPakFiles == false || Filename.Find("optional") == INDEX_NONE)
#endif
					{
						FoundPakFiles.Add(Filename);
					}
				}
			}
			return true;
		}
	};

	bool bSkipOptionalPakFiles = FParse::Param(FCommandLine::Get(), TEXT("SkipOptionalPakFiles"));

	// Find all pak files.
	FPakSearchVisitor Visitor(OutPakFiles, WildCard, FPlatformMisc::GetPlatformChunkInstall(), bSkipOptionalPakFiles);
	LowLevelFile->IterateDirectoryRecursively(Directory, Visitor);
}

void FPakPlatformFile::FindAllPakFiles(IPlatformFile* LowLevelFile, const TArray<FString>& PakFolders, const FString& WildCard, TArray<FString>& OutPakFiles)
{
	// Find pak files from the specified directories.	
	for (int32 FolderIndex = 0; FolderIndex < PakFolders.Num(); ++FolderIndex)
	{
		FindPakFilesInDirectory(LowLevelFile, *PakFolders[FolderIndex], WildCard, OutPakFiles);
	}

	// alert anyone listening
	if (OutPakFiles.Num() == 0)
	{
		FCoreDelegates::NoPakFilesMountedDelegate.Broadcast();
	}
}

void FPakPlatformFile::GetPakFolders(const TCHAR* CmdLine, TArray<FString>& OutPakFolders)
{
#if !UE_BUILD_SHIPPING
	// Command line folders
	FString PakDirs;
	if (FParse::Value(CmdLine, TEXT("-pakdir="), PakDirs))
	{
		TArray<FString> CmdLineFolders;
		PakDirs.ParseIntoArray(CmdLineFolders, TEXT("*"), true);
		OutPakFolders.Append(CmdLineFolders);
	}
#endif

	// @todo plugin urgent: Needs to handle plugin Pak directories, too
	// Hardcoded locations
	OutPakFolders.Add(FString::Printf(TEXT("%sPaks/"), *FPaths::ProjectContentDir()));
	OutPakFolders.Add(FString::Printf(TEXT("%sPaks/"), *FPaths::ProjectSavedDir()));
	OutPakFolders.Add(FString::Printf(TEXT("%sPaks/"), *FPaths::EngineContentDir()));
}

bool FPakPlatformFile::CheckIfPakFilesExist(IPlatformFile* LowLevelFile, const TArray<FString>& PakFolders)
{
	TArray<FString> FoundPakFiles;
	FindAllPakFiles(LowLevelFile, PakFolders, TEXT(ALL_PAKS_WILDCARD), FoundPakFiles);
	return FoundPakFiles.Num() > 0;
}

bool FPakPlatformFile::ShouldBeUsed(IPlatformFile* Inner, const TCHAR* CmdLine) const
{
	bool Result = false;
#if (!WITH_EDITOR || IS_MONOLITHIC)
	if (!FParse::Param(CmdLine, TEXT("NoPak")))
	{
		TArray<FString> PakFolders;
		GetPakFolders(CmdLine, PakFolders);
		Result = CheckIfPakFilesExist(Inner, PakFolders);
	}
#endif
	return Result;
}

bool FPakPlatformFile::Initialize(IPlatformFile* Inner, const TCHAR* CmdLine)
{
	LLM_SCOPE(ELLMTag::FileSystem);
	SCOPED_BOOT_TIMING("FPakPlatformFile::Initialize");
	// Inner is required.
	check(Inner != NULL);
	LowerLevel = Inner;

#if EXCLUDE_NONPAK_UE_EXTENSIONS && !WITH_EDITOR
	// Extensions for file types that should only ever be in a pak file. Used to stop unnecessary access to the lower level platform file
	ExcludedNonPakExtensions.Add(TEXT("uasset"));
	ExcludedNonPakExtensions.Add(TEXT("umap"));
	ExcludedNonPakExtensions.Add(TEXT("ubulk"));
	ExcludedNonPakExtensions.Add(TEXT("uexp"));
#endif

#if DISABLE_NONUFS_INI_WHEN_COOKED
	IniFileExtension = TEXT(".ini");
	GameUserSettingsIniFilename = TEXT("GameUserSettings.ini");
#endif

	// Signed if we have keys, and are not running with fileopenlog (currently results in a deadlock).
	bSigned = FCoreDelegates::GetPakSigningKeysDelegate().IsBound() && !FParse::Param(FCommandLine::Get(), TEXT("fileopenlog"));

	FString StartupPaksWildcard = GMountStartupPaksWildCard;
#if !UE_BUILD_SHIPPING
	FParse::Value(FCommandLine::Get(), TEXT("StartupPaksWildcard="), StartupPaksWildcard);
#endif

	// Find and mount pak files from the specified directories.
	TArray<FString> PakFolders;
	GetPakFolders(FCommandLine::Get(), PakFolders);
	MountAllPakFiles(PakFolders, *StartupPaksWildcard);

#if !UE_BUILD_SHIPPING
	GPakExec = MakeUnique<FPakExec>(*this);
#endif // !UE_BUILD_SHIPPING

	FCoreDelegates::OnMountAllPakFiles.BindRaw(this, &FPakPlatformFile::MountAllPakFiles);
	FCoreDelegates::OnMountPak.BindRaw(this, &FPakPlatformFile::HandleMountPakDelegate);
	FCoreDelegates::OnUnmountPak.BindRaw(this, &FPakPlatformFile::HandleUnmountPakDelegate);
	FCoreDelegates::OnOptimizeMemoryUsageForMountedPaks.BindRaw(this, &FPakPlatformFile::OptimizeMemoryUsageForMountedPaks);

	FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FPakPlatformFile::OptimizeMemoryUsageForMountedPaks);

	return !!LowerLevel;
}

void FPakPlatformFile::InitializeNewAsyncIO()
{
#if USE_PAK_PRECACHE
#if !WITH_EDITOR
	if (FPlatformProcess::SupportsMultithreading() && !FParse::Param(FCommandLine::Get(), TEXT("FileOpenLog")))
	{
		FPakPrecacher::Init(LowerLevel, FCoreDelegates::GetPakSigningKeysDelegate().IsBound());
	}
	else
#endif
	{
		UE_CLOG(FParse::Param(FCommandLine::Get(), TEXT("FileOpenLog")), LogPakFile, Display, TEXT("Disabled pak precacher to get an accurate load order. This should only be used to collect gameopenorder.log, as it is quite slow."));
		GPakCache_Enable = 0;
	}
#endif
}

void FPakPlatformFile::OptimizeMemoryUsageForMountedPaks()
{
#if !(IS_PROGRAM || WITH_EDITOR)
	FSlowHeartBeatScope SuspendHeartBeat;
	bool bUnloadPakEntryFilenamesIfPossible = FParse::Param(FCommandLine::Get(), TEXT("unloadpakentryfilenames"));
	GConfig->GetBool(TEXT("Pak"), TEXT("UnloadPakEntryFilenamesIfPossible"), bUnloadPakEntryFilenamesIfPossible, GEngineIni);

	if ((bUnloadPakEntryFilenamesIfPossible && !FParse::Param(FCommandLine::Get(), TEXT("nounloadpakentries"))) || FParse::Param(FCommandLine::Get(), TEXT("unloadpakentries")))
	{
		// With [Pak] UnloadPakEntryFilenamesIfPossible enabled, [Pak] DirectoryRootsToKeepInMemoryWhenUnloadingPakEntryFilenames
		// can contain pak entry directory wildcards of which the entire recursive directory structure of filenames underneath a
		// matching wildcard will be kept.
		//
		// Example:
		//   [Pak]
		//   DirectoryRootsToKeepInMemoryWhenUnloadingPakEntryFilenames="*/Config/Tags/"
		//   +DirectoryRootsToKeepInMemoryWhenUnloadingPakEntryFilenames="*/Content/Localization/*"
		TArray<FString> DirectoryRootsToKeep;
		GConfig->GetArray(TEXT("Pak"), TEXT("DirectoryRootsToKeepInMemoryWhenUnloadingPakEntryFilenames"), DirectoryRootsToKeep, GEngineIni);

		FPakPlatformFile* PakPlatformFile = (FPakPlatformFile*)(FPlatformFileManager::Get().FindPlatformFile(FPakPlatformFile::GetTypeName()));
		PakPlatformFile->UnloadPakEntryFilenames(&DirectoryRootsToKeep);
	}

	bool bShrinkPakEntriesMemoryUsage = FParse::Param(FCommandLine::Get(), TEXT("shrinkpakentries"));
	GConfig->GetBool(TEXT("Pak"), TEXT("ShrinkPakEntriesMemoryUsage"), bShrinkPakEntriesMemoryUsage, GEngineIni);
	if (bShrinkPakEntriesMemoryUsage)
	{
		FPakPlatformFile* PakPlatformFile = (FPakPlatformFile*)(FPlatformFileManager::Get().FindPlatformFile(FPakPlatformFile::GetTypeName()));
		PakPlatformFile->ShrinkPakEntriesMemoryUsage();
	}
#endif
}


bool FPakPlatformFile::Mount(const TCHAR* InPakFilename, uint32 PakOrder, const TCHAR* InPath /*= NULL*/, bool bLoadIndex /*= true*/)
{
	bool bSuccess = false;
	TSharedPtr<IFileHandle> PakHandle = MakeShareable(LowerLevel->OpenRead(InPakFilename));
	if (PakHandle.IsValid())
	{
		FPakFile* Pak = new FPakFile(LowerLevel, InPakFilename, bSigned, bLoadIndex);
		if (Pak->IsValid())
		{
			if (!Pak->GetInfo().EncryptionKeyGuid.IsValid() || GetRegisteredEncryptionKeys().HasKey(Pak->GetInfo().EncryptionKeyGuid))
			{
				if (InPath != NULL)
				{
					Pak->SetMountPoint(InPath);
				}
				FString PakFilename = InPakFilename;
				if (PakFilename.EndsWith(TEXT("_P.pak")))
				{
					// Prioritize based on the chunk version number
					// Default to version 1 for single patch system
					uint32 ChunkVersionNumber = 1;
					FString StrippedPakFilename = PakFilename.LeftChop(6);
					int32 VersionEndIndex = PakFilename.Find("_", ESearchCase::CaseSensitive, ESearchDir::FromEnd);
					if (VersionEndIndex != INDEX_NONE && VersionEndIndex > 0)
					{
						int32 VersionStartIndex = PakFilename.Find("_", ESearchCase::CaseSensitive, ESearchDir::FromEnd, VersionEndIndex - 1);
						if (VersionStartIndex != INDEX_NONE)
						{
							VersionStartIndex++;
							FString VersionString = PakFilename.Mid(VersionStartIndex, VersionEndIndex - VersionStartIndex);
							if (VersionString.IsNumeric())
							{
								int32 ChunkVersionSigned = FCString::Atoi(*VersionString);
								if (ChunkVersionSigned >= 1)
								{
									// Increment by one so that the first patch file still gets more priority than the base pak file
									ChunkVersionNumber = (uint32)ChunkVersionSigned + 1;
								}
							}
						}
					}
					PakOrder += 100 * ChunkVersionNumber;
				}
				{
					// Add new pak file
					FScopeLock ScopedLock(&PakListCritical);
					FPakListEntry Entry;
					Entry.ReadOrder = PakOrder;
					Entry.PakFile = Pak;
					PakFiles.Add(Entry);
					PakFiles.StableSort();
				}
				bSuccess = true;
			}
			else
			{
				UE_LOG(LogPakFile, Display, TEXT("Deferring mount of pak \"%s\" until encryption key '%s' becomes available"), InPakFilename, *Pak->GetInfo().EncryptionKeyGuid.ToString());

				check(!GetRegisteredEncryptionKeys().HasKey(Pak->GetInfo().EncryptionKeyGuid));
				FPakListDeferredEntry& Entry = PendingEncryptedPakFiles[PendingEncryptedPakFiles.Add(FPakListDeferredEntry())];
				Entry.Filename = InPakFilename;
				Entry.Path = InPath;
				Entry.ReadOrder = PakOrder;
				Entry.EncryptionKeyGuid = Pak->GetInfo().EncryptionKeyGuid;
				Entry.PakchunkIndex = Pak->PakchunkIndex;

				delete Pak;
				PakHandle.Reset();
				return false;
			}
		}
		else
		{
			UE_LOG(LogPakFile, Warning, TEXT("Failed to mount pak \"%s\", pak is invalid."), InPakFilename);
		}

		if (bSuccess)
		{
			if (FIoDispatcher::IsInitialized())
			{
				FIoStoreEnvironment IoStoreEnvironment;
				IoStoreEnvironment.InitializeFileEnvironment(FPaths::ChangeExtension(InPakFilename, FString()));
				FIoStatus IoStatus = FIoDispatcher::Get().Mount(IoStoreEnvironment);
				if (IoStatus.IsOk())
				{
					UE_LOG(LogPakFile, Display, TEXT("Mounted IoStore environment \"%s\""), *IoStoreEnvironment.GetPath());
				}
				else
				{
					UE_LOG(LogPakFile, Warning, TEXT("Failed to mount IoStore environment \"%s\""), *IoStoreEnvironment.GetPath());
				}
			}

			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			FCoreDelegates::PakFileMountedCallback.Broadcast(InPakFilename);
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
			FCoreDelegates::OnPakFileMounted.Broadcast(InPakFilename, Pak->PakchunkIndex);
			if (FCoreDelegates::NewFileAddedDelegate.IsBound())
			{
				TArray<FString> Filenames;
				Pak->GetFilenames(Filenames);
				for (const FString& Filename : Filenames)
				{
					FCoreDelegates::NewFileAddedDelegate.Broadcast(Filename);
				}
			}
		}
		else
		{
			delete Pak;
		}
	}
	else
	{
		UE_LOG(LogPakFile, Warning, TEXT("Failed to open pak \"%s\""), InPakFilename);
	}
	return bSuccess;
}

bool FPakPlatformFile::Unmount(const TCHAR* InPakFilename)
{
#if USE_PAK_PRECACHE
	if (GPakCache_Enable)
	{
		FPakPrecacher::Get().Unmount(InPakFilename);
	}
#endif
	{
		FScopeLock ScopedLock(&PakListCritical);

		for (int32 PakIndex = 0; PakIndex < PakFiles.Num(); PakIndex++)
		{
			if (PakFiles[PakIndex].PakFile->GetFilename() == InPakFilename)
			{
				FPakListEntry& PakListEntry = PakFiles[PakIndex];
				RemoveCachedPakSignaturesFile(*PakListEntry.PakFile->GetFilename());
				delete PakListEntry.PakFile;
				PakFiles.RemoveAt(PakIndex);
				return true;
			}
		}
	}
	return false;
}

IFileHandle* FPakPlatformFile::CreatePakFileHandle(const TCHAR* Filename, FPakFile* PakFile, const FPakEntry* FileEntry)
{
	IFileHandle* Result = NULL;
	bool bNeedsDelete = true;
	TFunction<FArchive*()> AcquirePakReader = [PakFile, LowerLevelPlatformFile = LowerLevel]() { return PakFile->GetSharedReader(LowerLevelPlatformFile); };

	// Create the handle.
	if (FileEntry->CompressionMethodIndex != 0 && PakFile->GetInfo().Version >= FPakInfo::PakFile_Version_CompressionEncryption)
	{
		if (FileEntry->IsEncrypted())
		{
			Result = new FPakFileHandle< FPakCompressedReaderPolicy<FPakSimpleEncryption> >(*PakFile, *FileEntry, AcquirePakReader, bNeedsDelete);
		}
		else
		{
			Result = new FPakFileHandle< FPakCompressedReaderPolicy<> >(*PakFile, *FileEntry, AcquirePakReader, bNeedsDelete);
		}
	}
	else if (FileEntry->IsEncrypted())
	{
		Result = new FPakFileHandle< FPakReaderPolicy<FPakSimpleEncryption> >(*PakFile, *FileEntry, AcquirePakReader, bNeedsDelete);
	}
	else
	{
		Result = new FPakFileHandle<>(*PakFile, *FileEntry, AcquirePakReader, bNeedsDelete);
	}

	return Result;
}

int32 FPakPlatformFile::MountAllPakFiles(const TArray<FString>& PakFolders)
{
	return MountAllPakFiles(PakFolders, TEXT(ALL_PAKS_WILDCARD));
}

int32 FPakPlatformFile::MountAllPakFiles(const TArray<FString>& PakFolders, const FString& WildCard)
{
	int32 NumPakFilesMounted = 0;

	bool bMountPaks = true;
	TArray<FString> PaksToLoad;
#if !UE_BUILD_SHIPPING
	// Optionally get a list of pak filenames to load, only these paks will be mounted
	FString CmdLinePaksToLoad;
	if (FParse::Value(FCommandLine::Get(), TEXT("-paklist="), CmdLinePaksToLoad))
	{
		CmdLinePaksToLoad.ParseIntoArray(PaksToLoad, TEXT("+"), true);
	}

	//if we are using a fileserver, then dont' mount paks automatically.  We only want to read files from the server.
	FString FileHostIP;
	const bool bCookOnTheFly = FParse::Value(FCommandLine::Get(), TEXT("filehostip"), FileHostIP);
	const bool bPreCookedNetwork = FParse::Param(FCommandLine::Get(), TEXT("precookednetwork"));
	if (bPreCookedNetwork)
	{
		// precooked network builds are dependent on cook on the fly
		check(bCookOnTheFly);
	}
	bMountPaks &= (!bCookOnTheFly || bPreCookedNetwork);
#endif

	if (bMountPaks)
	{
		TArray<FString> FoundPakFiles;
		FindAllPakFiles(LowerLevel, PakFolders, WildCard, FoundPakFiles);

		// HACK: If no pak files are found with the wildcard, fallback to mounting everything.
		if (FoundPakFiles.Num() == 0)
		{
			FindAllPakFiles(LowerLevel, PakFolders, ALL_PAKS_WILDCARD, FoundPakFiles);
		}

		// Sort in descending order.
		FoundPakFiles.Sort(TGreater<FString>());
		// Mount all found pak files

		TArray<FPakListEntry> ExistingPaks;
		GetMountedPaks(ExistingPaks);
		TSet<FString> ExistingPaksFileName;
		// Find the single pak we just mounted
		for (auto Pak : ExistingPaks)
		{
			ExistingPaksFileName.Add(Pak.PakFile->GetFilename());
		}


		for (int32 PakFileIndex = 0; PakFileIndex < FoundPakFiles.Num(); PakFileIndex++)
		{
			const FString& PakFilename = FoundPakFiles[PakFileIndex];

			UE_LOG(LogPakFile, Display, TEXT("Found Pak file %s attempting to mount."), *PakFilename);

			if (PaksToLoad.Num() && !PaksToLoad.Contains(FPaths::GetBaseFilename(PakFilename)))
			{
				continue;
			}

			if (ExistingPaksFileName.Contains(PakFilename))
			{
				UE_LOG(LogPakFile, Display, TEXT("Pak file %s already exists."), *PakFilename);
				continue;
			}

			uint32 PakOrder = GetPakOrderFromPakFilePath(PakFilename);

			UE_LOG(LogPakFile, Display, TEXT("Mounting pak file %s."), *PakFilename);

			SCOPED_BOOT_TIMING("Pak_Mount");
			if (Mount(*PakFilename, PakOrder))
			{
				++NumPakFilesMounted;
			}
		}
	}
	return NumPakFilesMounted;
}

int32 FPakPlatformFile::GetPakOrderFromPakFilePath(const FString& PakFilePath)
{
	if (PakFilePath.StartsWith(FString::Printf(TEXT("%sPaks/%s-"), *FPaths::ProjectContentDir(), FApp::GetProjectName())))
	{
		return 4;
	}
	else if (PakFilePath.StartsWith(FPaths::ProjectContentDir()))
	{
		return 3;
	}
	else if (PakFilePath.StartsWith(FPaths::EngineContentDir()))
	{
		return 2;
	}
	else if (PakFilePath.StartsWith(FPaths::ProjectSavedDir()))
	{
		return 1;
	}

	return 0;
}

bool FPakPlatformFile::HandleMountPakDelegate(const FString& PakFilePath, int32 PakOrder, IPlatformFile::FDirectoryVisitor* Visitor)
{
	FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Mounting pak file: %s \n"), *PakFilePath);

	if (PakOrder == INDEX_NONE)
	{
		PakOrder = GetPakOrderFromPakFilePath(PakFilePath);
	}
	
	bool bReturn = Mount(*PakFilePath, PakOrder);
	if (bReturn && Visitor != nullptr)
	{
		TArray<FPakListEntry> Paks;
		GetMountedPaks(Paks);
		// Find the single pak we just mounted
		for (auto Pak : Paks)
		{
			if (PakFilePath == Pak.PakFile->GetFilename())
			{
				// Get a list of all of the files in the pak
				for (FPakFile::FFileIterator It(*Pak.PakFile); It; ++It)
				{
					Visitor->Visit(*It.Filename(), false);
				}
				return true;
			}
		}
	}
	return bReturn;
}

bool FPakPlatformFile::HandleUnmountPakDelegate(const FString& PakFilePath)
{
	FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Unmounting pak file: %s \n"), *PakFilePath);

	return Unmount(*PakFilePath);
}

void FPakPlatformFile::RegisterEncryptionKey(const FGuid& InGuid, const FAES::FAESKey& InKey)
{
	GetRegisteredEncryptionKeys().AddKey(InGuid, InKey);

	int32 NumMounted = 0;
	TSet<int32> ChunksToNotify;

	for (const FPakListDeferredEntry& Entry : PendingEncryptedPakFiles)
	{
		if (Entry.EncryptionKeyGuid == InGuid)
		{
			if (Mount(*Entry.Filename, Entry.ReadOrder, Entry.Path.Len() == 0 ? nullptr : *Entry.Path))
			{
				UE_LOG(LogPakFile, Log, TEXT("Successfully mounted deferred pak file '%s'"), *Entry.Filename);
				NumMounted++;

				int32 PakchunkIndex = GetPakchunkIndexFromPakFile(Entry.Filename);
				if (PakchunkIndex != INDEX_NONE)
				{
					ChunksToNotify.Add(PakchunkIndex);
				}
			}
			else
			{
				UE_LOG(LogPakFile, Warning, TEXT("Failed to mount deferred pak file '%s'"), *Entry.Filename);
			}
		}
	}

	if (NumMounted > 0)
	{
		IPlatformChunkInstall * ChunkInstall = FPlatformMisc::GetPlatformChunkInstall();
		if (ChunkInstall)
		{
			for (int32 PakchunkIndex : ChunksToNotify)
			{
				ChunkInstall->ExternalNotifyChunkAvailable(PakchunkIndex);
			}
		}

		PendingEncryptedPakFiles.RemoveAll([InGuid](const FPakListDeferredEntry& Entry) { return Entry.EncryptionKeyGuid == InGuid; });

		{
			LLM_SCOPE(ELLMTag::FileSystem);
			OptimizeMemoryUsageForMountedPaks();
		}

		UE_LOG(LogPakFile, Log, TEXT("Registered encryption key '%s': %d pak files mounted, %d remain pending"), *InGuid.ToString(), NumMounted, PendingEncryptedPakFiles.Num());
	}
}

IFileHandle* FPakPlatformFile::OpenRead(const TCHAR* Filename, bool bAllowWrite)
{
	IFileHandle* Result = NULL;
	FPakFile* PakFile = NULL;
	FPakEntry FileEntry;
	if (FindFileInPakFiles(Filename, &PakFile, &FileEntry))
	{
#if PAK_TRACKER
		TrackPak(Filename, &FileEntry);
#endif

		Result = CreatePakFileHandle(Filename, PakFile, &FileEntry);

		if (Result)
		{
			FCoreDelegates::OnFileOpenedForReadFromPakFile.Broadcast(*PakFile->GetFilename(), Filename);
		}
	}
	else
	{
		if (IsNonPakFilenameAllowed(Filename))
		{
			// Default to wrapped file
			Result = LowerLevel->OpenRead(Filename, bAllowWrite);
		}
	}
	return Result;
}

const TCHAR* FPakPlatformFile::GetMountStartupPaksWildCard()
{
	return *GMountStartupPaksWildCard;
}

void FPakPlatformFile::SetMountStartupPaksWildCard(const FString& WildCard)
{
	GMountStartupPaksWildCard = WildCard;
}


EChunkLocation::Type FPakPlatformFile::GetPakChunkLocation(int32 InPakchunkIndex) const
{
	FScopeLock ScopedLock(&PakListCritical);

	for (const FPakListEntry& PakEntry : PakFiles)
	{
		if (PakEntry.PakFile->PakchunkIndex == InPakchunkIndex)
		{
			return EChunkLocation::LocalFast;
		}
	}

	for (const FPakListDeferredEntry& PendingPak : PendingEncryptedPakFiles)
	{
		if (PendingPak.PakchunkIndex == InPakchunkIndex)
		{
			return EChunkLocation::NotAvailable;
		}
	}

	return EChunkLocation::DoesNotExist;
}

bool FPakPlatformFile::AnyChunksAvailable() const
{
	FScopeLock ScopedLock(&PakListCritical);

	for (const FPakListEntry& PakEntry : PakFiles)
	{
		if (PakEntry.PakFile->PakchunkIndex != INDEX_NONE)
		{
			return true;
		}
	}

	for (const FPakListDeferredEntry& PendingPak : PendingEncryptedPakFiles)
	{
		if (PendingPak.PakchunkIndex != INDEX_NONE)
		{
			return true;
		}
	}

	return false;
}

bool FPakPlatformFile::BufferedCopyFile(IFileHandle& Dest, IFileHandle& Source, const int64 FileSize, uint8* Buffer, const int64 BufferSize) const
{
	int64 RemainingSizeToCopy = FileSize;
	// Continue copying chunks using the buffer
	while (RemainingSizeToCopy > 0)
	{
		const int64 SizeToCopy = FMath::Min(BufferSize, RemainingSizeToCopy);
		if (Source.Read(Buffer, SizeToCopy) == false)
		{
			return false;
		}
		if (Dest.Write(Buffer, SizeToCopy) == false)
		{
			return false;
		}
		RemainingSizeToCopy -= SizeToCopy;
	}
	return true;
}

bool FPakPlatformFile::CopyFile(const TCHAR* To, const TCHAR* From, EPlatformFileRead ReadFlags, EPlatformFileWrite WriteFlags)
{
	bool Result = false;
	FPakEntry FileEntry;
	FPakFile* PakFile = NULL;
	if (FindFileInPakFiles(From, &PakFile, &FileEntry))
	{
		// Copy from pak to LowerLevel->
		// Create handles both files.
		TUniquePtr<IFileHandle> DestHandle(LowerLevel->OpenWrite(To, false, (WriteFlags & EPlatformFileWrite::AllowRead) != EPlatformFileWrite::None));
		TUniquePtr<IFileHandle> SourceHandle(CreatePakFileHandle(From, PakFile, &FileEntry));

		if (DestHandle && SourceHandle)
		{
			const int64 BufferSize = 64 * 1024; // Copy in 64K chunks.
			uint8* Buffer = (uint8*)FMemory::Malloc(BufferSize);
			Result = BufferedCopyFile(*DestHandle, *SourceHandle, SourceHandle->Size(), Buffer, BufferSize);
			FMemory::Free(Buffer);
		}
	}
	else
	{
		Result = LowerLevel->CopyFile(To, From, ReadFlags, WriteFlags);
	}
	return Result;
}

void FPakPlatformFile::UnloadPakEntryFilenames(TArray<FString>* DirectoryRootsToKeep)
{
	int32 TotalNumFilenames = 0;
	int32 NumFilenamesUnloaded = 0;
	int32 NumPaks = 0;
	double Timer = 0.0;
	{
		SCOPE_SECONDS_COUNTER(Timer);

		TArray<FPakListEntry> Paks;
		GetMountedPaks(Paks);
		TMap<uint64, FPakEntry> CrossPakCollisionDetector;
		
		for (auto Pak : Paks)
		{
			if (Pak.PakFile->HasFilenames())
			{
				TotalNumFilenames += Pak.PakFile->GetNumFiles();
			}
		}

		CrossPakCollisionDetector.Reserve(TotalNumFilenames);

		// Sort the pak list by number of entries so that we allow the larger ones a better chance of not encountering a collision
		Algo::Sort(Paks, [](const FPakListEntry& A, const FPakListEntry& B) { return A.PakFile->GetNumFiles() > B.PakFile->GetNumFiles(); });

		for (auto Pak : Paks)
		{
			if (Pak.PakFile->HasFilenames())
			{
				NumPaks++;
				int32 NumFilesInThisPak = Pak.PakFile->GetNumFiles();

				if (Pak.PakFile->UnloadPakEntryFilenames(CrossPakCollisionDetector, DirectoryRootsToKeep))
				{
					NumFilenamesUnloaded += NumFilesInThisPak;
				}
			}
		}
	}
	UE_LOG(LogPakFile, Log, TEXT("Unloaded %d/%d filenames from %d pak files in %.4fs"), NumFilenamesUnloaded, TotalNumFilenames, NumPaks, Timer);
}

void FPakPlatformFile::ShrinkPakEntriesMemoryUsage()
{
	double Timer = 0.0;
	int32 NumPakFiles = 0;
	int32 NumEntries = 0;
	{
		SCOPE_SECONDS_COUNTER(Timer);

		TArray<FPakListEntry> Paks;
		GetMountedPaks(Paks);
		for (auto Pak : Paks)
		{
			if (!Pak.PakFile->HasShrunkPakEntries())
			{
				NumPakFiles++;
				if (Pak.PakFile->ShrinkPakEntriesMemoryUsage())
				{
					NumEntries += Pak.PakFile->GetNumFiles();
				}
			}
		}
	}
	UE_LOG(LogPakFile, Log, TEXT("Shrunk %d entries from %d pak files in %.4fs"), NumEntries, NumPakFiles, Timer);
}

/**
 * Module for the pak file
 */
class FPakFileModule : public IPlatformFileModule
{
public:
	virtual IPlatformFile* GetPlatformFile() override
	{
		check(Singleton.IsValid());
		return Singleton.Get();
	}

	virtual void StartupModule() override
	{
		Singleton = MakeUnique<FPakPlatformFile>();
		FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("RSA"));
	}

	virtual void ShutdownModule() override
	{
		// remove ourselves from the platform file chain (there can be late writes after the shutdown).
		if (Singleton.IsValid())
		{
			if (FPlatformFileManager::Get().FindPlatformFile(Singleton.Get()->GetName()))
			{
				FPlatformFileManager::Get().RemovePlatformFile(Singleton.Get());
			}
		}

		Singleton.Reset();
	}

	TUniquePtr<IPlatformFile> Singleton;
};

void FPakFile::AddSpecialFile(FPakEntry Entry, const FString& Filename)
{
	MakeDirectoryFromPath(MountPoint);

	int32 EntryIndex = Data->Files.Num();

	// Add new file info.
	Data->Files.Add(Entry);
	NumEntries++;

	// Construct Index of all directories in pak file.


	FString CleanFilename(Filename.Mid(MountPoint.Len() ));

	FString Path = FPaths::GetPath(CleanFilename);
	FString Path2 = FPaths::GetPath(Filename);
	MakeDirectoryFromPath(Path);



	FPakDirectory* Directory = Data->Index.Find(Path);
	if (Directory != NULL)
	{
		Directory->Add(FPaths::GetCleanFilename(CleanFilename), EntryIndex);
	}
	else
	{
		FPakDirectory& NewDirectory = Data->Index.Add(Path);
		NewDirectory.Add(FPaths::GetCleanFilename(CleanFilename), EntryIndex);

		// add the parent directories up to the mount point
		while (MountPoint != Path)
		{
			Path.LeftInline(Path.Len() - 1, false);
			int32 Offset = 0;
			if (Path.FindLastChar('/', Offset))
			{
				Path.LeftInline(Offset, false);
				MakeDirectoryFromPath(Path);
				if (Data->Index.Find(Path) == NULL)
				{
					Data->Index.Add(Path);
				}
			}
			else
			{
				Path = MountPoint;
			}
		}
	}
}



void FPakPlatformFile::MakeUniquePakFilesForTheseFiles(TArray<TArray<FString>> InFiles)
{
	for (int k = 0; k < InFiles.Num(); k++)
	{
		FPakFile* NewPakFile = NULL;
		for (int i = 0; i < InFiles[k].Num(); i++)
		{
			FPakEntry FileEntry;
			FPakFile* PakFile = NULL;
			bool bFoundEntry = FindFileInPakFiles(*InFiles[k][i], &PakFile, &FileEntry);
			if (bFoundEntry && PakFile && PakFile->GetFilenameName() != NAME_None)
			{
				if (NewPakFile == nullptr && Mount(*PakFile->GetFilename(), 500, *PakFile->MountPoint, false))
				{
					// we successfully mounted the file, find the empty pak file we just added.
					for (int j = 0; j < PakFiles.Num(); j++)
					{
						if (PakFiles[j].PakFile->Data->Files.Num() == 0 && PakFiles[j].PakFile->CachedTotalSize == PakFile->CachedTotalSize)
						{
							NewPakFile = PakFiles[j].PakFile;
							break;
						}
					}
					if (NewPakFile != nullptr)
					{
						NewPakFile->SetCacheType(FPakFile::ECacheType::Individual);
					}
				}

				if (NewPakFile != nullptr)
				{
//					NewPakFile->SetUnderlyingCacheTrimDisabled(true);
					NewPakFile->AddSpecialFile(FileEntry, *InFiles[k][i]);
				}

			}
		}

	}
}


IMPLEMENT_MODULE(FPakFileModule, PakFile);
