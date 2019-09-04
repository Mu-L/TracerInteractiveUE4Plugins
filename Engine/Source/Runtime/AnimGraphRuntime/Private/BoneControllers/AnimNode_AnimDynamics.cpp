// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BoneControllers/AnimNode_AnimDynamics.h"
#include "GameFramework/WorldSettings.h"
#include "Animation/AnimInstanceProxy.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "CommonAnimationLibrary.h"

DEFINE_STAT(STAT_AnimDynamicsOverall);
DEFINE_STAT(STAT_AnimDynamicsWindData);
DEFINE_STAT(STAT_AnimDynamicsBoneEval);
DEFINE_STAT(STAT_AnimDynamicsSubSteps);

TAutoConsoleVariable<int32> CVarRestrictLod(TEXT("p.AnimDynamicsRestrictLOD"), -1, TEXT("Forces anim dynamics to be enabled for only a specified LOD, -1 to enable on all LODs."));
TAutoConsoleVariable<int32> CVarLODThreshold(TEXT("p.AnimDynamicsLODThreshold"), -1, TEXT("Max LOD that anim dynamics is allowed to run on. Provides a global threshold that overrides per-node the LODThreshold property. -1 means no override."), ECVF_Scalability);
TAutoConsoleVariable<int32> CVarEnableDynamics(TEXT("p.AnimDynamics"), 1, TEXT("Enables/Disables anim dynamics node updates."), ECVF_Scalability);
TAutoConsoleVariable<int32> CVarEnableAdaptiveSubstep(TEXT("p.AnimDynamicsAdaptiveSubstep"), 0, TEXT("Enables/disables adaptive substepping. Adaptive substepping will substep the simulation when it is necessary and maintain a debt buffer for time, always trying to utilise as much time as possible."));
TAutoConsoleVariable<int32> CVarAdaptiveSubstepNumDebtFrames(TEXT("p.AnimDynamicsNumDebtFrames"), 5, TEXT("Number of frames to maintain as time debt when using adaptive substepping, this should be at least 1 or the time debt will never be cleared."));
TAutoConsoleVariable<int32> CVarEnableWind(TEXT("p.AnimDynamicsWind"), 1, TEXT("Enables/Disables anim dynamics wind forces globally."), ECVF_Scalability);

const float FAnimNode_AnimDynamics::MaxTimeDebt = (1.0f / 60.0f) * 5.0f; // 5 frames max debt

#if ENABLE_ANIM_DRAW_DEBUG

TAutoConsoleVariable<int32> CVarShowDebug(TEXT("p.animdynamics.showdebug"), 0, TEXT("Enable/disable the drawing of animdynamics data."));
TAutoConsoleVariable<FString> CVarDebugBone(TEXT("p.animdynamics.debugbone"), FString(), TEXT("Filters p.animdynamics.showdebug to a specific bone by name."));

void FAnimNode_AnimDynamics::DrawBodies(FComponentSpacePoseContext& InContext, const TArray<FAnimPhysRigidBody*>& InBodies)
{
	if(CVarShowDebug.GetValueOnAnyThread() == 0)
	{
		return;
	}

	auto ToWorldT = [this](FComponentSpacePoseContext& InPoseContext, const FTransform& SimTransform)
	{
		FTransform OutTransform = GetComponentSpaceTransformFromSimSpace(SimulationSpace, InPoseContext, SimTransform);
		OutTransform *= InPoseContext.AnimInstanceProxy->GetComponentTransform();
		return OutTransform;
	};

	auto ToWorldV = [this](FComponentSpacePoseContext& InPoseContext, const FVector& SimLocation)
	{
		FVector OutLoc = GetComponentSpaceTransformFromSimSpace(SimulationSpace, InPoseContext, FTransform(SimLocation)).GetTranslation();
		OutLoc = InPoseContext.AnimInstanceProxy->GetComponentTransform().TransformPosition(SimLocation);
		return OutLoc;
	};

	FAnimInstanceProxy* Proxy = InContext.AnimInstanceProxy;

	check(Proxy);

	const FString FilteredBoneName = CVarDebugBone.GetValueOnAnyThread();
	const bool bFilterBone = FilteredBoneName.Len() > 0;

	const int32 NumBodies = Bodies.Num();
	for(int32 BodyIndex = 0 ; BodyIndex < NumBodies ; ++BodyIndex)
	{
		const FAnimPhysRigidBody& Body = Bodies[BodyIndex].RigidBody.PhysBody;

		if(bFilterBone && BoundBoneReferences[BodyIndex].BoneName != FName(*FilteredBoneName))
		{
			continue;
		}

		FTransform Transform(Body.Pose.Orientation, Body.Pose.Position + Body.Pose.Orientation.RotateVector(JointOffsets[BodyIndex]));
		Transform = GetComponentSpaceTransformFromSimSpace(SimulationSpace, InContext, Transform);
		Transform *= Proxy->GetComponentTransform();

		Proxy->AnimDrawDebugCoordinateSystem(Transform.GetTranslation(), Transform.Rotator(), 2.0f, false, -1.0f, 0.15f);

		for(const FAnimPhysShape& Shape : Body.Shapes)
		{
			const int32 NumTris = Shape.Triangles.Num();
			for(int32 TriIndex = 0; TriIndex < NumTris; ++TriIndex)
			{
				FIntVector Tri = Shape.Triangles[TriIndex];

				Proxy->AnimDrawDebugLine(ToWorldV(InContext, Transform.TransformPosition(Shape.Vertices[Tri.X])), ToWorldV(InContext, Transform.TransformPosition(Shape.Vertices[Tri.Y])), FColor::Yellow, false, -1.0f, 0.15f);
				Proxy->AnimDrawDebugLine(ToWorldV(InContext, Transform.TransformPosition(Shape.Vertices[Tri.Y])), ToWorldV(InContext, Transform.TransformPosition(Shape.Vertices[Tri.Z])), FColor::Yellow, false, -1.0f, 0.15f);
				Proxy->AnimDrawDebugLine(ToWorldV(InContext, Transform.TransformPosition(Shape.Vertices[Tri.Z])), ToWorldV(InContext, Transform.TransformPosition(Shape.Vertices[Tri.X])), FColor::Yellow, false, -1.0f, 0.15f);
			}
		}
	}

	if(SimulationSpace != AnimPhysSimSpaceType::World)
	{
		FTransform Origin;

		switch(SimulationSpace)
		{
			case AnimPhysSimSpaceType::Actor:
			{
				Origin = Proxy->GetActorTransform();
			}
			break;
			case AnimPhysSimSpaceType::BoneRelative:
			{
				Origin = Proxy->GetComponentTransform() * InContext.Pose.GetComponentSpaceTransform(FCompactPoseBoneIndex(RelativeSpaceBone.BoneIndex));
			}
			break;
			case AnimPhysSimSpaceType::Component:
			{
				Origin = Proxy->GetComponentTransform();
			}
			break;
			case AnimPhysSimSpaceType::RootRelative:
			{
				Origin = Proxy->GetComponentTransform() * InContext.Pose.GetComponentSpaceTransform(FCompactPoseBoneIndex(0));
			}
			break;

			default: break;
		}

		Proxy->AnimDrawDebugSphere(Origin.GetTranslation(), 25.0f, 16, FColor::Green, false, -1.0f, 0.15f);
		Proxy->AnimDrawDebugCoordinateSystem(Origin.GetTranslation(), Origin.Rotator(), 3.0f, false, -1.0f, 0.15f);
	}
}
#endif

