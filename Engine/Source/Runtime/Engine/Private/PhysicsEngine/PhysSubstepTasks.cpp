// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsEngine/PhysSubstepTasks.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Physics/PhysScene_PhysX.h"

#if PHYSICS_INTERFACE_PHYSX
#include "PhysXPublic.h"

uint8* PhysXCompletionTask::GetScratchBufferData()
{
	return ScratchBuffer ? ScratchBuffer->Buffer : nullptr;
}

int32 PhysXCompletionTask::GetScratchBufferSize()
{
	return ScratchBuffer ? ScratchBuffer->BufferSize : 0;
}
#endif

struct FSubstepCallbackGuard
{
#if !UE_BUILD_SHIPPING
	FSubstepCallbackGuard(FPhysSubstepTask& InSubstepTask) : SubstepTask(InSubstepTask)
	{
		++SubstepTask.SubstepCallbackGuard;
	}

	~FSubstepCallbackGuard()
	{
		--SubstepTask.SubstepCallbackGuard;
	}

	FPhysSubstepTask& SubstepTask;
#else
	FSubstepCallbackGuard(FPhysSubstepTask&)
	{
	}
#endif
};

#if PHYSICS_INTERFACE_PHYSX
FPhysSubstepTask::FPhysSubstepTask(PxApexScene * GivenScene, FPhysScene* InPhysScene) :
	NumSubsteps(0),
	SubTime(0.f),
	DeltaSeconds(0.f),
	FullSimulationTask(0),
	Alpha(0.f),
	StepScale(0.f),
	TotalSubTime(0.f),
	CurrentSubStep(0),
	SubstepCallbackGuard(0),
	PhysScene(InPhysScene),
	PAScene(GivenScene)
{
	check(PAScene);
}
#endif

void FPhysSubstepTask::SwapBuffers()
{
	External = !External;
}

void FPhysSubstepTask::RemoveBodyInstance_AssumesLocked(FBodyInstance* BodyInstance)
{
	PhysTargetBuffers[External].Remove(BodyInstance);
	PhysTargetBuffers[!External].Remove(BodyInstance);
}

void FPhysSubstepTask::SetKinematicTarget_AssumesLocked(FBodyInstance* Body, const FTransform& TM)
{
#if WITH_PHYSX
	TM.DiagnosticCheck_IsValid();

	//We only interpolate kinematic actors that need it
	if (!Body->IsNonKinematic() && Body->ShouldInterpolateWhenSubStepping())
	{
		FKinematicTarget_AssumesLocked KinmaticTarget(Body, TM);
		FPhysTarget & TargetState = PhysTargetBuffers[External].FindOrAdd(Body);
		TargetState.bKinematicTarget = true;
		TargetState.KinematicTarget = KinmaticTarget;
	}
#endif
}

bool FPhysSubstepTask::GetKinematicTarget_AssumesLocked(const FBodyInstance* Body, FTransform& OutTM) const
{
#if WITH_PHYSX
	if (const FPhysTarget* TargetState = PhysTargetBuffers[External].Find(Body))
	{
		if (TargetState->bKinematicTarget)
		{
			OutTM = TargetState->KinematicTarget.TargetTM;
			return true;
		}
	}
#endif

	return false;
}

void FPhysSubstepTask::AddCustomPhysics_AssumesLocked(FBodyInstance* Body, const FCalculateCustomPhysics& CalculateCustomPhysics)
{
#if WITH_PHYSX
	//Limit custom physics to non kinematic actors
	if (Body->IsNonKinematic())
	{
		FCustomTarget CustomTarget(CalculateCustomPhysics);

		FPhysTarget & TargetState = PhysTargetBuffers[External].FindOrAdd(Body);
		TargetState.CustomPhysics.Add(CustomTarget);
	}
#endif
}

#if !UE_BUILD_SHIPPING
#define SUBSTEPPING_WARNING() ensureMsgf(SubstepCallbackGuard == 0, TEXT("Applying a sub-stepped force from a substep callback. This usually indicates an error. Make sure you're only using physx data, and that you are adding non-substepped forces"));
#else
#define SUBSTEPPING_WARNING()
#endif

void FPhysSubstepTask::AddForce_AssumesLocked(FBodyInstance* Body, const FVector& Force, bool bAccelChange)
{
#if WITH_PHYSX
	//We should only apply forces on non kinematic actors
	if (Body->IsNonKinematic())
	{
		SUBSTEPPING_WARNING()
		FForceTarget ForceTarget;
		ForceTarget.bPosition = false;
		ForceTarget.Force = Force;
		ForceTarget.bAccelChange = bAccelChange;

		FPhysTarget & TargetState = PhysTargetBuffers[External].FindOrAdd(Body);
		TargetState.Forces.Add(ForceTarget);
	}
#endif
}

