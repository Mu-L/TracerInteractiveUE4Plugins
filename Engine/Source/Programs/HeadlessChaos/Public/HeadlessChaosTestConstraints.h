// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "HeadlessChaosTestUtility.h"
#include "Modules/ModuleManager.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/Sphere.h"
#include "Chaos/Utilities.h"

namespace ChaosTest 
{
	/**
	 * Base class for constraint tests. Provides a basic sim with no builtin constraint support.
	 */
	template <typename TEvolution>
	class FConstraintsTest
	{
	public:

		FConstraintsTest(const int32 NumIterations, const FReal Gravity)
			: Evolution(SOAs, PhysicalMaterials)
		{
			PhysicalMaterial = MakeUnique<FChaosPhysicsMaterial>();
			PhysicalMaterial->Friction = 0;
			PhysicalMaterial->Restitution = 0;
			PhysicalMaterial->SleepingLinearThreshold = 0;
			PhysicalMaterial->SleepingAngularThreshold = 0;
			PhysicalMaterial->DisabledLinearThreshold = 0;
			PhysicalMaterial->DisabledAngularThreshold = 0;

			Evolution.SetNumIterations(NumIterations);
			Evolution.GetGravityForces().SetAcceleration(Gravity * FVec3(0, 0, -1));
		}

		virtual ~FConstraintsTest()
		{
		}

		auto AddParticleBox(const FVec3& Position, const FRotation3& Rotation, const FVec3& Size, FReal Mass)
		{
			TGeometryParticleHandle<FReal, 3>& Particle = Mass > SMALL_NUMBER ? *AppendDynamicParticleBox<FReal>(SOAs, Size) : *AppendStaticParticleBox<FReal>(SOAs, Size);

			ResetParticle(&Particle, Position, Rotation, FVec3(0), FVec3(0));

			auto PBDParticlePtr = Particle.CastToRigidParticle();
			if(PBDParticlePtr && PBDParticlePtr->ObjectState() == EObjectStateType::Dynamic)
			{
				auto& PBDParticle = *PBDParticlePtr;
				PBDParticle.M() = PBDParticle.M() * Mass;
				PBDParticle.I() = PBDParticle.I() * Mass;
				PBDParticle.InvM() = PBDParticle.InvM() * ((FReal)1 / Mass);
				PBDParticle.InvI() = PBDParticle.InvI() * ((FReal)1 / Mass);
			}
			Evolution.SetPhysicsMaterial(&Particle, MakeSerializable(PhysicalMaterial));

			return &Particle;
		}

		void ResetParticle(TGeometryParticleHandle<FReal, 3>* Particle, const FVec3& Position, const FRotation3& Rotation, const FVec3& Velocity, const FVec3& AngularVelocity)
		{
			Particle->X() = Position;
			Particle->R() = Rotation;
			if (auto KinParticle = Particle->CastToKinematicParticle())
			{
				KinParticle->V() = Velocity;
				KinParticle->W() = AngularVelocity;
			}
			if (auto PBDParticle = Particle->CastToRigidParticle())
			{
				PBDParticle->P() = Position;
				PBDParticle->Q() = Rotation;
			}
		}

		// Solver state
		TPBDRigidsSOAs<FReal, 3> SOAs;
		TEvolution Evolution;
		TUniquePtr<FChaosPhysicsMaterial> PhysicalMaterial;
		THandleArray<FChaosPhysicsMaterial> PhysicalMaterials;

		TGeometryParticleHandle<FReal, 3>* GetParticle(const int32 Idx)
		{
			return SOAs.GetParticleHandles().Handle(Idx).Get();
		}
	};

}