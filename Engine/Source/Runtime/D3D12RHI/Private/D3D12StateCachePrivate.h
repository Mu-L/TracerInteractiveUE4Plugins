// Copyright Epic Games, Inc. All Rights Reserved.

// Implementation of Device Context State Caching to improve draw
//	thread performance by removing redundant device context calls.

#pragma once
#include "D3D12DirectCommandListManager.h"

//-----------------------------------------------------------------------------
//	Configuration
//-----------------------------------------------------------------------------

// If set, includes a runtime toggle console command for debugging D3D12  state caching.
// ("TOGGLESTATECACHE")
#define D3D12_STATE_CACHE_RUNTIME_TOGGLE 0

// If set, includes a cache state verification check.
// After each state set call, the cached state is compared against the actual state.
// This is *very slow* and should only be enabled to debug the state caching system.
#ifndef D3D12_STATE_CACHE_DEBUG
#define D3D12_STATE_CACHE_DEBUG 0
#endif

// Uncomment only for debugging of the descriptor heap management; this is very noisy
//#define VERBOSE_DESCRIPTOR_HEAP_DEBUG 1

// The number of view descriptors available per (online) descriptor heap, depending on hardware tier
#define NUM_SAMPLER_DESCRIPTORS D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE
#define DESCRIPTOR_HEAP_BLOCK_SIZE 10000

#define NUM_VIEW_DESCRIPTORS_TIER_1 D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1
#define NUM_VIEW_DESCRIPTORS_TIER_2 D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_2
// Only some tier 3 hardware can use > 1 million descriptors in a heap, the only way to tell if hardware can
// is to try and create a heap and check for failure. Unless we really want > 1 million Descriptors we'll cap
// out at 1M for now.
#define NUM_VIEW_DESCRIPTORS_TIER_3 D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_2

// Heap for updating UAV counter values.
#define COUNTER_HEAP_SIZE 1024 * 64

// Keep set state functions inline to reduce call overhead
#define D3D12_STATE_CACHE_INLINE FORCEINLINE

#if D3D12_STATE_CACHE_RUNTIME_TOGGLE
extern bool GD3D12SkipStateCaching;
#else
static const bool GD3D12SkipStateCaching = false;
#endif

extern int32 GGlobalViewHeapSize;


enum ED3D12PipelineType
{
	D3D12PT_Graphics,
	D3D12PT_Compute,
	D3D12PT_RayTracing,
};


#define MAX_VBS			D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT

typedef uint32 VBSlotMask;
static_assert((8 * sizeof(VBSlotMask)) >= MAX_VBS, "VBSlotMask isn't large enough to cover all VBs. Please increase the size.");

struct FD3D12VertexBufferCache
{
	FD3D12VertexBufferCache()
	{
		Clear();
	};

	inline void Clear()
	{
		FMemory::Memzero(CurrentVertexBufferViews, sizeof(CurrentVertexBufferViews));
		FMemory::Memzero(CurrentVertexBufferResources, sizeof(CurrentVertexBufferResources));
		FMemory::Memzero(ResidencyHandles, sizeof(ResidencyHandles));
		MaxBoundVertexBufferIndex = INDEX_NONE;
		BoundVBMask = 0;
	}

	D3D12_VERTEX_BUFFER_VIEW CurrentVertexBufferViews[MAX_VBS];
	FD3D12ResourceLocation* CurrentVertexBufferResources[MAX_VBS];
	FD3D12ResidencyHandle* ResidencyHandles[MAX_VBS];
	int32 MaxBoundVertexBufferIndex;
	VBSlotMask BoundVBMask;
};

struct FD3D12IndexBufferCache
{
	FD3D12IndexBufferCache()
	{
		Clear();
	}

	inline void Clear()
	{
		FMemory::Memzero(&CurrentIndexBufferView, sizeof(CurrentIndexBufferView));
	}

	D3D12_INDEX_BUFFER_VIEW CurrentIndexBufferView;
};

template<typename ResourceSlotMask>
struct FD3D12ResourceCache
{
	static inline void CleanSlot(ResourceSlotMask& SlotMask, uint32 SlotIndex)
	{
		SlotMask &= ~((ResourceSlotMask)1 << SlotIndex);
	}

	static inline void CleanSlots(ResourceSlotMask& SlotMask, uint32 NumSlots)
	{
		SlotMask &= ~(((ResourceSlotMask)1 << NumSlots) - 1);
	}

	static inline void DirtySlot(ResourceSlotMask& SlotMask, uint32 SlotIndex)
	{
		SlotMask |= ((ResourceSlotMask)1 << SlotIndex);
	}

	static inline bool IsSlotDirty(const ResourceSlotMask& SlotMask, uint32 SlotIndex)
	{
		return (SlotMask & ((ResourceSlotMask)1 << SlotIndex)) != 0;
	}

	// Mark a specific shader stage as dirty.
	inline void Dirty(EShaderFrequency ShaderFrequency, const ResourceSlotMask& SlotMask = -1)
	{
		checkSlow(ShaderFrequency < UE_ARRAY_COUNT(DirtySlotMask));
		DirtySlotMask[ShaderFrequency] |= SlotMask;
	}

	// Mark specified bind slots, on all graphics stages, as dirty.
	inline void DirtyGraphics(const ResourceSlotMask& SlotMask = -1)
	{
		Dirty(SF_Vertex, SlotMask);
		Dirty(SF_Hull, SlotMask);
		Dirty(SF_Domain, SlotMask);
		Dirty(SF_Pixel, SlotMask);
		Dirty(SF_Geometry, SlotMask);
	}

	// Mark specified bind slots on compute as dirty.
	inline void DirtyCompute(const ResourceSlotMask& SlotMask = -1)
	{
		Dirty(SF_Compute, SlotMask);
	}

