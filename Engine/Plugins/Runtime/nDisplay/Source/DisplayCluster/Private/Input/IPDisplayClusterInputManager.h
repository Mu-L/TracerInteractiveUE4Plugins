// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Input/IDisplayClusterInputManager.h"
#include "Network/DisplayClusterMessage.h"

#include "IPDisplayClusterManager.h"


/**
 * Input manager private interface
 */
class IPDisplayClusterInputManager
	: public IDisplayClusterInputManager
	, public IPDisplayClusterManager
{
public:
	virtual ~IPDisplayClusterInputManager()
	{ }

	virtual void Update() = 0;

	virtual void ExportInputData(FDisplayClusterMessage::DataType& InputData) const = 0;
	virtual void ImportInputData(const FDisplayClusterMessage::DataType& InputData) = 0;
};
