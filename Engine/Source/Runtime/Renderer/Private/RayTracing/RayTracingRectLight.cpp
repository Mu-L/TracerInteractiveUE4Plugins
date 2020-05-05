// Copyright Epic Games, Inc. All Rights Reserved.

#include "DeferredShadingRenderer.h"

#if RHI_RAYTRACING

#include "ClearQuad.h"
#include "SceneRendering.h"
#include "SceneRenderTargets.h"
#include "RenderTargetPool.h"
#include "RHIResources.h"
#include "UniformBuffer.h"
#include "VisualizeTexture.h"

#include "PostProcess/PostProcessing.h"
#include "PostProcess/SceneFilterRendering.h"
#include "RectLightSceneProxy.h"
#include "RaytracingOptions.h"

#include "RHI/Public/PipelineStateCache.h"

static int32 GRayTracingStochasticRectLight = 0;
static FAutoConsoleVariableRef CVarRayTracingStochasticRectLight(
	TEXT("r.RayTracing.StochasticRectLight"),
	GRayTracingStochasticRectLight,
	TEXT("0: use analytical evaluation (default)\n")
	TEXT("1: use stochastic evaluation\n")
);

static int32 GRayTracingStochasticRectLightSamplesPerPixel = -1;
static FAutoConsoleVariableRef CVarRayTracingRecLightStochasticSamplesPerPixel(
	TEXT("r.RayTracing.StochasticRectLight.SamplesPerPixel"),
	GRayTracingStochasticRectLightSamplesPerPixel,
	TEXT("Sets the samples-per-pixel for rect light evaluation (default = 1)")
);

static int32 GRayTracingStochasticRectLightIsTextureImportanceSampling = 1;
static FAutoConsoleVariableRef CVarRayTracingStochasticRecLightIsTextureImportanceSampling(
	TEXT("r.RayTracing.StochasticRectLight.IsTextureImportanceSampling"),
	GRayTracingStochasticRectLightIsTextureImportanceSampling,
	TEXT("Enable importance sampling for rect light evaluation (default = 1)")
);

bool ShouldRenderRayTracingStochasticRectLight(const FLightSceneInfo& LightInfo)
{
	return IsRayTracingEnabled() 
		&& GRayTracingStochasticRectLight == 1 
		&& GetForceRayTracingEffectsCVarValue() != 0
		&& LightInfo.Proxy->CastsRaytracedShadow()
		&& LightInfo.Proxy->GetLightType() == LightType_Rect;
}

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FRectLightData, )
// Pass settings
SHADER_PARAMETER(int, SamplesPerPixel)
SHADER_PARAMETER(int, bIsTextureImportanceSampling)
// Light data
SHADER_PARAMETER(FVector, Position)
SHADER_PARAMETER(FVector, Normal)
SHADER_PARAMETER(FVector, dPdu)
SHADER_PARAMETER(FVector, dPdv)
SHADER_PARAMETER(FVector, Color)
SHADER_PARAMETER(float, Width)
SHADER_PARAMETER(float, Height)
SHADER_PARAMETER(FIntVector, MipTreeDimensions)
SHADER_PARAMETER(float, MaxNormalBias)
SHADER_PARAMETER(float, BarnCosAngle)
SHADER_PARAMETER(float, BarnLength)
SHADER_PARAMETER_TEXTURE(Texture2D, Texture)
SHADER_PARAMETER_SAMPLER(SamplerState, TextureSampler)
// Sampling data
SHADER_PARAMETER_SRV(Buffer<float>, MipTree)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

DECLARE_GPU_STAT_NAMED(RayTracingRectLight, TEXT("Ray Tracing RectLight"));

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FRectLightData, "RectLight");



class FBuildRectLightMipTreeCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FBuildRectLightMipTreeCS, Global)

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 16;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}

	FBuildRectLightMipTreeCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		TextureParameter.Bind(Initializer.ParameterMap, TEXT("RectLightTexture"));
		TextureSamplerParameter.Bind(Initializer.ParameterMap, TEXT("TextureSampler"));
		DimensionsParameter.Bind(Initializer.ParameterMap, TEXT("Dimensions"));
		MipLevelParameter.Bind(Initializer.ParameterMap, TEXT("MipLevel"));
		MipTreeParameter.Bind(Initializer.ParameterMap, TEXT("MipTree"));
	}

	FBuildRectLightMipTreeCS() {}

	void SetParameters(
		FRHICommandList& RHICmdList,
		FTextureRHIRef Texture,
		const FIntVector& Dimensions,
		uint32 MipLevel,
		FRWBuffer& MipTree
	)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		SetShaderValue(RHICmdList, ShaderRHI, DimensionsParameter, Dimensions);
		SetShaderValue(RHICmdList, ShaderRHI, MipLevelParameter, MipLevel);
		SetTextureParameter(RHICmdList, ShaderRHI, TextureParameter, TextureSamplerParameter, TStaticSamplerState<SF_Bilinear>::GetRHI(), Texture);

		check(MipTreeParameter.IsBound());
		MipTreeParameter.SetBuffer(RHICmdList, ShaderRHI, MipTree);
	}

	void UnsetParameters(
		FRHICommandList& RHICmdList,
		EResourceTransitionAccess TransitionAccess,
		EResourceTransitionPipeline TransitionPipeline,
		FRWBuffer& MipTree,
		FRHIComputeFence* Fence)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		MipTreeParameter.UnsetUAV(RHICmdList, ShaderRHI);
		RHICmdList.TransitionResource(TransitionAccess, TransitionPipeline, MipTree.UAV, Fence);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, TextureParameter);
	LAYOUT_FIELD(FShaderResourceParameter, TextureSamplerParameter);

	LAYOUT_FIELD(FShaderParameter, DimensionsParameter);
	LAYOUT_FIELD(FShaderParameter, MipLevelParameter);
	LAYOUT_FIELD(FRWShaderParameter, MipTreeParameter);
};

IMPLEMENT_SHADER_TYPE(, FBuildRectLightMipTreeCS, TEXT("/Engine/Private/Raytracing/BuildMipTreeCS.usf"), TEXT("BuildRectLightMipTreeCS"), SF_Compute)

DECLARE_GPU_STAT_NAMED(BuildRectLightMipTreeStat, TEXT("build RectLight MipTree"));

FRectLightRayTracingData BuildRectLightMipTree(FRHICommandListImmediate& RHICmdList, UTexture* SourceTexture)
{
	SCOPED_GPU_STAT(RHICmdList, BuildRectLightMipTreeStat);

	check(IsInRenderingThread());
	FRectLightRayTracingData Data;
	FTextureRHIRef RhiTexture = SourceTexture ? SourceTexture->Resource->TextureRHI : GWhiteTexture->TextureRHI;

	const auto ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);
	TShaderMapRef<FBuildRectLightMipTreeCS> BuildRectLightMipTreeComputeShader(ShaderMap);
	RHICmdList.SetComputeShader(BuildRectLightMipTreeComputeShader.GetComputeShader());

	// Allocate MIP tree
	FIntVector TextureSize = RhiTexture->GetSizeXYZ();
	uint32 MipLevelCount = FMath::Min(FMath::CeilLogTwo(TextureSize.X), FMath::CeilLogTwo(TextureSize.Y));
	Data.RectLightMipTreeDimensions = FIntVector(1 << MipLevelCount, 1 << MipLevelCount, 1);
	uint32 NumElements = Data.RectLightMipTreeDimensions.X * Data.RectLightMipTreeDimensions.Y;
	for (uint32 MipLevel = 1; MipLevel <= MipLevelCount; ++MipLevel)
	{
		uint32 NumElementsInLevel = (Data.RectLightMipTreeDimensions.X >> MipLevel) * (Data.RectLightMipTreeDimensions.Y >> MipLevel);
		NumElements += NumElementsInLevel;
	}

	Data.RectLightMipTree.Initialize(sizeof(float), NumElements, PF_R32_FLOAT, BUF_UnorderedAccess | BUF_ShaderResource);

	// Execute hierarchical build
	for (uint32 MipLevel = 0; MipLevel <= MipLevelCount; ++MipLevel)
	{
		FComputeFenceRHIRef MipLevelFence = RHICmdList.CreateComputeFence(TEXT("RectLightMipTree Build"));
		BuildRectLightMipTreeComputeShader->SetParameters(RHICmdList, RhiTexture, Data.RectLightMipTreeDimensions, MipLevel, Data.RectLightMipTree);
		FIntVector MipLevelDimensions = FIntVector(Data.RectLightMipTreeDimensions.X >> MipLevel, Data.RectLightMipTreeDimensions.Y >> MipLevel, 1);
		FIntVector NumGroups = FIntVector::DivideAndRoundUp(MipLevelDimensions, FBuildRectLightMipTreeCS::GetGroupSize());
		DispatchComputeShader(RHICmdList, BuildRectLightMipTreeComputeShader.GetShader(), NumGroups.X, NumGroups.Y, 1);
		BuildRectLightMipTreeComputeShader->UnsetParameters(RHICmdList, EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, Data.RectLightMipTree, MipLevelFence);
	}
	FComputeFenceRHIRef TransitionFence = RHICmdList.CreateComputeFence(TEXT("RectLightMipTree Transition"));
	BuildRectLightMipTreeComputeShader->UnsetParameters(RHICmdList, EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, Data.RectLightMipTree, TransitionFence);

	return Data;
}



