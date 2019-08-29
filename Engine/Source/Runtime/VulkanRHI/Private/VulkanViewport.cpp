// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanViewport.cpp: Vulkan viewport RHI implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "VulkanSwapChain.h"
#include "VulkanPendingState.h"
#include "VulkanContext.h"

//#todo-Lumin: Until we have LuminEngine.ini
FAutoConsoleVariable GCVarDelayAcquireBackBuffer(
	TEXT("r.Vulkan.DelayAcquireBackBuffer"),
	(PLATFORM_ANDROID && !PLATFORM_LUMIN) ? 0 : 1,
	TEXT("Delay acquiring the back buffer until present"),
	ECVF_ReadOnly
);

struct FRHICommandAcquireBackBuffer final : public FRHICommand<FRHICommandAcquireBackBuffer>
{
	FVulkanViewport* Viewport;
	FVulkanBackBuffer* NewBackBuffer;
	FORCEINLINE_DEBUGGABLE FRHICommandAcquireBackBuffer(FVulkanViewport* InViewport, FVulkanBackBuffer* InNewBackBuffer)
		: Viewport(InViewport)
		, NewBackBuffer(InNewBackBuffer)
	{
	}

	void Execute(FRHICommandListBase& CmdList)
	{
		Viewport->AcquireBackBuffer(CmdList, NewBackBuffer);
	}
};


struct FRHICommandProcessDeferredDeletionQueue final : public FRHICommand<FRHICommandProcessDeferredDeletionQueue>
{
	FVulkanDevice* Device;
	FORCEINLINE_DEBUGGABLE FRHICommandProcessDeferredDeletionQueue(FVulkanDevice* InDevice)
		: Device(InDevice)
	{
	}

	void Execute(FRHICommandListBase& CmdList)
	{
		Device->GetDeferredDeletionQueue().ReleaseResources();
	}
};


FVulkanViewport::FVulkanViewport(FVulkanDynamicRHI* InRHI, FVulkanDevice* InDevice, void* InWindowHandle, uint32 InSizeX, uint32 InSizeY, bool bInIsFullscreen, EPixelFormat InPreferredPixelFormat)
	: VulkanRHI::FDeviceChild(InDevice)
	, RHI(InRHI)
	, SizeX(InSizeX)
	, SizeY(InSizeY)
	, bIsFullscreen(bInIsFullscreen)
	, PixelFormat(InPreferredPixelFormat)
	, AcquiredImageIndex(-1)
	, SwapChain(nullptr)
	, WindowHandle(InWindowHandle)
	, PresentCount(0)
	, AcquiredSemaphore(nullptr)
{
	check(IsInGameThread());
	FMemory::Memzero(BackBufferImages);
	RHI->Viewports.Add(this);

	// Make sure Instance is created
	RHI->InitInstance();

	CreateSwapchain();

	if (FVulkanPlatform::SupportsStandardSwapchain())
	{
		for (int32 Index = 0; Index < NUM_BUFFERS; ++Index)
		{
			RenderingDoneSemaphores[Index] = new VulkanRHI::FSemaphore(*InDevice);
			RenderingDoneSemaphores[Index]->AddRef();
		}
	}
}

FVulkanViewport::~FVulkanViewport()
{
	RenderingBackBuffer = nullptr;
	RHIBackBuffer = nullptr;

	if (FVulkanPlatform::SupportsStandardSwapchain())
	{
		for (int32 Index = 0; Index < NUM_BUFFERS; ++Index)
		{
			RenderingDoneSemaphores[Index]->Release();

			TextureViews[Index].Destroy(*Device);

			Device->NotifyDeletedImage(BackBufferImages[Index]);
		}

		SwapChain->Destroy();
		delete SwapChain;
		SwapChain = nullptr;
	}

	RHI->Viewports.Remove(this);
}

int32 FVulkanViewport::DoAcquireImageIndex(FVulkanViewport* Viewport)
{
	return Viewport->AcquiredImageIndex = Viewport->SwapChain->AcquireImageIndex(&Viewport->AcquiredSemaphore);
}

