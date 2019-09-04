// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimNode_AssetPlayerBase.h"
#include "Animation/InputScaleBias.h"
#include "Animation/AnimSequenceDecompressionContext.h"
#include "Animation/AnimSequenceBase.h"
#include "AnimNode_SequencePlayer.generated.h"


// Sequence player node
USTRUCT(BlueprintInternalUseOnly)
struct ENGINE_API FAnimNode_SequencePlayer : public FAnimNode_AssetPlayerBase
{
	GENERATED_USTRUCT_BODY()
public:
	// The animation sequence asset to play
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PinHiddenByDefault, DisallowedClasses="AnimMontage"))
	UAnimSequenceBase* Sequence;

	// The Basis in which the PlayRate is expressed in. This is used to rescale PlayRate inputs.
	// For example a Basis of 100 means that the PlayRate input will be divided by 100.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PinHiddenByDefault))
	float PlayRateBasis;

	// The play rate multiplier. Can be negative, which will cause the animation to play in reverse.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PinHiddenByDefault))
	float PlayRate;
	
	// Additional scaling, offsetting and clamping of PlayRate input.
	// Performed after PlayRateBasis.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FInputScaleBiasClamp PlayRateScaleBiasClamp;

	// The start up position, it only applies when reinitialized
	// if you loop, it will still start from 0.f after finishing the round
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PinHiddenByDefault))
	float StartPosition;

	// Should the animation continue looping when it reaches the end?
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PinHiddenByDefault))
	bool bLoopAnimation;

public:
	FAnimNode_SequencePlayer()
		: Sequence(nullptr)
		, PlayRateBasis(1.0f)
		, PlayRate(1.0f)
		, StartPosition(0.f)
		, bLoopAnimation(true)
	{
	}

	// FAnimNode_AssetPlayerBase interface
	virtual float GetCurrentAssetTime() override;
	virtual float GetCurrentAssetTimePlayRateAdjusted() override;
	virtual float GetCurrentAssetLength() override;
	virtual UAnimationAsset* GetAnimAsset() override { return Sequence; }
	// End of FAnimNode_AssetPlayerBase interface

	// FAnimNode_Base interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void UpdateAssetPlayer(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void OverrideAsset(UAnimationAsset* NewAsset) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	// End of FAnimNode_Base interface

	float GetTimeFromEnd(float CurrentNodeTime);
};
