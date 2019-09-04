// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*================================================================================
	LinuxPlatformProperties.h - Basic static properties of a platform 
	These are shared between:
		the runtime platform - via FPlatformProperties
		the target platforms - via ITargetPlatform
==================================================================================*/

#pragma once

#include "CoreTypes.h"

// since we include another platform's header, we have to undefine the special define, so that we don't double typedef
#ifdef PROPERTY_HEADER_SHOULD_DEFINE_TYPE
#define SAVED_PROPERTY_HEADER_SHOULD_DEFINE_TYPE
#undef PROPERTY_HEADER_SHOULD_DEFINE_TYPE
#endif

#include "Android/AndroidPlatformProperties.h"


/**
 * Implements Lumin platform properties.
 */
struct FLuminPlatformProperties : public FAndroidPlatformProperties
{
	static FORCEINLINE const char* IniPlatformName( )
	{
		return "Lumin";
	}

	static FORCEINLINE const char* PlatformName( )
	{
		return "Lumin";
	}

	static FORCEINLINE bool SupportsAutoSDK()
	{
		return false;
	}
};

#ifdef SAVED_PROPERTY_HEADER_SHOULD_DEFINE_TYPE
typedef FLuminPlatformProperties FPlatformProperties;
#endif
