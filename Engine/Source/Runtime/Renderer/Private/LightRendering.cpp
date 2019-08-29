// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LightRendering.cpp: Light rendering implementation.
=============================================================================*/

#include "LightRendering.h"
#include "DeferredShadingRenderer.h"
#include "LightPropagationVolume.h"
#include "ScenePrivate.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PipelineStateCache.h"
#include "ClearQuad.h"
#include "Engine/SubsurfaceProfile.h"
#include "ShowFlags.h"

DECLARE_GPU_STAT(Lights);

IMPLEMENT_UNIFORM_BUFFER_STRUCT(FDeferredLightUniformStruct,TEXT("DeferredLightUniforms"));

extern int32 GUseTranslucentLightingVolumes;
ENGINE_API const IPooledRenderTarget* GetSubsufaceProfileTexture_RT(FRHICommandListImmediate& RHICmdList);


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
	RHICmdList.DrawIndexedPrimitive(StencilingGeometry::GStencilSphereIndexBuffer.IndexBufferRHI, PT_TriangleList, 0, 0,
		StencilingGeometry::GStencilSphereVertexBuffer.GetVertexCount(), 0, 
		StencilingGeometry::GStencilSphereIndexBuffer.GetIndexCount() / 3, 1);
}
		
void StencilingGeometry::DrawVectorSphere(FRHICommandList& RHICmdList)
{
	RHICmdList.SetStreamSource(0, StencilingGeometry::GStencilSphereVectorBuffer.VertexBufferRHI, 0);
	RHICmdList.DrawIndexedPrimitive(StencilingGeometry::GStencilSphereIndexBuffer.IndexBufferRHI, PT_TriangleList, 0, 0,
									StencilingGeometry::GStencilSphereVectorBuffer.GetVertexCount(), 0,
									StencilingGeometry::GStencilSphereIndexBuffer.GetIndexCount() / 3, 1);
}

void StencilingGeometry::DrawCone(FRHICommandList& RHICmdList)
{
	// No Stream Source needed since it will generate vertices on the fly
	RHICmdList.SetStreamSource(0, StencilingGeometry::GStencilConeVertexBuffer.VertexBufferRHI, 0);

	RHICmdList.DrawIndexedPrimitive(StencilingGeometry::GStencilConeIndexBuffer.IndexBufferRHI, PT_TriangleList, 0, 0,
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
	DECLARE_GLOBAL_SHADER(FDeferredLightPS)

	class FSourceShapeDim		: SHADER_PERMUTATION_ENUM_CLASS("LIGHT_SOURCE_SHAPE", ELightSourceShape);
	class FIESProfileDim		: SHADER_PERMUTATION_BOOL("USE_IES_PROFILE");
	class FInverseSquaredDim	: SHADER_PERMUTATION_BOOL("INVERSE_SQUARED_FALLOFF");
	class FVisualizeCullingDim	: SHADER_PERMUTATION_BOOL("VISUALIZE_LIGHT_CULLING");
	class FLightingChannelsDim	: SHADER_PERMUTATION_BOOL("USE_LIGHTING_CHANNELS");
	class FTransmissionDim		: SHADER_PERMUTATION_BOOL("USE_TRANSMISSION");

	using FPermutationDomain = TShaderPermutationDomain<
		FSourceShapeDim,
		FIESProfileDim,
		FInverseSquaredDim,
		FVisualizeCullingDim,
		FLightingChannelsDim,
		FTransmissionDim >;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if( PermutationVector.Get< FSourceShapeDim >() == ELightSourceShape::Directional && (
			PermutationVector.Get< FIESProfileDim >() ||
			PermutationVector.Get< FInverseSquaredDim >() ) )
		{
			return false;
		}

		if( PermutationVector.Get< FSourceShapeDim >() == ELightSourceShape::Rect &&
			!PermutationVector.Get< FInverseSquaredDim >() )
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

		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4);
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
	}

	FDeferredLightPS()
	{}

