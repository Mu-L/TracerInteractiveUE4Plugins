// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SceneRenderTargets.cpp: Scene render target implementation.
=============================================================================*/

#include "PostProcess/SceneRenderTargets.h"
#include "Shader.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "SceneRenderTargetParameters.h"
#include "VelocityRendering.h"
#include "RendererModule.h"
#include "LightPropagationVolume.h"
#include "ScenePrivate.h"
#include "HdrCustomResolveShaders.h"
#include "WideCustomResolveShaders.h"
#include "ClearQuad.h"
#include "RenderUtils.h"
#include "PipelineStateCache.h"
#include "OneColorShader.h"
#include "ResolveShader.h"
#include "EngineGlobals.h"
#include "UnrealEngine.h"
#include "StereoRendering.h"
#include "StereoRenderTargetManager.h"
#include "VT/VirtualTextureSystem.h"
#include "VT/VirtualTextureFeedback.h"
#include "VisualizeTexture.h"

static TAutoConsoleVariable<int32> CVarRSMResolution(
	TEXT("r.LPV.RSMResolution"),
	360,
	TEXT("Reflective Shadow Map resolution (used for LPV) - higher values result in less aliasing artifacts, at the cost of performance"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

/*-----------------------------------------------------------------------------
FSceneRenderTargets
-----------------------------------------------------------------------------*/

int32 GDownsampledOcclusionQueries = 0;
static FAutoConsoleVariableRef CVarDownsampledOcclusionQueries(
	TEXT("r.DownsampledOcclusionQueries"),
	GDownsampledOcclusionQueries,
	TEXT("Whether to issue occlusion queries to a downsampled depth buffer"),
	ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarSceneTargetsResizingMethod(
	TEXT("r.SceneRenderTargetResizeMethod"),
	0,
	TEXT("Control the scene render target resize method:\n")
	TEXT("(This value is only used in game mode and on windowing platforms.)\n")
	TEXT("0: Resize to match requested render size (Default) (Least memory use, can cause stalls when size changes e.g. ScreenPercentage)\n")
	TEXT("1: Fixed to screen resolution.\n")
	TEXT("2: Expands to encompass the largest requested render dimension. (Most memory use, least prone to allocation stalls.)"),	
	ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarCustomDepth(
	TEXT("r.CustomDepth"),
	1,
	TEXT("0: feature is disabled\n")
	TEXT("1: feature is enabled, texture is created on demand\n")
	TEXT("2: feature is enabled, texture is not released until required (should be the project setting if the feature should not stall)\n")
	TEXT("3: feature is enabled, stencil writes are enabled, texture is not released until required (should be the project setting if the feature should not stall)"),
	ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarMSAACount(
	TEXT("r.MSAACount"),
	4,
	TEXT("Number of MSAA samples to use with the forward renderer.  Only used when MSAA is enabled in the rendering project settings.\n")
	TEXT("0: MSAA disabled (Temporal AA enabled)\n")
	TEXT("1: MSAA disabled\n")
	TEXT("2: Use 2x MSAA\n")
	TEXT("4: Use 4x MSAA")
	TEXT("8: Use 8x MSAA"),
	ECVF_RenderThreadSafe | ECVF_Scalability
	);

static TAutoConsoleVariable<int32> CVarMobileMSAA(
	TEXT("r.MobileMSAA"),
	1,
	TEXT("Use MSAA instead of Temporal AA on mobile:\n")
	TEXT("1: Use Temporal AA (MSAA disabled)\n")
	TEXT("2: Use 2x MSAA (Temporal AA disabled)\n")
	TEXT("4: Use 4x MSAA (Temporal AA disabled)\n")
	TEXT("8: Use 8x MSAA (Temporal AA disabled)"),
	ECVF_RenderThreadSafe | ECVF_Scalability
	);

static TAutoConsoleVariable<int32> CVarGBufferFormat(
	TEXT("r.GBufferFormat"),
	1,
	TEXT("Defines the memory layout used for the GBuffer.\n")
	TEXT("(affects performance, mostly through bandwidth, quality of normals and material attributes).\n")
	TEXT(" 0: lower precision (8bit per component, for profiling)\n")
	TEXT(" 1: low precision (default)\n")
	TEXT(" 3: high precision normals encoding\n")
	TEXT(" 5: high precision"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarDefaultBackBufferPixelFormat(
	TEXT("r.DefaultBackBufferPixelFormat"),
	4,
	TEXT("Defines the default back buffer pixel format.\n")
	TEXT(" 0: 8bit RGBA\n")
	TEXT(" 1: 16bit RGBA\n")
	TEXT(" 2: Float RGB\n")
	TEXT(" 3: Float RGBA\n")
	TEXT(" 4: 10bit RGB, 2bit Alpha\n"),
	ECVF_ReadOnly);

int32 GAllowCustomMSAAResolves = 1;
static FAutoConsoleVariableRef CVarAllowCustomResolves(
   TEXT("r.MSAA.AllowCustomResolves"),
   GAllowCustomMSAAResolves,
   TEXT("Whether to use builtin HW resolve or allow custom shader MSAA resolves"),
   ECVF_RenderThreadSafe
   );

int32 GVirtualTextureFeedbackFactor = 16;
static FAutoConsoleVariableRef CVarVirtualTextureFeedbackFactor(
	TEXT("r.vt.FeedbackFactor"),
	GVirtualTextureFeedbackFactor,
	TEXT("The size of the VT feedback buffer is calculated by dividing the render resolution by this factor"),
	ECVF_RenderThreadSafe | ECVF_ReadOnly /*Read-only as shaders are compiled with this value*/
);

static TAutoConsoleVariable<int32> CVarMobileClearSceneColorWithMaxAlpha(
	TEXT("r.MobileClearSceneColorWithMaxAlpha"),
	0,
	TEXT("Whether the scene color alpha channel on mobile should be cleared with MAX_FLT.\n")
    TEXT(" 0: disabled (default)\n"),
	ECVF_RenderThreadSafe);

/** The global render targets used for scene rendering. */
static TGlobalResource<FSceneRenderTargets> SceneRenderTargetsSingleton;

extern int32 GUseTranslucentLightingVolumes;

FSceneRenderTargets& FSceneRenderTargets::Get(FRHICommandList& RHICmdList)
{
	FSceneRenderTargets* SceneContext = (FSceneRenderTargets*)RHICmdList.GetRenderThreadContext(FRHICommandListBase::ERenderThreadContext::SceneRenderTargets);
	if (!SceneContext)
	{
		return SceneRenderTargetsSingleton;
	}
	check(!RHICmdList.IsImmediate());
	return *SceneContext;
}

FSceneRenderTargets& FSceneRenderTargets::Get(FRHICommandListImmediate& RHICmdList)
{
	check(IsInRenderingThread() && !RHICmdList.GetRenderThreadContext(FRHICommandListBase::ERenderThreadContext::SceneRenderTargets)
		&& !FTaskGraphInterface::Get().IsThreadProcessingTasks(ENamedThreads::GetRenderThread_Local())); // if we are processing tasks on the local queue, it is assumed this are in support of async tasks, which cannot use the current state of the render targets. This can be relaxed if needed.
	return SceneRenderTargetsSingleton;
}

FSceneRenderTargets& FSceneRenderTargets::Get(FRHIAsyncComputeCommandListImmediate& RHICmdList)
{
	check(IsInRenderingThread() && !RHICmdList.GetRenderThreadContext(FRHICommandListBase::ERenderThreadContext::SceneRenderTargets)
		&& !FTaskGraphInterface::Get().IsThreadProcessingTasks(ENamedThreads::GetRenderThread_Local())); // if we are processing tasks on the local queue, it is assumed this are in support of async tasks, which cannot use the current state of the render targets. This can be relaxed if needed.
	return SceneRenderTargetsSingleton;
}

FSceneRenderTargets& FSceneRenderTargets::GetGlobalUnsafe()
{
	check(IsInRenderingThread());
	return SceneRenderTargetsSingleton;
}

FSceneRenderTargets& FSceneRenderTargets::Get_FrameConstantsOnly()
{
	return SceneRenderTargetsSingleton;
}

 FSceneRenderTargets* FSceneRenderTargets::CreateSnapshot(const FViewInfo& InView)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FSceneRenderTargets_CreateSnapshot);
	check(IsInRenderingThread() && FMemStack::Get().GetNumMarks() == 1); // we do not want this popped before the end of the scene and it better be the scene allocator
	FSceneRenderTargets* NewSnapshot = new (FMemStack::Get()) FSceneRenderTargets(InView, *this);
	check(NewSnapshot->bSnapshot);
	Snapshots.Add(NewSnapshot);
	return NewSnapshot;
}

void FSceneRenderTargets::SetSnapshotOnCmdList(FRHICommandList& TargetCmdList)
{
	check(bSnapshot);
	TargetCmdList.SetRenderThreadContext(this, FRHICommandListBase::ERenderThreadContext::SceneRenderTargets);
}

void FSceneRenderTargets::DestroyAllSnapshots()
{
	if (Snapshots.Num())
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FSceneRenderTargets_DestroyAllSnapshots);
		check(IsInRenderingThread());
		for (auto Snapshot : Snapshots)
		{
			Snapshot->~FSceneRenderTargets();
		}
		Snapshots.Reset();
		GRenderTargetPool.DestructSnapshots();
	}
}



template <size_t N>
static void SnapshotArray(TRefCountPtr<IPooledRenderTarget> (&Dest)[N], const TRefCountPtr<IPooledRenderTarget> (&Src)[N])
{
	for (int32 Index = 0; Index < N; Index++)
	{
		Dest[Index] = GRenderTargetPool.MakeSnapshot(Src[Index]);
	}
}

template <uint32 N>
static void SnapshotArray(TArray<TRefCountPtr<IPooledRenderTarget>, TInlineAllocator<N>>(&Dest), const TArray<TRefCountPtr<IPooledRenderTarget>, TInlineAllocator<N>>(&Src))
{
	Dest.SetNum(Src.Num());
	for (int32 Index = 0; Index < Src.Num(); Index++)
	{
		Dest[Index] = GRenderTargetPool.MakeSnapshot(Src[Index]);
	}
}

FSceneRenderTargets::FSceneRenderTargets(const FViewInfo& View, const FSceneRenderTargets& SnapshotSource)
	: LightAttenuation(GRenderTargetPool.MakeSnapshot(SnapshotSource.LightAttenuation))
	, LightAccumulation(GRenderTargetPool.MakeSnapshot(SnapshotSource.LightAccumulation))
	, DirectionalOcclusion(GRenderTargetPool.MakeSnapshot(SnapshotSource.DirectionalOcclusion))
	, SceneDepthZ(GRenderTargetPool.MakeSnapshot(SnapshotSource.SceneDepthZ))	
	, SceneVelocity(GRenderTargetPool.MakeSnapshot(SnapshotSource.SceneVelocity))
	, LightingChannels(GRenderTargetPool.MakeSnapshot(SnapshotSource.LightingChannels))
	, SceneAlphaCopy(GRenderTargetPool.MakeSnapshot(SnapshotSource.SceneAlphaCopy))
	, AuxiliarySceneDepthZ(GRenderTargetPool.MakeSnapshot(SnapshotSource.AuxiliarySceneDepthZ))
	, SmallDepthZ(GRenderTargetPool.MakeSnapshot(SnapshotSource.SmallDepthZ))
	, GBufferA(GRenderTargetPool.MakeSnapshot(SnapshotSource.GBufferA))
	, GBufferB(GRenderTargetPool.MakeSnapshot(SnapshotSource.GBufferB))
	, GBufferC(GRenderTargetPool.MakeSnapshot(SnapshotSource.GBufferC))
	, GBufferD(GRenderTargetPool.MakeSnapshot(SnapshotSource.GBufferD))
	, GBufferE(GRenderTargetPool.MakeSnapshot(SnapshotSource.GBufferE))
	, DBufferA(GRenderTargetPool.MakeSnapshot(SnapshotSource.DBufferA))
	, DBufferB(GRenderTargetPool.MakeSnapshot(SnapshotSource.DBufferB))
	, DBufferC(GRenderTargetPool.MakeSnapshot(SnapshotSource.DBufferC))
	, DBufferMask(GRenderTargetPool.MakeSnapshot(SnapshotSource.DBufferMask))
	, ScreenSpaceAO(GRenderTargetPool.MakeSnapshot(SnapshotSource.ScreenSpaceAO))
	, QuadOverdrawBuffer(GRenderTargetPool.MakeSnapshot(SnapshotSource.QuadOverdrawBuffer))
	, CustomDepth(GRenderTargetPool.MakeSnapshot(SnapshotSource.CustomDepth))
	, MobileCustomStencil(GRenderTargetPool.MakeSnapshot(SnapshotSource.MobileCustomStencil))
	, CustomStencilSRV(SnapshotSource.CustomStencilSRV)
	, SkySHIrradianceMap(GRenderTargetPool.MakeSnapshot(SnapshotSource.SkySHIrradianceMap))
	, MobileMultiViewSceneColor(GRenderTargetPool.MakeSnapshot(SnapshotSource.MobileMultiViewSceneColor))
	, MobileMultiViewSceneDepthZ(GRenderTargetPool.MakeSnapshot(SnapshotSource.MobileMultiViewSceneDepthZ))
	, EditorPrimitivesColor(GRenderTargetPool.MakeSnapshot(SnapshotSource.EditorPrimitivesColor))
	, EditorPrimitivesDepth(GRenderTargetPool.MakeSnapshot(SnapshotSource.EditorPrimitivesDepth))
	, SeparateTranslucencyRT(SnapshotSource.SeparateTranslucencyRT)
	, DownsampledTranslucencyDepthRT(SnapshotSource.DownsampledTranslucencyDepthRT)
	, bScreenSpaceAOIsValid(SnapshotSource.bScreenSpaceAOIsValid)
	, bCustomDepthIsValid(SnapshotSource.bCustomDepthIsValid)
	, GBufferRefCount(SnapshotSource.GBufferRefCount)
	, ThisFrameNumber(SnapshotSource.ThisFrameNumber)
	, CurrentDesiredSizeIndex(SnapshotSource.CurrentDesiredSizeIndex)
	, bVelocityPass(SnapshotSource.bVelocityPass)
	, bSeparateTranslucencyPass(SnapshotSource.bSeparateTranslucencyPass)
	, BufferSize(SnapshotSource.BufferSize)
	, SeparateTranslucencyBufferSize(SnapshotSource.SeparateTranslucencyBufferSize)
	, SeparateTranslucencyScale(SnapshotSource.SeparateTranslucencyScale)
	, SmallColorDepthDownsampleFactor(SnapshotSource.SmallColorDepthDownsampleFactor)
	, bUseDownsizedOcclusionQueries(SnapshotSource.bUseDownsizedOcclusionQueries)
	, CurrentGBufferFormat(SnapshotSource.CurrentGBufferFormat)
	, CurrentSceneColorFormat(SnapshotSource.CurrentSceneColorFormat)
	, CurrentMobileSceneColorFormat(SnapshotSource.CurrentMobileSceneColorFormat)
	, bAllowStaticLighting(SnapshotSource.bAllowStaticLighting)
	, CurrentMaxShadowResolution(SnapshotSource.CurrentMaxShadowResolution)
	, CurrentRSMResolution(SnapshotSource.CurrentRSMResolution)
	, CurrentTranslucencyLightingVolumeDim(SnapshotSource.CurrentTranslucencyLightingVolumeDim)
	, CurrentMobile32bpp(SnapshotSource.CurrentMobile32bpp)
	, CurrentMSAACount(SnapshotSource.CurrentMSAACount)
	, CurrentMinShadowResolution(SnapshotSource.CurrentMinShadowResolution)
	, bCurrentLightPropagationVolume(SnapshotSource.bCurrentLightPropagationVolume)
	, CurrentFeatureLevel(SnapshotSource.CurrentFeatureLevel)
	, CurrentShadingPath(SnapshotSource.CurrentShadingPath)
	, bRequireSceneColorAlpha(SnapshotSource.bRequireSceneColorAlpha)
	, bAllocateVelocityGBuffer(SnapshotSource.bAllocateVelocityGBuffer)
	, bGBuffersFastCleared(SnapshotSource.bGBuffersFastCleared)	
	, bSceneDepthCleared(SnapshotSource.bSceneDepthCleared)	
	, bSnapshot(true)
	, DefaultColorClear(SnapshotSource.DefaultColorClear)
	, DefaultDepthClear(SnapshotSource.DefaultDepthClear)
	, QuadOverdrawIndex(SnapshotSource.QuadOverdrawIndex)
	, bHMDAllocatedDepthTarget(SnapshotSource.bHMDAllocatedDepthTarget)
{
	FMemory::Memcpy(LargestDesiredSizes, SnapshotSource.LargestDesiredSizes);
#if PREVENT_RENDERTARGET_SIZE_THRASHING
	FMemory::Memcpy(HistoryFlags, SnapshotSource.HistoryFlags, sizeof(SnapshotSource.HistoryFlags));
#endif
	SnapshotArray(SceneColor, SnapshotSource.SceneColor);
	SnapshotArray(ReflectionColorScratchCubemap, SnapshotSource.ReflectionColorScratchCubemap);
	SnapshotArray(DiffuseIrradianceScratchCubemap, SnapshotSource.DiffuseIrradianceScratchCubemap);
	SnapshotArray(TranslucencyLightingVolumeAmbient, SnapshotSource.TranslucencyLightingVolumeAmbient);
	SnapshotArray(TranslucencyLightingVolumeDirectional, SnapshotSource.TranslucencyLightingVolumeDirectional);
	SnapshotArray(OptionalShadowDepthColor, SnapshotSource.OptionalShadowDepthColor);
	VirtualTextureFeedback.MakeSnapshot(SnapshotSource.VirtualTextureFeedback);
}

inline const TCHAR* GetSceneColorTargetName(EShadingPath ShadingPath)
{
	const TCHAR* SceneColorNames[(uint32)EShadingPath::Num] =
	{ 
		TEXT("SceneColorMobile"), 
		TEXT("SceneColorDeferred")
	};
	check((uint32)ShadingPath < ARRAY_COUNT(SceneColorNames));
	return SceneColorNames[(uint32)ShadingPath];
}

#if PREVENT_RENDERTARGET_SIZE_THRASHING

namespace ERenderTargetHistory
{
	enum Type
	{
		RTH_SceneCapture		= 0x1,
		RTH_ReflectionCapture	= 0x2,
		RTH_HighresScreenshot	= 0x4,
		RTH_MaskAll				= 0x7,
	};
}

static inline void UpdateHistoryFlags(uint8& Flags, bool bIsSceneCapture, bool bIsReflectionCapture, bool bIsHighResScreenShot)
{
	Flags |= bIsSceneCapture ? ERenderTargetHistory::RTH_SceneCapture : 0;
	Flags |= bIsReflectionCapture ? ERenderTargetHistory::RTH_ReflectionCapture : 0;
	Flags |= bIsHighResScreenShot ? ERenderTargetHistory::RTH_HighresScreenshot : 0;
}

template <uint32 NumEntries>
static bool AnyCaptureRenderedRecently(const uint8* HistoryFlags, uint8 Mask)
{
	uint8 Result = 0;
	for (uint32 Idx = 0; Idx < NumEntries; ++Idx)
	{
		Result |= (HistoryFlags[Idx] & Mask);
	}
	return Result != 0;
}

#define UPDATE_HISTORY_FLAGS(Flags, bIsSceneCapture, bIsReflectionCapture, bIsHighResScreenShot) UpdateHistoryFlags(Flags, bIsSceneCapture, bIsReflectionCapture, bIsHighResScreenShot)
#define ANY_CAPTURE_RENDERED_RECENTLY(HistoryFlags, NumEntries) AnyCaptureRenderedRecently<NumEntries>(HistoryFlags, ERenderTargetHistory::RTH_MaskAll)
#define ANY_HIGHRES_CAPTURE_RENDERED_RECENTLY(HistoryFlags, NumEntries) AnyCaptureRenderedRecently<NumEntries>(HistoryFlags, ERenderTargetHistory::RTH_HighresScreenshot)

#else

#define UPDATE_HISTORY_FLAGS(Flags, bIsSceneCapture, bIsReflectionCapture, bIsHighResScreenShot)
#define ANY_CAPTURE_RENDERED_RECENTLY(HistoryFlags, NumEntries) (false)
#define ANY_HIGHRES_CAPTURE_RENDERED_RECENTLY(HistoryFlags, NumEntries) (false)
#endif

FIntPoint FSceneRenderTargets::ComputeDesiredSize(const FSceneViewFamily& ViewFamily)
{
	enum ESizingMethods { RequestedSize, ScreenRes, Grow, VisibleSizingMethodsCount };
	ESizingMethods SceneTargetsSizingMethod = Grow;

	bool bIsSceneCapture = false;
	bool bIsReflectionCapture = false;
	bool bIsVRScene = false;

	for (int32 ViewIndex = 0, ViewCount = ViewFamily.Views.Num(); ViewIndex < ViewCount; ++ViewIndex)
	{
		const FSceneView* View = ViewFamily.Views[ViewIndex];

		bIsSceneCapture |= View->bIsSceneCapture;
		bIsReflectionCapture |= View->bIsReflectionCapture;
		bIsVRScene |= View->StereoPass != EStereoscopicPass::eSSP_FULL;
	}

	if(!FPlatformProperties::SupportsWindowedMode() || (bIsVRScene && !bIsSceneCapture))
	{
		// Force ScreenRes on non windowed platforms.
		SceneTargetsSizingMethod = RequestedSize;
	}
	else if (GIsEditor)
	{
		// Always grow scene render targets in the editor.
		SceneTargetsSizingMethod = Grow;
	}	
	else
	{
		// Otherwise use the setting specified by the console variable.
		SceneTargetsSizingMethod = (ESizingMethods) FMath::Clamp(CVarSceneTargetsResizingMethod.GetValueOnRenderThread(), 0, (int32)VisibleSizingMethodsCount);
	}

	FIntPoint DesiredBufferSize = FIntPoint::ZeroValue;
	FIntPoint DesiredFamilyBufferSize = FSceneRenderer::GetDesiredInternalBufferSize(ViewFamily);
	switch (SceneTargetsSizingMethod)
	{
		case RequestedSize:
			DesiredBufferSize = DesiredFamilyBufferSize;
			break;

		case ScreenRes:
			DesiredBufferSize = FIntPoint(GSystemResolution.ResX, GSystemResolution.ResY);
			break;

		case Grow:
			DesiredBufferSize = FIntPoint(
				FMath::Max((int32)GetBufferSizeXY().X, DesiredFamilyBufferSize.X),
				FMath::Max((int32)GetBufferSizeXY().Y, DesiredFamilyBufferSize.Y));
			break;

		default:
			checkNoEntry();
	}

	const uint32 FrameNumber = ViewFamily.FrameNumber;
	if (ThisFrameNumber != FrameNumber)
	{
		ThisFrameNumber = FrameNumber;
		if (++CurrentDesiredSizeIndex == FrameSizeHistoryCount)
		{
			CurrentDesiredSizeIndex -= FrameSizeHistoryCount;
		}
		// this allows the BufferSize to shrink each frame (in game)
		LargestDesiredSizes[CurrentDesiredSizeIndex] = FIntPoint::ZeroValue;
#if PREVENT_RENDERTARGET_SIZE_THRASHING
		HistoryFlags[CurrentDesiredSizeIndex] = 0;
#endif
	}

	// this allows The BufferSize to not grow below the SceneCapture requests (happen before scene rendering, in the same frame with a Grow request)
	FIntPoint& LargestDesiredSizeThisFrame = LargestDesiredSizes[CurrentDesiredSizeIndex];
	LargestDesiredSizeThisFrame = LargestDesiredSizeThisFrame.ComponentMax(DesiredBufferSize);
	bool bIsHighResScreenshot = GIsHighResScreenshot;
	UPDATE_HISTORY_FLAGS(HistoryFlags[CurrentDesiredSizeIndex], bIsSceneCapture, bIsReflectionCapture, bIsHighResScreenshot);

	// we want to shrink the buffer but as we can have multiple scenecaptures per frame we have to delay that a frame to get all size requests
	// Don't save buffer size in history while making high-res screenshot.
	// We have to use the requested size when allocating an hmd depth target to ensure it matches the hmd allocated render target size.
	bool bAllowDelayResize = !GIsHighResScreenshot && !bHMDAllocatedDepthTarget;

	// Don't consider the history buffer when the aspect ratio changes, the existing buffers won't make much sense at all.
	// This prevents problems when orientation changes on mobile in particular.
	// bIsReflectionCapture is explicitly checked on all platforms to prevent aspect ratio change detection from forcing the immediate buffer resize.
	// This ensures that 1) buffers are not resized spuriously during reflection rendering 2) all cubemap faces use the same render target size.
	if (bAllowDelayResize && !bIsReflectionCapture && !ANY_CAPTURE_RENDERED_RECENTLY(HistoryFlags, FrameSizeHistoryCount))
	{
		const bool bAspectRatioChanged =
			!BufferSize.Y ||
			!FMath::IsNearlyEqual(
				(float)BufferSize.X / BufferSize.Y,
				(float)DesiredBufferSize.X / DesiredBufferSize.Y);

		if (bAspectRatioChanged)
		{
			bAllowDelayResize = false;

			// At this point we're assuming a simple output resize and forcing a hard swap so clear the history.
			// If we don't the next frame will fail this check as the allocated aspect ratio will match the new
			// frame's forced size so we end up looking through the history again, finding the previous old size
			// and reallocating. Only after a few frames can the results actually settle when the history clears 
			for (int32 i = 0; i < FrameSizeHistoryCount; ++i)
			{
				LargestDesiredSizes[i] = FIntPoint::ZeroValue;
#if PREVENT_RENDERTARGET_SIZE_THRASHING
				HistoryFlags[i] = 0;
#endif
			}
		}
	}
	const bool bAnyHighresScreenshotRecently = ANY_HIGHRES_CAPTURE_RENDERED_RECENTLY(HistoryFlags, FrameSizeHistoryCount);
	if(bAnyHighresScreenshotRecently != GIsHighResScreenshot)
	{
		bAllowDelayResize = false;
	}

	if(bAllowDelayResize)
	{
		for (int32 i = 0; i < FrameSizeHistoryCount; ++i)
		{
			DesiredBufferSize = DesiredBufferSize.ComponentMax(LargestDesiredSizes[i]);
		}
	}

	return DesiredBufferSize;
}

uint16 FSceneRenderTargets::GetNumSceneColorMSAASamples(ERHIFeatureLevel::Type InFeatureLevel)
{
	uint16 NumSamples = 1;

	if (InFeatureLevel >= ERHIFeatureLevel::SM4)
	{
		static IConsoleVariable* CVarDefaultAntiAliasing = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DefaultFeature.AntiAliasing"));
		EAntiAliasingMethod Method = (EAntiAliasingMethod)CVarDefaultAntiAliasing->GetInt();

		if (IsForwardShadingEnabled(GetFeatureLevelShaderPlatform(InFeatureLevel)) && Method == AAM_MSAA)
		{
			NumSamples = FMath::Max(1, CVarMSAACount.GetValueOnRenderThread());

			if (NumSamples != 1 && NumSamples != 2 && NumSamples != 4 && NumSamples != 8)
			{
				UE_LOG(LogRenderer, Warning, TEXT("Requested %d samples for MSAA, but this is not supported; falling back to 1 sample"), NumSamples);
				NumSamples = 1;
			}
		}
	}
	else
	{
		NumSamples = CVarMobileMSAA.GetValueOnRenderThread();

		static uint16 PlatformMaxSampleCount = GDynamicRHI->RHIGetPlatformTextureMaxSampleCount();
		NumSamples = FMath::Min(NumSamples, PlatformMaxSampleCount);
		
		if (NumSamples != 1 && NumSamples != 2 && NumSamples != 4 && NumSamples != 8)
		{
			UE_LOG(LogRenderer, Warning, TEXT("Requested %d samples for MSAA, but this is not supported; falling back to 1 sample"), NumSamples);
			NumSamples = 1;
		}
	}
	if (NumSamples > 1 && !RHISupportsMSAA(GShaderPlatformForFeatureLevel[InFeatureLevel]))
	{
		NumSamples = 1;

		static bool bWarned = false;

		if (!bWarned)
		{
			bWarned = true;
			UE_LOG(LogRenderer, Log, TEXT("MSAA requested but the platform doesn't support MSAA, falling back to Temporal AA"));
		}
	}

	return NumSamples;
}

void FSceneRenderTargets::Allocate(FRHICommandListImmediate& RHICmdList, const FSceneRenderer* SceneRenderer)
{
	check(IsInRenderingThread());
	// ViewFamily setup wasn't complete
	check(SceneRenderer->ViewFamily.FrameNumber != UINT_MAX);

	const FSceneViewFamily& ViewFamily = SceneRenderer->ViewFamily;

	// If feature level has changed, release all previously allocated targets to the pool. If feature level has changed but
	const auto NewFeatureLevel = ViewFamily.Scene->GetFeatureLevel();
	CurrentShadingPath = ViewFamily.Scene->GetShadingPath();

	bRequireSceneColorAlpha = false;

	for (int32 ViewIndex = 0; ViewIndex < ViewFamily.Views.Num(); ViewIndex++)
	{
		// Planar reflections and scene captures use scene color alpha to keep track of where content has been rendered, for compositing into a different scene later
		if (ViewFamily.Views[ViewIndex]->bIsPlanarReflection || ViewFamily.Views[ViewIndex]->bIsSceneCapture)
		{
			bRequireSceneColorAlpha = true;
		}
	}

	FIntPoint DesiredBufferSize = ComputeDesiredSize(ViewFamily);
	check(DesiredBufferSize.X > 0 && DesiredBufferSize.Y > 0);
	QuantizeSceneBufferSize(DesiredBufferSize, DesiredBufferSize);

	int GBufferFormat = CVarGBufferFormat.GetValueOnRenderThread();

	// Set default clear values
	if (CurrentShadingPath == EShadingPath::Mobile &&
		CVarMobileClearSceneColorWithMaxAlpha.GetValueOnRenderThread())
	{
		// On mobile the scene depth is calculated from the alpha component of the scene color
		// Use BlackMaxAlpha to ensure un-rendered pixels have max depth...
		SetDefaultColorClear(FClearValueBinding::BlackMaxAlpha);
	}
	else
	{
		SetDefaultColorClear(FClearValueBinding::Black);
	}
	SetDefaultDepthClear(FClearValueBinding::DepthFar);

	int SceneColorFormat;
	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SceneColorFormat"));

		SceneColorFormat = CVar->GetValueOnRenderThread();
	}

	EPixelFormat MobileSceneColorFormat = GetDesiredMobileSceneColorFormat();
		
	bool bNewAllowStaticLighting;
	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));

		bNewAllowStaticLighting = CVar->GetValueOnRenderThread() != 0;
	}

	bool bDownsampledOcclusionQueries = GDownsampledOcclusionQueries != 0;

	int32 MaxShadowResolution = GetCachedScalabilityCVars().MaxShadowResolution;

	int32 RSMResolution = FMath::Clamp(CVarRSMResolution.GetValueOnRenderThread(), 1, 2048);

	if (ViewFamily.Scene->GetShadingPath() == EShadingPath::Mobile)
	{
		// ensure there is always enough space for mobile renderer's tiled shadow maps
		// by reducing the shadow map resolution.
		int32 MaxShadowDepthBufferDim = FMath::Max(GMaxShadowDepthBufferSizeX, GMaxShadowDepthBufferSizeY);
		if (MaxShadowResolution * 2 >  MaxShadowDepthBufferDim)
		{
			MaxShadowResolution = MaxShadowDepthBufferDim / 2;
		}
	}

	const int32 TranslucencyLightingVolumeDim = GetTranslucencyLightingVolumeDim();

	uint32 Mobile32bpp = !IsMobileHDR() || IsMobileHDR32bpp();

	int32 MSAACount = GetNumSceneColorMSAASamples(NewFeatureLevel);

	bool bLightPropagationVolume = UseLightPropagationVolumeRT(NewFeatureLevel);

	uint32 MinShadowResolution;
	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Shadow.MinResolution"));

		MinShadowResolution = CVar->GetValueOnRenderThread();
	}

	if( (BufferSize.X != DesiredBufferSize.X) ||
		(BufferSize.Y != DesiredBufferSize.Y) ||
		(CurrentGBufferFormat != GBufferFormat) ||
		(CurrentSceneColorFormat != SceneColorFormat) ||
		(CurrentMobileSceneColorFormat != MobileSceneColorFormat) ||
		(bAllowStaticLighting != bNewAllowStaticLighting) ||
		(bUseDownsizedOcclusionQueries != bDownsampledOcclusionQueries) ||
		(CurrentMaxShadowResolution != MaxShadowResolution) ||
 		(CurrentRSMResolution != RSMResolution) ||
		(CurrentTranslucencyLightingVolumeDim != TranslucencyLightingVolumeDim) ||
		(CurrentMobile32bpp != Mobile32bpp) ||
		(CurrentMSAACount != MSAACount) ||
		(bCurrentLightPropagationVolume != bLightPropagationVolume) ||
		(CurrentMinShadowResolution != MinShadowResolution))
	{
		CurrentGBufferFormat = GBufferFormat;
		CurrentSceneColorFormat = SceneColorFormat;
		CurrentMobileSceneColorFormat = MobileSceneColorFormat;
		bAllowStaticLighting = bNewAllowStaticLighting;
		bUseDownsizedOcclusionQueries = bDownsampledOcclusionQueries;
		CurrentMaxShadowResolution = MaxShadowResolution;
		CurrentRSMResolution = RSMResolution;
		CurrentTranslucencyLightingVolumeDim = TranslucencyLightingVolumeDim;
		CurrentMobile32bpp = Mobile32bpp;
		CurrentMSAACount = MSAACount;
		CurrentMinShadowResolution = MinShadowResolution;
		bCurrentLightPropagationVolume = bLightPropagationVolume;

		// Reinitialize the render targets for the given size.
		SetBufferSize(DesiredBufferSize.X, DesiredBufferSize.Y);

		UE_LOG(LogRenderer, Log, TEXT("Reallocating scene render targets to support %ux%u Format %u NumSamples %u (Frame:%u)."), BufferSize.X, BufferSize.Y, (uint32)GetSceneColorFormat(NewFeatureLevel), CurrentMSAACount, ViewFamily.FrameNumber);

		UpdateRHI();
	}

	// Do allocation of render targets if they aren't available for the current shading path
	CurrentFeatureLevel = NewFeatureLevel;
	AllocateRenderTargets(RHICmdList, ViewFamily.Views.Num());
}

