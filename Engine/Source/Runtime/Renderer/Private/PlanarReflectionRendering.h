// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
PlanarReflectionRendering.h: shared planar reflection rendering declarations.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameters.h"

class FShaderParameterMap;
class FSceneView;

/** Parameters needed for planar reflections, shared by multiple shaders. */
class FPlanarReflectionParameters
{
public:

	void Bind(const FShaderParameterMap& ParameterMap)
	{
		ReflectionPlane.Bind(ParameterMap, TEXT("ReflectionPlane"));
		PlanarReflectionOrigin.Bind(ParameterMap, TEXT("PlanarReflectionOrigin"));
		PlanarReflectionXAxis.Bind(ParameterMap, TEXT("PlanarReflectionXAxis"));
		PlanarReflectionYAxis.Bind(ParameterMap, TEXT("PlanarReflectionYAxis"));
		InverseTransposeMirrorMatrix.Bind(ParameterMap, TEXT("InverseTransposeMirrorMatrix"));
		PlanarReflectionParameters.Bind(ParameterMap, TEXT("PlanarReflectionParameters"));
		PlanarReflectionParameters2.Bind(ParameterMap, TEXT("PlanarReflectionParameters2"));
		ProjectionWithExtraFOV.Bind(ParameterMap, TEXT("ProjectionWithExtraFOV"));
		PlanarReflectionScreenScaleBias.Bind(ParameterMap, TEXT("PlanarReflectionScreenScaleBias"));
		IsStereoParameter.Bind(ParameterMap, TEXT("bIsStereo"));
		PlanarReflectionScreenBound.Bind(ParameterMap, TEXT("PlanarReflectionScreenBound"));
		PlanarReflectionTexture.Bind(ParameterMap, TEXT("PlanarReflectionTexture"));
		PlanarReflectionSampler.Bind(ParameterMap, TEXT("PlanarReflectionSampler"));
	}

	void SetParameters(FRHICommandList& RHICmdList, FPixelShaderRHIParamRef ShaderRHI, const FSceneView& View, const class FPlanarReflectionSceneProxy* ReflectionSceneProxy);

	/** Serializer. */
	friend FArchive& operator<<(FArchive& Ar, FPlanarReflectionParameters& P)
	{
		Ar << P.ReflectionPlane;
		Ar << P.PlanarReflectionOrigin;
		Ar << P.PlanarReflectionXAxis;
		Ar << P.PlanarReflectionYAxis;
		Ar << P.InverseTransposeMirrorMatrix;
		Ar << P.PlanarReflectionParameters;
		Ar << P.PlanarReflectionParameters2;
		Ar << P.ProjectionWithExtraFOV;
		Ar << P.PlanarReflectionScreenScaleBias;
		Ar << P.IsStereoParameter;
		Ar << P.PlanarReflectionScreenBound;
		Ar << P.PlanarReflectionTexture;
		Ar << P.PlanarReflectionSampler;
		return Ar;
	}

private:

	FShaderParameter ReflectionPlane;
	FShaderParameter PlanarReflectionOrigin;
	FShaderParameter PlanarReflectionXAxis;
	FShaderParameter PlanarReflectionYAxis;
	FShaderParameter InverseTransposeMirrorMatrix;
	FShaderParameter PlanarReflectionParameters;
	FShaderParameter PlanarReflectionParameters2;
	FShaderParameter ProjectionWithExtraFOV;
	FShaderParameter PlanarReflectionScreenScaleBias;
	FShaderParameter IsStereoParameter;
	FShaderParameter PlanarReflectionScreenBound;
	FShaderResourceParameter PlanarReflectionTexture;
	FShaderResourceParameter PlanarReflectionSampler;
};
