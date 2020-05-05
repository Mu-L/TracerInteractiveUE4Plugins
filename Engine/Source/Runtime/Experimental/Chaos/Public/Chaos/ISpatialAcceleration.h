// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Chaos/Vector.h"
#include "Chaos/Box.h"
#include "GeometryParticlesfwd.h"
#include "ChaosCheck.h"


namespace Chaos
{

struct CHAOS_API FQueryFastData
{
	FQueryFastData(const FVec3& InDir, const FReal InLength)
		: Dir(InDir)
		, InvDir( (InDir[0] == 0) ? 0 : 1 / Dir[0], (InDir[1] == 0) ? 0 : 1 / Dir[1], (InDir[2] == 0) ? 0 : 1 / Dir[2])
		, bParallel{ InDir[0] == 0, InDir[1] == 0, InDir[2] == 0 }
	{
		CHAOS_ENSURE(InLength);
		SetLength(InLength);
	}

	const FVec3& Dir;
	const FVec3 InvDir;

	FReal CurrentLength;
	FReal InvCurrentLength;

	const bool bParallel[3];

#ifdef _MSC_VER
	#pragma warning (push)
	#pragma warning(disable:4723)
#endif
	//compiler complaining about divide by 0, but we are guarding against it.
	//Seems like it's trying to evaluate div independent of the guard?

	void SetLength(const FReal InLength)
	{
		CurrentLength = InLength;

		if(InLength)
		{
			InvCurrentLength = 1 / InLength;
		}
	}

#ifdef _MSC_VER
	#pragma warning(pop)
#endif


protected:
	FQueryFastData(const FVec3& InDummyDir)
		: Dir(InDummyDir)
		, InvDir()
		, bParallel{}
	{}
};

//dummy struct for templatized paths
struct CHAOS_API FQueryFastDataVoid : public FQueryFastData
{
	FQueryFastDataVoid() : FQueryFastData(DummyDir) {}
	
	FVec3 DummyDir;
};

template <typename T, int d>
class TAABB;

template <typename T, int d>
class TGeometryParticle;

template <typename T, int d>
class TSpatialRay
{
public:
	TSpatialRay()
		: Start((T)0)
		, End((T)0)
	{}

	TSpatialRay(const TVector<T, d>& InStart, const TVector<T, d> InEnd)
		: Start(InStart)
		, End(InEnd)
	{}

	TVector<T, d> Start;
	TVector<T, d> End;
};

/** A struct which is passed to spatial acceleration visitors whenever there are potential hits.
	In production, this class will only contain a payload.
*/
template <typename TPayloadType>
struct CHAOS_API TSpatialVisitorData
{
	TPayloadType Payload;
	TSpatialVisitorData(const TPayloadType& InPayload, const bool bInHasBounds = false, const TAABB<float, 3>& InBounds = TAABB<float, 3>::ZeroAABB())
		: Payload(InPayload)
#if !(UE_BUILD_TEST || UE_BUILD_SHIPPING)
		, bHasBounds(bInHasBounds)
		, Bounds(InBounds)
	{ }
	bool bHasBounds;
	TAABB<float, 3> Bounds;
#else
	{ }
#endif
};

/** Visitor base class used to iterate through spatial acceleration structures.
	This class is responsible for gathering any information it wants (for example narrow phase query results).
	This class determines whether the acceleration structure should continue to iterate through potential instances
*/
template <typename TPayloadType, typename T>
class CHAOS_API ISpatialVisitor
{
public:
	virtual ~ISpatialVisitor() = default;

	/** Called whenever an instance in the acceleration structure may overlap
		@Instance - the instance we are potentially overlapping
		Returns true to continue iterating through the acceleration structure
	*/
	virtual bool Overlap(const TSpatialVisitorData<TPayloadType>& Instance) = 0;

	/** Called whenever an instance in the acceleration structure may intersect with a raycast
		@Instance - the instance we are potentially intersecting with a raycast
		@CurData - the current query data. Call SetLength to update the length all future intersection tests will use. A blocking intersection should update this
		Returns true to continue iterating through the acceleration structure
	*/
	virtual bool Raycast(const TSpatialVisitorData<TPayloadType>& Instance, FQueryFastData& CurData) = 0;

