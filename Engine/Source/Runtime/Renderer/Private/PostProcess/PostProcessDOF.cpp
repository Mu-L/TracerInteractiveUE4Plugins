// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessDOF.cpp: Post process Depth of Field implementation.
=============================================================================*/

#include "PostProcess/PostProcessDOF.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "PostProcess/SceneRenderTargets.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PostProcess/PostProcessBokehDOF.h"
#include "SceneRenderTargetParameters.h"
#include "PostProcess/PostProcessing.h"
#include "ClearQuad.h"
#include "PipelineStateCache.h"

/** Encapsulates the DOF setup pixel shader. */
// @param FarBlur 0:off, 1:on
// @param NearBlur 0:off, 1:on, 2:on with Vignette
template <uint32 FarBlur, uint32 NearBlur>
class FPostProcessDOFSetupPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessDOFSetupPS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters,OutEnvironment);
		OutEnvironment.SetDefine(TEXT("MOBILE_SHADING"), IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4) ? 0u : 1u);
		OutEnvironment.SetDefine(TEXT("NEAR_BLUR"), (uint32)(NearBlur >= 1));
		OutEnvironment.SetDefine(TEXT("DOF_VIGNETTE"), (uint32)(NearBlur == 2));
		OutEnvironment.SetDefine(TEXT("MRT_COUNT"), FarBlur + (NearBlur > 0));
	}

	/** Default constructor. */
	FPostProcessDOFSetupPS() {}

public:
	FPostProcessPassParameters PostprocessParameter;
	FSceneTextureShaderParameters SceneTextureParameters;
	FShaderParameter DepthOfFieldParams;
	
	/** Initialization constructor. */
	FPostProcessDOFSetupPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
		SceneTextureParameters.Bind(Initializer);
		DepthOfFieldParams.Bind(Initializer.ParameterMap,TEXT("DepthOfFieldParams"));
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << PostprocessParameter << SceneTextureParameters << DepthOfFieldParams;
		return bShaderHasOutdatedParameters;
	}

	template <typename TRHICmdList>
	void SetParameters(TRHICmdList& RHICmdList, const FRenderingCompositePassContext& Context)
	{
		FRHIPixelShader* ShaderRHI = GetPixelShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);

		if (Context.GetFeatureLevel() < ERHIFeatureLevel::SM4)
		{
			// Trying bilin here to attempt to alleviate some issues with 1/4 res input...
			PostprocessParameter.SetPS(RHICmdList, ShaderRHI, Context, TStaticSamplerState<SF_Bilinear, AM_Border, AM_Border, AM_Clamp>::GetRHI());
		}
		else
		{
			PostprocessParameter.SetPS(RHICmdList, ShaderRHI, Context, TStaticSamplerState<SF_Point, AM_Border, AM_Border, AM_Clamp>::GetRHI());
		}

		SceneTextureParameters.Set(RHICmdList, ShaderRHI, Context.View.FeatureLevel, ESceneTextureSetupMode::All);

		{
			FVector4 DepthOfFieldParamValues[2];

			FRCPassPostProcessDOFSetup::ComputeDepthOfFieldParams(Context, DepthOfFieldParamValues);

			SetShaderValueArray(RHICmdList, ShaderRHI, DepthOfFieldParams, DepthOfFieldParamValues, 2);
		}
	}

	static const TCHAR* GetSourceFilename()
	{
		return TEXT("/Engine/Private/PostProcessDOF.usf");
	}

	static const TCHAR* GetFunctionName()
	{
		return TEXT("SetupPS");
	}
};

