// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimationSettings.h"
#include "Animation/AnimCompress_BitwiseCompressOnly.h"

UAnimationSettings::UAnimationSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, CompressCommandletVersion(2)	// Bump this up to trigger full recompression. Otherwise only new animations imported will be recompressed.
	, ForceRecompression(false)
	, bForceBelowThreshold(false)
	, bFirstRecompressUsingCurrentOrDefault(true)
	, bRaiseMaxErrorToExisting(false)
	, bEnablePerformanceLog(false)
	, bTickAnimationOnSkeletalMeshInit(true)
	, DefaultAttributeBlendMode(ECustomAttributeBlendType::Override)
{
	SectionName = TEXT("Animation");

	KeyEndEffectorsMatchNameArray.Add(TEXT("IK"));
	KeyEndEffectorsMatchNameArray.Add(TEXT("eye"));
	KeyEndEffectorsMatchNameArray.Add(TEXT("weapon"));
	KeyEndEffectorsMatchNameArray.Add(TEXT("hand"));
	KeyEndEffectorsMatchNameArray.Add(TEXT("attach"));
	KeyEndEffectorsMatchNameArray.Add(TEXT("camera"));
}


#if WITH_EDITOR
void UAnimationSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

// 	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
// 	if (PropertyName == GET_MEMBER_NAME_CHECKED(UAnimationSettings, FrictionCombineMode))
// 	{
// 		UPhysicalMaterial::RebuildPhysicalMaterials();
// 	}
// 	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UAnimationSettings, LockedAxis))
// 	{
// 		UMovementComponent::PhysicsLockedAxisSettingChanged();
// 	}
}


#endif	// WITH_EDITOR
