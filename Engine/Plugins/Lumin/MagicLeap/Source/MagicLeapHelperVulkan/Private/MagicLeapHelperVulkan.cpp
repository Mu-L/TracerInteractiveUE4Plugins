// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapHelperVulkan.h"
#include "IMagicLeapHelperVulkanPlugin.h"
#include "Engine/Engine.h"
#include "XRThreadUtils.h"

#include "Lumin/CAPIShims/LuminAPIGraphics.h"
#include "Lumin/CAPIShims/LuminAPIGraphicsUtils.h"

#if PLATFORM_SUPPORTS_VULKAN
#include "VulkanRHIPrivate.h"
#include "ScreenRendering.h"
#include "VulkanPendingState.h"
#include "VulkanContext.h"
#include "VulkanUtil.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogMagicLeapHelperVulkan, Display, All);

class FMagicLeapHelperVulkanPlugin : public IMagicLeapHelperVulkanPlugin
{};

IMPLEMENT_MODULE(FMagicLeapHelperVulkanPlugin, MagicLeapHelperVulkan);

//////////////////////////////////////////////////////////////////////////

void FMagicLeapHelperVulkan::BlitImage(uint64 SrcName, int32 SrcX, int32 SrcY, int32 SrcZ, int32 SrcWidth, int32 SrcHeight, int32 SrcDepth, uint64 DstName, int32 DstLayer, int32 DstX, int32 DstY, int32 DstZ, int32 DstWidth, int32 DstHeight, int32 DstDepth, bool bIsDepthStencil)
{
#if PLATFORM_SUPPORTS_VULKAN
	VkImage Src = (VkImage)SrcName;
	VkImage Dst = (VkImage)DstName;

	FVulkanDynamicRHI* RHI = (FVulkanDynamicRHI*)GDynamicRHI;

	FVulkanCommandBufferManager* CmdBufferMgr = RHI->GetDevice()->GetImmediateContext().GetCommandBufferManager();
	FVulkanCmdBuffer* CmdBuffer = CmdBufferMgr->GetUploadCmdBuffer();

	/*{
		VkClearColorValue Color;
		Color.float32[0] = 0;
		Color.float32[1] = 0;
		Color.float32[2] = 1;
		Color.float32[3] = 1;
		VkImageSubresourceRange Range;
		Range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		Range.baseMipLevel = 0;
		Range.levelCount = 1;
		Range.baseArrayLayer = 0;
		Range.layerCount = 1;
		VulkanRHI::vkCmdClearColorImage(CmdBuffer->GetHandle(), Src, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, &Color, 1, &Range);
	}*/

	VkImageBlit Region;
	FMemory::Memzero(Region);
	Region.srcOffsets[0].x = SrcX;
	Region.srcOffsets[0].y = SrcY;
	Region.srcOffsets[0].z = SrcZ;
	Region.srcOffsets[1].x = SrcX + SrcWidth;
	Region.srcOffsets[1].y = SrcY + SrcHeight;
	Region.srcOffsets[1].z = SrcZ + SrcDepth;
	Region.srcSubresource.aspectMask = bIsDepthStencil ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	Region.srcSubresource.layerCount = 1;
	Region.dstOffsets[0].x = DstX;
	// Unreal's viewport is bottom-left, ml_graphics is top-left so we invert the texture here.
	Region.dstOffsets[0].y = DstY + DstHeight;
	Region.dstOffsets[0].z = DstZ;
	Region.dstOffsets[1].x = DstX + DstWidth;
	Region.dstOffsets[1].y = DstY;
	Region.dstOffsets[1].z = DstZ + DstDepth;
	Region.dstSubresource.aspectMask = bIsDepthStencil ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	Region.dstSubresource.baseArrayLayer = DstLayer;
	Region.dstSubresource.layerCount = 1;
	VulkanRHI::vkCmdBlitImage(CmdBuffer->GetHandle(), Src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region, bIsDepthStencil ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
#endif
}

void FMagicLeapHelperVulkan::ClearImage(uint64 DstName, const FLinearColor& ClearColor, uint32 BaseMipLevel, uint32 LevelCount, uint32 BaseArrayLayer, uint32 LayerCount, bool bIsDepthStencil)
{
#if PLATFORM_SUPPORTS_VULKAN
	VkImage Dst = (VkImage)DstName;

	FVulkanDynamicRHI* RHI = (FVulkanDynamicRHI*)GDynamicRHI;

	FVulkanCommandBufferManager* CmdBufferMgr = RHI->GetDevice()->GetImmediateContext().GetCommandBufferManager();
	FVulkanCmdBuffer* CmdBuffer = CmdBufferMgr->GetUploadCmdBuffer();

	VkImageSubresourceRange Range;
	Range.aspectMask = bIsDepthStencil ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	Range.baseMipLevel = BaseMipLevel;
	Range.levelCount = LevelCount;
	Range.baseArrayLayer = BaseArrayLayer;
	Range.layerCount = LayerCount;
	if (bIsDepthStencil)
	{
		VkClearDepthStencilValue Value;
		Value.depth = FClearValueBinding::DepthFar.Value.DSValue.Depth;
		Value.stencil = FClearValueBinding::DepthFar.Value.DSValue.Stencil;

		VulkanRHI::vkCmdClearDepthStencilImage(CmdBuffer->GetHandle(), Dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &Value, 1, &Range);
	}
	else
	{
		VkClearColorValue Color;
		Color.float32[0] = ClearColor.R;
		Color.float32[1] = ClearColor.G;
		Color.float32[2] = ClearColor.B;
		Color.float32[3] = ClearColor.A;

		VulkanRHI::vkCmdClearColorImage(CmdBuffer->GetHandle(), Dst, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, &Color, 1, &Range);
	}
#endif
}

void FMagicLeapHelperVulkan::SignalObjects(uint64 SignalObject0, uint64 SignalObject1, uint64 WaitObject)
{
#if PLATFORM_SUPPORTS_VULKAN
	FVulkanDynamicRHI* RHI = (FVulkanDynamicRHI*)GDynamicRHI;

	FVulkanCommandBufferManager* CmdBufferMgr = RHI->GetDevice()->GetImmediateContext().GetCommandBufferManager();
	FVulkanCmdBuffer* CmdBuffer = CmdBufferMgr->GetUploadCmdBuffer();

	// VulkanRHI::FSemaphore is self recycling.
	VulkanRHI::FSemaphore* WaitSemaphore = new VulkanRHI::FSemaphore(*(RHI->GetDevice()), (VkSemaphore)WaitObject);
	CmdBuffer->AddWaitSemaphore(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, WaitSemaphore);

	VkSemaphore Semaphores[2] =
	{
		(VkSemaphore)SignalObject0,
		(VkSemaphore)SignalObject1
	};

	CmdBufferMgr->SubmitUploadCmdBuffer(2, Semaphores);
#endif
}

uint64 FMagicLeapHelperVulkan::AliasImageSRGB(const uint64 Allocation, const uint64 AllocationOffset, const uint32 Width, const uint32 Height)
{
#if PLATFORM_SUPPORTS_VULKAN
	VkImageCreateInfo ImageCreateInfo;

	// This must match the RenderTargetTexture image other than format, which we are aliasing as srgb to match the output of the tonemapper.
	ZeroVulkanStruct(ImageCreateInfo, VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
	ImageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	ImageCreateInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
	ImageCreateInfo.extent.width = Width;
	ImageCreateInfo.extent.height = Height;
	ImageCreateInfo.extent.depth = 1;
	ImageCreateInfo.mipLevels = 1;
	ImageCreateInfo.arrayLayers = 1;
	ImageCreateInfo.flags = 0;
	ImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	ImageCreateInfo.usage = 0;
	ImageCreateInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	ImageCreateInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	ImageCreateInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
	ImageCreateInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	ImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	ImageCreateInfo.queueFamilyIndexCount = 0;
	ImageCreateInfo.pQueueFamilyIndices = nullptr;
	ImageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	ImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;

	FVulkanDynamicRHI* RHI = (FVulkanDynamicRHI*)GDynamicRHI;
	FVulkanDevice* Device = RHI->GetDevice();
	VkImage Result = VK_NULL_HANDLE;
	VERIFYVULKANRESULT(VulkanRHI::vkCreateImage(Device->GetInstanceHandle(), &ImageCreateInfo, nullptr, &Result));

	VERIFYVULKANRESULT(VulkanRHI::vkBindImageMemory(Device->GetInstanceHandle(), Result, (VkDeviceMemory)Allocation, AllocationOffset));

	check(Result != VK_NULL_HANDLE);

	FVulkanCommandBufferManager* CmdBufferMgr = RHI->GetDevice()->GetImmediateContext().GetCommandBufferManager();
	FVulkanCmdBuffer* CmdBuffer = CmdBufferMgr->GetUploadCmdBuffer();

	VkImageSubresourceRange Range;
	Range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	Range.baseMipLevel = 0;
	Range.levelCount = 1;
	Range.baseArrayLayer = 0;
	Range.layerCount = 1;

	RHI->VulkanSetImageLayout(CmdBuffer->GetHandle(), Result, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Range);

	return (uint64)Result;
#else
	return 0;
#endif
}

void FMagicLeapHelperVulkan::DestroyImageSRGB(void* Image)
{
#if PLATFORM_SUPPORTS_VULKAN
	if (Image != VK_NULL_HANDLE)
	{
		FVulkanDynamicRHI* RHI = (FVulkanDynamicRHI*)GDynamicRHI;
		FVulkanDevice* Device = RHI->GetDevice();
		VulkanRHI::vkDestroyImage(Device->GetInstanceHandle(), reinterpret_cast<VkImage>(Image), nullptr);
	}
#endif
}

bool FMagicLeapHelperVulkan::GetVulkanInstanceExtensionsRequired(TArray<const ANSICHAR*>& Out)
{
#if PLATFORM_LUMIN
	// Used inside ml_graphics. We get an error from validation layers for this extension not being enabled.
	// TODO: Talk to graphics team for adding a MLGraphicsEnumerateRequiredVkInstanceExtensions() function so we dont have to hardcode this here.
	// TODO: Talk to Epic if VULKAN_ENABLE_DESKTOP_HMD_SUPPORT can/should be enabled on Lumin since the extension is supported.
	Out.Add(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#endif // PLATFORM_LUMIN

	return true;
}

bool FMagicLeapHelperVulkan::GetVulkanDeviceExtensionsRequired(VkPhysicalDevice_T* pPhysicalDevice, TArray<const ANSICHAR*>& Out)
{
#if PLATFORM_LUMIN
	// Get the extensions supported by the device through the RHI
	TArray<VkExtensionProperties> Properties;
	{
		uint32_t PropertyCount;
		VulkanRHI::vkEnumerateDeviceExtensionProperties((VkPhysicalDevice) pPhysicalDevice, nullptr, &PropertyCount, nullptr);
		Properties.SetNum(PropertyCount);
		VulkanRHI::vkEnumerateDeviceExtensionProperties((VkPhysicalDevice) pPhysicalDevice, nullptr, &PropertyCount, Properties.GetData());
	}

	// Get the extensions required by ML
	TArray<VkExtensionProperties> RequiredExtensions;
	{
		uint32_t PropertyCount = 0;
		MLGraphicsEnumerateRequiredVkDeviceExtensionsForMediaHandleImport(nullptr, &PropertyCount);
		RequiredExtensions.SetNum(PropertyCount);
		MLGraphicsEnumerateRequiredVkDeviceExtensionsForMediaHandleImport(RequiredExtensions.GetData(), &PropertyCount);
	}

	int32 ExtensionsFound = 0;
	for (int32 ExtensionIndex = 0; ExtensionIndex < RequiredExtensions.Num(); ExtensionIndex++)
	{
		for (int32 PropertyIndex = 0; PropertyIndex < Properties.Num(); PropertyIndex++)
		{
			if (!FCStringAnsi::Strcmp(Properties[PropertyIndex].extensionName, RequiredExtensions[ExtensionIndex].extensionName))
			{
				ANSICHAR* const FoundExtensionName = (ANSICHAR*)FMemory::Malloc(VK_MAX_EXTENSION_NAME_SIZE);
				FMemory::Memcpy(FoundExtensionName, RequiredExtensions[ExtensionIndex].extensionName, VK_MAX_EXTENSION_NAME_SIZE);
				Out.Add(FoundExtensionName);
				ExtensionsFound++;
				break;
			}
		}
	}

	const bool bFoundRequiredExtensions = (ExtensionsFound == RequiredExtensions.Num());
	GSupportsImageExternal = bFoundRequiredExtensions; // This should probably be set by the vk rhi if the needed extensions are supported VK_KHR_external_memory?

	// Used inside ml_graphics. We get an error from validation layers for these extensions not being enabled.
	// Added after the checks for GSupportsImageExternal so we don't taint its flag with these unrelated extensions.
	// TODO: Talk to graphics team for adding a MLGraphicsEnumerateRequiredVkDeviceExtensions() function so we dont have to hardcode this here.
	Out.Add("VK_KHR_external_semaphore");
	Out.Add("VK_KHR_external_semaphore_fd");

	return bFoundRequiredExtensions;
#endif //PLATFORM_LUMIN

	return true;
}

bool FMagicLeapHelperVulkan::GetMediaTexture(FTextureRHIRef& ResultTexture, FSamplerStateRHIRef& SamplerResult, const uint64 MediaTextureHandle)
{
#if PLATFORM_LUMIN
	FVulkanDynamicRHI* const RHI = (FVulkanDynamicRHI*)GDynamicRHI;
	FVulkanDevice* const Device = RHI->GetDevice();
	MLGraphicsImportedMediaSurface MediaSurface; 


	const MLResult Result = MLGraphicsImportVkImageFromMediaHandle(Device->GetInstanceHandle(), MediaTextureHandle, &MediaSurface);
	if (Result != MLResult_Ok)
	{
		return false;
	}

	VkImage ImportedImage = MediaSurface.imported_image;
	ExecuteOnRHIThread_DoNotWait([Device, ImportedImage]()
	{
		VkImageMemoryBarrier ImageBarrier;
		FMemory::Memzero(&ImageBarrier, sizeof(VkImageMemoryBarrier));
		ImageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		ImageBarrier.pNext = nullptr;
		ImageBarrier.srcAccessMask = 0;
		ImageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		ImageBarrier.oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
		ImageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		ImageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		ImageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		ImageBarrier.image = ImportedImage;
		ImageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		ImageBarrier.subresourceRange.baseMipLevel = 0;
		ImageBarrier.subresourceRange.levelCount = 1;
		ImageBarrier.subresourceRange.baseArrayLayer = 0;
		ImageBarrier.subresourceRange.layerCount = 1;

		FVulkanCommandListContext& ImmediateContext = Device->GetImmediateContext();
		FVulkanCmdBuffer* CmdBuffer = ImmediateContext.GetCommandBufferManager()->GetUploadCmdBuffer();
		VulkanRHI::vkCmdPipelineBarrier(
			CmdBuffer->GetHandle(),
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0,
			0,
			nullptr,
			0,
			nullptr,
			1,
			&ImageBarrier);
	});

	FSamplerYcbcrConversionInitializer ConversionInitializer;
	FMemory::Memzero(&ConversionInitializer, sizeof(FSamplerYcbcrConversionInitializer));
	ConversionInitializer.Format = MediaSurface.format;
	ConversionInitializer.ExternalFormat = MediaSurface.external_format;

	ConversionInitializer.Components.a = MediaSurface.sampler_ycbcr_conversion_components.a;
	ConversionInitializer.Components.r = MediaSurface.sampler_ycbcr_conversion_components.r;
	ConversionInitializer.Components.g = MediaSurface.sampler_ycbcr_conversion_components.g;
	ConversionInitializer.Components.b = MediaSurface.sampler_ycbcr_conversion_components.b;

	ConversionInitializer.Model = MediaSurface.suggested_ycbcr_model;
	ConversionInitializer.Range = MediaSurface.suggested_ycbcr_range;
	ConversionInitializer.XOffset = MediaSurface.suggested_x_chroma_offset;
	ConversionInitializer.YOffset = MediaSurface.suggested_y_chroma_offset;

	ResultTexture = RHI->RHICreateTexture2DFromResource(PF_B8G8R8A8, 1, 1, 1, 1, MediaSurface.imported_image, ConversionInitializer, 0);

	// Create a single sampler for the associated media player
	if (SamplerResult == nullptr)
	{
		FSamplerStateInitializerRHI SamplerStateInitializer(SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp);
		SamplerResult = RHI->RHICreateSamplerState(SamplerStateInitializer, ConversionInitializer);
	}

	// Insert the RHI thread lock fence. This stops any parallel translate tasks running until the command above has completed on the RHI thread.
	// There's an odd edge case where parallel rendering is trying to access the RHI's layout map and the command to add it hasn't completed;
	// wait for the RHI thread while we investigate the root cause of this issue.
	FRHICommandListImmediate& RHICmdList = GetImmediateCommandList_ForRenderCommand();
	FGraphEventRef Fence = RHICmdList.RHIThreadFence(true);
	FRHICommandListExecutor::WaitOnRHIThreadFence(Fence);

	return true;
#endif //PLATFORM_LUMIN
	return false;
}

void FMagicLeapHelperVulkan::AliasMediaTexture(FTextureRHIRef& DestTexture, FTextureRHIRef& SrcTexture)
{
#if PLATFORM_LUMIN
	GDynamicRHI->RHIAliasTextureResources((FTextureRHIRef&)DestTexture, (FTextureRHIRef&)SrcTexture);
#endif
}
