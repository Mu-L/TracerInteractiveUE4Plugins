// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Device/QuadBufferStereo/DisplayClusterDeviceQuadBufferStereoBase.h"

#include "Misc/DisplayClusterLog.h"

#include <utility>


FDisplayClusterDeviceQuadBufferStereoBase::FDisplayClusterDeviceQuadBufferStereoBase()
{
}

FDisplayClusterDeviceQuadBufferStereoBase::~FDisplayClusterDeviceQuadBufferStereoBase()
{
}


void FDisplayClusterDeviceQuadBufferStereoBase::CalculateRenderTargetSize(const class FViewport& Viewport, uint32& InOutSizeX, uint32& InOutSizeY)
{
	check(IsInGameThread());

	// Call base to calculate single-view RT size
	FDisplayClusterDeviceBase::CalculateRenderTargetSize(Viewport, InOutSizeX, InOutSizeY);
	
	// And make it twice bigger for second eye
	InOutSizeX = Viewport.GetSizeXY().X * 2;
	InOutSizeY = Viewport.GetSizeXY().Y;

	// Store right eye region
	EyeRegions[1] = FIntRect(FIntPoint(InOutSizeX / 2, 0), FIntPoint(InOutSizeX, InOutSizeY));

	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Render target size: [%d x %d]"), InOutSizeX, InOutSizeY);

	check(InOutSizeX > 0 && InOutSizeY > 0);
}

void FDisplayClusterDeviceQuadBufferStereoBase::AdjustViewRect(enum EStereoscopicPass StereoPassType, int32& X, int32& Y, uint32& SizeX, uint32& SizeY) const
{
	const EDisplayClusterEyeType EyeType = DecodeEyeType(StereoPassType);
	const int ViewportIndex = DecodeViewportIndex(StereoPassType);
	const int ViewIdx = DecodeViewIndex(StereoPassType);

	// Current viewport data
	FDisplayClusterRenderViewport& RenderViewport = RenderViewports[ViewportIndex];

	// Provide the Engine with a viewport rectangle
	const FIntRect& ViewportRect = RenderViewports[ViewportIndex].GetRect();
	X = ViewportRect.Min.X;
	Y = ViewportRect.Min.Y;

	if (EyeType == EDisplayClusterEyeType::StereoRight)
	{
		X += SizeX;
	}

	SizeX = ViewportRect.Width();
	SizeY = ViewportRect.Height();

	// Update view context
	FDisplayClusterRenderViewContext& ViewContext = RenderViewport.GetContext(ViewIdx);
	ViewContext.RenderTargetRect = FIntRect(X, Y, X + SizeX, Y + SizeY);

	const FIntRect& r = ViewContext.RenderTargetRect;
	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Adjusted view rect: ViewportIdx=%d, EyeType=%d, [%d,%d - %d,%d]"), ViewportIndex, int(EyeType), r.Min.X, r.Min.Y, r.Max.X, r.Max.Y);
}

void FDisplayClusterDeviceQuadBufferStereoBase::CopyTextureToBackBuffer_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* BackBuffer, FRHITexture2D* SrcTexture, FVector2D WindowSize) const
{
	check(IsInRenderingThread());

	const FIntPoint SrcSize = SrcTexture->GetSizeXY();
	const FIntPoint BBSize  = BackBuffer->GetSizeXY();
	
	// Calculate sub regions to copy
	const int HalfSrcSizeX = SrcSize.X / 2;

	FResolveParams copyParamsLeft;
	copyParamsLeft.DestArrayIndex = 0;
	copyParamsLeft.SourceArrayIndex = 0;
	copyParamsLeft.Rect.X1 = 0;
	copyParamsLeft.Rect.Y1 = 0;
	copyParamsLeft.Rect.X2 = HalfSrcSizeX;
	copyParamsLeft.Rect.Y2 = BBSize.Y;
	copyParamsLeft.DestRect.X1 = 0;
	copyParamsLeft.DestRect.Y1 = 0;
	copyParamsLeft.DestRect.X2 = HalfSrcSizeX;
	copyParamsLeft.DestRect.Y2 = BBSize.Y;

	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("CopyToResolveTarget [L]: [%d,%d - %d,%d] -> [%d,%d - %d,%d]"),
		copyParamsLeft.Rect.X1, copyParamsLeft.Rect.Y1, copyParamsLeft.Rect.X2, copyParamsLeft.Rect.Y2,
		copyParamsLeft.DestRect.X1, copyParamsLeft.DestRect.Y1, copyParamsLeft.DestRect.X2, copyParamsLeft.DestRect.Y2);

	RHICmdList.CopyToResolveTarget(SrcTexture, BackBuffer, copyParamsLeft);

	FResolveParams copyParamsRight;
	copyParamsRight.DestArrayIndex = 1;
	copyParamsRight.SourceArrayIndex = 0;

	copyParamsRight.Rect = copyParamsLeft.Rect;
	copyParamsRight.Rect.X1 = HalfSrcSizeX;
	copyParamsRight.Rect.X2 = SrcSize.X;

	copyParamsRight.DestRect = copyParamsLeft.DestRect;

	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("CopyToResolveTarget [R]: [%d,%d - %d,%d] -> [%d,%d - %d,%d]"),
		copyParamsRight.Rect.X1, copyParamsRight.Rect.Y1, copyParamsRight.Rect.X2, copyParamsRight.Rect.Y2,
		copyParamsRight.DestRect.X1, copyParamsRight.DestRect.Y1, copyParamsRight.DestRect.X2, copyParamsRight.DestRect.Y2);

	RHICmdList.CopyToResolveTarget(SrcTexture, BackBuffer, copyParamsRight);
}
