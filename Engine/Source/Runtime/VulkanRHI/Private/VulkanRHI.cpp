// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanRHI.cpp: Vulkan device RHI implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "HardwareInfo.h"
#include "VulkanShaderResources.h"
#include "VulkanResources.h"
#include "VulkanPendingState.h"
#include "VulkanContext.h"
#include "Misc/CommandLine.h"
#include "GenericPlatform/GenericPlatformDriver.h"
#include "Modules/ModuleManager.h"
#include "VulkanPipelineState.h"
#include "Misc/FileHelper.h"

extern RHI_API bool GUseTexture3DBulkDataRHI;

#if VULKAN_ENABLE_DESKTOP_HMD_SUPPORT
#include "Runtime/HeadMountedDisplay/Public/IHeadMountedDisplayModule.h"
#endif

#define LOCTEXT_NAMESPACE "VulkanRHI"

#ifdef VK_API_VERSION
// Check the SDK is least the API version we want to use
static_assert(VK_API_VERSION >= UE_VK_API_VERSION, "Vulkan SDK is older than the version we want to support (UE_VK_API_VERSION). Please update your SDK.");
#elif !defined(VK_HEADER_VERSION)
	#error No VulkanSDK defines?
#endif

#if defined(VK_HEADER_VERSION) && VK_HEADER_VERSION < 8 && (VK_API_VERSION < VK_MAKE_VERSION(1, 0, 3))
	#include <vulkan/vk_ext_debug_report.h>
#endif

///////////////////////////////////////////////////////////////////////////////

TAutoConsoleVariable<int32> GRHIThreadCvar(
	TEXT("r.Vulkan.RHIThread"),
	!(PLATFORM_LUMIN || PLATFORM_LUMINGL4),
	TEXT("0 to only use Render Thread\n")
	TEXT("1 to use ONE RHI Thread\n")
	TEXT("2 to use multiple RHI Thread\n")
);

bool GGPUCrashDebuggingEnabled = false;


extern TAutoConsoleVariable<int32> GRHIAllowAsyncComputeCvar;


DEFINE_LOG_CATEGORY(LogVulkan)

bool FVulkanDynamicRHIModule::IsSupported()
{
	return true;
}

FDynamicRHI* FVulkanDynamicRHIModule::CreateRHI(ERHIFeatureLevel::Type InRequestedFeatureLevel)
{
	if (!GIsEditor &&
		(FVulkanPlatform::RequiresMobileRenderer() ||
			InRequestedFeatureLevel == ERHIFeatureLevel::ES3_1 || InRequestedFeatureLevel == ERHIFeatureLevel::ES2 ||
			FParse::Param(FCommandLine::Get(), TEXT("featureleveles31")) || FParse::Param(FCommandLine::Get(), TEXT("featureleveles2"))))
	{
		GMaxRHIFeatureLevel = ERHIFeatureLevel::ES3_1;
		GMaxRHIShaderPlatform = PLATFORM_LUMIN ? SP_VULKAN_ES3_1_LUMIN : (PLATFORM_ANDROID ? SP_VULKAN_ES3_1_ANDROID : SP_VULKAN_PCES3_1);
	}
	else if (InRequestedFeatureLevel == ERHIFeatureLevel::SM4)
	{
		GMaxRHIFeatureLevel = ERHIFeatureLevel::SM4;
		GMaxRHIShaderPlatform = SP_VULKAN_SM4;
	}
	else
	{
		GMaxRHIFeatureLevel = ERHIFeatureLevel::SM5;
		GMaxRHIShaderPlatform = (PLATFORM_LUMINGL4 || PLATFORM_LUMIN) ? SP_VULKAN_SM5_LUMIN : SP_VULKAN_SM5;
	}

	// VULKAN_USE_MSAA_RESOLVE_ATTACHMENTS=0 requires separate MSAA and resolve textures
	check(RHISupportsSeparateMSAAAndResolveTextures(GMaxRHIShaderPlatform) == (!VULKAN_USE_MSAA_RESOLVE_ATTACHMENTS));

	return new FVulkanDynamicRHI();
}

IMPLEMENT_MODULE(FVulkanDynamicRHIModule, VulkanRHI);


FVulkanCommandListContext::FVulkanCommandListContext(FVulkanDynamicRHI* InRHI, FVulkanDevice* InDevice, FVulkanQueue* InQueue, FVulkanCommandListContext* InImmediate)
	: RHI(InRHI)
	, Immediate(InImmediate)
	, Device(InDevice)
	, Queue(InQueue)
	, bSubmitAtNextSafePoint(false)
	, bAutomaticFlushAfterComputeShader(true)
	, UniformBufferUploader(nullptr)
	, TempFrameAllocationBuffer(InDevice)
	, CommandBufferManager(nullptr)
	, PendingGfxState(nullptr)
	, PendingComputeState(nullptr)
	, FrameCounter(0)
	, GpuProfiler(this, InDevice)
{
	FrameTiming = new FVulkanGPUTiming(this, InDevice);
	FrameTiming->Initialize();

	// Create CommandBufferManager, contain all active buffers
	CommandBufferManager = new FVulkanCommandBufferManager(InDevice, this);
	if (IsImmediate())
	{
		// Insert the Begin frame timestamp query. On EndDrawingViewport() we'll insert the End and immediately after a new Begin()
		WriteBeginTimestamp(CommandBufferManager->GetActiveCmdBuffer());

		// Flush the cmd buffer immediately to ensure a valid
		// 'Last submitted' cmd buffer exists at frame 0.
		CommandBufferManager->SubmitActiveCmdBuffer();
		CommandBufferManager->PrepareForNewActiveCommandBuffer();
	}

	// Create Pending state, contains pipeline states such as current shader and etc..
	PendingGfxState = new FVulkanPendingGfxState(Device, *this);
	PendingComputeState = new FVulkanPendingComputeState(Device, *this);

#if !VULKAN_USE_DESCRIPTOR_POOL_MANAGER
	// Add an initial pool
	FVulkanDescriptorPool* Pool = new FVulkanDescriptorPool(Device);
	DescriptorPools.Add(Pool);
#endif
	UniformBufferUploader = new FVulkanUniformBufferUploader(Device);
}

FVulkanCommandListContext::~FVulkanCommandListContext()
{
	check(CommandBufferManager != nullptr);
	delete CommandBufferManager;
	CommandBufferManager = nullptr;

	TransitionAndLayoutManager.Destroy(*Device, Immediate ? &TransitionAndLayoutManager : nullptr);

	delete UniformBufferUploader;
	delete PendingGfxState;
	delete PendingComputeState;

	TempFrameAllocationBuffer.Destroy();

#if VULKAN_USE_DESCRIPTOR_POOL_MANAGER
	for (auto TypedDescriptorPoolsPair : DescriptorPools)
	{
		auto& TypedDescriptorPools = TypedDescriptorPoolsPair.Value;
		for (int32 Index = 0; Index < TypedDescriptorPools.Num(); ++Index)
		{
			delete TypedDescriptorPools[Index];
		}

		TypedDescriptorPools.Reset(0);
	}
#else
	for (int32 Index = 0; Index < DescriptorPools.Num(); ++Index)
	{
		delete DescriptorPools[Index];
	}
	DescriptorPools.Reset(0);
#endif
}


FVulkanCommandListContextImmediate::FVulkanCommandListContextImmediate(FVulkanDynamicRHI* InRHI, FVulkanDevice* InDevice, FVulkanQueue* InQueue)
	: FVulkanCommandListContext(InRHI, InDevice, InQueue, nullptr)
{
}


FVulkanDynamicRHI::FVulkanDynamicRHI()
	: Instance(VK_NULL_HANDLE)
	, Device(nullptr)
	, DrawingViewport(nullptr)
{
	// This should be called once at the start 
	check(IsInGameThread());
	check(!GIsThreadedRendering);

	GPoolSizeVRAMPercentage = 0;
	GTexturePoolSize = 0;
	GConfig->GetInt(TEXT("TextureStreaming"), TEXT("PoolSizeVRAMPercentage"), GPoolSizeVRAMPercentage, GEngineIni);
}

