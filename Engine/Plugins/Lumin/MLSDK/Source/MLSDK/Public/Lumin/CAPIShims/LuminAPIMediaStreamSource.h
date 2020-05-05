// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_media_stream_source.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_mediaplayer, MLResult, MLMediaStreamSourceCreate)
#define MLMediaStreamSourceCreate ::MLSDK_API::MLMediaStreamSourceCreateShim
CREATE_FUNCTION_SHIM(ml_mediaplayer, MLResult, MLMediaStreamSourceDestroy)
#define MLMediaStreamSourceDestroy ::MLSDK_API::MLMediaStreamSourceDestroyShim
CREATE_FUNCTION_SHIM(ml_mediaplayer, MLResult, MLMediaStreamSourceGetBuffer)
#define MLMediaStreamSourceGetBuffer ::MLSDK_API::MLMediaStreamSourceGetBufferShim
CREATE_FUNCTION_SHIM(ml_mediaplayer, MLResult, MLMediaStreamSourcePushBuffer)
#define MLMediaStreamSourcePushBuffer ::MLSDK_API::MLMediaStreamSourcePushBufferShim
CREATE_FUNCTION_SHIM(ml_mediaplayer, MLResult, MLMediaStreamSourcePushEOS)
#define MLMediaStreamSourcePushEOS ::MLSDK_API::MLMediaStreamSourcePushEOSShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
