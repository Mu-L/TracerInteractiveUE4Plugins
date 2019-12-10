// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Http.h"
#include "HttpLibraryEnums.h"
#include "JsonLibrary.h"

typedef TMap<FString, FString> FHttpLibraryHeaders;

DECLARE_DELEGATE_ThreeParams( FHttpLibraryResponse, int32 /*StatusCode*/, EHttpLibraryContentType /*ContentType*/, const FString& /*Content*/ );
DECLARE_DELEGATE_TwoParams( FHttpLibraryProgress, int32 /*Sent*/, int32 /*Received*/ );

DECLARE_DELEGATE_TwoParams( FHttpLibraryJsonResponse, int32 /*StatusCode*/, const FJsonLibraryValue& /*Content*/ );
DECLARE_DELEGATE_FourParams( FHttpLibraryBinaryResponse, int32 /*StatusCode*/, const FHttpLibraryHeaders& /*Headers*/, EHttpLibraryContentType /*ContentType*/, const TArray<uint8>& /*Content*/ );

struct HTTPLIBRARY_API IHttpLibraryRequest
{
	IHttpLibraryRequest();
	virtual ~IHttpLibraryRequest();

	EHttpLibraryRequestMethod Method;
	FString URL;
	
	TMap<FString, FString> Headers;
	TMap<FString, FString> QueryString;

protected:

	TSharedPtr<IHttpRequest> HttpRequest;

	virtual bool Create();
	virtual bool Create( const FString& Content, EHttpLibraryContentType ContentType = EHttpLibraryContentType::Default );
	virtual bool Create( const TArray<uint8>& Content, EHttpLibraryContentType ContentType = EHttpLibraryContentType::Default );
	virtual bool Create( const FJsonLibraryValue& Content );

	virtual bool Process();

public:
	
	bool IsRunning() const;
	bool IsComplete() const;

	bool Send();
	bool Send( const FString& Content, EHttpLibraryContentType ContentType = EHttpLibraryContentType::Default );
	bool Send( const TArray<uint8>& Content, EHttpLibraryContentType ContentType = EHttpLibraryContentType::Default );
	bool Send( const FJsonLibraryValue& Content );

	void Cancel();
	void Reset();
};
