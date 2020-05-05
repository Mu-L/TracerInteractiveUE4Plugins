// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Joint/PBDJointSolverCholesky.h"
#include "Chaos/Joint/ChaosJointLog.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDJointConstraintUtilities.h"
#include "Chaos/Utilities.h"
#include "ChaosStats.h"

//#pragma optimize("", off)

// Set to '1' enable solver stats (very high frequency, so usually disabled)
#define CHAOS_JOINTSOLVER_STATSENABLED 0

namespace Chaos
{
#if CHAOS_JOINTSOLVER_STATSENABLED
	DECLARE_CYCLE_STAT(TEXT("TPBDJointConstraints::ApplyConstraints"), STAT_JointSolver_ApplyConstraints, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("TPBDJointConstraints::ApplyDrives"), STAT_JointSolver_ApplyDrives, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("TPBDJointConstraints::ApplyPushOut"), STAT_JointSolver_Jacobian, STATGROUP_Chaos);
#define CHAOS_JOINTSOLVER_SCOPE_CYCLE_STAT SCOPE_CYCLE_COUNTER(X)
#else
#define CHAOS_JOINTSOLVER_SCOPE_CYCLE_STAT(X)
#endif

	FJointSolverCholesky::FJointSolverCholesky()
	{
	}

	void FJointSolverCholesky::UpdateDerivedState()
	{
		Xs[0] = Ps[0] + Qs[0] * XLs[0].GetTranslation();
		Xs[1] = Ps[1] + Qs[1] * XLs[1].GetTranslation();
		Rs[0] = Qs[0] * XLs[0].GetRotation();
		Rs[1] = Qs[1] * XLs[1].GetRotation();
	}


	void FJointSolverCholesky::InitConstraints(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FVec3& P0,
		const FRotation3& Q0,
		const FVec3& P1,
		const FRotation3& Q1,
		const FReal InvM0,
		const FMatrix33& InvIL0,
		const FReal InvM1,
		const FMatrix33& InvIL1,
		const FRigidTransform3& XL0,
		const FRigidTransform3& XL1)
	{
		XLs[0] = XL0;
		XLs[1] = XL1;
		InvILs[0] = InvIL0;
		InvILs[1] = InvIL1;
		InvMs[0] = InvM0;
		InvMs[1] = InvM1;

		Ps[0] = P0;
		Ps[1] = P1;
		Qs[0] = Q0;
		Qs[1] = Q1;
		Qs[1].EnforceShortestArcWith(Qs[0]);

		Stiffness = FPBDJointUtilities::GetLinearStiffness(SolverSettings, JointSettings);
		AngularDriveStiffness = 0.0f;// FPBDJointUtilities::GetAngularDriveStiffness(SolverSettings, JointSettings);
		SwingTwistAngleTolerance = SolverSettings.SwingTwistAngleTolerance;
		bEnableTwistLimits = SolverSettings.bEnableTwistLimits;
		bEnableSwingLimits = SolverSettings.bEnableSwingLimits;
		bEnableDrives = SolverSettings.bEnableDrives;

		UpdateDerivedState();
	}

	void FJointSolverCholesky::ApplyConstraints(
		const FReal Dt,
		const FPBDJointSettings& JointSettings)
	{
		CHAOS_JOINTSOLVER_SCOPE_CYCLE_STAT(STAT_JointSolver_ApplyConstraints);

		FDenseMatrix61 C;
		FDenseMatrix66 J0, J1;
		BuildJacobianAndResidual_Constraints(JointSettings, J0, J1, C);

		SolveAndApply(JointSettings, J0, J1, C);
	}

	void FJointSolverCholesky::ApplyDrives(
		const FReal Dt,
		const FPBDJointSettings& JointSettings)
	{
		CHAOS_JOINTSOLVER_SCOPE_CYCLE_STAT(STAT_JointSolver_ApplyDrives);

		FDenseMatrix61 C;
		FDenseMatrix66 J0, J1;
		BuildJacobianAndResidual_Drives(JointSettings, J0, J1, C);

		SolveAndApply(JointSettings, J0, J1, C);
	}

