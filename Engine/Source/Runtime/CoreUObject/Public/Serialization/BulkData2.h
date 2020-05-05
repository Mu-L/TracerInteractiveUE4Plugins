// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BulkDataBuffer.h"
#include "Async/AsyncFileHandle.h"
#include "IO/IoDispatcher.h"

struct FOwnedBulkDataPtr;
class IMappedFileHandle;
class IMappedFileRegion;

/**
 * Represents an IO request from the BulkData streaming API.
 *
 * It functions pretty much the same as IAsyncReadRequest expect that it also holds
 * the file handle as well.
 */
class COREUOBJECT_API IBulkDataIORequest
{
public:
	virtual ~IBulkDataIORequest() {}

	virtual bool PollCompletion() const = 0;
	virtual bool WaitCompletion(float TimeLimitSeconds = 0.0f) const = 0;

	virtual uint8* GetReadResults() = 0;
	virtual int64 GetSize() const = 0;

	virtual void Cancel() = 0;
};

using FileToken = int32;
struct FBulkDataOrId
{
	union
	{
		// Inline data or fallback path
		struct
		{
			uint64 BulkDataSize;
			FileToken Token;

		} Fallback;

		// For IODispatcher
		FIoChunkId ChunkID;
	}; // Note that the union will end up being 16 bytes with padding
};
DECLARE_INTRINSIC_TYPE_LAYOUT(FBulkDataOrId);

/**
 * This is a wrapper for the BulkData memory allocation so we can use a single pointer to either
 * reference a straight memory allocation or in the case that the BulkData object represents a 
 * memory mapped file region, a FOwnedBulkDataPtr.
 * This makes the code more complex but it means that we do not pay any additional memory cost when
 * memory mapping isn't being used at a small cpu cost. However the number of BulkData object usually
 * means that the memory saving is worth it compared to how infrequently the memory accessors are 
 * actually called.
 *
 * Note: We use a flag set in the owning BulkData object to tell us what storage type we are using 
 * so all accessors require that a pointer to the parent object be passed in.
 */
class FBulkDataBase;
class FBulkDataAllocation
{
public:
	// Misc
	bool IsLoaded() const { return Allocation != nullptr; }
	void Free(FBulkDataBase* Owner);

	// Set as a raw buffer
	void* AllocateData(FBulkDataBase* Owner, SIZE_T SizeInBytes); //DataBuffer = FMemory::Realloc(DataBuffer, SizeInBytes, DEFAULT_ALIGNMENT);
	void SetData(FBulkDataBase* Owner, void* Buffer);

	// Set as memory mapped
	void SetMemoryMappedData(FBulkDataBase* Owner, IMappedFileHandle* MappedHandle, IMappedFileRegion* MappedRegion);

	// Getters		
	void* GetAllocationForWrite(const FBulkDataBase* Owner) const;
	const void* GetAllocationReadOnly(const FBulkDataBase* Owner) const;

	FOwnedBulkDataPtr* StealFileMapping(FBulkDataBase* Owner);
	void Swap(FBulkDataBase* Owner, void** DstBuffer);
private:
	void* Allocation = nullptr; // Will either be the data allocation or a FOwnedBulkDataPtr if memory mapped
};
DECLARE_INTRINSIC_TYPE_LAYOUT(FBulkDataAllocation);

/**
 * Callback to use when making streaming requests
 */
typedef TFunction<void(bool bWasCancelled, IBulkDataIORequest*)> FBulkDataIORequestCallBack;

/**
 * @documentation @todo documentation
 */
class COREUOBJECT_API FBulkDataBase
{
	DECLARE_TYPE_LAYOUT(FBulkDataBase, NonVirtual);

public:
	using BulkDataRangeArray = TArray<FBulkDataBase*, TInlineAllocator<8>>;

	static void				SetIoDispatcher(FIoDispatcher* InIoDispatcher) { IoDispatcher = InIoDispatcher; }
	static FIoDispatcher* GetIoDispatcher() { return IoDispatcher; }
public:
	static constexpr FileToken InvalidToken = INDEX_NONE;

	FBulkDataBase(const FBulkDataBase& Other) { *this = Other; }
	FBulkDataBase(FBulkDataBase&& Other);
	FBulkDataBase& operator=(const FBulkDataBase& Other);

	FBulkDataBase()
	{ 
		Data.Fallback.BulkDataSize = 0;
		Data.Fallback.Token = InvalidToken;
	}
	~FBulkDataBase();

protected:

