// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

//~=============================================================================
// BlendSpaceFactory1D
//~=============================================================================

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Factories/Factory.h"
#include "BlendSpaceFactory1D.generated.h"

struct FAssetData;
class SWindow;

UCLASS(hidecategories=Object, MinimalAPI)
class UBlendSpaceFactory1D : public UFactory
{
	GENERATED_UCLASS_BODY()

	UPROPERTY()
	class USkeleton*	TargetSkeleton;

	/** The preview mesh to use with this animation */
	UPROPERTY()
	class USkeletalMesh* PreviewSkeletalMesh;

	//~ Begin UFactory Interface
	virtual bool ConfigureProperties() override;
	virtual UObject* FactoryCreateNew(UClass* Class,UObject* InParent,FName Name,EObjectFlags Flags,UObject* Context,FFeedbackContext* Warn) override;
	//~ Begin UFactory Interface

private:
	void OnTargetSkeletonSelected(const FAssetData& SelectedAsset);

private:
	TSharedPtr<SWindow> PickerWindow;
};



