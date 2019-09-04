// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "GlobalShader.h"

struct FGenerateMipsStruct;

class RENDERCORE_API FGenerateMips
{
public:
	//Public function for executing the generate mips compute shader 
	//Default sampler is always bilinear clamp
	static void Execute(FRHICommandListImmediate& RHICmdList, FRHITexture* InTexture,
		ESamplerFilter InFilter = SF_Bilinear,
		ESamplerAddressMode InAddressU = AM_Clamp,
		ESamplerAddressMode InAddressV = AM_Clamp,
		ESamplerAddressMode InAddressW = AM_Clamp);

private:
	static void Compute(FRHICommandListImmediate& RHIImmCmdList, FRHITexture* InTexture);
	static FGenerateMipsStruct* SetupTexture(FRHITexture* InTexture,
		ESamplerFilter InFilter = SF_Bilinear,
		ESamplerAddressMode InAddressU = AM_Clamp,
		ESamplerAddressMode InAddressV = AM_Clamp,
		ESamplerAddressMode InAddressW = AM_Clamp);
};