// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/GCObject.h"
#include "Chaos/ChaosEngineInterface.h"

//Chaos includes. Todo: move to chaos core so we can include for all of engine
#include "Chaos/Declares.h"
#include "PhysicsProxy/SingleParticlePhysicsProxyFwd.h"
#include "Framework/Threading.h"
#include "Chaos/Core.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/PBDRigidsEvolutionFwd.h"
#include "Async/TaskGraphInterfaces.h"

// Currently compilation issue with Incredibuild when including headers required by event template functions
#define XGE_FIXED 0

class AdvanceOneTimeStepTask;
class FChaosSolversModule;
struct FForceFieldProxy;
struct FSolverStateStorage;

class FSkeletalMeshPhysicsProxy;
class FStaticMeshPhysicsProxy;
class FPerSolverFieldSystem;

class IPhysicsProxyBase;

namespace Chaos
{
	class FPhysicsProxy;

	struct FCollisionEventData;

	enum EEventType : int32;

	template<typename PayloadType, typename HandlerType>
	class TRawEventHandler;

	template <typename T, int d>
	class TAccelerationStructureHandle;

	template <typename TPayload, typename T, int d>
	class ISpatialAcceleration;

	template <typename TPayload, typename T, int d>
	class ISpatialAccelerationCollection;

	template <typename T>
	class TArrayCollectionArray;

	class FPBDRigidDirtyParticlesBufferAccessor;
}

/**
* Low level Chaos scene used when building custom simulations that don't exist in the main world physics scene.
*/
class PHYSICSCORE_API FChaosScene
#if WITH_ENGINE
	: public FGCObject
#endif
{
public:
	FChaosScene(
		UObject* OwnerPtr
#if CHAOS_CHECKED
	, const FName& DebugName=NAME_None
#endif
);

	virtual ~FChaosScene();

	/**
	 * Get the internal Chaos solver object
	 */
	Chaos::FPhysicsSolver* GetSolver() const { return SceneSolver; }

#if WITH_ENGINE
	// FGCObject Interface ///////////////////////////////////////////////////
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const
	{
		return "FChaosScene";
	}
	//////////////////////////////////////////////////////////////////////////
#endif
	
	const Chaos::ISpatialAcceleration<Chaos::TAccelerationStructureHandle<float, 3>, float, 3>* GetSpacialAcceleration() const;
	Chaos::ISpatialAcceleration<Chaos::TAccelerationStructureHandle<float, 3>, float, 3>* GetSpacialAcceleration();

	void AddActorsToScene_AssumesLocked(TArray<FPhysicsActorHandle>& InHandles,const bool bImmediate=true);
	void RemoveActorFromAccelerationStructure(FPhysicsActorHandle& Actor);
	void UpdateActorsInAccelerationStructure(const TArrayView<FPhysicsActorHandle>& Actors);
	void UpdateActorInAccelerationStructure(const FPhysicsActorHandle& Actor);

	void WaitPhysScenes();

	/**
	 * Copies the acceleration structure out of the solver, does no thread safety checking so ensure calls
	 * to this are made at appropriate sync points if required
	 */
	void CopySolverAccelerationStructure();

	/**
	 * Flushes all pending global, task and solver command queues and refreshes the spatial acceleration
	 * for the scene. Required when querying against a currently non-running scene to ensure the scene
	 * is correctly represented
	 */
	void Flush_AssumesLocked();
#if WITH_EDITOR
	void AddPieModifiedObject(UObject* InObj);
#endif

	void StartFrame();
	void SetUpForFrame(const FVector* NewGrav,float InDeltaSeconds,float InMaxPhysicsDeltaTime,float InMaxSubstepDeltaTime,int32 InMaxSubsteps,bool bSubstepping);
	void EndFrame();

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPhysScenePostTick,FChaosScene*);
	FOnPhysScenePostTick OnPhysScenePostTick;

	bool IsCompletionEventComplete() const;
	FGraphEventArray GetCompletionEvents();

protected:

	TUniquePtr<Chaos::ISpatialAccelerationCollection<Chaos::TAccelerationStructureHandle<float, 3>, float, 3>> SolverAccelerationStructure;

	// Control module for Chaos - cached to avoid constantly hitting the module manager
	FChaosSolversModule* ChaosModule;

	// Solver representing this scene
	Chaos::FPhysicsSolver* SceneSolver;

	/** Scene lock object for external threads (non-physics) */
	Chaos::FPhysicsSceneGuard ExternalDataLock;

#if WITH_EDITOR
	// List of objects that we modified during a PIE run for physics simulation caching.
	TArray<UObject*> PieModifiedObjects;
#endif

	// Allow other code to obtain read-locks when needed
	friend struct FScopedSceneReadLock;
	friend struct FScopedSceneLock_Chaos;

	//Engine interface BEGIN
	virtual float OnStartFrame(float InDeltaTime){ return InDeltaTime; }
	virtual void OnSyncBodies(const int32 SolverSyncTimestamp, Chaos::FPBDRigidDirtyParticlesBufferAccessor& Accessor);
	//Engine interface END

	float MDeltaTime;

	UObject* Owner;

private:

	void SetGravity(const Chaos::TVector<float,3>& Acceleration)
	{
		// #todo : Implement
	}

	template <typename TSolver>
	void SyncBodies(TSolver* Solver);

	// Taskgraph control
	FGraphEventArray CompletionEvents;
};