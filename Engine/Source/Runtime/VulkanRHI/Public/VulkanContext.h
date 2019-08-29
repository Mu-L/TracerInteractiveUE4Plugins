// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanContext.h: Class to generate Vulkan command buffers from RHI CommandLists
=============================================================================*/

#pragma once 

#include "VulkanResources.h"

class FVulkanDevice;
class FVulkanCommandBufferManager;
class FVulkanPendingGfxState;
class FVulkanPendingComputeState;
class FVulkanQueue;
class FVulkanOcclusionQueryPool;

class FTransitionAndLayoutManagerData
{
public:
	void TempCopy(const FTransitionAndLayoutManagerData& In)
	{
		Framebuffers = In.Framebuffers;
		RenderPasses = In.RenderPasses;
		Layouts = In.Layouts;
	}

protected:
	TMap<uint32, FVulkanRenderPass*> RenderPasses;
	struct FFramebufferList
	{
		TArray<FVulkanFramebuffer*> Framebuffer;
	};
	TMap<uint32, FFramebufferList*> Framebuffers;
	TMap<VkImage, VkImageLayout> Layouts;
};

class FTransitionAndLayoutManager : public FTransitionAndLayoutManagerData
{
	using FFramebufferList = FTransitionAndLayoutManagerData::FFramebufferList;
public:
	FTransitionAndLayoutManager()
		: CurrentRenderPass(nullptr)
		, CurrentFramebuffer(nullptr)
	{
	}

	void Destroy(FVulkanDevice& InDevice, FTransitionAndLayoutManager* Immediate);

	FVulkanFramebuffer* GetOrCreateFramebuffer(FVulkanDevice& InDevice, const FRHISetRenderTargetsInfo& RenderTargetsInfo, const FVulkanRenderTargetLayout& RTLayout, FVulkanRenderPass* RenderPass);
	FVulkanRenderPass* GetOrCreateRenderPass(FVulkanDevice& InDevice, const FVulkanRenderTargetLayout& RTLayout)
	{
		uint32 RenderPassHash = RTLayout.GetRenderPassHash();
		FVulkanRenderPass** FoundRenderPass = nullptr;
		{
			FScopeLock Lock(&RenderPassesCS);
			FoundRenderPass = RenderPasses.Find(RenderPassHash);
		}
		if (FoundRenderPass)
		{
			return *FoundRenderPass;
		}

		FVulkanRenderPass* RenderPass = new FVulkanRenderPass(InDevice, RTLayout);
		{
			FScopeLock Lock(&RenderPassesCS);
			RenderPasses.Add(RenderPassHash, RenderPass);
		}
		return RenderPass;
	}

	void BeginEmulatedRenderPass(FVulkanCommandListContext& Context, FVulkanDevice& InDevice, FVulkanCmdBuffer* CmdBuffer, const FRHISetRenderTargetsInfo& RenderTargetsInfo, const FVulkanRenderTargetLayout& RTLayout, FVulkanRenderPass* RenderPass, FVulkanFramebuffer* Framebuffer);
	void EndEmulatedRenderPass(FVulkanCmdBuffer* CmdBuffer);

	void BeginRealRenderPass(FVulkanCommandListContext& Context, FVulkanDevice& InDevice, FVulkanCmdBuffer* CmdBuffer, const FRHIRenderPassInfo& RPInfo, const FVulkanRenderTargetLayout& RTLayout, FVulkanRenderPass* RenderPass, FVulkanFramebuffer* Framebuffer);
	void EndRealRenderPass(FVulkanCmdBuffer* CmdBuffer);

	struct FGenerateMipsInfo
	{
		int32 NumRenderTargets = 0;

		bool bInsideGenerateMips;
		bool bLastMip;
		int32 CurrentSlice;
		int32 CurrentMip;

