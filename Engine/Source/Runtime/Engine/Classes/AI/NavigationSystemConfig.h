// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPath.h"
#include "NavigationSystemConfig.generated.h"

class UNavigationSystemBase;
class UWorld;


UCLASS(config = Engine, defaultconfig, EditInlineNew, DisplayName = "Generic Navigation System Config", collapseCategories)
class ENGINE_API UNavigationSystemConfig : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category=Navigation, meta = (MetaClass = "NavigationSystemBase", NoResetToDefault))
	FSoftClassPath NavigationSystemClass;
	
public:
	UNavigationSystemConfig(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual UNavigationSystemBase* CreateAndConfigureNavigationSystem(UWorld& World) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR

	static TSubclassOf<UNavigationSystemConfig> GetDefaultConfigClass();
};

UCLASS(MinimalAPI, HideCategories=Navigation)
class UNullNavSysConfig : public UNavigationSystemConfig
{
	GENERATED_BODY()
public:
	UNullNavSysConfig(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
