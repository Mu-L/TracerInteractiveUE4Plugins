// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
D3D12CommandContext.h: D3D12 Command Context Interfaces
=============================================================================*/

#pragma once
#define AFR_ENGINE_CHANGES_PRESENT 0

// TODO: Because the upper engine is yet to implement these interfaces we can't 'override' something that doesn't exist.
//       Remove when upper engine is ready
#if AFR_ENGINE_CHANGES_PRESENT
#define AFR_API_OVERRIDE override
#else
#define AFR_API_OVERRIDE
#endif

#include "D3D12RHIPrivate.h"
#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include <delayimp.h>

#if USE_PIX
	#include "pix3.h"
#endif
#include "Windows/HideWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_END

// Base class used to define commands that are not device specific, or that broadcast to all devices.
class FD3D12CommandContextBase : public IRHICommandContext, public FD3D12AdapterChild
{
public:

	FD3D12CommandContextBase(class FD3D12Adapter* InParent, FRHIGPUMask InNodeMask, bool InIsDefaultContext, bool InIsAsyncComputeContext);

	void RHIBeginDrawingViewport(FViewportRHIParamRef Viewport, FTextureRHIParamRef RenderTargetRHI) final override;
	void RHIEndDrawingViewport(FViewportRHIParamRef Viewport, bool bPresent, bool bLockToVsync) final override;

	void RHIBeginFrame() final override;
	void RHIEndFrame() final override;

	void RHIWaitComputeFence(FComputeFenceRHIParamRef InFence) final override;

	virtual void UpdateMemoryStats();

	FRHIGPUMask GetGPUMask() const { return GPUMask; }

	bool IsDefaultContext() const { return bIsDefaultContext; }

	virtual void RHISetAsyncComputeBudget(EAsyncComputeBudget Budget) {}

protected:

	virtual FD3D12CommandContext* GetContext(uint32 InGPUIndex) = 0;

	// State for begin/end draw primitive UP interface.
	struct FUserPrimitiveData
	{
		FUserPrimitiveData() { FMemory::Memzero(*this); }
		~FUserPrimitiveData() { check(!VertexData && !IndexData); }

		uint32 PrimitiveType;
		uint32 NumPrimitives;
		uint32 NumVertices;
		uint32 VertexDataStride;
		void* VertexData;

		uint32 MinVertexIndex;
		uint32 NumIndices;
		uint32 IndexDataStride;
		void* IndexData;

		operator bool() const { return NumVertices != 0; }
		void Reset() { FMemory::Memzero(*this); }
	};

	FUserPrimitiveData PendingUP;

protected:

	FRHIGPUMask GPUMask;

	const bool bIsDefaultContext;
	const bool bIsAsyncComputeContext;
};

class FD3D12CommandContext : public FD3D12CommandContextBase, public FD3D12DeviceChild
{
public:
	FD3D12CommandContext(class FD3D12Device* InParent, FD3D12SubAllocatedOnlineHeap::SubAllocationDesc& SubHeapDesc, bool InIsDefaultContext, bool InIsAsyncComputeContext = false);
	virtual ~FD3D12CommandContext();

	FD3D12CommandListManager& GetCommandListManager();

	template<typename TRHIType>
	static FORCEINLINE typename TD3D12ResourceTraits<TRHIType>::TConcreteType* ResourceCast(TRHIType* Resource)
	{
		return static_cast<typename TD3D12ResourceTraits<TRHIType>::TConcreteType*>(Resource);
	}

	void EndFrame()
	{
		StateCache.GetDescriptorCache()->EndFrame();

		// Return the current command allocator to the pool so it can be reused for a future frame
		// Note: the default context releases it's command allocator before Present.
		if (!IsDefaultContext())
		{
			ReleaseCommandAllocator();
		}
	}

	// If necessary, this gets a new command allocator for this context.
	void ConditionalObtainCommandAllocator();

	// Next time a command list is opened on this context, it will use a different command allocator.
	void ReleaseCommandAllocator();

	// Cycle to a new command list, but don't execute the current one yet.
	void OpenCommandList();
	void CloseCommandList();

	// Close the D3D command list and execute it.  Optionally wait for the GPU to finish. Returns the handle to the command list so you can wait for it later.
	FD3D12CommandListHandle FlushCommands(bool WaitForCompletion = false);

	void Finish(TArray<FD3D12CommandListHandle>& CommandLists);

	void ClearState();
	void ConditionalClearShaderResource(FD3D12ResourceLocation* Resource);
	void ClearAllShaderResources();

	FD3D12FastConstantAllocator ConstantsAllocator;

	// Handles to the command list and direct command allocator this context owns (granted by the command list manager/command allocator manager), and a direct pointer to the D3D command list/command allocator.
	FD3D12CommandListHandle CommandListHandle;
	FD3D12CommandAllocator* CommandAllocator;
	FD3D12CommandAllocatorManager CommandAllocatorManager;

	FD3D12StateCache StateCache;

	FD3D12DynamicRHI& OwningRHI;

