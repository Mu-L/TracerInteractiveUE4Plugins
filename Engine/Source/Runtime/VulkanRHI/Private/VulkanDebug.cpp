// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanDebug.cpp: Vulkan device RHI implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"

FAutoConsoleVariable GCVarUniqueValidationMessages(
	TEXT("r.Vulkan.UniqueValidationMessages"),
	1,
	TEXT("Filter out validation errors with the same code (only when r.Vulkan.EnableValidation is non zero)")
);

#define VULKAN_ENABLE_TRACKING_CALLSTACK			1

#define CREATE_MSG_CALLBACK							"vkCreateDebugReportCallbackEXT"
#define DESTROY_MSG_CALLBACK						"vkDestroyDebugReportCallbackEXT"

DEFINE_LOG_CATEGORY(LogVulkanRHI);

#if VULKAN_HAS_DEBUGGING_ENABLED

#if PLATFORM_ANDROID
	#define VULKAN_REPORT_LOG(Format, ...)	UE_LOG(LogVulkanRHI, Warning, Format, __VA_ARGS__)
#else
	#define VULKAN_REPORT_LOG(Format, ...)	do { if (FPlatformMisc::IsDebuggerPresent()) \
											{ \
												FPlatformMisc::LowLevelOutputDebugStringf(Format, __VA_ARGS__); FPlatformMisc::LowLevelOutputDebugString(TEXT("\n")); \
											} \
											UE_LOG(LogVulkanRHI, Warning, Format, __VA_ARGS__); } while (0)
#endif // PLATFORM_ANDROID

extern TAutoConsoleVariable<int32> GValidationCvar;

