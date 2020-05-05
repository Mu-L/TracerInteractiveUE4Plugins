// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	
=============================================================================*/

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Misc/MemStack.h"
#include "EngineDefines.h"
#include "RHIDefinitions.h"
#include "RHI.h"
#include "RenderingThread.h"
#include "Engine/Scene.h"
#include "SceneInterface.h"
#include "LegacyScreenPercentageDriver.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "RHIStaticStates.h"
#include "SceneView.h"
#include "Shader.h"
#include "TextureResource.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneCaptureComponentCube.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTargetCube.h"
#include "PostProcess/SceneRenderTargets.h"
#include "GlobalShader.h"
#include "SceneRenderTargetParameters.h"
#include "SceneRendering.h"
#include "DeferredShadingRenderer.h"
#include "ScenePrivate.h"
#include "PostProcess/SceneFilterRendering.h"
#include "ScreenRendering.h"
#include "MobileSceneCaptureRendering.h"
#include "ClearQuad.h"
#include "PipelineStateCache.h"
#include "RendererModule.h"
#include "Rendering/MotionVectorSimulation.h"
#include "SceneViewExtension.h"
#include "GenerateMips.h"

const TCHAR* GShaderSourceModeDefineName[] =
{
	TEXT("SOURCE_MODE_SCENE_COLOR_AND_OPACITY"),
	TEXT("SOURCE_MODE_SCENE_COLOR_NO_ALPHA"),
	nullptr,
	TEXT("SOURCE_MODE_SCENE_COLOR_SCENE_DEPTH"),
	TEXT("SOURCE_MODE_SCENE_DEPTH"),
	TEXT("SOURCE_MODE_DEVICE_DEPTH"),
	TEXT("SOURCE_MODE_NORMAL"),
	TEXT("SOURCE_MODE_BASE_COLOR"),
	nullptr
};

static TAutoConsoleVariable<int32> CVarEnableViewExtensionsForSceneCapture(
	TEXT("r.SceneCapture.EnableViewExtensions"),
	0,
	TEXT("Whether to enable view extensions when doing scene capture.\n")
	TEXT("0: Disable view extensions (default).\n")
	TEXT("1: Enable view extensions.\n"),
	ECVF_Default);

/**
 * A pixel shader for capturing a component of the rendered scene for a scene capture.
 */
template<ESceneCaptureSource CaptureSource>
class TSceneCapturePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TSceneCapturePS,Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) 
	{ 
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		const TCHAR* DefineName = GShaderSourceModeDefineName[CaptureSource];
		if (DefineName)
		{
			OutEnvironment.SetDefine(DefineName, 1);
		}
	}

	TSceneCapturePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
		SceneTextureParameters.Bind(Initializer);
	}
	TSceneCapturePS() {}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundPixelShader(), View.ViewUniformBuffer);
		SceneTextureParameters.Set(RHICmdList, RHICmdList.GetBoundPixelShader(), View.FeatureLevel, ESceneTextureSetupMode::All);
	}

private:
	LAYOUT_FIELD(FSceneTextureShaderParameters, SceneTextureParameters)
};

IMPLEMENT_SHADER_TYPE(template<>, TSceneCapturePS<SCS_SceneColorHDR>, TEXT("/Engine/Private/SceneCapturePixelShader.usf"), TEXT("Main"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, TSceneCapturePS<SCS_SceneColorHDRNoAlpha>, TEXT("/Engine/Private/SceneCapturePixelShader.usf"), TEXT("Main"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>,TSceneCapturePS<SCS_SceneColorSceneDepth>,TEXT("/Engine/Private/SceneCapturePixelShader.usf"),TEXT("Main"),SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>,TSceneCapturePS<SCS_SceneDepth>,TEXT("/Engine/Private/SceneCapturePixelShader.usf"),TEXT("Main"),SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, TSceneCapturePS<SCS_DeviceDepth>, TEXT("/Engine/Private/SceneCapturePixelShader.usf"), TEXT("Main"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>,TSceneCapturePS<SCS_Normal>,TEXT("/Engine/Private/SceneCapturePixelShader.usf"),TEXT("Main"),SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>,TSceneCapturePS<SCS_BaseColor>,TEXT("/Engine/Private/SceneCapturePixelShader.usf"),TEXT("Main"),SF_Pixel);

class FODSCapturePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FODSCapturePS, Global);
public:

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	FODSCapturePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		LeftEyeTexture.Bind(Initializer.ParameterMap, TEXT("LeftEyeTexture"));
		RightEyeTexture.Bind(Initializer.ParameterMap, TEXT("RightEyeTexture"));
		LeftEyeTextureSampler.Bind(Initializer.ParameterMap, TEXT("LeftEyeTextureSampler"));
		RightEyeTextureSampler.Bind(Initializer.ParameterMap, TEXT("RightEyeTextureSampler"));
	}

	FODSCapturePS() {}

	void SetParameters(FRHICommandList& RHICmdList, const FTextureRHIRef InLeftEyeTexture, const FTextureRHIRef InRightEyeTexture)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();
		
		SetTextureParameter(
			RHICmdList,
			ShaderRHI,
			LeftEyeTexture,
			LeftEyeTextureSampler,
			TStaticSamplerState<SF_Bilinear>::GetRHI(),
			InLeftEyeTexture);

		SetTextureParameter(
			RHICmdList,
			ShaderRHI,
			RightEyeTexture,
			RightEyeTextureSampler,
			TStaticSamplerState<SF_Bilinear>::GetRHI(),
			InRightEyeTexture);
	}

	LAYOUT_FIELD(FShaderResourceParameter, LeftEyeTexture)
	LAYOUT_FIELD(FShaderResourceParameter, RightEyeTexture)
	LAYOUT_FIELD(FShaderResourceParameter, LeftEyeTextureSampler)
	LAYOUT_FIELD(FShaderResourceParameter, RightEyeTextureSampler)
};