	void FJointSolverCholesky::BuildJacobianAndResidual_Constraints(
		const FPBDJointSettings& JointSettings,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C)
	{
		CHAOS_JOINTSOLVER_SCOPE_CYCLE_STAT(STAT_JointSolver_Jacobian);

		// Initialize with zero constraints
		J0.SetDimensions(0, 6);
		J1.SetDimensions(0, 6);
		C.SetDimensions(0, 1);

		AddLinearConstraints(JointSettings, J0, J1, C);
		AddAngularConstraints(JointSettings, J0, J1, C);
	}

	void FJointSolverCholesky::BuildJacobianAndResidual_Drives(
		const FPBDJointSettings& JointSettings,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C)
	{
		CHAOS_JOINTSOLVER_SCOPE_CYCLE_STAT(STAT_JointSolver_Jacobian);

		// Initialize with zero constraints
		J0.SetDimensions(0, 6);
		J1.SetDimensions(0, 6);
		C.SetDimensions(0, 1);

		//AddLinearConstraints(JointSettings, J0, J1, C);
		AddAngularDrives(JointSettings, J0, J1, C);
	}

	void FJointSolverCholesky::SolveAndApply(
		const FPBDJointSettings& JointSettings,
		const FDenseMatrix66& J0,
		const FDenseMatrix66& J1,
		const FDenseMatrix61& C)
	{
		// Solving for world-space position/rotation corrections D(6x1) where
		// D = [I.Jt / [J.I.Jt]].C = I.Jt.L
		// I is the inverse mass matrix, J the Jacobian, C the current constraint violation,
		// and L = [1 / [J.I.Jt]].C is the Joint-space correction.

		// For N constraints
		// Constraint error: C(Nx1)
		// Jacobian : J(Nx6)
		// The Jacobians will be some sub-set of the following rows, depending on the "active" constraints 
		// ("active" = enabled and either fixed or with limits exceeded).
		//
		// J0(Nx6) = | XAxis          -XAxis x Connector0 |
		//           | YAxis          -YAxis x Connector0 |
		//           | ZAxis          -ZAxis x Connector0 |
		//           | 0              TwistAxis           |
		//           | 0              Swing1Axis          |
		//           | 0              Swing2Axis          |
		//
		// J1(Nx6) = | -XAxis         XAxis x Connector1  |
		//           | -YAxis         YAxis x Connector1  |
		//           | -ZAxis         ZAxis x Connector1  |
		//           | 0              -TwistAxis          |
		//           | 0              -Swing1Axis         |
		//           | 0              -Swing2Axis         |
		//

		// InvM(6x6) = inverse mass matrix
		const FMassMatrix InvM0 = FMassMatrix::Make(InvMs[0], Qs[0], InvILs[0]);
		const FMassMatrix InvM1 = FMassMatrix::Make(InvMs[1], Qs[1], InvILs[1]);

		// IJt(6xN) = I(6x6).Jt(6xN)
		const FDenseMatrix66 IJt0 = FDenseMatrix66::MultiplyABt(InvM0, J0);
		const FDenseMatrix66 IJt1 = FDenseMatrix66::MultiplyABt(InvM1, J1);

		// Joint-space mass: F(NxN) = J(Nx6).I(6x6).Jt(6xN) = J(Nx6).IJt(6xN)
		// NOTE: Result is symmetric
		const FDenseMatrix66 F0 = FDenseMatrix66::MultiplyAB_Symmetric(J0, IJt0);
		const FDenseMatrix66 F = FDenseMatrix66::MultiplyBCAddA_Symmetric(F0, J1, IJt1);

		// Joint-space correction: L(Nx1) = [1/F](NxN).C(Nx1)
		FDenseMatrix61 L;
		if (FDenseMatrixSolver::SolvePositiveDefinite(F, C, L))
		{
			// World-space correction: D(6x1) = I(6x6).Jt(6xN).L(Nx1) = IJt(6xN).L(Nx1)
			const FDenseMatrix61 D0 = FDenseMatrix61::MultiplyAB(IJt0, L);
			const FDenseMatrix61 D1 = FDenseMatrix61::MultiplyAB(IJt1, L);

			// Extract world-space position correction
			Ps[0] = Ps[0] + FVec3(Stiffness * D0.At(0, 0), Stiffness * D0.At(1, 0), Stiffness * D0.At(2, 0));
			Ps[1] = Ps[1] + FVec3(Stiffness * D1.At(0, 0), Stiffness * D1.At(1, 0), Stiffness * D1.At(2, 0));

			// Extract world-space rotation correction
			const FReal HalfStiffness = (FReal)0.5 * Stiffness;
			const FRotation3 DQ0 = FRotation3::FromElements(HalfStiffness * D0.At(3, 0), HalfStiffness * D0.At(4, 0), HalfStiffness * D0.At(5, 0), 0) * Qs[0];
			const FRotation3 DQ1 = FRotation3::FromElements(HalfStiffness * D1.At(3, 0), HalfStiffness * D1.At(4, 0), HalfStiffness * D1.At(5, 0), 0) * Qs[1];
			Qs[0] = (Qs[0] + DQ0).GetNormalized();
			Qs[1] = (Qs[1] + DQ1).GetNormalized();
			Qs[1].EnforceShortestArcWith(Qs[0]);

			UpdateDerivedState();
		}
	}

