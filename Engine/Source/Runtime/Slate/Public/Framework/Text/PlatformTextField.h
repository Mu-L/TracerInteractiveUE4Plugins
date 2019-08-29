// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if PLATFORM_IOS
	#include "IOS/IOSPlatformTextField.h"
#elif PLATFORM_ANDROID && USE_ANDROID_JNI
	#include "Android/AndroidPlatformTextField.h"
#elif PLATFORM_PS4
	#include "PS4/PS4PlatformTextField.h"
#elif PLATFORM_XBOXONE
	#include "XboxOne/XboxOnePlatformTextField.h"
#elif PLATFORM_SWITCH
	#include "Switch/SwitchPlatformTextField.h"
#else
	#include "Framework/Text/GenericPlatformTextField.h"
#endif
