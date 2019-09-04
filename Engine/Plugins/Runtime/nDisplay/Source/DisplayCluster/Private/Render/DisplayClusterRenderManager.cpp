// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/DisplayClusterRenderManager.h"
#include "Config/IPDisplayClusterConfigManager.h"

#include "Engine/GameViewportClient.h"
#include "Engine/GameEngine.h"

#include "DisplayClusterGlobals.h"
#include "DisplayClusterLog.h"
#include "DisplayClusterOperationMode.h"
#include "DisplayClusterStrings.h"

#include "Config/DisplayClusterConfigTypes.h"
#include "Config/IDisplayClusterConfigManager.h"

#include "Game/IDisplayClusterGameManager.h"

#include "Render/Device/DisplayClusterRenderDeviceFactoryInternal.h"
#include "Render/Device/DisplayClusterDeviceNativePresentHandler.h"
#include "Render/Device/Monoscopic/DisplayClusterDeviceMonoscopicDX11.h"

#include "Render/Device/IDisplayClusterRenderDeviceFactory.h"
#include "Render/PostProcess/IDisplayClusterPostProcess.h"
#include "Render/Projection/IDisplayClusterProjectionPolicyFactory.h"
#include "Render/Projection/IDisplayClusterProjectionPolicy.h"
#include "Render/Synchronization/IDisplayClusterRenderSyncPolicyFactory.h"

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyFactoryInternal.h"
#include "Render/Synchronization/DisplayClusterRenderSyncPolicyNone.h"
#include "Render/Synchronization/DisplayClusterRenderSyncPolicySoftwareGeneric.h"

#include "Misc/DisplayClusterHelpers.h"
#include "DisplayClusterUtils/DisplayClusterTypesConverter.h"

#include "UnrealClient.h"
#include "Kismet/GameplayStatics.h"


FDisplayClusterRenderManager::FDisplayClusterRenderManager()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	// Instantiate and register internal render device factory
	TSharedPtr<IDisplayClusterRenderDeviceFactory> NewRenderDeviceFactory(new FDisplayClusterRenderDeviceFactoryInternal);
	RegisterRenderDeviceFactory(DisplayClusterStrings::args::dev::Mono, NewRenderDeviceFactory);
	RegisterRenderDeviceFactory(DisplayClusterStrings::args::dev::QBS,  NewRenderDeviceFactory);
	RegisterRenderDeviceFactory(DisplayClusterStrings::args::dev::SbS,  NewRenderDeviceFactory);
	RegisterRenderDeviceFactory(DisplayClusterStrings::args::dev::TB,   NewRenderDeviceFactory);

	// Instantiate and register internal sync policy factory
	TSharedPtr<IDisplayClusterRenderSyncPolicyFactory> NewSyncPolicyFactory(new FDisplayClusterRenderSyncPolicyFactoryInternal);
	RegisterSynchronizationPolicyFactory(FString("0"), NewSyncPolicyFactory); // 0 - none
	RegisterSynchronizationPolicyFactory(FString("1"), NewSyncPolicyFactory); // 1 - network sync (soft sync)
	RegisterSynchronizationPolicyFactory(FString("2"), NewSyncPolicyFactory); // 2 - hardware sync (NVIDIA frame lock and swap sync)
}

FDisplayClusterRenderManager::~FDisplayClusterRenderManager()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayClusterManager
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterRenderManager::Init(EDisplayClusterOperationMode OperationMode)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	CurrentOperationMode = OperationMode;

	return true;
}

void FDisplayClusterRenderManager::Release()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	//@note: No need to release our RenderDevice. It will be released in a safe way by TSharedPtr.
}