static VkBool32 VKAPI_PTR DebugReportFunction(
	VkDebugReportFlagsEXT			MsgFlags,
	VkDebugReportObjectTypeEXT		ObjType,
	uint64_t						SrcObject,
	size_t							Location,
	int32							MsgCode,
	const ANSICHAR*					LayerPrefix,
	const ANSICHAR*					Msg,
	void*							UserData)
{
#if VULKAN_ENABLE_DUMP_LAYER
	VulkanRHI::FlushDebugWrapperLog();
#endif

	const char* MsgPrefix = "UNKNOWN";
	if (MsgFlags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
	{
		// Ignore some errors we might not fix...
		if (!FCStringAnsi::Strcmp(LayerPrefix, "SC"))
		{
			if (MsgCode == 3)
			{
				// Attachment N not written by fragment shader
				return VK_FALSE;
			}
			else if (MsgCode == 5)
			{
				// SPIR-V module not valid: MemoryBarrier: Vulkan specification requires Memory Semantics to have one of the following bits set: Acquire, Release, AcquireRelease or SequentiallyConsistent
				return VK_FALSE;
			}
		}
		if (!FCStringAnsi::Strcmp(LayerPrefix, "DS"))
		{
			if (MsgCode == 6)
			{
				auto* Found = FCStringAnsi::Strstr(Msg, " array layer ");
				if (Found && Found[13] >= '1' && Found[13] <= '9')
				{
					// Potential bug in the validation layers for slice > 1 on 3d textures
					return VK_FALSE;
				}
			}
			else if (MsgCode == 15)
			{
				// Cannot get query results on queryPool 0x327 with index 193 as data has not been collected for this index.
				//return VK_FALSE;
			}
		}

		MsgPrefix = "ERROR";
	}
	else if (MsgFlags & VK_DEBUG_REPORT_WARNING_BIT_EXT)
	{
		MsgPrefix = "WARN";

		// Ignore some warnings we might not fix...
		if (!FCStringAnsi::Strcmp(LayerPrefix, "SC"))
		{
			if (MsgCode == 2)
			{
				// fragment shader writes to output location 0 with no matching attachment
				return VK_FALSE;
			}
		}

	}
	else if (MsgFlags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT)
	{
		MsgPrefix = "PERF";
		// Ignore some errors we might not fix...
		if (!FCStringAnsi::Strcmp(LayerPrefix, "SC"))
		{
			if (MsgCode == 2)
			{
				// vertex shader outputs unused interpolator
				return VK_FALSE;
			}
		}
		else if (!FCStringAnsi::Strcmp(LayerPrefix, "DS"))
		{
			if (MsgCode == 15)
			{
				// DescriptorSet previously bound is incompatible with set newly bound as set #0 so set #1 and any subsequent sets were disturbed by newly bound pipelineLayout
				return VK_FALSE;
			}
		}
	}
	else if (MsgFlags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT)
	{
		MsgPrefix = "INFO";
	}
	else if (MsgFlags & VK_DEBUG_REPORT_DEBUG_BIT_EXT)
	{
		MsgPrefix = "DEBUG";
	}
	else
	{
		ensure(0);
	}

	FString LayerCode = FString::Printf(TEXT("%s%x"), ANSI_TO_TCHAR(LayerPrefix), MsgCode);

	static TSet<FString> SeenCodes;
	if (GCVarUniqueValidationMessages->GetInt() == 0 || !SeenCodes.Contains(LayerCode))
	{
		VULKAN_REPORT_LOG(TEXT("*** [%s:%s] Obj 0x%p Loc %d %s\n"), ANSI_TO_TCHAR(MsgPrefix), *LayerCode, (void*)SrcObject, (uint32)Location, ANSI_TO_TCHAR(Msg));
		if (GCVarUniqueValidationMessages->GetInt() == 1)
		{
			SeenCodes.Add(LayerCode);
		}
	}

	return VK_FALSE;
}

#if VULKAN_SUPPORTS_DEBUG_UTILS
static VkBool32 DebugUtilsCallback(VkDebugUtilsMessageSeverityFlagBitsEXT MsgSeverity, VkDebugUtilsMessageTypeFlagsEXT MsgType,
	const VkDebugUtilsMessengerCallbackDataEXT* CallbackData, void* UserData)
{
	if (!CallbackData->pMessageIdName)
	{
		if (MsgType == 2 && CallbackData->messageIdNumber == 5)
		{
			// SPIR-V module not valid: MemoryBarrier: Vulkan specification requires Memory Semantics to have one of the following bits set: Acquire, Release, AcquireRelease or SequentiallyConsistent
			return VK_FALSE;
		}
		else if (MsgType == 2 && CallbackData->messageIdNumber == 2)
		{
			// fragment shader writes to output location 0 with no matching attachment
			return VK_FALSE;
		}
		else if (MsgType == 2 && CallbackData->messageIdNumber == 3)
		{
			// Attachment 2 not written by fragment shader
			return VK_FALSE;
		}
		else if (MsgType == 6 && CallbackData->messageIdNumber == 2)
		{
			// Vertex shader writes to output location 0.0 which is not consumed by fragment shader
			return VK_FALSE;
		}
	}

	const TCHAR* Severity = TEXT("");
	if (MsgSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
	{
		ensure((MsgSeverity & ~VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) == 0);
		Severity = TEXT("Error");
	}
	else if (MsgSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
	{
		ensure((MsgSeverity & ~VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) == 0);
		Severity = TEXT("Warning");
	}
	else if (MsgSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
	{
		ensure((MsgSeverity & ~VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) == 0);
		Severity = TEXT("Info");
	}
	else if (MsgSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
	{
		ensure((MsgSeverity & ~VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) == 0);
		Severity = TEXT("Verbose");
	}

	uint32 MsgBucket = 0;
	const TCHAR* Type = TEXT("");
	if (MsgType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)
	{
		if (MsgType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
		{
			ensure((MsgType & ~(VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)) == 0);
			Type = TEXT(" General/Validation");
			MsgBucket = 2;
		}
		else
		{
			ensure((MsgType & ~VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) == 0);
			Type = TEXT(" General");
			MsgBucket = 1;
		}
	}
	else if (MsgType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
	{
		if (MsgType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
		{
			ensure((MsgType & ~(VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)) == 0);
			Type = TEXT("Perf/Validation");
			MsgBucket = 2;
		}
		else
		{
			ensure((MsgType & ~VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) == 0);
			Type = TEXT("Validation");
			MsgBucket = 3;
		}
	}
	else if (MsgType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
	{
		ensure((MsgType & ~VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) == 0);
		Type = TEXT("Perf");
		MsgBucket = 4;
	}

	static TStaticArray<TSet<int32>, 10> SeenCodes;
	if (GCVarUniqueValidationMessages->GetInt() == 0 || !SeenCodes[MsgBucket].Contains(CallbackData->messageIdNumber))
	{
		if (CallbackData->pMessageIdName)
		{
			VULKAN_REPORT_LOG(TEXT("*** %s %s:%d(%s) %s\n"), Type, Severity, CallbackData->messageIdNumber, ANSI_TO_TCHAR(CallbackData->pMessageIdName), ANSI_TO_TCHAR(CallbackData->pMessage));
		}
		else
		{
			VULKAN_REPORT_LOG(TEXT("*** %s %s:%d %s\n"), Type, Severity, CallbackData->messageIdNumber, ANSI_TO_TCHAR(CallbackData->pMessage));
		}
		if (GCVarUniqueValidationMessages->GetInt() == 1)
		{
			SeenCodes[MsgBucket].Add(CallbackData->messageIdNumber);
		}
	}

	return VK_FALSE;
}
#endif

void FVulkanDynamicRHI::SetupDebugLayerCallback()
{
#if VULKAN_SUPPORTS_DEBUG_UTILS
	if (bSupportsDebugUtilsExt)
	{
		PFN_vkCreateDebugUtilsMessengerEXT CreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)(void*)VulkanRHI::vkGetInstanceProcAddr(Instance, "vkCreateDebugUtilsMessengerEXT");
		if (CreateDebugUtilsMessengerEXT)
		{
			VkDebugUtilsMessengerCreateInfoEXT CreateInfo;
			ZeroVulkanStruct(CreateInfo, VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);

			int32 CVar = GValidationCvar.GetValueOnRenderThread();
			CreateInfo.messageSeverity = (CVar >= 1 ? VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT : 0) |
				(CVar >= 2 ? VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT : 0) | (CVar >= 3 ? VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT : 0);
			CreateInfo.messageType = (CVar >= 1 ? (VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) : 0) |
				(CVar >= 3 ? VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT : 0);
			CreateInfo.pfnUserCallback = (PFN_vkDebugUtilsMessengerCallbackEXT)(void*)DebugUtilsCallback;
			VkResult Result = (*CreateDebugUtilsMessengerEXT)(Instance, &CreateInfo, nullptr, &Messenger);
			ensure(Result == VK_SUCCESS);
		}
	}
	else
#endif
	if (bSupportsDebugCallbackExt)
	{
		PFN_vkCreateDebugReportCallbackEXT CreateMsgCallback = (PFN_vkCreateDebugReportCallbackEXT)(void*)VulkanRHI::vkGetInstanceProcAddr(Instance, CREATE_MSG_CALLBACK);
		if (CreateMsgCallback)
		{
			VkDebugReportCallbackCreateInfoEXT CreateInfo;
			ZeroVulkanStruct(CreateInfo, VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT);
			CreateInfo.pfnCallback = DebugReportFunction;

			int32 CVar = GValidationCvar.GetValueOnRenderThread();
			switch (CVar)
			{
			default:
				CreateInfo.flags |= VK_DEBUG_REPORT_DEBUG_BIT_EXT;
				// Fall-through...
			case 4:
				CreateInfo.flags |= VK_DEBUG_REPORT_INFORMATION_BIT_EXT;
				// Fall-through...
			case 3:
				CreateInfo.flags |= VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
				// Fall-through...
			case 2:
				CreateInfo.flags |= VK_DEBUG_REPORT_WARNING_BIT_EXT;
				// Fall-through...
			case 1:
				CreateInfo.flags |= VK_DEBUG_REPORT_ERROR_BIT_EXT;
				break;
			case 0:
				// Nothing to do!
				break;
			}
			VkResult Result = CreateMsgCallback(Instance, &CreateInfo, nullptr, &MsgCallback);
			switch (Result)
			{
			case VK_SUCCESS:
				break;
			case VK_ERROR_OUT_OF_HOST_MEMORY:
				UE_LOG(LogVulkanRHI, Warning, TEXT("CreateMsgCallback: out of host memory/CreateMsgCallback Failure; debug reporting skipped"));
				break;
			default:
				UE_LOG(LogVulkanRHI, Warning, TEXT("CreateMsgCallback: unknown failure %d/CreateMsgCallback Failure; debug reporting skipped"), (int32)Result);
				break;
			}
		}
		else
		{
			UE_LOG(LogVulkanRHI, Warning, TEXT("GetProcAddr: Unable to find vkDbgCreateMsgCallback/vkGetInstanceProcAddr; debug reporting skipped!"));
		}
	}
	else
	{
		UE_LOG(LogVulkanRHI, Warning, TEXT("Instance does not support 'VK_EXT_debug_report' extension; debug reporting skipped!"));
		return;
	}
}

void FVulkanDynamicRHI::RemoveDebugLayerCallback()
{
#if VULKAN_SUPPORTS_DEBUG_UTILS
	if (Messenger != VK_NULL_HANDLE)
	{
		PFN_vkDestroyDebugUtilsMessengerEXT DestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)(void*)VulkanRHI::vkGetInstanceProcAddr(Instance, "vkDestroyDebugUtilsMessengerEXT");
		if (DestroyDebugUtilsMessengerEXT)
		{
			(*DestroyDebugUtilsMessengerEXT)(Instance, Messenger, nullptr);
		}
	}
	else
#endif
	if (MsgCallback != VK_NULL_HANDLE)
	{
		PFN_vkDestroyDebugReportCallbackEXT DestroyMsgCallback = (PFN_vkDestroyDebugReportCallbackEXT)(void*)VulkanRHI::vkGetInstanceProcAddr(Instance, DESTROY_MSG_CALLBACK);
		checkf(DestroyMsgCallback, TEXT("GetProcAddr: Unable to find vkDbgCreateMsgCallback\vkGetInstanceProcAddr Failure"));
		DestroyMsgCallback(Instance, MsgCallback, nullptr);
	}
}


#if VULKAN_ENABLE_TRACKING_LAYER

template <typename TResourceCreateInfoType>
struct TTrackingResource
{
	FString DebugName = TEXT("null");
	TResourceCreateInfoType CreateInfo;

#if VULKAN_ENABLE_TRACKING_CALLSTACK
	FString CreateCallstack;
#endif
};
static FCriticalSection GTrackingCS;

#if VULKAN_ENABLE_TRACKING_CALLSTACK
#include "HAL/PlatformStackWalk.h"

static FCriticalSection GStackTraceMutex;
static char GStackTrace[65536];
static void CaptureCallStack(FString& OutCallstack, uint32 Delta)
{
	FScopeLock ScopeLock(&GStackTraceMutex);
	GStackTrace[0] = 0;
	FPlatformStackWalk::StackWalkAndDump(GStackTrace, 65535, 3 + Delta);
	OutCallstack = ANSI_TO_TCHAR(GStackTrace);
}
#endif
#endif

#if VULKAN_ENABLE_IMAGE_TRACKING_LAYER
struct FTrackingImage
{
	struct
	{
		FString DebugName = TEXT("null");
		VkImageCreateInfo CreateInfo;
		bool bFromSwapchain = false;
	} Info;

	// ArrayLayers[Mips]
	TArray<TArray<VkImageLayout>> ArrayLayouts;

#if VULKAN_ENABLE_TRACKING_CALLSTACK
	struct FHistoryEntry
	{
		TArray<TArray<VkImageLayout>> ArrayLayouts;
		FString Callstack;
	};
	TArray<FHistoryEntry> History;
	FString CreateCallstack;
#endif

	void Setup(uint32 NumArrayLayers, uint32 NumMips, VkImageLayout Layout, bool bFromSwapchain)
	{
		if (bFromSwapchain)
		{
			Info.CreateInfo.arrayLayers = 1;
			Info.CreateInfo.imageType = VK_IMAGE_TYPE_2D;
			Info.CreateInfo.mipLevels = 1;
			Info.bFromSwapchain = bFromSwapchain;
		}
		ensure(NumArrayLayers > 0);
		ensure(NumMips > 0);
		ArrayLayouts.Empty(0);
		ArrayLayouts.AddDefaulted(NumArrayLayers);
		for (uint32 Index = 0; Index < NumArrayLayers; ++Index)
		{
			TArray<VkImageLayout>& MipLayouts = ArrayLayouts[Index];
			MipLayouts.Empty(0);
			for (uint32 MIndex = 0; MIndex < NumMips; ++MIndex)
			{
				MipLayouts.Add(Layout);
			}
		}
	}

	void Setup(const VkImageCreateInfo& CreateInfo, bool bFromSwapchain)
	{
		Info.CreateInfo = CreateInfo;
		Info.bFromSwapchain = bFromSwapchain;
		Setup(CreateInfo.arrayLayers, CreateInfo.mipLevels, CreateInfo.initialLayout, bFromSwapchain);
	}
};
static TMap<VkImage, FTrackingImage> GVulkanTrackingImageLayouts;
static TMap<VkImageView, TTrackingResource<VkImageViewCreateInfo>> GVulkanTrackingImageViews;
#endif

#if VULKAN_ENABLE_BUFFER_TRACKING_LAYER
static TMap<VkBuffer, TTrackingResource<VkBufferCreateInfo>> GVulkanTrackingBuffers;
static TMap<VkBuffer, TArray<VkBufferView>> GVulkanTrackingBufferToBufferViews;
static TMap<VkBufferView, TTrackingResource<VkBufferViewCreateInfo>> GVulkanTrackingBufferViews;
#endif


#if VULKAN_ENABLE_DUMP_LAYER
#include "Misc/OutputDeviceRedirector.h"
namespace VulkanRHI
{
	void TrackImageViewAdd(VkImageView View, const VkImageViewCreateInfo* CreateInfo);
	void TrackBufferViewAdd(VkBufferView View, const VkBufferViewCreateInfo* CreateInfo);

	static FCriticalSection CS;
	struct FMutexString
	{
		FString Inner;

		FMutexString& operator += (const TCHAR* S)
		{
			FScopeLock Lock(&CS);
			Inner += S;
			return *this;
		}

		FMutexString& operator += (const char* S)
		{
			FScopeLock Lock(&CS);
			Inner += ANSI_TO_TCHAR(S);
			return *this;
		}

		FMutexString& operator += (const char S)
		{
			FScopeLock Lock(&CS);
			char T[2] = "\0";
			T[0] = S;
			Inner += ANSI_TO_TCHAR(T);
			return *this;
		}

		FMutexString& operator = (TCHAR T)
		{
			FScopeLock Lock(&CS);
			Inner = TEXT("");
			Inner += T;
			return *this;
		}

		FMutexString& operator = (const TCHAR* S)
		{
			FScopeLock Lock(&CS);
			Inner = S;
			return *this;
		}

		FMutexString& operator += (const FString& S)
		{
			FScopeLock Lock(&CS);
			Inner += S;
			return *this;
		}

		int32 Len() const
		{
			FScopeLock Lock(&CS);
			return Inner.Len();
		}
	};

	static FMutexString DebugLog;
	static int32 DebugLine = 1;

	static const TCHAR* Tabs = TEXT("\t\t\t\t\t\t\t\t\t");

	static FCriticalSection GTrackLock;
	static TMap<VkImageView, VkImageViewCreateInfo> GImageViewTracker;
	static TMap<VkBufferView, VkBufferViewCreateInfo> GBufferViewTracker;
	// Image/Layer/Mip
	struct FImageLayout
	{
		void Init(uint32 NumLayers, uint32 NumMips)
		{
			ensure(Layouts.Num() == 0);
			Layouts.Empty(NumLayers);

			TArray<VkImageLayout> Temp;
			Temp.AddUninitialized(NumMips);
			for (uint32 Index = 0; Index < NumMips; ++Index)
			{
				Temp[Index] = VK_IMAGE_LAYOUT_UNDEFINED;
			}

			for (uint32 Index = 0; Index < NumLayers; ++Index)
			{
				Layouts.Add(Temp);
			}
		}

		TArray<TArray<VkImageLayout>> Layouts;
	};
	static TMap<VkImage, FImageLayout> GOLDImageLayoutTracker;
	struct FRenderPassInfo
	{
		TArray<VkAttachmentDescription> Descriptions;
		VkRenderPassCreateInfo Info;
	};
	static TMap<VkRenderPass, FRenderPassInfo> GRenderPassInfo;
	struct FFBInfo
	{
		TArray<VkImageView> Attachments;
		VkFramebufferCreateInfo Info;
	};
	static TMap<VkFramebuffer, FFBInfo> GFramebufferInfo;

	static void ValidationFail()
	{
		int i =0;
		++i;
	}

	void FlushDebugWrapperLog()
	{
		if (DebugLog.Len() > 0)
		{
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("VULKANRHI: %s"), *DebugLog.Inner);
			//GLog->Flush();
			//UE_LOG(LogVulkanRHI, Display, TEXT("Vulkan Wrapper Log:\n%s"), *DebugLog);
			//GLog->Flush();
			DebugLog = TEXT("");
		}
	}

	static void HandleFlushWrapperLog(const TArray<FString>& Args)
	{
		FlushDebugWrapperLog();
	}

	static FAutoConsoleCommand CVarVulkanFlushLog(
		TEXT("r.Vulkan.FlushLog"),
		TEXT("\n"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&HandleFlushWrapperLog)
		);

	static FString GetPipelineBindPointString(VkPipelineBindPoint BindPoint)
	{
		switch (BindPoint)
		{
		case VK_PIPELINE_BIND_POINT_GRAPHICS:
			return TEXT("GFX");
		case VK_PIPELINE_BIND_POINT_COMPUTE:
			return TEXT("COMPUTE");
		default:
			break;
		}
		return FString::Printf(TEXT("Unknown VkPipelineBindPoint %d"), (int32)BindPoint);
	}

	static FString GetVkFormatString(VkFormat Format)
	{
		switch (Format)
		{
			// + 10 to skip "VK_FORMAT"
#define VKSWITCHCASE(x)	case x: return TEXT(#x) + 10;
			VKSWITCHCASE(VK_FORMAT_UNDEFINED)
			VKSWITCHCASE(VK_FORMAT_R4G4_UNORM_PACK8)
			VKSWITCHCASE(VK_FORMAT_R4G4B4A4_UNORM_PACK16)
			VKSWITCHCASE(VK_FORMAT_B4G4R4A4_UNORM_PACK16)
			VKSWITCHCASE(VK_FORMAT_R5G6B5_UNORM_PACK16)
			VKSWITCHCASE(VK_FORMAT_B5G6R5_UNORM_PACK16)
			VKSWITCHCASE(VK_FORMAT_R5G5B5A1_UNORM_PACK16)
			VKSWITCHCASE(VK_FORMAT_B5G5R5A1_UNORM_PACK16)
			VKSWITCHCASE(VK_FORMAT_A1R5G5B5_UNORM_PACK16)
			VKSWITCHCASE(VK_FORMAT_R8_UNORM)
			VKSWITCHCASE(VK_FORMAT_R8_SNORM)
			VKSWITCHCASE(VK_FORMAT_R8_USCALED)
			VKSWITCHCASE(VK_FORMAT_R8_SSCALED)
			VKSWITCHCASE(VK_FORMAT_R8_UINT)
			VKSWITCHCASE(VK_FORMAT_R8_SINT)
			VKSWITCHCASE(VK_FORMAT_R8_SRGB)
			VKSWITCHCASE(VK_FORMAT_R8G8_UNORM)
			VKSWITCHCASE(VK_FORMAT_R8G8_SNORM)
			VKSWITCHCASE(VK_FORMAT_R8G8_USCALED)
			VKSWITCHCASE(VK_FORMAT_R8G8_SSCALED)
			VKSWITCHCASE(VK_FORMAT_R8G8_UINT)
			VKSWITCHCASE(VK_FORMAT_R8G8_SINT)
			VKSWITCHCASE(VK_FORMAT_R8G8_SRGB)
			VKSWITCHCASE(VK_FORMAT_R8G8B8_UNORM)
			VKSWITCHCASE(VK_FORMAT_R8G8B8_SNORM)
			VKSWITCHCASE(VK_FORMAT_R8G8B8_USCALED)
			VKSWITCHCASE(VK_FORMAT_R8G8B8_SSCALED)
			VKSWITCHCASE(VK_FORMAT_R8G8B8_UINT)
			VKSWITCHCASE(VK_FORMAT_R8G8B8_SINT)
			VKSWITCHCASE(VK_FORMAT_R8G8B8_SRGB)
			VKSWITCHCASE(VK_FORMAT_B8G8R8_UNORM)
			VKSWITCHCASE(VK_FORMAT_B8G8R8_SNORM)
			VKSWITCHCASE(VK_FORMAT_B8G8R8_USCALED)
			VKSWITCHCASE(VK_FORMAT_B8G8R8_SSCALED)
			VKSWITCHCASE(VK_FORMAT_B8G8R8_UINT)
			VKSWITCHCASE(VK_FORMAT_B8G8R8_SINT)
			VKSWITCHCASE(VK_FORMAT_B8G8R8_SRGB)
			VKSWITCHCASE(VK_FORMAT_R8G8B8A8_UNORM)
			VKSWITCHCASE(VK_FORMAT_R8G8B8A8_SNORM)
			VKSWITCHCASE(VK_FORMAT_R8G8B8A8_USCALED)
			VKSWITCHCASE(VK_FORMAT_R8G8B8A8_SSCALED)
			VKSWITCHCASE(VK_FORMAT_R8G8B8A8_UINT)
			VKSWITCHCASE(VK_FORMAT_R8G8B8A8_SINT)
			VKSWITCHCASE(VK_FORMAT_R8G8B8A8_SRGB)
			VKSWITCHCASE(VK_FORMAT_B8G8R8A8_UNORM)
			VKSWITCHCASE(VK_FORMAT_B8G8R8A8_SNORM)
			VKSWITCHCASE(VK_FORMAT_B8G8R8A8_USCALED)
			VKSWITCHCASE(VK_FORMAT_B8G8R8A8_SSCALED)
			VKSWITCHCASE(VK_FORMAT_B8G8R8A8_UINT)
			VKSWITCHCASE(VK_FORMAT_B8G8R8A8_SINT)
			VKSWITCHCASE(VK_FORMAT_B8G8R8A8_SRGB)
			VKSWITCHCASE(VK_FORMAT_A8B8G8R8_UNORM_PACK32)
			VKSWITCHCASE(VK_FORMAT_A8B8G8R8_SNORM_PACK32)
			VKSWITCHCASE(VK_FORMAT_A8B8G8R8_USCALED_PACK32)
			VKSWITCHCASE(VK_FORMAT_A8B8G8R8_SSCALED_PACK32)
			VKSWITCHCASE(VK_FORMAT_A8B8G8R8_UINT_PACK32)
			VKSWITCHCASE(VK_FORMAT_A8B8G8R8_SINT_PACK32)
			VKSWITCHCASE(VK_FORMAT_A8B8G8R8_SRGB_PACK32)
			VKSWITCHCASE(VK_FORMAT_A2R10G10B10_UNORM_PACK32)
			VKSWITCHCASE(VK_FORMAT_A2R10G10B10_SNORM_PACK32)
			VKSWITCHCASE(VK_FORMAT_A2R10G10B10_USCALED_PACK32)
			VKSWITCHCASE(VK_FORMAT_A2R10G10B10_SSCALED_PACK32)
			VKSWITCHCASE(VK_FORMAT_A2R10G10B10_UINT_PACK32)
			VKSWITCHCASE(VK_FORMAT_A2R10G10B10_SINT_PACK32)
			VKSWITCHCASE(VK_FORMAT_A2B10G10R10_UNORM_PACK32)
			VKSWITCHCASE(VK_FORMAT_A2B10G10R10_SNORM_PACK32)
			VKSWITCHCASE(VK_FORMAT_A2B10G10R10_USCALED_PACK32)
			VKSWITCHCASE(VK_FORMAT_A2B10G10R10_SSCALED_PACK32)
			VKSWITCHCASE(VK_FORMAT_A2B10G10R10_UINT_PACK32)
			VKSWITCHCASE(VK_FORMAT_A2B10G10R10_SINT_PACK32)
			VKSWITCHCASE(VK_FORMAT_R16_UNORM)
			VKSWITCHCASE(VK_FORMAT_R16_SNORM)
			VKSWITCHCASE(VK_FORMAT_R16_USCALED)
			VKSWITCHCASE(VK_FORMAT_R16_SSCALED)
			VKSWITCHCASE(VK_FORMAT_R16_UINT)
			VKSWITCHCASE(VK_FORMAT_R16_SINT)
			VKSWITCHCASE(VK_FORMAT_R16_SFLOAT)
			VKSWITCHCASE(VK_FORMAT_R16G16_UNORM)
			VKSWITCHCASE(VK_FORMAT_R16G16_SNORM)
			VKSWITCHCASE(VK_FORMAT_R16G16_USCALED)
			VKSWITCHCASE(VK_FORMAT_R16G16_SSCALED)
			VKSWITCHCASE(VK_FORMAT_R16G16_UINT)
			VKSWITCHCASE(VK_FORMAT_R16G16_SINT)
			VKSWITCHCASE(VK_FORMAT_R16G16_SFLOAT)
			VKSWITCHCASE(VK_FORMAT_R16G16B16_UNORM)
			VKSWITCHCASE(VK_FORMAT_R16G16B16_SNORM)
			VKSWITCHCASE(VK_FORMAT_R16G16B16_USCALED)
			VKSWITCHCASE(VK_FORMAT_R16G16B16_SSCALED)
			VKSWITCHCASE(VK_FORMAT_R16G16B16_UINT)
			VKSWITCHCASE(VK_FORMAT_R16G16B16_SINT)
			VKSWITCHCASE(VK_FORMAT_R16G16B16_SFLOAT)
			VKSWITCHCASE(VK_FORMAT_R16G16B16A16_UNORM)
			VKSWITCHCASE(VK_FORMAT_R16G16B16A16_SNORM)
			VKSWITCHCASE(VK_FORMAT_R16G16B16A16_USCALED)
			VKSWITCHCASE(VK_FORMAT_R16G16B16A16_SSCALED)
			VKSWITCHCASE(VK_FORMAT_R16G16B16A16_UINT)
			VKSWITCHCASE(VK_FORMAT_R16G16B16A16_SINT)
			VKSWITCHCASE(VK_FORMAT_R16G16B16A16_SFLOAT)
			VKSWITCHCASE(VK_FORMAT_R32_UINT)
			VKSWITCHCASE(VK_FORMAT_R32_SINT)
			VKSWITCHCASE(VK_FORMAT_R32_SFLOAT)
			VKSWITCHCASE(VK_FORMAT_R32G32_UINT)
			VKSWITCHCASE(VK_FORMAT_R32G32_SINT)
			VKSWITCHCASE(VK_FORMAT_R32G32_SFLOAT)
			VKSWITCHCASE(VK_FORMAT_R32G32B32_UINT)
			VKSWITCHCASE(VK_FORMAT_R32G32B32_SINT)
			VKSWITCHCASE(VK_FORMAT_R32G32B32_SFLOAT)
			VKSWITCHCASE(VK_FORMAT_R32G32B32A32_UINT)
			VKSWITCHCASE(VK_FORMAT_R32G32B32A32_SINT)
			VKSWITCHCASE(VK_FORMAT_R32G32B32A32_SFLOAT)
			VKSWITCHCASE(VK_FORMAT_R64_UINT)
			VKSWITCHCASE(VK_FORMAT_R64_SINT)
			VKSWITCHCASE(VK_FORMAT_R64_SFLOAT)
			VKSWITCHCASE(VK_FORMAT_R64G64_UINT)
			VKSWITCHCASE(VK_FORMAT_R64G64_SINT)
			VKSWITCHCASE(VK_FORMAT_R64G64_SFLOAT)
			VKSWITCHCASE(VK_FORMAT_R64G64B64_UINT)
			VKSWITCHCASE(VK_FORMAT_R64G64B64_SINT)
			VKSWITCHCASE(VK_FORMAT_R64G64B64_SFLOAT)
			VKSWITCHCASE(VK_FORMAT_R64G64B64A64_UINT)
			VKSWITCHCASE(VK_FORMAT_R64G64B64A64_SINT)
			VKSWITCHCASE(VK_FORMAT_R64G64B64A64_SFLOAT)
			VKSWITCHCASE(VK_FORMAT_B10G11R11_UFLOAT_PACK32)
			VKSWITCHCASE(VK_FORMAT_E5B9G9R9_UFLOAT_PACK32)
			VKSWITCHCASE(VK_FORMAT_D16_UNORM)
			VKSWITCHCASE(VK_FORMAT_X8_D24_UNORM_PACK32)
			VKSWITCHCASE(VK_FORMAT_D32_SFLOAT)
			VKSWITCHCASE(VK_FORMAT_S8_UINT)
			VKSWITCHCASE(VK_FORMAT_D16_UNORM_S8_UINT)
			VKSWITCHCASE(VK_FORMAT_D24_UNORM_S8_UINT)
			VKSWITCHCASE(VK_FORMAT_D32_SFLOAT_S8_UINT)
			VKSWITCHCASE(VK_FORMAT_BC1_RGB_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC1_RGB_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC1_RGBA_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC1_RGBA_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC2_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC2_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC3_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC3_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC4_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC4_SNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC5_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC5_SNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC6H_UFLOAT_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC6H_SFLOAT_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC7_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_BC7_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_EAC_R11_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_EAC_R11_SNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_EAC_R11G11_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_EAC_R11G11_SNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_4x4_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_4x4_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_5x4_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_5x4_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_5x5_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_5x5_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_6x5_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_6x5_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_6x6_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_6x6_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_8x5_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_8x5_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_8x6_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_8x6_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_8x8_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_8x8_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_10x5_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_10x5_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_10x6_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_10x6_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_10x8_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_10x8_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_10x10_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_10x10_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_12x10_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_12x10_SRGB_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_12x12_UNORM_BLOCK)
			VKSWITCHCASE(VK_FORMAT_ASTC_12x12_SRGB_BLOCK)
#undef VKSWITCHCASE
		default:
			break;
		}
		return FString::Printf(TEXT("Unknown VkFormat %d"), (int32)Format);
	}

	static FString GetVkResultErrorString(VkResult Result)
	{
		switch (Result)
		{
			// + 3 to skip "VK_"
#define VKSWITCHCASE(x)	case x: return TEXT(#x) + 3;
			VKSWITCHCASE(VK_SUCCESS)
			VKSWITCHCASE(VK_NOT_READY)
			VKSWITCHCASE(VK_TIMEOUT)
			VKSWITCHCASE(VK_EVENT_SET)
			VKSWITCHCASE(VK_EVENT_RESET)
			VKSWITCHCASE(VK_INCOMPLETE)
			VKSWITCHCASE(VK_ERROR_OUT_OF_HOST_MEMORY)
			VKSWITCHCASE(VK_ERROR_OUT_OF_DEVICE_MEMORY)
			VKSWITCHCASE(VK_ERROR_INITIALIZATION_FAILED)
			VKSWITCHCASE(VK_ERROR_DEVICE_LOST)
			VKSWITCHCASE(VK_ERROR_MEMORY_MAP_FAILED)
			VKSWITCHCASE(VK_ERROR_LAYER_NOT_PRESENT)
			VKSWITCHCASE(VK_ERROR_EXTENSION_NOT_PRESENT)
			VKSWITCHCASE(VK_ERROR_FEATURE_NOT_PRESENT)
			VKSWITCHCASE(VK_ERROR_INCOMPATIBLE_DRIVER)
			VKSWITCHCASE(VK_ERROR_TOO_MANY_OBJECTS)
			VKSWITCHCASE(VK_ERROR_FORMAT_NOT_SUPPORTED)
			VKSWITCHCASE(VK_ERROR_SURFACE_LOST_KHR)
			VKSWITCHCASE(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR)
			VKSWITCHCASE(VK_SUBOPTIMAL_KHR)
			VKSWITCHCASE(VK_ERROR_OUT_OF_DATE_KHR)
			VKSWITCHCASE(VK_ERROR_INCOMPATIBLE_DISPLAY_KHR)
			VKSWITCHCASE(VK_ERROR_VALIDATION_FAILED_EXT)
#if VK_HEADER_VERSION >= 13
			VKSWITCHCASE(VK_ERROR_INVALID_SHADER_NV)
#endif
#if VK_HEADER_VERSION >= 24
			VKSWITCHCASE(VK_ERROR_FRAGMENTED_POOL)
#endif
#if VK_HEADER_VERSION >= 39
			VKSWITCHCASE(VK_ERROR_OUT_OF_POOL_MEMORY_KHR)
#endif
#if VK_HEADER_VERSION >= 65
			VKSWITCHCASE(VK_ERROR_INVALID_EXTERNAL_HANDLE_KHR)
			VKSWITCHCASE(VK_ERROR_NOT_PERMITTED_EXT)
#endif
#undef VKSWITCHCASE
		default:
			break;
		}

		return FString::Printf(TEXT("Unknown VkResult %d"), (int32)Result);
	}

	FString GetImageTilingString(VkImageTiling Tiling)
	{
		switch (Tiling)
		{
			// + 16 to skip "VK_IMAGE_TILING_"
#define VKSWITCHCASE(x)	case x: return TEXT(#x) + 16;
			VKSWITCHCASE(VK_IMAGE_TILING_OPTIMAL)
			VKSWITCHCASE(VK_IMAGE_TILING_LINEAR)
#undef VKSWITCHCASE
		default:
			break;
		}

		return FString::Printf(TEXT("Unknown VkImageTiling %d"), (int32)Tiling);
	}

	static FString GetImageLayoutString(VkImageLayout Layout)
	{
		switch (Layout)
		{
			// + 16 to skip "VK_IMAGE_LAYOUT"
#define VKSWITCHCASE(x)	case x: return TEXT(#x) + 16;
			VKSWITCHCASE(VK_IMAGE_LAYOUT_UNDEFINED)
			VKSWITCHCASE(VK_IMAGE_LAYOUT_GENERAL)
			VKSWITCHCASE(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
			VKSWITCHCASE(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
			VKSWITCHCASE(VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
			VKSWITCHCASE(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			VKSWITCHCASE(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
			VKSWITCHCASE(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
			VKSWITCHCASE(VK_IMAGE_LAYOUT_PREINITIALIZED)
			VKSWITCHCASE(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
#undef VKSWITCHCASE
		default:
			break;
		}

		return FString::Printf(TEXT("Unknown VkImageLayout %d"), (int32)Layout);
	}

	static FString GetImageViewTypeString(VkImageViewType Type)
	{
		switch (Type)
		{
			// + 19 to skip "VK_IMAGE_VIEW_TYPE_"
#define VKSWITCHCASE(x)	case x: return TEXT(#x) + 19;
		VKSWITCHCASE(VK_IMAGE_VIEW_TYPE_1D)
		VKSWITCHCASE(VK_IMAGE_VIEW_TYPE_2D)
		VKSWITCHCASE(VK_IMAGE_VIEW_TYPE_3D)
		VKSWITCHCASE(VK_IMAGE_VIEW_TYPE_CUBE)
		VKSWITCHCASE(VK_IMAGE_VIEW_TYPE_1D_ARRAY)
		VKSWITCHCASE(VK_IMAGE_VIEW_TYPE_2D_ARRAY)
		VKSWITCHCASE(VK_IMAGE_VIEW_TYPE_CUBE_ARRAY)
#undef VKSWITCHCASE
		default:
			break;
		}

		return FString::Printf(TEXT("Unknown VkImageViewType %d"), (int32)Type);
	}

	static FString GetImageTypeString(VkImageType Type)
	{
		switch (Type)
		{
			// + 14 to skip "VK_IMAGE_TYPE_1D"
#define VKSWITCHCASE(x)	case x: return TEXT(#x) + 14;
		VKSWITCHCASE(VK_IMAGE_TYPE_1D)
		VKSWITCHCASE(VK_IMAGE_TYPE_2D)
		VKSWITCHCASE(VK_IMAGE_TYPE_3D)
#undef VKSWITCHCASE
		default:
			break;
		}

		return FString::Printf(TEXT("Unknown VkImageType %d"), (int32)Type);
	}

	static FString GetDescriptorTypeString(VkDescriptorType Type)
	{
		switch (Type)
		{
			// + 19 to skip "VK_DESCRIPTOR_TYPE_"
#define VKSWITCHCASE(x)	case x: return TEXT(#x) + 19;
		VKSWITCHCASE(VK_DESCRIPTOR_TYPE_SAMPLER)
		VKSWITCHCASE(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
		VKSWITCHCASE(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
		VKSWITCHCASE(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
		VKSWITCHCASE(VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER)
		VKSWITCHCASE(VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)
		VKSWITCHCASE(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
		VKSWITCHCASE(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
		VKSWITCHCASE(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
		VKSWITCHCASE(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
		VKSWITCHCASE(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)
#undef VKSWITCHCASE
		default:
			break;
		}

		return FString::Printf(TEXT("Unknown VkDescriptorType %d"), (int32)Type);
	}

	static FString GetStencilOpString(VkStencilOp Op)
	{
		switch (Op)
		{
			// + 14 to skip "VK_STENCIL_OP_"
#define VKSWITCHCASE(x)	case x: return TEXT(#x) + 14;
			VKSWITCHCASE(VK_STENCIL_OP_KEEP)
			VKSWITCHCASE(VK_STENCIL_OP_ZERO)
			VKSWITCHCASE(VK_STENCIL_OP_REPLACE)
			VKSWITCHCASE(VK_STENCIL_OP_INCREMENT_AND_CLAMP)
			VKSWITCHCASE(VK_STENCIL_OP_DECREMENT_AND_CLAMP)
			VKSWITCHCASE(VK_STENCIL_OP_INVERT)
			VKSWITCHCASE(VK_STENCIL_OP_INCREMENT_AND_WRAP)
			VKSWITCHCASE(VK_STENCIL_OP_DECREMENT_AND_WRAP)
#undef VKSWITCHCASE
		default:
			break;
		}

		return FString::Printf(TEXT("Unknown VkStencilOp %d"), (int32)Op);
	}

	static FString GetCompareOpString(VkCompareOp Op)
	{
		switch (Op)
		{
			// + 14 to skip "VK_COMPARE_OP_"
#define VKSWITCHCASE(x)	case x: return TEXT(#x) + 14;
			VKSWITCHCASE(VK_COMPARE_OP_NEVER)
			VKSWITCHCASE(VK_COMPARE_OP_LESS)
			VKSWITCHCASE(VK_COMPARE_OP_EQUAL)
			VKSWITCHCASE(VK_COMPARE_OP_LESS_OR_EQUAL)
			VKSWITCHCASE(VK_COMPARE_OP_GREATER)
			VKSWITCHCASE(VK_COMPARE_OP_NOT_EQUAL)
			VKSWITCHCASE(VK_COMPARE_OP_GREATER_OR_EQUAL)
			VKSWITCHCASE(VK_COMPARE_OP_ALWAYS)
#undef VKSWITCHCASE
		default:
			break;
		}

		return FString::Printf(TEXT("Unknown VkStencilOp %d"), (int32)Op);
	}

	static FString GetComponentMappingString(const VkComponentMapping& Mapping)
	{
		auto GetSwizzle = [](VkComponentSwizzle Swizzle) -> const TCHAR*
			{
				switch (Swizzle)
				{
				case VK_COMPONENT_SWIZZLE_IDENTITY: return TEXT("ID");
				case VK_COMPONENT_SWIZZLE_ZERO:		return TEXT("0");
				case VK_COMPONENT_SWIZZLE_ONE:		return TEXT("1");
				case VK_COMPONENT_SWIZZLE_R:		return TEXT("R");
				case VK_COMPONENT_SWIZZLE_G:		return TEXT("G");
				case VK_COMPONENT_SWIZZLE_B:		return TEXT("B");
				case VK_COMPONENT_SWIZZLE_A:		return TEXT("A");
				default:
					check(0);
					return TEXT("-");
				}
			};
		return FString::Printf(TEXT("(r=%s, g=%s, b=%s, a=%s)"), GetSwizzle(Mapping.r), GetSwizzle(Mapping.g), GetSwizzle(Mapping.b), GetSwizzle(Mapping.a));
	}


#define AppendBitFieldName(BitField, Name) \
	if ((Flags & BitField) == BitField)\
	{\
		Flags &= ~BitField;\
		if (String.Len() > 0)\
		{\
			String += TEXT("|");\
		}\
		String += Name;\
	}

	static FString GetAspectMaskString(VkImageAspectFlags Flags)
	{
		if (Flags == 0)
		{
			return TEXT("0");
		}
		FString String;
		AppendBitFieldName(VK_IMAGE_ASPECT_COLOR_BIT, TEXT("COLOR"));
		AppendBitFieldName(VK_IMAGE_ASPECT_DEPTH_BIT, TEXT("DEPTH"));
		AppendBitFieldName(VK_IMAGE_ASPECT_STENCIL_BIT, TEXT("STENCIL"));
		AppendBitFieldName(VK_IMAGE_ASPECT_METADATA_BIT, TEXT("METADATA"));
		if (Flags != 0)
		{
			FString Unknown = FString::Printf(TEXT("%d"), Flags);
			AppendBitFieldName(Flags, Unknown);
		}
		return String;
	}

	static FString GetAccessFlagString(VkAccessFlags Flags)
	{
		if (Flags == 0)
		{
			return TEXT("0");
		}
		FString String;
		AppendBitFieldName(VK_ACCESS_INDIRECT_COMMAND_READ_BIT, TEXT("INDIRECT_COMMAND"));
		AppendBitFieldName(VK_ACCESS_INDEX_READ_BIT, TEXT("INDEX_READ"));
		AppendBitFieldName(VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, TEXT("VERTEX_ATTR_READ"));
		AppendBitFieldName(VK_ACCESS_UNIFORM_READ_BIT, TEXT("UNIF_READ"));
		AppendBitFieldName(VK_ACCESS_INPUT_ATTACHMENT_READ_BIT, TEXT("INPUT_ATT_READ"));
		AppendBitFieldName(VK_ACCESS_SHADER_READ_BIT, TEXT("SHADER_READ"));
		AppendBitFieldName(VK_ACCESS_SHADER_WRITE_BIT, TEXT("SHADER_WRITE"));
		AppendBitFieldName(VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, TEXT("COLOR_ATT_READ"));
		AppendBitFieldName(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, TEXT("COLOR_ATT_WRITE"));
		AppendBitFieldName(VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT, TEXT("DS_ATT_READ"));
		AppendBitFieldName(VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, TEXT("DS_ATT_WRITE"));
		AppendBitFieldName(VK_ACCESS_TRANSFER_READ_BIT, TEXT("TRANSFER_READ"));
		AppendBitFieldName(VK_ACCESS_TRANSFER_WRITE_BIT, TEXT("TRANSFER_WRITE"));
		AppendBitFieldName(VK_ACCESS_HOST_READ_BIT, TEXT("HOST_READ"));
		AppendBitFieldName(VK_ACCESS_HOST_WRITE_BIT, TEXT("HOST_WRITE"));
		AppendBitFieldName(VK_ACCESS_MEMORY_READ_BIT, TEXT("MEM_READ"));
		AppendBitFieldName(VK_ACCESS_MEMORY_WRITE_BIT, TEXT("MEM_WRITE"));
		if (Flags != 0)
		{
			FString Unknown = FString::Printf(TEXT("%d"), Flags);
			AppendBitFieldName(Flags, Unknown);
		}
		return String;
	}

	FString GetSampleCountString(VkSampleCountFlags Flags)
	{
		if (Flags == 0)
		{
			return TEXT("0");
		}
		FString String;
		AppendBitFieldName(VK_SAMPLE_COUNT_1_BIT, TEXT("1"));
		AppendBitFieldName(VK_SAMPLE_COUNT_2_BIT, TEXT("2"));
		AppendBitFieldName(VK_SAMPLE_COUNT_4_BIT, TEXT("4"));
		AppendBitFieldName(VK_SAMPLE_COUNT_8_BIT, TEXT("8"));
		AppendBitFieldName(VK_SAMPLE_COUNT_16_BIT, TEXT("16"));
		AppendBitFieldName(VK_SAMPLE_COUNT_32_BIT, TEXT("32"));
		AppendBitFieldName(VK_SAMPLE_COUNT_64_BIT, TEXT("64"));
		if (Flags != 0)
		{
			FString Unknown = FString::Printf(TEXT("%d"), Flags);
			AppendBitFieldName(Flags, Unknown);
		}
		return String;
	}

	FString GetImageUsageString(VkImageUsageFlags Flags)
	{
		if (Flags == 0)
		{
			return TEXT("0");
		}
		FString String;
		AppendBitFieldName(VK_IMAGE_USAGE_TRANSFER_SRC_BIT, TEXT("XFER_SRC"));
		AppendBitFieldName(VK_IMAGE_USAGE_TRANSFER_DST_BIT, TEXT("XFER_DST"));
		AppendBitFieldName(VK_IMAGE_USAGE_SAMPLED_BIT, TEXT("SAMPLED"));
		AppendBitFieldName(VK_IMAGE_USAGE_STORAGE_BIT, TEXT("STORAGE"));
		AppendBitFieldName(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, TEXT("COLOR_ATT"));
		AppendBitFieldName(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, TEXT("DS_ATT"));
		AppendBitFieldName(VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT, TEXT("TRANS_ATT"));
		AppendBitFieldName(VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, TEXT("IN_ATT"));
		if (Flags != 0)
		{
			FString Unknown = FString::Printf(TEXT("%d"), Flags);
			AppendBitFieldName(Flags, Unknown);
		}
		return String;
	}
#undef  AppendBitFieldName

	static FString GetImageSubresourceLayers(const VkImageSubresourceLayers& Layer)
	{
		return FString::Printf(TEXT("Aspect=%s MipLvl=%d BaseArray=%d NumLayers=%d"), *GetAspectMaskString(Layer.aspectMask), Layer.mipLevel, Layer.baseArrayLayer, Layer.layerCount);
	}

	FString GetExtentString(const VkExtent3D& Extent)
	{
		return FString::Printf(TEXT("w:%d h:%d d:%d"), Extent.width, Extent.height, Extent.depth);
	}

	static FString GetExtentString(const VkExtent2D& Extent)
	{
		return FString::Printf(TEXT("w:%d h:%d"), Extent.width, Extent.height);
	}

	static FString GetOffsetString(const VkOffset3D& Offset)
	{
		return FString::Printf(TEXT("x:%d y:%d z:%d"), Offset.x, Offset.y, Offset.z);
	}

	static FString GetOffsetString(const VkOffset2D& Offset)
	{
		return FString::Printf(TEXT("x:%d y:%d"), Offset.x, Offset.y);
	}

	static FString GetRectString(const VkRect2D& Rect)
	{
		return FString::Printf(TEXT("%s %s"), *GetOffsetString(Rect.offset), *GetExtentString(Rect.extent));
	}

	FString GetBufferImageCopyString(const VkBufferImageCopy& Region)
	{
		return FString::Printf(TEXT("BufOff=%d BufRow=%d BufImgHeight=%d ImgSubR=[%s] ImgOff=[%s] ImgExt=[%s]"),
			Region.bufferOffset, Region.bufferRowLength, Region.bufferImageHeight, *GetImageSubresourceLayers(Region.imageSubresource),
			*GetOffsetString(Region.imageOffset), *GetExtentString(Region.imageExtent));
	}

	static FString GetImageSubResourceRangeString(const VkImageSubresourceRange& Range)
	{
		return FString::Printf(TEXT("AspectMask=%s, BaseMip=%d, NumLevels=%d, BaseArrayLayer=%d, NumLayers=%d"), *GetAspectMaskString(Range.aspectMask), Range.baseMipLevel, Range.levelCount, Range.baseArrayLayer, Range.layerCount);		
	}

	static FString GetStageMaskString(VkPipelineStageFlags Flags)
	{
		return FString::Printf(TEXT("VkPipelineStageFlags=0x%x"), (uint32)Flags);
	}

	static FString GetClearColorValueString(const VkClearColorValue& Value)
	{
		return FString::Printf(TEXT("0x%x(%f), 0x%x(%f), 0x%x(%f), 0x%x(%f)"),
			Value.uint32[0], Value.float32[0],
			Value.uint32[1], Value.float32[1],
			Value.uint32[2], Value.float32[2],
			Value.uint32[3], Value.float32[3]);
	}

	static FString GetClearDepthStencilValueString(const VkClearDepthStencilValue& Value)
	{
		return FString::Printf(TEXT("d:%f s:%d"), Value.depth, Value.stencil);
	}

	static FString GetClearValueString(const VkClearValue& Value)
	{
		return FString::Printf(TEXT("(%s/%s)"), *GetClearColorValueString(Value.color), *GetClearDepthStencilValueString(Value.depthStencil));
	}

	void PrintfBeginResult(const FString& String)
	{
		DebugLog += FString::Printf(TEXT("[GLOBAL METHOD]     %8d: %s"), DebugLine++, *String);
		FlushDebugWrapperLog();
	}

	void PrintfBegin(const FString& String)
	{
		DebugLog += FString::Printf(TEXT("[GLOBAL METHOD]     %8d: %s\n"), DebugLine++, *String);
		FlushDebugWrapperLog();
	}

	void DevicePrintfBeginResult(VkDevice Device, const FString& String)
	{
		DebugLog += FString::Printf(TEXT("[D:0x%p]%8d: %s"), Device, DebugLine++, *String);
		FlushDebugWrapperLog();
	}

	void DevicePrintfBegin(VkDevice Device, const FString& String)
	{
		DebugLog += FString::Printf(TEXT("[D:0x%p]%8d: %s\n"), Device, DebugLine++, *String);
		FlushDebugWrapperLog();
	}

	void CmdPrintfBegin(VkCommandBuffer CmdBuffer, const FString& String)
	{
		DebugLog += FString::Printf(TEXT("[C:0x%p]%8d: %s\n"), CmdBuffer, DebugLine++, *String);
		FlushDebugWrapperLog();
	}

	void CmdPrintfBeginResult(VkCommandBuffer CmdBuffer, const FString& String)
	{
		DebugLog += FString::Printf(TEXT("[C:0x%p]%8d: %s"), CmdBuffer, DebugLine++, *String);
		FlushDebugWrapperLog();
	}

	void PrintResult(VkResult Result)
	{
		DebugLog += FString::Printf(TEXT(" -> %s\n"), *GetVkResultErrorString(Result));
		FlushDebugWrapperLog();
	}

	void PrintResultAndPointer(VkResult Result, void* Handle)
	{
		DebugLog += FString::Printf(TEXT(" -> %s => 0x%p\n"), *GetVkResultErrorString(Result), Handle);
		FlushDebugWrapperLog();
	}

	void PrintResultAndNamedHandle(VkResult Result, const TCHAR* HandleName, void* Handle)
	{
		DebugLog += FString::Printf(TEXT(" -> %s => %s=0x%p\n"), *GetVkResultErrorString(Result), HandleName, Handle);
		FlushDebugWrapperLog();
	}

	void PrintResultAndNamedHandles(VkResult Result, const TCHAR* HandleName, uint32 NumHandles, uint64* Handles)
	{
		DebugLog += FString::Printf(TEXT(" -> %s => %s\n"), *GetVkResultErrorString(Result), HandleName);
		for (uint32 Index = 0; Index < NumHandles; ++Index)
		{
			DebugLog += FString::Printf(TEXT(" [%d]=0x%p"), Index, (void*)Handles[Index]);
		}
		DebugLog += TEXT("\n");
		FlushDebugWrapperLog();
	}

	void PrintResultAndPointer(VkResult Result, uint64 Handle)
	{
		DebugLog += FString::Printf(TEXT(" -> %s => %ull\n"), *GetVkResultErrorString(Result), Handle);
		FlushDebugWrapperLog();
	}

	void PrintResultAndNamedHandle(VkResult Result, const TCHAR* HandleName, uint64 Handle)
	{
		DebugLog += FString::Printf(TEXT(" -> %s => %s=%ull\n"), *GetVkResultErrorString(Result), HandleName, Handle);
		FlushDebugWrapperLog();
	}
}
#endif

#if VULKAN_ENABLE_CUSTOM_LAYER
void FWrapLayer::GetPhysicalDeviceMemoryProperties(VkResult Result, VkPhysicalDevice PhysicalDevice, VkPhysicalDeviceMemoryProperties* Properties)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
			PrintfBegin(FString::Printf(TEXT("vkGetPhysicalDeviceMemoryProperties(OutProp=0x%p)[...]"), Properties));
			FlushDebugWrapperLog();
#endif
	}
}

void FWrapLayer::QueueWaitIdle(VkResult Result, VkQueue Queue)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintfBeginResult(FString::Printf(TEXT("vkQueueWaitIdle(Queue=0x%p)"), Queue));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResult(Result);
#endif
	}
}

void FWrapLayer::AllocateMemory(VkResult Result, VkDevice Device, const VkMemoryAllocateInfo* AllocateInfo, VkDeviceMemory* Memory)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkAllocateMemory(AllocateInfo=0x%p, OutMem=0x%p): Size=%d, MemTypeIndex=%d"), AllocateInfo, Memory, (uint32)AllocateInfo->allocationSize, AllocateInfo->memoryTypeIndex));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResultAndNamedHandle(Result, TEXT("DevMem"), *Memory);
		FlushDebugWrapperLog();
#endif
	}
}

#if VULKAN_ENABLE_DUMP_LAYER
static void DumpMemoryRequirements(VkMemoryRequirements* MemoryRequirements)
{
	DebugLog += FString::Printf(TEXT(" -> Size=%d Align=%d MemTypeBits=0x%x\n"), (uint32)MemoryRequirements->size, (uint32)MemoryRequirements->alignment, MemoryRequirements->memoryTypeBits);
	FlushDebugWrapperLog();
}
#endif

void FWrapLayer::GetBufferMemoryRequirements(VkResult Result, VkDevice Device, VkBuffer Buffer, VkMemoryRequirements* MemoryRequirements)
{
#if VULKAN_ENABLE_DUMP_LAYER
	if (Result == VK_RESULT_MAX_ENUM)
	{
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkGetBufferMemoryRequirements(Buffer=0x%p, OutReq=0x%p)"), Buffer, MemoryRequirements));
	}
	else
	{
		DumpMemoryRequirements(MemoryRequirements);
	}
#endif
}

void FWrapLayer::GetImageMemoryRequirements(VkResult Result, VkDevice Device, VkImage Image, VkMemoryRequirements* MemoryRequirements)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkGetImageMemoryRequirements(Image=0x%p, OutReq=0x%p)"), Image, MemoryRequirements));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DumpMemoryRequirements(MemoryRequirements);
#endif
	}
}


void FWrapLayer::CreateBuffer(VkResult Result, VkDevice Device, const VkBufferCreateInfo* CreateInfo, VkBuffer* Buffer)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkCreateBuffer(Info=0x%p, OutBuffer=0x%p)[...]"), CreateInfo, Buffer));
		DebugLog += FString::Printf(TEXT("%sVkBufferCreateInfo: Flags=%d, Size=%d, Usage=%d"), Tabs, CreateInfo->flags, (uint32)CreateInfo->size, (uint32)CreateInfo->usage);
		FlushDebugWrapperLog();
	/*
	VkSharingMode          sharingMode;
	uint32_t               queueFamilyIndexCount;
	const uint32_t*        pQueueFamilyIndices;
	*/
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResultAndNamedHandle(Result, TEXT("Buffer"), *Buffer);
#endif
#if VULKAN_ENABLE_BUFFER_TRACKING_LAYER
		FScopeLock ScopeLock(&GTrackingCS);
		if (Buffer && CreateInfo)
		{
			auto& TrackingBuffer = GVulkanTrackingBuffers.FindOrAdd(*Buffer);
			TrackingBuffer.CreateInfo = *CreateInfo;

#if VULKAN_ENABLE_TRACKING_CALLSTACK
			CaptureCallStack(TrackingBuffer.CreateCallstack, 3);
#endif
		}
#endif
	}
}


void FWrapLayer::CreateBufferView(VkResult Result, VkDevice Device, const VkBufferViewCreateInfo* CreateInfo, VkBufferView* BufferView)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkCreateBufferView(Info=0x%p, OutBufferView=0x%p)\n"), CreateInfo, BufferView));
		DebugLog += FString::Printf(TEXT("%sVkBufferCreateInfo: Flags=%d, Buffer=0x%p, Format=%s, Offset=%d, Range=%d\n"), Tabs, CreateInfo->flags, CreateInfo->buffer,
					*GetVkFormatString(CreateInfo->format), CreateInfo->offset, CreateInfo->range);
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		TrackBufferViewAdd(*BufferView, CreateInfo);
		PrintResultAndNamedHandle(Result, TEXT("BufferView"), *BufferView);
		FlushDebugWrapperLog();
#endif
#if VULKAN_ENABLE_BUFFER_TRACKING_LAYER
		FScopeLock ScopeLock(&GTrackingCS);
		if (BufferView && CreateInfo)
		{
			auto& TrackingBuffer = GVulkanTrackingBufferViews.FindOrAdd(*BufferView);
			TrackingBuffer.CreateInfo = *CreateInfo;
#if VULKAN_ENABLE_TRACKING_CALLSTACK
			CaptureCallStack(TrackingBuffer.CreateCallstack, 3);
#endif
			GVulkanTrackingBufferToBufferViews.FindOrAdd(CreateInfo->buffer).Add(*BufferView);
		}
#endif
	}
}

void FWrapLayer::CreateImage(VkResult Result, VkDevice Device, const VkImageCreateInfo* CreateInfo, VkImage* Image)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkCreateImage(Info=0x%p, OutImage=0x%p)"), CreateInfo, Image));
		DebugLog += FString::Printf(TEXT("%sVkImageCreateInfo: Flags=%d, ImageType=%s, Format=%s, MipLevels=%d, ArrayLayers=%d, Samples=%s\n"), Tabs, CreateInfo->flags, *GetImageTypeString(CreateInfo->imageType),
			*GetVkFormatString(CreateInfo->format), CreateInfo->mipLevels, CreateInfo->arrayLayers, *GetSampleCountString(CreateInfo->samples));
		DebugLog += FString::Printf(TEXT("%s\tExtent=(%s) Tiling=%s, Usage=%s, Initial=%s\n"), Tabs, *GetExtentString(CreateInfo->extent),
			*GetImageTilingString(CreateInfo->tiling), *GetImageUsageString(CreateInfo->usage), *GetImageLayoutString(CreateInfo->initialLayout));
		FlushDebugWrapperLog();
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResultAndNamedHandle(Result, TEXT("Image"), *Image);
		FImageLayout& Found = GOLDImageLayoutTracker.FindOrAdd(*Image);
		Found.Init(FMath::Max(CreateInfo->arrayLayers, CreateInfo->extent.depth), CreateInfo->mipLevels);
		FlushDebugWrapperLog();
#endif

#if VULKAN_ENABLE_IMAGE_TRACKING_LAYER
		{
			FScopeLock ScopeLock(&GTrackingCS);
			if (Image && CreateInfo)
			{
				FTrackingImage& TrackingImage = GVulkanTrackingImageLayouts.FindOrAdd(*Image);
				TrackingImage.Setup(*CreateInfo, false);

#if VULKAN_ENABLE_TRACKING_CALLSTACK
				CaptureCallStack(TrackingImage.CreateCallstack, 3);
#endif
			}
		}
#endif
	}
}

void FWrapLayer::DestroyImage(VkResult Result, VkDevice Device, VkImage Image)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkDestroyImage(Image=0x%p)"), Image));
		GOLDImageLayoutTracker.Remove(Image);
#endif
	}
	else
	{
#if VULKAN_ENABLE_IMAGE_TRACKING_LAYER
		{
			FScopeLock ScopeLock(&GTrackingCS);
			int32 NumRemoved = GVulkanTrackingImageLayouts.Remove(Image);
			ensure(NumRemoved > 0);
		}
#endif
	}
}

void FWrapLayer::CreateImageView(VkResult Result, VkDevice Device, const VkImageViewCreateInfo* CreateInfo, VkImageView* ImageView)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkCreateImageView(Info=0x%p, OutImageView=0x%p)"), CreateInfo, ImageView));
		DebugLog += FString::Printf(TEXT("%sVkImageViewCreateInfo: Flags=%d, Image=0x%p, ViewType=%s, Format=%s, Components=%s\n"), Tabs, CreateInfo->flags, CreateInfo->image,
			*GetImageViewTypeString(CreateInfo->viewType), *GetVkFormatString(CreateInfo->format), *GetComponentMappingString(CreateInfo->components));
		DebugLog += FString::Printf(TEXT("%s\tSubresourceRange=(%s)"), Tabs, *GetImageSubResourceRangeString(CreateInfo->subresourceRange));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		TrackImageViewAdd(*ImageView, CreateInfo);
		PrintResultAndNamedHandle(Result, TEXT("ImageView"), *ImageView);
		FlushDebugWrapperLog();
#endif
#if VULKAN_ENABLE_IMAGE_TRACKING_LAYER
		{
			FScopeLock ScopeLock(&GTrackingCS);
			GVulkanTrackingImageViews.FindOrAdd(*ImageView).CreateInfo = *CreateInfo;
		}
#endif
	}
}


void FWrapLayer::CreateFence(VkResult Result, VkDevice Device, const VkFenceCreateInfo* CreateInfo, VkFence* Fence)
{
#if VULKAN_ENABLE_DUMP_LAYER
	if (Result == VK_RESULT_MAX_ENUM)
	{
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkCreateFence(CreateInfo=0x%p%s, OutFence=0x%p)"),
			CreateInfo, (CreateInfo->flags == VK_FENCE_CREATE_SIGNALED_BIT) ? TEXT("(SIGNALED)") : TEXT(""), Fence));
	}
	else
	{
		PrintResultAndNamedHandle(Result, TEXT("Fence"), *Fence);
	}
	FlushDebugWrapperLog();
#endif
}

