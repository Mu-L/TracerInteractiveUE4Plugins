// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterRootActor.h"

#include "Components/SceneComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#include "HAL/IConsoleManager.h"

#include "Config/IPDisplayClusterConfigManager.h"
#include "Config/DisplayClusterConfigTypes.h"
#include "Misc/DisplayClusterAppExit.h"

#include "DisplayClusterRootComponent.h"
#include "DisplayClusterSceneComponentSyncParent.h"
#include "DisplayClusterPlayerInput.h"

#include "DisplayClusterGlobals.h"
#include "DisplayClusterLog.h"


ADisplayClusterRootActor::ADisplayClusterRootActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bExitOnEsc(true)
	, bShowProjectionScreens(false)
	, ProjectionScreensMaterial(nullptr)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterGame);

	// Root component
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));

	// DisplayCluster sync
	DisplayClusterRootComponent = CreateDefaultSubobject<UDisplayClusterRootComponent>(TEXT("DisplayClusterRoot"));
	DisplayClusterRootComponent->AttachToComponent(RootComponent, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = ETickingGroup::TG_PostUpdateWork;
	bFindCameraComponentWhenViewTarget = false;
	bReplicates = false;
	SpawnCollisionHandlingMethod = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
}

void ADisplayClusterRootActor::BeginPlay()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterGame);

	int32 NativeInputSyncPolicy = 0;

	// Store current operation mode
	OperationMode = GDisplayCluster->GetOperationMode();

	if (OperationMode == EDisplayClusterOperationMode::Cluster ||
		OperationMode == EDisplayClusterOperationMode::Editor)
	{
		// Read native input synchronization settings
		IPDisplayClusterConfigManager* const ConfigMgr = GDisplayCluster->GetPrivateConfigMgr();
		if (ConfigMgr)
		{
			FDisplayClusterConfigGeneral CfgGeneral = ConfigMgr->GetConfigGeneral();
			NativeInputSyncPolicy = CfgGeneral.NativeInputSyncPolicy;
			UE_LOG(LogDisplayClusterGame, Log, TEXT("Native input sync policy: %d"), NativeInputSyncPolicy);
		}
	}

	if (OperationMode == EDisplayClusterOperationMode::Cluster)
	{
		// Optionally activate native input synchronization
		if (NativeInputSyncPolicy == 1)
		{
			APlayerController* const PlayerController = GetWorld()->GetFirstPlayerController();
			if (PlayerController)
			{
				PlayerController->PlayerInput = NewObject<UDisplayClusterPlayerInput>(PlayerController);
			}
		}
	}

	Super::BeginPlay();
}

void ADisplayClusterRootActor::BeginDestroy()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterGame);

	Super::BeginDestroy();
}

void ADisplayClusterRootActor::Tick(float DeltaSeconds)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterGame);

	if (OperationMode == EDisplayClusterOperationMode::Cluster ||
		OperationMode == EDisplayClusterOperationMode::Editor)
	{
		UWorld* const CurWorld = GetWorld();
		if (CurWorld)
		{
			APlayerController* const CurPlayerController = CurWorld->GetFirstPlayerController();
			if (CurPlayerController)
			{
				APlayerCameraManager* const CurPlayerCameraManager = CurPlayerController->PlayerCameraManager;
				if (CurPlayerCameraManager)
				{
					SetActorLocationAndRotation(CurPlayerCameraManager->GetCameraLocation(), CurPlayerCameraManager->GetCameraRotation());
				}

				if (bExitOnEsc)
				{
					if (CurPlayerController->WasInputKeyJustPressed(EKeys::Escape))
					{
						FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::ExitType::NormalSoft, FString("Exit on ESC requested"));
					}
				}
			}
		}
	}

	// Show 'not supported' warning if instanced stereo is used
	if (OperationMode != EDisplayClusterOperationMode::Disabled)
	{
		static const TConsoleVariableData<int32>* const InstancedStereoCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("vr.InstancedStereo"));
		if (InstancedStereoCVar)
		{
			const bool bIsInstancedStereoRequested = (InstancedStereoCVar->GetValueOnGameThread() != 0);
			if (bIsInstancedStereoRequested)
			{
				UE_LOG(LogDisplayClusterGame, Error, TEXT("Instanced stereo was requested. nDisplay doesn't support instanced stereo so far."));
				GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::Red, TEXT("nDisplay doesn't support instanced stereo"));
			}
		}
	}

	Super::Tick(DeltaSeconds);
}
