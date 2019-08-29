// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PersistentUserWidget.generated.h"

UCLASS(Abstract, EditInlineNew, BlueprintType, Blueprintable, meta=(DontUseGenericSpawnObject="True"))
class WEBUI_API UPersistentUserWidget : public UUserWidget
{
    GENERATED_UCLASS_BODY()

public:

    virtual void OnLevelRemovedFromWorld( ULevel* InLevel, UWorld* InWorld ) override;
};