void FSceneRenderTargets::BeginRenderingSceneColor(FRHICommandList& RHICmdList, ESimpleRenderTargetMode RenderTargetMode/*=EUninitializedColorExistingDepth*/, FExclusiveDepthStencil DepthStencilAccess, bool bTransitionWritable)
{
	check(RHICmdList.IsOutsideRenderPass());

	SCOPED_DRAW_EVENT(RHICmdList, BeginRenderingSceneColor);
	AllocSceneColor(RHICmdList);

	ERenderTargetLoadAction ColorLoadAction, DepthLoadAction, StencilLoadAction;
	ERenderTargetStoreAction ColorStoreAction, DepthStoreAction, StencilStoreAction;
	DecodeRenderTargetMode(RenderTargetMode, ColorLoadAction, ColorStoreAction, DepthLoadAction, DepthStoreAction, StencilLoadAction, StencilStoreAction, DepthStencilAccess);

	FRHIRenderPassInfo RPInfo(GetSceneColorSurface(), MakeRenderTargetActions(ColorLoadAction, ColorStoreAction));
	RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(MakeRenderTargetActions(DepthLoadAction, DepthStoreAction), MakeRenderTargetActions(StencilLoadAction, StencilStoreAction));
	RPInfo.DepthStencilRenderTarget.DepthStencilTarget = GetSceneDepthSurface();
	RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = DepthStencilAccess;

	if (bTransitionWritable)
	{
		TransitionRenderPassTargets(RHICmdList, RPInfo);
	}
	RHICmdList.BeginRenderPass(RPInfo, TEXT("BeginRenderingSceneColor"));
} 

void FSceneRenderTargets::FinishRenderingSceneColor(FRHICommandList& RHICmdList)
{
	RHICmdList.EndRenderPass();
}

int32 FSceneRenderTargets::FillGBufferRenderPassInfo(ERenderTargetLoadAction ColorLoadAction, FRHIRenderPassInfo& OutRenderPassInfo, int32& OutVelocityRTIndex)
{
	int32 MRTCount = 0;
	OutRenderPassInfo.ColorRenderTargets[MRTCount].Action = MakeRenderTargetActions(ColorLoadAction, ERenderTargetStoreAction::EStore);
	OutRenderPassInfo.ColorRenderTargets[MRTCount].RenderTarget = GetSceneColorSurface();
	OutRenderPassInfo.ColorRenderTargets[MRTCount].ArraySlice = -1;
	OutRenderPassInfo.ColorRenderTargets[MRTCount].MipIndex = 0;
	MRTCount++;

	const EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(CurrentFeatureLevel);
	const bool bUseGBuffer = IsUsingGBuffers(ShaderPlatform);

	if (bUseGBuffer)
	{
		OutRenderPassInfo.ColorRenderTargets[MRTCount].Action = MakeRenderTargetActions(ColorLoadAction, ERenderTargetStoreAction::EStore);
		OutRenderPassInfo.ColorRenderTargets[MRTCount].RenderTarget = GBufferA->GetRenderTargetItem().TargetableTexture;
		OutRenderPassInfo.ColorRenderTargets[MRTCount].ArraySlice = -1;
		OutRenderPassInfo.ColorRenderTargets[MRTCount].MipIndex = 0;
		MRTCount++;

		OutRenderPassInfo.ColorRenderTargets[MRTCount].Action = MakeRenderTargetActions(ColorLoadAction, ERenderTargetStoreAction::EStore);
		OutRenderPassInfo.ColorRenderTargets[MRTCount].RenderTarget = GBufferB->GetRenderTargetItem().TargetableTexture;
		OutRenderPassInfo.ColorRenderTargets[MRTCount].ArraySlice = -1;
		OutRenderPassInfo.ColorRenderTargets[MRTCount].MipIndex = 0;
		MRTCount++;

		OutRenderPassInfo.ColorRenderTargets[MRTCount].Action = MakeRenderTargetActions(ColorLoadAction, ERenderTargetStoreAction::EStore);
		OutRenderPassInfo.ColorRenderTargets[MRTCount].RenderTarget = GBufferC->GetRenderTargetItem().TargetableTexture;
		OutRenderPassInfo.ColorRenderTargets[MRTCount].ArraySlice = -1;
		OutRenderPassInfo.ColorRenderTargets[MRTCount].MipIndex = 0;
		MRTCount++;
	}

	// The velocity buffer needs to be bound before other optionnal rendertargets (when UseSelectiveBasePassOutputs() is true).
	// Otherwise there is an issue on some AMD hardware where the target does not get updated. Seems to be related to the velocity buffer format as it works fine with other targets.
	if (bAllocateVelocityGBuffer && !IsSimpleForwardShadingEnabled(ShaderPlatform))
	{
		OutVelocityRTIndex = MRTCount;
		check(OutVelocityRTIndex == 4 || (!bUseGBuffer && OutVelocityRTIndex == 1)); // As defined in BasePassPixelShader.usf
		
		OutRenderPassInfo.ColorRenderTargets[MRTCount].Action = MakeRenderTargetActions(ColorLoadAction, ERenderTargetStoreAction::EStore);
		OutRenderPassInfo.ColorRenderTargets[MRTCount].RenderTarget = SceneVelocity->GetRenderTargetItem().TargetableTexture;
		OutRenderPassInfo.ColorRenderTargets[MRTCount].ArraySlice = -1;
		OutRenderPassInfo.ColorRenderTargets[MRTCount].MipIndex = 0;
		MRTCount++;
	}
	else
	{
		OutVelocityRTIndex = -1;
	}

	if (bUseGBuffer)
	{
		OutRenderPassInfo.ColorRenderTargets[MRTCount].Action = MakeRenderTargetActions(ColorLoadAction, ERenderTargetStoreAction::EStore);
		OutRenderPassInfo.ColorRenderTargets[MRTCount].RenderTarget = GBufferD->GetRenderTargetItem().TargetableTexture;
		OutRenderPassInfo.ColorRenderTargets[MRTCount].ArraySlice = -1;
		OutRenderPassInfo.ColorRenderTargets[MRTCount].MipIndex = 0;
		MRTCount++;

		if (bAllowStaticLighting)
		{
			check(MRTCount == (bAllocateVelocityGBuffer ? 6 : 5)); // As defined in BasePassPixelShader.usf
			OutRenderPassInfo.ColorRenderTargets[MRTCount].Action = MakeRenderTargetActions(ColorLoadAction, ERenderTargetStoreAction::EStore);
			OutRenderPassInfo.ColorRenderTargets[MRTCount].RenderTarget = GBufferE->GetRenderTargetItem().TargetableTexture;
			OutRenderPassInfo.ColorRenderTargets[MRTCount].ArraySlice = -1;
			OutRenderPassInfo.ColorRenderTargets[MRTCount].MipIndex = 0;
			MRTCount++;
		}
	}

	check(MRTCount <= MaxSimultaneousRenderTargets);
	return MRTCount;
}

