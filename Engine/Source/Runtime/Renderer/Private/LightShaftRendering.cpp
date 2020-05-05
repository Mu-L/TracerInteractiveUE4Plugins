// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	FogRendering.cpp: Fog rendering implementation.
=============================================================================*/

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "Misc/MemStack.h"
#include "HAL/IConsoleManager.h"
#include "RHIDefinitions.h"
#include "RHI.h"
#include "ShaderParameters.h"
#include "RendererInterface.h"
#include "Shader.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "RHIStaticStates.h"
#include "PostProcess/SceneRenderTargets.h"
#include "LightSceneInfo.h"
#include "GlobalShader.h"
#include "SceneRenderTargetParameters.h"
#include "DeferredShadingRenderer.h"
#include "ScenePrivate.h"
#include "ScreenRendering.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PostProcess/RenderingCompositionGraph.h"
#include "PostProcess/PostProcessInput.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/PostProcessTemporalAA.h"
#include "PipelineStateCache.h"
#include "SceneTextureParameters.h"

/** Tweaked values from UE3 implementation **/
extern const float PointLightFadeDistanceIncrease = 200;
extern const float PointLightRadiusFadeFactor = 5;

// 0 is off, any other value is on, later we can expose more quality settings e.g. sample count
int32 GLightShafts = 1;
static FAutoConsoleVariableRef CVarLightShaftQuality(
	TEXT("r.LightShaftQuality"),
	GLightShafts,
	TEXT("Defines the light shaft quality (mobile and non mobile).\n")
	TEXT("  0: off\n")
	TEXT("  1: on (default)"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

int32 GLightShaftAllowTAA = 1;
static FAutoConsoleVariableRef CVarLightAllowTAA(
	TEXT("r.LightShaftAllowTAA"),
	GLightShaftAllowTAA,
	TEXT("Allows temporal filtering for lightshafts.\n")
	TEXT("  0: off\n")
	TEXT("  1: on (default)"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

int32 GLightShaftDownsampleFactor = 2;
static FAutoConsoleVariableRef CVarCacheLightShaftDownsampleFactor(
	TEXT("r.LightShaftDownSampleFactor"),
	GLightShaftDownsampleFactor,
	TEXT("Downsample factor for light shafts. range: 1..8"),
	ECVF_RenderThreadSafe
	);

int32 GLightShaftRenderAfterDOF = 0;
static FAutoConsoleVariableRef CVarRenderLightshaftsAfterDOF(
	TEXT("r.LightShaftRenderToSeparateTranslucency"),
	GLightShaftRenderAfterDOF,
	TEXT("If enabled, light shafts will be rendered to the separate translucency buffer.\n")
	TEXT("This ensures postprocess materials with BL_BeforeTranslucnecy are applied before light shafts"),
	ECVF_RenderThreadSafe
	);

int32 GetLightShaftDownsampleFactor()
{
	return FMath::Clamp(GLightShaftDownsampleFactor, 1, 8);
}

int32 GLightShaftBlurPasses = 3;
static FAutoConsoleVariableRef CVarCacheLightShaftBlurPasses(
	TEXT("r.LightShaftBlurPasses"),
	GLightShaftBlurPasses,
	TEXT("Number of light shaft blur passes."),
	ECVF_RenderThreadSafe
	);

float GLightShaftFirstPassDistance = .1f;
static FAutoConsoleVariableRef CVarCacheLightShaftFirstPassDistance(
	TEXT("r.LightShaftFirstPassDistance"),
	GLightShaftFirstPassDistance,
	TEXT("Fraction of the distance to the light to blur on the first radial blur pass."),
	ECVF_RenderThreadSafe
	);

// Must touch LightShaftShader.usf to propagate a change
int32 GLightShaftBlurNumSamples = 12;
static FAutoConsoleVariableRef CVarCacheLightShaftNumSamples(
	TEXT("r.LightShaftNumSamples"),
	GLightShaftBlurNumSamples,
	TEXT("Number of samples per light shaft radial blur pass.  Also affects how quickly the blur distance increases with each pass."),
	ECVF_RenderThreadSafe | ECVF_ReadOnly
	);

/*-----------------------------------------------------------------------------
	FLightShaftPixelShaderParameters
-----------------------------------------------------------------------------*/

/** Light shaft parameters that are shared between multiple pixel shaders. */
class FLightShaftPixelShaderParameters
{
	DECLARE_INLINE_TYPE_LAYOUT(FLightShaftPixelShaderParameters, NonVirtual);
public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		TextureSpaceBlurOriginParameter.Bind(ParameterMap,TEXT("TextureSpaceBlurOrigin"));
		WorldSpaceBlurOriginAndRadiusParameter.Bind(ParameterMap,TEXT("WorldSpaceBlurOriginAndRadius"));
		LightSourceRadius.Bind(ParameterMap,TEXT("LightSourceRadius"));
		WorldSpaceSpotDirectionParameter.Bind(ParameterMap,TEXT("WorldSpaceSpotDirection"));
		SpotAnglesParameter.Bind(ParameterMap, TEXT("SpotAngles"));
		WorldSpaceCameraPositionParameter.Bind(ParameterMap,TEXT("WorldSpaceCameraPositionAndDistance"));
		UVMinMaxParameter.Bind(ParameterMap, TEXT("UVMinMax"));
		AspectRatioAndInvAspectRatioParameter.Bind(ParameterMap,TEXT("AspectRatioAndInvAspectRatio"));
		LightShaftParameters.Bind(ParameterMap, TEXT("LightShaftParameters"));
		BloomTintAndThresholdParameter.Bind(ParameterMap,TEXT("BloomTintAndThreshold"));
		BloomMaxBrightnessParameter.Bind(ParameterMap,TEXT("BloomMaxBrightness"));
		DistanceFadeParameter.Bind(ParameterMap, TEXT("DistanceFade"));
		SourceTextureParameter.Bind(ParameterMap, TEXT("SourceTexture"));
		SourceTextureSamplerParameter.Bind(ParameterMap, TEXT("SourceTextureSampler"));
	}

	friend FArchive& operator<<(FArchive& Ar,FLightShaftPixelShaderParameters& Parameters)
	{
		Ar << Parameters.TextureSpaceBlurOriginParameter;
		Ar << Parameters.WorldSpaceBlurOriginAndRadiusParameter;
		Ar << Parameters.LightSourceRadius;
		Ar << Parameters.SpotAnglesParameter;
		Ar << Parameters.WorldSpaceSpotDirectionParameter;
		Ar << Parameters.WorldSpaceCameraPositionParameter;
		Ar << Parameters.UVMinMaxParameter;
		Ar << Parameters.AspectRatioAndInvAspectRatioParameter;
		Ar << Parameters.LightShaftParameters;
		Ar << Parameters.BloomTintAndThresholdParameter;
		Ar << Parameters.BloomMaxBrightnessParameter;
		Ar << Parameters.DistanceFadeParameter;
		Ar << Parameters.SourceTextureParameter;
		Ar << Parameters.SourceTextureSamplerParameter;
		return Ar;
	}

	template<typename ShaderRHIParamRef>
	void SetParameters(FRHICommandList& RHICmdList, const ShaderRHIParamRef Shader, const FLightSceneInfo* LightSceneInfo, const FViewInfo& View, TRefCountPtr<IPooledRenderTarget>& PassSource)
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		const uint32 DownsampleFactor = GetLightShaftDownsampleFactor();
		FIntPoint DownSampledViewSize(FMath::FloorToInt(View.ViewRect.Width() / DownsampleFactor), FMath::FloorToInt(View.ViewRect.Height() / DownsampleFactor));
		const FIntPoint FilterBufferSize = SceneContext.GetBufferSizeXY() / DownsampleFactor;

		const FVector2D ViewRatioOfBuffer((float)DownSampledViewSize.X / FilterBufferSize.X, (float)DownSampledViewSize.Y / FilterBufferSize.Y);
		const FVector4 AspectRatioAndInvAspectRatio(
			ViewRatioOfBuffer.X, 
			(float)FilterBufferSize.X * ViewRatioOfBuffer.Y / FilterBufferSize.Y, 
			1.0f / ViewRatioOfBuffer.X, 
			(float)FilterBufferSize.Y / (FilterBufferSize.X * ViewRatioOfBuffer.Y));

		SetShaderValue(RHICmdList, Shader, AspectRatioAndInvAspectRatioParameter, AspectRatioAndInvAspectRatio);

		const FVector WorldSpaceBlurOrigin = LightSceneInfo->Proxy->GetLightPositionForLightShafts(View.ViewMatrices.GetViewOrigin());
		// Transform into texture coordinates
		FVector4 ProjectedBlurOrigin = View.WorldToScreen(WorldSpaceBlurOrigin);

		const FIntPoint BufferSize = SceneContext.GetBufferSizeXY();
		const float InvBufferSizeX = 1.0f / BufferSize.X;
		const float InvBufferSizeY = 1.0f / BufferSize.Y;

		FVector2D ScreenSpaceBlurOrigin;
		{
			verify(ProjectedBlurOrigin.W > 0.0f); 
			float InvW = 1.0f / ProjectedBlurOrigin.W;
			float Y = (GProjectionSignY > 0.0f) ? ProjectedBlurOrigin.Y : 1.0f - ProjectedBlurOrigin.Y;
			ScreenSpaceBlurOrigin = FVector2D(
				View.ViewRect.Min.X + (0.5f + ProjectedBlurOrigin.X * 0.5f * InvW) * View.ViewRect.Width(),
				View.ViewRect.Min.Y + (0.5f - Y * 0.5f * InvW) * View.ViewRect.Height()
				);
		}
		
		ScreenSpaceBlurOrigin.X *= InvBufferSizeX;
		ScreenSpaceBlurOrigin.Y *= InvBufferSizeY;
		FVector2D TextureSpaceBlurOrigin(ScreenSpaceBlurOrigin * FVector2D(AspectRatioAndInvAspectRatio.Z, AspectRatioAndInvAspectRatio.W));

		SetShaderValue(RHICmdList, Shader, TextureSpaceBlurOriginParameter, TextureSpaceBlurOrigin);

		SetShaderValue(RHICmdList, Shader, WorldSpaceBlurOriginAndRadiusParameter, FVector4(WorldSpaceBlurOrigin, LightSceneInfo->Proxy->GetRadius()));
		SetShaderValue(RHICmdList, Shader, LightSourceRadius, LightSceneInfo->Proxy->GetSourceRadius());

		const bool bIsSpotLight = LightSceneInfo->Proxy->GetLightType() == LightType_Spot;
		if (bIsSpotLight)
		{
			SetShaderValue(RHICmdList, Shader, WorldSpaceSpotDirectionParameter, LightSceneInfo->Proxy->GetDirection());
			SetShaderValue(RHICmdList, Shader, SpotAnglesParameter, LightSceneInfo->Proxy->GetLightShaftConeParams());
		}

		const float DistanceFromLight = (View.ViewMatrices.GetViewOrigin() - WorldSpaceBlurOrigin).Size() + PointLightFadeDistanceIncrease;
		SetShaderValue(RHICmdList, Shader, WorldSpaceCameraPositionParameter, FVector4(View.ViewMatrices.GetViewOrigin(), DistanceFromLight));

		const FIntPoint DownSampledXY = View.ViewRect.Min / DownsampleFactor;
		const uint32 DownsampledSizeX = View.ViewRect.Width() / DownsampleFactor;
		const uint32 DownsampledSizeY = View.ViewRect.Height() / DownsampleFactor;

		// Limits for where the pixel shader is allowed to sample
		// Prevents reading from outside the valid region of a render target
		// Clamp to 1 less than the actual max, 
		// Since the bottom-right row/column of texels will contain some unwanted values if the size of scene color is not a factor of the downsample factor
		float MinU, MinV, MaxU, MaxV;
		{
			MinU = (DownSampledXY.X + 0.5f) / (float)FilterBufferSize.X;
			MinV = (DownSampledXY.Y + 0.5f) / (float)FilterBufferSize.Y;
			MaxU = (float(DownSampledXY.X) + DownsampledSizeX - 0.5f) / (float)FilterBufferSize.X;
			MaxV = (float(DownSampledXY.Y) + DownsampledSizeY - 0.5f) / (float)FilterBufferSize.Y;
		}

		FVector4 UVMinMax( MinU, MinV, MaxU, MaxV );
		SetShaderValue(RHICmdList, Shader, UVMinMaxParameter, UVMinMax);

		const FLinearColor BloomTint = LightSceneInfo->BloomTint;
		SetShaderValue(RHICmdList, Shader, BloomTintAndThresholdParameter, FVector4(BloomTint.R, BloomTint.G, BloomTint.B, LightSceneInfo->BloomThreshold));
		SetShaderValue(RHICmdList, Shader, BloomMaxBrightnessParameter, LightSceneInfo->BloomMaxBrightness);

		float OcclusionMaskDarkness;
		float OcclusionDepthRange;
		LightSceneInfo->Proxy->GetLightShaftOcclusionParameters(OcclusionMaskDarkness, OcclusionDepthRange);

		const FVector4 LightShaftParameterValues(1.0f / OcclusionDepthRange, LightSceneInfo->BloomScale, 1, OcclusionMaskDarkness);
		SetShaderValue(RHICmdList, Shader, LightShaftParameters, LightShaftParameterValues);

		float DistanceFade = 0.0f;
		if (LightSceneInfo->Proxy->GetLightType() != LightType_Directional)
		{
			DistanceFade = FMath::Clamp(DistanceFromLight / (LightSceneInfo->Proxy->GetRadius() * PointLightRadiusFadeFactor), 0.0f, 1.0f);
		}

		SetShaderValue(RHICmdList, Shader, DistanceFadeParameter, DistanceFade);

		if (IsValidRef(PassSource))
		{
			SetTextureParameter(
				RHICmdList, 
				Shader,
				SourceTextureParameter, SourceTextureSamplerParameter,
				TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
				PassSource->GetRenderTargetItem().ShaderResourceTexture
				);
		}
	}
private:

	LAYOUT_FIELD(FShaderParameter, TextureSpaceBlurOriginParameter)
	LAYOUT_FIELD(FShaderParameter, WorldSpaceBlurOriginAndRadiusParameter)
	LAYOUT_FIELD(FShaderParameter, LightSourceRadius)
	LAYOUT_FIELD(FShaderParameter, SpotAnglesParameter)
	LAYOUT_FIELD(FShaderParameter, WorldSpaceSpotDirectionParameter)
	LAYOUT_FIELD(FShaderParameter, WorldSpaceCameraPositionParameter)
	LAYOUT_FIELD(FShaderParameter, UVMinMaxParameter)
	LAYOUT_FIELD(FShaderParameter, AspectRatioAndInvAspectRatioParameter)
	LAYOUT_FIELD(FShaderParameter, LightShaftParameters)
	LAYOUT_FIELD(FShaderParameter, BloomTintAndThresholdParameter)
	LAYOUT_FIELD(FShaderParameter, BloomMaxBrightnessParameter);
	LAYOUT_FIELD(FShaderParameter, DistanceFadeParameter)
	LAYOUT_FIELD(FShaderResourceParameter, SourceTextureParameter)
	LAYOUT_FIELD(FShaderResourceParameter, SourceTextureSamplerParameter)

};

/*-----------------------------------------------------------------------------
	FDownsampleLightShaftsVertexShader
-----------------------------------------------------------------------------*/

class FDownsampleLightShaftsVertexShader : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FDownsampleLightShaftsVertexShader,Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	/** Default constructor. */
	FDownsampleLightShaftsVertexShader() {}

	/** Initialization constructor. */
	FDownsampleLightShaftsVertexShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
	}

	/** Sets shader parameter values */
	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundVertexShader(), View.ViewUniformBuffer);
	}
};