	void FJointSolverCholesky::AddLinearRow(
		const FVec3& Axis,
		const FVec3& Connector0,
		const FVec3& Connector1,
		const FReal Error,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C)
	{
		const int32 RowIndex = J0.NumRows();
		J0.AddRows(1);
		J1.AddRows(1);
		C.AddRows(1);

		J0.SetRowAt(RowIndex, 0, Axis);
		J0.SetRowAt(RowIndex, 3, -FVec3::CrossProduct(Axis, Connector0));

		J1.SetRowAt(RowIndex, 0, -Axis);
		J1.SetRowAt(RowIndex, 3, FVec3::CrossProduct(Axis, Connector1));

		C.SetAt(RowIndex, 0, Error);
	}

	void FJointSolverCholesky::AddAngularRow(
		const FVec3& Axis0,
		const FVec3& Axis1,
		const FReal Error,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C)
	{
		const int32 RowIndex = J0.NumRows();
		J0.AddRows(1);
		J1.AddRows(1);
		C.AddRows(1);

		J0.SetRowAt(RowIndex, 0, 0, 0, 0);
		J0.SetRowAt(RowIndex, 3, Axis0);

		J1.SetRowAt(RowIndex, 0, 0, 0, 0);
		J1.SetRowAt(RowIndex, 3, -Axis1);

		C.SetAt(RowIndex, 0, Error);
	}


	// 3 constraints along principle axes
	void FJointSolverCholesky::AddLinearConstraints_Point(
		const FPBDJointSettings& JointSettings,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C)
	{
		const int32 RowIndex = J0.NumRows();
		J0.AddRows(3);
		J1.AddRows(3);
		C.AddRows(3);

		// Cross Product
		//Result[0] = V1[1] * V2[2] - V1[2] * V2[1];
		//Result[1] = V1[2] * V2[0] - V1[0] * V2[2];
		//Result[2] = V1[0] * V2[1] - V1[1] * V2[0];

		const FVec3 XP0 = Xs[0] - Ps[0];
		J0.SetBlockAtDiagonal33(RowIndex, 0, (FReal)1, (FReal)0);
		J0.SetRowAt(RowIndex + 0, 3, (FReal)0, XP0[2], -XP0[1]);	// -(1,0,0) x XP0
		J0.SetRowAt(RowIndex + 1, 3, -XP0[2], (FReal)0, XP0[0]);	// -(0,1,0) x XP0
		J0.SetRowAt(RowIndex + 2, 3, XP0[1], -XP0[0], (FReal)0);	// -(0,0,1) x XP0

		const FVec3 XP1 = Xs[1] - Ps[1];
		J1.SetBlockAtDiagonal33(RowIndex, 0, (FReal)-1, (FReal)0);
		J1.SetRowAt(RowIndex + 0, 3, (FReal)0, -XP1[2], XP1[1]);	// (1,0,0) x XP1
		J1.SetRowAt(RowIndex + 1, 3, XP1[2], (FReal)0, -XP1[0]);	// (0,1,0) x XP1
		J1.SetRowAt(RowIndex + 2, 3, -XP1[1], XP1[0], (FReal)0);	// (0,0,1) x XP1

		const FVec3 ConstraintSeparation = Xs[1] - Xs[0];
		C.SetAt(RowIndex + 0, 0, ConstraintSeparation[0]);
		C.SetAt(RowIndex + 1, 0, ConstraintSeparation[1]);
		C.SetAt(RowIndex + 2, 0, ConstraintSeparation[2]);
	}

