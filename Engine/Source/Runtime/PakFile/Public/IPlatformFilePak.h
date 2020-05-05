// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Stats/Stats.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Templates/UniquePtr.h"
#include "Math/BigInt.h"
#include "Misc/AES.h"
#include "RSA.h"
#include "Misc/SecureHash.h"
#include "GenericPlatform/GenericPlatformChunkInstall.h"
#include "Serialization/MemoryImage.h"

class FChunkCacheWorker;
class IAsyncReadFileHandle;

PAKFILE_API DECLARE_LOG_CATEGORY_EXTERN(LogPakFile, Log, All);
DECLARE_FLOAT_ACCUMULATOR_STAT_EXTERN(TEXT("Total pak file read time"), STAT_PakFile_Read, STATGROUP_PakFile, PAKFILE_API);

DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num open pak file handles"), STAT_PakFile_NumOpenHandles, STATGROUP_PakFile, PAKFILE_API);

#define PAK_TRACKER 0

// Define the type of a chunk hash. Currently selectable between SHA1 and CRC32.
#define PAKHASH_USE_CRC	1
#if PAKHASH_USE_CRC
typedef uint32 TPakChunkHash;
#else
typedef FSHAHash TPakChunkHash;
#endif

PAKFILE_API TPakChunkHash ComputePakChunkHash(const void* InData, int64 InDataSizeInBytes);
FORCEINLINE FString ChunkHashToString(const TPakChunkHash& InHash)
{
#if PAKHASH_USE_CRC
	return FString::Printf(TEXT("%08X"), InHash);
#else
	return LexToString(InHash);
#endif
}

struct FPakChunkSignatureCheckFailedData
{
	FPakChunkSignatureCheckFailedData(const FString& InPakFilename, const TPakChunkHash& InExpectedHash, const TPakChunkHash& InReceivedHash, int32 InChunkIndex)
		: PakFilename(InPakFilename)
		, ChunkIndex(InChunkIndex)
		, ExpectedHash(InExpectedHash)
		, ReceivedHash(InReceivedHash)
	{
	}
	FString PakFilename;
	int32 ChunkIndex;
	TPakChunkHash ExpectedHash;
	TPakChunkHash ReceivedHash;

