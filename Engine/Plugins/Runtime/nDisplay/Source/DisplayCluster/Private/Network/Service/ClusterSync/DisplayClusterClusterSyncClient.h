// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Network/DisplayClusterClient.h"
#include "Network/DisplayClusterMessage.h"
#include "Network/Protocol/IPDisplayClusterClusterSyncProtocol.h"


/**
 * Cluster synchronization client
 */
class FDisplayClusterClusterSyncClient
	: public FDisplayClusterClient
	, public IPDisplayClusterClusterSyncProtocol
{
public:
	FDisplayClusterClusterSyncClient();
	FDisplayClusterClusterSyncClient(const FString& InName);

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPDisplayClusterClusterSyncProtocol
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void WaitForGameStart() override;
	virtual void WaitForFrameStart() override;
	virtual void WaitForFrameEnd() override;
	virtual void WaitForTickEnd() override;
	virtual void GetDeltaTime(float& DeltaSeconds) override;
	virtual void GetFrameTime(TOptional<FQualifiedFrameTime>& FrameTime) override;
	virtual void GetSyncData(FDisplayClusterMessage::DataType& SyncData, EDisplayClusterSyncGroup SyncGroup) override;
	virtual void GetInputData(FDisplayClusterMessage::DataType& InputData) override;
	virtual void GetEventsData(FDisplayClusterMessage::DataType& EventsData) override;
	virtual void GetNativeInputData(FDisplayClusterMessage::DataType& NativeInputData) override;
};
