// Copyright 2021 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#if !UE_SERVER
#include "IWebBrowserSchemeHandler.h"

class WEBUI_API FWebInterfaceSchemeHandler : public IWebBrowserSchemeHandler
{
public:
	FWebInterfaceSchemeHandler();
	virtual ~FWebInterfaceSchemeHandler();
	
	virtual bool ProcessRequest( const FString& Verb, const FString& Url, const FSimpleDelegate& OnHeadersReady ) override;
	virtual void GetResponseHeaders( IHeaders& OutHeaders ) override;
	virtual bool ReadResponse( uint8* OutBytes, int32 BytesToRead, int32& BytesRead, const FSimpleDelegate& OnMoreDataReady ) override;
	virtual void Cancel() override;

protected:
	FString MimeType;

	int32 ContentLength;
	int32 TotalBytesRead;

	FArchive* Reader;

	void CreateReader( const FString& FilePath );
	void CloseReader();
};

class WEBUI_API FWebInterfaceSchemeHandlerFactory : public IWebBrowserSchemeHandlerFactory
{
	virtual TUniquePtr<IWebBrowserSchemeHandler> Create( FString Verb, FString Url ) override;
};
#endif
