// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "AppleARKitVideoOverlay.h"
#include "AppleARKitFrame.h"
#include "Materials/Material.h"
#include "MaterialShaderType.h"
#include "MaterialShader.h"
#include "ExternalTexture.h"
#include "Misc/Guid.h"
#include "ExternalTextureGuid.h"
#include "Containers/ResourceArray.h"
#include "MediaShaders.h"
#include "PipelineStateCache.h"
#include "RHIUtilities.h"
#include "RHIStaticStates.h"
#include "EngineModule.h"
#include "SceneUtils.h"
#include "RendererInterface.h"
#include "ScreenRendering.h"
#include "Containers/DynamicRHIResourceArray.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PostProcessParameters.h"
#if SUPPORTS_ARKIT_1_0
	#include "IOSAppDelegate.h"
#endif

#if SUPPORTS_ARKIT_1_0
/**
* Passes a CVMetalTextureRef through to the RHI to wrap in an RHI texture without traversing system memory.
* @see FAvfTexture2DResourceWrapper & FMetalSurface::FMetalSurface
*/
class FAppleARKitCameraTextureResourceWrapper : public FResourceBulkDataInterface
{
public:
	FAppleARKitCameraTextureResourceWrapper(CFTypeRef InImageBuffer)
		: ImageBuffer(InImageBuffer)
	{
		check(ImageBuffer);
		CFRetain(ImageBuffer);
	}

	/**
	* @return ptr to the resource memory which has been preallocated
	*/
	virtual const void* GetResourceBulkData() const override
	{
		return ImageBuffer;
	}

	/**
	* @return size of resource memory
	*/
	virtual uint32 GetResourceBulkDataSize() const override
	{
		return 0;
	}

	/**
	* @return the type of bulk data for special handling
	*/
	virtual EBulkDataType GetResourceType() const override
	{
		return EBulkDataType::MediaTexture;
	}

	/**
	* Free memory after it has been used to initialize RHI resource
	*/
	virtual void Discard() override
	{
		delete this;
	}

	virtual ~FAppleARKitCameraTextureResourceWrapper()
	{
		CFRelease(ImageBuffer);
		ImageBuffer = nullptr;
	}

	CFTypeRef ImageBuffer;
};
#endif

FAppleARKitVideoOverlay::FAppleARKitVideoOverlay()
	: RenderingOverlayMaterial(nullptr)
	, LastUpdateTimestamp(-1.0)
{
	RenderingOverlayMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/AppleARKit/ARKitCameraMaterial.ARKitCameraMaterial"));
	check(RenderingOverlayMaterial != nullptr);
	RenderingOverlayMaterial->AddToRoot();
}

