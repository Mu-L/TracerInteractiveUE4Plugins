// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#include "HttpLibraryJsonRequest.h"
#include "HttpLibraryHelpers.h"

void StaticJsonProgress( FHttpRequestPtr Request, int32 BytesSent, int32 BytesReceived, FHttpLibraryProgress OnProgress )
{
	OnProgress.ExecuteIfBound( BytesSent, BytesReceived );
}

void StaticJsonResponse( FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, FHttpLibraryJsonResponse OnResponse )
{
	if ( !OnResponse.IsBound() )
		return;

	if ( Response.IsValid() && bWasSuccessful )
	{
		const int32                   ResponseCode    = Response->GetResponseCode();
		const TArray<uint8>&          ResponseContent = Response->GetContent();
		const EHttpLibraryContentType ResponseType    = UHttpLibraryHelpers::FindContentType( Response->GetContentType() );
		
		switch ( ResponseType )
		{
			case EHttpLibraryContentType::Default:
			case EHttpLibraryContentType::JSON:
			case EHttpLibraryContentType::JS:
			case EHttpLibraryContentType::TXT:
				OnResponse.Execute( ResponseCode, UHttpLibraryHelpers::ConvertBytesToJson( ResponseContent ) );
				break;

			default:
				OnResponse.Execute( ResponseCode, FJsonLibraryValue::Parse( FString() ) );
				break;
		}
	}
	else
		OnResponse.Execute( 0, FJsonLibraryValue::Parse( FString() ) );
}


bool FHttpLibraryJsonRequest::Process()
{
	HttpRequest->OnRequestProgress().BindStatic( &StaticJsonProgress, OnProgress );
	HttpRequest->OnProcessRequestComplete().BindStatic( &StaticJsonResponse, OnResponse );

	return IHttpLibraryRequest::Process();
}


UHttpLibraryJsonRequest::UHttpLibraryJsonRequest( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{
	Http.OnResponse.BindUObject( this, &UHttpLibraryJsonRequest::TriggerResponse );
	Http.OnProgress.BindUObject( this, &UHttpLibraryJsonRequest::TriggerProgress );
}

void UHttpLibraryJsonRequest::TriggerResponse( int32 StatusCode, const FJsonLibraryValue& Content )
{
	OnResponse.ExecuteIfBound( StatusCode, Content );
}

void UHttpLibraryJsonRequest::TriggerProgress( int32 Sent, int32 Received )
{
	OnProgress.ExecuteIfBound( Sent, Received );
}

bool UHttpLibraryJsonRequest::IsRunning() const
{
	return Http.IsRunning();
}

bool UHttpLibraryJsonRequest::IsComplete() const
{
	return Http.IsComplete();
}

bool UHttpLibraryJsonRequest::Send( const FString& URL, const TMap<FString, FString>& QueryString, const TMap<FString, FString>& Headers, EHttpLibraryRequestMethod Method /*= EHttpLibraryRequestMethod::GET*/ )
{
	if ( Http.IsRunning() )
		return false;

	Http.Method = Method;
	Http.URL    = URL;

	Http.QueryString = QueryString;
	Http.Headers     = Headers;
	
	return Http.Send();
}

bool UHttpLibraryJsonRequest::SendString( const FString& URL, const TMap<FString, FString>& QueryString, const TMap<FString, FString>& Headers, const FString& Content, EHttpLibraryContentType ContentType /*= EHttpLibraryContentType::Default*/, EHttpLibraryRequestMethod Method /*= EHttpLibraryRequestMethod::POST*/ )
{
	if ( Http.IsRunning() )
		return false;

	Http.Method = Method;
	Http.URL    = URL;

	Http.QueryString = QueryString;
	Http.Headers     = Headers;
	
	return Http.Send( Content, ContentType );
}

bool UHttpLibraryJsonRequest::SendJSON( const FString& URL, const TMap<FString, FString>& QueryString, const TMap<FString, FString>& Headers, const FJsonLibraryValue& Content, EHttpLibraryRequestMethod Method /*= EHttpLibraryRequestMethod::POST*/ )
{
	if ( Http.IsRunning() )
		return false;

	Http.Method = Method;
	Http.URL    = URL;

	Http.QueryString = QueryString;
	Http.Headers     = Headers;
	
	return Http.Send( Content );
}

bool UHttpLibraryJsonRequest::SendBinary( const FString& URL, const TMap<FString, FString>& QueryString, const TMap<FString, FString>& Headers, const TArray<uint8>& Content, EHttpLibraryContentType ContentType /*= EHttpLibraryContentType::Default*/, EHttpLibraryRequestMethod Method /*= EHttpLibraryRequestMethod::POST*/ )
{
	if ( Http.IsRunning() )
		return false;

	Http.Method = Method;
	Http.URL    = URL;

	Http.QueryString = QueryString;
	Http.Headers     = Headers;

	return Http.Send( Content, ContentType );
}

bool UHttpLibraryJsonRequest::Cancel()
{
	if ( !Http.IsRunning() )
		return false;

	Http.Cancel();
	return true;
}
