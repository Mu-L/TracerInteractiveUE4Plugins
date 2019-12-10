// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if WITH_CHAOS

#include "Physics/Experimental/PhysInterface_Chaos.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Physics/Experimental/ChaosInterfaceUtils.h"
#include "Physics/PhysicsInterfaceTypes.h"
#include "PhysicsEngine/AggregateGeom.h"

#include "Chaos/Box.h"
#include "Chaos/Cylinder.h"
#include "Chaos/TaperedCylinder.h"
#include "Chaos/Capsule.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/Levelset.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/Sphere.h"
#include "Chaos/Matrix.h"
#include "Chaos/MassProperties.h"
#include "PhysicsSolver.h"
#include "Templates/UniquePtr.h"
#include "ChaosSolversModule.h"
#include "Chaos/ErrorReporter.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/Convex.h"
#include "Chaos/GeometryQueries.h"
#include "Chaos/Plane.h"
#include "ChaosCheck.h"

#include "Async/ParallelFor.h"
#include "Components/PrimitiveComponent.h"
#include "Physics/PhysicsFiltering.h"
#include "Collision/CollisionConversions.h"
#include "PhysicsInterfaceUtilsCore.h"

#if WITH_PHYSX
#include "geometry/PxConvexMesh.h"
#include "geometry/PxTriangleMesh.h"
#include "foundation/PxVec3.h"
#include "extensions/PxMassProperties.h"
#endif

DEFINE_STAT(STAT_TotalPhysicsTime);
DEFINE_STAT(STAT_NumCloths);
DEFINE_STAT(STAT_NumClothVerts);

DECLARE_CYCLE_STAT(TEXT("Start Physics Time (sync)"), STAT_PhysicsKickOffDynamicsTime, STATGROUP_Physics);
DECLARE_CYCLE_STAT(TEXT("Fetch Results Time (sync)"), STAT_PhysicsFetchDynamicsTime, STATGROUP_Physics);

DECLARE_CYCLE_STAT(TEXT("Start Physics Time (async)"), STAT_PhysicsKickOffDynamicsTime_Async, STATGROUP_Physics);
DECLARE_CYCLE_STAT(TEXT("Fetch Results Time (async)"), STAT_PhysicsFetchDynamicsTime_Async, STATGROUP_Physics);

DECLARE_CYCLE_STAT(TEXT("Update Kinematics On Deferred SkelMeshes"), STAT_UpdateKinematicsOnDeferredSkelMeshes, STATGROUP_Physics);

DECLARE_CYCLE_STAT(TEXT("Phys Events Time"), STAT_PhysicsEventTime, STATGROUP_Physics);
DECLARE_CYCLE_STAT(TEXT("SyncComponentsToBodies (sync)"), STAT_SyncComponentsToBodies, STATGROUP_Physics);
DECLARE_CYCLE_STAT(TEXT("SyncComponentsToBodies (async)"), STAT_SyncComponentsToBodies_Async, STATGROUP_Physics);

DECLARE_DWORD_COUNTER_STAT(TEXT("Broadphase Adds"), STAT_NumBroadphaseAdds, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("Broadphase Removes"), STAT_NumBroadphaseRemoves, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("Active Constraints"), STAT_NumActiveConstraints, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("Active Simulated Bodies"), STAT_NumActiveSimulatedBodies, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("Active Kinematic Bodies"), STAT_NumActiveKinematicBodies, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("Mobile Bodies"), STAT_NumMobileBodies, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("Static Bodies"), STAT_NumStaticBodies, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("Shapes"), STAT_NumShapes, STATGROUP_Physics);

DECLARE_DWORD_COUNTER_STAT(TEXT("(ASync) Broadphase Adds"), STAT_NumBroadphaseAddsAsync, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("(ASync) Broadphase Removes"), STAT_NumBroadphaseRemovesAsync, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("(ASync) Active Constraints"), STAT_NumActiveConstraintsAsync, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("(ASync) Active Simulated Bodies"), STAT_NumActiveSimulatedBodiesAsync, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("(ASync) Active Kinematic Bodies"), STAT_NumActiveKinematicBodiesAsync, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("(ASync) Mobile Bodies"), STAT_NumMobileBodiesAsync, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("(ASync) Static Bodies"), STAT_NumStaticBodiesAsync, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("(ASync) Shapes"), STAT_NumShapesAsync, STATGROUP_Physics);

ECollisionShapeType GetGeometryType(const Chaos::TPerShapeData<float, 3>& Shape)
{
	return GetType(*Shape.Geometry);
}

const Chaos::TImplicitObject<float, 3>& FPhysicsShapeReference_Chaos::GetGeometry() const
{
	check(IsValid()); return *Shape->Geometry;
}

FPhysInterface_Chaos::FPhysInterface_Chaos(const AWorldSettings* Settings) 
{

}

FPhysInterface_Chaos::~FPhysInterface_Chaos()
{
}


// Interface functions
void FPhysInterface_Chaos::CreateActor(const FActorCreationParams& InParams, FPhysicsActorHandle& Handle)
{
	LLM_SCOPE(ELLMTag::Chaos);

	// Create the new particle
	if (InParams.bStatic)
	{
		Handle = Chaos::TGeometryParticle<float, 3>::CreateParticle().Release();	//todo: should BodyInstance use a unique ptr to manage this memory?
	}
	else if (InParams.BodyInstance && InParams.BodyInstance->ShouldInstanceSimulatingPhysics())
	{
		TUniquePtr<Chaos::TPBDRigidParticle<float, 3>> RigidHandle = Chaos::TPBDRigidParticle<float, 3>::CreateParticle();
		RigidHandle->SetGravityEnabled(InParams.bEnableGravity);
		Handle = RigidHandle.Release();	//todo: should BodyInstance use a unique ptr to manage this memory?
	}
	else
	{
		Handle = Chaos::TKinematicGeometryParticle<float, 3>::CreateParticle().Release(); //todo: should BodyInstance use a unique ptr to manage this memory?
	}

	// Set up the new particle's game-thread data. This will be sent to physics-thread when
	// the particle is added to the scene later.
	Handle->SetX(InParams.InitialTM.GetLocation());
	Handle->SetR(InParams.InitialTM.GetRotation());
}


void FPhysInterface_Chaos::AddActorToSolver(FPhysicsActorHandle& Handle, Chaos::FPhysicsSolver* Solver, Chaos::IDispatcher* Dispatcher)
{
	LLM_SCOPE(ELLMTag::Chaos);

	Solver->RegisterObject(Handle);
}

void FPhysInterface_Chaos::ReleaseActor(FPhysicsActorHandle& Handle, FPhysScene* InScene, bool bNeverDerferRelease)
{
	if(!Handle)
	{
		UE_LOG(LogChaos, Warning, TEXT("Attempting to release an actor with a null handle"));
		CHAOS_ENSURE(false);

		return;
	}

	if (InScene)
	{
		InScene->GetScene().RemoveActorFromAccelerationStructure(Handle);
		RemoveActorFromSolver(Handle, InScene->GetSolver(), FChaosSolversModule::GetModule()->GetDispatcher());
	}

	delete Handle;

	Handle = nullptr;
}

void FPhysInterface_Chaos::RemoveActorFromSolver(FPhysicsActorHandle& Handle, Chaos::FPhysicsSolver* Solver, Chaos::IDispatcher* Dispatcher)
{
	if (Solver && Handle->Proxy)
	{
		Solver->UnregisterObject(Handle);
	}
}

// Aggregate is not relevant for Chaos yet
FPhysicsAggregateReference_Chaos FPhysInterface_Chaos::CreateAggregate(int32 MaxBodies)
{
	// #todo : Implement
    FPhysicsAggregateReference_Chaos NewAggregate;
    return NewAggregate;
}

void FPhysInterface_Chaos::ReleaseAggregate(FPhysicsAggregateReference_Chaos& InAggregate) {}
int32 FPhysInterface_Chaos::GetNumActorsInAggregate(const FPhysicsAggregateReference_Chaos& InAggregate) { return 0; }
void FPhysInterface_Chaos::AddActorToAggregate_AssumesLocked(const FPhysicsAggregateReference_Chaos& InAggregate, const FPhysicsActorHandle& InActor) {}


int32 FPhysInterface_Chaos::GetNumShapes(const FPhysicsActorHandle& InHandle)
{
	// #todo : Implement
	return InHandle->ShapesArray().Num();
}

void FPhysInterface_Chaos::ReleaseShape(const FPhysicsShapeHandle& InShape)
{
    check(!IsValid(InShape.ActorRef));
	//no need to delete because ownership is on actor. Is this an invalid assumption with the current API?
	//delete InShape.Shape;
}

void FPhysInterface_Chaos::AttachShape(const FPhysicsActorHandle& InActor, const FPhysicsShapeHandle& InNewShape)
{
	// #todo : Implement
}

void FPhysInterface_Chaos::DetachShape(const FPhysicsActorHandle& InActor, FPhysicsShapeHandle& InShape, bool bWakeTouching)
{
	// #todo : Implement
}

void FPhysInterface_Chaos::SetActorUserData_AssumesLocked(FPhysicsActorHandle& InActorReference, FPhysicsUserData* InUserData)
{
	InActorReference->SetUserData(InUserData);
}

bool FPhysInterface_Chaos::IsRigidBody(const FPhysicsActorHandle& InActorReference)
{
	return InActorReference->ObjectState() == Chaos::EObjectStateType::Dynamic;
}

bool FPhysInterface_Chaos::IsStatic(const FPhysicsActorHandle& InActorReference)
{
	return InActorReference->ObjectState() == Chaos::EObjectStateType::Static;
}

bool FPhysInterface_Chaos::IsKinematic(const FPhysicsActorHandle& InActorReference)
{
	return InActorReference->ObjectState() == Chaos::EObjectStateType::Kinematic;
}

bool FPhysInterface_Chaos::IsKinematic_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	return IsKinematic(InActorReference);
}

bool FPhysInterface_Chaos::IsSleeping(const FPhysicsActorHandle& InActorReference)
{
	// #todo : Implement
	return false;
}

