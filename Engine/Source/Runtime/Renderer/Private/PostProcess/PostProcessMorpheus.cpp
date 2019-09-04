// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessMorpheus.cpp: Post processing for Sony Morpheus HMD device.
=============================================================================*/

#include "PostProcess/PostProcessMorpheus.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "SceneRenderTargetParameters.h"
#include "SceneRendering.h"
#include "PostProcess/SceneFilterRendering.h"
#include "IHeadMountedDisplay.h"
#include "IXRTrackingSystem.h"
#include "Misc/ConfigCacheIni.h"
#include "Engine/Engine.h"
#include "EngineGlobals.h"
#include "PipelineStateCache.h"

DEFINE_LOG_CATEGORY_STATIC(LogMorpheusHMDPostProcess, All, All);

#if defined(MORPHEUS_ENGINE_DISTORTION) && MORPHEUS_ENGINE_DISTORTION

/** Encapsulates the post processing HMD distortion and correction pixel shader. */
class FPostProcessMorpheusPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessMorpheusPS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		// we must use a run time check for this because the builds the build machines create will have Morpheus defined,
		// but a user will not necessarily have the Morpheus files
		bool bEnableMorpheus = false;
		if (GConfig->GetBool(TEXT("/Script/MorpheusEditor.MorpheusRuntimeSettings"), TEXT("bEnableMorpheus"), bEnableMorpheus, GEngineIni))
		{
			return bEnableMorpheus;
		}
		return false;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("NEW_MORPHEUS_DISTORTION"), TEXT("1"));
	}

	/** Default constructor. */
	FPostProcessMorpheusPS()
	{
	}

public:
	FPostProcessPassParameters PostprocessParameter;
	FSceneTextureShaderParameters SceneTextureParameters;
	
	// Distortion parameter values
	FShaderParameter TextureScale;
	FShaderParameter TextureOffset;
	FShaderParameter TextureUVOffset;
	FShaderParameter RCoefficients;
	FShaderParameter GCoefficients;
	FShaderParameter BCoefficients;

	FShaderResourceParameter DistortionTextureSampler; 

	/** Initialization constructor. */
	FPostProcessMorpheusPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
		SceneTextureParameters.Bind(Initializer);

		TextureScale.Bind(Initializer.ParameterMap, TEXT("TextureScale"));
		//check(TextureScaleLeft.IsBound());		

		TextureOffset.Bind(Initializer.ParameterMap, TEXT("TextureOffset"));
		//check(TextureOffsetRight.IsBound());

		TextureUVOffset.Bind(Initializer.ParameterMap, TEXT("TextureUVOffset"));
		//check(TextureUVOffset.IsBound());

		DistortionTextureSampler.Bind(Initializer.ParameterMap, TEXT("DistortionTextureSampler"));
		//check(DistortionTextureSampler.IsBound());		

		RCoefficients.Bind(Initializer.ParameterMap, TEXT("RCoefficients"));
		GCoefficients.Bind(Initializer.ParameterMap, TEXT("GCoefficients"));
		BCoefficients.Bind(Initializer.ParameterMap, TEXT("BCoefficients"));
	}


	template <typename TRHICmdList>
	void SetPS(TRHICmdList& RHICmdList, const FRenderingCompositePassContext& Context, FIntRect SrcRect, FIntPoint SrcBufferSize, EStereoscopicPass StereoPass, FMatrix& QuadTexTransform)
	{
		FRHIPixelShader* ShaderRHI = GetPixelShader();
		
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);

		PostprocessParameter.SetPS(RHICmdList, ShaderRHI, Context, TStaticSamplerState<SF_Bilinear, AM_Border, AM_Border, AM_Border>::GetRHI());
		SceneTextureParameters.Set(RHICmdList, ShaderRHI, Context.View.FeatureLevel, ESceneTextureSetupMode::All);

		{
			static FName MorpheusName(TEXT("PSVR"));
			check(GEngine->XRSystem.IsValid());
			check(GEngine->XRSystem->GetSystemName() == MorpheusName);

			IHeadMountedDisplay* HMDDevice = GEngine->XRSystem->GetHMDDevice();
			check(HMDDevice);

			auto RCoefs = HMDDevice->GetRedDistortionParameters();
			auto GCoefs = HMDDevice->GetGreenDistortionParameters();
			auto BCoefs = HMDDevice->GetBlueDistortionParameters();
			for (uint32 i = 0; i < 5; ++i)
			{
				SetShaderValue(RHICmdList, ShaderRHI, RCoefficients, RCoefs[i], i);
				SetShaderValue(RHICmdList, ShaderRHI, GCoefficients, GCoefs[i], i);
				SetShaderValue(RHICmdList, ShaderRHI, BCoefficients, BCoefs[i], i);
			}

			check (StereoPass != eSSP_FULL);
			if (StereoPass == eSSP_LEFT_EYE)
			{
				SetShaderValue(RHICmdList, ShaderRHI, TextureScale, HMDDevice->GetTextureScaleLeft());
				SetShaderValue(RHICmdList, ShaderRHI, TextureOffset, HMDDevice->GetTextureOffsetLeft());
				SetShaderValue(RHICmdList, ShaderRHI, TextureUVOffset, 0.0f);
			}
			else
			{
				SetShaderValue(RHICmdList, ShaderRHI, TextureScale, HMDDevice->GetTextureScaleRight());
				SetShaderValue(RHICmdList, ShaderRHI, TextureOffset, HMDDevice->GetTextureOffsetRight());
				SetShaderValue(RHICmdList, ShaderRHI, TextureUVOffset, -0.5f);
			}				
				  
			QuadTexTransform = FMatrix::Identity;            
		}
	}
	
	// FShader interface.
	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << PostprocessParameter << SceneTextureParameters << TextureScale << TextureOffset << TextureUVOffset << RCoefficients << GCoefficients << BCoefficients << DistortionTextureSampler;
		return bShaderHasOutdatedParameters;
	}
};

