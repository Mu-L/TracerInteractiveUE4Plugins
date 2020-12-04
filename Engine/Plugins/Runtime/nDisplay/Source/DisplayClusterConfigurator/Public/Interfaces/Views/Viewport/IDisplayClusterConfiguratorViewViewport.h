// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/Views/IDisplayClusterConfiguratorView.h"

class IDisplayClusterConfiguratorPreviewScene;


class IDisplayClusterConfiguratorViewViewport
	: public IDisplayClusterConfiguratorView
{
public:
	virtual TSharedRef<IDisplayClusterConfiguratorPreviewScene> GetPreviewScene() const = 0;
};
