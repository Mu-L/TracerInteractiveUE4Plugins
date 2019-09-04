// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D12View.cpp: 
=============================================================================*/

#include "D3D12RHIPrivate.h"

static FORCEINLINE D3D12_SHADER_RESOURCE_VIEW_DESC GetVertexBufferSRVDesc(FD3D12VertexBuffer* VertexBuffer, uint32& CreationStride, uint8 Format)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	if (VertexBuffer->GetUsage() & BUF_ByteAddressBuffer)
	{
		SRVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
		SRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
		CreationStride = 4;
	}
	else
	{
		SRVDesc.Format = FindShaderResourceDXGIFormat((DXGI_FORMAT)GPixelFormats[Format].PlatformFormat, false);
	}
	SRVDesc.Buffer.FirstElement = VertexBuffer->ResourceLocation.GetOffsetFromBaseOfResource() / CreationStride;
	SRVDesc.Buffer.NumElements = VertexBuffer->GetSize() / CreationStride;
	SRVDesc.Buffer.StructureByteStride = 0;
	return SRVDesc;
}

static FORCEINLINE D3D12_SHADER_RESOURCE_VIEW_DESC GetIndexBufferSRVDesc(FD3D12IndexBuffer* IndexBuffer)
{
	const uint32 Usage = IndexBuffer->GetUsage();
	const uint32 Width = IndexBuffer->GetSize();
	const uint32 CreationStride = IndexBuffer->GetStride();
	const FD3D12ResourceLocation& Location = IndexBuffer->ResourceLocation;

	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	if (Usage & BUF_ByteAddressBuffer)
	{
		check(CreationStride == 4u);
		SRVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
		SRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
	}
	else
	{
		check(CreationStride == 2u || CreationStride == 4u);
		SRVDesc.Format = CreationStride == 2u ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
	}
	SRVDesc.Buffer.NumElements = Width / CreationStride;
	SRVDesc.Buffer.StructureByteStride = 0;

	if (Location.GetResource())
	{
		// Create a Shader Resource View
		SRVDesc.Buffer.FirstElement = Location.GetOffsetFromBaseOfResource() / CreationStride;
	}
	else
	{
		// Null underlying D3D12 resource should only be the case for dynamic resources
		check(Usage & BUF_AnyDynamic);
	}
	return SRVDesc;
}

template<typename TextureType>
FD3D12ShaderResourceView* CreateSRV(TextureType* Texture, D3D12_SHADER_RESOURCE_VIEW_DESC& Desc)
{
	if (Texture == nullptr)
	{
		return nullptr;
	}

	FD3D12Adapter* Adapter = Texture->GetParentDevice()->GetParentAdapter();

	return Adapter->CreateLinkedViews<TextureType, FD3D12ShaderResourceView>(Texture, [&Desc](TextureType* Texture)
	{
		return new FD3D12ShaderResourceView(Texture->GetParentDevice(), Desc, Texture->ResourceLocation);
	});
}