// #define avoids a lot of code duplication
#define VARIATION1(A, B) typedef FPostProcessDOFSetupPS<A, B> FPostProcessDOFSetupPS##A##B; \
	IMPLEMENT_SHADER_TYPE2(FPostProcessDOFSetupPS##A##B, SF_Pixel);

	VARIATION1(0, 1) VARIATION1(0, 2)
	VARIATION1(1, 0) VARIATION1(1, 1) VARIATION1(1, 2)
	
#undef VARIATION1

// @param FarBlur 0:off, 1:on
// @param NearBlur 0:off, 1:on, 2:on with Vignette
template <uint32 FarBlur, uint32 NearBlur>
static FShader* SetDOFShaderTempl(const FRenderingCompositePassContext& Context)
{
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	// set the state
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	TShaderMapRef<FPostProcessVS> VertexShader(Context.GetShaderMap());
	TShaderMapRef<FPostProcessDOFSetupPS<FarBlur, NearBlur> > PixelShader(Context.GetShaderMap());

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
	SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

	VertexShader->SetParameters(Context);
	PixelShader->SetParameters(Context.RHICmdList, Context);

	return *VertexShader;
}


void FRCPassPostProcessDOFSetup::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_DRAW_EVENT(Context.RHICmdList, DOFSetup);

	const FPooledRenderTargetDesc* InputDesc = GetInputDesc(ePId_Input0);

	if (!InputDesc)
	{
		// input is not hooked up correctly
		return;
	}

	uint32 NumRenderTargets = (bNearBlur && bFarBlur) ? 2 : 1;

	const FViewInfo& View = Context.View;
	const FSceneViewFamily& ViewFamily = *(View.Family);

	const auto FeatureLevel = Context.GetFeatureLevel();
	auto ShaderMap = Context.GetShaderMap();

	FIntPoint SrcSize = InputDesc->Extent;
	FIntPoint DestSize = PassOutputs[0].RenderTargetDesc.Extent;

	// e.g. 4 means the input texture is 4x smaller than the buffer size
	uint32 ScaleFactor = FSceneRenderTargets::Get(Context.RHICmdList).GetBufferSizeXY().X / SrcSize.X;

	FIntRect SrcRect = View.ViewRect / ScaleFactor;
	FIntRect DestRect = SrcRect / 2;

	const FSceneRenderTargetItem& DestRenderTarget0 = PassOutputs[0].RequestSurface(Context);
	const FSceneRenderTargetItem& DestRenderTarget1 = (NumRenderTargets == 2) ? PassOutputs[1].RequestSurface(Context) : FSceneRenderTargetItem();

	// Set the view family's render target/viewport.
	FRHITexture* RenderTargets[2] =
	{
		DestRenderTarget0.TargetableTexture,
		DestRenderTarget1.TargetableTexture
	};
	
	ERenderTargetActions LoadStoreAction = ERenderTargetActions::Load_Store;

	//@todo Ronin find a way to use the same codepath for all platforms.
	const EShaderPlatform ShaderPlatform = GShaderPlatformForFeatureLevel[Context.GetFeatureLevel()];
	if (IsVulkanMobilePlatform(ShaderPlatform))
	{
		LoadStoreAction = ERenderTargetActions::Clear_Store;
	}

	FRHIRenderPassInfo RPInfo(NumRenderTargets, RenderTargets, LoadStoreAction);
	Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("DOFSetup"));
	{
		if (View.StereoPass == eSSP_FULL)
		{
			FLinearColor ClearColors[2] =
			{
				FLinearColor(0, 0, 0, 0),
				FLinearColor(0, 0, 0, 0)
			};
			// is optimized away if possible (RT size=view size, )
			DrawClearQuadMRT(Context.RHICmdList, true, NumRenderTargets, ClearColors, false, 0, false, 0, DestSize, DestRect);
		}

		Context.SetViewportAndCallRHI(DestRect.Min.X, DestRect.Min.Y, 0.0f, DestRect.Max.X + 1, DestRect.Max.Y + 1, 1.0f);

		const float DOFVignetteSize = FMath::Max(0.0f, View.FinalPostProcessSettings.DepthOfFieldVignetteSize);

		// todo: test is conservative, with bad content we would waste a bit of performance
		const bool bDOFVignette = bNearBlur && DOFVignetteSize < 200.0f;

		// 0:off, 1:on, 2:on with Vignette
		uint32 NearBlur = 0;
		{
			if (bNearBlur)
			{
				NearBlur = (DOFVignetteSize < 200.0f) ? 2 : 1;
			}
		}

		FShader* VertexShader = 0;

		if (bFarBlur)
		{
			switch (NearBlur)
			{
			case 0: VertexShader = SetDOFShaderTempl<1, 0>(Context); break;
			case 1: VertexShader = SetDOFShaderTempl<1, 1>(Context); break;
			case 2: VertexShader = SetDOFShaderTempl<1, 2>(Context); break;
			default: check(!"Invalid NearBlur input");
			}
		}
		else
		{
			switch (NearBlur)
			{
			case 0: check(!"Internal error: In this case we should not run the pass"); break;
			case 1: VertexShader = SetDOFShaderTempl<0, 1>(Context); break;
			case 2: VertexShader = SetDOFShaderTempl<0, 2>(Context); break;
			default: check(!"Invalid NearBlur input");
			}
		}

		DrawPostProcessPass(
			Context.RHICmdList,
			0, 0,
			DestRect.Width() + 1, DestRect.Height() + 1,
			SrcRect.Min.X, SrcRect.Min.Y,
			SrcRect.Width() + 1, SrcRect.Height() + 1,
			DestRect.Size() + FIntPoint(1, 1),
			SrcSize,
			VertexShader,
			View.StereoPass,
			Context.HasHmdMesh(),
			EDRF_UseTriangleOptimization);
	}
	Context.RHICmdList.EndRenderPass();
	Context.RHICmdList.CopyToResolveTarget(DestRenderTarget0.TargetableTexture, DestRenderTarget0.ShaderResourceTexture, FResolveParams());

	if (DestRenderTarget1.TargetableTexture)
	{
		Context.RHICmdList.CopyToResolveTarget(DestRenderTarget1.TargetableTexture, DestRenderTarget1.ShaderResourceTexture, FResolveParams());
	}

	// #todo-rco: needed to avoid multiple resolves clearing the RT with VK.
	// #todo mattc this is probably busted now since the resolves previously happened after this SetRenderTarget.
	UnbindRenderTargets(Context.RHICmdList);
}

