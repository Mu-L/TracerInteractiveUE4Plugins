// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Slate/WidgetRenderer.h"
#include "TextureResource.h"
#include "Layout/ArrangedChildren.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "Rendering/SlateDrawBuffer.h"
#include "Engine/TextureRenderTarget2D.h"

#if !UE_SERVER
#include "Interfaces/ISlateRHIRendererModule.h"
#endif // !UE_SERVER

#include "Widgets/LayerManager/STooltipPresenter.h"
#include "Widgets/Layout/SPopup.h"
#include "Framework/Application/SlateApplication.h"

FWidgetRenderer::FWidgetRenderer(bool bUseGammaCorrection, bool bInClearTarget)
	: bPrepassNeeded(true)
	, bClearHitTestGrid(true)
	, bUseGammaSpace(bUseGammaCorrection)
	, bClearTarget(bInClearTarget)
	, ViewOffset(0.0f, 0.0f)
{
#if !UE_SERVER
	if ( LIKELY(FApp::CanEverRender()) )
	{
		Renderer = FModuleManager::Get().LoadModuleChecked<ISlateRHIRendererModule>("SlateRHIRenderer").CreateSlate3DRenderer(bUseGammaSpace);
	}
#endif
}

FWidgetRenderer::~FWidgetRenderer()
{
}

ISlate3DRenderer* FWidgetRenderer::GetSlateRenderer()
{
	return Renderer.Get();
}

void FWidgetRenderer::SetUseGammaCorrection(bool bInUseGammaSpace)
{
	bUseGammaSpace = bInUseGammaSpace;

#if !UE_SERVER
	if (LIKELY(FApp::CanEverRender()))
	{
		Renderer->SetUseGammaCorrection(bInUseGammaSpace);
	}
#endif
}

void FWidgetRenderer::SetApplyColorDeficiencyCorrection(bool bInApplyColorCorrection)
{
#if !UE_SERVER
	if (LIKELY(FApp::CanEverRender()))
	{
		Renderer->SetApplyColorDeficiencyCorrection(bInApplyColorCorrection);
	}
#endif
}

UTextureRenderTarget2D* FWidgetRenderer::DrawWidget(const TSharedRef<SWidget>& Widget, FVector2D DrawSize)
{
	if ( LIKELY(FApp::CanEverRender()) )
	{
		UTextureRenderTarget2D* RenderTarget = FWidgetRenderer::CreateTargetFor(DrawSize, TF_Bilinear, bUseGammaSpace);

		DrawWidget(RenderTarget, Widget, DrawSize, 0);

		return RenderTarget;
	}

	return nullptr;
}

UTextureRenderTarget2D* FWidgetRenderer::CreateTargetFor(FVector2D DrawSize, TextureFilter InFilter, bool bUseGammaCorrection)
{
	if ( LIKELY(FApp::CanEverRender()) )
	{
		const bool bIsLinearSpace = !bUseGammaCorrection;

		UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>();
		RenderTarget->Filter = InFilter;
		RenderTarget->ClearColor = FLinearColor::Transparent;
		RenderTarget->SRGB = bIsLinearSpace;
		RenderTarget->TargetGamma = 1;
		RenderTarget->InitCustomFormat(DrawSize.X, DrawSize.Y, PF_B8G8R8A8, bIsLinearSpace);
		RenderTarget->UpdateResourceImmediate(true);

		return RenderTarget;
	}

	return nullptr;
}

void FWidgetRenderer::DrawWidget(FRenderTarget* RenderTarget, const TSharedRef<SWidget>& Widget, FVector2D DrawSize, float DeltaTime, bool bDeferRenderTargetUpdate)
{
	DrawWidget(RenderTarget, Widget, 1.f, DrawSize, DeltaTime, bDeferRenderTargetUpdate);
}

void FWidgetRenderer::DrawWidget(UTextureRenderTarget2D* RenderTarget, const TSharedRef<SWidget>& Widget, FVector2D DrawSize, float DeltaTime, bool bDeferRenderTargetUpdate)
{
	DrawWidget(RenderTarget->GameThread_GetRenderTargetResource(), Widget, DrawSize, DeltaTime, bDeferRenderTargetUpdate);
}

void FWidgetRenderer::DrawWidget(
	FRenderTarget* RenderTarget,
	const TSharedRef<SWidget>& Widget,
	float Scale,
	FVector2D DrawSize,
	float DeltaTime,
	bool bDeferRenderTargetUpdate)
{
	TSharedRef<SVirtualWindow> Window = SNew(SVirtualWindow).Size(DrawSize);
	TSharedRef<FHittestGrid> HitTestGrid = MakeShareable(new FHittestGrid());

	Window->SetContent(Widget);
	Window->Resize(DrawSize);

	DrawWindow(RenderTarget, HitTestGrid, Window, Scale, DrawSize, DeltaTime, bDeferRenderTargetUpdate);
}

void FWidgetRenderer::DrawWidget(
	UTextureRenderTarget2D* RenderTarget,
	const TSharedRef<SWidget>& Widget,
	float Scale,
	FVector2D DrawSize,
	float DeltaTime,
	bool bDeferRenderTargetUpdate)
{
	DrawWidget(RenderTarget->GameThread_GetRenderTargetResource(), Widget, Scale, DrawSize, DeltaTime, bDeferRenderTargetUpdate);
}

