// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimNode_Root.h"

/////////////////////////////////////////////////////
// FAnimNode_Root

FAnimNode_Root::FAnimNode_Root()
{
}

void FAnimNode_Root::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_Base::Initialize_AnyThread(Context);

	Result.Initialize(Context);
}

void FAnimNode_Root::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) 
{
	Result.CacheBones(Context);
}

void FAnimNode_Root::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	TRACE_ANIM_NODE_VALUE(Context, TEXT("Name"), Name);

	GetEvaluateGraphExposedInputs().Execute(Context);
	Result.Update(Context);
}

void FAnimNode_Root::Evaluate_AnyThread(FPoseContext& Output)
{
	Result.Evaluate(Output);
}

void FAnimNode_Root::GatherDebugData(FNodeDebugData& DebugData)
{
	FString DebugLine = DebugData.GetNodeName(this);
	DebugData.AddDebugItem(DebugLine);
	Result.GatherDebugData(DebugData);
}