bool FPhysInterface_Chaos::IsCcdEnabled(const FPhysicsActorHandle& InActorReference)
{
    return false;
}

bool FPhysInterface_Chaos::IsInScene(const FPhysicsActorHandle& InActorReference)
{
	// TODO: Implement
	return false;
}

FPhysScene* FPhysInterface_Chaos::GetCurrentScene(const FPhysicsActorHandle& InHandle)
{
	if(!InHandle)
	{
		UE_LOG(LogChaos, Warning, TEXT("Attempting to get the current scene for a null handle."));
		CHAOS_ENSURE(false);
		return nullptr;
	}

	if (IPhysicsProxyBase* Proxy = InHandle->Proxy)
	{
		Chaos::FPhysicsSolver* Solver = Proxy->GetSolver();
		return static_cast<FPhysScene*>(Solver ? Solver->PhysSceneHack : nullptr);
	}
	return nullptr;
}

bool FPhysInterface_Chaos::CanSimulate_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	// #todo : Implement
	return true;
}

float FPhysInterface_Chaos::GetMass_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	if (const Chaos::TPBDRigidParticle<float,3>* RigidParticle = InActorReference->AsDynamic())
	{
		return RigidParticle->M();
	}
	return 0.f;
}

void FPhysInterface_Chaos::SetSendsSleepNotifies_AssumesLocked(const FPhysicsActorHandle& InActorReference, bool bSendSleepNotifies)
{
	// # todo: Implement
    //check(bSendSleepNotifies == false);
}

void FPhysInterface_Chaos::PutToSleep_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	// #todo : Implement
}

void FPhysInterface_Chaos::WakeUp_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	// #todo : Implement
}

void FPhysInterface_Chaos::SetIsKinematic_AssumesLocked(const FPhysicsActorHandle& InActorReference, bool bIsKinematic)
{
	// #todo : Implement
}

void FPhysInterface_Chaos::SetCcdEnabled_AssumesLocked(const FPhysicsActorHandle& InActorReference, bool bIsCcdEnabled)
{
	// #todo: Implement
    //check(bIsCcdEnabled == false);
}

FTransform FPhysInterface_Chaos::GetGlobalPose_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	return Chaos::TRigidTransform<float, 3>(InActorReference->X(), InActorReference->R());
}

void FPhysInterface_Chaos::SetGlobalPose_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FTransform& InNewPose, bool bAutoWake)
{
	InActorReference->SetX(InNewPose.GetLocation());
	InActorReference->SetR(InNewPose.GetRotation());
}

FTransform FPhysInterface_Chaos::GetTransform_AssumesLocked(const FPhysicsActorHandle& InRef, bool bForceGlobalPose /*= false*/)
{
	if(!bForceGlobalPose)
	{
		if(IsDynamic(InRef))
		{
			if(HasKinematicTarget_AssumesLocked(InRef))
			{
				return GetKinematicTarget_AssumesLocked(InRef);
			}
		}
	}

	return GetGlobalPose_AssumesLocked(InRef);
}

bool FPhysInterface_Chaos::HasKinematicTarget_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
    return IsStatic(InActorReference);
}

FTransform FPhysInterface_Chaos::GetKinematicTarget_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	// #todo : Implement
	//for now just use global pose
	return FPhysInterface_Chaos::GetGlobalPose_AssumesLocked(InActorReference);
}

void FPhysInterface_Chaos::SetKinematicTarget_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FTransform& InNewTarget)
{
	// #todo : Implement
	//for now just use global pose
	FPhysInterface_Chaos::SetGlobalPose_AssumesLocked(InActorReference, InNewTarget);
}

FVector FPhysInterface_Chaos::GetLinearVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	// #todo : Implement
	return FVector(0);
}

void FPhysInterface_Chaos::SetLinearVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InNewVelocity, bool bAutoWake)
{
	// #todo : Implement
}

FVector FPhysInterface_Chaos::GetAngularVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	// #todo : Implement
	return FVector(0);
}

void FPhysInterface_Chaos::SetAngularVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InNewVelocity, bool bAutoWake)
{
	// #todo : Implement
}

float FPhysInterface_Chaos::GetMaxAngularVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
    return FLT_MAX;
}

void FPhysInterface_Chaos::SetMaxAngularVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference, float InMaxAngularVelocity)
{
}

float FPhysInterface_Chaos::GetMaxDepenetrationVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
    return FLT_MAX;
}

void FPhysInterface_Chaos::SetMaxDepenetrationVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference, float InMaxDepenetrationVelocity)
{
}

FVector FPhysInterface_Chaos::GetWorldVelocityAtPoint_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InPoint)
{
	// #todo : Implement
	return FVector(0);
}

FTransform FPhysInterface_Chaos::GetComTransform_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	// #todo : Implement
	return FTransform();
}

FTransform FPhysInterface_Chaos::GetComTransformLocal_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	// #todo : Implement
	return FTransform();
}

FVector FPhysInterface_Chaos::GetLocalInertiaTensor_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	if (Chaos::TPBDRigidParticle<float, 3 >* RigidParticle = InActorReference->AsDynamic())
	{
		const Chaos::PMatrix<float, 3, 3> & Tensor = RigidParticle->I();
		return FVector(Tensor.M[0][0], Tensor.M[1][1], Tensor.M[2][2]) ;
	}
	return FVector::ZeroVector;
}

FBox FPhysInterface_Chaos::GetBounds_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	// #todo : Implement
	//const auto& Box = InActorReference.GetScene()->Scene.GetSolver()->GetRigidParticles().Geometry(InActorReference.GetScene()->GetIndexFromId(InActorReference.GetId()))->BoundingBox();
    return FBox(FVector(-0.5), FVector(0.5));
}

void FPhysInterface_Chaos::SetLinearDamping_AssumesLocked(const FPhysicsActorHandle& InActorReference, float InDamping)
{

}

void FPhysInterface_Chaos::SetAngularDamping_AssumesLocked(const FPhysicsActorHandle& InActorReference, float InDamping)
{

}

void FPhysInterface_Chaos::AddImpulse_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InForce)
{
	// #todo : Implement
    //InActorReference.GetScene()->AddForce(InForce, InActorReference.GetId());
}

void FPhysInterface_Chaos::AddAngularImpulseInRadians_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InTorque)
{
	// #todo : Implement
    //InActorReference.GetScene()->AddTorque(InTorque, InActorReference.GetId());
}

void FPhysInterface_Chaos::AddVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InForce)
{	
	// #todo : Implement
    //InActorReference.GetScene()->AddForce(InForce * InActorReference.GetScene()->Scene.GetSolver()->GetRigidParticles().M(InActorReference.GetScene()->GetIndexFromId(InActorReference.GetId())), InActorReference.GetId());
}

void FPhysInterface_Chaos::AddAngularVelocityInRadians_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InTorque)
{
	// #todo : Implement
    //InActorReference.GetScene()->AddTorque(InActorReference.GetScene()->Scene.GetSolver()->GetRigidParticles().I(InActorReference.GetScene()->GetIndexFromId(InActorReference.GetId())) * Chaos::TVector<float, 3>(InTorque), InActorReference.GetId());
}

void FPhysInterface_Chaos::AddImpulseAtLocation_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InImpulse, const FVector& InLocation)
{
    // @todo(mlentine): We don't currently have a way to apply an instantaneous force. Do we need this?
}

void FPhysInterface_Chaos::AddRadialImpulse_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InOrigin, float InRadius, float InStrength, ERadialImpulseFalloff InFalloff, bool bInVelChange)
{
    // @todo(mlentine): We don't currently have a way to apply an instantaneous force. Do we need this?
}

bool FPhysInterface_Chaos::IsGravityEnabled_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
	if (Chaos::TPBDRigidParticle<float, 3 >* RigidParticle = InActorReference->AsDynamic())
	{
		return RigidParticle->IsGravityEnabled();
	}
	return false;
}
void FPhysInterface_Chaos::SetGravityEnabled_AssumesLocked(const FPhysicsActorHandle& InActorReference, bool bEnabled)
{
	if (Chaos::TPBDRigidParticle<float, 3 >* RigidParticle = InActorReference->AsDynamic())
	{
		RigidParticle->SetGravityEnabled(bEnabled);
		FPhysicsCommand::ExecuteWrite(InActorReference, [&](const FPhysicsActorHandle& Actor)
		{
			// todo : This is currently synced in FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::PushToPhysicsState. 
			//        Ideally this would execute a write command to the gravity forces on the physics thread. However,
			//        the Actor.Handle() does not have access to the Evolution, so the PerParticleGravityForces are not accessible. 
			//        This will need to be fixed. 
		});
	}
}

float FPhysInterface_Chaos::GetSleepEnergyThreshold_AssumesLocked(const FPhysicsActorHandle& InActorReference)
{
    return 0;
}
void FPhysInterface_Chaos::SetSleepEnergyThreshold_AssumesLocked(const FPhysicsActorHandle& InActorReference, float InEnergyThreshold)
{
}

void FPhysInterface_Chaos::SetMass_AssumesLocked(FPhysicsActorHandle& InActorReference, float InMass)
{
	if (Chaos::TPBDRigidParticle<float, 3 >* RigidParticle = InActorReference->AsDynamic())
	{
		RigidParticle->SetM(InMass);
		if (ensure(!FMath::IsNearlyZero(InMass)))
		{
			RigidParticle->SetInvM(1./InMass);
		}
		else
		{
			RigidParticle->SetInvM(0);
		}
	}
}

void FPhysInterface_Chaos::SetMassSpaceInertiaTensor_AssumesLocked(FPhysicsActorHandle& InActorReference, const FVector& InTensor)
{
	if (Chaos::TPBDRigidParticle<float, 3 >* RigidParticle = InActorReference->AsDynamic())
	{
		if(ensure(!FMath::IsNearlyZero(InTensor.X)) && ensure(!FMath::IsNearlyZero(InTensor.Y)) && ensure(!FMath::IsNearlyZero(InTensor.Z)) )
		{
			RigidParticle->SetI(Chaos::PMatrix<float, 3, 3>(InTensor.X, InTensor.Y, InTensor.Z));
			RigidParticle->SetInvI(Chaos::PMatrix<float, 3, 3>(1./InTensor.X, 1./InTensor.Y, 1./InTensor.Z));
		}
	}
}

