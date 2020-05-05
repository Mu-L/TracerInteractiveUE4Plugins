// Copyright Epic Games, Inc. All Rights Reserved.

#include "OculusMR_CastingCameraActor.h"

#include "OculusMRPrivate.h"
#include "OculusHMD_Settings.h"
#include "OculusHMD.h"
#include "OculusHMD_SpectatorScreenController.h"
#include "OculusMRModule.h"
#include "OculusMR_Settings.h"
#include "OculusMR_State.h"
#include "OculusMR_PlaneMeshComponent.h"
#include "OculusMR_BoundaryActor.h"
#include "OculusMR_BoundaryMeshComponent.h"
#include "OculusMRFunctionLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RenderingThread.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "VRNotificationsComponent.h"
#include "RenderUtils.h"
#include "AudioDevice.h"

#define LOCTEXT_NAMESPACE "OculusMR_CastingCameraActor"

// Possibly add 2=Limited in a future update
static TAutoConsoleVariable<int32> CEnableExternalCompositionPostProcess(TEXT("oculus.mr.ExternalCompositionPostProcess"), 0, TEXT("Enable MR external composition post process: 0=Off, 1=On")); 
static TAutoConsoleVariable<int32> COverrideMixedRealityParametersVar(TEXT("oculus.mr.OverrideParameters"), 0, TEXT("Use the Mixed Reality console variables"));
static TAutoConsoleVariable<int32> CChromaKeyColorRVar(TEXT("oculus.mr.ChromaKeyColor_R"), 0, TEXT("Chroma Key Color R"));
static TAutoConsoleVariable<int32> CChromaKeyColorGVar(TEXT("oculus.mr.ChromaKeyColor_G"), 255, TEXT("Chroma Key Color G"));
static TAutoConsoleVariable<int32> CChromaKeyColorBVar(TEXT("oculus.mr.ChromaKeyColor_B"), 0, TEXT("Chroma Key Color B"));
static TAutoConsoleVariable<float> CChromaKeySimilarityVar(TEXT("oculus.mr.ChromaKeySimilarity"), 0.6f, TEXT("Chroma Key Similarity"));
static TAutoConsoleVariable<float> CChromaKeySmoothRangeVar(TEXT("oculus.mr.ChromaKeySmoothRange"), 0.03f, TEXT("Chroma Key Smooth Range"));
static TAutoConsoleVariable<float> CChromaKeySpillRangeVar(TEXT("oculus.mr.ChromaKeySpillRange"), 0.04f, TEXT("Chroma Key Spill Range"));
static TAutoConsoleVariable<float> CCastingLantencyVar(TEXT("oculus.mr.CastingLantency"), 0, TEXT("Casting Latency"));

namespace
{
	bool GetCameraTrackedObjectPoseInTrackingSpace(OculusHMD::FOculusHMD* OculusHMD, const FTrackedCamera& TrackedCamera, OculusHMD::FPose& CameraTrackedObjectPose)
	{
		using namespace OculusHMD;

		CameraTrackedObjectPose = FPose(FQuat::Identity, FVector::ZeroVector);

		if (TrackedCamera.AttachedTrackedDevice != ETrackedDeviceType::None)
		{
			ovrpResult result = ovrpSuccess;
			ovrpPoseStatef cameraPoseState;
			ovrpNode deviceNode = ToOvrpNode(TrackedCamera.AttachedTrackedDevice);
			ovrpBool nodePresent = ovrpBool_False;
			result = FOculusHMDModule::GetPluginWrapper().GetNodePresent2(deviceNode, &nodePresent);
			if (!OVRP_SUCCESS(result))
			{
				UE_LOG(LogMR, Warning, TEXT("Unable to check if AttachedTrackedDevice is present"));
				return false;
			}
			if (!nodePresent)
			{
				UE_LOG(LogMR, Warning, TEXT("AttachedTrackedDevice is not present"));
				return false;
			}

			OculusHMD::FGameFrame* CurrentFrame;
			if (IsInGameThread())
			{
				CurrentFrame = OculusHMD->GetNextFrameToRender();
			}
			else
			{
				CurrentFrame = OculusHMD->GetFrame_RenderThread();
			}

			result = CurrentFrame ? FOculusHMDModule::GetPluginWrapper().GetNodePoseState3(ovrpStep_Render, CurrentFrame->FrameNumber, deviceNode, &cameraPoseState) : ovrpFailure;
			if (!OVRP_SUCCESS(result))
			{
				UE_LOG(LogMR, Warning, TEXT("Unable to retrieve AttachedTrackedDevice pose state"));
				return false;
			}
			OculusHMD->ConvertPose(cameraPoseState.Pose, CameraTrackedObjectPose);
		}

		return true;
	}
}

//////////////////////////////////////////////////////////////////////////
// ACastingCameraActor

AOculusMR_CastingCameraActor::AOculusMR_CastingCameraActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ChromaKeyMaterial(NULL)
	, ChromaKeyLitMaterial(NULL)
	, ChromaKeyMaterialInstance(NULL)
	, ChromaKeyLitMaterialInstance(NULL)
	, CameraFrameMaterialInstance(NULL)
	, TrackedCameraCalibrationRequired(false)
	, HasTrackedCameraCalibrationCalibrated(false)
	, RefreshBoundaryMeshCounter(3)
	, ForegroundLayerBackgroundColor(FColor::Green)
	, ForegroundMaxDistance(300.0f)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;

	VRNotificationComponent = CreateDefaultSubobject<UVRNotificationsComponent>(TEXT("VRNotificationComponent"));

#if PLATFORM_WINDOWS
	PlaneMeshComponent = CreateDefaultSubobject<UOculusMR_PlaneMeshComponent>(TEXT("PlaneMeshComponent"));
	PlaneMeshComponent->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	PlaneMeshComponent->ResetRelativeTransform();
	PlaneMeshComponent->SetVisibility(false);
