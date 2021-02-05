// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalRHI.cpp: Metal device RHI implementation.
=============================================================================*/

#include "MetalRHIPrivate.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "RenderUtils.h"
#if PLATFORM_IOS
#include "IOS/IOSAppDelegate.h"
#elif PLATFORM_MAC
#include "Mac/MacApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "GenericPlatform/GenericPlatformFile.h"
#endif
#include "HAL/FileManager.h"
#include "MetalProfiler.h"
#include "GenericPlatform/GenericPlatformDriver.h"
#include "MetalShaderResources.h"
#include "MetalLLM.h"
#include "Engine/RendererSettings.h"
#include "MetalTransitionData.h"

DEFINE_LOG_CATEGORY(LogMetal)

bool GIsMetalInitialized = false;

FMetalBufferFormat GMetalBufferFormats[PF_MAX];

static TAutoConsoleVariable<int32> CVarUseRHIThread(
													TEXT("r.Metal.IOSRHIThread"),
													0,
													TEXT("Controls RHIThread usage for IOS:\n")
													TEXT("\t0: No RHIThread.\n")
													TEXT("\t1: Use RHIThread.\n")
													TEXT("Default is 0."),
													ECVF_Default | ECVF_RenderThreadSafe
													);

static TAutoConsoleVariable<int32> CVarIntelUseRHIThread(
													TEXT("r.Metal.IntelRHIThread"),
													0,
													TEXT("Controls RHIThread usage for Mac Intel HW:\n")
													TEXT("\t0: No RHIThread.\n")
													TEXT("\t1: Use RHIThread.\n")
													TEXT("Default is 0."),
													ECVF_Default | ECVF_RenderThreadSafe
													);


static void ValidateTargetedRHIFeatureLevelExists(EShaderPlatform Platform)
{
	bool bSupportsShaderPlatform = false;
#if PLATFORM_MAC
	TArray<FString> TargetedShaderFormats;
	GConfig->GetArray(TEXT("/Script/MacTargetPlatform.MacTargetSettings"), TEXT("TargetedRHIs"), TargetedShaderFormats, GEngineIni);
	
	for (FString Name : TargetedShaderFormats)
	{
		FName ShaderFormatName(*Name);
		if (ShaderFormatToLegacyShaderPlatform(ShaderFormatName) == Platform)
		{
			bSupportsShaderPlatform = true;
			break;
		}
	}
#else
	if (Platform == SP_METAL || Platform == SP_METAL_TVOS)
	{
		GConfig->GetBool(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("bSupportsMetal"), bSupportsShaderPlatform, GEngineIni);
	}
	else if (Platform == SP_METAL_MRT || Platform == SP_METAL_MRT_TVOS)
	{
		GConfig->GetBool(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("bSupportsMetalMRT"), bSupportsShaderPlatform, GEngineIni);
	}
#endif
	
	if (!bSupportsShaderPlatform && !WITH_EDITOR)
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("ShaderPlatform"), FText::FromString(LegacyShaderPlatformToShaderFormat(Platform).ToString()));
		FText LocalizedMsg = FText::Format(NSLOCTEXT("MetalRHI", "ShaderPlatformUnavailable","Shader platform: {ShaderPlatform} was not cooked! Please enable this shader platform in the project's target settings."),Args);
		
		FText Title = NSLOCTEXT("MetalRHI", "ShaderPlatformUnavailableTitle","Shader Platform Unavailable");
		FMessageDialog::Open(EAppMsgType::Ok, LocalizedMsg, &Title);
		FPlatformMisc::RequestExit(true);
		
		METAL_FATAL_ERROR(TEXT("Shader platform: %s was not cooked! Please enable this shader platform in the project's target settings."), *LegacyShaderPlatformToShaderFormat(Platform).ToString());
	}
}

#if PLATFORM_MAC && WITH_EDITOR
static void VerifyMetalCompiler()
{
	FString OutStdOut;
	FString OutStdErr;
	
	// Using xcrun or xcodebuild will fire xcode-select if xcode or command line tools are not installed
	// This will also issue a popup dialog which will attempt to install command line tools which we don't want from the Editor
	
	// xcode-select --print-path
	// Can print out /Applications/Xcode.app/Contents/Developer OR /Library/Developer/CommandLineTools
	// CommandLineTools is no good for us as the Metal compiler isn't included
	{
		int32 ReturnCode = -1;
		bool bFoundXCode = false;
		
		FPlatformProcess::ExecProcess(TEXT("/usr/bin/xcode-select"), TEXT("--print-path"), &ReturnCode, &OutStdOut, &OutStdErr);
		if(ReturnCode == 0 && OutStdOut.Len() > 0)
		{
			OutStdOut.RemoveAt(OutStdOut.Len() - 1);
			if (IFileManager::Get().DirectoryExists(*OutStdOut))
			{
				FString XcodeAppPath = OutStdOut.Left(OutStdOut.Find(TEXT(".app/")) + 4);
				NSBundle* XcodeBundle = [NSBundle bundleWithPath:XcodeAppPath.GetNSString()];
				if (XcodeBundle)
				{
					bFoundXCode = true;
				}
			}
		}
		
		if(!bFoundXCode)
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText(NSLOCTEXT("MetalRHI", "XCodeMissingInstall", "Can't find Xcode install for Metal compiler. Please install Xcode and run Xcode.app to accept license or ensure active developer directory is set to current Xcode installation using xcode-select.")));
			FPlatformMisc::RequestExit(true);
			return;
		}
	}
	
	// xcodebuild -license check
	// -license check :returns 0 for accepted, otherwise 1 for command line tools or non zero for license not accepted
	// -checkFirstLaunchStatus | -runFirstLaunch : returns status and runs first launch not so useful from within the editor as sudo is required
	{
		int ReturnCode = -1;
		FPlatformProcess::ExecProcess(TEXT("/usr/bin/xcodebuild"), TEXT("-license check"), &ReturnCode, &OutStdOut, &OutStdErr);
		if(ReturnCode != 0)
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::Format(NSLOCTEXT("MetalRHI", "XCodeLicenseAgreement", "Xcode license agreement error: {0}"), FText::FromString(OutStdErr)));
			FPlatformMisc::RequestExit(true);
			return;
		}
	}
	
	
	// xcrun will return non zero if using command line tools
	// This can fail for license agreement as well or wrong command line tools set i.e set to /Library/Developer/CommandLineTools rather than Applications/Xcode.app/Contents/Developer
	{
		int ReturnCode = -1;
		FPlatformProcess::ExecProcess(TEXT("/usr/bin/xcrun"), TEXT("-sdk macosx metal -v"), &ReturnCode, &OutStdOut, &OutStdErr);
		if(ReturnCode != 0)
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::Format(NSLOCTEXT("MetalRHI", "XCodeMetalCompiler", "Xcode Metal Compiler error: {0}"), FText::FromString(OutStdErr)));
			FPlatformMisc::RequestExit(true);
			return;
		}
	}
}
#endif

