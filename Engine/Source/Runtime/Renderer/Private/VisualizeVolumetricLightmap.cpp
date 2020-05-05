// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VolumetricLightmap.cpp
=============================================================================*/

#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "RenderResource.h"
#include "ShaderParameters.h"
#include "RendererInterface.h"
#include "Shader.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "RHIStaticStates.h"
#include "PostProcess/SceneRenderTargets.h"
#include "GlobalShader.h"
#include "SceneRenderTargetParameters.h"
#include "DeferredShadingRenderer.h"
#include "PipelineStateCache.h"
#include "ClearQuad.h"
#include "ScenePrivate.h"
#include "SpriteIndexBuffer.h"
#include "SceneFilterRendering.h"
#include "PrecomputedVolumetricLightmap.h"

float GVolumetricLightmapVisualizationRadiusScale = .01f;
FAutoConsoleVariableRef CVarVolumetricLightmapVisualizationRadiusScale(
	TEXT("r.VolumetricLightmap.VisualizationRadiusScale"),
	GVolumetricLightmapVisualizationRadiusScale,
	TEXT("Scales the size of the spheres used to visualize volumetric lightmap samples."),
	ECVF_RenderThreadSafe
	);

float GVolumetricLightmapVisualizationMinScreenFraction = .001f;
FAutoConsoleVariableRef CVarVolumetricLightmapVisualizationMinScreenFraction(
	TEXT("r.VolumetricLightmap.VisualizationMinScreenFraction"),
	GVolumetricLightmapVisualizationMinScreenFraction,
	TEXT("Minimum screen size of a volumetric lightmap visualization sphere"),
	ECVF_RenderThreadSafe
	);

// Nvidia has lower vertex throughput when only processing a few verts per instance
const int32 GQuadsPerVisualizeInstance = 8;

TGlobalResource< FSpriteIndexBuffer<GQuadsPerVisualizeInstance> > GVisualizeQuadIndexBuffer;

class FVisualizeVolumetricLightmapVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FVisualizeVolumetricLightmapVS, Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("QUADS_PER_INSTANCE"), GQuadsPerVisualizeInstance);
	}

	/** Default constructor. */
	FVisualizeVolumetricLightmapVS() {}

	/** Initialization constructor. */
	FVisualizeVolumetricLightmapVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		VisualizationRadiusScale.Bind(Initializer.ParameterMap, TEXT("VisualizationRadiusScale"));
		VisualizationMinScreenFraction.Bind(Initializer.ParameterMap, TEXT("VisualizationMinScreenFraction"));
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View)
	{
		FRHIVertexShader* ShaderRHI = RHICmdList.GetBoundVertexShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		SetShaderValue(RHICmdList, ShaderRHI, VisualizationRadiusScale, GVolumetricLightmapVisualizationRadiusScale);
		SetShaderValue(RHICmdList, ShaderRHI, VisualizationMinScreenFraction, GVolumetricLightmapVisualizationMinScreenFraction);
	}

private:
	LAYOUT_FIELD(FShaderParameter, VisualizationRadiusScale);
	LAYOUT_FIELD(FShaderParameter, VisualizationMinScreenFraction);
};

IMPLEMENT_SHADER_TYPE(,FVisualizeVolumetricLightmapVS,TEXT("/Engine/Private/VisualizeVolumetricLightmap.usf"),TEXT("VisualizeVolumetricLightmapVS"),SF_Vertex);


class FVisualizeVolumetricLightmapPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FVisualizeVolumetricLightmapPS, Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	/** Default constructor. */
	FVisualizeVolumetricLightmapPS() {}

	/** Initialization constructor. */
	FVisualizeVolumetricLightmapPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		DiffuseColor.Bind(Initializer.ParameterMap, TEXT("DiffuseColor"));
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		FLinearColor DiffuseColorValue(.18f, .18f, .18f);

		if (!View.Family->EngineShowFlags.Materials)
		{
			DiffuseColorValue = GEngine->LightingOnlyBrightness;
		}

		SetShaderValue(RHICmdList, ShaderRHI, DiffuseColor, DiffuseColorValue);
	}

private:
	LAYOUT_FIELD(FShaderParameter, DiffuseColor);
};

IMPLEMENT_SHADER_TYPE(,FVisualizeVolumetricLightmapPS,TEXT("/Engine/Private/VisualizeVolumetricLightmap.usf"),TEXT("VisualizeVolumetricLightmapPS"),SF_Pixel);

void FDeferredShadingSceneRenderer::VisualizeVolumetricLightmap(FRHICommandListImmediate& RHICmdList)
{
	if (ViewFamily.EngineShowFlags.VisualizeVolumetricLightmap
		&& Scene->VolumetricLightmapSceneData.GetLevelVolumetricLightmap()
		&& Scene->VolumetricLightmapSceneData.GetLevelVolumetricLightmap()->Data->IndirectionTextureDimensions.GetMin() > 0)
	{
		const FPrecomputedVolumetricLightmapData* VolumetricLightmapData = Scene->VolumetricLightmapSceneData.GetLevelVolumetricLightmap()->Data;

		SCOPED_DRAW_EVENT(RHICmdList, VisualizeVolumetricLightmap);
					
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

		int32 NumRenderTargets = 1;

		FRHITexture* RenderTargets[2] =
		{
			SceneContext.GetSceneColorSurface(),
			nullptr,
		};

		if (SceneContext.GBufferB)
		{
			RenderTargets[NumRenderTargets] = SceneContext.GBufferB->GetRenderTargetItem().TargetableTexture;
			NumRenderTargets++;
		}

		FRHIRenderPassInfo RPInfo(NumRenderTargets, RenderTargets, ERenderTargetActions::Load_Store);
		RPInfo.DepthStencilRenderTarget.Action = EDepthStencilTargetActions::LoadDepthStencil_StoreDepthStencil;
		RPInfo.DepthStencilRenderTarget.DepthStencilTarget = SceneContext.GetSceneDepthSurface();
		RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthWrite_StencilWrite;

		RHICmdList.BeginRenderPass(RPInfo, TEXT("VisualizeVolumetricLightmap"));
		{
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				const FViewInfo& View = Views[ViewIndex];
				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

				RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI();
				GraphicsPSOInit.BlendState = TStaticBlendStateWriteMask<CW_RGB, CW_RGBA>::GetRHI();
				GraphicsPSOInit.PrimitiveType = PT_TriangleList;

				TShaderMapRef<FVisualizeVolumetricLightmapVS> VertexShader(View.ShaderMap);
				TShaderMapRef<FVisualizeVolumetricLightmapPS> PixelShader(View.ShaderMap);

				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

				VertexShader->SetParameters(RHICmdList, View);
				PixelShader->SetParameters(RHICmdList, View);

				int32 BrickSize = VolumetricLightmapData->BrickSize;
				uint32 NumQuads = VolumetricLightmapData->IndirectionTextureDimensions.X * VolumetricLightmapData->IndirectionTextureDimensions.Y * VolumetricLightmapData->IndirectionTextureDimensions.Z * BrickSize * BrickSize * BrickSize;

				RHICmdList.SetStreamSource(0, NULL, 0);
				RHICmdList.DrawIndexedPrimitive(GVisualizeQuadIndexBuffer.IndexBufferRHI, 0, 0, 4 * GQuadsPerVisualizeInstance, 0, 2 * GQuadsPerVisualizeInstance, FMath::DivideAndRoundUp(FMath::Min(NumQuads, 0x7FFFFFFFu / 4), (uint32)GQuadsPerVisualizeInstance));
			}
		}
		RHICmdList.EndRenderPass();

	}
}