	void Serialize(FArchive& Ar, UObject* Owner, int32 Index, bool bAttemptFileMapping, int32 ElementSize);

public:
	// Unimplemented:
	void* Lock(uint32 LockFlags);
	const void* LockReadOnly() const;
	void Unlock();
	bool IsLocked() const;

	void* Realloc(int64 SizeInBytes);

	/**
	 * Retrieves a copy of the bulk data.
	 *
	 * @param Dest [in/out] Pointer to pointer going to hold copy, can point to nullptr in which case memory is allocated
	 * @param bDiscardInternalCopy Whether to discard/ free the potentially internally allocated copy of the data
	 */
	void GetCopy(void** Dest, bool bDiscardInternalCopy);

	int64 GetBulkDataSize() const;

	void SetBulkDataFlags(uint32 BulkDataFlagsToSet);
	void ResetBulkDataFlags(uint32 BulkDataFlagsToSet);
	void ClearBulkDataFlags(uint32 BulkDataFlagsToClear);
	uint32 GetBulkDataFlags() const { return BulkDataFlags; }

	bool CanLoadFromDisk() const;

	/**
	 * Returns true if the data references a file that currently exists and can be referenced by the file system.
	 */
	bool DoesExist() const;

	bool IsStoredCompressedOnDisk() const;
	FName GetDecompressionFormat() const;

	bool IsBulkDataLoaded() const { return DataAllocation.IsLoaded(); }

	// TODO: The flag tests could be inline if we fixed the header dependency issues (the flags are defined in Bulkdata.h at the moment)
	bool IsAvailableForUse() const;
	bool IsDuplicateNonOptional() const;
	bool IsOptional() const;
	bool IsInlined() const;
	UE_DEPRECATED(4.25, "Use ::IsInSeparateFile() instead")
	FORCEINLINE bool InSeperateFile() const { return IsInSeparateFile(); }
	bool IsInSeparateFile() const;
	bool IsSingleUse() const;
	bool IsMemoryMapped() const;
	bool IsDataMemoryMapped() const;
	bool IsUsingIODispatcher() const;

	IAsyncReadFileHandle* OpenAsyncReadHandle() const;

	IBulkDataIORequest* CreateStreamingRequest(EAsyncIOPriorityAndFlags Priority, FBulkDataIORequestCallBack* CompleteCallback, uint8* UserSuppliedMemory) const;
	IBulkDataIORequest* CreateStreamingRequest(int64 OffsetInBulkData, int64 BytesToRead, EAsyncIOPriorityAndFlags Priority, FBulkDataIORequestCallBack* CompleteCallback, uint8* UserSuppliedMemory) const;

	static IBulkDataIORequest* CreateStreamingRequestForRange(const BulkDataRangeArray& RangeArray, EAsyncIOPriorityAndFlags Priority, FBulkDataIORequestCallBack* CompleteCallback);

	void RemoveBulkData();

	bool IsAsyncLoadingComplete() const { return true; }

	// Added for compatibility with the older BulkData system
	int64 GetBulkDataOffsetInFile() const;
	FString GetFilename() const;

public:
	// The following methods are for compatibility with SoundWave.cpp which assumes memory mapping.
	void ForceBulkDataResident(); // Is closer to MakeSureBulkDataIsLoaded in the old system but kept the name due to existing use
	FOwnedBulkDataPtr* StealFileMapping();

private:
	friend FBulkDataAllocation;

	void SetRuntimeBulkDataFlags(uint32 BulkDataFlagsToSet);
	void ClearRuntimeBulkDataFlags(uint32 BulkDataFlagsToClear);

	/**
	 * Poll to see if it is safe to discard the data owned by the Bulkdata object
	 *
	 * @return True if we are allowed to discard the existing data in the Bulkdata object.
	 */
	bool CanDiscardInternalData() const;

	void LoadDataDirectly(void** DstBuffer);

	void ProcessDuplicateData(FArchive& Ar, const UPackage* Package, const FString* Filename, int64& InOutSizeOnDisk, int64& InOutOffsetInFile);
	void SerializeDuplicateData(FArchive& Ar, uint32& OutBulkDataFlags, int64& OutBulkDataSizeOnDisk, int64& OutBulkDataOffsetInFile);
	void SerializeBulkData(FArchive& Ar, void* DstBuffer, int64 DataLength);

	bool MemoryMapBulkData(const FString& Filename, int64 OffsetInBulkData, int64 BytesToRead);