int32 FSceneRenderTargets::GetGBufferRenderTargets(ERenderTargetLoadAction ColorLoadAction, FRHIRenderTargetView OutRenderTargets[MaxSimultaneousRenderTargets], int32& OutVelocityRTIndex)
{
	int32 MRTCount = 0;
	OutRenderTargets[MRTCount++] = FRHIRenderTargetView(GetSceneColorSurface(), 0, -1, ColorLoadAction, ERenderTargetStoreAction::EStore);

	const EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(CurrentFeatureLevel);
	const bool bUseGBuffer = IsUsingGBuffers(ShaderPlatform);
	if (bUseGBuffer)
	{
		OutRenderTargets[MRTCount++] = FRHIRenderTargetView(GBufferA->GetRenderTargetItem().TargetableTexture, 0, -1, ColorLoadAction, ERenderTargetStoreAction::EStore);
		OutRenderTargets[MRTCount++] = FRHIRenderTargetView(GBufferB->GetRenderTargetItem().TargetableTexture, 0, -1, ColorLoadAction, ERenderTargetStoreAction::EStore);
		OutRenderTargets[MRTCount++] = FRHIRenderTargetView(GBufferC->GetRenderTargetItem().TargetableTexture, 0, -1, ColorLoadAction, ERenderTargetStoreAction::EStore);
	}

	// The velocity buffer needs to be bound before other optionnal rendertargets (when UseSelectiveBasePassOutputs() is true).
	// Otherwise there is an issue on some AMD hardware where the target does not get updated. Seems to be related to the velocity buffer format as it works fine with other targets.
	if (bAllocateVelocityGBuffer && !IsSimpleForwardShadingEnabled(ShaderPlatform))
	{
		OutVelocityRTIndex = MRTCount;
		check(OutVelocityRTIndex == 4 || (!bUseGBuffer && OutVelocityRTIndex == 1)); // As defined in BasePassPixelShader.usf
		OutRenderTargets[MRTCount++] = FRHIRenderTargetView(SceneVelocity->GetRenderTargetItem().TargetableTexture, 0, -1, ColorLoadAction, ERenderTargetStoreAction::EStore);
	}
	else
	{
		OutVelocityRTIndex = -1;
	}

	if (bUseGBuffer)
	{
		OutRenderTargets[MRTCount++] = FRHIRenderTargetView(GBufferD->GetRenderTargetItem().TargetableTexture, 0, -1, ColorLoadAction, ERenderTargetStoreAction::EStore);

		if (bAllowStaticLighting)
		{
			check(MRTCount == (bAllocateVelocityGBuffer ? 6 : 5)); // As defined in BasePassPixelShader.usf
			OutRenderTargets[MRTCount++] = FRHIRenderTargetView(GBufferE->GetRenderTargetItem().TargetableTexture, 0, -1, ColorLoadAction, ERenderTargetStoreAction::EStore);
		}
	}

	check(MRTCount <= MaxSimultaneousRenderTargets);
	return MRTCount;
}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
int32 FSceneRenderTargets::GetQuadOverdrawUAVIndex(EShaderPlatform Platform, ERHIFeatureLevel::Type FeatureLevel)
{
	if (IsSimpleForwardShadingEnabled(Platform))
	{
		return 1;
	}
	else if (IsForwardShadingEnabled(Platform))
	{
		return FVelocityRendering::BasePassCanOutputVelocity(FeatureLevel) ? 2 : 1;
	}
	else // GBuffer
	{
		return FVelocityRendering::BasePassCanOutputVelocity(FeatureLevel) ? 7 : 6;
	}
}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

void FSceneRenderTargets::SetQuadOverdrawUAV(FRHICommandList& RHICmdList, bool bBindQuadOverdrawBuffers, bool bClearQuadOverdrawBuffers, FRHIRenderPassInfo& OutInfo)
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	const EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(CurrentFeatureLevel);
	if (bBindQuadOverdrawBuffers && AllowDebugViewShaderMode(DVSM_QuadComplexity, ShaderPlatform, CurrentFeatureLevel))
	{
		if (QuadOverdrawBuffer.IsValid() && QuadOverdrawBuffer->GetRenderTargetItem().UAV.IsValid())
		{
			QuadOverdrawIndex = GetQuadOverdrawUAVIndex(ShaderPlatform, CurrentFeatureLevel);

			// Increase the rendertarget count in order to control the bound slot of the UAV.
			check(OutInfo.GetNumColorRenderTargets() <= QuadOverdrawIndex);
			OutInfo.UAVIndex = QuadOverdrawIndex;
			OutInfo.UAVs[OutInfo.NumUAVs++] = QuadOverdrawBuffer->GetRenderTargetItem().UAV;

			if (bClearQuadOverdrawBuffers)
			{
				// Clear to default value
				const uint32 ClearValue[4] = { 0, 0, 0, 0 };
				ClearUAV(RHICmdList, QuadOverdrawBuffer->GetRenderTargetItem(), ClearValue);
				RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToGfx, QuadOverdrawBuffer->GetRenderTargetItem().UAV);
			}
		}
	}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
}

void FSceneRenderTargets::BindVirtualTextureFeedbackUAV(FRHIRenderPassInfo& RPInfo)
{
	// must match VT_FEEDBACK_REGISTER in VirtualTextureCommon.ush
	static const int32 VT_FEEDBACK_REGISTER = 7;

	checkf(VirtualTextureFeedback.FeedbackTextureGPU, TEXT("Invalid attempt to render VT feedback after it has already been transferred to CPU, need to adjust location of TransferGPUToCPU"));
	check(RPInfo.GetNumColorRenderTargets() <= VT_FEEDBACK_REGISTER);
	RPInfo.UAVIndex = VT_FEEDBACK_REGISTER;
	RPInfo.UAVs[RPInfo.NumUAVs++] = VirtualTextureFeedback.FeedbackTextureGPU->GetRenderTargetItem().UAV;
}

void FSceneRenderTargets::BeginRenderingGBuffer(FRHICommandList& RHICmdList, ERenderTargetLoadAction ColorLoadAction, ERenderTargetLoadAction DepthLoadAction, FExclusiveDepthStencil::Type DepthStencilAccess, bool bBindQuadOverdrawBuffers, bool bClearQuadOverdrawBuffers, const FLinearColor& ClearColor/*=(0,0,0,1)*/, bool bIsWireframe)
{
	check(RHICmdList.IsOutsideRenderPass());

	SCOPED_DRAW_EVENT(RHICmdList, BeginRenderingGBuffer);
	check(CurrentFeatureLevel >= ERHIFeatureLevel::SM4);
	AllocSceneColor(RHICmdList);

	const ERenderTargetStoreAction DepthStoreAction = (DepthStencilAccess & FExclusiveDepthStencil::DepthWrite) ? ERenderTargetStoreAction::EStore : ERenderTargetStoreAction::ENoAction;

	bool bClearColor = ColorLoadAction == ERenderTargetLoadAction::EClear;
	bool bClearDepth = DepthLoadAction == ERenderTargetLoadAction::EClear;

	//if the desired clear color doesn't match the bound hwclear value, or there isn't one at all (editor code)
	//then we need to fall back to a shader clear.
	const FTextureRHIRef& SceneColorTex = GetSceneColorSurface();
	bool bShaderClear = false;
	if (bClearColor)
	{
		if (!SceneColorTex->HasClearValue() || (ClearColor != SceneColorTex->GetClearColor()))
		{
			ColorLoadAction = ERenderTargetLoadAction::ENoAction;
			bShaderClear = true;
		}
		else
		{
			bGBuffersFastCleared = true;
		}
	}

	int32 VelocityRTIndex = -1;
	FRHIRenderPassInfo RPInfo;
	int32 MRTCount = FillGBufferRenderPassInfo(ColorLoadAction, RPInfo, VelocityRTIndex);

	//make sure our conditions for shader clear fallback are valid.
	check(RPInfo.ColorRenderTargets[0].RenderTarget == SceneColorTex);

	if (bClearDepth)
	{
		bSceneDepthCleared = true;
	}

	SetQuadOverdrawUAV(RHICmdList, bBindQuadOverdrawBuffers, bClearQuadOverdrawBuffers, RPInfo);

	// Stencil always has to be store or certain VK drivers will leave the attachment in an undefined state.
	RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(MakeRenderTargetActions(DepthLoadAction, DepthStoreAction), MakeRenderTargetActions(DepthLoadAction, ERenderTargetStoreAction::EStore));
	RPInfo.DepthStencilRenderTarget.DepthStencilTarget = GetSceneDepthSurface();
	RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = DepthStencilAccess;

	if(UseVirtualTexturing(CurrentFeatureLevel) && !bBindQuadOverdrawBuffers )
	{
		BindVirtualTextureFeedbackUAV(RPInfo);
	}	

	RHICmdList.BeginRenderPass(RPInfo, TEXT("GBuffer"));

	if (bShaderClear)
	{
		FLinearColor ClearColors[MaxSimultaneousRenderTargets];
		FRHITexture* Textures[MaxSimultaneousRenderTargets];
		ClearColors[0] = ClearColor;
		Textures[0] = RPInfo.ColorRenderTargets[0].RenderTarget;
		for (int32 i = 1; i < MRTCount; ++i)
		{
			ClearColors[i] = RPInfo.ColorRenderTargets[i].RenderTarget->GetClearColor();
			Textures[i] = RPInfo.ColorRenderTargets[i].RenderTarget;
		}
		//depth/stencil should have been handled by the fast clear.  only color for RT0 can get changed.
		DrawClearQuadMRT(RHICmdList, true, MRTCount, ClearColors, false, 0, false, 0);
	}

	//bind any clear data that won't be bound automatically
	bool bBindClearColor = !bClearColor && bGBuffersFastCleared;
	bool bBindClearDepth = !bClearDepth && bSceneDepthCleared;
	RHICmdList.BindClearMRTValues(bBindClearColor, bBindClearDepth, bBindClearDepth);

	if (bIsWireframe)
	{
		RHICmdList.EndRenderPass();

		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

		RPInfo = FRHIRenderPassInfo();

		RPInfo.ColorRenderTargets[0].Action = MakeRenderTargetActions(ColorLoadAction, ERenderTargetStoreAction::EStore);
		RPInfo.ColorRenderTargets[0].RenderTarget = SceneContext.GetEditorPrimitivesColor(RHICmdList);
		RPInfo.ColorRenderTargets[0].ArraySlice = -1;
		RPInfo.ColorRenderTargets[0].MipIndex = 0;

		//FRHIRenderPassInfo RPInfo(SceneContext.GetEditorPrimitivesColor(RHICmdList), ColorLoadAction);
		RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(MakeRenderTargetActions(DepthLoadAction, ERenderTargetStoreAction::EStore), MakeRenderTargetActions(DepthLoadAction, ERenderTargetStoreAction::EStore));
		RPInfo.DepthStencilRenderTarget.DepthStencilTarget = SceneContext.GetEditorPrimitivesDepth(RHICmdList);
		RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthWrite_StencilWrite;
		RHICmdList.BeginRenderPass(RPInfo, TEXT("Wireframe"));	
	}
}

void FSceneRenderTargets::FinishGBufferPassAndResolve(FRHICommandListImmediate& RHICmdList)
{
	RHICmdList.EndRenderPass();

	const EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(CurrentFeatureLevel);
	if (IsSimpleForwardShadingEnabled(ShaderPlatform))
	{
		// Currently nothing to do here for simple forward shading.
		return;
	}

	int32 VelocityRTIndex;
	FRHIRenderTargetView RenderTargets[MaxSimultaneousRenderTargets];
	int32 NumMRTs = GetGBufferRenderTargets(ERenderTargetLoadAction::ELoad, RenderTargets, VelocityRTIndex);

	FResolveParams ResolveParams;

	// Skip the SceneColor resolve when using forward shading as this is done after this function is called.
	int32 i = IsForwardShadingEnabled(ShaderPlatform) ? 1 : 0;

	for ( ; i < NumMRTs; ++i)
	{
		 // When the basepass outputs to the velocity buffer, don't resolve it yet if selective outputs are enabled, as it will be resolved after the velocity pass.
		if (i != VelocityRTIndex || !IsUsingSelectiveBasePassOutputs(ShaderPlatform))
		{
			RHICmdList.CopyToResolveTarget(RenderTargets[i].Texture, RenderTargets[i].Texture, ResolveParams);
		}
	}

	QuadOverdrawIndex = INDEX_NONE;
}

int32 FSceneRenderTargets::GetNumGBufferTargets() const
{
	int32 NumGBufferTargets = 1;

	if (CurrentFeatureLevel >= ERHIFeatureLevel::SM4)
	{
		const EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(CurrentFeatureLevel);
		if (IsUsingGBuffers(ShaderPlatform))
		{
			// This needs to match TBasePassPixelShaderBaseType::ModifyCompilationEnvironment()
			NumGBufferTargets = bAllowStaticLighting ? 6 : 5;
		}

		if (bAllocateVelocityGBuffer)
		{
			++NumGBufferTargets;
		}
	}
	return NumGBufferTargets;
}

void FSceneRenderTargets::AllocSceneColor(FRHICommandList& RHICmdList)
{
	TRefCountPtr<IPooledRenderTarget>& SceneColorTarget = GetSceneColorForCurrentShadingPath();
	if (SceneColorTarget && 
		SceneColorTarget->GetRenderTargetItem().TargetableTexture->HasClearValue() && 
		!(SceneColorTarget->GetRenderTargetItem().TargetableTexture->GetClearBinding() == DefaultColorClear))
	{
		const FLinearColor CurrentClearColor = SceneColorTarget->GetRenderTargetItem().TargetableTexture->GetClearBinding().GetClearColor();
		const FLinearColor NewClearColor = DefaultColorClear.GetClearColor();
		UE_LOG(LogRenderer, Log, TEXT("Releasing previous color target to switch default clear from: %f %f %f %f to: %f %f %f %f"), 
			CurrentClearColor.R, 
			CurrentClearColor.G, 
			CurrentClearColor.B, 
			CurrentClearColor.A, 
			NewClearColor.R, 
			NewClearColor.G, 
			NewClearColor.B, 
			NewClearColor.A);
		SceneColorTarget.SafeRelease();
	}

	if (GetSceneColorForCurrentShadingPath())
	{
		return;
	}

	EPixelFormat SceneColorBufferFormat = GetSceneColorFormat();

	// Mobile non-mobileHDR is the only platform rendering to a true sRGB buffer natively
	bool MobileHWsRGB = IsMobileColorsRGB() && IsMobilePlatform(GShaderPlatformForFeatureLevel[CurrentFeatureLevel]);

	// Create the scene color.
	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, SceneColorBufferFormat, DefaultColorClear, TexCreate_None, TexCreate_RenderTargetable, false));
		Desc.Flags |= GFastVRamConfig.SceneColor;
		Desc.NumSamples = GetNumSceneColorMSAASamples(CurrentFeatureLevel);

		if (CurrentFeatureLevel >= ERHIFeatureLevel::SM5 && Desc.NumSamples == 1)
		{
			// GCNPerformanceTweets.pdf Tip 37: Warning: Causes additional synchronization between draw calls when using a render target allocated with this flag, use sparingly
			Desc.TargetableFlags |= TexCreate_UAV;
		}
		Desc.Flags |= MobileHWsRGB ? TexCreate_SRGB : 0;

		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, GetSceneColorForCurrentShadingPath(), GetSceneColorTargetName(CurrentShadingPath));
	}

	check(GetSceneColorForCurrentShadingPath());
}

void FSceneRenderTargets::AllocMobileMultiViewSceneColor(FRHICommandList& RHICmdList, const int32 ScaleFactor)
{
	// For mono support. 
	// Ensure we clear alpha to 0. We use alpha to tag which pixels had objects rendered into them so we can mask them out for the mono pass
	if (MobileMultiViewSceneColor && !(MobileMultiViewSceneColor->GetRenderTargetItem().TargetableTexture->GetClearBinding() == DefaultColorClear))
	{
		MobileMultiViewSceneColor.SafeRelease();
	}

	if (!MobileMultiViewSceneColor)
	{
		const EPixelFormat SceneColorBufferFormat = GetSceneColorFormat();
		const FIntPoint MultiViewBufferSize(BufferSize.X / ScaleFactor, BufferSize.Y);

		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(MultiViewBufferSize, SceneColorBufferFormat, DefaultColorClear, TexCreate_None, TexCreate_RenderTargetable, false));
		Desc.NumSamples = GetNumSceneColorMSAASamples(CurrentFeatureLevel);
		Desc.ArraySize = 2;
		Desc.bIsArray = true;
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, MobileMultiViewSceneColor, TEXT("MobileMultiViewSceneColor"));
	}
	check(MobileMultiViewSceneColor);
}

void FSceneRenderTargets::AllocMobileMultiViewDepth(FRHICommandList& RHICmdList, const int32 ScaleFactor)
{
	// For mono support. We change the default depth clear value to the mono clip plane to clip the stereo portion of the frustum.
	if (MobileMultiViewSceneDepthZ && !(MobileMultiViewSceneDepthZ->GetRenderTargetItem().TargetableTexture->GetClearBinding() == DefaultDepthClear))
	{
		MobileMultiViewSceneDepthZ.SafeRelease();
	}

	if (!MobileMultiViewSceneDepthZ)
	{
		const FIntPoint MultiViewBufferSize(BufferSize.X / ScaleFactor, BufferSize.Y);

		// Using the result of GetDepthFormat() without stencil due to packed depth-stencil not working in array frame buffers.
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(MultiViewBufferSize, PF_D24, DefaultDepthClear, TexCreate_None, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource | TexCreate_InputAttachmentRead, false));
		Desc.Flags |= TexCreate_FastVRAM;
		Desc.NumSamples = GetNumSceneColorMSAASamples(CurrentFeatureLevel);
		Desc.ArraySize = 2;
		Desc.bIsArray = true;
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, MobileMultiViewSceneDepthZ, TEXT("MobileMultiViewSceneDepthZ"));
	}
	check(MobileMultiViewSceneDepthZ);
}

void FSceneRenderTargets::AllocLightAttenuation(FRHICommandList& RHICmdList)
{
	if(LightAttenuation && !GFastVRamConfig.bDirty )
	{
		// no work needed
		return;
	}

	check(IsInRenderingThread());

	// Create a texture to store the resolved light attenuation values, and a render-targetable surface to hold the unresolved light attenuation values.
	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_B8G8R8A8, FClearValueBinding::White, TexCreate_None, TexCreate_RenderTargetable, false));
		Desc.NumSamples = GetNumSceneColorMSAASamples(CurrentFeatureLevel);
		Desc.Flags |= GFastVRamConfig.LightAttenuation;
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, LightAttenuation, TEXT("LightAttenuation"));
	}

	// otherwise we have a severe problem
	check(LightAttenuation);
}

