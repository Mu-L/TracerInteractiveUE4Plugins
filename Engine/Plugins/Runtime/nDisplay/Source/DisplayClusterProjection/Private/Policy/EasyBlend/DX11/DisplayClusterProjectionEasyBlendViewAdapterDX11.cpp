// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/EasyBlend/DX11/DisplayClusterProjectionEasyBlendViewAdapterDX11.h"
#include "Policy/EasyBlend/DX11/DisplayClusterProjectionEasyBlendLibraryDX11.h"

#include "DisplayClusterProjectionHelpers.h"
#include "DisplayClusterProjectionLog.h"

#include "RHI.h"
#include "RHIResources.h"
#include "RHIUtilities.h"

#include "Windows/D3D11RHI/Private/D3D11RHIPrivate.h"

#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/RendererSettings.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"


FDisplayClusterProjectionEasyBlendViewAdapterDX11::FDisplayClusterProjectionEasyBlendViewAdapterDX11(const FDisplayClusterProjectionEasyBlendViewAdapterBase::FInitParams& InitParams)
	: FDisplayClusterProjectionEasyBlendViewAdapterBase(InitParams)
	, bIsRenderResourcesInitialized(false)
{
	check(InitParams.NumViews > 0)

	Views.AddDefaulted(InitParams.NumViews);
}

FDisplayClusterProjectionEasyBlendViewAdapterDX11::~FDisplayClusterProjectionEasyBlendViewAdapterDX11()
{
	for (auto& View : Views)
	{
		if (View.bIsMeshInitialized)
		{
			// Release the mesh data only if it was previously initialized
			DisplayClusterProjectionEasyBlendLibraryDX11::EasyBlendUninitializeFunc(View.EasyBlendMeshData.Get());
		}
	}
}

bool FDisplayClusterProjectionEasyBlendViewAdapterDX11::Initialize(const FString& File)
{
	// Initialize EasyBlend DLL API
	if (!DisplayClusterProjectionEasyBlendLibraryDX11::Initialize())
	{
		UE_LOG(LogDisplayClusterProjectionEasyBlend, Error, TEXT("Couldn't link to the EasyBlend DLL"));
		return false;
	}

	// Check if EasyBlend geometry file exists
	if (!FPaths::FileExists(File))
	{
		UE_LOG(LogDisplayClusterProjectionEasyBlend, Error, TEXT("File '%s' not found"), *File);
		return false;
	}

	// Initialize EasyBlend data for each view
	const char* const FileName = TCHAR_TO_ANSI(*File);
	for (auto& It : Views)
	{
		// Initialize the mesh data
		{
			FScopeLock lock(&DllAccessCS);

			check(DisplayClusterProjectionEasyBlendLibraryDX11::EasyBlendInitializeFunc);
			const EasyBlendSDKDXError Result = DisplayClusterProjectionEasyBlendLibraryDX11::EasyBlendInitializeFunc(FileName, It.EasyBlendMeshData.Get());
			if (!EasyBlendSDKDX_SUCCEEDED(Result))
			{
				UE_LOG(LogDisplayClusterProjectionEasyBlend, Error, TEXT("Couldn't initialize EasyBlend internals"));
				return false;
			}
		}

		// EasyBlendMeshData has been initialized
		It.bIsMeshInitialized = true;

		// Only perspective projection is supported so far
		if (It.EasyBlendMeshData->Projection != EasyBlendSDKDX_PROJECTION_Perspective)
		{
			UE_LOG(LogDisplayClusterProjectionEasyBlend, Error, TEXT("EasyBlend mesh data has projection value %d. Only perspective projection is allowed at this version."), EasyBlendSDKDX_PROJECTION_Perspective);
			return false;
		}
	}

	return true;
}

