// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RendererPrivate.h: Renderer interface private definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"

class FCanvas;
class FHitProxyId;
class FLightCacheInterface;
class FMaterial;
class FPrimitiveSceneInfo;
class FSceneInterface;
class FSceneRenderTargets;
class FSceneView;
class FSceneViewFamily;
class FSceneViewStateInterface;
struct FMeshBatch;
struct FSynthBenchmarkResults;

template<class ResourceType> class TGlobalResource;

DECLARE_LOG_CATEGORY_EXTERN(LogRenderer, Log, All);

/** The renderer module implementation. */
class FRendererModule : public IRendererModule
{
public:
	FRendererModule();
	virtual bool SupportsDynamicReloading() override { return true; }

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	virtual void BeginRenderingViewFamily(FCanvas* Canvas,FSceneViewFamily* ViewFamily) override;
	virtual void CreateAndInitSingleView(FRHICommandListImmediate& RHICmdList, class FSceneViewFamily* ViewFamily, const struct FSceneViewInitOptions* ViewInitOptions) override;
	virtual FSceneInterface* AllocateScene(UWorld* World, bool bInRequiresHitProxies, bool bCreateFXSystem, ERHIFeatureLevel::Type InFeatureLevel) override;
	virtual void RemoveScene(FSceneInterface* Scene) override;
	virtual void UpdateStaticDrawLists() override;
	virtual void UpdateStaticDrawListsForMaterials(const TArray<const FMaterial*>& Materials) override;
	virtual FSceneViewStateInterface* AllocateViewState() override;
	virtual uint32 GetNumDynamicLightsAffectingPrimitive(const FPrimitiveSceneInfo* PrimitiveSceneInfo,const FLightCacheInterface* LCI) override;
	virtual void ReallocateSceneRenderTargets() override;
	virtual void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources, bool bWorldChanged) override;
	virtual void SceneRenderTargetsSetBufferSize(uint32 SizeX, uint32 SizeY) override;
	virtual void InitializeSystemTextures(FRHICommandListImmediate& RHICmdList);
	virtual void DrawTileMesh(FRHICommandListImmediate& RHICmdList, FMeshPassProcessorRenderState& DrawRenderState, const FSceneView& View, FMeshBatch& Mesh, bool bIsHitTesting, const FHitProxyId& HitProxyId, bool bUse128bitRT = false) override;
	virtual void DebugLogOnCrash() override;
	virtual void GPUBenchmark(FSynthBenchmarkResults& InOut, float WorkScale) override;
	virtual void ExecVisualizeTextureCmd(const FString& Cmd) override;
	virtual void UpdateMapNeedsLightingFullyRebuiltState(UWorld* World) override;
	virtual void DrawRectangle(
		FRHICommandList& RHICmdList,
		float X,
		float Y,
		float SizeX,
		float SizeY,
		float U,
		float V,
		float SizeU,
		float SizeV,
		FIntPoint TargetSize,
		FIntPoint TextureSize,
		const TShaderRef<FShader>& VertexShader,
		EDrawRectangleFlags Flags = EDRF_Default
		) override;

	virtual const TSet<FSceneInterface*>& GetAllocatedScenes() override
	{
		return AllocatedScenes;
	}

	virtual void RegisterCustomCullingImpl(ICustomCulling* impl) override;
	virtual void UnregisterCustomCullingImpl(ICustomCulling* impl) override;

	virtual void RegisterPostOpaqueRenderDelegate(const FPostOpaqueRenderDelegate& PostOpaqueRenderDelegate) override;
	virtual void RegisterOverlayRenderDelegate(const FPostOpaqueRenderDelegate& OverlayRenderDelegate) override;
	virtual void RenderPostOpaqueExtensions(const FViewInfo& View, FRHICommandListImmediate& RHICmdList, FSceneRenderTargets& SceneContext, TUniformBufferRef<FSceneTexturesUniformParameters>& SceneTextureUniformParams) override;
	virtual void RenderOverlayExtensions(const FViewInfo& View, FRHICommandListImmediate& RHICmdList, FSceneRenderTargets& SceneContext) override;

	virtual bool HasPostOpaqueExtentions() const override
	{
		return PostOpaqueRenderDelegate.IsBound();
	}

	virtual FOnResolvedSceneColor& GetResolvedSceneColorCallbacks() override
	{
		return PostResolvedSceneColorCallbacks;
	}

	virtual void RenderPostResolvedSceneColorExtension(FRHICommandListImmediate& RHICmdList, class FSceneRenderTargets& SceneContext) override;

	virtual void PostRenderAllViewports() override;

	virtual void PerFrameCleanupIfSkipRenderer() override;

	virtual IAllocatedVirtualTexture* AllocateVirtualTexture(const FAllocatedVTDescription& Desc) override;
	virtual void DestroyVirtualTexture(IAllocatedVirtualTexture* AllocatedVT) override;
	virtual FVirtualTextureProducerHandle RegisterVirtualTextureProducer(const FVTProducerDescription& Desc, IVirtualTexture* Producer) override;
	virtual void ReleaseVirtualTextureProducer(const FVirtualTextureProducerHandle& Handle) override;
	virtual void AddVirtualTextureProducerDestroyedCallback(const FVirtualTextureProducerHandle& Handle, FVTProducerDestroyedFunction* Function, void* Baton) override;
	virtual uint32 RemoveAllVirtualTextureProducerDestroyedCallbacks(const void* Baton) override;
	virtual void RequestVirtualTextureTiles(const FVector2D& InScreenSpaceSize, int32 InMipLevel) override;
	virtual void RequestVirtualTextureTilesForRegion(IAllocatedVirtualTexture* AllocatedVT, const FVector2D& InScreenSpaceSize, const FIntRect& InTextureRegion, int32 InMipLevel) override;
	virtual void LoadPendingVirtualTextureTiles(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel) override;
	virtual void FlushVirtualTextureCache() override;
	virtual void RegisterPersistentViewUniformBufferExtension(IPersistentViewUniformBufferExtension* Extension) override;
private:
	TSet<FSceneInterface*> AllocatedScenes;
	FPostOpaqueRenderDelegate PostOpaqueRenderDelegate;
	FPostOpaqueRenderDelegate OverlayRenderDelegate;
	FOnResolvedSceneColor PostResolvedSceneColorCallbacks;
};

extern ICustomCulling* GCustomCullingImpl;