void FPhysSubstepTask::AddForceAtPosition_AssumesLocked(FBodyInstance* Body, const FVector& Force, const FVector& Position, bool bIsLocalForce)
{
#if WITH_PHYSX
	if (Body->IsNonKinematic())
	{
		SUBSTEPPING_WARNING()
		FForceTarget ForceTarget;
		ForceTarget.bPosition = true;
		ForceTarget.Force = Force;
		ForceTarget.Position = Position;
		ForceTarget.bIsLocalForce = bIsLocalForce;

		FPhysTarget & TargetState = PhysTargetBuffers[External].FindOrAdd(Body);
		TargetState.Forces.Add(ForceTarget);
	}
#endif
}
void FPhysSubstepTask::AddTorque_AssumesLocked(FBodyInstance* Body, const FVector& Torque, bool bAccelChange)
{
#if WITH_PHYSX
	//We should only apply torque on non kinematic actors
	if (Body->IsNonKinematic())
	{
		SUBSTEPPING_WARNING()
		FTorqueTarget TorqueTarget;
		TorqueTarget.Torque = Torque;
		TorqueTarget.bAccelChange = bAccelChange;

		FPhysTarget & TargetState = PhysTargetBuffers[External].FindOrAdd(Body);
		TargetState.Torques.Add(TorqueTarget);
	}
#endif
}

void FPhysSubstepTask::ClearTorques_AssumesLocked(FBodyInstance* Body)
{
#if WITH_PHYSX
	if (Body->IsNonKinematic())
	{
		SUBSTEPPING_WARNING()
		FPhysTarget & TargetState = PhysTargetBuffers[External].FindOrAdd(Body);
		TargetState.Torques.Empty();
	}
#endif
}

void FPhysSubstepTask::AddRadialForceToBody_AssumesLocked(FBodyInstance* Body, const FVector& Origin, const float Radius, const float Strength, const uint8 Falloff, const bool bAccelChange)
{
#if WITH_PHYSX
	//We should only apply torque on non kinematic actors
	if (Body->IsNonKinematic())
	{
		SUBSTEPPING_WARNING()
		FRadialForceTarget RadialForceTarget;
		RadialForceTarget.Origin = Origin;
		RadialForceTarget.Radius = Radius;
		RadialForceTarget.Strength = Strength;
		RadialForceTarget.Falloff = Falloff;
		RadialForceTarget.bAccelChange = bAccelChange;

		FPhysTarget & TargetState = PhysTargetBuffers[External].FindOrAdd(Body);
		TargetState.RadialForces.Add(RadialForceTarget);
	}
#endif
}

void FPhysSubstepTask::ClearForces_AssumesLocked(FBodyInstance* Body)
{
#if WITH_PHYSX
	if (Body->IsNonKinematic())
	{
		SUBSTEPPING_WARNING()
		FPhysTarget & TargetState = PhysTargetBuffers[External].FindOrAdd(Body);
		TargetState.Forces.Empty();
		TargetState.RadialForces.Empty();
	}
#endif
}

/** Applies custom physics - Assumes caller has obtained writer lock */
void FPhysSubstepTask::ApplyCustomPhysics(const FPhysTarget& PhysTarget, FBodyInstance* BodyInstance, float DeltaTime)
{
#if WITH_PHYSX
	FSubstepCallbackGuard Guard(*this);
	for (int32 i = 0; i < PhysTarget.CustomPhysics.Num(); ++i)
	{
		const FCustomTarget& CustomTarget = PhysTarget.CustomPhysics[i];

		CustomTarget.CalculateCustomPhysics->ExecuteIfBound(DeltaTime, BodyInstance);
	}
#endif
}

#if PHYSICS_INTERFACE_PHYSX
bool IsKinematicHelper(const PxRigidBody* PRigidBody)
{
	const bool bIsKinematic = PRigidBody->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC;
	return bIsKinematic;
}
#endif

