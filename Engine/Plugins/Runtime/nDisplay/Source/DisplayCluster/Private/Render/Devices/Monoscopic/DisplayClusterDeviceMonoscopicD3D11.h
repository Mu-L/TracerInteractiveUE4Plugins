// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Devices/QuadBufferStereo/DisplayClusterDeviceQuadBufferStereoD3D11.h"
#include "Render/Devices/DisplayClusterDeviceInternals.h"


/**
 * Monoscopic emulation device (DirectX 11)
 */
class FDisplayClusterDeviceMonoscopicD3D11 : public FDisplayClusterDeviceQuadBufferStereoD3D11
{
public:
	FDisplayClusterDeviceMonoscopicD3D11();
	virtual ~FDisplayClusterDeviceMonoscopicD3D11();

	virtual bool ShouldUseSeparateRenderTarget() const override { return false; };
protected:
	virtual bool Present(int32& InOutSyncInterval) override;
};
