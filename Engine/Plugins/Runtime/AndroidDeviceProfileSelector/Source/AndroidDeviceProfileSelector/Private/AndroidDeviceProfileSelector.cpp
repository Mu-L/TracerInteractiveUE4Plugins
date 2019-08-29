// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "AndroidDeviceProfileSelector.h"
#include "AndroidDeviceProfileMatchingRules.h"
#include "AndroidJavaSurfaceViewDevices.h"
#include "Templates/Casts.h"
#include "Internationalization/Regex.h"

UAndroidDeviceProfileMatchingRules::UAndroidDeviceProfileMatchingRules(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UAndroidJavaSurfaceViewDevices::UAndroidJavaSurfaceViewDevices(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

static UAndroidDeviceProfileMatchingRules* GetAndroidDeviceProfileMatchingRules()
{
	// We need to initialize the class early as device profiles need to be evaluated before ProcessNewlyLoadedUObjects can be called.
	extern UClass* Z_Construct_UClass_UAndroidDeviceProfileMatchingRules();
	CreatePackage(nullptr, UAndroidDeviceProfileMatchingRules::StaticPackage());
	Z_Construct_UClass_UAndroidDeviceProfileMatchingRules();

	// Get the default object which will has the values from DeviceProfiles.ini
	UAndroidDeviceProfileMatchingRules* Rules = Cast<UAndroidDeviceProfileMatchingRules>(UAndroidDeviceProfileMatchingRules::StaticClass()->GetDefaultObject());
	check(Rules);
	return Rules;
}

FString FAndroidDeviceProfileSelector::FindMatchingProfile(const FString& GPUFamily, const FString& GLVersion, const FString& AndroidVersion, const FString& DeviceMake, const FString& DeviceModel, const FString& VulkanAvailable, const FString& VulkanVersion, const FString& UsingHoudini, const FString& ProfileName)
{
	FString OutProfileName = ProfileName;

	for (const FProfileMatch& Profile : GetAndroidDeviceProfileMatchingRules()->MatchProfile)
	{
		FString PreviousRegexMatch;
		bool bFoundMatch = true;
		for (const FProfileMatchItem& Item : Profile.Match)
		{
			const FString* SourceString = nullptr;
			switch (Item.SourceType)
			{
			case SRC_PreviousRegexMatch:
				SourceString = &PreviousRegexMatch;
				break;
			case SRC_GpuFamily:
				SourceString = &GPUFamily;
				break;
			case SRC_GlVersion:
				SourceString = &GLVersion;
				break;
			case SRC_AndroidVersion:
				SourceString = &AndroidVersion;
				break;
			case SRC_DeviceMake:
				SourceString = &DeviceMake;
				break;
			case SRC_DeviceModel:
				SourceString = &DeviceModel;
				break;
			case SRC_VulkanVersion:
				SourceString = &VulkanVersion;
				break;
			case SRC_UsingHoudini:
				SourceString = &UsingHoudini;
				break;
			case SRC_VulkanAvailable:
				SourceString = &VulkanAvailable;
				break;
			default:
				continue;
			}

			switch (Item.CompareType)
			{
			case CMP_Equal:
				if (*SourceString != Item.MatchString)
				{
					bFoundMatch = false;
				}
				break;
			case CMP_Less:
				if (FPlatformString::Atoi(**SourceString) >= FPlatformString::Atoi(*Item.MatchString))
				{
					bFoundMatch = false;
				}
				break;
			case CMP_LessEqual:
				if (FPlatformString::Atoi(**SourceString) > FPlatformString::Atoi(*Item.MatchString))
				{
					bFoundMatch = false;
				}
				break;
			case CMP_Greater:
				if (FPlatformString::Atoi(**SourceString) <= FPlatformString::Atoi(*Item.MatchString))
				{
					bFoundMatch = false;
				}
				break;
			case CMP_GreaterEqual:
				if (FPlatformString::Atoi(**SourceString) < FPlatformString::Atoi(*Item.MatchString))
				{
					bFoundMatch = false;
				}
				break;
			case CMP_NotEqual:
				if (*SourceString == Item.MatchString)
				{
					bFoundMatch = false;
				}
				break;
			case CMP_Regex:
			{
				const FRegexPattern RegexPattern(Item.MatchString);
				FRegexMatcher RegexMatcher(RegexPattern, *SourceString);
				if (RegexMatcher.FindNext())
				{
					PreviousRegexMatch = RegexMatcher.GetCaptureGroup(1);
				}
				else
				{
					bFoundMatch = false;
				}
			}
			break;
			default:
				bFoundMatch = false;
			}

			if (!bFoundMatch)
			{
				break;
			}
		}

		if (bFoundMatch)
		{
			OutProfileName = Profile.Profile;
			break;
		}
	}
	return OutProfileName;
}

int32 FAndroidDeviceProfileSelector::GetNumProfiles()
{
	return GetAndroidDeviceProfileMatchingRules()->MatchProfile.Num();
}
