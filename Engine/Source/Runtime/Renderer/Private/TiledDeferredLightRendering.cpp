// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	TiledDeferredLightRendering.cpp: Implementation of tiled deferred shading
=============================================================================*/

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"
#include "EngineGlobals.h"
#include "RHI.h"
#include "UniformBuffer.h"
#include "ShaderParameters.h"
#include "RendererInterface.h"
#include "Shader.h"
#include "SceneUtils.h"
#include "RHIStaticStates.h"
#include "PostProcess/SceneRenderTargets.h"
#include "LightSceneInfo.h"
#include "GlobalShader.h"
#include "SceneRenderTargetParameters.h"
#include "DeferredShadingRenderer.h"
#include "ScenePrivate.h"

/** 
 * Maximum number of lights that can be handled by tiled deferred in a single compute shader pass.
 * If the scene has more visible lights than this, multiple tiled deferred passes will be needed which incurs the tile setup multiple times.
 * This is currently limited by the size of the light constant buffers. 
 */
static const int32 GMaxNumTiledDeferredLights = 1024;

/** 
 * Tile size for the deferred light compute shader.  Larger tiles have more threads in flight, but less accurate culling.
 * Tweaked for ~200 onscreen lights on a 7970.
 * Changing this requires touching the shader to cause a recompile.
 */
const int32 GDeferredLightTileSizeX = 16;
const int32 GDeferredLightTileSizeY = 16;

int32 GUseTiledDeferredShading = 1;
static FAutoConsoleVariableRef CVarUseTiledDeferredShading(
	TEXT("r.TiledDeferredShading"),
	GUseTiledDeferredShading,
	TEXT("Whether to use tiled deferred shading.  0 is off, 1 is on (default)"),
	ECVF_RenderThreadSafe
	);

// Tiled deferred has fixed overhead due to tile setup, but scales better than standard deferred
int32 GNumLightsBeforeUsingTiledDeferred = 80;
static FAutoConsoleVariableRef CVarNumLightsBeforeUsingTiledDeferred(
	TEXT("r.TiledDeferredShading.MinimumCount"),
	GNumLightsBeforeUsingTiledDeferred,
	TEXT("Number of applicable lights that must be on screen before switching to tiled deferred.\n")
	TEXT("0 means all lights that qualify (e.g. no shadows, ...) are rendered tiled deferred. Default: 80"),
	ECVF_RenderThreadSafe
	);

/** 
 * First constant buffer of light data for tiled deferred. 
 * Light data is split into two constant buffers to allow more lights per pass before hitting the d3d11 max constant buffer size of 4096 float4's
 */
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FTiledDeferredLightData,)
	SHADER_PARAMETER_ARRAY(FVector4,LightPositionAndInvRadius,[GMaxNumTiledDeferredLights])
	SHADER_PARAMETER_ARRAY(FVector4,LightColorAndFalloffExponent,[GMaxNumTiledDeferredLights])
END_GLOBAL_SHADER_PARAMETER_STRUCT()

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FTiledDeferredLightData,"TiledDeferred");

/** Second constant buffer of light data for tiled deferred. */
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FTiledDeferredLightData2,)
	SHADER_PARAMETER_ARRAY(FVector4,LightDirectionAndSpotlightMaskAndSpecularScale,[GMaxNumTiledDeferredLights])
	SHADER_PARAMETER_ARRAY(FVector4,SpotAnglesAndSourceRadiusAndSimpleLighting,[GMaxNumTiledDeferredLights])
	SHADER_PARAMETER_ARRAY(FVector4,ShadowMapChannelMask,[GMaxNumTiledDeferredLights])
END_GLOBAL_SHADER_PARAMETER_STRUCT()

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FTiledDeferredLightData2,"TiledDeferred2");

