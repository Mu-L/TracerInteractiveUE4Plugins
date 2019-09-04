// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================================
	IOSPlatformMisc.h: iOS platform misc functions
==============================================================================================*/

#pragma once
#include "GenericPlatform/GenericPlatformMisc.h"
#include "IOS/IOSSystemIncludes.h"
#include "Apple/ApplePlatformMisc.h"

template <typename FuncType>
class TFunction;

/**
* iOS implementation of the misc OS functions
**/
struct CORE_API FIOSPlatformMisc : public FApplePlatformMisc
{
    static void PlatformPreInit();
	static void PlatformInit();
    static void PlatformHandleSplashScreen(bool ShowSplashScreen = false);
	static const TCHAR* GetPlatformFeaturesModuleName();

	FORCEINLINE static int32 GetMaxPathLength()
	{
		return IOS_MAX_PATH;
	}

	static bool AllowThreadHeartBeat()
	{
		return false;
	}

	static EAppReturnType::Type MessageBoxExt( EAppMsgType::Type MsgType, const TCHAR* Text, const TCHAR* Caption );
	static void SetMemoryWarningHandler(void (* Handler)(const FGenericMemoryWarningContext& Context));
	static bool HasPlatformFeature(const TCHAR* FeatureName);
	static bool SetStoredValue(const FString& InStoreId, const FString& InSectionName, const FString& InKeyName, const FString& InValue);
	static bool GetStoredValue(const FString& InStoreId, const FString& InSectionName, const FString& InKeyName, FString& OutValue);
	static bool DeleteStoredValue(const FString& InStoreId, const FString& InSectionName, const FString& InKeyName);
	static void GetValidTargetPlatforms(class TArray<class FString>& TargetPlatformNames);
	static ENetworkConnectionType GetNetworkConnectionType();
	static bool HasActiveWiFiConnection();
	static const TCHAR* GamePersistentDownloadDir();
    static bool HasSeparateChannelForDebugOutput();

	UE_DEPRECATED(4.21, "Use GetDeviceVolume, it is now callable on all platforms.")
	static int GetAudioVolume();

	static bool AreHeadphonesPluggedIn();
	static int GetBatteryLevel();
	static bool IsRunningOnBattery();
	static float GetDeviceTemperatureLevel();
	static EDeviceScreenOrientation GetDeviceOrientation();
	static int32 GetDeviceVolume();
	static void SetBrightness(float Brightness);
	static float GetBrightness();
	static void ResetBrightness(); //reset brightness to original value the application started with
	static bool SupportsBrightness() { return true; }
    static bool IsInLowPowerMode();

	//////// Notifications
	static void RegisterForRemoteNotifications();
	static bool IsRegisteredForRemoteNotifications();
	static void UnregisterForRemoteNotifications();
	// Check if notifications are allowed if min iOS version is < 10
	UE_DEPRECATED(4.21, "IsAllowedRemoteNotifications is deprecated. Use FIOSLocalNotificationService::CheckAllowedNotifications instead.")
	static bool IsAllowedRemoteNotifications();
	
	static class IPlatformChunkInstall* GetPlatformChunkInstall();

	static bool SupportsForceTouchInput();

	static void PrepareMobileHaptics(EMobileHapticsType Type);
	static void TriggerMobileHaptics();
	static void ReleaseMobileHaptics();

	static void ShareURL(const FString& URL, const FText& Description, int32 LocationHintX, int32 LocationHintY);

	static FString LoadTextFileFromPlatformPackage(const FString& RelativePath);

	static void EnableVoiceChat(bool bEnable);
	static bool IsVoiceChatEnabled();

	//////// Platform specific
	static int GetDefaultStackSize();
	static void HandleLowMemoryWarning();
	static bool IsPackagedForDistribution();
	UE_DEPRECATED(4.14, "GetUniqueDeviceId is deprecated. Use GetDeviceId instead.")
	static FString GetUniqueDeviceId();
	/**
	 * Implemented using UIDevice::identifierForVendor,
	 * so all the caveats that apply to that API call apply here.
	 */
	static FString GetDeviceId();
	static FString GetOSVersion();
	static FString GetUniqueAdvertisingId();
	static bool GetDiskTotalAndFreeSpace(const FString& InPath, uint64& TotalNumberOfBytes, uint64& NumberOfFreeBytes);
	
	static void RequestStoreReview();