FAnimNode_AnimDynamics::FAnimNode_AnimDynamics()
: LinearDampingOverride(0.0f)
, AngularDampingOverride(0.0f)
, PreviousCompWorldSpaceTM(FTransform::Identity)
, PreviousActorWorldSpaceTM(FTransform::Identity)
, BoxExtents(0.0f)
, LocalJointOffset(0.0f)
, GravityScale(1.0f)
, GravityOverride(ForceInitToZero)
, LinearSpringConstant(0.0f)
, AngularSpringConstant(0.0f)
, WindScale(1.0f)
, AngularBiasOverride(0.0f)
, NumSolverIterationsPreUpdate(4)
, NumSolverIterationsPostUpdate(1)
, SphereCollisionRadius(0.0f)
, ExternalForce(ForceInitToZero)
, CollisionType(AnimPhysCollisionType::CoM)
, SimulationSpace(AnimPhysSimSpaceType::Component)
, LastSimSpace(AnimPhysSimSpaceType::Component)
, InitTeleportType(ETeleportType::None)
, bUseSphericalLimits(false)
, bUsePlanarLimit(true)
, bDoUpdate(true)
, bDoEval(true)
, bOverrideLinearDamping(false)
, bOverrideAngularBias(false)
, bOverrideAngularDamping(false)
, bEnableWind(false)
, bWindWasEnabled(false)
, bUseGravityOverride(false)
, bLinearSpring(false)
, bAngularSpring(false)
, bChain(false)
, RetargetingSettings(FRotationRetargetingInfo(false /* enabled */))
#if ENABLE_ANIM_DRAW_DEBUG
, FilteredBoneIndex(INDEX_NONE)
#endif
{
	ComponentLinearAccScale = FVector::ZeroVector;
	ComponentLinearVelScale = FVector::ZeroVector;
	ComponentAppliedLinearAccClamp = FVector(100000, 100000, 100000);

	PreviousComponentLinearVelocity = FVector::ZeroVector;
}

void FAnimNode_AnimDynamics::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_SkeletalControlBase::Initialize_AnyThread(Context);

	FBoneContainer& RequiredBones = Context.AnimInstanceProxy->GetRequiredBones();

	InitializeBoneReferences(RequiredBones);

	if(BoundBone.IsValidToEvaluate(RequiredBones))
	{
		RequestInitialise(ETeleportType::ResetPhysics);
	}

	PreviousCompWorldSpaceTM = Context.AnimInstanceProxy->GetComponentTransform();
	PreviousActorWorldSpaceTM = Context.AnimInstanceProxy->GetActorTransform();

	NextTimeStep = 0.0f;
	TimeDebt = 0.0f;
}

void FAnimNode_AnimDynamics::UpdateInternal(const FAnimationUpdateContext& Context)
{
	FAnimNode_SkeletalControlBase::UpdateInternal(Context);

	NextTimeStep = Context.GetDeltaTime();
}

struct FSimBodiesScratch : public TThreadSingleton<FSimBodiesScratch>
{
	TArray<FAnimPhysRigidBody*> SimBodies;
};