void FSceneRenderTargets::ReleaseGBufferTargets()
{
	GBufferA.SafeRelease();
	GBufferB.SafeRelease();
	GBufferC.SafeRelease();
	GBufferD.SafeRelease();
	GBufferE.SafeRelease();
	SceneVelocity.SafeRelease();
}

void FSceneRenderTargets::PreallocGBufferTargets()
{
	bAllocateVelocityGBuffer = FVelocityRendering::BasePassCanOutputVelocity(CurrentFeatureLevel);
}

void FSceneRenderTargets::GetGBufferADesc(FPooledRenderTargetDesc& Desc) const
{
	// good to see the quality loss due to precision in the gbuffer
	const bool bHighPrecisionGBuffers = (CurrentGBufferFormat >= EGBufferFormat::Force16BitsPerChannel);
	// good to profile the impact of non 8 bit formats
	const bool bEnforce8BitPerChannel = (CurrentGBufferFormat == EGBufferFormat::Force8BitsPerChannel);

	// Create the world-space normal g-buffer.
	{
		EPixelFormat NormalGBufferFormat = bHighPrecisionGBuffers ? PF_FloatRGBA : PF_A2B10G10R10;

		if (bEnforce8BitPerChannel)
		{
			NormalGBufferFormat = PF_B8G8R8A8;
		}
		else if (CurrentGBufferFormat == EGBufferFormat::HighPrecisionNormals)
		{
			NormalGBufferFormat = PF_FloatRGBA;
		}

		Desc = FPooledRenderTargetDesc::Create2DDesc(BufferSize, NormalGBufferFormat, FClearValueBinding::Transparent, TexCreate_None, TexCreate_RenderTargetable, false);
		Desc.Flags |= GFastVRamConfig.GBufferA;
	}
}

void FSceneRenderTargets::AllocGBufferTargets(FRHICommandList& RHICmdList)
{	
	// AdjustGBufferRefCount +1 doesn't match -1 (within the same frame)
	ensure(GBufferRefCount == 0);

	if (GBufferA)
	{
		// no work needed
		return;
	}

	// create GBuffer on demand so it can be shared with other pooled RT
	const EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(CurrentFeatureLevel);
	const bool bUseGBuffer = IsUsingGBuffers(ShaderPlatform);
	const bool bCanReadGBufferUniforms = (bUseGBuffer || IsSimpleForwardShadingEnabled(ShaderPlatform)) && CurrentFeatureLevel >= ERHIFeatureLevel::SM4;
	if (bUseGBuffer)
	{
		// good to see the quality loss due to precision in the gbuffer
		const bool bHighPrecisionGBuffers = (CurrentGBufferFormat >= EGBufferFormat::Force16BitsPerChannel);
		// good to profile the impact of non 8 bit formats
		const bool bEnforce8BitPerChannel = (CurrentGBufferFormat == EGBufferFormat::Force8BitsPerChannel);

		// Create the world-space normal g-buffer.
		{
			FPooledRenderTargetDesc Desc;
			GetGBufferADesc(Desc);
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, GBufferA, TEXT("GBufferA"));
		}

		// Create the specular color and power g-buffer.
		{
			const EPixelFormat SpecularGBufferFormat = bHighPrecisionGBuffers ? PF_FloatRGBA : PF_B8G8R8A8;

			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, SpecularGBufferFormat, FClearValueBinding::Transparent, TexCreate_None, TexCreate_RenderTargetable, false));
			Desc.Flags |= GFastVRamConfig.GBufferB;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, GBufferB, TEXT("GBufferB"));
		}

		// Create the diffuse color g-buffer.
		{
			const EPixelFormat DiffuseGBufferFormat = bHighPrecisionGBuffers ? PF_FloatRGBA : PF_B8G8R8A8;
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, DiffuseGBufferFormat, FClearValueBinding::Transparent, TexCreate_SRGB, TexCreate_RenderTargetable, false));
			Desc.Flags |= GFastVRamConfig.GBufferC;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, GBufferC, TEXT("GBufferC"));
		}

		// Create the mask g-buffer (e.g. SSAO, subsurface scattering, wet surface mask, skylight mask, ...).
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_B8G8R8A8, FClearValueBinding::Transparent, TexCreate_None, TexCreate_RenderTargetable, false));
			Desc.Flags |= GFastVRamConfig.GBufferD;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, GBufferD, TEXT("GBufferD"));
		}

		if (bAllowStaticLighting)
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_B8G8R8A8, FClearValueBinding::Transparent, TexCreate_None, TexCreate_RenderTargetable, false));
			Desc.Flags |= GFastVRamConfig.GBufferE;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, GBufferE, TEXT("GBufferE"));
		}

		// otherwise we have a severe problem
		check(GBufferA);
	}

	if (bAllocateVelocityGBuffer)
	{
		FPooledRenderTargetDesc VelocityRTDesc = FVelocityRendering::GetRenderTargetDesc();
		VelocityRTDesc.Flags |= GFastVRamConfig.GBufferVelocity;
		GRenderTargetPool.FindFreeElement(RHICmdList, VelocityRTDesc, SceneVelocity, TEXT("GBufferVelocity"));
	}

	GBufferRefCount = 1;
}

const TRefCountPtr<IPooledRenderTarget>& FSceneRenderTargets::GetSceneColor() const
{
	if (!GetSceneColorForCurrentShadingPath())
	{
		// to avoid log/ensure spam
		static bool bFirst = true;
		if(bFirst)
		{
			bFirst = false;

			// the first called should be AllocSceneColor(), contact MartinM if that happens
			ensure(GetSceneColorForCurrentShadingPath());
		}

		return GSystemTextures.BlackDummy;
	}

	return GetSceneColorForCurrentShadingPath();
}

bool FSceneRenderTargets::IsSceneColorAllocated() const
{
	return GetSceneColorForCurrentShadingPath() != 0;
}

TRefCountPtr<IPooledRenderTarget>& FSceneRenderTargets::GetSceneColor()
{
	if (!GetSceneColorForCurrentShadingPath())
	{
		// to avoid log/ensure spam
		static bool bFirst = true;
		if(bFirst)
		{
			bFirst = false;

			// the first called should be AllocSceneColor(), contact MartinM if that happens
			ensure(GetSceneColorForCurrentShadingPath());
		}

		return GSystemTextures.BlackDummy;
	}

	return GetSceneColorForCurrentShadingPath();
}

void FSceneRenderTargets::SetSceneColor(IPooledRenderTarget* In)
{
	check(CurrentShadingPath < EShadingPath::Num);
	SceneColor[(int32)GetSceneColorFormatType()] = In;
}

void FSceneRenderTargets::SetLightAttenuation(IPooledRenderTarget* In)
{
	LightAttenuation = In;
}

const TRefCountPtr<IPooledRenderTarget>& FSceneRenderTargets::GetLightAttenuation() const
{
	if(!LightAttenuation)
	{
		// to avoid log/ensure spam
		static bool bFirst = true;
		if(bFirst)
		{
			bFirst = false;

			// First we need to call AllocLightAttenuation()
			ensure(LightAttenuation);
		}

		return GSystemTextures.WhiteDummy;
	}

	return LightAttenuation;
}

TRefCountPtr<IPooledRenderTarget>& FSceneRenderTargets::GetLightAttenuation()
{
	if(!LightAttenuation)
	{
		// to avoid log/ensure spam
		static bool bFirst = true;
		if(bFirst)
		{
			bFirst = false;

			// the first called should be AllocLightAttenuation()
			ensure(LightAttenuation);
		}

		return GSystemTextures.WhiteDummy;
	}

	return LightAttenuation;
}

void FSceneRenderTargets::AdjustGBufferRefCount(FRHICommandList& RHICmdList, int Delta)
{
	if (Delta > 0 && GBufferRefCount == 0)
	{
		AllocGBufferTargets(RHICmdList);
	}
	else
	{
		GBufferRefCount += Delta;

		if (GBufferRefCount == 0)
		{
			ReleaseGBufferTargets();
		}
	}	
}

bool FSceneRenderTargets::BeginRenderingCustomDepth(FRHICommandListImmediate& RHICmdList, bool bPrimitives)
{
	check(!RHICmdList.IsInsideRenderPass());

	IPooledRenderTarget* CustomDepthRenderTarget = RequestCustomDepth(RHICmdList, bPrimitives);

	if(CustomDepthRenderTarget)
	{
		SCOPED_DRAW_EVENT(RHICmdList, BeginRenderingCustomDepth);

		FRHIRenderPassInfo RPInfo;

		const bool bWritesCustomStencilValues = IsCustomDepthPassWritingStencil();
		const bool bRequiresStencilColorTarget = (bWritesCustomStencilValues && CurrentFeatureLevel <= ERHIFeatureLevel::ES3_1);

		int32 NumColorTargets = 0;
		FRHIRenderTargetView ColorView = {};
		if (bRequiresStencilColorTarget)
		{
			checkSlow(MobileCustomStencil.IsValid());

			RPInfo.ColorRenderTargets[0].Action = ERenderTargetActions::Clear_Store;
			RPInfo.ColorRenderTargets[0].ArraySlice = -1;
			RPInfo.ColorRenderTargets[0].MipIndex = 0;
			RPInfo.ColorRenderTargets[0].RenderTarget = MobileCustomStencil->GetRenderTargetItem().ShaderResourceTexture;

			ColorView =	FRHIRenderTargetView(MobileCustomStencil->GetRenderTargetItem().ShaderResourceTexture, 0, -1, ERenderTargetLoadAction::EClear, ERenderTargetStoreAction::EStore);
			NumColorTargets = 1;
		}

		RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(ERenderTargetActions::Clear_Store, ERenderTargetActions::Load_Store);
		if (bWritesCustomStencilValues)
		{
			RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(ERenderTargetActions::Clear_Store, ERenderTargetActions::Clear_Store);
		}

		RPInfo.DepthStencilRenderTarget.DepthStencilTarget = CustomDepthRenderTarget->GetRenderTargetItem().ShaderResourceTexture;
		RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthWrite_StencilWrite;

		check(RPInfo.DepthStencilRenderTarget.DepthStencilTarget->GetStencilClearValue() == 0);

		// #todo-renderpasses need ResolveRect here
		//RPInfo.DepthStencilRenderTarget.ResolveTarget = CustomDepth->GetRenderTargetItem().ShaderResourceTexture;
		RHICmdList.BeginRenderPass(RPInfo, TEXT("BeginRenderCustomDepth"));

		return true;
	}

	return false;
}

void FSceneRenderTargets::FinishRenderingCustomDepth(FRHICommandListImmediate& RHICmdList, const FResolveRect& ResolveRect)
{
	check(RHICmdList.IsInsideRenderPass());

	SCOPED_DRAW_EVENT(RHICmdList, FinishRenderingCustomDepth);

	RHICmdList.EndRenderPass();
	
	if (CurrentFeatureLevel <= ERHIFeatureLevel::ES3_1 && IsCustomDepthPassWritingStencil() && MobileCustomStencil.IsValid())
	{
		RHICmdList.CopyToResolveTarget(MobileCustomStencil->GetRenderTargetItem().TargetableTexture, MobileCustomStencil->GetRenderTargetItem().ShaderResourceTexture, FResolveParams(ResolveRect));
	}

	RHICmdList.CopyToResolveTarget(CustomDepth->GetRenderTargetItem().TargetableTexture, CustomDepth->GetRenderTargetItem().ShaderResourceTexture, FResolveParams(ResolveRect));

	bCustomDepthIsValid = true;
}

void FSceneRenderTargets::BeginRenderingPrePass(FRHICommandList& RHICmdList, bool bPerformClear)
{
	check(RHICmdList.IsOutsideRenderPass());

	SCOPED_DRAW_EVENT(RHICmdList, BeginRenderingPrePass);

	FTexture2DRHIRef DepthTarget = GetSceneDepthSurface();
	
	// No color target bound for the prepass.
	FRHIRenderPassInfo RPInfo;
	RPInfo.DepthStencilRenderTarget.DepthStencilTarget = DepthTarget;
	RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthWrite_StencilWrite;
	
	if (bPerformClear)
	{				
		// Clear the depth buffer.
		// Note, this is a reversed Z depth surface, so 0.0f is the far plane.
		RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(ERenderTargetActions::Clear_Store, ERenderTargetActions::Clear_Store);
		bSceneDepthCleared = true;	
	}
	else
	{
		// Set the scene depth surface and a DUMMY buffer as color buffer
		// (as long as it's the same dimension as the depth buffer),	
		RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(ERenderTargetActions::Load_Store, ERenderTargetActions::Load_Store);
	}
		
	RHICmdList.BeginRenderPass(RPInfo, TEXT("BeginRenderingPrePass"));
		
	if (!bPerformClear)
	{
		// Needs to be called after we start a renderpass.
		RHICmdList.BindClearMRTValues(false, true, true);
	}
}

void FSceneRenderTargets::FinishRenderingPrePass(FRHICommandListImmediate& RHICmdList)
{
	check(RHICmdList.IsInsideRenderPass());

	SCOPED_DRAW_EVENT(RHICmdList, FinishRenderingPrePass);
	RHICmdList.EndRenderPass();

	GVisualizeTexture.SetCheckPoint(RHICmdList, SceneDepthZ);
}

void FSceneRenderTargets::BeginRenderingSceneAlphaCopy(FRHICommandListImmediate& RHICmdList)
{
	check(!RHICmdList.IsInsideRenderPass());

	SCOPED_DRAW_EVENT(RHICmdList, BeginRenderingSceneAlphaCopy);
	GVisualizeTexture.SetCheckPoint(RHICmdList, SceneAlphaCopy);

	FRHIRenderPassInfo RPInfo(GetSceneAlphaCopySurface(), ERenderTargetActions::Load_Store, SceneAlphaCopy->GetRenderTargetItem().ShaderResourceTexture);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("BeginRenderingSceneAlphaCopy"));
}

void FSceneRenderTargets::FinishRenderingSceneAlphaCopy(FRHICommandListImmediate& RHICmdList)
{
	check(RHICmdList.IsInsideRenderPass());

	SCOPED_DRAW_EVENT(RHICmdList, FinishRenderingSceneAlphaCopy);
	RHICmdList.EndRenderPass();
	GVisualizeTexture.SetCheckPoint(RHICmdList, SceneAlphaCopy);
}


void FSceneRenderTargets::BeginRenderingLightAttenuation(FRHICommandList& RHICmdList, bool bClearToWhite)
{
	SCOPED_CONDITIONAL_DRAW_EVENT(RHICmdList, ClearLightAttenuation, bClearToWhite);
	SCOPED_CONDITIONAL_DRAW_EVENT(RHICmdList, BeginRenderingLightAttenuation, !bClearToWhite);

	AllocLightAttenuation(RHICmdList);

	GVisualizeTexture.SetCheckPoint(RHICmdList, GetLightAttenuation());

	FRHIRenderPassInfo RPInfo;

	// Set the light attenuation surface as the render target, and the scene depth buffer as the depth-stencil surface.
	if (bClearToWhite)
	{
		RPInfo.ColorRenderTargets[0].RenderTarget = GetLightAttenuationSurface();
		RPInfo.ColorRenderTargets[0].Action = ERenderTargetActions::Clear_Store;
		RPInfo.ColorRenderTargets[0].ArraySlice = -1;
		RPInfo.ColorRenderTargets[0].MipIndex = 0;
		RPInfo.ColorRenderTargets[0].ResolveTarget = LightAttenuation->GetRenderTargetItem().ShaderResourceTexture;
		RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(ERenderTargetActions::Load_DontStore, ERenderTargetActions::Load_DontStore);
		RPInfo.DepthStencilRenderTarget.DepthStencilTarget = GetSceneDepthSurface();
		RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthRead_StencilWrite;
		RPInfo.DepthStencilRenderTarget.ResolveTarget = nullptr;
	}
	else
	{
		RPInfo.ColorRenderTargets[0].RenderTarget = GetLightAttenuationSurface();
		RPInfo.ColorRenderTargets[0].Action = ERenderTargetActions::Load_Store;
		RPInfo.ColorRenderTargets[0].ArraySlice = -1;
		RPInfo.ColorRenderTargets[0].MipIndex = 0;
		RPInfo.ColorRenderTargets[0].ResolveTarget = LightAttenuation->GetRenderTargetItem().ShaderResourceTexture;
		RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(ERenderTargetActions::Load_Store, ERenderTargetActions::Load_Store);
		RPInfo.DepthStencilRenderTarget.DepthStencilTarget = GetSceneDepthSurface();
		RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthRead_StencilWrite;
		RPInfo.DepthStencilRenderTarget.ResolveTarget = nullptr;
	}

	TransitionRenderPassTargets(RHICmdList, RPInfo);

	RHICmdList.BeginRenderPass(RPInfo, TEXT("BeginRenderingLightAttenuation"));
}

void FSceneRenderTargets::FinishRenderingLightAttenuation(FRHICommandList& RHICmdList)
{
	SCOPED_DRAW_EVENT(RHICmdList, FinishRenderingLightAttenuation);

	RHICmdList.EndRenderPass();
	
	GVisualizeTexture.SetCheckPoint(RHICmdList, GetLightAttenuation());
}

TRefCountPtr<IPooledRenderTarget>& FSceneRenderTargets::GetSeparateTranslucency(FRHICommandList& RHICmdList, FIntPoint Size)
{
	if (!SeparateTranslucencyRT || SeparateTranslucencyRT->GetDesc().Extent != Size)
	{
		uint32 Flags = TexCreate_RenderTargetable;

		// Create the SeparateTranslucency render target (alpha is needed to lerping)
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(Size, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_None, Flags, false));
		Desc.Flags |= GFastVRamConfig.SeparateTranslucency;
		Desc.AutoWritable = false;
		Desc.NumSamples = GetNumSceneColorMSAASamples(CurrentFeatureLevel);
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SeparateTranslucencyRT, TEXT("SeparateTranslucency"));
	}
	return SeparateTranslucencyRT;
}

TRefCountPtr<IPooledRenderTarget>& FSceneRenderTargets::GetDownsampledTranslucencyDepth(FRHICommandList& RHICmdList, FIntPoint Size)
{
	if (!DownsampledTranslucencyDepthRT || DownsampledTranslucencyDepthRT->GetDesc().Extent != Size)
	{
		// Create the SeparateTranslucency depth render target 
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(Size, PF_DepthStencil, FClearValueBinding::None, TexCreate_None, TexCreate_DepthStencilTargetable, false));
		Desc.NumSamples = GetNumSceneColorMSAASamples(CurrentFeatureLevel);
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, DownsampledTranslucencyDepthRT, TEXT("SeparateTranslucencyDepth"));
	}
	return DownsampledTranslucencyDepthRT;
}