	// Mark specified bind slots on graphics and compute as dirty.
	inline void DirtyAll(const ResourceSlotMask& SlotMask = -1)
	{
		DirtyGraphics(SlotMask);
		DirtyCompute(SlotMask);
	}

	ResourceSlotMask DirtySlotMask[SF_NumStandardFrequencies];
};

struct FD3D12ConstantBufferCache : public FD3D12ResourceCache<CBVSlotMask>
{
	FD3D12ConstantBufferCache()
	{
		Clear();
	}

	inline void Clear()
	{
		DirtyAll();

		FMemory::Memzero(CurrentGPUVirtualAddress, sizeof(CurrentGPUVirtualAddress));
		FMemory::Memzero(ResidencyHandles, sizeof(ResidencyHandles));
#if USE_STATIC_ROOT_SIGNATURE
		FMemory::Memzero(CBHandles, sizeof(CBHandles));
#endif
	}

#if USE_STATIC_ROOT_SIGNATURE
	D3D12_CPU_DESCRIPTOR_HANDLE CBHandles[SF_NumStandardFrequencies][MAX_CBS];
#endif
	D3D12_GPU_VIRTUAL_ADDRESS CurrentGPUVirtualAddress[SF_NumStandardFrequencies][MAX_CBS];
	FD3D12ResidencyHandle* ResidencyHandles[SF_NumStandardFrequencies][MAX_CBS];
};

struct FD3D12ShaderResourceViewCache : public FD3D12ResourceCache<SRVSlotMask>
{
	FD3D12ShaderResourceViewCache()
	{
		Clear();
	}

	inline void Clear()
	{
		DirtyAll();

		FMemory::Memzero(ResidencyHandles);
		FMemory::Memzero(BoundMask);
		
		for (int32& Index : MaxBoundIndex)
		{
			Index = INDEX_NONE;
		}

		for (int32 FrequencyIdx = 0; FrequencyIdx < SF_NumStandardFrequencies; ++FrequencyIdx)
		{
			for (int32 SRVIdx = 0; SRVIdx < MAX_SRVS; ++SRVIdx)
			{
				Views[FrequencyIdx][SRVIdx].SafeRelease();
			}
		}
	}

	TRefCountPtr<FD3D12ShaderResourceView> Views[SF_NumStandardFrequencies][MAX_SRVS];
	FD3D12ResidencyHandle* ResidencyHandles[SF_NumStandardFrequencies][MAX_SRVS];

	SRVSlotMask BoundMask[SF_NumStandardFrequencies];
	int32 MaxBoundIndex[SF_NumStandardFrequencies];
};

struct FD3D12UnorderedAccessViewCache : public FD3D12ResourceCache<UAVSlotMask>
{
	FD3D12UnorderedAccessViewCache()
	{
		Clear();
	}

	inline void Clear()
	{
		DirtyAll();

		FMemory::Memzero(Views);
		FMemory::Memzero(ResidencyHandles);

		for (uint32& Index : StartSlot)
		{
			Index = INDEX_NONE;
		}
	}

	FD3D12UnorderedAccessView* Views[SF_NumStandardFrequencies][MAX_UAVS];
	FD3D12ResidencyHandle* ResidencyHandles[SF_NumStandardFrequencies][MAX_UAVS];
	uint32 StartSlot[SF_NumStandardFrequencies];
};

struct FD3D12SamplerStateCache : public FD3D12ResourceCache<SamplerSlotMask>
{
	FD3D12SamplerStateCache()
	{
		Clear();
	}

	inline void Clear()
	{
		DirtyAll();

		FMemory::Memzero(States);
	}

	FD3D12SamplerState* States[SF_NumStandardFrequencies][MAX_SAMPLERS];
};


static inline D3D_PRIMITIVE_TOPOLOGY GetD3D12PrimitiveType(uint32 PrimitiveType, bool bUsingTessellation)
{
	static const uint8 D3D12PrimitiveType[] =
	{
		D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,               // PT_TriangleList
		D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,              // PT_TriangleStrip
		D3D_PRIMITIVE_TOPOLOGY_LINELIST,                   // PT_LineList
		0,                                                 // PT_QuadList
		D3D_PRIMITIVE_TOPOLOGY_POINTLIST,                  // PT_PointList
#if defined(D3D12RHI_PRIMITIVE_TOPOLOGY_RECTLIST)          // PT_RectList
		D3D_PRIMITIVE_TOPOLOGY_RECTLIST,
#else
		0,
#endif
		D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST,  // PT_1_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST,  // PT_2_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST,  // PT_3_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST,  // PT_4_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_5_CONTROL_POINT_PATCHLIST,  // PT_5_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_6_CONTROL_POINT_PATCHLIST,  // PT_6_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_7_CONTROL_POINT_PATCHLIST,  // PT_7_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_8_CONTROL_POINT_PATCHLIST,  // PT_8_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_9_CONTROL_POINT_PATCHLIST,  // PT_9_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_10_CONTROL_POINT_PATCHLIST, // PT_10_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_11_CONTROL_POINT_PATCHLIST, // PT_11_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_12_CONTROL_POINT_PATCHLIST, // PT_12_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_13_CONTROL_POINT_PATCHLIST, // PT_13_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_14_CONTROL_POINT_PATCHLIST, // PT_14_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_15_CONTROL_POINT_PATCHLIST, // PT_15_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_16_CONTROL_POINT_PATCHLIST, // PT_16_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_17_CONTROL_POINT_PATCHLIST, // PT_17_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_18_CONTROL_POINT_PATCHLIST, // PT_18_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_19_CONTROL_POINT_PATCHLIST, // PT_19_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_20_CONTROL_POINT_PATCHLIST, // PT_20_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_21_CONTROL_POINT_PATCHLIST, // PT_21_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_22_CONTROL_POINT_PATCHLIST, // PT_22_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_23_CONTROL_POINT_PATCHLIST, // PT_23_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_24_CONTROL_POINT_PATCHLIST, // PT_24_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_25_CONTROL_POINT_PATCHLIST, // PT_25_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_26_CONTROL_POINT_PATCHLIST, // PT_26_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_27_CONTROL_POINT_PATCHLIST, // PT_27_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_28_CONTROL_POINT_PATCHLIST, // PT_28_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_29_CONTROL_POINT_PATCHLIST, // PT_29_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_30_CONTROL_POINT_PATCHLIST, // PT_30_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_31_CONTROL_POINT_PATCHLIST, // PT_31_ControlPointPatchList
		D3D_PRIMITIVE_TOPOLOGY_32_CONTROL_POINT_PATCHLIST, // PT_32_ControlPointPatchList
	};
	static_assert(UE_ARRAY_COUNT(D3D12PrimitiveType) == PT_Num, "Primitive lookup table is wrong size");

	if (bUsingTessellation)
	{
		if (PrimitiveType == PT_TriangleList)
		{
			// This is the case for tessellation without AEN or other buffers, so just flip to 3 CPs
			return D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
		}
		else/* if (PrimitiveType < PT_1_ControlPointPatchList)*/
		{
			checkf(PrimitiveType >= PT_1_ControlPointPatchList, TEXT("Invalid type specified for tessellated render, probably missing a case in FSkeletalMeshSceneProxy::DrawDynamicElementsByMaterial or FStaticMeshSceneProxy::GetMeshElement"));
		}
	}

	D3D_PRIMITIVE_TOPOLOGY D3DType = (D3D_PRIMITIVE_TOPOLOGY) D3D12PrimitiveType[PrimitiveType];
	checkf(D3DType, TEXT("Unknown primitive type: %u"), PrimitiveType);
	return D3DType;
}

