// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessBusyWait.cpp: Post processing busy wait implementation. For Debugging GPU timing.
=============================================================================*/

#include "PostProcess/PostProcessBusyWait.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "PostProcess/SceneRenderTargets.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PostProcess/PostProcessing.h"
#include "PipelineStateCache.h"

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
static TAutoConsoleVariable<float> CVarSetGPUBusyWait(
	TEXT("r.GPUBusyWait"),
	0.0f,
	TEXT("<=0:off, >0: keep the GPU busy with n units of some fixed amount of work, independent on the resolution\n")
	TEXT("This can be useful to make GPU timing experiments. The value should roughly represent milliseconds.\n")
	TEXT("Clamped at 500."),
	ECVF_Cheat | ECVF_RenderThreadSafe);
#endif

/** Encapsulates the post processing busy wait pixel shader. */
class FPostProcessBusyWaitPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessBusyWaitPS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	/** Default constructor. */
	FPostProcessBusyWaitPS() {}

public:
	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter);
	LAYOUT_FIELD(FShaderParameter, GPUBusyWait);

	/** Initialization constructor. */
	FPostProcessBusyWaitPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
		GPUBusyWait.Bind(Initializer.ParameterMap,TEXT("GPUBusyWait"));
	}

	template <typename TRHICmdList>
	void SetPS(TRHICmdList& RHICmdList, const FRenderingCompositePassContext& Context)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);

		PostprocessParameter.SetPS(RHICmdList, ShaderRHI, Context, TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		{
			uint32 PixelCount = Context.View.ViewRect.Size().X * Context.View.ViewRect.Size().Y;

			float CVarValue = FMath::Clamp(CVarSetGPUBusyWait.GetValueOnRenderThread(), 0.0f, 500.0f);

			// multiply with large number to get more human friendly number range
			// calibrated on a NV580 to be roughly a millisecond
			// divide by viewport pixel count
			uint32 Value = (uint32)(CVarValue * 1000000000.0 / 6.12 / PixelCount);

			SetShaderValue(RHICmdList, ShaderRHI, GPUBusyWait, Value);
		}
#endif
	}
};

IMPLEMENT_SHADER_TYPE(,FPostProcessBusyWaitPS,TEXT("/Engine/Private/PostProcessBusyWait.usf"),TEXT("MainPS"),SF_Pixel);

void FRCPassPostProcessBusyWait::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_DRAW_EVENT(Context.RHICmdList, BusyWait);
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(Context.RHICmdList);

	SceneContext.BeginRenderingLightAttenuation(Context.RHICmdList);
	
	const FSceneRenderTargetItem& DestRenderTarget = SceneContext.GetLightAttenuation()->GetRenderTargetItem();

	FRHIRenderPassInfo RPInfo(DestRenderTarget.TargetableTexture, ERenderTargetActions::Load_Store);
	Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("PostProcessBusyWait"));
	{
		const FViewInfo& View = Context.View;

		FIntRect SrcRect = View.ViewRect;
		FIntRect DestRect = View.UnscaledViewRect;

		Context.SetViewportAndCallRHI(DestRect);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		TShaderMapRef<FPostProcessVS> VertexShader(Context.GetShaderMap());
		TShaderMapRef<FPostProcessBusyWaitPS> PixelShader(Context.GetShaderMap());

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

		PixelShader->SetPS(Context.RHICmdList, Context);

		// Draw a quad mapping scene color to the view's render target
		DrawRectangle(
			Context.RHICmdList,
			0, 0,
			DestRect.Width(), DestRect.Height(),
			SrcRect.Min.X, SrcRect.Min.Y,
			SrcRect.Width(), SrcRect.Height(),
			DestRect.Size(),
			SrcRect.Size(),
			VertexShader,
			EDRF_UseTriangleOptimization);
	}
	Context.RHICmdList.EndRenderPass();
	Context.RHICmdList.CopyToResolveTarget(DestRenderTarget.TargetableTexture, DestRenderTarget.ShaderResourceTexture, FResolveParams());

	SceneContext.SetLightAttenuation(0);
}

FPooledRenderTargetDesc FRCPassPostProcessBusyWait::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret;

	Ret.DebugName = TEXT("BusyWait");

	return FPooledRenderTargetDesc();
}

bool FRCPassPostProcessBusyWait::IsPassRequired()
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.GPUBusyWait"));

	float Value = CVar->GetValueOnAnyThread();

	return Value > 0;
#else
	return false;
#endif
}
