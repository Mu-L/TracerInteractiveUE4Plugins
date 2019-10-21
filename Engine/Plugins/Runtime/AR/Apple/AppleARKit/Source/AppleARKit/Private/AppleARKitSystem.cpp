// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AppleARKitSystem.h"
#include "DefaultXRCamera.h"
#include "AppleARKitSessionDelegate.h"
#include "Misc/ScopeLock.h"
#include "AppleARKitModule.h"
#include "AppleARKitConversion.h"
#include "AppleARKitVideoOverlay.h"
#include "AppleARKitFrame.h"
#include "AppleARKitConversion.h"
#include "GeneralProjectSettings.h"
#include "ARSessionConfig.h"
#include "AppleARKitSettings.h"
#include "AppleARKitTrackable.h"
#include "ARLightEstimate.h"
#include "ARTraceResult.h"
#include "ARPin.h"
#include "Async/Async.h"
#include "HAL/ThreadSafeCounter.h"
#include "Misc/FileHelper.h"

// For mesh occlusion
#include "MRMeshComponent.h"
#include "AROriginActor.h"

// To separate out the face ar library linkage from standard ar apps
#include "AppleARKitFaceSupport.h"
#include "AppleARKitPoseTrackingLiveLink.h"

// For orientation changed
#include "Misc/CoreDelegates.h"

#if PLATFORM_IOS
	#include "IOSRuntimeSettings.h"

	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wpartial-availability"
#endif

DECLARE_CYCLE_STAT(TEXT("SessionDidUpdateFrame_DelegateThread"), STAT_FAppleARKitSystem_SessionUpdateFrame, STATGROUP_ARKIT);
DECLARE_CYCLE_STAT(TEXT("SessionDidAddAnchors_DelegateThread"), STAT_FAppleARKitSystem_SessionDidAddAnchors, STATGROUP_ARKIT);
DECLARE_CYCLE_STAT(TEXT("SessionDidUpdateAnchors_DelegateThread"), STAT_FAppleARKitSystem_SessionDidUpdateAnchors, STATGROUP_ARKIT);
DECLARE_CYCLE_STAT(TEXT("SessionDidRemoveAnchors_DelegateThread"), STAT_FAppleARKitSystem_SessionDidRemoveAnchors, STATGROUP_ARKIT);
DECLARE_CYCLE_STAT(TEXT("UpdateARKitPerf"), STAT_FAppleARKitSystem_UpdateARKitPerf, STATGROUP_ARKIT);
DECLARE_DWORD_COUNTER_STAT(TEXT("ARKit CPU %"), STAT_ARKitThreads, STATGROUP_ARKIT);

// Copied from IOSPlatformProcess because it's not accessible by external code
#define GAME_THREAD_PRIORITY 47
#define RENDER_THREAD_PRIORITY 45

#if PLATFORM_IOS && !PLATFORM_TVOS
// Copied from IOSPlatformProcess because it's not accessible by external code
static void SetThreadPriority(int32 Priority)
{
	struct sched_param Sched;
	FMemory::Memzero(&Sched, sizeof(struct sched_param));
	
	// Read the current priority and policy
	int32 CurrentPolicy = SCHED_RR;
	pthread_getschedparam(pthread_self(), &CurrentPolicy, &Sched);
	
	// Set the new priority and policy (apple recommended FIFO for the two main non-working threads)
	int32 Policy = SCHED_FIFO;
	Sched.sched_priority = Priority;
	pthread_setschedparam(pthread_self(), Policy, &Sched);
}
#else
static void SetThreadPriority(int32 Priority)
{
	// Ignored
}
#endif

//
//  FAppleARKitXRCamera
//

class FAppleARKitXRCamera : public FDefaultXRCamera
{
public:
	FAppleARKitXRCamera(const FAutoRegister& AutoRegister, FAppleARKitSystem& InTrackingSystem, int32 InDeviceId)
	: FDefaultXRCamera( AutoRegister, &InTrackingSystem, InDeviceId )
	, ARKitSystem( InTrackingSystem )
	{}
	
	void AdjustThreadPriority(int32 NewPriority)
	{
		ThreadPriority.Set(NewPriority);
	}

	void SetOverlayTexture(UARTextureCameraImage* InCameraImage)
	{
		VideoOverlay.SetOverlayTexture(InCameraImage);
	}
	
	void SetEnablePersonOcclusion(bool bEnable)
    {
        VideoOverlay.SetEnablePersonOcclusion(bEnable);
    }
	
private:
	//~ FDefaultXRCamera
	void OverrideFOV(float& InOutFOV)
	{
		// @todo arkit : is it safe not to lock here? Theoretically this should only be called on the game thread.
		ensure(IsInGameThread());
		const bool bShouldOverrideFOV = ARKitSystem.GetARCompositionComponent()->GetSessionConfig().ShouldRenderCameraOverlay();
		if (bShouldOverrideFOV && ARKitSystem.GameThreadFrame.IsValid())
		{
			if (ARKitSystem.DeviceOrientation == EDeviceScreenOrientation::Portrait || ARKitSystem.DeviceOrientation == EDeviceScreenOrientation::PortraitUpsideDown)
			{
				// Portrait
				InOutFOV = ARKitSystem.GameThreadFrame->Camera.GetVerticalFieldOfViewForScreen(EAppleARKitBackgroundFitMode::Fill);
			}
			else
			{
				// Landscape
				InOutFOV = ARKitSystem.GameThreadFrame->Camera.GetHorizontalFieldOfViewForScreen(EAppleARKitBackgroundFitMode::Fill);
			}
		}
	}
	
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override
	{
		FDefaultXRCamera::SetupView(InViewFamily, InView);
	}
	
	virtual void SetupViewProjectionMatrix(FSceneViewProjectionData& InOutProjectionData) override
	{
		FDefaultXRCamera::SetupViewProjectionMatrix(InOutProjectionData);
	}
	
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override
	{
		FDefaultXRCamera::BeginRenderViewFamily(InViewFamily);
	}
	
	virtual void PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) override
	{
		// Adjust our thread priority if requested
		if (LastThreadPriority.GetValue() != ThreadPriority.GetValue())
		{
			SetThreadPriority(ThreadPriority.GetValue());
			LastThreadPriority.Set(ThreadPriority.GetValue());
		}
		FDefaultXRCamera::PreRenderView_RenderThread(RHICmdList, InView);
	}
	
	virtual void PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) override
	{
		// Grab the latest frame from ARKit
		{
			FScopeLock ScopeLock(&ARKitSystem.FrameLock);
			ARKitSystem.RenderThreadFrame = ARKitSystem.LastReceivedFrame;
		}

		FDefaultXRCamera::PreRenderViewFamily_RenderThread(RHICmdList, InViewFamily);
	}
	
	virtual void PostRenderBasePass_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) override
	{
		if (ARKitSystem.RenderThreadFrame.IsValid())
		{
			VideoOverlay.RenderVideoOverlay_RenderThread(RHICmdList, InView, *ARKitSystem.RenderThreadFrame, ARKitSystem.DeviceOrientation, ARKitSystem.GetWorldToMetersScale());
		}
	}
	
	virtual bool GetPassthroughCameraUVs_RenderThread(TArray<FVector2D>& OutUVs) override
	{
		return VideoOverlay.GetPassthroughCameraUVs_RenderThread(OutUVs, ARKitSystem.DeviceOrientation);
	}

	virtual bool IsActiveThisFrame(class FViewport* InViewport) const override
	{
		// Base implementation needs this call as it updates bCurrentFrameIsStereoRendering as a side effect.
		// We'll ignore the result however.
		FDefaultXRCamera::IsActiveThisFrame(InViewport);

		// Check to see if they have disabled the automatic rendering or not
		// Most Face AR apps that are driving other meshes using the face capture (animoji style) will disable this.
		const bool bRenderOverlay =
			ARKitSystem.OnGetARSessionStatus().Status == EARSessionStatus::Running &&
			ARKitSystem.GetARCompositionComponent()->GetSessionConfig().ShouldRenderCameraOverlay();

#if SUPPORTS_ARKIT_1_0
		if (FAppleARKitAvailability::SupportsARKit10())
		{
			return bRenderOverlay;
		}
#endif
		return false;
	}
	//~ FDefaultXRCamera
	
private:
	FAppleARKitSystem& ARKitSystem;
	FAppleARKitVideoOverlay VideoOverlay;
	
	// Thread priority support
	FThreadSafeCounter ThreadPriority;
	FThreadSafeCounter LastThreadPriority;
};

//
//  FAppleARKitSystem
//

FAppleARKitSystem::FAppleARKitSystem()
: FXRTrackingSystemBase(this)
, DeviceOrientation(EDeviceScreenOrientation::Unknown)
, DerivedTrackingToUnrealRotation(FRotator::ZeroRotator)
, LightEstimate(nullptr)
, CameraImage(nullptr)
, CameraDepth(nullptr)
, LastTrackedGeometry_DebugId(0)
, FaceARSupport(nullptr)
, PoseTrackingARLiveLink(nullptr)
, TimecodeProvider(nullptr)
{
	// See Initialize(), as we need access to SharedThis()
#if SUPPORTS_ARKIT_1_0
	IAppleImageUtilsPlugin::Load();
#endif
}

FAppleARKitSystem::~FAppleARKitSystem()
{
	// Unregister our ability to hit-test in AR with Unreal
}

void FAppleARKitSystem::Shutdown()
{
#if SUPPORTS_ARKIT_1_0
	if (Session != nullptr)
	{
		FaceARSupport = nullptr;
		PoseTrackingARLiveLink = nullptr;
		[Session pause];
		Session.delegate = nullptr;
		[Session release];
		Session = nullptr;
	}
#endif
	CameraDepth = nullptr;
	CameraImage = nullptr;
	
	PersonSegmentationImage = nullptr;
	PersonSegmentationDepthImage = nullptr;
}

void FAppleARKitSystem::CheckForFaceARSupport(UARSessionConfig* InSessionConfig)
{
	if (InSessionConfig->GetSessionType() != EARSessionType::Face)
	{
		// Clear the face ar support so we don't forward to it
		FaceARSupport = nullptr;
		return;
	}

	// We need to get the face support from the factory method, which is a modular feature to avoid dependencies
	TArray<IAppleARKitFaceSupport*> Impls = IModularFeatures::Get().GetModularFeatureImplementations<IAppleARKitFaceSupport>("AppleARKitFaceSupport");
	if (ensureAlwaysMsgf(Impls.Num() > 0, TEXT("Face AR session has been requested but the face ar plugin is not enabled")))
	{
		FaceARSupport = Impls[0];
		ensureAlwaysMsgf(FaceARSupport != nullptr, TEXT("Face AR session has been requested but the face ar plugin is not enabled"));
	}
}

void FAppleARKitSystem::CheckForPoseTrackingARLiveLink(UARSessionConfig* InSessionConfig)
{
#if SUPPORTS_ARKIT_3_0	
	if (InSessionConfig->GetSessionType() != EARSessionType::PoseTracking)
	{
		// Clear the face ar support so we don't forward to it
		PoseTrackingARLiveLink = nullptr;
		return;
	}

	// We need to get the face support from the factory method, which is a modular feature to avoid dependencies
	TArray<IAppleARKitPoseTrackingLiveLink*> Impls = IModularFeatures::Get().GetModularFeatureImplementations<IAppleARKitPoseTrackingLiveLink>("AppleARKitPoseTrackingLiveLink");
	if (ensureAlwaysMsgf(Impls.Num() > 0, TEXT("Body Tracking AR session has been requested but the body tracking ar plugin is not enabled")))
	{
		PoseTrackingARLiveLink = Impls[0];
		ensureAlwaysMsgf(PoseTrackingARLiveLink != nullptr, TEXT("Body Tracking AR session has been requested but the body tracking ar plugin is not enabled"));
	}
#endif
}

FName FAppleARKitSystem::GetSystemName() const
{
	static const FName AppleARKitSystemName(TEXT("AppleARKit"));
	return AppleARKitSystemName;
}

bool FAppleARKitSystem::GetCurrentPose(int32 DeviceId, FQuat& OutOrientation, FVector& OutPosition)
{
	if (DeviceId == IXRTrackingSystem::HMDDeviceId && GameThreadFrame.IsValid() && IsHeadTrackingAllowed())
	{
		// Do not have to lock here, because we are on the game
		// thread and GameThreadFrame is only written to from the game thread.
		
		
		// Apply alignment transform if there is one.
		FTransform CurrentTransform(GameThreadFrame->Camera.Orientation, GameThreadFrame->Camera.Translation);
		CurrentTransform = FTransform(DerivedTrackingToUnrealRotation) * CurrentTransform;
		CurrentTransform *= GetARCompositionComponent()->GetAlignmentTransform();
		
		
		// Apply counter-rotation to compensate for mobile device orientation
		OutOrientation = CurrentTransform.GetRotation();
		OutPosition = CurrentTransform.GetLocation();

		return true;
	}
	else
	{
		return false;
	}
}

FString FAppleARKitSystem::GetVersionString() const
{
	return TEXT("AppleARKit - V1.0");
}


