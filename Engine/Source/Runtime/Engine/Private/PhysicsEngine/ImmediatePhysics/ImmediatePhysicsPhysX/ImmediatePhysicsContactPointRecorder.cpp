// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/ImmediatePhysics/ImmediatePhysicsPhysX/ImmediatePhysicsContactPointRecorder_PhysX.h"

#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX

#include "Physics/ImmediatePhysics/ImmediatePhysicsPhysX/ImmediatePhysicsSimulation_PhysX.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsPhysX/ImmediatePhysicsContactPair_PhysX.h"

namespace ImmediatePhysics_PhysX
{

	EFrictionCombineMode::Type GetCombineMode(EFrictionCombineMode::Type A, EFrictionCombineMode::Type B)
	{
		return FMath::Max(A, B);
	}

	float UseCombineMode(EFrictionCombineMode::Type Mode, float A, float B)
	{
		switch (Mode)
		{
		case EFrictionCombineMode::Average: return (A * 0.5f) + (B * 0.5f);
		case EFrictionCombineMode::Multiply: return A * B;
		case EFrictionCombineMode::Min: return FMath::Min(A, B);
		case EFrictionCombineMode::Max: return FMath::Max(A, B);
		default: return 0.f;	//error
		}
	}

	bool FContactPointRecorder::recordContacts(const Gu::ContactPoint* ContactPoints, const PxU32 NumContacts, const PxU32 Index)
	{
		FContactPair ContactPair;
		ContactPair.DynamicActorDataIndex = DynamicActorDataIndex;
		ContactPair.OtherActorDataIndex = OtherActorDataIndex;
		ContactPair.StartContactIndex = Simulation.ContactPoints.Num();
		ContactPair.NumContacts = NumContacts;
		ContactPair.PairIdx = PairIdx;

		//TODO: can probably optimize this for the default material case
		EFrictionCombineMode::Type FrictionCombine = GetCombineMode(SimulatedShapeMaterial.FrictionCombineMode, OtherShapeMaterial.FrictionCombineMode);
		EFrictionCombineMode::Type RestitutionCombine = GetCombineMode(SimulatedShapeMaterial.FrictionCombineMode, OtherShapeMaterial.FrictionCombineMode);

		const float StaticFriction = UseCombineMode(FrictionCombine, SimulatedShapeMaterial.StaticFriction, OtherShapeMaterial.StaticFriction);
		const float DynamicFriction = UseCombineMode(FrictionCombine, SimulatedShapeMaterial.DynamicFriction, OtherShapeMaterial.DynamicFriction);
		const float Restitution = UseCombineMode(RestitutionCombine, SimulatedShapeMaterial.Restitution, OtherShapeMaterial.Restitution);

		for (PxU32 ContactIndex = 0; ContactIndex < NumContacts; ++ContactIndex)
		{
			//Fill in solver-specific data that our contact gen does not produce...

			Gu::ContactPoint NewPoint = ContactPoints[ContactIndex];
			NewPoint.maxImpulse = PX_MAX_F32;
			NewPoint.targetVel = PxVec3(0.f);
			NewPoint.staticFriction = StaticFriction;
			NewPoint.dynamicFriction = DynamicFriction;
			NewPoint.restitution = Restitution;
			NewPoint.materialFlags = 0;
			Simulation.ContactPoints.Add(NewPoint);
		}

		Simulation.ContactPairs.Add(ContactPair);
		return true;
	}

}

#endif // WITH_PHYSX
