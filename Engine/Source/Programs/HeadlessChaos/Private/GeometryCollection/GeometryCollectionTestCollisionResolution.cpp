// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionTestCollisionResolution.h"
#include "GeometryCollection/GeometryCollectionTestFramework.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollectionProxyData.h"
#include "HeadlessChaosTestUtility.h"

namespace GeometryCollectionTest
{
	using namespace ChaosTest;
	TYPED_TEST(AllTraits, GeometryCollection_CollisionResolutionTest)
	{
		using Traits = TypeParam;
		TFramework<Traits> UnitTest;

		TGeometryCollectionWrapper<Traits>* Collection = nullptr;
		{
			FVector GlobalTranslation(0, 0, 10); 
			FQuat GlobalRotation = FQuat::MakeFromEuler(FVector(0));
			
			CreationParameters Params; 
			Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Dynamic; 
			Params.EnableClustering = false;
			Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Sphere;  
			Params.SimplicialType = ESimplicialType::Chaos_Simplicial_Sphere; 
			Params.CollisionType = ECollisionTypeEnum::Chaos_Volumetric;
			Params.RootTransform = FTransform(GlobalRotation, GlobalTranslation); 
			Params.NestedTransforms = { FTransform::Identity, FTransform::Identity, FTransform::Identity };
			
			Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();
			EXPECT_EQ(Collection->DynamicCollection->Parent[0], 1); // is a child of index one
			EXPECT_TRUE(Collection->DynamicCollection->MassToLocal[0].Equals(FTransform::Identity)); // we are not testing MassToLocal in this test
			
			UnitTest.AddSimulationObject(Collection);
		}

		RigidBodyWrapper* Floor = TNewSimulationObject<GeometryType::RigidFloor>::Init<Traits>()->template As<RigidBodyWrapper>();
		UnitTest.AddSimulationObject(Floor);

		UnitTest.Initialize();
		for (int i = 0; i < 10000; i++) 
		{
			UnitTest.Advance();
		}
		{
			// validate that Simplicials are null when CollisionType==Chaos_Volumetric
			EXPECT_EQ(Collection->DynamicCollection->Transform.Num(), 4);
			EXPECT_EQ(Collection->DynamicCollection->Simplicials[0], nullptr);
			EXPECT_EQ(Collection->DynamicCollection->Simplicials[1], nullptr);
			EXPECT_EQ(Collection->DynamicCollection->Simplicials[2], nullptr);
			EXPECT_EQ(Collection->DynamicCollection->Simplicials[3], nullptr);
			EXPECT_EQ(UnitTest.Solver->GetParticles().GetGeometryCollectionParticles().CollisionParticles(0), nullptr);

			EXPECT_LT(FMath::Abs(Collection->RestCollection->Transform[0].GetTranslation().Z)-10.f, KINDA_SMALL_NUMBER);
			EXPECT_LT(FMath::Abs(Collection->DynamicCollection->Transform[0].GetTranslation().Z - 1.0), 0.1);
		}
	}

