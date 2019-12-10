// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ProfilingDebugging/ScopedTimers.h"

#if WITH_PHYSX

#include "Chaos/PBDRigidsEvolutionGBF.h"

#ifndef SQ_REPLAY_TEST
#define SQ_REPLAY_TEST(cond) bEnsureOnMismatch ? ensure(cond) : (cond)
#endif

bool SQComparisonHelper(FPhysTestSerializer& Serializer, bool bEnsureOnMismatch = false)
{
	using namespace Chaos;

	bool bTestPassed = true;
	const float DistanceTolerance = 1e-1f;
	const float NormalTolerance = 1e-2f;

	const FSQCapture& CapturedSQ = *Serializer.GetSQCapture();
	switch (CapturedSQ.SQType)
	{
	case FSQCapture::ESQType::Raycast:
	{
		auto PxHitBuffer = MakeUnique<PhysXInterface::FDynamicHitBuffer<PxRaycastHit>>();
		Serializer.GetPhysXData()->raycast(U2PVector(CapturedSQ.StartPoint), U2PVector(CapturedSQ.Dir), CapturedSQ.DeltaMag, *PxHitBuffer, U2PHitFlags(CapturedSQ.OutputFlags.HitFlags), CapturedSQ.QueryFilterData, CapturedSQ.FilterCallback.Get());

		bTestPassed &= SQ_REPLAY_TEST(PxHitBuffer->hasBlock == CapturedSQ.PhysXRaycastBuffer.hasBlock);
		bTestPassed &= SQ_REPLAY_TEST(PxHitBuffer->GetNumHits() == CapturedSQ.PhysXRaycastBuffer.GetNumHits());
		for (int32 Idx = 0; Idx < PxHitBuffer->GetNumHits(); ++Idx)
		{
			//bTestPassed &= SQ_REPLAY_TEST(FMath::IsNearlyEqual(PxHitBuffer.GetHits()[Idx].normal.x, CapturedSQ.PhysXRaycastBuffer.GetHits()[Idx].normal.x));
			//bTestPassed &= SQ_REPLAY_TEST(FMath::IsNearlyEqual(PxHitBuffer.GetHits()[Idx].normal.y, CapturedSQ.PhysXRaycastBuffer.GetHits()[Idx].normal.y));
			//bTestPassed &= SQ_REPLAY_TEST(FMath::IsNearlyEqual(PxHitBuffer.GetHits()[Idx].normal.z, CapturedSQ.PhysXRaycastBuffer.GetHits()[Idx].normal.z));
		}

		if (PxHitBuffer->hasBlock)
		{
			bTestPassed &= SQ_REPLAY_TEST(FMath::IsNearlyEqual(PxHitBuffer->block.position.x, CapturedSQ.PhysXRaycastBuffer.block.position.x, DistanceTolerance));
			bTestPassed &= SQ_REPLAY_TEST(FMath::IsNearlyEqual(PxHitBuffer->block.position.y, CapturedSQ.PhysXRaycastBuffer.block.position.y, DistanceTolerance));
			bTestPassed &= SQ_REPLAY_TEST(FMath::IsNearlyEqual(PxHitBuffer->block.position.z, CapturedSQ.PhysXRaycastBuffer.block.position.z, DistanceTolerance));
		}

		auto ChaosHitBuffer = MakeUnique<ChaosInterface::FSQHitBuffer<ChaosInterface::FRaycastHit>>();
		TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<float, 3>, float, 3>> Accelerator;
		Serializer.GetChaosData()->UpdateExternalAccelerationStructure(Accelerator);
		FChaosSQAccelerator SQAccelerator(*Accelerator); SQAccelerator.Raycast(CapturedSQ.StartPoint, CapturedSQ.Dir, CapturedSQ.DeltaMag, *ChaosHitBuffer, CapturedSQ.OutputFlags.HitFlags, CapturedSQ.QueryFilterData, *CapturedSQ.FilterCallback);
		
		bTestPassed &= SQ_REPLAY_TEST(ChaosHitBuffer->HasBlockingHit() == CapturedSQ.PhysXRaycastBuffer.hasBlock);
		bTestPassed &= SQ_REPLAY_TEST(ChaosHitBuffer->GetNumHits() == CapturedSQ.PhysXRaycastBuffer.GetNumHits());
		for (int32 Idx = 0; Idx < ChaosHitBuffer->GetNumHits(); ++Idx)
		{
			//not sorted
			//bTestPassed &= SQ_REPLAY_TEST(FMath::IsNearlyEqual(ChaosHitBuffer.GetHits()[Idx].WorldNormal.X, CapturedSQ.PhysXRaycastBuffer.GetHits()[Idx].normal.x));
			//bTestPassed &= SQ_REPLAY_TEST(FMath::IsNearlyEqual(ChaosHitBuffer.GetHits()[Idx].WorldNormal.Y, CapturedSQ.PhysXRaycastBuffer.GetHits()[Idx].normal.y));
			//bTestPassed &= SQ_REPLAY_TEST(FMath::IsNearlyEqual(ChaosHitBuffer.GetHits()[Idx].WorldNormal.Z, CapturedSQ.PhysXRaycastBuffer.GetHits()[Idx].normal.z));
		}

		if (ChaosHitBuffer->HasBlockingHit())
		{
			bTestPassed &= SQ_REPLAY_TEST(FMath::IsNearlyEqual(ChaosHitBuffer->GetBlock()->WorldPosition.X, CapturedSQ.PhysXRaycastBuffer.block.position.x, DistanceTolerance));
			bTestPassed &= SQ_REPLAY_TEST(FMath::IsNearlyEqual(ChaosHitBuffer->GetBlock()->WorldPosition.Y, CapturedSQ.PhysXRaycastBuffer.block.position.y, DistanceTolerance));
			bTestPassed &= SQ_REPLAY_TEST(FMath::IsNearlyEqual(ChaosHitBuffer->GetBlock()->WorldPosition.Z, CapturedSQ.PhysXRaycastBuffer.block.position.z, DistanceTolerance));

			bTestPassed &= SQ_REPLAY_TEST(FMath::IsNearlyEqual(ChaosHitBuffer->GetBlock()->WorldNormal.X, CapturedSQ.PhysXRaycastBuffer.block.normal.x, NormalTolerance));
			bTestPassed &= SQ_REPLAY_TEST(FMath::IsNearlyEqual(ChaosHitBuffer->GetBlock()->WorldNormal.Y, CapturedSQ.PhysXRaycastBuffer.block.normal.y, NormalTolerance));
			bTestPassed &= SQ_REPLAY_TEST(FMath::IsNearlyEqual(ChaosHitBuffer->GetBlock()->WorldNormal.Z, CapturedSQ.PhysXRaycastBuffer.block.normal.z, NormalTolerance));
		}
		break;
	}
	case FSQCapture::ESQType::Sweep:
	{
		//For sweep there are many solutions (many contacts possible) so we only bother testing for Distance
		auto PxHitBuffer = MakeUnique<PhysXInterface::FDynamicHitBuffer<PxSweepHit>>();
		Serializer.GetPhysXData()->sweep(CapturedSQ.PhysXGeometry.any(), U2PTransform(CapturedSQ.StartTM), U2PVector(CapturedSQ.Dir), CapturedSQ.DeltaMag, *PxHitBuffer, U2PHitFlags(CapturedSQ.OutputFlags.HitFlags), CapturedSQ.QueryFilterData, CapturedSQ.FilterCallback.Get());

		bTestPassed &= SQ_REPLAY_TEST(PxHitBuffer->hasBlock == CapturedSQ.PhysXSweepBuffer.hasBlock);
		bTestPassed &= SQ_REPLAY_TEST(PxHitBuffer->GetNumHits() == CapturedSQ.PhysXSweepBuffer.GetNumHits());
		for (int32 Idx = 0; Idx < PxHitBuffer->GetNumHits(); ++Idx)
		{
			//bTestPassed &= SQ_REPLAY_TEST(FMath::IsNearlyEqual(PxHitBuffer.GetHits()[Idx].normal.x, CapturedSQ.PhysXSweepBuffer.GetHits()[Idx].normal.x, DistanceTolerance));
			//bTestPassed &= SQ_REPLAY_TEST(FMath::IsNearlyEqual(PxHitBuffer.GetHits()[Idx].normal.y, CapturedSQ.PhysXSweepBuffer.GetHits()[Idx].normal.y, DistanceTolerance));
			//bTestPassed &= SQ_REPLAY_TEST(FMath::IsNearlyEqual(PxHitBuffer.GetHits()[Idx].normal.z, CapturedSQ.PhysXSweepBuffer.GetHits()[Idx].normal.z, DistanceTolerance));
		}

		auto ChaosHitBuffer = MakeUnique<ChaosInterface::FSQHitBuffer<ChaosInterface::FSweepHit>>();
		TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<float, 3>, float, 3>> Accelerator;
		Serializer.GetChaosData()->UpdateExternalAccelerationStructure(Accelerator);
		FChaosSQAccelerator SQAccelerator(*Accelerator); SQAccelerator.Sweep(*CapturedSQ.ChaosGeometry, CapturedSQ.StartTM, CapturedSQ.Dir, CapturedSQ.DeltaMag, *ChaosHitBuffer, CapturedSQ.OutputFlags.HitFlags, CapturedSQ.QueryFilterData, *CapturedSQ.FilterCallback);
		
		bTestPassed &= SQ_REPLAY_TEST(ChaosHitBuffer->HasBlockingHit() == CapturedSQ.PhysXSweepBuffer.hasBlock);
		bTestPassed &= SQ_REPLAY_TEST(ChaosHitBuffer->GetNumHits() == CapturedSQ.PhysXSweepBuffer.GetNumHits());
		for (int32 Idx = 0; Idx < ChaosHitBuffer->GetNumHits(); ++Idx)
		{
			//not sorted
			//bTestPassed &= SQ_REPLAY_TEST(FMath::IsNearlyEqual(ChaosHitBuffer.GetHits()[Idx].WorldNormal.X, CapturedSQ.PhysXSweepBuffer.GetHits()[Idx].normal.x));
			//bTestPassed &= SQ_REPLAY_TEST(FMath::IsNearlyEqual(ChaosHitBuffer.GetHits()[Idx].WorldNormal.Y, CapturedSQ.PhysXSweepBuffer.GetHits()[Idx].normal.y));
			//bTestPassed &= SQ_REPLAY_TEST(FMath::IsNearlyEqual(ChaosHitBuffer.GetHits()[Idx].WorldNormal.Z, CapturedSQ.PhysXSweepBuffer.GetHits()[Idx].normal.z));
		}

		if (ChaosHitBuffer->HasBlockingHit())
		{
			bTestPassed &= SQ_REPLAY_TEST(FMath::IsNearlyEqual(ChaosHitBuffer->GetBlock()->Distance, CapturedSQ.PhysXSweepBuffer.block.distance, DistanceTolerance));
		}
		break;
	}
	case FSQCapture::ESQType::Overlap:
	{
		auto PxHitBuffer = MakeUnique<PhysXInterface::FDynamicHitBuffer<PxOverlapHit>>();
		Serializer.GetPhysXData()->overlap(CapturedSQ.PhysXGeometry.any(), U2PTransform(CapturedSQ.StartTM), *PxHitBuffer, CapturedSQ.QueryFilterData, CapturedSQ.FilterCallback.Get());

		bTestPassed &= SQ_REPLAY_TEST(PxHitBuffer->GetNumHits() == CapturedSQ.PhysXOverlapBuffer.GetNumHits());

		auto ChaosHitBuffer = MakeUnique<ChaosInterface::FSQHitBuffer<ChaosInterface::FOverlapHit>>();
		TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<float, 3>, float, 3>> Accelerator;
		Serializer.GetChaosData()->UpdateExternalAccelerationStructure(Accelerator);
		FChaosSQAccelerator SQAccelerator(*Accelerator);
		SQAccelerator.Overlap(*CapturedSQ.ChaosGeometry, CapturedSQ.StartTM, *ChaosHitBuffer, CapturedSQ.QueryFilterData, *CapturedSQ.FilterCallback);
		
		bTestPassed &= SQ_REPLAY_TEST(ChaosHitBuffer->GetNumHits() == CapturedSQ.PhysXOverlapBuffer.GetNumHits());
		break;
	}
	}

	return bTestPassed;
}

