// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNodes/AnimNode_BlendSpaceEvaluator.h"
#include "Animation/BlendSpaceBase.h"
#include "Animation/AnimTrace.h"

/////////////////////////////////////////////////////
// FAnimNode_BlendSpaceEvaluator

FAnimNode_BlendSpaceEvaluator::FAnimNode_BlendSpaceEvaluator()
	: FAnimNode_BlendSpacePlayer()
	, NormalizedTime(0.f)
{
}

void FAnimNode_BlendSpaceEvaluator::UpdateAssetPlayer(const FAnimationUpdateContext& Context)
{
	GetEvaluateGraphExposedInputs().Execute(Context);
	InternalTimeAccumulator = FMath::Clamp(NormalizedTime, 0.f, 1.f);
	PlayRate = 0.f;

	UpdateInternal(Context);

	TRACE_ANIM_NODE_VALUE(Context, TEXT("Name"), BlendSpace ? *BlendSpace->GetName() : TEXT("None"));
	TRACE_ANIM_NODE_VALUE(Context, TEXT("Blend Space"), BlendSpace);
	TRACE_ANIM_NODE_VALUE(Context, TEXT("Playback Time"), InternalTimeAccumulator);
}

void FAnimNode_BlendSpaceEvaluator::GatherDebugData(FNodeDebugData& DebugData)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(GatherDebugData)
	FString DebugLine = DebugData.GetNodeName(this);

	DebugLine += FString::Printf(TEXT("('%s' Play Time: %.3f)"), *BlendSpace->GetName(), InternalTimeAccumulator);
	DebugData.AddDebugItem(DebugLine, true);
}
