// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/SortedMap.h"
#include "UObject/WeakObjectPtr.h"
#include "Async/Future.h"

/**
 * Flags serialized with the bulk data.
 */
enum EBulkDataFlags
{
	/** Empty flag set.																*/
	BULKDATA_None								= 0,
	/** If set, payload is stored at the end of the file and not inline				*/
	BULKDATA_PayloadAtEndOfFile					= 1<<0,
	/** If set, payload should be [un]compressed using ZLIB during serialization.	*/
	BULKDATA_SerializeCompressedZLIB			= 1<<1,
	/** Force usage of SerializeElement over bulk serialization.					*/
	BULKDATA_ForceSingleElementSerialization	= 1<<2,
	/** Bulk data is only used once at runtime in the game.							*/
	BULKDATA_SingleUse							= 1<<3,
	/** Bulk data won't be used and doesn't need to be loaded						*/
	BULKDATA_Unused								= 1<<5,
	/** Forces the payload to be saved inline, regardless of its size				*/
	BULKDATA_ForceInlinePayload					= 1<<6,
	/** Flag to check if either compression mode is specified						*/
	BULKDATA_SerializeCompressed				= (BULKDATA_SerializeCompressedZLIB),
	/** Forces the payload to be always streamed, regardless of its size */
	BULKDATA_ForceStreamPayload					= 1 << 7,
	/** If set, payload is stored in a .upack file alongside the uasset				*/
	BULKDATA_PayloadInSeperateFile				= 1 << 8,
	/** DEPRECATED: If set, payload is compressed using platform specific bit window	*/
	BULKDATA_SerializeCompressedBitWindow		= 1 << 9,
	/** There is a new default to inline unless you opt out */
	BULKDATA_Force_NOT_InlinePayload			= 1 << 10,
	/** This payload is optional and may not be on device */
	BULKDATA_OptionalPayload					= 1 << 11,
	/** This payload will be memory mapped, this requires alignment, no compression etc. */
	BULKDATA_MemoryMappedPayload = 1 << 12,
	/** Bulk data size is 64 bits long */
	BULKDATA_Size64Bit							= 1 << 13
};

/**
 * Enumeration for bulk data lock status.
 */
enum EBulkDataLockStatus
{
	/** Unlocked array													*/
	LOCKSTATUS_Unlocked							= 0,
	/** Locked read-only												*/
	LOCKSTATUS_ReadOnlyLock						= 1,
	/** Locked read-write-realloc										*/
	LOCKSTATUS_ReadWriteLock					= 2,
};

/**
 * Enumeration for bulk data lock behavior
 */
enum EBulkDataLockFlags
{
	LOCK_READ_ONLY								= 1,
	LOCK_READ_WRITE								= 2,
};

class IMappedFileHandle;
class IMappedFileRegion;

/*-----------------------------------------------------------------------------
	Base version of untyped bulk data.
-----------------------------------------------------------------------------*/

/**
 * @documentation @todo documentation
 */
struct COREUOBJECT_API FOwnedBulkDataPtr
{

	FOwnedBulkDataPtr(void* InAllocatedData)
		: AllocatedData(InAllocatedData)
		, MappedHandle(nullptr)
		, MappedRegion(nullptr)
	{
		
	}

	FOwnedBulkDataPtr(IMappedFileHandle* Handle, IMappedFileRegion* Region)
		: AllocatedData(nullptr)
		, MappedHandle(Handle)
		, MappedRegion(Region)
	{
		
	}

	~FOwnedBulkDataPtr();
	const void* GetPointer();

	IMappedFileHandle* GetMappedHandle()
	{
		return MappedHandle;
	}
	IMappedFileRegion* GetMappedRegion()
	{
		return MappedRegion;
	}

	void RelinquishOwnership()
	{
		AllocatedData = nullptr;
		MappedHandle = nullptr;
		MappedRegion = nullptr;
	}

private:
	// hidden
	FOwnedBulkDataPtr() {}


	// if allocated memory was used, this will be non-null
	void* AllocatedData;
	
