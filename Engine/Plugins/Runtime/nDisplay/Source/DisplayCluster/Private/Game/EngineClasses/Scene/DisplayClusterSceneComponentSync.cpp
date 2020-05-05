// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterSceneComponentSync.h"

#include "GameFramework/Actor.h"

#include "Cluster/IPDisplayClusterClusterManager.h"
#include "Game/IPDisplayClusterGameManager.h"

#include "DisplayClusterUtils/DisplayClusterTypesConverter.h"
#include "DisplayClusterGlobals.h"
#include "DisplayClusterLog.h"


UDisplayClusterSceneComponentSync::UDisplayClusterSceneComponentSync(const FObjectInitializer& ObjectInitializer) :
	USceneComponent(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UDisplayClusterSceneComponentSync::BeginPlay()
{
	Super::BeginPlay();

	if (!GDisplayCluster->IsModuleInitialized())
	{
		return;
	}

	// Generate unique sync id
	SyncId = GenerateSyncId();

	GameMgr = GDisplayCluster->GetPrivateGameMgr();
	if (GameMgr && GameMgr->IsDisplayClusterActive())
	{
		// Register sync object
		ClusterMgr = GDisplayCluster->GetPrivateClusterMgr();
		if (ClusterMgr)
		{
			UE_LOG(LogDisplayClusterGame, Log, TEXT("Registering sync object %s..."), *SyncId);
			ClusterMgr->RegisterSyncObject(this, EDisplayClusterSyncGroup::Tick);
		}
		else
		{
			UE_LOG(LogDisplayClusterGame, Warning, TEXT("Couldn't register %s scene component sync. Looks like we're in non-DisplayCluster mode."), *SyncId);
		}
	}
}

void UDisplayClusterSceneComponentSync::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GDisplayCluster->IsModuleInitialized())
	{
		if (GameMgr && GameMgr->IsDisplayClusterActive())
		{
			if (ClusterMgr)
			{
				UE_LOG(LogDisplayClusterGame, Log, TEXT("Unregistering sync object %s..."), *SyncId);
				ClusterMgr->UnregisterSyncObject(this);
			}
		}
	}

	Super::EndPlay(EndPlayReason);
}

void UDisplayClusterSceneComponentSync::TickComponent( float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction )
{
	Super::TickComponent( DeltaTime, TickType, ThisTickFunction );

	// ...
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterClusterSyncObject
//////////////////////////////////////////////////////////////////////////////////////////////
bool UDisplayClusterSceneComponentSync::IsActive() const
{
	return !this->IsPendingKill();
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterStringSerializable
//////////////////////////////////////////////////////////////////////////////////////////////
FString UDisplayClusterSceneComponentSync::GenerateSyncId()
{
	return FString::Printf(TEXT("S_%s"), *GetOwner()->GetName());
}

FString UDisplayClusterSceneComponentSync::SerializeToString() const
{
	return FDisplayClusterTypesConverter::template ToHexString(GetSyncTransform());
}

bool UDisplayClusterSceneComponentSync::DeserializeFromString(const FString& data)
{
	FTransform NewTransform = FDisplayClusterTypesConverter::template FromHexString<FTransform>(data);
	UE_LOG(LogDisplayClusterGame, Verbose, TEXT("%s: applying transform data <%s>"), *SyncId, *NewTransform.ToHumanReadableString());
	SetSyncTransform(NewTransform);

	return true;
}