bool FAppleARKitSystem::EnumerateTrackedDevices(TArray<int32>& OutDevices, EXRTrackedDeviceType Type)
{
	if (Type == EXRTrackedDeviceType::Any || Type == EXRTrackedDeviceType::HeadMountedDisplay)
	{
		static const int32 DeviceId = IXRTrackingSystem::HMDDeviceId;
		OutDevices.Add(DeviceId);
		return true;
	}
	return false;
}

void FAppleARKitSystem::CalcTrackingToWorldRotation()
{
	// We rotate the camera to counteract the portrait vs. landscape viewport rotation
	DerivedTrackingToUnrealRotation = FRotator::ZeroRotator;

	const EARWorldAlignment WorldAlignment = GetARCompositionComponent()->GetSessionConfig().GetWorldAlignment();
	if (WorldAlignment == EARWorldAlignment::Gravity || WorldAlignment == EARWorldAlignment::GravityAndHeading)
	{
		switch (DeviceOrientation)
		{
			case EDeviceScreenOrientation::Portrait:
				DerivedTrackingToUnrealRotation = FRotator(0.0f, 0.0f, -90.0f);
				break;
				
			case EDeviceScreenOrientation::PortraitUpsideDown:
				DerivedTrackingToUnrealRotation = FRotator(0.0f, 0.0f, 90.0f);
				break;
				
			default:
			case EDeviceScreenOrientation::LandscapeRight:
				break;
				
			case EDeviceScreenOrientation::LandscapeLeft:
				DerivedTrackingToUnrealRotation = FRotator(0.0f, 0.0f, 180.0f);
				break;
		}
	}
	// Camera aligned which means +X is to the right along the long axis
	else
	{
		switch (DeviceOrientation)
		{
			case EDeviceScreenOrientation::Portrait:
				DerivedTrackingToUnrealRotation = FRotator(0.0f, 0.0f, 90.0f);
				break;
				
			case EDeviceScreenOrientation::PortraitUpsideDown:
				DerivedTrackingToUnrealRotation = FRotator(0.0f, 0.0f, -90.0f);
				break;
				
			default:
			case EDeviceScreenOrientation::LandscapeLeft:
				DerivedTrackingToUnrealRotation = FRotator(0.0f, 0.0f, -180.0f);
				break;
				
			case EDeviceScreenOrientation::LandscapeRight:
				break;
		}
	}
}

void FAppleARKitSystem::UpdateFrame()
{
	FScopeLock ScopeLock( &FrameLock );
	// This might get called multiple times per frame so only update if delegate version is newer
	if (!GameThreadFrame.IsValid() || !LastReceivedFrame.IsValid() ||
		GameThreadFrame->Timestamp < LastReceivedFrame->Timestamp)
	{
		GameThreadFrame = LastReceivedFrame;
		if (GameThreadFrame.IsValid())
		{
#if SUPPORTS_ARKIT_1_0
			if (GameThreadFrame->CameraImage != nullptr)
			{
				// Reuse the UObjects because otherwise the time between GCs causes ARKit to be starved of resources
                CameraImage->Init(FPlatformTime::Seconds(), GameThreadFrame->CameraImage);
			}

			if (GameThreadFrame->CameraDepth != nullptr)
			{
				// Reuse the UObjects because otherwise the time between GCs causes ARKit to be starved of resources
				CameraDepth->Init(FPlatformTime::Seconds(), GameThreadFrame->CameraDepth);
			}
#endif
			
#if SUPPORTS_ARKIT_3_0
			FAppleARKitXRCamera* Camera = GetARKitXRCamera();
			check(Camera);
			
			if (GameThreadFrame->SegmentationBuffer)
			{
				if (!PersonSegmentationImage)
				{
					PersonSegmentationImage = NewObject<UAppleARKitTextureCameraImage>();
				}
				PersonSegmentationImage->Init(FPlatformTime::Seconds(), GameThreadFrame->SegmentationBuffer);
				PersonSegmentationImage->EnqueueNewCameraImage(GameThreadFrame->SegmentationBuffer);
			}
			
			if (GameThreadFrame->EstimatedDepthData)
			{
				if (!PersonSegmentationDepthImage)
				{
					PersonSegmentationDepthImage = NewObject<UAppleARKitTextureCameraImage>();
				}
				PersonSegmentationDepthImage->Init(FPlatformTime::Seconds(), GameThreadFrame->EstimatedDepthData);
				PersonSegmentationDepthImage->EnqueueNewCameraImage(GameThreadFrame->EstimatedDepthData);
			}
#endif
		}
	}
}

void FAppleARKitSystem::UpdatePoses()
{
	UpdateFrame();
}


void FAppleARKitSystem::ResetOrientationAndPosition(float Yaw)
{
	// @todo arkit implement FAppleARKitSystem::ResetOrientationAndPosition
}

bool FAppleARKitSystem::IsHeadTrackingAllowed() const
{
	// Check to see if they have disabled the automatic camera tracking or not
	// For face AR tracking movements of the device most likely won't want to be tracked
	const bool bEnableCameraTracking =
		OnGetARSessionStatus().Status == EARSessionStatus::Running &&
		GetARCompositionComponent()->GetSessionConfig().ShouldEnableCameraTracking();

#if SUPPORTS_ARKIT_1_0
	if (FAppleARKitAvailability::SupportsARKit10())
	{
		return bEnableCameraTracking;
	}
	else
	{
		return false;
	}
#else
	return false;
#endif
}

TSharedPtr<class IXRCamera, ESPMode::ThreadSafe> FAppleARKitSystem::GetXRCamera(int32 DeviceId)
{
	// Don't create/load UObjects on the render thread
	if (!XRCamera.IsValid() && IsInGameThread())
	{
		TSharedRef<FAppleARKitXRCamera, ESPMode::ThreadSafe> NewCamera = FSceneViewExtensions::NewExtension<FAppleARKitXRCamera>(*this, DeviceId);
		XRCamera = NewCamera;
	}
	
	return XRCamera;
}

FAppleARKitXRCamera* FAppleARKitSystem::GetARKitXRCamera()
{
	return (FAppleARKitXRCamera*)GetXRCamera(0).Get();
}

float FAppleARKitSystem::GetWorldToMetersScale() const
{
	// @todo arkit FAppleARKitSystem::GetWorldToMetersScale needs a real scale somehow
	return 100.0f;
}

void FAppleARKitSystem::OnBeginRendering_GameThread()
{
#if PLATFORM_MAC || PLATFORM_IOS
    // Queue an update on the render thread
	CameraImage->Init_RenderThread();
	
	if (PersonSegmentationImage)
	{
		PersonSegmentationImage->Init_RenderThread();
	}
	
	if (PersonSegmentationDepthImage)
	{
		PersonSegmentationDepthImage->Init_RenderThread();
	}
#endif
	UpdatePoses();
}

bool FAppleARKitSystem::OnStartGameFrame(FWorldContext& WorldContext)
{
	FXRTrackingSystemBase::OnStartGameFrame(WorldContext);
	
	CachedTrackingToWorld = ComputeTrackingToWorldTransform(WorldContext);
	
	if (GameThreadFrame.IsValid())
	{
		if (GameThreadFrame->LightEstimate.bIsValid)
		{
			UARBasicLightEstimate* NewLightEstimate = NewObject<UARBasicLightEstimate>();
			NewLightEstimate->SetLightEstimate( GameThreadFrame->LightEstimate.AmbientIntensity,  GameThreadFrame->LightEstimate.AmbientColorTemperatureKelvin);
			LightEstimate = NewLightEstimate;
		}
		else
		{
			LightEstimate = nullptr;
		}
		
	}
	
	return true;
}

void* FAppleARKitSystem::GetARSessionRawPointer()
{
#if SUPPORTS_ARKIT_1_0
	return static_cast<void*>(Session);
#endif
	ensureAlwaysMsgf(false, TEXT("FAppleARKitSystem::GetARSessionRawPointer is unimplemented on current platform."));
	return nullptr;
}

void* FAppleARKitSystem::GetGameThreadARFrameRawPointer()
{
#if SUPPORTS_ARKIT_1_0
	if (GameThreadFrame.IsValid())
	{
		return GameThreadFrame->NativeFrame;
	}
	else
	{
		return nullptr;
	}
#endif
	ensureAlwaysMsgf(false, TEXT("FAppleARKitSystem::GetARGameThreadFrameRawPointer is unimplemented on current platform."));
	return nullptr;
}

//bool FAppleARKitSystem::ARLineTraceFromScreenPoint(const FVector2D ScreenPosition, TArray<FARTraceResult>& OutHitResults)
//{
//	const bool bSuccess = HitTestAtScreenPosition(ScreenPosition, EAppleARKitHitTestResultType::ExistingPlaneUsingExtent, OutHitResults);
//	return bSuccess;
//}

void FAppleARKitSystem::OnARSystemInitialized()
{
	// Register for device orientation changes
	FCoreDelegates::ApplicationReceivedScreenOrientationChangedNotificationDelegate.AddThreadSafeSP(this, &FAppleARKitSystem::OrientationChanged);
}

EARTrackingQuality FAppleARKitSystem::OnGetTrackingQuality() const
{
	return GameThreadFrame.IsValid()
		? GameThreadFrame->Camera.TrackingQuality
		: EARTrackingQuality::NotTracking;
}

void FAppleARKitSystem::OnStartARSession(UARSessionConfig* SessionConfig)
{
	Run(SessionConfig);
}

void FAppleARKitSystem::OnPauseARSession()
{
	ensureAlwaysMsgf(false, TEXT("FAppleARKitSystem::OnPauseARSession() is unimplemented."));
}

void FAppleARKitSystem::OnStopARSession()
{
	Pause();
}

FARSessionStatus FAppleARKitSystem::OnGetARSessionStatus() const
{
	return IsRunning()
		? FARSessionStatus(EARSessionStatus::Running)
		: FARSessionStatus(EARSessionStatus::NotStarted);
}

void FAppleARKitSystem::OnSetAlignmentTransform(const FTransform& InAlignmentTransform)
{
	const FTransform& NewAlignmentTransform = InAlignmentTransform;
	
	// Update transform for all geometries
	for (auto GeoIt=TrackedGeometries.CreateIterator(); GeoIt; ++GeoIt)
	{
		GeoIt.Value()->UpdateAlignmentTransform(NewAlignmentTransform);
	}
	
	// Update transform for all Pins
	for (UARPin* Pin : Pins)
	{
		Pin->UpdateAlignmentTransform(NewAlignmentTransform);
	}
}

static bool IsHitInRange( float UnrealHitDistance )
{
    // Skip results further than 5m or closer that 20cm from camera
	return 20.0f < UnrealHitDistance && UnrealHitDistance < 500.0f;
}

#if SUPPORTS_ARKIT_1_0

static UARTrackedGeometry* FindGeometryFromAnchor( ARAnchor* InAnchor, TMap<FGuid, UARTrackedGeometry*>& Geometries )
{
	if (InAnchor != NULL)
	{
		const FGuid AnchorGUID = FAppleARKitConversion::ToFGuid( InAnchor.identifier );
		UARTrackedGeometry** Result = Geometries.Find(AnchorGUID);
		if (Result != nullptr)
		{
			return *Result;
		}
	}
	
	return nullptr;
}

#endif