#endif

	ChromaKeyMaterial = Cast<UMaterial>(StaticLoadObject(UMaterial::StaticClass(), NULL, TEXT("/OculusVR/Materials/OculusMR_ChromaKey")));
	if (!ChromaKeyMaterial)
	{
		UE_LOG(LogMR, Warning, TEXT("Invalid ChromaKeyMaterial"));
	}

	ChromaKeyLitMaterial = Cast<UMaterial>(StaticLoadObject(UMaterial::StaticClass(), NULL, TEXT("/OculusVR/Materials/OculusMR_ChromaKey_Lit")));
	if (!ChromaKeyLitMaterial)
	{
		UE_LOG(LogMR, Warning, TEXT("Invalid ChromaKeyLitMaterial"));
	}

	OpaqueColoredMaterial = Cast<UMaterial>(StaticLoadObject(UMaterial::StaticClass(), NULL, TEXT("/OculusVR/Materials/OculusMR_OpaqueColoredMaterial")));
	if (!OpaqueColoredMaterial)
	{
		UE_LOG(LogMR, Warning, TEXT("Invalid OpaqueColoredMaterial"));
	}

	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		ConstructorHelpers::FObjectFinder<UTexture2D> WhiteSquareTexture;

		FConstructorStatics()
			: WhiteSquareTexture(TEXT("/Engine/EngineResources/WhiteSquareTexture"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	DefaultTexture_White = ConstructorStatics.WhiteSquareTexture.Object;
	check(DefaultTexture_White);

	BoundarySceneCaptureActor = nullptr;
	ForegroundCaptureActor = nullptr;

	// Set the render targets for background and foreground to copies of the default texture
#if PLATFORM_WINDOWS
	BackgroundRenderTargets.SetNum(1);
	ForegroundRenderTargets.SetNum(1);

	BackgroundRenderTargets[0] = NewObject<UTextureRenderTarget2D>();
	BackgroundRenderTargets[0]->RenderTargetFormat = RTF_RGBA8;
	BackgroundRenderTargets[0]->TargetGamma = 1.001;

	ForegroundRenderTargets[0] = NewObject<UTextureRenderTarget2D>();
	ForegroundRenderTargets[0]->RenderTargetFormat = RTF_RGBA8;
	ForegroundRenderTargets[0]->TargetGamma = 1.001;
#elif PLATFORM_ANDROID
	BackgroundRenderTargets.SetNum(NumRTs);
	ForegroundRenderTargets.SetNum(NumRTs);
	AudioBuffers.SetNum(NumRTs);
	AudioTimes.SetNum(NumRTs);

	for (unsigned int i = 0; i < NumRTs; ++i)
	{
		BackgroundRenderTargets[i] = NewObject<UTextureRenderTarget2D>();
		BackgroundRenderTargets[i]->RenderTargetFormat = RTF_RGBA8;

		ForegroundRenderTargets[i] = NewObject<UTextureRenderTarget2D>();
		ForegroundRenderTargets[i]->RenderTargetFormat = RTF_RGBA8;

		AudioTimes[i] = 0.0;
	}

	SyncId = -1;
	RenderedRTs = 0;
	CaptureIndex = 0;
#endif
}

void AOculusMR_CastingCameraActor::BeginDestroy()
{
	CloseTrackedCamera();
	Super::BeginDestroy();
}

bool AOculusMR_CastingCameraActor::RefreshExternalCamera()
{
	using namespace OculusHMD;
	if (MRState->TrackedCamera.Index >= 0)
	{
		int cameraCount;
		if (OVRP_FAILURE(FOculusHMDModule::GetPluginWrapper().GetExternalCameraCount(&cameraCount)))
		{
			cameraCount = 0;
		}
		if (MRState->TrackedCamera.Index >= cameraCount)
		{
			UE_LOG(LogMR, Error, TEXT("Invalid TrackedCamera Index"));
			return false;
		}
		FOculusHMD* OculusHMD = GEngine->XRSystem.IsValid() ? (FOculusHMD*)(GEngine->XRSystem->GetHMDDevice()) : nullptr;
		if (!OculusHMD)
		{
			UE_LOG(LogMR, Error, TEXT("Unable to retrieve OculusHMD"));
			return false;
		}
		ovrpResult result = ovrpSuccess;
		ovrpCameraExtrinsics cameraExtrinsics;
		result = FOculusHMDModule::GetPluginWrapper().GetExternalCameraExtrinsics(MRState->TrackedCamera.Index, &cameraExtrinsics);
		if (OVRP_FAILURE(result))
		{
			UE_LOG(LogMR, Error, TEXT("FOculusHMDModule::GetPluginWrapper().GetExternalCameraExtrinsics failed"));
			return false;
		}
		MRState->TrackedCamera.AttachedTrackedDevice = OculusHMD::ToETrackedDeviceType(cameraExtrinsics.AttachedToNode);
		OculusHMD::FPose Pose;
		OculusHMD->ConvertPose(cameraExtrinsics.RelativePose, Pose);
		MRState->TrackedCamera.CalibratedRotation = Pose.Orientation.Rotator();
		MRState->TrackedCamera.CalibratedOffset = Pose.Position;
	}

	return true;
}

void AOculusMR_CastingCameraActor::BeginPlay()
{
	Super::BeginPlay();

	SetupTrackedCamera();
	RequestTrackedCameraCalibration();
	SetupMRCScreen();

#if PLATFORM_WINDOWS
	BoundaryActor = GetWorld()->SpawnActor<AOculusMR_BoundaryActor>(AOculusMR_BoundaryActor::StaticClass());
	BoundaryActor->SetActorTransform(FTransform::Identity);

	BoundarySceneCaptureActor = GetWorld()->SpawnActor<ASceneCapture2D>(ASceneCapture2D::StaticClass());
	BoundarySceneCaptureActor->GetCaptureComponent2D()->CaptureSource = SCS_SceneColorHDRNoAlpha;
	BoundarySceneCaptureActor->GetCaptureComponent2D()->CaptureStereoPass = eSSP_FULL;
	BoundarySceneCaptureActor->GetCaptureComponent2D()->bCaptureEveryFrame = false;
	BoundarySceneCaptureActor->GetCaptureComponent2D()->bCaptureOnMovement = false;
	BoundarySceneCaptureActor->GetCaptureComponent2D()->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	BoundarySceneCaptureActor->GetCaptureComponent2D()->ShowOnlyActorComponents(BoundaryActor);
	BoundarySceneCaptureActor->GetCaptureComponent2D()->ShowFlags.Fog = false;
	BoundarySceneCaptureActor->GetCaptureComponent2D()->ShowFlags.PostProcessing = false;
	BoundarySceneCaptureActor->GetCaptureComponent2D()->ShowFlags.Lighting = false;
	BoundarySceneCaptureActor->GetCaptureComponent2D()->ShowFlags.DisableAdvancedFeatures();
	BoundarySceneCaptureActor->GetCaptureComponent2D()->bEnableClipPlane = false;
	BoundarySceneCaptureActor->GetCaptureComponent2D()->MaxViewDistanceOverride = 10000.0f;

	if (BoundarySceneCaptureActor->GetCaptureComponent2D()->TextureTarget)
	{
		BoundarySceneCaptureActor->GetCaptureComponent2D()->TextureTarget->ClearColor = FLinearColor::Black;
	}

	RefreshBoundaryMesh();
#endif

	FScriptDelegate Delegate;
	Delegate.BindUFunction(this, FName(TEXT("OnHMDRecentered")));
	VRNotificationComponent->HMDRecenteredDelegate.Add(Delegate);

#if PLATFORM_ANDROID
	FAudioDeviceHandle AudioDevice = FAudioDevice::GetMainAudioDevice();
	AudioDevice->StartRecording(nullptr, 0.1);
#endif
}

void AOculusMR_CastingCameraActor::EndPlay(EEndPlayReason::Type Reason)
{
#if PLATFORM_ANDROID
	FAudioDeviceHandle AudioDevice = FAudioDevice::GetMainAudioDevice();
	float NumChannels = 2;
	float SampleRate = AudioDevice->GetSampleRate();
	AudioDevice->StopRecording(nullptr, NumChannels, SampleRate);
#endif

	VRNotificationComponent->HMDRecenteredDelegate.Remove(this, FName(TEXT("OnHMDRecentered")));

#if PLATFORM_WINDOWS
	if (BoundarySceneCaptureActor)
	{
		BoundarySceneCaptureActor->Destroy();
		BoundarySceneCaptureActor = NULL;
	}

	if (BoundaryActor)
	{
		BoundaryActor->Destroy();
		BoundaryActor = NULL;
	}
#endif

	MRState->TrackingReferenceComponent = nullptr;

	CloseMRCScreen();

	CloseTrackedCamera();
	Super::EndPlay(Reason);
}

void AOculusMR_CastingCameraActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (MRState->BindToTrackedCameraIndexRequested)
	{
		Execute_BindToTrackedCameraIndexIfAvailable();
	}

	if (!RefreshExternalCamera())
	{
		CloseTrackedCamera();
		return;
	}

	if (COverrideMixedRealityParametersVar.GetValueOnAnyThread() > 0)
	{
		MRSettings->ChromaKeyColor = FColor(CChromaKeyColorRVar.GetValueOnAnyThread(), CChromaKeyColorGVar.GetValueOnAnyThread(), CChromaKeyColorBVar.GetValueOnAnyThread());
		MRSettings->ChromaKeySimilarity = CChromaKeySimilarityVar.GetValueOnAnyThread();
		MRSettings->ChromaKeySmoothRange = CChromaKeySmoothRangeVar.GetValueOnAnyThread();
		MRSettings->ChromaKeySpillRange = CChromaKeySpillRangeVar.GetValueOnAnyThread();
		MRSettings->CastingLatency = CCastingLantencyVar.GetValueOnAnyThread();
	}

	// Reset capturing components if the composition method changes
	if (MRState->ChangeCameraStateRequested)
	{
		CloseTrackedCamera();
		CloseMRCScreen();
		SetupTrackedCamera();
		SetupMRCScreen();
	}

#if PLATFORM_WINDOWS
	if (MRSettings->GetCompositionMethod() == EOculusMR_CompositionMethod::ExternalComposition)
#endif
	{
		if (ForegroundLayerBackgroundColor != MRSettings->BackdropColor)
		{
			ForegroundLayerBackgroundColor = MRSettings->BackdropColor;
			SetBackdropMaterialColor();
		}
		// Enable external composition post process based on setting
		bool bPostProcess = MRSettings->ExternalCompositionPostProcessEffects != EOculusMR_PostProcessEffects::PPE_Off;
		if (COverrideMixedRealityParametersVar.GetValueOnAnyThread() > 0)
		{
			bPostProcess = CEnableExternalCompositionPostProcess.GetValueOnAnyThread() > 0;
		}
		GetCaptureComponent2D()->ShowFlags.PostProcessing = bPostProcess;
		if (ForegroundCaptureActor)
		{
			ForegroundCaptureActor->GetCaptureComponent2D()->ShowFlags.PostProcessing = bPostProcess;
		}
	}
#if PLATFORM_WINDOWS
	else if (MRSettings->GetCompositionMethod() == EOculusMR_CompositionMethod::DirectComposition)
	{
		SetupCameraFrameMaterialInstance();

		if (CameraFrameMaterialInstance)
		{
			CameraFrameMaterialInstance->SetVectorParameterValue(FName(TEXT("ChromaKeyColor")), FLinearColor(MRSettings->ChromaKeyColor));
			CameraFrameMaterialInstance->SetScalarParameterValue(FName(TEXT("ChromaKeySimilarity")), MRSettings->ChromaKeySimilarity);
			CameraFrameMaterialInstance->SetScalarParameterValue(FName(TEXT("ChromaKeySmoothRange")), MRSettings->ChromaKeySmoothRange);
			CameraFrameMaterialInstance->SetScalarParameterValue(FName(TEXT("ChromaKeySpillRange")), MRSettings->ChromaKeySpillRange);
			if (MRSettings->GetUseDynamicLighting())
			{
				CameraFrameMaterialInstance->SetScalarParameterValue(FName(TEXT("DepthSmoothFactor")), MRSettings->DynamicLightingDepthSmoothFactor);
				CameraFrameMaterialInstance->SetScalarParameterValue(FName(TEXT("DepthVariationClampingValue")), MRSettings->DynamicLightingDepthVariationClampingValue);
			}
		}
	}
#endif

	if (MRState->CurrentCapturingCamera != ovrpCameraDevice_None)
	{
		ovrpBool colorFrameAvailable = ovrpBool_False;
		ovrpSizei colorFrameSize = { 0, 0 };
		const ovrpByte* colorFrameData = nullptr;
		int colorRowPitch = 0;

		if (OVRP_SUCCESS(FOculusHMDModule::GetPluginWrapper().IsCameraDeviceColorFrameAvailable2(MRState->CurrentCapturingCamera, &colorFrameAvailable)) && colorFrameAvailable &&
			OVRP_SUCCESS(FOculusHMDModule::GetPluginWrapper().GetCameraDeviceColorFrameSize(MRState->CurrentCapturingCamera, &colorFrameSize)) &&
			OVRP_SUCCESS(FOculusHMDModule::GetPluginWrapper().GetCameraDeviceColorFrameBgraPixels(MRState->CurrentCapturingCamera, &colorFrameData, &colorRowPitch)))
		{
			UpdateCameraColorTexture(colorFrameSize, colorFrameData, colorRowPitch);
		}

		ovrpBool supportDepth = ovrpBool_False;
		ovrpBool depthFrameAvailable = ovrpBool_False;
		ovrpSizei depthFrameSize = { 0, 0 };
		const float* depthFrameData = nullptr;
		int depthRowPitch = 0;
		if (MRSettings->GetUseDynamicLighting() &&
			OVRP_SUCCESS(FOculusHMDModule::GetPluginWrapper().DoesCameraDeviceSupportDepth(MRState->CurrentCapturingCamera, &supportDepth)) && supportDepth &&
			OVRP_SUCCESS(FOculusHMDModule::GetPluginWrapper().IsCameraDeviceDepthFrameAvailable(MRState->CurrentCapturingCamera, &depthFrameAvailable)) && depthFrameAvailable &&
			OVRP_SUCCESS(FOculusHMDModule::GetPluginWrapper().GetCameraDeviceDepthFrameSize(MRState->CurrentCapturingCamera, &depthFrameSize)) &&
			OVRP_SUCCESS(FOculusHMDModule::GetPluginWrapper().GetCameraDeviceDepthFramePixels(MRState->CurrentCapturingCamera, &depthFrameData, &depthRowPitch))
			)
		{
			UpdateCameraDepthTexture(depthFrameSize, depthFrameData, depthRowPitch);
		}
	}

	if (TrackedCameraCalibrationRequired)
	{
		CalibrateTrackedCameraPose();
	}

	UpdateTrackedCameraPosition();

#if PLATFORM_WINDOWS
	if (MRSettings->GetCompositionMethod() == EOculusMR_CompositionMethod::DirectComposition)
	{
		UpdateBoundaryCapture();
	}

	RepositionPlaneMesh();

	double HandPoseStateLatencyToSet = (double)MRSettings->HandPoseStateLatency;
	ovrpResult result = FOculusHMDModule::GetPluginWrapper().SetHandNodePoseStateLatency(HandPoseStateLatencyToSet);
	if (OVRP_FAILURE(result))
	{
		UE_LOG(LogMR, Warning, TEXT("FOculusHMDModule::GetPluginWrapper().SetHandNodePoseStateLatency(%f) failed, result %d"), HandPoseStateLatencyToSet, (int)result);
	}
#endif

	UpdateRenderTargetSize();

#if PLATFORM_ANDROID
	// Alternate foreground and background captures by nulling the capture component texture target
	if (GetCaptureComponent2D()->IsVisible())
	{
		GetCaptureComponent2D()->SetVisibility(false);

		// Encode a texture the frame before we render to it again to ensure completed render at the cost of latency
		unsigned int EncodeIndex = (CaptureIndex + 1) % NumRTs;

		// Skip encoding for the first few frames before they have completed rendering
		if (RenderedRTs > EncodeIndex)
		{
			FOculusHMDModule::GetPluginWrapper().Media_SyncMrcFrame(SyncId);

			int NumChannels = 2;
			double AudioTime = AudioTimes[EncodeIndex];
			void* BackgroundTexture;
			void* ForegroundTexture;

			if (IsVulkanPlatform(GMaxRHIShaderPlatform))
			{
				ExecuteOnRenderThread([this, EncodeIndex, &BackgroundTexture, &ForegroundTexture]()
				{
					ExecuteOnRHIThread([this, EncodeIndex, &BackgroundTexture, &ForegroundTexture]()
					{
						// The Vulkan RHI's implementation of GetNativeResource is different and returns the VkImage cast
						// as a void* instead of a pointer to the VkImage, so we need this workaround
						BackgroundTexture = (void*)BackgroundRenderTargets[EncodeIndex]->Resource->TextureRHI->GetNativeResource();
						ForegroundTexture = (void*)ForegroundRenderTargets[EncodeIndex]->Resource->TextureRHI->GetNativeResource();
					});
				});
			}
			else
			{
				ExecuteOnRenderThread([this, EncodeIndex, &BackgroundTexture, &ForegroundTexture]()
				{
					ExecuteOnRHIThread([this, EncodeIndex, &BackgroundTexture, &ForegroundTexture]()
					{
						BackgroundTexture = *((void**)BackgroundRenderTargets[EncodeIndex]->Resource->TextureRHI->GetNativeResource());
						ForegroundTexture = *((void**)ForegroundRenderTargets[EncodeIndex]->Resource->TextureRHI->GetNativeResource());
					});
				});
			}
			FOculusHMDModule::GetPluginWrapper().Media_EncodeMrcFrameWithDualTextures(BackgroundTexture, ForegroundTexture, AudioBuffers[EncodeIndex].GetData(), AudioBuffers[EncodeIndex].Num() * sizeof(float), NumChannels, AudioTime, &SyncId);
		}
		ForegroundCaptureActor->GetCaptureComponent2D()->SetVisibility(true);
	}
	else if (ForegroundCaptureActor && ForegroundCaptureActor->GetCaptureComponent2D()->IsVisible())
	{
		ForegroundCaptureActor->GetCaptureComponent2D()->SetVisibility(false);

		// Increment scene captures to next texture
		CaptureIndex = (CaptureIndex + 1) % NumRTs;
		GetCaptureComponent2D()->TextureTarget = BackgroundRenderTargets[CaptureIndex];
		ForegroundCaptureActor->GetCaptureComponent2D()->TextureTarget = ForegroundRenderTargets[CaptureIndex];
		GetCaptureComponent2D()->SetVisibility(true);

		FAudioDeviceHandle AudioDevice = FAudioDevice::GetMainAudioDevice();
		float NumChannels, SampleRate;
		NumChannels = 2;
		SampleRate = AudioDevice->GetSampleRate();
		AudioBuffers[CaptureIndex] = AudioDevice->StopRecording(nullptr, NumChannels, SampleRate);
		AudioTimes[CaptureIndex] = AudioDevice->GetAudioTime();
		//UE_LOG(LogMR, Error, TEXT("SampleRate: %f, NumChannels: %f, Time: %f, Buffer Length: %d, Buffer: %p"), SampleRate, NumChannels, AudioDevice->GetAudioTime(), AudioBuffers[EncodeIndex].Num(), AudioBuffers[EncodeIndex].GetData());
		AudioDevice->StartRecording(nullptr, 0.1);

		// Increment this counter for the initial cycle through "swapchain"
		if (RenderedRTs < NumRTs)
		{
			RenderedRTs++;
		}
	}
#endif
}

