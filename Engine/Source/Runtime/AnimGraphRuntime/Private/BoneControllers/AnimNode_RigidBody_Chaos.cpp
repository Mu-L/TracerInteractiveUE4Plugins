// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BoneControllers/AnimNode_RigidBody_Chaos.h"
#include "AnimationRuntime.h"
#include "Animation/AnimInstanceProxy.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "GameFramework/PawnMovementComponent.h"

#include "Chaos/DebugDrawQueue.h"
#include "Chaos/ErrorReporter.h"
#include "ChaosSolversModule.h"
#include "ChaosStats.h"
#include "PBDRigidsSolver.h"
#include "SolverObjects/SkeletalMeshPhysicsObject.h"

#include "Logging/MessageLog.h"

//#pragma optimize("", off)

#define CHAOS_RBAN_TODO 0

/////////////////////////////////////////////////////
// FAnimNode_Ragdoll_Chaos

#define LOCTEXT_NAMESPACE "ImmediatePhysics"

TAutoConsoleVariable<int32> CVarEnableChaosRigidBodyNode(TEXT("p.ChaosRigidBodyNode"), 1, TEXT("Enables/disables chaos rigid body node updates and evaluations"), ECVF_Scalability);

FAnimNode_RigidBody_Chaos::FAnimNode_RigidBody_Chaos()
	: PhysicalMaterial(nullptr)

	, bSimulating(true)
	, NumIterations(1)
	, bNotifyCollisions(false)
	, ObjectType(EObjectStateTypeEnum::Chaos_Object_Kinematic)

	, Density(2.4)  // dense brick
	, MinMass(0.001)
	, MaxMass(1.e6)

	, CollisionType(ECollisionTypeEnum::Chaos_Volumetric)
	, ImplicitShapeParticlesPerUnitArea(0.1)
	, ImplicitShapeMinNumParticles(0)
	, ImplicitShapeMaxNumParticles(50)
	, MinLevelSetResolution(5)
	, MaxLevelSetResolution(10)
	, CollisionGroup(0)

	, InitialVelocityType(EInitialVelocityTypeEnum::Chaos_Initial_Velocity_User_Defined)
	, InitialLinearVelocity(0.f)
	, InitialAngularVelocity(0.f)

{
	ResetSimulatedTeleportType = ETeleportType::None;
	OverridePhysicsAsset = nullptr;
	bOverrideWorldGravity = false;
	CachedBoundsScale = 1.2f;
	SimulationSpace = ESimulationSpace::ComponentSpace;
	ExternalForce = FVector::ZeroVector;
	OverrideWorldGravity = FVector::ZeroVector;
	TotalMass = 0.f;
	CachedBounds.W = 0;
	UnsafeWorld = nullptr;
	bSimulationStarted = false;
	bCheckForBodyTransformInit = false;
	OverlapChannel = ECC_WorldStatic;
	bEnableWorldGeometry = false;
	bTransferBoneVelocities = false;
	bFreezeIncomingPoseOnStart = false;
	bClampLinearTranslationLimitToRefPose = false;

	PreviousTransform = CurrentTransform = FTransform::Identity;
	PreviousComponentLinearVelocity = FVector::ZeroVector;	

	ComponentLinearAccScale = FVector::ZeroVector;
	ComponentLinearVelScale = FVector::ZeroVector;
	ComponentAppliedLinearAccClamp = FVector(10000,10000,10000);
	bForceDisableCollisionBetweenConstraintBodies = false;
}

FAnimNode_RigidBody_Chaos::~FAnimNode_RigidBody_Chaos()
{
}

