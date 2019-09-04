// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if INCLUDE_CHAOS

#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDCollisionConstraint.h"
#include "Chaos/ArrayCollection.h"
#include "UObject/GCObject.h"

struct FKinematicProxy;
class FFieldSystemCommand;

namespace Chaos
{
	class FPBDRigidsSolver;
}

enum class ESolverObjectType
{
	NoneType = 0,
	StaticMeshType = 1,
	GeometryCollectionType = 2,
	FieldType = 3,
	SkeletalMeshType = 4
};

class ISolverObjectBase
{
public:
	virtual UObject* GetOwner() const = 0;

protected:
	// Intended to be non-virtual and protected. This ensures that derived classes can successfully call this destructor
	// but no one can delete using a ISolverObjectBase*
	~ISolverObjectBase() = default;
};

struct SolverObjectWrapper
{
	ISolverObjectBase* SolverObject;
	ESolverObjectType Type;
};

/**
 * Base object interface for solver objects. Defines the expected API for objects
 * uses CRTP for static dispatch, entire API considered "pure-virtual" and must be* defined.
 * Forgetting to implement any of the interface functions will give errors regarding
 * recursion on all control paths for TSolverObject<T> where T will be the type
 * that has not correctly implemented the API.
 *
 * PersistentTask uses ISolverObjectBase, so when implementing a new specialized type
 * it is necessary to include its header file in PersistentTask.cpp allowing the linker
 * to properly resolve the new type. 
 *
 * May not be necessary overall once the engine has solidified - we can just use the
 * final concrete objects but this gives us almost the same flexibility as the old
 * callbacks while solving most of the drawbacks (virtual dispatch, cross-object interaction)
 *
 * #BG TODO - rename the callbacks functions, document for the base solver object
 */
template<typename Concrete>
class TSolverObject : public ISolverObjectBase
{
public:
	using FParticlesType = Chaos::TPBDRigidParticles<float, 3>;
	using FCollisionConstraintsType = Chaos::TPBDCollisionConstraint<float, 3>;
	using FIntArray = Chaos::TArrayCollectionArray<int32>;

	TSolverObject()
		: Solver(nullptr)
		, Owner(nullptr)
	{
	}

	explicit TSolverObject(UObject* InOwner)
		: Solver(nullptr)
		, Owner(InOwner)
	{
	}

	/** Virtual destructor for derived objects, ideally no other virtuals should exist in this chain */
	virtual ~TSolverObject() {}

	/**
	 * The following functions are to be implemented by all solver objects as we're using CRTP / F-Bound to
	 * statically dispatch the calls. Any common functions should be added here and to the derived solver objects
	 */

	// Previously callback related functions, all called in the context of the physics thread if enabled.
	bool IsSimulating() const { return static_cast<const Concrete*>(this)->IsSimulating(); }
	void UpdateKinematicBodiesCallback(const FParticlesType& InParticles, const float InDt, const float InTime, FKinematicProxy& InKinematicProxy) { static_cast<Concrete*>(this)->UpdateKinematicBodiesCallback(InParticles, InDt, InTime, InKinematicProxy); }
	void StartFrameCallback(const float InDt, const float InTime) { static_cast<Concrete*>(this)->StartFrameCallback(InDt, InTime); }
	void EndFrameCallback(const float InDt) { static_cast<Concrete*>(this)->EndFrameCallback(InDt); }
	void CreateRigidBodyCallback(FParticlesType& InOutParticles) { static_cast<Concrete*>(this)->CreateRigidBodyCallback(InOutParticles); }
	void ParameterUpdateCallback(FParticlesType& InParticles, const float InTime) { static_cast<Concrete*>(this)->ParameterUpdateCallback(InParticles, InTime); }
	void DisableCollisionsCallback(TSet<TTuple<int32, int32>>& InPairs) { static_cast<Concrete*>(this)->DisableCollisionsCallback(InPairs); }
	void AddForceCallback(FParticlesType& InParticles, const float InDt, const int32 InIndex) { static_cast<Concrete*>(this)->AddForceCallback(InParticles, InDt, InIndex); }
	void FieldForcesUpdateCallback(Chaos::FPBDRigidsSolver* InSolver, FParticlesType& Particles, Chaos::TArrayCollectionArray<FVector> & Force, Chaos::TArrayCollectionArray<FVector> & Torque, const float Time) { static_cast<Concrete*>(this)->FieldForcesUpdateCallback(InSolver, Particles, Force, Torque, Time); }

