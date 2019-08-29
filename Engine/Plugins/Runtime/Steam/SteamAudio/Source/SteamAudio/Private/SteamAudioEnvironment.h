//
// Copyright (C) Valve Corporation. All rights reserved.
//

#pragma once

#include "ISteamAudioModule.h"
#include "phonon.h"
#include "PhononCommon.h"
#include "PhononScene.h"

namespace SteamAudio
{
	/************************************************************************/
	/* FEnvironment                                                         */
	/* This class handles an instance of the Steam Audio environment,       */
	/* as well as the environmental renderer used by the audio plugins.     */
	/************************************************************************/
	class FEnvironment
	{
	public:
		FEnvironment();
		~FEnvironment();

		IPLhandle Initialize(UWorld* World, FAudioDevice* InAudioDevice);
		void Shutdown();

		IPLhandle GetSceneHandle() { return PhononScene; };

		IPLhandle GetEnvironmentHandle() { return PhononEnvironment; };

		IPLhandle GetEnvironmentalRendererHandle() { return EnvironmentalRenderer; };

		FCriticalSection* GetEnvironmentCriticalSection() { return &EnvironmentCriticalSection; };

	private:
		FCriticalSection EnvironmentCriticalSection;
		IPLhandle ComputeDevice;
		IPLhandle PhononScene;
		FPhononSceneInfo PhononSceneInfo;
		IPLhandle PhononEnvironment;
		IPLhandle EnvironmentalRenderer;
		IPLhandle ProbeManager;
		TArray<IPLhandle> ProbeBatches;
		IPLSimulationSettings SimulationSettings;
		IPLRenderingSettings RenderingSettings;
		IPLAudioFormat EnvironmentalOutputAudioFormat;
	};
}