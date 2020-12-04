// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CoreTypes.h"
#include "Engine/EngineTypes.h"
#include "Containers/UnrealString.h"
#include "Logging/LogMacros.h"

#if (defined(PLATFORM_WINDOWS) || defined(PLATFORM_HOLOLENS))
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"

#if PLATFORM_DESKTOP
    #if PLATFORM_64BITS
        #define TARGET_ARCH TEXT("x64")
    #else
        #define TARGET_ARCH TEXT("x86")
    #endif
#else //HoloLens
    #if PLATFORM_CPU_ARM_FAMILY
        #if (defined(__aarch64__) || defined(_M_ARM64))
            #define TARGET_ARCH TEXT("arm64")
        #else
            #define TARGET_ARCH TEXT("arm")
        #endif
    #else
        #if PLATFORM_64BITS
            #define TARGET_ARCH TEXT("x64")
        #else
            #define TARGET_ARCH TEXT("x86")
        #endif
    #endif    
#endif

#endif

THIRD_PARTY_INCLUDES_START
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include "HLMediaLibrary/inc/HLMediaLibrary.h"
THIRD_PARTY_INCLUDES_END

#include "Windows/COMPointer.h"

#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
#include "Windows/HideWindowsPlatformTypes.h"
#endif

// General Log
DECLARE_LOG_CATEGORY_EXTERN(LogHLMediaModule, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogHLMediaPlayer, Log, All);
