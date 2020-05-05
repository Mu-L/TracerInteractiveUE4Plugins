// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/LowLevelMemTracker.h"

#if ENABLE_LOW_LEVEL_MEM_TRACKER

#define LLM_PAGE_SIZE (16*1024)

// POD types only
template<typename T>
class FLLMArray
{
public:
	FLLMArray()
		: Array(StaticArray)
		, Count(0)
		, Capacity(StaticArrayCapacity)
		, Allocator(NULL)
	{
	}

	~FLLMArray()
	{
		Clear(true);
	}

	void SetAllocator(FLLMAllocator* InAllocator)
	{
		Allocator = InAllocator;
	}

	uint32 Num() const
	{
		return Count;
	}

	void Clear(bool ReleaseMemory = false)
	{
		if (ReleaseMemory)
		{
			if (Array != StaticArray)
			{
				Allocator->Free(Array, Capacity * sizeof(T));
				Array = StaticArray;
			}
			Capacity = StaticArrayCapacity;
		}
		Count = 0;
	}

	void Add(const T& Item)
	{
		if (Count == Capacity)
		{
			uint32 NewCapacity = DefaultCapacity;
			if (Capacity)
			{
				NewCapacity = Capacity + (Capacity / 2);
				ensureMsgf(NewCapacity > Capacity, TEXT("Unsigned integer overflow."));
			}
			Reserve(NewCapacity);
		}

		Array[Count] = Item;
		++Count;
	}

	T RemoveLast()
	{
		LLMCheck(Count > 0);
		--Count;
		T Last = Array[Count];

		return Last;
	}

	T& operator[](uint32 Index)
	{
		LLMCheck(Index >= 0 && Index < Count);
		return Array[Index];
	}

	const T& operator[](uint32 Index) const
	{
		LLMCheck(Index >= 0 && Index < Count);
		return Array[Index];
	}

	T& GetLast()
	{
		LLMCheck(Count > 0);
		return Array[Count - 1];
	}

	void Reserve(uint32 NewCapacity)
	{
		if (NewCapacity == Capacity)
		{
			return;
		}

		if (NewCapacity <= StaticArrayCapacity)
		{
			if (Capacity > StaticArrayCapacity)
			{
				if (Count)
				{
					memcpy(StaticArray, Array, Count * sizeof(T));
					Allocator->Free(Array, Capacity * sizeof(T));
				}

				Array = StaticArray;
				Capacity = StaticArrayCapacity;
			}
		}
		else
		{
			NewCapacity = AlignArbitrary(NewCapacity, ItemsPerPage);

			T* NewArray = (T*)Allocator->Alloc(NewCapacity * sizeof(T));

			if (Count)
			{
				memcpy(NewArray, Array, Count * sizeof(T));
				if (Array != StaticArray)
				{
					Allocator->Free(Array, Capacity * sizeof(T));
				}
			}

			Array = NewArray;
			Capacity = NewCapacity;
		}
	}

	void operator=(const FLLMArray<T>& Other)
	{
		Clear();
		Reserve(Other.Count);
		memcpy(Array, Other.Array, Other.Count * sizeof(T));
		Count = Other.Count;
	}

	void Trim()
	{
		// Trim if usage has dropped below 3/4 of the total capacity
		if (Array != StaticArray && Count < (Capacity - (Capacity / 4)))
		{
			Reserve(Count);
		} 
	}

private:
	T* Array;
	uint32 Count;
	uint32 Capacity;

	FLLMAllocator* Allocator;

	static const int StaticArrayCapacity = 64;	// because the default size is so large this actually saves memory
	T StaticArray[StaticArrayCapacity];

	static const int ItemsPerPage = LLM_PAGE_SIZE / sizeof(T);
	static const int DefaultCapacity = ItemsPerPage;
};

// calls constructor/destructor
template<class T>
class FLLMObjectAllocator
{
	struct Block
	{
		Block* Next;
	};

public:
	FLLMObjectAllocator()
		: BlockList(NULL)
		, FreeList(NULL)
	{
	}

	~FLLMObjectAllocator()
	{
		Clear();
	}