#if VULKAN_ENABLE_DUMP_LAYER
static void DumpFenceList(uint32 FenceCount, const VkFence* Fences)
{
	for (uint32 Index = 0; Index < FenceCount; ++Index)
	{
		DebugLog += Tabs;
		DebugLog += '\t';
		DebugLog += FString::Printf(TEXT("Fence[%d]=0x%p"), Index, Fences[Index]);
		if (Index < FenceCount - 1)
		{
			DebugLog += TEXT("\n");
		}
	}
	FlushDebugWrapperLog();
}
#endif

void FWrapLayer::ResetFences(VkResult Result, VkDevice Device, uint32 FenceCount, const VkFence* Fences)
{
#if VULKAN_ENABLE_DUMP_LAYER
	if (Result == VK_RESULT_MAX_ENUM)
	{
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkResetFences(Count=%d, Fences=0x%p)"), FenceCount, Fences));
		DumpFenceList(FenceCount, Fences);
	}
	else
	{
		PrintResult(Result);
	}
#endif
}

void FWrapLayer::WaitForFences(VkResult Result, VkDevice Device, uint32 FenceCount, const VkFence* Fences, VkBool32 bWaitAll, uint64_t Timeout)
{
#if VULKAN_ENABLE_DUMP_LAYER
	if (Result == VK_RESULT_MAX_ENUM)
	{
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkWaitForFences(Count=0x%p, Fences=%d, WaitAll=%d, Timeout=0x%p)"), FenceCount, Fences, (uint32)bWaitAll, Timeout));
		DumpFenceList(FenceCount, Fences);
	}
	else
	{
		PrintResult(Result);
	}
#endif
}