void FPhysInterface_Chaos::SetComLocalPose_AssumesLocked(const FPhysicsActorHandle& InHandle, const FTransform& InComLocalPose)
{
    //@todo(mlentine): What is InComLocalPose? If the center of an object is not the local pose then many things break including the three vector represtnation of inertia.
}

float FPhysInterface_Chaos::GetStabilizationEnergyThreshold_AssumesLocked(const FPhysicsActorHandle& InHandle)
{
	// #todo : Implement
	return 0.0f;
}

void FPhysInterface_Chaos::SetStabilizationEnergyThreshold_AssumesLocked(const FPhysicsActorHandle& InHandle, float InThreshold)
{
	// #todo : Implement
}

uint32 FPhysInterface_Chaos::GetSolverPositionIterationCount_AssumesLocked(const FPhysicsActorHandle& InHandle)
{
	// #todo : Implement
	return 0;
}

void FPhysInterface_Chaos::SetSolverPositionIterationCount_AssumesLocked(const FPhysicsActorHandle& InHandle, uint32 InSolverIterationCount)
{
	// #todo : Implement
}

uint32 FPhysInterface_Chaos::GetSolverVelocityIterationCount_AssumesLocked(const FPhysicsActorHandle& InHandle)
{
	// #todo : Implement
	return 0;
}

void FPhysInterface_Chaos::SetSolverVelocityIterationCount_AssumesLocked(const FPhysicsActorHandle& InHandle, uint32 InSolverIterationCount)
{
	// #todo : Implement
}

float FPhysInterface_Chaos::GetWakeCounter_AssumesLocked(const FPhysicsActorHandle& InHandle)
{
	// #todo : Implement
	return 0.0f;
}

void FPhysInterface_Chaos::SetWakeCounter_AssumesLocked(const FPhysicsActorHandle& InHandle, float InWakeCounter)
{
	// #todo : Implement
}

SIZE_T FPhysInterface_Chaos::GetResourceSizeEx(const FPhysicsActorHandle& InActorRef)
{
    return sizeof(FPhysicsActorHandle);
}
	
// Constraints
FPhysicsConstraintReference_Chaos FPhysInterface_Chaos::CreateConstraint(const FPhysicsActorHandle& InActorRef1, const FPhysicsActorHandle& InActorRef2, const FTransform& InLocalFrame1, const FTransform& InLocalFrame2)
{
	// #todo : Implement
	FPhysicsConstraintReference_Chaos ConstraintRef;
	return ConstraintRef;
}

void FPhysInterface_Chaos::SetConstraintUserData(const FPhysicsConstraintReference_Chaos& InConstraintRef, void* InUserData)
{
	// #todo : Implement
}

void FPhysInterface_Chaos::ReleaseConstraint(FPhysicsConstraintReference_Chaos& InConstraintRef)
{
	// #todo : Implement
}

FTransform FPhysInterface_Chaos::GetLocalPose(const FPhysicsConstraintReference_Chaos& InConstraintRef, EConstraintFrame::Type InFrame)
{
	// #todo : Implement
	//
	//int32 Index1 = InConstraintRef.GetScene()->MSpringConstraints->Constraints()[InConstraintRef.GetScene()->GetConstraintIndexFromId(InConstraintRef.GetId())][0];
	//int32 Index2 = InConstraintRef.GetScene()->MSpringConstraints->Constraints()[InConstraintRef.GetScene()->GetConstraintIndexFromId(InConstraintRef.GetId())][1];
	//Chaos::TRigidTransform<float, 3> Transform1(InConstraintRef.GetScene()->Scene.GetSolver()->GetRigidParticles().X(Index1), InConstraintRef.GetScene()->Scene.GetSolver()->GetRigidParticles().R(Index1));
	//Chaos::TRigidTransform<float, 3> Transform2(InConstraintRef.GetScene()->Scene.GetSolver()->GetRigidParticles().X(Index2), InConstraintRef.GetScene()->Scene.GetSolver()->GetRigidParticles().R(Index2));
	// @todo(mlentine): This is likely broken
	//FTransform(Transform1.Inverse() * Transform2);

	return  FTransform();
}

FTransform FPhysInterface_Chaos::GetGlobalPose(const FPhysicsConstraintReference_Chaos& InConstraintRef, EConstraintFrame::Type InFrame)
{
	// #todo : Implement
	return  FTransform();
}

FVector FPhysInterface_Chaos::GetLocation(const FPhysicsConstraintReference_Chaos& InConstraintRef)
{
	// #todo : Implement
	return  FVector(0.f);
}

void FPhysInterface_Chaos::GetForce(const FPhysicsConstraintReference_Chaos& InConstraintRef, FVector& OutLinForce, FVector& OutAngForce)
{
	// #todo : Implement
}

void FPhysInterface_Chaos::GetDriveLinearVelocity(const FPhysicsConstraintReference_Chaos& InConstraintRef, FVector& OutLinVelocity)
{
	// #todo : Implement
}

void FPhysInterface_Chaos::GetDriveAngularVelocity(const FPhysicsConstraintReference_Chaos& InConstraintRef, FVector& OutAngVelocity)
{
	// #todo : Implement
}

float FPhysInterface_Chaos::GetCurrentSwing1(const FPhysicsConstraintReference_Chaos& InConstraintRef)
{
    return GetLocalPose(InConstraintRef, EConstraintFrame::Frame2).GetRotation().Euler().X;
}

float FPhysInterface_Chaos::GetCurrentSwing2(const FPhysicsConstraintReference_Chaos& InConstraintRef)
{
    return GetLocalPose(InConstraintRef, EConstraintFrame::Frame2).GetRotation().Euler().Y;
}

float FPhysInterface_Chaos::GetCurrentTwist(const FPhysicsConstraintReference_Chaos& InConstraintRef)
{
    return GetLocalPose(InConstraintRef, EConstraintFrame::Frame2).GetRotation().Euler().Z;
}

void FPhysInterface_Chaos::SetCanVisualize(const FPhysicsConstraintReference_Chaos& InConstraintRef, bool bInCanVisualize)
{

}

void FPhysInterface_Chaos::SetCollisionEnabled(const FPhysicsConstraintReference_Chaos& InConstraintRef, bool bInCollisionEnabled)
{
	// #todo : Implement
}

void FPhysInterface_Chaos::SetProjectionEnabled_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, bool bInProjectionEnabled, float InLinearTolerance, float InAngularToleranceDegrees)
{

}

void FPhysInterface_Chaos::SetParentDominates_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, bool bInParentDominates)
{

}

void FPhysInterface_Chaos::SetBreakForces_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InLinearBreakForce, float InAngularBreakForce)
{

}

void FPhysInterface_Chaos::SetLocalPose(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FTransform& InPose, EConstraintFrame::Type InFrame)
{

}

void FPhysInterface_Chaos::SetLinearMotionLimitType_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, PhysicsInterfaceTypes::ELimitAxis InAxis, ELinearConstraintMotion InMotion)
{

}

void FPhysInterface_Chaos::SetAngularMotionLimitType_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, PhysicsInterfaceTypes::ELimitAxis InAxis, EAngularConstraintMotion InMotion)
{

}

void FPhysInterface_Chaos::UpdateLinearLimitParams_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InLimit, float InAverageMass, const FLinearConstraint& InParams)
{

}

void FPhysInterface_Chaos::UpdateConeLimitParams_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InAverageMass, const FConeConstraint& InParams)
{

}

void FPhysInterface_Chaos::UpdateTwistLimitParams_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InAverageMass, const FTwistConstraint& InParams)
{

}

void FPhysInterface_Chaos::UpdateLinearDrive_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FLinearDriveConstraint& InDriveParams)
{

}

void FPhysInterface_Chaos::UpdateAngularDrive_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FAngularDriveConstraint& InDriveParams)
{

}

void FPhysInterface_Chaos::UpdateDriveTarget_AssumesLocked(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FLinearDriveConstraint& InLinDrive, const FAngularDriveConstraint& InAngDrive)
{

}

void FPhysInterface_Chaos::SetDrivePosition(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FVector& InPosition)
{

}

void FPhysInterface_Chaos::SetDriveOrientation(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FQuat& InOrientation)
{

}

void FPhysInterface_Chaos::SetDriveLinearVelocity(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FVector& InLinVelocity)
{

}

void FPhysInterface_Chaos::SetDriveAngularVelocity(const FPhysicsConstraintReference_Chaos& InConstraintRef, const FVector& InAngVelocity)
{

}

void FPhysInterface_Chaos::SetTwistLimit(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InLowerLimit, float InUpperLimit, float InContactDistance)
{

}

void FPhysInterface_Chaos::SetSwingLimit(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InYLimit, float InZLimit, float InContactDistance)
{

}

void FPhysInterface_Chaos::SetLinearLimit(const FPhysicsConstraintReference_Chaos& InConstraintRef, float InLimit)
{

}

bool FPhysInterface_Chaos::IsBroken(const FPhysicsConstraintReference_Chaos& InConstraintRef)
{
	// #todo : Implement
	return true;
}

enum class EPhysicsInterfaceScopedLockType : uint8
{
	Read,
	Write
};

struct FScopedSceneLock_Chaos
{
	FScopedSceneLock_Chaos(FPhysicsActorHandle const * InActorHandle, EPhysicsInterfaceScopedLockType InLockType)
		: LockType(InLockType)
	{
		Scene = GetSceneForActor(InActorHandle);
		LockScene();
	}

