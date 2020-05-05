// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Chaos/DenseMatrix.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/PBDJointConstraintTypes.h"
#include "Chaos/PBDJointConstraintUtilities.h"
#include "Chaos/Utilities.h"

namespace Chaos
{
	struct FJointSolverResult
	{
	public:
		static FJointSolverResult MakeActive() { return { 1, 0 }; }
		static FJointSolverResult MakeSolved() { return { 0, 1 }; }

		FJointSolverResult()
			: NumActive(0)
			, NumSolved(0)
		{
		}
		
		int32 GetNumActive() const { return NumActive; }
		int32 GetNumSolved() const { return NumSolved; }
		
		FJointSolverResult& operator+=(const FJointSolverResult& R)
		{
			NumActive += R.NumActive;
			NumSolved += R.NumSolved;
			return *this;
		}

		friend FJointSolverResult operator+(const FJointSolverResult& L, const FJointSolverResult& R)
		{
			return { L.NumActive + R.NumActive, L.NumSolved + R.NumSolved };
		}

	private:
		FJointSolverResult(int32 InActive, int32 InSolved)
			: NumActive(InActive)
			, NumSolved(InSolved)
		{
		}

		int32 NumActive;
		int32 NumSolved;
	};

	/**
	 * Calculate new positions and rotations for a pair of bodies connected by a joint.
	 *
	 * This solver treats of the 6 possible constraints (up to 3 linear and 3 angular)
	 * individually and resolves them in sequence.
	 *
	 * \see FJointSolverCholesky
	 */
	class FJointSolverGaussSeidel
	{
	public:
		static const int32 MaxConstrainedBodies = 2;

		FORCEINLINE const FVec3& GetP(const int32 Index) const
		{
			checkSlow(Index < MaxConstrainedBodies);
			return Ps[Index];
		}

		FORCEINLINE const FRotation3& GetQ(const int32 Index) const
		{
			checkSlow(Index < MaxConstrainedBodies);
			return Qs[Index];
		}

		FORCEINLINE const FVec3& GetV(const int32 Index) const
		{
			checkSlow(Index < MaxConstrainedBodies);
			return Vs[Index];
		}

		FORCEINLINE const FVec3& GetW(const int32 Index) const
		{
			checkSlow(Index < MaxConstrainedBodies);
			return Ws[Index];
		}

		FJointSolverGaussSeidel();

		void Init(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const FVec3& PrevP0,
			const FVec3& PrevP1,
			const FRotation3& PrevQ0,
			const FRotation3& PrevQ1,
			const FReal InvM0,
			const FVec3& InvIL0,
			const FReal InvM1,
			const FVec3& InvIL1,
			const FRigidTransform3& XL0,
			const FRigidTransform3& XL1);

		void Update(
			const FReal Dt,
			const FVec3& P0,
			const FRotation3& Q0,
			const FVec3& V0,
			const FVec3& W0,
			const FVec3& P1,
			const FRotation3& Q1,
			const FVec3& V1,
			const FVec3& W1);