FPooledRenderTargetDesc FRCPassPostProcessDOFSetup::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret = GetInput(ePId_Input0)->GetOutput()->RenderTargetDesc;

//	Ret.Extent = FIntPoint::DivideAndRoundUp(ret.Extent, 2);
	Ret.Extent /= 2;
	Ret.Extent.X = FMath::Max(1, Ret.Extent.X);
	Ret.Extent.Y = FMath::Max(1, Ret.Extent.Y);

	Ret.Reset();
	Ret.TargetableFlags &= ~(uint32)TexCreate_UAV;
	Ret.TargetableFlags |= TexCreate_RenderTargetable;
	Ret.AutoWritable = false;
	Ret.DebugName = (InPassOutputId == ePId_Output0) ? TEXT("DOFSetup0") : TEXT("DOFSetup1");

	// more precision for additive blending and we need the alpha channel
	Ret.Format = PF_FloatRGBA;

	Ret.ClearValue = FClearValueBinding(FLinearColor(0,0,0,0));

	return Ret;
}




/** Encapsulates the DOF setup pixel shader. */
// @param FarBlur 0:off, 1:on
// @param NearBlur 0:off, 1:on
template <uint32 FarBlur, uint32 NearBlur, uint32 SeparateTranslucency>
class FPostProcessDOFRecombinePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessDOFRecombinePS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters,OutEnvironment);
		OutEnvironment.SetDefine(TEXT("FAR_BLUR"), FarBlur);
		OutEnvironment.SetDefine(TEXT("NEAR_BLUR"), NearBlur);
		OutEnvironment.SetDefine(TEXT("SEPARATE_TRANSLUCENCY"), SeparateTranslucency);
		OutEnvironment.SetDefine(TEXT("MOBILE_SHADING"), IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4) ? 0u : 1u);
	}

	/** Default constructor. */
	FPostProcessDOFRecombinePS() {}

public:
	FPostProcessPassParameters PostprocessParameter;
	FSceneTextureShaderParameters SceneTextureParameters;
	FShaderParameter DepthOfFieldUVLimit;

	/** Initialization constructor. */
	FPostProcessDOFRecombinePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
		SceneTextureParameters.Bind(Initializer);
		DepthOfFieldUVLimit.Bind(Initializer.ParameterMap,TEXT("DepthOfFieldUVLimit"));
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << PostprocessParameter << SceneTextureParameters << DepthOfFieldUVLimit;
		return bShaderHasOutdatedParameters;
	}

	void SetParameters(const FRenderingCompositePassContext& Context)
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(Context.RHICmdList);
		FRHIPixelShader* ShaderRHI = GetPixelShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);

		SceneTextureParameters.Set(Context.RHICmdList, ShaderRHI, Context.View.FeatureLevel, ESceneTextureSetupMode::All);
		PostprocessParameter.SetPS(Context.RHICmdList, ShaderRHI, Context, TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(), eFC_0001);
		
		// Compute out of bounds UVs in the source texture.
		FVector4 Bounds;
		Bounds.X = (((float)((Context.View.ViewRect.Min.X + 1) & (~1))) + 3.0f) / ((float)(SceneContext.GetBufferSizeXY().X));
		Bounds.Y = (((float)((Context.View.ViewRect.Min.Y + 1) & (~1))) + 3.0f) / ((float)(SceneContext.GetBufferSizeXY().Y));
		Bounds.Z = (((float)(Context.View.ViewRect.Max.X & (~1))) - 3.0f) / ((float)(SceneContext.GetBufferSizeXY().X));
		Bounds.W = (((float)(Context.View.ViewRect.Max.Y & (~1))) - 3.0f) / ((float)(SceneContext.GetBufferSizeXY().Y));

		SetShaderValue(Context.RHICmdList, ShaderRHI, DepthOfFieldUVLimit, Bounds);
	}
	
	static const TCHAR* GetSourceFilename()
	{
		return TEXT("/Engine/Private/PostProcessDOF.usf");
	}

	static const TCHAR* GetFunctionName()
	{
		return TEXT("MainRecombinePS");
	}
};

