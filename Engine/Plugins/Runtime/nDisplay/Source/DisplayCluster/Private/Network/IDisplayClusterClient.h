// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


/**
 * DisplayCluster TCP client interface
 */
class IDisplayClusterClient
{
public:
	virtual ~IDisplayClusterClient() = 0
	{ }

public:
	// Connects to a server
	virtual bool Connect(const FString& Address, const int32 Port, const int32 ConnectTriesAmount, const float ConnectRetryDelay) = 0;
	// Terminates current connection
	virtual void Disconnect() = 0;
	// Returns connection status
	virtual bool IsConnected() const = 0;
	// Returns client name
	virtual FString GetName() const = 0;
};