void FWrapLayer::CreateSemaphore(VkResult Result, VkDevice Device, const VkSemaphoreCreateInfo* CreateInfo, VkSemaphore* Semaphore)
{
#if VULKAN_ENABLE_DUMP_LAYER
	if (Result == VK_RESULT_MAX_ENUM)
	{
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkCreateSemaphore(CreateInfo=0x%p, OutSemaphore=0x%p)"), CreateInfo, Semaphore));
	}
	else
	{
		PrintResultAndNamedHandle(Result, TEXT("Semaphore"), *Semaphore);
	}
	FlushDebugWrapperLog();
#endif
}


#if VULKAN_ENABLE_DUMP_LAYER
static void DumpMappedMemoryRanges(VkResult Result, uint32 MemoryRangeCount, const VkMappedMemoryRange* MemoryRanges)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
		for (uint32 Index = 0; Index < MemoryRangeCount; ++Index)
		{
			const VkMappedMemoryRange& Range = MemoryRanges[Index];
/*
			typedef struct VkMappedMemoryRange {
				VkStructureType    sType;
				const void*        pNext;
				VkDeviceMemory     memory;
				VkDeviceSize       offset;
				VkDeviceSize       size;
			} VkMappedMemoryRange;
*/
			DebugLog += FString::Printf(TEXT("%s%d Memory=0x%p Offset=%d Size=%d\n"), Tabs, Index,
				(void*)Range.memory, (uint64)Range.offset, (uint64)Range.size);
		}
		FlushDebugWrapperLog();
	}
}
#endif

void FWrapLayer::InvalidateMappedMemoryRanges(VkResult Result, VkDevice Device, uint32 MemoryRangeCount, const VkMappedMemoryRange* MemoryRanges)
{
#if VULKAN_ENABLE_DUMP_LAYER
	if (Result == VK_RESULT_MAX_ENUM)
	{
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkInvalidateMappedMemoryRanges(Count=%d, Ranges=0x%p)"), MemoryRangeCount, MemoryRanges));
		DumpMappedMemoryRanges(Result, MemoryRangeCount, MemoryRanges);
	}
	else
	{
		PrintResult(Result);
	}
#endif
}

void FWrapLayer::FlushMappedMemoryRanges(VkResult Result, VkDevice Device, uint32 MemoryRangeCount, const VkMappedMemoryRange* MemoryRanges)
{
#if VULKAN_ENABLE_DUMP_LAYER
	if (Result == VK_RESULT_MAX_ENUM)
	{
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkFlushMappedMemoryRanges(Count=%d, Ranges=0x%p)"), MemoryRangeCount, MemoryRanges));
		DumpMappedMemoryRanges(Result, MemoryRangeCount, MemoryRanges);
	}
	else
	{
		PrintResult(Result);
	}
#endif
}


void FWrapLayer::ResolveImage(VkResult Result, VkCommandBuffer CommandBuffer, VkImage SrcImage, VkImageLayout SrcImageLayout, VkImage DstImage, VkImageLayout DstImageLayout, uint32 RegionCount, const VkImageResolve* Regions)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdResolveImage(SrcImage=0x%p, SrcImageLayout=%s, DestImage=0x%p, DestImageLayout=%s, NumRegions=%d, Regions=0x%p)[...]"),
			CommandBuffer, SrcImage, *GetImageLayoutString(SrcImageLayout), DstImage, *GetImageLayoutString(DstImageLayout), RegionCount, Regions));
		for (uint32 Index = 0; Index < RegionCount; ++Index)
		{
			DebugLog += Tabs;
			DebugLog += FString::Printf(TEXT("Region %d: "), Index);
			/*
						typedef struct VkImageResolve {
							VkImageSubresourceLayers    srcSubresource;
							VkOffset3D                  srcOffset;
							VkImageSubresourceLayers    dstSubresource;
							VkOffset3D                  dstOffset;
							VkExtent3D                  extent;

			*/
		}
		FlushDebugWrapperLog();
#endif

	}
}

void FWrapLayer::FreeDescriptorSets(VkResult Result, VkDevice Device, VkDescriptorPool DescriptorPool, uint32 DescriptorSetCount, const VkDescriptorSet* DescriptorSets)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
			DevicePrintfBegin(Device, FString::Printf(TEXT("vkFreeDescriptorSets(Pool=0x%p, NumSets=%d, Sets=0x%p)"), DescriptorPool, DescriptorSetCount, DescriptorSets));
			for (uint32 Index = 0; Index < DescriptorSetCount; ++Index)
			{
				DebugLog += Tabs;
				DebugLog += FString::Printf(TEXT("Set %d: 0x%p\n"), Index, DescriptorSets[Index]);
			}
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResult(Result);
#endif
	}
}

void FWrapLayer::CreateInstance(VkResult Result, const VkInstanceCreateInfo* CreateInfo, VkInstance* Instance)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintfBegin(FString::Printf(TEXT("vkCreateInstance(Info=0x%p, OutInstance=0x%p)[...]"), CreateInfo, Instance));
		/*
				typedef struct VkInstanceCreateInfo {
					VkInstanceCreateFlags       flags;
					const VkApplicationInfo*    pApplicationInfo;
					uint32_t                    enabledLayerCount;
					const char* const*          ppEnabledLayerNames;
					uint32_t                    enabledExtensionCount;
					const char* const*          ppEnabledExtensionNames;
				} VkInstanceCreateInfo;
		*/
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResultAndNamedHandle(Result, TEXT("Instance"), *Instance);
		FlushDebugWrapperLog();
#endif
	}
}

void FWrapLayer::EnumeratePhysicalDevices(VkResult Result, VkInstance Instance, uint32* PhysicalDeviceCount, VkPhysicalDevice* PhysicalDevices)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintfBegin(FString::Printf(TEXT("vkEnumeratePhysicalDevices(Instance=0x%p, Count=0x%p, Devices=0x%p)"), Instance, PhysicalDeviceCount, PhysicalDevices));
		if (PhysicalDeviceCount)
		{
			DebugLog += Tabs;
			DebugLog += FString::Printf(TEXT("OutCount=%d\n"), *PhysicalDeviceCount);
			if (PhysicalDevices)
			{
				for (uint32 Index = 0; Index < *PhysicalDeviceCount; ++Index)
				{
					DebugLog += Tabs;
					DebugLog += FString::Printf(TEXT("\tOutDevice[%d]=0x%p\n"), Index, PhysicalDevices[Index]);
				}
			}
			FlushDebugWrapperLog();
		}
#endif
	}
}

#if VULKAN_ENABLE_DUMP_LAYER
static void DumpImageMemoryBarriers(uint32 ImageMemoryBarrierCount, const VkImageMemoryBarrier* ImageMemoryBarriers)
{
	if (ImageMemoryBarrierCount)
	{
		for (uint32 Index = 0; Index < ImageMemoryBarrierCount; ++Index)
		{
			DebugLog += FString::Printf(TEXT("%s\tImageBarrier[%d]: srcAccess=%s, oldLayout=%s, srcQueueFamilyIndex=%d\n"), Tabs, Index, *GetAccessFlagString(ImageMemoryBarriers[Index].srcAccessMask), *GetImageLayoutString(ImageMemoryBarriers[Index].oldLayout), ImageMemoryBarriers[Index].srcQueueFamilyIndex);
			DebugLog += FString::Printf(TEXT("%s\t\tdstAccess=%s, newLayout=%s, dstQueueFamilyIndex=%d\n"), Tabs, *GetAccessFlagString(ImageMemoryBarriers[Index].dstAccessMask), *GetImageLayoutString(ImageMemoryBarriers[Index].newLayout), ImageMemoryBarriers[Index].dstQueueFamilyIndex);
			DebugLog += FString::Printf(TEXT("%s\t\tImage=0x%p, subresourceRange=(%s)\n"), Tabs, ImageMemoryBarriers[Index].image, *GetImageSubResourceRangeString(ImageMemoryBarriers[Index].subresourceRange));
		}
	}
}
#endif

void FWrapLayer::PipelineBarrier(VkResult Result, VkCommandBuffer CommandBuffer, VkPipelineStageFlags SrcStageMask, VkPipelineStageFlags DstStageMask, VkDependencyFlags DependencyFlags, uint32 MemoryBarrierCount, const VkMemoryBarrier* MemoryBarriers, uint32 BufferMemoryBarrierCount, const VkBufferMemoryBarrier* BufferMemoryBarriers, uint32 ImageMemoryBarrierCount, const VkImageMemoryBarrier* ImageMemoryBarriers)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdPipelineBarrier(SrcMask=%s, DestMask=%s, Flags=%d, NumMemB=%d, MemB=0x%p,"), *GetStageMaskString(SrcStageMask), *GetStageMaskString(DstStageMask), (uint32)DependencyFlags, MemoryBarrierCount, MemoryBarriers));
		DebugLog += FString::Printf(TEXT("%s\tNumBufferB=%d, BufferB=0x%p, NumImageB=%d, ImageB=0x%p)[...]\n"), Tabs, BufferMemoryBarrierCount, BufferMemoryBarriers, ImageMemoryBarrierCount, ImageMemoryBarriers);
		DumpImageMemoryBarriers(ImageMemoryBarrierCount, ImageMemoryBarriers);
		FlushDebugWrapperLog();
#endif

#if VULKAN_ENABLE_IMAGE_TRACKING_LAYER
		{
			FScopeLock ScopeLock(&GTrackingCS);
			for (uint32 Index = 0; Index < ImageMemoryBarrierCount; ++Index)
			{
				FTrackingImage* TrackingImage = GVulkanTrackingImageLayouts.Find(ImageMemoryBarriers[Index].image);
				check(TrackingImage);
#if VULKAN_ENABLE_TRACKING_CALLSTACK
				FTrackingImage::FHistoryEntry* HistoryEntry = new (TrackingImage->History) FTrackingImage::FHistoryEntry;
				HistoryEntry->ArrayLayouts = TrackingImage->ArrayLayouts;
#endif
				const VkImageSubresourceRange& Range = ImageMemoryBarriers[Index].subresourceRange;
				uint32 NumLayers = (Range.layerCount == VK_REMAINING_ARRAY_LAYERS) ? (TrackingImage->Info.CreateInfo.arrayLayers - Range.baseArrayLayer) : Range.layerCount;
				for (uint32 LIndex = Range.baseArrayLayer; LIndex < Range.baseArrayLayer + NumLayers; ++LIndex)
				{
					TArray<VkImageLayout>& MipLayouts = TrackingImage->ArrayLayouts[LIndex];
					uint32 NumLevels = (Range.levelCount == VK_REMAINING_MIP_LEVELS ) ? (TrackingImage->Info.CreateInfo.mipLevels - Range.baseMipLevel) : Range.levelCount;
					for (uint32 MIndex = Range.baseMipLevel; MIndex < Range.baseMipLevel + NumLevels; ++MIndex)
					{
						if (ImageMemoryBarriers[Index].oldLayout != VK_IMAGE_LAYOUT_UNDEFINED && MipLayouts[MIndex] != ImageMemoryBarriers[Index].oldLayout)
						{
							ensure(0);
						}
						MipLayouts[MIndex] = ImageMemoryBarriers[Index].newLayout;
					}
				}
#if VULKAN_ENABLE_TRACKING_CALLSTACK
				CaptureCallStack(HistoryEntry->Callstack, 2);
#endif
			}
		}
#endif
	}
}

