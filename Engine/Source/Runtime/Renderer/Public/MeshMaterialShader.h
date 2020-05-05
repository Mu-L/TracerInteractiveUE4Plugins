// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MeshMaterialShader.h: Shader base classes
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameters.h"
#include "VertexFactory.h"
#include "MeshMaterialShaderType.h"
#include "MaterialShader.h"
#include "MeshDrawShaderBindings.h"

class FPrimitiveSceneProxy;
struct FMeshBatchElement;
struct FMeshDrawingRenderState;
struct FMeshPassProcessorRenderState;

template<typename TBufferStruct> class TUniformBufferRef;

class FMeshMaterialShaderElementData
{
public:
	FRHIUniformBuffer* FadeUniformBuffer = nullptr;
	FRHIUniformBuffer* DitherUniformBuffer = nullptr;

	RENDERER_API void InitializeMeshMaterialData(const FSceneView* SceneView, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, const FMeshBatch& RESTRICT MeshBatch, int32 StaticMeshId, bool bAllowStencilDither);
};

struct FMeshMaterialShaderPermutationParameters : public FMaterialShaderPermutationParameters
{
	// Type of vertex factory to compile.
	const FVertexFactoryType* VertexFactoryType;

	FMeshMaterialShaderPermutationParameters(EShaderPlatform InPlatform, const FMaterialShaderParameters& InMaterialParameters, const FVertexFactoryType* InVertexFactoryType, const int32 InPermutationId)
		: FMaterialShaderPermutationParameters(InPlatform, InMaterialParameters, InPermutationId)
		, VertexFactoryType(InVertexFactoryType)
	{}
};

struct FVertexFactoryShaderPermutationParameters
{
	EShaderPlatform Platform;
	FMaterialShaderParameters MaterialParameters;
	const FVertexFactoryType* VertexFactoryType;

	FVertexFactoryShaderPermutationParameters(EShaderPlatform InPlatform, const FMaterialShaderParameters& InMaterialParameters, const FVertexFactoryType* InVertexFactoryType)
		: Platform(InPlatform)
		, MaterialParameters(InMaterialParameters)
		, VertexFactoryType(InVertexFactoryType)
	{}
};

/** Base class of all shaders that need material and vertex factory parameters. */
class RENDERER_API FMeshMaterialShader : public FMaterialShader
{
	DECLARE_TYPE_LAYOUT(FMeshMaterialShader, NonVirtual);
public:
	using FPermutationParameters = FMeshMaterialShaderPermutationParameters;
	using ShaderMetaType = FMeshMaterialShaderType;

	FMeshMaterialShader() {}

	FMeshMaterialShader(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer);

	// Declared as a friend, so that it can be called from other modules via static linkage, even if the compiler doesn't inline it.
	FORCEINLINE friend void ValidateAfterBind(const FShaderType* Type, FMeshMaterialShader* Shader)
	{
		checkfSlow(Shader->PassUniformBuffer.IsInitialized(), TEXT("FMeshMaterialShader must bind a pass uniform buffer, even if it is just FSceneTexturesUniformParameters: %s"), Type->GetName());
	}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FMeshMaterialShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const;

	void GetElementShaderBindings(
		const FShaderMapPointerTable& PointerTable,
		const FScene* Scene, 
		const FSceneView* ViewIfDynamicMeshCommand, 
		const FVertexFactory* VertexFactory,
		const EVertexInputStreamType InputStreamType,
		const FStaticFeatureLevel FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMeshBatch& MeshBatch,
		const FMeshBatchElement& BatchElement, 
		const FMeshMaterialShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const;

	template<typename ShaderType, typename PointerTableType, typename ShaderElementDataType>
	static inline void GetElementShaderBindings(const TShaderRefBase<ShaderType, PointerTableType>& Shader,
		const FScene* Scene,
		const FSceneView* ViewIfDynamicMeshCommand,
		const FVertexFactory* VertexFactory,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMeshBatch& MeshBatch,
		const FMeshBatchElement& BatchElement,
		const ShaderElementDataType& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams)
	{
		Shader->GetElementShaderBindings(Shader.GetPointerTable(), Scene, ViewIfDynamicMeshCommand, VertexFactory, InputStreamType, FeatureLevel, PrimitiveSceneProxy, MeshBatch, BatchElement, ShaderElementData, ShaderBindings, VertexStreams);
	}

private:
	void WriteFrozenVertexFactoryParameters(FMemoryImageWriter& Writer, const TMemoryImagePtr<FVertexFactoryShaderParameters>& InVertexFactoryParameters) const;
	LAYOUT_FIELD_WITH_WRITER(TMemoryImagePtr<FVertexFactoryShaderParameters>, VertexFactoryParameters, WriteFrozenVertexFactoryParameters);

protected:
	LAYOUT_FIELD(FShaderUniformBufferParameter, PassUniformBuffer);
};
