// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IStylusInputModule.h"

class FWindowsStylusInputInterfaceImpl;

class FWindowsStylusInputInterface : public IStylusInputInterfaceInternal
{
public:
	FWindowsStylusInputInterface(FWindowsStylusInputInterfaceImpl* InImpl);
	virtual ~FWindowsStylusInputInterface();

	virtual int32 NumInputDevices() const override;
	virtual IStylusInputDevice* GetInputDevice(int32 Index) const override;

private:
	// pImpl to avoid including Windows headers.
	FWindowsStylusInputInterfaceImpl* Impl;
	TArray<IStylusMessageHandler*> MessageHandlers;
};