FMetalDynamicRHI::FMetalDynamicRHI(ERHIFeatureLevel::Type RequestedFeatureLevel)
: ImmediateContext(nullptr, FMetalDeviceContext::CreateDeviceContext())
, AsyncComputeContext(nullptr)
{
	@autoreleasepool {
	// This should be called once at the start 
	check( IsInGameThread() );
	check( !GIsThreadedRendering );
	
#if PLATFORM_MAC && WITH_EDITOR
	VerifyMetalCompiler();
#endif
	
	GRHISupportsMultithreading = true;
	
	// we cannot render to a volume texture without geometry shader or vertex-shader-layer support, so initialise to false and enable based on platform feature availability
	GSupportsVolumeTextureRendering = false;
	
	// Metal always needs a render target to render with fragment shaders!
	GRHIRequiresRenderTargetForPixelShaderUAVs = true;

	//@todo-rco: Query name from API
	GRHIAdapterName = TEXT("Metal");
	GRHIVendorId = 1; // non-zero to avoid asserts

	bool const bRequestedFeatureLevel = (RequestedFeatureLevel != ERHIFeatureLevel::Num);
	bool bSupportsPointLights = false;
	bool bSupportsRHIThread = false;
	
	// get the device to ask about capabilities?
	mtlpp::Device Device = ImmediateContext.Context->GetDevice();
		
#if PLATFORM_IOS
	// A8 can use 256 bits of MRTs
#if PLATFORM_TVOS
	bool bCanUseWideMRTs = true;
	bool bCanUseASTC = true;
	GRHISupportsDrawIndirect = Device.SupportsFeatureSet(mtlpp::FeatureSet::tvOS_GPUFamily2_v1);
#else
	bool bCanUseWideMRTs = Device.SupportsFeatureSet(mtlpp::FeatureSet::iOS_GPUFamily2_v1);
	bool bCanUseASTC = Device.SupportsFeatureSet(mtlpp::FeatureSet::iOS_GPUFamily2_v1) && !FParse::Param(FCommandLine::Get(),TEXT("noastc"));
	
	GRHISupportsRWTextureBuffers = Device.SupportsFeatureSet(mtlpp::FeatureSet::iOS_GPUFamily4_v1);
	GRHISupportsDrawIndirect = Device.SupportsFeatureSet(mtlpp::FeatureSet::iOS_GPUFamily3_v1);

	const mtlpp::FeatureSet FeatureSets[] = {
		mtlpp::FeatureSet::iOS_GPUFamily1_v1,
		mtlpp::FeatureSet::iOS_GPUFamily2_v1,
		mtlpp::FeatureSet::iOS_GPUFamily3_v1,
		mtlpp::FeatureSet::iOS_GPUFamily4_v1
	};
		
	const uint8 FeatureSetVersions[][3] = {
		{8, 0, 0},
		{8, 3, 0},
		{10, 0, 0},
		{11, 0, 0}
	};
	
	GRHIDeviceId = 0;
	for (uint32 i = 0; i < 4; i++)
	{
		if (FPlatformMisc::IOSVersionCompare(FeatureSetVersions[i][0],FeatureSetVersions[i][1],FeatureSetVersions[i][2]) >= 0 && Device.SupportsFeatureSet(FeatureSets[i]))
		{
			GRHIDeviceId++;
		}
	}
		
	GSupportsVolumeTextureRendering = FMetalCommandQueue::SupportsFeature(EMetalFeaturesLayeredRendering);
	bSupportsPointLights = GSupportsVolumeTextureRendering;
#endif

    bool bProjectSupportsMRTs = false;
    GConfig->GetBool(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("bSupportsMetalMRT"), bProjectSupportsMRTs, GEngineIni);

	bool const bRequestedMetalMRT = ((RequestedFeatureLevel >= ERHIFeatureLevel::SM5) || (!bRequestedFeatureLevel && FParse::Param(FCommandLine::Get(),TEXT("metalmrt"))));
	bSupportsRHIThread = FParse::Param(FCommandLine::Get(),TEXT("rhithread"));

    // only allow GBuffers, etc on A8s (A7s are just not going to cut it)
    if (bProjectSupportsMRTs && bCanUseWideMRTs && bRequestedMetalMRT)
    {
#if PLATFORM_TVOS
		ValidateTargetedRHIFeatureLevelExists(SP_METAL_MRT);
		GMaxRHIShaderPlatform = SP_METAL_MRT_TVOS;
#else
		ValidateTargetedRHIFeatureLevelExists(SP_METAL_MRT);
        GMaxRHIShaderPlatform = SP_METAL_MRT;
#endif
		GMaxRHIFeatureLevel = ERHIFeatureLevel::SM5;
    }
    else
	{
		if (bRequestedMetalMRT)
		{
			UE_LOG(LogMetal, Warning, TEXT("Metal MRT support requires an iOS or tvOS device with an A8 processor or later. Falling back to Metal ES 3.1."));
		}
		
#if PLATFORM_TVOS
		ValidateTargetedRHIFeatureLevelExists(SP_METAL_TVOS);
		GMaxRHIShaderPlatform = SP_METAL_TVOS;
#else
		ValidateTargetedRHIFeatureLevelExists(SP_METAL);
		GMaxRHIShaderPlatform = SP_METAL;
#endif
        GMaxRHIFeatureLevel = ERHIFeatureLevel::ES3_1;
	}
		
	FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
		
	MemoryStats.DedicatedVideoMemory = 0;
	MemoryStats.TotalGraphicsMemory = Stats.AvailablePhysical;
	MemoryStats.DedicatedSystemMemory = 0;
	MemoryStats.SharedSystemMemory = Stats.AvailablePhysical;
	
#if PLATFORM_TVOS
	GShaderPlatformForFeatureLevel[ERHIFeatureLevel::ES2_REMOVED] = SP_NumPlatforms;
	GShaderPlatformForFeatureLevel[ERHIFeatureLevel::ES3_1] = SP_METAL_TVOS;
#else
	GShaderPlatformForFeatureLevel[ERHIFeatureLevel::ES2_REMOVED] = SP_NumPlatforms;
	GShaderPlatformForFeatureLevel[ERHIFeatureLevel::ES3_1] = SP_METAL;
#endif
	GShaderPlatformForFeatureLevel[ERHIFeatureLevel::SM4_REMOVED] = SP_NumPlatforms;
	GShaderPlatformForFeatureLevel[ERHIFeatureLevel::SM5] = (GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5) ? GMaxRHIShaderPlatform : SP_NumPlatforms;

#else // PLATFORM_IOS
                
	uint32 DeviceIndex = ((FMetalDeviceContext*)ImmediateContext.Context)->GetDeviceIndex();
	
	TArray<FMacPlatformMisc::FGPUDescriptor> const& GPUs = FPlatformMisc::GetGPUDescriptors();
	check(DeviceIndex < GPUs.Num());
	FMacPlatformMisc::FGPUDescriptor const& GPUDesc = GPUs[DeviceIndex];
	
    // A8 can use 256 bits of MRTs
    bool bCanUseWideMRTs = true;
    bool bCanUseASTC = false;
	bool bSupportsD24S8 = false;
	bool bSupportsD16 = false;
	
	GRHIAdapterName = FString(Device.GetName());
	
	// However they don't all support other features depending on the version of the OS.
	bool bSupportsTiledReflections = false;
	bool bSupportsDistanceFields = false;
	
	// Default is SM5 on:
	// 10.11.6 for AMD/Nvidia
	// 10.12.2+ for AMD/Nvidia
	// 10.12.4+ for Intel
	bool bSupportsSM5 = true;
	bool bIsIntelHaswell = false;
	
	// All should work on Catalina+ using GPU end time
	GSupportsTimestampRenderQueries = FPlatformMisc::MacOSXVersionCompare(10,15,0) >= 0;
	
	if(GRHIAdapterName.Contains("Nvidia"))
	{
		bSupportsPointLights = true;
		GRHIVendorId = 0x10DE;
		bSupportsTiledReflections = true;
		bSupportsDistanceFields = (FPlatformMisc::MacOSXVersionCompare(10,11,4) >= 0);
		bSupportsRHIThread = (FPlatformMisc::MacOSXVersionCompare(10,12,0) >= 0);
	}
	else if(GRHIAdapterName.Contains("ATi") || GRHIAdapterName.Contains("AMD"))
	{
		bSupportsPointLights = true;
		GRHIVendorId = 0x1002;
		if((FPlatformMisc::MacOSXVersionCompare(10,12,0) < 0) && GPUDesc.GPUVendorId == GRHIVendorId)
		{
			GRHIAdapterName = FString(GPUDesc.GPUName);
		}
		bSupportsTiledReflections = true;
		bSupportsDistanceFields = (FPlatformMisc::MacOSXVersionCompare(10,11,4) >= 0);
		bSupportsRHIThread = true;
		
		// On AMD can also use completion handler time stamp if macOS < Catalina
		GSupportsTimestampRenderQueries = true;
	}
	else if(GRHIAdapterName.Contains("Intel"))
	{
		bSupportsTiledReflections = false;
		bSupportsPointLights = (FPlatformMisc::MacOSXVersionCompare(10,14,6) > 0);
		GRHIVendorId = 0x8086;
		// HACK: Meshes jump around in Infiltrator with RHI thread on. Needs further investigation and a real fix.
		bSupportsRHIThread = false || CVarIntelUseRHIThread.GetValueOnAnyThread() > 0;
		bSupportsDistanceFields = (FPlatformMisc::MacOSXVersionCompare(10,12,2) >= 0);
		bIsIntelHaswell = (GRHIAdapterName == TEXT("Intel HD Graphics 5000") || GRHIAdapterName == TEXT("Intel Iris Graphics") || GRHIAdapterName == TEXT("Intel Iris Pro Graphics"));
	}
	else if(GRHIAdapterName.Contains("Apple"))
	{
		bSupportsPointLights = true;
		GRHIVendorId = 0x106B;
		bSupportsTiledReflections = true;
		bSupportsDistanceFields = true;
		bSupportsRHIThread = true;
		GSupportsTimestampRenderQueries = true;
	}

	bool const bRequestedSM5 = (RequestedFeatureLevel == ERHIFeatureLevel::SM5 || (!bRequestedFeatureLevel && (FParse::Param(FCommandLine::Get(),TEXT("metalsm5")) || FParse::Param(FCommandLine::Get(),TEXT("metalmrt")))));
	if(bSupportsSM5 && bRequestedSM5)
	{
		GMaxRHIFeatureLevel = ERHIFeatureLevel::SM5;
		if (!FParse::Param(FCommandLine::Get(),TEXT("metalmrt")))
		{
			GMaxRHIShaderPlatform = SP_METAL_SM5;
		}
		else
		{
			GMaxRHIShaderPlatform = SP_METAL_MRT_MAC;
		}
	}
	else
	{
		if (bRequestedSM5)
		{
			UE_LOG(LogMetal, Warning, TEXT("Metal Shader Model 5 w/tessellation support requires 10.12.6 for Nvidia, it is broken on 10.13.0+. Falling back to Metal Shader Model 5 without tessellation support."));
		}
	
		GMaxRHIFeatureLevel = ERHIFeatureLevel::SM5;
		GMaxRHIShaderPlatform = SP_METAL_SM5_NOTESS;
	}

	ERHIFeatureLevel::Type PreviewFeatureLevel;
	if (RHIGetPreviewFeatureLevel(PreviewFeatureLevel))
	{
		check(PreviewFeatureLevel == ERHIFeatureLevel::ES3_1);

		// ES3.1 feature level emulation
		GMaxRHIFeatureLevel = PreviewFeatureLevel;
		if (GMaxRHIFeatureLevel == ERHIFeatureLevel::ES3_1)
		{
			GMaxRHIShaderPlatform = SP_METAL_MACES3_1;
		}
	}

	ValidateTargetedRHIFeatureLevelExists(GMaxRHIShaderPlatform);
	
	GShaderPlatformForFeatureLevel[ERHIFeatureLevel::ES2_REMOVED] = SP_NumPlatforms;
	GShaderPlatformForFeatureLevel[ERHIFeatureLevel::ES3_1] = (GMaxRHIFeatureLevel >= ERHIFeatureLevel::ES3_1) ? SP_METAL_MACES3_1 : SP_NumPlatforms;
	GShaderPlatformForFeatureLevel[ERHIFeatureLevel::SM4_REMOVED] = SP_NumPlatforms;
	GShaderPlatformForFeatureLevel[ERHIFeatureLevel::SM5] = (GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5) ? GMaxRHIShaderPlatform : SP_NumPlatforms;
	
	// Mac GPUs support layer indexing.
	GSupportsVolumeTextureRendering = (GMaxRHIShaderPlatform != SP_METAL_MRT_MAC);
	bSupportsPointLights &= (GMaxRHIShaderPlatform != SP_METAL_MRT_MAC);
	
	// Make sure the vendors match - the assumption that order in IORegistry is the order in Metal may not hold up forever.
	if(GPUDesc.GPUVendorId == GRHIVendorId)
	{
		GRHIDeviceId = GPUDesc.GPUDeviceId;
		MemoryStats.DedicatedVideoMemory = (int64)GPUDesc.GPUMemoryMB * 1024 * 1024;
		MemoryStats.TotalGraphicsMemory = (int64)GPUDesc.GPUMemoryMB * 1024 * 1024;
		MemoryStats.DedicatedSystemMemory = 0;
		MemoryStats.SharedSystemMemory = 0;
	}
	
	// Change the support depth format if we can
	bSupportsD24S8 = Device.IsDepth24Stencil8PixelFormatSupported();
	
	// Disable tiled reflections on Mac Metal for some GPU drivers that ignore the lod-level and so render incorrectly.
	if (!bSupportsTiledReflections && !FParse::Param(FCommandLine::Get(),TEXT("metaltiledreflections")))
	{
		static auto CVarDoTiledReflections = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DoTiledReflections"));
		if(CVarDoTiledReflections && CVarDoTiledReflections->GetInt() != 0)
		{
			CVarDoTiledReflections->Set(0);
		}
	}
	
	// Disable the distance field AO & shadowing effects on GPU drivers that don't currently execute the shaders correctly.
	if ((GMaxRHIShaderPlatform == SP_METAL_SM5 || GMaxRHIShaderPlatform == SP_METAL_SM5_NOTESS) && !bSupportsDistanceFields && !FParse::Param(FCommandLine::Get(),TEXT("metaldistancefields")))
	{
		static auto CVarDistanceFieldAO = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DistanceFieldAO"));
		if(CVarDistanceFieldAO && CVarDistanceFieldAO->GetInt() != 0)
		{
			CVarDistanceFieldAO->Set(0);
		}
		
		static auto CVarDistanceFieldShadowing = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DistanceFieldShadowing"));
		if(CVarDistanceFieldShadowing && CVarDistanceFieldShadowing->GetInt() != 0)
		{
			CVarDistanceFieldShadowing->Set(0);
		}
	}
	
#endif

	
	GRHISupportsCopyToTextureMultipleMips = true;
		
	if(
	   #if PLATFORM_MAC
	   (Device.SupportsFeatureSet(mtlpp::FeatureSet::macOS_GPUFamily1_v3) && FPlatformMisc::MacOSXVersionCompare(10,13,0) >= 0)
	   #elif PLATFORM_IOS || PLATFORM_TVOS
	   FPlatformMisc::IOSVersionCompare(10,3,0)
	   #endif
	   )
	{
		GRHISupportsDynamicResolution = true;
		GRHISupportsFrameCyclesBubblesRemoval = true;
	}

	GPoolSizeVRAMPercentage = 0;
	GTexturePoolSize = 0;
	GConfig->GetInt(TEXT("TextureStreaming"), TEXT("PoolSizeVRAMPercentage"), GPoolSizeVRAMPercentage, GEngineIni);
	if ( GPoolSizeVRAMPercentage > 0 && MemoryStats.TotalGraphicsMemory > 0 )
	{
		float PoolSize = float(GPoolSizeVRAMPercentage) * 0.01f * float(MemoryStats.TotalGraphicsMemory);
		
		// Truncate GTexturePoolSize to MB (but still counted in bytes)
		GTexturePoolSize = int64(FGenericPlatformMath::TruncToFloat(PoolSize / 1024.0f / 1024.0f)) * 1024 * 1024;
		
		UE_LOG(LogRHI,Log,TEXT("Texture pool is %llu MB (%d%% of %llu MB)"),
			   GTexturePoolSize / 1024 / 1024,
			   GPoolSizeVRAMPercentage,
			   MemoryStats.TotalGraphicsMemory / 1024 / 1024);
	}
	else
	{
		static const auto CVarStreamingTexturePoolSize = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Streaming.PoolSize"));
		GTexturePoolSize = (int64)CVarStreamingTexturePoolSize->GetValueOnAnyThread() * 1024 * 1024;

		UE_LOG(LogRHI,Log,TEXT("Texture pool is %llu MB (of %llu MB total graphics mem)"),
			   GTexturePoolSize / 1024 / 1024,
			   MemoryStats.TotalGraphicsMemory / 1024 / 1024);
	}

	GRHITransitionPrivateData_SizeInBytes = sizeof(FMetalTransitionData);
	GRHITransitionPrivateData_AlignInBytes = alignof(FMetalTransitionData);

	GRHISupportsRHIThread = false;
	if (GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5)
	{
#if METAL_SUPPORTS_PARALLEL_RHI_EXECUTE
#if WITH_EDITORONLY_DATA
		GRHISupportsRHIThread = (!GIsEditor && bSupportsRHIThread);
#else
		GRHISupportsRHIThread = bSupportsRHIThread;
#endif
		GRHISupportsParallelRHIExecute = GRHISupportsRHIThread && ((!IsRHIDeviceIntel() && !IsRHIDeviceNVIDIA()) || FParse::Param(FCommandLine::Get(),TEXT("metalparallel")));
#endif
		GSupportsEfficientAsyncCompute = GRHISupportsParallelRHIExecute && (IsRHIDeviceAMD() || /*TODO: IsRHIDeviceApple()*/ (GRHIVendorId == 0x106B) || PLATFORM_IOS || FParse::Param(FCommandLine::Get(),TEXT("metalasynccompute"))); // Only AMD and Apple currently support async. compute and it requires parallel execution to be useful.
		GSupportsParallelOcclusionQueries = GRHISupportsRHIThread;
	}
	else
	{
		GRHISupportsRHIThread = bSupportsRHIThread || (CVarUseRHIThread.GetValueOnAnyThread() > 0);
		GRHISupportsParallelRHIExecute = false;
		GSupportsEfficientAsyncCompute = false;
		GSupportsParallelOcclusionQueries = false;
	}

	if (FPlatformMisc::IsDebuggerPresent() && UE_BUILD_DEBUG)
	{
#if PLATFORM_IOS // @todo zebra : needs a RENDER_API or whatever
		// Enable debug markers if we're running in Xcode
		extern int32 GEmitMeshDrawEvent;
		GEmitMeshDrawEvent = 1;
#endif
		SetEmitDrawEvents(true);
	}
	
	// Force disable vertex-shader-layer point light rendering on GPUs that don't support it properly yet.
	if(!bSupportsPointLights && !FParse::Param(FCommandLine::Get(),TEXT("metalpointlights")))
	{
		// Disable point light cubemap shadows on Mac Metal as currently they aren't supported.
		static auto CVarCubemapShadows = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AllowPointLightCubemapShadows"));
		if(CVarCubemapShadows && CVarCubemapShadows->GetInt() != 0)
		{
			CVarCubemapShadows->Set(0);
		}
	}
	
	if (!GSupportsVolumeTextureRendering && !FParse::Param(FCommandLine::Get(),TEXT("metaltlv")))
	{
		// Disable point light cubemap shadows on Mac Metal as currently they aren't supported.
		static auto CVarTranslucentLightingVolume = IConsoleManager::Get().FindConsoleVariable(TEXT("r.TranslucentLightingVolume"));
		if(CVarTranslucentLightingVolume && CVarTranslucentLightingVolume->GetInt() != 0)
		{
			CVarTranslucentLightingVolume->Set(0);
		}
	}

#if PLATFORM_MAC
	if (IsRHIDeviceIntel() && FPlatformMisc::MacOSXVersionCompare(10,13,5) < 0)
	{
		static auto CVarSGShadowQuality = IConsoleManager::Get().FindConsoleVariable((TEXT("sg.ShadowQuality")));
		if (CVarSGShadowQuality && CVarSGShadowQuality->GetInt() != 0)
		{
			CVarSGShadowQuality->Set(0);
		}
	}

	if (bIsIntelHaswell)
	{
		static auto CVarForceDisableVideoPlayback = IConsoleManager::Get().FindConsoleVariable((TEXT("Fort.ForceDisableVideoPlayback")));
		if (CVarForceDisableVideoPlayback && CVarForceDisableVideoPlayback->GetInt() != 1)
		{
			CVarForceDisableVideoPlayback->Set(1);
		}
	}
#endif

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	// we don't want to auto-enable draw events in Test
	SetEmitDrawEvents(GetEmitDrawEvents() | ENABLE_METAL_GPUEVENTS);
#endif

	GSupportsShaderFramebufferFetch = !PLATFORM_MAC && GMaxRHIShaderPlatform != SP_METAL_MRT && GMaxRHIShaderPlatform != SP_METAL_MRT_TVOS;
	GHardwareHiddenSurfaceRemoval = true;
	GSupportsRenderTargetFormat_PF_G8 = false;
	GRHISupportsTextureStreaming = true;
	GSupportsWideMRT = bCanUseWideMRTs;
	// GSupportsTransientResourceAliasing = FMetalCommandQueue::SupportsFeature(EMetalFeaturesHeaps) && FMetalCommandQueue::SupportsFeature(EMetalFeaturesFences);
	GSupportsSeparateRenderTargetBlendState = (GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5);

#if PLATFORM_MAC
	check(Device.SupportsFeatureSet(mtlpp::FeatureSet::macOS_GPUFamily1_v1));
	GRHISupportsBaseVertexIndex = FPlatformMisc::MacOSXVersionCompare(10,11,2) >= 0 || !IsRHIDeviceAMD(); // Supported on macOS & iOS but not tvOS - broken on AMD prior to 10.11.2
	GRHISupportsFirstInstance = true; // Supported on macOS & iOS but not tvOS.
	GMaxTextureDimensions = 16384;
	GMaxCubeTextureDimensions = 16384;
	GMaxTextureArrayLayers = 2048;
	GMaxShadowDepthBufferSizeX = GMaxTextureDimensions;
	GMaxShadowDepthBufferSizeY = GMaxTextureDimensions;
    bSupportsD16 = !FParse::Param(FCommandLine::Get(),TEXT("nometalv2")) && Device.SupportsFeatureSet(mtlpp::FeatureSet::macOS_GPUFamily1_v2);
    GRHISupportsHDROutput = FPlatformMisc::MacOSXVersionCompare(10,14,4) >= 0 && Device.SupportsFeatureSet(mtlpp::FeatureSet::macOS_GPUFamily1_v2);
	GRHIHDRDisplayOutputFormat = (GRHISupportsHDROutput) ? PF_PLATFORM_HDR_0 : PF_B8G8R8A8;
	// Based on the spec below, the maxTotalThreadsPerThreadgroup is not a fixed number but calculated according to the device current ability, so the available threads could less than the maximum number.
	// For safety and keep the consistency for all platform, reduce the maximum number to half of the device based.
	// https://developer.apple.com/documentation/metal/mtlcomputepipelinedescriptor/2966560-maxtotalthreadsperthreadgroup?language=objc
	GMaxWorkGroupInvocations = 512;
#else
	//@todo investigate gpufam4
	GMaxComputeSharedMemory = 1 << 14;
#if PLATFORM_TVOS
	GRHISupportsBaseVertexIndex = false;
	GRHISupportsFirstInstance = false; // Supported on macOS & iOS but not tvOS.
	GRHISupportsHDROutput = false;
	GRHIHDRDisplayOutputFormat = PF_B8G8R8A8; // must have a default value for non-hdr, just like mac or ios
#else
	// Only A9+ can support this, so for now we need to limit this to the desktop-forward renderer only.
	GRHISupportsBaseVertexIndex = Device.SupportsFeatureSet(mtlpp::FeatureSet::iOS_GPUFamily3_v1) && (GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5);
	GRHISupportsFirstInstance = GRHISupportsBaseVertexIndex;
	
	// TODO: Move this into IOSPlatform
	if (@available(iOS 11.0, *))
	{
		@autoreleasepool {
			UIScreen* mainScreen = [UIScreen mainScreen];
			UIDisplayGamut gamut = mainScreen.traitCollection.displayGamut;
			GRHISupportsHDROutput = FPlatformMisc::IOSVersionCompare(10, 0, 0) && gamut == UIDisplayGamutP3;
		}
	}
	
	GRHIHDRDisplayOutputFormat = (GRHISupportsHDROutput) ? PF_PLATFORM_HDR_0 : PF_B8G8R8A8;
	// Based on the spec below, the maxTotalThreadsPerThreadgroup is not a fixed number but calculated according to the device current ability, so the available threads could less than the maximum number.
	// For safety and keep the consistency for all platform, reduce the maximum number to half of the device based.
	// https://developer.apple.com/documentation/metal/mtlcomputepipelinedescriptor/2966560-maxtotalthreadsperthreadgroup?language=objc
	GMaxWorkGroupInvocations = Device.SupportsFeatureSet(mtlpp::FeatureSet::iOS_GPUFamily4_v1) ? 512 : 256;
#endif
	GMaxTextureDimensions = 8192;
	GMaxCubeTextureDimensions = 8192;
	GMaxTextureArrayLayers = 2048;
	GMaxShadowDepthBufferSizeX = GMaxTextureDimensions;
	GMaxShadowDepthBufferSizeY = GMaxTextureDimensions;
#endif
	
	GMaxTextureMipCount = FPlatformMath::CeilLogTwo( GMaxTextureDimensions ) + 1;
	GMaxTextureMipCount = FPlatformMath::Min<int32>( MAX_TEXTURE_MIP_COUNT, GMaxTextureMipCount );

	// Initialize the buffer format map - in such a way as to be able to validate it in non-shipping...
#if METAL_DEBUG_OPTIONS
	FMemory::Memset(GMetalBufferFormats, 255);
#endif
	GMetalBufferFormats[PF_Unknown              ] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_A32B32G32R32F        ] = { mtlpp::PixelFormat::RGBA32Float, (uint8)EMetalBufferFormat::RGBA32Float };
	GMetalBufferFormats[PF_B8G8R8A8             ] = { mtlpp::PixelFormat::RGBA8Unorm, (uint8)EMetalBufferFormat::RGBA8Unorm }; // mtlpp::PixelFormat::BGRA8Unorm/EMetalBufferFormat::BGRA8Unorm,  < We don't support this as a vertex-format so we have code to swizzle in the shader
	GMetalBufferFormats[PF_G8                   ] = { mtlpp::PixelFormat::R8Unorm, (uint8)EMetalBufferFormat::R8Unorm };
	GMetalBufferFormats[PF_G16                  ] = { mtlpp::PixelFormat::R16Unorm, (uint8)EMetalBufferFormat::R16Unorm };
	GMetalBufferFormats[PF_DXT1                 ] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_DXT3                 ] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_DXT5                 ] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_UYVY                 ] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_FloatRGB             ] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::RGB16Half };
	GMetalBufferFormats[PF_FloatRGBA            ] = { mtlpp::PixelFormat::RGBA16Float, (uint8)EMetalBufferFormat::RGBA16Half };
	GMetalBufferFormats[PF_DepthStencil         ] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_ShadowDepth          ] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_R32_FLOAT            ] = { mtlpp::PixelFormat::R32Float, (uint8)EMetalBufferFormat::R32Float };
	GMetalBufferFormats[PF_G16R16               ] = { mtlpp::PixelFormat::RG16Unorm, (uint8)EMetalBufferFormat::RG16Unorm };
	GMetalBufferFormats[PF_G16R16F              ] = { mtlpp::PixelFormat::RG16Float, (uint8)EMetalBufferFormat::RG16Half };
	GMetalBufferFormats[PF_G16R16F_FILTER       ] = { mtlpp::PixelFormat::RG16Float, (uint8)EMetalBufferFormat::RG16Half };
	GMetalBufferFormats[PF_G32R32F              ] = { mtlpp::PixelFormat::RG32Float, (uint8)EMetalBufferFormat::RG32Float };
	GMetalBufferFormats[PF_A2B10G10R10          ] = { mtlpp::PixelFormat::RGB10A2Unorm, (uint8)EMetalBufferFormat::RGB10A2Unorm };
	GMetalBufferFormats[PF_A16B16G16R16         ] = { mtlpp::PixelFormat::RGBA16Unorm, (uint8)EMetalBufferFormat::RGBA16Half };
	GMetalBufferFormats[PF_D24                  ] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_R16F                 ] = { mtlpp::PixelFormat::R16Float, (uint8)EMetalBufferFormat::RG16Half };
	GMetalBufferFormats[PF_R16F_FILTER          ] = { mtlpp::PixelFormat::R16Float, (uint8)EMetalBufferFormat::RG16Half };
	GMetalBufferFormats[PF_BC5                  ] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_V8U8                 ] = { mtlpp::PixelFormat::RG8Snorm, (uint8)EMetalBufferFormat::RG8Unorm };
	GMetalBufferFormats[PF_A1                   ] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_FloatR11G11B10       ] = { mtlpp::PixelFormat::RG11B10Float, (uint8)EMetalBufferFormat::RG11B10Half }; // < May not work on tvOS
	GMetalBufferFormats[PF_A8                   ] = { mtlpp::PixelFormat::A8Unorm, (uint8)EMetalBufferFormat::R8Unorm };
	GMetalBufferFormats[PF_R32_UINT             ] = { mtlpp::PixelFormat::R32Uint, (uint8)EMetalBufferFormat::R32Uint };
	GMetalBufferFormats[PF_R32_SINT             ] = { mtlpp::PixelFormat::R32Sint, (uint8)EMetalBufferFormat::R32Sint };
	GMetalBufferFormats[PF_PVRTC2               ] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_PVRTC4               ] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_R16_UINT             ] = { mtlpp::PixelFormat::R16Uint, (uint8)EMetalBufferFormat::R16Uint };
	GMetalBufferFormats[PF_R16_SINT             ] = { mtlpp::PixelFormat::R16Sint, (uint8)EMetalBufferFormat::R16Sint };
	GMetalBufferFormats[PF_R16G16B16A16_UINT    ] = { mtlpp::PixelFormat::RGBA16Uint, (uint8)EMetalBufferFormat::RGBA16Uint };
	GMetalBufferFormats[PF_R16G16B16A16_SINT    ] = { mtlpp::PixelFormat::RGBA16Sint, (uint8)EMetalBufferFormat::RGBA16Sint };
	GMetalBufferFormats[PF_R5G6B5_UNORM         ] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::R5G6B5Unorm };
	GMetalBufferFormats[PF_R8G8B8A8             ] = { mtlpp::PixelFormat::RGBA8Unorm, (uint8)EMetalBufferFormat::RGBA8Unorm };
	GMetalBufferFormats[PF_A8R8G8B8				] = { mtlpp::PixelFormat::RGBA8Unorm, (uint8)EMetalBufferFormat::RGBA8Unorm }; // mtlpp::PixelFormat::BGRA8Unorm/EMetalBufferFormat::BGRA8Unorm,  < We don't support this as a vertex-format so we have code to swizzle in the shader
	GMetalBufferFormats[PF_BC4					] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_R8G8                 ] = { mtlpp::PixelFormat::RG8Unorm, (uint8)EMetalBufferFormat::RG8Unorm };
	GMetalBufferFormats[PF_ATC_RGB				] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_ATC_RGBA_E			] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_ATC_RGBA_I			] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_X24_G8				] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_ETC1					] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_ETC2_RGB				] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_ETC2_RGBA			] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_R32G32B32A32_UINT	] = { mtlpp::PixelFormat::RGBA32Uint, (uint8)EMetalBufferFormat::RGBA32Uint };
	GMetalBufferFormats[PF_R16G16_UINT			] = { mtlpp::PixelFormat::RG16Uint, (uint8)EMetalBufferFormat::RG16Uint };
	GMetalBufferFormats[PF_R32G32_UINT			] = { mtlpp::PixelFormat::RG32Uint, (uint8)EMetalBufferFormat::RG32Uint };
	GMetalBufferFormats[PF_ASTC_4x4             ] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_ASTC_6x6             ] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_ASTC_8x8             ] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_ASTC_10x10           ] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_ASTC_12x12           ] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_BC6H					] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_BC7					] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_R8_UINT				] = { mtlpp::PixelFormat::R8Uint, (uint8)EMetalBufferFormat::R8Uint };
	GMetalBufferFormats[PF_R8					] = { mtlpp::PixelFormat::R8Unorm, (uint8)EMetalBufferFormat::R8Unorm };
	GMetalBufferFormats[PF_L8					] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::R8Unorm };
	GMetalBufferFormats[PF_XGXR8				] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_R8G8B8A8_UINT		] = { mtlpp::PixelFormat::RGBA8Uint, (uint8)EMetalBufferFormat::RGBA8Uint };
	GMetalBufferFormats[PF_R8G8B8A8_SNORM		] = { mtlpp::PixelFormat::RGBA8Snorm, (uint8)EMetalBufferFormat::RGBA8Snorm };
	GMetalBufferFormats[PF_R16G16B16A16_UNORM	] = { mtlpp::PixelFormat::RGBA16Unorm, (uint8)EMetalBufferFormat::RGBA16Unorm };
	GMetalBufferFormats[PF_R16G16B16A16_SNORM	] = { mtlpp::PixelFormat::RGBA16Snorm, (uint8)EMetalBufferFormat::RGBA16Snorm };
	GMetalBufferFormats[PF_PLATFORM_HDR_0		] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_PLATFORM_HDR_1		] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_PLATFORM_HDR_2		] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_NV12					] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	
	GMetalBufferFormats[PF_ETC2_R11_EAC			] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
	GMetalBufferFormats[PF_ETC2_RG11_EAC		] = { mtlpp::PixelFormat::Invalid, (uint8)EMetalBufferFormat::Unknown };
		
	// Initialize the platform pixel format map.
	GPixelFormats[PF_Unknown			].PlatformFormat	= (uint32)mtlpp::PixelFormat::Invalid;
	GPixelFormats[PF_A32B32G32R32F		].PlatformFormat	= (uint32)mtlpp::PixelFormat::RGBA32Float;
	GPixelFormats[PF_B8G8R8A8			].PlatformFormat	= (uint32)mtlpp::PixelFormat::BGRA8Unorm;
	GPixelFormats[PF_G8					].PlatformFormat	= (uint32)mtlpp::PixelFormat::R8Unorm;
	GPixelFormats[PF_G16				].PlatformFormat	= (uint32)mtlpp::PixelFormat::R16Unorm;
	GPixelFormats[PF_R32G32B32A32_UINT	].PlatformFormat	= (uint32)mtlpp::PixelFormat::RGBA32Uint;
	GPixelFormats[PF_R16G16_UINT		].PlatformFormat	= (uint32)mtlpp::PixelFormat::RG16Uint;
	GPixelFormats[PF_R32G32_UINT		].PlatformFormat	= (uint32)mtlpp::PixelFormat::RG32Uint;
		
