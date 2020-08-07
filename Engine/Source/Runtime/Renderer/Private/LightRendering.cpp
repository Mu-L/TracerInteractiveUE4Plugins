// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LightRendering.cpp: Light rendering implementation.
=============================================================================*/

#include "LightRendering.h"
#include "RendererModule.h"
#include "DeferredShadingRenderer.h"
#include "LightPropagationVolume.h"
#include "ScenePrivate.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PipelineStateCache.h"
#include "ClearQuad.h"
#include "Engine/SubsurfaceProfile.h"
#include "ShowFlags.h"
#include "VisualizeTexture.h"
#include "RayTracing/RaytracingOptions.h"
#include "SceneTextureParameters.h"
#include "HairStrands/HairStrandsRendering.h"
#include "ScreenPass.h"
#include "SkyAtmosphereRendering.h"

// ENABLE_DEBUG_DISCARD_PROP is used to test the lighting code by allowing to discard lights to see how performance scales
// It ought never to be enabled in a shipping build, and is probably only really useful when woring on the shading code.
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	#define ENABLE_DEBUG_DISCARD_PROP 1
#else // (UE_BUILD_SHIPPING || UE_BUILD_TEST)
	#define ENABLE_DEBUG_DISCARD_PROP 0
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

DECLARE_GPU_STAT(Lights);

IMPLEMENT_TYPE_LAYOUT(FLightFunctionSharedParameters);
IMPLEMENT_TYPE_LAYOUT(FStencilingGeometryShaderParameters);
IMPLEMENT_TYPE_LAYOUT(FOnePassPointShadowProjectionShaderParameters);
IMPLEMENT_TYPE_LAYOUT(FShadowProjectionShaderParameters);

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FDeferredLightUniformStruct, "DeferredLightUniforms");

extern int32 GUseTranslucentLightingVolumes;
ENGINE_API IPooledRenderTarget* GetSubsufaceProfileTexture_RT(FRHICommandListImmediate& RHICmdList);


static int32 GAllowDepthBoundsTest = 1;
static FAutoConsoleVariableRef CVarAllowDepthBoundsTest(
	TEXT("r.AllowDepthBoundsTest"),
	GAllowDepthBoundsTest,
	TEXT("If true, use enable depth bounds test when rendering defered lights.")
	);

static int32 bAllowSimpleLights = 1;
static FAutoConsoleVariableRef CVarAllowSimpleLights(
	TEXT("r.AllowSimpleLights"),
	bAllowSimpleLights,
	TEXT("If true, we allow simple (ie particle) lights")
);

static int32 GRayTracingShadows = 1;
static FAutoConsoleVariableRef CVarRayTracingOcclusion(
	TEXT("r.RayTracing.Shadows"),
	GRayTracingShadows,
	TEXT("0: use traditional rasterized shadow map\n")
	TEXT("1: use ray tracing shadows (default)"),
	ECVF_RenderThreadSafe
);

static int32 GShadowRayTracingSamplesPerPixel = 1;
static FAutoConsoleVariableRef CVarShadowRayTracingSamplesPerPixel(
	TEXT("r.RayTracing.Shadow.SamplesPerPixel"),
	GShadowRayTracingSamplesPerPixel,
	TEXT("Sets the samples-per-pixel for directional light occlusion (default = 1)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarShadowUseDenoiser(
	TEXT("r.Shadow.Denoiser"),
	2,
	TEXT("Choose the denoising algorithm.\n")
	TEXT(" 0: Disabled (default);\n")
	TEXT(" 1: Forces the default denoiser of the renderer;\n")
	TEXT(" 2: GScreenSpaceDenoiser witch may be overriden by a third party plugin.\n"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMaxShadowDenoisingBatchSize(
	TEXT("r.Shadow.Denoiser.MaxBatchSize"), 4,
	TEXT("Maximum number of shadow to denoise at the same time."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMaxShadowRayTracingBatchSize(
	TEXT("r.RayTracing.Shadow.MaxBatchSize"), 8,
	TEXT("Maximum number of shadows to trace at the same time."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarAllowClearLightSceneExtentsOnly(
	TEXT("r.AllowClearLightSceneExtentsOnly"), 1,
	TEXT(""),
	ECVF_RenderThreadSafe);


#if ENABLE_DEBUG_DISCARD_PROP
static float GDebugLightDiscardProp = 0.0f;
static FAutoConsoleVariableRef CVarDebugLightDiscardProp(
	TEXT("r.DebugLightDiscardProp"),
	GDebugLightDiscardProp,
	TEXT("[0,1]: Proportion of lights to discard for debug/performance profiling purposes.")
);
#endif // ENABLE_DEBUG_DISCARD_PROP



#if RHI_RAYTRACING
bool ShouldRenderRayTracingShadows(const FLightSceneProxy& LightProxy)
{
	const int32 ForceAllRayTracingEffects = GetForceRayTracingEffectsCVarValue();
	const bool bRTShadowsEnabled = (ForceAllRayTracingEffects > 0 || (GRayTracingShadows > 0 && ForceAllRayTracingEffects < 0));

	return IsRayTracingEnabled() && bRTShadowsEnabled && LightProxy.CastsRaytracedShadow();
}

bool ShouldRenderRayTracingShadows(const FLightSceneInfoCompact& LightInfo)
{
	const int32 ForceAllRayTracingEffects = GetForceRayTracingEffectsCVarValue();
	const bool bRTShadowsEnabled = (ForceAllRayTracingEffects > 0 || (GRayTracingShadows > 0 && ForceAllRayTracingEffects < 0));

	return IsRayTracingEnabled() && bRTShadowsEnabled && LightInfo.bCastRaytracedShadow;
}
#endif // RHI_RAYTRACING


FLightOcclusionType GetLightOcclusionType(const FLightSceneProxy& Proxy)
{
#if RHI_RAYTRACING
	return ShouldRenderRayTracingShadows(Proxy) ? FLightOcclusionType::Raytraced : FLightOcclusionType::Shadowmap;
#else
	return FLightOcclusionType::Shadowmap;
#endif
}

FLightOcclusionType GetLightOcclusionType(const FLightSceneInfoCompact& LightInfo)
{
#if RHI_RAYTRACING
	return ShouldRenderRayTracingShadows(LightInfo) ? FLightOcclusionType::Raytraced : FLightOcclusionType::Shadowmap;
#else
	return FLightOcclusionType::Shadowmap;
#endif
}

float GetLightFadeFactor(const FSceneView& View, const FLightSceneProxy* Proxy)
{
	// Distance fade
	FSphere Bounds = Proxy->GetBoundingSphere();

	const float DistanceSquared = (Bounds.Center - View.ViewMatrices.GetViewOrigin()).SizeSquared();
	extern float GMinScreenRadiusForLights;
	float SizeFade = FMath::Square(FMath::Min(0.0002f, GMinScreenRadiusForLights / Bounds.W) * View.LODDistanceFactor) * DistanceSquared;
	SizeFade = FMath::Clamp(6.0f - 6.0f * SizeFade, 0.0f, 1.0f);

	extern float GLightMaxDrawDistanceScale;
	float MaxDist = Proxy->GetMaxDrawDistance() * GLightMaxDrawDistanceScale;
	float Range = Proxy->GetFadeRange();
	float DistanceFade = MaxDist ? (MaxDist - FMath::Sqrt(DistanceSquared)) / Range : 1.0f;
	DistanceFade = FMath::Clamp(DistanceFade, 0.0f, 1.0f);
	return SizeFade * DistanceFade;
}

void StencilingGeometry::DrawSphere(FRHICommandList& RHICmdList)
{
	RHICmdList.SetStreamSource(0, StencilingGeometry::GStencilSphereVertexBuffer.VertexBufferRHI, 0);
	RHICmdList.DrawIndexedPrimitive(StencilingGeometry::GStencilSphereIndexBuffer.IndexBufferRHI, 0, 0,
		StencilingGeometry::GStencilSphereVertexBuffer.GetVertexCount(), 0,
		StencilingGeometry::GStencilSphereIndexBuffer.GetIndexCount() / 3, 1);
}

void StencilingGeometry::DrawVectorSphere(FRHICommandList& RHICmdList)
{
	RHICmdList.SetStreamSource(0, StencilingGeometry::GStencilSphereVectorBuffer.VertexBufferRHI, 0);
	RHICmdList.DrawIndexedPrimitive(StencilingGeometry::GStencilSphereIndexBuffer.IndexBufferRHI, 0, 0,
									StencilingGeometry::GStencilSphereVectorBuffer.GetVertexCount(), 0,
									StencilingGeometry::GStencilSphereIndexBuffer.GetIndexCount() / 3, 1);
}

void StencilingGeometry::DrawCone(FRHICommandList& RHICmdList)
{
	// No Stream Source needed since it will generate vertices on the fly
	RHICmdList.SetStreamSource(0, StencilingGeometry::GStencilConeVertexBuffer.VertexBufferRHI, 0);

	RHICmdList.DrawIndexedPrimitive(StencilingGeometry::GStencilConeIndexBuffer.IndexBufferRHI, 0, 0,
		FStencilConeIndexBuffer::NumVerts, 0, StencilingGeometry::GStencilConeIndexBuffer.GetIndexCount() / 3, 1);
}

/** The stencil sphere vertex buffer. */
TGlobalResource<StencilingGeometry::TStencilSphereVertexBuffer<18, 12, FVector4> > StencilingGeometry::GStencilSphereVertexBuffer;
TGlobalResource<StencilingGeometry::TStencilSphereVertexBuffer<18, 12, FVector> > StencilingGeometry::GStencilSphereVectorBuffer;

/** The stencil sphere index buffer. */
TGlobalResource<StencilingGeometry::TStencilSphereIndexBuffer<18, 12> > StencilingGeometry::GStencilSphereIndexBuffer;

TGlobalResource<StencilingGeometry::TStencilSphereVertexBuffer<4, 4, FVector4> > StencilingGeometry::GLowPolyStencilSphereVertexBuffer;
TGlobalResource<StencilingGeometry::TStencilSphereIndexBuffer<4, 4> > StencilingGeometry::GLowPolyStencilSphereIndexBuffer;

/** The (dummy) stencil cone vertex buffer. */
TGlobalResource<StencilingGeometry::FStencilConeVertexBuffer> StencilingGeometry::GStencilConeVertexBuffer;

/** The stencil cone index buffer. */
TGlobalResource<StencilingGeometry::FStencilConeIndexBuffer> StencilingGeometry::GStencilConeIndexBuffer;


// Implement a version for directional lights, and a version for point / spot lights
IMPLEMENT_SHADER_TYPE(template<>,TDeferredLightVS<false>,TEXT("/Engine/Private/DeferredLightVertexShaders.usf"),TEXT("DirectionalVertexMain"),SF_Vertex);
IMPLEMENT_SHADER_TYPE(template<>,TDeferredLightVS<true>,TEXT("/Engine/Private/DeferredLightVertexShaders.usf"),TEXT("RadialVertexMain"),SF_Vertex);


struct FRenderLightParams
{
	// Precompute transmittance
	FShaderResourceViewRHIRef DeepShadow_TransmittanceMaskBuffer = nullptr;
	uint32 DeepShadow_TransmittanceMaskBufferMaxCount = 0;
	
	// Visibility buffer data
	IPooledRenderTarget* HairCategorizationTexture = nullptr;
	IPooledRenderTarget* HairVisibilityNodeOffsetAndCount = nullptr;
	IPooledRenderTarget* HairVisibilityNodeCount = nullptr;
	FShaderResourceViewRHIRef HairVisibilityNodeCoordsSRV = nullptr;
	FShaderResourceViewRHIRef HairVisibilityNodeDataSRV = nullptr;

	IPooledRenderTarget* ScreenShadowMaskSubPixelTexture = nullptr;
};


class TDeferredLightHairVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TDeferredLightHairVS, Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_HAIR"), 1);
	}

	TDeferredLightHairVS() {}
	TDeferredLightHairVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		MaxViewportResolution.Bind(Initializer.ParameterMap, TEXT("MaxViewportResolution"));
		HairVisibilityNodeCount.Bind(Initializer.ParameterMap, TEXT("HairVisibilityNodeCount"));
	}

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View, const FHairStrandsVisibilityData* VisibilityData)
	{
		FRHIVertexShader* ShaderRHI = RHICmdList.GetBoundVertexShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		if (!VisibilityData)
		{
			return;
		}

		if (HairVisibilityNodeCount.IsBound() && VisibilityData->NodeCount)
		{
			SetTextureParameter(RHICmdList,	ShaderRHI, HairVisibilityNodeCount, VisibilityData->NodeCount->GetRenderTargetItem().ShaderResourceTexture);
		}

		SetShaderValue(RHICmdList, ShaderRHI, MaxViewportResolution, VisibilityData->SampleLightingViewportResolution);
	}

private:
	LAYOUT_FIELD(FShaderParameter, MaxViewportResolution);
	LAYOUT_FIELD(FShaderResourceParameter, HairVisibilityNodeCount);
};

IMPLEMENT_SHADER_TYPE(, TDeferredLightHairVS, TEXT("/Engine/Private/DeferredLightVertexShaders.usf"), TEXT("HairVertexMain"), SF_Vertex);


enum class ELightSourceShape
{
	Directional,
	Capsule,
	Rect,

	MAX
};

/** A pixel shader for rendering the light in a deferred pass. */
class FDeferredLightPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FDeferredLightPS, Global)

	class FSourceShapeDim		: SHADER_PERMUTATION_ENUM_CLASS("LIGHT_SOURCE_SHAPE", ELightSourceShape);
	class FSourceTextureDim		: SHADER_PERMUTATION_BOOL("USE_SOURCE_TEXTURE");
	class FIESProfileDim		: SHADER_PERMUTATION_BOOL("USE_IES_PROFILE");
	class FInverseSquaredDim	: SHADER_PERMUTATION_BOOL("INVERSE_SQUARED_FALLOFF");
	class FVisualizeCullingDim	: SHADER_PERMUTATION_BOOL("VISUALIZE_LIGHT_CULLING");
	class FLightingChannelsDim	: SHADER_PERMUTATION_BOOL("USE_LIGHTING_CHANNELS");
	class FTransmissionDim		: SHADER_PERMUTATION_BOOL("USE_TRANSMISSION");
	class FHairLighting			: SHADER_PERMUTATION_INT("USE_HAIR_LIGHTING", 3);
	class FAtmosphereTransmittance: SHADER_PERMUTATION_BOOL("USE_ATMOSPHERE_TRANSMITTANCE");

	using FPermutationDomain = TShaderPermutationDomain<
		FSourceShapeDim,
		FSourceTextureDim,
		FIESProfileDim,
		FInverseSquaredDim,
		FVisualizeCullingDim,
		FLightingChannelsDim,
		FTransmissionDim,
		FHairLighting,
		FAtmosphereTransmittance>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if( PermutationVector.Get< FSourceShapeDim >() == ELightSourceShape::Directional && (
			PermutationVector.Get< FIESProfileDim >() ||
			PermutationVector.Get< FInverseSquaredDim >() ) )
		{
			return false;
		}

		if (PermutationVector.Get< FSourceShapeDim >() != ELightSourceShape::Directional && PermutationVector.Get<FAtmosphereTransmittance>())
		{
			return false;
		}

		if( PermutationVector.Get< FSourceShapeDim >() == ELightSourceShape::Rect )
		{
			if(	!PermutationVector.Get< FInverseSquaredDim >() )
			{
				return false;
			}
		}
		else
		{
			if( PermutationVector.Get< FSourceTextureDim >() )
			{
				return false;
			}
		}

		if (PermutationVector.Get<FHairLighting>() && !IsHairStrandsSupported(Parameters.Platform))
		{
			return false;
		}

		if (PermutationVector.Get< FHairLighting >() == 2 && (
			PermutationVector.Get< FVisualizeCullingDim >() ||
			PermutationVector.Get< FTransmissionDim >()))
		{
			return false;
		}

		/*if( PermutationVector.Get< FVisualizeCullingDim >() && (
			PermutationVector.Get< FSourceShapeDim >() == ELightSourceShape::Rect ||
			PermutationVector.Get< FIESProfileDim >() ||
			PermutationVector.Get< FInverseSquaredDim >() ) )
		{
			return false;
		}*/

		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	FDeferredLightPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	:	FGlobalShader(Initializer)
	{
		SceneTextureParameters.Bind(Initializer);
		LightAttenuationTexture.Bind(Initializer.ParameterMap, TEXT("LightAttenuationTexture"));
		LightAttenuationTextureSampler.Bind(Initializer.ParameterMap, TEXT("LightAttenuationTextureSampler"));
		LTCMatTexture.Bind(Initializer.ParameterMap, TEXT("LTCMatTexture"));
		LTCMatSampler.Bind(Initializer.ParameterMap, TEXT("LTCMatSampler"));
		LTCAmpTexture.Bind(Initializer.ParameterMap, TEXT("LTCAmpTexture"));
		LTCAmpSampler.Bind(Initializer.ParameterMap, TEXT("LTCAmpSampler"));
		IESTexture.Bind(Initializer.ParameterMap, TEXT("IESTexture"));
		IESTextureSampler.Bind(Initializer.ParameterMap, TEXT("IESTextureSampler"));
		LightingChannelsTexture.Bind(Initializer.ParameterMap, TEXT("LightingChannelsTexture"));
		LightingChannelsSampler.Bind(Initializer.ParameterMap, TEXT("LightingChannelsSampler"));
		TransmissionProfilesTexture.Bind(Initializer.ParameterMap, TEXT("SSProfilesTexture"));
		TransmissionProfilesLinearSampler.Bind(Initializer.ParameterMap, TEXT("TransmissionProfilesLinearSampler"));

		HairTransmittanceBuffer.Bind(Initializer.ParameterMap, TEXT("HairTransmittanceBuffer"));
		HairTransmittanceBufferMaxCount.Bind(Initializer.ParameterMap, TEXT("HairTransmittanceBufferMaxCount"));
		ScreenShadowMaskSubPixelTexture.Bind(Initializer.ParameterMap, TEXT("ScreenShadowMaskSubPixelTexture")); // TODO hook the shader itself

		HairLUTTexture.Bind(Initializer.ParameterMap, TEXT("HairLUTTexture"));
		HairLUTSampler.Bind(Initializer.ParameterMap, TEXT("HairLUTSampler"));
		HairComponents.Bind(Initializer.ParameterMap, TEXT("HairComponents"));
		HairShadowMaskValid.Bind(Initializer.ParameterMap, TEXT("HairShadowMaskValid"));
		HairDualScatteringRoughnessOverride.Bind(Initializer.ParameterMap, TEXT("HairDualScatteringRoughnessOverride"));

		HairCategorizationTexture.Bind(Initializer.ParameterMap, TEXT("HairCategorizationTexture"));
		HairVisibilityNodeOffsetAndCount.Bind(Initializer.ParameterMap, TEXT("HairVisibilityNodeOffsetAndCount"));
		HairVisibilityNodeCoords.Bind(Initializer.ParameterMap, TEXT("HairVisibilityNodeCoords"));
		HairVisibilityNodeData.Bind(Initializer.ParameterMap, TEXT("HairVisibilityNodeData"));
	}

	FDeferredLightPS()
	{}

public:
	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FSceneView& View, 
		const FLightSceneInfo* LightSceneInfo, 
		IPooledRenderTarget* ScreenShadowMaskTexture, 
		FRenderLightParams* RenderLightParams)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();
		SetParametersBase(RHICmdList, ShaderRHI, View, ScreenShadowMaskTexture, LightSceneInfo->Proxy->GetIESTextureResource(), RenderLightParams);
		SetDeferredLightParameters(RHICmdList, ShaderRHI, GetUniformBufferParameter<FDeferredLightUniformStruct>(), LightSceneInfo, View);
	}

	void SetParametersSimpleLight(FRHICommandList& RHICmdList, const FSceneView& View, const FSimpleLightEntry& SimpleLight, const FSimpleLightPerViewEntry& SimpleLightPerViewData)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();
		SetParametersBase(RHICmdList, ShaderRHI, View, nullptr, nullptr, nullptr);
		SetSimpleDeferredLightParameters(RHICmdList, ShaderRHI, GetUniformBufferParameter<FDeferredLightUniformStruct>(), SimpleLight, SimpleLightPerViewData, View);
	}