TArray<FARTraceResult> FAppleARKitSystem::OnLineTraceTrackedObjects( const FVector2D ScreenCoord, EARLineTraceChannels TraceChannels )
{
	const float WorldToMetersScale = GetWorldToMetersScale();
	TArray<FARTraceResult> Results;
	
	// Sanity check
	if (IsRunning())
	{
#if SUPPORTS_ARKIT_1_0
		
		TSharedPtr<FARSupportInterface , ESPMode::ThreadSafe> This = GetARCompositionComponent();
		
		@autoreleasepool
		{
			// Perform a hit test on the Session's last frame
			ARFrame* HitTestFrame = Session.currentFrame;
			if (HitTestFrame)
			{
				Results.Reserve(8);
				
				// Convert the screen position to normalised coordinates in the capture image space
				FVector2D NormalizedImagePosition = FAppleARKitCamera( HitTestFrame.camera ).GetImageCoordinateForScreenPosition( ScreenCoord, EAppleARKitBackgroundFitMode::Fill );
				switch (DeviceOrientation)
				{
					case EDeviceScreenOrientation::Portrait:
						NormalizedImagePosition = FVector2D( NormalizedImagePosition.Y, 1.0f - NormalizedImagePosition.X );
						break;
						
					case EDeviceScreenOrientation::PortraitUpsideDown:
						NormalizedImagePosition = FVector2D( 1.0f - NormalizedImagePosition.Y, NormalizedImagePosition.X );
						break;
						
					default:
					case EDeviceScreenOrientation::LandscapeRight:
						break;
						
					case EDeviceScreenOrientation::LandscapeLeft:
						NormalizedImagePosition = FVector2D(1.0f, 1.0f) - NormalizedImagePosition;
						break;
				};
				
				// GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Blue, FString::Printf(TEXT("Hit Test At Screen Position: x: %f, y: %f"), NormalizedImagePosition.X, NormalizedImagePosition.Y));
				
				// First run hit test against existing planes with extents (converting & filtering results as we go)
				if (!!(TraceChannels & EARLineTraceChannels::PlaneUsingExtent) || !!(TraceChannels & EARLineTraceChannels::PlaneUsingBoundaryPolygon))
				{
					// First run hit test against existing planes with extents (converting & filtering results as we go)
					NSArray< ARHitTestResult* >* PlaneHitTestResults = [HitTestFrame hitTest:CGPointMake(NormalizedImagePosition.X, NormalizedImagePosition.Y) types:ARHitTestResultTypeExistingPlaneUsingExtent];
					for ( ARHitTestResult* HitTestResult in PlaneHitTestResults )
					{
						const float UnrealHitDistance = HitTestResult.distance * WorldToMetersScale;
						if ( IsHitInRange( UnrealHitDistance ) )
						{
							// Hit result has passed and above filtering, add it to the list
							// Convert to Unreal's Hit Test result format
							Results.Add(FARTraceResult(This, UnrealHitDistance, EARLineTraceChannels::PlaneUsingExtent, FAppleARKitConversion::ToFTransform(HitTestResult.worldTransform)*GetARCompositionComponent()->GetAlignmentTransform(), FindGeometryFromAnchor(HitTestResult.anchor, TrackedGeometries)));
						}
					}
				}
				
				// If there were no valid results, fall back to hit testing against one shot plane
				if (!!(TraceChannels & EARLineTraceChannels::GroundPlane))
				{
					NSArray< ARHitTestResult* >* PlaneHitTestResults = [HitTestFrame hitTest:CGPointMake(NormalizedImagePosition.X, NormalizedImagePosition.Y) types:ARHitTestResultTypeEstimatedHorizontalPlane];
					for ( ARHitTestResult* HitTestResult in PlaneHitTestResults )
					{
						const float UnrealHitDistance = HitTestResult.distance * WorldToMetersScale;
						if ( IsHitInRange( UnrealHitDistance ) )
						{
							// Hit result has passed and above filtering, add it to the list
							// Convert to Unreal's Hit Test result format
							Results.Add(FARTraceResult(This, UnrealHitDistance, EARLineTraceChannels::GroundPlane, FAppleARKitConversion::ToFTransform(HitTestResult.worldTransform)*GetARCompositionComponent()->GetAlignmentTransform(), FindGeometryFromAnchor(HitTestResult.anchor, TrackedGeometries)));
						}
					}
				}
				
				// If there were no valid results, fall back further to hit testing against feature points
				if (!!(TraceChannels & EARLineTraceChannels::FeaturePoint))
				{
					// GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("No results for plane hit test - reverting to feature points"), NormalizedImagePosition.X, NormalizedImagePosition.Y));
					
					NSArray< ARHitTestResult* >* FeatureHitTestResults = [HitTestFrame hitTest:CGPointMake(NormalizedImagePosition.X, NormalizedImagePosition.Y) types:ARHitTestResultTypeFeaturePoint];
					for ( ARHitTestResult* HitTestResult in FeatureHitTestResults )
					{
						const float UnrealHitDistance = HitTestResult.distance * WorldToMetersScale;
						if ( IsHitInRange( UnrealHitDistance ) )
						{
							// Hit result has passed and above filtering, add it to the list
							// Convert to Unreal's Hit Test result format
							Results.Add( FARTraceResult( This, UnrealHitDistance, EARLineTraceChannels::FeaturePoint, FAppleARKitConversion::ToFTransform( HitTestResult.worldTransform )*GetARCompositionComponent()->GetAlignmentTransform(), FindGeometryFromAnchor(HitTestResult.anchor, TrackedGeometries) ) );
						}
					}
				}
			}
		}
#endif
	}
	
	if (Results.Num() > 1)
	{
		Results.Sort([](const FARTraceResult& A, const FARTraceResult& B)
		{
			return A.GetDistanceFromCamera() < B.GetDistanceFromCamera();
		});
	}
	
	return Results;
}

TArray<FARTraceResult> FAppleARKitSystem::OnLineTraceTrackedObjects(const FVector Start, const FVector End, EARLineTraceChannels TraceChannels)
{
	UE_LOG(LogAppleARKit, Warning, TEXT("FAppleARKitSystem::OnLineTraceTrackedObjects(Start, End, TraceChannels) is currently unsupported.  No results will be returned."))
	TArray<FARTraceResult> EmptyResults;
	return EmptyResults;
}

TArray<UARTrackedGeometry*> FAppleARKitSystem::OnGetAllTrackedGeometries() const
{
	TArray<UARTrackedGeometry*> Geometries;
	TrackedGeometries.GenerateValueArray(Geometries);
	return Geometries;
}

TArray<UARPin*> FAppleARKitSystem::OnGetAllPins() const
{
	return Pins;
}

UARTextureCameraImage* FAppleARKitSystem::OnGetCameraImage()
{
	return CameraImage;
}

UARTextureCameraDepth* FAppleARKitSystem::OnGetCameraDepth()
{
	return CameraDepth;
}

UARLightEstimate* FAppleARKitSystem::OnGetCurrentLightEstimate() const
{
	return LightEstimate;
}

UARPin* FAppleARKitSystem::OnPinComponent( USceneComponent* ComponentToPin, const FTransform& PinToWorldTransform, UARTrackedGeometry* TrackedGeometry, const FName DebugName )
{
	if ( ensureMsgf(ComponentToPin != nullptr, TEXT("Cannot pin component.")) )
	{
		if (UARPin* FindResult = ARKitUtil::PinFromComponent(ComponentToPin, Pins))
		{
			UE_LOG(LogAppleARKit, Warning, TEXT("Component %s is already pinned. Unpin it first."), *ComponentToPin->GetReadableName());
			OnRemovePin(FindResult);
		}

		// PinToWorld * AlignedTrackingToWorld(-1) * TrackingToAlignedTracking(-1) = PinToWorld * WorldToAlignedTracking * AlignedTrackingToTracking
		// The Worlds and AlignedTracking cancel out, and we get PinToTracking
		// But we must translate this logic into Unreal's transform API
		const FTransform& TrackingToAlignedTracking = GetARCompositionComponent()->GetAlignmentTransform();
		const FTransform PinToTrackingTransform = PinToWorldTransform.GetRelativeTransform(GetTrackingToWorldTransform()).GetRelativeTransform(TrackingToAlignedTracking);

		// If the user did not provide a TrackedGeometry, create the simplest TrackedGeometry for this pin.
		UARTrackedGeometry* GeometryToPinTo = TrackedGeometry;
		if (GeometryToPinTo == nullptr)
		{
			double UpdateTimestamp = FPlatformTime::Seconds();
			
			GeometryToPinTo = NewObject<UARTrackedPoint>();
			GeometryToPinTo->UpdateTrackedGeometry(GetARCompositionComponent().ToSharedRef(), 0, FPlatformTime::Seconds(), PinToTrackingTransform, GetARCompositionComponent()->GetAlignmentTransform());
		}
		
		UARPin* NewPin = NewObject<UARPin>();
		NewPin->InitARPin(GetARCompositionComponent().ToSharedRef(), ComponentToPin, PinToTrackingTransform, GeometryToPinTo, DebugName);
		
		Pins.Add(NewPin);
		
		return NewPin;
	}
	else
	{
		return nullptr;
	}
}

void FAppleARKitSystem::OnRemovePin(UARPin* PinToRemove)
{
	Pins.RemoveSingleSwap(PinToRemove);
}

bool FAppleARKitSystem::GetCurrentFrame(FAppleARKitFrame& OutCurrentFrame) const
{
	if( GameThreadFrame.IsValid() )
	{
		OutCurrentFrame = *GameThreadFrame;
		return true;
	}
	else
	{
		return false;
	}
}

bool FAppleARKitSystem::OnIsTrackingTypeSupported(EARSessionType SessionType) const
{
#if SUPPORTS_ARKIT_1_0
	switch (SessionType)
	{
		case EARSessionType::Orientation:
		{
			return AROrientationTrackingConfiguration.isSupported == TRUE;
		}
		case EARSessionType::World:
		{
			return ARWorldTrackingConfiguration.isSupported == TRUE;
		}
		case EARSessionType::Face:
		{
			// We need to get the face support from the factory method, which is a modular feature to avoid dependencies
			TArray<IAppleARKitFaceSupport*> Impls = IModularFeatures::Get().GetModularFeatureImplementations<IAppleARKitFaceSupport>("AppleARKitFaceSupport");
			if (Impls.Num() > 0 && Impls[0] != nullptr)
			{
				return Impls[0]->DoesSupportFaceAR();
			}
			return false;
		}
#if SUPPORTS_ARKIT_2_0
		case EARSessionType::Image:
		{
			return ARImageTrackingConfiguration.isSupported == TRUE;
		}
		case EARSessionType::ObjectScanning:
		{
			return ARObjectScanningConfiguration.isSupported == TRUE;
		}
#endif
#if SUPPORTS_ARKIT_3_0
		case EARSessionType::PoseTracking:
		{
			return ARBodyTrackingConfiguration.isSupported == TRUE;
		}
#endif
	}
#endif
	return false;
}

bool FAppleARKitSystem::OnAddManualEnvironmentCaptureProbe(FVector Location, FVector Extent)
{
#if SUPPORTS_ARKIT_2_0
	if (Session != nullptr)
	{
		if (FAppleARKitAvailability::SupportsARKit20())
		{
//@joeg -- Todo need to fix this transform as it needs to use the alignment transform too
			// Build and add the anchor
			simd_float4x4 AnchorMatrix = FAppleARKitConversion::ToARKitMatrix(FTransform(Location));
			simd_float3 AnchorExtent = FAppleARKitConversion::ToARKitVector(Extent * 2.f);
			AREnvironmentProbeAnchor* ARProbe = [[AREnvironmentProbeAnchor alloc] initWithTransform: AnchorMatrix extent: AnchorExtent];
			[Session addAnchor: ARProbe];
			[ARProbe release];
		}
		return true;
	}
#endif
	return false;
}

TArray<FARVideoFormat> FAppleARKitSystem::OnGetSupportedVideoFormats(EARSessionType SessionType) const
{
#if SUPPORTS_ARKIT_1_5
	if (FAppleARKitAvailability::SupportsARKit15())
	{
		switch (SessionType)
		{
			case EARSessionType::Face:
			{
				// We need to get the face support from the factory method, which is a modular feature to avoid dependencies
				TArray<IAppleARKitFaceSupport*> Impls = IModularFeatures::Get().GetModularFeatureImplementations<IAppleARKitFaceSupport>("AppleARKitFaceSupport");
				break;
			}
			case EARSessionType::World:
			{
				return FAppleARKitConversion::FromARVideoFormatArray(ARWorldTrackingConfiguration.supportedVideoFormats);
			}
		}
	}
#endif
	return TArray<FARVideoFormat>();
}

TArray<FVector> FAppleARKitSystem::OnGetPointCloud() const
{
	TArray<FVector> PointCloud;
	
#if SUPPORTS_ARKIT_1_0
	if (GameThreadFrame.IsValid())
	{
		ARFrame* InARFrame = (ARFrame*)GameThreadFrame->NativeFrame;
		ARPointCloud* InARPointCloud = InARFrame.rawFeaturePoints;
		if (InARPointCloud != nullptr)
		{
			const int32 Count = InARPointCloud.count;
			PointCloud.Empty(Count);
			PointCloud.AddUninitialized(Count);
			for (int32 Index = 0; Index < Count; Index++)
			{
				PointCloud[Index] = FAppleARKitConversion::ToFVector(InARPointCloud.points[Index]);
			}
		}
	}
#endif
	return PointCloud;
}

#if SUPPORTS_ARKIT_2_0
/** Since both the object extraction and world saving need to get the world map async, use a common chunk of code for this */
class FAppleARKitGetWorldMapObjectAsyncTask
{
public:
	/** Performs the call to get the world map and triggers the OnWorldMapAcquired() the completion handler */
	void Run()
	{
		[Session getCurrentWorldMapWithCompletionHandler: ^(ARWorldMap* worldMap, NSError* error)
		 {
			 WorldMap = worldMap;
			 [WorldMap retain];
			 bool bWasSuccessful = error == nullptr;
			 FString ErrorString;
			 if (error != nullptr)
			 {
				 ErrorString = [error localizedDescription];
			 }
			 OnWorldMapAcquired(bWasSuccessful, ErrorString);
		 }];
	}
	
protected:
	FAppleARKitGetWorldMapObjectAsyncTask(ARSession* InSession) :
		Session(InSession)
	{
		CFRetain(Session);
	}
	
	void Release()
	{
		if (Session != nullptr)
		{
			[Session release];
			Session = nullptr;
		}
		if (WorldMap != nullptr)
		{
			[WorldMap release];
			WorldMap = nullptr;
		}
	}

	/** Called once the world map completion handler is called */
	virtual void OnWorldMapAcquired(bool bWasSuccessful, FString ErrorString) = 0;

	/** The session object that we'll grab the world from */
	ARSession* Session;
	/** The world map object once the call has completed */
	ARWorldMap* WorldMap;
};