	// up to 1 constraint limiting distance
	void FJointSolverCholesky::AddLinearConstraints_Sphere(
		const FPBDJointSettings& JointSettings,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C)
	{
		const FReal Limit = JointSettings.LinearLimit;
		const FVec3 ConstraintSeparation = Xs[1] - Xs[0];
		const FReal ConstraintSeparationLen = ConstraintSeparation.Size();

		bool bConstraintActive = (ConstraintSeparationLen >= FMath::Max(Limit, KINDA_SMALL_NUMBER));
		if (bConstraintActive)
		{
			const FVec3 XP0 = Xs[0] - Ps[0];
			const FVec3 XP1 = Xs[1] - Ps[1];
			const FVec3 Axis = ConstraintSeparation / ConstraintSeparationLen;
			const FReal Error = ConstraintSeparationLen - Limit;

			AddLinearRow(Axis, XP0, XP1, Error, J0, J1, C);
		}
	}

	// up to 2 constraint: 1 limiting distance along the axis and another limiting lateral distance from the axis
	void FJointSolverCholesky::AddLinearConstraints_Cylinder(
		const FPBDJointSettings& JointSettings,
		const EJointMotionType AxisMotion,
		const FVec3& Axis,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C)
	{
		const FVec3 ConstraintSeparation = Xs[1] - Xs[0];
		const FVec3 XP0 = Xs[0] - Ps[0];
		const FVec3 XP1 = Xs[1] - Ps[1];

		// Axial Constraint
		const FReal ConstraintDistanceAxial = FVec3::DotProduct(ConstraintSeparation, Axis);
		bool bAxisConstraintActive = (AxisMotion != EJointMotionType::Free);
		if (bAxisConstraintActive)
		{
			const FReal Error = ConstraintDistanceAxial;

			AddLinearRow(Axis, XP0, XP1, Error, J0, J1, C);
		}

		// Radial Constraint
		const FVec3 ConstraintSeparationRadial = ConstraintSeparation - ConstraintDistanceAxial * Axis;
		const FReal ConstraintDistanceRadial = ConstraintSeparationRadial.Size();
		const FReal RadialLimit = JointSettings.LinearLimit;
		bool bRadialConstraintActive = (ConstraintDistanceRadial >= RadialLimit);
		if (bRadialConstraintActive)
		{
			const FVec3 RadialAxis = ConstraintSeparationRadial / ConstraintDistanceRadial;
			const FReal Error = ConstraintDistanceRadial - RadialLimit;

			AddLinearRow(RadialAxis, XP0, XP1, Error, J0, J1, C);
		}
	}

	// up to 1 constraint limiting distance along the axis (lateral motion unrestricted)
	void FJointSolverCholesky::AddLinearConstraints_Plane(
		const FPBDJointSettings& JointSettings,
		const EJointMotionType AxisMotion,
		const FVec3& Axis,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C)
	{
		const FReal Limit = (AxisMotion == EJointMotionType::Limited) ? JointSettings.LinearLimit : (FReal)0;
		const FVec3 ConstraintSeparation = Xs[1] - Xs[0];

		// Planar Constraint
		const FReal ConstraintDistanceAxial = FVec3::DotProduct(ConstraintSeparation, Axis);
		bool bAxisConstraintActive = (ConstraintDistanceAxial <= -Limit) || (ConstraintDistanceAxial >= Limit);
		if (bAxisConstraintActive)
		{
			const FVec3 XP0 = Xs[0] - Ps[0];
			const FVec3 XP1 = Xs[1] - Ps[1];
			const FReal Error = (ConstraintDistanceAxial >= Limit) ? ConstraintDistanceAxial - Limit : ConstraintDistanceAxial + Limit;

			AddLinearRow(Axis, XP0, XP1, Error, J0, J1, C);
		}
	}