bool FAnimNode_AnimDynamics::IsAnimDynamicsSystemEnabledFor(int32 InLOD)
{
	int32 RestrictToLOD = CVarRestrictLod.GetValueOnAnyThread();
	bool bEnabledForLod = RestrictToLOD >= 0 ? InLOD == RestrictToLOD : true;

	// note this doesn't check LODThreshold of global value here. That's checked in
	// GetLODThreshold per node
	return (CVarEnableDynamics.GetValueOnAnyThread() == 1 && bEnabledForLod);
}
void FAnimNode_AnimDynamics::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	SCOPE_CYCLE_COUNTER(STAT_AnimDynamicsOverall);

	if (IsAnimDynamicsSystemEnabledFor(Output.AnimInstanceProxy->GetLODLevel()))
	{
		if(LastSimSpace != SimulationSpace)
		{
			// Our sim space has been changed since our last update, we need to convert all of our
			// body transforms into the new space.
			ConvertSimulationSpace(Output, LastSimSpace, SimulationSpace);
		}

		// Pretty nasty - but there isn't really a good way to get clean bone transforms (without the modification from
		// previous runs) so we have to initialize here, checking often so we can restart a simulation in the editor.
		if (InitTeleportType != ETeleportType::None)
		{
			InitPhysics(Output);
			InitTeleportType = ETeleportType::None;
		}

		const FBoneContainer& RequiredBones = Output.Pose.GetPose().GetBoneContainer();
		while(BodiesToReset.Num() > 0)
		{
			FAnimPhysLinkedBody* BodyToReset = BodiesToReset.Pop(false);
			if(BodyToReset && BodyToReset->RigidBody.BoundBone.IsValidToEvaluate(RequiredBones))
			{
				FTransform BoneTransform = GetBoneTransformInSimSpace(Output, BodyToReset->RigidBody.BoundBone.GetCompactPoseIndex(RequiredBones));
				FAnimPhysRigidBody& PhysBody = BodyToReset->RigidBody.PhysBody;

				PhysBody.Pose.Position = BoneTransform.GetTranslation();
				PhysBody.Pose.Orientation = BoneTransform.GetRotation();
				PhysBody.LinearMomentum = FVector::ZeroVector;
				PhysBody.AngularMomentum = FVector::ZeroVector;
			}
		}

		if (bDoUpdate && NextTimeStep > AnimPhysicsMinDeltaTime)
		{
			// Calculate gravity direction
			SimSpaceGravityDirection = TransformWorldVectorToSimSpace(Output, FVector(0.0f, 0.0f, -1.0f));

			FVector OrientedExternalForce = ExternalForce;
			if(!OrientedExternalForce.IsNearlyZero())
			{
				OrientedExternalForce = TransformWorldVectorToSimSpace(Output, OrientedExternalForce);
			}

			// We don't send any bodies that don't have valid bones to the simulation
			TArray<FAnimPhysRigidBody*>& SimBodies = FSimBodiesScratch::Get().SimBodies;
			SimBodies.Empty(SimBodies.Num());
			for(int32& ActiveIndex : ActiveBoneIndices)
			{
				if(BaseBodyPtrs.IsValidIndex(ActiveIndex))
				{
					SimBodies.Add(BaseBodyPtrs[ActiveIndex]);
				}
			}

			FVector ComponentLinearAcc(0.0f);

			if (SimulationSpace != AnimPhysSimSpaceType::World)
			{
				FTransform CurrentTransform = Output.AnimInstanceProxy->GetComponentTransform();

				// Calc linear velocity
				const FVector ComponentDeltaLocation = CurrentTransform.GetTranslation() - PreviousCompWorldSpaceTM.GetTranslation();
				const FVector ComponentLinearVelocity = ComponentDeltaLocation / NextTimeStep;
				// Apply acceleration that opposed velocity (basically 'drag')
				ComponentLinearAcc += TransformWorldVectorToSimSpace(Output, -ComponentLinearVelocity) * ComponentLinearVelScale;

				// Calc linear acceleration
				const FVector ComponentLinearAcceleration = (ComponentLinearVelocity - PreviousComponentLinearVelocity) / NextTimeStep;
				PreviousComponentLinearVelocity = ComponentLinearVelocity;
				// Apply opposite acceleration to bodies
				ComponentLinearAcc += TransformWorldVectorToSimSpace(Output, -ComponentLinearAcceleration) * ComponentLinearAccScale;

				// Clamp to desired strength
				ComponentLinearAcc = ComponentLinearAcc.BoundToBox(-ComponentAppliedLinearAccClamp, ComponentAppliedLinearAccClamp);
			}

			if (CVarEnableAdaptiveSubstep.GetValueOnAnyThread() == 1)
			{
				float CurrentTimeDilation = Output.AnimInstanceProxy->GetTimeDilation();
				float FixedTimeStep = MaxSubstepDeltaTime * CurrentTimeDilation;

				// Clamp the fixed timestep down to max physics tick time.
				// at high speeds the simulation will not converge as the delta time is too high, this will
				// help to keep constraints together at a cost of physical accuracy
				FixedTimeStep = FMath::Clamp(FixedTimeStep, 0.0f, MaxPhysicsDeltaTime);

				// Calculate number of substeps we should do.
				int32 NumIters = FMath::TruncToInt((NextTimeStep + (TimeDebt * CurrentTimeDilation)) / FixedTimeStep);
				NumIters = FMath::Clamp(NumIters, 0, MaxSubsteps);

				SET_DWORD_STAT(STAT_AnimDynamicsSubSteps, NumIters);

				// Store the remaining time as debt for later frames
				TimeDebt = (NextTimeStep + TimeDebt) - (NumIters * FixedTimeStep);
				TimeDebt = FMath::Clamp(TimeDebt, 0.0f, MaxTimeDebt);

				NextTimeStep = FixedTimeStep;

				for (int32 Iter = 0; Iter < NumIters; ++Iter)
				{
					UpdateLimits(Output);
					FAnimPhys::PhysicsUpdate(FixedTimeStep, SimBodies, LinearLimits, AngularLimits, Springs, SimSpaceGravityDirection, OrientedExternalForce, ComponentLinearAcc, NumSolverIterationsPreUpdate, NumSolverIterationsPostUpdate);
				}
			}
			else
			{
				// Do variable frame-time update
				const float MaxDeltaTime = MaxPhysicsDeltaTime;

				NextTimeStep = FMath::Min(NextTimeStep, MaxDeltaTime);

				UpdateLimits(Output);
				FAnimPhys::PhysicsUpdate(NextTimeStep, SimBodies, LinearLimits, AngularLimits, Springs, SimSpaceGravityDirection, OrientedExternalForce, ComponentLinearAcc, NumSolverIterationsPreUpdate, NumSolverIterationsPostUpdate);
			}

#if ENABLE_ANIM_DRAW_DEBUG
			DrawBodies(Output, SimBodies);
#endif
		}

		if (bDoEval)
		{
			SCOPE_CYCLE_COUNTER(STAT_AnimDynamicsBoneEval);

			const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();

			for (int32 Idx = 0; Idx < BoundBoneReferences.Num(); ++Idx)
			{
				FBoneReference& CurrentChainBone = BoundBoneReferences[Idx];
				FAnimPhysRigidBody& CurrentBody = Bodies[Idx].RigidBody.PhysBody;

				// Skip invalid bones
				if(!CurrentChainBone.IsValidToEvaluate(BoneContainer))
				{
					continue;
				}

				FCompactPoseBoneIndex BoneIndex = CurrentChainBone.GetCompactPoseIndex(BoneContainer);

				FTransform NewBoneTransform(CurrentBody.Pose.Orientation, CurrentBody.Pose.Position + CurrentBody.Pose.Orientation.RotateVector(JointOffsets[Idx]));

				if (RetargetingSettings.bEnabled)
				{
					FTransform ParentTransform = FTransform::Identity;
					FCompactPoseBoneIndex ParentBoneIndex = BoneContainer.GetParentBoneIndex(BoneIndex);
					if (ParentBoneIndex != INDEX_NONE)
					{
						ParentTransform = GetBoneTransformInSimSpace(Output, ParentBoneIndex);
					}

					FQuat RetargetedRotation = CommonAnimationLibrary::RetargetSingleRotation(
						NewBoneTransform.GetRotation(),
						RetargetingSettings.Source * ParentTransform,
						RetargetingSettings.Target * ParentTransform,
						RetargetingSettings.CustomCurve,
						RetargetingSettings.EasingType,
						RetargetingSettings.bFlipEasing,
						RetargetingSettings.EasingWeight,
						RetargetingSettings.RotationComponent,
						RetargetingSettings.TwistAxis,
						RetargetingSettings.bUseAbsoluteAngle,
						RetargetingSettings.SourceMinimum,
						RetargetingSettings.SourceMaximum,
						RetargetingSettings.TargetMinimum,
						RetargetingSettings.TargetMaximum);

					NewBoneTransform.SetRotation(RetargetedRotation);
				}

				NewBoneTransform = GetComponentSpaceTransformFromSimSpace(SimulationSpace, Output, NewBoneTransform);

				OutBoneTransforms.Add(FBoneTransform(BoneIndex, NewBoneTransform));
			}
		}

		// Store our sim space incase it changes
		LastSimSpace = SimulationSpace;

		// Store previous component and actor space transform
		PreviousCompWorldSpaceTM = Output.AnimInstanceProxy->GetComponentTransform();
		PreviousActorWorldSpaceTM = Output.AnimInstanceProxy->GetActorTransform();
	}
}

