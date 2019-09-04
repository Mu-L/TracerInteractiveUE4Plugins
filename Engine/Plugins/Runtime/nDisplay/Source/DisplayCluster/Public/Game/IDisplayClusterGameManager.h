// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DisplayClusterCameraComponent.h"
#include "DisplayClusterScreenComponent.h"
#include "DisplayClusterPawn.h"


/**
 * Public game manager interface
 */
class IDisplayClusterGameManager
{
public:
	virtual ~IDisplayClusterGameManager() = 0
	{ }

	virtual ADisplayClusterPawn*                    GetRoot() const = 0;

	virtual TArray<UDisplayClusterScreenComponent*> GetAllScreens() const = 0;
	virtual UDisplayClusterScreenComponent*         GetScreenById(const FString& id) const = 0;
	virtual int32                                   GetScreensAmount() const = 0;

	virtual TArray<UDisplayClusterCameraComponent*> GetAllCameras() const = 0;
	virtual UDisplayClusterCameraComponent*         GetCameraById(const FString& id) const = 0;
	virtual int32                                   GetCamerasAmount() const = 0;
	virtual UDisplayClusterCameraComponent*         GetDefaultCamera() const = 0;
	virtual void                                    SetDefaultCamera(int32 idx) = 0;
	virtual void                                    SetDefaultCamera(const FString& id) = 0;

	virtual TArray<UDisplayClusterSceneComponent*>  GetAllNodes() const = 0;
	virtual UDisplayClusterSceneComponent*          GetNodeById(const FString& id) const = 0;

	virtual USceneComponent*                        GetTranslationDirectionComponent() const = 0;
	virtual void                                    SetTranslationDirectionComponent(USceneComponent* const pComp) = 0;
	virtual void                                    SetTranslationDirectionComponent(const FString& id) = 0;

	virtual USceneComponent*                        GetRotateAroundComponent() const = 0;
	virtual void                                    SetRotateAroundComponent(USceneComponent* const pComp) = 0;
	virtual void                                    SetRotateAroundComponent(const FString& id) = 0;
};