void FAppleARKitVideoOverlay::UpdateVideoTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FAppleARKitFrame& Frame, const FSceneViewFamily& InViewFamily)
{
	// Allocate and register
	if (VideoTextureY == nullptr)
	{
		check(VideoTextureCbCr == nullptr);
		check(OverlayIndexBufferRHI == nullptr);
		check(OverlayVertexBufferRHI[0] == nullptr);

		FRHIResourceCreateInfo CreateInfo;
		VideoTextureY = RHICmdList.CreateTexture2D(1, 1, PF_R8G8B8A8, 1, 1, 0, CreateInfo);
		VideoTextureCbCr = RHICmdList.CreateTexture2D(1, 1, PF_R8G8B8A8, 1, 1, 0, CreateInfo);

		FSamplerStateInitializerRHI SamplerStateInitializer(SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap);
		FSamplerStateRHIRef SamplerStateRHI = RHICreateSamplerState(SamplerStateInitializer);

		FExternalTextureRegistry::Get().RegisterExternalTexture(ARKitPassthroughCameraExternalTextureYGuid, VideoTextureY, SamplerStateRHI);
		FExternalTextureRegistry::Get().RegisterExternalTexture(ARKitPassthroughCameraExternalTextureCbCrGuid, VideoTextureCbCr, SamplerStateRHI);

		// Setup index buffer
		const uint16 Indices[] = { 0, 1, 2, 2, 1, 3 };

		TResourceArray<uint16, INDEXBUFFER_ALIGNMENT> IndexBuffer;
		const uint32 NumIndices = ARRAY_COUNT(Indices);
		IndexBuffer.AddUninitialized(NumIndices);
		FMemory::Memcpy(IndexBuffer.GetData(), Indices, NumIndices * sizeof(uint16));

		FRHIResourceCreateInfo CreateInfoIB(&IndexBuffer);
		OverlayIndexBufferRHI = RHICreateIndexBuffer(sizeof(uint16), IndexBuffer.GetResourceDataSize(), BUF_Static, CreateInfoIB);

		check(InViewFamily.Views.Num() > 0);
		const FSceneView& View = *InViewFamily.Views[0];

		const FVector2D ViewSize(View.UnconstrainedViewRect.Max.X, View.UnconstrainedViewRect.Max.Y);
		
		// CameraSize is 1280 x 720 regardless of the device orientation. We flip it here if needed to make it consistent with the view size.
		FVector2D CameraSize = Frame.Camera.ImageResolution;
		if ((ViewSize.X > ViewSize.Y) != (CameraSize.X > CameraSize.Y))
		{
			CameraSize = FVector2D(CameraSize.Y, CameraSize.X);
		}
		
		const float CameraAspectRatio = CameraSize.X / CameraSize.Y;
		const float ViewAspectRatio = ViewSize.X / ViewSize.Y;
		const float ViewAspectRatioLandscape = (ViewSize.X > ViewSize.Y) ? ViewAspectRatio : ViewSize.Y / ViewSize.X;
		
		float UVOffsetAmount = 0.0f;
		if (!FMath::IsNearlyEqual(ViewAspectRatio, CameraAspectRatio))
		{
			if (ViewAspectRatio > CameraAspectRatio)
			{
				UVOffsetAmount = 0.5f * (1.0f - (CameraAspectRatio / ViewAspectRatio));
			}
			else
			{
				UVOffsetAmount = 0.5f * (1.0f - (ViewAspectRatio / CameraAspectRatio));
			}
		}
		
		const FVector2D UVOffset = (ViewAspectRatioLandscape <= Frame.Camera.GetAspectRatio()) ? FVector2D(UVOffsetAmount, 0.0f) : FVector2D(0.0f, UVOffsetAmount);
		
		// Setup vertex buffer
		const FVector4 Positions[] =
		{
			FVector4(0.0f, 1.0f, 0.0f, 1.0f),
			FVector4(0.0f, 0.0f, 0.0f, 1.0f),
			FVector4(1.0f, 1.0f, 0.0f, 1.0f),
			FVector4(1.0f, 0.0f, 0.0f, 1.0f)
		};

		const FVector2D UVs[] =
		{
			// Landscape left
			FVector2D(UVOffset.X, 1.0f - UVOffset.Y),
			FVector2D(UVOffset.X, UVOffset.Y),
			FVector2D(1.0f - UVOffset.X, 1.0f - UVOffset.Y),
			FVector2D(1.0f - UVOffset.X, UVOffset.Y),

			// Landscape right
			FVector2D(1.0f - UVOffset.X, UVOffset.Y),
			FVector2D(1.0f - UVOffset.X, 1.0f - UVOffset.Y),
			FVector2D(UVOffset.X, UVOffset.Y),
			FVector2D(UVOffset.X, 1.0f - UVOffset.Y),

			// Portrait
			FVector2D(1.0f - UVOffset.X, 1.0f - UVOffset.Y),
			FVector2D(UVOffset.X, 1.0f - UVOffset.Y),
			FVector2D(1.0f - UVOffset.X, UVOffset.Y),
			FVector2D(UVOffset.X, UVOffset.Y),

			// Portrait Upside Down
			FVector2D(UVOffset.X, UVOffset.Y),
			FVector2D(1.0f - UVOffset.X, UVOffset.Y),
			FVector2D(UVOffset.X, 1.0f - UVOffset.Y),
			FVector2D(1.0f - UVOffset.X, 1.0f - UVOffset.Y)
		};

		uint32 UVIndex = 0;
		for (uint32 OrientationIter = 0; OrientationIter < 4; ++OrientationIter)
		{
			TResourceArray<FFilterVertex, VERTEXBUFFER_ALIGNMENT> Vertices;
			Vertices.SetNumUninitialized(4);

			Vertices[0].Position = Positions[0];
			Vertices[0].UV = UVs[UVIndex];

			Vertices[1].Position = Positions[1];
			Vertices[1].UV = UVs[UVIndex + 1];

			Vertices[2].Position = Positions[2];
			Vertices[2].UV = UVs[UVIndex + 2];

			Vertices[3].Position = Positions[3];
			Vertices[3].UV = UVs[UVIndex + 3];

			UVIndex += 4;

			FRHIResourceCreateInfo CreateInfoVB(&Vertices);
			OverlayVertexBufferRHI[OrientationIter] = RHICreateVertexBuffer(Vertices.GetResourceDataSize(), BUF_Static, CreateInfoVB);
		}
	}

#if SUPPORTS_ARKIT_1_0
	if (FAppleARKitAvailability::SupportsARKit10())
	{
		check(IsMetalPlatform(GMaxRHIShaderPlatform));

		if (LastUpdateTimestamp != Frame.Timestamp && Frame.CapturedYImage && Frame.CapturedCbCrImage)
		{
			FRHIResourceCreateInfo CreateInfo;
			const uint32 CreateFlags = TexCreate_Dynamic | TexCreate_NoTiling | TexCreate_ShaderResource;
			CreateInfo.BulkData = new FAppleARKitCameraTextureResourceWrapper(Frame.CapturedYImage);
			CreateInfo.ResourceArray = nullptr;

			// pull the Y and CbCr textures out of the captured image planes (format is fake here, it will get the format from the FAppleARKitCameraTextureResourceWrapper)
			VideoTextureY = RHICreateTexture2D(Frame.CapturedYImageWidth, Frame.CapturedYImageHeight, /*Format=*/PF_B8G8R8A8, /*NumMips=*/1, /*NumSamples=*/1, CreateFlags, CreateInfo);

			CreateInfo.BulkData = new FAppleARKitCameraTextureResourceWrapper(Frame.CapturedCbCrImage);
			VideoTextureCbCr = RHICreateTexture2D(Frame.CapturedCbCrImageWidth, Frame.CapturedCbCrImageHeight, /*Format=*/PF_B8G8R8A8, /*NumMips=*/1, /*NumSamples=*/1, CreateFlags, CreateInfo);

			// todo: Add an update call to the registry instead of this unregister/re-register
			FExternalTextureRegistry::Get().UnregisterExternalTexture(ARKitPassthroughCameraExternalTextureYGuid);
			FExternalTextureRegistry::Get().UnregisterExternalTexture(ARKitPassthroughCameraExternalTextureCbCrGuid);

			FSamplerStateInitializerRHI SamplerStateInitializer(SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap);
			FSamplerStateRHIRef SamplerStateRHI = RHICreateSamplerState(SamplerStateInitializer);

			FExternalTextureRegistry::Get().RegisterExternalTexture(ARKitPassthroughCameraExternalTextureYGuid, VideoTextureY, SamplerStateRHI);
			FExternalTextureRegistry::Get().RegisterExternalTexture(ARKitPassthroughCameraExternalTextureCbCrGuid, VideoTextureCbCr, SamplerStateRHI);

			CFRelease(Frame.CapturedYImage);
			CFRelease(Frame.CapturedCbCrImage);
			Frame.CapturedYImage = nullptr;
			Frame.CapturedCbCrImage = nullptr;

			LastUpdateTimestamp = Frame.Timestamp;
		}
	}
#endif
}

