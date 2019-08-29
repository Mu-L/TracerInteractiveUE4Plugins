// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#include "IHttpLibraryRequest.h"
#include "HttpLibraryHelpers.h"

IHttpLibraryRequest::IHttpLibraryRequest()
{
	Method = EHttpLibraryRequestMethod::GET;
}

IHttpLibraryRequest::~IHttpLibraryRequest()
{
	HttpRequest.Reset();
}

bool IHttpLibraryRequest::Create()
{
	return Create( TArray<uint8>(), EHttpLibraryContentType::Default );
}

bool IHttpLibraryRequest::Create( const FString& Content, EHttpLibraryContentType ContentType /*= EHttpLibraryContentType::Default*/ )
{
	return Create( UHttpLibraryHelpers::ConvertStringToBytes( Content ), ContentType );
}

bool IHttpLibraryRequest::Create( const TArray<uint8>& Content, EHttpLibraryContentType ContentType /*= EHttpLibraryContentType::Default*/ )
{
	if ( HttpRequest.IsValid() )
		HttpRequest.Reset();

	if ( URL.IsEmpty() )
		return false;

	FHttpModule* Http = &FHttpModule::Get();
	if ( !Http || !Http->IsHttpEnabled() )
		return false;
	
	HttpRequest = Http->CreateRequest();
	if ( !HttpRequest.IsValid() )
		return false;

	switch ( Method )
	{
		default:
		case EHttpLibraryRequestMethod::GET:     HttpRequest->SetVerb( "GET" );     break;
		case EHttpLibraryRequestMethod::POST:    HttpRequest->SetVerb( "POST" );    break;
		case EHttpLibraryRequestMethod::PUT:     HttpRequest->SetVerb( "PUT" );     break;
		case EHttpLibraryRequestMethod::PATCH:   HttpRequest->SetVerb( "PATCH" );   break;
		case EHttpLibraryRequestMethod::DELETE:  HttpRequest->SetVerb( "DELETE" );  break;
		case EHttpLibraryRequestMethod::HEAD:    HttpRequest->SetVerb( "HEAD" );    break;
		case EHttpLibraryRequestMethod::CONNECT: HttpRequest->SetVerb( "CONNECT" ); break;
		case EHttpLibraryRequestMethod::OPTIONS: HttpRequest->SetVerb( "OPTIONS" ); break;
		case EHttpLibraryRequestMethod::TRACE:   HttpRequest->SetVerb( "TRACE" );   break;
	}

	if ( QueryString.Num() > 0 )
		HttpRequest->SetURL( UHttpLibraryHelpers::AppendQueryString( URL, QueryString ) );
	else
		HttpRequest->SetURL( URL );

	if ( ContentType != EHttpLibraryContentType::Default )
		HttpRequest->SetHeader( "Content-Type", UHttpLibraryHelpers::GetContentType( ContentType ) );

	if ( Headers.Num() > 0 )
		for ( TPair<FString, FString> Header : Headers )
			if ( Header.Key.ToLower() != "content-type" )
				HttpRequest->SetHeader( Header.Key, Header.Value );

	if ( Content.Num() > 0 )
		HttpRequest->SetContent( Content );
	
	return true;
}

bool IHttpLibraryRequest::Create( const FJsonLibraryValue& Content )
{
	return Create( UHttpLibraryHelpers::ConvertJsonToBytes( Content ), EHttpLibraryContentType::JSON );
}

bool IHttpLibraryRequest::Process()
{
	return HttpRequest->ProcessRequest();
}

bool IHttpLibraryRequest::IsRunning() const
{
	if ( !HttpRequest.IsValid() )
		return false;

	EHttpRequestStatus::Type Status = HttpRequest->GetStatus();
	return Status == EHttpRequestStatus::Processing;
}

bool IHttpLibraryRequest::IsComplete() const
{
	if ( !HttpRequest.IsValid() )
		return false;

	EHttpRequestStatus::Type Status = HttpRequest->GetStatus();
	return Status == EHttpRequestStatus::Failed || Status == EHttpRequestStatus::Failed_ConnectionError || Status == EHttpRequestStatus::Succeeded;
}

bool IHttpLibraryRequest::Send()
{
	if ( !Create() )
		return false;

	return Process();
}

bool IHttpLibraryRequest::Send( const FString& Content, EHttpLibraryContentType ContentType /*= EHttpLibraryContentType::Default*/ )
{
	if ( !Create( Content, ContentType ) )
		return false;

	return Process();
}

bool IHttpLibraryRequest::Send( const TArray<uint8>& Content, EHttpLibraryContentType ContentType /*= EHttpLibraryContentType::Default*/ )
{
	if ( !Create( Content, ContentType ) )
		return false;

	return Process();
}

bool IHttpLibraryRequest::Send( const FJsonLibraryValue& Content )
{
	if ( !Create( Content ) )
		return false;

	return Process();
}

void IHttpLibraryRequest::Cancel()
{
	if ( !HttpRequest.IsValid() )
		return;

	if ( HttpRequest->GetStatus() == EHttpRequestStatus::Processing )
		HttpRequest->CancelRequest();

	HttpRequest.Reset();
}

void IHttpLibraryRequest::Reset()
{
	Method = EHttpLibraryRequestMethod::GET;
	URL    = FString();

	QueryString = TMap<FString, FString>();
	Headers     = TMap<FString, FString>();
	
	HttpRequest.Reset();
}