//@joeg -- The API changed last minute so you don't need to resolve the world to get an object anymore
// This needs to be cleaned up
class FAppleARKitGetCandidateObjectAsyncTask :
	public FARGetCandidateObjectAsyncTask
{
public:
	FAppleARKitGetCandidateObjectAsyncTask(ARSession* InSession, FVector InLocation, FVector InExtent) :
		Location(InLocation)
		, Extent(InExtent)
		, ReferenceObject(nullptr)
		, Session(InSession)
	{
		[Session retain];
	}
	
	/** @return the candidate object that you can use for detection later */
	virtual UARCandidateObject* GetCandidateObject() override
	{
		if (ReferenceObject != nullptr)
		{
			UARCandidateObject* CandidateObject = NewObject<UARCandidateObject>();
			
			FVector RefObjCenter = FAppleARKitConversion::ToFVector(ReferenceObject.center);
			FVector RefObjExtent = 0.5f * FAppleARKitConversion::ToFVector(ReferenceObject.extent);
			FBox BoundingBox(RefObjCenter, RefObjExtent);
			CandidateObject->SetBoundingBox(BoundingBox);
			
			// Serialize the object into a byte array and stick that on the candidate object
			NSError* ErrorObj = nullptr;
			NSData* RefObjData = [NSKeyedArchiver archivedDataWithRootObject: ReferenceObject requiringSecureCoding: YES error: &ErrorObj];
			uint32 SavedSize = RefObjData.length;
			TArray<uint8> RawBytes;
			RawBytes.AddUninitialized(SavedSize);
			FPlatformMemory::Memcpy(RawBytes.GetData(), [RefObjData bytes], SavedSize);
			CandidateObject->SetCandidateObjectData(RawBytes);

			return CandidateObject;
		}
		return nullptr;
	}
	
	virtual ~FAppleARKitGetCandidateObjectAsyncTask()
	{
		[Session release];
		if (ReferenceObject != nullptr)
		{
			CFRelease(ReferenceObject);
		}
	}

	void Run()
	{
		simd_float4x4 ARMatrix = FAppleARKitConversion::ToARKitMatrix(FTransform(Location));
		simd_float3 Center = 0.f;
		simd_float3 ARExtent = FAppleARKitConversion::ToARKitVector(Extent * 2.f);

		[Session createReferenceObjectWithTransform: ARMatrix center: Center extent: ARExtent
		 completionHandler: ^(ARReferenceObject* refObject, NSError* error)
		{
			ReferenceObject = refObject;
			CFRetain(ReferenceObject);
			bool bWasSuccessful = error == nullptr;
			bHadError = error != nullptr;
			FString ErrorString;
			if (error != nullptr)
			{
				ErrorString = [error localizedDescription];
			}
			bIsDone = true;
		}];
	}
	
private:
	FVector Location;
	FVector Extent;
	ARReferenceObject* ReferenceObject;

	/** The session object that we'll grab the object from */
	ARSession* Session;
};

class FAppleARKitSaveWorldAsyncTask :
	public FARSaveWorldAsyncTask,
	public FAppleARKitGetWorldMapObjectAsyncTask
{
public:
	FAppleARKitSaveWorldAsyncTask(ARSession* InSession) :
		FAppleARKitGetWorldMapObjectAsyncTask(InSession)
	{
	}

	virtual ~FAppleARKitSaveWorldAsyncTask()
	{
		Release();
	}

	virtual void OnWorldMapAcquired(bool bWasSuccessful, FString ErrorString) override
	{
		if (bWasSuccessful)
		{
			NSError* ErrorObj = nullptr;
			NSData* WorldNSData = [NSKeyedArchiver archivedDataWithRootObject: WorldMap requiringSecureCoding: YES error: &ErrorObj];
			if (ErrorObj == nullptr)
			{
				int32 UncompressedSize = WorldNSData.length;
				
				TArray<uint8> CompressedData;
				CompressedData.AddUninitialized(WorldNSData.length + AR_SAVE_WORLD_HEADER_SIZE);
				uint8* Buffer = (uint8*)CompressedData.GetData();
				// Write our magic header into our buffer
				FARWorldSaveHeader& Header = *(FARWorldSaveHeader*)Buffer;
				Header = FARWorldSaveHeader();
				Header.UncompressedSize = UncompressedSize;
				
				// Compress the data
				uint8* CompressInto = Buffer + AR_SAVE_WORLD_HEADER_SIZE;
				int32 CompressedSize = UncompressedSize;
				uint8* UncompressedData = (uint8*)[WorldNSData bytes];
				verify(FCompression::CompressMemory(NAME_Zlib, CompressInto, CompressedSize, UncompressedData, UncompressedSize));
				
				// Only copy out the amount of compressed data and the header
				int32 CompressedSizePlusHeader = CompressedSize + AR_SAVE_WORLD_HEADER_SIZE;
				WorldData.AddUninitialized(CompressedSizePlusHeader);
				FPlatformMemory::Memcpy(WorldData.GetData(), CompressedData.GetData(), CompressedSizePlusHeader);
			}
			else
			{
				Error = [ErrorObj localizedDescription];
				bHadError = true;
			}
		}
		else
		{
			Error = ErrorString;
			bHadError = true;
		}
		// Trigger that we're done
		bIsDone = true;
	}
};
#endif

TSharedPtr<FARGetCandidateObjectAsyncTask, ESPMode::ThreadSafe> FAppleARKitSystem::OnGetCandidateObject(FVector Location, FVector Extent) const
{
#if SUPPORTS_ARKIT_2_0
	if (Session != nullptr)
	{
		if (FAppleARKitAvailability::SupportsARKit20())
		{
			TSharedPtr<FAppleARKitGetCandidateObjectAsyncTask, ESPMode::ThreadSafe> Task = MakeShared<FAppleARKitGetCandidateObjectAsyncTask, ESPMode::ThreadSafe>(Session, Location, Extent);
			Task->Run();
			return Task;
		}
	}
#endif
	return  MakeShared<FARErrorGetCandidateObjectAsyncTask, ESPMode::ThreadSafe>(TEXT("GetCandidateObject - requires a valid, running ARKit 2.0 session"));
}

TSharedPtr<FARSaveWorldAsyncTask, ESPMode::ThreadSafe> FAppleARKitSystem::OnSaveWorld() const
{
#if SUPPORTS_ARKIT_2_0
	if (Session != nullptr)
	{
		if (FAppleARKitAvailability::SupportsARKit20())
		{
			TSharedPtr<FAppleARKitSaveWorldAsyncTask, ESPMode::ThreadSafe> Task = MakeShared<FAppleARKitSaveWorldAsyncTask, ESPMode::ThreadSafe>(Session);
			Task->Run();
			return Task;
		}
	}
#endif
	return  MakeShared<FARErrorSaveWorldAsyncTask, ESPMode::ThreadSafe>(TEXT("SaveWorld - requires a valid, running ARKit 2.0 session"));
}

EARWorldMappingState FAppleARKitSystem::OnGetWorldMappingStatus() const
{
	if (GameThreadFrame.IsValid())
	{
		return GameThreadFrame->WorldMappingState;
	}
	return EARWorldMappingState::NotAvailable;
}


void FAppleARKitSystem::AddReferencedObjects( FReferenceCollector& Collector )
{
	Collector.AddReferencedObjects( TrackedGeometries );
	Collector.AddReferencedObjects( Pins );
	Collector.AddReferencedObject( CameraImage );
	Collector.AddReferencedObject( CameraDepth );
	Collector.AddReferencedObjects( CandidateImages );
	Collector.AddReferencedObjects( CandidateObjects );
	Collector.AddReferencedObject( TimecodeProvider );
	Collector.AddReferencedObject( PersonSegmentationImage );
	Collector.AddReferencedObject( PersonSegmentationDepthImage );

	if(LightEstimate)
	{
		Collector.AddReferencedObject(LightEstimate);
	}
}

bool FAppleARKitSystem::HitTestAtScreenPosition(const FVector2D ScreenPosition, EAppleARKitHitTestResultType InTypes, TArray< FAppleARKitHitTestResult >& OutResults)
{
	ensureMsgf(false,TEXT("UNIMPLEMENTED; see OnLineTraceTrackedObjects()"));
	return false;
}

void FAppleARKitSystem::SetDeviceOrientation(EDeviceScreenOrientation InOrientation)
{
	ensureAlwaysMsgf(InOrientation != EDeviceScreenOrientation::Unknown, TEXT("statusBarOrientation should only ever return valid orientations"));
	if (InOrientation == EDeviceScreenOrientation::Unknown)
	{
		// This is the default for AR apps
		InOrientation = EDeviceScreenOrientation::LandscapeLeft;
	}

	if (DeviceOrientation != InOrientation)
	{
		DeviceOrientation = InOrientation;
		CalcTrackingToWorldRotation();
	}
}

void FAppleARKitSystem::ClearTrackedGeometries()
{
#if SUPPORTS_ARKIT_1_0
	TArray<FGuid> Keys;
	TrackedGeometries.GetKeys(Keys);
	for (const FGuid& Key : Keys)
	{
		SessionDidRemoveAnchors_Internal(Key);
	}
#endif
}

void FAppleARKitSystem::SetupCameraTextures()
{
#if SUPPORTS_ARKIT_1_0
	if (CameraImage == nullptr)
	{
		CameraImage = NewObject<UAppleARKitTextureCameraImage>();
		CameraImage->Init(FPlatformTime::Seconds(), nullptr);
		FAppleARKitXRCamera* Camera = GetARKitXRCamera();
		check(Camera);
		Camera->SetOverlayTexture(CameraImage);
	}
	if (CameraDepth == nullptr)
	{
		CameraDepth = NewObject<UAppleARKitTextureCameraDepth>();
	}
#endif
}

PRAGMA_DISABLE_OPTIMIZATION
bool FAppleARKitSystem::Run(UARSessionConfig* SessionConfig)
{
	TimecodeProvider = UAppleARKitSettings::GetTimecodeProvider();

	SetupCameraTextures();
	
	if (FAppleARKitXRCamera* Camera = GetARKitXRCamera())
    {
        Camera->SetEnablePersonOcclusion(SessionConfig->bUsePersonSegmentationForOcclusion);
    }

	{
		// Clear out any existing frames since they aren't valid anymore
		FScopeLock ScopeLock(&FrameLock);
		GameThreadFrame = TSharedPtr<FAppleARKitFrame, ESPMode::ThreadSafe>();
		LastReceivedFrame = TSharedPtr<FAppleARKitFrame, ESPMode::ThreadSafe>();
	}

	// Make sure this is set at session start, because there are timing issues with using only the delegate approach
	if (DeviceOrientation == EDeviceScreenOrientation::Unknown)
	{
		EDeviceScreenOrientation ScreenOrientation = FPlatformMisc::GetDeviceOrientation();
		SetDeviceOrientation( ScreenOrientation );
	}


#if SUPPORTS_ARKIT_1_0
	// Don't do the conversion work if they don't want this
	FAppleARKitAnchorData::bGenerateGeometry = SessionConfig->bGenerateMeshDataFromTrackedGeometry;

	if (FAppleARKitAvailability::SupportsARKit10())
	{
		ARSessionRunOptions options = 0;

		ARConfiguration* Configuration = nullptr;
		CheckForFaceARSupport(SessionConfig);
		CheckForPoseTrackingARLiveLink(SessionConfig);
		if (FaceARSupport == nullptr)
		{
			Configuration = FAppleARKitConversion::ToARConfiguration(SessionConfig, CandidateImages, ConvertedCandidateImages, CandidateObjects);
		}
		else
		{
			Configuration = FaceARSupport->ToARConfiguration(SessionConfig, TimecodeProvider);
		}

		// Not all session types are supported by all devices
		if (Configuration == nullptr)
		{
			UE_LOG(LogAppleARKit, Error, TEXT("The requested session type is not supported by this device"));
			return false;
		}
		
		// Configure additional tracking features
		FAppleARKitConversion::ConfigureSessionTrackingFeatures(SessionConfig, Configuration);

		// Create our ARSessionDelegate
		if (Delegate == nullptr)
		{
			Delegate = [[FAppleARKitSessionDelegate alloc] initWithAppleARKitSystem:this];
		}

		if (Session == nullptr)
		{
			// Start a new ARSession
			Session = [ARSession new];
			Session.delegate = Delegate;
			Session.delegateQueue = dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0);
		}
		else
		{
			// Check what the user has set for reseting options
			if (SessionConfig->ShouldResetCameraTracking())
			{
				options |= ARSessionRunOptionResetTracking;
			}
			if (SessionConfig->ShouldResetTrackedObjects())
			{
				options |= ARSessionRunOptionRemoveExistingAnchors;
				// The user requested us to remove existing anchors so remove ours now
				ClearTrackedGeometries();
			}
		}
		
#if PLATFORM_IOS && !PLATFORM_TVOS
		// Check if we need to adjust the priorities to allow ARKit to have more CPU time
		if (GetMutableDefault<UAppleARKitSettings>()->ShouldAdjustThreadPriorities())
		{
			int32 GameOverride = GetMutableDefault<UAppleARKitSettings>()->GetGameThreadPriorityOverride();
			int32 RenderOverride = GetMutableDefault<UAppleARKitSettings>()->GetRenderThreadPriorityOverride();
			SetThreadPriority(GameOverride);
			if (XRCamera.IsValid())
			{
				FAppleARKitXRCamera* Camera = (FAppleARKitXRCamera*)XRCamera.Get();
				Camera->AdjustThreadPriority(RenderOverride);
			}
			
			UE_LOG(LogAppleARKit, Log, TEXT("Overriding thread priorities: Game Thread (%d), Render Thread (%d)"), GameOverride, RenderOverride);
		}
#endif

		UE_LOG(LogAppleARKit, Log, TEXT("Starting session: %p with options %d"), this, options);

		// Start the session with the configuration
		[Session runWithConfiguration : Configuration options : options];
	}
	