	TYPED_TEST(AllTraits,GeometryCollection_CollisionResolution_SimplicialSphereToPlane)
	{
		using Traits = TypeParam;
		TFramework<Traits> UnitTest;
		const FReal Radius = 100.0f; // cm

		TGeometryCollectionWrapper<Traits>* Collection = nullptr;
		{
			
			FVector GlobalTranslation(0, 0, Radius + 10); FQuat GlobalRotation = FQuat::MakeFromEuler(FVector(0));
			CreationParameters Params; Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Dynamic; Params.EnableClustering = false;
			Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Sphere;  Params.SimplicialType = ESimplicialType::Chaos_Simplicial_Sphere; Params.CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			Params.RootTransform = FTransform(GlobalRotation, GlobalTranslation); Params.NestedTransforms = { FTransform::Identity, FTransform::Identity, FTransform::Identity };
			FVector Scale(Radius);
			Params.GeomTransform.SetScale3D(Scale); // Sphere radius
			Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();
			EXPECT_EQ(Collection->DynamicCollection->Parent[0], 1); // is a child of index one
			EXPECT_TRUE(Collection->DynamicCollection->MassToLocal[0].Equals(FTransform::Identity)); // we are not testing MassToLocal in this test

			UnitTest.AddSimulationObject(Collection);
		}

		RigidBodyWrapper* Floor = TNewSimulationObject<GeometryType::RigidFloor>::Init<Traits>()->template As<RigidBodyWrapper>();
		UnitTest.AddSimulationObject(Floor);

		UnitTest.Initialize();
		for (int i = 0; i < 1000; i++)
		{
			UnitTest.Advance();
		}
		{
			// validate that Simplicials are null when CollisionType==Chaos_Volumetric
			EXPECT_EQ(Collection->DynamicCollection->Transform.Num(), 4);
			EXPECT_TRUE(Collection->DynamicCollection->Simplicials[0]!=nullptr);
			EXPECT_TRUE(UnitTest.Solver->GetParticles().GetGeometryCollectionParticles().CollisionParticles(0)!=nullptr);
			EXPECT_TRUE(Collection->DynamicCollection->Simplicials[0]->Size() == UnitTest.Solver->GetParticles().GetGeometryCollectionParticles().CollisionParticles(0)->Size());
			EXPECT_TRUE(Collection->DynamicCollection->Simplicials[0]->Size() != 0);

			EXPECT_LT(FMath::Abs(Collection->RestCollection->Transform[0].GetTranslation().Z - (Radius + 10.f)), KINDA_SMALL_NUMBER);
			// ball settles within 10% of radius (The ball will sink deeper than expected due to contact position averaging within CullDistance)
			EXPECT_LT(FMath::Abs(Collection->DynamicCollection->Transform[0].GetTranslation().Z - Radius), Radius * 0.1f); 
		}
	}



	TYPED_TEST(AllTraits,GeometryCollection_CollisionResolution_AnalyticSphereToAnalyticSphere)
	{
		using Traits = TypeParam;
		// simplicial sphere to implicit sphere
		TFramework<Traits> UnitTest;

		CreationParameters Params;
		Params.CollisionType = ECollisionTypeEnum::Chaos_Volumetric;

		Params.EnableClustering = false;

		FVector Scale(1.f);
		Params.GeomTransform.SetScale3D(Scale); // Sphere radius
		Params.EnableClustering = false;

		// Make a dynamic simplicial sphere
		Params.SimplicialType = ESimplicialType::Chaos_Simplicial_Sphere;
		//Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_None; // Fails, falls right through
		Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Sphere;

		Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Dynamic;
		Params.RootTransform =
			FTransform(FQuat::MakeFromEuler(FVector(0)), FVector(0, 0, 3.0));
		TGeometryCollectionWrapper<Traits>* SimplicialSphereCollection =
			TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();
		UnitTest.AddSimulationObject(SimplicialSphereCollection);

		// Make a kinematic implicit sphere
		Params.SimplicialType = ESimplicialType::Chaos_Simplicial_Sphere;
		Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Sphere;

		Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Kinematic;
		Params.RootTransform =
			FTransform(FQuat::MakeFromEuler(FVector(0)), FVector(0));
		TGeometryCollectionWrapper<Traits>* ImplicitSphereCollection =
			TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();
		UnitTest.AddSimulationObject(ImplicitSphereCollection);

		// Hard code masstolocal on rest collection to identity
		{
			TManagedArray<FTransform>& MassToLocal =
				SimplicialSphereCollection->RestCollection->template GetAttribute<FTransform>(
					TEXT("MassToLocal"), FTransformCollection::TransformGroup);
			check(MassToLocal.Num() == 1);
			MassToLocal[0] = FTransform::Identity;
		}
		{
			TManagedArray<FTransform>& MassToLocal =
				ImplicitSphereCollection->RestCollection->template GetAttribute<FTransform>(
					TEXT("MassToLocal"), FTransformCollection::TransformGroup);
			check(MassToLocal.Num() == 1);
			MassToLocal[0] = FTransform::Identity;
		}

		UnitTest.Initialize();
		EXPECT_EQ(
			SimplicialSphereCollection->DynamicCollection->Transform[0].GetTranslation().Z,
			ImplicitSphereCollection->DynamicCollection->Transform[0].GetTranslation().Z + 3);

		const FVector FirstX = SimplicialSphereCollection->DynamicCollection->Transform[0].GetTranslation();
		FVector PrevX = FirstX;
		for (int i = 0; i < 10; i++)
		{
			UnitTest.Advance();

			const FVector& CurrX = SimplicialSphereCollection->DynamicCollection->Transform[0].GetTranslation();
			EXPECT_NE(CurrX.Z, FirstX.Z); // moved since init
			EXPECT_GE(PrevX.Z - CurrX.Z, -KINDA_SMALL_NUMBER); // falling in -Z, or stopped
			EXPECT_LE(FMath::Abs(CurrX.X), KINDA_SMALL_NUMBER); // straight down
			EXPECT_LE(FMath::Abs(CurrX.Y), KINDA_SMALL_NUMBER); // straight down
			PrevX = CurrX;
		}

		{
			// We expect the simplical sphere to drop by 0.1 in Z and come to rest
			// on top of the implicit sphere.
			const FVector& CurrX = SimplicialSphereCollection->DynamicCollection->Transform[0].GetTranslation();
			EXPECT_LE(CurrX.Z - 2.0, 0.2); // Relative large fudge factor accounts for aliasing?
		}
	}

