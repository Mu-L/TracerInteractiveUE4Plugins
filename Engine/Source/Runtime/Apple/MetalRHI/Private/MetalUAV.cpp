// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "MetalRHIPrivate.h"
#include "MetalCommandBuffer.h"
#include "RenderUtils.h"

FMetalShaderResourceView::FMetalShaderResourceView()
: TextureView(nullptr)
, MipLevel(0)
, NumMips(0)
, Format(0)
, Stride(0)
{
	
}

FMetalShaderResourceView::~FMetalShaderResourceView()
{
	if(TextureView)
	{
		FMetalSurface* Surface = GetMetalSurfaceFromRHITexture(SourceTexture);
		if (Surface)
		{
			Surface->SRVs.Remove(this);
		}
		
		if(TextureView->Texture)
		{
			TextureView->Texture = nil;
			
			TextureView->MSAATexture = nil;
		}
		delete TextureView;
		TextureView = nullptr;
	}
	
	SourceVertexBuffer = NULL;
	SourceTexture = NULL;
}

ns::AutoReleased<FMetalTexture> FMetalShaderResourceView::GetLinearTexture(bool const bUAV)
{
	ns::AutoReleased<FMetalTexture> NewLinearTexture;
	{
		if (IsValidRef(SourceVertexBuffer))
		{
			NewLinearTexture = SourceVertexBuffer->GetLinearTexture((EPixelFormat)Format);
			check(NewLinearTexture);
		}
		else if (IsValidRef(SourceIndexBuffer))
		{
			NewLinearTexture = SourceIndexBuffer->GetLinearTexture((EPixelFormat)Format);;
			check(NewLinearTexture);
		}
	}
	return NewLinearTexture;
}

FUnorderedAccessViewRHIRef FMetalDynamicRHI::RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBuffer, bool bUseUAVCounter, bool bAppendBuffer)
{
	return GDynamicRHI->RHICreateUnorderedAccessView(StructuredBuffer, bUseUAVCounter, bAppendBuffer);
}

FUnorderedAccessViewRHIRef FMetalDynamicRHI::RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, uint32 MipLevel)
{
	FMetalSurface* Surface = (FMetalSurface*)Texture->GetTextureBaseRHI();
	FMetalTexture Tex = Surface->Texture;
	if (!(Tex.GetUsage() & mtlpp::TextureUsage::PixelFormatView))
	{
		FScopedRHIThreadStaller StallRHIThread(RHICmdList);
		return GDynamicRHI->RHICreateUnorderedAccessView(Texture, MipLevel);
	}
	else
	{
		return GDynamicRHI->RHICreateUnorderedAccessView(Texture, MipLevel);
	}
}

FUnorderedAccessViewRHIRef FMetalDynamicRHI::RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBuffer, uint8 Format)
{
	FUnorderedAccessViewRHIRef Result = GDynamicRHI->RHICreateUnorderedAccessView(VertexBuffer, Format);
	if (IsRunningRHIInSeparateThread() && !RHICmdList.Bypass()) 	{ 		RHICmdList.RHIThreadFence(true); 	}
	return Result;
}

FUnorderedAccessViewRHIRef FMetalDynamicRHI::RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* IndexBuffer, uint8 Format)
{
	FUnorderedAccessViewRHIRef Result = GDynamicRHI->RHICreateUnorderedAccessView(IndexBuffer, Format);
	if (IsRunningRHIInSeparateThread() && !RHICmdList.Bypass()) 	{ 		RHICmdList.RHIThreadFence(true); 	}
	return Result;
}

FUnorderedAccessViewRHIRef FMetalDynamicRHI::RHICreateUnorderedAccessView(FRHIStructuredBuffer* StructuredBufferRHI, bool bUseUAVCounter, bool bAppendBuffer)
{
	@autoreleasepool {
	FMetalStructuredBuffer* StructuredBuffer = ResourceCast(StructuredBufferRHI);
	
	FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
	SRV->SourceVertexBuffer = nullptr;
	SRV->SourceIndexBuffer = nullptr;
	SRV->TextureView = nullptr;
	SRV->SourceStructuredBuffer = StructuredBuffer;

	// create the UAV buffer to point to the structured buffer's memory
	FMetalUnorderedAccessView* UAV = new FMetalUnorderedAccessView;
	UAV->SourceView = SRV;
	return UAV;
	}
}