private:

	void SetParametersBase(
		FRHICommandList& RHICmdList, 
		FRHIPixelShader* ShaderRHI, 
		const FSceneView& View, 
		IPooledRenderTarget* ScreenShadowMaskTexture, 
		FTexture* IESTextureResource, 
		FRenderLightParams* RenderLightParams)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI,View.ViewUniformBuffer);
		SceneTextureParameters.Set(RHICmdList, ShaderRHI, View.FeatureLevel, ESceneTextureSetupMode::All);

		FSceneRenderTargets& SceneRenderTargets = FSceneRenderTargets::Get(RHICmdList);

		if(LightAttenuationTexture.IsBound())
		{
			SetTextureParameter(
				RHICmdList,
				ShaderRHI,
				LightAttenuationTexture,
				LightAttenuationTextureSampler,
				TStaticSamplerState<SF_Point,AM_Wrap,AM_Wrap,AM_Wrap>::GetRHI(),
				ScreenShadowMaskTexture ? ScreenShadowMaskTexture->GetRenderTargetItem().ShaderResourceTexture : GWhiteTexture->TextureRHI
				);
		}

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

		{
			FRHITexture* TextureRHI = IESTextureResource ? IESTextureResource->TextureRHI : GSystemTextures.WhiteDummy->GetRenderTargetItem().TargetableTexture;

			SetTextureParameter(
				RHICmdList,
				ShaderRHI,
				IESTexture,
				IESTextureSampler,
				TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
				TextureRHI
				);
		}

		if( LightingChannelsTexture.IsBound() )
		{
			FRHITexture* LightingChannelsTextureRHI = SceneRenderTargets.LightingChannels ? SceneRenderTargets.LightingChannels->GetRenderTargetItem().ShaderResourceTexture : GSystemTextures.WhiteDummy->GetRenderTargetItem().TargetableTexture;

			SetTextureParameter(
				RHICmdList,
				ShaderRHI,
				LightingChannelsTexture,
				LightingChannelsSampler,
				TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
				LightingChannelsTextureRHI
				);
		}

		if( TransmissionProfilesTexture.IsBound() )
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

		if (HairTransmittanceBuffer.IsBound())
		{
			const uint32 TransmittanceBufferMaxCount = RenderLightParams ? RenderLightParams->DeepShadow_TransmittanceMaskBufferMaxCount : 0;
			SetShaderValue(
				RHICmdList,
				ShaderRHI,
				HairTransmittanceBufferMaxCount,
				TransmittanceBufferMaxCount);
			if (RenderLightParams && RenderLightParams->DeepShadow_TransmittanceMaskBuffer)
			{
				SetSRVParameter(RHICmdList, ShaderRHI, HairTransmittanceBuffer, RenderLightParams->DeepShadow_TransmittanceMaskBuffer);
			}
		}

		if (ScreenShadowMaskSubPixelTexture.IsBound())
		{
			if (RenderLightParams)
			{
				SetTextureParameter(
					RHICmdList,
					ShaderRHI,
					ScreenShadowMaskSubPixelTexture,
					LightAttenuationTextureSampler,
					TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),
					(RenderLightParams && RenderLightParams->ScreenShadowMaskSubPixelTexture) ? RenderLightParams->ScreenShadowMaskSubPixelTexture->GetRenderTargetItem().ShaderResourceTexture : GWhiteTexture->TextureRHI);

				uint32 InHairShadowMaskValid = RenderLightParams->ScreenShadowMaskSubPixelTexture ? 1 : 0;
				SetShaderValue(
					RHICmdList,
					ShaderRHI,
					HairShadowMaskValid,
					InHairShadowMaskValid);
			}
		}

		if (HairCategorizationTexture.IsBound())
		{
			if (RenderLightParams && RenderLightParams->HairCategorizationTexture)
			{
				SetTextureParameter(
					RHICmdList,
					ShaderRHI,
					HairCategorizationTexture,
					LightAttenuationTextureSampler,
					TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),
					RenderLightParams->HairCategorizationTexture->GetRenderTargetItem().TargetableTexture);
			}
		}

		if (HairVisibilityNodeOffsetAndCount.IsBound())
		{
			if (RenderLightParams && RenderLightParams->HairVisibilityNodeOffsetAndCount)
			{
				SetTextureParameter(
					RHICmdList,
					ShaderRHI,
					HairVisibilityNodeOffsetAndCount,
					LightAttenuationTextureSampler,
					TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),
					RenderLightParams->HairVisibilityNodeOffsetAndCount->GetRenderTargetItem().TargetableTexture);
			}
		}
		
		if (HairVisibilityNodeCoords.IsBound())
		{
			if (RenderLightParams && RenderLightParams->HairVisibilityNodeCoordsSRV)
			{
				FShaderResourceViewRHIRef SRV = RenderLightParams->HairVisibilityNodeCoordsSRV;
				SetSRVParameter(
					RHICmdList, 
					ShaderRHI, 
					HairVisibilityNodeCoords,
					SRV);
			}
		}

		if (HairVisibilityNodeData.IsBound())
		{
			if (RenderLightParams && RenderLightParams->HairVisibilityNodeDataSRV)
			{
				FShaderResourceViewRHIRef SRV = RenderLightParams->HairVisibilityNodeDataSRV;
				SetSRVParameter(
					RHICmdList, 
					ShaderRHI, 
					HairVisibilityNodeData, 
					SRV);
			}
		}

		if (HairLUTTexture.IsBound())
		{
			IPooledRenderTarget* HairLUTTextureResource = GSystemTextures.HairLUT0;
			SetTextureParameter(
				RHICmdList,
				ShaderRHI,
				HairLUTTexture,
				HairLUTSampler,
				TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),
				HairLUTTextureResource ? HairLUTTextureResource->GetRenderTargetItem().ShaderResourceTexture : GBlackVolumeTexture->TextureRHI);
		}

		if (HairComponents.IsBound())
		{
			uint32 InHairComponents = ToBitfield(GetHairComponents());
			SetShaderValue(
				RHICmdList,
				ShaderRHI,
				HairComponents,
				InHairComponents);
		}

		if (HairDualScatteringRoughnessOverride.IsBound())
		{
			const float DualScatteringRoughness = GetHairDualScatteringRoughnessOverride();
			SetShaderValue(
				RHICmdList,
				ShaderRHI,
				HairDualScatteringRoughnessOverride,
				DualScatteringRoughness);
		}
		
	}

	LAYOUT_FIELD(FSceneTextureShaderParameters, SceneTextureParameters);
	LAYOUT_FIELD(FShaderResourceParameter, LightAttenuationTexture);
	LAYOUT_FIELD(FShaderResourceParameter, LightAttenuationTextureSampler);
	LAYOUT_FIELD(FShaderResourceParameter, LTCMatTexture);
	LAYOUT_FIELD(FShaderResourceParameter, LTCMatSampler);
	LAYOUT_FIELD(FShaderResourceParameter, LTCAmpTexture);
	LAYOUT_FIELD(FShaderResourceParameter, LTCAmpSampler);
	LAYOUT_FIELD(FShaderResourceParameter, IESTexture);
	LAYOUT_FIELD(FShaderResourceParameter, IESTextureSampler);
	LAYOUT_FIELD(FShaderResourceParameter, LightingChannelsTexture);
	LAYOUT_FIELD(FShaderResourceParameter, LightingChannelsSampler);
	LAYOUT_FIELD(FShaderResourceParameter, TransmissionProfilesTexture);
	LAYOUT_FIELD(FShaderResourceParameter, TransmissionProfilesLinearSampler);

	LAYOUT_FIELD(FShaderParameter, HairTransmittanceBufferMaxCount);
	LAYOUT_FIELD(FShaderResourceParameter, HairTransmittanceBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, HairCategorizationTexture);
	LAYOUT_FIELD(FShaderResourceParameter, HairVisibilityNodeOffsetAndCount);
	LAYOUT_FIELD(FShaderResourceParameter, HairVisibilityNodeCoords);
	LAYOUT_FIELD(FShaderResourceParameter, HairVisibilityNodeData);
	LAYOUT_FIELD(FShaderResourceParameter, ScreenShadowMaskSubPixelTexture);

	LAYOUT_FIELD(FShaderResourceParameter, HairLUTTexture);
	LAYOUT_FIELD(FShaderResourceParameter, HairLUTSampler);
	LAYOUT_FIELD(FShaderParameter, HairComponents);
	LAYOUT_FIELD(FShaderParameter, HairShadowMaskValid);
	LAYOUT_FIELD(FShaderParameter, HairDualScatteringRoughnessOverride);
};

