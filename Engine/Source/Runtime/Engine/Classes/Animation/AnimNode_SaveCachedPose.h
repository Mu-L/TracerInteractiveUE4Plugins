// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimTypes.h"
#include "Animation/AnimCurveTypes.h"
#include "BonePose.h"
#include "Animation/AnimNodeBase.h"
#include "AnimNode_SaveCachedPose.generated.h"

USTRUCT(BlueprintInternalUseOnly)
struct ENGINE_API FAnimNode_SaveCachedPose : public FAnimNode_Base
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Links)
	FPoseLink Pose;

	/** Intentionally not exposed, set by AnimBlueprintCompiler */
	UPROPERTY()
	FName CachePoseName;

	float GlobalWeight;

protected:
	FCompactPose CachedPose;
	FBlendedCurve CachedCurve;

	TArray<FAnimationUpdateContext> CachedUpdateContexts;

	FGraphTraversalCounter InitializationCounter;
	FGraphTraversalCounter CachedBonesCounter;
	FGraphTraversalCounter UpdateCounter;
	FGraphTraversalCounter EvaluationCounter;

public:	
	FAnimNode_SaveCachedPose();

	// FAnimNode_Base interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	// End of FAnimNode_Base interface

	void PostGraphUpdate();
};