public:
	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, const FLightSceneInfo* LightSceneInfo, IPooledRenderTarget* ScreenShadowMaskTexture)
	{
		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();
		SetParametersBase(RHICmdList, ShaderRHI, View, ScreenShadowMaskTexture, LightSceneInfo->Proxy->GetIESTextureResource());
		SetDeferredLightParameters(RHICmdList, ShaderRHI, GetUniformBufferParameter<FDeferredLightUniformStruct>(), LightSceneInfo, View);
	}

	void SetParametersSimpleLight(FRHICommandList& RHICmdList, const FSceneView& View, const FSimpleLightEntry& SimpleLight, const FSimpleLightPerViewEntry& SimpleLightPerViewData)
	{
		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();
		SetParametersBase(RHICmdList, ShaderRHI, View, NULL, NULL);
		SetSimpleDeferredLightParameters(RHICmdList, ShaderRHI, GetUniformBufferParameter<FDeferredLightUniformStruct>(), SimpleLight, SimpleLightPerViewData, View);
	}

	virtual bool Serialize(FArchive& Ar) override
	{		
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << SceneTextureParameters;
		Ar << LightAttenuationTexture;
		Ar << LightAttenuationTextureSampler;
		Ar << LTCMatTexture;
		Ar << LTCMatSampler;
		Ar << LTCAmpTexture;
		Ar << LTCAmpSampler;
		Ar << IESTexture;
		Ar << IESTextureSampler;
		Ar << LightingChannelsTexture;
		Ar << LightingChannelsSampler;
		Ar << TransmissionProfilesTexture;
		Ar << TransmissionProfilesLinearSampler;
		return bShaderHasOutdatedParameters;
	}

private:

	void SetParametersBase(FRHICommandList& RHICmdList, const FPixelShaderRHIParamRef ShaderRHI, const FSceneView& View, IPooledRenderTarget* ScreenShadowMaskTexture, FTexture* IESTextureResource)
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
			FTextureRHIParamRef TextureRHI = IESTextureResource ? IESTextureResource->TextureRHI : GSystemTextures.WhiteDummy->GetRenderTargetItem().TargetableTexture;

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
			FTextureRHIParamRef LightingChannelsTextureRHI = SceneRenderTargets.LightingChannels ? SceneRenderTargets.LightingChannels->GetRenderTargetItem().ShaderResourceTexture : GSystemTextures.WhiteDummy->GetRenderTargetItem().TargetableTexture;

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
	}

	FSceneTextureShaderParameters SceneTextureParameters;
	FShaderResourceParameter LightAttenuationTexture;
	FShaderResourceParameter LightAttenuationTextureSampler;
	FShaderResourceParameter LTCMatTexture;
	FShaderResourceParameter LTCMatSampler;
	FShaderResourceParameter LTCAmpTexture;
	FShaderResourceParameter LTCAmpSampler;
	FShaderResourceParameter IESTexture;
	FShaderResourceParameter IESTextureSampler;
	FShaderResourceParameter LightingChannelsTexture;
	FShaderResourceParameter LightingChannelsSampler;
	FShaderResourceParameter TransmissionProfilesTexture;
	FShaderResourceParameter TransmissionProfilesLinearSampler;
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
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4);
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
		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI,View.ViewUniformBuffer);
		const float HasValidChannelValue = LightSceneInfo->Proxy->GetPreviewShadowMapChannel() == INDEX_NONE ? 0.0f : 1.0f;
		SetShaderValue(RHICmdList, ShaderRHI, HasValidChannel, HasValidChannelValue);
		SceneTextureParameters.Set(RHICmdList, ShaderRHI, View.FeatureLevel, ESceneTextureSetupMode::All);
		SetDeferredLightParameters(RHICmdList, ShaderRHI, GetUniformBufferParameter<FDeferredLightUniformStruct>(), LightSceneInfo, View);
	}

	virtual bool Serialize(FArchive& Ar) override
	{		
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << HasValidChannel;
		Ar << SceneTextureParameters;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter HasValidChannel;
	FSceneTextureShaderParameters SceneTextureParameters;
};