bool FDisplayClusterRenderManager::StartSession(const FString& configPath, const FString& nodeId)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	ConfigPath = configPath;
	ClusterNodeId = nodeId;

	if (CurrentOperationMode == EDisplayClusterOperationMode::Disabled)
	{
		UE_LOG(LogDisplayClusterRender, Log, TEXT("Operation mode is 'Disabled' so no initialization will be performed"));
		return true;
	}

	// Create synchronization object
	UE_LOG(LogDisplayClusterRender, Log, TEXT("Instantiating synchronization policy object..."));
	SyncPolicy = CreateRenderSyncPolicy();

	// Instantiate render device
	UE_LOG(LogDisplayClusterRender, Log, TEXT("Instantiating stereo device..."));
	RenderDevice = CreateRenderDevice();

	// Set new device as the engine's stereoscopic device
	if (GEngine && RenderDevice.IsValid())
	{
		GEngine->StereoRenderingDevice = StaticCastSharedPtr<IStereoRendering>(RenderDevice);
	}

	// When session is starting in Editor the device won't be initialized so we avoid nullptr access here.
	//@todo Now we always have a device, even for Editor. Change the condition working on the EditorDevice.
	return (RenderDevice.IsValid() ? RenderDevice->Initialize() : true);
}

void FDisplayClusterRenderManager::EndSession()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

bool FDisplayClusterRenderManager::StartScene(UWorld* InWorld)
{
	if (RenderDevice.IsValid())
	{
		RenderDevice->InitializeWorldContent(InWorld);
	}

	return true;
}

void FDisplayClusterRenderManager::EndScene()
{
#if WITH_EDITOR
	if (GIsEditor)
	{
		// Since we can run multiple PIE sessions we have to clean device before the next one.
		GEngine->StereoRenderingDevice.Reset();
		RenderDevice.Reset();
	}
#endif
}

void FDisplayClusterRenderManager::PreTick(float DeltaSeconds)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	// Adjust position and size of game window to match window config.
	// This needs to happen after UGameEngine::SwitchGameWindowToUseGameViewport
	// is called. In practice that happens from FEngineLoop::Init after a call
	// to UGameEngine::Start - therefore this is done in PreTick on the first frame.
	if (!bWindowAdjusted)
	{
		bWindowAdjusted = true;

		//#ifdef  DISPLAY_CLUSTER_USE_DEBUG_STANDALONE_CONFIG
#if 0
		if (GDisplayCluster->GetPrivateConfigMgr()->IsRunningDebugAuto())
		{
			UE_LOG(LogDisplayClusterRender, Log, TEXT("Running in debug auto mode. Adjusting window..."));
			ResizeWindow(DisplayClusterConstants::misc::DebugAutoWinX, DisplayClusterConstants::misc::DebugAutoWinY, DisplayClusterConstants::misc::DebugAutoResX, DisplayClusterConstants::misc::DebugAutoResY);
			return;
		}
#endif

		if (FParse::Param(FCommandLine::Get(), TEXT("windowed")))
		{
			int32 WinX = 0;
			int32 WinY = 0;
			int32 ResX = 0;
			int32 ResY = 0;

			if (FParse::Value(FCommandLine::Get(), TEXT("WinX="), WinX) &&
				FParse::Value(FCommandLine::Get(), TEXT("WinY="), WinY) &&
				FParse::Value(FCommandLine::Get(), TEXT("ResX="), ResX) &&
				FParse::Value(FCommandLine::Get(), TEXT("ResY="), ResY))
			{
				ResizeWindow(WinX, WinY, ResX, ResY);
			}
			else
			{
				UE_LOG(LogDisplayClusterRender, Error, TEXT("Wrong window pos/size arguments"));
			}
		}
	}
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterRenderManager
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterRenderManager::RegisterRenderDeviceFactory(const FString& InDeviceType, TSharedPtr<IDisplayClusterRenderDeviceFactory>& InFactory)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	UE_LOG(LogDisplayClusterRender, Log, TEXT("Registering factory for rendering device type: %s"), *InDeviceType);

	if (!InFactory.IsValid())
	{
		UE_LOG(LogDisplayClusterRender, Warning, TEXT("Invalid factory object"));
		return false;
	}

	{
		FScopeLock lock(&CritSecInternals);

		if (RenderDeviceFactories.Contains(InDeviceType))
		{
			UE_LOG(LogDisplayClusterRender, Warning, TEXT("Setting a new factory for '%s' rendering device type"), *InDeviceType);
		}

		RenderDeviceFactories.Emplace(InDeviceType, InFactory);
	}

	UE_LOG(LogDisplayClusterRender, Log, TEXT("Registered factory for rendering device type: %s"), *InDeviceType);

	return true;
}