	// if memory mapped IO was used, these will be non-null
	IMappedFileHandle* MappedHandle;
	IMappedFileRegion* MappedRegion;
};

/**
 * @documentation @todo documentation
 */
struct COREUOBJECT_API FUntypedBulkData
{
private:
	// This struct represents an optional allocation.
	struct FAllocatedPtr
	{
		FAllocatedPtr()
			: Ptr       (nullptr)
			, MappedHandle(nullptr)
			, MappedRegion(nullptr)
			, bAllocated(false)
		{
		}

		FAllocatedPtr(FAllocatedPtr&& Other)
			: Ptr       (Other.Ptr)
			, MappedHandle(Other.MappedHandle)
			, MappedRegion(Other.MappedRegion)
			, bAllocated(Other.bAllocated)
		{
			Other.Ptr = nullptr;
			Other.MappedHandle = nullptr;
			Other.MappedRegion = nullptr;
			Other.bAllocated = false;
		}

		FAllocatedPtr& operator=(FAllocatedPtr&& Other)
		{
			Swap(*this, Other);
			Other.Deallocate();

			return *this;
		}

		~FAllocatedPtr()
		{
			Deallocate();
		}

		void* Get() const
		{
			return Ptr;
		}

		FORCEINLINE explicit operator bool() const
		{
			return bAllocated;
		}

		void Reallocate(int64 Count, int32 Alignment = DEFAULT_ALIGNMENT)
		{
			check(!MappedHandle && !MappedRegion); // not legal for mapped bulk data
			if (Count)
			{
				Ptr = FMemory::Realloc(Ptr, Count, Alignment);
			}
			else
			{
				FMemory::Free(Ptr);
				Ptr = nullptr;
			}

			bAllocated = true;
		}

		void* ReleaseWithoutDeallocating()
		{
			if (MappedHandle || MappedRegion)
			{
				// Super scary, we returned a pointer to a mapped file but we have no guarantees that this outlives the pointer we let out into the engine
				// @todo transfer ownership properly by making FAllocatedPtr a public thing that can be used by people to take ownership of the entire mapping. 
			}
			void* Result = Ptr;
			Ptr = nullptr;
			bAllocated = false;
			return Result;
		}

		void Deallocate()
		{
			if (MappedHandle || MappedRegion)
			{
				UnmapFile();
			}
			FMemory::Free(Ptr);
			Ptr = nullptr;
			bAllocated = false;
		}

		COREUOBJECT_API bool MapFile(const TCHAR *Filename, int64 Offset, int64 Size);
		COREUOBJECT_API void UnmapFile();

		FOwnedBulkDataPtr* StealFileMapping()
		{
			FOwnedBulkDataPtr* Result;
			// make the proper kind of owner pointer info
			if (MappedHandle && MappedRegion && Ptr && bAllocated)
			{
				Result = new FOwnedBulkDataPtr(MappedHandle, MappedRegion);
			}
			else
			{
				Result = new FOwnedBulkDataPtr(Ptr);
			}

			// no matter what, this allocated pointer is now fully owned by the caller, so we just null everything out, no deletions
			MappedHandle = nullptr;
			MappedRegion = nullptr;
			Ptr = nullptr;
			bAllocated = false;

			return Result;
		}
		
	private:
		FAllocatedPtr(const FAllocatedPtr&);
		FAllocatedPtr& operator=(const FAllocatedPtr&);

		void* Ptr;
		IMappedFileHandle* MappedHandle;
		IMappedFileRegion* MappedRegion;
		bool  bAllocated;
	};

public:
	friend class FLinkerLoad;

	/*-----------------------------------------------------------------------------
		Constructors and operators
	-----------------------------------------------------------------------------*/

	/**
	 * Constructor, initializing all member variables.
	 */
	FUntypedBulkData();

	/**
	 * Copy constructor. Use the common routine to perform the copy.
	 *
	 * @param Other the source array to copy
	 */
	FUntypedBulkData( const FUntypedBulkData& Other );