/** Applies forces - Assumes caller has obtained writer lock */
void FPhysSubstepTask::ApplyForces_AssumesLocked(const FPhysTarget& PhysTarget, FBodyInstance* BodyInstance)
{
#if WITH_PHYSX
#if WITH_CHAOS || WITH_IMMEDIATE_PHYSX
    check(false);
#else
	/** Apply Forces */
	PxRigidBody* PRigidBody = FPhysicsInterface::GetPxRigidBody_AssumesLocked(BodyInstance->GetPhysicsActorHandle());

	for (int32 i = 0; i < PhysTarget.Forces.Num(); ++i)
	{
		const FForceTarget& ForceTarget = PhysTarget.Forces[i];

		if (ForceTarget.bPosition)
		{
			if (!ForceTarget.bIsLocalForce)
			{
				PxRigidBodyExt::addForceAtPos(*PRigidBody, U2PVector(ForceTarget.Force), U2PVector(ForceTarget.Position), PxForceMode::eFORCE, true);
			}
			else
			{
				PxRigidBodyExt::addLocalForceAtLocalPos(*PRigidBody, U2PVector(ForceTarget.Force), U2PVector(ForceTarget.Position), PxForceMode::eFORCE, true);
			}
		}
		else
		{
			PRigidBody->addForce(U2PVector(ForceTarget.Force), ForceTarget.bAccelChange ? PxForceMode::eACCELERATION : PxForceMode::eFORCE, true);
		}
	}
#endif
#endif
}

/** Applies torques - Assumes caller has obtained writer lock */
void FPhysSubstepTask::ApplyTorques_AssumesLocked(const FPhysTarget& PhysTarget, FBodyInstance* BodyInstance)
{
#if WITH_PHYSX
#if WITH_CHAOS || WITH_IMMEDIATE_PHYSX
    check(false);
#else
	/** Apply Torques */
	PxRigidBody* PRigidBody = FPhysicsInterface_PhysX::GetPxRigidBody_AssumesLocked(BodyInstance->GetPhysicsActorHandle());

	for (int32 i = 0; i < PhysTarget.Torques.Num(); ++i)
	{
		const FTorqueTarget& TorqueTarget = PhysTarget.Torques[i];
		PRigidBody->addTorque(U2PVector(TorqueTarget.Torque), TorqueTarget.bAccelChange ? PxForceMode::eACCELERATION : PxForceMode::eFORCE, true);
	}
#endif
#endif
}

/** Applies radial forces - Assumes caller has obtained writer lock */
void FPhysSubstepTask::ApplyRadialForces_AssumesLocked(const FPhysTarget& PhysTarget, FBodyInstance* BodyInstance)
{
#if WITH_PHYSX
#if WITH_CHAOS || WITH_IMMEDIATE_PHYSX
    check(false);
#else
	/** Apply Torques */
	PxRigidBody* PRigidBody = FPhysicsInterface_PhysX::GetPxRigidBody_AssumesLocked(BodyInstance->GetPhysicsActorHandle());

	for (int32 i = 0; i < PhysTarget.RadialForces.Num(); ++i)
	{
		const FRadialForceTarget& RadialForceTArget= PhysTarget.RadialForces[i];
		AddRadialForceToPxRigidBody_AssumesLocked(*PRigidBody, RadialForceTArget.Origin, RadialForceTArget.Radius, RadialForceTArget.Strength, RadialForceTArget.Falloff, RadialForceTArget.bAccelChange);
	}
#endif
#endif
}


/** Interpolates kinematic actor transform - Assumes caller has obtained writer lock */
void FPhysSubstepTask::InterpolateKinematicActor_AssumesLocked(const FPhysTarget& PhysTarget, FBodyInstance* BodyInstance, float InAlpha)
{
#if WITH_PHYSX
#if WITH_CHAOS || WITH_IMMEDIATE_PHYSX
    check(false);
#else
	PxRigidDynamic * PRigidDynamic = FPhysicsInterface_PhysX::GetPxRigidDynamic_AssumesLocked(BodyInstance->GetPhysicsActorHandle());
	InAlpha = FMath::Clamp(InAlpha, 0.f, 1.f);

	/** Interpolate kinematic actors */
	if (PhysTarget.bKinematicTarget)
	{
		//It's possible that the actor is no longer kinematic and is now simulating. In that case do nothing
		if (!BodyInstance->IsNonKinematic())
		{
			const FKinematicTarget& KinematicTarget = PhysTarget.KinematicTarget;
			const FTransform& TargetTM = KinematicTarget.TargetTM;
			const FTransform& StartTM = KinematicTarget.OriginalTM;
			FTransform InterTM = FTransform::Identity;

			InterTM.SetLocation(FMath::Lerp(StartTM.GetLocation(), TargetTM.GetLocation(), InAlpha));
			InterTM.SetRotation(FMath::Lerp(StartTM.GetRotation(), TargetTM.GetRotation(), InAlpha));

			PxTransform PNewPose = U2PTransform(InterTM);
			if (!ensureMsgf(PNewPose.isValid(), TEXT("Found an Invalid Pose for %s. Using previous pose."), *BodyInstance->GetBodyDebugName()))
			{
				PRigidDynamic->getKinematicTarget(PNewPose);
				if (!ensureMsgf(PNewPose.isValid(), TEXT("Previous pose is invalid. Using identity.")))
				{
					PNewPose = U2PTransform(FTransform::Identity);
				}
			}
			PRigidDynamic->setKinematicTarget(PNewPose);
		}
	}
#endif
#endif
}

