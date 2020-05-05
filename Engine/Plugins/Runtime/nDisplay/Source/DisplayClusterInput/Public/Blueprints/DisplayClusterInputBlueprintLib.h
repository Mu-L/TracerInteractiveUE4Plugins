// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Blueprints/IDisplayClusterInputBlueprintAPI.h"
#include "DisplayClusterInputBlueprintLib.generated.h"


/**
 * Blueprint API function library
 */
UCLASS()
class UDisplayClusterInputBlueprintLib
	: public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

public:
	/** Return Display Cluster API interface. */
	UFUNCTION(BlueprintPure, meta = (DisplayName = "DisplayClusterInput Module API"), Category = "nDisplay")
	static void GetAPI(TScriptInterface<IDisplayClusterInputBlueprintAPI>& OutAPI);
};