#endif
	
	// @todo arkit Add support for relocating ARKit space to Unreal World Origin? BaseTransform = FTransform::Identity;
	
	// Set running state
	bIsRunning = true;
	
	GetARCompositionComponent()->OnARSessionStarted.Broadcast();
	return true;
}
PRAGMA_ENABLE_OPTIMIZATION

bool FAppleARKitSystem::IsRunning() const
{
	return bIsRunning;
}

bool FAppleARKitSystem::Pause()
{
	// Already stopped?
	if (!IsRunning())
	{
		return true;
	}
	
	UE_LOG(LogAppleARKit, Log, TEXT("Stopping session: %p"), this);

#if SUPPORTS_ARKIT_1_0
	if (FAppleARKitAvailability::SupportsARKit10())
	{
		// Suspend the session
		[Session pause];
	}
	
#if PLATFORM_IOS && !PLATFORM_TVOS
	// Check if we need to adjust the priorities to allow ARKit to have more CPU time
	if (GetMutableDefault<UAppleARKitSettings>()->ShouldAdjustThreadPriorities())
	{
		SetThreadPriority(GAME_THREAD_PRIORITY);
		if (XRCamera.IsValid())
		{
			FAppleARKitXRCamera* Camera = (FAppleARKitXRCamera*)XRCamera.Get();
			Camera->AdjustThreadPriority(RENDER_THREAD_PRIORITY);
		}
		
		UE_LOG(LogAppleARKit, Log, TEXT("Restoring thread priorities: Game Thread (%d), Render Thread (%d)"), GAME_THREAD_PRIORITY, RENDER_THREAD_PRIORITY);
}
#endif
	
#endif
	
	// Set running state
	bIsRunning = false;
	
	return true;
}

void FAppleARKitSystem::OrientationChanged(const int32 NewOrientationRaw)
{
	const EDeviceScreenOrientation NewOrientation = static_cast<EDeviceScreenOrientation>(NewOrientationRaw);
	SetDeviceOrientation(NewOrientation);
}
						
void FAppleARKitSystem::SessionDidUpdateFrame_DelegateThread(TSharedPtr< FAppleARKitFrame, ESPMode::ThreadSafe > Frame)
{
	{
		auto UpdateFrameTask = FSimpleDelegateGraphTask::FDelegate::CreateThreadSafeSP( this, &FAppleARKitSystem::SessionDidUpdateFrame_Internal, Frame.ToSharedRef() );
		FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(UpdateFrameTask, GET_STATID(STAT_FAppleARKitSystem_SessionUpdateFrame), nullptr, ENamedThreads::GameThread);
	}
	{
		UpdateARKitPerfStats();
#if SUPPORTS_ARKIT_1_0
		if (GetMutableDefault<UAppleARKitSettings>()->ShouldWriteCameraImagePerFrame())
		{
			WriteCameraImageToDisk(Frame->CameraImage);
		}
		
		if (CameraImage != nullptr)
		{
			CameraImage->EnqueueNewCameraImage(Frame->CameraImage);
		}
#endif
	}
}
			
void FAppleARKitSystem::SessionDidFailWithError_DelegateThread(const FString& Error)
{
	UE_LOG(LogAppleARKit, Warning, TEXT("Session failed with error: %s"), *Error);
}

#if SUPPORTS_ARKIT_1_0

TArray<int32> FAppleARKitAnchorData::FaceIndices;
bool FAppleARKitAnchorData::bGenerateGeometry = false;
TSharedPtr<FARPose3D> FAppleARKitAnchorData::BodyRefPose = TSharedPtr<FARPose3D>();

static TSharedPtr<FAppleARKitAnchorData> MakeAnchorData( ARAnchor* Anchor, double Timestamp, uint32 FrameNumber )
{
	TSharedPtr<FAppleARKitAnchorData> NewAnchor;
	if ([Anchor isKindOfClass:[ARPlaneAnchor class]])
	{
		ARPlaneAnchor* PlaneAnchor = (ARPlaneAnchor*)Anchor;
		NewAnchor = MakeShared<FAppleARKitAnchorData>(
			FAppleARKitConversion::ToFGuid(PlaneAnchor.identifier),
			FAppleARKitConversion::ToFTransform(PlaneAnchor.transform),
			FAppleARKitConversion::ToFVector(PlaneAnchor.center),
			// @todo use World Settings WorldToMetersScale
			0.5f*FAppleARKitConversion::ToFVector(PlaneAnchor.extent).GetAbs(),
			FAppleARKitConversion::ToEARPlaneOrientation(PlaneAnchor.alignment)
		);

#if SUPPORTS_ARKIT_1_5
		if (FAppleARKitAvailability::SupportsARKit15())
		{
			if (FAppleARKitAnchorData::bGenerateGeometry)
			{
				const int32 NumBoundaryVerts = PlaneAnchor.geometry.boundaryVertexCount;
				NewAnchor->BoundaryVerts.Reset(NumBoundaryVerts);
				for(int32 i=0; i<NumBoundaryVerts; ++i)
				{
					const vector_float3& Vert = PlaneAnchor.geometry.boundaryVertices[i];
					NewAnchor->BoundaryVerts.Add(FAppleARKitConversion::ToFVector(Vert));
				}
				// Generate the mesh from the plane
				NewAnchor->Vertices.Reset(4);
				NewAnchor->Vertices.Add(NewAnchor->Center + NewAnchor->Extent);
				NewAnchor->Vertices.Add(NewAnchor->Center + FVector(NewAnchor->Extent.X, -NewAnchor->Extent.Y, NewAnchor->Extent.Z));
				NewAnchor->Vertices.Add(NewAnchor->Center + FVector(-NewAnchor->Extent.X, -NewAnchor->Extent.Y, NewAnchor->Extent.Z));
				NewAnchor->Vertices.Add(NewAnchor->Center + FVector(-NewAnchor->Extent.X, NewAnchor->Extent.Y, NewAnchor->Extent.Z));

				// Two triangles
				NewAnchor->Indices.Reset(6);
				NewAnchor->Indices.Add(0);
				NewAnchor->Indices.Add(1);
				NewAnchor->Indices.Add(2);
				NewAnchor->Indices.Add(2);
				NewAnchor->Indices.Add(3);
				NewAnchor->Indices.Add(0);
			}
		}
#endif
#if SUPPORTS_ARKIT_2_0
		if (FAppleARKitAvailability::SupportsARKit20())
		{
			NewAnchor->ObjectClassification = FAppleARKitConversion::ToEARObjectClassification(PlaneAnchor.classification);
		}
#else
		NewAnchor->ObjectClassification = EARObjectClassification::Unknown;
#endif
	}
#if SUPPORTS_ARKIT_1_5
	else if (FAppleARKitAvailability::SupportsARKit15() && [Anchor isKindOfClass:[ARImageAnchor class]])
	{
		ARImageAnchor* ImageAnchor = (ARImageAnchor*)Anchor;
		NewAnchor = MakeShared<FAppleARKitAnchorData>(
			FAppleARKitConversion::ToFGuid(ImageAnchor.identifier),
			FAppleARKitConversion::ToFTransform(ImageAnchor.transform),
			EAppleAnchorType::ImageAnchor,
			FString(ImageAnchor.referenceImage.name)
		);
#if SUPPORTS_ARKIT_2_0
		if (FAppleARKitAvailability::SupportsARKit20())
		{
			NewAnchor->bIsTracked = ImageAnchor.isTracked;
		}
		if (FAppleARKitAnchorData::bGenerateGeometry)
		{
			FVector Extent(ImageAnchor.referenceImage.physicalSize.width, ImageAnchor.referenceImage.physicalSize.height, 0.f);
			// Scale by half since this is an extent around the center (same as scale then divide by 2)
			Extent *= 50.f;
			// Generate the mesh from the reference image's sizes
			NewAnchor->Vertices.Reset(4);
			NewAnchor->Vertices.Add(Extent);
			NewAnchor->Vertices.Add(FVector(Extent.X, -Extent.Y, Extent.Z));
			NewAnchor->Vertices.Add(FVector(-Extent.X, -Extent.Y, Extent.Z));
			NewAnchor->Vertices.Add(FVector(-Extent.X, Extent.Y, Extent.Z));
			
			// Two triangles
			NewAnchor->Indices.Reset(6);
			NewAnchor->Indices.Add(0);
			NewAnchor->Indices.Add(1);
			NewAnchor->Indices.Add(2);
			NewAnchor->Indices.Add(2);
			NewAnchor->Indices.Add(3);
			NewAnchor->Indices.Add(0);
		}
#endif
	}
#endif
#if SUPPORTS_ARKIT_2_0
	else if (FAppleARKitAvailability::SupportsARKit20() && [Anchor isKindOfClass:[AREnvironmentProbeAnchor class]])
	{
		AREnvironmentProbeAnchor* ProbeAnchor = (AREnvironmentProbeAnchor*)Anchor;
		NewAnchor = MakeShared<FAppleARKitAnchorData>(
			FAppleARKitConversion::ToFGuid(ProbeAnchor.identifier),
			FAppleARKitConversion::ToFTransform(ProbeAnchor.transform),
			0.5f * FAppleARKitConversion::ToFVector(ProbeAnchor.extent).GetAbs(),
			ProbeAnchor.environmentTexture
		);
	}
	else if (FAppleARKitAvailability::SupportsARKit20() && [Anchor isKindOfClass:[ARObjectAnchor class]])
	{
		ARObjectAnchor* ObjectAnchor = (ARObjectAnchor*)Anchor;
		NewAnchor = MakeShared<FAppleARKitAnchorData>(
			  FAppleARKitConversion::ToFGuid(ObjectAnchor.identifier),
			  FAppleARKitConversion::ToFTransform(ObjectAnchor.transform),
			  EAppleAnchorType::ObjectAnchor,
			  FString(ObjectAnchor.referenceObject.name)
		  );
	}
#endif
#if SUPPORTS_ARKIT_3_0
	else if (FAppleARKitAvailability::SupportsARKit30() && [Anchor isKindOfClass:[ARBodyAnchor class]])
	{
		ARBodyAnchor* BodyAnchor = (ARBodyAnchor*)Anchor;

		if (!FAppleARKitAnchorData::BodyRefPose)
		{
			FAppleARKitAnchorData::BodyRefPose = MakeShared<FARPose3D>(FAppleARKitConversion::ToARPose3D(BodyAnchor.skeleton.definition.neutralBodySkeleton3D, false));
		}

		NewAnchor = MakeShared<FAppleARKitAnchorData>(
			  FAppleARKitConversion::ToFGuid(BodyAnchor.identifier),
			  FAppleARKitConversion::ToFTransform(BodyAnchor.transform),
			  FAppleARKitConversion::ToARPose3D(BodyAnchor)
		  );
	}
#endif
	else
	{
		NewAnchor = MakeShared<FAppleARKitAnchorData>(
			FAppleARKitConversion::ToFGuid(Anchor.identifier),
			FAppleARKitConversion::ToFTransform(Anchor.transform));
	}

	NewAnchor->Timestamp = Timestamp;
	NewAnchor->FrameNumber = FrameNumber;
	
	return NewAnchor;
}

void FAppleARKitSystem::SessionDidAddAnchors_DelegateThread( NSArray<ARAnchor*>* anchors )
{
	// If this object is valid, we are running a face session and need that code to process things
	if (FaceARSupport != nullptr)
	{
		const FRotator& AdjustBy = GetARCompositionComponent()->GetSessionConfig().GetWorldAlignment() == EARWorldAlignment::Camera ? DerivedTrackingToUnrealRotation : FRotator::ZeroRotator;
		const EARFaceTrackingUpdate UpdateSetting = GetARCompositionComponent()->GetSessionConfig().GetFaceTrackingUpdate();

		const TArray<TSharedPtr<FAppleARKitAnchorData>> AnchorList = FaceARSupport->MakeAnchorData(anchors, AdjustBy, UpdateSetting);
		for (TSharedPtr<FAppleARKitAnchorData> NewAnchorData : AnchorList)
		{
			auto AddAnchorTask = FSimpleDelegateGraphTask::FDelegate::CreateSP(this, &FAppleARKitSystem::SessionDidAddAnchors_Internal, NewAnchorData.ToSharedRef());
			FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(AddAnchorTask, GET_STATID(STAT_FAppleARKitSystem_SessionDidAddAnchors), nullptr, ENamedThreads::GameThread);
		}
		return;
	}

	// Make sure all anchors get the same timestamp and frame number
	double Timestamp = FPlatformTime::Seconds();
	uint32 FrameNumber = TimecodeProvider->GetTimecode().Frames;

	for (ARAnchor* anchor in anchors)
	{
		TSharedPtr<FAppleARKitAnchorData> NewAnchorData = MakeAnchorData(anchor, Timestamp, FrameNumber);
		if (ensure(NewAnchorData.IsValid()))
		{
			auto AddAnchorTask = FSimpleDelegateGraphTask::FDelegate::CreateSP(this, &FAppleARKitSystem::SessionDidAddAnchors_Internal, NewAnchorData.ToSharedRef());
			FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(AddAnchorTask, GET_STATID(STAT_FAppleARKitSystem_SessionDidAddAnchors), nullptr, ENamedThreads::GameThread);
		}
	}
}