#if PLATFORM_IOS
    GPixelFormats[PF_DXT1				].PlatformFormat	= (uint32)mtlpp::PixelFormat::Invalid;
	GPixelFormats[PF_DXT1				].Supported			= false;
    GPixelFormats[PF_DXT3				].PlatformFormat	= (uint32)mtlpp::PixelFormat::Invalid;
	GPixelFormats[PF_DXT3				].Supported			= false;
    GPixelFormats[PF_DXT5				].PlatformFormat	= (uint32)mtlpp::PixelFormat::Invalid;
	GPixelFormats[PF_DXT5				].Supported			= false;
	GPixelFormats[PF_BC5				].PlatformFormat	= (uint32)mtlpp::PixelFormat::Invalid;
	GPixelFormats[PF_BC5				].Supported			= false;
	GPixelFormats[PF_PVRTC2				].PlatformFormat	= (uint32)mtlpp::PixelFormat::PVRTC_RGBA_2BPP;
	GPixelFormats[PF_PVRTC2				].Supported			= true;
	GPixelFormats[PF_PVRTC4				].PlatformFormat	= (uint32)mtlpp::PixelFormat::PVRTC_RGBA_4BPP;
	GPixelFormats[PF_PVRTC4				].Supported			= true;
	GPixelFormats[PF_PVRTC4				].PlatformFormat	= (uint32)mtlpp::PixelFormat::PVRTC_RGBA_4BPP;
	GPixelFormats[PF_PVRTC4				].Supported			= true;
	GPixelFormats[PF_ASTC_4x4			].PlatformFormat	= (uint32)mtlpp::PixelFormat::ASTC_4x4_LDR;
	GPixelFormats[PF_ASTC_4x4			].Supported			= bCanUseASTC;
	GPixelFormats[PF_ASTC_6x6			].PlatformFormat	= (uint32)mtlpp::PixelFormat::ASTC_6x6_LDR;
	GPixelFormats[PF_ASTC_6x6			].Supported			= bCanUseASTC;
	GPixelFormats[PF_ASTC_8x8			].PlatformFormat	= (uint32)mtlpp::PixelFormat::ASTC_8x8_LDR;
	GPixelFormats[PF_ASTC_8x8			].Supported			= bCanUseASTC;
	GPixelFormats[PF_ASTC_10x10			].PlatformFormat	= (uint32)mtlpp::PixelFormat::ASTC_10x10_LDR;
	GPixelFormats[PF_ASTC_10x10			].Supported			= bCanUseASTC;
	GPixelFormats[PF_ASTC_12x12			].PlatformFormat	= (uint32)mtlpp::PixelFormat::ASTC_12x12_LDR;
	GPixelFormats[PF_ASTC_12x12			].Supported			= bCanUseASTC;

	// used with virtual textures
	GPixelFormats[PF_ETC2_RGB	  		].PlatformFormat	= (uint32)mtlpp::PixelFormat::ETC2_RGB8;
	GPixelFormats[PF_ETC2_RGB			].Supported			= true;
	GPixelFormats[PF_ETC2_RGBA	  		].PlatformFormat	= (uint32)mtlpp::PixelFormat::EAC_RGBA8;
	GPixelFormats[PF_ETC2_RGBA			].Supported			= true;
	GPixelFormats[PF_ETC2_R11_EAC	  	].PlatformFormat	= (uint32)mtlpp::PixelFormat::EAC_R11Unorm;
	GPixelFormats[PF_ETC2_R11_EAC		].Supported			= true;
	GPixelFormats[PF_ETC2_RG11_EAC		].PlatformFormat	= (uint32)mtlpp::PixelFormat::EAC_RG11Unorm;
	GPixelFormats[PF_ETC2_RG11_EAC		].Supported			= true;

	// IOS HDR format is BGR10_XR (32bits, 3 components)
	GPixelFormats[PF_PLATFORM_HDR_0		].BlockSizeX		= 1;
	GPixelFormats[PF_PLATFORM_HDR_0		].BlockSizeY		= 1;
	GPixelFormats[PF_PLATFORM_HDR_0		].BlockSizeZ		= 1;
	GPixelFormats[PF_PLATFORM_HDR_0		].BlockBytes		= 4;
	GPixelFormats[PF_PLATFORM_HDR_0		].NumComponents		= 3;
	GPixelFormats[PF_PLATFORM_HDR_0		].PlatformFormat	= (uint32)mtlpp::PixelFormat::BGR10_XR_sRGB;
	GPixelFormats[PF_PLATFORM_HDR_0		].Supported			= GRHISupportsHDROutput;
		
