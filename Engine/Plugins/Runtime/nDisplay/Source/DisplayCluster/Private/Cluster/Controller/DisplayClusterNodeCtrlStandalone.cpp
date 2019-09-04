// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Cluster/Controller/DisplayClusterNodeCtrlStandalone.h"


FDisplayClusterNodeCtrlStandalone::FDisplayClusterNodeCtrlStandalone(const FString& ctrlName, const FString& nodeName) :
	FDisplayClusterNodeCtrlBase(ctrlName, nodeName)
{
}


FDisplayClusterNodeCtrlStandalone::~FDisplayClusterNodeCtrlStandalone()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayClusterClusterSyncProtocol
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterNodeCtrlStandalone::WaitForGameStart()
{
	// Nothing special to do here in standalone mode
}

void FDisplayClusterNodeCtrlStandalone::WaitForFrameStart()
{
	// Nothing special to do here in standalone mode
}

void FDisplayClusterNodeCtrlStandalone::WaitForFrameEnd()
{
	// Nothing special to do here in standalone mode
}

void FDisplayClusterNodeCtrlStandalone::WaitForTickEnd()
{
	// Nothing special to do here in standalone mode
}

void FDisplayClusterNodeCtrlStandalone::GetDeltaTime(float& deltaTime)
{
	// Nothing special to do here in standalone mode
}

void FDisplayClusterNodeCtrlStandalone::GetTimecode(FTimecode& timecode, FFrameRate& frameRate)
{
	// Nothing special to do here in standalone mode
}

void FDisplayClusterNodeCtrlStandalone::GetSyncData(FDisplayClusterMessage::DataType& data)
{
	// Nothing special to do here in standalone mode
}

void FDisplayClusterNodeCtrlStandalone::GetInputData(FDisplayClusterMessage::DataType& data)
{
	// Nothing special to do here in standalone mode
}

void FDisplayClusterNodeCtrlStandalone::GetEventsData(FDisplayClusterMessage::DataType& data)
{
	// Nothing special to do here in standalone mode
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayClusterSwapSyncProtocol
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterNodeCtrlStandalone::WaitForSwapSync(double* pThreadWaitTime, double* pBarrierWaitTime)
{
	// Nothing special to do here in standalone mode
}

//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayClusterClusterEventsProtocol
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterNodeCtrlStandalone::EmitClusterEvent(const FDisplayClusterClusterEvent& Event)
{
	// Nothing special to do here in standalone mode
}