	FPakChunkSignatureCheckFailedData() : ChunkIndex(0) {}
};
/** Delegate for allowing a game to restrict the accessing of non-pak files */
DECLARE_DELEGATE_RetVal_OneParam(bool, FFilenameSecurityDelegate, const TCHAR* /*InFilename*/);
DECLARE_DELEGATE_ThreeParams(FPakCustomEncryptionDelegate, uint8* /*InData*/, uint32 /*InDataSize*/, FGuid /*InEncryptionKeyGuid*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FPakChunkSignatureCheckFailedHandler, const FPakChunkSignatureCheckFailedData&);
DECLARE_MULTICAST_DELEGATE_OneParam(FPakMasterSignatureTableCheckFailureHandler, const FString&);

/**
 * Struct which holds pak file info (version, index offset, hash value).
 */
struct FPakInfo
{
	enum 
	{
		/** Magic number to use in header */
		PakFile_Magic = 0x5A6F12E1,
		/** Size of cached data. */
		MaxChunkDataSize = 64*1024,
		/** Length of a compression format name */
		CompressionMethodNameLen = 32,
		/** Number of allowed different methods */
		MaxNumCompressionMethods=5, // when we remove patchcompatibilitymode421 we can reduce this to 4
	};

	/** Version numbers. */
	enum
	{
		PakFile_Version_Initial = 1,
		PakFile_Version_NoTimestamps = 2,
		PakFile_Version_CompressionEncryption = 3,
		PakFile_Version_IndexEncryption = 4,
		PakFile_Version_RelativeChunkOffsets = 5,
		PakFile_Version_DeleteRecords = 6,
		PakFile_Version_EncryptionKeyGuid = 7,
		PakFile_Version_FNameBasedCompressionMethod = 8,
		PakFile_Version_FrozenIndex = 9,


		PakFile_Version_Last,
		PakFile_Version_Invalid,
		PakFile_Version_Latest = PakFile_Version_Last - 1
	};

	/** Pak file magic value. */
	uint32 Magic;
	/** Pak file version. */
	int32 Version;
	/** Offset to pak file index. */
	int64 IndexOffset;
	/** Size (in bytes) of pak file index. */
	int64 IndexSize;
	/** Index SHA1 value. */
	FSHAHash IndexHash;
	/** Flag indicating if the pak index has been encrypted. */
	uint8 bEncryptedIndex;
	/** Flag indicating if the pak index has been frozen */
	// @todo loadtime: we should find a way to unload the index - potentially make two indices, the full one and unloaded one? unclear how, but at least we now have an option to choose per-platform
	uint8 bIndexIsFrozen;
	/** Encryption key guid. Empty if we should use the embedded key. */
	FGuid EncryptionKeyGuid;
	/** Compression methods used in this pak file (FNames, saved as FStrings) */
	TArray<FName> CompressionMethods;

	/**
	 * Constructor.
	 */
	FPakInfo()
		: Magic(PakFile_Magic)
		, Version(PakFile_Version_Latest)
		, IndexOffset(-1)
		, IndexSize(0)
		, bEncryptedIndex(0)
		, bIndexIsFrozen(0)
	{
		// we always put in a NAME_None entry as index 0, so that an uncompressed PakEntry will have CompressionMethodIndex of 0 and can early out easily
		CompressionMethods.Add(NAME_None);
	}

	/**
	 * Gets the size of data serialized by this struct.
	 *
	 * @return Serialized data size.
	 */
	int64 GetSerializedSize(int32 InVersion = PakFile_Version_Latest) const
	{
		int64 Size = sizeof(Magic) + sizeof(Version) + sizeof(IndexOffset) + sizeof(IndexSize) + sizeof(IndexHash) + sizeof(bEncryptedIndex);
		if (InVersion >= PakFile_Version_EncryptionKeyGuid) Size += sizeof(EncryptionKeyGuid);
		if (InVersion >= PakFile_Version_FNameBasedCompressionMethod) Size += CompressionMethodNameLen * MaxNumCompressionMethods;
		if (InVersion >= PakFile_Version_FrozenIndex) Size += sizeof(bIndexIsFrozen);

		return Size;
	}

	/**
	 */
	int64 HasRelativeCompressedChunkOffsets() const
	{
		return Version >= PakFile_Version_RelativeChunkOffsets;
	}

	/**
	 * Serializes this struct.
	 *
	 * @param Ar Archive to serialize data with.
	 */
	void Serialize(FArchive& Ar, int32 InVersion)
	{
		if (Ar.IsLoading() && Ar.TotalSize() < (Ar.Tell() + GetSerializedSize(InVersion)))
		{
			Magic = 0;
			return;
		}

		if (Ar.IsSaving() || InVersion >= PakFile_Version_EncryptionKeyGuid)
		{
			Ar << EncryptionKeyGuid;
		}
		Ar << bEncryptedIndex;
		Ar << Magic;
		if (Magic != PakFile_Magic)
		{
			// handle old versions by failing out now (earlier versions will be attempted)
			Magic = 0;
			return;
		}

		Ar << Version;
		Ar << IndexOffset;
		Ar << IndexSize;
		Ar << IndexHash;

		if (Ar.IsLoading())
		{
			if (Version < PakFile_Version_IndexEncryption)
			{
				bEncryptedIndex = false;
			}

			if (Version < PakFile_Version_EncryptionKeyGuid)
			{
				EncryptionKeyGuid.Invalidate();
			}
		}

		if (Version >= PakFile_Version_FrozenIndex)
		{
			Ar << bIndexIsFrozen;
		}

		if (Version < PakFile_Version_FNameBasedCompressionMethod)
		{
			// for old versions, put in some known names that we may have used
			CompressionMethods.Add(NAME_Zlib);
			CompressionMethods.Add(NAME_Gzip);
			CompressionMethods.Add(TEXT("Oodle"));
		}
		else
		{
			// we need to serialize a known size, so make a buffer of "strings"
			const int32 BufferSize = CompressionMethodNameLen * MaxNumCompressionMethods;
			ANSICHAR Methods[BufferSize];
			if (Ar.IsLoading())
			{
				Ar.Serialize(Methods, BufferSize);
				for (int32 Index = 0; Index < MaxNumCompressionMethods; Index++)
				{
					ANSICHAR* MethodString = &Methods[Index * CompressionMethodNameLen];
					if (MethodString[0] != 0)
					{
						CompressionMethods.Add(FName(MethodString));
					}
				}
			}
			else
			{
				// we always zero out fully what we write out so that reading in is simple
				FMemory::Memzero(Methods, BufferSize);

				for (int32 Index = 1; Index < CompressionMethods.Num(); Index++)
				{
					ANSICHAR* MethodString = &Methods[(Index - 1) * CompressionMethodNameLen];
					FCStringAnsi::Strcpy(MethodString, CompressionMethodNameLen, TCHAR_TO_ANSI(*CompressionMethods[Index].ToString()));
				}
				Ar.Serialize(Methods, BufferSize);
			}
		}
	}

	uint8 GetCompressionMethodIndex(FName CompressionMethod)
	{
		// look for existing method
		for (uint8 Index = 0; Index < CompressionMethods.Num(); Index++)
		{
			if (CompressionMethods[Index] == CompressionMethod)
			{
				return Index;
			}
		}

		checkf(CompressionMethod.ToString().Len() < CompressionMethodNameLen, TEXT("Compression method name, %s, is too long for pak file serialization. You can increase CompressionMethodNameLen, but then will have to handle version management."), *CompressionMethod.ToString());
		// CompressionMethods always has None at Index 0, that we don't serialize, so we can allow for one more in the array
		checkf(CompressionMethods.Num() <= MaxNumCompressionMethods, TEXT("Too many unique compression methods in one pak file. You can increase MaxNumCompressionMethods, but then will have to handle version management."));

		// add it if it didn't exist
		return CompressionMethods.Add(CompressionMethod);
	}

	FName GetCompressionMethod(uint8 Index) const
	{
		return CompressionMethods[Index];
	}
};

/**
 * Struct storing offsets and sizes of a compressed block.
 */
struct FPakCompressedBlock
{
	DECLARE_TYPE_LAYOUT(FPakCompressedBlock, NonVirtual);

	/** Offset of the start of a compression block. Offset is relative to the start of the compressed chunk data */
	LAYOUT_FIELD(int64, CompressedStart);
	/** Offset of the end of a compression block. This may not align completely with the start of the next block. Offset is relative to the start of the compressed chunk data. */
	LAYOUT_FIELD(int64, CompressedEnd);

	bool operator == (const FPakCompressedBlock& B) const
	{
		return CompressedStart == B.CompressedStart && CompressedEnd == B.CompressedEnd;
	}

	bool operator != (const FPakCompressedBlock& B) const
	{
		return !(*this == B);
	}
};

FORCEINLINE FArchive& operator<<(FArchive& Ar, FPakCompressedBlock& Block)
{
	Ar << Block.CompressedStart;
	Ar << Block.CompressedEnd;
	return Ar;
}

/**
 * Struct holding info about a single file stored in pak file.
 *
 * CHANGE THIS FILE RARELY AND WITH GREAT CARE. MODIFICATIONS
 * WILL RESULT IN EVERY PAK ENTRY IN AN EXISTING INSTALL HAVING TO
 * TO BE PATCHED.
 *
*  On Fortnite that would be 15GB of data 
* (250k pak entries * 64kb patch block) just to add/change/remove 
 * a field.
 * 
 */
struct FPakEntry
{
	DECLARE_TYPE_LAYOUT(FPakEntry, NonVirtual);

	static const uint8 Flag_None = 0x00;
	static const uint8 Flag_Encrypted = 0x01;
	static const uint8 Flag_Deleted = 0x02;

	/** Offset into pak file where the file is stored.*/
	LAYOUT_FIELD(int64, Offset);
	/** Serialized file size. */
	LAYOUT_FIELD(int64, Size);
	/** Uncompressed file size. */
	LAYOUT_FIELD(int64, UncompressedSize);
	/** File SHA1 value. */
	LAYOUT_ARRAY(uint8, Hash, 20);
	/** Array of compression blocks that describe how to decompress this pak entry. */
	LAYOUT_FIELD(TMemoryImageArray<FPakCompressedBlock>, CompressionBlocks);
	/** Size of a compressed block in the file. */
	LAYOUT_FIELD(uint32, CompressionBlockSize);
	/** Index into the compression methods in this pakfile. */
	LAYOUT_FIELD(uint32, CompressionMethodIndex);
	/** Pak entry flags. */
	LAYOUT_FIELD(uint8, Flags);
	/** Flag is set to true when FileHeader has been checked against PakHeader. It is not serialized. */
	LAYOUT_MUTABLE_FIELD(bool, Verified);

	/**
	 * Constructor.
	 */
	FPakEntry()
		: Offset(-1)
		, Size(0)
		, UncompressedSize(0)
		, CompressionBlockSize(0)
		, CompressionMethodIndex(0)
		, Flags(Flag_None)
		, Verified(false)
	{
		FMemory::Memset(Hash, 0, sizeof(Hash));
	}

	/**
	 * Gets the size of data serialized by this struct.
	 *
	 * @return Serialized data size.
	 */
	int64 GetSerializedSize(int32 Version) const
	{
		int64 SerializedSize = sizeof(Offset) + sizeof(Size) + sizeof(UncompressedSize) + sizeof(Hash);

		if (Version >= FPakInfo::PakFile_Version_FNameBasedCompressionMethod)
		{
			SerializedSize += sizeof(CompressionMethodIndex);
		}
		else
		{
			SerializedSize += sizeof(int32); // Old CompressedMethod var from pre-fname based compression methods
		}

		if (Version >= FPakInfo::PakFile_Version_CompressionEncryption)
		{
			SerializedSize += sizeof(Flags) + sizeof(CompressionBlockSize);
			if(CompressionMethodIndex != 0)
			{
				SerializedSize += sizeof(FPakCompressedBlock) * CompressionBlocks.Num() + sizeof(int32);
			}
		}
		if (Version < FPakInfo::PakFile_Version_NoTimestamps)
		{
			// Timestamp
			SerializedSize += sizeof(int64);
		}
		return SerializedSize;
	}

	/**
	 * Compares two FPakEntry structs.
	 */
	bool operator == (const FPakEntry& B) const
	{
		// Offsets are not compared here because they're not
		// serialized with file headers anyway.
		return Size == B.Size && 
			UncompressedSize == B.UncompressedSize &&
			CompressionMethodIndex == B.CompressionMethodIndex &&
			Flags == B.Flags &&
			CompressionBlockSize == B.CompressionBlockSize &&
			FMemory::Memcmp(Hash, B.Hash, sizeof(Hash)) == 0 &&
			CompressionBlocks == B.CompressionBlocks;
	}

	/**
	 * Compares two FPakEntry structs.
	 */
	bool operator != (const FPakEntry& B) const
	{
		// Offsets are not compared here because they're not
		// serialized with file headers anyway.
		return Size != B.Size || 
			UncompressedSize != B.UncompressedSize ||
			CompressionMethodIndex != B.CompressionMethodIndex ||
			Flags != B.Flags ||
			CompressionBlockSize != B.CompressionBlockSize ||
			FMemory::Memcmp(Hash, B.Hash, sizeof(Hash)) != 0 ||
			CompressionBlocks != B.CompressionBlocks;
	}

	/**
	 * Serializes FPakEntry struct.
	 *
	 * @param Ar Archive to serialize data with.
	 * @param Entry Data to serialize.
	 */
	void Serialize(FArchive& Ar, int32 Version)
	{
		Ar << Offset;
		Ar << Size;
		Ar << UncompressedSize;
		if (Version < FPakInfo::PakFile_Version_FNameBasedCompressionMethod)
		{
			int32 LegacyCompressionMethod;
			Ar << LegacyCompressionMethod;
			if (LegacyCompressionMethod == COMPRESS_None)
			{
				CompressionMethodIndex = 0;
			}
			else if (LegacyCompressionMethod & COMPRESS_ZLIB)
			{
				CompressionMethodIndex = 1;
			}
			else if (LegacyCompressionMethod & COMPRESS_GZIP)
			{
				CompressionMethodIndex = 2;
			}
			else if (LegacyCompressionMethod & COMPRESS_Custom)
			{
				CompressionMethodIndex = 3;
			}
			else
			{
				UE_LOG(LogPakFile, Fatal, TEXT("Found an unknown compression type in pak file, will need to be supported for legacy files"));
			}
		}
		else
		{
			Ar << CompressionMethodIndex;
		}
		if (Version <= FPakInfo::PakFile_Version_Initial)
		{
			FDateTime Timestamp;
			Ar << Timestamp;
		}
		Ar.Serialize(Hash, sizeof(Hash));
		if (Version >= FPakInfo::PakFile_Version_CompressionEncryption)
		{
			if(CompressionMethodIndex != 0)
			{
				Ar << CompressionBlocks;
			}
			Ar << Flags;
			Ar << CompressionBlockSize;
		}
	}

	FORCEINLINE void SetFlag( uint8 InFlag, bool bValue )
	{
		if( bValue )
		{
			Flags |= InFlag;
		}
		else
		{
			Flags &= ~InFlag;
		}
	}

	FORCEINLINE bool GetFlag( uint8 InFlag ) const
	{
		return (Flags & InFlag) == InFlag;
	}
	
	FORCEINLINE bool IsEncrypted() const             { return GetFlag(Flag_Encrypted); }
	FORCEINLINE void SetEncrypted( bool bEncrypted ) { SetFlag( Flag_Encrypted, bEncrypted ); }

	FORCEINLINE bool IsDeleteRecord() const                { return GetFlag(Flag_Deleted); }
	FORCEINLINE void SetDeleteRecord( bool bDeleteRecord ) { SetFlag(Flag_Deleted, bDeleteRecord ); }


	/**
	* Verifies two entries match to check for corruption.
	*
	* @param FileEntryA Entry 1.
	* @param FileEntryB Entry 2.
	*/
	static bool VerifyPakEntriesMatch(const FPakEntry& FileEntryA, const FPakEntry& FileEntryB);
};

/** Pak directory type mapping a filename to a FPakEntry index within the FPakFile.Files array or the MiniPakEntriesOffsets array depending on the bit-encoded state of the FPakEntry structures. */
typedef TMemoryImageMap<FMemoryImageString, int32> FPakDirectory;

struct PAKFILE_API FPakFileData
{
	DECLARE_TYPE_LAYOUT(FPakFileData, NonVirtual);


	/** Mount point. */
	LAYOUT_FIELD(FMemoryImageString, MountPoint);
	/** Info on all files stored in pak. */
	LAYOUT_FIELD(TMemoryImageArray<FPakEntry>, Files);
	/** Pak Index organized as a map of directories for faster Directory iteration. Completely valid only when bFilenamesRemoved == false, although portions may still be valid after a call to UnloadPakEntryFilenames() while utilizing DirectoryRootsToKeep. */
	LAYOUT_FIELD((TMemoryImageMap<FMemoryImageString, FPakDirectory>), Index);
};

/**
 * Pak file.
 */
class PAKFILE_API FPakFile : FNoncopyable
{
	friend class FPakPlatformFile;

	TUniquePtr<FPakFileData> Data;

	/** Pak filename. */
	FString PakFilename;
	FName PakFilenameName;
	/** Archive to serialize the pak file from. */
	TUniquePtr<class FChunkCacheWorker> Decryptor;
	/** Map of readers assigned to threads. */
	TMap<uint32, TUniquePtr<FArchive>> ReaderMap;
	/** Critical section for accessing ReaderMap. */
	FCriticalSection CriticalSection;
	/** Pak file info (trailer). */
	FPakInfo Info;
	/** Mount point. */
	FString MountPoint;
	/** The hash to use when generating a filename hash (CRC) to avoid collisions within the hashed filename space. */
	uint64 FilenameStartHash;
	/** An array of 256 + 1 size that represents the starting index of the most significant byte of a hash group within the FilenameHashes array. */
	uint32* FilenameHashesIndex;
	/** An array of NumEntries size mapping 1:1 with FilenameHashes and describing the index of the FPakEntry. */
	int32* FilenameHashesIndices;
	/** A tightly packed array of filename hashes (CRC) of NumEntries size. */
	uint64* FilenameHashes;
	/** A tightly packed array, NumEntries in size, of offsets to the pak entry data within the MiniPakEntries buffer */
	uint32* MiniPakEntriesOffsets;
	/** Memory buffer representing the minimal file entry headers, NumEntries in size */
	uint8* MiniPakEntries;
	/** The number of file entries in the pak file */
	int32 NumEntries;
	/** Timestamp of this pak file. */
	FDateTime Timestamp;	
	/** TotalSize of the pak file */
	int64 CachedTotalSize;
	/** True if this is a signed pak file. */
	bool bSigned;
	/** True if this pak file is valid and usable. */
	bool bIsValid;
	/** True if all filenames in memory for this pak file have been hashed to a 32-bit value. Wildcard traversal is impossible when true. */
	bool bFilenamesRemoved;
	/** ID for the chunk this pakfile is part of. INDEX_NONE if this isn't a pak chunk (derived from filename) */
	int32 PakchunkIndex;
	/** Flag to say we tried shrinking pak entries already */
	bool bAttemptedPakEntryShrink;
	/** Flag to say we tried unloading pak index filenames already */
	bool bAttemptedPakFilenameUnload;

	class IMappedFileHandle* MappedFileHandle;
	FCriticalSection MappedFileHandleCriticalSection;


	static inline int32 CDECL CompareFilenameHashes(const void* Left, const void* Right)
	{
		const uint64* LeftHash = (const uint64*)Left;
		const uint64* RightHash = (const uint64*)Right;
		if (*LeftHash < *RightHash)
		{
			return -1;
		}
		if (*LeftHash > *RightHash)
		{
			return 1;
		}
		return 0;
	}

	FArchive* CreatePakReader(const TCHAR* Filename);
	FArchive* CreatePakReader(IFileHandle& InHandle, const TCHAR* Filename);
	FArchive* SetupSignedPakReader(FArchive* Reader, const TCHAR* Filename);


public:
	/** Pak files can share a cache or have their own */
	enum class ECacheType : uint8
	{
		Shared,
		Individual,
	};
private:
	/** The type of cache this pak file should have */
	ECacheType	CacheType;
	/** The index of this pak file into the cache array, -1 = not initialized */
	int32		CacheIndex;
	/** Allow the cache of a pak file to never shrink, should be used with caution, it will burn memory */
	bool UnderlyingCacheTrimDisabled;

public:

	void SetUnderlyingCacheTrimDisabled(bool InUnderlyingCacheTrimDisabled) { UnderlyingCacheTrimDisabled = InUnderlyingCacheTrimDisabled; }
	bool GetUnderlyingCacheTrimDisabled(void) { return UnderlyingCacheTrimDisabled; }

	void SetCacheType(ECacheType InCacheType) { CacheType = InCacheType; }
	ECacheType GetCacheType(void) { return CacheType; }
	void SetCacheIndex(int32 InCacheIndex) { CacheIndex = InCacheIndex; }
	int32 GetCacheIndex(void) { return CacheIndex; }
#if IS_PROGRAM
	/**
	* Opens a pak file given its filename.
	*
	* @param Filename Pak filename.
	* @param bIsSigned true if the pak is signed
	*/
	FPakFile(const TCHAR* Filename, bool bIsSigned);
#endif

	/**
	 * Creates a pak file using the supplied file handle.
	 *
	 * @param LowerLevel Lower level platform file.
	 * @param Filename Filename.
	 * @param bIsSigned = true if the pak is signed.
	 */
	FPakFile(IPlatformFile* LowerLevel, const TCHAR* Filename, bool bIsSigned, bool bLoadIndex = true);

	/**
	 * Creates a pak file using the supplied archive.
	 *
	 * @param Archive	Pointer to the archive which contains the pak file data.
	 */
#if WITH_EDITOR
	FPakFile(FArchive* Archive);
#endif

	~FPakFile();

	/**
	 * Checks if the pak file is valid.
	 *
	 * @return true if this pak file is valid, false otherwise.
	 */
	bool IsValid() const
	{
		return bIsValid;
	}

	/**
	 * Checks if the pak has valid chunk signature checking data, and that the data passed the initial signing check
	 *
	 * @return true if this pak file has passed the initial signature checking phase
	 */
	bool PassedSignatureChecks() const;

	/**
	 * Gets pak filename.
	 *
	 * @return Pak filename.
	 */
	const FString& GetFilename() const
	{
		return PakFilename;
	}
	FName GetFilenameName() const
	{
		return PakFilenameName;
	}

	int64 TotalSize() const
	{
		return CachedTotalSize;
	}

	/**
	 * Gets pak file index.
	 *
	 * @return Pak index.
	 */
	const TMemoryImageMap<FMemoryImageString, FPakDirectory>& GetIndex() const
	{
		return Data->Index;
	}

	/**
	 * Gets the number of files in this pak.
	 */
	int32 GetNumFiles() const
	{
		return NumEntries;
	}

	void GetFilenames(TArray<FString>& OutFileList) const;

	void GetFilenamesInChunk(const TArray<int32>& InChunkIDs, TArray<FString>& OutFileList);

	/**
	 * Gets shared pak file archive for given thread.
	 *
	 * @return Pointer to pak file archive used to read data from pak.
	 */
	FArchive* GetSharedReader(IPlatformFile* LowerLevel);

	/**
	 * Finds an entry in the pak file matching the given filename.
	 *
	 * @param Filename File to find.
	 * @param OutEntry The optional address of an FPakEntry instance where the found file information should be stored. Pass NULL to only check for file existence.
	 * @return Returns true if the file was found, false otherwise.
	 */
	enum class EFindResult : uint8
	{
		NotFound,
		Found,
		FoundDeleted,
	};
	EFindResult Find(const FString& Filename, FPakEntry* OutEntry) const;

	/**
	 * Sets the pak file mount point.
	 *
	 * @param Path New mount point path.
	 */
	void SetMountPoint(const TCHAR* Path)
	{
		MountPoint = Path;
		MakeDirectoryFromPath(MountPoint);
	}

	/**
	 * Gets pak file mount point.
	 *
	 * @return Mount point path.
	 */
	const FString& GetMountPoint() const
	{
		return MountPoint;
	}

	/**
	 * Looks for files or directories within the pak file.
	 *
	 * @param OutFiles List of files or folder matching search criteria.
	 * @param InPath Path to look for files or folder at.
	 * @param bIncludeFiles If true OutFiles will include matching files.
	 * @param bIncludeDirectories If true OutFiles will include matching folders.
	 * @param bRecursive If true, sub-folders will also be checked.
	 */
	template <class ContainerType>
	void FindFilesAtPath(ContainerType& OutFiles, const TCHAR* InPath, bool bIncludeFiles = true, bool bIncludeDirectories = false, bool bRecursive = false) const
	{
		// Make sure all directory names end with '/'.
		FString Directory(InPath);
		MakeDirectoryFromPath(Directory);

		// Check the specified path is under the mount point of this pak file.
		// The reverse case (MountPoint StartsWith Directory) is needed to properly handle
		// pak files that are a subdirectory of the actual directory.
		if ((Directory.StartsWith(MountPoint)) || (MountPoint.StartsWith(Directory)))
		{
			if (bFilenamesRemoved)
			{
				//FPlatformMisc::LowLevelOutputDebugString(*(FString("FindFilesAtPath() used when bFilenamesRemoved == true: ") + InPath));
			}

			//checkf(!bFilenamesRemoved, TEXT("FPakFile::FindFilesAtPath() can only be used before FPakPlatformFile::UnloadFilenames() is called."));

			TArray<FString> DirectoriesInPak; // List of all unique directories at path
			for (TMemoryImageMap<FMemoryImageString, FPakDirectory>::TConstIterator It(Data->Index); It; ++It)
			{
				FString PakPath(MountPoint + It.Key());
				// Check if the file is under the specified path.
				if (PakPath.StartsWith(Directory))
				{				
					if (bRecursive == true)
					{
						// Add everything
						if (bIncludeFiles)
						{
							for (FPakDirectory::TConstIterator DirectoryIt(It.Value()); DirectoryIt; ++DirectoryIt)
							{
								OutFiles.Add(MountPoint + It.Key() + DirectoryIt.Key());
							}
						}
						if (bIncludeDirectories)
						{
							if (Directory != PakPath)
							{
								DirectoriesInPak.Add(PakPath);
							}
						}
					}
					else
					{
						int32 SubDirIndex = PakPath.Len() > Directory.Len() ? PakPath.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Directory.Len() + 1) : INDEX_NONE;
						// Add files in the specified folder only.
						if (bIncludeFiles && SubDirIndex == INDEX_NONE)
						{
							for (FPakDirectory::TConstIterator DirectoryIt(It.Value()); DirectoryIt; ++DirectoryIt)
							{
								OutFiles.Add(MountPoint + It.Key() + DirectoryIt.Key());
							}
						}
						// Add sub-folders in the specified folder only
						if (bIncludeDirectories && SubDirIndex >= 0)
						{
							DirectoriesInPak.AddUnique(PakPath.Left(SubDirIndex + 1));
						}
					}
				}
			}
			OutFiles.Append(DirectoriesInPak);
		}
	}

	/**
	 * Finds a directory in pak file.
	 *
	 * @param InPath Directory path.
	 * @return Pointer to a map with directory contents if the directory was found, NULL otherwise.
	 */
	const FPakDirectory* FindDirectory(const TCHAR* InPath) const
	{
		FString Directory(InPath);
		MakeDirectoryFromPath(Directory);
		const FPakDirectory* PakDirectory = NULL;

		// Check the specified path is under the mount point of this pak file.
		if (Directory.StartsWith(MountPoint))
		{
			PakDirectory = Data->Index.Find(Directory.Mid(MountPoint.Len()));
		}
		return PakDirectory;
	}

	/**
	 * Checks if a directory exists in pak file.
	 *
	 * @param InPath Directory path.
	 * @return true if the given path exists in pak file, false otherwise.
	 */
	bool DirectoryExists(const TCHAR* InPath) const
	{
		return !!FindDirectory(InPath);
	}
	
	/**
	 * Checks the validity of the pak data by reading out the data for every file in the pak
	 *
	 * @return true if the pak file is valid
	 */
	bool Check();

	/** Iterator class used to iterate over all files in pak. */
	class FFileIterator
	{
		/** Owner pak file. */
		const FPakFile& PakFile;
		/** Index iterator. */
		TMemoryImageMap<FMemoryImageString, FPakDirectory>::TConstIterator IndexIt;
		/** Directory iterator. */
		FPakDirectory::TConstIterator DirectoryIt;
		/** The cached filename for return in Filename() */
		FString CachedFilename;
		/** Whether to include delete records in the iteration */
		bool bIncludeDeleted;

	public:
		/**
		 * Constructor.
		 *
		 * @param InPakFile Pak file to iterate.
		 */
		FFileIterator(const FPakFile& InPakFile, bool bInIncludeDeleted = false )
			: PakFile(InPakFile)
			, IndexIt(PakFile.GetIndex())
			, DirectoryIt((IndexIt ? FPakDirectory::TConstIterator(IndexIt.Value()): FPakDirectory()))
			, bIncludeDeleted(bInIncludeDeleted)
		{
			AdvanceToValid();
			UpdateCachedFilename();
		}

		FFileIterator& operator++()		
		{ 
			// Continue with the next file
			++DirectoryIt;
			AdvanceToValid();
			UpdateCachedFilename();
			return *this; 
		}

		/** conversion to "bool" returning true if the iterator is valid. */
		FORCEINLINE explicit operator bool() const
		{ 
			return !!IndexIt; 
		}
		/** inverse of the "bool" operator */
		FORCEINLINE bool operator !() const
		{
			return !(bool)*this;
		}

		const FString& Filename() const		{ return CachedFilename; }
		const FPakEntry& Info() const	{ return PakFile.Data->Files[DirectoryIt.Value()]; }
		const int32 GetIndexInPakFile() const { return DirectoryIt.Value(); }

	private:
		void AdvanceToValid()
		{
			SkipDeletedIfRequired();
			while (!DirectoryIt && IndexIt)
			{
				// No more files in the current directory, jump to the next one.
				++IndexIt;
				if (IndexIt)
				{
					// No need to check if there's files in the current directory. If a directory
					// exists in the index it is always non-empty.
					DirectoryIt.~TConstIterator();
					new(&DirectoryIt) FPakDirectory::TConstIterator(IndexIt.Value());
					SkipDeletedIfRequired();
				}
			}
		}

		FORCEINLINE void UpdateCachedFilename()
		{
			if (!!IndexIt && !!DirectoryIt)
			{
				CachedFilename = FString(IndexIt.Key()) + FString(DirectoryIt.Key());
			}
			else
			{
				CachedFilename.Empty();
			}
		}

		FORCEINLINE void SkipDeletedIfRequired()
		{
			if (!bIncludeDeleted)
			{
				while (DirectoryIt && Info().IsDeleteRecord())
				{
					++DirectoryIt;
				}
			}
		}
	};

	/**
	 * Gets this pak file info.
	 *
	 * @return Info about this pak file.
	 */
	const FPakInfo& GetInfo() const
	{
		return Info;
	}

	/**
	 * Gets this pak file's tiemstamp.
	 *
	 * @return Timestamp.
	 */
	const FDateTime& GetTimestamp() const
	{
		return Timestamp;
	}
	
	/**
	 * Returns whether the pak filenames are still resident in memory.
	 *
	 * @return true if filenames are present, false otherwise.
	 */
	bool HasFilenames() const
	{
		return !bFilenamesRemoved;
	}

	/**
	 * Saves memory by hashing the filenames, if possible. After this process,
	 * wildcard scanning of pak entries can no longer be performed. Returns TRUE
	 * if the process successfully unloaded filenames from this pak
	 *
	 * @param CrossPakCollisionChecker A map of hash->fileentry records encountered during filename unloading on other pak files. Used to detect collisions with entries in other pak files.
	 * @param DirectoryRootsToKeep An array of strings in wildcard format that specify whole directory structures of filenames to keep in memory for directory iteration to work.
	 * @param bAllowRetries If a collision is encountered, change the intial seed and try again a fixed number of times before failing
	 */
	bool UnloadPakEntryFilenames(TMap<uint64, FPakEntry>& CrossPakCollisionChecker, TArray<FString>* DirectoryRootsToKeep = nullptr, bool bAllowRetries = true);

	/**
	 * Lower memory usage by bit-encoding the pak file entry information.
	 */
	bool ShrinkPakEntriesMemoryUsage();

	/**
	 * Returns whether the pak files list has been shrunk or not
	 */
	bool HasShrunkPakEntries() const
	{
		return bAttemptedPakEntryShrink;
	}

private:

	/**
	 * Initializes the pak file.
	 */
	void Initialize(FArchive* Reader, bool bLoadIndex = true);

	/**
	 * Loads and initializes pak file index.
	 */
	void LoadIndex(FArchive* Reader);

	/**
	 * Manually add a file to a pak file,
	 */
	void AddSpecialFile(FPakEntry Entry, const FString& Filename);

	/**
	 * Decodes a bit-encoded pak entry.
	 *
	 * @param Filename File to find.
	 * @param OutEntry The optional address of an FPakEntry instance where the found file information should be stored. Pass NULL to only check for file existence.
	 * @return Returns true if the file was found, false otherwise.
	 */
	bool DecodePakEntry(const uint8* SourcePtr, FPakEntry* OutEntry) const
	{
		// Grab the big bitfield value:
		// Bit 31 = Offset 32-bit safe?
		// Bit 30 = Uncompressed size 32-bit safe?
		// Bit 29 = Size 32-bit safe?
		// Bits 28-23 = Compression method
		// Bit 22 = Encrypted
		// Bits 21-6 = Compression blocks count
		// Bits 5-0 = Compression block size
		uint32 Value = *(uint32*)SourcePtr;
		SourcePtr += sizeof(uint32);

		// Filter out the CompressionMethod.
		OutEntry->CompressionMethodIndex = (Value >> 23) & 0x3f;

		// Test for 32-bit safe values. Grab it, or memcpy the 64-bit value
		// to avoid alignment exceptions on platforms requiring 64-bit alignment
		// for 64-bit variables.
		//
		// Read the Offset.
		bool bIsOffset32BitSafe = (Value & (1 << 31)) != 0;
		if (bIsOffset32BitSafe)
		{
			OutEntry->Offset = *(uint32*)SourcePtr;
			SourcePtr += sizeof(uint32);
		}
		else
		{
			FMemory::Memcpy(&OutEntry->Offset, SourcePtr, sizeof(int64));
			SourcePtr += sizeof(int64);
		}

		// Read the UncompressedSize.
		bool bIsUncompressedSize32BitSafe = (Value & (1 << 30)) != 0;
		if (bIsUncompressedSize32BitSafe)
		{
			OutEntry->UncompressedSize = *(uint32*)SourcePtr;
			SourcePtr += sizeof(uint32);
		}
		else
		{
			FMemory::Memcpy(&OutEntry->UncompressedSize, SourcePtr, sizeof(int64));
			SourcePtr += sizeof(int64);
		}

		// Fill in the Size.
		if (OutEntry->CompressionMethodIndex != 0)
		{
			// Size is only present if compression is applied.
			bool bIsSize32BitSafe = (Value & (1 << 29)) != 0;
			if (bIsSize32BitSafe)
			{
				OutEntry->Size = *(uint32*)SourcePtr;
				SourcePtr += sizeof(uint32);
			}
			else
			{
				FMemory::Memcpy(&OutEntry->Size, SourcePtr, sizeof(int64));
				SourcePtr += sizeof(int64);
			}
		}
		else
		{
			// The Size is the same thing as the UncompressedSize when
			// CompressionMethod == COMPRESS_None.
			OutEntry->Size = OutEntry->UncompressedSize;
		}

		// Filter the encrypted flag.
		OutEntry->SetEncrypted((Value & (1 << 22)) != 0);

		// This should clear out any excess CompressionBlocks that may be valid in the user's
		// passed in entry.
		uint32 CompressionBlocksCount = (Value >> 6) & 0xffff;
		OutEntry->CompressionBlocks.Empty(CompressionBlocksCount);
		OutEntry->CompressionBlocks.SetNum(CompressionBlocksCount);

		// Filter the compression block size or use the UncompressedSize if less that 64k.
		OutEntry->CompressionBlockSize = 0;
		if (CompressionBlocksCount > 0)
		{
			OutEntry->CompressionBlockSize = OutEntry->UncompressedSize < 65536 ? (uint32)OutEntry->UncompressedSize : ((Value & 0x3f) << 11);
		}

		// Set Verified to true to avoid have a synchronous open fail comparing FPakEntry structures.
		OutEntry->Verified = true;

		// Set bDeleteRecord to false, because it obviously isn't deleted if we are here.
		OutEntry->SetDeleteRecord(false);

		// Base offset to the compressed data
		int64 BaseOffset = Info.HasRelativeCompressedChunkOffsets() ? 0 : OutEntry->Offset;

		// Handle building of the CompressionBlocks array.
		if (OutEntry->CompressionBlocks.Num() == 1 && !OutEntry->IsEncrypted())
		{
			// If the number of CompressionBlocks is 1, we didn't store any extra information.
			// Derive what we can from the entry's file offset and size.
			FPakCompressedBlock& CompressedBlock = OutEntry->CompressionBlocks[0];
			CompressedBlock.CompressedStart = BaseOffset + OutEntry->GetSerializedSize(Info.Version);
			CompressedBlock.CompressedEnd = CompressedBlock.CompressedStart + OutEntry->Size;
		}
		else if (OutEntry->CompressionBlocks.Num() > 0)
		{
			// Get the right pointer to start copying the CompressionBlocks information from.
			uint32* CompressionBlockSizePtr = (uint32*)SourcePtr;

			// Alignment of the compressed blocks
			uint64 CompressedBlockAlignment = OutEntry->IsEncrypted() ? FAES::AESBlockSize : 1;

			// CompressedBlockOffset is the starting offset. Everything else can be derived from there.
			int64 CompressedBlockOffset = BaseOffset + OutEntry->GetSerializedSize(Info.Version);
			for (int CompressionBlockIndex = 0; CompressionBlockIndex < OutEntry->CompressionBlocks.Num(); ++CompressionBlockIndex)
			{
				FPakCompressedBlock& CompressedBlock = OutEntry->CompressionBlocks[CompressionBlockIndex];
				CompressedBlock.CompressedStart = CompressedBlockOffset;
				CompressedBlock.CompressedEnd = CompressedBlockOffset + *CompressionBlockSizePtr++;
				CompressedBlockOffset += Align(CompressedBlock.CompressedEnd - CompressedBlock.CompressedStart, CompressedBlockAlignment);
			}
		}

		return true;
	}

public:

	/**
	 * Helper function to append '/' at the end of path.
	 *
	 * @param Path - path to convert in place to directory.
	 */
	static void MakeDirectoryFromPath(FString& Path)
	{
		if (Path.Len() > 0 && Path[Path.Len() - 1] != '/')
		{
			Path += TEXT("/");
		}
	}
};