/** Compute shader used to implement tiled deferred lighting. */
template <bool bVisualizeLightCulling>
class FTiledDeferredLightingCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FTiledDeferredLightingCS,Global)
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), GDeferredLightTileSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), GDeferredLightTileSizeY);
		OutEnvironment.SetDefine(TEXT("MAX_LIGHTS"), GMaxNumTiledDeferredLights);
		OutEnvironment.SetDefine(TEXT("VISUALIZE_LIGHT_CULLING"), (uint32)bVisualizeLightCulling);
		// To reduce shader compile time of compute shaders with shared memory, doesn't have an impact on generated code with current compiler (June 2010 DX SDK)
		OutEnvironment.CompilerFlags.Add(CFLAG_StandardOptimization);
	}

	FTiledDeferredLightingCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		SceneTextureParameters.Bind(Initializer);
		InTexture.Bind(Initializer.ParameterMap, TEXT("InTexture"));
		OutTexture.Bind(Initializer.ParameterMap, TEXT("OutTexture"));
		NumLights.Bind(Initializer.ParameterMap, TEXT("NumLights"));
		ViewDimensions.Bind(Initializer.ParameterMap, TEXT("ViewDimensions"));
		LTCMatTexture.Bind(Initializer.ParameterMap, TEXT("LTCMatTexture"));
		LTCMatSampler.Bind(Initializer.ParameterMap, TEXT("LTCMatSampler"));
		LTCAmpTexture.Bind(Initializer.ParameterMap, TEXT("LTCAmpTexture"));
		LTCAmpSampler.Bind(Initializer.ParameterMap, TEXT("LTCAmpSampler"));
		TransmissionProfilesTexture.Bind(Initializer.ParameterMap, TEXT("SSProfilesTexture"));
		TransmissionProfilesLinearSampler.Bind(Initializer.ParameterMap, TEXT("TransmissionProfilesLinearSampler"));
	}

	FTiledDeferredLightingCS()
	{
	}

	CA_SUPPRESS(6262);
	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FViewInfo& View, 
		int32 ViewIndex,
		int32 NumViews,
		const TArray<FSortedLightSceneInfo, SceneRenderingAllocator>& SortedLights,
		int32 TiledDeferredLightsStart, 
		int32 TiledDeferredLightsEnd,
		const FSimpleLightArray& SimpleLights,
		int32 StartIndex,
		int32 NumThisPass,
		IPooledRenderTarget& InTextureValue,
		IPooledRenderTarget& OutTextureValue)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);
		SceneTextureParameters.Set(RHICmdList, ShaderRHI, View.FeatureLevel, ESceneTextureSetupMode::All);
		SetTextureParameter(RHICmdList, ShaderRHI, InTexture, InTextureValue.GetRenderTargetItem().ShaderResourceTexture);

		FRHIUnorderedAccessView* OutUAV = OutTextureValue.GetRenderTargetItem().UAV;
		RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, &OutUAV, 1);
		OutTexture.SetTexture(RHICmdList, ShaderRHI, 0, OutUAV);

		SetShaderValue(RHICmdList, ShaderRHI, ViewDimensions, View.ViewRect);

		SetTextureParameter(
			RHICmdList, 
			ShaderRHI,
			LTCMatTexture,
			LTCMatSampler,
			TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
			GSystemTextures.LTCMat->GetRenderTargetItem().ShaderResourceTexture
			);

		SetTextureParameter(
			RHICmdList, 
			ShaderRHI,
			LTCAmpTexture,
			LTCAmpSampler,
			TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
			GSystemTextures.LTCAmp->GetRenderTargetItem().ShaderResourceTexture
			);

		const int32 NumLightsToRenderInSortedLights = TiledDeferredLightsEnd - TiledDeferredLightsStart;

		if (TransmissionProfilesTexture.IsBound())
		{
			FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
			const IPooledRenderTarget* PooledRT = GetSubsufaceProfileTexture_RT((FRHICommandListImmediate&)RHICmdList);

			if (!PooledRT)
			{
				// no subsurface profile was used yet
				PooledRT = GSystemTextures.BlackDummy;
			}

			const FSceneRenderTargetItem& Item = PooledRT->GetRenderTargetItem();

			SetTextureParameter(RHICmdList,
				ShaderRHI,
				TransmissionProfilesTexture,
				TransmissionProfilesLinearSampler,
				TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),
				Item.ShaderResourceTexture);
		}

		static const auto AllowStaticLightingVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));
		const bool bAllowStaticLighting = (!AllowStaticLightingVar || AllowStaticLightingVar->GetValueOnRenderThread() != 0);

		FTiledDeferredLightData LightData;
		FTiledDeferredLightData2 LightData2;

		for (int32 LightIndex = 0; LightIndex < NumThisPass; LightIndex++)
		{
			if (StartIndex + LightIndex < NumLightsToRenderInSortedLights)
			{
				const FSortedLightSceneInfo& SortedLightInfo = SortedLights[TiledDeferredLightsStart + StartIndex + LightIndex];
				const FLightSceneInfo* const LightSceneInfo = SortedLightInfo.LightSceneInfo;

				FLightShaderParameters LightParameters;
				LightSceneInfo->Proxy->GetLightShaderParameters(LightParameters);

				LightData.LightPositionAndInvRadius[LightIndex] = FVector4(LightParameters.Position, LightParameters.InvRadius);
				LightData.LightColorAndFalloffExponent[LightIndex] = FVector4(LightParameters.Color, LightParameters.FalloffExponent);

				if (LightSceneInfo->Proxy->IsInverseSquared())
				{
					LightData.LightColorAndFalloffExponent[LightIndex].W = 0;
				}

				// When rendering reflection captures, the direct lighting of the light is actually the indirect specular from the main view
				if (View.bIsReflectionCapture)
				{
					LightData.LightColorAndFalloffExponent[LightIndex].X *= LightSceneInfo->Proxy->GetIndirectLightingScale();
					LightData.LightColorAndFalloffExponent[LightIndex].Y *= LightSceneInfo->Proxy->GetIndirectLightingScale();
					LightData.LightColorAndFalloffExponent[LightIndex].Z *= LightSceneInfo->Proxy->GetIndirectLightingScale();
				}

				{
					// SignBit:Spotlight, SpecularScale = abs();
					float W = LightParameters.SpecularScale * ((LightSceneInfo->Proxy->GetLightType() == LightType_Spot) ? 1 : -1);

					LightData2.LightDirectionAndSpotlightMaskAndSpecularScale[LightIndex] = FVector4(LightParameters.Direction, W);
				}

				// Lights with non-0 length don't support tiled deferred pass, should not have gotten into this list
				ensure(LightParameters.SourceLength==0.0f);

				LightData2.SpotAnglesAndSourceRadiusAndSimpleLighting[LightIndex] = FVector4(
						LightParameters.SpotAngles.X,
						LightParameters.SpotAngles.Y,
						LightParameters.SourceRadius,
						0.0f);

				int32 ShadowMapChannel = LightSceneInfo->Proxy->GetShadowMapChannel();

				if (!bAllowStaticLighting)
				{
					ShadowMapChannel = INDEX_NONE;
				}

				LightData2.ShadowMapChannelMask[LightIndex] = FVector4(
					ShadowMapChannel == 0 ? 1 : 0,
					ShadowMapChannel == 1 ? 1 : 0,
					ShadowMapChannel == 2 ? 1 : 0,
					ShadowMapChannel == 3 ? 1 : 0);
			}
			else
			{
				int32 SimpleLightIndex = StartIndex + LightIndex - NumLightsToRenderInSortedLights;
				const FSimpleLightEntry& SimpleLight = SimpleLights.InstanceData[SimpleLightIndex];
				const FSimpleLightPerViewEntry& SimpleLightPerViewData = SimpleLights.GetViewDependentData(SimpleLightIndex, ViewIndex, NumViews);
				LightData.LightPositionAndInvRadius[LightIndex] = FVector4(SimpleLightPerViewData.Position, 1.0f / FMath::Max(SimpleLight.Radius, KINDA_SMALL_NUMBER));
				LightData.LightColorAndFalloffExponent[LightIndex] = FVector4(SimpleLight.Color, SimpleLight.Exponent);
				LightData2.LightDirectionAndSpotlightMaskAndSpecularScale[LightIndex] = FVector4(FVector(1, 0, 0), 0);
				LightData2.SpotAnglesAndSourceRadiusAndSimpleLighting[LightIndex] = FVector4(-2, 1, 0, 1.0f);
				LightData2.ShadowMapChannelMask[LightIndex] = FVector4(0, 0, 0, 0);
			}
		}

		SetUniformBufferParameterImmediate(RHICmdList, ShaderRHI, GetUniformBufferParameter<FTiledDeferredLightData>(), LightData);
		SetUniformBufferParameterImmediate(RHICmdList, ShaderRHI, GetUniformBufferParameter<FTiledDeferredLightData2>(), LightData2);
		SetShaderValue(RHICmdList, ShaderRHI, NumLights, NumThisPass);
	}

	void UnsetParameters(FRHICommandList& RHICmdList, IPooledRenderTarget& OutTextureValue)
	{
		OutTexture.UnsetUAV(RHICmdList, RHICmdList.GetBoundComputeShader());

		FRHIUnorderedAccessView* OutUAV = OutTextureValue.GetRenderTargetItem().UAV;
		RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, &OutUAV, 1);
	}

	static const TCHAR* GetSourceFilename()
	{
		return TEXT("/Engine/Private/TiledDeferredLightShaders.usf");
	}

	static const TCHAR* GetFunctionName()
	{
		return TEXT("TiledDeferredLightingMain");
	}