#if PLATFORM_TVOS
    if (!Device.SupportsFeatureSet(mtlpp::FeatureSet::tvOS_GPUFamily2_v1))
#else
	if (!Device.SupportsFeatureSet(mtlpp::FeatureSet::iOS_GPUFamily3_v2))
#endif
	{
		GPixelFormats[PF_FloatRGB			].PlatformFormat 	= (uint32)mtlpp::PixelFormat::RGBA16Float;
		GPixelFormats[PF_FloatRGBA			].BlockBytes		= 8;
		GPixelFormats[PF_FloatR11G11B10		].PlatformFormat	= (uint32)mtlpp::PixelFormat::RGBA16Float;
		GPixelFormats[PF_FloatR11G11B10		].BlockBytes		= 8;
		GPixelFormats[PF_FloatR11G11B10		].Supported			= true;
	}
	else
	{
		GPixelFormats[PF_FloatRGB			].PlatformFormat	= (uint32)mtlpp::PixelFormat::RG11B10Float;
		GPixelFormats[PF_FloatRGB			].BlockBytes		= 4;
		GPixelFormats[PF_FloatR11G11B10		].PlatformFormat	= (uint32)mtlpp::PixelFormat::RG11B10Float;
		GPixelFormats[PF_FloatR11G11B10		].BlockBytes		= 4;
		GPixelFormats[PF_FloatR11G11B10		].Supported			= true;
	}
	
		GPixelFormats[PF_DepthStencil		].PlatformFormat	= (uint32)mtlpp::PixelFormat::Depth32Float_Stencil8;
		GPixelFormats[PF_DepthStencil		].BlockBytes		= 4;

	GPixelFormats[PF_DepthStencil		].Supported			= true;
	GPixelFormats[PF_ShadowDepth		].PlatformFormat	= (uint32)mtlpp::PixelFormat::Depth32Float;
	GPixelFormats[PF_ShadowDepth		].BlockBytes		= 4;
	GPixelFormats[PF_ShadowDepth		].Supported			= true;
		
	GPixelFormats[PF_BC5				].PlatformFormat	= (uint32)mtlpp::PixelFormat::Invalid;
	GPixelFormats[PF_R5G6B5_UNORM		].PlatformFormat	= (uint32)mtlpp::PixelFormat::B5G6R5Unorm;
