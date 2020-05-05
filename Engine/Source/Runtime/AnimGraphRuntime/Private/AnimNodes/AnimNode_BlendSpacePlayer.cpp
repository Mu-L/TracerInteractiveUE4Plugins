// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNodes/AnimNode_BlendSpacePlayer.h"
#include "Animation/BlendSpaceBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimInstanceProxy.h"
#include "AnimGraphRuntimeTrace.h"

/////////////////////////////////////////////////////
// FAnimNode_BlendSpacePlayer

FAnimNode_BlendSpacePlayer::FAnimNode_BlendSpacePlayer()
	: X(0.0f)
	, Y(0.0f)
	, Z(0.0f)
	, PlayRate(1.0f)
	, bLoop(true)
	, bResetPlayTimeWhenBlendSpaceChanges(true)
	, StartPosition(0.f)
	, BlendSpace(nullptr)
	, PreviousBlendSpace(nullptr)
{
}

float FAnimNode_BlendSpacePlayer::GetCurrentAssetTime()
{
	if(const FBlendSampleData* HighestWeightedSample = GetHighestWeightedSample())
	{
		return HighestWeightedSample->Time;
	}

	// No sample
	return 0.0f;
}

float FAnimNode_BlendSpacePlayer::GetCurrentAssetTimePlayRateAdjusted()
{
	float Length = GetCurrentAssetLength();
	return PlayRate < 0.0f ? Length - InternalTimeAccumulator * Length : Length * InternalTimeAccumulator;
}

float FAnimNode_BlendSpacePlayer::GetCurrentAssetLength()
{
	if(const FBlendSampleData* HighestWeightedSample = GetHighestWeightedSample())
	{
		if (BlendSpace != nullptr)
		{
			const FBlendSample& Sample = BlendSpace->GetBlendSample(HighestWeightedSample->SampleDataIndex);
			return Sample.Animation->SequenceLength;
		}
	}

	// No sample
	return 0.0f;
}

void FAnimNode_BlendSpacePlayer::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread)
	FAnimNode_AssetPlayerBase::Initialize_AnyThread(Context);

	GetEvaluateGraphExposedInputs().Execute(Context);

	Reinitialize();

	PreviousBlendSpace = BlendSpace;
}

void FAnimNode_BlendSpacePlayer::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(CacheBones_AnyThread)
}

void FAnimNode_BlendSpacePlayer::UpdateAssetPlayer(const FAnimationUpdateContext& Context)
{
	GetEvaluateGraphExposedInputs().Execute(Context);

	UpdateInternal(Context);
}

void FAnimNode_BlendSpacePlayer::UpdateInternal(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(UpdateInternal)
	if ((BlendSpace != NULL) && (Context.AnimInstanceProxy->IsSkeletonCompatible(BlendSpace->GetSkeleton())))
	{
		// Create a tick record and fill it out
		FAnimGroupInstance* SyncGroup;
		FAnimTickRecord& TickRecord = Context.AnimInstanceProxy->CreateUninitializedTickRecord(GroupIndex, /*out*/ SyncGroup);

		const FVector BlendInput(X, Y, Z);
	
		if (PreviousBlendSpace != BlendSpace)
		{
			Reinitialize(bResetPlayTimeWhenBlendSpaceChanges);
		}

		Context.AnimInstanceProxy->MakeBlendSpaceTickRecord(TickRecord, BlendSpace, BlendInput, BlendSampleDataCache, BlendFilter, bLoop, PlayRate, Context.GetFinalBlendWeight(), /*inout*/ InternalTimeAccumulator, MarkerTickRecord);

		// Update the sync group if it exists
		if (SyncGroup != NULL)
		{
			SyncGroup->TestTickRecordForLeadership(GroupRole);
		}


		TRACE_ANIM_TICK_RECORD(Context, TickRecord);

#if ANIM_NODE_IDS_AVAILABLE && WITH_EDITORONLY_DATA
		if (FAnimBlueprintDebugData* DebugData = Context.AnimInstanceProxy->GetAnimBlueprintDebugData())
		{
			DebugData->RecordBlendSpacePlayer(Context.GetCurrentNodeId(), BlendSpace, BlendInput.X, BlendInput.Y, BlendInput.Z);
		}
#endif

		PreviousBlendSpace = BlendSpace;
	}

	TRACE_BLENDSPACE_PLAYER(Context, *this);
	TRACE_ANIM_NODE_VALUE(Context, TEXT("Name"), BlendSpace ? *BlendSpace->GetName() : TEXT("None"));
	TRACE_ANIM_NODE_VALUE(Context, TEXT("Blend Space"), BlendSpace);
	TRACE_ANIM_NODE_VALUE(Context, TEXT("Playback Time"), InternalTimeAccumulator);
}

void FAnimNode_BlendSpacePlayer::Evaluate_AnyThread(FPoseContext& Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Evaluate_AnyThread)
	if ((BlendSpace != NULL) && (Output.AnimInstanceProxy->IsSkeletonCompatible(BlendSpace->GetSkeleton())))
	{
		BlendSpace->GetAnimationPose(BlendSampleDataCache, Output.Pose, Output.Curve);
	}
	else
	{
		Output.ResetToRefPose();
	}
}

void FAnimNode_BlendSpacePlayer::OverrideAsset(UAnimationAsset* NewAsset)
{
	if(UBlendSpaceBase* NewBlendSpace = Cast<UBlendSpaceBase>(NewAsset))
	{
		BlendSpace = NewBlendSpace;
	}
}

void FAnimNode_BlendSpacePlayer::GatherDebugData(FNodeDebugData& DebugData)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(GatherDebugData)
	FString DebugLine = DebugData.GetNodeName(this);
	if (BlendSpace)
	{
		DebugLine += FString::Printf(TEXT("('%s' Play Time: %.3f)"), *BlendSpace->GetName(), InternalTimeAccumulator);

		DebugData.AddDebugItem(DebugLine, true);

		//BlendSpace->GetBlendSamples();
	}
}

float FAnimNode_BlendSpacePlayer::GetTimeFromEnd(float CurrentTime)
{
	return BlendSpace != nullptr ? BlendSpace->GetMaxCurrentTime() - CurrentTime : 0.0f;
}

UAnimationAsset* FAnimNode_BlendSpacePlayer::GetAnimAsset()
{
	return BlendSpace;
}

const FBlendSampleData* FAnimNode_BlendSpacePlayer::GetHighestWeightedSample() const
{
	if(BlendSampleDataCache.Num() == 0)
	{
		return nullptr;
	}

	const FBlendSampleData* HighestSample = &BlendSampleDataCache[0];

	for(int32 Idx = 1; Idx < BlendSampleDataCache.Num(); ++Idx)
	{
		if(BlendSampleDataCache[Idx].TotalWeight > HighestSample->TotalWeight)
		{
			HighestSample = &BlendSampleDataCache[Idx];
		}
	}

	return HighestSample;
}

void FAnimNode_BlendSpacePlayer::Reinitialize(bool bResetTime)
{
	BlendSampleDataCache.Empty();
	if(bResetTime)
	{
		InternalTimeAccumulator = FMath::Clamp(StartPosition, 0.f, 1.0f);
		if (StartPosition == 0.f && PlayRate < 0.0f)
		{
			// Blend spaces run between 0 and 1
			InternalTimeAccumulator = 1.0f;
		}
	}

	if (BlendSpace != NULL)
	{
		BlendSpace->InitializeFilter(&BlendFilter);
	}
}
