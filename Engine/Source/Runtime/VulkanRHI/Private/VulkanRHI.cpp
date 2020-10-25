// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanRHI.cpp: Vulkan device RHI implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "BuildSettings.h"
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
#include "VulkanLLM.h"
#include "Misc/EngineVersion.h"
#include "GlobalShader.h"
#include "RHIValidation.h"

static_assert(sizeof(VkStructureType) == sizeof(int32), "ZeroVulkanStruct() assumes VkStructureType is int32!");

extern RHI_API bool GUseTexture3DBulkDataRHI;

TAtomic<uint64> GVulkanBufferHandleIdCounter{ 0 };
TAtomic<uint64> GVulkanBufferViewHandleIdCounter{ 0 };
TAtomic<uint64> GVulkanImageViewHandleIdCounter{ 0 };
TAtomic<uint64> GVulkanSamplerHandleIdCounter{ 0 };
TAtomic<uint64> GVulkanDSetLayoutHandleIdCounter{ 0 };


FVulkanCommandBufferManager* GVulkanCommandBufferManager = nullptr;

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
	1,
	TEXT("0 to only use Render Thread\n")
	TEXT("1 to use ONE RHI Thread\n")
	TEXT("2 to use multiple RHI Thread\n")
);

bool GGPUCrashDebuggingEnabled = false;


extern TAutoConsoleVariable<int32> GRHIAllowAsyncComputeCvar;


#if VULKAN_HAS_VALIDATION_FEATURES
static inline TArray<VkValidationFeatureEnableEXT> GetValidationFeaturesEnabled()
{
	TArray<VkValidationFeatureEnableEXT> Features;
	Features.Add(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT);
	extern TAutoConsoleVariable<int32> GGPUValidationCvar;
	if (GGPUValidationCvar.GetValueOnAnyThread() > 1)
	{
		Features.Add(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT);
	}

	return Features;
}
#endif

DEFINE_LOG_CATEGORY(LogVulkan)

bool FVulkanDynamicRHIModule::IsSupported()
{
	return FVulkanPlatform::IsSupported();
}

FDynamicRHI* FVulkanDynamicRHIModule::CreateRHI(ERHIFeatureLevel::Type InRequestedFeatureLevel)
{
	FVulkanPlatform::SetupMaxRHIFeatureLevelAndShaderPlatform(InRequestedFeatureLevel);
	check(GMaxRHIFeatureLevel != ERHIFeatureLevel::Num);

	GVulkanRHI = new FVulkanDynamicRHI();
#if ENABLE_RHI_VALIDATION
	if (FParse::Param(FCommandLine::Get(), TEXT("RHIValidation")))
	{
		GValidationRHI = new FValidationRHI(GVulkanRHI);
	}
	else
	{
		check(!GValidationRHI);
	}

	return GValidationRHI ? (FDynamicRHI*)GValidationRHI : (FDynamicRHI*)GVulkanRHI;
#else
	return GVulkanRHI;
#endif
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

	// Create CommandBufferManager, contain all active buffers
	CommandBufferManager = new FVulkanCommandBufferManager(InDevice, this);
	GVulkanCommandBufferManager = CommandBufferManager;
	FrameTiming->Initialize();
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

	UniformBufferUploader = new FVulkanUniformBufferUploader(Device);

	GlobalUniformBuffers.AddZeroed(FUniformBufferStaticSlotRegistry::Get().GetSlotCount());
}

FVulkanCommandListContext::~FVulkanCommandListContext()
{
	if (FVulkanPlatform::SupportsTimestampRenderQueries())
	{
		FrameTiming->Release();
		delete FrameTiming;
		FrameTiming = nullptr;
	}

	check(CommandBufferManager != nullptr);
	delete CommandBufferManager;
	CommandBufferManager = nullptr;
	GVulkanCommandBufferManager = nullptr;

	TransitionAndLayoutManager.Destroy(*Device, Immediate ? &TransitionAndLayoutManager : nullptr);

	delete UniformBufferUploader;
	delete PendingGfxState;
	delete PendingComputeState;

	TempFrameAllocationBuffer.Destroy();
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
	GRHISupportsMultithreading = true;
	GConfig->GetInt(TEXT("TextureStreaming"), TEXT("PoolSizeVRAMPercentage"), GPoolSizeVRAMPercentage, GEngineIni);
}

void FVulkanDynamicRHI::Init()
{
	// Setup the validation requests ready before we load dlls
	SetupValidationRequests();

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
		GGPUCrashDebuggingEnabled = (GPUCrashDebuggingCVar && GPUCrashDebuggingCVar->GetInt() != 0) || FParse::Param(FCommandLine::Get(), TEXT("gpucrashdebugging"));
	}

	InitInstance();

#if VULKAN_USE_LLM
	LLM(VulkanLLM::Initialize());
#endif


	static const auto CVarStreamingTexturePoolSize = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Streaming.PoolSize"));
	int32 StreamingPoolSizeValue = CVarStreamingTexturePoolSize->GetValueOnAnyThread();
			
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
	else if (StreamingPoolSizeValue > 0)
	{
		GTexturePoolSize = (int64)StreamingPoolSizeValue * 1024 * 1024;

		const uint64 TotalGPUMemory = Device->GetMemoryManager().GetTotalMemory(true);
		UE_LOG(LogRHI,Log,TEXT("Texture pool is %llu MB (of %llu MB total graphics mem)"),
				GTexturePoolSize / 1024 / 1024,
				TotalGPUMemory / 1024 / 1024);
	}
}

