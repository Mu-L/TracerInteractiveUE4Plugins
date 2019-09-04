// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VulkanRHIPrivate.h"
#include "VulkanContext.h"

FVulkanShaderResourceView::FVulkanShaderResourceView(FVulkanDevice* Device, FRHIResource* InRHIBuffer, FVulkanResourceMultiBuffer* InSourceBuffer, uint32 InSize, EPixelFormat InFormat)
	: VulkanRHI::FDeviceChild(Device)
	, BufferViewFormat(InFormat)
	, SourceTexture(nullptr)
	, SourceStructuredBuffer(nullptr)
	, Size(InSize)
	, SourceBuffer(InSourceBuffer)
	, SourceRHIBuffer(InRHIBuffer)
{
	check(Device);
	if(SourceBuffer)
	{
		int32 NumBuffers = SourceBuffer->IsVolatile() ? 1 : SourceBuffer->GetNumBuffers();
		BufferViews.AddZeroed(NumBuffers);
	}
	check(BufferViewFormat != PF_Unknown);
}


FVulkanShaderResourceView::~FVulkanShaderResourceView()
{
	Clear();
	Device = nullptr;
}

void FVulkanShaderResourceView::Clear()
{
	SourceRHIBuffer = nullptr;
	SourceBuffer = nullptr;
	BufferViews.Empty();
	SourceStructuredBuffer = nullptr;
	if (Device)
	{
		TextureView.Destroy(*Device);
	}
	SourceTexture = nullptr;

	VolatileBufferHandle = VK_NULL_HANDLE;
	VolatileLockCounter = MAX_uint32;
}

void FVulkanShaderResourceView::Rename(FRHIResource* InRHIBuffer, FVulkanResourceMultiBuffer* InSourceBuffer, uint32 InSize, EPixelFormat InFormat)
{
	check(Device);
	BufferViewFormat = InFormat;
	SourceTexture = nullptr;
	TextureView.Destroy(*Device);
	SourceStructuredBuffer = nullptr;
	MipLevel = 0;
	NumMips = -1;
	BufferViews.Reset();
	BufferViews.AddZeroed(InSourceBuffer->IsVolatile() ? 1 : InSourceBuffer->GetNumBuffers());
	BufferIndex = 0;
	Size = InSize;
	SourceBuffer = InSourceBuffer;
	SourceRHIBuffer = InRHIBuffer;
	VolatileBufferHandle = VK_NULL_HANDLE;	
	VolatileLockCounter = MAX_uint32;
}

