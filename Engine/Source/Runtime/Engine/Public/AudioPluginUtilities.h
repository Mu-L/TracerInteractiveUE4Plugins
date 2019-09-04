// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once
#include "IAudioExtensionPlugin.h"

/************************************************************************/
/* Plugin Utilities                                                     */
/************************************************************************/
struct ENGINE_API AudioPluginUtilities
{

#if PLATFORM_WINDOWS
	static const EAudioPlatform CurrentPlatform = EAudioPlatform::Windows;
#elif PLATFORM_MAC
	static const EAudioPlatform CurrentPlatform = EAudioPlatform::Mac;
#elif PLATFORM_UNIX
	static const EAudioPlatform CurrentPlatform = EAudioPlatform::Linux;
#elif PLATFORM_IOS
	static const EAudioPlatform CurrentPlatform = EAudioPlatform::IOS;
//Lumin needs to be before Android because it derives off of it, therefore defines PLATFORM_ANDROID=1
#elif PLATFORM_LUMIN
	static const EAudioPlatform CurrentPlatform = EAudioPlatform::Lumin;
#elif PLATFORM_ANDROID
	static const EAudioPlatform CurrentPlatform = EAudioPlatform::Android;
#elif PLATFORM_XBOXONE
	static const EAudioPlatform CurrentPlatform = EAudioPlatform::XboxOne;
#elif PLATFORM_PS4
	static const EAudioPlatform CurrentPlatform = EAudioPlatform::Playstation4;
#elif PLATFORM_SWITCH
	static const EAudioPlatform CurrentPlatform = EAudioPlatform::Switch;
#elif PLATFORM_HTML5
	static const EAudioPlatform CurrentPlatform = EAudioPlatform::HTML5;
#elif PLATFORM_HOLOLENS
	static const EAudioPlatform CurrentPlatform = EAudioPlatform::HoloLens;
#else
	static const EAudioPlatform CurrentPlatform = EAudioPlatform::Unknown;
#endif

	/** Platform config section for each platform's target settings. */
	static const TCHAR* GetPlatformConfigSection(EAudioPlatform AudioPlatform);

	/*
	 * These functions return a pointer to the plugin factory
	 * that matches the plugin name specified in the target
	 * platform's settings.
	 *
	 * if no matching plugin is found, nullptr is returned.
	 */
	static IAudioSpatializationFactory* GetDesiredSpatializationPlugin(EAudioPlatform AudioPlatform);
	static IAudioReverbFactory* GetDesiredReverbPlugin(EAudioPlatform AudioPlatform);
	static IAudioOcclusionFactory* GetDesiredOcclusionPlugin(EAudioPlatform AudioPlatform);
	static IAudioModulationFactory* GetDesiredModulationPlugin(EAudioPlatform AudioPlatform);

	/** This function returns the name of the plugin specified in the platform settings. */
	static FString GetDesiredPluginName(EAudioPlugin PluginType, EAudioPlatform AudioPlatform);
};