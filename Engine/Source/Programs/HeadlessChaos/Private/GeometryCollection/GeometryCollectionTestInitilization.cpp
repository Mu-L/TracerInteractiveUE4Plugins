// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionTestInitilization.h"
#include "GeometryCollection/GeometryCollectionTestFramework.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "HeadlessChaosTestUtility.h"

namespace GeometryCollectionTest
{
using namespace ChaosTest;


TYPED_TEST(AllTraits,GeometryCollection_Initilization_TransformedGeometryCollectionRoot)
{
	using Traits = TypeParam;
	FVector GlobalTranslation(10); FQuat GlobalRotation = FQuat::MakeFromEuler(FVector(0));
	CreationParameters Params; Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Dynamic; Params.EnableClustering = false;
	Params.RootTransform = FTransform(GlobalRotation,GlobalTranslation); Params.NestedTransforms ={FTransform(FVector(10)),FTransform::Identity,FTransform::Identity};
	TGeometryCollectionWrapper<Traits>* Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();
	EXPECT_EQ(Collection->DynamicCollection->Parent[0],1); // is a child of index one

	TFramework<Traits> UnitTest;
	UnitTest.AddSimulationObject(Collection);
	UnitTest.Initialize();
	UnitTest.Advance();

	{ // test results
		EXPECT_EQ(UnitTest.Solver->GetParticles().GetGeometryCollectionParticles().Size(),1);
		FVector X = UnitTest.Solver->GetParticles().GetGeometryCollectionParticles().X(0);
		FQuat R = UnitTest.Solver->GetParticles().GetGeometryCollectionParticles().R(0);
		EXPECT_TRUE((R * GlobalRotation.Inverse()).IsIdentity(KINDA_SMALL_NUMBER));
		EXPECT_NEAR(X.X - GlobalTranslation[0],0.0f,KINDA_SMALL_NUMBER);
		EXPECT_NEAR(X.Y - GlobalTranslation[1],0.0f,KINDA_SMALL_NUMBER);
		EXPECT_LT(X.Z,GlobalTranslation[2]);

		TArray<FTransform> Transform;
		GeometryCollectionAlgo::GlobalMatrices(Collection->DynamicCollection->Transform,Collection->DynamicCollection->Parent,Transform);
		EXPECT_EQ(Collection->DynamicCollection->Parent[0],FGeometryCollection::Invalid); // is not a child
		EXPECT_NEAR(Transform[0].GetTranslation().X - GlobalTranslation[0],0.0f,KINDA_SMALL_NUMBER);
		EXPECT_NEAR(Transform[0].GetTranslation().Y - GlobalTranslation[1],0.0f,KINDA_SMALL_NUMBER);
		EXPECT_LT(Transform[0].GetTranslation().Z,GlobalTranslation[2]);
	}
}


TYPED_TEST(AllTraits,GeometryCollection_Initilization_TransformedGeometryCollectionParentNode)
{
	using Traits = TypeParam;
	FVector GlobalTranslation(10); FQuat GlobalRotation = FQuat::MakeFromEuler(FVector(0));
	CreationParameters Params; Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Dynamic; Params.EnableClustering = false;
	Params.RootTransform = FTransform(GlobalRotation,GlobalTranslation); Params.NestedTransforms ={FTransform::Identity,FTransform(FVector(10)),FTransform::Identity};
	TGeometryCollectionWrapper<Traits>* Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();

	TFramework<Traits> UnitTest;
	UnitTest.AddSimulationObject(Collection);
	UnitTest.Initialize();
	UnitTest.Advance();

	{ // test results
		EXPECT_EQ(UnitTest.Solver->GetParticles().GetGeometryCollectionParticles().Size(),1);
		FVector X = UnitTest.Solver->GetParticles().GetGeometryCollectionParticles().X(0);
		FQuat R = UnitTest.Solver->GetParticles().GetGeometryCollectionParticles().R(0);
		EXPECT_TRUE((R * GlobalRotation.Inverse()).IsIdentity(KINDA_SMALL_NUMBER));
		EXPECT_NEAR(X.X - GlobalTranslation[0],0.0f,KINDA_SMALL_NUMBER);
		EXPECT_NEAR(X.Y - GlobalTranslation[1],0.0f,KINDA_SMALL_NUMBER);
		EXPECT_LT(X.Z,GlobalTranslation[2]);

		TArray<FTransform> Transform;
		GeometryCollectionAlgo::GlobalMatrices(Collection->DynamicCollection->Transform,Collection->DynamicCollection->Parent,Transform);
		EXPECT_EQ(Collection->DynamicCollection->Parent[0],FGeometryCollection::Invalid); // is not a child
		EXPECT_NEAR(Transform[0].GetTranslation().X - GlobalTranslation[0],0.0f,KINDA_SMALL_NUMBER);
		EXPECT_NEAR(Transform[0].GetTranslation().Y - GlobalTranslation[1],0.0f,KINDA_SMALL_NUMBER);
		EXPECT_LT(Transform[0].GetTranslation().Z,GlobalTranslation[2]);
	}
}


TYPED_TEST(AllTraits,GeometryCollection_Initilization_TransformedGeometryCollectionGeometryNode)
{
	using Traits = TypeParam;
	FVector GlobalTranslation(10); FQuat GlobalRotation = FQuat::MakeFromEuler(FVector(0));
	CreationParameters Params; Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Dynamic; Params.EnableClustering = false;
	Params.RootTransform = FTransform(GlobalRotation,GlobalTranslation); Params.NestedTransforms ={FTransform::Identity,FTransform::Identity,FTransform(FVector(10))};
	TGeometryCollectionWrapper<Traits>* Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();

	TFramework<Traits> UnitTest;
	UnitTest.AddSimulationObject(Collection);
	UnitTest.Initialize();
	UnitTest.Advance();

	{ // test results
		EXPECT_EQ(UnitTest.Solver->GetParticles().GetGeometryCollectionParticles().Size(),1);
		FVector X = UnitTest.Solver->GetParticles().GetGeometryCollectionParticles().X(0);
		FQuat R = UnitTest.Solver->GetParticles().GetGeometryCollectionParticles().R(0);
		EXPECT_TRUE((R * GlobalRotation.Inverse()).IsIdentity(KINDA_SMALL_NUMBER));
		EXPECT_NEAR(X.X - GlobalTranslation[0],0.0f,KINDA_SMALL_NUMBER);
		EXPECT_NEAR(X.Y - GlobalTranslation[1],0.0f,KINDA_SMALL_NUMBER);
		EXPECT_LT(X.Z,GlobalTranslation[2]);

		TArray<FTransform> Transform;
		GeometryCollectionAlgo::GlobalMatrices(Collection->DynamicCollection->Transform,Collection->DynamicCollection->Parent,Transform);
		EXPECT_EQ(Collection->DynamicCollection->Parent[0],FGeometryCollection::Invalid); // is not a child
		EXPECT_NEAR(Transform[0].GetTranslation().X - GlobalTranslation[0],0.0f,KINDA_SMALL_NUMBER);
		EXPECT_NEAR(Transform[0].GetTranslation().Y - GlobalTranslation[1],0.0f,KINDA_SMALL_NUMBER);
		EXPECT_LT(Transform[0].GetTranslation().Z,GlobalTranslation[2]);
	}
}


TYPED_TEST(AllTraits,GeometryCollection_Initilization_TransformedGeometryCollectionGeometryVertices)
{
	using Traits = TypeParam;
	FVector GlobalTranslation(10); FQuat GlobalRotation = FQuat::MakeFromEuler(FVector(0));
	CreationParameters Params; Params.DynamicState = EObjectStateTypeEnum::Chaos_Object_Dynamic; Params.EnableClustering = false;
	Params.SimplicialType = ESimplicialType::Chaos_Simplicial_Sphere; Params.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Sphere;
	Params.GeomTransform = FTransform(GlobalRotation,GlobalTranslation); Params.NestedTransforms ={FTransform::Identity,FTransform::Identity,FTransform::Identity};
	TGeometryCollectionWrapper<Traits>* Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>(Params)->template As<TGeometryCollectionWrapper<Traits>>();

	//
	// validate the vertices have been moved.  
	//
	TArray<FTransform> RestTransforms;
	int32 NumVertices = Collection->DynamicCollection->NumElements(FGeometryCollection::VerticesGroup);
	TManagedArray<FVector>& Vertices = Collection->RestCollection->template GetAttribute<FVector>("Vertex",FGeometryCollection::VerticesGroup);
	TManagedArray<int32>& BoneMap = Collection->RestCollection->template GetAttribute<int32>("BoneMap",FGeometryCollection::VerticesGroup);
	GeometryCollectionAlgo::GlobalMatrices(Collection->RestCollection->Transform,Collection->RestCollection->Parent,RestTransforms);
	FVector CenterOfMass(0);  for(int VertexIndex =0; VertexIndex <NumVertices; VertexIndex++) CenterOfMass += RestTransforms[BoneMap[VertexIndex]].TransformPosition(Vertices[VertexIndex]); CenterOfMass /= NumVertices;
	EXPECT_NEAR(CenterOfMass.X - GlobalTranslation[0],0.0f,KINDA_SMALL_NUMBER);
	EXPECT_NEAR(CenterOfMass.Y - GlobalTranslation[0],0.0f,KINDA_SMALL_NUMBER);
	EXPECT_NEAR(CenterOfMass.Z - GlobalTranslation[0],0.0f,KINDA_SMALL_NUMBER);



	TFramework<Traits> UnitTest;
	UnitTest.AddSimulationObject(Collection);
	UnitTest.Initialize();
	UnitTest.Advance();

	{ // test results
		TManagedArray<FTransform>& MassToLocal = Collection->RestCollection->template GetAttribute<FTransform>("MassToLocal",FGeometryCollection::TransformGroup);


		//EXPECT_EQ(UnitTest.Solver->GetParticles().GetGeometryCollectionParticles().Size(), 1);
		//FVector X = UnitTest.Solver->GetParticles().GetGeometryCollectionParticles().X(0);
		//FQuat R = UnitTest.Solver->GetParticles().GetGeometryCollectionParticles().R(0);
		//EXPECT_TRUE((R * GlobalRotation.Inverse()).IsIdentity(KINDA_SMALL_NUMBER));
		//EXPECT_NEAR(X.X - GlobalTranslation[0], 0.0f, KINDA_SMALL_NUMBER);
		//EXPECT_NEAR(X.Y - GlobalTranslation[1], 0.0f, KINDA_SMALL_NUMBER);
		//EXPECT_LT(X.Z, GlobalTranslation[2]);

		//TArray<FTransform> Transform;
		//GeometryCollectionAlgo::GlobalMatrices(Collection->DynamicCollection->Transform, Collection->DynamicCollection->Parent, Transform);
		//EXPECT_EQ(Collection->DynamicCollection->Parent[0], FGeometryCollection::Invalid); // is not a child
		//EXPECT_NEAR(Transform[0].GetTranslation().X - GlobalTranslation[0], 0.0f, KINDA_SMALL_NUMBER);
		//EXPECT_NEAR(Transform[0].GetTranslation().Y - GlobalTranslation[1], 0.0f, KINDA_SMALL_NUMBER);
		//EXPECT_LT(Transform[0].GetTranslation().Z, GlobalTranslation[2]);
	}
}


} // namespace GeometryCollectionTest
