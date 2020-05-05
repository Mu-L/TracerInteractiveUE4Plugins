// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShadowDepthRendering.cpp: Shadow depth rendering implementation
=============================================================================*/

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "Misc/MemStack.h"
#include "RHIDefinitions.h"
#include "HAL/IConsoleManager.h"
#include "Async/TaskGraphInterfaces.h"
#include "RHI.h"
#include "HitProxies.h"
#include "ShaderParameters.h"
#include "RenderResource.h"
#include "RendererInterface.h"
#include "PrimitiveViewRelevance.h"
#include "UniformBuffer.h"
#include "Shader.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "Materials/Material.h"
#include "RHIStaticStates.h"
#include "PostProcess/SceneRenderTargets.h"
#include "GlobalShader.h"
#include "MaterialShaderType.h"
#include "MaterialShader.h"
#include "MeshMaterialShader.h"
#include "ShaderBaseClasses.h"
#include "ShadowRendering.h"
#include "SceneRendering.h"
#include "LightPropagationVolume.h"
#include "ScenePrivate.h"
#include "PostProcess/SceneFilterRendering.h"
#include "ScreenRendering.h"
#include "ClearQuad.h"
#include "PipelineStateCache.h"
#include "MeshPassProcessor.inl"
#include "VisualizeTexture.h"
#include "GPUScene.h"

DECLARE_GPU_STAT_NAMED(ShadowDepths, TEXT("Shadow Depths"));

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FShadowDepthPassUniformParameters, "ShadowDepthPass");
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FMobileShadowDepthPassUniformParameters, "MobileShadowDepthPass");

template<bool bUsingVertexLayers = false>
class TScreenVSForGS : public FScreenVS
{
	DECLARE_SHADER_TYPE(TScreenVSForGS, Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && (!bUsingVertexLayers || RHISupportsVertexShaderLayer(Parameters.Platform));
	}

	TScreenVSForGS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FScreenVS(Initializer)
	{
	}
	TScreenVSForGS() {}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FScreenVS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("USING_LAYERS"), (uint32)(bUsingVertexLayers ? 1 : 0));
		if (!bUsingVertexLayers)
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_VertexToGeometryShader);
		}
	}
};

IMPLEMENT_SHADER_TYPE(template<>, TScreenVSForGS<false>, TEXT("/Engine/Private/ScreenVertexShader.usf"), TEXT("MainForGS"), SF_Vertex);
IMPLEMENT_SHADER_TYPE(template<>, TScreenVSForGS<true>, TEXT("/Engine/Private/ScreenVertexShader.usf"), TEXT("MainForGS"), SF_Vertex);


static TAutoConsoleVariable<int32> CVarShadowForceSerialSingleRenderPass(
	TEXT("r.Shadow.ForceSerialSingleRenderPass"),
	0,
	TEXT("Force Serial shadow passes to render in 1 pass."),
	ECVF_RenderThreadSafe);

void SetupShadowDepthPassUniformBuffer(
	const FProjectedShadowInfo* ShadowInfo,
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	FShadowDepthPassUniformParameters& ShadowDepthPassParameters,
	FLightPropagationVolume* LPV = nullptr
)
{
	FSceneRenderTargets& SceneRenderTargets = FSceneRenderTargets::Get(RHICmdList);
	SetupSceneTextureUniformParameters(SceneRenderTargets, View.FeatureLevel, ESceneTextureSetupMode::None, ShadowDepthPassParameters.SceneTextures);

	ShadowDepthPassParameters.ProjectionMatrix = FTranslationMatrix(ShadowInfo->PreShadowTranslation - View.ViewMatrices.GetPreViewTranslation()) * ShadowInfo->SubjectAndReceiverMatrix;	
	ShadowDepthPassParameters.ViewMatrix = ShadowInfo->ShadowViewMatrix;

	ShadowDepthPassParameters.ShadowParams = FVector4(ShadowInfo->GetShaderDepthBias(), ShadowInfo->GetShaderSlopeDepthBias(), ShadowInfo->GetShaderMaxSlopeDepthBias(), ShadowInfo->bOnePassPointLightShadow ? 1 : ShadowInfo->InvMaxSubjectDepth);
	// Only clamp vertices to the near plane when rendering whole scene directional light shadow depths or preshadows from directional lights
	const bool bClampToNearPlaneValue = ShadowInfo->IsWholeSceneDirectionalShadow() || (ShadowInfo->bPreShadow && ShadowInfo->bDirectionalLight);
	ShadowDepthPassParameters.bClampToNearPlane = bClampToNearPlaneValue ? 1.0f : 0.0f;

	if (ShadowInfo->bOnePassPointLightShadow)
	{
		const FMatrix Translation = FTranslationMatrix(-View.ViewMatrices.GetPreViewTranslation());

		for (int32 FaceIndex = 0; FaceIndex < 6; FaceIndex++)
		{
			// Have to apply the pre-view translation to the view - projection matrices
			FMatrix TranslatedShadowViewProjectionMatrix = Translation * ShadowInfo->OnePassShadowViewProjectionMatrices[FaceIndex];
			ShadowDepthPassParameters.ShadowViewProjectionMatrices[FaceIndex] = TranslatedShadowViewProjectionMatrix;
			ShadowDepthPassParameters.ShadowViewMatrices[FaceIndex] = ShadowInfo->OnePassShadowViewMatrices[FaceIndex];
		}
	}


	ShadowDepthPassParameters.RWGvListBuffer = GBlackTextureWithUAV->UnorderedAccessViewRHI;
	ShadowDepthPassParameters.RWGvListHeadBuffer = GBlackTextureWithUAV->UnorderedAccessViewRHI;
	ShadowDepthPassParameters.RWVplListBuffer = GBlackTextureWithUAV->UnorderedAccessViewRHI;
	ShadowDepthPassParameters.RWVplListHeadBuffer = GBlackTextureWithUAV->UnorderedAccessViewRHI;

	if (ShadowInfo->bReflectiveShadowmap)
	{
		const FSceneViewState* ViewState = (const FSceneViewState*)View.State;

		if (ViewState)
		{
			const FLightPropagationVolume* Lpv = ViewState->GetLightPropagationVolume(View.GetFeatureLevel());

			if (Lpv)
			{
				ShadowDepthPassParameters.LPV = Lpv->GetWriteUniformBufferParams();
			}

			ShadowDepthPassParameters.RWGvListBuffer = LPV->GetGvListBufferUav();
			ShadowDepthPassParameters.RWGvListHeadBuffer = LPV->GetGvListHeadBufferUav();
			ShadowDepthPassParameters.RWVplListBuffer = LPV->GetVplListBufferUav();
			ShadowDepthPassParameters.RWVplListHeadBuffer = LPV->GetVplListHeadBufferUav();
		}
	}
}

void SetupShadowDepthPassUniformBuffer(
	const FProjectedShadowInfo* ShadowInfo,
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	FMobileShadowDepthPassUniformParameters& ShadowDepthPassParameters)
{
	FSceneRenderTargets& SceneRenderTargets = FSceneRenderTargets::Get(RHICmdList);
	SetupMobileSceneTextureUniformParameters(SceneRenderTargets, View.FeatureLevel, false, false, ShadowDepthPassParameters.SceneTextures);

	ShadowDepthPassParameters.ProjectionMatrix = FTranslationMatrix(ShadowInfo->PreShadowTranslation - View.ViewMatrices.GetPreViewTranslation()) * ShadowInfo->SubjectAndReceiverMatrix;
	ShadowDepthPassParameters.ViewMatrix = ShadowInfo->ShadowViewMatrix;

	ShadowDepthPassParameters.ShadowParams = FVector4(ShadowInfo->GetShaderDepthBias(), ShadowInfo->GetShaderSlopeDepthBias(), ShadowInfo->GetShaderMaxSlopeDepthBias(), ShadowInfo->InvMaxSubjectDepth);
	// Only clamp vertices to the near plane when rendering whole scene directional light shadow depths or preshadows from directional lights
	const bool bClampToNearPlaneValue = ShadowInfo->IsWholeSceneDirectionalShadow() || (ShadowInfo->bPreShadow && ShadowInfo->bDirectionalLight);
	ShadowDepthPassParameters.bClampToNearPlane = bClampToNearPlaneValue ? 1.0f : 0.0f;
}

class FShadowDepthShaderElementData : public FMeshMaterialShaderElementData
{
public:

	int32 LayerId;
};

/**
* A vertex shader for rendering the depth of a mesh.
*/
class FShadowDepthVS : public FMeshMaterialShader
{
	DECLARE_INLINE_TYPE_LAYOUT(FShadowDepthVS, NonVirtual);
public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return false;
	}

	FShadowDepthVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FMeshMaterialShader(Initializer)
	{
		const ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel((EShaderPlatform)Initializer.Target.Platform);

		if (FSceneInterface::GetShadingPath(FeatureLevel) == EShadingPath::Deferred)
		{
			PassUniformBuffer.Bind(Initializer.ParameterMap, FShadowDepthPassUniformParameters::StaticStructMetadata.GetShaderVariableName());
		}

		if (FSceneInterface::GetShadingPath(FeatureLevel) == EShadingPath::Mobile)
		{
			PassUniformBuffer.Bind(Initializer.ParameterMap, FMobileShadowDepthPassUniformParameters::StaticStructMetadata.GetShaderVariableName());
		}

		LayerId.Bind(Initializer.ParameterMap, TEXT("LayerId"));
	}

	FShadowDepthVS() {}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FShadowDepthShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);

		ShaderBindings.Add(LayerId, ShaderElementData.LayerId);
	}

private:
	
	LAYOUT_FIELD(FShaderParameter, LayerId);
};

enum EShadowDepthVertexShaderMode
{
	VertexShadowDepth_PerspectiveCorrect,
	VertexShadowDepth_OutputDepth,
	VertexShadowDepth_OnePassPointLight
};