#else
    GPixelFormats[PF_DXT1				].PlatformFormat	= (uint32)mtlpp::PixelFormat::BC1_RGBA;
    GPixelFormats[PF_DXT3				].PlatformFormat	= (uint32)mtlpp::PixelFormat::BC2_RGBA;
    GPixelFormats[PF_DXT5				].PlatformFormat	= (uint32)mtlpp::PixelFormat::BC3_RGBA;
	
	GPixelFormats[PF_FloatRGB			].PlatformFormat	= (uint32)mtlpp::PixelFormat::RG11B10Float;
	GPixelFormats[PF_FloatRGB			].BlockBytes		= 4;
	GPixelFormats[PF_FloatR11G11B10		].PlatformFormat	= (uint32)mtlpp::PixelFormat::RG11B10Float;
	GPixelFormats[PF_FloatR11G11B10		].BlockBytes		= 4;
	GPixelFormats[PF_FloatR11G11B10		].Supported			= true;
	
	// Only one HDR format for OSX.
	GPixelFormats[PF_PLATFORM_HDR_0		].BlockSizeX		= 1;
	GPixelFormats[PF_PLATFORM_HDR_0		].BlockSizeY		= 1;
	GPixelFormats[PF_PLATFORM_HDR_0		].BlockSizeZ		= 1;
	GPixelFormats[PF_PLATFORM_HDR_0		].BlockBytes		= 8;
	GPixelFormats[PF_PLATFORM_HDR_0		].NumComponents		= 4;
	GPixelFormats[PF_PLATFORM_HDR_0		].PlatformFormat	= (uint32)mtlpp::PixelFormat::RGBA16Float;
	GPixelFormats[PF_PLATFORM_HDR_0		].Supported			= GRHISupportsHDROutput;
		
	// Use Depth28_Stencil8 when it is available for consistency
	if(bSupportsD24S8)
	{
		GPixelFormats[PF_DepthStencil	].PlatformFormat	= (uint32)mtlpp::PixelFormat::Depth24Unorm_Stencil8;
	}
	else
	{
		GPixelFormats[PF_DepthStencil	].PlatformFormat	= (uint32)mtlpp::PixelFormat::Depth32Float_Stencil8;
	}
	GPixelFormats[PF_DepthStencil		].BlockBytes		= 4;
	GPixelFormats[PF_DepthStencil		].Supported			= true;
	if (bSupportsD16)
	{
		GPixelFormats[PF_ShadowDepth		].PlatformFormat	= (uint32)mtlpp::PixelFormat::Depth16Unorm;
		GPixelFormats[PF_ShadowDepth		].BlockBytes		= 2;
	}
	else
	{
		GPixelFormats[PF_ShadowDepth		].PlatformFormat	= (uint32)mtlpp::PixelFormat::Depth32Float;
		GPixelFormats[PF_ShadowDepth		].BlockBytes		= 4;
	}
	GPixelFormats[PF_ShadowDepth		].Supported			= true;
	if(bSupportsD24S8)
	{
		GPixelFormats[PF_D24			].PlatformFormat	= (uint32)mtlpp::PixelFormat::Depth24Unorm_Stencil8;
	}
	else
	{
		GPixelFormats[PF_D24			].PlatformFormat	= (uint32)mtlpp::PixelFormat::Depth32Float;
	}
	GPixelFormats[PF_D24				].Supported			= true;
	GPixelFormats[PF_BC4				].Supported			= true;
	GPixelFormats[PF_BC4				].PlatformFormat	= (uint32)mtlpp::PixelFormat::BC4_RUnorm;
	GPixelFormats[PF_BC5				].Supported			= true;
	GPixelFormats[PF_BC5				].PlatformFormat	= (uint32)mtlpp::PixelFormat::BC5_RGUnorm;
	GPixelFormats[PF_BC6H				].Supported			= true;
	GPixelFormats[PF_BC6H               ].PlatformFormat	= (uint32)mtlpp::PixelFormat::BC6H_RGBUfloat;
	GPixelFormats[PF_BC7				].Supported			= true;
	GPixelFormats[PF_BC7				].PlatformFormat	= (uint32)mtlpp::PixelFormat::BC7_RGBAUnorm;
	GPixelFormats[PF_R5G6B5_UNORM		].PlatformFormat	= (uint32)mtlpp::PixelFormat::Invalid;