IMPLEMENT_GLOBAL_SHADER(FDeferredLightPS, "/Engine/Private/DeferredLightPixelShaders.usf", "DeferredLightPixelMain", SF_Pixel);


/** Shader used to visualize stationary light overlap. */
template<bool bRadialAttenuation>
class TDeferredLightOverlapPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TDeferredLightOverlapPS,Global)
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("RADIAL_ATTENUATION"), (uint32)bRadialAttenuation);
	}

	TDeferredLightOverlapPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	:	FGlobalShader(Initializer)
	{
		HasValidChannel.Bind(Initializer.ParameterMap, TEXT("HasValidChannel"));
		SceneTextureParameters.Bind(Initializer);
	}

	TDeferredLightOverlapPS()
	{
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, const FLightSceneInfo* LightSceneInfo)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI,View.ViewUniformBuffer);
		const float HasValidChannelValue = LightSceneInfo->Proxy->GetPreviewShadowMapChannel() == INDEX_NONE ? 0.0f : 1.0f;
		SetShaderValue(RHICmdList, ShaderRHI, HasValidChannel, HasValidChannelValue);
		SceneTextureParameters.Set(RHICmdList, ShaderRHI, View.FeatureLevel, ESceneTextureSetupMode::All);
		SetDeferredLightParameters(RHICmdList, ShaderRHI, GetUniformBufferParameter<FDeferredLightUniformStruct>(), LightSceneInfo, View);
	}

private:
	LAYOUT_FIELD(FShaderParameter, HasValidChannel);
	LAYOUT_FIELD(FSceneTextureShaderParameters, SceneTextureParameters);
};

IMPLEMENT_SHADER_TYPE(template<>, TDeferredLightOverlapPS<true>, TEXT("/Engine/Private/StationaryLightOverlapShaders.usf"), TEXT("OverlapRadialPixelMain"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, TDeferredLightOverlapPS<false>, TEXT("/Engine/Private/StationaryLightOverlapShaders.usf"), TEXT("OverlapDirectionalPixelMain"), SF_Pixel);

void FSceneRenderer::SplitSimpleLightsByView(const FSceneViewFamily& ViewFamily, const TArray<FViewInfo>& Views, const FSimpleLightArray& SimpleLights, FSimpleLightArray* SimpleLightsByView)
{
	for (int32 LightIndex = 0; LightIndex < SimpleLights.InstanceData.Num(); ++LightIndex)
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			FSimpleLightPerViewEntry PerViewEntry = SimpleLights.GetViewDependentData(LightIndex, ViewIndex, Views.Num());
			SimpleLightsByView[ViewIndex].InstanceData.Add(SimpleLights.InstanceData[LightIndex]);
			SimpleLightsByView[ViewIndex].PerViewData.Add(PerViewEntry);
		}
	}
}

/** Gathers simple lights from visible primtives in the passed in views. */
void FSceneRenderer::GatherSimpleLights(const FSceneViewFamily& ViewFamily, const TArray<FViewInfo>& Views, FSimpleLightArray& SimpleLights)
{
	TArray<const FPrimitiveSceneInfo*, SceneRenderingAllocator> PrimitivesWithSimpleLights;

	// Gather visible primitives from all views that might have simple lights
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		for (int32 PrimitiveIndex = 0; PrimitiveIndex < View.VisibleDynamicPrimitivesWithSimpleLights.Num(); PrimitiveIndex++)
		{
			const FPrimitiveSceneInfo* PrimitiveSceneInfo = View.VisibleDynamicPrimitivesWithSimpleLights[PrimitiveIndex];

			// TArray::AddUnique is slow, but not expecting many entries in PrimitivesWithSimpleLights
			PrimitivesWithSimpleLights.AddUnique(PrimitiveSceneInfo);
		}
	}

	// Gather simple lights from the primitives
	for (int32 PrimitiveIndex = 0; PrimitiveIndex < PrimitivesWithSimpleLights.Num(); PrimitiveIndex++)
	{
		const FPrimitiveSceneInfo* Primitive = PrimitivesWithSimpleLights[PrimitiveIndex];
		Primitive->Proxy->GatherSimpleLights(ViewFamily, SimpleLights);
	}
}

/** Gets a readable light name for use with a draw event. */
void FSceneRenderer::GetLightNameForDrawEvent(const FLightSceneProxy* LightProxy, FString& LightNameWithLevel)
{
#if WANTS_DRAW_MESH_EVENTS
	if (GetEmitDrawEvents())
	{
		FString FullLevelName = LightProxy->GetLevelName().ToString();
		const int32 LastSlashIndex = FullLevelName.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);

		if (LastSlashIndex != INDEX_NONE)
		{
			// Trim the leading path before the level name to make it more readable
			// The level FName was taken directly from the Outermost UObject, otherwise we would do this operation on the game thread
			FullLevelName.MidInline(LastSlashIndex + 1, FullLevelName.Len() - (LastSlashIndex + 1), false);
		}

		LightNameWithLevel = FullLevelName + TEXT(".") + LightProxy->GetComponentName().ToString();
	}
#endif
}

extern int32 GbEnableAsyncComputeTranslucencyLightingVolumeClear;

uint32 GetShadowQuality();

static bool LightRequiresDenosier(const FLightSceneInfo& LightSceneInfo)
{
	ELightComponentType LightType = ELightComponentType(LightSceneInfo.Proxy->GetLightType());
	if (LightType == LightType_Directional)
	{
		return LightSceneInfo.Proxy->GetLightSourceAngle() > 0;
	}
	else if (LightType == LightType_Point || LightType == LightType_Spot)
	{
		return LightSceneInfo.Proxy->GetSourceRadius() > 0;
	}
	else if (LightType == LightType_Rect)
	{
		return true;
	}
	else
	{
		check(0);
	}
	return false;
}



void FDeferredShadingSceneRenderer::GatherAndSortLights(FSortedLightSetSceneInfo& OutSortedLights)
{
	if (bAllowSimpleLights)
	{
		GatherSimpleLights(ViewFamily, Views, OutSortedLights.SimpleLights);
	}
	FSimpleLightArray &SimpleLights = OutSortedLights.SimpleLights;
	TArray<FSortedLightSceneInfo, SceneRenderingAllocator> &SortedLights = OutSortedLights.SortedLights;

	// NOTE: we allocate space also for simple lights such that they can be referenced in the same sorted range
	SortedLights.Empty(Scene->Lights.Num() + SimpleLights.InstanceData.Num());

	bool bDynamicShadows = ViewFamily.EngineShowFlags.DynamicShadows && GetShadowQuality() > 0;

#if ENABLE_DEBUG_DISCARD_PROP
	int Total = Scene->Lights.Num() + SimpleLights.InstanceData.Num();
	int NumToKeep = int(float(Total) * (1.0f - GDebugLightDiscardProp));
	const float DebugDiscardStride = float(NumToKeep) / float(Total);
	float DebugDiscardCounter = 0.0f;
#endif // ENABLE_DEBUG_DISCARD_PROP
	// Build a list of visible lights.
	for (TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights); LightIt; ++LightIt)
	{
		const FLightSceneInfoCompact& LightSceneInfoCompact = *LightIt;
		const FLightSceneInfo* const LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;

#if ENABLE_DEBUG_DISCARD_PROP
		{
			int PrevCounter = int(DebugDiscardCounter);
			DebugDiscardCounter += DebugDiscardStride;
			if (PrevCounter >= int(DebugDiscardCounter))
			{
				continue;
			}
		}
#endif // ENABLE_DEBUG_DISCARD_PROP

		if (LightSceneInfo->ShouldRenderLightViewIndependent()
			// Reflection override skips direct specular because it tends to be blindingly bright with a perfectly smooth surface
			&& !ViewFamily.EngineShowFlags.ReflectionOverride)
		{
			// Check if the light is visible in any of the views.
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				if (LightSceneInfo->ShouldRenderLight(Views[ViewIndex]))
				{
					FSortedLightSceneInfo* SortedLightInfo = new(SortedLights) FSortedLightSceneInfo(LightSceneInfo);

					// Check for shadows and light functions.
					SortedLightInfo->SortKey.Fields.LightType = LightSceneInfoCompact.LightType;
					SortedLightInfo->SortKey.Fields.bTextureProfile = ViewFamily.EngineShowFlags.TexturedLightProfiles && LightSceneInfo->Proxy->GetIESTextureResource();
					SortedLightInfo->SortKey.Fields.bShadowed = bDynamicShadows && CheckForProjectedShadows(LightSceneInfo);
					SortedLightInfo->SortKey.Fields.bLightFunction = ViewFamily.EngineShowFlags.LightFunctions && CheckForLightFunction(LightSceneInfo);
					SortedLightInfo->SortKey.Fields.bUsesLightingChannels = Views[ViewIndex].bUsesLightingChannels && LightSceneInfo->Proxy->GetLightingChannelMask() != GetDefaultLightingChannelMask();

					// These are not simple lights.
					SortedLightInfo->SortKey.Fields.bIsNotSimpleLight = 1;


					// tiled and clustered deferred lighting only supported for certain lights that don't use any additional features
					// And also that are not directional (mostly because it does'nt make so much sense to insert them into every grid cell in the universe)
					// In the forward case one directional light gets put into its own variables, and in the deferred case it gets a full-screen pass.
					// Usually it'll have shadows and stuff anyway.
					// Rect lights are not supported as the performance impact is significant even if not used, for now, left for trad. deferred.
					const bool bTiledOrClusteredDeferredSupported =
						!SortedLightInfo->SortKey.Fields.bTextureProfile &&
						!SortedLightInfo->SortKey.Fields.bShadowed &&
						!SortedLightInfo->SortKey.Fields.bLightFunction &&
						!SortedLightInfo->SortKey.Fields.bUsesLightingChannels
						&& LightSceneInfoCompact.LightType != LightType_Directional
						&& LightSceneInfoCompact.LightType != LightType_Rect;

					SortedLightInfo->SortKey.Fields.bTiledDeferredNotSupported = !(bTiledOrClusteredDeferredSupported && LightSceneInfo->Proxy->IsTiledDeferredLightingSupported());

					SortedLightInfo->SortKey.Fields.bClusteredDeferredNotSupported = !bTiledOrClusteredDeferredSupported;
					break;
				}
			}
		}
	}
	// Add the simple lights also
	for (int32 SimpleLightIndex = 0; SimpleLightIndex < SimpleLights.InstanceData.Num(); SimpleLightIndex++)
	{
#if ENABLE_DEBUG_DISCARD_PROP
		{
			int PrevCounter = int(DebugDiscardCounter);
			DebugDiscardCounter += DebugDiscardStride;
			if (PrevCounter >= int(DebugDiscardCounter))
			{
				continue;
			}
		}
#endif // ENABLE_DEBUG_DISCARD_PROP

		FSortedLightSceneInfo* SortedLightInfo = new(SortedLights) FSortedLightSceneInfo(SimpleLightIndex);
		SortedLightInfo->SortKey.Fields.LightType = LightType_Point;
		SortedLightInfo->SortKey.Fields.bTextureProfile = 0;
		SortedLightInfo->SortKey.Fields.bShadowed = 0;
		SortedLightInfo->SortKey.Fields.bLightFunction = 0;
		SortedLightInfo->SortKey.Fields.bUsesLightingChannels = 0;

		// These are simple lights.
		SortedLightInfo->SortKey.Fields.bIsNotSimpleLight = 0;

		// Simple lights are ok to use with tiled and clustered deferred lighting
		SortedLightInfo->SortKey.Fields.bTiledDeferredNotSupported = 0;
		SortedLightInfo->SortKey.Fields.bClusteredDeferredNotSupported = 0;
	}

	// Sort non-shadowed, non-light function lights first to avoid render target switches.
	struct FCompareFSortedLightSceneInfo
	{
		FORCEINLINE bool operator()( const FSortedLightSceneInfo& A, const FSortedLightSceneInfo& B ) const
		{
			return A.SortKey.Packed < B.SortKey.Packed;
		}
	};
	SortedLights.Sort( FCompareFSortedLightSceneInfo() );

	// Scan and find ranges.
	OutSortedLights.SimpleLightsEnd = SortedLights.Num();
	OutSortedLights.TiledSupportedEnd = SortedLights.Num();
	OutSortedLights.ClusteredSupportedEnd = SortedLights.Num();
	OutSortedLights.AttenuationLightStart = SortedLights.Num();

	// Iterate over all lights to be rendered and build ranges for tiled deferred and unshadowed lights
	for (int32 LightIndex = 0; LightIndex < SortedLights.Num(); LightIndex++)
	{
		const FSortedLightSceneInfo& SortedLightInfo = SortedLights[LightIndex];
		const bool bDrawShadows = SortedLightInfo.SortKey.Fields.bShadowed;
		const bool bDrawLightFunction = SortedLightInfo.SortKey.Fields.bLightFunction;
		const bool bTextureLightProfile = SortedLightInfo.SortKey.Fields.bTextureProfile;
		const bool bLightingChannels = SortedLightInfo.SortKey.Fields.bUsesLightingChannels;

		if (SortedLightInfo.SortKey.Fields.bIsNotSimpleLight && OutSortedLights.SimpleLightsEnd == SortedLights.Num())
		{
			// Mark the first index to not be simple
			OutSortedLights.SimpleLightsEnd = LightIndex;
		}

		if (SortedLightInfo.SortKey.Fields.bTiledDeferredNotSupported && OutSortedLights.TiledSupportedEnd == SortedLights.Num())
		{
			// Mark the first index to not support tiled deferred
			OutSortedLights.TiledSupportedEnd = LightIndex;
		}

		if (SortedLightInfo.SortKey.Fields.bClusteredDeferredNotSupported && OutSortedLights.ClusteredSupportedEnd == SortedLights.Num())
		{
			// Mark the first index to not support clustered deferred
			OutSortedLights.ClusteredSupportedEnd = LightIndex;
		}

		if (bDrawShadows || bDrawLightFunction || bLightingChannels)
		{
			// Once we find a shadowed light, we can exit the loop, these lights should never support tiled deferred rendering either
			check(SortedLightInfo.SortKey.Fields.bTiledDeferredNotSupported);
			OutSortedLights.AttenuationLightStart = LightIndex;
			break;
		}
	}

	// Make sure no obvious things went wrong!
	check(OutSortedLights.TiledSupportedEnd >= OutSortedLights.SimpleLightsEnd);
	check(OutSortedLights.ClusteredSupportedEnd >= OutSortedLights.TiledSupportedEnd);
	check(OutSortedLights.AttenuationLightStart >= OutSortedLights.ClusteredSupportedEnd);
}