// We use something similar to the PostProcessMaterial to render the color camera overlay.
template <bool bIsMobileRenderer>
class FARKitCameraOverlayVS : public FMaterialShader
{
	DECLARE_SHADER_TYPE(FARKitCameraOverlayVS, Material);
public:

	static bool ShouldCompilePermutation(EShaderPlatform Platform, const FMaterial* Material)
	{
		if (bIsMobileRenderer)
		{
			return Material->GetMaterialDomain() == MD_PostProcess && IsMobilePlatform(Platform);
		}
		else
		{
			return Material->GetMaterialDomain() == MD_PostProcess && !IsMobilePlatform(Platform);
		}
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, const class FMaterial* Material, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL"), 1);
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL_BEFORE_TONEMAP"), (Material->GetBlendableLocation() != BL_AfterTonemapping) ? 1 : 0);
	}

	FARKitCameraOverlayVS() { }
	FARKitCameraOverlayVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMaterialShader(Initializer)
	{
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView View)
	{
		const FVertexShaderRHIParamRef ShaderRHI = GetVertexShader();
		FMaterialShader::SetViewParameters(RHICmdList, ShaderRHI, View, View.ViewUniformBuffer);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		const bool bShaderHasOutdatedParameters = FMaterialShader::Serialize(Ar);
		return bShaderHasOutdatedParameters;
	}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FARKitCameraOverlayVS<true>, TEXT("/Engine/Private/PostProcessMaterialShaders.usf"), TEXT("MainVS_ES2"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FARKitCameraOverlayVS<false>, TEXT("/Engine/Private/PostProcessMaterialShaders.usf"), TEXT("MainVS_VideoOverlay"), SF_Vertex);

template <bool bIsMobileRenderer>
class FARKitCameraOverlayPS : public FMaterialShader
{
	DECLARE_SHADER_TYPE(FARKitCameraOverlayPS, Material);
public:

	static bool ShouldCompilePermutation(EShaderPlatform Platform, const FMaterial* Material)
	{
		if (bIsMobileRenderer)
		{
			return Material->GetMaterialDomain() == MD_PostProcess && IsMobilePlatform(Platform);
		}
		else
		{
			return Material->GetMaterialDomain() == MD_PostProcess && !IsMobilePlatform(Platform);
		}
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, const class FMaterial* Material, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL"), 1);
		OutEnvironment.SetDefine(TEXT("OUTPUT_GAMMA_SPACE"), IsMobileHDR() ? 0 : 1);
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL_BEFORE_TONEMAP"), (Material->GetBlendableLocation() != BL_AfterTonemapping) ? 1 : 0);
	}

	FARKitCameraOverlayPS() {}
	FARKitCameraOverlayPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FMaterialShader(Initializer)
	{
		for (uint32 InputIter = 0; InputIter < ePId_Input_MAX; ++InputIter)
		{
			PostprocessInputParameter[InputIter].Bind(Initializer.ParameterMap, *FString::Printf(TEXT("PostprocessInput%d"), InputIter));
			PostprocessInputParameterSampler[InputIter].Bind(Initializer.ParameterMap, *FString::Printf(TEXT("PostprocessInput%dSampler"), InputIter));
		}
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView View, const FMaterialRenderProxy* Material)
	{
		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();
		FMaterialShader::SetParameters(RHICmdList, ShaderRHI, Material, *Material->GetMaterial(View.GetFeatureLevel()), View, View.ViewUniformBuffer, ESceneTextureSetupMode::None);

		for (uint32 InputIter = 0; InputIter < ePId_Input_MAX; ++InputIter)
		{
			if (PostprocessInputParameter[InputIter].IsBound())
			{
				SetTextureParameter(
					RHICmdList, 
					ShaderRHI, 
					PostprocessInputParameter[InputIter],
					PostprocessInputParameterSampler[InputIter],
					TStaticSamplerState<>::GetRHI(),
					GBlackTexture->TextureRHI);
			}
		}
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		const bool bShaderHasOutdatedParameters = FMaterialShader::Serialize(Ar);
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderResourceParameter PostprocessInputParameter[ePId_Input_MAX];
	FShaderResourceParameter PostprocessInputParameterSampler[ePId_Input_MAX];
};

IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FARKitCameraOverlayPS<true>, TEXT("/Engine/Private/PostProcessMaterialShaders.usf"), TEXT("MainPS_ES2"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FARKitCameraOverlayPS<false>, TEXT("/Engine/Private/PostProcessMaterialShaders.usf"), TEXT("MainPS_VideoOverlay"), SF_Pixel);

void FAppleARKitVideoOverlay::RenderVideoOverlay_RenderThread(FRHICommandListImmediate& RHICmdList, const FSceneView& InView, const EScreenOrientation::Type DeviceOrientation)
{
#if SUPPORTS_ARKIT_1_0

	if (FAppleARKitAvailability::SupportsARKit10())
	{
		if (RenderingOverlayMaterial == nullptr || !RenderingOverlayMaterial->IsValidLowLevel())
		{
			return;
		}

		const auto FeatureLevel = InView.GetFeatureLevel();
		IRendererModule& RendererModule = GetRendererModule();

		const FMaterial* const CameraMaterial = RenderingOverlayMaterial->GetRenderProxy(false)->GetMaterial(FeatureLevel);
		const FMaterialShaderMap* const MaterialShaderMap = CameraMaterial->GetRenderingThreadShaderMap();

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = RendererModule.GetFilterVertexDeclaration().VertexDeclarationRHI;
		
		const bool bIsMobileRenderer = FeatureLevel <= ERHIFeatureLevel::ES3_1;
		FMaterialShader* VertexShader = nullptr;
		FMaterialShader* PixelShader = nullptr;
		if (bIsMobileRenderer)
		{
			VertexShader = MaterialShaderMap->GetShader<FARKitCameraOverlayVS<true>>();
			PixelShader = MaterialShaderMap->GetShader<FARKitCameraOverlayPS<true>>();
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(reinterpret_cast<FARKitCameraOverlayVS<true>*>(VertexShader));
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(reinterpret_cast<FARKitCameraOverlayPS<true>*>(PixelShader));
		}
		else
		{
			VertexShader = MaterialShaderMap->GetShader<FARKitCameraOverlayVS<false>>();
			PixelShader = MaterialShaderMap->GetShader<FARKitCameraOverlayPS<false>>();
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(reinterpret_cast<FARKitCameraOverlayVS<false>*>(VertexShader));
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(reinterpret_cast<FARKitCameraOverlayPS<false>*>(PixelShader));
		}
		
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
		
		const FIntPoint ViewSize = InView.UnconstrainedViewRect.Size();
		FDrawRectangleParameters Parameters;
		Parameters.PosScaleBias = FVector4(ViewSize.X, ViewSize.Y, 0, 0);
		Parameters.UVScaleBias = FVector4(1.0f, 1.0f, 0.0f, 0.0f);
		Parameters.InvTargetSizeAndTextureSize = FVector4(
														  1.0f / ViewSize.X, 1.0f / ViewSize.Y,
														  1.0f, 1.0f);

		if (bIsMobileRenderer)
		{
			FARKitCameraOverlayVS<true>* const VertexShaderPtr = reinterpret_cast<FARKitCameraOverlayVS<true>*>(VertexShader);
			SetUniformBufferParameterImmediate(RHICmdList, VertexShaderPtr->GetVertexShader(), VertexShaderPtr->GetUniformBufferParameter<FDrawRectangleParameters>(), Parameters);
			VertexShaderPtr->SetParameters(RHICmdList, InView);
			reinterpret_cast<FARKitCameraOverlayPS<true>*>(PixelShader)->SetParameters(RHICmdList, InView, RenderingOverlayMaterial->GetRenderProxy(false));
		}
		else
		{
			FARKitCameraOverlayVS<false>* const VertexShaderPtr = reinterpret_cast<FARKitCameraOverlayVS<false>*>(VertexShader);
			SetUniformBufferParameterImmediate(RHICmdList, VertexShaderPtr->GetVertexShader(), VertexShaderPtr->GetUniformBufferParameter<FDrawRectangleParameters>(), Parameters);
			VertexShaderPtr->SetParameters(RHICmdList, InView);
			reinterpret_cast<FARKitCameraOverlayPS<false>*>(PixelShader)->SetParameters(RHICmdList, InView, RenderingOverlayMaterial->GetRenderProxy(false));
		}

		FVertexBufferRHIParamRef VertexBufferRHI = nullptr;
		switch (DeviceOrientation)
		{
			case EScreenOrientation::Type::LandscapeLeft:
				VertexBufferRHI = OverlayVertexBufferRHI[0];
				break;

			case EScreenOrientation::Type::LandscapeRight:
				VertexBufferRHI = OverlayVertexBufferRHI[1];
				break;

			case EScreenOrientation::Type::Portrait:
				VertexBufferRHI = OverlayVertexBufferRHI[2];
				break;

			case EScreenOrientation::Type::PortraitUpsideDown:
				VertexBufferRHI = OverlayVertexBufferRHI[3];
				break;

			default:
				VertexBufferRHI = OverlayVertexBufferRHI[0];
				break;
		}

		if (VertexBufferRHI && OverlayIndexBufferRHI.IsValid())
		{
			RHICmdList.SetStreamSource(0, VertexBufferRHI, 0);
			RHICmdList.DrawIndexedPrimitive(
				OverlayIndexBufferRHI,
				PT_TriangleList,
				/*BaseVertexIndex=*/ 0,
				/*MinIndex=*/ 0,
				/*NumVertices=*/ 4,
				/*StartIndex=*/ 0,
				/*NumPrimitives=*/ 2,
				/*NumInstances=*/ 1
			);
		}
	}
#endif
}