//-----------------------------------------------------------------------------
//	FD3D12StateCache Class Definition
//-----------------------------------------------------------------------------
class FD3D12StateCacheBase : public FD3D12DeviceChild, public FD3D12SingleNodeGPUObject
{
	friend class FD3D12DynamicRHI;

protected:
	FD3D12CommandContext* CmdContext;

	bool bNeedSetVB;
	bool bNeedSetRTs;
	bool bNeedSetSOs;
	bool bSRVSCleared;
	bool bNeedSetViewports;
	bool bNeedSetScissorRects;
	bool bNeedSetPrimitiveTopology;
	bool bNeedSetBlendFactor;
	bool bNeedSetStencilRef;
	bool bNeedSetDepthBounds;
	bool bAutoFlushComputeShaderCache;
	D3D12_RESOURCE_BINDING_TIER ResourceBindingTier;

	struct
	{
		struct
		{
			// Cache
			TRefCountPtr<FD3D12GraphicsPipelineState> CurrentPipelineStateObject;

			// Note: Current root signature is part of the bound shader state, which is part of the PSO
			bool bNeedSetRootSignature;

			// Depth Stencil State Cache
			uint32 CurrentReferenceStencil;

			// Blend State Cache
			float CurrentBlendFactor[4];

			// Viewport
			uint32	CurrentNumberOfViewports;
			D3D12_VIEWPORT CurrentViewport[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];

			// Vertex Buffer State
			FD3D12VertexBufferCache VBCache;

			// SO
			uint32			CurrentNumberOfStreamOutTargets;
			FD3D12Resource* CurrentStreamOutTargets[D3D12_SO_STREAM_COUNT];
			uint32			CurrentSOOffsets[D3D12_SO_STREAM_COUNT];

			// Index Buffer State
			FD3D12IndexBufferCache IBCache;

			// Primitive Topology State
			EPrimitiveType CurrentPrimitiveType;
			D3D_PRIMITIVE_TOPOLOGY CurrentPrimitiveTopology;
			uint32 PrimitiveTypeFactor;
			uint32 PrimitiveTypeOffset;
			uint32* CurrentPrimitiveStat;
			uint32 NumTriangles;
			uint32 NumLines;

			// Input Layout State
			D3D12_RECT CurrentScissorRects[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
			uint32 CurrentNumberOfScissorRects;

			uint16 StreamStrides[MaxVertexElementCount];

			FD3D12RenderTargetView* RenderTargetArray[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
			uint32 CurrentNumberOfRenderTargets;

			FD3D12DepthStencilView* CurrentDepthStencilTarget;

			float MinDepth;
			float MaxDepth;
		} Graphics;

		struct
		{
			// Cache
			TRefCountPtr<FD3D12ComputePipelineState> CurrentPipelineStateObject;

			// Note: Current root signature is part of the bound compute shader, which is part of the PSO
			bool bNeedSetRootSignature;

			// Need to cache compute budget, as we need to reset if after PSO changes
			EAsyncComputeBudget ComputeBudget;
		} Compute;

		struct
		{
			FD3D12ShaderResourceViewCache SRVCache;
			FD3D12ConstantBufferCache CBVCache;
			FD3D12UnorderedAccessViewCache UAVCache;
			FD3D12SamplerStateCache SamplerCache;

			// PSO
			ID3D12PipelineState* CurrentPipelineStateObject;
			bool bNeedSetPSO;

			uint32 CurrentShaderSamplerCounts[SF_NumStandardFrequencies];
			uint32 CurrentShaderSRVCounts[SF_NumStandardFrequencies];
			uint32 CurrentShaderCBCounts[SF_NumStandardFrequencies];
			uint32 CurrentShaderUAVCounts[SF_NumStandardFrequencies];
		} Common;
	} PipelineState;

	FD3D12DescriptorCache DescriptorCache;

	void InternalSetIndexBuffer(FD3D12Resource* Resource);

