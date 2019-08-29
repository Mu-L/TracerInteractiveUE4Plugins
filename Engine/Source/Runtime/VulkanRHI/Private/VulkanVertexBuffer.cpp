// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanVertexBuffer.cpp: Vulkan vertex buffer RHI implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "VulkanLLM.h"

FVulkanVertexBuffer::FVulkanVertexBuffer(FVulkanDevice* InDevice, uint32 InSize, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo, class FRHICommandListImmediate* InRHICmdList)
	: FRHIVertexBuffer(InSize, InUsage)
	, FVulkanResourceMultiBuffer(InDevice, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, InSize, InUsage, CreateInfo, InRHICmdList)
{
}

FVertexBufferRHIRef FVulkanDynamicRHI::RHICreateVertexBuffer(uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanVertexBuffers);
	FVulkanVertexBuffer* VertexBuffer = new FVulkanVertexBuffer(Device, Size, InUsage, CreateInfo, nullptr);
	return VertexBuffer;
}

void* FVulkanDynamicRHI::RHILockVertexBuffer(FVertexBufferRHIParamRef VertexBufferRHI, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanVertexBuffers);
	FVulkanVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);
	return VertexBuffer->Lock(false, LockMode, Size, Offset);
}

#if VULKAN_BUFFER_LOCK_THREADSAFE
void* FVulkanDynamicRHI::LockVertexBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, FVertexBufferRHIParamRef VertexBufferRHI, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode)
{
	return this->RHILockVertexBuffer(VertexBufferRHI, Offset, SizeRHI, LockMode);
}
#endif

void FVulkanDynamicRHI::RHIUnlockVertexBuffer(FVertexBufferRHIParamRef VertexBufferRHI)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanVertexBuffers);
	FVulkanVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);
	VertexBuffer->Unlock(false);
}

#if VULKAN_BUFFER_LOCK_THREADSAFE
void FVulkanDynamicRHI::UnlockVertexBuffer_RenderThread(FRHICommandListImmediate& RHICmdList, FVertexBufferRHIParamRef VertexBufferRHI)
{
	return this->RHIUnlockVertexBuffer(VertexBufferRHI);
}
#endif

void FVulkanDynamicRHI::RHICopyVertexBuffer(FVertexBufferRHIParamRef SourceBufferRHI,FVertexBufferRHIParamRef DestBufferRHI)
{
	VULKAN_SIGNAL_UNIMPLEMENTED();
}