	// Possible iOS devices
	enum EIOSDevice
	{
		// add new devices to the top, and add to IOSDeviceNames below!
		IOS_IPhone4,
		IOS_IPhone4S,
		IOS_IPhone5, // also the IPhone5C
		IOS_IPhone5S,
		IOS_IPodTouch5,
		IOS_IPodTouch6,
		IOS_IPad2,
		IOS_IPad3,
		IOS_IPad4,
		IOS_IPadMini,
		IOS_IPadMini2, // also the iPadMini3
		IOS_IPadMini4,
		IOS_IPadAir,
		IOS_IPadAir2,
		IOS_IPhone6,
		IOS_IPhone6Plus,
		IOS_IPhone6S,
		IOS_IPhone6SPlus,
        IOS_IPhone7,
        IOS_IPhone7Plus,
		IOS_IPhone8,
		IOS_IPhone8Plus,
		IOS_IPhoneX,
		IOS_IPadPro,
		IOS_AppleTV,
		IOS_AppleTV4K,
		IOS_IPhoneSE,
		IOS_IPadPro_129,
		IOS_IPadPro_97,
		IOS_IPadPro_105,
		IOS_IPadPro2_129,
		IOS_IPad5,
        IOS_IPhoneXS,
        IOS_IPhoneXSMax,
        IOS_IPhoneXR,
		IOS_IPad6,
		IOS_IPadPro_11,
		IOS_IPadPro3_129,
        IOS_IPadAir3,
        IOS_IPadMini5,
		IOS_IPodTouch7,
		IOS_Unknown,
	};

	static EIOSDevice GetIOSDeviceType();

	static FORCENOINLINE const TCHAR* GetDefaultDeviceProfileName()
	{
		static const TCHAR* IOSDeviceNames[] = 
		{
			TEXT("IPhone4"),
			TEXT("IPhone4S"),
			TEXT("IPhone5"),
			TEXT("IPhone5S"),
			TEXT("IPodTouch5"),
			TEXT("IPodTouch6"),
			TEXT("IPad2"),
			TEXT("IPad3"),
			TEXT("IPad4"),
			TEXT("IPadMini"),
			TEXT("IPadMini2"),
			TEXT("IPadMini4"),
			TEXT("IPadAir"),
			TEXT("IPadAir2"),
			TEXT("IPhone6"),
			TEXT("IPhone6Plus"),
			TEXT("IPhone6S"),
			TEXT("IPhone6SPlus"),
			TEXT("IPhone7"),
			TEXT("IPhone7Plus"),
			TEXT("IPhone8"),
			TEXT("IPhone8Plus"),
			TEXT("IPhoneX"),
			TEXT("IPadPro"),
			TEXT("AppleTV"),
			TEXT("AppleTV4K"),
			TEXT("IPhoneSE"),
			TEXT("IPadPro129"),
			TEXT("IPadPro97"),
			TEXT("IPadPro105"),
			TEXT("IPadPro2_129"),
			TEXT("IPad5"),
            TEXT("IPhoneXS"),
            TEXT("IPhoneXSMax"),
            TEXT("IPhoneXR"),
			TEXT("IPad6"),
			TEXT("IPadPro11"),
			TEXT("IPadPro3_129"),
            TEXT("IPadAir3"),
            TEXT("IPadMini5"),
			TEXT("IPodTouch7"),
			TEXT("Unknown"),
		};
		static_assert((sizeof(IOSDeviceNames) / sizeof(IOSDeviceNames[0])) == ((int32)IOS_Unknown + 1), "Mismatched IOSDeviceNames and EIOSDevice.");
		
		// look up into the string array by the enum
		return IOSDeviceNames[(int32)GetIOSDeviceType()];
	}

	static FString GetCPUVendor();
	static FString GetCPUBrand();
	static void GetOSVersions(FString& out_OSVersionLabel, FString& out_OSSubVersionLabel);
	static int32 IOSVersionCompare(uint8 Major, uint8 Minor, uint8 Revision);
	static FString GetProjectVersion();

	static void SetGracefulTerminationHandler();
	static void SetCrashHandler(void(*CrashHandler)(const FGenericCrashContext& Context));

	static bool SupportsDeviceCheckToken()
	{
		return true;
	}

	static bool RequestDeviceCheckToken(TFunction<void(const TArray<uint8>&)> QuerySucceededFunc, TFunction<void(const FString&, const FString&)> QueryFailedFunc);
    
    // added these for now because Crashlytics doesn't properly break up different callstacks all ending in UE_LOG(LogXXX, Fatal, ...)
    static FORCENOINLINE CA_NO_RETURN void GPUAssert();
    static FORCENOINLINE CA_NO_RETURN void MetalAssert();
};

typedef FIOSPlatformMisc FPlatformMisc;
