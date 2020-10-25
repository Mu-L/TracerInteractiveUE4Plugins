// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanRenderTarget.cpp: Vulkan render target implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "ScreenRendering.h"
#include "VulkanPendingState.h"
#include "VulkanContext.h"
#include "SceneUtils.h"
#include "RHISurfaceDataConversion.h"

static int32 GSubmitOnCopyToResolve = 0;
static FAutoConsoleVariableRef CVarVulkanSubmitOnCopyToResolve(
	TEXT("r.Vulkan.SubmitOnCopyToResolve"),
	GSubmitOnCopyToResolve,
	TEXT("Submits the Queue to the GPU on every RHICopyToResolveTarget call.\n")
	TEXT(" 0: Do not submit (default)\n")
	TEXT(" 1: Submit"),
	ECVF_Default
	);

static int32 GIgnoreCPUReads = 0;
static FAutoConsoleVariableRef CVarVulkanIgnoreCPUReads(
	TEXT("r.Vulkan.IgnoreCPUReads"),
	GIgnoreCPUReads,
	TEXT("Debugging utility for GPU->CPU reads.\n")
	TEXT(" 0 will read from the GPU (default).\n")
	TEXT(" 1 will read from GPU but fill the buffer instead of copying from a texture.\n")
	TEXT(" 2 will NOT read from the GPU and fill with zeros.\n"),
	ECVF_Default
	);

static FCriticalSection GStagingMapLock;
static TMap<FVulkanTextureBase*, VulkanRHI::FStagingBuffer*> GPendingLockedStagingBuffers;

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
TAutoConsoleVariable<int32> CVarVulkanDebugBarrier(
	TEXT("r.Vulkan.DebugBarrier"),
	0,
	TEXT("Forces a full barrier for debugging. This is a mask/bitfield (so add up the values)!\n")
	TEXT(" 0: Don't (default)\n")
	TEXT(" 1: Enable heavy barriers after EndRenderPass()\n")
	TEXT(" 2: Enable heavy barriers after every dispatch\n")
	TEXT(" 4: Enable heavy barriers after upload cmd buffers\n")
	TEXT(" 8: Enable heavy barriers after active cmd buffers\n")
	TEXT(" 16: Enable heavy buffer barrier after uploads\n")
	TEXT(" 32: Enable heavy buffer barrier between acquiring back buffer and blitting into swapchain\n"),
	ECVF_Default
);
#endif


void FTransitionAndLayoutManager::Destroy(FVulkanDevice& InDevice, FTransitionAndLayoutManager* Immediate)
{
	check(!GIsRHIInitialized);

	if (Immediate)
	{
		Immediate->RenderPasses.Append(RenderPasses);
		Immediate->Framebuffers.Append(Framebuffers);
	}
	else
	{
		for (auto& Pair : RenderPasses)
		{
			delete Pair.Value;
		}

		for (auto& Pair : Framebuffers)
		{
			FFramebufferList* List = Pair.Value;
			for (int32 Index = List->Framebuffer.Num() - 1; Index >= 0; --Index)
			{
				List->Framebuffer[Index]->Destroy(InDevice);
				delete List->Framebuffer[Index];
			}
			delete List;
		}
	}

	RenderPasses.Reset();
	Framebuffers.Reset();
}

FVulkanFramebuffer* FTransitionAndLayoutManager::GetOrCreateFramebuffer(FVulkanDevice& InDevice, const FRHISetRenderTargetsInfo& RenderTargetsInfo, const FVulkanRenderTargetLayout& RTLayout, FVulkanRenderPass* RenderPass)
{
	uint32 RTLayoutHash = RTLayout.GetRenderPassCompatibleHash();

	uint64 MipsAndSlicesValues[MaxSimultaneousRenderTargets];
	for (int32 Index = 0; Index < MaxSimultaneousRenderTargets; ++Index)
	{
		MipsAndSlicesValues[Index] = ((uint64)RenderTargetsInfo.ColorRenderTarget[Index].ArraySliceIndex << (uint64)32) | (uint64)RenderTargetsInfo.ColorRenderTarget[Index].MipIndex;
	}
	RTLayoutHash = FCrc::MemCrc32(MipsAndSlicesValues, sizeof(MipsAndSlicesValues), RTLayoutHash);

	FFramebufferList** FoundFramebufferList = Framebuffers.Find(RTLayoutHash);
	FFramebufferList* FramebufferList = nullptr;
	if (FoundFramebufferList)
	{
		FramebufferList = *FoundFramebufferList;

		for (int32 Index = 0; Index < FramebufferList->Framebuffer.Num(); ++Index)
		{
			if (FramebufferList->Framebuffer[Index]->Matches(RenderTargetsInfo))
			{
				return FramebufferList->Framebuffer[Index];
			}
		}
	}
	else
	{
		FramebufferList = new FFramebufferList;
		Framebuffers.Add(RTLayoutHash, FramebufferList);
	}

	FVulkanFramebuffer* Framebuffer = new FVulkanFramebuffer(InDevice, RenderTargetsInfo, RTLayout, *RenderPass);
	FramebufferList->Framebuffer.Add(Framebuffer);
	return Framebuffer;
}

FVulkanRenderPass* FVulkanCommandListContext::PrepareRenderPassForPSOCreation(const FGraphicsPipelineStateInitializer& Initializer)
{
	FVulkanRenderTargetLayout RTLayout(Initializer);
	return PrepareRenderPassForPSOCreation(RTLayout);
}

FVulkanRenderPass* FVulkanCommandListContext::PrepareRenderPassForPSOCreation(const FVulkanRenderTargetLayout& RTLayout)
{
	FVulkanRenderPass* RenderPass = nullptr;
	RenderPass = TransitionAndLayoutManager.GetOrCreateRenderPass(*Device, RTLayout);
	return RenderPass;
}

