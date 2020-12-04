// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Config/IPDisplayClusterConfigManager.h"
#include "DisplayClusterConfigurationTypes.h"
#include "UObject/StrongObjectPtr.h"


/**
 * Config manager. Responsible for loading data from config file and providing with it to any other classes.
 */
class FDisplayClusterConfigManager
	: public IPDisplayClusterConfigManager
{
public:
	FDisplayClusterConfigManager() = default;
	virtual ~FDisplayClusterConfigManager() = default;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPDisplayClusterManager
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Init(EDisplayClusterOperationMode OperationMode) override;
	virtual void Release() override;
	virtual bool StartSession(const UDisplayClusterConfigurationData* InConfigData, const FString& NodeId) override;
	virtual void EndSession() override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterConfigManager
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual const UDisplayClusterConfigurationData* GetConfig() const override
	{
		return ConfigData.Get();
	}

	virtual FString GetConfigPath() const override
	{
		return ConfigData.IsValid() ? ConfigData.Get()->Meta.FilePath : FString();
	}

	virtual FString GetLocalNodeId() const override
	{
		return ClusterNodeId; 
	}

	virtual FString GetMasterNodeId() const override;

	virtual const UDisplayClusterConfigurationClusterNode* GetMasterNode() const override;
	virtual const UDisplayClusterConfigurationClusterNode* GetLocalNode() const override;
	virtual const UDisplayClusterConfigurationViewport*    GetLocalViewport(const FString& ViewportId) const override;

	virtual bool GetLocalPostprocess(const FString& PostprocessId, FDisplayClusterConfigurationPostprocess& OutPostprocess) const override;
	virtual bool GetLocalProjection(const FString& ViewportId, FDisplayClusterConfigurationProjection& OutProjection) const override;

private:
	FString ClusterNodeId;

	TStrongObjectPtr<const UDisplayClusterConfigurationData> ConfigData;
};
