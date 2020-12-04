// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Templates/ChooseClass.h"
#include "Chaos/PBDRigidClusteredParticles.h"
#include "Chaos/PBDGeometryCollectionParticles.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/ParticleIterator.h"
#include "Chaos/Properties.h"
#include "ChaosCheck.h"
#include "Chaos/ChaosDebugDrawDeclares.h"
#if CHAOS_DEBUG_DRAW
#include "Chaos/ChaosDebugDraw.h"
#endif

class IPhysicsProxyBase;

namespace Chaos
{
	class FConstraintHandle;

template <typename T, int d>
struct TGeometryParticleParameters
{
	TGeometryParticleParameters()
		: bDisabled(false) {}
	bool bDisabled;
};

template <typename T, int d>
struct TKinematicGeometryParticleParameters : public TGeometryParticleParameters<T, d>
{
	TKinematicGeometryParticleParameters()
		: TGeometryParticleParameters<T, d>() {}
};

template <typename T, int d>
struct TPBDRigidParticleParameters : public TKinematicGeometryParticleParameters<T, d>
{
	TPBDRigidParticleParameters()
		: TKinematicGeometryParticleParameters<T, d>()
		, bStartSleeping(false)
		, bGravityEnabled(true)
	{}
	bool bStartSleeping;
	bool bGravityEnabled;
};

/** Concrete can either be the game thread or physics representation, but API stays the same. Useful for keeping initialization and other logic the same*/
template <typename T, int d, typename FConcrete>
void GeometryParticleDefaultConstruct(FConcrete& Concrete, const TGeometryParticleParameters<T,d>& Params)
{
	Concrete.SetX(TVector<T, d>(0));
	Concrete.SetR(TRotation<T, d>::Identity);
	Concrete.SetSpatialIdx(FSpatialAccelerationIdx{ 0,0 });
	Concrete.SetUserData(nullptr);
}

template <typename T, int d, typename FConcrete>
void KinematicGeometryParticleDefaultConstruct(FConcrete& Concrete, const TKinematicGeometryParticleParameters<T, d>& Params)
{
	Concrete.SetV(TVector<T, d>(0));
	Concrete.SetW(TVector<T, d>(0));
}

template <typename T, int d, bool bPersistent>
void PBDRigidParticleHandleImpDefaultConstruct(TPBDRigidParticleHandleImp<T, d, bPersistent>& Concrete, const TPBDRigidParticleParameters<T, d>& Params)
{
	//don't bother calling parent since the call gets made by the corresponding hierarchy in FConcrete
	Concrete.SetCollisionGroup(0);
	Concrete.ClearCollisionConstraintFlag();
	Concrete.SetDisabled(Params.bDisabled);
	Concrete.SetPreV(Concrete.V());
	Concrete.SetPreW(Concrete.W());
	Concrete.SetP(Concrete.X());
	Concrete.SetQ(Concrete.R());
	Concrete.SetVSmooth(Concrete.V());
	Concrete.SetWSmooth(Concrete.W());
	Concrete.SetF(TVector<T, d>(0));
	Concrete.SetTorque(TVector<T, d>(0));
	Concrete.SetLinearImpulse(TVector<T, d>(0));
	Concrete.SetAngularImpulse(TVector<T, d>(0));
	Concrete.SetM(1);
	Concrete.SetInvM(1);
	Concrete.SetCenterOfMass(TVector<T,d>(0));
	Concrete.SetRotationOfMass(TRotation<T,d>::FromIdentity());
	Concrete.SetI(PMatrix<T, d, d>(1, 1, 1));
	Concrete.SetInvI(PMatrix<T, d, d>(1, 1, 1));
	Concrete.SetLinearEtherDrag(0.f);
	Concrete.SetAngularEtherDrag(0.f);
	Concrete.SetObjectStateLowLevel(Params.bStartSleeping ? EObjectStateType::Sleeping : EObjectStateType::Dynamic);
	Concrete.SetGravityEnabled(Params.bGravityEnabled);
	Concrete.SetResimType(EResimType::FullResim);
}

template <typename T, int d>
void PBDRigidParticleDefaultConstruct(TPBDRigidParticle<T,d>& Concrete, const TPBDRigidParticleParameters<T, d>& Params)
{
	//don't bother calling parent since the call gets made by the corresponding hierarchy in FConcrete
	Concrete.SetCollisionGroup(0);
	//Concrete.SetDisabled(Params.bDisabled);
	Concrete.SetF(TVector<T, d>(0));
	Concrete.SetTorque(TVector<T, d>(0));
	Concrete.SetLinearImpulse(TVector<T, d>(0));
	Concrete.SetAngularImpulse(TVector<T, d>(0));
	Concrete.SetM(1);
	Concrete.SetInvM(1);
	Concrete.SetCenterOfMass(TVector<T,d>(0));
	Concrete.SetRotationOfMass(TRotation<T,d>(FQuat(EForceInit::ForceInit)));
	Concrete.SetI(PMatrix<T, d, d>(1, 1, 1));
	Concrete.SetInvI(PMatrix<T, d, d>(1, 1, 1));
	Concrete.SetLinearEtherDrag(0.f);
	Concrete.SetAngularEtherDrag(0.f);
	Concrete.SetObjectState(Params.bStartSleeping ? EObjectStateType::Sleeping : EObjectStateType::Dynamic);
	Concrete.SetGravityEnabled(Params.bGravityEnabled);
	Concrete.ClearEvents();
	Concrete.SetInitialized(false);
	Concrete.SetResimType(EResimType::FullResim);
}



template <typename T, int d, typename FConcrete>
void PBDRigidClusteredParticleDefaultConstruct(FConcrete& Concrete, const TPBDRigidParticleParameters<T, d>& Params)
{
	//don't bother calling parent since the call gets made by the corresponding hierarchy in FConcrete
}

template <typename FConcrete>
bool GeometryParticleSleeping(const FConcrete& Concrete)
{
	return Concrete.ObjectState() == EObjectStateType::Sleeping;
}

//Used to filter out at the acceleration structure layer
//Returns true when there is no way a later PreFilter will succeed. Avoid virtuals etc..
FORCEINLINE_DEBUGGABLE bool PrePreFilterImp(const FCollisionFilterData& QueryFilterData, const FCollisionFilterData& UnionFilterData)
{
	//HACK: need to replace all these hard-coded values with proper enums, bad modules are not setup for it right now
	//ECollisionQuery QueryType = (ECollisionQuery)QueryFilter.Word0;
	//if (QueryType != ECollisionQuery::ObjectQuery)
	if(QueryFilterData.Word0)
	{
		//since we're taking the union of shapes we can only support trace channel
		//const ECollisionChannel QuerierChannel = GetCollisionChannel(QueryFilter.Word3);
		const uint32 QuerierChannel = (QueryFilterData.Word3 << 6) >> (32 - 5);
	
		//uint32 const QuerierBit = ECC_TO_BITFIELD(QuerierChannel);
		const uint32 QuerierBit = (1 << (QuerierChannel));
	
		// check if Querier wants a hit
		const uint32 TouchOrBlock = (UnionFilterData.Word1 | UnionFilterData.Word2);
		return !(QuerierBit & TouchOrBlock);
	}

	return false;
}

/** Wrapper that holds both physics thread data and GT data. It's possible that the physics handle is null if we're doing operations entirely on external threads*/
template <typename T, int d>
class TAccelerationStructureHandle
{
public:
	TAccelerationStructureHandle(TGeometryParticleHandle<T, d>* InHandle);
	TAccelerationStructureHandle(TGeometryParticle<T, d>* InGeometryParticle = nullptr);

	template <bool bPersistent>
	TAccelerationStructureHandle(TGeometryParticleHandleImp<T, d, bPersistent>& InHandle);

	//Should only be used by GT and scene query threads where an appropriate lock has been acquired
	TGeometryParticle<T, d>* GetExternalGeometryParticle_ExternalThread() const { return ExternalGeometryParticle; }

	//Should only be used by PT
	TGeometryParticleHandle<T, d>* GetGeometryParticleHandle_PhysicsThread() const { return GeometryParticleHandle; }

	bool operator==(const TAccelerationStructureHandle<T, d>& Rhs) const
	{
		CHAOS_ENSURE((ExternalGeometryParticle || GeometryParticleHandle));
		CHAOS_ENSURE((Rhs.ExternalGeometryParticle || Rhs.GeometryParticleHandle));

		if (!GeometryParticleHandle || !Rhs.GeometryParticleHandle)
		{
			return (ExternalGeometryParticle == Rhs.ExternalGeometryParticle);
		}
		else
		{
			return (GeometryParticleHandle == Rhs.GeometryParticleHandle);
		}
	}

	bool operator!=(const TAccelerationStructureHandle<T, d>& Rhs) const
	{
		return !(*this == Rhs);
	}

	void Serialize(FChaosArchive& Ar);

	FUniqueIdx UniqueIdx() const
	{
		return CachedUniqueIdx;
	}

	bool PrePreFilter(const void* QueryData) const
	{
		if(bCanPrePreFilter)
		{
			if (const FCollisionFilterData* QueryFilterData = static_cast<const FCollisionFilterData*>(QueryData))
			{
				return PrePreFilterImp(*QueryFilterData, UnionFilterData);
			}
		}
		
		return false;
	}

	void UpdateFrom(const TAccelerationStructureHandle<T, d>& InOther)
	{
		UnionFilterData.Word0 = InOther.UnionFilterData.Word0;
		UnionFilterData.Word1 = InOther.UnionFilterData.Word1;
		UnionFilterData.Word2 = InOther.UnionFilterData.Word2;
		UnionFilterData.Word3 = InOther.UnionFilterData.Word3;
	}

private:
	TGeometryParticle<T, d>* ExternalGeometryParticle;
	TGeometryParticleHandle<T, d>* GeometryParticleHandle;

	FUniqueIdx CachedUniqueIdx;
	FCollisionFilterData UnionFilterData;
	bool bCanPrePreFilter;

	template <typename TParticle>
	void UpdatePrePreFilter(const TParticle& Particle);

public:
#if CHAOS_DEBUG_DRAW
	void DebugDraw(const bool bExternal, const bool bHit) const;
#endif
};

template <typename T, int d>
class TParticleHandleBase
{
public:
	using TType = T;
	static constexpr int D = d;

	TParticleHandleBase()
		: SerializableGeometryParticles(TSerializablePtr<TGeometryParticles<T,d>>())
		, ParticleIdx(0)
		, Type(EParticleType::Static)
	{
	}

	template <typename TParticlesType>
	TParticleHandleBase(TSerializablePtr<TParticlesType> InParticles, int32 InParticleIdx)
	: SerializableGeometryParticles(InParticles)
	, ParticleIdx(InParticleIdx)
	, Type(InParticles ? InParticles->ParticleType() : EParticleType::Static)
	{
	}

	//Should only be used for transient handles - maybe we can protect this better?
	TParticleHandleBase(TGeometryParticles<T,d>* InParticles, int32 InParticleIdx)
		: SerializableGeometryParticles(TSerializablePtr<TGeometryParticles<T, d>>())
		, ParticleIdx(InParticleIdx)
		, Type(InParticles ? InParticles->ParticleType() : EParticleType::Static)
	{
		GeometryParticles = InParticles;
	}

	//NOTE: this is not virtual and only acceptable because we know the children have no extra data that requires destruction. 
	//You must modify the union to extend this class and do not add any member variables
	~TParticleHandleBase()
	{
	}

	void Serialize(FChaosArchive& Ar)
	{
		Ar << ParticleIdx;
		uint8 RawType = (uint8)Type;
		Ar << RawType;
		Type = (EParticleType)RawType;
		Ar << SerializableGeometryParticles;
	}

	//This is needed for post serialization fixup of raw pointer. Should only be called by serialization code which is low level and knows the implementation details
	void SetSOALowLevel(TGeometryParticles<T, d>* InParticles)
	{
		//should not be swapping SOAs
		ensure(GeometryParticles == nullptr || GeometryParticles == InParticles);
		GeometryParticles = InParticles;
	}

	EParticleType GetParticleType() const { return Type; }
protected:
	union
	{
		TSerializablePtr<TGeometryParticles<T, d>> SerializableGeometryParticles;
		TGeometryParticles<T, d>* GeometryParticles;
		TKinematicGeometryParticles<T, d>* KinematicGeometryParticles;
		TPBDRigidParticles<T, d>* PBDRigidParticles;
		TPBDRigidClusteredParticles<T, d>* PBDRigidClusteredParticles;
	};

	template <typename TSOA>
	friend class TConstParticleIterator;
	//todo: maybe make private?
	int32 ParticleIdx;	//Index into the particle struct of arrays. Note the index can change
	EParticleType Type;
};

template <typename T, int d, bool bPersistent>
class TKinematicGeometryParticleHandleImp;

template <typename T, int d, bool bPersistent>
class TPBDRigidParticleHandleImp;

template <typename T, int d>
TGeometryParticleHandle<T, d>* GetHandleHelper(TGeometryParticleHandle<T, d>* Handle) { return Handle; }
template <typename T, int d>
const TGeometryParticleHandle<T, d>* GetHandleHelper(const TGeometryParticleHandle<T, d>* Handle) { return Handle; }
template <typename T, int d>
TGeometryParticleHandle<T, d>* GetHandleHelper(TTransientGeometryParticleHandle<T, d>* Handle);
template <typename T, int d>
const TGeometryParticleHandle<T, d>* GetHandleHelper(const TTransientGeometryParticleHandle<T, d>* Handle);

template <typename T, int d>
class TGeometryParticleHandles;

template <typename T, int d, bool bPersistent>
class TGeometryParticleHandleImp : public TParticleHandleBase<T,d>
{
public:
	using TTransientHandle = TTransientGeometryParticleHandle<T, d>;
	using THandleBase = TParticleHandleBase<T, d>;
	using THandleBase::GeometryParticles;
	using THandleBase::ParticleIdx;
	using THandleBase::Type;
	using TSOAType = TGeometryParticles<T,d>;

	static constexpr bool AlwaysSerializable = bPersistent;

	static TGeometryParticleHandleImp<T, d, bPersistent>* SerializationFactory(FChaosArchive& Ar, TGeometryParticleHandleImp<T, d, bPersistent>* Handle);

	template <typename TPayloadType>
	TPayloadType GetPayload(int32 Idx)
	{
		return TPayloadType(Handle());
	}


protected:
	//needed for serialization
	TGeometryParticleHandleImp()
		: TParticleHandleBase<T, d>()
	{
	}

	TGeometryParticleHandleImp(TSerializablePtr<TGeometryParticles<T, d>> InParticles, int32 InParticleIdx, int32 InHandleIdx, const TGeometryParticleParameters<T, d>& Params)
		: TParticleHandleBase<T, d>(InParticles, InParticleIdx)
		, HandleIdx(InHandleIdx)
	{
		//GeometryParticles->Handle(ParticleIdx) = this;
		//TODO: patch from SOA
		GeometryParticleDefaultConstruct<T, d>(*this, Params);
		SetHasBounds(false);
	}

	template <typename TParticlesType, typename TParams>
	static TUniquePtr<typename TParticlesType::THandleType> CreateParticleHandleHelper(TSerializablePtr<TParticlesType> InParticles, int32 InParticleIdx, int32 InHandleIdx, const TParams& Params)
	{
		check(bPersistent);	//non persistent should not be going through this path
		auto NewHandle = new typename TParticlesType::THandleType(InParticles, InParticleIdx, InHandleIdx, Params);
		TUniquePtr<typename TParticlesType::THandleType> Unique(NewHandle);
		const_cast<TParticlesType*>(InParticles.Get())->SetHandle(InParticleIdx, NewHandle);	//todo: add non const serializable ptr
		return Unique;
	}
public:

	static TUniquePtr<TGeometryParticleHandleImp<T,d, bPersistent>> CreateParticleHandle(TSerializablePtr<TGeometryParticles<T, d>> InParticles, int32 InParticleIdx, int32 InHandleIdx, const TGeometryParticleParameters<T, d>& Params = TGeometryParticleParameters<T, d>())
	{
		return TGeometryParticleHandleImp<T,d,bPersistent>::CreateParticleHandleHelper(InParticles, InParticleIdx, InHandleIdx, Params);
	}


	~TGeometryParticleHandleImp()
	{
		if (bPersistent)
		{
			GeometryParticles->ResetWeakParticleHandle(ParticleIdx);
			GeometryParticles->DestroyParticle(ParticleIdx);
			if (static_cast<uint32>(ParticleIdx) < GeometryParticles->Size())
			{
				if (GeometryParticles->RemoveParticleBehavior() == ERemoveParticleBehavior::RemoveAtSwap)
				{
					GeometryParticles->Handle(ParticleIdx)->ParticleIdx = ParticleIdx;
				}
				else
				{
					//need to update all handles >= ParticleIdx
					for (int32 Idx = ParticleIdx; static_cast<uint32>(Idx) < GeometryParticles->Size(); ++Idx)
					{
						GeometryParticles->Handle(Idx)->ParticleIdx -= 1;
					}
				}
			}
		}
	}

	template <typename T2, int d2>
	friend TGeometryParticleHandle<T2, d2>* GetHandleHelper(TTransientGeometryParticleHandle<T2, d2>* Handle);
	template <typename T2, int d2>
	friend const TGeometryParticleHandle<T2, d2>* GetHandleHelper(const TTransientGeometryParticleHandle<T2, d2>* Handle);

	TGeometryParticleHandleImp(const TGeometryParticleHandleImp&) = delete;

	const TVector<T, d>& X() const { return GeometryParticles->X(ParticleIdx); }
	TVector<T, d>& X() { return GeometryParticles->X(ParticleIdx); }
	void SetX(const TVector<T, d>& InX) { GeometryParticles->X(ParticleIdx) = InX; }

	FUniqueIdx UniqueIdx() const { return GeometryParticles->UniqueIdx(ParticleIdx); }
	void SetUniqueIdx(const FUniqueIdx UniqueIdx) const { GeometryParticles->UniqueIdx(ParticleIdx) = UniqueIdx; }

	void* UserData() const { return GeometryParticles->UserData(ParticleIdx); }
	void SetUserData(void* InUserData) { GeometryParticles->UserData(ParticleIdx) = InUserData; }

	const TRotation<T, d>& R() const { return GeometryParticles->R(ParticleIdx); }
	TRotation<T, d>& R() { return GeometryParticles->R(ParticleIdx); }
	void SetR(const TRotation<T, d>& InR) { GeometryParticles->R(ParticleIdx) = InR; }

	void SetXR(const FParticlePositionRotation& XR)
	{
		SetX(XR.X());
		SetR(XR.R());
	}

	void SetNonFrequentData(const FParticleNonFrequentData& InData)
	{
		SetSharedGeometry(InData.Geometry());
		SetUserData(InData.UserData());
		SetUniqueIdx(InData.UniqueIdx());
		SetSpatialIdx(InData.SpatialIdx());

#if CHAOS_CHECKED
		SetDebugName(InData.DebugName());
#endif
	}

	ESyncState SyncState() const
	{
		return GeometryParticles->SyncState(ParticleIdx);
	}

	void SetSyncState(ESyncState State)
	{
		GeometryParticles->SyncState(ParticleIdx) = State;
	}

	TSerializablePtr<FImplicitObject> Geometry() const { return GeometryParticles->Geometry(ParticleIdx); }
	void SetGeometry(TSerializablePtr<FImplicitObject> InGeometry) { GeometryParticles->SetGeometry(ParticleIdx, InGeometry); }

	TSharedPtr<FImplicitObject, ESPMode::ThreadSafe> SharedGeometry() const { return GeometryParticles->SharedGeometry(ParticleIdx); }
	void SetSharedGeometry(TSharedPtr<FImplicitObject, ESPMode::ThreadSafe> InGeometry) { GeometryParticles->SetSharedGeometry(ParticleIdx, InGeometry); }

	TSharedPtr<FImplicitObject, ESPMode::ThreadSafe> SharedGeometryLowLevel() const { return GeometryParticles->SharedGeometry(ParticleIdx); }

	const TUniquePtr<FImplicitObject>& DynamicGeometry() const { return GeometryParticles->DynamicGeometry(ParticleIdx); }
	void SetDynamicGeometry(TUniquePtr<FImplicitObject>&& Unique) { GeometryParticles->SetDynamicGeometry(ParticleIdx, MoveTemp(Unique)); }

	const FShapesArray& ShapesArray() const { return GeometryParticles->ShapesArray(ParticleIdx); }

	const TAABB<T, d>& LocalBounds() const { return GeometryParticles->LocalBounds(ParticleIdx); }
	void SetLocalBounds(const TAABB<T, d>& NewBounds) { GeometryParticles->LocalBounds(ParticleIdx) = NewBounds; }

	const TAABB<T, d>& WorldSpaceInflatedBounds() const { return GeometryParticles->WorldSpaceInflatedBounds(ParticleIdx); }
	void SetWorldSpaceInflatedBounds(const TAABB<T, d>& WorldSpaceInflatedBounds)
	{
		GeometryParticles->SetWorldSpaceInflatedBounds(ParticleIdx, WorldSpaceInflatedBounds);
	}

	bool HasBounds() const { return GeometryParticles->HasBounds(ParticleIdx); }
	void SetHasBounds(bool bHasBounds) { GeometryParticles->HasBounds(ParticleIdx) = bHasBounds; }

	FSpatialAccelerationIdx SpatialIdx() const { return GeometryParticles->SpatialIdx(ParticleIdx); }
	void SetSpatialIdx(FSpatialAccelerationIdx Idx) { GeometryParticles->SpatialIdx(ParticleIdx) = Idx; }

#if CHAOS_CHECKED
	const FName& DebugName() const { return GeometryParticles->DebugName(ParticleIdx); }
	void SetDebugName(const FName& InDebugName) { GeometryParticles->DebugName(ParticleIdx) = InDebugName; }
#endif
	
	EObjectStateType ObjectState() const;

	TGeometryParticle<T, d>* GTGeometryParticle() const { return GeometryParticles->GTGeometryParticle(ParticleIdx); }
	TGeometryParticle<T, d>*& GTGeometryParticle() { return GeometryParticles->GTGeometryParticle(ParticleIdx); }

	const TKinematicGeometryParticleHandleImp<T, d, bPersistent>* CastToKinematicParticle() const;
	TKinematicGeometryParticleHandleImp<T, d, bPersistent>* CastToKinematicParticle();

	const TPBDRigidParticleHandleImp<T, d, bPersistent>* CastToRigidParticle() const;
	TPBDRigidParticleHandleImp<T, d, bPersistent>* CastToRigidParticle();

	const TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>* CastToClustered() const;
	TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>* CastToClustered();

	const TGeometryParticleHandle<T, d>* Handle() const { return GetHandleHelper(this);}
	TGeometryParticleHandle<T, d>* Handle() { return GetHandleHelper(this); }

	bool Sleeping() const { return GeometryParticleSleeping(*this); }

	template <typename Container>
	const auto& AuxilaryValue(const Container& AuxContainer) const
	{
		return AuxContainer[HandleIdx];
	}

	template <typename Container>
	auto& AuxilaryValue(Container& AuxContainer)
	{
		return AuxContainer[HandleIdx];
	}

#if CHAOS_DETERMINISTIC
	FParticleID ParticleID() const { return GeometryParticles->ParticleID(ParticleIdx); }
	FParticleID& ParticleID() { return GeometryParticles->ParticleID(ParticleIdx); }
#endif

	void MoveToSOA(TGeometryParticles<T, d>& ToSOA)
	{
		static_assert(bPersistent, "Cannot move particles from a transient handle");
		check(ToSOA.ParticleType() == Type);
		if (GeometryParticles != &ToSOA)
		{
			GeometryParticles->MoveToOtherParticles(ParticleIdx, ToSOA);
			if (static_cast<uint32>(ParticleIdx) < GeometryParticles->Size())
			{
				GeometryParticles->Handle(ParticleIdx)->ParticleIdx = ParticleIdx;
			}
			const int32 NewParticleIdx = ToSOA.Size() - 1;
			ParticleIdx = NewParticleIdx;
			GeometryParticles = &ToSOA;
		}
	}

	static constexpr EParticleType StaticType() { return EParticleType::Static; }

	FString ToString() const;

	void Serialize(FChaosArchive& Ar)
	{
		THandleBase::Serialize(Ar);
		Ar << HandleIdx;
		GeometryParticles->SetHandle(ParticleIdx, this);
	}

	const FPerShapeData* GetImplicitShape(const FImplicitObject* InObject)
	{
		return GeometryParticles->GetImplicitShape(ParticleIdx, InObject);
	}

	FWeakParticleHandle& WeakParticleHandle()
	{
		return GeometryParticles->WeakParticleHandle(ParticleIdx);
	}

	TArray<FConstraintHandle*>& ParticleConstraints()
	{
		return GeometryParticles->ParticleConstraints(ParticleIdx);
	}

	void AddConstraintHandle(FConstraintHandle* InConstraintHandle )
	{
		return GeometryParticles->AddConstraintHandle(ParticleIdx, InConstraintHandle);
	}

	void RemoveConstraintHandle(FConstraintHandle* InConstraintHandle)
	{
		return GeometryParticles->RemoveConstraintHandle(ParticleIdx, InConstraintHandle);
	}

protected:

	friend TGeometryParticleHandles<T, d>;
	
	struct FInvalidFromTransient {};
	typename TChooseClass<bPersistent, int32, FInvalidFromTransient>::Result HandleIdx;	//Index into the handles array. This is useful for binding external attributes. Note the index can change
};

template<>
template<>
int32 TGeometryParticleHandleImp<float, 3, true>::GetPayload<int32>(int32 Idx);

template<>
template<>
int32 TGeometryParticleHandleImp<float, 3, false>::GetPayload<int32>(int32 Idx);



template <typename T, int d>
TGeometryParticleHandle<T, d>* GetHandleHelper(TTransientGeometryParticleHandle<T, d>* Handle)
{
	return Handle->GeometryParticles->Handle(Handle->ParticleIdx);
}
template <typename T, int d>
const TGeometryParticleHandle<T, d>* GetHandleHelper(const TTransientGeometryParticleHandle<T, d>* Handle)
{
	return Handle->GeometryParticles->Handle(Handle->ParticleIdx);
}

template <typename T, int d, bool bPersistent>
class TKinematicGeometryParticleHandleImp : public TGeometryParticleHandleImp<T,d, bPersistent>
{
public:
	using TGeometryParticleHandleImp<T, d, bPersistent>::ParticleIdx;
	using TGeometryParticleHandleImp<T, d, bPersistent>::KinematicGeometryParticles;
	using TGeometryParticleHandleImp<T, d, bPersistent>::Type;
	using TGeometryParticleHandleImp<T, d, bPersistent>::CastToRigidParticle;
	using TTransientHandle = TTransientKinematicGeometryParticleHandle<T, d>;
	using TSOAType = TKinematicGeometryParticles<T, d>;
	using TGeometryParticleHandleImp<T, d, bPersistent>::SetXR;
	
protected:
	friend class TGeometryParticleHandleImp<T, d, bPersistent>;
	//needed for serialization
	TKinematicGeometryParticleHandleImp()
		: TGeometryParticleHandleImp<T, d, bPersistent>()
	{
	}

	TKinematicGeometryParticleHandleImp(TSerializablePtr<TKinematicGeometryParticles<T, d>> Particles, int32 InIdx, int32 InGlobalIdx, const TKinematicGeometryParticleParameters<T, d>& Params)
		: TGeometryParticleHandleImp<T, d, bPersistent>(TSerializablePtr<TGeometryParticles<T, d>>(Particles), InIdx, InGlobalIdx, Params)
	{
		KinematicGeometryParticleDefaultConstruct<T, d>(*this, Params);
	}
public:

	static TUniquePtr<TKinematicGeometryParticleHandleImp<T, d, bPersistent>> CreateParticleHandle(TSerializablePtr<TKinematicGeometryParticles<T, d>> InParticles, int32 InParticleIdx, int32 InHandleIdx, const TKinematicGeometryParticleParameters<T, d>& Params = TKinematicGeometryParticleParameters<T, d>())
	{
		return TGeometryParticleHandleImp<T, d, bPersistent>::CreateParticleHandleHelper(InParticles, InParticleIdx, InHandleIdx, Params);
	}

	TSerializablePtr <TKinematicGeometryParticleHandleImp<T, d, bPersistent>> ToSerializable() const
	{
		TSerializablePtr<TKinematicGeometryParticleHandleImp<T, d, bPersistent>> Serializable;
		Serializable.SetFromRawLowLevel(this);	//this is safe because CreateParticleHandle gives back a TUniquePtr
		return Serializable;
	}

	const TVector<T, d>& V() const { return KinematicGeometryParticles->V(ParticleIdx); }
	TVector<T, d>& V() { return KinematicGeometryParticles->V(ParticleIdx); }
	void SetV(const TVector<T, d>& InV) { KinematicGeometryParticles->V(ParticleIdx) = InV; }

	const TVector<T, d>& W() const { return KinematicGeometryParticles->W(ParticleIdx); }
	TVector<T, d>& W() { return KinematicGeometryParticles->W(ParticleIdx); }
	void SetW(const TVector<T, d>& InW) { KinematicGeometryParticles->W(ParticleIdx) = InW; }

	void SetVelocities(const FParticleVelocities& Velocities)
	{
		SetV(Velocities.V());
		SetW(Velocities.W());
	}

	void SetKinematicTarget(const TKinematicTarget<T, d>& InKinematicTarget)
	{
		KinematicGeometryParticles->KinematicTarget(ParticleIdx) = InKinematicTarget;
	}

	const TKinematicTarget<T, d>& KinematicTarget() const { return KinematicGeometryParticles->KinematicTarget(ParticleIdx); }
	TKinematicTarget<T, d>& KinematicTarget() { return KinematicGeometryParticles->KinematicTarget(ParticleIdx); }

	//Really only useful when using a transient handle
	const TKinematicGeometryParticleHandleImp<T, d, true>* Handle() const { return KinematicGeometryParticles->Handle(ParticleIdx); }
	TKinematicGeometryParticleHandleImp<T, d, true>* Handle() { return KinematicGeometryParticles->Handle(ParticleIdx); }

	EObjectStateType ObjectState() const;
	static constexpr EParticleType StaticType() { return EParticleType::Kinematic; }
};

template <typename T, int d, bool bPersistent>
class TPBDRigidParticleHandleImp : public TKinematicGeometryParticleHandleImp<T, d, bPersistent>
{
public:
	using TGeometryParticleHandleImp<T, d, bPersistent>::ParticleIdx;
	using TGeometryParticleHandleImp<T, d, bPersistent>::PBDRigidParticles;
	using TKinematicGeometryParticleHandleImp<T, d, bPersistent>::V;
	using TKinematicGeometryParticleHandleImp<T, d, bPersistent>::W;
	using TGeometryParticleHandleImp<T, d, bPersistent>::Type;
	using TTransientHandle = TTransientPBDRigidParticleHandle<T, d>;
	using TSOAType = TPBDRigidParticles<T, d>;

protected:
	friend class TGeometryParticleHandleImp<T, d, bPersistent>;