void FVulkanDynamicRHI::Init()
{
	if (!FVulkanPlatform::LoadVulkanLibrary())
	{
#if PLATFORM_LINUX
		// be more verbose on Linux
		FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *LOCTEXT("UnableToInitializeVulkanLinux", "Unable to load Vulkan library and/or acquire the necessary function pointers. Make sure an up-to-date libvulkan.so.1 is installed.").ToString(),
									 *LOCTEXT("UnableToInitializeVulkanLinuxTitle", "Unable to initialize Vulkan.").ToString());
#endif // PLATFORM_LINUX
		UE_LOG(LogVulkanRHI, Fatal, TEXT("Failed to find all required Vulkan entry points; make sure your driver supports Vulkan!"));
	}

	{
		IConsoleVariable* GPUCrashDebuggingCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.GPUCrashDebugging"));
		GGPUCrashDebuggingEnabled = GPUCrashDebuggingCVar && GPUCrashDebuggingCVar->GetInt() != 0;
	}

	InitInstance();

	if (GPoolSizeVRAMPercentage > 0)
	{
		const uint64 TotalGPUMemory = Device->GetMemoryManager().GetTotalMemory(true);

		float PoolSize = float(GPoolSizeVRAMPercentage) * 0.01f * float(TotalGPUMemory);

		// Truncate GTexturePoolSize to MB (but still counted in bytes)
		GTexturePoolSize = int64(FGenericPlatformMath::TruncToFloat(PoolSize / 1024.0f / 1024.0f)) * 1024 * 1024;

		UE_LOG(LogRHI, Log, TEXT("Texture pool is %llu MB (%d%% of %llu MB)"),
			GTexturePoolSize / 1024 / 1024,
			GPoolSizeVRAMPercentage,
			TotalGPUMemory / 1024 / 1024);
	}
}

void FVulkanDynamicRHI::Shutdown()
{
	if (FParse::Param(FCommandLine::Get(), TEXT("savevulkanpsocacheonexit")))
	{
		SavePipelineCache();
	}

	check(IsInGameThread() && IsInRenderingThread());
	check(Device);

	Device->PrepareForDestroy();

	EmptyCachedBoundShaderStates();

	FVulkanVertexDeclaration::EmptyCache();

	if (GIsRHIInitialized)
	{
		// Reset the RHI initialized flag.
		GIsRHIInitialized = false;

		GRHINeedsExtraDeletionLatency = false;

		check(!GIsCriticalError);

		// Ask all initialized FRenderResources to release their RHI resources.
		for (TLinkedList<FRenderResource*>::TIterator ResourceIt(FRenderResource::GetResourceList());ResourceIt;ResourceIt.Next())
		{
			FRenderResource* Resource = *ResourceIt;
			check(Resource->IsInitialized());
			Resource->ReleaseRHI();
		}

		for (TLinkedList<FRenderResource*>::TIterator ResourceIt(FRenderResource::GetResourceList());ResourceIt;ResourceIt.Next())
		{
			ResourceIt->ReleaseDynamicRHI();
		}

		{
			for (auto& Pair : Device->SamplerMap)
			{
				FVulkanSamplerState* SamplerState = (FVulkanSamplerState*)Pair.Value.GetReference();
				VulkanRHI::vkDestroySampler(Device->GetInstanceHandle(), SamplerState->Sampler, nullptr);
			}
			Device->SamplerMap.Empty();
		}

		// Flush all pending deletes before destroying the device.
		FRHIResource::FlushPendingDeletes();

		// And again since some might get on a pending queue
		FRHIResource::FlushPendingDeletes();
	}

	Device->Destroy();

	delete Device;
	Device = nullptr;

	// Release the early HMD interface used to query extra extensions - if any was used
	HMDVulkanExtensions = nullptr;

#if VULKAN_HAS_DEBUGGING_ENABLED
	RemoveDebugLayerCallback();
#endif

	VulkanRHI::vkDestroyInstance(Instance, nullptr);

	IConsoleManager::Get().UnregisterConsoleObject(SavePipelineCacheCmd);
	IConsoleManager::Get().UnregisterConsoleObject(RebuildPipelineCacheCmd);

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	IConsoleManager::Get().UnregisterConsoleObject(DumpMemoryCmd);
#endif

	FVulkanPlatform::FreeVulkanLibrary();

#if VULKAN_ENABLE_DUMP_LAYER
	VulkanRHI::FlushDebugWrapperLog();
#endif
}

void FVulkanDynamicRHI::CreateInstance()
{
	// Engine registration can be disabled via console var. Also disable automatically if ShaderDevelopmentMode is on.
	auto* CVarShaderDevelopmentMode = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.ShaderDevelopmentMode"));
	auto* CVarDisableEngineAndAppRegistration = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DisableEngineAndAppRegistration"));
	bool bDisableEngineRegistration = (CVarDisableEngineAndAppRegistration && CVarDisableEngineAndAppRegistration->GetValueOnAnyThread() != 0) ||
		(CVarShaderDevelopmentMode && CVarShaderDevelopmentMode->GetValueOnAnyThread() != 0);
	VkApplicationInfo AppInfo;
	ZeroVulkanStruct(AppInfo, VK_STRUCTURE_TYPE_APPLICATION_INFO);
	AppInfo.pApplicationName = bDisableEngineRegistration ? "" : "UE4";
	//AppInfo.applicationVersion = 0;
	AppInfo.pEngineName = bDisableEngineRegistration ? "" : "UE4";
	AppInfo.engineVersion = 15;
	AppInfo.apiVersion = UE_VK_API_VERSION;

	VkInstanceCreateInfo InstInfo;
	ZeroVulkanStruct(InstInfo, VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
	InstInfo.pApplicationInfo = &AppInfo;

	GetInstanceLayersAndExtensions(InstanceExtensions, InstanceLayers, bSupportsDebugUtilsExt);

	InstInfo.enabledExtensionCount = InstanceExtensions.Num();
	InstInfo.ppEnabledExtensionNames = InstInfo.enabledExtensionCount > 0 ? (const ANSICHAR* const*)InstanceExtensions.GetData() : nullptr;
	
	InstInfo.enabledLayerCount = InstanceLayers.Num();
	InstInfo.ppEnabledLayerNames = InstInfo.enabledLayerCount > 0 ? InstanceLayers.GetData() : nullptr;
#if VULKAN_HAS_DEBUGGING_ENABLED
	bSupportsDebugCallbackExt = !bSupportsDebugUtilsExt && InstanceExtensions.ContainsByPredicate([](const ANSICHAR* Key)
		{ 
			return Key && !FCStringAnsi::Strcmp(Key, VK_EXT_DEBUG_REPORT_EXTENSION_NAME); 
		});
#endif

	VkResult Result = VulkanRHI::vkCreateInstance(&InstInfo, nullptr, &Instance);
	
	if (Result == VK_ERROR_INCOMPATIBLE_DRIVER)
	{
		FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, TEXT(
			"Cannot find a compatible Vulkan driver (ICD).\n\nPlease look at the Getting Started guide for "
			"additional information."), TEXT("Incompatible Vulkan driver found!"));
		FPlatformMisc::RequestExitWithStatus(true, 1);
		// unreachable
		return;
	}
	else if(Result == VK_ERROR_EXTENSION_NOT_PRESENT)
	{
		// Check for missing extensions 
		FString MissingExtensions;

		uint32_t PropertyCount;
		VulkanRHI::vkEnumerateInstanceExtensionProperties(nullptr, &PropertyCount, nullptr);

		TArray<VkExtensionProperties> Properties;
		Properties.SetNum(PropertyCount);
		VulkanRHI::vkEnumerateInstanceExtensionProperties(nullptr, &PropertyCount, Properties.GetData());

		for (const ANSICHAR* Extension : InstanceExtensions)
		{
			bool bExtensionFound = false;

			for (uint32_t PropertyIndex = 0; PropertyIndex < PropertyCount; PropertyIndex++)
			{
				const char* PropertyExtensionName = Properties[PropertyIndex].extensionName;

				if (!FCStringAnsi::Strcmp(PropertyExtensionName, Extension))
				{
					bExtensionFound = true;
					break;
				}
			}

			if (!bExtensionFound)
			{
				FString ExtensionStr = ANSI_TO_TCHAR(Extension);
				UE_LOG(LogVulkanRHI, Error, TEXT("Missing required Vulkan extension: %s"), *ExtensionStr);
				MissingExtensions += ExtensionStr + TEXT("\n");
			}
		}

		FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *FString::Printf(TEXT(
			"Vulkan driver doesn't contain specified extensions:\n%s;\n\
			make sure your layers path is set appropriately."), *MissingExtensions), TEXT("Incomplete Vulkan driver found!"));
	}
	else if (Result != VK_SUCCESS)
	{
		FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, TEXT(
			"Vulkan failed to create instace (apiVersion=0x%x)\n\nDo you have a compatible Vulkan "
			 "driver (ICD) installed?\nPlease look at "
			 "the Getting Started guide for additional information."), TEXT("No Vulkan driver found!"));
		FPlatformMisc::RequestExitWithStatus(true, 1);
		// unreachable
		return;
	}

	VERIFYVULKANRESULT(Result);

	if (!FVulkanPlatform::LoadVulkanInstanceFunctions(Instance))
	{
		FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, TEXT(
			"Failed to find all required Vulkan entry points! Try updating your driver."), TEXT("No Vulkan entry points found!"));
	}

