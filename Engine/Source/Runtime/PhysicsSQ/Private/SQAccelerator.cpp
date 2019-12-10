// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SQAccelerator.h"
#include "CollisionQueryFilterCallbackCore.h"

#if PHYSICS_INTERFACE_PHYSX
#include "SceneQueryPhysXImp.h"	//todo: use nice platform wrapper
#include "PhysXInterfaceWrapperCore.h"
#endif

#if WITH_CHAOS
#include "Experimental/SceneQueryChaosImp.h"
#endif

#include "ChaosInterfaceWrapperCore.h"
#include "Chaos/ISpatialAcceleration.h"
#include "Chaos/PBDCollisionConstraint.h"
#include "Chaos/GeometryQueries.h"
#include "Chaos/DebugDrawQueue.h"

#if CHAOS_DEBUG_DRAW
int32 ChaosSQDrawDebugVisitorQueries = 0;
FAutoConsoleVariableRef CVarChaosSQDrawDebugQueries(TEXT("p.Chaos.SQ.DrawDebugVisitorQueries"), ChaosSQDrawDebugVisitorQueries, TEXT("Draw bounds of objects visited by visitors in scene queries."));
#endif

void FSQAcceleratorUnion::Raycast(const FVector& Start, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitRaycast>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	for (const ISQAccelerator* Accelerator : Accelerators)
	{
		Accelerator->Raycast(Start, Dir, DeltaMagnitude, HitBuffer, OutputFlags, QueryFilterData, QueryCallback);
	}
}

void FSQAcceleratorUnion::Sweep(const FPhysicsGeometry& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitSweep>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	for (const ISQAccelerator* Accelerator : Accelerators)
	{
		Accelerator->Sweep(QueryGeom, StartTM, Dir, DeltaMagnitude, HitBuffer, OutputFlags, QueryFilterData, QueryCallback);
	}
}

void FSQAcceleratorUnion::Overlap(const FPhysicsGeometry& QueryGeom, const FTransform& GeomPose, FPhysicsHitCallback<FHitOverlap>& HitBuffer, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	for (const ISQAccelerator* Accelerator : Accelerators)
	{
		Accelerator->Overlap(QueryGeom, GeomPose, HitBuffer, QueryFilterData, QueryCallback);
	}
}

void FSQAcceleratorUnion::AddSQAccelerator(ISQAccelerator* InAccelerator)
{
	Accelerators.AddUnique(InAccelerator);
}

void FSQAcceleratorUnion::RemoveSQAccelerator(ISQAccelerator* AcceleratorToRemove)
{
	Accelerators.RemoveSingleSwap(AcceleratorToRemove);	//todo(ocohen): probably want to order these in some optimal way
}

FChaosSQAccelerator::FChaosSQAccelerator(const Chaos::ISpatialAcceleration<Chaos::TAccelerationStructureHandle<float, 3>, float, 3>& InSpatialAcceleration)
	: SpatialAcceleration(InSpatialAcceleration)
{}

struct FPreFilterInfo
{
	const Chaos::TImplicitObject<float, 3>* Geom;
	int32 ActorIdx;
};

void FillHitHelper(ChaosInterface::FLocationHit& Hit, const float Distance, const FVector& WorldPosition, const FVector& WorldNormal, int32 FaceIdx)
{
	Hit.Distance = Distance;
	Hit.WorldPosition = WorldPosition;
	Hit.WorldNormal = WorldNormal;
	Hit.Flags = Distance > 0 ? EHitFlags::Distance | EHitFlags::Normal | EHitFlags::Position : EHitFlags::Distance | EHitFlags::FaceIndex;
	Hit.FaceIndex = FaceIdx;
}

void FillHitHelper(ChaosInterface::FOverlapHit& Hit, const float Distance, const FVector& WorldPosition, const FVector& WorldNormal, int32 FaceIdx)
{
}

template <typename TPayload, typename THitType>
struct TSQVisitor : public Chaos::ISpatialVisitor<TPayload, float>
{
	TSQVisitor(const FVector& InStartPoint, const FVector& InDir, ChaosInterface::FSQHitBuffer<ChaosInterface::FRaycastHit>& InHitBuffer, EHitFlags InOutputFlags,
		const FQueryFilterData& InQueryFilterData, ICollisionQueryFilterCallbackBase& InQueryCallback, const FQueryDebugParams& InDebugParams)
		: StartPoint(InStartPoint)
		, Dir(InDir)
		, HitBuffer(InHitBuffer)
		, OutputFlags(InOutputFlags)
		, QueryFilterData(InQueryFilterData)
		, QueryCallback(InQueryCallback)
		, bAnyHit(false)
		, DebugParams(InDebugParams)
	{
#if WITH_PHYSX
		//#TODO - reimplement query flags alternative for Chaos
		bAnyHit = QueryFilterData.flags & PxQueryFlag::eANY_HIT;
#endif
	}

