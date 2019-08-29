// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "AnimNodes/AnimNode_ModifyCurve.h"
#include "AnimationRuntime.h"
#include "Animation/AnimInstanceProxy.h"

FAnimNode_ModifyCurve::FAnimNode_ModifyCurve()
{
	ApplyMode = EModifyCurveApplyMode::Blend;
	Alpha = 1.f;
}

void FAnimNode_ModifyCurve::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	Super::Initialize_AnyThread(Context);
	SourcePose.Initialize(Context);

	// Init our last values array to be the right size
	if (ApplyMode == EModifyCurveApplyMode::WeightedMovingAverage)
	{
		LastCurveValues.Reset(CurveValues.Num());
		LastCurveValues.AddZeroed(CurveValues.Num());
	}
}

void FAnimNode_ModifyCurve::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Super::CacheBones_AnyThread(Context);
	SourcePose.CacheBones(Context);
}

void FAnimNode_ModifyCurve::Evaluate_AnyThread(FPoseContext& Output)
{
	FPoseContext SourceData(Output);
	SourcePose.Evaluate(SourceData);

	Output = SourceData;

	//	Morph target and Material parameter curves
	USkeleton* Skeleton = Output.AnimInstanceProxy->GetSkeleton();

	check(CurveNames.Num() == CurveValues.Num());

	for (int32 ModIdx = 0; ModIdx < CurveNames.Num(); ModIdx++)
	{
		FName CurveName = CurveNames[ModIdx];
		SmartName::UID_Type NameUID = Skeleton->GetUIDByName(USkeleton::AnimCurveMappingName, CurveName);
		if (NameUID != SmartName::MaxUID)
		{
			float CurveValue = CurveValues[ModIdx];
			float CurrentValue = Output.Curve.Get(NameUID);

			// Use ApplyMode enum to decide how to apply
			if (ApplyMode == EModifyCurveApplyMode::Add)
			{
				Output.Curve.Set(NameUID, CurrentValue + CurveValue);
			}
			else if (ApplyMode == EModifyCurveApplyMode::Scale)
			{
				Output.Curve.Set(NameUID, CurrentValue * CurveValue);
			}
			else if (ApplyMode == EModifyCurveApplyMode::WeightedMovingAverage)
			{
				check(LastCurveValues.Num() == CurveValues.Num());

				const float LastCurveValue = LastCurveValues[ModIdx];
				const float WAvg = (CurrentValue * Alpha) + (LastCurveValue * (1.f - Alpha));
				// Update the last curve value for next run
				LastCurveValues[ModIdx] = WAvg;

				Output.Curve.Set(NameUID, WAvg);
			}
			else if (ApplyMode == EModifyCurveApplyMode::RemapCurve)
			{
				const float RemapScale = 1.f / FMath::Max(1.f - CurveValue, 0.01f);
				const float RemappedValue = FMath::Min(FMath::Max(CurrentValue - CurveValue, 0.f) * RemapScale, 1.f);
				Output.Curve.Set(NameUID, RemappedValue);
			}
			else // Blend
			{
				float UseAlpha = FMath::Clamp(Alpha, 0.f, 1.f);
				Output.Curve.Set(NameUID, FMath::Lerp(CurrentValue, CurveValue, UseAlpha));
			}
		}
	}
}

void FAnimNode_ModifyCurve::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	// Run update on input pose nodes
	SourcePose.Update(Context);

	// Evaluate any BP logic plugged into this node
	EvaluateGraphExposedInputs.Execute(Context);
}

#if WITH_EDITOR

void FAnimNode_ModifyCurve::AddCurve(const FName& InName, float InValue)
{
	CurveValues.Add(InValue);
	CurveNames.Add(InName);
}

void FAnimNode_ModifyCurve::RemoveCurve(int32 PoseIndex)
{
	CurveValues.RemoveAt(PoseIndex);
	CurveNames.RemoveAt(PoseIndex);
}

#endif // WITH_EDITOR
