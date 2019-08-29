// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessMaterial.cpp: Post processing Material implementation.
=============================================================================*/

#include "PostProcess/PostProcessMaterial.h"
#include "Materials/Material.h"
#include "MaterialShaderType.h"
#include "MaterialShader.h"
#include "SceneUtils.h"
#include "PostProcess/SceneRenderTargets.h"
#include "PostProcess/SceneFilterRendering.h"
#include "SceneRendering.h"
#include "ClearQuad.h"

enum class EPostProcessMaterialTarget
{
	HighEnd,
	Mobile
};

static bool ShouldCachePostProcessMaterial(EPostProcessMaterialTarget MaterialTarget, EShaderPlatform Platform, const FMaterial* Material)
{
	if (Material->GetMaterialDomain() == MD_PostProcess)
	{
		switch (MaterialTarget)
		{
		case EPostProcessMaterialTarget::HighEnd:
			return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM4);
		case EPostProcessMaterialTarget::Mobile:
			return IsMobilePlatform(Platform) && IsMobileHDR();
		}
	}

	return false;
}

template<EPostProcessMaterialTarget MaterialTarget>
class FPostProcessMaterialVS : public FMaterialShader
{
	DECLARE_SHADER_TYPE(FPostProcessMaterialVS, Material);
public:

	/**
	  * Only compile these shaders for post processing domain materials
	  */
	static bool ShouldCompilePermutation(EShaderPlatform Platform, const FMaterial* Material)
	{
		return ShouldCachePostProcessMaterial(MaterialTarget, Platform, Material);
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, const class FMaterial* Material, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Platform, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL"), 1);

		if (MaterialTarget == EPostProcessMaterialTarget::Mobile)
		{
			OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL_BEFORE_TONEMAP"), (Material->GetBlendableLocation() != BL_AfterTonemapping) ? 1 : 0);
		}
	}
	
	FPostProcessMaterialVS( )	{ }
	FPostProcessMaterialVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMaterialShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
	}

	void SetParameters(FRHICommandList& RHICmdList, const FRenderingCompositePassContext& Context )
	{
		const FVertexShaderRHIParamRef ShaderRHI = GetVertexShader();
		FMaterialShader::SetViewParameters(RHICmdList, ShaderRHI, Context.View, Context.View.ViewUniformBuffer);
		PostprocessParameter.SetVS(ShaderRHI, Context, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
	}

	// Begin FShader interface
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FMaterialShader::Serialize(Ar);
		Ar << PostprocessParameter;
		return bShaderHasOutdatedParameters;
	}
	//  End FShader interface 
private:
	FPostProcessPassParameters PostprocessParameter;
};

typedef FPostProcessMaterialVS<EPostProcessMaterialTarget::HighEnd> FPostProcessMaterialVS_HighEnd;
typedef FPostProcessMaterialVS<EPostProcessMaterialTarget::Mobile> FPostProcessMaterialVS_Mobile;

IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,FPostProcessMaterialVS_HighEnd,TEXT("/Engine/Private/PostProcessMaterialShaders.usf"),TEXT("MainVS"),SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,FPostProcessMaterialVS_Mobile,TEXT("/Engine/Private/PostProcessMaterialShaders.usf"),TEXT("MainVS_ES2"),SF_Vertex);

/**
 * A pixel shader for rendering a post process material
 */
template<EPostProcessMaterialTarget MaterialTarget, uint32 UVPolicy>
class FPostProcessMaterialPS : public FMaterialShader
{
	DECLARE_SHADER_TYPE(FPostProcessMaterialPS,Material);
public:

	/**
	  * Only compile these shaders for post processing domain materials
	  */
	static bool ShouldCompilePermutation(EShaderPlatform Platform, const FMaterial* Material)
	{
		return ShouldCachePostProcessMaterial(MaterialTarget, Platform, Material);
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, const class FMaterial* Material, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Platform, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL"), 1);
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL_UV_POLICY"), UVPolicy);

		EBlendableLocation Location = EBlendableLocation(Material->GetBlendableLocation());
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL_AFTER_TAA_UPSAMPLE"), (Location == BL_AfterTonemapping || Location == BL_ReplacingTonemapper) ? 1 : 0);

		if (MaterialTarget == EPostProcessMaterialTarget::Mobile)
		{
			OutEnvironment.SetDefine(TEXT("MOBILE_FORCE_DEPTH_TEXTURE_READS"), 1); // Ensure post process materials will not attempt depth buffer fetch operations.
			OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL_BEFORE_TONEMAP"), (Location != BL_AfterTonemapping) ? 1 : 0);
		}
	}