#endif
	GPixelFormats[PF_UYVY				].PlatformFormat	= (uint32)mtlpp::PixelFormat::Invalid;
	GPixelFormats[PF_FloatRGBA			].PlatformFormat	= (uint32)mtlpp::PixelFormat::RGBA16Float;
	GPixelFormats[PF_FloatRGBA			].BlockBytes		= 8;
    GPixelFormats[PF_X24_G8				].PlatformFormat	= (uint32)mtlpp::PixelFormat::Stencil8;
    GPixelFormats[PF_X24_G8				].BlockBytes		= 1;
	GPixelFormats[PF_R32_FLOAT			].PlatformFormat	= (uint32)mtlpp::PixelFormat::R32Float;
	GPixelFormats[PF_G16R16				].PlatformFormat	= (uint32)mtlpp::PixelFormat::RG16Unorm;
	GPixelFormats[PF_G16R16				].Supported			= true;
	GPixelFormats[PF_G16R16F			].PlatformFormat	= (uint32)mtlpp::PixelFormat::RG16Float;
	GPixelFormats[PF_G16R16F_FILTER		].PlatformFormat	= (uint32)mtlpp::PixelFormat::RG16Float;
	GPixelFormats[PF_G32R32F			].PlatformFormat	= (uint32)mtlpp::PixelFormat::RG32Float;
	GPixelFormats[PF_A2B10G10R10		].PlatformFormat    = (uint32)mtlpp::PixelFormat::RGB10A2Unorm;
	GPixelFormats[PF_A16B16G16R16		].PlatformFormat    = (uint32)mtlpp::PixelFormat::RGBA16Unorm;
	GPixelFormats[PF_R16F				].PlatformFormat	= (uint32)mtlpp::PixelFormat::R16Float;
	GPixelFormats[PF_R16F_FILTER		].PlatformFormat	= (uint32)mtlpp::PixelFormat::R16Float;
	GPixelFormats[PF_V8U8				].PlatformFormat	= (uint32)mtlpp::PixelFormat::RG8Snorm;
	GPixelFormats[PF_A1					].PlatformFormat	= (uint32)mtlpp::PixelFormat::Invalid;
	// A8 does not allow writes in Metal. So we will fake it with R8.
	// If you change this you must also change the swizzle pattern in Platform.ush
	// See Texture2DSample_A8 in Common.ush and A8_SAMPLE_MASK in Platform.ush
	GPixelFormats[PF_A8					].PlatformFormat	= (uint32)mtlpp::PixelFormat::R8Unorm;
	GPixelFormats[PF_R32_UINT			].PlatformFormat	= (uint32)mtlpp::PixelFormat::R32Uint;
	GPixelFormats[PF_R32_SINT			].PlatformFormat	= (uint32)mtlpp::PixelFormat::R32Sint;
	GPixelFormats[PF_R16G16B16A16_UINT	].PlatformFormat	= (uint32)mtlpp::PixelFormat::RGBA16Uint;
	GPixelFormats[PF_R16G16B16A16_SINT	].PlatformFormat	= (uint32)mtlpp::PixelFormat::RGBA16Sint;
	GPixelFormats[PF_R8G8B8A8			].PlatformFormat	= (uint32)mtlpp::PixelFormat::RGBA8Unorm;
	GPixelFormats[PF_R8G8B8A8_UINT		].PlatformFormat	= (uint32)mtlpp::PixelFormat::RGBA8Uint;
	GPixelFormats[PF_R8G8B8A8_SNORM		].PlatformFormat	= (uint32)mtlpp::PixelFormat::RGBA8Snorm;
	GPixelFormats[PF_R8G8				].PlatformFormat	= (uint32)mtlpp::PixelFormat::RG8Unorm;
	GPixelFormats[PF_R16_SINT			].PlatformFormat	= (uint32)mtlpp::PixelFormat::R16Sint;
	GPixelFormats[PF_R16_UINT			].PlatformFormat	= (uint32)mtlpp::PixelFormat::R16Uint;
	GPixelFormats[PF_R8_UINT			].PlatformFormat	= (uint32)mtlpp::PixelFormat::R8Uint;
	GPixelFormats[PF_R8					].PlatformFormat	= (uint32)mtlpp::PixelFormat::R8Unorm;

	GPixelFormats[PF_R16G16B16A16_UNORM ].PlatformFormat	= (uint32)mtlpp::PixelFormat::RGBA16Unorm;
	GPixelFormats[PF_R16G16B16A16_SNORM ].PlatformFormat	= (uint32)mtlpp::PixelFormat::RGBA16Snorm;

	GPixelFormats[PF_NV12				].PlatformFormat	= (uint32)mtlpp::PixelFormat::Invalid;
	GPixelFormats[PF_NV12				].Supported			= false;
	