void AOculusMR_CastingCameraActor::UpdateBoundaryCapture()
{
	if (MRSettings->VirtualGreenScreenType != EOculusMR_VirtualGreenScreenType::VGS_Off)
	{
		if (RefreshBoundaryMeshCounter > 0)
		{
			--RefreshBoundaryMeshCounter;
			BoundaryActor->BoundaryMeshComponent->MarkRenderStateDirty();
		}
		FVector TRLocation;
		FRotator TRRotation;
		if (UOculusMRFunctionLibrary::GetTrackingReferenceLocationAndRotationInWorldSpace(MRState->TrackingReferenceComponent, TRLocation, TRRotation))
		{
			FTransform TargetTransform(TRRotation, TRLocation);
			BoundaryActor->BoundaryMeshComponent->SetComponentToWorld(TargetTransform);
		}
		else
		{
			UE_LOG(LogMR, Warning, TEXT("Could not get the tracking reference transform"));
		}
	}

	if (MRSettings->VirtualGreenScreenType != EOculusMR_VirtualGreenScreenType::VGS_Off && BoundaryActor->IsBoundaryValid())
	{
		if (MRSettings->VirtualGreenScreenType == EOculusMR_VirtualGreenScreenType::VGS_OuterBoundary)
		{
			if (BoundaryActor->BoundaryMeshComponent->BoundaryType != EOculusMR_BoundaryType::BT_OuterBoundary)
			{
				BoundaryActor->BoundaryMeshComponent->BoundaryType = EOculusMR_BoundaryType::BT_OuterBoundary;
				RefreshBoundaryMesh();
			}
		}
		else if (MRSettings->VirtualGreenScreenType == EOculusMR_VirtualGreenScreenType::VGS_PlayArea)
		{
			if (BoundaryActor->BoundaryMeshComponent->BoundaryType != EOculusMR_BoundaryType::BT_PlayArea)
			{
				BoundaryActor->BoundaryMeshComponent->BoundaryType = EOculusMR_BoundaryType::BT_PlayArea;
				RefreshBoundaryMesh();
			}
		}

		BoundarySceneCaptureActor->SetActorTransform(GetActorTransform());
		BoundarySceneCaptureActor->GetCaptureComponent2D()->FOVAngle = GetCaptureComponent2D()->FOVAngle;
		UTextureRenderTarget2D* RenderTarget = BoundarySceneCaptureActor->GetCaptureComponent2D()->TextureTarget;

		int ViewWidth = MRSettings->bUseTrackedCameraResolution ? MRState->TrackedCamera.SizeX : MRSettings->WidthPerView;
		int ViewHeight = MRSettings->bUseTrackedCameraResolution ? MRState->TrackedCamera.SizeY : MRSettings->HeightPerView;
		if (RenderTarget == NULL || RenderTarget->GetSurfaceWidth() != ViewWidth || RenderTarget->GetSurfaceHeight() != ViewHeight)
		{
			RenderTarget = NewObject<UTextureRenderTarget2D>();
			RenderTarget->ClearColor = FLinearColor::Black;
			RenderTarget->bAutoGenerateMips = false;
			RenderTarget->bGPUSharedFlag = false;
			RenderTarget->InitCustomFormat(ViewWidth, ViewHeight, PF_B8G8R8A8, false);
			BoundarySceneCaptureActor->GetCaptureComponent2D()->TextureTarget = RenderTarget;
		}
		BoundarySceneCaptureActor->GetCaptureComponent2D()->CaptureSceneDeferred();

		if (CameraFrameMaterialInstance)
		{
			CameraFrameMaterialInstance->SetTextureParameterValue(FName(TEXT("MaskTexture")), RenderTarget);
		}
	}
	else
	{
		if (CameraFrameMaterialInstance)
		{
			CameraFrameMaterialInstance->SetTextureParameterValue(FName(TEXT("MaskTexture")), DefaultTexture_White);
		}
	}
}