	void Clear()
	{
		Block* BlockIter = BlockList;
		while (BlockIter)
		{
			Block* Next = BlockIter->Next;
			Allocator->Free(BlockIter, BlockSize);
			BlockIter = Next;
		}

		BlockList = NULL;
		FreeList = NULL;
	}

	T* New()
	{
		T* Item = FreeList;

		if (!Item)
		{
			AllocNewFreeList();
			Item = FreeList;
		}

		FreeList = *(T**)Item;
		new (Item)T();
		return Item;
	}

	void Delete(T* Item)
	{
		Item->~T();
		*(T**)Item = FreeList;
		FreeList = Item;
	}

	void SetAllocator(FLLMAllocator* InAllocator)
	{
		Allocator = InAllocator;
	}

private:
	void AllocNewFreeList()
	{
		Block* NewBlock = (Block*)Allocator->Alloc(BlockSize);
		NewBlock->Next = BlockList;
		BlockList = NewBlock;

		int32 FirstOffset = sizeof(Block);
		int ItemCount = (BlockSize - FirstOffset) / sizeof(T);
		FreeList = (T*)((char*)NewBlock + FirstOffset);
		T* Item = FreeList;
		for (int i = 0; i < ItemCount - 1; ++i, ++Item)
		{
			*(T**)Item = Item + 1;
		}
		*(T**)Item = NULL;
	}

private:
	static const int BlockSize = LLM_PAGE_SIZE;
	Block* BlockList;
	T* FreeList;

	FLLMAllocator* Allocator;
};

/*
* hash map
*/
template<typename TKey, typename TValue1, typename TValue2>
class LLMMap
{
public:
	struct Values
	{
		TValue1 Value1;
		TValue2 Value2;
	};

	LLMMap()
		: Allocator(NULL)
		, Map(NULL)
		, Count(0)
		, Capacity(0)
#ifdef PROFILE_LLMMAP
		, IterAcc(0)
		, IterCount(0)
#endif
	{
	}

	~LLMMap()
	{
		Clear();
	}

	void SetAllocator(FLLMAllocator* InAllocator, uint32 InDefaultCapacity = DefaultCapacity)
	{
		FScopeLock Lock(&CriticalSection);

		Allocator = InAllocator;

		Keys.SetAllocator(Allocator);
		KeyHashes.SetAllocator(Allocator);
		Values1.SetAllocator(Allocator);
		Values2.SetAllocator(Allocator);
		FreeKeyIndices.SetAllocator(Allocator);

		Reserve(InDefaultCapacity);
	}

	void Clear()
	{
		FScopeLock Lock(&CriticalSection);

		Keys.Clear(true);
		KeyHashes.Clear(true);
		Values1.Clear(true);
		Values2.Clear(true);
		FreeKeyIndices.Clear(true);

		Allocator->Free(Map, Capacity * sizeof(uint32));
		Map = NULL;
		Count = 0;
		Capacity = 0;
	}

	// Add a value to this set.
	// If this set already contains the value does nothing.
	void Add(const TKey& Key, const TValue1& Value1, const TValue2& Value2)
	{
		LLMCheck(Map);

		uint32 KeyHash = Key.GetHashCode();

		FScopeLock Lock(&CriticalSection);

		uint32 MapIndex = GetMapIndex(Key, KeyHash);
		uint32 KeyIndex = Map[MapIndex];

		if (KeyIndex != InvalidIndex)
		{
			static bool ShownWarning = false;
			if (!ShownWarning)
			{
				FPlatformMisc::LowLevelOutputDebugString(TEXT("LLM WARNING: Replacing allocation in tracking map. Alloc/Free Mismatch.\n"));
				ShownWarning = true;
			}

			Values1[KeyIndex] = Value1;
			Values2[KeyIndex] = Value2;
		}
		else
		{
			if (Count == (Margin * Capacity) / 256U)
			{
				Grow();
				MapIndex = GetMapIndex(Key, KeyHash);
			}

			if (FreeKeyIndices.Num())
			{
				uint32 FreeIndex = FreeKeyIndices.RemoveLast();
				Map[MapIndex] = FreeIndex;
				Keys[FreeIndex] = Key;
				KeyHashes[FreeIndex] = KeyHash;
				Values1[FreeIndex] = Value1;
				Values2[FreeIndex] = Value2;
			}
			else
			{
				Map[MapIndex] = Keys.Num();
				Keys.Add(Key);
				KeyHashes.Add(KeyHash);
				Values1.Add(Value1);
				Values2.Add(Value2);
			}

			++Count;
		}
	}