void FAppleARKitSystem::SessionDidUpdateAnchors_DelegateThread( NSArray<ARAnchor*>* anchors )
{
	// If this object is valid, we are running a face session and need that code to process things
	if (FaceARSupport != nullptr)
	{
		double UpdateTimestamp = FPlatformTime::Seconds();
		const FRotator& AdjustBy = GetARCompositionComponent()->GetSessionConfig().GetWorldAlignment() == EARWorldAlignment::Camera ? DerivedTrackingToUnrealRotation : FRotator::ZeroRotator;
		const EARFaceTrackingUpdate UpdateSetting = GetARCompositionComponent()->GetSessionConfig().GetFaceTrackingUpdate();

		const TArray<TSharedPtr<FAppleARKitAnchorData>> AnchorList = FaceARSupport->MakeAnchorData(anchors, AdjustBy, UpdateSetting);
		for (TSharedPtr<FAppleARKitAnchorData> NewAnchorData : AnchorList)
		{
			auto UpdateAnchorTask = FSimpleDelegateGraphTask::FDelegate::CreateSP(this, &FAppleARKitSystem::SessionDidUpdateAnchors_Internal, NewAnchorData.ToSharedRef());
			FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(UpdateAnchorTask, GET_STATID(STAT_FAppleARKitSystem_SessionDidUpdateAnchors), nullptr, ENamedThreads::GameThread);
		}
		return;
	}

	// Make sure all anchors get the same timestamp and frame number
	double Timestamp = FPlatformTime::Seconds();
	uint32 FrameNumber = TimecodeProvider->GetTimecode().Frames;

	for (ARAnchor* anchor in anchors)
	{
		TSharedPtr<FAppleARKitAnchorData> NewAnchorData = MakeAnchorData(anchor, Timestamp, FrameNumber);
		if (ensure(NewAnchorData.IsValid()))
		{
			auto UpdateAnchorTask = FSimpleDelegateGraphTask::FDelegate::CreateSP(this, &FAppleARKitSystem::SessionDidUpdateAnchors_Internal, NewAnchorData.ToSharedRef());
			FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(UpdateAnchorTask, GET_STATID(STAT_FAppleARKitSystem_SessionDidUpdateAnchors), nullptr, ENamedThreads::GameThread);
		}
	}
}

void FAppleARKitSystem::SessionDidRemoveAnchors_DelegateThread( NSArray<ARAnchor*>* anchors )
{
	// Face AR Anchors are also removed this way, no need for special code since they are tracked geometry
	for (ARAnchor* anchor in anchors)
	{
		// Convert to FGuid
		const FGuid AnchorGuid = FAppleARKitConversion::ToFGuid( anchor.identifier );

		auto RemoveAnchorTask = FSimpleDelegateGraphTask::FDelegate::CreateSP(this, &FAppleARKitSystem::SessionDidRemoveAnchors_Internal, AnchorGuid);
		FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(RemoveAnchorTask, GET_STATID(STAT_FAppleARKitSystem_SessionDidRemoveAnchors), nullptr, ENamedThreads::GameThread);
	}
}


void FAppleARKitSystem::SessionDidAddAnchors_Internal( TSharedRef<FAppleARKitAnchorData> AnchorData )
{
	double UpdateTimestamp = FPlatformTime::Seconds();
	
	const TSharedPtr<FARSupportInterface , ESPMode::ThreadSafe>& ARComponent = GetARCompositionComponent();

	// In case we have camera tracking turned off, we still need to update the frame
	if (!ARComponent->GetSessionConfig().ShouldEnableCameraTracking())
	{
		UpdateFrame();
	}
	
	// If this object is valid, we are running a face session and we need to publish LiveLink data on the game thread
	if (FaceARSupport != nullptr && AnchorData->AnchorType == EAppleAnchorType::FaceAnchor)
	{
		FaceARSupport->PublishLiveLinkData(AnchorData);
	}

	if (PoseTrackingARLiveLink != nullptr && AnchorData->AnchorType == EAppleAnchorType::PoseAnchor)
	{
		PoseTrackingARLiveLink->PublishLiveLinkData(AnchorData);
	}

	FString NewAnchorDebugName;
	UARTrackedGeometry* NewGeometry = nullptr;
	switch (AnchorData->AnchorType)
	{
		case EAppleAnchorType::Anchor:
		{
			NewAnchorDebugName = FString::Printf(TEXT("ANCHOR-%02d"), LastTrackedGeometry_DebugId++);
			NewGeometry = NewObject<UARTrackedGeometry>();
			NewGeometry->UpdateTrackedGeometry(ARComponent.ToSharedRef(), AnchorData->FrameNumber, AnchorData->Timestamp, AnchorData->Transform, GetARCompositionComponent()->GetAlignmentTransform());
			break;
		}
		case EAppleAnchorType::PlaneAnchor:
		{
			NewAnchorDebugName = FString::Printf(TEXT("PLN-%02d"), LastTrackedGeometry_DebugId++);
			UARPlaneGeometry* NewGeo = NewObject<UARPlaneGeometry>();
			NewGeo->UpdateTrackedGeometry(ARComponent.ToSharedRef(), AnchorData->FrameNumber, AnchorData->Timestamp, AnchorData->Transform, GetARCompositionComponent()->GetAlignmentTransform(), AnchorData->Center, AnchorData->Extent);
			NewGeo->SetOrientation(AnchorData->Orientation);
			const UARSessionConfig& SessionConfig = GetARCompositionComponent()->GetSessionConfig();
			// Add the occlusion geo if configured
			if (SessionConfig.bGenerateMeshDataFromTrackedGeometry)
			{
				AAROriginActor* OriginActor = AAROriginActor::GetOriginActor();
				UMRMeshComponent* MRMesh = NewObject<UMRMeshComponent>(OriginActor);

				// Set the occlusion and wireframe defaults
				MRMesh->SetEnableMeshOcclusion(SessionConfig.bUseMeshDataForOcclusion);
				MRMesh->SetUseWireframe(SessionConfig.bRenderMeshDataInWireframe);
				MRMesh->SetNeverCreateCollisionMesh(!SessionConfig.bGenerateCollisionForMeshData);
				MRMesh->SetEnableNavMesh(SessionConfig.bGenerateNavMeshForMeshData);

				// Set parent and register
				MRMesh->SetupAttachment(OriginActor->GetRootComponent());
				MRMesh->RegisterComponent();

				// MRMesh takes ownership of the data in the arrays at this point
				MRMesh->UpdateMesh(AnchorData->Transform.GetLocation(), AnchorData->Transform.GetRotation(), AnchorData->Transform.GetScale3D(), AnchorData->Vertices, AnchorData->Indices);

				// Connect the tracked geo to the MRMesh
				NewGeo->SetUnderlyingMesh(MRMesh);
			}
			NewGeo->SetObjectClassification(AnchorData->ObjectClassification);
			NewGeometry = NewGeo;
			break;
		}
		case EAppleAnchorType::FaceAnchor:
		{
			static TArray<FVector2D> NotUsed;
			NewAnchorDebugName = FString::Printf(TEXT("FACE-%02d"), LastTrackedGeometry_DebugId++);
			UARFaceGeometry* NewGeo = NewObject<UARFaceGeometry>();
			NewGeo->UpdateFaceGeometry(ARComponent.ToSharedRef(), AnchorData->FrameNumber, AnchorData->Timestamp, AnchorData->Transform, GetARCompositionComponent()->GetAlignmentTransform(), AnchorData->BlendShapes, AnchorData->FaceVerts, AnchorData->FaceIndices, NotUsed, AnchorData->LeftEyeTransform, AnchorData->RightEyeTransform, AnchorData->LookAtTarget);
			NewGeo->SetTrackingState(EARTrackingState::Tracking);
			NewGeometry = NewGeo;
			break;
		}
		case EAppleAnchorType::ImageAnchor:
		{
			NewAnchorDebugName = FString::Printf(TEXT("IMG-%02d"), LastTrackedGeometry_DebugId++);
			UARTrackedImage* NewImage = NewObject<UARTrackedImage>();
			UARCandidateImage** CandidateImage = CandidateImages.Find(AnchorData->DetectedAnchorName);
			ensure(CandidateImage != nullptr);
			FVector2D PhysicalSize((*CandidateImage)->GetPhysicalWidth(), (*CandidateImage)->GetPhysicalHeight());
			NewImage->UpdateTrackedGeometry(ARComponent.ToSharedRef(), AnchorData->FrameNumber, AnchorData->Timestamp, AnchorData->Transform, GetARCompositionComponent()->GetAlignmentTransform(), PhysicalSize, *CandidateImage);
			NewGeometry = NewImage;
			const UARSessionConfig& SessionConfig = GetARCompositionComponent()->GetSessionConfig();
			// Add the occlusion geo if configured
			if (SessionConfig.bGenerateMeshDataFromTrackedGeometry)
			{
				AAROriginActor* OriginActor = AAROriginActor::GetOriginActor();
				UMRMeshComponent* MRMesh = NewObject<UMRMeshComponent>(OriginActor);
				
				// Set the occlusion and wireframe defaults
				MRMesh->SetEnableMeshOcclusion(SessionConfig.bUseMeshDataForOcclusion);
				MRMesh->SetUseWireframe(SessionConfig.bRenderMeshDataInWireframe);
				MRMesh->SetNeverCreateCollisionMesh(!SessionConfig.bGenerateCollisionForMeshData);
				MRMesh->SetEnableNavMesh(SessionConfig.bGenerateNavMeshForMeshData);
				
				// Set parent and register
				MRMesh->SetupAttachment(OriginActor->GetRootComponent());
				MRMesh->RegisterComponent();
				
				// MRMesh takes ownership of the data in the arrays at this point
				MRMesh->UpdateMesh(AnchorData->Transform.GetLocation(), AnchorData->Transform.GetRotation(), AnchorData->Transform.GetScale3D(), AnchorData->Vertices, AnchorData->Indices);
				
				// Connect the tracked geo to the MRMesh
				NewImage->SetUnderlyingMesh(MRMesh);
			}
			break;
		}
		case EAppleAnchorType::EnvironmentProbeAnchor:
		{
			NewAnchorDebugName = FString::Printf(TEXT("ENV-%02d"), LastTrackedGeometry_DebugId++);
			UAppleARKitEnvironmentCaptureProbe* NewProbe = NewObject<UAppleARKitEnvironmentCaptureProbe>();
			NewProbe->UpdateEnvironmentCapture(ARComponent.ToSharedRef(), AnchorData->FrameNumber, AnchorData->Timestamp, AnchorData->Transform, GetARCompositionComponent()->GetAlignmentTransform(), AnchorData->Extent, AnchorData->ProbeTexture);
			NewGeometry = NewProbe;
			break;
		}
		case EAppleAnchorType::ObjectAnchor:
		{
			NewAnchorDebugName = FString::Printf(TEXT("OBJ-%02d"), LastTrackedGeometry_DebugId++);
			UARTrackedObject* NewTrackedObject = NewObject<UARTrackedObject>();
			UARCandidateObject** CandidateObject = CandidateObjects.Find(AnchorData->DetectedAnchorName);
			ensure(CandidateObject != nullptr);
			NewTrackedObject->UpdateTrackedGeometry(ARComponent.ToSharedRef(), AnchorData->FrameNumber, AnchorData->Timestamp, AnchorData->Transform, GetARCompositionComponent()->GetAlignmentTransform(), *CandidateObject);
			NewGeometry = NewTrackedObject;
			break;
		}
		case EAppleAnchorType::PoseAnchor:
		{
			NewAnchorDebugName = FString::Printf(TEXT("POSE-%02d"), LastTrackedGeometry_DebugId++);
			UARTrackedPose* NewTrackedPose = NewObject<UARTrackedPose>();
			NewTrackedPose->UpdateTrackedPose(ARComponent.ToSharedRef(), AnchorData->FrameNumber, AnchorData->Timestamp, AnchorData->Transform, GetARCompositionComponent()->GetAlignmentTransform(), AnchorData->TrackedPose);
			NewGeometry = NewTrackedPose;
			break;
		}
	}
	check(NewGeometry != nullptr);

	UARTrackedGeometry* NewTrackedGeometry = TrackedGeometries.Add( AnchorData->AnchorGUID, NewGeometry );

	NewTrackedGeometry->UniqueId = AnchorData->AnchorGUID;
	NewTrackedGeometry->SetDebugName( FName(*NewAnchorDebugName) );

	// Trigger the delegate so anyone listening can take action
	TriggerOnTrackableAddedDelegates(NewTrackedGeometry);
}

