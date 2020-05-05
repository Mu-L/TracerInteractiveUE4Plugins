// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "MatineeTrackVectorPropHelper.h"
#include "MatineeTrackLinearColorPropHelper.generated.h"

class UInterpGroup;
class UInterpTrack;

UCLASS()
class UMatineeTrackLinearColorPropHelper : public UMatineeTrackVectorPropHelper
{
	GENERATED_UCLASS_BODY()

public:

	// UInterpTrackHelper interface

	virtual	bool PreCreateTrack( UInterpGroup* Group, const UInterpTrack *TrackDef, bool bDuplicatingTrack, bool bAllowPrompts ) const override;
	virtual void  PostCreateTrack( UInterpTrack *Track, bool bDuplicatingTrack, int32 TrackIndex ) const override;
};

