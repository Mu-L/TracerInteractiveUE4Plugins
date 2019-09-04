// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "Hierarchy.h"
#include "ControlRigDefines.h"
#include "RigUnit_TwoBoneIKFK.generated.h"

/** 
 * Spec define: https://wiki.it.epicgames.net/display/TeamOnline/FKIK
 */

USTRUCT(meta=(DisplayName="TwoBoneIK/FK", Category="Transforms", Deprecated = "4.23.0"))
struct CONTROLRIG_API FRigUnit_TwoBoneIKFK : public FRigUnitMutable
{
	GENERATED_BODY()

	virtual void Execute(const FRigUnitContext& Context) override;

	FRigUnit_TwoBoneIKFK()
		: PoleTarget(ForceInitToZero)
		, Spin(0.f)
		, IKBlend(1.f)
		, PreviousFKIKBlend(1.f)
		, StartJointIndex(INDEX_NONE)
		, MidJointIndex(INDEX_NONE)
		, EndJointIndex(INDEX_NONE)
		, UpperLimbLength(0.f)
		, LowerLimbLength(0.f)
	{}

	UPROPERTY(EditAnywhere, Category = "TwoBoneIKFK", meta = (Input))
	FName StartJoint;

	UPROPERTY(EditAnywhere, Category = "TwoBoneIKFK", meta = (Input))
	FName EndJoint;

	// # Transform to use as the pole target(specifies the plane of solution)
	UPROPERTY(EditAnywhere, Category = "TwoBoneIKFK", meta = (Input))
	FVector PoleTarget;

	// Float: Spin(Default : 0.0) # Amount of twist to apply to the solution plane(Additive after application of Pole Target motion)
	UPROPERTY(EditAnywhere, Category = "TwoBoneIKFK", meta = (Input))
	float Spin;

	//# Transform to use as the end effector of the IK system
	UPROPERTY(EditAnywhere, Category = "TwoBoneIKFK", meta = (Input, AllowSourceAccess))
	FTransform EndEffector;

	//Float : IKBlend(Default : 0.0)             # Blend between 0.0 (FK) and 1.0 (IK)solutions
	UPROPERTY(EditAnywhere, Category = "TwoBoneIKFK", meta = (Input, ClampMin = "0", AllowSourceAccess))
	float IKBlend; 

	// Transform : Start Joint FK Transform         # Transform for the Start Joint when in FK mode
	// Transform: Mid Joint FK Transform           # Transform for the Mid Joint when in FK mode
	// Transform : End Joint FK Transform          # Transform for the End Joint when in FK mode
	UPROPERTY(EditAnywhere, Category = "TwoBoneIKFK", meta = (Input, AllowSourceAccess))
	FTransform StartJointFKTransform;

	UPROPERTY(EditAnywhere, Category = "TwoBoneIKFK", meta = (Input, AllowSourceAccess))
	FTransform MidJointFKTransform;

	UPROPERTY(EditAnywhere, Category = "TwoBoneIKFK", meta = (Input, AllowSourceAccess))
	FTransform EndJointFKTransform;

private:
	float PreviousFKIKBlend;

	FTransform StartJointIKTransform;
	FTransform MidJointIKTransform;
	FTransform EndJointIKTransform;

	int32 StartJointIndex;
	int32 MidJointIndex;
	int32 EndJointIndex;

	float UpperLimbLength;
	float LowerLimbLength;

	void SolveIK();

	friend class URigUnitEditor_TwoBoneIKFK;
};