static bool HasHairStrandsClusters(int32 ViewIndex, const FHairStrandsDatas* HairDatas)
{
	return HairDatas && ViewIndex < HairDatas->MacroGroupsPerViews.Views.Num() && HairDatas->MacroGroupsPerViews.Views[ViewIndex].Datas.Num() > 0;
};

static FHairStrandsOcclusionResources GetHairStrandsResources(int32 ViewIndex, FRDGBuilder& GraphBuilder, const FHairStrandsDatas* HairDatas)
{
	FHairStrandsOcclusionResources Out;
	if (HairDatas && ViewIndex < HairDatas->HairVisibilityViews.HairDatas.Num())
	{
		if (HairDatas->HairVisibilityViews.HairDatas[ViewIndex].CategorizationTexture)
		{
			Out.CategorizationTexture = GraphBuilder.RegisterExternalTexture(HairDatas->HairVisibilityViews.HairDatas[ViewIndex].CategorizationTexture);
		}
		if (HairDatas->HairVisibilityViews.HairDatas[ViewIndex].LightChannelMaskTexture)
		{
			Out.LightChannelMaskTexture = GraphBuilder.RegisterExternalTexture(HairDatas->HairVisibilityViews.HairDatas[ViewIndex].LightChannelMaskTexture);
		}

		Out.VoxelResources = &HairDatas->MacroGroupsPerViews.Views[ViewIndex].VirtualVoxelResources;
	}
	return Out;
}