	TYPED_TEST(AllTraits, DISABLED_GeometryCollection_CollisionResolution_AnalyticCubeToAnalyticCube)
	{
		using Traits = TypeParam;

		// simplicial sphere to implicit sphere
		TFramework<Traits> UnitTest;

		CreationParameters Params;
		Params.EnableClustering = false;

		FVector Scale(1.f, 1.f, 1.f);
		Params.GeomTransform.SetScale3D(Scale); // Box dimensions
		Params.EnableClustering = false;

		// Make a dynamic box
		Params.SimplicialType = ESimplicialType::Chaos_Simplicial_Box;
		Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
		Params.CollisionType = ECollisionTypeEnum::Chaos_Volumetric;

		Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Dynamic;
		Params.RootTransform = FTransform(FQuat::MakeFromEuler(FVector(0)), FVector(0, 0, 3.0));
		TGeometryCollectionWrapper<Traits>* BoxCollection0 =
			TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();
		UnitTest.AddSimulationObject(BoxCollection0);

		// Make a kinematic box
		Params.SimplicialType = ESimplicialType::Chaos_Simplicial_Box;
		Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
		Params.CollisionType = ECollisionTypeEnum::Chaos_Volumetric;

		Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Kinematic;
		Params.RootTransform = FTransform(FQuat::MakeFromEuler(FVector(0)), FVector(0));
		TGeometryCollectionWrapper<Traits>* BoxCollection1 =
			TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();
		UnitTest.AddSimulationObject(BoxCollection1);
/*
		// Hard code masstolocal on rest collection to identity
		{
			TManagedArray<FTransform>& MassToLocal =
				BoxCollection0->RestCollection->GetAttribute<FTransform>(
					TEXT("MassToLocal"), FTransformCollection::TransformGroup);
			check(MassToLocal.Num() == 1);
			MassToLocal[0] = FTransform::Identity;
		}
		{
			TManagedArray<FTransform>& MassToLocal =
				BoxCollection1->RestCollection->GetAttribute<FTransform>(
					TEXT("MassToLocal"), FTransformCollection::TransformGroup);
			check(MassToLocal.Num() == 1);
			MassToLocal[0] = FTransform::Identity;
		}
*/
		UnitTest.Initialize();
		EXPECT_EQ(
			BoxCollection0->DynamicCollection->Transform[0].GetTranslation().Z,
			BoxCollection1->DynamicCollection->Transform[0].GetTranslation().Z + 3);

		const FVector FirstX = BoxCollection0->DynamicCollection->Transform[0].GetTranslation();
		FVector PrevX = FirstX;
		for (int i = 0; i < 10; i++)
		{
			UnitTest.Advance();

			const FVector& CurrX = BoxCollection0->DynamicCollection->Transform[0].GetTranslation();
			EXPECT_NE(CurrX.Z, FirstX.Z); // moved since init
			EXPECT_LE(CurrX.Z, PrevX.Z); // falling in -Z, or stopped
			EXPECT_LE(FMath::Abs(CurrX.X), KINDA_SMALL_NUMBER); // No deflection
			EXPECT_LE(FMath::Abs(CurrX.Y), KINDA_SMALL_NUMBER); // No deflection
			PrevX = CurrX;
		}

		{
			// We expect the simplical sphere to drop by 0.1 in Z and come to rest
			// on top of the implicit sphere.
			const FVector& CurrX = BoxCollection0->DynamicCollection->Transform[0].GetTranslation();
			EXPECT_LE(CurrX.Z - 2.0, 0.2); // Relative large fudge factor accounts for aliasing?
		}

	}