void FWrapLayer::WaitEvents(VkResult Result, VkCommandBuffer CommandBuffer, uint32 EventCount, const VkEvent* Events, VkPipelineStageFlags SrcStageMask, VkPipelineStageFlags DstStageMask, uint32 MemoryBarrierCount,
		const VkMemoryBarrier* MemoryBarriers, uint32 BufferMemoryBarrierCount, const VkBufferMemoryBarrier* BufferMemoryBarriers, uint32 ImageMemoryBarrierCount, const VkImageMemoryBarrier* ImageMemoryBarriers)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdWaitEvents(NumEvents=%d, Events=0x%p, SrcMask=%s, DestMask=%s, NumMemB=%d, MemB=0x%p,"), EventCount, Events, *GetStageMaskString(SrcStageMask), *GetStageMaskString(DstStageMask), MemoryBarrierCount, MemoryBarriers));
		DebugLog += FString::Printf(TEXT("%s\tNumBufferB=%d, BufferB=0x%p, NumImageB=%d, ImageB=0x%p)[...]\n"), Tabs, BufferMemoryBarrierCount, BufferMemoryBarriers, ImageMemoryBarrierCount, ImageMemoryBarriers);
		for (uint32 Index = 0; Index < EventCount; ++Index)
		{
			DebugLog += FString::Printf(TEXT("%s\tEvents[%d]=0x%p)\n"), Tabs, Index, Events[Index]);
		}
		DumpImageMemoryBarriers(ImageMemoryBarrierCount, ImageMemoryBarriers);
		FlushDebugWrapperLog();
#endif
	}
}

void FWrapLayer::BindDescriptorSets(VkResult Result, VkCommandBuffer CommandBuffer, VkPipelineBindPoint PipelineBindPoint, VkPipelineLayout Layout, uint32 FirstSet, uint32 DescriptorSetCount, const VkDescriptorSet* DescriptorSets, uint32 DynamicOffsetCount, const uint32* DynamicOffsets)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdBindDescriptorSets(BindPoint=%s, Layout=0x%p, FirstSet=%d, NumDS=%d, DS=0x%p, NumDynamicOffset=%d, DynamicOffsets=0x%p)"), *GetPipelineBindPointString(PipelineBindPoint), Layout, FirstSet, DescriptorSetCount, DescriptorSets, DynamicOffsetCount, DynamicOffsets));
		for (uint32 Index = 0; Index < DescriptorSetCount; ++Index)
		{
			DebugLog += FString::Printf(TEXT("%s\tDS[%d]=0x%p\n"), Tabs, Index, DescriptorSets[Index]);
		}
		for (uint32 Index = 0; Index < DynamicOffsetCount; ++Index)
		{
			DebugLog += FString::Printf(TEXT("%s\tDynamicOffset[%d]=%d (0x%x)\n"), Tabs, Index, DynamicOffsets[Index], DynamicOffsets[Index]);
		}
		FlushDebugWrapperLog();
#endif
	}
}

void FWrapLayer::CreateDescriptorSetLayout(VkResult Result, VkDevice Device, const VkDescriptorSetLayoutCreateInfo* CreateInfo, VkDescriptorSetLayout* SetLayout)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkCreateDescriptorSetLayout(Info=0x%p, OutLayout=0x%p)[...]"), CreateInfo, SetLayout));
		DebugLog += FString::Printf(TEXT("%sNumBindings=%d, Bindings=0x%p\n"), Tabs, CreateInfo->bindingCount, CreateInfo->pBindings);
		for (uint32 Index = 0; Index < CreateInfo->bindingCount; ++Index)
		{
			DebugLog += FString::Printf(TEXT("%s\tBinding[%d]= binding=%d DescType=%s NumDesc=%d StageFlags=%x\n"), Tabs, Index,
				CreateInfo->pBindings[Index].binding, *GetDescriptorTypeString(CreateInfo->pBindings[Index].descriptorType), CreateInfo->pBindings[Index].descriptorCount, (uint32)CreateInfo->pBindings[Index].stageFlags);
		}
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResultAndNamedHandle(Result, TEXT("DescriptorSetLayout"), *SetLayout);
#endif
	}
}

void FWrapLayer::AllocateDescriptorSets(VkResult Result, VkDevice Device, const VkDescriptorSetAllocateInfo* AllocateInfo, VkDescriptorSet* DescriptorSets)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkAllocateDescriptorSets(Info=0x%p, OutSets=0x%p)"), AllocateInfo, DescriptorSets));
		DebugLog += FString::Printf(TEXT("%s\tVkDescriptorSetAllocateInfo: Pool=0x%p, NumSetLayouts=%d:"), Tabs, AllocateInfo->descriptorPool, AllocateInfo->descriptorSetCount);
		for (uint32 Index = 0; Index < AllocateInfo->descriptorSetCount; ++Index)
		{
			DebugLog += FString::Printf(TEXT(" [%d]=0x%p"), Index, (void*)AllocateInfo->pSetLayouts[Index]);
			FlushDebugWrapperLog();
		}
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResultAndNamedHandles(Result, TEXT("DescriptorSet"), AllocateInfo->descriptorSetCount, (uint64*)DescriptorSets);
#endif
	}
}

void FWrapLayer::UpdateDescriptorSets(VkResult Result, VkDevice Device, uint32 DescriptorWriteCount, const VkWriteDescriptorSet* DescriptorWrites, uint32 DescriptorCopyCount, const VkCopyDescriptorSet* DescriptorCopies)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkUpdateDescriptorSets(NumWrites=%d, Writes=0x%p, NumCopies=%d, Copies=0x%p)"), DescriptorWriteCount, DescriptorWrites, DescriptorCopyCount, DescriptorCopies));
		for (uint32 Index = 0; Index < DescriptorWriteCount; ++Index)
		{
			DebugLog += FString::Printf(TEXT("%sWrite[%d]: Set=0x%p Binding=%d DstArrayElem=%d NumDesc=%d DescType=%s "), Tabs, Index,
				DescriptorWrites[Index].dstSet, DescriptorWrites[Index].dstBinding, DescriptorWrites[Index].dstArrayElement, DescriptorWrites[Index].descriptorCount, *GetDescriptorTypeString(DescriptorWrites[Index].descriptorType));

			switch (DescriptorWrites[Index].descriptorType)
			{
			case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
			case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
			case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
			case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
				DebugLog += FString::Printf(TEXT("pBufferInfo=0x%p\n"), DescriptorWrites[Index].pBufferInfo);
				if (DescriptorWrites[Index].pBufferInfo)
				{
					for (uint32 SubIndex = 0; SubIndex < DescriptorWrites[Index].descriptorCount; ++SubIndex)
					{
						DebugLog += FString::Printf(TEXT("%s\tpBufferInfo[%d]: buffer=0x%p, offset=%d, range=%d\n"), Tabs, SubIndex,
							DescriptorWrites[Index].pBufferInfo->buffer, (int32)DescriptorWrites[Index].pBufferInfo->offset, (int32)DescriptorWrites[Index].pBufferInfo->range);
					}
				}
				else
				{
					ValidationFail();
				}
				break;

			case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
			case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
				DebugLog += FString::Printf(TEXT("pTexelBufferView=0x%p\n"), DescriptorWrites[Index].pTexelBufferView);
				if (DescriptorWrites[Index].pTexelBufferView)
				{
					for (uint32 SubIndex = 0; SubIndex < DescriptorWrites[Index].descriptorCount; ++SubIndex)
					{
						DebugLog += FString::Printf(TEXT("%s\tpTexelBufferView[%d]=0x%p(B:0x%p)\n"), Tabs, SubIndex, DescriptorWrites[Index].pTexelBufferView[SubIndex], GBufferViewTracker.FindChecked(DescriptorWrites[Index].pTexelBufferView[SubIndex]).buffer);
					}
				}
				else
				{
					ValidationFail();
				}
				break;

			case VK_DESCRIPTOR_TYPE_SAMPLER:
			case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
			default:
				DebugLog += FString::Printf(TEXT("pImageInfo=0x%p\n"), DescriptorWrites[Index].pImageInfo);
				if (DescriptorWrites[Index].pImageInfo)
				{
					for (uint32 SubIndex = 0; SubIndex < DescriptorWrites[Index].descriptorCount; ++SubIndex)
					{
						DebugLog += FString::Printf(TEXT("%s\tpImageInfo[%d]: Sampler=0x%p, ImageView=0x%p(I:0x%p), imageLayout=%s\n"), Tabs, SubIndex,
							DescriptorWrites[Index].pImageInfo->sampler, DescriptorWrites[Index].pImageInfo->imageView, GImageViewTracker.FindChecked(DescriptorWrites[Index].pImageInfo->imageView).image, *GetImageLayoutString(DescriptorWrites[Index].pImageInfo->imageLayout));
					}
				}
				else
				{
					ValidationFail();
				}
				break;
			}
		}
		FlushDebugWrapperLog();
#endif
	}
	/*
	typedef struct VkWriteDescriptorSet {
		VkDescriptorSet                  dstSet;
		uint32_t                         dstBinding;
		uint32_t                         dstArrayElement;
		uint32_t                         descriptorCount;
		VkDescriptorType                 descriptorType;
		const VkDescriptorImageInfo*     pImageInfo;
		const VkDescriptorBufferInfo*    pBufferInfo;
		const VkBufferView*              pTexelBufferView;
	} VkWriteDescriptorSet;
*/
}

void FWrapLayer::CreateFramebuffer(VkResult Result, VkDevice Device, const VkFramebufferCreateInfo* CreateInfo, VkFramebuffer* Framebuffer)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkCreateFramebuffer(Info=0x%p, OutFramebuffer=0x%p)"), CreateInfo, Framebuffer));
		DebugLog += FString::Printf(TEXT("%sVkFramebufferCreateInfo: Flags=%d, RenderPass=0x%p, NumAttachments=%d\n"), Tabs, CreateInfo->flags, CreateInfo->renderPass, CreateInfo->attachmentCount);
		for (uint32 Index = 0; Index < CreateInfo->attachmentCount; ++Index)
		{
			DebugLog += FString::Printf(TEXT("%s\tAttachment[%d]: ImageView=0x%p(I:0x%p)\n"), Tabs, Index, CreateInfo->pAttachments[Index], GImageViewTracker.FindChecked(CreateInfo->pAttachments[Index]).image);
		}
		DebugLog += FString::Printf(TEXT("%s\twidth=%d, height=%d, layers=%d\n"), Tabs, CreateInfo->width, CreateInfo->height, CreateInfo->layers);
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResultAndNamedHandle(Result, TEXT("Framebuffer"), *Framebuffer);
		if (Result == VK_SUCCESS)
		{
			FFBInfo Info;
			Info.Info = *CreateInfo;
			Info.Attachments.AddUninitialized(CreateInfo->attachmentCount);
			FMemory::Memcpy(&Info.Attachments[0], CreateInfo->pAttachments, CreateInfo->attachmentCount * sizeof(VkImageView));
			Info.Info.pAttachments = &Info.Attachments[0];
			GFramebufferInfo.Add(*Framebuffer, Info);
		}
#endif
	}
}

void FWrapLayer::CreateRenderPass(VkResult Result, VkDevice Device, const VkRenderPassCreateInfo* CreateInfo, VkRenderPass* RenderPass)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkCreateRenderPass(Info=0x%p, OutRenderPass=0x%p)[...]"), CreateInfo, RenderPass));
		DebugLog += FString::Printf(TEXT("%s\tVkRenderPassCreateInfo: NumAttachments=%d, Attachments=0x%p, NumSubPasses=%d, SubPasses=0x%p\n"), Tabs, CreateInfo->attachmentCount, CreateInfo->pAttachments, CreateInfo->subpassCount, CreateInfo->pSubpasses);
		for (uint32 Index = 0; Index < CreateInfo->attachmentCount; ++Index)
		{
			auto GetLoadOpString = [](VkAttachmentLoadOp Op) -> FString
				{
					switch (Op)
					{
					case VK_ATTACHMENT_LOAD_OP_LOAD: return TEXT("LOAD");
					case VK_ATTACHMENT_LOAD_OP_CLEAR: return TEXT("CLEAR");
					case VK_ATTACHMENT_LOAD_OP_DONT_CARE: return TEXT("DONT_CARE");
					default: return FString::Printf(TEXT("Invalid(%d)"), (uint32)Op);

					}
				};
			auto GetStoreOpString = [](VkAttachmentStoreOp Op) -> FString
				{
					switch(Op)
					{
					case VK_ATTACHMENT_STORE_OP_STORE: return TEXT("STORE");
					case VK_ATTACHMENT_STORE_OP_DONT_CARE: return TEXT("DONT_CARE");
					default: return FString::Printf(TEXT("Invalid(%d)"), (uint32)Op);

					}
				};

			const VkAttachmentDescription& Desc = CreateInfo->pAttachments[Index];
			DebugLog += FString::Printf(TEXT("%s\t\tAttachment[%d]: Flags=%s, Format=%s, Samples=%s, Load=%s, Store=%s\n"), Tabs, Index,
				(Desc.flags == VK_ATTACHMENT_DESCRIPTION_MAY_ALIAS_BIT ? TEXT("MAY_ALIAS") : TEXT("0")),
				*GetVkFormatString(Desc.format), *GetSampleCountString(Desc.samples), *GetLoadOpString(Desc.loadOp), *GetStoreOpString(Desc.storeOp));
			DebugLog += FString::Printf(TEXT("%s\t\t\tLoadStencil=%s, StoreStencil=%s, Initial=%s, Final=%s\n"), Tabs, 
				*GetLoadOpString(Desc.stencilLoadOp), *GetStoreOpString(Desc.stencilStoreOp), *VulkanRHI::GetImageLayoutString(Desc.initialLayout), *VulkanRHI::GetImageLayoutString(Desc.finalLayout));
		}

		for (uint32 Index = 0; Index < CreateInfo->subpassCount; ++Index)
		{
			const VkSubpassDescription& Desc = CreateInfo->pSubpasses[Index];
			DebugLog += FString::Printf(TEXT("%s\t\tSubpass[%d]: Flags=%d, Bind=%s, NumInputAttach=%d, InputAttach=0x%p, NumColorAttach=%d, ColorAttach=0x%p, DSAttch=0x%p\n"), Tabs, Index,
				Desc.flags,
				Desc.pipelineBindPoint == VK_PIPELINE_BIND_POINT_COMPUTE ? TEXT("Compute") : TEXT("Gfx"),
				Desc.inputAttachmentCount, Desc.pInputAttachments, Desc.colorAttachmentCount, Desc.pColorAttachments, Desc.pDepthStencilAttachment);
			for (uint32 SubIndex = 0; SubIndex < Desc.inputAttachmentCount; ++SubIndex)
			{
				DebugLog += FString::Printf(TEXT("%s\t\t\tInputAttach[%d]: Attach=%d, Layout=%s\n"), Tabs, Index,
					Desc.pInputAttachments[SubIndex].attachment, *GetImageLayoutString(Desc.pInputAttachments[SubIndex].layout));
			}
			for (uint32 SubIndex = 0; SubIndex < Desc.colorAttachmentCount; ++SubIndex)
			{
				DebugLog += FString::Printf(TEXT("%s\t\t\tColorAttach[%d]: Attach=%d, Layout=%s\n"), Tabs, Index,
					Desc.pColorAttachments[SubIndex].attachment, *GetImageLayoutString(Desc.pColorAttachments[SubIndex].layout));
			}
			if (Desc.pDepthStencilAttachment)
			{
				DebugLog += FString::Printf(TEXT("%s\t\t\tDSAttach: Attach=%d, Layout=%s\n"), Tabs, Desc.pDepthStencilAttachment->attachment, *GetImageLayoutString(Desc.pDepthStencilAttachment->layout));
			}
			/*
			typedef struct VkSubpassDescription {
				const VkAttachmentReference*    pResolveAttachments;
				uint32_t                        preserveAttachmentCount;
				const uint32_t*                 pPreserveAttachments;
			} VkSubpassDescription;*/
		}
/*
		typedef struct VkRenderPassCreateInfo {
			uint32_t                          dependencyCount;
			const VkSubpassDependency*        pDependencies;
		} VkRenderPassCreateInfo;
*/
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResultAndNamedHandle(Result, TEXT("RenderPass"), *RenderPass);
		if (Result == VK_SUCCESS)
		{
			FRenderPassInfo Info;
			Info.Info = *CreateInfo;
			Info.Info.pAttachments = nullptr;
			Info.Info.pSubpasses = nullptr;
			Info.Info.pDependencies = nullptr;
			Info.Descriptions.AddUninitialized(CreateInfo->attachmentCount);
			if (CreateInfo->attachmentCount)
			{
				FMemory::Memcpy(&Info.Descriptions[0], CreateInfo->pAttachments, CreateInfo->attachmentCount * sizeof(VkAttachmentDescription));
			}
			GRenderPassInfo.Add(*RenderPass, Info);
		}
		FlushDebugWrapperLog();
#endif
	}
}

void FWrapLayer::QueueSubmit(VkResult Result, VkQueue Queue, uint32 SubmitCount, const VkSubmitInfo* Submits, VkFence Fence)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintfBeginResult(FString::Printf(TEXT("vkQueueSubmit(Queue=0x%p, Count=%d, Submits=0x%p, Fence=0x%p)"), Queue, SubmitCount, Submits, Fence));
		for (uint32 Index = 0; Index < SubmitCount; ++Index)
		{
			DebugLog += FString::Printf(TEXT("\n%sSubmit[%d]:"), Tabs, Index);
			if (Submits[Index].waitSemaphoreCount > 0)
			{
				DebugLog += FString::Printf(TEXT("\n%s\tWaitSemaphores(Mask): "), Tabs, Index);
				for (uint32 SubIndex = 0; SubIndex < Submits[Index].waitSemaphoreCount; ++SubIndex)
				{
					DebugLog += FString::Printf(TEXT("0x%p(%d) "), Submits[Index].pWaitSemaphores[SubIndex], (int32)Submits[Index].pWaitDstStageMask[SubIndex]);
				}
			}
			if (Submits[Index].commandBufferCount > 0)
			{
				DebugLog += FString::Printf(TEXT("\n%s\tCommandBuffers: "), Tabs, Index);
				for (uint32 SubIndex = 0; SubIndex < Submits[Index].commandBufferCount; ++SubIndex)
				{
					DebugLog += FString::Printf(TEXT("0x%p "), Submits[Index].pCommandBuffers[SubIndex]);
				}
			}
			if (Submits[Index].signalSemaphoreCount > 0)
			{
				DebugLog += FString::Printf(TEXT("\n%s\tSignalSemaphore: "), Tabs, Index);
				for (uint32 SubIndex = 0; SubIndex < Submits[Index].signalSemaphoreCount; ++SubIndex)
				{
					DebugLog += FString::Printf(TEXT("0x%p "), Submits[Index].pSignalSemaphores[SubIndex]);
				}
			}
		}
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResult(Result);
#endif
	}
}

void FWrapLayer::CreateShaderModule(VkResult Result, VkDevice Device, const VkShaderModuleCreateInfo* CreateInfo, VkShaderModule* ShaderModule)
{
#if VULKAN_ENABLE_DUMP_LAYER
	if (Result == VK_RESULT_MAX_ENUM)
	{
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkCreateShaderModule(CreateInfo=0x%p, OutShaderModule=0x%p)[...]"), CreateInfo, ShaderModule));
		/*
			typedef struct VkShaderModuleCreateInfo {
				VkStructureType              sType;
				const void*                  pNext;
				VkShaderModuleCreateFlags    flags;
				size_t                       codeSize;
				const uint32_t*              pCode;
			} VkShaderModuleCreateInfo;
		*/
	}
	else
	{
		PrintResultAndNamedHandle(Result, TEXT("ShaderModule"), *ShaderModule);
	}
	FlushDebugWrapperLog();
#endif
}

void FWrapLayer::CreatePipelineCache(VkResult Result, VkDevice Device, const VkPipelineCacheCreateInfo* CreateInfo, VkPipelineCache* PipelineCache)
{
#if VULKAN_ENABLE_DUMP_LAYER
	if (Result == VK_RESULT_MAX_ENUM)
	{
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkCreatePipelineCache(CreateInfo=0x%p, OutPipelineCache=0x%p) InitialSize=%d Data=0x%p "), CreateInfo, PipelineCache, (uint32)CreateInfo->initialDataSize, CreateInfo->pInitialData));
	}
	else
	{
		PrintResultAndNamedHandle(Result, TEXT("PipelineCache"), *PipelineCache);
	}
	FlushDebugWrapperLog();
#endif
}