void FAnimNode_RigidBody_Chaos::GatherDebugData(FNodeDebugData& DebugData)
{
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

FVector WorldVectorToSpaceNoScaleChaos(ESimulationSpace Space, const FVector& WorldDir, const FTransform& ComponentToWorld, const FTransform& BaseBoneTM)
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

FVector WorldPositionToSpaceChaos(ESimulationSpace Space, const FVector& WorldPoint, const FTransform& ComponentToWorld, const FTransform& BaseBoneTM)
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

FORCEINLINE_DEBUGGABLE FTransform ConvertCSTransformToSimSpaceChaos(ESimulationSpace SimulationSpace, const FTransform& InCSTransform, const FTransform& ComponentToWorld, const FTransform& BaseBoneTM)
{
	switch (SimulationSpace)
	{
		case ESimulationSpace::ComponentSpace: return InCSTransform;
		case ESimulationSpace::WorldSpace:  return InCSTransform * ComponentToWorld; 
		case ESimulationSpace::BaseBoneSpace: return InCSTransform.GetRelativeTransform(BaseBoneTM); break;
		default: ensureMsgf(false, TEXT("Unsupported Simulation Space")); return InCSTransform;
	}
}

void FAnimNode_RigidBody_Chaos::UpdateComponentPose_AnyThread(const FAnimationUpdateContext& Context)
{
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
			FAnimationUpdateContext FrozenContext = Context;
			FrozenContext.FractionalWeightAndTime(1.f, 0.f);

			Super::UpdateComponentPose_AnyThread(FrozenContext);
		}
	}
	else
	{
		Super::UpdateComponentPose_AnyThread(Context);
	}
}

void FAnimNode_RigidBody_Chaos::EvaluateComponentPose_AnyThread(FComponentSpacePoseContext& Output)
{
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

void FAnimNode_RigidBody_Chaos::InitializeNewBodyTransformsDuringSimulation(FComponentSpacePoseContext& Output, const FTransform& ComponentTransform, const FTransform& BaseBoneTM)
{
#if CHAOS_RBAN_TODO
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
			}
			// If we don't have a parent body, then we can just grab the incoming pose in component space.
			else
			{
				const FTransform& ComponentSpaceTM = Output.Pose.GetComponentSpaceTransform(OutputData.CompactPoseBoneIndex);
				const FTransform BodyTM = ConvertCSTransformToSimSpaceChaos(SimulationSpace, ComponentSpaceTM, ComponentTransform, BaseBoneTM);

				Bodies[BodyIndex]->SetWorldTransform(BodyTM);
			}
		}
	}
#endif
}

DECLARE_CYCLE_STAT(TEXT("RigidBody_Chaos_Eval"), STAT_RigidBody_Chaos_Eval, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("FAnimNode_RigidBody_Chaos::EvaluateSkeletalControl_AnyThread"), STAT_ImmediateEvaluateSkeletalControl_Chaos, STATGROUP_Chaos);

void FAnimNode_RigidBody_Chaos::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
#if INCLUDE_CHAOS
	SCOPE_CYCLE_COUNTER(STAT_RigidBody_Chaos_Eval);
	SCOPE_CYCLE_COUNTER(STAT_ImmediateEvaluateSkeletalControl_Chaos);
	//SCOPED_NAMED_EVENT_TEXT("FAnimNode_Ragdoll::EvaluateSkeletalControl_AnyThread", FColor::Magenta);

	// Update our eval counter, and decide whether we need to reset simulated bodies, if our anim instance hasn't updated in a while.
	if(EvalCounter.HasEverBeenUpdated() && !EvalCounter.WasSynchronizedLastFrame(Output.AnimInstanceProxy->GetEvaluationCounter()))
	{
		ResetSimulatedTeleportType = ETeleportType::ResetPhysics;
	}
	EvalCounter.SynchronizeWith(Output.AnimInstanceProxy->GetEvaluationCounter());

	const float DeltaSeconds = AccumulatedDeltaTime;
	AccumulatedDeltaTime = 0.f;

	if (Solver && Solver->Enabled())
	{
		PhysicsObject->CaptureInputs(DeltaSeconds,
			[this, &Output](const float Dt, FSkeletalMeshPhysicsObjectParams & OutPhysicsParams) -> bool
			{
				return UpdatePhysicsInputs(Output, Dt, OutPhysicsParams.BoneHierarchy);
			});

		Solver->AdvanceSolverBy(DeltaSeconds);
		
		// @todo(ccaulfield): this feels clumsy
		PhysicsObject->CacheResults();
		PhysicsObject->FlipCache();
		PhysicsObject->SyncToCache();

		if (const FSkeletalMeshPhysicsObjectOutputs* PhysicsOutputs = PhysicsObject->GetOutputs())
		{
			UpdateAnimNodeOutputs(PhysicsObject->GetBoneHierarchy(), *PhysicsOutputs, Output, OutBoneTransforms);
		}
	}