template <int TextureImportanceSampling>
class FRectLightRGS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FRectLightRGS, Global)

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("TEXTURE_IMPORTANCE_SAMPLING"), TextureImportanceSampling);
	}

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	FRectLightRGS() {}
	~FRectLightRGS() {}

	FRectLightRGS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ViewParameter.Bind(Initializer.ParameterMap, TEXT("View"));
		SceneTexturesParameter.Bind(Initializer.ParameterMap, TEXT("SceneTexturesStruct"));
		RectLightParameter.Bind(Initializer.ParameterMap, TEXT("RectLight"));
		TLASParameter.Bind(Initializer.ParameterMap, TEXT("TLAS"));
		TransmissionProfilesTextureParameter.Bind(Initializer.ParameterMap, TEXT("SSProfilesTexture"));
		TransmissionProfilesLinearSamplerParameter.Bind(Initializer.ParameterMap, TEXT("TransmissionProfilesLinearSampler"));

		LuminanceUAVParameter.Bind(Initializer.ParameterMap, TEXT("RWLuminanceUAV"));
		RayDistanceUAVParameter.Bind(Initializer.ParameterMap, TEXT("RWRayDistanceUAV"));
	}

	/*bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << ViewParameter;
		Ar << SceneTexturesParameter;
		Ar << RectLightParameter;
		Ar << TLASParameter;
		Ar << TransmissionProfilesTextureParameter;
		Ar << TransmissionProfilesLinearSamplerParameter;
		Ar << LuminanceUAVParameter;
		Ar << RayDistanceUAVParameter;
		return bShaderHasOutdatedParameters;
	}*/

	void Dispatch(
		FRHICommandListImmediate& RHICmdList,
		FRHIRayTracingShader* ShaderRHI,
		const FRayTracingScene& RayTracingScene,
		FRHIUniformBuffer* ViewUniformBuffer,
		FRHIUniformBuffer* SceneTexturesUniformBuffer,
		FRHIUniformBuffer* RectLightUniformBuffer,
		FRHIUnorderedAccessView* LuminanceUAV,
		FRHIUnorderedAccessView* RayDistanceUAV,
		uint32 Width, uint32 Height
	)
	{
		FRayTracingPipelineStateInitializer Initializer;

		FRHIRayTracingShader* RayGenShaderTable[] = { ShaderRHI };
		Initializer.SetRayGenShaderTable(RayGenShaderTable);

		FRayTracingPipelineState* Pipeline = PipelineStateCache::GetAndOrCreateRayTracingPipelineState(RHICmdList, Initializer);

		FRayTracingShaderBindingsWriter GlobalResources;
		GlobalResources.Set(TLASParameter, RayTracingScene.RayTracingSceneRHI->GetShaderResourceView());
		GlobalResources.Set(ViewParameter, ViewUniformBuffer);
		GlobalResources.Set(SceneTexturesParameter, SceneTexturesUniformBuffer);
		GlobalResources.Set(RectLightParameter, RectLightUniformBuffer);
		GlobalResources.Set(LuminanceUAVParameter, LuminanceUAV);
		GlobalResources.Set(RayDistanceUAVParameter, RayDistanceUAV);

		if (TransmissionProfilesTextureParameter.IsBound())
		{
			FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
			const IPooledRenderTarget* PooledRT = GetSubsufaceProfileTexture_RT((FRHICommandListImmediate&)RHICmdList);

			if (!PooledRT)
			{
				// no subsurface profile was used yet
				PooledRT = GSystemTextures.BlackDummy;
			}
			const FSceneRenderTargetItem& Item = PooledRT->GetRenderTargetItem();

			GlobalResources.SetTexture(TransmissionProfilesTextureParameter.GetBaseIndex(), Item.ShaderResourceTexture);
			GlobalResources.SetSampler(TransmissionProfilesLinearSamplerParameter.GetBaseIndex(), TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
		}

		RHICmdList.RayTraceDispatch(Pipeline, ShaderRHI, RayTracingScene.RayTracingSceneRHI, GlobalResources, Width, Height);
	}

private:
	// Input
	LAYOUT_FIELD(FShaderResourceParameter, TLASParameter);
	LAYOUT_FIELD(FShaderUniformBufferParameter, ViewParameter);
	LAYOUT_FIELD(FShaderUniformBufferParameter, SceneTexturesParameter);
	LAYOUT_FIELD(FShaderUniformBufferParameter, RectLightParameter);

	// SSS Profile
	LAYOUT_FIELD(FShaderResourceParameter, TransmissionProfilesTextureParameter);
	LAYOUT_FIELD(FShaderResourceParameter, TransmissionProfilesLinearSamplerParameter);

	// Output
	LAYOUT_FIELD(FShaderResourceParameter, LuminanceUAVParameter);
	LAYOUT_FIELD(FShaderResourceParameter, RayDistanceUAVParameter);
};