void FWrapLayer::CreateCommandPool(VkResult Result, VkDevice Device, const VkCommandPoolCreateInfo* CreateInfo, VkCommandPool* CommandPool)
{
#if VULKAN_ENABLE_DUMP_LAYER
	if (Result == VK_RESULT_MAX_ENUM)
	{
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkCreateCommandPool(CreateInfo=0x%p, OutCommandPool=0x%p)[...]"), CreateInfo, CommandPool));
	}
	else
	{
		PrintResultAndNamedHandle(Result, TEXT("CommandPool"), *CommandPool);
	}
	FlushDebugWrapperLog();
#endif
}

void FWrapLayer::CreateQueryPool(VkResult Result, VkDevice Device, const VkQueryPoolCreateInfo* CreateInfo, VkQueryPool* QueryPool)
{
#if VULKAN_ENABLE_DUMP_LAYER
	if (Result == VK_RESULT_MAX_ENUM)
	{
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkCreateQueryPool(CreateInfo=0x%p, OutQueryPool=0x%p)[...]"), CreateInfo, QueryPool));
	}
	else
	{
		PrintResultAndNamedHandle(Result, TEXT("QueryPool"), *QueryPool);
	}
	FlushDebugWrapperLog();
#endif
}

void FWrapLayer::CreatePipelineLayout(VkResult Result, VkDevice Device, const VkPipelineLayoutCreateInfo* CreateInfo, VkPipelineLayout* PipelineLayout)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkCreatePipelineLayout(CreateInfo=0x%p, OutPipelineLayout=0x%p) NumLayouts=%d"), CreateInfo, PipelineLayout, CreateInfo->setLayoutCount));
		DebugLog += FString::Printf(TEXT("%sLayouts: "), Tabs);
		for (uint32 Index = 0; Index < CreateInfo->setLayoutCount; ++Index)
		{
			DebugLog += FString::Printf(TEXT("%d=0x%p "), Index, CreateInfo->pSetLayouts[Index]);
		}
		DebugLog += '\n';
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResultAndNamedHandle(Result, TEXT("PipelineLayout"), *PipelineLayout);
#endif
	}
	/*
	typedef struct VkPipelineLayoutCreateInfo {
	VkPipelineLayoutCreateFlags     flags;
	uint32_t                        pushConstantRangeCount;
	const VkPushConstantRange*      pPushConstantRanges;
	} VkPipelineLayoutCreateInfo;
	*/
}

void FWrapLayer::CreateDescriptorPool(VkResult Result, VkDevice Device, const VkDescriptorPoolCreateInfo* CreateInfo, VkDescriptorPool* DescriptorPool)
{
#if VULKAN_ENABLE_DUMP_LAYER
	if (Result == VK_RESULT_MAX_ENUM)
	{
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkCreateDescriptorPool(CreateInfo=0x%p, OutDescriptorPool=0x%p)[...]"), CreateInfo, DescriptorPool));
	}
	else
	{
		PrintResultAndNamedHandle(Result, TEXT("DescriptorPool"), *DescriptorPool);
	}
	FlushDebugWrapperLog();
#endif
}


void FWrapLayer::CreateSampler(VkResult Result, VkDevice Device, const VkSamplerCreateInfo* CreateInfo, VkSampler* Sampler)
{
#if VULKAN_ENABLE_DUMP_LAYER
	if (Result == VK_RESULT_MAX_ENUM)
	{
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkCreateSampler(CreateInfo=0x%p, OutSampler=0x%p)[...]"), CreateInfo, Sampler));
	}
	else
	{
		PrintResultAndNamedHandle(Result, TEXT("Sampler"), *Sampler);
	}
	FlushDebugWrapperLog();
#endif
}


void FWrapLayer::CreateDevice(VkResult Result, VkPhysicalDevice PhysicalDevice, const VkDeviceCreateInfo* CreateInfo, VkDevice* Device)
{
#if VULKAN_ENABLE_DUMP_LAYER
	if (Result == VK_RESULT_MAX_ENUM)
	{
		PrintfBeginResult(FString::Printf(TEXT("vkCreateDevice(PhysicalDevice=0x%p, CreateInfo=0x%p, OutDevice=0x%p)[...]"), PhysicalDevice, CreateInfo, Device));
	}
	else
	{
		PrintResultAndNamedHandle(Result, TEXT("Device"), *Device);
	}
	FlushDebugWrapperLog();
#endif
}

void FWrapLayer::GetPhysicalDeviceFeatures(VkResult Result, VkPhysicalDevice PhysicalDevice, VkPhysicalDeviceFeatures* Features)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintfBeginResult(FString::Printf(TEXT("GetPhysicalDeviceFeatures(PhysicalDevice=0x%p, Features=0x%p)[...]"), PhysicalDevice, Features));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DebugLog += FString::Printf(TEXT("VkPhysicalDeviceFeatures [...]\n"));
#endif
	}
}

void FWrapLayer::GetPhysicalDeviceFormatProperties(VkResult Result, VkPhysicalDevice PhysicalDevice, VkFormat Format, VkFormatProperties* FormatProperties)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintfBegin(FString::Printf(TEXT("vkGetPhysicalDeviceFormatProperties(PhysicalDevice=0x%p, Format=%d, FormatProperties=0x%p)[...]"), PhysicalDevice, (int32)Format, FormatProperties));
#endif
	}
}

void FWrapLayer::GetPhysicalDeviceProperties(VkResult Result, VkPhysicalDevice PhysicalDevice, VkPhysicalDeviceProperties* FormatProperties)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintfBegin(FString::Printf(TEXT("vkGetPhysicalDeviceProperties(PhysicalDevice=0x%p, Properties=0x%p)[...]"), PhysicalDevice, FormatProperties));
#endif
	}
}

void FWrapLayer::BeginCommandBuffer(VkResult Result, VkCommandBuffer CommandBuffer, const VkCommandBufferBeginInfo* BeginInfo)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintfBeginResult(FString::Printf(TEXT("vkBeginCommandBuffer(CmdBuffer=0x%p, Info=0x%p)[...]"), CommandBuffer, BeginInfo));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResult(Result);
#endif
	}
}

void FWrapLayer::EndCommandBuffer(VkResult Result, VkCommandBuffer CommandBuffer)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBeginResult(CommandBuffer, FString::Printf(TEXT("vkEndCommandBuffer(Cmd=0x%p)")));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResult(Result);
#endif
	}
}

void FWrapLayer::ResetQueryPool(VkResult Result, VkCommandBuffer CommandBuffer, VkQueryPool QueryPool, uint32 FirstQuery, uint32 QueryCount)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdResetQueryPool(QueryPool=0x%p, FirstQuery=%d, NumQueries=%d)"), QueryPool, FirstQuery, QueryCount));
#endif
	}
}

void FWrapLayer::WriteTimestamp(VkResult Result, VkCommandBuffer CommandBuffer, VkPipelineStageFlagBits PipelineStage, VkQueryPool QueryPool, uint32 Query)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdWriteTimestamp(PipelineStage=%d, QueryPool=0x%p, Query=%d)"), (int32)PipelineStage, QueryPool, Query));
#endif
	}
}

void FWrapLayer::BindPipeline(VkResult Result, VkCommandBuffer CommandBuffer, VkPipelineBindPoint PipelineBindPoint, VkPipeline Pipeline)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdBindPipeline(BindPoint=%d, Pipeline=0x%p)[...]"), (int32)PipelineBindPoint, Pipeline));
#endif
	}
	else
	{
	}
}


void FWrapLayer::BeginRenderPass(VkResult Result, VkCommandBuffer CommandBuffer, const VkRenderPassBeginInfo* RenderPassBegin, VkSubpassContents Contents)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		auto GetSubpassContents = [](VkSubpassContents InContents) -> FString
			{
				switch (InContents)
				{
					case VK_SUBPASS_CONTENTS_INLINE: return TEXT("INLINE");
					case VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS: return TEXT("SECONDARY_CMD_BUFS");
					default: return FString::Printf(TEXT("%d"), (int32)InContents);
				}					
			};
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("----- vkCmdBeginRenderPass(BeginInfo=0x%p, Contents=%s)"), RenderPassBegin, *GetSubpassContents(Contents)));
		DebugLog += FString::Printf(TEXT("%sBeginInfo: RenderPass=0x%p, Framebuffer=0x%p, renderArea=(x:%d, y:%d, %s), clearValues=%d\n"),
			Tabs, RenderPassBegin->renderPass, RenderPassBegin->framebuffer, 
			RenderPassBegin->renderArea.offset.x, RenderPassBegin->renderArea.offset.y, 
			*GetExtentString(RenderPassBegin->renderArea.extent),
			RenderPassBegin->clearValueCount);
		for (uint32 Index = 0; Index < RenderPassBegin->clearValueCount; ++Index)
		{
			DebugLog += FString::Printf(TEXT("%s\tclearValue[%d]=(%s)\n"), Tabs, Index, *GetClearValueString(RenderPassBegin->pClearValues[Index]));
		}

		FRenderPassInfo* FoundRPInfo = GRenderPassInfo.Find(RenderPassBegin->renderPass);
		ensure(FoundRPInfo);
		if (FoundRPInfo)
		{
			FFBInfo* FoundFBInfo = GFramebufferInfo.Find(RenderPassBegin->framebuffer);
			ensure(FoundFBInfo);
			if (FoundFBInfo)
			{
				for (uint32 Index = 0; Index < FoundFBInfo->Info.attachmentCount; ++Index)
				{
					VkImageView View = FoundFBInfo->Attachments[Index];
					VkImageViewCreateInfo* FoundImageInfo = GImageViewTracker.Find(View);
					ensure(FoundImageInfo);
					if (FoundImageInfo)
					{
						DebugLog += FString::Printf(TEXT("%s\t\tAttachment[%d]: ImageView=0x%p(I:0x%p)\n"), Tabs, Index, View, FoundImageInfo->image);
					}
				}
			}
		}

		FlushDebugWrapperLog();
#endif
	}
}

void FWrapLayer::EndRenderPass(VkResult Result, VkCommandBuffer CommandBuffer)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, TEXT("----- vkCmdEndRenderPass()"));
#endif
	}
}

void FWrapLayer::BeginQuery(VkResult Result, VkCommandBuffer CommandBuffer, VkQueryPool QueryPool, uint32 Query, VkQueryControlFlags Flags)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdBeginQuery(QueryPool=0x%p, Query=%d Flags=%d)"), QueryPool, Query, Flags));
#endif
	}
}
void FWrapLayer::EndQuery(VkResult Result, VkCommandBuffer CommandBuffer, VkQueryPool QueryPool, uint32 Query)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdEndQuery(QueryPool=0x%p, Query=%d)"), QueryPool, Query));
#endif
	}
}


void FWrapLayer::BindVertexBuffers(VkResult Result, VkCommandBuffer CommandBuffer, uint32 FirstBinding, uint32 BindingCount, const VkBuffer* Buffers, const VkDeviceSize* Offsets)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdBindVertexBuffers(FirstBinding=%d, NumBindings=%d, Buffers=0x%p, Offsets=0x%p)[...]"), FirstBinding, BindingCount, Buffers, Offsets));
		FlushDebugWrapperLog();
#endif
	}
}

void FWrapLayer::BindIndexBuffer(VkResult Result, VkCommandBuffer CommandBuffer, VkBuffer IndexBuffer, VkDeviceSize Offset, VkIndexType IndexType)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdBindIndexBuffer(Buffer=0x%p, Offset=%d, IndexType=%d)"), IndexBuffer, (int32)Offset, (int32)IndexType));
		FlushDebugWrapperLog();
#endif
	}
}

void FWrapLayer::SetViewport(VkResult Result, VkCommandBuffer CommandBuffer, uint32 FirstViewport, uint32 ViewportCount, const VkViewport* Viewports)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdSetViewport(FirstViewport=%d, ViewportCount=%d, Viewports=0x%p)[...]"), FirstViewport, ViewportCount, Viewports));
#endif
	}
}

void FWrapLayer::SetScissor(VkResult Result, VkCommandBuffer CommandBuffer, uint32 FirstScissor, uint32 ScissorCount, const VkRect2D* Scissors)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdSetScissor(FirstScissor=%d, ScissorCount=%d, Scissors=0x%p)[...]"), FirstScissor, ScissorCount, Scissors));
#endif
	}
}

void FWrapLayer::SetLineWidth(VkResult Result, VkCommandBuffer CommandBuffer, float LineWidth)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdSetLineWidth(Width=%f)"), LineWidth));
#endif
	}
}

void FWrapLayer::Draw(VkResult Result, VkCommandBuffer CommandBuffer, uint32 VertexCount, uint32 InstanceCount, uint32 FirstVertex, uint32 FirstInstance)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdDraw(NumVertices=%d, NumInstances=%d, FirstVertex=%d, FirstInstance=%d)"), VertexCount, InstanceCount, FirstVertex, FirstInstance));
#endif
	}
}

void FWrapLayer::DrawIndexed(VkResult Result, VkCommandBuffer CommandBuffer, uint32 IndexCount, uint32 InstanceCount, uint32 FirstIndex, int32_t VertexOffset, uint32 FirstInstance)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdDrawIndexed(IndexCount=%d, NumInstances=%d, FirstIndex=%d, VertexOffset=%d, FirstInstance=%d)"), IndexCount, InstanceCount, FirstIndex, VertexOffset, FirstInstance));
#endif
	}
}

void FWrapLayer::DrawIndirect(VkResult Result, VkCommandBuffer CommandBuffer, VkBuffer Buffer, VkDeviceSize Offset, uint32 DrawCount, uint32 Stride)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdDrawIndirect(Buffer=0x%p, Offset=%d, DrawCount=%d, Stride=%d)"), (void*)Buffer, Offset, DrawCount, Stride));
#endif
	}
}

void FWrapLayer::DrawIndexedIndirect(VkResult Result, VkCommandBuffer CommandBuffer, VkBuffer Buffer, VkDeviceSize Offset, uint32 DrawCount, uint32 Stride)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdDrawIndexedIndirect(Buffer=0x%p, Offset=%d, DrawCount=%d, Stride=%d)"), (void*)Buffer, Offset, DrawCount, Stride));
#endif
	}
}

void FWrapLayer::Dispatch(VkResult Result, VkCommandBuffer CommandBuffer, uint32 X, uint32 Y, uint32 Z)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdDispatch(X=%d, Y=%d Z=%d)"), X, Y, Z));
#endif
	}
}

void FWrapLayer::DispatchIndirect(VkResult Result, VkCommandBuffer CommandBuffer, VkBuffer Buffer, VkDeviceSize Offset)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdDispatchIndirect(Buffer=0x%p, Offset=%d)"), (void*)Buffer, Offset));
#endif
	}
}

void FWrapLayer::CopyImage(VkResult Result, VkCommandBuffer CommandBuffer, VkImage SrcImage, VkImageLayout SrcImageLayout, VkImage DstImage, VkImageLayout DstImageLayout, uint32 RegionCount, const VkImageCopy* Regions)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdCopyImage(SrcImage=0x%p, SrcImageLayout=%d, DstImage=0x%p, DstImageLayout=%d, RegionCount=%d, Regions=0x%p)[...]"), SrcImage, (int32)SrcImageLayout, DstImage, (int32)DstImageLayout, RegionCount, Regions));
#endif
#if VULKAN_ENABLE_IMAGE_TRACKING_LAYER
		FScopeLock ScopeLock(&GTrackingCS);
		FTrackingImage* FoundSrc = GVulkanTrackingImageLayouts.Find(SrcImage);
		FTrackingImage* FoundDest = GVulkanTrackingImageLayouts.Find(DstImage);
		ensure(FoundSrc && FoundDest);
		if (FoundSrc && FoundDest)
		{
			for (uint32 Index = 0; Index < RegionCount; ++Index)
			{
				ensure(Regions[Index].srcSubresource.layerCount == Regions[Index].dstSubresource.layerCount);
				for (uint32 LIndex = 0; LIndex < Regions[Index].srcSubresource.layerCount; ++LIndex)
				{
					ensure(FoundSrc->ArrayLayouts[Regions[Index].srcSubresource.baseArrayLayer + LIndex][Regions[Index].srcSubresource.mipLevel] == SrcImageLayout);
					ensure(FoundDest->ArrayLayouts[Regions[Index].dstSubresource.baseArrayLayer + LIndex][Regions[Index].dstSubresource.mipLevel] == DstImageLayout);
				}
			}
		}
#endif
	}
}



void FWrapLayer::CopyBufferToImage(VkResult Result, VkCommandBuffer CommandBuffer, VkBuffer SrcBuffer, VkImage DstImage, VkImageLayout DstImageLayout, uint32 RegionCount, const VkBufferImageCopy* Regions)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdCopyBufferToImage(SrcBuffer=0x%p, DstImage=0x%p, DstImageLayout=%s, NumRegions=%d, Regions=0x%p)"),
			SrcBuffer, DstImage, *GetImageLayoutString(DstImageLayout), RegionCount, Regions));
		for (uint32 Index = 0; Index < RegionCount; ++Index)
		{
			DebugLog += FString::Printf(TEXT("%sRegion[%d]: %s\n"), Tabs, Index, *GetBufferImageCopyString(Regions[Index]));
		}
		FlushDebugWrapperLog();
#endif
#if VULKAN_ENABLE_IMAGE_TRACKING_LAYER
		FScopeLock ScopeLock(&GTrackingCS);
		FTrackingImage* FoundDest = GVulkanTrackingImageLayouts.Find(DstImage);
		ensure(FoundDest);
		if (FoundDest)
		{
			for (uint32 Index = 0; Index < RegionCount; ++Index)
			{
				for (uint32 LIndex = 0; LIndex < Regions[Index].imageSubresource.layerCount; ++LIndex)
				{
					ensure(FoundDest->ArrayLayouts[Regions[Index].imageSubresource.baseArrayLayer + LIndex][Regions[Index].imageSubresource.mipLevel] == DstImageLayout);
				}
			}
		}
#endif
	}
}

void FWrapLayer::CopyImageToBuffer(VkResult Result, VkCommandBuffer CommandBuffer, VkImage SrcImage, VkImageLayout SrcImageLayout, VkBuffer DstBuffer, uint32 RegionCount, const VkBufferImageCopy* Regions)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdCopyImageToBuffer(SrcImage=0x%p, SrcImageLayout=%s, SrcBuffer=0x%p, NumRegions=%d, Regions=0x%p)"),
			SrcImage, *GetImageLayoutString(SrcImageLayout), DstBuffer, RegionCount, Regions));
		for (uint32 Index = 0; Index < RegionCount; ++Index)
		{
			DebugLog += FString::Printf(TEXT("%sRegion[%d]: %s\n"), Tabs, Index, *GetBufferImageCopyString(Regions[Index]));
		}
		FlushDebugWrapperLog();
#endif
	}
}

void FWrapLayer::CopyBuffer(VkResult Result, VkCommandBuffer CommandBuffer, VkBuffer SrcBuffer, VkBuffer DstBuffer, uint32 RegionCount, const VkBufferCopy* Regions)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdCopyBuffer(SrcBuffer=0x%p, DstBuffer=0x%p, NumRegions=%d, Regions=0x%p)"), SrcBuffer, DstBuffer, RegionCount, Regions));
		for (uint32 Index = 0; Index < RegionCount; ++Index)
		{
			DebugLog += FString::Printf(TEXT("%sRegion[%d]: SrcOffset=%d DestOffset=%d Size=%d\n"), Tabs, Index,
				(int32)Regions[Index].srcOffset, (int32)Regions[Index].dstOffset, (int32)Regions[Index].size);
		}
		FlushDebugWrapperLog();
