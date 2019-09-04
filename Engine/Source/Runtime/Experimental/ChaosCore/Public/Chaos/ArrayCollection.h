// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/ArrayCollectionArrayBase.h"

namespace Chaos
{
class TArrayCollection
{
public:
	TArrayCollection()
	    : MSize(0) {}
	TArrayCollection(const TArrayCollection& Other) = delete;
	TArrayCollection(TArrayCollection&& Other) = delete;
	~TArrayCollection() {}

	int32 AddArray(TArrayCollectionArrayBase* Array)
	{
		int32 Index = MArrays.Find(nullptr);
		if(Index == INDEX_NONE)
		{
			Index = MArrays.Num();
			MArrays.Add(Array);
		}
		else
		{
			MArrays[Index] = Array;
		}
		MArrays[Index]->Resize(MSize);
		return Index;
	}

	void RemoveArray(TArrayCollectionArrayBase* Array)
	{
		const int32 Idx = MArrays.Find(Array);
		if(Idx != INDEX_NONE)
		{
			MArrays[Idx] = nullptr;
		}
	}

	uint32 Size() const 
	{ return MSize; }

	uint64 ComputeColumnSize() const
	{
		uint64 Size = 0;
		for (TArrayCollectionArrayBase* Array : MArrays)
		{
			if (Array)
			{
				Size += Array->SizeOfElem();
			}
		}

		return Size;
	}

protected:
	void AddElementsHelper(const int32 Num)
	{
		if(Num == 0)
		{
			return;
		}
		ResizeHelper(MSize + Num);
	}

	void ResizeHelper(const int32 Num)
	{
		check(Num >= 0);
		MSize = Num;
		for (TArrayCollectionArrayBase* Array : MArrays)
		{
			if(Array)
			{
				Array->Resize(Num);
			}
		}
	}

	void RemoveAtHelper(const int32 Index, const int32 Count)
	{
		for (TArrayCollectionArrayBase* Array : MArrays)
		{
			if (Array)
			{
				Array->RemoveAt(Index, Count);
			}
		}
		MSize = FMath::Min(static_cast<int32>(MSize) - Index, Count);
	}

private:
	TArray<TArrayCollectionArrayBase*> MArrays;

protected:
	uint32 MSize;
};
}