#if CHAOS_RBAN_TODO
	if (CVarEnableRigidBodyNode.GetValueOnAnyThread() != 0 && PhysicsSimulation)	
	{
		const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
		const FTransform CompWorldSpaceTM = Output.AnimInstanceProxy->GetComponentTransform();
		if(!EvalCounter.HasEverBeenUpdated())
		{
			PreviousCompWorldSpaceTM = CompWorldSpaceTM;
		}

		// @todo(ccaulfield) fix
		const FTransform BaseBoneTM = FTransform::Identity;

		const FTransform BaseBoneTM = Output.Pose.GetComponentSpaceTransform(BaseBoneRef.GetCompactPoseIndex(BoneContainer));

		// Initialize potential new bodies because of LOD change.
		if (ResetSimulatedTeleportType == ETeleportType::None && bCheckForBodyTransformInit)
		{
			bCheckForBodyTransformInit = false;
			InitializeNewBodyTransformsDuringSimulation(Output, CompWorldSpaceTM, BaseBoneTM);
		}

		// If time advances, update simulation
		// Reset if necessary
		if (ResetSimulatedTeleportType != ETeleportType::None)
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

							const FTransform PrevSSTM = ConvertCSTransformToSimSpaceChaos(SimulationSpace, PrevCSTM, CompWorldSpaceTM, BaseBoneTM);
							const FTransform NextSSTM = ConvertCSTransformToSimSpaceChaos(SimulationSpace, NextCSTM, CompWorldSpaceTM, BaseBoneTM);

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

						BodyTM = ConvertCSTransformToSimSpaceChaos(SimulationSpace, ComponentSpaceTM, CompWorldSpaceTM, BaseBoneTM);
						Bodies[BodyIndex]->SetWorldTransform(BodyTM);
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
						const FTransform BodyTM = ConvertCSTransformToSimSpaceChaos(SimulationSpace, ComponentSpaceTM, CompWorldSpaceTM, BaseBoneTM);
						Bodies[BodyIndex]->SetWorldTransform(BodyTM);
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
		else if(DeltaSeconds > 0.0f)
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
				FVector ApplyLinearAcc = WorldVectorToSpaceNoScaleChaos(SimulationSpace, -ComponentLinearVelocity, CompWorldSpaceTM, BaseBoneTM) * ComponentLinearVelScale;

				// Calc linear acceleration
				const FVector ComponentLinearAcceleration = (ComponentLinearVelocity - PreviousComponentLinearVelocity) / DeltaSeconds;
				PreviousComponentLinearVelocity = ComponentLinearVelocity;
				// Apply opposite acceleration to bodies
				ApplyLinearAcc += WorldVectorToSpaceNoScaleChaos(SimulationSpace, -ComponentLinearAcceleration, CompWorldSpaceTM, BaseBoneTM) * ComponentLinearAccScale;

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

			for (const FOutputBoneData& OutputData : OutputBoneData)
			{
				const int32 BodyIndex = OutputData.BodyIndex;
				if (!BodyAnimData[BodyIndex].bIsSimulated)
				{
					const FTransform& ComponentSpaceTM = Output.Pose.GetComponentSpaceTransform(OutputData.CompactPoseBoneIndex);
					const FTransform BodyTM = ConvertCSTransformToSimSpaceChaos(SimulationSpace, ComponentSpaceTM, CompWorldSpaceTM, BaseBoneTM);

					Bodies[BodyIndex]->SetKinematicTarget(BodyTM);
				}
			}
			
			UpdateWorldForces(CompWorldSpaceTM, BaseBoneTM);
			const FVector SimSpaceGravity = WorldVectorToSpaceNoScaleChaos(SimulationSpace, WorldSpaceGravity, CompWorldSpaceTM, BaseBoneTM);

			// Run simulation at a minimum of 30 FPS to prevent system from exploding.
			// DeltaTime can be higher due to URO, so take multiple iterations in that case.
			const float MaxDeltaSeconds = 1.f / 30.f;
			const int32 NumIterations = FMath::Clamp(FMath::CeilToInt(DeltaSeconds / MaxDeltaSeconds), 1, 4);
			const float StepDeltaTime = DeltaSeconds / float(NumIterations);

			for (int32 Step = 1; Step <= NumIterations; Step++)
			{
				// We call the _AssumesLocked version here without a lock as the simulation is local to this node and we know
				// we're not going to alter anything while this is running.
				PhysicsSimulation->Simulate_AssumesLocked(StepDeltaTime, SimSpaceGravity);
			}
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
#endif // CHAOS_RBAN_TODO
#endif // INCLUDE_CHAOS
}