FUnorderedAccessViewRHIRef FMetalDynamicRHI::RHICreateUnorderedAccessView(FRHITexture* TextureRHI, uint32 MipLevel)
{
	@autoreleasepool {
	FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
	SRV->SourceTexture = TextureRHI;
	
	FMetalSurface* Surface = GetMetalSurfaceFromRHITexture(TextureRHI);
	SRV->TextureView = Surface ? new FMetalSurface(*Surface, NSMakeRange(MipLevel, 1)) : nullptr;
	
	SRV->SourceVertexBuffer = nullptr;
	SRV->SourceIndexBuffer = nullptr;
	SRV->SourceStructuredBuffer = nullptr;
	
	SRV->MipLevel = MipLevel;
	SRV->NumMips = 1;
	SRV->Format = PF_Unknown;
		
	if (Surface)
	{
		Surface->SRVs.Add(SRV);
	}
		
	// create the UAV buffer to point to the structured buffer's memory
	FMetalUnorderedAccessView* UAV = new FMetalUnorderedAccessView;
	UAV->SourceView = SRV;

	return UAV;
	}
}

FUnorderedAccessViewRHIRef FMetalDynamicRHI::RHICreateUnorderedAccessView(FRHIVertexBuffer* VertexBufferRHI, uint8 Format)
{
	@autoreleasepool {
	FMetalVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);
	
	FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
	SRV->SourceVertexBuffer = VertexBuffer;
	SRV->TextureView = nullptr;
	SRV->SourceIndexBuffer = nullptr;
	SRV->SourceStructuredBuffer = nullptr;
	SRV->Format = Format;
	{
		check(VertexBuffer->GetUsage() & BUF_UnorderedAccess);
		VertexBuffer->CreateLinearTexture((EPixelFormat)Format, VertexBuffer);
	}
		
	// create the UAV buffer to point to the structured buffer's memory
	FMetalUnorderedAccessView* UAV = new FMetalUnorderedAccessView;
	UAV->SourceView = SRV;

	return UAV;
	}
}

FUnorderedAccessViewRHIRef FMetalDynamicRHI::RHICreateUnorderedAccessView(FRHIIndexBuffer* IndexBufferRHI, uint8 Format)
{
	@autoreleasepool {
		FMetalIndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);
		
		FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
		SRV->SourceVertexBuffer = nullptr;
		SRV->TextureView = nullptr;
		SRV->SourceIndexBuffer = IndexBuffer;
		SRV->SourceStructuredBuffer = nullptr;
		SRV->Format = Format;
		{
			check(IndexBuffer->GetUsage() & BUF_UnorderedAccess);
			IndexBuffer->CreateLinearTexture((EPixelFormat)Format, IndexBuffer);
		}
		
		// create the UAV buffer to point to the structured buffer's memory
		FMetalUnorderedAccessView* UAV = new FMetalUnorderedAccessView;
		UAV->SourceView = SRV;
		
		return UAV;
	}
}

FShaderResourceViewRHIRef FMetalDynamicRHI::CreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format)
{
	FShaderResourceViewRHIRef Result = GDynamicRHI->RHICreateShaderResourceView(VertexBuffer, Stride, Format);
	if (IsRunningRHIInSeparateThread() && !RHICmdList.Bypass()) 	{ 		RHICmdList.RHIThreadFence(true); 	}
	return Result;
}

