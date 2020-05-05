// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHI.h"
#include "RHIResources.h"

/**
 * Interface class to implement custom sample conversion
 */
class IMediaTextureSampleConverter
{
public:
	virtual void Convert(FTexture2DRHIRef InDstTexture) = 0;
};