bool FDisplayClusterRenderManager::UnregisterRenderDeviceFactory(const FString& InDeviceType)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	UE_LOG(LogDisplayClusterRender, Log, TEXT("Unregistering factory for rendering device type: %s"), *InDeviceType);

	{
		FScopeLock lock(&CritSecInternals);

		if (!RenderDeviceFactories.Contains(InDeviceType))
		{
			UE_LOG(LogDisplayClusterRender, Warning, TEXT("A factory for '%s' rendering device type not found"), *InDeviceType);
			return false;
		}

		RenderDeviceFactories.Remove(InDeviceType);
	}

	UE_LOG(LogDisplayClusterRender, Log, TEXT("Unregistered factory for rendering device type: %s"), *InDeviceType);

	return true;
}

bool FDisplayClusterRenderManager::RegisterSynchronizationPolicyFactory(const FString& InSyncPolicyType, TSharedPtr<IDisplayClusterRenderSyncPolicyFactory>& InFactory)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	UE_LOG(LogDisplayClusterRender, Log, TEXT("Registering factory for synchronization policy: %s"), *InSyncPolicyType);

	if (!InFactory.IsValid())
	{
		UE_LOG(LogDisplayClusterRender, Warning, TEXT("Invalid factory object"));
		return false;
	}

	{
		FScopeLock lock(&CritSecInternals);

		if (SyncPolicyFactories.Contains(InSyncPolicyType))
		{
			UE_LOG(LogDisplayClusterRender, Warning, TEXT("A new factory for '%s' synchronization policy was set"), *InSyncPolicyType);
		}

		SyncPolicyFactories.Emplace(InSyncPolicyType, InFactory);
	}

	UE_LOG(LogDisplayClusterRender, Log, TEXT("Registered factory for syncrhonization policy: %s"), *InSyncPolicyType);

	return true;
}

bool FDisplayClusterRenderManager::UnregisterSynchronizationPolicyFactory(const FString& InSyncPolicyType)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	UE_LOG(LogDisplayClusterRender, Log, TEXT("Unregistering factory for syncrhonization policy: %s"), *InSyncPolicyType);

	{
		FScopeLock lock(&CritSecInternals);

		if (!SyncPolicyFactories.Contains(InSyncPolicyType))
		{
			UE_LOG(LogDisplayClusterRender, Warning, TEXT("A factory for '%s' syncrhonization policy not found"), *InSyncPolicyType);
			return false;
		}

		SyncPolicyFactories.Remove(InSyncPolicyType);
	}

	UE_LOG(LogDisplayClusterRender, Log, TEXT("Unregistered factory for syncrhonization policy: %s"), *InSyncPolicyType);

	return true;
}

TSharedPtr<IDisplayClusterRenderSyncPolicy> FDisplayClusterRenderManager::GetCurrentSynchronizationPolicy()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
	FScopeLock lock(&CritSecInternals);
	return SyncPolicy;
}

bool FDisplayClusterRenderManager::RegisterProjectionPolicyFactory(const FString& InProjectionType, TSharedPtr<IDisplayClusterProjectionPolicyFactory>& InFactory)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	UE_LOG(LogDisplayClusterRender, Log, TEXT("Registering factory for projection type: %s"), *InProjectionType);

	if (!InFactory.IsValid())
	{
		UE_LOG(LogDisplayClusterRender, Warning, TEXT("Invalid factory object"));
		return false;
	}

	{
		FScopeLock lock(&CritSecInternals);

		if (ProjectionPolicyFactories.Contains(InProjectionType))
		{
			UE_LOG(LogDisplayClusterRender, Warning, TEXT("A new factory for '%s' projection policy was set"), *InProjectionType);
		}

		ProjectionPolicyFactories.Emplace(InProjectionType, InFactory);
	}

	UE_LOG(LogDisplayClusterRender, Log, TEXT("Registered factory for projection type: %s"), *InProjectionType);

	return true;
}

