// Copyright Epic Games, Inc. All Rights Reserved.

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
#include "GpuDebugRendering.h"

IMPLEMENT_TYPE_LAYOUT(FSceneTextureShaderParameters);

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

static TAutoConsoleVariable<int32> CVarSceneTargetsResizeMethod(
	TEXT("r.SceneRenderTargetResizeMethod"),
	0,
	TEXT("Control the scene render target resize method:\n")
	TEXT("(This value is only used in game mode and on windowing platforms unless 'r.SceneRenderTargetsResizingMethodForceOverride' is enabled.)\n")
	TEXT("0: Resize to match requested render size (Default) (Least memory use, can cause stalls when size changes e.g. ScreenPercentage)\n")
	TEXT("1: Fixed to screen resolution.\n")
	TEXT("2: Expands to encompass the largest requested render dimension. (Most memory use, least prone to allocation stalls.)"),	
	ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarSceneTargetsResizeMethodForceOverride(
	TEXT("r.SceneRenderTargetResizeMethodForceOverride"),
	0,
	TEXT("Forces 'r.SceneRenderTargetResizeMethod' to be respected on all configurations.\n")
	TEXT("0: Disabled.\n")
	TEXT("1: Enabled.\n"),
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

static TAutoConsoleVariable<int32> CVarMobileCustomDepthDownSample(
	TEXT("r.Mobile.CustomDepthDownSample"),
	0,
	TEXT("Perform Mobile CustomDepth at HalfRes \n ")
	TEXT("0: Off (default)\n ")
	TEXT("1: On \n "),
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
	, SmallDepthZ(GRenderTargetPool.MakeSnapshot(SnapshotSource.SmallDepthZ))
	, GBufferA(GRenderTargetPool.MakeSnapshot(SnapshotSource.GBufferA))
	, GBufferB(GRenderTargetPool.MakeSnapshot(SnapshotSource.GBufferB))
	, GBufferC(GRenderTargetPool.MakeSnapshot(SnapshotSource.GBufferC))
	, GBufferD(GRenderTargetPool.MakeSnapshot(SnapshotSource.GBufferD))
	, GBufferE(GRenderTargetPool.MakeSnapshot(SnapshotSource.GBufferE))
	, GBufferF(GRenderTargetPool.MakeSnapshot(SnapshotSource.GBufferF))
	, DBufferA(GRenderTargetPool.MakeSnapshot(SnapshotSource.DBufferA))
	, DBufferB(GRenderTargetPool.MakeSnapshot(SnapshotSource.DBufferB))
	, DBufferC(GRenderTargetPool.MakeSnapshot(SnapshotSource.DBufferC))
	, DBufferMask(GRenderTargetPool.MakeSnapshot(SnapshotSource.DBufferMask))
	, ScreenSpaceAO(GRenderTargetPool.MakeSnapshot(SnapshotSource.ScreenSpaceAO))
	, ScreenSpaceGTAOHorizons(GRenderTargetPool.MakeSnapshot(SnapshotSource.ScreenSpaceGTAOHorizons))
	, QuadOverdrawBuffer(GRenderTargetPool.MakeSnapshot(SnapshotSource.QuadOverdrawBuffer))
	, CustomDepth(GRenderTargetPool.MakeSnapshot(SnapshotSource.CustomDepth))
	, MobileCustomDepth(GRenderTargetPool.MakeSnapshot(SnapshotSource.MobileCustomDepth))
	, MobileCustomStencil(GRenderTargetPool.MakeSnapshot(SnapshotSource.MobileCustomStencil))
	, CustomStencilSRV(SnapshotSource.CustomStencilSRV)
	, SkySHIrradianceMap(GRenderTargetPool.MakeSnapshot(SnapshotSource.SkySHIrradianceMap))
	, MobileMultiViewSceneColor(GRenderTargetPool.MakeSnapshot(SnapshotSource.MobileMultiViewSceneColor))
	, MobileMultiViewSceneDepthZ(GRenderTargetPool.MakeSnapshot(SnapshotSource.MobileMultiViewSceneDepthZ))
	, EditorPrimitivesColor(GRenderTargetPool.MakeSnapshot(SnapshotSource.EditorPrimitivesColor))
	, EditorPrimitivesDepth(GRenderTargetPool.MakeSnapshot(SnapshotSource.EditorPrimitivesDepth))
	, SeparateTranslucencyRT(SnapshotSource.SeparateTranslucencyRT)
	, SeparateTranslucencyModulateRT(SnapshotSource.SeparateTranslucencyModulateRT)
	, DownsampledTranslucencyDepthRT(SnapshotSource.DownsampledTranslucencyDepthRT)
	, FoveationTexture(GRenderTargetPool.MakeSnapshot(SnapshotSource.FoveationTexture))
	, bScreenSpaceAOIsValid(SnapshotSource.bScreenSpaceAOIsValid)
	, bCustomDepthIsValid(SnapshotSource.bCustomDepthIsValid)
	, GBufferRefCount(SnapshotSource.GBufferRefCount)
	, ThisFrameNumber(SnapshotSource.ThisFrameNumber)
	, CurrentDesiredSizeIndex(SnapshotSource.CurrentDesiredSizeIndex)
	, bVelocityPass(SnapshotSource.bVelocityPass)
	, bSeparateTranslucencyPass(SnapshotSource.bSeparateTranslucencyPass)
	, BufferSize(SnapshotSource.BufferSize)
	, SeparateTranslucencyBufferSize(SnapshotSource.SeparateTranslucencyBufferSize)
	, LastStereoSize(SnapshotSource.LastStereoSize)
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
	, bKeepDepthContent(SnapshotSource.bKeepDepthContent)
	, bAllocatedFoveationTexture(SnapshotSource.bAllocatedFoveationTexture)
	, bAllowTangentGBuffer(SnapshotSource.bAllowTangentGBuffer)
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
}

inline const TCHAR* GetSceneColorTargetName(EShadingPath ShadingPath)
{
	const TCHAR* SceneColorNames[(uint32)EShadingPath::Num] =
	{ 
		TEXT("SceneColorMobile"), 
		TEXT("SceneColorDeferred")
	};
	check((uint32)ShadingPath < UE_ARRAY_COUNT(SceneColorNames));
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
		bIsVRScene |= (IStereoRendering::IsStereoEyeView(*View) && GEngine->XRSystem.IsValid());
	}

	FIntPoint DesiredBufferSize = FIntPoint::ZeroValue;
	FIntPoint DesiredFamilyBufferSize = FSceneRenderer::GetDesiredInternalBufferSize(ViewFamily);

	{
		bool bUseResizeMethodCVar = true;

		if (CVarSceneTargetsResizeMethodForceOverride.GetValueOnRenderThread() != 1)
		{
			if (!FPlatformProperties::SupportsWindowedMode() || bIsVRScene)
			{
				if (bIsVRScene)
				{
					if (!bIsSceneCapture && !bIsReflectionCapture)
					{
						// If this isn't a scene capture, and it's a VR scene, and the size has changed since the last time we
						// rendered a VR scene (or this is the first time), use the requested size method.
						if (DesiredFamilyBufferSize.X != LastStereoSize.X || DesiredFamilyBufferSize.Y != LastStereoSize.Y)
						{
							LastStereoSize = DesiredFamilyBufferSize;
							SceneTargetsSizingMethod = RequestedSize;
							UE_LOG(LogRenderer, Warning, TEXT("Resizing VR buffer to %d by %d"), DesiredFamilyBufferSize.X, DesiredFamilyBufferSize.Y);
						}
						else
						{
							// Otherwise use the grow method.
							SceneTargetsSizingMethod = Grow;
						}
					}
					else
					{
						// If this is a scene capture, and it's smaller than the VR view size, then don't re-allocate buffers, just use the "grow" method.
						// If it's bigger than the VR view, then log a warning, and use resize method.
						if (DesiredFamilyBufferSize.X > LastStereoSize.X || DesiredFamilyBufferSize.Y > LastStereoSize.Y)
						{
							if (LastStereoSize.X > 0 && bIsSceneCapture)
							{
								static bool DisplayedCaptureSizeWarning = false;
								if (!DisplayedCaptureSizeWarning)
								{
									DisplayedCaptureSizeWarning = true;
									UE_LOG(LogRenderer, Warning, TEXT("Scene capture of %d by %d is larger than the current VR target. If this is deliberate for a capture that is being done for multiple frames, consider the performance and memory implications. To disable this warning and ensure optimal behavior with this path, set r.SceneRenderTargetResizeMethod to 2, and r.SceneRenderTargetResizeMethodForceOverride to 1."), DesiredFamilyBufferSize.X, DesiredFamilyBufferSize.Y);
								}
							}
							SceneTargetsSizingMethod = RequestedSize;
						}
						else
						{
							SceneTargetsSizingMethod = Grow;
						}
					}
				}
				else
				{
					// Force ScreenRes on non windowed platforms.
					SceneTargetsSizingMethod = RequestedSize;
				}
				bUseResizeMethodCVar = false;
			}
			else if (GIsEditor)
			{
				// Always grow scene render targets in the editor.
				SceneTargetsSizingMethod = Grow;
				bUseResizeMethodCVar = false;
			}
		}

		if (bUseResizeMethodCVar)
		{
			// Otherwise use the setting specified by the console variable.
			SceneTargetsSizingMethod = (ESizingMethods)FMath::Clamp(CVarSceneTargetsResizeMethod.GetValueOnRenderThread(), 0, (int32)VisibleSizingMethodsCount);
		}
	}

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
	bool bAllowDelayResize = !GIsHighResScreenshot && !bIsVRScene;

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

	if (InFeatureLevel >= ERHIFeatureLevel::SM5)
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

int32 FSceneRenderTargets::GetGBufferRenderTargets(TRefCountPtr<IPooledRenderTarget>* OutRenderTargets[MaxSimultaneousRenderTargets], int32& OutVelocityRTIndex, int32& OutTangentRTIndex)
{
	int32 MRTCount = 0;
	OutRenderTargets[MRTCount++] = &GetSceneColor();

	const EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(CurrentFeatureLevel);
	const bool bUseGBuffer = IsUsingGBuffers(ShaderPlatform);

	if (bUseGBuffer)
	{
		OutRenderTargets[MRTCount++] = &GBufferA;
		OutRenderTargets[MRTCount++] = &GBufferB;
		OutRenderTargets[MRTCount++] = &GBufferC;
	}

	// The velocity buffer needs to be bound before other optionnal rendertargets (when UseSelectiveBasePassOutputs() is true).
	// Otherwise there is an issue on some AMD hardware where the target does not get updated. Seems to be related to the velocity buffer format as it works fine with other targets.
	if (bAllocateVelocityGBuffer && !IsSimpleForwardShadingEnabled(ShaderPlatform))
	{
		OutVelocityRTIndex = MRTCount;
		check(OutVelocityRTIndex == 4 || (!bUseGBuffer && OutVelocityRTIndex == 1)); // As defined in BasePassPixelShader.usf
		OutRenderTargets[MRTCount++] = &SceneVelocity;
	}
	else
	{
		OutVelocityRTIndex = -1;
	}

	if (bUseGBuffer && bAllowTangentGBuffer)
	{
		check(!bAllocateVelocityGBuffer);
		OutTangentRTIndex = MRTCount;

		OutRenderTargets[MRTCount++] = &GBufferF;
	}
	else
	{
		OutTangentRTIndex = -1;
	}

	if (bUseGBuffer)
	{
		OutRenderTargets[MRTCount++] = &GBufferD;

		if (bAllowStaticLighting)
		{
			check(MRTCount == (bAllocateVelocityGBuffer || bAllowTangentGBuffer ? 6 : 5)); // As defined in BasePassPixelShader.usf
			OutRenderTargets[MRTCount++] = &GBufferE;
		}
	}

	check(MRTCount <= MaxSimultaneousRenderTargets);
	return MRTCount;
}

int32 FSceneRenderTargets::FillGBufferRenderPassInfo(ERenderTargetLoadAction ColorLoadAction, FRHIRenderPassInfo& OutRenderPassInfo, int32& OutVelocityRTIndex, int32& OutTangentRTIndex)
{
	TRefCountPtr<IPooledRenderTarget>* RenderTargets[MaxSimultaneousRenderTargets];
	int32 MRTCount = GetGBufferRenderTargets(RenderTargets, OutVelocityRTIndex, OutTangentRTIndex);

	for (int32 MRTIdx = 0; MRTIdx < MRTCount; ++MRTIdx)
	{
		OutRenderPassInfo.ColorRenderTargets[MRTIdx].Action = MakeRenderTargetActions(ColorLoadAction, ERenderTargetStoreAction::EStore);
		OutRenderPassInfo.ColorRenderTargets[MRTIdx].RenderTarget = (*RenderTargets[MRTIdx])->GetRenderTargetItem().TargetableTexture;
		OutRenderPassInfo.ColorRenderTargets[MRTIdx].ArraySlice = -1;
		OutRenderPassInfo.ColorRenderTargets[MRTIdx].MipIndex = 0;
	}

	return MRTCount;
}

int32 FSceneRenderTargets::GetGBufferRenderTargets(ERenderTargetLoadAction ColorLoadAction, FRHIRenderTargetView OutRenderTargets[MaxSimultaneousRenderTargets], int32& OutVelocityRTIndex, int32& OutTangentRTIndex)
{
	TRefCountPtr<IPooledRenderTarget>* RenderTargets[MaxSimultaneousRenderTargets];
	int32 MRTCount = GetGBufferRenderTargets(RenderTargets, OutVelocityRTIndex, OutTangentRTIndex);

	for (int32 MRTIdx = 0; MRTIdx < MRTCount; ++MRTIdx)
	{
		OutRenderTargets[MRTIdx] = FRHIRenderTargetView((*RenderTargets[MRTIdx])->GetRenderTargetItem().TargetableTexture, 0, -1, ColorLoadAction, ERenderTargetStoreAction::EStore);
	}

	return MRTCount;
}

int32 FSceneRenderTargets::GetGBufferRenderTargets(FRDGBuilder& GraphBuilder, ERenderTargetLoadAction ColorLoadAction, FRenderTargetBinding OutRenderTargets[MaxSimultaneousRenderTargets], int32& OutVelocityRTIndex, int32& OutTangentRTIndex)
{
	TRefCountPtr<IPooledRenderTarget>* RenderTargets[MaxSimultaneousRenderTargets];
	int32 MRTCount = GetGBufferRenderTargets(RenderTargets, OutVelocityRTIndex, OutTangentRTIndex);

	for (int32 MRTIdx = 0; MRTIdx < MRTCount; ++MRTIdx)
	{
		FPooledRenderTargetDesc Desc = (*RenderTargets[MRTIdx])->GetDesc();
		FRDGTextureRef RDGTextureRef = GraphBuilder.RegisterExternalTexture(*RenderTargets[MRTIdx], Desc.DebugName);

		OutRenderTargets[MRTIdx] = FRenderTargetBinding(RDGTextureRef, ColorLoadAction);
	}

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
		return FVelocityRendering::BasePassCanOutputVelocity(FeatureLevel) || BasePassCanOutputTangent(FeatureLevel) ? 7 : 6;
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
			if (bClearQuadOverdrawBuffers)
			{
				// Clear to default value
				RHICmdList.ClearUAVUint(QuadOverdrawBuffer->GetRenderTargetItem().UAV, FUintVector4(0, 0, 0, 0));
				RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToGfx, QuadOverdrawBuffer->GetRenderTargetItem().UAV);
			}
		}
	}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
}
FUnorderedAccessViewRHIRef FSceneRenderTargets::GetQuadOverdrawBufferUAV()
{
	const EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(CurrentFeatureLevel);
	bool bBindQuadOverdrawBuffers = true;
	if (bBindQuadOverdrawBuffers && AllowDebugViewShaderMode(DVSM_QuadComplexity, ShaderPlatform, CurrentFeatureLevel))
	{
		if (QuadOverdrawBuffer.IsValid() && QuadOverdrawBuffer->GetRenderTargetItem().UAV.IsValid())
		{
			return QuadOverdrawBuffer->GetRenderTargetItem().UAV;
		}
	}
	return GBlackTextureWithUAV->UnorderedAccessViewRHI;
}