IMPLEMENT_SHADER_TYPE(, FODSCapturePS, TEXT("/Engine/Private/ODSCapture.usf"), TEXT("MainPS"), SF_Pixel);

void FDeferredShadingSceneRenderer::CopySceneCaptureComponentToTarget(FRHICommandListImmediate& RHICmdList)
{
	ESceneCaptureSource SceneCaptureSource = ViewFamily.SceneCaptureSource;

	if (IsAnyForwardShadingEnabled(ViewFamily.GetShaderPlatform()) && (SceneCaptureSource == SCS_Normal || SceneCaptureSource == SCS_BaseColor))
	{
		SceneCaptureSource = SCS_SceneColorHDR;
	}

	if (SceneCaptureSource != SCS_FinalColorLDR && SceneCaptureSource != SCS_FinalColorHDR && SceneCaptureSource != SCS_FinalToneCurveHDR)
	{
		SCOPED_DRAW_EVENT(RHICmdList, CaptureSceneComponent);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			FViewInfo& View = Views[ViewIndex];

			FRHIRenderPassInfo RPInfo(ViewFamily.RenderTarget->GetRenderTargetTexture(), ERenderTargetActions::DontLoad_Store);
			RHICmdList.BeginRenderPass(RPInfo, TEXT("ViewCapture"));
			{
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

				if (SceneCaptureSource == SCS_SceneColorHDR && ViewFamily.SceneCaptureCompositeMode == SCCM_Composite)
				{
					// Blend with existing render target color. Scene capture color is already pre-multiplied by alpha.
					GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_SourceAlpha, BO_Add, BF_Zero, BF_SourceAlpha>::GetRHI();
				}
				else if (SceneCaptureSource == SCS_SceneColorHDR && ViewFamily.SceneCaptureCompositeMode == SCCM_Additive)
				{
					// Add to existing render target color. Scene capture color is already pre-multiplied by alpha.
					GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_SourceAlpha>::GetRHI();
				}
				else
				{
					GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
				}

				TShaderMapRef<FScreenVS> VertexShader(View.ShaderMap);
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.PrimitiveType = PT_TriangleList;

				if (SceneCaptureSource == SCS_SceneColorHDR)
				{
					TShaderMapRef<TSceneCapturePS<SCS_SceneColorHDR> > PixelShader(View.ShaderMap);
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

					PixelShader->SetParameters(RHICmdList, View);
				}
				else if (SceneCaptureSource == SCS_SceneColorHDRNoAlpha)
				{
					TShaderMapRef<TSceneCapturePS<SCS_SceneColorHDRNoAlpha> > PixelShader(View.ShaderMap);
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

					PixelShader->SetParameters(RHICmdList, View);
				}
				else if (SceneCaptureSource == SCS_SceneColorSceneDepth)
				{
					TShaderMapRef<TSceneCapturePS<SCS_SceneColorSceneDepth> > PixelShader(View.ShaderMap);
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

					PixelShader->SetParameters(RHICmdList, View);
				}
				else if (SceneCaptureSource == SCS_SceneDepth)
				{
					TShaderMapRef<TSceneCapturePS<SCS_SceneDepth> > PixelShader(View.ShaderMap);
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

					PixelShader->SetParameters(RHICmdList, View);
				}
				else if (ViewFamily.SceneCaptureSource == SCS_DeviceDepth)
				{
					TShaderMapRef<TSceneCapturePS<SCS_DeviceDepth> > PixelShader(View.ShaderMap);
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

					PixelShader->SetParameters(RHICmdList, View);
				}
				else if (SceneCaptureSource == SCS_Normal)
				{
					TShaderMapRef<TSceneCapturePS<SCS_Normal> > PixelShader(View.ShaderMap);
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

					PixelShader->SetParameters(RHICmdList, View);
				}
				else if (SceneCaptureSource == SCS_BaseColor)
				{
					TShaderMapRef<TSceneCapturePS<SCS_BaseColor> > PixelShader(View.ShaderMap);
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

					PixelShader->SetParameters(RHICmdList, View);
				}
				else
				{
					check(0);
				}

				VertexShader->SetParameters(RHICmdList, View.ViewUniformBuffer);

				DrawRectangle(
					RHICmdList,
					View.ViewRect.Min.X, View.ViewRect.Min.Y,
					View.ViewRect.Width(), View.ViewRect.Height(),
					View.ViewRect.Min.X, View.ViewRect.Min.Y,
					View.ViewRect.Width(), View.ViewRect.Height(),
					View.UnconstrainedViewRect.Size(),
					FSceneRenderTargets::Get(RHICmdList).GetBufferSizeXY(),
					VertexShader,
					EDRF_UseTriangleOptimization);
			}
			RHICmdList.EndRenderPass();
		} // foreach view
	}
}