void FPhysSubstepTask::SubstepInterpolation(float InAlpha, float DeltaTime)
{
#if PHYSICS_INTERFACE_PHYSX
#if WITH_APEX
	SCOPED_APEX_SCENE_WRITE_LOCK(PAScene);
	PxScene * PScene = PAScene->getPhysXScene();
#else
	PxScene * PScene = PAScene;
	SCOPED_SCENE_WRITE_LOCK(PScene);
#endif
#if WITH_CHAOS || WITH_IMMEDIATE_PHYSX
    check(false);
#else

	/** Note: We lock the entire scene before iterating. The assumption is that removing an FBodyInstance from the map will also be wrapped by this lock */
	
	
	PhysTargetMap& Targets = PhysTargetBuffers[!External];

	for (PhysTargetMap::TIterator Itr = Targets.CreateIterator(); Itr; ++Itr)
	{
		FPhysTarget & PhysTarget = Itr.Value();
		FBodyInstance * BodyInstance = Itr.Key();
		PxRigidBody* PRigidBody = FPhysicsInterface_PhysX::GetPxRigidBody_AssumesLocked(BodyInstance->GetPhysicsActorHandle());

		if (PRigidBody == NULL)
		{
			continue;
		}

		//We should only be iterating over actors that belong to this scene
		check(PRigidBody->getScene() == PScene);

		if (!IsKinematicHelper(PRigidBody))
		{
			ApplyCustomPhysics(PhysTarget, BodyInstance, DeltaTime);
			ApplyForces_AssumesLocked(PhysTarget, BodyInstance);
			ApplyTorques_AssumesLocked(PhysTarget, BodyInstance);
			ApplyRadialForces_AssumesLocked(PhysTarget, BodyInstance);
		}else
		{
			InterpolateKinematicActor_AssumesLocked(PhysTarget, BodyInstance, InAlpha);
		}
	}

	/** Final substep */
	if (InAlpha >= 1.f)
	{
		Targets.Empty(Targets.Num());
	}
#endif
#endif
}

float FPhysSubstepTask::UpdateTime(float UseDelta)
{
	float FrameRate = 1.f;
	uint32 MaxSubSteps = 1;

	UPhysicsSettings * PhysSetting = UPhysicsSettings::Get();
	FrameRate = PhysSetting->MaxSubstepDeltaTime;
	MaxSubSteps = PhysSetting->MaxSubsteps;
	
	float FrameRateInv = 1.f / FrameRate;

	//Figure out how big dt to make for desired framerate
	DeltaSeconds = FMath::Min(UseDelta, MaxSubSteps * FrameRate);
	NumSubsteps = FMath::CeilToInt(DeltaSeconds * FrameRateInv);
	NumSubsteps = FMath::Max(NumSubsteps > MaxSubSteps ? MaxSubSteps : NumSubsteps, (uint32) 1);
	SubTime = DeltaSeconds / NumSubsteps;

	return SubTime;
}

#if PHYSICS_INTERFACE_PHYSX
void FPhysSubstepTask::StepSimulation(PhysXCompletionTask * Task)
{
	check(SubTime > 0.f);
	check(DeltaSeconds > 0.f);

	FullSimulationTask = Task;
	Alpha = 0.f;
	StepScale = SubTime / DeltaSeconds;
	TotalSubTime = 0.f;
	CurrentSubStep = 0;

	SubstepSimulationStart();
}
#endif

DECLARE_CYCLE_STAT(TEXT("Phys SubstepStart"), STAT_SubstepSimulationStart, STATGROUP_Physics);
DECLARE_CYCLE_STAT(TEXT("Phys SubstepEnd"), STAT_SubstepSimulationEnd, STATGROUP_Physics);