	template<typename Traits>
	void CollisionResolution_SimplicialCubeToAnalyticCube()
	{

	}


	TYPED_TEST(AllTraits,GeometryCollection_CollisionResolution_SimplicialSphereToAnalyticSphere)
	{
		using Traits = TypeParam;
		TFramework<Traits> UnitTest;
		// This should exercise CollisionResolution::ConstructLevelsetLevelsetConstraints(...) with ispc:SampleSphere* (Paticle to Analytic Sphere)

		TGeometryCollectionWrapper<Traits>* Collection = nullptr;
		{
			FVector GlobalTranslation(0, 0, 10); 
			FQuat GlobalRotation = FQuat::MakeFromEuler(FVector(0));

			CreationParameters Params; 
			Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Dynamic; 
			Params.EnableClustering = false;
			Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_LevelSet;
			Params.SimplicialType = ESimplicialType::Chaos_Simplicial_Sphere;
			Params.CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			Params.RootTransform = FTransform(GlobalRotation, GlobalTranslation); 
			Params.NestedTransforms = { FTransform::Identity, FTransform::Identity, FTransform::Identity };

			Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();
			
			EXPECT_EQ(Collection->DynamicCollection->Parent[0], 1); // is a child of index one
			EXPECT_TRUE(Collection->DynamicCollection->MassToLocal[0].Equals(FTransform::Identity)); // we are not testing MassToLocal in this test

			UnitTest.AddSimulationObject(Collection);
		}

		TGeometryCollectionWrapper<Traits>* CollectionStaticSphere = nullptr;
		{
			FVector GlobalTranslation(0, 0, 0);
			FQuat GlobalRotation = FQuat::MakeFromEuler(FVector(0));
			CreationParameters Params;
			Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Static; 
			Params.EnableClustering = false;
			Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Sphere;
			Params.SimplicialType = ESimplicialType::Chaos_Simplicial_Sphere;
			Params.CollisionType = ECollisionTypeEnum::Chaos_Volumetric;
			Params.RootTransform = FTransform(GlobalRotation, GlobalTranslation); Params.NestedTransforms = { FTransform::Identity, FTransform::Identity, FTransform::Identity };
			CollectionStaticSphere = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();
			EXPECT_EQ(CollectionStaticSphere->DynamicCollection->Parent[0], 1); // is a child of index one
			EXPECT_TRUE(CollectionStaticSphere->DynamicCollection->MassToLocal[0].Equals(FTransform::Identity)); // we are not testing MassToLocal in this test

			UnitTest.AddSimulationObject(CollectionStaticSphere);
		}

		UnitTest.Initialize();

		for (int i = 0; i < 20; i++)
		{
			UnitTest.Advance();
		}
		{
			// validate simplicials and implicits are configured correctly
			EXPECT_EQ(Collection->DynamicCollection->Transform.Num(), 4);
			EXPECT_TRUE(Collection->DynamicCollection->Simplicials[0] != nullptr);
			EXPECT_TRUE(UnitTest.Solver->GetParticles().GetGeometryCollectionParticles().CollisionParticles(0) != nullptr);
			EXPECT_TRUE(Collection->DynamicCollection->Simplicials[0]->Size() == UnitTest.Solver->GetParticles().GetGeometryCollectionParticles().CollisionParticles(0)->Size());
			EXPECT_TRUE(Collection->DynamicCollection->Simplicials[0]->Size() != 0);
			EXPECT_TRUE(Collection->DynamicCollection->Implicits[0]->GetType() == (int32)Chaos::ImplicitObjectType::LevelSet);

			EXPECT_EQ(CollectionStaticSphere->DynamicCollection->Transform.Num(), 4);
			EXPECT_TRUE(CollectionStaticSphere->DynamicCollection->Simplicials[0] == nullptr);
			EXPECT_TRUE(CollectionStaticSphere->DynamicCollection->Implicits[0]->GetType() == (int32)Chaos::ImplicitObjectType::Sphere);

			// validate the ball collides and moved away from the static ball
			EXPECT_LT(FMath::Abs(Collection->RestCollection->Transform[0].GetTranslation().Z) - 10.f, KINDA_SMALL_NUMBER);
			EXPECT_TRUE(FMath::Abs(Collection->DynamicCollection->Transform[0].GetTranslation().X) < 0.001); // No deflection
			EXPECT_TRUE(FMath::Abs(Collection->DynamicCollection->Transform[0].GetTranslation().Y) < 0.001); // No deflection
			EXPECT_LT(Collection->DynamicCollection->Transform[0].GetTranslation().Z, 2.1f); // ball fell
		}
	}