void AOculusMR_CastingCameraActor::UpdateCameraColorTexture(const ovrpSizei &frameSize, const ovrpByte* frameData, int rowPitch)
{
	if (CameraColorTexture->GetSizeX() != frameSize.w || CameraColorTexture->GetSizeY() != frameSize.h)
	{
		UE_LOG(LogMR, Log, TEXT("CameraColorTexture resize to (%d, %d)"), frameSize.w, frameSize.h);
		CameraColorTexture = UTexture2D::CreateTransient(frameSize.w, frameSize.h);
		CameraColorTexture->UpdateResource();
		if (CameraFrameMaterialInstance)
		{
			CameraFrameMaterialInstance->SetTextureParameterValue(FName(TEXT("CameraCaptureTexture")), CameraColorTexture);
			CameraFrameMaterialInstance->SetVectorParameterValue(FName(TEXT("CameraCaptureTextureSize")),
				FLinearColor((float)CameraColorTexture->GetSizeX(), (float)CameraColorTexture->GetSizeY(), 1.0f / FMath::Max<int32>(1, CameraColorTexture->GetSizeX()), 1.0f / FMath::Max<int32>(1, CameraColorTexture->GetSizeY())));
		}
	}
	uint32 Pitch = rowPitch;
	uint32 DataSize = frameSize.h * Pitch;
	uint8* SrcData = (uint8*)FMemory::Malloc(DataSize);
	FMemory::Memcpy(SrcData, frameData, DataSize);

	struct FUploadCameraTextureContext
	{
		uint8* CameraBuffer;	// Render thread assumes ownership
		uint32 CameraBufferPitch;
		FTexture2DResource* DestTextureResource;
		uint32 FrameWidth;
		uint32 FrameHeight;
	} Context =
	{
		SrcData,
		Pitch,
		(FTexture2DResource*)CameraColorTexture->Resource,
		(uint32) frameSize.w,
		(uint32) frameSize.h
	};

	ENQUEUE_RENDER_COMMAND(UpdateCameraColorTexture)(
		[Context](FRHICommandListImmediate& RHICmdList)
		{
			const FUpdateTextureRegion2D UpdateRegion(
				0, 0,		// Dest X, Y
				0, 0,		// Source X, Y
				Context.FrameWidth,	    // Width
				Context.FrameHeight	    // Height
			);

			RHIUpdateTexture2D(
				Context.DestTextureResource->GetTexture2DRHI(),	// Destination GPU texture
				0,												// Mip map index
				UpdateRegion,									// Update region
				Context.CameraBufferPitch,						// Source buffer pitch
				Context.CameraBuffer);							// Source buffer pointer

			FMemory::Free(Context.CameraBuffer);
		}
	);
}

