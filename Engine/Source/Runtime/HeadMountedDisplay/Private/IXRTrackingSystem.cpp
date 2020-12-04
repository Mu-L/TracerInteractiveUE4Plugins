// Copyright Epic Games, Inc. All Rights Reserved.

#include "IXRTrackingSystem.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"

void IXRTrackingSystem::GetHMDData(UObject* WorldContext, FXRHMDData& HMDData)
{
	HMDData.bValid = true;
	HMDData.DeviceName = GetSystemName();
	HMDData.ApplicationInstanceID = FApp::GetInstanceId();

	bool bIsTracking = IsTracking(IXRTrackingSystem::HMDDeviceId);
	HMDData.TrackingStatus = bIsTracking ? ETrackingStatus::Tracked : ETrackingStatus::NotTracked;

	APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(WorldContext, 0);
	if (CameraManager)
	{
		HMDData.Rotation = CameraManager->GetCameraRotation().Quaternion();
		HMDData.Position = CameraManager->GetCameraLocation();
	}
	//GetCurrentPose(0, HMDVisualizationData.Rotation, HMDVisualizationData.Position);
}

bool IXRTrackingSystem::IsHeadTrackingAllowedForWorld(UWorld& World) const
{
#if WITH_EDITOR
	// For VR PIE only the first instance uses the headset
	return IsHeadTrackingAllowed() && ((World.WorldType != EWorldType::PIE) || (World.GetOutermost()->PIEInstanceID == 0));
#else
	return IsHeadTrackingAllowed();
#endif
}