	/** Called whenever an instance in the acceleration structure may intersect with a sweep
		@Instance - the instance we are potentially intersecting with a sweep
		@CurLength - the length all future intersection tests will use. A blocking intersection should update this
		Returns true to continue iterating through the acceleration structure
	*/
	virtual bool Sweep(const TSpatialVisitorData<TPayloadType>& Instance, FQueryFastData& CurData) = 0;

	virtual const void* GetQueryData() const { return nullptr; }
};

/**
 * Can be implemented by external, non-chaos systems to collect / render
 * debug information from spacial structures. When passed to the debug
 * methods on ISpatialAcceleration the methods will be called out by
 * the spacial structure if implemented for the external system to handle
 * the actual drawing.
 */
template <typename T>
class ISpacialDebugDrawInterface
{
public:
	
	virtual ~ISpacialDebugDrawInterface() = default;

	virtual void Box(const TAABB<T, 3>& InBox, const TVector<T, 3>& InLinearColor, float InThickness) = 0;
	virtual void Line(const TVector<T, 3>& InBegin, const TVector<T, 3>& InEnd, const TVector<T, 3>& InLinearColor, float InThickness)  = 0;

};

enum ESpatialAcceleration
{
	BoundingVolume,
	AABBTree,
	AABBTreeBV,
	Collection,
	Unknown,
	//For custom types continue the enum after ESpatialAcceleration::Unknown
};

using SpatialAccelerationType = uint8;	//see ESpatialAcceleration. Projects can add their own custom types by using enum values higher than ESpatialAcceleration::Unknown

template <typename TPayload>
typename TEnableIf<!TIsPointer<TPayload>::Value, FUniqueIdx>::Type GetUniqueIdx(const TPayload& Payload)
{
	const FUniqueIdx Idx = Payload.UniqueIdx();
	ensure(Idx.IsValid());
	return Idx;
}

template <typename TPayload>
typename TEnableIf<TIsPointer<TPayload>::Value,FUniqueIdx>::Type GetUniqueIdx(const TPayload& Payload)
{
	const FUniqueIdx Idx = Payload->UniqueIdx();
	ensure(Idx.IsValid());
	return Idx;
}

FORCEINLINE FUniqueIdx GetUniqueIdx(const int32 Payload)
{
	ensure(Payload >=0);	//-1 idx implies it was never set
	return FUniqueIdx(Payload);
}

FORCEINLINE FUniqueIdx GetUniqueIdx(const FUniqueIdx Payload)
{
	ensure(Payload.IsValid());
	return Payload;
}


template <typename TPayloadType, typename T>
struct TPayloadBoundsElement
{
	TPayloadType Payload;
	TAABB<T, 3> Bounds;

	void Serialize(FChaosArchive& Ar)
	{
		Ar << Payload;
		TBox<T,3>::SerializeAsAABB(Ar, Bounds);
	}

	template <typename TPayloadType2>
	TPayloadType2 GetPayload(int32 Idx) const { return Payload; }

	bool HasBoundingBox() const { return true; }

	const TAABB<T, 3>& BoundingBox() const { return Bounds; }

	FUniqueIdx UniqueIdx() const
	{
		return ::Chaos::GetUniqueIdx(Payload);
	}
};

template <typename TPayloadType, typename T>
FChaosArchive& operator<<(FChaosArchive& Ar, TPayloadBoundsElement<TPayloadType, T>& PayloadElement)
{
	PayloadElement.Serialize(Ar);
	return Ar;
}

template <typename TPayloadType, typename T, int d>
class CHAOS_API ISpatialAcceleration
{
public:

	ISpatialAcceleration(SpatialAccelerationType InType = ESpatialAcceleration::Unknown)
		: Type(InType), AsyncTimeSlicingComplete(true)
	{
	}
	virtual ~ISpatialAcceleration() = default;

	virtual bool IsAsyncTimeSlicingComplete() { return AsyncTimeSlicingComplete; }
	virtual void ProgressAsyncTimeSlicing(bool ForceBuildCompletion = false) {}
	virtual TArray<TPayloadType> FindAllIntersections(const TAABB<T, d>& Box) const { check(false); return TArray<TPayloadType>(); }

	virtual void Raycast(const TVector<T, d>& Start, const TVector<T, d>& Dir, const T Length, ISpatialVisitor<TPayloadType, T>& Visitor) const { check(false); }
	virtual void Sweep(const TVector<T, d>& Start, const TVector<T, d>& Dir, const T Length, const TVector<T, d> QueryHalfExtents, ISpatialVisitor<TPayloadType, T>& Visitor) const { check(false);}
	virtual void Overlap(const TAABB<T, d>& QueryBounds, ISpatialVisitor<TPayloadType, T>& Visitor) const { check(false); }