/** Renders the scene's lighting. */
void FDeferredShadingSceneRenderer::RenderLights(FRHICommandListImmediate& RHICmdList, FSortedLightSetSceneInfo &SortedLightSet, const FHairStrandsDatas* HairDatas)
{
	const bool bUseHairLighting = HairDatas != nullptr;
	const FHairStrandsVisibilityViews* InHairVisibilityViews = HairDatas ? &HairDatas->HairVisibilityViews : nullptr;

	check(RHICmdList.IsOutsideRenderPass());

	SCOPED_NAMED_EVENT(FDeferredShadingSceneRenderer_RenderLights, FColor::Emerald);
	SCOPED_DRAW_EVENT(RHICmdList, Lights);
	SCOPED_GPU_STAT(RHICmdList, Lights);


	bool bStencilBufferDirty = false;	// The stencil buffer should've been cleared to 0 already

	SCOPE_CYCLE_COUNTER(STAT_LightingDrawTime);
	SCOPE_CYCLE_COUNTER(STAT_LightRendering);

	const FSimpleLightArray &SimpleLights = SortedLightSet.SimpleLights;
	const TArray<FSortedLightSceneInfo, SceneRenderingAllocator> &SortedLights = SortedLightSet.SortedLights;
	const int32 AttenuationLightStart = SortedLightSet.AttenuationLightStart;
	const int32 SimpleLightsEnd = SortedLightSet.SimpleLightsEnd;

	{
		SCOPED_DRAW_EVENT(RHICmdList, DirectLighting);

		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

		if (GbEnableAsyncComputeTranslucencyLightingVolumeClear && GSupportsEfficientAsyncCompute)
		{
			//Gfx pipe must wait for the async compute clear of the translucency volume clear.
			RHICmdList.WaitComputeFence(TranslucencyLightingVolumeClearEndFence);
		}

		if(ViewFamily.EngineShowFlags.DirectLighting)
		{
			SCOPED_DRAW_EVENT(RHICmdList, NonShadowedLights);
			INC_DWORD_STAT_BY(STAT_NumUnshadowedLights, AttenuationLightStart);

			// Currently they have a special path anyway in case of standard deferred so always skip the simple lights
			int32 StandardDeferredStart = SortedLightSet.SimpleLightsEnd;

			bool bRenderSimpleLightsStandardDeferred = SortedLightSet.SimpleLights.InstanceData.Num() > 0;

			UE_CLOG(ShouldUseClusteredDeferredShading() && !AreClusteredLightsInLightGrid(), LogRenderer, Warning,
				TEXT("Clustered deferred shading is enabled, but lights were not injected in grid, falling back to other methods (hint 'r.LightCulling.Quality' may cause this)."));

			// True if the clustered shading is enabled and the feature level is there, and that the light grid had lights injected.
			if (ShouldUseClusteredDeferredShading() && AreClusteredLightsInLightGrid())
			{
				// Tell the trad. deferred that the clustered deferred capable lights are taken care of.
				// This includes the simple lights
				StandardDeferredStart = SortedLightSet.ClusteredSupportedEnd;
				// Tell the trad. deferred that the simple lights are spoken for.
				bRenderSimpleLightsStandardDeferred = false;
				AddClusteredDeferredShadingPass(RHICmdList, SortedLightSet);
			}
			else if (CanUseTiledDeferred())
			{
				bool bAnyViewIsStereo = false;
				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
				{
					if (IStereoRendering::IsStereoEyeView(Views[ViewIndex]))
					{
						bAnyViewIsStereo = true;
						break;
					}
				}

				// Use tiled deferred shading on any unshadowed lights without a texture light profile
				if (ShouldUseTiledDeferred(SortedLightSet.TiledSupportedEnd) && !bAnyViewIsStereo)
				{
					// Update the range that needs to be processed by standard deferred to exclude the lights done with tiled
					StandardDeferredStart = SortedLightSet.TiledSupportedEnd;
					bRenderSimpleLightsStandardDeferred = false;
					RenderTiledDeferredLighting(RHICmdList, SortedLights, SortedLightSet.SimpleLightsEnd, SortedLightSet.TiledSupportedEnd, SimpleLights);
				}
			}

			if (bRenderSimpleLightsStandardDeferred)
			{
				SceneContext.BeginRenderingSceneColor(RHICmdList, ESimpleRenderTargetMode::EExistingColorAndDepth, FExclusiveDepthStencil::DepthRead_StencilWrite);
				RenderSimpleLightsStandardDeferred(RHICmdList, SortedLightSet.SimpleLights);
				SceneContext.FinishRenderingSceneColor(RHICmdList);
			}

			if (!bUseHairLighting)
			{
				SCOPED_DRAW_EVENT(RHICmdList, StandardDeferredLighting);

				// make sure we don't clear the depth
				SceneContext.BeginRenderingSceneColor(RHICmdList, ESimpleRenderTargetMode::EExistingColorAndDepth, FExclusiveDepthStencil::DepthRead_StencilWrite, true);

				// Draw non-shadowed non-light function lights without changing render targets between them
				for (int32 LightIndex = StandardDeferredStart; LightIndex < AttenuationLightStart; LightIndex++)
				{
					const FSortedLightSceneInfo& SortedLightInfo = SortedLights[LightIndex];
					const FLightSceneInfo* const LightSceneInfo = SortedLightInfo.LightSceneInfo;

					// Render the light to the scene color buffer, using a 1x1 white texture as input
					RenderLight(RHICmdList, LightSceneInfo, nullptr, nullptr, false, false);
				}

				SceneContext.FinishRenderingSceneColor(RHICmdList);
			}
			else 
			// Add a special version when hair rendering is enabled for getting lighting on hair. 
			// This is a temporary solution as normally we should render a pre-shadow when a hair cluster is visible on screen
			{
				SCOPED_DRAW_EVENT(RHICmdList, StandardDeferredLighting);

				// make sure we don't clear the depth

				// Draw non-shadowed non-light function lights without changing render targets between them
				for (int32 LightIndex = StandardDeferredStart; LightIndex < AttenuationLightStart; LightIndex++)
				{
					const FSortedLightSceneInfo& SortedLightInfo = SortedLights[LightIndex];
					const FLightSceneInfo* const LightSceneInfo = SortedLightInfo.LightSceneInfo;

					FHairStrandsTransmittanceMaskData TransmittanceMaskData;
					TRefCountPtr<IPooledRenderTarget> NullScreenShadowMaskSubPixelTexture = nullptr;
					TransmittanceMaskData = RenderHairStrandsTransmittanceMask(RHICmdList, Views, LightSceneInfo, HairDatas, NullScreenShadowMaskSubPixelTexture);

					// Render the light to the scene color buffer, using a 1x1 white texture as input
					SceneContext.BeginRenderingSceneColor(RHICmdList, ESimpleRenderTargetMode::EExistingColorAndDepth, FExclusiveDepthStencil::DepthRead_StencilWrite, true);
					RenderLight(RHICmdList, LightSceneInfo, nullptr, InHairVisibilityViews, false, false);
					SceneContext.FinishRenderingSceneColor(RHICmdList);
				}

			}

			if (GUseTranslucentLightingVolumes && GSupportsVolumeTextureRendering)
			{
				if (AttenuationLightStart)
				{
					// Inject non-shadowed, non-simple, non-light function lights in to the volume.
					SCOPED_DRAW_EVENT(RHICmdList, InjectNonShadowedTranslucentLighting);
					InjectTranslucentVolumeLightingArray(RHICmdList, SortedLights, SimpleLightsEnd, AttenuationLightStart);
				}

				if(SimpleLights.InstanceData.Num() > 0)
				{
					FSimpleLightArray* SimpleLightsByView = new FSimpleLightArray[Views.Num()];

					SplitSimpleLightsByView(ViewFamily, Views, SimpleLights, SimpleLightsByView);

					for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
					{
						if (SimpleLightsByView[ViewIndex].InstanceData.Num() > 0)
						{
							SCOPED_DRAW_EVENT(RHICmdList, InjectSimpleLightsTranslucentLighting);
							InjectSimpleTranslucentVolumeLightingArray(RHICmdList, SimpleLightsByView[ViewIndex], Views[ViewIndex], ViewIndex);
						}
					}

					delete[] SimpleLightsByView;
				}
			}
		}

		EShaderPlatform ShaderPlatformForFeatureLevel = GShaderPlatformForFeatureLevel[FeatureLevel];

		if ( IsFeatureLevelSupported(ShaderPlatformForFeatureLevel, ERHIFeatureLevel::SM5) )
		{
			SCOPED_DRAW_EVENT(RHICmdList, IndirectLighting);
			bool bRenderedRSM = false;
			// Render Reflective shadow maps
			// Draw shadowed and light function lights
			for (int32 LightIndex = AttenuationLightStart; LightIndex < SortedLights.Num(); LightIndex++)
			{
				const FSortedLightSceneInfo& SortedLightInfo = SortedLights[LightIndex];
				const FLightSceneInfo& LightSceneInfo = *SortedLightInfo.LightSceneInfo;
				// Render any reflective shadow maps (if necessary)
				if ( LightSceneInfo.Proxy && LightSceneInfo.Proxy->NeedsLPVInjection() )
				{
					if ( LightSceneInfo.Proxy->HasReflectiveShadowMap() )
					{
						INC_DWORD_STAT(STAT_NumReflectiveShadowMapLights);
						InjectReflectiveShadowMaps(RHICmdList, &LightSceneInfo);
						bRenderedRSM = true;
					}
				}
			}

			// LPV Direct Light Injection
			if ( bRenderedRSM )
			{
				for (int32 LightIndex = SimpleLightsEnd; LightIndex < SortedLights.Num(); LightIndex++)
				{
					const FSortedLightSceneInfo& SortedLightInfo = SortedLights[LightIndex];
					const FLightSceneInfo* const LightSceneInfo = SortedLightInfo.LightSceneInfo;

					// Render any reflective shadow maps (if necessary)
					if ( LightSceneInfo && LightSceneInfo->Proxy && LightSceneInfo->Proxy->NeedsLPVInjection() )
					{
						if ( !LightSceneInfo->Proxy->HasReflectiveShadowMap() )
						{
							// Inject the light directly into all relevant LPVs
							for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
							{
								FViewInfo& View = Views[ViewIndex];

								if (LightSceneInfo->ShouldRenderLight(View))
								{
									FSceneViewState* ViewState = (FSceneViewState*)View.State;
									if (ViewState)
									{
										FLightPropagationVolume* Lpv = ViewState->GetLightPropagationVolume(View.GetFeatureLevel());
										if (Lpv && LightSceneInfo->Proxy)
										{
											Lpv->InjectLightDirect(RHICmdList, *LightSceneInfo->Proxy, View);
										}
									}
								}
							}
						}
					}
				}
			}

			// Kickoff the LPV update (asynchronously if possible)
			UpdateLPVs(RHICmdList);
		}

		{
			SCOPED_DRAW_EVENT(RHICmdList, ShadowedLights);

			const int32 DenoiserMode = CVarShadowUseDenoiser.GetValueOnRenderThread();

			const IScreenSpaceDenoiser* DefaultDenoiser = IScreenSpaceDenoiser::GetDefaultDenoiser();
			const IScreenSpaceDenoiser* DenoiserToUse = DenoiserMode == 1 ? DefaultDenoiser : GScreenSpaceDenoiser;

			TArray<TRefCountPtr<IPooledRenderTarget>> PreprocessedShadowMaskTextures;
			TArray<TRefCountPtr<IPooledRenderTarget>> PreprocessedShadowMaskSubPixelTextures;

			const int32 MaxDenoisingBatchSize = FMath::Clamp(CVarMaxShadowDenoisingBatchSize.GetValueOnRenderThread(), 1, IScreenSpaceDenoiser::kMaxBatchSize);
			const int32 MaxRTShadowBatchSize = CVarMaxShadowRayTracingBatchSize.GetValueOnRenderThread();
			const bool bDoShadowDenoisingBatching = DenoiserMode != 0 && MaxDenoisingBatchSize > 1;

			//#dxr_todo: support multiview for the batching case
			const bool bDoShadowBatching = (bDoShadowDenoisingBatching || MaxRTShadowBatchSize > 1) && Views.Num() == 1;

			// Optimisations: batches all shadow ray tracing denoising. Definitely could be smarter to avoid high VGPR pressure if this entire
			// function was converted to render graph, and want least intrusive change as possible. So right not it trades render target memory pressure
			// for denoising perf.
			if (RHI_RAYTRACING && bDoShadowBatching)
			{
				const uint32 ViewIndex = 0;
				FViewInfo& View = Views[ViewIndex];

				// Allocate PreprocessedShadowMaskTextures once so QueueTextureExtraction can deferred write.
				{
					if (!View.bStatePrevViewInfoIsReadOnly)
					{
						View.ViewState->PrevFrameViewInfo.ShadowHistories.Empty();
						View.ViewState->PrevFrameViewInfo.ShadowHistories.Reserve(SortedLights.Num());
					}

					PreprocessedShadowMaskTextures.SetNum(SortedLights.Num());
				}

				PreprocessedShadowMaskTextures.SetNum(SortedLights.Num());

				if (HasHairStrandsClusters(ViewIndex, HairDatas))
				{ 
					PreprocessedShadowMaskSubPixelTextures.SetNum(SortedLights.Num());
				}
			} // if (RHI_RAYTRACING)

			bool bDirectLighting = ViewFamily.EngineShowFlags.DirectLighting;
			bool bShadowMaskReadable = false;
			TRefCountPtr<IPooledRenderTarget> ScreenShadowMaskTexture;
			TRefCountPtr<IPooledRenderTarget> ScreenShadowMaskSubPixelTexture;

			// Draw shadowed and light function lights
			for (int32 LightIndex = AttenuationLightStart; LightIndex < SortedLights.Num(); LightIndex++)
			{
				const FSortedLightSceneInfo& SortedLightInfo = SortedLights[LightIndex];
				const FLightSceneInfo& LightSceneInfo = *SortedLightInfo.LightSceneInfo;

				// Note: Skip shadow mask generation for rect light if direct illumination is computed
				//		 stochastically (rather than analytically + shadow mask)
				const bool bDrawShadows = SortedLightInfo.SortKey.Fields.bShadowed && !ShouldRenderRayTracingStochasticRectLight(LightSceneInfo);
				bool bDrawLightFunction = SortedLightInfo.SortKey.Fields.bLightFunction;
				bool bDrawPreviewIndicator = ViewFamily.EngineShowFlags.PreviewShadowsIndicator && !LightSceneInfo.IsPrecomputedLightingValid() && LightSceneInfo.Proxy->HasStaticShadowing();
				bool bInjectedTranslucentVolume = false;
				bool bUsedShadowMaskTexture = false;
				const bool bDrawHairShadow = bDrawShadows && bUseHairLighting;
				const bool bUseHairDeepShadow = bDrawShadows && bUseHairLighting && LightSceneInfo.Proxy->CastsHairStrandsDeepShadow();

				FScopeCycleCounter Context(LightSceneInfo.Proxy->GetStatId());

				if ((bDrawShadows || bDrawLightFunction || bDrawPreviewIndicator) && !ScreenShadowMaskTexture.IsValid())
				{
					SceneContext.AllocateScreenShadowMask(RHICmdList, ScreenShadowMaskTexture);
					bShadowMaskReadable = false;
					if (bUseHairLighting)
					{
						SceneContext.AllocateScreenShadowMask(RHICmdList, ScreenShadowMaskSubPixelTexture, true);
					}
				}

				FString LightNameWithLevel;
				GetLightNameForDrawEvent(LightSceneInfo.Proxy, LightNameWithLevel);
				SCOPED_DRAW_EVENTF(RHICmdList, EventLightPass, *LightNameWithLevel);

				if (bDrawShadows)
				{
					INC_DWORD_STAT(STAT_NumShadowedLights);

					const FLightOcclusionType OcclusionType = GetLightOcclusionType(*LightSceneInfo.Proxy);

					// Inline ray traced shadow batching, launches shadow batches when needed
					// reduces memory overhead while keeping shadows batched to optimize costs
					{
						const uint32 ViewIndex = 0;
						FViewInfo& View = Views[ViewIndex];

						IScreenSpaceDenoiser::FShadowRayTracingConfig RayTracingConfig;
						RayTracingConfig.RayCountPerPixel = LightSceneInfo.Proxy->GetSamplesPerPixel();

						const bool bDenoiserCompatible = !LightRequiresDenosier(LightSceneInfo) || IScreenSpaceDenoiser::EShadowRequirements::PenumbraAndClosestOccluder == DenoiserToUse->GetShadowRequirements(View, LightSceneInfo, RayTracingConfig);

						const bool bWantsBatchedShadow = OcclusionType == FLightOcclusionType::Raytraced && 
							bDoShadowBatching &&
							bDenoiserCompatible &&
							SortedLightInfo.SortKey.Fields.bShadowed && 
							!ShouldRenderRayTracingStochasticRectLight(LightSceneInfo);

						// determine if this light doesn't yet have a precomuted shadow and execute a batch to amortize costs if one is needed
						if (
							RHI_RAYTRACING &&
							bWantsBatchedShadow &&
							(PreprocessedShadowMaskTextures.Num() == 0 || !PreprocessedShadowMaskTextures[LightIndex - AttenuationLightStart]))
						{
							SCOPED_DRAW_EVENT(RHICmdList, ShadowBatch);
							TStaticArray<IScreenSpaceDenoiser::FShadowVisibilityParameters, IScreenSpaceDenoiser::kMaxBatchSize> DenoisingQueue;
							TStaticArray<int32, IScreenSpaceDenoiser::kMaxBatchSize> LightIndices;

							FRDGBuilder GraphBuilder(RHICmdList);

							FSceneTextureParameters SceneTextures;
							SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

							int32 ProcessShadows = 0;

							// Lambda to share the code quicking of the shadow denoiser.
							auto QuickOffDenoisingBatch = [&]() {
								int32 InputParameterCount = 0;
								for (int32 i = 0; i < IScreenSpaceDenoiser::kMaxBatchSize; i++)
								{
									InputParameterCount += DenoisingQueue[i].LightSceneInfo != nullptr ? 1 : 0;
								}

								check(InputParameterCount >= 1);

								TStaticArray<IScreenSpaceDenoiser::FShadowVisibilityOutputs, IScreenSpaceDenoiser::kMaxBatchSize> Outputs;

								RDG_EVENT_SCOPE(GraphBuilder, "%s%s(Shadow BatchSize=%d) %dx%d",
									DenoiserToUse != DefaultDenoiser ? TEXT("ThirdParty ") : TEXT(""),
									DenoiserToUse->GetDebugName(),
									InputParameterCount,
									View.ViewRect.Width(), View.ViewRect.Height());

								DenoiserToUse->DenoiseShadowVisibilityMasks(
									GraphBuilder,
									View,
									&View.PrevViewInfo,
									SceneTextures,
									DenoisingQueue,
									InputParameterCount,
									Outputs);

								for (int32 i = 0; i < InputParameterCount; i++)
								{
									const FLightSceneInfo* LocalLightSceneInfo = DenoisingQueue[i].LightSceneInfo;

									int32 LocalLightIndex = LightIndices[i];
									TRefCountPtr<IPooledRenderTarget>* RefDestination = &PreprocessedShadowMaskTextures[LocalLightIndex - AttenuationLightStart];
									check(*RefDestination == nullptr);

									GraphBuilder.QueueTextureExtraction(Outputs[i].Mask, RefDestination);
									DenoisingQueue[i].LightSceneInfo = nullptr;
								}
							}; // QuickOffDenoisingBatch

							// Ray trace shadows of light that needs, and quick off denoising batch.
							for (int32 LightBatchIndex = LightIndex; LightBatchIndex < SortedLights.Num(); LightBatchIndex++)
							{
								const FSortedLightSceneInfo& BatchSortedLightInfo = SortedLights[LightBatchIndex];
								const FLightSceneInfo& BatchLightSceneInfo = *BatchSortedLightInfo.LightSceneInfo;

								// Denoiser do not support texture rect light important sampling.
								const bool bBatchDrawShadows = BatchSortedLightInfo.SortKey.Fields.bShadowed && !ShouldRenderRayTracingStochasticRectLight(BatchLightSceneInfo);

								if (!bBatchDrawShadows)
									continue;

								const FLightOcclusionType BatchOcclusionType = GetLightOcclusionType(*BatchLightSceneInfo.Proxy);
								if (BatchOcclusionType != FLightOcclusionType::Raytraced)
									continue;

								const bool bRequiresDenoiser = LightRequiresDenosier(BatchLightSceneInfo) && DenoiserMode > 0;

								IScreenSpaceDenoiser::FShadowRayTracingConfig BatchRayTracingConfig;
								BatchRayTracingConfig.RayCountPerPixel = BatchLightSceneInfo.Proxy->GetSamplesPerPixel();

								IScreenSpaceDenoiser::EShadowRequirements DenoiserRequirements = bRequiresDenoiser ?
									DenoiserToUse->GetShadowRequirements(View, BatchLightSceneInfo, BatchRayTracingConfig) :
									IScreenSpaceDenoiser::EShadowRequirements::Bailout;

								// Not worth batching and increase memory pressure if the denoiser do not support this ray tracing config.
								// TODO: add suport for batch with multiple SPP.
								if (bRequiresDenoiser && DenoiserRequirements != IScreenSpaceDenoiser::EShadowRequirements::PenumbraAndClosestOccluder)
								{
									continue;
								}

								// Ray trace the shadow.
								//#dxr_todo: support multiview for the batching case
								FRDGTextureRef RayTracingShadowMaskTexture;
								{
									FRDGTextureDesc Desc = FRDGTextureDesc::Create2DDesc(
										SceneTextures.SceneDepthBuffer->Desc.Extent,
										PF_FloatRGBA,
										FClearValueBinding::Black,
										TexCreate_None,
										TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV,
										/* bInForceSeparateTargetAndShaderResource = */ false);
									RayTracingShadowMaskTexture = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingOcclusion"));
								}

								FRDGTextureRef RayDistanceTexture;
								{
									FRDGTextureDesc Desc = FRDGTextureDesc::Create2DDesc(
										SceneTextures.SceneDepthBuffer->Desc.Extent,
										PF_R16F,
										FClearValueBinding::Black,
										TexCreate_None,
										TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV,
										/* bInForceSeparateTargetAndShaderResource = */ false);
									RayDistanceTexture = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingOcclusionDistance"));
								}

								FRDGTextureRef SubPixelRayTracingShadowMaskTexture = nullptr;
								FRDGTextureUAV* SubPixelRayTracingShadowMaskUAV = nullptr;
								if (bUseHairLighting)
								{
									FRDGTextureDesc Desc = FRDGTextureDesc::Create2DDesc(
										SceneTextures.SceneDepthBuffer->Desc.Extent,
										PF_FloatRGBA,
										FClearValueBinding::Black,
										TexCreate_None,
										TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV,
										/* bInForceSeparateTargetAndShaderResource = */ false);
									SubPixelRayTracingShadowMaskTexture = GraphBuilder.CreateTexture(Desc, TEXT("SubPixelRayTracingOcclusion"));
									SubPixelRayTracingShadowMaskUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SubPixelRayTracingShadowMaskTexture));
								}

								FString BatchLightNameWithLevel;
								GetLightNameForDrawEvent(BatchLightSceneInfo.Proxy, BatchLightNameWithLevel);

								FRDGTextureUAV* RayTracingShadowMaskUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RayTracingShadowMaskTexture));
								FRDGTextureUAV* RayHitDistanceUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RayDistanceTexture));
								FHairStrandsOcclusionResources HairResources = GetHairStrandsResources(ViewIndex, GraphBuilder, HairDatas);
								HairResources.bUseHairVoxel = !BatchLightSceneInfo.Proxy->CastsHairStrandsDeepShadow();
								{
									RDG_EVENT_SCOPE(GraphBuilder, "%s", *BatchLightNameWithLevel);

									// Ray trace the shadow cast by opaque geometries on to hair strands geometries
									// Note: No denoiser is required on this output, as the hair strands are geometrically noisy, which make it hard to denoise
									RenderRayTracingShadows(
										GraphBuilder,
										SceneTextures,
										View,
										BatchLightSceneInfo,
										BatchRayTracingConfig,
										DenoiserRequirements,
										&HairResources,
										RayTracingShadowMaskUAV,
										RayHitDistanceUAV,
										SubPixelRayTracingShadowMaskUAV);
									
									if (HasHairStrandsClusters(ViewIndex, HairDatas))
									{
										TRefCountPtr<IPooledRenderTarget>* RefDestination = &PreprocessedShadowMaskSubPixelTextures[LightBatchIndex - AttenuationLightStart];
										check(*RefDestination == nullptr);

										GraphBuilder.QueueTextureExtraction(SubPixelRayTracingShadowMaskTexture, RefDestination);
									}
								}

								bool bBatchFull = false;

								if (bRequiresDenoiser)
								{
									// Queue the ray tracing output for shadow denoising.
									for (int32 i = 0; i < IScreenSpaceDenoiser::kMaxBatchSize; i++)
									{
										if (DenoisingQueue[i].LightSceneInfo == nullptr)
										{
											DenoisingQueue[i].LightSceneInfo = &BatchLightSceneInfo;
											DenoisingQueue[i].RayTracingConfig = RayTracingConfig;
											DenoisingQueue[i].InputTextures.Mask = RayTracingShadowMaskTexture;
											DenoisingQueue[i].InputTextures.ClosestOccluder = RayDistanceTexture;
											LightIndices[i] = LightBatchIndex;

											// If queue for this light type is full, quick of the batch.
											if ((i + 1) == MaxDenoisingBatchSize)
											{
												QuickOffDenoisingBatch();
												bBatchFull = true;
											}
											break;
										}
										else
										{
											check((i - 1) < IScreenSpaceDenoiser::kMaxBatchSize);
										}
									}
								}
								else
								{
									GraphBuilder.QueueTextureExtraction(RayTracingShadowMaskTexture, &PreprocessedShadowMaskTextures[LightBatchIndex - AttenuationLightStart]);
								}

								// terminate batch if we filled a denoiser batch or hit our max light batch
								ProcessShadows++;
								if (bBatchFull || ProcessShadows == MaxRTShadowBatchSize)
								{
									break;
								}

							} // for (int32 LightBatchIndex = LightIndex; LightIndex < SortedLights.Num(); LightIndex++)

							// Ensures all denoising queues are processed.
							if (DenoisingQueue[0].LightSceneInfo)
							{
								QuickOffDenoisingBatch();
							}

							GraphBuilder.Execute();
						}
					} // end inline batched raytraced shadow

					if (RHI_RAYTRACING && PreprocessedShadowMaskTextures.Num() > 0 && PreprocessedShadowMaskTextures[LightIndex - AttenuationLightStart])
					{
						const uint32 ShadowMaskIndex = LightIndex - AttenuationLightStart;
						ScreenShadowMaskTexture = PreprocessedShadowMaskTextures[ShadowMaskIndex];
						PreprocessedShadowMaskTextures[ShadowMaskIndex] = nullptr;

						// Subp-ixel shadow for hair strands geometries
						if (bUseHairLighting && ShadowMaskIndex < uint32(PreprocessedShadowMaskSubPixelTextures.Num()))
						{
							ScreenShadowMaskSubPixelTexture = PreprocessedShadowMaskSubPixelTextures[ShadowMaskIndex];
							PreprocessedShadowMaskSubPixelTextures[ShadowMaskIndex] = nullptr;
						}

						// Inject deep shadow mask if the light supports it
						if (bUseHairDeepShadow)
						{
							RenderHairStrandsShadowMask(RHICmdList, Views, &LightSceneInfo, HairDatas, ScreenShadowMaskTexture);
						}
					}
					else if (OcclusionType == FLightOcclusionType::Raytraced)
					{
						FRDGBuilder GraphBuilder(RHICmdList);

						FSceneTextureParameters SceneTextures;
						SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

						FRDGTextureRef RayTracingShadowMaskTexture;
						{
							FRDGTextureDesc Desc = FRDGTextureDesc::Create2DDesc(
								SceneTextures.SceneDepthBuffer->Desc.Extent,
								PF_FloatRGBA,
								FClearValueBinding::Black,
								TexCreate_None,
								TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV,
								/* bInForceSeparateTargetAndShaderResource = */ false);
							RayTracingShadowMaskTexture = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingOcclusion"));
						}

						FRDGTextureRef RayDistanceTexture;
						{
							FRDGTextureDesc Desc = FRDGTextureDesc::Create2DDesc(
								SceneTextures.SceneDepthBuffer->Desc.Extent,
								PF_R16F,
								FClearValueBinding::Black,
								TexCreate_None,
								TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV,
								/* bInForceSeparateTargetAndShaderResource = */ false);
							RayDistanceTexture = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingOcclusionDistance"));
						}

						FRDGTextureUAV* RayTracingShadowMaskUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RayTracingShadowMaskTexture));
						FRDGTextureUAV* RayHitDistanceUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RayDistanceTexture));

						FRDGTextureRef SubPixelRayTracingShadowMaskTexture = nullptr;
						FRDGTextureUAV* SubPixelRayTracingShadowMaskUAV = nullptr;
						if (bUseHairLighting)
						{
							FRDGTextureDesc Desc = FRDGTextureDesc::Create2DDesc(
								SceneTextures.SceneDepthBuffer->Desc.Extent,
								PF_FloatRGBA,
								FClearValueBinding::Black,
								TexCreate_None,
								TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV,
								/* bInForceSeparateTargetAndShaderResource = */ false);
							SubPixelRayTracingShadowMaskTexture = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingOcclusion"));
							SubPixelRayTracingShadowMaskUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SubPixelRayTracingShadowMaskTexture));
						}


						FRDGTextureRef RayTracingShadowMaskTileTexture;
						{
							FRDGTextureDesc Desc = FRDGTextureDesc::Create2DDesc(
								SceneTextures.SceneDepthBuffer->Desc.Extent,
								PF_FloatRGBA,
								FClearValueBinding::Black,
								TexCreate_None,
								TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV,
								/* bInForceSeparateTargetAndShaderResource = */ false);
							RayTracingShadowMaskTileTexture = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingOcclusionTile"));
						}

						bool bIsMultiview = Views.Num() > 0;

						for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
						{
							FViewInfo& View = Views[ViewIndex];

							IScreenSpaceDenoiser::FShadowRayTracingConfig RayTracingConfig;
							RayTracingConfig.RayCountPerPixel = LightSceneInfo.Proxy->GetSamplesPerPixel();

							IScreenSpaceDenoiser::EShadowRequirements DenoiserRequirements = IScreenSpaceDenoiser::EShadowRequirements::Bailout;
							if (DenoiserMode != 0 && LightRequiresDenosier(LightSceneInfo))
							{
								DenoiserRequirements = DenoiserToUse->GetShadowRequirements(View, LightSceneInfo, RayTracingConfig);
							}

							FHairStrandsOcclusionResources HairResources = GetHairStrandsResources(ViewIndex, GraphBuilder, HairDatas);
							HairResources.bUseHairVoxel = !bUseHairDeepShadow;

							RenderRayTracingShadows(
								GraphBuilder,
								SceneTextures,
								View,
								LightSceneInfo,
								RayTracingConfig,
								DenoiserRequirements,
								&HairResources,
								RayTracingShadowMaskUAV,
								RayHitDistanceUAV,
								SubPixelRayTracingShadowMaskUAV);

							if (DenoiserRequirements != IScreenSpaceDenoiser::EShadowRequirements::Bailout)
							{
								TStaticArray<IScreenSpaceDenoiser::FShadowVisibilityParameters, IScreenSpaceDenoiser::kMaxBatchSize> InputParameters;
								TStaticArray<IScreenSpaceDenoiser::FShadowVisibilityOutputs, IScreenSpaceDenoiser::kMaxBatchSize> Outputs;

								InputParameters[0].InputTextures.Mask = RayTracingShadowMaskTexture;
								InputParameters[0].InputTextures.ClosestOccluder = RayDistanceTexture;
								InputParameters[0].LightSceneInfo = &LightSceneInfo;
								InputParameters[0].RayTracingConfig = RayTracingConfig;

								int32 InputParameterCount = 1;

								RDG_EVENT_SCOPE(GraphBuilder, "%s%s(Shadow BatchSize=%d) %dx%d",
									DenoiserToUse != DefaultDenoiser ? TEXT("ThirdParty ") : TEXT(""),
									DenoiserToUse->GetDebugName(),
									InputParameterCount,
									View.ViewRect.Width(), View.ViewRect.Height());

								DenoiserToUse->DenoiseShadowVisibilityMasks(
									GraphBuilder,
									View,
									&View.PrevViewInfo,
									SceneTextures,
									InputParameters,
									InputParameterCount,
									Outputs);

								if (bIsMultiview)
								{
									AddDrawTexturePass(GraphBuilder, View, Outputs[0].Mask, RayTracingShadowMaskTileTexture, View.ViewRect.Min, View.ViewRect.Min, View.ViewRect.Size());
									GraphBuilder.QueueTextureExtraction(RayTracingShadowMaskTileTexture, &ScreenShadowMaskTexture);
								}
								else
								{
									GraphBuilder.QueueTextureExtraction(Outputs[0].Mask, &ScreenShadowMaskTexture);
								}
							}
							else
							{
								GraphBuilder.QueueTextureExtraction(RayTracingShadowMaskTexture, &ScreenShadowMaskTexture);
							}

							if (HasHairStrandsClusters(ViewIndex, HairDatas))
							{
								GraphBuilder.QueueTextureExtraction(SubPixelRayTracingShadowMaskTexture, &ScreenShadowMaskSubPixelTexture);
							}
						}

						GraphBuilder.Execute();

						// Inject deep shadow mask if the light supports it
						if (HairDatas && bUseHairDeepShadow)
						{
							RenderHairStrandsShadowMask(RHICmdList, Views, &LightSceneInfo, HairDatas, ScreenShadowMaskTexture);
						}
					}
					else // (OcclusionType == FOcclusionType::Shadowmap)
					{
						for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
						{
							const FViewInfo& View = Views[ViewIndex];
							View.HeightfieldLightingViewInfo.ClearShadowing(View, RHICmdList, LightSceneInfo);
						}
					
						auto ClearShadowMask = [&](TRefCountPtr<IPooledRenderTarget>& InScreenShadowMaskTexture)
						{
							// Clear light attenuation for local lights with a quad covering their extents
							const bool bClearLightScreenExtentsOnly = CVarAllowClearLightSceneExtentsOnly.GetValueOnRenderThread() && SortedLightInfo.SortKey.Fields.LightType != LightType_Directional;
							// All shadows render with min blending
							bool bClearToWhite = !bClearLightScreenExtentsOnly;

							FRHIRenderPassInfo RPInfo(InScreenShadowMaskTexture->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::Load_Store);
							RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(ERenderTargetActions::Load_DontStore, ERenderTargetActions::Load_Store);
							RPInfo.DepthStencilRenderTarget.DepthStencilTarget = SceneContext.GetSceneDepthSurface();
							RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthRead_StencilWrite;
							if (bClearToWhite)
							{
								RPInfo.ColorRenderTargets[0].Action = ERenderTargetActions::Clear_Store;
							}

							TransitionRenderPassTargets(RHICmdList, RPInfo);
							RHICmdList.BeginRenderPass(RPInfo, TEXT("ClearScreenShadowMask"));
							if (bClearLightScreenExtentsOnly)
							{
								SCOPED_DRAW_EVENT(RHICmdList, ClearQuad);
	
								for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
								{
									const FViewInfo& View = Views[ViewIndex];
									FIntRect ScissorRect;

									if (!LightSceneInfo.Proxy->GetScissorRect(ScissorRect, View, View.ViewRect))
									{
										ScissorRect = View.ViewRect;
									}

									if (ScissorRect.Min.X < ScissorRect.Max.X && ScissorRect.Min.Y < ScissorRect.Max.Y)
									{
										RHICmdList.SetViewport(ScissorRect.Min.X, ScissorRect.Min.Y, 0.0f, ScissorRect.Max.X, ScissorRect.Max.Y, 1.0f);
										DrawClearQuad(RHICmdList, true, FLinearColor(1, 1, 1, 1), false, 0, false, 0);
									}
									else
									{
										LightSceneInfo.Proxy->GetScissorRect(ScissorRect, View, View.ViewRect);
									}
								}
							}
							RHICmdList.EndRenderPass();
						};

						ClearShadowMask(ScreenShadowMaskTexture);
						if (ScreenShadowMaskSubPixelTexture)
						{
							ClearShadowMask(ScreenShadowMaskSubPixelTexture);
						}

						RenderShadowProjections(RHICmdList, &LightSceneInfo, ScreenShadowMaskTexture, ScreenShadowMaskSubPixelTexture, HairDatas, bInjectedTranslucentVolume);
					}

					bUsedShadowMaskTexture = true;
				}

				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					const FViewInfo& View = Views[ViewIndex];
					View.HeightfieldLightingViewInfo.ComputeLighting(View, RHICmdList, LightSceneInfo);
				}

				// Render light function to the attenuation buffer.
				if (bDirectLighting)
				{
					if (bDrawLightFunction)
					{
						const bool bLightFunctionRendered = RenderLightFunction(RHICmdList, &LightSceneInfo, ScreenShadowMaskTexture, bDrawShadows, false);
						bUsedShadowMaskTexture |= bLightFunctionRendered;
					}

					if (bDrawPreviewIndicator)
					{
						RenderPreviewShadowsIndicator(RHICmdList, &LightSceneInfo, ScreenShadowMaskTexture, bUsedShadowMaskTexture);
					}

					if (!bDrawShadows)
					{
						INC_DWORD_STAT(STAT_NumLightFunctionOnlyLights);
					}
				}

				if (bUsedShadowMaskTexture)
				{
					check(ScreenShadowMaskTexture);
					RHICmdList.CopyToResolveTarget(ScreenShadowMaskTexture->GetRenderTargetItem().TargetableTexture, ScreenShadowMaskTexture->GetRenderTargetItem().ShaderResourceTexture, FResolveParams(FResolveRect()));
					if (ScreenShadowMaskSubPixelTexture)
					{
						RHICmdList.CopyToResolveTarget(ScreenShadowMaskSubPixelTexture->GetRenderTargetItem().TargetableTexture, ScreenShadowMaskSubPixelTexture->GetRenderTargetItem().ShaderResourceTexture, FResolveParams(FResolveRect()));
					}

					if (!bShadowMaskReadable)
					{
						RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, ScreenShadowMaskTexture->GetRenderTargetItem().ShaderResourceTexture);
						if (ScreenShadowMaskSubPixelTexture)
						{
							RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, ScreenShadowMaskSubPixelTexture->GetRenderTargetItem().ShaderResourceTexture);
						}
						bShadowMaskReadable = true;
					}

					GVisualizeTexture.SetCheckPoint(RHICmdList, ScreenShadowMaskTexture);
					if (ScreenShadowMaskSubPixelTexture)
					{
						GVisualizeTexture.SetCheckPoint(RHICmdList, ScreenShadowMaskSubPixelTexture);
					}
				}

				if(bDirectLighting && !bInjectedTranslucentVolume)
				{
					for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
					{
						SCOPED_DRAW_EVENT(RHICmdList, InjectTranslucentVolume);
						// Accumulate this light's unshadowed contribution to the translucency lighting volume
						InjectTranslucentVolumeLighting(RHICmdList, LightSceneInfo, NULL, Views[ViewIndex], ViewIndex);
					}
				}

				FHairStrandsTransmittanceMaskData TransmittanceMaskData;
				if (bDrawHairShadow)
				{
					TransmittanceMaskData = RenderHairStrandsTransmittanceMask(RHICmdList, Views, &LightSceneInfo, HairDatas, ScreenShadowMaskSubPixelTexture);
				}

				if (ShouldRenderRayTracingStochasticRectLight(LightSceneInfo))
				{
					TRefCountPtr<IPooledRenderTarget> RectLightRT;
					TRefCountPtr<IPooledRenderTarget> HitDistanceRT;
					RenderRayTracingStochasticRectLight(RHICmdList, LightSceneInfo, RectLightRT, HitDistanceRT);
					// #dxr_todo: Denoise RectLight
					CompositeRayTracingSkyLight(RHICmdList, RectLightRT, HitDistanceRT);
				}
				else
				{
					SCOPED_DRAW_EVENT(RHICmdList, StandardDeferredLighting);
					SceneContext.BeginRenderingSceneColor(RHICmdList, ESimpleRenderTargetMode::EExistingColorAndDepth, FExclusiveDepthStencil::DepthRead_StencilWrite, true);

					// ScreenShadowMaskTexture might have been created for a previous light, but only use it if we wrote valid data into it for this light
					IPooledRenderTarget* LightShadowMaskTexture = nullptr;
					IPooledRenderTarget* LightShadowMaskSubPixelTexture = nullptr;
					if (bUsedShadowMaskTexture)
					{
						LightShadowMaskTexture = ScreenShadowMaskTexture;
						LightShadowMaskSubPixelTexture = ScreenShadowMaskSubPixelTexture;
					}

					// Render the light to the scene color buffer, conditionally using the attenuation buffer or a 1x1 white texture as input 
					if (bDirectLighting)
					{
						RenderLight(RHICmdList, &LightSceneInfo, LightShadowMaskTexture, InHairVisibilityViews, false, true);
					}

					SceneContext.FinishRenderingSceneColor(RHICmdList);

					if (bUseHairLighting)
					{
						RenderLightForHair(RHICmdList, &LightSceneInfo, LightShadowMaskSubPixelTexture, &TransmittanceMaskData, InHairVisibilityViews);
					}
				}
			}
		}
	}
}

