// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "AppleARKitAvailability.h"

#if SUPPORTS_ARKIT_1_0

// ARKit
#include <ARKit/ARKit.h>

@interface FAppleARKitSessionDelegate : NSObject<ARSessionDelegate>
{
}

- (id)initWithAppleARKitSystem:(class FAppleARKitSystem*)InAppleARKitSystem;

#if MATERIAL_CAMERAIMAGE_CONVERSION
- (void)setMetalTextureCache:(CVMetalTextureCacheRef)InMetalTextureCache;
#endif

@end

#endif
