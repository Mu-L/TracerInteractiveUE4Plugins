// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsPlatformTime.h"
#elif PLATFORM_PS4
#include "PS4/PS4Time.h"
#elif PLATFORM_XBOXONE
#include "XboxOne/XboxOneTime.h"
#elif PLATFORM_MAC
#include "Apple/ApplePlatformTime.h"
#elif PLATFORM_IOS
#include "Apple/ApplePlatformTime.h"
#elif PLATFORM_ANDROID
#include "Android/AndroidTime.h"
#elif PLATFORM_HTML5
#include "HTML5/HTML5PlatformTime.h"
#elif PLATFORM_UNIX
#include "Unix/UnixPlatformTime.h"
#elif PLATFORM_SWITCH
#include "Switch/SwitchPlatformTime.h"
#else
#error Unknown platform
#endif
