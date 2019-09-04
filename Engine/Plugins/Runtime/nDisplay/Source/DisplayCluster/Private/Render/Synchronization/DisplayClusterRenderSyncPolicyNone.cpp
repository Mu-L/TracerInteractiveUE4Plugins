// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyNone.h"



FDisplayClusterRenderSyncPolicyNone::FDisplayClusterRenderSyncPolicyNone()
{
}

FDisplayClusterRenderSyncPolicyNone::~FDisplayClusterRenderSyncPolicyNone()
{
}

bool FDisplayClusterRenderSyncPolicyNone::SynchronizeClusterRendering(int32& InOutSyncInterval)
{
	// Override sync interval with 0 to show a frame ASAP. We don't care about tearing in this policy.
	InOutSyncInterval = 0;
	// Tell a caller that he still needs to present a frame
	return true;
}