/**
 * Placeholder Class
 */
class PAKFILE_API FPakNoEncryption
{
public:
	enum 
	{
		Alignment = 1,
	};

	static FORCEINLINE int64 AlignReadRequest(int64 Size) 
	{
		return Size;
	}

	static FORCEINLINE void DecryptBlock(void* Data, int64 Size, const FGuid& EncryptionKeyGuid)
	{
		// Nothing needs to be done here
	}
};

/**
 * Typedef for a function that returns an archive to use for accessing an underlying pak file
 */
typedef TFunction<FArchive*()> TAcquirePakReaderFunction;

template< typename EncryptionPolicy = FPakNoEncryption >
class PAKFILE_API FPakReaderPolicy
{
public:
	/** Pak file that own this file data */
	const FPakFile&		PakFile;
	/** Pak file entry for this file. */
	FPakEntry			PakEntry;
	/** Pak file archive to read the data from. */
	TAcquirePakReaderFunction AcquirePakReader;
	/** Offset to the file in pak (including the file header). */
	int64				OffsetToFile;

	FPakReaderPolicy(const FPakFile& InPakFile,const FPakEntry& InPakEntry, TAcquirePakReaderFunction& InAcquirePakReader)
		: PakFile(InPakFile)
		, PakEntry(InPakEntry)
		, AcquirePakReader(InAcquirePakReader)
	{
		OffsetToFile = PakEntry.Offset + PakEntry.GetSerializedSize(PakFile.GetInfo().Version);
	}