	FScopedSceneLock_Chaos(FPhysicsActorHandle const * InActorHandleA, FPhysicsActorHandle const * InActorHandleB, EPhysicsInterfaceScopedLockType InLockType)
		: LockType(InLockType)
	{
		FPhysScene_ChaosInterface* SceneA = GetSceneForActor(InActorHandleA);
		FPhysScene_ChaosInterface* SceneB = GetSceneForActor(InActorHandleB);

		if(SceneA == SceneB)
		{
			Scene = SceneA;
		}
		else if(!SceneA || !SceneB)
		{
			Scene = SceneA ? SceneA : SceneB;
		}
		else
		{
			UE_LOG(LogPhysics, Warning, TEXT("Attempted to aquire a physics scene lock for two paired actors that were not in the same scene. Skipping lock"));
		}

		LockScene();
	}

	FScopedSceneLock_Chaos(FPhysicsConstraintHandle const * InHandle, EPhysicsInterfaceScopedLockType InLockType)
		: Scene(nullptr)
		, LockType(InLockType)
	{
		UE_LOG(LogPhysics, Warning, TEXT("Constraint instance attempted scene lock, Constraints currently unimplemented"));
	}

	FScopedSceneLock_Chaos(USkeletalMeshComponent* InSkelMeshComp, EPhysicsInterfaceScopedLockType InLockType)
		: LockType(InLockType)
	{
		Scene = nullptr;

		if(InSkelMeshComp)
		{
			for(FBodyInstance* BI : InSkelMeshComp->Bodies)
			{
				Scene = GetSceneForActor(&BI->GetPhysicsActorHandle());
				if(Scene)
				{
					break;
				}
			}
		}

		LockScene();
	}

	FScopedSceneLock_Chaos(FPhysScene_ChaosInterface* InScene, EPhysicsInterfaceScopedLockType InLockType)
		: Scene(InScene)
		, LockType(InLockType)
	{
		LockScene();
	}

	~FScopedSceneLock_Chaos()
	{
		UnlockScene();
	}

private:

	void LockScene()
	{
		if(!Scene)
		{
			return;
		}

		switch(LockType)
		{
		case EPhysicsInterfaceScopedLockType::Read:
			Scene->GetScene().ExternalDataLock.ReadLock();
			break;
		case EPhysicsInterfaceScopedLockType::Write:
			Scene->GetScene().ExternalDataLock.WriteLock();
			break;
		}
	}

	void UnlockScene()
	{
		if(!Scene)
		{
			return;
		}

		switch(LockType)
		{
		case EPhysicsInterfaceScopedLockType::Read:
			Scene->GetScene().ExternalDataLock.ReadUnlock();
			break;
		case EPhysicsInterfaceScopedLockType::Write:
			Scene->GetScene().ExternalDataLock.WriteUnlock();
			break;
		}
	}

	FPhysScene_ChaosInterface* GetSceneForActor(FPhysicsActorHandle const * InActorHandle)
	{
		FBodyInstance* ActorInstance = (*InActorHandle) ? FPhysicsUserData_Chaos::Get<FBodyInstance>((*InActorHandle)->UserData()) : nullptr;

		if(ActorInstance)
		{
			return ActorInstance->GetPhysicsScene();
		}

		return nullptr;
	}

	FPhysScene_ChaosInterface* Scene;
	EPhysicsInterfaceScopedLockType LockType;
};

bool FPhysInterface_Chaos::ExecuteOnUnbrokenConstraintReadOnly(const FPhysicsConstraintReference_Chaos& InConstraintRef, TFunctionRef<void(const FPhysicsConstraintReference_Chaos&)> Func)
{
    if (!IsBroken(InConstraintRef))
    {
		FScopedSceneLock_Chaos SceneLock(&InConstraintRef, EPhysicsInterfaceScopedLockType::Read);
        Func(InConstraintRef);
        return true;
    }
    return false;
}

bool FPhysInterface_Chaos::ExecuteOnUnbrokenConstraintReadWrite(const FPhysicsConstraintReference_Chaos& InConstraintRef, TFunctionRef<void(const FPhysicsConstraintReference_Chaos&)> Func)
{
    if (!IsBroken(InConstraintRef))
    {
		FScopedSceneLock_Chaos SceneLock(&InConstraintRef, EPhysicsInterfaceScopedLockType::Write);
        Func(InConstraintRef);
        return true;
    }
    return false;
}

bool FPhysInterface_Chaos::ExecuteRead(const FPhysicsActorHandle& InActorReference, TFunctionRef<void(const FPhysicsActorHandle& Actor)> InCallable)
{
	if(InActorReference)
	{
		FScopedSceneLock_Chaos SceneLock(&InActorReference, EPhysicsInterfaceScopedLockType::Read);

		InCallable(InActorReference);
		return true;
	}
	return false;
}

bool FPhysInterface_Chaos::ExecuteRead(USkeletalMeshComponent* InMeshComponent, TFunctionRef<void()> InCallable)
{
	FScopedSceneLock_Chaos SceneLock(InMeshComponent, EPhysicsInterfaceScopedLockType::Read);
	InCallable();
	return true;
}

bool FPhysInterface_Chaos::ExecuteRead(const FPhysicsActorHandle& InActorReferenceA, const FPhysicsActorHandle& InActorReferenceB, TFunctionRef<void(const FPhysicsActorHandle& ActorA, const FPhysicsActorHandle& ActorB)> InCallable)
{
	FScopedSceneLock_Chaos SceneLock(&InActorReferenceA, &InActorReferenceB, EPhysicsInterfaceScopedLockType::Read);
	InCallable(InActorReferenceA, InActorReferenceB);
	return true;
}

bool FPhysInterface_Chaos::ExecuteRead(const FPhysicsConstraintReference_Chaos& InConstraintRef, TFunctionRef<void(const FPhysicsConstraintReference_Chaos& Constraint)> InCallable)
{
	if(InConstraintRef.IsValid())
	{
		FScopedSceneLock_Chaos SceneLock(&InConstraintRef, EPhysicsInterfaceScopedLockType::Read);
		InCallable(InConstraintRef);
		return true;
	}

	return false;
}

bool FPhysInterface_Chaos::ExecuteRead(FPhysScene* InScene, TFunctionRef<void()> InCallable)
{
	if(InScene)
	{
		FScopedSceneLock_Chaos SceneLock(InScene, EPhysicsInterfaceScopedLockType::Read);
		InCallable();
		return true;
	}

	return false;
}

bool FPhysInterface_Chaos::ExecuteWrite(const FPhysicsActorHandle& InActorReference, TFunctionRef<void(const FPhysicsActorHandle& Actor)> InCallable)
{
	//why do we have a write that takes in a const handle?
	if(InActorReference)
	{
		FScopedSceneLock_Chaos SceneLock(&InActorReference, EPhysicsInterfaceScopedLockType::Write);
		InCallable(InActorReference);
		return true;
	}
	return false;
}

bool FPhysInterface_Chaos::ExecuteWrite(FPhysicsActorHandle& InActorReference, TFunctionRef<void(FPhysicsActorHandle& Actor)> InCallable)
{
	if(InActorReference)
	{
		FScopedSceneLock_Chaos SceneLock(&InActorReference, EPhysicsInterfaceScopedLockType::Write);
		InCallable(InActorReference);
		return true;
	}

	return false;
}

bool FPhysInterface_Chaos::ExecuteWrite(USkeletalMeshComponent* InMeshComponent, TFunctionRef<void()> InCallable)
{
	FScopedSceneLock_Chaos SceneLock(InMeshComponent, EPhysicsInterfaceScopedLockType::Write);
	InCallable();
	return true;
}

bool FPhysInterface_Chaos::ExecuteWrite(const FPhysicsActorHandle& InActorReferenceA, const FPhysicsActorHandle& InActorReferenceB, TFunctionRef<void(const FPhysicsActorHandle& ActorA, const FPhysicsActorHandle& ActorB)> InCallable)
{
	FScopedSceneLock_Chaos SceneLock(&InActorReferenceA, &InActorReferenceB, EPhysicsInterfaceScopedLockType::Write);
	InCallable(InActorReferenceA, InActorReferenceB);
	return true;
}

bool FPhysInterface_Chaos::ExecuteWrite(const FPhysicsConstraintReference_Chaos& InConstraintRef, TFunctionRef<void(const FPhysicsConstraintReference_Chaos& Constraint)> InCallable)
{
	if(InConstraintRef.IsValid())
	{
		FScopedSceneLock_Chaos SceneLock(&InConstraintRef, EPhysicsInterfaceScopedLockType::Write);
		InCallable(InConstraintRef);
		return true;
	}

	return false;
}

bool FPhysInterface_Chaos::ExecuteWrite(FPhysScene* InScene, TFunctionRef<void()> InCallable)
{
	if(InScene)
	{
		FScopedSceneLock_Chaos SceneLock(InScene, EPhysicsInterfaceScopedLockType::Write);
		InCallable();
		return true;
	}

	return false;
}

void FPhysInterface_Chaos::ExecuteShapeWrite(FBodyInstance* InInstance, FPhysicsShapeHandle& InShape, TFunctionRef<void(FPhysicsShapeHandle& InShape)> InCallable)
{
	if(InInstance && InShape.IsValid())
	{
		FScopedSceneLock_Chaos SceneLock(&InInstance->GetPhysicsActorHandle(), EPhysicsInterfaceScopedLockType::Write);
		InCallable(InShape);
	}
}


FPhysicsShapeHandle FPhysInterface_Chaos::CreateShape(physx::PxGeometry* InGeom, bool bSimulation, bool bQuery, UPhysicalMaterial* InSimpleMaterial, TArray<UPhysicalMaterial*>* InComplexMaterials)
{
	// #todo : Implement
	// @todo(mlentine): Should we be doing anything with the InGeom here?
    FPhysicsActorHandle NewActor = nullptr;
	return { nullptr, bSimulation, bQuery, NewActor };
}

const FBodyInstance* FPhysInterface_Chaos::ShapeToOriginalBodyInstance(const FBodyInstance* InCurrentInstance, const Chaos::TPerShapeData<float, 3>* InShape)
{
	//question: this is identical to physx version, should it be in body instance?
	check(InCurrentInstance);
	check(InShape);

	const FBodyInstance* TargetInstance = InCurrentInstance->WeldParent ? InCurrentInstance->WeldParent : InCurrentInstance;
	const FBodyInstance* OutInstance = TargetInstance;

	if (const TMap<FPhysicsShapeHandle, FBodyInstance::FWeldInfo>* WeldInfo = InCurrentInstance->GetCurrentWeldInfo())
	{
		for (const TPair<FPhysicsShapeHandle, FBodyInstance::FWeldInfo>& Pair : *WeldInfo)
		{
			if (Pair.Key.Shape == InShape)
			{
				TargetInstance = Pair.Value.ChildBI;
			}
		}
	}

	return TargetInstance;
}



