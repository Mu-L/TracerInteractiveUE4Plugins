// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "LiveLinkRetargetAsset.generated.h"

class USkeleton;
struct FLiveLinkAnimationFrameData;
struct FLiveLinkBaseFrameData;
struct FLiveLinkBaseStaticData;
struct FLiveLinkSkeletonStaticData;
struct FCompactPose;
struct FBlendedCurve;

// Base class for retargeting live link data. 
UCLASS(Abstract)
class LIVELINK_API ULiveLinkRetargetAsset : public UObject
{
	GENERATED_UCLASS_BODY()

	// Takes the supplied curve name and value and applies it to the blended curve (as approriate given the supplied skeleton
	void ApplyCurveValue(const USkeleton* Skeleton, const FName CurveName, const float CurveValue, FBlendedCurve& OutCurve) const;

	// Builds curve data into OutCurve from the supplied live link frame
	void BuildCurveData(const FLiveLinkSkeletonStaticData* InSkeletonData, const FLiveLinkAnimationFrameData* InFrameData, const FCompactPose& InPose, FBlendedCurve& OutCurve) const;

	// Builds curve data into OutCurve from the supplied map of curve name to float
	void BuildCurveData(const TMap<FName, float>& CurveMap, const FCompactPose& InPose, FBlendedCurve& OutCurve) const;

	// Called once when the retargeter is created 
	virtual void Initialize() {}

	// Build OutPose and OutCurve from the supplied InFrame.
	UE_DEPRECATED(4.23, "ULiveLinkRetargetAsset::BuildPoseForSubject is deprecated. Please use ULiveLinkRetargetAsset::BuildPoseFromAnimationData and ULiveLinkRetargetAsset::BuildPoseAndCurveFromBaseData instead.")
	virtual void BuildPoseForSubject(float DeltaTime, const FLiveLinkSkeletonStaticData* InSkeletonData, const FLiveLinkAnimationFrameData* InFrameData, FCompactPose& OutPose, FBlendedCurve& OutCurve) PURE_VIRTUAL(ULiveLinkRetargetAsset::BuildPoseForSubject, );
	
	// Build OutPose from AnimationData if subject was from this type
	virtual void BuildPoseFromAnimationData(float DeltaTime, const FLiveLinkSkeletonStaticData* InSkeletonData, const FLiveLinkAnimationFrameData* InFrameData, FCompactPose& OutPose) {}

	// Build OutPose and OutCurve from the basic data. Called for every type of subjects
	virtual void BuildPoseAndCurveFromBaseData(float DeltaTime, const FLiveLinkBaseStaticData* InBaseStaticData, const FLiveLinkBaseFrameData* InBaseFrameData, FCompactPose& OutPose, FBlendedCurve& OutCurve) {}
};
