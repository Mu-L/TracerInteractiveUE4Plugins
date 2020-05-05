// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ClothPhysicalMeshData.h"
#include "ChaosWeightMapTarget.generated.h"


/** Targets for painted weight maps (aka masks). */
UENUM()
enum class EChaosWeightMapTarget : uint8
{
	None                = (uint8)EWeightMapTargetCommon::None,
	MaxDistance         = (uint8)EWeightMapTargetCommon::MaxDistance,
	BackstopDistance    = (uint8)EWeightMapTargetCommon::BackstopDistance,
	BackstopRadius      = (uint8)EWeightMapTargetCommon::BackstopRadius,
	AnimDriveMultiplier = (uint8)EWeightMapTargetCommon::AnimDriveMultiplier
	// Add Chaos specific maps below this line
};
