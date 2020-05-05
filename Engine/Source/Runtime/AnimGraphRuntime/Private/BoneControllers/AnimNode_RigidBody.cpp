// Copyright Epic Games, Inc. All Rights Reserved.

#include "BoneControllers/AnimNode_RigidBody.h"
#include "AnimationRuntime.h"
#include "Animation/AnimInstanceProxy.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Physics/PhysicsInterfaceCore.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsActorHandle.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsSimulation.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsStats.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Logging/MessageLog.h"

//PRAGMA_DISABLE_OPTIMIZATION

/////////////////////////////////////////////////////
// FAnimNode_RigidBody

#define LOCTEXT_NAMESPACE "ImmediatePhysics"

DEFINE_STAT(STAT_RigidBodyNodeInitTime);

CSV_DECLARE_CATEGORY_MODULE_EXTERN(ENGINE_API, Animation);

TAutoConsoleVariable<int32> CVarEnableRigidBodyNode(TEXT("p.RigidBodyNode"), 1, TEXT("Enables/disables rigid body node updates and evaluations"), ECVF_Default);
TAutoConsoleVariable<int32> CVarRigidBodyLODThreshold(TEXT("p.RigidBodyLODThreshold"), -1, TEXT("Max LOD that rigid body node is allowed to run on. Provides a global threshold that overrides per-node the LODThreshold property. -1 means no override."), ECVF_Scalability);

int32 RBAN_MaxSubSteps = 4;
bool bRBAN_EnableTimeBasedReset = true;
FAutoConsoleVariableRef CVarRigidBodyNodeMaxSteps(TEXT("p.RigidBodyNode.MaxSubSteps"), RBAN_MaxSubSteps, TEXT("Set the maximum number of simulation steps in the update loop"), ECVF_Default);
FAutoConsoleVariableRef CVarRigidBodyNodeEnableTimeBasedReset(TEXT("p.RigidBodyNode.EnableTimeBasedReset"), bRBAN_EnableTimeBasedReset, TEXT("If true, Rigid Body nodes are reset when they have not been updated for a while (default true)"), ECVF_Default);

FAnimNode_RigidBody::FAnimNode_RigidBody():
	QueryParams(NAME_None, FCollisionQueryParams::GetUnknownStatId())
{
	AccumulatedDeltaTime = 0.0f;
	ResetSimulatedTeleportType = ETeleportType::None;
	PhysicsSimulation = nullptr;
	OverridePhysicsAsset = nullptr;
	UsePhysicsAsset = nullptr;
	bOverrideWorldGravity = false;
	CachedBoundsScale = 1.2f;
	SimulationSpace = ESimulationSpace::ComponentSpace;
	ExternalForce = FVector::ZeroVector;
#if WITH_EDITORONLY_DATA
	bComponentSpaceSimulation_DEPRECATED = true;
#endif
	OverrideWorldGravity = FVector::ZeroVector;
	TotalMass = 0.f;
	CachedBounds.W = 0;
	PhysScene = nullptr;
	UnsafeWorld = nullptr;
	bSimulationStarted = false;
	bCheckForBodyTransformInit = false;
	OverlapChannel = ECC_WorldStatic;
	bEnableWorldGeometry = false;
	bTransferBoneVelocities = false;
	bFreezeIncomingPoseOnStart = false;
	bClampLinearTranslationLimitToRefPose = false;

	OverrideSolverIterations.SolverIterations = -1;
	OverrideSolverIterations.JointIterations = -1;
	OverrideSolverIterations.CollisionIterations = -1;
	OverrideSolverIterations.SolverPushOutIterations = -1;
	OverrideSolverIterations.JointPushOutIterations = -1;
	OverrideSolverIterations.CollisionPushOutIterations = -1;

	PreviousTransform = CurrentTransform = FTransform::Identity;
	PreviousComponentLinearVelocity = FVector::ZeroVector;	

	ComponentLinearAccScale = FVector::ZeroVector;
	ComponentLinearVelScale = FVector::ZeroVector;
	ComponentAppliedLinearAccClamp = FVector(10000,10000,10000);
	bForceDisableCollisionBetweenConstraintBodies = false;
}

FAnimNode_RigidBody::~FAnimNode_RigidBody()
{
	delete PhysicsSimulation;
}

void FAnimNode_RigidBody::GatherDebugData(FNodeDebugData& DebugData)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(GatherDebugData)
	FString DebugLine = DebugData.GetNodeName(this);

	DebugLine += "(";
	AddDebugNodeData(DebugLine);
	DebugLine += ")";

	DebugData.AddDebugItem(DebugLine);

	const bool bUsingFrozenPose = bFreezeIncomingPoseOnStart && bSimulationStarted && (CapturedFrozenPose.GetPose().GetNumBones() > 0);
	if (!bUsingFrozenPose)
	{
		ComponentPose.GatherDebugData(DebugData);
	}
}

FTransform SpaceToWorldTransform(ESimulationSpace Space, const FTransform& ComponentToWorld, const FTransform& BaseBoneTM)
{
	switch (Space)
	{
	case ESimulationSpace::ComponentSpace: 
		return ComponentToWorld;
	case ESimulationSpace::WorldSpace: 
		return FTransform::Identity;
	case ESimulationSpace::BaseBoneSpace:
		return BaseBoneTM * ComponentToWorld;
	default:
		return FTransform::Identity;
	}
}

FVector WorldVectorToSpaceNoScale(ESimulationSpace Space, const FVector& WorldDir, const FTransform& ComponentToWorld, const FTransform& BaseBoneTM)
{
	switch(Space)
	{
		case ESimulationSpace::ComponentSpace: return ComponentToWorld.InverseTransformVectorNoScale(WorldDir);
		case ESimulationSpace::WorldSpace: return WorldDir;
		case ESimulationSpace::BaseBoneSpace:
			return BaseBoneTM.InverseTransformVectorNoScale(ComponentToWorld.InverseTransformVectorNoScale(WorldDir));
		default: return FVector::ZeroVector;
	}
}

FVector WorldPositionToSpace(ESimulationSpace Space, const FVector& WorldPoint, const FTransform& ComponentToWorld, const FTransform& BaseBoneTM)
{
	switch (Space)
	{
		case ESimulationSpace::ComponentSpace: return ComponentToWorld.InverseTransformPosition(WorldPoint);
		case ESimulationSpace::WorldSpace: return WorldPoint;
		case ESimulationSpace::BaseBoneSpace:
			return BaseBoneTM.InverseTransformPosition(ComponentToWorld.InverseTransformPosition(WorldPoint));
		default: return FVector::ZeroVector;
	}
}

FORCEINLINE_DEBUGGABLE FTransform ConvertCSTransformToSimSpace(ESimulationSpace SimulationSpace, const FTransform& InCSTransform, const FTransform& ComponentToWorld, const FTransform& BaseBoneTM)
{
	switch (SimulationSpace)
	{
		case ESimulationSpace::ComponentSpace: return InCSTransform;
		case ESimulationSpace::WorldSpace:  return InCSTransform * ComponentToWorld; 
		case ESimulationSpace::BaseBoneSpace: return InCSTransform.GetRelativeTransform(BaseBoneTM); break;
		default: ensureMsgf(false, TEXT("Unsupported Simulation Space")); return InCSTransform;
	}
}