void AOculusMR_CastingCameraActor::UpdateCameraDepthTexture(const ovrpSizei &frameSize, const float* frameData, int rowPitch)
{
	if (!CameraDepthTexture || CameraDepthTexture->GetSizeX() != frameSize.w || CameraDepthTexture->GetSizeY() != frameSize.h)
	{
		UE_LOG(LogMR, Log, TEXT("CameraDepthTexture resize to (%d, %d)"), frameSize.w, frameSize.h);
		CameraDepthTexture = UTexture2D::CreateTransient(frameSize.w, frameSize.h, PF_R32_FLOAT);
		CameraDepthTexture->UpdateResource();
		if (CameraFrameMaterialInstance && MRSettings->GetUseDynamicLighting())
		{
			CameraFrameMaterialInstance->SetTextureParameterValue(FName(TEXT("CameraDepthTexture")), CameraDepthTexture);
		}
	}
	uint32 Pitch = rowPitch;
	uint32 DataSize = frameSize.h * Pitch;
	uint8* SrcData = (uint8*)FMemory::Malloc(DataSize);
	FMemory::Memcpy(SrcData, frameData, DataSize);

	struct FUploadCameraTextureContext
	{
		uint8* CameraBuffer;	// Render thread assumes ownership
		uint32 CameraBufferPitch;
		FTexture2DResource* DestTextureResource;
		uint32 FrameWidth;
		uint32 FrameHeight;
	} Context =
	{
		SrcData,
		Pitch,
		(FTexture2DResource*)CameraDepthTexture->Resource,
		(uint32) frameSize.w,
		(uint32) frameSize.h
	};

	ENQUEUE_RENDER_COMMAND(UpdateCameraDepthTexture)(
		[Context](FRHICommandListImmediate& RHICmdList)
		{
			const FUpdateTextureRegion2D UpdateRegion(
				0, 0,		// Dest X, Y
				0, 0,		// Source X, Y
				Context.FrameWidth,	    // Width
				Context.FrameHeight	    // Height
			);

			RHIUpdateTexture2D(
				Context.DestTextureResource->GetTexture2DRHI(),	// Destination GPU texture
				0,												// Mip map index
				UpdateRegion,									// Update region
				Context.CameraBufferPitch,						// Source buffer pitch
				Context.CameraBuffer);							// Source buffer pointer

			FMemory::Free(Context.CameraBuffer);
		}
	);
}

void AOculusMR_CastingCameraActor::Execute_BindToTrackedCameraIndexIfAvailable()
{
	if (!MRState->BindToTrackedCameraIndexRequested)
	{
		return;
	}

	FTrackedCamera TempTrackedCamera;
	if (MRSettings->GetBindToTrackedCameraIndex() >= 0)
	{
		TArray<FTrackedCamera> TrackedCameras;
		UOculusMRFunctionLibrary::GetAllTrackedCamera(TrackedCameras);
		int i;
		for (i = 0; i < TrackedCameras.Num(); ++i)
		{
			if (TrackedCameras[i].Index == MRSettings->GetBindToTrackedCameraIndex())
			{
				TempTrackedCamera = TrackedCameras[i];
				break;
			}
		}
		if (i == TrackedCameras.Num())
		{
			UE_LOG(LogMR, Warning, TEXT("Unable to find TrackedCamera at index %d, use TempTrackedCamera"), MRSettings->GetBindToTrackedCameraIndex());
		}
	}
	else
	{
		UE_LOG(LogMR, Warning, TEXT("BindToTrackedCameraIndex == %d, use TempTrackedCamera"), MRSettings->GetBindToTrackedCameraIndex());
	}

	MRState->TrackedCamera = TempTrackedCamera;
	if (MRState->TrackedCamera.Index < 0)
	{
		SetTrackedCameraUserPoseWithCameraTransform();
	}

	MRState->BindToTrackedCameraIndexRequested = false;
}

void AOculusMR_CastingCameraActor::RequestTrackedCameraCalibration()
{
	TrackedCameraCalibrationRequired = true;
}