void FAppleARKitSystem::SessionDidUpdateAnchors_Internal( TSharedRef<FAppleARKitAnchorData> AnchorData )
{
	double UpdateTimestamp = FPlatformTime::Seconds();
	
	const TSharedPtr<FARSupportInterface , ESPMode::ThreadSafe>& ARComponent = GetARCompositionComponent();

	// In case we have camera tracking turned off, we still need to update the frame
	if (!ARComponent->GetSessionConfig().ShouldEnableCameraTracking())
	{
		UpdateFrame();
	}

	// If this object is valid, we are running a face session and we need to publish LiveLink data on the game thread
	if (FaceARSupport != nullptr && AnchorData->AnchorType == EAppleAnchorType::FaceAnchor)
	{
		FaceARSupport->PublishLiveLinkData(AnchorData);
	}

	if (PoseTrackingARLiveLink != nullptr && AnchorData->AnchorType == EAppleAnchorType::PoseAnchor)
	{
		PoseTrackingARLiveLink->PublishLiveLinkData(AnchorData);
	}

	UARTrackedGeometry** GeometrySearchResult = TrackedGeometries.Find(AnchorData->AnchorGUID);
	if (GeometrySearchResult != nullptr)
	{
		UARTrackedGeometry* FoundGeometry = *GeometrySearchResult;
		TArray<UARPin*> PinsToUpdate = ARKitUtil::PinsFromGeometry(FoundGeometry, Pins);
		
		
		// We figure out the delta transform for the Anchor (aka. TrackedGeometry in ARKit) and apply that
		// delta to figure out the new ARPin transform.
		const FTransform Anchor_LocalToTrackingTransform_PreUpdate = FoundGeometry->GetLocalToTrackingTransform_NoAlignment();
		const FTransform& Anchor_LocalToTrackingTransform_PostUpdate = AnchorData->Transform;
		
		const FTransform AnchorDeltaTransform = Anchor_LocalToTrackingTransform_PreUpdate.GetRelativeTransform(Anchor_LocalToTrackingTransform_PostUpdate);
		
		switch (AnchorData->AnchorType)
		{
			case EAppleAnchorType::Anchor:
			{
				FoundGeometry->UpdateTrackedGeometry(ARComponent.ToSharedRef(), AnchorData->FrameNumber, AnchorData->Timestamp, AnchorData->Transform, GetARCompositionComponent()->GetAlignmentTransform());
				for (UARPin* Pin : PinsToUpdate)
				{
					const FTransform Pin_LocalToTrackingTransform_PostUpdate = Pin->GetLocalToTrackingTransform_NoAlignment() * AnchorDeltaTransform;
					Pin->OnTransformUpdated(Pin_LocalToTrackingTransform_PostUpdate);
				}
				
				break;
			}
			case EAppleAnchorType::PlaneAnchor:
			{
				if (UARPlaneGeometry* PlaneGeo = Cast<UARPlaneGeometry>(FoundGeometry))
				{
					PlaneGeo->UpdateTrackedGeometry(ARComponent.ToSharedRef(), AnchorData->FrameNumber, AnchorData->Timestamp, AnchorData->Transform, GetARCompositionComponent()->GetAlignmentTransform(), AnchorData->Center, AnchorData->Extent, AnchorData->BoundaryVerts, nullptr);
					for (UARPin* Pin : PinsToUpdate)
					{
						const FTransform Pin_LocalToTrackingTransform_PostUpdate = Pin->GetLocalToTrackingTransform_NoAlignment() * AnchorDeltaTransform;
						Pin->OnTransformUpdated(Pin_LocalToTrackingTransform_PostUpdate);
					}
					PlaneGeo->SetOrientation(AnchorData->Orientation);
					PlaneGeo->SetObjectClassification(AnchorData->ObjectClassification);
					// Update the occlusion geo if configured
					if (GetARCompositionComponent()->GetSessionConfig().bGenerateMeshDataFromTrackedGeometry)
					{
						UMRMeshComponent* MRMesh = PlaneGeo->GetUnderlyingMesh();
						check(MRMesh != nullptr);
						// MRMesh takes ownership of the data in the arrays at this point
						MRMesh->UpdateMesh(AnchorData->Transform.GetLocation(), AnchorData->Transform.GetRotation(), AnchorData->Transform.GetScale3D(), AnchorData->Vertices, AnchorData->Indices);
					}
				}
				break;
			}
			case EAppleAnchorType::FaceAnchor:
			{
				if (UARFaceGeometry* FaceGeo = Cast<UARFaceGeometry>(FoundGeometry))
				{
					static TArray<FVector2D> NotUsed;
					FaceGeo->UpdateFaceGeometry(ARComponent.ToSharedRef(), AnchorData->FrameNumber, AnchorData->Timestamp, AnchorData->Transform, GetARCompositionComponent()->GetAlignmentTransform(), AnchorData->BlendShapes, AnchorData->FaceVerts, AnchorData->FaceIndices, NotUsed, AnchorData->LeftEyeTransform, AnchorData->RightEyeTransform, AnchorData->LookAtTarget);
					FaceGeo->SetTrackingState(AnchorData->bIsTracked ? EARTrackingState::Tracking : EARTrackingState::NotTracking);
					for (UARPin* Pin : PinsToUpdate)
					{
						const FTransform Pin_LocalToTrackingTransform_PostUpdate = Pin->GetLocalToTrackingTransform_NoAlignment() * AnchorDeltaTransform;
						Pin->OnTransformUpdated(Pin_LocalToTrackingTransform_PostUpdate);
					}
				}
				break;
			}
            case EAppleAnchorType::ImageAnchor:
            {
				if (UARTrackedImage* ImageAnchor = Cast<UARTrackedImage>(FoundGeometry))
				{
					UARCandidateImage** CandidateImage = CandidateImages.Find(AnchorData->DetectedAnchorName);
					ensure(CandidateImage != nullptr);
					FVector2D PhysicalSize((*CandidateImage)->GetPhysicalWidth(), (*CandidateImage)->GetPhysicalHeight());
					ImageAnchor->UpdateTrackedGeometry(ARComponent.ToSharedRef(), AnchorData->FrameNumber, AnchorData->Timestamp, AnchorData->Transform, GetARCompositionComponent()->GetAlignmentTransform(), PhysicalSize, *CandidateImage);
					ImageAnchor->SetTrackingState(AnchorData->bIsTracked ? EARTrackingState::Tracking : EARTrackingState::NotTracking);
					for (UARPin* Pin : PinsToUpdate)
					{
						const FTransform Pin_LocalToTrackingTransform_PostUpdate = Pin->GetLocalToTrackingTransform_NoAlignment() * AnchorDeltaTransform;
						Pin->OnTransformUpdated(Pin_LocalToTrackingTransform_PostUpdate);
					}
					// Update the occlusion geo if configured
					if (GetARCompositionComponent()->GetSessionConfig().bGenerateMeshDataFromTrackedGeometry)
					{
						UMRMeshComponent* MRMesh = ImageAnchor->GetUnderlyingMesh();
						check(MRMesh != nullptr);
						// MRMesh takes ownership of the data in the arrays at this point
						MRMesh->UpdateMesh(AnchorData->Transform.GetLocation(), AnchorData->Transform.GetRotation(), AnchorData->Transform.GetScale3D(), AnchorData->Vertices, AnchorData->Indices);
					}
				}
                break;
            }
			case EAppleAnchorType::EnvironmentProbeAnchor:
			{
				if (UAppleARKitEnvironmentCaptureProbe* ProbeAnchor = Cast<UAppleARKitEnvironmentCaptureProbe>(FoundGeometry))
				{
					// NOTE: The metal texture will be a different texture every time the cubemap is updated which requires a render resource flush
					ProbeAnchor->UpdateEnvironmentCapture(ARComponent.ToSharedRef(), AnchorData->FrameNumber, AnchorData->Timestamp, AnchorData->Transform, GetARCompositionComponent()->GetAlignmentTransform(), AnchorData->Extent, AnchorData->ProbeTexture);
					for (UARPin* Pin : PinsToUpdate)
					{
						const FTransform Pin_LocalToTrackingTransform_PostUpdate = Pin->GetLocalToTrackingTransform_NoAlignment() * AnchorDeltaTransform;
						Pin->OnTransformUpdated(Pin_LocalToTrackingTransform_PostUpdate);
					}
				}
				break;
			}
			case EAppleAnchorType::PoseAnchor:
			{
				if (UARTrackedPose* TrackedPose = Cast<UARTrackedPose>(FoundGeometry))
				{
					TrackedPose->UpdateTrackedPose(ARComponent.ToSharedRef(), AnchorData->FrameNumber, AnchorData->Timestamp, AnchorData->Transform, GetARCompositionComponent()->GetAlignmentTransform(), AnchorData->TrackedPose);
					
					// TODO: why is this duplicated for every anchor type??
					for (UARPin* Pin : PinsToUpdate)
					{
						const FTransform Pin_LocalToTrackingTransform_PostUpdate = Pin->GetLocalToTrackingTransform_NoAlignment() * AnchorDeltaTransform;
						Pin->OnTransformUpdated(Pin_LocalToTrackingTransform_PostUpdate);
					}
				}
				break;
			}
		}
		// Trigger the delegate so anyone listening can take action
		TriggerOnTrackableUpdatedDelegates(FoundGeometry);
	}
}

void FAppleARKitSystem::SessionDidRemoveAnchors_Internal( FGuid AnchorGuid )
{
	const TSharedPtr<FARSupportInterface , ESPMode::ThreadSafe>& ARComponent = GetARCompositionComponent();

	// In case we have camera tracking turned off, we still need to update the frame
	if (!ARComponent->GetSessionConfig().ShouldEnableCameraTracking())
	{
		UpdateFrame();
	}

	// Notify pin that it is being orphaned
	{
		UARTrackedGeometry** FoundGeo = TrackedGeometries.Find(AnchorGuid);
		// This no longer performs a FindChecked() because the act of discard on restart can cause this to be missing
		if (FoundGeo != nullptr)
		{
			UARTrackedGeometry* TrackedGeometryBeingRemoved = *FoundGeo;
			TrackedGeometryBeingRemoved->UpdateTrackingState(EARTrackingState::StoppedTracking);
			// Remove the occlusion mesh if present
			UMRMeshComponent* MRMesh = TrackedGeometryBeingRemoved->GetUnderlyingMesh();
			if (MRMesh != nullptr)
			{
				MRMesh->UnregisterComponent();
				TrackedGeometryBeingRemoved->SetUnderlyingMesh(nullptr);
			}

			TArray<UARPin*> ARPinsBeingOrphaned = ARKitUtil::PinsFromGeometry(TrackedGeometryBeingRemoved, Pins);
			for(UARPin* PinBeingOrphaned : ARPinsBeingOrphaned)
			{
				PinBeingOrphaned->OnTrackingStateChanged(EARTrackingState::StoppedTracking);
			}
			// Trigger the delegate so anyone listening can take action
			TriggerOnTrackableRemovedDelegates(TrackedGeometryBeingRemoved);
		}
	}
	
	TrackedGeometries.Remove(AnchorGuid);
}

#endif

void FAppleARKitSystem::SessionDidUpdateFrame_Internal( TSharedRef< FAppleARKitFrame, ESPMode::ThreadSafe > Frame )
{
	LastReceivedFrame = Frame;
	UpdateFrame();
}

#if STATS
struct FARKitThreadTimes
{
	TArray<FString> ThreadNames;
	int32 LastTotal;
	int32 NewTotal;
	
	FARKitThreadTimes() :
		LastTotal(0)
		, NewTotal(0)
	{
		ThreadNames.Add(TEXT("com.apple.CoreMotion"));
		ThreadNames.Add(TEXT("com.apple.arkit"));
		ThreadNames.Add(TEXT("FilteringFrameDownsampleNodeWorkQueue"));
		ThreadNames.Add(TEXT("FeatureDetectorNodeWorkQueue"));
		ThreadNames.Add(TEXT("SurfaceDetectionNode"));
		ThreadNames.Add(TEXT("VIOEngineNode"));
		ThreadNames.Add(TEXT("ImageDetectionQueue"));
	}

	bool IsARKitThread(const FString& Name)
	{
		if (Name.Len() == 0)
		{
			return false;
		}
		
		for (int32 Index = 0; Index < ThreadNames.Num(); Index++)
		{
			if (Name.StartsWith(ThreadNames[Index]))
			{
				return true;
			}
		}
		return false;
	}
	
	void FrameReset()
	{
		LastTotal = NewTotal;
		NewTotal = 0;
	}
};
#endif

void FAppleARKitSystem::UpdateARKitPerfStats()
{
#if STATS && SUPPORTS_ARKIT_1_0
	static FARKitThreadTimes ARKitThreadTimes;

	SCOPE_CYCLE_COUNTER(STAT_FAppleARKitSystem_UpdateARKitPerf);
	ARKitThreadTimes.FrameReset();
	
	thread_array_t ThreadArray;
	mach_msg_type_number_t ThreadCount;
	if (task_threads(mach_task_self(), &ThreadArray, &ThreadCount) != KERN_SUCCESS)
	{
		return;
	}

	for (int32 Index = 0; Index < (int32)ThreadCount; Index++)
	{
		mach_msg_type_number_t ThreadInfoCount = THREAD_BASIC_INFO_COUNT;
		mach_msg_type_number_t ExtThreadInfoCount = THREAD_EXTENDED_INFO_COUNT;
		thread_info_data_t ThreadInfo;
		thread_extended_info_data_t ExtThreadInfo;
		// Get the basic thread info for this thread
		if (thread_info(ThreadArray[Index], THREAD_BASIC_INFO, (thread_info_t)ThreadInfo, &ThreadInfoCount) != KERN_SUCCESS)
		{
			continue;
		}
		// And the extended thread info for this thread
		if (thread_info(ThreadArray[Index], THREAD_EXTENDED_INFO, (thread_info_t)&ExtThreadInfo, &ExtThreadInfoCount) != KERN_SUCCESS)
		{
			continue;
		}
		thread_basic_info_t BasicInfo = (thread_basic_info_t)ThreadInfo;
		FString ThreadName(ExtThreadInfo.pth_name);
		if (ARKitThreadTimes.IsARKitThread(ThreadName))
		{
			// CPU usage is reported as a scaled number, so convert to %
			int32 ScaledPercent = FMath::RoundToInt((float)BasicInfo->cpu_usage / (float)TH_USAGE_SCALE * 100.f);
			ARKitThreadTimes.NewTotal += ScaledPercent;
		}
//		UE_LOG(LogAppleARKit, Log, TEXT("Thread %s used cpu (%d), seconds (%d), microseconds (%d)"), *ThreadName, BasicInfo->cpu_usage, BasicInfo->user_time.seconds + BasicInfo->system_time.seconds, BasicInfo->user_time.microseconds + BasicInfo->system_time.microseconds);
	}
	vm_deallocate(mach_task_self(), (vm_offset_t)ThreadArray, ThreadCount * sizeof(thread_t));
	SET_DWORD_STAT(STAT_ARKitThreads, ARKitThreadTimes.NewTotal);
#endif
}