	/**
	 * Virtual destructor, free'ing allocated memory.
	 */
	virtual ~FUntypedBulkData();

	/**
	 * Copies the source array into this one after detaching from archive.
	 *
	 * @param Other the source array to copy
	 */
	FUntypedBulkData& operator=( const FUntypedBulkData& Other );

	/*-----------------------------------------------------------------------------
		Static functions.
	-----------------------------------------------------------------------------*/

	/**
	 * Dumps detailed information of bulk data usage.
	 *
	 * @param Log FOutputDevice to use for logging
	 */
	static void DumpBulkDataUsage( FOutputDevice& Log );

	/*-----------------------------------------------------------------------------
		Accessors
	-----------------------------------------------------------------------------*/

	/**
	 * Returns the number of elements in this bulk data array.
	 *
	 * @return Number of elements in this bulk data array
	 */
	int64 GetElementCount() const;
	/**
	 * Returns size in bytes of single element.
	 *
	 * Pure virtual that needs to be overloaded in derived classes.
	 *
	 * @return Size in bytes of single element
	 */
	virtual int32 GetElementSize() const = 0;
	/**
	 * Returns the size of the bulk data in bytes.
	 *
	 * @return Size of the bulk data in bytes
	 */
	int64 GetBulkDataSize() const;
	/**
	 * Returns the size of the bulk data on disk. This can differ from GetBulkDataSize if
	 * BULKDATA_SerializeCompressed is set.
	 *
	 * @return Size of the bulk data on disk or INDEX_NONE in case there's no association
	 */
	int64 GetBulkDataSizeOnDisk() const;
	/**
	 * Returns the offset into the file the bulk data is located at.
	 *
	 * @return Offset into the file or INDEX_NONE in case there is no association
	 */
	int64 GetBulkDataOffsetInFile() const;
	/**
	 * Returns whether the bulk data is stored compressed on disk.
	 *
	 * @return true if data is compressed on disk, false otherwise
	 */
	bool IsStoredCompressedOnDisk() const;

	/**
	 * Returns true if the data can be loaded from disk.
	 */
	bool CanLoadFromDisk() const;

	/**
	 * Returns flags usable to decompress the bulk data
	 * 
	 * @return NAME_None if the data was not compressed on disk, or valid format to pass to FCompression::UncompressMemory for this data
	 */
	FName GetDecompressionFormat() const;

	/**
	 * Returns whether the bulk data is currently loaded and resident in memory.
	 *
	 * @return true if bulk data is loaded, false otherwise
	 */
	bool IsBulkDataLoaded() const;

	/**
	* Returns whether the bulk data asynchronous load has completed.
	*
	* @return true if bulk data has been loaded or async loading was not used to load this data, false otherwise
	*/
	bool IsAsyncLoadingComplete() const;

	/**
	* Returns whether this bulk data is used
	* @return true if BULKDATA_Unused is not set
	*/
	bool IsAvailableForUse() const;

	/**
	 * Sets the passed in bulk data flags.
	 *
	 * @param BulkDataFlagsToSet	Bulk data flags to set
	 */
	void SetBulkDataFlags( uint32 BulkDataFlagsToSet );

	/**
	* Gets the current bulk data flags.
	*
	* @return Bulk data flags currently set
	*/
	uint32 GetBulkDataFlags() const;

	/**
	 * Sets the passed in bulk data alignment.
	 *
	 * @param BulkDataAlignmentToSet	Bulk data alignment to set
	 */
	void SetBulkDataAlignment( uint32 BulkDataAlignmentToSet );

	/**
	* Gets the current bulk data alignment.
	*
	* @return Bulk data alignment currently set
	*/
	uint32 GetBulkDataAlignment() const;

	/**
	 * Clears the passed in bulk data flags.
	 *
	 * @param BulkDataFlagsToClear	Bulk data flags to clear
	 */
	void ClearBulkDataFlags( uint32 BulkDataFlagsToClear );