	//needed for serialization
	TPBDRigidParticleHandleImp()
		: TKinematicGeometryParticleHandleImp<T, d, bPersistent>()
	{
	}

	TPBDRigidParticleHandleImp<T, d, bPersistent>(TSerializablePtr<TPBDRigidParticles<T, d>> Particles, int32 InIdx, int32 InGlobalIdx, const TPBDRigidParticleParameters<T, d>& Params = TPBDRigidParticleParameters<T, d>())
		: TKinematicGeometryParticleHandleImp<T, d, bPersistent>(TSerializablePtr<TKinematicGeometryParticles<T, d>>(Particles), InIdx, InGlobalIdx, Params)
	{
		PBDRigidParticleHandleImpDefaultConstruct<T, d>(*this, Params);
		SetIsland(INDEX_NONE);
		SetToBeRemovedOnFracture(false);
	}
public:

	static TUniquePtr<TPBDRigidParticleHandleImp<T, d, bPersistent>> CreateParticleHandle(TSerializablePtr<TPBDRigidParticles<T, d>> InParticles, int32 InParticleIdx, int32 InHandleIdx, const TPBDRigidParticleParameters<T, d>& Params = TPBDRigidParticleParameters<T, d>())
	{
		return TGeometryParticleHandleImp<T, d, bPersistent>::CreateParticleHandleHelper(InParticles, InParticleIdx, InHandleIdx, Params);
	}

	TSerializablePtr <TPBDRigidParticleHandleImp<T, d, bPersistent>> ToSerializable() const
	{
		TSerializablePtr<TPBDRigidParticleHandleImp<T, d, bPersistent>> Serializable;
		Serializable.SetFromRawLowLevel(this);	//this is safe because CreateParticleHandle gives back a TUniquePtr
		return Serializable;
	}

	void SetXR(const FParticlePositionRotation& InXR)
	{
		TKinematicGeometryParticleHandleImp<T, d, bPersistent>::SetXR(InXR);
		SetP(InXR.X());
		SetQ(InXR.R());
	}

	operator TPBDRigidParticleHandleImp<T, d, false>& () { return reinterpret_cast<TPBDRigidParticleHandleImp<T, d, false>&>(*this); }

	const TUniquePtr<TBVHParticles<T, d>>& CollisionParticles() const { return PBDRigidParticles->CollisionParticles(ParticleIdx); }
	TUniquePtr<TBVHParticles<T, d>>& CollisionParticles() { return PBDRigidParticles->CollisionParticles(ParticleIdx); }

	int32 CollisionParticlesSize() const { return PBDRigidParticles->CollisionParticlesSize(ParticleIdx); }
	void CollisionParticlesInitIfNeeded() { PBDRigidParticles->CollisionParticlesInitIfNeeded(ParticleIdx); }
	void SetCollisionParticles(TParticles<T, d>&& Points) { PBDRigidParticles->SetCollisionParticles(ParticleIdx, MoveTemp(Points)); }

	int32 CollisionGroup() const { return PBDRigidParticles->CollisionGroup(ParticleIdx); }
	int32& CollisionGroup() { return PBDRigidParticles->CollisionGroup(ParticleIdx); }
	void SetCollisionGroup(const int32 InCollisionGroup) { PBDRigidParticles->CollisionGroup(ParticleIdx) = InCollisionGroup; }

	bool HasCollisionConstraintFlag(const ECollisionConstraintFlags Flag) const { return  PBDRigidParticles->HasCollisionConstraintFlag(Flag, ParticleIdx); }
	void AddCollisionConstraintFlag(const ECollisionConstraintFlags Flag) { PBDRigidParticles->AddCollisionConstraintFlag(Flag, ParticleIdx); }
	void RemoveCollisionConstraintFlag(const ECollisionConstraintFlags Flag) { PBDRigidParticles->RemoveCollisionConstraintFlag(Flag, ParticleIdx); }
	void ClearCollisionConstraintFlag() { PBDRigidParticles->ClearCollisionConstraintFlag(ParticleIdx); }

	bool Disabled() const { return PBDRigidParticles->Disabled(ParticleIdx); }
	bool& Disabled() { return PBDRigidParticles->DisabledRef(ParticleIdx); }

	// See Comment on TRigidParticle::SetDisabledLowLevel. State changes in Evolution should accompany this call.
	void SetDisabledLowLevel(bool disabled) { PBDRigidParticles->SetDisabledLowLevel(ParticleIdx, disabled); }
	void SetDisabled(const bool InDisabled) { PBDRigidParticles->DisabledRef(ParticleIdx) = InDisabled; }

	const TVector<T, d>& PreV() const { return PBDRigidParticles->PreV(ParticleIdx); }
	TVector<T, d>& PreV() { return PBDRigidParticles->PreV(ParticleIdx); }
	void SetPreV(const TVector<T, d>& InPreV) { PBDRigidParticles->PreV(ParticleIdx) = InPreV; }

	const TVector<T, d>& PreW() const { return PBDRigidParticles->PreW(ParticleIdx); }
	TVector<T, d>& PreW() { return PBDRigidParticles->PreW(ParticleIdx); }
	void SetPreW(const TVector<T, d>& InPreW) { PBDRigidParticles->PreW(ParticleIdx) = InPreW; }

	const TVector<T, d>& P() const { return PBDRigidParticles->P(ParticleIdx); }
	TVector<T, d>& P() { return PBDRigidParticles->P(ParticleIdx); }
	void SetP(const TVector<T, d>& InP) { PBDRigidParticles->P(ParticleIdx) = InP; }

	const TRotation<T, d>& Q() const { return PBDRigidParticles->Q(ParticleIdx); }
	TRotation<T, d>& Q() { return PBDRigidParticles->Q(ParticleIdx); }
	void SetQ(const TRotation<T, d>& InQ) { PBDRigidParticles->Q(ParticleIdx) = InQ; }

	const TVector<T, d>& VSmooth() const { return PBDRigidParticles->VSmooth(ParticleIdx); }
	TVector<T, d>& VSmooth() { return PBDRigidParticles->VSmooth(ParticleIdx); }
	void SetVSmooth(const TVector<T, d>& InVSmooth) { PBDRigidParticles->VSmooth(ParticleIdx) = InVSmooth; }

	const TVector<T, d>& WSmooth() const { return PBDRigidParticles->WSmooth(ParticleIdx); }
	TVector<T, d>& WSmooth() { return PBDRigidParticles->WSmooth(ParticleIdx); }
	void SetWSmooth(const TVector<T, d>& InWSmooth) { PBDRigidParticles->WSmooth(ParticleIdx) = InWSmooth; }

	const TVector<T, d>& F() const { return PBDRigidParticles->F(ParticleIdx); }
	TVector<T, d>& F() { return PBDRigidParticles->F(ParticleIdx); }
	void SetF(const TVector<T, d>& InF) { PBDRigidParticles->F(ParticleIdx) = InF; }

	const TVector<T, d>& Torque() const { return PBDRigidParticles->Torque(ParticleIdx); }
	TVector<T, d>& Torque() { return PBDRigidParticles->Torque(ParticleIdx); }
	void SetTorque(const TVector<T, d>& InTorque) { PBDRigidParticles->Torque(ParticleIdx) = InTorque; }

	const TVector<T, d>& LinearImpulse() const { return PBDRigidParticles->LinearImpulse(ParticleIdx); }
	TVector<T, d>& LinearImpulse() { return PBDRigidParticles->LinearImpulse(ParticleIdx); }
	void SetLinearImpulse(const TVector<T, d>& InLinearImpulse) { PBDRigidParticles->LinearImpulse(ParticleIdx) = InLinearImpulse; }

	const TVector<T, d>& AngularImpulse() const { return PBDRigidParticles->AngularImpulse(ParticleIdx); }
	TVector<T, d>& AngularImpulse() { return PBDRigidParticles->AngularImpulse(ParticleIdx); }
	void SetAngularImpulse(const TVector<T, d>& InAngularImpulse) { PBDRigidParticles->AngularImpulse(ParticleIdx) = InAngularImpulse; }

	void SetDynamics(const FParticleDynamics& Dynamics)
	{
		SetF(Dynamics.F());
		SetTorque(Dynamics.Torque());
		SetLinearImpulse(Dynamics.LinearImpulse());
		SetAngularImpulse(Dynamics.AngularImpulse());
	}

	void SetMassProps(const FParticleMassProps& Props)
	{
		SetCenterOfMass(Props.CenterOfMass());
		SetRotationOfMass(Props.RotationOfMass());
		SetI(Props.I());
		SetInvI(Props.InvI());
		SetM(Props.M());
		SetInvM(Props.InvM());
	}

	void SetDynamicMisc(const FParticleDynamicMisc& DynamicMisc)
	{
		SetLinearEtherDrag(DynamicMisc.LinearEtherDrag());
		SetAngularEtherDrag(DynamicMisc.AngularEtherDrag());
		SetCollisionGroup(DynamicMisc.CollisionGroup());
		SetGravityEnabled(DynamicMisc.GravityEnabled());
		SetResimType(DynamicMisc.ResimType());
	}

	void ResetSmoothedVelocities()
	{
		SetVSmooth(V());
		SetWSmooth(W());
	}

	const PMatrix<T, d, d>& I() const { return PBDRigidParticles->I(ParticleIdx); }
	PMatrix<T, d, d>& I() { return PBDRigidParticles->I(ParticleIdx); }
	void SetI(const PMatrix<T, d, d>& InI) { PBDRigidParticles->I(ParticleIdx) = InI; }

	const PMatrix<T, d, d>& InvI() const { return PBDRigidParticles->InvI(ParticleIdx); }
	PMatrix<T, d, d>& InvI() { return PBDRigidParticles->InvI(ParticleIdx); }
	void SetInvI(const PMatrix<T, d, d>& InInvI) { PBDRigidParticles->InvI(ParticleIdx) = InInvI; }

	T M() const { return PBDRigidParticles->M(ParticleIdx); }
	T& M() { return PBDRigidParticles->M(ParticleIdx); }
	void SetM(const T& InM) { PBDRigidParticles->M(ParticleIdx) = InM; }

	T InvM() const { return PBDRigidParticles->InvM(ParticleIdx); }
	T& InvM() { return PBDRigidParticles->InvM(ParticleIdx); }
	void SetInvM(const T& InInvM) { PBDRigidParticles->InvM(ParticleIdx) = InInvM; }

	const TVector<T,d>& CenterOfMass() const { return PBDRigidParticles->CenterOfMass(ParticleIdx); }
	void SetCenterOfMass(const TVector<T,d>& InCenterOfMass) { PBDRigidParticles->CenterOfMass(ParticleIdx) = InCenterOfMass; }

	const TRotation<T,d>& RotationOfMass() const { return PBDRigidParticles->RotationOfMass(ParticleIdx); }
	void SetRotationOfMass(const TRotation<T,d>& InRotationOfMass) { PBDRigidParticles->RotationOfMass(ParticleIdx) = InRotationOfMass; }


	T LinearEtherDrag() const { return PBDRigidParticles->LinearEtherDrag(ParticleIdx); }
	T& LinearEtherDrag() { return PBDRigidParticles->LinearEtherDrag(ParticleIdx); }
	void SetLinearEtherDrag(const T& InLinearEtherDrag) { PBDRigidParticles->LinearEtherDrag(ParticleIdx) = InLinearEtherDrag; }

	T AngularEtherDrag() const { return PBDRigidParticles->AngularEtherDrag(ParticleIdx); }
	T& AngularEtherDrag() { return PBDRigidParticles->AngularEtherDrag(ParticleIdx); }
	void SetAngularEtherDrag(const T& InAngularEtherDrag) { PBDRigidParticles->AngularEtherDrag(ParticleIdx) = InAngularEtherDrag; }

	int32 Island() const { return PBDRigidParticles->Island(ParticleIdx); }
	int32& Island() { return PBDRigidParticles->Island(ParticleIdx); }
	void SetIsland(const int32 InIsland) { PBDRigidParticles->Island(ParticleIdx) = InIsland; }

	bool ToBeRemovedOnFracture() const { return PBDRigidParticles->ToBeRemovedOnFracture(ParticleIdx); }
	bool& ToBeRemovedOnFracture() { return PBDRigidParticles->ToBeRemovedOnFracture(ParticleIdx); }
	void SetToBeRemovedOnFracture(const bool bToBeRemovedOnFracture) { PBDRigidParticles->ToBeRemovedOnFracture(ParticleIdx) = bToBeRemovedOnFracture; }

	EObjectStateType ObjectState() const { return PBDRigidParticles->ObjectState(ParticleIdx); }
	void SetObjectState(EObjectStateType InState) { PBDRigidParticles->SetObjectState(ParticleIdx, InState); }
	void SetObjectStateLowLevel(EObjectStateType InState) { PBDRigidParticles->SetObjectState(ParticleIdx, InState); }
	
	bool Sleeping() const { return PBDRigidParticles->Sleeping(ParticleIdx); }
	void SetSleeping(bool bSleeping) { PBDRigidParticles->SetSleeping(ParticleIdx, bSleeping); }

	//Really only useful when using a transient handle
	const TPBDRigidParticleHandleImp<T, d, true>* Handle() const { return PBDRigidParticles->Handle(ParticleIdx); }
	TPBDRigidParticleHandleImp<T, d, true>* Handle() { return PBDRigidParticles->Handle(ParticleIdx); }

	bool GravityEnabled() const { return PBDRigidParticles->GravityEnabled(ParticleIdx); }

	void SetGravityEnabled(bool bEnabled){ PBDRigidParticles->GravityEnabled(ParticleIdx) = bEnabled; }

	EResimType ResimType() const { return PBDRigidParticles->ResimType(ParticleIdx);}

	void SetResimType(EResimType ResimType){ PBDRigidParticles->ResimType(ParticleIdx) = ResimType; }



	static constexpr EParticleType StaticType() { return EParticleType::Rigid; }
};

template <typename T, int d, bool bPersistent>
class TPBDRigidClusteredParticleHandleImp : public TPBDRigidParticleHandleImp<T, d, bPersistent>
{
public:
	using TGeometryParticleHandleImp<T, d, bPersistent>::ParticleIdx;
	using TGeometryParticleHandleImp<T, d, bPersistent>::PBDRigidClusteredParticles;
	using TGeometryParticleHandleImp<T, d, bPersistent>::Type;
	using TTransientHandle = TTransientPBDRigidParticleHandle<T, d>;
	using TSOAType = TPBDRigidClusteredParticles<T, d>;

protected:
	friend class TGeometryParticleHandleImp<T, d, bPersistent>;
	//needed for serialization
	TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>()
	: TPBDRigidParticleHandleImp<T, d, bPersistent>()
	{
	}

	TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>(TSerializablePtr<TPBDRigidClusteredParticles<T, d>> Particles, int32 InIdx, int32 InGlobalIdx, const TPBDRigidParticleParameters<T, d>& Params)
		: TPBDRigidParticleHandleImp<T, d, bPersistent>(TSerializablePtr<TPBDRigidParticles<T, d>>(Particles), InIdx, InGlobalIdx, Params)
	{
		PBDRigidClusteredParticleDefaultConstruct<T, d>(*this, Params);
	}
public:

	static TUniquePtr<TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>> CreateParticleHandle(TSerializablePtr<TPBDRigidClusteredParticles<T, d>> InParticles, int32 InParticleIdx, int32 InHandleIdx, const TPBDRigidParticleParameters<T, d>& Params = TPBDRigidParticleParameters<T, d>())
	{
		return TGeometryParticleHandleImp<T, d, bPersistent>::CreateParticleHandleHelper(InParticles, InParticleIdx, InHandleIdx, Params);
	}

