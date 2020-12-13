// Copyright 2021 Tracer Interactive, LLC. All Rights Reserved.
#include "WebInterfaceSchemeHandler.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HAL/FileManager.h"

#define BIN "application/octet-stream"
FWebInterfaceSchemeHandler::FWebInterfaceSchemeHandler()
	: MimeType( BIN )
	, ContentLength( 0 )
	, TotalBytesRead( 0 )
	, Reader( nullptr )
{
}

FWebInterfaceSchemeHandler::~FWebInterfaceSchemeHandler()
{
	IWebBrowserSchemeHandler::~IWebBrowserSchemeHandler();
	CloseReader();
}

bool FWebInterfaceSchemeHandler::ProcessRequest( const FString& Verb, const FString& Url, const FSimpleDelegate& OnHeadersReady )
{
	if ( Verb.ToUpper() != "GET" )
		return false;

	FString FilePath = Url;
	if ( FilePath.Contains( "://" ) )
	{
		FString Scheme;
		if ( FParse::SchemeNameFromURI( *FilePath, Scheme ) )
			FilePath = FilePath.RightChop( Scheme.Len() + 3 );
	}

	FilePath = FPaths::ProjectContentDir() + FilePath;
	FilePath = FilePath.Replace( TEXT( "\\" ), TEXT( "/" ) );
	FilePath = FilePath.Replace( TEXT( "//" ), TEXT( "/" ) );

	const int64 FileSize = IFileManager::Get().FileSize( *FilePath );
	if ( FileSize != INDEX_NONE )
	{
		ContentLength = (int32)FileSize;
		if ( FileSize > INT32_MAX )
			return false;
	
		MimeType = FGenericPlatformHttp::GetMimeType( FilePath );
		if ( MimeType.Len() == 0 || MimeType == "application/unknown" )
			MimeType = BIN;

		CreateReader( *FilePath );
	}
	
	OnHeadersReady.Execute();
	return true;
}

void FWebInterfaceSchemeHandler::GetResponseHeaders( IHeaders& OutHeaders )
{
	if ( Reader )
	{
		OutHeaders.SetStatusCode( 200 );
		OutHeaders.SetMimeType( *MimeType );
		OutHeaders.SetContentLength( ContentLength );
	}
	else
		OutHeaders.SetStatusCode( 404 );
}

bool FWebInterfaceSchemeHandler::ReadResponse( uint8* OutBytes, int32 BytesToRead, int32& BytesRead, const FSimpleDelegate& OnMoreDataReady )
{
	BytesRead = 0;
	if ( !Reader )
		return false;

	BytesRead = ContentLength - TotalBytesRead;
	if ( BytesRead <= 0 )
		return false;

	if ( BytesRead > BytesToRead )
		BytesRead = BytesToRead;

	Reader->Serialize( OutBytes, BytesRead );
	TotalBytesRead += BytesRead;

	if ( TotalBytesRead < ContentLength )
		OnMoreDataReady.Execute();
	else
		CloseReader();
	
	return true;
}

void FWebInterfaceSchemeHandler::Cancel()
{
	ContentLength  = 0;
	TotalBytesRead = 0;

	CloseReader();
}

void FWebInterfaceSchemeHandler::CreateReader( const FString& FilePath )
{
	Reader = IFileManager::Get().CreateFileReader( *FilePath );
}

void FWebInterfaceSchemeHandler::CloseReader()
{
	if ( !Reader )
		return;

	Reader->Close();

	delete Reader;
	Reader = nullptr;
}


TUniquePtr<IWebBrowserSchemeHandler> FWebInterfaceSchemeHandlerFactory::Create( FString Verb, FString Url )
{
	return MakeUnique<FWebInterfaceSchemeHandler>();
}