void FAnimNode_RigidBody::UpdateComponentPose_AnyThread(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(UpdateComponentPose_AnyThread)
	// Only freeze update graph after initial update, as we want to get that pose through.
	if (bFreezeIncomingPoseOnStart && bSimulationStarted && ResetSimulatedTeleportType == ETeleportType::None)
	{
		// If we have a Frozen Pose captured, 
		// then we don't need to update the rest of the graph.
		if (CapturedFrozenPose.GetPose().GetNumBones() > 0)
		{
		}
		else
		{
			// Create a new context with zero deltatime to freeze time in rest of the graph.
			// This will be used to capture a frozen pose.
			FAnimationUpdateContext FrozenContext = Context.FractionalWeightAndTime(1.f, 0.f);

			Super::UpdateComponentPose_AnyThread(FrozenContext);
		}
	}
	else
	{
		Super::UpdateComponentPose_AnyThread(Context);
	}
}

void FAnimNode_RigidBody::EvaluateComponentPose_AnyThread(FComponentSpacePoseContext& Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(EvaluateComponentPose_AnyThread)
	if (bFreezeIncomingPoseOnStart && bSimulationStarted)
	{
		// If we have a Frozen Pose captured, use it.
		// Only after our intialize setup. As we need new pose for that.
		if (ResetSimulatedTeleportType == ETeleportType::None && (CapturedFrozenPose.GetPose().GetNumBones() > 0))
		{
			Output.Pose.CopyPose(CapturedFrozenPose);
			Output.Curve.CopyFrom(CapturedFrozenCurves);
		}
		// Otherwise eval graph to capture it.
		else
		{
			Super::EvaluateComponentPose_AnyThread(Output);
			CapturedFrozenPose.CopyPose(Output.Pose);
			CapturedFrozenCurves.CopyFrom(Output.Curve);
		}
	}
	else
	{
		Super::EvaluateComponentPose_AnyThread(Output);
	}

	// Capture incoming pose if 'bTransferBoneVelocities' is set.
	// That is, until simulation starts.
	if (bTransferBoneVelocities && !bSimulationStarted)
	{
		CapturedBoneVelocityPose.CopyPose(Output.Pose);
		CapturedBoneVelocityPose.CopyAndAssignBoneContainer(CapturedBoneVelocityBoneContainer);
	}
}

void FAnimNode_RigidBody::InitializeNewBodyTransformsDuringSimulation(FComponentSpacePoseContext& Output, const FTransform& ComponentTransform, const FTransform& BaseBoneTM)
{
	for (const FOutputBoneData& OutputData : OutputBoneData)
	{
		const int32 BodyIndex = OutputData.BodyIndex;
		FBodyAnimData& BodyData = BodyAnimData[BodyIndex];
		if (!BodyData.bBodyTransformInitialized)
		{
			BodyData.bBodyTransformInitialized = true;

			// If we have a parent body, we need to grab relative transforms to it.
			if (OutputData.ParentBodyIndex != INDEX_NONE)
			{
				ensure(BodyAnimData[OutputData.ParentBodyIndex].bBodyTransformInitialized);

				FTransform BodyRelativeTransform = FTransform::Identity;
				for (const FCompactPoseBoneIndex CompactBoneIndex : OutputData.BoneIndicesToParentBody)
				{
					const FTransform& LocalSpaceTM = Output.Pose.GetLocalSpaceTransform(CompactBoneIndex);
					BodyRelativeTransform = BodyRelativeTransform * LocalSpaceTM;
				}

				const FTransform WSBodyTM = BodyRelativeTransform * Bodies[OutputData.ParentBodyIndex]->GetWorldTransform();
				Bodies[BodyIndex]->SetWorldTransform(WSBodyTM);
				BodyAnimData[BodyIndex].RefPoseLength = BodyRelativeTransform.GetLocation().Size();
			}
			// If we don't have a parent body, then we can just grab the incoming pose in component space.
			else
			{
				const FTransform& ComponentSpaceTM = Output.Pose.GetComponentSpaceTransform(OutputData.CompactPoseBoneIndex);
				const FTransform BodyTM = ConvertCSTransformToSimSpace(SimulationSpace, ComponentSpaceTM, ComponentTransform, BaseBoneTM);

				Bodies[BodyIndex]->SetWorldTransform(BodyTM);
			}
		}
	}
}

DECLARE_CYCLE_STAT(TEXT("RigidBody_Eval"), STAT_RigidBody_Eval, STATGROUP_Anim);

DECLARE_CYCLE_STAT(TEXT("FAnimNode_RigidBody::EvaluateSkeletalControl_AnyThread"), STAT_ImmediateEvaluateSkeletalControl, STATGROUP_ImmediatePhysics);