bool FDisplayClusterRenderManager::UnregisterProjectionPolicyFactory(const FString& InProjectionType)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	UE_LOG(LogDisplayClusterRender, Log, TEXT("Unregistering factory for projection policy: %s"), *InProjectionType);

	{
		FScopeLock lock(&CritSecInternals);

		if (!ProjectionPolicyFactories.Contains(InProjectionType))
		{
			UE_LOG(LogDisplayClusterRender, Warning, TEXT("A handler for '%s' projection type not found"), *InProjectionType);
			return false;
		}

		ProjectionPolicyFactories.Remove(InProjectionType);
	}

	UE_LOG(LogDisplayClusterRender, Log, TEXT("Unregistered factory for projection policy: %s"), *InProjectionType);

	return true;
}

TSharedPtr<IDisplayClusterProjectionPolicyFactory> FDisplayClusterRenderManager::GetProjectionPolicyFactory(const FString& InProjectionType)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	FScopeLock lock(&CritSecInternals);

	if (ProjectionPolicyFactories.Contains(InProjectionType))
	{
		return ProjectionPolicyFactories[InProjectionType];
	}

	UE_LOG(LogDisplayClusterRender, Warning, TEXT("No factory found for projection policy: %s"), *InProjectionType);

	return nullptr;
}

bool FDisplayClusterRenderManager::RegisterPostprocessOperation(const FString& InName, TSharedPtr<IDisplayClusterPostProcess>& InOperation, int InPriority /* = 0 */)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
	IDisplayClusterRenderManager::FDisplayClusterPPInfo PPInfo(InOperation, InPriority);
	return RegisterPostprocessOperation(InName, PPInfo);
}

bool FDisplayClusterRenderManager::RegisterPostprocessOperation(const FString& InName, IPDisplayClusterRenderManager::FDisplayClusterPPInfo& InPPInfo)
{
	UE_LOG(LogDisplayClusterRender, Log, TEXT("Registering post-process operation: %s"), *InName);

	if (!InPPInfo.Operation.IsValid())
	{
		UE_LOG(LogDisplayClusterRender, Warning, TEXT("Trying to set invalid post-process operation"));
		return false;
	}

	if (InName.IsEmpty())
	{
		UE_LOG(LogDisplayClusterRender, Warning, TEXT("Invalid name of a post-process operation"));
		return false;
	}

	{
		FScopeLock lock(&CritSecInternals);

		if (PostProcessOperations.Contains(InName))
		{
			UE_LOG(LogDisplayClusterRender, Warning, TEXT("Post-process operation '%s' exists"), *InName);
			return false;
		}

		// Store new operation
		PostProcessOperations.Emplace(InName, InPPInfo);

		// Sort operations by priority
		PostProcessOperations.ValueSort([](const FDisplayClusterPPInfo& Left, const FDisplayClusterPPInfo& Right)
		{
			return Left.Priority < Right.Priority;
		});
	}

	UE_LOG(LogDisplayClusterRender, Log, TEXT("Registered post-process operation: %s"), *InName);

	return true;
}

bool FDisplayClusterRenderManager::UnregisterPostprocessOperation(const FString& InName)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	UE_LOG(LogDisplayClusterRender, Log, TEXT("Unregistering post-process operation: %s"), *InName);

	{
		FScopeLock lock(&CritSecInternals);

		if (!PostProcessOperations.Contains(InName))
		{
			UE_LOG(LogDisplayClusterRender, Warning, TEXT("Post-process operation <%s> not found"), *InName);
			return false;
		}
		else
		{
			PostProcessOperations.Remove(InName);
		}
	}

	UE_LOG(LogDisplayClusterRender, Log, TEXT("Unregistered post-process operation: %s"), *InName);

	return true;
}

TMap<FString, IPDisplayClusterRenderManager::FDisplayClusterPPInfo> FDisplayClusterRenderManager::GetRegisteredPostprocessOperations() const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
	FScopeLock lock(&CritSecInternals);
	return PostProcessOperations;
}