#define IMPLEMENT_RECT_LIGHT_TYPE(TextureImportanceSampling) \
	typedef FRectLightRGS<TextureImportanceSampling> FRectLightRGS##TextureImportanceSampling; \
	IMPLEMENT_SHADER_TYPE(template<>, FRectLightRGS##TextureImportanceSampling, TEXT("/Engine/Private/RayTracing/RayTracingRectLightRGS.usf"), TEXT("RectLightRGS"), SF_RayGen);

IMPLEMENT_RECT_LIGHT_TYPE(0);
IMPLEMENT_RECT_LIGHT_TYPE(1);

class FVisualizeRectLightMipTreePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FVisualizeRectLightMipTreePS, Global);

public:
	static bool ShouldCache(EShaderPlatform Platform)
	{
		return ShouldCompileRayTracingShadersForProject(Platform);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	FVisualizeRectLightMipTreePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		DimensionsParameter.Bind(Initializer.ParameterMap, TEXT("Dimensions"));
		MipTreeParameter.Bind(Initializer.ParameterMap, TEXT("MipTree"));
	}

	FVisualizeRectLightMipTreePS() {}

	template<typename TRHICommandList>
	void SetParameters(
		TRHICommandList& RHICmdList,
		const FViewInfo& View,
		const FRWBuffer& MipTree,
		const FIntVector Dimensions)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		SetShaderValue(RHICmdList, ShaderRHI, DimensionsParameter, Dimensions);
		SetSRVParameter(RHICmdList, ShaderRHI, MipTreeParameter, MipTree.SRV);
	}