FUnorderedAccessViewRHIRef FSceneRenderTargets::GetVirtualTextureFeedbackUAV()
{
	if (!VirtualTextureFeedback.FeedbackBufferUAV)
	{
		return GEmptyVertexBufferWithUAV->UnorderedAccessViewRHI;
	}
	
	return VirtualTextureFeedback.FeedbackBufferUAV;
}

void FSceneRenderTargets::BindVirtualTextureFeedbackUAV(FRHIRenderPassInfo& RPInfo)
{
	return;
}

void FSceneRenderTargets::BeginRenderingGBuffer(FRHICommandList& RHICmdList, ERenderTargetLoadAction ColorLoadAction, ERenderTargetLoadAction DepthLoadAction, FExclusiveDepthStencil DepthStencilAccess, bool bBindQuadOverdrawBuffers, bool bClearQuadOverdrawBuffers, const FLinearColor& ClearColor/*=(0,0,0,1)*/, bool bIsWireframe)
{
	check(RHICmdList.IsOutsideRenderPass());

	SCOPED_DRAW_EVENT(RHICmdList, BeginRenderingGBuffer);
	check(CurrentFeatureLevel >= ERHIFeatureLevel::SM5);
	AllocSceneColor(RHICmdList);

	const ERenderTargetStoreAction DepthStoreAction = DepthStencilAccess.IsDepthWrite() ? ERenderTargetStoreAction::EStore : ERenderTargetStoreAction::ENoAction;

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
	int32 TangentRTIndex = -1;
	FRHIRenderPassInfo RPInfo;
	int32 MRTCount = FillGBufferRenderPassInfo(ColorLoadAction, RPInfo, VelocityRTIndex, TangentRTIndex);

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

	RHICmdList.TransitionResource(DepthStencilAccess, RPInfo.DepthStencilRenderTarget.DepthStencilTarget);

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

void FSceneRenderTargets::FinishGBufferPassAndResolve(FRHICommandListImmediate& RHICmdList, FExclusiveDepthStencil DepthStencilAccess)
{
	RHICmdList.EndRenderPass();

	const EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(CurrentFeatureLevel);
	if (IsSimpleForwardShadingEnabled(ShaderPlatform))
	{
		// Currently nothing to do here for simple forward shading.
		return;
	}

	int32 VelocityRTIndex = -1;
	int32 TangentRTIndex = -1;
	FRHIRenderTargetView RenderTargets[MaxSimultaneousRenderTargets];
	int32 NumMRTs = GetGBufferRenderTargets(ERenderTargetLoadAction::ELoad, RenderTargets, VelocityRTIndex, TangentRTIndex);

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

	// If depth or stencil writes are enabled, transition the depth buffer back to readable.
	// It was set to writable in BeginRenderingGBuffer().
	RHICmdList.TransitionResource(DepthStencilAccess.GetReadableTransition(), GetSceneDepthSurface());
}

int32 FSceneRenderTargets::GetNumGBufferTargets() const
{
	int32 NumGBufferTargets = 1;

	if (CurrentFeatureLevel >= ERHIFeatureLevel::SM5)
	{
		const EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(CurrentFeatureLevel);
		if (IsUsingGBuffers(ShaderPlatform))
		{
			// This needs to match TBasePassPixelShaderBaseType::ModifyCompilationEnvironment()
			NumGBufferTargets = bAllowStaticLighting ? 6 : 5;
		}

		if (bAllocateVelocityGBuffer || bAllowTangentGBuffer)
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
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, SceneColorBufferFormat, DefaultColorClear, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
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

	bool MobileHWsRGB = IsMobileColorsRGB();

	if (!MobileMultiViewSceneColor)
	{
		const EPixelFormat SceneColorBufferFormat = GetSceneColorFormat();
		const FIntPoint MultiViewBufferSize(BufferSize.X / ScaleFactor, BufferSize.Y);

		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(MultiViewBufferSize, SceneColorBufferFormat, DefaultColorClear, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
		Desc.NumSamples = GetNumSceneColorMSAASamples(CurrentFeatureLevel);
		Desc.ArraySize = 2;
		Desc.bIsArray = true;
		Desc.Flags |= MobileHWsRGB ? TexCreate_SRGB : 0;
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
	GBufferF.SafeRelease();
	SceneVelocity.SafeRelease();
}

void FSceneRenderTargets::PreallocGBufferTargets()
{
	bAllocateVelocityGBuffer = FVelocityRendering::BasePassCanOutputVelocity(CurrentFeatureLevel);
	bAllowTangentGBuffer = BasePassCanOutputTangent(CurrentFeatureLevel);
}

EPixelFormat FSceneRenderTargets::GetGBufferAFormat() const
{
	// good to see the quality loss due to precision in the gbuffer
	const bool bHighPrecisionGBuffers = (CurrentGBufferFormat >= EGBufferFormat::Force16BitsPerChannel);
	// good to profile the impact of non 8 bit formats
	const bool bEnforce8BitPerChannel = (CurrentGBufferFormat == EGBufferFormat::Force8BitsPerChannel);

	EPixelFormat NormalGBufferFormat = bHighPrecisionGBuffers ? PF_FloatRGBA : PF_A2B10G10R10;

	if (bEnforce8BitPerChannel)
	{
		NormalGBufferFormat = PF_B8G8R8A8;
	}
	else if (CurrentGBufferFormat == EGBufferFormat::HighPrecisionNormals)
	{
		NormalGBufferFormat = PF_FloatRGBA;
	}

	return NormalGBufferFormat;
}

EPixelFormat FSceneRenderTargets::GetGBufferBFormat() const
{
	// good to see the quality loss due to precision in the gbuffer
	const bool bHighPrecisionGBuffers = (CurrentGBufferFormat >= EGBufferFormat::Force16BitsPerChannel);

	const EPixelFormat SpecularGBufferFormat = bHighPrecisionGBuffers ? PF_FloatRGBA : PF_B8G8R8A8;

	return SpecularGBufferFormat;
}

EPixelFormat FSceneRenderTargets::GetGBufferCFormat() const
{
	// good to see the quality loss due to precision in the gbuffer
	const bool bHighPrecisionGBuffers = (CurrentGBufferFormat >= EGBufferFormat::Force16BitsPerChannel);

	const EPixelFormat DiffuseGBufferFormat = bHighPrecisionGBuffers ? PF_FloatRGBA : PF_B8G8R8A8;

	return DiffuseGBufferFormat;
}

EPixelFormat FSceneRenderTargets::GetGBufferDFormat() const
{
	return PF_B8G8R8A8;
}

EPixelFormat FSceneRenderTargets::GetGBufferEFormat() const
{
	return PF_B8G8R8A8;
}

EPixelFormat FSceneRenderTargets::GetGBufferFFormat() const
{
	// good to see the quality loss due to precision in the gbuffer
	const bool bHighPrecisionGBuffers = (CurrentGBufferFormat >= EGBufferFormat::Force16BitsPerChannel);
	// good to profile the impact of non 8 bit formats
	const bool bEnforce8BitPerChannel = (CurrentGBufferFormat == EGBufferFormat::Force8BitsPerChannel);

	EPixelFormat NormalGBufferFormat = bHighPrecisionGBuffers ? PF_FloatRGBA : PF_B8G8R8A8;

	if (bEnforce8BitPerChannel)
	{
		NormalGBufferFormat = PF_B8G8R8A8;
	}
	else if (CurrentGBufferFormat == EGBufferFormat::HighPrecisionNormals)
	{
		NormalGBufferFormat = PF_FloatRGBA;
	}

	return NormalGBufferFormat;
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
	const bool bCanReadGBufferUniforms = (bUseGBuffer || IsSimpleForwardShadingEnabled(ShaderPlatform)) && CurrentFeatureLevel >= ERHIFeatureLevel::SM5;
	if (bUseGBuffer)
	{
		// Create the world-space normal g-buffer.
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, GetGBufferAFormat(), FClearValueBinding::Transparent, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
			Desc.Flags |= GFastVRamConfig.GBufferA;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, GBufferA, TEXT("GBufferA"));
		}

		// Create the specular color and power g-buffer.
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, GetGBufferBFormat(), FClearValueBinding::Transparent, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
			Desc.Flags |= GFastVRamConfig.GBufferB;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, GBufferB, TEXT("GBufferB"));
		}

		// Create the diffuse color g-buffer.
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, GetGBufferCFormat(), FClearValueBinding::Transparent, TexCreate_SRGB, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
			Desc.Flags |= GFastVRamConfig.GBufferC;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, GBufferC, TEXT("GBufferC"));
		}

		// Create the mask g-buffer (e.g. SSAO, subsurface scattering, wet surface mask, skylight mask, ...).
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, GetGBufferDFormat(), FClearValueBinding::Transparent, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
			Desc.Flags |= GFastVRamConfig.GBufferD;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, GBufferD, TEXT("GBufferD"));
		}

		if (bAllowStaticLighting)
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, GetGBufferEFormat(), FClearValueBinding::Transparent, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
			Desc.Flags |= GFastVRamConfig.GBufferE;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, GBufferE, TEXT("GBufferE"));
		}

		// Create the world-space tangent g-buffer.
		if (bAllowTangentGBuffer)
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, GetGBufferFFormat(), FClearValueBinding({0.5f, 0.5f, 0.5f, 0.5f}), TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
			Desc.Flags |= GFastVRamConfig.GBufferF;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, GBufferF, TEXT("GBufferF"));
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
		const bool bRequiresStencilColorTarget = (CurrentFeatureLevel <= ERHIFeatureLevel::ES3_1);

		int32 NumColorTargets = 0;
		if (bRequiresStencilColorTarget)
		{
			checkSlow(MobileCustomDepth.IsValid() && MobileCustomStencil.IsValid());

			RPInfo.ColorRenderTargets[0].Action = ERenderTargetActions::Clear_Store;
			RPInfo.ColorRenderTargets[0].ArraySlice = -1;
			RPInfo.ColorRenderTargets[0].MipIndex = 0;
			RPInfo.ColorRenderTargets[0].RenderTarget = MobileCustomDepth->GetRenderTargetItem().ShaderResourceTexture;

			RPInfo.ColorRenderTargets[1].Action = bWritesCustomStencilValues ? ERenderTargetActions::Clear_Store : ERenderTargetActions::DontLoad_DontStore;
			RPInfo.ColorRenderTargets[1].ArraySlice = -1;
			RPInfo.ColorRenderTargets[1].MipIndex = 0;
			RPInfo.ColorRenderTargets[1].RenderTarget = MobileCustomStencil->GetRenderTargetItem().ShaderResourceTexture;

			NumColorTargets = 2;
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
	
	if (CurrentFeatureLevel <= ERHIFeatureLevel::ES3_1)
	{
		RHICmdList.CopyToResolveTarget(MobileCustomDepth->GetRenderTargetItem().TargetableTexture, MobileCustomDepth->GetRenderTargetItem().ShaderResourceTexture, FResolveParams(ResolveRect));
		RHICmdList.CopyToResolveTarget(MobileCustomStencil->GetRenderTargetItem().TargetableTexture, MobileCustomStencil->GetRenderTargetItem().ShaderResourceTexture, FResolveParams(ResolveRect));
	}
	else
	{
		RHICmdList.CopyToResolveTarget(CustomDepth->GetRenderTargetItem().TargetableTexture, CustomDepth->GetRenderTargetItem().ShaderResourceTexture, FResolveParams(ResolveRect));
	}

	bCustomDepthIsValid = true;
}