void FAnimNode_RigidBody::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(EvaluateSkeletalControl_AnyThread)
	SCOPE_CYCLE_COUNTER(STAT_RigidBody_Eval);
	CSV_SCOPED_TIMING_STAT(Animation, RigidBodyEval);
	FScopeCycleCounterUObject AdditionalScope(UsePhysicsAsset, GET_STATID(STAT_RigidBody_Eval));
	SCOPE_CYCLE_COUNTER(STAT_ImmediateEvaluateSkeletalControl);
	//SCOPED_NAMED_EVENT_TEXT("FAnimNode_RigidBody::EvaluateSkeletalControl_AnyThread", FColor::Magenta);

	// Update our eval counter, and decide whether we need to reset simulated bodies, if our anim instance hasn't updated in a while.
	if(EvalCounter.HasEverBeenUpdated())
	{
		// Always propagate skip rate as it can go up and down between updates
		EvalCounter.SetMaxSkippedFrames(Output.AnimInstanceProxy->GetEvaluationCounter().GetMaxSkippedFrames());
		if(!EvalCounter.WasSynchronizedLastFrame(Output.AnimInstanceProxy->GetEvaluationCounter())  && bRBAN_EnableTimeBasedReset)
		{
			ResetSimulatedTeleportType = ETeleportType::ResetPhysics;
		}
	}
	EvalCounter.SynchronizeWith(Output.AnimInstanceProxy->GetEvaluationCounter());

	const float DeltaSeconds = AccumulatedDeltaTime;
	AccumulatedDeltaTime = 0.f;

	if (bEnabled && PhysicsSimulation)	
	{
		const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
		const FTransform CompWorldSpaceTM = Output.AnimInstanceProxy->GetComponentTransform();
		if(!EvalCounter.HasEverBeenUpdated())
		{
			PreviousCompWorldSpaceTM = CompWorldSpaceTM;
		}
		const FTransform BaseBoneTM = Output.Pose.GetComponentSpaceTransform(BaseBoneRef.GetCompactPoseIndex(BoneContainer));

		PhysicsSimulation->SetSimulationSpaceTransform(SpaceToWorldTransform(SimulationSpace, CompWorldSpaceTM, BaseBoneTM));

		// Initialize potential new bodies because of LOD change.
		if (ResetSimulatedTeleportType == ETeleportType::None && bCheckForBodyTransformInit)
		{
			bCheckForBodyTransformInit = false;
			InitializeNewBodyTransformsDuringSimulation(Output, CompWorldSpaceTM, BaseBoneTM);
		}

		// If time advances, update simulation
		// Reset if necessary
		bool bDynamicsReset = (ResetSimulatedTeleportType != ETeleportType::None);
		if (bDynamicsReset)
		{
			// Capture bone velocities if we have captured a bone velocity pose.
			if (bTransferBoneVelocities && (CapturedBoneVelocityPose.GetPose().GetNumBones() > 0))
			{
				for (const FOutputBoneData& OutputData : OutputBoneData)
				{
					const int32 BodyIndex = OutputData.BodyIndex;
					FBodyAnimData& BodyData = BodyAnimData[BodyIndex];

					if (BodyData.bIsSimulated)
					{
						const FCompactPoseBoneIndex NextCompactPoseBoneIndex = OutputData.CompactPoseBoneIndex;
						// Convert CompactPoseBoneIndex to SkeletonBoneIndex...
						const int32 PoseSkeletonBoneIndex = BoneContainer.GetPoseToSkeletonBoneIndexArray()[NextCompactPoseBoneIndex.GetInt()];
						// ... So we can convert to the captured pose CompactPoseBoneIndex. 
						// In case there was a LOD change, and poses are not compatible anymore.
						const FCompactPoseBoneIndex PrevCompactPoseBoneIndex = CapturedBoneVelocityBoneContainer.GetCompactPoseIndexFromSkeletonIndex(PoseSkeletonBoneIndex);

						if (PrevCompactPoseBoneIndex != FCompactPoseBoneIndex(INDEX_NONE))
						{
							const FTransform PrevCSTM = CapturedBoneVelocityPose.GetComponentSpaceTransform(PrevCompactPoseBoneIndex);
							const FTransform NextCSTM = Output.Pose.GetComponentSpaceTransform(NextCompactPoseBoneIndex);

							const FTransform PrevSSTM = ConvertCSTransformToSimSpace(SimulationSpace, PrevCSTM, CompWorldSpaceTM, BaseBoneTM);
							const FTransform NextSSTM = ConvertCSTransformToSimSpace(SimulationSpace, NextCSTM, CompWorldSpaceTM, BaseBoneTM);

							// Linear Velocity
							if(DeltaSeconds > 0.0f)
							{
								BodyData.TransferedBoneLinearVelocity = ((NextSSTM.GetLocation() - PrevSSTM.GetLocation()) / DeltaSeconds);
							}
							else
							{
								BodyData.TransferedBoneLinearVelocity = (FVector::ZeroVector);
							}

							// Angular Velocity
							const FQuat DeltaRotation = (NextSSTM.GetRotation().Inverse() * PrevSSTM.GetRotation());
							const float RotationAngle = DeltaRotation.GetAngle() / DeltaSeconds;
							BodyData.TransferedBoneAngularVelocity = (FQuat(DeltaRotation.GetRotationAxis(), RotationAngle)); 
						}
					}
				}
			}

			
			switch(ResetSimulatedTeleportType)
			{
				case ETeleportType::TeleportPhysics:
				{
					// Teleport bodies.
					for (const FOutputBoneData& OutputData : OutputBoneData)
					{
						const int32 BodyIndex = OutputData.BodyIndex;
						BodyAnimData[BodyIndex].bBodyTransformInitialized = true;

						FTransform BodyTM = Bodies[BodyIndex]->GetWorldTransform();
						FTransform ComponentSpaceTM;

						switch(SimulationSpace)
						{
							case ESimulationSpace::ComponentSpace: ComponentSpaceTM = BodyTM; break;
							case ESimulationSpace::WorldSpace: ComponentSpaceTM = BodyTM.GetRelativeTransform(PreviousCompWorldSpaceTM); break;
							case ESimulationSpace::BaseBoneSpace: ComponentSpaceTM = BodyTM * BaseBoneTM; break;
							default: ensureMsgf(false, TEXT("Unsupported Simulation Space")); ComponentSpaceTM = BodyTM;
						}

						BodyTM = ConvertCSTransformToSimSpace(SimulationSpace, ComponentSpaceTM, CompWorldSpaceTM, BaseBoneTM);
						Bodies[BodyIndex]->SetWorldTransform(BodyTM);
						if (OutputData.ParentBodyIndex != INDEX_NONE)
						{
							BodyAnimData[BodyIndex].RefPoseLength = BodyTM.GetRelativeTransform(Bodies[OutputData.ParentBodyIndex]->GetWorldTransform()).GetLocation().Size();
						}
					}
				}
				break;

				case ETeleportType::ResetPhysics:
				{
					// Completely reset bodies.
					for (const FOutputBoneData& OutputData : OutputBoneData)
					{
						const int32 BodyIndex = OutputData.BodyIndex;
						BodyAnimData[BodyIndex].bBodyTransformInitialized = true;

						const FTransform& ComponentSpaceTM = Output.Pose.GetComponentSpaceTransform(OutputData.CompactPoseBoneIndex);
						const FTransform BodyTM = ConvertCSTransformToSimSpace(SimulationSpace, ComponentSpaceTM, CompWorldSpaceTM, BaseBoneTM);
						Bodies[BodyIndex]->SetWorldTransform(BodyTM);
						if (OutputData.ParentBodyIndex != INDEX_NONE)
						{
							BodyAnimData[BodyIndex].RefPoseLength = BodyTM.GetRelativeTransform(Bodies[OutputData.ParentBodyIndex]->GetWorldTransform()).GetLocation().Size();
						}
					}
				}
				break;
			}

			// Always reset after a teleport
			PreviousCompWorldSpaceTM = CompWorldSpaceTM;
			ResetSimulatedTeleportType = ETeleportType::None;
			PreviousComponentLinearVelocity = FVector::ZeroVector;
		}
		// Only need to tick physics if we didn't reset and we have some time to simulate
		if((bSimulateAnimPhysicsAfterReset || !bDynamicsReset) && DeltaSeconds > AnimPhysicsMinDeltaTime)
		{
			// Transfer bone velocities previously captured.
			if (bTransferBoneVelocities && (CapturedBoneVelocityPose.GetPose().GetNumBones() > 0))
			{
				for (const FOutputBoneData& OutputData : OutputBoneData)
				{
					const int32 BodyIndex = OutputData.BodyIndex;
					const FBodyAnimData& BodyData = BodyAnimData[BodyIndex];

					if (BodyData.bIsSimulated)
					{
						ImmediatePhysics::FActorHandle* Body = Bodies[BodyIndex];
						Body->SetLinearVelocity(BodyData.TransferedBoneLinearVelocity);

						const FQuat AngularVelocity = BodyData.TransferedBoneAngularVelocity;
						Body->SetAngularVelocity(AngularVelocity.GetRotationAxis() * AngularVelocity.GetAngle());
					}
				}

				// Free up our captured pose after it's been used.
				CapturedBoneVelocityPose.Empty();
			}
			else if (SimulationSpace != ESimulationSpace::WorldSpace)
			{
				// Calc linear velocity
				const FVector ComponentDeltaLocation = CurrentTransform.GetTranslation() - PreviousTransform.GetTranslation();
				const FVector ComponentLinearVelocity = ComponentDeltaLocation / DeltaSeconds;
				// Apply acceleration that opposed velocity (basically 'drag')
				FVector ApplyLinearAcc = WorldVectorToSpaceNoScale(SimulationSpace, -ComponentLinearVelocity, CompWorldSpaceTM, BaseBoneTM) * ComponentLinearVelScale;

				// Calc linear acceleration
				const FVector ComponentLinearAcceleration = (ComponentLinearVelocity - PreviousComponentLinearVelocity) / DeltaSeconds;
				PreviousComponentLinearVelocity = ComponentLinearVelocity;
				// Apply opposite acceleration to bodies
				ApplyLinearAcc += WorldVectorToSpaceNoScale(SimulationSpace, -ComponentLinearAcceleration, CompWorldSpaceTM, BaseBoneTM) * ComponentLinearAccScale;

				// Iterate over bodies
				for (const FOutputBoneData& OutputData : OutputBoneData)
				{
					const int32 BodyIndex = OutputData.BodyIndex;
					const FBodyAnimData& BodyData = BodyAnimData[BodyIndex];

					if (BodyData.bIsSimulated)
					{
						ImmediatePhysics::FActorHandle* Body = Bodies[BodyIndex];

						// Apply 
						const float BodyInvMass = Body->GetInverseMass();
						if (BodyInvMass > 0.f)
						{
							// Final desired acceleration to apply to body
							FVector FinalBodyLinearAcc = ApplyLinearAcc;

							// Clamp if desired
							if (!ComponentAppliedLinearAccClamp.IsNearlyZero())
							{
								FinalBodyLinearAcc = FinalBodyLinearAcc.BoundToBox(-ComponentAppliedLinearAccClamp, ComponentAppliedLinearAccClamp);
							}

							// Apply to body
							Body->AddForce(FinalBodyLinearAcc / BodyInvMass);
						}
					}
				}
			}

			// @todo(ccaulfield): We should be interpolating kinematic targets for each sub-step below
			for (const FOutputBoneData& OutputData : OutputBoneData)
			{
				const int32 BodyIndex = OutputData.BodyIndex;
				if (!BodyAnimData[BodyIndex].bIsSimulated)
				{
					const FTransform& ComponentSpaceTM = Output.Pose.GetComponentSpaceTransform(OutputData.CompactPoseBoneIndex);
					const FTransform BodyTM = ConvertCSTransformToSimSpace(SimulationSpace, ComponentSpaceTM, CompWorldSpaceTM, BaseBoneTM);

					Bodies[BodyIndex]->SetKinematicTarget(BodyTM);
				}
			}
			
			UpdateWorldForces(CompWorldSpaceTM, BaseBoneTM);
			const FVector SimSpaceGravity = WorldVectorToSpaceNoScale(SimulationSpace, WorldSpaceGravity, CompWorldSpaceTM, BaseBoneTM);

			// Run simulation at a minimum of 30 FPS to prevent system from exploding.
			// DeltaTime can be higher due to URO, so take multiple iterations in that case.
			const int32 MaxSteps = RBAN_MaxSubSteps;
			const float MaxDeltaSeconds = 1.f / 30.f;

#if !WITH_CHAOS
			const int32 NumSteps = FMath::Clamp(FMath::CeilToInt(DeltaSeconds / MaxDeltaSeconds), 1, MaxSteps);
			const float StepDeltaTime = DeltaSeconds / float(NumSteps);
			for (int32 Step = 1; Step <= NumSteps; Step++)
			{
				// We call the _AssumesLocked version here without a lock as the simulation is local to this node and we know
				// we're not going to alter anything while this is running.
				PhysicsSimulation->Simulate_AssumesLocked(StepDeltaTime, SimSpaceGravity);
			}
#else
			PhysicsSimulation->SetSolverIterations(
				SolverIterations.SolverIterations,
				SolverIterations.JointIterations,
				SolverIterations.CollisionIterations,
				SolverIterations.SolverPushOutIterations,
				SolverIterations.JointPushOutIterations,
				SolverIterations.CollisionPushOutIterations
			);
			PhysicsSimulation->SetSolverIterations(
				OverrideSolverIterations.SolverIterations, 
				OverrideSolverIterations.JointIterations, 
				OverrideSolverIterations.CollisionIterations, 
				OverrideSolverIterations.SolverPushOutIterations, 
				OverrideSolverIterations.JointPushOutIterations, 
				OverrideSolverIterations.CollisionPushOutIterations);

			PhysicsSimulation->Simulate_AssumesLocked(DeltaSeconds, MaxDeltaSeconds, MaxSteps, SimSpaceGravity);
#endif
		}
		
		//write back to animation system
		for (const FOutputBoneData& OutputData : OutputBoneData)
		{
			const int32 BodyIndex = OutputData.BodyIndex;
			if (BodyAnimData[BodyIndex].bIsSimulated)
			{
				FTransform BodyTM = Bodies[BodyIndex]->GetWorldTransform();

				// if we clamp translation, we only do this when all linear translation are locked
				// 
				if (bClampLinearTranslationLimitToRefPose
					&&BodyAnimData[BodyIndex].LinearXMotion == ELinearConstraintMotion::LCM_Locked
					&& BodyAnimData[BodyIndex].LinearYMotion == ELinearConstraintMotion::LCM_Locked
					&& BodyAnimData[BodyIndex].LinearZMotion == ELinearConstraintMotion::LCM_Locked)
				{
					// grab local space of length from ref pose 
					// we have linear limit value - see if that works
					// calculate current local space from parent
					// find parent transform
					const int32 ParentBodyIndex = OutputData.ParentBodyIndex;
					FTransform ParentTransform = FTransform::Identity;
					if (ParentBodyIndex != INDEX_NONE)
					{
						ParentTransform = Bodies[ParentBodyIndex]->GetWorldTransform();
					}

					// get local transform
					FTransform LocalTransform = BodyTM.GetRelativeTransform(ParentTransform);
					const float CurrentLength = LocalTransform.GetTranslation().Size();

					// this is inconsistent with constraint. The actual linear limit is set by constraint
					if (!FMath::IsNearlyEqual(CurrentLength, BodyAnimData[BodyIndex].RefPoseLength, KINDA_SMALL_NUMBER))
					{
						float RefPoseLength = BodyAnimData[BodyIndex].RefPoseLength;
						if (CurrentLength > RefPoseLength)
						{
							float Scale = (CurrentLength > KINDA_SMALL_NUMBER) ? RefPoseLength / CurrentLength : 0.f;
							// we don't use 1.f here because 1.f can create pops based on float issue. 
							// so we only activate clamping when less than 90%
							if (Scale < 0.9f)
							{
								LocalTransform.ScaleTranslation(Scale);
								BodyTM = LocalTransform * ParentTransform;
								Bodies[BodyIndex]->SetWorldTransform(BodyTM);
							}
						}
					}
				}

				FTransform ComponentSpaceTM;

				switch(SimulationSpace)
				{
					case ESimulationSpace::ComponentSpace: ComponentSpaceTM = BodyTM; break;
					case ESimulationSpace::WorldSpace: ComponentSpaceTM = BodyTM.GetRelativeTransform(CompWorldSpaceTM); break;
					case ESimulationSpace::BaseBoneSpace: ComponentSpaceTM = BodyTM * BaseBoneTM; break;
					default: ensureMsgf(false, TEXT("Unsupported Simulation Space")); ComponentSpaceTM = BodyTM;
				}
					
				OutBoneTransforms.Add(FBoneTransform(OutputData.CompactPoseBoneIndex, ComponentSpaceTM));
			}
		}

		PreviousCompWorldSpaceTM = CompWorldSpaceTM;
	}
}

