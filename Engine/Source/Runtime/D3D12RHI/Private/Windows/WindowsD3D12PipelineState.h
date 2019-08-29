// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

// Implementation of D3D12 Pipelinestate related functions

#pragma once

class FD3D12RootSignature; // forward-declare
class FD3D12BoundShaderState; // forward-declare
struct FD3D12ComputePipelineStateDesc;
struct FD3D12LowLevelGraphicsPipelineStateDesc;

// Graphics pipeline stream struct that represents the latest version of PSO subobjects currently used by the RHI.
struct FD3D12_GRAPHICS_PIPELINE_STATE_STREAM
{
	// Note: Unused members are currently commented out to exclude them from the stream.
	// This results in a smaller struct and thus fewer tokens to parse at runtime. Feel free to add/change as necessary.

	//CD3DX12_PIPELINE_STATE_STREAM_FLAGS Flags;
	CD3DX12_PIPELINE_STATE_STREAM_NODE_MASK NodeMask;
	CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
	CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
	CD3DX12_PIPELINE_STATE_STREAM_IB_STRIP_CUT_VALUE IBStripCutValue;
	CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
	CD3DX12_PIPELINE_STATE_STREAM_VS VS;
	CD3DX12_PIPELINE_STATE_STREAM_GS GS;
	CD3DX12_PIPELINE_STATE_STREAM_STREAM_OUTPUT StreamOutput;
	CD3DX12_PIPELINE_STATE_STREAM_HS HS;
	CD3DX12_PIPELINE_STATE_STREAM_DS DS;
	CD3DX12_PIPELINE_STATE_STREAM_PS PS;
	CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC BlendState;
	CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL1 DepthStencilState;
	CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
	CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER RasterizerState;
	CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
	CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC SampleDesc;
	CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_MASK SampleMask;
	CD3DX12_PIPELINE_STATE_STREAM_CACHED_PSO CachedPSO;
	//CD3DX12_PIPELINE_STATE_STREAM_VIEW_INSTANCING ViewInstancingDesc;
};

// Compute pipeline stream struct that represents the latest version of PSO subobjects currently used by the RHI.
struct FD3D12_COMPUTE_PIPELINE_STATE_STREAM
{
	// Note: Unused members are currently commented out to exclude them from the stream.
	// This results in a smaller struct and thus fewer tokens to parse at runtime. Feel free to add/change as necessary.

	//CD3DX12_PIPELINE_STATE_STREAM_FLAGS Flags;
	CD3DX12_PIPELINE_STATE_STREAM_NODE_MASK NodeMask;
	CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
	CD3DX12_PIPELINE_STATE_STREAM_CS CS;
	CD3DX12_PIPELINE_STATE_STREAM_CACHED_PSO CachedPSO;
};

struct ComputePipelineCreationArgs_POD
{
	const FD3D12ComputePipelineStateDesc* Desc;
	ID3D12PipelineLibrary* Library;

	void Init(ComputePipelineCreationArgs_POD InArgs)
	{
		Desc = InArgs.Desc;
		Library = InArgs.Library;
	}
};

struct GraphicsPipelineCreationArgs_POD
{
	const FD3D12LowLevelGraphicsPipelineStateDesc* Desc;
	ID3D12PipelineLibrary* Library;

	void Init(GraphicsPipelineCreationArgs_POD InArgs)
	{
		Desc = InArgs.Desc;
		Library = InArgs.Library;
	}
};

#include "D3D12PipelineState.h"

struct ComputePipelineCreationArgs
{
	ComputePipelineCreationArgs()
	{
		Args.Desc = nullptr;
		Args.Library = nullptr;
	}

	ComputePipelineCreationArgs(const FD3D12ComputePipelineStateDesc* InDesc, ID3D12PipelineLibrary* InLibrary)
	{
		Args.Desc = InDesc;
		Args.Library = InLibrary;
	}

