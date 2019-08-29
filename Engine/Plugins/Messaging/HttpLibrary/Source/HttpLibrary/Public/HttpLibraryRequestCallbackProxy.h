// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Http.h"
#include "HttpLibraryEnums.h"
#include "HttpLibraryBinaryRequest.h"
#include "HttpLibraryRequestCallbackProxy.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams( FHttpLibraryRequestCallback, const TArray<uint8>&, Response, EHttpLibraryContentType, ContentType, int32, StatusCode, int32, BytesSent, int32, BytesReceived );

UCLASS(MinimalAPI)
class UHttpLibraryRequestCallbackProxy : public UObject
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(BlueprintAssignable)
	FHttpLibraryRequestCallback OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FHttpLibraryRequestCallback OnProgress;

	UPROPERTY(BlueprintAssignable)
	FHttpLibraryRequestCallback OnFailure;

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", DisplayName = "HTTP Request Proxy", AdvancedDisplay = "Method,QueryString,Headers,Content,ContentType", AutoCreateRefTerm = "QueryString,Headers,Content"), Category = "HTTP Library")
	static UHttpLibraryRequestCallbackProxy* CreateProxyObjectForRequest( EHttpLibraryRequestMethod Method, const FString& URL, const TMap<FString, FString>& QueryString, const TMap<FString, FString>& Headers, const TArray<uint8>& Content, EHttpLibraryContentType ContentType = EHttpLibraryContentType::Default );

protected:

	FHttpLibraryBinaryRequest Http;

	void TriggerResponse( int32 StatusCode, const TMap<FString, FString>& Headers, EHttpLibraryContentType ContentType, const TArray<uint8>& Content );
	void TriggerProgress( int32 Sent, int32 Received );

	void ProcessRequest( EHttpLibraryRequestMethod Method, const FString& URL, const TMap<FString, FString>& QueryString, const TMap<FString, FString>& Headers, const TArray<uint8>& Content, EHttpLibraryContentType ContentType );

private:

	int32 HttpSent;
	int32 HttpReceived;
};