void FDeferredShadingSceneRenderer::RenderLightArrayForOverlapViewmode(FRHICommandListImmediate& RHICmdList, const TSparseArray<FLightSceneInfoCompact>& LightArray)
{
	for (TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(LightArray); LightIt; ++LightIt)
	{
		const FLightSceneInfoCompact& LightSceneInfoCompact = *LightIt;
		const FLightSceneInfo* LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;

		// Nothing to do for black lights.
		if(LightSceneInfoCompact.Color.IsAlmostBlack())
		{
			continue;
		}

		bool bShouldRender = false;

		// Check if the light is visible in any of the views.
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			bShouldRender |= LightSceneInfo->ShouldRenderLight(Views[ViewIndex]);
		}

		if (bShouldRender
			// Only render shadow casting stationary lights
			&& LightSceneInfo->Proxy->HasStaticShadowing()
			&& !LightSceneInfo->Proxy->HasStaticLighting()
			&& LightSceneInfo->Proxy->CastsStaticShadow())
		{
			RenderLight(RHICmdList, LightSceneInfo, nullptr, nullptr, true, false);
		}
	}
}

void FDeferredShadingSceneRenderer::RenderStationaryLightOverlap(FRHICommandListImmediate& RHICmdList)
{
	if (Scene->bIsEditorScene)
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		SceneContext.BeginRenderingSceneColor(RHICmdList, ESimpleRenderTargetMode::EUninitializedColorExistingDepth, FExclusiveDepthStencil::DepthRead_StencilWrite);

		// Clear to discard base pass values in scene color since we didn't skip that, to have valid scene depths
		DrawClearQuad(RHICmdList, FLinearColor::Black);

		RenderLightArrayForOverlapViewmode(RHICmdList, Scene->Lights);

		//Note: making use of FScene::InvisibleLights, which contains lights that haven't been added to the scene in the same way as visible lights
		// So code called by RenderLightArrayForOverlapViewmode must be careful what it accesses
		RenderLightArrayForOverlapViewmode(RHICmdList, Scene->InvisibleLights);

		SceneContext.FinishRenderingSceneColor(RHICmdList);
	}
}