	Values GetValue(const TKey& Key)
	{
		uint32 KeyHash = Key.GetHashCode();

		FScopeLock Lock(&CriticalSection);

		uint32 MapIndex = GetMapIndex(Key, KeyHash);

		uint32 KeyIndex = Map[MapIndex];
		LLMCheck(KeyIndex != InvalidIndex);

		Values RetValues;
		RetValues.Value1 = Values1[KeyIndex];
		RetValues.Value2 = Values2[KeyIndex];

		return RetValues;
	}

	Values Remove(const TKey& Key)
	{
		uint32 KeyHash = Key.GetHashCode();

		LLMCheck(Map);

		FScopeLock Lock(&CriticalSection);

		uint32 MapIndex = GetMapIndex(Key, KeyHash);
		if (!LLMEnsure(IsItemInUse(MapIndex)))
		{
			return Values();
		}

		uint32 KeyIndex = Map[MapIndex];

		Values RetValues;
		RetValues.Value1 = Values1[KeyIndex];
		RetValues.Value2 = Values2[KeyIndex];

		if (KeyIndex == Keys.Num() - 1)
		{
			Keys.RemoveLast();
			KeyHashes.RemoveLast();
			Values1.RemoveLast();
			Values2.RemoveLast();
		}
		else
		{
			FreeKeyIndices.Add(KeyIndex);
		}

		// find first index in this array
		uint32 IndexIter = MapIndex;
		uint32 FirstIndex = MapIndex;
		if (!IndexIter)
		{
			IndexIter = Capacity;
		}
		--IndexIter;
		while (IsItemInUse(IndexIter))
		{
			FirstIndex = IndexIter;
			if (!IndexIter)
			{
				IndexIter = Capacity;
			}
			--IndexIter;
		}

		bool Found = false;
		for (;;)
		{
			// find the last item in the array that can replace the item being removed
			uint32 IndexIter2 = (MapIndex + 1) & (Capacity - 1);

			uint32 SwapIndex = InvalidIndex;
			while (IsItemInUse(IndexIter2))
			{
				uint32 SearchKeyIndex = Map[IndexIter2];
				const uint32 SearchHashCode = KeyHashes[SearchKeyIndex];
				const uint32 SearchInsertIndex = SearchHashCode & (Capacity - 1);

				if (InRange(SearchInsertIndex, FirstIndex, MapIndex))
				{
					SwapIndex = IndexIter2;
					Found = true;
				}

				IndexIter2 = (IndexIter2 + 1) & (Capacity - 1);
			}

			// swap the item
			if (Found)
			{
				Map[MapIndex] = Map[SwapIndex];
				MapIndex = SwapIndex;
				Found = false;
			}
			else
			{
				break;
			}
		}

		// remove the last item
		Map[MapIndex] = InvalidIndex;

		--Count;

		return RetValues;
	}

	uint32 Num() const
	{
		FScopeLock Lock(&CriticalSection);

		return Count;
	}

	bool HasKey(const TKey& Key)
	{
		uint32 KeyHash = Key.GetHashCode();

		FScopeLock Lock(&CriticalSection);

		uint32 MapIndex = GetMapIndex(Key, KeyHash);
		return IsItemInUse(MapIndex);
	}