void ComputeBodyInsertionOrderChaos(TArray<FBoneIndexType>& InsertionOrder, const USkeletalMeshComponent& SKC)
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

#if INCLUDE_CHAOS

void FAnimNode_RigidBody_Chaos::PhysicsObjectInitCallback(const USkeletalMeshComponent* InSkelMeshComponent, const UAnimInstance* InAnimInstance, FSkeletalMeshPhysicsObjectParams& OutPhysicsParams)
{
	OutPhysicsParams.bSimulating = bSimulating;

	// @todo(ccaulfield): do we need this?
	//GetPathName(this, OutParams.Name);
	if (InitialVelocityType == EInitialVelocityTypeEnum::Chaos_Initial_Velocity_User_Defined)
	{
		OutPhysicsParams.InitialLinearVelocity = InitialLinearVelocity;
		OutPhysicsParams.InitialAngularVelocity = InitialAngularVelocity;
	}

	//OutParams.PhysicalMaterial = MakeSerializable(PhysicsMaterial);
	OutPhysicsParams.ObjectType = ObjectType;

	OutPhysicsParams.Density = Density;
	OutPhysicsParams.MinMass = MinMass;
	OutPhysicsParams.MaxMass = MaxMass;

	OutPhysicsParams.CollisionType = CollisionType;
	OutPhysicsParams.ParticlesPerUnitArea = ImplicitShapeParticlesPerUnitArea;
	OutPhysicsParams.MinNumParticles = ImplicitShapeMinNumParticles;
	OutPhysicsParams.MaxNumParticles = ImplicitShapeMaxNumParticles;
	OutPhysicsParams.MinRes = MinLevelSetResolution;
	OutPhysicsParams.MaxRes = MaxLevelSetResolution;
	OutPhysicsParams.CollisionGroup = CollisionGroup;

	const USkeletalMesh* SkeletalMesh = InSkelMeshComponent->SkeletalMesh;
	const AActor* OwningActor = InSkelMeshComponent->GetOwner();
	if (SkeletalMesh && OwningActor)
	{
		const UPhysicsAsset* PhysicsAsset = OverridePhysicsAsset ? OverridePhysicsAsset : SkeletalMesh->PhysicsAsset;
		FPhysicsAssetSimulationUtil::BuildParams(OwningActor, OwningActor, InSkelMeshComponent, PhysicsAsset, OutPhysicsParams);
	}
}

bool FAnimNode_RigidBody_Chaos::UpdatePhysicsInputs(FComponentSpacePoseContext& InPoseContext, const float Dt, FBoneHierarchy& InOutBoneHierarchy)
{
	InOutBoneHierarchy.PrepareForUpdate();
	for (int32 BoneIndex : InOutBoneHierarchy.GetBoneIndices())
	{
		InOutBoneHierarchy.SetAnimLocalSpaceTransform(BoneIndex, InPoseContext.Pose.GetLocalSpaceTransform(FCompactPoseBoneIndex(BoneIndex)));
	}
	InOutBoneHierarchy.SetActorWorldSpaceTransform(InPoseContext.AnimInstanceProxy->GetComponentTransform());
	InOutBoneHierarchy.PrepareAnimWorldSpaceTransforms();

	return true;
}

void FAnimNode_RigidBody_Chaos::UpdateAnimNodeOutputs(const FBoneHierarchy& InBoneHierarchy, const FSkeletalMeshPhysicsObjectOutputs& InPhysicsOutputs, FComponentSpacePoseContext& PoseContext, TArray<FBoneTransform>& OutComponentSpaceBoneTransforms)
{
	const FTransform& ComponentWorldSpaceTransform = PoseContext.AnimInstanceProxy->GetComponentTransform();

	for (const int32 BoneIndex : InBoneHierarchy.GetBoneIndices())
	{
		const FAnalyticImplicitGroup* ShapeGroup = InBoneHierarchy.GetAnalyticShapeGroup(BoneIndex);

		// The first time we have no body, we are done
		// @todo(ccaulfield): is this right?
		if ((ShapeGroup == nullptr) || (ShapeGroup->GetRigidBodyId() == INDEX_NONE))
		{
			break;
		}

		// Shouldn't have to process Kinematic bodies
		// @todo(ccaulfield): should store actual state of particle
		if (ShapeGroup->GetRigidBodyState() == EObjectStateTypeEnum::Chaos_Object_Kinematic)
		{
			continue;
		}

		// @todo(ccaulfield): This should be pulling from an updated Hierarchy (so we support non-simulated child bones). See FSkeletalMeshPhysicsObject::SyncToCache
		const int32 TransformIndex = InBoneHierarchy.GetTransformIndex(BoneIndex);
		if (TransformIndex != INDEX_NONE)
		{
			FTransform ComponentSpaceBoneTransform = InPhysicsOutputs.Transforms[TransformIndex].GetRelativeTransform(ComponentWorldSpaceTransform);
			OutComponentSpaceBoneTransforms.Emplace(FBoneTransform(FCompactPoseBoneIndex(BoneIndex), ComponentSpaceBoneTransform));
		}
	}
}