void FPhysInterface_Chaos::AddGeometry(FPhysicsActorHandle& InActor, const FGeometryAddParams& InParams, TArray<FPhysicsShapeHandle>* OutOptShapes)
{
	TArray<TUniquePtr<Chaos::TImplicitObject<float, 3>>> Geoms;
	Chaos::TShapesArray<float, 3> Shapes;
	ChaosInterface::CreateGeometry(InParams, Geoms, Shapes);

#if WITH_CHAOS
	if (InActor && Geoms.Num())
	{
		if (OutOptShapes)
		{
			for (auto& Shape : Shapes)
			{
				OutOptShapes->Add({ Shape.Get(), true, true, InActor });
			}
		}

		//todo: we should not be creating unique geometry per actor
		InActor->SetGeometry(MakeUnique<Chaos::TImplicitObjectUnion<float, 3>>(MoveTemp(Geoms)));
		InActor->SetShapesArray(MoveTemp(Shapes));
	}
#endif
}


// todo(brice): Implicit Initialization Pipeline(WIP)
// ... add virtual TImplicitObject::NewCopy()
// @todo(mlentine,brice): We probably need to actually duplicate the data here, add virtual TImplicitObject::NewCopy()
FPhysicsShapeHandle FPhysInterface_Chaos::CloneShape(const FPhysicsShapeHandle& InShape)
{
	FPhysicsActorHandle NewActor = nullptr; // why zero and not the default INDEX_NONE?
	return { InShape.Shape, InShape.bSimulation, InShape.bQuery, NewActor };
}

FPhysicsGeometryCollection_Chaos FPhysInterface_Chaos::GetGeometryCollection(const FPhysicsShapeHandle& InShape)
{
	FPhysicsGeometryCollection_Chaos NewCollection(InShape);
	return NewCollection;
}

FPhysicsGeometryCollection_Chaos::~FPhysicsGeometryCollection_Chaos() = default;
FPhysicsGeometryCollection_Chaos::FPhysicsGeometryCollection_Chaos(FPhysicsGeometryCollection_Chaos&& Steal) = default;

ECollisionShapeType FPhysicsGeometryCollection_Chaos::GetType() const
{
	return GetImplicitType(Geom);
}

const Chaos::TImplicitObject<float, 3>& FPhysicsGeometryCollection_Chaos::GetGeometry() const
{
	return Geom;
}

const Chaos::TBox<float, 3>& FPhysicsGeometryCollection_Chaos::GetBoxGeometry() const
{
	return Geom.GetObjectChecked<Chaos::TBox<float, 3>>();
}

const Chaos::TSphere<float, 3>&  FPhysicsGeometryCollection_Chaos::GetSphereGeometry() const
{
	return Geom.GetObjectChecked<Chaos::TSphere<float, 3>>();
}
const Chaos::TCapsule<float>&  FPhysicsGeometryCollection_Chaos::GetCapsuleGeometry() const
{
	return Geom.GetObjectChecked<Chaos::TCapsule<float>>();
}

const Chaos::TConvex<float, 3>& FPhysicsGeometryCollection_Chaos::GetConvexGeometry() const
{
	return Geom.GetObjectChecked<Chaos::TConvex<float, 3>>();
}

const Chaos::TTriangleMeshImplicitObject<float>& FPhysicsGeometryCollection_Chaos::GetTriMeshGeometry() const
{
	return Geom.GetObjectChecked<Chaos::TTriangleMeshImplicitObject<float>>();
}

FPhysicsGeometryCollection_Chaos::FPhysicsGeometryCollection_Chaos(const FPhysicsShapeReference_Chaos& InShape)
	: Geom(InShape.GetGeometry())
{
}

FCollisionFilterData FPhysInterface_Chaos::GetSimulationFilter(const FPhysicsShapeHandle& InShape)
{
	return FCollisionFilterData();
}

FCollisionFilterData FPhysInterface_Chaos::GetQueryFilter(const FPhysicsShapeHandle& InShape)
{
	return FCollisionFilterData();
}

void FPhysInterface_Chaos::SetQueryFilter(const FPhysicsShapeReference_Chaos& InShapeRef, const FCollisionFilterData& InFilter)
{
	InShapeRef.Shape->QueryData = InFilter;
}

void FPhysInterface_Chaos::SetSimulationFilter(const FPhysicsShapeReference_Chaos& InShapeRef, const FCollisionFilterData& InFilter)
{
	InShapeRef.Shape->SimData = InFilter;
}

bool FPhysInterface_Chaos::IsSimulationShape(const FPhysicsShapeHandle& InShape)
{
    return InShape.bSimulation;
}

bool FPhysInterface_Chaos::IsQueryShape(const FPhysicsShapeHandle& InShape)
{
    return InShape.bQuery;
}

bool FPhysInterface_Chaos::IsShapeType(const FPhysicsShapeReference_Chaos& InShapeRef, ECollisionShapeType InType)
{
    if (InType == ECollisionShapeType::Box && InShapeRef.Shape->Geometry->GetType() == Chaos::ImplicitObjectType::Box)
    {
        return true;
    }
    if (InType == ECollisionShapeType::Sphere && InShapeRef.Shape->Geometry->GetType() == Chaos::ImplicitObjectType::Sphere)
    {
        return true;
    }
    if (InType == ECollisionShapeType::Plane && InShapeRef.Shape->Geometry->GetType() == Chaos::ImplicitObjectType::Plane)
    {
        return true;
    }
    if (InType == ECollisionShapeType::Capsule && InShapeRef.Shape->Geometry->GetType() == Chaos::ImplicitObjectType::Capsule)
    {
        return true;
    }
    if (InType == ECollisionShapeType::Convex && InShapeRef.Shape->Geometry->GetType() == Chaos::ImplicitObjectType::Convex)
    {
        return true;
    }
    if (InType == ECollisionShapeType::Trimesh && InShapeRef.Shape->Geometry->GetType() == Chaos::ImplicitObjectType::TriangleMesh)
    {
        return true;
    }
    if (InType == ECollisionShapeType::Heightfield && InShapeRef.Shape->Geometry->GetType() == Chaos::ImplicitObjectType::HeightField)
    {
        return true;
    }
    if (InType == ECollisionShapeType::Scaled && InShapeRef.Shape->Geometry->GetType() == Chaos::ImplicitObjectType::Scaled)
    {
        return true;
    }

    return false;
}

ECollisionShapeType FPhysInterface_Chaos::GetShapeType(const FPhysicsShapeReference_Chaos& InShapeRef, bool bGetInnerType)
{
	Chaos::ImplicitObjectType Type = InShapeRef.Shape->Geometry->GetType(true);
	if (bGetInnerType && Type == Chaos::ImplicitObjectType::Scaled)
	{
		Type = InShapeRef.Shape->Geometry->GetObject<Chaos::TImplicitObjectScaled<float, 3>>()->GetUnscaledObject()->GetType(true);
	}

    if (Type == Chaos::ImplicitObjectType::Box)
    {
        return ECollisionShapeType::Box;
    }
    if (Type == Chaos::ImplicitObjectType::Sphere)
    {
        return ECollisionShapeType::Sphere;
    }
    if (Type == Chaos::ImplicitObjectType::Plane)
    {
        return ECollisionShapeType::Plane;
    }
    if (Type == Chaos::ImplicitObjectType::Capsule)
    {
        return ECollisionShapeType::Capsule;
    }
    if (Type == Chaos::ImplicitObjectType::Convex)
    {
        return ECollisionShapeType::Convex;
    }
    if (Type == Chaos::ImplicitObjectType::TriangleMesh)
    {
        return ECollisionShapeType::Trimesh;
    }
    if (Type == Chaos::ImplicitObjectType::HeightField)
    {
        return ECollisionShapeType::Heightfield;
    }
    if (Type == Chaos::ImplicitObjectType::Scaled)
    {
        return ECollisionShapeType::Scaled;
    }

    return ECollisionShapeType::None;
}

FTransform FPhysInterface_Chaos::GetLocalTransform(const FPhysicsShapeReference_Chaos& InShapeRef)
{
    // Transforms are baked into the object so there is never a local transform
    if (InShapeRef.Shape->Geometry->GetType() == Chaos::ImplicitObjectType::Transformed && FPhysicsInterface::IsValid(InShapeRef.ActorRef))
    {
        return InShapeRef.Shape->Geometry->GetObject<Chaos::TImplicitObjectTransformed<float, 3>>()->GetTransform();
    }
    else
    {
        return FTransform();
    }
}

void FPhysInterface_Chaos::SetLocalTransform(const FPhysicsShapeHandle& InShape, const FTransform& NewLocalTransform)
{
#if !WITH_CHAOS_NEEDS_TO_BE_FIXED
    if (InShape.ActorRef.IsValid())
    {
        TArray<RigidBodyId> Ids = {InShape.ActorRef.GetId()};
        const auto Index = InShape.ActorRef.GetScene()->GetIndexFromId(InShape.ActorRef.GetId());
        if (InShape.Object->GetType() == Chaos::ImplicitObjectType::Transformed)
        {
            // @todo(mlentine): We can avoid creating a new object here by adding delayed update support for the object transforms
            LocalParticles.SetDynamicGeometry(Index, MakeUnique<Chaos::TImplicitObjectTransformed<float, 3>>(InShape.Object->GetObject<Chaos::TImplicitObjectTransformed<float, 3>>()->Object(), NewLocalTransform));
        }
        else
        {
            LocalParticles.SetDynamicGeometry(Index, MakeUnique<Chaos::TImplicitObjectTransformed<float, 3>>(InShape.Object, NewLocalTransform));
        }
    }
    {
        if (InShape.Object->GetType() == Chaos::ImplicitObjectType::Transformed)
        {
            InShape.Object->GetObject<Chaos::TImplicitObjectTransformed<float, 3>>()->SetTransform(NewLocalTransform);
        }
        else
        {
            const_cast<FPhysicsShapeHandle&>(InShape).Object = new Chaos::TImplicitObjectTransformed<float, 3>(InShape.Object, NewLocalTransform);
        }
    }
#endif
}