#if METAL_DEBUG_OPTIONS
	for (uint32 i = 0; i < PF_MAX; i++)
	{
		checkf((NSUInteger)GMetalBufferFormats[i].LinearTextureFormat != NSUIntegerMax, TEXT("Metal linear texture format for pixel-format %s (%d) is not configured!"), GPixelFormats[i].Name, i);
		checkf(GMetalBufferFormats[i].DataFormat != 255, TEXT("Metal data buffer format for pixel-format %s (%d) is not configured!"), GPixelFormats[i].Name, i);
	}
#endif
		
	// get driver version (todo: share with other RHIs)
	{
		FGPUDriverInfo GPUDriverInfo = FPlatformMisc::GetGPUDriverInfo(GRHIAdapterName);
		
		GRHIAdapterUserDriverVersion = GPUDriverInfo.UserDriverVersion;
		GRHIAdapterInternalDriverVersion = GPUDriverInfo.InternalDriverVersion;
		GRHIAdapterDriverDate = GPUDriverInfo.DriverDate;
		
		UE_LOG(LogMetal, Display, TEXT("    Adapter Name: %s"), *GRHIAdapterName);
		UE_LOG(LogMetal, Display, TEXT("  Driver Version: %s (internal:%s, unified:%s)"), *GRHIAdapterUserDriverVersion, *GRHIAdapterInternalDriverVersion, *GPUDriverInfo.GetUnifiedDriverVersion());
		UE_LOG(LogMetal, Display, TEXT("     Driver Date: %s"), *GRHIAdapterDriverDate);
		UE_LOG(LogMetal, Display, TEXT("          Vendor: %s"), *GPUDriverInfo.ProviderName);
#if PLATFORM_MAC
		if(GPUDesc.GPUVendorId == GRHIVendorId)
		{
			UE_LOG(LogMetal, Display,  TEXT("      Vendor ID: %d"), GPUDesc.GPUVendorId);
			UE_LOG(LogMetal, Display,  TEXT("      Device ID: %d"), GPUDesc.GPUDeviceId);
			UE_LOG(LogMetal, Display,  TEXT("      VRAM (MB): %d"), GPUDesc.GPUMemoryMB);
		}
		else
		{
			UE_LOG(LogMetal, Warning,  TEXT("GPU descriptor (%s) from IORegistry failed to match Metal (%s)"), *FString(GPUDesc.GPUName), *GRHIAdapterName);
		}
#endif
	}

#if PLATFORM_MAC
	if (!FPlatformProcess::IsSandboxedApplication())
	{
		// Cleanup local BinaryPSOs folder as it's not used anymore.
		const FString BinaryPSOsDir = FPaths::ProjectSavedDir() / TEXT("BinaryPSOs");
		IPlatformFile::GetPlatformPhysical().DeleteDirectoryRecursively(*BinaryPSOsDir);
	}
#endif

	((FMetalDeviceContext&)ImmediateContext.GetInternalContext()).Init();
		
	GDynamicRHI = this;
	GIsMetalInitialized = true;

	ImmediateContext.Profiler = nullptr;
#if ENABLE_METAL_GPUPROFILE
	ImmediateContext.Profiler = FMetalProfiler::CreateProfiler(ImmediateContext.Context);
	if (ImmediateContext.Profiler)
		ImmediateContext.Profiler->BeginFrame();
#endif
	AsyncComputeContext = GSupportsEfficientAsyncCompute ? new FMetalRHIComputeContext(ImmediateContext.Profiler, new FMetalContext(ImmediateContext.Context->GetDevice(), ImmediateContext.Context->GetCommandQueue(), true)) : nullptr;

#if ENABLE_METAL_GPUPROFILE
		if (ImmediateContext.Profiler)
			ImmediateContext.Profiler->EndFrame();
#endif
	}
}

FMetalDynamicRHI::~FMetalDynamicRHI()
{
	check(IsInGameThread() && IsInRenderingThread());
	
	GIsMetalInitialized = false;
	GIsRHIInitialized = false;

	// Ask all initialized FRenderResources to release their RHI resources.
	FRenderResource::ReleaseRHIForAllResources();	
	
#if ENABLE_METAL_GPUPROFILE
	FMetalProfiler::DestroyProfiler();
#endif
}

uint64 FMetalDynamicRHI::RHICalcTexture2DPlatformSize(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, const FRHIResourceCreateInfo& CreateInfo, uint32& OutAlign)
{
	@autoreleasepool {
	OutAlign = 0;
	return CalcTextureSize(SizeX, SizeY, (EPixelFormat)Format, NumMips);
	}
}

