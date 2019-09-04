// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "BoneIndices.h"

/** Transient structure for CCDIK node evaluation */
struct CCDIKChainLink
{
public:
	/** Transform of bone in component space. */
	FTransform Transform;

	/** Transform of bone in local space. This is mutable as their component space changes or parents*/
	FTransform LocalTransform;

	/** Transform Index that this control will output */
	int32 TransformIndex;

	/** Child bones which are overlapping this bone. 
	 * They have a zero length distance, so they will inherit this bone's transformation. */
	TArray<int32> ChildZeroLengthTransformIndices;

	float CurrentAngleDelta;

	CCDIKChainLink()
		: TransformIndex(INDEX_NONE)
		, CurrentAngleDelta(0.f)
	{
	}

	CCDIKChainLink(const FTransform& InTransform, const FTransform& InLocalTransform, const int32& InTransformIndex)
		: Transform(InTransform)
		, LocalTransform(InLocalTransform)
		, TransformIndex(InTransformIndex)
		, CurrentAngleDelta(0.f)
	{
	}
};

namespace AnimationCore
{
	ANIMATIONCORE_API bool SolveCCDIK(TArray<CCDIKChainLink>& InOutChain, const FVector& TargetPosition, float Precision, int32 MaxIteration, bool bStartFromTail, bool bEnableRotationLimit, const TArray<float>& RotationLimitPerJoints);
};
