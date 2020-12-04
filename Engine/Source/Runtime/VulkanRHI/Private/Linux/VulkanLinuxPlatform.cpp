// Copyright Epic Games, Inc. All Rights Reserved.

#include "VulkanLinuxPlatform.h"
#include "../VulkanRHIPrivate.h"
#include <dlfcn.h>
#include <SDL.h>
#include <SDL_vulkan.h>
#include "Linux/LinuxPlatformApplicationMisc.h"



// Vulkan function pointers
#define DEFINE_VK_ENTRYPOINTS(Type,Func) Type VulkanDynamicAPI::Func = NULL;
ENUM_VK_ENTRYPOINTS_ALL(DEFINE_VK_ENTRYPOINTS)

static bool GRenderOffScreen = false;
static bool GForceEnableDebugMarkers = false;

void* FVulkanLinuxPlatform::VulkanLib = nullptr;
bool FVulkanLinuxPlatform::bAttemptedLoad = false;

bool FVulkanLinuxPlatform::IsSupported()
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("RenderOffScreen")))
	{
		// If we're not running offscreen mode, make sure we have a display envvar set
		bool bHasX11Display = (getenv("DISPLAY") != nullptr);

		if (!bHasX11Display)
		{
			// check Wayland
			bool bHasWaylandSession = (getenv("WAYLAND_DISPLAY") != nullptr);

			if (!bHasWaylandSession)
			{
				UE_LOG(LogRHI, Warning, TEXT("Could not detect DISPLAY or WAYLAND_DISPLAY environment variables"));
				return false;
			}
		}
	}

	// Attempt to load the library
	return LoadVulkanLibrary();
}

bool FVulkanLinuxPlatform::LoadVulkanLibrary()
{
	if (bAttemptedLoad)
	{
		return (VulkanLib != nullptr);
	}
	bAttemptedLoad = true;

	// try to load libvulkan.so
	VulkanLib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);

	if (VulkanLib == nullptr)
	{
		return false;
	}

	bool bFoundAllEntryPoints = true;
#define CHECK_VK_ENTRYPOINTS(Type,Func) if (VulkanDynamicAPI::Func == NULL) { bFoundAllEntryPoints = false; UE_LOG(LogRHI, Warning, TEXT("Failed to find entry point for %s"), TEXT(#Func)); }

	// Initialize all of the entry points we have to query manually
#define GET_VK_ENTRYPOINTS(Type,Func) VulkanDynamicAPI::Func = (Type)dlsym(VulkanLib, #Func);
	ENUM_VK_ENTRYPOINTS_BASE(GET_VK_ENTRYPOINTS);
	ENUM_VK_ENTRYPOINTS_BASE(CHECK_VK_ENTRYPOINTS);
	if (!bFoundAllEntryPoints)
	{
		dlclose(VulkanLib);
		VulkanLib = nullptr;
		return false;
	}

	ENUM_VK_ENTRYPOINTS_OPTIONAL_BASE(GET_VK_ENTRYPOINTS);
#if UE_BUILD_DEBUG
	ENUM_VK_ENTRYPOINTS_OPTIONAL_BASE(CHECK_VK_ENTRYPOINTS);
#endif

	ENUM_VK_ENTRYPOINTS_PLATFORM_BASE(GET_VK_ENTRYPOINTS);
	ENUM_VK_ENTRYPOINTS_PLATFORM_BASE(CHECK_VK_ENTRYPOINTS);

#undef GET_VK_ENTRYPOINTS

	// Check for force enabling debug markers
	GForceEnableDebugMarkers = FParse::Param(FCommandLine::Get(), TEXT("vulkandebugmarkers"));

	GRenderOffScreen = FParse::Param(FCommandLine::Get(), TEXT("RenderOffScreen"));
	return true;
}

bool FVulkanLinuxPlatform::ForceEnableDebugMarkers()
{
	return GForceEnableDebugMarkers;
}

bool FVulkanLinuxPlatform::LoadVulkanInstanceFunctions(VkInstance inInstance)
{
	bool bFoundAllEntryPoints = true;
#define CHECK_VK_ENTRYPOINTS(Type,Func) if (VulkanDynamicAPI::Func == NULL) { bFoundAllEntryPoints = false; UE_LOG(LogRHI, Warning, TEXT("Failed to find entry point for %s"), TEXT(#Func)); }

#define GETINSTANCE_VK_ENTRYPOINTS(Type, Func) VulkanDynamicAPI::Func = (Type)VulkanDynamicAPI::vkGetInstanceProcAddr(inInstance, #Func);
	ENUM_VK_ENTRYPOINTS_INSTANCE(GETINSTANCE_VK_ENTRYPOINTS);
	ENUM_VK_ENTRYPOINTS_INSTANCE(CHECK_VK_ENTRYPOINTS);
	ENUM_VK_ENTRYPOINTS_SURFACE_INSTANCE(GETINSTANCE_VK_ENTRYPOINTS);
	ENUM_VK_ENTRYPOINTS_SURFACE_INSTANCE(CHECK_VK_ENTRYPOINTS);

	if (!bFoundAllEntryPoints && !GRenderOffScreen)
	{
		return false;
	}

	ENUM_VK_ENTRYPOINTS_OPTIONAL_INSTANCE(GETINSTANCE_VK_ENTRYPOINTS);
	ENUM_VK_ENTRYPOINTS_OPTIONAL_PLATFORM_INSTANCE(GETINSTANCE_VK_ENTRYPOINTS);
#if UE_BUILD_DEBUG
	ENUM_VK_ENTRYPOINTS_OPTIONAL_INSTANCE(CHECK_VK_ENTRYPOINTS);
	ENUM_VK_ENTRYPOINTS_OPTIONAL_PLATFORM_INSTANCE(CHECK_VK_ENTRYPOINTS);
#endif

	ENUM_VK_ENTRYPOINTS_PLATFORM_INSTANCE(GETINSTANCE_VK_ENTRYPOINTS);
	ENUM_VK_ENTRYPOINTS_PLATFORM_INSTANCE(CHECK_VK_ENTRYPOINTS);
#undef GET_VK_ENTRYPOINTS

	return true;
}