	void InternalSetStreamSource(FD3D12ResourceLocation* VertexBufferLocation, uint32 StreamIndex, uint32 Stride, uint32 Offset);

	template <typename TShader> struct StateCacheShaderTraits;
#define DECLARE_SHADER_TRAITS(Name) \
	template <> struct StateCacheShaderTraits<FD3D12##Name##Shader> \
	{ \
		static const EShaderFrequency Frequency = SF_##Name; \
		static FD3D12##Name##Shader* GetShader(FD3D12BoundShaderState* BSS) { return BSS ? BSS->Get##Name##Shader() : nullptr; } \
		static FD3D12##Name##Shader* GetShader(FD3D12GraphicsPipelineState* PSO) { return PSO ? (FD3D12##Name##Shader*)PSO->PipelineStateInitializer.BoundShaderState.##Name##ShaderRHI : nullptr; } \
	}
	DECLARE_SHADER_TRAITS(Vertex);
	DECLARE_SHADER_TRAITS(Pixel);
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	DECLARE_SHADER_TRAITS(Domain);
	DECLARE_SHADER_TRAITS(Hull);
#endif
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
	DECLARE_SHADER_TRAITS(Geometry);
#endif
#undef DECLARE_SHADER_TRAITS

	template <typename TShader> D3D12_STATE_CACHE_INLINE void SetShader(TShader* Shader)
	{
		typedef StateCacheShaderTraits<TShader> Traits;
		TShader* OldShader = Traits::GetShader(GetGraphicsPipelineState());

		if (OldShader != Shader)
		{
			PipelineState.Common.CurrentShaderSamplerCounts[Traits::Frequency] = (Shader) ? Shader->ResourceCounts.NumSamplers : 0;
			PipelineState.Common.CurrentShaderSRVCounts[Traits::Frequency]     = (Shader) ? Shader->ResourceCounts.NumSRVs     : 0;
			PipelineState.Common.CurrentShaderCBCounts[Traits::Frequency]      = (Shader) ? Shader->ResourceCounts.NumCBs      : 0;
			PipelineState.Common.CurrentShaderUAVCounts[Traits::Frequency]     = (Shader) ? Shader->ResourceCounts.NumUAVs     : 0;
		
			// Shader changed so its resource table is dirty
			SetDirtyUniformBuffers(this->CmdContext, Traits::Frequency);
		}
	}

	template <typename TShader> D3D12_STATE_CACHE_INLINE void GetShader(TShader** Shader)
	{
		*Shader = StateCacheShaderTraits<TShader>::GetShader(GetGraphicsPipelineState());
	}

	template <ED3D12PipelineType PipelineType>
	D3D12_STATE_CACHE_INLINE void InternalSetPipelineState()
	{
		static_assert(PipelineType != D3D12PT_RayTracing, "FD3D12StateCacheBase is not expected to be used with ray tracing.");

		// See if we need to set our PSO:
		// In D3D11, you could Set dispatch arguments, then set Draw arguments, then call Draw/Dispatch/Draw/Dispatch without setting arguments again.
		// In D3D12, we need to understand when the app switches between Draw/Dispatch and make sure the correct PSO is set.

		bool bNeedSetPSO = PipelineState.Common.bNeedSetPSO;
		ID3D12PipelineState*& CurrentPSO = PipelineState.Common.CurrentPipelineStateObject;
		ID3D12PipelineState* const RequiredPSO = (PipelineType == D3D12PT_Compute) 
			? PipelineState.Compute.CurrentPipelineStateObject->PipelineState->GetPipelineState() 
			: PipelineState.Graphics.CurrentPipelineStateObject->PipelineState->GetPipelineState();

		if (CurrentPSO != RequiredPSO)
		{
			CurrentPSO = RequiredPSO;
			bNeedSetPSO = true;
		}

		// Set the PSO on the command list if necessary.
		if (bNeedSetPSO)
		{
			check(CurrentPSO);
			SetPipelineState(this->CmdContext, CurrentPSO);
			PipelineState.Common.bNeedSetPSO = false;
		}
	}

private:

	// SetDirtyUniformBuffers and SetPipelineState helper functions are required
	// to allow using FD3D12CommandContext type which is not defined at this point.
	// Making ContextType a template parameter delays instantiation of these functions.

	template <typename ContextType>
	static void SetDirtyUniformBuffers(ContextType* Context, EShaderFrequency Frequency)
	{
		Context->DirtyUniformBuffers[Frequency] = 0xffff;
	}

	template <typename ContextType>
	static void SetPipelineState(ContextType* Context, ID3D12PipelineState* State)
	{
		Context->CommandListHandle->SetPipelineState(State);
	}

public:

	void InheritState(const FD3D12StateCacheBase& AncestralCache)
	{
		FMemory::Memcpy(&PipelineState, &AncestralCache.PipelineState, sizeof(PipelineState));
		DirtyState();
	}

	FD3D12DescriptorCache* GetDescriptorCache()
	{
		return &DescriptorCache;
	}

	FD3D12GraphicsPipelineState* GetGraphicsPipelineState() const
	{
		return PipelineState.Graphics.CurrentPipelineStateObject;
	}

	const FD3D12RootSignature* GetGraphicsRootSignature() const
	{
		return PipelineState.Graphics.CurrentPipelineStateObject ? PipelineState.Graphics.CurrentPipelineStateObject->RootSignature : nullptr;
	}

	inline EPrimitiveType GetGraphicsPipelinePrimitiveType() const
	{
		return PipelineState.Graphics.CurrentPrimitiveType;
	}

	inline uint32 GetVertexCountAndIncrementStat(uint32 NumPrimitives)
	{
		*PipelineState.Graphics.CurrentPrimitiveStat += NumPrimitives;
		return PipelineState.Graphics.PrimitiveTypeFactor * NumPrimitives + PipelineState.Graphics.PrimitiveTypeOffset;
	}

