// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "NavAreas/NavArea.h"
#include "NavArea_Obstacle.generated.h"

/** In general represents a high cost area, that shouldn't be traversed by anyone unless no other path exist.*/
UCLASS(Config = Engine)
class NAVIGATIONSYSTEM_API UNavArea_Obstacle : public UNavArea
{
	GENERATED_UCLASS_BODY()
};