IMPLEMENT_SHADER_TYPE(,FDownsampleLightShaftsVertexShader,TEXT("/Engine/Private/LightShaftShader.usf"),TEXT("DownsampleLightShaftsVertexMain"),SF_Vertex);

/*-----------------------------------------------------------------------------
	TDownsampleLightShaftsPixelShader
-----------------------------------------------------------------------------*/

template<ELightComponentType LightType, bool bOcclusionTerm> 
class TDownsampleLightShaftsPixelShader : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TDownsampleLightShaftsPixelShader,Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); 
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("POINT_LIGHT_SHAFTS"), (LightType == LightType_Point || LightType == LightType_Spot));
		OutEnvironment.SetDefine(TEXT("SPOT_LIGHT_SHAFTS"), (LightType == LightType_Spot));
		OutEnvironment.SetDefine(TEXT("POINT_LIGHT_RADIUS_FADE_FACTOR"), PointLightRadiusFadeFactor);
		OutEnvironment.SetDefine(TEXT("OCCLUSION_TERM"), (uint32)bOcclusionTerm);
	}

	/** Default constructor. */
	TDownsampleLightShaftsPixelShader() {}

	/** Initialization constructor. */
	TDownsampleLightShaftsPixelShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
		LightShaftParameters.Bind(Initializer.ParameterMap);
		SampleOffsetsParameter.Bind(Initializer.ParameterMap,TEXT("SampleOffsets"));
		SceneTextureParams.Bind(Initializer);
	}

	/** Sets shader parameter values */
	void SetParameters(FRHICommandList& RHICmdList, const FLightSceneInfo* LightSceneInfo, const FViewInfo& View, TRefCountPtr<IPooledRenderTarget>& PassSource)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundPixelShader(), View.ViewUniformBuffer);
		LightShaftParameters.SetParameters(RHICmdList, RHICmdList.GetBoundPixelShader(), LightSceneInfo, View, PassSource);

		const FIntPoint BufferSize = FSceneRenderTargets::Get(RHICmdList).GetBufferSizeXY();
		FVector2D SampleOffsets(1.0f / BufferSize.X, 1.0f / BufferSize.Y);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(),SampleOffsetsParameter,SampleOffsets);
		SceneTextureParams.Set(RHICmdList, RHICmdList.GetBoundPixelShader(), View.FeatureLevel, ESceneTextureSetupMode::All);
	}