	FPostProcessMaterialPS() {}
	FPostProcessMaterialPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FMaterialShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
	}

	template <typename TRHICmdList>
	void SetParameters(TRHICmdList& RHICmdList, const FRenderingCompositePassContext& Context, const FMaterialRenderProxy* Material )
	{
		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();

		FMaterialShader::SetParameters(RHICmdList, ShaderRHI, Material, *Material->GetMaterial(Context.View.GetFeatureLevel()), Context.View, Context.View.ViewUniformBuffer, ESceneTextureSetupMode::All);
		PostprocessParameter.SetPS(RHICmdList, ShaderRHI, Context, TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FMaterialShader::Serialize(Ar);
		Ar << PostprocessParameter;
		return bShaderHasOutdatedParameters;
	}

private:
	FPostProcessPassParameters PostprocessParameter;
};

typedef FPostProcessMaterialPS<EPostProcessMaterialTarget::HighEnd, 0> FFPostProcessMaterialPS_HighEnd0;
typedef FPostProcessMaterialPS<EPostProcessMaterialTarget::HighEnd, 1> FFPostProcessMaterialPS_HighEnd1;
typedef FPostProcessMaterialPS<EPostProcessMaterialTarget::Mobile, 0> FPostProcessMaterialPS_Mobile;

IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FFPostProcessMaterialPS_HighEnd0, TEXT("/Engine/Private/PostProcessMaterialShaders.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FFPostProcessMaterialPS_HighEnd1, TEXT("/Engine/Private/PostProcessMaterialShaders.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,FPostProcessMaterialPS_Mobile,TEXT("/Engine/Private/PostProcessMaterialShaders.usf"),TEXT("MainPS_ES2"),SF_Pixel);

FRCPassPostProcessMaterial::FRCPassPostProcessMaterial(UMaterialInterface* InMaterialInterface, ERHIFeatureLevel::Type InFeatureLevel, EPixelFormat OutputFormatIN)
: MaterialInterface(InMaterialInterface), OutputFormat(OutputFormatIN)
{
	FMaterialRenderProxy* Proxy = MaterialInterface->GetRenderProxy(false);
	check(Proxy);

	const FMaterial* Material = Proxy->GetMaterialNoFallback(InFeatureLevel);
	
	if (!Material || Material->GetMaterialDomain() != MD_PostProcess)
	{
		MaterialInterface = UMaterial::GetDefaultMaterial(MD_PostProcess);
	}
}
		
/** The filter vertex declaration resource type. */
class FPostProcessMaterialVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;
	
	/** Destructor. */
	virtual ~FPostProcessMaterialVertexDeclaration() {}
	
	virtual void InitRHI()
	{
		FVertexDeclarationElementList Elements;
		uint32 Stride = sizeof(FFilterVertex);
		Elements.Add(FVertexElement(0,STRUCT_OFFSET(FFilterVertex,Position),VET_Float4,0,Stride));
		VertexDeclarationRHI = RHICreateVertexDeclaration(Elements);
	}
	
	virtual void ReleaseRHI()
	{
		VertexDeclarationRHI.SafeRelease();
	}
};
TGlobalResource<FPostProcessMaterialVertexDeclaration> GPostProcessMaterialVertexDeclaration;