void ComputeBodyInsertionOrder(TArray<FBoneIndexType>& InsertionOrder, const USkeletalMeshComponent& SKC)
{
	//We want to ensure simulated bodies are sorted by LOD so that the first simulated bodies are at the highest LOD.
	//Since LOD2 is a subset of LOD1 which is a subset of LOD0 we can change the number of simulated bodies without any reordering
	//For this to work we must first insert all simulated bodies in the right order. We then insert all the kinematic bodies in the right order

	InsertionOrder.Reset();

	const int32 NumLODs = SKC.GetNumLODs();
	if(NumLODs > 0)
	{
		TArray<bool> InSortedOrder;

		TArray<FBoneIndexType> RequiredBones0;
		TArray<FBoneIndexType> ComponentSpaceTMs0;
		SKC.ComputeRequiredBones(RequiredBones0, ComponentSpaceTMs0, 0, /*bIgnorePhysicsAsset=*/ true);

		InSortedOrder.AddZeroed(RequiredBones0.Num());

		auto MergeIndices = [&InsertionOrder, &InSortedOrder](const TArray<FBoneIndexType>& RequiredBones) -> void
		{
			for (FBoneIndexType BoneIdx : RequiredBones)
			{
				if (!InSortedOrder[BoneIdx])
				{
					InsertionOrder.Add(BoneIdx);
				}

				InSortedOrder[BoneIdx] = true;
			}
		};


		for(int32 LodIdx = NumLODs - 1; LodIdx > 0; --LodIdx)
		{
			TArray<FBoneIndexType> RequiredBones;
			TArray<FBoneIndexType> ComponentSpaceTMs;
			SKC.ComputeRequiredBones(RequiredBones, ComponentSpaceTMs, LodIdx, /*bIgnorePhysicsAsset=*/ true);
			MergeIndices(RequiredBones);
		}

		MergeIndices(RequiredBones0);
	}
}