void FTransitionAndLayoutManager::BeginEmulatedRenderPass(FVulkanCommandListContext& Context, FVulkanDevice& InDevice, FVulkanCmdBuffer* CmdBuffer, const FRHISetRenderTargetsInfo& RenderTargetsInfo, const FVulkanRenderTargetLayout& RTLayout, FVulkanRenderPass* RenderPass, FVulkanFramebuffer* Framebuffer)
{
	check(!CurrentRenderPass);
	VkClearValue ClearValues[MaxSimultaneousRenderTargets + 1];
	FMemory::Memzero(ClearValues);

	int32 Index = 0;
	for (Index = 0; Index < RenderTargetsInfo.NumColorRenderTargets; ++Index)
	{
		FRHITexture* Texture = RenderTargetsInfo.ColorRenderTarget[Index].Texture;
		if (Texture)
		{
			FVulkanSurface& Surface = FVulkanTextureBase::Cast(Texture)->Surface;
			VkImage Image = Surface.Image;

			VkImageLayout* Found = Layouts.Find(Image);
			if (!Found)
			{
				Found = &Layouts.Add(Image, VK_IMAGE_LAYOUT_UNDEFINED);
			}

			if (*Found != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
			{
				if (*Found == VK_IMAGE_LAYOUT_UNDEFINED)
				{
					VulkanRHI::ImagePipelineBarrier(CmdBuffer->GetHandle(), Image, EImageLayoutBarrier::Undefined, EImageLayoutBarrier::ColorAttachment, SetupImageSubresourceRange());
				}
				else
				{
					Context.RHITransitionResources(EResourceTransitionAccess::EWritable, &Texture, 1);
				}
			}

			const FLinearColor& ClearColor = Texture->HasClearValue() ? Texture->GetClearColor() : FLinearColor::Black;
			ClearValues[Index].color.float32[0] = ClearColor.R;
			ClearValues[Index].color.float32[1] = ClearColor.G;
			ClearValues[Index].color.float32[2] = ClearColor.B;
			ClearValues[Index].color.float32[3] = ClearColor.A;

			*Found = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
	}

	if (RenderTargetsInfo.DepthStencilRenderTarget.Texture)
	{
		FRHITexture* DSTexture = RenderTargetsInfo.DepthStencilRenderTarget.Texture;
		FVulkanSurface& Surface = FVulkanTextureBase::Cast(DSTexture)->Surface;
		VkImageLayout& DSLayout = Layouts.FindOrAdd(Surface.Image);
		FExclusiveDepthStencil RequestedDSAccess = RenderTargetsInfo.DepthStencilRenderTarget.GetDepthStencilAccess();
		
		if (FVulkanPlatform::RequiresDepthWriteOnStencilClear() && 
			RenderTargetsInfo.DepthStencilRenderTarget.DepthStoreAction == ERenderTargetStoreAction::EStore &&
			RenderTargetsInfo.DepthStencilRenderTarget.GetStencilStoreAction() == ERenderTargetStoreAction::EStore)
		{
			RequestedDSAccess = FExclusiveDepthStencil::DepthWrite_StencilWrite;
		}

		VkImageLayout FinalLayout = VulkanRHI::GetDepthStencilLayout(RequestedDSAccess, InDevice);

		// Check if we need to transition the depth stencil texture(s) based on the current layout and the requested access mode for the render target
		if (DSLayout != FinalLayout)
		{
			VulkanRHI::FPendingBarrier Barrier;
			int32 BarrierIndex = Barrier.AddImageBarrier(Surface.Image, Surface.GetFullAspectMask(), 1);
			VulkanRHI::EImageLayoutBarrier SrcLayout = VulkanRHI::GetImageLayoutFromVulkanLayout(DSLayout);
			VulkanRHI::EImageLayoutBarrier DstLayout = VulkanRHI::GetImageLayoutFromVulkanLayout(FinalLayout);
			Barrier.SetTransition(BarrierIndex, SrcLayout, DstLayout);
			Barrier.Execute(CmdBuffer);
			DSLayout = FinalLayout;
		}

		if (DSTexture->HasClearValue())
		{
			float Depth = 0;
			uint32 Stencil = 0;
			DSTexture->GetDepthStencilClearValue(Depth, Stencil);
			ClearValues[RenderTargetsInfo.NumColorRenderTargets].depthStencil.depth = Depth;
			ClearValues[RenderTargetsInfo.NumColorRenderTargets].depthStencil.stencil = Stencil;
		}
	}
	
	CmdBuffer->BeginRenderPass(RenderPass->GetLayout(), RenderPass, Framebuffer, ClearValues);

	{
		const VkExtent3D& Extents = RTLayout.GetExtent3D();
		Context.GetPendingGfxState()->SetViewport(0, 0, 0, Extents.width, Extents.height, 1);
	}

	CurrentFramebuffer = Framebuffer;
	CurrentRenderPass = RenderPass;
}

void FTransitionAndLayoutManager::EndEmulatedRenderPass(FVulkanCmdBuffer* CmdBuffer)
{
	check(CurrentRenderPass);
	check(!bInsideRealRenderPass);
	CmdBuffer->EndRenderPass();
	CurrentRenderPass = nullptr;

	VulkanRHI::DebugHeavyWeightBarrier(CmdBuffer->GetHandle(), 1);
}

void FTransitionAndLayoutManager::BeginRealRenderPass(FVulkanCommandListContext& Context, FVulkanDevice& InDevice, FVulkanCmdBuffer* CmdBuffer, const FRHIRenderPassInfo& RPInfo, const FVulkanRenderTargetLayout& RTLayout, FVulkanRenderPass* RenderPass, FVulkanFramebuffer* Framebuffer)
{
	check(!CurrentRenderPass);
	check(!bInsideRealRenderPass);
	// (NumRT + 1 [Depth] ) * 2 [surface + resolve]
	VkClearValue ClearValues[(MaxSimultaneousRenderTargets + 1) * 2];
	uint32 ClearValueIndex = 0;
	bool bNeedsClearValues = RenderPass->GetNumUsedClearValues() > 0;
	FMemory::Memzero(ClearValues);

	int32 NumColorTargets = RPInfo.GetNumColorRenderTargets();
	int32 Index = 0;
	FPendingBarrier Barrier;
	if (RPInfo.bGeneratingMips)
	{
		GenerateMipsInfo.NumRenderTargets = NumColorTargets;
	}

	for (Index = 0; Index < NumColorTargets; ++Index)
	{
		FRHITexture* Texture = RPInfo.ColorRenderTargets[Index].RenderTarget;
		CA_ASSUME(Texture);
		FVulkanSurface& Surface = FVulkanTextureBase::Cast(Texture)->Surface;
		check(Surface.Image != VK_NULL_HANDLE);

		VkImageLayout* Found = Layouts.Find(Surface.Image);
		check(Found);

		if (RPInfo.bGeneratingMips)
		{
			int32 NumMips = Surface.GetNumMips();
			if (!GenerateMipsInfo.bInsideGenerateMips)
			{
#if !USING_CODE_ANALYSIS
				// This condition triggers static analysis as it doesn't have side effects.
				ensure(*Found == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL || *Found == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
#endif
				int32 NumSlices = Surface.GetNumberOfArrayLevels();
				GenerateMipsInfo.bInsideGenerateMips = true;
				GenerateMipsInfo.Target[Index].CurrentImage = Surface.Image;

				GenerateMipsInfo.Target[Index].Layouts.Reset(0);
				for (int32 SliceIndex = 0; SliceIndex < NumSlices; ++SliceIndex)
				{
					GenerateMipsInfo.Target[Index].Layouts.AddDefaulted();
					for (int32 MipIndex = 0; MipIndex < NumMips; ++MipIndex)
					{
						GenerateMipsInfo.Target[Index].Layouts[SliceIndex].Add(*Found);
					}
				}

				if (*Found != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
				{
					// This is since the previous mip index is used as a source image for the descriptor, it needs to know it's in R/O state
					*Found = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				}
			}

			ensure(GenerateMipsInfo.Target[Index].CurrentImage == Surface.Image);

			int32 SliceIndex = (uint32)FMath::Max<int32>(RPInfo.ColorRenderTargets[Index].ArraySlice, 0);
			int32 RTMipIndex = RPInfo.ColorRenderTargets[Index].MipIndex;
			check(RTMipIndex > 0);
			GenerateMipsInfo.CurrentSlice = SliceIndex;
			GenerateMipsInfo.CurrentMip = RTMipIndex;
			GenerateMipsInfo.bLastMip = (RTMipIndex == (NumMips - 1));

			// Check that previous mip is read only
			if (GenerateMipsInfo.Target[Index].Layouts[SliceIndex][RTMipIndex - 1] != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			{
				// Transition to readable
				int32 BarrierIndex = Barrier.AddImageBarrier(Surface.Image, VK_IMAGE_ASPECT_COLOR_BIT, 1);
				VkImageSubresourceRange& Range = Barrier.GetSubresource(BarrierIndex);
				Range.baseMipLevel = RTMipIndex - 1;
				Range.baseArrayLayer = SliceIndex;
#if !USING_CODE_ANALYSIS
				ensure(GenerateMipsInfo.Target[Index].Layouts[SliceIndex][RTMipIndex - 1] == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
#endif
				Barrier.SetTransition(BarrierIndex, EImageLayoutBarrier::ColorAttachment, EImageLayoutBarrier::PixelShaderRead);
				GenerateMipsInfo.Target[Index].Layouts[SliceIndex][RTMipIndex - 1] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}

			// Check that current mip is write-only
			if (GenerateMipsInfo.Target[Index].Layouts[SliceIndex][RTMipIndex] != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
			{
				// Transition to writeable
				int32 BarrierIndex = Barrier.AddImageBarrier(Surface.Image, VK_IMAGE_ASPECT_COLOR_BIT, 1);
				VkImageSubresourceRange& Range = Barrier.GetSubresource(BarrierIndex);
				Range.baseMipLevel = RTMipIndex;
				Range.baseArrayLayer = SliceIndex;
#if !USING_CODE_ANALYSIS
				ensure(GenerateMipsInfo.Target[Index].Layouts[SliceIndex][RTMipIndex] == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
#endif
				Barrier.SetTransition(BarrierIndex, EImageLayoutBarrier::PixelShaderRead, EImageLayoutBarrier::ColorAttachment);
				GenerateMipsInfo.Target[Index].Layouts[SliceIndex][RTMipIndex] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			}
		}
		else
		{
			if (*Found == VK_IMAGE_LAYOUT_UNDEFINED)
			{
				VulkanRHI::ImagePipelineBarrier(CmdBuffer->GetHandle(), Surface.Image, EImageLayoutBarrier::Undefined, EImageLayoutBarrier::ColorAttachment, SetupImageSubresourceRange());
			}
			else if (*Found == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && GetLoadAction(RPInfo.ColorRenderTargets[Index].Action) == ERenderTargetLoadAction::ELoad)
			{
				// make sure we have dependency between multiple render-passes that use the same attachment
				// otherwise GPU can be execute them in any order
				VulkanRHI::ImagePipelineBarrier(CmdBuffer->GetHandle(), Surface.Image, EImageLayoutBarrier::ColorAttachment, EImageLayoutBarrier::ColorAttachment, SetupImageSubresourceRange());
			}
			else
			{
				Context.RHITransitionResources(EResourceTransitionAccess::EWritable, &Texture, 1);
			}

			*Found = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		if (bNeedsClearValues)
		{
			const FLinearColor& ClearColor = Texture->HasClearValue() ? Texture->GetClearColor() : FLinearColor::Black;
			ClearValues[ClearValueIndex].color.float32[0] = ClearColor.R;
			ClearValues[ClearValueIndex].color.float32[1] = ClearColor.G;
			ClearValues[ClearValueIndex].color.float32[2] = ClearColor.B;
			ClearValues[ClearValueIndex].color.float32[3] = ClearColor.A;
			++ClearValueIndex;
			if (Surface.GetNumSamples() > 1)
			{
				++ClearValueIndex;
			}
		}
	}

	FRHITexture* DSTexture = RPInfo.DepthStencilRenderTarget.DepthStencilTarget;
	if (DSTexture)
	{
		FVulkanSurface& Surface = FVulkanTextureBase::Cast(DSTexture)->Surface;
		VkImageLayout& DSLayout = Layouts.FindOrAdd(Surface.Image);
		FExclusiveDepthStencil RequestedDSAccess = RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil;
		VkImageLayout FinalLayout = VulkanRHI::GetDepthStencilLayout(RequestedDSAccess, InDevice);

		// Check if we need to transition the depth stencil texture(s) based on the current layout and the requested access mode for the render target
		if (DSLayout != FinalLayout)
		{
			int32 BarrierIndex = Barrier.AddImageBarrier(Surface.Image, Surface.GetFullAspectMask(), 1);
			VulkanRHI::EImageLayoutBarrier SrcLayout = VulkanRHI::GetImageLayoutFromVulkanLayout(DSLayout);
			VulkanRHI::EImageLayoutBarrier DstLayout = VulkanRHI::GetImageLayoutFromVulkanLayout(FinalLayout);
			Barrier.SetTransition(BarrierIndex, SrcLayout, DstLayout);
			DSLayout = FinalLayout;
		}

		if (DSTexture->HasClearValue() && bNeedsClearValues)
		{
			float Depth = 0;
			uint32 Stencil = 0;
			DSTexture->GetDepthStencilClearValue(Depth, Stencil);
			ClearValues[ClearValueIndex].depthStencil.depth = Depth;
			ClearValues[ClearValueIndex].depthStencil.stencil = Stencil;
			++ClearValueIndex;
		}
	}

	ensure(ClearValueIndex <= RenderPass->GetNumUsedClearValues());

	Barrier.Execute(CmdBuffer);

	CmdBuffer->BeginRenderPass(RenderPass->GetLayout(), RenderPass, Framebuffer, ClearValues);

	{
		const VkExtent3D& Extents = RTLayout.GetExtent3D();
		Context.GetPendingGfxState()->SetViewport(0, 0, 0, Extents.width, Extents.height, 1);
	}

	CurrentFramebuffer = Framebuffer;
	CurrentRenderPass = RenderPass;
	bInsideRealRenderPass = true;
}

void FTransitionAndLayoutManager::EndRealRenderPass(FVulkanCmdBuffer* CmdBuffer)
{
	check(CurrentRenderPass);
	check(bInsideRealRenderPass);
	CmdBuffer->EndRenderPass();

	if (GenerateMipsInfo.bInsideGenerateMips)
	{
		FPendingBarrier Barrier;
		for (int32 Index = 0; Index < GenerateMipsInfo.NumRenderTargets; ++Index)
		{
			ensure(GenerateMipsInfo.Target[Index].Layouts[GenerateMipsInfo.CurrentSlice][GenerateMipsInfo.CurrentMip] == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

			// Transition to readable
			int32 BarrierIndex = Barrier.AddImageBarrier(GenerateMipsInfo.Target[Index].CurrentImage, VK_IMAGE_ASPECT_COLOR_BIT, 1);
			VkImageSubresourceRange& Range = Barrier.GetSubresource(BarrierIndex);
			Range.baseMipLevel = GenerateMipsInfo.CurrentMip;
			Range.baseArrayLayer = GenerateMipsInfo.CurrentSlice;
			Barrier.SetTransition(BarrierIndex, EImageLayoutBarrier::ColorAttachment, EImageLayoutBarrier::PixelShaderRead);
			// This could really be ignored...
			GenerateMipsInfo.Target[Index].Layouts[GenerateMipsInfo.CurrentSlice][GenerateMipsInfo.CurrentMip] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
		Barrier.Execute(CmdBuffer);

		if (GenerateMipsInfo.bLastMip)
		{
			GenerateMipsInfo.Reset();
		}
	}

	CurrentRenderPass = nullptr;
	bInsideRealRenderPass = false;

	VulkanRHI::DebugHeavyWeightBarrier(CmdBuffer->GetHandle(), 1);
}

void FTransitionAndLayoutManager::NotifyDeletedRenderTarget(FVulkanDevice& InDevice, VkImage Image)
{
	for (auto It = Framebuffers.CreateIterator(); It; ++It)
	{
		FFramebufferList* List = It->Value;
		for (int32 Index = List->Framebuffer.Num() - 1; Index >= 0; --Index)
		{
			FVulkanFramebuffer* Framebuffer = List->Framebuffer[Index];
			if (Framebuffer->ContainsRenderTarget(Image))
			{
				List->Framebuffer.RemoveAtSwap(Index, 1, false);
				Framebuffer->Destroy(InDevice);

				if (Framebuffer == CurrentFramebuffer)
				{
					CurrentFramebuffer = nullptr;
				}

				delete Framebuffer;
			}
		}

		if (List->Framebuffer.Num() == 0)
		{
			delete List;
			It.RemoveCurrent();
		}
	}
}

VkImageLayout FTransitionAndLayoutManager::FindOrAddLayout(VkImage Image, VkImageLayout LayoutIfNotFound)
{
	VkImageLayout* Found = Layouts.Find(Image);
	if (Found)
	{
		return *Found;
	}

	Layouts.Add(Image, LayoutIfNotFound);
	return LayoutIfNotFound;
}

VkImageLayout& FTransitionAndLayoutManager::FindOrAddLayoutRW(VkImage Image, VkImageLayout LayoutIfNotFound)
{
	VkImageLayout* Found = Layouts.Find(Image);
	if (Found)
	{
		return *Found;
	}
	return Layouts.Add(Image, LayoutIfNotFound);
}

void FTransitionAndLayoutManager::TransitionResource(FVulkanCmdBuffer* CmdBuffer, FVulkanSurface& Surface, VulkanRHI::EImageLayoutBarrier DestLayout)
{
	VkImageLayout* FoundLayout = Layouts.Find(Surface.Image);
	VkImageLayout VulkanDestLayout = VulkanRHI::GetImageLayout(DestLayout);
	if (FoundLayout)
	{
		if (*FoundLayout != VulkanDestLayout)
		{
			VulkanRHI::EImageLayoutBarrier SourceLayout = GetImageLayoutFromVulkanLayout(*FoundLayout);
			VulkanRHI::ImagePipelineBarrier(CmdBuffer->GetHandle(), Surface.Image, SourceLayout, DestLayout, VulkanRHI::SetupImageSubresourceRange(Surface.GetFullAspectMask()));
			*FoundLayout = VulkanDestLayout;
		}
	}
	else
	{
		VulkanRHI::ImagePipelineBarrier(CmdBuffer->GetHandle(), Surface.Image, EImageLayoutBarrier::Undefined, DestLayout, VulkanRHI::SetupImageSubresourceRange(Surface.GetFullAspectMask()));
		Layouts.Add(Surface.Image, VulkanDestLayout);
	}
}

void FVulkanCommandListContext::RHISetRenderTargets(uint32 NumSimultaneousRenderTargets, const FRHIRenderTargetView* NewRenderTargets, const FRHIDepthRenderTargetView* NewDepthStencilTarget)
{
	FRHIDepthRenderTargetView DepthView;
	if (NewDepthStencilTarget)
	{
		DepthView = *NewDepthStencilTarget;
	}
	else
	{
		DepthView = FRHIDepthRenderTargetView(nullptr, ERenderTargetLoadAction::ENoAction, ERenderTargetStoreAction::ENoAction, ERenderTargetLoadAction::ENoAction, ERenderTargetStoreAction::ENoAction);
	}

	if (NumSimultaneousRenderTargets == 1 && (!NewRenderTargets || !NewRenderTargets->Texture))
	{
		--NumSimultaneousRenderTargets;
	}

	FRHISetRenderTargetsInfo RenderTargetsInfo(NumSimultaneousRenderTargets, NewRenderTargets, DepthView);
	
	RHISetRenderTargetsAndClear(RenderTargetsInfo);
}

// Find out whether we can re-use current renderpass instead of starting new one
static bool IsCompatibleRenderPass(FVulkanRenderPass* CurrentRenderPass, FVulkanRenderPass* NewRenderPass)
{
	if (CurrentRenderPass == nullptr || NewRenderPass == nullptr)
	{
		return false;
	}

	const FVulkanRenderTargetLayout& CurrentLayout = CurrentRenderPass->GetLayout();
	const FVulkanRenderTargetLayout& NewLayout = NewRenderPass->GetLayout();
	
	if (CurrentLayout.GetRenderPassCompatibleHash() != NewLayout.GetRenderPassCompatibleHash())
	{
		return false;
	}
	
	int32 NumDesc = CurrentLayout.GetNumAttachmentDescriptions();
	check(NumDesc == NewLayout.GetNumAttachmentDescriptions());

	const VkAttachmentDescription* CurrentDescriptions = CurrentLayout.GetAttachmentDescriptions();
	const VkAttachmentDescription* NewDescriptions = NewLayout.GetAttachmentDescriptions();
	for (int32 i = 0; i < NumDesc; ++i)
	{
		const VkAttachmentDescription& CurrentDesc = CurrentDescriptions[i];
		const VkAttachmentDescription& NewDesc = NewDescriptions[i];
		
		// New render-pass wants a clear target
		if (NewDesc.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR || NewDesc.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
		{
			return false;
		}

		// New render-pass wants to store, while current does not
		if ((NewDesc.storeOp == VK_ATTACHMENT_STORE_OP_STORE && CurrentDesc.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE) ||
			(NewDesc.stencilStoreOp == VK_ATTACHMENT_STORE_OP_STORE && CurrentDesc.stencilStoreOp == VK_ATTACHMENT_STORE_OP_DONT_CARE))
		{
			return false;
		}

		if (NewDesc.finalLayout != CurrentDesc.finalLayout)
		{
			return false;
		}
	}
	
	return true;
}

void FVulkanCommandListContext::RHISetRenderTargetsAndClear(const FRHISetRenderTargetsInfo& RenderTargetsInfo)
{
	FVulkanRenderTargetLayout RTLayout(*Device, RenderTargetsInfo);

	TransitionAndLayoutManager.GenerateMipsInfo.Reset();

	FVulkanRenderPass* RenderPass = nullptr;
	FVulkanFramebuffer* Framebuffer = nullptr;

	if (RTLayout.GetExtent2D().width != 0 && RTLayout.GetExtent2D().height != 0)
	{
		RenderPass = TransitionAndLayoutManager.GetOrCreateRenderPass(*Device, RTLayout);
		Framebuffer = TransitionAndLayoutManager.GetOrCreateFramebuffer(*Device, RenderTargetsInfo, RTLayout, RenderPass);
	}

	if (Framebuffer == TransitionAndLayoutManager.CurrentFramebuffer && RenderPass != nullptr && IsCompatibleRenderPass(TransitionAndLayoutManager.CurrentRenderPass, RenderPass))
	{
		return;
	}

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

	if (SafePointSubmit())
	{
		CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
	}

	if (RenderPass != nullptr && Framebuffer != nullptr)
	{
		if (RenderTargetsInfo.DepthStencilRenderTarget.Texture ||
			RenderTargetsInfo.NumColorRenderTargets > 1 ||
			((RenderTargetsInfo.NumColorRenderTargets == 1) && RenderTargetsInfo.ColorRenderTarget[0].Texture))
		{
			TransitionAndLayoutManager.BeginEmulatedRenderPass(*this, *Device, CmdBuffer, RenderTargetsInfo, RTLayout, RenderPass, Framebuffer);
		}
		else
		{
			ensureMsgf(0, TEXT("RenderPass not started! Bad combination of values? Depth %p #Color %d Color0 %p"), (void*)RenderTargetsInfo.DepthStencilRenderTarget.Texture, RenderTargetsInfo.NumColorRenderTargets, (void*)RenderTargetsInfo.ColorRenderTarget[0].Texture);
		}
	}

}

void FVulkanCommandListContext::RHICopyToResolveTarget(FRHITexture* SourceTextureRHI, FRHITexture* DestTextureRHI, const FResolveParams& InResolveParams)
{
	//FRCLog::Printf(FString::Printf(TEXT("RHICopyToResolveTarget")));
	if (!SourceTextureRHI || !DestTextureRHI)
	{
		// no need to do anything (silently ignored)
		return;
	}

	RHITransitionResources(EResourceTransitionAccess::EReadable, &SourceTextureRHI, 1);

	auto CopyImage = [](FTransitionAndLayoutManager& InRenderPassState, FVulkanCmdBuffer* InCmdBuffer, FVulkanSurface& SrcSurface, FVulkanSurface& DstSurface, uint32 SrcNumLayers, uint32 DstNumLayers, const FResolveParams& ResolveParams)
	{
		VkImageLayout SrcLayout = InRenderPassState.FindLayoutChecked(SrcSurface.Image);
		bool bIsDepth = DstSurface.IsDepthOrStencilAspect();
		VkImageLayout& DstLayout = InRenderPassState.FindOrAddLayoutRW(DstSurface.Image, VK_IMAGE_LAYOUT_UNDEFINED);
		bool bCopyIntoCPUReadable = (DstSurface.UEFlags & TexCreate_CPUReadback) == TexCreate_CPUReadback;

		check(InCmdBuffer->IsOutsideRenderPass());
		VkCommandBuffer CmdBuffer = InCmdBuffer->GetHandle();

		VkImageSubresourceRange SrcRange;
		SrcRange.aspectMask = SrcSurface.GetFullAspectMask();
		SrcRange.baseMipLevel = ResolveParams.MipIndex;
		SrcRange.levelCount = 1;
		SrcRange.baseArrayLayer = ResolveParams.SourceArrayIndex * SrcNumLayers + (SrcNumLayers == 6 ? ResolveParams.CubeFace : 0);
		SrcRange.layerCount = 1;

		VkImageSubresourceRange DstRange;
		DstRange.aspectMask = DstSurface.GetFullAspectMask();
		DstRange.baseMipLevel = ResolveParams.MipIndex;
		DstRange.levelCount = 1;
		DstRange.baseArrayLayer = ResolveParams.DestArrayIndex * DstNumLayers + (DstNumLayers == 6 ? ResolveParams.CubeFace : 0);
		DstRange.layerCount = 1;

		VulkanSetImageLayout(CmdBuffer, SrcSurface.Image, SrcLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, SrcRange);
		VulkanSetImageLayout(CmdBuffer, DstSurface.Image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, DstRange);

		VkImageCopy Region;
		FMemory::Memzero(Region);
		ensure(SrcSurface.Width == DstSurface.Width && SrcSurface.Height == DstSurface.Height);
		Region.extent.width = FMath::Max(1u, SrcSurface.Width>> ResolveParams.MipIndex);
		Region.extent.height = FMath::Max(1u, SrcSurface.Height >> ResolveParams.MipIndex);
		Region.extent.depth = 1;
		Region.srcSubresource.aspectMask = SrcSurface.GetFullAspectMask();
		Region.srcSubresource.baseArrayLayer = SrcRange.baseArrayLayer;
		Region.srcSubresource.layerCount = 1;
		Region.srcSubresource.mipLevel = ResolveParams.MipIndex;
		Region.dstSubresource.aspectMask = DstSurface.GetFullAspectMask();
		Region.dstSubresource.baseArrayLayer = DstRange.baseArrayLayer;
		Region.dstSubresource.layerCount = 1;
		Region.dstSubresource.mipLevel = ResolveParams.MipIndex;
		VulkanRHI::vkCmdCopyImage(CmdBuffer,
			SrcSurface.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			DstSurface.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &Region);

		VulkanSetImageLayout(CmdBuffer, SrcSurface.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, SrcLayout, SrcRange);
		if (bCopyIntoCPUReadable)
		{
			VulkanSetImageLayout(CmdBuffer, DstSurface.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, DstRange);
			DstLayout = VK_IMAGE_LAYOUT_GENERAL;
		}
		else
		{
			DstLayout = bIsDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			VulkanSetImageLayout(CmdBuffer, DstSurface.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, DstLayout, DstRange);
		}
	};

	FRHITexture2D* SourceTexture2D = SourceTextureRHI->GetTexture2D();
	FRHITexture2DArray* SourceTexture2DArray = SourceTextureRHI->GetTexture2DArray();
	FRHITexture3D* SourceTexture3D = SourceTextureRHI->GetTexture3D();
	FRHITextureCube* SourceTextureCube = SourceTextureRHI->GetTextureCube();
	FRHITexture2D* DestTexture2D = DestTextureRHI->GetTexture2D();
	FRHITexture2DArray* DestTexture2DArray = DestTextureRHI->GetTexture2DArray();
	FRHITexture3D* DestTexture3D = DestTextureRHI->GetTexture3D();
	FRHITextureCube* DestTextureCube = DestTextureRHI->GetTextureCube();
	FVulkanCmdBuffer* CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();

	if (SourceTexture2D && DestTexture2D)
	{
		FVulkanTexture2D* VulkanSourceTexture = (FVulkanTexture2D*)SourceTexture2D;
		FVulkanTexture2D* VulkanDestTexture = (FVulkanTexture2D*)DestTexture2D;
		if (VulkanSourceTexture->Surface.Image != VulkanDestTexture->Surface.Image) 
		{
			CopyImage(TransitionAndLayoutManager, CmdBuffer, VulkanSourceTexture->Surface, VulkanDestTexture->Surface, 1, 1, InResolveParams);
		}
	}
	else if (SourceTextureCube && DestTextureCube) 
	{
		FVulkanTextureCube* VulkanSourceTexture = (FVulkanTextureCube*)SourceTextureCube;
		FVulkanTextureCube* VulkanDestTexture = (FVulkanTextureCube*)DestTextureCube;
		if (VulkanSourceTexture->Surface.Image != VulkanDestTexture->Surface.Image) 
		{
			CopyImage(TransitionAndLayoutManager, CmdBuffer, VulkanSourceTexture->Surface, VulkanDestTexture->Surface, 6, 6, InResolveParams);
		}
	}
	else if (SourceTexture2D && DestTextureCube) 
	{
		FVulkanTexture2D* VulkanSourceTexture = (FVulkanTexture2D*)SourceTexture2D;
		FVulkanTextureCube* VulkanDestTexture = (FVulkanTextureCube*)DestTextureCube;
		if (VulkanSourceTexture->Surface.Image != VulkanDestTexture->Surface.Image) 
		{
			CopyImage(TransitionAndLayoutManager, CmdBuffer, VulkanSourceTexture->Surface, VulkanDestTexture->Surface, 1, 6, InResolveParams);
		}
	}
	else if (SourceTexture3D && DestTexture3D) 
	{
		FVulkanTexture3D* VulkanSourceTexture = (FVulkanTexture3D*)SourceTexture3D;
		FVulkanTexture3D* VulkanDestTexture = (FVulkanTexture3D*)DestTexture3D;
		if (VulkanSourceTexture->Surface.Image != VulkanDestTexture->Surface.Image)
		{
			CopyImage(TransitionAndLayoutManager, CmdBuffer, VulkanSourceTexture->Surface, VulkanDestTexture->Surface, 1, 1, InResolveParams);
		}
	}
	else if (SourceTexture2DArray && DestTexture2DArray)
	{
		FVulkanTexture2DArray* VulkanSourceTexture = (FVulkanTexture2DArray*)SourceTexture2DArray;
		FVulkanTexture2DArray* VulkanDestTexture = (FVulkanTexture2DArray*)DestTexture2DArray;
		if (VulkanSourceTexture->Surface.Image != VulkanDestTexture->Surface.Image)
		{
			CopyImage(TransitionAndLayoutManager, CmdBuffer, VulkanSourceTexture->Surface, VulkanDestTexture->Surface, VulkanDestTexture->GetSizeZ(), VulkanSourceTexture->GetSizeZ(), InResolveParams);
		}
	} 
	else 
	{
		checkf(false, TEXT("Using unsupported Resolve combination"));
	}
}


void FVulkanDynamicRHI::RHIReadSurfaceData(FRHITexture* TextureRHI, FIntRect Rect, TArray<FColor>& OutData, FReadSurfaceDataFlags InFlags)
{
	FRHITexture2D* TextureRHI2D = TextureRHI->GetTexture2D();
	check(TextureRHI2D);
	FVulkanTexture2D* Texture2D = (FVulkanTexture2D*)TextureRHI2D;
	uint32 NumPixels = (Rect.Max.X - Rect.Min.X) * (Rect.Max.Y - Rect.Min.Y);

	if (GIgnoreCPUReads == 2)
	{
		// Debug: Fill with CPU
		OutData.Empty(0);
		OutData.AddZeroed(NumPixels);
		return;
	}

	Device->PrepareForCPURead();

	FVulkanCommandListContext& ImmediateContext = Device->GetImmediateContext();

	FVulkanCmdBuffer* CmdBuffer = ImmediateContext.GetCommandBufferManager()->GetUploadCmdBuffer();

	ensure(Texture2D->Surface.StorageFormat == VK_FORMAT_R8G8B8A8_UNORM || Texture2D->Surface.StorageFormat == VK_FORMAT_B8G8R8A8_UNORM || Texture2D->Surface.StorageFormat == VK_FORMAT_R16G16B16A16_SFLOAT || Texture2D->Surface.StorageFormat == VK_FORMAT_A2B10G10R10_UNORM_PACK32 || Texture2D->Surface.StorageFormat == VK_FORMAT_R16G16B16A16_UNORM);
	bool bIs8Bpp = true;
	switch(Texture2D->Surface.StorageFormat)
	{
	case VK_FORMAT_R16G16B16A16_SFLOAT:
	case VK_FORMAT_R16G16B16A16_SNORM:
	case VK_FORMAT_R16G16B16A16_UINT:
	case VK_FORMAT_R16G16B16A16_SINT:
		bIs8Bpp = false;
		break;
	default:
		break;
	}

	const uint32 Size = NumPixels * sizeof(FColor) * (bIs8Bpp ? 2 : 1);
	VulkanRHI::FStagingBuffer* StagingBuffer = Device->GetStagingManager().AcquireBuffer(Size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
	if (GIgnoreCPUReads == 0)
	{
		VkBufferImageCopy CopyRegion;
		FMemory::Memzero(CopyRegion);
		uint32 MipLevel = InFlags.GetMip();
		uint32 SizeX = FMath::Max(TextureRHI2D->GetSizeX() >> MipLevel, 1u);
		uint32 SizeY = FMath::Max(TextureRHI2D->GetSizeY() >> MipLevel, 1u);
		//CopyRegion.bufferOffset = 0;
		CopyRegion.bufferRowLength = SizeX;
		CopyRegion.bufferImageHeight = SizeY;
		CopyRegion.imageSubresource.aspectMask = Texture2D->Surface.GetFullAspectMask();
		CopyRegion.imageSubresource.mipLevel = MipLevel;
		//CopyRegion.imageSubresource.baseArrayLayer = 0;
		CopyRegion.imageSubresource.layerCount = 1;
		CopyRegion.imageExtent.width = SizeX;
		CopyRegion.imageExtent.height = SizeY;
		CopyRegion.imageExtent.depth = 1;

		//#todo-rco: Multithreaded!
		VkImageLayout& CurrentLayout = Device->GetImmediateContext().FindOrAddLayoutRW(Texture2D->Surface.Image, VK_IMAGE_LAYOUT_UNDEFINED);
		bool bHadLayout = (CurrentLayout != VK_IMAGE_LAYOUT_UNDEFINED);
		if (CurrentLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
		{
			VulkanSetImageLayoutAllMips(CmdBuffer->GetHandle(), Texture2D->Surface.Image, CurrentLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		}

		VulkanRHI::vkCmdCopyImageToBuffer(CmdBuffer->GetHandle(), Texture2D->Surface.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, StagingBuffer->GetHandle(), 1, &CopyRegion);
		if (bHadLayout && CurrentLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
		{
			VulkanSetImageLayoutAllMips(CmdBuffer->GetHandle(), Texture2D->Surface.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, CurrentLayout);
		}
		else
		{
			CurrentLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		}
	}
	else
	{
		VulkanRHI::vkCmdFillBuffer(CmdBuffer->GetHandle(), StagingBuffer->GetHandle(), 0, Size, (uint32)0xffffffff);
	}

	VkBufferMemoryBarrier Barrier;
	ensure(StagingBuffer->GetSize() >= Size);
	//#todo-rco: Change offset if reusing a buffer suballocation
	VulkanRHI::SetupAndZeroBufferBarrier(Barrier, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT, StagingBuffer->GetHandle(), 0/*StagingBuffer->GetOffset()*/, Size);
	VulkanRHI::vkCmdPipelineBarrier(CmdBuffer->GetHandle(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &Barrier, 0, nullptr);

	// Force upload
	ImmediateContext.GetCommandBufferManager()->SubmitUploadCmdBuffer();
	Device->WaitUntilIdle();

/*
	VkMappedMemoryRange MappedRange;
	ZeroVulkanStruct(MappedRange, VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE);
	MappedRange.memory = StagingBuffer->GetDeviceMemoryHandle();
	MappedRange.offset = StagingBuffer->GetAllocationOffset();
	MappedRange.size = Size;
	VulkanRHI::vkInvalidateMappedMemoryRanges(Device->GetInstanceHandle(), 1, &MappedRange);
*/
	StagingBuffer->InvalidateMappedMemory();

	OutData.SetNum(NumPixels);
	FColor* Dest = OutData.GetData();

	uint32 DestWidth  = Rect.Max.X - Rect.Min.X;
	uint32 DestHeight = Rect.Max.Y - Rect.Min.Y;

	if (Texture2D->Surface.StorageFormat == VK_FORMAT_R16G16B16A16_SFLOAT)
	{
		uint32 PixelByteSize = 8u;
		uint8* In = (uint8*)StagingBuffer->GetMappedPointer() + (Rect.Min.Y * TextureRHI2D->GetSizeX() + Rect.Min.X) * PixelByteSize;
		uint32 SrcPitch = TextureRHI2D->GetSizeX() * PixelByteSize;
		ConvertRawR16G16B16A16FDataToFColor(DestWidth, DestHeight, In, SrcPitch, Dest, false);
	}
	else if (Texture2D->Surface.StorageFormat == VK_FORMAT_A2B10G10R10_UNORM_PACK32)
	{
		uint32 PixelByteSize = 4u;
		uint8* In = (uint8*)StagingBuffer->GetMappedPointer() + (Rect.Min.Y * TextureRHI2D->GetSizeX() + Rect.Min.X) * PixelByteSize;
		uint32 SrcPitch = TextureRHI2D->GetSizeX() * PixelByteSize;
		ConvertRawR10G10B10A2DataToFColor(DestWidth, DestHeight, In, SrcPitch, Dest);
	}
	else if (Texture2D->Surface.StorageFormat == VK_FORMAT_R8G8B8A8_UNORM)
	{
		uint32 PixelByteSize = 4u;
		uint8* In = (uint8*)StagingBuffer->GetMappedPointer() + (Rect.Min.Y * TextureRHI2D->GetSizeX() + Rect.Min.X) * PixelByteSize;
		uint32 SrcPitch = TextureRHI2D->GetSizeX() * PixelByteSize;
		ConvertRawR8G8B8A8DataToFColor(DestWidth, DestHeight, In, SrcPitch, Dest);
	}
	else if (Texture2D->Surface.StorageFormat == VK_FORMAT_R16G16B16A16_UNORM)
	{
		uint32 PixelByteSize = 8u;
		uint8* In = (uint8*)StagingBuffer->GetMappedPointer() + (Rect.Min.Y * TextureRHI2D->GetSizeX() + Rect.Min.X) * PixelByteSize;
		uint32 SrcPitch = TextureRHI2D->GetSizeX() * PixelByteSize;
		ConvertRawR16G16B16A16DataToFColor(DestWidth, DestHeight, In, SrcPitch, Dest);
	}
	else if (Texture2D->Surface.StorageFormat == VK_FORMAT_B8G8R8A8_UNORM)
	{
		uint32 PixelByteSize = 4u;
		uint8* In = (uint8*)StagingBuffer->GetMappedPointer() + (Rect.Min.Y * TextureRHI2D->GetSizeX() + Rect.Min.X) * PixelByteSize;
		uint32 SrcPitch = TextureRHI2D->GetSizeX() * PixelByteSize;
		ConvertRawB8G8R8A8DataToFColor(DestWidth, DestHeight, In, SrcPitch, Dest);
	}

	Device->GetStagingManager().ReleaseBuffer(CmdBuffer, StagingBuffer);
	ImmediateContext.GetCommandBufferManager()->PrepareForNewActiveCommandBuffer();
}

void FVulkanDynamicRHI::RHIReadSurfaceData(FRHITexture* TextureRHI, FIntRect Rect, TArray<FLinearColor>& OutData, FReadSurfaceDataFlags InFlags)
{
	TArray<FColor> FromColorData;
	RHIReadSurfaceData(TextureRHI, Rect, FromColorData, InFlags);
	for (FColor& From : FromColorData)
	{
		OutData.Emplace(FLinearColor(From));
	}
}

void FVulkanDynamicRHI::RHIMapStagingSurface(FRHITexture* TextureRHI, FRHIGPUFence* FenceRHI, void*& OutData, int32& OutWidth, int32& OutHeight, uint32 GPUIndex)
{
	FRHITexture2D* TextureRHI2D = TextureRHI->GetTexture2D();
	check(TextureRHI2D);
	FVulkanTexture2D* Texture2D = ResourceCast(TextureRHI2D);

	if (FenceRHI && !FenceRHI->Poll())
	{
		Device->SubmitCommandsAndFlushGPU();
		FVulkanGPUFence* Fence = ResourceCast(FenceRHI);
		Device->GetImmediateContext().GetCommandBufferManager()->WaitForCmdBuffer(Fence->GetCmdBuffer());
	}

	int32 Pitch = Texture2D->GetSizeX();
	if (ensureMsgf(Texture2D->Surface.GetTiling() == VK_IMAGE_TILING_LINEAR, TEXT("RHIMapStagingSurface() called with a %d x %d texture in non-linear tiling %d, the result will likely be garbled."), static_cast<int32>(Texture2D->GetSizeX()), static_cast<int32>(Texture2D->GetSizeY()), static_cast<int32>(Texture2D->Surface.GetTiling())))
	{
		// Pitch can be only retrieved from linear textures.
		VkImageSubresource ImageSubResource;
		FMemory::Memzero(ImageSubResource);

		ImageSubResource.aspectMask = Texture2D->Surface.GetFullAspectMask();
		ImageSubResource.mipLevel = 0;
		ImageSubResource.arrayLayer = 0;

		VkSubresourceLayout SubResourceLayout;
		VulkanRHI::vkGetImageSubresourceLayout(Device->GetInstanceHandle(), Texture2D->Surface.Image, &ImageSubResource, &SubResourceLayout);

		int32 BitsPerPixel = (int32)GetNumBitsPerPixel(Texture2D->Surface.StorageFormat);
		int32 RowPitchBits = SubResourceLayout.rowPitch * 8;
		Pitch = RowPitchBits / BitsPerPixel;
	}

	OutWidth = Pitch;
	OutHeight = Texture2D->GetSizeY();

	OutData = Texture2D->Surface.GetMappedPointer();
	Texture2D->Surface.InvalidateMappedMemory();
}

void FVulkanDynamicRHI::RHIUnmapStagingSurface(FRHITexture* TextureRHI, uint32 GPUIndex)
{
}

void FVulkanDynamicRHI::RHIReadSurfaceFloatData(FRHITexture* TextureRHI, FIntRect Rect, TArray<FFloat16Color>& OutData, ECubeFace CubeFace,int32 ArrayIndex,int32 MipIndex)
{
	auto DoCopyFloat = [](FVulkanDevice* InDevice, FVulkanCmdBuffer* InCmdBuffer, const FVulkanSurface& Surface, uint32 InMipIndex, uint32 SrcBaseArrayLayer, FIntRect InRect, TArray<FFloat16Color>& OutputData)
	{
		ensure(Surface.StorageFormat == VK_FORMAT_R16G16B16A16_SFLOAT);

		uint32 NumPixels = (Surface.Width >> InMipIndex) * (Surface.Height >> InMipIndex);
		const uint32 Size = NumPixels * sizeof(FFloat16Color);
		VulkanRHI::FStagingBuffer* StagingBuffer = InDevice->GetStagingManager().AcquireBuffer(Size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

		if (GIgnoreCPUReads == 0)
		{
			VkBufferImageCopy CopyRegion;
			FMemory::Memzero(CopyRegion);
			//Region.bufferOffset = 0;
			CopyRegion.bufferRowLength = Surface.Width >> InMipIndex;
			CopyRegion.bufferImageHeight = Surface.Height >> InMipIndex;
			CopyRegion.imageSubresource.aspectMask = Surface.GetFullAspectMask();
			CopyRegion.imageSubresource.mipLevel = InMipIndex;
			CopyRegion.imageSubresource.baseArrayLayer = SrcBaseArrayLayer;
			CopyRegion.imageSubresource.layerCount = 1;
			CopyRegion.imageExtent.width = Surface.Width >> InMipIndex;
			CopyRegion.imageExtent.height = Surface.Height >> InMipIndex;
			CopyRegion.imageExtent.depth = 1;

			//#todo-rco: Multithreaded!
			VkImageLayout& CurrentLayout = InDevice->GetImmediateContext().FindOrAddLayoutRW(Surface.Image, VK_IMAGE_LAYOUT_UNDEFINED);
			bool bHadLayout = (CurrentLayout != VK_IMAGE_LAYOUT_UNDEFINED);
			if (CurrentLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
			{
				VulkanSetImageLayoutSimple(InCmdBuffer->GetHandle(), Surface.Image, CurrentLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
			}

			VulkanRHI::vkCmdCopyImageToBuffer(InCmdBuffer->GetHandle(), Surface.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, StagingBuffer->GetHandle(), 1, &CopyRegion);

			if (bHadLayout && CurrentLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
			{
				VulkanSetImageLayoutSimple(InCmdBuffer->GetHandle(), Surface.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, CurrentLayout);
			}
			else
			{
				CurrentLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			}
		}
		else
		{
			VulkanRHI::vkCmdFillBuffer(InCmdBuffer->GetHandle(), StagingBuffer->GetHandle(), 0, Size, (FFloat16(1.0).Encoded << 16) + FFloat16(1.0).Encoded);
		}

		VkBufferMemoryBarrier Barrier;
		// the staging buffer size may be bigger then the size due to alignment, etc. but it must not be smaller!
		ensure(StagingBuffer->GetSize() >= Size);
		//#todo-rco: Change offset if reusing a buffer suballocation
		VulkanRHI::SetupAndZeroBufferBarrier(Barrier, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT, StagingBuffer->GetHandle(), 0/*StagingBuffer->GetOffset()*/, StagingBuffer->GetSize());
		VulkanRHI::vkCmdPipelineBarrier(InCmdBuffer->GetHandle(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &Barrier, 0, nullptr);

		// Force upload
		InDevice->GetImmediateContext().GetCommandBufferManager()->SubmitUploadCmdBuffer();
		InDevice->WaitUntilIdle();

		StagingBuffer->InvalidateMappedMemory();

		uint32 OutWidth = InRect.Max.X - InRect.Min.X;
		uint32 OutHeight= InRect.Max.Y - InRect.Min.Y;
		OutputData.SetNum(OutWidth * OutHeight);
		uint32 OutIndex = 0;
		FFloat16Color* Dest = OutputData.GetData();
		for (int32 Row = InRect.Min.Y; Row < InRect.Max.Y; ++Row)
		{
			FFloat16Color* Src = (FFloat16Color*)StagingBuffer->GetMappedPointer() + Row * (Surface.Width >> InMipIndex) + InRect.Min.X;
			for (int32 Col = InRect.Min.X; Col < InRect.Max.X; ++Col)
			{
				OutputData[OutIndex++] = *Src++;
			}
		}
		InDevice->GetStagingManager().ReleaseBuffer(InCmdBuffer, StagingBuffer);
	};

	if (GIgnoreCPUReads == 2)
	{
		// Debug: Fill with CPU
		uint32 NumPixels = 0;
		if (TextureRHI->GetTextureCube())
		{
			FRHITextureCube* TextureRHICube = TextureRHI->GetTextureCube();
			FVulkanTextureCube* TextureCube = (FVulkanTextureCube*)TextureRHICube;
			NumPixels = (TextureCube->Surface.Width >> MipIndex) * (TextureCube->Surface.Height >> MipIndex);
		}
		else
		{
			FRHITexture2D* TextureRHI2D = TextureRHI->GetTexture2D();
			check(TextureRHI2D);
			FVulkanTexture2D* Texture2D = (FVulkanTexture2D*)TextureRHI2D;
			NumPixels = (Texture2D->Surface.Width >> MipIndex) * (Texture2D->Surface.Height >> MipIndex);
		}

		OutData.Empty(0);
		OutData.AddZeroed(NumPixels);
	}
	else
	{
		Device->PrepareForCPURead();

		FVulkanCmdBuffer* CmdBuffer = Device->GetImmediateContext().GetCommandBufferManager()->GetUploadCmdBuffer();
		if (TextureRHI->GetTextureCube())
		{
			FRHITextureCube* TextureRHICube = TextureRHI->GetTextureCube();
			FVulkanTextureCube* TextureCube = (FVulkanTextureCube*)TextureRHICube;
			DoCopyFloat(Device, CmdBuffer, TextureCube->Surface, MipIndex, CubeFace + 6 * ArrayIndex, Rect, OutData);
		}
		else
		{
			FRHITexture2D* TextureRHI2D = TextureRHI->GetTexture2D();
			check(TextureRHI2D);
			FVulkanTexture2D* Texture2D = (FVulkanTexture2D*)TextureRHI2D;
			DoCopyFloat(Device, CmdBuffer, Texture2D->Surface, MipIndex, ArrayIndex, Rect, OutData);
		}
		Device->GetImmediateContext().GetCommandBufferManager()->PrepareForNewActiveCommandBuffer();
	}
}

void FVulkanDynamicRHI::RHIRead3DSurfaceFloatData(FRHITexture* TextureRHI,FIntRect InRect,FIntPoint ZMinMax,TArray<FFloat16Color>& OutData)
{
	FRHITexture3D* TextureRHI3D = TextureRHI->GetTexture3D();
	check(TextureRHI3D);
	FVulkanTexture3D* Texture3D = (FVulkanTexture3D*)TextureRHI3D;
	FVulkanSurface& Surface = Texture3D->Surface;

	uint32 SizeX = InRect.Width();
	uint32 SizeY = InRect.Height();
	uint32 SizeZ = ZMinMax.Y - ZMinMax.X;
	uint32 NumPixels = SizeX * SizeY * SizeZ;
	const uint32 Size = NumPixels * sizeof(FFloat16Color);

	// Allocate the output buffer.
	OutData.Reserve(Size);
	if (GIgnoreCPUReads == 2)
	{
		OutData.AddZeroed(Size);

		// Debug: Fill with CPU
		return;
	}

	Device->PrepareForCPURead();
	FVulkanCmdBuffer* CmdBuffer = Device->GetImmediateContext().GetCommandBufferManager()->GetUploadCmdBuffer();

	ensure(Surface.StorageFormat == VK_FORMAT_R16G16B16A16_SFLOAT);

	VulkanRHI::FStagingBuffer* StagingBuffer = Device->GetStagingManager().AcquireBuffer(Size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
	if (GIgnoreCPUReads == 0)
	{
		VkBufferImageCopy CopyRegion;
		FMemory::Memzero(CopyRegion);
		//Region.bufferOffset = 0;
		CopyRegion.bufferRowLength = Surface.Width;
		CopyRegion.bufferImageHeight = Surface.Height;
		CopyRegion.imageSubresource.aspectMask = Surface.GetFullAspectMask();
		//CopyRegion.imageSubresource.mipLevel = 0;
		//CopyRegion.imageSubresource.baseArrayLayer = 0;
		CopyRegion.imageSubresource.layerCount = 1;
		CopyRegion.imageOffset.x = InRect.Min.X;
		CopyRegion.imageOffset.y = InRect.Min.Y;
		CopyRegion.imageOffset.z = ZMinMax.X;
		CopyRegion.imageExtent.width = SizeX;
		CopyRegion.imageExtent.height = SizeY;
		CopyRegion.imageExtent.depth = SizeZ;

		//#todo-rco: Multithreaded!
		VkImageLayout& CurrentLayout = Device->GetImmediateContext().FindOrAddLayoutRW(Surface.Image, VK_IMAGE_LAYOUT_UNDEFINED);
		bool bHadLayout = (CurrentLayout != VK_IMAGE_LAYOUT_UNDEFINED);
		if (CurrentLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
		{
			VulkanSetImageLayoutSimple(CmdBuffer->GetHandle(), Surface.Image, CurrentLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		}

		VulkanRHI::vkCmdCopyImageToBuffer(CmdBuffer->GetHandle(), Surface.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, StagingBuffer->GetHandle(), 1, &CopyRegion);

		if (bHadLayout && CurrentLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
		{
			VulkanSetImageLayoutSimple(CmdBuffer->GetHandle(), Surface.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, CurrentLayout);
		}
		else
		{
			CurrentLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		}
	}
	else
	{
		VulkanRHI::vkCmdFillBuffer(CmdBuffer->GetHandle(), StagingBuffer->GetHandle(), 0, Size, (FFloat16(1.0).Encoded << 16) + FFloat16(1.0).Encoded);
	}

	VkBufferMemoryBarrier Barrier;
	// the staging buffer size may be bigger then the size due to alignment, etc. but it must not be smaller!
	ensure(StagingBuffer->GetSize() >= Size);
	//#todo-rco: Change offset if reusing a buffer suballocation
	VulkanRHI::SetupAndZeroBufferBarrier(Barrier, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT, StagingBuffer->GetHandle(), 0/*StagingBuffer->GetOffset()*/, StagingBuffer->GetSize());
	VulkanRHI::vkCmdPipelineBarrier(CmdBuffer->GetHandle(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &Barrier, 0, nullptr);

	// Force upload
	Device->GetImmediateContext().GetCommandBufferManager()->SubmitUploadCmdBuffer();
	Device->WaitUntilIdle();

	StagingBuffer->InvalidateMappedMemory();

	OutData.SetNum(NumPixels);
	FFloat16Color* Dest = OutData.GetData();
	for (int32 Layer = ZMinMax.X; Layer < ZMinMax.Y; ++Layer)
	{
		for (int32 Row = InRect.Min.Y; Row < InRect.Max.Y; ++Row)
		{
			FFloat16Color* Src = (FFloat16Color*)StagingBuffer->GetMappedPointer() + Layer * SizeX * SizeY + Row * Surface.Width + InRect.Min.X;
			for (int32 Col = InRect.Min.X; Col < InRect.Max.X; ++Col)
			{
				*Dest++ = *Src++;
			}
		}
	}
	FFloat16Color* End = OutData.GetData() + OutData.Num();
	checkf(Dest <= End, TEXT("Memory overwrite! Calculated total size %d: SizeX %d SizeY %d SizeZ %d; InRect(%d, %d, %d, %d) InZ(%d, %d)"),
		Size, SizeX, SizeY, SizeZ, InRect.Min.X, InRect.Min.Y, InRect.Max.X, InRect.Max.Y, ZMinMax.X, ZMinMax.Y);
	Device->GetStagingManager().ReleaseBuffer(CmdBuffer, StagingBuffer);
	Device->GetImmediateContext().GetCommandBufferManager()->PrepareForNewActiveCommandBuffer();
}

void FVulkanCommandListContext::RHITransitionResources(EResourceTransitionAccess TransitionType, EResourceTransitionPipeline TransitionPipeline, FRHIUnorderedAccessView** InUAVs, int32 NumUAVs, FRHIComputeFence* WriteComputeFenceRHI)
{
	FPendingTransition PendingTransition;
	if (NumUAVs > 0)
	{
		for (int32 Index = 0; Index < NumUAVs; ++Index)
		{
			if (InUAVs[Index])
			{
				PendingTransition.UAVs.Add(InUAVs[Index]);
			}
		}

		if (PendingTransition.UAVs.Num() > 0)
		{
			PendingTransition.TransitionType = TransitionType;
			PendingTransition.TransitionPipeline = TransitionPipeline;
			PendingTransition.WriteComputeFenceRHI = WriteComputeFenceRHI;
			TransitionResources(PendingTransition);
		}
	}
}

void FVulkanCommandListContext::RHITransitionResources(EResourceTransitionAccess TransitionType, FRHITexture** InTextures, int32 NumTextures)
{
	if (NumTextures > 0)
	{
		FPendingTransition PendingTransition;
		for (int32 Index = 0; Index < NumTextures; ++Index)
		{
			FRHITexture* RHITexture = InTextures[Index];
			if (RHITexture)
			{
				PendingTransition.Textures.Add(RHITexture);
				
				FVulkanTextureBase* VulkanTexture = FVulkanTextureBase::Cast(RHITexture);
				VulkanTexture->OnTransitionResource(*this, TransitionType);
			}
		}

		if (PendingTransition.Textures.Num() > 0)
		{
			PendingTransition.TransitionType = TransitionType;
			PendingTransition.TransitionPipeline = EResourceTransitionPipeline::EGfxToGfx; // Default to GfxToGfx which is ignored for textures
			TransitionResources(PendingTransition);
		}
	}
}

void FVulkanCommandListContext::RHITransitionResources(EResourceTransitionAccess TransitionType, EResourceTransitionPipeline TransitionPipeline, FRHITexture** InTextures, int32 NumTextures)
{
	if (NumTextures > 0)
	{
		FPendingTransition PendingTransition;
		for (int32 Index = 0; Index < NumTextures; ++Index)
		{
			FRHITexture* RHITexture = InTextures[Index];
			if (RHITexture)
			{
				PendingTransition.Textures.Add(RHITexture);

				FVulkanTextureBase* VulkanTexture = FVulkanTextureBase::Cast(RHITexture);
				VulkanTexture->OnTransitionResource(*this, TransitionType);
			}
		}

		if (PendingTransition.Textures.Num() > 0)
		{
			PendingTransition.TransitionType = TransitionType;
			PendingTransition.TransitionPipeline = TransitionPipeline;
			TransitionResources(PendingTransition);
		}
	}
}


bool FVulkanCommandListContext::FPendingTransition::GatherBarriers(FTransitionAndLayoutManager& InTransitionAndLayoutManager, TArray<VkBufferMemoryBarrier>& OutBufferBarriers, TArray<VkImageMemoryBarrier>& OutImageBarriers) const
{
	bool bEmpty = true;
	for (int32 Index = 0; Index < UAVs.Num(); ++Index)
	{
		FVulkanUnorderedAccessView* UAV = ResourceCast(UAVs[Index]);

		VkAccessFlags SrcAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, DestAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		switch (TransitionType)
		{
		case EResourceTransitionAccess::EWritable:
			SrcAccess = VK_ACCESS_SHADER_READ_BIT;
			DestAccess = VK_ACCESS_SHADER_WRITE_BIT;
			break;
		case EResourceTransitionAccess::EReadable:
			SrcAccess = VK_ACCESS_SHADER_WRITE_BIT;
			DestAccess = VK_ACCESS_SHADER_READ_BIT;
			break;
		case EResourceTransitionAccess::ERWSubResBarrier: //not optimal, but will have to do for now
		case EResourceTransitionAccess::ERWBarrier:
			SrcAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			DestAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			break;
		case EResourceTransitionAccess::ERWNoBarrier:
			//#todo-rco: Skip for now
			continue;
		default:
			ensure(0);
			break;
		}

		if (!UAV)
		{
			continue;
		}

		if (UAV->SourceVertexBuffer)
		{
			VkBufferMemoryBarrier& Barrier = OutBufferBarriers[OutBufferBarriers.AddUninitialized()];
			if (BUF_DrawIndirect == (UAV->SourceVertexBuffer->GetUEUsage() & BUF_DrawIndirect)) //for indirect read we translate Read INDIRECT_COMMAND_READ
			{
				if (DestAccess == VK_ACCESS_SHADER_READ_BIT)
				{
					DestAccess = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
				}
			}

			VulkanRHI::SetupAndZeroBufferBarrier(Barrier, SrcAccess, DestAccess, UAV->SourceVertexBuffer->GetHandle(), UAV->SourceVertexBuffer->GetOffset(), UAV->SourceVertexBuffer->GetSize());
			bEmpty = false;
		}
		else if (UAV->SourceTexture)
		{
			auto UpdateAccessFromLayout = [](VkAccessFlags Flags, VkImageLayout Layout) -> VkAccessFlags
			{
				switch(Layout)
				{
				case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
					return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				default:
					return Flags;
				}
			};
			VkImageMemoryBarrier& Barrier = OutImageBarriers[OutImageBarriers.AddUninitialized()];
			FVulkanTextureBase* VulkanTexture = FVulkanTextureBase::Cast(UAV->SourceTexture);
			VkImageLayout DestLayout = (TransitionPipeline == EResourceTransitionPipeline::EComputeToGfx || TransitionPipeline == EResourceTransitionPipeline::EGfxToGfx)
				? (VulkanTexture->Surface.IsDepthOrStencilAspect() ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
				: VK_IMAGE_LAYOUT_GENERAL;

			VkImageLayout& Layout = InTransitionAndLayoutManager.FindOrAddLayoutRW(VulkanTexture->Surface.Image, VK_IMAGE_LAYOUT_UNDEFINED);
			SrcAccess = UpdateAccessFromLayout(SrcAccess, Layout);
			DestAccess = UpdateAccessFromLayout(DestAccess, DestLayout);


			VulkanRHI::SetupAndZeroImageBarrierOLD(Barrier, VulkanTexture->Surface, SrcAccess, Layout, DestAccess, DestLayout);
			Layout = DestLayout;
			bEmpty = false;
		}
		else if (UAV->SourceStructuredBuffer)
		{
			VkBufferMemoryBarrier& Barrier = OutBufferBarriers[OutBufferBarriers.AddUninitialized()];
			
			if(BUF_DrawIndirect == (UAV->SourceStructuredBuffer->GetUEUsage() & BUF_DrawIndirect)) //for indirect read we translate Read INDIRECT_COMMAND_READ
			{
				if(SrcAccess == VK_ACCESS_SHADER_READ_BIT)
				{
					SrcAccess = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
				}
				else if(DestAccess == VK_ACCESS_SHADER_READ_BIT)
				{
					DestAccess = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
				}
			}
			VulkanRHI::SetupAndZeroBufferBarrier(Barrier, SrcAccess, DestAccess, UAV->SourceStructuredBuffer->GetHandle(), UAV->SourceStructuredBuffer->GetOffset(), UAV->SourceStructuredBuffer->GetSize());
			bEmpty = false;
		}
		else if (UAV->SourceIndexBuffer)
		{
			VkBufferMemoryBarrier& Barrier = OutBufferBarriers[OutBufferBarriers.AddUninitialized()];
			VulkanRHI::SetupAndZeroBufferBarrier(Barrier, SrcAccess, DestAccess, UAV->SourceIndexBuffer->GetHandle(), UAV->SourceIndexBuffer->GetOffset(), UAV->SourceIndexBuffer->GetSize());
			bEmpty = false;
		}
		else
		{
			ensure(0);
		}
	}

	return !bEmpty;
}



void FVulkanCommandListContext::RHITransitionResources(FExclusiveDepthStencil DepthStencilMode, FRHITexture* DepthTexture)
{
	static IConsoleVariable* CVarShowTransitions = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ProfileGPU.ShowTransitions"));
	bool bShowTransitionEvents = CVarShowTransitions->GetInt() != 0;
	FVulkanCmdBuffer* CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
	check(CmdBuffer->HasBegun());
	check(!TransitionAndLayoutManager.CurrentRenderPass);

	if (bShowTransitionEvents)
	{
		SCOPED_RHI_DRAW_EVENTF(*this, RHITransitionResourcesLoop, TEXT("To:%s"), *DepthTexture->GetName().ToString());
	}

	FVulkanTextureBase* VulkanTexture = FVulkanTextureBase::Cast(DepthTexture);

	VulkanRHI::FPendingBarrier Barrier;
	VkImageLayout& SrcLayout = TransitionAndLayoutManager.FindOrAddLayoutRW(VulkanTexture->Surface.Image, VK_IMAGE_LAYOUT_UNDEFINED);
	check(VulkanTexture->Surface.IsDepthOrStencilAspect())
	VkImageLayout DstLayout = SrcLayout;
	
	if(DepthStencilMode.IsDepthWrite())
	{
		if(DepthStencilMode.IsStencilWrite())
		{
			DstLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		}
		else
		{
			DstLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL;
		}
	}
	else
	{
		if (DepthStencilMode.IsStencilWrite())
		{
			DstLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL;
		}
		else
		{
			DstLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		}
	}
		
	int32 BarrierIndex = Barrier.AddImageBarrier(VulkanTexture->Surface.Image, VulkanTexture->Surface.GetFullAspectMask(), VulkanTexture->Surface.GetNumMips(), VulkanTexture->Surface.NumArrayLevels);
	Barrier.SetTransition(BarrierIndex, VulkanRHI::GetImageLayoutFromVulkanLayout(SrcLayout), VulkanRHI::GetImageLayoutFromVulkanLayout(DstLayout));
	SrcLayout = DstLayout;
	Barrier.Execute(CmdBuffer, false);
}


void FVulkanCommandListContext::TransitionResources(const FPendingTransition& PendingTransition)
{
	static IConsoleVariable* CVarShowTransitions = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ProfileGPU.ShowTransitions"));
	bool bShowTransitionEvents = CVarShowTransitions->GetInt() != 0;

	if (PendingTransition.Textures.Num() > 0)
	{
		ensure(IsImmediate() || Device->IsRealAsyncComputeContext(this));

		SCOPED_RHI_CONDITIONAL_DRAW_EVENTF(*this, RHITransitionResources, bShowTransitionEvents, TEXT("TransitionTo: %s: %i Textures"), *FResourceTransitionUtility::ResourceTransitionAccessStrings[(int32)PendingTransition.TransitionType], PendingTransition.Textures.Num());

		FVulkanCmdBuffer* CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
		check(CmdBuffer->HasBegun());

		//#todo-rco: Metadata is kind of a hack as decals do not have a read transition yet
		if (PendingTransition.TransitionType == EResourceTransitionAccess::EReadable || PendingTransition.TransitionType == EResourceTransitionAccess::EMetaData)
		{
			if (TransitionAndLayoutManager.CurrentRenderPass)
			{
				// If any of the textures are in the current render pass, we need to end it
				uint32 TexturesInsideRenderPass = 0;
				for (int32 Index = 0; Index < PendingTransition.Textures.Num(); ++Index)
				{
					FVulkanTextureBase* VulkanTexture = FVulkanTextureBase::Cast(PendingTransition.Textures[Index]);
					VkImage Image = VulkanTexture->Surface.Image;
					if (TransitionAndLayoutManager.CurrentFramebuffer->ContainsRenderTarget(Image))
					{
						++TexturesInsideRenderPass;
						bool bIsDepthStencil = VulkanTexture->Surface.IsDepthOrStencilAspect();
						VkImageLayout FoundLayout = TransitionAndLayoutManager.FindOrAddLayout(Image, VK_IMAGE_LAYOUT_UNDEFINED);
						VkImageLayout EnsureLayout = (bIsDepthStencil ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
						if (FoundLayout != VK_IMAGE_LAYOUT_UNDEFINED)
						{
							ensure(FoundLayout == EnsureLayout);
						}
					}
					else
					{
						//ensureMsgf(0, TEXT("Unable to transition texture as we're inside a render pass!"));
					}
				}

				if (TexturesInsideRenderPass > 0)
				{
					TransitionAndLayoutManager.EndEmulatedRenderPass(CmdBuffer);

					if (GVulkanSubmitAfterEveryEndRenderPass)
					{
						CommandBufferManager->SubmitActiveCmdBuffer();
						CommandBufferManager->PrepareForNewActiveCommandBuffer();
						CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
					}
				}
			}

			if (bShowTransitionEvents)
			{
				for (int32 Index = 0; Index < PendingTransition.Textures.Num(); ++Index)
				{
					SCOPED_RHI_DRAW_EVENTF(*this, RHITransitionResourcesLoop, TEXT("To:%i - %s"), Index, *PendingTransition.Textures[Index]->GetName().ToString());
				}
			}

			VulkanRHI::FPendingBarrier Barrier;
			for (int32 Index = 0; Index < PendingTransition.Textures.Num(); ++Index)
			{
				// If we are transitioning from compute we need additional pipeline stages
				// We can ignore the other transition types as their barriers are more explicitly handled elsewhere
				VkPipelineStageFlags SourceStage = 0, DestStage = 0;
				switch (PendingTransition.TransitionPipeline)
				{
				case EResourceTransitionPipeline::EGfxToGfx:
				case EResourceTransitionPipeline::EGfxToCompute:
					break;
				case EResourceTransitionPipeline::EComputeToGfx:
					SourceStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
					DestStage = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
					break;
				case EResourceTransitionPipeline::EComputeToCompute:
					SourceStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
					DestStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
					break;
				default:
					ensureMsgf(0, TEXT("Unknown transition pipeline %d"), (int32)PendingTransition.TransitionPipeline);
					break;
				}

				FVulkanTextureBase* VulkanTexture = FVulkanTextureBase::Cast(PendingTransition.Textures[Index]);
				VkImageLayout& SrcLayout = TransitionAndLayoutManager.FindOrAddLayoutRW(VulkanTexture->Surface.Image, VK_IMAGE_LAYOUT_UNDEFINED);
				bool bIsDepthStencil = VulkanTexture->Surface.IsDepthOrStencilAspect();
				// During HMD rendering we get a frame where nothing is rendered into the depth buffer, but CopyToTexture is still called...
				// ensure(SrcLayout != VK_IMAGE_LAYOUT_UNDEFINED || bIsDepthStencil);
				VkImageLayout DstLayout = bIsDepthStencil ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

				int32 BarrierIndex = Barrier.AddImageBarrier(VulkanTexture->Surface.Image, VulkanTexture->Surface.GetFullAspectMask(), VulkanTexture->Surface.GetNumMips(), VulkanTexture->Surface.NumArrayLevels);
				Barrier.SetTransition(BarrierIndex, VulkanRHI::GetImageLayoutFromVulkanLayout(SrcLayout), VulkanRHI::GetImageLayoutFromVulkanLayout(DstLayout));
				Barrier.AddStages(SourceStage, DestStage);
				SrcLayout = DstLayout;
			}
			//#todo-rco: Temp ensure disabled
			Barrier.Execute(CmdBuffer, false);
		}
		else if (PendingTransition.TransitionType == EResourceTransitionAccess::EWritable)
		{
			if (bShowTransitionEvents)
			{
				for (int32 i = 0; i < PendingTransition.Textures.Num(); ++i)
				{
					FRHITexture* RHITexture = PendingTransition.Textures[i];
					SCOPED_RHI_DRAW_EVENTF(*this, RHITransitionResourcesLoop, TEXT("To:%i - %s"), i, *PendingTransition.Textures[i]->GetName().ToString());
				}
			}

			VulkanRHI::FPendingBarrier Barrier;

			for (int32 Index = 0; Index < PendingTransition.Textures.Num(); ++Index)
			{
				FVulkanSurface& Surface = FVulkanTextureBase::Cast(PendingTransition.Textures[Index])->Surface;

				const VkImageAspectFlags AspectMask = Surface.GetFullAspectMask();
				VkImageLayout& SrcLayout = TransitionAndLayoutManager.FindOrAddLayoutRW(Surface.Image, VK_IMAGE_LAYOUT_UNDEFINED);
				
				VkImageLayout FinalLayout;
				if ((AspectMask & VK_IMAGE_ASPECT_COLOR_BIT) != 0)
				{
					FinalLayout = (Surface.UEFlags & TexCreate_RenderTargetable) ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
				}
				else
				{
					check(Surface.IsDepthOrStencilAspect());
					FinalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				}

				if (SrcLayout != FinalLayout)
				{
					int32 BarrierIndex = Barrier.AddImageBarrier(Surface.Image, AspectMask, Surface.GetNumMips(), Surface.NumArrayLevels);
					//todo: TransitionAndLayoutManager should use EImageLayoutBarrier type?
					Barrier.SetTransition(BarrierIndex, VulkanRHI::GetImageLayoutFromVulkanLayout(SrcLayout), VulkanRHI::GetImageLayoutFromVulkanLayout(FinalLayout));
					SrcLayout = FinalLayout;
				}
			}

			if (Barrier.NumImageBarriers() > 0)
			{
				//#todo-rco: Until render passes come online, assume writable means end render pass
				if (TransitionAndLayoutManager.CurrentRenderPass)
				{
					TransitionAndLayoutManager.EndEmulatedRenderPass(CmdBuffer);
					if (GVulkanSubmitAfterEveryEndRenderPass)
					{
						CommandBufferManager->SubmitActiveCmdBuffer();
						CommandBufferManager->PrepareForNewActiveCommandBuffer();
						CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
					}
				}

				Barrier.Execute(CmdBuffer);
			}
		}
		else if (PendingTransition.TransitionType == EResourceTransitionAccess::ERWSubResBarrier)
		{
			// This mode is only used for generating mipmaps only - old style
			if (CmdBuffer->IsInsideRenderPass())
			{
				check(PendingTransition.Textures.Num() == 1);
				TransitionAndLayoutManager.EndEmulatedRenderPass(CmdBuffer);

				if (GVulkanSubmitAfterEveryEndRenderPass)
				{
					CommandBufferManager->SubmitActiveCmdBuffer();
					CommandBufferManager->PrepareForNewActiveCommandBuffer();
					CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
				}
			}
		}
		else if (PendingTransition.TransitionType == EResourceTransitionAccess::EMetaData)
		{
			// Nothing to do here
		}
		else
		{
			ensure(0);
		}

		if (CommandBufferManager->GetActiveCmdBuffer()->IsOutsideRenderPass())
		{
			if (SafePointSubmit())
			{
				CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
			}
		}
	}
	else
	{
		SCOPED_RHI_CONDITIONAL_DRAW_EVENTF(*this, RHITransitionResources, bShowTransitionEvents, TEXT("TransitionTo: %s: %i UAVs"), *FResourceTransitionUtility::ResourceTransitionAccessStrings[(int32)PendingTransition.TransitionType], PendingTransition.UAVs.Num());

		const bool bIsRealAsyncComputeContext = Device->IsRealAsyncComputeContext(this);
		ensure(IsImmediate() || bIsRealAsyncComputeContext);
		check(PendingTransition.UAVs.Num() > 0);
		FVulkanCmdBuffer* CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
		TArray<VkBufferMemoryBarrier> BufferBarriers;
		TArray<VkImageMemoryBarrier> ImageBarriers;
		if (PendingTransition.GatherBarriers(TransitionAndLayoutManager, BufferBarriers, ImageBarriers))
		{
			// If we can support async compute, add this if writing a fence from the gfx context, or transitioning queues (as it requires transferring ownership of resources)
			if (Device->HasAsyncComputeQueue() &&
				(this == &Device->GetImmediateComputeContext() ||
					(PendingTransition.WriteComputeFenceRHI && (PendingTransition.TransitionPipeline == EResourceTransitionPipeline::EComputeToGfx || PendingTransition.TransitionPipeline == EResourceTransitionPipeline::EGfxToCompute))))
			{
				TransitionUAVResourcesTransferringOwnership(Device->GetImmediateContext(), Device->GetImmediateComputeContext(), PendingTransition.TransitionPipeline, BufferBarriers, ImageBarriers);
			}
			else
			{
				// 'Vanilla' transitions within the same queue
				VkPipelineStageFlags SourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, DestStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
				switch (PendingTransition.TransitionPipeline)
				{
				case EResourceTransitionPipeline::EGfxToGfx:
					SourceStage = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
					DestStage = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
					break;
				case EResourceTransitionPipeline::EGfxToCompute:
					SourceStage = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
					DestStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
					break;
				case EResourceTransitionPipeline::EComputeToGfx:
					SourceStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
					DestStage = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
					break;
				case EResourceTransitionPipeline::EComputeToCompute:
					SourceStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
					DestStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
					break;
				default:
					ensureMsgf(0, TEXT("Unknown transition pipeline %d"), (int32)PendingTransition.TransitionPipeline);
					break;
				}

				if (BufferBarriers.Num() && TransitionAndLayoutManager.CurrentRenderPass != nullptr)
				{
					TransitionAndLayoutManager.EndEmulatedRenderPass(CmdBuffer);

					if (GVulkanSubmitAfterEveryEndRenderPass)
					{
						CommandBufferManager->SubmitActiveCmdBuffer();
						CommandBufferManager->PrepareForNewActiveCommandBuffer();
						CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
					}
				}

				VulkanRHI::vkCmdPipelineBarrier(CmdBuffer->GetHandle(), SourceStage, DestStage, 0, 0, nullptr, BufferBarriers.Num(), BufferBarriers.GetData(), ImageBarriers.Num(), ImageBarriers.GetData());
			}
		}

		if (PendingTransition.WriteComputeFenceRHI)
		{
			// Can't do events between queues
			FVulkanComputeFence* Fence = ResourceCast(PendingTransition.WriteComputeFenceRHI);
			Fence->WriteCmd(CmdBuffer->GetHandle(), !bIsRealAsyncComputeContext);
		}
	}
}


void FVulkanCommandListContext::TransitionUAVResourcesTransferringOwnership(FVulkanCommandListContext& GfxContext, FVulkanCommandListContext& ComputeContext, 
	EResourceTransitionPipeline Pipeline, const TArray<VkBufferMemoryBarrier>& InBufferBarriers, const TArray<VkImageMemoryBarrier>& InImageBarriers)
{
	auto DoBarriers = [&InImageBarriers, &InBufferBarriers](uint32 SrcQueueIndex, uint32 DestQueueIndex, FVulkanCmdBuffer* SrcCmdBuffer, FVulkanCmdBuffer* DstCmdBuffer, VkPipelineStageFlags SrcStageFlags, VkPipelineStageFlags DestStageFlags)
	{
		TArray<VkBufferMemoryBarrier> BufferBarriers = InBufferBarriers;
		TArray<VkImageMemoryBarrier> ImageBarriers = InImageBarriers;

		// Release resources
		for (VkBufferMemoryBarrier& Barrier : BufferBarriers)
		{
			Barrier.dstAccessMask = 0;
			Barrier.srcQueueFamilyIndex = SrcQueueIndex;
			Barrier.dstQueueFamilyIndex = DestQueueIndex;
		}

		for (VkImageMemoryBarrier& Barrier : ImageBarriers)
		{
			Barrier.dstAccessMask = 0;
			Barrier.srcQueueFamilyIndex = SrcQueueIndex;
			Barrier.dstQueueFamilyIndex = DestQueueIndex;
		}

		VulkanRHI::vkCmdPipelineBarrier(SrcCmdBuffer->GetHandle(), SrcStageFlags, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, BufferBarriers.Num(), BufferBarriers.GetData(), ImageBarriers.Num(), ImageBarriers.GetData());

		// Now acquire and restore dstAccessMask
		for (VkBufferMemoryBarrier& Barrier : BufferBarriers)
		{
			Barrier.srcAccessMask = 0;
			size_t Index = &Barrier - &BufferBarriers[0];
			Barrier.dstAccessMask = InBufferBarriers[Index].dstAccessMask;
		}

		for (VkImageMemoryBarrier& Barrier : ImageBarriers)
		{
			Barrier.srcAccessMask = 0;
			size_t Index = &Barrier - &ImageBarriers[0];
			Barrier.dstAccessMask = ImageBarriers[Index].dstAccessMask;
		}

		VulkanRHI::vkCmdPipelineBarrier(DstCmdBuffer->GetHandle(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, DestStageFlags, 0, 0, nullptr, BufferBarriers.Num(), BufferBarriers.GetData(), ImageBarriers.Num(), ImageBarriers.GetData());
	};

	bool bComputeToGfx = Pipeline == EResourceTransitionPipeline::EComputeToGfx;
	ensure(bComputeToGfx || Pipeline == EResourceTransitionPipeline::EGfxToCompute);
	uint32 GfxQueueIndex = GfxContext.Device->GetGraphicsQueue()->GetFamilyIndex();
	uint32 ComputeQueueIndex = ComputeContext.Device->GetComputeQueue()->GetFamilyIndex();
	FVulkanCmdBuffer* GfxCmdBuffer = GfxContext.GetCommandBufferManager()->GetActiveCmdBuffer();
	if (!ComputeContext.GetCommandBufferManager()->HasPendingActiveCmdBuffer())
	{
		ComputeContext.GetCommandBufferManager()->PrepareForNewActiveCommandBuffer();
	}
	FVulkanCmdBuffer* ComputeCmdBuffer = ComputeContext.GetCommandBufferManager()->GetActiveCmdBuffer();
	if (bComputeToGfx)
	{
		DoBarriers(ComputeQueueIndex, GfxQueueIndex, ComputeCmdBuffer, GfxCmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	}
	else
	{
		DoBarriers(GfxQueueIndex, ComputeQueueIndex, GfxCmdBuffer, ComputeCmdBuffer, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	}
}


void FVulkanCommandListContext::RHIBeginRenderPass(const FRHIRenderPassInfo& InInfo, const TCHAR* InName)
{
	FVulkanCmdBuffer* CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
	if (TransitionAndLayoutManager.CurrentRenderPass)
	{
		checkf(!TransitionAndLayoutManager.bInsideRealRenderPass, TEXT("Didn't call RHIEndRenderPass()!"));
		TransitionAndLayoutManager.EndEmulatedRenderPass(CmdBuffer);
	}

	TransitionAndLayoutManager.bInsideRealRenderPass = false;

	if (GVulkanSubmitAfterEveryEndRenderPass)
	{
		CommandBufferManager->SubmitActiveCmdBuffer();
		CommandBufferManager->PrepareForNewActiveCommandBuffer();
		CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
	}
	else if (SafePointSubmit())
	{
		CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
	}

	RenderPassInfo = InInfo;
	RHIPushEvent(InName ? InName : TEXT("<unnamed RenderPass>"), FColor::Green);
	if (InInfo.bOcclusionQueries)
	{
		BeginOcclusionQueryBatch(CmdBuffer, InInfo.NumOcclusionQueries);
	}

	FVulkanRenderTargetLayout RTLayout(*Device, InInfo);
	check(RTLayout.GetExtent2D().width != 0 && RTLayout.GetExtent2D().height != 0);
	FVulkanRenderPass* RenderPass = TransitionAndLayoutManager.GetOrCreateRenderPass(*Device, RTLayout);
	FRHISetRenderTargetsInfo RTInfo;
	InInfo.ConvertToRenderTargetsInfo(RTInfo);

	FVulkanFramebuffer* Framebuffer = TransitionAndLayoutManager.GetOrCreateFramebuffer(*Device, RTInfo, RTLayout, RenderPass);
	checkf(RenderPass != nullptr && Framebuffer != nullptr, TEXT("RenderPass not started! Bad combination of values? Depth %p #Color %d Color0 %p"), (void*)InInfo.DepthStencilRenderTarget.DepthStencilTarget, InInfo.GetNumColorRenderTargets(), (void*)InInfo.ColorRenderTargets[0].RenderTarget);
	TransitionAndLayoutManager.BeginRealRenderPass(*this, *Device, CmdBuffer, InInfo, RTLayout, RenderPass, Framebuffer);
}


void FVulkanCommandListContext::RHIEndRenderPass()
{
	FVulkanCmdBuffer* CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
	if (RenderPassInfo.bOcclusionQueries)
	{
		EndOcclusionQueryBatch(CmdBuffer);
	}
	else
	{
		TransitionAndLayoutManager.EndRealRenderPass(CmdBuffer);
	}
	if(!RenderPassInfo.bIsMSAA)
	{
		for (int32 Index = 0; Index < MaxSimultaneousRenderTargets; ++Index)
		{
			if (!RenderPassInfo.ColorRenderTargets[Index].RenderTarget)
			{
				break;
			}
			if (RenderPassInfo.ColorRenderTargets[Index].ResolveTarget)
			{
				RHICopyToResolveTarget(RenderPassInfo.ColorRenderTargets[Index].RenderTarget, RenderPassInfo.ColorRenderTargets[Index].ResolveTarget, RenderPassInfo.ResolveParameters);
			}
		}
		if (RenderPassInfo.DepthStencilRenderTarget.DepthStencilTarget && RenderPassInfo.DepthStencilRenderTarget.ResolveTarget)
		{
			RHICopyToResolveTarget(RenderPassInfo.DepthStencilRenderTarget.DepthStencilTarget, RenderPassInfo.DepthStencilRenderTarget.ResolveTarget, RenderPassInfo.ResolveParameters);
		}
	}
	RHIPopEvent();
}

void FVulkanCommandListContext::RHINextSubpass()
{
	check(TransitionAndLayoutManager.CurrentRenderPass);
	FVulkanCmdBuffer* CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
	VkCommandBuffer Cmd = CmdBuffer->GetHandle();
	VulkanRHI::vkCmdNextSubpass(Cmd, VK_SUBPASS_CONTENTS_INLINE);
}

// Need a separate struct so we can memzero/remove dependencies on reference counts
struct FRenderPassCompatibleHashableStruct
{
	FRenderPassCompatibleHashableStruct()
	{
		FMemory::Memzero(*this);
	}

	uint8						NumAttachments;
	uint8						bIsMultiview;
	uint8						NumSamples;
	uint8						SubpassHint;
	// +1 for DepthStencil, +1 for Fragment Density
	VkFormat					Formats[MaxSimultaneousRenderTargets + 2];
};

// Need a separate struct so we can memzero/remove dependencies on reference counts
struct FRenderPassFullHashableStruct
{
	FRenderPassFullHashableStruct()
	{
		FMemory::Memzero(*this);
	}

	// +1 for Depth, +1 for Stencil, +1 for Fragment Density
	TEnumAsByte<VkAttachmentLoadOp>		LoadOps[MaxSimultaneousRenderTargets + 3];
	TEnumAsByte<VkAttachmentStoreOp>	StoreOps[MaxSimultaneousRenderTargets + 3];
#if VULKAN_USE_REAL_RENDERPASS_COMPATIBILITY
	// If the initial != final we need to add FinalLayout and potentially RefLayout
	VkImageLayout						InitialLayout[MaxSimultaneousRenderTargets + 2];
	//VkImageLayout						FinalLayout[MaxSimultaneousRenderTargets + 2];
	//VkImageLayout						RefLayout[MaxSimultaneousRenderTargets + 2];
#endif
};


FVulkanRenderTargetLayout::FVulkanRenderTargetLayout(FVulkanDevice& InDevice, const FRHISetRenderTargetsInfo& RTInfo)
	: NumAttachmentDescriptions(0)
	, NumColorAttachments(0)
	, bHasDepthStencil(false)
	, bHasResolveAttachments(false)
	, bHasFragmentDensityAttachment(false)
	, NumSamples(0)
	, NumUsedClearValues(0)
	, bIsMultiView(0)
{
	FMemory::Memzero(ColorReferences);
	FMemory::Memzero(DepthStencilReference);
	FMemory::Memzero(FragmentDensityReference);
	FMemory::Memzero(ResolveReferences);
	FMemory::Memzero(InputAttachments);
	FMemory::Memzero(Desc);
	FMemory::Memzero(Extent);

	FRenderPassCompatibleHashableStruct CompatibleHashInfo;
	FRenderPassFullHashableStruct FullHashInfo;

	bool bSetExtent = false;
	bool bFoundClearOp = false;
	for (int32 Index = 0; Index < RTInfo.NumColorRenderTargets; ++Index)
	{
		const FRHIRenderTargetView& RTView = RTInfo.ColorRenderTarget[Index];
		if (RTView.Texture)
		{
			FVulkanTextureBase* Texture = FVulkanTextureBase::Cast(RTView.Texture);
			check(Texture);
	
			if (bSetExtent)
			{
				ensure(Extent.Extent3D.width == FMath::Max(1u, Texture->Surface.Width >> RTView.MipIndex));
				ensure(Extent.Extent3D.height == FMath::Max(1u, Texture->Surface.Height >> RTView.MipIndex));
				ensure(Extent.Extent3D.depth == Texture->Surface.Depth);
			}
			else
			{
				bSetExtent = true;
				Extent.Extent3D.width = FMath::Max(1u, Texture->Surface.Width >> RTView.MipIndex);
				Extent.Extent3D.height = FMath::Max(1u, Texture->Surface.Height >> RTView.MipIndex);
				Extent.Extent3D.depth = Texture->Surface.Depth;
			}

			FVulkanSurface* Surface = &Texture->Surface;

			ensure(!NumSamples || NumSamples == Surface->GetNumSamples());
			NumSamples = Surface->GetNumSamples();
		
			VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
			CurrDesc.samples = static_cast<VkSampleCountFlagBits>(NumSamples);
			CurrDesc.format = UEToVkTextureFormat(RTView.Texture->GetFormat(), (Texture->Surface.UEFlags & TexCreate_SRGB) == TexCreate_SRGB);
			CurrDesc.loadOp = RenderTargetLoadActionToVulkan(RTView.LoadAction);
			bFoundClearOp = bFoundClearOp || (CurrDesc.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
			CurrDesc.storeOp = RenderTargetStoreActionToVulkan(RTView.StoreAction);
			CurrDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			CurrDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

			if (Texture->Surface.UEFlags & TexCreate_Memoryless)
			{
				ensure(CurrDesc.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
			}

			// If the initial != final we need to change the FullHashInfo and use FinalLayout
			CurrDesc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			CurrDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			ColorReferences[NumColorAttachments].attachment = NumAttachmentDescriptions;
			ColorReferences[NumColorAttachments].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			const bool bHasValidResolveAttachment = RTInfo.bHasResolveAttachments && RTInfo.ColorResolveRenderTarget[Index].Texture;
			ensure(!(CurrDesc.samples > VK_SAMPLE_COUNT_1_BIT && FVulkanPlatform::RequiresRenderPassResolveAttachments()) || bHasValidResolveAttachment);
			if (CurrDesc.samples > VK_SAMPLE_COUNT_1_BIT && bHasValidResolveAttachment && FVulkanPlatform::RequiresRenderPassResolveAttachments())
			{
				Desc[NumAttachmentDescriptions + 1] = Desc[NumAttachmentDescriptions];
				Desc[NumAttachmentDescriptions + 1].samples = VK_SAMPLE_COUNT_1_BIT;
				Desc[NumAttachmentDescriptions + 1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				ResolveReferences[NumColorAttachments].attachment = NumAttachmentDescriptions + 1;
				ResolveReferences[NumColorAttachments].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				++NumAttachmentDescriptions;
				bHasResolveAttachments = true;
			}

			CompatibleHashInfo.Formats[NumColorAttachments] = CurrDesc.format;
			FullHashInfo.LoadOps[NumColorAttachments] = CurrDesc.loadOp;
			FullHashInfo.StoreOps[NumColorAttachments] = CurrDesc.storeOp;
#if VULKAN_USE_REAL_RENDERPASS_COMPATIBILITY
			FullHashInfo.InitialLayout[NumColorAttachments] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
#endif
			++CompatibleHashInfo.NumAttachments;

			++NumAttachmentDescriptions;
			++NumColorAttachments;
		}
	}

	VkImageLayout DepthStencilLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (RTInfo.DepthStencilRenderTarget.Texture)
	{
		VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
		FMemory::Memzero(CurrDesc);
		FVulkanTextureBase* Texture = FVulkanTextureBase::Cast(RTInfo.DepthStencilRenderTarget.Texture);
		check(Texture);

		FVulkanSurface* Surface = &Texture->Surface;
		ensure(!NumSamples || NumSamples == Surface->GetNumSamples());
		NumSamples = Surface->GetNumSamples();

		CurrDesc.samples = static_cast<VkSampleCountFlagBits>(NumSamples);
		CurrDesc.format = UEToVkTextureFormat(RTInfo.DepthStencilRenderTarget.Texture->GetFormat(), false);
		CurrDesc.loadOp = RenderTargetLoadActionToVulkan(RTInfo.DepthStencilRenderTarget.DepthLoadAction);
		CurrDesc.stencilLoadOp = RenderTargetLoadActionToVulkan(RTInfo.DepthStencilRenderTarget.StencilLoadAction);
		bFoundClearOp = bFoundClearOp || (CurrDesc.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR || CurrDesc.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
		if (CurrDesc.samples == VK_SAMPLE_COUNT_1_BIT)
		{
			CurrDesc.storeOp = RenderTargetStoreActionToVulkan(RTInfo.DepthStencilRenderTarget.DepthStoreAction);
			CurrDesc.stencilStoreOp = RenderTargetStoreActionToVulkan(RTInfo.DepthStencilRenderTarget.GetStencilStoreAction());

			if (Texture->Surface.UEFlags & TexCreate_Memoryless)
			{
				ensure(CurrDesc.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
				ensure(CurrDesc.stencilStoreOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
			}
		}
		else
		{
			// Never want to store MSAA depth/stencil
			CurrDesc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			CurrDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		}

		DepthStencilLayout = VulkanRHI::GetDepthStencilLayout(RTInfo.DepthStencilRenderTarget.GetDepthStencilAccess(), InDevice);

		// If the initial != final we need to change the FullHashInfo and use FinalLayout
		CurrDesc.initialLayout = DepthStencilLayout;
		CurrDesc.finalLayout = DepthStencilLayout;

		DepthStencilReference.attachment = NumAttachmentDescriptions;
		DepthStencilReference.layout = DepthStencilLayout;

		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets] = CurrDesc.loadOp;
		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets + 1] = CurrDesc.stencilLoadOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets] = CurrDesc.storeOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets + 1] = CurrDesc.stencilStoreOp;
#if VULKAN_USE_REAL_RENDERPASS_COMPATIBILITY
		FullHashInfo.InitialLayout[MaxSimultaneousRenderTargets] = DepthStencilLayout;
#endif
		CompatibleHashInfo.Formats[MaxSimultaneousRenderTargets] = CurrDesc.format;

		++NumAttachmentDescriptions;
/*
		if (CurrDesc.samples > VK_SAMPLE_COUNT_1_BIT)
		{
			Desc[NumAttachments + 1] = Desc[NumAttachments];
			Desc[NumAttachments + 1].samples = VK_SAMPLE_COUNT_1_BIT;
			ResolveReferences[NumColorAttachments].attachment = NumAttachments + 1;
			ResolveReferences[NumColorAttachments].layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			++NumAttachments;
			bHasResolveAttachments = true;
		}*/

		bHasDepthStencil = true;

		if (bSetExtent)
		{
			// Depth can be greater or equal to color
			ensure(Texture->Surface.Width >= Extent.Extent3D.width);
			ensure(Texture->Surface.Height >= Extent.Extent3D.height);
		}
		else
		{
			bSetExtent = true;
			Extent.Extent3D.width = Texture->Surface.Width;
			Extent.Extent3D.height = Texture->Surface.Height;
			Extent.Extent3D.depth = Texture->Surface.NumArrayLevels;
		}
	}

	if (InDevice.GetOptionalExtensions().HasEXTFragmentDensityMap && RTInfo.FoveationTexture)
	{
		FVulkanTextureBase* Texture = FVulkanTextureBase::Cast(RTInfo.FoveationTexture);
		check(Texture);

		VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
		FMemory::Memzero(CurrDesc);

		const VkImageLayout FragmentDensityLayout = VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT;

		CurrDesc.flags = 0;
		CurrDesc.format = UEToVkTextureFormat(RTInfo.FoveationTexture->GetFormat(), false);
		CurrDesc.samples = static_cast<VkSampleCountFlagBits>(RTInfo.FoveationTexture->GetNumSamples());
		CurrDesc.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		CurrDesc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		CurrDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		CurrDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		CurrDesc.initialLayout = FragmentDensityLayout;
		CurrDesc.finalLayout = FragmentDensityLayout;

		FragmentDensityReference.attachment = NumAttachmentDescriptions;
		FragmentDensityReference.layout = FragmentDensityLayout;

		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets + 2] = CurrDesc.stencilLoadOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets + 2] = CurrDesc.stencilStoreOp;
#if VULKAN_USE_REAL_RENDERPASS_COMPATIBILITY
		FullHashInfo.InitialLayout[MaxSimultaneousRenderTargets + 1] = FragmentDensityLayout;
#endif
		CompatibleHashInfo.Formats[MaxSimultaneousRenderTargets + 1] = CurrDesc.format;

		++NumAttachmentDescriptions;
		bHasFragmentDensityAttachment = true;
	}

	SubpassHint = ESubpassHint::None;
	CompatibleHashInfo.SubpassHint = 0;

	CompatibleHashInfo.NumSamples = NumSamples;
	CompatibleHashInfo.bIsMultiview = bIsMultiView;

	RenderPassCompatibleHash = FCrc::MemCrc32(&CompatibleHashInfo, sizeof(CompatibleHashInfo));
	RenderPassFullHash = FCrc::MemCrc32(&FullHashInfo, sizeof(FullHashInfo), RenderPassCompatibleHash);
	NumUsedClearValues = bFoundClearOp ? NumAttachmentDescriptions : 0;
	bCalculatedHash = true;
}


FVulkanRenderTargetLayout::FVulkanRenderTargetLayout(FVulkanDevice& InDevice, const FRHIRenderPassInfo& RPInfo)
	: NumAttachmentDescriptions(0)
	, NumColorAttachments(0)
	, bHasDepthStencil(false)
	, bHasResolveAttachments(false)
	, bHasFragmentDensityAttachment(false)
	, NumSamples(0)
	, NumUsedClearValues(0)
	, bIsMultiView(RPInfo.bMultiviewPass)
{
	FMemory::Memzero(ColorReferences);
	FMemory::Memzero(DepthStencilReference);
	FMemory::Memzero(FragmentDensityReference);
	FMemory::Memzero(ResolveReferences);
	FMemory::Memzero(InputAttachments);
	FMemory::Memzero(Desc);
	FMemory::Memzero(Extent);

	FRenderPassCompatibleHashableStruct CompatibleHashInfo;
	FRenderPassFullHashableStruct FullHashInfo;

	bool bSetExtent = false;
	bool bFoundClearOp = false;
	bool bMultiviewRenderTargets = false;

	int32 NumColorRenderTargets = RPInfo.GetNumColorRenderTargets();
	for (int32 Index = 0; Index < NumColorRenderTargets; ++Index)
	{
		const FRHIRenderPassInfo::FColorEntry& ColorEntry = RPInfo.ColorRenderTargets[Index];
		FVulkanTextureBase* Texture = FVulkanTextureBase::Cast(ColorEntry.RenderTarget);
		check(Texture);

		if (bSetExtent)
		{
			ensure(Extent.Extent3D.width == FMath::Max(1u, Texture->Surface.Width >> ColorEntry.MipIndex));
			ensure(Extent.Extent3D.height == FMath::Max(1u, Texture->Surface.Height >> ColorEntry.MipIndex));
			ensure(Extent.Extent3D.depth == Texture->Surface.Depth);
		}
		else
		{
			bSetExtent = true;
			Extent.Extent3D.width = FMath::Max(1u, Texture->Surface.Width >> ColorEntry.MipIndex);
			Extent.Extent3D.height = FMath::Max(1u, Texture->Surface.Height >> ColorEntry.MipIndex);
			Extent.Extent3D.depth = Texture->Surface.Depth;
		}

		ensure(!NumSamples || NumSamples == ColorEntry.RenderTarget->GetNumSamples());
		NumSamples = ColorEntry.RenderTarget->GetNumSamples();

		ensure( !bMultiviewRenderTargets || Texture->Surface.NumArrayLevels > 1 );
		bMultiviewRenderTargets = Texture->Surface.NumArrayLevels > 1;

		VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
		CurrDesc.samples = static_cast<VkSampleCountFlagBits>(NumSamples);
		CurrDesc.format = UEToVkTextureFormat(ColorEntry.RenderTarget->GetFormat(), (Texture->Surface.UEFlags & TexCreate_SRGB) == TexCreate_SRGB);
		CurrDesc.loadOp = RenderTargetLoadActionToVulkan(GetLoadAction(ColorEntry.Action));
		bFoundClearOp = bFoundClearOp || (CurrDesc.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
		CurrDesc.storeOp = RenderTargetStoreActionToVulkan(GetStoreAction(ColorEntry.Action));
		CurrDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		CurrDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

		if (Texture->Surface.UEFlags & TexCreate_Memoryless)
		{
			ensure(CurrDesc.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
		}

		// If the initial != final we need to change the FullHashInfo and use FinalLayout
		CurrDesc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		CurrDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		ColorReferences[NumColorAttachments].attachment = NumAttachmentDescriptions;
		ColorReferences[NumColorAttachments].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		ensure(!(CurrDesc.samples > VK_SAMPLE_COUNT_1_BIT && FVulkanPlatform::RequiresRenderPassResolveAttachments()) || ColorEntry.ResolveTarget);
		if (CurrDesc.samples > VK_SAMPLE_COUNT_1_BIT && ColorEntry.ResolveTarget && FVulkanPlatform::RequiresRenderPassResolveAttachments())
		{
			Desc[NumAttachmentDescriptions + 1] = Desc[NumAttachmentDescriptions];
			Desc[NumAttachmentDescriptions + 1].samples = VK_SAMPLE_COUNT_1_BIT;
			Desc[NumAttachmentDescriptions + 1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			ResolveReferences[NumColorAttachments].attachment = NumAttachmentDescriptions + 1;
			ResolveReferences[NumColorAttachments].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			++NumAttachmentDescriptions;
			bHasResolveAttachments = true;
		}

		CompatibleHashInfo.Formats[NumColorAttachments] = CurrDesc.format;
		FullHashInfo.LoadOps[NumColorAttachments] = CurrDesc.loadOp;
#if VULKAN_USE_REAL_RENDERPASS_COMPATIBILITY
		FullHashInfo.InitialLayout[NumColorAttachments] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
#endif
		FullHashInfo.StoreOps[NumColorAttachments] = CurrDesc.storeOp;
		++CompatibleHashInfo.NumAttachments;

		++NumAttachmentDescriptions;
		++NumColorAttachments;
	}

	VkImageLayout DepthStencilLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (RPInfo.DepthStencilRenderTarget.DepthStencilTarget)
	{
		VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
		FMemory::Memzero(CurrDesc);
		FVulkanTextureBase* Texture = FVulkanTextureBase::Cast(RPInfo.DepthStencilRenderTarget.DepthStencilTarget);
		check(Texture);

		CurrDesc.samples = static_cast<VkSampleCountFlagBits>(RPInfo.DepthStencilRenderTarget.DepthStencilTarget->GetNumSamples());
		ensure(!NumSamples || CurrDesc.samples == NumSamples);
		NumSamples = CurrDesc.samples;
		CurrDesc.format = UEToVkTextureFormat(RPInfo.DepthStencilRenderTarget.DepthStencilTarget->GetFormat(), false);
		CurrDesc.loadOp = RenderTargetLoadActionToVulkan(GetLoadAction(GetDepthActions(RPInfo.DepthStencilRenderTarget.Action)));
		CurrDesc.stencilLoadOp = RenderTargetLoadActionToVulkan(GetLoadAction(GetStencilActions(RPInfo.DepthStencilRenderTarget.Action)));
		bFoundClearOp = bFoundClearOp || (CurrDesc.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR || CurrDesc.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
		if (CurrDesc.samples != VK_SAMPLE_COUNT_1_BIT)
		{
			// Can't resolve MSAA depth/stencil
			ensure(GetStoreAction(GetDepthActions(RPInfo.DepthStencilRenderTarget.Action)) != ERenderTargetStoreAction::EMultisampleResolve);
			ensure(GetStoreAction(GetStencilActions(RPInfo.DepthStencilRenderTarget.Action)) != ERenderTargetStoreAction::EMultisampleResolve);
		}

		CurrDesc.storeOp = RenderTargetStoreActionToVulkan(GetStoreAction(GetDepthActions(RPInfo.DepthStencilRenderTarget.Action)));
		CurrDesc.stencilStoreOp = RenderTargetStoreActionToVulkan(GetStoreAction(GetStencilActions(RPInfo.DepthStencilRenderTarget.Action)));

		if (Texture->Surface.UEFlags & TexCreate_Memoryless)
		{
			ensure(CurrDesc.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
			ensure(CurrDesc.stencilStoreOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
		}

		FExclusiveDepthStencil ExclusiveDepthStencil = RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil;
		if (FVulkanPlatform::RequiresDepthWriteOnStencilClear() &&
			RPInfo.DepthStencilRenderTarget.Action == EDepthStencilTargetActions::LoadDepthClearStencil_StoreDepthStencil)
		{
			ExclusiveDepthStencil = FExclusiveDepthStencil::DepthWrite_StencilWrite;
		}

		DepthStencilLayout = VulkanRHI::GetDepthStencilLayout(ExclusiveDepthStencil, InDevice);
		// If the initial != final we need to change the FullHashInfo and use FinalLayout
		CurrDesc.initialLayout = DepthStencilLayout;
		CurrDesc.finalLayout = DepthStencilLayout;
		DepthStencilReference.attachment = NumAttachmentDescriptions;
		DepthStencilReference.layout = DepthStencilLayout;

		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets] = CurrDesc.loadOp;
		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets + 1] = CurrDesc.stencilLoadOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets] = CurrDesc.storeOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets + 1] = CurrDesc.stencilStoreOp;
#if VULKAN_USE_REAL_RENDERPASS_COMPATIBILITY
		FullHashInfo.InitialLayout[MaxSimultaneousRenderTargets] = DepthStencilLayout;
#endif
		CompatibleHashInfo.Formats[MaxSimultaneousRenderTargets] = CurrDesc.format;

		++NumAttachmentDescriptions;
		/*
		if (CurrDesc.samples > VK_SAMPLE_COUNT_1_BIT)
		{
		Desc[NumAttachments + 1] = Desc[NumAttachments];
		Desc[NumAttachments + 1].samples = VK_SAMPLE_COUNT_1_BIT;
		ResolveReferences[NumColorAttachments].attachment = NumAttachments + 1;
		ResolveReferences[NumColorAttachments].layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		++NumAttachments;
		bHasResolveAttachments = true;
		}*/

		bHasDepthStencil = true;

		ensure(!bMultiviewRenderTargets || Texture->Surface.NumArrayLevels > 1);
		bMultiviewRenderTargets = Texture->Surface.NumArrayLevels > 1;

		if (bSetExtent)
		{
			// Depth can be greater or equal to color
			ensure(Texture->Surface.Width >= Extent.Extent3D.width);
			ensure(Texture->Surface.Height >= Extent.Extent3D.height);
		}
		else
		{
			bSetExtent = true;
			Extent.Extent3D.width = Texture->Surface.Width;
			Extent.Extent3D.height = Texture->Surface.Height;
			Extent.Extent3D.depth = Texture->Surface.Depth;
		}
	}

	if (InDevice.GetOptionalExtensions().HasEXTFragmentDensityMap && RPInfo.FoveationTexture)
	{
		FVulkanTextureBase* Texture = FVulkanTextureBase::Cast(RPInfo.FoveationTexture);
		check(Texture);

		VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
		FMemory::Memzero(CurrDesc);

		const VkImageLayout FragmentDensityLayout = VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT;

		CurrDesc.flags = 0;
		CurrDesc.format = UEToVkTextureFormat(RPInfo.FoveationTexture->GetFormat(), false);
		CurrDesc.samples = static_cast<VkSampleCountFlagBits>(RPInfo.FoveationTexture->GetNumSamples());
		CurrDesc.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		CurrDesc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		CurrDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		CurrDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		CurrDesc.initialLayout = FragmentDensityLayout;
		CurrDesc.finalLayout = FragmentDensityLayout;

		FragmentDensityReference.attachment = NumAttachmentDescriptions;
		FragmentDensityReference.layout = FragmentDensityLayout;

		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets + 2] = CurrDesc.stencilLoadOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets + 2] = CurrDesc.stencilStoreOp;
#if VULKAN_USE_REAL_RENDERPASS_COMPATIBILITY
		FullHashInfo.InitialLayout[MaxSimultaneousRenderTargets + 1] = FragmentDensityLayout;
#endif
		CompatibleHashInfo.Formats[MaxSimultaneousRenderTargets + 1] = CurrDesc.format;

		++NumAttachmentDescriptions;
		bHasFragmentDensityAttachment = true;
	}

	SubpassHint = RPInfo.SubpassHint;
	CompatibleHashInfo.SubpassHint = (uint8)RPInfo.SubpassHint;

	CompatibleHashInfo.NumSamples = NumSamples;
	CompatibleHashInfo.bIsMultiview = bIsMultiView;

	if (bIsMultiView && !bMultiviewRenderTargets)
	{
		UE_LOG(LogVulkan, Error, TEXT("Non multiview textures on a multiview layout!"));
	}

	RenderPassCompatibleHash = FCrc::MemCrc32(&CompatibleHashInfo, sizeof(CompatibleHashInfo));
	RenderPassFullHash = FCrc::MemCrc32(&FullHashInfo, sizeof(FullHashInfo), RenderPassCompatibleHash);
	NumUsedClearValues = bFoundClearOp ? NumAttachmentDescriptions : 0;
	bCalculatedHash = true;
}

FVulkanRenderTargetLayout::FVulkanRenderTargetLayout(const FGraphicsPipelineStateInitializer& Initializer)
	: NumAttachmentDescriptions(0)
	, NumColorAttachments(0)
	, bHasDepthStencil(false)
	, bHasResolveAttachments(false)
	, bHasFragmentDensityAttachment(false)
	, NumSamples(0)
	, NumUsedClearValues(0)
	, bIsMultiView(0)
{
	FMemory::Memzero(ColorReferences);
	FMemory::Memzero(DepthStencilReference);
	FMemory::Memzero(FragmentDensityReference);
	FMemory::Memzero(ResolveReferences);
	FMemory::Memzero(InputAttachments);
	FMemory::Memzero(Desc);
	FMemory::Memzero(Extent);

	FRenderPassCompatibleHashableStruct CompatibleHashInfo;
	FRenderPassFullHashableStruct FullHashInfo;

	bool bSetExtent = false;
	bool bFoundClearOp = false;
	bIsMultiView = Initializer.bMultiView;
	NumSamples = Initializer.NumSamples;
	for (uint32 Index = 0; Index < Initializer.RenderTargetsEnabled; ++Index)
	{
		EPixelFormat UEFormat = (EPixelFormat)Initializer.RenderTargetFormats[Index];
		if (UEFormat != PF_Unknown)
		{
			VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
			CurrDesc.samples = static_cast<VkSampleCountFlagBits>(NumSamples);
			CurrDesc.format = UEToVkTextureFormat(UEFormat, (Initializer.RenderTargetFlags[Index] & TexCreate_SRGB) == TexCreate_SRGB);
			CurrDesc.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			CurrDesc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			CurrDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			CurrDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

			// If the initial != final we need to change the FullHashInfo and use FinalLayout
			CurrDesc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			CurrDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			ColorReferences[NumColorAttachments].attachment = NumAttachmentDescriptions;
			ColorReferences[NumColorAttachments].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			if (CurrDesc.samples > VK_SAMPLE_COUNT_1_BIT && FVulkanPlatform::RequiresRenderPassResolveAttachments())
			{
				Desc[NumAttachmentDescriptions + 1] = Desc[NumAttachmentDescriptions];
				Desc[NumAttachmentDescriptions + 1].samples = VK_SAMPLE_COUNT_1_BIT;
				Desc[NumAttachmentDescriptions + 1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				ResolveReferences[NumColorAttachments].attachment = NumAttachmentDescriptions + 1;
				ResolveReferences[NumColorAttachments].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				++NumAttachmentDescriptions;
				bHasResolveAttachments = true;
			}

			CompatibleHashInfo.Formats[NumColorAttachments] = CurrDesc.format;
			FullHashInfo.LoadOps[NumColorAttachments] = CurrDesc.loadOp;
			FullHashInfo.StoreOps[NumColorAttachments] = CurrDesc.storeOp;
#if VULKAN_USE_REAL_RENDERPASS_COMPATIBILITY
			FullHashInfo.InitialLayout[NumColorAttachments] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
#endif
			++CompatibleHashInfo.NumAttachments;

			++NumAttachmentDescriptions;
			++NumColorAttachments;
		}
	}

	if (Initializer.DepthStencilTargetFormat != PF_Unknown)
	{
		VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
		FMemory::Memzero(CurrDesc);

		CurrDesc.samples = static_cast<VkSampleCountFlagBits>(NumSamples);
		CurrDesc.format = UEToVkTextureFormat(Initializer.DepthStencilTargetFormat, false);
		CurrDesc.loadOp = RenderTargetLoadActionToVulkan(Initializer.DepthTargetLoadAction);
		CurrDesc.stencilLoadOp = RenderTargetLoadActionToVulkan(Initializer.StencilTargetLoadAction);
		if (CurrDesc.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR || CurrDesc.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
		{
			bFoundClearOp = true;
		}
		if (CurrDesc.samples == VK_SAMPLE_COUNT_1_BIT)
		{
			CurrDesc.storeOp = RenderTargetStoreActionToVulkan(Initializer.StencilTargetStoreAction);
			CurrDesc.stencilStoreOp = RenderTargetStoreActionToVulkan(Initializer.StencilTargetStoreAction);
		}
		else
		{
			// Never want to store MSAA depth/stencil
			CurrDesc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			CurrDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		}

		// If the initial != final we need to change the FullHashInfo and use FinalLayout
		CurrDesc.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		CurrDesc.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		DepthStencilReference.attachment = NumAttachmentDescriptions;
		DepthStencilReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		/*
		if (CurrDesc.samples > VK_SAMPLE_COUNT_1_BIT)
		{
		Desc[NumAttachments + 1] = Desc[NumAttachments];
		Desc[NumAttachments + 1].samples = VK_SAMPLE_COUNT_1_BIT;
		ResolveReferences[NumColorAttachments].attachment = NumAttachments + 1;
		ResolveReferences[NumColorAttachments].layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		++NumAttachments;
		bHasResolveAttachments = true;
		}*/

		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets] = CurrDesc.loadOp;
		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets + 1] = CurrDesc.stencilLoadOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets] = CurrDesc.storeOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets + 1] = CurrDesc.stencilStoreOp;
#if VULKAN_USE_REAL_RENDERPASS_COMPATIBILITY
		FullHashInfo.InitialLayout[MaxSimultaneousRenderTargets] = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
#endif
		CompatibleHashInfo.Formats[MaxSimultaneousRenderTargets] = CurrDesc.format;

		++NumAttachmentDescriptions;
		bHasDepthStencil = true;
	}

	if (Initializer.bHasFragmentDensityAttachment)
	{
		VkAttachmentDescription& CurrDesc = Desc[NumAttachmentDescriptions];
		FMemory::Memzero(CurrDesc);

		const VkImageLayout FragmentDensityLayout = VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT;

		CurrDesc.flags = 0;
		CurrDesc.format = VK_FORMAT_R8G8_UNORM;
		CurrDesc.samples = VK_SAMPLE_COUNT_1_BIT;
		CurrDesc.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		CurrDesc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		CurrDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		CurrDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		CurrDesc.initialLayout = FragmentDensityLayout;
		CurrDesc.finalLayout = FragmentDensityLayout;

		FragmentDensityReference.attachment = NumAttachmentDescriptions;
		FragmentDensityReference.layout = FragmentDensityLayout;

		FullHashInfo.LoadOps[MaxSimultaneousRenderTargets + 2] = CurrDesc.stencilLoadOp;
		FullHashInfo.StoreOps[MaxSimultaneousRenderTargets + 2] = CurrDesc.stencilStoreOp;
#if VULKAN_USE_REAL_RENDERPASS_COMPATIBILITY
		FullHashInfo.InitialLayout[MaxSimultaneousRenderTargets + 1] = FragmentDensityLayout;
#endif
		CompatibleHashInfo.Formats[MaxSimultaneousRenderTargets + 1] = CurrDesc.format;

		++NumAttachmentDescriptions;
		bHasFragmentDensityAttachment = true;
	}

	SubpassHint = Initializer.SubpassHint;
	CompatibleHashInfo.SubpassHint = (uint8)Initializer.SubpassHint;

	CompatibleHashInfo.NumSamples = NumSamples;
	CompatibleHashInfo.bIsMultiview = bIsMultiView;

	RenderPassCompatibleHash = FCrc::MemCrc32(&CompatibleHashInfo, sizeof(CompatibleHashInfo));
	RenderPassFullHash = FCrc::MemCrc32(&FullHashInfo, sizeof(FullHashInfo), RenderPassCompatibleHash);
	NumUsedClearValues = bFoundClearOp ? NumAttachmentDescriptions : 0;
	bCalculatedHash = true;
}