void FSceneRenderTargets::BeginRenderingPrePass(FRHICommandList& RHICmdList, bool bPerformClear, bool bStencilClear)
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
		ERenderTargetActions StencilAction = bStencilClear ? ERenderTargetActions::Clear_Store : ERenderTargetActions::Load_Store;
		RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(ERenderTargetActions::Clear_Store, StencilAction);
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
		uint32 Flags = TexCreate_RenderTargetable | TexCreate_ShaderResource;

		// Create the SeparateTranslucency render target (alpha is needed to lerping)
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(Size, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_None, Flags, false));
		Desc.Flags |= GFastVRamConfig.SeparateTranslucency;
		Desc.AutoWritable = false;
		Desc.NumSamples = GetNumSceneColorMSAASamples(CurrentFeatureLevel);
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SeparateTranslucencyRT, TEXT("SeparateTranslucency"), false);
	}
	return SeparateTranslucencyRT;
}

TRefCountPtr<IPooledRenderTarget>& FSceneRenderTargets::GetSeparateTranslucencyDummy()
{
	// Make sure the dummy is consistent with the clear color of separate translucnecy render target
	return GSystemTextures.BlackAlphaOneDummy;
}

TRefCountPtr<IPooledRenderTarget>& FSceneRenderTargets::GetSeparateTranslucencyModulate(FRHICommandList& RHICmdList, FIntPoint Size)
{
	if (!SeparateTranslucencyModulateRT || SeparateTranslucencyModulateRT->GetDesc().Extent != Size)
	{
		uint32 Flags = TexCreate_RenderTargetable | TexCreate_ShaderResource;

		// Create the SeparateTranslucency render target (alpha is needed to lerping)
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(Size, PF_FloatR11G11B10, FClearValueBinding::White, TexCreate_None, Flags, false));
		Desc.Flags |= GFastVRamConfig.SeparateTranslucencyModulate;
		Desc.AutoWritable = false;
		Desc.NumSamples = GetNumSceneColorMSAASamples(CurrentFeatureLevel);
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SeparateTranslucencyModulateRT, TEXT("SeparateTranslucencyModulate"), false);
	}
	return SeparateTranslucencyModulateRT;
}

