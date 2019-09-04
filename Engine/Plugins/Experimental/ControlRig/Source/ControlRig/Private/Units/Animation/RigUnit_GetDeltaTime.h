// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Animation/RigUnit_AnimBase.h"
#include "RigUnit_GetDeltaTime.generated.h"

/**
 * Returns the time gone by from the previous evaluation
 */
USTRUCT(meta=(DisplayName="Delta Time"))
struct FRigUnit_GetDeltaTime : public FRigUnit_AnimBase
{
	GENERATED_BODY()
	
	FRigUnit_GetDeltaTime()
	{
		Result = 0.f;
	}

	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Output))
	float Result;
};

