// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.


/*=============================================================================================
	AndroidMisc.h: Android platform misc functions
==============================================================================================*/

#pragma once
#include "CoreTypes.h"
#include "Android/AndroidSystemIncludes.h"
#include "GenericPlatform/GenericPlatformMisc.h"
//@todo android: this entire file

template <typename FuncType>
class TFunction;

#if UE_BUILD_SHIPPING
#define UE_DEBUG_BREAK() ((void)0)
#else
#define UE_DEBUG_BREAK() (FAndroidMisc::DebugBreakInternal())
#endif

/**
 * Android implementation of the misc OS functions
 */
struct CORE_API FAndroidMisc : public FGenericPlatformMisc
{
	static void RequestExit( bool Force );
	static void LowLevelOutputDebugString(const TCHAR *Message);
	static void LocalPrint(const TCHAR *Message);
	static void PlatformPreInit();
	static void PlatformInit();
	static void PlatformTearDown();
	static void PlatformHandleSplashScreen(bool ShowSplashScreen);
	static void GetEnvironmentVariable(const TCHAR* VariableName, TCHAR* Result, int32 ResultLength);
	static const TCHAR* GetSystemErrorMessage(TCHAR* OutBuffer, int32 BufferCount, int32 Error);
	static EAppReturnType::Type MessageBoxExt( EAppMsgType::Type MsgType, const TCHAR* Text, const TCHAR* Caption );
	static bool UseRenderThread();
	static bool HasPlatformFeature(const TCHAR* FeatureName);
	static bool ShouldDisablePluginAtRuntime(const FString& PluginName);
	static void SetThreadName(const char* name);
	static bool SupportsES30();

public:

	static bool AllowThreadHeartBeat()
	{
		return false;
	}

	struct FCPUStatTime{
		uint64_t			TotalTime;
		uint64_t			UserTime;
		uint64_t			NiceTime;
		uint64_t			SystemTime;
		uint64_t			SoftIRQTime;
		uint64_t			IRQTime;
		uint64_t			IdleTime;
		uint64_t			IOWaitTime;
	};

	struct FCPUState
	{
		const static int32			MaxSupportedCores = 16; //Core count 16 is maximum for now
		int32						CoreCount;
		int32						ActivatedCoreCount;
		ANSICHAR					Name[6];
		FAndroidMisc::FCPUStatTime	CurrentUsage[MaxSupportedCores]; 
		FAndroidMisc::FCPUStatTime	PreviousUsage[MaxSupportedCores];
		int32						Status[MaxSupportedCores];
		double						Utilization[MaxSupportedCores];
		double						AverageUtilization;
		
	};

	static FCPUState& GetCPUState();
	static int32 NumberOfCores();
	static int32 NumberOfCoresIncludingHyperthreads();
	static bool SupportsLocalCaching();
	static void SetCrashHandler(void (* CrashHandler)(const FGenericCrashContext& Context));
	// NOTE: THIS FUNCTION IS DEFINED IN ANDROIDOPENGL.CPP
	static void GetValidTargetPlatforms(class TArray<class FString>& TargetPlatformNames);
	static bool GetUseVirtualJoysticks();
	static bool SupportsTouchInput();
	static bool IsStandaloneStereoOnlyDevice();
	static const TCHAR* GetDefaultDeviceProfileName() { return TEXT("Android_Default"); }
	static bool GetVolumeButtonsHandledBySystem();
	static void SetVolumeButtonsHandledBySystem(bool enabled);
	// Returns current volume, 0-15
	static int GetVolumeState(double* OutTimeOfChangeInSec = nullptr);

#if USE_ANDROID_FILE
	static const TCHAR* GamePersistentDownloadDir();
	static FString GetLoginId();
#endif
#if USE_ANDROID_JNI
	static FString GetDeviceId();
	static FString GetUniqueAdvertisingId();
#endif
	static FString GetCPUVendor();
	static FString GetCPUBrand();
	static FString GetPrimaryGPUBrand();
	static void GetOSVersions(FString& out_OSVersionLabel, FString& out_OSSubVersionLabel);
	static bool GetDiskTotalAndFreeSpace(const FString& InPath, uint64& TotalNumberOfBytes, uint64& NumberOfFreeBytes);
	
	enum EBatteryState
	{
		BATTERY_STATE_UNKNOWN = 1,
		BATTERY_STATE_CHARGING,
		BATTERY_STATE_DISCHARGING,
		BATTERY_STATE_NOT_CHARGING,
		BATTERY_STATE_FULL
	};
	struct FBatteryState
	{
		FAndroidMisc::EBatteryState	State;
		int							Level;          // in range [0,100]
		float						Temperature;    // in degrees of Celsius
	};

	static FBatteryState GetBatteryState();
	static int GetBatteryLevel();
	static bool IsRunningOnBattery();
	static float GetDeviceTemperatureLevel();
	static bool AreHeadPhonesPluggedIn();
	static ENetworkConnectionType GetNetworkConnectionType();
#if USE_ANDROID_JNI
	static bool HasActiveWiFiConnection();
#endif

	static void RegisterForRemoteNotifications();
	static void UnregisterForRemoteNotifications();

	/** @return Memory representing a true type or open type font provided by the platform as a default font for unreal to consume; empty array if the default font failed to load. */
	static TArray<uint8> GetSystemFontBytes();

	static IPlatformChunkInstall* GetPlatformChunkInstall();

