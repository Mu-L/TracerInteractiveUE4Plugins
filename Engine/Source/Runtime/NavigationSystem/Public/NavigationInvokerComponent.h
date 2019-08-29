// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "Components/ActorComponent.h"
#include "NavigationInvokerComponent.generated.h"

class UNavigationSystemV1;

UCLASS(ClassGroup = (Navigation), meta = (BlueprintSpawnableComponent), hidecategories = (Activation))
class NAVIGATIONSYSTEM_API UNavigationInvokerComponent : public UActorComponent
{
	GENERATED_BODY()

protected:

	UPROPERTY(EditAnywhere, Category = Navigation, meta = (ClampMin = "0.1", UIMin = "0.1"))
	float TileGenerationRadius;

	UPROPERTY(EditAnywhere, Category = Navigation, meta = (ClampMin = "0.1", UIMin = "0.1"))
	float TileRemovalRadius;

public:
	UNavigationInvokerComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	void RegisterWithNavigationSystem(UNavigationSystemV1& NavSys);

protected:
	virtual void Activate(bool bReset = false) override;
	virtual void Deactivate() override;
};