// #define avoids a lot of code duplication
#define VARIATION1(A, B, C) typedef FPostProcessDOFRecombinePS<A, B, C> FPostProcessDOFRecombinePS##A##B##C; \
	IMPLEMENT_SHADER_TYPE2(FPostProcessDOFRecombinePS##A##B##C, SF_Pixel);

	VARIATION1(0, 1, 0) VARIATION1(1, 0, 0) VARIATION1(1, 1, 0)
	VARIATION1(0, 1, 1) VARIATION1(1, 0, 1) VARIATION1(1, 1, 1)
	
#undef VARIATION1

// @param FarBlur 0:off, 1:on
// @param NearBlur 0:off, 1:on, 2:on with Vignette
template <uint32 FarBlur, uint32 NearBlur, uint32 SeparateTranslucency>
static FShader* SetDOFRecombineShaderTempl(const FRenderingCompositePassContext& Context)
{
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	TShaderMapRef<FPostProcessVS> VertexShader(Context.GetShaderMap());
	TShaderMapRef<FPostProcessDOFRecombinePS<FarBlur, NearBlur, SeparateTranslucency> > PixelShader(Context.GetShaderMap());

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
	SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

	VertexShader->SetParameters(Context);
	PixelShader->SetParameters(Context);

	return *VertexShader;
}

template <uint32 FarBlur, uint32 NearBlur>
static FShader* SetDOFRecombineShaderTempl(const FRenderingCompositePassContext& Context, bool bSeparateTranslucency)
{
	if (bSeparateTranslucency)
	{
		return SetDOFRecombineShaderTempl<FarBlur, NearBlur, 1u>(Context);
	}

	return SetDOFRecombineShaderTempl<FarBlur, NearBlur, 0u>(Context);
}

void FRCPassPostProcessDOFRecombine::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_DRAW_EVENT(Context.RHICmdList, DOFRecombine);

	// Get the Far or Near RTDesc
	const FPooledRenderTargetDesc* InputDesc = GetInputDesc(ePId_Input1);
	
	if(!InputDesc)
	{
		InputDesc = GetInputDesc(ePId_Input2);
	}

	check(InputDesc);

	const FViewInfo& View = Context.View;

	const auto FeatureLevel = Context.GetFeatureLevel();
	auto ShaderMap = Context.GetShaderMap();

	FIntPoint TexSize = InputDesc->Extent;

	// usually 1, 2, 4 or 8
	uint32 ScaleToFullRes = FMath::DivideAndRoundUp(FSceneRenderTargets::Get(Context.RHICmdList).GetBufferSizeXY().X , TexSize.X);

	FIntRect HalfResViewRect = View.ViewRect / ScaleToFullRes;

	const FSceneRenderTargetItem& DestRenderTarget = PassOutputs[0].RequestSurface(Context);

	// Set the view family's render target/viewport.

	//@todo Ronin find a way to use the same codepath for all platforms.
	const EShaderPlatform ShaderPlatform = GShaderPlatformForFeatureLevel[Context.GetFeatureLevel()];
	ERenderTargetActions LoadStoreAction = ERenderTargetActions::Load_Store;
	if (IsVulkanMobilePlatform(ShaderPlatform))
	{
		LoadStoreAction = ERenderTargetActions::Clear_Store;
	}
	else if (View.StereoPass == eSSP_FULL)
	{
		// #todo-renderpasses a possible optimization here is to use DontLoad since we'll clear immediately.
		LoadStoreAction = ERenderTargetActions::Load_Store;
	}

	FRHIRenderPassInfo RPInfo(DestRenderTarget.TargetableTexture, LoadStoreAction);
	Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("DOFRecombine"));
	{
		if (View.StereoPass == eSSP_FULL)
		{
			// is optimized away if possible (RT size=view size, )
			DrawClearQuad(Context.RHICmdList, true, FLinearColor::Black, false, 0, false, 0, PassOutputs[0].RenderTargetDesc.Extent, View.ViewRect);
		}
		Context.SetViewportAndCallRHI(View.ViewRect);

		bool bFarBlur = GetInputDesc(ePId_Input1) != 0;
		bool bNearBlur = GetInputDesc(ePId_Input2) != 0;
		bool bSeparateTranslucency = GetInputDesc(ePId_Input3) != 0;

		FShader* VertexShader = 0;

		if (bFarBlur)
		{
			if (bNearBlur)
			{
				VertexShader = SetDOFRecombineShaderTempl<1, 1>(Context, bSeparateTranslucency);
			}
			else
			{
				VertexShader = SetDOFRecombineShaderTempl<1, 0>(Context, bSeparateTranslucency);
			}
		}
		else
		{
			VertexShader = SetDOFRecombineShaderTempl<0, 1>(Context, bSeparateTranslucency);
		}

		DrawPostProcessPass(
			Context.RHICmdList,
			0, 0,
			View.ViewRect.Width(), View.ViewRect.Height(),
			HalfResViewRect.Min.X, HalfResViewRect.Min.Y,
			HalfResViewRect.Width(), HalfResViewRect.Height(),
			View.ViewRect.Size(),
			TexSize,
			VertexShader,
			View.StereoPass,
			Context.HasHmdMesh(),
			EDRF_UseTriangleOptimization);
	}
	Context.RHICmdList.EndRenderPass();
	Context.RHICmdList.CopyToResolveTarget(DestRenderTarget.TargetableTexture, DestRenderTarget.ShaderResourceTexture, FResolveParams());
}