	TSQVisitor(const FTransform& InStartTM, const FVector& InDir, ChaosInterface::FSQHitBuffer<ChaosInterface::FSweepHit>& InHitBuffer, EHitFlags InOutputFlags,
		const FQueryFilterData& InQueryFilterData, ICollisionQueryFilterCallbackBase& InQueryCallback, const Chaos::TImplicitObject<float,3>& InQueryGeom, const FQueryDebugParams& InDebugParams)
		: StartTM(InStartTM)
		, Dir(InDir)
		, HitBuffer(InHitBuffer)
		, OutputFlags(InOutputFlags)
		, QueryFilterData(InQueryFilterData)
		, QueryCallback(InQueryCallback)
		, bAnyHit(false)
		, QueryGeom(&InQueryGeom)
		, DebugParams(InDebugParams)
	{
#if WITH_PHYSX
		//#TODO - reimplement query flags alternative for Chaos
		bAnyHit = QueryFilterData.flags & PxQueryFlag::eANY_HIT;
#endif
		//todo: check THitType is sweep
	}

	TSQVisitor(const FTransform& InWorldTM, ChaosInterface::FSQHitBuffer<ChaosInterface::FOverlapHit>& InHitBuffer,
		const FQueryFilterData& InQueryFilterData, ICollisionQueryFilterCallbackBase& InQueryCallback, const Chaos::TImplicitObject<float, 3>& InQueryGeom, const FQueryDebugParams& InDebugParams)
		: StartTM(InWorldTM)
		, HitBuffer(InHitBuffer)
		, QueryFilterData(InQueryFilterData)
		, QueryCallback(InQueryCallback)
		, bAnyHit(false)
		, QueryGeom(&InQueryGeom)
		, DebugParams(InDebugParams)
	{
#if WITH_PHYSX
		//#TODO - reimplement query flags alternative for Chaos
		bAnyHit = QueryFilterData.flags & PxQueryFlag::eANY_HIT;
#endif
		//todo: check THitType is overlap
	}

	virtual bool Raycast(const Chaos::TSpatialVisitorData<TPayload>& Instance, float& CurLength) override
	{
		return Visit<ESQType::Raycast>(Instance, CurLength);
	}

	virtual bool Sweep(const Chaos::TSpatialVisitorData<TPayload>& Instance, float& CurLength) override
	{
		return Visit<ESQType::Sweep>(Instance, CurLength);
	}

	virtual bool Overlap(const Chaos::TSpatialVisitorData<TPayload>& Instance) override
	{
		float DummyLength;
		return Visit<ESQType::Overlap>(Instance, DummyLength);
	}

private:

	enum class ESQType
	{
		Raycast,
		Sweep,
		Overlap
	};