	void Trim()
	{
		FScopeLock Lock(&CriticalSection);

		Keys.Trim();
		KeyHashes.Trim();
		Values1.Trim();
		Values2.Trim();
		FreeKeyIndices.Trim();
	}

private:
	void Reserve(uint32 NewCapacity)
	{
		NewCapacity = GetNextPow2(NewCapacity);

		// keep a copy of the old map
		uint32* OldMap = Map;
		uint32 OldCapacity = Capacity;

		// allocate the new table
		Capacity = NewCapacity;
		Map = (uint32*)Allocator->Alloc(NewCapacity * sizeof(uint32));

		for (uint32 Index = 0; Index < NewCapacity; ++Index)
			Map[Index] = InvalidIndex;

		// copy the values from the old to the new table
		uint32* OldItem = OldMap;
		for (uint32 Index = 0; Index < OldCapacity; ++Index, ++OldItem)
		{
			uint32 KeyIndex = *OldItem;

			if (KeyIndex != InvalidIndex)
			{
				uint32 MapIndex = GetMapIndex(Keys[KeyIndex], KeyHashes[KeyIndex]);
				Map[MapIndex] = KeyIndex;
			}
		}

		Allocator->Free(OldMap, OldCapacity * sizeof(uint32));
	}

	static uint32 GetNextPow2(uint32 value)
	{
		uint32 p = 2;
		while (p < value)
			p *= 2;
		return p;
	}

	bool IsItemInUse(uint32 MapIndex) const
	{
		return Map[MapIndex] != InvalidIndex;
	}

	uint32 GetMapIndex(const TKey& Key, uint32 Hash) const
	{
		int32 Mask = Capacity - 1;
		int32 MapIndex = Hash & Mask;
		int32 KeyIndex = Map[MapIndex];

		while (KeyIndex != InvalidIndex && !(Keys[KeyIndex] == Key))
		{
			MapIndex = (MapIndex + 1) & Mask;
			KeyIndex = Map[MapIndex];
#ifdef PROFILE_LLMMAP
			++IterAcc;
#endif
		}

#ifdef PROFILE_LLMMAP
		++IterCount;
		double Average = IterAcc / (double)IterCount;
		if (Average > 2.0)
		{
			static double LastWriteTime = 0.0;
			double Now = FPlatformTime::Seconds();
			if (Now - LastWriteTime > 5)
			{
				LastWriteTime = Now;
				UE_LOG(LogStats, Log, TEXT("WARNING: LLMMap average: %f\n"), (float)Average);
			}
		}
#endif
		return MapIndex;
	}

	// Increase the capacity of the map
	void Grow()
	{
		uint32 NewCapacity = Capacity ? 2 * Capacity : DefaultCapacity;
		Reserve(NewCapacity);
	}

	static bool InRange(
		const uint32 Index,
		const uint32 StartIndex,
		const uint32 EndIndex)
	{
		return (StartIndex <= EndIndex) ?
			Index >= StartIndex && Index <= EndIndex :
			Index >= StartIndex || Index <= EndIndex;
	}

	// data
private:
	enum { DefaultCapacity = 1024 * 1024 };
	enum { InvalidIndex = -1 };
	static const uint32 Margin = (30 * 256) / 100;

	FCriticalSection CriticalSection;

	FLLMAllocator* Allocator;

	uint32* Map;
	uint32 Count;
	uint32 Capacity;

	// all these arrays must be kept in sync and are accessed by MapIndex
	FLLMArray<TKey> Keys;
	FLLMArray<uint32> KeyHashes;
	FLLMArray<TValue1> Values1;
	FLLMArray<TValue2> Values2;

	FLLMArray<int> FreeKeyIndices;

#ifdef PROFILE_LLMMAP
	mutable int64 IterAcc;
	mutable int64 IterCount;
#endif
};

// Pointer key for hash map
struct PointerKey
{
	PointerKey() : Pointer(NULL) {}
	PointerKey(const void* InPointer) : Pointer(InPointer) {}
	uint32 GetHashCode() const
	{
		// 64 bit to 32 bit Hash
		uint64 Key = (uint64)Pointer;
		Key = (~Key) + (Key << 18);
		Key = Key ^ (Key >> 31);
		Key = Key * 21;
		Key = Key ^ (Key >> 11);
		Key = Key + (Key << 6);
		Key = Key ^ (Key >> 22);
		return (unsigned int)Key;
	}
	bool operator==(const PointerKey& other) const { return Pointer == other.Pointer; }
	const void* Pointer;
};

#endif		// #if ENABLE_LOW_LEVEL_MEM_TRACKER

