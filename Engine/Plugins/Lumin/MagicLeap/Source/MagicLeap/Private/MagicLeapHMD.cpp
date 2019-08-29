// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapHMD.h"
#include "MagicLeapHMDFunctionLibrary.h"
#include "RendererPrivate.h"
#include "PostProcess/PostProcessHMD.h"
#include "ScreenRendering.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameEngine.h"
#include "Widgets/SViewport.h"
#include "Slate/SceneViewport.h"
#include "AppFramework.h"
#include "PipelineStateCache.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "LuminRuntimeSettings.h"

#include "XRThreadUtils.h"

#if !PLATFORM_MAC
#include "MagicLeapHelperVulkan.h"
#endif //!PLATFORM_MAC

#include "MagicLeapUtils.h"
#include "MagicLeapPluginUtil.h"
#include "UnrealEngine.h"
#include "ClearQuad.h"
#include "MagicLeapXRCamera.h"
#include "IMagicLeapInputDevice.h"
#include "GeneralProjectSettings.h"
#include "MagicLeapSettings.h"
#include "MagicLeapSDKDetection.h"

#if !PLATFORM_MAC // @todo Lumin: I had to add this to get Mac to compile - trying to add GL to Mac build had massive compile issues
#include "OpenGLDrv.h"
#endif

#include "MagicLeapPluginUtil.h" // for ML_INCLUDES_START/END

#if WITH_MLSDK
ML_INCLUDES_START
#include <ml_perception.h>

#if !PLATFORM_LUMIN
#include <ml_virtual_device.h>
#include "Misc/MessageDialog.h"
#endif

#if PLATFORM_LUMIN
#define VK_NO_PROTOTYPES
#include <vulkan.h>
#undef VK_NO_PROTOTYPES

#include <ml_graphics.h>
#include "VulkanRHIBridge.h"
#else
#include <ml_graphics.h>
#endif // PLATFORM_LUMIN

ML_INCLUDES_END
#endif //WITH_MLSDK

#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#include "Editor/EditorPerformanceSettings.h"
#include "Misc/CoreDelegates.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#endif

#define LOCTEXT_NAMESPACE "MagicLeap"

//---------------------------------------------------
// MagicLeapHMD Plugin Implementation
//---------------------------------------------------

class FMagicLeapPlugin : public IMagicLeapPlugin
{
public:
	FMagicLeapPlugin()
		: bIsVDZIEnabled(false)
	{
	}

	/** IModuleInterface implementation */
	virtual void StartupModule() override
	{
		FMagicLeapSDKDetection::DetectSDK();

#if PLATFORM_LUMIN
		// Append the command line environment enclosed within spaces as we can't set it in the
		// environment. Note that any code accessing GCmdLine before appInit won't
		// respect the command line environment additions.
		FCommandLine::Append(TEXT(" \"-norhithread\""));
#endif //PLATFORM_LUMIN

		// Ideally, we should be able to query GetDefault<UMagicLeapSettings>()->bEnableZI directly.
		// Unfortunately, the UObject system hasn't finished initialization when this module has been loaded.
		GConfig->GetBool(TEXT("/Script/MagicLeap.MagicLeapSettings"), TEXT("bEnableZI"), bIsVDZIEnabled, GEngineIni);

		APISetup.Startup(bIsVDZIEnabled);
#if WITH_MLSDK
		APISetup.LoadDLL(TEXT("ml_perception_client"));
		APISetup.LoadDLL(TEXT("ml_graphics"));
		APISetup.LoadDLL(TEXT("ml_lifecycle"));
#endif

		if (bIsVDZIEnabled)
		{
#if PLATFORM_WINDOWS
			UE_LOG(LogMagicLeap, Log, TEXT("ML VDZI mode enabled. Using OpenGL renderer."));

			APISetup.LoadDLL(TEXT("ml_virtual_device"));

			// DirectX, which is currently not supported by MagicLeap, is default API on Windows.
			// OpenGL is forced by loading module in PostConfigInit phase and passing in command line.
			// -opengl will force editor to use OpenGL3/SM4 feature level. Fwd VR path requires SM5 feature level, thus passing -opengl here will break editor preview window with Fwd VR path
			// The cmd arg for OpenGL4/SM5 feature level is -opengl4 in Windows.
			FCommandLine::Append(TEXT(" -opengl4 "));
#endif // PLATFORM_WINDOWS
		}

#if WITH_EDITOR
		// We don't quite have control of when the "Settings" module is loaded, so we'll wait until PostEngineInit to register settings.
		FCoreDelegates::OnPostEngineInit.AddRaw(this, &FMagicLeapPlugin::AddEditorSettings);
#endif // WITH_EDITOR

		IMagicLeapPlugin::StartupModule();
	}

	virtual void ShutdownModule() override
	{
#if WITH_EDITOR
		RemoveEditorSettings();
#endif
		APISetup.Shutdown();
		IMagicLeapPlugin::ShutdownModule();
	}

	/** IHeadMountedDisplayModule implementation */
	virtual TSharedPtr< class IXRTrackingSystem, ESPMode::ThreadSafe > CreateTrackingSystem() override
	{
		auto LocalHMD = GetOrCreateHMD();
		if (LocalHMD.IsValid() && !LocalHMD->IsInitialized())
		{
			LocalHMD->Startup();
		}
		return LocalHMD;
	}

#if ENGINE_MAJOR_VERSION >= 4 && ENGINE_MINOR_VERSION <= 13
	FString GetModulePriorityKeyName() const
#else
	FString GetModuleKeyName() const
#endif // ENGINE_MAJOR_VERSION >= 4 && ENGINE_MINOR_VERSION <= 13
	{
		return FString(TEXT("MagicLeap"));
	}

	virtual bool IsMagicLeapHMDValidAndConnected() const override
	{
		if (IsValid(GEngine) && GEngine->XRSystem.IsValid() && (GEngine->XRSystem->GetSystemName().Compare(FMagicLeapHMD::SystemName) == 0))
		{
			TSharedPtr<FMagicLeapHMD, ESPMode::ThreadSafe> hmd = StaticCastSharedPtr<FMagicLeapHMD>(GEngine->XRSystem);
			if (hmd.IsValid())
			{
				return hmd->IsHMDConnected();
			}
		}
		return false;
	}

	virtual TWeakPtr<IMagicLeapHMD, ESPMode::ThreadSafe> GetHMD() override
	{
		return GetOrCreateHMD();
	}

private:

#if WITH_EDITOR
	void AddEditorSettings()
	{
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");

		// While this should usually be true, it's not guaranteed that the settings module will be loaded in the editor.
		// UBT allows setting bBuildDeveloperTools to false while bBuildEditor can be true.
		// The former option indirectly controls loading of the "Settings" module.
		if (SettingsModule)
		{
			SettingsModule->RegisterSettings("Project", "Plugins", "Magic Leap",
				LOCTEXT("MagicLeapSettingsName", "Magic Leap Plugin"),
				LOCTEXT("MagicLeapSettingsDescription", "Configure the Magic Leap plug-in."),
				GetMutableDefault<UMagicLeapSettings>()
			);
		}
	}

	void RemoveEditorSettings()
	{
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
		if (SettingsModule)
		{
			SettingsModule->UnregisterSettings("Project", "Plugins", "Magic Leap");
		}
	}
#endif

	TSharedPtr<FMagicLeapHMD, ESPMode::ThreadSafe> GetOrCreateHMD()
	{
#if !PLATFORM_MAC
		if (!HMD.IsValid())
		{
			HMD = MakeShared<FMagicLeapHMD, ESPMode::ThreadSafe>(this, bIsVDZIEnabled);
		}
#endif
#if WITH_EDITOR
		if (bIsVDZIEnabled)
		{
			return HMD;
		}
		else
		{
			return nullptr;
		}
#else
		return HMD;
#endif
	}

	bool bIsVDZIEnabled;
	FMagicLeapAPISetup APISetup;
	TSharedPtr<FMagicLeapHMD, ESPMode::ThreadSafe> HMD;
};

IMPLEMENT_MODULE(FMagicLeapPlugin, MagicLeap)

//////////////////////////////////////////////////////////////////////////

const FName FMagicLeapHMD::SystemName(TEXT("MagicLeap"));

FName FMagicLeapHMD::GetSystemName() const
{
	return SystemName;
}

bool FMagicLeapHMD::OnStartGameFrame(FWorldContext& WorldContext)
{
#if WITH_MLSDK
	check(IsInGameThread());

	if (!WorldContext.World() || !WorldContext.World()->IsGameWorld())
	{
		// ignore all non-game worlds
		return false;
	}

#if !PLATFORM_LUMIN
	// With VDZI, we need to enable on the start of game frame after stereo has been enabled.  On Lumin itself, it's enabled in EnableStereo() immediately
	if (bStereoEnabled != bStereoDesired)
	{
		bStereoEnabled = EnableStereo(bStereoDesired);
	}
#endif //!PLATFORM_LUMIN

	if (bStereoEnabled)
	{
		InitDevice();
	}

	AppFramework.BeginUpdate();

	FTrackingFrame* TrackingFrame = GetCurrentFrame();
	check(TrackingFrame);
	// init tracking frame if first frame, otherwise we keep using last frame's data until it is refreshed in BeginRendering_GameThread
	if (TrackingFrame->Snapshot == nullptr)
	{
		RefreshTrackingFrame();
	}

	// override the default value that the frame constructor initialized and make sure it is non zero
	TrackingFrame->WorldToMetersScale = WorldContext.World()->GetWorldSettings()->WorldToMeters;
	TrackingFrame->WorldToMetersScale = TrackingFrame->WorldToMetersScale == 0.0f ? 100.0f : TrackingFrame->WorldToMetersScale;
#endif //WITH_MLSDK

	return true;
}