	FORCEINLINE int64 FileSize() const 
	{
		return PakEntry.Size;
	}

	void Serialize(int64 DesiredPosition, void* V, int64 Length)
	{
		FGuid EncryptionKeyGuid = PakFile.GetInfo().EncryptionKeyGuid;
		const constexpr int64 Alignment = (int64)EncryptionPolicy::Alignment;
		const constexpr int64 AlignmentMask = ~(Alignment - 1);
		uint8 TempBuffer[Alignment];
		FArchive* PakReader = AcquirePakReader();
		if (EncryptionPolicy::AlignReadRequest(DesiredPosition) != DesiredPosition)
		{
			int64 Start = DesiredPosition & AlignmentMask;
			int64 Offset = DesiredPosition - Start;
			int64 CopySize = FMath::Min(Alignment - Offset, Length);
			PakReader->Seek(OffsetToFile + Start);
			PakReader->Serialize(TempBuffer, Alignment);
			EncryptionPolicy::DecryptBlock(TempBuffer, Alignment, EncryptionKeyGuid);
			FMemory::Memcpy(V, TempBuffer + Offset, CopySize);
			V = (void*)((uint8*)V + CopySize);
			DesiredPosition += CopySize;
			Length -= CopySize;
			check(Length == 0 || DesiredPosition % Alignment == 0);
		}
		else
		{
			PakReader->Seek(OffsetToFile + DesiredPosition);
		}
		
		int64 CopySize = Length & AlignmentMask;
		PakReader->Serialize(V, CopySize);
		EncryptionPolicy::DecryptBlock(V, CopySize, EncryptionKeyGuid);
		Length -= CopySize;
		V = (void*)((uint8*)V + CopySize);

		if (Length > 0)
		{
			PakReader->Serialize(TempBuffer, Alignment);
			EncryptionPolicy::DecryptBlock(TempBuffer, Alignment, EncryptionKeyGuid);
			FMemory::Memcpy(V, TempBuffer, Length);
		}
	}
};

/**
 * File handle to read from pak file.
 */
template< typename ReaderPolicy = FPakReaderPolicy<> >
class PAKFILE_API FPakFileHandle : public IFileHandle
{	
	/** True if PakReader is shared and should not be deleted by this handle. */
	const bool bSharedReader;
	/** Current read position. */
	int64 ReadPos;
	/** Class that controls reading from pak file */
	ReaderPolicy Reader;

public:

	/**
	 * Constructs pak file handle to read from pak.
	 *
	 * @param InFilename Filename
	 * @param InPakEntry Entry in the pak file.
	 * @param InAcquirePakReaderFunction Function that returns the archive to use for serialization. The result of this should not be cached, but reacquired on each serialization operation
	 */
	FPakFileHandle(const FPakFile& InPakFile, const FPakEntry& InPakEntry, TAcquirePakReaderFunction& InAcquirePakReaderFunction, bool bIsSharedReader)
		: bSharedReader(bIsSharedReader)
		, ReadPos(0)
		, Reader(InPakFile, InPakEntry, InAcquirePakReaderFunction)
	{
		INC_DWORD_STAT(STAT_PakFile_NumOpenHandles);
	}

