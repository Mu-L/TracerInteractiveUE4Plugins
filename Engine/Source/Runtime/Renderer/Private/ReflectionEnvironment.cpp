// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Reflection Environment - feature that provides HDR glossy reflections on any surfaces, leveraging precomputation to prefilter cubemaps of the scene
=============================================================================*/

#include "ReflectionEnvironment.h"
#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "UniformBuffer.h"
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
#include "BasePassRendering.h"
#include "ScenePrivate.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/PostProcessSubsurface.h"
#include "PostProcess/ScreenSpaceReflections.h"
#include "LightRendering.h"
#include "LightPropagationVolumeSettings.h"
#include "PipelineStateCache.h"
#include "DistanceFieldAmbientOcclusion.h"

DECLARE_GPU_STAT_NAMED(ReflectionEnvironment, TEXT("Reflection Environment"));
DECLARE_GPU_STAT(SkyLightDiffuse);

extern TAutoConsoleVariable<int32> CVarLPVMixing;

static TAutoConsoleVariable<int32> CVarReflectionEnvironment(
	TEXT("r.ReflectionEnvironment"),
	1,
	TEXT("Whether to render the reflection environment feature, which implements local reflections through Reflection Capture actors.\n")
	TEXT(" 0: off\n")
	TEXT(" 1: on and blend with scene (default)")
	TEXT(" 2: on and overwrite scene (only in non-shipping builds)"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

int32 GReflectionEnvironmentLightmapMixing = 1;
FAutoConsoleVariableRef CVarReflectionEnvironmentLightmapMixing(
	TEXT("r.ReflectionEnvironmentLightmapMixing"),
	GReflectionEnvironmentLightmapMixing,
	TEXT("Whether to mix indirect specular from reflection captures with indirect diffuse from lightmaps for rough surfaces."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GReflectionEnvironmentLightmapMixBasedOnRoughness = 1;
FAutoConsoleVariableRef CVarReflectionEnvironmentLightmapMixBasedOnRoughness(
	TEXT("r.ReflectionEnvironmentLightmapMixBasedOnRoughness"),
	GReflectionEnvironmentLightmapMixBasedOnRoughness,
	TEXT("Whether to reduce lightmap mixing with reflection captures for very smooth surfaces.  This is useful to make sure reflection captures match SSR / planar reflections in brightness."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GReflectionEnvironmentBeginMixingRoughness = .1f;
FAutoConsoleVariableRef CVarReflectionEnvironmentBeginMixingRoughness(
	TEXT("r.ReflectionEnvironmentBeginMixingRoughness"),
	GReflectionEnvironmentBeginMixingRoughness,
	TEXT("Min roughness value at which to begin mixing reflection captures with lightmap indirect diffuse."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GReflectionEnvironmentEndMixingRoughness = .3f;
FAutoConsoleVariableRef CVarReflectionEnvironmentEndMixingRoughness(
	TEXT("r.ReflectionEnvironmentEndMixingRoughness"),
	GReflectionEnvironmentEndMixingRoughness,
	TEXT("Min roughness value at which to end mixing reflection captures with lightmap indirect diffuse."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GReflectionEnvironmentLightmapMixLargestWeight = 10000;
FAutoConsoleVariableRef CVarReflectionEnvironmentLightmapMixLargestWeight(
	TEXT("r.ReflectionEnvironmentLightmapMixLargestWeight"),
	GReflectionEnvironmentLightmapMixLargestWeight,
	TEXT("When set to 1 can be used to clamp lightmap mixing such that only darkening from lightmaps are applied to reflection captures."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarDoTiledReflections(
	TEXT("r.DoTiledReflections"),
	1,
	TEXT("Compute Reflection Environment with Tiled compute shader..\n")
	TEXT(" 0: off\n")
	TEXT(" 1: on (default)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarSkySpecularOcclusionStrength(
	TEXT("r.SkySpecularOcclusionStrength"),
	1,
	TEXT("Strength of skylight specular occlusion from DFAO (default is 1.0)"),
	ECVF_RenderThreadSafe);

// to avoid having direct access from many places
static int GetReflectionEnvironmentCVar()
{
	int32 RetVal = CVarReflectionEnvironment.GetValueOnAnyThread();

#if (UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// Disabling the debug part of this CVar when in shipping
	if (RetVal == 2)
	{
		RetVal = 1;
	}
#endif

	return RetVal;
}

FVector GetReflectionEnvironmentRoughnessMixingScaleBiasAndLargestWeight()
{
	float RoughnessMixingRange = 1.0f / FMath::Max(GReflectionEnvironmentEndMixingRoughness - GReflectionEnvironmentBeginMixingRoughness, .001f);

	if (GReflectionEnvironmentLightmapMixing == 0)
	{
		return FVector(0, 0, GReflectionEnvironmentLightmapMixLargestWeight);
	}

	if (GReflectionEnvironmentEndMixingRoughness == 0.0f && GReflectionEnvironmentBeginMixingRoughness == 0.0f)
	{
		// Make sure a Roughness of 0 results in full mixing when disabling roughness-based mixing
		return FVector(0, 1, GReflectionEnvironmentLightmapMixLargestWeight);
	}

	if (!GReflectionEnvironmentLightmapMixBasedOnRoughness)
	{
		return FVector(0, 1, GReflectionEnvironmentLightmapMixLargestWeight);
	}

	return FVector(RoughnessMixingRange, -GReflectionEnvironmentBeginMixingRoughness * RoughnessMixingRange, GReflectionEnvironmentLightmapMixLargestWeight);
}

bool IsReflectionEnvironmentAvailable(ERHIFeatureLevel::Type InFeatureLevel)
{
	return (InFeatureLevel >= ERHIFeatureLevel::SM4) && (GetReflectionEnvironmentCVar() != 0);
}

bool IsReflectionCaptureAvailable()
{
	static IConsoleVariable* AllowStaticLightingVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AllowStaticLighting"));
	return (!AllowStaticLightingVar || AllowStaticLightingVar->GetInt() != 0);
}

IMPLEMENT_UNIFORM_BUFFER_STRUCT(FReflectionUniformParameters, TEXT("ReflectionStruct"));

void SetupReflectionUniformParameters(const FViewInfo& View, FReflectionUniformParameters& OutParameters)
{
	FTexture* SkyLightTextureResource = GBlackTextureCube;
	FTexture* SkyLightBlendDestinationTextureResource = GBlackTextureCube;
	float ApplySkyLightMask = 0;
	float BlendFraction = 0;
	bool bSkyLightIsDynamic = false;
	float SkyAverageBrightness = 1.0f;

	const bool bApplySkyLight = View.Family->EngineShowFlags.SkyLighting;
	const FScene* Scene = (const FScene*)View.Family->Scene;

	if (Scene
		&& Scene->SkyLight 
		&& Scene->SkyLight->ProcessedTexture
		&& bApplySkyLight)
	{
		const FSkyLightSceneProxy& SkyLight = *Scene->SkyLight;
		SkyLightTextureResource = SkyLight.ProcessedTexture;
		BlendFraction = SkyLight.BlendFraction;

		if (SkyLight.BlendFraction > 0.0f && SkyLight.BlendDestinationProcessedTexture)
		{
			if (SkyLight.BlendFraction < 1.0f)
			{
				SkyLightBlendDestinationTextureResource = SkyLight.BlendDestinationProcessedTexture;
			}
			else
			{
				SkyLightTextureResource = SkyLight.BlendDestinationProcessedTexture;
				BlendFraction = 0;
			}
		}
		
		ApplySkyLightMask = 1;
		bSkyLightIsDynamic = !SkyLight.bHasStaticLighting && !SkyLight.bWantsStaticShadowing;
		SkyAverageBrightness = SkyLight.AverageBrightness;
	}

	const int32 CubemapWidth = SkyLightTextureResource->GetSizeX();
	const float SkyMipCount = FMath::Log2(CubemapWidth) + 1.0f;

	OutParameters.SkyLightCubemap = SkyLightTextureResource->TextureRHI;
	OutParameters.SkyLightCubemapSampler = SkyLightTextureResource->SamplerStateRHI;
	OutParameters.SkyLightBlendDestinationCubemap = SkyLightBlendDestinationTextureResource->TextureRHI;
	OutParameters.SkyLightBlendDestinationCubemapSampler = SkyLightBlendDestinationTextureResource->SamplerStateRHI;
	OutParameters.SkyLightParameters = FVector4(SkyMipCount - 1.0f, ApplySkyLightMask, bSkyLightIsDynamic ? 1.0f : 0.0f, BlendFraction);
	OutParameters.SkyLightCubemapBrightness = SkyAverageBrightness;

	// Note: GBlackCubeArrayTexture has an alpha of 0, which is needed to represent invalid data so the sky cubemap can still be applied
	FTextureRHIParamRef CubeArrayTexture = View.FeatureLevel >= ERHIFeatureLevel::SM5 ? GBlackCubeArrayTexture->TextureRHI : GBlackTextureCube->TextureRHI;

	if (View.Family->EngineShowFlags.ReflectionEnvironment 
		&& View.FeatureLevel >= ERHIFeatureLevel::SM5
		&& Scene
		&& Scene->ReflectionSceneData.CubemapArray.IsValid())
	{
		CubeArrayTexture = Scene->ReflectionSceneData.CubemapArray.GetRenderTarget().ShaderResourceTexture;
	}

	OutParameters.ReflectionCubemap = CubeArrayTexture;
	OutParameters.ReflectionCubemapSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
}

void FReflectionEnvironmentCubemapArray::InitDynamicRHI()
{
	if (GetFeatureLevel() >= ERHIFeatureLevel::SM5)
	{
		const int32 NumReflectionCaptureMips = FMath::CeilLogTwo(CubemapSize) + 1;

		ReleaseCubeArray();

		FPooledRenderTargetDesc Desc(
			FPooledRenderTargetDesc::CreateCubemapDesc(
				CubemapSize,
				// Alpha stores sky mask
				PF_FloatRGBA, 
				FClearValueBinding::None,
				TexCreate_None,
				TexCreate_None,
				false, 
				// Cubemap array of 1 produces a regular cubemap, so guarantee it will be allocated as an array
				FMath::Max<uint32>(MaxCubemaps, 2),
				NumReflectionCaptureMips
				)
			);

		Desc.AutoWritable = false;
	
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

		// Allocate TextureCubeArray for the scene's reflection captures
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, ReflectionEnvs, TEXT("ReflectionEnvs"));
	}
}

void FReflectionEnvironmentCubemapArray::ReleaseCubeArray()
{
	// it's unlikely we can reuse the TextureCubeArray so when we release it we want to really remove it
	GRenderTargetPool.FreeUnusedResource(ReflectionEnvs);
}

void FReflectionEnvironmentCubemapArray::ReleaseDynamicRHI()
{
	ReleaseCubeArray();
}

void FReflectionEnvironmentSceneData::ResizeCubemapArrayGPU(uint32 InMaxCubemaps, int32 InCubemapSize)
{
	check(IsInRenderingThread());

	// If the cubemap array isn't setup yet then no copying/reallocation is necessary. Just go through the old path
	if (!CubemapArray.IsInitialized())
	{
		CubemapArraySlotsUsed.Init(false, InMaxCubemaps);
		CubemapArray.UpdateMaxCubemaps(InMaxCubemaps, InCubemapSize);
		return;
	}

	// Generate a remapping table for the elements
	TArray<int32> IndexRemapping;
	int32 Count = 0;
	for (int i = 0; i < CubemapArray.GetMaxCubemaps(); i++)
	{
		bool bUsed = i < CubemapArraySlotsUsed.Num() ? CubemapArraySlotsUsed[i] : false;
		if (bUsed)
		{
			IndexRemapping.Add(Count);
			Count++;
		}
		else
		{
			IndexRemapping.Add(-1);
		}
	}

	// Reset the CubemapArraySlotsUsed array (we'll recompute it below)
	CubemapArraySlotsUsed.Init(false, InMaxCubemaps);

	// Spin through the AllocatedReflectionCaptureState map and remap the indices based on the LUT
	TArray<const UReflectionCaptureComponent*> Components;
	AllocatedReflectionCaptureState.GetKeys(Components);
	int32 UsedCubemapCount = 0;
	for (int32 i=0; i<Components.Num(); i++ )
	{
		FCaptureComponentSceneState* ComponentStatePtr = AllocatedReflectionCaptureState.Find(Components[i]);
		check(ComponentStatePtr->CaptureIndex < IndexRemapping.Num());
		int32 NewIndex = IndexRemapping[ComponentStatePtr->CaptureIndex];
		CubemapArraySlotsUsed[NewIndex] = true; 
		ComponentStatePtr->CaptureIndex = NewIndex;
		check(ComponentStatePtr->CaptureIndex > -1);
		UsedCubemapCount = FMath::Max(UsedCubemapCount, ComponentStatePtr->CaptureIndex + 1);
	}

	// Clear elements in the remapping array which are outside the range of the used components (these were allocated but not used)
	for (int i = 0; i < IndexRemapping.Num(); i++)
	{
		if (IndexRemapping[i] >= UsedCubemapCount)
		{
			IndexRemapping[i] = -1;
		}
	}

	CubemapArray.ResizeCubemapArrayGPU(InMaxCubemaps, InCubemapSize, IndexRemapping);
}

void FReflectionEnvironmentCubemapArray::ResizeCubemapArrayGPU(uint32 InMaxCubemaps, int32 InCubemapSize, const TArray<int32>& IndexRemapping)
{
	check(IsInRenderingThread());
	check(GetFeatureLevel() >= ERHIFeatureLevel::SM5);
	check(IsInitialized());
	check(InCubemapSize == CubemapSize);

	// Take a reference to the old cubemap array and then release it to prevent it getting destroyed during InitDynamicRHI
	TRefCountPtr<IPooledRenderTarget> OldReflectionEnvs = ReflectionEnvs;
	ReflectionEnvs = nullptr;
	int OldMaxCubemaps = MaxCubemaps;
	MaxCubemaps = InMaxCubemaps;

	InitDynamicRHI();

	FTextureRHIRef TexRef = OldReflectionEnvs->GetRenderTargetItem().TargetableTexture;
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	const int32 NumMips = FMath::CeilLogTwo(InCubemapSize) + 1;

	{
		SCOPED_DRAW_EVENT(RHICmdList, ReflectionEnvironment_ResizeCubemapArray);
		SCOPED_GPU_STAT(RHICmdList, ReflectionEnvironment)

		// Copy the cubemaps, remapping the elements as necessary
		FResolveParams ResolveParams;
		ResolveParams.Rect = FResolveRect();
		for (int32 SourceCubemapIndex = 0; SourceCubemapIndex < OldMaxCubemaps; SourceCubemapIndex++)
		{
			int32 DestCubemapIndex = IndexRemapping[SourceCubemapIndex];
			if (DestCubemapIndex != -1)
			{
				ResolveParams.SourceArrayIndex = SourceCubemapIndex;
				ResolveParams.DestArrayIndex = DestCubemapIndex;

				check(SourceCubemapIndex < OldMaxCubemaps);
				check(DestCubemapIndex < (int32)MaxCubemaps);

				for (int Face = 0; Face < 6; Face++)
				{
					ResolveParams.CubeFace = (ECubeFace)Face;
					for (int Mip = 0; Mip < NumMips; Mip++)
					{
						ResolveParams.MipIndex = Mip;
						//@TODO: We should use an explicit copy method for this rather than CopyToResolveTarget, but that doesn't exist right now. 
						// For now, we'll just do this on RHIs where we know CopyToResolveTarget does the right thing. In future we should look to 
						// add a a new RHI method
						check(GRHISupportsResolveCubemapFaces);
						RHICmdList.CopyToResolveTarget(OldReflectionEnvs->GetRenderTargetItem().ShaderResourceTexture, ReflectionEnvs->GetRenderTargetItem().ShaderResourceTexture, ResolveParams);
					}
				}
			}
		}
	}
	GRenderTargetPool.FreeUnusedResource(OldReflectionEnvs);
}

void FReflectionEnvironmentCubemapArray::UpdateMaxCubemaps(uint32 InMaxCubemaps, int32 InCubemapSize)
{
	MaxCubemaps = InMaxCubemaps;
	CubemapSize = InCubemapSize;

	// Reallocate the cubemap array
	if (IsInitialized())
	{
		UpdateRHI();
	}
	else
	{
		InitResource();
	}
}

class FSkyLightParameters
{
public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		ApplyBentNormalAO.Bind(ParameterMap, TEXT("ApplyBentNormalAO"));
		InvSkySpecularOcclusionStrength.Bind(ParameterMap, TEXT("InvSkySpecularOcclusionStrength"));
		OcclusionTintAndMinOcclusion.Bind(ParameterMap, TEXT("OcclusionTintAndMinOcclusion"));
		ContrastAndNormalizeMulAdd.Bind(ParameterMap, TEXT("ContrastAndNormalizeMulAdd"));
		OcclusionExponent.Bind(ParameterMap, TEXT("OcclusionExponent"));
		OcclusionCombineMode.Bind(ParameterMap, TEXT("OcclusionCombineMode"));
	}

	template<typename ShaderRHIParamRef, typename TRHICmdList>
	void SetParameters(TRHICmdList& RHICmdList, const ShaderRHIParamRef& ShaderRHI, bool bApplyBentNormalAO, float SkySpecularOcclusionStrength, const FSkyLightSceneProxy* SkyLight)
	{
		float SkyLightContrast = 0.01f;
		float SkyLightOcclusionExponent = 1.0f;
		FVector4 SkyLightOcclusionTintAndMinOcclusion(0.0f, 0.0f, 0.0f, 0.0f);
		EOcclusionCombineMode SkyLightOcclusionCombineMode = EOcclusionCombineMode::OCM_MAX;
		if (SkyLight)
		{
			FDistanceFieldAOParameters Parameters(SkyLight->OcclusionMaxDistance, SkyLight->Contrast);
			SkyLightContrast = Parameters.Contrast;
			SkyLightOcclusionExponent = SkyLight->OcclusionExponent;
			SkyLightOcclusionTintAndMinOcclusion = FVector4(SkyLight->OcclusionTint);
			SkyLightOcclusionTintAndMinOcclusion.W = SkyLight->MinOcclusion;
			SkyLightOcclusionCombineMode = SkyLight->OcclusionCombineMode;
		}

		// Scale and bias to remap the contrast curve to [0,1]
		const float Min = 1 / (1 + FMath::Exp(-SkyLightContrast * (0 * 10 - 5)));
		const float Max = 1 / (1 + FMath::Exp(-SkyLightContrast * (1 * 10 - 5)));
		const float Mul = 1.0f / (Max - Min);
		const float Add = -Min / (Max - Min);

		SetShaderValue(RHICmdList, ShaderRHI, ContrastAndNormalizeMulAdd, FVector(SkyLightContrast, Mul, Add));

		SetShaderValue(RHICmdList, ShaderRHI, OcclusionExponent, SkyLightOcclusionExponent);
		SetShaderValue(RHICmdList, ShaderRHI, OcclusionTintAndMinOcclusion, SkyLightOcclusionTintAndMinOcclusion);
		SetShaderValue(RHICmdList, ShaderRHI, OcclusionCombineMode, SkyLightOcclusionCombineMode == OCM_Minimum ? 0.0f : 1.0f);
		SetShaderValue(RHICmdList, ShaderRHI, ApplyBentNormalAO, bApplyBentNormalAO ? 1.0f : 0.0f);
		SetShaderValue(RHICmdList, ShaderRHI, InvSkySpecularOcclusionStrength, 1.0f / FMath::Max(SkySpecularOcclusionStrength, .1f));
	}

	friend FArchive& operator<<(FArchive& Ar, FSkyLightParameters& P)
	{
		Ar << P.ApplyBentNormalAO;
		Ar << P.InvSkySpecularOcclusionStrength;
		Ar << P.OcclusionTintAndMinOcclusion;
		Ar << P.ContrastAndNormalizeMulAdd;
		Ar << P.OcclusionExponent;
		Ar << P.OcclusionCombineMode;

		return Ar;
	}

private:
	FShaderParameter ApplyBentNormalAO;
	FShaderParameter InvSkySpecularOcclusionStrength;
	FShaderParameter OcclusionTintAndMinOcclusion;
	FShaderParameter ContrastAndNormalizeMulAdd;
	FShaderParameter OcclusionExponent;
	FShaderParameter OcclusionCombineMode;
};

struct FReflectionCaptureSortData
{
	uint32 Guid;
	int32 CaptureIndex;
	FVector4 PositionAndRadius;
	FVector4 CaptureProperties;
	FMatrix BoxTransform;
	FVector4 BoxScales;
	FVector4 CaptureOffsetAndAverageBrightness;

	bool operator < (const FReflectionCaptureSortData& Other) const
	{
		if( PositionAndRadius.W != Other.PositionAndRadius.W )
		{
			return PositionAndRadius.W < Other.PositionAndRadius.W;
		}
		else
		{
			return Guid < Other.Guid;
		}
	}
};

IMPLEMENT_UNIFORM_BUFFER_STRUCT(FReflectionCaptureShaderData,TEXT("ReflectionCapture"));

/** Compute shader that does tiled deferred culling of reflection captures, then sorts and composites them. */
class FReflectionEnvironmentSkyLightingPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FReflectionEnvironmentSkyLightingPS, Global)
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("MAX_CAPTURES"), GMaxNumReflectionCaptures);
		OutEnvironment.CompilerFlags.Add(CFLAG_StandardOptimization);
		FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
	}

	FReflectionEnvironmentSkyLightingPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		SceneTextureParameters.Bind(Initializer);
		ReflectionCubemap.Bind(Initializer.ParameterMap,TEXT("ReflectionCubemap"));
		ReflectionCubemapSampler.Bind(Initializer.ParameterMap,TEXT("ReflectionCubemapSampler"));
		ScreenSpaceReflectionsTexture.Bind(Initializer.ParameterMap, TEXT("ScreenSpaceReflectionsTexture"));
		ScreenSpaceReflectionsSampler.Bind(Initializer.ParameterMap, TEXT("ScreenSpaceReflectionsSampler"));
		PreIntegratedGF.Bind(Initializer.ParameterMap, TEXT("PreIntegratedGF"));
		PreIntegratedGFSampler.Bind(Initializer.ParameterMap, TEXT("PreIntegratedGFSampler"));
		DFAOUpsampleParameters.Bind(Initializer.ParameterMap);
		SkyLightParameters.Bind(Initializer.ParameterMap);
	}

	FReflectionEnvironmentSkyLightingPS()
	{
	}

	template<typename TRHICommandList>
	void SetParameters(
		TRHICommandList& RHICmdList,
		const FViewInfo& View,
		FTextureRHIParamRef SSRTexture,
		const TRefCountPtr<IPooledRenderTarget>& DynamicBentNormalAO
	)
	{
		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);
		SceneTextureParameters.Set(RHICmdList, ShaderRHI, View.FeatureLevel, ESceneTextureSetupMode::All);

		FScene* Scene = (FScene*)View.Family->Scene;

		FTextureRHIParamRef CubemapArray;
		if (Scene->ReflectionSceneData.CubemapArray.IsValid() &&
			Scene->ReflectionSceneData.CubemapArray.GetRenderTarget().IsValid())
		{
			CubemapArray = Scene->ReflectionSceneData.CubemapArray.GetRenderTarget().ShaderResourceTexture;
		}
		else
		{
			CubemapArray = GBlackCubeArrayTexture->TextureRHI;
		}

		SetTextureParameter(
			RHICmdList,
			ShaderRHI,
			ReflectionCubemap,
			ReflectionCubemapSampler,
			TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),
			CubemapArray);

		SetTextureParameter(RHICmdList, ShaderRHI, ScreenSpaceReflectionsTexture, ScreenSpaceReflectionsSampler, TStaticSamplerState<SF_Point>::GetRHI(), SSRTexture);

		SetUniformBufferParameter(RHICmdList, ShaderRHI, GetUniformBufferParameter<FReflectionCaptureShaderData>(), View.ReflectionCaptureUniformBuffer);

		SetTextureParameter(RHICmdList, ShaderRHI, PreIntegratedGF, PreIntegratedGFSampler, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), GSystemTextures.PreintegratedGF->GetRenderTargetItem().ShaderResourceTexture);
	
		FReflectionUniformParameters ReflectionUniformParameters;
		SetupReflectionUniformParameters(View, ReflectionUniformParameters);
		SetUniformBufferParameterImmediate(RHICmdList, ShaderRHI, GetUniformBufferParameter<FReflectionUniformParameters>(), ReflectionUniformParameters);

		bool const bApplyBentNormalAO = DynamicBentNormalAO ? true : false;
		SkyLightParameters.SetParameters(RHICmdList, ShaderRHI, bApplyBentNormalAO, CVarSkySpecularOcclusionStrength.GetValueOnRenderThread(), Scene->SkyLight);

		SetUniformBufferParameter(RHICmdList, ShaderRHI, GetUniformBufferParameter<FForwardLightData>(), View.ForwardLightingResources->ForwardLightDataUniformBuffer);
		DFAOUpsampleParameters.Set(RHICmdList, ShaderRHI, View, DynamicBentNormalAO);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << SceneTextureParameters;
		Ar << ReflectionCubemap;
		Ar << ReflectionCubemapSampler;
		Ar << ScreenSpaceReflectionsTexture;
		Ar << ScreenSpaceReflectionsSampler;
		Ar << PreIntegratedGF;
		Ar << PreIntegratedGFSampler;
		Ar << DFAOUpsampleParameters;
		Ar << SkyLightParameters;
		return bShaderHasOutdatedParameters;
	}

private:

	FSceneTextureShaderParameters SceneTextureParameters;
	FShaderResourceParameter ReflectionCubemap;
	FShaderResourceParameter ReflectionCubemapSampler;
	FShaderResourceParameter ScreenSpaceReflectionsTexture;
	FShaderResourceParameter ScreenSpaceReflectionsSampler;
	FShaderResourceParameter PreIntegratedGF;
	FShaderResourceParameter PreIntegratedGFSampler;
	FDFAOUpsampleParameters DFAOUpsampleParameters;
	FSkyLightParameters SkyLightParameters;
};

template< uint32 bUseLightmaps, uint32 bBoxCapturesOnly, uint32 bSphereCapturesOnly, uint32 bSupportDFAOIndirectOcclusion, uint32 bSpecularBounce, uint32 bEnableSkyLight, uint32 bEnableDynamicSkyLight, uint32 bApplySkyShadowing>
class TReflectionEnvironmentSkyLightingPS : public FReflectionEnvironmentSkyLightingPS
{
	DECLARE_SHADER_TYPE(TReflectionEnvironmentSkyLightingPS, Global);

	/** Default constructor. */
	TReflectionEnvironmentSkyLightingPS() {}
public:
	TReflectionEnvironmentSkyLightingPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FReflectionEnvironmentSkyLightingPS(Initializer)
	{}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FReflectionEnvironmentSkyLightingPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("USE_LIGHTMAPS"), bUseLightmaps);
		OutEnvironment.SetDefine(TEXT("REFLECTION_COMPOSITE_HAS_BOX_CAPTURES"), bBoxCapturesOnly);
		OutEnvironment.SetDefine(TEXT("REFLECTION_COMPOSITE_HAS_SPHERE_CAPTURES"), bSphereCapturesOnly);
		OutEnvironment.SetDefine(TEXT("SUPPORT_DFAO_INDIRECT_OCCLUSION"), bSupportDFAOIndirectOcclusion);
		OutEnvironment.SetDefine(TEXT("SPECULAR_BOUNCE"), bSpecularBounce);
		OutEnvironment.SetDefine(TEXT("ENABLE_SKY_LIGHT"), bEnableSkyLight);
		OutEnvironment.SetDefine(TEXT("ENABLE_DYNAMIC_SKY_LIGHT"), bEnableDynamicSkyLight);
		OutEnvironment.SetDefine(TEXT("APPLY_SKY_SHADOWING"), bApplySkyShadowing);
	}

	static const TCHAR* GetDebugName()
	{
		static const FString Name = FString::Printf(TEXT("TReflectionEnvironmentSkyLightingPS(%s,%s,%s,%s,%s,%s,%s,%s)"),
			bUseLightmaps == 1 ? TEXT("true") : TEXT("false"),
			bBoxCapturesOnly == 1 ? TEXT("true") : TEXT("false"),
			bSphereCapturesOnly == 1 ? TEXT("true") : TEXT("false"),
			bSupportDFAOIndirectOcclusion == 1 ? TEXT("true") : TEXT("false"),
			bSpecularBounce == 1 ? TEXT("true") : TEXT("false"),
			bEnableSkyLight == 1 ? TEXT("true") : TEXT("false"),
			bEnableDynamicSkyLight == 1 ? TEXT("true") : TEXT("false"),
			bApplySkyShadowing == 1 ? TEXT("true") : TEXT("false")
		);

		return *Name;
	}
};

// The C preprocessor doesn't like commas in macro parameters (typically template paramater list)
#define ARG_WITH_COMMAS(...) __VA_ARGS__

// Templatized version of IMPLEMENT_SHADER_TYPE
// This allows us to avoid 32 IMPLEMENT_SHADER_TYPE macros, one per shader variation
IMPLEMENT_SHADER_TYPE_WITH_DEBUG_NAME(
	ARG_WITH_COMMAS(template<uint32 A, uint32 B, uint32 C, uint32 D, uint32 E, uint32 F, uint32 G, uint32 H>),
	ARG_WITH_COMMAS(TReflectionEnvironmentSkyLightingPS<A, B, C, D, E, F, G, H>),
	TEXT("/Engine/Private/ReflectionEnvironmentPixelShader.usf"),
	TEXT("ReflectionEnvironmentSkyLighting"),
	SF_Pixel)

// This function selects a shader variation dynamically at runtime based on its parameters
// Intuitively it can be seen as translating SelectShader(1, 0, 1, 1, 0) into ShaderInstance<1, 0, 1, 1, 0>()
// i.e. translating function parameters into template parameters, which allows us to avoid 32 if-elses / switch-cases
// When this function is called, it will also be the place where the 32 shader variations get actually instantiated
template <class ShaderClass, template<uint32...> class ShaderTemplateClass, uint32... ExtractedTemplateParameters>
ShaderClass* SelectShaderVariation(TShaderMap<FGlobalShaderType>* ShaderMap)
{
	return *TShaderMapRef< ShaderTemplateClass<ExtractedTemplateParameters...> >(ShaderMap);
}

template <class ShaderClass, template<uint32...> class ShaderTemplateClass, uint32... ExtractedTemplateParameters, typename... Args>
ShaderClass* SelectShaderVariation(TShaderMap<FGlobalShaderType>* ShaderMap, bool first, Args... args)
{
	return first ?
		SelectShaderVariation<ShaderClass, ShaderTemplateClass, ExtractedTemplateParameters..., 1>(ShaderMap, args...) :
		SelectShaderVariation<ShaderClass, ShaderTemplateClass, ExtractedTemplateParameters..., 0>(ShaderMap, args...);
}

bool FDeferredShadingSceneRenderer::ShouldDoReflectionEnvironment() const
{
	const ERHIFeatureLevel::Type SceneFeatureLevel = Scene->GetFeatureLevel();

	return IsReflectionEnvironmentAvailable(SceneFeatureLevel)
		&& Scene->ReflectionSceneData.RegisteredReflectionCaptures.Num()
		&& ViewFamily.EngineShowFlags.ReflectionEnvironment;
}

void GatherAndSortReflectionCaptures(const FViewInfo& View, const FScene* Scene, TArray<FReflectionCaptureSortData>& OutSortData, int32& OutNumBoxCaptures, int32& OutNumSphereCaptures, float& OutFurthestReflectionCaptureDistance)
{	
	OutSortData.Reset(Scene->ReflectionSceneData.RegisteredReflectionCaptures.Num());
	OutNumBoxCaptures = 0;
	OutNumSphereCaptures = 0;
	OutFurthestReflectionCaptureDistance = 1000;

	const int32 MaxCubemaps = Scene->ReflectionSceneData.CubemapArray.GetMaxCubemaps();

	if (View.Family->EngineShowFlags.ReflectionEnvironment)
	{
		// Pack only visible reflection captures into the uniform buffer, each with an index to its cubemap array entry
		//@todo - view frustum culling
		for (int32 ReflectionProxyIndex = 0; ReflectionProxyIndex < Scene->ReflectionSceneData.RegisteredReflectionCaptures.Num() && OutSortData.Num() < GMaxNumReflectionCaptures; ReflectionProxyIndex++)
		{
			FReflectionCaptureProxy* CurrentCapture = Scene->ReflectionSceneData.RegisteredReflectionCaptures[ReflectionProxyIndex];

			FReflectionCaptureSortData NewSortEntry;

			NewSortEntry.CaptureIndex = -1;
			NewSortEntry.CaptureOffsetAndAverageBrightness = FVector4(CurrentCapture->CaptureOffset, 1.0f);
			if (Scene->GetFeatureLevel() >= ERHIFeatureLevel::SM5)
			{
				const FCaptureComponentSceneState* ComponentStatePtr = Scene->ReflectionSceneData.AllocatedReflectionCaptureState.Find(CurrentCapture->Component);
				NewSortEntry.CaptureIndex = ComponentStatePtr ? ComponentStatePtr->CaptureIndex : 0;
				check(NewSortEntry.CaptureIndex < MaxCubemaps || NewSortEntry.CaptureIndex == 0);
				NewSortEntry.CaptureOffsetAndAverageBrightness.W = ComponentStatePtr ? ComponentStatePtr->AverageBrightness : 1.0f;
			}

			NewSortEntry.Guid = CurrentCapture->Guid;
			NewSortEntry.PositionAndRadius = FVector4(CurrentCapture->Position, CurrentCapture->InfluenceRadius);
			float ShapeTypeValue = (float)CurrentCapture->Shape;
			NewSortEntry.CaptureProperties = FVector4(CurrentCapture->Brightness, NewSortEntry.CaptureIndex, ShapeTypeValue, 0);

			if (CurrentCapture->Shape == EReflectionCaptureShape::Plane)
			{
				//planes count as boxes in the compute shader.
				++OutNumBoxCaptures;
				NewSortEntry.BoxTransform = FMatrix(
					FPlane(CurrentCapture->ReflectionPlane),
					FPlane(CurrentCapture->ReflectionXAxisAndYScale),
					FPlane(0, 0, 0, 0),
					FPlane(0, 0, 0, 0));

				NewSortEntry.BoxScales = FVector4(0);
			}
			else if (CurrentCapture->Shape == EReflectionCaptureShape::Sphere)
			{
				++OutNumSphereCaptures;
			}
			else
			{
				++OutNumBoxCaptures;
				NewSortEntry.BoxTransform = CurrentCapture->BoxTransform;
				NewSortEntry.BoxScales = FVector4(CurrentCapture->BoxScales, CurrentCapture->BoxTransitionDistance);
			}

			const FSphere BoundingSphere(CurrentCapture->Position, CurrentCapture->InfluenceRadius);
			const float Distance = View.ViewMatrices.GetViewMatrix().TransformPosition(BoundingSphere.Center).Z + BoundingSphere.W;
			OutFurthestReflectionCaptureDistance = FMath::Max(OutFurthestReflectionCaptureDistance, Distance);

			OutSortData.Add(NewSortEntry);
		}
	}

	OutSortData.Sort();	
}

void FDeferredShadingSceneRenderer::SetupReflectionCaptureBuffers(FViewInfo& View, FRHICommandListImmediate& RHICmdList)
{
	TArray<FReflectionCaptureSortData> SortData;
	GatherAndSortReflectionCaptures(View, Scene, SortData, View.NumBoxReflectionCaptures, View.NumSphereReflectionCaptures, View.FurthestReflectionCaptureDistance);
		
	if (View.GetFeatureLevel() >= ERHIFeatureLevel::SM5)
	{
		FReflectionCaptureShaderData SamplePositionsBuffer;

		for (int32 CaptureIndex = 0; CaptureIndex < SortData.Num(); CaptureIndex++)
		{
			SamplePositionsBuffer.PositionAndRadius[CaptureIndex] = SortData[CaptureIndex].PositionAndRadius;
			SamplePositionsBuffer.CaptureProperties[CaptureIndex] = SortData[CaptureIndex].CaptureProperties;
			SamplePositionsBuffer.CaptureOffsetAndAverageBrightness[CaptureIndex] = SortData[CaptureIndex].CaptureOffsetAndAverageBrightness;
			SamplePositionsBuffer.BoxTransform[CaptureIndex] = SortData[CaptureIndex].BoxTransform;
			SamplePositionsBuffer.BoxScales[CaptureIndex] = SortData[CaptureIndex].BoxScales;
		}

		View.ReflectionCaptureUniformBuffer = TUniformBufferRef<FReflectionCaptureShaderData>::CreateUniformBufferImmediate(SamplePositionsBuffer, UniformBuffer_SingleFrame);
	}
}

void FDeferredShadingSceneRenderer::RenderDeferredReflectionsAndSkyLighting(FRHICommandListImmediate& RHICmdList, TRefCountPtr<IPooledRenderTarget>& DynamicBentNormalAO, TRefCountPtr<IPooledRenderTarget>& VelocityRT)
{
	if (ViewFamily.EngineShowFlags.VisualizeLightCulling || !ViewFamily.EngineShowFlags.Lighting)
	{
		return;
	}

	// If we're currently capturing a reflection capture, output SpecularColor * IndirectIrradiance for metals so they are not black in reflections,
	// Since we don't have multiple bounce specular reflections
	bool bReflectionCapture = false;
	for (int32 ViewIndex = 0, Num = Views.Num(); ViewIndex < Num; ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		bReflectionCapture = bReflectionCapture || View.bIsReflectionCapture;
	}

	const bool bSkyLight = Scene->SkyLight
		&& Scene->SkyLight->ProcessedTexture
		&& !Scene->SkyLight->bHasStaticLighting;

	bool bDynamicSkyLight = ShouldRenderDeferredDynamicSkyLight(Scene, ViewFamily);
	bool bApplySkyShadowing = false;
	if (bDynamicSkyLight)
	{
		SCOPED_DRAW_EVENT(RHICmdList, SkyLightDiffuse);
		SCOPED_GPU_STAT(RHICmdList, SkyLightDiffuse);

		FDistanceFieldAOParameters Parameters(Scene->SkyLight->OcclusionMaxDistance, Scene->SkyLight->Contrast);

		extern int32 GDistanceFieldAOApplyToStaticIndirect;
		if (Scene->SkyLight->bCastShadows
			&& !GDistanceFieldAOApplyToStaticIndirect
			&& ShouldRenderDistanceFieldAO()
			&& ViewFamily.EngineShowFlags.AmbientOcclusion)
		{
			bApplySkyShadowing = RenderDistanceFieldLighting(RHICmdList, Parameters, VelocityRT, DynamicBentNormalAO, false, false);
		}
	}

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	static const auto AllowStaticLightingVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));
	const bool bUseLightmaps = (AllowStaticLightingVar->GetValueOnRenderThread() == 1);

	const bool bReflectionEnv = ShouldDoReflectionEnvironment();

	for (int32 ViewIndex = 0, Num = Views.Num(); ViewIndex < Num; ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];

		const uint32 bSSR = ShouldRenderScreenSpaceReflections(Views[ViewIndex]);

		TRefCountPtr<IPooledRenderTarget> SSROutput = GSystemTextures.BlackDummy;
		if (bSSR)
		{
			RenderScreenSpaceReflections(RHICmdList, View, SSROutput, VelocityRT);
		}

		const bool bPlanarReflections = RenderDeferredPlanarReflections(RHICmdList, View, false, SSROutput);

		bool bRequiresApply = bSkyLight || bDynamicSkyLight || bReflectionEnv || bSSR || bPlanarReflections;

		if (bRequiresApply)
		{
			SCOPED_GPU_STAT(RHICmdList, ReflectionEnvironment);
			SCOPED_DRAW_EVENTF(RHICmdList, ReflectionEnvironment, TEXT("ReflectionEnvironmentAndSky"));

			// Render the reflection environment with tiled deferred culling
			bool bHasBoxCaptures = (View.NumBoxReflectionCaptures > 0);
			bool bHasSphereCaptures = (View.NumSphereReflectionCaptures > 0);

			TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);

			FReflectionEnvironmentSkyLightingPS* PixelShader =
				SelectShaderVariation<FReflectionEnvironmentSkyLightingPS, TReflectionEnvironmentSkyLightingPS>
				(View.ShaderMap, bUseLightmaps, bHasBoxCaptures, bHasSphereCaptures, DynamicBentNormalAO != NULL, bReflectionCapture, bSkyLight, bDynamicSkyLight, bApplySkyShadowing);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;

			SceneContext.BeginRenderingSceneColor(
				RHICmdList,
				bReflectionCapture ? ESimpleRenderTargetMode::EUninitializedColorExistingDepth : ESimpleRenderTargetMode::EExistingColorAndDepth,
				FExclusiveDepthStencil::DepthRead_StencilWrite);
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			extern int32 GAOOverwriteSceneColor;
			if (bReflectionCapture)
			{
				GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One>::GetRHI();
			}
			else if (GetReflectionEnvironmentCVar() == 2 || GAOOverwriteSceneColor)
			{
				// override scene color for debugging
				GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			}
			else
			{
				const bool bCheckerboardSubsurfaceRendering = FRCPassPostProcessSubsurface::RequiresCheckerboardSubsurfaceRendering(SceneContext.GetSceneColorFormat());
				if (bCheckerboardSubsurfaceRendering)
				{
					GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One>::GetRHI();
				}
				else
				{
					GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI();
				}
			}

			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(PixelShader);
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

			PixelShader->SetParameters(RHICmdList, View, SSROutput->GetRenderTargetItem().ShaderResourceTexture, DynamicBentNormalAO);

			if (bReflectionCapture)
			{
				DrawRectangle(
					RHICmdList,
					0, 0,
					View.ViewRect.Width(), View.ViewRect.Height(),
					0, 0,
					View.ViewRect.Width(), View.ViewRect.Height(),
					FIntPoint(View.ViewRect.Width(), View.ViewRect.Height()),
					SceneContext.GetBufferSizeXY(),
					*VertexShader,
					EDRF_UseTriangleOptimization);
			}
			else
			{
				DrawRectangle(
					RHICmdList,
					0, 0,
					View.ViewRect.Width(), View.ViewRect.Height(),
					View.ViewRect.Min.X, View.ViewRect.Min.Y,
					View.ViewRect.Width(), View.ViewRect.Height(),
					FIntPoint(View.ViewRect.Width(), View.ViewRect.Height()),
					SceneContext.GetBufferSizeXY(),
					*VertexShader);
			}

			ResolveSceneColor(RHICmdList);
		}
	}
}