private:

	LAYOUT_FIELD(FLightShaftPixelShaderParameters, LightShaftParameters);
	LAYOUT_FIELD(FShaderParameter, SampleOffsetsParameter);
	LAYOUT_FIELD(FSceneTextureShaderParameters, SceneTextureParams);
};

#define IMPLEMENT_LSDOWNSAMPLE_PIXELSHADER_TYPE(LightType,DownsampleValue) \
	typedef TDownsampleLightShaftsPixelShader<LightType, DownsampleValue> TDownsampleLightShaftsPixelShader##LightType##DownsampleValue; \
	IMPLEMENT_SHADER_TYPE(template<>,TDownsampleLightShaftsPixelShader##LightType##DownsampleValue,TEXT("/Engine/Private/LightShaftShader.usf"),TEXT("DownsampleLightShaftsPixelMain"),SF_Pixel);

IMPLEMENT_LSDOWNSAMPLE_PIXELSHADER_TYPE(LightType_Point, true);
IMPLEMENT_LSDOWNSAMPLE_PIXELSHADER_TYPE(LightType_Spot, true);
IMPLEMENT_LSDOWNSAMPLE_PIXELSHADER_TYPE(LightType_Directional, true);
IMPLEMENT_LSDOWNSAMPLE_PIXELSHADER_TYPE(LightType_Point, false);
IMPLEMENT_LSDOWNSAMPLE_PIXELSHADER_TYPE(LightType_Spot, false);
IMPLEMENT_LSDOWNSAMPLE_PIXELSHADER_TYPE(LightType_Directional, false);

/*-----------------------------------------------------------------------------
	FBlurLightShaftsPixelShader
-----------------------------------------------------------------------------*/

class FBlurLightShaftsPixelShader : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FBlurLightShaftsPixelShader,Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); 
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("NUM_SAMPLES"), GLightShaftBlurNumSamples);
	}

	/** Default constructor. */
	FBlurLightShaftsPixelShader() {}

	/** Initialization constructor. */
	FBlurLightShaftsPixelShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
		RadialBlurParameters.Bind(Initializer.ParameterMap, TEXT("RadialBlurParameters"));
		LightShaftParameters.Bind(Initializer.ParameterMap);
	}

	/** Sets shader parameter values */
	void SetParameters(FRHICommandList& RHICmdList, const FLightSceneInfo* LightSceneInfo, const FViewInfo& View, int32 PassIndex, TRefCountPtr<IPooledRenderTarget>& PassSource)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundPixelShader(), View.ViewUniformBuffer);
		LightShaftParameters.SetParameters(RHICmdList, RHICmdList.GetBoundPixelShader(), LightSceneInfo, View, PassSource);

		const FVector4 Parameters(GLightShaftBlurNumSamples, GLightShaftFirstPassDistance, PassIndex);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), RadialBlurParameters, Parameters);
	}