/** Sets up rasterizer and depth state for rendering bounding geometry in a deferred pass. */
void SetBoundingGeometryRasterizerAndDepthState(FGraphicsPipelineStateInitializer& GraphicsPSOInit, const FViewInfo& View, const FSphere& LightBounds)
{
	const bool bCameraInsideLightGeometry = ((FVector)View.ViewMatrices.GetViewOrigin() - LightBounds.Center).SizeSquared() < FMath::Square(LightBounds.W * 1.05f + View.NearClippingDistance * 2.0f)
		// Always draw backfaces in ortho
		//@todo - accurate ortho camera / light intersection
		|| !View.IsPerspectiveProjection();

	if (bCameraInsideLightGeometry)
	{
		// Render backfaces with depth tests disabled since the camera is inside (or close to inside) the light geometry
		GraphicsPSOInit.RasterizerState = View.bReverseCulling ? TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI() : TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI();
	}
	else
	{
		// Render frontfaces with depth tests on to get the speedup from HiZ since the camera is outside the light geometry
		GraphicsPSOInit.RasterizerState = View.bReverseCulling ? TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI() : TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI();
	}

	GraphicsPSOInit.DepthStencilState =
		bCameraInsideLightGeometry
		? TStaticDepthStencilState<false, CF_Always>::GetRHI()
		: TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI();
}

template<bool bUseIESProfile, bool bRadialAttenuation, bool bInverseSquaredFalloff>
static void SetShaderTemplLightingSimple(
	FRHICommandList& RHICmdList,
	FGraphicsPipelineStateInitializer& GraphicsPSOInit,
	const FViewInfo& View,
	const TShaderRef<FShader>& VertexShader,
	const FSimpleLightEntry& SimpleLight,
	const FSimpleLightPerViewEntry& SimpleLightPerViewData)
{
	FDeferredLightPS::FPermutationDomain PermutationVector;
	PermutationVector.Set< FDeferredLightPS::FSourceShapeDim >( ELightSourceShape::Capsule );
	PermutationVector.Set< FDeferredLightPS::FIESProfileDim >( bUseIESProfile );
	PermutationVector.Set< FDeferredLightPS::FInverseSquaredDim >( bInverseSquaredFalloff );
	PermutationVector.Set< FDeferredLightPS::FVisualizeCullingDim >( View.Family->EngineShowFlags.VisualizeLightCulling );
	PermutationVector.Set< FDeferredLightPS::FLightingChannelsDim >( false );
	PermutationVector.Set< FDeferredLightPS::FTransmissionDim >( false );
	PermutationVector.Set< FDeferredLightPS::FHairLighting>( 0 );
	PermutationVector.Set < FDeferredLightPS::FAtmosphereTransmittance >( false );

	TShaderMapRef< FDeferredLightPS > PixelShader( View.ShaderMap, PermutationVector );
	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
	PixelShader->SetParametersSimpleLight(RHICmdList, View, SimpleLight, SimpleLightPerViewData);
}

// Use DBT to allow work culling on shadow lights
void CalculateLightNearFarDepthFromBounds(const FViewInfo& View, const FSphere &LightBounds, float &NearDepth, float &FarDepth)
{
	const FMatrix ViewProjection = View.ViewMatrices.GetViewProjectionMatrix();
	const FVector ViewDirection = View.GetViewDirection();

	// push camera relative bounds center along view vec by its radius
	const FVector FarPoint = LightBounds.Center + LightBounds.W * ViewDirection;
	const FVector4 FarPoint4 = FVector4(FarPoint, 1.f);
	const FVector4 FarPoint4Clip = ViewProjection.TransformFVector4(FarPoint4);
	FarDepth = FarPoint4Clip.Z / FarPoint4Clip.W;

	// pull camera relative bounds center along -view vec by its radius
	const FVector NearPoint = LightBounds.Center - LightBounds.W * ViewDirection;
	const FVector4 NearPoint4 = FVector4(NearPoint, 1.f);
	const FVector4 NearPoint4Clip = ViewProjection.TransformFVector4(NearPoint4);
	NearDepth = NearPoint4Clip.Z / NearPoint4Clip.W;

	// negative means behind view, but we use a NearClipPlane==1.f depth

	if (NearPoint4Clip.W < 0)
		NearDepth = 1;

	if (FarPoint4Clip.W < 0)
		FarDepth = 1;

	NearDepth = FMath::Clamp(NearDepth, 0.0f, 1.0f);
	FarDepth = FMath::Clamp(FarDepth, 0.0f, 1.0f);

}

/**
 * Used by RenderLights to render a light to the scene color buffer.
 *
 * @param LightSceneInfo Represents the current light
 * @param LightIndex The light's index into FScene::Lights
 * @return true if anything got rendered
 */

void FDeferredShadingSceneRenderer::RenderLight(FRHICommandList& RHICmdList, const FLightSceneInfo* LightSceneInfo, IPooledRenderTarget* ScreenShadowMaskTexture, const FHairStrandsVisibilityViews* InHairVisibilityViews,  bool bRenderOverlap, bool bIssueDrawEvent)
{
	SCOPE_CYCLE_COUNTER(STAT_DirectLightRenderingTime);
	INC_DWORD_STAT(STAT_NumLightsUsingStandardDeferred);
	SCOPED_CONDITIONAL_DRAW_EVENT(RHICmdList, StandardDeferredLighting, bIssueDrawEvent);

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI();

	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	const FSphere LightBounds = LightSceneInfo->Proxy->GetBoundingSphere();
	const bool bTransmission = LightSceneInfo->Proxy->Transmission();

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];

		// Ensure the light is valid for this view
		if (!LightSceneInfo->ShouldRenderLight(View))
		{
			continue;
		}

		bool bUseIESTexture = false;

		if(View.Family->EngineShowFlags.TexturedLightProfiles)
		{
			bUseIESTexture = (LightSceneInfo->Proxy->GetIESTextureResource() != 0);
		}

		// Set the device viewport for the view.
		RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

		FRenderLightParams RenderLightParams;
		const bool bHairLighting = InHairVisibilityViews && ViewIndex < InHairVisibilityViews->HairDatas.Num() && InHairVisibilityViews->HairDatas[ViewIndex].CategorizationTexture != nullptr;
		if (bHairLighting)
		{
			RenderLightParams.HairCategorizationTexture = InHairVisibilityViews->HairDatas[ViewIndex].CategorizationTexture;
		}
		if (LightSceneInfo->Proxy->GetLightType() == LightType_Directional)
		{
			// Turn DBT back off
			GraphicsPSOInit.bDepthBounds = false;
			TShaderMapRef<TDeferredLightVS<false> > VertexShader(View.ShaderMap);

			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			if (bRenderOverlap)
			{
				TShaderMapRef<TDeferredLightOverlapPS<false> > PixelShader(View.ShaderMap);
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
				PixelShader->SetParameters(RHICmdList, View, LightSceneInfo);
			}
			else
			{
				const bool bAtmospherePerPixelTransmittance = LightSceneInfo->Proxy->IsUsedAsAtmosphereSunLight() && ShouldApplyAtmosphereLightPerPixelTransmittance(Scene, View.Family->EngineShowFlags);

				FDeferredLightPS::FPermutationDomain PermutationVector;
				PermutationVector.Set< FDeferredLightPS::FSourceShapeDim >( ELightSourceShape::Directional );
				PermutationVector.Set< FDeferredLightPS::FIESProfileDim >( false );
				PermutationVector.Set< FDeferredLightPS::FInverseSquaredDim >( false );
				PermutationVector.Set< FDeferredLightPS::FVisualizeCullingDim >( View.Family->EngineShowFlags.VisualizeLightCulling );
				PermutationVector.Set< FDeferredLightPS::FLightingChannelsDim >( View.bUsesLightingChannels );
				PermutationVector.Set< FDeferredLightPS::FTransmissionDim >( bTransmission );
				PermutationVector.Set< FDeferredLightPS::FHairLighting>(bHairLighting ? 1 : 0);
				// Only directional lights are rendered in this path, so we only need to check if it is use to light the atmosphere
				PermutationVector.Set< FDeferredLightPS::FAtmosphereTransmittance >(bAtmospherePerPixelTransmittance);

				TShaderMapRef< FDeferredLightPS > PixelShader( View.ShaderMap, PermutationVector );
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
				PixelShader->SetParameters(RHICmdList, View, LightSceneInfo, ScreenShadowMaskTexture, (bHairLighting) ? &RenderLightParams : nullptr);
			}

			VertexShader->SetParameters(RHICmdList, View, LightSceneInfo);

			// Apply the directional light as a full screen quad
			DrawRectangle(
				RHICmdList,
				0, 0,
				View.ViewRect.Width(), View.ViewRect.Height(),
				View.ViewRect.Min.X, View.ViewRect.Min.Y,
				View.ViewRect.Width(), View.ViewRect.Height(),
				View.ViewRect.Size(),
				FSceneRenderTargets::Get(RHICmdList).GetBufferSizeXY(),
				VertexShader,
				EDRF_UseTriangleOptimization);
		}
		else
		{
			// Use DBT to allow work culling on shadow lights
			// Disable depth bound when hair rendering is enabled as this rejects partially covered pixel write (with opaque background)
			GraphicsPSOInit.bDepthBounds = GSupportsDepthBoundsTest && GAllowDepthBoundsTest != 0;

			TShaderMapRef<TDeferredLightVS<true> > VertexShader(View.ShaderMap);

			SetBoundingGeometryRasterizerAndDepthState(GraphicsPSOInit, View, LightBounds);

			if (bRenderOverlap)
			{
				TShaderMapRef<TDeferredLightOverlapPS<true> > PixelShader(View.ShaderMap);
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
				PixelShader->SetParameters(RHICmdList, View, LightSceneInfo);
			}
			else
			{
				FDeferredLightPS::FPermutationDomain PermutationVector;
				PermutationVector.Set< FDeferredLightPS::FSourceShapeDim >( LightSceneInfo->Proxy->IsRectLight() ? ELightSourceShape::Rect : ELightSourceShape::Capsule );
				PermutationVector.Set< FDeferredLightPS::FSourceTextureDim >( LightSceneInfo->Proxy->IsRectLight() && LightSceneInfo->Proxy->HasSourceTexture() );
				PermutationVector.Set< FDeferredLightPS::FIESProfileDim >( bUseIESTexture );
				PermutationVector.Set< FDeferredLightPS::FInverseSquaredDim >( LightSceneInfo->Proxy->IsInverseSquared() );
				PermutationVector.Set< FDeferredLightPS::FVisualizeCullingDim >( View.Family->EngineShowFlags.VisualizeLightCulling );
				PermutationVector.Set< FDeferredLightPS::FLightingChannelsDim >( View.bUsesLightingChannels );
				PermutationVector.Set< FDeferredLightPS::FTransmissionDim >( bTransmission );
				PermutationVector.Set< FDeferredLightPS::FHairLighting>(bHairLighting ? 1 : 0);
				PermutationVector.Set < FDeferredLightPS::FAtmosphereTransmittance >(false);

				TShaderMapRef< FDeferredLightPS > PixelShader( View.ShaderMap, PermutationVector );
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
				PixelShader->SetParameters(RHICmdList, View, LightSceneInfo, ScreenShadowMaskTexture, (bHairLighting) ? &RenderLightParams : nullptr);
			}

			VertexShader->SetParameters(RHICmdList, View, LightSceneInfo);

			// Use DBT to allow work culling on shadow lights
			if (GraphicsPSOInit.bDepthBounds)
			{
				// Can use the depth bounds test to skip work for pixels which won't be touched by the light (i.e outside the depth range)
				float NearDepth = 1.f;
				float FarDepth = 0.f;
				CalculateLightNearFarDepthFromBounds(View,LightBounds,NearDepth,FarDepth);

				if (NearDepth <= FarDepth)
				{
					NearDepth = 1.0f;
					FarDepth = 0.0f;
				}

				// UE4 uses reversed depth, so far < near
				RHICmdList.SetDepthBounds(FarDepth, NearDepth);
			}

			if( LightSceneInfo->Proxy->GetLightType() == LightType_Point ||
				LightSceneInfo->Proxy->GetLightType() == LightType_Rect )
			{
				// Apply the point or spot light with some approximate bounding geometry,
				// So we can get speedups from depth testing and not processing pixels outside of the light's influence.
				StencilingGeometry::DrawSphere(RHICmdList);
			}
			else if (LightSceneInfo->Proxy->GetLightType() == LightType_Spot)
			{
				StencilingGeometry::DrawCone(RHICmdList);
			}
		}
	}
}

