// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if WITH_PHYSX

#include "PhysXPublic.h"
#include "Physics/PhysicsInterfaceDeclares.h"
#include "Physics/PhysicsInterfaceCore.h"
#include "PhysicsInterfaceDeclaresCore.h"

#if !WITH_CHAOS_NEEDS_TO_BE_FIXED
#include "SQAccelerator.h"
#include "PhysicsEngine/CollisionQueryFilterCallback.h"
#include "PhysicsCore.h"
#if PHYSICS_INTERFACE_PHYSX
#include "PhysXInterfaceWrapper.h"
#elif PHYSICS_INTERFACE_LLIMMEDIATE
#include "Physics/Experimental//LLImmediateInterfaceWrapper.h"
#include "Experimental/SceneQueryLLImmediateImp.h"
#endif

#include "PhysTestSerializer.h"
#include "SQVerifier.h"

int32 ForceStandardSQ = 0;
FAutoConsoleVariableRef CVarForceStandardSQ(TEXT("p.ForceStandardSQ"), ForceStandardSQ, TEXT("If enabled, we force the standard scene query even if custom SQ structure is enabled"));


#if !UE_BUILD_SHIPPING
int32 SerializeSQs = 0;
int32 SerializeBadSQs = 0;
int32 ReplaySQs = 0;

FAutoConsoleVariableRef CVarSerializeSQs(TEXT("p.SerializeSQs"), SerializeSQs, TEXT("If enabled, we create a sq capture per sq. This can be very expensive as the entire scene is saved out"));
FAutoConsoleVariableRef CVarReplaySweeps(TEXT("p.ReplaySQs"), ReplaySQs, TEXT("If enabled, we rerun the sq against chaos"));
FAutoConsoleVariableRef CVarSerializeBadSweeps(TEXT("p.SerializeBadSQs"), SerializeBadSQs, TEXT("If enabled, we create a sq capture whenever chaos and physx diverge"));

void FinalizeCapture(FPhysTestSerializer& Serializer)
{
	if (SerializeSQs)
	{
		Serializer.Serialize(TEXT("SQCapture"));
	}
#if INCLUDE_CHAOS
	if (ReplaySQs)
	{
		const bool bReplaySuccess = SQComparisonHelper(Serializer);
		if (!bReplaySuccess)
		{
			UE_LOG(LogPhysicsCore, Warning, TEXT("Chaos SQ does not match physx"));
			if (SerializeBadSQs && !SerializeSQs)
			{
				Serializer.Serialize(TEXT("BadSQCapture"));
			}
		}
	}
#endif
}
#else
constexpr int32 SerializeSQs = 0;
constexpr int32 ReplaySQs = 0;
// No-op in shipping
void FinalizeCapture(FPhysTestSerializer& Serializer) {}
#endif

void LowLevelRaycast(FPhysScene& Scene, const FVector& Start, const FVector& Dir, float DeltaMag, FPhysicsHitCallback<FHitRaycast>& HitBuffer, EHitFlags OutputFlags, FQueryFlags QueryFlags, const FCollisionFilterData& Filter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase* QueryCallback)
{
#if WITH_CUSTOM_SQ_STRUCTURE
	if (ForceStandardSQ == 0)
	{
		ISQAccelerator* SQAccelerator = Scene.GetSQAccelerator();
		SQAccelerator->Raycast(Start, Dir, DeltaMag, HitBuffer, OutputFlags, QueryFlags, Filter, QueryFilterData, *QueryCallback);
		FinalizeQuery(HitBuffer);
	}
	else
#endif
	{
#if PHYSICS_INTERFACE_PHYSX
		if (SerializeSQs | ReplaySQs)
		{
			FPhysTestSerializer Serializer;
			Serializer.SetPhysicsData(*Scene.GetPxScene());
			FSQCapture& SweepCapture = Serializer.CaptureSQ();
			SweepCapture.StartCapturePhysXRaycast(*Scene.GetPxScene(), Start, Dir, DeltaMag, OutputFlags, QueryFilterData, Filter, *QueryCallback);
			Scene.GetPxScene()->raycast(U2PVector(Start), U2PVector(Dir), DeltaMag, HitBuffer, U2PHitFlags(OutputFlags), QueryFilterData, QueryCallback);
			SweepCapture.EndCapturePhysXRaycast(HitBuffer);

			FinalizeCapture(Serializer);
		}
		else
		{
			Scene.GetPxScene()->raycast(U2PVector(Start), U2PVector(Dir), DeltaMag, HitBuffer, U2PHitFlags(OutputFlags), QueryFilterData, QueryCallback);
		}
#endif
	}
}