private:

	LAYOUT_FIELD(FShaderParameter, RadialBlurParameters);
	LAYOUT_FIELD(FLightShaftPixelShaderParameters, LightShaftParameters);
};

IMPLEMENT_SHADER_TYPE(,FBlurLightShaftsPixelShader,TEXT("/Engine/Private/LightShaftShader.usf"),TEXT("BlurLightShaftsMain"),SF_Pixel);

/*-----------------------------------------------------------------------------
	FFinishOcclusionPixelShader
-----------------------------------------------------------------------------*/

class FFinishOcclusionPixelShader : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FFinishOcclusionPixelShader,Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); 
	}

	/** Default constructor. */
	FFinishOcclusionPixelShader() {}

	/** Initialization constructor. */
	FFinishOcclusionPixelShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
		LightShaftParameters.Bind(Initializer.ParameterMap);
	}

	/** Sets shader parameter values */
	void SetParameters(FRHICommandList& RHICmdList, const FLightSceneInfo* LightSceneInfo, const FViewInfo& View, TRefCountPtr<IPooledRenderTarget>& PassSource)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundPixelShader(), View.ViewUniformBuffer);
		LightShaftParameters.SetParameters(RHICmdList, RHICmdList.GetBoundPixelShader(), LightSceneInfo, View, PassSource);
	}

private:

	LAYOUT_FIELD(FLightShaftPixelShaderParameters, LightShaftParameters);
};

IMPLEMENT_SHADER_TYPE(,FFinishOcclusionPixelShader,TEXT("/Engine/Private/LightShaftShader.usf"),TEXT("FinishOcclusionMain"),SF_Pixel);

void AllocateOrReuseLightShaftRenderTarget(FRHICommandListImmediate& RHICmdList, TRefCountPtr<IPooledRenderTarget>& Target, const TCHAR* Name)
{
	if (!Target)
	{
		EPixelFormat LightShaftFilterBufferFormat = PF_FloatRGB;
		const FIntPoint BufferSize = FSceneRenderTargets::Get(RHICmdList).GetBufferSizeXY();
		FIntPoint LightShaftSize(FMath::Max<uint32>(BufferSize.X / GetLightShaftDownsampleFactor(), 1), FMath::Max<uint32>(BufferSize.Y / GetLightShaftDownsampleFactor(), 1));
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(LightShaftSize, LightShaftFilterBufferFormat, FClearValueBinding::White, TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable, false));
		Desc.AutoWritable = false;
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, Target, Name);

		FRHIRenderPassInfo RPInfo(Target->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::Clear_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("ClearLightShaft"));
		RHICmdList.EndRenderPass();
	}
}

