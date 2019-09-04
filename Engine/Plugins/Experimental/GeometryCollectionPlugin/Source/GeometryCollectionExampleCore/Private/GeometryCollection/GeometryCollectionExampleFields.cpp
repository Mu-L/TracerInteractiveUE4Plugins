// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionExampleFields.h"

#include "Field/FieldSystem.h"
#include "Field/FieldSystemNodes.h"
#include "Field/FieldSystemCoreAlgo.h"
#include "Field/FieldSystemNoiseAlgo.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/TransformCollection.h"
#include "Misc/FileHelper.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(GCTF_Log, Verbose, All);

namespace GeometryCollectionExample
{
	template<class T>
	bool Fields_NoiseSample(ExampleResponse&& R)
	{
		int Bounds = 100;
		TArray<ContextIndex> IndicesArray;
		ContextIndex::ContiguousIndices(IndicesArray, Bounds*Bounds);
		TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());

		TArray<FVector> SamplesArray;
		SamplesArray.Init(FVector(0.f), Bounds*Bounds);
		for (int32 Index = 0, i=0; i < Bounds; i++)
		{
			for (int32 j = 0; j < Bounds; j++)
			{
				SamplesArray[Index] = 1000.*FVector(i,j,0);
				Index++;
			}
		}
		TArrayView<FVector> SamplesView(&(SamplesArray.operator[](0)), SamplesArray.Num());

		float MinDoimain = -1.0, MaxDomain = 1.0;
		FTransform Transform(FQuat::MakeFromEuler(FVector(45,45,45)), FVector(100, 0, 0), FVector(2, 1, 1));
		FNoiseField * NoiseField = new FNoiseField(MinDoimain, MaxDomain,Transform);

		FFieldContext Context{
			IndexView,
			SamplesView,
			FFieldContext::UniquePointerMap()
		};

		TArray<float> ResultsArray;
		ResultsArray.Init(false, Bounds*Bounds);
		TArrayView<float> ResultsView(&(ResultsArray.operator[](0)), ResultsArray.Num());
		NoiseField->Evaluate(Context, ResultsView);

		float min = FLT_MAX, max = -FLT_MAX;

		for (int32 Index = 0, i = 0; i < Bounds; i++)
		{
			for (int32 j = 0; j < Bounds; j++)
			{
				min = FMath::Min(min, ResultsView[Index]);
				max = FMath::Max(max, ResultsView[Index]);
				//std::cout << ResultsView[Index] << " ";
				Index++;
			}
			//std::cout << std::endl;
		}

		R.ExpectTrue(min >= MinDoimain);
		R.ExpectTrue(max <= MaxDomain);
		R.ExpectTrue(min < max);

		delete NoiseField;

