// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Debug/RigUnit_DebugPrimitives.h"
#include "Units/RigUnitContext.h"

void FRigUnit_DebugRectangle::Execute(const FRigUnitContext& Context)
{
	if (Context.State == EControlRigState::Init)
	{
		return;
	}

	if (Context.DrawInterface == nullptr || !bEnabled)
	{
		return;
	}

	FTransform DrawTransform = Transform;
	if (Space != NAME_None && Context.HierarchyReference.Get() != nullptr)
	{
		DrawTransform = Context.HierarchyReference.Get()->GetGlobalTransform(Space) * DrawTransform;
	}

	Context.DrawInterface->DrawRectangle(WorldOffset, DrawTransform, Scale, Color, Thickness);
}

void FRigUnit_DebugArc::Execute(const FRigUnitContext& Context)
{
	if (Context.State == EControlRigState::Init)
	{
		return;
	}

	if (Context.DrawInterface == nullptr || !bEnabled)
	{
		return;
	}

	FTransform DrawTransform = Transform;
	if (Space != NAME_None && Context.HierarchyReference.Get() != nullptr)
	{
		DrawTransform = Context.HierarchyReference.Get()->GetGlobalTransform(Space) * DrawTransform;
	}

	Context.DrawInterface->DrawArc(WorldOffset, DrawTransform, Radius, FMath::DegreesToRadians(MinimumDegrees), FMath::DegreesToRadians(MaximumDegrees), Color, Thickness, Detail);
}