void FAnimNode_RigidBody::InitPhysics(const UAnimInstance* InAnimInstance)
{
	SCOPE_CYCLE_COUNTER(STAT_RigidBodyNodeInitTime);

	delete PhysicsSimulation;
	PhysicsSimulation = nullptr;

	const USkeletalMeshComponent* SkeletalMeshComp = InAnimInstance->GetSkelMeshComponent();
	const USkeletalMesh* SkeletalMeshAsset = SkeletalMeshComp->SkeletalMesh;

	const FReferenceSkeleton& SkelMeshRefSkel = SkeletalMeshAsset->RefSkeleton;
	UsePhysicsAsset = OverridePhysicsAsset ? OverridePhysicsAsset : InAnimInstance->GetSkelMeshComponent()->GetPhysicsAsset();
		
	USkeleton* SkeletonAsset = InAnimInstance->CurrentSkeleton;
	ensure(SkeletonAsset == SkeletalMeshAsset->Skeleton);

	const int32 SkelMeshLinkupIndex = SkeletonAsset->GetMeshLinkupIndex(SkeletalMeshAsset);
	ensure(SkelMeshLinkupIndex != INDEX_NONE);
	const FSkeletonToMeshLinkup& SkeletonToMeshLinkupTable = SkeletonAsset->LinkupCache[SkelMeshLinkupIndex];
	const TArray<int32>& MeshToSkeletonBoneIndex = SkeletonToMeshLinkupTable.MeshToSkeletonTable;
	
	const int32 NumSkeletonBones = SkeletonAsset->GetReferenceSkeleton().GetNum();
	SkeletonBoneIndexToBodyIndex.Reset(NumSkeletonBones);
	SkeletonBoneIndexToBodyIndex.Init(INDEX_NONE, NumSkeletonBones);

	PreviousTransform = SkeletalMeshComp->GetComponentToWorld();

	if (UPhysicsSettings* Settings = UPhysicsSettings::Get())
	{
		AnimPhysicsMinDeltaTime = Settings->AnimPhysicsMinDeltaTime;
		bSimulateAnimPhysicsAfterReset = Settings->bSimulateAnimPhysicsAfterReset;
	}
	else
	{
		AnimPhysicsMinDeltaTime = 0.f;
		bSimulateAnimPhysicsAfterReset = false;
	}
	
	bEnabled = UsePhysicsAsset && SkeletalMeshComp->GetAllowRigidBodyAnimNode() && CVarEnableRigidBodyNode.GetValueOnAnyThread() != 0;
	if(bEnabled)
	{
		PhysicsSimulation = new ImmediatePhysics::FSimulation();
		const int32 NumBodies = UsePhysicsAsset->SkeletalBodySetups.Num();
		Bodies.Empty(NumBodies);
		ComponentsInSim.Reset();
		BodyAnimData.Reset(NumBodies);
		BodyAnimData.AddDefaulted(NumBodies);
		TotalMass = 0.f;

		// Instantiate a FBodyInstance/FConstraintInstance set that will be cloned into the Immediate Physics sim.
		// NOTE: We do not have a skeleton at the moment, so we have to use the ref pose
		TArray<FBodyInstance*> HighLevelBodyInstances;
		TArray<FConstraintInstance*> HighLevelConstraintInstances;

		// Chaos relies on the initial pose to set up constraint positions
		bool bCreateBodiesInRefPose = (WITH_CHAOS != 0);
		SkeletalMeshComp->InstantiatePhysicsAssetRefPose(
			*UsePhysicsAsset, 
			SimulationSpace == ESimulationSpace::WorldSpace ? SkeletalMeshComp->GetComponentToWorld().GetScale3D() : FVector(1.f), 
			HighLevelBodyInstances, 
			HighLevelConstraintInstances, 
			nullptr, 
			nullptr, 
			INDEX_NONE, 
			FPhysicsAggregateHandle(),
			bCreateBodiesInRefPose);

		TMap<FName, ImmediatePhysics::FActorHandle*> NamesToHandles;
		TArray<ImmediatePhysics::FActorHandle*> IgnoreCollisionActors;

		TArray<FBoneIndexType> InsertionOrder;
		ComputeBodyInsertionOrder(InsertionOrder, *SkeletalMeshComp);

		const int32 NumBonesLOD0 = InsertionOrder.Num();

		// If our skeleton is not the one that was used to build the PhysicsAsset, some bodies may be missing, or rearranged.
		// We need to map the original indices to the new bodies for use by the CollisionDisableTable.
		// NOTE: This array is indexed by the original BodyInstance body index (BodyInstance->InstanceBodyIndex)
		TArray<ImmediatePhysics::FActorHandle*> BodyIndexToActorHandle;
		BodyIndexToActorHandle.AddZeroed(HighLevelBodyInstances.Num());

		TArray<FBodyInstance*> BodiesSorted;
		BodiesSorted.AddZeroed(NumBonesLOD0);

		for (FBodyInstance* BI : HighLevelBodyInstances)
		{
			if(BI->IsValidBodyInstance())
			{
				BodiesSorted[BI->InstanceBoneIndex] = BI;
			}
		}

		// Create the immediate physics bodies
		for (FBoneIndexType InsertBone : InsertionOrder)
		{
			if (FBodyInstance* BodyInstance = BodiesSorted[InsertBone])
			{
				UBodySetup* BodySetup = UsePhysicsAsset->SkeletalBodySetups[BodyInstance->InstanceBodyIndex];

				bool bSimulated = (BodySetup->PhysicsType == EPhysicsType::PhysType_Simulated);
				ImmediatePhysics::EActorType ActorType = bSimulated ?  ImmediatePhysics::EActorType::DynamicActor : ImmediatePhysics::EActorType::KinematicActor;
				ImmediatePhysics::FActorHandle* NewBodyHandle = PhysicsSimulation->CreateActor(ActorType, BodyInstance, BodyInstance->GetUnrealWorldTransform());
				if (NewBodyHandle)
				{
					if (bSimulated)
					{
						const float InvMass = NewBodyHandle->GetInverseMass();
						TotalMass += InvMass > 0.f ? 1.f / InvMass : 0.f;
					}
					const int32 BodyIndex = Bodies.Add(NewBodyHandle);
					const int32 SkeletonBoneIndex = MeshToSkeletonBoneIndex[InsertBone];
					SkeletonBoneIndexToBodyIndex[SkeletonBoneIndex] = BodyIndex;
					BodyAnimData[BodyIndex].bIsSimulated = bSimulated;
					NamesToHandles.Add(BodySetup->BoneName, NewBodyHandle);
					BodyIndexToActorHandle[BodyInstance->InstanceBodyIndex] = NewBodyHandle;

					if (BodySetup->CollisionReponse == EBodyCollisionResponse::BodyCollision_Disabled)
					{
						IgnoreCollisionActors.Add(NewBodyHandle);
					}

#if WITH_CHAOS
					NewBodyHandle->SetName(BodySetup->BoneName);
#endif
				}
			}
		}

		//Insert joints so that they coincide body order. That is, if we stop simulating all bodies past some index, we can simply ignore joints past a corresponding index without any re-order
		//For this to work we consider the most last inserted bone in each joint
		TArray<int32> InsertionOrderPerBone;
		InsertionOrderPerBone.AddUninitialized(NumBonesLOD0);

		for(int32 Position = 0; Position < NumBonesLOD0; ++Position)
		{
			InsertionOrderPerBone[InsertionOrder[Position]] = Position;
		}

		HighLevelConstraintInstances.Sort([&InsertionOrderPerBone, &SkelMeshRefSkel](const FConstraintInstance& LHS, const FConstraintInstance& RHS)
		{
			if(LHS.IsValidConstraintInstance() && RHS.IsValidConstraintInstance())
			{
				const int32 BoneIdxLHS1 = SkelMeshRefSkel.FindBoneIndex(LHS.ConstraintBone1);
				const int32 BoneIdxLHS2 = SkelMeshRefSkel.FindBoneIndex(LHS.ConstraintBone2);

				const int32 BoneIdxRHS1 = SkelMeshRefSkel.FindBoneIndex(RHS.ConstraintBone1);
				const int32 BoneIdxRHS2 = SkelMeshRefSkel.FindBoneIndex(RHS.ConstraintBone2);

				const int32 MaxPositionLHS = FMath::Max(InsertionOrderPerBone[BoneIdxLHS1], InsertionOrderPerBone[BoneIdxLHS2]);
				const int32 MaxPositionRHS = FMath::Max(InsertionOrderPerBone[BoneIdxRHS1], InsertionOrderPerBone[BoneIdxRHS2]);

				return MaxPositionLHS < MaxPositionRHS;
			}
			
			return false;
		});


		if(NamesToHandles.Num() > 0)
		{
			//constraints
			for(int32 ConstraintIdx = 0; ConstraintIdx < HighLevelConstraintInstances.Num(); ++ConstraintIdx)
			{
				FConstraintInstance* CI = HighLevelConstraintInstances[ConstraintIdx];
				ImmediatePhysics::FActorHandle* Body1Handle = NamesToHandles.FindRef(CI->ConstraintBone1);
				ImmediatePhysics::FActorHandle* Body2Handle = NamesToHandles.FindRef(CI->ConstraintBone2);

				if(Body1Handle && Body2Handle)
				{
					if (Body1Handle->IsSimulated() || Body2Handle->IsSimulated())
					{
						PhysicsSimulation->CreateJoint(CI, Body1Handle, Body2Handle);
						if (bForceDisableCollisionBetweenConstraintBodies)
						{
							int32 BodyIndex1 = UsePhysicsAsset->FindBodyIndex(CI->ConstraintBone1);
							int32 BodyIndex2 = UsePhysicsAsset->FindBodyIndex(CI->ConstraintBone2);
							if (BodyIndex1 != INDEX_NONE && BodyIndex2 != INDEX_NONE)
							{
								UsePhysicsAsset->DisableCollision(BodyIndex1, BodyIndex2);
							}
						}

						int32 BodyIndex;
						if (Bodies.Find(Body1Handle, BodyIndex))
						{
							BodyAnimData[BodyIndex].LinearXMotion = CI->GetLinearXMotion();
							BodyAnimData[BodyIndex].LinearYMotion = CI->GetLinearYMotion();
							BodyAnimData[BodyIndex].LinearZMotion = CI->GetLinearZMotion();
							BodyAnimData[BodyIndex].LinearLimit = CI->GetLinearLimit();

							//set limit to ref pose 
							FTransform Body1Transform = Body1Handle->GetWorldTransform();
							FTransform Body2Transform = Body2Handle->GetWorldTransform();
							BodyAnimData[BodyIndex].RefPoseLength = Body1Transform.GetRelativeTransform(Body2Transform).GetLocation().Size();
						}
					}
				}

				CI->TermConstraint();
				delete CI;
			}

			ResetSimulatedTeleportType = ETeleportType::ResetPhysics;
		}

		// Terminate all of the instances, cannot be done during insert or we may break constraint chains
		for(FBodyInstance* Instance : HighLevelBodyInstances)
		{
			if(Instance->IsValidBodyInstance())
			{
				Instance->TermBody(true);
			}

			delete Instance;
		}

		HighLevelBodyInstances.Empty();
		BodiesSorted.Empty();

		TArray<ImmediatePhysics::FSimulation::FIgnorePair> IgnorePairs;
		const TMap<FRigidBodyIndexPair, bool>& DisableTable = UsePhysicsAsset->CollisionDisableTable;
		for(auto ConstItr = DisableTable.CreateConstIterator(); ConstItr; ++ConstItr)
		{
			int32 IndexA = ConstItr.Key().Indices[0];
			int32 IndexB = ConstItr.Key().Indices[1];
			if ((IndexA < BodyIndexToActorHandle.Num()) && (IndexB < BodyIndexToActorHandle.Num()))
			{
				if ((BodyIndexToActorHandle[IndexA] != nullptr) && (BodyIndexToActorHandle[IndexB] != nullptr))
				{
					ImmediatePhysics::FSimulation::FIgnorePair Pair;
					Pair.A = BodyIndexToActorHandle[IndexA];
					Pair.B = BodyIndexToActorHandle[IndexB];
					IgnorePairs.Add(Pair);
				}
			}
		}

		PhysicsSimulation->SetIgnoreCollisionPairTable(IgnorePairs);
		PhysicsSimulation->SetIgnoreCollisionActors(IgnoreCollisionActors);

#if WITH_CHAOS
		SolverIterations = UsePhysicsAsset->SolverIterations;
		PhysicsSimulation->SetSolverIterations(
			SolverIterations.SolverIterations,
			SolverIterations.JointIterations,
			SolverIterations.CollisionIterations,
			SolverIterations.SolverPushOutIterations,
			SolverIterations.JointPushOutIterations,
			SolverIterations.CollisionPushOutIterations
		);
#endif
	}
}