		return !R.HasError();

	}
	template bool Fields_NoiseSample<float>(ExampleResponse&& R);

	template<class T>
	bool Fields_RadialIntMask(ExampleResponse&& R)
	{
		TArray<ContextIndex> IndicesArray;
		ContextIndex::ContiguousIndices(IndicesArray, 10);
		TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());

		TArray<FVector> SamplesArray;
		SamplesArray.Init(FVector(0.f), 10);
		for (int32 Index = 0; Index < 10; Index++)
		{
			SamplesArray[Index] = FVector(Index);
		}
		TArrayView<FVector> SamplesView(&(SamplesArray.operator[](0)), SamplesArray.Num());



		FRadialIntMask * RadialMask = new FRadialIntMask(0.0, FVector(), 1.0, 0.0, ESetMaskConditionType::Field_Set_Always);
		RadialMask->Position = FVector(0.0, 0.0, 0.0);
		RadialMask->Radius = 5.0;

		FFieldContext Context{
			IndexView,
			SamplesView,
			FFieldContext::UniquePointerMap()
		};

		TArray<int32> ResultsArray;
		ResultsArray.Init(false, 10.0);
		TArrayView<int32> ResultsView(&(ResultsArray.operator[](0)), ResultsArray.Num());
		RadialMask->Evaluate(Context, ResultsView);

		for (int32 Index = 0; Index < 10; Index++)
		{
			if (Index <= 2)
			{
				R.ExpectTrue(!!ResultsView[Index]);
			}
			else
			{
				R.ExpectTrue(!ResultsView[Index]);
			}
			//UE_LOG(GCTF_Log, Error, TEXT("[%d] %d"), Index, ResultsView[Index]);
		}
		delete RadialMask;

		return !R.HasError();
	}
	template bool Fields_RadialIntMask<float>(ExampleResponse&& R);

	template<class T>
	bool Fields_RadialFalloff(ExampleResponse&& R)
	{
		TArray<ContextIndex> IndicesArray;
		ContextIndex::ContiguousIndices(IndicesArray, 10);
		TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());


		TArray<FVector> SamplesArray;
		SamplesArray.Init(FVector(0.f), 10);
		for (int32 Index = 0; Index < 10; Index++)
		{
			SamplesArray[Index] = FVector(Index, 0, 0);
		}
		TArrayView<FVector> SamplesView(&(SamplesArray.operator[](0)), SamplesArray.Num());

		FRadialFalloff * RadialFalloff = new FRadialFalloff();
		RadialFalloff->Position = FVector(0.0, 0.0, 0.0);
		RadialFalloff->Radius = 5.0;
		RadialFalloff->Magnitude = 3.0;

		FFieldContext Context{
			IndexView,
			SamplesView,
			FFieldContext::UniquePointerMap()
		};

		TArray<float> ResultsArray;
		ResultsArray.Init(false, 10);
		TArrayView<float> ResultsView(&(ResultsArray.operator[](0)), ResultsArray.Num());
		RadialFalloff->Evaluate(Context, ResultsView);

		for (int32 Index = 0; Index < 10; Index++)
		{
			float ExpectedVal = RadialFalloff->Magnitude*float((RadialFalloff->Radius * RadialFalloff->Radius) - (Index*Index)) / (RadialFalloff->Radius*RadialFalloff->Radius);

			if (Index <= 5)
			{
				R.ExpectTrue(FMath::Abs(ResultsView[Index]) - ExpectedVal < KINDA_SMALL_NUMBER);
			}
			else
			{
				R.ExpectTrue(!ResultsView[Index]);
			}
			//UE_LOG(GCTF_Log, Error, TEXT("[%d] sample:%3.5f (%3.5f,%3.5f,%3.5f) %3.5f"), Index,
			//	ExpectedVal ,
			//	SamplesArray[Index].X, SamplesArray[Index].Y, SamplesArray[Index].Z,
			//	ResultsView[Index]);
		}
		delete RadialFalloff;

		return !R.HasError();
	}
	template bool Fields_RadialFalloff<float>(ExampleResponse&& R);

	template<class T>
	bool Fields_PlaneFalloff(ExampleResponse&& R)
	{
		TArray<ContextIndex> IndicesArray;
		ContextIndex::ContiguousIndices(IndicesArray, 10);
		TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());


		TArray<FVector> SamplesArray;
		SamplesArray.Init(FVector(0.f), 10);
		for (int32 Index = 0; Index < 10; Index++)
		{
			SamplesArray[Index] = FVector(0, 0, Index-5);
		}
		TArrayView<FVector> SamplesView(&(SamplesArray.operator[](0)), SamplesArray.Num());

		FPlaneFalloff * PlaneFalloff = new FPlaneFalloff();
		PlaneFalloff->Position = FVector(0.0, 0.0, 0.0);
		PlaneFalloff->Normal = FVector(0.0, 0.0, 1.0);
		PlaneFalloff->Magnitude = 3.0;

		FFieldContext Context{
			IndexView,
			SamplesView,
			FFieldContext::UniquePointerMap()
		};

		TArray<float> ResultsArray;
		ResultsArray.Init(false, 10);
		TArrayView<float> ResultsView(&(ResultsArray.operator[](0)), ResultsArray.Num());
		PlaneFalloff->Evaluate(Context, ResultsView);

		FPlane Plane(PlaneFalloff->Position, PlaneFalloff->Normal);
		for (int32 Index = 0; Index < 10; Index++)
		{
			float ExpectedVal = 0.f;
			float Distance = Plane.PlaneDot(SamplesArray[Index]);
			if (Distance < 0)
			{
				ExpectedVal = -PlaneFalloff->Magnitude * Distance;
			}

			R.ExpectTrue(FMath::Abs(ResultsView[Index]) - ExpectedVal < KINDA_SMALL_NUMBER);
		}
		delete PlaneFalloff;

		return !R.HasError();
	}
	template bool Fields_PlaneFalloff<float>(ExampleResponse&& R);


	template<class T>
	bool Fields_UniformVector(ExampleResponse&& R)
	{
		TArray<ContextIndex> IndicesArray;
		ContextIndex::ContiguousIndices(IndicesArray, 10);
		TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());


		TArray<FVector> SamplesArray;
		SamplesArray.Init(FVector(0.f), 10);
		for (int32 Index = 0; Index < 10; Index++)
		{
			SamplesArray[Index] = FVector(Index, 0, 0);
		}
		TArrayView<FVector> SamplesView(&(SamplesArray.operator[](0)), SamplesArray.Num());

		FUniformVector * UniformVector = new FUniformVector();
		UniformVector->Direction = FVector(3, 5, 7);
		UniformVector->Magnitude = 10.0;

		FFieldContext Context{
			IndexView,
			SamplesView,
			FFieldContext::UniquePointerMap()
		};

		TArray<FVector> ResultsArray;
		ResultsArray.Init(FVector(0), 10);
		TArrayView<FVector> ResultsView(&(ResultsArray.operator[](0)), ResultsArray.Num());
		UniformVector->Evaluate(Context, ResultsView);

		for (int32 Index = 0; Index < 10; Index++)
		{
			FVector ExpectedVal = 10.0*FVector(3, 5, 7);
			R.ExpectTrue((ResultsView[Index] - ExpectedVal).Size() < KINDA_SMALL_NUMBER);
			//UE_LOG(GCTF_Log, Error, TEXT("[%d] sample:(%3.5f,%3.5f,%3.5f) (%3.5f,%3.5f,%3.5f) (%3.5f,%3.5f,%3.5f)"), Index,
			//	ExpectedVal.X, ExpectedVal.Y, ExpectedVal.Z,
			//	SamplesArray[Index].X, SamplesArray[Index].Y, SamplesArray[Index].Z,
			//	ResultsView[Index].X, ResultsView[Index].Y, ResultsView[Index].Z);
		}
		delete UniformVector;

		return !R.HasError();
	}
	template bool Fields_UniformVector<float>(ExampleResponse&& R);


	template<class T>
	bool Fields_RaidalVector(ExampleResponse&& R)
	{
		TArray<ContextIndex> IndicesArray;
		ContextIndex::ContiguousIndices(IndicesArray, 10);
		TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());


		TArray<FVector> SamplesArray;
		SamplesArray.Init(FVector(0.f), 10);
		for (int32 Index = 0; Index < 10; Index++)
		{
			SamplesArray[Index] = FVector(100.*(FMath::SRand() - 0.5), 100.*(FMath::SRand() - 0.5), 100.*(FMath::SRand() - 0.5));
		}
		TArrayView<FVector> SamplesView(&(SamplesArray.operator[](0)), SamplesArray.Num());

		FRadialVector * RadialVector = new FRadialVector();
		RadialVector->Position = FVector(3, 4, 5);
		RadialVector->Magnitude = 10.0;

		FFieldContext Context{
			IndexView,
			SamplesView,
			FFieldContext::UniquePointerMap()
		};

		TArray<FVector> ResultsArray;
		ResultsArray.Init(FVector(0), 10);
		TArrayView<FVector> ResultsView(&(ResultsArray.operator[](0)), ResultsArray.Num());
		RadialVector->Evaluate(Context, ResultsView);

		for (int32 Index = 0; Index < 10; Index++)
		{
			FVector ExpectedVal = RadialVector->Magnitude * (SamplesArray[Index] - RadialVector->Position).GetSafeNormal();
			R.ExpectTrue((ResultsView[Index] - ExpectedVal).Size() < KINDA_SMALL_NUMBER);
			//UE_LOG(GCTF_Log, Error, TEXT("[%d] sample:(%3.5f,%3.5f,%3.5f) (%3.5f,%3.5f,%3.5f) (%3.5f,%3.5f,%3.5f)"), Index,
			//	ExpectedVal.X, ExpectedVal.Y, ExpectedVal.Z,
			//	SamplesArray[Index].X, SamplesArray[Index].Y, SamplesArray[Index].Z,
			//	ResultsView[Index].X, ResultsView[Index].Y, ResultsView[Index].Z);
		}
		delete RadialVector;

		return !R.HasError();
	}
	template bool Fields_RaidalVector<float>(ExampleResponse&& R);


	template<class T>
	bool Fields_SumVectorFullMult(ExampleResponse&& R)
	{
		TArray<ContextIndex> IndicesArray;
		ContextIndex::ContiguousIndices(IndicesArray, 10);
		TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());


		float AverageSampleLength = 0.0;
		TArray<FVector> SamplesArray;
		SamplesArray.Init(FVector(0.f), 10);
		for (int32 Index = 0; Index < 10; Index++)
		{
			SamplesArray[Index] = FVector(0);
			if (Index > 0)
			{
				SamplesArray[Index] = FVector(100.*(FMath::SRand() - 0.5), 100.*(FMath::SRand() - 0.5), 100.*(FMath::SRand() - 0.5));
			}
			AverageSampleLength += SamplesArray[Index].Size();
		}
		AverageSampleLength /= 10.0;
		TArrayView<FVector> SamplesView(&(SamplesArray.operator[](0)), SamplesArray.Num());

		FRadialFalloff * RadialFalloff = new FRadialFalloff();
		RadialFalloff->Position = FVector(0.0, 0.0, 0.0);
		RadialFalloff->Radius = AverageSampleLength;
		RadialFalloff->Magnitude = 3.0;

		FRadialVector * RadialVector = new FRadialVector();
		RadialVector->Position = FVector(0);
		RadialVector->Magnitude = 10.0;

		FUniformVector * UniformVector = new FUniformVector();
		UniformVector->Direction = FVector(3, 5, 7);
		UniformVector->Magnitude = 10.0;

		FSumVector * SumVector = new FSumVector(1.0, RadialFalloff, RadialVector, UniformVector, EFieldOperationType::Field_Multiply);

		FFieldContext Context{
			IndexView,
			SamplesView,
			FFieldContext::UniquePointerMap()
		};

		TArray<FVector> ResultsArray;
		ResultsArray.Init(FVector(0), 10);
		TArrayView<FVector> ResultsView(&(ResultsArray.operator[](0)), ResultsArray.Num());
		SumVector->Evaluate(Context, ResultsView);

		float RadialFalloffSize2 = RadialFalloff->Radius * RadialFalloff->Radius;
		for (int32 Index = 0; Index < 10; Index++)
		{
			FVector RightResult = UniformVector->Magnitude * UniformVector->Direction;
			FVector LeftResult = RadialVector->Magnitude  * (SamplesArray[Index] - RadialVector->Position).GetSafeNormal();
			float RadialFalloffDelta2 = (SamplesArray[Index] - RadialFalloff->Position).SizeSquared();
			float ScalarResult = RadialFalloff->Magnitude* (RadialFalloffSize2 - RadialFalloffDelta2) / RadialFalloffSize2;
			if (RadialFalloffDelta2 >= RadialFalloffSize2)
				ScalarResult = 0.f;

			FVector ExpectedVal = ScalarResult * (LeftResult * RightResult);

			//UE_LOG(GCTF_Log, Error, TEXT("[%d:%3.5f] sample:(%3.5f,%3.5f,%3.5f) (%3.5f,%3.5f,%3.5f) (%3.5f,%3.5f,%3.5f)"), Index,
			//	(ResultsView[Index] - ExpectedVal).Size(),
			//	ExpectedVal.X, ExpectedVal.Y, ExpectedVal.Z,
			//	SamplesArray[Index].X, SamplesArray[Index].Y, SamplesArray[Index].Z,
			//	ResultsView[Index].X, ResultsView[Index].Y, ResultsView[Index].Z);

			R.ExpectTrue((ResultsView[Index] - ExpectedVal).Size() < KINDA_SMALL_NUMBER);
		}
		delete SumVector;

		return !R.HasError();
	}
	template bool Fields_SumVectorFullMult<float>(ExampleResponse&& R);

	template<class T>
	bool Fields_SumVectorFullDiv(ExampleResponse&& R)
	{
		TArray<ContextIndex> IndicesArray;
		ContextIndex::ContiguousIndices(IndicesArray, 10);
		TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());


		float AverageSampleLength = 0.0;
		TArray<FVector> SamplesArray;
		SamplesArray.Init(FVector(0.f), 10);
		for (int32 Index = 0; Index < 10; Index++)
		{
			SamplesArray[Index] = FVector(0);
			if (Index > 0)
			{
				SamplesArray[Index] = FVector(100.*(FMath::SRand() - 0.5), 100.*(FMath::SRand() - 0.5), 100.*(FMath::SRand() - 0.5));
			}
			AverageSampleLength += SamplesArray[Index].Size();
		}
		AverageSampleLength /= 10.0;
		TArrayView<FVector> SamplesView(&(SamplesArray.operator[](0)), SamplesArray.Num());

		FRadialFalloff * RadialFalloff = new FRadialFalloff();
		RadialFalloff->Position = FVector(0.0, 0.0, 0.0);
		RadialFalloff->Radius = AverageSampleLength;
		RadialFalloff->Magnitude = 3.0;

		FRadialVector * RadialVector = new FRadialVector();
		RadialVector->Position = FVector(0);
		RadialVector->Magnitude = 10.0;

		FUniformVector * UniformVector = new FUniformVector();
		UniformVector->Direction = FVector(3, 5, 7);
		UniformVector->Magnitude = 10.0;

		FSumVector * SumVector = new FSumVector(1.0, RadialFalloff, UniformVector, RadialVector,  EFieldOperationType::Field_Divide);

		FFieldContext Context{
			IndexView,
			SamplesView,
			FFieldContext::UniquePointerMap()
		};

		TArray<FVector> ResultsArray;
		ResultsArray.Init(FVector(0), 10);
		TArrayView<FVector> ResultsView(&(ResultsArray.operator[](0)), ResultsArray.Num());
		SumVector->Evaluate(Context, ResultsView);

		float RadialFalloffSize2 = RadialFalloff->Radius * RadialFalloff->Radius;
		for (int32 Index = 0; Index < 10; Index++)
		{
			FVector RightResult = UniformVector->Magnitude * UniformVector->Direction;
			FVector LeftResult = RadialVector->Magnitude  * (SamplesArray[Index] - RadialVector->Position).GetSafeNormal();
			float RadialFalloffDelta2 = (SamplesArray[Index] - RadialFalloff->Position).SizeSquared();
			float ScalarResult = RadialFalloff->Magnitude* (RadialFalloffSize2 - RadialFalloffDelta2) / RadialFalloffSize2;
			if (RadialFalloffDelta2 >= RadialFalloffSize2)
				ScalarResult = 0.f;

			FVector ExpectedVal = ScalarResult * (LeftResult / RightResult);

			//UE_LOG(GCTF_Log, Error, TEXT("[%d:%3.5f] sample:(%3.5f,%3.5f,%3.5f) (%3.5f,%3.5f,%3.5f) (%3.5f,%3.5f,%3.5f)"), Index,
			//	(ResultsView[Index] - ExpectedVal).Size(),
			//	ExpectedVal.X, ExpectedVal.Y, ExpectedVal.Z,
			//	SamplesArray[Index].X, SamplesArray[Index].Y, SamplesArray[Index].Z,
			//	ResultsView[Index].X, ResultsView[Index].Y, ResultsView[Index].Z);

			R.ExpectTrue((ResultsView[Index] - ExpectedVal).Size() < KINDA_SMALL_NUMBER);
		}
		delete SumVector;

		return !R.HasError();
	}
	template bool Fields_SumVectorFullDiv<float>(ExampleResponse&& R);


	template<class T>
	bool Fields_SumVectorFullAdd(ExampleResponse&& R)
	{
		TArray<ContextIndex> IndicesArray;
		ContextIndex::ContiguousIndices(IndicesArray, 10);
		TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());


		float AverageSampleLength = 0.0;
		TArray<FVector> SamplesArray;
		SamplesArray.Init(FVector(0.f), 10);
		for (int32 Index = 0; Index < 10; Index++)
		{
			SamplesArray[Index] = FVector(0);
			if (Index > 0)
			{
				SamplesArray[Index] = FVector(100.*(FMath::SRand() - 0.5), 100.*(FMath::SRand() - 0.5), 100.*(FMath::SRand() - 0.5));
			}
			AverageSampleLength += SamplesArray[Index].Size();
		}
		AverageSampleLength /= 10.0;
		TArrayView<FVector> SamplesView(&(SamplesArray.operator[](0)), SamplesArray.Num());


		FRadialFalloff * RadialFalloff = new FRadialFalloff();
		RadialFalloff->Position = FVector(0.0, 0.0, 0.0);
		RadialFalloff->Radius = AverageSampleLength;
		RadialFalloff->Magnitude = 3.0;

		FRadialVector * RadialVector = new FRadialVector();
		RadialVector->Position = FVector(0);
		RadialVector->Magnitude = 10.0;

		FUniformVector * UniformVector = new FUniformVector();
		UniformVector->Direction = FVector(3, 5, 7);
		UniformVector->Magnitude = 10.0;

		FSumVector * SumVector = new FSumVector(1.0, RadialFalloff, UniformVector, RadialVector, EFieldOperationType::Field_Add);

		FFieldContext Context{
			IndexView,
			SamplesView,
			FFieldContext::UniquePointerMap()
		};

		TArray<FVector> ResultsArray;
		ResultsArray.Init(FVector(0), 10);
		TArrayView<FVector> ResultsView(&(ResultsArray.operator[](0)), ResultsArray.Num());
		SumVector->Evaluate(Context, ResultsView);

		float RadialFalloffSize2 = RadialFalloff->Radius * RadialFalloff->Radius;
		for (int32 Index = 0; Index < 10; Index++)
		{
			FVector RightResult = UniformVector->Magnitude * UniformVector->Direction;
			FVector LeftResult = RadialVector->Magnitude  * (SamplesArray[Index] - RadialVector->Position).GetSafeNormal();
			float RadialFalloffDelta2 = (SamplesArray[Index] - RadialFalloff->Position).SizeSquared();
			float ScalarResult = RadialFalloff->Magnitude* (RadialFalloffSize2 - RadialFalloffDelta2) / RadialFalloffSize2;
			if (RadialFalloffDelta2 >= RadialFalloffSize2)
				ScalarResult = 0.f;

			FVector ExpectedVal = ScalarResult * (LeftResult + RightResult);

			//UE_LOG(GCTF_Log, Error, TEXT("[%d:%3.5f] sample:(%3.5f,%3.5f,%3.5f) (%3.5f,%3.5f,%3.5f) (%3.5f,%3.5f,%3.5f)"), Index,
			//	(ResultsView[Index] - ExpectedVal).Size(),
			//	ExpectedVal.X, ExpectedVal.Y, ExpectedVal.Z,
			//	SamplesArray[Index].X, SamplesArray[Index].Y, SamplesArray[Index].Z,
			//	ResultsView[Index].X, ResultsView[Index].Y, ResultsView[Index].Z);

			R.ExpectTrue((ResultsView[Index] - ExpectedVal).Size() < KINDA_SMALL_NUMBER);
		}
		delete SumVector;

		return !R.HasError();
	}
	template bool Fields_SumVectorFullAdd<float>(ExampleResponse&& R);


	template<class T>
	bool Fields_SumVectorFullSub(ExampleResponse&& R)
	{
		TArray<ContextIndex> IndicesArray;
		ContextIndex::ContiguousIndices(IndicesArray, 10);
		TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());


		float AverageSampleLength = 0.0;
		TArray<FVector> SamplesArray;
		SamplesArray.Init(FVector(0.f), 10);
		for (int32 Index = 0; Index < 10; Index++)
		{
			SamplesArray[Index] = FVector(0);
			if (Index > 0)
			{
				SamplesArray[Index] = FVector(100.*(FMath::SRand() - 0.5), 100.*(FMath::SRand() - 0.5), 100.*(FMath::SRand() - 0.5));
			}
			AverageSampleLength += SamplesArray[Index].Size();
		}
		AverageSampleLength /= 10.0;
		TArrayView<FVector> SamplesView(&(SamplesArray.operator[](0)), SamplesArray.Num());


		FRadialFalloff * RadialFalloff = new FRadialFalloff();
		RadialFalloff->Position = FVector(0.0, 0.0, 0.0);
		RadialFalloff->Radius = AverageSampleLength;
		RadialFalloff->Magnitude = 3.0;

		FRadialVector * RadialVector = new FRadialVector();
		RadialVector->Position = FVector(0);
		RadialVector->Magnitude = 10.0;

		FUniformVector * UniformVector = new FUniformVector();
		UniformVector->Direction = FVector(3, 5, 7);
		UniformVector->Magnitude = 10.0;

		FSumVector * SumVector = new FSumVector(1.0, RadialFalloff, UniformVector, RadialVector, EFieldOperationType::Field_Substract);

		FFieldContext Context{
			IndexView,
			SamplesView,
			FFieldContext::UniquePointerMap()
		};

		TArray<FVector> ResultsArray;
		ResultsArray.Init(FVector(0), 10);
		TArrayView<FVector> ResultsView(&(ResultsArray.operator[](0)), ResultsArray.Num());
		SumVector->Evaluate(Context, ResultsView);

		float RadialFalloffSize2 = RadialFalloff->Radius * RadialFalloff->Radius;
		for (int32 Index = 0; Index < 10; Index++)
		{
			FVector RightResult = UniformVector->Magnitude * UniformVector->Direction;
			FVector LeftResult = RadialVector->Magnitude  * (SamplesArray[Index] - RadialVector->Position).GetSafeNormal();
			float RadialFalloffDelta2 = (SamplesArray[Index] - RadialFalloff->Position).SizeSquared();
			float ScalarResult = RadialFalloff->Magnitude* (RadialFalloffSize2 - RadialFalloffDelta2) / RadialFalloffSize2;
			if (RadialFalloffDelta2 >= RadialFalloffSize2)
				ScalarResult = 0.f;

			FVector ExpectedVal = ScalarResult * (LeftResult - RightResult);

			//UE_LOG(GCTF_Log, Error, TEXT("[%d:%3.5f] sample:(%3.5f,%3.5f,%3.5f) (%3.5f,%3.5f,%3.5f) (%3.5f,%3.5f,%3.5f)"), Index,
			//	(ResultsView[Index] - ExpectedVal).Size(),
			//	ExpectedVal.X, ExpectedVal.Y, ExpectedVal.Z,
			//	SamplesArray[Index].X, SamplesArray[Index].Y, SamplesArray[Index].Z,
			//	ResultsView[Index].X, ResultsView[Index].Y, ResultsView[Index].Z);

			R.ExpectTrue((ResultsView[Index] - ExpectedVal).Size() < KINDA_SMALL_NUMBER);
		}
		delete SumVector;

		return !R.HasError();
	}
	template bool Fields_SumVectorFullSub<float>(ExampleResponse&& R);


	template<class T>
	bool Fields_SumVectorLeftSide(ExampleResponse&& R)
	{
		TArray<ContextIndex> IndicesArray;
		ContextIndex::ContiguousIndices(IndicesArray, 10);
		TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());


		float AverageSampleLength = 0.0;
		TArray<FVector> SamplesArray;
		SamplesArray.Init(FVector(0.f), 10);
		for (int32 Index = 0; Index < 10; Index++)
		{
			SamplesArray[Index] = FVector(0);
			if (Index > 0)
			{
				SamplesArray[Index] = FVector(100.*(FMath::SRand() - 0.5), 100.*(FMath::SRand() - 0.5), 100.*(FMath::SRand() - 0.5));
			}
			AverageSampleLength += SamplesArray[Index].Size();
		}
		AverageSampleLength /= 10.0;
		TArrayView<FVector> SamplesView(&(SamplesArray.operator[](0)), SamplesArray.Num());

		FRadialFalloff * RadialFalloff = new FRadialFalloff();
		RadialFalloff->Position = FVector(0.0, 0.0, 0.0);
		RadialFalloff->Radius = AverageSampleLength;
		RadialFalloff->Magnitude = 3.0;

		FRadialVector * RadialVector = new FRadialVector();
		RadialVector->Position = FVector(0);
		RadialVector->Magnitude = 10.0;

		FUniformVector * UniformVector = new FUniformVector();
		UniformVector->Direction = FVector(3, 5, 7);
		UniformVector->Magnitude = 10.0;

		FSumVector * SumVector = new FSumVector(1.0, RadialFalloff, nullptr, RadialVector, EFieldOperationType::Field_Multiply);

		FFieldContext Context{
			IndexView,
			SamplesView,
			FFieldContext::UniquePointerMap()
		};

		TArray<FVector> ResultsArray;
		ResultsArray.Init(FVector(0), 10);
		TArrayView<FVector> ResultsView(&(ResultsArray.operator[](0)), ResultsArray.Num());
		SumVector->Evaluate(Context, ResultsView);

		float RadialFalloffSize2 = RadialFalloff->Radius * RadialFalloff->Radius;
		for (int32 Index = 0; Index < 10; Index++)
		{
			FVector RightResult = UniformVector->Magnitude * UniformVector->Direction;
			FVector LeftResult = RadialVector->Magnitude  * (SamplesArray[Index] - RadialVector->Position).GetSafeNormal();
			float RadialFalloffDelta2 = (SamplesArray[Index] - RadialFalloff->Position).SizeSquared();
			float ScalarResult = RadialFalloff->Magnitude* (RadialFalloffSize2 - RadialFalloffDelta2) / RadialFalloffSize2;
			if (RadialFalloffDelta2 >= RadialFalloffSize2)
				ScalarResult = 0.f;

			FVector ExpectedVal = ScalarResult * (LeftResult);

			//UE_LOG(GCTF_Log, Error, TEXT("[%d:%3.5f] sample:(%3.5f,%3.5f,%3.5f) (%3.5f,%3.5f,%3.5f) (%3.5f,%3.5f,%3.5f)"), Index,
			//	(ResultsView[Index] - ExpectedVal).Size(),
			//	ExpectedVal.X, ExpectedVal.Y, ExpectedVal.Z,
			//	SamplesArray[Index].X, SamplesArray[Index].Y, SamplesArray[Index].Z,
			//	ResultsView[Index].X, ResultsView[Index].Y, ResultsView[Index].Z);

			R.ExpectTrue((ResultsView[Index] - ExpectedVal).Size() < KINDA_SMALL_NUMBER);
		}
		delete SumVector;
		delete UniformVector;

		return !R.HasError();
	}
	template bool Fields_SumVectorLeftSide<float>(ExampleResponse&& R);

	template<class T>
	bool Fields_SumVectorRightSide(ExampleResponse&& R)
	{
		TArray<ContextIndex> IndicesArray;
		ContextIndex::ContiguousIndices(IndicesArray, 10);
		TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());


		float AverageSampleLength = 0.0;
		TArray<FVector> SamplesArray;
		SamplesArray.Init(FVector(0.f), 10);
		for (int32 Index = 0; Index < 10; Index++)
		{
			SamplesArray[Index] = FVector(0);
			if (Index > 0)
			{
				SamplesArray[Index] = FVector(100.*(FMath::SRand() - 0.5), 100.*(FMath::SRand() - 0.5), 100.*(FMath::SRand() - 0.5));
			}
			AverageSampleLength += SamplesArray[Index].Size();
		}
		AverageSampleLength /= 10.0;
		TArrayView<FVector> SamplesView(&(SamplesArray.operator[](0)), SamplesArray.Num());


		FRadialFalloff * RadialFalloff = new FRadialFalloff();
		RadialFalloff->Position = FVector(0.0, 0.0, 0.0);
		RadialFalloff->Radius = AverageSampleLength;
		RadialFalloff->Magnitude = 3.0;

		FRadialVector * RadialVector = new FRadialVector();
		RadialVector->Position = FVector(0);
		RadialVector->Magnitude = 10.0;

		FUniformVector * UniformVector = new FUniformVector();
		UniformVector->Direction = FVector(3, 5, 7);
		UniformVector->Magnitude = 10.0;

		FSumVector * SumVector = new FSumVector(1.0, RadialFalloff, UniformVector, nullptr, EFieldOperationType::Field_Multiply);


		FFieldContext Context{
			IndexView,
			SamplesView,
			FFieldContext::UniquePointerMap()
		};

		TArray<FVector> ResultsArray;
		ResultsArray.Init(FVector(0), 10);
		TArrayView<FVector> ResultsView(&(ResultsArray.operator[](0)), ResultsArray.Num());
		SumVector->Evaluate(Context, ResultsView);

		float RadialFalloffSize2 = RadialFalloff->Radius * RadialFalloff->Radius;
		for (int32 Index = 0; Index < 10; Index++)
		{
			FVector RightResult = UniformVector->Magnitude * UniformVector->Direction;
			FVector LeftResult = RadialVector->Magnitude  * (SamplesArray[Index] - RadialVector->Position).GetSafeNormal();
			float RadialFalloffDelta2 = (SamplesArray[Index] - RadialFalloff->Position).SizeSquared();
			float ScalarResult = RadialFalloff->Magnitude* (RadialFalloffSize2 - RadialFalloffDelta2) / RadialFalloffSize2;
			if (RadialFalloffDelta2 >= RadialFalloffSize2)
				ScalarResult = 0.f;

			FVector ExpectedVal = ScalarResult * (RightResult);

			//UE_LOG(GCTF_Log, Error, TEXT("[%d:%3.5f] sample:(%3.5f,%3.5f,%3.5f) (%3.5f,%3.5f,%3.5f) (%3.5f,%3.5f,%3.5f)"), Index,
			//	(ResultsView[Index] - ExpectedVal).Size(),
			//	ExpectedVal.X, ExpectedVal.Y, ExpectedVal.Z,
			//	SamplesArray[Index].X, SamplesArray[Index].Y, SamplesArray[Index].Z,
			//	ResultsView[Index].X, ResultsView[Index].Y, ResultsView[Index].Z);

			R.ExpectTrue((ResultsView[Index] - ExpectedVal).Size() < KINDA_SMALL_NUMBER);
		}
		delete SumVector;
		delete RadialVector;

		return !R.HasError();
	}
	template bool Fields_SumVectorRightSide<float>(ExampleResponse&& R);


	template<class T>
	bool Fields_SumScalar(ExampleResponse&& R)
	{
		int32 NumPoints = 20;
		TArray<ContextIndex> IndicesArray;
		ContextIndex::ContiguousIndices(IndicesArray, NumPoints);
		TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());


		TArray<FVector> SamplesArray;
		SamplesArray.Init(FVector(0.f), NumPoints);
		for (int32 Index = -10; Index < 10; Index++)
		{
			SamplesArray[Index + 10] = FVector(Index, 0, 0);
		}
		TArrayView<FVector> SamplesView(&(SamplesArray.operator[](0)), SamplesArray.Num());


		
		FRadialFalloff * RadialFalloff = new FRadialFalloff();
		RadialFalloff->Position = FVector(5.0, 0.0, 0.0);
		RadialFalloff->Radius = 10;
		RadialFalloff->Magnitude = 3.0;
		float RadialFalloffRadius2 = RadialFalloff->Radius * RadialFalloff->Radius;

		FRadialFalloff * RadialFalloff2 = new FRadialFalloff();
		RadialFalloff2->Position = FVector(5.0, 0.0, 0.0);
		RadialFalloff2->Radius = 10;
		RadialFalloff2->Magnitude = 3.0;
		float RadialFalloff2Radius2 = RadialFalloff2->Radius * RadialFalloff2->Radius;

		FSumScalar * SumScalar = new FSumScalar(1.f, RadialFalloff, RadialFalloff2,EFieldOperationType::Field_Multiply);

		FFieldContext Context{
		IndexView,
		SamplesView,
		FFieldContext::UniquePointerMap()
		};

		TArray<float> ResultsArray;
		ResultsArray.Init(0.f, SamplesArray.Num());
		TArrayView<float> ResultsView(&(ResultsArray.operator[](0)), ResultsArray.Num());
		SumScalar->Evaluate(Context, ResultsView);

		for (int32 Index = 0; Index < NumPoints; Index++)
		{
			float ScalarRight = 0.f;
			{// FRadialFalloff::Evaluate
				float  RadialFalloffDelta2 = (RadialFalloff->Position - SamplesArray[Index]).SizeSquared();
				if (RadialFalloffDelta2 < RadialFalloffRadius2)
				{
					ScalarRight = RadialFalloff->Magnitude * (RadialFalloffRadius2 - RadialFalloffDelta2) / RadialFalloffRadius2;
				}
			}

			float ScalarLeft = 0.f;
			{ //  FRadialFalloff2::Evaluate
				float  RadialFalloff2Delta2 = (RadialFalloff2->Position - SamplesArray[Index]).SizeSquared();
				if (RadialFalloff2Delta2 < RadialFalloff2Radius2)
				{
					ScalarLeft = RadialFalloff2->Magnitude * (RadialFalloff2Radius2 - RadialFalloff2Delta2) / RadialFalloff2Radius2;
				}
			}

			float ExpectedVal = ScalarLeft * ScalarRight;

			//UE_LOG(GCTF_Log, Error, TEXT("[%d:%3.5f] sample:(%3.5f,%3.5f,%3.5f) %3.5f -> %3.5f"), Index,
			//	ResultsView[Index] - ExpectedVal,
			//	SamplesArray[Index].X, SamplesArray[Index].Y, SamplesArray[Index].Z,
			//	ExpectedVal, ResultsView[Index]);

			R.ExpectTrue((ResultsView[Index] - ExpectedVal) < KINDA_SMALL_NUMBER);
		}
		delete SumScalar;

		return !R.HasError();
	}
	template bool Fields_SumScalar<float>(ExampleResponse&& R);


	template<class T>
	bool Fields_SumScalarRightSide(ExampleResponse&& R)
	{
		int32 NumPoints = 20;
		TArray<ContextIndex> IndicesArray;
		ContextIndex::ContiguousIndices(IndicesArray, NumPoints);
		TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());

		TArray<FVector> SamplesArray;
		SamplesArray.Init(FVector(0.f), NumPoints);
		for (int32 Index = -10; Index < 10; Index++)
		{
			SamplesArray[Index + 10] = FVector(Index, 0, 0);
		}
		TArrayView<FVector> SamplesView(&(SamplesArray.operator[](0)), SamplesArray.Num());


		FRadialFalloff * RadialFalloff = new FRadialFalloff();
		RadialFalloff->Position = FVector(5.0, 0.0, 0.0);
		RadialFalloff->Radius = 10;
		RadialFalloff->Magnitude = 3.0;
		float RadialFalloffRadius2 = RadialFalloff->Radius * RadialFalloff->Radius;

		FRadialFalloff * RadialFalloff2 = new FRadialFalloff();
		RadialFalloff2->Position = FVector(5.0, 0.0, 0.0);
		RadialFalloff2->Radius = 10;
		RadialFalloff2->Magnitude = 3.0;
		float RadialFalloff2Radius2 = RadialFalloff2->Radius * RadialFalloff2->Radius;

		FSumScalar * SumScalar = new FSumScalar(1.f, RadialFalloff, nullptr, EFieldOperationType::Field_Multiply);

		FFieldContext Context{
			IndexView,
			SamplesView,
			FFieldContext::UniquePointerMap()
		};

		TArray<float> ResultsArray;
		ResultsArray.Init(0.f, SamplesArray.Num());
		TArrayView<float> ResultsView(&(ResultsArray.operator[](0)), ResultsArray.Num());
		SumScalar->Evaluate(Context, ResultsView);

		for (int32 Index = 0; Index < NumPoints; Index++)
		{
			float ScalarRight = 0.f;
			{// FRadialFalloff::Evaluate
				float  RadialFalloffDelta2 = (RadialFalloff->Position - SamplesArray[Index]).SizeSquared();
				if (RadialFalloffDelta2 < RadialFalloffRadius2)
				{
					ScalarRight = RadialFalloff->Magnitude * (RadialFalloffRadius2 - RadialFalloffDelta2) / RadialFalloffRadius2;
				}
			}

			float ScalarLeft = 1.f;

			float ExpectedVal = ScalarLeft * ScalarRight;

			//UE_LOG(GCTF_Log, Error, TEXT("[%d:%3.5f] sample:(%3.5f,%3.5f,%3.5f) %3.5f -> %3.5f"), Index,
			//	ResultsView[Index] - ExpectedVal,
			//	SamplesArray[Index].X, SamplesArray[Index].Y, SamplesArray[Index].Z,
			//	ExpectedVal, ResultsView[Index]);

			R.ExpectTrue((ResultsView[Index] - ExpectedVal) < KINDA_SMALL_NUMBER);
		}
		delete SumScalar;
		delete RadialFalloff2;

		return !R.HasError();
	}
	template bool Fields_SumScalarRightSide<float>(ExampleResponse&& R);


	template<class T>
	bool Fields_SumScalarLeftSide(ExampleResponse&& R)
	{
		int32 NumPoints = 20;
		TArray<ContextIndex> IndicesArray;
		ContextIndex::ContiguousIndices(IndicesArray, NumPoints);
		TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());

		TArray<FVector> SamplesArray;
		SamplesArray.Init(FVector(0.f), NumPoints);
		for (int32 Index = -10; Index < 10; Index++)
		{
			SamplesArray[Index + 10] = FVector(Index, 0, 0);
		}
		TArrayView<FVector> SamplesView(&(SamplesArray.operator[](0)), SamplesArray.Num());



		FRadialFalloff * RadialFalloff = new FRadialFalloff();
		RadialFalloff->Position = FVector(5.0, 0.0, 0.0);
		RadialFalloff->Radius = 10;
		RadialFalloff->Magnitude = 3.0;
		float RadialFalloffRadius2 = RadialFalloff->Radius * RadialFalloff->Radius;

		FRadialFalloff * RadialFalloff2 = new FRadialFalloff();
		RadialFalloff2->Position = FVector(5.0, 0.0, 0.0);
		RadialFalloff2->Radius = 10;
		RadialFalloff2->Magnitude = 3.0;
		float RadialFalloff2Radius2 = RadialFalloff2->Radius * RadialFalloff2->Radius;

		FSumScalar * SumScalar = new FSumScalar(1.f, nullptr, RadialFalloff2, EFieldOperationType::Field_Multiply);

		FFieldContext Context{
			IndexView,
			SamplesView,
			FFieldContext::UniquePointerMap()
		};

		TArray<float> ResultsArray;
		ResultsArray.Init(0.f, SamplesArray.Num());
		TArrayView<float> ResultsView(&(ResultsArray.operator[](0)), ResultsArray.Num());
		SumScalar->Evaluate(Context, ResultsView);

		for (int32 Index = 0; Index < NumPoints; Index++)
		{
			float ScalarRight = 1.f;

			float ScalarLeft = 0.f;
			{ //  FRadialFalloff2::Evaluate
				float  RadialFalloff2Delta2 = (RadialFalloff2->Position - SamplesArray[Index]).SizeSquared();
				if (RadialFalloff2Delta2 < RadialFalloff2Radius2)
				{
					ScalarLeft = RadialFalloff2->Magnitude * (RadialFalloff2Radius2 - RadialFalloff2Delta2) / RadialFalloff2Radius2;
				}
			}

			float ExpectedVal = ScalarLeft * ScalarRight;

			//UE_LOG(GCTF_Log, Error, TEXT("[%d:%3.5f] sample:(%3.5f,%3.5f,%3.5f) %3.5f -> %3.5f"), Index,
			//	ResultsView[Index] - ExpectedVal,
			//	SamplesArray[Index].X, SamplesArray[Index].Y, SamplesArray[Index].Z,
			//	ExpectedVal, ResultsView[Index]);

			R.ExpectTrue((ResultsView[Index] - ExpectedVal) < KINDA_SMALL_NUMBER);
		}
		delete SumScalar;
		delete RadialFalloff;

		return !R.HasError();
	}
	template bool Fields_SumScalarLeftSide<float>(ExampleResponse&& R);




	template<class T>
	bool Fields_Culling(ExampleResponse&& R)
	{
		int32 NumPoints = 20;
		TArray<ContextIndex> IndicesArray;
		ContextIndex::ContiguousIndices(IndicesArray, NumPoints);
		TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());

		TArray<FVector> SamplesArray;
		SamplesArray.Init(FVector(0.f), NumPoints);
		for (int32 Index = -10; Index < 10; Index++)
		{
			SamplesArray[Index + 10] = FVector(Index, 0, 0);
		}
		TArrayView<FVector> SamplesView(&(SamplesArray.operator[](0)), SamplesArray.Num());



		FRadialFalloff * RadialFalloff = new FRadialFalloff();
		RadialFalloff->Position = FVector(0.0, 0.0, 0.0);
		RadialFalloff->Radius = 4.;
		RadialFalloff->Magnitude = 3.0;
		float RadialFalloffRadius2 = RadialFalloff->Radius * RadialFalloff->Radius;


		FRadialFalloff * RadialFalloff2 = new FRadialFalloff();
		RadialFalloff2->Position = FVector(5.0, 0.0, 0.0);
		RadialFalloff2->Radius = 10;
		RadialFalloff2->Magnitude = 3.0;
		float RadialFalloff2Radius2 = RadialFalloff2->Radius * RadialFalloff2->Radius;

		FCullingField<float> * CullingField = new FCullingField<float>(RadialFalloff, RadialFalloff2, EFieldCullingOperationType::Field_Culling_Outside);

		FFieldContext Context{
			IndexView,
			SamplesView,
			FFieldContext::UniquePointerMap()
		};

		TArray<float> ResultsArray;
		ResultsArray.Init(0.f, SamplesArray.Num());
		TArrayView<float> ResultsView(&(ResultsArray.operator[](0)), ResultsArray.Num());
		CullingField->Evaluate(Context, ResultsView);

		for (int32 Index = 0; Index < NumPoints; Index++)
		{
			float ScalarRight = 1.f;

			float ScalarLeft = 0.f;
			{ //  FCullingField::Evaluate
				float  RadialFalloff2Delta2 = (RadialFalloff2->Position - SamplesArray[Index]).SizeSquared();
				if (RadialFalloff2Delta2 < RadialFalloff2Radius2)
				{
					ScalarLeft = RadialFalloff2->Magnitude * (RadialFalloff2Radius2 - RadialFalloff2Delta2) / RadialFalloff2Radius2;
				}
			}

			float ExpectedVal = ScalarLeft * ScalarRight;

			//UE_LOG(GCTF_Log, Error, TEXT("[%d:%3.5f] sample:(%3.5f,%3.5f,%3.5f) %3.5f -> %3.5f"), Index,
			//	ResultsView[Index] - ExpectedVal,
			//	SamplesArray[Index].X, SamplesArray[Index].Y, SamplesArray[Index].Z,
			//	ExpectedVal, ResultsView[Index]);

			R.ExpectTrue((ResultsView[Index] - ExpectedVal) < KINDA_SMALL_NUMBER);
		}
		delete CullingField;

		return !R.HasError();
	}
	template bool Fields_Culling<float>(ExampleResponse&& R);


	FFieldSystemCommand SaveAndLoad(FFieldSystemCommand & CommandOut)
	{
		FBufferArchive Ar;
		Ar.SetIsSaving(true);
		Ar.SetIsLoading(false);
		FString Filename = "Fields_SerializeAPI.tmp";

		FFieldSystemCommand CommandIn;

		CommandOut.Serialize(Ar);
		FFileHelper::SaveArrayToFile(Ar, *Filename);
		Ar.FlushCache();
		Ar.Empty();

		TArray<uint8> InputArray;
		FFileHelper::LoadFileToArray(InputArray, *Filename);
		FMemoryReader InputArchive = FMemoryReader(InputArray, true);
		InputArchive.Seek(0);
		CommandIn.Serialize(InputArchive);

		return CommandIn;
	}

	template<class T>
	bool Fields_SerializeAPI(ExampleResponse&& R)
	{
		{
			FFieldSystemCommand CommandOut("FUniformInteger", new FUniformInteger(3));
			FFieldSystemCommand CommandIn = SaveAndLoad(CommandOut);
			R.ExpectTrue(CommandIn == CommandOut);
		}

		{
			FFieldSystemCommand CommandOut("FRadialIntMask", new FRadialIntMask(1.f,FVector(3,5,7),11,13) );
			FFieldSystemCommand CommandIn = SaveAndLoad(CommandOut);
			R.ExpectTrue(CommandIn == CommandOut);
		}

		{
			FFieldSystemCommand CommandOut("FUniformScalar", new FUniformScalar(13));
			FFieldSystemCommand CommandIn = SaveAndLoad(CommandOut);
			R.ExpectTrue(CommandIn == CommandOut);
		}

		{
			FFieldSystemCommand CommandOut("FRadialFalloff", new FRadialFalloff(1.f,3.f,5.f,7.f,11.f,FVector(13, 17, 19)));
			FFieldSystemCommand CommandIn = SaveAndLoad(CommandOut);
			R.ExpectTrue(CommandIn == CommandOut);
		}

		{
			FFieldSystemCommand CommandOut("FPlaneFalloff", new FPlaneFalloff(1.f, 3.f, 5.f, 7.f, 100.f, FVector(9, 11, 13), FVector(17, 19, 23)));
			FFieldSystemCommand CommandIn = SaveAndLoad(CommandOut);
			R.ExpectTrue(CommandIn == CommandOut);
		}
		
		{
			FFieldSystemCommand CommandOut("FBoxFalloff", new FBoxFalloff(1.f, 7.f, 9.f, 13.f, FTransform::Identity));
			FFieldSystemCommand CommandIn = SaveAndLoad(CommandOut);
			R.ExpectTrue(CommandIn == CommandOut);
		}
		
		{
			FFieldSystemCommand CommandOut("FNoiseField", new	FNoiseField(1.f, 3.f));
			FFieldSystemCommand CommandIn = SaveAndLoad(CommandOut);
			R.ExpectTrue(CommandIn == CommandOut);
		}

		{
			FFieldSystemCommand CommandOut("FUniformVector", new FUniformVector(1.f, FVector(3, 5, 7)));
			FFieldSystemCommand CommandIn = SaveAndLoad(CommandOut);
			R.ExpectTrue(CommandIn == CommandOut);
		}

		{
			FFieldSystemCommand CommandOut("FRadialVector", new FRadialVector(1.f,FVector(3, 5, 7)));
			FFieldSystemCommand CommandIn = SaveAndLoad(CommandOut);
			R.ExpectTrue(CommandIn == CommandOut);
		}

		{
			FFieldSystemCommand CommandOut("FRandomVector", new FRandomVector(1.f));
			FFieldSystemCommand CommandIn = SaveAndLoad(CommandOut);
			R.ExpectTrue(CommandIn == CommandOut);
		}

		{
			FUniformScalar* UniformScalar = new FUniformScalar(1.f);
			FRadialFalloff* RadialScalar = new FRadialFalloff(1.f, 3.f, 5.f, 7.f, 11.f, FVector(13, 17, 19));
			FFieldSystemCommand CommandOut("FSumScalar", new FSumScalar(1.f, UniformScalar, RadialScalar, EFieldOperationType::Field_Substract));
			FFieldSystemCommand CommandIn = SaveAndLoad(CommandOut);
			R.ExpectTrue(CommandIn == CommandOut);
		}

		{
			FUniformScalar* UniformScalar = new FUniformScalar(41.f);
			FUniformVector* UniformVector = new FUniformVector(31.f, FVector(3, 5, 7));
			FRadialVector* RadialVector = new FRadialVector(21.f, FVector(3, 5, 7));
			FFieldSystemCommand CommandOut("FSumVector", new FSumVector(1.f, UniformScalar, UniformVector, RadialVector, EFieldOperationType::Field_Divide));
			FFieldSystemCommand CommandIn = SaveAndLoad(CommandOut);
			R.ExpectTrue(CommandIn == CommandOut);
		}

		// conversion fields
		{
			FUniformScalar* UniformScalar = new FUniformScalar(41.f);
			FFieldSystemCommand CommandOut("FConversionField", new FConversionField<float,int32>(UniformScalar));
			FFieldSystemCommand CommandIn = SaveAndLoad(CommandOut);
			R.ExpectTrue(CommandIn == CommandOut);
		}
		{
			FUniformInteger* UniformInteger = new FUniformInteger(3);
			FFieldSystemCommand CommandOut("FConversionField", new FConversionField<int32, float>(UniformInteger));
			FFieldSystemCommand CommandIn = SaveAndLoad(CommandOut);
			R.ExpectTrue(CommandIn == CommandOut);
		}

		// culling fields
		{
			FUniformInteger* UniformInteger = new FUniformInteger(3);
			FRadialFalloff* RadialScalar = new FRadialFalloff(1.f, 3.f, 5.f, 7.f, 11.f, FVector(13, 17, 19));
			FFieldSystemCommand CommandOut("FCullingField", new FCullingField<int32>(RadialScalar,UniformInteger));
			FFieldSystemCommand CommandIn = SaveAndLoad(CommandOut);
			R.ExpectTrue(CommandIn == CommandOut);
		}
		{
			FUniformScalar* UniformScalar = new FUniformScalar(3.f);
			FRadialFalloff* RadialScalar = new FRadialFalloff(1.f, 3.f, 5.f, 7.f, 11.f, FVector(13, 17, 19));
			FFieldSystemCommand CommandOut("FCullingField", new FCullingField<float>(RadialScalar, UniformScalar));
			FFieldSystemCommand CommandIn = SaveAndLoad(CommandOut);
			R.ExpectTrue(CommandIn == CommandOut);
		}
		{
			FUniformVector* UniformVector = new FUniformVector(3.f);
			FRadialFalloff* RadialScalar = new FRadialFalloff(1.f, 3.f, 5.f, 7.f, 11.f, FVector(13, 17, 19));
			FFieldSystemCommand CommandOut("FCullingField", new FCullingField<FVector>(RadialScalar, UniformVector));
			FFieldSystemCommand CommandIn = SaveAndLoad(CommandOut);
			R.ExpectTrue(CommandIn == CommandOut);
		}

		// terminals
		{
			FFieldSystemCommand CommandOut("FReturnResultsTerminal", new FReturnResultsTerminal<int32>());
			FFieldSystemCommand CommandIn = SaveAndLoad(CommandOut);
			R.ExpectTrue(CommandIn == CommandOut);
		}
		{
			FFieldSystemCommand CommandOut("FReturnResultsTerminal", new FReturnResultsTerminal<float>());
			FFieldSystemCommand CommandIn = SaveAndLoad(CommandOut);
			R.ExpectTrue(CommandIn == CommandOut);
		}
		{
			FFieldSystemCommand CommandOut("FReturnResultsTerminal", new FReturnResultsTerminal<FVector>());
			FFieldSystemCommand CommandIn = SaveAndLoad(CommandOut);
			R.ExpectTrue(CommandIn == CommandOut);
		}


		// depth test with lots of nodes.
		{
			FUniformScalar* UniformScalar = new FUniformScalar(3);
			FConversionField<float, int32>* ConversionFieldFI = new FConversionField<float, int32>(UniformScalar);

			FBoxFalloff* BoxFalloff = new FBoxFalloff(1.f, 7.f, 9.f, 13.f, FTransform::Identity);
			FCullingField<int32>* CullingFieldI = new FCullingField<int32>(BoxFalloff, ConversionFieldFI);

			FUniformInteger* UniformInteger = new FUniformInteger(3);
			FConversionField<int32, float>*  ConversionFieldIF = new FConversionField<int32, float>(UniformInteger);

			FPlaneFalloff* PlaneFalloff = new FPlaneFalloff(1.f, 3.f, 5.f, 7.f, 100.f, FVector(9, 11, 13), FVector(17, 19, 23));
			FCullingField<float>* CullingFieldF = new FCullingField<float>(PlaneFalloff, ConversionFieldIF);


			FNoiseField* NoiseField2 = new FNoiseField(1.f, 3.f);
			FRandomVector* RandomVector = new FRandomVector(1.f);
			FCullingField<FVector>* CullingFieldV = new FCullingField<FVector>(NoiseField2, RandomVector);

			FNoiseField* NoiseField = new FNoiseField(1.f, 3.f);
			FSumScalar* SumScalar = new FSumScalar(1.f, CullingFieldF, NoiseField, EFieldOperationType::Field_Substract);

			FUniformVector* UniformVector = new FUniformVector(1.f, FVector(3, 5, 7));
			FSumVector* SumVector = new FSumVector(1.f, SumScalar, CullingFieldV, UniformVector, EFieldOperationType::Field_Divide);

			FReturnResultsTerminal<int32>*  ReturnResultsTerminalI = new FReturnResultsTerminal<int32>();
			FConversionField<int32, float>*  ConversionFieldIF2 = new FConversionField<int32, float>(ReturnResultsTerminalI);
			FReturnResultsTerminal<float>* ReturnResultsTerminalF = new FReturnResultsTerminal<float>();
			FSumScalar* SumScalar2 = new FSumScalar(1.f, ReturnResultsTerminalF, ConversionFieldIF2, EFieldOperationType::Field_Substract);


			FReturnResultsTerminal<FVector>* ReturnResultsTerminalV =new FReturnResultsTerminal<FVector>();
			FSumVector* SumVector2 = new FSumVector(1.f, SumScalar2, ReturnResultsTerminalV, SumVector, EFieldOperationType::Field_Divide);

			FReturnResultsTerminal<FVector>* ReturnResultsTerminalV2 = new FReturnResultsTerminal<FVector>();
			FConversionField<int32, float>*  ConversionFieldIF3 = new FConversionField<int32, float>(CullingFieldI);
			FSumVector* SumVector3 = new FSumVector(1.f, ConversionFieldIF3, SumVector2, ReturnResultsTerminalV2, EFieldOperationType::Field_Divide);

			FFieldSystemCommand CommandOut("DeepTreeOfEverything", SumVector3);
			FFieldSystemCommand CommandIn = SaveAndLoad(CommandOut);
			R.ExpectTrue(CommandIn == CommandOut);
		}

		return !R.HasError();

	}
	template bool Fields_SerializeAPI<float>(ExampleResponse&& R);


}
