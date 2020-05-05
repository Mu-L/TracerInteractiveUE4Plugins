// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/Evolution/PBDMinEvolution.h"
#include "Chaos/Collision/CollisionDetector.h"
#include "Chaos/Collision/NarrowPhase.h"
#include "Chaos/Collision/ParticlePairBroadPhase.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDConstraintRule.h"
#include "Chaos/PBDRigidsSOAs.h"
#include "Chaos/PerParticleAddImpulses.h"
#include "Chaos/PerParticleEtherDrag.h"
#include "Chaos/PerParticleEulerStepVelocity.h"
#include "Chaos/PerParticleGravity.h"
#include "Chaos/PerParticleInitForce.h"
#include "Chaos/PerParticlePBDEulerStep.h"
#include "Chaos/PerParticlePBDGroundConstraint.h"
#include "Chaos/PerParticlePBDUpdateFromDeltaPosition.h"
#include "ChaosStats.h"

//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{
	DECLARE_CYCLE_STAT(TEXT("MinEvolution::AdvanceOneTimeStep"), STAT_MinEvolution_AdvanceOneTimeStep, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("MinEvolution::Integrate"), STAT_MinEvolution_Integrate, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("MinEvolution::KinematicTargets"), STAT_MinEvolution_KinematicTargets, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("MinEvolution::PrepareConstraints"), STAT_MinEvolution_PrepareConstraints, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("MinEvolution::UnprepareConstraints"), STAT_MinEvolution_UnprepareConstraints, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("MinEvolution::ApplyConstraints"), STAT_MinEvolution_ApplyConstraints, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("MinEvolution::UpdateVelocities"), STAT_MinEvolution_UpdateVelocites, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("MinEvolution::ApplyPushOut"), STAT_MinEvolution_ApplyPushOut, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("MinEvolution::DetectCollisions"), STAT_MinEvolution_DetectCollisions, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("MinEvolution::UpdatePositions"), STAT_MinEvolution_UpdatePositions, STATGROUP_Chaos);

	FPBDMinEvolution::FPBDMinEvolution(FRigidParticleSOAs& InParticles, FCollisionDetector& InCollisionDetector, const FReal InBoundsExtension)
		: Particles(InParticles)
		, CollisionDetector(InCollisionDetector)
		, NumApplyIterations(0)
		, NumApplyPushOutIterations(0)
		, BoundsExtension(InBoundsExtension)
		, Gravity(FVec3(0))
	{
	}

	void FPBDMinEvolution::AddConstraintRule(FSimpleConstraintRule* Rule)
	{
		ConstraintRules.Add(Rule);
	}

	void FPBDMinEvolution::Advance(const FReal StepDt, const int32 NumSteps)
	{
		for (TTransientPBDRigidParticleHandle<FReal, 3>& Particle : Particles.GetActiveParticlesView())
		{
			if (Particle.ObjectState() == EObjectStateType::Dynamic)
			{
				Particle.F() += Particle.M() * Gravity;
			}
		}

		for (int32 Step = 0; Step < NumSteps; ++Step)
		{
			// StepFraction: how much of the remaining time this step represents, used to interpolate kinematic targets
			// E.g., for 4 steps this will be: 1/4, 1/3, 1/2, 1
			const float StepFraction = (FReal)1 / (FReal)(NumSteps - Step);

			UE_LOG(LogChaos, Verbose, TEXT("Advance dt = %f [%d/%d]"), StepDt, Step + 1, NumSteps);

			AdvanceOneTimeStep(StepDt, StepFraction);
		}

		for (TTransientPBDRigidParticleHandle<FReal, 3>& Particle : Particles.GetActiveParticlesView())
		{
			if (Particle.ObjectState() == EObjectStateType::Dynamic)
			{
				Particle.F() = FVec3(0);
				Particle.Torque() = FVec3(0);
			}
		}
	}

	void FPBDMinEvolution::AdvanceOneTimeStep(const FReal Dt, const FReal StepFraction)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_AdvanceOneTimeStep);

		Integrate(Dt);

		ApplyKinematicTargets(Dt, StepFraction);

		if (PostIntegrateCallback != nullptr)
		{
			PostIntegrateCallback();
		}

		DetectCollisions(Dt);

		if (PostDetectCollisionsCallback != nullptr)
		{
			PostDetectCollisionsCallback();
		}

		if (Dt > 0)
		{
			PrepareConstraints(Dt);

			ApplyConstraints(Dt);

			if (PostApplyCallback != nullptr)
			{
				PostApplyCallback();
			}

			UpdateVelocities(Dt);

			ApplyPushOutConstraints(Dt);

			if (PostApplyPushOutCallback != nullptr)
			{
				PostApplyPushOutCallback();
			}

			UnprepareConstraints(Dt);

			UpdatePositions(Dt);
		}
	}

	// @todo(ccaulfield): dedupe (PBDRigidsEvolutionGBF)
	void FPBDMinEvolution::Integrate(FReal Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_Integrate);

		for (TTransientPBDRigidParticleHandle<FReal, 3>& Particle : Particles.GetActiveParticlesView())
		{
			if (Particle.ObjectState() == EObjectStateType::Dynamic)
			{
				Particle.PreV() = Particle.V();
				Particle.PreW() = Particle.W();

				const FVec3 XCoM = FParticleUtilitiesXR::GetCoMWorldPosition(&Particle);
				const FRotation3 RCoM = FParticleUtilitiesXR::GetCoMWorldRotation(&Particle);

				// Calculate new velocities from forces, torques and drag
				const FMatrix33 WorldInvI = Utilities::ComputeWorldSpaceInertia(RCoM, Particle.InvI());
				const FVec3 DV = Particle.InvM() * (Particle.F() * Dt + Particle.LinearImpulse());
				const FVec3 DW = WorldInvI * (Particle.Torque() * Dt + Particle.AngularImpulse());
				const FReal LinearDrag = FMath::Max(FReal(0), FReal(1) - (Particle.LinearEtherDrag() * Dt));
				const FReal AngularDrag = FMath::Max(FReal(0), FReal(1) - (Particle.AngularEtherDrag() * Dt));
				const FVec3 V = LinearDrag * (Particle.V() + DV);
				const FVec3 W = AngularDrag * (Particle.W() + DW);

				const FVec3 PCoM = XCoM + V * Dt;
				const FRotation3 QCoM = FRotation3::IntegrateRotationWithAngularVelocity(RCoM, W, Dt);

				// Update particle state (forces are not zeroed until the end of the frame)
				FParticleUtilitiesPQ::SetCoMWorldTransform(&Particle, PCoM, QCoM);
				Particle.V() = V;
				Particle.W() = W;
				Particle.LinearImpulse() = FVec3(0);
				Particle.AngularImpulse() = FVec3(0);

				// Update world-space bounds
				if (Particle.HasBounds())
				{
					const TAABB<FReal, 3>& LocalBounds = Particle.LocalBounds();
					
					TAABB<FReal, 3> WorldSpaceBounds = LocalBounds.TransformedAABB(FRigidTransform3(Particle.P(), Particle.Q()));
					WorldSpaceBounds.ThickenSymmetrically(WorldSpaceBounds.Extents() * BoundsExtension);

					// Dynamic bodies may get pulled back into their old positions by joints - make sure we find collisions that may prevent this
					// We could add the AABB at X/R here, but I'm avoiding another call to TransformedAABB. Hopefully this is good enough.
					WorldSpaceBounds.GrowByVector(Particle.X() - Particle.P());

					Particle.SetWorldSpaceInflatedBounds(WorldSpaceBounds);
				}
			}
		}
	}

	// @todo(ccaulfield): dedupe (PBDRigidsEvolutionGBF)
	void FPBDMinEvolution::ApplyKinematicTargets(FReal Dt, FReal StepFraction)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_KinematicTargets);

		check(StepFraction > (FReal)0);
		check(StepFraction <= (FReal)1);

		// @todo(ccaulfield): optimize. Depending on the number of kinematics relative to the number that have 
		// targets set, it may be faster to process a command list rather than iterate over them all each frame. 
		const FReal MinDt = 1e-6f;
		for (auto& Particle : Particles.GetActiveKinematicParticlesView())
		{
			const FVec3 PrevX = Particle.X();
			const FRotation3 PrevR = Particle.R();

			TKinematicTarget<FReal, 3>& KinematicTarget = Particle.KinematicTarget();
			switch (KinematicTarget.GetMode())
			{
			case EKinematicTargetMode::None:
				// Nothing to do
				break;

			case EKinematicTargetMode::Zero:
			{
				// Reset velocity and then switch to do-nothing mode
				Particle.V() = FVec3(0);
				Particle.W() = FVec3(0);
				KinematicTarget.SetMode(EKinematicTargetMode::None);
				break;
			}

			case EKinematicTargetMode::Position:
			{
				// Move to kinematic target and update velocities to match
				// Target positions only need to be processed once, and we reset the velocity next frame (if no new target is set)
				FVec3 TargetPos;
				FRotation3 TargetRot;
				if (FMath::IsNearlyEqual(StepFraction, (FReal)1, KINDA_SMALL_NUMBER))
				{
					TargetPos = KinematicTarget.GetTarget().GetLocation();
					TargetRot = KinematicTarget.GetTarget().GetRotation();
					KinematicTarget.SetMode(EKinematicTargetMode::Zero);
				}
				else
				{
					TargetPos = FVec3::Lerp(Particle.X(), KinematicTarget.GetTarget().GetLocation(), StepFraction);
					TargetRot = FRotation3::Slerp(Particle.R(), KinematicTarget.GetTarget().GetRotation(), StepFraction);
				}
				if (Dt > MinDt)
				{
					Particle.V() = FVec3::CalculateVelocity(Particle.X(), TargetPos, Dt);
					Particle.W() = FRotation3::CalculateAngularVelocity(Particle.R(), TargetRot, Dt);
				}
				Particle.X() = TargetPos;
				Particle.R() = TargetRot;
				break;
			}

			case EKinematicTargetMode::Velocity:
			{
				// Move based on velocity
				Particle.X() = Particle.X() + Particle.V() * Dt;
				FRotation3::IntegrateRotationWithAngularVelocity(Particle.R(), Particle.W(), Dt);
				break;
			}
			}

			// Update world space bouunds
			if (Particle.HasBounds())
			{
				const TAABB<FReal, 3>& LocalBounds = Particle.LocalBounds();
				
				TAABB<FReal, 3> WorldSpaceBounds = LocalBounds.TransformedAABB(FRigidTransform3(Particle.X(), Particle.R()));
				WorldSpaceBounds.ThickenSymmetrically(WorldSpaceBounds.Extents() * BoundsExtension);

				//TAABB<FReal, 3> PrevWorldSpaceBounds = LocalBounds.TransformedAABB(FRigidTransform3(PrevX, PrevR));
				//WorldSpaceBounds.GrowToInclude(PrevWorldSpaceBounds);

				Particle.SetWorldSpaceInflatedBounds(WorldSpaceBounds);
			}
		}
	}

	void FPBDMinEvolution::DetectCollisions(FReal Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_DetectCollisions);

		// @todo(ccaulfield): doesn't need to be every frame
		PrioritizedConstraintRules = ConstraintRules;
		PrioritizedConstraintRules.StableSort();

		for (FSimpleConstraintRule* ConstraintRule : PrioritizedConstraintRules)
		{
			ConstraintRule->UpdatePositionBasedState(Dt);
		}

		CollisionDetector.DetectCollisions(Dt);
	}

	void FPBDMinEvolution::PrepareConstraints(FReal Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_PrepareConstraints);

		for (FSimpleConstraintRule* ConstraintRule : PrioritizedConstraintRules)
		{
			ConstraintRule->PrepareConstraints(Dt);
		}
	}

	void FPBDMinEvolution::UnprepareConstraints(FReal Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_PrepareConstraints);

		for (FSimpleConstraintRule* ConstraintRule : PrioritizedConstraintRules)
		{
			ConstraintRule->UnprepareConstraints(Dt);
		}
	}

	void FPBDMinEvolution::ApplyConstraints(FReal Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_ApplyConstraints);

		for (int32 i = 0; i < NumApplyIterations; ++i)
		{
			bool bNeedsAnotherIteration = false;
			for (FSimpleConstraintRule* ConstraintRule : PrioritizedConstraintRules)
			{
				bNeedsAnotherIteration |= ConstraintRule->ApplyConstraints(Dt, i, NumApplyIterations);
			}

			if (!bNeedsAnotherIteration)
			{
				break;
			}
		}
	}

	void FPBDMinEvolution::UpdateVelocities(FReal Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_UpdateVelocites);

		TPerParticlePBDUpdateFromDeltaPosition<FReal, 3> UpdateVelocityRule;
		for (auto& Particle : Particles.GetActiveParticlesView())
		{
			UpdateVelocityRule.Apply(Particle, Dt);
		}
	}

	void FPBDMinEvolution::ApplyPushOutConstraints(FReal Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_ApplyPushOut);

		for (int32 It = 0; It < NumApplyPushOutIterations; ++It)
		{
			bool bNeedsAnotherIteration = false;
			for (FSimpleConstraintRule* ConstraintRule : PrioritizedConstraintRules)
			{
				bNeedsAnotherIteration |= ConstraintRule->ApplyPushOut(Dt, It, NumApplyPushOutIterations);
			}

			if (!bNeedsAnotherIteration)
			{
				break;
			}
		}
	}

	void FPBDMinEvolution::UpdatePositions(FReal Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_UpdatePositions);
		for (auto& Particle : Particles.GetActiveParticlesView())
		{
			Particle.X() = Particle.P();
			Particle.R() = Particle.Q();
		}
	}

}
