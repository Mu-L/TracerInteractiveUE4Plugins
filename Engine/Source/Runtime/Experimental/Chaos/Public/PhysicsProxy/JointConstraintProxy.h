// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/Framework/MultiBufferResource.h"
#include "Chaos/Framework/PhysicsProxy.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/GeometryParticlesfwd.h"
#include "Chaos/ParticleHandle.h"
#include "PhysicsCoreTypes.h"
#include "Chaos/Defines.h"
#include "Chaos/EvolutionTraits.h"

namespace Chaos
{
	class FJointConstraint;

	template <typename Traits>
	class TPBDRigidsEvolutionGBF;
}

/**
 * \p CONSTRAINT_TYPE is one of:
 *		\c Chaos::FJointConstraint
 *      \c Chaos::FPositionConstraint // @todo(chaos)
 *      \c Chaos::FVelocityConstraint // @todo(chaos)
 */
template<class CONSTRAINT_TYPE>
class TJointConstraintProxy : public TPhysicsProxy<TJointConstraintProxy<CONSTRAINT_TYPE>,void>
{
	typedef TPhysicsProxy<TJointConstraintProxy<CONSTRAINT_TYPE>, void> Base;

public:
	using FReal = Chaos::FReal;
	using FConstraintHandle = typename CONSTRAINT_TYPE::FHandle;
	using FConstraintData = typename CONSTRAINT_TYPE::FData;
	using FJointConstraints = Chaos::FPBDJointConstraints;
	using FJointConstraintDirtyFlags = Chaos::FJointConstraintDirtyFlags;
	using FParticlePair = Chaos::TVector<Chaos::TGeometryParticle<FReal, 3>*, 2>;
	using FParticleHandlePair = Chaos::TVector<Chaos::TGeometryParticleHandle<FReal, 3>*, 2>;

	struct FOutputData {
		bool bIsBroken = false;
		FVector Force = FVector(0);
		FVector Torque = FVector(0);
	};

	TJointConstraintProxy() = delete;
	TJointConstraintProxy(CONSTRAINT_TYPE* InConstraint, FConstraintHandle* InHandle, UObject* InOwner = nullptr); // @todo(brice) : make FPBDJointSetting a type defined on the CONSTRAINT_TYPE
	virtual ~TJointConstraintProxy();

	EPhysicsProxyType ConcreteType();
	
	bool IsValid() { return Constraint != nullptr && Constraint->IsValid(); }

	bool IsInitialized() const { return bInitialized; }
	void SetInitialized() { bInitialized = true; }

	//
	//  Lifespan Management
	//

	template <typename Traits>
	void CHAOS_API InitializeOnPhysicsThread(Chaos::TPBDRigidsSolver<Traits>* InSolver);

	// Merge to perform a remote sync
	template <typename Traits>
	void CHAOS_API PushStateOnGameThread(Chaos::TPBDRigidsSolver<Traits>* InSolver);

	template <typename Traits>
	void CHAOS_API PushStateOnPhysicsThread(Chaos::TPBDRigidsSolver<Traits>* InSolver);
	// Merge to perform a remote sync - END

	template <typename Traits>
	void CHAOS_API DestroyOnPhysicsThread(Chaos::TPBDRigidsSolver<Traits>* InSolver);

	void SyncBeforeDestroy() {}
	void OnRemoveFromScene() {}


	//
	// Member Access
	//

	FConstraintHandle* GetHandle()
	{
		return Handle;
	}

	const FConstraintHandle* GetHandle() const
	{
		return Handle;
	}

	virtual void* GetHandleUnsafe() const override
	{
		return Handle;
	}

	void SetHandle(FConstraintHandle* InHandle)
	{
		Handle = InHandle;
	}

	CONSTRAINT_TYPE* GetConstraint()
	{
		return Constraint;
	}

	const CONSTRAINT_TYPE* GetConstraint() const
	{
		return Constraint;
	}

	//
	// Threading API
	//

	/**/
	void FlipBuffer() { OutputBuffer->FlipProducer(); }

	template <typename Traits>
	void PushToPhysicsState(Chaos::TPBDRigidsEvolutionGBF<Traits>& Evolution) {}

	/**/
	void ClearAccumulatedData() {}

	/**/
	void BufferPhysicsResults();

	/**/
	bool CHAOS_API PullFromPhysicsState(const int32 SolverSyncTimestamp);

	/**/
	bool IsDirty() { return Constraint->IsDirty(); }
	
private:

	// Input Buffer
	FConstraintData JointSettingsBuffer;
	FJointConstraintDirtyFlags DirtyFlagsBuffer;

	// Output Buffer
	TUniquePtr<Chaos::IBufferResource<FOutputData>> OutputBuffer;

	CONSTRAINT_TYPE* Constraint;
	FConstraintHandle* Handle;
	bool bInitialized;


public:
	void AddForceCallback(FParticlesType& InParticles, const float InDt, const int32 InIndex) {}
	void DisableCollisionsCallback(TSet<TTuple<int32, int32>>& InPairs) {}
	void BindParticleCallbackMapping(Chaos::TArrayCollectionArray<PhysicsProxyWrapper>& PhysicsProxyReverseMap, Chaos::TArrayCollectionArray<int32>& ParticleIDReverseMap) {}
	void EndFrameCallback(const float InDt) {}
	void ParameterUpdateCallback(FParticlesType& InParticles, const float InTime) {}
	void CreateRigidBodyCallback(FParticlesType& InOutParticles) {}
	bool IsSimulating() const { return true; }
	void UpdateKinematicBodiesCallback(const FParticlesType& InParticles, const float InDt, const float InTime, FKinematicProxy& InKinematicProxy) {}
	void StartFrameCallback(const float InDt, const float InTime) {}

};


template<> CHAOS_API EPhysicsProxyType TJointConstraintProxy<Chaos::FJointConstraint>::ConcreteType();
template<> CHAOS_API void TJointConstraintProxy<Chaos::FJointConstraint>::BufferPhysicsResults();
template<> CHAOS_API bool TJointConstraintProxy<Chaos::FJointConstraint>::PullFromPhysicsState(const int32 SolverSyncTimestamp);

extern template class TJointConstraintProxy< Chaos::FJointConstraint >;
typedef TJointConstraintProxy< Chaos::FJointConstraint > FJointConstraintPhysicsProxy;