	TSerializablePtr <TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>> ToSerializable() const
	{
		TSerializablePtr<TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>> Serializable;
		Serializable.SetFromRawLowLevel(this);	//this is safe because CreateParticleHandle gives back a TUniquePtr
		return Serializable;
	}

	void SetClusterId(const ClusterId& Id) { PBDRigidClusteredParticles->ClusterIds(ParticleIdx) = Id; }
	const ClusterId& ClusterIds() const { return PBDRigidClusteredParticles->ClusterIds(ParticleIdx); }
	ClusterId& ClusterIds() { return PBDRigidClusteredParticles->ClusterIds(ParticleIdx); }

	const TRigidTransform<T,d>& ChildToParent() const { return PBDRigidClusteredParticles->ChildToParent(ParticleIdx); }
	TRigidTransform<T,d>& ChildToParent() { return PBDRigidClusteredParticles->ChildToParent(ParticleIdx); }
	void SetChildToParent(const TRigidTransform<T, d>& Xf) { PBDRigidClusteredParticles->ChildToParent(ParticleIdx) = Xf; }

	const int32& ClusterGroupIndex() const { return PBDRigidClusteredParticles->ClusterGroupIndex(ParticleIdx); }
	int32& ClusterGroupIndex() { return PBDRigidClusteredParticles->ClusterGroupIndex(ParticleIdx); }
	void SetClusterGroupIndex(const int32 Idx) { PBDRigidClusteredParticles->ClusterGroupIndex(ParticleIdx) = Idx; }

	const bool& InternalCluster() const { return PBDRigidClusteredParticles->InternalCluster(ParticleIdx); }
	bool& InternalCluster() { return PBDRigidClusteredParticles->InternalCluster(ParticleIdx); }
	void SetInternalCluster(const bool Value) { PBDRigidClusteredParticles->InternalCluster(ParticleIdx) = Value; }

	const TUniquePtr<FImplicitObjectUnionClustered>& ChildrenSpatial() const { return PBDRigidClusteredParticles->ChildrenSpatial(ParticleIdx); }
	TUniquePtr<FImplicitObjectUnionClustered>& ChildrenSpatial() { return PBDRigidClusteredParticles->ChildrenSpatial(ParticleIdx); }
	void SetChildrenSpatial(TUniquePtr<FImplicitObjectUnion>& Obj) { PBDRigidClusteredParticles->ChildrenSpatial(ParticleIdx) = Obj; }

	const FMultiChildProxyId& MultiChildProxyId() const { return PBDRigidClusteredParticles->MultiChildProxyId(ParticleIdx); }
	FMultiChildProxyId& MultiChildProxyId() { return PBDRigidClusteredParticles->MultiChildProxyId(ParticleIdx); }
	void SetMultiChildProxyId(const FMultiChildProxyId& Id) { PBDRigidClusteredParticles->MultiChildProxyId(ParticleIdx) = Id; }

	const TUniquePtr<TMultiChildProxyData<T, d>>& MultiChildProxyData() const { return PBDRigidClusteredParticles->MultiChildProxyData(ParticleIdx); }
	TUniquePtr<TMultiChildProxyData<T, d>>& MultiChildProxyData() { return PBDRigidClusteredParticles->MultiChildProxyData(ParticleIdx); }
	void SetMultiChildProxyData(TUniquePtr<TMultiChildProxyData<T, d>>&& ProxyData) { PBDRigidClusteredParticles->MultiChildProxyData(ParticleIdx) = MoveTemp(ProxyData); }

	const T& CollisionImpulse() const { return PBDRigidClusteredParticles->CollisionImpulses(ParticleIdx); }
	T& CollisionImpulse() { return PBDRigidClusteredParticles->CollisionImpulses(ParticleIdx); }
	void SetCollisionImpulse(const T Value) { PBDRigidClusteredParticles->CollisionImpulses(ParticleIdx) = Value; }
	const T& CollisionImpulses() const { return PBDRigidClusteredParticles->CollisionImpulses(ParticleIdx); }
	T& CollisionImpulses() { return PBDRigidClusteredParticles->CollisionImpulses(ParticleIdx); }
	void SetCollisionImpulses(const T Value) { PBDRigidClusteredParticles->CollisionImpulses(ParticleIdx) = Value; }

	const T& Strain() const { return PBDRigidClusteredParticles->Strains(ParticleIdx); }
	T& Strain() { return PBDRigidClusteredParticles->Strains(ParticleIdx); }
	void SetStrain(const T Value) { PBDRigidClusteredParticles->Strains(ParticleIdx) = Value; }
	const T& Strains() const { return PBDRigidClusteredParticles->Strains(ParticleIdx); }
	T& Strains() { return PBDRigidClusteredParticles->Strains(ParticleIdx); }
	void SetStrains(const T Value) { PBDRigidClusteredParticles->Strains(ParticleIdx) = Value; }

	const TArray<TConnectivityEdge<T>>& ConnectivityEdges() const { return PBDRigidClusteredParticles->ConnectivityEdges(ParticleIdx); }
	TArray<TConnectivityEdge<T>>& ConnectivityEdges() { return PBDRigidClusteredParticles->ConnectivityEdges(ParticleIdx); }
	void SetConnectivityEdges(const TArray<TConnectivityEdge<T>>& Edges) { PBDRigidClusteredParticles->ConnectivityEdges(ParticleIdx) = Edges; }
	void SetConnectivityEdges(TArray<TConnectivityEdge<T>>&& Edges) { PBDRigidClusteredParticles->ConnectivityEdges(ParticleIdx) = MoveTemp(Edges); }

	const TPBDRigidClusteredParticleHandleImp<T, d, true>* Handle() const { return PBDRigidClusteredParticles->Handle(ParticleIdx); }
	TPBDRigidClusteredParticleHandleImp<T, d, true>* Handle() { return PBDRigidClusteredParticles->Handle(ParticleIdx); }

	static constexpr EParticleType StaticType() { return EParticleType::Rigid; }

	int32 TransientParticleIndex() const { return ParticleIdx; }
};

template <typename T, int d, bool bPersistent = true>
class TPBDGeometryCollectionParticleHandleImp : public TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>
{
public:
	using TGeometryParticleHandleImp<T, d, bPersistent>::ParticleIdx;
	using TGeometryParticleHandleImp<T, d, bPersistent>::PBDRigidParticles;
	using TGeometryParticleHandleImp<T, d, bPersistent>::Type;
	using TTransientHandle = TTransientPBDGeometryCollectionParticleHandle<T, d>;
	using TSOAType = TPBDGeometryCollectionParticles<T, d>;

protected:
	friend class TGeometryParticleHandleImp<T, d, bPersistent>;

	//needed for serialization
	TPBDGeometryCollectionParticleHandleImp()
		: TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>()
	{}

	TPBDGeometryCollectionParticleHandleImp<T, d, bPersistent>(
		TSerializablePtr<TPBDGeometryCollectionParticles<T, d>> Particles, 
		int32 InIdx, 
		int32 InGlobalIdx, 
		const TPBDRigidParticleParameters<T, d>& Params = TPBDRigidParticleParameters<T, d>())
		: TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>(
			TSerializablePtr<TPBDRigidClusteredParticles<T, d>>(Particles), InIdx, InGlobalIdx, Params)
	{}
public:

	static TUniquePtr<TPBDGeometryCollectionParticleHandleImp<T, d, bPersistent>> CreateParticleHandle(
		TSerializablePtr<TPBDGeometryCollectionParticles<T, d>> InParticles, 
		int32 InParticleIdx, 
		int32 InHandleIdx, 
		const TPBDRigidParticleParameters<T, d>& Params = TPBDRigidParticleParameters<T, d>())
	{
		return TGeometryParticleHandleImp<T, d, bPersistent>::CreateParticleHandleHelper(
			InParticles, InParticleIdx, InHandleIdx, Params);
	}

	TSerializablePtr<TPBDGeometryCollectionParticleHandleImp<T, d, bPersistent>> ToSerializable() const
	{
		TSerializablePtr<TPBDGeometryCollectionParticleHandleImp<T, d, bPersistent>> Serializable;
		Serializable.SetFromRawLowLevel(this);
		return Serializable;
	}

	const TPBDGeometryCollectionParticleHandleImp<T, d, true>* Handle() const { return PBDRigidParticles->Handle(ParticleIdx); }
	TPBDGeometryCollectionParticleHandleImp<T, d, true>* Handle() { return PBDRigidParticles->Handle(ParticleIdx); }

	static constexpr EParticleType StaticType() { return EParticleType::GeometryCollection; }
};

template <typename T, int d, bool bPersistent>
TKinematicGeometryParticleHandleImp<T, d, bPersistent>* TGeometryParticleHandleImp<T, d, bPersistent>::CastToKinematicParticle() { return Type >= EParticleType::Kinematic ? static_cast<TKinematicGeometryParticleHandleImp<T, d, bPersistent>*>(this) : nullptr; }

template <typename T, int d, bool bPersistent>
const TKinematicGeometryParticleHandleImp<T, d, bPersistent>* TGeometryParticleHandleImp<T,d, bPersistent>::CastToKinematicParticle() const { return Type >= EParticleType::Kinematic ? static_cast<const TKinematicGeometryParticleHandleImp<T, d, bPersistent>*>(this) : nullptr; }

template <typename T, int d, bool bPersistent>
const TPBDRigidParticleHandleImp<T, d, bPersistent>* TGeometryParticleHandleImp<T, d, bPersistent>::CastToRigidParticle() const { return Type >= EParticleType::Rigid ? static_cast<const TPBDRigidParticleHandleImp<T, d, bPersistent>*>(this) : nullptr; }

template <typename T, int d, bool bPersistent>
TPBDRigidParticleHandleImp<T, d, bPersistent>* TGeometryParticleHandleImp<T, d, bPersistent>::CastToRigidParticle() { return Type >= EParticleType::Rigid ? static_cast<TPBDRigidParticleHandleImp<T, d, bPersistent>*>(this) : nullptr; }

template <typename T, int d, bool bPersistent>
const TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>* TGeometryParticleHandleImp<T, d, bPersistent>::CastToClustered() const { return Type >= EParticleType::Clustered ? static_cast<const TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>*>(this) : nullptr; }

template <typename T, int d, bool bPersistent>
TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>* TGeometryParticleHandleImp<T, d, bPersistent>::CastToClustered() { return Type >= EParticleType::Clustered ? static_cast<TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>*>(this) : nullptr; }

template <typename T, int d, bool bPersistent>
EObjectStateType TGeometryParticleHandleImp<T,d, bPersistent>::ObjectState() const
{
	const TKinematicGeometryParticleHandleImp<T, d, bPersistent>* Kin = CastToKinematicParticle();
	return Kin ? Kin->ObjectState() : EObjectStateType::Static;
}

template <typename T, int d, bool bPersistent>
EObjectStateType TKinematicGeometryParticleHandleImp<T, d, bPersistent>::ObjectState() const
{
	const TPBDRigidParticleHandleImp<T, d, bPersistent>* Dyn = CastToRigidParticle();
	return Dyn ? Dyn->ObjectState() : EObjectStateType::Kinematic;
}

enum class EWakeEventEntry : uint8
{
	None,
	Awake,
	Sleep
};

template <typename T, int d, bool bPersistent>
FString TGeometryParticleHandleImp<T, d, bPersistent>::ToString() const
{
	switch (Type)
	{
	case EParticleType::Static:
		return FString::Printf(TEXT("Static[%d]"), ParticleIdx);
	case EParticleType::Kinematic:
		return FString::Printf(TEXT("Kinemmatic[%d]"), ParticleIdx);
	case EParticleType::Rigid:
		return FString::Printf(TEXT("Dynamic[%d]"), ParticleIdx);
	case EParticleType::GeometryCollection:
		return FString::Printf(TEXT("GeometryCollection[%d]"), ParticleIdx);
	case EParticleType::Clustered:
		return FString::Printf(TEXT("Clustered[%d]"), ParticleIdx);
	default:
		break;
	}
	return FString();
}

template <typename T, int d, bool bPersistent>
TGeometryParticleHandleImp<T,d,bPersistent>* TGeometryParticleHandleImp<T,d, bPersistent>::SerializationFactory(FChaosArchive& Ar, TGeometryParticleHandleImp<T, d, bPersistent>* Handle)
{
	check(bPersistent);
	static_assert(sizeof(TGeometryParticleHandleImp<T, d, bPersistent>) == sizeof(TGeometryParticleHandleImp<T, d, bPersistent>), "No new members in derived classes");
	return Ar.IsLoading() ? new TGeometryParticleHandleImp<T, d, bPersistent>() : nullptr;
}

template <typename T, int d>
class TGenericParticleHandleHandleImp
{
public:
	TGenericParticleHandleHandleImp(TGeometryParticleHandle<T, d>* InHandle) { MHandle = InHandle; }

	// Check for the exact type of particle (see also AsKinematic etc, which will work on derived types)
	bool IsStatic() const { return (MHandle->ObjectState() == EObjectStateType::Static); }
	bool IsKinematic() const { return (MHandle->ObjectState() == EObjectStateType::Kinematic); }
	bool IsDynamic() const { return (MHandle->ObjectState() == EObjectStateType::Dynamic) || (MHandle->ObjectState() == EObjectStateType::Sleeping); }

	const TKinematicGeometryParticleHandle<T, d>* CastToKinematicParticle() const { return MHandle->CastToKinematicParticle(); }
	TKinematicGeometryParticleHandle<T, d>* CastToKinematicParticle() { return MHandle->CastToKinematicParticle(); }
	const TPBDRigidParticleHandle<T, d>* CastToRigidParticle() const { return MHandle->CastToRigidParticle(); }
	TPBDRigidParticleHandle<T, d>* CastToRigidParticle() { return MHandle->CastToRigidParticle(); }
	const TGeometryParticleHandle<T, d>* GeometryParticleHandle() const { return MHandle; }
	TGeometryParticleHandle<T, d>* GeometryParticleHandle() { return MHandle; }
	//Needed for templated code to be the same
	const TGeometryParticleHandle<T, d>* Handle() const { return MHandle; }
	TGeometryParticleHandle<T, d>* Handle() { return MHandle; }

	// Static Particles
	TVector<T, d>& X() { return MHandle->X(); }
	const TVector<T, d>& X() const { return MHandle->X(); }
	TRotation<T, d>& R() { return MHandle->R(); }
	const TRotation<T, d>& R() const { return MHandle->R(); }
	TSerializablePtr<FImplicitObject> Geometry() const { return MHandle->Geometry(); }
	const TUniquePtr<FImplicitObject>& DynamicGeometry() const { return MHandle->DynamicGeometry(); }
	bool Sleeping() const { return MHandle->Sleeping(); }
	FString ToString() const { return MHandle->ToString(); }

	template <typename Container>
	const auto& AuxilaryValue(const Container& AuxContainer) const { return MHandle->AuxilaryValue(AuxContainer); }
	template <typename Container>
	auto& AuxilaryValue(Container& AuxContainer) { return MHandle->AuxilaryValue(AuxContainer); }

	// Kinematic Particles
	const TVector<T, d>& V() const { return (MHandle->CastToKinematicParticle()) ? MHandle->CastToKinematicParticle()->V() : ZeroVector; }
	const TVector<T, d>& W() const { return (MHandle->CastToKinematicParticle()) ? MHandle->CastToKinematicParticle()->W() : ZeroVector; }

	void SetV(const FVec3& InV) { if (MHandle->CastToKinematicParticle()) { MHandle->CastToKinematicParticle()->V() = InV; } }
	void SetW(const FVec3& InW) { if (MHandle->CastToKinematicParticle()) { MHandle->CastToKinematicParticle()->W() = InW; } }