	ComputePipelineCreationArgs(const ComputePipelineCreationArgs& InArgs)
		: ComputePipelineCreationArgs(InArgs.Args.Desc, InArgs.Args.Library)
	{}

	ComputePipelineCreationArgs& operator=(const ComputePipelineCreationArgs& Other)
	{
		if (this != &Other)
		{
			Args.Desc = Other.Args.Desc;
			Args.Library = Other.Args.Library;
		}

		return *this;
	}

	ComputePipelineCreationArgs_POD Args;
};

struct GraphicsPipelineCreationArgs
{
	GraphicsPipelineCreationArgs()
	{
		Args.Desc = nullptr;
		Args.Library = nullptr;
	}

	GraphicsPipelineCreationArgs(const FD3D12LowLevelGraphicsPipelineStateDesc* InDesc, ID3D12PipelineLibrary* InLibrary)
	{
		Args.Desc = InDesc;
		Args.Library = InLibrary;
	}

	GraphicsPipelineCreationArgs(const GraphicsPipelineCreationArgs& InArgs)
		: GraphicsPipelineCreationArgs(InArgs.Args.Desc, InArgs.Args.Library)
	{}

	GraphicsPipelineCreationArgs& operator=(const GraphicsPipelineCreationArgs& Other)
	{
		if (this != &Other)
		{
			Args.Desc = Other.Args.Desc;
			Args.Library = Other.Args.Library;
		}

		return *this;
	}

	GraphicsPipelineCreationArgs_POD Args;
};

class FD3D12PipelineStateCache : public FD3D12PipelineStateCacheBase
{
private:
	FDiskCacheInterface DiskBinaryCache;
	TRefCountPtr<ID3D12PipelineLibrary> PipelineLibrary;
	bool bUseAPILibaries;


	// Add_NoLock
	void AddToDiskCache(const FD3D12LowLevelGraphicsPipelineStateDesc& Desc, FD3D12PipelineState* PipelineState);
	void AddToDiskCache(const FD3D12ComputePipelineStateDesc& Desc, FD3D12PipelineState* PipelineState);

	void WriteOutShaderBlob(PSO_CACHE_TYPE Cache, ID3D12PipelineState* APIPso);

	template<typename PipelineStateDescType>
	void ReadBackShaderBlob(PipelineStateDescType& Desc, PSO_CACHE_TYPE Cache)
	{
		SIZE_T* cachedBlobOffset = nullptr;
		DiskCaches[Cache].SetPointerAndAdvanceFilePosition((void**)&cachedBlobOffset, sizeof(SIZE_T));

		SIZE_T* cachedBlobSize = nullptr;
		DiskCaches[Cache].SetPointerAndAdvanceFilePosition((void**)&cachedBlobSize, sizeof(SIZE_T));

		check(cachedBlobOffset);
		check(cachedBlobSize);

		if (UseCachedBlobs())
		{
			check(*cachedBlobSize);
			Desc.CachedPSO.CachedBlobSizeInBytes = *cachedBlobSize;
			Desc.CachedPSO.pCachedBlob = DiskBinaryCache.GetDataAt(*cachedBlobOffset);
		}
		else
		{
			Desc.CachedPSO.CachedBlobSizeInBytes = 0;
			Desc.CachedPSO.pCachedBlob = nullptr;
		}
	}

	bool UsePipelineLibrary() const
	{
		return bUseAPILibaries && PipelineLibrary != nullptr;
	}

	bool UseCachedBlobs() const
	{
		// Use Cached Blobs if Pipeline Librarys aren't supported.
		//return bUseAPILibaries && !UsePipelineLibrary();
		return false; // Don't try to use cached blobs (for now).
	}

protected:

	void OnPSOCreated(FD3D12PipelineState* PipelineState, const FD3D12LowLevelGraphicsPipelineStateDesc& Desc) final override;
	void OnPSOCreated(FD3D12PipelineState* PipelineState, const FD3D12ComputePipelineStateDesc& Desc) final override;

public:

	void RebuildFromDiskCache(ID3D12RootSignature* GraphicsRootSignature, ID3D12RootSignature* ComputeRootSignature);

	void Close();

	void Init(FString &GraphicsCacheFilename, FString &ComputeCacheFilename, FString &DriverBlobFilename);
	bool IsInErrorState() const;

	FD3D12PipelineStateCache(FD3D12Adapter* InParent);
	~FD3D12PipelineStateCache();
};

template <typename TDesc> struct TPSOFunctionMap;
template<> struct TPSOFunctionMap < D3D12_GRAPHICS_PIPELINE_STATE_DESC >
{
	static decltype(&ID3D12Device::CreateGraphicsPipelineState) GetCreatePipelineState() { return &ID3D12Device::CreateGraphicsPipelineState; }
	static decltype(&ID3D12PipelineLibrary::LoadGraphicsPipeline) GetLoadPipeline() { return &ID3D12PipelineLibrary::LoadGraphicsPipeline; }
	static FString GetString() { return TEXT("Graphics"); }
};
template<> struct TPSOFunctionMap < D3D12_COMPUTE_PIPELINE_STATE_DESC >
{
	static decltype(&ID3D12Device::CreateComputePipelineState) GetCreatePipelineState() { return &ID3D12Device::CreateComputePipelineState; }
	static decltype(&ID3D12PipelineLibrary::LoadComputePipeline) GetLoadPipeline() { return &ID3D12PipelineLibrary::LoadComputePipeline; }
	static FString GetString() { return TEXT("Compute"); }
};

template <typename TDesc> struct TPSOStreamFunctionMap;
template<> struct TPSOStreamFunctionMap < GraphicsPipelineCreationArgs_POD >
{
	static decltype(&FD3D12_GRAPHICS_PIPELINE_STATE_DESC::GraphicsDescV0) GetPipelineStateDescV0() { return &FD3D12_GRAPHICS_PIPELINE_STATE_DESC::GraphicsDescV0; }
	typedef D3D12_GRAPHICS_PIPELINE_STATE_DESC D3D12PipelineStateDescV0Type;
	typedef FD3D12_GRAPHICS_PIPELINE_STATE_STREAM D3D12PipelineStateStreamType;
	static FString GetString() { return TEXT("Graphics Stream"); }
};
template<> struct TPSOStreamFunctionMap < ComputePipelineCreationArgs_POD >
{
	static decltype(&FD3D12_COMPUTE_PIPELINE_STATE_DESC::ComputeDescV0) GetPipelineStateDescV0() { return &FD3D12_COMPUTE_PIPELINE_STATE_DESC::ComputeDescV0; }
	typedef D3D12_COMPUTE_PIPELINE_STATE_DESC D3D12PipelineStateDescV0Type;
	typedef FD3D12_COMPUTE_PIPELINE_STATE_STREAM D3D12PipelineStateStreamType;
	static FString GetString() { return TEXT("Compute Stream"); }
};

// Thread-safe create graphics/compute pipeline state. Conditionally load/store the PSO using a Pipeline Library.
template <typename TDesc>
ID3D12PipelineState* CreatePipelineState(ID3D12Device* Device, const TDesc* Desc, ID3D12PipelineLibrary* Library, const TCHAR* Name);

// Thread-safe create graphics/compute pipeline state. Conditionally load/store the PSO using a Pipeline Library.
template <typename TDesc>
ID3D12PipelineState* CreatePipelineStateFromStream(ID3D12Device2* Device, const D3D12_PIPELINE_STATE_STREAM_DESC* Desc, ID3D12PipelineLibrary1* Library, const TCHAR* Name);

template <typename TDesc>
ID3D12PipelineState* CreatePipelineStateWrapper(FD3D12Adapter* Adapter, const TDesc* CreationArgs);