private:
	LAYOUT_FIELD(FSceneTextureShaderParameters, SceneTextureParameters);
	LAYOUT_FIELD(FShaderResourceParameter, InTexture);
	LAYOUT_FIELD(FRWShaderParameter, OutTexture);
	LAYOUT_FIELD(FShaderParameter, NumLights);
	LAYOUT_FIELD(FShaderParameter, ViewDimensions);
	LAYOUT_FIELD(FShaderResourceParameter, LTCMatTexture);
	LAYOUT_FIELD(FShaderResourceParameter, LTCMatSampler);
	LAYOUT_FIELD(FShaderResourceParameter, LTCAmpTexture);
	LAYOUT_FIELD(FShaderResourceParameter, LTCAmpSampler);
	LAYOUT_FIELD(FShaderResourceParameter, TransmissionProfilesTexture);
	LAYOUT_FIELD(FShaderResourceParameter, TransmissionProfilesLinearSampler);
};

// #define avoids a lot of code duplication
#define VARIATION1(A) typedef FTiledDeferredLightingCS<A> FTiledDeferredLightingCS##A; \
	IMPLEMENT_SHADER_TYPE2(FTiledDeferredLightingCS##A, SF_Compute);

VARIATION1(0)			VARIATION1(1)
#undef VARIATION1


bool FDeferredShadingSceneRenderer::CanUseTiledDeferred() const
{
	return GUseTiledDeferredShading != 0 && Scene->GetFeatureLevel() >= ERHIFeatureLevel::SM5;
}

bool FDeferredShadingSceneRenderer::ShouldUseTiledDeferred(int32 NumTiledDeferredLights) const
{
	// Only use tiled deferred if there are enough unshadowed lights to justify the fixed cost, 
	return (NumTiledDeferredLights >= GNumLightsBeforeUsingTiledDeferred);
}

template <bool bVisualizeLightCulling>
static void SetShaderTemplTiledLighting(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	int32 ViewIndex,
	int32 NumViews,
	const TArray<FSortedLightSceneInfo, SceneRenderingAllocator>& SortedLights,
	int32 TiledDeferredLightsStart, 
	int32 TiledDeferredLightsEnd,
	const FSimpleLightArray& SimpleLights,
	int32 StartIndex,
	int32 NumThisPass,
	IPooledRenderTarget& InTexture,
	IPooledRenderTarget& OutTexture)
{
	TShaderMapRef<FTiledDeferredLightingCS<bVisualizeLightCulling> > ComputeShader(View.ShaderMap);
	RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());

	ComputeShader->SetParameters(RHICmdList, View, ViewIndex, NumViews, SortedLights, TiledDeferredLightsStart, TiledDeferredLightsEnd, SimpleLights, StartIndex, NumThisPass, InTexture, OutTexture);

	uint32 GroupSizeX = (View.ViewRect.Size().X + GDeferredLightTileSizeX - 1) / GDeferredLightTileSizeX;
	uint32 GroupSizeY = (View.ViewRect.Size().Y + GDeferredLightTileSizeY - 1) / GDeferredLightTileSizeY;
	DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), GroupSizeX, GroupSizeY, 1);

	ComputeShader->UnsetParameters(RHICmdList, OutTexture);
}