/** Generates the downsampled light shaft mask for either occlusion or bloom.  This swaps input and output before returning. */
template<bool bDownsampleOcclusion>
void DownsamplePass(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, const FLightSceneInfo* LightSceneInfo, TRefCountPtr<IPooledRenderTarget>& LightShaftsSource, TRefCountPtr<IPooledRenderTarget>& LightShaftsDest)
{
	const FIntPoint BufferSize = FSceneRenderTargets::Get(RHICmdList).GetBufferSizeXY();
	const uint32 DownsampleFactor	= GetLightShaftDownsampleFactor();
	const FIntPoint FilterBufferSize = BufferSize / DownsampleFactor;
	const FIntRect DownsampledRect = FIntRect::DivideAndRoundUp(View.ViewRect, DownsampleFactor);
	const FIntPoint DownSampledXY = DownsampledRect.Min;
	const uint32 DownsampledSizeX = DownsampledRect.Width();
	const uint32 DownsampledSizeY = DownsampledRect.Height();

	FRHIRenderPassInfo RPInfo(LightShaftsDest->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::Load_Store);
	TransitionRenderPassTargets(RHICmdList, RPInfo);
	RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, FSceneRenderTargets::Get(RHICmdList).GetSceneColorTexture());
	RHICmdList.BeginRenderPass(RPInfo, TEXT("DownsampleLightshaftMask"));
	{
		RHICmdList.SetViewport(DownSampledXY.X, DownSampledXY.Y, 0.0f, DownSampledXY.X + DownsampledSizeX, DownSampledXY.Y + DownsampledSizeY, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		// No depth tests, no backface culling.
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		// Set shaders and texture
		TShaderMapRef<FDownsampleLightShaftsVertexShader> DownsampleLightShaftsVertexShader(View.ShaderMap);
		TRefCountPtr<IPooledRenderTarget> UnusedRT;

		switch (LightSceneInfo->Proxy->GetLightType())
		{
		case LightType_Directional:
		{
			TShaderMapRef<TDownsampleLightShaftsPixelShader<LightType_Directional, bDownsampleOcclusion> > DownsampleLightShaftsPixelShader(View.ShaderMap);
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = DownsampleLightShaftsVertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = DownsampleLightShaftsPixelShader.GetPixelShader();
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
			DownsampleLightShaftsPixelShader->SetParameters(RHICmdList, LightSceneInfo, View, UnusedRT);
		}
		break;
		case LightType_Spot:
		{
			TShaderMapRef<TDownsampleLightShaftsPixelShader<LightType_Spot, bDownsampleOcclusion> > DownsampleLightShaftsPixelShader(View.ShaderMap);
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = DownsampleLightShaftsVertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = DownsampleLightShaftsPixelShader.GetPixelShader();
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
			DownsampleLightShaftsPixelShader->SetParameters(RHICmdList, LightSceneInfo, View, UnusedRT);
		}
		break;
		default:
		case LightType_Point:
		{
			TShaderMapRef<TDownsampleLightShaftsPixelShader<LightType_Point, bDownsampleOcclusion> > DownsampleLightShaftsPixelShader(View.ShaderMap);
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = DownsampleLightShaftsVertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = DownsampleLightShaftsPixelShader.GetPixelShader();
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
			DownsampleLightShaftsPixelShader->SetParameters(RHICmdList, LightSceneInfo, View, UnusedRT);
		}
		break;
		}

		DownsampleLightShaftsVertexShader->SetParameters(RHICmdList, View);

		// Downsample scene color and depth, and convert them into a bloom term and an occlusion masking term
		DrawRectangle(
			RHICmdList,
			0, 0,
			DownsampledSizeX, DownsampledSizeY,
			View.ViewRect.Min.X, View.ViewRect.Min.Y,
			View.ViewRect.Width(), View.ViewRect.Height(),
			FIntPoint(DownsampledSizeX, DownsampledSizeY),
			BufferSize,
			DownsampleLightShaftsVertexShader,
			EDRF_UseTriangleOptimization);
	}
	RHICmdList.EndRenderPass();
	RHICmdList.CopyToResolveTarget(LightShaftsDest->GetRenderTargetItem().TargetableTexture, LightShaftsDest->GetRenderTargetItem().ShaderResourceTexture, FResolveParams());

	Swap(LightShaftsSource, LightShaftsDest);
}

/** Applies Temporal AA to the light shaft source. */
void ApplyTemporalAA(
	FRHICommandListImmediate& RHICmdList,
	FViewInfo& View, 
	const TCHAR* HistoryRTName,
	FTemporalAAHistory* HistoryState,
	/** Source mask (for either occlusion or bloom). */
	TRefCountPtr<IPooledRenderTarget>& LightShaftsSource, 
	/** Output of Temporal AA for the next step in the pipeline. */
	TRefCountPtr<IPooledRenderTarget>& HistoryOutput)
{
	if (View.AntiAliasingMethod == AAM_TemporalAA && HistoryState && GLightShaftAllowTAA)
	{
		FRDGBuilder GraphBuilder(RHICmdList);

		FRDGTextureRef LightShaftSetup = GraphBuilder.RegisterExternalTexture(LightShaftsSource, TEXT("LightShaftsSource"));

		FSceneTextureParameters SceneTextures;
		SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

		FTAAPassParameters TAAParameters(View);
		TAAParameters.Pass = ETAAPassConfig::LightShaft;
		TAAParameters.SetupViewRect(View, /** ResolutionDivisor = */ 2);
		TAAParameters.SceneColorInput = LightShaftSetup;

		FTAAOutputs Outputs = AddTemporalAAPass(
			GraphBuilder,
			SceneTextures,
			View,
			TAAParameters,
			*HistoryState,
			HistoryState);

		GraphBuilder.QueueTextureExtraction(Outputs.SceneColor, &HistoryOutput);

		GraphBuilder.Execute();
	}
	else
	{
		// Temporal AA is disabled or there is no view state - pass through
		HistoryOutput = LightShaftsSource;
	}
}