#if VULKAN_HAS_DEBUGGING_ENABLED
	SetupDebugLayerCallback();
#endif
}

//#todo-rco: Common RHI should handle this...
static inline int32 PreferAdapterVendor()
{
	if (FParse::Param(FCommandLine::Get(), TEXT("preferAMD")))
	{
		return 0x1002;
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("preferIntel")))
	{
		return 0x8086;
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("preferNvidia")))
	{
		return 0x10DE;
	}

	return -1;
}

void FVulkanDynamicRHI::SelectAndInitDevice()
{
	uint32 GpuCount = 0;
	VERIFYVULKANRESULT_EXPANDED(VulkanRHI::vkEnumeratePhysicalDevices(Instance, &GpuCount, nullptr));
	check(GpuCount >= 1);

	TArray<VkPhysicalDevice> PhysicalDevices;
	PhysicalDevices.AddZeroed(GpuCount);
	VERIFYVULKANRESULT_EXPANDED(VulkanRHI::vkEnumeratePhysicalDevices(Instance, &GpuCount, PhysicalDevices.GetData()));

#if VULKAN_ENABLE_DESKTOP_HMD_SUPPORT
	FVulkanDevice* HmdDevice = nullptr;
	uint32 HmdDeviceIndex = 0;
#endif
	struct FDiscreteDevice
	{
		FVulkanDevice* Device;
		uint32 DeviceIndex;
	};
	TArray<FDiscreteDevice> DiscreteDevices;

#if VULKAN_ENABLE_DESKTOP_HMD_SUPPORT
	// Allow HMD to override which graphics adapter is chosen, so we pick the adapter where the HMD is connected
	uint64 HmdGraphicsAdapterLuid  = IHeadMountedDisplayModule::IsAvailable() ? IHeadMountedDisplayModule::Get().GetGraphicsAdapterLuid() : 0;
#endif

	UE_LOG(LogVulkanRHI, Display, TEXT("Found %d device(s)"), GpuCount);
	for (uint32 Index = 0; Index < GpuCount; ++Index)
	{
		FVulkanDevice* NewDevice = new FVulkanDevice(PhysicalDevices[Index]);
		Devices.Add(NewDevice);

		bool bIsDiscrete = NewDevice->QueryGPU(Index);

#if VULKAN_ENABLE_DESKTOP_HMD_SUPPORT
		if (!HmdDevice && HmdGraphicsAdapterLuid != 0 &&
			NewDevice->GetOptionalExtensions().HasKHRGetPhysicalDeviceProperties2 &&
			FMemory::Memcmp(&HmdGraphicsAdapterLuid, &NewDevice->GetDeviceIdProperties().deviceLUID, VK_LUID_SIZE_KHR) == 0)
		{
			HmdDevice = NewDevice;
			HmdDeviceIndex = Index;
		}
#endif
		if (bIsDiscrete)
		{
			DiscreteDevices.Add({NewDevice, Index});
		}
	}

	uint32 DeviceIndex = -1;

#if VULKAN_ENABLE_DESKTOP_HMD_SUPPORT
	if (HmdDevice)
	{
		Device = HmdDevice;
		DeviceIndex = HmdDeviceIndex;
	}
#endif

	if (DeviceIndex == -1)
	{
		if (DiscreteDevices.Num() > 0)
		{
			if (DiscreteDevices.Num() > 1)
			{
				// Check for preferred
				for (int32 Index = 0; Index < DiscreteDevices.Num(); ++Index)
				{
					if (DiscreteDevices[Index].Device->GpuProps.vendorID == PreferAdapterVendor())
					{
						DeviceIndex = DiscreteDevices[Index].DeviceIndex;
						Device = DiscreteDevices[Index].Device;
						break;
					}
				}
			}

			if (DeviceIndex == -1)
			{
				Device = DiscreteDevices[0].Device;
				DeviceIndex = DiscreteDevices[0].DeviceIndex;
			}
		}
		else
		{
			Device = Devices[0];
			DeviceIndex = 0;
		}
	}

	check(Device);

	const VkPhysicalDeviceProperties& Props = Device->GetDeviceProperties();
	GRHIVendorId = Props.vendorID;
	GRHIAdapterName = ANSI_TO_TCHAR(Props.deviceName);

	Device->InitGPU(DeviceIndex);

	if (PLATFORM_ANDROID)
	{
		GRHIAdapterInternalDriverVersion = FString::Printf(TEXT("%d.%d.%d"), VK_VERSION_MAJOR(Props.apiVersion), VK_VERSION_MINOR(Props.apiVersion), VK_VERSION_PATCH(Props.apiVersion));
	}
	else if (IsRHIDeviceNVIDIA())
	{
		union UNvidiaDriverVersion
		{
			struct
			{
#if PLATFORM_LITTLE_ENDIAN
				uint32 Tertiary		: 6;
				uint32 Secondary	: 8;
				uint32 Minor		: 8;
				uint32 Major		: 10;
#else
				uint32 Major		: 10;
				uint32 Minor		: 8;
				uint32 Secondary	: 8;
				uint32 Tertiary		: 6;
#endif
			};
			uint32 Packed;
		};
		UNvidiaDriverVersion NvidiaVersion;
		static_assert(sizeof(NvidiaVersion) == sizeof(Props.driverVersion), "Mismatched Nvidia pack driver version!");
		NvidiaVersion.Packed = Props.driverVersion;
		GRHIAdapterUserDriverVersion = FString::Printf(TEXT("%d.%d"), NvidiaVersion.Major, NvidiaVersion.Minor);

		// Ignore GRHIAdapterInternalDriverVersion for now as the device name doesn't match
	}
	else if(PLATFORM_UNIX)
	{
		GRHIAdapterInternalDriverVersion = FString::Printf(TEXT("%d.%d.%d (0x%X)"), VK_VERSION_MAJOR(Props.apiVersion), VK_VERSION_MINOR(Props.apiVersion), VK_VERSION_PATCH(Props.apiVersion), Props.apiVersion);
		GRHIAdapterUserDriverVersion = FString::Printf(TEXT("%d.%d.%d (0x%X)"), VK_VERSION_MAJOR(Props.driverVersion), VK_VERSION_MINOR(Props.driverVersion), VK_VERSION_PATCH(Props.driverVersion), Props.driverVersion);
		GRHIDeviceId = Props.deviceID;
	}
}