DECLARE_CYCLE_STAT(TEXT("FAnimNode_RigidBody::UpdateWorldGeometry"), STAT_ImmediateUpdateWorldGeometry, STATGROUP_ImmediatePhysics);

void FAnimNode_RigidBody::UpdateWorldGeometry(const UWorld& World, const USkeletalMeshComponent& SKC)
{
	SCOPE_CYCLE_COUNTER(STAT_ImmediateUpdateWorldGeometry);
	QueryParams = FCollisionQueryParams(SCENE_QUERY_STAT(RagdollNodeFindGeometry), /*bTraceComplex=*/false);
#if WITH_EDITOR
	if(!World.IsGameWorld())
	{
		QueryParams.MobilityType = EQueryMobilityType::Any;	//If we're in some preview world trace against everything because things like the preview floor are not static
		QueryParams.AddIgnoredComponent(&SKC);
	}
	else
#endif
	{
		QueryParams.MobilityType = EQueryMobilityType::Static;	//We only want static actors
	}

	Bounds = SKC.CalcBounds(SKC.GetComponentToWorld()).GetSphere();

	if (!Bounds.IsInside(CachedBounds))
	{
		// Since the cached bounds are no longer valid, update them.
		
		CachedBounds = Bounds;
		CachedBounds.W *= CachedBoundsScale;

		// Cache the PhysScene and World for use in UpdateWorldForces.
		PhysScene = World.GetPhysicsScene();
		UnsafeWorld = &World;
	}
}

