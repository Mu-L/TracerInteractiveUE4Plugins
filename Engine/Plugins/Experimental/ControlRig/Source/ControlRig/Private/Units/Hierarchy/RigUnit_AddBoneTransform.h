// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "RigUnit_AddBoneTransform.generated.h"


/**
 * AddBoneTransform is used to perform a change in the hierarchy by setting a single bone's transform.
 */
USTRUCT(meta=(DisplayName="Offset Transform", Category="Hierarchy", DocumentationPolicy="Strict", Keywords="Offset,AddToBoneTransform"))
struct FRigUnit_AddBoneTransform : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_AddBoneTransform()
		: bPostMultiply(false)
		, bPropagateToChildren(false)
		, CachedBoneIndex(INDEX_NONE)
	{}

	virtual FString GetUnitLabel() const override;
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Bone to set the transform for.
	 */
	UPROPERTY(meta = (Input, BoneName, Constant))
	FName Bone;

	/**
	 * The transform value to set for the given Bone.
	 */
	UPROPERTY(meta = (Input))
	FTransform Transform;

	/**
	 * If set to true the transform will be post multiplied, otherwise pre multiplied.
	 * Post multiplying means that the transform is understood as a parent space change,
	 * while pre multiplying means that the transform is understood as a child space change.
	 */
	UPROPERTY(meta = (Input))
	bool bPostMultiply;

	/**
	 * If set to true all of the global transforms of the children 
	 * of this bone will be recalculated based on their local transforms.
	 * Note: This is computationally more expensive than turning it off.
	 */
	UPROPERTY(meta = (Input))
	bool bPropagateToChildren;

	// Used to cache the internally used bone index
	UPROPERTY()
	int32 CachedBoneIndex;
};