	// Dynamic Particles

	// TODO: Make all of these check ObjectState to maintain current functionality
	int32 CollisionParticlesSize() const
	{
		if (MHandle->CastToRigidParticle() && MHandle->ObjectState() == EObjectStateType::Dynamic)
		{
			return MHandle->CastToRigidParticle()->CollisionParticlesSize();
		}

		return 0;
	}

	const TUniquePtr<TBVHParticles<T, d>>& CollisionParticles() const
	{
		if (MHandle->CastToRigidParticle() && MHandle->ObjectState() == EObjectStateType::Dynamic)
		{
			return MHandle->CastToRigidParticle()->CollisionParticles();
		}

		return NullBVHParticles;
	}

	int32 CollisionGroup() const
	{
		if (MHandle->CastToRigidParticle() && MHandle->ObjectState() == EObjectStateType::Dynamic)
		{
			return MHandle->CastToRigidParticle()->CollisionGroup();
		}

		return 0;
	}

	bool HasCollisionConstraintFlag(const ECollisionConstraintFlags Flag)  const
	{
		if (MHandle->CastToRigidParticle() && MHandle->ObjectState() == EObjectStateType::Dynamic)
		{
			return MHandle->CastToRigidParticle()->HasCollisionConstraintFlag(Flag);
		}

		return false;
	}


	// @todo(ccaulfield): should be available on all types?
	bool Disabled() const
	{
		if (MHandle->CastToRigidParticle() && MHandle->ObjectState() == EObjectStateType::Dynamic)
		{
			return MHandle->CastToRigidParticle()->Disabled();
		}

		return false;
	}

	// @todo(ccaulfield): should be available on kinematics?
	const TVector<T, d>& PreV() const
	{
		if (MHandle->CastToRigidParticle() && MHandle->ObjectState() == EObjectStateType::Dynamic)
		{
			return MHandle->CastToRigidParticle()->PreV();
		}

		return ZeroVector;
	}

	// @todo(ccaulfield): should be available on kinematics?
	const TVector<T, d>& PreW() const
	{
		if (MHandle->CastToRigidParticle() && MHandle->ObjectState() == EObjectStateType::Dynamic)
		{
			return MHandle->CastToRigidParticle()->PreW();
		}
		return ZeroVector;
	}

	TVector<T, d>& P()
	{
		if (MHandle->CastToRigidParticle() && MHandle->ObjectState() == EObjectStateType::Dynamic)
		{
			return MHandle->CastToRigidParticle()->P();
		}

		return X();
	}

	const TVector<T, d>& P() const
	{
		if (MHandle->CastToRigidParticle() && MHandle->ObjectState() == EObjectStateType::Dynamic)
		{
			return MHandle->CastToRigidParticle()->P();
		}

		return X();
	}

	TRotation<T, d>& Q()
	{
		if (MHandle->CastToRigidParticle() && MHandle->ObjectState() == EObjectStateType::Dynamic)
		{
			return MHandle->CastToRigidParticle()->Q();
		}

		return R();
	}

	const TRotation<T, d>& Q() const
	{
		if (MHandle->CastToRigidParticle() && MHandle->ObjectState() == EObjectStateType::Dynamic)
		{
			return MHandle->CastToRigidParticle()->Q();
		}

		return R();
	}

	const TVector<T, d>& VSmooth() const
	{
		if (MHandle->CastToRigidParticle())
		{
			return MHandle->CastToRigidParticle()->VSmooth();
		}

		return V();
	}

	const TVector<T, d>& WSmooth() const
	{
		if (MHandle->CastToRigidParticle())
		{
			return MHandle->CastToRigidParticle()->WSmooth();
		}

		return W();
	}

	const TVector<T, d>& F() const 
	{ 
		if (MHandle->CastToRigidParticle() && MHandle->ObjectState() == EObjectStateType::Dynamic)
		{
			return MHandle->CastToRigidParticle()->F();
		}

		return ZeroVector;
	}
	const TVector<T, d>& Torque() const 
	{ 
		if (MHandle->CastToRigidParticle() && MHandle->ObjectState() == EObjectStateType::Dynamic)
		{
			return MHandle->CastToRigidParticle()->Torque();
		}

		return ZeroVector;
	}

	const EObjectStateType ObjectState()  const
	{
		return MHandle->ObjectState();
	}

	FParticleID ParticleID() const
	{
		return MHandle->ParticleID();
	}

	FUniqueIdx UniqueIdx() const
	{
		return MHandle->UniqueIdx();
	}

	//Named this way for templated code
	bool HasBoundingBox() const
	{
		return MHandle->HasBounds();
	}

	//Named this way for templated code
	const TAABB<T,d>& BoundingBox() const
	{
		return MHandle->WorldSpaceInflatedBounds();
	}

	const PMatrix<T, d, d>& I() const 
	{ 
		if (MHandle->CastToRigidParticle() && MHandle->ObjectState() == EObjectStateType::Dynamic)
		{
			return MHandle->CastToRigidParticle()->I();
		}

		return ZeroMatrix;
	}

	const PMatrix<T, d, d>& InvI() const 
	{ 
		if (MHandle->CastToRigidParticle() && MHandle->ObjectState() == EObjectStateType::Dynamic)
		{
			return MHandle->CastToRigidParticle()->InvI();
		}

		return ZeroMatrix;
	}

	T M() const
	{
		if (MHandle->CastToRigidParticle() && MHandle->ObjectState() == EObjectStateType::Dynamic)
		{
			return MHandle->CastToRigidParticle()->M();
		}

		return (T)0;
	}

	T InvM() const
	{
		if (MHandle->CastToRigidParticle() && MHandle->ObjectState() == EObjectStateType::Dynamic)
		{
			return MHandle->CastToRigidParticle()->InvM();
		}

		return (T)0;
	}

	TVector<T, d> CenterOfMass() const
	{
		if (auto RigidHandle = MHandle->CastToRigidParticle())
		{
			return RigidHandle->CenterOfMass();
		}

		return TVector<T, d>(0);
	}

	TRotation<T, d> RotationOfMass() const
	{
		if (auto RigidHandle = MHandle->CastToRigidParticle())
		{
			return RigidHandle->RotationOfMass();
		}

		return TRotation<T, d>::FromIdentity();
	}

	T LinearEtherDrag() const
	{
		if (auto RigidHandle = MHandle->CastToRigidParticle())
		{
			return RigidHandle->LinearEtherDrag();
		}
		return 0.0f;
	}

	T AngularEtherDrag() const
	{
		if (auto RigidHandle = MHandle->CastToRigidParticle())
		{
			return RigidHandle->AngularEtherDrag();
		}
		return 0.0f;
	}


#if CHAOS_CHECKED
	const FName& DebugName() const
	{
		return MHandle->DebugName();
	}
#endif

	int32 Island() const
	{
		if (MHandle->CastToRigidParticle() && MHandle->ObjectState() == EObjectStateType::Dynamic)
		{
			return MHandle->CastToRigidParticle()->Island();
		}

		return INDEX_NONE;
	}

	bool ToBeRemovedOnFracture() const 
	{
		if (MHandle->CastToRigidParticle() && MHandle->ObjectState() == EObjectStateType::Dynamic)
		{
			return MHandle->CastToRigidParticle()->ToBeRemovedOnFracture();
		}

		return false;
	}

	static constexpr EParticleType StaticType()
	{
		return EParticleType::Unknown;
	}

private:
	TGeometryParticleHandle<T, d>* MHandle;

	static const TVector<T, d> ZeroVector;
	static const TRotation<T, d> IdentityRotation;
	static const PMatrix<T, d, d> ZeroMatrix;
	static const TUniquePtr<TBVHParticles<T, d>> NullBVHParticles;
};

/**
 * A wrapper around any type of particle handle to provide a consistent (read-only) API for all particle types.
 * This can make code simpler because you can write code that is type-agnostic, but it
 * has a cost. Where possible it is better to write code that is specific to the type(s)
 * of particles being operated on. TGenericParticleHandle has pointer semantics, so you can use one wherever
 * you have a particle handle pointer;
 *
 */
template <typename T, int d>
class TGenericParticleHandle
{
public:
	TGenericParticleHandle(TGeometryParticleHandle<T, d>* InHandle) : Imp(InHandle) {}

	TGenericParticleHandleHandleImp<T, d>* operator->() const { return const_cast<TGenericParticleHandleHandleImp<T, d>*>(&Imp); }
	TGenericParticleHandleHandleImp<T, d>* Get() const { return const_cast<TGenericParticleHandleHandleImp<T, d>*>(&Imp); }

private:
	TGenericParticleHandleHandleImp<T, d> Imp;
};

template <typename T, int d>
class TConstGenericParticleHandle
{
public:
	TConstGenericParticleHandle(const TGeometryParticleHandle<T, d>* InHandle) : Imp(const_cast<TGeometryParticleHandle<T, d>*>(InHandle)) {}

	const TGenericParticleHandleHandleImp<T, d>* operator->() const { return &Imp; }

private:
	TGenericParticleHandleHandleImp<T, d> Imp;
};

template <typename T, int d>
const TVector<T, d> TGenericParticleHandleHandleImp<T, d>::ZeroVector = TVector<T, d>(0);

template <typename T, int d>
const TRotation<T, d> TGenericParticleHandleHandleImp<T, d>::IdentityRotation = TRotation<T, d>(0, 0, 0, 1);

template <typename T, int d>
const PMatrix<T, d, d> TGenericParticleHandleHandleImp<T, d>::ZeroMatrix = PMatrix<T, d, d>(0);

template <typename T, int d>
const TUniquePtr<TBVHParticles<T, d>> TGenericParticleHandleHandleImp<T, d>::NullBVHParticles = TUniquePtr<TBVHParticles<T, d>>();



template <typename T, int d>
class TGeometryParticleHandles : public TArrayCollection
{
public:
	TGeometryParticleHandles()
	{
		AddArray(&Handles);
	}

	void AddHandles(int32 NumHandles)
	{
		AddElementsHelper(NumHandles);
	}

	void Reset()
	{
		ResizeHelper(0);
	}

	void DestroyHandleSwap(TGeometryParticleHandle<T,d>* Handle)
	{
		const int32 UnstableIdx = Handle->HandleIdx;
		RemoveAtSwapHelper(UnstableIdx);
		if (static_cast<uint32>(UnstableIdx) < Size())
		{
			Handles[UnstableIdx]->HandleIdx = UnstableIdx;
		}
	}

	void Serialize(FChaosArchive& Ar)
	{
		Ar << Handles;
		ResizeHelper(Handles.Num());
	}

	const TUniquePtr<TGeometryParticleHandle<T, d>>& Handle(int32 Idx) const { return Handles[Idx];}
	TUniquePtr<TGeometryParticleHandle<T, d>>& Handle(int32 Idx) { return Handles[Idx]; }
private:
	TArrayCollectionArray<TUniquePtr<TGeometryParticleHandleImp<T, d, true>>> Handles;
};

template <typename T, int d>
class TKinematicGeometryParticle;

template <typename T, int d>
class TPBDRigidParticle;


/**
 * Base class for transient classes used to communicate simulated particle state 
 * between game and physics threads, which is managed by proxies.
 *
 * Note the lack of virtual api.
 */
class FParticleData
{
public:
	FParticleData(EParticleType InType = EParticleType::Static)
		: Type(InType)
	{}

	void Reset() { Type = EParticleType::Static; }

	virtual ~FParticleData() = default;

	EParticleType Type;
};

template <typename T, int d> 
class TGeometryParticleData;

template <typename T, int d>
class TKinematicGeometryParticleData;

template <typename T, int d>
class TPBDRigidParticleData;


template <typename T, int d>
class TGeometryParticle
{
public:
	typedef TGeometryParticleData<T, d> FData;
	typedef TGeometryParticleHandle<T, d> FHandle;

	friend FData;

	static constexpr bool AlwaysSerializable = true;

protected:

	TGeometryParticle(const TGeometryParticleParameters<T, d>& StaticParams = TGeometryParticleParameters<T, d>())
	{
		Type = EParticleType::Static;
		Proxy = nullptr;
		GeometryParticleDefaultConstruct<T, d>(*this, StaticParams);
	}

public:
	static TUniquePtr<TGeometryParticle<T, d>> CreateParticle(const TGeometryParticleParameters<T, d>& Params = TGeometryParticleParameters<T, d>())
	{
		return TUniquePtr< TGeometryParticle<T, d>>(new TGeometryParticle<T, d>(Params));
	}

	virtual ~TGeometryParticle() {}	//only virtual for easier memory management. Should generally be a static API

	TGeometryParticle(const TGeometryParticle&) = delete;

	TGeometryParticle& operator=(const TGeometryParticle&) = delete;

	virtual void Serialize(FChaosArchive& Ar)
	{
		Ar << MXR;
		Ar << MNonFrequentData;
		Ar << MShapesArray;
		Ar << Type;
		//Ar << MDirtyFlags;

		Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
		if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::SerializeShapeWorldSpaceBounds)
		{
			UpdateShapeBounds();
		}

		if(Ar.IsLoading())
		{
			MapImplicitShapes();
		}
	}

	virtual bool IsParticleValid() const
	{
		auto& Geometry = MNonFrequentData.Read().Geometry();
		return Geometry && Geometry->IsValidGeometry();	//todo: if we want support for sample particles without geometry we need to adjust this
	}

	static TGeometryParticle<T, d>* SerializationFactory(FChaosArchive& Ar, TGeometryParticle<T, d>* Serializable);

	const TVector<T, d>& X() const { return MXR.Read().X(); }
	void SetX(const TVector<T, d>& InX, bool bInvalidate = true);

	FUniqueIdx UniqueIdx() const { return MNonFrequentData.Read().UniqueIdx(); }
	void SetUniqueIdx(const FUniqueIdx UniqueIdx, bool bInvalidate = true)
	{
		MNonFrequentData.Modify(bInvalidate,MDirtyFlags,Proxy,[UniqueIdx](auto& Data){ Data.SetUniqueIdx(UniqueIdx);});
	}

	const TRotation<T, d>& R() const { return MXR.Read().R(); }
	void SetR(const TRotation<T, d>& InR, bool bInvalidate = true);

	void SetXR(const FParticlePositionRotation& InXR, bool bInvalidate = true)
	{
		MXR.Write(InXR,bInvalidate,MDirtyFlags,Proxy);
	}
	
	//todo: geometry should not be owned by particle
	void SetGeometry(TUniquePtr<FImplicitObject>&& UniqueGeometry)
	{
		// Take ownership of the geometry, putting it into a shared ptr.
		// This is necessary because we cannot be sure whether the particle
		// will be destroyed on the game thread or physics thread first,
		// but geometry data is shared between them.
		FImplicitObject* RawGeometry = UniqueGeometry.Release();
		SetGeometry(TSharedPtr<FImplicitObject, ESPMode::ThreadSafe>(RawGeometry));
	}

	// TODO: Right now this method exists so we can do things like FPhysTestSerializer::CreateChaosData.
	//       We should replace this with a method for supporting SetGeometry(RawGeometry).
	void SetGeometry(TSharedPtr<FImplicitObject, ESPMode::ThreadSafe> SharedGeometry)
	{
		MNonFrequentData.Modify(true,MDirtyFlags,Proxy,[&SharedGeometry](auto& Data){ Data.SetGeometry(SharedGeometry);});
		UpdateShapesArray();
	}

	void SetGeometry(TSerializablePtr<FImplicitObject> RawGeometry)
	{
		// Ultimately this method should replace SetGeometry(SharedPtr).
		// We don't really want people making shared ptrs to geometry everywhere.
		check(false);
	}

