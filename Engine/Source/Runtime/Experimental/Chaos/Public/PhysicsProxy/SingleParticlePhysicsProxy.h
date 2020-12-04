// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/Framework/MultiBufferResource.h"
#include "Chaos/Framework/PhysicsProxy.h"
#include "Chaos/PBDPositionConstraints.h"
#include "Chaos/ParticleHandle.h"
#include "PhysicsCoreTypes.h"
#include "Chaos/Defines.h"
#include "Chaos/EvolutionTraits.h"

namespace Chaos
{
	template<typename T, int d>
	class TGeometryParticle;

	template <typename Traits>
	class TPBDRigidsEvolutionGBF;
}

class FInitialState
{
public:
	FInitialState()
	    : Mass(0.f)
	    , InvMass(0.f)
	    , InertiaTensor(1.f)
	{}

	FInitialState(float MassIn, float InvMassIn, FVector InertiaTensorIn)
	    : Mass(MassIn)
	    , InvMass(InvMassIn)
	    , InertiaTensor(InertiaTensorIn)
	{}

	float GetMass() const { return Mass; }
	float GetInverseMass() const { return InvMass; }
	FVector GetInertiaTensor() const { return InertiaTensor; }

private:
	float Mass;
	float InvMass;
	FVector InertiaTensor;
};

/**
 * \p PARTICLE_TYPE is one of:
 *		\c Chaos::TGeometryParticle<float,3>
 *		\c Chaos::TKinematicGeometryParticle<float,3>
 *		\c Chaos::TPBDRigidParticle<float,3>
 */
template<class PARTICLE_TYPE>
class FSingleParticlePhysicsProxy : public TPhysicsProxy<FSingleParticlePhysicsProxy<PARTICLE_TYPE>,void>
{
	typedef TPhysicsProxy<FSingleParticlePhysicsProxy<PARTICLE_TYPE>, void> Base;

public:
	using FParticleHandle = typename PARTICLE_TYPE::FHandle;
	using FStorageData = typename PARTICLE_TYPE::FData;

	FSingleParticlePhysicsProxy() = delete;
	FSingleParticlePhysicsProxy(PARTICLE_TYPE* InParticle, FParticleHandle* InHandle, UObject* InOwner = nullptr, FInitialState InitialState = FInitialState());
	virtual ~FSingleParticlePhysicsProxy();

	// DELETE MOST OF ME
	void Initialize() {}
	bool IsSimulating() const { return true; }
	void ParameterUpdateCallback(FParticlesType& InParticles, const float InTime) {}
	void UpdateKinematicBodiesCallback(const FParticlesType& InParticles, const float InDt, const float InTime, FKinematicProxy& InKinematicProxy) {}
	void BindParticleCallbackMapping(Chaos::TArrayCollectionArray<PhysicsProxyWrapper>& PhysicsProxyReverseMap, Chaos::TArrayCollectionArray<int32>& ParticleIDReverseMap) {}
	void StartFrameCallback(const float InDt, const float InTime) {}
	void EndFrameCallback(const float InDt) {}
	void CreateRigidBodyCallback(FParticlesType& InOutParticles) {}
	void DisableCollisionsCallback(TSet<TTuple<int32, int32>>& InPairs) {}
	void AddForceCallback(FParticlesType& InParticles, const float InDt, const int32 InIndex) {}
	void FieldForcesUpdateCallback(Chaos::FPhysicsSolver* InSolver, FParticlesType& Particles, Chaos::TArrayCollectionArray<FVector>& Force, Chaos::TArrayCollectionArray<FVector>& Torque, const float Time) {}
	void BufferCommand(Chaos::FPhysicsSolver* InSolver, const FFieldSystemCommand& InCommand) {}
	void SyncBeforeDestroy() {}
	void OnRemoveFromScene() {}
	// END DELETE ME

	/**/
	const FInitialState& GetInitialState() const;

	FParticleHandle* GetHandle()
	{
		return Handle;
	}

	const FParticleHandle* GetHandle() const
	{
		return Handle;
	}

	virtual void* GetHandleUnsafe() const override
	{
		return Handle;
	}

	void SetHandle(FParticleHandle* InHandle)
	{
		Handle = InHandle;
	}

	void* GetUserData() const
	{
		auto GameThreadHandle = Handle->GTGeometryParticle();
		return GameThreadHandle ? GameThreadHandle->UserData() : nullptr;
	}

	Chaos::TRigidTransform<float, 3> GetTransform()
	{
		return Chaos::TRigidTransform<float, 3>(Handle->X(), Handle->R());
	}

	void* NewData()
	{
		return nullptr;
	}

	/**/
	EPhysicsProxyType ConcreteType() { return EPhysicsProxyType::NoneType; }

	// Threading API

	/**/
	void FlipBuffer() { BufferedData->FlipProducer(); }

	template <typename Traits>
	void PushToPhysicsState(const Chaos::FDirtyPropertiesManager& Manager,int32 DataIdx,const Chaos::FDirtyProxy& Dirty,Chaos::FShapeDirtyData* ShapesData, Chaos::TPBDRigidsEvolutionGBF<Traits>& Evolution);

	/**/
	void ClearAccumulatedData();

	/**/
	void BufferPhysicsResults();

	/**/
	bool PullFromPhysicsState(const int32 SolverSyncTimestamp);

	/**/
	bool IsDirty();

	bool IsInitialized() const { return bInitialized; }
	void SetInitialized(bool InInitialized) { bInitialized = InInitialized; }