	// up to 1 constraint limiting rotation about twist axes
	void FJointSolverCholesky::AddAngularConstraints_Twist(
		const FPBDJointSettings& JointSettings,
		const FRotation3& R01Twist,
		const FRotation3& R01Swing,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C)
	{
		FVec3 TwistAxis01 = FJointConstants::TwistAxis();
		FReal TwistAngle = R01Twist.GetAngle();
		if (TwistAngle > PI)
		{
			TwistAngle = TwistAngle - (FReal)2 * PI;
		}
		if (R01Twist.X < 0)
		{
			TwistAngle = -TwistAngle;
		}

		const FReal TwistAngleMax = JointSettings.AngularLimits[(int32)EJointAngularConstraintIndex::Twist];
		bool bConstraintActive = (TwistAngle <= -TwistAngleMax) || (TwistAngle >= TwistAngleMax);
		if (bConstraintActive)
		{
			const FVec3 Axis0 = Rs[0] * TwistAxis01;
			const FVec3 Axis1 = Rs[1] * TwistAxis01;
			const FReal Error = (TwistAngle >= TwistAngleMax) ? TwistAngle - TwistAngleMax : TwistAngle + TwistAngleMax;

			AddAngularRow(Axis0, Axis1, Error, J0, J1, C);
		}
	}

	// up to 1 constraint limiting angle between twist axes
	void FJointSolverCholesky::AddAngularConstraints_Cone(
		const FPBDJointSettings& JointSettings,
		const FRotation3& R01Twist,
		const FRotation3& R01Swing,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C)
	{
		FVec3 SwingAxis01;
		FReal SwingAngle;
		R01Swing.ToAxisAndAngleSafe(SwingAxis01, SwingAngle, FJointConstants::Swing1Axis(), SwingTwistAngleTolerance);
		if (SwingAngle > PI)
		{
			SwingAngle = SwingAngle - (FReal)2 * PI;
		}

		// Calculate swing limit for the current swing axis
		FReal SwingAngleMax = FLT_MAX;
		const FReal Swing1Limit = JointSettings.AngularLimits[(int32)EJointAngularConstraintIndex::Swing1];
		const FReal Swing2Limit = JointSettings.AngularLimits[(int32)EJointAngularConstraintIndex::Swing2];

		// Circular swing limit
		SwingAngleMax = Swing1Limit;

		// Elliptical swing limit
		if (!FMath::IsNearlyEqual(Swing1Limit, Swing2Limit, KINDA_SMALL_NUMBER))
		{
			// Map swing axis to ellipse and calculate limit for this swing axis
			const FReal DotSwing1 = FMath::Abs(FVec3::DotProduct(SwingAxis01, FJointConstants::Swing1Axis()));
			const FReal DotSwing2 = FMath::Abs(FVec3::DotProduct(SwingAxis01, FJointConstants::Swing2Axis()));
			SwingAngleMax = FMath::Sqrt(Swing1Limit * DotSwing2 * Swing1Limit * DotSwing2 + Swing2Limit * DotSwing1 * Swing2Limit * DotSwing1);
		}

		bool bConstraintActive = (SwingAngle <= -SwingAngleMax) || (SwingAngle >= SwingAngleMax);
		if (bConstraintActive)
		{
			const FVec3 Axis = Rs[0] * SwingAxis01;
			const FReal Error = (SwingAngle >= SwingAngleMax) ? SwingAngle - SwingAngleMax : SwingAngle + SwingAngleMax;

			AddAngularRow(Axis, Axis, Error, J0, J1, C);
		}
	}

	float GetSwingAngle(const FReal SwingY, const FReal SwingW)
	{
		return 4.0f * FMath::Atan2(SwingY, 1.0f + SwingW);
	}