void FDeferredShadingSceneRenderer::RenderLightForHair(
	FRHICommandList& RHICmdList, 
	const FLightSceneInfo* LightSceneInfo, 
	IPooledRenderTarget* HairShadowMaskTexture, 
	FHairStrandsTransmittanceMaskData* InTransmittanceMaskData, 
	const FHairStrandsVisibilityViews* InHairVisibilityViews)
{
	SCOPE_CYCLE_COUNTER(STAT_DirectLightRenderingTime);
	INC_DWORD_STAT(STAT_NumLightsUsingStandardDeferred);
	SCOPED_CONDITIONAL_DRAW_EVENT(RHICmdList, StandardDeferredLighting_Hair, true);
	

	const bool bHairRenderingEnabled = InTransmittanceMaskData && InHairVisibilityViews && (LightSceneInfo->Proxy->CastsHairStrandsDeepShadow() || IsHairStrandsVoxelizationEnable());
	if (!bHairRenderingEnabled)
	{
		return;
	}


	const FSphere LightBounds = LightSceneInfo->Proxy->GetBoundingSphere();
	const bool bTransmission = LightSceneInfo->Proxy->Transmission();

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];

		// Ensure the light is valid for this view
		if (!LightSceneInfo->ShouldRenderLight(View) || ViewIndex >= InHairVisibilityViews->HairDatas.Num())
		{
			continue;
		}

		check(RHICmdList.IsOutsideRenderPass());

		const FHairStrandsVisibilityData& HairVisibilityData = InHairVisibilityViews->HairDatas[ViewIndex];
		if (!HairVisibilityData.SampleLightingBuffer)
		{
			continue;
		}

		FRenderLightParams RenderLightParams;
		RenderLightParams.DeepShadow_TransmittanceMaskBuffer = InTransmittanceMaskData ? InTransmittanceMaskData->TransmittanceMaskSRV : nullptr;
		RenderLightParams.DeepShadow_TransmittanceMaskBufferMaxCount = InTransmittanceMaskData && InTransmittanceMaskData->TransmittanceMask ? InTransmittanceMaskData->TransmittanceMask->Desc.NumElements : 0;
		RenderLightParams.ScreenShadowMaskSubPixelTexture = HairShadowMaskTexture;
		RenderLightParams.HairVisibilityNodeOffsetAndCount = HairVisibilityData.NodeIndex;
		RenderLightParams.HairVisibilityNodeDataSRV = HairVisibilityData.NodeDataSRV;
		RenderLightParams.HairVisibilityNodeCoordsSRV = HairVisibilityData.NodeCoordSRV;
		RenderLightParams.HairCategorizationTexture = HairVisibilityData.CategorizationTexture;

		FRHIRenderPassInfo RPInfo(HairVisibilityData.SampleLightingBuffer->GetRenderTargetItem().TargetableTexture, MakeRenderTargetActions(ERenderTargetLoadAction::ELoad, ERenderTargetStoreAction::EStore));
		RHICmdList.BeginRenderPass(RPInfo, TEXT("HairLighting"));
		RHICmdList.SetViewport(0, 0, 0.0f, HairVisibilityData.SampleLightingViewportResolution.X, HairVisibilityData.SampleLightingViewportResolution.Y, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Max, BF_SourceAlpha, BF_DestAlpha>::GetRHI();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		FDeferredLightPS::FPermutationDomain PermutationVector;
		if (LightSceneInfo->Proxy->GetLightType() == LightType_Directional)
		{
			PermutationVector.Set< FDeferredLightPS::FSourceShapeDim >(ELightSourceShape::Directional);
			PermutationVector.Set< FDeferredLightPS::FSourceTextureDim >(false);
			PermutationVector.Set< FDeferredLightPS::FIESProfileDim >(false);
			PermutationVector.Set< FDeferredLightPS::FInverseSquaredDim >(false);
		}
		else
		{
			const bool bUseIESTexture = View.Family->EngineShowFlags.TexturedLightProfiles && LightSceneInfo->Proxy->GetIESTextureResource() != 0;
			PermutationVector.Set< FDeferredLightPS::FSourceShapeDim >(LightSceneInfo->Proxy->IsRectLight() ? ELightSourceShape::Rect : ELightSourceShape::Capsule);
			PermutationVector.Set< FDeferredLightPS::FSourceTextureDim >(LightSceneInfo->Proxy->IsRectLight() && LightSceneInfo->Proxy->HasSourceTexture());
			PermutationVector.Set< FDeferredLightPS::FIESProfileDim >(bUseIESTexture);
			PermutationVector.Set< FDeferredLightPS::FInverseSquaredDim >(LightSceneInfo->Proxy->IsInverseSquared());
		}
		PermutationVector.Set< FDeferredLightPS::FLightingChannelsDim >(View.bUsesLightingChannels);
		PermutationVector.Set< FDeferredLightPS::FVisualizeCullingDim >(false);
		PermutationVector.Set< FDeferredLightPS::FTransmissionDim >(false);
		PermutationVector.Set< FDeferredLightPS::FHairLighting>(2);

		TShaderMapRef<TDeferredLightHairVS> VertexShader(View.ShaderMap);
		TShaderMapRef<FDeferredLightPS> PixelShader(View.ShaderMap, PermutationVector);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
		GraphicsPSOInit.bDepthBounds = false;
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		VertexShader->SetParameters(RHICmdList, View, &HairVisibilityData);
		PixelShader->SetParameters(RHICmdList, View,  LightSceneInfo, HairShadowMaskTexture, bHairRenderingEnabled ? &RenderLightParams : nullptr);

		RHICmdList.SetStreamSource(0, nullptr, 0);
		RHICmdList.DrawPrimitive(0, 1, 1);

		RHICmdList.EndRenderPass();
	}
}

// Forward lighting version for hair
void FDeferredShadingSceneRenderer::RenderLightsForHair(FRHICommandListImmediate& RHICmdList, FSortedLightSetSceneInfo &SortedLightSet, const FHairStrandsDatas* HairDatas, TRefCountPtr<IPooledRenderTarget>& InScreenShadowMaskSubPixelTexture)
{
	const FSimpleLightArray &SimpleLights = SortedLightSet.SimpleLights;
	const TArray<FSortedLightSceneInfo, SceneRenderingAllocator> &SortedLights = SortedLightSet.SortedLights;
	const int32 AttenuationLightStart = SortedLightSet.AttenuationLightStart;
	const int32 SimpleLightsEnd = SortedLightSet.SimpleLightsEnd;

	const bool bUseHairLighting = HairDatas != nullptr;
	if (ViewFamily.EngineShowFlags.DirectLighting && bUseHairLighting)
	{
		SCOPED_DRAW_EVENT(RHICmdList, DirectLighting);

		//FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		for (int32 LightIndex = AttenuationLightStart; LightIndex < SortedLights.Num(); LightIndex++)
		{
			const FSortedLightSceneInfo& SortedLightInfo = SortedLights[LightIndex];
			const FLightSceneInfo& LightSceneInfo = *SortedLightInfo.LightSceneInfo;
			if (LightSceneInfo.Proxy)
			{
				TRefCountPtr<IPooledRenderTarget> ScreenShadowMaskSubPixelTexture = InScreenShadowMaskSubPixelTexture;

				const bool bDrawHairShadow = SortedLightInfo.SortKey.Fields.bShadowed;
				FHairStrandsTransmittanceMaskData TransmittanceMaskData;
				if (bDrawHairShadow)
				{
					TransmittanceMaskData = RenderHairStrandsTransmittanceMask(RHICmdList, Views, &LightSceneInfo, HairDatas, ScreenShadowMaskSubPixelTexture);
				}

				RenderLightForHair(RHICmdList, 
					&LightSceneInfo,
					ScreenShadowMaskSubPixelTexture,
					bDrawHairShadow ? &TransmittanceMaskData : nullptr,
					&HairDatas->HairVisibilityViews);
			}
		}
	}
}

void FDeferredShadingSceneRenderer::RenderSimpleLightsStandardDeferred(FRHICommandListImmediate& RHICmdList, const FSimpleLightArray& SimpleLights)
{
	SCOPE_CYCLE_COUNTER(STAT_DirectLightRenderingTime);
	INC_DWORD_STAT_BY(STAT_NumLightsUsingStandardDeferred, SimpleLights.InstanceData.Num());
	SCOPED_DRAW_EVENT(RHICmdList, StandardDeferredSimpleLights);

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	// Use additive blending for color
	GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	const int32 NumViews = Views.Num();
	for (int32 LightIndex = 0; LightIndex < SimpleLights.InstanceData.Num(); LightIndex++)
	{
		const FSimpleLightEntry& SimpleLight = SimpleLights.InstanceData[LightIndex];

		for (int32 ViewIndex = 0; ViewIndex < NumViews; ViewIndex++)
		{
			const FSimpleLightPerViewEntry& SimpleLightPerViewData = SimpleLights.GetViewDependentData(LightIndex, ViewIndex, NumViews);
			const FSphere LightBounds(SimpleLightPerViewData.Position, SimpleLight.Radius);

			FViewInfo& View = Views[ViewIndex];

			// Set the device viewport for the view.
			RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

			TShaderMapRef<TDeferredLightVS<true> > VertexShader(View.ShaderMap);

			SetBoundingGeometryRasterizerAndDepthState(GraphicsPSOInit, View, LightBounds);

			if (SimpleLight.Exponent == 0)
			{
				// inverse squared
				SetShaderTemplLightingSimple<false, true, true>(RHICmdList, GraphicsPSOInit, View, VertexShader, SimpleLight, SimpleLightPerViewData);
			}
			else
			{
				// light's exponent, not inverse squared
				SetShaderTemplLightingSimple<false, true, false>(RHICmdList, GraphicsPSOInit, View, VertexShader, SimpleLight, SimpleLightPerViewData);
			}

			VertexShader->SetSimpleLightParameters(RHICmdList, View, LightBounds);

			// Apply the point or spot light with some approximately bounding geometry,
			// So we can get speedups from depth testing and not processing pixels outside of the light's influence.
			StencilingGeometry::DrawSphere(RHICmdList);
		}
	}
}