void FVulkanDynamicRHI::InitInstance()
{
	check(IsInGameThread());

	// Wait for the rendering thread to go idle.
	SCOPED_SUSPEND_RENDERING_THREAD(false);

	if (!Device)
	{
		check(!GIsRHIInitialized);

		FVulkanPlatform::OverrideCrashHandlers();

		GRHISupportsAsyncTextureCreation = false;
		GEnableAsyncCompute = false;

		CreateInstance();
		SelectAndInitDevice();

		bool bDeviceSupportsGeometryShaders = Device->GetFeatures().geometryShader != 0;
		bool bDeviceSupportsTessellation = Device->GetFeatures().tessellationShader != 0;

		const VkPhysicalDeviceProperties& Props = Device->GetDeviceProperties();

		// Initialize the RHI capabilities.
		GRHISupportsFirstInstance = true;
		GSupportsDepthBoundsTest = Device->GetFeatures().depthBounds != 0;
		GSupportsRenderTargetFormat_PF_G8 = false;	// #todo-rco
		GRHISupportsTextureStreaming = true;
		GSupportsTimestampRenderQueries = FVulkanPlatform::SupportsTimestampRenderQueries();
#if VULKAN_ENABLE_DUMP_LAYER
		// Disable RHI thread by default if the dump layer is enabled
		GRHISupportsRHIThread = false;
#else
		GRHISupportsRHIThread = GRHIThreadCvar->GetInt() != 0;
		GRHISupportsParallelRHIExecute = GRHIThreadCvar->GetInt() > 1;
#endif
		//#todo-rco: Add newer Nvidia also
		GSupportsEfficientAsyncCompute = IsRHIDeviceAMD() && (GRHIAllowAsyncComputeCvar.GetValueOnAnyThread() > 0) && (Device->ComputeContext != Device->ImmediateContext);

		GSupportsVolumeTextureRendering = true;

		// Indicate that the RHI needs to use the engine's deferred deletion queue.
		GRHINeedsExtraDeletionLatency = true;

		GMaxShadowDepthBufferSizeX =  FPlatformMath::Min<int32>(Props.limits.maxImageDimension2D, GMaxShadowDepthBufferSizeX);
		GMaxShadowDepthBufferSizeY =  FPlatformMath::Min<int32>(Props.limits.maxImageDimension2D, GMaxShadowDepthBufferSizeY);
		GMaxTextureDimensions = Props.limits.maxImageDimension2D;
		GMaxTextureMipCount = FPlatformMath::CeilLogTwo( GMaxTextureDimensions ) + 1;
		GMaxTextureMipCount = FPlatformMath::Min<int32>( MAX_TEXTURE_MIP_COUNT, GMaxTextureMipCount );
		GMaxCubeTextureDimensions = Props.limits.maxImageDimensionCube;
		GMaxTextureArrayLayers = Props.limits.maxImageArrayLayers;
		GRHISupportsBaseVertexIndex = true;
		GSupportsSeparateRenderTargetBlendState = true;

		GSupportsDepthFetchDuringDepthTest = FVulkanPlatform::SupportsDepthFetchDuringDepthTest();

		GShaderPlatformForFeatureLevel[ERHIFeatureLevel::ES2] = GMaxRHIFeatureLevel == ERHIFeatureLevel::ES2 ? GMaxRHIShaderPlatform : SP_NumPlatforms;
		GShaderPlatformForFeatureLevel[ERHIFeatureLevel::ES3_1] = GMaxRHIFeatureLevel == ERHIFeatureLevel::ES3_1 ? GMaxRHIShaderPlatform : SP_NumPlatforms;
		GShaderPlatformForFeatureLevel[ERHIFeatureLevel::SM4] = GMaxRHIFeatureLevel == ERHIFeatureLevel::SM4 ? GMaxRHIShaderPlatform : SP_NumPlatforms;
		GShaderPlatformForFeatureLevel[ERHIFeatureLevel::SM5] = (GMaxRHIFeatureLevel == ERHIFeatureLevel::SM5 /*&& bDeviceSupportsTessellation*/) ? GMaxRHIShaderPlatform : SP_NumPlatforms;

		GRHIRequiresRenderTargetForPixelShaderUAVs = true;

		GUseTexture3DBulkDataRHI = true;

		GDynamicRHI = this;

		// Notify all initialized FRenderResources that there's a valid RHI device to create their RHI resources for now.
		for (TLinkedList<FRenderResource*>::TIterator ResourceIt(FRenderResource::GetResourceList()); ResourceIt; ResourceIt.Next())
		{
			ResourceIt->InitRHI();
		}
		// Dynamic resources can have dependencies on static resources (with uniform buffers) and must initialized last!
		for (TLinkedList<FRenderResource*>::TIterator ResourceIt(FRenderResource::GetResourceList()); ResourceIt; ResourceIt.Next())
		{
			ResourceIt->InitDynamicRHI();
		}

		FHardwareInfo::RegisterHardwareInfo(NAME_RHI, TEXT("Vulkan"));

		GProjectionSignY = 1.0f;

		GIsRHIInitialized = true;

		SavePipelineCacheCmd = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("r.Vulkan.SavePipelineCache"),
			TEXT("Save pipeline cache."),
			FConsoleCommandDelegate::CreateStatic(SavePipelineCache),
			ECVF_Default
			);

		RebuildPipelineCacheCmd = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("r.Vulkan.RebuildPipelineCache"),
			TEXT("Rebuilds pipeline cache."),
			FConsoleCommandDelegate::CreateStatic(RebuildPipelineCache),
			ECVF_Default
			);

#if VULKAN_SUPPORTS_VALIDATION_CACHE
#if VULKAN_HAS_DEBUGGING_ENABLED
		if (GValidationCvar.GetValueOnAnyThread() > 0)
		{
			SaveValidationCacheCmd = IConsoleManager::Get().RegisterConsoleCommand(
				TEXT("r.Vulkan.SaveValidationCache"),
				TEXT("Save validation cache."),
				FConsoleCommandDelegate::CreateStatic(SaveValidationCache),
				ECVF_Default
				);
		}
#endif
#endif

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
		DumpMemoryCmd = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("r.Vulkan.DumpMemory"),
			TEXT("Dumps memory map."),
			FConsoleCommandDelegate::CreateStatic(DumpMemory),
			ECVF_Default
			);
#endif
	}
}

void FVulkanCommandListContext::RHIBeginFrame()
{
	check(IsImmediate());
	RHIPrivateBeginFrame();

	extern uint32 GVulkanRHIDeletionFrameNumber;
	++GVulkanRHIDeletionFrameNumber;

	GpuProfiler.BeginFrame();
}


void FVulkanCommandListContext::RHIBeginScene()
{
	//FRCLog::Printf(FString::Printf(TEXT("FVulkanCommandListContext::RHIBeginScene()")));
}

void FVulkanCommandListContext::RHIEndScene()
{
	//FRCLog::Printf(FString::Printf(TEXT("FVulkanCommandListContext::RHIEndScene()")));
}

void FVulkanCommandListContext::RHIBeginDrawingViewport(FViewportRHIParamRef ViewportRHI, FTextureRHIParamRef RenderTargetRHI)
{
	//FRCLog::Printf(FString::Printf(TEXT("FVulkanCommandListContext::RHIBeginDrawingViewport\n")));
	check(ViewportRHI);
	FVulkanViewport* Viewport = ResourceCast(ViewportRHI);
	RHI->DrawingViewport = Viewport;
}

void FVulkanCommandListContext::RHIEndDrawingViewport(FViewportRHIParamRef ViewportRHI, bool bPresent, bool bLockToVsync)
{
	//FRCLog::Printf(FString::Printf(TEXT("FVulkanCommandListContext::RHIEndDrawingViewport()")));
	check(IsImmediate());
	FVulkanViewport* Viewport = ResourceCast(ViewportRHI);
	check(Viewport == RHI->DrawingViewport);

	//#todo-rco: Unbind all pending state
/*
	check(bPresent);
	RHI->Present();
*/
	FVulkanCmdBuffer* CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
	check(!CmdBuffer->HasEnded());
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

	WriteEndTimestamp(CmdBuffer);

	bool bNativePresent = Viewport->Present(this, CmdBuffer, Queue, Device->GetPresentQueue(), bLockToVsync);
	if (bNativePresent)
	{
		//#todo-rco: Check for r.FinishCurrentFrame
	}

	RHI->DrawingViewport = nullptr;

	ReadAndCalculateGPUFrameTime();
	WriteBeginTimestamp(CommandBufferManager->GetActiveCmdBuffer());
}