TRefCountPtr<IPooledRenderTarget>& FSceneRenderTargets::GetSeparateTranslucencyModulateDummy()
{
	// Make sure the dummy is consistent with the clear color of separate translucnecy render target
	return GSystemTextures.WhiteDummy;
}

TRefCountPtr<IPooledRenderTarget>& FSceneRenderTargets::GetDownsampledTranslucencyDepth(FRHICommandList& RHICmdList, FIntPoint Size)
{
	if (!DownsampledTranslucencyDepthRT || DownsampledTranslucencyDepthRT->GetDesc().Extent != Size)
	{
		// Create the SeparateTranslucency depth render target 
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(Size, PF_DepthStencil, FClearValueBinding::None, TexCreate_None, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource, false));
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

	RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, RPInfo.ColorRenderTargets[0].RenderTarget);
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

// this code is copy pasted from BeginRenderingSeparateTranslucency, might be a good idea to add some helperfunctions.
void FSceneRenderTargets::BeginRenderingSeparateTranslucencyModulate(FRHICommandList& RHICmdList, const FViewInfo& View, const FSceneRenderer& Renderer, bool bFirstTimeThisFrame)
{
	check(RHICmdList.IsOutsideRenderPass());

	bSeparateTranslucencyPass = true;

	SCOPED_DRAW_EVENT(RHICmdList, BeginSeparateTranslucencyModulate);

	TRefCountPtr<IPooledRenderTarget>* SeparateTranslucencyModulate;
	if (bSnapshot)
	{
		check(SeparateTranslucencyModulateRT.GetReference());
		SeparateTranslucencyModulate = &SeparateTranslucencyModulateRT;
	}
	else
	{
		SeparateTranslucencyModulate = &GetSeparateTranslucencyModulate(RHICmdList, SeparateTranslucencyBufferSize);
	}
	// dpeth is the same as regular separate
	const FTexture2DRHIRef &SeparateTranslucencyDepth = SeparateTranslucencyScale < 1.0f ? (const FTexture2DRHIRef&)GetDownsampledTranslucencyDepth(RHICmdList, SeparateTranslucencyBufferSize)->GetRenderTargetItem().TargetableTexture : GetSceneDepthSurface();

	// start at white and multiply down, as opposed to the separate pass which starts and black and adds up
	check((*SeparateTranslucencyModulate)->GetRenderTargetItem().TargetableTexture->GetClearColor() == FLinearColor::White);
	FRHIRenderPassInfo RPInfo((*SeparateTranslucencyModulate)->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::Load_Store);
	
	// this is the same as regular separate
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

	RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, RPInfo.ColorRenderTargets[0].RenderTarget);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("BeginRenderingSeparateTranslucencyModulate"));

	// same as regular separate
	if (!bFirstTimeThisFrame)
	{
		// Clear the stencil buffer for ResponsiveAA
		RHICmdList.BindClearMRTValues(true, false, true);
	}

	// same as regular separate, SeparateTranslucencyScale is the same for both Separate and SeparateModulate

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


