// Copyright Epic Games, Inc. All Rights Reserved.

#include "D3D12RHIPrivate.h"
#include "ClearReplacementShaders.h"

template<typename ResourceType>
inline FD3D12UnorderedAccessView* CreateUAV(D3D12_UNORDERED_ACCESS_VIEW_DESC& Desc, ResourceType* Resource, bool bNeedsCounterResource)
{
	if (Resource == nullptr)
	{
		return nullptr;
	}

	FD3D12Adapter* Adapter = Resource->GetParentDevice()->GetParentAdapter();

	return Adapter->CreateLinkedViews<ResourceType, FD3D12UnorderedAccessView>(Resource, [bNeedsCounterResource, &Desc](ResourceType* Resource)
	{
		FD3D12Device* Device = Resource->GetParentDevice();
		FD3D12Resource* CounterResource = nullptr;

		if (bNeedsCounterResource)
		{
			const FRHIGPUMask Node = Device->GetGPUMask();
			Device->GetParentAdapter()->CreateBuffer(D3D12_HEAP_TYPE_DEFAULT, Node, Node, 4, &CounterResource,  TEXT("Counter"), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		}

		return new FD3D12UnorderedAccessView(Device, Desc, Resource->ResourceLocation, CounterResource);
	});
}

FUnorderedAccessViewRHIRef FD3D12DynamicRHI::RHICreateUnorderedAccessView(FRHIStructuredBuffer* StructuredBufferRHI, bool bUseUAVCounter, bool bAppendBuffer)
{
	FD3D12StructuredBuffer*  StructuredBuffer = FD3D12DynamicRHI::ResourceCast(StructuredBufferRHI);

	FD3D12ResourceLocation& Location = StructuredBuffer->ResourceLocation;

	const D3D12_RESOURCE_DESC& BufferDesc = Location.GetResource()->GetDesc();

	const uint32 BufferUsage = StructuredBuffer->GetUsage();
	const bool bByteAccessBuffer = (BufferUsage & BUF_ByteAddressBuffer) != 0;
	const bool bStructuredBuffer = !bByteAccessBuffer;
	check(bByteAccessBuffer != bStructuredBuffer); // You can't have a structured buffer that allows raw views

	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
	UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	UAVDesc.Format = DXGI_FORMAT_UNKNOWN;

	uint32 EffectiveStride = StructuredBuffer->GetStride();

	if (bByteAccessBuffer)
	{
		UAVDesc.Format  = DXGI_FORMAT_R32_TYPELESS;
		EffectiveStride = 4;
	}

	else if (BufferUsage & BUF_DrawIndirect)
	{
		UAVDesc.Format  = DXGI_FORMAT_R32_UINT;
		EffectiveStride = 4;
	}
	UAVDesc.Buffer.FirstElement = Location.GetOffsetFromBaseOfResource() / EffectiveStride;
	UAVDesc.Buffer.NumElements  = Location.GetSize() / EffectiveStride;
	UAVDesc.Buffer.StructureByteStride = bStructuredBuffer ? EffectiveStride : 0;
	UAVDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

	UAVDesc.Buffer.CounterOffsetInBytes = 0;

	const bool bNeedsCounterResource = bAppendBuffer | bUseUAVCounter;

	if (bByteAccessBuffer)
	{
		UAVDesc.Buffer.Flags |= D3D12_BUFFER_UAV_FLAG_RAW;
	}

	return CreateUAV(UAVDesc, StructuredBuffer, bNeedsCounterResource);
}

FUnorderedAccessViewRHIRef FD3D12DynamicRHI::RHICreateUnorderedAccessView(FRHITexture* TextureRHI, uint32 MipLevel)
{
	FD3D12TextureBase* Texture = GetD3D12TextureFromRHITexture(TextureRHI);

	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};

	const DXGI_FORMAT PlatformResourceFormat = (DXGI_FORMAT)GPixelFormats[TextureRHI->GetFormat()].PlatformFormat;
	UAVDesc.Format = FindShaderResourceDXGIFormat(PlatformResourceFormat, false);

	if (TextureRHI->GetTexture3D() != NULL)
	{
		FD3D12Texture3D* Texture3D = (FD3D12Texture3D*)Texture;
		UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
		UAVDesc.Texture3D.MipSlice = MipLevel;
		UAVDesc.Texture3D.FirstWSlice = 0;
		UAVDesc.Texture3D.WSize = Texture3D->GetSizeZ() >> MipLevel;

		return CreateUAV(UAVDesc, Texture3D, false);
	}
	else if (TextureRHI->GetTexture2DArray() != NULL)
	{
		FD3D12Texture2DArray* Texture2DArray = (FD3D12Texture2DArray*)Texture;
		UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
		UAVDesc.Texture2DArray.MipSlice = MipLevel;
		UAVDesc.Texture2DArray.FirstArraySlice = 0;
		UAVDesc.Texture2DArray.ArraySize = Texture2DArray->GetSizeZ();
		UAVDesc.Texture2DArray.PlaneSlice = GetPlaneSliceFromViewFormat(PlatformResourceFormat, UAVDesc.Format);

		return CreateUAV(UAVDesc, Texture2DArray, false);
	}
	else if (TextureRHI->GetTextureCube() != NULL)
	{
		FD3D12TextureCube* TextureCube = (FD3D12TextureCube*)Texture;
		UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
		UAVDesc.Texture2DArray.MipSlice = MipLevel;
		UAVDesc.Texture2DArray.FirstArraySlice = 0;
		UAVDesc.Texture2DArray.ArraySize = TextureCube->GetSizeZ();
		UAVDesc.Texture2DArray.PlaneSlice = GetPlaneSliceFromViewFormat(PlatformResourceFormat, UAVDesc.Format);

		return CreateUAV(UAVDesc, TextureCube, false);
	}
	else
	{
		FD3D12Texture2D* Texture2D = (FD3D12Texture2D*)Texture;
		UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		UAVDesc.Texture2D.MipSlice = MipLevel;
		UAVDesc.Texture2D.PlaneSlice = GetPlaneSliceFromViewFormat(PlatformResourceFormat, UAVDesc.Format);

		return CreateUAV(UAVDesc, Texture2D, false);
	}
}

