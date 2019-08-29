// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDRigidsEvolutionPGS.h"

#include "Chaos/Box.h"
#include "Chaos/Defines.h"
#include "Chaos/Framework/Parallel.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/PBDCollisionConstraint.h"
#include "Chaos/PBDCollisionConstraintPGS.h"
#include "Chaos/PBDCollisionSpringConstraints.h"
#include "Chaos/PerParticleEtherDrag.h"
#include "Chaos/PerParticleEulerStepVelocity.h"
#include "Chaos/PerParticleGravity.h"
#include "Chaos/PerParticleInitForce.h"
#include "Chaos/PerParticlePBDEulerStep.h"
#include "Chaos/PerParticlePBDGroundConstraint.h"
#include "Chaos/PerParticlePBDUpdateFromDeltaPosition.h"
#include "ChaosStats.h"

#include "ProfilingDebugging/ScopedTimers.h"
#include "Chaos/DebugDrawQueue.h"

#define LOCTEXT_NAMESPACE "Chaos"

using namespace Chaos;

template<class T, int d>
TPBDRigidsEvolutionPGS<T, d>::TPBDRigidsEvolutionPGS(TPBDRigidParticles<T, d>&& InParticles, int32 NumIterations)
    : Base(MoveTemp(InParticles), NumIterations)
{
	SetParticleUpdateVelocityFunction([PBDUpdateRule = TPerParticlePBDUpdateFromDeltaPosition<float, 3>(), this](TPBDRigidParticles<T, d>& MParticlesInput, const T Dt, const TArray<int32>& ActiveIndices) {
		PhysicsParallelFor(ActiveIndices.Num(), [&](int32 ActiveIndex) {
			int32 Index = ActiveIndices[ActiveIndex];
			PBDUpdateRule.Apply(MParticlesInput, Dt, Index);
		});
	});

	SetParticleUpdatePositionFunction([this](TPBDRigidParticles<T, d>& ParticlesInput, const T Dt)
	{
		PhysicsParallelFor(MActiveIndicesArray.Num(), [&](int32 ActiveIndex)
		{
			int32 Index = MActiveIndicesArray[ActiveIndex];
			ParticlesInput.X(Index) = ParticlesInput.P(Index);
			ParticlesInput.R(Index) = ParticlesInput.Q(Index);
		});
	});
}

template<class T, int d>
void TPBDRigidsEvolutionPGS<T, d>::IntegrateV(const TArray<int32>& ActiveIndices, const T Dt)
{	
	TPerParticleInitForce<T, d> InitForceRule;
	TPerParticleEulerStepVelocity<T, d> EulerStepVelocityRule;

	PhysicsParallelFor(ActiveIndices.Num(), [&](int32 ActiveIndex) {
		int32 Index = ActiveIndices[ActiveIndex];
		check(!MParticles.Disabled(Index) && !MParticles.Sleeping(Index));

		//save off previous velocities
		MParticles.PreV(Index) = MParticles.V(Index);
		MParticles.PreW(Index) = MParticles.W(Index);

		InitForceRule.Apply(MParticles, Dt, Index);
		for (auto ForceRule : MForceRules)
		{
			ForceRule(MParticles, Dt, Index);
		}
		EulerStepVelocityRule.Apply(MParticles, Dt, Index);
	});
}

template<class T, int d>
void TPBDRigidsEvolutionPGS<T, d>::IntegrateX(const TArray<int32>& ActiveIndices, const T Dt)
{	
	TPerParticleEtherDrag<T, d> EtherDragRule(0.0, 0.0);
	TPerParticlePBDEulerStep<T, d> EulerStepRule;
	PhysicsParallelFor(ActiveIndices.Num(), [&](int32 ActiveIndex) {
		int32 Index = ActiveIndices[ActiveIndex];
		EtherDragRule.Apply(MParticles, Dt, Index);
		EulerStepRule.Apply(MParticles, Dt, Index);
	});
}

template<class T, int d>
void TPBDRigidsEvolutionPGS<T, d>::AdvanceOneTimeStep(const T Dt)
{
	UE_LOG(LogChaos, Verbose, TEXT("START FRAME with Dt %f"), Dt);
	double FrameTime = 0, Time = 0;

	TPBDCollisionConstraintPGS<T, d> CollisionRule(MParticles, MCollided, MPushOutIterations, MPushOutPairIterations, (T)0, MRestitution, MFriction);
	MActiveIndicesArray = MActiveIndices.Array();

	IntegrateV(MActiveIndicesArray, Dt);
	CollisionRule.ComputeConstraints(MParticles, Dt);
	CollisionRule.UpdateIslandsFromConstraints(MParticles, MIslandParticles, IslandSleepCounts, MActiveIndices);

	TArray<bool> SleepedIslands;
	SleepedIslands.SetNum(MIslandParticles.Num());
	PhysicsParallelFor(MIslandParticles.Num(), [&](int32 Island) {
		TArray<int32> ActiveIndices = MIslandParticles[Island].Array();
		CollisionRule.Apply(MParticles, Dt, Island);
		IntegrateX(ActiveIndices, Dt);
		/*for (int i = 0; i < MNumIterations; ++i)
		{
			for (auto ConstraintRule : MConstraintRules)
			{
				ConstraintRule(MParticles, Dt);
			}
		}*/
		CollisionRule.ApplyPushOut(MParticles, Dt, ActiveIndices, Island);
		MParticleUpdateVelocity(MParticles, Dt, ActiveIndices);
		// Turn off if not moving
		SleepedIslands[Island] = CollisionRule.SleepInactive(MParticles, ActiveIndices, IslandSleepCounts[Island], Island, SleepLinearThreshold, SleepAngularThreshold);
	});

	for (int32 i = 0; i < MIslandParticles.Num(); ++i)
	{
		if (SleepedIslands[i])
		{
			for (const int32 Index : MIslandParticles[i])
			{
				MActiveIndices.Remove(Index);
			}
		}
	}

	CollisionRule.CopyOutConstraints(MIslandParticles.Num());

	MParticleUpdatePosition(MParticles, Dt);

#if CHAOS_DEBUG_DRAW
	if (FDebugDrawQueue::IsDebugDrawingEnabled())
	{
		for (uint32 Idx = 0; Idx < MParticles.Size(); ++Idx)
		{
			if (MParticles.Disabled(Idx)) { continue; }
			if (MParticles.CollisionParticles(Idx))
			{
				for (uint32 CollisionIdx = 0; CollisionIdx < MParticles.CollisionParticles(Idx)->Size(); ++CollisionIdx)
				{
					const TVector<T, d>& X = MParticles.CollisionParticles(Idx)->X(CollisionIdx);
					const TVector<T, d> WorldX = TRigidTransform<T, d>(MParticles.X(Idx), MParticles.R(Idx)).TransformPosition(X);
					FDebugDrawQueue::GetInstance().DrawDebugPoint(WorldX, FColor::Purple, false, 1e-4, 0, 10.f);
				}
			}
			
		}
	}
#endif

	MTime += Dt;
}

template class Chaos::TPBDRigidsEvolutionPGS<float, 3>;

#undef LOCTEXT_NAMESPACE