	// up to 1 constraint limiting rotation about swing axis (relative to body 0)
	void FJointSolverCholesky::AddAngularConstraints_Swing(
		const FPBDJointSettings& JointSettings,
		const EJointAngularConstraintIndex SwingConstraintIndex,
		const EJointAngularAxisIndex SwingAxisIndex,
		const FRotation3& R01Twist,
		const FRotation3& R01Swing,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C)
	{
		FVec3 TwistAxis01;
		FReal TwistAngle;
		R01Twist.ToAxisAndAngleSafe(TwistAxis01, TwistAngle, FJointConstants::TwistAxis(), SwingTwistAngleTolerance);
		if (TwistAngle > PI)
		{
			TwistAngle = TwistAngle - (FReal)2 * PI;
		}
		if (FVec3::DotProduct(TwistAxis01, FJointConstants::TwistAxis()) < 0)
		{
			TwistAxis01 = -TwistAxis01;
			TwistAngle = -TwistAngle;
		}
		const FVec3 TwistAxis = Rs[0] * TwistAxis01;

		const FRotation3 R1NoTwist = Rs[1] * R01Twist.Inverse();
		const FMatrix33 Axes0 = Rs[0].ToMatrix();
		const FMatrix33 Axes1 = R1NoTwist.ToMatrix();
		FVec3 SwingCross = FVec3::CrossProduct(Axes0.GetAxis((int32)SwingAxisIndex), Axes1.GetAxis((int32)SwingAxisIndex));
		SwingCross = SwingCross - FVec3::DotProduct(TwistAxis, SwingCross) * TwistAxis;
		const FReal SwingCrossLen = SwingCross.Size();
		if (SwingCrossLen > KINDA_SMALL_NUMBER)
		{
			FReal SwingAngle = FMath::Asin(FMath::Clamp(SwingCrossLen, (FReal)0, (FReal)1));
			const FReal SwingDot = FVec3::DotProduct(Axes0.GetAxis((int32)SwingAxisIndex), Axes1.GetAxis((int32)SwingAxisIndex));
			if (SwingDot < (FReal)0)
			{
				SwingAngle = (FReal)PI - SwingAngle;
			}

			const FMatrix33 RM0 = Rs[0].ToMatrix();
			const FReal R01SwingYorZ = (SwingAxisIndex == EJointAngularAxisIndex::Swing1) ? R01Swing.Y : R01Swing.Z;
			const FReal SwingAngle2 = GetSwingAngle(R01SwingYorZ, R01Swing.W);


			FReal SwingAngleMax = JointSettings.AngularLimits[(int32)SwingConstraintIndex];
			bool bConstraintActive = (SwingAngle <= -SwingAngleMax) || (SwingAngle >= SwingAngleMax);
			if (bConstraintActive)
			{
				const FVec3 Axis = SwingCross / SwingCrossLen;
				const FReal Error = (SwingAngle >= SwingAngleMax) ? SwingAngle - SwingAngleMax : SwingAngle + SwingAngleMax;

				AddAngularRow(Axis, Axis, Error, J0, J1, C);
			}
		}
	}

	//void FJointSolverCholesky::AddAngularConstraints_Swing(
	//	const FPBDJointSettings& JointSettings,
	//	const EJointAngularConstraintIndex SwingConstraintIndex,
	//	const EJointAngularAxisIndex SwingAxisIndex,
	//	const FRotation3& R01Twist,
	//	const FRotation3& R01Swing,
	//	FDenseMatrix66& J0,
	//	FDenseMatrix66& J1,
	//	FDenseMatrix61& C)
	//{
	//	const FMatrix33 RM0 = Rs[0].ToMatrix();
	//	const FReal R01SwingYorZ = ((int32)SwingAxisIndex == 2) ? R01Swing.Z : R01Swing.Y;	// Can't index a quat :(
	//	FReal SwingAngle = GetSwingAngle(R01SwingYorZ, R01Swing.W);