void FWidgetRenderer::DrawWindow(
	FRenderTarget* RenderTarget,
	TSharedRef<FHittestGrid> HitTestGrid,
	TSharedRef<SWindow> Window,
	float Scale,
	FVector2D DrawSize,
	float DeltaTime,
	bool bDeferRenderTargetUpdate)
{
	FGeometry WindowGeometry = FGeometry::MakeRoot(DrawSize * ( 1 / Scale ), FSlateLayoutTransform(Scale));

	DrawWindow
	(
		RenderTarget,
		HitTestGrid,
		Window,
		WindowGeometry,
		WindowGeometry.GetLayoutBoundingRect(),
		DeltaTime,
		bDeferRenderTargetUpdate
	);
}

void FWidgetRenderer::DrawWindow(
	UTextureRenderTarget2D* RenderTarget,
	TSharedRef<FHittestGrid> HitTestGrid,
	TSharedRef<SWindow> Window,
	float Scale,
	FVector2D DrawSize,
	float DeltaTime,
	bool bDeferRenderTargetUpdate)
{
	DrawWindow(RenderTarget->GameThread_GetRenderTargetResource(), HitTestGrid, Window, Scale, DrawSize, DeltaTime, bDeferRenderTargetUpdate);
}

void FWidgetRenderer::DrawWindow(
	FRenderTarget* RenderTarget,
	TSharedRef<FHittestGrid> HitTestGrid,
	TSharedRef<SWindow> Window,
	FGeometry WindowGeometry,
	FSlateRect WindowClipRect,
	float DeltaTime,
	bool bDeferRenderTargetUpdate)
{
	FPaintArgs PaintArgs(Window.Get(), HitTestGrid.Get(), FVector2D::ZeroVector, FApp::GetCurrentTime(), DeltaTime);
	DrawWindow(PaintArgs, RenderTarget, Window, WindowGeometry, WindowClipRect, DeltaTime, bDeferRenderTargetUpdate);
}

void FWidgetRenderer::DrawWindow(
	UTextureRenderTarget2D* RenderTarget,
	TSharedRef<FHittestGrid> HitTestGrid,
	TSharedRef<SWindow> Window,
	FGeometry WindowGeometry,
	FSlateRect WindowClipRect,
	float DeltaTime,
	bool bDeferRenderTargetUpdate)
{
	DrawWindow(RenderTarget->GameThread_GetRenderTargetResource(), HitTestGrid, Window, WindowGeometry, WindowClipRect, DeltaTime, bDeferRenderTargetUpdate);
}

void FWidgetRenderer::DrawWindow(
	const FPaintArgs& PaintArgs,
	FRenderTarget* RenderTarget,
	TSharedRef<SWindow> Window,
	FGeometry WindowGeometry,
	FSlateRect WindowClipRect,
	float DeltaTime,
	bool bDeferRenderTargetUpdate)
{
#if !UE_SERVER
	FSlateRenderer* MainSlateRenderer = FSlateApplication::Get().GetRenderer();
	FScopeLock ScopeLock(MainSlateRenderer->GetResourceCriticalSection());

	if ( LIKELY(FApp::CanEverRender()) )
	{
	    if ( bPrepassNeeded )
	    {
		    // Ticking can cause geometry changes.  Recompute
		    Window->SlatePrepass(WindowGeometry.Scale);
	    }
    
		if ( bClearHitTestGrid )
		{
			// Prepare the test grid 
			PaintArgs.GetGrid().ClearGridForNewFrame(WindowClipRect);
		}
    
	    // Get the free buffer & add our virtual window
	    FSlateDrawBuffer& DrawBuffer = Renderer->GetDrawBuffer();
	    FSlateWindowElementList& WindowElementList = DrawBuffer.AddWindowElementList(Window);
    
	    int32 MaxLayerId = 0;
	    {
		    // Paint the window
		    MaxLayerId = Window->Paint(
			    PaintArgs,
			    WindowGeometry, WindowClipRect,
			    WindowElementList,
			    0,
			    FWidgetStyle(),
			    Window->IsEnabled());
	    }

		//MaxLayerId = WindowElementList.PaintDeferred(MaxLayerId);
		DeferredPaints = WindowElementList.GetDeferredPaintList();

		Renderer->DrawWindow_GameThread(DrawBuffer);

		DrawBuffer.ViewOffset = ViewOffset;

		FRenderThreadUpdateContext Context =
		{
			&DrawBuffer,
			static_cast<float>(FApp::GetCurrentTime() - GStartTime),
			static_cast<float>(FApp::GetDeltaTime()),
			static_cast<float>(FPlatformTime::Seconds() - GStartTime),
			RenderTarget,
			Renderer.Get(),
			bClearTarget
		};

		FSlateApplication::Get().GetRenderer()->AddWidgetRendererUpdate(Context, bDeferRenderTargetUpdate);
	}
#endif // !UE_SERVER
}

void FWidgetRenderer::DrawWindow(
	const FPaintArgs& PaintArgs,
	UTextureRenderTarget2D* RenderTarget,
	TSharedRef<SWindow> Window,
	FGeometry WindowGeometry,
	FSlateRect WindowClipRect,
	float DeltaTime,
	bool bDeferRenderTargetUpdate)
{
	DrawWindow(PaintArgs, RenderTarget->GameThread_GetRenderTargetResource(), Window, WindowGeometry, WindowClipRect, DeltaTime, bDeferRenderTargetUpdate);
}
