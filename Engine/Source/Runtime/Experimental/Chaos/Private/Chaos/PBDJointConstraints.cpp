// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/ChaosDebugDraw.h"
#include "Chaos/DebugDrawQueue.h"
#include "Chaos/Joint/ChaosJointLog.h"
#include "Chaos/Joint/PBDJointSolverGaussSeidel.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDJointConstraintUtilities.h"
#include "Chaos/Utilities.h"
#include "ChaosLog.h"
#include "ChaosStats.h"

#include "HAL/IConsoleManager.h"

//PRAGMA_DISABLE_OPTIMIZATION

bool bChaos_Joint_EarlyOut_Enabled = true;
FAutoConsoleVariableRef CVarChaosJointEarlyOutEnabled(TEXT("p.Chaos.Joint.EarlyOut"), bChaos_Joint_EarlyOut_Enabled, TEXT("Whether to iterating when joints report being solved"));

namespace Chaos
{
	DECLARE_CYCLE_STAT(TEXT("Joints::Sort"), STAT_Joints_Sort, STATGROUP_ChaosJoint);
	DECLARE_CYCLE_STAT(TEXT("Joints::Apply"), STAT_Joints_Apply, STATGROUP_ChaosJoint);
	DECLARE_CYCLE_STAT(TEXT("Joints::ApplyPushOut"), STAT_Joints_ApplyPushOut, STATGROUP_ChaosJoint);