#endif // INCLUDE_CHAOS

void FAnimNode_RigidBody_Chaos::InitPhysics(const UAnimInstance* InAnimInstance)
{
	const USkeletalMeshComponent* SkeletalMeshComp = InAnimInstance->GetSkelMeshComponent();
	const USkeletalMesh* SkeletalMeshAsset = SkeletalMeshComp->SkeletalMesh;

	const FReferenceSkeleton& SkelMeshRefSkel = SkeletalMeshAsset->RefSkeleton;
	UPhysicsAsset* UsePhysicsAsset = OverridePhysicsAsset ? OverridePhysicsAsset : InAnimInstance->GetSkelMeshComponent()->GetPhysicsAsset();
		
	USkeleton* SkeletonAsset = InAnimInstance->CurrentSkeleton;
	ensure(SkeletonAsset == SkeletalMeshAsset->Skeleton);

	const int32 SkelMeshLinkupIndex = SkeletonAsset->GetMeshLinkupIndex(SkeletalMeshAsset);
	ensure(SkelMeshLinkupIndex != INDEX_NONE);
	const FSkeletonToMeshLinkup& SkeletonToMeshLinkupTable = SkeletonAsset->LinkupCache[SkelMeshLinkupIndex];
	const TArray<int32>& MeshToSkeletonBoneIndex = SkeletonToMeshLinkupTable.MeshToSkeletonTable;
	
	// @todo(ccaulfield) fix
	//const int32 NumSkeletonBones = SkeletonAsset->GetReferenceSkeleton().GetNum();
	//SkeletonBoneIndexToBodyIndex.Reset(NumSkeletonBones);
	//SkeletonBoneIndexToBodyIndex.Init(INDEX_NONE, NumSkeletonBones);

	PreviousTransform = InAnimInstance->GetSkelMeshComponent()->GetComponentToWorld();

	if(UsePhysicsAsset)
	{
#if INCLUDE_CHAOS
		Solver = Chaos::FPBDRigidsSolver::FAccessor::CreateSolver();
		Solver->SetIterations(NumIterations);

		PhysicsObject = new FSkeletalMeshPhysicsObject(
			InAnimInstance->GetOwningActor(),
			[this, InAnimInstance, SkeletalMeshComp](FSkeletalMeshPhysicsObjectParams & OutPhysicsParams)
			{
				PhysicsObjectInitCallback(SkeletalMeshComp, InAnimInstance, OutPhysicsParams);
			});

		//PhysicsMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<float>>();

		// @todo(ccaulfield): should only be one call for all this...?
		// @todo(ccaulfield): remove the floor
		Solver->RegisterObject(PhysicsObject);
		PhysicsObject->Initialize();
		Solver->SetHasFloor(true);
		Solver->SetIsFloorAnalytic(true);
		Solver->SetEnabled(true);
#endif

#if CHAOS_RBAN_TODO
		delete PhysicsSimulation;
		PhysicsSimulation = new ImmediatePhysics::FSimulation();
		const int32 NumBodies = UsePhysicsAsset->SkeletalBodySetups.Num();
		Bodies.Empty(NumBodies);
		ComponentsInSim.Reset();
		BodyAnimData.Reset(NumBodies);
		BodyAnimData.AddDefaulted(NumBodies);
		TotalMass = 0.f;
		
		TArray<FBodyInstance*> HighLevelBodyInstances;
		TArray<FConstraintInstance*> HighLevelConstraintInstances;
		SkeletalMeshComp->InstantiatePhysicsAssetRefPose(*UsePhysicsAsset, SimulationSpace == ESimulationSpace::WorldSpace ? SkeletalMeshComp->GetComponentToWorld().GetScale3D() : FVector(1.f), HighLevelBodyInstances, HighLevelConstraintInstances);

		TMap<FName, ImmediatePhysics::FActorHandle*> NamesToHandles;
		TArray<ImmediatePhysics::FActorHandle*> IgnoreCollisionActors;

		TArray<FBoneIndexType> InsertionOrder;
		ComputeBodyInsertionOrderChaos(InsertionOrder, *SkeletalMeshComp);

		const int32 NumBonesLOD0 = InsertionOrder.Num();

		TArray<ImmediatePhysics::FActorHandle*> BodyIndexToActorHandle;
		BodyIndexToActorHandle.AddZeroed(NumBonesLOD0);

		TArray<FBodyInstance*> BodiesSorted;
		BodiesSorted.AddZeroed(NumBonesLOD0);

		for (FBodyInstance* BI : HighLevelBodyInstances)
		{
			if(BI->IsValidBodyInstance())
			{
				BodiesSorted[BI->InstanceBoneIndex] = BI;
			}
		}

		auto InsertBodiesHelper = [&](bool bSimulatedBodies)
		{
			for (FBoneIndexType InsertBone : InsertionOrder)
			{
				if (FBodyInstance* BodyInstance = BodiesSorted[InsertBone])
				{
					UBodySetup* BodySetup = UsePhysicsAsset->SkeletalBodySetups[BodyInstance->InstanceBodyIndex];
					const bool bKinematic = BodySetup->PhysicsType != EPhysicsType::PhysType_Simulated;
					const FTransform& LastTransform = SkeletalMeshComp->GetBoneTransform(InsertBone);	//This is out of date, but will still give our bodies an initial setup that matches the constraints (TODO: use refpose)

					ImmediatePhysics::FActorHandle* NewBodyHandle = nullptr;
					if (bSimulatedBodies && !bKinematic)
					{
#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
						PxRigidActor* PRigidActor = FPhysicsInterface::GetPxRigidActor_AssumesLocked(BodyInstance->GetPhysicsActorHandle());
						if(PxRigidDynamic* PDynamic = PRigidActor->is<PxRigidDynamic>())
						{
							NewBodyHandle = PhysicsSimulation->CreateDynamicActor(PDynamic, LastTransform);
						checkSlow(NewBodyHandle);
						const float InvMass = NewBodyHandle->GetInverseMass();
						TotalMass += InvMass > 0.f ? 1.f / InvMass : 0.f;
						}
#endif
					}
					else if (bSimulatedBodies && bKinematic)
					{
#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
						PxRigidActor* PRigidActor = FPhysicsInterface::GetPxRigidActor_AssumesLocked(BodyInstance->GetPhysicsActorHandle());
						if(PxRigidBody* PRigidBody = PRigidActor->is<PxRigidBody>())
						{
							NewBodyHandle = PhysicsSimulation->CreateKinematicActor(PRigidBody, LastTransform);
						}
#endif
					}

					if (NewBodyHandle)
					{
						const int32 BodyIndex = Bodies.Add(NewBodyHandle);

						const int32 SkeletonBoneIndex = MeshToSkeletonBoneIndex[InsertBone];
						SkeletonBoneIndexToBodyIndex[SkeletonBoneIndex] = BodyIndex;
						BodyAnimData[BodyIndex].bIsSimulated = !bKinematic;
						NamesToHandles.Add(BodySetup->BoneName, NewBodyHandle);
						BodyIndexToActorHandle[BodyInstance->InstanceBodyIndex] = NewBodyHandle;

						if (BodySetup->CollisionReponse == EBodyCollisionResponse::BodyCollision_Disabled)
						{
							IgnoreCollisionActors.Add(NewBodyHandle);
						}
					}
				}
			}
		};

		//Insert simulated bodies first to avoid any re-ordering
		InsertBodiesHelper(/*bSimulated=*/true);
		InsertBodiesHelper(/*bSimulated=*/false);

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

#if WITH_PHYSX
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
						FPhysicsConstraintHandle& ConstraintRef = CI->ConstraintHandle;

#if WITH_CHAOS || WITH_IMMEDIATE_PHYSX || PHYSICS_INTERFACE_LLIMMEDIATE
						ensure(false);
#else
						PhysicsSimulation->CreateJoint(ConstraintRef.ConstraintData, Body1Handle, Body2Handle);
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
#endif
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
			ImmediatePhysics::FSimulation::FIgnorePair Pair;
			Pair.A = BodyIndexToActorHandle[ConstItr.Key().Indices[0]];
			Pair.B = BodyIndexToActorHandle[ConstItr.Key().Indices[1]];
			IgnorePairs.Add(Pair);
		}

		PhysicsSimulation->SetIgnoreCollisionPairTable(IgnorePairs);
		PhysicsSimulation->SetIgnoreCollisionActors(IgnoreCollisionActors);
#endif
#endif // TODO
	}
}