static TAutoConsoleVariable<int32> CVarSupportPointLightWholeSceneShadows(
	TEXT("r.SupportPointLightWholeSceneShadows"),
	1,
	TEXT("Enables shadowcasting point lights."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

/**
* A vertex shader for rendering the depth of a mesh.
*/
template <EShadowDepthVertexShaderMode ShaderMode, bool bRenderReflectiveShadowMap, bool bUsePositionOnlyStream, bool bIsForGeometryShader = false>
class TShadowDepthVS : public FShadowDepthVS
{
	DECLARE_SHADER_TYPE(TShadowDepthVS, MeshMaterial);
public:

	TShadowDepthVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FShadowDepthVS(Initializer)
	{
	}

	TShadowDepthVS() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		const EShaderPlatform Platform = Parameters.Platform;

		static const auto SupportAllShaderPermutationsVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SupportAllShaderPermutations"));
		const bool bForceAllPermutations = SupportAllShaderPermutationsVar && SupportAllShaderPermutationsVar->GetValueOnAnyThread() != 0;
		const bool bSupportPointLightWholeSceneShadows = CVarSupportPointLightWholeSceneShadows.GetValueOnAnyThread() != 0 || bForceAllPermutations;
		const bool bRHISupportsShadowCastingPointLights = RHISupportsGeometryShaders(Platform) || RHISupportsVertexShaderLayer(Platform);

		if (bIsForGeometryShader && (!bSupportPointLightWholeSceneShadows || !bRHISupportsShadowCastingPointLights))
		{
			return false;
		}

		//Note: This logic needs to stay in sync with OverrideWithDefaultMaterialForShadowDepth!
		// Compile for special engine materials.
		if (bRenderReflectiveShadowMap)
		{
			static const auto SupportLPV = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.LightPropagationVolume"));
			if (SupportLPV && SupportLPV->GetValueOnAnyThread() == 0)
			{
				return false;
			}
			else
			{
				// Reflective shadow map shaders must be compiled for every material because they access the material normal
				return !bUsePositionOnlyStream
					// Don't render ShadowDepth for translucent unlit materials, unless we're injecting emissive
					&& (Parameters.MaterialParameters.bShouldCastDynamicShadows || Parameters.MaterialParameters.bShouldInjectEmissiveIntoLPV || Parameters.MaterialParameters.bShouldBlockGI)
					&& IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
			}
		}
		else
		{
			return (Parameters.MaterialParameters.bIsSpecialEngineMaterial
				// Masked and WPO materials need their shaders but cannot be used with a position only stream.
				|| ((!Parameters.MaterialParameters.bWritesEveryPixelShadowPass || Parameters.MaterialParameters.bMaterialMayModifyMeshPosition) && !bUsePositionOnlyStream))
				// Only compile one pass point light shaders for feature levels >= SM5
				&& (ShaderMode != VertexShadowDepth_OnePassPointLight || IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5))
				// Only compile position-only shaders for vertex factories that support it. (Note: this assumes that a vertex factor which supports PositionOnly, supports also PositionAndNormalOnly)
				&& (!bUsePositionOnlyStream || Parameters.VertexFactoryType->SupportsPositionOnly())
				// Don't render ShadowDepth for translucent unlit materials
				&& Parameters.MaterialParameters.bShouldCastDynamicShadows
				// Only compile perspective correct light shaders for feature levels >= SM5
				&& (ShaderMode != VertexShadowDepth_PerspectiveCorrect || IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5));
		}
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FShadowDepthVS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("PERSPECTIVE_CORRECT_DEPTH"), (uint32)(ShaderMode == VertexShadowDepth_PerspectiveCorrect));
		OutEnvironment.SetDefine(TEXT("ONEPASS_POINTLIGHT_SHADOW"), (uint32)(ShaderMode == VertexShadowDepth_OnePassPointLight));
		OutEnvironment.SetDefine(TEXT("REFLECTIVE_SHADOW_MAP"), (uint32)bRenderReflectiveShadowMap);
		OutEnvironment.SetDefine(TEXT("POSITION_ONLY"), (uint32)bUsePositionOnlyStream);
		OutEnvironment.SetDefine(TEXT("IS_FOR_GEOMETRY_SHADER"), (uint32)bIsForGeometryShader);

		if (bIsForGeometryShader)
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_VertexToGeometryShader);
		}
	}
};


/**
* A Hull shader for rendering the depth of a mesh.
*/
template <EShadowDepthVertexShaderMode ShaderMode, bool bRenderReflectiveShadowMap>
class TShadowDepthHS : public FBaseHS
{
	DECLARE_SHADER_TYPE(TShadowDepthHS, MeshMaterial);
public:


	TShadowDepthHS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FBaseHS(Initializer)
	{}

	TShadowDepthHS() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		// Re-use ShouldCache from vertex shader
		return FBaseHS::ShouldCompilePermutation(Parameters)
			&& TShadowDepthVS<ShaderMode, bRenderReflectiveShadowMap, false>::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		// Re-use compilation env from vertex shader

		TShadowDepthVS<ShaderMode, bRenderReflectiveShadowMap, false>::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

/**
* A Domain shader for rendering the depth of a mesh.
*/
template <EShadowDepthVertexShaderMode ShaderMode, bool bRenderReflectiveShadowMap>
class TShadowDepthDS : public FBaseDS
{
	DECLARE_SHADER_TYPE(TShadowDepthDS, MeshMaterial);
public:

	TShadowDepthDS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FBaseDS(Initializer)
	{
		const ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel((EShaderPlatform)Initializer.Target.Platform);

		if (FSceneInterface::GetShadingPath(FeatureLevel) == EShadingPath::Deferred)
		{
			PassUniformBuffer.Bind(Initializer.ParameterMap, FShadowDepthPassUniformParameters::StaticStructMetadata.GetShaderVariableName());
		}

		if (FSceneInterface::GetShadingPath(FeatureLevel) == EShadingPath::Mobile)
		{
			PassUniformBuffer.Bind(Initializer.ParameterMap, FMobileShadowDepthPassUniformParameters::StaticStructMetadata.GetShaderVariableName());
		}
	}

	TShadowDepthDS() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		// Re-use ShouldCache from vertex shader
		return FBaseDS::ShouldCompilePermutation(Parameters)
			&& TShadowDepthVS<ShaderMode, bRenderReflectiveShadowMap, false>::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		// Re-use compilation env from vertex shader
		TShadowDepthVS<ShaderMode, bRenderReflectiveShadowMap, false>::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

/** Geometry shader that allows one pass point light shadows by cloning triangles to all faces of the cube map. */
class FOnePassPointShadowDepthGS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FOnePassPointShadowDepthGS, MeshMaterial);
public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return RHISupportsGeometryShaders(Parameters.Platform) && TShadowDepthVS<VertexShadowDepth_OnePassPointLight, false, false, true>::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("ONEPASS_POINTLIGHT_SHADOW"), 1);
		TShadowDepthVS<VertexShadowDepth_OnePassPointLight, false, false, true>::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	FOnePassPointShadowDepthGS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		const ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel((EShaderPlatform)Initializer.Target.Platform);

		if (FSceneInterface::GetShadingPath(FeatureLevel) == EShadingPath::Deferred)
		{
			PassUniformBuffer.Bind(Initializer.ParameterMap, FShadowDepthPassUniformParameters::StaticStructMetadata.GetShaderVariableName());
		}

		if (FSceneInterface::GetShadingPath(FeatureLevel) == EShadingPath::Mobile)
		{
			PassUniformBuffer.Bind(Initializer.ParameterMap, FMobileShadowDepthPassUniformParameters::StaticStructMetadata.GetShaderVariableName());
		}
	}

	FOnePassPointShadowDepthGS() {}
};

IMPLEMENT_SHADER_TYPE(, FOnePassPointShadowDepthGS, TEXT("/Engine/Private/ShadowDepthVertexShader.usf"), TEXT("MainOnePassPointLightGS"), SF_Geometry);

#define IMPLEMENT_SHADOW_DEPTH_SHADERMODE_SHADERS(ShaderMode,bRenderReflectiveShadowMap) \
	typedef TShadowDepthVS<ShaderMode, bRenderReflectiveShadowMap, false> TShadowDepthVS##ShaderMode##bRenderReflectiveShadowMap; \
	typedef TShadowDepthHS<ShaderMode, bRenderReflectiveShadowMap       > TShadowDepthHS##ShaderMode##bRenderReflectiveShadowMap; \
	typedef TShadowDepthDS<ShaderMode, bRenderReflectiveShadowMap       > TShadowDepthDS##ShaderMode##bRenderReflectiveShadowMap; \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, TShadowDepthVS##ShaderMode##bRenderReflectiveShadowMap, TEXT("/Engine/Private/ShadowDepthVertexShader.usf"), TEXT("Main"),       SF_Vertex); \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, TShadowDepthHS##ShaderMode##bRenderReflectiveShadowMap, TEXT("/Engine/Private/ShadowDepthVertexShader.usf"), TEXT("MainHull"),   SF_Hull  ); \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, TShadowDepthDS##ShaderMode##bRenderReflectiveShadowMap, TEXT("/Engine/Private/ShadowDepthVertexShader.usf"), TEXT("MainDomain"), SF_Domain);

IMPLEMENT_SHADOW_DEPTH_SHADERMODE_SHADERS(VertexShadowDepth_PerspectiveCorrect, true);
IMPLEMENT_SHADOW_DEPTH_SHADERMODE_SHADERS(VertexShadowDepth_PerspectiveCorrect, false);
IMPLEMENT_SHADOW_DEPTH_SHADERMODE_SHADERS(VertexShadowDepth_OutputDepth, true);
IMPLEMENT_SHADOW_DEPTH_SHADERMODE_SHADERS(VertexShadowDepth_OutputDepth, false);
IMPLEMENT_SHADOW_DEPTH_SHADERMODE_SHADERS(VertexShadowDepth_OnePassPointLight, false);

// Position only vertex shaders.
typedef TShadowDepthVS<VertexShadowDepth_PerspectiveCorrect, false, true> TShadowDepthVSVertexShadowDepth_PerspectiveCorrectPositionOnly;
typedef TShadowDepthVS<VertexShadowDepth_OutputDepth,        false, true> TShadowDepthVSVertexShadowDepth_OutputDepthPositionOnly;
typedef TShadowDepthVS<VertexShadowDepth_OnePassPointLight,  false, true> TShadowDepthVSVertexShadowDepth_OnePassPointLightPositionOnly;
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, TShadowDepthVSVertexShadowDepth_PerspectiveCorrectPositionOnly, TEXT("/Engine/Private/ShadowDepthVertexShader.usf"), TEXT("PositionOnlyMain"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, TShadowDepthVSVertexShadowDepth_OutputDepthPositionOnly,        TEXT("/Engine/Private/ShadowDepthVertexShader.usf"), TEXT("PositionOnlyMain"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, TShadowDepthVSVertexShadowDepth_OnePassPointLightPositionOnly,  TEXT("/Engine/Private/ShadowDepthVertexShader.usf"), TEXT("PositionOnlyMain"), SF_Vertex);

// One pass point light VS for GS shaders.
typedef TShadowDepthVS<VertexShadowDepth_OnePassPointLight, false, false, true> TShadowDepthVSForGSVertexShadowDepth_OnePassPointLight;
typedef TShadowDepthVS<VertexShadowDepth_OnePassPointLight, false, true,  true> TShadowDepthVSForGSVertexShadowDepth_OnePassPointLightPositionOnly;
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, TShadowDepthVSForGSVertexShadowDepth_OnePassPointLight,             TEXT("/Engine/Private/ShadowDepthVertexShader.usf"), TEXT("MainForGS"),             SF_Vertex); \
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, TShadowDepthVSForGSVertexShadowDepth_OnePassPointLightPositionOnly, TEXT("/Engine/Private/ShadowDepthVertexShader.usf"), TEXT("PositionOnlyMainForGS"), SF_Vertex);

/**
* A pixel shader for rendering the depth of a mesh.
*/
class FShadowDepthBasePS : public FMeshMaterialShader
{
	DECLARE_INLINE_TYPE_LAYOUT(FShadowDepthBasePS, NonVirtual);
public:

	FShadowDepthBasePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		GvListBuffer.Bind(Initializer.ParameterMap, TEXT("RWGvListBuffer"));
		GvListHeadBuffer.Bind(Initializer.ParameterMap, TEXT("RWGvListHeadBuffer"));
		VplListBuffer.Bind(Initializer.ParameterMap, TEXT("RWVplListBuffer"));
		VplListHeadBuffer.Bind(Initializer.ParameterMap, TEXT("RWVplListHeadBuffer"));

		const ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel((EShaderPlatform)Initializer.Target.Platform);

		if (FSceneInterface::GetShadingPath(FeatureLevel) == EShadingPath::Deferred)
		{
			PassUniformBuffer.Bind(Initializer.ParameterMap, FShadowDepthPassUniformParameters::StaticStructMetadata.GetShaderVariableName());
		}

		if (FSceneInterface::GetShadingPath(FeatureLevel) == EShadingPath::Mobile)
		{
			PassUniformBuffer.Bind(Initializer.ParameterMap, FMobileShadowDepthPassUniformParameters::StaticStructMetadata.GetShaderVariableName());
		}
	}

	FShadowDepthBasePS() {}

private:
	
	LAYOUT_FIELD(FRWShaderParameter, GvListBuffer);
	LAYOUT_FIELD(FRWShaderParameter, GvListHeadBuffer);
	LAYOUT_FIELD(FRWShaderParameter, VplListBuffer);
	LAYOUT_FIELD(FRWShaderParameter, VplListHeadBuffer);
};

