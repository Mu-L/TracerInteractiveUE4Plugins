// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyBase.h"

#include "Cluster/IPDisplayClusterClusterManager.h"
#include "Cluster/Controller/IDisplayClusterNodeController.h"

#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterLog.h"


void FDisplayClusterRenderSyncPolicyBase::SyncBarrierRenderThread()
{
	if (GDisplayCluster->GetOperationMode() == EDisplayClusterOperationMode::Disabled)
	{
		return;
	}

	double ThreadTime  = 0.f;
	double BarrierTime = 0.f;

	IDisplayClusterNodeController* const pController = GDisplayCluster->GetPrivateClusterMgr()->GetController();
	if (pController)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(nDisplay SyncPolicyBase::SyncBarrier);
		pController->WaitForSwapSync(&ThreadTime, &BarrierTime);
	}

	UE_LOG(LogDisplayClusterRenderSync, VeryVerbose, TEXT("Render barrier wait: t=%lf b=%lf"), ThreadTime, BarrierTime);
}