bool FMagicLeapHMD::OnEndGameFrame(FWorldContext& WorldContext)
{
	check(IsInGameThread());

	if (!WorldContext.World() || !WorldContext.World()->IsGameWorld())
	{
		// ignore all non-game worlds
		return false;
	}

	return true;
}

bool FMagicLeapHMD::IsHMDConnected()
{
#if WITH_MLSDK
#if PLATFORM_LUMIN
	return AppFramework.IsInitialized();
#elif PLATFORM_WINDOWS
	bool bZIServerRunning = false;
	if (bIsVDZIEnabled)
	{
		ML_FUNCTION_WRAPPER(bZIServerRunning = MLVirtualDeviceIsServerRunning())
	}
	// TODO: MLVirtualDeviceIsServerRunning() crashes when called on render thread.
	return AppFramework.IsInitialized() && bIsVDZIEnabled && bZIServerRunning;
#else
	return false;
#endif

#else
	return false;
#endif //WITH_MLSDK
}

bool FMagicLeapHMD::IsHMDEnabled() const
{
#if WITH_EDITOR
	// We indicate the HMD is disable in editor so that the vr-editor mode is disabled.
	// We do this by indicating it's enabled only when playing (i.e. when OnBeginPlay
	// was called) and only when executing a console command (which is only indicative
	// when the GStatProcessingViewportClient is valid -- See
	// UUnrealEdEngine::Exec() for why this is the case).
	return bHmdEnabled && bIsPlaying && (nullptr != GStatProcessingViewportClient);
#else
	return bHmdEnabled;
#endif
}

void FMagicLeapHMD::EnableHMD(bool Enable)
{
	bHmdEnabled = Enable;
	if (!bHmdEnabled)
	{
		EnableStereo(false);
	}
}

bool FMagicLeapHMD::GetHMDMonitorInfo(MonitorInfo& MonitorDesc)
{
	// Use ML device resolution only when HMD is enabled.
	// This ensures that we don't provide an invalid resolution when the device is not connected.
	// TODO: check if we can rely on the return bool value from AppFramework.GetDeviceResolution() instead.
	if (IsInitialized() && bHmdEnabled)
	{
		FVector2D Resolution;
		AppFramework.GetDeviceResolution(Resolution);
		MonitorDesc.MonitorName = "";
		MonitorDesc.MonitorId = 0;
		MonitorDesc.DesktopX = 0;
		MonitorDesc.DesktopY = 0;
		MonitorDesc.ResolutionX = Resolution.X;
		MonitorDesc.ResolutionY = Resolution.Y;
		return true;
	}
	else
	{
		MonitorDesc.MonitorName = "";
		MonitorDesc.MonitorId = 0;
		MonitorDesc.DesktopX = 0;
		MonitorDesc.DesktopY = 0;
		MonitorDesc.ResolutionX = 0;
		MonitorDesc.ResolutionY = 0;
		return false;
	}
}

void FMagicLeapHMD::GetFieldOfView(float& OutHFOVInDegrees, float& OutVFOVInDegrees) const
{
	FTrackingFrame* frame = GetCurrentFrame();
	if (frame)
	{
		OutHFOVInDegrees = frame->HFov;
		OutVFOVInDegrees = frame->VFov;
	}
}

bool FMagicLeapHMD::DoesSupportPositionalTracking() const
{
	return bHmdPosTracking;
}

bool FMagicLeapHMD::HasValidTrackingPosition()
{
	FTrackingFrame* frame = GetCurrentFrame();
	return bHmdPosTracking && frame ? frame->HasHeadTrackingPosition : false;
}

bool FMagicLeapHMD::GetTrackingSensorProperties(int32 DeviceId, FQuat& OutOrientation, FVector& OutPosition, FXRSensorProperties& OutSensorProperties)
{
	// Assuming there is only one tracker (the device itself) on the system being optically tracked
	FTrackingFrame* frame = GetCurrentFrame();
	if (frame)
	{
		const float HalfHFOV = frame->HFov / 2.f;
		const float HalfVFOV = frame->VFov / 2.f;

		OutSensorProperties.TopFOV = HalfVFOV;
		OutSensorProperties.BottomFOV = HalfVFOV;

		OutSensorProperties.LeftFOV = HalfHFOV;
		OutSensorProperties.RightFOV = HalfHFOV;

		OutSensorProperties.CameraDistance = 0.f;
		OutSensorProperties.NearPlane = 8.f;
		OutSensorProperties.FarPlane = 400.f; // Assumption, should get real numbers on this!

		return true;
	}

	return false;
}

void FMagicLeapHMD::SetInterpupillaryDistance(float NewInterpupillaryDistance)
{
	IPD = NewInterpupillaryDistance;
}

float FMagicLeapHMD::GetInterpupillaryDistance() const
{
	return IPD;
}

bool FMagicLeapHMD::IsChromaAbCorrectionEnabled() const
{
	return true;
}

bool FMagicLeapHMD::IsHeadTrackingAllowed() const
{
	const FTrackingFrame* frame = GetCurrentFrame();
	if (!frame)
	{
		return false;
	}
#if WITH_EDITOR
	if (GIsEditor)
	{
		UEditorEngine* EdEngine = Cast<UEditorEngine>(GEngine);
		return ((!EdEngine || EdEngine->bUseVRPreviewForPlayWorld || GetDefault<ULevelEditorPlaySettings>()->ViewportGetsHMDControl) && GEngine->IsStereoscopic3D());
	}
#endif // WITH_EDITOR
	return (GEngine->IsStereoscopic3D());
}

void FMagicLeapHMD::ResetOrientationAndPosition(float yaw)
{
	ResetOrientation(yaw);
	ResetPosition();
}

void FMagicLeapHMD::ResetOrientation(float Yaw)
{
	const FTrackingFrame* frame = GetCurrentFrame();

	FTransform RawTransform;
	if (frame)
	{
		RawTransform = frame->RawPose;
	}

	FRotator ViewRotation = RawTransform.Rotator();
	ViewRotation.Pitch = 0;
	ViewRotation.Roll = 0;

	if (Yaw != 0.f)
	{
		// apply optional yaw offset
		ViewRotation.Yaw -= Yaw;
		ViewRotation.Normalize();
	}

	AppFramework.SetBaseOrientation(ViewRotation.Quaternion());
}

void FMagicLeapHMD::ResetPosition()
{
	const FTrackingFrame* frame = GetCurrentFrame();

	FTransform RawTransform;
	if (frame)
	{
		RawTransform = frame->RawPose;
	}

	FVector ViewPositon = RawTransform.GetTranslation();
	AppFramework.SetBasePosition(ViewPositon);
}

void FMagicLeapHMD::SetBasePosition(const FVector& InBasePosition)
{
	AppFramework.SetBasePosition(InBasePosition);
}

FVector FMagicLeapHMD::GetBasePosition() const
{
	return AppFramework.GetBasePosition();
}

void FMagicLeapHMD::SetBaseRotation(const FRotator& BaseRot)
{
	AppFramework.SetBaseRotation(BaseRot);
}

FRotator FMagicLeapHMD::GetBaseRotation() const
{
	return AppFramework.GetBaseRotation();
}

void FMagicLeapHMD::SetBaseOrientation(const FQuat& BaseOrient)
{
	AppFramework.SetBaseOrientation(BaseOrient);
}

FQuat FMagicLeapHMD::GetBaseOrientation() const
{
	return AppFramework.GetBaseOrientation();
}

bool FMagicLeapHMD::EnumerateTrackedDevices(TArray<int32>& OutDevices, EXRTrackedDeviceType Type /*= EXRTrackedDeviceType::Any*/)
{
	//@todo:  Add controller tracking here
	if (Type == EXRTrackedDeviceType::Any || Type == EXRTrackedDeviceType::HeadMountedDisplay)
	{
		static const int32 DeviceId = IXRTrackingSystem::HMDDeviceId;
		OutDevices.Add(DeviceId);
		return true;
	}

	return false;
}

