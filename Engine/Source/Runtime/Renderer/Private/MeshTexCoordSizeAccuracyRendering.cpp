// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
MeshTexCoordSizeAccuracyRendering.cpp: Contains definitions for rendering the viewmode.
=============================================================================*/

#include "MeshTexCoordSizeAccuracyRendering.h"
#include "Components.h"
#include "PrimitiveSceneProxy.h"
#include "EngineGlobals.h"
#include "MeshBatch.h"
#include "Engine/Engine.h"

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

IMPLEMENT_SHADER_TYPE(,FMeshTexCoordSizeAccuracyPS,TEXT("/Engine/Private/MeshTexCoordSizeAccuracyPixelShader.usf"),TEXT("Main"),SF_Pixel);

void FMeshTexCoordSizeAccuracyInterface::GetDebugViewModeShaderBindings(
	const FDebugViewModePS& ShaderBase,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT Material,
	EDebugViewShaderMode DebugViewMode,
	const FVector& ViewOrigin,
	int32 VisualizeLODIndex,
	int32 VisualizeElementIndex,
	int32 NumVSInstructions,
	int32 NumPSInstructions,
	int32 ViewModeParam,
	FName ViewModeParamName,
	FMeshDrawSingleShaderBindings& ShaderBindings
) const
{
	const FMeshTexCoordSizeAccuracyPS& Shader = static_cast<const FMeshTexCoordSizeAccuracyPS&>(ShaderBase);
	const int32 AnalysisIndex = ViewModeParam >= 0 ? FMath::Clamp<int32>(ViewModeParam, 0, MAX_TEXCOORDS - 1) : -1;

	FVector4 WorldUVDensities;
#if WITH_EDITORONLY_DATA
	if (!PrimitiveSceneProxy || !PrimitiveSceneProxy->GetMeshUVDensities(VisualizeLODIndex, VisualizeElementIndex, WorldUVDensities))
#endif
	{
		FMemory::Memzero(WorldUVDensities);
	}

	ShaderBindings.Add(Shader.CPUTexelFactorParameter, WorldUVDensities);
	ShaderBindings.Add(Shader.PrimitiveAlphaParameter, (!PrimitiveSceneProxy || PrimitiveSceneProxy->IsSelected()) ? 1.f : .2f);
	ShaderBindings.Add(Shader.TexCoordAnalysisIndexParameter, AnalysisIndex);
}


#endif //!(UE_BUILD_SHIPPING || UE_BUILD_TEST)