FUnorderedAccessViewRHIRef FD3D12DynamicRHI::RHICreateUnorderedAccessView(FRHIVertexBuffer* VertexBufferRHI, uint8 Format)
{
	FD3D12VertexBuffer*  VertexBuffer = FD3D12DynamicRHI::ResourceCast(VertexBufferRHI);
	FD3D12ResourceLocation& Location = VertexBuffer->ResourceLocation;

	const D3D12_RESOURCE_DESC& BufferDesc = Location.GetResource()->GetDesc();
	const uint64 effectiveBufferSize = Location.GetSize();

	const uint32 BufferUsage = VertexBuffer->GetUsage();
	const bool bByteAccessBuffer = (BufferUsage & BUF_ByteAddressBuffer) != 0;

	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
	UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	UAVDesc.Format = FindUnorderedAccessDXGIFormat((DXGI_FORMAT)GPixelFormats[Format].PlatformFormat);
	UAVDesc.Buffer.FirstElement = Location.GetOffsetFromBaseOfResource();

	UAVDesc.Buffer.NumElements = effectiveBufferSize / GPixelFormats[Format].BlockBytes;
	UAVDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	UAVDesc.Buffer.CounterOffsetInBytes = 0;
	UAVDesc.Buffer.StructureByteStride = 0;

	if (bByteAccessBuffer)
	{
		UAVDesc.Buffer.Flags |= D3D12_BUFFER_UAV_FLAG_RAW;
		UAVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
		UAVDesc.Buffer.NumElements = effectiveBufferSize / 4;
		UAVDesc.Buffer.FirstElement /= 4;
	}

	else
	{
		UAVDesc.Buffer.NumElements = effectiveBufferSize / GPixelFormats[Format].BlockBytes;
		UAVDesc.Buffer.FirstElement /= GPixelFormats[Format].BlockBytes;
	}

	return CreateUAV(UAVDesc, VertexBuffer, false);
}