	/**
	 * Constructs pak file handle to read from pak.
	 *
	 * @param InFilename Filename
	 * @param InPakEntry Entry in the pak file.
	 * @param InPakFile Pak file.
	 */
	FPakFileHandle(const FPakFile& InPakFile, const FPakEntry& InPakEntry, FArchive* InPakReader, bool bIsSharedReader)
		: bSharedReader(bIsSharedReader)
		, ReadPos(0)
		, Reader(InPakFile, InPakEntry, [InPakReader]() { return InPakReader; })
	{
		INC_DWORD_STAT(STAT_PakFile_NumOpenHandles);
	}

	/**
	 * Destructor. Cleans up the reader archive if necessary.
	 */
	virtual ~FPakFileHandle()
	{
		if (!bSharedReader)
		{
			delete Reader.AcquirePakReader();
		}

		DEC_DWORD_STAT(STAT_PakFile_NumOpenHandles);
	}

	//~ Begin IFileHandle Interface
	virtual int64 Tell() override
	{
		return ReadPos;
	}
	virtual bool Seek(int64 NewPosition) override
	{
		if (NewPosition > Reader.FileSize() || NewPosition < 0)
		{
			return false;
		}
		ReadPos = NewPosition;
		return true;
	}
	virtual bool SeekFromEnd(int64 NewPositionRelativeToEnd) override
	{
		return Seek(Reader.FileSize() - NewPositionRelativeToEnd);
	}
	virtual bool Read(uint8* Destination, int64 BytesToRead) override
	{
		SCOPE_SECONDS_ACCUMULATOR(STAT_PakFile_Read);

		// Check that the file header is OK
		if (!Reader.PakEntry.Verified)
		{
			FPakEntry FileHeader;
			FArchive* PakReader = Reader.AcquirePakReader();
			PakReader->Seek(Reader.PakEntry.Offset);
			FileHeader.Serialize(*PakReader, Reader.PakFile.GetInfo().Version);
			if (FPakEntry::VerifyPakEntriesMatch(Reader.PakEntry, FileHeader))
			{
				Reader.PakEntry.Verified = true;
			}
			else
			{
				//Header is corrupt, fail the read
				return false;
			}
		}
		//
		if (Reader.FileSize() >= (ReadPos + BytesToRead))
		{
			// Read directly from Pak.
			Reader.Serialize(ReadPos, Destination, BytesToRead);
			ReadPos += BytesToRead;
			return true;
		}
		else
		{
			return false;
		}
	}
	virtual bool Write(const uint8* Source, int64 BytesToWrite) override
	{
		// Writing in pak files is not allowed.
		return false;
	}
	virtual int64 Size() override
	{
		return Reader.FileSize();
	}
	virtual bool Flush(const bool bFullFlush = false) override
	{
		// pak files are read only, so don't need to support flushing
		return false;
	}
	virtual bool Truncate(int64 NewSize) override
	{
		// pak files are read only, so don't need to support truncation
		return false;
	}
	///~ End IFileHandle Interface
};

/**
 * Platform file wrapper to be able to use pak files.
 **/
class PAKFILE_API FPakPlatformFile : public IPlatformFile
{
	struct FPakListEntry
	{
		FPakListEntry()
			: ReadOrder(0)
			, PakFile(nullptr)
		{}

		uint32		ReadOrder;
		FPakFile*	PakFile;

		FORCEINLINE bool operator < (const FPakListEntry& RHS) const
		{
			return ReadOrder > RHS.ReadOrder;
		}
	};

	struct FPakListDeferredEntry
	{
		FString Filename;
		FString Path;
		uint32 ReadOrder;
		FGuid EncryptionKeyGuid;
		int32 PakchunkIndex;
	};
	
	/** Wrapped file */
	IPlatformFile* LowerLevel;
	/** List of all available pak files. */
	TArray<FPakListEntry> PakFiles;
	/** List of all pak filenames with dynamic encryption where we don't have the key yet */
	TArray<FPakListDeferredEntry> PendingEncryptedPakFiles;
	/** True if this we're using signed content. */
	bool bSigned;
	/** Synchronization object for accessing the list of currently mounted pak files. */
	mutable FCriticalSection PakListCritical;
	/** Cache of extensions that we automatically reject if not found in pak file */
	TSet<FName> ExcludedNonPakExtensions;
	/** The extension used for ini files, used for excluding ini files */
	FString IniFileExtension;
	/** The filename for the gameusersettings ini file, used for excluding ini files, but not gameusersettings */
	FString GameUserSettingsIniFilename;

	/**
	 * Gets mounted pak files
	 */
	FORCEINLINE void GetMountedPaks(TArray<FPakListEntry>& Paks)
	{
		FScopeLock ScopedLock(&PakListCritical);
		Paks.Append(PakFiles);
	}