void FSceneRenderTargets::BeginRenderingTranslucency(FRHICommandList& RHICmdList, const FViewInfo& View, const FSceneRenderer& Renderer, bool bFirstTimeThisFrame)
{
	check(RHICmdList.IsOutsideRenderPass());

	// Use the scene color buffer (similar logic to BeginRenderingSceneColor, except VT feedback UAV is also bound)
	SCOPED_DRAW_EVENT(RHICmdList, BeginRenderingTranslucency);
	AllocSceneColor(RHICmdList);

	ERenderTargetLoadAction ColorLoadAction, DepthLoadAction, StencilLoadAction;
	ERenderTargetStoreAction ColorStoreAction, DepthStoreAction, StencilStoreAction;
	DecodeRenderTargetMode(ESimpleRenderTargetMode::EExistingColorAndDepth, ColorLoadAction, ColorStoreAction, DepthLoadAction, DepthStoreAction, StencilLoadAction, StencilStoreAction, FExclusiveDepthStencil::DepthRead_StencilWrite);

	FRHIRenderPassInfo RPInfo(GetSceneColorSurface(), MakeRenderTargetActions(ColorLoadAction, ColorStoreAction));
	RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(MakeRenderTargetActions(DepthLoadAction, DepthStoreAction), MakeRenderTargetActions(StencilLoadAction, StencilStoreAction));
	RPInfo.DepthStencilRenderTarget.DepthStencilTarget = GetSceneDepthSurface();
	RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthRead_StencilWrite;

	if (UseVirtualTexturing(CurrentFeatureLevel))
	{
		BindVirtualTextureFeedbackUAV(RPInfo);
	}

	TransitionRenderPassTargets(RHICmdList, RPInfo);

	RHICmdList.BeginRenderPass(RPInfo, TEXT("BeginRenderingTranslucency"));

	if (bFirstTimeThisFrame)
	{
		// Clear the stencil buffer for ResponsiveAA
		DrawClearQuad(RHICmdList, false, FLinearColor(), false, 0, true, 0);
	}

	// viewport to match view size
	if (View.IsInstancedStereoPass())
	{
		if (View.bIsMultiViewEnabled)
		{
			const FViewInfo* LeftView = static_cast<const FViewInfo*>(View.Family->Views[0]);
			const FViewInfo* RightView = static_cast<const FViewInfo*>(View.Family->Views[1]);

			const uint32 LeftMinX = LeftView->ViewRect.Min.X;
			const uint32 LeftMaxX = LeftView->ViewRect.Max.X;
			const uint32 RightMinX = RightView->ViewRect.Min.X;
			const uint32 RightMaxX = RightView->ViewRect.Max.X;

			const uint32 LeftMaxY = LeftView->ViewRect.Max.Y;
			const uint32 RightMaxY = RightView->ViewRect.Max.Y;

			RHICmdList.SetStereoViewport(LeftMinX, RightMinX, 0, 0, 0.0f, LeftMaxX, RightMaxX, LeftMaxY, RightMaxY, 1.0f);
		}
		else
		{
			RHICmdList.SetViewport(0, 0, 0, Renderer.InstancedStereoWidth, View.ViewRect.Max.Y, 1);
		}
	}
	else
	{
		RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
	}
}

void FSceneRenderTargets::FinishRenderingTranslucency(FRHICommandList& RHICmdList)
{
	// This is a nop on purpose for consistency's sake
}

void FSceneRenderTargets::BeginRenderingSeparateTranslucency(FRHICommandList& RHICmdList, const FViewInfo& View, const FSceneRenderer& Renderer, bool bFirstTimeThisFrame)
{
	check(RHICmdList.IsOutsideRenderPass());

	bSeparateTranslucencyPass = true;

	SCOPED_DRAW_EVENT(RHICmdList, BeginSeparateTranslucency);

	TRefCountPtr<IPooledRenderTarget>* SeparateTranslucency;
	if (bSnapshot)
	{
		check(SeparateTranslucencyRT.GetReference());
		SeparateTranslucency = &SeparateTranslucencyRT;
	}
	else
	{
		SeparateTranslucency = &GetSeparateTranslucency(RHICmdList, SeparateTranslucencyBufferSize);
	}
	const FTexture2DRHIRef &SeparateTranslucencyDepth = SeparateTranslucencyScale < 1.0f ? (const FTexture2DRHIRef&)GetDownsampledTranslucencyDepth(RHICmdList, SeparateTranslucencyBufferSize)->GetRenderTargetItem().TargetableTexture : GetSceneDepthSurface();

	check((*SeparateTranslucency)->GetRenderTargetItem().TargetableTexture->GetClearColor() == FLinearColor::Black);
	FRHIRenderPassInfo RPInfo((*SeparateTranslucency)->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::Load_Store);
	// #todo-renderpass do we ever want to write to depth in these passes?
	// when do we write to stencil?
	RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(ERenderTargetActions::Load_DontStore, ERenderTargetActions::Load_Store);
	RPInfo.DepthStencilRenderTarget.DepthStencilTarget = SeparateTranslucencyDepth;
	RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthRead_StencilWrite;
	RPInfo.DepthStencilRenderTarget.ResolveTarget = nullptr;

	if (bFirstTimeThisFrame)
	{
		// Clear the color target the first pass through and re-use after
		RPInfo.ColorRenderTargets[0].Action = ERenderTargetActions::Clear_Store;
	}
	
	if (UseVirtualTexturing(CurrentFeatureLevel))
	{
		BindVirtualTextureFeedbackUAV(RPInfo);
	}

	RHICmdList.BeginRenderPass(RPInfo, TEXT("BeginRenderingSeparateTranslucency"));

	if (!bFirstTimeThisFrame)
	{
		// Clear the stencil buffer for ResponsiveAA
		RHICmdList.BindClearMRTValues(true, false, true);
	}

	// viewport to match view size
	if (View.IsInstancedStereoPass())
	{
		if (View.bIsMultiViewEnabled)
		{
			const FViewInfo* LeftView = static_cast<const FViewInfo*>(View.Family->Views[0]);
			const FViewInfo* RightView = static_cast<const FViewInfo*>(View.Family->Views[1]);

			const uint32 LeftMinX = LeftView->ViewRect.Min.X * SeparateTranslucencyScale;
			const uint32 LeftMaxX = LeftView->ViewRect.Max.X * SeparateTranslucencyScale;
			const uint32 RightMinX = RightView->ViewRect.Min.X * SeparateTranslucencyScale;
			const uint32 RightMaxX = RightView->ViewRect.Max.X * SeparateTranslucencyScale;

			const uint32 LeftMaxY = LeftView->ViewRect.Max.Y * SeparateTranslucencyScale;
			const uint32 RightMaxY = RightView->ViewRect.Max.Y * SeparateTranslucencyScale;

			RHICmdList.SetStereoViewport(LeftMinX, RightMinX, 0, 0, 0.0f, LeftMaxX, RightMaxX, LeftMaxY, RightMaxY, 1.0f);
		}
		else
		{
			RHICmdList.SetViewport(0, 0, 0, Renderer.InstancedStereoWidth * SeparateTranslucencyScale, View.ViewRect.Max.Y * SeparateTranslucencyScale, 1.0f);
		}	
	}
	else
	{
		RHICmdList.SetViewport(View.ViewRect.Min.X * SeparateTranslucencyScale, View.ViewRect.Min.Y * SeparateTranslucencyScale, 0.0f, View.ViewRect.Max.X * SeparateTranslucencyScale, View.ViewRect.Max.Y * SeparateTranslucencyScale, 1.0f);
	}
}

void FSceneRenderTargets::ResolveSeparateTranslucency(FRHICommandList& RHICmdList, const FViewInfo& View)
{
	SCOPED_DRAW_EVENT(RHICmdList, ResolveSeparateTranslucency);

	check(RHICmdList.IsOutsideRenderPass());

	TRefCountPtr<IPooledRenderTarget>* SeparateTranslucency;
	TRefCountPtr<IPooledRenderTarget>* SeparateTranslucencyDepth;
	if (bSnapshot)
	{
		check(SeparateTranslucencyRT.GetReference());
		SeparateTranslucency = &SeparateTranslucencyRT;
		SeparateTranslucencyDepth = SeparateTranslucencyScale < 1.f ? &DownsampledTranslucencyDepthRT : &SceneDepthZ;
	}
	else
	{
		SeparateTranslucency = &GetSeparateTranslucency(RHICmdList, SeparateTranslucencyBufferSize);
		SeparateTranslucencyDepth = SeparateTranslucencyScale < 1.f ? &GetDownsampledTranslucencyDepth(RHICmdList, SeparateTranslucencyBufferSize) : &SceneDepthZ;
	}

	const FResolveRect SeparateResolveRect(
		View.ViewRect.Min.X * SeparateTranslucencyScale, 
		View.ViewRect.Min.Y * SeparateTranslucencyScale, 
		View.ViewRect.Max.X * SeparateTranslucencyScale, 
		View.ViewRect.Max.Y * SeparateTranslucencyScale
		);

	RHICmdList.CopyToResolveTarget((*SeparateTranslucency)->GetRenderTargetItem().TargetableTexture, (*SeparateTranslucency)->GetRenderTargetItem().ShaderResourceTexture, SeparateResolveRect);
	RHICmdList.CopyToResolveTarget((*SeparateTranslucencyDepth)->GetRenderTargetItem().TargetableTexture, (*SeparateTranslucencyDepth)->GetRenderTargetItem().ShaderResourceTexture, SeparateResolveRect);

	bSeparateTranslucencyPass = false;
}

FResolveRect FSceneRenderTargets::GetDefaultRect(const FResolveRect& Rect, uint32 DefaultWidth, uint32 DefaultHeight)
{
	if (Rect.X1 >= 0 && Rect.X2 >= 0 && Rect.Y1 >= 0 && Rect.Y2 >= 0)
	{
		return Rect;
	}
	else
	{
		return FResolveRect(0, 0, DefaultWidth, DefaultHeight);
	}
}

void FSceneRenderTargets::ResolveDepthTexture(FRHICommandList& RHICmdList, const FTexture2DRHIRef& SourceTexture, const FTexture2DRHIRef& DestTexture, const FResolveParams& ResolveParams)
{
	check(!RHICmdList.IsInsideRenderPass());

	FResolveRect ResolveRect = ResolveParams.Rect;

	RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, SourceTexture);

	FRHIRenderPassInfo RPInfo;
	RPInfo.DepthStencilRenderTarget.Action = EDepthStencilTargetActions::LoadDepthStencil_StoreDepthStencil;
	RPInfo.DepthStencilRenderTarget.DepthStencilTarget = DestTexture;
	RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthWrite_StencilWrite;

	TransitionRenderPassTargets(RHICmdList, RPInfo);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("ResolveDepthTexture"));
	{
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	// No alpha blending, no depth tests or writes, no stencil tests or writes, no backface culling.
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();

	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_Always>::GetRHI();

	const uint32 SourceWidth = SourceTexture->GetSizeX();
	const uint32 SourceHeight = SourceTexture->GetSizeY();

	const uint32 TargetWidth = DestTexture->GetSizeX();
	const uint32 TargetHeight = DestTexture->GetSizeY();

	RHICmdList.SetViewport(0.0f, 0.0f, 0.0f, TargetWidth, TargetHeight, 1.0f);

	FResolveRect SourceRect = GetDefaultRect(ResolveParams.Rect, SourceWidth, SourceHeight);
	FResolveRect DestRect = GetDefaultRect(ResolveParams.Rect, TargetWidth, TargetHeight);

	// Generate the vertices used to copy from the source surface to the destination surface.
	const float MinU = SourceRect.X1;
	const float MinV = SourceRect.Y1;
	const float MaxU = SourceRect.X2;
	const float MaxV = SourceRect.Y2;
	const float MinX = -1.f + DestRect.X1 / ((float)TargetWidth * 0.5f);
	const float MinY = +1.f - DestRect.Y1 / ((float)TargetHeight * 0.5f);
	const float MaxX = -1.f + DestRect.X2 / ((float)TargetWidth * 0.5f);
	const float MaxY = +1.f - DestRect.Y2 / ((float)TargetHeight * 0.5f);

	// Set the vertex and pixel shader
	auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FResolveVS> ResolveVertexShader(ShaderMap);

	TShaderMapRef<FResolveDepthPS> ResolvePixelShaderAny(ShaderMap);
	TShaderMapRef<FResolveDepth2XPS> ResolvePixelShader2X(ShaderMap);
	TShaderMapRef<FResolveDepth4XPS> ResolvePixelShader4X(ShaderMap);
	TShaderMapRef<FResolveDepth8XPS> ResolvePixelShader8X(ShaderMap);

	int32 TextureIndex = -1;
	FRHIPixelShader* ResolvePixelShader;
	switch (SourceTexture->GetNumSamples())
	{
	case 2:
		TextureIndex = ResolvePixelShader2X->UnresolvedSurface.GetBaseIndex();
		ResolvePixelShader = GETSAFERHISHADER_PIXEL(*ResolvePixelShader2X);
		break;
	case 4:
		TextureIndex = ResolvePixelShader4X->UnresolvedSurface.GetBaseIndex();
		ResolvePixelShader = GETSAFERHISHADER_PIXEL(*ResolvePixelShader4X);
		break;
	case 8:
		TextureIndex = ResolvePixelShader8X->UnresolvedSurface.GetBaseIndex();
		ResolvePixelShader = GETSAFERHISHADER_PIXEL(*ResolvePixelShader8X);
		break;
	default:
		ensureMsgf(false, TEXT("Unsupported depth resolve for samples: %i.  Dynamic loop method isn't supported on all platforms.  Please add specific case."), SourceTexture->GetNumSamples());
		TextureIndex = ResolvePixelShaderAny->UnresolvedSurface.GetBaseIndex();
		ResolvePixelShader = GETSAFERHISHADER_PIXEL(*ResolvePixelShaderAny);
		break;
	}

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GScreenVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*ResolveVertexShader);
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = ResolvePixelShader;
	GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
	RHICmdList.SetBlendFactor(FLinearColor::White);

	// Set the source texture.	
	if (SourceTexture)
	{
		RHICmdList.SetShaderTexture(ResolvePixelShader, TextureIndex, SourceTexture);
	}

	FRHIResourceCreateInfo CreateInfo;
	FVertexBufferRHIRef VertexBufferRHI = RHICreateVertexBuffer(sizeof(FScreenVertex) * 4, BUF_Volatile, CreateInfo);
	void* VoidPtr = RHILockVertexBuffer(VertexBufferRHI, 0, sizeof(FScreenVertex) * 4, RLM_WriteOnly);

	// Generate the vertices used
	FScreenVertex* Vertices = (FScreenVertex*)VoidPtr;

	Vertices[0].Position.X = MaxX;
	Vertices[0].Position.Y = MinY;
	Vertices[0].UV.X = MaxU;
	Vertices[0].UV.Y = MinV;

	Vertices[1].Position.X = MaxX;
	Vertices[1].Position.Y = MaxY;
	Vertices[1].UV.X = MaxU;
	Vertices[1].UV.Y = MaxV;

	Vertices[2].Position.X = MinX;
	Vertices[2].Position.Y = MinY;
	Vertices[2].UV.X = MinU;
	Vertices[2].UV.Y = MinV;

	Vertices[3].Position.X = MinX;
	Vertices[3].Position.Y = MaxY;
	Vertices[3].UV.X = MinU;
	Vertices[3].UV.Y = MaxV;

	RHIUnlockVertexBuffer(VertexBufferRHI);
	RHICmdList.SetStreamSource(0, VertexBufferRHI, 0);
	RHICmdList.DrawPrimitive(0, 2, 1);
}
	RHICmdList.EndRenderPass();
}
void FSceneRenderTargets::ResolveSceneDepthTexture(FRHICommandList& RHICmdList, const FResolveRect& ResolveRect)
{
	check(RHICmdList.IsOutsideRenderPass());

	SCOPED_DRAW_EVENT(RHICmdList, ResolveSceneDepthTexture);

	FResolveParams ResolveParams;
	if (ResolveRect.IsValid())
	{
		ResolveParams.DestRect = ResolveRect;
		ResolveParams.Rect = ResolveRect;
	}

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	uint32 CurrentNumSamples = SceneDepthZ->GetDesc().NumSamples;

	const EShaderPlatform CurrentShaderPlatform = GShaderPlatformForFeatureLevel[SceneContext.GetCurrentFeatureLevel()];
	if ((CurrentNumSamples <= 1 || !RHISupportsSeparateMSAAAndResolveTextures(CurrentShaderPlatform)) || !GAllowCustomMSAAResolves)
	{
		RHICmdList.CopyToResolveTarget(GetSceneDepthSurface(), GetSceneDepthTexture(), ResolveParams);
	}
	else
	{
		ResolveDepthTexture(RHICmdList, GetSceneDepthSurface(), GetSceneDepthTexture(), ResolveParams);
	}
}

void FSceneRenderTargets::ResolveSceneDepthToAuxiliaryTexture(FRHICommandList& RHICmdList)
{
	// Resolve the scene depth to an auxiliary texture when SM3/SM4 is in use. This needs to happen so the auxiliary texture can be bound as a shader parameter
	// while the primary scene depth texture can be bound as the target. Simultaneously binding a single DepthStencil resource as a parameter and target
	// is unsupported in d3d feature level 10.
	if(!GSupportsDepthFetchDuringDepthTest)
	{
		SCOPED_DRAW_EVENT(RHICmdList, ResolveSceneDepthToAuxiliaryTexture);
		RHICmdList.CopyToResolveTarget(GetSceneDepthSurface(), GetAuxiliarySceneDepthTexture(), FResolveParams());
	}
}

void FSceneRenderTargets::CleanUpEditorPrimitiveTargets()
{
	EditorPrimitivesDepth.SafeRelease();
	EditorPrimitivesColor.SafeRelease();
}

int32 FSceneRenderTargets::GetEditorMSAACompositingSampleCount() const
{
	int32 Value = 1;

	// only supported on SM5 yet (SM4 doesn't have MSAA sample load functionality which makes it harder to implement)
	if (CurrentFeatureLevel >= ERHIFeatureLevel::SM5 && GRHISupportsMSAADepthSampleAccess)
	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MSAA.CompositingSampleCount"));

		Value = CVar->GetValueOnRenderThread();

		if(Value <= 1)
		{
			Value = 1;
		}
		else if(Value <= 2)
		{
			Value = 2;
		}
		else if(Value <= 4)
		{
			Value = 4;
		}
		else
		{
			Value = 8;
		}
	}

	return Value;
}

const FTexture2DRHIRef& FSceneRenderTargets::GetEditorPrimitivesColor(FRHICommandList& RHICmdList)
{
	const bool bIsValid = IsValidRef(EditorPrimitivesColor);

	if( !bIsValid || EditorPrimitivesColor->GetDesc().NumSamples != GetEditorMSAACompositingSampleCount() )
	{
		// If the target is does not match the MSAA settings it needs to be recreated
		InitEditorPrimitivesColor(RHICmdList);
	}

	return (const FTexture2DRHIRef&)EditorPrimitivesColor->GetRenderTargetItem().TargetableTexture;
}


const FTexture2DRHIRef& FSceneRenderTargets::GetEditorPrimitivesDepth(FRHICommandList& RHICmdList)
{
	const bool bIsValid = IsValidRef(EditorPrimitivesDepth);

	if (!bIsValid || (CurrentFeatureLevel >= ERHIFeatureLevel::SM5 && EditorPrimitivesDepth->GetDesc().NumSamples != GetEditorMSAACompositingSampleCount()) )
	{
		// If the target is does not match the MSAA settings it needs to be recreated
		InitEditorPrimitivesDepth(RHICmdList);
	}

	return (const FTexture2DRHIRef&)EditorPrimitivesDepth->GetRenderTargetItem().TargetableTexture;
}

void FSceneRenderTargets::InitEditorPrimitivesColor(FRHICommandList& RHICmdList)
{
	FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, 
		PF_B8G8R8A8,
		FClearValueBinding::Transparent,
		TexCreate_None, 
		TexCreate_ShaderResource | TexCreate_RenderTargetable,
		false));

	Desc.NumSamples = GetEditorMSAACompositingSampleCount();

	GRenderTargetPool.FindFreeElement(RHICmdList, Desc, EditorPrimitivesColor, TEXT("EditorPrimitivesColor"));
}

void FSceneRenderTargets::InitEditorPrimitivesDepth(FRHICommandList& RHICmdList)
{
	FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, 
		PF_DepthStencil,
		FClearValueBinding::DepthFar,
		TexCreate_None, 
		TexCreate_ShaderResource | TexCreate_DepthStencilTargetable,
		false));

	Desc.NumSamples = GetEditorMSAACompositingSampleCount();

	GRenderTargetPool.FindFreeElement(RHICmdList, Desc, EditorPrimitivesDepth, TEXT("EditorPrimitivesDepth"));
}

void FSceneRenderTargets::SetBufferSize(int32 InBufferSizeX, int32 InBufferSizeY)
{
	QuantizeSceneBufferSize(FIntPoint(InBufferSizeX, InBufferSizeY), BufferSize);
}