bool FVulkanViewport::DoCheckedSwapChainJob(TFunction<int32(FVulkanViewport*)> SwapChainJob)
{
	int32 AttemptsPending = 4;
	int32 Status = SwapChainJob(this);

	while (Status < 0 && AttemptsPending > 0)
	{
		if (Status == (int32)FVulkanSwapChain::EStatus::OutOfDate)
		{
			UE_LOG(LogVulkanRHI, Verbose, TEXT("Swapchain is out of date! Trying to recreate the swapchain."));
		}
		else if (Status == (int32)FVulkanSwapChain::EStatus::SurfaceLost)
		{
			UE_LOG(LogVulkanRHI, Warning, TEXT("Swapchain surface lost! Trying to recreate the swapchain."));
		}
		else
		{
			check(0);
		}

		RecreateSwapchain(WindowHandle, true);

		// Swapchain creation pushes some commands - flush the command buffers now to begin with a fresh state
		Device->SubmitCommandsAndFlushGPU();
		Device->WaitUntilIdle();

		Status = SwapChainJob(this);

		--AttemptsPending;
	}

	return Status >= 0;
}

void FVulkanViewport::AcquireBackBuffer(FRHICommandListBase& CmdList, FVulkanBackBuffer* NewBackBuffer)
{
	if (FVulkanPlatform::SupportsStandardSwapchain())
	{
		check(NewBackBuffer);

		int32 PrevImageIndex = AcquiredImageIndex;
		if (!DoCheckedSwapChainJob(DoAcquireImageIndex))
		{
			UE_LOG(LogVulkanRHI, Fatal, TEXT("Swapchain acquire image index failed!"));
		}
		check(AcquiredImageIndex != -1);
		RHIBackBuffer = NewBackBuffer;
		RHIBackBuffer->Surface.Image = BackBufferImages[AcquiredImageIndex];
		RHIBackBuffer->DefaultView.View = TextureViews[AcquiredImageIndex].View;
	}
	FVulkanCommandListContext& Context = (FVulkanCommandListContext&)CmdList.GetContext();

	FVulkanCommandBufferManager* CmdBufferManager = Context.GetCommandBufferManager();
	FVulkanCmdBuffer* CmdBuffer = CmdBufferManager->GetActiveCmdBuffer();
	if (CmdBuffer->IsInsideRenderPass())
	{
		// This could happen due to a SetRT(AndClear) call lingering around (so emulated needs to be ended); however REAL render passes should already have been ended!
		FTransitionAndLayoutManager& LayoutMgr = Context.GetTransitionAndLayoutManager();
		checkf(!LayoutMgr.bInsideRealRenderPass, TEXT("Did not end Render Pass!"));
		LayoutMgr.EndEmulatedRenderPass(CmdBuffer);
	}

	if (FVulkanPlatform::SupportsStandardSwapchain())
	{
		VulkanRHI::ImagePipelineBarrier(CmdBuffer->GetHandle(), BackBufferImages[AcquiredImageIndex], EImageLayoutBarrier::Undefined, EImageLayoutBarrier::ColorAttachment, VulkanRHI::SetupImageSubresourceRange());
	}

	// Submit here so we can add a dependency with the acquired semaphore
	CmdBuffer->End();
	if (FVulkanPlatform::SupportsStandardSwapchain())
	{
		CmdBuffer->AddWaitSemaphore(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, AcquiredSemaphore);
	}
	Device->GetGraphicsQueue()->Submit(CmdBuffer);
	CmdBufferManager->PrepareForNewActiveCommandBuffer();
}

FVulkanTexture2D* FVulkanViewport::GetBackBuffer(FRHICommandList& RHICmdList)
{
	check(IsInRenderingThread());

	if (!RenderingBackBuffer && FVulkanPlatform::SupportsStandardSwapchain())
	{
		check(!DelayAcquireBackBuffer());

		RenderingBackBuffer = new FVulkanBackBuffer(*Device, PixelFormat, SizeX, SizeY, VK_NULL_HANDLE, TexCreate_Presentable | TexCreate_RenderTargetable);
		check(RHICmdList.IsImmediate());
#if VULKAN_ENABLE_DRAW_MARKERS
		if (Device->GetDebugMarkerSetObjectName())
		{
			VulkanRHI::SetDebugMarkerName(Device->GetDebugMarkerSetObjectName(), Device->GetInstanceHandle(), RenderingBackBuffer->Surface.Image, "RenderingBackBuffer");
		}
#endif

		if (RHICmdList.Bypass() || !IsRunningRHIInSeparateThread())
		{
			FRHICommandAcquireBackBuffer Cmd(this, RenderingBackBuffer);
			Cmd.Execute(RHICmdList);
		}
		else
		{
			new (RHICmdList.AllocCommand<FRHICommandAcquireBackBuffer>()) FRHICommandAcquireBackBuffer(this, RenderingBackBuffer);
		}
	}

	return RenderingBackBuffer;
}

