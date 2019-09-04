// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#include "HttpLibraryRequestCallbackProxy.h"
#include "HttpLibraryHelpers.h"

UHttpLibraryRequestCallbackProxy::UHttpLibraryRequestCallbackProxy( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{
	HttpSent     = 0;
	HttpReceived = 0;
}

void UHttpLibraryRequestCallbackProxy::ProcessRequest( EHttpLibraryRequestMethod Method, const FString& URL, const TMap<FString, FString>& QueryString, const TMap<FString, FString>& Headers, const TArray<uint8>& Content, EHttpLibraryContentType ContentType )
{
	Http.Method = Method;
	Http.URL    = URL;

	Http.QueryString = QueryString;
	Http.Headers     = Headers;

	Http.OnResponse.BindUObject( this, &UHttpLibraryRequestCallbackProxy::TriggerResponse );
	Http.OnProgress.BindUObject( this, &UHttpLibraryRequestCallbackProxy::TriggerProgress );

	Http.Send( Content, ContentType );
}

void UHttpLibraryRequestCallbackProxy::TriggerResponse( int32 StatusCode, const TMap<FString, FString>& Headers, EHttpLibraryContentType ContentType, const TArray<uint8>& Content )
{
	if ( StatusCode > 0 )
		OnSuccess.Broadcast( Content, ContentType, StatusCode, HttpSent, HttpReceived );
	else
		OnFailure.Broadcast( TArray<uint8>(), EHttpLibraryContentType::Default, 0, HttpSent, HttpReceived );
	
	HttpSent     = 0;
	HttpReceived = 0;

	Http.Reset();
}

void UHttpLibraryRequestCallbackProxy::TriggerProgress( int32 Sent, int32 Received )
{
	HttpSent     = Sent;
	HttpReceived = Received;

	OnProgress.Broadcast( TArray<uint8>(), EHttpLibraryContentType::Default, 0, HttpSent, HttpReceived );
}

UHttpLibraryRequestCallbackProxy* UHttpLibraryRequestCallbackProxy::CreateProxyObjectForRequest( EHttpLibraryRequestMethod Method, const FString& URL, const TMap<FString, FString>& QueryString, const TMap<FString, FString>& Headers, const TArray<uint8>& Content, EHttpLibraryContentType ContentType )
{
	UHttpLibraryRequestCallbackProxy* Proxy = NewObject<UHttpLibraryRequestCallbackProxy>();
	Proxy->SetFlags( RF_StrongRefOnFrame );
	Proxy->ProcessRequest( Method, URL, QueryString, Headers, Content, ContentType );
	return Proxy;
}