void FDeferredShadingSceneRenderer::RenderTiledDeferredLighting(FRHICommandListImmediate& RHICmdList, const TArray<FSortedLightSceneInfo, SceneRenderingAllocator>& SortedLights, int32 TiledDeferredLightsStart, int32 TiledDeferredLightsEnd, const FSimpleLightArray& SimpleLights)
{
	const int32 NumUnshadowedLights = TiledDeferredLightsEnd - TiledDeferredLightsStart;

	check(GUseTiledDeferredShading);
	check(SortedLights.Num() >= NumUnshadowedLights);

	const int32 NumLightsToRender = NumUnshadowedLights + SimpleLights.InstanceData.Num();
	const int32 NumLightsToRenderInSortedLights = NumUnshadowedLights;

	if (NumLightsToRender > 0)
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		INC_DWORD_STAT_BY(STAT_NumLightsUsingTiledDeferred, NumLightsToRender);
		INC_DWORD_STAT_BY(STAT_NumLightsUsingSimpleTiledDeferred, SimpleLights.InstanceData.Num());
		SCOPE_CYCLE_COUNTER(STAT_DirectLightRenderingTime);

		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		UnbindRenderTargets(RHICmdList);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS

		// Determine how many compute shader passes will be needed to process all the lights
		const int32 NumPassesNeeded = FMath::DivideAndRoundUp(NumLightsToRender, GMaxNumTiledDeferredLights);
		for (int32 PassIndex = 0; PassIndex < NumPassesNeeded; PassIndex++)
		{
			const int32 StartIndex = PassIndex * GMaxNumTiledDeferredLights;
			const int32 NumThisPass = (PassIndex == NumPassesNeeded - 1) ? NumLightsToRender - StartIndex : GMaxNumTiledDeferredLights;
			check(NumThisPass > 0);

			// One some hardware we can read and write from the same UAV with a 32 bit format. We don't do that yet.
			TRefCountPtr<IPooledRenderTarget> OutTexture;
			{
				ResolveSceneColor(RHICmdList);

				FPooledRenderTargetDesc Desc = SceneContext.GetSceneColor()->GetDesc();
				Desc.TargetableFlags |= TexCreate_UAV;

				GRenderTargetPool.FindFreeElement(RHICmdList, Desc, OutTexture, TEXT("SceneColorTiled") );
			}

			{
				SCOPED_DRAW_EVENT(RHICmdList, TiledDeferredLighting);

				IPooledRenderTarget& InTexture = *SceneContext.GetSceneColor();

				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					const FViewInfo& View = Views[ViewIndex];

					if(View.Family->EngineShowFlags.VisualizeLightCulling)
					{
						SetShaderTemplTiledLighting<1>(RHICmdList, View, ViewIndex, Views.Num(), SortedLights, TiledDeferredLightsStart, TiledDeferredLightsEnd, SimpleLights, StartIndex, NumThisPass, InTexture, *OutTexture);
					}
					else
					{
						SetShaderTemplTiledLighting<0>(RHICmdList, View, ViewIndex, Views.Num(), SortedLights, TiledDeferredLightsStart, TiledDeferredLightsEnd, SimpleLights, StartIndex, NumThisPass, InTexture, *OutTexture);
					}
				}
			}

			// swap with the former SceneColor
			SceneContext.SetSceneColor(OutTexture);
		}
	}
}