	template <ESQType SQ>
	bool Visit(const Chaos::TSpatialVisitorData<TPayload>& Instance, float& CurLength)
	{
#if CHAOS_DEBUG_DRAW && WITH_CHAOS
		if (DebugParams.IsDebugQuery() && ChaosSQDrawDebugVisitorQueries)
		{
			DebugDraw<SQ>(Instance, CurLength);
		}
#endif

		TPayload Payload = Instance.Payload;

		//todo: add a check to ensure hitbuffer matches SQ type
		using namespace Chaos;
		TGeometryParticle<float, 3>* GeometryParticle = Payload.GetExternalGeometryParticle_ExternalThread();
		const TShapesArray<float,3>& Shapes = GeometryParticle->ShapesArray();

		for (const auto& Shape : Shapes)
		{
			const TImplicitObject<float, 3>* Geom = Shape->Geometry.Get();
			//TODO: use gt particles directly
			//#TODO alternative to px flags
#if WITH_PHYSX
			ECollisionQueryHitType HitType = QueryFilterData.flags & PxQueryFlag::ePREFILTER ? QueryCallback.PreFilter(P2UFilterData(QueryFilterData.data), *Shape, *GeometryParticle) : ECollisionQueryHitType::Block;
#else
			//#TODO Chaos flag alternative
			ensure(false);
			ECollisionQueryHitType HitType = ECollisionQueryHitType::Block;
#endif
			if (HitType != ECollisionQueryHitType::None)
			{
				const TRigidTransform<float, 3> ActorTM(GeometryParticle->X(), GeometryParticle->R());

				THitType Hit;
				Hit.Actor = GeometryParticle;
				Hit.Shape = Shape.Get();

				bool bHit = false;

				TVector<float, 3> WorldPosition, WorldNormal;
				float Distance = 0;	//not needed but fixes compiler warning for overlap
				int32 FaceIdx = INDEX_NONE;	//not needed but fixes compiler warning for overlap

				if (SQ == ESQType::Raycast)
				{
					TVector<float, 3> LocalNormal;
					TVector<float, 3> LocalPosition;

					const TVector<float, 3> DirLocal = ActorTM.InverseTransformVectorNoScale(Dir);
					const TVector<float, 3> StartLocal = ActorTM.InverseTransformPositionNoScale(StartPoint);
					bHit = Geom->Raycast(StartLocal, DirLocal, CurLength, /*Thickness=*/0.f, Distance, LocalPosition, LocalNormal, FaceIdx);
					if (bHit)
					{
						WorldPosition = ActorTM.TransformPositionNoScale(LocalPosition);
						WorldNormal = ActorTM.TransformVectorNoScale(LocalNormal);
					}
				}
				else if(SQ == ESQType::Sweep && CurLength > 0)
				{
					bHit = SweepQuery<float, 3>(*Geom, ActorTM, *QueryGeom, StartTM, Dir, CurLength, Distance, WorldPosition, WorldNormal, FaceIdx);
				}
				else if (SQ == ESQType::Overlap || (SQ == ESQType::Sweep && CurLength == 0))
				{
					bHit = OverlapQuery<float, 3>(*Geom, ActorTM, *QueryGeom, StartTM, /*Thickness=*/0);
				}

				if(bHit)
				{
					FillHitHelper(Hit, Distance, WorldPosition, WorldNormal, FaceIdx);
#if WITH_PHYSX
					HitType = QueryFilterData.flags & PxQueryFlag::ePOSTFILTER ? QueryCallback.PostFilter(P2UFilterData(QueryFilterData.data), Hit) : HitType;
#else
					//#TODO Chaos flag alternative
					ensure(false);
#endif
					if (HitType != ECollisionQueryHitType::None)
					{

						//overlap never blocks
						const bool bBlocker = SQ != ESQType::Overlap && (HitType == ECollisionQueryHitType::Block || bAnyHit || HitBuffer.WantsSingleResult());
						HitBuffer.InsertHit(Hit, bBlocker);

						if (bBlocker)
						{
							CurLength = Distance;
							if (CurLength == 0 && (SQ == ESQType::Raycast || HitBuffer.WantsSingleResult()))	//raycasts always fail with distance 0, sweeps only matter if we want multi overlaps
							{
								return false;	//initial overlap so nothing will be better than this
							}
						}

						if (bAnyHit)
						{
							return false;
						}
					}
				}
			}
		}

		return true;
	}

#if CHAOS_DEBUG_DRAW && !(UE_BUILD_TEST || UE_BUILD_SHIPPING)
	template <ESQType SQ>
	void DebugDraw(const Chaos::TSpatialVisitorData<TPayload>& Instance, const float CurLength)
	{
		if (SQ == ESQType::Raycast)
		{
			const FVector EndPoint = StartPoint + (Dir * CurLength);
			Chaos::FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(StartPoint, EndPoint, 5.f, FColor::Green, false, -1.f, 0, 1.f);
		}

		if (Instance.bHasBounds)
		{
			Chaos::FDebugDrawQueue::GetInstance().DrawDebugBox(Instance.Bounds.Center(), Instance.Bounds.Extents(), FQuat::Identity, FColor::Red, false, -1.f, 0, 2.f);
		}
	}
#endif

	const FTransform StartTM;
	const FVector StartPoint;
	const FVector Dir;
	ChaosInterface::FSQHitBuffer<THitType>& HitBuffer;
	EHitFlags OutputFlags;
	const FQueryFilterData& QueryFilterData;
	ICollisionQueryFilterCallbackBase& QueryCallback;
	bool bAnyHit;
	const Chaos::TImplicitObject<float, 3>* QueryGeom;
	const FQueryDebugParams DebugParams;
};

void FChaosSQAccelerator::Raycast(const FVector& Start, const FVector& Dir, const float DeltaMagnitude, ChaosInterface::FSQHitBuffer<ChaosInterface::FRaycastHit>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback, const FQueryDebugParams& DebugParams) const
{
	using namespace Chaos;
	using namespace ChaosInterface;

	TSQVisitor<TAccelerationStructureHandle<float, 3>, FRaycastHit> RaycastVisitor(Start, Dir, HitBuffer, OutputFlags, QueryFilterData, QueryCallback, DebugParams);
	HitBuffer.IncFlushCount();
	SpatialAcceleration.Raycast(Start, Dir, DeltaMagnitude, RaycastVisitor);
	HitBuffer.DecFlushCount();
}