	TYPED_TEST(AllTraits,GeometryCollection_CollisionResolution_SimplicialSphereToImplicitSphere)
	{
		using Traits = TypeParam;
		
		// simplicial sphere to implicit sphere
		TFramework<Traits> UnitTest;

		CreationParameters Params; 
		Params.EnableClustering = false;

		FReal Radius = 100.0f;
		FVector Scale(Radius);
		Params.GeomTransform.SetScale3D(Scale); // Sphere radius
		Params.EnableClustering = false;

		// Make a dynamic simplicial sphere
		Params.SimplicialType = ESimplicialType::Chaos_Simplicial_Sphere;
		Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Sphere;
		Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Dynamic;
		Params.RootTransform =FTransform(FQuat::MakeFromEuler(FVector(0)), FVector(0, 0, 2.0f * Radius + 1.0));
		TGeometryCollectionWrapper<Traits>* SimplicialSphereCollection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();
		UnitTest.AddSimulationObject(SimplicialSphereCollection);

		// Make a kinematic implicit sphere
		Params.SimplicialType = ESimplicialType::Chaos_Simplicial_Sphere;
		Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_LevelSet;
		Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Kinematic;
		Params.RootTransform = FTransform(FQuat::MakeFromEuler(FVector(0)), FVector(0));
		TGeometryCollectionWrapper<Traits>* ImplicitSphereCollection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();
		UnitTest.AddSimulationObject(ImplicitSphereCollection);

		// Hard code masstolocal on rest collection to identity
		{
			TManagedArray<FTransform>& MassToLocal = SimplicialSphereCollection->RestCollection->template GetAttribute<FTransform>(TEXT("MassToLocal"), FTransformCollection::TransformGroup);
			check(MassToLocal.Num() == 1);
			MassToLocal[0] = FTransform::Identity;
		}
		{
			TManagedArray<FTransform>& MassToLocal =ImplicitSphereCollection->RestCollection->template GetAttribute<FTransform>(TEXT("MassToLocal"), FTransformCollection::TransformGroup);
			check(MassToLocal.Num() == 1);
			MassToLocal[0] = FTransform::Identity;
		}

		UnitTest.Initialize();
		EXPECT_EQ(SimplicialSphereCollection->DynamicCollection->Transform[0].GetTranslation().Z, ImplicitSphereCollection->DynamicCollection->Transform[0].GetTranslation().Z + 2.0f * Radius + 1.0f);

		const FVector FirstX = SimplicialSphereCollection->DynamicCollection->Transform[0].GetTranslation();
		FVector PrevX = FirstX;
		for (int i = 0; i < 10; i++)
		{
			UnitTest.Advance();

			const FVector& CurrX = SimplicialSphereCollection->DynamicCollection->Transform[0].GetTranslation();
			EXPECT_NE(CurrX.Z, FirstX.Z); // moved since init
			EXPECT_LE(FMath::Abs(CurrX.X), 0.1f); // straight down
			EXPECT_LE(FMath::Abs(CurrX.Y), 0.1f); // straight down
			PrevX = CurrX;
		}
		
		{
			// We expect the simplical sphere to drop by 0.1 in Z and come to rest
			// on top of the implicit sphere.
			const FVector& CurrX = SimplicialSphereCollection->DynamicCollection->Transform[0].GetTranslation();
			EXPECT_LE(FMath::Abs(CurrX.Z - 2.0f * Radius), 0.1 * Radius);
		}
	}