	void MergeGeometry(TArray<TUniquePtr<FImplicitObject>>&& Objects);

	void RemoveShape(FPerShapeData* InShape, bool bWakeTouching);

	const TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>& SharedGeometryLowLevel() const { return MNonFrequentData.Read().Geometry(); }

	void* UserData() const { return MNonFrequentData.Read().UserData(); }
	void SetUserData(void* InUserData)
	{
		MNonFrequentData.Modify(true,MDirtyFlags,Proxy,[InUserData](auto& Data){ Data.SetUserData(InUserData);});
	}

	void UpdateShapeBounds()
	{
		UpdateShapeBounds(FRigidTransform3(X(), R()));
	}

	void UpdateShapeBounds(const FTransform& Transform)
	{
		const TSharedPtr<FImplicitObject, ESPMode::ThreadSafe>& GeomShared = MNonFrequentData.Read().Geometry();
		if (GeomShared.IsValid() && GeomShared->HasBoundingBox())
		{
			for (auto& Shape : MShapesArray)
			{
				Shape->UpdateShapeBounds(Transform);
			}
		}
	}

	void SetShapeSimCollisionEnabled(int32 InShapeIndex, bool bInEnabled)
	{
		const bool bCurrent = MShapesArray[InShapeIndex]->GetSimEnabled();
		if(bCurrent != bInEnabled)
		{
			MShapesArray[InShapeIndex]->SetSimEnabled(bInEnabled);
		}
	}

	void SetShapeQueryCollisionEnabled(int32 InShapeIndex, bool bInEnabled)
	{
		const bool bCurrent = MShapesArray[InShapeIndex]->GetQueryEnabled();
		if(bCurrent != bInEnabled)
		{
			MShapesArray[InShapeIndex]->SetQueryEnabled(bInEnabled);
		}
	}

	void SetShapeCollisionTraceType(int32 InShapeIndex,EChaosCollisionTraceFlag TraceType)
	{
		const EChaosCollisionTraceFlag Current = MShapesArray[InShapeIndex]->GetCollisionTraceType();
		if(Current != TraceType)
		{
			MShapesArray[InShapeIndex]->SetCollisionTraceType(TraceType);
		}
	}

	void SetShapeSimData(int32 InShapeIndex,const FCollisionFilterData& SimData)
	{
		const FCollisionFilterData& Current = MShapesArray[InShapeIndex]->GetSimData();
		if(Current != SimData)
		{
			MShapesArray[InShapeIndex]->SetSimData(SimData);
		}
	}

#if CHAOS_CHECKED
	const FName DebugName() const { return MNonFrequentData.Read().DebugName(); }
	void SetDebugName(const FName& InDebugName)
	{
		MNonFrequentData.Modify(true,MDirtyFlags,Proxy,[&InDebugName](auto& Data){ Data.SetDebugName(InDebugName);});
	}
#endif

	//Note: this must be called after setting geometry. This API seems bad. Should probably be part of setting geometry
	void SetShapesArray(FShapesArray&& InShapesArray)
	{
		ensure(InShapesArray.Num() == MShapesArray.Num());
		MShapesArray = MoveTemp(InShapesArray);
		MapImplicitShapes();
	}

	void MergeShapesArray(FShapesArray&& OtherShapesArray)
	{
		int Idx = MShapesArray.Num() - OtherShapesArray.Num();
		for (TUniquePtr<FPerShapeData>& Shape : OtherShapesArray)
		{
			ensure(Idx < MShapesArray.Num());
			MShapesArray[Idx++] = MoveTemp(Shape);
		}
		MapImplicitShapes();
	}

	void SetIgnoreAnalyticCollisionsImp(FImplicitObject* Implicit, bool bIgnoreAnalyticCollisions);
	void SetIgnoreAnalyticCollisions(bool bIgnoreAnalyticCollisions)
	{
		if (MNonFrequentData.Read().Geometry())
		{
			SetIgnoreAnalyticCollisionsImp(MNonFrequentData.Read().Geometry().Get(), bIgnoreAnalyticCollisions);
		}
	}

	TSerializablePtr<FImplicitObject> Geometry() const { return MakeSerializable(MNonFrequentData.Read().Geometry()); }

	const FShapesArray& ShapesArray() const { return MShapesArray; }

	EObjectStateType ObjectState() const;
	void SetObjectState(const EObjectStateType InState, bool bAllowEvents = false, bool bInvalidate=true);

	EParticleType ObjectType() const
	{
		return Type;
	}


	const TKinematicGeometryParticle<T, d>* CastToKinematicParticle() const;
	TKinematicGeometryParticle<T, d>* CastToKinematicParticle();

	const TPBDRigidParticle<T, d>* CastToRigidParticle() const;
	TPBDRigidParticle<T, d>* CastToRigidParticle();

	FSpatialAccelerationIdx SpatialIdx() const { return MNonFrequentData.Read().SpatialIdx(); }
	void SetSpatialIdx(FSpatialAccelerationIdx Idx)
	{
		MNonFrequentData.Modify(true,MDirtyFlags,Proxy,[Idx](auto& Data){ Data.SetSpatialIdx(Idx);});
	}

	void SetNonFrequentData(const FParticleNonFrequentData& InData)
	{
		MNonFrequentData.Write(InData,true,MDirtyFlags,Proxy);
	}

	bool IsDirty() const
	{
		return MDirtyFlags.IsDirty();
	}

	bool IsClean() const
	{
		return MDirtyFlags.IsClean();
	}

	bool IsDirty(const EParticleFlags CheckBits) const
	{
		return MDirtyFlags.IsDirty(CheckBits);
	}

	const FParticleDirtyFlags& DirtyFlags() const
	{
		return MDirtyFlags;
	}

	void ClearDirtyFlags()
	{
		MDirtyFlags.Clear();
	}
	
	TGeometryParticleHandle<T, d>* Handle() const
	{
		if (Proxy)
		{
			return static_cast<TGeometryParticleHandle<T, d>*>(Proxy->GetHandleUnsafe());
		}

		return nullptr;
	}

	const FPerShapeData* GetImplicitShape(const FImplicitObject* InImplicit) const
	{
		const int32* ShapeIndex = ImplicitShapeMap.Find(InImplicit);
		if(ShapeIndex)
		{
			return MShapesArray[*ShapeIndex].Get();
		}

		return nullptr;
	}

	void SyncRemoteData(FDirtyPropertiesManager& Manager, int32 DataIdx, FParticleDirtyData& RemoteData, const TArray<int32>& ShapeDataIndices, FShapeDirtyData* ShapesRemoteData) const
	{
		RemoteData.SetFlags(MDirtyFlags);
		SyncRemoteDataImp(Manager, DataIdx, RemoteData);

		for(const int32 ShapeDataIdx : ShapeDataIndices)
		{
			FShapeDirtyData& ShapeRemoteData = ShapesRemoteData[ShapeDataIdx];
			const int32 ShapeIdx = ShapeRemoteData.GetShapeIdx();
			MShapesArray[ShapeIdx]->SyncRemoteData(Manager, ShapeDataIdx, ShapeRemoteData);
		}
	}


	class IPhysicsProxyBase* GetProxy() const
	{
		return Proxy;
	}

	void SetProxy(IPhysicsProxyBase* InProxy)
	{
		Proxy = InProxy;
		if(Proxy)
		{
			if(MDirtyFlags.IsDirty())
			{
				if(FPhysicsSolverBase* PhysicsSolverBase = Proxy->GetSolver<FPhysicsSolverBase>())
				{
					PhysicsSolverBase->AddDirtyProxy(Proxy);
				}
			}
		}

		for(auto& Shape : MShapesArray)
		{
			Shape->SetProxy(Proxy);
		}
	}

protected:

	// Pointer to any data that the solver wants to associate with this particle
	// TODO: It's important to eventually hide this!
	// Right now it's exposed to lubricate the creation of the whole proxy system.
	class IPhysicsProxyBase* Proxy;
private:

	TParticleProperty<FParticlePositionRotation, EParticleProperty::XR> MXR;
	TParticleProperty<FParticleNonFrequentData,EParticleProperty::NonFrequentData> MNonFrequentData;

	FShapesArray MShapesArray;
	TMap<const FImplicitObject*, int32> ImplicitShapeMap;


public:
	// Ryan: FGeometryCollectionPhysicsProxy needs access to GeometrySharedLowLevel(), 
	// as it needs access for the same reason as ParticleData.  For some reason
	// the friend declaration isn't working.  Exposing this function until this 
	// can be straightened out.
	//friend class FGeometryCollectionPhysicsProxy;
	// This is only for use by ParticleData. This should be called only in one place,
	// when the geometry is being copied from GT to PT.
	TSharedPtr<FImplicitObject, ESPMode::ThreadSafe> GeometrySharedLowLevel() const
	{
		return MNonFrequentData.Read().Geometry();
	}
private:

protected:

	EParticleType Type;
	FParticleDirtyFlags MDirtyFlags;

	void MarkDirty(const EParticleFlags DirtyBits, bool bInvalidate = true);

	void UpdateShapesArray()
	{
		UpdateShapesArrayFromGeometry(MShapesArray, MakeSerializable(MNonFrequentData.Read().Geometry()), FRigidTransform3(X(), R()), Proxy);
		MapImplicitShapes();
	}

	virtual void SyncRemoteDataImp(FDirtyPropertiesManager& Manager, int32 DataIdx, const FParticleDirtyData& RemoteData) const
	{
		MXR.SyncRemote(Manager, DataIdx, RemoteData);
		MNonFrequentData.SyncRemote(Manager, DataIdx, RemoteData);
	}

	void MapImplicitShapes();
};

template <typename T, int d>
FChaosArchive& operator<<(FChaosArchive& Ar, TGeometryParticle<T, d>& Particle)
{
	Particle.Serialize(Ar);
	return Ar;
}

template <typename T, int d>
class TGeometryParticleData : public FParticleData
{
	typedef FParticleData Base;
public:

	TGeometryParticleData(EParticleType InType = EParticleType::Static)
		: FParticleData(InType)
		, X(TVector<T, d>(0))
		, R(TRotation<T, d>())
		, SpatialIdx(FSpatialAccelerationIdx{ 0,0 })
		, UserData(nullptr)
		, DirtyFlags()
#if CHAOS_CHECKED
		, DebugName(NAME_None)
#endif
	{}

	TGeometryParticleData(const TGeometryParticle<T, d>& InParticle)
		: FParticleData(EParticleType::Static)
		, X(InParticle.X())
		, R(InParticle.R())
		, Geometry(InParticle.GeometrySharedLowLevel())
		, SpatialIdx(InParticle.SpatialIdx())
		, UniqueIdx(InParticle.UniqueIdx())
		, UserData(InParticle.UserData())
		, DirtyFlags(InParticle.DirtyFlags())
#if CHAOS_CHECKED
		, DebugName(InParticle.DebugName())
#endif
	{
		const FShapesArray& Shapes = InParticle.ShapesArray();
		ShapeCollisionDisableFlags.Empty(Shapes.Num());
		CollisionTraceType.Empty(Shapes.Num());
		ShapeSimData.Empty(Shapes.Num());
		ShapeQueryData.Empty(Shapes.Num());
		ShapeMaterials.Empty(Shapes.Num());
		for (const TUniquePtr<FPerShapeData>& ShapePtr : Shapes)
		{
			ShapeCollisionDisableFlags.Add(!ShapePtr->GetSimEnabled());
			CollisionTraceType.Add(ShapePtr->GetCollisionTraceType());
			ShapeSimData.Add(ShapePtr->GetSimData());
			ShapeQueryData.Add(ShapePtr->GetQueryData());
			ShapeMaterials.Add(ShapePtr->GetMaterials());
		}
	}

	//todo: remove
	void Reset() 
	{ 
#if 0
		FParticleData::Reset();  
		X = TVector<T, d>(0); 
		R = TRotation<T, d>(); 
		Geometry = TSharedPtr<FImplicitObjectUnion, ESPMode::ThreadSafe>();
		SpatialIdx = FSpatialAccelerationIdx{ 0,0 };
		UniqueIdx = FUniqueIdx();
		UserData = nullptr;
		DirtyFlags.Clear();
		ShapeCollisionDisableFlags.Reset();
		CollisionTraceType.Reset();
		ShapeSimData.Reset();
		ShapeQueryData.Reset();
		ShapeMaterials.Reset();
#if CHAOS_CHECKED
		DebugName = NAME_None;
#endif
#endif
	}
	
	//todo: remove
	void Init(const TGeometryParticle<T, d>& InParticle)
		{
#if 0
			Type = EParticleType::Static;
			X = InParticle.X();
			R = InParticle.R();
			Geometry = InParticle.GeometrySharedLowLevel();
			SpatialIdx = InParticle.SpatialIdx();
			UniqueIdx = InParticle.UniqueIdx();
			UserData = InParticle.UserData();
			DirtyFlags = InParticle.DirtyFlags();
#if CHAOS_CHECKED
			DebugName = InParticle.DebugName();
#endif
			const FShapesArray& Shapes = InParticle.ShapesArray();
			ShapeCollisionDisableFlags.Empty(Shapes.Num());
			CollisionTraceType.Empty(Shapes.Num());
			ShapeSimData.Empty(Shapes.Num());
			ShapeQueryData.Empty(Shapes.Num());
			for (const TUniquePtr<FPerShapeData>& ShapePtr : Shapes)
			{
				ShapeCollisionDisableFlags.Add(ShapePtr->bDisable);
				CollisionTraceType.Add(ShapePtr->CollisionTraceType);
				ShapeSimData.Add(ShapePtr->SimData);
				ShapeQueryData.Add(ShapePtr->QueryData);
				ShapeMaterials.Add(ShapePtr->Materials);
			}
#endif
	}
	TVector<T, d> X;
	TRotation<T, d> R;
	TSharedPtr<FImplicitObject, ESPMode::ThreadSafe> Geometry;
	FSpatialAccelerationIdx SpatialIdx;
	FUniqueIdx UniqueIdx;
	void* UserData;
	FParticleDirtyFlags DirtyFlags;
	TBitArray<> ShapeCollisionDisableFlags;
	TArray<EChaosCollisionTraceFlag> CollisionTraceType;
	TArray < FCollisionFilterData > ShapeSimData;
	TArray < FCollisionFilterData > ShapeQueryData;
	TArray < TArray<FMaterialHandle> > ShapeMaterials;
#if CHAOS_CHECKED
	FName DebugName;
#endif

};


template <typename T, int d>
class TKinematicGeometryParticle : public TGeometryParticle<T, d>
{
public:
	typedef TKinematicGeometryParticleData<T, d> FData;
	typedef TKinematicGeometryParticleHandle<T, d> FHandle;

	using TGeometryParticle<T, d>::Type;
	using TGeometryParticle<T, d>::CastToRigidParticle;
	using Base = TGeometryParticle<T,d>;
	using Base::MDirtyFlags;

protected:
	using Base::Proxy;

	friend TGeometryParticle<T,d>* TGeometryParticle<T, d>::SerializationFactory(FChaosArchive& Ar, TGeometryParticle<T, d>* Serializable);
	TKinematicGeometryParticle(const TKinematicGeometryParticleParameters<T, d>& KinematicParams = TKinematicGeometryParticleParameters<T,d>())
		: TGeometryParticle<T, d>(KinematicParams)
	{
		Type = EParticleType::Kinematic;
		KinematicGeometryParticleDefaultConstruct<T, d>(*this, KinematicParams);
	}
public:
	static TUniquePtr<TKinematicGeometryParticle<T, d>> CreateParticle(const TKinematicGeometryParticleParameters<T, d>& Params = TKinematicGeometryParticleParameters<T, d>())
	{
		return TUniquePtr< TKinematicGeometryParticle<T, d>>(new TKinematicGeometryParticle<T, d>(Params));
	}

