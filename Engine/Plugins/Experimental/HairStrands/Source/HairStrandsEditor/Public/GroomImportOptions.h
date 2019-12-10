// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GroomSettings.h"
#include "UObject/Object.h"
#include "GroomImportOptions.generated.h"

UCLASS(BlueprintType, config = EditorPerProjectUserSettings)
class HAIRSTRANDSEDITOR_API UGroomImportOptions : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, meta = (ShowOnlyInnerProperties), Category = Conversion)
	FGroomConversionSettings ConversionSettings;

	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, meta = (ShowOnlyInnerProperties), Category = BuildSettings)
	FGroomBuildSettings BuildSettings;
};