	/** The Particle Binding creates a connection between the particles in the simulation and the solver objects dataset. */
	void BindParticleCallbackMapping(Chaos::TArrayCollectionArray<SolverObjectWrapper> & SolverObjectReverseMap, Chaos::TArrayCollectionArray<int32> & ParticleIDReverseMap) {static_cast<Concrete*>(this)->BindParticleCallbackMapping(SolverObjectReverseMap, ParticleIDReverseMap);}

	/** Called to buffer a command to be processed at the next available safe opportunity */
	void BufferCommand(Chaos::FPBDRigidsSolver* InSolver, const FFieldSystemCommand& InCommand) { static_cast<Concrete*>(this)->BufferCommand(InSolver, InCommand); }

	/**
	 * CONTEXT: GAMETHREAD
	 * Called during the gamethread sync after the proxy has been removed from its solver
	 * intended for final handoff of any data the proxy has that the gamethread may
	 * be interested in
	 */
	void SyncBeforeDestroy() { static_cast<Concrete*>(this)->SyncBeforeDestroy(); }

	/**
	 * CONTEXT: PHYSICSTHREAD
	 * Called on the physics thread when the engine is shutting down the proxy and we need to remove it from
	 * any active simulations. Proxies are expected to entirely clean up their simulation
	 * state within this method. This is run in the task command step by the scene
	 * so the simulation will currently be idle
	 */
	void OnRemoveFromScene() { static_cast<Concrete*>(this)->OnRemoveFromScene(); }

	/**
	 * CONTEXT: PHYSICSTHREAD
	 * Called per-tick after the simulation has completed. The proxy should cache the results of their
	 */
	void CacheResults() { static_cast<Concrete*>(this)->CacheResults(); }

	/**
	 * CONTEXT: PHYSICSTHREAD (Write Locked)
	 * Called by the physics thread to signal that it is safe to perform any double-buffer flips here.
	 * The physics thread has pre-locked an RW lock for this operation so the game thread won't be reading
	 * the data
	 */
	void FlipCache() { static_cast<Concrete*>(this)->FlipCache(); }

	/**
	 * CONTEXT: GAMETHREAD (Read Locked)
	 * Perform a similar operation to Sync, but take the data from a gamethread-safe cache. This will be called
	 * from the game thread when it cannot sync to the physics thread. The simulation is very likely to be running
	 * when this happens so never read any physics thread data here!
	 *
	 * Note: A read lock will have been aquired for this - so the physics thread won't force a buffer flip while this
	 * sync is ongoing
	 */
	void SyncToCache() { static_cast<Concrete*>(this)->SyncToCache(); }

	/**
	 * End SolverObject Common interface
	 */

	/** Get/Set the solver this object belongs to */
	void SetSolver(Chaos::FPBDRigidsSolver* InSolver) { Solver = InSolver; }
	Chaos::FPBDRigidsSolver* GetSolver() const { return Solver; }

	/** Gets the owning external object for this solver object, never used internally */
	virtual UObject* GetOwner() const override { return Owner; }

protected:

private:

	/** The solver that owns the solver object */
	Chaos::FPBDRigidsSolver* Solver;

	/** 
	 * The owner for this solver object, essentially user-data managed by the caller 
	 * @see GetOwner
	 */
	UObject* Owner;
};

#endif
