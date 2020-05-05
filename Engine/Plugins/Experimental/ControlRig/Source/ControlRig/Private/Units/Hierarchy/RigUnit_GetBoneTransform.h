// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "RigUnit_GetBoneTransform.generated.h"

/**
 * GetBoneTransform is used to retrieve a single transform from a hierarchy.
 */
USTRUCT(meta=(DisplayName="Get Transform", Category="Hierarchy", DocumentationPolicy = "Strict", Keywords="GetBoneTransform"))
struct FRigUnit_GetBoneTransform : public FRigUnit
{
	GENERATED_BODY()

	FRigUnit_GetBoneTransform()
		: Space(EBoneGetterSetterMode::GlobalSpace)
		, CachedBoneIndex(INDEX_NONE)
	{}

	virtual FString GetUnitLabel() const override;

	virtual FName DetermineSpaceForPin(const FString& InPinPath, void* InUserContext) const override
	{
		if (InPinPath.StartsWith(TEXT("Transform")) && Space == EBoneGetterSetterMode::LocalSpace)
		{
			if (const FRigHierarchyContainer* Container = (const FRigHierarchyContainer*)InUserContext)
			{
				int32 BoneIndex = Container->BoneHierarchy.GetIndex(Bone);
				if (BoneIndex != INDEX_NONE)
				{
					return Container->BoneHierarchy[BoneIndex].ParentName;
				}

			}
		}
		return NAME_None;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Bone to retrieve the transform for.
	 */
	UPROPERTY(meta = (Input, CustomWidget = "BoneName", Constant))
	FName Bone;

	/**
	 * Defines if the bone's transform should be retrieved
	 * in local or global space.
	 */ 
	UPROPERTY(meta = (Input))
	EBoneGetterSetterMode Space;

	// The current transform of the given bone - or identity in case it wasn't found.
	UPROPERTY(meta=(Output))
	FTransform Transform;

	// Used to cache the internally used bone index
	UPROPERTY()
	int32 CachedBoneIndex;
};