void FinishSceneStat()
{
}

#if WITH_PHYSX

void CalculateMassPropertiesOfImplicitType(
	TArray< Chaos::TMassProperties<float, 3> > & MassProperties,
	float & TotalMass,
	const Chaos::TRigidTransform<float, 3> & WorldTransform,
	const Chaos::TImplicitObject<float, 3>* ImplicitObject, 
	float InDensityKGPerCM)
{
	// WIP
	// @todo : Support center of mass offsets.
	// @todo : Support Mass space alignment. 

	if (ImplicitObject)
	{
		Chaos::TVector<float, 3> Scale = WorldTransform.GetScale3D();
		Chaos::PMatrix<float, 3, 3> ScaleM(Scale.X, Scale.Y, Scale.Z);

		if (ImplicitObject->GetType() == Chaos::ImplicitObjectType::Sphere)
		{
			const Chaos::TSphere<float, 3> * Sphere = ImplicitObject->template GetObject<Chaos::TSphere<float, 3>>();
			float Mass = Sphere->GetVolume()*InDensityKGPerCM;

			Chaos::TMassProperties<float, 3> MassProperty;
			MassProperty.CenterOfMass = Chaos::TVector<float, 3>(0);
			MassProperty.Volume = Sphere->GetVolume();
			MassProperty.InertiaTensor = Sphere->GetInertiaTensor(Mass)*ScaleM;
			MassProperty.RotationOfMass = Chaos::TRotation<float, 3>::FromIdentity();
			//MassProperty.RotationOfMass = Chaos::TransformToLocalSpace<float,3>(MassProperty.InertiaTensor);

			MassProperties.Add(MassProperty);
			TotalMass += Mass;
		}
		else if (ImplicitObject->GetType() == Chaos::ImplicitObjectType::Box)
		{
			const Chaos::TBox<float, 3> * Box = ImplicitObject->template GetObject<Chaos::TBox<float, 3>>();
			float Mass = Box->GetVolume()*InDensityKGPerCM;

			Chaos::TMassProperties<float, 3> MassProperty;
			MassProperty.CenterOfMass = Chaos::TVector<float,3>(0);
			MassProperty.Volume = Box->GetVolume();
			MassProperty.InertiaTensor = Box->GetInertiaTensor(Mass); // What's the box? Scale!
			MassProperty.RotationOfMass = Chaos::TRotation<float, 3>::FromIdentity();
			//MassProperty.RotationOfMass = Chaos::TransformToLocalSpace<float, 3>(MassProperty.InertiaTensor);

			MassProperties.Add(MassProperty);
			TotalMass += Mass;
		}
		else if (ImplicitObject->GetType() == Chaos::ImplicitObjectType::Capsule)
		{
			const Chaos::TCapsule<float> * Capsule = ImplicitObject->template GetObject<Chaos::TCapsule<float>>();
			float Mass = Capsule->GetVolume()*InDensityKGPerCM;

			Chaos::TMassProperties<float, 3> MassProperty;
			MassProperty.CenterOfMass = Chaos::TVector<float, 3>(0);
			MassProperty.Volume = Capsule->GetVolume();
			MassProperty.InertiaTensor = Capsule->GetInertiaTensor(Mass)*ScaleM;
			MassProperty.RotationOfMass = Chaos::TRotation<float, 3>::FromIdentity();
			//MassProperty.RotationOfMass = Chaos::TransformToLocalSpace<float, 3>(MassProperty.InertiaTensor);

			MassProperties.Add(MassProperty);
			TotalMass += Mass;
		}
		else if (ImplicitObject->GetType() == Chaos::ImplicitObjectType::Cylinder)
		{
			const Chaos::TCylinder<float> * Cylinder = ImplicitObject->template GetObject<Chaos::TCylinder<float>>();
			float Mass = Cylinder->GetVolume()*InDensityKGPerCM;

			Chaos::TMassProperties<float, 3> MassProperty;
			MassProperty.CenterOfMass = Chaos::TVector<float, 3>(0);
			MassProperty.Volume = Cylinder->GetVolume();
			MassProperty.InertiaTensor = Cylinder->GetInertiaTensor(Mass)*ScaleM;
			MassProperty.RotationOfMass = Chaos::TRotation<float, 3>::FromIdentity();
			//MassProperty.RotationOfMass = Chaos::TransformToLocalSpace<float, 3>(MassProperty.InertiaTensor);

			MassProperties.Add(MassProperty);
			TotalMass += Mass;
		}
		else if (ImplicitObject->GetType() == Chaos::ImplicitObjectType::TaperedCylinder)
		{
			const Chaos::TTaperedCylinder<float> * TaperedCylinder = ImplicitObject->template GetObject<Chaos::TTaperedCylinder<float>>();
			float Mass = TaperedCylinder->GetVolume()*InDensityKGPerCM;

			Chaos::TMassProperties<float, 3> MassProperty;
			MassProperty.CenterOfMass = Chaos::TVector<float, 3>(0);
			MassProperty.Volume = TaperedCylinder->GetVolume();
			MassProperty.InertiaTensor = TaperedCylinder->GetInertiaTensor(Mass)*ScaleM;
			MassProperty.RotationOfMass = Chaos::TRotation<float, 3>::FromIdentity();
			//MassProperty.RotationOfMass = Chaos::TransformToLocalSpace<float, 3>(MassProperty.InertiaTensor);

			MassProperties.Add(MassProperty);
			TotalMass += Mass;
		}
		else if (ImplicitObject->GetType() == Chaos::ImplicitObjectType::Convex)
		{
			// @todo: until this is actually used by anything that matters, just keep it simple. 
			const Chaos::TConvex<float,3> * Convex = ImplicitObject->template GetObject<Chaos::TConvex<float,3>>();
			float Mass = Convex->BoundingBox().GetVolume()*InDensityKGPerCM;

			Chaos::TMassProperties<float, 3> MassProperty;
			MassProperty.CenterOfMass = Chaos::TVector<float, 3>(0);
			MassProperty.Volume = Convex->BoundingBox().GetVolume();
			MassProperty.InertiaTensor = Chaos::TBox<float, 3>::GetInertiaTensor(Mass, Convex->BoundingBox().Extents());
			MassProperty.RotationOfMass = Chaos::TRotation<float, 3>::FromIdentity();
			//MassProperty.RotationOfMass = Chaos::TransformToLocalSpace<float, 3>(MassProperty.InertiaTensor);

			MassProperties.Add(MassProperty);
			TotalMass += Mass;
		}
		else if (ImplicitObject->GetType() == Chaos::ImplicitObjectType::TriangleMesh)
		{
			// @todo: until this is actually used by anything that matters, just keep it simple. 
			const Chaos::TTriangleMeshImplicitObject<float> * TriangleMeshImplicitObject = ImplicitObject->template GetObject<Chaos::TTriangleMeshImplicitObject<float>>();
			float Mass = TriangleMeshImplicitObject->BoundingBox().GetVolume()*InDensityKGPerCM;

			Chaos::TMassProperties<float, 3> MassProperty;
			MassProperty.CenterOfMass = Chaos::TVector<float, 3>(0);
			MassProperty.Volume = TriangleMeshImplicitObject->BoundingBox().GetVolume();
			MassProperty.InertiaTensor = Chaos::TBox<float, 3>::GetInertiaTensor(Mass, TriangleMeshImplicitObject->BoundingBox().Extents());
			MassProperty.RotationOfMass = Chaos::TRotation<float, 3>::FromIdentity();
			//MassProperty.RotationOfMass = Chaos::TransformToLocalSpace<float, 3>(MassProperty.InertiaTensor);

			MassProperties.Add(MassProperty);
			TotalMass += Mass;
		}
		else if (ImplicitObject->GetType() == Chaos::ImplicitObjectType::Scaled)
		{
			const Chaos::TImplicitObjectScaled<float, 3> * ScaledObject = ImplicitObject->template GetObject<Chaos::TImplicitObjectScaled<float, 3>>();
			if (ensureMsgf(ScaledObject->GetUnscaledObject(), TEXT("Error : Null internal MObject type within TImplicitObjectScaled.")))
			{
				Chaos::TRigidTransform<float, 3> ScaledWorldTransform(WorldTransform.GetTranslation(), WorldTransform.GetRotation(), ScaledObject->GetScale());
				CalculateMassPropertiesOfImplicitType(MassProperties, TotalMass, ScaledWorldTransform, ScaledObject->GetUnscaledObject(), InDensityKGPerCM);
			}
		}
		else if (ImplicitObject->GetType() == Chaos::ImplicitObjectType::Union)
		{
			const Chaos::TImplicitObjectUnion<float,3> * ImplicitUnion = ImplicitObject->template GetObject<Chaos::TImplicitObjectUnion<float,3>>();

			for (const TUniquePtr < Chaos::TImplicitObject<float,3> > & ImplicitSubObject : ImplicitUnion->GetObjects())
			{
				CalculateMassPropertiesOfImplicitType(MassProperties, TotalMass, WorldTransform, ImplicitSubObject.Get(), InDensityKGPerCM);
			}
		}
		else
		{
			// @todo : Enable when ready.
			//ensureMsgf(false, TEXT("Unsupported implicit object type (%s)."), *(ImplicitObject->GetTypeName().ToString()) );
		}
	}
}

