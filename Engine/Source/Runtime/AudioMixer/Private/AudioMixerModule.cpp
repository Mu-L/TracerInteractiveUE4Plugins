// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AudioMixerModule.h"
#include "Modules/ModuleManager.h"
#include "AudioMixerLog.h"

DEFINE_LOG_CATEGORY(LogAudioMixer);
DEFINE_LOG_CATEGORY(LogAudioMixerDebug);

class FAudioMixerModule : public IModuleInterface
{
public:

	virtual void StartupModule() override
	{
	}
};

IMPLEMENT_MODULE(FAudioMixerModule, AudioMixer);
