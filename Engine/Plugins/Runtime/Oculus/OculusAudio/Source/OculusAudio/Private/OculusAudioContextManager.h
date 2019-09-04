// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioPluginUtilities.h"
#include "OVR_Audio.h"

class UActorComponent;
class FOculusAudioContextManager : public IAudioPluginListener
{
public:
	FOculusAudioContextManager();
	virtual ~FOculusAudioContextManager() override;

	virtual void OnListenerInitialize(FAudioDevice* AudioDevice, UWorld* ListenerWorld) override;
	virtual void OnListenerShutdown(FAudioDevice* AudioDevice) override;

	static ovrAudioContext GetOrCreateSerializationContext(UActorComponent* Parent);
private:
	// FIXME: can we do something better than global static variables?
	static ovrAudioContext SerializationContext;
	static UActorComponent* SerializationParent;
	ovrAudioContext Context;
};