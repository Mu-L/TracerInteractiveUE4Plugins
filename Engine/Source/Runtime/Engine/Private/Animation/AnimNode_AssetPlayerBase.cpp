// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimNode_AssetPlayerBase.h"
#include "Animation/AnimInstanceProxy.h"

FAnimNode_AssetPlayerBase::FAnimNode_AssetPlayerBase()
	: GroupName(NAME_None)
#if WITH_EDITORONLY_DATA
	, GroupIndex_DEPRECATED(INDEX_NONE)
#endif
	, GroupRole(EAnimGroupRole::CanBeLeader)
	, GroupScope(EAnimSyncGroupScope::Local)
	, bIgnoreForRelevancyTest(false)
	, bHasBeenFullWeight(false)
	, BlendWeight(0.0f)
	, InternalTimeAccumulator(0.0f)
{

}

void FAnimNode_AssetPlayerBase::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_Base::Initialize_AnyThread(Context);

	MarkerTickRecord.Reset();
	bHasBeenFullWeight = false;
}

void FAnimNode_AssetPlayerBase::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	// Cache the current weight and update the node
	BlendWeight = Context.GetFinalBlendWeight();
	bHasBeenFullWeight = bHasBeenFullWeight || (BlendWeight >= (1.0f - ZERO_ANIMWEIGHT_THRESH));

	UpdateAssetPlayer(Context);
}

void FAnimNode_AssetPlayerBase::CreateTickRecordForNode(const FAnimationUpdateContext& Context, UAnimSequenceBase* Sequence, bool bLooping, float PlayRate)
{
	// Create a tick record and fill it out
	const float FinalBlendWeight = Context.GetFinalBlendWeight();

	FAnimGroupInstance* SyncGroup;
	const FName GroupNameToUse = ((GroupRole < EAnimGroupRole::TransitionLeader) || bHasBeenFullWeight) ? GroupName : NAME_None;

	FAnimTickRecord& TickRecord = Context.AnimInstanceProxy->CreateUninitializedTickRecordInScope(/*out*/ SyncGroup, GroupNameToUse, GroupScope);

	Context.AnimInstanceProxy->MakeSequenceTickRecord(TickRecord, Sequence, bLooping, PlayRate, FinalBlendWeight, /*inout*/ InternalTimeAccumulator, MarkerTickRecord);
	TickRecord.RootMotionWeightModifier = Context.GetRootMotionWeightModifier();

	// Update the sync group if it exists
	if (SyncGroup != NULL)
	{
		SyncGroup->TestTickRecordForLeadership(GroupRole);
	}

	TRACE_ANIM_TICK_RECORD(Context, TickRecord);
}

float FAnimNode_AssetPlayerBase::GetCachedBlendWeight() const
{
	return BlendWeight;
}

float FAnimNode_AssetPlayerBase::GetAccumulatedTime() const
{
	return InternalTimeAccumulator;
}

void FAnimNode_AssetPlayerBase::SetAccumulatedTime(const float& NewTime)
{
	InternalTimeAccumulator = NewTime;
}

UAnimationAsset* FAnimNode_AssetPlayerBase::GetAnimAsset()
{
	return nullptr;
}

void FAnimNode_AssetPlayerBase::ClearCachedBlendWeight()
{
	BlendWeight = 0.0f;
}