void FVulkanShaderResourceView::UpdateView()
{
#if VULKAN_ENABLE_AGGRESSIVE_STATS
	SCOPE_CYCLE_COUNTER(STAT_VulkanSRVUpdateTime);
#endif

	// update the buffer view for dynamic backed buffers (or if it was never set)
	if (SourceBuffer != nullptr)
	{
		if (SourceBuffer->IsVolatile() && VolatileLockCounter != SourceBuffer->GetVolatileLockCounter())
		{
			VkBuffer SourceVolatileBufferHandle = SourceBuffer->GetHandle();

			// We might end up with the same BufferView, so do not recreate in that case
			if (!BufferViews[0] 
				|| BufferViews[0]->Offset != SourceBuffer->GetOffset() 
				|| BufferViews[0]->Size != Size
				|| VolatileBufferHandle != SourceVolatileBufferHandle)
			{
				BufferViews[0] = nullptr;
			}

			VolatileLockCounter = SourceBuffer->GetVolatileLockCounter();
			VolatileBufferHandle = SourceVolatileBufferHandle;
		}
		else if (SourceBuffer->IsDynamic())
		{
			BufferIndex = SourceBuffer->GetDynamicIndex();
		}

		if (!BufferViews[BufferIndex])
		{
			BufferViews[BufferIndex] = new FVulkanBufferView(Device);
			BufferViews[BufferIndex]->Create(SourceBuffer, BufferViewFormat, SourceBuffer->GetOffset(), Size);
		}
	}
	else if (SourceStructuredBuffer)
	{
		// Nothing...
	}
	else
	{
		ensure(SRGBOverride == SRGBO_Default); // TODO - handle other cases

		if (TextureView.View == VK_NULL_HANDLE)
		{
			EPixelFormat Format = (BufferViewFormat == PF_Unknown) ? SourceTexture->GetFormat() : BufferViewFormat;
			if (FRHITexture2D* Tex2D = SourceTexture->GetTexture2D())
			{
				FVulkanTexture2D* VTex2D = ResourceCast(Tex2D);
				EPixelFormat OriginalFormat = Format;
				TextureView.Create(*Device, VTex2D->Surface.Image, VK_IMAGE_VIEW_TYPE_2D, VTex2D->Surface.GetPartialAspectMask(), Format, UEToVkTextureFormat(Format, false), MipLevel, NumMips, 0, 1);
			}
			else if (FRHITextureCube* TexCube = SourceTexture->GetTextureCube())
			{
				FVulkanTextureCube* VTexCube = ResourceCast(TexCube);
				TextureView.Create(*Device, VTexCube->Surface.Image, VK_IMAGE_VIEW_TYPE_CUBE, VTexCube->Surface.GetPartialAspectMask(), Format, UEToVkTextureFormat(Format, false), MipLevel, NumMips, 0, 1);
			}
			else if (FRHITexture3D* Tex3D = SourceTexture->GetTexture3D())
			{
				FVulkanTexture3D* VTex3D = ResourceCast(Tex3D);
				TextureView.Create(*Device, VTex3D->Surface.Image, VK_IMAGE_VIEW_TYPE_3D, VTex3D->Surface.GetPartialAspectMask(), Format, UEToVkTextureFormat(Format, false), MipLevel, NumMips, 0, 1);
			}
			else if (FRHITexture2DArray* Tex2DArray = SourceTexture->GetTexture2DArray())
			{
				FVulkanTexture2DArray* VTex2DArray = ResourceCast(Tex2DArray);
				TextureView.Create(
					*Device,
					VTex2DArray->Surface.Image,
					VK_IMAGE_VIEW_TYPE_2D_ARRAY,
					VTex2DArray->Surface.GetPartialAspectMask(),
					Format,
					UEToVkTextureFormat(Format, false),
					MipLevel,
					NumMips,
					FirstArraySlice,
					(NumArraySlices == 0 ? VTex2DArray->GetSizeZ() : NumArraySlices)
				);
			}
			else
			{
				ensure(0);
			}
		}
	}
}


FVulkanUnorderedAccessView::~FVulkanUnorderedAccessView()
{
	TextureView.Destroy(*Device);
	BufferView = nullptr;
	SourceVertexBuffer = nullptr;
	SourceTexture = nullptr;
	Device = nullptr;
}