	virtual void RemoveElement(const TPayloadType& Payload)
	{
		check(false);	//not implemented
	}

	virtual void UpdateElement(const TPayloadType& Payload, const TAABB<T, d>& NewBounds, bool bHasBounds)
	{
		check(false);
	}

	virtual void RemoveElementFrom(const TPayloadType& Payload, FSpatialAccelerationIdx Idx)
	{
		RemoveElement(Payload);
	}

	virtual void UpdateElementIn(const TPayloadType& Payload, const TAABB<T, d>& NewBounds, bool bHasBounds, FSpatialAccelerationIdx Idx)
	{
		UpdateElement(Payload, NewBounds, bHasBounds);
	}

	virtual TUniquePtr<ISpatialAcceleration<TPayloadType, T, d>> Copy() const
	{
		check(false);	//not implemented
		return nullptr;
	}

#if !UE_BUILD_SHIPPING
	virtual void DebugDraw(ISpacialDebugDrawInterface<T>* InInterface) const {}
	virtual void DumpStats() const {}
#endif

	static ISpatialAcceleration<TPayloadType, T, d>* SerializationFactory(FChaosArchive& Ar, ISpatialAcceleration<TPayloadType, T, d>* Accel);
	virtual void Serialize(FChaosArchive& Ar)
	{
		check(false);
	}

	SpatialAccelerationType GetType() const { return Type; }

	template <typename TConcrete>
	TConcrete* As()
	{
		return Type == TConcrete::StaticType ? static_cast<TConcrete*>(this) : nullptr;
	}

	template <typename TConcrete>
	const TConcrete* As() const
	{
		return Type == TConcrete::StaticType ? static_cast<const TConcrete*>(this) : nullptr;
	}

	template <typename TConcrete>
	TConcrete& AsChecked()
	{
		check(Type == TConcrete::StaticType); 
		return static_cast<TConcrete&>(*this);
	}

	template <typename TConcrete>
	const TConcrete& AsChecked() const
	{
		check(Type == TConcrete::StaticType);
		return static_cast<const TConcrete&>(*this);
	}

protected:
	virtual void SetAsyncTimeSlicingComplete(bool InState) { AsyncTimeSlicingComplete = InState; }

private:
	SpatialAccelerationType Type;
	bool AsyncTimeSlicingComplete;
};

template <typename TBase, typename TDerived>
static TUniquePtr<TDerived> AsUniqueSpatialAcceleration(TUniquePtr<TBase>&& Base)
{
	if (TDerived* Derived = Base->template As<TDerived>())
	{
		Base.Release();
		return TUniquePtr<TDerived>(Derived);
	}
	return nullptr;
}

template <typename TDerived, typename TBase>
static TUniquePtr<TDerived> AsUniqueSpatialAccelerationChecked(TUniquePtr<TBase>&& Base)
{
	TDerived& Derived = Base->template AsChecked<TDerived>();
	Base.Release();
	return TUniquePtr<TDerived>(&Derived);
}

/** Helper class used to bridge virtual to template implementation of acceleration structures */
template <typename TPayloadType, typename T>
class TSpatialVisitor
{
public:
	TSpatialVisitor(ISpatialVisitor<TPayloadType, T>& InVisitor)
		: Visitor(InVisitor) {}
	FORCEINLINE bool VisitOverlap(const TSpatialVisitorData<TPayloadType>& Instance)
	{
		return Visitor.Overlap(Instance);
	}

	FORCEINLINE bool VisitRaycast(const TSpatialVisitorData<TPayloadType>& Instance, FQueryFastData& CurData)
	{
		return Visitor.Raycast(Instance, CurData);
	}

	FORCEINLINE bool VisitSweep(const TSpatialVisitorData<TPayloadType>& Instance, FQueryFastData& CurData)
	{
		return Visitor.Sweep(Instance, CurData);
	}

