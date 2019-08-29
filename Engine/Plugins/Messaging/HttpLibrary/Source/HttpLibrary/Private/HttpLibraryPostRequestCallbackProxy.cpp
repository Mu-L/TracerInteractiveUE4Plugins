// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#include "HttpLibraryPostRequestCallbackProxy.h"
#include "HttpLibraryHelpers.h"

UHttpLibraryPostRequestCallbackProxy::UHttpLibraryPostRequestCallbackProxy( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{
	//
}

void UHttpLibraryPostRequestCallbackProxy::ProcessRequest( const FString& URL, const TMap<FString, FString>& QueryString, const TMap<FString, FString>& Headers, const FJsonLibraryValue& Content )
{
	Http.Method = EHttpLibraryRequestMethod::POST;
	Http.URL    = URL;

	Http.QueryString = QueryString;
	Http.Headers     = Headers;

	Http.OnResponse.BindUObject( this, &UHttpLibraryPostRequestCallbackProxy::TriggerResponse );
	Http.Send( Content );
}

void UHttpLibraryPostRequestCallbackProxy::TriggerResponse( int32 StatusCode, const FJsonLibraryValue& Content )
{
	if ( StatusCode > 0 )
		OnSuccess.Broadcast( Content, StatusCode );
	else
		OnFailure.Broadcast( FJsonLibraryValue::Parse( FString() ), 0 );

	Http.Reset();
}

UHttpLibraryPostRequestCallbackProxy* UHttpLibraryPostRequestCallbackProxy::CreateProxyObjectForPost( const FString& URL, const TMap<FString, FString>& QueryString, const TMap<FString, FString>& Headers, const FJsonLibraryValue& Content )
{
	UHttpLibraryPostRequestCallbackProxy* Proxy = NewObject<UHttpLibraryPostRequestCallbackProxy>();
	Proxy->SetFlags( RF_StrongRefOnFrame );
	Proxy->ProcessRequest( URL, QueryString, Headers, Content );
	return Proxy;
}