void FAnimNode_AnimDynamics::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	BoundBone.Initialize(RequiredBones);

	if (bChain)
	{
		ChainEnd.Initialize(RequiredBones);
	}

	for (FAnimPhysPlanarLimit& PlanarLimit : PlanarLimits)
	{
		PlanarLimit.DrivingBone.Initialize(RequiredBones);
	}

	for (FAnimPhysSphericalLimit& SphericalLimit : SphericalLimits)
	{
		SphericalLimit.DrivingBone.Initialize(RequiredBones);
	}

	if (SimulationSpace == AnimPhysSimSpaceType::BoneRelative)
	{
		RelativeSpaceBone.Initialize(RequiredBones);
	}
	
	// If we're currently simulating (LOD change etc.)
	bool bSimulating = ActiveBoneIndices.Num() > 0;

	const int32 NumRefs = BoundBoneReferences.Num();
	for(int32 BoneRefIdx = 0; BoneRefIdx < NumRefs; ++BoneRefIdx)
	{
		FBoneReference& BoneRef = BoundBoneReferences[BoneRefIdx];
		BoneRef.Initialize(RequiredBones);

		if(bSimulating)
		{
			if(BoneRef.IsValidToEvaluate(RequiredBones) && !ActiveBoneIndices.Contains(BoneRefIdx))
			{
				// This body is inactive and needs to be reset to bone position
				// as it is now required for the current LOD
				BodiesToReset.Add(&Bodies[BoneRefIdx]);
			}
		}
	}

	ActiveBoneIndices.Empty(ActiveBoneIndices.Num());
	const int32 NumBodies = Bodies.Num();
	for(int32 BodyIdx = 0; BodyIdx < NumBodies; ++BodyIdx)
	{
		FAnimPhysLinkedBody& LinkedBody = Bodies[BodyIdx];
		LinkedBody.RigidBody.BoundBone.Initialize(RequiredBones);

		// If this bone is active in this LOD, add to the active list.
		if(LinkedBody.RigidBody.BoundBone.IsValidToEvaluate(RequiredBones))
		{
			ActiveBoneIndices.Add(BodyIdx);
		}
	}
}

void FAnimNode_AnimDynamics::GatherDebugData(FNodeDebugData& DebugData)
{
	const float ActualBiasedAlpha = AlphaScaleBias.ApplyTo(Alpha);

	FString DebugLine = DebugData.GetNodeName(this);
	DebugLine += FString::Printf(TEXT("(Alpha: %.1f%%)"), ActualBiasedAlpha*100.f);

	DebugData.AddDebugItem(DebugLine);
	ComponentPose.GatherDebugData(DebugData);
}

bool FAnimNode_AnimDynamics::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	bool bValid = BoundBone.IsValidToEvaluate(RequiredBones);

	if (bChain)
	{
		bool bChainEndValid = ChainEnd.IsValidToEvaluate(RequiredBones);
		bool bSubChainValid = false;

		if(!bChainEndValid)
		{
			// Check for LOD subchain
			int32 NumValidBonesFromRoot = 0;
			for(FBoneReference& BoneRef : BoundBoneReferences)
			{
				if(BoneRef.IsValidToEvaluate(RequiredBones))
				{
					bSubChainValid = true;
					break;
				}
			}
		}

		bValid = bValid && (bChainEndValid || bSubChainValid);
	}

	return bValid;
}

int32 FAnimNode_AnimDynamics::GetNumBodies() const
{
	return Bodies.Num();
}

const FAnimPhysRigidBody& FAnimNode_AnimDynamics::GetPhysBody(int32 BodyIndex) const
{
	return Bodies[BodyIndex].RigidBody.PhysBody;
}

#if WITH_EDITOR
FVector FAnimNode_AnimDynamics::GetBodyLocalJointOffset(int32 BodyIndex) const
{
	if (JointOffsets.IsValidIndex(BodyIndex))
	{
		return JointOffsets[BodyIndex];
	}
	return FVector::ZeroVector;
}

int32 FAnimNode_AnimDynamics::GetNumBoundBones() const
{
	return BoundBoneReferences.Num();
}

const FBoneReference* FAnimNode_AnimDynamics::GetBoundBoneReference(int32 Index) const
{
	if(BoundBoneReferences.IsValidIndex(Index))
	{
		return &BoundBoneReferences[Index];
	}
	return nullptr;
}

#endif

void FAnimNode_AnimDynamics::RequestInitialise(ETeleportType InTeleportType)
{ 
	// Request an initialization. Teleport type can only go higher - i.e. if we have requested a reset, then a teleport will still reset fully
	InitTeleportType = ((InTeleportType > InitTeleportType) ? InTeleportType : InitTeleportType); 
}