void FVulkanCommandListContext::RHIEndFrame()
{
	check(IsImmediate());
	//FRCLog::Printf(FString::Printf(TEXT("FVulkanCommandListContext::RHIEndFrame()")));

	
	GetGPUProfiler().EndFrame();

	Device->GetStagingManager().ProcessPendingFree(false, true);
	Device->GetResourceHeapManager().ReleaseFreedPages();

#if VULKAN_USE_DESCRIPTOR_POOL_MANAGER
	Device->GetDescriptorPoolsManager().GC();
#endif

	++FrameCounter;
}

void FVulkanCommandListContext::RHIPushEvent(const TCHAR* Name, FColor Color)
{
	FString EventName = Name;
	EventStack.Add(Name);

#if VULKAN_ENABLE_DRAW_MARKERS
#if 0//VULKAN_SUPPORTS_DEBUG_UTILS
	if (auto CmdBeginLabel = Device->GetCmdBeginDebugLabel())
	{
		FTCHARToUTF8 Converter(Name);
		VkDebugUtilsLabelEXT Label;
		ZeroVulkanStruct(Label, VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT);
		Label.pLabelName = Converter.Get();
		FLinearColor LColor(Color);
		Label.color[0] = LColor.R;
		Label.color[1] = LColor.G;
		Label.color[2] = LColor.B;
		Label.color[3] = LColor.A;
		CmdBeginLabel(GetCommandBufferManager()->GetActiveCmdBuffer()->GetHandle(), &Label);
	}
	else
#endif
	if (auto CmdDbgMarkerBegin = Device->GetCmdDbgMarkerBegin())
	{
		FTCHARToUTF8 Converter(Name);
		VkDebugMarkerMarkerInfoEXT Info;
		ZeroVulkanStruct(Info, VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT);
		Info.pMarkerName = Converter.Get();
		FLinearColor LColor(Color);
		Info.color[0] = LColor.R;
		Info.color[1] = LColor.G;
		Info.color[2] = LColor.B;
		Info.color[3] = LColor.A;
		CmdDbgMarkerBegin(GetCommandBufferManager()->GetActiveCmdBuffer()->GetHandle(), &Info);
	}
#endif

#if VULKAN_SUPPORTS_AMD_BUFFER_MARKER
	if (GpuProfiler.bTrackingGPUCrashData)
	{
		GpuProfiler.PushMarkerForCrash(GetCommandBufferManager()->GetActiveCmdBuffer()->GetHandle(), Device->GetCrashMarkerBuffer(), Name);
	}
#endif

	//only valid on immediate context currently.  needs to be fixed for parallel rhi execute
	if (IsImmediate())
	{
#if VULKAN_ENABLE_DUMP_LAYER
		VulkanRHI::DumpLayerPushMarker(Name);
#endif

		GpuProfiler.PushEvent(Name, Color);
	}
}

void FVulkanCommandListContext::RHIPopEvent()
{
#if VULKAN_ENABLE_DRAW_MARKERS
#if 0//VULKAN_SUPPORTS_DEBUG_UTILS
	if (auto CmdEndLabel = Device->GetCmdEndDebugLabel())
	{
		CmdEndLabel(GetCommandBufferManager()->GetActiveCmdBuffer()->GetHandle());
	}
	else
#endif
	if (auto CmdDbgMarkerEnd = Device->GetCmdDbgMarkerEnd())
	{
		CmdDbgMarkerEnd(GetCommandBufferManager()->GetActiveCmdBuffer()->GetHandle());
	}
#endif

#if VULKAN_SUPPORTS_AMD_BUFFER_MARKER
	if (GpuProfiler.bTrackingGPUCrashData)
	{
		GpuProfiler.PopMarkerForCrash(GetCommandBufferManager()->GetActiveCmdBuffer()->GetHandle(), Device->GetCrashMarkerBuffer());
	}
#endif

	//only valid on immediate context currently.  needs to be fixed for parallel rhi execute
	if (IsImmediate())
	{
#if VULKAN_ENABLE_DUMP_LAYER
		VulkanRHI::DumpLayerPopMarker();
#endif

		GpuProfiler.PopEvent();
	}

	check(EventStack.Num() > 0);
	EventStack.Pop(false);
}

void FVulkanDynamicRHI::RHIGetSupportedResolution( uint32 &Width, uint32 &Height )
{
}

bool FVulkanDynamicRHI::RHIGetAvailableResolutions(FScreenResolutionArray& Resolutions, bool bIgnoreRefreshRate)
{
	return false;
}

void FVulkanDynamicRHI::RHIFlushResources()
{
}

void FVulkanDynamicRHI::RHIAcquireThreadOwnership()
{
}

void FVulkanDynamicRHI::RHIReleaseThreadOwnership()
{
}

void* FVulkanDynamicRHI::RHIGetNativeDevice()
{
	return (void*)Device->GetInstanceHandle();
}

IRHICommandContext* FVulkanDynamicRHI::RHIGetDefaultContext()
{
	return &Device->GetImmediateContext();
}

IRHIComputeContext* FVulkanDynamicRHI::RHIGetDefaultAsyncComputeContext()
{
	return &Device->GetImmediateComputeContext();
}

IRHICommandContextContainer* FVulkanDynamicRHI::RHIGetCommandContextContainer(int32 Index, int32 Num)
{
	if (GRHIThreadCvar.GetValueOnAnyThread() > 1)
	{
		return new FVulkanCommandContextContainer(Device);
	}

	return nullptr;
}

void FVulkanDynamicRHI::RHISubmitCommandsAndFlushGPU()
{
	Device->SubmitCommandsAndFlushGPU();
}

FTexture2DRHIRef FVulkanDynamicRHI::RHICreateTexture2DFromResource(EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 NumMips, uint32 NumSamples, uint32 NumSamplesTileMem, VkImage Resource, uint32 Flags)
{
	return new FVulkanTexture2D(*Device, Format, SizeX, SizeY, NumMips, NumSamples, NumSamplesTileMem, Resource, Flags, FRHIResourceCreateInfo());
}

FTexture2DArrayRHIRef FVulkanDynamicRHI::RHICreateTexture2DArrayFromResource(EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 ArraySize, uint32 NumMips, VkImage Resource, uint32 Flags)
{
	return new FVulkanTexture2DArray(*Device, Format, SizeX, SizeY, ArraySize, NumMips, Resource, Flags, nullptr, FClearValueBinding());
}

FTextureCubeRHIRef FVulkanDynamicRHI::RHICreateTextureCubeFromResource(EPixelFormat Format, uint32 Size, bool bArray, uint32 ArraySize, uint32 NumMips, VkImage Resource, uint32 Flags)
{
	return new FVulkanTextureCube(*Device, Format, Size, bArray, ArraySize, NumMips, Resource, Flags, nullptr, FClearValueBinding());
}

void FVulkanDynamicRHI::RHIAliasTextureResources(FTextureRHIParamRef DestTextureRHI, FTextureRHIParamRef SrcTextureRHI)
{
	if (DestTextureRHI && SrcTextureRHI)
	{
		FVulkanTextureBase* DestTextureBase = (FVulkanTextureBase*) DestTextureRHI->GetTextureBaseRHI();
		FVulkanTextureBase* SrcTextureBase = (FVulkanTextureBase*) SrcTextureRHI->GetTextureBaseRHI();

		if (DestTextureBase && SrcTextureBase)
		{
			DestTextureBase->AliasTextureResources(SrcTextureBase);
		}
	}
}