enum EShadowDepthPixelShaderMode
{
	PixelShadowDepth_NonPerspectiveCorrect,
	PixelShadowDepth_PerspectiveCorrect,
	PixelShadowDepth_OnePassPointLight
};

template <EShadowDepthPixelShaderMode ShaderMode, bool bRenderReflectiveShadowMap>
class TShadowDepthPS : public FShadowDepthBasePS
{
	DECLARE_SHADER_TYPE(TShadowDepthPS, MeshMaterial);
public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		const EShaderPlatform Platform = Parameters.Platform;

		if (!IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5))
		{
			return (Parameters.MaterialParameters.bIsSpecialEngineMaterial
				// Only compile for masked or lit translucent materials
				|| !Parameters.MaterialParameters.bWritesEveryPixelShadowPass
				|| (Parameters.MaterialParameters.bMaterialMayModifyMeshPosition && Parameters.MaterialParameters.bIsUsedWithInstancedStaticMeshes)
				// Perspective correct rendering needs a pixel shader and WPO materials can't be overridden with default material.
				|| (ShaderMode == PixelShadowDepth_PerspectiveCorrect && Parameters.MaterialParameters.bMaterialMayModifyMeshPosition))
				&& ShaderMode == PixelShadowDepth_NonPerspectiveCorrect
				// Don't render ShadowDepth for translucent unlit materials
				&& Parameters.MaterialParameters.bShouldCastDynamicShadows
				&& !bRenderReflectiveShadowMap;
		}

		if (bRenderReflectiveShadowMap)
		{
			static const auto SupportLPV = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.LightPropagationVolume"));
			if (SupportLPV && SupportLPV->GetValueOnAnyThread() == 0)
			{
				return false;
			}
			else
			{
				//Note: This logic needs to stay in sync with OverrideWithDefaultMaterialForShadowDepth!
				// Reflective shadow map shaders must be compiled for every material because they access the material normal
				return
					// Only compile one pass point light shaders for feature levels >= SM4
					(Parameters.MaterialParameters.bShouldCastDynamicShadows || Parameters.MaterialParameters.bShouldInjectEmissiveIntoLPV || Parameters.MaterialParameters.bShouldBlockGI)
					&& IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
			}
		}
		else
		{
			//Note: This logic needs to stay in sync with OverrideWithDefaultMaterialForShadowDepth!
			return (Parameters.MaterialParameters.bIsSpecialEngineMaterial
				// Only compile for masked or lit translucent materials
				|| !Parameters.MaterialParameters.bWritesEveryPixelShadowPass
				|| (Parameters.MaterialParameters.bMaterialMayModifyMeshPosition && Parameters.MaterialParameters.bIsUsedWithInstancedStaticMeshes)
				// Perspective correct rendering needs a pixel shader and WPO materials can't be overridden with default material.
				|| (ShaderMode == PixelShadowDepth_PerspectiveCorrect && Parameters.MaterialParameters.bMaterialMayModifyMeshPosition))
				// Only compile one pass point light shaders for feature levels >= SM5
				&& (ShaderMode != PixelShadowDepth_OnePassPointLight || IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5))
				// Don't render ShadowDepth for translucent unlit materials
				&& Parameters.MaterialParameters.bShouldCastDynamicShadows
				&& IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
		}
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FShadowDepthBasePS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("PERSPECTIVE_CORRECT_DEPTH"), (uint32)(ShaderMode == PixelShadowDepth_PerspectiveCorrect));
		OutEnvironment.SetDefine(TEXT("ONEPASS_POINTLIGHT_SHADOW"), (uint32)(ShaderMode == PixelShadowDepth_OnePassPointLight));
		OutEnvironment.SetDefine(TEXT("REFLECTIVE_SHADOW_MAP"), (uint32)bRenderReflectiveShadowMap);
	}

	TShadowDepthPS()
	{
	}

	TShadowDepthPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FShadowDepthBasePS(Initializer)
	{
	}
};

// typedef required to get around macro expansion failure due to commas in template argument list for TShadowDepthPixelShader
#define IMPLEMENT_SHADOWDEPTHPASS_PIXELSHADER_TYPE(ShaderMode, bRenderReflectiveShadowMap) \
	typedef TShadowDepthPS<ShaderMode, bRenderReflectiveShadowMap> TShadowDepthPS##ShaderMode##bRenderReflectiveShadowMap; \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TShadowDepthPS##ShaderMode##bRenderReflectiveShadowMap,TEXT("/Engine/Private/ShadowDepthPixelShader.usf"),TEXT("Main"),SF_Pixel);

IMPLEMENT_SHADOWDEPTHPASS_PIXELSHADER_TYPE(PixelShadowDepth_NonPerspectiveCorrect, true);
IMPLEMENT_SHADOWDEPTHPASS_PIXELSHADER_TYPE(PixelShadowDepth_NonPerspectiveCorrect, false);
IMPLEMENT_SHADOWDEPTHPASS_PIXELSHADER_TYPE(PixelShadowDepth_PerspectiveCorrect, true);
IMPLEMENT_SHADOWDEPTHPASS_PIXELSHADER_TYPE(PixelShadowDepth_PerspectiveCorrect, false);
IMPLEMENT_SHADOWDEPTHPASS_PIXELSHADER_TYPE(PixelShadowDepth_OnePassPointLight, true);
IMPLEMENT_SHADOWDEPTHPASS_PIXELSHADER_TYPE(PixelShadowDepth_OnePassPointLight, false);

/**
* Overrides a material used for shadow depth rendering with the default material when appropriate.
* Overriding in this manner can reduce state switches and the number of shaders that have to be compiled.
* This logic needs to stay in sync with shadow depth shader ShouldCache logic.
*/
void OverrideWithDefaultMaterialForShadowDepth(
	const FMaterialRenderProxy*& InOutMaterialRenderProxy,
	const FMaterial*& InOutMaterialResource,
	bool bReflectiveShadowmap,
	ERHIFeatureLevel::Type InFeatureLevel)
{
	// Override with the default material when possible.
	if (InOutMaterialResource->WritesEveryPixel(true) &&						// Don't override masked materials.
		!InOutMaterialResource->MaterialModifiesMeshPosition_RenderThread() &&	// Don't override materials using world position offset.
		!bReflectiveShadowmap)													// Don't override when rendering reflective shadow maps.
	{
		const FMaterialRenderProxy* DefaultProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
		const FMaterial* DefaultMaterialResource = DefaultProxy->GetMaterial(InFeatureLevel);

		// Override with the default material for opaque materials that don't modify mesh position.
		InOutMaterialRenderProxy = DefaultProxy;
		InOutMaterialResource = DefaultMaterialResource;
	}
}

template <bool bRenderingReflectiveShadowMaps>
void GetShadowDepthPassShaders(
	const FMaterial& Material,
	const FVertexFactory* VertexFactory,
	ERHIFeatureLevel::Type FeatureLevel,
	bool bDirectionalLight,
	bool bOnePassPointLightShadow,
	bool bPositionOnlyVS,
	TShaderRef<FShadowDepthVS>& VertexShader,
	TShaderRef<FBaseHS>& HullShader,
	TShaderRef<FBaseDS>& DomainShader,
	TShaderRef<FShadowDepthBasePS>& PixelShader,
	TShaderRef<FOnePassPointShadowDepthGS>& GeometryShader)
{
	check(!bOnePassPointLightShadow || !bRenderingReflectiveShadowMaps);

	// Use perspective correct shadow depths for shadow types which typically render low poly meshes into the shadow depth buffer.
	// Depth will be interpolated to the pixel shader and written out, which disables HiZ and double speed Z.
	// Directional light shadows use an ortho projection and can use the non-perspective correct path without artifacts.
	// One pass point lights don't output a linear depth, so they are already perspective correct.
	const bool bUsePerspectiveCorrectShadowDepths = !bDirectionalLight && !bOnePassPointLightShadow;

	HullShader.Reset();
	DomainShader.Reset();
	GeometryShader.Reset();

	FVertexFactoryType* VFType = VertexFactory->GetType();

	const bool bInitializeTessellationShaders =
		Material.GetTessellationMode() != MTM_NoTessellation
		&& RHISupportsTessellation(GShaderPlatformForFeatureLevel[FeatureLevel])
		&& VFType->SupportsTessellationShaders();

	// Vertex related shaders
	if (bOnePassPointLightShadow)
	{
		if (bPositionOnlyVS)
		{
			VertexShader = Material.GetShader<TShadowDepthVS<VertexShadowDepth_OnePassPointLight, false, true, true> >(VFType);
		}
		else
		{
			VertexShader = Material.GetShader<TShadowDepthVS<VertexShadowDepth_OnePassPointLight, false, false, true> >(VFType);
		}

		if (RHISupportsGeometryShaders(GShaderPlatformForFeatureLevel[FeatureLevel]))
		{
			// Use the geometry shader which will clone output triangles to all faces of the cube map
			GeometryShader = Material.GetShader<FOnePassPointShadowDepthGS>(VFType);
		}

		if (bInitializeTessellationShaders)
		{
			HullShader = Material.GetShader<TShadowDepthHS<VertexShadowDepth_OnePassPointLight, false> >(VFType);
			DomainShader = Material.GetShader<TShadowDepthDS<VertexShadowDepth_OnePassPointLight, false> >(VFType);
		}
	}
	else if (bUsePerspectiveCorrectShadowDepths)
	{
		if (bRenderingReflectiveShadowMaps)
		{
			VertexShader = Material.GetShader<TShadowDepthVS<VertexShadowDepth_PerspectiveCorrect, true, false> >(VFType);
		}
		else
		{
			if (bPositionOnlyVS)
			{
				VertexShader = Material.GetShader<TShadowDepthVS<VertexShadowDepth_PerspectiveCorrect, false, true> >(VFType);
			}
			else
			{
				VertexShader = Material.GetShader<TShadowDepthVS<VertexShadowDepth_PerspectiveCorrect, false, false> >(VFType);
			}
		}

		if (bInitializeTessellationShaders)
		{
			HullShader = Material.GetShader<TShadowDepthHS<VertexShadowDepth_PerspectiveCorrect, bRenderingReflectiveShadowMaps> >(VFType);
			DomainShader = Material.GetShader<TShadowDepthDS<VertexShadowDepth_PerspectiveCorrect, bRenderingReflectiveShadowMaps> >(VFType);
		}
	}
	else
	{
		if (bRenderingReflectiveShadowMaps)
		{
			VertexShader = Material.GetShader<TShadowDepthVS<VertexShadowDepth_OutputDepth, true, false> >(VFType);

			if (bInitializeTessellationShaders)
			{
				HullShader = Material.GetShader<TShadowDepthHS<VertexShadowDepth_OutputDepth, true> >(VFType);
				DomainShader = Material.GetShader<TShadowDepthDS<VertexShadowDepth_OutputDepth, true> >(VFType);
			}
		}
		else
		{
			if (bPositionOnlyVS)
			{
				VertexShader = Material.GetShader<TShadowDepthVS<VertexShadowDepth_OutputDepth, false, true> >(VFType);
			}
			else
			{
				VertexShader = Material.GetShader<TShadowDepthVS<VertexShadowDepth_OutputDepth, false, false> >(VFType);
			}

			if (bInitializeTessellationShaders)
			{
				HullShader = Material.GetShader<TShadowDepthHS<VertexShadowDepth_OutputDepth, false> >(VFType);
				DomainShader = Material.GetShader<TShadowDepthDS<VertexShadowDepth_OutputDepth, false> >(VFType);
			}
		}
	}

	// Pixel shaders
	if (Material.WritesEveryPixel(true) && !bUsePerspectiveCorrectShadowDepths && !bRenderingReflectiveShadowMaps && VertexFactory->SupportsNullPixelShader())
	{
		// No pixel shader necessary.
		PixelShader.Reset();
	}
	else
	{
		if (bUsePerspectiveCorrectShadowDepths)
		{
			PixelShader = Material.GetShader<TShadowDepthPS<PixelShadowDepth_PerspectiveCorrect, bRenderingReflectiveShadowMaps> >(VFType, false);
		}
		else if (bOnePassPointLightShadow)
		{
			PixelShader = Material.GetShader<TShadowDepthPS<PixelShadowDepth_OnePassPointLight, false> >(VFType, false);
		}
		else
		{
			PixelShader = Material.GetShader<TShadowDepthPS<PixelShadowDepth_NonPerspectiveCorrect, bRenderingReflectiveShadowMaps> >(VFType, false);
		}
	}
}