void AOculusMR_CastingCameraActor::CalibrateTrackedCameraPose()
{
	SetTrackedCameraInitialPoseWithPlayerTransform();
	HasTrackedCameraCalibrationCalibrated = true;
	TrackedCameraCalibrationRequired = false;
}

void AOculusMR_CastingCameraActor::SetTrackedCameraInitialPoseWithPlayerTransform()
{
	using namespace OculusHMD;

	FOculusHMD* OculusHMD = GEngine->XRSystem.IsValid() ? (FOculusHMD*)(GEngine->XRSystem->GetHMDDevice()) : nullptr;
	if (!OculusHMD)
	{
		UE_LOG(LogMR, Warning, TEXT("Unable to retrieve OculusHMD"));
		return;
	}

	FPose CameraTrackedObjectPose;
	if (!GetCameraTrackedObjectPoseInTrackingSpace(OculusHMD, MRState->TrackedCamera, CameraTrackedObjectPose))
	{
		return;
	}

	FPose CameraPose = CameraTrackedObjectPose * FPose(MRState->TrackedCamera.CalibratedRotation.Quaternion(), MRState->TrackedCamera.CalibratedOffset);
	CameraPose = CameraPose * FPose(MRState->TrackedCamera.UserRotation.Quaternion(), MRState->TrackedCamera.UserOffset);

	FQuat TROrientation;
	FVector TRLocation;
	FRotator TRRotation;
	if (!UOculusMRFunctionLibrary::GetTrackingReferenceLocationAndRotationInWorldSpace(MRState->TrackingReferenceComponent, TRLocation, TRRotation))
	{
		UE_LOG(LogMR, Warning, TEXT("Could not get player position"));
		return;
	}

	TROrientation = TRRotation.Quaternion();
	FPose FinalPose = FPose(TROrientation, TRLocation) * CameraPose;

	InitialCameraAbsoluteOrientation = FinalPose.Orientation;
	InitialCameraAbsolutePosition = FinalPose.Position;
	InitialCameraRelativeOrientation = CameraPose.Orientation;
	InitialCameraRelativePosition = CameraPose.Position;

	GetCaptureComponent2D()->FOVAngle = MRState->TrackedCamera.FieldOfView;

	if (ForegroundCaptureActor)
	{
		ForegroundCaptureActor->GetCaptureComponent2D()->FOVAngle = MRState->TrackedCamera.FieldOfView;
	}
}


void AOculusMR_CastingCameraActor::SetTrackedCameraUserPoseWithCameraTransform()
{
	using namespace OculusHMD;

	FOculusHMD* OculusHMD = GEngine->XRSystem.IsValid() ? (FOculusHMD*)(GEngine->XRSystem->GetHMDDevice()) : nullptr;
	if (!OculusHMD)
	{
		UE_LOG(LogMR, Warning, TEXT("Unable to retrieve OculusHMD"));
		return;
	}

	FPose CameraTrackedObjectPose;
	if (!GetCameraTrackedObjectPoseInTrackingSpace(OculusHMD, MRState->TrackedCamera, CameraTrackedObjectPose))
	{
		return;
	}

	FPose CameraPose = CameraTrackedObjectPose * FPose(MRState->TrackedCamera.CalibratedRotation.Quaternion(), MRState->TrackedCamera.CalibratedOffset);

	FQuat TROrientation;
	FVector TRLocation;
	FRotator TRRotation;
	if (!UOculusMRFunctionLibrary::GetTrackingReferenceLocationAndRotationInWorldSpace(MRState->TrackingReferenceComponent, TRLocation, TRRotation))
	{
		UE_LOG(LogMR, Warning, TEXT("Could not get player position"));
		return;
	}
	TROrientation = TRRotation.Quaternion();
	FPose PlayerPose(TROrientation, TRLocation);
	FPose CurrentCameraPose = PlayerPose * CameraPose;

	FPose ExpectedCameraPose(GetCaptureComponent2D()->GetComponentRotation().Quaternion(), GetCaptureComponent2D()->GetComponentLocation());
	FPose UserPose = CurrentCameraPose.Inverse() * ExpectedCameraPose;

	MRState->TrackedCamera.UserRotation = UserPose.Orientation.Rotator();
	MRState->TrackedCamera.UserOffset = UserPose.Position;
}

void AOculusMR_CastingCameraActor::UpdateTrackedCameraPosition()
{
	check(HasTrackedCameraCalibrationCalibrated);

	using namespace OculusHMD;

	FOculusHMD* OculusHMD = GEngine->XRSystem.IsValid() ? (FOculusHMD*)(GEngine->XRSystem->GetHMDDevice()) : nullptr;
	if (!OculusHMD)
	{
		UE_LOG(LogMR, Warning, TEXT("Unable to retrieve OculusHMD"));
		return;
	}

	FPose CameraTrackedObjectPose;
	if (!GetCameraTrackedObjectPoseInTrackingSpace(OculusHMD, MRState->TrackedCamera, CameraTrackedObjectPose))
	{
		return;
	}

	FPose CameraTrackingSpacePose = FPose(MRState->TrackedCamera.CalibratedRotation.Quaternion(), MRState->TrackedCamera.CalibratedOffset);
#if PLATFORM_ANDROID
	ovrpPosef OvrpPose;
	FOculusHMDModule::GetPluginWrapper().GetTrackingTransformRawPose(&OvrpPose);
	FPose RawPose;
	OculusHMD->ConvertPose(OvrpPose, RawPose);
	FPose CalibrationRawPose = FPose(MRState->TrackedCamera.RawRotation.Quaternion(), MRState->TrackedCamera.RawOffset);
	CameraTrackingSpacePose = RawPose * (CalibrationRawPose.Inverse() * CameraTrackingSpacePose);
#endif
	FPose CameraPose = CameraTrackedObjectPose * CameraTrackingSpacePose;
	CameraPose = CameraPose * FPose(MRState->TrackedCamera.UserRotation.Quaternion(), MRState->TrackedCamera.UserOffset);
	CameraPose.Position = CameraPose.Position * MRState->ScalingFactor;

	float Distance = 0.0f;
	if (MRSettings->ClippingReference == EOculusMR_ClippingReference::CR_TrackingReference)
	{
		Distance = -FVector::DotProduct(CameraPose.Orientation.GetForwardVector().GetSafeNormal2D(), CameraPose.Position);
	}
	else if (MRSettings->ClippingReference == EOculusMR_ClippingReference::CR_Head)
	{
		FQuat HeadOrientation;
		FVector HeadPosition;
		OculusHMD->GetCurrentPose(IXRTrackingSystem::HMDDeviceId, HeadOrientation, HeadPosition);
		FVector HeadToCamera = HeadPosition - CameraPose.Position;
		Distance = FVector::DotProduct(CameraPose.Orientation.GetForwardVector().GetSafeNormal2D(), HeadToCamera);
	}
	else
	{
		checkNoEntry();
	}
	ForegroundMaxDistance = FMath::Max(Distance, GMinClipZ);
	if (ForegroundCaptureActor)
	{
		ForegroundCaptureActor->GetCaptureComponent2D()->MaxViewDistanceOverride = ForegroundMaxDistance;
	}

	FPose FinalPose;
	FQuat TROrientation;
	FVector TRLocation;
	FRotator TRRotation;
	if (!UOculusMRFunctionLibrary::GetTrackingReferenceLocationAndRotationInWorldSpace(MRState->TrackingReferenceComponent, TRLocation, TRRotation))
	{
		UE_LOG(LogMR, Warning, TEXT("Could not get player position"));
		return;
	}

	TROrientation = TRRotation.Quaternion();
	FinalPose = FPose(TROrientation, TRLocation) * CameraPose;

	FTransform FinalTransform(FinalPose.Orientation, FinalPose.Position);
	RootComponent->SetWorldTransform(FinalTransform);
	GetCaptureComponent2D()->FOVAngle = MRState->TrackedCamera.FieldOfView;

	if (ForegroundCaptureActor)
	{
		ForegroundCaptureActor->GetCaptureComponent2D()->FOVAngle = MRState->TrackedCamera.FieldOfView;
	}
}