		struct
		{
			// Per face/slice array of mip layouts
			TArray<TArray<VkImageLayout>> Layouts;
			VkImage CurrentImage;
		} Target[MaxSimultaneousRenderTargets];

		FGenerateMipsInfo()
		{
			Reset();
		}

		void Reset()
		{
			NumRenderTargets = 0;
			bInsideGenerateMips = false;
			bLastMip = false;
			CurrentSlice = -1;
			CurrentMip = -1;
			for (int32 Index = 0; Index < MaxSimultaneousRenderTargets; ++Index)
			{
				Target[Index].Layouts.Reset(0);
				Target[Index].CurrentImage = VK_NULL_HANDLE;
			}
		}
	} GenerateMipsInfo;

	bool bInsideRealRenderPass = false;

	FVulkanRenderPass* CurrentRenderPass;
	FVulkanFramebuffer* CurrentFramebuffer;

	FCriticalSection RenderPassesCS;

	void NotifyDeletedRenderTarget(FVulkanDevice& InDevice, VkImage Image);

	inline void NotifyDeletedImage(VkImage Image)
	{
		Layouts.Remove(Image);
	}

	VkImageLayout FindLayoutChecked(VkImage Image) const
	{
		return Layouts.FindChecked(Image);
	}

	VkImageLayout FindOrAddLayout(VkImage Image, VkImageLayout LayoutIfNotFound)
	{
		VkImageLayout* Found = Layouts.Find(Image);
		if (Found)
		{
			return *Found;
		}

		Layouts.Add(Image, LayoutIfNotFound);
		return LayoutIfNotFound;
	}

	VkImageLayout& FindOrAddLayoutRW(VkImage Image, VkImageLayout LayoutIfNotFound)
	{
		VkImageLayout* Found = Layouts.Find(Image);
		if (Found)
		{
			return *Found;
		}

		return Layouts.Add(Image, LayoutIfNotFound);
	}

	void TransitionResource(FVulkanCmdBuffer* CmdBuffer, FVulkanSurface& Surface, VulkanRHI::EImageLayoutBarrier DestLayout);
};

class FVulkanCommandListContext : public IRHICommandContext
{
public:
	FVulkanCommandListContext(FVulkanDynamicRHI* InRHI, FVulkanDevice* InDevice, FVulkanQueue* InQueue, FVulkanCommandListContext* InImmediate);
	virtual ~FVulkanCommandListContext();

	inline bool IsImmediate() const
	{
		return Immediate == nullptr;
	}