void FVulkanLinuxPlatform::FreeVulkanLibrary()
{
	if (VulkanLib != nullptr)
	{
#define CLEAR_VK_ENTRYPOINTS(Type,Func) VulkanDynamicAPI::Func = nullptr;
		ENUM_VK_ENTRYPOINTS_ALL(CLEAR_VK_ENTRYPOINTS);

		dlclose(VulkanLib);
		VulkanLib = nullptr;
	}
	bAttemptedLoad = false;
}

namespace
{
	void EnsureSDLIsInited()
	{
		if (!FLinuxPlatformApplicationMisc::InitSDL()) //   will not initialize more than once
		{
			FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, TEXT("Vulkan InitSDL() failed, cannot initialize SDL."), TEXT("InitSDL Failed"));
			UE_LOG(LogInit, Error, TEXT("Vulkan InitSDL() failed, cannot initialize SDL."));
		}
	}
}

void FVulkanLinuxPlatform::GetInstanceExtensions(TArray<const ANSICHAR*>& OutExtensions)
{
	EnsureSDLIsInited();

	// we don't hardcode the extensions in Linux, we query SDL
	static TArray<const ANSICHAR*> CachedLinuxExtensions;
	if (CachedLinuxExtensions.Num() == 0)
	{
		uint32_t Count = 0;
		auto RequiredExtensions = SDL_Vulkan_GetRequiredInstanceExtensions(&Count);
		for (int32 i = 0; i < Count; i++)
		{
			CachedLinuxExtensions.Add(RequiredExtensions[i]);
		}
	}
	OutExtensions.Append(CachedLinuxExtensions);
}

void FVulkanLinuxPlatform::GetDeviceExtensions(EGpuVendorId VendorId, TArray<const ANSICHAR*>& OutExtensions)
{
	const bool bAllowVendorDevice = !FParse::Param(FCommandLine::Get(), TEXT("novendordevice"));

#if VULKAN_SUPPORTS_DRIVER_PROPERTIES
	OutExtensions.Add(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME);
#endif

#if VULKAN_SUPPORTS_DEDICATED_ALLOCATION
	OutExtensions.Add(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
	OutExtensions.Add(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
#endif

	if (GGPUCrashDebuggingEnabled)
	{
#if VULKAN_SUPPORTS_AMD_BUFFER_MARKER
		if (VendorId == EGpuVendorId::Amd && bAllowVendorDevice)
		{
			OutExtensions.Add(VK_AMD_BUFFER_MARKER_EXTENSION_NAME);
		}
#endif
#if VULKAN_SUPPORTS_NV_DIAGNOSTIC_CHECKPOINT
		if (VendorId == EGpuVendorId::Nvidia && bAllowVendorDevice)
		{
			OutExtensions.Add(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
		}
#endif
#if VULKAN_SUPPORTS_NV_DIAGNOSTIC_CHECKPOINT
		if (VendorId == EGpuVendorId::Nvidia && bAllowVendorDevice)
		{
			OutExtensions.Add(VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME);
		}
#endif
	}
}

void FVulkanLinuxPlatform::CreateSurface(void* WindowHandle, VkInstance Instance, VkSurfaceKHR* OutSurface)
{
	EnsureSDLIsInited();

	if (SDL_Vulkan_CreateSurface((SDL_Window*)WindowHandle, Instance, OutSurface) == SDL_FALSE)
	{
		UE_LOG(LogInit, Error, TEXT("Error initializing SDL Vulkan Surface: %s"), SDL_GetError());
		check(0);
	}
}

bool FVulkanLinuxPlatform::SupportsStandardSwapchain()
{
	return GRenderOffScreen ?
		false : FVulkanGenericPlatform::SupportsStandardSwapchain();
}

EPixelFormat FVulkanLinuxPlatform::GetPixelFormatForNonDefaultSwapchain()
{
	return GRenderOffScreen ?
		PF_R8G8B8A8 : FVulkanGenericPlatform::GetPixelFormatForNonDefaultSwapchain();
}

void FVulkanLinuxPlatform::WriteCrashMarker(const FOptionalVulkanDeviceExtensions& OptionalExtensions, VkCommandBuffer CmdBuffer, VkBuffer DestBuffer, const TArrayView<uint32>& Entries, bool bAdding)
{
	ensure(Entries.Num() <= GMaxCrashBufferEntries);

	if (OptionalExtensions.HasAMDBufferMarker)
	{
		// AMD API only allows updating one entry at a time. Assume buffer has entry 0 as num entries
		VulkanDynamicAPI::vkCmdWriteBufferMarkerAMD(CmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, DestBuffer, 0, Entries.Num());
		if (bAdding)
		{
			int32 LastIndex = Entries.Num() - 1;
			// +1 size as entries start at index 1
			VulkanDynamicAPI::vkCmdWriteBufferMarkerAMD(CmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, DestBuffer, (1 + LastIndex) * sizeof(uint32), Entries[LastIndex]);
		}
	}
	else if (OptionalExtensions.HasNVDiagnosticCheckpoints)
	{
		if (bAdding)
		{
			int32 LastIndex = Entries.Num() - 1;
			uint32 Value = Entries[LastIndex];
			VulkanDynamicAPI::vkCmdSetCheckpointNV(CmdBuffer, (void*)(size_t)Value);
		}
	}
}
