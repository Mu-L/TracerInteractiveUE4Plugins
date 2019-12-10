// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#include "HttpLibraryHelpers.h"
#include "Http.h"

FString UHttpLibraryHelpers::GetContentType( EHttpLibraryContentType ContentType )
{
	switch ( ContentType )
	{
		case EHttpLibraryContentType::TXT:   return "text/plain";
		case EHttpLibraryContentType::HTML:  return "text/html";
		case EHttpLibraryContentType::CSS:   return "text/css";
		case EHttpLibraryContentType::CSV:   return "text/csv";
		case EHttpLibraryContentType::JSON:  return "application/json";
		case EHttpLibraryContentType::JS:    return "application/javascript";
		case EHttpLibraryContentType::RTF:   return "application/rtf";
		case EHttpLibraryContentType::XML:   return "application/xml";
		case EHttpLibraryContentType::XHTML: return "application/xhtml+xml";
		case EHttpLibraryContentType::BIN:   return "application/octet-stream";
	}
	
	return FString();
}

EHttpLibraryContentType UHttpLibraryHelpers::FindContentType( const FString& ContentType )
{
	FString LHS;
	FString RHS;
	if ( !ContentType.Contains( ";" ) || !ContentType.Split( ";", &LHS, &RHS ) )
		LHS = ContentType;

	LHS = LHS.ToLower();
	if ( LHS == "text/plain" )
		return EHttpLibraryContentType::TXT;
	if ( LHS == "text/html" )
		return EHttpLibraryContentType::HTML;
	if ( LHS == "text/css" )
		return EHttpLibraryContentType::CSS;
	if ( LHS == "text/csv" )
		return EHttpLibraryContentType::CSV;
	if ( LHS == "application/json" )
		return EHttpLibraryContentType::JSON;
	if ( LHS == "application/javascript" )
		return EHttpLibraryContentType::JS;
	if ( LHS == "application/rtf" )
		return EHttpLibraryContentType::RTF;
	if ( LHS == "application/xml" )
		return EHttpLibraryContentType::XML;
	if ( LHS == "application/xhtml+xml" )
		return EHttpLibraryContentType::XHTML;
	if ( LHS == "application/octet-stream" )
		return EHttpLibraryContentType::BIN;

	return EHttpLibraryContentType::Default;
}

FString UHttpLibraryHelpers::ConvertBytesToString( const TArray<uint8>& Data )
{
	if ( Data.Num() > 0 && Data.Last() == 0 )
		return UTF8_TO_TCHAR( Data.GetData() );

	TArray<uint8> ZeroTerminated( Data );
	ZeroTerminated.Add( 0 );
	return UTF8_TO_TCHAR( ZeroTerminated.GetData() );
}
TArray<uint8> UHttpLibraryHelpers::ConvertStringToBytes( const FString& Data )
{
	TArray<uint8> Payload;

	FTCHARToUTF8 Converter( *Data );
	Payload.SetNum( Converter.Length() );
	FMemory::Memcpy( Payload.GetData(), (uint8*)(ANSICHAR*)Converter.Get(), Payload.Num() );

	return Payload;
}

FJsonLibraryValue UHttpLibraryHelpers::ConvertBytesToJson( const TArray<uint8>& Data )
{
	return FJsonLibraryValue::Parse( ConvertBytesToString( Data ) );
}
TArray<uint8> UHttpLibraryHelpers::ConvertJsonToBytes( const FJsonLibraryValue& Data )
{
	return ConvertStringToBytes( Data.Stringify() );
}

FString UHttpLibraryHelpers::AppendQueryString( const FString& URL, const TMap<FString, FString>& QueryString )
{
	if ( QueryString.Num() <= 0 )
		return URL;

	FString LHS;
	FString RHS;
	if ( !URL.Contains( "?" ) || !URL.Split( "?", &LHS, &RHS ) )
		LHS = URL;

	for ( TPair<FString, FString> Param : QueryString )
	{
		if ( Param.Key.IsEmpty() )
			continue;

		if ( !RHS.IsEmpty() )
			RHS += "&";

		RHS += FPlatformHttp::UrlEncode( Param.Key );
		RHS += "=";

		if ( Param.Value.IsEmpty() )
			continue;
		
		RHS += FPlatformHttp::UrlEncode( Param.Value );
	}

	return LHS + "?" + RHS;
}

bool UHttpLibraryHelpers::IsHttpEnabled()
{
	FHttpModule* Http = &FHttpModule::Get();
	if ( Http )
		return Http->IsHttpEnabled();

	return false;
}

float UHttpLibraryHelpers::GetHttpTimeout()
{
	FHttpModule* Http = &FHttpModule::Get();
	if ( !Http || !Http->IsHttpEnabled() )
		return 0.0f;

	return Http->GetHttpTimeout();
}
void UHttpLibraryHelpers::SetHttpTimeout( float Timeout )
{
	FHttpModule* Http = &FHttpModule::Get();
	if ( !Http || !Http->IsHttpEnabled() )
		return;

	Http->SetHttpTimeout( FMath::Max( 0.0f, Timeout ) );
}

UHttpLibraryRequest* UHttpLibraryHelpers::ConstructHttpRequest( const FHttpLibraryRequestOnResponse& Response )
{
	UHttpLibraryRequest* Object = NewObject<UHttpLibraryRequest>();
	Object->OnResponse = Response;

	return Object;
}

UHttpLibraryRequest* UHttpLibraryHelpers::ConstructHttpRequestWithProgress( const FHttpLibraryRequestOnResponse& Response, const FHttpLibraryRequestOnProgress& Progress )
{
	UHttpLibraryRequest* Object = NewObject<UHttpLibraryRequest>();
	Object->OnResponse = Response;
	Object->OnProgress = Progress;

	return Object;
}

UHttpLibraryJsonRequest* UHttpLibraryHelpers::ConstructHttpJsonRequest( const FHttpLibraryRequestOnJsonResponse& Response )
{
	UHttpLibraryJsonRequest* Object = NewObject<UHttpLibraryJsonRequest>();
	Object->OnResponse = Response;

	return Object;
}

UHttpLibraryJsonRequest* UHttpLibraryHelpers::ConstructHttpJsonRequestWithProgress( const FHttpLibraryRequestOnJsonResponse& Response, const FHttpLibraryRequestOnJsonProgress& Progress )
{
	UHttpLibraryJsonRequest* Object = NewObject<UHttpLibraryJsonRequest>();
	Object->OnResponse = Response;
	Object->OnProgress = Progress;

	return Object;
}

UHttpLibraryBinaryRequest* UHttpLibraryHelpers::ConstructHttpBinaryRequest( const FHttpLibraryRequestOnBinaryResponse& Response )
{
	UHttpLibraryBinaryRequest* Object = NewObject<UHttpLibraryBinaryRequest>();
	Object->OnResponse = Response;

	return Object;
}

UHttpLibraryBinaryRequest* UHttpLibraryHelpers::ConstructHttpBinaryRequestWithProgress( const FHttpLibraryRequestOnBinaryResponse& Response, const FHttpLibraryRequestOnBinaryProgress& Progress )
{
	UHttpLibraryBinaryRequest* Object = NewObject<UHttpLibraryBinaryRequest>();
	Object->OnResponse = Response;
	Object->OnProgress = Progress;

	return Object;
}
