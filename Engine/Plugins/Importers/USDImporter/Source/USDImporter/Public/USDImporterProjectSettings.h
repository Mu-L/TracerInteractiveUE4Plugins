// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "USDPrimResolver.h"
#include "USDImporterProjectSettings.generated.h"

UCLASS(config=Editor, meta=(DisplayName=USDImporter), Deprecated)
class UDEPRECATED_UUSDImporterProjectSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(config, EditAnywhere, Category=General)
	TArray<FDirectoryPath> AdditionalPluginDirectories;

	/**
	 * Allows a custom class to be specified that will resolve UsdPrim objects in a usd file to actors and assets
	 */
	UPROPERTY(config)
	TSubclassOf<UDEPRECATED_UUSDPrimResolver> CustomPrimResolver_DEPRECATED;
};