void FSceneRenderTargets::ResolveSeparateTranslucencyModulate(FRHICommandList& RHICmdList, const FViewInfo& View)
{
	SCOPED_DRAW_EVENT(RHICmdList, ResolveSeparateTranslucencyModulate);

	check(RHICmdList.IsOutsideRenderPass());

	TRefCountPtr<IPooledRenderTarget>* SeparateTranslucencyModulate;
	TRefCountPtr<IPooledRenderTarget>* SeparateTranslucencyDepth; // same depth as regular
	if (bSnapshot)
	{
		check(SeparateTranslucencyRT.GetReference());
		SeparateTranslucencyModulate = &SeparateTranslucencyModulateRT;
		SeparateTranslucencyDepth = SeparateTranslucencyScale < 1.f ? &DownsampledTranslucencyDepthRT : &SceneDepthZ;
	}
	else
	{
		SeparateTranslucencyModulate = &GetSeparateTranslucencyModulate(RHICmdList, SeparateTranslucencyBufferSize);
		SeparateTranslucencyDepth = SeparateTranslucencyScale < 1.f ? &GetDownsampledTranslucencyDepth(RHICmdList, SeparateTranslucencyBufferSize) : &SceneDepthZ;
	}

	const FResolveRect SeparateResolveRect(
		View.ViewRect.Min.X * SeparateTranslucencyScale, 
		View.ViewRect.Min.Y * SeparateTranslucencyScale, 
		View.ViewRect.Max.X * SeparateTranslucencyScale, 
		View.ViewRect.Max.Y * SeparateTranslucencyScale
		);

	RHICmdList.CopyToResolveTarget((*SeparateTranslucencyModulate)->GetRenderTargetItem().TargetableTexture, (*SeparateTranslucencyModulate)->GetRenderTargetItem().ShaderResourceTexture, SeparateResolveRect);
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
			ResolvePixelShader = ResolvePixelShader2X.GetPixelShader();
			break;
		case 4:
			TextureIndex = ResolvePixelShader4X->UnresolvedSurface.GetBaseIndex();
			ResolvePixelShader = ResolvePixelShader4X.GetPixelShader();
			break;
		case 8:
			TextureIndex = ResolvePixelShader8X->UnresolvedSurface.GetBaseIndex();
			ResolvePixelShader = ResolvePixelShader8X.GetPixelShader();
			break;
		default:
			ensureMsgf(false, TEXT("Unsupported depth resolve for samples: %i.  Dynamic loop method isn't supported on all platforms.  Please add specific case."), SourceTexture->GetNumSamples());
			TextureIndex = ResolvePixelShaderAny->UnresolvedSurface.GetBaseIndex();
			ResolvePixelShader = ResolvePixelShaderAny.GetPixelShader();
			break;
		}

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GScreenVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = ResolveVertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = ResolvePixelShader;
		GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
		RHICmdList.SetBlendFactor(FLinearColor::White);

		// Set the source texture.	
		if (SourceTexture)
		{
			RHICmdList.SetShaderTexture(ResolvePixelShader, TextureIndex, SourceTexture);
		}

		ResolveVertexShader->SetParameters(RHICmdList, SourceRect, DestRect, TargetWidth, TargetHeight);

		RHICmdList.DrawPrimitive(0, 2, 1);
	}
	RHICmdList.EndRenderPass();

	// Transition the destination depth texture to readable. Ignore stencil.
	RHICmdList.TransitionResource(FExclusiveDepthStencil::DepthRead_StencilNop, DestTexture);
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

	Desc.bForceSharedTargetAndShaderResource = true;

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

	Desc.bForceSharedTargetAndShaderResource = true;

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

void FSceneRenderTargets::AllocateMobileRenderTargets(FRHICommandListImmediate& RHICmdList)
{
	// on ES2 we don't do on demand allocation of SceneColor yet (in non ES2 it's released in the Tonemapper Process())
	AllocSceneColor(RHICmdList);
	AllocateCommonDepthTargets(RHICmdList);
	AllocateFoveationTexture(RHICmdList);

	static const auto MobileMultiViewCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("vr.MobileMultiView"));

	const bool bIsUsingMobileMultiView = (GSupportsMobileMultiView || GRHISupportsArrayIndexFromAnyShader) && (MobileMultiViewCVar && MobileMultiViewCVar->GetValueOnAnyThread() != 0);

	if (bIsUsingMobileMultiView)
	{
		// If the plugin uses separate render targets it is required to support mobile multi-view direct
		IStereoRenderTargetManager* const StereoRenderTargetManager = GEngine->StereoRenderingDevice.IsValid() ? GEngine->StereoRenderingDevice->GetRenderTargetManager() : nullptr;
		const bool bIsMobileMultiViewDirectEnabled = StereoRenderTargetManager && StereoRenderTargetManager->ShouldUseSeparateRenderTarget();

		const int32 ScaleFactor = (bIsMobileMultiViewDirectEnabled) ? 1 : 2;

		AllocMobileMultiViewSceneColor(RHICmdList, ScaleFactor);
		AllocMobileMultiViewDepth(RHICmdList, ScaleFactor);
	}

	AllocateDebugViewModeTargets(RHICmdList);

	EPixelFormat Format = GetSceneColor()->GetDesc().Format;

	SceneAlphaCopy = GSystemTextures.MaxFP16Depth;
	
	if (UseVirtualTexturing(CurrentFeatureLevel))
	{
		FIntPoint FeedbackSize = FIntPoint::DivideAndRoundUp(BufferSize, FMath::Max(GVirtualTextureFeedbackFactor, 1));
		VirtualTextureFeedback.CreateResourceGPU(RHICmdList, FeedbackSize);
	}
}

// This is a helper class. It generates and provides N names with
// sequentially incremented postfix starting from 0.
// Example: SomeName0, SomeName1, ..., SomeName117
class FIncrementalNamesHolder
{
public:
	FIncrementalNamesHolder(const TCHAR* const Name, uint32 Size)
	{
		check(Size > 0);
		Names.Empty(Size);
		for (uint32 i = 0; i < Size; ++i)
		{
			Names.Add(FString::Printf(TEXT("%s%d"), Name, i));
		}
	}

	const TCHAR* const operator[] (uint32 Idx) const
	{
		CA_SUPPRESS(6385);	// Doesn't like COM
		return *Names[Idx];
	}

private:
	TArray<FString> Names;
};