void FSceneRenderTargets::SetSeparateTranslucencyBufferSize(bool bAnyViewWantsDownsampledSeparateTranslucency)
{
	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.SeparateTranslucencyScreenPercentage"));
	const float CVarScale = FMath::Clamp(CVar->GetValueOnRenderThread() / 100.0f, 0.0f, 100.0f);
	float EffectiveScale = CVarScale;

	// 'r.SeparateTranslucencyScreenPercentage' CVar wins over automatic downsampling
	if (FMath::Abs(CVarScale - 1.0f) < .001f && bAnyViewWantsDownsampledSeparateTranslucency)
	{
		EffectiveScale = .5f;
	}

	int32 ScaledX = GetBufferSizeXY().X * EffectiveScale;
	int32 ScaledY = GetBufferSizeXY().Y * EffectiveScale;
	SeparateTranslucencyBufferSize = FIntPoint(FMath::Max(ScaledX, 1), FMath::Max(ScaledY, 1));
	SeparateTranslucencyScale = EffectiveScale;
}

void FSceneRenderTargets::AllocateMobileRenderTargets(FRHICommandList& RHICmdList)
{
	// on ES2 we don't do on demand allocation of SceneColor yet (in non ES2 it's released in the Tonemapper Process())
	AllocSceneColor(RHICmdList);
	AllocateCommonDepthTargets(RHICmdList);

#if PLATFORM_ANDROID
	static const auto MobileMultiViewCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("vr.MobileMultiView"));
	static const auto CVarMobileMultiViewDirect = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("vr.MobileMultiView.Direct"));

	const bool bIsUsingMobileMultiView = GSupportsMobileMultiView && (MobileMultiViewCVar && MobileMultiViewCVar->GetValueOnAnyThread() != 0);

	// TODO: Test platform support for direct
	const bool bIsMobileMultiViewDirectEnabled = (CVarMobileMultiViewDirect && CVarMobileMultiViewDirect->GetValueOnAnyThread() != 0);

	if (bIsUsingMobileMultiView)
	{
		const int32 ScaleFactor = (bIsMobileMultiViewDirectEnabled) ? 1 : 2;

		AllocMobileMultiViewSceneColor(RHICmdList, ScaleFactor);
		AllocMobileMultiViewDepth(RHICmdList, ScaleFactor);
	}
#endif

	AllocateDebugViewModeTargets(RHICmdList);

	EPixelFormat Format = GetSceneColor()->GetDesc().Format;

#if PLATFORM_HTML5
	// For 64-bit ES2 without framebuffer fetch, create extra render target for copy of alpha channel.
	if((Format == PF_FloatRGBA) && (GSupportsShaderFramebufferFetch == false)) 
	{
		// creating a PF_R16F (a true one-channel renderable fp texture) is only supported on GL if EXT_texture_rg is available.  It's present
		// on iOS, but not in WebGL or Android.
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_FloatRGBA, FClearValueBinding::None, TexCreate_None, TexCreate_RenderTargetable, false));
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SceneAlphaCopy, TEXT("SceneAlphaCopy"));
	}
	else
#endif
	{
		SceneAlphaCopy = GSystemTextures.MaxFP16Depth;
	}
}

// This is a helper class. It generates and provides N names with
// sequentially incremented postfix starting from 0.
// Example: SomeName0, SomeName1, ..., SomeName117
class IncrementalNamesHolder
{
public:
	IncrementalNamesHolder(const TCHAR* const Name, uint32 Size)
		: ArraySize(Size)
	{
		check(Size > 0);
		Names = new FString[Size];
		for (uint32 i = 0; i < Size; ++i)
		{
			Names[i] = FString::Printf(TEXT("%s%d"), Name, i);
		}
	}

	~IncrementalNamesHolder()
	{
		delete[] Names;
	}

	const TCHAR* const operator[] (uint32 Idx) const
	{
		check(Idx < ArraySize);
		return *Names[Idx];
	}

private:
	const uint32 ArraySize;
	FString*     Names = nullptr;
};

// for easier use of "VisualizeTexture"
static const TCHAR* const GetVolumeName(uint32 Id, bool bDirectional)
{
	constexpr uint32 MaxNames = 128;
	static IncrementalNamesHolder Names   (TEXT("TranslucentVolume"),    MaxNames);
	static IncrementalNamesHolder NamesDir(TEXT("TranslucentVolumeDir"), MaxNames);

	check(Id < MaxNames);

	return bDirectional ? NamesDir[Id] : Names[Id];
}

void FSceneRenderTargets::AllocateReflectionTargets(FRHICommandList& RHICmdList, int32 TargetSize)
{
	if (GSupportsRenderTargetFormat_PF_FloatRGBA)
	{
		const int32 NumReflectionCaptureMips = FMath::CeilLogTwo(TargetSize) + 1;

		if (ReflectionColorScratchCubemap[0] && ReflectionColorScratchCubemap[0]->GetRenderTargetItem().TargetableTexture->GetNumMips() != NumReflectionCaptureMips)
		{
			ReflectionColorScratchCubemap[0].SafeRelease();
			ReflectionColorScratchCubemap[1].SafeRelease();
		}

		// Reflection targets are shared between both mobile and deferred shading paths. If we have already allocated for one and are now allocating for the other,
		// we can skip these targets.
		bool bSharedReflectionTargetsAllocated = ReflectionColorScratchCubemap[0] != nullptr;

		if (!bSharedReflectionTargetsAllocated)
		{
			// We write to these cubemap faces individually during filtering
			uint32 CubeTexFlags = TexCreate_TargetArraySlicesIndependently;

			{
				// Create scratch cubemaps for filtering passes
				FPooledRenderTargetDesc Desc2(FPooledRenderTargetDesc::CreateCubemapDesc(TargetSize, PF_FloatRGBA, FClearValueBinding(FLinearColor(0, 10000, 0, 0)), CubeTexFlags, TexCreate_RenderTargetable, false, 1, NumReflectionCaptureMips));
				GRenderTargetPool.FindFreeElement(RHICmdList, Desc2, ReflectionColorScratchCubemap[0], TEXT("ReflectionColorScratchCubemap0"), true, ERenderTargetTransience::NonTransient );
				GRenderTargetPool.FindFreeElement(RHICmdList, Desc2, ReflectionColorScratchCubemap[1], TEXT("ReflectionColorScratchCubemap1"), true, ERenderTargetTransience::NonTransient );
			}

			extern int32 GDiffuseIrradianceCubemapSize;
			const int32 NumDiffuseIrradianceMips = FMath::CeilLogTwo(GDiffuseIrradianceCubemapSize) + 1;

			{
				FPooledRenderTargetDesc Desc2(FPooledRenderTargetDesc::CreateCubemapDesc(GDiffuseIrradianceCubemapSize, PF_FloatRGBA, FClearValueBinding(FLinearColor(0, 10000, 0, 0)), CubeTexFlags, TexCreate_RenderTargetable, false, 1, NumDiffuseIrradianceMips));
				GRenderTargetPool.FindFreeElement(RHICmdList, Desc2, DiffuseIrradianceScratchCubemap[0], TEXT("DiffuseIrradianceScratchCubemap0"), true, ERenderTargetTransience::NonTransient);
				GRenderTargetPool.FindFreeElement(RHICmdList, Desc2, DiffuseIrradianceScratchCubemap[1], TEXT("DiffuseIrradianceScratchCubemap1"), true, ERenderTargetTransience::NonTransient);
			}

			{
				FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(FSHVector3::MaxSHBasis, 1), PF_FloatRGBA, FClearValueBinding(FLinearColor(0, 10000, 0, 0)), TexCreate_None, TexCreate_RenderTargetable, false));
				GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SkySHIrradianceMap, TEXT("SkySHIrradianceMap"), true, ERenderTargetTransience::NonTransient);
			}
		}
	}
}

void FSceneRenderTargets::AllocateDebugViewModeTargets(FRHICommandList& RHICmdList)
{
	// If the shader/quad complexity shader need a quad overdraw buffer to be bind, allocate it.
	if (AllowDebugViewShaderMode(DVSM_QuadComplexity, GetFeatureLevelShaderPlatform(CurrentFeatureLevel), CurrentFeatureLevel))
	{
		FIntPoint QuadOverdrawSize;
		QuadOverdrawSize.X = 2 * FMath::Max<uint32>((BufferSize.X + 1) / 2, 1); // The size is time 2 since left side is QuadDescriptor, and right side QuadComplexity.
		QuadOverdrawSize.Y = FMath::Max<uint32>((BufferSize.Y + 1) / 2, 1);

		FPooledRenderTargetDesc QuadOverdrawDesc = FPooledRenderTargetDesc::Create2DDesc(
			QuadOverdrawSize, 
			PF_R32_UINT,
			FClearValueBinding::None,
			0,
			TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV,
			false
			);

		GRenderTargetPool.FindFreeElement(RHICmdList, QuadOverdrawDesc, QuadOverdrawBuffer, TEXT("QuadOverdrawBuffer"));
	}
}

void FSceneRenderTargets::AllocateCommonDepthTargets(FRHICommandList& RHICmdList)
{
	const bool bStereo = GEngine->StereoRenderingDevice.IsValid() && GEngine->StereoRenderingDevice->IsStereoEnabled();
	IStereoRenderTargetManager* const StereoRenderTargetManager = bStereo ? GEngine->StereoRenderingDevice->GetRenderTargetManager() : nullptr;

	if (SceneDepthZ && (!(SceneDepthZ->GetRenderTargetItem().TargetableTexture->GetClearBinding() == DefaultDepthClear) || (StereoRenderTargetManager && StereoRenderTargetManager->NeedReAllocateDepthTexture(SceneDepthZ))))
	{
		uint32 StencilCurrent, StencilNew;
		float DepthCurrent, DepthNew;
		SceneDepthZ->GetRenderTargetItem().TargetableTexture->GetClearBinding().GetDepthStencil(DepthCurrent, StencilCurrent);
		DefaultDepthClear.GetDepthStencil(DepthNew, StencilNew);
		UE_LOG(LogRenderer, Log, TEXT("Releasing previous depth to switch default clear from depth: %f stencil: %u to depth: %f stencil: %u"), DepthCurrent, StencilCurrent, DepthNew, StencilNew);
		SceneDepthZ.SafeRelease();
	}

	if (!SceneDepthZ || GFastVRamConfig.bDirty)
	{
		FTexture2DRHIRef DepthTex, SRTex;
		bHMDAllocatedDepthTarget = StereoRenderTargetManager && StereoRenderTargetManager->AllocateDepthTexture(0, BufferSize.X, BufferSize.Y, PF_X24_G8, 0, TexCreate_None, TexCreate_DepthStencilTargetable, DepthTex, SRTex, GetNumSceneColorMSAASamples(CurrentFeatureLevel));

		// Create a texture to store the resolved scene depth, and a render-targetable surface to hold the unresolved scene depth.
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_DepthStencil, DefaultDepthClear, TexCreate_None, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource | TexCreate_InputAttachmentRead, false));
		Desc.NumSamples = GetNumSceneColorMSAASamples(CurrentFeatureLevel);
		Desc.Flags |= GFastVRamConfig.SceneDepth;

		// Only defer texture allocation if we're an HMD-allocated target, and we're not MSAA.
		const bool bDeferTextureAllocation = bHMDAllocatedDepthTarget && Desc.NumSamples == 1;
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SceneDepthZ, TEXT("SceneDepthZ"), true, ERenderTargetTransience::Transient, bDeferTextureAllocation);

		if (SceneDepthZ && bHMDAllocatedDepthTarget)
		{
			const uint32 OldElementSize = SceneDepthZ->ComputeMemorySize();
		
			// If SRT and texture are different (MSAA), only modify the resolve render target, to avoid creating a swapchain of MSAA textures
			if (SceneDepthZ->GetRenderTargetItem().ShaderResourceTexture == SceneDepthZ->GetRenderTargetItem().TargetableTexture)
			{
				SceneDepthZ->GetRenderTargetItem().ShaderResourceTexture = SceneDepthZ->GetRenderTargetItem().TargetableTexture = SRTex;
			}
			else
			{
				SceneDepthZ->GetRenderTargetItem().ShaderResourceTexture = SRTex;
			}

			GRenderTargetPool.UpdateElementSize(SceneDepthZ, OldElementSize);
		}

		SceneStencilSRV.SafeRelease();
	}

	// We need to update the stencil SRV every frame if the depth target was allocated by an HMD.
	// TODO: This should be handled by the HMD depth target swap chain, but currently it only updates the depth SRV.
	if (bHMDAllocatedDepthTarget)
	{
		SceneStencilSRV.SafeRelease();
	}

	if (SceneDepthZ && !SceneStencilSRV)
	{
		SceneStencilSRV = RHICreateShaderResourceView((FTexture2DRHIRef&)SceneDepthZ->GetRenderTargetItem().TargetableTexture, 0, 1, PF_X24_G8);
	}

	// When targeting DX Feature Level 10, create an auxiliary texture to store the resolved scene depth, and a render-targetable surface to hold the unresolved scene depth.
	if (!AuxiliarySceneDepthZ && !GSupportsDepthFetchDuringDepthTest)
	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_DepthStencil, DefaultDepthClear, TexCreate_None, TexCreate_DepthStencilTargetable, false));
		Desc.AutoWritable = false;
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, AuxiliarySceneDepthZ, TEXT("AuxiliarySceneDepthZ"), true, ERenderTargetTransience::NonTransient);
	}
}

void FSceneRenderTargets::AllocateScreenShadowMask(FRHICommandList& RHICmdList, TRefCountPtr<IPooledRenderTarget>& ScreenShadowMaskTexture)
{
	FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(GetBufferSizeXY(), PF_B8G8R8A8, FClearValueBinding::White, TexCreate_None, TexCreate_RenderTargetable, false));
	Desc.Flags |= GFastVRamConfig.ScreenSpaceShadowMask;
	Desc.NumSamples = GetNumSceneColorMSAASamples(GetCurrentFeatureLevel());
	GRenderTargetPool.FindFreeElement(RHICmdList, Desc, ScreenShadowMaskTexture, TEXT("ScreenShadowMaskTexture"));
}

const FTexture2DRHIRef& FSceneRenderTargets::GetOptionalShadowDepthColorSurface(FRHICommandList& RHICmdList, int32 Width, int32 Height) const
{
	// Look for matching resolution
	int32 EmptySlot = -1;
	for (int32 Index = 0; Index < ARRAY_COUNT(OptionalShadowDepthColor); Index++)
	{
		if (OptionalShadowDepthColor[Index])
		{
			const FTexture2DRHIRef& TargetTexture = (const FTexture2DRHIRef&)OptionalShadowDepthColor[Index]->GetRenderTargetItem().TargetableTexture;
			if (TargetTexture->GetSizeX() == Width && TargetTexture->GetSizeY() == Height)
			{
				return TargetTexture;
			}
		}
		else
		{
			// Remember this as a free slot for allocation attempt
			EmptySlot = Index;
		}
	}

	if (EmptySlot == -1)
	{
		UE_LOG(LogRenderer, Fatal, TEXT("Exceeded storage space for OptionalShadowDepthColorSurface. Increase array size."));
	}

	// Allocate new shadow color buffer (it must be the same resolution as the depth target!)
	const FIntPoint ShadowColorBufferResolution = FIntPoint(Width, Height);
	FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(ShadowColorBufferResolution, PF_B8G8R8A8, FClearValueBinding::None, TexCreate_None, TexCreate_RenderTargetable, false));
	GRenderTargetPool.FindFreeElement(RHICmdList, Desc, (TRefCountPtr<IPooledRenderTarget>&)OptionalShadowDepthColor[EmptySlot], TEXT("OptionalShadowDepthColor"));
	UE_LOG(LogRenderer, Log, TEXT("Allocated OptionalShadowDepthColorSurface %d x %d"), Width, Height);

	return (const FTexture2DRHIRef&)OptionalShadowDepthColor[EmptySlot]->GetRenderTargetItem().TargetableTexture;
}

void FSceneRenderTargets::AllocateLightingChannelTexture(FRHICommandList& RHICmdList)
{
	if (!LightingChannels)
	{
		// Only need 3 bits for lighting channels
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_R16_UINT, FClearValueBinding::None, TexCreate_None, TexCreate_RenderTargetable, false));
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, LightingChannels, TEXT("LightingChannels"), true, ERenderTargetTransience::NonTransient);
	}
}

void FSceneRenderTargets::AllocateDeferredShadingPathRenderTargets(FRHICommandListImmediate& RHICmdList, const int32 NumViews)
{
	AllocateCommonDepthTargets(RHICmdList);

	// Create a quarter-sized version of the scene depth.
	{
		FIntPoint SmallDepthZSize(FMath::Max<uint32>(BufferSize.X / SmallColorDepthDownsampleFactor, 1), FMath::Max<uint32>(BufferSize.Y / SmallColorDepthDownsampleFactor, 1));

		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(SmallDepthZSize, PF_DepthStencil, FClearValueBinding::None, TexCreate_None, TexCreate_DepthStencilTargetable, true));
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SmallDepthZ, TEXT("SmallDepthZ"), true, ERenderTargetTransience::NonTransient);
	}

	// Create the required render targets if running Highend.
	if (CurrentFeatureLevel >= ERHIFeatureLevel::SM4)
	{
		// Create the screen space ambient occlusion buffer
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_G8, FClearValueBinding::White, TexCreate_None, TexCreate_RenderTargetable, false));
			Desc.Flags |= GFastVRamConfig.ScreenSpaceAO;

			if (CurrentFeatureLevel >= ERHIFeatureLevel::SM5)
			{
				// UAV is only needed to support "r.AmbientOcclusion.Compute"
				// todo: ideally this should be only UAV or RT, not both
				Desc.TargetableFlags |= TexCreate_UAV;
			}
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, ScreenSpaceAO, TEXT("ScreenSpaceAO"), true, ERenderTargetTransience::NonTransient);
		}
		
		{
			const int32 TranslucencyLightingVolumeDim = GetTranslucencyLightingVolumeDim();

			// TODO: We can skip the and TLV allocations when rendering in forward shading mode
			uint32 TranslucencyTargetFlags = TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_ReduceMemoryWithTilingMode;

			if (CurrentFeatureLevel >= ERHIFeatureLevel::SM5)
			{
				TranslucencyTargetFlags |= TexCreate_UAV;
			}

			TranslucencyLightingVolumeAmbient.SetNum(NumViews * NumTranslucentVolumeRenderTargetSets);
			TranslucencyLightingVolumeDirectional.SetNum(NumViews * NumTranslucentVolumeRenderTargetSets);

			for (int32 RTSetIndex = 0; RTSetIndex < NumTranslucentVolumeRenderTargetSets * NumViews; RTSetIndex++)
			{
				GRenderTargetPool.FindFreeElement(
					RHICmdList,
					FPooledRenderTargetDesc(FPooledRenderTargetDesc::CreateVolumeDesc(
						TranslucencyLightingVolumeDim,
						TranslucencyLightingVolumeDim,
						TranslucencyLightingVolumeDim,
						PF_FloatRGBA,
						FClearValueBinding::Transparent,
						0,
						TranslucencyTargetFlags,
						false,
						1,
						false)),
					TranslucencyLightingVolumeAmbient[RTSetIndex],
					GetVolumeName(RTSetIndex, false),
					true,
					ERenderTargetTransience::NonTransient
					);

				//Tests to catch UE-31578, UE-32536 and UE-22073 crash (Defferred Render Targets not being allocated)
				ensureMsgf(TranslucencyLightingVolumeAmbient[RTSetIndex], TEXT("Failed to allocate render target %s with dimension %i and flags %i"),
					GetVolumeName(RTSetIndex, false),
					TranslucencyLightingVolumeDim,
					TranslucencyTargetFlags);

				GRenderTargetPool.FindFreeElement(
					RHICmdList,
					FPooledRenderTargetDesc(FPooledRenderTargetDesc::CreateVolumeDesc(
						TranslucencyLightingVolumeDim,
						TranslucencyLightingVolumeDim,
						TranslucencyLightingVolumeDim,
						PF_FloatRGBA,
						FClearValueBinding::Transparent,
						0,
						TranslucencyTargetFlags,
						false,
						1,
						false)),
					TranslucencyLightingVolumeDirectional[RTSetIndex],
					GetVolumeName(RTSetIndex, true),
					true,
					ERenderTargetTransience::NonTransient
					);

				//Tests to catch UE-31578, UE-32536 and UE-22073 crash
				ensureMsgf(TranslucencyLightingVolumeDirectional[RTSetIndex], TEXT("Failed to allocate render target %s with dimension %i and flags %i"),
					GetVolumeName(RTSetIndex, true),
					TranslucencyLightingVolumeDim,
					TranslucencyTargetFlags);
			}

			//these get bound even with the CVAR off, make sure they aren't full of garbage.
			if (!GUseTranslucentLightingVolumes)
			{
				ClearTranslucentVolumeLighting(RHICmdList, 0);
			}
		}
	}

	// LPV : Dynamic directional occlusion for diffuse and specular
	if(UseLightPropagationVolumeRT(CurrentFeatureLevel))
	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_R8G8, FClearValueBinding::Transparent, TexCreate_None, TexCreate_RenderTargetable, false));
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, DirectionalOcclusion, TEXT("DirectionalOcclusion"));
	}

	if (CurrentFeatureLevel >= ERHIFeatureLevel::SM4) 
	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable, false));
		if (CurrentFeatureLevel >= ERHIFeatureLevel::SM5)
		{
			Desc.TargetableFlags |= TexCreate_UAV;
		}
		Desc.Flags |= GFastVRamConfig.LightAccumulation;
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, LightAccumulation, TEXT("LightAccumulation"), true, ERenderTargetTransience::NonTransient);
	}

	AllocateDebugViewModeTargets(RHICmdList);

	if (bAllocateVelocityGBuffer)
	{
		FPooledRenderTargetDesc VelocityRTDesc = FVelocityRendering::GetRenderTargetDesc();
		VelocityRTDesc.Flags |= GFastVRamConfig.GBufferVelocity;
		GRenderTargetPool.FindFreeElement(RHICmdList, VelocityRTDesc, SceneVelocity, TEXT("GBufferVelocity"));
	}

	if (UseVirtualTexturing(CurrentFeatureLevel))
	{
		FIntPoint FeedbackSize = FIntPoint::DivideAndRoundUp(BufferSize, FMath::Max(GVirtualTextureFeedbackFactor, 1));
		VirtualTextureFeedback.CreateResourceGPU(RHICmdList, FeedbackSize);
	}
}