	inline uint32 GetNumTrianglesStat() const { return PipelineState.Graphics.NumTriangles; }
	inline uint32 GetNumLinesStat() const { return PipelineState.Graphics.NumLines; }

	const FD3D12RootSignature* GetComputeRootSignature() const
	{
		return PipelineState.Compute.CurrentPipelineStateObject ? PipelineState.Compute.CurrentPipelineStateObject->ComputeShader->pRootSignature : nullptr;
	}

	void ClearSRVs();

	template <EShaderFrequency ShaderFrequency>
	void ClearShaderResourceViews(FD3D12ResourceLocation*& ResourceLocation)
	{
		//SCOPE_CYCLE_COUNTER(STAT_D3D12ClearShaderResourceViewsTime);

		if (PipelineState.Common.SRVCache.MaxBoundIndex[ShaderFrequency] < 0)
		{
			return;
		}

		auto& CurrentShaderResourceViews = PipelineState.Common.SRVCache.Views[ShaderFrequency];
		for (int32 i = 0; i <= PipelineState.Common.SRVCache.MaxBoundIndex[ShaderFrequency]; ++i)
		{
			if (CurrentShaderResourceViews[i] && CurrentShaderResourceViews[i]->GetResourceLocation() == ResourceLocation)
			{
				SetShaderResourceView<ShaderFrequency>(nullptr, i);
			}
		}
	}

	template <EShaderFrequency ShaderFrequency>
	void SetShaderResourceView(FD3D12ShaderResourceView* SRV, uint32 ResourceIndex);
	
	void SetScissorRects(uint32 Count, const D3D12_RECT* const ScissorRects);
	void SetScissorRect(const D3D12_RECT& ScissorRect);

	D3D12_STATE_CACHE_INLINE const D3D12_RECT& GetScissorRect(int32 Index = 0) const
	{
		return PipelineState.Graphics.CurrentScissorRects[Index];
	}

	void SetViewport(const D3D12_VIEWPORT& Viewport);
	void SetViewports(uint32 Count, const D3D12_VIEWPORT* const Viewports);

	D3D12_STATE_CACHE_INLINE uint32 GetNumViewports() const
	{
		return PipelineState.Graphics.CurrentNumberOfViewports;
	}

	D3D12_STATE_CACHE_INLINE const D3D12_VIEWPORT& GetViewport(int32 Index = 0) const
	{
		return PipelineState.Graphics.CurrentViewport[Index];
	}