static void UpdateSceneCaptureContentDeferred_RenderThread(
	FRHICommandListImmediate& RHICmdList, 
	FSceneRenderer* SceneRenderer, 
	FRenderTarget* RenderTarget, 
	FTexture* RenderTargetTexture, 
	const FString& EventName, 
	const FResolveParams& ResolveParams,
	bool bGenerateMips,
	const FGenerateMipsParams& GenerateMipsParams
	)
{
	FMemMark MemStackMark(FMemStack::Get());

	// update any resources that needed a deferred update
	FDeferredUpdateResource::UpdateResources(RHICmdList);
	{
#if WANTS_DRAW_MESH_EVENTS
		SCOPED_DRAW_EVENTF(RHICmdList, SceneCapture, TEXT("SceneCapture %s"), *EventName);
#else
		SCOPED_DRAW_EVENT(RHICmdList, UpdateSceneCaptureContent_RenderThread);
#endif

		const FRenderTarget* Target = SceneRenderer->ViewFamily.RenderTarget;

		// TODO: Could avoid the clear by replacing with dummy black system texture.
		FViewInfo& View = SceneRenderer->Views[0];

		FRHIRenderPassInfo RPInfo(Target->GetRenderTargetTexture(), ERenderTargetActions::DontLoad_Store);
		RPInfo.ResolveParameters = ResolveParams;
		TransitionRenderPassTargets(RHICmdList, RPInfo);

		RHICmdList.BeginRenderPass(RPInfo, TEXT("ClearSceneCaptureContent"));
		DrawClearQuad(RHICmdList, true, FLinearColor::Black, false, 0, false, 0, Target->GetSizeXY(), View.UnscaledViewRect);
		RHICmdList.EndRenderPass();

		// Render the scene normally
		{
			SCOPED_DRAW_EVENT(RHICmdList, RenderScene);
			SceneRenderer->Render(RHICmdList);
		}

		if (bGenerateMips)
		{
			FGenerateMips::Execute(RHICmdList, RenderTarget->GetRenderTargetTexture(), GenerateMipsParams);
		}

		// Note: When the ViewFamily.SceneCaptureSource requires scene textures (i.e. SceneCaptureSource != SCS_FinalColorLDR), the copy to RenderTarget 
		// will be done in CopySceneCaptureComponentToTarget while the GBuffers are still alive for the frame.
		RHICmdList.CopyToResolveTarget(RenderTarget->GetRenderTargetTexture(), RenderTargetTexture->TextureRHI, ResolveParams);		
	}

	FSceneRenderer::WaitForTasksClearSnapshotsAndDeleteSceneRenderer(RHICmdList, SceneRenderer);
}

static void ODSCapture_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	const FTexture* const LeftEyeTexture,
	const FTexture* const RightEyeTexture,
	FRenderTarget* const RenderTarget, 
	const ERHIFeatureLevel::Type FeatureLevel)
{
	FRHIRenderPassInfo RPInfo(RenderTarget->GetRenderTargetTexture(), ERenderTargetActions::Load_Store);
	TransitionRenderPassTargets(RHICmdList, RPInfo);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("ODSCapture"));
	{

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		const auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
		TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
		TShaderMapRef<FODSCapturePS> PixelShader(ShaderMap);
		extern TGlobalResource<FFilterVertexDeclaration> GFilterVertexDeclaration;

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		PixelShader->SetParameters(RHICmdList, LeftEyeTexture->TextureRHI->GetTextureCube(), RightEyeTexture->TextureRHI->GetTextureCube());

		const FIntPoint& TargetSize = RenderTarget->GetSizeXY();
		RHICmdList.SetViewport(0, 0, 0.0f, TargetSize.X, TargetSize.Y, 1.0f);

		DrawRectangle(
			RHICmdList,
			0, 0,
			static_cast<float>(TargetSize.X), static_cast<float>(TargetSize.Y),
			0, 0,
			TargetSize.X, TargetSize.Y,
			TargetSize,
			TargetSize,
			VertexShader,
			EDRF_UseTriangleOptimization);
	}
	RHICmdList.EndRenderPass();
}