/** Applies screen space radial blur passes. */
void ApplyRadialBlurPasses(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View, 
	const FLightSceneInfo* const LightSceneInfo, 
	/** First pass source - this will not be overwritten. */
	TRefCountPtr<IPooledRenderTarget>& FirstPassSource, 
	/** Subsequent pass source, will also contain the final result. */
	TRefCountPtr<IPooledRenderTarget>& LightShaftsSource, 
	/** First pass dest. */
	TRefCountPtr<IPooledRenderTarget>& LightShaftsDest)
{
	check(RHICmdList.IsOutsideRenderPass());

	TShaderMapRef<FScreenVS> ScreenVertexShader(View.ShaderMap);

	const FIntPoint BufferSize = FSceneRenderTargets::Get(RHICmdList).GetBufferSizeXY();
	const uint32 DownsampleFactor	= GetLightShaftDownsampleFactor();
	const FIntPoint FilterBufferSize = BufferSize / DownsampleFactor;
	const FIntPoint DownSampledXY = View.ViewRect.Min / DownsampleFactor;
	const uint32 DownsampledSizeX = View.ViewRect.Width() / DownsampleFactor;
	const uint32 DownsampledSizeY = View.ViewRect.Height() / DownsampleFactor;
	const uint32 NumPasses = FMath::Max(GLightShaftBlurPasses, 0);

	for (uint32 PassIndex = 0; PassIndex < NumPasses; PassIndex++)
	{
		FRHIRenderPassInfo RPInfo(LightShaftsDest->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::Load_Store);
		TransitionRenderPassTargets(RHICmdList, RPInfo);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("LightShaftPass"));
		{
			RHICmdList.SetViewport(0, 0, 0.0f, FilterBufferSize.X, FilterBufferSize.Y, 1.0f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			TShaderMapRef<FBlurLightShaftsPixelShader> BlurLightShaftsPixelShader(View.ShaderMap);

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = ScreenVertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = BlurLightShaftsPixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			TRefCountPtr<IPooledRenderTarget>& EffectiveSource = PassIndex == 0 ? FirstPassSource : LightShaftsSource;
			/// ?
			BlurLightShaftsPixelShader->SetParameters(RHICmdList, LightSceneInfo, View, PassIndex, EffectiveSource);

			{
				// Apply a radial blur to the bloom and occlusion mask
				DrawRectangle(
					RHICmdList,
					DownSampledXY.X, DownSampledXY.Y,
					DownsampledSizeX, DownsampledSizeY,
					DownSampledXY.X, DownSampledXY.Y,
					DownsampledSizeX, DownsampledSizeY,
					FilterBufferSize, FilterBufferSize,
					ScreenVertexShader,
					EDRF_UseTriangleOptimization);
			}
		}
		RHICmdList.EndRenderPass();
		RHICmdList.CopyToResolveTarget(LightShaftsDest->GetRenderTargetItem().TargetableTexture, LightShaftsDest->GetRenderTargetItem().ShaderResourceTexture, FResolveParams());

		// Swap input and output for the next pass
		Swap(LightShaftsSource, LightShaftsDest);
	}
}

void FinishOcclusionTerm(FRHICommandList& RHICmdList, const FViewInfo& View, const FLightSceneInfo* const LightSceneInfo, TRefCountPtr<IPooledRenderTarget>& LightShaftsSource, TRefCountPtr<IPooledRenderTarget>& LightShaftsDest)
{
	check(RHICmdList.IsOutsideRenderPass());

	TShaderMapRef<FScreenVS> ScreenVertexShader(View.ShaderMap);

	const FIntPoint BufferSize = FSceneRenderTargets::Get(RHICmdList).GetBufferSizeXY();
	const uint32 DownsampleFactor	= GetLightShaftDownsampleFactor();
	const FIntPoint FilterBufferSize = BufferSize / DownsampleFactor;
	const FIntPoint DownSampledXY = View.ViewRect.Min / DownsampleFactor;
	const uint32 DownsampledSizeX = View.ViewRect.Width() / DownsampleFactor;
	const uint32 DownsampledSizeY = View.ViewRect.Height() / DownsampleFactor;

	FRHIRenderPassInfo RPInfo(LightShaftsDest->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::Load_Store);
	TransitionRenderPassTargets(RHICmdList, RPInfo);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("FinishOcclusionTerm"));
	{
		RHICmdList.SetViewport(0, 0, 0.0f, FilterBufferSize.X, FilterBufferSize.Y, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		TShaderMapRef<FFinishOcclusionPixelShader> MaskOcclusionTermPixelShader(View.ShaderMap);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = ScreenVertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = MaskOcclusionTermPixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
		/// ?
		MaskOcclusionTermPixelShader->SetParameters(RHICmdList, LightSceneInfo, View, LightShaftsSource);

		{
			// Apply a radial blur to the bloom and occlusion mask
			DrawRectangle(
				RHICmdList,
				DownSampledXY.X, DownSampledXY.Y,
				DownsampledSizeX, DownsampledSizeY,
				DownSampledXY.X, DownSampledXY.Y,
				DownsampledSizeX, DownsampledSizeY,
				FilterBufferSize, FilterBufferSize,
				ScreenVertexShader,
				EDRF_UseTriangleOptimization);
		}
	}
	RHICmdList.EndRenderPass();
	RHICmdList.CopyToResolveTarget(LightShaftsDest->GetRenderTargetItem().TargetableTexture, LightShaftsDest->GetRenderTargetItem().ShaderResourceTexture, FResolveParams());
}

bool DoesViewFamilyAllowLightShafts(const FSceneViewFamily& ViewFamily)
{
	return GLightShafts
		&& ViewFamily.EngineShowFlags.LightShafts
		&& ViewFamily.EngineShowFlags.Lighting
		&& !ViewFamily.UseDebugViewPS()
		&& !(ViewFamily.EngineShowFlags.VisualizeDOF)
		&& !(ViewFamily.EngineShowFlags.VisualizeBuffer)
		&& !(ViewFamily.EngineShowFlags.VisualizeHDR)
		&& !(ViewFamily.EngineShowFlags.VisualizeMotionBlur);
}

bool ShouldRenderLightShaftsForLight(const FViewInfo& View, const FLightSceneInfo* LightSceneInfo)
{
	const FVector WorldSpaceBlurOrigin = LightSceneInfo->Proxy->GetLightPositionForLightShafts(View.ViewMatrices.GetViewOrigin());

	// Transform into post projection space
	FVector4 ProjectedBlurOrigin = View.ViewMatrices.GetViewProjectionMatrix().TransformPosition(WorldSpaceBlurOrigin);

	const float DistanceToBlurOrigin = (View.ViewMatrices.GetViewOrigin() - WorldSpaceBlurOrigin).Size() + PointLightFadeDistanceIncrease;

	// Don't render if the light's origin is behind the view
	return ProjectedBlurOrigin.W > 0.0f
		// Don't render point lights that have completely faded out
		&& (LightSceneInfo->Proxy->GetLightType() == LightType_Directional 
			|| DistanceToBlurOrigin < LightSceneInfo->Proxy->GetRadius() * PointLightRadiusFadeFactor);
}