	D3D12_STATE_CACHE_INLINE void GetViewports(uint32* Count, D3D12_VIEWPORT* Viewports) const
	{
		check(*Count);
		if (Viewports) //NULL is legal if you just want count
		{
			//as per d3d spec
			const int32 StorageSizeCount = (int32)(*Count);
			const int32 CopyCount = FMath::Min(FMath::Min(StorageSizeCount, (int32)PipelineState.Graphics.CurrentNumberOfViewports), D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
			if (CopyCount > 0)
			{
				FMemory::Memcpy(Viewports, &PipelineState.Graphics.CurrentViewport[0], sizeof(D3D12_VIEWPORT) * CopyCount);
			}
			//remaining viewports in supplied array must be set to zero
			if (StorageSizeCount > CopyCount)
			{
				FMemory::Memset(&Viewports[CopyCount], 0, sizeof(D3D12_VIEWPORT) * (StorageSizeCount - CopyCount));
			}
		}
		*Count = PipelineState.Graphics.CurrentNumberOfViewports;
	}

	template <EShaderFrequency ShaderFrequency>
	D3D12_STATE_CACHE_INLINE void SetSamplerState(FD3D12SamplerState* SamplerState, uint32 SamplerIndex)
	{
		check(SamplerIndex < MAX_SAMPLERS);
		auto& Samplers = PipelineState.Common.SamplerCache.States[ShaderFrequency];
		if ((Samplers[SamplerIndex] != SamplerState) || GD3D12SkipStateCaching)
		{
			Samplers[SamplerIndex] = SamplerState;
			FD3D12SamplerStateCache::DirtySlot(PipelineState.Common.SamplerCache.DirtySlotMask[ShaderFrequency], SamplerIndex);
		}
	}

	template <EShaderFrequency ShaderFrequency>
	D3D12_STATE_CACHE_INLINE void GetSamplerState(uint32 StartSamplerIndex, uint32 NumSamplerIndexes, FD3D12SamplerState** SamplerStates) const
	{
		check(StartSamplerIndex + NumSamplerIndexes <= D3D12_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
		auto& CurrentShaderResourceViews = PipelineState.Common.SRVCache.Views[ShaderFrequency];
		for (uint32 StateLoop = 0; StateLoop < NumSamplerIndexes; StateLoop++)
		{
			SamplerStates[StateLoop] = CurrentShaderResourceViews[StateLoop + StartSamplerIndex];
			if (SamplerStates[StateLoop])
			{
				SamplerStates[StateLoop]->AddRef();
			}
		}
	}

	template <EShaderFrequency ShaderFrequency>
	void D3D12_STATE_CACHE_INLINE SetConstantsFromUniformBuffer(uint32 SlotIndex, FD3D12UniformBuffer* UniformBuffer)
	{
		check(SlotIndex < MAX_CBS);
		FD3D12ConstantBufferCache& CBVCache = PipelineState.Common.CBVCache;
		D3D12_GPU_VIRTUAL_ADDRESS& CurrentGPUVirtualAddress = CBVCache.CurrentGPUVirtualAddress[ShaderFrequency][SlotIndex];

		if (UniformBuffer && UniformBuffer->ResourceLocation.GetGPUVirtualAddress())
		{
			const FD3D12ResourceLocation& ResourceLocation = UniformBuffer->ResourceLocation;
			// Only update the constant buffer if it has changed.
			if (ResourceLocation.GetGPUVirtualAddress() != CurrentGPUVirtualAddress)
			{
				CurrentGPUVirtualAddress = ResourceLocation.GetGPUVirtualAddress();
				CBVCache.ResidencyHandles[ShaderFrequency][SlotIndex] = ResourceLocation.GetResource()->GetResidencyHandle();
				FD3D12ConstantBufferCache::DirtySlot(CBVCache.DirtySlotMask[ShaderFrequency], SlotIndex);
			}

#if USE_STATIC_ROOT_SIGNATURE
			CBVCache.CBHandles[ShaderFrequency][SlotIndex] = UniformBuffer->View->OfflineDescriptorHandle;
#endif
		}
		else if (CurrentGPUVirtualAddress != 0)
		{
			CurrentGPUVirtualAddress = 0;
			CBVCache.ResidencyHandles[ShaderFrequency][SlotIndex] = nullptr;
			FD3D12ConstantBufferCache::DirtySlot(CBVCache.DirtySlotMask[ShaderFrequency], SlotIndex);
#if USE_STATIC_ROOT_SIGNATURE
			CBVCache.CBHandles[ShaderFrequency][SlotIndex].ptr = 0;
#endif
		}
		else
		{
#if USE_STATIC_ROOT_SIGNATURE
			CBVCache.CBHandles[ShaderFrequency][SlotIndex].ptr = 0;
#endif
		}
	}

	template <EShaderFrequency ShaderFrequency>
	void D3D12_STATE_CACHE_INLINE SetConstantBuffer(FD3D12ConstantBuffer& Buffer, bool bDiscardSharedConstants)
	{
		FD3D12ResourceLocation Location(GetParentDevice());

		if (Buffer.Version(Location, bDiscardSharedConstants))
		{
			// Note: Code assumes the slot index is always 0.
			const uint32 SlotIndex = 0;

			FD3D12ConstantBufferCache& CBVCache = PipelineState.Common.CBVCache;
			D3D12_GPU_VIRTUAL_ADDRESS& CurrentGPUVirtualAddress = CBVCache.CurrentGPUVirtualAddress[ShaderFrequency][SlotIndex];
			check(Location.GetGPUVirtualAddress() != CurrentGPUVirtualAddress);
			CurrentGPUVirtualAddress = Location.GetGPUVirtualAddress();
			CBVCache.ResidencyHandles[ShaderFrequency][SlotIndex] = Location.GetResource()->GetResidencyHandle();
			FD3D12ConstantBufferCache::DirtySlot(CBVCache.DirtySlotMask[ShaderFrequency], SlotIndex);

#if USE_STATIC_ROOT_SIGNATURE
			CBVCache.CBHandles[ShaderFrequency][SlotIndex] = Buffer.View->OfflineDescriptorHandle;
#endif
		}
	}

	void SetBlendFactor(const float BlendFactor[4]);
	const float* GetBlendFactor() const { return PipelineState.Graphics.CurrentBlendFactor; }
	
	void SetStencilRef(uint32 StencilRef);
	uint32 GetStencilRef() const { return PipelineState.Graphics.CurrentReferenceStencil; }

	D3D12_STATE_CACHE_INLINE void GetVertexShader(FD3D12VertexShader** Shader)
	{
		GetShader(Shader);
	}

	D3D12_STATE_CACHE_INLINE void GetHullShader(FD3D12HullShader** Shader)
	{
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
		GetShader(Shader);
#else
		*Shader = nullptr;
#endif
	}

	D3D12_STATE_CACHE_INLINE void GetDomainShader(FD3D12DomainShader** Shader)
	{
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
		GetShader(Shader);
#else
		*Shader = nullptr;
#endif
	}

	D3D12_STATE_CACHE_INLINE void GetGeometryShader(FD3D12GeometryShader** Shader)
	{
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
		GetShader(Shader);
#else
		*Shader = nullptr;
#endif
	}

	D3D12_STATE_CACHE_INLINE void GetPixelShader(FD3D12PixelShader** Shader)
	{
		GetShader(Shader);
	}

	D3D12_STATE_CACHE_INLINE void SetGraphicsPipelineState(FD3D12GraphicsPipelineState* GraphicsPipelineState, bool bTessellationChanged)
	{
		check(GraphicsPipelineState);
		if (PipelineState.Graphics.CurrentPipelineStateObject != GraphicsPipelineState)
		{
			SetStreamStrides(GraphicsPipelineState->StreamStrides);
			SetShader(GraphicsPipelineState->GetVertexShader());
			SetShader(GraphicsPipelineState->GetPixelShader());
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
			SetShader(GraphicsPipelineState->GetDomainShader());
			SetShader(GraphicsPipelineState->GetHullShader());
#endif
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
			SetShader(GraphicsPipelineState->GetGeometryShader());
#endif
			// See if we need to change the root signature
			if (GetGraphicsRootSignature() != GraphicsPipelineState->RootSignature)
			{
				PipelineState.Graphics.bNeedSetRootSignature = true;
			}

			// Save the PSO
			PipelineState.Common.bNeedSetPSO = true;
			PipelineState.Graphics.CurrentPipelineStateObject = GraphicsPipelineState;

			EPrimitiveType PrimitiveType = GraphicsPipelineState->PipelineStateInitializer.PrimitiveType;
			if (PipelineState.Graphics.CurrentPrimitiveType != PrimitiveType || bTessellationChanged)
			{
				const bool bUsingTessellation = GraphicsPipelineState->GetHullShader() && GraphicsPipelineState->GetDomainShader();
				PipelineState.Graphics.CurrentPrimitiveType = PrimitiveType;
				PipelineState.Graphics.CurrentPrimitiveTopology = GetD3D12PrimitiveType(PrimitiveType, bUsingTessellation);
				bNeedSetPrimitiveTopology = true;

				static_assert(PT_Num == 38, "This computation needs to be updated, matching that of GetVertexCountForPrimitiveCount()");
				PipelineState.Graphics.PrimitiveTypeFactor = (PrimitiveType == PT_TriangleList)? 3 : (PrimitiveType == PT_LineList)? 2 : (PrimitiveType == PT_RectList)? 3 : (PrimitiveType >= PT_1_ControlPointPatchList)? (PrimitiveType - PT_1_ControlPointPatchList + 1) : 1;
				PipelineState.Graphics.PrimitiveTypeOffset = (PrimitiveType == PT_TriangleStrip)? 2 : 0;
				PipelineState.Graphics.CurrentPrimitiveStat = (PrimitiveType == PT_LineList)? &PipelineState.Graphics.NumLines : &PipelineState.Graphics.NumTriangles;
			}

			// Set the PSO
			InternalSetPipelineState<D3D12PT_Graphics>();
		}
	}

	D3D12_STATE_CACHE_INLINE void SetComputePipelineState(FD3D12ComputePipelineState* ComputePipelineState)
	{
		check(ComputePipelineState);
		if (PipelineState.Compute.CurrentPipelineStateObject != ComputePipelineState)
		{
			// Save the PSO
			PipelineState.Common.bNeedSetPSO = true;
			PipelineState.Compute.CurrentPipelineStateObject = ComputePipelineState;

			// Set the PSO
			InternalSetPipelineState<D3D12PT_Compute>();
		}
	}

	void SetComputeShader(FD3D12ComputeShader* Shader);

	D3D12_STATE_CACHE_INLINE void GetComputeShader(FD3D12ComputeShader** ComputeShader) const
	{
		*ComputeShader = PipelineState.Compute.CurrentPipelineStateObject ? PipelineState.Compute.CurrentPipelineStateObject->ComputeShader : nullptr;
	}

	D3D12_STATE_CACHE_INLINE void SetStreamStrides(const uint16* InStreamStrides)
	{
		FMemory::Memcpy(PipelineState.Graphics.StreamStrides, InStreamStrides, sizeof(PipelineState.Graphics.StreamStrides));
	}

	D3D12_STATE_CACHE_INLINE void SetStreamSource(FD3D12ResourceLocation* VertexBufferLocation, uint32 StreamIndex, uint32 Stride, uint32 Offset)
	{
		ensure(Stride == PipelineState.Graphics.StreamStrides[StreamIndex]);
		InternalSetStreamSource(VertexBufferLocation, StreamIndex, Stride, Offset);
	}

	D3D12_STATE_CACHE_INLINE void SetStreamSource(FD3D12ResourceLocation* VertexBufferLocation, uint32 StreamIndex, uint32 Offset)
	{
		InternalSetStreamSource(VertexBufferLocation, StreamIndex, PipelineState.Graphics.StreamStrides[StreamIndex], Offset);
	}

	D3D12_STATE_CACHE_INLINE bool IsShaderResource(const FD3D12ResourceLocation* VertexBufferLocation) const
	{
		for (int i = 0; i < SF_NumStandardFrequencies; i++)
		{
			if (PipelineState.Common.SRVCache.MaxBoundIndex[i] < 0)
			{
				continue;
			}

			for (int32 j = 0; j < PipelineState.Common.SRVCache.MaxBoundIndex[i]; ++j)
			{
				if (PipelineState.Common.SRVCache.Views[i][j] && PipelineState.Common.SRVCache.Views[i][j]->GetResourceLocation())
				{
					if (PipelineState.Common.SRVCache.Views[i][j]->GetResourceLocation() == VertexBufferLocation)
					{
						return true;
					}
				}
			}
		}

		return false;
	}

	D3D12_STATE_CACHE_INLINE bool IsStreamSource(const FD3D12ResourceLocation* VertexBufferLocation) const
	{
		for (int32 index = 0; index <= PipelineState.Graphics.VBCache.MaxBoundVertexBufferIndex; ++index)
		{
			if (PipelineState.Graphics.VBCache.CurrentVertexBufferResources[index] == VertexBufferLocation)
			{
				return true;
			}
		}

		return false;
	}

public:

	D3D12_STATE_CACHE_INLINE void SetIndexBuffer(const FD3D12ResourceLocation& IndexBufferLocation, DXGI_FORMAT Format, uint32 Offset)
	{
		D3D12_GPU_VIRTUAL_ADDRESS BufferLocation = IndexBufferLocation.GetGPUVirtualAddress() + Offset;
		UINT SizeInBytes = IndexBufferLocation.GetSize() - Offset;

		D3D12_INDEX_BUFFER_VIEW& CurrentView = PipelineState.Graphics.IBCache.CurrentIndexBufferView;

		if (BufferLocation != CurrentView.BufferLocation ||
			SizeInBytes != CurrentView.SizeInBytes ||
			Format != CurrentView.Format ||
			GD3D12SkipStateCaching)
		{
			CurrentView.BufferLocation = BufferLocation;
			CurrentView.SizeInBytes = SizeInBytes;
			CurrentView.Format = Format;

			InternalSetIndexBuffer(IndexBufferLocation.GetResource());
		}
	}

	D3D12_STATE_CACHE_INLINE void GetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY* PrimitiveTopology) const
	{
		*PrimitiveTopology = PipelineState.Graphics.CurrentPrimitiveTopology;
	}

	FD3D12StateCacheBase(FRHIGPUMask Node);

	void Init(FD3D12Device* InParent, FD3D12CommandContext* InCmdContext, const FD3D12StateCacheBase* AncestralState, FD3D12SubAllocatedOnlineHeap::SubAllocationDesc& SubHeapDesc);

	virtual ~FD3D12StateCacheBase()
	{
	}

#if D3D12_RHI_RAYTRACING
	// When transitioning between RayGen and Compute, it is necessary to clear the state cache
	void TransitionComputeState(ED3D12PipelineType PipelineType)
	{
		if (LastComputePipelineType != PipelineType)
		{
			PipelineState.Common.bNeedSetPSO = true;
			PipelineState.Compute.bNeedSetRootSignature = true;

			LastComputePipelineType = PipelineType;
		}
	}

	ED3D12PipelineType LastComputePipelineType = D3D12PT_Compute;
#endif // D3D12_RHI_RAYTRACING

	template <ED3D12PipelineType PipelineType> 
	void ApplyState();
	void ApplySamplers(const FD3D12RootSignature* const pRootSignature, uint32 StartStage, uint32 EndStage);
	void DirtyStateForNewCommandList();
	void DirtyState();
	void DirtyViewDescriptorTables();
	void DirtySamplerDescriptorTables();
	bool AssertResourceStates(ED3D12PipelineType PipelineType);

	void SetRenderTargets(uint32 NumSimultaneousRenderTargets, FD3D12RenderTargetView** RTArray, FD3D12DepthStencilView* DSTarget);
	D3D12_STATE_CACHE_INLINE void GetRenderTargets(FD3D12RenderTargetView **RTArray, uint32* NumSimultaneousRTs, FD3D12DepthStencilView** DepthStencilTarget)
	{
		if (RTArray) //NULL is legal
		{
			FMemory::Memcpy(RTArray, PipelineState.Graphics.RenderTargetArray, sizeof(FD3D12RenderTargetView*)* D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT);
			*NumSimultaneousRTs = PipelineState.Graphics.CurrentNumberOfRenderTargets;
		}

		if (DepthStencilTarget)
		{
			*DepthStencilTarget = PipelineState.Graphics.CurrentDepthStencilTarget;
		}
	}

	template <EShaderFrequency ShaderStage>
	void SetUAVs(uint32 UAVStartSlot, uint32 NumSimultaneousUAVs, FD3D12UnorderedAccessView** UAVArray, uint32* UAVInitialCountArray);
	template <EShaderFrequency ShaderStage>
	void ClearUAVs();


	void SetDepthBounds(float MinDepth, float MaxDepth)
	{
		if (PipelineState.Graphics.MinDepth != MinDepth || PipelineState.Graphics.MaxDepth != MaxDepth)
		{
			PipelineState.Graphics.MinDepth = MinDepth;
			PipelineState.Graphics.MaxDepth = MaxDepth;

			bNeedSetDepthBounds = GSupportsDepthBoundsTest;
		}
	}

	void SetComputeBudget(EAsyncComputeBudget ComputeBudget)
	{
		PipelineState.Compute.ComputeBudget = ComputeBudget;
	}

	D3D12_STATE_CACHE_INLINE void AutoFlushComputeShaderCache(bool bEnable)
	{
		bAutoFlushComputeShaderCache = bEnable;
	}

	void FlushComputeShaderCache(bool bForce = false);

	/**
	 * Clears all D3D12 State, setting all input/output resource slots, shaders, input layouts,
	 * predications, scissor rectangles, depth-stencil state, rasterizer state, blend state,
	 * sampler state, and viewports to NULL
	 */
	virtual void ClearState();

	/**
	 * Releases any object references held by the state cache
	 */
	void Clear();

	void ForceSetGraphicsRootSignature() { PipelineState.Graphics.bNeedSetRootSignature = true; }
	void ForceSetComputeRootSignature() { PipelineState.Compute.bNeedSetRootSignature = true; }
	void ForceSetVB() { bNeedSetVB = true; }
	void ForceSetRTs() { bNeedSetRTs = true; }
	void ForceSetSOs() { bNeedSetSOs = true; }
	void ForceSetSamplersPerShaderStage(uint32 Frequency) { PipelineState.Common.SamplerCache.Dirty((EShaderFrequency)Frequency); }
	void ForceSetSRVsPerShaderStage(uint32 Frequency) { PipelineState.Common.SRVCache.Dirty((EShaderFrequency)Frequency); }
	void ForceSetViewports() { bNeedSetViewports = true; }
	void ForceSetScissorRects() { bNeedSetScissorRects = true; }
	void ForceSetPrimitiveTopology() { bNeedSetPrimitiveTopology = true; }
	void ForceSetBlendFactor() { bNeedSetBlendFactor = true; }
	void ForceSetStencilRef() { bNeedSetStencilRef = true; }

	bool GetForceSetVB() const { return bNeedSetVB; }
	bool GetForceSetRTs() const { return bNeedSetRTs; }
	bool GetForceSetSOs() const { return bNeedSetSOs; }
	bool GetForceSetSamplersPerShaderStage(uint32 Frequency) const { return PipelineState.Common.SamplerCache.DirtySlotMask[Frequency] != 0; }
	bool GetForceSetSRVsPerShaderStage(uint32 Frequency) const { return PipelineState.Common.SRVCache.DirtySlotMask[Frequency] != 0; }
	bool GetForceSetViewports() const { return bNeedSetViewports; }
	bool GetForceSetScissorRects() const { return bNeedSetScissorRects; }
	bool GetForceSetPrimitiveTopology() const { return bNeedSetPrimitiveTopology; }
	bool GetForceSetBlendFactor() const { return bNeedSetBlendFactor; }
	bool GetForceSetStencilRef() const { return bNeedSetStencilRef; }


#if D3D12_STATE_CACHE_DEBUG
protected:
	// Debug helper methods to verify cached state integrity.
	template <EShaderFrequency ShaderFrequency>
	void VerifySamplerStates();

	template <EShaderFrequency ShaderFrequency>
	void VerifyConstantBuffers();

	template <EShaderFrequency ShaderFrequency>
	void VerifyShaderResourceViews();
#endif
};
