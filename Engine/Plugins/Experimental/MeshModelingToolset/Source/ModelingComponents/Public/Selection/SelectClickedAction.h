// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "BaseBehaviors/BehaviorTargetInterfaces.h"

#pragma once

/**
 * BehaviorTarget to do world raycast selection from a click
 * Currently used to click-select reference planes in the world
 */
class FSelectClickedAction : public IClickBehaviorTarget
{
	bool DoRayCast(const FInputDeviceRay& ClickPos, bool callbackOnHit)
	{
		FVector RayStart = ClickPos.WorldRay.Origin;
		FVector RayEnd = ClickPos.WorldRay.PointAt(999999);
		FCollisionObjectQueryParams QueryParams(FCollisionObjectQueryParams::AllObjects);
		FHitResult Result;
		bool bHitWorld = World->LineTraceSingleByObjectType(Result, RayStart, RayEnd, QueryParams);
		if (callbackOnHit && bHitWorld && OnClickedPositionFunc != nullptr)
		{
			OnClickedPositionFunc(Result);
		}
		return bHitWorld;
	}

public:
	UWorld* World;
	TFunction<void(const FHitResult&)> OnClickedPositionFunc = nullptr;

	virtual bool IsHitByClick(const FInputDeviceRay& ClickPos) override
	{
		return DoRayCast(ClickPos, false);
	}

	virtual void OnClicked(const FInputDeviceRay& ClickPos) override
	{
		DoRayCast(ClickPos, true);
	}
};

