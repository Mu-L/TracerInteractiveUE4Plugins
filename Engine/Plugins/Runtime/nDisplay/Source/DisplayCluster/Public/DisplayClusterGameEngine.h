// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/GameEngine.h"

#include "Config/DisplayClusterConfigTypes.h"
#include "DisplayClusterOperationMode.h"

#include "DisplayClusterGameEngine.generated.h"


class IPDisplayClusterClusterManager;
class IPDisplayClusterNodeController;
class IPDisplayClusterInputManager;
class IDisplayClusterClusterSyncObject;


/**
 * Extended game engine
 */
UCLASS()
class DISPLAYCLUSTER_API UDisplayClusterGameEngine
	: public UGameEngine
{
	GENERATED_BODY()

public:
	virtual void Init(class IEngineLoop* InEngineLoop) override;
	virtual void PreExit() override;
	virtual void Tick(float DeltaSeconds, bool bIdleMode) override;
	virtual bool LoadMap(FWorldContext& WorldContext, FURL URL, class UPendingNetGame* Pending, FString& Error) override;

	EDisplayClusterOperationMode GetOperationMode() const { return OperationMode; }

	void RegisterSyncObject(IDisplayClusterClusterSyncObject* SyncObj);
	void UnregisterSyncObject(IDisplayClusterClusterSyncObject* SyncObj);

	bool IsMaster() const;

protected:
	virtual bool InitializeInternals();
	EDisplayClusterOperationMode DetectOperationMode();

private:
	IPDisplayClusterClusterManager* ClusterMgr = nullptr;
	IPDisplayClusterNodeController* NodeController = nullptr;
	IPDisplayClusterInputManager*   InputMgr = nullptr;

	FDisplayClusterConfigDebug CfgDebug;
	EDisplayClusterOperationMode OperationMode = EDisplayClusterOperationMode::Disabled;
};