/** Renders light shafts. */
void FDeferredShadingSceneRenderer::RenderLightShaftOcclusion(FRHICommandListImmediate& RHICmdList, FLightShaftsOutput& Output)
{
	if (DoesViewFamilyAllowLightShafts(ViewFamily))
	{
		TRefCountPtr<IPooledRenderTarget> LightShafts0;
		TRefCountPtr<IPooledRenderTarget> LightShafts1;

		for (TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights); LightIt; ++LightIt)
		{
			const FLightSceneInfo* const LightSceneInfo = LightIt->LightSceneInfo;

			float OcclusionMaskDarkness;
			float OcclusionDepthRange;
			const bool bEnableOcclusion = LightSceneInfo->Proxy->GetLightShaftOcclusionParameters(OcclusionMaskDarkness, OcclusionDepthRange);

			if (bEnableOcclusion && LightSceneInfo->Proxy->GetLightType() == LightType_Directional)
			{
				bool bWillRenderLightShafts = false;

				for (int ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					FViewInfo& View = Views[ViewIndex];
					
					if (ShouldRenderLightShaftsForLight(View, LightSceneInfo))
					{
						bWillRenderLightShafts = true;
					}
				}

				if (bWillRenderLightShafts)
				{
					bool bAnyLightShaftsRendered = false;

					for (int ViewIndex = 0;ViewIndex < Views.Num();ViewIndex++)
					{
						FViewInfo& View = Views[ViewIndex];
					
						SCOPED_DRAW_EVENTF(RHICmdList, RenderLightShaftOcclusion, TEXT("RenderLightShaftOcclusion %dx%d (multiple passes)"), View.ViewRect.Width(), View.ViewRect.Height());

						if (ShouldRenderLightShaftsForLight(View, LightSceneInfo))
						{
							bAnyLightShaftsRendered = true;
						}
					}

					if (bAnyLightShaftsRendered)
					{
						// Allocate light shaft render targets on demand, using the pool
						// Need two targets to ping pong between
						AllocateOrReuseLightShaftRenderTarget(RHICmdList, LightShafts0, TEXT("LightShafts0"));
						AllocateOrReuseLightShaftRenderTarget(RHICmdList, LightShafts1, TEXT("LightShafts1"));

						for (int ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
						{
							FViewInfo& View = Views[ViewIndex];
							
							SCOPED_GPU_MASK(RHICmdList, View.GPUMask);
							SCOPED_DRAW_EVENTF(RHICmdList, RenderLightShaftOcclusion, TEXT("RenderLightShaftOcclusion %dx%d (multiple passes)"), View.ViewRect.Width(), View.ViewRect.Height());
		
							if (ShouldRenderLightShaftsForLight(View, LightSceneInfo))
							{
								INC_DWORD_STAT(STAT_LightShaftsLights);
		
								// Create a downsampled occlusion mask from scene depth, result will be in LightShafts0
								DownsamplePass<true>(RHICmdList, View, LightSceneInfo, LightShafts0, LightShafts1);
		
								FSceneViewState* ViewState = (FSceneViewState*)View.State;
								// Find the previous frame's occlusion mask
								TRefCountPtr<IPooledRenderTarget> HistoryOutput;
		
								// Apply temporal AA to the occlusion mask
								// Result will be in HistoryOutput
								ApplyTemporalAA(RHICmdList, View, TEXT("LSOcclusionHistory"), ViewState ? &ViewState->LightShaftOcclusionHistory : nullptr, LightShafts0, HistoryOutput);
		
								// Apply radial blur passes
								// Send HistoryOutput in as the first pass input only, so it will not be overwritten by any subsequent passes, since it is needed for next frame
								ApplyRadialBlurPasses(RHICmdList, View, LightSceneInfo, HistoryOutput, LightShafts0, LightShafts1);
		
								// Apply post-blur masking
								FinishOcclusionTerm(RHICmdList, View, LightSceneInfo, LightShafts0, LightShafts1);
		
								//@todo - different views could have different result render targets
								Output.LightShaftOcclusion = LightShafts1;
							}
						}
					}				
				}
			}
		}
	}
}

/*-----------------------------------------------------------------------------
	FApplyLightShaftsPixelShader
-----------------------------------------------------------------------------*/

class FApplyLightShaftsPixelShader : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FApplyLightShaftsPixelShader,Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); 
	}

	/** Default constructor. */
	FApplyLightShaftsPixelShader() {}

	/** Initialization constructor. */
	FApplyLightShaftsPixelShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
		SourceTextureParameter.Bind(Initializer.ParameterMap, TEXT("SourceTexture"));
		SourceTextureSamplerParameter.Bind(Initializer.ParameterMap, TEXT("SourceTextureSampler"));
		UVMinMaxParameter.Bind(Initializer.ParameterMap, TEXT("UVMinMax"));
	}

	/** Sets shader parameter values */
	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View, TRefCountPtr<IPooledRenderTarget>& LightShaftOcclusion, const FIntPoint& FilterBufferSize, const FIntRect& DownSampleRect)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundPixelShader(), View.ViewUniformBuffer);

		SetTextureParameter(
			RHICmdList, 
			RHICmdList.GetBoundPixelShader(),
			SourceTextureParameter, SourceTextureSamplerParameter,
			TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
			LightShaftOcclusion->GetRenderTargetItem().ShaderResourceTexture
			);

		FVector4 UVMinMaxValue(
			(DownSampleRect.Min.X + 0.5f) / float(FilterBufferSize.X),
			(DownSampleRect.Min.Y + 0.5f) / float(FilterBufferSize.Y),
			(DownSampleRect.Max.X - 0.5f) / float(FilterBufferSize.X),
			(DownSampleRect.Max.Y - 0.5f) / float(FilterBufferSize.Y));

		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), UVMinMaxParameter, UVMinMaxValue);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, SourceTextureParameter);
	LAYOUT_FIELD(FShaderResourceParameter, SourceTextureSamplerParameter);
	LAYOUT_FIELD(FShaderParameter, UVMinMaxParameter);
};

IMPLEMENT_SHADER_TYPE(,FApplyLightShaftsPixelShader,TEXT("/Engine/Private/LightShaftShader.usf"),TEXT("ApplyLightShaftsPixelMain"),SF_Pixel);