void LowLevelSweep(FPhysScene& Scene, const FPhysicsGeometry& QueryGeom, const FTransform& StartTM, const FVector& Dir, float DeltaMag, FPhysicsHitCallback<FHitSweep>& HitBuffer, EHitFlags OutputFlags, FQueryFlags QueryFlags, const FCollisionFilterData& Filter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase* QueryCallback)
{
#if WITH_CUSTOM_SQ_STRUCTURE
	if (ForceStandardSQ == 0)
	{
		ISQAccelerator* SQAccelerator = Scene.GetSQAccelerator();
		SQAccelerator->Sweep(QueryGeom, StartTM, Dir, DeltaMag, HitBuffer, OutputFlags, QueryFlags, Filter, QueryFilterData, *QueryCallback);
		FinalizeQuery(HitBuffer);
	}
	else
#endif
	{
#if PHYSICS_INTERFACE_PHYSX
		if (SerializeSQs | ReplaySQs)
		{
			FPhysTestSerializer Serializer;
			Serializer.SetPhysicsData(*Scene.GetPxScene());
			FSQCapture& SweepCapture = Serializer.CaptureSQ();
			SweepCapture.StartCapturePhysXSweep(*Scene.GetPxScene(), QueryGeom, StartTM, Dir, DeltaMag, OutputFlags, QueryFilterData, Filter, *QueryCallback);
			Scene.GetPxScene()->sweep(QueryGeom, U2PTransform(StartTM), U2PVector(Dir), DeltaMag, HitBuffer, U2PHitFlags(OutputFlags), QueryFilterData, QueryCallback);
			SweepCapture.EndCapturePhysXSweep(HitBuffer);

			FinalizeCapture(Serializer);
		}
		else
		{
			Scene.GetPxScene()->sweep(QueryGeom, U2PTransform(StartTM), U2PVector(Dir), DeltaMag, HitBuffer, U2PHitFlags(OutputFlags), QueryFilterData, QueryCallback);
		}
#endif
	}
}

void LowLevelOverlap(FPhysScene& Scene, const PxGeometry& QueryGeom, const FTransform& GeomPose, FPhysicsHitCallback<FHitOverlap>& HitBuffer, FQueryFlags QueryFlags, const FCollisionFilterData& Filter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase* QueryCallback)
{
#if WITH_CUSTOM_SQ_STRUCTURE
	if (ForceStandardSQ == 0)
	{
		ISQAccelerator* SQAccelerator = Scene.GetSQAccelerator();
		SQAccelerator->Overlap(QueryGeom, GeomPose, HitBuffer, QueryFlags, Filter, QueryFilterData, *QueryCallback);
		FinalizeQuery(HitBuffer);
	}
	else
#endif
	{
#if PHYSICS_INTERFACE_PHYSX
		if (SerializeSQs | ReplaySQs)
		{
			FPhysTestSerializer Serializer;
			Serializer.SetPhysicsData(*Scene.GetPxScene());
			FSQCapture& SweepCapture = Serializer.CaptureSQ();
			SweepCapture.StartCapturePhysXOverlap(*Scene.GetPxScene(), QueryGeom, GeomPose, QueryFilterData, Filter, *QueryCallback);
			Scene.GetPxScene()->overlap(QueryGeom, U2PTransform(GeomPose), HitBuffer, QueryFilterData, QueryCallback);
			SweepCapture.EndCapturePhysXOverlap(HitBuffer);

			FinalizeCapture(Serializer);
		}
		else
		{
			Scene.GetPxScene()->overlap(QueryGeom, U2PTransform(GeomPose), HitBuffer, QueryFilterData, QueryCallback);
		}		
#endif
	}
}

#endif // !WITH_CHAOS_NEEDS_TO_BE_FIXED
#endif // WITH_PHYSX 