FPooledRenderTargetDesc FRCPassPostProcessDOFRecombine::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret = GetInput(ePId_Input0)->GetOutput()->RenderTargetDesc;

	Ret.Reset();
	Ret.AutoWritable = false;
	Ret.DebugName = TEXT("DOFRecombine");

	Ret.ClearValue = FClearValueBinding(FLinearColor::Black);

	return Ret;
}

// static
void FRCPassPostProcessDOFSetup::ComputeDepthOfFieldParams(const FRenderingCompositePassContext& Context, FVector4 Out[2])
{
	// border between front and back layer as we don't use viewports (only possible with GS)
	const uint32 SafetyBorder = 40;

	uint32 FullRes = FSceneRenderTargets::Get(Context.RHICmdList).GetBufferSizeXY().Y;
	uint32 HalfRes = FMath::DivideAndRoundUp(FullRes, (uint32)2);
	uint32 BokehLayerSizeY = HalfRes * 2 + SafetyBorder;
	float SkyFocusDistance = Context.View.FinalPostProcessSettings.DepthOfFieldSkyFocusDistance;
	
	// *2 to go to account for Radius/Diameter, 100 for percent
	float DepthOfFieldVignetteSize = FMath::Max(0.0f, Context.View.FinalPostProcessSettings.DepthOfFieldVignetteSize / 100.0f * 2);
	// doesn't make much sense to expose this property as the effect is very non linear and it would cost some performance to fix that
	float DepthOfFieldVignetteFeather = 10.0f / 100.0f;
	float DepthOfFieldVignetteMul = 1.0f / DepthOfFieldVignetteFeather;
	float DepthOfFieldVignetteAdd = (0.5f - DepthOfFieldVignetteSize) * DepthOfFieldVignetteMul;
	Out[0] = FVector4(
		(SkyFocusDistance > 0) ? SkyFocusDistance : 100000000.0f,			// very large if <0 to not mask out skybox, can be optimized to disable feature completely
		DepthOfFieldVignetteMul,
		DepthOfFieldVignetteAdd,
		Context.View.FinalPostProcessSettings.DepthOfFieldOcclusion);
	FIntPoint ViewSize = Context.View.ViewRect.Size();
	
	// Scale and offset to put two views in one texture with safety border
	float UsedYDivTextureY = HalfRes / (float)BokehLayerSizeY;
	float YOffsetInPixel = HalfRes + SafetyBorder;
	float YOffsetInUV = (HalfRes + SafetyBorder) / (float)BokehLayerSizeY;
	Out[1] = FVector4(0.0f, YOffsetInUV, UsedYDivTextureY, YOffsetInPixel);
}