#if SUPPORTS_ARKIT_1_0
void FAppleARKitSystem::WriteCameraImageToDisk(CVPixelBufferRef PixelBuffer)
{
	CFRetain(PixelBuffer);
	int32 ImageQuality = GetMutableDefault<UAppleARKitSettings>()->GetWrittenCameraImageQuality();
	float ImageScale = GetMutableDefault<UAppleARKitSettings>()->GetWrittenCameraImageScale();
	ETextureRotationDirection ImageRotation = GetMutableDefault<UAppleARKitSettings>()->GetWrittenCameraImageRotation();
	FTimecode Timecode = TimecodeProvider->GetTimecode();
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [PixelBuffer, ImageQuality, ImageScale, ImageRotation, Timecode]()
	{
		CIImage* SourceImage = [[CIImage alloc] initWithCVPixelBuffer: PixelBuffer];
		TArray<uint8> JpegBytes;
		IAppleImageUtilsPlugin::Get().ConvertToJPEG(SourceImage, JpegBytes, ImageQuality, true, true, ImageScale, ImageRotation);
		[SourceImage release];
		// Build a unique file name
		FDateTime DateTime = FDateTime::UtcNow();
		static FString UserDir = FPlatformProcess::UserDir();
		const FString& FaceDir = GetMutableDefault<UAppleARKitSettings>()->GetFaceTrackingLogDir();
		const TCHAR* SubDir = FaceDir.Len() > 0 ? *FaceDir : TEXT("CameraImages");
		FString FileName = FString::Printf(TEXT("%s%s/Image_%d-%d-%d-%d-%d-%d-%d.jpeg"), *UserDir, SubDir,
			DateTime.GetYear(), DateTime.GetMonth(), DateTime.GetDay(), Timecode.Hours, Timecode.Minutes, Timecode.Seconds, Timecode.Frames);
		// Write the jpeg to disk
		if (!FFileHelper::SaveArrayToFile(JpegBytes, *FileName))
		{
			UE_LOG(LogAppleARKit, Error, TEXT("Failed to save JPEG to file name '%s'"), *FileName);
		}
		CFRelease(PixelBuffer);
	});
}
#endif

bool FAppleARKitSystem::OnIsSessionTrackingFeatureSupported(EARSessionType SessionType, EARSessionTrackingFeature SessionTrackingFeature) const
{
	return FAppleARKitConversion::IsSessionTrackingFeatureSupported(SessionType, SessionTrackingFeature);
}

TArray<FARPose2D> FAppleARKitSystem::OnGetTracked2DPose() const
{
	if (GameThreadFrame && GameThreadFrame->Tracked2DPose.SkeletonDefinition.NumJoints > 0)
	{
		return { GameThreadFrame->Tracked2DPose };
	}
	
	return {};
}

UARTextureCameraImage* FAppleARKitSystem::OnGetPersonSegmentationImage() const
{
	return PersonSegmentationImage;
}

UARTextureCameraImage* FAppleARKitSystem::OnGetPersonSegmentationDepthImage() const
{
	return PersonSegmentationDepthImage;
}

namespace AppleARKitSupport
{
	TSharedPtr<class FAppleARKitSystem, ESPMode::ThreadSafe> CreateAppleARKitSystem()
	{
#if SUPPORTS_ARKIT_1_0
		// Handle older iOS devices somehow calling this
		if (FAppleARKitAvailability::SupportsARKit10())
		{
			auto NewARKitSystem = MakeShared<FAppleARKitSystem, ESPMode::ThreadSafe>();
            return NewARKitSystem;
		}
#endif
		return TSharedPtr<class FAppleARKitSystem, ESPMode::ThreadSafe>();
	}
}

UTimecodeProvider* UAppleARKitSettings::GetTimecodeProvider()
{
	const FString& ProviderName = GetDefault<UAppleARKitSettings>()->ARKitTimecodeProvider;
	UTimecodeProvider* TimecodeProvider = FindObject<UTimecodeProvider>(GEngine, *ProviderName);
	if (TimecodeProvider == nullptr)
	{
		// Try to load the class that was requested
		UClass* Class = LoadClass<UTimecodeProvider>(nullptr, *ProviderName);
		if (Class != nullptr)
		{
			TimecodeProvider = NewObject<UTimecodeProvider>(GEngine, Class);
		}
	}
	// Create the default one if this failed for some reason
	if (TimecodeProvider == nullptr)
	{
		TimecodeProvider = NewObject<UTimecodeProvider>(GEngine, UAppleARKitTimecodeProvider::StaticClass());
	}
	return TimecodeProvider;
}

void UAppleARKitSettings::CreateFaceTrackingLogDir()
{
	const FString& FaceDir = GetMutableDefault<UAppleARKitSettings>()->GetFaceTrackingLogDir();
	const TCHAR* SubDir = FaceDir.Len() > 0 ? *FaceDir : TEXT("FaceTracking");
	const FString UserDir = FPlatformProcess::UserDir();
	if (!IFileManager::Get().DirectoryExists(*(UserDir / SubDir)))
	{
		IFileManager::Get().MakeDirectory(*(UserDir / SubDir));
	}
}

void UAppleARKitSettings::CreateImageLogDir()
{
	const FString& FaceDir = GetMutableDefault<UAppleARKitSettings>()->GetFaceTrackingLogDir();
	const TCHAR* SubDir = FaceDir.Len() > 0 ? *FaceDir : TEXT("CameraImages");
	const FString UserDir = FPlatformProcess::UserDir();
	if (!IFileManager::Get().DirectoryExists(*(UserDir / SubDir)))
	{
		IFileManager::Get().MakeDirectory(*(UserDir / SubDir));
	}
}

FString UAppleARKitSettings::GetFaceTrackingLogDir()
{
	FScopeLock ScopeLock(&CriticalSection);
	return FaceTrackingLogDir;
}

bool UAppleARKitSettings::IsLiveLinkEnabledForFaceTracking()
{
	FScopeLock ScopeLock(&CriticalSection);
	return LivelinkTrackingType == ELivelinkTrackingType::FaceTracking;
}

bool UAppleARKitSettings::IsLiveLinkEnabledForPoseTracking()
{
	FScopeLock ScopeLock(&CriticalSection);
	return LivelinkTrackingType == ELivelinkTrackingType::PoseTracking;
}

bool UAppleARKitSettings::IsFaceTrackingLoggingEnabled()
{
	FScopeLock ScopeLock(&CriticalSection);
	return bFaceTrackingLogData;
}

bool UAppleARKitSettings::ShouldFaceTrackingLogPerFrame()
{
	FScopeLock ScopeLock(&CriticalSection);
	return bFaceTrackingWriteEachFrame;
}

EARFaceTrackingFileWriterType UAppleARKitSettings::GetFaceTrackingFileWriterType()
{
	FScopeLock ScopeLock(&CriticalSection);
	return FaceTrackingFileWriterType;
}

bool UAppleARKitSettings::ShouldWriteCameraImagePerFrame()
{
	FScopeLock ScopeLock(&CriticalSection);
	return bShouldWriteCameraImagePerFrame;
}

float UAppleARKitSettings::GetWrittenCameraImageScale()
{
	FScopeLock ScopeLock(&CriticalSection);
	return WrittenCameraImageScale;
}

int32 UAppleARKitSettings::GetWrittenCameraImageQuality()
{
	FScopeLock ScopeLock(&CriticalSection);
	return WrittenCameraImageQuality;
}

ETextureRotationDirection UAppleARKitSettings::GetWrittenCameraImageRotation()
{
	FScopeLock ScopeLock(&CriticalSection);
	return WrittenCameraImageRotation;
}

int32 UAppleARKitSettings::GetLiveLinkPublishingPort()
{
	FScopeLock ScopeLock(&CriticalSection);
	return LiveLinkPublishingPort;
}

FName UAppleARKitSettings::GetLiveLinkSubjectName()
{
	FScopeLock ScopeLock(&CriticalSection);
	return DefaultFaceTrackingLiveLinkSubjectName;
}

EARFaceTrackingDirection UAppleARKitSettings::GetFaceTrackingDirection()
{
	FScopeLock ScopeLock(&CriticalSection);
	return DefaultFaceTrackingDirection;
}

bool UAppleARKitSettings::ShouldAdjustThreadPriorities()
{
	FScopeLock ScopeLock(&CriticalSection);
	return bAdjustThreadPrioritiesDuringARSession;
}

int32 UAppleARKitSettings::GetGameThreadPriorityOverride()
{
	FScopeLock ScopeLock(&CriticalSection);
	return GameThreadPriorityOverride;
}

int32 UAppleARKitSettings::GetRenderThreadPriorityOverride()
{
	FScopeLock ScopeLock(&CriticalSection);
	return RenderThreadPriorityOverride;
}

bool UAppleARKitSettings::Exec(UWorld*, const TCHAR* Cmd, FOutputDevice& Ar)
{
	if (FParse::Command(&Cmd, TEXT("ARKitSettings")))
	{
		FScopeLock ScopeLock(&CriticalSection);

		if (FParse::Command(&Cmd, TEXT("StartFileWriting")))
		{
			UAppleARKitSettings::CreateFaceTrackingLogDir();
			bFaceTrackingLogData = true;
			bShouldWriteCameraImagePerFrame = true;
			return true;
		}
		else if (FParse::Command(&Cmd, TEXT("StopFileWriting")))
		{
			bFaceTrackingLogData = false;
			bShouldWriteCameraImagePerFrame = false;
			return true;
		}
		else if (FParse::Command(&Cmd, TEXT("StartCameraFileWriting")))
		{
			bShouldWriteCameraImagePerFrame = true;
			return true;
		}
		else if (FParse::Command(&Cmd, TEXT("StopCameraFileWriting")))
		{
			bShouldWriteCameraImagePerFrame = false;
			return true;
		}
		else if (FParse::Command(&Cmd, TEXT("SavePerFrame")))
		{
			bFaceTrackingWriteEachFrame = true;
			return true;
		}
		else if (FParse::Command(&Cmd, TEXT("SaveOnDemand")))
		{
			bFaceTrackingWriteEachFrame = false;
			return true;
		}
		else if (FParse::Value(Cmd, TEXT("FaceLogDir="), FaceTrackingLogDir))
		{
			UAppleARKitSettings::CreateFaceTrackingLogDir();
			return true;
		}
		else if (FParse::Value(Cmd, TEXT("LiveLinkSubjectName="), DefaultFaceTrackingLiveLinkSubjectName))
		{
			return true;
		}
	}
	return false;
}


/** Used to run Exec commands */
static bool MeshARTestingExec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	bool bHandled = false;

	if (FParse::Command(&Cmd, TEXT("ARKIT")))
	{
		if (FParse::Command(&Cmd, TEXT("MRMESH")))
		{
			AAROriginActor* OriginActor = AAROriginActor::GetOriginActor();
			UMRMeshComponent* NewComp = NewObject<UMRMeshComponent>(OriginActor);
			NewComp->RegisterComponent();
			NewComp->SetUseWireframe(true);
			// Send a fake update to it
			FTransform Transform = FTransform::Identity;
			TArray<FVector> Vertices;
			TArray<MRMESH_INDEX_TYPE> Indices;

			Vertices.Reset(4);
			Vertices.Add(FVector(100.f, 100.f, 0.f));
			Vertices.Add(FVector(100.f, -100.f, 0.f));
			Vertices.Add(FVector(-100.f, -100.f, 0.f));
			Vertices.Add(FVector(-100.f, 100.f, 0.f));

			Indices.Reset(6);
			Indices.Add(0);
			Indices.Add(1);
			Indices.Add(2);
			Indices.Add(2);
			Indices.Add(3);
			Indices.Add(0);

			NewComp->UpdateMesh(Transform.GetLocation(), Transform.GetRotation(), Transform.GetScale3D(), Vertices, Indices);

			return true;
		}
	}

	return false;
}

FStaticSelfRegisteringExec MeshARTestingExecRegistration(MeshARTestingExec);

#if PLATFORM_IOS
	#pragma clang diagnostic pop
#endif