	// Tracks the currently set state blocks.
	FD3D12RenderTargetView* CurrentRenderTargets[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
	FD3D12UnorderedAccessView* CurrentUAVs[MAX_UAVS];
	FD3D12DepthStencilView* CurrentDepthStencilTarget;
	FD3D12TextureBase* CurrentDepthTexture;
	uint32 NumSimultaneousRenderTargets;
	uint32 NumUAVs;


	/** Track the currently bound uniform buffers. */
	FD3D12UniformBuffer* BoundUniformBuffers[SF_NumFrequencies][MAX_CBS];
	FUniformBufferRHIRef BoundUniformBufferRefs[SF_NumFrequencies][MAX_CBS];

	/** Bit array to track which uniform buffers have changed since the last draw call. */
	uint16 DirtyUniformBuffers[SF_NumFrequencies];

	/** Tracks the current depth stencil access type. */
	FExclusiveDepthStencil CurrentDSVAccessType;

	/** When a new shader is set, we discard all old constants set for the previous shader. */
	bool bDiscardSharedConstants;

	/** Set to true when the current shading setup uses tessellation */
	bool bUsingTessellation;

	virtual void FlushMetadata(FTextureRHIParamRef* InTextures, int32 NumTextures) {};

#if PLATFORM_SUPPORTS_VIRTUAL_TEXTURES
	bool bNeedFlushTextureCache;
	void InvalidateTextureCache() { bNeedFlushTextureCache = true; }
	inline void FlushTextureCacheIfNeeded()
	{
		if (bNeedFlushTextureCache)
		{
			FlushTextureCache();

			bNeedFlushTextureCache = false;
		}
	}
	virtual void FlushTextureCache() {};
#endif

	uint32 numDraws;
	uint32 numDispatches;
	uint32 numClears;
	uint32 numBarriers;
	uint32 numCopies;
	uint32 otherWorkCounter;

	bool HasDoneWork()
	{
		return (numDraws + numDispatches + numClears + numBarriers + numCopies + otherWorkCounter) > 0;
	}

	/** Dynamic vertex and index buffers. */
	FD3D12DynamicBuffer DynamicVB;
	FD3D12DynamicBuffer DynamicIB;

	/** Constant buffers for Set*ShaderParameter calls. */
	FD3D12ConstantBuffer VSConstantBuffer;
	FD3D12ConstantBuffer HSConstantBuffer;
	FD3D12ConstantBuffer DSConstantBuffer;
	FD3D12ConstantBuffer PSConstantBuffer;
	FD3D12ConstantBuffer GSConstantBuffer;
	FD3D12ConstantBuffer CSConstantBuffer;

	/** needs to be called before each draw call */
	void CommitNonComputeShaderConstants();

	/** needs to be called before each dispatch call */
	void CommitComputeShaderConstants();

	template <class ShaderType> void SetResourcesFromTables(const ShaderType* RESTRICT);
	void CommitGraphicsResourceTables();
	void CommitComputeResourceTables(FD3D12ComputeShader* ComputeShader);

	void ValidateExclusiveDepthStencilAccess(FExclusiveDepthStencil Src) const;

	void CommitRenderTargetsAndUAVs();

	template<typename TPixelShader>
	void ResolveTextureUsingShader(
		FRHICommandList_RecursiveHazardous& RHICmdList,
		FD3D12Texture2D* SourceTexture,
		FD3D12Texture2D* DestTexture,
		FD3D12RenderTargetView* DestSurfaceRTV,
		FD3D12DepthStencilView* DestSurfaceDSV,
		const D3D12_RESOURCE_DESC& ResolveTargetDesc,
		const FResolveRect& SourceRect,
		const FResolveRect& DestRect,
		typename TPixelShader::FParameter PixelShaderParameter
		);

	virtual void SetDepthBounds(float MinDepth, float MaxDepth);

	virtual void SetAsyncComputeBudgetInternal(EAsyncComputeBudget Budget) {}

	// IRHIComputeContext interface
	virtual void RHISetComputeShader(FComputeShaderRHIParamRef ComputeShader) final override;
	virtual void RHISetComputePipelineState(FRHIComputePipelineState* ComputePipelineState) final override;
	virtual void RHIDispatchComputeShader(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ) final override;
	virtual void RHIDispatchIndirectComputeShader(FVertexBufferRHIParamRef ArgumentBuffer, uint32 ArgumentOffset) final override;
	virtual void RHITransitionResources(EResourceTransitionAccess TransitionType, EResourceTransitionPipeline TransitionPipeline, FUnorderedAccessViewRHIParamRef* InUAVs, int32 NumUAVs, FComputeFenceRHIParamRef WriteComputeFenceRHI) final override;
	virtual void RHISetShaderTexture(FComputeShaderRHIParamRef PixelShader, uint32 TextureIndex, FTextureRHIParamRef NewTexture) final override;
	virtual void RHISetShaderSampler(FComputeShaderRHIParamRef ComputeShader, uint32 SamplerIndex, FSamplerStateRHIParamRef NewState) final override;
	virtual void RHISetUAVParameter(FComputeShaderRHIParamRef ComputeShader, uint32 UAVIndex, FUnorderedAccessViewRHIParamRef UAV) final override;
	virtual void RHISetUAVParameter(FComputeShaderRHIParamRef ComputeShader, uint32 UAVIndex, FUnorderedAccessViewRHIParamRef UAV, uint32 InitialCount) final override;
	virtual void RHISetShaderResourceViewParameter(FComputeShaderRHIParamRef ComputeShader, uint32 SamplerIndex, FShaderResourceViewRHIParamRef SRV) final override;
	virtual void RHISetShaderUniformBuffer(FComputeShaderRHIParamRef ComputeShader, uint32 BufferIndex, FUniformBufferRHIParamRef Buffer) final override;
	virtual void RHISetShaderParameter(FComputeShaderRHIParamRef ComputeShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override;
	virtual void RHIPushEvent(const TCHAR* Name, FColor Color) final override;
	virtual void RHIPopEvent() final override;
	virtual void RHISubmitCommandsHint() final override;

	// IRHICommandContext interface
	virtual void RHIAutomaticCacheFlushAfterComputeShader(bool bEnable) final override;
	virtual void RHIFlushComputeShaderCache() final override;
	virtual void RHISetMultipleViewports(uint32 Count, const FViewportBounds* Data) final override;
	virtual void RHIClearTinyUAV(FUnorderedAccessViewRHIParamRef UnorderedAccessViewRHI, const uint32* Values) final override;
	virtual void RHICopyToResolveTarget(FTextureRHIParamRef SourceTexture, FTextureRHIParamRef DestTexture, const FResolveParams& ResolveParams) final override;
	virtual void RHITransitionResources(EResourceTransitionAccess TransitionType, FTextureRHIParamRef* InTextures, int32 NumTextures) final override;
	virtual void RHIBeginRenderQuery(FRenderQueryRHIParamRef RenderQuery) final override;
	virtual void RHIEndRenderQuery(FRenderQueryRHIParamRef RenderQuery) final override;
	void RHIBeginOcclusionQueryBatch(uint32 NumQueriesInBatch);
	void RHIEndOcclusionQueryBatch();
	virtual void RHIBeginScene() final override;
	virtual void RHIEndScene() final override;
	virtual void RHISetStreamSource(uint32 StreamIndex, FVertexBufferRHIParamRef VertexBuffer, uint32 Offset) final override;
	virtual void RHISetRasterizerState(FRasterizerStateRHIParamRef NewState) final override;
	virtual void RHISetViewport(uint32 MinX, uint32 MinY, float MinZ, uint32 MaxX, uint32 MaxY, float MaxZ) final override;
	virtual void RHISetScissorRect(bool bEnable, uint32 MinX, uint32 MinY, uint32 MaxX, uint32 MaxY) final override;
	virtual void RHISetBoundShaderState(FBoundShaderStateRHIParamRef BoundShaderState) final override;
	virtual void RHISetGraphicsPipelineState(FGraphicsPipelineStateRHIParamRef GraphicsPipelineState) final override;
	virtual void RHISetShaderTexture(FVertexShaderRHIParamRef VertexShader, uint32 TextureIndex, FTextureRHIParamRef NewTexture) final override;
	virtual void RHISetShaderTexture(FHullShaderRHIParamRef HullShader, uint32 TextureIndex, FTextureRHIParamRef NewTexture) final override;
	virtual void RHISetShaderTexture(FDomainShaderRHIParamRef DomainShader, uint32 TextureIndex, FTextureRHIParamRef NewTexture) final override;
	virtual void RHISetShaderTexture(FGeometryShaderRHIParamRef GeometryShader, uint32 TextureIndex, FTextureRHIParamRef NewTexture) final override;
	virtual void RHISetShaderTexture(FPixelShaderRHIParamRef PixelShader, uint32 TextureIndex, FTextureRHIParamRef NewTexture) final override;
	virtual void RHISetShaderSampler(FVertexShaderRHIParamRef VertexShader, uint32 SamplerIndex, FSamplerStateRHIParamRef NewState) final override;
	virtual void RHISetShaderSampler(FGeometryShaderRHIParamRef GeometryShader, uint32 SamplerIndex, FSamplerStateRHIParamRef NewState) final override;
	virtual void RHISetShaderSampler(FDomainShaderRHIParamRef DomainShader, uint32 SamplerIndex, FSamplerStateRHIParamRef NewState) final override;
	virtual void RHISetShaderSampler(FHullShaderRHIParamRef HullShader, uint32 SamplerIndex, FSamplerStateRHIParamRef NewState) final override;
	virtual void RHISetShaderSampler(FPixelShaderRHIParamRef PixelShader, uint32 SamplerIndex, FSamplerStateRHIParamRef NewState) final override;
	virtual void RHISetShaderResourceViewParameter(FPixelShaderRHIParamRef PixelShader, uint32 SamplerIndex, FShaderResourceViewRHIParamRef SRV) final override;
	virtual void RHISetShaderResourceViewParameter(FVertexShaderRHIParamRef VertexShader, uint32 SamplerIndex, FShaderResourceViewRHIParamRef SRV) final override;
	virtual void RHISetShaderResourceViewParameter(FHullShaderRHIParamRef HullShader, uint32 SamplerIndex, FShaderResourceViewRHIParamRef SRV) final override;
	virtual void RHISetShaderResourceViewParameter(FDomainShaderRHIParamRef DomainShader, uint32 SamplerIndex, FShaderResourceViewRHIParamRef SRV) final override;
	virtual void RHISetShaderResourceViewParameter(FGeometryShaderRHIParamRef GeometryShader, uint32 SamplerIndex, FShaderResourceViewRHIParamRef SRV) final override;
	virtual void RHISetShaderUniformBuffer(FVertexShaderRHIParamRef VertexShader, uint32 BufferIndex, FUniformBufferRHIParamRef Buffer) final override;
	virtual void RHISetShaderUniformBuffer(FHullShaderRHIParamRef HullShader, uint32 BufferIndex, FUniformBufferRHIParamRef Buffer) final override;
	virtual void RHISetShaderUniformBuffer(FDomainShaderRHIParamRef DomainShader, uint32 BufferIndex, FUniformBufferRHIParamRef Buffer) final override;
	virtual void RHISetShaderUniformBuffer(FGeometryShaderRHIParamRef GeometryShader, uint32 BufferIndex, FUniformBufferRHIParamRef Buffer) final override;
	virtual void RHISetShaderUniformBuffer(FPixelShaderRHIParamRef PixelShader, uint32 BufferIndex, FUniformBufferRHIParamRef Buffer) final override;
	virtual void RHISetShaderParameter(FVertexShaderRHIParamRef VertexShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override;
	virtual void RHISetShaderParameter(FPixelShaderRHIParamRef PixelShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override;
	virtual void RHISetShaderParameter(FHullShaderRHIParamRef HullShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override;
	virtual void RHISetShaderParameter(FDomainShaderRHIParamRef DomainShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override;
	virtual void RHISetShaderParameter(FGeometryShaderRHIParamRef GeometryShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override;
	virtual void RHISetDepthStencilState(FDepthStencilStateRHIParamRef NewState, uint32 StencilRef) final override;
	virtual void RHISetStencilRef(uint32 StencilRef) final override;
	virtual void RHISetBlendState(FBlendStateRHIParamRef NewState, const FLinearColor& BlendFactor) final override;
	virtual void RHISetBlendFactor(const FLinearColor& BlendFactor) final override;
	virtual void RHISetRenderTargets(uint32 NumSimultaneousRenderTargets, const FRHIRenderTargetView* NewRenderTargets, const FRHIDepthRenderTargetView* NewDepthStencilTarget, uint32 NumUAVs, const FUnorderedAccessViewRHIParamRef* UAVs) final override;
	virtual void RHISetRenderTargetsAndClear(const FRHISetRenderTargetsInfo& RenderTargetsInfo) final override;
	virtual void RHIBindClearMRTValues(bool bClearColor, bool bClearDepth, bool bClearStencil) final override;
	virtual void RHIDrawPrimitive(uint32 PrimitiveType, uint32 BaseVertexIndex, uint32 NumPrimitives, uint32 NumInstances) final override;
	virtual void RHIDrawPrimitiveIndirect(uint32 PrimitiveType, FVertexBufferRHIParamRef ArgumentBuffer, uint32 ArgumentOffset) final override;
	virtual void RHIDrawIndexedIndirect(FIndexBufferRHIParamRef IndexBufferRHI, uint32 PrimitiveType, FStructuredBufferRHIParamRef ArgumentsBufferRHI, int32 DrawArgumentsIndex, uint32 NumInstances) final override;
	virtual void RHIDrawIndexedPrimitive(FIndexBufferRHIParamRef IndexBuffer, uint32 PrimitiveType, int32 BaseVertexIndex, uint32 FirstInstance, uint32 NumVertices, uint32 StartIndex, uint32 NumPrimitives, uint32 NumInstances) final override;
	virtual void RHIDrawIndexedPrimitiveIndirect(uint32 PrimitiveType, FIndexBufferRHIParamRef IndexBuffer, FVertexBufferRHIParamRef ArgumentBuffer, uint32 ArgumentOffset) final override;
	virtual void RHIBeginDrawPrimitiveUP(uint32 PrimitiveType, uint32 NumPrimitives, uint32 NumVertices, uint32 VertexDataStride, void*& OutVertexData) final override;
	virtual void RHIEndDrawPrimitiveUP() final override;
	virtual void RHIBeginDrawIndexedPrimitiveUP(uint32 PrimitiveType, uint32 NumPrimitives, uint32 NumVertices, uint32 VertexDataStride, void*& OutVertexData, uint32 MinVertexIndex, uint32 NumIndices, uint32 IndexDataStride, void*& OutIndexData) final override;
	virtual void RHIEndDrawIndexedPrimitiveUP() final override;
	virtual void RHIEnableDepthBoundsTest(bool bEnable) final override;
	virtual void RHISetDepthBounds(float MinDepth, float MaxDepth) final override;
	virtual void RHIUpdateTextureReference(FTextureReferenceRHIParamRef TextureRef, FTextureRHIParamRef NewTexture) final override;

	virtual void RHIClearMRTImpl(bool bClearColor, int32 NumClearColors, const FLinearColor* ColorArray, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil);

	virtual void RHIBeginRenderPass(const FRHIRenderPassInfo& InInfo, const TCHAR* InName) final override
	{
		IRHICommandContext::RHIBeginRenderPass(InInfo, InName);
		if (InInfo.bOcclusionQueries)
		{
			RHIBeginOcclusionQueryBatch(InInfo.NumOcclusionQueries);
		}
	}

	virtual void RHIEndRenderPass() final override
	{
		if (RenderPassInfo.bOcclusionQueries)
		{
			RHIEndOcclusionQueryBatch();
		}
		IRHICommandContext::RHIEndRenderPass();
	}

	// When using Alternate Frame Rendering some temporal effects i.e. effects which consume GPU work from previous frames must synchronize their resources
	// to prevent visual corruption.

	// This should be called right before the effect consumes it's temporal resources.
	virtual void RHIWaitForTemporalEffect(const FName& InEffectName) final AFR_API_OVERRIDE;

	// This should be called right after the effect generates the resources which will be used in subsequent frame(s).
	virtual void RHIBroadcastTemporalEffect(const FName& InEffectName, FTextureRHIParamRef* InTextures, int32 NumTextures) final AFR_API_OVERRIDE;

	template<typename ObjectType, typename RHIType>
	FORCEINLINE_DEBUGGABLE ObjectType* RetrieveObject(RHIType RHIObject)
	{
#if !WITH_MGPU
		return FD3D12DynamicRHI::ResourceCast(RHIObject);
#else
		ObjectType* Object = FD3D12DynamicRHI::ResourceCast(RHIObject);
		if (GNumExplicitGPUsForRendering > 1)
		{
			if (!Object)
			{
				return nullptr;
			}

			while (Object && Object->GetParentDevice() != GetParentDevice())
			{
				Object = Object->GetNextObject();
			}

			check(Object)
		}
		return Object;
#endif
	}

	inline FD3D12TextureBase* RetrieveTextureBase(FRHITexture* Texture)
	{
		if (!Texture)
		{
			return nullptr;
		}
		
#if !WITH_MGPU
		return ((FD3D12TextureBase*)Texture->GetTextureBaseRHI());
#else
		FD3D12TextureBase* Result((FD3D12TextureBase*)Texture->GetTextureBaseRHI());
		if (GNumExplicitGPUsForRendering > 1)
		{
			if (!Result)
			{
				return nullptr;
			}

			if (Result->GetBaseShaderResource() != Result)
			{
				Result = (FD3D12TextureBase*)Result->GetBaseShaderResource();
			}

			while (Result && Result->GetParentDevice() != GetParentDevice())
			{
				Result = Result->GetNextObject();
			}

			check(Result);
			return Result;
		}

		return Result;
#endif
	}

	uint32 GetGPUIndex() const { return GPUMask.ToIndex(); }

protected: 

	FD3D12CommandContext* GetContext(uint32 InGPUIndex) final override 
	{  
		return InGPUIndex == GetGPUIndex() ? this : nullptr; 
	}

private:
	void RHIClearMRT(bool bClearColor, int32 NumClearColors, const FLinearColor* ColorArray, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil);
};

// This class is a temporary shim to get AFR working. Currently the upper engine only queries for the 'Immediate Context'
// once. However when in AFR we need to switch which context is active every frame so we return an instance of this class
// as the default context so that we can control when to swap which device we talk to.
// Because IRHICommandContext is pure virtual we can return the normal FD3D12CommandContext when not using mGPU thus there
// is no additional overhead for the common case i.e. 1 GPU.
class FD3D12CommandContextRedirector final : public FD3D12CommandContextBase
{
public:
	FD3D12CommandContextRedirector(class FD3D12Adapter* InParent, bool InIsDefaultContext, bool InIsAsyncComputeContext);

#define ContextRedirect(Call) { for (uint32 GPUIndex : GPUMask) PhysicalContexts[GPUIndex]->##Call; }
#define ContextGPU0(Call) { PhysicalContexts[0]->##Call; }

	FORCEINLINE virtual void RHISetComputeShader(FComputeShaderRHIParamRef ComputeShader) final override
	{
		ContextRedirect(RHISetComputeShader(ComputeShader));
	}
	FORCEINLINE virtual void RHISetComputePipelineState(FRHIComputePipelineState* ComputePipelineState) final override
	{
		ContextRedirect(RHISetComputePipelineState(ComputePipelineState));
	}
	FORCEINLINE virtual void RHIDispatchComputeShader(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ) final override
	{
		ContextRedirect(RHIDispatchComputeShader(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ));
	}
	FORCEINLINE virtual void RHIDispatchIndirectComputeShader(FVertexBufferRHIParamRef ArgumentBuffer, uint32 ArgumentOffset) final override
	{
		ContextRedirect(RHIDispatchIndirectComputeShader(ArgumentBuffer, ArgumentOffset));
	}
	virtual void RHITransitionResources(EResourceTransitionAccess TransitionType, EResourceTransitionPipeline TransitionPipeline, FUnorderedAccessViewRHIParamRef* InUAVs, int32 NumUAVs, FComputeFenceRHIParamRef WriteComputeFenceRHI) final override;

	FORCEINLINE virtual void RHISetShaderTexture(FComputeShaderRHIParamRef PixelShader, uint32 TextureIndex, FTextureRHIParamRef NewTexture) final override
	{
		ContextRedirect(RHISetShaderTexture(PixelShader, TextureIndex, NewTexture));
	}
	FORCEINLINE virtual void RHISetShaderSampler(FComputeShaderRHIParamRef ComputeShader, uint32 SamplerIndex, FSamplerStateRHIParamRef NewState) final override
	{
		ContextRedirect(RHISetShaderSampler(ComputeShader, SamplerIndex, NewState));
	}
	FORCEINLINE virtual void RHISetUAVParameter(FComputeShaderRHIParamRef ComputeShader, uint32 UAVIndex, FUnorderedAccessViewRHIParamRef UAV) final override
	{
		ContextRedirect(RHISetUAVParameter(ComputeShader, UAVIndex, UAV));
	}
	FORCEINLINE virtual void RHISetUAVParameter(FComputeShaderRHIParamRef ComputeShader, uint32 UAVIndex, FUnorderedAccessViewRHIParamRef UAV, uint32 InitialCount) final override
	{
		ContextRedirect(RHISetUAVParameter(ComputeShader, UAVIndex, UAV, InitialCount));
	}
	FORCEINLINE virtual void RHISetShaderResourceViewParameter(FComputeShaderRHIParamRef ComputeShader, uint32 SamplerIndex, FShaderResourceViewRHIParamRef SRV) final override
	{
		ContextRedirect(RHISetShaderResourceViewParameter(ComputeShader, SamplerIndex, SRV));
	}
	FORCEINLINE virtual void RHISetShaderUniformBuffer(FComputeShaderRHIParamRef ComputeShader, uint32 BufferIndex, FUniformBufferRHIParamRef Buffer) final override
	{
		ContextRedirect(RHISetShaderUniformBuffer(ComputeShader, BufferIndex, Buffer));
	}
	FORCEINLINE virtual void RHISetShaderParameter(FComputeShaderRHIParamRef ComputeShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override
	{
		ContextRedirect(RHISetShaderParameter(ComputeShader, BufferIndex, BaseIndex, NumBytes, NewValue));
	}
	FORCEINLINE virtual void RHIPushEvent(const TCHAR* Name, FColor Color) final override
	{
		ContextRedirect(RHIPushEvent(Name, Color));
	}
	FORCEINLINE virtual void RHIPopEvent() final override
	{
		ContextRedirect(RHIPopEvent());
	}
	FORCEINLINE virtual void RHISubmitCommandsHint() final override
	{
		ContextRedirect(RHISubmitCommandsHint());
	}

	// IRHICommandContext interface
	FORCEINLINE virtual void RHIAutomaticCacheFlushAfterComputeShader(bool bEnable) final override
	{
		ContextRedirect(RHIAutomaticCacheFlushAfterComputeShader(bEnable));
	}
	FORCEINLINE virtual void RHIFlushComputeShaderCache() final override
	{
		ContextRedirect(RHIFlushComputeShaderCache());
	}
	FORCEINLINE virtual void RHISetMultipleViewports(uint32 Count, const FViewportBounds* Data) final override
	{
		ContextRedirect(RHISetMultipleViewports(Count, Data));
	}
	FORCEINLINE virtual void RHIClearTinyUAV(FUnorderedAccessViewRHIParamRef UnorderedAccessViewRHI, const uint32* Values) final override
	{
		ContextRedirect(RHIClearTinyUAV(UnorderedAccessViewRHI, Values));
	}
	FORCEINLINE virtual void RHICopyToResolveTarget(FTextureRHIParamRef SourceTexture, FTextureRHIParamRef DestTexture, const FResolveParams& ResolveParams) final override
	{
		ContextRedirect(RHICopyToResolveTarget(SourceTexture, DestTexture, ResolveParams));
	}
	FORCEINLINE virtual void RHITransitionResources(EResourceTransitionAccess TransitionType, FTextureRHIParamRef* InTextures, int32 NumTextures) final override
	{
		ContextRedirect(RHITransitionResources(TransitionType, InTextures, NumTextures));
	}
	FORCEINLINE virtual void RHIBeginRenderQuery(FRenderQueryRHIParamRef RenderQuery) final override
	{
		ContextRedirect(RHIBeginRenderQuery(RenderQuery));
	}
	FORCEINLINE virtual void RHIEndRenderQuery(FRenderQueryRHIParamRef RenderQuery) final override
	{
		ContextRedirect(RHIEndRenderQuery(RenderQuery));
	}
	FORCEINLINE virtual void RHIBeginScene() final override
	{
		ContextRedirect(RHIBeginScene());
	}
	FORCEINLINE virtual void RHIEndScene() final override
	{
		ContextRedirect(RHIEndScene());
	}
	FORCEINLINE virtual void RHISetStreamSource(uint32 StreamIndex, FVertexBufferRHIParamRef VertexBuffer, uint32 Offset) final override
	{
		ContextRedirect(RHISetStreamSource(StreamIndex, VertexBuffer, Offset));
	}
	FORCEINLINE virtual void RHISetRasterizerState(FRasterizerStateRHIParamRef NewState) final override
	{
		ContextRedirect(RHISetRasterizerState(NewState));
	}
	FORCEINLINE virtual void RHISetViewport(uint32 MinX, uint32 MinY, float MinZ, uint32 MaxX, uint32 MaxY, float MaxZ) final override
	{
		ContextRedirect(RHISetViewport(MinX, MinY, MinZ, MaxX, MaxY, MaxZ));
	}
	FORCEINLINE virtual void RHISetScissorRect(bool bEnable, uint32 MinX, uint32 MinY, uint32 MaxX, uint32 MaxY) final override
	{
		ContextRedirect(RHISetScissorRect(bEnable, MinX, MinY, MaxX, MaxY));
	}
	FORCEINLINE virtual void RHISetBoundShaderState(FBoundShaderStateRHIParamRef BoundShaderState) final override
	{
		ContextRedirect(RHISetBoundShaderState(BoundShaderState));
	}
	FORCEINLINE void RHISetGraphicsPipelineState(FGraphicsPipelineStateRHIParamRef GraphicsPipelineState) final override
	{
		ContextRedirect(RHISetGraphicsPipelineState(GraphicsPipelineState));
	}
	FORCEINLINE virtual void RHISetShaderTexture(FVertexShaderRHIParamRef VertexShader, uint32 TextureIndex, FTextureRHIParamRef NewTexture) final override
	{
		ContextRedirect(RHISetShaderTexture(VertexShader, TextureIndex, NewTexture));
	}
	FORCEINLINE virtual void RHISetShaderTexture(FHullShaderRHIParamRef HullShader, uint32 TextureIndex, FTextureRHIParamRef NewTexture) final override
	{
		ContextRedirect(RHISetShaderTexture(HullShader, TextureIndex, NewTexture));
	}
	FORCEINLINE virtual void RHISetShaderTexture(FDomainShaderRHIParamRef DomainShader, uint32 TextureIndex, FTextureRHIParamRef NewTexture) final override
	{
		ContextRedirect(RHISetShaderTexture(DomainShader, TextureIndex, NewTexture));
	}
	FORCEINLINE virtual void RHISetShaderTexture(FGeometryShaderRHIParamRef GeometryShader, uint32 TextureIndex, FTextureRHIParamRef NewTexture) final override
	{
		ContextRedirect(RHISetShaderTexture(GeometryShader, TextureIndex, NewTexture));
	}
	FORCEINLINE virtual void RHISetShaderTexture(FPixelShaderRHIParamRef PixelShader, uint32 TextureIndex, FTextureRHIParamRef NewTexture) final override
	{
		ContextRedirect(RHISetShaderTexture(PixelShader, TextureIndex, NewTexture));
	}
	FORCEINLINE virtual void RHISetShaderSampler(FVertexShaderRHIParamRef VertexShader, uint32 SamplerIndex, FSamplerStateRHIParamRef NewState) final override
	{
		ContextRedirect(RHISetShaderSampler(VertexShader, SamplerIndex, NewState));
	}
	FORCEINLINE virtual void RHISetShaderSampler(FGeometryShaderRHIParamRef GeometryShader, uint32 SamplerIndex, FSamplerStateRHIParamRef NewState) final override
	{
		ContextRedirect(RHISetShaderSampler(GeometryShader, SamplerIndex, NewState));
	}
	FORCEINLINE virtual void RHISetShaderSampler(FDomainShaderRHIParamRef DomainShader, uint32 SamplerIndex, FSamplerStateRHIParamRef NewState) final override
	{
		ContextRedirect(RHISetShaderSampler(DomainShader, SamplerIndex, NewState));
	}
	FORCEINLINE virtual void RHISetShaderSampler(FHullShaderRHIParamRef HullShader, uint32 SamplerIndex, FSamplerStateRHIParamRef NewState) final override
	{
		ContextRedirect(RHISetShaderSampler(HullShader, SamplerIndex, NewState));
	}
	FORCEINLINE virtual void RHISetShaderSampler(FPixelShaderRHIParamRef PixelShader, uint32 SamplerIndex, FSamplerStateRHIParamRef NewState) final override
	{
		ContextRedirect(RHISetShaderSampler(PixelShader, SamplerIndex, NewState));
	}
	FORCEINLINE virtual void RHISetShaderResourceViewParameter(FPixelShaderRHIParamRef PixelShader, uint32 SamplerIndex, FShaderResourceViewRHIParamRef SRV) final override
	{
		ContextRedirect(RHISetShaderResourceViewParameter(PixelShader, SamplerIndex, SRV));
	}
	FORCEINLINE virtual void RHISetShaderResourceViewParameter(FVertexShaderRHIParamRef VertexShader, uint32 SamplerIndex, FShaderResourceViewRHIParamRef SRV) final override
	{
		ContextRedirect(RHISetShaderResourceViewParameter(VertexShader, SamplerIndex, SRV));
	}
	FORCEINLINE virtual void RHISetShaderResourceViewParameter(FHullShaderRHIParamRef HullShader, uint32 SamplerIndex, FShaderResourceViewRHIParamRef SRV) final override
	{
		ContextRedirect(RHISetShaderResourceViewParameter(HullShader, SamplerIndex, SRV));
	}
	FORCEINLINE virtual void RHISetShaderResourceViewParameter(FDomainShaderRHIParamRef DomainShader, uint32 SamplerIndex, FShaderResourceViewRHIParamRef SRV) final override
	{
		ContextRedirect(RHISetShaderResourceViewParameter(DomainShader, SamplerIndex, SRV));
	}
	FORCEINLINE virtual void RHISetShaderResourceViewParameter(FGeometryShaderRHIParamRef GeometryShader, uint32 SamplerIndex, FShaderResourceViewRHIParamRef SRV) final override
	{
		ContextRedirect(RHISetShaderResourceViewParameter(GeometryShader, SamplerIndex, SRV));
	}
	FORCEINLINE virtual void RHISetShaderUniformBuffer(FVertexShaderRHIParamRef VertexShader, uint32 BufferIndex, FUniformBufferRHIParamRef Buffer) final override
	{
		ContextRedirect(RHISetShaderUniformBuffer(VertexShader, BufferIndex, Buffer));
	}
	FORCEINLINE virtual void RHISetShaderUniformBuffer(FHullShaderRHIParamRef HullShader, uint32 BufferIndex, FUniformBufferRHIParamRef Buffer) final override
	{
		ContextRedirect(RHISetShaderUniformBuffer(HullShader, BufferIndex, Buffer));
	}
	FORCEINLINE virtual void RHISetShaderUniformBuffer(FDomainShaderRHIParamRef DomainShader, uint32 BufferIndex, FUniformBufferRHIParamRef Buffer) final override
	{
		ContextRedirect(RHISetShaderUniformBuffer(DomainShader, BufferIndex, Buffer));
	}
	FORCEINLINE virtual void RHISetShaderUniformBuffer(FGeometryShaderRHIParamRef GeometryShader, uint32 BufferIndex, FUniformBufferRHIParamRef Buffer) final override
	{
		ContextRedirect(RHISetShaderUniformBuffer(GeometryShader, BufferIndex, Buffer));
	}
	FORCEINLINE virtual void RHISetShaderUniformBuffer(FPixelShaderRHIParamRef PixelShader, uint32 BufferIndex, FUniformBufferRHIParamRef Buffer) final override
	{
		ContextRedirect(RHISetShaderUniformBuffer(PixelShader, BufferIndex, Buffer));
	}
	FORCEINLINE virtual void RHISetShaderParameter(FVertexShaderRHIParamRef VertexShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override
	{
		ContextRedirect(RHISetShaderParameter(VertexShader, BufferIndex, BaseIndex, NumBytes, NewValue));
	}
	FORCEINLINE virtual void RHISetShaderParameter(FPixelShaderRHIParamRef PixelShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override
	{
		ContextRedirect(RHISetShaderParameter(PixelShader, BufferIndex, BaseIndex, NumBytes, NewValue));
	}
	FORCEINLINE virtual void RHISetShaderParameter(FHullShaderRHIParamRef HullShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override
	{
		ContextRedirect(RHISetShaderParameter(HullShader, BufferIndex, BaseIndex, NumBytes, NewValue));
	}
	FORCEINLINE virtual void RHISetShaderParameter(FDomainShaderRHIParamRef DomainShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override
	{
		ContextRedirect(RHISetShaderParameter(DomainShader, BufferIndex, BaseIndex, NumBytes, NewValue));
	}
	FORCEINLINE virtual void RHISetShaderParameter(FGeometryShaderRHIParamRef GeometryShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override
	{
		ContextRedirect(RHISetShaderParameter(GeometryShader, BufferIndex, BaseIndex, NumBytes, NewValue));
	}
	FORCEINLINE virtual void RHISetDepthStencilState(FDepthStencilStateRHIParamRef NewState, uint32 StencilRef) final override
	{
		ContextRedirect(RHISetDepthStencilState(NewState, StencilRef));
	}
	FORCEINLINE virtual void RHISetStencilRef(uint32 StencilRef) final override
	{
		ContextRedirect(RHISetStencilRef(StencilRef));
	}
	FORCEINLINE virtual void RHISetBlendState(FBlendStateRHIParamRef NewState, const FLinearColor& BlendFactor) final override
	{
		ContextRedirect(RHISetBlendState(NewState, BlendFactor));
	}
	FORCEINLINE void RHISetBlendFactor(const FLinearColor& BlendFactor) final override
	{
		ContextRedirect(RHISetBlendFactor(BlendFactor));
	}
	FORCEINLINE virtual void RHISetRenderTargets(uint32 NumSimultaneousRenderTargets, const FRHIRenderTargetView* NewRenderTargets, const FRHIDepthRenderTargetView* NewDepthStencilTarget, uint32 NumUAVs, const FUnorderedAccessViewRHIParamRef* UAVs) final override
	{
		ContextRedirect(RHISetRenderTargets(NumSimultaneousRenderTargets, NewRenderTargets, NewDepthStencilTarget, NumUAVs, UAVs));
	}
	FORCEINLINE virtual void RHISetRenderTargetsAndClear(const FRHISetRenderTargetsInfo& RenderTargetsInfo) final override
	{
		ContextRedirect(RHISetRenderTargetsAndClear(RenderTargetsInfo));
	}
	FORCEINLINE virtual void RHIBindClearMRTValues(bool bClearColor, bool bClearDepth, bool bClearStencil) final override
	{
		ContextRedirect(RHIBindClearMRTValues(bClearColor, bClearDepth, bClearStencil));
	}
	FORCEINLINE virtual void RHIDrawPrimitive(uint32 PrimitiveType, uint32 BaseVertexIndex, uint32 NumPrimitives, uint32 NumInstances) final override
	{
		ContextRedirect(RHIDrawPrimitive(PrimitiveType, BaseVertexIndex, NumPrimitives, NumInstances));
	}
	FORCEINLINE virtual void RHIDrawPrimitiveIndirect(uint32 PrimitiveType, FVertexBufferRHIParamRef ArgumentBuffer, uint32 ArgumentOffset) final override
	{
		ContextRedirect(RHIDrawPrimitiveIndirect(PrimitiveType, ArgumentBuffer, ArgumentOffset));
	}
	FORCEINLINE virtual void RHIDrawIndexedIndirect(FIndexBufferRHIParamRef IndexBufferRHI, uint32 PrimitiveType, FStructuredBufferRHIParamRef ArgumentsBufferRHI, int32 DrawArgumentsIndex, uint32 NumInstances) final override
	{
		ContextRedirect(RHIDrawIndexedIndirect(IndexBufferRHI, PrimitiveType, ArgumentsBufferRHI, DrawArgumentsIndex, NumInstances));
	}
	FORCEINLINE virtual void RHIDrawIndexedPrimitive(FIndexBufferRHIParamRef IndexBuffer, uint32 PrimitiveType, int32 BaseVertexIndex, uint32 FirstInstance, uint32 NumVertices, uint32 StartIndex, uint32 NumPrimitives, uint32 NumInstances) final override
	{
		ContextRedirect(RHIDrawIndexedPrimitive(IndexBuffer, PrimitiveType, BaseVertexIndex, FirstInstance, NumVertices, StartIndex, NumPrimitives, NumInstances));
	}
	FORCEINLINE virtual void RHIDrawIndexedPrimitiveIndirect(uint32 PrimitiveType, FIndexBufferRHIParamRef IndexBuffer, FVertexBufferRHIParamRef ArgumentBuffer, uint32 ArgumentOffset) final override
	{
		ContextRedirect(RHIDrawIndexedPrimitiveIndirect(PrimitiveType, IndexBuffer, ArgumentBuffer, ArgumentOffset));
	}
	
	virtual void RHIBeginDrawPrimitiveUP(uint32 PrimitiveType, uint32 NumPrimitives, uint32 NumVertices, uint32 VertexDataStride, void*& OutVertexData) final override;
	
	virtual void RHIEndDrawPrimitiveUP() final override;
	
	virtual void RHIBeginDrawIndexedPrimitiveUP(uint32 PrimitiveType, uint32 NumPrimitives, uint32 NumVertices, uint32 VertexDataStride, void*& OutVertexData, uint32 MinVertexIndex, uint32 NumIndices, uint32 IndexDataStride, void*& OutIndexData) final override;
	
	virtual void RHIEndDrawIndexedPrimitiveUP() final override;

	FORCEINLINE virtual void RHIEnableDepthBoundsTest(bool bEnable) final override
	{
		ContextRedirect(RHIEnableDepthBoundsTest(bEnable));
	}
	FORCEINLINE virtual void RHISetDepthBounds(float MinDepth, float MaxDepth) final override
	{
		ContextRedirect(RHISetDepthBounds(MinDepth, MaxDepth));
	}

	FORCEINLINE virtual void RHIUpdateTextureReference(FTextureReferenceRHIParamRef TextureRef, FTextureRHIParamRef NewTexture) final override
	{
		ContextRedirect(RHIUpdateTextureReference(TextureRef, NewTexture));
	}
	FORCEINLINE virtual void RHIClearMRTImpl(bool bClearColor, int32 NumClearColors, const FLinearColor* ColorArray, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil)
	{
		ContextRedirect(RHIClearMRTImpl(bClearColor, NumClearColors, ColorArray, bClearDepth, Depth, bClearStencil, Stencil));
	}

	FORCEINLINE virtual void RHIWaitForTemporalEffect(const FName& InEffectName) final AFR_API_OVERRIDE
	{
		ContextRedirect(RHIWaitForTemporalEffect(InEffectName));
	}

	FORCEINLINE virtual void RHIBroadcastTemporalEffect(const FName& InEffectName, FTextureRHIParamRef* InTextures, int32 NumTextures) final AFR_API_OVERRIDE
	{
		ContextRedirect(RHIBroadcastTemporalEffect(InEffectName, InTextures, NumTextures));
	}

	virtual void RHIBeginRenderPass(const FRHIRenderPassInfo& InInfo, const TCHAR* InName) final override
	{
		ContextRedirect(RHIBeginRenderPass(InInfo, InName));
	}

	virtual void RHIEndRenderPass() final override
	{
		ContextRedirect(RHIEndRenderPass());
	}

	FORCEINLINE void SetPhysicalContext(FD3D12CommandContext* Context)
	{
		check(Context);
		PhysicalContexts[Context->GetGPUIndex()] = Context;
	}

	FORCEINLINE FD3D12CommandContext* GetContext(uint32 GPUIndex) final override
	{
		return PhysicalContexts[GPUIndex];
	}

	FORCEINLINE void SetGPUMask(const FRHIGPUMask InGPUMask)
	{
		GPUMask = InGPUMask;
	}

private:

	FD3D12CommandContext* PhysicalContexts[MAX_NUM_GPUS];
};

class FD3D12TemporalEffect : public FD3D12AdapterChild
{
public:
	FD3D12TemporalEffect();
	FD3D12TemporalEffect(FD3D12Adapter* Parent, const FName& InEffectName);

	FD3D12TemporalEffect(const FD3D12TemporalEffect& Other);

	void Init();
	void Destroy();

	void WaitForPrevious(ED3D12CommandQueueType InQueueType);
	void SignalSyncComplete(ED3D12CommandQueueType InQueueType);

private:
	FD3D12Fence EffectFence;
};