void FVulkanUnorderedAccessView::UpdateView()
{
#if VULKAN_ENABLE_AGGRESSIVE_STATS
	SCOPE_CYCLE_COUNTER(STAT_VulkanUAVUpdateTime);
#endif

	// update the buffer view for dynamic VB backed buffers (or if it was never set)
	if (SourceVertexBuffer != nullptr)
	{
		if (SourceVertexBuffer->IsVolatile() && VolatileLockCounter != SourceVertexBuffer->GetVolatileLockCounter())
		{
			BufferView = nullptr;
			VolatileLockCounter = SourceVertexBuffer->GetVolatileLockCounter();
		}

		if (BufferView == nullptr || SourceVertexBuffer->IsDynamic())
		{
			// thanks to ref counting, overwriting the buffer will toss the old view
			BufferView = new FVulkanBufferView(Device);
			BufferView->Create(SourceVertexBuffer.GetReference(), BufferViewFormat, SourceVertexBuffer->GetOffset(), SourceVertexBuffer->GetSize());
		}
	}
	else if (SourceIndexBuffer != nullptr)
	{
		if (SourceIndexBuffer->IsVolatile() && VolatileLockCounter != SourceIndexBuffer->GetVolatileLockCounter())
		{
			BufferView = nullptr;
			VolatileLockCounter = SourceIndexBuffer->GetVolatileLockCounter();
		}

		if (BufferView == nullptr || SourceIndexBuffer->IsDynamic())
		{
			// thanks to ref counting, overwriting the buffer will toss the old view
			BufferView = new FVulkanBufferView(Device);
			BufferView->Create(SourceIndexBuffer.GetReference(), BufferViewFormat, SourceIndexBuffer->GetOffset(), SourceIndexBuffer->GetSize());
		}
	}
	else if (SourceStructuredBuffer)
	{
		// Nothing...
		//if (SourceStructuredBuffer->IsVolatile() && VolatileLockCounter != SourceStructuredBuffer->GetVolatileLockCounter())
		//{
		//	BufferView = nullptr;
		//	VolatileLockCounter = SourceStructuredBuffer->GetVolatileLockCounter();
		//}
	}
	else if (TextureView.View == VK_NULL_HANDLE)
	{
		EPixelFormat Format = (BufferViewFormat == PF_Unknown) ? SourceTexture->GetFormat() : BufferViewFormat;
		if (FRHITexture2D* Tex2D = SourceTexture->GetTexture2D())
		{
			FVulkanTexture2D* VTex2D = ResourceCast(Tex2D);
			TextureView.Create(*Device, VTex2D->Surface.Image, VK_IMAGE_VIEW_TYPE_2D, VTex2D->Surface.GetPartialAspectMask(), Format, UEToVkTextureFormat(Format, false), MipLevel, 1, 0, 1);
		}
		else if (FRHITextureCube* TexCube = SourceTexture->GetTextureCube())
		{
			FVulkanTextureCube* VTexCube = ResourceCast(TexCube);
			TextureView.Create(*Device, VTexCube->Surface.Image, VK_IMAGE_VIEW_TYPE_CUBE, VTexCube->Surface.GetPartialAspectMask(), Format, UEToVkTextureFormat(Format, false), MipLevel, 1, 0, 1);
		}
		else if (FRHITexture3D* Tex3D = SourceTexture->GetTexture3D())
		{
			FVulkanTexture3D* VTex3D = ResourceCast(Tex3D);
			TextureView.Create(*Device, VTex3D->Surface.Image, VK_IMAGE_VIEW_TYPE_3D, VTex3D->Surface.GetPartialAspectMask(), Format, UEToVkTextureFormat(Format, false), MipLevel, 1, 0, VTex3D->GetSizeZ());
		}
		else if (FRHITexture2DArray* Tex2DArray = SourceTexture->GetTexture2DArray())
		{
			FVulkanTexture2DArray* VTex2DArray = ResourceCast(Tex2DArray);
			TextureView.Create(*Device, VTex2DArray->Surface.Image, VK_IMAGE_VIEW_TYPE_2D_ARRAY, VTex2DArray->Surface.GetPartialAspectMask(), Format, UEToVkTextureFormat(Format, false), MipLevel, 1, 0, VTex2DArray->GetSizeZ());
		}
		else
		{
			ensure(0);
		}
	}
}

FUnorderedAccessViewRHIRef FVulkanDynamicRHI::RHICreateUnorderedAccessView(FRHIStructuredBuffer* StructuredBufferRHI, bool bUseUAVCounter, bool bAppendBuffer)
{
	FVulkanStructuredBuffer* StructuredBuffer = ResourceCast(StructuredBufferRHI);

	FVulkanUnorderedAccessView* UAV = new FVulkanUnorderedAccessView(Device);
	// delay the shader view create until we use it, so we just track the source info here
	UAV->SourceStructuredBuffer = StructuredBuffer;

	//#todo-rco: bUseUAVCounter and bAppendBuffer

	return UAV;
}

FUnorderedAccessViewRHIRef FVulkanDynamicRHI::RHICreateUnorderedAccessView(FRHITexture* TextureRHI, uint32 MipLevel)
{
	FVulkanUnorderedAccessView* UAV = new FVulkanUnorderedAccessView(Device);
	UAV->SourceTexture = TextureRHI;
	UAV->MipLevel = MipLevel;
	return UAV;
}

