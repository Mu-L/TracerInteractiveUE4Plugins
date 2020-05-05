// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimInstance.h"
#include "AnimNode_LiveLinkPose.h"

#include "LiveLinkInstance.generated.h"

class ULiveLinkRetargetAsset;

/** Proxy override for this UAnimInstance-derived class */
USTRUCT()
struct LIVELINK_API FLiveLinkInstanceProxy : public FAnimInstanceProxy
{
public:
	friend struct FAnimNode_LiveLinkPose;

	GENERATED_BODY()

		FLiveLinkInstanceProxy()
	{
	}

	FLiveLinkInstanceProxy(UAnimInstance* InAnimInstance)
		: FAnimInstanceProxy(InAnimInstance)
	{
	}

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;
	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override;

	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_LiveLinkPose PoseNode;
};

UCLASS(transient, NotBlueprintable)
class LIVELINK_API ULiveLinkInstance : public UAnimInstance
{
	GENERATED_UCLASS_BODY()

	void SetSubject(FLiveLinkSubjectName SubjectName)
	{
		GetProxyOnGameThread<FLiveLinkInstanceProxy>().PoseNode.LiveLinkSubjectName = SubjectName;
	}

	void SetRetargetAsset(TSubclassOf<ULiveLinkRetargetAsset> RetargetAsset)
	{
		GetProxyOnGameThread<FLiveLinkInstanceProxy>().PoseNode.RetargetAsset = RetargetAsset;
	}

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

	// Cache for GC
	UPROPERTY(transient)
	ULiveLinkRetargetAsset* CurrentRetargetAsset;

	friend FLiveLinkInstanceProxy;
};
