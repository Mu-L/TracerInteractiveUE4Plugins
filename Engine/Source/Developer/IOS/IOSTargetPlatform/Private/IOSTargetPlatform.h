// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	IOSTargetPlatform.h: Declares the FIOSTargetPlatform class.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Common/TargetPlatformBase.h"
#include "IOS/IOSPlatformProperties.h"
#include "Containers/Ticker.h"
#include "IOSMessageProtocol.h"
#include "IMessageContext.h"
#include "IOSTargetDevice.h"
#include "IOSDeviceHelper.h"
#include "Misc/ConfigCacheIni.h"


#if WITH_ENGINE
#include "AudioCompressionSettings.h"
#include "StaticMeshResources.h"
#endif // WITH_ENGINE

/**
 * FIOSTargetPlatform, abstraction for cooking iOS platforms
 */
class FIOSTargetPlatform : public TTargetPlatformBase<FIOSPlatformProperties>
{
public:

	/**
	 * Default constructor.
	 */
	IOSTARGETPLATFORM_API FIOSTargetPlatform(bool bInISTVOS, bool bInIsClientOnly);

	/**
	 * Destructor.
	 */
	~FIOSTargetPlatform();

public:

	//~ Begin TTargetPlatformBase Interface

	virtual bool IsServerOnly( ) const override
	{
		return false;
	}

	virtual bool IsClientOnly() const override
	{
		return bIsClientOnly;
	}

	//~ End TTargetPlatformBase Interface

public:

	//~ Begin ITargetPlatform Interface
	
	// this is used for cooking to a separate directory, NOT for runtime. Runtime TVOS is still "IOS"
	virtual FString PlatformName() const override
	{
		if (bIsTVOS)
		{
			return bIsClientOnly ? "TVOSClient" : "TVOS";
		}
		return bIsClientOnly ? "IOSClient" : "IOS";
	}

    virtual FString IniPlatformName() const override
    {
        return "IOS";
    }
    
	virtual void EnableDeviceCheck(bool OnOff) override;

	virtual void GetAllDevices( TArray<ITargetDevicePtr>& OutDevices ) const override;

	virtual bool GenerateStreamingInstallManifest(const TMultiMap<FString, int32>& PakchunkMap, const TSet<int32>& PakchunkIndicesInUse) const override
	{
		return true;
	}

	virtual ITargetDevicePtr GetDefaultDevice( ) const override;

	virtual ITargetDevicePtr GetDevice( const FTargetDeviceId& DeviceId ) override;

	virtual bool IsRunningPlatform( ) const override
	{
		#if PLATFORM_IOS && WITH_EDITOR
			return true;
		#else
			return false;
		#endif
	}

	virtual bool SupportsFeature( ETargetPlatformFeatures Feature ) const override;

	virtual bool CanSupportXGEShaderCompile() const override;

	virtual bool IsSdkInstalled(bool bProjectHasCode, FString& OutTutorialPath) const override;
	virtual int32 CheckRequirements(const FString& ProjectPath, bool bProjectHasCode, FString& OutTutorialPath, FString& OutDocumenationPath, FText& CustomizedLogMessage) const override;


#if WITH_ENGINE
	virtual void GetReflectionCaptureFormats( TArray<FName>& OutFormats ) const override
	{
		OutFormats.Add(FName(TEXT("EncodedHDR")));
		OutFormats.Add(FName(TEXT("FullHDR")));
	}

	virtual void GetAllPossibleShaderFormats( TArray<FName>& OutFormats ) const override;

	virtual void GetAllTargetedShaderFormats( TArray<FName>& OutFormats ) const override;
	virtual const class FStaticMeshLODSettings& GetStaticMeshLODSettings( ) const override
	{
		return StaticMeshLODSettings;
	}

	virtual void GetTextureFormats( const UTexture* Texture, TArray< TArray<FName> >& OutFormats) const override;

	virtual void GetAllTextureFormats( TArray<FName>& OutFormats) const override;

	virtual const UTextureLODSettings& GetTextureLODSettings() const override;

	virtual void RegisterTextureLODSettings(const UTextureLODSettings* InTextureLODSettings) override
	{
		TextureLODSettings = InTextureLODSettings;
	}