FUnorderedAccessViewRHIRef FVulkanDynamicRHI::RHICreateUnorderedAccessView(FRHIVertexBuffer* VertexBufferRHI, uint8 Format)
{
	FVulkanVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);

	FVulkanUnorderedAccessView* UAV = new FVulkanUnorderedAccessView(Device);
	// delay the shader view create until we use it, so we just track the source info here
	UAV->BufferViewFormat = (EPixelFormat)Format;
	UAV->SourceVertexBuffer = VertexBuffer;

	return UAV;
}

FUnorderedAccessViewRHIRef FVulkanDynamicRHI::RHICreateUnorderedAccessView(FRHIIndexBuffer* IndexBufferRHI, uint8 Format)
{
	FVulkanIndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);

	FVulkanUnorderedAccessView* UAV = new FVulkanUnorderedAccessView(Device);
	// delay the shader view create until we use it, so we just track the source info here
	UAV->BufferViewFormat = (EPixelFormat)Format;
	UAV->SourceIndexBuffer = IndexBuffer;

	return UAV;
}

FShaderResourceViewRHIRef FVulkanDynamicRHI::RHICreateShaderResourceView(FRHIStructuredBuffer* StructuredBufferRHI)
{
	FVulkanStructuredBuffer* StructuredBuffer = ResourceCast(StructuredBufferRHI);
	FVulkanShaderResourceView* SRV = new FVulkanShaderResourceView(Device, StructuredBuffer);
	return SRV;
}

FShaderResourceViewRHIRef FVulkanDynamicRHI::RHICreateShaderResourceView(FRHIVertexBuffer* VertexBufferRHI, uint32 Stride, uint8 Format)
{	
	if (!VertexBufferRHI)
	{
		return new FVulkanShaderResourceView(Device, nullptr, nullptr, 0, (EPixelFormat)Format);
	}
	FVulkanVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);
	return new FVulkanShaderResourceView(Device, VertexBufferRHI, VertexBuffer, VertexBuffer->GetSize(), (EPixelFormat)Format);
}

FShaderResourceViewRHIRef FVulkanDynamicRHI::RHICreateShaderResourceView(FRHITexture* Texture, const FRHITextureSRVCreateInfo& CreateInfo)
{
	FVulkanShaderResourceView* SRV = new FVulkanShaderResourceView(Device, Texture, CreateInfo);
	return SRV;
}

FShaderResourceViewRHIRef FVulkanDynamicRHI::RHICreateShaderResourceView(FRHIIndexBuffer* IndexBufferRHI)
{
	if (!IndexBufferRHI)
	{
		return new FVulkanShaderResourceView(Device, nullptr, nullptr, 0, PF_R16_UINT);
	}
	FVulkanIndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);
	check(IndexBufferRHI->GetStride() == 2 || IndexBufferRHI->GetStride() == 4);
	EPixelFormat Format = (IndexBufferRHI->GetStride() == 4) ? PF_R32_UINT : PF_R16_UINT;
	FVulkanShaderResourceView* SRV = new FVulkanShaderResourceView(Device, IndexBufferRHI, IndexBuffer, IndexBuffer->GetSize(), Format);
	return SRV;
}

void FVulkanDynamicRHI::RHIUpdateShaderResourceView(FRHIShaderResourceView* SRV, FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format)
{
	FVulkanShaderResourceView* SRVVk = ResourceCast(SRV);
	check(SRVVk && SRVVk->GetParent() == Device);
	if (!VertexBuffer)
	{
		SRVVk->Clear();
	}
	else if (SRVVk->SourceRHIBuffer.GetReference() != VertexBuffer)
	{
		FVulkanVertexBuffer* VertexBufferVk = ResourceCast(VertexBuffer);
		SRVVk->Rename(VertexBuffer, VertexBufferVk, VertexBufferVk->GetSize(), (EPixelFormat)Format);
	}
}

