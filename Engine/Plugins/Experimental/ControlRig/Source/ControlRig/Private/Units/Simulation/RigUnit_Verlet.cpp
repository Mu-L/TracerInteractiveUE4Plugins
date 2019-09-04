// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Simulation/RigUnit_Verlet.h"
#include "Units/RigUnitContext.h"

void FRigUnit_VerletIntegrateVector::Execute(const FRigUnitContext& Context)
{
	if (Context.State == EControlRigState::Init)
	{
		Point.Mass = 1.f;
		Position = Point.Position = Target;
		Velocity = Acceleration = Point.LinearVelocity = FVector::ZeroVector;
		return;
	}

	Point.LinearDamping = Damp;
	float U = FMath::Clamp<float>(Blend * Context.DeltaTime, 0.f, 1.f);
	FVector Force = (Target - Point.Position) * FMath::Max(Strength, 0.0001f);
	FVector PreviousVelocity = Point.LinearVelocity;
	Point = Point.IntegrateVerlet(Force, Blend, Context.DeltaTime);
	Acceleration = Point.LinearVelocity - PreviousVelocity;
	Position = Point.Position;
	Velocity = Point.LinearVelocity;
}