template <bool bHasPhysX = true>
void SQPerfComparisonHelper(const FString& TestName, FPhysTestSerializer& Serializer, bool bEnsureOnMismatch = false)
{
	using namespace Chaos;
	double PhysXSum = 0.0;
	double ChaosSum = 0.0;
	//double NumIterations = bHasPhysX ? 100 : 10000000000;
	double NumIterations = 100;

	const FSQCapture& CapturedSQ = *Serializer.GetSQCapture();
	switch (CapturedSQ.SQType)
	{
	case FSQCapture::ESQType::Raycast:
	{
		if (bHasPhysX)
		{
			for (double i = 0; i < NumIterations; ++i)
			{
				auto PxHitBuffer = MakeUnique<PhysXInterface::FDynamicHitBuffer<PxRaycastHit>>();
				FDurationTimer Timer(PhysXSum);
				Timer.Start();
				Serializer.GetPhysXData()->raycast(U2PVector(CapturedSQ.StartPoint), U2PVector(CapturedSQ.Dir), CapturedSQ.DeltaMag, *PxHitBuffer, U2PHitFlags(CapturedSQ.OutputFlags.HitFlags), CapturedSQ.QueryFilterData, CapturedSQ.FilterCallback.Get());
				Timer.Stop();
			}
		}

		TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<float, 3>, float, 3>> Accelerator;
		Serializer.GetChaosData()->UpdateExternalAccelerationStructure(Accelerator);
		FChaosSQAccelerator SQAccelerator(*Accelerator);
		for (double i = 0; i < NumIterations; ++i)
		{
			auto ChaosHitBuffer = MakeUnique<ChaosInterface::FSQHitBuffer<ChaosInterface::FRaycastHit>>();
			FDurationTimer Timer(ChaosSum);
			Timer.Start();
			SQAccelerator.Raycast(CapturedSQ.StartPoint, CapturedSQ.Dir, CapturedSQ.DeltaMag, *ChaosHitBuffer, CapturedSQ.OutputFlags.HitFlags, CapturedSQ.QueryFilterData, *CapturedSQ.FilterCallback);
			Timer.Stop();
		}
		break;
	}
	case FSQCapture::ESQType::Sweep:
	{
		if (bHasPhysX)
		{
			for (double i = 0; i < NumIterations; ++i)
			{
				auto PxHitBuffer = MakeUnique<PhysXInterface::FDynamicHitBuffer<PxSweepHit>>();
				FDurationTimer Timer(PhysXSum);
				Timer.Start();
				Serializer.GetPhysXData()->sweep(CapturedSQ.PhysXGeometry.any(), U2PTransform(CapturedSQ.StartTM), U2PVector(CapturedSQ.Dir), CapturedSQ.DeltaMag, *PxHitBuffer, U2PHitFlags(CapturedSQ.OutputFlags.HitFlags), CapturedSQ.QueryFilterData, CapturedSQ.FilterCallback.Get());
				Timer.Stop();
			}
		}

		TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<float, 3>, float, 3>> Accelerator;
		Serializer.GetChaosData()->UpdateExternalAccelerationStructure(Accelerator);
		FChaosSQAccelerator SQAccelerator(*Accelerator);
		for (double i = 0; i < NumIterations; ++i)
		{
			auto ChaosHitBuffer = MakeUnique<ChaosInterface::FSQHitBuffer<ChaosInterface::FSweepHit>>();
			FDurationTimer Timer(ChaosSum);
			Timer.Start();
			SQAccelerator.Sweep(*CapturedSQ.ChaosGeometry, CapturedSQ.StartTM, CapturedSQ.Dir, CapturedSQ.DeltaMag, *ChaosHitBuffer, CapturedSQ.OutputFlags.HitFlags, CapturedSQ.QueryFilterData, *CapturedSQ.FilterCallback);
			Timer.Stop();
		}
		break;
	}
	case FSQCapture::ESQType::Overlap:
	{
		if (bHasPhysX)
		{
			for (double i = 0; i < NumIterations; ++i)
			{
				auto PxHitBuffer = MakeUnique<PhysXInterface::FDynamicHitBuffer<PxOverlapHit>>();
				FDurationTimer Timer(PhysXSum);
				Timer.Start();
				Serializer.GetPhysXData()->overlap(CapturedSQ.PhysXGeometry.any(), U2PTransform(CapturedSQ.StartTM), *PxHitBuffer, CapturedSQ.QueryFilterData, CapturedSQ.FilterCallback.Get());
				Timer.Stop();
			}
		}

		TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<float, 3>, float, 3>> Accelerator;
		Serializer.GetChaosData()->UpdateExternalAccelerationStructure(Accelerator);
		FChaosSQAccelerator SQAccelerator(*Accelerator);
		for (double i = 0; i < NumIterations; ++i)
		{
			auto ChaosHitBuffer = MakeUnique<ChaosInterface::FSQHitBuffer<ChaosInterface::FOverlapHit>>();
			FDurationTimer Timer(ChaosSum);
			Timer.Start();
			SQAccelerator.Overlap(*CapturedSQ.ChaosGeometry, CapturedSQ.StartTM, *ChaosHitBuffer, CapturedSQ.QueryFilterData, *CapturedSQ.FilterCallback);
			Timer.Stop();
		}
		break;
	}
	}

	double AvgPhysX = 1000 * 1000 * PhysXSum / NumIterations;
	double AvgChaos = 1000 * 1000 * ChaosSum / NumIterations;

	if (bHasPhysX)
	{
		UE_LOG(LogPhysicsCore, Warning, TEXT("Perf Test:%s\nPhysX:%f(us), Chaos:%f(us)"), *TestName, AvgPhysX, AvgChaos);
	}
	else
	{
		UE_LOG(LogPhysicsCore, Warning, TEXT("Perf Test:%s\nChaos:%f(us)"), *TestName, AvgChaos);
	}
}