void FMagicLeapHMD::RefreshTrackingFrame()
{
#if WITH_MLSDK
	check(IsInGameThread());

	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.ScreenPercentage"));
	GameTrackingFrame.ScreenPercentage = CVar->GetValueOnGameThread() / 100.0f;

	// get the frame id for the frame
	GameTrackingFrame.FrameId = HeadTrackerData.coord_frame_head;
	GameTrackingFrame.FrameNumber = GFrameCounter;

	// set the horizontal and vertical fov for this frame
	GameTrackingFrame.HFov = AppFramework.GetFieldOfView().X;
	GameTrackingFrame.VFov = AppFramework.GetFieldOfView().Y;

	// Release the snapshot of the previous frame.
	// This is done here instead of on end frame because modules implemented as input devices (Gestures, controller)
	// are ticked and fire their events before the OnStartGameFrame().
	MLPerceptionReleaseSnapshot(GameTrackingFrame.Snapshot);
	// get the snapshot for the frame
	GameTrackingFrame.Snapshot = MLPerceptionGetSnapshot();

	MLHeadTrackingState state;
	bHeadTrackingStateAvailable = MLHeadTrackingGetState(HeadTracker, &state);
	if (bHeadTrackingStateAvailable)
	{
		HeadTrackingState.Mode = MLToUnrealHeadTrackingMode(state.mode);
		HeadTrackingState.Error = MLToUnrealHeadTrackingError(state.error);
		HeadTrackingState.Confidence = state.confidence;
	}

	EFailReason FailReason = EFailReason::None;
	// get the raw pose and tracking status for the frame
	FTransform head_transform;
	GameTrackingFrame.HasHeadTrackingPosition = AppFramework.GetTransform(GameTrackingFrame.FrameId, head_transform, FailReason);
	if (GameTrackingFrame.HasHeadTrackingPosition)
	{
		GameTrackingFrame.RawPose = head_transform;
	}
	else if (FailReason == EFailReason::NaNsInTransform)
	{
		UE_LOG(LogMagicLeap, Error, TEXT("NaNs in head transform."));
		GameTrackingFrame.RawPose = OldTrackingFrame.RawPose;
	}
	else
	{
		UE_CLOG(bIsPerceptionEnabled, LogMagicLeap, Warning, TEXT("Failed to get head tracking position: Reason = %i."), static_cast<int>(FailReason));
		GameTrackingFrame.RawPose = OldTrackingFrame.RawPose;
	}

	FVector CurrentPosition;
	FQuat CurrentOrientation;
	if (!GetCurrentPose(IXRTrackingSystem::HMDDeviceId, CurrentOrientation, CurrentPosition))
	{
		UE_LOG(LogMagicLeap, Warning, TEXT("Failed to get current pose."));
	}

	if (!FocusActor.IsValid())
	{
		UE_LOG(LogMagicLeap, Verbose, TEXT("Focus actor not set. Defaulting focus distance to 500.0 cm. Call the SetFocusActor() function to set a valid focus actor."));
	}

	// If GNearClipPlane is changed by the app at runtime,
	// ensure we clamp the near clip to the value provided by ml_graphics.
	UpdateNearClippingPlane();

	FVector focusPoint = (FocusActor.IsValid()) ? FocusActor->GetActorLocation() : (CurrentOrientation.GetForwardVector() * 500.0f + CurrentPosition);
	float focusDistance = FVector::DotProduct(focusPoint - CurrentPosition, CurrentOrientation.GetForwardVector());
	GameTrackingFrame.FocusDistance = (focusDistance > GNearClippingPlane) ? focusDistance : GNearClippingPlane;
#endif //WITH_MLSDK
}

#if WITH_MLSDK

EHeadTrackingError FMagicLeapHMD::MLToUnrealHeadTrackingError(MLHeadTrackingError error) const
{
	#define ERRCASE(x) case MLHeadTrackingError_##x: { return EHeadTrackingError::x; }
	switch(error)
	{
		ERRCASE(None)
		ERRCASE(NotEnoughFeatures)
		ERRCASE(LowLight)
		ERRCASE(Unknown)
	}
	return EHeadTrackingError::Unknown;
}

EHeadTrackingMode FMagicLeapHMD::MLToUnrealHeadTrackingMode(MLHeadTrackingMode mode) const
{
	switch(mode)
	{
		case MLHeadTrackingMode_6DOF: { return EHeadTrackingMode::PositionAndOrientation; }
		case MLHeadTrackingMode_3DOF: { return EHeadTrackingMode::OrientationOnly; }
	}
	return EHeadTrackingMode::Unknown;
}
#endif //WITH_MLSDK

#if !PLATFORM_LUMIN
void FMagicLeapHMD::DisplayWarningIfVDZINotEnabled()
{
	// If VDZI is disabled, IsHMDConnected() will false, and the editor won't attempt to run in VR mode.
	// However, the editor still stores LastExecutedPlayModeType as PlayMode_InVR, which gives us a hint that
	// the user was attempting to run with VR mode, but neglected to enabled VDZI.
	// For game mode on the host platform, we can just check command-line and .ini settings to see if VR is enabled.

	bool VREnabled = false;
#if WITH_EDITOR
	if (GIsEditor)
	{
		VREnabled = (GetDefault<ULevelEditorPlaySettings>()->LastExecutedPlayModeType == PlayMode_InVR);
	}
	else
#endif
	{
		VREnabled = FParse::Param(FCommandLine::Get(), TEXT("vr")) || GetDefault<UGeneralProjectSettings>()->bStartInVR;
	}

#if WITH_MLSDK
	if (!bIsVDZIEnabled && !bVDZIWarningDisplayed && VREnabled)
	{
		FString message =
			"Zero Iteration must be enabled to work with VR mode, which can be done as follows:\n"
			"1) Load the editor.\n"
			"2) Go to 'Edit -> Project Settings...'\n"
			"3) Toggle the 'Enable Zero Iteration' option under the 'Magic Leap Plugin' category.\n"
			"4) Restart the editor or game.";
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(message));
		bVDZIWarningDisplayed = true;
	}
#endif //WITH_MLSDK
}
#endif

#if PLATFORM_LUMIN
void FMagicLeapHMD::SetFrameTimingHint(ELuminFrameTimingHint InFrameTimingHint)
{
	const static UEnum* FrameTimingEnum = FindObject<UEnum>(ANY_PACKAGE, TEXT("ELuminFrameTimingHint"));
	check(FrameTimingEnum != nullptr);

	if (InFrameTimingHint != CurrentFrameTimingHint)
	{
		if (GraphicsClient != ML_INVALID_HANDLE)
		{
			MLGraphicsFrameTimingHint FTH = MLGraphicsFrameTimingHint_Unspecified;

			switch (InFrameTimingHint)
			{
			case ELuminFrameTimingHint::Unspecified:
				FTH = MLGraphicsFrameTimingHint_Unspecified;
				break;
			case ELuminFrameTimingHint::Maximum:
				FTH = MLGraphicsFrameTimingHint_Maximum;
				break;
			case ELuminFrameTimingHint::FPS_60:
				FTH = MLGraphicsFrameTimingHint_60Hz;
				break;
			case ELuminFrameTimingHint::FPS_120:
				FTH = MLGraphicsFrameTimingHint_120Hz;
				break;
			default:
				FTH = MLGraphicsFrameTimingHint_Unspecified;
				UE_LOG(LogMagicLeap, Warning, TEXT("Tried to set invalid Frame Timing Hint!  Defaulting to unspecified."));
			}

			MLStatus RetStatus;
			MLGraphicsSetFrameTimingHint(GraphicsClient, FTH, &RetStatus);

			if (RetStatus == MLStatus_OK)
			{
				UE_LOG(LogMagicLeap, Log, TEXT("Set Lumin frame timing hint to %s."), *FrameTimingEnum->GetNameStringByValue((int64)InFrameTimingHint));
				CurrentFrameTimingHint = InFrameTimingHint;
			}
			else
			{
				UE_LOG(LogMagicLeap, Log, TEXT("Failed to set Lumin frame timing hint to %s...invalid parameter!"), *FrameTimingEnum->GetNameStringByValue((int64)InFrameTimingHint));
			}
		}
		else
		{
			UE_LOG(LogMagicLeap, Warning, TEXT("Failed to set Lumin frame timing hint.  Invalid context."));
		}
	}
}
#endif //PLATFORM_LUMIN

float FMagicLeapHMD::GetWorldToMetersScale() const
{
	FTrackingFrame* TrackingFrame = GetCurrentFrame();
	check(TrackingFrame);

	return TrackingFrame->WorldToMetersScale;
}

bool FMagicLeapHMD::EnableStereo(bool bStereo)
{
	const bool bShouldStereo = IsHMDEnabled() ? bStereo : false;

#if !PLATFORM_LUMIN
	bStereoDesired = bShouldStereo;
#endif //!PLATFORM_LUMIN

#if WITH_EDITOR
	// We disable input globally for editor play as all input must come from the
	// Virtual Device / Zero Iteration system.
	//
	// NOTE: We do this here in addition to OnBeginPlay because the game viewport client
	// is not defined yet when the HMD begin play is invoked while doing PIE.
	//
	SetIgnoreInput(true);
#endif
	bStereoEnabled = bShouldStereo;

	// Uncap fps to enable FPS higher than 62
	GEngine->bForceDisableFrameRateSmoothing = bStereoEnabled;

	return bStereoEnabled;
}

bool FMagicLeapHMD::SetIgnoreInput(bool Ignore)
{
#if WITH_EDITOR
	UGameViewportClient* ViewportClient = GetGameViewportClient();
	// Change input settings only if running in the editor.
	// Without the GIsEditor check input doesnt work in "Play in Standalone Mode" since that uses the editor dlls itself.
	if (ViewportClient && GIsEditor)
	{
		bool Result = ViewportClient->IgnoreInput();
		ViewportClient->SetIgnoreInput(Ignore);
		if (DisableInputForBeginPlay && !Ignore)
		{
			// First time around we call this to disable the input globally. Hence we
			// also set mouse options. On subsequent calls we only set the input ignore flags.
			DisableInputForBeginPlay = false;
			ViewportClient->SetCaptureMouseOnClick(EMouseCaptureMode::NoCapture);
			ViewportClient->SetMouseLockMode(EMouseLockMode::DoNotLock);
			ViewportClient->SetHideCursorDuringCapture(false);
		}
		return Result;
	}
#endif
	return false;
}

