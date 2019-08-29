// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "InterpTrackHelper.h"
#include "MatineeTrackToggleHelper.generated.h"

class IMatineeBase;
class UInterpTrack;

UCLASS()
class UMatineeTrackToggleHelper : public UInterpTrackHelper
{
	GENERATED_UCLASS_BODY()

	void OnAddKeyTextEntry(const FString& ChosenText, IMatineeBase* Matinee, UInterpTrack* Track);

public:

	// UInterpTrackHelper interface

	virtual	bool PreCreateKeyframe( UInterpTrack *Track, float KeyTime ) const override;
	virtual void  PostCreateKeyframe( UInterpTrack *Track, int32 KeyIndex ) const override;
};