IMPLEMENT_SHADER_TYPE(template<>, TDeferredLightOverlapPS<true>, TEXT("/Engine/Private/StationaryLightOverlapShaders.usf"), TEXT("OverlapRadialPixelMain"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, TDeferredLightOverlapPS<false>, TEXT("/Engine/Private/StationaryLightOverlapShaders.usf"), TEXT("OverlapDirectionalPixelMain"), SF_Pixel);

/** Gathers simple lights from visible primtives in the passed in views. */
void FSceneRenderer::GatherSimpleLights(const FSceneViewFamily& ViewFamily, const TArray<FViewInfo>& Views, FSimpleLightArray& SimpleLights)
{
	TArray<const FPrimitiveSceneInfo*, SceneRenderingAllocator> PrimitivesWithSimpleLights;

	// Gather visible primitives from all views that might have simple lights
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		for (int32 PrimitiveIndex = 0; PrimitiveIndex < View.VisibleDynamicPrimitives.Num(); PrimitiveIndex++)
		{
			const FPrimitiveSceneInfo* PrimitiveSceneInfo = View.VisibleDynamicPrimitives[PrimitiveIndex];
			const int32 PrimitiveId = PrimitiveSceneInfo->GetIndex();
			const FPrimitiveViewRelevance& PrimitiveViewRelevance = View.PrimitiveViewRelevanceMap[PrimitiveId];

			if (PrimitiveViewRelevance.bHasSimpleLights)
			{
				// TArray::AddUnique is slow, but not expecting many entries in PrimitivesWithSimpleLights
				PrimitivesWithSimpleLights.AddUnique(PrimitiveSceneInfo);
			}
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
			FullLevelName = FullLevelName.Mid(LastSlashIndex + 1, FullLevelName.Len() - (LastSlashIndex + 1));
		}

		LightNameWithLevel = FullLevelName + TEXT(".") + LightProxy->GetComponentName().ToString();
	}
#endif
}

extern int32 GbEnableAsyncComputeTranslucencyLightingVolumeClear;

uint32 GetShadowQuality();