		FJointSolverResult ApplyConstraints(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		FJointSolverResult ApplyDrives(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		FJointSolverResult ApplyProjections(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

	private:

		void UpdateDerivedState(const int32 BodyIndex);
		void UpdateDerivedState();

		FJointSolverResult ApplyPositionConstraints(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		FJointSolverResult ApplyRotationConstraints(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		FJointSolverResult ApplyPositionDrives(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		FJointSolverResult ApplyRotationDrives(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		FJointSolverResult ApplyRotationProjection(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);


		void ApplyPositionDelta(
			const int32 BodyIndex,
			const FReal Stiffness,
			const FVec3& DP);

		void ApplyPositionDelta(
			const FReal Stiffness,
			const FVec3& DP0,
			const FVec3& DP1);

		void ApplyRotationDelta(
			const int32 BodyIndex,
			const FReal Stiffness,
			const FVec3& DR);

		void ApplyRotationDelta(
			const FReal Stiffness,
			const FVec3& DR0,
			const FVec3& DR1);

		void ApplyDelta(
			const int32 BodyIndex,
			const FReal Stiffness,
			const FVec3& DP,
			const FVec3& DR1);

		void ApplyVelocityDelta(
			const int32 BodyIndex,
			const FReal Stiffness,
			const FVec3& DV,
			const FVec3& DW);

		void ApplyVelocityDelta(
			const FReal Stiffness,
			const FVec3& DV0,
			const FVec3& DW0,
			const FVec3& DV1,
			const FVec3& DW1);

		void ApplyPositionConstraint(
			const FReal Stiffness,
			const FVec3& Axis,
			const FReal Delta);

		void ApplyPositionConstraintSoft(
			const FReal Dt,
			const FReal Stiffness,
			const FReal Damping,
			const bool bAccelerationMode,
			const FVec3& Axis,
			const FReal Delta,
			FReal& Lambda);

		void ApplyRotationConstraint(
			const FReal Stiffness,
			const FVec3& Axis,
			const FReal Angle);

		void ApplyRotationConstraintSoft(
			const FReal Dt,
			const FReal Stiffness,
			const FReal Damping,
			const bool bAccelerationMode,
			const FVec3& Axis,
			const FReal Angle,
			FReal& Lambda);

		void ApplyRotationConstraintSoftKD(
			const int32 KIndex,
			const int32 DIndex,
			const FReal Dt,
			const FReal Stiffness,
			const FReal Damping,
			const bool bAccelerationMode,
			const FVec3& Axis,
			const FReal Angle,
			FReal& Lambda);

		void ApplyRotationConstraintSoftDD(
			const FReal Dt,
			const FReal Stiffness,
			const FReal Damping,
			const bool bAccelerationMode,
			const FVec3& Axis,
			const FReal Angle,
			FReal& Lambda);

		FJointSolverResult ApplyLockedRotationConstraints(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const bool bApplyTwist,
			const bool bApplySwing);

		FJointSolverResult ApplyTwistConstraint(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const bool bUseSoftLimit);

		FJointSolverResult ApplyTwistDrive(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		FJointSolverResult ApplyTwistProjection(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		FJointSolverResult ApplyConeConstraint(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const bool bUseSoftLimit);

		FJointSolverResult ApplyConeDrive(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		FJointSolverResult ApplyConeProjection(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		// One Swing axis is free, and the other locked. This applies the lock: Body1 Twist axis is confined to a plane.
		FJointSolverResult ApplySingleLockedSwingConstraint(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const EJointAngularConstraintIndex SwingConstraintIndex,
			const bool bUseSoftLimit);

		// One Swing axis is free, and the other limited. This applies the limit: Body1 Twist axis is confined to space between two cones.
		FJointSolverResult ApplyDualConeSwingConstraint(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const EJointAngularConstraintIndex SwingConstraintIndex,
			const bool bUseSoftLimit);

		// One swing axis is locked, the other limited or locked. This applies the Limited axis (ApplyDualConeSwingConstraint is used for the locked axis).
		FJointSolverResult ApplySwingConstraint(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const EJointAngularConstraintIndex SwingConstraintIndex,
			const bool bUseSoftLimit);

		FJointSolverResult ApplySwingDrive(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const EJointAngularConstraintIndex SwingConstraintIndex);

		FJointSolverResult ApplySwingProjection(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const EJointAngularConstraintIndex SwingConstraintIndex);

		FJointSolverResult ApplySLerpDrive(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		FJointSolverResult ApplyPointPositionConstraintKD(
			const int32 KIndex,
			const int32 DIndex,
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		FJointSolverResult ApplyPointPositionConstraintDD(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		FJointSolverResult ApplySphericalPositionConstraint(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		FJointSolverResult ApplySphericalPositionDrive(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		FJointSolverResult ApplyCylindricalPositionConstraint(
			const FReal Dt,
			const int32 AxisIndex,
			const EJointMotionType AxialMotion,
			const EJointMotionType RadialMotion,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		FJointSolverResult ApplyCircularPositionDrive(
			const FReal Dt,
			const int32 AxisIndex,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		FJointSolverResult ApplyPlanarPositionConstraint(
			const FReal Dt,
			const int32 AxisIndex,
			const EJointMotionType AxialMotion,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		FJointSolverResult ApplyAxialPositionDrive(
			const FReal Dt,
			const int32 AxisIndex,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		FJointSolverResult ApplyPositionProjection(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);


		// Local-space constraint settings
		FRigidTransform3 XLs[MaxConstrainedBodies];	// Local-space joint connector transforms
		FVec3 InvILs[MaxConstrainedBodies];			// Local-space inverse inertias
		FReal InvMs[MaxConstrainedBodies];			// Inverse masses

		// World-space constraint state
		FVec3 Xs[MaxConstrainedBodies];				// World-space joint connector positions
		FReal LinearSoftLambda;						// XPBD constraint multipliers (net applied constraint-space deltas)
		FReal LinearDriveLambda;					// XPBD constraint multipliers (net applied constraint-space deltas)
		FRotation3 Rs[MaxConstrainedBodies];		// World-space joint connector rotations

		// World-space body state
		FVec3 Ps[MaxConstrainedBodies];				// World-space particle CoM positions
		FReal TwistSoftLambda;						// XPBD constraint multipliers (net applied constraint-space deltas)
		FReal SwingSoftLambda;						// XPBD constraint multipliers (net applied constraint-space deltas)
		FRotation3 Qs[MaxConstrainedBodies];		// World-space particle CoM rotations
		FVec3 Vs[MaxConstrainedBodies];				// World-space particle CoM velocities
		FVec3 Ws[MaxConstrainedBodies];				// World-space particle CoM angular velocities

		// XPBD Previous iteration world-space body state
		FVec3 PrevPs[MaxConstrainedBodies];			// World-space particle CoM positions
		FReal TwistDriveLambda;						// XPBD constraint multipliers (net applied constraint-space deltas)
		FReal SwingDriveLambda;						// XPBD constraint multipliers (net applied constraint-space deltas)
		FRotation3 PrevQs[MaxConstrainedBodies];	// World-space particle CoM rotations
		FVec3 PrevXs[MaxConstrainedBodies];			// World-space joint connector positions
		
		// Post-angular constraint position fixup (to reduce iterations required for stiff angular constraints)
		FReal AngularPositionCorrection;

		FReal PositionTolerance;					// Distance error below which we consider a constraint or drive solved
		FReal AngleTolerance;						// Angle error below which we consider a constraint or drive solved
	};

}