FShaderResourceViewRHIRef FMetalDynamicRHI::CreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* Buffer)
{
	FShaderResourceViewRHIRef Result = GDynamicRHI->RHICreateShaderResourceView(Buffer);
	if (IsRunningRHIInSeparateThread() && !RHICmdList.Bypass()) 	{ 		RHICmdList.RHIThreadFence(true); 	}
	return Result;
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format)
{
	FShaderResourceViewRHIRef Result = GDynamicRHI->RHICreateShaderResourceView(VertexBuffer, Stride, Format);
	if (IsRunningRHIInSeparateThread() && !RHICmdList.Bypass()) 	{ 		RHICmdList.RHIThreadFence(true); 	}
	return Result;
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* Buffer)
{
	FShaderResourceViewRHIRef Result = GDynamicRHI->RHICreateShaderResourceView(Buffer);
	if (IsRunningRHIInSeparateThread() && !RHICmdList.Bypass()) 	{ 		RHICmdList.RHIThreadFence(true); 	}
	return Result;
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBuffer)
{
	return GDynamicRHI->RHICreateShaderResourceView(StructuredBuffer);
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture2DRHI, const FRHITextureSRVCreateInfo& CreateInfo)
{
	FMetalSurface* Surface = (FMetalSurface*)Texture2DRHI->GetTextureBaseRHI();
	FMetalTexture Tex = Surface->Texture;
	if (!(Tex.GetUsage() & mtlpp::TextureUsage::PixelFormatView))
	{
		FScopedRHIThreadStaller StallRHIThread(RHICmdList);
		return GDynamicRHI->RHICreateShaderResourceView(Texture2DRHI, CreateInfo);
	}
	else
	{
		return GDynamicRHI->RHICreateShaderResourceView(Texture2DRHI, CreateInfo);
	}
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView(FRHITexture* Texture2DRHI, const FRHITextureSRVCreateInfo& CreateInfo)
{
	@autoreleasepool {
		FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
		SRV->SourceTexture = (FRHITexture*)Texture2DRHI;
		
		FMetalSurface* Surface = GetMetalSurfaceFromRHITexture(Texture2DRHI);
		
		// Asking to make a SRV with PF_Unknown means to use the same format.
		// This matches the behavior of the DX11 RHI.
		EPixelFormat Format = (EPixelFormat) CreateInfo.Format;
		if(Surface && Format == PF_Unknown)
		{
			Format = Surface->PixelFormat;
		}
		
		SRV->TextureView = Surface ? new FMetalSurface(*Surface, NSMakeRange(CreateInfo.MipLevel, CreateInfo.NumMipLevels), Format) : nullptr;
		
		SRV->SourceVertexBuffer = nullptr;
		SRV->SourceIndexBuffer = nullptr;
		SRV->SourceStructuredBuffer = nullptr;
		
		SRV->MipLevel = CreateInfo.MipLevel;
		SRV->NumMips = CreateInfo.NumMipLevels;
		SRV->Format = CreateInfo.Format;
		
		if (Surface)
		{
			Surface->SRVs.Add(SRV);
		}
		
		return SRV;
	}
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView(FRHIStructuredBuffer* StructuredBufferRHI)
{
	FMetalStructuredBuffer* StructuredBuffer = ResourceCast(StructuredBufferRHI);

	FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
	SRV->SourceVertexBuffer = nullptr;
	SRV->SourceIndexBuffer = nullptr;
	SRV->TextureView = nullptr;
	SRV->SourceStructuredBuffer = StructuredBuffer;
	
	return SRV;
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView(FRHIVertexBuffer* VertexBufferRHI, uint32 Stride, uint8 Format)
{
	@autoreleasepool {
	if (!VertexBufferRHI)
	{
		FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
		SRV->SourceVertexBuffer = nullptr;
		SRV->TextureView = nullptr;
		SRV->SourceIndexBuffer = nullptr;
		SRV->SourceStructuredBuffer = nullptr;
		SRV->Format = Format;
		SRV->Stride = 0;
		return SRV;
	}
	FMetalVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);
		
	FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
	SRV->SourceVertexBuffer = VertexBuffer;
	SRV->TextureView = nullptr;
	SRV->SourceIndexBuffer = nullptr;
	SRV->SourceStructuredBuffer = nullptr;
	SRV->Format = Format;
	SRV->Stride = Stride;
	{
		check(Stride == GPixelFormats[Format].BlockBytes);
		check(VertexBuffer->GetUsage() & BUF_ShaderResource);
		
		VertexBuffer->CreateLinearTexture((EPixelFormat)Format, VertexBuffer);
	}
	
	return SRV;
	}
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView(FRHIIndexBuffer* BufferRHI)
{
	@autoreleasepool {
	if (!BufferRHI)
	{
		FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
		SRV->SourceVertexBuffer = nullptr;
		SRV->TextureView = nullptr;
		SRV->SourceIndexBuffer = nullptr;
		SRV->SourceStructuredBuffer = nullptr;
		SRV->Format = PF_R16_UINT;
		SRV->Stride = 0;
		return SRV;
	}

	FMetalIndexBuffer* Buffer = ResourceCast(BufferRHI);
		
	FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
	SRV->SourceVertexBuffer = nullptr;
	SRV->SourceIndexBuffer = Buffer;
	SRV->TextureView = nullptr;
	SRV->SourceStructuredBuffer = nullptr;
	SRV->Format = (Buffer->IndexType == mtlpp::IndexType::UInt16) ? PF_R16_UINT : PF_R32_UINT;
	{
		Buffer->CreateLinearTexture((EPixelFormat)SRV->Format, Buffer);
	}
	
	return SRV;
	}
}

void FMetalDynamicRHI::RHIUpdateShaderResourceView(FRHIShaderResourceView* SRVRHI, FRHIVertexBuffer* VertexBufferRHI, uint32 Stride, uint8 Format)
{
	check(SRVRHI);
	FMetalShaderResourceView* SRV = ResourceCast(SRVRHI);
	if (!VertexBufferRHI)
	{
		SRV->SourceVertexBuffer = nullptr;
		SRV->TextureView = nullptr;
		SRV->SourceIndexBuffer = nullptr;
		SRV->SourceStructuredBuffer = nullptr;
		SRV->Format = Format;
		SRV->Stride = Stride;
	}
	else if (SRV->SourceVertexBuffer != VertexBufferRHI)
	{
		FMetalVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);
		SRV->SourceVertexBuffer = VertexBuffer;
		SRV->TextureView = nullptr;
		SRV->SourceIndexBuffer = nullptr;
		SRV->SourceStructuredBuffer = nullptr;
		SRV->Format = Format;
		SRV->Stride = Stride;
	}
}

void FMetalDynamicRHI::RHIUpdateShaderResourceView(FRHIShaderResourceView* SRVRHI, FRHIIndexBuffer* IndexBufferRHI)
{
	check(SRVRHI);
	FMetalShaderResourceView* SRV = ResourceCast(SRVRHI);
	if (!IndexBufferRHI)
	{
		SRV->SourceVertexBuffer = nullptr;
		SRV->TextureView = nullptr;
		SRV->SourceIndexBuffer = nullptr;
		SRV->SourceStructuredBuffer = nullptr;
		SRV->Format = PF_R16_UINT;
		SRV->Stride = 0;
	}
	else if (SRV->SourceIndexBuffer != IndexBufferRHI)
	{
		FMetalIndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);
		SRV->SourceVertexBuffer = nullptr;
		SRV->TextureView = nullptr;
		SRV->SourceIndexBuffer = IndexBuffer;
		SRV->SourceStructuredBuffer = nullptr;
		SRV->Format = (IndexBuffer->IndexType == mtlpp::IndexType::UInt16) ? PF_R16_UINT : PF_R32_UINT;
		SRV->Stride = 0;
	}
}

void FMetalRHICommandContext::RHIClearTinyUAV(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const uint32* Values)
{
	@autoreleasepool {
	FMetalUnorderedAccessView* UnorderedAccessView = ResourceCast(UnorderedAccessViewRHI);
	FMetalSurface* Surface = UnorderedAccessView->SourceView->SourceTexture ? GetMetalSurfaceFromRHITexture(UnorderedAccessView->SourceView->SourceTexture) : nullptr;
	if (UnorderedAccessView->SourceView->SourceStructuredBuffer || UnorderedAccessView->SourceView->SourceVertexBuffer || UnorderedAccessView->SourceView->SourceIndexBuffer || (Surface && Surface->Texture.GetBuffer()))
	{
		check(UnorderedAccessView->SourceView->SourceStructuredBuffer || UnorderedAccessView->SourceView->SourceVertexBuffer || UnorderedAccessView->SourceView->SourceIndexBuffer || (Surface && Surface->Texture.GetBuffer()));
		
		FMetalBuffer Buffer;
		uint32 Size = 0;
		if (UnorderedAccessView->SourceView->SourceVertexBuffer)
		{
			Buffer = UnorderedAccessView->SourceView->SourceVertexBuffer->Buffer;
			Size = UnorderedAccessView->SourceView->SourceVertexBuffer->GetSize();
		}
		else if (UnorderedAccessView->SourceView->SourceStructuredBuffer)
		{
			Buffer = UnorderedAccessView->SourceView->SourceStructuredBuffer->Buffer;
			Size = UnorderedAccessView->SourceView->SourceStructuredBuffer->GetSize();
		}
		else if (UnorderedAccessView->SourceView->SourceIndexBuffer)
		{
			Buffer = UnorderedAccessView->SourceView->SourceIndexBuffer->Buffer;
			Size = UnorderedAccessView->SourceView->SourceIndexBuffer->GetSize();
		}
		else if (Surface && Surface->Texture.GetBuffer())
		{
			Buffer = FMetalBuffer(Surface->Texture.GetBuffer(), false);
		}
		
		uint32 NumComponents = 1;
		uint32 NumBytes = 1;
		EPixelFormat Format = (EPixelFormat)UnorderedAccessView->SourceView->Format;
		if (Format != 0)
		{
			NumComponents = GPixelFormats[Format].NumComponents;
			NumBytes = GPixelFormats[Format].BlockBytes;
		}
		
		// If all the values are the same then we can treat it as one component.
		NumComponents = (Values[0] == Values[1] == Values[2] == Values[3]) ? 1 : NumComponents;
		
		
		if (NumComponents > 1 || NumBytes > 1)
		{
			// get the pointer to send back for writing
			uint32 AlignedSize = Align(Size, BufferOffsetAlignment);
			uint32 Offset = 0;
			FMetalBuffer Temp = nil;
			bool bBufferPooled = false;
			
			FMetalPooledBufferArgs Args(GetMetalDeviceContext().GetDevice(), AlignedSize, BUF_Dynamic, mtlpp::StorageMode::Shared);
			Temp = GetMetalDeviceContext().CreatePooledBuffer(Args);
			bBufferPooled = true;
			
			// Construct a pattern that can be encoded into the temporary buffer (handles packing & 2-byte formats).
			uint32 Pattern[4];
			switch(Format)
			{
				case PF_Unknown:
				case PF_R8_UINT:
				case PF_G8:
				case PF_A8:
				{
					Pattern[0] = Values[0];
					break;
				}
				case PF_G16:
				case PF_R16F:
				case PF_R16F_FILTER:
				case PF_R16_UINT:
				case PF_R16_SINT:
				{
					Pattern[0] = Values[0];
					break;
				}
				case PF_R32_FLOAT:
				case PF_R32_UINT:
				case PF_R32_SINT:
				{
					Pattern[0] = Values[0];
					break;
				}
				case PF_R8G8:
				case PF_V8U8:
				{
					UE_LOG(LogMetal, Warning, TEXT("UAV pattern fill for format: %d is untested"), Format);
					Pattern[0] = Values[0];
					Pattern[1] = Values[1];
					break;
				}
				case PF_G16R16:
				case PF_G16R16F:
				case PF_R16G16_UINT:
				case PF_G16R16F_FILTER:
				{
					UE_LOG(LogMetal, Warning, TEXT("UAV pattern fill for format: %d is untested"), Format);
					Pattern[0] = Values[0];
					Pattern[0] |= ((Values[1] & 0xffff) << 16);
					break;
				}
				case PF_G32R32F:
				case PF_R32G32_UINT:
				{
					UE_LOG(LogMetal, Warning, TEXT("UAV pattern fill for format: %d is untested"), Format);
					Pattern[0] = Values[0];
					Pattern[1] = Values[1];
					break;
				}
				case PF_R5G6B5_UNORM:
				{
					UE_LOG(LogMetal, Warning, TEXT("UAV pattern fill for format: %d is untested"), Format);
					Pattern[0] = Values[0] & 0x1f;
					Pattern[0] |= (Values[1] & 0x3f) << 5;
					Pattern[0] |= (Values[2] & 0x1f) << 11;
					break;
				}
				case PF_FloatR11G11B10:
				{
					UE_LOG(LogMetal, Warning, TEXT("UAV pattern fill for format: %d is untested"), Format);
					Pattern[0] = Values[0] & 0x7FF;
					Pattern[0] |= ((Values[1] & 0x7FF) << 11);
					Pattern[0] |= ((Values[2] & 0x3FF) << 22);
					break;
				}
				case PF_B8G8R8A8:
				case PF_R8G8B8A8:
				case PF_A8R8G8B8:
				{
					UE_LOG(LogMetal, Warning, TEXT("UAV pattern fill for format: %d is untested"), Format);
					Pattern[0] = Values[0];
					Pattern[0] |= ((Values[1] & 0xff) << 8);
					Pattern[0] |= ((Values[2] & 0xff) << 16);
					Pattern[0] |= ((Values[3] & 0xff) << 24);
					break;
				}
				case PF_A2B10G10R10:
				{
					UE_LOG(LogMetal, Warning, TEXT("UAV pattern fill for format: %d is untested"), Format);
					Pattern[0] = Values[0] & 0x3;
					Pattern[0] |= ((Values[1] & 0x3FF) << 2);
					Pattern[0] |= ((Values[2] & 0x3FF) << 12);
					Pattern[0] |= ((Values[3] & 0x3FF) << 22);
					break;
				}
				case PF_A16B16G16R16:
				case PF_R16G16B16A16_UINT:
				case PF_R16G16B16A16_SINT:
				case PF_R16G16B16A16_UNORM:
				case PF_R16G16B16A16_SNORM:
				{
					UE_LOG(LogMetal, Warning, TEXT("UAV pattern fill for format: %d is untested"), Format);
					Pattern[0] = Values[0];
					Pattern[0] |= ((Values[1] & 0xffff) << 16);
					Pattern[1] = Values[2];
					Pattern[1] |= ((Values[3] & 0xffff) << 16);
					break;
				}
				case PF_R32G32B32A32_UINT:
				case PF_A32B32G32R32F:
				{
					UE_LOG(LogMetal, Warning, TEXT("UAV pattern fill for format: %d is untested"), Format);
					Pattern[0] = Values[0];
					Pattern[1] = Values[1];
					Pattern[2] = Values[2];
					Pattern[3] = Values[3];
					break;
				}
				case PF_FloatRGB:
				case PF_FloatRGBA:
				{
					METAL_FATAL_ERROR(TEXT("No UAV pattern fill for format: %d"), Format);
					break;
				}
				case PF_DepthStencil:
				case PF_ShadowDepth:
				case PF_D24:
				case PF_X24_G8:
				case PF_A1:
				case PF_ASTC_4x4:
				case PF_ASTC_6x6:
				case PF_ASTC_8x8:
				case PF_ASTC_10x10:
				case PF_ASTC_12x12:
				case PF_BC6H:
				case PF_BC7:
				case PF_ETC1:
				case PF_ETC2_RGB:
				case PF_ETC2_RGBA:
				case PF_ATC_RGB:
				case PF_ATC_RGBA_E:
				case PF_ATC_RGBA_I:
				case PF_BC4:
				case PF_PVRTC2:
				case PF_PVRTC4:
				case PF_BC5:
				case PF_DXT1:
				case PF_DXT3:
				case PF_DXT5:
				case PF_UYVY:
				case PF_MAX:
				default:
				{
					METAL_FATAL_ERROR(TEXT("No UAV support for format: %d"), Format);
					break;
				}
			}
			
			// Pattern memset for varying blocksize (1/2/4/8/16 bytes)
			switch(NumBytes)
			{
				case 1:
				{
					memset(((uint8*)Temp.GetContents()) + Offset, Pattern[0], AlignedSize);
					break;
				}
				case 2:
				{
					uint16* Dst = (uint16*)(((uint8*)Temp.GetContents()) + Offset);
					for (uint32 i = 0; i < AlignedSize / 2; i++, Dst++)
					{
						*Dst = (uint16)Pattern[0];
					}
					break;
				}
				case 4:
				{
					memset_pattern4(((uint8*)Temp.GetContents()) + Offset, Values, AlignedSize);
					break;
				}
				case 8:
				{
					memset_pattern8(((uint8*)Temp.GetContents()) + Offset, Values, AlignedSize);
					break;
				}
				case 16:
				{
					memset_pattern16(((uint8*)Temp.GetContents()) + Offset, Values, AlignedSize);
					break;
				}
				default:
				{
					METAL_FATAL_ERROR(TEXT("Invalid UAV pattern fill size (%d) for: %d"), NumBytes, Format);
					break;
				}
			}
			
			Context->CopyFromBufferToBuffer(Temp, Offset, Buffer, 0, Size);
			
			if(bBufferPooled)
			{
				GetMetalDeviceContext().ReleaseBuffer(Temp);
			}
		}
		else
		{
			// Fill the buffer via a blit encoder - I hope that is sufficient.
			Context->FillBuffer(Buffer, ns::Range(0, Size), Values[0]);
		}
		
		// If there are problems you may need to add calls to restore the render command encoder at this point
		// but we don't generally want to do that.
	}
	else if (UnorderedAccessView->SourceView->SourceTexture)
	{
		UE_LOG(LogRHI, Fatal,TEXT("Metal RHI doesn't support RHIClearTinyUAV with FRHITexture yet!"));
	}
	else
	{
		UE_LOG(LogRHI, Fatal,TEXT("Metal RHI doesn't support RHIClearUAV with this type yet!"));
	}
	}
}

FComputeFenceRHIRef FMetalDynamicRHI::RHICreateComputeFence(const FName& Name)
{
	@autoreleasepool {
	return new FMetalComputeFence(Name);
	}
}

FMetalComputeFence::FMetalComputeFence(FName InName)
: FRHIComputeFence(InName)
, Fence(nullptr)
{}

FMetalComputeFence::~FMetalComputeFence()
{
	if (Fence)
		Fence->Release();
}

void FMetalComputeFence::Write(FMetalFence* InFence)
{
	check(!Fence);
	Fence = InFence;
	if (Fence)
		Fence->AddRef();
	
	FRHIComputeFence::WriteFence();
}

void FMetalComputeFence::Wait(FMetalContext& Context)
{
	if (Context.GetCurrentCommandBuffer())
	{
		Context.SubmitCommandsHint(EMetalSubmitFlagsNone);
	}
	Context.GetCurrentRenderPass().Begin(Fence);
	
	if (Fence)
		Fence->Release();
	
	Fence = nullptr;
}

void FMetalComputeFence::Reset()
{
	FRHIComputeFence::Reset();
	if (Fence)
		Fence->Release();

	Fence = nullptr;
}

void FMetalRHICommandContext::RHITransitionResources(EResourceTransitionAccess TransitionType, EResourceTransitionPipeline TransitionPipeline, FRHIUnorderedAccessView** InUAVs, int32 NumUAVs, FRHIComputeFence* WriteComputeFence)
{
	@autoreleasepool
	{
		if (TransitionType != EResourceTransitionAccess::EMetaData)
		{
			Context->TransitionResources(InUAVs, NumUAVs);
		}
		if (WriteComputeFence)
		{
			// Get the current render pass fence.
			TRefCountPtr<FMetalFence> const& MetalFence = Context->GetCurrentRenderPass().End();
			
			// Write it again as we may wait on this fence in two different encoders
			Context->GetCurrentRenderPass().Update(MetalFence);

			// Write it into the RHI object
			FMetalComputeFence* Fence = ResourceCast(WriteComputeFence);
			Fence->Write(MetalFence);
			if (GSupportsEfficientAsyncCompute)
			{
				this->RHISubmitCommandsHint();
			}
		}
	}
}

void FMetalRHICommandContext::RHITransitionResources(EResourceTransitionAccess TransitionType, FRHITexture** InTextures, int32 NumTextures)
{
	@autoreleasepool
	{
		if (TransitionType != EResourceTransitionAccess::EMetaData)
		{
			Context->TransitionResources(InTextures, NumTextures);
		}
		if (TransitionType == EResourceTransitionAccess::EReadable)
		{
			const FResolveParams ResolveParams;
			for (int32 i = 0; i < NumTextures; ++i)
			{
				RHICopyToResolveTarget(InTextures[i], InTextures[i], ResolveParams);
			}
		}
	}
}

void FMetalRHICommandContext::RHIWaitComputeFence(FRHIComputeFence* InFence)
{
	@autoreleasepool {
	if (InFence)
	{
		checkf(InFence->GetWriteEnqueued(), TEXT("ComputeFence: %s waited on before being written. This will hang the GPU."), *InFence->GetName().ToString());
		FMetalComputeFence* Fence = ResourceCast(InFence);
		Fence->Wait(*Context);
	}
	}
}

void FMetalGPUFence::WriteInternal(mtlpp::CommandBuffer& CmdBuffer)
{
	Fence = CmdBuffer.GetCompletionFence();
	check(Fence);
}

void FMetalRHICommandContext::RHICopyToStagingBuffer(FRHIVertexBuffer* SourceBufferRHI, FRHIStagingBuffer* DestinationStagingBufferRHI, uint32 Offset, uint32 NumBytes)
{
	@autoreleasepool {
		check(DestinationStagingBufferRHI);

		FMetalStagingBuffer* MetalStagingBuffer = ResourceCast(DestinationStagingBufferRHI);
		ensureMsgf(!MetalStagingBuffer->bIsLocked, TEXT("Attempting to Copy to a locked staging buffer. This may have undefined behavior"));
		FMetalVertexBuffer* SourceBuffer = ResourceCast(SourceBufferRHI);
		FMetalBuffer& ReadbackBuffer = MetalStagingBuffer->ShadowBuffer;

		// Need a shadow buffer for this read. If it hasn't been allocated in our FStagingBuffer or if
		// it's not big enough to hold our readback we need to allocate.
		if (!ReadbackBuffer || ReadbackBuffer.GetLength() < NumBytes)
		{
			if (ReadbackBuffer)
			{
				SafeReleaseMetalBuffer(ReadbackBuffer);
			}
			FMetalPooledBufferArgs ArgsCPU(GetMetalDeviceContext().GetDevice(), NumBytes, BUF_Dynamic, mtlpp::StorageMode::Shared);
			ReadbackBuffer = GetMetalDeviceContext().CreatePooledBuffer(ArgsCPU);
		}

		// Inline copy from the actual buffer to the shadow
		GetMetalDeviceContext().CopyFromBufferToBuffer(SourceBuffer->Buffer, Offset, ReadbackBuffer, 0, NumBytes);
	}
}

void FMetalRHICommandContext::RHIWriteGPUFence(FRHIGPUFence* FenceRHI)
{
	@autoreleasepool {
		check(FenceRHI);
		FMetalGPUFence* Fence = ResourceCast(FenceRHI);
		Fence->WriteInternal(Context->GetCurrentCommandBuffer());
	}
}

FGPUFenceRHIRef FMetalDynamicRHI::RHICreateGPUFence(const FName &Name)
{
	@autoreleasepool {
	return new FMetalGPUFence(Name);
	}
}

void FMetalGPUFence::Clear()
{
	Fence = mtlpp::CommandBufferFence();
}

bool FMetalGPUFence::Poll() const
{
	if (Fence)
	{
		return Fence.Wait(0);
	}
	else
	{
		return false;
	}
}
