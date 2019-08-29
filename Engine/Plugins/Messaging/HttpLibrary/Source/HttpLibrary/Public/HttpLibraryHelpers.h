// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HttpLibraryEnums.h"
#include "HttpLibraryRequest.h"
#include "HttpLibraryBinaryRequest.h"
#include "HttpLibraryJsonRequest.h"
#include "JsonLibrary.h"
#include "HttpLibraryHelpers.generated.h"

UCLASS()
class HTTPLIBRARY_API UHttpLibraryHelpers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Get the content type string.
	UFUNCTION(BlueprintPure, Category = "HTTP Library|Helpers")
	static FString GetContentType( EHttpLibraryContentType ContentType );
	// Find the content type from a string.
	UFUNCTION(BlueprintPure, Category = "HTTP Library|Helpers")
	static EHttpLibraryContentType FindContentType( const FString& ContentType );
	
	// Append a query string to a URL.
	UFUNCTION(BlueprintCallable, Category = "HTTP Library|Helpers", meta = (AutoCreateRefTerm = "QueryString"))
	static FString AppendQueryString( const FString& URL, const TMap<FString, FString>& QueryString );

	// Convert an array of bytes to a string.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Convert Bytes to String"), Category = "HTTP Library|Helpers", meta = (AutoCreateRefTerm = "Data"))
	static FString ConvertBytesToString( const TArray<uint8>& Data );
	// Convert a string to an array of bytes.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Convert String to Bytes"), Category = "HTTP Library|Helpers")
	static TArray<uint8> ConvertStringToBytes( const FString& Data );
	// Convert an array of bytes to a string.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Convert Bytes to JSON"), Category = "HTTP Library|Helpers", meta = (AutoCreateRefTerm = "Data"))
	static FJsonLibraryValue ConvertBytesToJson( const TArray<uint8>& Data );
	// Convert a string to an array of bytes.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Convert JSON to Bytes"), Category = "HTTP Library|Helpers")
	static TArray<uint8> ConvertJsonToBytes( const FJsonLibraryValue& Data );
	
	// Check if HTTP is enabled.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Is HTTP Enabled"), Category = "HTTP Library|Settings")
	static bool IsHttpEnabled();
	// Get the default HTTP timeout.
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Get HTTP Timeout"), Category = "HTTP Library|Settings")
	static float GetHttpTimeout();
	// Set the default HTTP timeout.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set HTTP Timeout"), Category = "HTTP Library|Settings")
	static void SetHttpTimeout( float Timeout );

protected:
	
	// Construct a HTTP request object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Construct HTTP", CompactNodeTitle = "HTTP", AutoCreateRefTerm = "Response"), Category = "HTTP Library|Request")
	static UHttpLibraryRequest* ConstructHttpRequest( const FHttpLibraryRequestOnResponse& Response );
	// Construct a HTTP request object with progress updates.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Construct HTTP w/ Progress", AutoCreateRefTerm = "Response,Progress"), Category = "HTTP Library|Request|Progress")
	static UHttpLibraryRequest* ConstructHttpRequestWithProgress( const FHttpLibraryRequestOnResponse& Response, const FHttpLibraryRequestOnProgress& Progress );
	
	// Construct a HTTP JSON request object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Construct HTTP (JSON)", CompactNodeTitle = "HTTP", AutoCreateRefTerm = "Response"), Category = "HTTP Library|Request")
	static UHttpLibraryJsonRequest* ConstructHttpJsonRequest( const FHttpLibraryRequestOnJsonResponse& Response );
	// Construct a HTTP JSON request object with progress updates.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Construct HTTP w/ Progress (JSON)", AutoCreateRefTerm = "Response,Progress"), Category = "HTTP Library|Request|Progress")
	static UHttpLibraryJsonRequest* ConstructHttpJsonRequestWithProgress( const FHttpLibraryRequestOnJsonResponse& Response, const FHttpLibraryRequestOnJsonProgress& Progress );

	// Construct a HTTP binary request object.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Construct HTTP (Binary)", CompactNodeTitle = "HTTP", AutoCreateRefTerm = "Response"), Category = "HTTP Library|Request")
	static UHttpLibraryBinaryRequest* ConstructHttpBinaryRequest( const FHttpLibraryRequestOnBinaryResponse& Response );
	// Construct a HTTP binary request object with progress updates.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Construct HTTP w/ Progress (Binary)", AutoCreateRefTerm = "Response,Progress"), Category = "HTTP Library|Request|Progress")
	static UHttpLibraryBinaryRequest* ConstructHttpBinaryRequestWithProgress( const FHttpLibraryRequestOnBinaryResponse& Response, const FHttpLibraryRequestOnBinaryProgress& Progress );
};