void FMagicLeapHMD::AdjustViewRect(EStereoscopicPass StereoPass, int32& X, int32& Y, uint32& SizeX, uint32& SizeY) const
{
	if (DebugViewportWidth > 0)
	{
		SizeX = DebugViewportWidth;
		SizeY = DebugViewportHeight;
	}

	X = 0;
	Y = 0;

	SizeX = SizeX / 2;
	if (StereoPass == eSSP_RIGHT_EYE)
	{
		X += SizeX;
	}
}

FMatrix FMagicLeapHMD::GetStereoProjectionMatrix(const enum EStereoscopicPass StereoPassType) const
{
#if WITH_MLSDK
	// This function should only be called in game thread
	check(IsInGameThread());
	check(IsStereoEnabled());
	const int viewport = (StereoPassType == eSSP_LEFT_EYE) ? 0 : 1;
	FTrackingFrame* frame = GetCurrentFrame();
	// TODO: Remove this for vulkan when we can get a better result from the frame
	return (frame && bDeviceInitialized && !IsVulkanPlatform(GMaxRHIShaderPlatform)) ? MagicLeap::ToFMatrix(frame->UpdateInfoArray.virtual_camera_extents[viewport].projection) : FMatrix::Identity;
#else
	return FMatrix();
#endif //WITH_MLSDK
}

void FMagicLeapHMD::InitCanvasFromView(FSceneView* InView, UCanvas* Canvas)
{
}

void FMagicLeapHMD::UpdateViewportRHIBridge(bool /* bUseSeparateRenderTarget */, const class FViewport& InViewport, FRHIViewport* const ViewportRHI)
{
	// Since device initialization finishes on the render thread, we must assume here that the device will be initialized by the time the frame is presented.
	const bool bRequireDeviceIsInitialized = false;
	FMagicLeapCustomPresent* const CP = GetActiveCustomPresent(bRequireDeviceIsInitialized);
	if (CP)
	{
		CP->UpdateViewport(InViewport, ViewportRHI);
	}
}

bool FMagicLeapHMD::GetHeadTrackingState(FHeadTrackingState& State) const
{
	if (bHeadTrackingStateAvailable)
	{
		State = HeadTrackingState;
	}
	return bHeadTrackingStateAvailable;
}

void FMagicLeapHMD::UpdateNearClippingPlane()
{
	GNearClippingPlane = (GameTrackingFrame.NearClippingPlane > GNearClippingPlane) ? GameTrackingFrame.NearClippingPlane : GNearClippingPlane;
}

FMagicLeapCustomPresent* FMagicLeapHMD::GetActiveCustomPresent(const bool bRequireDeviceIsInitialized) const
{
	if (bRequireDeviceIsInitialized && !bDeviceInitialized)
	{
		return nullptr;
	}

#if PLATFORM_WINDOWS
	if (CustomPresentD3D11)
	{
		return CustomPresentD3D11;
	}
#endif // PLATFORM_WINDOWS

#if PLATFORM_MAC
	if (CustomPresentMetal)
	{
		return CustomPresentMetal;
}
#endif // PLATFORM_MAC

#if PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_LUMIN
	if (CustomPresentOpenGL)
	{
		return CustomPresentOpenGL;
	}
#endif // PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_LUMIN

#if PLATFORM_LUMIN
	if (CustomPresentVulkan)
	{
		return CustomPresentVulkan;
	}
#endif // PLATFORM_LUMIN

	return nullptr;
}

void FMagicLeapHMD::CalculateRenderTargetSize(const class FViewport& Viewport, uint32& InOutSizeX, uint32& InOutSizeY)
{
	check(IsInGameThread());

	// set to the resolution of both eyes. unreal will pass in default 1280x720 which needs to be overridden here.
	FVector2D Resolution;
	// Device resolution takes into account the r.ScreenPercentage property that is set as the surface_scale for the camera params on BeginFrame().
	bool bValidResolution = AppFramework.GetDeviceResolution(Resolution);

	if (bValidResolution)
	{
		InOutSizeX = Resolution.X;
		InOutSizeY = Resolution.Y;
	}
}

bool FMagicLeapHMD::NeedReAllocateViewportRenderTarget(const FViewport& Viewport)
{
	check(IsInGameThread());

	if (IsStereoEnabled())
	{
		const uint32 InSizeX = Viewport.GetSizeXY().X;
		const uint32 InSizeY = Viewport.GetSizeXY().Y;
		FIntPoint RenderTargetSize;
		RenderTargetSize.X = Viewport.GetRenderTargetTexture()->GetSizeX();
		RenderTargetSize.Y = Viewport.GetRenderTargetTexture()->GetSizeY();

		uint32 NewSizeX = InSizeX, NewSizeY = InSizeY;
		CalculateRenderTargetSize(Viewport, NewSizeX, NewSizeY);
		if (NewSizeX != RenderTargetSize.X || NewSizeY != RenderTargetSize.Y)
		{
			return true;
		}
	}
	return false;
}

bool FMagicLeapHMD::AllocateRenderTargetTexture(uint32 Index, uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 Flags, uint32 TargetableTextureFlags, FTexture2DRHIRef& OutTargetableTexture, FTexture2DRHIRef& OutShaderResourceTexture, uint32 NumSamples)
{
	if (!IsStereoEnabled())
	{
		return false;
	}

#if PLATFORM_MAC
	// TODO: fix for Mac when VDZI is supported on Metal.
	return false;
#else
	FRHIResourceCreateInfo CreateInfo;
	RHICreateTargetableShaderResource2D(SizeX, SizeY, PF_R8G8B8A8, 1, TexCreate_None, TexCreate_RenderTargetable, false, CreateInfo, OutTargetableTexture, OutShaderResourceTexture);

	return true;
#endif
}

//FRHICustomPresent* FMagicLeapHMD::GetCustomPresent()
//{
//	return GetActiveCustomPresent();
//}

FMagicLeapHMD::FMagicLeapHMD(IMagicLeapPlugin* InMagicLeapPlugin, bool bEnableVDZI) :
	// We don't do any mirroring on Lumin as we render direct to the device only.
#if PLATFORM_LUMIN
	WindowMirrorMode(0),
#else
	WindowMirrorMode(1),
#endif
	DebugViewportWidth(0),
	DebugViewportHeight(0),
#if WITH_MLSDK
	GraphicsClient(ML_INVALID_HANDLE),
#endif //WITH_MLSDK
	bDeviceInitialized(0),
	bDeviceWasJustInitialized(0),
	bHmdEnabled(true),
#if PLATFORM_LUMIN
	bStereoEnabled(true),
#else
	bStereoEnabled(false),
	bStereoDesired(false),
#endif
	bHmdPosTracking(true),
	bHaveVisionTracking(false),
	IPD(0.064f),
	DeltaControlRotation(FRotator::ZeroRotator),
#if WITH_MLSDK
	HeadTracker(ML_INVALID_HANDLE),
	HeadTrackerData{},
#endif //WITH_MLSDK
	RendererModule(nullptr),
	MagicLeapPlugin(InMagicLeapPlugin),
	IdealScreenPercentage(100.0f),
	bIsPlaying(false),
	bIsPerceptionEnabled(false),
	bIsVDZIEnabled(bEnableVDZI),
	bVDZIWarningDisplayed(false),
	CurrentFrameTimingHint(ELuminFrameTimingHint::Unspecified),
	bHeadTrackingStateAvailable(false)
{
#if WITH_EDITOR
	World = nullptr;
	DisableInputForBeginPlay = false;
#endif
}

FMagicLeapHMD::~FMagicLeapHMD()
{
	Shutdown();
}

void FMagicLeapHMD::Startup()
{
	LoadFromIni();

	// grab a pointer to the renderer module for displaying our mirror window
	const FName RendererModuleName("Renderer");
	RendererModule = FModuleManager::GetModulePtr<IRendererModule>(RendererModuleName);

	// ALWAYS SET r.FinishCurrentFrame to false! Otherwise the perf might be poor.
	static const auto CFinishFrameVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.FinishCurrentFrame"));
	CFinishFrameVar->Set(false);

	// Uncap fps to enable FPS higher than 62
	GEngine->bForceDisableFrameRateSmoothing = bStereoEnabled;

	// Context must be created before the bridge so that the bridge can set the render api.
	AppFramework.Startup();

#if PLATFORM_WINDOWS
	if (IsPCPlatform(GMaxRHIShaderPlatform) && !IsOpenGLPlatform(GMaxRHIShaderPlatform))
	{
		UE_LOG(LogMagicLeap, Display, TEXT("Creating FMagicLeapCustomPresentD3D11"));
		CustomPresentD3D11 = new FMagicLeapCustomPresentD3D11(this);
	}
#endif // PLATFORM_WINDOWS

#if PLATFORM_MAC
	if (IsMetalPlatform(GMaxRHIShaderPlatform) && !IsOpenGLPlatform(GMaxRHIShaderPlatform))
	{
		UE_LOG(LogMagicLeap, Display, TEXT("Creating FMagicLeapCustomPresentMetal"));
		//DISABLED until complete
		//CustomPresentMetal = new FMagicLeapCustomPresentMetal(this);
	}
#endif // PLATFORM_MAC

#if PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_LUMIN
	if (IsOpenGLPlatform(GMaxRHIShaderPlatform))
	{
		UE_LOG(LogMagicLeap, Display, TEXT("Creating FMagicLeapCustomPresentOpenGL"));
		CustomPresentOpenGL = new FMagicLeapCustomPresentOpenGL(this);
	}
#endif // PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_LUMIN

#if PLATFORM_LUMIN
	if (IsVulkanPlatform(GMaxRHIShaderPlatform))
	{
		UE_LOG(LogMagicLeap, Display, TEXT("Creating FMagicLeapCustomPresentVulkan"));
		CustomPresentVulkan = new FMagicLeapCustomPresentVulkan(this);
	}
#endif // PLATFORM_LUMIN

	UE_LOG(LogMagicLeap, Log, TEXT("MagicLeap initialized."));
}