/*-----------------------------------------------------------------------------
FProjectedShadowInfo
-----------------------------------------------------------------------------*/

static void CheckShadowDepthMaterials(const FMaterialRenderProxy* InRenderProxy, const FMaterial* InMaterial, bool bReflectiveShadowmap, ERHIFeatureLevel::Type InFeatureLevel)
{
	const FMaterialRenderProxy* RenderProxy = InRenderProxy;
	const FMaterial* Material = InMaterial;
	OverrideWithDefaultMaterialForShadowDepth(RenderProxy, Material, bReflectiveShadowmap, InFeatureLevel);
	check(RenderProxy == InRenderProxy);
	check(Material == InMaterial);
}

void FProjectedShadowInfo::ClearDepth(FRHICommandList& RHICmdList, class FSceneRenderer* SceneRenderer, int32 NumColorTextures, FRHITexture** ColorTextures, FRHITexture* DepthTexture, bool bPerformClear)
{
	check(RHICmdList.IsInsideRenderPass());

	uint32 ViewportMinX = X;
	uint32 ViewportMinY = Y;
	float ViewportMinZ = 0.0f;
	uint32 ViewportMaxX = X + BorderSize * 2 + ResolutionX;
	uint32 ViewportMaxY = Y + BorderSize * 2 + ResolutionY;
	float ViewportMaxZ = 1.0f;

	int32 NumClearColors;
	bool bClearColor;
	FLinearColor Colors[2];

	// Translucent shadows use draw call clear
	check(!bTranslucentShadow);

	if (bReflectiveShadowmap)
	{
		// Clear color and depth targets			
		bClearColor = true;
		Colors[0] = FLinearColor(0, 0, 1, 0);
		Colors[1] = FLinearColor(0, 0, 0, 0);

		NumClearColors = FMath::Min(2, NumColorTextures);
	}
	else
	{
		// Clear depth only.
		bClearColor = false;
		Colors[0] = FLinearColor::White;
		NumClearColors = FMath::Min(1, NumColorTextures);
	}

	if (bPerformClear)
	{
		RHICmdList.SetViewport(
			ViewportMinX,
			ViewportMinY,
			ViewportMinZ,
			ViewportMaxX,
			ViewportMaxY,
			ViewportMaxZ
		);

		DrawClearQuadMRT(RHICmdList, bClearColor, NumClearColors, Colors, true, 1.0f, false, 0);
	}
	else
	{
		RHICmdList.BindClearMRTValues(bClearColor, true, false);
	}
}

void FProjectedShadowInfo::SetStateForView(FRHICommandList& RHICmdList) const
{
	check(bAllocated);

	RHICmdList.SetViewport(
		X + BorderSize,
		Y + BorderSize,
		0.0f,
		X + BorderSize + ResolutionX,
		Y + BorderSize + ResolutionY,
		1.0f
	);
}

void SetStateForShadowDepth(bool bReflectiveShadowmap, bool bOnePassPointLightShadow, FMeshPassProcessorRenderState& DrawRenderState)
{
	if (bReflectiveShadowmap && !bOnePassPointLightShadow)
	{
		// Enable color writes to the reflective shadow map targets with opaque blending
		DrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_RGBA, CW_RGBA>::GetRHI());
	}
	else
	{
		// Disable color writes
		DrawRenderState.SetBlendState(TStaticBlendState<CW_NONE>::GetRHI());
	}

	DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_LessEqual>::GetRHI());
}

static TAutoConsoleVariable<int32> CVarParallelShadows(
	TEXT("r.ParallelShadows"),
	1,
	TEXT("Toggles parallel shadow rendering. Parallel rendering must be enabled for this to have an effect."),
	ECVF_RenderThreadSafe
);
static TAutoConsoleVariable<int32> CVarParallelShadowsNonWholeScene(
	TEXT("r.ParallelShadowsNonWholeScene"),
	0,
	TEXT("Toggles parallel shadow rendering for non whole-scene shadows. r.ParallelShadows must be enabled for this to have an effect."),
	ECVF_RenderThreadSafe
);


static TAutoConsoleVariable<int32> CVarRHICmdShadowDeferredContexts(
	TEXT("r.RHICmdShadowDeferredContexts"),
	1,
	TEXT("True to use deferred contexts to parallelize shadow command list execution."));

static TAutoConsoleVariable<int32> CVarRHICmdFlushRenderThreadTasksShadowPass(
	TEXT("r.RHICmdFlushRenderThreadTasksShadowPass"),
	0,
	TEXT("Wait for completion of parallel render thread tasks at the end of each shadow pass.  A more granular version of r.RHICmdFlushRenderThreadTasks. If either r.RHICmdFlushRenderThreadTasks or r.RHICmdFlushRenderThreadTasksShadowPass is > 0 we will flush."));

DECLARE_CYCLE_STAT(TEXT("Shadow"), STAT_CLP_Shadow, STATGROUP_ParallelCommandListMarkers);

class FShadowParallelCommandListSet : public FParallelCommandListSet
{
	FProjectedShadowInfo& ProjectedShadowInfo;
	FBeginShadowRenderPassFunction BeginShadowRenderPass;
	EShadowDepthRenderMode RenderMode;

public:
	FShadowParallelCommandListSet(
		const FViewInfo& InView,
		const FSceneRenderer* InSceneRenderer,
		FRHICommandListImmediate& InParentCmdList,
		bool bInParallelExecute,
		bool bInCreateSceneContext,
		const FMeshPassProcessorRenderState& InDrawRenderState,
		FProjectedShadowInfo& InProjectedShadowInfo,
		FBeginShadowRenderPassFunction InBeginShadowRenderPass)
		: FParallelCommandListSet(GET_STATID(STAT_CLP_Shadow), InView, InSceneRenderer, InParentCmdList, bInParallelExecute, bInCreateSceneContext, InDrawRenderState)
		, ProjectedShadowInfo(InProjectedShadowInfo)
		, BeginShadowRenderPass(InBeginShadowRenderPass)
	{
		bBalanceCommands = false;
	}

	virtual ~FShadowParallelCommandListSet()
	{
		Dispatch();
	}

	virtual void SetStateOnCommandList(FRHICommandList& CmdList) override
	{
		FParallelCommandListSet::SetStateOnCommandList(CmdList);
		BeginShadowRenderPass(CmdList, false);
		ProjectedShadowInfo.SetStateForView(CmdList);
	}
};

class FCopyShadowMapsCubeGS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FCopyShadowMapsCubeGS, Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsGeometryShaders(Parameters.Platform) && IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	FCopyShadowMapsCubeGS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
	}
	FCopyShadowMapsCubeGS() {}
};

IMPLEMENT_SHADER_TYPE(, FCopyShadowMapsCubeGS, TEXT("/Engine/Private/CopyShadowMaps.usf"), TEXT("CopyCubeDepthGS"), SF_Geometry);

class FCopyShadowMapsCubePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FCopyShadowMapsCubePS, Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	FCopyShadowMapsCubePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		ShadowDepthTexture.Bind(Initializer.ParameterMap, TEXT("ShadowDepthCubeTexture"));
		ShadowDepthSampler.Bind(Initializer.ParameterMap, TEXT("ShadowDepthSampler"));
	}
	FCopyShadowMapsCubePS() {}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, IPooledRenderTarget* SourceShadowMap)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundPixelShader(), View.ViewUniformBuffer);

		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), ShadowDepthTexture, ShadowDepthSampler, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), SourceShadowMap->GetRenderTargetItem().ShaderResourceTexture);
	}

	LAYOUT_FIELD(FShaderResourceParameter, ShadowDepthTexture);
	LAYOUT_FIELD(FShaderResourceParameter, ShadowDepthSampler);
};

IMPLEMENT_SHADER_TYPE(, FCopyShadowMapsCubePS, TEXT("/Engine/Private/CopyShadowMaps.usf"), TEXT("CopyCubeDepthPS"), SF_Pixel);

/** */
class FCopyShadowMaps2DPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FCopyShadowMaps2DPS, Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	FCopyShadowMaps2DPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		ShadowDepthTexture.Bind(Initializer.ParameterMap, TEXT("ShadowDepthTexture"));
		ShadowDepthSampler.Bind(Initializer.ParameterMap, TEXT("ShadowDepthSampler"));
	}
	FCopyShadowMaps2DPS() {}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, IPooledRenderTarget* SourceShadowMap)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundPixelShader(), View.ViewUniformBuffer);

		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), ShadowDepthTexture, ShadowDepthSampler, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), SourceShadowMap->GetRenderTargetItem().ShaderResourceTexture);
	}

	LAYOUT_FIELD(FShaderResourceParameter, ShadowDepthTexture);
	LAYOUT_FIELD(FShaderResourceParameter, ShadowDepthSampler);
};

IMPLEMENT_SHADER_TYPE(, FCopyShadowMaps2DPS, TEXT("/Engine/Private/CopyShadowMaps.usf"), TEXT("Copy2DDepthPS"), SF_Pixel);

void FProjectedShadowInfo::CopyCachedShadowMap(FRHICommandList& RHICmdList, const FMeshPassProcessorRenderState& DrawRenderState, FSceneRenderer* SceneRenderer, const FViewInfo& View)
{
	check(CacheMode == SDCM_MovablePrimitivesOnly);
	const FCachedShadowMapData& CachedShadowMapData = SceneRenderer->Scene->CachedShadowMaps.FindChecked(GetLightSceneInfo().Id);

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	DrawRenderState.ApplyToPSO(GraphicsPSOInit);
	uint32 StencilRef = DrawRenderState.GetStencilRef();

	if (CachedShadowMapData.bCachedShadowMapHasPrimitives && CachedShadowMapData.ShadowMap.IsValid())
	{
		SCOPED_DRAW_EVENT(RHICmdList, CopyCachedShadowMap);

		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		// No depth tests, so we can replace the clear
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_Always>::GetRHI();

		extern TGlobalResource<FFilterVertexDeclaration> GFilterVertexDeclaration;

		if (bOnePassPointLightShadow)
		{
			if (RHISupportsGeometryShaders(GShaderPlatformForFeatureLevel[SceneRenderer->FeatureLevel]))
			{
				// Set shaders and texture
				TShaderMapRef<TScreenVSForGS<false>> ScreenVertexShader(View.ShaderMap);
				TShaderMapRef<FCopyShadowMapsCubeGS> GeometryShader(View.ShaderMap);
				TShaderMapRef<FCopyShadowMapsCubePS> PixelShader(View.ShaderMap);

				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = ScreenVertexShader.GetVertexShader();
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
				GraphicsPSOInit.BoundShaderState.GeometryShaderRHI = GeometryShader.GetGeometryShader();
#endif
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				GraphicsPSOInit.PrimitiveType = PT_TriangleList;

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
				RHICmdList.SetStencilRef(StencilRef);

				PixelShader->SetParameters(RHICmdList, View, CachedShadowMapData.ShadowMap.DepthTarget.GetReference());

				DrawRectangle(
					RHICmdList,
					0, 0,
					ResolutionX, ResolutionY,
					BorderSize, BorderSize,
					ResolutionX, ResolutionY,
					FIntPoint(ResolutionX, ResolutionY),
					CachedShadowMapData.ShadowMap.GetSize(),
					ScreenVertexShader,
					EDRF_Default);
			}
			else
			{
				check(RHISupportsVertexShaderLayer(GShaderPlatformForFeatureLevel[SceneRenderer->FeatureLevel]));

				// Set shaders and texture
				TShaderMapRef<TScreenVSForGS<true>> ScreenVertexShader(View.ShaderMap);
				TShaderMapRef<FCopyShadowMapsCubePS> PixelShader(View.ShaderMap);

				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = ScreenVertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				GraphicsPSOInit.PrimitiveType = PT_TriangleList;

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
				RHICmdList.SetStencilRef(StencilRef);

				PixelShader->SetParameters(RHICmdList, View, CachedShadowMapData.ShadowMap.DepthTarget.GetReference());

				DrawRectangle(
					RHICmdList,
					0, 0,
					ResolutionX, ResolutionY,
					BorderSize, BorderSize,
					ResolutionX, ResolutionY,
					FIntPoint(ResolutionX, ResolutionY),
					CachedShadowMapData.ShadowMap.GetSize(),
					ScreenVertexShader,
					EDRF_Default,
					6);
			}
		}
		else
		{
			// Set shaders and texture
			TShaderMapRef<FScreenVS> ScreenVertexShader(View.ShaderMap);
			TShaderMapRef<FCopyShadowMaps2DPS> PixelShader(View.ShaderMap);

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = ScreenVertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
			RHICmdList.SetStencilRef(StencilRef);

			PixelShader->SetParameters(RHICmdList, View, CachedShadowMapData.ShadowMap.DepthTarget.GetReference());

			DrawRectangle(
				RHICmdList,
				0, 0,
				ResolutionX, ResolutionY,
				BorderSize, BorderSize,
				ResolutionX, ResolutionY,
				FIntPoint(ResolutionX, ResolutionY),
				CachedShadowMapData.ShadowMap.GetSize(),
				ScreenVertexShader,
				EDRF_Default);
		}
	}
}


