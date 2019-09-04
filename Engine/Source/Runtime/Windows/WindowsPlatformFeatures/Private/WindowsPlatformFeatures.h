// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PlatformFeatures.h"
#include "WindowsPlatformFeaturesCommon.h"

class FWindowsPlatformFeaturesModule : public IPlatformFeaturesModule
{
public:

	/** Creates a new instance of the audio device implemented by the module. */
	FWindowsPlatformFeaturesModule();

	virtual IVideoRecordingSystem* GetVideoRecordingSystem() override;

private:
	/**
	 * Load global/generic modules, and perform any initialization
	 */
	bool StartupModules();

};


