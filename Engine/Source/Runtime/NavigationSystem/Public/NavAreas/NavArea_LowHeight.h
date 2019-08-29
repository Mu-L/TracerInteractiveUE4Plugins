// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "AI/Navigation/NavigationTypes.h"
#include "NavAreas/NavArea.h"
#include "NavArea_LowHeight.generated.h"

/** Special area that can be generated in spaces with insufficient free height above. Cannot be traversed by anyone. */
UCLASS(Config = Engine)
class NAVIGATIONSYSTEM_API UNavArea_LowHeight : public UNavArea
{
	GENERATED_UCLASS_BODY()
public:
	virtual bool IsLowArea() const override { return true; }
};
