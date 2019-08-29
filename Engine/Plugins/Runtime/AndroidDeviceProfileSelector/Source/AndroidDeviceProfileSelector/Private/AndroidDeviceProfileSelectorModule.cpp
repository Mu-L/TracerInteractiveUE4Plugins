// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "AndroidDeviceProfileSelectorModule.h"
#include "AndroidDeviceProfileSelector.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FAndroidDeviceProfileSelectorModule, AndroidDeviceProfileSelector);

void FAndroidDeviceProfileSelectorModule::StartupModule()
{
}

void FAndroidDeviceProfileSelectorModule::ShutdownModule()
{
}

const FString FAndroidDeviceProfileSelectorModule::GetRuntimeDeviceProfileName()
{
	// We are not expecting this module to have GetRuntimeDeviceProfileName called directly.
	// Android ProfileSelectorModule runtime is now in FAndroidDeviceProfileSelectorRuntimeModule.
	// Use GetDeviceProfileName.
	checkNoEntry();
	return FString();
}

const FString FAndroidDeviceProfileSelectorModule::GetDeviceProfileName(const TMap<FString, FString>& DeviceParameters)
{
	FString ProfileName; 

	// Pull out required device parameters:
	FString GPUFamily = DeviceParameters.FindChecked("GPUFamily");
	FString GLVersion = DeviceParameters.FindChecked("GLVersion");
	FString VulkanAvailable = DeviceParameters.FindChecked("VulkanAvailable");
	FString VulkanVersion = DeviceParameters.FindChecked("VulkanVersion");
	FString AndroidVersion = DeviceParameters.FindChecked("AndroidVersion");
	FString DeviceMake = DeviceParameters.FindChecked("DeviceMake");
	FString DeviceModel = DeviceParameters.FindChecked("DeviceModel");
	FString UsingHoudini = DeviceParameters.FindChecked("UsingHoudini");

	UE_LOG(LogAndroid, Log, TEXT("Checking %d rules from DeviceProfile ini file."), FAndroidDeviceProfileSelector::GetNumProfiles() );
	UE_LOG(LogAndroid, Log, TEXT("  Default profile: %s"), *ProfileName);
	UE_LOG(LogAndroid, Log, TEXT("  GpuFamily: %s"), *GPUFamily);
	UE_LOG(LogAndroid, Log, TEXT("  GlVersion: %s"), *GLVersion);
	UE_LOG(LogAndroid, Log, TEXT("  VulkanAvailable: %s"), *VulkanAvailable);
	UE_LOG(LogAndroid, Log, TEXT("  VulkanVersion: %s"), *VulkanVersion);
	UE_LOG(LogAndroid, Log, TEXT("  AndroidVersion: %s"), *AndroidVersion);
	UE_LOG(LogAndroid, Log, TEXT("  DeviceMake: %s"), *DeviceMake);
	UE_LOG(LogAndroid, Log, TEXT("  DeviceModel: %s"), *DeviceModel);
	UE_LOG(LogAndroid, Log, TEXT("  UsingHoudini: %s"), *UsingHoudini);

	ProfileName = FAndroidDeviceProfileSelector::FindMatchingProfile(GPUFamily, GLVersion, AndroidVersion, DeviceMake, DeviceModel, VulkanAvailable, VulkanVersion, UsingHoudini, ProfileName);

	UE_LOG(LogAndroid, Log, TEXT("Selected Device Profile: [%s]"), *ProfileName);

	return ProfileName;
}