	//
	// Constraint Handle
	//

	
	FPBDJointConstraintHandle::FPBDJointConstraintHandle()
	{
	}

	
	FPBDJointConstraintHandle::FPBDJointConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex)
		: TContainerConstraintHandle<FPBDJointConstraints>(InConstraintContainer, InConstraintIndex)
	{
	}

	
	void FPBDJointConstraintHandle::CalculateConstraintSpace(FVec3& OutXa, FMatrix33& OutRa, FVec3& OutXb, FMatrix33& OutRb) const
	{
		ConstraintContainer->CalculateConstraintSpace(ConstraintIndex, OutXa, OutRa, OutXb, OutRb);
	}

	
	void FPBDJointConstraintHandle::SetParticleLevels(const TVector<int32, 2>& ParticleLevels)
	{
		ConstraintContainer->SetParticleLevels(ConstraintIndex, ParticleLevels);
	}

	
	int32 FPBDJointConstraintHandle::GetConstraintLevel() const
	{
		return ConstraintContainer->GetConstraintLevel(ConstraintIndex);
	}

	
	const FPBDJointSettings& FPBDJointConstraintHandle::GetSettings() const
	{
		return ConstraintContainer->GetConstraintSettings(ConstraintIndex);
	}

	void FPBDJointConstraintHandle::SetSettings(const FPBDJointSettings& Settings)
	{
		ConstraintContainer->SetConstraintSettings(ConstraintIndex, Settings);
	}

	TVector<TGeometryParticleHandle<float,3>*, 2> FPBDJointConstraintHandle::GetConstrainedParticles() const 
	{ 
		return ConstraintContainer->GetConstrainedParticles(ConstraintIndex); 
	}

	//
	// Constraint Settings
	//

	
	FPBDJointSettings::FPBDJointSettings()
		: Stiffness(1)
		, LinearProjection(0)
		, AngularProjection(0)
		, ParentInvMassScale(1)
		, LinearMotionTypes({ EJointMotionType::Locked, EJointMotionType::Locked, EJointMotionType::Locked })
		, LinearLimit(FLT_MAX)
		, AngularMotionTypes({ EJointMotionType::Free, EJointMotionType::Free, EJointMotionType::Free })
		, AngularLimits(FVec3(FLT_MAX, FLT_MAX, FLT_MAX))
		, bSoftLinearLimitsEnabled(false)
		, bSoftTwistLimitsEnabled(false)
		, bSoftSwingLimitsEnabled(false)
		, LinearSoftForceMode(EJointForceMode::Acceleration)
		, AngularSoftForceMode(EJointForceMode::Acceleration)
		, SoftLinearStiffness(0)
		, SoftLinearDamping(0)
		, SoftTwistStiffness(0)
		, SoftTwistDamping(0)
		, SoftSwingStiffness(0)
		, SoftSwingDamping(0)
		, LinearDriveTarget(FVec3(0, 0, 0))
		, bLinearPositionDriveEnabled(TVector<bool, 3>(false, false, false))
		, bLinearVelocityDriveEnabled(TVector<bool, 3>(false, false, false))
		, LinearDriveForceMode(EJointForceMode::Acceleration)
		, LinearDriveStiffness(0)
		, LinearDriveDamping(0)
		, AngularDrivePositionTarget(FRotation3::FromIdentity())
		, AngularDriveTargetAngles(FVec3(0, 0, 0))
		, AngularDriveVelocityTarget(FVec3(0, 0, 0))
		, bAngularSLerpPositionDriveEnabled(false)
		, bAngularSLerpVelocityDriveEnabled(false)
		, bAngularTwistPositionDriveEnabled(false)
		, bAngularTwistVelocityDriveEnabled(false)
		, bAngularSwingPositionDriveEnabled(false)
		, bAngularSwingVelocityDriveEnabled(false)
		, AngularDriveForceMode(EJointForceMode::Acceleration)
		, AngularDriveStiffness(0)
		, AngularDriveDamping(0)
	{
	}


	void FPBDJointSettings::Sanitize()
	{
		// Reset limits if they won't be used (means we don't have to check if limited/locked in a few cases).
		// A side effect: if we enable a constraint, we need to reset the value of the limit.
		if ((LinearMotionTypes[0] != EJointMotionType::Limited) && (LinearMotionTypes[1] != EJointMotionType::Limited) && (LinearMotionTypes[2] != EJointMotionType::Limited))
		{
			LinearLimit = 0;
		}
		if (AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist] != EJointMotionType::Limited)
		{
			AngularLimits[(int32)EJointAngularConstraintIndex::Twist] = 0;
		}
		if (AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing1] != EJointMotionType::Limited)
		{
			AngularLimits[(int32)EJointAngularConstraintIndex::Swing1] = 0;
		}
		if (AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing2] != EJointMotionType::Limited)
		{
			AngularLimits[(int32)EJointAngularConstraintIndex::Swing2] = 0;
		}
	}

	
	FPBDJointState::FPBDJointState()
		: Level(INDEX_NONE)
		, ParticleLevels({ INDEX_NONE, INDEX_NONE })
	{
	}

	//
	// Solver Settings
	//

	
	FPBDJointSolverSettings::FPBDJointSolverSettings()
		: ApplyPairIterations(1)
		, ApplyPushOutPairIterations(1)
		, SwingTwistAngleTolerance(1.0e-6f)
		, PositionTolerance(0)
		, AngleTolerance(0)
		, MinParentMassRatio(0)
		, MaxInertiaRatio(0)
		, AngularConstraintPositionCorrection(1.0f)
		, bEnableTwistLimits(true)
		, bEnableSwingLimits(true)
		, bEnableDrives(true)
		, LinearProjection(0)
		, AngularProjection(0)
		, Stiffness(0)
		, LinearDriveStiffness(0)
		, LinearDriveDamping(0)
		, AngularDriveStiffness(0)
		, AngularDriveDamping(0)
		, SoftLinearStiffness(0)
		, SoftLinearDamping(0)
		, SoftTwistStiffness(0)
		, SoftTwistDamping(0)
		, SoftSwingStiffness(0)
		, SoftSwingDamping(0)
	{
	}

	//
	// Constraint Container
	//

	
	FPBDJointConstraints::FPBDJointConstraints(const FPBDJointSolverSettings& InSettings)
		: Settings(InSettings)
		, bRequiresSort(false)
		, PreApplyCallback(nullptr)
		, PostApplyCallback(nullptr)
	{
	}

	
	FPBDJointConstraints::~FPBDJointConstraints()
	{
	}

	
	const FPBDJointSolverSettings& FPBDJointConstraints::GetSettings() const
	{
		return Settings;
	}

	
	void FPBDJointConstraints::SetSettings(const FPBDJointSolverSettings& InSettings)
	{
		Settings = InSettings;
	}


	int32 FPBDJointConstraints::NumConstraints() const
	{
		return ConstraintParticles.Num();
	}

	void FPBDJointConstraints::GetConstrainedParticleIndices(const int32 ConstraintIndex, int32& Index0, int32& Index1) const
	{
		// In solvers we assume Particle0 is the parent particle (which it usually is as implemented in the editor). 
		// However, it is possible to set it up so that the kinematic particle is the child which we don't support, so...
		// If particle 0 is kinematic we make it the parent, otherwise particle 1 is the parent.
		// @todo(ccaulfield): look into this and confirm/fix properly
		if (!ConstraintParticles[ConstraintIndex][0]->CastToRigidParticle())
		{
			Index0 = 0;
			Index1 = 1;
		}
		else
		{
			Index0 = 1;
			Index1 = 0;
		}
	}

	typename FPBDJointConstraints::FConstraintContainerHandle* FPBDJointConstraints::AddConstraint(const FParticlePair& InConstrainedParticles, const FRigidTransform3& WorldConstraintFrame)
	{
		FTransformPair JointFrames;
		JointFrames[0] = FRigidTransform3(
			WorldConstraintFrame.GetTranslation() - InConstrainedParticles[0]->X(),
			WorldConstraintFrame.GetRotation() * InConstrainedParticles[0]->R().Inverse()
			);
		JointFrames[1] = FRigidTransform3(
			WorldConstraintFrame.GetTranslation() - InConstrainedParticles[1]->X(),
			WorldConstraintFrame.GetRotation() * InConstrainedParticles[1]->R().Inverse()
			);
		return AddConstraint(InConstrainedParticles, JointFrames, FPBDJointSettings());
	}

	
	typename FPBDJointConstraints::FConstraintContainerHandle* FPBDJointConstraints::AddConstraint(const FParticlePair& InConstrainedParticles, const FTransformPair& InConstraintFrames)
	{
		return AddConstraint(InConstrainedParticles, InConstraintFrames, FPBDJointSettings());
	}

	
	typename FPBDJointConstraints::FConstraintContainerHandle* FPBDJointConstraints::AddConstraint(const FParticlePair& InConstrainedParticles, const FTransformPair& InConstraintFrames, const FPBDJointSettings& InConstraintSettings)
	{
		int ConstraintIndex = Handles.Num();
		Handles.Add(HandleAllocator.AllocHandle(this, ConstraintIndex));
		ConstraintParticles.Add(InConstrainedParticles);
		ConstraintSettings.Add(InConstraintSettings);
		ConstraintFrames.Add(InConstraintFrames);
		ConstraintStates.Add(FPBDJointState());
		return Handles.Last();
	}

	
	void FPBDJointConstraints::RemoveConstraint(int ConstraintIndex)
	{
		FConstraintContainerHandle* ConstraintHandle = Handles[ConstraintIndex];
		if (ConstraintHandle != nullptr)
		{
			// Release the handle for the freed constraint
			HandleAllocator.FreeHandle(ConstraintHandle);
			Handles[ConstraintIndex] = nullptr;
		}

		// Swap the last constraint into the gap to keep the array packed
		ConstraintParticles.RemoveAtSwap(ConstraintIndex);
		ConstraintSettings.RemoveAtSwap(ConstraintIndex);
		ConstraintFrames.RemoveAtSwap(ConstraintIndex);
		ConstraintStates.RemoveAtSwap(ConstraintIndex);
		Handles.RemoveAtSwap(ConstraintIndex);

		// Update the handle for the constraint that was moved
		if (ConstraintIndex < Handles.Num())
		{
			SetConstraintIndex(Handles[ConstraintIndex], ConstraintIndex);
		}
	}

	
	void FPBDJointConstraints::RemoveConstraints(const TSet<TGeometryParticleHandle<FReal, 3>*>& RemovedParticles)
	{
	}


	void FPBDJointConstraints::SortConstraints()
	{
		// Sort constraints so that constraints with lower level (closer to a kinematic joint) are first
		// @todo(ccaulfield): should probably also take islands/particle order into account
		// @todo(ccaulfield): optimize (though isn't called very often)
		SCOPE_CYCLE_COUNTER(STAT_Joints_Sort);

		FHandles SortedHandles = Handles;
		SortedHandles.StableSort([](const FConstraintContainerHandle& L, const FConstraintContainerHandle& R)
			{
				return L.GetConstraintLevel() < R.GetConstraintLevel();
			});

		TArray<FPBDJointSettings> SortedConstraintSettings;
		TArray<FTransformPair> SortedConstraintFrames;
		TArray<FParticlePair> SortedConstraintParticles;
		TArray<FPBDJointState> SortedConstraintStates;
		int32 SortedConstraintIndex = 0;
		for (FConstraintContainerHandle* Handle : SortedHandles)
		{
			int32 UnsortedIndex = Handle->GetConstraintIndex();
			SortedConstraintSettings.Add(ConstraintSettings[UnsortedIndex]);
			SortedConstraintFrames.Add(ConstraintFrames[UnsortedIndex]);
			SortedConstraintParticles.Add(ConstraintParticles[UnsortedIndex]);
			SortedConstraintStates.Add(ConstraintStates[UnsortedIndex]);
			SetConstraintIndex(Handle, SortedConstraintIndex++);
		}

		Swap(ConstraintSettings, SortedConstraintSettings);
		Swap(ConstraintFrames, SortedConstraintFrames);
		Swap(ConstraintParticles, SortedConstraintParticles);
		Swap(ConstraintStates, SortedConstraintStates);
		Swap(Handles, SortedHandles);
	}


	
	void FPBDJointConstraints::SetPreApplyCallback(const FJointPreApplyCallback& Callback)
	{
		PreApplyCallback = Callback;
	}

	
	void FPBDJointConstraints::ClearPreApplyCallback()
	{
		PreApplyCallback = nullptr;
	}


	void FPBDJointConstraints::SetPostApplyCallback(const FJointPostApplyCallback& Callback)
	{
		PostApplyCallback = Callback;
	}


	void FPBDJointConstraints::ClearPostApplyCallback()
	{
		PostApplyCallback = nullptr;
	}


	void FPBDJointConstraints::SetPostProjectCallback(const FJointPostApplyCallback& Callback)
	{
		PostProjectCallback = Callback;
	}


	void FPBDJointConstraints::ClearPostProjectCallback()
	{
		PostProjectCallback = nullptr;
	}

	
	const typename FPBDJointConstraints::FConstraintContainerHandle* FPBDJointConstraints::GetConstraintHandle(int32 ConstraintIndex) const
	{
		return Handles[ConstraintIndex];
	}

	
	typename FPBDJointConstraints::FConstraintContainerHandle* FPBDJointConstraints::GetConstraintHandle(int32 ConstraintIndex)
	{
		return Handles[ConstraintIndex];
	}

	
	const typename FPBDJointConstraints::FParticlePair& FPBDJointConstraints::GetConstrainedParticles(int32 ConstraintIndex) const
	{
		return ConstraintParticles[ConstraintIndex];
	}

	
	const FPBDJointSettings& FPBDJointConstraints::GetConstraintSettings(int32 ConstraintIndex) const
	{
		return ConstraintSettings[ConstraintIndex];
	}


	void FPBDJointConstraints::SetConstraintSettings(int32 ConstraintIndex, const FPBDJointSettings& InConstraintSettings)
	{
		ConstraintSettings[ConstraintIndex] = InConstraintSettings;
	}

	
	int32 FPBDJointConstraints::GetConstraintLevel(int32 ConstraintIndex) const
	{
		return ConstraintStates[ConstraintIndex].Level;
	}

	
	void FPBDJointConstraints::SetParticleLevels(int32 ConstraintIndex, const TVector<int32, 2>& ParticleLevels)
	{
		int32 NewLevel = FMath::Min(ParticleLevels[0], ParticleLevels[1]);
		int32 PreviousLevel = ConstraintStates[ConstraintIndex].Level;
		ConstraintStates[ConstraintIndex].Level = NewLevel;
		ConstraintStates[ConstraintIndex].ParticleLevels = ParticleLevels;
		bRequiresSort = bRequiresSort || (NewLevel != PreviousLevel);
	}
	

	void FPBDJointConstraints::UpdatePositionBasedState(const FReal Dt)
	{
		if (bRequiresSort)
		{
			SortConstraints();
			bRequiresSort = false;
		}
	}


	void FPBDJointConstraints::PrepareConstraints(FReal Dt)
	{
		ConstraintSolvers.SetNum(NumConstraints());
		for (int32 ConstraintIndex = 0; ConstraintIndex < NumConstraints(); ++ConstraintIndex)
		{
			const FPBDJointSettings& JointSettings = ConstraintSettings[ConstraintIndex];
			const FTransformPair& JointFrames = ConstraintFrames[ConstraintIndex];
			FJointSolverGaussSeidel& Solver = ConstraintSolvers[ConstraintIndex];

			int32 Index0, Index1;
			GetConstrainedParticleIndices(ConstraintIndex, Index0, Index1);
			TGenericParticleHandle<FReal, 3> Particle0 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[ConstraintIndex][Index0]);
			TGenericParticleHandle<FReal, 3> Particle1 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[ConstraintIndex][Index1]);

			Solver.Init(
				Dt,
				Settings,
				JointSettings,
				FParticleUtilitiesXR::GetCoMWorldPosition(Particle0),	// Prev position
				FParticleUtilitiesXR::GetCoMWorldPosition(Particle1),	// Prev position
				FParticleUtilitiesXR::GetCoMWorldRotation(Particle0),	// Prev rotation
				FParticleUtilitiesXR::GetCoMWorldRotation(Particle1),	// Prev rotation
				Particle0->InvM(),
				Particle0->InvI().GetDiagonal(),
				Particle1->InvM(),
				Particle1->InvI().GetDiagonal(),
				FParticleUtilities::ParticleLocalToCoMLocal(Particle0, JointFrames[Index0]),
				FParticleUtilities::ParticleLocalToCoMLocal(Particle1, JointFrames[Index1]));
		}
	}


	void FPBDJointConstraints::UnprepareConstraints(FReal Dt)
	{
		ConstraintSolvers.Empty();
	}

	
	void FPBDJointConstraints::CalculateConstraintSpace(int32 ConstraintIndex, FVec3& OutX0, FMatrix33& OutR0, FVec3& OutX1, FMatrix33& OutR1) const
	{
		int32 Index0, Index1;
		GetConstrainedParticleIndices(ConstraintIndex, Index0, Index1);
		TGenericParticleHandle<FReal, 3> Particle0 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[ConstraintIndex][Index0]);
		TGenericParticleHandle<FReal, 3> Particle1 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[ConstraintIndex][Index1]);
		const FVec3 P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
		const FRotation3 Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
		const FVec3 P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);
		const FRotation3 Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);
		const FRigidTransform3& XL0 = FParticleUtilities::ParticleLocalToCoMLocal(Particle0, ConstraintFrames[ConstraintIndex][Index0]);
		const FRigidTransform3& XL1 = FParticleUtilities::ParticleLocalToCoMLocal(Particle1, ConstraintFrames[ConstraintIndex][Index1]);

		OutX0 = P0 + Q0 * XL0.GetTranslation();
		OutX1 = P1 + Q1 * XL1.GetTranslation();
		OutR0 = FRotation3(Q0 * XL0.GetRotation()).ToMatrix();
		OutR1 = FRotation3(Q1 * XL1.GetRotation()).ToMatrix();
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// Begin Simple API Solver. Iterate over constraints in array order.
	//
	//////////////////////////////////////////////////////////////////////////

	bool FPBDJointConstraints::Apply(const FReal Dt, const int32 It, const int32 NumIts)
	{
		SCOPE_CYCLE_COUNTER(STAT_Joints_Apply);

		if (PreApplyCallback != nullptr)
		{
			PreApplyCallback(Dt, Handles);
		}

		FJointSolverResult NetResult;
		if (Settings.ApplyPairIterations > 0)
		{
			for (int32 ConstraintIndex = 0; ConstraintIndex < NumConstraints(); ++ConstraintIndex)
			{
				NetResult += SolvePosition_GaussSiedel(Dt, ConstraintIndex, Settings.ApplyPairIterations, It, NumIts);
			}
		}

		UE_LOG(LogChaosJoint, Verbose, TEXT("Apply Iteration: %d / %d; Active: %d / %d"), It, NumIts, NetResult.GetNumActive(), NetResult.GetNumActive() + NetResult.GetNumSolved());

		if (PostApplyCallback != nullptr)
		{
			PostApplyCallback(Dt, Handles);
		}

		return (NetResult.GetNumActive() > 0);
	}

	bool FPBDJointConstraints::ApplyPushOut(const FReal Dt, const int32 It, const int32 NumIts)
	{
		SCOPE_CYCLE_COUNTER(STAT_Joints_ApplyPushOut);

		FJointSolverResult NetResult;
		if (Settings.ApplyPushOutPairIterations > 0)
		{
			for (int32 ConstraintIndex = 0; ConstraintIndex < NumConstraints(); ++ConstraintIndex)
			{
				NetResult += ProjectPosition_GaussSiedel(Dt, ConstraintIndex, Settings.ApplyPushOutPairIterations, It, NumIts);
			}
		}

		UE_LOG(LogChaosJoint, Verbose, TEXT("PushOut Iteration: %d / %d; Active: %d / %d"), It, NumIts, NetResult.GetNumActive(), NetResult.GetNumActive() + NetResult.GetNumSolved());

		if (PostProjectCallback != nullptr)
		{
			PostProjectCallback(Dt, Handles);
		}

		return (NetResult.GetNumActive() > 0);
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// End Simple API Solver.
	//
	//////////////////////////////////////////////////////////////////////////

	//////////////////////////////////////////////////////////////////////////
	//
	// Begin Graph API Solver. Iterate over constraints in connectivity order.
	//
	//////////////////////////////////////////////////////////////////////////

	bool FPBDJointConstraints::Apply(const FReal Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts)
	{
		SCOPE_CYCLE_COUNTER(STAT_Joints_Apply);

		// @todo(ccaulfield): make sorting optional
		// @todo(ccaulfield): handles should be sorted by level by the constraint rule/graph
		// @todo(ccaulfield): the best sort order depends on whether we are freezing.
		// If we are freezing we want the root-most (nearest to kinematic) bodies solved first.
		// For normal update we want the root body last, otherwise it gets dragged away from the root by the other bodies

		TArray<FConstraintContainerHandle*> SortedConstraintHandles = InConstraintHandles;
		SortedConstraintHandles.Sort([](const FConstraintContainerHandle& L, const FConstraintContainerHandle& R)
			{
				// Sort bodies from root to leaf
				return L.GetConstraintLevel() < R.GetConstraintLevel();
			});

		if (PreApplyCallback != nullptr)
		{
			PreApplyCallback(Dt, SortedConstraintHandles);
		}


		FJointSolverResult NetResult;
		if (Settings.ApplyPairIterations > 0)
		{
			for (FConstraintContainerHandle* ConstraintHandle : SortedConstraintHandles)
			{
				NetResult += SolvePosition_GaussSiedel(Dt, ConstraintHandle->GetConstraintIndex(), Settings.ApplyPairIterations, It, NumIts);
			}
		}

		if (PostApplyCallback != nullptr)
		{
			PostApplyCallback(Dt, SortedConstraintHandles);
		}

		return (NetResult.GetNumActive() > 0);
	}

	
	bool FPBDJointConstraints::ApplyPushOut(const FReal Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts)
	{
		SCOPE_CYCLE_COUNTER(STAT_Joints_ApplyPushOut);

		TArray<FConstraintContainerHandle*> SortedConstraintHandles = InConstraintHandles;
		SortedConstraintHandles.Sort([](const FConstraintContainerHandle& L, const FConstraintContainerHandle& R)
			{
				// Sort bodies from root to leaf
				return L.GetConstraintLevel() < R.GetConstraintLevel();
			});

		FJointSolverResult NetResult;
		if (Settings.ApplyPushOutPairIterations > 0)
		{
			for (FConstraintContainerHandle* ConstraintHandle : SortedConstraintHandles)
			{
				NetResult += ProjectPosition_GaussSiedel(Dt, ConstraintHandle->GetConstraintIndex(), Settings.ApplyPushOutPairIterations, It, NumIts);
			}
		}

		if (PostProjectCallback != nullptr)
		{
			PostProjectCallback(Dt, SortedConstraintHandles);
		}

		return (NetResult.GetNumActive() > 0);
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// End Graph API Solver.
	//
	//////////////////////////////////////////////////////////////////////////


	//////////////////////////////////////////////////////////////////////////
	//
	// Begin single-particle solve methods used by APIs
	//
	//////////////////////////////////////////////////////////////////////////

	void FPBDJointConstraints::UpdateParticleState(TPBDRigidParticleHandle<FReal, 3>* Rigid, const FReal Dt, const FVec3& PrevP, const FRotation3& PrevQ, const FVec3& P, const FRotation3& Q, const bool bUpdateVelocity)
	{
		if ((Rigid != nullptr) && (Rigid->ObjectState() == EObjectStateType::Dynamic))
		{
			FParticleUtilities::SetCoMWorldTransform(Rigid, P, Q);
			if (bUpdateVelocity && (Dt > SMALL_NUMBER))
			{
				const FVec3 DV = FVec3::CalculateVelocity(PrevP, P, Dt);
				const FVec3 DW = FRotation3::CalculateAngularVelocity(PrevQ, Q, Dt);
				Rigid->SetV(Rigid->V() + DV);
				Rigid->SetW(Rigid->W() + DW);
			}
		}
	}


	void FPBDJointConstraints::UpdateParticleStateExplicit(TPBDRigidParticleHandle<FReal, 3>* Rigid, const FReal Dt, const FVec3& P, const FRotation3& Q, const FVec3& V, const FVec3& W)
	{
		if ((Rigid != nullptr) && (Rigid->ObjectState() == EObjectStateType::Dynamic))
		{
			FParticleUtilities::SetCoMWorldTransform(Rigid, P, Q);
			Rigid->SetV(V);
			Rigid->SetW(W);
		}
	}


	// This position solver iterates over each of the inner constraints (position, twist, swing) and solves them independently.
	// This will converge slowly in some cases, particularly where resolving angular constraints violates position constraints and vice versa.
	FJointSolverResult FPBDJointConstraints::SolvePosition_GaussSiedel(const FReal Dt, const int32 ConstraintIndex, const int32 NumPairIts, const int32 It, const int32 NumIts)
	{
		const TVector<TGeometryParticleHandle<FReal, 3>*, 2>& Constraint = ConstraintParticles[ConstraintIndex];
		UE_LOG(LogChaosJoint, VeryVerbose, TEXT("Solve Joint Constraint %d %s %s (dt = %f; it = %d / %d)"), ConstraintIndex, *Constraint[0]->ToString(), *Constraint[1]->ToString(), Dt, It, NumIts);

		const FPBDJointSettings& JointSettings = ConstraintSettings[ConstraintIndex];
		FJointSolverGaussSeidel& Solver = ConstraintSolvers[ConstraintIndex];

		int32 Index0, Index1;
		GetConstrainedParticleIndices(ConstraintIndex, Index0, Index1);
		TGenericParticleHandle<FReal, 3> Particle0 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[ConstraintIndex][Index0]);
		TGenericParticleHandle<FReal, 3> Particle1 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[ConstraintIndex][Index1]);

		const FVec3 P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
		const FRotation3 Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
		const FVec3 P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);
		const FRotation3 Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);

		Solver.Update(
			Dt,
			P0,
			Q0,
			Particle0->V(),
			Particle0->W(),
			P1,
			Q1,
			Particle1->V(),
			Particle1->W());

		FJointSolverResult NetResult;
		for (int32 PairIt = 0; PairIt < NumPairIts; ++PairIt)
		{
			UE_LOG(LogChaosJoint, VeryVerbose, TEXT("  Pair Iteration %d / %d"), PairIt, NumIts);

			NetResult += Solver.ApplyConstraints(Dt, Settings, JointSettings);
			NetResult +=  Solver.ApplyDrives(Dt, Settings, JointSettings);

			if ((NetResult.GetNumActive() == 0) && bChaos_Joint_EarlyOut_Enabled)
			{
				break;
			}
		}

		UpdateParticleState(Particle0->CastToRigidParticle(), Dt, P0, Q0, Solver.GetP(0), Solver.GetQ(0));
		UpdateParticleState(Particle1->CastToRigidParticle(), Dt, P1, Q1, Solver.GetP(1), Solver.GetQ(1));

		return NetResult;
	}

	FJointSolverResult FPBDJointConstraints::ProjectPosition_GaussSiedel(const FReal Dt, const int32 ConstraintIndex, const int32 NumPairIts, const int32 It, const int32 NumIts)
	{
		const TVector<TGeometryParticleHandle<FReal, 3>*, 2>& Constraint = ConstraintParticles[ConstraintIndex];
		UE_LOG(LogChaosJoint, VeryVerbose, TEXT("Project Joint Constraint %d %s %s (dt = %f; it = %d / %d)"), ConstraintIndex, *Constraint[0]->ToString(), *Constraint[1]->ToString(), Dt, It, NumIts);

		const FPBDJointSettings& JointSettings = ConstraintSettings[ConstraintIndex];
		FJointSolverGaussSeidel& Solver = ConstraintSolvers[ConstraintIndex];

		int32 Index0, Index1;
		GetConstrainedParticleIndices(ConstraintIndex, Index0, Index1);
		TGenericParticleHandle<FReal, 3> Particle0 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[ConstraintIndex][Index0]);
		TGenericParticleHandle<FReal, 3> Particle1 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[ConstraintIndex][Index1]);

		Solver.Update(
			Dt,
			FParticleUtilities::GetCoMWorldPosition(Particle0),
			FParticleUtilities::GetCoMWorldRotation(Particle0),
			Particle0->V(),
			Particle0->W(),
			FParticleUtilities::GetCoMWorldPosition(Particle1),
			FParticleUtilities::GetCoMWorldRotation(Particle1),
			Particle1->V(),
			Particle1->W());

		FJointSolverResult NetResult;
		for (int32 PairIt = 0; PairIt < NumPairIts; ++PairIt)
		{
			NetResult = Solver.ApplyProjections(Dt, Settings, JointSettings);
			
			if ((NetResult.GetNumActive() == 0) && bChaos_Joint_EarlyOut_Enabled)
			{
				break;
			}
		}

		UpdateParticleStateExplicit(Particle0->CastToRigidParticle(), Dt, Solver.GetP(0), Solver.GetQ(0), Solver.GetV(0), Solver.GetW(0));
		UpdateParticleStateExplicit(Particle1->CastToRigidParticle(), Dt, Solver.GetP(1), Solver.GetQ(1), Solver.GetV(1), Solver.GetW(1));

		return NetResult;
	}
}

namespace Chaos
{
	template class TContainerConstraintHandle<FPBDJointConstraints>;
}