void FProjectedShadowInfo::SetupShadowUniformBuffers(FRHICommandListImmediate& RHICmdList, FScene* Scene, FLightPropagationVolume* LPV)
{
	const ERHIFeatureLevel::Type FeatureLevel = ShadowDepthView->FeatureLevel;
	if (FSceneInterface::GetShadingPath(FeatureLevel) == EShadingPath::Deferred)
	{
		FShadowDepthPassUniformParameters ShadowDepthPassParameters;
		SetupShadowDepthPassUniformBuffer(this, RHICmdList, *ShadowDepthView, ShadowDepthPassParameters, LPV);

		if (IsWholeSceneDirectionalShadow() && !bReflectiveShadowmap)
		{
			check(GetShadowDepthType() == CSMShadowDepthType);
			Scene->UniformBuffers.CSMShadowDepthPassUniformBuffer.UpdateUniformBufferImmediate(ShadowDepthPassParameters);
		}

		ShadowDepthPassUniformBuffer.UpdateUniformBufferImmediate(ShadowDepthPassParameters);

		if (DependentView)
		{
			extern TSet<IPersistentViewUniformBufferExtension*> PersistentViewUniformBufferExtensions;

			for (IPersistentViewUniformBufferExtension* Extension : PersistentViewUniformBufferExtensions)
			{
				Extension->BeginRenderView(DependentView);
			}
		}
	}
	
	// This needs to be done for both mobile and deferred
	UploadDynamicPrimitiveShaderDataForView(RHICmdList, *Scene, *ShadowDepthView);
}

void FProjectedShadowInfo::TransitionCachedShadowmap(FRHICommandListImmediate& RHICmdList, FScene* Scene)
{
	if (CacheMode == SDCM_MovablePrimitivesOnly)
	{
		const FCachedShadowMapData& CachedShadowMapData = Scene->CachedShadowMaps.FindChecked(GetLightSceneInfo().Id);
		if (CachedShadowMapData.bCachedShadowMapHasPrimitives && CachedShadowMapData.ShadowMap.IsValid())
		{
			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, CachedShadowMapData.ShadowMap.DepthTarget->GetRenderTargetItem().ShaderResourceTexture);
		}
	}
}


void FProjectedShadowInfo::RenderDepthInner(FRHICommandListImmediate& RHICmdList, FSceneRenderer* SceneRenderer, FBeginShadowRenderPassFunction BeginShadowRenderPass, bool bDoParallelDispatch)
{
	const ERHIFeatureLevel::Type FeatureLevel = ShadowDepthView->FeatureLevel;
	FRHIUniformBuffer* PassUniformBuffer = ShadowDepthPassUniformBuffer;

	const bool bIsWholeSceneDirectionalShadow = IsWholeSceneDirectionalShadow();

	if (bIsWholeSceneDirectionalShadow)
	{
		// CSM shadow depth cached mesh draw commands are all referencing the same view uniform buffer.  We need to update it before rendering each cascade.
		ShadowDepthView->ViewUniformBuffer.UpdateUniformBufferImmediate(*ShadowDepthView->CachedViewUniformShaderParameters);
		
		if (DependentView)
		{
			extern TSet<IPersistentViewUniformBufferExtension*> PersistentViewUniformBufferExtensions;

			for (IPersistentViewUniformBufferExtension* Extension : PersistentViewUniformBufferExtensions)
			{
				Extension->BeginRenderView(DependentView);
			}
		}
	}

	if (FSceneInterface::GetShadingPath(FeatureLevel) == EShadingPath::Mobile)
	{
		FMobileShadowDepthPassUniformParameters ShadowDepthPassParameters;
		SetupShadowDepthPassUniformBuffer(this, RHICmdList, *ShadowDepthView, ShadowDepthPassParameters);
		SceneRenderer->Scene->UniformBuffers.MobileCSMShadowDepthPassUniformBuffer.UpdateUniformBufferImmediate(ShadowDepthPassParameters);
		MobileShadowDepthPassUniformBuffer.UpdateUniformBufferImmediate(ShadowDepthPassParameters);
		PassUniformBuffer = SceneRenderer->Scene->UniformBuffers.MobileCSMShadowDepthPassUniformBuffer;
	}

	FMeshPassProcessorRenderState DrawRenderState(*ShadowDepthView, PassUniformBuffer);
	SetStateForShadowDepth(bReflectiveShadowmap, bOnePassPointLightShadow, DrawRenderState);
	SetStateForView(RHICmdList);

	if (CacheMode == SDCM_MovablePrimitivesOnly)
	{
		// In parallel mode we will not have a renderpass active at this point.
		if (bDoParallelDispatch)
		{
			BeginShadowRenderPass(RHICmdList, false);
		}

		// Copy in depths of static primitives before we render movable primitives
		CopyCachedShadowMap(RHICmdList, DrawRenderState, SceneRenderer, *ShadowDepthView);

		if (bDoParallelDispatch)
		{
			RHICmdList.EndRenderPass();
		}
	}

	if (bDoParallelDispatch)
	{
		check(IsInRenderingThread());
		// Parallel encoding requires its own renderpass.
		check(RHICmdList.IsOutsideRenderPass());

		// parallel version
		bool bFlush = CVarRHICmdFlushRenderThreadTasksShadowPass.GetValueOnRenderThread() > 0
			|| CVarRHICmdFlushRenderThreadTasks.GetValueOnRenderThread() > 0;
		FScopedCommandListWaitForTasks Flusher(bFlush);

		// Dispatch commands
		{
			FShadowParallelCommandListSet ParallelCommandListSet(*ShadowDepthView, SceneRenderer, RHICmdList, CVarRHICmdShadowDeferredContexts.GetValueOnRenderThread() > 0, !bFlush, DrawRenderState, *this, BeginShadowRenderPass);

			ShadowDepthPass.DispatchDraw(&ParallelCommandListSet, RHICmdList);
		}

		// Renderpass must be closed once we get here.
		check(RHICmdList.IsOutsideRenderPass());
	}
	else
	{
		// We must have already opened the renderpass by the time we get here.
		check(RHICmdList.IsInsideRenderPass());

		ShadowDepthPass.DispatchDraw(nullptr, RHICmdList);

		// Renderpass must still be open when we reach here
		check(RHICmdList.IsInsideRenderPass());
	}
}

void FProjectedShadowInfo::ModifyViewForShadow(FRHICommandList& RHICmdList, FViewInfo* FoundView) const
{
	FIntRect OriginalViewRect = FoundView->ViewRect;
	FoundView->ViewRect.Min.X = 0;
	FoundView->ViewRect.Min.Y = 0;
	FoundView->ViewRect.Max.X = ResolutionX;
	FoundView->ViewRect.Max.Y = ResolutionY;

	FoundView->ViewMatrices.HackRemoveTemporalAAProjectionJitter();

	if (CascadeSettings.bFarShadowCascade)
	{
		(int32&)FoundView->DrawDynamicFlags |= (int32)EDrawDynamicFlags::FarShadowCascade;
	}

	// Don't do material texture mip biasing in shadow maps.
	FoundView->MaterialTextureMipBias = 0;

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	FoundView->CachedViewUniformShaderParameters = MakeUnique<FViewUniformShaderParameters>();

	// Override the view matrix so that billboarding primitives will be aligned to the light
	FoundView->ViewMatrices.HackOverrideViewMatrixForShadows(ShadowViewMatrix);
	FBox VolumeBounds[TVC_MAX];
	FoundView->SetupUniformBufferParameters(
		SceneContext,
		VolumeBounds,
		TVC_MAX,
		*FoundView->CachedViewUniformShaderParameters);

	if (IsWholeSceneDirectionalShadow())
	{
		FScene* Scene = (FScene*)FoundView->Family->Scene;
		FoundView->ViewUniformBuffer = Scene->UniformBuffers.CSMShadowDepthViewUniformBuffer;
	}
	else
	{
		FoundView->ViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*FoundView->CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);
	}

	// we are going to set this back now because we only want the correct view rect for the uniform buffer. For LOD calculations, we want the rendering viewrect and proj matrix.
	FoundView->ViewRect = OriginalViewRect;

	extern int32 GPreshadowsForceLowestLOD;

	if (bPreShadow && GPreshadowsForceLowestLOD)
	{
		(int32&)FoundView->DrawDynamicFlags |= EDrawDynamicFlags::ForceLowestLOD;
	}
}

FViewInfo* FProjectedShadowInfo::FindViewForShadow(FSceneRenderer* SceneRenderer) const
{
	// Choose an arbitrary view where this shadow's subject is relevant.
	FViewInfo* FoundView = NULL;
	for (int32 ViewIndex = 0; ViewIndex < SceneRenderer->Views.Num(); ViewIndex++)
	{
		FViewInfo* CheckView = &SceneRenderer->Views[ViewIndex];
		const FVisibleLightViewInfo& VisibleLightViewInfo = CheckView->VisibleLightInfos[LightSceneInfo->Id];
		FPrimitiveViewRelevance ViewRel = VisibleLightViewInfo.ProjectedShadowViewRelevanceMap[ShadowId];
		if (ViewRel.bShadowRelevance)
		{
			FoundView = CheckView;
			break;
		}
	}
	check(FoundView);
	return FoundView;
}

void FProjectedShadowInfo::RenderDepth(FRHICommandListImmediate& RHICmdList, FSceneRenderer* SceneRenderer, FBeginShadowRenderPassFunction BeginShadowRenderPass, bool bDoParallelDispatch)
{
#if WANTS_DRAW_MESH_EVENTS
	FString EventName;

	if (GetEmitDrawEvents())
	{
		GetShadowTypeNameForDrawEvent(EventName);
		EventName += FString(TEXT(" ")) + FString::FromInt(ResolutionX) + TEXT("x") + FString::FromInt(ResolutionY);
	}

	SCOPED_DRAW_EVENTF(RHICmdList, EventShadowDepthActor, *EventName);
#endif

	CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_RenderWholeSceneShadowDepthsTime, bWholeSceneShadow);
	CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_RenderPerObjectShadowDepthsTime, !bWholeSceneShadow);
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RenderShadowDepth);

	RenderDepthInner(RHICmdList, SceneRenderer, BeginShadowRenderPass, bDoParallelDispatch);
}