	/**/
	Chaos::EWakeEventEntry GetWakeEvent() const;

	/**/
	void ClearEvents();

	PARTICLE_TYPE* GetParticle()
	{
		return Particle;
	}

	const PARTICLE_TYPE* GetParticle() const
	{
		return Particle;
	}
	
private:
	bool bInitialized;
	TArray<int32> InitializedIndices;

private:
	FInitialState InitialState;

	PARTICLE_TYPE* Particle;
	FParticleHandle* Handle;
	TUniquePtr<Chaos::IBufferResource<FStorageData>> BufferedData;
	//TUniquePtr<Chaos::IBufferResource<FPropertiesDataHolder>> PropertiesData;
	//TUniquePtr<Chaos::FDoubleBuffer<FPropertiesDataHolder>> PropertiesData;
};


// TGeometryParticle specialization prototypes
template< >
void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::ClearAccumulatedData();

template< >
CHAOS_API void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::BufferPhysicsResults();

template< >
CHAOS_API bool FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::PullFromPhysicsState(const int32 SolverSyncTimestamp);

template< >
bool FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::IsDirty();

template<>
template<typename Traits>
void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float,3>>::PushToPhysicsState(const Chaos::FDirtyPropertiesManager& Manager,int32 DataIdx,const Chaos::FDirtyProxy& Dirty,Chaos::FShapeDirtyData* ShapesData, Chaos::TPBDRigidsEvolutionGBF<Traits>& Evolution);

template< >
EPhysicsProxyType FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::ConcreteType();

template< >
CHAOS_API Chaos::EWakeEventEntry FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::GetWakeEvent() const;

template< >
CHAOS_API void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::ClearEvents();

// TKinematicGeometryParticle specialization prototypes

template<>
template<typename Traits>
void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float,3>>::PushToPhysicsState(const Chaos::FDirtyPropertiesManager& Manager,int32 DataIdx,const Chaos::FDirtyProxy& Dirty,Chaos::FShapeDirtyData* ShapesData, Chaos::TPBDRigidsEvolutionGBF<Traits>& Evolution);

template< >
void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::ClearAccumulatedData();

template< >
CHAOS_API void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::BufferPhysicsResults();

template< >
CHAOS_API bool FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::PullFromPhysicsState(const int32 SolverSyncTimestamp);

template< >
bool FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::IsDirty();

template< >
EPhysicsProxyType FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::ConcreteType();

template< >
CHAOS_API Chaos::EWakeEventEntry FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::GetWakeEvent() const;

template< >
CHAOS_API void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::ClearEvents();

// TPBDRigidParticles specialization prototypes

template< >
void FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::ClearAccumulatedData();

template< >
CHAOS_API void FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::BufferPhysicsResults();

template< >
CHAOS_API bool FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::PullFromPhysicsState(const int32 SolverSyncTimestamp);

template< >
bool FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::IsDirty();

template<>
template<typename Traits>
void FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float,3>>::PushToPhysicsState(const Chaos::FDirtyPropertiesManager& Manager,int32 DataIdx,const Chaos::FDirtyProxy& Dirty,Chaos::FShapeDirtyData* ShapesData, Chaos::TPBDRigidsEvolutionGBF<Traits>& Evolution);

template< >
EPhysicsProxyType FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::ConcreteType();

template< >
CHAOS_API Chaos::EWakeEventEntry FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::GetWakeEvent() const;

template< >
CHAOS_API void FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::ClearEvents();

#if PLATFORM_MAC || PLATFORM_LINUX
extern template class CHAOS_API FSingleParticlePhysicsProxy< Chaos::TGeometryParticle<float, 3> >;
extern template class CHAOS_API FSingleParticlePhysicsProxy< Chaos::TKinematicGeometryParticle<float, 3> >;
extern template class CHAOS_API FSingleParticlePhysicsProxy< Chaos::TPBDRigidParticle<float, 3> >;
#else
extern template class FSingleParticlePhysicsProxy< Chaos::TGeometryParticle<float, 3> >;
extern template class FSingleParticlePhysicsProxy< Chaos::TKinematicGeometryParticle<float, 3> >;
extern template class FSingleParticlePhysicsProxy< Chaos::TPBDRigidParticle<float,3> >;
#endif

#define EVOLUTION_TRAIT(Traits)\
extern template CHAOS_API void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<Chaos::FReal,3>>::PushToPhysicsState(const Chaos::FDirtyPropertiesManager& Manager,\
	int32 DataIdx,const Chaos::FDirtyProxy& Dirty,Chaos::FShapeDirtyData* ShapesData, Chaos::TPBDRigidsEvolutionGBF<Chaos::Traits>& Evolution);\
\
extern template CHAOS_API void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<Chaos::FReal,3>>::PushToPhysicsState(const Chaos::FDirtyPropertiesManager& Manager,\
	int32 DataIdx,const Chaos::FDirtyProxy& Dirty,Chaos::FShapeDirtyData* ShapesData,Chaos::TPBDRigidsEvolutionGBF<Chaos::Traits>& Evolution);\
\
extern template CHAOS_API void FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<Chaos::FReal,3>>::PushToPhysicsState(const Chaos::FDirtyPropertiesManager& Manager,\
	int32 DataIdx,const Chaos::FDirtyProxy& Dirty,Chaos::FShapeDirtyData* ShapesData, Chaos::TPBDRigidsEvolutionGBF<Chaos::Traits>& Evolution);
#include "Chaos/EvolutionTraits.inl"
#undef EVOLUTION_TRAIT
