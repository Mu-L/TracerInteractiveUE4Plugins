// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Debug/RigUnit_DebugBase.h"
#include "RigUnit_DebugPrimitives.generated.h"

USTRUCT(meta=(DisplayName="Draw Rectangle", Keywords="Draw Square"))
struct FRigUnit_DebugRectangle : public FRigUnit_DebugBaseMutable
{
	GENERATED_BODY()

	FRigUnit_DebugRectangle()
	{
		Transform = WorldOffset = FTransform::Identity;
		Color = FLinearColor::Red;
		Thickness = 0.f;
		Scale = 10.f;
		WorldOffset = FTransform::Identity;
		bEnabled = true;
	}

	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FTransform Transform;

	UPROPERTY(meta = (Input))
	FLinearColor Color;

	UPROPERTY(meta = (Input))
	float Scale;

	UPROPERTY(meta = (Input))
	float Thickness;

	UPROPERTY(meta = (Input, Constant, BoneName))
	FName Space;

	UPROPERTY(meta = (Input))
	FTransform WorldOffset;

	UPROPERTY(meta = (Input, Constant, BoneName))
	bool bEnabled;
};

USTRUCT(meta=(DisplayName="Draw Arc", Keywords="Draw Ellipse, Draw Circle"))
struct FRigUnit_DebugArc : public FRigUnit_DebugBaseMutable
{
	GENERATED_BODY()

	FRigUnit_DebugArc()
	{
		Transform = WorldOffset = FTransform::Identity;
		Color = FLinearColor::Red;
		Thickness = MinimumDegrees = 0.f;
		Radius = 10.f;
		MaximumDegrees = 360.f;
		Detail = 16;
		WorldOffset = FTransform::Identity;
		bEnabled = true;
	}

	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FTransform Transform;

	UPROPERTY(meta = (Input))
	FLinearColor Color;

	UPROPERTY(meta = (Input))
	float Radius;

	UPROPERTY(meta = (Input))
	float MinimumDegrees;

	UPROPERTY(meta = (Input))
	float MaximumDegrees;

	UPROPERTY(meta = (Input))
	float Thickness;

	UPROPERTY(meta = (Input))
	int32 Detail;

	UPROPERTY(meta = (Input, Constant, BoneName))
	FName Space;

	UPROPERTY(meta = (Input))
	FTransform WorldOffset;

	UPROPERTY(meta = (Input, Constant, BoneName))
	bool bEnabled;
};