// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	EmptyRenderTarget.cpp: Empty render target implementation.
=============================================================================*/

#include "EmptyRHIPrivate.h"
#include "ScreenRendering.h"


void FEmptyDynamicRHI::RHICopyToResolveTarget(FRHITexture* SourceTextureRHI, FRHITexture* DestTextureRHI, const FResolveParams& ResolveParams)
{

}

void FEmptyDynamicRHI::RHIReadSurfaceData(FRHITexture* TextureRHI, FIntRect Rect, TArray<FColor>& OutData, FReadSurfaceDataFlags InFlags)
{

}

void FEmptyDynamicRHI::RHIMapStagingSurface(FRHITexture* TextureRHI,void*& OutData,int32& OutWidth,int32& OutHeight)
{

}

void FEmptyDynamicRHI::RHIUnmapStagingSurface(FRHITexture* TextureRHI)
{

}

void FEmptyDynamicRHI::RHIReadSurfaceFloatData(FRHITexture* TextureRHI, FIntRect Rect, TArray<FFloat16Color>& OutData, ECubeFace CubeFace,int32 ArrayIndex,int32 MipIndex)
{

}

void FEmptyDynamicRHI::RHIRead3DSurfaceFloatData(FRHITexture* TextureRHI,FIntRect InRect,FIntPoint ZMinMax,TArray<FFloat16Color>& OutData)
{

}
