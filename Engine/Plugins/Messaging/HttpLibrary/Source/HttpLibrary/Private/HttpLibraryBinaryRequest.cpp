// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#include "HttpLibraryBinaryRequest.h"
#include "HttpLibraryHelpers.h"

void StaticBinaryProgress( FHttpRequestPtr Request, int32 BytesSent, int32 BytesReceived, FHttpLibraryProgress OnProgress )
{
	OnProgress.ExecuteIfBound( BytesSent, BytesReceived );
}

void StaticBinaryResponse( FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, FHttpLibraryBinaryResponse OnResponse )
{
	if ( !OnResponse.IsBound() )
		return;

	if ( Response.IsValid() && bWasSuccessful )
	{
		const int32                   ResponseCode    = Response->GetResponseCode();
		const TArray<FString>&        ResponseHeaders = Response->GetAllHeaders();
		const TArray<uint8>&          ResponseContent = Response->GetContent();
		const EHttpLibraryContentType ResponseType    = UHttpLibraryHelpers::FindContentType( Response->GetContentType() );

		TMap<FString, FString> Headers;
		for ( int32 i = 0; i < ResponseHeaders.Num(); i++ )
		{
			FString Key, Value;
			if ( !ResponseHeaders[ i ].Split( TEXT( ": " ), &Key, &Value ) )
				continue;
			
			if ( !Key.IsEmpty() && Key.ToLower() != "content-type" )
				Headers.Add( Key, Value );
		}

		OnResponse.Execute( ResponseCode, Headers, ResponseType, ResponseContent );
	}
	else
		OnResponse.Execute( 0, TMap<FString, FString>(), EHttpLibraryContentType::Default, TArray<uint8>() );
}


bool FHttpLibraryBinaryRequest::Process()
{
	HttpRequest->OnRequestProgress().BindStatic( &StaticBinaryProgress, OnProgress );
	HttpRequest->OnProcessRequestComplete().BindStatic( &StaticBinaryResponse, OnResponse );

	return IHttpLibraryRequest::Process();
}


UHttpLibraryBinaryRequest::UHttpLibraryBinaryRequest( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{
	Http.OnResponse.BindUObject( this, &UHttpLibraryBinaryRequest::TriggerResponse );
	Http.OnProgress.BindUObject( this, &UHttpLibraryBinaryRequest::TriggerProgress );
}

void UHttpLibraryBinaryRequest::TriggerResponse( int32 StatusCode, const TMap<FString, FString>& Headers, EHttpLibraryContentType Type, const TArray<uint8>& Content )
{
	TArray<FString> Array;
	for ( const TPair<FString, FString>& Temp : Headers )
		Array.Add( FString::Printf( TEXT( "%s: %s" ), *Temp.Key, *Temp.Value ) );

	OnResponse.ExecuteIfBound( StatusCode, Array, Type, Content );
}

void UHttpLibraryBinaryRequest::TriggerProgress( int32 Sent, int32 Received )
{
	OnProgress.ExecuteIfBound( Sent, Received );
}

bool UHttpLibraryBinaryRequest::IsRunning() const
{
	return Http.IsRunning();
}

bool UHttpLibraryBinaryRequest::IsComplete() const
{
	return Http.IsComplete();
}

bool UHttpLibraryBinaryRequest::Send( const FString& URL, const TMap<FString, FString>& QueryString, const TMap<FString, FString>& Headers, EHttpLibraryRequestMethod Method /*= EHttpLibraryRequestMethod::GET*/ )
{
	if ( Http.IsRunning() )
		return false;

	Http.Method = Method;
	Http.URL    = URL;

	Http.QueryString = QueryString;
	Http.Headers     = Headers;
	
	return Http.Send();
}

bool UHttpLibraryBinaryRequest::SendString( const FString& URL, const TMap<FString, FString>& QueryString, const TMap<FString, FString>& Headers, const FString& Content, EHttpLibraryContentType ContentType /*= EHttpLibraryContentType::Default*/, EHttpLibraryRequestMethod Method /*= EHttpLibraryRequestMethod::POST*/ )
{
	if ( Http.IsRunning() )
		return false;

	Http.Method = Method;
	Http.URL    = URL;

	Http.QueryString = QueryString;
	Http.Headers     = Headers;
	
	return Http.Send( Content, ContentType );
}

bool UHttpLibraryBinaryRequest::SendJSON( const FString& URL, const TMap<FString, FString>& QueryString, const TMap<FString, FString>& Headers, const FJsonLibraryValue& Content, EHttpLibraryRequestMethod Method /*= EHttpLibraryRequestMethod::POST*/ )
{
	if ( Http.IsRunning() )
		return false;

	Http.Method = Method;
	Http.URL    = URL;

	Http.QueryString = QueryString;
	Http.Headers     = Headers;
	
	return Http.Send( Content );
}

bool UHttpLibraryBinaryRequest::SendBinary( const FString& URL, const TMap<FString, FString>& QueryString, const TMap<FString, FString>& Headers, const TArray<uint8>& Content, EHttpLibraryContentType ContentType /*= EHttpLibraryContentType::Default*/, EHttpLibraryRequestMethod Method /*= EHttpLibraryRequestMethod::POST*/ )
{
	if ( Http.IsRunning() )
		return false;

	Http.Method = Method;
	Http.URL    = URL;

	Http.QueryString = QueryString;
	Http.Headers     = Headers;

	return Http.Send( Content, ContentType );
}

bool UHttpLibraryBinaryRequest::Cancel()
{
	if ( !Http.IsRunning() )
		return false;

	Http.Cancel();
	return true;
}