void FRCPassPostProcessMaterial::Process(FRenderingCompositePassContext& Context)
{
	FMaterialRenderProxy* Proxy = MaterialInterface->GetRenderProxy(false);

	check(Proxy);

	ERHIFeatureLevel::Type FeatureLevel = Context.View.GetFeatureLevel();
	
	const FMaterial* Material = Proxy->GetMaterial(FeatureLevel);
	
	check(Material);

	const FViewInfo& View = Context.View;
	const FSceneViewFamily& ViewFamily = *(View.Family);

	const FPooledRenderTargetDesc* InputDesc = GetInputDesc(ePId_Input0);

	const FSceneRenderTargetItem& DestRenderTarget = PassOutputs[0].RequestSurface(Context);

	FIntRect SrcRect = Context.SceneColorViewRect;
	FIntRect DestRect = Context.GetSceneColorDestRect(DestRenderTarget);
	checkf(DestRect.Size() == SrcRect.Size(), TEXT("Post process material should not be used as upscaling pass."));

	FIntPoint SrcSize = InputDesc->Extent;

	SCOPED_DRAW_EVENTF(Context.RHICmdList, PostProcessMaterial, TEXT("PostProcessMaterial %dx%d Material=%s"), DestRect.Width(), DestRect.Height(), *Material->GetFriendlyName());

	ERenderTargetLoadAction LoadAction = Context.GetLoadActionForRenderTarget(DestRenderTarget);
	FRHIRenderTargetView RtView = FRHIRenderTargetView(DestRenderTarget.TargetableTexture, LoadAction);
	FRHISetRenderTargetsInfo Info(1, &RtView, FRHIDepthRenderTargetView());
	Context.RHICmdList.SetRenderTargetsAndClear(Info);
	Context.SetViewportAndCallRHI(DestRect);

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	const FMaterialShaderMap* MaterialShaderMap = Material->GetRenderingThreadShaderMap();
	FShader* VertexShader = nullptr;

	// uses mobile's post process material.
	if (FeatureLevel <= ERHIFeatureLevel::ES3_1)
	{
		FPostProcessMaterialPS_Mobile* PixelShader_Mobile = MaterialShaderMap->GetShader<FPostProcessMaterialPS_Mobile>();
		FPostProcessMaterialVS_Mobile* VertexShader_Mobile = MaterialShaderMap->GetShader<FPostProcessMaterialVS_Mobile>();

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(VertexShader_Mobile);
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(PixelShader_Mobile);

		SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

		VertexShader_Mobile->SetParameters(Context.RHICmdList, Context);
		PixelShader_Mobile->SetParameters(Context.RHICmdList, Context, MaterialInterface->GetRenderProxy(false));
		VertexShader = VertexShader_Mobile;
	}
	// Uses highend post process material that assumed ViewSize == BufferSize.
	else if (View.ViewRect == Context.SceneColorViewRect && View.ViewRect.Size() == SrcSize && View.ViewRect.Min == FIntPoint::ZeroValue)
	{
		FFPostProcessMaterialPS_HighEnd0* PixelShader_HighEnd = MaterialShaderMap->GetShader<FFPostProcessMaterialPS_HighEnd0>();
		FPostProcessMaterialVS_HighEnd* VertexShader_HighEnd = MaterialShaderMap->GetShader<FPostProcessMaterialVS_HighEnd>();

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GPostProcessMaterialVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(VertexShader_HighEnd);
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(PixelShader_HighEnd);

		SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

		VertexShader_HighEnd->SetParameters(Context.RHICmdList, Context);
		PixelShader_HighEnd->SetParameters(Context.RHICmdList, Context, MaterialInterface->GetRenderProxy(false));
		VertexShader = VertexShader_HighEnd;
	}
	// Uses highend post process material that handle ViewSize != BufferSize.
	else
	{
		FFPostProcessMaterialPS_HighEnd1* PixelShader_HighEnd = MaterialShaderMap->GetShader<FFPostProcessMaterialPS_HighEnd1>();
		FPostProcessMaterialVS_HighEnd* VertexShader_HighEnd = MaterialShaderMap->GetShader<FPostProcessMaterialVS_HighEnd>();

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GPostProcessMaterialVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(VertexShader_HighEnd);
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(PixelShader_HighEnd);

		SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

		VertexShader_HighEnd->SetParameters(Context.RHICmdList, Context);
		PixelShader_HighEnd->SetParameters(Context.RHICmdList, Context, MaterialInterface->GetRenderProxy(false));
		VertexShader = VertexShader_HighEnd;
	}

	DrawPostProcessPass(
		Context.RHICmdList,
		0, 0,
		DestRect.Width(), DestRect.Height(),
		SrcRect.Min.X, SrcRect.Min.Y,
		SrcRect.Width(), SrcRect.Height(),
		DestRect.Size(),
		SrcSize,
		VertexShader,
		View.StereoPass,
		Context.HasHmdMesh(),
		EDRF_UseTriangleOptimization);

	Context.RHICmdList.CopyToResolveTarget(DestRenderTarget.TargetableTexture, DestRenderTarget.ShaderResourceTexture, FResolveParams());

	if(Material->NeedsGBuffer())
	{
		FSceneRenderTargets::Get(Context.RHICmdList).AdjustGBufferRefCount(Context.RHICmdList,-1);
	}
}

FPooledRenderTargetDesc FRCPassPostProcessMaterial::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret = GetInput(ePId_Input0)->GetOutput()->RenderTargetDesc;

	if (OutputFormat != PF_Unknown)
	{
		Ret.Format = OutputFormat;
	}
	Ret.Reset();
	Ret.AutoWritable = false;
	Ret.DebugName = TEXT("PostProcessMaterial");
	Ret.ClearValue = FClearValueBinding(FLinearColor::Black);

	return Ret;
}
