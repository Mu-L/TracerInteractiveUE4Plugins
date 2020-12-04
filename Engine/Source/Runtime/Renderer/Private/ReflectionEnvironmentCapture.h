// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Functionality for capturing the scene into reflection capture cubemaps, and prefiltering
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Math/SHMath.h"
#include "RHI.h"
#include "GlobalShader.h"

extern void ComputeDiffuseIrradiance(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FTextureRHIRef LightingSource, int32 LightingSourceMipIndex, FSHVectorRGB3* OutIrradianceEnvironmentMap);

FMatrix CalcCubeFaceViewRotationMatrix(ECubeFace Face);
FMatrix GetCubeProjectionMatrix(float HalfFovDeg, float CubeMapSize, float NearPlane);

/** Pixel shader used for filtering a mip. */
class FCubeFilterPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FCubeFilterPS, Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	FCubeFilterPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		CubeFace.Bind(Initializer.ParameterMap, TEXT("CubeFace"));
		MipIndex.Bind(Initializer.ParameterMap, TEXT("MipIndex"));
		NumMips.Bind(Initializer.ParameterMap, TEXT("NumMips"));
		SourceCubemapTexture.Bind(Initializer.ParameterMap, TEXT("SourceCubemapTexture"));
		SourceCubemapSampler.Bind(Initializer.ParameterMap, TEXT("SourceCubemapSampler"));
	}
	FCubeFilterPS() {}

	LAYOUT_FIELD(FShaderParameter, CubeFace);
	LAYOUT_FIELD(FShaderParameter, MipIndex);
	LAYOUT_FIELD(FShaderParameter, NumMips);
	LAYOUT_FIELD(FShaderResourceParameter, SourceCubemapTexture);
	LAYOUT_FIELD(FShaderResourceParameter, SourceCubemapSampler);
};

template< uint32 bNormalize >
class TCubeFilterPS : public FCubeFilterPS
{
	DECLARE_SHADER_TYPE(TCubeFilterPS, Global);
public:
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FCubeFilterPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("NORMALIZE"), bNormalize);
	}

	TCubeFilterPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FCubeFilterPS(Initializer)
	{}

	TCubeFilterPS() {}
};