uint64 FMetalDynamicRHI::RHICalcTexture3DPlatformSize(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, const FRHIResourceCreateInfo& CreateInfo, uint32& OutAlign)
{
	@autoreleasepool {
	OutAlign = 0;
	return CalcTextureSize3D(SizeX, SizeY, SizeZ, (EPixelFormat)Format, NumMips);
	}
}

uint64 FMetalDynamicRHI::RHICalcTextureCubePlatformSize(uint32 Size, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, const FRHIResourceCreateInfo& CreateInfo, uint32& OutAlign)
{
	@autoreleasepool {
	OutAlign = 0;
	return CalcTextureSize(Size, Size, (EPixelFormat)Format, NumMips) * 6;
	}
}

uint64 FMetalDynamicRHI::RHIGetMinimumAlignmentForBufferBackedSRV(EPixelFormat Format)
{
	return ImmediateContext.Context->GetDevice().GetMinimumLinearTextureAlignmentForPixelFormat((mtlpp::PixelFormat)GMetalBufferFormats[Format].LinearTextureFormat);
}

void FMetalDynamicRHI::Init()
{
	// Command lists need the validation RHI context if enabled, so call the global scope version of RHIGetDefaultContext() and RHIGetDefaultAsyncComputeContext().
	GRHICommandList.GetImmediateCommandList().SetContext(::RHIGetDefaultContext());
	GRHICommandList.GetImmediateAsyncComputeCommandList().SetComputeContext(::RHIGetDefaultAsyncComputeContext());

	FRenderResource::InitPreRHIResources();
	GIsRHIInitialized = true;
}

void FMetalRHIImmediateCommandContext::RHIBeginFrame()
{
	@autoreleasepool {
        RHIPrivateBeginFrame();
#if ENABLE_METAL_GPUPROFILE
	Profiler->BeginFrame();
#endif
	((FMetalDeviceContext*)Context)->BeginFrame();
	}
}

void FMetalRHICommandContext::RHIBeginFrame()
{
	check(false);
}

void FMetalRHIImmediateCommandContext::RHIEndFrame()
{
	@autoreleasepool {
#if ENABLE_METAL_GPUPROFILE
	Profiler->EndFrame();
#endif
	((FMetalDeviceContext*)Context)->EndFrame();
	}
}

void FMetalRHICommandContext::RHIEndFrame()
{
	check(false);
}

void FMetalRHIImmediateCommandContext::RHIBeginScene()
{
	@autoreleasepool {
	((FMetalDeviceContext*)Context)->BeginScene();
	}
}

void FMetalRHICommandContext::RHIBeginScene()
{
	check(false);
}

void FMetalRHIImmediateCommandContext::RHIEndScene()
{
	@autoreleasepool {
	((FMetalDeviceContext*)Context)->EndScene();
	}
}

void FMetalRHICommandContext::RHIEndScene()
{
	check(false);
}

void FMetalRHICommandContext::RHIPushEvent(const TCHAR* Name, FColor Color)
{
#if ENABLE_METAL_GPUEVENTS
	@autoreleasepool
	{
		FPlatformMisc::BeginNamedEvent(Color, Name);
#if ENABLE_METAL_GPUPROFILE
		Profiler->PushEvent(Name, Color);
#endif
		Context->GetCurrentRenderPass().PushDebugGroup([NSString stringWithCString:TCHAR_TO_UTF8(Name) encoding:NSUTF8StringEncoding]);
	}
#endif
}

void FMetalRHICommandContext::RHIPopEvent()
{
#if ENABLE_METAL_GPUEVENTS
	@autoreleasepool {
	FPlatformMisc::EndNamedEvent();
	Context->GetCurrentRenderPass().PopDebugGroup();
#if ENABLE_METAL_GPUPROFILE
	Profiler->PopEvent();
#endif
	}
#endif
}

void FMetalDynamicRHI::RHIGetSupportedResolution( uint32 &Width, uint32 &Height )
{
#if PLATFORM_MAC
	CGDisplayModeRef DisplayMode = FPlatformApplicationMisc::GetSupportedDisplayMode(kCGDirectMainDisplay, Width, Height);
	if (DisplayMode)
	{
		Width = CGDisplayModeGetWidth(DisplayMode);
		Height = CGDisplayModeGetHeight(DisplayMode);
		CGDisplayModeRelease(DisplayMode);
	}
#else
	UE_LOG(LogMetal, Warning,  TEXT("RHIGetSupportedResolution unimplemented!"));
#endif
}

bool FMetalDynamicRHI::RHIGetAvailableResolutions(FScreenResolutionArray& Resolutions, bool bIgnoreRefreshRate)
{
#if PLATFORM_MAC
	const int32 MinAllowableResolutionX = 0;
	const int32 MinAllowableResolutionY = 0;
	const int32 MaxAllowableResolutionX = 10480;
	const int32 MaxAllowableResolutionY = 10480;
	const int32 MinAllowableRefreshRate = 0;
	const int32 MaxAllowableRefreshRate = 10480;
	
	CFArrayRef AllModes = CGDisplayCopyAllDisplayModes(kCGDirectMainDisplay, NULL);
	if (AllModes)
	{
		const int32 NumModes = CFArrayGetCount(AllModes);
		const int32 Scale = (int32)FMacApplication::GetPrimaryScreenBackingScaleFactor();
		
		for (int32 Index = 0; Index < NumModes; Index++)
		{
			const CGDisplayModeRef Mode = (const CGDisplayModeRef)CFArrayGetValueAtIndex(AllModes, Index);
			const int32 Width = (int32)CGDisplayModeGetWidth(Mode) / Scale;
			const int32 Height = (int32)CGDisplayModeGetHeight(Mode) / Scale;
			const int32 RefreshRate = (int32)CGDisplayModeGetRefreshRate(Mode);
			
			if (Width >= MinAllowableResolutionX && Width <= MaxAllowableResolutionX && Height >= MinAllowableResolutionY && Height <= MaxAllowableResolutionY)
			{
				bool bAddIt = true;
				if (bIgnoreRefreshRate == false)
				{
					if (RefreshRate < MinAllowableRefreshRate || RefreshRate > MaxAllowableRefreshRate)
					{
						continue;
					}
				}
				else
				{
					// See if it is in the list already
					for (int32 CheckIndex = 0; CheckIndex < Resolutions.Num(); CheckIndex++)
					{
						FScreenResolutionRHI& CheckResolution = Resolutions[CheckIndex];
						if ((CheckResolution.Width == Width) &&
							(CheckResolution.Height == Height))
						{
							// Already in the list...
							bAddIt = false;
							break;
						}
					}
				}
				
				if (bAddIt)
				{
					// Add the mode to the list
					const int32 Temp2Index = Resolutions.AddZeroed();
					FScreenResolutionRHI& ScreenResolution = Resolutions[Temp2Index];
					
					ScreenResolution.Width = Width;
					ScreenResolution.Height = Height;
					ScreenResolution.RefreshRate = RefreshRate;
				}
			}
		}
		
		CFRelease(AllModes);
	}
	
	return true;
#else
	UE_LOG(LogMetal, Warning,  TEXT("RHIGetAvailableResolutions unimplemented!"));
	return false;
#endif
}

void FMetalDynamicRHI::RHIFlushResources()
{
	@autoreleasepool {
		((FMetalDeviceContext*)ImmediateContext.Context)->FlushFreeList(false);
		ImmediateContext.Context->SubmitCommandBufferAndWait();
		((FMetalDeviceContext*)ImmediateContext.Context)->ClearFreeList();
        ((FMetalDeviceContext*)ImmediateContext.Context)->DrainHeap();
		ImmediateContext.Context->GetCurrentState().Reset();
	}
}

void FMetalDynamicRHI::RHIAcquireThreadOwnership()
{
	SetupRecursiveResources();
}

void FMetalDynamicRHI::RHIReleaseThreadOwnership()
{
}

void* FMetalDynamicRHI::RHIGetNativeDevice()
{
	return (void*)ImmediateContext.Context->GetDevice().GetPtr();
}

void* FMetalDynamicRHI::RHIGetNativeInstance()
{
	return nullptr;
}


uint16 FMetalDynamicRHI::RHIGetPlatformTextureMaxSampleCount()
{
	TArray<EMobileMSAASampleCount::Type> SamplesArray{ EMobileMSAASampleCount::Type::One, EMobileMSAASampleCount::Type::Two, EMobileMSAASampleCount::Type::Four, EMobileMSAASampleCount::Type::Eight };

	uint16 PlatformMaxSampleCount = EMobileMSAASampleCount::Type::One;
	for (auto sampleIt = SamplesArray.CreateConstIterator(); sampleIt; ++sampleIt)
	{
		int sample = *sampleIt;

#if PLATFORM_IOS || PLATFORM_MAC
		id<MTLDevice> Device = (id<MTLDevice>)RHIGetNativeDevice();
		check(Device);

		if (![Device supportsTextureSampleCount : sample])
		{
			break;
		}
		PlatformMaxSampleCount = sample;
#endif
	}
	return PlatformMaxSampleCount;
}