static void UpdateSceneCaptureContent_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	FSceneRenderer* SceneRenderer,
	FRenderTarget* RenderTarget,
	FTexture* RenderTargetTexture,
	const FString& EventName,
	const FResolveParams& ResolveParams,
	bool bGenerateMips,
	const FGenerateMipsParams& GenerateMipsParams,
	const bool bDisableFlipCopyLDRGLES)
{
	FMaterialRenderProxy::UpdateDeferredCachedUniformExpressions();

	switch (SceneRenderer->Scene->GetShadingPath())
	{
		case EShadingPath::Mobile:
		{
			UpdateSceneCaptureContentMobile_RenderThread(
				RHICmdList,
				SceneRenderer,
				RenderTarget,
				RenderTargetTexture,
				EventName,
				ResolveParams,
				bGenerateMips,
				GenerateMipsParams,
				bDisableFlipCopyLDRGLES);
			break;
		}
		case EShadingPath::Deferred:
		{
			UpdateSceneCaptureContentDeferred_RenderThread(
				RHICmdList,
				SceneRenderer,
				RenderTarget,
				RenderTargetTexture,
				EventName,
				ResolveParams,
				bGenerateMips,
				GenerateMipsParams);
			break;
		}
		default:
			checkNoEntry();
			break;
	}
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UnbindRenderTargets(RHICmdList);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void BuildProjectionMatrix(FIntPoint RenderTargetSize, ECameraProjectionMode::Type ProjectionType, float FOV, float InOrthoWidth, float InNearClippingPlane, FMatrix& ProjectionMatrix)
{
	float const XAxisMultiplier = 1.0f;
	float const YAxisMultiplier = RenderTargetSize.X / (float)RenderTargetSize.Y;

	if (ProjectionType == ECameraProjectionMode::Orthographic)
	{
		check((int32)ERHIZBuffer::IsInverted);
		const float OrthoWidth = InOrthoWidth / 2.0f;
		const float OrthoHeight = InOrthoWidth / 2.0f * XAxisMultiplier / YAxisMultiplier;

		const float NearPlane = 0;
		const float FarPlane = WORLD_MAX / 8.0f;

		const float ZScale = 1.0f / (FarPlane - NearPlane);
		const float ZOffset = -NearPlane;

		ProjectionMatrix = FReversedZOrthoMatrix(
			OrthoWidth,
			OrthoHeight,
			ZScale,
			ZOffset
			);
	}
	else
	{
		if ((int32)ERHIZBuffer::IsInverted)
		{
			ProjectionMatrix = FReversedZPerspectiveMatrix(
				FOV,
				FOV,
				XAxisMultiplier,
				YAxisMultiplier,
				InNearClippingPlane,
				InNearClippingPlane
				);
		}
		else
		{
			ProjectionMatrix = FPerspectiveMatrix(
				FOV,
				FOV,
				XAxisMultiplier,
				YAxisMultiplier,
				InNearClippingPlane,
				InNearClippingPlane
				);
		}
	}
}

void SetupViewFamilyForSceneCapture(
	FSceneViewFamily& ViewFamily,
	USceneCaptureComponent* SceneCaptureComponent,
	const TArrayView<const FSceneCaptureViewInfo> Views,
	float MaxViewDistance,
	bool bCaptureSceneColor,
	bool bIsPlanarReflection,
	FPostProcessSettings* PostProcessSettings,
	float PostProcessBlendWeight,
	const AActor* ViewActor)
{
	check(!ViewFamily.GetScreenPercentageInterface());

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		const FSceneCaptureViewInfo& SceneCaptureViewInfo = Views[ViewIndex];

		FSceneViewInitOptions ViewInitOptions;
		ViewInitOptions.SetViewRectangle(SceneCaptureViewInfo.ViewRect);
		ViewInitOptions.ViewFamily = &ViewFamily;
		ViewInitOptions.ViewActor = ViewActor;
		ViewInitOptions.ViewOrigin = SceneCaptureViewInfo.ViewLocation;
		ViewInitOptions.ViewRotationMatrix = SceneCaptureViewInfo.ViewRotationMatrix;
		ViewInitOptions.BackgroundColor = FLinearColor::Black;
		ViewInitOptions.OverrideFarClippingPlaneDistance = MaxViewDistance;
		ViewInitOptions.StereoPass = SceneCaptureViewInfo.StereoPass;
		ViewInitOptions.SceneViewStateInterface = SceneCaptureComponent->GetViewState(ViewIndex);
		ViewInitOptions.ProjectionMatrix = SceneCaptureViewInfo.ProjectionMatrix;
		ViewInitOptions.LODDistanceFactor = FMath::Clamp(SceneCaptureComponent->LODDistanceFactor, .01f, 100.0f);

		if (ViewFamily.Scene->GetWorld() != nullptr && ViewFamily.Scene->GetWorld()->GetWorldSettings() != nullptr)
		{
			ViewInitOptions.WorldToMetersScale = ViewFamily.Scene->GetWorld()->GetWorldSettings()->WorldToMeters;
		}
		ViewInitOptions.StereoIPD = SceneCaptureViewInfo.StereoIPD * (ViewInitOptions.WorldToMetersScale / 100.0f);

		if (bCaptureSceneColor)
		{
			ViewFamily.EngineShowFlags.PostProcessing = 0;
			ViewInitOptions.OverlayColor = FLinearColor::Black;
		}

		FSceneView* View = new FSceneView(ViewInitOptions);

		View->bIsSceneCapture = true;
		View->bSceneCaptureUsesRayTracing = SceneCaptureComponent->bUseRayTracingIfEnabled;
		// Note: this has to be set before EndFinalPostprocessSettings
		View->bIsPlanarReflection = bIsPlanarReflection;
        // Needs to be reconfigured now that bIsPlanarReflection has changed.
		View->SetupAntiAliasingMethod();

		check(SceneCaptureComponent);
		for (auto It = SceneCaptureComponent->HiddenComponents.CreateConstIterator(); It; ++It)
		{
			// If the primitive component was destroyed, the weak pointer will return NULL.
			UPrimitiveComponent* PrimitiveComponent = It->Get();
			if (PrimitiveComponent)
			{
				View->HiddenPrimitives.Add(PrimitiveComponent->ComponentId);
			}
		}

		for (auto It = SceneCaptureComponent->HiddenActors.CreateConstIterator(); It; ++It)
		{
			AActor* Actor = *It;

			if (Actor)
			{
				for (UActorComponent* Component : Actor->GetComponents())
				{
					if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Component))
					{
						View->HiddenPrimitives.Add(PrimComp->ComponentId);
					}
				}
			}
		}

		if (SceneCaptureComponent->PrimitiveRenderMode == ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList)
		{
			View->ShowOnlyPrimitives.Emplace();

			for (auto It = SceneCaptureComponent->ShowOnlyComponents.CreateConstIterator(); It; ++It)
			{
				// If the primitive component was destroyed, the weak pointer will return NULL.
				UPrimitiveComponent* PrimitiveComponent = It->Get();
				if (PrimitiveComponent)
				{
					View->ShowOnlyPrimitives->Add(PrimitiveComponent->ComponentId);
				}
			}

			for (auto It = SceneCaptureComponent->ShowOnlyActors.CreateConstIterator(); It; ++It)
			{
				AActor* Actor = *It;

				if (Actor)
				{
					for (UActorComponent* Component : Actor->GetComponents())
					{
						if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Component))
						{
							View->ShowOnlyPrimitives->Add(PrimComp->ComponentId);
						}
					}
				}
			}
		}
		else if (SceneCaptureComponent->ShowOnlyComponents.Num() > 0 || SceneCaptureComponent->ShowOnlyActors.Num() > 0)
		{
			static bool bWarned = false;

			if (!bWarned)
			{
				UE_LOG(LogRenderer, Log, TEXT("Scene Capture has ShowOnlyComponents or ShowOnlyActors ignored by the PrimitiveRenderMode setting! %s"), *SceneCaptureComponent->GetPathName());
				bWarned = true;
			}
		}

		ViewFamily.Views.Add(View);

		View->StartFinalPostprocessSettings(SceneCaptureViewInfo.ViewLocation);
		View->OverridePostProcessSettings(*PostProcessSettings, PostProcessBlendWeight);
		View->EndFinalPostprocessSettings(ViewInitOptions);
	}
}