void FPhysInterface_Chaos::CalculateMassPropertiesFromShapeCollection(physx::PxMassProperties& OutProperties, const TArray<FPhysicsShapeHandle>& InShapes, float InDensityKGPerCM)
{
	float TotalMass = 0;
	TArray< Chaos::TMassProperties<float,3> > MassProperties;

	for (const FPhysicsShapeHandle& ShapeHandle : InShapes)
	{
		if (const Chaos::TPerShapeData<float, 3>* Shape = ShapeHandle.Shape)
		{
			if (const Chaos::TImplicitObject<float, 3> * ImplicitObject = Shape->Geometry.Get())
			{
				FTransform WorldTransform(ShapeHandle.ActorRef->R(), ShapeHandle.ActorRef->X());
				CalculateMassPropertiesOfImplicitType(MassProperties, TotalMass, WorldTransform, ImplicitObject, InDensityKGPerCM);
			}
		}
	}


	Chaos::PMatrix<float, 3, 3> Tensor;
	if (MassProperties.Num())
	{
		Tensor = Chaos::Combine<float, 3>(MassProperties).InertiaTensor;
	}
	else 
	{
		// @todo : Add support for all types, but for now just hard code a unit sphere tensor {r:50cm} if the type was not processed
		Tensor = Chaos::PMatrix<float, 3, 3>(5.24e5, 5.24e5, 5.24e5);
		TotalMass = 523.f;
	}

	float Mat[] = { Tensor.M[0][0],Tensor.M[0][1],Tensor.M[0][2],Tensor.M[1][0],Tensor.M[1][1],Tensor.M[1][2],Tensor.M[2][0],Tensor.M[2][1],Tensor.M[2][2] };
	OutProperties.inertiaTensor = PxMat33(Mat);
	OutProperties.mass = TotalMass;
}

#endif

bool FPhysInterface_Chaos::LineTrace_Geom(FHitResult& OutHit, const FBodyInstance* InInstance, const FVector& WorldStart, const FVector& WorldEnd, bool bTraceComplex, bool bExtractPhysMaterial)
{
	// Need an instance to trace against
	check(InInstance);

	OutHit.TraceStart = WorldStart;
	OutHit.TraceEnd = WorldEnd;

	bool bHitSomething = false;

	const FVector Delta = WorldEnd - WorldStart;
	const float DeltaMag = Delta.Size();
	if (DeltaMag > KINDA_SMALL_NUMBER)
	{
		{
			// #PHYS2 Really need a concept for "multi" locks here - as we're locking ActorRef but not TargetInstance->ActorRef
			FPhysicsCommand::ExecuteRead(InInstance->ActorHandle, [&](const FPhysicsActorHandle& Actor)
			{
				// If we're welded then the target instance is actually our parent
				const FBodyInstance* TargetInstance = InInstance->WeldParent ? InInstance->WeldParent : InInstance;
				if(const Chaos::TGeometryParticle<float, 3>* RigidBody = TargetInstance->ActorHandle)
				{
					FRaycastHit BestHit;
					BestHit.Distance = FLT_MAX;

					// Get all the shapes from the actor
					PhysicsInterfaceTypes::FInlineShapeArray Shapes;
					const int32 NumShapes = FillInlineShapeArray_AssumesLocked(Shapes, Actor);

					const FTransform WorldTM(RigidBody->R(), RigidBody->X());
					const FVector LocalStart = WorldTM.InverseTransformPositionNoScale(WorldStart);
					const FVector LocalDelta = WorldTM.InverseTransformVectorNoScale(Delta);

					// Iterate over each shape
					for (int32 ShapeIdx = 0; ShapeIdx < NumShapes; ShapeIdx++)
					{
						// #PHYS2 - SHAPES - Resolve this single cast case
						FPhysicsShapeReference_Chaos& ShapeRef = Shapes[ShapeIdx];
						Chaos::TPerShapeData<float, 3>* Shape = ShapeRef.Shape;
						check(Shape);

						if (TargetInstance->IsShapeBoundToBody(ShapeRef) == false)
						{
							continue;
						}

						// Filter so we trace against the right kind of collision
						FCollisionFilterData ShapeFilter = Shape->QueryData;
						const bool bShapeIsComplex = (ShapeFilter.Word3 & EPDF_ComplexCollision) != 0;
						const bool bShapeIsSimple = (ShapeFilter.Word3 & EPDF_SimpleCollision) != 0;
						if ((bTraceComplex && bShapeIsComplex) || (!bTraceComplex && bShapeIsSimple))
						{

							float Distance;
							Chaos::TVector<float, 3> LocalPosition;
							Chaos::TVector<float, 3> LocalNormal;

							int32 FaceIndex;
							if (Shape->Geometry->Raycast(LocalStart, LocalDelta / DeltaMag, DeltaMag, 0, Distance, LocalPosition, LocalNormal, FaceIndex))
							{
								if (Distance < BestHit.Distance)
								{
									BestHit.Distance = Distance;
									BestHit.WorldNormal = LocalNormal;	//will convert to world when best is chosen
									BestHit.WorldPosition = LocalPosition;
									BestHit.Shape = Shape;
									BestHit.Actor = Actor;
								}
							}
						}
					}

					if (BestHit.Distance < FLT_MAX)
					{
						BestHit.WorldNormal = WorldTM.TransformVectorNoScale(BestHit.WorldNormal);
						BestHit.WorldPosition = WorldTM.TransformPositionNoScale(BestHit.WorldPosition);

						// we just like to make sure if the hit is made, set to test touch
						FCollisionFilterData QueryFilter;
						QueryFilter.Word2 = 0xFFFFF;

						FTransform StartTM(WorldStart);
						const UPrimitiveComponent* OwnerComponentInst = InInstance->OwnerComponent.Get();
						ConvertQueryImpactHit(OwnerComponentInst ? OwnerComponentInst->GetWorld() : nullptr, BestHit, OutHit, DeltaMag, QueryFilter, WorldStart, WorldEnd, nullptr, StartTM, true, bExtractPhysMaterial);
						bHitSomething = true;
					}
				}
			});
		}
	}

	return bHitSomething;
}

bool FPhysInterface_Chaos::Sweep_Geom(FHitResult& OutHit, const FBodyInstance* InInstance, const FVector& InStart, const FVector& InEnd, const FQuat& InShapeRotation, const FCollisionShape& InShape, bool bSweepComplex)
{
	bool bSweepHit = false;

	if (InShape.IsNearlyZero())
	{
		bSweepHit = LineTrace_Geom(OutHit, InInstance, InStart, InEnd, bSweepComplex);
	}
	else
	{
		OutHit.TraceStart = InStart;
		OutHit.TraceEnd = InEnd;

		const FBodyInstance* TargetInstance = InInstance->WeldParent ? InInstance->WeldParent : InInstance;

		FPhysicsCommand::ExecuteRead(TargetInstance->ActorHandle, [&](const FPhysicsActorHandle& Actor)
		{
			const Chaos::TGeometryParticle<float, 3>* RigidBody = Actor;

			if (RigidBody && InInstance->OwnerComponent.Get())
			{
				FPhysicsShapeAdapter ShapeAdapter(InShapeRotation, InShape);

				const FVector Delta = InEnd - InStart;
				const float DeltaMag = Delta.Size();
				if (DeltaMag > KINDA_SMALL_NUMBER)
				{
					const FTransform ActorTM(RigidBody->R(), RigidBody->X());

					UPrimitiveComponent* OwnerComponentInst = InInstance->OwnerComponent.Get();
					FTransform StartTM(ShapeAdapter.GetGeomOrientation(), InStart);
					FTransform CompTM(OwnerComponentInst->GetComponentTransform());

					Chaos::TVector<float,3> Dir = Delta / DeltaMag;

					FSweepHit Hit;

					// Get all the shapes from the actor
					PhysicsInterfaceTypes::FInlineShapeArray Shapes;
					// #PHYS2 - SHAPES - Resolve this function to not use px stuff
					const int32 NumShapes = FillInlineShapeArray_AssumesLocked(Shapes, Actor); // #PHYS2 - Need a lock/execute here?

					// Iterate over each shape
					for (int32 ShapeIdx = 0; ShapeIdx < NumShapes; ShapeIdx++)
					{
						FPhysicsShapeReference_Chaos& ShapeRef = Shapes[ShapeIdx];
						Chaos::TPerShapeData<float, 3>* Shape = ShapeRef.Shape;
						check(Shape);

						// Skip shapes not bound to this instance
						if (!TargetInstance->IsShapeBoundToBody(ShapeRef))
						{
							continue;
						}

						// Filter so we trace against the right kind of collision
						FCollisionFilterData ShapeFilter = Shape->QueryData;;
						const bool bShapeIsComplex = (ShapeFilter.Word3 & EPDF_ComplexCollision) != 0;
						const bool bShapeIsSimple = (ShapeFilter.Word3 & EPDF_SimpleCollision) != 0;
						if ((bSweepComplex && bShapeIsComplex) || (!bSweepComplex && bShapeIsSimple))
						{
							//question: this is returning first result, is that valid? Keeping it the same as physx for now
							Chaos::TVector<float, 3> WorldPosition;
							Chaos::TVector<float, 3> WorldNormal;
							int32 FaceIdx;
							if(Chaos::SweepQuery<float, 3>(*Shape->Geometry, ActorTM, ShapeAdapter.GetGeometry(), StartTM, Dir, DeltaMag, Hit.Distance, WorldPosition, WorldNormal, FaceIdx))
							{
								// we just like to make sure if the hit is made
								FCollisionFilterData QueryFilter;
								QueryFilter.Word2 = 0xFFFFF;

								// we don't get Shape information when we access via PShape, so I filled it up
								Hit.Shape = Shape;
								Hit.Actor = ShapeRef.ActorRef;
								Hit.WorldPosition = WorldPosition;
								Hit.WorldNormal = WorldNormal;
								Hit.FaceIndex = FaceIdx;
								if (!HadInitialOverlap(Hit))
								{
									Hit.FaceIndex = FindFaceIndex(Hit, Dir);
								}

								FTransform StartTransform(InStart);
								ConvertQueryImpactHit(OwnerComponentInst->GetWorld(), Hit, OutHit, DeltaMag, QueryFilter, InStart, InEnd, nullptr, StartTransform, false, false);
								bSweepHit = true;
							}
						}
					}
				}
			}
		});
	}

	return bSweepHit;
}