void FMagicLeapHMD::Shutdown()
{
	ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER(ShutdownRen,
		FMagicLeapHMD*, Plugin, this,
		{
			Plugin->ShutdownRendering();
		});
	FlushRenderingCommands();

	ReleaseDevice();

	// IXRTrackingSystem::OnEndPlay() gets called only in the Editor.
	// This was causing the input trackers, header tracker and perception client to not be shutdown on the device resulting in the app not exiting cleanly.
	// Thus, we make an explicit call to function here.
	DisableDeviceFeatures();

	AppFramework.Shutdown();
}

void FMagicLeapHMD::LoadFromIni()
{
	const TCHAR* MagicLeapSettings = TEXT("MagicLeapSettings");
	// We don't do any mirroring on Lumin as we render direct to the device only.
#if !PLATFORM_LUMIN
	int32 WindowMirrorModeValue;

	if (GConfig->GetInt(MagicLeapSettings, TEXT("WindowMirrorMode"), WindowMirrorModeValue, GEngineIni))
	{
		WindowMirrorMode = WindowMirrorModeValue;
	}
#endif

#if PLATFORM_LUMIN
	ELuminFrameTimingHint ConfigFrameTimingHint = ELuminFrameTimingHint::Unspecified;
	const static UEnum* FrameTimingEnum = FindObject<UEnum>(ANY_PACKAGE, TEXT("ELuminFrameTimingHint"));

	FString EnumVal;
	GConfig->GetString(TEXT("/Script/LuminRuntimeSettings.LuminRuntimeSettings"), TEXT("FrameTimingHint"), EnumVal, GEngineIni);

	if (EnumVal.Len() > 0)
	{
		// This will be set later during Graphics Client initialization based on thje value in FrameTimingHint read from the config here.
		ConfigFrameTimingHint = static_cast<ELuminFrameTimingHint>(FrameTimingEnum->GetValueByNameString(EnumVal));
		SetFrameTimingHint(ConfigFrameTimingHint);
	}
#endif //PLATFORM_LUMIN
}

void FMagicLeapHMD::SaveToIni()
{
	const TCHAR* MagicLeapSettings = TEXT("MagicLeapSettings");
	// We don't do any mirroring on Lumin as we render direct to the device only.
#if !PLATFORM_LUMIN
	GConfig->SetInt(MagicLeapSettings, TEXT("WindowMirrorMode"), WindowMirrorMode, GEngineIni);
#endif
}

class FSceneViewport* FMagicLeapHMD::FindSceneViewport()
{
	if (!GIsEditor)
	{
		UGameEngine* GameEngine = Cast<UGameEngine>(GEngine);
		return GameEngine->SceneViewport.Get();
	}
#if WITH_EDITOR
	else
	{
		UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
		return (FSceneViewport*)(EditorEngine->GetPIEViewport());
	}
#endif
	return nullptr;
}

void FMagicLeapHMD::OnBeginPlay(FWorldContext& InWorldContext)
{
#if WITH_EDITOR
	InWorldContext.AddRef(World);
	DisableInputForBeginPlay = true;
	// We disable input globally for editor play as all input must come from the
	// Virtual Device / Zero Iteration system.
	SetIgnoreInput(true);
#endif

	EnableDeviceFeatures();
}

void FMagicLeapHMD::OnEndPlay(FWorldContext& InWorldContext)
{
#if WITH_EDITOR
	InWorldContext.RemoveRef(World);
#endif
	DisableDeviceFeatures();
}

void FMagicLeapHMD::EnableDeviceFeatures()
{
	bIsPlaying = true;
	if (GIsEditor)
	{
		InitDevice();
	}

#if !PLATFORM_LUMIN
	DisplayWarningIfVDZINotEnabled();
#endif

	// When run on a non-target platform, the VDZI may not necessarily be initialized.
	// In this case, just skip these steps since their timeouts may cause the game to appear to hang.
	if (IsHMDConnected())
	{
		EnablePerception();
		EnableHeadTracking();
		EnableInputDevices();
	}
	EnableLuminProfile();
}

void FMagicLeapHMD::DisableDeviceFeatures()
{
	AppFramework.OnApplicationShutdown();
	RestoreBaseProfile();
	DisableInputDevices();
	DisableHeadTracking();
	DisablePerception();
	if (GIsEditor)
	{
		ReleaseDevice();
	}
	bIsPlaying = false;
	bVDZIWarningDisplayed = false;
}

void FMagicLeapHMD::InitDevice_RenderThread()
{
#if WITH_MLSDK
	if (!bDeviceInitialized)
	{
		int32 bDeviceSuccessfullyInitialized = 0;
		// Unreal supports sRGB which is the default we are requesting from graphics as well now.
		MLGraphicsOptions gfx_opts;
		gfx_opts.graphics_flags = 0; //MLGraphicsFlags_DebugMode;
		gfx_opts.color_format = MLSurfaceFormat_RGBA8UNormSRGB;
		gfx_opts.depth_format = MLSurfaceFormat_D32Float;

		// HACK - Forward rendering on Lumin assumes a bottom-left origin.
		// There might be other platforms that need this that we haven't accounted for.
#if !PLATFORM_LUMIN || PLATFORM_LUMINGL4
		gfx_opts.graphics_flags = MLGraphicsFlags_OriginUpperLeft;
#else
		gfx_opts.graphics_flags = MLGraphicsFlags_Default;
#endif

#if PLATFORM_WINDOWS
		if (IsPCPlatform(GMaxRHIShaderPlatform) && !IsOpenGLPlatform(GMaxRHIShaderPlatform))
		{
			bDeviceSuccessfullyInitialized = 1;
			//UE_LOG(LogMagicLeap, Error, TEXT("FMagicLeapCustomPresentD3D11 is not supported."));
		}
#endif // PLATFORM_WINDOWS

#if PLATFORM_MAC
		if (IsMetalPlatform(GMaxRHIShaderPlatform) && !IsOpenGLPlatform(GMaxRHIShaderPlatform))
		{
			bDeviceSuccessfullyInitialized = 1;
			//UE_LOG(LogMagicLeap, Error, TEXT("FMagicLeapCustomPresentMetal is not supported."));
		}
#endif // PLATFORM_MAC

#if PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_LUMIN
		if (IsOpenGLPlatform(GMaxRHIShaderPlatform))
		{
			UE_LOG(LogMagicLeap, Display, TEXT("FMagicLeapCustomPresentOpenGL is supported."));
			MLHandle ContextHandle;
			auto OpenGLRHI = static_cast<FOpenGLDynamicRHI*>(GDynamicRHI);
			ContextHandle = reinterpret_cast<MLHandle>(OpenGLRHI->GetOpenGLCurrentContextHandle());
			MLStatus OutStatus;
			bDeviceSuccessfullyInitialized = MLGraphicsCreateClientGL(&gfx_opts, ContextHandle, &GraphicsClient, &OutStatus);
			if (bDeviceSuccessfullyInitialized)
			{
				InitializeClipExtents_RenderThread();
			}
			else
			{
				GraphicsClient = ML_INVALID_HANDLE;
				UE_LOG(LogMagicLeap, Error, TEXT("MLGraphicsCreateClientGL failed with status %d"), static_cast<int32>(OutStatus));
			}
		}
		// @todo Lumin vulkan: THis doesn't compile, even with some #includes - maybe need more, I dunno
		if (IsVulkanPlatform(GMaxRHIShaderPlatform))
		{
#if PLATFORM_LUMIN
			// TODO: Revise the condition after the fix is merged in the platform.
#if (MLSDK_VERSION_MINOR <= 11)
			static const auto* VulkanRHIThread = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Vulkan.RHIThread"));
			check(VulkanRHIThread);
			checkf(VulkanRHIThread->GetValueOnAnyThread() == 0, TEXT("Vulkan RHI Thread must be off! Current : r.Vulkan.RHIThread=%d"), VulkanRHIThread->GetValueOnAnyThread());

			static const auto* VulkanDelayAcquireBackBuffer = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Vulkan.DelayAcquireBackBuffer"));
			check(VulkanDelayAcquireBackBuffer);
			checkf(VulkanDelayAcquireBackBuffer->GetValueOnAnyThread() == 1, TEXT("Vulkan Delay Acquire Back Buffer must be on! Current : r.Vulkan.DelayAcquireBackBuffer=%d"), VulkanDelayAcquireBackBuffer->GetValueOnAnyThread());
#endif

#if (MLSDK_VERSION_MINOR == 8)
			gfx_opts.graphics_flags = MLGraphicsFlags_Default;
#else
			gfx_opts.graphics_flags = MLGraphicsFlags_OriginUpperLeft;
#endif
			ExecuteOnRHIThread_DoNotWait([this, gfx_opts]()
			{
				UE_LOG(LogMagicLeap, Display, TEXT("FMagicLeapCustomPresentVulkan is supported."));
				FVulkanDynamicRHI* VulkanDynamicRHI = (FVulkanDynamicRHI*)GDynamicRHI;
				FVulkanDevice* VulkanDevice = VulkanRHIBridge::GetDevice(VulkanDynamicRHI);
				uint64 PhysicalDevice = VulkanRHIBridge::GetPhysicalDevice(VulkanDevice);
				uint64 LogicalDevice = VulkanRHIBridge::GetLogicalDevice(VulkanDevice);
				GraphicsClient = ML_INVALID_HANDLE;
				MLStatus OutStatus;
				bool bDeviceSuccessfullyInitialized2 = MLGraphicsCreateClientVk(&gfx_opts, (VkPhysicalDevice)PhysicalDevice, (VkDevice)LogicalDevice, &GraphicsClient, &OutStatus);
				if (bDeviceSuccessfullyInitialized2)
				{
					InitializeClipExtents_RenderThread();
				}
				else
				{
					GraphicsClient = ML_INVALID_HANDLE;
					UE_LOG(LogMagicLeap, Error, TEXT("MLGraphicsCreateClientVk failed with status %d"), static_cast<int32>(OutStatus));
				}

				FPlatformAtomics::InterlockedExchange(&bDeviceInitialized, bDeviceSuccessfullyInitialized2);
				FPlatformAtomics::InterlockedExchange(&bDeviceWasJustInitialized, bDeviceSuccessfullyInitialized2);
			});
#endif // PLATFORM_LUMIN
		}
		else
		{
			FPlatformAtomics::InterlockedExchange(&bDeviceInitialized, bDeviceSuccessfullyInitialized);
			FPlatformAtomics::InterlockedExchange(&bDeviceWasJustInitialized, bDeviceSuccessfullyInitialized);
		}

#if PLATFORM_LUMIN
		// Initialize the frame timing hint, if we got a successful graphics client initialization
		if (GraphicsClient != ML_INVALID_HANDLE)
		{
			SetFrameTimingHint(CurrentFrameTimingHint);
		}
#endif //PLATFORM_LUMIN
#endif // PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_LUMIN
	}
#endif //WITH_MLSDK
}