	TYPED_TEST(AllTraits,GeometryCollection_CollisionResolution_SimplicialCubeToImplicitCube)
	{
		using Traits = TypeParam;

		// simplicial sphere to implicit sphere
		TFramework<Traits> UnitTest;

		CreationParameters Params;
		Params.EnableClustering = false;
		FReal Length = 100.0f;
		FVector Scale(Length, Length, Length);
		Params.GeomTransform.SetScale3D(Scale); // Box dimensions
		Params.EnableClustering = false;

		// Make a dynamic box
		Params.SimplicialType = ESimplicialType::Chaos_Simplicial_Box;
		Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;

		Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Dynamic;
		Params.RootTransform = FTransform(FQuat::MakeFromEuler(FVector(0)), FVector(0, 0, Length + 2.0f));
		TGeometryCollectionWrapper<Traits>* BoxCollection0 =
			TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();
		UnitTest.AddSimulationObject(BoxCollection0);

		// Make a kinematic box
		Params.SimplicialType = ESimplicialType::Chaos_Simplicial_Box;
		Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_LevelSet;

		Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Kinematic;
		Params.RootTransform = FTransform(FQuat::MakeFromEuler(FVector(0)), FVector(0));
		TGeometryCollectionWrapper<Traits>* BoxCollection1 =
			TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();
		UnitTest.AddSimulationObject(BoxCollection1);
/*
		// Hard code masstolocal on rest collection to identity
		{
			TManagedArray<FTransform>& MassToLocal =
				BoxCollection0->RestCollection->GetAttribute<FTransform>(
					TEXT("MassToLocal"), FTransformCollection::TransformGroup);
			check(MassToLocal.Num() == 1);
			MassToLocal[0] = FTransform::Identity;
		}
		{
			TManagedArray<FTransform>& MassToLocal =
				BoxCollection1->RestCollection->GetAttribute<FTransform>(
					TEXT("MassToLocal"), FTransformCollection::TransformGroup);
			check(MassToLocal.Num() == 1);
			MassToLocal[0] = FTransform::Identity;
		}
*/
		UnitTest.Initialize();
		EXPECT_EQ(
			BoxCollection0->DynamicCollection->Transform[0].GetTranslation().Z,
			BoxCollection1->DynamicCollection->Transform[0].GetTranslation().Z + Length + 2.0f);

		const FVector FirstX = BoxCollection0->DynamicCollection->Transform[0].GetTranslation();
		FVector PrevX = FirstX;
		for (int i = 0; i < 10; i++)
		{
			UnitTest.Advance();

			const FVector& CurrX = BoxCollection0->DynamicCollection->Transform[0].GetTranslation();
			EXPECT_NE(CurrX.Z, FirstX.Z); // moved since init
			EXPECT_LE(CurrX.Z, PrevX.Z); // falling in -Z, or stopped
			EXPECT_LE(FMath::Abs(CurrX.X), KINDA_SMALL_NUMBER); // straight down
			EXPECT_LE(FMath::Abs(CurrX.Y), KINDA_SMALL_NUMBER); // straight down
			PrevX = CurrX;
		}

		{
			// We expect the simplical cube to drop in Z direction and come to rest
			// on top of the implicit cube.
			const FVector& CurrX = BoxCollection0->DynamicCollection->Transform[0].GetTranslation();
			EXPECT_LE(FMath::Abs(CurrX.Z - Length), 0.2 * Length); // Relative large fudge factor accounts for spatial aliasing and contact location averaging.
		}
	}