/** Encapsulates the post processing vertex shader. */
class FPostProcessMorpheusVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessMorpheusVS,Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		// we must use a run time check for this because the builds the build machines create will have Morpheus defined,
		// but a user will not necessarily have the Morpheus files
		bool bEnableMorpheus = false;
		if (GConfig->GetBool(TEXT("/Script/MorpheusEditor.MorpheusRuntimeSettings"), TEXT("bEnableMorpheus"), bEnableMorpheus, GEngineIni))
		{
			return bEnableMorpheus;
		}
		return false;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("NEW_MORPHEUS_DISTORTION"), TEXT("1"));
	}

	/** Default constructor. */
	FPostProcessMorpheusVS() {}

	/** to have a similar interface as all other shaders */
	void SetParameters(const FRenderingCompositePassContext& Context)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, GetVertexShader(), Context.View.ViewUniformBuffer);
	}

	void SetParameters(FRHICommandList& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, GetVertexShader(), ViewUniformBuffer);
	}

public:

	/** Initialization constructor. */
	FPostProcessMorpheusVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
	}
};

IMPLEMENT_SHADER_TYPE(, FPostProcessMorpheusVS, TEXT("/Engine/Private/PostProcessHMDMorpheus.usf"), TEXT("MainVS"), SF_Vertex);
IMPLEMENT_SHADER_TYPE(, FPostProcessMorpheusPS, TEXT("/Engine/Private/PostProcessHMDMorpheus.usf"), TEXT("MainPS"), SF_Pixel);

void FRCPassPostProcessMorpheus::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_DRAW_EVENT(Context.RHICmdList, PostProcessMorpheus);
	const FPooledRenderTargetDesc* InputDesc = GetInputDesc(ePId_Input0);

	if(!InputDesc)
	{
		// input is not hooked up correctly
		return;
	}

	const FViewInfo& View = Context.View;
	const FSceneViewFamily& ViewFamily = *(View.Family);
	
	FIntRect SrcRect = View.ViewRect;

	// Hard coding the output dimensions.
	// Most VR pathways can send whatever resolution to the api, and it will handle scaling, but here
	// the output is just regular windows desktop, so we need it to be the right size regardless of pixel density.
	FIntRect DestRect(0, 0, 960, 1080);
	if (View.StereoPass == eSSP_RIGHT_EYE)
	{
		DestRect.Min.X += 960;
		DestRect.Max.X += 960;
	}
	
	FIntPoint SrcSize = InputDesc->Extent;

	const FSceneRenderTargetItem& DestRenderTarget = PassOutputs[0].RequestSurface(Context);

	// Set the view family's render target/viewport.
	FRHIRenderPassInfo RPInfo(DestRenderTarget.TargetableTexture, ERenderTargetActions::Load_Store);
	Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("MorpheusPostProcess"));
	{
		Context.SetViewportAndCallRHI(DestRect);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

#if defined(MORPHEUS_ENGINE_DISTORTION) && MORPHEUS_ENGINE_DISTORTION
		TShaderMapRef<FPostProcessMorpheusVS> VertexShader(Context.GetShaderMap());
		TShaderMapRef<FPostProcessMorpheusPS> PixelShader(Context.GetShaderMap());

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

		FMatrix QuadTexTransform;
		FMatrix QuadPosTransform = FMatrix::Identity;

		PixelShader->SetPS(Context.RHICmdList, Context, SrcRect, SrcSize, View.StereoPass, QuadTexTransform);

		// Draw a quad mapping scene color to the view's render target
		DrawTransformedRectangle(
			Context.RHICmdList,
			0, 0,
			DestRect.Width(), DestRect.Height(),
			QuadPosTransform,
			SrcRect.Min.X, SrcRect.Min.Y,
			SrcRect.Width(), SrcRect.Height(),
			QuadTexTransform,
			DestRect.Size(),
			SrcSize
		);
#elif PLATFORM_PS4
		checkf(false, TEXT("PS4 uses SDK distortion."));
#else
		checkf(false, TEXT("Unsupported path.  Morpheus should be disabled."));
#endif
	}
	Context.RHICmdList.EndRenderPass();
	Context.RHICmdList.CopyToResolveTarget(DestRenderTarget.TargetableTexture, DestRenderTarget.ShaderResourceTexture, FResolveParams());
}

FPooledRenderTargetDesc FRCPassPostProcessMorpheus::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret = GetInput(static_cast<EPassInputId>(0))->GetOutput()->RenderTargetDesc;

	Ret.NumSamples = 1;	// no MSAA
	Ret.Reset();
	Ret.DebugName = TEXT("Morpheus");

	return Ret;
}

#endif // MORPHEUS_ENGINE_DISTORTION