EPixelFormat FSceneRenderTargets::GetDesiredMobileSceneColorFormat() const
{
	EPixelFormat DefaultColorFormat = (!IsMobileHDR() || IsMobileHDR32bpp() || !GSupportsRenderTargetFormat_PF_FloatRGBA) ? PF_B8G8R8A8 : PF_FloatRGBA;
	check(GPixelFormats[DefaultColorFormat].Supported);

	EPixelFormat MobileSceneColorBufferFormat = DefaultColorFormat;
	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.SceneColorFormat"));
	int32 MobileSceneColor = CVar->GetValueOnRenderThread();
	switch (MobileSceneColor)
	{
		case 1:
			MobileSceneColorBufferFormat = PF_FloatRGBA; break;
		case 2:
			MobileSceneColorBufferFormat = PF_FloatR11G11B10; break;
		case 3:
			MobileSceneColorBufferFormat = PF_B8G8R8A8; break;
		default:
		break;
	}

	return GPixelFormats[MobileSceneColorBufferFormat].Supported ? MobileSceneColorBufferFormat : DefaultColorFormat;
}

EPixelFormat FSceneRenderTargets::GetMobileSceneColorFormat() const
{
	return CurrentMobileSceneColorFormat;
}

void FSceneRenderTargets::ClearTranslucentVolumeLighting(FRHICommandListImmediate& RHICmdList, int32 ViewIndex)
{
	if (GSupportsVolumeTextureRendering)
	{
		// Clear all volume textures in the same draw with MRT, which is faster than individually
		static_assert(TVC_MAX == 2, "Only expecting two translucency lighting cascades.");
		static IConsoleVariable* CVarTranslucencyVolumeBlur =
			IConsoleManager::Get().FindConsoleVariable(TEXT("r.TranslucencyVolumeBlur"));
		static constexpr int32 Num3DTextures = NumTranslucentVolumeRenderTargetSets << 1;

		FRHITexture* RenderTargets[Num3DTextures];
		bool bUseTransLightingVolBlur = CVarTranslucencyVolumeBlur->GetInt() > 0;
		const int32 NumIterations = bUseTransLightingVolBlur ?
			NumTranslucentVolumeRenderTargetSets : NumTranslucentVolumeRenderTargetSets - 1;

		for (int32 Idx = 0; Idx < NumIterations; ++Idx)
		{
			RenderTargets[Idx << 1] = TranslucencyLightingVolumeAmbient[Idx + NumTranslucentVolumeRenderTargetSets * ViewIndex]->GetRenderTargetItem().TargetableTexture;
			RenderTargets[(Idx << 1) + 1] = TranslucencyLightingVolumeDirectional[Idx + NumTranslucentVolumeRenderTargetSets * ViewIndex]->GetRenderTargetItem().TargetableTexture;
		}

		static const FLinearColor ClearColors[Num3DTextures] = { FLinearColor::Transparent };

		if (bUseTransLightingVolBlur)
		{
			ClearVolumeTextures<Num3DTextures>(RHICmdList, CurrentFeatureLevel, RenderTargets, ClearColors);
		}
		else
		{
			ClearVolumeTextures<Num3DTextures - 2>(RHICmdList, CurrentFeatureLevel, RenderTargets, ClearColors);
		}
	}
}

/** Helper function that clears the given volume texture render targets. */
template<int32 NumRenderTargets>
void FSceneRenderTargets::ClearVolumeTextures(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors)
{
	check(!RHICmdList.IsInsideRenderPass());

	FRHIRenderPassInfo RPInfo(NumRenderTargets, RenderTargets, ERenderTargetActions::DontLoad_Store);
	TransitionRenderPassTargets(RHICmdList, RPInfo);

	RHICmdList.BeginRenderPass(RPInfo, TEXT("ClearVolumeTextures"));
	{
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();

	const int32 TranslucencyLightingVolumeDim = GetTranslucencyLightingVolumeDim();

	const FVolumeBounds VolumeBounds(TranslucencyLightingVolumeDim);
	auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FWriteToSliceVS> VertexShader(ShaderMap);
	TOptionalShaderMapRef<FWriteToSliceGS> GeometryShader(ShaderMap);
	TShaderMapRef<TOneColorPixelShaderMRT<NumRenderTargets> > PixelShader(ShaderMap);

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GScreenVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
	GraphicsPSOInit.BoundShaderState.GeometryShaderRHI = GETSAFERHISHADER_GEOMETRY(*GeometryShader);
#endif
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
	GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

	VertexShader->SetParameters(RHICmdList, VolumeBounds, FIntVector(TranslucencyLightingVolumeDim));
	if (GeometryShader.IsValid())
	{
		GeometryShader->SetParameters(RHICmdList, VolumeBounds.MinZ);
	}
	PixelShader->SetColors(RHICmdList, ClearColors, NumRenderTargets);

	RasterizeToVolumeTexture(RHICmdList, VolumeBounds);
	}
	RHICmdList.EndRenderPass();

	RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, (FRHITexture**)RenderTargets, NumRenderTargets);
}
template void FSceneRenderTargets::ClearVolumeTextures<1>(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors);
template void FSceneRenderTargets::ClearVolumeTextures<2>(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors);
template void FSceneRenderTargets::ClearVolumeTextures<3>(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors);
template void FSceneRenderTargets::ClearVolumeTextures<4>(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors);
template void FSceneRenderTargets::ClearVolumeTextures<5>(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors);
template void FSceneRenderTargets::ClearVolumeTextures<6>(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors);
template void FSceneRenderTargets::ClearVolumeTextures<7>(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors);
template void FSceneRenderTargets::ClearVolumeTextures<8>(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors);

EPixelFormat FSceneRenderTargets::GetSceneColorFormat() const
{
	return GetSceneColorFormat(CurrentFeatureLevel);
}

EPixelFormat FSceneRenderTargets::GetSceneColorFormat(ERHIFeatureLevel::Type InFeatureLevel) const
{
	EPixelFormat SceneColorBufferFormat = PF_FloatRGBA;

	if (InFeatureLevel < ERHIFeatureLevel::SM4)
	{
		return GetMobileSceneColorFormat();
	}
	else
    {
	    switch(CurrentSceneColorFormat)
	    {
		    case 0:
			    SceneColorBufferFormat = PF_R8G8B8A8; break;
		    case 1:
			    SceneColorBufferFormat = PF_A2B10G10R10; break;
		    case 2:	
			    SceneColorBufferFormat = PF_FloatR11G11B10; break;
		    case 3:	
			    SceneColorBufferFormat = PF_FloatRGB; break;
		    case 4:
			    // default
			    break;
		    case 5:
			    SceneColorBufferFormat = PF_A32B32G32R32F; break;
	    }
    
		// Fallback in case the scene color selected isn't supported.
	    if (!GPixelFormats[SceneColorBufferFormat].Supported)
	    {
		    SceneColorBufferFormat = PF_FloatRGBA;
	    }

		if (bRequireSceneColorAlpha)
		{
			SceneColorBufferFormat = PF_FloatRGBA;
		}
	}

	return SceneColorBufferFormat;
}

void FSceneRenderTargets::AllocateRenderTargets(FRHICommandListImmediate& RHICmdList, const int32 NumViews)
{
	if (BufferSize.X > 0 && BufferSize.Y > 0 && (!AreShadingPathRenderTargetsAllocated(GetSceneColorFormatType()) || !AreRenderTargetClearsValid(GetSceneColorFormatType())))
	{
		if ((EShadingPath)CurrentShadingPath == EShadingPath::Mobile)
		{
			AllocateMobileRenderTargets(RHICmdList);
		}
		else
		{
			AllocateDeferredShadingPathRenderTargets(RHICmdList, NumViews);
		}
	}
}

void FSceneRenderTargets::ReleaseSceneColor()
{
	for (auto i = 0; i < (int32)ESceneColorFormatType::Num; ++i)
	{
		SceneColor[i].SafeRelease();
	}
	// Releases what might be part of a temporal history.
	{
		SceneDepthZ.SafeRelease();
		GBufferA.SafeRelease();
	}
}

void FSceneRenderTargets::ReleaseAllTargets()
{
	ReleaseGBufferTargets();

	ReleaseSceneColor();

	SceneAlphaCopy.SafeRelease();
	SceneDepthZ.SafeRelease();
	SceneStencilSRV.SafeRelease();
	LightingChannels.SafeRelease();
	AuxiliarySceneDepthZ.SafeRelease();
	SmallDepthZ.SafeRelease();
	DBufferA.SafeRelease();
	DBufferB.SafeRelease();
	DBufferC.SafeRelease();
	ScreenSpaceAO.SafeRelease();
	QuadOverdrawBuffer.SafeRelease();
	LightAttenuation.SafeRelease();
	LightAccumulation.SafeRelease();
	DirectionalOcclusion.SafeRelease();
	CustomDepth.SafeRelease();
	MobileCustomStencil.SafeRelease();
	CustomStencilSRV.SafeRelease();

	for (int32 i = 0; i < ARRAY_COUNT(OptionalShadowDepthColor); i++)
	{
		OptionalShadowDepthColor[i].SafeRelease();
	}

	for (int32 i = 0; i < ARRAY_COUNT(ReflectionColorScratchCubemap); i++)
	{
		ReflectionColorScratchCubemap[i].SafeRelease();
	}

	for (int32 i = 0; i < ARRAY_COUNT(DiffuseIrradianceScratchCubemap); i++)
	{
		DiffuseIrradianceScratchCubemap[i].SafeRelease();
	}

	SkySHIrradianceMap.SafeRelease();

	ensure(TranslucencyLightingVolumeAmbient.Num() == TranslucencyLightingVolumeDirectional.Num());
	for (int32 RTSetIndex = 0; RTSetIndex < TranslucencyLightingVolumeAmbient.Num(); RTSetIndex++)
	{
		TranslucencyLightingVolumeAmbient[RTSetIndex].SafeRelease();
		TranslucencyLightingVolumeDirectional[RTSetIndex].SafeRelease();
	}

	MobileMultiViewSceneColor.SafeRelease();
	MobileMultiViewSceneDepthZ.SafeRelease();

	EditorPrimitivesColor.SafeRelease();
	EditorPrimitivesDepth.SafeRelease();

	VirtualTextureFeedback.ReleaseResources();
}

void FSceneRenderTargets::ReleaseDynamicRHI()
{
	ReleaseAllTargets();
	GRenderTargetPool.FreeUnusedResources();
}

/** Returns the size of the shadow depth buffer, taking into account platform limitations and game specific resolution limits. */
FIntPoint FSceneRenderTargets::GetShadowDepthTextureResolution() const
{
	int32 MaxShadowRes = CurrentMaxShadowResolution;
	const FIntPoint ShadowBufferResolution(
			FMath::Clamp(MaxShadowRes,1,(int32)GMaxShadowDepthBufferSizeX),
			FMath::Clamp(MaxShadowRes,1,(int32)GMaxShadowDepthBufferSizeY));
	
	return ShadowBufferResolution;
}

FIntPoint FSceneRenderTargets::GetPreShadowCacheTextureResolution() const
{
	const FIntPoint ShadowDepthResolution = GetShadowDepthTextureResolution();
	// Higher numbers increase cache hit rate but also memory usage
	const int32 ExpandFactor = 2;

	static auto CVarPreShadowResolutionFactor = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.Shadow.PreShadowResolutionFactor"));

	float Factor = CVarPreShadowResolutionFactor->GetValueOnRenderThread();

	FIntPoint Ret;

	Ret.X = FMath::Clamp(FMath::TruncToInt(ShadowDepthResolution.X * Factor) * ExpandFactor, 1, (int32)GMaxShadowDepthBufferSizeX);
	Ret.Y = FMath::Clamp(FMath::TruncToInt(ShadowDepthResolution.Y * Factor) * ExpandFactor, 1, (int32)GMaxShadowDepthBufferSizeY);

	return Ret;
}

FIntPoint FSceneRenderTargets::GetTranslucentShadowDepthTextureResolution() const
{
	FIntPoint ShadowDepthResolution = GetShadowDepthTextureResolution();

	int32 Factor = GetTranslucentShadowDownsampleFactor();

	ShadowDepthResolution.X = FMath::Clamp(ShadowDepthResolution.X / Factor, 1, (int32)GMaxShadowDepthBufferSizeX);
	ShadowDepthResolution.Y = FMath::Clamp(ShadowDepthResolution.Y / Factor, 1, (int32)GMaxShadowDepthBufferSizeY);

	return ShadowDepthResolution;
}

const FTextureRHIRef& FSceneRenderTargets::GetSceneColorSurface() const							
{
	if (!GetSceneColorForCurrentShadingPath())
	{
		return GBlackTexture->TextureRHI;
	}

	return (const FTextureRHIRef&)GetSceneColor()->GetRenderTargetItem().TargetableTexture;
}

const FTextureRHIRef& FSceneRenderTargets::GetSceneColorTexture() const
{
	if (!GetSceneColorForCurrentShadingPath())
	{
		return GBlackTexture->TextureRHI;
	}

	return (const FTextureRHIRef&)GetSceneColor()->GetRenderTargetItem().ShaderResourceTexture; 
}

const FUnorderedAccessViewRHIRef& FSceneRenderTargets::GetSceneColorTextureUAV() const
{
	check(GetSceneColorForCurrentShadingPath());

	return (const FUnorderedAccessViewRHIRef&)GetSceneColor()->GetRenderTargetItem().UAV;
}

const FTexture2DRHIRef* FSceneRenderTargets::GetActualDepthTexture() const
{
	const FTexture2DRHIRef* DepthTexture = NULL;
	if((CurrentFeatureLevel >= ERHIFeatureLevel::SM4) || IsPCPlatform(GShaderPlatformForFeatureLevel[CurrentFeatureLevel]))
	{
		if(GSupportsDepthFetchDuringDepthTest)
		{
			DepthTexture = &GetSceneDepthTexture();
		}
		else
		{
			DepthTexture = &GetAuxiliarySceneDepthSurface();
		}
	}
	else if (IsMobilePlatform(GShaderPlatformForFeatureLevel[CurrentFeatureLevel]))
	{
		// TODO: avoid depth texture fetch when shader needs fragment previous depth and device supports framebuffer fetch

		//bool bSceneDepthInAlpha = (GetSceneColor()->GetDesc().Format == PF_FloatRGBA);
		//bool bOnChipDepthFetch = (GSupportsShaderDepthStencilFetch || (bSceneDepthInAlpha && GSupportsShaderFramebufferFetch));
		//
		//if (bOnChipDepthFetch)
		//{
		//	DepthTexture = (const FTexture2DRHIRef*)(&GSystemTextures.DepthDummy->GetRenderTargetItem().ShaderResourceTexture);
		//}
		//else
		{
			DepthTexture = &GetSceneDepthTexture();
		}
	}

	check(DepthTexture != NULL);

	return DepthTexture;
}

IPooledRenderTarget* FSceneRenderTargets::RequestCustomDepth(FRHICommandListImmediate& RHICmdList, bool bPrimitives)
{
	int Value = CVarCustomDepth.GetValueOnRenderThread();
	const bool bCustomDepthPassWritingStencil = IsCustomDepthPassWritingStencil();
	const bool bMobilePath = (CurrentFeatureLevel <= ERHIFeatureLevel::ES3_1);

	if ((Value == 1 && bPrimitives) || Value == 2 || bCustomDepthPassWritingStencil)
	{
		bool bHasValidCustomDepth = (CustomDepth.IsValid() && BufferSize == CustomDepth->GetDesc().Extent && !GFastVRamConfig.bDirty);
		bool bHasValidCustomStencil;
		if (bMobilePath)
		{
			bHasValidCustomStencil = (MobileCustomStencil.IsValid() && BufferSize == MobileCustomStencil->GetDesc().Extent);
		}
		else
		{
			bHasValidCustomStencil = CustomStencilSRV.IsValid();
		}
						
		if (!(bHasValidCustomDepth && bHasValidCustomStencil))
		{
			// Skip depth decompression, custom depth doesn't benefit from it
			// Also disables fast clears, but typically only a small portion of custom depth is written to anyway
			uint32 CustomDepthFlags = TexCreate_NoFastClear;

			// Todo: Could check if writes stencil here and create min viable target
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_DepthStencil, FClearValueBinding::DepthFar, CustomDepthFlags, TexCreate_DepthStencilTargetable, false));
			Desc.Flags |= GFastVRamConfig.CustomDepth;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, CustomDepth, TEXT("CustomDepth"), true, ERenderTargetTransience::NonTransient);
			
			if (bMobilePath)
			{
				FPooledRenderTargetDesc MobileCustomStencilDesc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_B8G8R8A8, FClearValueBinding::Transparent, TexCreate_None, TexCreate_RenderTargetable, false));
				GRenderTargetPool.FindFreeElement(RHICmdList, MobileCustomStencilDesc, MobileCustomStencil, TEXT("MobileCustomStencil"));
			}
			else
			{
				CustomStencilSRV = RHICreateShaderResourceView((FTexture2DRHIRef&)CustomDepth->GetRenderTargetItem().TargetableTexture, 0, 1, PF_X24_G8);
			}
		}
		return CustomDepth;
	}

	return 0;
}

bool FSceneRenderTargets::IsCustomDepthPassWritingStencil()
{
	return (CVarCustomDepth.GetValueOnRenderThread() == 3);
}

