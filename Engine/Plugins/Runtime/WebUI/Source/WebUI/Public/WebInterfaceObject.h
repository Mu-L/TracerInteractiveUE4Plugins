// Copyright 2021 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "JsonLibrary.h"
#include "WebInterfaceObject.generated.h"

class UWebInterface;

UCLASS()
class WEBUI_API UWebInterfaceObject : public UObject
{
	friend class UWebInterface;

	GENERATED_BODY()

public:
	
	UFUNCTION(BlueprintCallable, Category = "Web UI")
	void Broadcast( const FString& Name, const FString& Data, const FString& Callback );

private:

	TWeakObjectPtr<UWebInterface> MyInterface;
};