#endif
	}
}

void FWrapLayer::BlitImage(VkResult Result, VkCommandBuffer CommandBuffer, VkImage SrcImage, VkImageLayout SrcImageLayout, VkImage DstImage, VkImageLayout DstImageLayout, uint32 RegionCount, const VkImageBlit* Regions, VkFilter Filter)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdBlitImage(SrcImage=0x%p, SrcImageLayout=%d, DstImage=0x%p, DstImageLayout=%d, RegionCount=%d, Regions=0x%p, Filter=%d)[...]"), SrcImage, (int32)SrcImageLayout, DstImage, (int32)DstImageLayout, RegionCount, Regions, (int32)Filter));

		FlushDebugWrapperLog();
#endif
#if VULKAN_ENABLE_IMAGE_TRACKING_LAYER
		FScopeLock ScopeLock(&GTrackingCS);
		FTrackingImage* FoundSrc = GVulkanTrackingImageLayouts.Find(SrcImage);
		FTrackingImage* FoundDest = GVulkanTrackingImageLayouts.Find(DstImage);
		ensure(FoundSrc && FoundDest);
		if (FoundSrc && FoundDest)
		{
			for (uint32 Index = 0; Index < RegionCount; ++Index)
			{
				ensure(Regions[Index].srcSubresource.layerCount == Regions[Index].dstSubresource.layerCount);
				for (uint32 LIndex = 0; LIndex < Regions[Index].srcSubresource.layerCount; ++LIndex)
				{
					ensure(FoundSrc->ArrayLayouts[Regions[Index].srcSubresource.baseArrayLayer + LIndex][Regions[Index].srcSubresource.mipLevel] == SrcImageLayout);
					ensure(FoundDest->ArrayLayouts[Regions[Index].dstSubresource.baseArrayLayer + LIndex][Regions[Index].dstSubresource.mipLevel] == DstImageLayout);
				}
			}
		}
#endif
	}
}

#if VULKAN_ENABLE_DUMP_LAYER
static void DumpImageSubresourceLayout(VkSubresourceLayout* Layout)
{
	DebugLog += FString::Printf(TEXT("VkSubresourceLayout: [...]\n"));
	FlushDebugWrapperLog();
}
#endif

void FWrapLayer::GetImageSubresourceLayout(VkResult Result, VkDevice Device, VkImage Image, const VkImageSubresource* Subresource, VkSubresourceLayout* Layout)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkGetImageSubresourceLayout(Image=0x%p, Subresource=0x%p, OutLayout=0x%p)"), Image, Subresource, Layout));
		FlushDebugWrapperLog();
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DumpImageSubresourceLayout(Layout);
#endif
	}
}

void FWrapLayer::GetSwapChainImagesKHR(VkResult Result, VkDevice Device, VkSwapchainKHR Swapchain, uint32_t* SwapchainImageCount, VkImage* SwapchainImages)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkGetSwapchainImagesKHR(Swapchain=0x%p, OutSwapchainImageCount=0x%p, OutSwapchainImages=0x%p)\n"), Swapchain, SwapchainImageCount, SwapchainImages));
		FlushDebugWrapperLog();
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResult(Result);
		if (SwapchainImages)
		{
			for (uint32 Index = 0; Index < *SwapchainImageCount; ++Index)
			{
				DebugLog += FString::Printf(TEXT("%sImage[%d]=0x%p\n"), Tabs, Index, SwapchainImages[Index]);
				FImageLayout& Layout = GOLDImageLayoutTracker.Add(SwapchainImages[Index]);
				Layout.Init(1, 1);
			}
		}
		else
		{
			DebugLog += FString::Printf(TEXT("%sNumImages=%d\n"), Tabs, *SwapchainImageCount);
		}
#endif
#if VULKAN_ENABLE_IMAGE_TRACKING_LAYER
		if (SwapchainImages)
		{
			FScopeLock ScopeLock(&GTrackingCS);
			for (uint32 Index = 0; Index < *SwapchainImageCount; ++Index)
			{
				FTrackingImage& TrackingImage = GVulkanTrackingImageLayouts.FindOrAdd(SwapchainImages[Index]);
				TrackingImage.Setup(1, 1, VK_IMAGE_LAYOUT_UNDEFINED, true);
			}
		}
#endif
	}
}

void FWrapLayer::ClearAttachments(VkResult Result, VkCommandBuffer CommandBuffer, uint32 AttachmentCount, const VkClearAttachment* Attachments, uint32 RectCount, const VkClearRect* Rects)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdClearAttachments(AttachmentCount=%d, Attachments=0x%p, RectCount=%d, Rects=0x%p)"), AttachmentCount, Attachments, RectCount, Rects));
		for (uint32 Index = 0; Index < AttachmentCount; ++Index)
		{
			DebugLog += FString::Printf(TEXT("%sAttachment[%d]= aspect=%s ColorAtt=%d ClearValue=%s\n"), Tabs, Index,
				*GetAspectMaskString(Attachments[Index].aspectMask), Attachments[Index].colorAttachment, *GetClearValueString(Attachments[Index].clearValue));
		}

		for (uint32 Index = 0; Index < RectCount; ++Index)
		{
			DebugLog += FString::Printf(TEXT("%sRects[%d]= Rect=(%s) BaseArrayLayer=%d NumLayers=%d\n"), Tabs, Index, *GetRectString(Rects[Index].rect), Rects[Index].baseArrayLayer, Rects[Index].layerCount);
		}

		FlushDebugWrapperLog();
#endif
	}
}

void FWrapLayer::ClearColorImage(VkResult Result, VkCommandBuffer CommandBuffer, VkImage Image, VkImageLayout ImageLayout, const VkClearColorValue* Color, uint32 RangeCount, const VkImageSubresourceRange* Ranges)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdClearColorImage(Image=0x%p, ImageLayout=%s, Color=%s, RangeCount=%d, Ranges=0x%p)"), Image, *GetImageLayoutString(ImageLayout), *GetClearColorValueString(*Color), RangeCount, Ranges));
		for (uint32 Index = 0; Index < RangeCount; ++Index)
		{
			DebugLog += FString::Printf(TEXT("%sRange[%d]= %s\n"), Tabs, Index, *GetImageSubResourceRangeString(Ranges[Index]));
		}
		FlushDebugWrapperLog();
#endif
	}
}

void FWrapLayer::ClearDepthStencilImage(VkResult Result, VkCommandBuffer CommandBuffer, VkImage Image, VkImageLayout ImageLayout, const VkClearDepthStencilValue* DepthStencil, uint32 RangeCount, const VkImageSubresourceRange* Ranges)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdClearDepthStencilImage(Image=0x%p, ImageLayout=%s, DepthStencil=%s, RangeCount=%d, Ranges=0x%p)"), Image, *GetImageLayoutString(ImageLayout), *GetClearDepthStencilValueString(*DepthStencil), RangeCount, Ranges));
		for (uint32 Index = 0; Index < RangeCount; ++Index)
		{
			DebugLog += FString::Printf(TEXT("%sRange[%d]= %s\n"), Tabs, Index, *GetImageSubResourceRangeString(Ranges[Index]));
		}
		FlushDebugWrapperLog();
#endif
	}
}

void FWrapLayer::QueuePresent(VkResult Result, VkQueue Queue, const VkPresentInfoKHR* PresentInfo)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintfBeginResult(FString::Printf(TEXT("vkQueuePresentKHR(Queue=0x%p, Info=0x%p)[...]"), Queue, PresentInfo));

		DebugLog += FString::Printf(TEXT("\n%sPresentInfo: Results=0x%p"), Tabs, PresentInfo->pResults);
		if (PresentInfo->waitSemaphoreCount > 0)
		{
			DebugLog += FString::Printf(TEXT("\n%s\tWaitSemaphores: "), Tabs);
			for (uint32 SubIndex = 0; SubIndex < PresentInfo->waitSemaphoreCount; ++SubIndex)
			{
				DebugLog += FString::Printf(TEXT("0x%p "), PresentInfo->pWaitSemaphores[SubIndex]);
			}
		}
		if (PresentInfo->swapchainCount > 0)
		{
			DebugLog += FString::Printf(TEXT("\n%s\tSwapchains (ImageIndex): "), Tabs);
			for (uint32 SubIndex = 0; SubIndex < PresentInfo->swapchainCount; ++SubIndex)
			{
				DebugLog += FString::Printf(TEXT("0x%p(%d)"), PresentInfo->pSwapchains[SubIndex], PresentInfo->pImageIndices[SubIndex]);
			}
		}
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResult(Result);
#endif
	}
}

void FWrapLayer::CreateGraphicsPipelines(VkResult Result, VkDevice Device, VkPipelineCache PipelineCache, uint32 CreateInfoCount, const VkGraphicsPipelineCreateInfo* CreateInfos, VkPipeline* Pipelines)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkCreateGraphicsPipelines(PipelineCache=0x%p, CreateInfoCount=%d, CreateInfos=0x%p, OutPipelines=0x%p)[...]"), PipelineCache, CreateInfoCount, CreateInfos, Pipelines));
		for (uint32 Index = 0; Index < CreateInfoCount; ++Index)
		{
			const VkGraphicsPipelineCreateInfo& CreateInfo = CreateInfos[Index];
			DebugLog += FString::Printf(TEXT("%s%d: Flags=%d Stages=%d Layout=0x%p RenderPass=0x%p Subpass=%d\n"), Tabs, Index,
				CreateInfo.flags, CreateInfo.stageCount, (void*)CreateInfo.layout, (void*)CreateInfo.renderPass, CreateInfo.subpass);
			DebugLog += FString::Printf(TEXT("%s\tDepth Test %d Write %d %s Bounds %d (min %f max %f) Stencil %d\n"), Tabs, CreateInfo.pDepthStencilState->depthTestEnable, CreateInfo.pDepthStencilState->depthWriteEnable, *GetCompareOpString(CreateInfo.pDepthStencilState->depthCompareOp), CreateInfo.pDepthStencilState->depthBoundsTestEnable, CreateInfo.pDepthStencilState->minDepthBounds, CreateInfo.pDepthStencilState->maxDepthBounds, CreateInfo.pDepthStencilState->stencilTestEnable);

			auto PrintStencilOp = [](const VkStencilOpState& State)
			{
				return FString::Printf(TEXT("Fail %s Pass %s DepthFail %s Compare %s CompareMask 0x%x WriteMask 0x%x Ref 0x%0x"), *GetStencilOpString(State.failOp), *GetStencilOpString(State.passOp), *GetStencilOpString(State.depthFailOp), *GetCompareOpString(State.compareOp), State.compareMask, State.writeMask, State.reference);
			};

			DebugLog += FString::Printf(TEXT("%s\t\tFront: %s\n"), Tabs, *PrintStencilOp(CreateInfo.pDepthStencilState->front));
			DebugLog += FString::Printf(TEXT("%s\t\tBack: %s\n"), Tabs, *PrintStencilOp(CreateInfo.pDepthStencilState->back));
/*
			DebugLog += FString::Printf(TEXT(""));
			typedef struct VkGraphicsPipelineCreateInfo {
				const VkPipelineShaderStageCreateInfo*           pStages;
				const VkPipelineVertexInputStateCreateInfo*      pVertexInputState;
				const VkPipelineInputAssemblyStateCreateInfo*    pInputAssemblyState;
				const VkPipelineTessellationStateCreateInfo*     pTessellationState;
				const VkPipelineViewportStateCreateInfo*         pViewportState;
				const VkPipelineRasterizationStateCreateInfo*    pRasterizationState;
				const VkPipelineMultisampleStateCreateInfo*      pMultisampleState;
				const VkPipelineColorBlendStateCreateInfo*       pColorBlendState;
				const VkPipelineDynamicStateCreateInfo*          pDynamicState;
				VkPipeline                                       basePipelineHandle;
				int32_t                                          basePipelineIndex;
			} VkGraphicsPipelineCreateInfo;
*/
		}
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		//#todo-rco: Multiple pipelines!
		PrintResultAndNamedHandle(Result, TEXT("Pipeline"), Pipelines[0]);
#endif
	}
}

void FWrapLayer::GetDeviceQueue(VkResult Result, VkDevice Device, uint32 QueueFamilyIndex, uint32 QueueIndex, VkQueue* Queue)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkGetDeviceQueue(QueueFamilyIndex=%d, QueueIndex=%d, OutQueue=0x%p)\n"), QueueFamilyIndex, QueueIndex, Queue));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResultAndNamedHandle(VK_SUCCESS, TEXT("Queue"), *Queue);
#endif
	}
}

void FWrapLayer::DeviceWaitIdle(VkResult Result, VkDevice Device)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkDeviceWaitIdle()")));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResult(Result);
#endif
	}
}

void FWrapLayer::MapMemory(VkResult Result, VkDevice Device, VkDeviceMemory Memory, VkDeviceSize Offset, VkDeviceSize Size, VkMemoryMapFlags Flags, void** Data)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkMapMemory(DevMem=0x%p, Off=%d, Size=%d, Flags=0x%x, OutData=0x%p)\n"), Memory, (uint32)Offset, (uint32)Size, Flags, Data));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResultAndPointer(Result, *Data);
#endif
	}
}

void FWrapLayer::UnmapMemory(VkResult Result, VkDevice Device, VkDeviceMemory Memory)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkUnmapMemory(DevMem=0x%p)\n"), Memory));
#endif
	}
	else
	{
	}
}

void FWrapLayer::BindBufferMemory(VkResult Result, VkDevice Device, VkBuffer Buffer, VkDeviceMemory Memory, VkDeviceSize MemoryOffset)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkBindBufferMemory(Buffer=0x%p, DevMem=0x%p, MemOff=%d)\n"), Buffer, Memory, (uint32)MemoryOffset));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResult(Result);
#endif
	}
}

void FWrapLayer::BindImageMemory(VkResult Result, VkDevice Device, VkImage Image, VkDeviceMemory Memory, VkDeviceSize MemoryOffset)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkBindImageMemory(Image=0x%p, DevMem=0x%p, MemOff=%d)\n"), Image, Memory, (uint32)MemoryOffset));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResult(Result);
#endif
	}
}

void FWrapLayer::GetFenceStatus(VkResult Result, VkDevice Device, VkFence Fence)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkGetFenceStatus(Fence=0x%p)"), Fence));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResult(Result);
#endif
	}
}

void FWrapLayer::GetQueryPoolResults(VkResult Result, VkDevice Device, VkQueryPool QueryPool, uint32 FirstQuery, uint32 QueryCount, size_t DataSize, void* Data, VkDeviceSize Stride, VkQueryResultFlags Flags)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkGetQueryPoolResults(QueryPool=0x%p, FirstQuery=%d, QueryCount=%d, DataSize=%d, Data=0x%p, Stride=%d, Flags=%d)[...]"), QueryPool, FirstQuery, QueryCount, (int32)DataSize, Data, Stride, Flags));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResult(Result);
#endif
	}
}

void FWrapLayer::CreateComputePipelines(VkResult Result, VkDevice Device, VkPipelineCache PipelineCache, uint32 CreateInfoCount, const VkComputePipelineCreateInfo* CreateInfos, VkPipeline* Pipelines)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkCreateComputePipelines(PipelineCache=0x%p, CreateInfoCount=%d, CreateInfos=0x%p, OutPipelines=0x%p)[...]\n"), PipelineCache, CreateInfoCount, CreateInfos, Pipelines));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		//#todo-rco: Multiple pipelines!
		PrintResultAndNamedHandle(Result, TEXT("Pipeline"), Pipelines[0]);
#endif
	}
}

void FWrapLayer::AllocateCommandBuffers(VkResult Result, VkDevice Device, const VkCommandBufferAllocateInfo* AllocateInfo, VkCommandBuffer* CommandBuffers)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkAllocateCommandBuffers(AllocateInfo=0x%p, OutCommandBuffers=0x%p)[...]\n"), AllocateInfo, CommandBuffers));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResultAndNamedHandle(Result, TEXT("CommandBuffers"), *CommandBuffers);
#endif
	}
}

void FWrapLayer::CreateSwapchainKHR(VkResult Result, VkDevice Device, const VkSwapchainCreateInfoKHR* CreateInfo, VkSwapchainKHR* Swapchain)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkCreateSwapchainKHR(SwapChainInfo=0x%p, OutSwapChain=0x%p)[...]"), CreateInfo, Swapchain));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResultAndNamedHandle(Result, TEXT("SwapChain"), *Swapchain);
#endif
	}
}

void FWrapLayer::AcquireNextImageKHR(VkResult Result, VkDevice Device, VkSwapchainKHR Swapchain, uint64_t Timeout, VkSemaphore Semaphore, VkFence Fence, uint32_t* ImageIndex)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBeginResult(Device, FString::Printf(TEXT("vkAcquireNextImageKHR(Swapchain=0x%p, Timeout=0x%p, Semaphore=0x%p, Fence=0x%p, OutImageIndex=0x%p)[...]\n"), Swapchain, (void*)Timeout, Semaphore, Fence, ImageIndex));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResultAndNamedHandle(Result, TEXT("ImageIndex"), *ImageIndex);
#endif
	}
}

void FWrapLayer::FreeMemory(VkResult Result, VkDevice Device, VkDeviceMemory Memory)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkFreeMemory(DevMem=0x%p)"), Memory));
#endif
	}
}


void FWrapLayer::DestroyFence(VkResult Result, VkDevice Device, VkFence Fence)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkDestroyFence(Fence=0x%p)"), Fence));
#endif
	}
}

void FWrapLayer::DestroySemaphore(VkResult Result, VkDevice Device, VkSemaphore Semaphore)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkDestroySemaphore(Semaphore=0x%p)"), Semaphore));
#endif
	}
}

void FWrapLayer::CreateEvent(VkResult Result, VkDevice Device, const VkEventCreateInfo* CreateInfo, VkEvent* Event)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkCreateEvent(CreateInfo=0x%p, OutEvent=0x%p)"), CreateInfo, Event));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResultAndNamedHandle(Result, TEXT("Event"), *Event);
#endif
	}
}

void FWrapLayer::DestroyEvent(VkResult Result, VkDevice Device, VkEvent Event)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkDestroyEvent(Event=0x%p)"), Event));
#endif
	}
	else
	{
	}
}

void FWrapLayer::DestroyBuffer(VkResult Result, VkDevice Device, VkBuffer Buffer)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkDestroyBuffer(Buffer=0x%p)"), Buffer));
#endif
	}
	else
	{
#if VULKAN_ENABLE_BUFFER_TRACKING_LAYER
		{
			FScopeLock ScopeLock(&GTrackingCS);
			int32 NumRemoved = GVulkanTrackingBuffers.Remove(Buffer);
			ensure(NumRemoved > 0);

			auto* Found = GVulkanTrackingBufferToBufferViews.Find(Buffer);
			if (Found)
			{
				ensure(Found->Num() > 0);
			}
		}
#endif
	}
}

void FWrapLayer::DestroyBufferView(VkResult Result, VkDevice Device, VkBufferView BufferView)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkDestroyBufferView(BufferView=0x%p)"), BufferView));
#endif
	}
	else
	{
#if VULKAN_ENABLE_BUFFER_TRACKING_LAYER
		{
			FScopeLock ScopeLock(&GTrackingCS);
			int32 NumRemoved = GVulkanTrackingBufferViews.Remove(BufferView);
			ensure(NumRemoved > 0);
		}
#endif
	}
}

void FWrapLayer::DestroyQueryPool(VkResult Result, VkDevice Device, VkQueryPool QueryPool)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkDestroyQueryPool(QueryPool=0x%p)"), QueryPool));
#endif
	}
}

void FWrapLayer::DestroyImageView(VkResult Result, VkDevice Device, VkImageView ImageView)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkDestroyImageView(ImageView=0x%p)"), ImageView));
#endif
	}
	else
	{
#if VULKAN_ENABLE_IMAGE_TRACKING_LAYER
		{
			FScopeLock ScopeLock(&GTrackingCS);
			int32 NumRemoved = GVulkanTrackingImageViews.Remove(ImageView);
			ensure(NumRemoved > 0);
		}
#endif
	}
}