void FProjectedShadowInfo::SetupShadowDepthView(FRHICommandListImmediate& RHICmdList, FSceneRenderer* SceneRenderer)
{
	FViewInfo* FoundView = FindViewForShadow(SceneRenderer);
	check(FoundView && IsInRenderingThread());
	FViewInfo* DepthPassView = FoundView->CreateSnapshot();
	ModifyViewForShadow(RHICmdList, DepthPassView);
	ShadowDepthView = DepthPassView;
}

void FProjectedShadowInfo::GetShadowTypeNameForDrawEvent(FString& TypeName) const
{
	const FName ParentName = ParentSceneInfo ? ParentSceneInfo->Proxy->GetOwnerName() : NAME_None;

	if (bWholeSceneShadow)
	{
		if (CascadeSettings.ShadowSplitIndex >= 0)
		{
			TypeName = FString(TEXT("WholeScene split")) + FString::FromInt(CascadeSettings.ShadowSplitIndex);
		}
		else
		{
			if (CacheMode == SDCM_MovablePrimitivesOnly)
			{
				TypeName = FString(TEXT("WholeScene MovablePrimitives"));
			}
			else if (CacheMode == SDCM_StaticPrimitivesOnly)
			{
				TypeName = FString(TEXT("WholeScene StaticPrimitives"));
			}
			else
			{
				TypeName = FString(TEXT("WholeScene"));
			}
		}
	}
	else if (bPreShadow)
	{
		TypeName = FString(TEXT("PreShadow ")) + ParentName.ToString();
	}
	else
	{
		TypeName = FString(TEXT("PerObject ")) + ParentName.ToString();
	}
}

#if WITH_MGPU
FRHIGPUMask FSceneRenderer::GetGPUMaskForShadow(FProjectedShadowInfo* ProjectedShadowInfo) const
{
	// Preshadows are handled separately and check bDepthsCached.
	if (ProjectedShadowInfo->bPreShadow)
	{
		return AllViewsGPUMask;
	}

	// SDCM_StaticPrimitivesOnly shadows don't update every frame so we need to render
	// their depths on all possible GPUs.
	if (ProjectedShadowInfo->CacheMode == SDCM_StaticPrimitivesOnly)
	{
		// Cached whole scene shadows shouldn't be view dependent.
		checkSlow(ProjectedShadowInfo->DependentView == nullptr);

		// Multi-GPU support : Updating on all GPUs may be inefficient for AFR. Work is
		// wasted for any shadows that re-cache on consecutive frames.
		return FRHIGPUMask::All();
	}
	else
	{
		// View dependent shadows only need to render depths on their view's GPUs.
		if (ProjectedShadowInfo->DependentView != nullptr)
		{
			return ProjectedShadowInfo->DependentView->GPUMask;
		}
		else
		{
			return AllViewsGPUMask;
		}
	}
}
#endif // WITH_MGPU

void FSceneRenderer::RenderShadowDepthMapAtlases(FRHICommandListImmediate& RHICmdList)
{
	check(RHICmdList.IsOutsideRenderPass());

	// Perform setup work on all GPUs in case any cached shadows are being updated this
	// frame. We revert to the AllViewsGPUMask for uncached shadows.
	SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::All());

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	bool bCanUseParallelDispatch = RHICmdList.IsImmediate() &&  // translucent shadows are draw on the render thread, using a recursive cmdlist (which is not immediate)
		GRHICommandList.UseParallelAlgorithms() && CVarParallelShadows.GetValueOnRenderThread();

	for (int32 AtlasIndex = 0; AtlasIndex < SortedShadowsForShadowDepthPass.ShadowMapAtlases.Num(); AtlasIndex++)
	{
		const FSortedShadowMapAtlas& ShadowMapAtlas = SortedShadowsForShadowDepthPass.ShadowMapAtlases[AtlasIndex];
		FSceneRenderTargetItem& RenderTarget = ShadowMapAtlas.RenderTargets.DepthTarget->GetRenderTargetItem();
		FIntPoint AtlasSize = ShadowMapAtlas.RenderTargets.DepthTarget->GetDesc().Extent;

		GVisualizeTexture.SetCheckPoint(RHICmdList, ShadowMapAtlas.RenderTargets.DepthTarget.GetReference());

		SCOPED_DRAW_EVENTF(RHICmdList, EventShadowDepths, TEXT("Atlas%u %ux%u"), AtlasIndex, AtlasSize.X, AtlasSize.Y);

		auto BeginShadowRenderPass = [this, &RenderTarget, &SceneContext](FRHICommandList& InRHICmdList, bool bPerformClear)
		{
			check(RenderTarget.TargetableTexture->GetDepthClearValue() == 1.0f);

			ERenderTargetLoadAction DepthLoadAction = bPerformClear ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad;

			FRHIRenderPassInfo RPInfo(RenderTarget.TargetableTexture, MakeDepthStencilTargetActions(MakeRenderTargetActions(DepthLoadAction, ERenderTargetStoreAction::EStore), ERenderTargetActions::Load_Store), nullptr, FExclusiveDepthStencil::DepthWrite_StencilWrite);

			if (!GSupportsDepthRenderTargetWithoutColorRenderTarget)
			{
				RPInfo.ColorRenderTargets[0].Action = ERenderTargetActions::DontLoad_DontStore;
				RPInfo.ColorRenderTargets[0].RenderTarget = SceneContext.GetOptionalShadowDepthColorSurface(InRHICmdList, RPInfo.DepthStencilRenderTarget.DepthStencilTarget->GetTexture2D()->GetSizeX(), RPInfo.DepthStencilRenderTarget.DepthStencilTarget->GetTexture2D()->GetSizeY());
				InRHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, RPInfo.ColorRenderTargets[0].RenderTarget);
			}
			InRHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, RPInfo.DepthStencilRenderTarget.DepthStencilTarget);
			InRHICmdList.BeginRenderPass(RPInfo, TEXT("ShadowMapAtlases"));

			if (!bPerformClear)
			{
				InRHICmdList.BindClearMRTValues(false, true, false);
			}
		};

		TArray<FProjectedShadowInfo*, SceneRenderingAllocator> ParallelShadowPasses;
		TArray<FProjectedShadowInfo*, SceneRenderingAllocator> SerialShadowPasses;

		// Gather our passes here to minimize switching renderpasses
		for (int32 ShadowIndex = 0; ShadowIndex < ShadowMapAtlas.Shadows.Num(); ShadowIndex++)
		{
			FProjectedShadowInfo* ProjectedShadowInfo = ShadowMapAtlas.Shadows[ShadowIndex];

			const bool bDoParallelDispatch = bCanUseParallelDispatch &&
				(ProjectedShadowInfo->IsWholeSceneDirectionalShadow() || CVarParallelShadowsNonWholeScene.GetValueOnRenderThread());

			if (bDoParallelDispatch)
			{
				ParallelShadowPasses.Add(ProjectedShadowInfo);
			}
			else
			{
				SerialShadowPasses.Add(ProjectedShadowInfo);
			}
		}

		FLightSceneProxy* CurrentLightForDrawEvent = NULL;

#if WANTS_DRAW_MESH_EVENTS
		FDrawEvent LightEvent;
#endif

		if (ParallelShadowPasses.Num() > 0)
		{
			{
				// Clear before going wide.
				SCOPED_DRAW_EVENT(RHICmdList, SetShadowRTsAndClear);
				BeginShadowRenderPass(RHICmdList, true);
				RHICmdList.EndRenderPass();
			}

			for (int32 ShadowIndex = 0; ShadowIndex < ParallelShadowPasses.Num(); ShadowIndex++)
			{
				FProjectedShadowInfo* ProjectedShadowInfo = ParallelShadowPasses[ShadowIndex];
				SCOPED_GPU_MASK(RHICmdList, GetGPUMaskForShadow(ProjectedShadowInfo));

				if (!CurrentLightForDrawEvent || ProjectedShadowInfo->GetLightSceneInfo().Proxy != CurrentLightForDrawEvent)
				{
					if (CurrentLightForDrawEvent)
					{
						STOP_DRAW_EVENT(LightEvent);
					}

					CurrentLightForDrawEvent = ProjectedShadowInfo->GetLightSceneInfo().Proxy;
					FString LightNameWithLevel;
					GetLightNameForDrawEvent(CurrentLightForDrawEvent, LightNameWithLevel);

					BEGIN_DRAW_EVENTF(
						RHICmdList,
						LightNameEvent,
						LightEvent,
						*LightNameWithLevel);
				}
				ProjectedShadowInfo->SetupShadowUniformBuffers(RHICmdList, Scene);
				ProjectedShadowInfo->TransitionCachedShadowmap(RHICmdList, Scene);
				ProjectedShadowInfo->RenderDepth(RHICmdList, this, BeginShadowRenderPass, true);
			}
		}

		if (CurrentLightForDrawEvent)
		{
			STOP_DRAW_EVENT(LightEvent);
		}

		CurrentLightForDrawEvent = nullptr;

		if (SerialShadowPasses.Num() > 0)
		{
			bool bForceSingleRenderPass = CVarShadowForceSerialSingleRenderPass.GetValueOnAnyThread() != 0;
			if(bForceSingleRenderPass)
			{
				BeginShadowRenderPass(RHICmdList, true);
			}


			for (int32 ShadowIndex = 0; ShadowIndex < SerialShadowPasses.Num(); ShadowIndex++)
			{
				FProjectedShadowInfo* ProjectedShadowInfo = SerialShadowPasses[ShadowIndex];
				SCOPED_GPU_MASK(RHICmdList, GetGPUMaskForShadow(ProjectedShadowInfo));

				if (!CurrentLightForDrawEvent || ProjectedShadowInfo->GetLightSceneInfo().Proxy != CurrentLightForDrawEvent)
				{
					if (CurrentLightForDrawEvent)
					{
						STOP_DRAW_EVENT(LightEvent);
					}

					CurrentLightForDrawEvent = ProjectedShadowInfo->GetLightSceneInfo().Proxy;
					FString LightNameWithLevel;
					GetLightNameForDrawEvent(CurrentLightForDrawEvent, LightNameWithLevel);

					BEGIN_DRAW_EVENTF(
						RHICmdList,
						LightNameEvent,
						LightEvent,
						*LightNameWithLevel);
				}

				ProjectedShadowInfo->SetupShadowUniformBuffers(RHICmdList, Scene);
				ProjectedShadowInfo->TransitionCachedShadowmap(RHICmdList, Scene);
				if (!bForceSingleRenderPass)
				{
					BeginShadowRenderPass(RHICmdList, ShadowIndex == 0);
				}
				ProjectedShadowInfo->RenderDepth(RHICmdList, this, BeginShadowRenderPass, false);
				if(!bForceSingleRenderPass)
				{
					RHICmdList.EndRenderPass();
				}
			}
			if(bForceSingleRenderPass)
			{
				RHICmdList.EndRenderPass();
			}
		}

		if (CurrentLightForDrawEvent)
		{
			STOP_DRAW_EVENT(LightEvent);
			CurrentLightForDrawEvent = NULL;
		}

		RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, RenderTarget.TargetableTexture);
	}
}