FShaderResourceViewRHIRef FD3D12DynamicRHI::RHICreateShaderResourceView(FRHITexture* Texture, const FRHITextureSRVCreateInfo& CreateInfo)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	DXGI_FORMAT BaseTextureFormat = DXGI_FORMAT_UNKNOWN;
	FD3D12TextureBase* BaseTexture = nullptr;
	if (FD3D12Texture3D* Texture3D = FD3D12DynamicRHI::ResourceCast(Texture->GetTexture3D()))
	{
		const D3D12_RESOURCE_DESC& TextureDesc = Texture3D->GetResource()->GetDesc();
		BaseTextureFormat = TextureDesc.Format;
		BaseTexture = Texture3D;
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
		SRVDesc.Texture3D.MipLevels = CreateInfo.NumMipLevels;
		SRVDesc.Texture3D.MostDetailedMip = CreateInfo.MipLevel;
	}
	else if (FD3D12Texture2DArray* Texture2DArray = FD3D12DynamicRHI::ResourceCast(Texture->GetTexture2DArray()))
	{
		const D3D12_RESOURCE_DESC& TextureDesc = Texture2DArray->GetResource()->GetDesc();
		BaseTextureFormat = TextureDesc.Format;
		BaseTexture = Texture2DArray;
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		SRVDesc.Texture2DArray.ArraySize = (CreateInfo.NumArraySlices == 0 ? TextureDesc.DepthOrArraySize : CreateInfo.NumArraySlices);
		SRVDesc.Texture2DArray.FirstArraySlice = CreateInfo.FirstArraySlice;
		SRVDesc.Texture2DArray.MipLevels = CreateInfo.NumMipLevels;
		SRVDesc.Texture2DArray.MostDetailedMip = CreateInfo.MipLevel;
	}
	else if (FD3D12TextureCube* TextureCube = FD3D12DynamicRHI::ResourceCast(Texture->GetTextureCube()))
	{
		const D3D12_RESOURCE_DESC& TextureDesc = TextureCube->GetResource()->GetDesc();
		BaseTextureFormat = TextureDesc.Format;
		BaseTexture = TextureCube;
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		SRVDesc.TextureCube.MipLevels = CreateInfo.NumMipLevels;
		SRVDesc.TextureCube.MostDetailedMip = CreateInfo.MipLevel;
	}
	else
	{
		FD3D12Texture2D* Texture2D = FD3D12DynamicRHI::ResourceCast(Texture->GetTexture2D());
		const D3D12_RESOURCE_DESC& TextureDesc = Texture2D->GetResource()->GetDesc();
		BaseTextureFormat = TextureDesc.Format;
		BaseTexture = Texture2D;

		if (TextureDesc.SampleDesc.Count > 1)
		{
			// MS textures can't have mips apparently, so nothing else to set.
			SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
		}
		else
		{
			SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			SRVDesc.Texture2D.MipLevels = CreateInfo.NumMipLevels;
			SRVDesc.Texture2D.MostDetailedMip = CreateInfo.MipLevel;
		}
	}

	// Allow input CreateInfo to override SRGB and/or format
	const bool bBaseSRGB = (Texture->GetFlags() & TexCreate_SRGB) != 0;
	const bool bSRGB = (CreateInfo.SRGBOverride == SRGBO_ForceEnable) || (CreateInfo.SRGBOverride == SRGBO_Default && bBaseSRGB);
	const DXGI_FORMAT ViewTextureFormat = (CreateInfo.Format == PF_Unknown) ? BaseTextureFormat : (DXGI_FORMAT)GPixelFormats[CreateInfo.Format].PlatformFormat;
	SRVDesc.Format = FindShaderResourceDXGIFormat(ViewTextureFormat, bSRGB);

	switch (SRVDesc.ViewDimension)
	{
	case D3D12_SRV_DIMENSION_TEXTURE2D: SRVDesc.Texture2D.PlaneSlice = GetPlaneSliceFromViewFormat(BaseTextureFormat, SRVDesc.Format); break;
	case D3D12_SRV_DIMENSION_TEXTURE2DARRAY: SRVDesc.Texture2DArray.PlaneSlice = GetPlaneSliceFromViewFormat(BaseTextureFormat, SRVDesc.Format); break;
	default: break; // other view types don't support PlaneSlice
	}

	check(BaseTexture);
	return CreateSRV(BaseTexture, SRVDesc);
}