	FORCEINLINE const void* GetQueryData() const
	{
		return Visitor.GetQueryData();
	}

private:
	ISpatialVisitor<TPayloadType, T>& Visitor;
};

#ifndef CHAOS_SERIALIZE_OUT
#define CHAOS_SERIALIZE_OUT WITH_EDITOR
#endif

//Provides a TMap like API but backed by a dense array. The keys provided must implement GetUniqueIdx
template <typename TKey, typename TValue>
class TArrayAsMap
{
public:
	TValue* Find(const TKey& Key)
	{
		const int32 Idx = GetUniqueIdx(Key).Idx;
		if(Idx < Entries.Num() && Entries[Idx].bSet)
		{
			return &Entries[Idx].Value;
		}
		return nullptr;
	}

	TValue& FindChecked(const TKey& Key)
	{
		return Entries[GetUniqueIdx(Key).Idx].Value;
	}

	TValue& FindOrAdd(const TKey& Key)
	{
		if(TValue* Elem = Find(Key))
		{
			return *Elem;
		}

		return Add(Key);
	}

	void Empty()
	{
		Entries.Empty();
	}

	TValue& Add(const TKey& Key)
	{
		const int32 Idx = GetUniqueIdx(Key).Idx;
		if(Idx >= Entries.Num())
		{
			const int32 NumToAdd = Idx + 1 - Entries.Num();
			Entries.AddDefaulted(NumToAdd);
#if CHAOS_SERIALIZE_OUT
			KeysToSerializeOut.AddDefaulted(NumToAdd);
#endif
		}

		ensure(Entries[Idx].bSet == false);	//element already added
		Entries[Idx].bSet = true;

#if CHAOS_SERIALIZE_OUT
		KeysToSerializeOut[Idx] = Key;
#endif

		return Entries[Idx].Value;
	}

	void Add(const TKey& Key, const TValue& Value)
	{
		Add(Key) = Value;
	}

	void RemoveChecked(const TKey& Key)
	{
		const int32 Idx = GetUniqueIdx(Key).Idx;
		Entries[Idx] = FEntry();	//Mark as free, also resets default values for next use of value
#if CHAOS_SERIALIZE_OUT
		KeysToSerializeOut[Idx] = TKey();
#endif
	}

	void Remove(const TKey& Key)
	{
		const int32 Idx = GetUniqueIdx(Key).Idx;
		if(Idx >= 0 && Idx < Entries.Num())
		{
			Entries[Idx] = FEntry();	//Mark as free, also resets default values for next use of value
#if CHAOS_SERIALIZE_OUT
			KeysToSerializeOut[Idx] = TKey();
#endif
		}
	}

	void Reset()
	{
		Entries.Reset();
#if CHAOS_SERIALIZE_OUT 
		KeysToSerializeOut.Reset();
#endif
	}

	void Serialize(FChaosArchive& Ar)
	{
		bool bCanSerialize = Ar.IsLoading();
#if CHAOS_SERIALIZE_OUT 
		bCanSerialize = true;
#endif

		if(bCanSerialize)
		{
			TArray<TKey> DirectKeys;
			Ar << DirectKeys;

			for(auto& Key : DirectKeys)
			{
				TValue& Value = Add(Key);
				Ar << Value;
			}
		}
		else
		{
			ensure(false);	//can't serialize out, if you are trying to serialize for perf/debug set CHAOS_SERIALIZE_OUT to 1 
		}
	}

private:

	struct FEntry
	{
		TValue Value;
		bool bSet;

		FEntry()
			: bSet(false)
		{

		}
	};

	TArray<FEntry> Entries;

#if CHAOS_SERIALIZE_OUT
	//The indices are generated at runtime, so there's no way to serialize them directly
	//Because of that we serialize the actual key which we can find, and then at runtime we use its transient index
	TArray<TKey> KeysToSerializeOut;
#endif
};

template <typename TKey, typename TValue>
FChaosArchive& operator<< (FChaosArchive& Ar, TArrayAsMap<TKey, TValue>& Map)
{
	Map.Serialize(Ar);
	return Ar;
}


template <typename TPayload>
typename TEnableIf<!TIsPointer<TPayload>::Value, bool>::Type PrePreFilterHelper(const TPayload& Payload, const void* QueryData)
{
	return Payload.PrePreFilter(QueryData);
}

template <typename TPayload>
typename TEnableIf<TIsPointer<TPayload>::Value, bool>::Type PrePreFilterHelper(const TPayload& Payload, const void* QueryData)
{
	return false;
}

FORCEINLINE bool PrePreFilterHelper(const int32 Payload, const void* QueryData)
{
	return false;
}


}
