// Copyright 2021 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "JsonLibrary.h"
#include "WebInterfaceCallback.h"
#include "WebInterfaceHelpers.generated.h"

UCLASS()
class WEBUI_API UWebInterfaceHelpers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

#if UE_EDITOR
protected:
#else
public:
#endif

	// Check if this callback is valid.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Valid"), Category = "Web UI|Callback")
	static bool WebInterfaceCallback_IsValid( UPARAM(ref) const FWebInterfaceCallback& Target );
	// Call ue.interface.callback(data) in the browser context.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Call", AdvancedDisplay = "Data", AutoCreateRefTerm = "Data"), Category = "Web UI|Callback")
	static void WebInterfaceCallback_Call( UPARAM(ref) FWebInterfaceCallback& Target, const FJsonLibraryValue& Data );
};