void FAnimNode_AnimDynamics::InitPhysics(FComponentSpacePoseContext& Output)
{
	switch(InitTeleportType)
	{
		case ETeleportType::ResetPhysics:
		{
			// Clear up any existing physics data
			TermPhysics();

			const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();

	
			// List of bone indices in the chain.
			TArray<int32> ChainBoneIndices;
			TArray<FName> ChainBoneNames;

			if(ChainEnd.IsValidToEvaluate(BoneContainer))
			{
				// Add the end of the chain. We have to walk from the bottom upwards to find a chain
				// as walking downwards doesn't guarantee a single end point.

				ChainBoneIndices.Add(ChainEnd.BoneIndex);
				ChainBoneNames.Add(ChainEnd.BoneName);

				int32 ParentBoneIndex = BoneContainer.GetParentBoneIndex(ChainEnd.BoneIndex);

				// Walk up the chain until we either find the top or hit the root bone
				while(ParentBoneIndex > 0)
				{
					ChainBoneIndices.Add(ParentBoneIndex);
					ChainBoneNames.Add(BoneContainer.GetReferenceSkeleton().GetBoneName(ParentBoneIndex));

					if(ParentBoneIndex == BoundBone.BoneIndex)
					{
						// Found the top of the chain
						break;
					}

					ParentBoneIndex = BoneContainer.GetParentBoneIndex(ParentBoneIndex);
				}

				// Bail if we can't find a chain, and let the user know
				if(ParentBoneIndex != BoundBone.BoneIndex)
				{
					UE_LOG(LogAnimation, Error, TEXT("AnimDynamics: Attempted to find bone chain starting at %s and ending at %s but failed."), *BoundBone.BoneName.ToString(), *ChainEnd.BoneName.ToString());
					return;
				}
			}
			else
			{
				// No chain specified, just use the bound bone
				ChainBoneIndices.Add(BoundBone.BoneIndex);
				ChainBoneNames.Add(BoundBone.BoneName);
			}

			Bodies.Reserve(ChainBoneIndices.Num());
			// Walk backwards here as the chain was discovered in reverse order
			for (int32 Idx = ChainBoneIndices.Num() - 1; Idx >= 0; --Idx)
			{
				TArray<FAnimPhysShape> BodyShapes;
				BodyShapes.Add(FAnimPhysShape::MakeBox(BoxExtents));

				FBoneReference LinkBoneRef;
				LinkBoneRef.BoneName = ChainBoneNames[Idx];
				LinkBoneRef.Initialize(BoneContainer);

				// Calculate joint offsets by looking at the length of the bones and extending the provided offset
				if (BoundBoneReferences.Num() > 0)
				{
					FTransform CurrentBoneTransform = GetBoneTransformInSimSpace(Output, LinkBoneRef.GetCompactPoseIndex(BoneContainer));
					FTransform PreviousBoneTransform = GetBoneTransformInSimSpace(Output, BoundBoneReferences.Last().GetCompactPoseIndex(BoneContainer));

					FVector PreviousAnchor = PreviousBoneTransform.TransformPosition(-LocalJointOffset);
					float DistanceToAnchor = (PreviousBoneTransform.GetTranslation() - CurrentBoneTransform.GetTranslation()).Size() * 0.5f;

					if(LocalJointOffset.SizeSquared() < SMALL_NUMBER)
					{
						// No offset, just use the position between chain links as the offset
						// This is likely to just look horrible, but at least the bodies will
						// be placed correctly and not stack up at the top of the chain.
						JointOffsets.Add(PreviousAnchor - CurrentBoneTransform.GetTranslation());
					}
					else
					{
						// Extend offset along chain.
						JointOffsets.Add(LocalJointOffset.GetSafeNormal() * DistanceToAnchor);
					}
				}
				else
				{
					// No chain to worry about, just use the specified offset.
					JointOffsets.Add(LocalJointOffset);
				}

				BoundBoneReferences.Add(LinkBoneRef);

				FTransform BodyTransform = GetBoneTransformInSimSpace(Output, LinkBoneRef.GetCompactPoseIndex(BoneContainer));

				BodyTransform.SetTranslation(BodyTransform.GetTranslation() + BodyTransform.GetRotation().RotateVector(-LocalJointOffset));

				FAnimPhysLinkedBody NewChainBody(BodyShapes, BodyTransform.GetTranslation(), LinkBoneRef);
				FAnimPhysRigidBody& PhysicsBody = NewChainBody.RigidBody.PhysBody;
				PhysicsBody.Pose.Orientation = BodyTransform.GetRotation();
				PhysicsBody.PreviousOrientation = PhysicsBody.Pose.Orientation;
				PhysicsBody.NextOrientation = PhysicsBody.Pose.Orientation;
				PhysicsBody.CollisionType = CollisionType;

				switch(PhysicsBody.CollisionType)
				{
					case AnimPhysCollisionType::CustomSphere:
						PhysicsBody.SphereCollisionRadius = SphereCollisionRadius;
						break;
					case AnimPhysCollisionType::InnerSphere:
						PhysicsBody.SphereCollisionRadius = BoxExtents.GetAbsMin() / 2.0f;
						break;
					case AnimPhysCollisionType::OuterSphere:
						PhysicsBody.SphereCollisionRadius = BoxExtents.GetAbsMax() / 2.0f;
						break;
					default:
						break;
				}

				if (bOverrideLinearDamping)
				{
					PhysicsBody.bLinearDampingOverriden = true;
					PhysicsBody.LinearDamping = LinearDampingOverride;
				}

				if (bOverrideAngularDamping)
				{
					PhysicsBody.bAngularDampingOverriden = true;
					PhysicsBody.AngularDamping = AngularDampingOverride;
				}

				PhysicsBody.GravityScale = GravityScale;
				PhysicsBody.bUseGravityOverride = bUseGravityOverride;
				PhysicsBody.GravityOverride = GravityOverride;

				PhysicsBody.bWindEnabled = bWindWasEnabled;

				// Link to parent
				if (Bodies.Num() > 0)
				{
					NewChainBody.ParentBody = &Bodies.Last().RigidBody;
				}

				Bodies.Add(NewChainBody);
				ActiveBoneIndices.Add(Bodies.Num() - 1);
			}
	
			BaseBodyPtrs.Empty();
			for(FAnimPhysLinkedBody& Body : Bodies)
			{
				BaseBodyPtrs.Add(&Body.RigidBody.PhysBody);
			}

			// Set up transient constraint data
			const bool bXAxisLocked = ConstraintSetup.LinearXLimitType != AnimPhysLinearConstraintType::Free && ConstraintSetup.LinearAxesMin.X - ConstraintSetup.LinearAxesMax.X == 0.0f;
			const bool bYAxisLocked = ConstraintSetup.LinearYLimitType != AnimPhysLinearConstraintType::Free && ConstraintSetup.LinearAxesMin.Y - ConstraintSetup.LinearAxesMax.Y == 0.0f;
			const bool bZAxisLocked = ConstraintSetup.LinearZLimitType != AnimPhysLinearConstraintType::Free && ConstraintSetup.LinearAxesMin.Z - ConstraintSetup.LinearAxesMax.Z == 0.0f;

			ConstraintSetup.bLinearFullyLocked = bXAxisLocked && bYAxisLocked && bZAxisLocked;

			// Cache physics settings to avoid accessing UPhysicsSettings continuously
			if(UPhysicsSettings* Settings = UPhysicsSettings::Get())
			{
				AnimPhysicsMinDeltaTime = Settings->AnimPhysicsMinDeltaTime;
				MaxPhysicsDeltaTime = Settings->MaxPhysicsDeltaTime;
				MaxSubstepDeltaTime = Settings->MaxSubstepDeltaTime;
				MaxSubsteps = Settings->MaxSubsteps;
			}
			else
			{
				AnimPhysicsMinDeltaTime = 0.f;
				MaxPhysicsDeltaTime = (1.0f/30.0f);
				MaxSubstepDeltaTime = (1.0f/60.0f);
				MaxSubsteps = 4;
			}

			SimSpaceGravityDirection = TransformWorldVectorToSimSpace(Output, FVector(0.0f, 0.0f, -1.0f));
		}
		break;

		case ETeleportType::TeleportPhysics:
		{
			// Clear any external forces
			ExternalForce = FVector::ZeroVector;

			// Move any active bones
			for(const int32& BodyIndex : ActiveBoneIndices)
			{
				FAnimPhysRigidBody& Body = Bodies[BodyIndex].RigidBody.PhysBody;

				// Get old comp space transform
				FTransform BodyTransform(Body.Pose.Orientation, Body.Pose.Position + Body.Pose.Orientation.RotateVector(JointOffsets[BodyIndex]));
				BodyTransform = GetComponentSpaceTransformFromSimSpace(SimulationSpace, Output, BodyTransform, PreviousCompWorldSpaceTM, PreviousActorWorldSpaceTM);

				// move to new space
				BodyTransform = GetSimSpaceTransformFromComponentSpace(SimulationSpace, Output, BodyTransform);
				
				Body.Pose.Orientation = BodyTransform.GetRotation();
				Body.PreviousOrientation = Body.Pose.Orientation;
				Body.NextOrientation = Body.Pose.Orientation;

				Body.Pose.Position = BodyTransform.GetTranslation() - Body.Pose.Orientation.RotateVector(JointOffsets[BodyIndex]);
			}
		}
		break;
	}

	InitTeleportType = ETeleportType::None;
	PreviousCompWorldSpaceTM = Output.AnimInstanceProxy->GetComponentTransform();
	PreviousActorWorldSpaceTM = Output.AnimInstanceProxy->GetActorTransform();
}

