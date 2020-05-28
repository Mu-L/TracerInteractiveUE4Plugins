// Copyright 2020 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "Components/WidgetInteractionComponent.h"
#include "WebInterfaceInteractionComponent.generated.h"

UCLASS(ClassGroup = "UserInterface", meta = (BlueprintSpawnableComponent))
class WEBUI_API UWebInterfaceInteractionComponent : public UWidgetInteractionComponent
{
	GENERATED_UCLASS_BODY()

protected:

	virtual FWidgetTraceResult PerformTrace() const override;
};