void FVulkanDynamicRHI::RHIUpdateShaderResourceView(FRHIShaderResourceView* SRV, FRHIIndexBuffer* IndexBuffer)
{
	FVulkanShaderResourceView* SRVVk = ResourceCast(SRV);
	check(SRVVk && SRVVk->GetParent() == Device);
	if (!IndexBuffer)
	{
		SRVVk->Clear();
	}
	else if (SRVVk->SourceRHIBuffer.GetReference() != IndexBuffer)
	{
		FVulkanIndexBuffer* IndexBufferVk = ResourceCast(IndexBuffer);
		SRVVk->Rename(IndexBuffer, IndexBufferVk, IndexBufferVk->GetSize(), IndexBufferVk->GetStride() == 2u ? PF_R16_UINT : PF_R32_UINT);
	}
}

void FVulkanCommandListContext::RHIClearTinyUAV(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const uint32* Values)
{
	FVulkanUnorderedAccessView* UnorderedAccessView = ResourceCast(UnorderedAccessViewRHI);
	FVulkanCmdBuffer* CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();

	if (CmdBuffer->IsInsideRenderPass())
	{
		TransitionAndLayoutManager.EndEmulatedRenderPass(CmdBuffer);
		if (GVulkanSubmitAfterEveryEndRenderPass)
		{
			CommandBufferManager->SubmitActiveCmdBuffer();
			CommandBufferManager->PrepareForNewActiveCommandBuffer();
			CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
		}
	}

	if (UnorderedAccessView->SourceVertexBuffer)
	{
		FVulkanVertexBuffer* VertexBuffer = UnorderedAccessView->SourceVertexBuffer;
		switch (UnorderedAccessView->BufferViewFormat)
		{
		case PF_R32_SINT:
		case PF_R32_FLOAT:
		case PF_R32_UINT:
			break;
		case PF_A8R8G8B8:
		case PF_R8G8B8A8:
		case PF_B8G8R8A8:
			ensure(Values[0] == Values[1] && Values[1] == Values[2] && Values[2] == Values[3]);
			break;
		default:
			ensureMsgf(0, TEXT("Unsupported format (EPixelFormat)%d!"), (uint32)UnorderedAccessView->BufferViewFormat);
			break;
		}
		VulkanRHI::vkCmdFillBuffer(CmdBuffer->GetHandle(), VertexBuffer->GetHandle(), VertexBuffer->GetOffset(), VertexBuffer->GetSize(), Values[0]);
	}
	else
	{
		ensure(0);
	}
}

FVulkanComputeFence::FVulkanComputeFence(FVulkanDevice* InDevice, FName InName)
	: FRHIComputeFence(InName)
	, VulkanRHI::FGPUEvent(InDevice)
{
}

FVulkanComputeFence::~FVulkanComputeFence()
{
}

void FVulkanComputeFence::WriteCmd(VkCommandBuffer CmdBuffer, bool bInWriteEvent)
{
	FRHIComputeFence::WriteFence();
	bWriteEvent = bInWriteEvent;
	if (bInWriteEvent)
	{
		VulkanRHI::vkCmdSetEvent(CmdBuffer, Handle, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	}
}


void FVulkanComputeFence::WriteWaitEvent(VkCommandBuffer CmdBuffer)
{
	if (bWriteEvent)
	{
		VulkanRHI::vkCmdWaitEvents(CmdBuffer, 1, &Handle, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, nullptr, 0, nullptr, 0, nullptr);
	}
}

FComputeFenceRHIRef FVulkanDynamicRHI::RHICreateComputeFence(const FName& Name)
{
	return new FVulkanComputeFence(Device, Name);
}

void FVulkanGPUFence::Clear()
{
	CmdBuffer = nullptr;
	FenceSignaledCounter = MAX_uint64;
}

bool FVulkanGPUFence::Poll() const
{
	return (CmdBuffer && (FenceSignaledCounter < CmdBuffer->GetFenceSignaledCounter()));
}

FGPUFenceRHIRef FVulkanDynamicRHI::RHICreateGPUFence(const FName& Name)
{
	return new FVulkanGPUFence(Name);
}

void FVulkanCommandListContext::RHIWaitComputeFence(FRHIComputeFence* InFence)
{
	FVulkanComputeFence* Fence = ResourceCast(InFence);
	FVulkanCmdBuffer* CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
	Fence->WriteWaitEvent(CmdBuffer->GetHandle());
	IRHICommandContext::RHIWaitComputeFence(InFence);
}