void FDisplayClusterRenderManager::SetViewportCamera(const FString& InCameraId /* = FString() */, const FString& InViewportId /* = FString() */)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
	check(IsInGameThread());

	{
		FScopeLock lock(&CritSecInternals);
		if (RenderDevice.IsValid())
		{
			RenderDevice->SetViewportCamera(InCameraId, InViewportId);
		}
	}
}

bool FDisplayClusterRenderManager::GetViewportRect(const FString& InViewportID, FIntRect& Rect)
{
	if (!RenderDevice.IsValid())
	{
		return false;
	}

	return RenderDevice->GetViewportRect(InViewportID, Rect);
}

void FDisplayClusterRenderManager::SetStartPostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& StartPostProcessingSettings)
{
	if (!RenderDevice.IsValid())
	{
		return;
	}

	RenderDevice->SetStartPostProcessingSettings(ViewportID, StartPostProcessingSettings);
}

void FDisplayClusterRenderManager::SetOverridePostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& OverridePostProcessingSettings, float BlendWeight)
{
	if (!RenderDevice.IsValid())
	{
		return;
	}

	RenderDevice->SetOverridePostProcessingSettings(ViewportID, OverridePostProcessingSettings, BlendWeight);
}

void FDisplayClusterRenderManager::SetFinalPostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& FinalPostProcessingSettings)
{
	if (!RenderDevice.IsValid())
	{
		return;
	}

	RenderDevice->SetFinalPostProcessingSettings(ViewportID, FinalPostProcessingSettings);
}

void FDisplayClusterRenderManager::SetInterpupillaryDistance(const FString& CameraId, float EyeDistance)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
	check(IsInGameThread());

	UDisplayClusterCameraComponent* const Camera = DisplayClusterHelpers::game::GetCamera(CameraId);
	if (Camera)
	{
		Camera->SetInterpupillaryDistance(EyeDistance);
	}
}

float FDisplayClusterRenderManager::GetInterpupillaryDistance(const FString& CameraId) const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
	check(IsInGameThread());

	UDisplayClusterCameraComponent* const Camera = DisplayClusterHelpers::game::GetCamera(CameraId);
	return (Camera ? Camera->GetInterpupillaryDistance() : 0.f);
}

void FDisplayClusterRenderManager::SetEyesSwap(const FString& CameraId, bool EyeSwapped)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
	check(IsInGameThread());

	UDisplayClusterCameraComponent* const Camera = DisplayClusterHelpers::game::GetCamera(CameraId);
	if (Camera)
	{
		Camera->SetEyesSwap(EyeSwapped);
	}
}

bool FDisplayClusterRenderManager::GetEyesSwap(const FString& CameraId) const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
	check(IsInGameThread());

	UDisplayClusterCameraComponent* const Camera = DisplayClusterHelpers::game::GetCamera(CameraId);
	return (Camera ? Camera->GetEyesSwap() : false);
}

bool FDisplayClusterRenderManager::ToggleEyesSwap(const FString& CameraId)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
	check(IsInGameThread());

	UDisplayClusterCameraComponent* const Camera = DisplayClusterHelpers::game::GetCamera(CameraId);
	return (Camera ? Camera->ToggleEyesSwap() : false);
}

float FDisplayClusterRenderManager::GetNearCullingDistance(const FString& CameraId) const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
	check(IsInGameThread());

	UDisplayClusterCameraComponent* const Camera = DisplayClusterHelpers::game::GetCamera(CameraId);
	return (Camera ? Camera->GetNearCullingDistance() : 0.f);
}

void FDisplayClusterRenderManager::SetNearCullingDistance(const FString& CameraId, float NearDistance)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
	check(IsInGameThread());

	UDisplayClusterCameraComponent* const Camera = DisplayClusterHelpers::game::GetCamera(CameraId);
	if (Camera)
	{
		Camera->SetNearCullingDistance(NearDistance);
	}
}

float FDisplayClusterRenderManager::GetFarCullingDistance(const FString& CameraId) const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
	check(IsInGameThread());

	UDisplayClusterCameraComponent* const Camera = DisplayClusterHelpers::game::GetCamera(CameraId);
	return (Camera ? Camera->GetFarCullingDistance() : 0.f);
}