void ApplyLightShaftBloom(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, const FSceneRenderer& Renderer, const FLightSceneInfo* const LightSceneInfo, TRefCountPtr<IPooledRenderTarget>& LightShaftsSource)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	bool bUseSeparateTranslucency = false;
	if (View.Family->AllowTranslucencyAfterDOF() && GLightShaftRenderAfterDOF)
	{
		// If the separate translucency RT hasn't been allocated yet this frame, make sure to mark the call to BeginRenderingSeparateTranslucency as being the first in this frame
		SceneContext.BeginRenderingSeparateTranslucency(RHICmdList, View, Renderer, !SceneContext.SeparateTranslucencyRT.IsValid());
		bUseSeparateTranslucency = true;
	}
	else
	{
		SceneContext.BeginRenderingSceneColor(RHICmdList, ESimpleRenderTargetMode::EExistingColorAndDepth, FExclusiveDepthStencil::DepthRead_StencilWrite);
	}

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
	GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

	TShaderMapRef<FScreenVS> ScreenVertexShader(View.ShaderMap);
	TShaderMapRef<FApplyLightShaftsPixelShader> ApplyLightShaftsPixelShader(View.ShaderMap);

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = ScreenVertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = ApplyLightShaftsPixelShader.GetPixelShader();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

	const FIntPoint BufferSize = SceneContext.GetBufferSizeXY();
	const uint32 DownsampleFactor = GetLightShaftDownsampleFactor();
	const FIntPoint FilterBufferSize = SceneContext.GetBufferSizeXY() / DownsampleFactor;
	const FIntPoint DownSampledXY = View.ViewRect.Min / DownsampleFactor;
	const uint32 DownsampledSizeX = View.ViewRect.Width() / DownsampleFactor;
	const uint32 DownsampledSizeY = View.ViewRect.Height() / DownsampleFactor;

	FIntRect DownSampleRect;
	DownSampleRect.Min = DownSampledXY;
	DownSampleRect.Max.X = DownSampledXY.X + DownsampledSizeX;
	DownSampleRect.Max.Y = DownSampledXY.Y + DownsampledSizeY;

	/// ?
	ApplyLightShaftsPixelShader->SetParameters(RHICmdList, View, LightShaftsSource, FilterBufferSize, DownSampleRect);

	DrawRectangle( 
		RHICmdList,
		0, 0, 
		View.ViewRect.Width(), View.ViewRect.Height(),
		DownSampledXY.X, DownSampledXY.Y, 
		DownsampledSizeX, DownsampledSizeY,
		FIntPoint(View.ViewRect.Width(), View.ViewRect.Height()), FilterBufferSize,
		ScreenVertexShader,
		EDRF_UseTriangleOptimization);

	if (bUseSeparateTranslucency)
	{
		RHICmdList.EndRenderPass();
		SceneContext.ResolveSeparateTranslucency(RHICmdList, View);
	}
	else
	{
		SceneContext.FinishRenderingSceneColor(RHICmdList);
	}
}

void FSceneViewState::TrimHistoryRenderTargets(const FScene* Scene)
{
	for (TMap<const ULightComponent*, FTemporalAAHistory >::TIterator It(LightShaftBloomHistoryRTs); It; ++It)
	{
		bool bLightIsUsed = false;

		for (TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights); LightIt; ++LightIt)
		{
			const FLightSceneInfo* const LightSceneInfo = LightIt->LightSceneInfo;

			if (LightSceneInfo->Proxy->GetLightComponent() == It.Key())
			{
				bLightIsUsed = true;
				break;
			}
		}

		if (!bLightIsUsed)
		{
			// Remove references to render targets for lights that are no longer in the scene
			// This has to be done every frame instead of at light deregister time because the view states are not known by FScene
			It.RemoveCurrent();
		}
	}
}

void FDeferredShadingSceneRenderer::RenderLightShaftBloom(FRHICommandListImmediate& RHICmdList)
{
	if (DoesViewFamilyAllowLightShafts(ViewFamily))
	{
		TRefCountPtr<IPooledRenderTarget> LightShafts0;
		TRefCountPtr<IPooledRenderTarget> LightShafts1;

		for (TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights); LightIt; ++LightIt)
		{
			const FLightSceneInfo* const LightSceneInfo = LightIt->LightSceneInfo;

			if (LightSceneInfo->bEnableLightShaftBloom)
			{
				bool bWillRenderLightShafts = false;

				for (int ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					FViewInfo& View = Views[ViewIndex];

					if (ShouldRenderLightShaftsForLight(View, LightSceneInfo))
					{
						bWillRenderLightShafts = true;
					}
				}

				if (bWillRenderLightShafts)
				{
					// Allocate light shaft render targets on demand, using the pool
					AllocateOrReuseLightShaftRenderTarget(RHICmdList, LightShafts0, TEXT("LightShafts0"));
					AllocateOrReuseLightShaftRenderTarget(RHICmdList, LightShafts1, TEXT("LightShafts1"));

					for (int ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
					{
						FViewInfo& View = Views[ViewIndex];

						SCOPED_GPU_MASK(RHICmdList, View.GPUMask);
						SCOPED_DRAW_EVENTF(RHICmdList, RenderLightShaftBloom, TEXT("RenderLightShaftBloom %dx%d"), View.ViewRect.Width(), View.ViewRect.Height());

						if (ShouldRenderLightShaftsForLight(View, LightSceneInfo))
						{
							INC_DWORD_STAT(STAT_LightShaftsLights);

							// Generate the bloom source from scene color, masked by depth and downsampled
							DownsamplePass<false>(RHICmdList, View, LightSceneInfo, LightShafts0, LightShafts1);

							FSceneViewState* ViewState = (FSceneViewState*)View.State;
							FTemporalAAHistory* HistoryState = nullptr;

							if (ViewState)
							{
								// Find the previous frame's bloom source for this light
								HistoryState = &ViewState->LightShaftBloomHistoryRTs.FindOrAdd(LightSceneInfo->Proxy->GetLightComponent());
							}

							TRefCountPtr<IPooledRenderTarget> HistoryOutput;

							// Apply temporal AA to the occlusion mask
							// Result will be in HistoryOutput
							ApplyTemporalAA(RHICmdList, View, TEXT("LSBloomHistory"), HistoryState, LightShafts0, HistoryOutput);

							// Apply radial blur passes
							// Send HistoryOutput in as the first pass input only, so it will not be overwritten by any subsequent passes, since it is needed for next frame
							ApplyRadialBlurPasses(RHICmdList, View, LightSceneInfo, HistoryOutput, LightShafts0, LightShafts1);
						
							// Add light shaft bloom to scene color in full res
							ApplyLightShaftBloom(RHICmdList, View, *this, LightSceneInfo, LightShafts0);
						}
					}
				}
			}
		}
	}
}