	static void PrepareMobileHaptics(EMobileHapticsType Type);
	static void TriggerMobileHaptics();
	static void ReleaseMobileHaptics();
	static void ShareURL(const FString& URL, const FText& Description, int32 LocationHintX, int32 LocationHintY);

	// ANDROID ONLY:
	static void SetVersionInfo( FString AndroidVersion, FString DeviceMake, FString DeviceModel, FString OSLanguage );
	static const FString GetAndroidVersion();
	static const FString GetDeviceMake();
	static const FString GetDeviceModel();
	static const FString GetOSLanguage();
	static FString GetDefaultLocale();
	static FString GetGPUFamily();
	static FString GetGLVersion();
	static bool SupportsFloatingPointRenderTargets();
	static bool SupportsShaderFramebufferFetch();
	static bool SupportsShaderIOBlocks();
#if USE_ANDROID_JNI
	static int GetAndroidBuildVersion();
#endif

	/* HasVulkanDriverSupport
	 * @return true if this Android device supports a Vulkan API Unreal could use
	 */
	static bool HasVulkanDriverSupport();

	/* IsVulkanAvailable
	 * @return	true if there is driver support, we have an RHI, we are packaged with Vulkan support,
	 *			and not we are not forcing GLES with a command line switch
	 */
	static bool IsVulkanAvailable();

	/* ShouldUseVulkan
	 * @return true if Vulkan is available, and not disabled by device profile cvar
	 */
	static bool ShouldUseVulkan();
	static bool ShouldUseDesktopVulkan();
	static FString GetVulkanVersion();
	static bool IsDaydreamApplication();
	typedef TFunction<void(void* NewNativeHandle)> ReInitWindowCallbackType;
	static ReInitWindowCallbackType GetOnReInitWindowCallback();
	static void SetOnReInitWindowCallback(ReInitWindowCallbackType InOnReInitWindowCallback);
	static FString GetOSVersion();
	static bool GetOverrideResolution(int32 &ResX, int32& ResY) { return false; }

#if !UE_BUILD_SHIPPING
	static bool IsDebuggerPresent();

	FORCEINLINE static void DebugBreakInternal()
	{
		if( IsDebuggerPresent() )
		{
#if PLATFORM_ANDROID_ARM64
			__asm__(".inst 0xd4200000");
#elif PLATFORM_ANDROID_ARM
			__asm__("trap");
#else
			__asm__("int $3");
#endif
		}
	}

	DEPRECATED(4.19, "FPlatformMisc::DebugBreak is deprecated. Use the UE_DEBUG_BREAK() macro instead.")
	FORCEINLINE static void DebugBreak()
	{
		DebugBreakInternal();
	}
#endif

	/** Break into debugger. Returning false allows this function to be used in conditionals. */
	DEPRECATED(4.19, "FPlatformMisc::DebugBreakReturningFalse is deprecated. Use the (UE_DEBUG_BREAK(), false) expression instead.")
	FORCEINLINE static bool DebugBreakReturningFalse()
	{
#if !UE_BUILD_SHIPPING
		UE_DEBUG_BREAK();
#endif
		return false;
	}

	/** Prompts for remote debugging if debugger is not attached. Regardless of result, breaks into debugger afterwards. Returns false for use in conditionals. */
	DEPRECATED(4.19, "FPlatformMisc::DebugBreakAndPromptForRemoteReturningFalse() is deprecated.")
	static FORCEINLINE bool DebugBreakAndPromptForRemoteReturningFalse(bool bIsEnsure = false)
	{
#if !UE_BUILD_SHIPPING
		if (!IsDebuggerPresent())
		{
			PromptForRemoteDebugging(bIsEnsure);
		}

		UE_DEBUG_BREAK();
#endif

		return false;
	}

	FORCEINLINE static void MemoryBarrier()
	{
		__sync_synchronize();
	}


#if STATS || ENABLE_STATNAMEDEVENTS
	static void BeginNamedEventFrame();
	static void BeginNamedEvent(const struct FColor& Color, const TCHAR* Text);
	static void BeginNamedEvent(const struct FColor& Color, const ANSICHAR* Text);
	static void EndNamedEvent();
	static void CustomNamedStat(const TCHAR* Text, float Value, const TCHAR* Graph, const TCHAR* Unit);
	static void CustomNamedStat(const ANSICHAR* Text, float Value, const ANSICHAR* Graph, const ANSICHAR* Unit);
#endif

#if (STATS || ENABLE_STATNAMEDEVENTS)
	static int32 TraceMarkerFileDescriptor;
#endif
	
	// run time compatibility information
	static FString AndroidVersion; // version of android we are running eg "4.0.4"
	static FString DeviceMake; // make of the device we are running on eg. "samsung"
	static FString DeviceModel; // model of the device we are running on eg "SAMSUNG-SGH-I437"
	static FString OSLanguage; // language code the device is set to

	// Build version of Android, i.e. API level.
	static int32 AndroidBuildVersion;

	static bool VolumeButtonsHandledBySystem;

	enum class ECoreFrequencyProperty
	{
		CurrentFrequency,
		MaxFrequency,
		MinFrequency,
	};

	static uint32 GetCoreFrequency(int32 CoreIndex, ECoreFrequencyProperty CoreFrequencyProperty);
};

#if !PLATFORM_LUMIN
typedef FAndroidMisc FPlatformMisc;
#endif