void FAnimNode_AnimDynamics::TermPhysics()
{
	Bodies.Reset();
	LinearLimits.Reset();
	AngularLimits.Reset();
	Springs.Reset();
	ActiveBoneIndices.Reset();

	BoundBoneReferences.Reset();
	JointOffsets.Reset();
	BodiesToReset.Reset();
}

void FAnimNode_AnimDynamics::UpdateLimits(FComponentSpacePoseContext& Output)
{
	SCOPE_CYCLE_COUNTER(STAT_AnimDynamicsLimitUpdate);

	// We're always going to use the same number so don't realloc
	LinearLimits.Empty(LinearLimits.Num());
	AngularLimits.Empty(AngularLimits.Num());
	Springs.Empty(Springs.Num());

	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();

	for(int32 ActiveIndex : ActiveBoneIndices)
	{
		const FBoneReference& CurrentBoneRef = BoundBoneReferences[ActiveIndex];

		// If our bone isn't valid, move on
		if(!CurrentBoneRef.IsValidToEvaluate(BoneContainer))
		{
			continue;
		}

		FAnimPhysLinkedBody& ChainBody = Bodies[ActiveIndex];
		FAnimPhysRigidBody& RigidBody = Bodies[ActiveIndex].RigidBody.PhysBody;
		
		FAnimPhysRigidBody* PrevBody = nullptr;
		if (ChainBody.ParentBody)
		{
			PrevBody = &ChainBody.ParentBody->PhysBody;
		}

		// Get joint transform
		FCompactPoseBoneIndex BoneIndex = CurrentBoneRef.GetCompactPoseIndex(BoneContainer);
		FTransform BoundBoneTransform = GetBoneTransformInSimSpace(Output, BoneIndex);

		FTransform ShapeTransform = BoundBoneTransform;
		
		// Local offset to joint for Body1
		FVector Body1JointOffset = LocalJointOffset;

		if (PrevBody)
		{
			// Get the correct offset
			Body1JointOffset = JointOffsets[ActiveIndex];
			// Modify the shape transform to be correct in Body0 frame
			ShapeTransform = FTransform(FQuat::Identity, -Body1JointOffset);
		}
		
		if (ConstraintSetup.bLinearFullyLocked)
		{
			// Rather than calculate prismatic limits, just lock the transform (1 limit instead of 6)
			FAnimPhys::ConstrainPositionNailed(NextTimeStep, LinearLimits, PrevBody, ShapeTransform.GetTranslation(), &RigidBody, Body1JointOffset);
		}
		else
		{
			if (ConstraintSetup.LinearXLimitType != AnimPhysLinearConstraintType::Free)
			{
				FAnimPhys::ConstrainAlongDirection(NextTimeStep, LinearLimits, PrevBody, ShapeTransform.GetTranslation(), &RigidBody, Body1JointOffset, ShapeTransform.GetRotation().GetAxisX(), FVector2D(ConstraintSetup.LinearAxesMin.X, ConstraintSetup.LinearAxesMax.X));
			}

			if (ConstraintSetup.LinearYLimitType != AnimPhysLinearConstraintType::Free)
			{
				FAnimPhys::ConstrainAlongDirection(NextTimeStep, LinearLimits, PrevBody, ShapeTransform.GetTranslation(), &RigidBody, Body1JointOffset, ShapeTransform.GetRotation().GetAxisY(), FVector2D(ConstraintSetup.LinearAxesMin.Y, ConstraintSetup.LinearAxesMax.Y));
			}

			if (ConstraintSetup.LinearZLimitType != AnimPhysLinearConstraintType::Free)
			{
				FAnimPhys::ConstrainAlongDirection(NextTimeStep, LinearLimits, PrevBody, ShapeTransform.GetTranslation(), &RigidBody, Body1JointOffset, ShapeTransform.GetRotation().GetAxisZ(), FVector2D(ConstraintSetup.LinearAxesMin.Z, ConstraintSetup.LinearAxesMax.Z));
			}
		}

		if (ConstraintSetup.AngularConstraintType == AnimPhysAngularConstraintType::Angular)
		{
#if WITH_EDITOR
			// Check the ranges are valid when running in the editor, log if something is wrong
			if(ConstraintSetup.AngularLimitsMin.X > ConstraintSetup.AngularLimitsMax.X ||
			   ConstraintSetup.AngularLimitsMin.Y > ConstraintSetup.AngularLimitsMax.Y ||
			   ConstraintSetup.AngularLimitsMin.Z > ConstraintSetup.AngularLimitsMax.Z)
			{
				UE_LOG(LogAnimation, Warning, TEXT("AnimDynamics: Min/Max angular limits for bone %s incorrect, at least one min axis value is greater than the corresponding max."), *BoundBone.BoneName.ToString());
			}
#endif

			// Add angular limits. any limit with 360+ degree range is ignored and left free.
			FAnimPhys::ConstrainAngularRange(NextTimeStep, AngularLimits, PrevBody, &RigidBody, ShapeTransform.GetRotation(), ConstraintSetup.TwistAxis, ConstraintSetup.AngularLimitsMin, ConstraintSetup.AngularLimitsMax, bOverrideAngularBias ? AngularBiasOverride : AnimPhysicsConstants::JointBiasFactor);
		}
		else
		{
			FAnimPhys::ConstrainConeAngle(NextTimeStep, AngularLimits, PrevBody, BoundBoneTransform.GetRotation().GetAxisX(), &RigidBody, FVector(1.0f, 0.0f, 0.0f), ConstraintSetup.ConeAngle, bOverrideAngularBias ? AngularBiasOverride : AnimPhysicsConstants::JointBiasFactor);
		}

		if(PlanarLimits.Num() > 0 && bUsePlanarLimit)
		{
			for(FAnimPhysPlanarLimit& PlanarLimit : PlanarLimits)
			{
				FTransform LimitPlaneTransform = PlanarLimit.PlaneTransform;
				if(PlanarLimit.DrivingBone.IsValidToEvaluate(BoneContainer))
				{
					FCompactPoseBoneIndex DrivingBoneIndex = PlanarLimit.DrivingBone.GetCompactPoseIndex(BoneContainer);

					FTransform DrivingBoneTransform = GetBoneTransformInSimSpace(Output, DrivingBoneIndex);

					LimitPlaneTransform *= DrivingBoneTransform;
				}
				
				FAnimPhys::ConstrainPlanar(NextTimeStep, LinearLimits, &RigidBody, LimitPlaneTransform);
			}
		}

		if(SphericalLimits.Num() > 0 && bUseSphericalLimits)
		{
			for(FAnimPhysSphericalLimit& SphericalLimit : SphericalLimits)
			{
				FTransform SphereTransform = FTransform::Identity;
				SphereTransform.SetTranslation(SphericalLimit.SphereLocalOffset);

				if(SphericalLimit.DrivingBone.IsValidToEvaluate(BoneContainer))
				{
					FCompactPoseBoneIndex DrivingBoneIndex = SphericalLimit.DrivingBone.GetCompactPoseIndex(BoneContainer);

					FTransform DrivingBoneTransform = GetBoneTransformInSimSpace(Output, DrivingBoneIndex);

					SphereTransform *= DrivingBoneTransform;
				}

				switch(SphericalLimit.LimitType)
				{
				case ESphericalLimitType::Inner:
					FAnimPhys::ConstrainSphericalInner(NextTimeStep, LinearLimits, &RigidBody, SphereTransform, SphericalLimit.LimitRadius);
					break;
				case ESphericalLimitType::Outer:
					FAnimPhys::ConstrainSphericalOuter(NextTimeStep, LinearLimits, &RigidBody, SphereTransform, SphericalLimit.LimitRadius);
					break;
				default:
					break;
				}
			}
		}

		// Add spring if we need spring forces
		if (bAngularSpring || bLinearSpring)
		{
			FAnimPhys::CreateSpring(Springs, PrevBody, ShapeTransform.GetTranslation(), &RigidBody, FVector::ZeroVector);
			FAnimPhysSpring& NewSpring = Springs.Last();
			NewSpring.SpringConstantLinear = LinearSpringConstant;
			NewSpring.SpringConstantAngular = AngularSpringConstant;
			NewSpring.AngularTarget = ConstraintSetup.AngularTarget.GetSafeNormal();
			NewSpring.AngularTargetAxis = ConstraintSetup.AngularTargetAxis;
			NewSpring.TargetOrientationOffset = ShapeTransform.GetRotation();
			NewSpring.bApplyAngular = bAngularSpring;
			NewSpring.bApplyLinear = bLinearSpring;
		}
	}
}