void FDisplayClusterRenderManager::SetFarCullingDistance(const FString& CameraId, float FarDistance)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
	check(IsInGameThread());

	UDisplayClusterCameraComponent* const Camera = DisplayClusterHelpers::game::GetCamera(CameraId);
	if (Camera)
	{
		Camera->SetFarCullingDistance(FarDistance);
	}
}

void FDisplayClusterRenderManager::GetCullingDistance(const FString& CameraId, float& NearDistance, float& FarDistance) const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
	check(IsInGameThread());

	UDisplayClusterCameraComponent* const Camera = DisplayClusterHelpers::game::GetCamera(CameraId);
	if (Camera)
	{
		Camera->GetCullingDistance(NearDistance, FarDistance);
	}
}

void FDisplayClusterRenderManager::SetCullingDistance(const FString& CameraId, float NearDistance, float FarDistance)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
	check(IsInGameThread());

	UDisplayClusterCameraComponent* const Camera = DisplayClusterHelpers::game::GetCamera(CameraId);
	if (Camera)
	{
		Camera->SetCullingDistance(NearDistance, FarDistance);
	}
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterRenderManager
//////////////////////////////////////////////////////////////////////////////////////////////
TSharedPtr<IDisplayClusterRenderDevice, ESPMode::ThreadSafe> FDisplayClusterRenderManager::CreateRenderDevice() const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	TSharedPtr<IDisplayClusterRenderDevice, ESPMode::ThreadSafe> NewRenderDevice;

	if (CurrentOperationMode == EDisplayClusterOperationMode::Cluster || CurrentOperationMode == EDisplayClusterOperationMode::Standalone)
	{
		if (GDynamicRHI == nullptr)
		{
			UE_LOG(LogDisplayClusterRender, Error, TEXT("GDynamicRHI is null. Cannot detect RHI name."));
			return nullptr;
		}

		// Runtime RHI
		const FString RHIName = GDynamicRHI->GetName();

		// Monoscopic
		if (FParse::Param(FCommandLine::Get(), DisplayClusterStrings::args::dev::Mono))
		{
			NewRenderDevice = RenderDeviceFactories[DisplayClusterStrings::args::dev::Mono]->Create(DisplayClusterStrings::args::dev::Mono, RHIName);
		}
		// Quad buffer stereo
		else if (FParse::Param(FCommandLine::Get(), DisplayClusterStrings::args::dev::QBS))
		{
			NewRenderDevice = RenderDeviceFactories[DisplayClusterStrings::args::dev::QBS]->Create(DisplayClusterStrings::args::dev::QBS, RHIName);
		}
		// Side-by-side
		else if (FParse::Param(FCommandLine::Get(), DisplayClusterStrings::args::dev::SbS))
		{
			NewRenderDevice = RenderDeviceFactories[DisplayClusterStrings::args::dev::SbS]->Create(DisplayClusterStrings::args::dev::SbS, RHIName);
		}
		// Top-bottom
		else if (FParse::Param(FCommandLine::Get(), DisplayClusterStrings::args::dev::TB))
		{
			NewRenderDevice = RenderDeviceFactories[DisplayClusterStrings::args::dev::TB]->Create(DisplayClusterStrings::args::dev::TB, RHIName);
		}
		// Leave native render but inject custom present for cluster synchronization
		else
		{
			UE_LOG(LogDisplayClusterRender, Log, TEXT("A native present handler will be instantiated when viewport is available"));
			UGameViewportClient::OnViewportCreated().AddRaw(this, &FDisplayClusterRenderManager::OnViewportCreatedHandler);
		}
	}
	else if (CurrentOperationMode == EDisplayClusterOperationMode::Editor)
	{
		UE_LOG(LogDisplayClusterRender, Log, TEXT("Instantiating DX11 mono device for PIE"));
		NewRenderDevice = MakeShareable(new FDisplayClusterDeviceMonoscopicDX11());
	}
	else if (CurrentOperationMode == EDisplayClusterOperationMode::Disabled)
	{
		// Stereo device is not needed
		UE_LOG(LogDisplayClusterRender, Log, TEXT("No need to instantiate stereo device"));
	}
	else
	{
		UE_LOG(LogDisplayClusterRender, Warning, TEXT("Unknown operation mode"));
	}

	if (!NewRenderDevice.IsValid())
	{
		UE_LOG(LogDisplayClusterRender, Log, TEXT("No stereo device created"));
	}

	return NewRenderDevice;
}