void FSceneRenderer::RenderShadowDepthMaps(FRHICommandListImmediate& RHICmdList)
{
	check(RHICmdList.IsOutsideRenderPass());

	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderShadows);
	SCOPED_NAMED_EVENT(FSceneRenderer_RenderShadowDepthMaps, FColor::Emerald);
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	SCOPED_DRAW_EVENT(RHICmdList, ShadowDepths);
	SCOPED_GPU_STAT(RHICmdList, ShadowDepths);

	FSceneRenderer::RenderShadowDepthMapAtlases(RHICmdList);

	checkSlow(RHICmdList.IsOutsideRenderPass());

	// Perform setup work on all GPUs in case any cached shadows are being updated this
	// frame. We revert to the AllViewsGPUMask for uncached shadows.
#if WITH_MGPU
	ensure(RHICmdList.GetGPUMask() == AllViewsGPUMask);
#endif
	SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::All());

	for (int32 CubemapIndex = 0; CubemapIndex < SortedShadowsForShadowDepthPass.ShadowMapCubemaps.Num(); CubemapIndex++)
	{
		const FSortedShadowMapAtlas& ShadowMap = SortedShadowsForShadowDepthPass.ShadowMapCubemaps[CubemapIndex];
		FSceneRenderTargetItem& RenderTarget = ShadowMap.RenderTargets.DepthTarget->GetRenderTargetItem();
		FIntPoint TargetSize = ShadowMap.RenderTargets.DepthTarget->GetDesc().Extent;

		check(ShadowMap.Shadows.Num() == 1);
		FProjectedShadowInfo* ProjectedShadowInfo = ShadowMap.Shadows[0];
		SCOPED_GPU_MASK(RHICmdList, GetGPUMaskForShadow(ProjectedShadowInfo));

		const bool bDoParallelDispatch = RHICmdList.IsImmediate() &&  // translucent shadows are draw on the render thread, using a recursive cmdlist (which is not immediate)
			GRHICommandList.UseParallelAlgorithms() && CVarParallelShadows.GetValueOnRenderThread() &&
			(ProjectedShadowInfo->IsWholeSceneDirectionalShadow() || CVarParallelShadowsNonWholeScene.GetValueOnRenderThread());

		GVisualizeTexture.SetCheckPoint(RHICmdList, ShadowMap.RenderTargets.DepthTarget.GetReference());

		FString LightNameWithLevel;
		GetLightNameForDrawEvent(ProjectedShadowInfo->GetLightSceneInfo().Proxy, LightNameWithLevel);
		SCOPED_DRAW_EVENTF(RHICmdList, EventShadowDepths, TEXT("Cubemap %s %u^2"), *LightNameWithLevel, TargetSize.X, TargetSize.Y);

		ProjectedShadowInfo->SetupShadowUniformBuffers(RHICmdList, Scene);

		auto BeginShadowRenderPass = [this, &RenderTarget, &SceneContext](FRHICommandList& InRHICmdList, bool bPerformClear)
		{
			FRHITexture* DepthTarget = RenderTarget.TargetableTexture;
			ERenderTargetLoadAction DepthLoadAction = bPerformClear ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad;

			check(DepthTarget->GetDepthClearValue() == 1.0f);
			FRHIRenderPassInfo RPInfo(DepthTarget, MakeDepthStencilTargetActions(MakeRenderTargetActions(DepthLoadAction, ERenderTargetStoreAction::EStore), ERenderTargetActions::Load_Store), nullptr, FExclusiveDepthStencil::DepthWrite_StencilWrite);

			if (!GSupportsDepthRenderTargetWithoutColorRenderTarget)
			{
				RPInfo.ColorRenderTargets[0].Action = ERenderTargetActions::DontLoad_DontStore;
				RPInfo.ColorRenderTargets[0].ArraySlice = -1;
				RPInfo.ColorRenderTargets[0].MipIndex = 0;
				RPInfo.ColorRenderTargets[0].RenderTarget = SceneContext.GetOptionalShadowDepthColorSurface(InRHICmdList, DepthTarget->GetTexture2D()->GetSizeX(), DepthTarget->GetTexture2D()->GetSizeY());

				InRHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, RPInfo.ColorRenderTargets[0].RenderTarget);
			}
			InRHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, DepthTarget);
			InRHICmdList.BeginRenderPass(RPInfo, TEXT("ShadowDepthCubeMaps"));
		};

		{
			bool bDoClear = true;

			if (ProjectedShadowInfo->CacheMode == SDCM_MovablePrimitivesOnly
				&& Scene->CachedShadowMaps.FindChecked(ProjectedShadowInfo->GetLightSceneInfo().Id).bCachedShadowMapHasPrimitives)
			{
				// Skip the clear when we'll copy from a cached shadowmap
				bDoClear = false;
			}

			SCOPED_CONDITIONAL_DRAW_EVENT(RHICmdList, Clear, bDoClear);
			BeginShadowRenderPass(RHICmdList, bDoClear);
		}

		if (bDoParallelDispatch)
		{
			// In parallel mode this first pass will just be the clear.
			RHICmdList.EndRenderPass();
		}

		ProjectedShadowInfo->RenderDepth(RHICmdList, this, BeginShadowRenderPass, bDoParallelDispatch);

		if (!bDoParallelDispatch)
		{
			RHICmdList.EndRenderPass();
		}

		RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, RenderTarget.TargetableTexture);
	}

	checkSlow(RHICmdList.IsOutsideRenderPass());

	if (SortedShadowsForShadowDepthPass.PreshadowCache.Shadows.Num() > 0)
	{
		FSceneRenderTargetItem& RenderTarget = SortedShadowsForShadowDepthPass.PreshadowCache.RenderTargets.DepthTarget->GetRenderTargetItem();

		GVisualizeTexture.SetCheckPoint(RHICmdList, SortedShadowsForShadowDepthPass.PreshadowCache.RenderTargets.DepthTarget.GetReference());

		SCOPED_DRAW_EVENT(RHICmdList, PreshadowCache);

		for (int32 ShadowIndex = 0; ShadowIndex < SortedShadowsForShadowDepthPass.PreshadowCache.Shadows.Num(); ShadowIndex++)
		{
			FProjectedShadowInfo* ProjectedShadowInfo = SortedShadowsForShadowDepthPass.PreshadowCache.Shadows[ShadowIndex];

			if (!ProjectedShadowInfo->bDepthsCached)
			{
				// Multi-GPU support : Updating on all GPUs may be inefficient for AFR. Work is
				// wasted for any shadows that re-cache on consecutive frames.
				SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::All());

				const bool bDoParallelDispatch = RHICmdList.IsImmediate() &&  // translucent shadows are draw on the render thread, using a recursive cmdlist (which is not immediate)
					GRHICommandList.UseParallelAlgorithms() && CVarParallelShadows.GetValueOnRenderThread() &&
					(ProjectedShadowInfo->IsWholeSceneDirectionalShadow() || CVarParallelShadowsNonWholeScene.GetValueOnRenderThread());

				ProjectedShadowInfo->SetupShadowUniformBuffers(RHICmdList, Scene);

				auto BeginShadowRenderPass = [this, ProjectedShadowInfo](FRHICommandList& InRHICmdList, bool bPerformClear)
				{
					FRHITexture* PreShadowCacheDepthZ = Scene->PreShadowCacheDepthZ->GetRenderTargetItem().TargetableTexture.GetReference();
					InRHICmdList.TransitionResources(EResourceTransitionAccess::EWritable, &PreShadowCacheDepthZ, 1);

					FRHIRenderPassInfo RPInfo(PreShadowCacheDepthZ, EDepthStencilTargetActions::LoadDepthStencil_StoreDepthStencil, nullptr, FExclusiveDepthStencil::DepthWrite_StencilWrite);

					// Must preserve existing contents as the clear will be scissored
					InRHICmdList.BeginRenderPass(RPInfo, TEXT("ShadowDepthMaps"));
					ProjectedShadowInfo->ClearDepth(InRHICmdList, this, 0, nullptr, PreShadowCacheDepthZ, bPerformClear);
				};

				BeginShadowRenderPass(RHICmdList, true);

				if (bDoParallelDispatch)
				{
					// In parallel mode the first pass is just the clear.
					RHICmdList.EndRenderPass();
				}

				ProjectedShadowInfo->RenderDepth(RHICmdList, this, BeginShadowRenderPass, bDoParallelDispatch);

				if (!bDoParallelDispatch)
				{
					RHICmdList.EndRenderPass();
				}

				ProjectedShadowInfo->bDepthsCached = true;
			}
		}

		RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, RenderTarget.TargetableTexture);
	}

	for (int32 AtlasIndex = 0; AtlasIndex < SortedShadowsForShadowDepthPass.TranslucencyShadowMapAtlases.Num(); AtlasIndex++)
	{
		const FSortedShadowMapAtlas& ShadowMapAtlas = SortedShadowsForShadowDepthPass.TranslucencyShadowMapAtlases[AtlasIndex];
		FIntPoint TargetSize = ShadowMapAtlas.RenderTargets.ColorTargets[0]->GetDesc().Extent;

		SCOPED_DRAW_EVENTF(RHICmdList, EventShadowDepths, TEXT("TranslucencyAtlas%u %u^2"), AtlasIndex, TargetSize.X, TargetSize.Y);

		FSceneRenderTargetItem ColorTarget0 = ShadowMapAtlas.RenderTargets.ColorTargets[0]->GetRenderTargetItem();
		FSceneRenderTargetItem ColorTarget1 = ShadowMapAtlas.RenderTargets.ColorTargets[1]->GetRenderTargetItem();

		FRHITexture* RenderTargetArray[2] =
		{
			ColorTarget0.TargetableTexture,
			ColorTarget1.TargetableTexture
		};

		FRHIRenderPassInfo RPInfo(UE_ARRAY_COUNT(RenderTargetArray), RenderTargetArray, ERenderTargetActions::Load_Store);
		TransitionRenderPassTargets(RHICmdList, RPInfo);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("RenderTranslucencyDepths"));
		{
			for (int32 ShadowIndex = 0; ShadowIndex < ShadowMapAtlas.Shadows.Num(); ShadowIndex++)
			{
				FProjectedShadowInfo* ProjectedShadowInfo = ShadowMapAtlas.Shadows[ShadowIndex];
				SCOPED_GPU_MASK(RHICmdList, GetGPUMaskForShadow(ProjectedShadowInfo));
				ProjectedShadowInfo->RenderTranslucencyDepths(RHICmdList, this);
			}
		}
		RHICmdList.EndRenderPass();

		RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, ColorTarget0.TargetableTexture);
		RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, ColorTarget1.TargetableTexture);
	}

	// Get a copy of LpvWriteUniformBufferParams for parallel RSM draw-call submission
	{
		for (int32 ViewIdx = 0; ViewIdx < Views.Num(); ++ViewIdx)
		{
			FViewInfo& View = Views[ViewIdx];
			FSceneViewState* ViewState = View.ViewState;

			if (ViewState)
			{
				FLightPropagationVolume* Lpv = ViewState->GetLightPropagationVolume(FeatureLevel);

				if (Lpv)
				{
					Lpv->SetRsmUniformBuffer();
				}
			}
		}
	}

	for (int32 AtlasIndex = 0; AtlasIndex < SortedShadowsForShadowDepthPass.RSMAtlases.Num(); AtlasIndex++)
	{
		checkSlow(RHICmdList.IsOutsideRenderPass());

		const FSortedShadowMapAtlas& ShadowMapAtlas = SortedShadowsForShadowDepthPass.RSMAtlases[AtlasIndex];
		FSceneRenderTargetItem ColorTarget0 = ShadowMapAtlas.RenderTargets.ColorTargets[0]->GetRenderTargetItem();
		FSceneRenderTargetItem ColorTarget1 = ShadowMapAtlas.RenderTargets.ColorTargets[1]->GetRenderTargetItem();
		FSceneRenderTargetItem DepthTarget = ShadowMapAtlas.RenderTargets.DepthTarget->GetRenderTargetItem();
		FIntPoint TargetSize = ShadowMapAtlas.RenderTargets.DepthTarget->GetDesc().Extent;

		SCOPED_DRAW_EVENTF(RHICmdList, EventShadowDepths, TEXT("RSM%u %ux%u"), AtlasIndex, TargetSize.X, TargetSize.Y);

		for (int32 ShadowIndex = 0; ShadowIndex < ShadowMapAtlas.Shadows.Num(); ShadowIndex++)
		{
			FProjectedShadowInfo* ProjectedShadowInfo = ShadowMapAtlas.Shadows[ShadowIndex];
			SCOPED_GPU_MASK(RHICmdList, GetGPUMaskForShadow(ProjectedShadowInfo));

			const bool bDoParallelDispatch = RHICmdList.IsImmediate() &&  // translucent shadows are draw on the render thread, using a recursive cmdlist (which is not immediate)
				GRHICommandList.UseParallelAlgorithms() && CVarParallelShadows.GetValueOnRenderThread() &&
				(ProjectedShadowInfo->IsWholeSceneDirectionalShadow() || CVarParallelShadowsNonWholeScene.GetValueOnRenderThread());

			FSceneViewState* ViewState = (FSceneViewState*)ProjectedShadowInfo->DependentView->State;
			FLightPropagationVolume* LightPropagationVolume = ViewState->GetLightPropagationVolume(FeatureLevel);

			ProjectedShadowInfo->SetupShadowUniformBuffers(RHICmdList, Scene, LightPropagationVolume);

			auto BeginShadowRenderPass = [this, LightPropagationVolume, ProjectedShadowInfo, &ColorTarget0, &ColorTarget1, &DepthTarget](FRHICommandList& InRHICmdList, bool bPerformClear)
			{
				FRHITexture* RenderTargets[2];
				RenderTargets[0] = ColorTarget0.TargetableTexture;
				RenderTargets[1] = ColorTarget1.TargetableTexture;

				// Hook up the geometry volume UAVs
				FRHIUnorderedAccessView* Uavs[4];
				Uavs[0] = LightPropagationVolume->GetGvListBufferUav();
				Uavs[1] = LightPropagationVolume->GetGvListHeadBufferUav();
				Uavs[2] = LightPropagationVolume->GetVplListBufferUav();
				Uavs[3] = LightPropagationVolume->GetVplListHeadBufferUav();

				FRHIRenderPassInfo RPInfo(UE_ARRAY_COUNT(RenderTargets), RenderTargets, ERenderTargetActions::Load_Store);
				RPInfo.DepthStencilRenderTarget.Action = EDepthStencilTargetActions::LoadDepthStencil_StoreDepthStencil;
				RPInfo.DepthStencilRenderTarget.DepthStencilTarget = DepthTarget.TargetableTexture;
				RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthWrite_StencilWrite;


				InRHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToGfx, Uavs, UE_ARRAY_COUNT(Uavs));
				InRHICmdList.BeginRenderPass(RPInfo, TEXT("ShadowAtlas"));

				ProjectedShadowInfo->ClearDepth(InRHICmdList, this, UE_ARRAY_COUNT(RenderTargets), RenderTargets, DepthTarget.TargetableTexture, bPerformClear);
			};

			{
				SCOPED_DRAW_EVENT(RHICmdList, Clear);
				BeginShadowRenderPass(RHICmdList, true);
			}

			// In parallel mode the first renderpass is just the clear.
			if (bDoParallelDispatch)
			{
				RHICmdList.EndRenderPass();
			}

			ProjectedShadowInfo->RenderDepth(RHICmdList, this, BeginShadowRenderPass, bDoParallelDispatch);

			if (!bDoParallelDispatch)
			{
				RHICmdList.EndRenderPass();
			}
			{
				// Resolve the shadow depth z surface.
				RHICmdList.CopyToResolveTarget(DepthTarget.TargetableTexture, DepthTarget.ShaderResourceTexture, FResolveParams());
				RHICmdList.CopyToResolveTarget(ColorTarget0.TargetableTexture, ColorTarget0.ShaderResourceTexture, FResolveParams());
				RHICmdList.CopyToResolveTarget(ColorTarget1.TargetableTexture, ColorTarget1.ShaderResourceTexture, FResolveParams());

				FRHIUnorderedAccessView* UavsToReadable[2];
				UavsToReadable[0] = LightPropagationVolume->GetGvListBufferUav();
				UavsToReadable[1] = LightPropagationVolume->GetGvListHeadBufferUav();
				RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EGfxToGfx, UavsToReadable, UE_ARRAY_COUNT(UavsToReadable));
			}
			checkSlow(RHICmdList.IsOutsideRenderPass());
		}
	}

	checkSlow(RHICmdList.IsOutsideRenderPass());
}