// Location/Rotation inside the function is in EasyBlend space with a scale applied
bool FDisplayClusterProjectionEasyBlendViewAdapterDX11::CalculateView(const uint32 ViewIdx, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP)
{
	check(Views.Num() > (int)ViewIdx);

	ZNear = NCP;
	ZFar  = FCP;

	// Convert to EasyBlend coordinate system
	FVector EasyBlendEyeLocation;
	EasyBlendEyeLocation.X =  InOutViewLocation.Y;
	EasyBlendEyeLocation.Y = -InOutViewLocation.Z;
	EasyBlendEyeLocation.Z =  InOutViewLocation.X;

	// View rotation
	double Yaw   = 0.l;
	double Pitch = 0.l;
	double Roll  = 0.l;

	{
		FScopeLock lock(&DllAccessCS);

		// Update view location
		check(DisplayClusterProjectionEasyBlendLibraryDX11::EasyBlendSetEyepointFunc);
		DisplayClusterProjectionEasyBlendLibraryDX11::EasyBlendSetEyepointFunc(Views[ViewIdx].EasyBlendMeshData.Get(), EasyBlendEyeLocation.X, EasyBlendEyeLocation.Y, EasyBlendEyeLocation.Z);

		// Get actual view rotation
		check(DisplayClusterProjectionEasyBlendLibraryDX11::EasyBlendSDK_GetHeadingPitchRollFunc);
		DisplayClusterProjectionEasyBlendLibraryDX11::EasyBlendSDK_GetHeadingPitchRollFunc(Yaw, Pitch, Roll, Views[ViewIdx].EasyBlendMeshData.Get());
	}

	// Forward view rotation to a caller
	InOutViewRotation = FRotator(-(float)Pitch, (float)Yaw, (float)Roll);

	return true;
}

bool FDisplayClusterProjectionEasyBlendViewAdapterDX11::GetProjectionMatrix(const uint32 ViewIdx, FMatrix& OutPrjMatrix)
{
	check(Views.Num() > (int)ViewIdx);

	// Build Projection matrix:
	const float n = ZNear;
	const float f = ZFar;

	const float l = Views[ViewIdx].EasyBlendMeshData->Frustum.LeftAngle;
	const float r = Views[ViewIdx].EasyBlendMeshData->Frustum.RightAngle;
	const float b = Views[ViewIdx].EasyBlendMeshData->Frustum.BottomAngle;
	const float t = Views[ViewIdx].EasyBlendMeshData->Frustum.TopAngle;

	OutPrjMatrix = DisplayClusterHelpers::math::GetProjectionMatrixFromAngles(l, r, t, b, n, f);

	return true;
}