void FVulkanViewport::AdvanceBackBufferFrame()
{
	check(IsInRenderingThread());

	if (!DelayAcquireBackBuffer())
	{
		RenderingBackBuffer = nullptr;
	}
}

//void FVulkanViewport::WaitForFrameEventCompletion()
//{
//}


FVulkanFramebuffer::FVulkanFramebuffer(FVulkanDevice& Device, const FRHISetRenderTargetsInfo& InRTInfo, const FVulkanRenderTargetLayout& RTLayout, const FVulkanRenderPass& RenderPass)
	: Framebuffer(VK_NULL_HANDLE)
	, RTInfo(InRTInfo)
	, NumColorAttachments(0)
{
	AttachmentViews.Empty(RTLayout.GetNumAttachmentDescriptions());
	uint32 MipIndex = 0;

	const VkExtent3D& RTExtents = RTLayout.GetExtent3D();
	// Adreno does not like zero size RTs
	check(RTExtents.width != 0 && RTExtents.height != 0);
	uint32 NumLayers = RTExtents.depth;

	for (int32 Index = 0; Index < InRTInfo.NumColorRenderTargets; ++Index)
	{
		FRHITexture* RHITexture = InRTInfo.ColorRenderTarget[Index].Texture;
		if (!RHITexture)
		{
			continue;
		}

		FVulkanTextureBase* Texture = FVulkanTextureBase::Cast(RHITexture);
		ColorRenderTargetImages[Index] = Texture->Surface.Image;
		MipIndex = InRTInfo.ColorRenderTarget[Index].MipIndex;

		VkImageView RTView = VK_NULL_HANDLE;
		if (Texture->Surface.GetViewType() == VK_IMAGE_VIEW_TYPE_2D)
		{
			RTView = Texture->CreateRenderTargetView(MipIndex, 1, FMath::Max(0, (int32)InRTInfo.ColorRenderTarget[Index].ArraySliceIndex), 1);
		}
		else if (Texture->Surface.GetViewType() == VK_IMAGE_VIEW_TYPE_CUBE)
		{
			// Cube always renders one face at a time
			INC_DWORD_STAT(STAT_VulkanNumImageViews);
			RTView = FVulkanTextureView::StaticCreate(*Texture->Surface.Device, Texture->Surface.Image, VK_IMAGE_VIEW_TYPE_2D, Texture->Surface.GetFullAspectMask(), Texture->Surface.PixelFormat, Texture->Surface.ViewFormat, MipIndex, 1, InRTInfo.ColorRenderTarget[Index].ArraySliceIndex, 1, true);
		}
		else if (Texture->Surface.GetViewType() == VK_IMAGE_VIEW_TYPE_3D)
		{
			RTView = FVulkanTextureView::StaticCreate(*Texture->Surface.Device, Texture->Surface.Image, VK_IMAGE_VIEW_TYPE_2D_ARRAY, Texture->Surface.GetFullAspectMask(), Texture->Surface.PixelFormat, Texture->Surface.ViewFormat, MipIndex, 1, 0, Texture->Surface.Depth, true);
		}
		else
		{
			ensure(0);
		}

		if (Texture->MSAASurface)
		{
			AttachmentViews.Add(Texture->MSAAView.View);
		}

		AttachmentViews.Add(RTView);
		AttachmentViewsToDelete.Add(RTView);

		++NumColorAttachments;
	}

	if (RTLayout.GetHasDepthStencil())
	{
		FVulkanTextureBase* Texture = FVulkanTextureBase::Cast(InRTInfo.DepthStencilRenderTarget.Texture);
		DepthStencilRenderTargetImage = Texture->Surface.Image;
		bool bHasStencil = (Texture->Surface.PixelFormat == PF_DepthStencil || Texture->Surface.PixelFormat == PF_X24_G8);

		ensure(Texture->Surface.GetViewType() == VK_IMAGE_VIEW_TYPE_2D || Texture->Surface.GetViewType() == VK_IMAGE_VIEW_TYPE_CUBE);
		if (NumColorAttachments == 0 && Texture->Surface.GetViewType() == VK_IMAGE_VIEW_TYPE_CUBE)
		{
			VkImageView RTView = FVulkanTextureView::StaticCreate(*Texture->Surface.Device, Texture->Surface.Image, VK_IMAGE_VIEW_TYPE_2D_ARRAY, Texture->Surface.GetFullAspectMask(), Texture->Surface.PixelFormat, Texture->Surface.ViewFormat, MipIndex, 1, 0, 6, true);
			NumLayers = 6;
			AttachmentViews.Add(RTView);
			AttachmentViewsToDelete.Add(RTView);
		}
		else
		{
			AttachmentViews.Add(Texture->DefaultView.View);
		}
	}

	VkFramebufferCreateInfo CreateInfo;
	ZeroVulkanStruct(CreateInfo, VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
	CreateInfo.renderPass = RenderPass.GetHandle();
	CreateInfo.attachmentCount = AttachmentViews.Num();
	CreateInfo.pAttachments = AttachmentViews.GetData();
	CreateInfo.width  = RTExtents.width;
	CreateInfo.height = RTExtents.height;
	CreateInfo.layers = NumLayers;
	VERIFYVULKANRESULT_EXPANDED(VulkanRHI::vkCreateFramebuffer(Device.GetInstanceHandle(), &CreateInfo, nullptr, &Framebuffer));

	Extents.width = CreateInfo.width;
	Extents.height = CreateInfo.height;

	INC_DWORD_STAT(STAT_VulkanNumFrameBuffers);
}

FVulkanFramebuffer::~FVulkanFramebuffer()
{
	ensure(Framebuffer == VK_NULL_HANDLE);
}

void FVulkanFramebuffer::Destroy(FVulkanDevice& Device)
{
	VulkanRHI::FDeferredDeletionQueue& Queue = Device.GetDeferredDeletionQueue();

	for (int32 Index = 0; Index < AttachmentViewsToDelete.Num(); ++Index)
	{
		DEC_DWORD_STAT(STAT_VulkanNumImageViews);
		Queue.EnqueueResource(VulkanRHI::FDeferredDeletionQueue::EType::ImageView, AttachmentViewsToDelete[Index]);
	}

	Queue.EnqueueResource(VulkanRHI::FDeferredDeletionQueue::EType::Framebuffer, Framebuffer);
	Framebuffer = VK_NULL_HANDLE;

	DEC_DWORD_STAT(STAT_VulkanNumFrameBuffers);
}

bool FVulkanFramebuffer::Matches(const FRHISetRenderTargetsInfo& InRTInfo) const
{
	if (RTInfo.NumColorRenderTargets != InRTInfo.NumColorRenderTargets)
	{
		return false;
	}
	if (RTInfo.bClearColor != InRTInfo.bClearColor)
	{
		return false;
	}
	if (RTInfo.bClearDepth != InRTInfo.bClearDepth)
	{
		return false;
	}
	if (RTInfo.bClearStencil != InRTInfo.bClearStencil)
	{
		return false;
	}

	{
		const FRHIDepthRenderTargetView& A = RTInfo.DepthStencilRenderTarget;
		const FRHIDepthRenderTargetView& B = InRTInfo.DepthStencilRenderTarget;
		if (!(A == B))
		{
			return false;
		}

		if (A.Texture)
		{
			VkImage AImage = DepthStencilRenderTargetImage;
			VkImage BImage = ((FVulkanTextureBase*)B.Texture->GetTextureBaseRHI())->Surface.Image;
			if (AImage != BImage)
			{
				return false;
			}
		}
	}

	// We dont need to compare all render-tagets, since we
	// already have compared the number of render-targets
	for (int32 Index = 0; Index < RTInfo.NumColorRenderTargets; ++Index)
	{
		const FRHIRenderTargetView& A = RTInfo.ColorRenderTarget[Index];
		const FRHIRenderTargetView& B = InRTInfo.ColorRenderTarget[Index];
		if (!(A == B))
		{
			return false;
		}

		if (A.Texture)
		{
			VkImage AImage = ColorRenderTargetImages[Index];
			VkImage BImage = ((FVulkanTextureBase*)B.Texture->GetTextureBaseRHI())->Surface.Image;
			if (AImage != BImage)
			{
				return false;
			}
		}
	}

	return true;
}

// Tear down and recreate swapchain and related resources.
void FVulkanViewport::RecreateSwapchain(void* NewNativeWindow, bool bForce)
{
	if (WindowHandle == NewNativeWindow && !bForce)
	{
		// No action is required if handle has not changed.
		return;
	}

	RenderingBackBuffer = nullptr;
	RHIBackBuffer = nullptr;

	if (FVulkanPlatform::SupportsStandardSwapchain())
	{
		for (int32 Index = 0; Index < NUM_BUFFERS; ++Index)
		{
			TextureViews[Index].Destroy(*Device);
		}

		SwapChain->Destroy();
		delete SwapChain;
		SwapChain = nullptr;
	}

	for (VkImage& BackBufferImage : BackBufferImages)
	{
		BackBufferImage = VK_NULL_HANDLE;
	}

	WindowHandle = NewNativeWindow;
	CreateSwapchain();
}

void FVulkanViewport::Resize(uint32 InSizeX, uint32 InSizeY, bool bInIsFullscreen)
{
	// Submit all command buffers here
	Device->SubmitCommandsAndFlushGPU();

	Device->WaitUntilIdle();

	RenderingBackBuffer = nullptr;
	RHIBackBuffer = nullptr;

	if (FVulkanPlatform::SupportsStandardSwapchain())
	{
		for (VkImage& BackBufferImage : BackBufferImages)
		{
			Device->NotifyDeletedRenderTarget(BackBufferImage);
			BackBufferImage = VK_NULL_HANDLE;
		}

		for (int32 Index = 0; Index < NUM_BUFFERS; ++Index)
		{
			TextureViews[Index].Destroy(*Device);
		}

		Device->GetDeferredDeletionQueue().ReleaseResources(true);

		SwapChain->Destroy();
		delete SwapChain;
		SwapChain = nullptr;

		Device->GetDeferredDeletionQueue().ReleaseResources(true);
	}

	SizeX = InSizeX;
	SizeY = InSizeY;
	bIsFullscreen = bInIsFullscreen;
	CreateSwapchain();
}

void FVulkanViewport::CreateSwapchain()
{
	if (FVulkanPlatform::SupportsStandardSwapchain())
	{
		uint32 DesiredNumBackBuffers = NUM_BUFFERS;

		TArray<VkImage> Images;
		SwapChain = new FVulkanSwapChain(
			RHI->Instance, *Device, WindowHandle,
			PixelFormat, SizeX, SizeY,
			&DesiredNumBackBuffers,
			Images
		);

		check(Images.Num() == NUM_BUFFERS);

		FVulkanCmdBuffer* CmdBuffer = Device->GetImmediateContext().GetCommandBufferManager()->GetUploadCmdBuffer();
		ensure(CmdBuffer->IsOutsideRenderPass());

		for (int32 Index = 0; Index < Images.Num(); ++Index)
		{
			BackBufferImages[Index] = Images[Index];

			FName Name = FName(*FString::Printf(TEXT("BackBuffer%d"), Index));
			//BackBuffers[Index]->SetName(Name);

			TextureViews[Index].Create(*Device, Images[Index], VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, PixelFormat, UEToVkFormat(PixelFormat, false), 0, 1, 0, 1);

			// Clear the swapchain to avoid a validation warning, and transition to ColorAttachment
			{
				VkImageSubresourceRange Range;
				FMemory::Memzero(Range);
				Range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				Range.baseMipLevel = 0;
				Range.levelCount = 1;
				Range.baseArrayLayer = 0;
				Range.layerCount = 1;

				VkClearColorValue Color;
				FMemory::Memzero(Color);
				VulkanRHI::ImagePipelineBarrier(CmdBuffer->GetHandle(), Images[Index], EImageLayoutBarrier::Undefined, EImageLayoutBarrier::TransferDest, Range);
				VulkanRHI::vkCmdClearColorImage(CmdBuffer->GetHandle(), Images[Index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &Color, 1, &Range);
				VulkanRHI::ImagePipelineBarrier(CmdBuffer->GetHandle(), Images[Index], EImageLayoutBarrier::TransferDest, EImageLayoutBarrier::ColorAttachment, Range);
			}
		}

		Device->GetImmediateContext().GetCommandBufferManager()->SubmitUploadCmdBuffer();
	}
	else
	{
		PixelFormat = FVulkanPlatform::GetPixelFormatForNonDefaultSwapchain();
	}

	if (!FVulkanPlatform::SupportsStandardSwapchain() || DelayAcquireBackBuffer())
	{
		RenderingBackBuffer = new FVulkanBackBuffer(*Device, PixelFormat, SizeX, SizeY, TexCreate_RenderTargetable | TexCreate_ShaderResource);
#if VULKAN_ENABLE_DRAW_MARKERS
		if (Device->GetDebugMarkerSetObjectName())
		{
			VulkanRHI::SetDebugMarkerName(Device->GetDebugMarkerSetObjectName(), Device->GetInstanceHandle(), RenderingBackBuffer->Surface.Image, "RenderingBackBuffer");
		}
#endif
	}
}

inline static void CopyImageToBackBuffer(FVulkanCmdBuffer* CmdBuffer, bool bSourceReadOnly, VkImage SrcSurface, VkImage DstSurface, int32 SizeX, int32 SizeY)
{
	VulkanRHI::FPendingBarrier Barriers;
	int32 SourceIndex = Barriers.AddImageBarrier(SrcSurface, VK_IMAGE_ASPECT_COLOR_BIT, 1);
	int32 DestIndex = Barriers.AddImageBarrier(DstSurface, VK_IMAGE_ASPECT_COLOR_BIT, 1);

	// Prepare for copy
	Barriers.SetTransition(SourceIndex,
		bSourceReadOnly ? EImageLayoutBarrier::PixelShaderRead : EImageLayoutBarrier::ColorAttachment,
		EImageLayoutBarrier::TransferSource);
	Barriers.SetTransition(DestIndex, EImageLayoutBarrier::Undefined, EImageLayoutBarrier::TransferDest);
	Barriers.Execute(CmdBuffer);

	VkImageCopy Region;
	FMemory::Memzero(Region);
	Region.extent.width = SizeX;
	Region.extent.height = SizeY;
	Region.extent.depth = 1;
	Region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	//Region.srcSubresource.baseArrayLayer = 0;
	Region.srcSubresource.layerCount = 1;
	//Region.srcSubresource.mipLevel = 0;
	Region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	//Region.dstSubresource.baseArrayLayer = 0;
	Region.dstSubresource.layerCount = 1;
	//Region.dstSubresource.mipLevel = 0;
	VulkanRHI::vkCmdCopyImage(CmdBuffer->GetHandle(),
		SrcSurface, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		DstSurface, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &Region);

	// Prepare for present
	Barriers.ResetStages();
	Barriers.SetTransition(SourceIndex,
		EImageLayoutBarrier::TransferSource,
		bSourceReadOnly ? EImageLayoutBarrier::PixelShaderRead : EImageLayoutBarrier::ColorAttachment);
	Barriers.SetTransition(DestIndex, EImageLayoutBarrier::TransferDest, EImageLayoutBarrier::Present);
	Barriers.Execute(CmdBuffer);
}

bool FVulkanViewport::Present(FVulkanCommandListContext* Context, FVulkanCmdBuffer* CmdBuffer, FVulkanQueue* Queue, FVulkanQueue* PresentQueue, bool bLockToVsync)
{
	//Transition back buffer to presentable and submit that command
	check(CmdBuffer->IsOutsideRenderPass());

	if (DelayAcquireBackBuffer() && RenderingBackBuffer)
	{
		SCOPE_CYCLE_COUNTER(STAT_VulkanAcquireBackBuffer);
		if (FVulkanPlatform::SupportsStandardSwapchain())
		{
			if (!DoCheckedSwapChainJob(DoAcquireImageIndex))
			{
				UE_LOG(LogVulkanRHI, Fatal, TEXT("Swapchain acquire image index failed!"));
			}

			Context->RHIPushEvent(TEXT("CopyImageToBackBuffer"), FColor::Blue);
			CopyImageToBackBuffer(CmdBuffer, true, RenderingBackBuffer->Surface.Image, BackBufferImages[AcquiredImageIndex], SizeX, SizeY);
			Context->RHIPopEvent();
		}
	}
	else
	{
		check(AcquiredImageIndex != -1);

		check(RHIBackBuffer == nullptr || RHIBackBuffer->Surface.Image == BackBufferImages[AcquiredImageIndex]);

		VkImageLayout& Layout = Context->GetTransitionAndLayoutManager().FindOrAddLayoutRW(BackBufferImages[AcquiredImageIndex], VK_IMAGE_LAYOUT_UNDEFINED);
		VulkanRHI::ImagePipelineBarrier(CmdBuffer->GetHandle(), BackBufferImages[AcquiredImageIndex], VulkanRHI::GetImageLayoutFromVulkanLayout(Layout), EImageLayoutBarrier::Present, VulkanRHI::SetupImageSubresourceRange());
		Layout = VK_IMAGE_LAYOUT_UNDEFINED;
	}

	CmdBuffer->End();

	if (FVulkanPlatform::SupportsStandardSwapchain())
	{
		if (DelayAcquireBackBuffer())
		{
			CmdBuffer->AddWaitSemaphore(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, AcquiredSemaphore);
		}
		Queue->Submit(CmdBuffer, RenderingDoneSemaphores[AcquiredImageIndex]->GetHandle());
	}
	else
	{
		Queue->Submit(CmdBuffer);
	}

	//Flush all commands
	//check(0);

	//#todo-rco: Proper SyncInterval bLockToVsync ? RHIConsoleVariables::SyncInterval : 0
	int32 SyncInterval = 0;
	bool bNeedNativePresent = true;

	const bool bHasCustomPresent = IsValidRef(CustomPresent);
	if (bHasCustomPresent)
	{
		bNeedNativePresent = CustomPresent->Present(SyncInterval);
	}

	bool bResult = false;
	if (bNeedNativePresent && (!FVulkanPlatform::SupportsStandardSwapchain() || DelayAcquireBackBuffer() || RHIBackBuffer != nullptr))
	{
		// Present the back buffer to the viewport window.
		auto SwapChainJob = [Queue, PresentQueue](FVulkanViewport* Viewport)
		{
			return (int32)Viewport->SwapChain->Present(Queue, PresentQueue, Viewport->RenderingDoneSemaphores[Viewport->AcquiredImageIndex]);
		};
		if (FVulkanPlatform::SupportsStandardSwapchain() && !DoCheckedSwapChainJob(SwapChainJob))
		{
			UE_LOG(LogVulkanRHI, Fatal, TEXT("Swapchain present failed!"));
			bResult = false;
		}
		else
		{
			bResult = true;
		}

		if (bHasCustomPresent)
		{
			CustomPresent->PostPresent();
		}

		// Release the back buffer
		RHIBackBuffer = nullptr;
	}

	//static const TConsoleVariableData<int32>* CFinishFrameVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.FinishCurrentFrame"));
	//if (!CFinishFrameVar->GetValueOnRenderThread())
	//{
	//	// Wait for the GPU to finish rendering the previous frame before finishing this frame.
	//	WaitForFrameEventCompletion();
	//	IssueFrameEvent();
	//}
	//else
	//{
	//	// Finish current frame immediately to reduce latency
	//	IssueFrameEvent();
	//	WaitForFrameEventCompletion();
	//}

	// If the input latency timer has been triggered, block until the GPU is completely
	// finished displaying this frame and calculate the delta time.
	//if (GInputLatencyTimer.RenderThreadTrigger)
	//{
	//	WaitForFrameEventCompletion();
	//	uint32 EndTime = FPlatformTime::Cycles();
	//	GInputLatencyTimer.DeltaTime = EndTime - GInputLatencyTimer.StartTime;
	//	GInputLatencyTimer.RenderThreadTrigger = false;
	//}

	FVulkanCommandBufferManager* ImmediateCmdBufMgr = Device->GetImmediateContext().GetCommandBufferManager();
	// PrepareForNewActiveCommandBuffer might be called by swapchain re-creation routine. Skip prepare if we already have an open active buffer.
	if (ImmediateCmdBufMgr->GetActiveCmdBuffer() && !ImmediateCmdBufMgr->GetActiveCmdBuffer()->HasBegun())
	{
		ImmediateCmdBufMgr->PrepareForNewActiveCommandBuffer();
	}

	++PresentCount;
	++((FVulkanDynamicRHI*)GDynamicRHI)->TotalPresentCount;

	return bResult;
}

/*=============================================================================
 *	The following RHI functions must be called from the main thread.
 *=============================================================================*/
FViewportRHIRef FVulkanDynamicRHI::RHICreateViewport(void* WindowHandle, uint32 SizeX, uint32 SizeY, bool bIsFullscreen, EPixelFormat PreferredPixelFormat)
{
	check( IsInGameThread() );
	return new FVulkanViewport(this, Device, WindowHandle, SizeX, SizeY, bIsFullscreen, PreferredPixelFormat);
}

void FVulkanDynamicRHI::RHIResizeViewport(FViewportRHIParamRef ViewportRHI, uint32 SizeX, uint32 SizeY, bool bIsFullscreen)
{
	check(IsInGameThread());
	FVulkanViewport* Viewport = ResourceCast(ViewportRHI);

	if (Viewport->GetSizeXY() != FIntPoint(SizeX, SizeY))
	{
		FlushRenderingCommands();

		ENQUEUE_RENDER_COMMAND(ResizeViewport)(
			[Viewport, SizeX, SizeY, bIsFullscreen](FRHICommandListImmediate& RHICmdList)
			{
				Viewport->Resize(SizeX, SizeY, bIsFullscreen);
			});
		FlushRenderingCommands();
	}
}

void FVulkanDynamicRHI::RHITick(float DeltaTime)
{
	check(IsInGameThread());
	FVulkanDevice* VulkanDevice = GetDevice();
	ENQUEUE_RENDER_COMMAND(TempFrameReset)(
		[VulkanDevice](FRHICommandListImmediate& RHICmdList)
		{
			VulkanDevice->GetImmediateContext().GetTempFrameAllocationBuffer().Reset();
		});
}

FTexture2DRHIRef FVulkanDynamicRHI::RHIGetViewportBackBuffer(FViewportRHIParamRef ViewportRHI)
{
	check(IsInRenderingThread());
	check(ViewportRHI);
	FVulkanViewport* Viewport = ResourceCast(ViewportRHI);
	return Viewport->GetBackBuffer(FRHICommandListExecutor::GetImmediateCommandList());
}

void FVulkanDynamicRHI::RHIAdvanceFrameForGetViewportBackBuffer(FViewportRHIParamRef ViewportRHI)
{
	check(IsInRenderingThread());
	check(ViewportRHI);
	FVulkanViewport* Viewport = ResourceCast(ViewportRHI);
	Viewport->AdvanceBackBufferFrame();

	{
		FRHICommandList& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		if (RHICmdList.Bypass() || !IsRunningRHIInSeparateThread())
		{
			FRHICommandProcessDeferredDeletionQueue Cmd(Device);
			Cmd.Execute(RHICmdList);
		}
		else
		{
			check(IsInRenderingThread());
			new (RHICmdList.AllocCommand<FRHICommandProcessDeferredDeletionQueue>()) FRHICommandProcessDeferredDeletionQueue(Device);
		}
	}
}

void FVulkanCommandListContext::RHISetViewport(uint32 MinX, uint32 MinY, float MinZ, uint32 MaxX, uint32 MaxY, float MaxZ)
{
	PendingGfxState->SetViewport(MinX, MinY, MinZ, MaxX, MaxY, MaxZ);
}

void FVulkanCommandListContext::RHISetMultipleViewports(uint32 Count, const FViewportBounds* Data)
{
	VULKAN_SIGNAL_UNIMPLEMENTED();
}

void FVulkanCommandListContext::RHISetScissorRect(bool bEnable, uint32 MinX, uint32 MinY, uint32 MaxX, uint32 MaxY)
{
	PendingGfxState->SetScissor(bEnable, MinX, MinY, MaxX, MaxY);
}