/** Returns an index in the range [0, NumCubeShadowDepthSurfaces) given an input resolution. */
int32 FSceneRenderTargets::GetCubeShadowDepthZIndex(int32 ShadowResolution) const
{
	static auto CVarMinShadowResolution = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Shadow.MinResolution"));
	FIntPoint ObjectShadowBufferResolution = GetShadowDepthTextureResolution();

	// Use a lower resolution because cubemaps use a lot of memory
	ObjectShadowBufferResolution.X /= 2;
	ObjectShadowBufferResolution.Y /= 2;
	const int32 SurfaceSizes[NumCubeShadowDepthSurfaces] =
	{
		ObjectShadowBufferResolution.X,
		ObjectShadowBufferResolution.X / 2,
		ObjectShadowBufferResolution.X / 4,
		ObjectShadowBufferResolution.X / 8,
		CVarMinShadowResolution->GetValueOnRenderThread()
	};

	for (int32 SearchIndex = 0; SearchIndex < NumCubeShadowDepthSurfaces; SearchIndex++)
	{
		if (ShadowResolution >= SurfaceSizes[SearchIndex])
		{
			return SearchIndex;
		}
	}

	check(0);
	return 0;
}

/** Returns the appropriate resolution for a given cube shadow index. */
int32 FSceneRenderTargets::GetCubeShadowDepthZResolution(int32 ShadowIndex) const
{
	checkSlow(ShadowIndex >= 0 && ShadowIndex < NumCubeShadowDepthSurfaces);

	static auto CVarMinShadowResolution = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Shadow.MinResolution"));
	FIntPoint ObjectShadowBufferResolution = GetShadowDepthTextureResolution();

	// Use a lower resolution because cubemaps use a lot of memory
	ObjectShadowBufferResolution.X = FMath::Max(ObjectShadowBufferResolution.X / 2, 1);
	ObjectShadowBufferResolution.Y = FMath::Max(ObjectShadowBufferResolution.Y / 2, 1);
	const int32 SurfaceSizes[NumCubeShadowDepthSurfaces] =
	{
		ObjectShadowBufferResolution.X,
		FMath::Max(ObjectShadowBufferResolution.X / 2, 1),
		FMath::Max(ObjectShadowBufferResolution.X / 4, 1),
		FMath::Max(ObjectShadowBufferResolution.X / 8, 1),
		CVarMinShadowResolution->GetValueOnRenderThread()
	};
	return SurfaceSizes[ShadowIndex];
}

bool FSceneRenderTargets::AreRenderTargetClearsValid(ESceneColorFormatType InSceneColorFormatType) const
{
	switch (InSceneColorFormatType)
	{
	case ESceneColorFormatType::Mobile:
		{
			const TRefCountPtr<IPooledRenderTarget>& SceneColorTarget = GetSceneColorForCurrentShadingPath();
			const bool bColorValid = SceneColorTarget && (SceneColorTarget->GetRenderTargetItem().TargetableTexture->GetClearBinding() == DefaultColorClear);
			const bool bDepthValid = SceneDepthZ && (SceneDepthZ->GetRenderTargetItem().TargetableTexture->GetClearBinding() == DefaultDepthClear);
#if PLATFORM_ANDROID
			// For mobile multi-view + mono support
			const bool bMobileMultiViewColorValid = (!MobileMultiViewSceneColor || MobileMultiViewSceneColor->GetRenderTargetItem().TargetableTexture->GetClearBinding() == DefaultColorClear);
			const bool bMobileMultiViewDepthValid = (!MobileMultiViewSceneDepthZ || MobileMultiViewSceneDepthZ->GetRenderTargetItem().TargetableTexture->GetClearBinding() == DefaultDepthClear);
			return bColorValid && bDepthValid && bMobileMultiViewColorValid && bMobileMultiViewDepthValid;
#else
			return bColorValid && bDepthValid;
#endif
		}
	default:
		{
			return true;
		}
	}
}

bool FSceneRenderTargets::AreShadingPathRenderTargetsAllocated(ESceneColorFormatType InSceneColorFormatType) const
{
	switch (InSceneColorFormatType)
	{
	case ESceneColorFormatType::Mobile:
		{
			return (SceneColor[(int32)ESceneColorFormatType::Mobile] != nullptr);
		}
	case ESceneColorFormatType::HighEndWithAlpha:
		{
			return (SceneColor[(int32)ESceneColorFormatType::HighEndWithAlpha] != nullptr);
		}
	case ESceneColorFormatType::HighEnd:
		{
			return (SceneColor[(int32)ESceneColorFormatType::HighEnd] != nullptr);
		}
	default:
		{
			checkNoEntry();
			return false;
		}
	}
}

/*-----------------------------------------------------------------------------
FSceneTextureShaderParameters
-----------------------------------------------------------------------------*/

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FSceneTexturesUniformParameters, "SceneTexturesStruct");

void SetupSceneTextureUniformParameters(
	FSceneRenderTargets& SceneContext,
	ERHIFeatureLevel::Type FeatureLevel,
	ESceneTextureSetupMode SetupMode,
	FSceneTexturesUniformParameters& SceneTextureParameters)
{
	FRHITexture* WhiteDefault2D = GSystemTextures.WhiteDummy->GetRenderTargetItem().ShaderResourceTexture;
	FRHITexture* BlackDefault2D = GSystemTextures.BlackDummy->GetRenderTargetItem().ShaderResourceTexture;
	FRHITexture* DepthDefault = GSystemTextures.DepthDummy->GetRenderTargetItem().ShaderResourceTexture;

	// Scene Color / Depth
	{
		const bool bSetupDepth = (SetupMode & ESceneTextureSetupMode::SceneDepth) != ESceneTextureSetupMode::None;
		SceneTextureParameters.SceneColorTexture = bSetupDepth ? SceneContext.GetSceneColorTexture().GetReference() : BlackDefault2D;
		SceneTextureParameters.SceneColorTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		const FTexture2DRHIRef* ActualDepthTexture = SceneContext.GetActualDepthTexture();
		SceneTextureParameters.SceneDepthTexture = bSetupDepth && ActualDepthTexture ? (*ActualDepthTexture).GetReference() : DepthDefault;

		if (bSetupDepth && SceneContext.IsSeparateTranslucencyPass() && SceneContext.IsDownsampledTranslucencyDepthValid())
		{
			FIntPoint OutScaledSize;
			float OutScale;
			SceneContext.GetSeparateTranslucencyDimensions(OutScaledSize, OutScale);

			if (OutScale < 1.0f)
			{
				SceneTextureParameters.SceneDepthTexture = SceneContext.GetDownsampledTranslucencyDepthSurface();
			}
		}

		if (bSetupDepth)
		{
			SceneTextureParameters.SceneDepthTextureNonMS = GSupportsDepthFetchDuringDepthTest ? SceneContext.GetSceneDepthTexture() : SceneContext.GetAuxiliarySceneDepthSurface();
		}
		else
		{
			SceneTextureParameters.SceneDepthTextureNonMS = DepthDefault;
		}

		SceneTextureParameters.SceneDepthTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		SceneTextureParameters.SceneStencilTexture = bSetupDepth && SceneContext.SceneStencilSRV ? SceneContext.SceneStencilSRV : GNullColorVertexBuffer.VertexBufferSRV;
	}

	// GBuffer
	{
		const bool bSetupGBuffers = (SetupMode & ESceneTextureSetupMode::GBuffers) != ESceneTextureSetupMode::None;
		const EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(FeatureLevel);
		const bool bUseGBuffer = IsUsingGBuffers(ShaderPlatform);
		const bool bCanReadGBufferUniforms = bSetupGBuffers && (bUseGBuffer || IsSimpleForwardShadingEnabled(ShaderPlatform));

		// Allocate the Gbuffer resource uniform buffer.
		const FSceneRenderTargetItem& GBufferAToUse = bCanReadGBufferUniforms && SceneContext.GBufferA ? SceneContext.GBufferA->GetRenderTargetItem() : GSystemTextures.BlackDummy->GetRenderTargetItem();
		const FSceneRenderTargetItem& GBufferBToUse = bCanReadGBufferUniforms && SceneContext.GBufferB ? SceneContext.GBufferB->GetRenderTargetItem() : GSystemTextures.BlackDummy->GetRenderTargetItem();
		const FSceneRenderTargetItem& GBufferCToUse = bCanReadGBufferUniforms && SceneContext.GBufferC ? SceneContext.GBufferC->GetRenderTargetItem() : GSystemTextures.BlackDummy->GetRenderTargetItem();
		const FSceneRenderTargetItem& GBufferDToUse = bCanReadGBufferUniforms && SceneContext.GBufferD ? SceneContext.GBufferD->GetRenderTargetItem() : GSystemTextures.BlackDummy->GetRenderTargetItem();
		const FSceneRenderTargetItem& GBufferEToUse = bCanReadGBufferUniforms && SceneContext.GBufferE ? SceneContext.GBufferE->GetRenderTargetItem() : GSystemTextures.BlackDummy->GetRenderTargetItem();
		const FSceneRenderTargetItem& GBufferVelocityToUse = bCanReadGBufferUniforms && SceneContext.SceneVelocity ? SceneContext.SceneVelocity->GetRenderTargetItem() : GSystemTextures.BlackDummy->GetRenderTargetItem();

		SceneTextureParameters.GBufferATexture = GBufferAToUse.ShaderResourceTexture;
		SceneTextureParameters.GBufferBTexture = GBufferBToUse.ShaderResourceTexture;
		SceneTextureParameters.GBufferCTexture = GBufferCToUse.ShaderResourceTexture;
		SceneTextureParameters.GBufferDTexture = GBufferDToUse.ShaderResourceTexture;
		SceneTextureParameters.GBufferETexture = GBufferEToUse.ShaderResourceTexture;
		SceneTextureParameters.GBufferVelocityTexture = GBufferVelocityToUse.ShaderResourceTexture;
		
		SceneTextureParameters.GBufferATextureNonMS = GBufferAToUse.ShaderResourceTexture;
		SceneTextureParameters.GBufferBTextureNonMS = GBufferBToUse.ShaderResourceTexture;
		SceneTextureParameters.GBufferCTextureNonMS = GBufferCToUse.ShaderResourceTexture;
		SceneTextureParameters.GBufferDTextureNonMS = GBufferDToUse.ShaderResourceTexture;
		SceneTextureParameters.GBufferETextureNonMS = GBufferEToUse.ShaderResourceTexture;
		SceneTextureParameters.GBufferVelocityTextureNonMS = GBufferVelocityToUse.ShaderResourceTexture;

		SceneTextureParameters.GBufferATextureSampler = TStaticSamplerState<>::GetRHI();
		SceneTextureParameters.GBufferBTextureSampler = TStaticSamplerState<>::GetRHI();
		SceneTextureParameters.GBufferCTextureSampler = TStaticSamplerState<>::GetRHI();
		SceneTextureParameters.GBufferDTextureSampler = TStaticSamplerState<>::GetRHI();
		SceneTextureParameters.GBufferETextureSampler = TStaticSamplerState<>::GetRHI();
		SceneTextureParameters.GBufferVelocityTextureSampler = TStaticSamplerState<>::GetRHI();
	}
	
	// SSAO
	{
		const bool bSetupSSAO = (SetupMode & ESceneTextureSetupMode::SSAO) != ESceneTextureSetupMode::None;
		SceneTextureParameters.ScreenSpaceAOTexture = bSetupSSAO && SceneContext.bScreenSpaceAOIsValid ? SceneContext.ScreenSpaceAO->GetRenderTargetItem().ShaderResourceTexture.GetReference() : WhiteDefault2D;
		SceneTextureParameters.ScreenSpaceAOTextureSampler = TStaticSamplerState<>::GetRHI();
	}
	
	// Custom Depth / Stencil
	{
		const bool bSetupCustomDepth = (SetupMode & ESceneTextureSetupMode::CustomDepth) != ESceneTextureSetupMode::None;

		FRHITexture* CustomDepth = DepthDefault;
		FRHIShaderResourceView* CustomStencilSRV = GSystemTextures.WhiteDummySRV;

		// if there is no custom depth it's better to have the far distance there
		IPooledRenderTarget* CustomDepthTarget = SceneContext.bCustomDepthIsValid ? SceneContext.CustomDepth.GetReference() : 0;
		if (bSetupCustomDepth && CustomDepthTarget)
		{
			CustomDepth = CustomDepthTarget->GetRenderTargetItem().ShaderResourceTexture;
		}

		if (bSetupCustomDepth && SceneContext.bCustomDepthIsValid && SceneContext.CustomStencilSRV.GetReference())
		{
			CustomStencilSRV = SceneContext.CustomStencilSRV;
		}

		SceneTextureParameters.CustomDepthTexture = CustomDepth;
		SceneTextureParameters.CustomDepthTextureSampler = TStaticSamplerState<>::GetRHI();
		SceneTextureParameters.CustomDepthTextureNonMS = CustomDepth;
		SceneTextureParameters.CustomStencilTexture = CustomStencilSRV;
	}

	// Misc
	{
		// Setup by passes that support it
		SceneTextureParameters.EyeAdaptation = GWhiteTexture->TextureRHI; 
		SceneTextureParameters.SceneColorCopyTexture = BlackDefault2D;
		SceneTextureParameters.SceneColorCopyTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	}
}

TUniformBufferRef<FSceneTexturesUniformParameters> CreateSceneTextureUniformBuffer(
	FSceneRenderTargets& SceneContext,
	ERHIFeatureLevel::Type FeatureLevel,
	ESceneTextureSetupMode SetupMode,
	EUniformBufferUsage Usage)
{
	FSceneTexturesUniformParameters SceneTextures;
	SetupSceneTextureUniformParameters(SceneContext, FeatureLevel, SetupMode, SceneTextures);
	return CreateUniformBufferImmediate(SceneTextures, Usage);
}

template< typename TRHICmdList >
TUniformBufferRef<FSceneTexturesUniformParameters> CreateSceneTextureUniformBufferSingleDraw(TRHICmdList& RHICmdList, ESceneTextureSetupMode SceneTextureSetupMode, ERHIFeatureLevel::Type FeatureLevel) 
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	FSceneTexturesUniformParameters SceneTextureParameters;
	SetupSceneTextureUniformParameters(SceneContext, FeatureLevel, SceneTextureSetupMode, SceneTextureParameters);
	return TUniformBufferRef<FSceneTexturesUniformParameters>::CreateUniformBufferImmediate(SceneTextureParameters, UniformBuffer_SingleDraw);
}

#define IMPLEMENT_CreateSceneTextureUniformBuffer( TRHICmdList ) \
	template TUniformBufferRef<FSceneTexturesUniformParameters> CreateSceneTextureUniformBufferSingleDraw< TRHICmdList >(\
		TRHICmdList& RHICmdList,						\
		ESceneTextureSetupMode SceneTextureSetupMode,	\
		ERHIFeatureLevel::Type FeatureLevel				\
	);

IMPLEMENT_CreateSceneTextureUniformBuffer( FRHICommandList );
IMPLEMENT_CreateSceneTextureUniformBuffer( FRHICommandListImmediate );
IMPLEMENT_CreateSceneTextureUniformBuffer( FRHIAsyncComputeCommandListImmediate );

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FMobileSceneTextureUniformParameters, "MobileSceneTextures");

void SetupMobileSceneTextureUniformParameters(
	FSceneRenderTargets& SceneContext,
	ERHIFeatureLevel::Type FeatureLevel,
	bool bSceneTexturesValid,
	FMobileSceneTextureUniformParameters& SceneTextureParameters)
{
	FRHITexture* BlackDefault2D = GSystemTextures.BlackDummy->GetRenderTargetItem().ShaderResourceTexture;
	FRHITexture* DepthDefault = GSystemTextures.DepthDummy->GetRenderTargetItem().ShaderResourceTexture;

	SceneTextureParameters.SceneColorTexture = bSceneTexturesValid ? SceneContext.GetSceneColorTexture().GetReference() : BlackDefault2D;
	SceneTextureParameters.SceneColorTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	const FTexture2DRHIRef* ActualDepthTexture = SceneContext.GetActualDepthTexture();
	SceneTextureParameters.SceneDepthTexture = bSceneTexturesValid && ActualDepthTexture ? (*ActualDepthTexture).GetReference() : DepthDefault;
	SceneTextureParameters.SceneDepthTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	SceneTextureParameters.SceneAlphaCopyTexture = bSceneTexturesValid && SceneContext.HasSceneAlphaCopyTexture() ? SceneContext.GetSceneAlphaCopyTexture() : BlackDefault2D;
	SceneTextureParameters.SceneAlphaCopyTextureSampler = TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI();

	FRHITexture* CustomDepth = DepthDefault;

	// if there is no custom depth it's better to have the far distance there
	IPooledRenderTarget* CustomDepthTarget = SceneContext.bCustomDepthIsValid ? SceneContext.CustomDepth.GetReference() : 0;
	if (CustomDepthTarget)
	{
		CustomDepth = CustomDepthTarget->GetRenderTargetItem().ShaderResourceTexture;
	}

	SceneTextureParameters.CustomDepthTexture = CustomDepth;
	SceneTextureParameters.CustomDepthTextureSampler = TStaticSamplerState<>::GetRHI();

	FRHITexture* MobileCustomStencil = BlackDefault2D;

	if (SceneContext.MobileCustomStencil.IsValid())
	{
		MobileCustomStencil = SceneContext.MobileCustomStencil->GetRenderTargetItem().ShaderResourceTexture;
	}

	SceneTextureParameters.MobileCustomStencilTexture = MobileCustomStencil;
	SceneTextureParameters.MobileCustomStencilTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
}

template< typename TRHICmdList >
TUniformBufferRef<FMobileSceneTextureUniformParameters> CreateMobileSceneTextureUniformBufferSingleDraw(TRHICmdList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel) 
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	FMobileSceneTextureUniformParameters SceneTextureParameters;
	SetupMobileSceneTextureUniformParameters(SceneContext, FeatureLevel, true, SceneTextureParameters);
	return TUniformBufferRef<FMobileSceneTextureUniformParameters>::CreateUniformBufferImmediate(SceneTextureParameters, UniformBuffer_SingleDraw);
}

#define IMPLEMENT_CreateMobileSceneTextureUniformBuffer( TRHICmdList ) \
	template TUniformBufferRef<FMobileSceneTextureUniformParameters> CreateMobileSceneTextureUniformBufferSingleDraw< TRHICmdList >(\
		TRHICmdList& RHICmdList,						\
		ERHIFeatureLevel::Type FeatureLevel				\
	);

IMPLEMENT_CreateMobileSceneTextureUniformBuffer( FRHICommandList );
IMPLEMENT_CreateMobileSceneTextureUniformBuffer( FRHICommandListImmediate );
IMPLEMENT_CreateMobileSceneTextureUniformBuffer( FRHIAsyncComputeCommandListImmediate );

void BindSceneTextureUniformBufferDependentOnShadingPath(
	const FShader::CompiledShaderInitializerType& Initializer, 
	FShaderUniformBufferParameter& SceneTexturesUniformBuffer, 
	FShaderUniformBufferParameter& MobileSceneTexturesUniformBuffer)
{
	const ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel((EShaderPlatform)Initializer.Target.Platform);
		
	if (FSceneInterface::GetShadingPath(FeatureLevel) == EShadingPath::Deferred)
	{
		SceneTexturesUniformBuffer.Bind(Initializer.ParameterMap, FSceneTexturesUniformParameters::StaticStructMetadata.GetShaderVariableName());
		checkfSlow(!Initializer.ParameterMap.ContainsParameterAllocation(FMobileSceneTextureUniformParameters::StaticStructMetadata.GetShaderVariableName()), TEXT("Shader for Deferred shading path tried to bind FMobileSceneTextureUniformParameters which is only available in the mobile shading path: %s"), Initializer.Type->GetName());
	}
		
	if (FSceneInterface::GetShadingPath(FeatureLevel) == EShadingPath::Mobile)
	{
		MobileSceneTexturesUniformBuffer.Bind(Initializer.ParameterMap, FMobileSceneTextureUniformParameters::StaticStructMetadata.GetShaderVariableName());
		checkfSlow(!Initializer.ParameterMap.ContainsParameterAllocation(FSceneTexturesUniformParameters::StaticStructMetadata.GetShaderVariableName()), TEXT("Shader for Mobile shading path tried to bind FSceneTexturesUniformParameters which is only available in the deferred shading path: %s"), Initializer.Type->GetName());
	}
}