void FWrapLayer::DestroyShaderModule(VkResult Result, VkDevice Device, VkShaderModule ShaderModule)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
			DevicePrintfBegin(Device, FString::Printf(TEXT("vkDestroyShaderModule(ShaderModule=0x%p)"), ShaderModule));
#endif
	}
}

void FWrapLayer::DestroyPipelineCache(VkResult Result, VkDevice Device, VkPipelineCache PipelineCache)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkDestroyPipelineCache(PipelineCache=0x%p)"), PipelineCache));
#endif
	}
}

void FWrapLayer::GetPipelineCacheData(VkResult Result, VkDevice Device, VkPipelineCache PipelineCache, size_t* DataSize, void* Data)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkGetPipelineCacheData(PipelineCache=0x%p, DataSize=%d, [Data])"), PipelineCache, DataSize));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResult(Result);
#endif
	}
}

void FWrapLayer::MergePipelineCaches(VkResult Result, VkDevice Device, VkPipelineCache DestCache, uint32 SourceCacheCount, const VkPipelineCache* SrcCaches)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkMergePipelineCaches(DestCache=0x%p, SourceCacheCount=%d, [SrcCaches])"), DestCache, SourceCacheCount));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResult(Result);
#endif
	}
}

void FWrapLayer::DestroySampler(VkResult Result, VkDevice Device, VkSampler Sampler)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkDestroySampler(Sampler=0x%p)"), Sampler));
#endif
	}
}

void FWrapLayer::DestroySwapchainKHR(VkResult Result, VkDevice Device, VkSwapchainKHR SwapchainKHR)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkDestroySwapchainKHR(SwapchainKHR=0x%p)"), SwapchainKHR));
#endif
	}
}

void FWrapLayer::FreeCommandBuffers(VkResult Result, VkDevice Device, VkCommandPool CommandPool, uint32 CommandBufferCount, const VkCommandBuffer* CommandBuffers)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkFreeCommandBuffers(CommandPool=0x%p, CommandBufferCount=%d, CommandBuffers=0x%p)[...]"), CommandPool, CommandBufferCount, CommandBuffers));
#endif
	}
}


void FWrapLayer::DestroyInstance(VkResult Result, VkInstance Instance)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintfBegin(FString::Printf(TEXT("vkDestroyInstance(Instance=0x%p)"), Instance));
#endif
	}
}

void FWrapLayer::ResetDescriptorPool(VkResult Result, VkDevice Device, VkDescriptorPool DescriptorPool, VkDescriptorPoolResetFlags Flags)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkResetDescriptorPool(DescriptorPool=0x%p, Flags=0x%x)"), DescriptorPool, (uint32)Flags));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResult(Result);
#endif
	}
}

void FWrapLayer::DestroyDescriptorPool(VkResult Result, VkDevice Device, VkDescriptorPool DescriptorPool)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
			DevicePrintfBegin(Device, FString::Printf(TEXT("vkDestroyDescriptorPool(DescriptorPool=0x%p)"), DescriptorPool));
#endif
	}
}

void FWrapLayer::DestroyDescriptorSetLayout(VkResult Result, VkDevice Device, VkDescriptorSetLayout DescriptorSetLayout)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkDestroyDescriptorSetLayout(DescriptorSetLayout=0x%p)"), DescriptorSetLayout));
#endif
	}
}

void FWrapLayer::DestroySurfaceKHR(VkResult Result, VkInstance Instance, VkSurfaceKHR SurfaceKHR)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintfBeginResult(FString::Printf(TEXT("vkDestroySurfaceKHR(PhysicalDevice=%llu, Surface=%llu)"), (uint64)Instance, (uint64)SurfaceKHR));
#endif
	}
}
void FWrapLayer::DestroyFramebuffer(VkResult Result, VkDevice Device, VkFramebuffer Framebuffer)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkDestroyFramebuffer(Framebuffer=0x%p)"), Framebuffer));
#endif
	}
}

void FWrapLayer::DestroyRenderPass(VkResult Result, VkDevice Device, VkRenderPass RenderPass)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkDestroyRenderPass(RenderPass=0x%p)"), RenderPass));
#endif
	}
}

void FWrapLayer::DestroyCommandPool(VkResult Result, VkDevice Device, VkCommandPool CommandPool)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkDestroyCommandPool(CommandPool=0x%p)"), CommandPool));
#endif
	}
}

void FWrapLayer::DestroyPipelineLayout(VkResult Result, VkDevice Device, VkPipelineLayout PipelineLayout)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkDestroyPipelineLayout(PipelineLayout=0x%p)"), PipelineLayout));
#endif
	}
}

void FWrapLayer::DestroyPipeline(VkResult Result, VkDevice Device, VkPipeline Pipeline)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkDestroyPipeline(Pipeline=0x%p)"), Pipeline));
#endif
	}
}

void FWrapLayer::DestroyDevice(VkResult Result, VkDevice Device)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkDestroyDevice(Device=0x%p)"), Device));
#endif
	}
}

void FWrapLayer::ResetCommandBuffer(VkResult Result, VkCommandBuffer CommandBuffer, VkCommandBufferResetFlags Flags)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintfBeginResult(FString::Printf(TEXT("vkResetCommandBuffer(Cmd=0x%p, Flags=%d)"), CommandBuffer, Flags));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResult(Result);
#endif
	}
}


void FWrapLayer::GetPhysicalDeviceQueueFamilyProperties(VkResult Result, VkPhysicalDevice PhysicalDevice, uint32* QueueFamilyPropertyCount, VkQueueFamilyProperties* QueueFamilyProperties)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintfBeginResult(FString::Printf(TEXT("vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice=0x%p, QueueFamilyPropertyCount=0x%p, QueueFamilyProperties=0x%p)[...]"), PhysicalDevice, QueueFamilyPropertyCount, QueueFamilyProperties));
#endif
	}
}


#if VULKAN_SUPPORTS_DEDICATED_ALLOCATION
void FWrapLayer::GetImageMemoryRequirements2KHR(VkResult Result, VkDevice Device, const VkImageMemoryRequirementsInfo2KHR* Info, VkMemoryRequirements2KHR* MemoryRequirements)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		DevicePrintfBegin(Device, FString::Printf(TEXT("vkGetImageMemoryRequirements2KHR(Info=0x%p, MemReqs=0x%p)[...]"), Info, MemoryRequirements));
#endif
	}
}
#endif	// VULKAN_SUPPORTS_DEDICATED_ALLOCATION

#if VULKAN_HAS_PHYSICAL_DEVICE_PROPERTIES2
void FWrapLayer::GetPhysicalDeviceProperties2KHR(VkResult Result, VkPhysicalDevice PhysicalDevice, VkPhysicalDeviceProperties2KHR* Properties)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
			PrintfBegin(FString::Printf(TEXT("vkGetPhysicalDeviceProperties2KHR(PhysicalDevice=0x%p, Properties=0x%p)[...]"), PhysicalDevice, Properties));
#endif
	}
}
#endif


void FWrapLayer::SetDepthBias(VkResult Result, VkCommandBuffer CommandBuffer, float DepthBiasConstantFactor, float DepthBiasClamp, float DepthBiasSlopeFactor)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdSetDepthBias(ConstFactor=%f, Clamp=%f, SlopeFactor=%f)"), DepthBiasConstantFactor, DepthBiasClamp, DepthBiasSlopeFactor));
#endif
	}
}

void FWrapLayer::SetBlendConstants(VkResult Result, VkCommandBuffer CommandBuffer, const float BlendConstants[4])
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdSetBlendConstants(BlendConstants=[%f, %f, %f, %f])"), BlendConstants[0], BlendConstants[1], BlendConstants[2], BlendConstants[3]));
#endif
	}
}

void FWrapLayer::SetDepthBounds(VkResult Result, VkCommandBuffer CommandBuffer, float MinDepthBounds, float MaxDepthBounds)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdSetDepthBounds(Min=%f Max=%f])"), MinDepthBounds, MaxDepthBounds));
#endif
	}
}

void FWrapLayer::SetStencilCompareMask(VkResult Result, VkCommandBuffer CommandBuffer, VkStencilFaceFlags FaceMask, uint32 CompareMask)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdSetStencilCompareMask(FaceMask=%d, CompareMask=%d)"), (int32)FaceMask, (int32)CompareMask));
#endif
	}
}

void FWrapLayer::SetStencilWriteMask(VkResult Result, VkCommandBuffer CommandBuffer, VkStencilFaceFlags FaceMask, uint32 WriteMask)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdSetStencilWriteMask(FaceMask=%d, CompareMask=%d)"), (int32)FaceMask, (int32)WriteMask));
#endif
	}
}

void FWrapLayer::SetStencilReference(VkResult Result, VkCommandBuffer CommandBuffer, VkStencilFaceFlags FaceMask, uint32 Reference)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdSetStencilReference(FaceMask=%d, Ref=%d)"), (int32)FaceMask, (int32)Reference));
#endif
	}
}

void FWrapLayer::FillBuffer(VkResult Result, VkCommandBuffer CommandBuffer, VkBuffer DstBuffer, VkDeviceSize DstOffset, VkDeviceSize Size, uint32 Data)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdFillBuffer(DstBuffer=0x%p, DstOffset=%d, Size=%d, Data=0x%x)"), DstBuffer, (uint32)DstOffset, (uint32)Size, Data));
#endif
	}
}

void FWrapLayer::SetEvent(VkResult Result, VkCommandBuffer CommandBuffer, VkEvent Event, VkPipelineStageFlags StageMask)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdSetEvent(Event=0x%p, StageMask=0x%x)"), Event, StageMask));
#endif
	}
}

void FWrapLayer::ResetEvent(VkResult Result, VkCommandBuffer CommandBuffer, VkEvent Event, VkPipelineStageFlags StageMask)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdResetEvent(Event=0x%p, StageMask=0x%x)"), Event, StageMask));
#endif
	}
}

void FWrapLayer::CopyQueryPoolResults(VkResult Result, VkCommandBuffer CommandBuffer, VkQueryPool QueryPool, uint32 FirstQuery, uint32 QueryCount, VkBuffer DstBuffer, VkDeviceSize DstOffset, VkDeviceSize Stride, VkQueryResultFlags Flags)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		CmdPrintfBegin(CommandBuffer, FString::Printf(TEXT("vkCmdCopyQueryPoolResults(QueryPool=0x%p, FirstQuery=%d, QueryCount=%d, DstBuffer=0x%p, DstOffset=%d, Stride=%d, Flags=0x%x)"),
				(void*)QueryPool, FirstQuery, QueryCount, (void*)DstBuffer, (uint32)DstOffset, (uint32)Stride, (uint32)Flags));
#endif
	}
}

void FWrapLayer::GetInstanceProcAddr(VkResult Result, VkInstance Instance, const char* Name, PFN_vkVoidFunction VoidFunction)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
			PrintfBeginResult(FString::Printf(TEXT("vkGetInstanceProcAddr(Instance=0x%p, Name=%s)[...]"), Instance, ANSI_TO_TCHAR(Name)));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResultAndPointer(VK_SUCCESS, (void*)VoidFunction);
#endif
	}
}

void FWrapLayer::GetDeviceProcAddr(VkResult Result, VkDevice Device, const char* Name, PFN_vkVoidFunction VoidFunction)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintfBeginResult(FString::Printf(TEXT("vkGetDeviceProcAddr(Device=0x%p, Name=%s)[...]"), Device, ANSI_TO_TCHAR(Name)));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResultAndPointer(VK_SUCCESS, (void*)VoidFunction);
#endif
	}
}

void FWrapLayer::EnumerateInstanceExtensionProperties(VkResult Result, const char* LayerName, uint32* PropertyCount, VkExtensionProperties* Properties)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintfBeginResult(FString::Printf(TEXT("vkEnumerateInstanceExtensionProperties(LayerName=%s, PropertyCount=0x%p, Properties=0x%p)[...]"), ANSI_TO_TCHAR(LayerName), PropertyCount, Properties));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResultAndPointer(Result, (void*)(uint64)PropertyCount);
		PrintResultAndPointer(Result, (void*)Properties);
#endif
	}
}

void FWrapLayer::EnumerateDeviceExtensionProperties(VkResult Result, VkPhysicalDevice PhysicalDevice, const char* LayerName, uint32* PropertyCount, VkExtensionProperties* Properties)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintfBeginResult(FString::Printf(TEXT("vkEnumerateDeviceExtensionProperties(Device=0x%p, LayerName=%s, PropertyCount=0x%p, Properties=0x%p)[...]"), PhysicalDevice, ANSI_TO_TCHAR(LayerName), PropertyCount, Properties));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResultAndPointer(Result, (void*)(uint64)PropertyCount);
		PrintResultAndPointer(Result, (void*)Properties);
#endif
	}
}

void FWrapLayer::EnumerateInstanceLayerProperties(VkResult Result, uint32* PropertyCount, VkLayerProperties* Properties)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintfBeginResult(FString::Printf(TEXT("vkEnumerateInstanceLayerProperties(PropertyCount=0x%p, Properties=0x%p)[...]"), PropertyCount, Properties));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResultAndPointer(Result, (void*)(uint64)PropertyCount);
		PrintResultAndPointer(Result, (void*)Properties);
#endif
	}	
}

void FWrapLayer::EnumerateDeviceLayerProperties(VkResult Result, VkPhysicalDevice PhysicalDevice, uint32* PropertyCount, VkLayerProperties* Properties)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintfBeginResult(FString::Printf(TEXT("vkEnumerateDeviceLayerProperties(Device=0x%p, PropertyCount=0x%p, Properties=0x%p)[...]"), PhysicalDevice, PropertyCount, Properties));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResultAndPointer(Result, (void*)(uint64)PropertyCount);
		PrintResultAndPointer(Result, (void*)Properties);
#endif
	}
}

void FWrapLayer::GetPhysicalDeviceSurfaceCapabilitiesKHR(VkResult Result, VkPhysicalDevice PhysicalDevice, VkSurfaceKHR Surface, VkSurfaceCapabilitiesKHR* SurfaceCapabilities)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintfBeginResult(FString::Printf(TEXT("vkGetPhysicalDeviceSurfaceCapabilitiesKHR(PhysicalDevice=0x%p, Surface=0x%p)[...]"), PhysicalDevice, Surface));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResult(Result);
#endif
	}
}

void FWrapLayer::GetPhysicalDeviceSurfaceFormatsKHR(VkResult Result, VkPhysicalDevice PhysicalDevice, VkSurfaceKHR Surface, uint32_t* SurfaceFormatCountPtr, VkSurfaceFormatKHR* SurfaceFormats)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintfBeginResult(FString::Printf(TEXT("vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice=0x%p, Surface=0x%p)[...]"), PhysicalDevice, Surface));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResult(Result);
#endif
	}
}

void FWrapLayer::GetPhysicalDeviceSurfaceSupportKHR(VkResult Result, VkPhysicalDevice PhysicalDevice, uint32_t QueueFamilyIndex, VkSurfaceKHR Surface, VkBool32* SupportedPtr)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintfBeginResult(FString::Printf(TEXT("vkGetPhysicalDeviceSurfaceSupportKHR(PhysicalDevice=0x%p, QueueFamilyIndex=%d, Surface=0x%p)[...]"), PhysicalDevice, QueueFamilyIndex, (void*)Surface));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResult(Result);
#endif
	}
}

void FWrapLayer::GetPhysicalDeviceSurfacePresentModesKHR(VkResult Result, VkPhysicalDevice PhysicalDevice, VkSurfaceKHR Surface, uint32_t* PresentModeCountPtr, VkPresentModeKHR* PresentModesPtr)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintfBeginResult(FString::Printf(TEXT("vkGetPhysicalDeviceSurfacePresentModesKHR(PhysicalDevice=0x%p, Surface=0x%p, PresentModeCountPtr=%d, PresentModesPtr=0x%p)"), PhysicalDevice, Surface, PresentModeCountPtr ? *PresentModeCountPtr:0, PresentModesPtr));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResult(Result);
#endif
	}
}

#if VULKAN_USE_CREATE_ANDROID_SURFACE
void FWrapLayer::CreateAndroidSurfaceKHR(VkResult Result, VkInstance Instance, const VkAndroidSurfaceCreateInfoKHR* CreateInfo, VkSurfaceKHR* Surface)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintfBeginResult(FString::Printf(TEXT("vkCreateAndroidSurfaceKHR(Instance=0x%p, CreateInfo=0x%p, Surface=0x%p)[...]"), Instance, CreateInfo, Surface));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResult(Result);
#endif
	}
}
#endif

#if VULKAN_USE_CREATE_WIN32_SURFACE
void FWrapLayer::CreateWin32SurfaceKHR(VkResult Result, VkInstance Instance, const VkWin32SurfaceCreateInfoKHR* CreateInfo, VkSurfaceKHR* Surface)
{
	if (Result == VK_RESULT_MAX_ENUM)
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintfBeginResult(FString::Printf(TEXT("vkCreateWin32SurfaceKHR(Instance=0x%p, CreateInfo=0x%p, Surface=0x%p)[...]"), Instance, CreateInfo, Surface));
#endif
	}
	else
	{
#if VULKAN_ENABLE_DUMP_LAYER
		PrintResult(Result);
#endif
	}
}
#endif

#if VULKAN_ENABLE_IMAGE_TRACKING_LAYER
namespace VulkanRHI
{
	void BindDebugLabelName(VkImage Image, const TCHAR* Name)
	{
		FScopeLock ScopeLock(&GTrackingCS);
		FTrackingImage* Found = GVulkanTrackingImageLayouts.Find(Image);
		if (Found)
		{
			Found->Info.DebugName = Name ? Name : TEXT("null");
		}
		else
		{
			ensure(0);
		}
	}
}
#endif

#endif	// VULKAN_ENABLE_CUSTOM_LAYER

#if VULKAN_ENABLE_DUMP_LAYER
namespace VulkanRHI
{
	void TrackImageViewAdd(VkImageView View, const VkImageViewCreateInfo* CreateInfo)
	{
		FScopeLock Lock(&GTrackLock);
		GImageViewTracker.Add(View, *CreateInfo);
	}

	void TrackImageViewRemove(VkImageView View)
	{
		FScopeLock Lock(&GTrackLock);
		GImageViewTracker.Remove(View);
	}

	void TrackBufferViewAdd(VkBufferView View, const VkBufferViewCreateInfo* CreateInfo)
	{
		FScopeLock Lock(&GTrackLock);
		GBufferViewTracker.Add(View, *CreateInfo);
	}

	void TrackBufferViewRemove(VkBufferView View)
	{
		FScopeLock Lock(&GTrackLock);
		GBufferViewTracker.Remove(View);
	}


	static struct FGlobalDumpLog
	{
		~FGlobalDumpLog()
		{
			FlushDebugWrapperLog();
		}
	} GGlobalDumpLogInstance;

	static TArray<FString> GMarkers;
	void DumpLayerPushMarker(const TCHAR* InName)
	{
		FString String = TEXT("***** vkCmdDbgMarkerBeginEXT: ");
		for (auto& Name : GMarkers)
		{
			String += Name;
			String += TEXT("/");
		}
		GMarkers.Push(InName);
		String += InName;
		String += TEXT("\n");

		DebugLog += String;
		FlushDebugWrapperLog();
	}

	void DumpLayerPopMarker()
	{
		FString String = TEXT("***** vkCmdDbgMarkerEndEXT: ");
		GMarkers.Pop();
		for (auto& Name : GMarkers)
		{
			String += Name;
			String += TEXT("/");
		}

		String += TEXT("\n");

		DebugLog += String;
		FlushDebugWrapperLog();
	}
}
#endif	// VULKAN_ENABLE_DUMP_LAYER
#endif // VULKAN_HAS_DEBUGGING_ENABLED
