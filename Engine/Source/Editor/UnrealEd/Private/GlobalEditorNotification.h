// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Notifications/GlobalNotification.h"
#include "Stats/Stats.h"
#include "TickableEditorObject.h"

/**
 * Class used to provide simple global editor notifications (for things like shader compilation and texture streaming) 
 */
class FGlobalEditorNotification : public FGlobalNotification, public FTickableEditorObject
{

public:
	FGlobalEditorNotification(const double InEnableDelayInSeconds = 1.0)
		: FGlobalNotification(InEnableDelayInSeconds)
	{
	}

	virtual ~FGlobalEditorNotification()
	{
	}

private:
	/** FTickableEditorObject interface */
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }
	virtual TStatId GetStatId() const override;
};
