// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Math/Color.h"

#include "AudioModulationStyle.generated.h"

UCLASS()
class AUDIOMODULATION_API UAudioModulationStyle : public UBlueprintFunctionLibrary
{
public:
	GENERATED_BODY()

	UFUNCTION(BlueprintCallable, Category = "Audio|Modulation|Style")
	static const FColor GetVolumeBusColor() { return FColor(33, 183, 0); }

	UFUNCTION(BlueprintCallable, Category = "Audio|Modulation|Style")
	static const FColor GetPitchBusColor() { return FColor(181, 21, 0); }

	UFUNCTION(BlueprintCallable, Category = "Audio|Modulation|Style")
	static const FColor GetLPFBusColor() { return FColor(0, 156, 183); }

	UFUNCTION(BlueprintCallable, Category = "Audio|Modulation|Style")
	static const FColor GetHPFBusColor() { return FColor(94, 237, 183); }

	UFUNCTION(BlueprintCallable, Category = "Audio|Modulation|Style")
	static const FColor GetControlBusColor() { return FColor(215, 180, 210); }
};