static FSceneRenderer* CreateSceneRendererForSceneCapture(
	FScene* Scene,
	USceneCaptureComponent* SceneCaptureComponent,
	FRenderTarget* RenderTarget,
	FIntPoint RenderTargetSize,
	const FMatrix& ViewRotationMatrix,
	const FVector& ViewLocation,
	const FMatrix& ProjectionMatrix,
	float MaxViewDistance,
	bool bCaptureSceneColor,
	FPostProcessSettings* PostProcessSettings,
	float PostProcessBlendWeight,
	const AActor* ViewActor, 
	const float StereoIPD = 0.0f)
{
	FSceneCaptureViewInfo SceneCaptureViewInfo;
	SceneCaptureViewInfo.ViewRotationMatrix = ViewRotationMatrix;
	SceneCaptureViewInfo.ViewLocation = ViewLocation;
	SceneCaptureViewInfo.ProjectionMatrix = ProjectionMatrix;
	SceneCaptureViewInfo.StereoPass = EStereoscopicPass::eSSP_FULL;
	SceneCaptureViewInfo.StereoIPD = StereoIPD;
	SceneCaptureViewInfo.ViewRect = FIntRect(0, 0, RenderTargetSize.X, RenderTargetSize.Y);

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		RenderTarget,
		Scene,
		SceneCaptureComponent->ShowFlags)
		.SetResolveScene(!bCaptureSceneColor)
		.SetRealtimeUpdate(SceneCaptureComponent->bCaptureEveryFrame || SceneCaptureComponent->bAlwaysPersistRenderingState));
	
	if (CVarEnableViewExtensionsForSceneCapture.GetValueOnAnyThread() > 0)
	{
		ViewFamily.ViewExtensions = GEngine->ViewExtensions->GatherActiveExtensions(nullptr);
	}
	
	SetupViewFamilyForSceneCapture(
		ViewFamily,
		SceneCaptureComponent,
		MakeArrayView(&SceneCaptureViewInfo, 1),
		MaxViewDistance, 
		bCaptureSceneColor,
		/* bIsPlanarReflection = */ false,
		PostProcessSettings, 
		PostProcessBlendWeight,
		ViewActor);

	// Screen percentage is still not supported in scene capture.
	ViewFamily.EngineShowFlags.ScreenPercentage = false;
	ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(
		ViewFamily, /* GlobalResolutionFraction = */ 1.0f, /* AllowPostProcessSettingsScreenPercentage = */ false));

	return FSceneRenderer::CreateSceneRenderer(&ViewFamily, nullptr);
}

