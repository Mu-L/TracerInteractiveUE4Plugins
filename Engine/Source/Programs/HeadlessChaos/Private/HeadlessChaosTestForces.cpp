// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaosTestForces.h"

#include "HeadlessChaos.h"
#include "HeadlessChaosTestUtility.h"
#include "Modules/ModuleManager.h"
#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/Sphere.h"
#include "Chaos/Utilities.h"

namespace ChaosTest
{
	template<typename TEvolution, typename T>
	void Gravity()
	{
		TPBDRigidsSOAs<T, 3> Particles;
		THandleArray<FChaosPhysicsMaterial> PhysicalMaterials;
		TEvolution Evolution(Particles, PhysicalMaterials);
		
		TArray<TPBDRigidParticleHandle<T,3>*> Dynamics = Evolution.CreateDynamicParticles(1);
		Evolution.AdvanceOneTimeStep(0.1);
		EXPECT_LT(Dynamics[0]->X()[2], 0);
	}
	
	TYPED_TEST(AllEvolutions,Forces)
	{
		ChaosTest::Gravity<TypeParam,float>();
		SUCCEED();
	}
}