void FMagicLeapHMD::InitDevice()
{
	if (!bDeviceInitialized)
	{
		// If the HMD is not connected don't bother initializing the render device since the VDZI graphics calls freeze the editor if the VDZI server is not running.
		if (IsHMDConnected())
		{
			FMagicLeapHMD* This = this;
			ENQUEUE_RENDER_COMMAND(InitDevice)(
				[This](FRHICommandList& RHICmdList)
			{
				This->InitDevice_RenderThread();
			}
			);
		}
		else
		{
			bDeviceInitialized = 1;
			bDeviceWasJustInitialized = 1;
			// Disable HMD and Stereo rendering if the device is not connected.
			// This fixes the render target size and view rect for standalone desktop builds.
			EnableHMD(false);
		}
	}

	if (bDeviceWasJustInitialized)
	{
		FSceneViewport* SceneVP = FindSceneViewport();
		if (SceneVP && SceneVP->IsStereoRenderingAllowed())
		{
			FVector2D HmdSize;
			bool IsValidResolution = AppFramework.GetDeviceResolution(HmdSize);

			// An invalid resolution implies that the render thread hasn't received valid render info yet.
			// If this is the case, we need to defer initialization until that happens.
			//if (!IsValidResolution)
			//{
			//	FPlatformMisc::LowLevelOutputDebugStringf(TEXT("     !IsValidResolution!"));
			//	return;
			//}

			// This init must happen on the main thread for VR preview, otherwise it crashes on a non-Lumin RHI.

				// Save any runtime configuration changes from the .ini.
			LoadFromIni();

			// VD/ZI works best in windowed mode since it can sometimes be used in conjunction with the mock ml1 device's window.
#if PLATFORM_LUMIN
			EWindowMode::Type WindowMode = EWindowMode::Fullscreen;
#else
			EWindowMode::Type WindowMode = EWindowMode::Windowed;
#endif

			if (IsValidResolution)
			{
				// Set the viewport size only if the resolution is valid to have the correct size for standalone desktop builds.
				DebugViewportWidth = HmdSize.X;
				DebugViewportHeight = HmdSize.Y;
				FSystemResolution::RequestResolutionChange(HmdSize.X, HmdSize.Y, WindowMode);
				FPlatformAtomics::InterlockedExchange(&bDeviceWasJustInitialized, 0);
			}
			else if (!bHmdEnabled)
			{
				// If HMD is not enabled, set bDeviceWasJustInitialized to false so the device resolution code is not run every frame.
				FPlatformAtomics::InterlockedExchange(&bDeviceWasJustInitialized, 0);				
			}
		}
	}
}

void FMagicLeapHMD::ReleaseDevice()
{
	check(IsInGameThread());

	// save any runtime configuration changes to the .ini
	SaveToIni();

	ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER(ReleaseDevice_RT,
		FMagicLeapHMD*, Plugin, this,
		{
			Plugin->ReleaseDevice_RenderThread();
		}
	);

	// Wait for all resources to be released
	FlushRenderingCommands();
}

void FMagicLeapHMD::ReleaseDevice_RenderThread()
{
	check(IsInRenderingThread());

	// Do not check for SceneViewport here because it does not work for all platforms.
	// This is because of slightly different order of operations. Just check the flag.
	if (bDeviceInitialized)
	{
		FPlatformAtomics::InterlockedExchange(&bDeviceInitialized, 0);

#if PLATFORM_WINDOWS
		if (CustomPresentD3D11)
		{
			CustomPresentD3D11->Reset();
		}
		if (CustomPresentOpenGL)
		{
			CustomPresentOpenGL->Reset();
		}
#elif PLATFORM_MAC
		if (CustomPresentMetal)
		{
			CustomPresentMetal->Reset();
		}
#elif PLATFORM_LINUX
		if (CustomPresentOpenGL)
		{
			CustomPresentOpenGL->Reset();
		}
#else
		if (CustomPresentOpenGL)
		{
			CustomPresentOpenGL->Reset();
		}
		if (CustomPresentVulkan)
		{
			CustomPresentVulkan->Reset();
		}
#endif

#if WITH_MLSDK
		MLStatus OutStatus;
		bool bResult = MLGraphicsDestroyClient(&GraphicsClient, &OutStatus);
		if (!bResult)
		{
			UE_LOG(LogMagicLeap, Error, TEXT("MLGraphicsDestroyClient failed with status %d"), static_cast<int32>(OutStatus));
		}
		else
		{
			UE_LOG(LogMagicLeap, Display, TEXT("Graphics client destroyed successfully."));
		}
#endif //WITH_MLSDK
	}
}

bool FMagicLeapHMD::GetCurrentPose(int32 DeviceId, FQuat& OutOrientation, FVector& OutPosition)
{
	FTrackingFrame* Frame = GetCurrentFrame();
	if (Frame)
	{
		OutOrientation = Frame->RawPose.GetRotation();
		OutPosition = Frame->RawPose.GetLocation();

		return true;
	}

	return false;
}

bool FMagicLeapHMD::GetRelativeEyePose(int32 DeviceId, EStereoscopicPass Eye, FQuat& OutOrientation, FVector& OutPosition)
{
#if WITH_MLSDK
	OutOrientation = FQuat::Identity;
	OutPosition = FVector::ZeroVector;
	if (DeviceId == IXRTrackingSystem::HMDDeviceId && (Eye == eSSP_LEFT_EYE || Eye == eSSP_RIGHT_EYE))
	{
		FTrackingFrame const* const Frame = GetCurrentFrame();
		const int EyeIdx = (Eye == eSSP_LEFT_EYE) ? 0 : 1;

		const FTransform EyeToWorld = MagicLeap::ToFTransform(Frame->RenderInfoArray.virtual_cameras[EyeIdx].transform, Frame->WorldToMetersScale);		// "world" here means the HMDs tracking space
		const FTransform EyeToHMD = EyeToWorld * Frame->RawPose.Inverse();		// RawPose is HMDToWorld
		OutPosition = EyeToHMD.GetTranslation();
		OutOrientation = EyeToHMD.GetRotation();

		return true;
	}

#endif //WITH_MLSDK
	return false;
}

void FMagicLeapHMD::GetEyeRenderParams_RenderThread(const FRenderingCompositePassContext& Context, FVector2D& EyeToSrcUVScaleValue, FVector2D& EyeToSrcUVOffsetValue) const
{
	check(bDeviceInitialized);
	check(IsInRenderingThread());

	if (Context.View.StereoPass == eSSP_LEFT_EYE)
	{
		EyeToSrcUVOffsetValue.X = 0.0f;
		EyeToSrcUVOffsetValue.Y = 0.0f;

		EyeToSrcUVScaleValue.X = 0.5f;
		EyeToSrcUVScaleValue.Y = 1.0f;
	}
	else
	{
		EyeToSrcUVOffsetValue.X = 0.5f;
		EyeToSrcUVOffsetValue.Y = 0.0f;

		EyeToSrcUVScaleValue.X = 0.5f;
		EyeToSrcUVScaleValue.Y = 1.0f;
	}
}

