// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AndroidDeviceProfileSelector.h"
#include "AndroidDeviceProfileMatchingRules.h"
#include "AndroidJavaSurfaceViewDevices.h"
#include "Templates/Casts.h"
#include "Internationalization/Regex.h"
#include "Misc/CommandLine.h"
#include "Misc/SecureHash.h"
#include "Containers/StringConv.h"

#if ANDROIDDEVICEPROFILESELECTORSECRETS_H
#include "NoRedist/AndroidDeviceProfileSelectorSecrets.h"
#endif

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

FString FAndroidDeviceProfileSelector::FindMatchingProfile(const FString& GPUFamily, const FString& GLVersion, const FString& AndroidVersion, const FString& DeviceMake, const FString& DeviceModel, const FString& DeviceBuildNumber, const FString& VulkanAvailable, const FString& VulkanVersion, const FString& UsingHoudini, const FString& Hardware, const FString& Chipset, const FString& ProfileName)
{
	FString OutProfileName = ProfileName;
	FString CommandLine = FCommandLine::Get();

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
			case SRC_DeviceBuildNumber:
				SourceString = &DeviceBuildNumber;
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
			case SRC_CommandLine:
				SourceString = &CommandLine;
				break;
			case SRC_Hardware:
				SourceString = &Hardware;
				break;
			case SRC_Chipset:
				SourceString = &Chipset;
				break;
			default:
				continue;
			}

			const bool bNumericOperands = SourceString->IsNumeric() && Item.MatchString.IsNumeric();

			switch (Item.CompareType)
			{
			case CMP_Equal:
				if (Item.SourceType == SRC_CommandLine) 
				{
					if (!FParse::Param(*CommandLine, *Item.MatchString))
					{
						bFoundMatch = false;
					}
				}
				else
				{
					if (*SourceString != Item.MatchString)
					{
						bFoundMatch = false;
					}
				}
				break;
			case CMP_Less:
				if ((bNumericOperands && FCString::Atof(**SourceString) >= FCString::Atof(*Item.MatchString)) || (!bNumericOperands && *SourceString >= Item.MatchString))
				{
					bFoundMatch = false;
				}
				break;
			case CMP_LessEqual:
				if ((bNumericOperands && FCString::Atof(**SourceString) > FCString::Atof(*Item.MatchString)) || (!bNumericOperands && *SourceString > Item.MatchString))
				{
					bFoundMatch = false;
				}
				break;
			case CMP_Greater:
				if ((bNumericOperands && FCString::Atof(**SourceString) <= FCString::Atof(*Item.MatchString)) || (!bNumericOperands && *SourceString <= Item.MatchString))
				{
					bFoundMatch = false;
				}
				break;
			case CMP_GreaterEqual:
				if ((bNumericOperands && FCString::Atof(**SourceString) < FCString::Atof(*Item.MatchString)) || (!bNumericOperands && *SourceString < Item.MatchString))
				{
					bFoundMatch = false;
				}
				break;
			case CMP_NotEqual:
				if (Item.SourceType == SRC_CommandLine)
				{
					if (FParse::Param(*CommandLine, *Item.MatchString))
					{
						bFoundMatch = false;
					}
				}
				else
				{
					if (*SourceString == Item.MatchString)
					{
						bFoundMatch = false;
					}
				}
				break;
			case CMP_EqualIgnore:
				if (SourceString->ToLower() != Item.MatchString.ToLower())
				{
					bFoundMatch = false;
				}
				break;
			case CMP_LessIgnore:
				if (SourceString->ToLower() >= Item.MatchString.ToLower())
				{
					bFoundMatch = false;
				}
				break;
			case CMP_LessEqualIgnore:
				if (SourceString->ToLower() > Item.MatchString.ToLower())
				{
					bFoundMatch = false;
				}
				break;
			case CMP_GreaterIgnore:
				if (SourceString->ToLower() <= Item.MatchString.ToLower())
				{
					bFoundMatch = false;
				}
				break;
			case CMP_GreaterEqualIgnore:
				if (SourceString->ToLower() < Item.MatchString.ToLower())
				{
					bFoundMatch = false;
				}
				break;
			case CMP_NotEqualIgnore:
				if (SourceString->ToLower() == Item.MatchString.ToLower())
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
				case CMP_Hash:
				{
					// Salt string is concatenated onto the end of the input text.
					// For example the input string "PhoneModel" with salt "Salt" and pepper "Pepper" can be computed with
					// % printf "PhoneModelSaltPepper" | openssl dgst -sha1 -hex
					// resulting in d9e5cbd6b0e4dba00edd9de92cf64ee4c3f3a2db and would be stored in the matching rules as 
					// "Salt|d9e5cbd6b0e4dba00edd9de92cf64ee4c3f3a2db". Salt is optional.
					FString MatchHashString;
					FString SaltString;
					if (!Item.MatchString.Split(TEXT("|"), &SaltString, &MatchHashString))
					{
						MatchHashString = Item.MatchString;
					}
					FString HashInputString = *SourceString + SaltString
#if ANDROIDDEVICEPROFILESELECTORSECRETS_H
						+ HASH_PEPPER_SECRET_GUID.ToString()
#endif
						;

					FSHAHash SourceHash;
					FSHA1::HashBuffer(TCHAR_TO_ANSI(*HashInputString), HashInputString.Len(), SourceHash.Hash);
					if (SourceHash.ToString() != MatchHashString)
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
