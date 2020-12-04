// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNodes/AnimNode_SequenceEvaluator.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimTrace.h"

float FAnimNode_SequenceEvaluator::GetCurrentAssetTime()
{
	return ExplicitTime;
}

float FAnimNode_SequenceEvaluator::GetCurrentAssetLength()
{
	return Sequence ? Sequence->SequenceLength : 0.0f;
}

/////////////////////////////////////////////////////
// FAnimSequenceEvaluatorNode

void FAnimNode_SequenceEvaluator::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread)
	FAnimNode_AssetPlayerBase::Initialize_AnyThread(Context);
	bReinitialized = true;
}

void FAnimNode_SequenceEvaluator::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(CacheBones_AnyThread)
}

void FAnimNode_SequenceEvaluator::UpdateAssetPlayer(const FAnimationUpdateContext& Context)
{
	GetEvaluateGraphExposedInputs().Execute(Context);

	if (Sequence)
	{
		// Clamp input to a valid position on this sequence's time line.
		ExplicitTime = FMath::Clamp(ExplicitTime, 0.f, Sequence->SequenceLength);

		if ((!bTeleportToExplicitTime || (GroupName != NAME_None)) && (Context.AnimInstanceProxy->IsSkeletonCompatible(Sequence->GetSkeleton())))
		{
			if (bReinitialized)
			{
				switch (ReinitializationBehavior)
				{
					case ESequenceEvalReinit::StartPosition: InternalTimeAccumulator = StartPosition; break;
					case ESequenceEvalReinit::ExplicitTime: InternalTimeAccumulator = ExplicitTime; break;
				}

				InternalTimeAccumulator = FMath::Clamp(InternalTimeAccumulator, 0.f, Sequence->SequenceLength);
			}

			float TimeJump = ExplicitTime - InternalTimeAccumulator;
			if (bShouldLoop)
			{
				if (FMath::Abs(TimeJump) > (Sequence->SequenceLength * 0.5f))
				{
					if (TimeJump > 0.f)
					{
						TimeJump -= Sequence->SequenceLength;
					}
					else
					{
						TimeJump += Sequence->SequenceLength;
					}
				}
			}

			// if you jump from front to end or end to front, your time jump is 0.f, so nothing moves
			// to prevent that from happening, we set current accumulator to explicit time
			if (TimeJump == 0.f)
			{
				InternalTimeAccumulator = ExplicitTime;
			}
			
			const float DeltaTime = Context.GetDeltaTime();
			const float RateScale = Sequence->RateScale;
			const float PlayRate = FMath::IsNearlyZero(DeltaTime) || FMath::IsNearlyZero(RateScale) ? 0.f : (TimeJump / (DeltaTime * RateScale));
			CreateTickRecordForNode(Context, Sequence, bShouldLoop, PlayRate);
		}
		else
		{
			InternalTimeAccumulator = ExplicitTime;
		}
	}

	bReinitialized = false;

	TRACE_ANIM_NODE_VALUE(Context, TEXT("Name"), Sequence != nullptr ? Sequence->GetFName() : NAME_None);
	TRACE_ANIM_NODE_VALUE(Context, TEXT("Sequence"), Sequence);
	TRACE_ANIM_NODE_VALUE(Context, TEXT("InputTime"), ExplicitTime);
	TRACE_ANIM_NODE_VALUE(Context, TEXT("Time"), InternalTimeAccumulator);
}

void FAnimNode_SequenceEvaluator::Evaluate_AnyThread(FPoseContext& Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Evaluate_AnyThread)
	check(Output.AnimInstanceProxy != nullptr);
	if ((Sequence != nullptr) && (Output.AnimInstanceProxy->IsSkeletonCompatible(Sequence->GetSkeleton())))
	{
		FAnimationPoseData AnimationPoseData(Output);
		Sequence->GetAnimationPose(AnimationPoseData, FAnimExtractContext(InternalTimeAccumulator, Output.AnimInstanceProxy->ShouldExtractRootMotion()));
	}
	else
	{
		Output.ResetToRefPose();
	}
}

void FAnimNode_SequenceEvaluator::OverrideAsset(UAnimationAsset* NewAsset)
{
	if(UAnimSequenceBase* NewSequence = Cast<UAnimSequenceBase>(NewAsset))
	{
		Sequence = NewSequence;
	}
}

void FAnimNode_SequenceEvaluator::GatherDebugData(FNodeDebugData& DebugData)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(GatherDebugData)
	FString DebugLine = DebugData.GetNodeName(this);
	
	DebugLine += FString::Printf(TEXT("('%s' InputTime: %.3f, Time: %.3f)"), *GetNameSafe(Sequence), ExplicitTime, InternalTimeAccumulator);
	DebugData.AddDebugItem(DebugLine, true);
}
