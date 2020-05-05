// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HoloLensTargetPlatform.cpp: Implements the FHoloLensTargetPlatform class.
=============================================================================*/

#include "HoloLensTargetPlatform.h"
#include "HoloLensTargetDevice.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopeLock.h"
#include "HttpModule.h"
#include "PlatformHttp.h"
#include "Interfaces/IHttpResponse.h"
#include "HoloLensPlatformEditor.h"
#include "GeneralProjectSettings.h"

DEFINE_LOG_CATEGORY(LogHoloLensTargetPlatform);

FHoloLensTargetPlatform::FHoloLensTargetPlatform()
{
	PlatformInfo = ::PlatformInfo::FindPlatformInfo(FName("HoloLens"));

#if WITH_ENGINE
	FConfigCacheIni::LoadLocalIniFile(EngineSettings, TEXT("Engine"), true, *PlatformName());
	TextureLODSettings = nullptr; // These are registered by the device profile system.
	StaticMeshLODSettings.Initialize(EngineSettings);
#endif

	DeviceDetector = IHoloLensDeviceDetector::Create();

	DeviceDetectedRegistration = DeviceDetector->OnDeviceDetected().AddRaw(this, &FHoloLensTargetPlatform::OnDeviceDetected);

	{
		DeviceDetector->StartDeviceDetection();
	}
}

FHoloLensTargetPlatform::~FHoloLensTargetPlatform()
{
	DeviceDetector->OnDeviceDetected().Remove(DeviceDetectedRegistration);
}

void FHoloLensTargetPlatform::GetAllDevices(TArray<ITargetDevicePtr>& OutDevices) const
{
	DeviceDetector->StartDeviceDetection();

	OutDevices.Reset();
	FScopeLock Lock(&DevicesLock);
	OutDevices = Devices;
}

ITargetDevicePtr FHoloLensTargetPlatform::GetDevice(const FTargetDeviceId& DeviceId)
{
	if (PlatformName() == DeviceId.GetPlatformName())
	{
		DeviceDetector->StartDeviceDetection();

		FScopeLock Lock(&DevicesLock);
		for (ITargetDevicePtr Device : Devices)
		{
			if (DeviceId == Device->GetId())
			{
				return Device;
			}
		}
	}


	return nullptr;
}

ITargetDevicePtr FHoloLensTargetPlatform::GetDefaultDevice() const
{
	DeviceDetector->StartDeviceDetection();

	FScopeLock Lock(&DevicesLock);
	for (ITargetDevicePtr RemoteDevice : Devices)
	{
		if (RemoteDevice->IsDefault())
		{
			return RemoteDevice;
		}
	}

	return nullptr;
}

bool FHoloLensTargetPlatform::SupportsFeature(ETargetPlatformFeatures Feature) const
{
	switch (Feature)
	{
	case ETargetPlatformFeatures::Packaging:
	case ETargetPlatformFeatures::UserCredentials:
	case ETargetPlatformFeatures::DeviceOutputLog:
		return true;
	default:
		return TTargetPlatformBase<FHoloLensPlatformProperties>::SupportsFeature(Feature);
	}
}

#if WITH_ENGINE

void FHoloLensTargetPlatform::GetReflectionCaptureFormats(TArray<FName>& OutFormats) const
{
	OutFormats.Add(FName(TEXT("FullHDR")));
	OutFormats.Add(FName(TEXT("EncodedHDR")));
}

void FHoloLensTargetPlatform::GetTextureFormats(const UTexture* InTexture, TArray< TArray<FName> >& OutFormats) const
{
	GetDefaultTextureFormatNamePerLayer(OutFormats.AddDefaulted_GetRef(), this, InTexture, EngineSettings, false);
}

void FHoloLensTargetPlatform::GetAllTextureFormats(TArray<FName>& OutFormats) const
{
	GetAllDefaultTextureFormats(this, OutFormats, false);
}

static FName NAME_PCD3D_ES3_1(TEXT("PCD3D_ES31"));
static FName NAME_PCD3D_SM5(TEXT("PCD3D_SM5"));