DECLARE_CYCLE_STAT(TEXT("FAnimNode_RigidBody_Chaos::UpdateWorldGeometry"), STAT_ImmediateUpdateWorldGeometry_Chaos, STATGROUP_Chaos);

void FAnimNode_RigidBody_Chaos::UpdateWorldGeometry(const UWorld& World, const USkeletalMeshComponent& SKC)
{
#if CHAOS_RBAN_TODO
	SCOPE_CYCLE_COUNTER(STAT_ImmediateUpdateWorldGeometry_Chaos);
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
#endif
}

DECLARE_CYCLE_STAT(TEXT("FAnimNode_RigidBody_Chaos::UpdateWorldForces"), STAT_ImmediateUpdateWorldForces_Chaos, STATGROUP_Chaos);

void FAnimNode_RigidBody_Chaos::UpdateWorldForces(const FTransform& ComponentToWorld, const FTransform& BaseBoneTM)
{
#if CHAOS_RBAN_TODO
	SCOPE_CYCLE_COUNTER(STAT_ImmediateUpdateWorldForces_Chaos);

	if(TotalMass > 0.f)
	{
		for (const USkeletalMeshComponent::FPendingRadialForces& PendingRadialForce : PendingRadialForces)
		{
			const FVector RadialForceOrigin = WorldPositionToSpaceChaos(SimulationSpace, PendingRadialForce.Origin, ComponentToWorld, BaseBoneTM);
			for(ImmediatePhysics::FActorHandle* Body : Bodies)
			{
				const float InvMass = Body->GetInverseMass();
				if(InvMass > 0.f)
				{
					const float StrengthPerBody = PendingRadialForce.bIgnoreMass ? PendingRadialForce.Strength : PendingRadialForce.Strength / (TotalMass * InvMass);
					ImmediatePhysics::FSimulation::EForceType ForceType;
					if (PendingRadialForce.Type == USkeletalMeshComponent::FPendingRadialForces::AddImpulse)
					{
						ForceType = PendingRadialForce.bIgnoreMass ? ImmediatePhysics::FSimulation::EForceType::AddVelocity : ImmediatePhysics::FSimulation::EForceType::AddImpulse;
					}
					else
					{
						ForceType = PendingRadialForce.bIgnoreMass ? ImmediatePhysics::FSimulation::EForceType::AddAcceleration : ImmediatePhysics::FSimulation::EForceType::AddForce;
					}
					
					Body->AddRadialForce(RadialForceOrigin, StrengthPerBody, PendingRadialForce.Radius, PendingRadialForce.Falloff, ForceType);
				}
			}
		}

		if(!ExternalForce.IsNearlyZero())
		{
			const FVector ExternalForceInSimSpace = WorldVectorToSpaceNoScaleChaos(SimulationSpace, ExternalForce, ComponentToWorld, BaseBoneTM);
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
#endif
}

bool FAnimNode_RigidBody_Chaos::NeedsDynamicReset() const
{
	return true;
}

void FAnimNode_RigidBody_Chaos::ResetDynamics(ETeleportType InTeleportType)
{
	// This will be picked up next evaluate and reset our simulation.
	// Teleport type can only go higher - i.e. if we have requested a reset, then a teleport will still reset fully
	ResetSimulatedTeleportType = ((InTeleportType > ResetSimulatedTeleportType) ? InTeleportType : ResetSimulatedTeleportType);
}

DECLARE_CYCLE_STAT(TEXT("RigidBody_Chaos_PreUpdate"), STAT_RigidBody_Chaos_PreUpdate, STATGROUP_Anim);

void FAnimNode_RigidBody_Chaos::PreUpdate(const UAnimInstance* InAnimInstance)
{
#if CHAOS_RBAN_TODO
	// Don't update geometry if RBN is disabled
	if(CVarEnableRigidBodyNode.GetValueOnAnyThread() == 0)
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_RigidBody_Chaos_PreUpdate);

	UWorld* World = InAnimInstance->GetWorld();
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
#endif
}

int32 FAnimNode_RigidBody_Chaos::GetLODThreshold() const
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

DECLARE_CYCLE_STAT(TEXT("RigidBody_Chaos_Update"), STAT_RigidBody_Chaos_Update, STATGROUP_Anim);

void FAnimNode_RigidBody_Chaos::UpdateInternal(const FAnimationUpdateContext& Context)
{
	// Avoid this work if RBN is disabled, as the results would be discarded
	if(CVarEnableRigidBodyNode.GetValueOnAnyThread() == 0)
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_RigidBody_Chaos_PreUpdate);

	// Accumulate deltatime elapsed during update. To be used during evaluation.
	AccumulatedDeltaTime += Context.AnimInstanceProxy->GetDeltaSeconds();

#if CHAOS_RBAN_TODO
	if (UnsafeWorld != nullptr)
	{		

#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
		// Node is valid to evaluate. Simulation is starting.
		bSimulationStarted = true;

		TArray<FOverlapResult> Overlaps;
		UnsafeWorld->OverlapMultiByChannel(Overlaps, Bounds.Center, FQuat::Identity, OverlapChannel, FCollisionShape::MakeSphere(Bounds.W), QueryParams, FCollisionResponseParams(ECR_Overlap));

		SCOPED_SCENE_READ_LOCK(PhysScene ? PhysScene->GetPxScene() : nullptr); //TODO: expose this part to the anim node
	
		for (const FOverlapResult& Overlap : Overlaps)
		{
			if (UPrimitiveComponent* OverlapComp = Overlap.GetComponent())
			{
				if (ComponentsInSim.Contains(OverlapComp) == false)
				{
					ComponentsInSim.Add(OverlapComp);
#if WITH_CHAOS || WITH_IMMEDIATE_PHYSX
					check(false);
#else
					if (PxRigidActor* RigidActor = FPhysicsInterface_PhysX::GetPxRigidActor_AssumesLocked(OverlapComp->BodyInstance.ActorHandle))
					{
						PhysicsSimulation->CreateStaticActor(RigidActor, P2UTransform(RigidActor->getGlobalPose()));
					}
#endif
				}
			}
		}
#endif

		UnsafeWorld = nullptr;
		PhysScene = nullptr;
	}
#endif
}