	virtual void Serialize(FChaosArchive& Ar) override
	{
		TGeometryParticle<T, d>::Serialize(Ar);
		Ar << MVelocities;
		//Ar << MKinematicTarget; // TODO
	}

	const TVector<T, d>& V() const { return MVelocities.Read().V(); }
	void SetV(const TVector<T, d>& InV, bool bInvalidate = true);

	const TVector<T, d>& W() const { return MVelocities.Read().W(); }
	void SetW(const TVector<T, d>& InW, bool bInvalidate = true);

	const FKinematicTarget KinematicTarget() const {
		return MKinematicTarget.Read();
	}

	void SetKinematicTarget(const FKinematicTarget& KinematicTarget, bool bInvalidate = true)
	{
		MKinematicTarget.Write(KinematicTarget, bInvalidate, MDirtyFlags, Proxy);
	}

	void SetVelocities(const FParticleVelocities& InVelocities,bool bInvalidate = true)
	{
		MVelocities.Write(InVelocities,bInvalidate,MDirtyFlags,Proxy);
	}

	EObjectStateType ObjectState() const;

private:
	TParticleProperty<FParticleVelocities, EParticleProperty::Velocities> MVelocities;
	TParticleProperty<FKinematicTarget, EParticleProperty::KinematicTarget> MKinematicTarget;

protected:
	virtual void SyncRemoteDataImp(FDirtyPropertiesManager& Manager, int32 DataIdx, const FParticleDirtyData& RemoteData) const
	{
		Base::SyncRemoteDataImp(Manager, DataIdx, RemoteData);
		MVelocities.SyncRemote(Manager, DataIdx, RemoteData);
		MKinematicTarget.SyncRemote(Manager, DataIdx, RemoteData);
	}
};

template <typename T, int d>
class TKinematicGeometryParticleData : public TGeometryParticleData<T, d>
{
	typedef TGeometryParticleData<T, d> Base;
public:

	using TGeometryParticleData<T, d>::Type;

	TKinematicGeometryParticleData(EParticleType InType = EParticleType::Kinematic)
		: Base(InType)
		, MV(TVector<T, d>(0))
		, MW(TVector<T, d>(0)){}

	TKinematicGeometryParticleData(const TKinematicGeometryParticle<T, d>& InParticle)
		: Base(InParticle)
		, MV(InParticle.V())
		, MW(InParticle.W())
		, MKinematicTarget(InParticle.KinematicTarget())
	{
		Type = EParticleType::Kinematic;
	}


	void Reset() {
		TGeometryParticleData<T, d>::Reset();
		Type = EParticleType::Kinematic;
		MV = TVector<T, d>(0);
		MW = TVector<T, d>(0);
		MKinematicTarget.Clear();
	}

	void Init(const TKinematicGeometryParticle<T, d>& InParticle) {
			Base::Init(InParticle);
			MV = InParticle.V();
			MW = InParticle.W();
			Type = EParticleType::Kinematic;
			MKinematicTarget = InParticle.KinematicTarget();
	}
	TVector<T, d> MV;
	TVector<T, d> MW;
	TKinematicTarget<T, d> MKinematicTarget;
};


template <typename T, int d>
class TPBDRigidParticle : public TKinematicGeometryParticle<T, d>
{
public:
	typedef TPBDRigidParticleData<T, d> FData;
	typedef TPBDRigidParticleHandle<T, d> FHandle;

	using TGeometryParticle<T, d>::Type;
	using Base = TKinematicGeometryParticle<T,d>;

	using Base::MDirtyFlags;

protected:
	using Base::Proxy;
	friend TGeometryParticle<T, d>* TGeometryParticle<T, d>::SerializationFactory(FChaosArchive& Ar, TGeometryParticle<T, d>* Serializable);
	TPBDRigidParticle<T, d>(const TPBDRigidParticleParameters<T, d>& DynamicParams = TPBDRigidParticleParameters<T, d>())
		: TKinematicGeometryParticle<T, d>(DynamicParams), MWakeEvent(EWakeEventEntry::None)
	{
		Type = EParticleType::Rigid;
		MIsland = INDEX_NONE;
		MToBeRemovedOnFracture = false;
		PBDRigidParticleDefaultConstruct<T, d>(*this, DynamicParams);
	}
public:

	static TUniquePtr<TPBDRigidParticle<T, d>> CreateParticle(const TPBDRigidParticleParameters<T, d>& DynamicParams = TPBDRigidParticleParameters<T, d>())
	{
		return TUniquePtr< TPBDRigidParticle<T, d>>(new TPBDRigidParticle<T, d>(DynamicParams));
	}

	void Serialize(FChaosArchive& Ar) override
	{
		TKinematicGeometryParticle<T, d>::Serialize(Ar);
		Ar << MDynamics;

		Ar << MIsland;
		Ar << MToBeRemovedOnFracture;
	}

	const TUniquePtr<TBVHParticles<T, d>>& CollisionParticles() const { return MCollisionParticles; }

	int32 CollisionGroup() const { return MMiscData.Read().CollisionGroup(); }
	void SetCollisionGroup(const int32 InCollisionGroup)
	{
		MMiscData.Modify(true,MDirtyFlags,Proxy,[InCollisionGroup](auto& Data){ Data.SetCollisionGroup(InCollisionGroup);});
	}

	/*
	bool Disabled() const { return MMiscData.Read().bDisabled; }
	void SetDisabled(const bool InDisabled)
	{
		MMiscData.Modify(true,MDirtyFlags,Proxy,[InDisabled](auto& Data){ Data.bDisabled = InDisabled;});
	}*/

	bool GravityEnabled() const { return MMiscData.Read().GravityEnabled(); }
	void SetGravityEnabled(const bool InGravityEnabled)
	{
		MMiscData.Modify(true,MDirtyFlags,Proxy,[InGravityEnabled](auto& Data){ Data.SetGravityEnabled (InGravityEnabled);});
	}
	
	//todo: remove this
	bool IsInitialized() const { return MInitialized; }
	void SetInitialized(const bool InInitialized)
	{
		this->MInitialized = InInitialized;
	}

	void SetResimType(EResimType ResimType)
	{
		MMiscData.Modify(true,MDirtyFlags,Proxy,[ResimType](auto& Data){ Data.SetResimType(ResimType);});
	}

	EResimType ResimType() const
	{
		return MMiscData.Read().ResimType();
	}

	const TVector<T, d>& F() const { return MDynamics.Read().F(); }
	void SetF(const TVector<T, d>& InF, bool bInvalidate = true)
	{
		if (bInvalidate)
		{
			SetObjectState(EObjectStateType::Dynamic, true);
		}
		MDynamics.Modify(bInvalidate,MDirtyFlags,Proxy,[&InF](auto& Data){ Data.SetF(InF);});
	}

	const TVector<T, d>& Torque() const { return MDynamics.Read().Torque(); }
	void SetTorque(const TVector<T, d>& InTorque, bool bInvalidate=true)
	{
		if (bInvalidate)
		{
			SetObjectState(EObjectStateType::Dynamic, true);
		}
		MDynamics.Modify(bInvalidate,MDirtyFlags,Proxy,[&InTorque](auto& Data){ Data.SetTorque(InTorque);});
	}

	const TVector<T, d>& LinearImpulse() const { return MDynamics.Read().LinearImpulse(); }
	void SetLinearImpulse(const TVector<T, d>& InLinearImpulse, bool bInvalidate = true)
	{
		if (bInvalidate)
		{
			SetObjectState(EObjectStateType::Dynamic, true);
		}
		MDynamics.Modify(bInvalidate,MDirtyFlags,Proxy,[&InLinearImpulse](auto& Data){ Data.SetLinearImpulse(InLinearImpulse);});
	}

	const TVector<T, d>& AngularImpulse() const { return MDynamics.Read().AngularImpulse(); }
	void SetAngularImpulse(const TVector<T, d>& InAngularImpulse, bool bInvalidate = true)
	{
		if (bInvalidate)
		{
			SetObjectState(EObjectStateType::Dynamic, true);
		}
		MDynamics.Modify(bInvalidate,MDirtyFlags,Proxy,[&InAngularImpulse](auto& Data){ Data.SetAngularImpulse(InAngularImpulse);});
	}

	void SetDynamics(const FParticleDynamics& InDynamics,bool bInvalidate = true)
	{
		MDynamics.Write(InDynamics,bInvalidate,MDirtyFlags,Proxy);
	}

	void ResetSmoothedVelocities()
	{
		// Physics thread only. API required for FGeometryParticleStateBase::SyncToParticle
	}

	const PMatrix<T, d, d>& I() const { return MMassProps.Read().I(); }
	void SetI(const PMatrix<T, d, d>& InI)
	{
		MMassProps.Modify(true,MDirtyFlags,Proxy,[&InI](auto& Data){ Data.SetI(InI);});
	}

	const PMatrix<T, d, d>& InvI() const { return MMassProps.Read().InvI(); }
	void SetInvI(const PMatrix<T, d, d>& InInvI)
	{
		MMassProps.Modify(true,MDirtyFlags,Proxy,[&InInvI](auto& Data){ Data.SetInvI(InInvI);});
	}

	T M() const { return MMassProps.Read().M(); }
	void SetM(const T& InM)
	{
		MMassProps.Modify(true,MDirtyFlags,Proxy,[InM](auto& Data){ Data.SetM(InM);});
	}

	T InvM() const { return MMassProps.Read().InvM(); }
	void SetInvM(const T& InInvM)
	{
		MMassProps.Modify(true,MDirtyFlags,Proxy,[InInvM](auto& Data){ Data.SetInvM(InInvM);});
	}
	
	const TVector<T,d>& CenterOfMass() const { return MMassProps.Read().CenterOfMass(); }
	void SetCenterOfMass(const TVector<T,d>& InCenterOfMass,bool bInvalidate = true)
	{
		MMassProps.Modify(bInvalidate,MDirtyFlags,Proxy,[&InCenterOfMass](auto& Data){ Data.SetCenterOfMass(InCenterOfMass);});
	}

	const TRotation<T,d>& RotationOfMass() const { return MMassProps.Read().RotationOfMass(); }
	void SetRotationOfMass(const TRotation<T,d>& InRotationOfMass,bool bInvalidate = true)
	{
		MMassProps.Modify(bInvalidate,MDirtyFlags,Proxy,[&InRotationOfMass](auto& Data){ Data.SetRotationOfMass(InRotationOfMass);});
	}

	void SetMassProps(const FParticleMassProps& InProps)
	{
		MMassProps.Write(InProps,true,MDirtyFlags,Proxy);
	}

	void SetDynamicMisc(const FParticleDynamicMisc& DynamicMisc)
	{
		MMiscData.Write(DynamicMisc,true,MDirtyFlags,Proxy);
	}

	T LinearEtherDrag() const { return MMiscData.Read().LinearEtherDrag(); }
	void SetLinearEtherDrag(const T& InLinearEtherDrag)
	{
		MMiscData.Modify(true,MDirtyFlags,Proxy,[&InLinearEtherDrag](auto& Data){ Data.SetLinearEtherDrag(InLinearEtherDrag);});
	}

	T AngularEtherDrag() const { return MMiscData.Read().AngularEtherDrag(); }
	void SetAngularEtherDrag(const T& InAngularEtherDrag)
	{
		MMiscData.Modify(true,MDirtyFlags,Proxy,[&InAngularEtherDrag](auto& Data){ Data.SetAngularEtherDrag(InAngularEtherDrag);});
	}

	int32 Island() const { return MIsland; }
	// TODO(stett): Make the setter private. It is public right now to provide access to proxies.
	void SetIsland(const int32 InIsland)
	{
		this->MIsland = InIsland;
	}

	bool ToBeRemovedOnFracture() const { return MToBeRemovedOnFracture; }
	// TODO(stett): Make the setter private. It is public right now to provide access to proxies.
	void SetToBeRemovedOnFracture(const bool bToBeRemovedOnFracture)
	{
		this->MToBeRemovedOnFracture = bToBeRemovedOnFracture;
	}

	EObjectStateType ObjectState() const { return MMiscData.Read().ObjectState(); }
	void SetObjectState(const EObjectStateType InState, bool bAllowEvents = false, bool bInvalidate=true)
	{
		if (bAllowEvents)
		{
			const auto PreState = ObjectState();
			if(PreState == EObjectStateType::Dynamic && InState == EObjectStateType::Sleeping)
			{
				MWakeEvent = EWakeEventEntry::Sleep;
			}
			else if(PreState == EObjectStateType::Sleeping && InState == EObjectStateType::Dynamic)
			{
				MWakeEvent = EWakeEventEntry::Awake;
			}
		}

		if (InState == EObjectStateType::Sleeping)
		{
			// When an object is forced into a sleep state, the velocities must be zeroed and buffered,
			// in case the velocity is queried during sleep, or in case the object is woken up again.
			this->SetV(FVec3(0.f), bInvalidate);
			this->SetW(FVec3(0.f), bInvalidate);

			// Dynamic particle properties must be marked clean in order not to actually apply forces which
			// have been buffered. If another force is added after the object is put to sleep, the old forces
			// will remain and the new ones will accumulate and re-dirty the dynamic properties which will
			// wake the body.
			MDirtyFlags.MarkClean(ParticlePropToFlag(EParticleProperty::Dynamics));
		}

		MMiscData.Modify(bInvalidate,MDirtyFlags,Proxy,[&InState](auto& Data){ Data.SetObjectState(InState);});

	}

	void ClearEvents() { MWakeEvent = EWakeEventEntry::None; }
	EWakeEventEntry GetWakeEvent() { return MWakeEvent; }

private:
	TParticleProperty<FParticleMassProps,EParticleProperty::MassProps> MMassProps;
	TParticleProperty<FParticleDynamics, EParticleProperty::Dynamics> MDynamics;
	TParticleProperty<FParticleDynamicMisc,EParticleProperty::DynamicMisc> MMiscData;

	TUniquePtr<TBVHParticles<T, d>> MCollisionParticles;
	int32 MIsland;
	bool MToBeRemovedOnFracture;
	bool MInitialized;
	EWakeEventEntry MWakeEvent;

protected:
	virtual void SyncRemoteDataImp(FDirtyPropertiesManager& Manager, int32 DataIdx, const FParticleDirtyData& RemoteData) const
	{
		Base::SyncRemoteDataImp(Manager,DataIdx,RemoteData);
		MMassProps.SyncRemote(Manager,DataIdx,RemoteData);
		MDynamics.SyncRemote(Manager,DataIdx,RemoteData);
		MMiscData.SyncRemote(Manager,DataIdx,RemoteData);
	}
};

template <typename T, int d>
class TPBDGeometryCollectionParticle : public TPBDRigidParticle<T, d>
{
public:
	typedef TPBDRigidParticleData<T, d> FData;
	typedef TPBDGeometryCollectionParticleHandle<T, d> FHandle;

	using TGeometryParticle<T, d>::Type;
public:
	TPBDGeometryCollectionParticle<T,d>(const TPBDRigidParticleParameters<T,d>& DynamicParams = TPBDRigidParticleParameters<T,d>())
		: TPBDRigidParticle<T, d>(DynamicParams)
	{
		Type = EParticleType::GeometryCollection;
	}

	static TUniquePtr<TPBDGeometryCollectionParticle<T, d>> CreateParticle(const TPBDRigidParticleParameters<T, d>& DynamicParams = TPBDRigidParticleParameters<T, d>())
	{
		return TUniquePtr<TPBDGeometryCollectionParticle<T, d>>(new TPBDGeometryCollectionParticle<T, d>(DynamicParams));
	}
};