	virtual FName GetWaveFormat( const class USoundWave* Wave ) const override;
	virtual void GetAllWaveFormats(TArray<FName>& OutFormat) const override;

	virtual FPlatformAudioCookOverrides* GetAudioCompressionSettings() const override;

#endif // WITH_ENGINE

	virtual void GetBuildProjectSettingKeys(FString& OutSection, TArray<FString>& InBoolKeys, TArray<FString>& InIntKeys, TArray<FString>& InStringKeys) const override
	{
		OutSection = TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings");
		InBoolKeys.Add(TEXT("EnableRemoteShaderCompile"));
		InBoolKeys.Add(TEXT("bGeneratedSYMFile"));
		InBoolKeys.Add(TEXT("bGeneratedSYMBundle"));
		InBoolKeys.Add(TEXT("bGenerateXCArchive"));
		InBoolKeys.Add(TEXT("bShipForBitcode"));
		if (bIsTVOS)
		{
			InStringKeys.Add(TEXT("MinimumTVOSVersion"));
		}
		else
		{
			InStringKeys.Add(TEXT("MinimumiOSVersion"));
			InBoolKeys.Add(TEXT("bDevForArmV7")); InBoolKeys.Add(TEXT("bDevForArm64")); InBoolKeys.Add(TEXT("bDevForArmV7S"));
			InBoolKeys.Add(TEXT("bShipForArmV7")); InBoolKeys.Add(TEXT("bShipForArm64")); InBoolKeys.Add(TEXT("bShipForArmV7S"));
		}
	}

	DECLARE_DERIVED_EVENT(FIOSTargetPlatform, ITargetPlatform::FOnTargetDeviceDiscovered, FOnTargetDeviceDiscovered);
	virtual FOnTargetDeviceDiscovered& OnDeviceDiscovered( ) override
	{
		return DeviceDiscoveredEvent;
	}

	DECLARE_DERIVED_EVENT(FIOSTargetPlatform, ITargetPlatform::FOnTargetDeviceLost, FOnTargetDeviceLost);
	virtual FOnTargetDeviceLost& OnDeviceLost( ) override
	{
		return DeviceLostEvent;
	}

	//~ Begin ITargetPlatform Interface

protected:

	/**
	 * Sends a ping message over the network to find devices running the launch daemon.
	 */
	void PingNetworkDevices( );

private:

	// Handles when the ticker fires.
	bool HandleTicker( float DeltaTime );

	// Handles received pong messages from the LauncherDaemon.
	void HandlePongMessage( const FIOSLaunchDaemonPong& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context );

    void HandleDeviceConnected( const FIOSLaunchDaemonPong& Message );
    void HandleDeviceDisconnected( const FIOSLaunchDaemonPong& Message );

private:
	
	// true if this is targeting TVOS vs IOS
	bool bIsTVOS;

	// true if this is a client-only TP
	bool bIsClientOnly;

	// Contains all discovered IOSTargetDevices over the network.
	TMap<FTargetDeviceId, FIOSTargetDevicePtr> Devices;

	// Holds a delegate to be invoked when the widget ticks.
	FTickerDelegate TickDelegate;

	// Handle to the registered TickDelegate.
	FDelegateHandle TickDelegateHandle;

	// Holds the message endpoint used for communicating with the LaunchDaemon.
	TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> MessageEndpoint;

#if WITH_ENGINE
	// Holds the Engine INI settings, for quick use.
	FConfigFile EngineSettings;

	// Holds the cache of the target LOD settings.
	const UTextureLODSettings* TextureLODSettings;

	// Holds the static mesh LOD settings.
	FStaticMeshLODSettings StaticMeshLODSettings;
#endif // WITH_ENGINE

    // holds usb device helper
	FIOSDeviceHelper DeviceHelper;

private:

	// Holds an event delegate that is executed when a new target device has been discovered.
	FOnTargetDeviceDiscovered DeviceDiscoveredEvent;

	// Holds an event delegate that is executed when a target device has been lost, i.e. disconnected or timed out.
	FOnTargetDeviceLost DeviceLostEvent;
};