FUnorderedAccessViewRHIRef FD3D12DynamicRHI::RHICreateUnorderedAccessView(FRHIIndexBuffer* IndexBufferRHI, uint8 Format)
{
	FD3D12IndexBuffer* IndexBuffer = FD3D12DynamicRHI::ResourceCast(IndexBufferRHI);
	FD3D12ResourceLocation& Location = IndexBuffer->ResourceLocation;

	const D3D12_RESOURCE_DESC& BufferDesc = Location.GetResource()->GetDesc();
	const uint64 effectiveBufferSize = Location.GetSize();

	const uint32 BufferUsage = IndexBuffer->GetUsage();
	const bool bByteAccessBuffer = (BufferUsage & BUF_ByteAddressBuffer) != 0;

	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
	UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	UAVDesc.Format = FindUnorderedAccessDXGIFormat((DXGI_FORMAT)GPixelFormats[Format].PlatformFormat);
	UAVDesc.Buffer.FirstElement = Location.GetOffsetFromBaseOfResource();

	UAVDesc.Buffer.NumElements = effectiveBufferSize / GPixelFormats[Format].BlockBytes;
	UAVDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	UAVDesc.Buffer.CounterOffsetInBytes = 0;
	UAVDesc.Buffer.StructureByteStride = 0;

	if (bByteAccessBuffer)
	{
		UAVDesc.Buffer.Flags |= D3D12_BUFFER_UAV_FLAG_RAW;
		UAVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
		UAVDesc.Buffer.NumElements = effectiveBufferSize / 4;
		UAVDesc.Buffer.FirstElement /= 4;
	}

	else
	{
		UAVDesc.Buffer.NumElements = effectiveBufferSize / GPixelFormats[Format].BlockBytes;
		UAVDesc.Buffer.FirstElement /= GPixelFormats[Format].BlockBytes;
	}

	return CreateUAV(UAVDesc, IndexBuffer, false);
}

