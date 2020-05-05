// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Templates/ValueOrError.h"
#include "IGoogleVRControllerPlugin.h"

#if GOOGLEVRCONTROLLER_SUPPORTED_PLATFORMS

THIRD_PARTY_INCLUDES_START

#if GOOGLEVRCONTROLLER_SUPPORTED_ANDROID_PLATFORMS
#include "gvr_controller.h"
#endif

#if GOOGLEVRCONTROLLER_SUPPORTED_EMULATOR_PLATFORMS
#include "gvr_controller_emulator.h"
#endif

#if GOOGLEVRCONTROLLER_SUPPORTED_INSTANT_PREVIEW_PLATFORMS
// TODO: include ip_shared.h/ip_server.h
#endif

THIRD_PARTY_INCLUDES_END

#endif // GOOGLEVRCONTROLLER_SUPPORTED_ANDROID_PLATFORMS