	//	FReal SwingAngleMax = JointSettings.AngularLimits[(int32)SwingConstraintIndex];
	//	bool bConstraintActive = (SwingAngle <= -SwingAngleMax) || (SwingAngle >= SwingAngleMax);
	//	if (bConstraintActive)
	//	{
	//		const FVec3 Axis = RM0.GetAxis((int32)SwingAxisIndex);
	//		const FReal Error = (SwingAngle >= SwingAngleMax) ? SwingAngle - SwingAngleMax : SwingAngle + SwingAngleMax;

	//		AddAngularRow(Axis, Axis, Error, J0, J1, C);
	//	}
	//}

	void FJointSolverCholesky::AddAngularDrive_SLerp(
		const FPBDJointSettings& JointSettings,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C)
	{
		// @todo(ccaulfield): target angles
		FRotation3 R01Twist, R01Swing;
		FPBDJointUtilities::DecomposeSwingTwistLocal(Rs[0], Rs[1], R01Swing, R01Twist);

		FVec3 SwingAxis01;
		FReal SwingAngle;
		R01Swing.ToAxisAndAngleSafe(SwingAxis01, SwingAngle, FJointConstants::Swing1Axis(), SwingTwistAngleTolerance);
		if (SwingAngle > PI)
		{
			SwingAngle = SwingAngle - (FReal)2 * PI;
		}

		{
			const FVec3 Axis = Rs[0] * SwingAxis01;
			const FReal Error = AngularDriveStiffness * SwingAngle;

			AddAngularRow(Axis, Axis, Error, J0, J1, C);
		}
	}


	void FJointSolverCholesky::AddAngularDrive_Swing(
		const FPBDJointSettings& JointSettings,
		const EJointAngularConstraintIndex SwingConstraintIndex,
		const EJointAngularAxisIndex SwingAxisIndex,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C)
	{
	}

	// Add linear constraints to solver
	void FJointSolverCholesky::AddLinearConstraints(
		const FPBDJointSettings& JointSettings,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C)
	{
		const TVector<EJointMotionType, 3>& Motion = JointSettings.LinearMotionTypes;
		if ((Motion[0] == EJointMotionType::Locked) && (Motion[1] == EJointMotionType::Locked) && (Motion[2] == EJointMotionType::Locked))
		{
			AddLinearConstraints_Point(JointSettings, J0, J1, C);
		}
		else if ((Motion[0] == EJointMotionType::Limited) && (Motion[1] == EJointMotionType::Limited) && (Motion[2] == EJointMotionType::Limited))
		{
			AddLinearConstraints_Sphere(JointSettings, J0, J1, C);
		}
		else if ((Motion[1] == EJointMotionType::Limited) && (Motion[2] == EJointMotionType::Limited))
		{
			// Circular Limit (X Axis)
			AddLinearConstraints_Cylinder(JointSettings, Motion[0], Rs[0] * FVec3(1, 0, 0), J0, J1, C);
		}
		else if ((Motion[0] == EJointMotionType::Limited) && (Motion[2] == EJointMotionType::Limited))
		{
			// Circular Limit (Y Axis)
			AddLinearConstraints_Cylinder(JointSettings, Motion[1], Rs[0] * FVec3(0, 1, 0), J0, J1, C);
		}
		else if ((Motion[0] == EJointMotionType::Limited) && (Motion[1] == EJointMotionType::Limited))
		{
			// Circular Limit (Z Axis)
			AddLinearConstraints_Cylinder(JointSettings, Motion[2], Rs[0] * FVec3(0, 0, 1), J0, J1, C);
		}
		else
		{
			// Plane/Square/Cube Limits (no way to author square or cube limits, but would work if we wanted it)
			if (Motion[0] != EJointMotionType::Free)
			{
				AddLinearConstraints_Plane(JointSettings, Motion[0], Rs[0] * FVec3(1, 0, 0), J0, J1, C);
			}
			if (Motion[1] != EJointMotionType::Free)
			{
				AddLinearConstraints_Plane(JointSettings, Motion[1], Rs[0] * FVec3(0, 1, 0), J0, J1, C);
			}
			if (Motion[2] != EJointMotionType::Free)
			{
				AddLinearConstraints_Plane(JointSettings, Motion[2], Rs[0] * FVec3(0, 0, 1), J0, J1, C);
			}
		}
	}

