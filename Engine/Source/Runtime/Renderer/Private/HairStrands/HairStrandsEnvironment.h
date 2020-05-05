// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsEnvironment.h: Hair strands environment lighting.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "RenderGraphResources.h"

void RenderHairStrandsAmbientOcclusion(
	FRHICommandListImmediate& RHICmdList,
	const TArray<FViewInfo>& Views,
	const struct FHairStrandsDatas* HairDatas,
	const TRefCountPtr<IPooledRenderTarget>& InAOTexture);

void RenderHairStrandsEnvironmentLighting(
	FRDGBuilder& GraphBuilder,
	const uint32 ViewIndex,
	const TArray<FViewInfo>& Views,
	struct FHairStrandsDatas* HairDatas);

void RenderHairStrandsSceneColorScattering(
	FRHICommandListImmediate& RHICmdList,
	const TArray<FViewInfo>& Views,
	struct FHairStrandsDatas* HairDatas);