	/**
	 * Checks if a directory exists in one of the available pak files.
	 *
	 * @param Directory Directory to look for.
	 * @return true if the directory exists, false otherwise.
	 */
	bool DirectoryExistsInPakFiles(const TCHAR* Directory)
	{
		FString StandardPath = Directory;
		FPaths::MakeStandardFilename(StandardPath);

		TArray<FPakListEntry> Paks;
		GetMountedPaks(Paks);

		// Check all pak files.
		for (int32 PakIndex = 0; PakIndex < Paks.Num(); PakIndex++)
		{
			if (Paks[PakIndex].PakFile->DirectoryExists(*StandardPath))
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Helper function to copy a file from one handle to another usuing the supplied buffer.
	 *
	 * @param Dest Destination file handle.
	 * @param Source file handle.
	 * @param FileSize size of the source file.
	 * @param Buffer Pointer to the buffer used to copy data.
	 * @param BufferSize Sizeof of the buffer.
	 * @return true if the operation was successfull, false otherwise.
	 */
	bool BufferedCopyFile(IFileHandle& Dest, IFileHandle& Source, const int64 FileSize, uint8* Buffer, const int64 BufferSize) const;

	/**
	 * Creates file handle to read from Pak file.
	 *
	 * @param Filename Filename to create the handle for.
	 * @param PakFile Pak file to read from.
	 * @param FileEntry File entry to create the handle for.
	 * @return Pointer to the new handle.
	 */
	IFileHandle* CreatePakFileHandle(const TCHAR* Filename, FPakFile* PakFile, const FPakEntry* FileEntry);

	/**
	* Hardcode default load ordering of game main pak -> game content -> engine content -> saved dir
	* would be better to make this config but not even the config system is initialized here so we can't do that
	*/
	static int32 GetPakOrderFromPakFilePath(const FString& PakFilePath);

	/**
	 * Handler for device delegate to prompt us to load a new pak.	 
	 */
	bool HandleMountPakDelegate(const FString& PakFilePath, int32 PakOrder, IPlatformFile::FDirectoryVisitor* Visitor);

	/**
	 * Handler for device delegate to prompt us to unload a pak.
	 */
	bool HandleUnmountPakDelegate(const FString& PakFilePath);

	/**
	 * Finds all pak files in the given directory.
	 *
	 * @param Directory Directory to (recursively) look for pak files in
	 * @param OutPakFiles List of pak files
	 */
	static void FindPakFilesInDirectory(IPlatformFile* LowLevelFile, const TCHAR* Directory, const FString& WildCard, TArray<FString>& OutPakFiles);

	/**
	 * Finds all pak files in the known pak folders
	 *
	 * @param OutPakFiles List of all found pak files
	 */
	static void FindAllPakFiles(IPlatformFile* LowLevelFile, const TArray<FString>& PakFolders, const FString& WildCard, TArray<FString>& OutPakFiles);

	/**
	 * When security is enabled, determine if this filename can be looked for in the lower level file system
	 * 
	 * @param InFilename			Filename to check
	 * @param bAllowDirectories		Consider directories as valid filepaths?
	 */
	bool IsNonPakFilenameAllowed(const FString& InFilename);

	/**
	 * Registers a new AES key with the given guid. Triggers the mounting of any pak files that we encountered that use that key
	 *
	 * @param InEncryptionKeyGuid	Guid for this encryption key
	 * @param InKey					Encryption key
	 */
	void RegisterEncryptionKey(const FGuid& InEncryptionKeyGuid, const FAES::FAESKey& InKey);

public:

	//~ For visibility of overloads we don't override
	using IPlatformFile::IterateDirectory;
	using IPlatformFile::IterateDirectoryRecursively;
	using IPlatformFile::IterateDirectoryStat;
	using IPlatformFile::IterateDirectoryStatRecursively;

	/**
	 * Get the unique name for the pak platform file layer
	 */
	static const TCHAR* GetTypeName()
	{
		return TEXT("PakFile");
	}

	/**
	 * Get the wild card pattern used to identify paks to load on startup
	 */
	static const TCHAR* GetMountStartupPaksWildCard();

	/**
	 * Overrides the wildcard used for searching paks. Call before initialization
	 */
	static void SetMountStartupPaksWildCard(const FString& WildCard);

	/**
	* Determine location information for a given pakchunk index. Will be DoesNotExist if the pak file wasn't detected, NotAvailable if it exists but hasn't been mounted due to a missing encryption key, or LocalFast if it exists and has been mounted
	*/
	EChunkLocation::Type GetPakChunkLocation(int32 InPakchunkIndex) const;

	/**
	* Returns true if any of the mounted or pending pak files are chunks (filenames starting pakchunkN)
	*/
	bool AnyChunksAvailable() const;

	/**
	* Get a list of all pak files which have been successfully mounted
	*/
	FORCEINLINE void GetMountedPakFilenames(TArray<FString>& PakFilenames)
	{
		FScopeLock ScopedLock(&PakListCritical);
		PakFilenames.Empty(PakFiles.Num());
		for (FPakListEntry& Entry : PakFiles)
		{
			PakFilenames.Add(Entry.PakFile->GetFilename());
		}
	}

	/**
	 * Checks if pak files exist in any of the known pak file locations.
	 */
	static bool CheckIfPakFilesExist(IPlatformFile* LowLevelFile, const TArray<FString>& PakFolders);

	/**
	 * Gets all pak file locations.
	 */
	static void GetPakFolders(const TCHAR* CmdLine, TArray<FString>& OutPakFolders);

	/**
	* Helper function for accessing pak encryption key
	*/
	static void GetPakEncryptionKey(FAES::FAESKey& OutKey, const FGuid& InEncryptionKeyGuid);

	/**
	* Load a pak signature file. Validates the contents by comparing a SHA hash of the chunk table against and encrypted version that
	* is stored within the file. Returns nullptr if the data is missing or fails the signature check. This function also calls
	* the generic pak signature failure delegates if anything is wrong.
	*/
	static TSharedPtr<const struct FPakSignatureFile, ESPMode::ThreadSafe> GetPakSignatureFile(const TCHAR* InFilename);

	/**
	 * Remove the intenrally cached pointer to the signature file for the specified pak
	 */
	static void RemoveCachedPakSignaturesFile(const TCHAR* InFilename);

	/**
	 * Constructor.
	 * 
	 * @param InLowerLevel Wrapper platform file.
	 */
	FPakPlatformFile();

	/**
	 * Destructor.
	 */
	virtual ~FPakPlatformFile();

	virtual bool ShouldBeUsed(IPlatformFile* Inner, const TCHAR* CmdLine) const override;
	virtual bool Initialize(IPlatformFile* Inner, const TCHAR* CommandLineParam) override;
	virtual void InitializeNewAsyncIO() override;

	void OptimizeMemoryUsageForMountedPaks();

	virtual IPlatformFile* GetLowerLevel() override
	{
		return LowerLevel;
	}
	virtual void SetLowerLevel(IPlatformFile* NewLowerLevel) override
	{
		LowerLevel = NewLowerLevel;
	}

	virtual const TCHAR* GetName() const override
	{
		return FPakPlatformFile::GetTypeName();
	}

	void Tick() override;

	/**
	 * Mounts a pak file at the specified path.
	 *
	 * @param InPakFilename Pak filename.
	 * @param InPath Path to mount the pak at.
	 */
	bool Mount(const TCHAR* InPakFilename, uint32 PakOrder, const TCHAR* InPath = NULL, bool bLoadIndex = true);

	bool Unmount(const TCHAR* InPakFilename);

	int32 MountAllPakFiles(const TArray<FString>& PakFolders);
	int32 MountAllPakFiles(const TArray<FString>& PakFolders, const FString& WildCard);

	/**
	 * Make unique in memory pak files from a list of named files
	 */
	virtual void MakeUniquePakFilesForTheseFiles(TArray<TArray<FString>> InFiles);


	/**
	 * Finds a file in the specified pak files.
	 *
	 * @param Paks Pak files to find the file in.
	 * @param Filename File to find in pak files.
	 * @param OutPakFile Optional pointer to a pak file where the filename was found.
	 * @return Pointer to pak entry if the file was found, NULL otherwise.
	 */
	static bool FindFileInPakFiles(TArray<FPakListEntry>& Paks,const TCHAR* Filename,FPakFile** OutPakFile,FPakEntry* OutEntry = nullptr)
	{
		FString StandardFilename(Filename);
		FPaths::MakeStandardFilename(StandardFilename);

		int32 DeletedReadOrder = -1;

		for (int32 PakIndex = 0; PakIndex < Paks.Num(); PakIndex++)
		{
			int32 PakReadOrder = Paks[PakIndex].ReadOrder;
			if (DeletedReadOrder != -1 && DeletedReadOrder > PakReadOrder)
			{
				//found a delete record in a higher priority patch level, but now we're at a lower priority set - don't search further back or we'll find the original, old file.
				UE_LOG( LogPakFile, Verbose, TEXT("Delete Record: Accepted a delete record for %s"), Filename );
				return false;
			}

			FPakFile::EFindResult FindResult = Paks[PakIndex].PakFile->Find(*StandardFilename, OutEntry);
			if (FindResult == FPakFile::EFindResult::Found )
			{
				if (OutPakFile != NULL)
				{
					*OutPakFile = Paks[PakIndex].PakFile;
				}
				UE_CLOG( DeletedReadOrder != -1, LogPakFile, Verbose, TEXT("Delete Record: Ignored delete record for %s - found it in %s instead (asset was moved between chunks)"), Filename, *Paks[PakIndex].PakFile->GetFilename() );
				return true;
			}
			else if (FindResult == FPakFile::EFindResult::FoundDeleted )
			{
				DeletedReadOrder = PakReadOrder;
				UE_LOG( LogPakFile, Verbose, TEXT("Delete Record: Found a delete record for %s in %s"), Filename, *Paks[PakIndex].PakFile->GetFilename() );
			}
		}

		UE_CLOG( DeletedReadOrder != -1, LogPakFile, Warning, TEXT("Delete Record: No lower priority pak files looking for %s. (maybe not downloaded?)"), Filename );
		return false;
	}

	/**
	 * Finds a file in all available pak files.
	 *
	 * @param Filename File to find in pak files.
	 * @param OutPakFile Optional pointer to a pak file where the filename was found.
	 * @return Pointer to pak entry if the file was found, NULL otherwise.
	 */
	bool FindFileInPakFiles(const TCHAR* Filename, FPakFile** OutPakFile = nullptr, FPakEntry* OutEntry = nullptr)
	{
		TArray<FPakListEntry> Paks;
		GetMountedPaks(Paks);

		return FindFileInPakFiles(Paks, Filename, OutPakFile, OutEntry);
	}

	//~ Begin IPlatformFile Interface
	virtual bool FileExists(const TCHAR* Filename) override
	{
		// Check pak files first.
		if (FindFileInPakFiles(Filename))
		{
			return true;
		}
		// File has not been found in any of the pak files, continue looking in inner platform file.
		bool Result = false;
		if (IsNonPakFilenameAllowed(Filename))
		{
			Result = LowerLevel->FileExists(Filename);
		}
		return Result;
	}

	virtual int64 FileSize(const TCHAR* Filename) override
	{
		// Check pak files first
		FPakEntry FileEntry;
		if (FindFileInPakFiles(Filename, nullptr, &FileEntry))
		{
			return FileEntry.CompressionMethodIndex != 0 ? FileEntry.UncompressedSize : FileEntry.Size;
		}
		// First look for the file in the user dir.
		int64 Result = INDEX_NONE;
		if (IsNonPakFilenameAllowed(Filename))
		{
			Result = LowerLevel->FileSize(Filename);
		}
		return Result;
	}

	virtual bool DeleteFile(const TCHAR* Filename) override
	{
		// If file exists in pak file it will never get deleted.
		if (FindFileInPakFiles(Filename))
		{
			return false;
		}
		// The file does not exist in pak files, try LowerLevel->
		bool Result = false;
		if (IsNonPakFilenameAllowed(Filename))
		{
			Result = LowerLevel->DeleteFile(Filename);
		}
		return Result;
	}

	virtual bool IsReadOnly(const TCHAR* Filename) override
	{
		// Files in pak file are always read-only.
		if (FindFileInPakFiles(Filename))
		{
			return true;
		}
		// The file does not exist in pak files, try LowerLevel->
		bool Result = false;
		if (IsNonPakFilenameAllowed(Filename))
		{
			Result = LowerLevel->IsReadOnly(Filename);
		}
		return Result;
	}

	virtual bool MoveFile(const TCHAR* To, const TCHAR* From) override
	{
		// Files which exist in pak files can't be moved
		if (FindFileInPakFiles(From))
		{
			return false;
		}
		// Files not in pak are allowed to be moved.
		bool Result = false;
		if (IsNonPakFilenameAllowed(From))
		{
			Result = LowerLevel->MoveFile(To, From);
		}
		return Result;
	}

	virtual bool SetReadOnly(const TCHAR* Filename, bool bNewReadOnlyValue) override
	{
		// Files in pak file will never change their read-only flag.
		if (FindFileInPakFiles(Filename))
		{
			// This fails if soemone wants to make files from pak writable.
			return bNewReadOnlyValue;
		}
		// Try lower level
		bool Result = bNewReadOnlyValue;
		if (IsNonPakFilenameAllowed(Filename))
		{
			Result = LowerLevel->SetReadOnly(Filename, bNewReadOnlyValue);
		}
		return Result;
	}

	virtual FDateTime GetTimeStamp(const TCHAR* Filename) override
	{
		// Check pak files first.
		FPakFile* PakFile = NULL;
		if (FindFileInPakFiles(Filename, &PakFile))
		{
			return PakFile->GetTimestamp();
		}
		// Fall back to lower level.
		FDateTime Result = FDateTime::MinValue();
		if (IsNonPakFilenameAllowed(Filename))
		{
			double StartTime = (UE_LOG_ACTIVE(LogPakFile, Verbose)) ? FPlatformTime::Seconds() : 0.0;
			Result = LowerLevel->GetTimeStamp(Filename);
			UE_LOG(LogPakFile, Verbose, TEXT("GetTimeStamp on disk (!!) for %s took %6.2fms."), Filename, float(FPlatformTime::Seconds() - StartTime) * 1000.0f);
		}
		return Result;
	}

	virtual void GetTimeStampPair(const TCHAR* FilenameA, const TCHAR* FilenameB, FDateTime& OutTimeStampA, FDateTime& OutTimeStampB) override
	{
		FPakFile* PakFileA = nullptr;
		FPakFile* PakFileB = nullptr;
		FindFileInPakFiles(FilenameA, &PakFileA);
		FindFileInPakFiles(FilenameB, &PakFileB);

		// If either file exists, we'll assume both should exist here and therefore we can skip the
		// request to the lower level platform file.
		if (PakFileA != nullptr || PakFileB != nullptr)
		{
			OutTimeStampA = PakFileA != nullptr ? PakFileA->GetTimestamp() : FDateTime::MinValue();
			OutTimeStampB = PakFileB != nullptr ? PakFileB->GetTimestamp() : FDateTime::MinValue();
		}
		else
		{
			// Fall back to lower level.
			if (IsNonPakFilenameAllowed(FilenameA) && IsNonPakFilenameAllowed(FilenameB))
			{
				LowerLevel->GetTimeStampPair(FilenameA, FilenameB, OutTimeStampA, OutTimeStampB);
			}
			else
			{
				OutTimeStampA = FDateTime::MinValue();
				OutTimeStampB = FDateTime::MinValue();
			}
		}
	}

	virtual void SetTimeStamp(const TCHAR* Filename, FDateTime DateTime) override
	{
		// No modifications allowed on files from pak (although we could theoretically allow this one).
		if (!FindFileInPakFiles(Filename))
		{
			if (IsNonPakFilenameAllowed(Filename))
			{
				LowerLevel->SetTimeStamp(Filename, DateTime);
			}
		}
	}

	virtual FDateTime GetAccessTimeStamp(const TCHAR* Filename) override
	{
		// AccessTimestamp not yet supported in pak files (although it is possible).
		FPakFile* PakFile = NULL;
		if (FindFileInPakFiles(Filename, &PakFile))
		{
			return PakFile->GetTimestamp();
		}
		// Fall back to lower level.
		FDateTime Result = false;
		if (IsNonPakFilenameAllowed(Filename))
		{
			Result = LowerLevel->GetAccessTimeStamp(Filename);
		}
		return Result;
	}

	virtual FString GetFilenameOnDisk(const TCHAR* Filename) override
	{
		FPakEntry FileEntry;
		FPakFile* PakFile = NULL;
		if (FindFileInPakFiles(Filename, &PakFile, &FileEntry))
		{
			if (!PakFile->HasFilenames())
			{
				//FPlatformMisc::LowLevelOutputDebugString(*(FString("GetFilenameOfDisk() used when bFilenamesRemoved == true: ") + Filename));
			}
			//checkf(PakFile->HasFilenames(), TEXT("GetFilenameOnDisk() can only be used before FPakPlatformFile::UnloadPakEntryFilenames() is called."));

			const FString Path(FPaths::GetPath(Filename));
			const FPakDirectory* PakDirectory = PakFile->FindDirectory(*Path);
			if (PakDirectory != nullptr)
			{
				for (FPakDirectory::TConstIterator DirectoryIt(*PakDirectory); DirectoryIt; ++DirectoryIt)
				{
					if (PakFile->Data->Files[DirectoryIt.Value()].Offset == FileEntry.Offset)
					{
						const FString& RealFilename = DirectoryIt.Key();
						return Path / RealFilename;
					}
				}
			}
		}

		// Fall back to lower level.
		if (IsNonPakFilenameAllowed(Filename))
		{
			return LowerLevel->GetFilenameOnDisk(Filename);
		}
		else
		{
			return Filename;
		}
	}

	virtual IFileHandle* OpenRead(const TCHAR* Filename, bool bAllowWrite = false) override;

	virtual IFileHandle* OpenWrite(const TCHAR* Filename, bool bAppend = false, bool bAllowRead = false) override
	{
		// No modifications allowed on pak files.
		if (FindFileInPakFiles(Filename))
		{
			return nullptr;
		}
		// Use lower level to handle writing.
		return LowerLevel->OpenWrite(Filename, bAppend, bAllowRead);
	}

	virtual bool DirectoryExists(const TCHAR* Directory) override
	{
		// Check pak files first.
		if (DirectoryExistsInPakFiles(Directory))
		{
			return true;
		}
		// Directory does not exist in any of the pak files, continue searching using inner platform file.
		bool Result = LowerLevel->DirectoryExists(Directory); 
		return Result;
	}

	virtual bool CreateDirectory(const TCHAR* Directory) override
	{
		// Directories can be created only under the normal path
		return LowerLevel->CreateDirectory(Directory);
	}

	virtual bool DeleteDirectory(const TCHAR* Directory) override
	{
		// Even if the same directory exists outside of pak files it will never
		// get truely deleted from pak and will still be reported by Iterate functions.
		// Fail in cases like this.
		if (DirectoryExistsInPakFiles(Directory))
		{
			return false;
		}
		// Directory does not exist in pak files so it's safe to delete.
		return LowerLevel->DeleteDirectory(Directory);
	}

	virtual FFileStatData GetStatData(const TCHAR* FilenameOrDirectory) override
	{
		// Check pak files first.
		FPakEntry FileEntry;
		FPakFile* PakFile = nullptr;
		if (FindFileInPakFiles(FilenameOrDirectory, &PakFile, &FileEntry))
		{
			return FFileStatData(
				PakFile->GetTimestamp(),
				PakFile->GetTimestamp(),
				PakFile->GetTimestamp(),
				(FileEntry.CompressionMethodIndex != 0) ? FileEntry.UncompressedSize : FileEntry.Size,
				false,	// IsDirectory
				true	// IsReadOnly
				);
		}

		// Then check pak directories
		if (DirectoryExistsInPakFiles(FilenameOrDirectory))
		{
			return FFileStatData(
				PakFile->GetTimestamp(),
				PakFile->GetTimestamp(),
				PakFile->GetTimestamp(),
				-1,		// FileSize
				true,	// IsDirectory
				true	// IsReadOnly
				);
		}

		// Fall back to lower level.
		FFileStatData FileStatData;
		if (IsNonPakFilenameAllowed(FilenameOrDirectory))
		{
			FileStatData = LowerLevel->GetStatData(FilenameOrDirectory);
		}

		return FileStatData;
	}

	/**
	 * Helper class to filter out files which have already been visited in one of the pak files.
	 */
	class FPakVisitor : public IPlatformFile::FDirectoryVisitor
	{
	public:
		/** Wrapped visitor. */
		FDirectoryVisitor&	Visitor;
		/** Visited pak files. */
		TSet<FString>& VisitedPakFiles;
		/** Cached list of pak files. */
		TArray<FPakListEntry>& Paks;

		/** Constructor. */
		FPakVisitor(FDirectoryVisitor& InVisitor, TArray<FPakListEntry>& InPaks, TSet<FString>& InVisitedPakFiles)
			: Visitor(InVisitor)
			, VisitedPakFiles(InVisitedPakFiles)
			, Paks(InPaks)
		{}
		virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory)
		{
			if (bIsDirectory == false)
			{
				FString StandardFilename(FilenameOrDirectory);
				FPaths::MakeStandardFilename(StandardFilename);

				if (VisitedPakFiles.Contains(StandardFilename))
				{
					// Already visited, continue iterating.
					return true;
				}
				else if (FPakPlatformFile::FindFileInPakFiles(Paks, FilenameOrDirectory, nullptr))
				{
					VisitedPakFiles.Add(StandardFilename);
				}
			}			
			return Visitor.Visit(FilenameOrDirectory, bIsDirectory);
		}
	};

	virtual bool IterateDirectory(const TCHAR* Directory, IPlatformFile::FDirectoryVisitor& Visitor) override
	{
		bool Result = true;
		TSet<FString> FilesVisitedInPak;

		TArray<FPakListEntry> Paks;
		FString StandardDirectory = Directory;
		FPaths::MakeStandardFilename(StandardDirectory);
		
		bool bIsDownloadableDir = (FPaths::HasProjectPersistentDownloadDir() && StandardDirectory.StartsWith(FPaths::ProjectPersistentDownloadDir())) || StandardDirectory.StartsWith(FPaths::CloudDir());

		// don't look for in pak files for target-only locations
		if (!bIsDownloadableDir)
		{
			GetMountedPaks(Paks);
		}

		// Iterate pak files first
		for (int32 PakIndex = 0; PakIndex < Paks.Num(); PakIndex++)
		{
			FPakFile& PakFile = *Paks[PakIndex].PakFile;
			
			const bool bIncludeFiles = true;
			const bool bIncludeFolders = true;
			TSet<FString> FilesVisitedInThisPak;

			PakFile.FindFilesAtPath(FilesVisitedInThisPak, *StandardDirectory, bIncludeFiles, bIncludeFolders);
			for (TSet<FString>::TConstIterator SetIt(FilesVisitedInThisPak); SetIt && Result; ++SetIt)
			{
				const FString& Filename = *SetIt;
				if (!FilesVisitedInPak.Contains(Filename))
				{
					bool bIsDir = Filename.Len() && Filename[Filename.Len() - 1] == '/';
					if (bIsDir)
					{
						Result = Visitor.Visit(*Filename.LeftChop(1), true) && Result;
					}
					else
					{
						Result = Visitor.Visit(*Filename, false) && Result;
					}
					FilesVisitedInPak.Add(Filename);
				}
			}
		}
		if (Result && LowerLevel->DirectoryExists(Directory))
		{
			if (FilesVisitedInPak.Num())
			{
				// Iterate inner filesystem using FPakVisitor
				FPakVisitor PakVisitor(Visitor, Paks, FilesVisitedInPak);
				Result = LowerLevel->IterateDirectory(Directory, PakVisitor);
			}
			else
			{
				 // No point in using FPakVisitor as it will only slow things down.
				Result = LowerLevel->IterateDirectory(Directory, Visitor);
			}
		}
		return Result;
	}

	virtual bool IterateDirectoryRecursively(const TCHAR* Directory, IPlatformFile::FDirectoryVisitor& Visitor) override
	{
		TSet<FString> FilesVisitedInPak;
		TArray<FPakListEntry> Paks;
		GetMountedPaks(Paks);
		FPakVisitor PakVisitor(Visitor, Paks, FilesVisitedInPak);
		return IPlatformFile::IterateDirectoryRecursively(Directory, PakVisitor);
	}

	/**
	 * Helper class to filter out files which have already been visited in one of the pak files.
	 */
	class FPakStatVisitor : public IPlatformFile::FDirectoryStatVisitor
	{
	public:
		/** Wrapped visitor. */
		FDirectoryStatVisitor&	Visitor;
		/** Visited pak files. */
		TSet<FString>& VisitedPakFiles;
		/** Cached list of pak files. */
		TArray<FPakListEntry>& Paks;

		/** Constructor. */
		FPakStatVisitor(FDirectoryStatVisitor& InVisitor, TArray<FPakListEntry>& InPaks, TSet<FString>& InVisitedPakFiles)
			: Visitor(InVisitor)
			, VisitedPakFiles(InVisitedPakFiles)
			, Paks(InPaks)
		{}
		virtual bool Visit(const TCHAR* FilenameOrDirectory, const FFileStatData& StatData)
		{
			if (StatData.bIsDirectory == false)
			{
				FString StandardFilename(FilenameOrDirectory);
				FPaths::MakeStandardFilename(StandardFilename);

				if (VisitedPakFiles.Contains(StandardFilename))
				{
					// Already visited, continue iterating.
					return true;
				}
				else if (FPakPlatformFile::FindFileInPakFiles(Paks, FilenameOrDirectory, nullptr))
				{
					VisitedPakFiles.Add(StandardFilename);
				}
			}			
			return Visitor.Visit(FilenameOrDirectory, StatData);
		}
	};

	virtual bool IterateDirectoryStat(const TCHAR* Directory, IPlatformFile::FDirectoryStatVisitor& Visitor) override
	{
		bool Result = true;
		TSet<FString> FilesVisitedInPak;

		TArray<FPakListEntry> Paks;
 
		FString StandardDirectory = Directory;
		FPaths::MakeStandardFilename(StandardDirectory);

		bool bIsDownloadableDir = (FPaths::HasProjectPersistentDownloadDir() && StandardDirectory.StartsWith(FPaths::ProjectPersistentDownloadDir())) || StandardDirectory.StartsWith(FPaths::CloudDir());

		// don't look for in pak files for target-only locations
		if (!bIsDownloadableDir)
		{
			GetMountedPaks(Paks);
		}

		// Iterate pak files first
		for (int32 PakIndex = 0; PakIndex < Paks.Num(); PakIndex++)
		{
			FPakFile& PakFile = *Paks[PakIndex].PakFile;
			
			const bool bIncludeFiles = true;
			const bool bIncludeFolders = true;
			TSet<FString> FilesVisitedInThisPak;

			PakFile.FindFilesAtPath(FilesVisitedInThisPak, *StandardDirectory, bIncludeFiles, bIncludeFolders);
			for (TSet<FString>::TConstIterator SetIt(FilesVisitedInThisPak); SetIt && Result; ++SetIt)
			{
				const FString& Filename = *SetIt;
				if (!FilesVisitedInPak.Contains(Filename))
				{
					bool bIsDir = Filename.Len() && Filename[Filename.Len() - 1] == '/';

					int64 FileSize = -1;
					if (!bIsDir)
					{
						FPakEntry FileEntry;
						if (FindFileInPakFiles(*Filename, nullptr, &FileEntry))
						{
							FileSize = (FileEntry.CompressionMethodIndex != 0) ? FileEntry.UncompressedSize : FileEntry.Size;
						}
					}

					const FFileStatData StatData(
						PakFile.GetTimestamp(),
						PakFile.GetTimestamp(),
						PakFile.GetTimestamp(),
						FileSize, 
						bIsDir,
						true	// IsReadOnly
						);

					if (bIsDir)
					{
						Result = Visitor.Visit(*Filename.LeftChop(1), StatData) && Result;
					}
					else
					{
						Result = Visitor.Visit(*Filename, StatData) && Result;
					}
					FilesVisitedInPak.Add(Filename);
				}
			}
		}
		if (Result && LowerLevel->DirectoryExists(Directory))
		{
			if (FilesVisitedInPak.Num())
			{
				// Iterate inner filesystem using FPakVisitor
				FPakStatVisitor PakVisitor(Visitor, Paks, FilesVisitedInPak);
				Result = LowerLevel->IterateDirectoryStat(Directory, PakVisitor);
			}
			else
			{
				 // No point in using FPakVisitor as it will only slow things down.
				Result = LowerLevel->IterateDirectoryStat(Directory, Visitor);
			}
		}
		return Result;
	}

	virtual bool IterateDirectoryStatRecursively(const TCHAR* Directory, IPlatformFile::FDirectoryStatVisitor& Visitor) override
	{
		TSet<FString> FilesVisitedInPak;
		TArray<FPakListEntry> Paks;
		GetMountedPaks(Paks);
		FPakStatVisitor PakVisitor(Visitor, Paks, FilesVisitedInPak);
		return IPlatformFile::IterateDirectoryStatRecursively(Directory, PakVisitor);
	}

	virtual void FindFiles(TArray<FString>& FoundFiles, const TCHAR* Directory, const TCHAR* FileExtension) override
	{		
		if (LowerLevel->DirectoryExists(Directory))
		{
			LowerLevel->FindFiles(FoundFiles, Directory, FileExtension);
		}

		bool bRecursive = false;
		FindFilesInternal(FoundFiles, Directory, FileExtension, bRecursive);
	}
	
	virtual void FindFilesRecursively(TArray<FString>& FoundFiles, const TCHAR* Directory, const TCHAR* FileExtension) override
	{
		if (LowerLevel->DirectoryExists(Directory))
		{
			LowerLevel->FindFilesRecursively(FoundFiles, Directory, FileExtension);
		}
		
		bool bRecursive = true;
		FindFilesInternal(FoundFiles, Directory, FileExtension, bRecursive);
	}

	void FindFilesInternal(TArray<FString>& FoundFiles, const TCHAR* Directory, const TCHAR* FileExtension, bool bRecursive)
	{
		TArray<FPakListEntry> Paks;
		GetMountedPaks(Paks);
		if (Paks.Num())
		{
			TSet<FString> FilesVisited;
			FilesVisited.Append(FoundFiles);
			
			FString StandardDirectory = Directory;
			FString FileExtensionStr = FileExtension;
			FPaths::MakeStandardFilename(StandardDirectory);
			bool bIncludeFiles = true;
			bool bIncludeFolders = false;

			TArray<FString> FilesInPak;
			FilesInPak.Reserve(64);
			for (int32 PakIndex = 0; PakIndex < Paks.Num(); PakIndex++)
			{
				FPakFile& PakFile = *Paks[PakIndex].PakFile;
				PakFile.FindFilesAtPath(FilesInPak, *StandardDirectory, bIncludeFiles, bIncludeFolders, bRecursive);
			}
			
			for (const FString& Filename : FilesInPak)
			{
				// filter out files by FileExtension
				if (FileExtensionStr.Len())
				{
					if (!Filename.EndsWith(FileExtensionStr))
					{
						continue;
					}
				}
								
				// make sure we don't add duplicates to FoundFiles
				bool bVisited = false;
				FilesVisited.Add(Filename, &bVisited);
				if (!bVisited)
				{
					FoundFiles.Add(Filename);
				}
			}
		}
	}
	
	virtual bool DeleteDirectoryRecursively(const TCHAR* Directory) override
	{
		// Can't delete directories existing in pak files. See DeleteDirectory(..) for more info.
		if (DirectoryExistsInPakFiles(Directory))
		{
			return false;
		}
		// Directory does not exist in pak files so it's safe to delete.
		return LowerLevel->DeleteDirectoryRecursively(Directory);
	}

	virtual bool CreateDirectoryTree(const TCHAR* Directory) override
	{
		// Directories can only be created only under the normal path
		return LowerLevel->CreateDirectoryTree(Directory);
	}

	virtual bool CopyFile(const TCHAR* To, const TCHAR* From, EPlatformFileRead ReadFlags = EPlatformFileRead::None, EPlatformFileWrite WriteFlags = EPlatformFileWrite::None) override;

	virtual IAsyncReadFileHandle* OpenAsyncRead(const TCHAR* Filename) override;
	virtual void SetAsyncMinimumPriority(EAsyncIOPriorityAndFlags Priority) override;

	virtual IMappedFileHandle* OpenMapped(const TCHAR* Filename) override;
	/**
	 * Converts a filename to a path inside pak file.
	 *
	 * @param Filename Filename to convert.
	 * @param Pak Pak to convert the filename realative to.
	 * @param Relative filename.
	 */
	FString ConvertToPakRelativePath(const TCHAR* Filename, const FPakFile* Pak)
	{
		FString RelativeFilename(Filename);
		return RelativeFilename.Mid(Pak->GetMountPoint().Len());
	}

	FString ConvertToAbsolutePathForExternalAppForRead(const TCHAR* Filename) override
	{
		// Check in Pak file first
		FPakFile* Pak = NULL;
		if (FindFileInPakFiles(Filename, &Pak))
		{
			return FString::Printf(TEXT("Pak: %s/%s"), *Pak->GetFilename(), *ConvertToPakRelativePath(Filename, Pak));
		}
		else
		{
			return LowerLevel->ConvertToAbsolutePathForExternalAppForRead(Filename);
		}
	}

	FString ConvertToAbsolutePathForExternalAppForWrite(const TCHAR* Filename) override
	{
		// Check in Pak file first
		FPakFile* Pak = NULL;
		if (FindFileInPakFiles(Filename, &Pak))
		{
			return FString::Printf(TEXT("Pak: %s/%s"), *Pak->GetFilename(), *ConvertToPakRelativePath(Filename, Pak));
		}
		else
		{
			return LowerLevel->ConvertToAbsolutePathForExternalAppForWrite(Filename);
		}
	}
	//~ End IPlatformFile Interface

	// Access static delegate for loose file security
	static FFilenameSecurityDelegate& GetFilenameSecurityDelegate();

	// Access static delegate for custom encryption
	static FPakCustomEncryptionDelegate& GetPakCustomEncryptionDelegate();

	struct FPakSigningFailureHandlerData
	{
		FCriticalSection Lock;
		FPakChunkSignatureCheckFailedHandler ChunkSignatureCheckFailedDelegate;
		FPakMasterSignatureTableCheckFailureHandler MasterSignatureTableCheckFailedDelegate;
	};

	// Access static delegate for handling a pak signature check failure
	static FPakSigningFailureHandlerData& GetPakSigningFailureHandlerData();
	
	// Access static delegate for handling a pak signature check failure
	UE_DEPRECATED(4.25, "GetPakChunkSignatureCheckFailedHandler is not thread safe, so please migrate to using GetPakSigningFailureHandlerData and locking the critical section around any use of the delegates")
	static FPakChunkSignatureCheckFailedHandler& GetPakChunkSignatureCheckFailedHandler();

	// Access static delegate for handling a pak signature check failure
	UE_DEPRECATED(4.25, "GetPakMasterSignatureTableCheckFailureHandler is not thread safe, so please migrate to using GetPakSigningFailureHandlerData and locking the critical section around any use of the delegates")
	static FPakMasterSignatureTableCheckFailureHandler& GetPakMasterSignatureTableCheckFailureHandler();

	// Broadacast a signature check failure through any registered delegates in a thread safe way
	static void BroadcastPakChunkSignatureCheckFailure(const FPakChunkSignatureCheckFailedData& InData);

	// Broadacast a master signature table failure through any registered delegates in a thread safe way
	static void BroadcastPakMasterSignatureTableCheckFailure(const FString& InFilename);

	// Get a list of which files live in a given chunk
	void GetFilenamesInChunk(const FString& InPakFilename, const TArray<int32>& InChunkIDs, TArray<FString>& OutFileList);
	void GetFilenamesInPakFile(const FString& InPakFilename, TArray<FString>& OutFileList) ;

	void UnloadPakEntryFilenames(TArray<FString>* DirectoryRootsToKeep = nullptr);
	void ShrinkPakEntriesMemoryUsage();

	// BEGIN Console commands
#if !UE_BUILD_SHIPPING
	void HandlePakListCommand(const TCHAR* Cmd, FOutputDevice& Ar);
	void HandleMountCommand(const TCHAR* Cmd, FOutputDevice& Ar);
	void HandleUnmountCommand(const TCHAR* Cmd, FOutputDevice& Ar);
	void HandlePakCorruptCommand(const TCHAR* Cmd, FOutputDevice& Ar);
#endif
	// END Console commands
	
#if PAK_TRACKER
	static TMap<FString, int32> GPakSizeMap;
	static void TrackPak(const TCHAR* Filename, const FPakEntry* PakEntry);
	static TMap<FString, int32>& GetPakMap() { return GPakSizeMap; }
#endif

	// Internal cache of pak signature files
	static TMap<FName, TSharedPtr<const struct FPakSignatureFile, ESPMode::ThreadSafe>> PakSignatureFileCache;
	static FCriticalSection PakSignatureFileCacheLock;
};

/**
 * Structure which describes the content of the pak .sig files
 */
struct FPakSignatureFile
{
	// Magic number that tells us we're dealing with the new format sig files
	static const uint32 Magic = 0x73832DAA;