	/** 
	 * Returns the filename this bulkdata resides in
	 *
	 * @return Filename where this bulkdata can be loaded from
	 **/
	const FString& GetFilename() const { return Filename; }

	/*-----------------------------------------------------------------------------
		Data retrieval and manipulation.
	-----------------------------------------------------------------------------*/

	/**
	 * Retrieves a copy of the bulk data.
	 *
	 * @param Dest [in/out] Pointer to pointer going to hold copy, can point to NULL pointer in which case memory is allocated
	 * @param bDiscardInternalCopy Whether to discard/ free the potentially internally allocated copy of the data
	 */
	void GetCopy( void** Dest, bool bDiscardInternalCopy = true );

	/**
	 * Locks the bulk data and returns a pointer to it.
	 *
	 * @param	LockFlags	Flags determining lock behavior
	 */
	void* Lock( uint32 LockFlags );

	/**
	 * Locks the bulk data and returns a read-only pointer to it.
	 * This variant can be called on a const bulkdata
	 */
	const void* LockReadOnly() const;

	/**
	 * Change size of locked bulk data. Only valid if locked via read-write lock.
	 *
	 * @param InElementCount	Number of elements array should be resized to
	 */
	void* Realloc( int64 InElementCount );

	/** 
	 * Unlocks bulk data after which point the pointer returned by Lock no longer is valid.
	 */
	void Unlock() const;

	/** 
	 * Checks if this bulk is locked
	 */
	bool IsLocked() const { return LockStatus != LOCKSTATUS_Unlocked; }

	/**
	 * Clears/ removes the bulk data and resets element count to 0.
	 */
	void RemoveBulkData();

	/**
	 * Load the bulk data using a file reader. Works even when no archive is attached to the bulk data..
  	 * @return Whether the operation succeeded.
	 */
	bool LoadBulkDataWithFileReader();

	/**
	 * Forces the bulk data to be resident in memory and detaches the archive.
	 */
	void ForceBulkDataResident();
	
	/**
	 * Sets whether we should store the data compressed on disk.
	 *
	 * @param CompressionFlags	Flags to use for compressing the data. Use COMPRESS_NONE for no compression, or something like COMPRESS_ZLIB to compress the data
	 */
	UE_DEPRECATED(4.21, "Use the FName version of StoreCompressedOnDisk")
	void StoreCompressedOnDisk( ECompressionFlags CompressionFlags );
	void StoreCompressedOnDisk( FName CompressionFormat );

	/**
	 * Deallocates bulk data without detaching the archive, so that further bulk data accesses require a reload.
	 * Only supported in editor builds.
	 *
	 * @return Whether the operation succeeded.
	 */
	bool UnloadBulkData();

	/*-----------------------------------------------------------------------------
		Serialization.
	-----------------------------------------------------------------------------*/

	/**
	 * Serialize function used to serialize this bulk data structure.
	 *
	 * @param Ar	Archive to serialize with
	 * @param Owner	Object owning the bulk data
	 * @param Idx	Index of bulk data item being serialized
	 * @param bAttemptFileMapping	If true, attempt to map this instead of loading it into malloc'ed memory
	 */
	void Serialize( FArchive& Ar, UObject* Owner, int32 Idx=INDEX_NONE, bool bAttemptFileMapping = false);

	FOwnedBulkDataPtr* StealFileMapping()
	{
		// @todo if non-mapped bulk data, do we need to detach this, or mimic GetCopy more than we do?
		return BulkData.StealFileMapping();
	}

	/**
	 * Serialize just the bulk data portion to/ from the passed in memory.
	 *
	 * @param	Ar					Archive to serialize with
	 * @param	Data				Memory to serialize either to or from
	 */
	void SerializeBulkData( FArchive& Ar, void* Data );

	
	/*-----------------------------------------------------------------------------
		Class specific virtuals.
	-----------------------------------------------------------------------------*/

protected:

	/**
	* Serializes all elements, a single element at a time, allowing backward compatible serialization
	* and endian swapping to be performed.
	*
	* @param Ar			Archive to serialize with
	* @param Data			Base pointer to data
	*/
	virtual void SerializeElements(FArchive& Ar, void* Data);