FShaderResourceViewRHIRef FD3D12DynamicRHI::RHICreateShaderResourceView(FRHIStructuredBuffer* StructuredBufferRHI)
{
	struct FD3D12InitializeStructuredBufferSRVRHICommand final : public FRHICommand<FD3D12InitializeStructuredBufferSRVRHICommand>
	{
		FD3D12StructuredBuffer* StructuredBuffer;
		FD3D12ShaderResourceView* SRV;

		FD3D12InitializeStructuredBufferSRVRHICommand(FD3D12StructuredBuffer* InStructuredBuffer, FD3D12ShaderResourceView* InSRV)
			: StructuredBuffer(InStructuredBuffer)
			, SRV(InSRV)
		{}

		void Execute(FRHICommandListBase& RHICmdList)
		{
			FD3D12ResourceLocation& Location = StructuredBuffer->ResourceLocation;

			const uint64 Offset = Location.GetOffsetFromBaseOfResource();
			const D3D12_RESOURCE_DESC& BufferDesc = Location.GetResource()->GetDesc();

			const uint32 BufferUsage = StructuredBuffer->GetUsage();
			const bool bByteAccessBuffer = (BufferUsage & BUF_ByteAddressBuffer) != 0;
			const bool bUINT8Access = (BufferUsage & BUF_UINT8) != 0;
			// Create a Shader Resource View
			D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc ={};
			SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

			// BufferDesc.StructureByteStride  is not getting patched through the D3D resource DESC structs, so use the RHI version as a hack
			uint32 Stride = StructuredBuffer->GetStride();

			if (bByteAccessBuffer)
			{
				SRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
				SRVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
				Stride = 4;
			}
			else if (bUINT8Access)
			{
				SRVDesc.Format = DXGI_FORMAT_R8_UINT;
				Stride = 1;
			}
			else
			{
				SRVDesc.Buffer.StructureByteStride = Stride;
				SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
			}

			SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			SRVDesc.Buffer.NumElements = Location.GetSize() / Stride;
			SRVDesc.Buffer.FirstElement = Offset / Stride;

			// Create a Shader Resource View
			SRV->Initialize(SRVDesc, StructuredBuffer->ResourceLocation, Stride);
		}
	};

	FD3D12StructuredBuffer*  StructuredBuffer = FD3D12DynamicRHI::ResourceCast(StructuredBufferRHI);

	return GetAdapter().CreateLinkedViews<FD3D12StructuredBuffer,
		FD3D12ShaderResourceView>(StructuredBuffer, [](FD3D12StructuredBuffer* StructuredBuffer)
	{
		check(StructuredBuffer);

		FD3D12ShaderResourceView* ShaderResourceView = new FD3D12ShaderResourceView(StructuredBuffer->GetParentDevice());
		StructuredBuffer->SetDynamicSRV(ShaderResourceView);

		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		if (ShouldDeferBufferLockOperation(&RHICmdList) && (StructuredBuffer->GetUsage() & BUF_AnyDynamic))
		{
			// We have to defer the SRV initialization to the RHI thread if the buffer is dynamic (and RHI threading is enabled), as dynamic buffers can be renamed.
			// Also insert an RHI thread fence to prevent parallel translate tasks running until this command has completed.
			ALLOC_COMMAND_CL(RHICmdList, FD3D12InitializeStructuredBufferSRVRHICommand)(StructuredBuffer, ShaderResourceView);
			RHICmdList.RHIThreadFence(true);
		}
		else
		{
			// Run the command directly if we're bypassing RHI command list recording, or the buffer is not dynamic.
			FD3D12InitializeStructuredBufferSRVRHICommand Command(StructuredBuffer, ShaderResourceView);
			Command.Execute(RHICmdList);
		}

		return ShaderResourceView;
	});
}

