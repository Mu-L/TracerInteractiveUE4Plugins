// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sequencer/ControlRigSequencerAnimInstance.h"
#include "ControlRigSequencerAnimInstanceProxy.h"
#include "ControlRig.h"

UControlRigSequencerAnimInstance::UControlRigSequencerAnimInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bUseMultiThreadedAnimationUpdate = false;
}

FAnimInstanceProxy* UControlRigSequencerAnimInstance::CreateAnimInstanceProxy()
{
	return new FControlRigSequencerAnimInstanceProxy(this);
}

bool UControlRigSequencerAnimInstance::UpdateControlRig(UControlRig* InControlRig, uint32 SequenceId, bool bAdditive, bool bApplyBoneFilter, const FInputBlendPose& BoneFilter, float Weight, const FControlRigIOSettings& InputSettings, bool bExecute)
{
	CachedControlRig = InControlRig;
	return GetProxyOnGameThread<FControlRigSequencerAnimInstanceProxy>().UpdateControlRig(InControlRig, SequenceId, bAdditive, bApplyBoneFilter, BoneFilter, Weight, InputSettings, bExecute);
}

bool UControlRigSequencerAnimInstance::SetAnimationAsset(class UAnimationAsset* NewAsset)
{
	return GetProxyOnGameThread<FControlRigSequencerAnimInstanceProxy>().SetAnimationAsset(NewAsset);
}