	// Add angular constraints to solver
	void FJointSolverCholesky::AddAngularConstraints(
		const FPBDJointSettings& JointSettings,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C)
	{
		EJointMotionType TwistMotion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist];
		EJointMotionType Swing1Motion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing1];
		EJointMotionType Swing2Motion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing2];

		bool bAddTwist = bEnableTwistLimits && (TwistMotion != EJointMotionType::Free);
		bool bAddConeOrSwing = bEnableSwingLimits && ((Swing1Motion != EJointMotionType::Free) || (Swing2Motion != EJointMotionType::Free));

		if (bAddTwist || bAddConeOrSwing)
		{
			// Decompose rotation of body 1 relative to body 0 into swing and twist rotations, assuming twist is X axis
			FRotation3 R01Twist, R01Swing;
			FPBDJointUtilities::DecomposeSwingTwistLocal(Rs[0], Rs[1], R01Swing, R01Twist);

			// Add twist constraint
			if (bAddTwist)
			{
				AddAngularConstraints_Twist(JointSettings, R01Twist, R01Swing, J0, J1, C);
			}

			// Add swing constraints
			if (bAddConeOrSwing)
			{
				if ((Swing1Motion == EJointMotionType::Limited) && (Swing2Motion == EJointMotionType::Limited))
				{
					AddAngularConstraints_Cone(JointSettings, R01Twist, R01Swing, J0, J1, C);
				}
				else
				{
					if (Swing1Motion != EJointMotionType::Free)
					{
						AddAngularConstraints_Swing(JointSettings, EJointAngularConstraintIndex::Swing1, EJointAngularAxisIndex::Swing1, R01Twist, R01Swing, J0, J1, C);
					}
					if (Swing2Motion != EJointMotionType::Free)
					{
						AddAngularConstraints_Swing(JointSettings, EJointAngularConstraintIndex::Swing2, EJointAngularAxisIndex::Swing2, R01Twist, R01Swing, J0, J1, C);
					}
				}
			}
		}
	}

	// Add angular constraints to solver
	void FJointSolverCholesky::AddAngularDrives(
		const FPBDJointSettings& JointSettings,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C)
	{
		EJointMotionType TwistMotion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist];
		EJointMotionType Swing1Motion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing1];
		EJointMotionType Swing2Motion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing2];

		bool bTwistDriveEnabled = false;// bEnableDrives&& JointSettings.bAngularTwistDriveEnabled && (TwistMotion != EJointMotionType::Locked);
		bool bSwing1DriveEnabled = false;// bEnableDrives && JointSettings.bAngularSwingDriveEnabled && (Swing1Motion != EJointMotionType::Locked);
		bool bSwing2DriveEnabled = false;// bEnableDrives && JointSettings.bAngularSwingDriveEnabled && (Swing2Motion != EJointMotionType::Locked);
		bool bSlerpDriveEnabled = false;// bEnableDrives && JointSettings.bAngularSLerpDriveEnabled && (Swing1Motion != EJointMotionType::Locked) && (Swing2Motion != EJointMotionType::Locked);
		bool bAnyDriveEnabled = (bTwistDriveEnabled || bSwing1DriveEnabled || bSwing2DriveEnabled || bSlerpDriveEnabled);

		if (bAnyDriveEnabled)
		{
			if (bSlerpDriveEnabled)
			{
				AddAngularDrive_SLerp(JointSettings, J0, J1, C);
			}
			else
			{
				if (bTwistDriveEnabled)
				{
				}
				if (bSwing1DriveEnabled)
				{
					AddAngularDrive_Swing(JointSettings, EJointAngularConstraintIndex::Swing1, EJointAngularAxisIndex::Swing1, J0, J1, C);
				}
				if (bSwing2DriveEnabled)
				{
					AddAngularDrive_Swing(JointSettings, EJointAngularConstraintIndex::Swing2, EJointAngularAxisIndex::Swing2, J0, J1, C);
				}
			}
		}
	}

}