void AOculusMR_CastingCameraActor::InitializeStates(UOculusMR_Settings* MRSettingsIn, UOculusMR_State* MRStateIn)
{
	MRSettings = MRSettingsIn;
	MRState = MRStateIn;
}

void AOculusMR_CastingCameraActor::SetupTrackedCamera()
{
	if (!RefreshExternalCamera())
	{
		return;
	}

	RequestTrackedCameraCalibration();

	// Unset this flag before we can return
	MRState->ChangeCameraStateRequested = false;

#if PLATFORM_WINDOWS
	// Set the plane mesh to the camera stream in direct composition or static background for external composition
	if (MRSettings->GetCompositionMethod() == EOculusMR_CompositionMethod::DirectComposition)
	{
		ovrpBool cameraOpen;
		if (OVRP_SUCCESS(FOculusHMDModule::GetPluginWrapper().HasCameraDeviceOpened2(MRState->CurrentCapturingCamera, &cameraOpen)) && cameraOpen)
		{
			UE_LOG(LogMR, Log, TEXT("Create CameraColorTexture (1280x720)"));
			CameraColorTexture = UTexture2D::CreateTransient(1280, 720);
			CameraColorTexture->UpdateResource();
			CameraDepthTexture = DefaultTexture_White;
		}
		else
		{
			MRState->CurrentCapturingCamera = ovrpCameraDevice_None;
			UE_LOG(LogMR, Error, TEXT("Unable to open CapturingCamera"));
			return;
		}

		SetupCameraFrameMaterialInstance();
	}
	else if (MRSettings->GetCompositionMethod() == EOculusMR_CompositionMethod::ExternalComposition)
	{
		SetupBackdropMaterialInstance();
	}

	RepositionPlaneMesh();
#endif
}

void AOculusMR_CastingCameraActor::SetupCameraFrameMaterialInstance()
{
	if (MRSettings->GetUseDynamicLighting())
	{
		if (!ChromaKeyLitMaterialInstance && ChromaKeyLitMaterial)
		{
			ChromaKeyLitMaterialInstance = UMaterialInstanceDynamic::Create(ChromaKeyLitMaterial, this);
		}
		CameraFrameMaterialInstance = ChromaKeyLitMaterialInstance;
	}
	else
	{
		if (!ChromaKeyMaterialInstance && ChromaKeyMaterial)
		{
			ChromaKeyMaterialInstance = UMaterialInstanceDynamic::Create(ChromaKeyMaterial, this);
		}
		CameraFrameMaterialInstance = ChromaKeyMaterialInstance;
	}

	PlaneMeshComponent->SetMaterial(0, CameraFrameMaterialInstance);

	if (CameraFrameMaterialInstance && CameraColorTexture)
	{
		CameraFrameMaterialInstance->SetTextureParameterValue(FName(TEXT("CameraCaptureTexture")), CameraColorTexture);
		CameraFrameMaterialInstance->SetVectorParameterValue(FName(TEXT("CameraCaptureTextureSize")),
			FLinearColor((float)CameraColorTexture->GetSizeX(), (float)CameraColorTexture->GetSizeY(), 1.0f / FMath::Max<int32>(1, CameraColorTexture->GetSizeX()), 1.0f / FMath::Max<int32>(1, CameraColorTexture->GetSizeY())));
		if (MRSettings->GetUseDynamicLighting())
		{
			CameraFrameMaterialInstance->SetTextureParameterValue(FName(TEXT("CameraDepthTexture")), CameraDepthTexture);
		}
	}
}

void AOculusMR_CastingCameraActor::SetBackdropMaterialColor()
{
	if (BackdropMaterialInstance)
	{
		BackdropMaterialInstance->SetVectorParameterValue(FName(TEXT("Color")), GetForegroundLayerBackgroundColor());
	}
}

void AOculusMR_CastingCameraActor::SetupBackdropMaterialInstance()
{
	if (!BackdropMaterialInstance && OpaqueColoredMaterial)
	{
		BackdropMaterialInstance = UMaterialInstanceDynamic::Create(OpaqueColoredMaterial, this);
		BackdropMaterialInstance->SetScalarParameterValue(FName("Opacity"), 0.0f);
	}
	PlaneMeshComponent->SetMaterial(0, BackdropMaterialInstance);
	SetBackdropMaterialColor();
}

void AOculusMR_CastingCameraActor::RepositionPlaneMesh()
{
	FVector PlaneCenter = FVector::ForwardVector * ForegroundMaxDistance;
	FVector PlaneUp = FVector::UpVector;
	FVector PlaneNormal = -FVector::ForwardVector;
	int ViewWidth = MRSettings->bUseTrackedCameraResolution ? MRState->TrackedCamera.SizeX : MRSettings->WidthPerView;
	int ViewHeight = MRSettings->bUseTrackedCameraResolution ? MRState->TrackedCamera.SizeY : MRSettings->HeightPerView;
	float Width = ForegroundMaxDistance * FMath::Tan(FMath::DegreesToRadians(GetCaptureComponent2D()->FOVAngle) * 0.5f) * 2.0f;
	float Height = Width * ViewHeight / ViewWidth;
	FVector2D PlaneSize = FVector2D(Width, Height);
	PlaneMeshComponent->Place(PlaneCenter, PlaneUp, PlaneNormal, PlaneSize);
	if (CameraFrameMaterialInstance && MRSettings->GetUseDynamicLighting())
	{
		float WidthInMeter = Width / GWorld->GetWorldSettings()->WorldToMeters;
		float HeightInMeter = Height / GWorld->GetWorldSettings()->WorldToMeters;
		CameraFrameMaterialInstance->SetVectorParameterValue(FName(TEXT("TextureWorldSize")), FLinearColor(WidthInMeter, HeightInMeter, 1.0f / WidthInMeter, 1.0f / HeightInMeter));
	}
	PlaneMeshComponent->ResetRelativeTransform();
	PlaneMeshComponent->SetVisibility(true);
}

void AOculusMR_CastingCameraActor::OnHMDRecentered()
{
#if PLATFORM_WINDOWS
	RefreshBoundaryMesh();
#endif
	RequestTrackedCameraCalibration();
}

void AOculusMR_CastingCameraActor::RefreshBoundaryMesh()
{
	RefreshBoundaryMeshCounter = 3;
}