void FScene::UpdateSceneCaptureContents(USceneCaptureComponent2D* CaptureComponent)
{
	check(CaptureComponent);

	if (UTextureRenderTarget2D* TextureRenderTarget = CaptureComponent->TextureTarget)
	{
		FTransform Transform = CaptureComponent->GetComponentToWorld();
		FVector ViewLocation = Transform.GetTranslation();

		// Remove the translation from Transform because we only need rotation.
		Transform.SetTranslation(FVector::ZeroVector);
		Transform.SetScale3D(FVector::OneVector);
		FMatrix ViewRotationMatrix = Transform.ToInverseMatrixWithScale();

		// swap axis st. x=z,y=x,z=y (unreal coord space) so that z is up
		ViewRotationMatrix = ViewRotationMatrix * FMatrix(
			FPlane(0, 0, 1, 0),
			FPlane(1, 0, 0, 0),
			FPlane(0, 1, 0, 0),
			FPlane(0, 0, 0, 1));
		const float FOV = CaptureComponent->FOVAngle * (float)PI / 360.0f;
		FIntPoint CaptureSize(TextureRenderTarget->GetSurfaceWidth(), TextureRenderTarget->GetSurfaceHeight());

		FMatrix ProjectionMatrix;
		if (CaptureComponent->bUseCustomProjectionMatrix)
		{
			ProjectionMatrix = CaptureComponent->CustomProjectionMatrix;
		}
		else
		{
			const float ClippingPlane = (CaptureComponent->bOverride_CustomNearClippingPlane) ? CaptureComponent->CustomNearClippingPlane : GNearClippingPlane;
			BuildProjectionMatrix(CaptureSize, CaptureComponent->ProjectionType, FOV, CaptureComponent->OrthoWidth, ClippingPlane, ProjectionMatrix);
		}

		const bool bUseSceneColorTexture = CaptureComponent->CaptureSource != SCS_FinalColorLDR &&
			CaptureComponent->CaptureSource != SCS_FinalColorHDR && CaptureComponent->CaptureSource != SCS_FinalToneCurveHDR;

		FSceneRenderer* SceneRenderer = CreateSceneRendererForSceneCapture(
			this, 
			CaptureComponent, 
			TextureRenderTarget->GameThread_GetRenderTargetResource(), 
			CaptureSize, 
			ViewRotationMatrix, 
			ViewLocation, 
			ProjectionMatrix, 
			CaptureComponent->MaxViewDistanceOverride, 
			bUseSceneColorTexture,
			&CaptureComponent->PostProcessSettings, 
			CaptureComponent->PostProcessBlendWeight,
			CaptureComponent->GetViewOwner());

		SceneRenderer->Views[0].bFogOnlyOnRenderedOpaque = CaptureComponent->bConsiderUnrenderedOpaquePixelAsFullyTranslucent;

		SceneRenderer->ViewFamily.SceneCaptureSource = CaptureComponent->CaptureSource;
		SceneRenderer->ViewFamily.SceneCaptureCompositeMode = CaptureComponent->CompositeMode;

		// Ensure that the views for this scene capture reflect any simulated camera motion for this frame
		TOptional<FTransform> PreviousTransform = FMotionVectorSimulation::Get().GetPreviousTransform(CaptureComponent);

		// Process Scene View extensions for the capture component
		{
			for (int32 Index = 0; Index < CaptureComponent->SceneViewExtensions.Num(); ++Index)
			{
				TSharedPtr<ISceneViewExtension, ESPMode::ThreadSafe> Extension = CaptureComponent->SceneViewExtensions[Index].Pin();
				if (Extension.IsValid())
				{
					if (Extension->IsActiveThisFrame(nullptr))
					{
						SceneRenderer->ViewFamily.ViewExtensions.Add(Extension.ToSharedRef());
					}
				}
				else
				{
					CaptureComponent->SceneViewExtensions.RemoveAt(Index, 1, false);
					--Index;
				}
			}

			for (const TSharedRef<ISceneViewExtension, ESPMode::ThreadSafe>& Extension : SceneRenderer->ViewFamily.ViewExtensions)
			{
				Extension->SetupViewFamily(SceneRenderer->ViewFamily);
			}
		}

		{
			FPlane ClipPlane = FPlane(CaptureComponent->ClipPlaneBase, CaptureComponent->ClipPlaneNormal.GetSafeNormal());

			for (FSceneView& View : SceneRenderer->Views)
			{
				if (PreviousTransform.IsSet())
				{
					View.PreviousViewTransform = PreviousTransform.GetValue();
				}

				View.bCameraCut = CaptureComponent->bCameraCutThisFrame;

				if (CaptureComponent->bEnableClipPlane)
				{
					View.GlobalClippingPlane = ClipPlane;
					// Jitter can't be removed completely due to the clipping plane
					View.bAllowTemporalJitter = false;
				}

				for (const TSharedRef<ISceneViewExtension, ESPMode::ThreadSafe>& Extension : SceneRenderer->ViewFamily.ViewExtensions)
				{
					Extension->SetupView(SceneRenderer->ViewFamily, View);
				}
			}
		}

		// Reset scene capture's camera cut.
		CaptureComponent->bCameraCutThisFrame = false;

		FTextureRenderTargetResource* TextureRenderTargetResource = TextureRenderTarget->GameThread_GetRenderTargetResource();

		FString EventName;
		if (!CaptureComponent->ProfilingEventName.IsEmpty())
		{
			EventName = CaptureComponent->ProfilingEventName;
		}
		else if (CaptureComponent->GetOwner())
		{
			CaptureComponent->GetOwner()->GetFName().ToString(EventName);
		}

		const bool bGenerateMips = TextureRenderTarget->bAutoGenerateMips;
		FGenerateMipsParams GenerateMipsParams{TextureRenderTarget->MipsSamplerFilter == TF_Nearest ? SF_Point : (TextureRenderTarget->MipsSamplerFilter == TF_Trilinear ? SF_Trilinear : SF_Bilinear),
			TextureRenderTarget->MipsAddressU == TA_Wrap ? AM_Wrap : (TextureRenderTarget->MipsAddressU == TA_Mirror ? AM_Mirror : AM_Clamp),
			TextureRenderTarget->MipsAddressV == TA_Wrap ? AM_Wrap : (TextureRenderTarget->MipsAddressV == TA_Mirror ? AM_Mirror : AM_Clamp)};

		const bool bDisableFlipCopyGLES = CaptureComponent->bDisableFlipCopyGLES;

		ENQUEUE_RENDER_COMMAND(CaptureCommand)(
			[SceneRenderer, TextureRenderTargetResource, EventName, bGenerateMips, GenerateMipsParams, bDisableFlipCopyGLES](FRHICommandListImmediate& RHICmdList)
			{
				UpdateSceneCaptureContent_RenderThread(RHICmdList, SceneRenderer, TextureRenderTargetResource, TextureRenderTargetResource, EventName, FResolveParams(), bGenerateMips, GenerateMipsParams, bDisableFlipCopyGLES);
			}
		);
	}
}