FVulkanBuffer::FVulkanBuffer(FVulkanDevice& InDevice, uint32 InSize, VkFlags InUsage, VkMemoryPropertyFlags InMemPropertyFlags, bool bInAllowMultiLock, const char* File, int32 Line) :
	Device(InDevice),
	Buf(VK_NULL_HANDLE),
	Allocation(nullptr),
	Size(InSize),
	Usage(InUsage),
	BufferPtr(nullptr),
	bAllowMultiLock(bInAllowMultiLock),
	LockStack(0)
{
	VkBufferCreateInfo BufInfo;
	ZeroVulkanStruct(BufInfo, VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
	BufInfo.size = Size;
	BufInfo.usage = Usage;
	VERIFYVULKANRESULT_EXPANDED(VulkanRHI::vkCreateBuffer(Device.GetInstanceHandle(), &BufInfo, nullptr, &Buf));

	VkMemoryRequirements MemoryRequirements;
	VulkanRHI::vkGetBufferMemoryRequirements(Device.GetInstanceHandle(), Buf, &MemoryRequirements);

	Allocation = InDevice.GetMemoryManager().Alloc(false, MemoryRequirements.size, MemoryRequirements.memoryTypeBits, InMemPropertyFlags, nullptr, File ? File : __FILE__, Line ? Line : __LINE__);
	check(Allocation);
	VERIFYVULKANRESULT_EXPANDED(VulkanRHI::vkBindBufferMemory(Device.GetInstanceHandle(), Buf, Allocation->GetHandle(), 0));
}

FVulkanBuffer::~FVulkanBuffer()
{
	// The buffer should be unmapped
	check(BufferPtr == nullptr);

	Device.GetDeferredDeletionQueue().EnqueueResource(FDeferredDeletionQueue::EType::Buffer, Buf);
	Buf = VK_NULL_HANDLE;

	Device.GetMemoryManager().Free(Allocation);
	Allocation = nullptr;
}

void* FVulkanBuffer::Lock(uint32 InSize, uint32 InOffset)
{
	check(InSize + InOffset <= Size);

	uint32 BufferPtrOffset = 0;
	if (bAllowMultiLock)
	{
		if (LockStack == 0)
		{
			// lock the whole range
			BufferPtr = Allocation->Map(GetSize(), 0);
		}
		// offset the whole range by the requested offset
		BufferPtrOffset = InOffset;
		LockStack++;
	}
	else
	{
		check(BufferPtr == nullptr);
		BufferPtr = Allocation->Map(InSize, InOffset);
	}

	return (uint8*)BufferPtr + BufferPtrOffset;
}

void FVulkanBuffer::Unlock()
{
	// The buffer should be mapped, before it can be unmapped
	check(BufferPtr != nullptr);

	// for multi-lock, if not down to 0, do nothing
	if (bAllowMultiLock && --LockStack > 0)
	{
		return;
	}

	Allocation->Unmap();
	BufferPtr = nullptr;
}


FVulkanDescriptorSetsLayout::FVulkanDescriptorSetsLayout(FVulkanDevice* InDevice) :
	Device(InDevice)
{
}

FVulkanDescriptorSetsLayout::~FVulkanDescriptorSetsLayout()
{
	VulkanRHI::FDeferredDeletionQueue& DeletionQueue = Device->GetDeferredDeletionQueue();
	for (VkDescriptorSetLayout& Handle : LayoutHandles)
	{
		DeletionQueue.EnqueueResource(VulkanRHI::FDeferredDeletionQueue::EType::DescriptorSetLayout, Handle);
	}

	LayoutHandles.Reset(0);
}

void FVulkanDescriptorSetsLayoutInfo::AddDescriptor(int32 DescriptorSetIndex, const VkDescriptorSetLayoutBinding& Descriptor, int32 BindingIndex)
{
	// Increment type usage
	LayoutTypes[Descriptor.descriptorType]++;

	if (DescriptorSetIndex >= SetLayouts.Num())
	{
		SetLayouts.SetNum(DescriptorSetIndex + 1, false);
	}

	FSetLayout& DescSetLayout = SetLayouts[DescriptorSetIndex];

	VkDescriptorSetLayoutBinding* Binding = new(DescSetLayout.LayoutBindings) VkDescriptorSetLayoutBinding;
	*Binding = Descriptor;

	// Verify this descriptor doesn't already exist
	for (int32 Index = 0; Index < BindingIndex; ++Index)
	{
		ensure(DescSetLayout.LayoutBindings[Index].binding != BindingIndex || &DescSetLayout.LayoutBindings[Index] != Binding);
	}

	//#todo-rco: Needs a change for the hashing!
	ensure(!Descriptor.pImmutableSamplers);

	Hash = FCrc::MemCrc32(&Binding, sizeof(Binding), Hash);
}

#if VULKAN_USE_DESCRIPTOR_POOL_MANAGER
void FVulkanDescriptorSetsLayoutInfo::CompileTypesUsageID()
{
	static TMap<uint32, uint32> GTypesUsageHashMap;
	static uint32 GUniqueID = 1;

	const uint32 TypesUsageHash = FCrc::MemCrc32(LayoutTypes, sizeof(LayoutTypes));

	uint32* UniqueID = GTypesUsageHashMap.Find(TypesUsageHash);
	if (UniqueID == nullptr)
	{
		TypesUsageID = GTypesUsageHashMap.Add(TypesUsageHash, GUniqueID++);
	}
	else
	{
		TypesUsageID = *UniqueID;
	}
}
#endif

void FVulkanDescriptorSetsLayout::Compile()
{
	check(LayoutHandles.Num() == 0);

	// Check if we obey limits
	const VkPhysicalDeviceLimits& Limits = Device->GetLimits();
	
	// Check for maxDescriptorSetSamplers
	check(		LayoutTypes[VK_DESCRIPTOR_TYPE_SAMPLER]
			+	LayoutTypes[VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER]
			<	Limits.maxDescriptorSetSamplers);

	// Check for maxDescriptorSetUniformBuffers
	check(		LayoutTypes[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER]
			+	LayoutTypes[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC]
			<	Limits.maxDescriptorSetUniformBuffers);

	// Check for maxDescriptorSetUniformBuffersDynamic
	if (!IsRHIDeviceAMD())
	{
		check(LayoutTypes[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC]
			<	Limits.maxDescriptorSetUniformBuffersDynamic);
	}

	// Check for maxDescriptorSetStorageBuffers
	check(		LayoutTypes[VK_DESCRIPTOR_TYPE_STORAGE_BUFFER]
			+	LayoutTypes[VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC]
			<	Limits.maxDescriptorSetStorageBuffers);

	// Check for maxDescriptorSetStorageBuffersDynamic
	if (LayoutTypes[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC] > Limits.maxDescriptorSetUniformBuffersDynamic)
	{
		//#todo-rco: Downgrade to non-dynamic
	}
	check(		LayoutTypes[VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC]
			<	Limits.maxDescriptorSetStorageBuffersDynamic);

	// Check for maxDescriptorSetSampledImages
	check(LayoutTypes[VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER]
			+	LayoutTypes[VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE]
			+	LayoutTypes[VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER]
			<	Limits.maxDescriptorSetSampledImages);

	// Check for maxDescriptorSetStorageImages
	check(		LayoutTypes[VK_DESCRIPTOR_TYPE_STORAGE_IMAGE]
			+	LayoutTypes[VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER]
			<	Limits.maxDescriptorSetStorageImages);

	LayoutHandles.Empty(SetLayouts.Num());

	for (FSetLayout& Layout : SetLayouts)
	{
		VkDescriptorSetLayoutCreateInfo DescriptorLayoutInfo;
		ZeroVulkanStruct(DescriptorLayoutInfo, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
		DescriptorLayoutInfo.bindingCount = Layout.LayoutBindings.Num();
		DescriptorLayoutInfo.pBindings = Layout.LayoutBindings.GetData();

		VkDescriptorSetLayout* LayoutHandle = new(LayoutHandles) VkDescriptorSetLayout;
		VERIFYVULKANRESULT(VulkanRHI::vkCreateDescriptorSetLayout(Device->GetInstanceHandle(), &DescriptorLayoutInfo, nullptr, LayoutHandle));
	}

#if VULKAN_USE_DESCRIPTOR_POOL_MANAGER
	if (TypesUsageID == ~0)
	{
		CompileTypesUsageID();
	}

	DescriptorSetAllocateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	DescriptorSetAllocateInfo.descriptorSetCount = LayoutHandles.Num();
	DescriptorSetAllocateInfo.pSetLayouts = LayoutHandles.GetData();
#endif
}

#if !VULKAN_USE_DESCRIPTOR_POOL_MANAGER
FOLDVulkanDescriptorSets::FOLDVulkanDescriptorSets(FVulkanDevice* InDevice, const FVulkanDescriptorSetsLayout& InLayout, FVulkanCommandListContext* InContext)
	: Device(InDevice)
	, Pool(nullptr)
	, Layout(InLayout)
{
	const TArray<VkDescriptorSetLayout>& LayoutHandles = Layout.GetHandles();
	if (LayoutHandles.Num() > 0)
	{
		VkDescriptorSetAllocateInfo DescriptorSetAllocateInfo;
		ZeroVulkanStruct(DescriptorSetAllocateInfo, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
		// Pool will be filled in by FVulkanCommandListContext::AllocateDescriptorSets
		//DescriptorSetAllocateInfo.descriptorPool = Pool->GetHandle();
		DescriptorSetAllocateInfo.descriptorSetCount = LayoutHandles.Num();
		DescriptorSetAllocateInfo.pSetLayouts = LayoutHandles.GetData();

		Sets.AddZeroed(LayoutHandles.Num());

		Pool = InContext->AllocateDescriptorSets(DescriptorSetAllocateInfo, InLayout, Sets.GetData());
		Pool->TrackAddUsage(Layout);
	}

#if VULKAN_USE_DESCRIPTOR_POOL_MANAGER
	INC_DWORD_STAT_BY(STAT_VulkanNumDescSetsTotal, LayoutHandles.Num());
#endif
}

FOLDVulkanDescriptorSets::~FOLDVulkanDescriptorSets()
{
	Pool->TrackRemoveUsage(Layout);

	if (Sets.Num() > 0)
	{
		VERIFYVULKANRESULT(VulkanRHI::vkFreeDescriptorSets(Device->GetInstanceHandle(), Pool->GetHandle(), Sets.Num(), Sets.GetData()));
	}

#if VULKAN_USE_DESCRIPTOR_POOL_MANAGER
	DEC_DWORD_STAT_BY(STAT_VulkanNumDescSetsTotal, Sets.Num());
#endif
}
#endif

void FVulkanBufferView::Create(FVulkanBuffer& Buffer, EPixelFormat Format, uint32 InOffset, uint32 InSize)
{
	Offset = InOffset;
	Size = InSize;
	check(Format != PF_Unknown);
	const FPixelFormatInfo& FormatInfo = GPixelFormats[Format];
	check(FormatInfo.Supported);

	VkBufferViewCreateInfo ViewInfo;
	ZeroVulkanStruct(ViewInfo, VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO);
	ViewInfo.buffer = Buffer.GetBufferHandle();
	ViewInfo.format = (VkFormat)FormatInfo.PlatformFormat;
	ViewInfo.offset = Offset;
	ViewInfo.range = Size;
	Flags = Buffer.GetFlags() & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
	check(Flags);

	VERIFYVULKANRESULT(VulkanRHI::vkCreateBufferView(GetParent()->GetInstanceHandle(), &ViewInfo, nullptr, &View));
	INC_DWORD_STAT(STAT_VulkanNumBufferViews);
}

void FVulkanBufferView::Create(FVulkanResourceMultiBuffer* Buffer, EPixelFormat Format, uint32 InOffset, uint32 InSize)
{
	check(Format != PF_Unknown);
	const FPixelFormatInfo& FormatInfo = GPixelFormats[Format];
	check(FormatInfo.Supported);
	Create((VkFormat)FormatInfo.PlatformFormat, Buffer, InOffset, InSize);
}


void FVulkanBufferView::Create(VkFormat Format, FVulkanResourceMultiBuffer* Buffer, uint32 InOffset, uint32 InSize)
{
	Offset = InOffset;
	Size = InSize;
	check(Format != VK_FORMAT_UNDEFINED);

	VkBufferViewCreateInfo ViewInfo;
	ZeroVulkanStruct(ViewInfo, VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO);
	ViewInfo.buffer = Buffer->GetHandle();
	ViewInfo.format = Format;
	ViewInfo.offset = Offset;
	ViewInfo.range = Size;
	Flags = Buffer->GetBufferUsageFlags() & (VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT);
	check(Flags);

	VERIFYVULKANRESULT(VulkanRHI::vkCreateBufferView(GetParent()->GetInstanceHandle(), &ViewInfo, nullptr, &View));
	INC_DWORD_STAT(STAT_VulkanNumBufferViews);
}

void FVulkanBufferView::Destroy()
{
	if (View != VK_NULL_HANDLE)
	{
		DEC_DWORD_STAT(STAT_VulkanNumBufferViews);
		Device->GetDeferredDeletionQueue().EnqueueResource(FDeferredDeletionQueue::EType::BufferView, View);
		View = VK_NULL_HANDLE;
	}
}

FVulkanRenderPass::FVulkanRenderPass(FVulkanDevice& InDevice, const FVulkanRenderTargetLayout& InRTLayout) :
	Layout(InRTLayout),
	RenderPass(VK_NULL_HANDLE),
	NumUsedClearValues(InRTLayout.GetNumUsedClearValues()),
	Device(InDevice)
{
	INC_DWORD_STAT(STAT_VulkanNumRenderPasses);

	VkSubpassDescription SubpassDesc;
	FMemory::Memzero(SubpassDesc);
	SubpassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	//SubpassDesc.flags = 0;
	//SubpassDesc.inputAttachmentCount = 0;
	SubpassDesc.colorAttachmentCount = InRTLayout.GetNumColorAttachments();
	SubpassDesc.pColorAttachments = InRTLayout.GetColorAttachmentReferences();
	SubpassDesc.pResolveAttachments = InRTLayout.GetResolveAttachmentReferences();
	SubpassDesc.pDepthStencilAttachment = InRTLayout.GetDepthStencilAttachmentReference();
	//SubpassDesc.preserveAttachmentCount = 0;

	VkRenderPassCreateInfo CreateInfo;
	ZeroVulkanStruct(CreateInfo, VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
	CreateInfo.attachmentCount = InRTLayout.GetNumAttachmentDescriptions();
	CreateInfo.pAttachments = InRTLayout.GetAttachmentDescriptions();
	CreateInfo.subpassCount = 1;
	CreateInfo.pSubpasses = &SubpassDesc;
	//CreateInfo.dependencyCount = 0;

	VERIFYVULKANRESULT_EXPANDED(VulkanRHI::vkCreateRenderPass(Device.GetInstanceHandle(), &CreateInfo, nullptr, &RenderPass));
}

FVulkanRenderPass::~FVulkanRenderPass()
{
	DEC_DWORD_STAT(STAT_VulkanNumRenderPasses);

	Device.GetDeferredDeletionQueue().EnqueueResource(FDeferredDeletionQueue::EType::RenderPass, RenderPass);
	RenderPass = VK_NULL_HANDLE;
}


void VulkanSetImageLayout(
	VkCommandBuffer CmdBuffer,
	VkImage Image,
	VkImageLayout OldLayout,
	VkImageLayout NewLayout,
	const VkImageSubresourceRange& SubresourceRange)
{
	VkImageMemoryBarrier ImageBarrier;
	ZeroVulkanStruct(ImageBarrier, VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
	ImageBarrier.oldLayout = OldLayout;
	ImageBarrier.newLayout = NewLayout;
	ImageBarrier.image = Image;
	ImageBarrier.subresourceRange = SubresourceRange;
	ImageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	ImageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

	ImageBarrier.srcAccessMask = VulkanRHI::GetAccessMask(OldLayout);
	ImageBarrier.dstAccessMask = VulkanRHI::GetAccessMask(NewLayout);

	VkPipelineStageFlags SourceStages = VulkanRHI::GetStageFlags(OldLayout);
	VkPipelineStageFlags DestStages = VulkanRHI::GetStageFlags(NewLayout);

	VulkanRHI::vkCmdPipelineBarrier(CmdBuffer, SourceStages, DestStages, 0, 0, nullptr, 0, nullptr, 1, &ImageBarrier);
}

void VulkanResolveImage(VkCommandBuffer Cmd, FTextureRHIParamRef SourceTextureRHI, FTextureRHIParamRef DestTextureRHI)
{
	FVulkanTextureBase* Src = FVulkanTextureBase::Cast(SourceTextureRHI);
	FVulkanTextureBase* Dst = FVulkanTextureBase::Cast(DestTextureRHI);

	const VkImageAspectFlags AspectMask = Src->Surface.GetPartialAspectMask();
	check(AspectMask == Dst->Surface.GetPartialAspectMask());

	VkImageResolve ResolveDesc;
	FMemory::Memzero(ResolveDesc);
	ResolveDesc.srcSubresource.aspectMask = AspectMask;
	ResolveDesc.srcSubresource.baseArrayLayer = 0;
	ResolveDesc.srcSubresource.mipLevel = 0;
	ResolveDesc.srcSubresource.layerCount = 1;
	ResolveDesc.srcOffset.x = 0;
	ResolveDesc.srcOffset.y = 0;
	ResolveDesc.srcOffset.z = 0;
	ResolveDesc.dstSubresource.aspectMask = AspectMask;
	ResolveDesc.dstSubresource.baseArrayLayer = 0;
	ResolveDesc.dstSubresource.mipLevel = 0;
	ResolveDesc.dstSubresource.layerCount = 1;
	ResolveDesc.dstOffset.x = 0;
	ResolveDesc.dstOffset.y = 0;
	ResolveDesc.dstOffset.z = 0;
	ResolveDesc.extent.width = Src->Surface.Width;
	ResolveDesc.extent.height = Src->Surface.Height;
	ResolveDesc.extent.depth = 1;

	VulkanRHI::vkCmdResolveImage(Cmd,
		Src->Surface.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		Dst->Surface.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &ResolveDesc);
}

FVulkanRingBuffer::FVulkanRingBuffer(FVulkanDevice* InDevice, uint64 TotalSize, VkFlags Usage, VkMemoryPropertyFlags MemPropertyFlags)
	: VulkanRHI::FDeviceChild(InDevice)
	, BufferSize(TotalSize)
	, BufferOffset(0)
	, MinAlignment(0)
{
	FRHIResourceCreateInfo CreateInfo;
	BufferSuballocation = InDevice->GetResourceHeapManager().AllocateBuffer(TotalSize, Usage, MemPropertyFlags, __FILE__, __LINE__);
	MinAlignment = BufferSuballocation->GetBufferAllocation()->GetAlignment();

	// Start by wrapping around to set up the correct fence
	BufferOffset = TotalSize;
}

FVulkanRingBuffer::~FVulkanRingBuffer()
{
	delete BufferSuballocation;
}

uint64 FVulkanRingBuffer::AllocateMemory(uint64 Size, uint32 Alignment, FVulkanCmdBuffer* InCmdBuffer)
{
	CA_ASSUME(InCmdBuffer != nullptr); // Suppress static analysis warning
	Alignment = FMath::Max(Alignment, MinAlignment);
	uint64 AllocOffset = Align<uint64>(BufferOffset, Alignment);

	// wrap around if needed
	if (AllocOffset + Size >= BufferSize)
	{
		if (FenceCmdBuffer)
		{
			if (FenceCmdBuffer == InCmdBuffer && FenceCounter == InCmdBuffer->GetFenceSignaledCounter())
			{
				UE_LOG(LogVulkanRHI, Error, TEXT("Wrapped around the ring buffer. Requested more bytes than possible in the same cmd buffer!"));
			}
			else if (FenceCounter == FenceCmdBuffer->GetFenceSignaledCounter())
			{
				// Stall!
				UE_LOG(LogVulkanRHI, Error, TEXT("Wrapped around the ring buffer! Need to wait on the GPU!!!"));
			}
		}

		AllocOffset = 0;
		BufferOffset = Size;

		FenceCmdBuffer = InCmdBuffer;
		FenceCounter = InCmdBuffer->GetSubmittedFenceCounter();
	}
	else
	{
		// point to location after this guy
		BufferOffset = AllocOffset + Size;
	}

	return AllocOffset;
}

void FVulkanDynamicRHI::SavePipelineCache()
{
	FString CacheFile = GetPipelineCacheFilename();

	FVulkanDynamicRHI* RHI = (FVulkanDynamicRHI*)GDynamicRHI;
	RHI->Device->PipelineStateCache->Save(CacheFile);
}

void FVulkanDynamicRHI::RebuildPipelineCache()
{
	FVulkanDynamicRHI* RHI = (FVulkanDynamicRHI*)GDynamicRHI;
	RHI->Device->PipelineStateCache->RebuildCache();
}

#if VULKAN_SUPPORTS_VALIDATION_CACHE
void FVulkanDynamicRHI::SaveValidationCache()
{
	FVulkanDynamicRHI* RHI = (FVulkanDynamicRHI*)GDynamicRHI;
	VkValidationCacheEXT ValidationCache = RHI->Device->GetValidationCache();
	if (ValidationCache != VK_NULL_HANDLE)
	{
		VkDevice Device = RHI->Device->GetInstanceHandle();
		PFN_vkGetValidationCacheDataEXT vkGetValidationCacheData = (PFN_vkGetValidationCacheDataEXT)(void*)VulkanRHI::vkGetDeviceProcAddr(Device, "vkGetValidationCacheDataEXT");
		check(vkGetValidationCacheData);
		size_t CacheSize = 0;
		VkResult Result = vkGetValidationCacheData(Device, ValidationCache, &CacheSize, nullptr);
		if (Result == VK_SUCCESS)
		{
			if (CacheSize > 0)
			{
				TArray<uint8> Data;
				Data.AddUninitialized(CacheSize);
				Result = vkGetValidationCacheData(Device, ValidationCache, &CacheSize, Data.GetData());
				if (Result == VK_SUCCESS)
				{
					FString CacheFilename = GetValidationCacheFilename();
					if (FFileHelper::SaveArrayToFile(Data, *CacheFilename))
					{
						UE_LOG(LogVulkanRHI, Display, TEXT("Saved validation cache file '%s', %d bytes"), *CacheFilename, Data.Num());
					}
				}
				else
				{
					UE_LOG(LogVulkanRHI, Warning, TEXT("Failed to query Vulkan validation cache data, VkResult=%d"), Result);
				}
			}
		}
		else
		{
			UE_LOG(LogVulkanRHI, Warning, TEXT("Failed to query Vulkan validation cache size, VkResult=%d"), Result);
		}
	}
}
#endif

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
void FVulkanDynamicRHI::DumpMemory()
{
	FVulkanDynamicRHI* RHI = (FVulkanDynamicRHI*)GDynamicRHI;
	RHI->Device->GetMemoryManager().DumpMemory();
	RHI->Device->GetResourceHeapManager().DumpMemory();
	RHI->Device->GetStagingManager().DumpMemory();
}
#endif

void FVulkanDynamicRHI::RecreateSwapChain(void* NewNativeWindow)
{
	if (NewNativeWindow)
	{
		FlushRenderingCommands();
		FVulkanDynamicRHI* RHI = (FVulkanDynamicRHI*)GDynamicRHI;
		TArray<FVulkanViewport*> Viewports = RHI->Viewports;
		ENQUEUE_RENDER_COMMAND(VulkanRecreateSwapChain)(
			[Viewports, NewNativeWindow](FRHICommandListImmediate& RHICmdList)
			{
				for (auto& Viewport : Viewports)
				{
					Viewport->RecreateSwapchain(NewNativeWindow);
				}
			});
		FlushRenderingCommands();
	}
}

void FVulkanDynamicRHI::VulkanSetImageLayout( VkCommandBuffer CmdBuffer, VkImage Image, VkImageLayout OldLayout, VkImageLayout NewLayout, const VkImageSubresourceRange& SubresourceRange )
{
	::VulkanSetImageLayout( CmdBuffer, Image, OldLayout, NewLayout, SubresourceRange );
}

#undef LOCTEXT_NAMESPACE