DECLARE_CYCLE_STAT(TEXT("FAnimNode_RigidBody::UpdateWorldForces"), STAT_ImmediateUpdateWorldForces, STATGROUP_ImmediatePhysics);

void FAnimNode_RigidBody::UpdateWorldForces(const FTransform& ComponentToWorld, const FTransform& BaseBoneTM)
{
	SCOPE_CYCLE_COUNTER(STAT_ImmediateUpdateWorldForces);

	if(TotalMass > 0.f)
	{
		for (const USkeletalMeshComponent::FPendingRadialForces& PendingRadialForce : PendingRadialForces)
		{
			const FVector RadialForceOrigin = WorldPositionToSpace(SimulationSpace, PendingRadialForce.Origin, ComponentToWorld, BaseBoneTM);
			for(ImmediatePhysics::FActorHandle* Body : Bodies)
			{
				const float InvMass = Body->GetInverseMass();
				if(InvMass > 0.f)
				{
					const float StrengthPerBody = PendingRadialForce.bIgnoreMass ? PendingRadialForce.Strength : PendingRadialForce.Strength / (TotalMass * InvMass);
					ImmediatePhysics::EForceType ForceType;
					if (PendingRadialForce.Type == USkeletalMeshComponent::FPendingRadialForces::AddImpulse)
					{
						ForceType = PendingRadialForce.bIgnoreMass ? ImmediatePhysics::EForceType::AddVelocity : ImmediatePhysics::EForceType::AddImpulse;
					}
					else
					{
						ForceType = PendingRadialForce.bIgnoreMass ? ImmediatePhysics::EForceType::AddAcceleration : ImmediatePhysics::EForceType::AddForce;
					}
					
					Body->AddRadialForce(RadialForceOrigin, StrengthPerBody, PendingRadialForce.Radius, PendingRadialForce.Falloff, ForceType);
				}
			}
		}

		if(!ExternalForce.IsNearlyZero())
		{
			const FVector ExternalForceInSimSpace = WorldVectorToSpaceNoScale(SimulationSpace, ExternalForce, ComponentToWorld, BaseBoneTM);
			for (ImmediatePhysics::FActorHandle* Body : Bodies)
			{
				const float InvMass = Body->GetInverseMass();
				if (InvMass > 0.f)
				{
					Body->AddForce(ExternalForceInSimSpace);
				}
			}
		}
	}
}

bool FAnimNode_RigidBody::NeedsDynamicReset() const
{
	return true;
}

void FAnimNode_RigidBody::ResetDynamics(ETeleportType InTeleportType)
{
	// This will be picked up next evaluate and reset our simulation.
	// Teleport type can only go higher - i.e. if we have requested a reset, then a teleport will still reset fully
	ResetSimulatedTeleportType = ((InTeleportType > ResetSimulatedTeleportType) ? InTeleportType : ResetSimulatedTeleportType);
}

DECLARE_CYCLE_STAT(TEXT("RigidBody_PreUpdate"), STAT_RigidBody_PreUpdate, STATGROUP_Anim);

void FAnimNode_RigidBody::PreUpdate(const UAnimInstance* InAnimInstance)
{
	// Don't update geometry if RBN is disabled
	if(!bEnabled)
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_RigidBody_PreUpdate);

	USkeletalMeshComponent* SKC = InAnimInstance->GetSkelMeshComponent();
	APawn* PawnOwner = InAnimInstance->TryGetPawnOwner();
	UPawnMovementComponent* MovementComp = PawnOwner ? PawnOwner->GetMovementComponent() : nullptr;

#if WITH_EDITOR
	if (bEnableWorldGeometry && SimulationSpace != ESimulationSpace::WorldSpace)
	{
		FMessageLog("PIE").Warning(FText::Format(LOCTEXT("WorldCollisionComponentSpace", "Trying to use world collision without world space simulation for ''{0}''. This is not supported, please change SimulationSpace to WorldSpace"),
			FText::FromString(GetPathNameSafe(SKC))));
	}
#endif

	UWorld* World = InAnimInstance->GetWorld();
	if (World)
	{
		WorldSpaceGravity = bOverrideWorldGravity ? OverrideWorldGravity : (MovementComp ? FVector(0.f, 0.f, MovementComp->GetGravityZ()) : FVector(0.f, 0.f, World->GetGravityZ()));
		if(SKC)
		{
			if (PhysicsSimulation && bEnableWorldGeometry && SimulationSpace == ESimulationSpace::WorldSpace)
			{
				UpdateWorldGeometry(*World, *SKC);
			}

			PendingRadialForces = SKC->GetPendingRadialForces();

			PreviousTransform = CurrentTransform;
			CurrentTransform = SKC->GetComponentToWorld();
		}	
	}
}

int32 FAnimNode_RigidBody::GetLODThreshold() const
{
	if(CVarRigidBodyLODThreshold.GetValueOnAnyThread() != -1)
	{
		if(LODThreshold != -1)
		{
			return FMath::Min(LODThreshold, CVarRigidBodyLODThreshold.GetValueOnAnyThread());
		}
		else
		{
			return CVarRigidBodyLODThreshold.GetValueOnAnyThread();
		}
	}
	else
	{
		return LODThreshold;
	}
}

DECLARE_CYCLE_STAT(TEXT("RigidBody_Update"), STAT_RigidBody_Update, STATGROUP_Anim);