bool FAnimNode_AnimDynamics::HasPreUpdate() const
{
	if(CVarEnableDynamics.GetValueOnGameThread() == 1)
	{
		return (CVarEnableWind.GetValueOnGameThread() == 1 && (bEnableWind || bWindWasEnabled))
#if ENABLE_ANIM_DRAW_DEBUG
				|| (CVarShowDebug.GetValueOnGameThread() == 1 && !CVarDebugBone.GetValueOnGameThread().IsEmpty())
#endif
				;
	}

	return false;
}

void FAnimNode_AnimDynamics::PreUpdate(const UAnimInstance* InAnimInstance)
{
	// If dynamics are disabled, skip all this work as it'll never get used
	if(CVarEnableDynamics.GetValueOnAnyThread() == 0)
	{
		return;
	}

	if(!InAnimInstance)
	{
		// No anim instance, won't be able to find our world.
		return;
	}
	
	const USkeletalMeshComponent* SkelComp = InAnimInstance->GetSkelMeshComponent();
	
	if(!SkelComp || !SkelComp->GetWorld())
	{
		// Can't find our world.
		return;
	}

	const UWorld* World = SkelComp->GetWorld();

	if(CVarEnableWind.GetValueOnAnyThread() == 1 && bEnableWind)
	{
		SCOPE_CYCLE_COUNTER(STAT_AnimDynamicsWindData);

		for(FAnimPhysRigidBody* Body : BaseBodyPtrs)
		{
			Body->bWindEnabled = bEnableWind;

			if(Body->bWindEnabled && World->Scene)
			{
				FSceneInterface* Scene = World->Scene;

				// Unused by our simulation but needed for the call to GetWindParameters below
				float WindMinGust;
				float WindMaxGust;

				// Setup wind data
				Body->bWindEnabled = true;
				Scene->GetWindParameters_GameThread(SkelComp->GetComponentTransform().TransformPosition(Body->Pose.Position), Body->WindData.WindDirection, Body->WindData.WindSpeed, WindMinGust, WindMaxGust);

				Body->WindData.WindDirection = SkelComp->GetComponentTransform().Inverse().TransformVector(Body->WindData.WindDirection);
				Body->WindData.WindAdaption = FMath::FRandRange(0.0f, 2.0f);
				Body->WindData.BodyWindScale = WindScale;
			}
		}
	}
	else if (bWindWasEnabled)
	{
		SCOPE_CYCLE_COUNTER(STAT_AnimDynamicsWindData);
	
		bWindWasEnabled = false;
		for(FAnimPhysRigidBody* Body : BaseBodyPtrs)
		{
			Body->bWindEnabled = false;
		}
	}

#if ENABLE_ANIM_DRAW_DEBUG
	FilteredBoneIndex = INDEX_NONE;
	if(SkelComp)
	{
		FString FilteredBoneName = CVarDebugBone.GetValueOnGameThread();
		if(FilteredBoneName.Len() > 0)
		{
			FilteredBoneIndex = SkelComp->GetBoneIndex(FName(*FilteredBoneName));
		}
	}
#endif
}

int32 FAnimNode_AnimDynamics::GetLODThreshold() const
{
	if(CVarLODThreshold.GetValueOnAnyThread() != -1)
	{
		if(LODThreshold != -1)
		{
			return FMath::Min(LODThreshold, CVarLODThreshold.GetValueOnAnyThread());
		}
		else
		{
			return CVarLODThreshold.GetValueOnAnyThread();
		}
	}
	else
	{
		return LODThreshold;
	}
}

FTransform FAnimNode_AnimDynamics::GetBoneTransformInSimSpace(FComponentSpacePoseContext& Output, const FCompactPoseBoneIndex& BoneIndex)
{
	FTransform Transform = Output.Pose.GetComponentSpaceTransform(BoneIndex);

	return GetSimSpaceTransformFromComponentSpace(SimulationSpace, Output, Transform);
}

FTransform FAnimNode_AnimDynamics::GetComponentSpaceTransformFromSimSpace(AnimPhysSimSpaceType SimSpace, FComponentSpacePoseContext& Output, const FTransform& InSimTransform)
{
	return GetComponentSpaceTransformFromSimSpace(SimSpace, Output, InSimTransform, Output.AnimInstanceProxy->GetComponentTransform(), Output.AnimInstanceProxy->GetActorTransform());
}