void FD3D12CommandContext::ClearUAV(TRHICommandList_RecursiveHazardous<FD3D12CommandContext>& RHICmdList, FD3D12UnorderedAccessView* UnorderedAccessView, const void* ClearValues, bool bFloat)
{
	const D3D12_RESOURCE_DESC& ResourceDesc = UnorderedAccessView->GetResource()->GetDesc();
	const D3D12_UNORDERED_ACCESS_VIEW_DESC& UAVDesc = UnorderedAccessView->GetDesc();

	// Only structured buffers can have an unknown format
	check(UAVDesc.ViewDimension == D3D12_UAV_DIMENSION_BUFFER || UAVDesc.Format != DXGI_FORMAT_UNKNOWN);

	EClearReplacementValueType ValueType = EClearReplacementValueType::Float;
	switch (UAVDesc.Format)
	{
	case DXGI_FORMAT_R32G32B32A32_SINT:
	case DXGI_FORMAT_R32G32B32_SINT:
	case DXGI_FORMAT_R16G16B16A16_SINT:
	case DXGI_FORMAT_R32G32_SINT:
	case DXGI_FORMAT_R8G8B8A8_SINT:
	case DXGI_FORMAT_R16G16_SINT:
	case DXGI_FORMAT_R32_SINT:
	case DXGI_FORMAT_R8G8_SINT:
	case DXGI_FORMAT_R16_SINT:
	case DXGI_FORMAT_R8_SINT:
		ValueType = EClearReplacementValueType::Int32;
		break;

	case DXGI_FORMAT_R32G32B32A32_UINT:
	case DXGI_FORMAT_R32G32B32_UINT:
	case DXGI_FORMAT_R16G16B16A16_UINT:
	case DXGI_FORMAT_R32G32_UINT:
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
	case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
	case DXGI_FORMAT_R10G10B10A2_UINT:
	case DXGI_FORMAT_R8G8B8A8_UINT:
	case DXGI_FORMAT_R16G16_UINT:
	case DXGI_FORMAT_R32_UINT:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
	case DXGI_FORMAT_R8G8_UINT:
	case DXGI_FORMAT_R16_UINT:
	case DXGI_FORMAT_R8_UINT:
		ValueType = EClearReplacementValueType::Uint32;
		break;
	}

	ensureMsgf((UAVDesc.Format == DXGI_FORMAT_UNKNOWN) || (bFloat == (ValueType == EClearReplacementValueType::Float)), TEXT("Attempt to clear a UAV using the wrong RHIClearUAV function. Float vs Integer mismatch."));

	if (UAVDesc.ViewDimension == D3D12_UAV_DIMENSION_BUFFER)
	{
		if (UAVDesc.Format == DXGI_FORMAT_UNKNOWN)
		{
			// Structured buffer.
			RHICmdList.RunOnContext([UnorderedAccessView, ClearValues, UAVDesc](auto& Context)
			{
				// Alias the structured buffer with an R32_UINT UAV to perform the clear.
				// We construct a temporary UAV on the offline heap, copy it to the online heap, and then call ClearUnorderedAccessViewUint.

				FD3D12Device* ParentDevice = Context.GetParentDevice();
				ID3D12Device* Device = ParentDevice->GetDevice();
				ID3D12Resource* Resource = UnorderedAccessView->GetResource()->GetResource();

				// Structured buffer stride must be a multiple of sizeof(uint32)
				check(UAVDesc.Buffer.StructureByteStride % sizeof(uint32) == 0);
				uint32 DwordsPerElement = UAVDesc.Buffer.StructureByteStride / sizeof(uint32);

				D3D12_UNORDERED_ACCESS_VIEW_DESC R32UAVDesc = {};
				R32UAVDesc.Format = DXGI_FORMAT_R32_UINT;
				R32UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
				R32UAVDesc.Buffer.FirstElement = UAVDesc.Buffer.FirstElement * DwordsPerElement;
				R32UAVDesc.Buffer.NumElements = UAVDesc.Buffer.NumElements * DwordsPerElement;

				// Scoped descriptor handle will free the offline CPU handle once we return
				FD3D12DescriptorHandleUAV UAVHandle(ParentDevice);
				UAVHandle.CreateViewWithCounter(R32UAVDesc, Resource, nullptr);

				// Check if the view heap is full and needs to rollover.
				if (!Context.StateCache.GetDescriptorCache()->GetCurrentViewHeap()->CanReserveSlots(1))
				{
					Context.StateCache.GetDescriptorCache()->GetCurrentViewHeap()->RollOver();
				}

				uint32 ReservedSlot = Context.StateCache.GetDescriptorCache()->GetCurrentViewHeap()->ReserveSlots(1);
				D3D12_CPU_DESCRIPTOR_HANDLE CPUHandle = UAVHandle.GetHandle();
				D3D12_CPU_DESCRIPTOR_HANDLE DestSlot = Context.StateCache.GetDescriptorCache()->GetCurrentViewHeap()->GetCPUSlotHandle(ReservedSlot);
				D3D12_GPU_DESCRIPTOR_HANDLE GPUHandle = Context.StateCache.GetDescriptorCache()->GetCurrentViewHeap()->GetGPUSlotHandle(ReservedSlot);

				Device->CopyDescriptorsSimple(1, DestSlot, CPUHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				FD3D12DynamicRHI::TransitionResource(Context.CommandListHandle, UnorderedAccessView, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				Context.numClears++;

				Context.CommandListHandle.FlushResourceBarriers();
				Context.CommandListHandle->ClearUnorderedAccessViewUint(GPUHandle, CPUHandle, Resource, *reinterpret_cast<const UINT(*)[4]>(ClearValues), 0, nullptr);
				Context.CommandListHandle.UpdateResidency(UnorderedAccessView->GetResource());

				if (Context.IsDefaultContext())
				{
					ParentDevice->RegisterGPUWork(1);
				}
			});
		}
		else
		{
			ClearUAVShader_T<EClearReplacementResourceType::Buffer, 4, false>(RHICmdList, UnorderedAccessView, UAVDesc.Buffer.NumElements, 1, 1, ClearValues, ValueType);
		}
	}
	else
	{
		if (UAVDesc.ViewDimension == D3D12_UAV_DIMENSION_TEXTURE2D)
		{
			uint32 Width = ResourceDesc.Width >> UAVDesc.Texture2D.MipSlice;
			uint32 Height = ResourceDesc.Height >> UAVDesc.Texture2D.MipSlice;
			ClearUAVShader_T<EClearReplacementResourceType::Texture2D, 4, false>(RHICmdList, UnorderedAccessView, Width, Height, 1, ClearValues, ValueType);
		}
		else if (UAVDesc.ViewDimension == D3D12_UAV_DIMENSION_TEXTURE2DARRAY)
		{
			uint32 Width = ResourceDesc.Width >> UAVDesc.Texture2DArray.MipSlice;
			uint32 Height = ResourceDesc.Height >> UAVDesc.Texture2DArray.MipSlice;
			ClearUAVShader_T<EClearReplacementResourceType::Texture2DArray, 4, false>(RHICmdList, UnorderedAccessView, Width, Height, UAVDesc.Texture2DArray.ArraySize, ClearValues, ValueType);
		}
		else if (UAVDesc.ViewDimension == D3D12_UAV_DIMENSION_TEXTURE3D)
		{
			// @todo - is WSize / mip index handling here correct?
			uint32 Width = ResourceDesc.Width >> UAVDesc.Texture2DArray.MipSlice;
			uint32 Height = ResourceDesc.Height >> UAVDesc.Texture2DArray.MipSlice;
			ClearUAVShader_T<EClearReplacementResourceType::Texture3D, 4, false>(RHICmdList, UnorderedAccessView, Width, Height, UAVDesc.Texture3D.WSize, ClearValues, ValueType);
		}
		else
		{
			ensure(0);
		}
	}
}

void FD3D12CommandContext::RHIClearUAVFloat(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FVector4& Values)
{
	TRHICommandList_RecursiveHazardous<FD3D12CommandContext> RHICmdList(this, GetGPUMask());
	ClearUAV(RHICmdList, ResourceCast(UnorderedAccessViewRHI), &Values, true);
}

void FD3D12CommandContext::RHIClearUAVUint(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FUintVector4& Values)
{
	TRHICommandList_RecursiveHazardous<FD3D12CommandContext> RHICmdList(this, GetGPUMask());
	ClearUAV(RHICmdList, ResourceCast(UnorderedAccessViewRHI), &Values, false);
}

FUnorderedAccessViewRHIRef FD3D12DynamicRHI::RHICreateUnorderedAccessView_RenderThread(FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBufferRHI, bool bUseUAVCounter, bool bAppendBuffer)
{
	FD3D12StructuredBuffer* StructuredBuffer = FD3D12DynamicRHI::ResourceCast(StructuredBufferRHI);
	// TODO: we have to stall the RHI thread when creating SRVs of dynamic buffers because they get renamed.
	// perhaps we could do a deferred operation?
	if (StructuredBuffer->GetUsage() & BUF_AnyDynamic)
	{
		FScopedRHIThreadStaller StallRHIThread(RHICmdList);
		return RHICreateUnorderedAccessView(StructuredBufferRHI, bUseUAVCounter, bAppendBuffer);
	}
	return RHICreateUnorderedAccessView(StructuredBufferRHI, bUseUAVCounter, bAppendBuffer);
}

FUnorderedAccessViewRHIRef FD3D12DynamicRHI::RHICreateUnorderedAccessView_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, uint32 MipLevel)
{
	return FD3D12DynamicRHI::RHICreateUnorderedAccessView(Texture, MipLevel);
}

FUnorderedAccessViewRHIRef FD3D12DynamicRHI::RHICreateUnorderedAccessView_RenderThread(FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBufferRHI, uint8 Format)
{
	FD3D12VertexBuffer* VertexBuffer = FD3D12DynamicRHI::ResourceCast(VertexBufferRHI);

	// TODO: we have to stall the RHI thread when creating SRVs of dynamic buffers because they get renamed.
	// perhaps we could do a deferred operation?
	if (VertexBuffer->GetUsage() & BUF_AnyDynamic)
	{
		FScopedRHIThreadStaller StallRHIThread(RHICmdList);
		return RHICreateUnorderedAccessView(VertexBufferRHI, Format);
	}
	return RHICreateUnorderedAccessView(VertexBufferRHI, Format);
}

FD3D12StagingBuffer::~FD3D12StagingBuffer()
{
	if (StagedRead)
	{
		StagedRead->DeferDelete();
	}
}

void* FD3D12StagingBuffer::Lock(uint32 Offset, uint32 NumBytes)
{
	check(!bIsLocked);
	bIsLocked = true;
	if (StagedRead)
	{
		D3D12_RANGE ReadRange;
		ReadRange.Begin = Offset;
		ReadRange.End = Offset + NumBytes;
		return reinterpret_cast<uint8*>(StagedRead->Map(&ReadRange)) + Offset;
	}
	else
	{
		return nullptr;
	}
}

void FD3D12StagingBuffer::Unlock()
{
	check(bIsLocked);
	bIsLocked = false;
	if (StagedRead)
	{
		StagedRead->Unmap();
	}
}