bool Overlap_GeomInternal(const FBodyInstance* InInstance, const Chaos::TImplicitObject<float, 3>& InGeom, const FTransform& GeomTransform, FMTDResult* OutOptResult)
{
	const FBodyInstance* TargetInstance = InInstance->WeldParent ? InInstance->WeldParent : InInstance;
	Chaos::TGeometryParticle<float, 3>* RigidBody = TargetInstance->ActorHandle;

	if (RigidBody == nullptr)
	{
		return false;
	}

	// Get all the shapes from the actor
	PhysicsInterfaceTypes::FInlineShapeArray Shapes;
	const int32 NumShapes = FillInlineShapeArray_AssumesLocked(Shapes, RigidBody);

	const FTransform ActorTM(RigidBody->R(), RigidBody->X());

	// Iterate over each shape
	for (int32 ShapeIdx = 0; ShapeIdx < NumShapes; ++ShapeIdx)
	{
		FPhysicsShapeReference_Chaos& ShapeRef = Shapes[ShapeIdx];
		const Chaos::TPerShapeData<float,3>* Shape = ShapeRef.Shape;
		check(Shape);

		if (TargetInstance->IsShapeBoundToBody(ShapeRef))
		{
			//Chaos::TVector<float,3> POutDirection;
			//float OutDistance;

			if (OutOptResult)
			{
				ensure(false);	//todo: implement in Chaos
				/*
				PxTransform PTransform = U2PTransform(FPhysicsInterface::GetTransform(ShapeRef));
				if (PxGeometryQuery::computePenetration(POutDirection, OutDistance, InPxGeom, ShapePose, PShape->getGeometry().any(), PTransform))
				{
					//TODO: there are some edge cases that give us nan results. In these cases we skip
					if (!POutDirection.isFinite())
					{
						POutDirection.x = 0.f;
						POutDirection.y = 0.f;
						POutDirection.z = 0.f;
					}

					OutOptResult->Direction = P2UVector(POutDirection);
					OutOptResult->Distance = FMath::Abs(OutDistance);

					return true;
				}*/
				OutOptResult->Distance = 0;
			}
			//else	//todo: don't bother with this once MTD is implemented
			{
				if(Chaos::OverlapQuery<float, 3>(*Shape->Geometry, ActorTM, InGeom, GeomTransform))
				{
					return true;
				}
			}

		}
	}

	return false;
}

bool FPhysInterface_Chaos::Overlap_Geom(const FBodyInstance* InBodyInstance, const FPhysicsGeometryCollection& InGeometry, const FTransform& InShapeTransform, FMTDResult* OutOptResult)
{
	return Overlap_GeomInternal(InBodyInstance, InGeometry.GetGeometry(), InShapeTransform, OutOptResult);
}

bool FPhysInterface_Chaos::Overlap_Geom(const FBodyInstance* InBodyInstance, const FCollisionShape& InCollisionShape, const FQuat& InShapeRotation, const FTransform& InShapeTransform, FMTDResult* OutOptResult)
{
	FPhysicsShapeAdapter Adaptor(InShapeRotation, InCollisionShape);
	return Overlap_GeomInternal(InBodyInstance, Adaptor.GetGeometry(), Adaptor.GetGeomPose(InShapeTransform.GetTranslation()), OutOptResult);
}

bool FPhysInterface_Chaos::GetSquaredDistanceToBody(const FBodyInstance* InInstance, const FVector& InPoint, float& OutDistanceSquared, FVector* OutOptPointOnBody)
{
	if (OutOptPointOnBody)
	{
		*OutOptPointOnBody = InPoint;
		OutDistanceSquared = 0.f;
	}

	float ReturnDistance = -1.f;
	float MinPhi = BIG_NUMBER;
	bool bFoundValidBody = false;
	bool bEarlyOut = true;

	const FBodyInstance* UseBI = InInstance->WeldParent ? InInstance->WeldParent : InInstance;
	const FTransform BodyTM = UseBI->GetUnrealWorldTransform();
	const FVector LocalPoint = BodyTM.InverseTransformPositionNoScale(InPoint);

	FPhysicsCommand::ExecuteRead(UseBI->ActorHandle, [&](const FPhysicsActorHandle& Actor)
	{

		bEarlyOut = false;

		TArray<FPhysicsShapeReference_Chaos> Shapes;
		InInstance->GetAllShapes_AssumesLocked(Shapes);
		for (const FPhysicsShapeReference_Chaos& Shape : Shapes)
		{
			if (UseBI->IsShapeBoundToBody(Shape) == false)	//skip welded shapes that do not belong to us
			{
				continue;
			}

			bool bGetInnerType = true;
			ECollisionShapeType GeomType = FPhysicsInterface::GetShapeType(Shape, bGetInnerType);

			if (GeomType == ECollisionShapeType::Trimesh)
			{
				// Type unsupported for this function, but some other shapes will probably work. 
				continue;
			}

			bFoundValidBody = true;

			Chaos::TVector<float, 3> Normal;
			const float Phi = Shape.Shape->Geometry->PhiWithNormal(LocalPoint, Normal);
			if (Phi <= 0)
			{
				break;
			}
			else if (Phi < MinPhi)
			{
				MinPhi = Phi;
				OutDistanceSquared = Phi * Phi;
				if (OutOptPointOnBody)
				{
					const Chaos::TVector<float, 3> LocalClosestPoint = LocalPoint - Phi * Normal;
					*OutOptPointOnBody = BodyTM.TransformPositionNoScale(LocalClosestPoint);
				}
			}
		}
	});

	if (!bFoundValidBody && !bEarlyOut)
	{
		UE_LOG(LogPhysics, Verbose, TEXT("GetDistanceToBody: Component (%s) has no simple collision and cannot be queried for closest point."), InInstance->OwnerComponent.Get() ? *(InInstance->OwnerComponent->GetPathName()) : TEXT("NONE"));
	}

	return bFoundValidBody;
}

template<typename AllocatorType>
int32 GetAllShapesInternal_AssumedLocked(const FPhysicsActorHandle& InActorHandle, TArray<FPhysicsShapeReference_Chaos, AllocatorType>& OutShapes)
{
	OutShapes.Reset();
	const Chaos::TShapesArray<float, 3>& ShapesArray = InActorHandle->ShapesArray();
	//todo: can we avoid this construction?
	for (const auto& Shape : ShapesArray)
	{
		OutShapes.Add(FPhysicsShapeReference_Chaos(Shape.Get(), true, true, InActorHandle));
	}
	return OutShapes.Num();
}

template <>
int32 FPhysInterface_Chaos::GetAllShapes_AssumedLocked(const FPhysicsActorHandle& InActorHandle, TArray<FPhysicsShapeReference_Chaos, FDefaultAllocator>& OutShapes)
{
	return GetAllShapesInternal_AssumedLocked(InActorHandle, OutShapes);
}

template <>
int32 FPhysInterface_Chaos::GetAllShapes_AssumedLocked(const FPhysicsActorHandle& InActorHandle, PhysicsInterfaceTypes::FInlineShapeArray& OutShapes)
{
	return GetAllShapesInternal_AssumedLocked(InActorHandle, OutShapes);
}


FPhysicsShapeAdapter_Chaos::FPhysicsShapeAdapter_Chaos(const FQuat& Rot, const FCollisionShape& CollisionShape)
	: GeometryRotation(Rot)
{
	switch (CollisionShape.ShapeType)
	{
		case ECollisionShape::Capsule:
		{
			const float CapsuleRadius = CollisionShape.GetCapsuleRadius();
			const float CapsuleHalfHeight = CollisionShape.GetCapsuleHalfHeight();
			if (CapsuleRadius < CapsuleHalfHeight)
			{
				const float UseHalfHeight = FMath::Max(CollisionShape.GetCapsuleAxisHalfLength(), FCollisionShape::MinCapsuleAxisHalfHeight());
				const FVector Bot = FVector(0.f, 0.f, -UseHalfHeight);
				const FVector Top = FVector(0.f, 0.f, UseHalfHeight);
				const float UseRadius = FMath::Max(CapsuleRadius, FCollisionShape::MinCapsuleRadius());
				Geometry = TUniquePtr<FPhysicsGeometry>(new Chaos::TCapsule<float>(Bot, Top, UseRadius));
			}
			else
			{
				// Use a sphere instead.
				const float UseRadius = FMath::Max(CapsuleRadius, FCollisionShape::MinSphereRadius());
				Geometry = TUniquePtr<FPhysicsGeometry>(new Chaos::TSphere<float,3>(Chaos::TVector<float,3>(0), UseRadius));
			}
			break;
		}
		case ECollisionShape::Box:
		{
			Chaos::TVector<float,3> HalfExtents = CollisionShape.GetBox();
			HalfExtents.X = FMath::Max(HalfExtents.X, FCollisionShape::MinBoxExtent());
			HalfExtents.Y = FMath::Max(HalfExtents.Y, FCollisionShape::MinBoxExtent());
			HalfExtents.Z = FMath::Max(HalfExtents.Z, FCollisionShape::MinBoxExtent());

			Geometry = TUniquePtr<FPhysicsGeometry>(new Chaos::TBox<float, 3>(-HalfExtents, HalfExtents));
			break;
		}
		case ECollisionShape::Sphere:
		{
			const float UseRadius = FMath::Max(CollisionShape.GetSphereRadius(), FCollisionShape::MinSphereRadius());
			Geometry = TUniquePtr<FPhysicsGeometry>(new Chaos::TSphere<float, 3>(Chaos::TVector<float, 3>(0), UseRadius));
			break;
		}
		default:
			ensure(false);
			break;
	}
}

const FPhysicsGeometry& FPhysicsShapeAdapter_Chaos::GetGeometry() const
{
	return *Geometry;
}

FTransform FPhysicsShapeAdapter_Chaos::GetGeomPose(const FVector& Pos) const
{
	return FTransform(GeometryRotation, Pos);
}

const FQuat& FPhysicsShapeAdapter_Chaos::GetGeomOrientation() const
{
	return GeometryRotation;
}
#endif