// for easier use of "VisualizeTexture"
static const TCHAR* const GetVolumeName(uint32 Id, bool bDirectional)
{
	constexpr uint32 MaxNames = 128;
	static FIncrementalNamesHolder Names(TEXT("TranslucentVolume"), MaxNames);
	static FIncrementalNamesHolder NamesDir(TEXT("TranslucentVolumeDir"), MaxNames);

	check(Id < MaxNames);

	CA_SUPPRESS(6385);	// Doesn't like COM
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
		bHMDAllocatedDepthTarget = StereoRenderTargetManager && StereoRenderTargetManager->AllocateDepthTexture(0, BufferSize.X, BufferSize.Y, PF_DepthStencil, 1, TexCreate_None, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource | TexCreate_InputAttachmentRead, DepthTex, SRTex, GetNumSceneColorMSAASamples(CurrentFeatureLevel));

		// Allow UAV depth?
		const uint32 DepthUAVFlag = GRHISupportsDepthUAV ? TexCreate_UAV : 0;

		// Create a texture to store the resolved scene depth, and a render-targetable surface to hold the unresolved scene depth.
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_DepthStencil, DefaultDepthClear, TexCreate_None, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource | TexCreate_InputAttachmentRead | DepthUAVFlag, false));
		Desc.NumSamples = GetNumSceneColorMSAASamples(CurrentFeatureLevel);
		Desc.Flags |= GFastVRamConfig.SceneDepth;

		if (!bKeepDepthContent)
		{
			Desc.TargetableFlags |= TexCreate_Memoryless;
		}

		// Only defer texture allocation if we're an HMD-allocated target, and we're not MSAA.
		const bool bDeferTextureAllocation = bHMDAllocatedDepthTarget && Desc.NumSamples == 1;
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SceneDepthZ, TEXT("SceneDepthZ"), true, ERenderTargetTransience::Transient, bDeferTextureAllocation);

		if (SceneDepthZ && bHMDAllocatedDepthTarget)
		{
			const uint32 OldElementSize = SceneDepthZ->ComputeMemorySize();
		
			{
				FSceneRenderTargetItem& Item = SceneDepthZ->GetRenderTargetItem();

				// If SRT and texture are different (MSAA), only modify the resolve render target, to avoid creating a swapchain of MSAA textures
				if (Item.ShaderResourceTexture == Item.TargetableTexture)
				{
					Item.TargetableTexture = SRTex;
				}

				Item.ShaderResourceTexture = SRTex;
				Item.SRVs.Empty();
				Item.HTileSRV = nullptr;
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
}

void FSceneRenderTargets::AllocateFoveationTexture(FRHICommandList& RHICmdList)
{
	const bool bStereo = GEngine->StereoRenderingDevice.IsValid() && GEngine->StereoRenderingDevice->IsStereoEnabled();
	IStereoRenderTargetManager* const StereoRenderTargetManager = bStereo ? GEngine->StereoRenderingDevice->GetRenderTargetManager() : nullptr;

	FTexture2DRHIRef Texture;
	FIntPoint TextureSize;

	// Allocate variable resolution texture for VR foveation if supported
	if (StereoRenderTargetManager && StereoRenderTargetManager->NeedReAllocateFoveationTexture(FoveationTexture))
	{
		FoveationTexture.SafeRelease();
	}
	bAllocatedFoveationTexture = StereoRenderTargetManager && StereoRenderTargetManager->AllocateFoveationTexture(0, BufferSize.X, BufferSize.Y, PF_R8G8, 0, TexCreate_None, TexCreate_None, Texture, TextureSize);
	if (bAllocatedFoveationTexture)
	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(TextureSize, PF_R8G8, FClearValueBinding::White, TexCreate_None, TexCreate_None, false));
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, FoveationTexture, TEXT("FixedFoveation"));
		const uint32 OldElementSize = FoveationTexture->ComputeMemorySize();
		FoveationTexture->GetRenderTargetItem().ShaderResourceTexture = FoveationTexture->GetRenderTargetItem().TargetableTexture = Texture;
		GRenderTargetPool.UpdateElementSize(FoveationTexture, OldElementSize);
	}
}

void FSceneRenderTargets::AllocateScreenShadowMask(FRHICommandList& RHICmdList, TRefCountPtr<IPooledRenderTarget>& ScreenShadowMaskTexture, bool bCreateShaderResourceView)
{
	ETextureCreateFlags TargetableFlags = ETextureCreateFlags(TexCreate_RenderTargetable | TexCreate_ShaderResource | (bCreateShaderResourceView ? TexCreate_ShaderResource : TexCreate_None));
	FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(GetBufferSizeXY(), PF_B8G8R8A8, FClearValueBinding::White, TexCreate_None, TargetableFlags, false));
	Desc.Flags |= GFastVRamConfig.ScreenSpaceShadowMask;
	Desc.NumSamples = GetNumSceneColorMSAASamples(GetCurrentFeatureLevel());
	GRenderTargetPool.FindFreeElement(RHICmdList, Desc, ScreenShadowMaskTexture, TEXT("ScreenShadowMaskTexture"));
}

void FSceneRenderTargets::AllocateScreenTransmittanceMask(FRHICommandList& RHICmdList, TRefCountPtr<IPooledRenderTarget>& ScreenTransmittanceMaskTexture)
{
	FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(GetBufferSizeXY(), PF_FloatRGBA, FClearValueBinding::White, TexCreate_None, TexCreate_RenderTargetable | TexCreate_UAV, false));
	Desc.NumSamples = GetNumSceneColorMSAASamples(GetCurrentFeatureLevel());
	GRenderTargetPool.FindFreeElement(RHICmdList, Desc, ScreenTransmittanceMaskTexture, TEXT("ScreenTransmittanceMaskTexture"));
}

const FTexture2DRHIRef& FSceneRenderTargets::GetOptionalShadowDepthColorSurface(FRHICommandList& RHICmdList, int32 Width, int32 Height) const
{
	// Look for matching resolution
	int32 EmptySlot = -1;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(OptionalShadowDepthColor); Index++)
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
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_R16_UINT, FClearValueBinding::None, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, LightingChannels, TEXT("LightingChannels"), true, ERenderTargetTransience::NonTransient);
	}
}

void FSceneRenderTargets::AllocateDeferredShadingPathRenderTargets(FRHICommandListImmediate& RHICmdList, const int32 NumViews)
{
	AllocateCommonDepthTargets(RHICmdList);

	// Create a quarter-sized version of the scene depth.
	{
		FIntPoint SmallDepthZSize(FMath::Max<uint32>(BufferSize.X / SmallColorDepthDownsampleFactor, 1), FMath::Max<uint32>(BufferSize.Y / SmallColorDepthDownsampleFactor, 1));

		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(SmallDepthZSize, PF_DepthStencil, FClearValueBinding::None, TexCreate_None, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource, true));
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SmallDepthZ, TEXT("SmallDepthZ"), true, ERenderTargetTransience::NonTransient);
	}

	// Create the required render targets if running Highend.
	if (CurrentFeatureLevel >= ERHIFeatureLevel::SM5)
	{
		// Create the screen space ambient occlusion buffer
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_G8, FClearValueBinding::White, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
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
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_R8G8, FClearValueBinding::Transparent, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, DirectionalOcclusion, TEXT("DirectionalOcclusion"));
	}

	if (CurrentFeatureLevel >= ERHIFeatureLevel::SM5) 
	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
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
	EPixelFormat DefaultColorFormat = (!IsMobileHDR() || !GSupportsRenderTargetFormat_PF_FloatRGBA) ? PF_B8G8R8A8 : PF_FloatRGBA;
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
	TOneColorPixelShaderMRT::FPermutationDomain PermutationVector;
	PermutationVector.Set<TOneColorPixelShaderMRT::TOneColorPixelShaderNumOutputs>(NumRenderTargets);
	TShaderMapRef<TOneColorPixelShaderMRT>PixelShader(ShaderMap, PermutationVector);

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GScreenVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
	GraphicsPSOInit.BoundShaderState.GeometryShaderRHI = GeometryShader.GetGeometryShader();
