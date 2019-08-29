// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#include "HttpLibraryGetRequestCallbackProxy.h"
#include "HttpLibraryHelpers.h"

UHttpLibraryGetRequestCallbackProxy::UHttpLibraryGetRequestCallbackProxy( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{
	//
}

void UHttpLibraryGetRequestCallbackProxy::ProcessRequest( const FString& URL, const TMap<FString, FString>& QueryString, const TMap<FString, FString>& Headers )
{
	Http.Method = EHttpLibraryRequestMethod::GET;
	Http.URL    = URL;

	Http.QueryString = QueryString;
	Http.Headers     = Headers;

	Http.OnResponse.BindUObject( this, &UHttpLibraryGetRequestCallbackProxy::TriggerResponse );
	Http.Send();
}

void UHttpLibraryGetRequestCallbackProxy::TriggerResponse( int32 StatusCode, const FJsonLibraryValue& Content )
{
	if ( StatusCode > 0 )
		OnSuccess.Broadcast( Content, StatusCode );
	else
		OnFailure.Broadcast( FJsonLibraryValue::Parse( FString() ), 0 );

	Http.Reset();
}

UHttpLibraryGetRequestCallbackProxy* UHttpLibraryGetRequestCallbackProxy::CreateProxyObjectForGet( const FString& URL, const TMap<FString, FString>& QueryString, const TMap<FString, FString>& Headers )
{
	UHttpLibraryGetRequestCallbackProxy* Proxy = NewObject<UHttpLibraryGetRequestCallbackProxy>();
	Proxy->SetFlags( RF_StrongRefOnFrame );
	Proxy->ProcessRequest( URL, QueryString, Headers );
	return Proxy;
}
