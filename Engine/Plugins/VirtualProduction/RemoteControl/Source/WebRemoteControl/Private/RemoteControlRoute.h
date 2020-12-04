// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HttpResultCallback.h"
#include "HttpServerResponse.h"
#include "HttpServerRequest.h"
#include "HttpPath.h"

#include "RemoteControlRoute.generated.h"

struct FRemoteControlWebSocketMessage
{
	FString MessageName;
	int32 MessageId = -1;
	FGuid ClientId;
	TArrayView<uint8> RequestPayload;
};

DECLARE_DELEGATE_RetVal_TwoParams(bool, FRequestHandlerDelegate, const FHttpServerRequest&, const FHttpResultCallback&);
DECLARE_DELEGATE_OneParam(FWebSocketMessageDelegate, const FRemoteControlWebSocketMessage& /** Message */);

struct FRemoteControlRoute
{
	FRemoteControlRoute(FString InRouteDescription, FHttpPath InPath, EHttpServerRequestVerbs InVerb, FRequestHandlerDelegate InHandler)
		: RouteDescription(MoveTemp(InRouteDescription))
		, Path(MoveTemp(InPath))
		, Verb(InVerb)
		, Handler(MoveTemp(InHandler))
	{
	}
	/** A description of how the route should be used. */
	FString RouteDescription;
	/** Relative path (ie. /remote/object) */
	FHttpPath Path;
	/** The desired HTTP verb (ie. GET, PUT..) */
	EHttpServerRequestVerbs Verb;
	/** The handler called when the route is accessed. */
	FRequestHandlerDelegate Handler;

	friend uint32 GetTypeHash(const FRemoteControlRoute& Route) { return HashCombine(GetTypeHash(Route.Path), GetTypeHash(Route.Verb)); }
	friend bool operator==(const FRemoteControlRoute& LHS, const FRemoteControlRoute& RHS) { return LHS.Path == RHS.Path && LHS.Verb == RHS.Verb; }
};

struct FRemoteControlWebsocketRoute
{
	FRemoteControlWebsocketRoute(const FString& InRouteDescription, const FString& InMessageName, const FWebSocketMessageDelegate& InDelegate)
		: RouteDescription(InRouteDescription)
		, MessageName(InMessageName)
		, Delegate(InDelegate)
	{}
	
	/** A description of how the route should be used. */
	FString RouteDescription;
	/**  The message handled by this route. */
	FString MessageName;
	/** The handler called when the route is accessed. */
	FWebSocketMessageDelegate Delegate;

	friend uint32 GetTypeHash(const FRemoteControlWebsocketRoute& Route) { return GetTypeHash(Route.MessageName); }
	friend bool operator==(const FRemoteControlWebsocketRoute& LHS, const FRemoteControlWebsocketRoute& RHS) { return LHS.MessageName == RHS.MessageName; }
};

UENUM()
enum class ERemoteControlHttpVerbs : uint16
{
	None = 0,
	Get = 1 << 0,
	Post = 1 << 1,
	Put = 1 << 2,
	Patch = 1 << 3,
	Delete = 1 << 4,
	Options = 1 << 5
};

/**
 * Utility struct to create a textual representation of an http route.
 */
USTRUCT()
struct FRemoteControlRouteDescription
{
	GENERATED_BODY()

	FRemoteControlRouteDescription() = default;

	FRemoteControlRouteDescription(const FRemoteControlRoute& Route)
		: Path(Route.Path.GetPath())
		, Verb((ERemoteControlHttpVerbs)Route.Verb)
		, Description(Route.RouteDescription)
	{}

	UPROPERTY(EditAnywhere, Category = Test)
	FString Path;

	UPROPERTY()
	ERemoteControlHttpVerbs Verb;

	UPROPERTY()
	FString Description;
};