	/**
	 * Serializes a single element at a time, allowing backward compatible serialization
	 * and endian swapping to be performed. Needs to be overloaded by derived classes.
	 *
	 * @param Ar			Archive to serialize with
	 * @param Data			Base pointer to data
	 * @param ElementIndex	Index of element to serialize
	 */
	virtual void SerializeElement( FArchive& Ar, void* Data, int64 ElementIndex ) = 0;

	/**
	 * Returns whether single element serialization is required given an archive. This e.g.
	 * can be the case if the serialization for an element changes and the single element
	 * serialization code handles backward compatibility.
	 */
	virtual bool RequiresSingleElementSerialization( FArchive& Ar );
		
private:
#if WITH_EDITOR
	/**
	 * Detaches the bulk data from the passed in archive. Needs to match the archive we are currently
	 * attached to.
	 *
	 * @param Ar						Archive to detach from
	 * @param bEnsureBulkDataIsLoaded	whether to ensure that bulk data is load before detaching from archive
	 */
	void DetachFromArchive( FArchive* Ar, bool bEnsureBulkDataIsLoaded );
#endif // WITH_EDITOR

	/*-----------------------------------------------------------------------------
		Internal helpers
	-----------------------------------------------------------------------------*/

	/**
	 * Copies bulk data from passed in structure.
	 *
	 * @param	Other	Bulk data object to copy from.
	 */
	void Copy( const FUntypedBulkData& Other );

	/**
	 * Helper function initializing all member variables.
	 */
	void InitializeMemberVariables();

	/**
	 * Loads the bulk data if it is not already loaded.
	 */
	void MakeSureBulkDataIsLoaded();

	/**
	 * Loads the data from disk into the specified memory block. This requires us still being attached to an
	 * archive we can use for serialization.
	 *
	 * @param Dest Memory to serialize data into
	 */
	void LoadDataIntoMemory( void* Dest );

	/** Create the async load task */
	void AsyncLoadBulkData();

	/** Starts serializing bulk data asynchronously */
	void StartSerializingBulkData(FArchive& Ar, UObject* Owner, int32 Idx, bool bPayloadInline);

	/** Flushes any pending async load of bulk data  and copies the data to Dest buffer*/
	bool FlushAsyncLoading();

	/** Waits until pending async load finishes */
	void WaitForAsyncLoading();

	/** Resets async loading state */
	void ResetAsyncData();
	
	/** Returns true if bulk data should be loaded asynchronously */
	bool ShouldStreamBulkData();

	/*-----------------------------------------------------------------------------
		Member variables.
	-----------------------------------------------------------------------------*/

	/** Serialized flags for bulk data																					*/
	uint32					BulkDataFlags;
	/** Alignment of bulk data																							*/
	uint16					BulkDataAlignment;
	/** Current lock status																								*/
	uint16					LockStatus;
	/** Number of elements in bulk data array																			*/
	int64					ElementCount;
	/** Offset of bulk data into file or INDEX_NONE if no association													*/
	int64					BulkDataOffsetInFile;
	/** Size of bulk data on disk or INDEX_NONE if no association														*/
	int64					BulkDataSizeOnDisk;

	/** Pointer to cached bulk data																						*/
	FAllocatedPtr		BulkData;
	/** Pointer to cached async bulk data																				*/
	FAllocatedPtr		BulkDataAsync;
	/** Async helper for loading bulk data on a separate thread */
	TFuture<bool> SerializeFuture;

protected:
	/** name of the package file containing the bulkdata */
	FString				Filename;
#if WITH_EDITOR
	/** Archive associated with bulk data for serialization																*/
	FArchive*			AttachedAr;
	/** Used to make sure the linker doesn't get garbage collected at runtime for things with attached archives			*/
	FLinkerLoad*		Linker;
#else
	/** weak pointer to the linker this bulk data originally belonged to. */
	TWeakObjectPtr<UPackage> Package;
#endif // WITH_EDITOR
};