TSharedPtr<IDisplayClusterRenderSyncPolicy> FDisplayClusterRenderManager::CreateRenderSyncPolicy() const
{
	if (CurrentOperationMode != EDisplayClusterOperationMode::Cluster && CurrentOperationMode != EDisplayClusterOperationMode::Standalone)
	{
		UE_LOG(LogDisplayClusterRender, Warning, TEXT("Synchronization policy is not available for the current operation mode"));
		return nullptr;
	}

	if (GDynamicRHI == nullptr)
	{
		UE_LOG(LogDisplayClusterRender, Error, TEXT("GDynamicRHI is null. Cannot detect RHI name."));
		return nullptr;
	}

	// Create sync policy specified in a config file
	FDisplayClusterConfigGeneral CfgGeneral = GDisplayCluster->GetPrivateConfigMgr()->GetConfigGeneral();
	const FString SyncPolicyType = FDisplayClusterTypesConverter::ToString(CfgGeneral.SwapSyncPolicy);
	const FString RHIName = GDynamicRHI->GetName();
	TSharedPtr<IDisplayClusterRenderSyncPolicy> NewSyncPolicy;

	{
		if (SyncPolicyFactories.Contains(SyncPolicyType))
		{
			UE_LOG(LogDisplayClusterRender, Log, TEXT("A factory for the requested synchronization policy <%s> was found"), *SyncPolicyType);
			NewSyncPolicy = SyncPolicyFactories[SyncPolicyType]->Create(SyncPolicyType, RHIName);
		}
		else
		{
			UE_LOG(LogDisplayClusterRender, Log, TEXT("No factory found for the requested synchronization policy <%s>. Using fallback 'None' policy."), *SyncPolicyType);
			NewSyncPolicy = MakeShareable(new FDisplayClusterRenderSyncPolicyNone);
		}
	}

	// Fallback sync policy
	if (!NewSyncPolicy.IsValid())
	{
		UE_LOG(LogDisplayClusterRender, Log, TEXT("No factory found for the requested synchronization policy <%s>. Using fallback 'None' policy."), *SyncPolicyType);
		NewSyncPolicy = MakeShareable(new FDisplayClusterRenderSyncPolicySoftwareGeneric);
	}

	return NewSyncPolicy;
}

void FDisplayClusterRenderManager::ResizeWindow(int32 WinX, int32 WinY, int32 ResX, int32 ResY)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	UGameEngine* engine = Cast<UGameEngine>(GEngine);
	TSharedPtr<SWindow> window = engine->GameViewportWindow.Pin();
	check(window.IsValid());

	UE_LOG(LogDisplayClusterRender, Log, TEXT("Adjusting game window: pos [%d, %d],  size [%d x %d]"), WinX, WinY, ResX, ResY);

	// Adjust window position/size
	window->ReshapeWindow(FVector2D(WinX, WinY), FVector2D(ResX, ResY));
}

void FDisplayClusterRenderManager::OnViewportCreatedHandler()
{
	if (GEngine && GEngine->GameViewport)
	{
		if (!GEngine->GameViewport->Viewport->GetViewportRHI().IsValid())
		{
			GEngine->GameViewport->OnBeginDraw().AddRaw(this, &FDisplayClusterRenderManager::OnBeginDrawHandler);
		}
	}
}

void FDisplayClusterRenderManager::OnBeginDrawHandler()
{
	//@todo: this is fast solution for prototype. We shouldn't use raw handlers to be able to unsubscribe from the event.
	static bool initialized = false;
	if (!initialized && GEngine->GameViewport->Viewport->GetViewportRHI().IsValid())
	{
		NativePresentHandler = new FDisplayClusterDeviceNativePresentHandler();
		GEngine->GameViewport->Viewport->GetViewportRHI().GetReference()->SetCustomPresent(NativePresentHandler);
		initialized = true;
	}
}