	TYPED_TEST(AllTraits,GeometryCollection_CollisionResolution_SimplicialTetrahedronWithNonUniformMassToFloor)
	{
		using Traits = TypeParam;
		TFramework<Traits> UnitTest;

		FReal  Scale = 100.0f;

		TGeometryCollectionWrapper<Traits>* Collection = nullptr;
		{
			FVector GlobalTranslation(0, 0, Scale + 10); FQuat GlobalRotation = FQuat::MakeFromEuler(FVector(0));
			CreationParameters Params;
			Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Dynamic;
			Params.EnableClustering = false;
			Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_LevelSet;
			Params.SimplicialType = ESimplicialType::Chaos_Simplicial_Tetrahedron;
			Params.CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			Params.GeomTransform = FTransform(GlobalRotation, GlobalTranslation);
			FVector TetraHedronScale(Scale);
			Params.GeomTransform.SetScale3D(TetraHedronScale); // Tetrahedron dimensions
			Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();
			EXPECT_EQ(Collection->DynamicCollection->Parent[0], -1); // is a child of index one
			EXPECT_NEAR((Collection->DynamicCollection->MassToLocal[0].GetTranslation()-FVector(0,0,Scale + 10)).Size(),0,KINDA_SMALL_NUMBER);

			UnitTest.AddSimulationObject(Collection);
		}

		RigidBodyWrapper* Floor = TNewSimulationObject<GeometryType::RigidFloor>::Init<Traits>()->template As<RigidBodyWrapper>();
		UnitTest.AddSimulationObject(Floor);

		UnitTest.Initialize();

		for (int i = 0; i < 40; i++)
		{
			UnitTest.Advance();
		}
		{
			// Expected resting distance depends on the collision solver implementation. The current implementation uses PushOut
			// to set distance to 0 (see CollisionSolver.cpp ApplyPushOutManifold()), but real PBD would leave the distance at G.dt.dt
			// Note: This is set to the true PBD distance for now until zero restitution bouncing is fixed
			const FReal ExpectedRestingDistance = 0.0f; // True for manifold solver
			//const FReal ExpectedRestingDistance = UnitTest.Solver->GetEvolution()->GetGravityForces().GetAcceleration().Size() * UnitTest.Dt * UnitTest.Dt;	// Non-manifold version


			// validate the tetahedron collides and moved away from the static floor
			FVec3 RestTranslation = Collection->RestCollection->Transform[0].GetTranslation();
			FVec3 DynamicTranslation = Collection->DynamicCollection->Transform[0].GetTranslation();
			EXPECT_EQ(RestTranslation.Z, 0.f);
			EXPECT_NEAR(FMath::Abs(DynamicTranslation.X), 0.f, 0.01f);
			EXPECT_NEAR(FMath::Abs(DynamicTranslation.Y), 0.f, 0.01f);
			EXPECT_NEAR(DynamicTranslation.Z, -10.f + ExpectedRestingDistance, 0.1f);
		}
	}



} // namespace GeometryCollectionTest