/*-----------------------------------------------------------------------------
	uint8 version of bulk data.
-----------------------------------------------------------------------------*/

struct COREUOBJECT_API FByteBulkData : public FUntypedBulkData
{
	/**
	 * Returns size in bytes of single element.
	 *
	 * @return Size in bytes of single element
	 */
	virtual int32 GetElementSize() const;

	/**
	 * Serializes an element at a time allowing and dealing with endian conversion and backward compatiblity.
	 * 
	 * @param Ar			Archive to serialize with
	 * @param Data			Base pointer to data
	 * @param ElementIndex	Element index to serialize
	 */
	virtual void SerializeElement( FArchive& Ar, void* Data, int64 ElementIndex );
};

/*-----------------------------------------------------------------------------
	WORD version of bulk data.
-----------------------------------------------------------------------------*/

struct COREUOBJECT_API FWordBulkData : public FUntypedBulkData
{
	/**
	 * Returns size in bytes of single element.
	 *
	 * @return Size in bytes of single element
	 */
	virtual int32 GetElementSize() const;

	/**
	 * Serializes an element at a time allowing and dealing with endian conversion and backward compatiblity.
	 * 
	 * @param Ar			Archive to serialize with
	 * @param Data			Base pointer to data
	 * @param ElementIndex	Element index to serialize
	 */
	virtual void SerializeElement( FArchive& Ar, void* Data, int64 ElementIndex );
};

/*-----------------------------------------------------------------------------
	int32 version of bulk data.
-----------------------------------------------------------------------------*/

struct COREUOBJECT_API FIntBulkData : public FUntypedBulkData
{
	/**
	 * Returns size in bytes of single element.
	 *
	 * @return Size in bytes of single element
	 */
	virtual int32 GetElementSize() const;

	/**
	 * Serializes an element at a time allowing and dealing with endian conversion and backward compatiblity.
	 * 
	 * @param Ar			Archive to serialize with
	 * @param Data			Base pointer to data
	 * @param ElementIndex	Element index to serialize
	 */
	virtual void SerializeElement( FArchive& Ar, void* Data, int64 ElementIndex );
};

/*-----------------------------------------------------------------------------
	float version of bulk data.
-----------------------------------------------------------------------------*/

struct COREUOBJECT_API FFloatBulkData : public FUntypedBulkData
{
	/**
	 * Returns size in bytes of single element.
	 *
	 * @return Size in bytes of single element
	 */
	virtual int32 GetElementSize() const;

	/**
	 * Serializes an element at a time allowing and dealing with endian conversion and backward compatiblity.
	 * 
	 * @param Ar			Archive to serialize with
	 * @param Data			Base pointer to data
	 * @param ElementIndex	Element index to serialize
	 */
	virtual void SerializeElement( FArchive& Ar, void* Data, int64 ElementIndex );
};

class FFormatContainer
{
	friend class UBodySetup;

	TSortedMap<FName, FByteBulkData*, FDefaultAllocator, FNameFastLess> Formats;
	uint32 Alignment;
public:
	~FFormatContainer()
	{
		FlushData();
	}
	bool Contains(FName Format) const
	{
		return Formats.Contains(Format);
	}
	FByteBulkData& GetFormat(FName Format)
	{
		FByteBulkData* Result = Formats.FindRef(Format);
		if (!Result)
		{
			Result = new FByteBulkData;
			Formats.Add(Format, Result);
		}
		return *Result;
	}
	void FlushData()
	{
		for (const TPair<FName, FByteBulkData*>& Format : Formats)
		{
			delete Format.Value;
		}
		Formats.Empty();
	}
	COREUOBJECT_API void Serialize(FArchive& Ar, UObject* Owner, const TArray<FName>* FormatsToSave = nullptr, bool bSingleUse = true, uint32 InAlignment = DEFAULT_ALIGNMENT, bool bInline = true, bool bMapped = false);
	COREUOBJECT_API void SerializeAttemptMappedLoad(FArchive& Ar, UObject* Owner);
};