/** Renders the scene's lighting. */
void FDeferredShadingSceneRenderer::RenderLights(FRHICommandListImmediate& RHICmdList)
{
	SCOPED_NAMED_EVENT(FDeferredShadingSceneRenderer_RenderLights, FColor::Emerald);
	SCOPED_DRAW_EVENT(RHICmdList, Lights);
	SCOPED_GPU_STAT(RHICmdList, Lights);


	bool bStencilBufferDirty = false;	// The stencil buffer should've been cleared to 0 already

	SCOPE_CYCLE_COUNTER(STAT_LightingDrawTime);
	SCOPE_CYCLE_COUNTER(STAT_LightRendering);

	FSimpleLightArray SimpleLights;
	if (bAllowSimpleLights)
	{
		GatherSimpleLights(ViewFamily, Views, SimpleLights);
	}

	TArray<FSortedLightSceneInfo, SceneRenderingAllocator> SortedLights;
	SortedLights.Empty(Scene->Lights.Num());

	bool bDynamicShadows = ViewFamily.EngineShowFlags.DynamicShadows && GetShadowQuality() > 0;
	
	// Build a list of visible lights.
	for (TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights); LightIt; ++LightIt)
	{
		const FLightSceneInfoCompact& LightSceneInfoCompact = *LightIt;
		const FLightSceneInfo* const LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;

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
					break;
				}
			}
		}
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

	{
		SCOPED_DRAW_EVENT(RHICmdList, DirectLighting);

		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

		int32 AttenuationLightStart = SortedLights.Num();
		int32 SupportedByTiledDeferredLightEnd = SortedLights.Num();
		bool bAnyUnsupportedByTiledDeferred = false;

		// Iterate over all lights to be rendered and build ranges for tiled deferred and unshadowed lights
		for (int32 LightIndex = 0; LightIndex < SortedLights.Num(); LightIndex++)
		{
			const FSortedLightSceneInfo& SortedLightInfo = SortedLights[LightIndex];
			bool bDrawShadows = SortedLightInfo.SortKey.Fields.bShadowed;
			bool bDrawLightFunction = SortedLightInfo.SortKey.Fields.bLightFunction;
			bool bTextureLightProfile = SortedLightInfo.SortKey.Fields.bTextureProfile;
			bool bLightingChannels = SortedLightInfo.SortKey.Fields.bUsesLightingChannels;

			if (bTextureLightProfile && SupportedByTiledDeferredLightEnd == SortedLights.Num())
			{
				// Mark the first index to not support tiled deferred due to texture light profile
				SupportedByTiledDeferredLightEnd = LightIndex;
			}

			if (bDrawShadows || bDrawLightFunction || bLightingChannels)
			{
				AttenuationLightStart = LightIndex;

				if (SupportedByTiledDeferredLightEnd == SortedLights.Num())
				{
					// Mark the first index to not support tiled deferred due to shadowing
					SupportedByTiledDeferredLightEnd = LightIndex;
				}
				break;
			}

			if (LightIndex < SupportedByTiledDeferredLightEnd)
			{
				// Directional lights currently not supported by tiled deferred
				bAnyUnsupportedByTiledDeferred = bAnyUnsupportedByTiledDeferred 
					|| (SortedLightInfo.SortKey.Fields.LightType != LightType_Point && SortedLightInfo.SortKey.Fields.LightType != LightType_Spot);
			}
		}
		
		if (GbEnableAsyncComputeTranslucencyLightingVolumeClear && GSupportsEfficientAsyncCompute)
		{
			//Gfx pipe must wait for the async compute clear of the translucency volume clear.
			RHICmdList.WaitComputeFence(TranslucencyLightingVolumeClearEndFence);
		}

		if(ViewFamily.EngineShowFlags.DirectLighting)
		{
			SCOPED_DRAW_EVENT(RHICmdList, NonShadowedLights);
			INC_DWORD_STAT_BY(STAT_NumUnshadowedLights, AttenuationLightStart);

			int32 StandardDeferredStart = 0;

			bool bRenderSimpleLightsStandardDeferred = SimpleLights.InstanceData.Num() > 0;

			if (CanUseTiledDeferred())
			{
				bool bAnyViewIsStereo = false;
				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
				{
					if (Views[ViewIndex].StereoPass != eSSP_FULL)
					{
						bAnyViewIsStereo = true;
						break;
					}
				}

				// Use tiled deferred shading on any unshadowed lights without a texture light profile
				if (ShouldUseTiledDeferred(SupportedByTiledDeferredLightEnd, SimpleLights.InstanceData.Num()) && !bAnyUnsupportedByTiledDeferred && !bAnyViewIsStereo)
				{
					// Update the range that needs to be processed by standard deferred to exclude the lights done with tiled
					StandardDeferredStart = SupportedByTiledDeferredLightEnd;
					bRenderSimpleLightsStandardDeferred = false;
					RenderTiledDeferredLighting(RHICmdList, SortedLights, SupportedByTiledDeferredLightEnd, SimpleLights);
				}
			}
			
			if (bRenderSimpleLightsStandardDeferred)
			{
				SceneContext.BeginRenderingSceneColor(RHICmdList, ESimpleRenderTargetMode::EExistingColorAndDepth, FExclusiveDepthStencil::DepthRead_StencilWrite);
				RenderSimpleLightsStandardDeferred(RHICmdList, SimpleLights);
			}

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
					RenderLight(RHICmdList, LightSceneInfo, NULL, false, false);
				}
			}

			if (GUseTranslucentLightingVolumes && GSupportsVolumeTextureRendering)
			{
				if (AttenuationLightStart)
				{
					// Inject non-shadowed, non-light function lights in to the volume.
					SCOPED_DRAW_EVENT(RHICmdList, InjectNonShadowedTranslucentLighting);
					InjectTranslucentVolumeLightingArray(RHICmdList, SortedLights, AttenuationLightStart);
				}
				
				if (SimpleLights.InstanceData.Num() > 0)
				{
					SCOPED_DRAW_EVENT(RHICmdList, InjectSimpleLightsTranslucentLighting);
					InjectSimpleTranslucentVolumeLightingArray(RHICmdList, SimpleLights);
				}
			}
		}

		EShaderPlatform ShaderPlatform = GShaderPlatformForFeatureLevel[FeatureLevel]; 

		if ( IsFeatureLevelSupported(ShaderPlatform, ERHIFeatureLevel::SM5) )
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
				for (int32 LightIndex = 0; LightIndex < SortedLights.Num(); LightIndex++)
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

			bool bDirectLighting = ViewFamily.EngineShowFlags.DirectLighting;

			TRefCountPtr<IPooledRenderTarget> ScreenShadowMaskTexture;

			// Draw shadowed and light function lights
			for (int32 LightIndex = AttenuationLightStart; LightIndex < SortedLights.Num(); LightIndex++)
			{
				const FSortedLightSceneInfo& SortedLightInfo = SortedLights[LightIndex];
				const FLightSceneInfo& LightSceneInfo = *SortedLightInfo.LightSceneInfo;
				bool bDrawShadows = SortedLightInfo.SortKey.Fields.bShadowed;
				bool bDrawLightFunction = SortedLightInfo.SortKey.Fields.bLightFunction;
				bool bDrawPreviewIndicator = ViewFamily.EngineShowFlags.PreviewShadowsIndicator && !LightSceneInfo.IsPrecomputedLightingValid() && LightSceneInfo.Proxy->HasStaticShadowing();
				bool bInjectedTranslucentVolume = false;
				bool bUsedShadowMaskTexture = false;
				FScopeCycleCounter Context(LightSceneInfo.Proxy->GetStatId());

				if ((bDrawShadows || bDrawLightFunction || bDrawPreviewIndicator) && !ScreenShadowMaskTexture.IsValid())
				{
					SceneContext.AllocateScreenShadowMask(RHICmdList, ScreenShadowMaskTexture);
				}

				FString LightNameWithLevel;
				GetLightNameForDrawEvent(LightSceneInfo.Proxy, LightNameWithLevel);
				SCOPED_DRAW_EVENTF(RHICmdList, EventLightPass, *LightNameWithLevel);

				if (bDrawShadows)
				{
					INC_DWORD_STAT(STAT_NumShadowedLights);

					for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
					{
						const FViewInfo& View = Views[ViewIndex];
						View.HeightfieldLightingViewInfo.ClearShadowing(View, RHICmdList, LightSceneInfo);
					}

					// Clear light attenuation for local lights with a quad covering their extents
					const bool bClearLightScreenExtentsOnly = SortedLightInfo.SortKey.Fields.LightType != LightType_Directional;
					// All shadows render with min blending
					bool bClearToWhite = !bClearLightScreenExtentsOnly;

					SetRenderTarget(RHICmdList, ScreenShadowMaskTexture->GetRenderTargetItem().TargetableTexture, SceneContext.GetSceneDepthSurface(), bClearToWhite ? ESimpleRenderTargetMode::EClearColorExistingDepth : ESimpleRenderTargetMode::EExistingColorAndDepth, FExclusiveDepthStencil::DepthRead_StencilWrite, true);

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

					RenderShadowProjections(RHICmdList, &LightSceneInfo, ScreenShadowMaskTexture, bInjectedTranslucentVolume);

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
					RHICmdList.CopyToResolveTarget(ScreenShadowMaskTexture->GetRenderTargetItem().TargetableTexture, ScreenShadowMaskTexture->GetRenderTargetItem().ShaderResourceTexture, FResolveParams(FResolveRect()));
				}
			
				if(bDirectLighting && !bInjectedTranslucentVolume)
				{
					SCOPED_DRAW_EVENT(RHICmdList, InjectTranslucentVolume);
					// Accumulate this light's unshadowed contribution to the translucency lighting volume
					InjectTranslucentVolumeLighting(RHICmdList, LightSceneInfo, NULL);
				}

				GRenderTargetPool.VisualizeTexture.SetCheckPoint(RHICmdList, ScreenShadowMaskTexture);

				SceneContext.BeginRenderingSceneColor(RHICmdList, ESimpleRenderTargetMode::EExistingColorAndDepth, FExclusiveDepthStencil::DepthRead_StencilWrite);

				// Render the light to the scene color buffer, conditionally using the attenuation buffer or a 1x1 white texture as input 
				if(bDirectLighting)
				{
					RenderLight(RHICmdList, &LightSceneInfo, ScreenShadowMaskTexture, false, true);
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
			RenderLight(RHICmdList, LightSceneInfo, NULL, true, false);
		}
	}
}

