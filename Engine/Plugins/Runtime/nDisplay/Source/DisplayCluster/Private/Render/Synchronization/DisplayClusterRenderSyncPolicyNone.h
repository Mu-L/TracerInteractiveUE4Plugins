// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyBase.h"


/**
 * Synchronization policy - None (no synchronization)
 */
class FDisplayClusterRenderSyncPolicyNone
	: public FDisplayClusterRenderSyncPolicyBase
{
public:
	FDisplayClusterRenderSyncPolicyNone(const TMap<FString, FString>& Parameters);
	virtual ~FDisplayClusterRenderSyncPolicyNone();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterRenderSyncPolicy
	//////////////////////////////////////////////////////////////////////////////////////////////
	bool SynchronizeClusterRendering(int32& InOutSyncInterval) override;
};