private:
	LAYOUT_FIELD(FShaderParameter, DimensionsParameter);
	LAYOUT_FIELD(FShaderResourceParameter, MipTreeParameter);
};

IMPLEMENT_SHADER_TYPE(, FVisualizeRectLightMipTreePS, TEXT("/Engine/Private/PathTracing/VisualizeMipTreePixelShader.usf"), TEXT("VisualizeMipTreePS"), SF_Pixel)

void FDeferredShadingSceneRenderer::VisualizeRectLightMipTree(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	const FRWBuffer& RectLightMipTree,
	const FIntVector& RectLightMipTreeDimensions
)
{
	// Allocate render target
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	FPooledRenderTargetDesc Desc = SceneContext.GetSceneColor()->GetDesc();
	Desc.Flags &= ~(TexCreate_FastVRAM | TexCreate_Transient);
	TRefCountPtr<IPooledRenderTarget> RectLightMipTreeRT;
	GRenderTargetPool.FindFreeElement(RHICmdList, Desc, RectLightMipTreeRT, TEXT("RectLightMipTreeRT"));

	// Define shaders
	const auto ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	TShaderMapRef<FPostProcessVS> VertexShader(ShaderMap);
	TShaderMapRef<FVisualizeRectLightMipTreePS> PixelShader(ShaderMap);
	FRHITexture* RenderTargets[2] =
	{
		SceneContext.GetSceneColor()->GetRenderTargetItem().TargetableTexture,
		RectLightMipTreeRT->GetRenderTargetItem().TargetableTexture
	};
	FRHIRenderPassInfo RenderPassInfo(2, RenderTargets, ERenderTargetActions::Load_Store);
	RHICmdList.BeginRenderPass(RenderPassInfo, TEXT("RectLightMipTree Visualization"));

	// PSO definition
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;
	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

	// Transition to graphics
	RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, RectLightMipTree.UAV);

	// Draw
	RHICmdList.SetViewport((float)View.ViewRect.Min.X, (float)View.ViewRect.Min.Y, 0.0f, (float)View.ViewRect.Max.X, (float)View.ViewRect.Max.Y, 1.0f);
	PixelShader->SetParameters(RHICmdList, View, RectLightMipTree, RectLightMipTreeDimensions);
	DrawRectangle(
		RHICmdList,
		0, 0,
		View.ViewRect.Width(), View.ViewRect.Height(),
		View.ViewRect.Min.X, View.ViewRect.Min.Y,
		View.ViewRect.Width(), View.ViewRect.Height(),
		FIntPoint(View.ViewRect.Width(), View.ViewRect.Height()),
		SceneContext.GetBufferSizeXY(),
		VertexShader);
	ResolveSceneColor(RHICmdList);
	RHICmdList.EndRenderPass();
	GVisualizeTexture.SetCheckPoint(RHICmdList, RectLightMipTreeRT);

	// Transition to compute
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, RectLightMipTree.UAV);
}

void FDeferredShadingSceneRenderer::PrepareRayTracingRectLight(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	// Declare all RayGen shaders that require material closest hit shaders to be bound

	TShaderMapRef<FRectLightRGS<0>> Shader0(GetGlobalShaderMap(View.FeatureLevel));
	TShaderMapRef<FRectLightRGS<1>> Shader1(GetGlobalShaderMap(View.FeatureLevel));

	OutRayGenShaders.Add(Shader0.GetRayTracingShader());
	OutRayGenShaders.Add(Shader1.GetRayTracingShader());
}

