// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Delegates/Delegate.h"
#include "Lumin/LuminPlatformFile.h"

class CORE_API FLuminDelegates
{
public:
	DECLARE_MULTICAST_DELEGATE_TwoParams(FLuminAppStartupArgumentsDelegate, const TArray<FString>&, const TArray<FLuminFileInfo>&);

	// Called with arguments passed to the application on statup, perhaps meta data passed on by another application which launched this one.
	// TODO: make this private
	static FLuminAppStartupArgumentsDelegate LuminAppReceivedStartupArgumentsDelegate;

	DECLARE_MULTICAST_DELEGATE(FLuminApplicationLifetimeDelegate);

	static FLuminApplicationLifetimeDelegate DeviceHasReactivatedDelegate;
	static FLuminApplicationLifetimeDelegate DeviceWillEnterRealityModeDelegate;
	static FLuminApplicationLifetimeDelegate DeviceWillGoInStandbyDelegate;
};
