// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

// AppleARKit
#include "AppleARKitPlaneAnchor.h"
#include "AppleARKitModule.h"
#include "AppleARKitConversion.h"

// UE4
#include "Misc/ScopeLock.h"
#include "Math/UnrealMathUtility.h"

FVector UAppleARKitPlaneAnchor::GetCenter() const
{
	FScopeLock ScopeLock( &UpdateLock );

	return Center;
}

FVector UAppleARKitPlaneAnchor::GetExtent() const
{
	FScopeLock ScopeLock( &UpdateLock );

	return Extent;
}

FTransform UAppleARKitPlaneAnchor::GetTransformToCenter() const
{
	FScopeLock ScopeLock( &UpdateLock );

	return FTransform( Center ) * Transform;
}

#if SUPPORTS_ARKIT_1_0

void UAppleARKitPlaneAnchor::Update_DelegateThread( ARAnchor* Anchor )
{
	Super::Update_DelegateThread( Anchor );

	// Plane anchor?
	if ([Anchor isKindOfClass:[ARPlaneAnchor class]])
	{
		ARPlaneAnchor* PlaneAnchor = (ARPlaneAnchor*)Anchor;

		FScopeLock ScopeLock( &UpdateLock );
		
		// @todo use World Settings WorldToMetersScale
		Extent = FAppleARKitConversion::ToFVector( PlaneAnchor.extent ).GetAbs();
		Center = FAppleARKitConversion::ToFVector( PlaneAnchor.center );
	}
}

#endif