template <int TextureImportanceSampling>
void FDeferredShadingSceneRenderer::RenderRayTracingRectLightInternal(
	FRHICommandListImmediate& RHICmdList,
	const TArray<FViewInfo>& Views,
	const FLightSceneInfo& RectLightSceneInfo,
	TRefCountPtr<IPooledRenderTarget>& ScreenShadowMaskTexture,
	TRefCountPtr<IPooledRenderTarget>& RayDistanceTexture
)
{
	check(RectLightSceneInfo.Proxy);
	check(RectLightSceneInfo.Proxy->IsRectLight());
	FRectLightSceneProxy* RectLightSceneProxy = (FRectLightSceneProxy*)RectLightSceneInfo.Proxy;

	check(RectLightSceneProxy->RayTracingData);
	if( !RectLightSceneProxy->RayTracingData->bInitialised // Test needed in case GRayTracingStochasticRectLight is turned on in editor,
		|| (RectLightSceneProxy->SourceTexture && RectLightSceneProxy->SourceTexture->GetLightingGuid() != RectLightSceneProxy->RayTracingData->TextureLightingGuid))
	{
		// We ignore TextureImportanceSampling and RectLightSceneProxy->HasSourceTexture() because uniform buffer expect a resource.
		// So we always update.  
		// dxr-todo: cache texture RayTracingData render side based on GUID in a database (render thread safe and avoid duplicating the work for each light using the same texture).
		*RectLightSceneProxy->RayTracingData = BuildRectLightMipTree(RHICmdList, RectLightSceneProxy->SourceTexture);
		RectLightSceneProxy->RayTracingData->bInitialised = true;
		if (RectLightSceneProxy->SourceTexture)
		{
			RectLightSceneProxy->RayTracingData->TextureLightingGuid = RectLightSceneProxy->SourceTexture->GetLightingGuid();
		}
	}

#if 0
	// Debug visualization
	if (RectLightSceneProxy->SourceTexture)
	{
		VisualizeRectLightMipTree(RHICmdList, Views[0],
			RectLightSceneProxy->RayTracingData->RectLightMipTree,
			RectLightSceneProxy->RayTracingData->RectLightMipTreeDimensions);
	}
#endif

	FLightShaderParameters LightShaderParameters;
	RectLightSceneProxy->GetLightShaderParameters(LightShaderParameters);

	FRectLightData RectLightData;
	RectLightData.SamplesPerPixel = GRayTracingStochasticRectLightSamplesPerPixel >= 0 ? GRayTracingStochasticRectLightSamplesPerPixel : RectLightSceneProxy->GetSamplesPerPixel();
	RectLightData.bIsTextureImportanceSampling = GRayTracingStochasticRectLightIsTextureImportanceSampling;
	RectLightData.Position = RectLightSceneInfo.Proxy->GetOrigin();
	RectLightData.Normal = RectLightSceneInfo.Proxy->GetDirection();
	const FMatrix& WorldToLight = RectLightSceneInfo.Proxy->GetWorldToLight();
	RectLightData.dPdu = FVector(WorldToLight.M[0][1], WorldToLight.M[1][1], WorldToLight.M[2][1]);
	RectLightData.dPdv = FVector(WorldToLight.M[0][2], WorldToLight.M[1][2], WorldToLight.M[2][2]);
	RectLightData.Color = LightShaderParameters.Color / 2.0;

	// #dxr_todo: JIRA Ray traced textured area lights are 1.5X brighter than those in lit mode.
	if (RectLightSceneProxy->HasSourceTexture())
	{
		RectLightData.Color *= 2.0 / 3.0;
	}

	RectLightData.Width = 2.0f * LightShaderParameters.SourceRadius;
	RectLightData.Height = 2.0f * LightShaderParameters.SourceLength;
	RectLightData.Texture = LightShaderParameters.SourceTexture;
	RectLightData.TextureSampler = RHICreateSamplerState(FSamplerStateInitializerRHI(SF_Bilinear, AM_Border, AM_Border, AM_Border));
	RectLightData.MipTree = RectLightSceneProxy->RayTracingData->RectLightMipTree.SRV;
	RectLightData.MipTreeDimensions = RectLightSceneProxy->RayTracingData->RectLightMipTreeDimensions;
	RectLightData.MaxNormalBias = GetRaytracingMaxNormalBias();
	RectLightData.BarnCosAngle = FMath::Cos(FMath::DegreesToRadians(RectLightSceneProxy->BarnDoorAngle));
	RectLightData.BarnLength = RectLightSceneProxy->BarnDoorLength;
	FUniformBufferRHIRef RectLightUniformBuffer = RHICreateUniformBuffer(&RectLightData, FRectLightData::StaticStructMetadata.GetLayout(), EUniformBufferUsage::UniformBuffer_SingleDraw);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		const FViewInfo& View = Views[ViewIndex];
		const FIntPoint ViewSize = View.ViewRect.Size();

		TShaderMapRef<FRectLightRGS<TextureImportanceSampling>> RectLightRayGenerationShader(GetGlobalShaderMap(View.FeatureLevel));

		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		FSceneTexturesUniformParameters SceneTextures;
		SetupSceneTextureUniformParameters(SceneContext, View.FeatureLevel, ESceneTextureSetupMode::All, SceneTextures);
		FUniformBufferRHIRef SceneTexturesUniformBuffer = RHICreateUniformBuffer(&SceneTextures, FSceneTexturesUniformParameters::StaticStructMetadata.GetLayout(), EUniformBufferUsage::UniformBuffer_SingleDraw);

		// Dispatch
		RectLightRayGenerationShader->Dispatch(
			RHICmdList,
			RectLightRayGenerationShader.GetRayTracingShader(),
			View.RayTracingScene,
			View.ViewUniformBuffer,
			SceneTexturesUniformBuffer,
			RectLightUniformBuffer,
			ScreenShadowMaskTexture->GetRenderTargetItem().UAV,
			RayDistanceTexture->GetRenderTargetItem().UAV,
			ViewSize.X, ViewSize.Y
		);
	}

	// Transition out to graphics pipeline
	FComputeFenceRHIRef Fence = RHICmdList.CreateComputeFence(TEXT("RayTracingRectLight"));
	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToGfx, ScreenShadowMaskTexture->GetRenderTargetItem().UAV, Fence);
	GVisualizeTexture.SetCheckPoint(RHICmdList, ScreenShadowMaskTexture);

	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToGfx, RayDistanceTexture->GetRenderTargetItem().UAV);
	GVisualizeTexture.SetCheckPoint(RHICmdList, RayDistanceTexture);
}