void FMagicLeapHMD::OnBeginRendering_GameThread()
{
	check(IsInGameThread());

	RefreshTrackingFrame();

#if WITH_MLSDK
	// Copy the game tracking frame to the render frame.
	// Since we don't flush the render commands here, we copy the game frame thrice:
	// 1st copy when enqueuing the command
	// 2nd copy on the render thread during the command execution
	ExecuteOnRenderThread_DoNotWait([this, TrackingFrameCopy = GameTrackingFrame]()
	{
		MLSnapshot* OldSnapshot = RenderTrackingFrame.Snapshot;
		RenderTrackingFrame = TrackingFrameCopy;
#if !PLATFORM_MAC
		ExecuteOnRHIThread_DoNotWait([this, TrackingFrameCopy]()
		{
			RHITrackingFrame = TrackingFrameCopy;
		});
#endif //PLATFORM_MAC
	});
#endif //WITH_MLSDK
}

TSharedPtr<class IXRCamera, ESPMode::ThreadSafe> FMagicLeapHMD::GetXRCamera(int32 DeviceId /*= HMDDeviceId*/)
{
	check(DeviceId == HMDDeviceId);
	if (!XRCamera.IsValid())
	{
		XRCamera = FSceneViewExtensions::NewExtension<FMagicLeapXRCamera>(*this, DeviceId);
	}
	return XRCamera;
}

void FMagicLeapHMD::OnBeginRendering_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& ViewFamily)
{
	check(IsInRenderingThread());

	if (GetActiveCustomPresent())
	{
		GetActiveCustomPresent()->BeginRendering();
	}

}

void FMagicLeapHMD::RenderTexture_RenderThread(class FRHICommandListImmediate& RHICmdList, class FRHITexture2D* BackBuffer, class FRHITexture2D* SrcTexture, FVector2D WindowSize) const
{
#if WITH_MLSDK
	check(IsInRenderingThread());

	// If we aren't mirroring there's nothing to do as the actual render on device
	// happens in the custom presenter.
	if (WindowMirrorMode > 0)
	{
		SCOPED_DRAW_EVENT(RHICmdList, HMD_RenderTexture);

		// The debug viewport is the mirror window (if any).
		const uint32 ViewportWidth = BackBuffer->GetSizeX();
		const uint32 ViewportHeight = BackBuffer->GetSizeY();
		// The source texture is the two eye side-by-side render.
		const uint32 TextureWidth = SrcTexture->GetSizeX();
		const uint32 TextureHeight = SrcTexture->GetSizeY();

		// The BackBuffer is the debug view for mirror modes, i.e. vr-preview. In which case
		// it can be an arbitrary size different than the render size. Which means
		// we scale to that BackBuffer size, with either a letter-box or pill-box to
		// maintain aspect ratio.
		const uint32 SourceWidth = WindowMirrorMode == 1 ? TextureWidth / 2 : TextureWidth;
		const uint32 SourceHeight = TextureHeight;
		const float LetterboxScale = static_cast<float>(ViewportWidth) / static_cast<float>(SourceWidth);
		const float PillarboxScale = static_cast<float>(ViewportHeight) / static_cast<float>(SourceHeight);
		const float BlitScale = FMath::Min(LetterboxScale, PillarboxScale);
		const uint32 BlitWidth = static_cast<float>(SourceWidth) * BlitScale;
		const uint32 BlitHeight = static_cast<float>(SourceHeight) * BlitScale;
		const uint32 QuadX = (ViewportWidth - BlitWidth) * 0.5f;
		const uint32 QuadY = (ViewportHeight - BlitHeight) * 0.5f;

		SetRenderTarget(RHICmdList, BackBuffer, FTextureRHIRef());
		DrawClearQuad(RHICmdList, FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
		RHICmdList.SetViewport(QuadX, QuadY, 0, BlitWidth + QuadX, BlitHeight + QuadY, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		const auto FeatureLevel = GMaxRHIFeatureLevel;
		auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
		TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
		TShaderMapRef<FScreenPS> PixelShader(ShaderMap);

		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = RendererModule->GetFilterVertexDeclaration().VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Bilinear>::GetRHI(), SrcTexture);

		if (WindowMirrorMode == 1)
		{
			RendererModule->DrawRectangle(
				RHICmdList,
				0, 0,
				ViewportWidth, ViewportHeight,
				0.0f, 0.0f,
				0.5f, 1.0f,
				FIntPoint(ViewportWidth, ViewportHeight),
				FIntPoint(1, 1),
				*VertexShader,
				EDRF_Default);
		}
		else if (WindowMirrorMode == 2)
		{
			RendererModule->DrawRectangle(
				RHICmdList,
				0, 0,
				ViewportWidth, ViewportHeight,
				0.0f, 0.0f,
				1.0f, 1.0f,
				FIntPoint(ViewportWidth, ViewportHeight),
				FIntPoint(1, 1),
				*VertexShader,
				EDRF_Default);
		}
	}
#endif //WITH_MLSDK
}

void FMagicLeapHMD::GetOrthoProjection(int32 RTWidth, int32 RTHeight, float OrthoDistance, FMatrix OrthoProjection[2]) const
{
	float ScalingFactor = 1.5;
	OrthoProjection[0] = FScaleMatrix(ScalingFactor);
	OrthoProjection[1] = FScaleMatrix(ScalingFactor);
	OrthoProjection[1] = FTranslationMatrix(FVector(OrthoProjection[1].M[0][3] * RTWidth * 0.25f + RTWidth * 0.5f, 0, 0)) * OrthoProjection[1];
}

void FMagicLeapHMD::SetClippingPlanes(float NCP, float FCP)
{
	check(IsInGameThread());
	FTrackingFrame* frame = GetCurrentFrame();
	if (frame != nullptr)
	{
		frame->FarClippingPlane = (frame->RecommendedFarClippingPlane < FCP) ? frame->RecommendedFarClippingPlane : FCP;
		GNearClippingPlane = NCP;
		UpdateNearClippingPlane();
	}
}

void FMagicLeapHMD::RegisterMagicLeapInputDevice(IMagicLeapInputDevice* InputDevice)
{
	check(InputDevice);
	InputDevices.Add(InputDevice);
}

void FMagicLeapHMD::UnregisterMagicLeapInputDevice(IMagicLeapInputDevice* InputDevice)
{
	check(InputDevice);
	InputDevices.Remove(InputDevice);
}

bool FMagicLeapHMD::IsInitialized() const
{
	return (AppFramework.IsInitialized());
}

void FMagicLeapHMD::ShutdownRendering()
{
	check(IsInRenderingThread());
#if PLATFORM_WINDOWS
	if (CustomPresentD3D11.GetReference())
	{
		CustomPresentD3D11->Reset();
		CustomPresentD3D11->Shutdown();
		CustomPresentD3D11 = nullptr;
	}
#endif // PLATFORM_WINDOWS
#if PLATFORM_MAC
	if (CustomPresentMetal.GetReference())
	{
		CustomPresentMetal->Reset();
		CustomPresentMetal->Shutdown();
		CustomPresentMetal = nullptr;
}
#endif // PLATFORM_MAC
#if PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_LUMIN
	if (CustomPresentOpenGL.GetReference())
	{
		CustomPresentOpenGL->Reset();
		CustomPresentOpenGL->Shutdown();
		CustomPresentOpenGL = nullptr;
	}
#endif // PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_LUMIN
#if PLATFORM_LUMIN
	if (CustomPresentVulkan.GetReference())
	{
		CustomPresentVulkan->Reset();
		CustomPresentVulkan->Shutdown();
		CustomPresentVulkan = nullptr;
	}
#endif // PLATFORM_LUMIN
}

FTrackingFrame* FMagicLeapHMD::GetCurrentFrame() const
{
	if (IsInRHIThread())
	{
		return const_cast<FTrackingFrame*>(&RHITrackingFrame);
	}
	if (IsInRenderingThread())
	{
		return const_cast<FTrackingFrame*>(&RenderTrackingFrame);
	}
	else
	{
		return const_cast<FTrackingFrame*>(&GameTrackingFrame);
	}
}

FTrackingFrame* FMagicLeapHMD::GetOldFrame() const
{
	check(IsInGameThread());
	return const_cast<FTrackingFrame*>(&OldTrackingFrame);
}

void FMagicLeapHMD::InitializeOldFrameFromRenderFrame()
{
	if (IsInRHIThread())
	{
		OldTrackingFrame = RHITrackingFrame;
	}
	else if (IsInRenderingThread())
	{
		OldTrackingFrame = RenderTrackingFrame;
	}
}

const FAppFramework& FMagicLeapHMD::GetAppFrameworkConst() const
{
	return AppFramework;
}

FAppFramework& FMagicLeapHMD::GetAppFramework()
{
	return AppFramework;
}

void FMagicLeapHMD::SetFocusActor(const AActor* InFocusActor)
{
	FocusActor = InFocusActor;
}

bool FMagicLeapHMD::IsPerceptionEnabled() const
{
	return bIsPerceptionEnabled;
}

void FMagicLeapHMD::EnableLuminProfile()
{
	if (!GIsEditor)
	{
		// We only need to enable, and hence disable, the profile while doing vr-preview. Which
		// only is relevant while we are in the editor.
		return;
	}

	UDeviceProfileManager& ProfileManager = UDeviceProfileManager::Get();
	UDeviceProfile* Profile = ProfileManager.FindProfile("Lumin");
	UDeviceProfile* ActiveProfile = ProfileManager.GetActiveProfile();
	bool ShouldEnable =
		Profile &&
		(Profile != ActiveProfile) &&
		!BaseProfileState.bSaved;

	if (ShouldEnable)
	{
		for (const FString& CVarEntry : Profile->CVars)
		{
			FString CVarKey;
			FString CVarValue;
			TMap<FString, FString> ValidCVars;
			if (CVarEntry.Split(TEXT("="), &CVarKey, &CVarValue))
			{
				ValidCVars.Add(CVarKey, CVarValue);

				IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*CVarKey);
				if (CVar)
				{
					FString PreviousValue = CVar->GetString();
					BaseProfileState.CVarState.Add(CVarKey, PreviousValue);
					CVar->Set(*CVarValue);
				}
			}
		}

#if WITH_EDITOR
		UEditorPerformanceSettings* Settings = GetMutableDefault<UEditorPerformanceSettings>();
		check(Settings);
		BaseProfileState.bCPUThrottleEnabled = Settings->bThrottleCPUWhenNotForeground;
		Settings->bThrottleCPUWhenNotForeground = false;
		Settings->PostEditChange();
		Settings->SaveConfig();
#endif

		BaseProfileState.bSaved = true;
	}
}