	// Methods for dealing with the allocated data
	FORCEINLINE void* AllocateData(SIZE_T SizeInBytes) { return DataAllocation.AllocateData(this, SizeInBytes); }
	FORCEINLINE void  FreeData() { DataAllocation.Free(this); }
	FORCEINLINE void* GetDataBufferForWrite() const { return DataAllocation.GetAllocationForWrite(this); }
	FORCEINLINE const void* GetDataBufferReadOnly() const { return DataAllocation.GetAllocationReadOnly(this); }

	FString ConvertFilenameFromFlags(const FString& Filename) const;

private:

	static FIoDispatcher* IoDispatcher;

	LAYOUT_FIELD(FBulkDataOrId, Data);
	LAYOUT_FIELD(FBulkDataAllocation, DataAllocation);
	LAYOUT_FIELD_INITIALIZED(uint32, BulkDataFlags, 0);
	LAYOUT_MUTABLE_FIELD_INITIALIZED(uint8, LockStatus, 0); // Mutable so that the read only lock can be const
};

/**
 * @documentation @todo documentation
 */
template<typename ElementType>
class COREUOBJECT_API FUntypedBulkData2 : public FBulkDataBase
{
	// In the older Bulkdata system the data was being loaded as if it was POD with the option to opt out
	// but nothing actually opted out. This check should help catch if any non-POD data was actually being
	// used or not.
	static_assert(TIsPODType<ElementType>::Value, "FUntypedBulkData2 is limited to POD types!");
public:
	FORCEINLINE FUntypedBulkData2() {}

	void Serialize(FArchive& Ar, UObject* Owner, int32 Index, bool bAttemptFileMapping)
	{
		FBulkDataBase::Serialize(Ar, Owner, Index, bAttemptFileMapping, sizeof(ElementType));
	}
	
	// @TODO: The following two ::Serialize methods are a work around for the default parameters in the old 
	// BulkData api that are not used anywhere and to avoid causing code compilation issues for licensee code.
	// At some point in the future we should remove Index and bAttemptFileMapping from both the old and new 
	// BulkData api implementations of ::Serialize and then use UE_DEPRECATE to update existing code properly.
	FORCEINLINE void Serialize(FArchive& Ar, UObject* Owner)
	{	
		Serialize(Ar, Owner, INDEX_NONE, false);
	}

	// @TODO: See above
	FORCEINLINE void Serialize(FArchive& Ar, UObject* Owner, int32 Index)
	{
		Serialize(Ar, Owner, Index, false);
	}

	/**
	 * Returns the number of elements held by the BulkData object.
	 *
	 * @return Number of elements.
	 */
	int64 GetElementCount() const 
	{ 
		return GetBulkDataSize() / GetElementSize(); 
	}

	/**
	 * Returns size in bytes of single element.
	 *
	 * @return The size of the element.
	 */
	int32 GetElementSize() const
	{ 
		return sizeof(ElementType); 
	}

	ElementType* Lock(uint32 LockFlags)
	{
		return (ElementType*)FBulkDataBase::Lock(LockFlags);
	}

	const ElementType* LockReadOnly() const
	{
		return (const ElementType*)FBulkDataBase::LockReadOnly();
	}

	ElementType* Realloc(int64 InElementCount)
	{
		return (ElementType*)FBulkDataBase::Realloc(InElementCount * sizeof(ElementType));
	}

	/**
	 * Returns a copy encapsulated by a FBulkDataBuffer.
	 *
	 * @param RequestedElementCount If set to greater than 0, the returned FBulkDataBuffer will be limited to
	 * this number of elements. This will give an error if larger than the actual number of elements in the BulkData object.
	 * @param bDiscardInternalCopy If true then the BulkData object will free it's internal buffer once called.
	 *
	 * @return A FBulkDataBuffer that owns a copy of the BulkData, this might be a subset of the data depending on the value of RequestedSize.
	 */
	FORCEINLINE FBulkDataBuffer<ElementType> GetCopyAsBuffer(int64 RequestedElementCount, bool bDiscardInternalCopy)
	{
		const int64 MaxElementCount = GetElementCount();

		check(RequestedElementCount <= MaxElementCount);

		ElementType* Ptr = nullptr;
		GetCopy((void**)& Ptr, bDiscardInternalCopy);

		const int64 BufferSize = (RequestedElementCount > 0 ? RequestedElementCount : MaxElementCount);

		return FBulkDataBuffer<ElementType>(Ptr, BufferSize);
	}
};

// Commonly used types
using FByteBulkData2 = FUntypedBulkData2<uint8>;
using FWordBulkData2 = FUntypedBulkData2<uint16>;
using FIntBulkData2 = FUntypedBulkData2<int32>;
using FFloatBulkData2 = FUntypedBulkData2<float>;