#endif // RHI_RAYTRACING

void FDeferredShadingSceneRenderer::RenderRayTracingStochasticRectLight(
	FRHICommandListImmediate& RHICmdList,
	const FLightSceneInfo& RectLightSceneInfo,
	TRefCountPtr<IPooledRenderTarget>& RectLightRT,
	TRefCountPtr<IPooledRenderTarget>& HitDistanceRT
)
#if RHI_RAYTRACING
{
	SCOPED_DRAW_EVENT(RHICmdList, RayTracingRectLight);
	SCOPED_GPU_STAT(RHICmdList, RayTracingRectLight);

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	FPooledRenderTargetDesc Desc = SceneContext.GetSceneColor()->GetDesc();
	Desc.Format = PF_FloatRGBA;
	Desc.Flags &= ~(TexCreate_FastVRAM | TexCreate_Transient);
	GRenderTargetPool.FindFreeElement(RHICmdList, Desc, RectLightRT, TEXT("RayTracingRectLight"));
	//ClearUAV(RHICmdList, RectLightRT->GetRenderTargetItem(), FLinearColor::Black);

	Desc.Format = PF_R16F;
	GRenderTargetPool.FindFreeElement(RHICmdList, Desc, HitDistanceRT, TEXT("RayTracingRectLightDistance"));
	//ClearUAV(RHICmdList, HitDistanceRT->GetRenderTargetItem(), FLinearColor::Black);

	if (RectLightSceneInfo.Proxy->HasSourceTexture())
	{
		RenderRayTracingRectLightInternal<1>(RHICmdList, Views, RectLightSceneInfo, RectLightRT, HitDistanceRT);
	}
	else
	{
		RenderRayTracingRectLightInternal<0>(RHICmdList, Views, RectLightSceneInfo, RectLightRT, HitDistanceRT);
	}
}
#else
{
	unimplemented();
}
#endif
