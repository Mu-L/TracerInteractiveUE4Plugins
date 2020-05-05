// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "NavAreas/NavArea.h"
#include "NavArea_Null.generated.h"

/** In general represents an empty area, that cannot be traversed by anyone. Ever.*/
UCLASS(Config=Engine)
class NAVIGATIONSYSTEM_API UNavArea_Null : public UNavArea
{
	GENERATED_UCLASS_BODY()
};