#endif

#if INCLUDE_CHAOS

bool SQValidityHelper(FPhysTestSerializer& Serializer)
{
	using namespace Chaos;

	bool bTestPassed = true;
	const float DistanceTolerance = 1e-1f;
	const float NormalTolerance = 1e-2f;

	const FSQCapture& CapturedSQ = *Serializer.GetSQCapture();
	switch (CapturedSQ.SQType)
	{
		case FSQCapture::ESQType::Raycast:
		{
			ChaosInterface::FSQHitBuffer<ChaosInterface::FRaycastHit> ChaosHitBuffer;
			TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<float, 3>, float, 3>> Accelerator;
			Serializer.GetChaosData()->UpdateExternalAccelerationStructure(Accelerator);
			FChaosSQAccelerator SQAccelerator(*Accelerator);
			SQAccelerator.Raycast(CapturedSQ.StartPoint, CapturedSQ.Dir, CapturedSQ.DeltaMag, ChaosHitBuffer, CapturedSQ.OutputFlags.HitFlags, CapturedSQ.QueryFilterData, *CapturedSQ.FilterCallback);
			
			const bool bHasBlockingHit = ChaosHitBuffer.HasBlockingHit();
			const int32 NumHits = ChaosHitBuffer.GetNumHits();
			for (int32 Idx = 0; Idx < NumHits; ++Idx)
			{
				// TODO: DO tests
			}
			break;
		}
		case FSQCapture::ESQType::Sweep:
		{
			ChaosInterface::FSQHitBuffer<ChaosInterface::FSweepHit> ChaosHitBuffer;
			TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<float, 3>, float, 3>> Accelerator;
			Serializer.GetChaosData()->UpdateExternalAccelerationStructure(Accelerator);
			FChaosSQAccelerator SQAccelerator(*Accelerator);
			SQAccelerator.Sweep(*CapturedSQ.ChaosGeometry, CapturedSQ.StartTM, CapturedSQ.Dir, CapturedSQ.DeltaMag, ChaosHitBuffer, CapturedSQ.OutputFlags.HitFlags, CapturedSQ.QueryFilterData, *CapturedSQ.FilterCallback);
			
			const bool bHasBlockingHit = ChaosHitBuffer.HasBlockingHit();
			const int32 NumHits = ChaosHitBuffer.GetNumHits();
			for (int32 Idx = 0; Idx < NumHits; ++Idx)
			{
				ChaosInterface::FSweepHit& Hit = ChaosHitBuffer.GetHits()[Idx];

				if (!HadInitialOverlap(Hit))
				{
					const int32 FaceIdx = FindFaceIndex(Hit, CapturedSQ.Dir);
					bTestPassed |= (FaceIdx != INDEX_NONE);
				}
			}

			break;
		}
		case FSQCapture::ESQType::Overlap:
		{
			ChaosInterface::FSQHitBuffer<ChaosInterface::FOverlapHit> ChaosHitBuffer;
			TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<float, 3>, float, 3>> Accelerator;
			Serializer.GetChaosData()->UpdateExternalAccelerationStructure(Accelerator);
			FChaosSQAccelerator SQAccelerator(*Accelerator);
			SQAccelerator.Overlap(*CapturedSQ.ChaosGeometry, CapturedSQ.StartTM, ChaosHitBuffer, CapturedSQ.QueryFilterData, *CapturedSQ.FilterCallback);
			break;
		}
	}

	return bTestPassed;
}

#endif