FTransform FAnimNode_AnimDynamics::GetComponentSpaceTransformFromSimSpace(AnimPhysSimSpaceType SimSpace, FComponentSpacePoseContext& Output, const FTransform& InSimTransform, const FTransform& InCompWorldSpaceTM, const FTransform& InActorWorldSpaceTM)
{
	FTransform OutTransform = InSimTransform;

	switch(SimSpace)
	{
		// Change nothing, already in component space
	case AnimPhysSimSpaceType::Component:
	{
		break;
	}

	case AnimPhysSimSpaceType::Actor:
	{
		FTransform WorldTransform(OutTransform * InActorWorldSpaceTM);
		WorldTransform.SetToRelativeTransform(InCompWorldSpaceTM);
		OutTransform = WorldTransform;

		break;
	}

	case AnimPhysSimSpaceType::RootRelative:
	{
		const FBoneContainer& RequiredBones = Output.Pose.GetPose().GetBoneContainer();

		FCompactPoseBoneIndex RootBoneCompactIndex(0);

		FTransform RelativeBoneTransform = Output.Pose.GetComponentSpaceTransform(RootBoneCompactIndex);
		OutTransform = OutTransform * RelativeBoneTransform;

		break;
	}

	case AnimPhysSimSpaceType::BoneRelative:
	{
		const FBoneContainer& RequiredBones = Output.Pose.GetPose().GetBoneContainer();
		if(RelativeSpaceBone.IsValidToEvaluate(RequiredBones))
		{
			FTransform RelativeBoneTransform = Output.Pose.GetComponentSpaceTransform(RelativeSpaceBone.GetCompactPoseIndex(RequiredBones));
			OutTransform = OutTransform * RelativeBoneTransform;
		}

		break;
	}
	case AnimPhysSimSpaceType::World:
	{
		OutTransform *= InCompWorldSpaceTM.Inverse();
	}

	default:
		break;
	}

	return OutTransform;
}


FTransform FAnimNode_AnimDynamics::GetSimSpaceTransformFromComponentSpace(AnimPhysSimSpaceType SimSpace, FComponentSpacePoseContext& Output, const FTransform& InComponentTransform)
{
	FTransform ResultTransform = InComponentTransform;

	switch(SimSpace)
	{
		// Change nothing, already in component space
	case AnimPhysSimSpaceType::Component:
	{
		break;
	}

	case AnimPhysSimSpaceType::Actor:
	{
		FTransform WorldTransform = ResultTransform * Output.AnimInstanceProxy->GetComponentTransform();
		WorldTransform.SetToRelativeTransform(Output.AnimInstanceProxy->GetActorTransform());
		ResultTransform = WorldTransform;

		break;
	}

	case AnimPhysSimSpaceType::RootRelative:
	{
		const FBoneContainer& RequiredBones = Output.Pose.GetPose().GetBoneContainer();

		FCompactPoseBoneIndex RootBoneCompactIndex(0);

		FTransform RelativeBoneTransform = Output.Pose.GetComponentSpaceTransform(RootBoneCompactIndex);
		ResultTransform = ResultTransform.GetRelativeTransform(RelativeBoneTransform);

		break;
	}

	case AnimPhysSimSpaceType::BoneRelative:
	{
		const FBoneContainer& RequiredBones = Output.Pose.GetPose().GetBoneContainer();
		if(RelativeSpaceBone.IsValidToEvaluate(RequiredBones))
		{
			FTransform RelativeBoneTransform = Output.Pose.GetComponentSpaceTransform(RelativeSpaceBone.GetCompactPoseIndex(RequiredBones));
			ResultTransform = ResultTransform.GetRelativeTransform(RelativeBoneTransform);
		}

		break;
	}

	case AnimPhysSimSpaceType::World:
	{
		// Out to world space
		ResultTransform *= Output.AnimInstanceProxy->GetComponentTransform();
	}

	default:
		break;
	}

	return ResultTransform;
}

FVector FAnimNode_AnimDynamics::TransformWorldVectorToSimSpace(FComponentSpacePoseContext& Output, const FVector& InVec)
{
	FVector OutVec = InVec;

	switch(SimulationSpace)
	{
	case AnimPhysSimSpaceType::Component:
	{
		OutVec = Output.AnimInstanceProxy->GetComponentTransform().InverseTransformVectorNoScale(OutVec);

		break;
	}

	case AnimPhysSimSpaceType::Actor:
	{
		OutVec = Output.AnimInstanceProxy->GetActorTransform().InverseTransformVectorNoScale(OutVec);

		break;
	}

	case AnimPhysSimSpaceType::RootRelative:
	{
		const FBoneContainer& RequiredBones = Output.Pose.GetPose().GetBoneContainer();

		FCompactPoseBoneIndex RootBoneCompactIndex(0);

		FTransform RelativeBoneTransform = Output.Pose.GetComponentSpaceTransform(RootBoneCompactIndex);
		RelativeBoneTransform = Output.AnimInstanceProxy->GetComponentTransform() * RelativeBoneTransform;
		OutVec = RelativeBoneTransform.InverseTransformVectorNoScale(OutVec);

		break;
	}

	case AnimPhysSimSpaceType::BoneRelative:
	{
		const FBoneContainer& RequiredBones = Output.Pose.GetPose().GetBoneContainer();
		if(RelativeSpaceBone.IsValidToEvaluate(RequiredBones))
		{
			FTransform RelativeBoneTransform = Output.Pose.GetComponentSpaceTransform(RelativeSpaceBone.GetCompactPoseIndex(RequiredBones));
			RelativeBoneTransform = Output.AnimInstanceProxy->GetComponentTransform() * RelativeBoneTransform;
			OutVec = RelativeBoneTransform.InverseTransformVectorNoScale(OutVec);
		}

		break;
	}
	case AnimPhysSimSpaceType::World:
	{
		break;
	}

	default:
		break;
	}

	return OutVec;
}

void FAnimNode_AnimDynamics::ConvertSimulationSpace(FComponentSpacePoseContext& Output, AnimPhysSimSpaceType From, AnimPhysSimSpaceType To)
{
	for(FAnimPhysRigidBody* Body : BaseBodyPtrs)
	{
		if(!Body)
		{
			return;
		}

		// Get transform
		FTransform BodyTransform(Body->Pose.Orientation, Body->Pose.Position);
		// Out to component space
		BodyTransform = GetComponentSpaceTransformFromSimSpace(LastSimSpace, Output, BodyTransform);
		// In to new space
		BodyTransform = GetSimSpaceTransformFromComponentSpace(SimulationSpace, Output, BodyTransform);

		// Push back to body
		Body->Pose.Orientation = BodyTransform.GetRotation();
		Body->Pose.Position = BodyTransform.GetTranslation();
	}
}