void FPhysSubstepTask::SubstepSimulationStart()
{
	SCOPE_CYCLE_COUNTER(STAT_TotalPhysicsTime);
	SCOPE_CYCLE_COUNTER(STAT_SubstepSimulationStart);
#if PHYSICS_INTERFACE_PHYSX
	check(SubTime > 0.f);
	check(DeltaSeconds > 0.f);
	
	check(!CompletionEvent.GetReference());	//should be done
	CompletionEvent = FGraphEvent::CreateGraphEvent();
	PhysXCompletionTask* SubstepTask = new PhysXCompletionTask(CompletionEvent,PAScene->getTaskManager());
	ENamedThreads::Type NamedThread = PhysSingleThreadedMode() ? ENamedThreads::GameThread : ENamedThreads::SetTaskPriority(ENamedThreads::GameThread, ENamedThreads::HighTaskPriority);

	DECLARE_CYCLE_STAT(TEXT("FDelegateGraphTask.ProcessPhysSubstepSimulation"),
		STAT_FDelegateGraphTask_ProcessPhysSubstepSimulation,
		STATGROUP_TaskGraphTasks);

	FDelegateGraphTask::CreateAndDispatchWhenReady(
		FDelegateGraphTask::FDelegate::CreateRaw(this, &FPhysSubstepTask::SubstepSimulationEnd),
		GET_STATID(STAT_FDelegateGraphTask_ProcessPhysSubstepSimulation), CompletionEvent, ENamedThreads::GameThread, NamedThread);

	++CurrentSubStep;	

	bool bLastSubstep = CurrentSubStep >= NumSubsteps;

	if (!bLastSubstep)
	{

		Alpha += StepScale;
		TotalSubTime += SubTime;
	}

	float DeltaTime = bLastSubstep ? (DeltaSeconds - TotalSubTime) : SubTime;
	float Interpolation = bLastSubstep ? 1.f : Alpha;

	// Call scene step delegate
#if !WITH_CHAOS
	if (PhysScene != nullptr)
	{
		FSubstepCallbackGuard Guard(*this);
		PhysScene->OnPhysSceneStep.Broadcast(PhysScene, DeltaTime);
	}
#endif

	SubstepInterpolation(Interpolation, DeltaTime);

#if WITH_APEX
	PAScene->simulate(DeltaTime, bLastSubstep, SubstepTask, FullSimulationTask->GetScratchBufferData(), FullSimulationTask->GetScratchBufferSize());
#else
	PAScene->lockWrite();
	PAScene->simulate(DeltaTime, SubstepTask, FullSimulationTask->GetScratchBufferData(), FullSimulationTask->GetScratchBufferSize());
	PAScene->unlockWrite();
#endif

	SubstepTask->removeReference();
#endif
}

void FPhysSubstepTask::SubstepSimulationEnd(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
#if PHYSICS_INTERFACE_PHYSX
	CompletionEvent = NULL;
	if (CurrentSubStep < NumSubsteps)
	{
		uint32 OutErrorCode = 0;
		{
			SCOPE_CYCLE_COUNTER(STAT_TotalPhysicsTime);
			SCOPE_CYCLE_COUNTER(STAT_SubstepSimulationEnd);

			{
#if WITH_APEX
				SCOPED_APEX_SCENE_WRITE_LOCK(PAScene);
				PxScene* PScene = PAScene->getPhysXScene();
#else
				PxScene* PScene = PAScene;
				SCOPED_SCENE_WRITE_LOCK(PScene);
#endif

				// Dont rebuild scene queries here as this is an intermediate step
				PScene->setSceneQueryUpdateMode(PxSceneQueryUpdateMode::eBUILD_DISABLED_COMMIT_DISABLED);

				PAScene->fetchResults(true, &OutErrorCode);

				// re-enable query updates (the next fetchResults will rebuild the SQ tree)
				PScene->setSceneQueryUpdateMode(PxSceneQueryUpdateMode::eBUILD_ENABLED_COMMIT_ENABLED);
			}
		}

		if (OutErrorCode != 0)
		{
			UE_LOG(LogPhysics, Log, TEXT("PHYSX FETCHRESULTS ERROR: %d"), OutErrorCode);
		}

		SubstepSimulationStart();
	}
	else
	{
		SCOPE_CYCLE_COUNTER(STAT_TotalPhysicsTime);
		SCOPE_CYCLE_COUNTER(STAT_SubstepSimulationEnd);

		//final step we call fetch on in game thread
		FullSimulationTask->removeReference();
	}
#endif
}