template<bool bRenderReflectiveShadowMap>
void FShadowDepthPassMeshProcessor::Process(
	const FMeshBatch& RESTRICT MeshBatch,
	uint64 BatchElementMask,
	int32 StaticMeshId,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	TMeshProcessorShaders<
		FShadowDepthVS,
		FBaseHS,
		FBaseDS,
		FShadowDepthBasePS,
		FOnePassPointShadowDepthGS> ShadowDepthPassShaders;

	const bool bUsePositionOnlyVS = 
		!bRenderReflectiveShadowMap
		&& VertexFactory->SupportsPositionAndNormalOnlyStream()
		&& MaterialResource.WritesEveryPixel(true)
		&& !MaterialResource.MaterialModifiesMeshPosition_RenderThread();

	GetShadowDepthPassShaders<bRenderReflectiveShadowMap>(
		MaterialResource,
		VertexFactory,
		FeatureLevel,
		ShadowDepthType.bDirectionalLight,
		ShadowDepthType.bOnePassPointLightShadow,
		bUsePositionOnlyVS,
		ShadowDepthPassShaders.VertexShader,
		ShadowDepthPassShaders.HullShader,
		ShadowDepthPassShaders.DomainShader,
		ShadowDepthPassShaders.PixelShader,
		ShadowDepthPassShaders.GeometryShader);

	FShadowDepthShaderElementData ShaderElementData;
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

	const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(ShadowDepthPassShaders.VertexShader, ShadowDepthPassShaders.PixelShader);

	const uint32 InstanceFactor = !ShadowDepthType.bOnePassPointLightShadow || RHISupportsGeometryShaders(GShaderPlatformForFeatureLevel[FeatureLevel]) ? 1 : 6;
	for (uint32 i = 0; i < InstanceFactor; i++)
	{
		ShaderElementData.LayerId = i;

		BuildMeshDrawCommands(
			MeshBatch,
			BatchElementMask,
			PrimitiveSceneProxy,
			MaterialRenderProxy,
			MaterialResource,
			PassDrawRenderState,
			ShadowDepthPassShaders,
			MeshFillMode,
			MeshCullMode,
			SortKey,
			bUsePositionOnlyVS ? EMeshPassFeatures::PositionAndNormalOnly : EMeshPassFeatures::Default,
			ShaderElementData);
	}
}

void FShadowDepthPassMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	if (MeshBatch.CastShadow)
	{
		// Determine the mesh's material and blend mode.
		const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
		const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);

		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;
		const EBlendMode BlendMode = Material.GetBlendMode();
		const bool bReflectiveShadowmap = ShadowDepthType.bReflectiveShadowmap && !ShadowDepthType.bOnePassPointLightShadow;
		const bool bShouldCastShadow = Material.ShouldCastDynamicShadows();

		const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
		const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material, OverrideSettings);

		ERasterizerCullMode FinalCullMode;

		{
			const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, Material, OverrideSettings);

			const bool bTwoSided = Material.IsTwoSided() || PrimitiveSceneProxy->CastsShadowAsTwoSided();
			// @TODO: only render directional light shadows as two sided, and only when blocking is enabled (required by geometry volume injection)
			const bool bEffectivelyTwoSided = ShadowDepthType.bReflectiveShadowmap ? true : bTwoSided;
			// Invert culling order when mobile HDR == false.
			auto ShaderPlatform = GShaderPlatformForFeatureLevel[FeatureLevel];
			static auto* MobileHDRCvar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileHDR"));
			check(MobileHDRCvar);
			const bool bPlatformReversesCulling = (RHINeedsToSwitchVerticalAxis(ShaderPlatform) && MobileHDRCvar->GetValueOnAnyThread() == 0);

			const bool bRenderSceneTwoSided = bEffectivelyTwoSided;
			const bool bReverseCullMode = XOR(bPlatformReversesCulling, ShadowDepthType.bOnePassPointLightShadow);

			FinalCullMode = bRenderSceneTwoSided ? CM_None : bReverseCullMode ? InverseCullMode(MeshCullMode) : MeshCullMode;
		}

		if ((bShouldCastShadow || (bReflectiveShadowmap && (Material.ShouldInjectEmissiveIntoLPV() || Material.ShouldBlockGI())))
			&& ShouldIncludeDomainInMeshPass(Material.GetMaterialDomain())
			&& ShouldIncludeMaterialInDefaultOpaquePass(Material))
		{
			const FMaterialRenderProxy* EffectiveMaterialRenderProxy = &MaterialRenderProxy;
			const FMaterial* EffectiveMaterial = &Material;

			OverrideWithDefaultMaterialForShadowDepth(EffectiveMaterialRenderProxy, EffectiveMaterial, ShadowDepthType.bReflectiveShadowmap, FeatureLevel);

			if (ShadowDepthType.bReflectiveShadowmap)
			{
				Process<true>(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, *EffectiveMaterialRenderProxy, *EffectiveMaterial, MeshFillMode, FinalCullMode);
			}
			else
			{
				Process<false>(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, *EffectiveMaterialRenderProxy, *EffectiveMaterial, MeshFillMode, FinalCullMode);
			}
		}
	}
}

FShadowDepthPassMeshProcessor::FShadowDepthPassMeshProcessor(
	const FScene* Scene,
	const FSceneView* InViewIfDynamicMeshCommand,
	const TUniformBufferRef<FViewUniformShaderParameters>& InViewUniformBuffer,
	FRHIUniformBuffer* InPassUniformBuffer,
	FShadowDepthType InShadowDepthType,
	FMeshPassDrawListContext* InDrawListContext)
	: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
	, PassDrawRenderState(FMeshPassProcessorRenderState(InViewUniformBuffer, InPassUniformBuffer))
	, ShadowDepthType(InShadowDepthType)
{
	SetStateForShadowDepth(ShadowDepthType.bReflectiveShadowmap, ShadowDepthType.bOnePassPointLightShadow, PassDrawRenderState);
}

FShadowDepthType CSMShadowDepthType(true, false, false);

FMeshPassProcessor* CreateCSMShadowDepthPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FRHIUniformBuffer* PassUniformBuffer = nullptr;

	EShadingPath ShadingPath = Scene->GetShadingPath();
	if (ShadingPath == EShadingPath::Mobile)
	{
		PassUniformBuffer = Scene->UniformBuffers.MobileCSMShadowDepthPassUniformBuffer;
	}
	else //deferred
	{
		PassUniformBuffer = Scene->UniformBuffers.CSMShadowDepthPassUniformBuffer;
	}

	return new(FMemStack::Get()) FShadowDepthPassMeshProcessor(
		Scene,
		InViewIfDynamicMeshCommand,
		Scene->UniformBuffers.CSMShadowDepthViewUniformBuffer,
		PassUniformBuffer,
		CSMShadowDepthType,
		InDrawListContext);
}

FRegisterPassProcessorCreateFunction RegisterCSMShadowDepthPass(&CreateCSMShadowDepthPassProcessor, EShadingPath::Deferred, EMeshPass::CSMShadowDepth, EMeshPassFlags::CachedMeshCommands);
FRegisterPassProcessorCreateFunction RegisterMobileCSMShadowDepthPass(&CreateCSMShadowDepthPassProcessor, EShadingPath::Mobile, EMeshPass::CSMShadowDepth, EMeshPassFlags::CachedMeshCommands);