bool FDisplayClusterProjectionEasyBlendViewAdapterDX11::ApplyWarpBlend_RenderThread(const uint32 ViewIdx, FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewportRect)
{
	check(IsInRenderingThread());
	check(Views.Num() > (int)ViewIdx);

	if (!InitializeResources_RenderThread())
	{
		return false;
	}

	FD3D11DynamicRHI* d3d11RHI = static_cast<FD3D11DynamicRHI*>(GDynamicRHI);
	FViewport* MainViewport = GEngine->GameViewport->Viewport;
	if (d3d11RHI==nullptr || MainViewport==nullptr)
	{
		return false;
	}

	// Copy the requested region to a temporary texture
	LoadViewportTexture_RenderThread(ViewIdx, RHICmdList, SrcTexture, ViewportRect);

	// Prepare the textures
	FD3D11TextureBase* DstTextureRHI = static_cast<FD3D11TextureBase*>(Views[ViewIdx].TargetableTexture->GetTextureBaseRHI());
	FD3D11TextureBase* SrcTextureRHI = static_cast<FD3D11TextureBase*>(Views[ViewIdx].ShaderResourceTexture->GetTextureBaseRHI());

	ID3D11RenderTargetView* DstTextureRTV = DstTextureRHI->GetRenderTargetView(0, -1);

	ID3D11Texture2D * DstTextureD3D11 = static_cast<ID3D11Texture2D*>(DstTextureRHI->GetResource());
	ID3D11Texture2D * SrcTextureD3D11 = static_cast<ID3D11Texture2D*>(SrcTextureRHI->GetResource());

	// Setup In/Out EasyBlend textures
	{
		FScopeLock lock(&DllAccessCS);

		check(DisplayClusterProjectionEasyBlendLibraryDX11::EasyBlendSetInputTexture2DFunc);
		const EasyBlendSDKDXError EasyBlendSDKDXError1 = DisplayClusterProjectionEasyBlendLibraryDX11::EasyBlendSetInputTexture2DFunc(Views[ViewIdx].EasyBlendMeshData.Get(), SrcTextureD3D11);

		check(DisplayClusterProjectionEasyBlendLibraryDX11::EasyBlendSetOutputTexture2DFunc);
		const EasyBlendSDKDXError EasyBlendSDKDXError2 = DisplayClusterProjectionEasyBlendLibraryDX11::EasyBlendSetOutputTexture2DFunc(Views[ViewIdx].EasyBlendMeshData.Get(), DstTextureD3D11);

		if (!(EasyBlendSDKDX_SUCCEEDED(EasyBlendSDKDXError1) && EasyBlendSDKDX_SUCCEEDED(EasyBlendSDKDXError2)))
		{
			UE_LOG(LogDisplayClusterProjectionEasyBlend, Error, TEXT("Coulnd't configure in/out textures"));
			return false;
		}
	}

	D3D11_VIEWPORT RenderViewportData;
	RenderViewportData.MinDepth = 0.0f;
	RenderViewportData.MaxDepth = 1.0f;
	RenderViewportData.Width  = static_cast<float>(GetViewportSize().X);
	RenderViewportData.Height = static_cast<float>(GetViewportSize().Y);
	RenderViewportData.TopLeftX = 0.0f;
	RenderViewportData.TopLeftY = 0.0f;

	FD3D11Device*          Device        = d3d11RHI->GetDevice();
	FD3D11DeviceContext*   DeviceContext = d3d11RHI->GetDeviceContext();

	FD3D11Viewport* Viewport  = static_cast<FD3D11Viewport*>(MainViewport->GetViewportRHI().GetReference());
	IDXGISwapChain* SwapChain = (IDXGISwapChain*)Viewport->GetSwapChain();

	DeviceContext->RSSetViewports(1, &RenderViewportData);
	DeviceContext->OMSetRenderTargets(1, &DstTextureRTV, nullptr);
	DeviceContext->Flush();

	{
		FScopeLock lock(&DllAccessCS);

		// Perform warp&blend by the EasyBlend
		check(DisplayClusterProjectionEasyBlendLibraryDX11::EasyBlendDXRenderFunc);
		EasyBlendSDKDXError EasyBlendSDKDXError = DisplayClusterProjectionEasyBlendLibraryDX11::EasyBlendDXRenderFunc(
			Views[ViewIdx].EasyBlendMeshData.Get(),
			Device,
			DeviceContext,
			SwapChain,
			false);

		if (!EasyBlendSDKDX_SUCCEEDED(EasyBlendSDKDXError))
		{
			UE_LOG(LogDisplayClusterProjectionEasyBlend, Error, TEXT("EasyBlend couldn't perform rendering operation"));
			return false;
		}
	}

	// Copy results back to our render target
	SaveViewportTexture_RenderThread(ViewIdx, RHICmdList, SrcTexture, ViewportRect);

	return true;
}

