// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#include "HttpLibraryRequest.h"
#include "HttpLibraryHelpers.h"

void StaticProgress( FHttpRequestPtr Request, int32 BytesSent, int32 BytesReceived, FHttpLibraryProgress OnProgress )
{
	OnProgress.ExecuteIfBound( BytesSent, BytesReceived );
}

void StaticResponse( FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, FHttpLibraryResponse OnResponse )
{
	if ( !OnResponse.IsBound() )
		return;

	if ( Response.IsValid() && bWasSuccessful )
	{
		const int32                   ResponseCode    = Response->GetResponseCode();
		const TArray<uint8>&          ResponseContent = Response->GetContent();
		const EHttpLibraryContentType ResponseType    = UHttpLibraryHelpers::FindContentType( Response->GetContentType() );

		OnResponse.Execute( ResponseCode, ResponseType, UHttpLibraryHelpers::ConvertBytesToString( ResponseContent ) );
	}
	else
		OnResponse.Execute( 0, EHttpLibraryContentType::Default, FString() );
}

bool FHttpLibraryRequest::Process()
{
	HttpRequest->OnRequestProgress().BindStatic( &StaticProgress, OnProgress );
	HttpRequest->OnProcessRequestComplete().BindStatic( &StaticResponse, OnResponse );

	return IHttpLibraryRequest::Process();
}


UHttpLibraryRequest::UHttpLibraryRequest( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{
	Http.OnResponse.BindUObject( this, &UHttpLibraryRequest::TriggerResponse );
	Http.OnProgress.BindUObject( this, &UHttpLibraryRequest::TriggerProgress );
}

void UHttpLibraryRequest::TriggerResponse( int32 StatusCode, EHttpLibraryContentType Type, const FString& Content )
{
	OnResponse.ExecuteIfBound( StatusCode, Type, Content );
}

void UHttpLibraryRequest::TriggerProgress( int32 Sent, int32 Received )
{
	OnProgress.ExecuteIfBound( Sent, Received );
}

bool UHttpLibraryRequest::IsRunning() const
{
	return Http.IsRunning();
}

bool UHttpLibraryRequest::IsComplete() const
{
	return Http.IsComplete();
}

bool UHttpLibraryRequest::Send( const FString& URL, const TMap<FString, FString>& QueryString, const TMap<FString, FString>& Headers, EHttpLibraryRequestMethod Method /*= EHttpLibraryRequestMethod::GET*/ )
{
	if ( Http.IsRunning() )
		return false;

	Http.Method = Method;
	Http.URL    = URL;

	Http.QueryString = QueryString;
	Http.Headers     = Headers;
	
	return Http.Send();
}

bool UHttpLibraryRequest::SendString( const FString& URL, const TMap<FString, FString>& QueryString, const TMap<FString, FString>& Headers, const FString& Content, EHttpLibraryContentType ContentType /*= EHttpLibraryContentType::Default*/, EHttpLibraryRequestMethod Method /*= EHttpLibraryRequestMethod::POST*/ )
{
	if ( Http.IsRunning() )
		return false;

	Http.Method = Method;
	Http.URL    = URL;

	Http.QueryString = QueryString;
	Http.Headers     = Headers;
	
	return Http.Send( Content, ContentType );
}

bool UHttpLibraryRequest::SendJSON( const FString& URL, const TMap<FString, FString>& QueryString, const TMap<FString, FString>& Headers, const FJsonLibraryValue& Content, EHttpLibraryRequestMethod Method /*= EHttpLibraryRequestMethod::POST*/ )
{
	if ( Http.IsRunning() )
		return false;

	Http.Method = Method;
	Http.URL    = URL;

	Http.QueryString = QueryString;
	Http.Headers     = Headers;
	
	return Http.Send( Content );
}

bool UHttpLibraryRequest::SendBinary( const FString& URL, const TMap<FString, FString>& QueryString, const TMap<FString, FString>& Headers, const TArray<uint8>& Content, EHttpLibraryContentType ContentType /*= EHttpLibraryContentType::Default*/, EHttpLibraryRequestMethod Method /*= EHttpLibraryRequestMethod::POST*/ )
{
	if ( Http.IsRunning() )
		return false;

	Http.Method = Method;
	Http.URL    = URL;

	Http.QueryString = QueryString;
	Http.Headers     = Headers;

	return Http.Send( Content, ContentType );
}

bool UHttpLibraryRequest::Cancel()
{
	if ( !Http.IsRunning() )
		return false;

	Http.Cancel();
	return true;
}
