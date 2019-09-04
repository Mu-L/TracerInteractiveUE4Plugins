// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// AppleARKit
#include "AppleARKitHitTestResult.h"
#include "AppleARKitConversion.h"

#if SUPPORTS_ARKIT_1_0

/** Conversion function from ARKit native ARHitTestResultType */
EAppleARKitHitTestResultType ToEAppleARKitHitTestResultType(ARHitTestResultType InTypes)
{
	EAppleARKitHitTestResultType Types = EAppleARKitHitTestResultType::None;

	if (!!(InTypes & ARHitTestResultTypeFeaturePoint))
	{
		Types |= EAppleARKitHitTestResultType::FeaturePoint;
	}

	if (!!(InTypes & ARHitTestResultTypeEstimatedHorizontalPlane))
	{
		Types |= EAppleARKitHitTestResultType::EstimatedHorizontalPlane;
	}

	if (!!(InTypes & ARHitTestResultTypeExistingPlane))
	{
		Types |= EAppleARKitHitTestResultType::ExistingPlane;
	}

    if (!!(InTypes & ARHitTestResultTypeExistingPlaneUsingExtent))
    {
        Types |= EAppleARKitHitTestResultType::ExistingPlaneUsingExtent;
    }
    
	return Types;
}

/** Conversion function to ARKit native ARHitTestResultType */
ARHitTestResultType ToARHitTestResultType(EAppleARKitHitTestResultType InTypes)
{
	ARHitTestResultType Types = 0;

	if (!!(InTypes & EAppleARKitHitTestResultType::FeaturePoint))
	{
		Types |= ARHitTestResultTypeFeaturePoint;
	}

	if (!!(InTypes & EAppleARKitHitTestResultType::EstimatedHorizontalPlane))
	{
		Types |= ARHitTestResultTypeEstimatedHorizontalPlane;
	}

	if (!!(InTypes & EAppleARKitHitTestResultType::ExistingPlane))
	{
		Types |= ARHitTestResultTypeExistingPlane;
	}

    if (!!(InTypes & EAppleARKitHitTestResultType::ExistingPlaneUsingExtent))
    {
        Types |= ARHitTestResultTypeExistingPlaneUsingExtent;
    }
    
	return Types;
}

#endif
