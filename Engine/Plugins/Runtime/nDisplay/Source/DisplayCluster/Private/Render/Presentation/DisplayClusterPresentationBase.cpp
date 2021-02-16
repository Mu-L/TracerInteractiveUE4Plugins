// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Presentation/DisplayClusterPresentationBase.h "
#include "Render/Synchronization/IDisplayClusterRenderSyncPolicy.h"

#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterLog.h"


// Custom VSync interval control
static TAutoConsoleVariable<int32>  CVarVSyncInterval(
	TEXT("nDisplay.render.VSyncInterval"),
	1,
	TEXT("VSync interval"),
	ECVF_RenderThreadSafe
);


FDisplayClusterPresentationBase::FDisplayClusterPresentationBase(FViewport* const InViewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& InSyncPolicy)
	: FRHICustomPresent()
	, Viewport(InViewport)
	, SyncPolicy(InSyncPolicy)
{
}

FDisplayClusterPresentationBase::~FDisplayClusterPresentationBase()
{
}

uint32 FDisplayClusterPresentationBase::GetSwapInt() const
{
	const uint32 SyncInterval = static_cast<uint32>(CVarVSyncInterval.GetValueOnAnyThread());
	return (SyncInterval);
}

void FDisplayClusterPresentationBase::OnBackBufferResize()
{
}

bool FDisplayClusterPresentationBase::Present(int32& InOutSyncInterval)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(nDisplay RenderDevice::PresentationBase);

	bool bNeedPresent = true;

	// Get sync policy instance
	if (SyncPolicy.IsValid())
	{
		// Update sync value with nDisplay value
		InOutSyncInterval = GetSwapInt();
		// False results means we don't need to present current frame, the sync object already presented it
		bNeedPresent = SyncPolicy->SynchronizeClusterRendering(InOutSyncInterval);
	}

	return bNeedPresent;
}