bool FDisplayClusterProjectionEasyBlendViewAdapterDX11::InitializeResources_RenderThread()
{
	check(IsInRenderingThread());

	if (!bIsRenderResourcesInitialized)
	{
		FScopeLock lock(&RenderingResourcesInitializationCS);
		if (!bIsRenderResourcesInitialized)
		{
			static const TConsoleVariableData<int32>* CVarDefaultBackBufferPixelFormat = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DefaultBackBufferPixelFormat"));
			static const EPixelFormat SceneTargetFormat = EDefaultBackBufferPixelFormat::Convert2PixelFormat(EDefaultBackBufferPixelFormat::FromInt(CVarDefaultBackBufferPixelFormat->GetValueOnRenderThread()));

			check(GDynamicRHI);
			check(GEngine);
			check(GEngine->GameViewport);

			FD3D11DynamicRHI* d3d11RHI = static_cast<FD3D11DynamicRHI*>(GDynamicRHI);
			FViewport* MainViewport = GEngine->GameViewport->Viewport;

			if (d3d11RHI && MainViewport)
			{
				FD3D11Device*                 Device = d3d11RHI->GetDevice();
				FD3D11DeviceContext*   DeviceContext = d3d11RHI->GetDeviceContext();

				check(Device);
				check(DeviceContext);

				FD3D11Viewport* Viewport = static_cast<FD3D11Viewport*>(MainViewport->GetViewportRHI().GetReference());
				IDXGISwapChain*  SwapChain = (IDXGISwapChain*)Viewport->GetSwapChain();

				check(Viewport);
				check(SwapChain);

				// Create RT texture for viewport warp
				for (auto& It : Views)
				{
					FRHIResourceCreateInfo CreateInfo;
					FTexture2DRHIRef DummyTexRef;

					RHICreateTargetableShaderResource2D(GetViewportSize().X, GetViewportSize().Y, SceneTargetFormat, 1, TexCreate_None, TexCreate_RenderTargetable, true, CreateInfo, It.TargetableTexture, It.ShaderResourceTexture);

					// Initialize EasyBlend internals
					check(DisplayClusterProjectionEasyBlendLibraryDX11::EasyBlendInitDeviceObjectsFunc);
					EasyBlendSDKDXError sdkErr = DisplayClusterProjectionEasyBlendLibraryDX11::EasyBlendInitDeviceObjectsFunc(It.EasyBlendMeshData.Get(), Device, DeviceContext, SwapChain);
					if (EasyBlendSDKDX_FAILED(sdkErr))
					{
						UE_LOG(LogDisplayClusterProjectionEasyBlend, Error, TEXT("Couldn't initialize EasyBlend Device/DeviceContext/SwapChain"));
					}
				}

				// Here we set initialization flag. In case we couldn't initialize the EasyBlend device objects, we don't want do it again.
				// However, the per-view bIsInitialized flag must be tested before call any EasyBlend function.
				bIsRenderResourcesInitialized = true;
			}
		}
	}

	return bIsRenderResourcesInitialized;
}

void FDisplayClusterProjectionEasyBlendViewAdapterDX11::LoadViewportTexture_RenderThread(const uint32 ViewIdx, FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewportRect)
{
	check(IsInRenderingThread());

	FResolveParams copyParams;
	copyParams.DestArrayIndex = 0;
	copyParams.SourceArrayIndex = 0;

	copyParams.Rect.X1 = ViewportRect.Min.X;
	copyParams.Rect.X2 = ViewportRect.Max.X;

	copyParams.Rect.Y1 = ViewportRect.Min.Y;
	copyParams.Rect.Y2 = ViewportRect.Max.Y;

	copyParams.DestRect.X1 = 0;
	copyParams.DestRect.X2 = GetViewportSize().X;

	copyParams.DestRect.Y1 = 0;
	copyParams.DestRect.Y2 = GetViewportSize().Y;

	RHICmdList.CopyToResolveTarget(SrcTexture, Views[ViewIdx].ShaderResourceTexture, copyParams);

	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
}

void FDisplayClusterProjectionEasyBlendViewAdapterDX11::SaveViewportTexture_RenderThread(const uint32 ViewIdx, FRHICommandListImmediate& RHICmdList, FRHITexture2D* DstTexture, const FIntRect& ViewportRect)
{
	check(IsInRenderingThread());

	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);

	FResolveParams copyParams;
	copyParams.DestArrayIndex = 0;
	copyParams.SourceArrayIndex = 0;

	copyParams.Rect.X1 = 0;
	copyParams.Rect.X2 = GetViewportSize().X;
	copyParams.Rect.Y1 = 0;
	copyParams.Rect.Y2 = GetViewportSize().Y;

	copyParams.DestRect.X1 = ViewportRect.Min.X;
	copyParams.DestRect.X2 = ViewportRect.Max.X;

	copyParams.DestRect.Y1 = ViewportRect.Min.Y;
	copyParams.DestRect.Y2 = ViewportRect.Max.Y;

	RHICmdList.CopyToResolveTarget(Views[ViewIdx].TargetableTexture, DstTexture, copyParams);
}