void FChaosSQAccelerator::Sweep(const Chaos::TImplicitObject<float, 3>& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float DeltaMagnitude, ChaosInterface::FSQHitBuffer<ChaosInterface::FSweepHit>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback, const FQueryDebugParams& DebugParams) const
{
	using namespace Chaos;
	using namespace ChaosInterface;

	const TBox<float, 3> Bounds = QueryGeom.BoundingBox().TransformedBox(StartTM);
	const FVector HalfExtents = Bounds.Extents() * 0.5f;
	TSQVisitor<TAccelerationStructureHandle<float, 3>, FSweepHit> SweepVisitor(StartTM, Dir, HitBuffer, OutputFlags, QueryFilterData, QueryCallback, QueryGeom, DebugParams);
	HitBuffer.IncFlushCount();
	SpatialAcceleration.Sweep(Bounds.GetCenter(), Dir, DeltaMagnitude, HalfExtents, SweepVisitor);
	HitBuffer.DecFlushCount();
}

void FChaosSQAccelerator::Overlap(const Chaos::TImplicitObject<float, 3>& QueryGeom, const FTransform& GeomPose, ChaosInterface::FSQHitBuffer<ChaosInterface::FOverlapHit>& HitBuffer, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback, const FQueryDebugParams& DebugParams) const
{
	using namespace Chaos;
	using namespace ChaosInterface;

	const TBox<float, 3> Bounds = QueryGeom.BoundingBox().TransformedBox(GeomPose);
	TSQVisitor<TAccelerationStructureHandle<float, 3>, FOverlapHit> OverlapVisitor(GeomPose, HitBuffer, QueryFilterData, QueryCallback, QueryGeom, DebugParams);
	HitBuffer.IncFlushCount();
	SpatialAcceleration.Overlap(Bounds, OverlapVisitor);
	HitBuffer.DecFlushCount();
}

#if WITH_PHYSX
FChaosSQAcceleratorAdapter::FChaosSQAcceleratorAdapter(const Chaos::ISpatialAcceleration<Chaos::TAccelerationStructureHandle<float, 3>, float, 3>& InSpatialAcceleration)
	: ChaosSQAccelerator(InSpatialAcceleration)
{
}

void FChaosSQAcceleratorAdapter::Raycast(const FVector& Start, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitRaycast>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	check(false);
}

void FChaosSQAcceleratorAdapter::Sweep(const FPhysicsGeometry& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitSweep>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	check(false);
}

void FChaosSQAcceleratorAdapter::Overlap(const FPhysicsGeometry& QueryGeom, const FTransform& GeomPose, FPhysicsHitCallback<FHitOverlap>& HitBuffer, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	check(false);
}
#endif


#if WITH_PHYSX && !WITH_CHAOS

FPhysXSQAccelerator::FPhysXSQAccelerator()
	: Scene(nullptr)
{

}

FPhysXSQAccelerator::FPhysXSQAccelerator(physx::PxScene* InScene)
	: Scene(InScene)
{

}

void FPhysXSQAccelerator::Raycast(const FVector& Start, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitRaycast>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	check(Scene);

	FPhysicsRaycastInputAdapater Inputs(Start, Dir, OutputFlags);
	Scene->raycast(Inputs.Start, Inputs.Dir, DeltaMagnitude, HitBuffer, Inputs.OutputFlags, QueryFilterData, &QueryCallback);
}

void FPhysXSQAccelerator::Sweep(const FPhysicsGeometry& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float DeltaMagnitude, FPhysicsHitCallback<FHitSweep>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	check(Scene);

	FPhysicsSweepInputAdapater Inputs(StartTM, Dir, OutputFlags);
	Scene->sweep(QueryGeom, Inputs.StartTM, Inputs.Dir, DeltaMagnitude, HitBuffer, Inputs.OutputFlags, QueryFilterData, &QueryCallback);
}

void FPhysXSQAccelerator::Overlap(const FPhysicsGeometry& QueryGeom, const FTransform& GeomPose, FPhysicsHitCallback<FHitOverlap>& HitBuffer, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const
{
	check(Scene);

	FPhysicsOverlapInputAdapater Inputs(GeomPose);
	Scene->overlap(QueryGeom, Inputs.GeomPose, HitBuffer, QueryFilterData, &QueryCallback);
}

void FPhysXSQAccelerator::SetScene(physx::PxScene* InScene)
{
	Scene = InScene;
}
#endif