	enum class EVersion
	{
		Invalid,
		First,

		Last,
		Latest = Last - 1
	};

	// Sig file version. Set to Legacy if the sig file is of an old version
	EVersion Version = EVersion::Latest;

	// RSA encrypted hash
	TArray<uint8> EncryptedHash;

	// SHA1 hash of the chunk CRC data. Only valid after calling DecryptSignatureAndValidate
	FSHAHash DecryptedHash;

	// CRCs of each contiguous 64kb block of the pak file
	TArray<TPakChunkHash> ChunkHashes;
	
	/**
	 * Initialize and hash the CRC list then use the provided private key to encrypt the hash
	 */
	void SetChunkHashesAndSign(const TArray<TPakChunkHash>& InChunkHashes, const FRSAKeyHandle InKey)
	{
		ChunkHashes = InChunkHashes;
		DecryptedHash = ComputeCurrentMasterHash();
		FRSA::EncryptPrivate(TArrayView<const uint8>(DecryptedHash.Hash, UE_ARRAY_COUNT(FSHAHash::Hash)), EncryptedHash, InKey);
	}

	/**
	 * Serialize/deserialize this object to/from an FArchive
	 */
	void Serialize(FArchive& Ar)
	{
		uint32 FileMagic = Magic;
		Ar << FileMagic;

		if (Ar.IsLoading() && FileMagic != Magic)
		{
			Version = EVersion::Invalid;
			EncryptedHash.Empty();
			ChunkHashes.Empty();
			return;
		}

		Ar << Version;
		Ar << EncryptedHash;
		Ar << ChunkHashes;
	}

