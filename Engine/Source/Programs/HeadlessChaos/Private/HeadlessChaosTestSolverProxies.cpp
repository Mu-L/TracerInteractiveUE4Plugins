// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaosTestSolverProxies.h"
#include "HeadlessChaos.h"
#include "HeadlessChaosTestUtility.h"


#include "Chaos/ParticleHandle.h"
#include "Chaos/ErrorReporter.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "Chaos/Utilities.h"
#include "PBDRigidsSolver.h"
#include "ChaosSolversModule.h"

#include "Modules/ModuleManager.h"


namespace ChaosTest {

    using namespace Chaos;

	template<typename Traits, class T>
	void SingleParticleProxySingleThreadTest()
	{
		auto Sphere = TSharedPtr<FImplicitObject, ESPMode::ThreadSafe>(new TSphere<float, 3>(TVector<float, 3>(0), 10));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();

		// Make a solver
		auto* Solver = Module->CreateSolver<Traits>(nullptr);
		
		// Make a particle

		TUniquePtr<Chaos::TPBDRigidParticle<float, 3>> Particle = Chaos::TPBDRigidParticle<float, 3>::CreateParticle();
		Particle->SetGeometry(Sphere);
		Particle->SetX(TVector<float, 3>(0, 0, 0));
		Particle->SetGravityEnabled(false);
		Solver->RegisterObject(Particle.Get());

		Particle->SetV(TVector<float, 3>(0, 0, 10));
		Solver->AddDirtyProxy(Particle->GetProxy());

		::ChaosTest::SetParticleSimDataToCollide({ Particle.Get() });

		Solver->AdvanceAndDispatch_External(100.0f);

		Solver->BufferPhysicsResults();
		Solver->FlipBuffers();
		Solver->UpdateGameThreadStructures();

		// Make sure game thread data has changed
		TVector<float, 3> V = Particle->V();
		EXPECT_EQ(V.X, 0.f);
		EXPECT_GT(V.Z, 0.f);

		TVector<float, 3> X = Particle->X();
		EXPECT_EQ(X.X, 0.f);
		EXPECT_GT(X.Z, 0.f);

		// Throw out the proxy
		Solver->UnregisterObject(Particle.Get());

		Module->DestroySolver(Solver);		
	}

	template<typename Traits, class T>
	void SingleParticleProxyTaskGraphTest()
	{
		//
		// DISABLED TEST
		//
		// There is currently no way to execute a TaskGraph or DedicatedThread simulation in a unit test.
		// This test should be enabled when TaskGraph simulation is supported for unit tests.
		//

		auto Sphere = TSharedPtr<FImplicitObject, ESPMode::ThreadSafe>(new TSphere<float, 3>(TVector<float, 3>(0), 10));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();

		// Make a solver
		auto* Solver = Module->CreateSolver<Traits>(nullptr, EThreadingMode::DedicatedThread);
		
		// Make a particle

		TUniquePtr<Chaos::TPBDRigidParticle<float, 3>> Particle = Chaos::TPBDRigidParticle<float, 3>::CreateParticle();
		Particle->SetGeometry(Sphere);
		Particle->SetX(TVector<float, 3>(0, 0, 0));
		Solver->RegisterObject(Particle.Get());

		Particle->SetV(TVector<float, 3>(0, 0, 10));
		Solver->AddDirtyProxy(Particle->GetProxy());

		int32 Counter = 0;
		while (Particle->X().Size() == 0.f)
		{
			// This might not be the correct way to advance when using the TaskGraph.
			//TODO: use event returned
			Solver->AdvanceAndDispatch_External(100.0f);


			Solver->BufferPhysicsResults();
			Solver->FlipBuffers();
			Solver->UpdateGameThreadStructures();

			EXPECT_LE(Counter++, 5);
		}

		// Make sure game thread data has changed
		TVector<float, 3> V = Particle->V();
		EXPECT_EQ(V.X, 0.f);
		EXPECT_GT(V.Z, 0.f);

		TVector<float, 3> X = Particle->X();
		EXPECT_EQ(X.X, 0.f);
		EXPECT_GT(X.Z, 0.f);

		// Throw out the proxy
		Solver->UnregisterObject(Particle.Get());

		Module->DestroySolver(Solver);
	}


	template<typename Traits, class T>
	void SingleParticleProxyWakeEventPropergationTest()
	{
		using namespace Chaos;
		auto Sphere = TSharedPtr<FImplicitObject, ESPMode::ThreadSafe>(new TSphere<float, 3>(TVector<float, 3>(0), 10));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();

		// Make a solver
		auto* Solver = Module->CreateSolver<Traits>(nullptr);
		
		// Make a particle

		TUniquePtr<TPBDRigidParticle<float, 3>> Particle = TPBDRigidParticle<float, 3>::CreateParticle();
		Particle->SetGeometry(Sphere);
		Particle->SetX(TVector<float, 3>(0, 0, 220));
		Particle->SetV(TVector<float, 3>(0, 0, -10));
		Solver->RegisterObject(Particle.Get());
		Solver->AddDirtyProxy(Particle->GetProxy());

		TUniquePtr<TPBDRigidParticle<float, 3>> Particle2 = TPBDRigidParticle<float, 3>::CreateParticle();
		Particle2->SetGeometry(Sphere);
		Particle2->SetX(TVector<float, 3>(0, 0, 100));
		Particle2->SetV(TVector<float, 3>(0, 0, 0));
		Solver->RegisterObject(Particle2.Get());
		Solver->AddDirtyProxy(Particle2->GetProxy());
		Particle2->SetObjectState(Chaos::EObjectStateType::Sleeping);

		::ChaosTest::SetParticleSimDataToCollide({ Particle.Get(),Particle2.Get() });

		// let top paticle collide and wake up second particle
		int32 LoopCount = 0;
		while (Particle2->GetWakeEvent() == EWakeEventEntry::None && LoopCount++ < 20)
		{
			Solver->AdvanceAndDispatch_External(100.0f);

			Solver->BufferPhysicsResults();
			Solver->FlipBuffers();
			Solver->UpdateGameThreadStructures();
		}

		// Make sure game thread data has changed
		TVector<float, 3> V = Particle->V();
		EXPECT_EQ(Particle->GetWakeEvent(), EWakeEventEntry::None);
		EXPECT_EQ(Particle->ObjectState(), Chaos::EObjectStateType::Dynamic);

		EXPECT_EQ(Particle2->GetWakeEvent(), EWakeEventEntry::Awake);
		EXPECT_EQ(Particle2->ObjectState(), Chaos::EObjectStateType::Dynamic);

		Particle2->ClearEvents();
		EXPECT_EQ(Particle2->GetWakeEvent(), EWakeEventEntry::None);

		// Throw out the proxy
		Solver->UnregisterObject(Particle.Get());

		Module->DestroySolver(Solver);
	}

	TYPED_TEST(AllTraits, SingleParticleProxyTests)
	{
		ChaosTest::SingleParticleProxySingleThreadTest<TypeParam,float>();
		ChaosTest::SingleParticleProxyWakeEventPropergationTest<TypeParam,float>();
	}

	TYPED_TEST(AllTraits,DISABLED_SingleParticleProxyTests)
	{
		ChaosTest::SingleParticleProxyTaskGraphTest<TypeParam,float>();
	}

}