void BuildProjectionMatrix(float YMultiplier, float FOV, float FarClipPlane, FMatrix& ProjectionMatrix)
{
	if (FarClipPlane < GNearClippingPlane)
	{
		FarClipPlane = GNearClippingPlane;
	}

	if ((int32)ERHIZBuffer::IsInverted)
	{
		ProjectionMatrix = FReversedZPerspectiveMatrix(
			FOV,
			FOV,
			1.0f,
			YMultiplier,
			GNearClippingPlane,
			FarClipPlane
		);
	}
	else
	{
		ProjectionMatrix = FPerspectiveMatrix(
			FOV,
			FOV,
			1.0f,
			YMultiplier,
			GNearClippingPlane,
			FarClipPlane
		);
	}
}

void AOculusMR_CastingCameraActor::UpdateRenderTargetSize()
{
	int ViewWidth = MRSettings->bUseTrackedCameraResolution ? MRState->TrackedCamera.SizeX : MRSettings->WidthPerView;
	int ViewHeight = MRSettings->bUseTrackedCameraResolution ? MRState->TrackedCamera.SizeY : MRSettings->HeightPerView;

#if PLATFORM_WINDOWS
	BackgroundRenderTargets[0]->ResizeTarget(ViewWidth, ViewHeight);
	if (ForegroundRenderTargets[0])
	{
		ForegroundRenderTargets[0]->ResizeTarget(ViewWidth, ViewHeight);
	}
#endif
#if PLATFORM_ANDROID
	FIntPoint CameraTargetSize = FIntPoint(ViewWidth, ViewHeight);
	float FOV = GetCaptureComponent2D()->FOVAngle * (float)PI / 360.0f;

	if (OVRP_SUCCESS(FOculusHMDModule::GetPluginWrapper().Media_GetMrcFrameSize(&ViewWidth, &ViewHeight)))
	{
		// Frame size is doublewide, so divide by 2
		ViewWidth /= 2;

		for (unsigned int i = 0; i < NumRTs; ++i)
		{
			BackgroundRenderTargets[i]->ResizeTarget(ViewWidth, ViewHeight);
			if (ForegroundRenderTargets[i])
			{
				ForegroundRenderTargets[i]->ResizeTarget(ViewWidth, ViewHeight);
			}
		}

		// Use custom projection matrix for far clip plane and to use camera aspect ratio instead of rendertarget aspect ratio
		float YMultiplier = (float)CameraTargetSize.X / (float)CameraTargetSize.Y;
		GetCaptureComponent2D()->bUseCustomProjectionMatrix = true;
		BuildProjectionMatrix(YMultiplier, FOV, GNearClippingPlane, GetCaptureComponent2D()->CustomProjectionMatrix);
		if (ForegroundCaptureActor)
		{
			ForegroundCaptureActor->GetCaptureComponent2D()->bUseCustomProjectionMatrix = true;
			BuildProjectionMatrix(YMultiplier, FOV, ForegroundMaxDistance, ForegroundCaptureActor->GetCaptureComponent2D()->CustomProjectionMatrix);
		}
	}
#endif
}

void AOculusMR_CastingCameraActor::SetupMRCScreen()
{
#if PLATFORM_WINDOWS
	OculusHMD::FSpectatorScreenController* SpecScreen = nullptr;
	IHeadMountedDisplay* HMD = GEngine->XRSystem.IsValid() ? GEngine->XRSystem->GetHMDDevice() : nullptr;
	if (HMD)
	{
		SpecScreen = (OculusHMD::FSpectatorScreenController*)HMD->GetSpectatorScreenController();
	}
	if (SpecScreen) {
#endif
		UpdateRenderTargetSize();

		GetCaptureComponent2D()->bDisableFlipCopyGLES = true;
		// LDR for gamma correction and post process
		GetCaptureComponent2D()->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

		// Render scene capture 2D output to spectator screen
		GetCaptureComponent2D()->TextureTarget = BackgroundRenderTargets[0];

#if PLATFORM_WINDOWS
		if (MRSettings->GetCompositionMethod() == EOculusMR_CompositionMethod::ExternalComposition)
#endif
		{
			ForegroundCaptureActor = GetWorld()->SpawnActor<ASceneCapture2D>();

			ForegroundCaptureActor->GetCaptureComponent2D()->bDisableFlipCopyGLES = true;
			// LDR for gamma correction and post process
			ForegroundCaptureActor->GetCaptureComponent2D()->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
#if PLATFORM_ANDROID
			// Start with foreground capture actor off on android
			ForegroundCaptureActor->GetCaptureComponent2D()->SetVisibility(false);
#endif

			// Don't render anything past the foreground for performance
			ForegroundCaptureActor->GetCaptureComponent2D()->MaxViewDistanceOverride = ForegroundMaxDistance;

			ForegroundCaptureActor->GetCaptureComponent2D()->TextureTarget = ForegroundRenderTargets[0];
#if PLATFORM_WINDOWS
			// Render use split foreground/background rendering to spectator screen
			SpecScreen->SetMRForeground(ForegroundRenderTargets[0]);
			SpecScreen->SetMRBackground(BackgroundRenderTargets[0]);
			SpecScreen->SetMRSpectatorScreenMode(OculusHMD::EMRSpectatorScreenMode::ExternalComposition);

			// Set the plane mesh to only render to foreground target
			PlaneMeshComponent->SetPlaneRenderTarget(ForegroundRenderTargets[0]);
#endif
			// Set foreground capture to match background capture
			ForegroundCaptureActor->AttachToActor(this, FAttachmentTransformRules(EAttachmentRule::SnapToTarget, true));
		}
#if PLATFORM_WINDOWS
		else if (MRSettings->GetCompositionMethod() == EOculusMR_CompositionMethod::DirectComposition)
		{
			SpecScreen->SetMRBackground(BackgroundRenderTargets[0]);
			SpecScreen->SetMRSpectatorScreenMode(OculusHMD::EMRSpectatorScreenMode::DirectComposition);
			// Set the plane mesh to only render to MRC capture target
			PlaneMeshComponent->SetPlaneRenderTarget(BackgroundRenderTargets[0]);

			if (ForegroundCaptureActor)
			{
				ForegroundCaptureActor->Destroy();
				ForegroundCaptureActor = nullptr;
			}
		}
	}
	else
	{
		UE_LOG(LogMR, Error, TEXT("Cannot find spectator screen"));
	}
#endif
}

void AOculusMR_CastingCameraActor::CloseMRCScreen()
{
#if PLATFORM_WINDOWS
	OculusHMD::FSpectatorScreenController* SpecScreen = nullptr;
	IHeadMountedDisplay* HMD = GEngine->XRSystem.IsValid() ? GEngine->XRSystem->GetHMDDevice() : nullptr;
	if (HMD)
	{
		SpecScreen = (OculusHMD::FSpectatorScreenController*)HMD->GetSpectatorScreenController();
	}
	// Restore original spectator screen mode
	if (SpecScreen) {
		SpecScreen->SetMRSpectatorScreenMode(OculusHMD::EMRSpectatorScreenMode::Default);
		SpecScreen->SetMRForeground(nullptr);
		SpecScreen->SetMRBackground(nullptr);
	}
#endif
	if (ForegroundCaptureActor)
	{
		ForegroundCaptureActor->Destroy();
		ForegroundCaptureActor = nullptr;
	}
}

void AOculusMR_CastingCameraActor::CloseTrackedCamera()
{
	if (PlaneMeshComponent) {
		PlaneMeshComponent->SetVisibility(false);
	}
	CameraFrameMaterialInstance = NULL;
}

#undef LOCTEXT_NAMESPACE
