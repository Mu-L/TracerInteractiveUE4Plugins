// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Http.h"
#include "HttpLibraryEnums.h"
#include "HttpLibraryJsonRequest.h"
#include "JsonLibrary.h"
#include "HttpLibraryGetRequestCallbackProxy.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams( FHttpLibraryGetRequestCallback, const FJsonLibraryValue&, Response, int32, StatusCode );

UCLASS(MinimalAPI)
class UHttpLibraryGetRequestCallbackProxy : public UObject
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(BlueprintAssignable)
	FHttpLibraryGetRequestCallback OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FHttpLibraryGetRequestCallback OnFailure;

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", DisplayName = "HTTP GET Request Proxy", AdvancedDisplay = "QueryString,Headers", AutoCreateRefTerm = "QueryString,Headers"), Category = "HTTP Library")
	static UHttpLibraryGetRequestCallbackProxy* CreateProxyObjectForGet( const FString& URL, const TMap<FString, FString>& QueryString, const TMap<FString, FString>& Headers );

protected:

	FHttpLibraryJsonRequest Http;
	
	void ProcessRequest( const FString& URL, const TMap<FString, FString>& QueryString, const TMap<FString, FString>& Headers );
	void TriggerResponse( int32 StatusCode, const FJsonLibraryValue& Content );
};