void FVulkanDynamicRHI::PostInit()
{
	//work around layering violation
	TShaderMapRef<FNULLPS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)).GetPixelShader();
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

		FVulkanPlatform::OverridePlatformHandlers(false);

		GRHINeedsExtraDeletionLatency = false;

		check(!GIsCriticalError);

		// Ask all initialized FRenderResources to release their RHI resources.
		FRenderResource::ReleaseRHIForAllResources();

		{
			for (auto& Pair : Device->SamplerMap)
			{
				FVulkanSamplerState* SamplerState = (FVulkanSamplerState*)Pair.Value.GetReference();
				VulkanRHI::vkDestroySampler(Device->GetInstanceHandle(), SamplerState->Sampler, VULKAN_CPU_ALLOCATOR);
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

	VulkanRHI::vkDestroyInstance(Instance, VULKAN_CPU_ALLOCATOR);

	IConsoleManager::Get().UnregisterConsoleObject(SavePipelineCacheCmd);
	IConsoleManager::Get().UnregisterConsoleObject(RebuildPipelineCacheCmd);

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	IConsoleManager::Get().UnregisterConsoleObject(DumpMemoryCmd);
	IConsoleManager::Get().UnregisterConsoleObject(DumpLRUCmd);
	IConsoleManager::Get().UnregisterConsoleObject(TrimLRUCmd);
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

	// EngineName will be of the form "UnrealEngine4.21", with the minor version ("21" in this example)
	// updated with every quarterly release
	FString EngineName = FApp::GetEpicProductIdentifier() + FEngineVersion::Current().ToString(EVersionComponent::Minor);
	FTCHARToUTF8 EngineNameConverter(*EngineName);
	FTCHARToUTF8 ProjectNameConverter(FApp::GetProjectName());

	VkApplicationInfo AppInfo;
	ZeroVulkanStruct(AppInfo, VK_STRUCTURE_TYPE_APPLICATION_INFO);
	AppInfo.pApplicationName = bDisableEngineRegistration ? nullptr : ProjectNameConverter.Get();
	AppInfo.applicationVersion = static_cast<uint32>(BuildSettings::GetCurrentChangelist()) | (BuildSettings::IsLicenseeVersion() ? 0x80000000 : 0);
	AppInfo.pEngineName = bDisableEngineRegistration ? nullptr : EngineNameConverter.Get();
	AppInfo.engineVersion = FEngineVersion::Current().GetMinor();
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

#if VULKAN_HAS_VALIDATION_FEATURES
	bool bHasGPUValidation = InstanceExtensions.ContainsByPredicate([](const ANSICHAR* Key)
		{
			return Key && !FCStringAnsi::Strcmp(Key, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
		});
	VkValidationFeaturesEXT ValidationFeatures;
	TArray<VkValidationFeatureEnableEXT> ValidationFeaturesEnabled = GetValidationFeaturesEnabled();
	if (bHasGPUValidation)
	{
		ZeroVulkanStruct(ValidationFeatures, VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT);
		ValidationFeatures.pNext = InstInfo.pNext;
		ValidationFeatures.enabledValidationFeatureCount = (uint32)ValidationFeaturesEnabled.Num();
		ValidationFeatures.pEnabledValidationFeatures = ValidationFeaturesEnabled.GetData();
		InstInfo.pNext = &ValidationFeatures;
	}
#endif
#endif

	VkResult Result = VulkanRHI::vkCreateInstance(&InstInfo, VULKAN_CPU_ALLOCATOR, &Instance);
	
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
			"Vulkan failed to create instance (apiVersion=0x%x)\n\nDo you have a compatible Vulkan "
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

	OptionalInstanceExtensions.Setup(InstanceExtensions);
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
	VkResult Result = VulkanRHI::vkEnumeratePhysicalDevices(Instance, &GpuCount, nullptr);
	if (Result == VK_ERROR_INITIALIZATION_FAILED)
	{
		FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, TEXT("Cannot find a compatible Vulkan device or driver. Try updating your video driver to a more recent version and make sure your video card supports Vulkan.\n\n"), TEXT("Vulkan device not available"));
		FPlatformMisc::RequestExitWithStatus(true, 1);
	}
	VERIFYVULKANRESULT_EXPANDED(Result);
	checkf(GpuCount >= 1, TEXT("No GPU(s)/Driver(s) that support Vulkan were found! Make sure your drivers are up to date and that you are not pending a reboot."));

	TArray<VkPhysicalDevice> PhysicalDevices;
	PhysicalDevices.AddZeroed(GpuCount);
	VERIFYVULKANRESULT_EXPANDED(VulkanRHI::vkEnumeratePhysicalDevices(Instance, &GpuCount, PhysicalDevices.GetData()));
	checkf(GpuCount >= 1, TEXT("Couldn't enumerate physical devices! Make sure your drivers are up to date and that you are not pending a reboot."));

	FVulkanDevice* HmdDevice = nullptr;
	uint32 HmdDeviceIndex = 0;
	struct FDeviceInfo
	{
		FVulkanDevice* Device;
		uint32 DeviceIndex;
	};
	TArray<FDeviceInfo> DiscreteDevices;
	TArray<FDeviceInfo> IntegratedDevices;

	TArray<FDeviceInfo> OriginalOrderedDevices;

#if VULKAN_ENABLE_DESKTOP_HMD_SUPPORT
	// Allow HMD to override which graphics adapter is chosen, so we pick the adapter where the HMD is connected
	uint64 HmdGraphicsAdapterLuid  = IHeadMountedDisplayModule::IsAvailable() ? IHeadMountedDisplayModule::Get().GetGraphicsAdapterLuid() : 0;
#endif

	UE_LOG(LogVulkanRHI, Display, TEXT("Found %d device(s)"), GpuCount);
	for (uint32 Index = 0; Index < GpuCount; ++Index)
	{
		FVulkanDevice* NewDevice = new FVulkanDevice(this, PhysicalDevices[Index]);
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
		else
		{
			IntegratedDevices.Add({NewDevice, Index});
		}

		OriginalOrderedDevices.Add({NewDevice, Index});
	}

	uint32 DeviceIndex = -1;
#if VULKAN_ENABLE_DESKTOP_HMD_SUPPORT
	if (HmdDevice)
	{
		Device = HmdDevice;
		DeviceIndex = HmdDeviceIndex;
	}
#endif

	// Add all integrated to the end of the list
	DiscreteDevices.Append(IntegratedDevices);

	// Non-static as it is used only a few times
	auto* CVarGraphicsAdapter = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.GraphicsAdapter"));
	int32 CVarExplicitAdapterValue = CVarGraphicsAdapter ? CVarGraphicsAdapter->GetValueOnAnyThread() : -1;
	FParse::Value(FCommandLine::Get(), TEXT("graphicsadapter="), CVarExplicitAdapterValue);

	// If HMD didn't choose one... (disable static analysis that DeviceIndex is always -1)
	if (DeviceIndex == -1)	//-V547
	{
		if (CVarExplicitAdapterValue >= (int32)GpuCount)
		{
			UE_LOG(LogVulkanRHI, Warning, TEXT("Tried to use r.GraphicsAdapter=%d, but only %d Adapter(s) found. Falling back to first device..."), CVarExplicitAdapterValue, GpuCount);
			CVarExplicitAdapterValue = 0;
		}
		
		if (CVarExplicitAdapterValue >= 0)
		{
			DeviceIndex = OriginalOrderedDevices[CVarExplicitAdapterValue].DeviceIndex;
			Device = OriginalOrderedDevices[CVarExplicitAdapterValue].Device;
		}
		else
		{
			if (CVarExplicitAdapterValue == -2)
			{
				DeviceIndex = OriginalOrderedDevices[0].DeviceIndex;
				Device = OriginalOrderedDevices[0].Device;
			}
			else if (DiscreteDevices.Num() > 0 && CVarExplicitAdapterValue == -1)
			{
				int32 PreferredVendor = PreferAdapterVendor();
				if (DiscreteDevices.Num() > 1 && PreferredVendor != -1)
				{
					// Check for preferred
					for (int32 Index = 0; Index < DiscreteDevices.Num(); ++Index)
					{
						if (DiscreteDevices[Index].Device->GpuProps.vendorID == PreferredVendor)
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
				checkf(0, TEXT("No devices found!"));
				DeviceIndex = 0;
			}
		}
	}

	const VkPhysicalDeviceProperties& Props = Device->GetDeviceProperties();
	GRHIVendorId = Props.vendorID;
	GRHIAdapterName = ANSI_TO_TCHAR(Props.deviceName);

	FVulkanPlatform::CheckDeviceDriver(DeviceIndex, Device->GetVendorId(), Props);

	Device->InitGPU(DeviceIndex);

	if (PLATFORM_ANDROID && !PLATFORM_LUMIN)
	{
		GRHIAdapterName.Append(TEXT(" Vulkan"));
		GRHIAdapterInternalDriverVersion = FString::Printf(TEXT("%d.%d.%d"), VK_VERSION_MAJOR(Props.apiVersion), VK_VERSION_MINOR(Props.apiVersion), VK_VERSION_PATCH(Props.apiVersion));
	}
	else if (Device->GetVendorId() == EGpuVendorId::Nvidia)
	{
		UNvidiaDriverVersion NvidiaVersion;
		static_assert(sizeof(NvidiaVersion) == sizeof(Props.driverVersion), "Mismatched Nvidia pack driver version!");
		NvidiaVersion.Packed = Props.driverVersion;
		GRHIAdapterUserDriverVersion = FString::Printf(TEXT("%d.%d"), NvidiaVersion.Major, NvidiaVersion.Minor);
		UE_LOG(LogVulkanRHI, Display, TEXT("Nvidia User Driver Version = %s"), *GRHIAdapterUserDriverVersion);

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

		FVulkanPlatform::OverridePlatformHandlers(true);

		GRHISupportsAsyncTextureCreation = false;
		GEnableAsyncCompute = false;

		CreateInstance();
		SelectAndInitDevice();

#if VULKAN_HAS_DEBUGGING_ENABLED
		if (GRenderDocFound)
		{
			EnableIdealGPUCaptureOptions(true);
		}
#endif
		//bool bDeviceSupportsTessellation = Device->GetPhysicalFeatures().tessellationShader != 0;

		const VkPhysicalDeviceProperties& Props = Device->GetDeviceProperties();

		// Initialize the RHI capabilities.
		GRHISupportsFirstInstance = true;
		GRHISupportsDynamicResolution = FVulkanPlatform::SupportsDynamicResolution();
		GSupportsDepthBoundsTest = Device->GetPhysicalFeatures().depthBounds != 0;
		GSupportsRenderTargetFormat_PF_G8 = false;	// #todo-rco
		GRHISupportsTextureStreaming = true;
		GSupportsTimestampRenderQueries = FVulkanPlatform::SupportsTimestampRenderQueries();
#if VULKAN_ENABLE_DUMP_LAYER
		// Disable RHI thread by default if the dump layer is enabled
		GRHISupportsRHIThread = false;
		GRHISupportsParallelRHIExecute = false;
#else
		GRHISupportsRHIThread = GRHIThreadCvar->GetInt() != 0;
		GRHISupportsParallelRHIExecute = GRHIThreadCvar->GetInt() > 1;
#endif
		// Some platforms might only have CPU for an RHI thread, but not for parallel tasks
		GSupportsParallelRenderingTasksWithSeparateRHIThread = GRHISupportsRHIThread ? FVulkanPlatform::SupportParallelRenderingTasks() : false;

		//#todo-rco: Add newer Nvidia also
		GSupportsEfficientAsyncCompute = (Device->GetVendorId() == EGpuVendorId::Amd) && (GRHIAllowAsyncComputeCvar.GetValueOnAnyThread() > 0) && (Device->ComputeContext != Device->ImmediateContext);

		GSupportsVolumeTextureRendering = FVulkanPlatform::SupportsVolumeTextureRendering();

		// Indicate that the RHI needs to use the engine's deferred deletion queue.
		GRHINeedsExtraDeletionLatency = true;

		GRHISupportsCopyToTextureMultipleMips = true;

		GMaxShadowDepthBufferSizeX =  FPlatformMath::Min<int32>(Props.limits.maxImageDimension2D, GMaxShadowDepthBufferSizeX);
		GMaxShadowDepthBufferSizeY =  FPlatformMath::Min<int32>(Props.limits.maxImageDimension2D, GMaxShadowDepthBufferSizeY);
		GMaxTextureDimensions = Props.limits.maxImageDimension2D;
		GMaxBufferDimensions = Props.limits.maxTexelBufferElements;
		GMaxTextureMipCount = FPlatformMath::CeilLogTwo( GMaxTextureDimensions ) + 1;
		GMaxTextureMipCount = FPlatformMath::Min<int32>( MAX_TEXTURE_MIP_COUNT, GMaxTextureMipCount );
		GMaxCubeTextureDimensions = Props.limits.maxImageDimensionCube;
		GMaxTextureArrayLayers = Props.limits.maxImageArrayLayers;
		GRHISupportsBaseVertexIndex = true;
		GSupportsSeparateRenderTargetBlendState = true;

		FVulkanPlatform::SetupFeatureLevels();

		GRHIRequiresRenderTargetForPixelShaderUAVs = true;

		GUseTexture3DBulkDataRHI = false;

		FHardwareInfo::RegisterHardwareInfo(NAME_RHI, TEXT("Vulkan"));

		GProjectionSignY = 1.0f;

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
		DumpLRUCmd = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("r.Vulkan.DumpPSOLRU"),
			TEXT("Dumps Vulkan PSO LRU."),
			FConsoleCommandDelegate::CreateStatic(DumpLRU),
			ECVF_Default
		);
		TrimLRUCmd = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("r.Vulkan.TrimPSOLRU"),
			TEXT("Trim Vulkan PSO LRU."),
			FConsoleCommandDelegate::CreateStatic(TrimLRU),
			ECVF_Default
		);

#endif

		GRHICommandList.GetImmediateCommandList().SetContext(RHIGetDefaultContext());
		GRHICommandList.GetImmediateAsyncComputeCommandList().SetComputeContext(RHIGetDefaultAsyncComputeContext());
		FRenderResource::InitPreRHIResources();
		GIsRHIInitialized = true;
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

void FVulkanCommandListContext::RHIBeginDrawingViewport(FRHIViewport* ViewportRHI, FRHITexture* RenderTargetRHI)
{
	//FRCLog::Printf(FString::Printf(TEXT("FVulkanCommandListContext::RHIBeginDrawingViewport\n")));
	check(ViewportRHI);
	FVulkanViewport* Viewport = ResourceCast(ViewportRHI);
	RHI->DrawingViewport = Viewport;
}

void FVulkanCommandListContext::RHIEndDrawingViewport(FRHIViewport* ViewportRHI, bool bPresent, bool bLockToVsync)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanMisc);
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
	
	if (UseVulkanDescriptorCache())
	{
		Device->GetDescriptorSetCache().GC();
	}
	else
	{
		Device->GetDescriptorPoolsManager().GC();
	}

	Device->ReleaseUnusedOcclusionQueryPools();

	++FrameCounter;
}

void FVulkanCommandListContext::RHIPushEvent(const TCHAR* Name, FColor Color)
{
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

#if VULKAN_SUPPORTS_GPU_CRASH_DUMPS
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

#if VULKAN_SUPPORTS_GPU_CRASH_DUMPS
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

void* FVulkanDynamicRHI::RHIGetNativeInstance()
{
	return (void*)GetInstance();
}

IRHICommandContext* FVulkanDynamicRHI::RHIGetDefaultContext()
{
	return &Device->GetImmediateContext();
}

IRHIComputeContext* FVulkanDynamicRHI::RHIGetDefaultAsyncComputeContext()
{
	return &Device->GetImmediateComputeContext();
}

uint64 FVulkanDynamicRHI::RHIGetMinimumAlignmentForBufferBackedSRV(EPixelFormat Format)
{
	const VkPhysicalDeviceLimits& Limits = Device->GetLimits();
	return Limits.minTexelBufferOffsetAlignment;
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

FTexture2DRHIRef FVulkanDynamicRHI::RHICreateTexture2DFromResource(EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 NumMips, uint32 NumSamples, VkImage Resource, uint32 Flags)
{
	const FRHIResourceCreateInfo ResourceCreateInfo(IsDepthOrStencilFormat(Format) ? FClearValueBinding::DepthZero : FClearValueBinding::Transparent);
	return new FVulkanTexture2D(*Device, Format, SizeX, SizeY, NumMips, NumSamples, Resource, Flags, ResourceCreateInfo);
}

FTexture2DRHIRef FVulkanDynamicRHI::RHICreateTexture2DFromResource(EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 NumMips, uint32 NumSamples, VkImage Resource, FSamplerYcbcrConversionInitializer& ConversionInitializer, uint32 Flags)
{
	const FRHIResourceCreateInfo ResourceCreateInfo(IsDepthOrStencilFormat(Format) ? FClearValueBinding::DepthZero : FClearValueBinding::Transparent);
	return new FVulkanTexture2D(*Device, Format, SizeX, SizeY, NumMips, NumSamples, Resource, ConversionInitializer, Flags, ResourceCreateInfo);
}

FTexture2DArrayRHIRef FVulkanDynamicRHI::RHICreateTexture2DArrayFromResource(EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 ArraySize, uint32 NumMips, uint32 NumSamples, VkImage Resource, uint32 Flags)
{
	const FClearValueBinding ClearValueBinding(IsDepthOrStencilFormat(Format) ? FClearValueBinding::DepthZero : FClearValueBinding::Transparent);
	return new FVulkanTexture2DArray(*Device, Format, SizeX, SizeY, ArraySize, NumMips, NumSamples, Resource, Flags, nullptr, ClearValueBinding);
}

FTextureCubeRHIRef FVulkanDynamicRHI::RHICreateTextureCubeFromResource(EPixelFormat Format, uint32 Size, bool bArray, uint32 ArraySize, uint32 NumMips, VkImage Resource, uint32 Flags)
{
	const FClearValueBinding ClearValueBinding(IsDepthOrStencilFormat(Format) ? FClearValueBinding::DepthZero : FClearValueBinding::Transparent);
	return new FVulkanTextureCube(*Device, Format, Size, bArray, ArraySize, NumMips, Resource, Flags, nullptr, ClearValueBinding);
}

void FVulkanDynamicRHI::RHIAliasTextureResources(FRHITexture* DestTextureRHI, FRHITexture* SrcTextureRHI)
{
	check(false);
}

FTextureRHIRef FVulkanDynamicRHI::RHICreateAliasedTexture(FRHITexture* SourceTexture)
{
	check(false);
	return nullptr;
}

void FVulkanDynamicRHI::RHIAliasTextureResources(FTextureRHIRef& DestTextureRHI, FTextureRHIRef& SrcTextureRHI)
{
	if (DestTextureRHI && SrcTextureRHI)
	{
		FVulkanTextureBase* DestTextureBase = (FVulkanTextureBase*)DestTextureRHI->GetTextureBaseRHI();
		FVulkanTextureBase* SrcTextureBase = (FVulkanTextureBase*)SrcTextureRHI->GetTextureBaseRHI();

		if (DestTextureBase && SrcTextureBase)
		{
			DestTextureBase->AliasTextureResources(SrcTextureRHI);
		}
	}
}

FTextureRHIRef FVulkanDynamicRHI::RHICreateAliasedTexture(FTextureRHIRef& SourceTextureRHI)
{
	FVulkanTextureBase* SourceTexture = (FVulkanTextureBase*)SourceTextureRHI->GetTextureBaseRHI();
	FTextureRHIRef AliasedTexture;
	if (SourceTextureRHI->GetTexture2D() != nullptr)
	{
		AliasedTexture = new FVulkanTexture2D(SourceTextureRHI, (FVulkanTexture2D*)SourceTexture);
	}
	else if (SourceTextureRHI->GetTexture2DArray() != nullptr)
	{
		AliasedTexture = new FVulkanTexture2DArray(SourceTextureRHI, (FVulkanTexture2DArray*)SourceTexture);
	}
	else if (SourceTextureRHI->GetTextureCube() != nullptr)
	{
		AliasedTexture = new FVulkanTextureCube(SourceTextureRHI, (FVulkanTextureCube*)SourceTexture);
	}
	else
	{
		UE_LOG(LogRHI, Error, TEXT("Currently FVulkanDynamicRHI::RHICreateAliasedTexture only supports 2D, 2D Array and Cube textures."));
	}

	return AliasedTexture;
}

void FVulkanDynamicRHI::RHICopySubTextureRegion(FRHITexture2D* SourceTexture, FRHITexture2D* DestinationTexture, FBox2D SourceBox, FBox2D DestinationBox)
{
	FRHICopyTextureInfo CopyInfo;

	CopyInfo.Size.X = (int32)(SourceBox.Max.X - SourceBox.Min.X);
	CopyInfo.Size.Y = (int32)(SourceBox.Max.Y - SourceBox.Min.Y);

	CopyInfo.SourcePosition.X = (int32)(SourceBox.Min.X);
	CopyInfo.SourcePosition.Y = (int32)(SourceBox.Min.Y);
	CopyInfo.DestPosition.X = (int32)(DestinationBox.Min.X);
	CopyInfo.DestPosition.Y = (int32)(DestinationBox.Min.Y);

	RHIGetDefaultContext()->RHICopyTexture(SourceTexture, DestinationTexture, CopyInfo);
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
	VERIFYVULKANRESULT_EXPANDED(VulkanRHI::vkCreateBuffer(Device.GetInstanceHandle(), &BufInfo, VULKAN_CPU_ALLOCATOR, &Buf));

	VkMemoryRequirements MemoryRequirements;
	VulkanRHI::vkGetBufferMemoryRequirements(Device.GetInstanceHandle(), Buf, &MemoryRequirements);

	Allocation = InDevice.GetMemoryManager().Alloc(false, MemoryRequirements.size, MemoryRequirements.memoryTypeBits, InMemPropertyFlags, nullptr, VULKAN_MEMORY_MEDIUM_PRIORITY, File ? File : __FILE__, Line ? Line : __LINE__);
	check(Allocation);
	VERIFYVULKANRESULT_EXPANDED(VulkanRHI::vkBindBufferMemory(Device.GetInstanceHandle(), Buf, Allocation->GetHandle(), 0));
}

FVulkanBuffer::~FVulkanBuffer()
{
	// The buffer should be unmapped
	check(BufferPtr == nullptr);

	Device.GetDeferredDeletionQueue().EnqueueResource(FDeferredDeletionQueue2::EType::Buffer, Buf);
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
	// Handles are owned by FVulkanPipelineStateCacheManager
	LayoutHandles.Reset(0);
}

void FVulkanDescriptorSetsLayoutInfo::AddDescriptor(int32 DescriptorSetIndex, const VkDescriptorSetLayoutBinding& Descriptor)
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

	const FDescriptorSetRemappingInfo::FSetInfo& SetInfo = RemappingInfo.SetInfos[DescriptorSetIndex];
	check(SetInfo.Types[Descriptor.binding] == Descriptor.descriptorType);
	switch (Descriptor.descriptorType)
	{
	case VK_DESCRIPTOR_TYPE_SAMPLER:
	case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
	case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
	case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
	case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
		++RemappingInfo.SetInfos[DescriptorSetIndex].NumImageInfos;
		break;
	case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
	case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
	case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
		++RemappingInfo.SetInfos[DescriptorSetIndex].NumBufferInfos;
		break;
	case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
	case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
		break;
	default:
		checkf(0, TEXT("Unsupported descriptor type %d"), (int32)Descriptor.descriptorType);
		break;
	}
}

void FVulkanDescriptorSetsLayoutInfo::GenerateHash(const TArrayView<FRHISamplerState*>& InImmutableSamplers)
{
	const int32 LayoutCount = SetLayouts.Num();
	Hash = FCrc::MemCrc32(&TypesUsageID, sizeof(uint32), LayoutCount);

	for (int32 layoutIndex = 0; layoutIndex < LayoutCount; ++layoutIndex)
	{
		SetLayouts[layoutIndex].GenerateHash();
		Hash = FCrc::MemCrc32(&SetLayouts[layoutIndex].Hash, sizeof(uint32), Hash);
	}

	for (uint32 RemapingIndex = 0; RemapingIndex < ShaderStage::NumStages; ++RemapingIndex)
	{
		Hash = FCrc::MemCrc32(&RemappingInfo.StageInfos[RemapingIndex].PackedUBDescriptorSet, sizeof(uint16), Hash);
		Hash = FCrc::MemCrc32(&RemappingInfo.StageInfos[RemapingIndex].Pad0, sizeof(uint16), Hash);

		TArray<FDescriptorSetRemappingInfo::FRemappingInfo>& Globals = RemappingInfo.StageInfos[RemapingIndex].Globals;
		Hash = FCrc::MemCrc32(Globals.GetData(), sizeof(FDescriptorSetRemappingInfo::FRemappingInfo) * Globals.Num(), Hash);

		TArray<FDescriptorSetRemappingInfo::FUBRemappingInfo>& UniformBuffers = RemappingInfo.StageInfos[RemapingIndex].UniformBuffers;
		Hash = FCrc::MemCrc32(UniformBuffers.GetData(), sizeof(FDescriptorSetRemappingInfo::FUBRemappingInfo) * UniformBuffers.Num(), Hash);

		TArray<uint16>& PackedUBBindingIndices = RemappingInfo.StageInfos[RemapingIndex].PackedUBBindingIndices;
		Hash = FCrc::MemCrc32(PackedUBBindingIndices.GetData(), sizeof(uint16) * PackedUBBindingIndices.Num(), Hash);
	}

#if VULKAN_SUPPORTS_COLOR_CONVERSIONS
	VkSampler ImmutableSamplers[MaxImmutableSamplers];
	VkSampler* ImmutableSamplerPtr = ImmutableSamplers;
	for (int32 Index = 0; Index < InImmutableSamplers.Num(); ++Index)
	{
		FRHISamplerState* SamplerState = InImmutableSamplers[Index];
		*ImmutableSamplerPtr++ = SamplerState ? ResourceCast(SamplerState)->Sampler : VK_NULL_HANDLE;
	}
	FMemory::Memzero(ImmutableSamplerPtr, (MaxImmutableSamplers - InImmutableSamplers.Num()));
	Hash = FCrc::MemCrc32(ImmutableSamplers, sizeof(VkSampler) * MaxImmutableSamplers, Hash);
#endif
}

static FCriticalSection GTypesUsageCS;
void FVulkanDescriptorSetsLayoutInfo::CompileTypesUsageID()
{
	FScopeLock ScopeLock(&GTypesUsageCS);

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

void FVulkanDescriptorSetsLayout::Compile(FVulkanDescriptorSetLayoutMap& DSetLayoutMap)
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
	check(Device->GetVendorId() == EGpuVendorId::Amd ||
				LayoutTypes[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC]
			<	Limits.maxDescriptorSetUniformBuffersDynamic);

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
	check(		LayoutTypes[VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER]
			+	LayoutTypes[VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE]
			+	LayoutTypes[VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER]
			<	Limits.maxDescriptorSetSampledImages);

	// Check for maxDescriptorSetStorageImages
	check(		LayoutTypes[VK_DESCRIPTOR_TYPE_STORAGE_IMAGE]
			+	LayoutTypes[VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER]
			<	Limits.maxDescriptorSetStorageImages);

	check(LayoutTypes[VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT] < Limits.maxDescriptorSetInputAttachments);
	
	LayoutHandles.Empty(SetLayouts.Num());

	if (UseVulkanDescriptorCache())
	{
		LayoutHandleIds.Empty(SetLayouts.Num());
	}
				
	for (FSetLayout& Layout : SetLayouts)
	{
		VkDescriptorSetLayout* LayoutHandle = new(LayoutHandles) VkDescriptorSetLayout;

		uint32* LayoutHandleId = nullptr;
		if (UseVulkanDescriptorCache())
		{
			LayoutHandleId = new(LayoutHandleIds) uint32;
		}
			
		if (FVulkanDescriptorSetLayoutEntry* Found = DSetLayoutMap.Find(Layout))
		{
			*LayoutHandle = Found->Handle;
			if (LayoutHandleId)
			{
				*LayoutHandleId = Found->HandleId;
			}
			continue;
		}

		VkDescriptorSetLayoutCreateInfo DescriptorLayoutInfo;
		ZeroVulkanStruct(DescriptorLayoutInfo, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
		DescriptorLayoutInfo.bindingCount = Layout.LayoutBindings.Num();
		DescriptorLayoutInfo.pBindings = Layout.LayoutBindings.GetData();

		VERIFYVULKANRESULT(VulkanRHI::vkCreateDescriptorSetLayout(Device->GetInstanceHandle(), &DescriptorLayoutInfo, VULKAN_CPU_ALLOCATOR, LayoutHandle));

		if (LayoutHandleId)
		{
			*LayoutHandleId = ++GVulkanDSetLayoutHandleIdCounter;
		}

		FVulkanDescriptorSetLayoutEntry DescriptorSetLayoutEntry;
		DescriptorSetLayoutEntry.Handle = *LayoutHandle;
		DescriptorSetLayoutEntry.HandleId = LayoutHandleId ? *LayoutHandleId : 0;
				
		DSetLayoutMap.Add(Layout, DescriptorSetLayoutEntry);
	}

	if (TypesUsageID == ~0)
	{
		CompileTypesUsageID();
	}

	ZeroVulkanStruct(DescriptorSetAllocateInfo, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
	DescriptorSetAllocateInfo.descriptorSetCount = LayoutHandles.Num();
	DescriptorSetAllocateInfo.pSetLayouts = LayoutHandles.GetData();
}


void FVulkanBufferView::Create(FVulkanBuffer& Buffer, EPixelFormat Format, uint32 InOffset, uint32 InSize)
{
	Offset = InOffset;
	Size = InSize;
	check(Format != PF_Unknown);
	VkFormat BufferFormat = GVulkanBufferFormat[Format];
	check(BufferFormat != VK_FORMAT_UNDEFINED);

	VkBufferViewCreateInfo ViewInfo;
	ZeroVulkanStruct(ViewInfo, VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO);
	ViewInfo.buffer = Buffer.GetBufferHandle();
	ViewInfo.format = BufferFormat;
	ViewInfo.offset = Offset;
	ViewInfo.range = Size;
	Flags = Buffer.GetFlags() & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
	check(Flags);

	VERIFYVULKANRESULT(VulkanRHI::vkCreateBufferView(GetParent()->GetInstanceHandle(), &ViewInfo, VULKAN_CPU_ALLOCATOR, &View));
	
	if (UseVulkanDescriptorCache())
	{
		ViewId = ++GVulkanBufferViewHandleIdCounter;
	}

	INC_DWORD_STAT(STAT_VulkanNumBufferViews);
}

void FVulkanBufferView::Create(FVulkanResourceMultiBuffer* Buffer, EPixelFormat Format, uint32 InOffset, uint32 InSize)
{
	check(Format != PF_Unknown);
	VkFormat BufferFormat = GVulkanBufferFormat[Format];
	check(BufferFormat != VK_FORMAT_UNDEFINED);
	Create(BufferFormat, Buffer, InOffset, InSize);
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

	//#todo-rco: Revisit this if buffer views become VK_BUFFER_USAGE_STORAGE_BUFFER_BIT instead of VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT
	const VkPhysicalDeviceLimits& Limits = Device->GetLimits();
	const uint64 MaxSize = (uint64)Limits.maxTexelBufferElements * GetNumBitsPerPixel(Format) / 8;
	ViewInfo.range = FMath::Min<uint64>(Size, MaxSize);
	// TODO: add a check() for exceeding MaxSize, to catch code which blindly makes views without checking the platform limits.

	Flags = Buffer->GetBufferUsageFlags() & (VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT);
	check(Flags);

	VERIFYVULKANRESULT(VulkanRHI::vkCreateBufferView(GetParent()->GetInstanceHandle(), &ViewInfo, VULKAN_CPU_ALLOCATOR, &View));
	
	if (UseVulkanDescriptorCache())
	{
		ViewId = ++GVulkanBufferViewHandleIdCounter;
	}

	INC_DWORD_STAT(STAT_VulkanNumBufferViews);
}

void FVulkanBufferView::Destroy()
{
	if (View != VK_NULL_HANDLE)
	{
		DEC_DWORD_STAT(STAT_VulkanNumBufferViews);
		Device->GetDeferredDeletionQueue().EnqueueResource(FDeferredDeletionQueue2::EType::BufferView, View);
		View = VK_NULL_HANDLE;
		ViewId = 0;
	}
}

static VkRenderPass CreateRenderPass(FVulkanDevice& InDevice, const FVulkanRenderTargetLayout& RTLayout)
{
	VkRenderPassCreateInfo CreateInfo;
	ZeroVulkanStruct(CreateInfo, VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
	
	uint32 NumSubpasses = 0;
	uint32 NumDependencies = 0;

	VkSubpassDescription SubpassDescriptions[2];
	VkSubpassDependency SubpassDependencies[2];

	const bool bHasDepthReadSubpass = RTLayout.GetSubpassHint() == ESubpassHint::DepthReadSubpass;
		
	// main sub-pass
	{
		VkSubpassDescription& SubpassDesc = SubpassDescriptions[NumSubpasses++];
		FMemory::Memzero(&SubpassDesc, sizeof(VkSubpassDescription));

		SubpassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		SubpassDesc.colorAttachmentCount = RTLayout.GetNumColorAttachments();
		SubpassDesc.pColorAttachments = RTLayout.GetColorAttachmentReferences();
		SubpassDesc.pResolveAttachments = bHasDepthReadSubpass ? nullptr : RTLayout.GetResolveAttachmentReferences();
		SubpassDesc.pDepthStencilAttachment = RTLayout.GetDepthStencilAttachmentReference();
	}

	// depth read sub-pass
	VkAttachmentReference InputAttachments[MaxSimultaneousRenderTargets + 1];
	if (bHasDepthReadSubpass)
	{
		VkSubpassDescription& SubpassDesc = SubpassDescriptions[NumSubpasses++];
		FMemory::Memzero(&SubpassDesc, sizeof(VkSubpassDescription));

		SubpassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		SubpassDesc.colorAttachmentCount = RTLayout.GetNumColorAttachments();
		SubpassDesc.pColorAttachments = RTLayout.GetColorAttachmentReferences();
		SubpassDesc.pResolveAttachments = RTLayout.GetResolveAttachmentReferences();

		check(RTLayout.GetDepthStencilAttachmentReference());

		uint32 NumInputAttachments = 1;
		InputAttachments[0].attachment = RTLayout.GetDepthStencilAttachmentReference()->attachment;
		InputAttachments[0].layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		
		SubpassDesc.inputAttachmentCount = NumInputAttachments;
		SubpassDesc.pInputAttachments = InputAttachments;
		// depth attachment is same as input attachment
		SubpassDesc.pDepthStencilAttachment = InputAttachments;
						
		VkSubpassDependency& SubpassDep = SubpassDependencies[NumDependencies++];
		SubpassDep.srcSubpass = 0;
		SubpassDep.dstSubpass = 1;
	    SubpassDep.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		SubpassDep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		SubpassDep.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		SubpassDep.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
		SubpassDep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
	}
	
	CreateInfo.attachmentCount = RTLayout.GetNumAttachmentDescriptions();
	CreateInfo.pAttachments = RTLayout.GetAttachmentDescriptions();
	CreateInfo.subpassCount = NumSubpasses;
	CreateInfo.pSubpasses = SubpassDescriptions;
	CreateInfo.dependencyCount = NumDependencies;
	CreateInfo.pDependencies = SubpassDependencies;

	/*
	Bit mask that specifies which view rendering is broadcast to
	0011 = Broadcast to first and second view (layer)
	*/
	const uint32_t ViewMask[2] = { 0b00000011, 0b00000011 };

	/*
	Bit mask that specifices correlation between views
	An implementation may use this for optimizations (concurrent render)
	*/
	const uint32_t CorrelationMask = 0b00000011;

	VkRenderPassMultiviewCreateInfo MultiviewInfo;
	if (RTLayout.GetIsMultiView())
	{
		FMemory::Memzero(MultiviewInfo);
		MultiviewInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO;
		MultiviewInfo.pNext = nullptr;
		MultiviewInfo.subpassCount = NumSubpasses;
		MultiviewInfo.pViewMasks = ViewMask;
		MultiviewInfo.dependencyCount = 0;
		MultiviewInfo.pViewOffsets = nullptr;
		MultiviewInfo.correlationMaskCount = 1;
		MultiviewInfo.pCorrelationMasks = &CorrelationMask;

		CreateInfo.pNext = &MultiviewInfo;
	}
	
	VkRenderPassFragmentDensityMapCreateInfoEXT FragDensityCreateInfo;
	if (InDevice.GetOptionalExtensions().HasEXTFragmentDensityMap && RTLayout.GetHasFragmentDensityAttachment())
	{
		ZeroVulkanStruct(FragDensityCreateInfo, VK_STRUCTURE_TYPE_RENDER_PASS_FRAGMENT_DENSITY_MAP_CREATE_INFO_EXT);
		FragDensityCreateInfo.fragmentDensityMapAttachment = *RTLayout.GetFragmentDensityAttachmentReference();

		// Chain fragment density info onto create info and the rest of the pNexts
		// onto the fragment density info
		FragDensityCreateInfo.pNext = CreateInfo.pNext;
		CreateInfo.pNext = &FragDensityCreateInfo;
	}

	VkRenderPass RenderPassHandle;
	VERIFYVULKANRESULT_EXPANDED(VulkanRHI::vkCreateRenderPass(InDevice.GetInstanceHandle(), &CreateInfo, VULKAN_CPU_ALLOCATOR, &RenderPassHandle));
	return RenderPassHandle;
}

FVulkanRenderPass::FVulkanRenderPass(FVulkanDevice& InDevice, const FVulkanRenderTargetLayout& InRTLayout) :
	Layout(InRTLayout),
	RenderPass(VK_NULL_HANDLE),
	NumUsedClearValues(InRTLayout.GetNumUsedClearValues()),
	Device(InDevice)
{
	INC_DWORD_STAT(STAT_VulkanNumRenderPasses);
	RenderPass = CreateRenderPass(InDevice, InRTLayout);
}

FVulkanRenderPass::~FVulkanRenderPass()
{
	DEC_DWORD_STAT(STAT_VulkanNumRenderPasses);

	Device.GetDeferredDeletionQueue().EnqueueResource(FDeferredDeletionQueue2::EType::RenderPass, RenderPass);
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

void VulkanResolveImage(VkCommandBuffer Cmd, FRHITexture* SourceTextureRHI, FRHITexture* DestTextureRHI)
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
	BufferSuballocation = InDevice->GetResourceHeapManager().AllocateBuffer(TotalSize, Usage, MemPropertyFlags, __FILE__, __LINE__);
	MinAlignment = BufferSuballocation->GetBufferAllocation()->GetAlignment();

	// Start by wrapping around to set up the correct fence
	BufferOffset = TotalSize;
}

FVulkanRingBuffer::~FVulkanRingBuffer()
{
	Device->GetDeferredDeletionQueue().EnqueueBufferSuballocationDirect(BufferSuballocation);
	BufferSuballocation = 0;
}

uint64 FVulkanRingBuffer::WrapAroundAllocateMemory(uint64 Size, uint32 Alignment, FVulkanCmdBuffer* InCmdBuffer)
{
	CA_ASSUME(InCmdBuffer != nullptr); // Suppress static analysis warning
	uint64 AllocationOffset = Align<uint64>(BufferOffset, Alignment);
	ensure(AllocationOffset + Size > BufferSize);

	// Check to see if we can wrap around the ring buffer
	if (FenceCmdBuffer)
	{
		if (FenceCounter == FenceCmdBuffer->GetFenceSignaledCounterI())
		{
			//if (FenceCounter == FenceCmdBuffer->GetSubmittedFenceCounter())
			{
				//UE_LOG(LogVulkanRHI, Error, TEXT("Ringbuffer overflow during the same cmd buffer!"));
			}
			//else
			{
				//UE_LOG(LogVulkanRHI, Error, TEXT("Wrapped around the ring buffer! Waiting for the GPU..."));
				//Device->GetImmediateContext().GetCommandBufferManager()->WaitForCmdBuffer(FenceCmdBuffer, 0.5f);
			}
		}
	}

	BufferOffset = Size;

	FenceCmdBuffer = InCmdBuffer;
	FenceCounter = InCmdBuffer->GetSubmittedFenceCounter();

	return 0;
}

void FVulkanDynamicRHI::SavePipelineCache()
{
	FString CacheFile = GetPipelineCacheFilename();

	GVulkanRHI->Device->PipelineStateCache->Save(CacheFile);
}

void FVulkanDynamicRHI::RebuildPipelineCache()
{
	GVulkanRHI->Device->PipelineStateCache->RebuildCache();
}

#if VULKAN_SUPPORTS_VALIDATION_CACHE
void FVulkanDynamicRHI::SaveValidationCache()
{
	VkValidationCacheEXT ValidationCache = GVulkanRHI->Device->GetValidationCache();
	if (ValidationCache != VK_NULL_HANDLE)
	{
		VkDevice Device = GVulkanRHI->Device->GetInstanceHandle();
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
	GVulkanRHI->Device->GetMemoryManager().DumpMemory();
	GVulkanRHI->Device->GetResourceHeapManager().DumpMemory();
	GVulkanRHI->Device->GetStagingManager().DumpMemory();
}
void FVulkanDynamicRHI::DumpLRU()
{
	FVulkanDynamicRHI* RHI = (FVulkanDynamicRHI*)GDynamicRHI;
	RHI->Device->PipelineStateCache->LRUDump();
}
void FVulkanDynamicRHI::TrimLRU()
{
	FVulkanDynamicRHI* RHI = (FVulkanDynamicRHI*)GDynamicRHI;
	RHI->Device->PipelineStateCache->LRUDebugEvictAll();
}


#endif

void FVulkanDynamicRHI::DestroySwapChain()
{
	if (IsInGameThread())
	{
		FlushRenderingCommands();
	}

	FVulkanDynamicRHI* RHI = (FVulkanDynamicRHI*)GDynamicRHI;
	TArray<FVulkanViewport*> Viewports = RHI->Viewports;
	ENQUEUE_RENDER_COMMAND(VulkanDestroySwapChain)(
		[Viewports](FRHICommandListImmediate& RHICmdList)
	{
		UE_LOG(LogVulkanRHI, Log, TEXT("Destroy swapchain ... "));
		
		for (FVulkanViewport* Viewport : Viewports)
		{
			Viewport->DestroySwapchain(nullptr);
		}
	});

	if (IsInGameThread())
	{
		FlushRenderingCommands();
	}
}

void FVulkanDynamicRHI::RecreateSwapChain(void* NewNativeWindow)
{
	if (NewNativeWindow)
	{
		if (IsInGameThread())
		{
			FlushRenderingCommands();
		}

		TArray<FVulkanViewport*> Viewports = GVulkanRHI->Viewports;
		ENQUEUE_RENDER_COMMAND(VulkanRecreateSwapChain)(
			[Viewports, NewNativeWindow](FRHICommandListImmediate& RHICmdList)
		{
			UE_LOG(LogVulkanRHI, Log, TEXT("Recreate swapchain ... "));
			
			for (FVulkanViewport* Viewport : Viewports)
			{
				Viewport->RecreateSwapchain(NewNativeWindow);
			}
		});

		if (IsInGameThread())
		{
			FlushRenderingCommands();
		}
	}
}

void FVulkanDynamicRHI::VulkanSetImageLayout( VkCommandBuffer CmdBuffer, VkImage Image, VkImageLayout OldLayout, VkImageLayout NewLayout, const VkImageSubresourceRange& SubresourceRange )
{
	::VulkanSetImageLayout( CmdBuffer, Image, OldLayout, NewLayout, SubresourceRange );
}

#undef LOCTEXT_NAMESPACE