template <typename T, int d>
class TPBDRigidParticleData : public TKinematicGeometryParticleData<T, d>
{
	typedef TKinematicGeometryParticleData<T, d> Base;
public:

	using TKinematicGeometryParticleData<T, d>::Type;

	TPBDRigidParticleData(EParticleType InType = EParticleType::Rigid)
		: Base(InType)
		, MF(TVector<T, d>(0))
		, MTorque(TVector<T, d>(0))
		, MLinearImpulse(TVector<T, d>(0))
		, MAngularImpulse(TVector<T, d>(0))
		, MI(PMatrix<T, d, d>(0))
		, MInvI(PMatrix<T, d, d>(0))
		, MCollisionParticles(nullptr)
		, MM(T(0))
		, MInvM(T(0))
		, MCenterOfMass(TVector<T, d>(0))
		, MRotationOfMass(TRotation<T, d>(FQuat(EForceInit::ForceInit)))
		, MLinearEtherDrag(T(0))
		, MAngularEtherDrag(T(0))
		, MIsland(INDEX_NONE)
		, MCollisionGroup(0)
		, MObjectState(EObjectStateType::Uninitialized)
		//, MDisabled(false)
		, MToBeRemovedOnFracture(false)
		, MGravityEnabled(false)
		, MInitialized(false)
	{}

	TPBDRigidParticleData(const TPBDRigidParticle<T, d>& InParticle)
		: Base(InParticle)
		, MF(InParticle.F())
		, MTorque(InParticle.Torque())
		, MLinearImpulse(InParticle.LinearImpulse())
		, MAngularImpulse(InParticle.AngularImpulse())
		, MI(InParticle.I())
		, MInvI(InParticle.InvI())
		, MCollisionParticles(nullptr)
		, MM(InParticle.M())
		, MInvM(InParticle.InvM())
		, MCenterOfMass(InParticle.CenterOfMass())
		, MRotationOfMass(InParticle.RotationOfMass())
		, MLinearEtherDrag(InParticle.LinearEtherDrag())
		, MAngularEtherDrag(InParticle.AngularEtherDrag())
		, MIsland(InParticle.Island())
		, MCollisionGroup(InParticle.CollisionGroup())
		, MObjectState(InParticle.ObjectState())
		//, MDisabled(InParticle.Disabled())
		, MToBeRemovedOnFracture(InParticle.ToBeRemovedOnFracture())
		, MGravityEnabled(InParticle.GravityEnabled())
		, MInitialized(InParticle.IsInitialized())
	{
		Type = EParticleType::Rigid;
	}


	TVector<T, d> MF;
	TVector<T, d> MTorque;
	TVector<T, d> MLinearImpulse;
	TVector<T, d> MAngularImpulse;
	PMatrix<T, d, d> MI;
	PMatrix<T, d, d> MInvI;
	const TBVHParticles<T, d> * MCollisionParticles;
	T MM;
	T MInvM;
	TVector<T,d> MCenterOfMass;
	TRotation<T,d> MRotationOfMass;
	T MLinearEtherDrag;
	T MAngularEtherDrag;
	int32 MIsland;
	int32 MCollisionGroup;
	EObjectStateType MObjectState;
	//bool MDisabled;
	bool MToBeRemovedOnFracture;
	bool MGravityEnabled;
	bool MInitialized;

	void Reset() {
		TKinematicGeometryParticleData<T, d>::Reset();
		Type = EParticleType::Rigid;
		MF = TVector<T, d>(0);
		MTorque = TVector<T, d>(0);
		MLinearImpulse = TVector<T, d>(0);
		MAngularImpulse = TVector<T, d>(0);
		MI = PMatrix<T, d, d>(0);
		MInvI = PMatrix<T, d, d>(0);
		MCollisionParticles = nullptr;
		MM = T(0);
		MInvM = T(0);
		MCenterOfMass = TVector<T,d>(0);
		MRotationOfMass = TRotation<T,d>::FromIdentity();
		MLinearEtherDrag = T(0);
		MAngularEtherDrag = T(0);
		MIsland = INDEX_NONE;
		MCollisionGroup = 0;
		MObjectState = EObjectStateType::Uninitialized;
		//MDisabled = false;
		MToBeRemovedOnFracture = false;
		MGravityEnabled = false;
		MInitialized = false;
	}
	void Init(const TPBDRigidParticle<T, d>& InParticle) {
			Base::Init(InParticle);
			MF = InParticle.F();
			MTorque = InParticle.Torque();
			MLinearImpulse = InParticle.LinearImpulse();
			MAngularImpulse = InParticle.AngularImpulse();
			MI = InParticle.I();
			MInvI = InParticle.InvI();
			MCollisionParticles = nullptr;
			MM = InParticle.M();
			MInvM = InParticle.InvM();
			MLinearEtherDrag = InParticle.LinearEtherDrag();
			MAngularEtherDrag = InParticle.AngularEtherDrag();
			MIsland = InParticle.Island();
			MCollisionGroup = InParticle.CollisionGroup();
			MObjectState = InParticle.ObjectState();
			//MDisabled = InParticle.Disabled();
			MToBeRemovedOnFracture = InParticle.ToBeRemovedOnFracture();
			MGravityEnabled = InParticle.GravityEnabled();
			MInitialized = InParticle.IsInitialized();
			Type = EParticleType::Rigid;
		}
};


template <typename T, int d>
const TKinematicGeometryParticle<T, d>* TGeometryParticle<T, d>::CastToKinematicParticle() const
{
	if (Type >= EParticleType::Kinematic)
	{
		return static_cast<const TKinematicGeometryParticle<T, d>*>(this);
	}

	return nullptr;
}

template <typename T, int d>
TKinematicGeometryParticle<T, d>* TGeometryParticle<T, d>::CastToKinematicParticle()
{
	if (Type >= EParticleType::Kinematic)
	{
		return static_cast<TKinematicGeometryParticle<T, d>*>(this);
	}

	return nullptr;
}

template <typename T, int d>
TPBDRigidParticle<T, d>* TGeometryParticle<T, d>::CastToRigidParticle() 
{
	if (Type >= EParticleType::Rigid)
	{
		return static_cast<TPBDRigidParticle<T, d>*>(this);
	}

	return nullptr;
}


template <typename T, int d>
const TPBDRigidParticle<T, d>* TGeometryParticle<T, d>::CastToRigidParticle()  const
{
	if (Type >= EParticleType::Rigid)
	{
		return static_cast<const TPBDRigidParticle<T, d>*>(this);
	}

	return nullptr;
}

template <typename T, int d>
void TGeometryParticle<T, d>::SetObjectState(const EObjectStateType InState, bool bAllowEvents, bool bInvalidate)
{
	TPBDRigidParticle<T, d>* Dyn = CastToRigidParticle();
	if (Dyn)
	{
		Dyn->SetObjectState(InState, bAllowEvents, bInvalidate);
	}
}

template <typename T, int d>
void TGeometryParticle<T, d>::SetX(const TVector<T, d>& InX, bool bInvalidate)
{
	if (bInvalidate)
	{
		TPBDRigidParticle<T, d>* Dyn = CastToRigidParticle();
		if (Dyn && Dyn->ObjectState() == EObjectStateType::Sleeping)
		{
			Dyn->SetObjectState(EObjectStateType::Dynamic, true);
		}
	}
	MXR.Modify(bInvalidate, MDirtyFlags, Proxy, [&InX](auto& Data) { Data.SetX(InX); });
}

template <typename T, int d>
void TGeometryParticle<T, d>::SetR(const TRotation<T, d>& InR, bool bInvalidate)
{
	if (bInvalidate)
	{
		TPBDRigidParticle<T, d>* Dyn = CastToRigidParticle();
		if (Dyn && Dyn->ObjectState() == EObjectStateType::Sleeping)
		{
			Dyn->SetObjectState(EObjectStateType::Dynamic, true);
		}
	}
	MXR.Modify(bInvalidate, MDirtyFlags, Proxy, [&InR](auto& Data) { Data.SetR(InR); });
}

template <typename T, int d>
EObjectStateType TGeometryParticle<T, d>::ObjectState() const
{
	const TKinematicGeometryParticle<T, d>* Kin = CastToKinematicParticle();
	return Kin ? Kin->ObjectState() : EObjectStateType::Static;
}

template <typename T, int d>
EObjectStateType TKinematicGeometryParticle<T, d>::ObjectState() const
{
	const TPBDRigidParticle<T, d>* Dyn = CastToRigidParticle();
	return Dyn ? Dyn->ObjectState() : EObjectStateType::Kinematic;
}

template <typename T, int d>
void TKinematicGeometryParticle<T, d>::SetV(const TVector<T, d>& InV, bool bInvalidate)
{
	if (bInvalidate)
	{
		TPBDRigidParticle<T, d>* Dyn = CastToRigidParticle();
		if (Dyn && Dyn->ObjectState() == EObjectStateType::Sleeping && !InV.IsNearlyZero())
		{
			Dyn->SetObjectState(EObjectStateType::Dynamic, true);
		}
	}
	MVelocities.Modify(bInvalidate, MDirtyFlags, Proxy, [&InV](auto& Data) { Data.SetV(InV); });
}

template <typename T, int d>
void TKinematicGeometryParticle<T, d>::SetW(const TVector<T, d>& InW, bool bInvalidate)
{
	if (bInvalidate)
	{
		TPBDRigidParticle<T, d>* Dyn = CastToRigidParticle();
		if (Dyn && Dyn->ObjectState() == EObjectStateType::Sleeping && !InW.IsNearlyZero())
		{
			Dyn->SetObjectState(EObjectStateType::Dynamic, true);
		}
	}
	MVelocities.Modify(bInvalidate, MDirtyFlags, Proxy, [&InW](auto& Data) { Data.SetW(InW); });
}

template <typename T, int d>
TGeometryParticle<T, d>* TGeometryParticle<T, d>::SerializationFactory(FChaosArchive& Ar, TGeometryParticle<T, d>* Serializable)
{
	int8 ObjectType = Ar.IsLoading() ? 0 : (int8)Serializable->Type;
	Ar << ObjectType;
	switch ((EParticleType)ObjectType)
	{
	case EParticleType::Static: if (Ar.IsLoading()) { return new TGeometryParticle<T, d>(); } break;
	case EParticleType::Kinematic: if (Ar.IsLoading()) { return new TKinematicGeometryParticle<T, d>(); } break;
	case EParticleType::Rigid: if (Ar.IsLoading()) { return new TPBDRigidParticle<T, d>(); } break;
	case EParticleType::GeometryCollection: if (Ar.IsLoading()) { return new TPBDGeometryCollectionParticle<T, d>(); } break;
	default:
		check(false);
	}
	return nullptr;
}

template <>
CHAOS_API void Chaos::TGeometryParticle<float, 3>::MarkDirty(const EParticleFlags DirtyBits, bool bInvalidate);

template <typename T, int d>
TAccelerationStructureHandle<T,d>::TAccelerationStructureHandle(TGeometryParticleHandle<T, d>* InHandle)
	: ExternalGeometryParticle(InHandle->GTGeometryParticle())
	, GeometryParticleHandle(InHandle)
	, CachedUniqueIdx(InHandle->UniqueIdx())
	, bCanPrePreFilter(false)
{
	ensure(CachedUniqueIdx.IsValid());
	if (InHandle)
	{
		UpdatePrePreFilter(*InHandle);
	}
}

template <typename T, int d>
TAccelerationStructureHandle<T,d>::TAccelerationStructureHandle(TGeometryParticle<T, d>* InGeometryParticle)
	: ExternalGeometryParticle(InGeometryParticle)
	, GeometryParticleHandle(InGeometryParticle ? InGeometryParticle->Handle() : nullptr)
	, CachedUniqueIdx(InGeometryParticle ? InGeometryParticle->UniqueIdx() : FUniqueIdx())
	, bCanPrePreFilter(false)
{
	if (InGeometryParticle)
	{
		ensure(CachedUniqueIdx.IsValid());
		ensure(IsInGameThread());
		UpdatePrePreFilter(*InGeometryParticle);
	}
}

template <typename T, int d>
template <bool bPersistent>
TAccelerationStructureHandle<T, d>::TAccelerationStructureHandle(TGeometryParticleHandleImp<T, d, bPersistent>& InHandle)
	: ExternalGeometryParticle(InHandle.GTGeometryParticle())
	, GeometryParticleHandle(InHandle.Handle())
	, CachedUniqueIdx(InHandle.UniqueIdx())
	, bCanPrePreFilter(false)
{
	ensure(CachedUniqueIdx.IsValid());
	UpdatePrePreFilter(InHandle);
}

template <typename T, int d>
template <typename TParticle>
void TAccelerationStructureHandle<T, d>::UpdatePrePreFilter(const TParticle& Particle)
{
	const auto& Shapes = Particle.ShapesArray();
	for (const auto& Shape : Shapes)
	{
		UnionFilterData.Word0 |= Shape->GetQueryData().Word0;
		UnionFilterData.Word1 |= Shape->GetQueryData().Word1;
		UnionFilterData.Word2 |= Shape->GetQueryData().Word2;
		UnionFilterData.Word3 |= Shape->GetQueryData().Word3;
	}
	
	bCanPrePreFilter = true;

}


template <typename T, int d>
void TAccelerationStructureHandle<T, d>::Serialize(FChaosArchive& Ar)
{
	Ar << AsAlwaysSerializable(ExternalGeometryParticle);
	Ar << AsAlwaysSerializable(GeometryParticleHandle);

	Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
	if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::SerializeHashResult && Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::UniquePayloadIdx)
	{
		uint32 DummyHash;
		Ar << DummyHash;
	}
	
	if(GeometryParticleHandle)
	{
		CachedUniqueIdx = GeometryParticleHandle->UniqueIdx();
	}
	else if(ExternalGeometryParticle)
	{
		CachedUniqueIdx = ExternalGeometryParticle->UniqueIdx();
	}
	
	if (GeometryParticleHandle && ExternalGeometryParticle)
	{
		ensure(GeometryParticleHandle->UniqueIdx() == ExternalGeometryParticle->UniqueIdx());
	}
	
	ensure(!GeometryParticleHandle || CachedUniqueIdx.IsValid());
	ensure(!ExternalGeometryParticle || CachedUniqueIdx.IsValid());
}

template <typename T, int d>
FChaosArchive& operator<<(FChaosArchive& Ar, TAccelerationStructureHandle<T, d>& AccelerationHandle)
{
	AccelerationHandle.Serialize(Ar);
	return Ar;
}

#if CHAOS_DEBUG_DRAW
template <typename T, int d>
void TAccelerationStructureHandle<T, d>::DebugDraw(const bool bExternal, const bool bHit) const
{
	if (ExternalGeometryParticle && bExternal)
	{
		DebugDraw::DrawParticleShapes(FRigidTransform3(), ExternalGeometryParticle, bHit ? FColor::Red : FColor::Green);
	}

	if (GeometryParticleHandle && !bExternal)
	{
		DebugDraw::DrawParticleShapes(FRigidTransform3(), GeometryParticleHandle, bHit ? FColor(200, 100, 100) : FColor(100, 200, 100));
	}
}
#endif

#if PLATFORM_MAC || PLATFORM_LINUX
extern template class CHAOS_API TGeometryParticle<float, 3>;
extern template class CHAOS_API TKinematicGeometryParticle<float, 3>;
extern template class CHAOS_API TPBDRigidParticle<float, 3>;
#else
extern template class TGeometryParticle<float, 3>;
extern template class TKinematicGeometryParticle<float, 3>;
extern template class TPBDRigidParticle<float, 3>;
#endif
} // namespace Chaos

