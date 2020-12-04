// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "IOpenXREyeTrackerModule.h"
#include "EyeTrackerTypes.h"
#include "IEyeTracker.h"
#include "Containers/Ticker.h"
#include "IOpenXRExtensionPlugin.h"
#include "GameFramework/HUD.h"

#include "OpenXRCore.h"

class FOpenXREyeTracker : public IEyeTracker, public IOpenXRExtensionPlugin
{
public:
	FOpenXREyeTracker();
	virtual ~FOpenXREyeTracker();

	/************************************************************************/
	/* IEyeTracker                                                          */
	/************************************************************************/
	void Destroy();
	virtual void SetEyeTrackedPlayer(APlayerController* PlayerController) override {};
	virtual bool GetEyeTrackerGazeData(FEyeTrackerGazeData& OutGazeData) const override;
	virtual bool GetEyeTrackerStereoGazeData(FEyeTrackerStereoGazeData& OutGazeData) const override;
	virtual EEyeTrackerStatus GetEyeTrackerStatus() const override;
	virtual bool IsStereoGazeDataAvailable() const override;
	
	/************************************************************************/
	/* IOpenXRExtensionPlugin                                               */
	/************************************************************************/

	virtual bool GetRequiredExtensions(TArray<const ANSICHAR*>& OutExtensions) override;
	virtual bool GetInteractionProfile(XrInstance InInstance, FString& OutKeyPrefix, XrPath& OutPath, bool& OutHasHaptics) override;
	virtual void AddActions(XrInstance Instance, TFunction<XrAction(XrActionType InActionType, const FName& InName, const TArray<XrPath>& InSubactionPaths)> AddAction) override;

	virtual const void* OnBeginSession(XrSession InSession, const void* InNext) override;
	virtual void PostSyncActions(XrSession InSession) override;
	virtual void UpdateDeviceLocations(XrSession InSession, XrTime DisplayTime, XrSpace TrackingSpace) override;

	void DrawDebug(AHUD* HUD, UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo, float& YL, float& YPos);

private:
	class IXRTrackingSystem* XRTrackingSystem = nullptr;

	bool bSessionStarted = false;
	XrActionsSyncInfo SyncInfo{ XR_TYPE_ACTIONS_SYNC_INFO };
	XrAction EyeTrackerAction;
	XrSpace GazeActionSpace;
	XrActionStatePose ActionStatePose{ XR_TYPE_ACTION_STATE_POSE };

	// EyeTracker cached data
	XrSpaceLocation EyeTrackerSpaceLocation{ XR_TYPE_SPACE_LOCATION };
};

class FOpenXREyeTrackerModule : public IOpenXREyeTrackerModule
{
	/************************************************************************/
	/* IInputDeviceModule                                                   */
	/************************************************************************/
public:
	FOpenXREyeTrackerModule();
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual TSharedPtr< class IEyeTracker, ESPMode::ThreadSafe > CreateEyeTracker() override;

	/************************************************************************/
	/* IEyeTrackerModule													*/
	/************************************************************************/

	virtual bool IsEyeTrackerConnected() const override;

private:
	TSharedPtr<FOpenXREyeTracker, ESPMode::ThreadSafe> EyeTracker;
	FDelegateHandle OnDrawDebugHandle;

	void OnDrawDebug(AHUD* HUD, UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo, float& YL, float& YPos);
};
