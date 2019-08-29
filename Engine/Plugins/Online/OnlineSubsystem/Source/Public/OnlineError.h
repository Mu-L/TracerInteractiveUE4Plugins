// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace EOnlineServerConnectionStatus {
	enum Type : uint8;
}

/** Generic Error response for OSS calls */
struct ONLINESUBSYSTEM_API FOnlineError
{
public:
	FOnlineError();
	explicit FOnlineError(bool bSucceeded);
	explicit FOnlineError(const FString& ErrorCode);
	explicit FOnlineError(FString&& ErrorCode);
	explicit FOnlineError(const int32 ErrorCode);
	explicit FOnlineError(const FText& ErrorMessage);

	/** Same as the Ctors but can be called any time (does NOT set bSucceeded to false) */
	void SetFromErrorCode(const FString& ErrorCode);
	void SetFromErrorCode(FString&& ErrorCode);
	void SetFromErrorCode(const int32 ErrorCode);
	void SetFromErrorMessage(const FText& ErrorMessage);
	void SetFromErrorMessage(const FText& ErrorMessage, const int32 ErrorCode);

	/** Converts the HttpResult into a EOnlineServerConnectionStatus */
	EOnlineServerConnectionStatus::Type GetConnectionStatusFromHttpResult() const;

	/** Code useful when all you have is raw error info from old APIs */
	static const FString GenericErrorCode;

	/** Call this if you want to log this out (will pick the best string representation) */
	const TCHAR* ToLogString() const;

	/** Was this request successful? */
	bool WasSuccessful() const { return bSucceeded; }

public:
	/** Did the request succeed fully. If this is true the rest of the struct probably doesn't matter */
	bool bSucceeded;

	/** The HTTP response code. Will be 0 if a connection error occurred or if HTTP was not used */
	int32 HttpResult;

	/** The raw unparsed error message from server. Used for pass-through error processing by other systems. */
	FString ErrorRaw;

	/** Intended to be interpreted by code. */
	FString ErrorCode;

	/** Suitable for display to end user. Guaranteed to be in the current locale (or empty) */
	FText ErrorMessage;

	/** Numeric error code provided by the backend expected to correspond to error stored in ErrorCode */
	int32 NumericErrorCode;
};