#endif
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
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

	if (InFeatureLevel < ERHIFeatureLevel::SM5)
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
	if (BufferSize.X > 0 && BufferSize.Y > 0 && IsAllocateRenderTargetsRequired())
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
	else if ((EShadingPath)CurrentShadingPath == EShadingPath::Mobile && SceneDepthZ)
	{
		// If the render targets are already allocated, but the keep depth content flag has changed,
		// we need to reallocate the depth buffer.
		uint32 DepthBufferFlags = SceneDepthZ->GetRenderTargetItem().TargetableTexture->GetFlags();
		bool bCurrentKeepDepthContent = (DepthBufferFlags & TexCreate_Memoryless) == 0;
		if (bCurrentKeepDepthContent != bKeepDepthContent)
		{
			SceneDepthZ.SafeRelease();
			// Make sure the old depth buffer is freed by flushing the target pool.
			GRenderTargetPool.FreeUnusedResources();
			AllocateCommonDepthTargets(RHICmdList);
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
	SmallDepthZ.SafeRelease();
	DBufferA.SafeRelease();
	DBufferB.SafeRelease();
	DBufferC.SafeRelease();
	ScreenSpaceAO.SafeRelease();
	ScreenSpaceGTAOHorizons.SafeRelease();
	QuadOverdrawBuffer.SafeRelease();
	LightAttenuation.SafeRelease();
	LightAccumulation.SafeRelease();
	DirectionalOcclusion.SafeRelease();
	CustomDepth.SafeRelease();
	MobileCustomDepth.SafeRelease();
	MobileCustomStencil.SafeRelease();
	CustomStencilSRV.SafeRelease();

	for (int32 i = 0; i < UE_ARRAY_COUNT(OptionalShadowDepthColor); i++)
	{
		OptionalShadowDepthColor[i].SafeRelease();
	}

	for (int32 i = 0; i < UE_ARRAY_COUNT(ReflectionColorScratchCubemap); i++)
	{
		ReflectionColorScratchCubemap[i].SafeRelease();
	}

	for (int32 i = 0; i < UE_ARRAY_COUNT(DiffuseIrradianceScratchCubemap); i++)
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

	FoveationTexture.SafeRelease();

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

IPooledRenderTarget* FSceneRenderTargets::RequestCustomDepth(FRHICommandListImmediate& RHICmdList, bool bPrimitives)
{
	int Value = CVarCustomDepth.GetValueOnRenderThread();
	const bool bCustomDepthPassWritingStencil = IsCustomDepthPassWritingStencil();
	const bool bMobilePath = (CurrentFeatureLevel <= ERHIFeatureLevel::ES3_1);
	int32 DownsampleFactor = bMobilePath && CVarMobileCustomDepthDownSample.GetValueOnRenderThread() > 0 ? 2 : 1;

	FIntPoint CustomDepthBufferSize = FIntPoint::DivideAndRoundUp(BufferSize, DownsampleFactor);

	if ((Value == 1 && bPrimitives) || Value == 2 || bCustomDepthPassWritingStencil)
	{
		bool bHasValidCustomDepth = (CustomDepth.IsValid() && CustomDepthBufferSize == CustomDepth->GetDesc().Extent && !GFastVRamConfig.bDirty);
		bool bHasValidCustomStencil;
		if (bMobilePath)
		{
			bHasValidCustomStencil = (MobileCustomStencil.IsValid() && CustomDepthBufferSize == MobileCustomStencil->GetDesc().Extent) && 
									//Use memory less when stencil writing is disabled and vice versa
									bCustomDepthPassWritingStencil == ((MobileCustomStencil->GetDesc().TargetableFlags & TexCreate_Memoryless) == 0);
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
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(CustomDepthBufferSize, PF_DepthStencil, FClearValueBinding::DepthFar, CustomDepthFlags, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource, false));
			Desc.Flags |= GFastVRamConfig.CustomDepth;
			
			if (bMobilePath)
			{
				Desc.TargetableFlags |= TexCreate_Memoryless;
			}

			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, CustomDepth, TEXT("CustomDepth"), true, ERenderTargetTransience::NonTransient);
			
			if (bMobilePath)
			{
				uint32 CustomDepthStencilTargetableFlags = TexCreate_RenderTargetable | TexCreate_ShaderResource;

				FPooledRenderTargetDesc MobileCustomDepthDesc(FPooledRenderTargetDesc::Create2DDesc(CustomDepthBufferSize, PF_R16F, FClearValueBinding(FLinearColor(65500.0f, 65500.0f, 65500.0f)), TexCreate_None, CustomDepthStencilTargetableFlags, false));
				GRenderTargetPool.FindFreeElement(RHICmdList, MobileCustomDepthDesc, MobileCustomDepth, TEXT("MobileCustomDepth"));

				if (!bCustomDepthPassWritingStencil)
				{
					CustomDepthStencilTargetableFlags |= TexCreate_Memoryless;
				}

				FPooledRenderTargetDesc MobileCustomStencilDesc(FPooledRenderTargetDesc::Create2DDesc(CustomDepthBufferSize, PF_G8, FClearValueBinding::Transparent, TexCreate_None, CustomDepthStencilTargetableFlags, false));
				GRenderTargetPool.FindFreeElement(RHICmdList, MobileCustomStencilDesc, MobileCustomStencil, TEXT("MobileCustomStencil"));

				CustomStencilSRV.SafeRelease();
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

			// For mobile multi-view + mono support
			const bool bMobileMultiViewColorValid = (!MobileMultiViewSceneColor || MobileMultiViewSceneColor->GetRenderTargetItem().TargetableTexture->GetClearBinding() == DefaultColorClear);
			const bool bMobileMultiViewDepthValid = (!MobileMultiViewSceneDepthZ || MobileMultiViewSceneDepthZ->GetRenderTargetItem().TargetableTexture->GetClearBinding() == DefaultDepthClear);
			return bColorValid && bDepthValid && bMobileMultiViewColorValid && bMobileMultiViewDepthValid;
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

bool FSceneRenderTargets::IsAllocateRenderTargetsRequired() const
{
	bool bAllocateRequired = false;

	const bool bStereo = GEngine->StereoRenderingDevice.IsValid() && GEngine->StereoRenderingDevice->IsStereoEnabled();
	IStereoRenderTargetManager* const StereoRenderTargetManager = bStereo ? GEngine->StereoRenderingDevice->GetRenderTargetManager() : nullptr;

	// HMD controlled foveation textures may be destroyed externally and need a new allocation
	if (StereoRenderTargetManager && StereoRenderTargetManager->NeedReAllocateFoveationTexture(FoveationTexture))
	{
		bAllocateRequired = true;
	}
	
	return bAllocateRequired || !AreShadingPathRenderTargetsAllocated(GetSceneColorFormatType()) || !AreRenderTargetClearsValid(GetSceneColorFormatType());
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

		const FTexture2DRHIRef& SceneDepthTexture = SceneContext.GetSceneDepthTexture();
		SceneTextureParameters.SceneDepthTexture = bSetupDepth && SceneDepthTexture.IsValid() ? SceneDepthTexture.GetReference() : DepthDefault;

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
			SceneTextureParameters.SceneDepthTextureNonMS = SceneContext.GetSceneDepthTexture();
		}
		else
		{
			SceneTextureParameters.SceneDepthTextureNonMS = DepthDefault;
		}

		SceneTextureParameters.SceneDepthTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		SceneTextureParameters.SceneStencilTexture = bSetupDepth && SceneContext.SceneStencilSRV ? SceneContext.SceneStencilSRV : GSystemTextures.StencilDummySRV;
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
		const FSceneRenderTargetItem& GBufferFToUse = bCanReadGBufferUniforms && SceneContext.GBufferF ? SceneContext.GBufferF->GetRenderTargetItem() : GSystemTextures.BlackDummy->GetRenderTargetItem();
		const FSceneRenderTargetItem& GBufferVelocityToUse = bCanReadGBufferUniforms && SceneContext.SceneVelocity ? SceneContext.SceneVelocity->GetRenderTargetItem() : GSystemTextures.BlackDummy->GetRenderTargetItem();

		SceneTextureParameters.GBufferATexture = GBufferAToUse.ShaderResourceTexture;
		SceneTextureParameters.GBufferBTexture = GBufferBToUse.ShaderResourceTexture;
		SceneTextureParameters.GBufferCTexture = GBufferCToUse.ShaderResourceTexture;
		SceneTextureParameters.GBufferDTexture = GBufferDToUse.ShaderResourceTexture;
		SceneTextureParameters.GBufferETexture = GBufferEToUse.ShaderResourceTexture;
		SceneTextureParameters.GBufferFTexture = GBufferFToUse.ShaderResourceTexture;
		SceneTextureParameters.GBufferVelocityTexture = GBufferVelocityToUse.ShaderResourceTexture;
		
		SceneTextureParameters.GBufferATextureNonMS = GBufferAToUse.ShaderResourceTexture;
		SceneTextureParameters.GBufferBTextureNonMS = GBufferBToUse.ShaderResourceTexture;
		SceneTextureParameters.GBufferCTextureNonMS = GBufferCToUse.ShaderResourceTexture;
		SceneTextureParameters.GBufferDTextureNonMS = GBufferDToUse.ShaderResourceTexture;
		SceneTextureParameters.GBufferETextureNonMS = GBufferEToUse.ShaderResourceTexture;
		SceneTextureParameters.GBufferFTextureNonMS = GBufferFToUse.ShaderResourceTexture;
		SceneTextureParameters.GBufferVelocityTextureNonMS = GBufferVelocityToUse.ShaderResourceTexture;

		SceneTextureParameters.GBufferATextureSampler = TStaticSamplerState<>::GetRHI();
		SceneTextureParameters.GBufferBTextureSampler = TStaticSamplerState<>::GetRHI();
		SceneTextureParameters.GBufferCTextureSampler = TStaticSamplerState<>::GetRHI();
		SceneTextureParameters.GBufferDTextureSampler = TStaticSamplerState<>::GetRHI();
		SceneTextureParameters.GBufferETextureSampler = TStaticSamplerState<>::GetRHI();
		SceneTextureParameters.GBufferFTextureSampler = TStaticSamplerState<>::GetRHI();
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
		FRHIShaderResourceView* CustomStencilSRV = GSystemTextures.StencilDummySRV;

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
	template RENDERER_API TUniformBufferRef<FSceneTexturesUniformParameters> CreateSceneTextureUniformBufferSingleDraw< TRHICmdList >(\
		TRHICmdList& RHICmdList,						\
		ESceneTextureSetupMode SceneTextureSetupMode,	\
		ERHIFeatureLevel::Type FeatureLevel				\
	)

IMPLEMENT_CreateSceneTextureUniformBuffer( FRHICommandList );
IMPLEMENT_CreateSceneTextureUniformBuffer( FRHICommandListImmediate );
IMPLEMENT_CreateSceneTextureUniformBuffer( FRHIAsyncComputeCommandListImmediate );

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FMobileSceneTextureUniformParameters, "MobileSceneTextures");

void SetupMobileSceneTextureUniformParameters(
	FSceneRenderTargets& SceneContext,
	ERHIFeatureLevel::Type FeatureLevel,
	bool bSceneTexturesValid,
	bool bCustomDepthIsValid,
	FMobileSceneTextureUniformParameters& SceneTextureParameters)
{
	FRHITexture* BlackDefault2D = GSystemTextures.BlackDummy->GetRenderTargetItem().ShaderResourceTexture;
	FRHITexture* MaxFP16Depth2D = GSystemTextures.MaxFP16Depth->GetRenderTargetItem().ShaderResourceTexture;
	FRHITexture* DepthDefault = GSystemTextures.DepthDummy->GetRenderTargetItem().ShaderResourceTexture;

	SceneTextureParameters.SceneColorTexture = bSceneTexturesValid ? SceneContext.GetSceneColorTexture().GetReference() : BlackDefault2D;
	SceneTextureParameters.SceneColorTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	FRHITexture* SceneDepth = DepthDefault;
	const FTexture2DRHIRef& SceneDepthTextureRef = SceneContext.GetSceneDepthTexture();
	if (bSceneTexturesValid && SceneDepthTextureRef && (SceneDepthTextureRef->GetFlags() & TexCreate_Memoryless) == 0)
	{
		SceneDepth = SceneDepthTextureRef.GetReference();
	}
	SceneTextureParameters.SceneDepthTexture = SceneDepth;
	SceneTextureParameters.SceneDepthTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	SceneTextureParameters.SceneAlphaCopyTexture = bSceneTexturesValid && SceneContext.HasSceneAlphaCopyTexture() ? SceneContext.GetSceneAlphaCopyTexture() : BlackDefault2D;
	SceneTextureParameters.SceneAlphaCopyTextureSampler = TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI();

	FRHITexture* CustomDepth = MaxFP16Depth2D;

	// if there is no custom depth it's better to have the far distance there
	// we should update all pass uniform buffers at the start of frame on mobile, SceneContext.bCustomDepthIsValid is invalid at InitView, so pass the parameter from View.bUsesCustomDepthStencil
	if (bCustomDepthIsValid && SceneContext.MobileCustomDepth.IsValid())
	{
		CustomDepth = SceneContext.MobileCustomDepth->GetRenderTargetItem().ShaderResourceTexture;
	}

	SceneTextureParameters.CustomDepthTexture = CustomDepth;
	SceneTextureParameters.CustomDepthTextureSampler = TStaticSamplerState<>::GetRHI();

	FRHITexture* MobileCustomStencil = BlackDefault2D;

	if (bCustomDepthIsValid && SceneContext.MobileCustomStencil.IsValid())
	{
		MobileCustomStencil = SceneContext.MobileCustomStencil->GetRenderTargetItem().ShaderResourceTexture;
	}

	SceneTextureParameters.MobileCustomStencilTexture = MobileCustomStencil;
	SceneTextureParameters.MobileCustomStencilTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	
	if (SceneContext.VirtualTextureFeedback.FeedbackBufferUAV.IsValid())
	{
		SceneTextureParameters.VirtualTextureFeedbackUAV = SceneContext.VirtualTextureFeedback.FeedbackBufferUAV;
	}
	else
	{
		SceneTextureParameters.VirtualTextureFeedbackUAV = GVirtualTextureFeedbackDummyResource.UAV;
	}

	SceneTextureParameters.EyeAdaptationBuffer = GWhiteVertexBufferWithSRV->ShaderResourceViewRHI;
}

template< typename TRHICmdList >
TUniformBufferRef<FMobileSceneTextureUniformParameters> CreateMobileSceneTextureUniformBufferSingleDraw(TRHICmdList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel) 
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	FMobileSceneTextureUniformParameters SceneTextureParameters;
	SetupMobileSceneTextureUniformParameters(SceneContext, FeatureLevel, true, SceneContext.bCustomDepthIsValid, SceneTextureParameters);
	return TUniformBufferRef<FMobileSceneTextureUniformParameters>::CreateUniformBufferImmediate(SceneTextureParameters, UniformBuffer_SingleDraw);
}

#define IMPLEMENT_CreateMobileSceneTextureUniformBuffer( TRHICmdList ) \
	template RENDERER_API TUniformBufferRef<FMobileSceneTextureUniformParameters> CreateMobileSceneTextureUniformBufferSingleDraw< TRHICmdList >(\
		TRHICmdList& RHICmdList,						\
		ERHIFeatureLevel::Type FeatureLevel				\
	);

IMPLEMENT_CreateMobileSceneTextureUniformBuffer( FRHICommandList );
IMPLEMENT_CreateMobileSceneTextureUniformBuffer( FRHICommandListImmediate );
IMPLEMENT_CreateMobileSceneTextureUniformBuffer( FRHIAsyncComputeCommandListImmediate );

void BindSceneTextureUniformBufferDependentOnShadingPath(
	const FShader::CompiledShaderInitializerType& Initializer, 
	FShaderUniformBufferParameter& SceneTexturesUniformBuffer)
{
	const ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel((EShaderPlatform)Initializer.Target.Platform);
		
	if (FSceneInterface::GetShadingPath(FeatureLevel) == EShadingPath::Deferred)
	{
		SceneTexturesUniformBuffer.Bind(Initializer.ParameterMap, FSceneTexturesUniformParameters::StaticStructMetadata.GetShaderVariableName());
		checkfSlow(!Initializer.ParameterMap.ContainsParameterAllocation(FMobileSceneTextureUniformParameters::StaticStructMetadata.GetShaderVariableName()), TEXT("Shader for Deferred shading path tried to bind FMobileSceneTextureUniformParameters which is only available in the mobile shading path: %s"), Initializer.Type->GetName());
	}
	else if (FSceneInterface::GetShadingPath(FeatureLevel) == EShadingPath::Mobile)
	{
		SceneTexturesUniformBuffer.Bind(Initializer.ParameterMap, FMobileSceneTextureUniformParameters::StaticStructMetadata.GetShaderVariableName());
		checkfSlow(!Initializer.ParameterMap.ContainsParameterAllocation(FSceneTexturesUniformParameters::StaticStructMetadata.GetShaderVariableName()), TEXT("Shader for Mobile shading path tried to bind FSceneTexturesUniformParameters which is only available in the deferred shading path: %s"), Initializer.Type->GetName());
	}
}
