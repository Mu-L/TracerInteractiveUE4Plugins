// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Highlevel/RigUnit_HighlevelBase.h"
#include "FABRIK.h"
#include "RigUnit_FABRIK.generated.h"

/**
 * The FABRIK solver can solve N-Bone chains using 
 * the Forward and Backward Reaching Inverse Kinematics algorithm.
 * For now this node supports single effector chains only.
 */
USTRUCT(meta=(DisplayName="Basic FABRIK", Category="Hierarchy", Keywords="N-Bone,IK"))
struct FRigUnit_FABRIK : public FRigUnit_HighlevelBaseMutable
{
	GENERATED_BODY()

	virtual void Execute(const FRigUnitContext& Context) override;

	FRigUnit_FABRIK()
	{
		EffectorIndex = INDEX_NONE;
		Precision = 1.f;
		MaxIterations = 10;
		EffectorTransform = FTransform::Identity;
		bPropagateToChildren = false;
	}

	/**
	 * The first bone in the chain to solve
	 */
	UPROPERTY(meta = (Input, Constant, BoneName))
	FName StartBone;

	/**
	 * The last bone in the chain to solve - the effector
	 */
	UPROPERTY(meta = (Input, Constant, BoneName))
	FName EffectorBone;

	/**
	 * The transform of the effector in global space
	 */
	UPROPERTY(meta = (Input))
	FTransform EffectorTransform;

	/**
	 * The precision to use for the fabrik solver
	 */
	UPROPERTY(meta = (Input, Constant))
	float Precision;

	/**
	 * If set to true all of the global transforms of the children
	 * of this bone will be recalculated based on their local transforms.
	 * Note: This is computationally more expensive than turning it off.
	 */
	UPROPERTY(meta = (Input))
	bool bPropagateToChildren;

	/**
	 * The maximum number of iterations. Values between 4 and 16 are common.
	 */
	UPROPERTY(meta = (Input))
	int32 MaxIterations;

	TArray<FABRIKChainLink> Chain;
	TArray<int32> BoneIndices;
	int32 EffectorIndex;
};