void FDeferredShadingSceneRenderer::RenderStationaryLightOverlap(FRHICommandListImmediate& RHICmdList)
{
	if (Scene->bIsEditorScene)
	{
		FSceneRenderTargets::Get(RHICmdList).BeginRenderingSceneColor(RHICmdList, ESimpleRenderTargetMode::EUninitializedColorExistingDepth, FExclusiveDepthStencil::DepthRead_StencilWrite);

		// Clear to discard base pass values in scene color since we didn't skip that, to have valid scene depths
		DrawClearQuad(RHICmdList, FLinearColor::Black);

		RenderLightArrayForOverlapViewmode(RHICmdList, Scene->Lights);

		//Note: making use of FScene::InvisibleLights, which contains lights that haven't been added to the scene in the same way as visible lights
		// So code called by RenderLightArrayForOverlapViewmode must be careful what it accesses
		RenderLightArrayForOverlapViewmode(RHICmdList, Scene->InvisibleLights);
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
	FShader* VertexShader,
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

	TShaderMapRef< FDeferredLightPS > PixelShader( View.ShaderMap, PermutationVector );
	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(VertexShader);
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
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
void FDeferredShadingSceneRenderer::RenderLight(FRHICommandList& RHICmdList, const FLightSceneInfo* LightSceneInfo, IPooledRenderTarget* ScreenShadowMaskTexture, bool bRenderOverlap, bool bIssueDrawEvent)
{
	SCOPE_CYCLE_COUNTER(STAT_DirectLightRenderingTime);
	INC_DWORD_STAT(STAT_NumLightsUsingStandardDeferred);
	SCOPED_CONDITIONAL_DRAW_EVENT(RHICmdList, StandardDeferredLighting, bIssueDrawEvent);

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	// Use additive blending for color
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
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
				PixelShader->SetParameters(RHICmdList, View, LightSceneInfo);
			}
			else
			{
				FDeferredLightPS::FPermutationDomain PermutationVector;
				PermutationVector.Set< FDeferredLightPS::FSourceShapeDim >( ELightSourceShape::Directional );
				PermutationVector.Set< FDeferredLightPS::FIESProfileDim >( false );
				PermutationVector.Set< FDeferredLightPS::FInverseSquaredDim >( false );
				PermutationVector.Set< FDeferredLightPS::FVisualizeCullingDim >( View.Family->EngineShowFlags.VisualizeLightCulling );
				PermutationVector.Set< FDeferredLightPS::FLightingChannelsDim >( View.bUsesLightingChannels );
				PermutationVector.Set< FDeferredLightPS::FTransmissionDim >( bTransmission );

				TShaderMapRef< FDeferredLightPS > PixelShader( View.ShaderMap, PermutationVector );
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
				PixelShader->SetParameters(RHICmdList, View, LightSceneInfo, ScreenShadowMaskTexture);
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
				*VertexShader,
				EDRF_UseTriangleOptimization);
		}
		else
		{
			// Use DBT to allow work culling on shadow lights
			GraphicsPSOInit.bDepthBounds = (GSupportsDepthBoundsTest && GAllowDepthBoundsTest != 0);

			TShaderMapRef<TDeferredLightVS<true> > VertexShader(View.ShaderMap);

			SetBoundingGeometryRasterizerAndDepthState(GraphicsPSOInit, View, LightBounds);

			if (bRenderOverlap)
			{
				TShaderMapRef<TDeferredLightOverlapPS<true> > PixelShader(View.ShaderMap);
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
				PixelShader->SetParameters(RHICmdList, View, LightSceneInfo);
			}
			else
			{
				FDeferredLightPS::FPermutationDomain PermutationVector;
				PermutationVector.Set< FDeferredLightPS::FSourceShapeDim >( LightSceneInfo->Proxy->IsRectLight() ? ELightSourceShape::Rect : ELightSourceShape::Capsule );
				PermutationVector.Set< FDeferredLightPS::FIESProfileDim >( bUseIESTexture );
				PermutationVector.Set< FDeferredLightPS::FInverseSquaredDim >( LightSceneInfo->Proxy->IsInverseSquared() );
				PermutationVector.Set< FDeferredLightPS::FVisualizeCullingDim >( View.Family->EngineShowFlags.VisualizeLightCulling );
				PermutationVector.Set< FDeferredLightPS::FLightingChannelsDim >( View.bUsesLightingChannels );
				PermutationVector.Set< FDeferredLightPS::FTransmissionDim >( bTransmission );

				TShaderMapRef< FDeferredLightPS > PixelShader( View.ShaderMap, PermutationVector );
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
				PixelShader->SetParameters(RHICmdList, View, LightSceneInfo, ScreenShadowMaskTexture);
			}

			VertexShader->SetParameters(RHICmdList, View, LightSceneInfo);

			// Use DBT to allow work culling on shadow lights
			if (GSupportsDepthBoundsTest && GAllowDepthBoundsTest != 0)
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
				SetShaderTemplLightingSimple<false, true, true>(RHICmdList, GraphicsPSOInit, View, *VertexShader, SimpleLight, SimpleLightPerViewData);
			}
			else
			{
				// light's exponent, not inverse squared
				SetShaderTemplLightingSimple<false, true, false>(RHICmdList, GraphicsPSOInit, View, *VertexShader, SimpleLight, SimpleLightPerViewData);
			}

			VertexShader->SetSimpleLightParameters(RHICmdList, View, LightBounds);

			// Apply the point or spot light with some approximately bounding geometry, 
			// So we can get speedups from depth testing and not processing pixels outside of the light's influence.
			StencilingGeometry::DrawSphere(RHICmdList);
		}
	}
}