void FScene::UpdateSceneCaptureContents(USceneCaptureComponentCube* CaptureComponent)
{
	struct FLocal
	{
		/** Creates a transformation for a cubemap face, following the D3D cubemap layout. */
		static FMatrix CalcCubeFaceTransform(ECubeFace Face)
		{
			static const FVector XAxis(1.f, 0.f, 0.f);
			static const FVector YAxis(0.f, 1.f, 0.f);
			static const FVector ZAxis(0.f, 0.f, 1.f);

			// vectors we will need for our basis
			FVector vUp(YAxis);
			FVector vDir;
			switch (Face)
			{
				case CubeFace_PosX:
					vDir = XAxis;
					break;
				case CubeFace_NegX:
					vDir = -XAxis;
					break;
				case CubeFace_PosY:
					vUp = -ZAxis;
					vDir = YAxis;
					break;
				case CubeFace_NegY:
					vUp = ZAxis;
					vDir = -YAxis;
					break;
				case CubeFace_PosZ:
					vDir = ZAxis;
					break;
				case CubeFace_NegZ:
					vDir = -ZAxis;
					break;
			}
			// derive right vector
			FVector vRight(vUp ^ vDir);
			// create matrix from the 3 axes
			return FBasisVectorMatrix(vRight, vUp, vDir, FVector::ZeroVector);
		}
	} ;

	check(CaptureComponent);

	const bool bIsODS = CaptureComponent->TextureTargetLeft && CaptureComponent->TextureTargetRight && CaptureComponent->TextureTargetODS;
	const uint32 StartIndex = (bIsODS) ? 1 : 0;
	const uint32 EndIndex = (bIsODS) ? 3 : 1;
	
	UTextureRenderTargetCube* const TextureTargets[] = {
		CaptureComponent->TextureTarget, 
		CaptureComponent->TextureTargetLeft, 
		CaptureComponent->TextureTargetRight
	};

	FTransform Transform = CaptureComponent->GetComponentToWorld();
	const FVector ViewLocation = Transform.GetTranslation();

	if (CaptureComponent->bCaptureRotation)
	{
		// Remove the translation from Transform because we only need rotation.
		Transform.SetTranslation(FVector::ZeroVector);
		Transform.SetScale3D(FVector::OneVector);
	}

	for (uint32 CaptureIter = StartIndex; CaptureIter < EndIndex; ++CaptureIter)
	{
		UTextureRenderTargetCube* const TextureTarget = TextureTargets[CaptureIter];

		if (GetFeatureLevel() >= ERHIFeatureLevel::ES3_1 && TextureTarget)
		{
			const float FOV = 90 * (float)PI / 360.0f;
			for (int32 faceidx = 0; faceidx < (int32)ECubeFace::CubeFace_MAX; faceidx++)
			{
				const ECubeFace TargetFace = (ECubeFace)faceidx;
				const FVector Location = CaptureComponent->GetComponentToWorld().GetTranslation();

				FMatrix ViewRotationMatrix;

				if (CaptureComponent->bCaptureRotation)
				{
					ViewRotationMatrix = Transform.ToInverseMatrixWithScale() * FLocal::CalcCubeFaceTransform(TargetFace);
				}
				else
				{
					ViewRotationMatrix = FLocal::CalcCubeFaceTransform(TargetFace);
				}
				FIntPoint CaptureSize(TextureTarget->GetSurfaceWidth(), TextureTarget->GetSurfaceHeight());
				FMatrix ProjectionMatrix;
				BuildProjectionMatrix(CaptureSize, ECameraProjectionMode::Perspective, FOV, 1.0f, GNearClippingPlane, ProjectionMatrix);
				FPostProcessSettings PostProcessSettings;

				float StereoIPD = 0.0f;
				if (bIsODS)
				{
					StereoIPD = (CaptureIter == 1) ? CaptureComponent->IPD * -0.5f : CaptureComponent->IPD * 0.5f;
				}

				FSceneRenderer* SceneRenderer = CreateSceneRendererForSceneCapture(this, CaptureComponent, TextureTarget->GameThread_GetRenderTargetResource(), CaptureSize, ViewRotationMatrix, Location, ProjectionMatrix, CaptureComponent->MaxViewDistanceOverride, true, &PostProcessSettings, 0, CaptureComponent->GetViewOwner(), StereoIPD);
				SceneRenderer->ViewFamily.SceneCaptureSource = CaptureComponent->CaptureSource;

				FTextureRenderTargetCubeResource* TextureRenderTarget = static_cast<FTextureRenderTargetCubeResource*>(TextureTarget->GameThread_GetRenderTargetResource());
				FString EventName;
				if (!CaptureComponent->ProfilingEventName.IsEmpty())
				{
					EventName = CaptureComponent->ProfilingEventName;
				}
				else if (CaptureComponent->GetOwner())
				{
					CaptureComponent->GetOwner()->GetFName().ToString(EventName);
				}
				ENQUEUE_RENDER_COMMAND(CaptureCommand)(
					[SceneRenderer, TextureRenderTarget, EventName, TargetFace](FRHICommandListImmediate& RHICmdList)
				{
					UpdateSceneCaptureContent_RenderThread(RHICmdList, SceneRenderer, TextureRenderTarget, TextureRenderTarget, EventName, FResolveParams(FResolveRect(), TargetFace), false, FGenerateMipsParams(), false);
				}
				);
			}
		}
	}

	if (bIsODS)
	{
		const FTextureRenderTargetCubeResource* const LeftEye = static_cast<FTextureRenderTargetCubeResource*>(CaptureComponent->TextureTargetLeft->GameThread_GetRenderTargetResource());
		const FTextureRenderTargetCubeResource* const RightEye = static_cast<FTextureRenderTargetCubeResource*>(CaptureComponent->TextureTargetRight->GameThread_GetRenderTargetResource());
		FTextureRenderTargetResource* const RenderTarget = CaptureComponent->TextureTargetODS->GameThread_GetRenderTargetResource();
		const ERHIFeatureLevel::Type InFeatureLevel = FeatureLevel;

		ENQUEUE_RENDER_COMMAND(ODSCaptureCommand)(
			[LeftEye, RightEye, RenderTarget, InFeatureLevel](FRHICommandListImmediate& RHICmdList)
		{
			ODSCapture_RenderThread(RHICmdList, LeftEye, RightEye, RenderTarget, InFeatureLevel);
		}
		);
	}
}
