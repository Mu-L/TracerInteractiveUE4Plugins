// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Http.h"
#include "HttpLibraryEnums.h"
#include "HttpLibraryJsonRequest.h"
#include "JsonLibrary.h"
#include "HttpLibraryPostRequestCallbackProxy.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams( FHttpLibraryPostRequestCallback, const FJsonLibraryValue&, Response, int32, StatusCode );

UCLASS(MinimalAPI)
class UHttpLibraryPostRequestCallbackProxy : public UObject
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(BlueprintAssignable)
	FHttpLibraryPostRequestCallback OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FHttpLibraryPostRequestCallback OnFailure;

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", DisplayName = "HTTP POST Request Proxy", AdvancedDisplay = "QueryString,Headers", AutoCreateRefTerm = "QueryString,Headers,Content"), Category = "HTTP Library")
	static UHttpLibraryPostRequestCallbackProxy* CreateProxyObjectForPost( const FString& URL, const TMap<FString, FString>& QueryString, const TMap<FString, FString>& Headers, const FJsonLibraryValue& Content );

protected:

	FHttpLibraryJsonRequest Http;
	
	void ProcessRequest( const FString& URL, const TMap<FString, FString>& QueryString, const TMap<FString, FString>& Headers, const FJsonLibraryValue& Content );
	void TriggerResponse( int32 StatusCode, const FJsonLibraryValue& Content );
};