FShaderResourceViewRHIRef FD3D12DynamicRHI::RHICreateShaderResourceView(FRHIVertexBuffer* VertexBufferRHI, uint32 Stride, uint8 Format)
{
	struct FD3D12InitializeVertexBufferSRVRHICommand final : public FRHICommand<FD3D12InitializeVertexBufferSRVRHICommand>
	{
		FD3D12VertexBuffer* VertexBuffer;
		FD3D12ShaderResourceView* SRV;

		uint32 Stride;
		uint8 Format;

		FD3D12InitializeVertexBufferSRVRHICommand(FD3D12VertexBuffer* InVertexBuffer, FD3D12ShaderResourceView* InSRV, uint32 InStride, uint8 InFormat)
			: VertexBuffer(InVertexBuffer)
			, SRV(InSRV)
			, Stride(InStride)
			, Format(InFormat)
		{}

		void Execute(FRHICommandListBase& RHICmdList)
		{
			uint32 CreationStride = Stride;
			D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = GetVertexBufferSRVDesc(VertexBuffer, CreationStride, Format);

			// Create a Shader Resource View
			SRV->Initialize(SRVDesc, VertexBuffer->ResourceLocation, CreationStride);
		}
	};

	if (!VertexBufferRHI)
	{
		return new FD3D12ShaderResourceView(nullptr);
	}

	FD3D12VertexBuffer* VertexBuffer = FD3D12DynamicRHI::ResourceCast(VertexBufferRHI);
	return GetAdapter().CreateLinkedViews<FD3D12VertexBuffer, FD3D12ShaderResourceView>(VertexBuffer,
	[Stride, Format](FD3D12VertexBuffer* VertexBuffer)
	{
		check(VertexBuffer);

		FD3D12ShaderResourceView* ShaderResourceView = new FD3D12ShaderResourceView(VertexBuffer->GetParentDevice());
		VertexBuffer->SetDynamicSRV(ShaderResourceView);

		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		if (ShouldDeferBufferLockOperation(&RHICmdList) && (VertexBuffer->GetUsage() & BUF_AnyDynamic))
		{
			// We have to defer the SRV initialization to the RHI thread if the buffer is dynamic (and RHI threading is enabled), as dynamic buffers can be renamed.
			// Also insert an RHI thread fence to prevent parallel translate tasks running until this command has completed.
			ALLOC_COMMAND_CL(RHICmdList, FD3D12InitializeVertexBufferSRVRHICommand)(VertexBuffer, ShaderResourceView, Stride, Format);
			RHICmdList.RHIThreadFence(true);
		}
		else
		{
			// Run the command directly if we're bypassing RHI command list recording, or the buffer is not dynamic.
			FD3D12InitializeVertexBufferSRVRHICommand Command(VertexBuffer, ShaderResourceView, Stride, Format);
			Command.Execute(RHICmdList);
		}

		return ShaderResourceView;
	});
}

FShaderResourceViewRHIRef FD3D12DynamicRHI::RHICreateShaderResourceView(FRHIIndexBuffer* BufferRHI)
{
	if (!BufferRHI)
	{
		return new FD3D12ShaderResourceView(nullptr);
	}
	FD3D12IndexBuffer* IndexBuffer = FD3D12DynamicRHI::ResourceCast(BufferRHI);
	return GetAdapter().CreateLinkedViews<FD3D12IndexBuffer, FD3D12ShaderResourceView>(IndexBuffer,
		[](FD3D12IndexBuffer* IndexBuffer)
	{
		check(IndexBuffer);

		FD3D12ResourceLocation& Location = IndexBuffer->ResourceLocation;
		const uint32 CreationStride = IndexBuffer->GetStride();
		D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = GetIndexBufferSRVDesc(IndexBuffer);

		FD3D12ShaderResourceView* ShaderResourceView = new FD3D12ShaderResourceView(IndexBuffer->GetParentDevice(), SRVDesc, Location, CreationStride);
		return ShaderResourceView;
	});
}

void FD3D12DynamicRHI::RHIUpdateShaderResourceView(FRHIShaderResourceView* SRV, FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format)
{
	check(SRV);
	if (VertexBuffer)
	{
		FD3D12VertexBuffer* VBD3D12 = ResourceCast(VertexBuffer);
		FD3D12ShaderResourceView* SRVD3D12 = ResourceCast(SRV);
		D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = GetVertexBufferSRVDesc(VBD3D12, Stride, Format);

		// Rename the SRV to view on the new vertex buffer
		while (VBD3D12)
		{
			FD3D12Device* ParentDevice = VBD3D12->GetParentDevice();
			SRVD3D12->Initialize(ParentDevice, SRVDesc, VBD3D12->ResourceLocation, Stride);
			VBD3D12->SetDynamicSRV(SRVD3D12);
			VBD3D12 = VBD3D12->GetNextObject();
			if (VBD3D12 && !SRVD3D12->GetNextObject())
			{
				SRVD3D12->SetNextObject(new FD3D12ShaderResourceView(VBD3D12->GetParentDevice()));
			}
			SRVD3D12 = SRVD3D12->GetNextObject();
		}
	}
}