void FMagicLeapHMD::RestoreBaseProfile()
{
	//if we're quitting, we shouldn't be restoring a profile
	if (!GIsRunning)
	{
		return;
	}

	if (!GIsEditor)
	{
		// We only need to enable, and hence disable, the profile while doing vr-preview. Which
		// only is relevant while we are in the editor.
		return;
	}

	if (BaseProfileState.bSaved)
	{
#if WITH_EDITOR
		UEditorPerformanceSettings* Settings = GetMutableDefault<UEditorPerformanceSettings>();
		check(Settings);
		Settings->bThrottleCPUWhenNotForeground = BaseProfileState.bCPUThrottleEnabled;
		Settings->PostEditChange();
		Settings->SaveConfig();
#endif

		for (auto& It : BaseProfileState.CVarState)
		{
			IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*It.Key);
			if (CVar)
			{
				CVar->Set(*It.Value);
			}
		}

		BaseProfileState.bSaved = false;
		BaseProfileState.CVarState.Empty();
	}
}

void FMagicLeapHMD::EnableInputDevices()
{
	for (auto& It : InputDevices)
	{
		if (It->SupportsExplicitEnable())
		{
			It->Enable();
		}
	}
}

void FMagicLeapHMD::DisableInputDevices()
{
	for (auto& It : InputDevices)
	{
		It->Disable();
	}
}

void FMagicLeapHMD::EnablePerception()
{
#if WITH_MLSDK
	if (!bIsPerceptionEnabled)
	{
		MLPerceptionSettings perception_settings;
		MLPerceptionInitSettings(&perception_settings);
		bIsPerceptionEnabled = MLPerceptionStartup(&perception_settings);
		if (!bIsPerceptionEnabled)
		{
			UE_LOG(LogMagicLeap, Error, TEXT("Error starting perception client."));
		}
	}
#endif //WITH_MLSDK
}

void FMagicLeapHMD::DisablePerception()
{
#if WITH_MLSDK
	if (bIsPerceptionEnabled)
	{
		bIsPerceptionEnabled = !MLPerceptionShutdown();
		if (bIsPerceptionEnabled)
		{
			UE_LOG(LogMagicLeap, Error, TEXT("Error shutting down perception client."));
		}
		else
		{
			UE_LOG(LogMagicLeap, Display, TEXT("Perception client shutdown successfully."));
		}
	}
#endif //WITH_MLSDK
}

void FMagicLeapHMD::EnableHeadTracking()
{
#if WITH_MLSDK
	if (HeadTracker == ML_INVALID_HANDLE)
	{
		HeadTracker = MLHeadTrackingCreate();
		if (HeadTracker != ML_INVALID_HANDLE)
		{
			MLHeadTrackingGetStaticData(HeadTracker, &HeadTrackerData);
		}
		else
		{
			UE_LOG(LogMagicLeap, Error, TEXT("Error starting head tracking."));
		}
	}
#endif //WITH_MLSDK
}

void FMagicLeapHMD::DisableHeadTracking()
{
#if WITH_MLSDK
	if (HeadTracker != ML_INVALID_HANDLE)
	{
		bool bResult = MLHeadTrackingDestroy(HeadTracker);
		if (bResult)
		{
			HeadTracker = ML_INVALID_HANDLE;
		}
		else
		{
			UE_LOG(LogMagicLeap, Error, TEXT("Error destroying head tracking."));
		}
	}
#endif //WITH_MLSDK
}

void FMagicLeapHMD::InitializeClipExtents_RenderThread()
{
#if WITH_MLSDK
	MLStatus OutStatus;
	MLGraphicsRenderTargetsInfo RenderTargetInfo;
	bool bResult = MLGraphicsGetRenderTargets(GraphicsClient, &RenderTargetInfo, &OutStatus);
	if (bResult)
	{
		GameTrackingFrame.NearClippingPlane = RenderTargetInfo.min_clip * GameTrackingFrame.WorldToMetersScale;
		GameTrackingFrame.RecommendedFarClippingPlane = RenderTargetInfo.max_clip * GameTrackingFrame.WorldToMetersScale;
		UpdateNearClippingPlane();
	}
	else
	{
		UE_LOG(LogMagicLeap, Error, TEXT("MLGraphicsGetRenderTargets() failed with status %d"), static_cast<int32>(OutStatus));
	}

	// get the clip extents for clipping content in update thread
	bResult = MLGraphicsGetClipExtents(GraphicsClient, &GameTrackingFrame.UpdateInfoArray, &OutStatus);
	if (!bResult)
	{
		FString ErrorMesg = FString::Printf(TEXT("MLGraphicsGetClipExtents failed with status %d"), static_cast<int32>(OutStatus));

		// In case we're running under VD/ZI, there's always the risk of disconnects.
		// In those cases, the graphics API can return an error, but the client handle might still be valid.
		// So we need to ensure that we always have valid data to prevent any Nan-related errors.
		// On Lumin, we'll just assert.
#if PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_MAC
		GameTrackingFrame.Handle = ML_INVALID_HANDLE;
		MagicLeap::ResetClipExtentsInfoArray(GameTrackingFrame.UpdateInfoArray);
		UE_LOG(LogMagicLeap, Error, TEXT("%s"), *ErrorMesg);
#else
		UE_LOG(LogMagicLeap, Fatal, TEXT("%s"), *ErrorMesg);
#endif
	}

	/* Expected Right Handed Projection Model */
	/*
	MLGraphicsProjectionType_ReversedInfiniteZ
	proj_mat[2][2] = 0.0f;
	proj_mat[2][3] = -1.0f;
	proj_mat[3][2] = near_clip_meters;
	*/

	/* Convert full extents from Graphics Projection Model to Unreal Projection Model */
	// Graphics returns values in Infinite Z. We convert it to Reversed Infinite Z here.
	GameTrackingFrame.UpdateInfoArray.full_extents.projection.matrix_colmajor[10] = 0.0f; // Model change hack
	GameTrackingFrame.UpdateInfoArray.full_extents.projection.matrix_colmajor[11] = -1.0f; // Model change hack

	// We also convert the near plane into centimeters since Unreal directly uses these values
	// for various calculations such as shadow algorithm and expects units to be in centimeters
	GameTrackingFrame.UpdateInfoArray.full_extents.projection.matrix_colmajor[14] = GNearClippingPlane; // Model change hack

	/* Convert eye extents from Graphics Projection Model to Unreal Projection Model */
	for (uint32_t eye = 0; eye < GameTrackingFrame.UpdateInfoArray.num_virtual_cameras; ++eye)
	{
		// Graphics returns values in Infinite Z. We convert it to Reversed Infinite Z here.
		GameTrackingFrame.UpdateInfoArray.virtual_camera_extents[eye].projection.matrix_colmajor[10] = 0.0f; // Model change hack
		GameTrackingFrame.UpdateInfoArray.virtual_camera_extents[eye].projection.matrix_colmajor[11] = -1.0f; // Model change hack

		// We also convert the near plane into centimeters since Unreal directly uses these values
		// for various calculations such as shadow algorithm and expects units to be in centimeters
		GameTrackingFrame.UpdateInfoArray.virtual_camera_extents[eye].projection.matrix_colmajor[14] = GNearClippingPlane; // Model change hack
	}

	// TODO Apply snapshot head pose to all the update transforms because graphics does not apply pose
	// But we currently use the last frame render transforms so this does not need to be done just yet
#endif //WITH_MLSDK
}

#if WITH_EDITOR
UGameViewportClient * FMagicLeapHMD::GetGameViewportClient()
{
	return World ? World->GetGameViewport() : nullptr;
}

FMagicLeapHMD* FMagicLeapHMD::GetHMD()
{
	return static_cast<FMagicLeapHMD*>(GEngine->XRSystem->GetHMDDevice());
}
#endif

#undef LOCTEXT_NAMESPACE