void FAnimNode_RigidBody_Chaos::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
#if CHAOS_RBAN_TODO
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

	for(int32 Index = 0; Index < NumRequiredBoneIndices; ++Index)
	{
		const FCompactPoseBoneIndex CompactPoseBoneIndex(Index);
		const FBoneIndexType SkeletonBoneIndex = RequiredBones.GetSkeletonIndex(CompactPoseBoneIndex);
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
				OutputData->ParentBodyIndex = SkeletonBoneIndexToBodyIndex[SkeletonParentBoneIndex];
				if (OutputData->ParentBodyIndex != INDEX_NONE)
				{
					break;
				}

				OutputData->BoneIndicesToParentBody.Add(CompactParentIndex);
				CompactParentIndex = RequiredBones.GetParentBoneIndex(CompactParentIndex);
			}
		}
	}

	// New bodies potentially introduced with new LOD
	// We'll have to initialize their transform.
	bCheckForBodyTransformInit = true;

	if(PhysicsSimulation)
	{
		PhysicsSimulation->SetNumActiveBodies(NumSimulatedBodies);
	}

	// We're switching to a new LOD, this invalidates our captured poses.
	CapturedFrozenPose.Empty();
	CapturedFrozenCurves.Empty();
#endif
}

void FAnimNode_RigidBody_Chaos::OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance)
{
	InitPhysics(InAnimInstance);
}

#if WITH_EDITORONLY_DATA
void FAnimNode_RigidBody_Chaos::PostSerialize(const FArchive& Ar)
{
}
#endif

#undef LOCTEXT_NAMESPACE