	virtual void RHISetStreamSource(uint32 StreamIndex, FVertexBufferRHIParamRef VertexBuffer, uint32 Offset) final override;
	virtual void RHISetRasterizerState(FRasterizerStateRHIParamRef NewState) final override;
	virtual void RHISetViewport(uint32 MinX, uint32 MinY, float MinZ, uint32 MaxX, uint32 MaxY, float MaxZ) final override;
	virtual void RHISetScissorRect(bool bEnable, uint32 MinX, uint32 MinY, uint32 MaxX, uint32 MaxY) final override;
	virtual void RHISetBoundShaderState(FBoundShaderStateRHIParamRef BoundShaderState) final override;
	virtual void RHISetGraphicsPipelineState(FGraphicsPipelineStateRHIParamRef GraphicsState) final override;
	virtual void RHISetShaderTexture(FVertexShaderRHIParamRef VertexShader, uint32 TextureIndex, FTextureRHIParamRef NewTexture) final override;
	virtual void RHISetShaderTexture(FHullShaderRHIParamRef HullShader, uint32 TextureIndex, FTextureRHIParamRef NewTexture) final override;
	virtual void RHISetShaderTexture(FDomainShaderRHIParamRef DomainShader, uint32 TextureIndex, FTextureRHIParamRef NewTexture) final override;
	virtual void RHISetShaderTexture(FGeometryShaderRHIParamRef GeometryShader, uint32 TextureIndex, FTextureRHIParamRef NewTexture) final override;
	virtual void RHISetShaderTexture(FPixelShaderRHIParamRef PixelShader, uint32 TextureIndex, FTextureRHIParamRef NewTexture) final override;
	virtual void RHISetShaderTexture(FComputeShaderRHIParamRef PixelShader, uint32 TextureIndex, FTextureRHIParamRef NewTexture) final override;
	virtual void RHISetShaderSampler(FComputeShaderRHIParamRef ComputeShader, uint32 SamplerIndex, FSamplerStateRHIParamRef NewState) final override;
	virtual void RHISetShaderSampler(FVertexShaderRHIParamRef VertexShader, uint32 SamplerIndex, FSamplerStateRHIParamRef NewState) final override;
	virtual void RHISetShaderSampler(FGeometryShaderRHIParamRef GeometryShader, uint32 SamplerIndex, FSamplerStateRHIParamRef NewState) final override;
	virtual void RHISetShaderSampler(FDomainShaderRHIParamRef DomainShader, uint32 SamplerIndex, FSamplerStateRHIParamRef NewState) final override;
	virtual void RHISetShaderSampler(FHullShaderRHIParamRef HullShader, uint32 SamplerIndex, FSamplerStateRHIParamRef NewState) final override;
	virtual void RHISetShaderSampler(FPixelShaderRHIParamRef PixelShader, uint32 SamplerIndex, FSamplerStateRHIParamRef NewState) final override;
	virtual void RHISetUAVParameter(FComputeShaderRHIParamRef ComputeShader, uint32 UAVIndex, FUnorderedAccessViewRHIParamRef UAV) final override;
	virtual void RHISetUAVParameter(FComputeShaderRHIParamRef ComputeShader, uint32 UAVIndex, FUnorderedAccessViewRHIParamRef UAV, uint32 InitialCount) final override;
	virtual void RHISetShaderResourceViewParameter(FPixelShaderRHIParamRef PixelShader, uint32 SamplerIndex, FShaderResourceViewRHIParamRef SRV) final override;
	virtual void RHISetShaderResourceViewParameter(FVertexShaderRHIParamRef VertexShader, uint32 SamplerIndex, FShaderResourceViewRHIParamRef SRV) final override;
	virtual void RHISetShaderResourceViewParameter(FComputeShaderRHIParamRef ComputeShader, uint32 SamplerIndex, FShaderResourceViewRHIParamRef SRV) final override;
	virtual void RHISetShaderResourceViewParameter(FHullShaderRHIParamRef HullShader, uint32 SamplerIndex, FShaderResourceViewRHIParamRef SRV) final override;
	virtual void RHISetShaderResourceViewParameter(FDomainShaderRHIParamRef DomainShader, uint32 SamplerIndex, FShaderResourceViewRHIParamRef SRV) final override;
	virtual void RHISetShaderResourceViewParameter(FGeometryShaderRHIParamRef GeometryShader, uint32 SamplerIndex, FShaderResourceViewRHIParamRef SRV) final override;
	virtual void RHISetShaderUniformBuffer(FVertexShaderRHIParamRef VertexShader, uint32 BufferIndex, FUniformBufferRHIParamRef Buffer) final override;
	virtual void RHISetShaderUniformBuffer(FHullShaderRHIParamRef HullShader, uint32 BufferIndex, FUniformBufferRHIParamRef Buffer) final override;
	virtual void RHISetShaderUniformBuffer(FDomainShaderRHIParamRef DomainShader, uint32 BufferIndex, FUniformBufferRHIParamRef Buffer) final override;
	virtual void RHISetShaderUniformBuffer(FGeometryShaderRHIParamRef GeometryShader, uint32 BufferIndex, FUniformBufferRHIParamRef Buffer) final override;
	virtual void RHISetShaderUniformBuffer(FPixelShaderRHIParamRef PixelShader, uint32 BufferIndex, FUniformBufferRHIParamRef Buffer) final override;
	virtual void RHISetShaderUniformBuffer(FComputeShaderRHIParamRef ComputeShader, uint32 BufferIndex, FUniformBufferRHIParamRef Buffer) final override;
	virtual void RHISetShaderParameter(FVertexShaderRHIParamRef VertexShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override;
	virtual void RHISetShaderParameter(FHullShaderRHIParamRef HullShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override;
	virtual void RHISetShaderParameter(FDomainShaderRHIParamRef DomainShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override;
	virtual void RHISetShaderParameter(FGeometryShaderRHIParamRef GeometryShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override;
	virtual void RHISetShaderParameter(FPixelShaderRHIParamRef PixelShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override;
	virtual void RHISetShaderParameter(FComputeShaderRHIParamRef ComputeShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue) final override;
	virtual void RHISetDepthStencilState(FDepthStencilStateRHIParamRef NewState, uint32 StencilRef) final override;
	virtual void RHISetStencilRef(uint32 StencilRef) final override;
	virtual void RHISetBlendState(FBlendStateRHIParamRef NewState, const FLinearColor& BlendFactor) final override;
	virtual void RHISetRenderTargets(uint32 NumSimultaneousRenderTargets, const FRHIRenderTargetView* NewRenderTargets, const FRHIDepthRenderTargetView* NewDepthStencilTarget, uint32 NumUAVs, const FUnorderedAccessViewRHIParamRef* UAVs) final override;
	virtual void RHISetRenderTargetsAndClear(const FRHISetRenderTargetsInfo& RenderTargetsInfo) final override;
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
	virtual void RHIPushEvent(const TCHAR* Name, FColor Color) final override;
	virtual void RHIPopEvent() final override;

	virtual void RHISetComputeShader(FComputeShaderRHIParamRef ComputeShader) final override;
	virtual void RHISetComputePipelineState(FRHIComputePipelineState* ComputePipelineState) final override;
	virtual void RHIWaitComputeFence(FComputeFenceRHIParamRef InFence) final override;
	virtual void RHIDispatchComputeShader(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ) final override;
	virtual void RHIDispatchIndirectComputeShader(FVertexBufferRHIParamRef ArgumentBuffer, uint32 ArgumentOffset) final override;
	virtual void RHIAutomaticCacheFlushAfterComputeShader(bool bEnable) final override;

	virtual void RHIFlushComputeShaderCache() final override;
	virtual void RHISetMultipleViewports(uint32 Count, const FViewportBounds* Data) final override;
	virtual void RHIClearTinyUAV(FUnorderedAccessViewRHIParamRef UnorderedAccessViewRHI, const uint32* Values) final override;
	virtual void RHICopyToResolveTarget(FTextureRHIParamRef SourceTexture, FTextureRHIParamRef DestTexture, const FResolveParams& ResolveParams) final override;
	virtual void RHICopyTexture(FTextureRHIParamRef SourceTexture, FTextureRHIParamRef DestTexture, const FRHICopyTextureInfo& CopyInfo) final override;
	virtual void RHITransitionResources(EResourceTransitionAccess TransitionType, FTextureRHIParamRef* InRenderTargets, int32 NumTextures) final override;
	virtual void RHITransitionResources(EResourceTransitionAccess TransitionType, EResourceTransitionPipeline TransitionPipeline, FUnorderedAccessViewRHIParamRef* InUAVs, int32 NumUAVs, FComputeFenceRHIParamRef WriteComputeFence) final override;

	// Render time measurement
	virtual void RHIBeginRenderQuery(FRenderQueryRHIParamRef RenderQuery) final override;
	virtual void RHIEndRenderQuery(FRenderQueryRHIParamRef RenderQuery) final override;

	virtual void RHIUpdateTextureReference(FTextureReferenceRHIParamRef TextureRef, FTextureRHIParamRef NewTexture) final override;
#if VULKAN_USE_NEW_QUERIES
	void BeginOcclusionQueryBatch(FVulkanCmdBuffer* CmdBuffer, uint32 NumQueriesInBatch);
	void EndOcclusionQueryBatch(FVulkanCmdBuffer* CmdBuffer);
#else
	void RHIBeginOcclusionQueryBatch(uint32 NumQueriesInBatch);
	void RHIEndOcclusionQueryBatch();
#endif
	virtual void RHISubmitCommandsHint() final override;

	virtual void RHIBeginDrawingViewport(FViewportRHIParamRef Viewport, FTextureRHIParamRef RenderTargetRHI) final override;
	virtual void RHIEndDrawingViewport(FViewportRHIParamRef Viewport, bool bPresent, bool bLockToVsync) final override;

	virtual void RHIBeginFrame() final override;
	virtual void RHIEndFrame() final override;

	virtual void RHIBeginScene() final override;
	virtual void RHIEndScene() final override;

	virtual void RHIBeginRenderPass(const FRHIRenderPassInfo& InInfo, const TCHAR* InName) final override;
	virtual void RHIEndRenderPass() final override;

	inline FVulkanCommandBufferManager* GetCommandBufferManager()
	{
		return CommandBufferManager;
	}

	inline VulkanRHI::FTempFrameAllocationBuffer& GetTempFrameAllocationBuffer()
	{
		return TempFrameAllocationBuffer;
	}

	inline FVulkanPendingGfxState* GetPendingGfxState()
	{
		return PendingGfxState;
	}

	inline FVulkanPendingComputeState* GetPendingComputeState()
	{
		return PendingComputeState;
	}

	// OutSets must have been previously pre-allocated
	FVulkanDescriptorPool* AllocateDescriptorSets(const VkDescriptorSetAllocateInfo& DescriptorSetAllocateInfo, const FVulkanDescriptorSetsLayout& Layout, VkDescriptorSet* OutSets);

	inline void NotifyDeletedRenderTarget(VkImage Image)
	{
		TransitionAndLayoutManager.NotifyDeletedRenderTarget(*Device, Image);
	}

	inline void NotifyDeletedImage(VkImage Image)
	{
		TransitionAndLayoutManager.NotifyDeletedImage(Image);
	}

	inline FVulkanRenderPass* GetCurrentRenderPass()
	{
		return TransitionAndLayoutManager.CurrentRenderPass;
	}

	inline uint64 GetFrameCounter() const
	{
		return FrameCounter;
	}

	inline FVulkanUniformBufferUploader* GetUniformBufferUploader()
	{
		return UniformBufferUploader;
	}

	inline FVulkanQueue* GetQueue()
	{
		return Queue;
	}

	void WriteBeginTimestamp(FVulkanCmdBuffer* CmdBuffer);
	void WriteEndTimestamp(FVulkanCmdBuffer* CmdBuffer);

	void ReadAndCalculateGPUFrameTime();
	
	inline FVulkanGPUProfiler& GetGPUProfiler()
	{
		return GpuProfiler;
	}

	inline FVulkanDevice* GetDevice() const
	{
		return Device;
	}
	void EndRenderQueryInternal(FVulkanCmdBuffer* CmdBuffer, FVulkanRenderQuery* Query);

	inline VkImageLayout FindLayout(VkImage Image)
	{
		return TransitionAndLayoutManager.FindLayoutChecked(Image);
	}

	inline VkImageLayout GetLayoutForDescriptor(const FVulkanSurface& Surface) const
	{
		if (Surface.IsDepthOrStencilAspect())
		{
#if VULKAN_SUPPORTS_MAINTENANCE_LAYER2
			// If the spec gets lenient, we could remove this search since then 
			// Images in VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL_KHR could be used with 
			// descriptor write of VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
			if (Device->GetOptionalExtensions().HasKHRMaintenance2)
			{
				return TransitionAndLayoutManager.FindLayoutChecked(Surface.Image);
			}
#else
			return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
#endif
		}

		return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	inline VkImageLayout FindOrAddLayout(VkImage Image, VkImageLayout NewLayout)
	{
		return TransitionAndLayoutManager.FindOrAddLayout(Image, NewLayout);
	}

	inline VkImageLayout& FindOrAddLayoutRW(VkImage Image, VkImageLayout NewLayout)
	{
		return TransitionAndLayoutManager.FindOrAddLayoutRW(Image, NewLayout);
	}

	void PrepareParallelFromBase(const FVulkanCommandListContext& BaseContext);

protected:
	FVulkanDynamicRHI* RHI;
	FVulkanCommandListContext* Immediate;
	FVulkanDevice* Device;
	FVulkanQueue* Queue;
	bool bSubmitAtNextSafePoint;
	bool bAutomaticFlushAfterComputeShader;
	FVulkanUniformBufferUploader* UniformBufferUploader;

	void SetShaderUniformBuffer(DescriptorSet::EStage Stage, const FVulkanUniformBuffer* UniformBuffer, int32 BindingIndex, const FVulkanShader* Shader);

	struct
	{
		VulkanRHI::FTempFrameAllocationBuffer::FTempAllocInfo VertexAllocInfo;
		uint32 NumVertices = 0;
		uint32 VertexDataStride = 0;

		VulkanRHI::FTempFrameAllocationBuffer::FTempAllocInfo IndexAllocInfo;
		VkIndexType IndexType = VK_INDEX_TYPE_MAX_ENUM;
		uint32 PrimitiveType = 0;
		uint32 NumPrimitives = 0;
		uint32 MinVertexIndex = 0;
		uint32 IndexDataStride = 0;
	} UserPrimitive;


	VulkanRHI::FTempFrameAllocationBuffer TempFrameAllocationBuffer;

	TArray<FString> EventStack;

	FVulkanCommandBufferManager* CommandBufferManager;

#if VULKAN_USE_DESCRIPTOR_POOL_MANAGER
	typedef TArray<FVulkanDescriptorPool*> FDescriptorPoolArray;
	TMap<uint32, FDescriptorPoolArray> DescriptorPools;
#else
	TArray<FVulkanDescriptorPool*> DescriptorPools;
#endif

	FTransitionAndLayoutManager TransitionAndLayoutManager;


	struct FPendingTransition
	{
		EResourceTransitionAccess TransitionType;

		// Only one of Textures or UAVs is active at a time
		TArray<FRHITexture*> Textures;

		TArray<FRHIUnorderedAccessView*> UAVs;
		FRHIComputeFence* WriteComputeFenceRHI;
		EResourceTransitionPipeline TransitionPipeline;

		bool GatherBarriers(FTransitionAndLayoutManager& TransitionAndLayoutManager, TArray<VkBufferMemoryBarrier>& OutBufferBarriers, 
			TArray<VkImageMemoryBarrier>& OutImageBarriers) const;
	};

	void TransitionResources(const FPendingTransition& PendingTransition);
	static void TransitionUAVResourcesTransferringOwnership(FVulkanCommandListContext& GfxContext, FVulkanCommandListContext& ComputeContext, 
		EResourceTransitionPipeline Pipeline, const TArray<VkBufferMemoryBarrier>& BufferBarriers, const TArray<VkImageMemoryBarrier>& ImageBarriers);

#if VULKAN_USE_NEW_QUERIES
	FVulkanOcclusionQueryPool* CurrentOcclusionQueryPool = nullptr;
#else
	struct FOcclusionQueryData
	{
		FVulkanCmdBuffer* CmdBuffer;
		uint64 FenceCounter;

		FOcclusionQueryData()
			: CmdBuffer(nullptr)
			, FenceCounter(0)
		{
		}

		void AddToResetList(FVulkanQueryPool* Pool, int32 QueryIndex)
		{
			TArray<uint64>& ListPerPool = ResetList.FindOrAdd(Pool);
			int32 Word = QueryIndex / 64;
			uint64 Bit = QueryIndex % 64;
			uint64 BitMask = (uint64)1 << Bit;
			if (Word >= ListPerPool.Num())
			{
				ListPerPool.AddZeroed(Word - ListPerPool.Num() + 1);
			}
			ListPerPool[Word] = ListPerPool[Word] | BitMask;
		}

		void ResetQueries(FVulkanCmdBuffer* CmdBuffer);

		void ClearResetList()
		{
			for (auto& Pair : ResetList)
			{
				FMemory::Memzero(&Pair.Value[0], Pair.Value.Num() * sizeof(uint64));
			}
		}

		TMap<FVulkanQueryPool*, TArray<uint64>> ResetList;
	};
	FOcclusionQueryData CurrentOcclusionQueryData;
	void AdvanceQuery(FVulkanRenderQuery* Query);
#endif

	// List of UAVs which need setting for pixel shaders. D3D treats UAVs like rendertargets so the RHI doesn't make SetUAV calls at the right time
	struct FPendingPixelUAV
	{
		FVulkanUnorderedAccessView* UAV;
		uint32 BindIndex;
	};
	TArray<FPendingPixelUAV> PendingPixelUAVs;

	FVulkanPendingGfxState* PendingGfxState;
	FVulkanPendingComputeState* PendingComputeState;

	void PrepareForCPURead();
	void RequestSubmitCurrentCommands();

	void InternalClearMRT(FVulkanCmdBuffer* CmdBuffer, bool bClearColor, int32 NumClearColors, const FLinearColor* ColorArray, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil);

public:
	inline FTransitionAndLayoutManager& GetTransitionAndLayoutManager()
	{
		return TransitionAndLayoutManager;
	}

	FVulkanRenderPass* PrepareRenderPassForPSOCreation(const FGraphicsPipelineStateInitializer& Initializer);
	FVulkanRenderPass* PrepareRenderPassForPSOCreation(const FVulkanRenderTargetLayout& Initializer);

private:
	void RHIClearMRT(bool bClearColor, int32 NumClearColors, const FLinearColor* ColorArray, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil);

	inline bool SafePointSubmit()
	{
		if (bSubmitAtNextSafePoint)
		{
			InternalSubmitActiveCmdBuffer();
			bSubmitAtNextSafePoint = false;
			return true;
		}

		return false;
	}

	void InternalSubmitActiveCmdBuffer();
	void FlushAfterComputeShader();

	friend class FVulkanDevice;
	friend class FVulkanDynamicRHI;

	// Number of times EndFrame() has been called on this context
	uint64 FrameCounter;

	FVulkanGPUProfiler GpuProfiler;
	FVulkanGPUTiming* FrameTiming;

	friend struct FVulkanCommandContextContainer;
};

class FVulkanCommandListContextImmediate : public FVulkanCommandListContext
{
public:
	FVulkanCommandListContextImmediate(FVulkanDynamicRHI* InRHI, FVulkanDevice* InDevice, FVulkanQueue* InQueue);
};


struct FVulkanCommandContextContainer : public IRHICommandContextContainer, public VulkanRHI::FDeviceChild
{
	FVulkanCommandListContext* CmdContext;

	FVulkanCommandContextContainer(FVulkanDevice* InDevice);

	virtual IRHICommandContext* GetContext() override final;
	virtual void FinishContext() override final;
	virtual void SubmitAndFreeContextContainer(int32 Index, int32 Num) override final;

	/** Custom new/delete with recycling */
	void* operator new(size_t Size);
	void operator delete(void* RawMemory);

private:
	friend class FVulkanDevice;
};

inline FVulkanCommandListContextImmediate& FVulkanDevice::GetImmediateContext()
{
	return *ImmediateContext;
}