void FHoloLensTargetPlatform::GetAllPossibleShaderFormats(TArray<FName>& OutFormats) const
{
	OutFormats.AddUnique(NAME_PCD3D_ES3_1);
	OutFormats.AddUnique(NAME_PCD3D_SM5);
}

void FHoloLensTargetPlatform::GetAllTargetedShaderFormats(TArray<FName>& OutFormats) const
{
	OutFormats.AddUnique(NAME_PCD3D_ES3_1);
	OutFormats.AddUnique(NAME_PCD3D_SM5);
}

#endif

void FHoloLensTargetPlatform::OnDeviceDetected(const FHoloLensDeviceInfo& Info)
{
	FHoloLensDevicePtr NewDevice = MakeShared<FHoloLensTargetDevice, ESPMode::ThreadSafe>(*this, Info);
	{
		FScopeLock Lock(&DevicesLock);
		Devices.Add(NewDevice);
	}
	DeviceDiscoveredEvent.Broadcast(NewDevice.ToSharedRef());
}

bool FHoloLensTargetPlatform::SupportsBuildTarget(EBuildTargetType BuildTarget) const
{
	return BuildTarget == EBuildTargetType::Game;
}

bool FHoloLensTargetPlatform::IsSdkInstalled(bool bProjectHasCode, FString& OutDocumentationPath) const
{
	OutDocumentationPath = TEXT("Platforms/HoloLens/GettingStarted");

	const TArray<FHoloLensSDKVersion>& SDKVersions = FHoloLensSDKVersion::GetSDKVersions();
	return SDKVersions.Num() > 0;
}

int32 FHoloLensTargetPlatform::CheckRequirements(bool bProjectHasCode, EBuildConfiguration Configuration, bool bRequiresAssetNativization, FString& OutTutorialPath, FString& OutDocumentationPath, FText& CustomizedLogMessage) const
{
	OutDocumentationPath = TEXT("Platforms/HoloLens/GettingStarted");
	FString LocalErrors;

	int32 BuildStatus = ETargetPlatformReadyStatus::Ready;
	if (!IsSdkInstalled(bProjectHasCode, OutTutorialPath))
	{
		BuildStatus |= ETargetPlatformReadyStatus::SDKNotFound;
	}
	FString PublisherIdentityName = GetDefault<UGeneralProjectSettings>()->CompanyDistinguishedName;
	if (PublisherIdentityName.IsEmpty())
	{
		LocalErrors += TEXT("Missing Company Distinguished Name (See Project Settings).");
		BuildStatus |= ETargetPlatformReadyStatus::SigningKeyNotFound;
	}
	else
	{
		if (PublisherIdentityName.Contains(TEXT("CN=")) && PublisherIdentityName.Len() == 3)
		{
			LocalErrors += TEXT(" Malformed Company Distinguished Name (See Project Settings).");
			BuildStatus |= ETargetPlatformReadyStatus::SigningKeyNotFound;
		}
	}
	FString ProjectName = GetDefault<UGeneralProjectSettings>()->ProjectName;
	if (ProjectName.IsEmpty())
	{
		LocalErrors += TEXT(" Missing Project Name (See Project Settings).");
		BuildStatus |= ETargetPlatformReadyStatus::SigningKeyNotFound;
	}

	// Set the path if missing any of the bits needed for signing
	if (BuildStatus & ETargetPlatformReadyStatus::SigningKeyNotFound)
	{
		OutDocumentationPath = TEXT("Platforms/HoloLens/Signing");
	}

	if (BuildStatus != ETargetPlatformReadyStatus::Ready)
	{
		UE_LOG(LogHoloLensTargetPlatform, Warning, TEXT("FHoloLensTargetPlatform::CheckRequirements found these problems: %s"), *LocalErrors);
	}

	return BuildStatus;
}

bool FHoloLensTargetPlatform::AddDevice(const FString& DeviceId, const FString& DeviceUserFriendlyName, const FString& Username, const FString& Password, bool bDefault)
{
	DeviceDetector->TryAddDevice(DeviceId, DeviceUserFriendlyName, Username, Password);

	return true;
}