void FD3D12DynamicRHI::RHIUpdateShaderResourceView(FRHIShaderResourceView* SRV, FRHIIndexBuffer* IndexBuffer)
{
	check(SRV);
	if (IndexBuffer)
	{
		FD3D12IndexBuffer* IBD3D12 = ResourceCast(IndexBuffer);
		FD3D12ShaderResourceView* SRVD3D12 = ResourceCast(SRV);
		D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = GetIndexBufferSRVDesc(IBD3D12);
		const uint32 Stride = IBD3D12->GetStride();

		// Rename the SRV to view on the new index buffer
		while (IBD3D12)
		{
			FD3D12Device* ParentDevice = IBD3D12->GetParentDevice();
			SRVD3D12->Initialize(ParentDevice, SRVDesc, IBD3D12->ResourceLocation, Stride);
			IBD3D12 = IBD3D12->GetNextObject();
			if (IBD3D12 && !SRVD3D12->GetNextObject())
			{
				SRVD3D12->SetNextObject(new FD3D12ShaderResourceView(IBD3D12->GetParentDevice()));
			}
			SRVD3D12 = SRVD3D12->GetNextObject();
		}
	}
}

FShaderResourceViewRHIRef FD3D12DynamicRHI::RHICreateShaderResourceView_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, const FRHITextureSRVCreateInfo& CreateInfo)
{
	return RHICreateShaderResourceView(Texture, CreateInfo);
}

FShaderResourceViewRHIRef FD3D12DynamicRHI::RHICreateShaderResourceView_RenderThread(FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBufferRHI, uint32 Stride, uint8 Format)
{
	return RHICreateShaderResourceView(VertexBufferRHI, Stride, Format);
}

FShaderResourceViewRHIRef FD3D12DynamicRHI::CreateShaderResourceView_RenderThread(FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBufferRHI, uint32 Stride, uint8 Format)
{
	return RHICreateShaderResourceView_RenderThread(RHICmdList, VertexBufferRHI, Stride, Format);
}

FShaderResourceViewRHIRef FD3D12DynamicRHI::RHICreateShaderResourceView_RenderThread(FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBufferRHI)
{
	return RHICreateShaderResourceView(StructuredBufferRHI);
}

#if USE_STATIC_ROOT_SIGNATURE
void FD3D12ConstantBufferView::AllocateHeapSlot()
{
	if (!OfflineDescriptorHandle.ptr)
	{
	    FD3D12OfflineDescriptorManager& DescriptorAllocator = GetParentDevice()->GetViewDescriptorAllocator<D3D12_CONSTANT_BUFFER_VIEW_DESC>();
	    OfflineDescriptorHandle = DescriptorAllocator.AllocateHeapSlot(OfflineHeapIndex);
	    check(OfflineDescriptorHandle.ptr != 0);
	}
}

void FD3D12ConstantBufferView::FreeHeapSlot()
{
	if (OfflineDescriptorHandle.ptr)
	{
		FD3D12OfflineDescriptorManager& DescriptorAllocator = GetParentDevice()->GetViewDescriptorAllocator<D3D12_CONSTANT_BUFFER_VIEW_DESC>();
		DescriptorAllocator.FreeHeapSlot(OfflineDescriptorHandle, OfflineHeapIndex);
		OfflineDescriptorHandle.ptr = 0;
	}
}

void FD3D12ConstantBufferView::Create(D3D12_GPU_VIRTUAL_ADDRESS GPUAddress, const uint32 AlignedSize)
{
	Desc.BufferLocation = GPUAddress;
	Desc.SizeInBytes = AlignedSize;
	GetParentDevice()->GetDevice()->CreateConstantBufferView(&Desc, OfflineDescriptorHandle);
}
#endif