	/**
	 * Decrypt the chunk CRCs hash and validate that it matches the current one
	 */
	bool DecryptSignatureAndValidate(const FRSAKeyHandle InKey, const FString& InFilename)
	{
		if (Version == EVersion::Invalid)
		{
			UE_LOG(LogPakFile, Warning, TEXT("Pak signature file for '%s' was invalid"), *InFilename);
		}
		else
		{
			TArray<uint8> Decrypted;
			int32 BytesDecrypted = FRSA::DecryptPublic(EncryptedHash, Decrypted, InKey);
			if (BytesDecrypted == UE_ARRAY_COUNT(FSHAHash::Hash))
			{
				FSHAHash CurrentHash = ComputeCurrentMasterHash();
				if (FMemory::Memcmp(Decrypted.GetData(), CurrentHash.Hash, Decrypted.Num()) == 0)
				{
					return true;
				}
				else
				{
					UE_LOG(LogPakFile, Warning, TEXT("Pak signature table validation failed for '%s'! Expected %s, Received %s"), *InFilename, *DecryptedHash.ToString(), *CurrentHash.ToString());
				}
			}
			else
			{
				UE_LOG(LogPakFile, Warning, TEXT("Pak signature table validation failed for '%s'! Failed to decrypt signature"), *InFilename);
			}
		}

		FPakPlatformFile::BroadcastPakMasterSignatureTableCheckFailure(InFilename);
		return false;
	}

	/**
	 * Helper function for computing the SHA1 hash of the current chunk CRC array
	 */
	FSHAHash ComputeCurrentMasterHash() const
	{
		FSHAHash CurrentHash;
		FSHA1::HashBuffer(ChunkHashes.GetData(), ChunkHashes.Num() * sizeof(TPakChunkHash), CurrentHash.Hash);
		return CurrentHash;
	}
};