void FAnimNode_RigidBody::UpdateInternal(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(UpdateInternal)
	// Avoid this work if RBN is disabled, as the results would be discarded
	if(!bEnabled)
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_RigidBody_Update);

	// Accumulate deltatime elapsed during update. To be used during evaluation.
	AccumulatedDeltaTime += Context.AnimInstanceProxy->GetDeltaSeconds();

	if (UnsafeWorld != nullptr)
	{		
		// Node is valid to evaluate. Simulation is starting.
		bSimulationStarted = true;

		TArray<FOverlapResult> Overlaps;
		UnsafeWorld->OverlapMultiByChannel(Overlaps, Bounds.Center, FQuat::Identity, OverlapChannel, FCollisionShape::MakeSphere(Bounds.W), QueryParams, FCollisionResponseParams(ECR_Overlap));

		// @todo(ccaulfield): is there an engine-independent way to do this?
#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
		SCOPED_SCENE_READ_LOCK(PhysScene ? PhysScene->GetPxScene() : nullptr); //TODO: expose this part to the anim node
#endif

		for (const FOverlapResult& Overlap : Overlaps)
		{
			if (UPrimitiveComponent* OverlapComp = Overlap.GetComponent())
			{
				if (ComponentsInSim.Contains(OverlapComp) == false)
				{
					ComponentsInSim.Add(OverlapComp);
					PhysicsSimulation->CreateActor(ImmediatePhysics::EActorType::StaticActor, &OverlapComp->BodyInstance, OverlapComp->BodyInstance.GetUnrealWorldTransform());
				}
			}
		}
		UnsafeWorld = nullptr;
		PhysScene = nullptr;
	}
}

void FAnimNode_RigidBody::InitializeBoneReferences(const FBoneContainer& RequiredBones) 
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(InitializeBoneReferences)
	/** We only need to update simulated bones and children of simulated bones*/
	const int32 NumBodies = Bodies.Num();
	const TArray<FBoneIndexType>& RequiredBoneIndices = RequiredBones.GetBoneIndicesArray();
	const int32 NumRequiredBoneIndices = RequiredBoneIndices.Num();
	const FReferenceSkeleton& RefSkeleton = RequiredBones.GetReferenceSkeleton();

	OutputBoneData.Empty(NumBodies);

	int32 NumSimulatedBodies = 0;

	// if no name is entered, use root
	if (BaseBoneRef.BoneName == NAME_None)
	{
		BaseBoneRef.BoneName = RefSkeleton.GetBoneName(0);
	}

	if (BaseBoneRef.BoneName != NAME_None)
	{
		BaseBoneRef.Initialize(RequiredBones);
	}

	bool bHasInvalidBoneReference = false;
	for (int32 Index = 0; Index < NumRequiredBoneIndices; ++Index)
	{
		const FCompactPoseBoneIndex CompactPoseBoneIndex(Index);
		const FBoneIndexType SkeletonBoneIndex = RequiredBones.GetSkeletonIndex(CompactPoseBoneIndex);
		const FBoneIndexType IndexToBodyNum = SkeletonBoneIndexToBodyIndex.Num();

		// If we have a missing bone in our skeleton, we don't want to have an out of bounds access.
		if (SkeletonBoneIndex >= IndexToBodyNum)
		{
			bHasInvalidBoneReference = true;
			break;
		}

		const int32 BodyIndex = SkeletonBoneIndexToBodyIndex[SkeletonBoneIndex];

		if (BodyIndex != INDEX_NONE)
		{
			//If we have a body we need to save it for later
			FOutputBoneData* OutputData = new (OutputBoneData) FOutputBoneData();
			OutputData->BodyIndex = BodyIndex;
			OutputData->CompactPoseBoneIndex = CompactPoseBoneIndex;

			if (BodyAnimData[BodyIndex].bIsSimulated)
			{
				++NumSimulatedBodies;
			}

			OutputData->BoneIndicesToParentBody.Add(CompactPoseBoneIndex);

			// Walk up parent chain until we find parent body.
			OutputData->ParentBodyIndex = INDEX_NONE;
			FCompactPoseBoneIndex CompactParentIndex = RequiredBones.GetParentBoneIndex(CompactPoseBoneIndex);
			while (CompactParentIndex != INDEX_NONE)
			{
				const FBoneIndexType SkeletonParentBoneIndex = RequiredBones.GetSkeletonIndex(CompactParentIndex);

				// Must check our parent as well for a missing bone.
				if (SkeletonParentBoneIndex >= IndexToBodyNum)
				{
					bHasInvalidBoneReference = true;
					break;
				}

				OutputData->ParentBodyIndex = SkeletonBoneIndexToBodyIndex[SkeletonParentBoneIndex];
				if (OutputData->ParentBodyIndex != INDEX_NONE)
				{
					break;
				}

				OutputData->BoneIndicesToParentBody.Add(CompactParentIndex);
				CompactParentIndex = RequiredBones.GetParentBoneIndex(CompactParentIndex);
			}

			if (bHasInvalidBoneReference)
			{
				break;
			}
		}
	}

	if (bHasInvalidBoneReference)
	{
		// If a bone was missing, let us know which asset it happened on, and clear our bone container to make the bad asset visible.
		ensureMsgf(false, TEXT("FAnimNode_RigidBody::InitializeBoneReferences: The Skeleton %s, is missing bones that SkeletalMesh %s needs. Skeleton might need to be resaved."),
			*GetNameSafe(RequiredBones.GetSkeletonAsset()), *GetNameSafe(RequiredBones.GetSkeletalMeshAsset()));
		OutputBoneData.Empty();
	}
	else
	{
		// New bodies potentially introduced with new LOD
		// We'll have to initialize their transform.
		bCheckForBodyTransformInit = true;

		if (PhysicsSimulation)
		{
			PhysicsSimulation->SetNumActiveBodies(NumSimulatedBodies);
		}

		// We're switching to a new LOD, this invalidates our captured poses.
		CapturedFrozenPose.Empty();
		CapturedFrozenCurves.Empty();
	}
}

void FAnimNode_RigidBody::AddImpulseAtLocation(FVector Impulse, FVector Location, FName BoneName)
{
#if WITH_CHAOS
	// Find the body. This is currently only used in the editor and will need optimizing if used in game
	for (int32 BodyIndex = 0; BodyIndex < Bodies.Num(); ++BodyIndex)
	{
		ImmediatePhysics::FActorHandle* Body = Bodies[BodyIndex];
		if (Body->GetName() == BoneName)
		{
			Body->AddImpulseAtLocation(Impulse, Location);
		}
	}
#endif
}


void FAnimNode_RigidBody::OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance)
{
	CSV_SCOPED_TIMING_STAT(Animation, RigidBodyInit);
	InitPhysics(InAnimInstance);
}

#if WITH_EDITORONLY_DATA
void FAnimNode_RigidBody::PostSerialize(const FArchive& Ar)
{
	if(bComponentSpaceSimulation_DEPRECATED == false)
	{
		//If this is not the default value it means we have old content where we were simulating in world space
		SimulationSpace = ESimulationSpace::WorldSpace;
		bComponentSpaceSimulation_DEPRECATED = true;
	}
}
#endif

bool FAnimNode_RigidBody::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	return BaseBoneRef.IsValidToEvaluate(RequiredBones);
}

#undef LOCTEXT_NAMESPACE
