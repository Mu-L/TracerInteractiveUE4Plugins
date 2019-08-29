// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "AudioMixerSourceManager.h"
#include "AudioMixerSource.h"
#include "AudioMixerDevice.h"
#include "AudioMixerSourceVoice.h"
#include "AudioMixerSubmix.h"
#include "IAudioExtensionPlugin.h"
#include "AudioMixer.h"

DEFINE_STAT(STAT_AudioMixerHRTF);

static int32 DisableParallelSourceProcessingCvar = 1;
FAutoConsoleVariableRef CVarDisableParallelSourceProcessing(
	TEXT("au.DisableParallelSourceProcessing"),
	DisableParallelSourceProcessingCvar,
	TEXT("Disables using async tasks for processing sources.\n")
	TEXT("0: Not Disabled, 1: Disabled"),
	ECVF_Default);

static int32 DisableFilteringCvar = 0;
FAutoConsoleVariableRef CVarDisableFiltering(
	TEXT("au.DisableFiltering"),
	DisableFilteringCvar,
	TEXT("Disables using the per-source lowpass and highpass filter.\n")
	TEXT("0: Not Disabled, 1: Disabled"),
	ECVF_Default);

static int32 DisableHPFilteringCvar = 0;
FAutoConsoleVariableRef CVarDisableHPFiltering(
	TEXT("au.DisableHPFiltering"),
	DisableHPFilteringCvar,
	TEXT("Disables using the per-source highpass filter.\n")
	TEXT("0: Not Disabled, 1: Disabled"),
	ECVF_Default);

static int32 DisableEnvelopeFollowingCvar = 0;
FAutoConsoleVariableRef CVarDisableEnvelopeFollowing(
	TEXT("au.DisableEnvelopeFollowing"),
	DisableEnvelopeFollowingCvar,
	TEXT("Disables using the envlope follower for source envelope tracking.\n")
	TEXT("0: Not Disabled, 1: Disabled"),
	ECVF_Default);

static int32 DisableSourceEffectsCvar = 0;
FAutoConsoleVariableRef CVarDisableSourceEffects(
	TEXT("au.DisableSourceEffects"),
	DisableSourceEffectsCvar,
	TEXT("Disables using any source effects.\n")
	TEXT("0: Not Disabled, 1: Disabled"),
	ECVF_Default);

static int32 DisableDistanceAttenuationCvar = 0;
FAutoConsoleVariableRef CVarDisableDistanceAttenuation(
	TEXT("au.DisableDistanceAttenuation"),
	DisableDistanceAttenuationCvar,
	TEXT("Disables using any Distance Attenuation.\n")
	TEXT("0: Not Disabled, 1: Disabled"),
	ECVF_Default);

static int32 BypassAudioPluginsCvar = 0;
FAutoConsoleVariableRef CVarBypassAudioPlugins(
	TEXT("au.BypassAudioPlugins"),
	BypassAudioPluginsCvar,
	TEXT("Bypasses any audio plugin processing.\n")
	TEXT("0: Not Disabled, 1: Disabled"),
	ECVF_Default);

#define ONEOVERSHORTMAX (3.0517578125e-5f) // 1/32768
#define ENVELOPE_TAIL_THRESHOLD (1.58489e-5f) // -96 dB

#define VALIDATE_SOURCE_MIXER_STATE 1

#if AUDIO_MIXER_ENABLE_DEBUG_MODE

// Macro which checks if the source id is in debug mode, avoids having a bunch of #ifdefs in code
#define AUDIO_MIXER_DEBUG_LOG(SourceId, Format, ...)																							\
	if (SourceInfos[SourceId].bIsDebugMode)																													\
	{																																			\
		FString CustomMessage = FString::Printf(Format, ##__VA_ARGS__);																			\
		FString LogMessage = FString::Printf(TEXT("<Debug Sound Log> [Id=%d][Name=%s]: %s"), SourceId, *SourceInfos[SourceId].DebugName, *CustomMessage);	\
		UE_LOG(LogAudioMixer, Log, TEXT("%s"), *LogMessage);																								\
	}

#else

#define AUDIO_MIXER_DEBUG_LOG(SourceId, Message)

#endif

// Disable subframe timing logic
#define AUDIO_SUBFRAME_ENABLED 0

namespace Audio
{
	/*************************************************************************
	* FMixerSourceManager
	**************************************************************************/

	FMixerSourceManager::FMixerSourceManager(FMixerDevice* InMixerDevice)
		: MixerDevice(InMixerDevice)
		, NumActiveSources(0)
		, NumTotalSources(0)
		, NumOutputFrames(0)
		, NumOutputSamples(0)
		, NumSourceWorkers(4)
		, bInitialized(false)
		, bUsingSpatializationPlugin(false)
	{
	}

	FMixerSourceManager::~FMixerSourceManager()
	{
		if (SourceWorkers.Num() > 0)
		{
			for (int32 i = 0; i < SourceWorkers.Num(); ++i)
			{
				delete SourceWorkers[i];
				SourceWorkers[i] = nullptr;
			}

			SourceWorkers.Reset();
		}
	}

	void FMixerSourceManager::Init(const FSourceManagerInitParams& InitParams)
	{
		AUDIO_MIXER_CHECK(InitParams.NumSources > 0);

		if (bInitialized || !MixerDevice)
		{
			return;
		}

		AUDIO_MIXER_CHECK(MixerDevice->GetSampleRate() > 0);

		NumTotalSources = InitParams.NumSources;

		NumOutputFrames = MixerDevice->PlatformSettings.CallbackBufferFrameSize;
		NumOutputSamples = NumOutputFrames * MixerDevice->GetNumDeviceChannels();

		MixerSources.Init(nullptr, NumTotalSources);

		SourceInfos.AddDefaulted(NumTotalSources);

		for (int32 i = 0; i < NumTotalSources; ++i)
		{
			FSourceInfo& SourceInfo = SourceInfos[i];

			SourceInfo.MixerSourceBuffer = nullptr;

			SourceInfo.VolumeSourceStart = -1.0f;
			SourceInfo.VolumeSourceDestination = -1.0f;
			SourceInfo.VolumeFadeSlope = 0.0f;
			SourceInfo.VolumeFadeStart = 0.0f;
			SourceInfo.VolumeFadeFramePosition = 0;
			SourceInfo.VolumeFadeNumFrames = 0;

			SourceInfo.DistanceAttenuationSourceStart = -1.0f;
			SourceInfo.DistanceAttenuationSourceDestination = -1.0f;

			SourceInfo.SourceListener = nullptr;
			SourceInfo.CurrentPCMBuffer = nullptr;	
			SourceInfo.CurrentAudioChunkNumFrames = 0;
			SourceInfo.CurrentFrameAlpha = 0.0f;
			SourceInfo.CurrentFrameIndex = 0;
			SourceInfo.NumFramesPlayed = 0;
			SourceInfo.StartTime = 0.0;
			SourceInfo.SubmixSends.Reset();
			SourceInfo.BusId = INDEX_NONE;
			SourceInfo.BusDurationFrames = INDEX_NONE;
		
			SourceInfo.BusSends[(int32)EBusSendType::PreEffect].Reset();
			SourceInfo.BusSends[(int32)EBusSendType::PostEffect].Reset();

			SourceInfo.SourceEffectChainId = INDEX_NONE;

			SourceInfo.SourceEnvelopeFollower = Audio::FEnvelopeFollower(MixerDevice->SampleRate, 10, 100, Audio::EPeakMode::Peak);
			SourceInfo.SourceEnvelopeValue = 0.0f;
			SourceInfo.bEffectTailsDone = false;
			SourceInfo.PostEffectBuffers = nullptr;
		
			SourceInfo.bIs3D = false;
			SourceInfo.bIsCenterChannelOnly = false;
			SourceInfo.bIsActive = false;
			SourceInfo.bIsPlaying = false;
			SourceInfo.bIsPaused = false;
			SourceInfo.bIsStopping = false;
			SourceInfo.bIsDone = false;
			SourceInfo.bIsLastBuffer = false;
			SourceInfo.bIsBusy = false;
			SourceInfo.bUseHRTFSpatializer = false;
			SourceInfo.bUseOcclusionPlugin = false;
			SourceInfo.bUseReverbPlugin = false;
			SourceInfo.bHasStarted = false;
			SourceInfo.bIsDebugMode = false;
			SourceInfo.bOutputToBusOnly = false;
			SourceInfo.bIsVorbis = false;
			SourceInfo.bIsBypassingLPF = false;
			SourceInfo.bIsBypassingHPF = false;

			SourceInfo.NumInputChannels = 0;
			SourceInfo.NumPostEffectChannels = 0;
			SourceInfo.NumInputFrames = 0;
		}
		
		GameThreadInfo.bIsBusy.AddDefaulted(NumTotalSources);
		GameThreadInfo.bNeedsSpeakerMap.AddDefaulted(NumTotalSources);
		GameThreadInfo.bIsDebugMode.AddDefaulted(NumTotalSources);
		GameThreadInfo.FreeSourceIndices.Reset(NumTotalSources);
		for (int32 i = NumTotalSources - 1; i >= 0; --i)
		{
			GameThreadInfo.FreeSourceIndices.Add(i);
		}

		// Initialize the source buffer memory usage to max source scratch buffers (num frames times max source channels)
		for (int32 SourceId = 0; SourceId < NumTotalSources; ++SourceId)
		{
			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			SourceInfo.SourceBuffer.Reset(NumOutputFrames * 8);
			SourceInfo.PreDistanceAttenuationBuffer.Reset(NumOutputFrames * 8);
			SourceInfo.AudioPluginOutputData.AudioBuffer.Reset(NumOutputFrames * 2);
		}

		// Setup the source workers
		SourceWorkers.Reset();
		if (NumSourceWorkers > 0)
		{
			const int32 NumSourcesPerWorker = FMath::Max(NumTotalSources / NumSourceWorkers, 1);
			int32 StartId = 0;
			int32 EndId = 0;
			while (EndId < NumTotalSources)
			{
				EndId = FMath::Min(StartId + NumSourcesPerWorker, NumTotalSources);
				SourceWorkers.Add(new FAsyncTask<FAudioMixerSourceWorker>(this, StartId, EndId));
				StartId = EndId;
			}
		}
		NumSourceWorkers = SourceWorkers.Num();

		// Cache the spatialization plugin
		SpatializationPlugin = MixerDevice->SpatializationPluginInterface;
		if (SpatializationPlugin.IsValid())
		{
			bUsingSpatializationPlugin = true;
		}

		bInitialized = true;
		bPumpQueue = false;
	}

	void FMixerSourceManager::Update()
	{
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

#if VALIDATE_SOURCE_MIXER_STATE
		for (int32 i = 0; i < NumTotalSources; ++i)
		{
			if (!GameThreadInfo.bIsBusy[i])
			{
				// Make sure that our bIsFree and FreeSourceIndices are correct
				AUDIO_MIXER_CHECK(GameThreadInfo.FreeSourceIndices.Contains(i) == true);
			}
		}
#endif

		int32 CurrentRenderIndex = RenderThreadCommandBufferIndex.GetValue();
		int32 CurrentGameIndex = AudioThreadCommandBufferIndex.GetValue();
		check(CurrentGameIndex == 0 || CurrentGameIndex == 1);
		check(CurrentRenderIndex == 0 || CurrentRenderIndex == 1);

		// If these values are the same, that means the audio render thread has finished the last buffer queue so is ready for the next block
		if (CurrentRenderIndex == CurrentGameIndex)
		{
			// This flags the audio render thread to be able to pump the next batch of commands
			// And will allow the audio thread to write to a new command slot
			const int32 NextIndex = !CurrentGameIndex;

			// Make sure we've actually emptied the command queue from the render thread before writing to it
			check(CommandBuffers[NextIndex].SourceCommandQueue.Num() == 0);
			AudioThreadCommandBufferIndex.Set(NextIndex);
			bPumpQueue = true;
		}
	}

	void FMixerSourceManager::ReleaseSource(const int32 SourceId)
	{
		AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(bInitialized);
		AUDIO_MIXER_CHECK(MixerSources[SourceId] != nullptr);

		AUDIO_MIXER_DEBUG_LOG(SourceId, TEXT("Is releasing"));
		
		FSourceInfo& SourceInfo = SourceInfos[SourceId];

#if AUDIO_MIXER_ENABLE_DEBUG_MODE
		if (SourceInfo.bIsDebugMode)
		{
			DebugSoloSources.Remove(SourceId);
		}
#endif
		// Remove from list of active bus or source ids depending on what type of source this is
		if (SourceInfo.BusId != INDEX_NONE)
		{
			// Remove this bus from the registry of bus instances
			FMixerBus* Bus = Buses.Find(SourceInfo.BusId);
			AUDIO_MIXER_CHECK(Bus);

			// Remove this source from the list of bus instances.
			if (Bus->RemoveInstanceId(SourceId))
			{
				Buses.Remove(SourceInfo.BusId);
			}
		}

		// Remove this source's send list from the bus data registry
		for (int32 BusSendType = 0; BusSendType < (int32)EBusSendType::Count; ++BusSendType)
		{
			for (uint32 BusId : SourceInfo.BusSends[BusSendType])
			{
				// we should have a bus registration entry still since the send hasn't been cleaned up yet
				FMixerBus* Bus = Buses.Find(BusId);
				AUDIO_MIXER_CHECK(Bus);

				if (Bus->RemoveBusSend((EBusSendType)BusSendType, SourceId))
				{
					Buses.Remove(BusId);
				}
			}

			SourceInfo.BusSends[BusSendType].Reset();
		}

		SourceInfo.BusId = INDEX_NONE;
		SourceInfo.BusDurationFrames = INDEX_NONE;

		// Free the mixer source buffer data
		if (SourceInfo.MixerSourceBuffer.IsValid())
		{
			PendingSourceBuffers.Add(SourceInfo.MixerSourceBuffer);
			SourceInfo.MixerSourceBuffer = nullptr;
		}

		SourceInfo.SourceListener = nullptr;

		// Remove the mixer source from its submix sends
		for (FMixerSourceSubmixSend& SubmixSendItem : SourceInfo.SubmixSends)
		{
			SubmixSendItem.Submix->RemoveSourceVoice(MixerSources[SourceId]);
		}
		SourceInfo.SubmixSends.Reset();

		// Notify plugin effects
		if (SourceInfo.bUseHRTFSpatializer)
		{
			AUDIO_MIXER_CHECK(bUsingSpatializationPlugin);
			SpatializationPlugin->OnReleaseSource(SourceId);
		}

		if (SourceInfo.bUseOcclusionPlugin)
		{
			MixerDevice->OcclusionInterface->OnReleaseSource(SourceId);
		}

		if (SourceInfo.bUseReverbPlugin)
		{
			MixerDevice->ReverbPluginInterface->OnReleaseSource(SourceId);
		}

		// Delete the source effects
		SourceInfo.SourceEffectChainId = INDEX_NONE;
		ResetSourceEffectChain(SourceId);

		SourceInfo.SourceEnvelopeFollower.Reset();
		SourceInfo.bEffectTailsDone = true;

		// Release the source voice back to the mixer device. This is pooled.
		MixerDevice->ReleaseMixerSourceVoice(MixerSources[SourceId]);
		MixerSources[SourceId] = nullptr;

		// Reset all state and data
		SourceInfo.PitchSourceParam.Init();
		SourceInfo.VolumeSourceStart = -1.0f;
		SourceInfo.VolumeSourceDestination = -1.0f;
		SourceInfo.VolumeFadeSlope = 0.0f;
		SourceInfo.VolumeFadeStart = 0.0f;
		SourceInfo.VolumeFadeFramePosition = 0;
		SourceInfo.VolumeFadeNumFrames = 0;

		SourceInfo.DistanceAttenuationSourceStart = -1.0f;
		SourceInfo.DistanceAttenuationSourceDestination = -1.0f;
		SourceInfo.LPFCutoffFrequencyParam.Init();
		SourceInfo.HPFCutoffFrequencyParam.Init();

		SourceInfo.LowPassFilter.Reset();
		SourceInfo.HighPassFilter.Reset();
		SourceInfo.CurrentPCMBuffer = nullptr;
		SourceInfo.CurrentAudioChunkNumFrames = 0;
		SourceInfo.SourceBuffer.Reset();
		SourceInfo.PreDistanceAttenuationBuffer.Reset();
		SourceInfo.AudioPluginOutputData.AudioBuffer.Reset();
		SourceInfo.CurrentFrameValues.Reset();
		SourceInfo.NextFrameValues.Reset();
		SourceInfo.CurrentFrameAlpha = 0.0f;
		SourceInfo.CurrentFrameIndex = 0;
		SourceInfo.NumFramesPlayed = 0;
		SourceInfo.StartTime = 0.0;
		SourceInfo.PostEffectBuffers = nullptr;
		SourceInfo.bIs3D = false;
		SourceInfo.bIsCenterChannelOnly = false;
		SourceInfo.bIsActive = false;
		SourceInfo.bIsPlaying = false;
		SourceInfo.bIsDone = true;
		SourceInfo.bIsLastBuffer = false;
		SourceInfo.bIsPaused = false;
		SourceInfo.bIsStopping = false;
		SourceInfo.bIsBusy = false;
		SourceInfo.bUseHRTFSpatializer = false;
		SourceInfo.bUseOcclusionPlugin = false;
		SourceInfo.bUseReverbPlugin = false;
		SourceInfo.bHasStarted = false;
		SourceInfo.bIsDebugMode = false;
		SourceInfo.bOutputToBusOnly = false;
		SourceInfo.bIsBypassingLPF = false;
		SourceInfo.bIsBypassingHPF = false;
		SourceInfo.DebugName = FString();
		SourceInfo.NumInputChannels = 0;
		SourceInfo.NumPostEffectChannels = 0;

		// Reset submix channel infos
		for (int32 i = 0; i < (int32)ESubmixChannelFormat::Count; ++i)
		{
			SourceInfo.SubmixChannelInfo[i].OutputBuffer.Reset();
			SourceInfo.SubmixChannelInfo[i].ChannelMapParam.Reset();
			SourceInfo.SubmixChannelInfo[i].bUsed = false;
		}

		GameThreadInfo.bNeedsSpeakerMap[SourceId] = false;
	}

	void FMixerSourceManager::BuildSourceEffectChain(const int32 SourceId, FSoundEffectSourceInitData& InitData, const TArray<FSourceEffectChainEntry>& InSourceEffectChain)
	{
		// Create new source effects. The memory will be owned by the source manager.
		for (const FSourceEffectChainEntry& ChainEntry : InSourceEffectChain)
		{
			// Presets can have null entries
			if (!ChainEntry.Preset)
			{
				continue;
			}

			FSoundEffectSource* NewEffect = static_cast<FSoundEffectSource*>(ChainEntry.Preset->CreateNewEffect());
			NewEffect->RegisterWithPreset(ChainEntry.Preset);

			// Get this source effect presets unique id so instances can identify their originating preset object
			const uint32 PresetUniqueId = ChainEntry.Preset->GetUniqueID();
			InitData.ParentPresetUniqueId = PresetUniqueId;

			NewEffect->Init(InitData);
			NewEffect->SetPreset(ChainEntry.Preset);
			NewEffect->SetEnabled(!ChainEntry.bBypass);

			// Add the effect instance
			FSourceInfo& SourceInfo = SourceInfos[SourceId];
			SourceInfo.SourceEffects.Add(NewEffect);

			// Add a slot entry for the preset so it can change while running. This will get sent to the running effect instance if the preset changes.
			SourceInfo.SourceEffectPresets.Add(nullptr);
		}
	}

	void FMixerSourceManager::ResetSourceEffectChain(const int32 SourceId)
	{
		FSourceInfo& SourceInfo = SourceInfos[SourceId];

		for (int32 i = 0; i < SourceInfo.SourceEffects.Num(); ++i)
		{
			SourceInfo.SourceEffects[i]->UnregisterWithPreset();
			delete SourceInfo.SourceEffects[i];
			SourceInfo.SourceEffects[i] = nullptr;
		}
		SourceInfo.SourceEffects.Reset();

		for (int32 i = 0; i < SourceInfo.SourceEffectPresets.Num(); ++i)
		{
			SourceInfo.SourceEffectPresets[i] = nullptr;
		}
		SourceInfo.SourceEffectPresets.Reset();
	}

	bool FMixerSourceManager::GetFreeSourceId(int32& OutSourceId)
	{
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		if (GameThreadInfo.FreeSourceIndices.Num())
		{
			OutSourceId = GameThreadInfo.FreeSourceIndices.Pop();

			AUDIO_MIXER_CHECK(OutSourceId < NumTotalSources);
			AUDIO_MIXER_CHECK(!GameThreadInfo.bIsBusy[OutSourceId]);

			AUDIO_MIXER_CHECK(!GameThreadInfo.bIsDebugMode[OutSourceId]);
			AUDIO_MIXER_CHECK(NumActiveSources < NumTotalSources);
			++NumActiveSources;

			GameThreadInfo.bIsBusy[OutSourceId] = true;
			return true;
		}
		AUDIO_MIXER_CHECK(false);
		return false;
	}

	int32 FMixerSourceManager::GetNumActiveSources() const
	{
		return NumActiveSources;
	}

	int32 FMixerSourceManager::GetNumActiveBuses() const
	{
		return Buses.Num();
	}

	void FMixerSourceManager::InitSource(const int32 SourceId, const FMixerSourceVoiceInitParams& InitParams)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK(!GameThreadInfo.bIsDebugMode[SourceId]);
		AUDIO_MIXER_CHECK(InitParams.SourceListener != nullptr);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

#if AUDIO_MIXER_ENABLE_DEBUG_MODE
		GameThreadInfo.bIsDebugMode[SourceId] = InitParams.bIsDebugMode;
#endif 

		// Make sure we flag that this source needs a speaker map to at least get one
		GameThreadInfo.bNeedsSpeakerMap[SourceId] = true;

		AudioMixerThreadCommand([this, SourceId, InitParams]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);
			AUDIO_MIXER_CHECK(InitParams.SourceVoice != nullptr);

			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			// Initialize the mixer source buffer decoder with the given mixer buffer
			SourceInfo.MixerSourceBuffer = InitParams.MixerSourceBuffer;
			SourceInfo.MixerSourceBuffer->OnBeginGenerate();

			SourceInfo.bIsPlaying = false;
			SourceInfo.bIsPaused = false;
			SourceInfo.bIsStopping = false;
			SourceInfo.bIsActive = true;
			SourceInfo.bIsBusy = true;
			SourceInfo.bIsDone = false;
			SourceInfo.bIsLastBuffer = false;
			SourceInfo.bUseHRTFSpatializer = InitParams.bUseHRTFSpatialization;
			SourceInfo.bIsVorbis = InitParams.bIsVorbis;
			SourceInfo.AudioComponentID = InitParams.AudioComponentID;

			// Call initialization from the render thread so anything wanting to do any initialization here can do so (e.g. procedural sound waves)
			SourceInfo.SourceListener = InitParams.SourceListener;
			SourceInfo.SourceListener->OnBeginGenerate();

			SourceInfo.NumInputChannels = InitParams.NumInputChannels;
			SourceInfo.NumInputFrames = InitParams.NumInputFrames;

			// Initialize the number of per-source LPF filters based on input channels
			SourceInfo.LowPassFilter.Init(MixerDevice->SampleRate, InitParams.NumInputChannels);

			SourceInfo.HighPassFilter.Init(MixerDevice->SampleRate, InitParams.NumInputChannels, 0, nullptr);
			SourceInfo.HighPassFilter.SetFilterType(EFilter::Type::HighPass);

			SourceInfo.SourceEnvelopeFollower = Audio::FEnvelopeFollower(MixerDevice->SampleRate / NumOutputFrames, (float)InitParams.EnvelopeFollowerAttackTime, (float)InitParams.EnvelopeFollowerReleaseTime, Audio::EPeakMode::Peak);

			// Create the spatialization plugin source effect
			if (InitParams.bUseHRTFSpatialization)
			{
				AUDIO_MIXER_CHECK(bUsingSpatializationPlugin);
				SpatializationPlugin->OnInitSource(SourceId, InitParams.AudioComponentUserID, InitParams.SpatializationPluginSettings);
			}

			// Create the occlusion plugin source effect
			if (InitParams.OcclusionPluginSettings != nullptr)
			{
				MixerDevice->OcclusionInterface->OnInitSource(SourceId, InitParams.AudioComponentUserID, InitParams.NumInputChannels, InitParams.OcclusionPluginSettings);
				SourceInfo.bUseOcclusionPlugin = true;
			}

			// Create the reverb plugin source effect
			if (InitParams.ReverbPluginSettings != nullptr)
			{
				MixerDevice->ReverbPluginInterface->OnInitSource(SourceId, InitParams.AudioComponentUserID, InitParams.NumInputChannels, InitParams.ReverbPluginSettings);
				SourceInfo.bUseReverbPlugin = true;
			}

			// Default all sounds to not consider effect chain tails when playing
			SourceInfo.bEffectTailsDone = true;

			// Copy the source effect chain if the channel count is 1 or 2
			if (InitParams.NumInputChannels <= 2)
			{
				// If we're told to care about effect chain tails, then we're not allowed
				// to stop playing until the effect chain tails are finished
				SourceInfo.bEffectTailsDone = !InitParams.bPlayEffectChainTails;

				FSoundEffectSourceInitData InitData;
				InitData.SampleRate = MixerDevice->SampleRate;
				InitData.NumSourceChannels = InitParams.NumInputChannels;
				InitData.AudioClock = MixerDevice->GetAudioTime();

				if (InitParams.NumInputFrames != INDEX_NONE)
				{
					InitData.SourceDuration = (float)InitParams.NumInputFrames / MixerDevice->SampleRate;
				}
				else
				{
					// Procedural sound waves have no known duration
					InitData.SourceDuration = (float)INDEX_NONE;
				}

				SourceInfo.SourceEffectChainId = InitParams.SourceEffectChainId;
				BuildSourceEffectChain(SourceId, InitData, InitParams.SourceEffectChain);

				// Whether or not to output to bus only
				SourceInfo.bOutputToBusOnly = InitParams.bOutputToBusOnly;

				// If this is a bus, add this source id to the list of active bus ids
				if (InitParams.BusId != INDEX_NONE)
				{
					// Setting this BusId will flag this source as a bus. It doesn't try to generate 
					// audio in the normal way but instead will render in a second stage, after normal source rendering.
					SourceInfo.BusId = InitParams.BusId;

					// Bus duration allows us to stop a bus after a given time
					if (InitParams.BusDuration != 0.0f)
					{
						SourceInfo.BusDurationFrames = InitParams.BusDuration * MixerDevice->GetSampleRate();
					}

					// Register this bus as an instance
					FMixerBus* Bus = Buses.Find(InitParams.BusId);
					if (Bus)
					{
						// If this bus is already registered, add this as a source id
						Bus->AddInstanceId(SourceId);
					}
					else
					{
						// If the bus is not registered, make a new entry
						FMixerBus NewBusData(this, InitParams.NumInputChannels, NumOutputFrames);

						NewBusData.AddInstanceId(SourceId);

						Buses.Add(InitParams.BusId, NewBusData);
					}
				}

				// Iterate through source's bus sends and add this source to the bus send list
				// Note: buses can also send their audio to other buses.
				for (int32 BusSendType = 0; BusSendType < (int32)EBusSendType::Count; ++BusSendType)
				{
					for (const FMixerBusSend& BusSend : InitParams.BusSends[BusSendType])
					{
						// New struct to map which source (SourceId) is sending to the bus
						FBusSend NewBusSend;
						NewBusSend.SourceId = SourceId;
						NewBusSend.SendLevel = BusSend.SendLevel;

						// Get existing BusId and add the send, or create new bus registration
						FMixerBus* Bus = Buses.Find(BusSend.BusId);
						if (Bus)
						{
							Bus->AddBusSend((EBusSendType)BusSendType, NewBusSend);
						}
						else
						{
							// If the bus is not registered, make a new entry
							FMixerBus NewBusData(this, InitParams.NumInputChannels, NumOutputFrames);

							// Add a send to it. This will not have a bus instance id (i.e. won't output audio), but 
							// we register the send anyway in the event that this bus does play, we'll know to send this
							// source's audio to it.
							NewBusData.AddBusSend((EBusSendType)BusSendType, NewBusSend);

							Buses.Add(BusSend.BusId, NewBusData);
						}

						// Store on this source, which buses its sending its audio to
						SourceInfo.BusSends[BusSendType].Add(BusSend.BusId);
					}
				}
			}

			SourceInfo.CurrentFrameValues.Init(0.0f, InitParams.NumInputChannels);
			SourceInfo.NextFrameValues.Init(0.0f, InitParams.NumInputChannels);

			AUDIO_MIXER_CHECK(MixerSources[SourceId] == nullptr);
			MixerSources[SourceId] = InitParams.SourceVoice;

			// Loop through the source's sends and add this source to those submixes with the send info

			AUDIO_MIXER_CHECK(SourceInfo.SubmixSends.Num() == 0);

			for (int32 i = 0; i < InitParams.SubmixSends.Num(); ++i)
			{
				const FMixerSourceSubmixSend& MixerSubmixSend = InitParams.SubmixSends[i];
				SourceInfo.SubmixSends.Add(MixerSubmixSend);
				MixerSubmixSend.Submix->AddOrSetSourceVoice(InitParams.SourceVoice, MixerSubmixSend.SendLevel);

				// Prepare output buffers and speaker map entries for every submix channel type
				const ESubmixChannelFormat SubmixChannelType = MixerSubmixSend.Submix->GetSubmixChannels();

				// Flag that we're going to be using this channel info entry
				SourceInfo.SubmixChannelInfo[(int32)SubmixChannelType].bUsed = true;
			}

#if AUDIO_MIXER_ENABLE_DEBUG_MODE
			AUDIO_MIXER_CHECK(!SourceInfo.bIsDebugMode);
			SourceInfo.bIsDebugMode = InitParams.bIsDebugMode;

			AUDIO_MIXER_CHECK(SourceInfo.DebugName.IsEmpty());
			SourceInfo.DebugName = InitParams.DebugName;
#endif 

			AUDIO_MIXER_DEBUG_LOG(SourceId, TEXT("Is initializing"));
		});
	}

	void FMixerSourceManager::ReleaseSourceId(const int32 SourceId)
	{
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AUDIO_MIXER_CHECK(NumActiveSources > 0);
		--NumActiveSources;

		GameThreadInfo.bIsBusy[SourceId] = false;

#if AUDIO_MIXER_ENABLE_DEBUG_MODE
		GameThreadInfo.bIsDebugMode[SourceId] = false;
#endif

		GameThreadInfo.FreeSourceIndices.Push(SourceId);

		AUDIO_MIXER_CHECK(GameThreadInfo.FreeSourceIndices.Contains(SourceId));

		AudioMixerThreadCommand([this, SourceId]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

			ReleaseSource(SourceId);
		});
	}

	void FMixerSourceManager::Play(const int32 SourceId)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		// Compute the frame within which to start the sound based on the current "thread faction" on the audio thread
		double StartTime = MixerDevice->GetAudioThreadTime();

		AudioMixerThreadCommand([this, SourceId, StartTime]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

			FSourceInfo& SourceInfo = SourceInfos[SourceId];
			
			SourceInfo.bIsPlaying = true;
			SourceInfo.bIsPaused = false;
			SourceInfo.bIsActive = true;

			SourceInfo.StartTime = StartTime;

			AUDIO_MIXER_DEBUG_LOG(SourceId, TEXT("Is playing"));
		});
	}

	void FMixerSourceManager::Stop(const int32 SourceId)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AudioMixerThreadCommand([this, SourceId]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			SourceInfo.bIsPlaying = false;
			SourceInfo.bIsPaused = false;
			SourceInfo.bIsActive = false;
			SourceInfo.bIsStopping = false;

			AUDIO_MIXER_DEBUG_LOG(SourceId, TEXT("Is immediately stopping"));
		});
	}

	void FMixerSourceManager::StopFade(const int32 SourceId, const int32 NumFrames)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK(NumFrames > 0);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);


		AudioMixerThreadCommand([this, SourceId, NumFrames]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			SourceInfo.bIsPaused = false;
			SourceInfo.bIsStopping = true;
			
			// Only allow multiple of 4 fade frames and positive
			int32 NumFadeFrames = AlignArbitrary(NumFrames, 4);
			if (NumFadeFrames <= 0)
			{
				// Stop immediately if we've been given no fade frames
				SourceInfo.bIsPlaying = false;
				SourceInfo.bIsPaused = false;
				SourceInfo.bIsActive = false;
				SourceInfo.bIsStopping = false;
			}
			else
			{
				// compute the fade slope
				SourceInfo.VolumeFadeStart = SourceInfo.VolumeSourceStart;
				SourceInfo.VolumeFadeNumFrames = NumFadeFrames;
				SourceInfo.VolumeFadeSlope = -SourceInfo.VolumeSourceStart / SourceInfo.VolumeFadeNumFrames;
				SourceInfo.VolumeFadeFramePosition = 0;
			}

			AUDIO_MIXER_DEBUG_LOG(SourceId, TEXT("Is stopping with fade"));
		});
	}


	void FMixerSourceManager::Pause(const int32 SourceId)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AudioMixerThreadCommand([this, SourceId]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			SourceInfo.bIsPaused = true;
			SourceInfo.bIsActive = false;
		});
	}

	void FMixerSourceManager::SetPitch(const int32 SourceId, const float Pitch)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);

		AudioMixerThreadCommand([this, SourceId, Pitch]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);
			check(NumOutputFrames > 0);

			SourceInfos[SourceId].PitchSourceParam.SetValue(Pitch, NumOutputFrames);
		});
	}

	void FMixerSourceManager::SetVolume(const int32 SourceId, const float Volume)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AudioMixerThreadCommand([this, SourceId, Volume]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);
			check(NumOutputFrames > 0);

			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			// Only set the volume if we're not stopping. Stopping sources are setting their volume to 0.0.
			if (!SourceInfo.bIsStopping)
			{
				// If we've not yet set a volume, we need to immediately set the start and destination to be the same value (to avoid an initial fade in)
				if (SourceInfos[SourceId].VolumeSourceDestination < 0.0f)
				{
					SourceInfos[SourceId].VolumeSourceStart = Volume;
				}

				SourceInfos[SourceId].VolumeSourceDestination = Volume;
			}
		});
	}

	void FMixerSourceManager::SetDistanceAttenuation(const int32 SourceId, const float DistanceAttenuation)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AudioMixerThreadCommand([this, SourceId, DistanceAttenuation]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);
			check(NumOutputFrames > 0);

			// If we've not yet set a distance attenuation, we need to immediately set the start and destination to be the same value (to avoid an initial fade in)
			if (SourceInfos[SourceId].DistanceAttenuationSourceDestination < 0.0f)
			{
				SourceInfos[SourceId].DistanceAttenuationSourceStart = DistanceAttenuation;
			}

			SourceInfos[SourceId].DistanceAttenuationSourceDestination = DistanceAttenuation;
		});
	}

	void FMixerSourceManager::SetSpatializationParams(const int32 SourceId, const FSpatializationParams& InParams)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AudioMixerThreadCommand([this, SourceId, InParams]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

			SourceInfos[SourceId].SpatParams = InParams;
		});
	}

	void FMixerSourceManager::SetChannelMap(const int32 SourceId, const ESubmixChannelFormat SubmixChannelType, const Audio::AlignedFloatBuffer& ChannelMap, const bool bInIs3D, const bool bInIsCenterChannelOnly)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AudioMixerThreadCommand([this, SourceId, SubmixChannelType, ChannelMap, bInIs3D, bInIsCenterChannelOnly]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

			check(NumOutputFrames > 0);

			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			// Set whether or not this is a 3d channel map and if its center channel only. Used for reseting channel maps on device change.
			SourceInfo.bIs3D = bInIs3D;
			SourceInfo.bIsCenterChannelOnly = bInIsCenterChannelOnly;

			FSubmixChannelTypeInfo& ChannelTypeInfo = SourceInfo.SubmixChannelInfo[(int32)SubmixChannelType];
			ChannelTypeInfo.bUsed = true;

			// Fix up the channel map in case device output count changed
			if (SubmixChannelType == ESubmixChannelFormat::Device)
			{
				const int32 NumSourceChannels = SourceInfo.bUseHRTFSpatializer ? 2 : SourceInfo.NumInputChannels;
				const int32 NumOutputChannels = MixerDevice->GetNumDeviceChannels();
				const int32 ChannelMapSize = NumSourceChannels * NumOutputChannels;

				// If this is true, then the device changed while the command was in-flight
				if (ChannelMap.Num() != ChannelMapSize)
				{
					Audio::AlignedFloatBuffer NewChannelMap;

					// If 3d then just zero it out, we'll get another channel map shortly
					if (bInIs3D)
					{
						NewChannelMap.AddZeroed(ChannelMapSize);
						GameThreadInfo.bNeedsSpeakerMap[SourceId] = true;
					}
					// Otherwise, get an appropriate channel map for the new device configuration
					else
					{
						MixerDevice->Get2DChannelMap(SourceInfo.bIsVorbis, ESubmixChannelFormat::Device, NumSourceChannels, bInIsCenterChannelOnly, NewChannelMap);
					}

					// Make sure we've been flagged to be using this submix channel type entry
					ChannelTypeInfo.ChannelMapParam.SetChannelMap(NewChannelMap, NumOutputChannels);
				}
				else
				{
					GameThreadInfo.bNeedsSpeakerMap[SourceId] = false;
					ChannelTypeInfo.ChannelMapParam.SetChannelMap(ChannelMap, NumOutputFrames);
				}
			}
			else
			{
				// Since we're artificially mixing to this channel count, we don't need to deal with device reset
				ChannelTypeInfo.ChannelMapParam.SetChannelMap(ChannelMap, NumOutputFrames);
			}
		});
	}

	void FMixerSourceManager::SetLPFFrequency(const int32 SourceId, const float InLPFFrequency)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AudioMixerThreadCommand([this, SourceId, InLPFFrequency]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

			SourceInfos[SourceId].LPFCutoffFrequencyParam.SetValue(InLPFFrequency, NumOutputFrames);
		});
	}

	void FMixerSourceManager::SetHPFFrequency(const int32 SourceId, const float InHPFFrequency)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AudioMixerThreadCommand([this, SourceId, InHPFFrequency]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

			SourceInfos[SourceId].HPFCutoffFrequencyParam.SetValue(InHPFFrequency, NumOutputFrames);
		});
	}

	void FMixerSourceManager::SetSubmixSendInfo(const int32 SourceId, const FMixerSourceSubmixSend& InSubmixSend)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AudioMixerThreadCommand([this, SourceId, InSubmixSend]()
		{
			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			bool bIsNew = true;
			for (FMixerSourceSubmixSend& SubmixSend : SourceInfo.SubmixSends)
			{
				if (SubmixSend.Submix->GetId() == InSubmixSend.Submix->GetId())
				{
					SubmixSend.SendLevel = InSubmixSend.SendLevel;
					bIsNew = false;
					break;
				}
			}

			if (bIsNew)
			{
				SourceInfo.SubmixSends.Add(InSubmixSend);

				// Flag that we're now using this submix channel info
				ESubmixChannelFormat ChannelType = InSubmixSend.Submix->GetSubmixChannels();
				SourceInfo.SubmixChannelInfo[(int32)ChannelType].bUsed = true;
			}

			InSubmixSend.Submix->AddOrSetSourceVoice(MixerSources[SourceId], InSubmixSend.SendLevel);
		});
	}

	void FMixerSourceManager::SetListenerTransforms(const TArray<FTransform>& InListenerTransforms)
	{
		AudioMixerThreadCommand([this, InListenerTransforms]()
		{
			ListenerTransforms = InListenerTransforms;
		});
	}

	const TArray<FTransform>* FMixerSourceManager::GetListenerTransforms() const
	{
		AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);
		return &ListenerTransforms;
	}

	int64 FMixerSourceManager::GetNumFramesPlayed(const int32 SourceId) const
	{
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);
		return SourceInfos[SourceId].NumFramesPlayed;
	}

	float FMixerSourceManager::GetEnvelopeValue(const int32 SourceId) const
	{
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);
		return SourceInfos[SourceId].SourceEnvelopeValue;
	}

	bool FMixerSourceManager::NeedsSpeakerMap(const int32 SourceId) const
	{
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);
		return GameThreadInfo.bNeedsSpeakerMap[SourceId];
	}

	void FMixerSourceManager::ReadSourceFrame(const int32 SourceId)
	{
		FSourceInfo& SourceInfo = SourceInfos[SourceId];

		const int32 NumChannels = SourceInfo.NumInputChannels;

		// Check if the next frame index is out of range of the total number of frames we have in our current audio buffer
		bool bNextFrameOutOfRange = (SourceInfo.CurrentFrameIndex + 1) >= SourceInfo.CurrentAudioChunkNumFrames;
		bool bCurrentFrameOutOfRange = SourceInfo.CurrentFrameIndex >= SourceInfo.CurrentAudioChunkNumFrames;

		bool bReadCurrentFrame = true;

		// Check the boolean conditions that determine if we need to pop buffers from our queue (in PCMRT case) *OR* loop back (looping PCM data)
		while (bNextFrameOutOfRange || bCurrentFrameOutOfRange)
		{
			// If our current frame is in range, but next frame isn't, read the current frame now to avoid pops when transitioning between buffers
			if (bNextFrameOutOfRange && !bCurrentFrameOutOfRange)
			{
				// Don't need to read the current frame audio after reading new audio chunk
				bReadCurrentFrame = false;

				AUDIO_MIXER_CHECK(SourceInfo.CurrentPCMBuffer.IsValid());
				const float* AudioData = SourceInfo.CurrentPCMBuffer->AudioData.GetData();
				const int32 CurrentSampleIndex = SourceInfo.CurrentFrameIndex * NumChannels;

				for (int32 Channel = 0; Channel < NumChannels; ++Channel)
				{
					SourceInfo.CurrentFrameValues[Channel] = AudioData[CurrentSampleIndex + Channel];
				}
			}

			// If this is our first PCM buffer, we don't need to do a callback to get more audio
			if (SourceInfo.CurrentPCMBuffer.IsValid())
			{
				if (SourceInfo.CurrentPCMBuffer->LoopCount == Audio::LOOP_FOREVER && !SourceInfo.CurrentPCMBuffer->bRealTimeBuffer)
				{
					AUDIO_MIXER_DEBUG_LOG(SourceId, TEXT("Hit Loop boundary, looping."));

					SourceInfo.CurrentFrameIndex = FMath::Max(SourceInfo.CurrentFrameIndex - SourceInfo.CurrentAudioChunkNumFrames, 0);
					break;
				}

				SourceInfo.MixerSourceBuffer->OnBufferEnd();
			}

			// If we have audio in our queue, we're still playing
			if (SourceInfo.MixerSourceBuffer->GetNumBuffersQueued() > 0)
			{
				SourceInfo.CurrentPCMBuffer = SourceInfo.MixerSourceBuffer->GetNextBuffer();
				SourceInfo.CurrentAudioChunkNumFrames = SourceInfo.CurrentPCMBuffer->Samples / NumChannels;

				// Subtract the number of frames in the current buffer from our frame index.
				// Note: if this is the first time we're playing, CurrentFrameIndex will be 0
				if (bReadCurrentFrame)
				{
					SourceInfo.CurrentFrameIndex = FMath::Max(SourceInfo.CurrentFrameIndex - SourceInfo.CurrentAudioChunkNumFrames, 0);
				}
				else
				{
					// Since we're not reading the current frame, we allow the current frame index to be negative (NextFrameIndex will then be 0)
					// This prevents dropping a frame of audio on the buffer boundary
					SourceInfo.CurrentFrameIndex = -1;
				}
			}
			else
			{
				SourceInfo.bIsLastBuffer = true;
				return;
			}

			bNextFrameOutOfRange = (SourceInfo.CurrentFrameIndex + 1) >= SourceInfo.CurrentAudioChunkNumFrames;
			bCurrentFrameOutOfRange = SourceInfo.CurrentFrameIndex >= SourceInfo.CurrentAudioChunkNumFrames;
		}

		if (SourceInfo.CurrentPCMBuffer.IsValid())
		{
			// Grab the float PCM audio data (which could be a new audio chunk from previous ReadSourceFrame call)
			const float* AudioData = SourceInfo.CurrentPCMBuffer->AudioData.GetData();
			const int32 NextSampleIndex = (SourceInfo.CurrentFrameIndex + 1)  * NumChannels;

			if (bReadCurrentFrame)
			{
				const int32 CurrentSampleIndex = SourceInfo.CurrentFrameIndex * NumChannels;
				for (int32 Channel = 0; Channel < NumChannels; ++Channel)
				{
					SourceInfo.CurrentFrameValues[Channel] = AudioData[CurrentSampleIndex + Channel];
					SourceInfo.NextFrameValues[Channel] = AudioData[NextSampleIndex + Channel];
				}
			}
			else
			{
				for (int32 Channel = 0; Channel < NumChannels; ++Channel)
				{
					SourceInfo.NextFrameValues[Channel] = AudioData[NextSampleIndex + Channel];
				}
			}
		}
	}

	void FMixerSourceManager::ComputeSourceBuffersForIdRange(const bool bGenerateBuses, const int32 SourceIdStart, const int32 SourceIdEnd)
	{
		SCOPE_CYCLE_COUNTER(STAT_AudioMixerSourceBuffers);

		const double AudioRenderThreadTime = MixerDevice->GetAudioRenderThreadTime();
		const double AudioClockDelta = MixerDevice->GetAudioClockDelta();

		for (int32 SourceId = SourceIdStart; SourceId < SourceIdEnd; ++SourceId)
		{
			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			if (!SourceInfo.bIsBusy || !SourceInfo.bIsPlaying || SourceInfo.bIsPaused)
			{
				continue;
			}

			// If this source is still playing at this point but technically done, zero the buffers. We haven't yet been removed by the FMixerSource owner.
			// This should be rare but could happen due to thread timing since done-ness is queried on audio thread.
			if (SourceInfo.bIsDone)
			{
				const int32 NumSamples = NumOutputFrames * SourceInfo.NumInputChannels;

				SourceInfo.PreDistanceAttenuationBuffer.Reset();
				SourceInfo.PreDistanceAttenuationBuffer.AddZeroed(NumSamples);

				SourceInfo.SourceBuffer.Reset();
				SourceInfo.SourceBuffer.AddZeroed(NumSamples);

				continue;
			}

			const bool bIsBus = SourceInfo.BusId != INDEX_NONE;
			if ((bGenerateBuses && !bIsBus) || (!bGenerateBuses && bIsBus))
			{
				continue;
			}

			// Fill array with elements all at once to avoid sequential Add() operation overhead.
			const int32 NumSamples = NumOutputFrames * SourceInfo.NumInputChannels;
			
			// Initialize both the pre-distance attenuation buffer and the source buffer
			SourceInfo.PreDistanceAttenuationBuffer.Reset();
			SourceInfo.PreDistanceAttenuationBuffer.AddZeroed(NumSamples);

			SourceInfo.SourceBuffer.Reset();
			SourceInfo.SourceBuffer.AddZeroed(NumSamples);

			float* PreDistanceAttenBufferPtr = SourceInfo.PreDistanceAttenuationBuffer.GetData();

			// if this is a bus, we just want to copy the bus audio to this source's output audio
			// Note we need to copy this since bus instances may have different audio via dynamic source effects, etc.
			if (bIsBus)
			{
				// Get the source's rendered bus data
				const FMixerBus* Bus = Buses.Find(SourceInfo.BusId);
				const float* BusBufferPtr = Bus->GetCurrentBusBuffer();

				int32 NumFramesPlayed = NumOutputFrames;
				if (SourceInfo.BusDurationFrames != INDEX_NONE)
				{
					// If we're now finishing, only copy over the real data
					if ((SourceInfo.NumFramesPlayed + NumOutputFrames) >= SourceInfo.BusDurationFrames)
					{
						NumFramesPlayed = SourceInfo.BusDurationFrames - SourceInfo.NumFramesPlayed;
						SourceInfo.bIsLastBuffer = true;				
					}
				}

				SourceInfo.NumFramesPlayed += NumFramesPlayed;

				// Simply copy into the pre distance attenuation buffer ptr
				FMemory::Memcpy(PreDistanceAttenBufferPtr, BusBufferPtr, sizeof(float) * NumFramesPlayed * SourceInfo.NumInputChannels);
			}
			else
			{

#if AUDIO_SUBFRAME_ENABLED
				// If we're not going to start yet, just continue
				double StartFraction = (SourceInfo.StartTime - AudioRenderThreadTime) / AudioClockDelta;
				if (StartFraction >= 1.0)
				{
					// note this is already zero'd so no need to write zeroes
					SourceInfo.PitchSourceParam.Reset();
					continue;
				}
				
				// Init the frame index iterator to 0 (i.e. render whole buffer)
				int32 StartFrame = 0;

				// If the start fraction is greater than 0.0 (and is less than 1.0), we are starting on a sub-frame
				// Otherwise, just start playing it right away
				if (StartFraction > 0.0)
				{
					StartFrame = NumOutputFrames * StartFraction;
				}

				// Update sample index to the frame we're starting on, accounting for source channels
				int32 SampleIndex = StartFrame * SourceInfo.NumInputChannels;
				bool bWriteOutZeros = true;
#else
				int32 SampleIndex = 0;
				int32 StartFrame = 0;
#endif

				for (int32 Frame = StartFrame; Frame < NumOutputFrames; ++Frame)
				{
					// If we've read our last buffer, we're done
					if (SourceInfo.bIsLastBuffer)
					{
						break;
					}

					// Whether or not we need to read another sample from the source buffers
					// If we haven't yet played any frames, then we will need to read the first source samples no matter what
					bool bReadNextSample = !SourceInfo.bHasStarted;

					// Reset that we've started generating audio
					SourceInfo.bHasStarted = true;

					// Update the PrevFrameIndex value for the source based on alpha value
					while (SourceInfo.CurrentFrameAlpha >= 1.0f)
					{
						// Our inter-frame alpha lerping value is causing us to read new source frames
						bReadNextSample = true;

						// Bump up the current frame index
						SourceInfo.CurrentFrameIndex++;

						// Bump up the frames played -- this is tracking the total frames in source file played
						// CurrentFrameIndex can wrap for looping sounds so won't be accurate in that case
						SourceInfo.NumFramesPlayed++;

						SourceInfo.CurrentFrameAlpha -= 1.0f;
					}

					// If our alpha parameter caused us to jump to a new source frame, we need
					// read new samples into our prev and next frame sample data
					if (bReadNextSample)
					{
						ReadSourceFrame(SourceId);
					}

					// perform linear SRC to get the next sample value from the decoded buffer
					for (int32 Channel = 0; Channel < SourceInfo.NumInputChannels; ++Channel)
					{
						const float CurrFrameValue = SourceInfo.CurrentFrameValues[Channel];
						const float NextFrameValue = SourceInfo.NextFrameValues[Channel];
						const float CurrentAlpha = SourceInfo.CurrentFrameAlpha;

						PreDistanceAttenBufferPtr[SampleIndex++] = FMath::Lerp(CurrFrameValue, NextFrameValue, CurrentAlpha);
					}
					const float CurrentPitchScale = SourceInfo.PitchSourceParam.Update();

					SourceInfo.CurrentFrameAlpha += CurrentPitchScale;
				}

				// After processing the frames, reset the pitch param
				SourceInfo.PitchSourceParam.Reset();
			}
		}
	}

	void FMixerSourceManager::ComputeBuses()
	{
		// Loop through the bus registry and mix source audio
		for (auto& Entry : Buses)
		{
			FMixerBus& Bus = Entry.Value;
			Bus.MixBuffer();
		}
	}

	void FMixerSourceManager::UpdateBuses()
	{
		// Update the bus states post mixing. This flips the current/previous buffer indices.
		for (auto& Entry : Buses)
		{
			FMixerBus& Bus = Entry.Value;
			Bus.Update();
		}
	}

	void FMixerSourceManager::ApplyDistanceAttenuation(FSourceInfo& SourceInfo, int32 NumSamples)
	{
		if (DisableDistanceAttenuationCvar)
		{
			return;
		}

		float* PostDistanceAttenBufferPtr = SourceInfo.SourceBuffer.GetData();
		Audio::FadeBufferFast(PostDistanceAttenBufferPtr, SourceInfo.SourceBuffer.Num(), SourceInfo.DistanceAttenuationSourceStart, SourceInfo.DistanceAttenuationSourceDestination);
		SourceInfo.DistanceAttenuationSourceStart = SourceInfo.DistanceAttenuationSourceDestination;
	}

	void FMixerSourceManager::ComputePluginAudio(FSourceInfo& SourceInfo, int32 SourceId, int32 NumSamples)
	{
		if (BypassAudioPluginsCvar)
		{
			return;
		}

		// Apply the distance attenuation before sending to plugins
		float* PreDistanceAttenBufferPtr = SourceInfo.PreDistanceAttenuationBuffer.GetData();
		float* PostDistanceAttenBufferPtr = SourceInfo.SourceBuffer.GetData();

		bool bShouldMixInReverb = false;
		if (SourceInfo.bUseReverbPlugin)
		{
			const FSpatializationParams* SourceSpatParams = &SourceInfo.SpatParams;

			// Move the audio buffer to the reverb plugin buffer
			FAudioPluginSourceInputData AudioPluginInputData;
			AudioPluginInputData.SourceId = SourceId;
			AudioPluginInputData.AudioBuffer = &SourceInfo.SourceBuffer;
			AudioPluginInputData.SpatializationParams = SourceSpatParams;
			AudioPluginInputData.NumChannels = SourceInfo.NumInputChannels;
			AudioPluginInputData.AudioComponentId = SourceInfo.AudioComponentID;
			SourceInfo.AudioPluginOutputData.AudioBuffer.Reset();
			SourceInfo.AudioPluginOutputData.AudioBuffer.AddZeroed(AudioPluginInputData.AudioBuffer->Num());

			MixerDevice->ReverbPluginInterface->ProcessSourceAudio(AudioPluginInputData, SourceInfo.AudioPluginOutputData);

			// Make sure the buffer counts didn't change and are still the same size
			AUDIO_MIXER_CHECK(SourceInfo.AudioPluginOutputData.AudioBuffer.Num() == NumSamples);

			//If the reverb effect doesn't send it's audio to an external device, mix the output data back in.
			if (!MixerDevice->bReverbIsExternalSend)
			{
				// Copy the reverb-processed data back to the source buffer
				SourceInfo.ReverbPluginOutputBuffer.Reset();
				SourceInfo.ReverbPluginOutputBuffer.Append(SourceInfo.AudioPluginOutputData.AudioBuffer);
				bShouldMixInReverb = true;
			}
		}

		if (SourceInfo.bUseOcclusionPlugin)
		{
			const FSpatializationParams* SourceSpatParams = &SourceInfo.SpatParams;

			// Move the audio buffer to the occlusion plugin buffer
			FAudioPluginSourceInputData AudioPluginInputData;
			AudioPluginInputData.SourceId = SourceId;
			AudioPluginInputData.AudioBuffer = &SourceInfo.SourceBuffer;
			AudioPluginInputData.SpatializationParams = SourceSpatParams;
			AudioPluginInputData.NumChannels = SourceInfo.NumInputChannels;
			AudioPluginInputData.AudioComponentId = SourceInfo.AudioComponentID;

			SourceInfo.AudioPluginOutputData.AudioBuffer.Reset();
			SourceInfo.AudioPluginOutputData.AudioBuffer.AddZeroed(AudioPluginInputData.AudioBuffer->Num());

			MixerDevice->OcclusionInterface->ProcessAudio(AudioPluginInputData, SourceInfo.AudioPluginOutputData);

			// Make sure the buffer counts didn't change and are still the same size
			AUDIO_MIXER_CHECK(SourceInfo.AudioPluginOutputData.AudioBuffer.Num() == NumSamples);

			// Copy the occlusion-processed data back to the source buffer and mix with the reverb plugin output buffer
			if (bShouldMixInReverb)
			{
				const float* ReverbPluginOutputBufferPtr = SourceInfo.ReverbPluginOutputBuffer.GetData();
				const float* AudioPluginOutputDataPtr = SourceInfo.AudioPluginOutputData.AudioBuffer.GetData();

				Audio::SumBuffers(ReverbPluginOutputBufferPtr, AudioPluginOutputDataPtr, PostDistanceAttenBufferPtr, NumSamples);
			}
			else
			{
				FMemory::Memcpy(PostDistanceAttenBufferPtr, SourceInfo.AudioPluginOutputData.AudioBuffer.GetData(), sizeof(float) * NumSamples);
			}
		}
		else if (bShouldMixInReverb)
		{
			const float* ReverbPluginOutputBufferPtr = SourceInfo.ReverbPluginOutputBuffer.GetData();
			Audio::MixInBufferFast(ReverbPluginOutputBufferPtr, PostDistanceAttenBufferPtr, NumSamples);
		}

		// If the source has HRTF processing enabled, run it through the spatializer
		if (SourceInfo.bUseHRTFSpatializer)
		{
			SCOPE_CYCLE_COUNTER(STAT_AudioMixerHRTF);

			AUDIO_MIXER_CHECK(SpatializationPlugin.IsValid());
			AUDIO_MIXER_CHECK(SourceInfo.NumInputChannels == 1);

			FAudioPluginSourceInputData AudioPluginInputData;
			AudioPluginInputData.AudioBuffer = &SourceInfo.SourceBuffer;
			AudioPluginInputData.NumChannels = SourceInfo.NumInputChannels;
			AudioPluginInputData.SourceId = SourceId;
			AudioPluginInputData.SpatializationParams = &SourceInfo.SpatParams;

			if (!MixerDevice->bSpatializationIsExternalSend)
			{
				SourceInfo.AudioPluginOutputData.AudioBuffer.Reset();
				SourceInfo.AudioPluginOutputData.AudioBuffer.AddZeroed(2 * NumOutputFrames);
			}

			SpatializationPlugin->ProcessAudio(AudioPluginInputData, SourceInfo.AudioPluginOutputData);

			// If this is an external send, we treat this source audio as if it was still a mono source
			// This will allow it to traditionally pan in the ComputeOutputBuffers function and be
			// sent to submixes (e.g. reverb) panned and mixed down. Certain submixes will want this spatial 
			// information in addition to the external send. We've already bypassed adding this source
			// to a base submix (e.g. master/eq, etc)
			if (MixerDevice->bSpatializationIsExternalSend)
			{
				// Otherwise our pre- and post-effect channels are the same as the input channels
				SourceInfo.NumPostEffectChannels = SourceInfo.NumInputChannels;

				// Set the ptr to use for post-effect buffers rather than the plugin output data (since the plugin won't have output audio data)
				SourceInfo.PostEffectBuffers = &SourceInfo.SourceBuffer;
			}
			else
			{
				// Otherwise, we are now a 2-channel file and should not be spatialized using normal 3d spatialization
				SourceInfo.NumPostEffectChannels = 2;
				SourceInfo.PostEffectBuffers = &SourceInfo.AudioPluginOutputData.AudioBuffer;
			}
		}
		else
		{
			// Otherwise our pre- and post-effect channels are the same as the input channels
			SourceInfo.NumPostEffectChannels = SourceInfo.NumInputChannels;

			// Set the ptr to use for post-effect buffers
			SourceInfo.PostEffectBuffers = &SourceInfo.SourceBuffer;
		}
	}

	void FMixerSourceManager::ComputePostSourceEffectBufferForIdRange(bool bGenerateBuses, const int32 SourceIdStart, const int32 SourceIdEnd)
	{
		SCOPE_CYCLE_COUNTER(STAT_AudioMixerSourceEffectBuffers);

		const bool bIsDebugModeEnabled = DebugSoloSources.Num() > 0;

		for (int32 SourceId = SourceIdStart; SourceId < SourceIdEnd; ++SourceId)
		{
			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			if (!SourceInfo.bIsBusy || !SourceInfo.bIsPlaying || SourceInfo.bIsPaused || (SourceInfo.bIsDone && SourceInfo.bEffectTailsDone)) 
			{
				continue;
			}

			const bool bIsBus = SourceInfo.BusId != INDEX_NONE;
			if ((bGenerateBuses && !bIsBus) || (!bGenerateBuses && bIsBus))
			{
				continue;
			}

			// Copy and store the current state of the pre-distance attenuation buffer before we feed it through our source effects
			// This is used by pre-effect sends
			if (SourceInfo.BusSends[(int32)EBusSendType::PreEffect].Num() > 0)
			{
				SourceInfo.PreEffectBuffer.Reset();
				SourceInfo.PreEffectBuffer.Append(SourceInfo.PreDistanceAttenuationBuffer);
			}

			float* PreDistanceAttenBufferPtr = SourceInfo.PreDistanceAttenuationBuffer.GetData();
			const int32 NumSamples = SourceInfo.PreDistanceAttenuationBuffer.Num();

			// Update volume fade information if we're stopping
			if (SourceInfo.bIsStopping)
			{
				const int32 NumFadeFrames = FMath::Min(SourceInfo.VolumeFadeNumFrames - SourceInfo.VolumeFadeFramePosition, NumOutputFrames);

				SourceInfo.VolumeFadeFramePosition += NumFadeFrames;
				SourceInfo.VolumeSourceDestination = SourceInfo.VolumeFadeSlope * (float) SourceInfo.VolumeFadeFramePosition + SourceInfo.VolumeFadeStart;

				if (FMath::IsNearlyZero(SourceInfo.VolumeSourceDestination, KINDA_SMALL_NUMBER))
				{
					SourceInfo.VolumeSourceDestination = 0.0f;
				}

				const int32 NumFadeSamples = NumFadeFrames * SourceInfo.NumInputChannels;

				Audio::FadeBufferFast(PreDistanceAttenBufferPtr, NumFadeSamples, SourceInfo.VolumeSourceStart, SourceInfo.VolumeSourceDestination);

				// Zero the rest of the buffer
				if (NumFadeFrames < NumOutputFrames)
				{
					int32 SamplesLeft = NumSamples - NumFadeSamples;
					FMemory::Memzero(&PreDistanceAttenBufferPtr[NumFadeSamples], sizeof(float)*SamplesLeft);
				}

				SourceInfo.VolumeSourceStart = SourceInfo.VolumeSourceDestination;
			}
			else
			{
				Audio::FadeBufferFast(PreDistanceAttenBufferPtr, NumSamples, SourceInfo.VolumeSourceStart, SourceInfo.VolumeSourceDestination);
				SourceInfo.VolumeSourceStart = SourceInfo.VolumeSourceDestination;
			}

			// Now process the effect chain if it exists
			if (!DisableSourceEffectsCvar && SourceInfo.SourceEffects.Num() > 0)
			{
				SourceInfo.SourceEffectInputData.CurrentVolume = SourceInfo.VolumeSourceDestination;

				SourceInfo.SourceEffectOutputData.AudioFrame.Reset();
				SourceInfo.SourceEffectOutputData.AudioFrame.AddZeroed(SourceInfo.NumInputChannels);

				SourceInfo.SourceEffectInputData.AudioFrame.Reset();
				SourceInfo.SourceEffectInputData.AudioFrame.AddZeroed(SourceInfo.NumInputChannels);

				float* SourceEffectInputDataFramePtr = SourceInfo.SourceEffectInputData.AudioFrame.GetData();
				float* SourceEffectOutputDataFramePtr = SourceInfo.SourceEffectOutputData.AudioFrame.GetData();

				// Process the effect chain for this buffer per frame
				for (int32 Sample = 0; Sample < NumSamples; Sample += SourceInfo.NumInputChannels)
				{
					// Get the buffer input sample
					for (int32 Chan = 0; Chan < SourceInfo.NumInputChannels; ++Chan)
					{
						SourceEffectInputDataFramePtr[Chan] = PreDistanceAttenBufferPtr[Sample + Chan];
					}

					for (int32 SourceEffectIndex = 0; SourceEffectIndex < SourceInfo.SourceEffects.Num(); ++SourceEffectIndex)
					{
						if (SourceInfo.SourceEffects[SourceEffectIndex]->IsActive())
						{
							SourceInfo.SourceEffects[SourceEffectIndex]->Update();

							SourceInfo.SourceEffects[SourceEffectIndex]->ProcessAudio(SourceInfo.SourceEffectInputData, SourceInfo.SourceEffectOutputData);

							// Copy the output of the effect into the input so the next effect will get the outputs audio
							for (int32 Chan = 0; Chan < SourceInfo.NumInputChannels; ++Chan)
							{
								SourceEffectInputDataFramePtr[Chan] = SourceEffectOutputDataFramePtr[Chan];
							}
						}
					}

					// Copy audio frame back to the buffer
					for (int32 Chan = 0; Chan < SourceInfo.NumInputChannels; ++Chan)
					{
						PreDistanceAttenBufferPtr[Sample + Chan] = SourceEffectInputDataFramePtr[Chan];
					}
				}
			}

			const bool bWasEffectTailsDone = SourceInfo.bEffectTailsDone;

			if (!DisableEnvelopeFollowingCvar)
			{
				// Compute the source envelope using pre-distance attenuation buffer
				float AverageSampleValue = Audio::GetAverageAmplitude(PreDistanceAttenBufferPtr, NumSamples);
				SourceInfo.SourceEnvelopeFollower.ProcessAudio(AverageSampleValue);

				// Copy the current value of the envelope follower (block-rate value)
				SourceInfo.SourceEnvelopeValue = SourceInfo.SourceEnvelopeFollower.GetCurrentValue();

				SourceInfo.bEffectTailsDone = SourceInfo.bEffectTailsDone || SourceInfo.SourceEnvelopeValue < ENVELOPE_TAIL_THRESHOLD;
			}
			else
			{
				SourceInfo.bEffectTailsDone = true;
			}

			if (!bWasEffectTailsDone && SourceInfo.bEffectTailsDone)
			{
				SourceInfo.SourceListener->OnEffectTailsDone();
			}

			// Only scale with distance attenuation and send to source audio to plugins if we're not in output-to-bus only mode
			if (!SourceInfo.bOutputToBusOnly && !DisableFilteringCvar)
			{
				float* PostDistanceAttenBufferPtr = SourceInfo.SourceBuffer.GetData();

				// Process the filters after the source effects
				for (int32 Frame = 0; Frame < NumOutputFrames; ++Frame)
				{
					const float LPFFreq = SourceInfo.LPFCutoffFrequencyParam.Update();
					const float HPFFreq = SourceInfo.HPFCutoffFrequencyParam.Update();

					SourceInfo.LowPassFilter.SetFrequency(LPFFreq);

					SourceInfo.HighPassFilter.SetFrequency(HPFFreq);
					SourceInfo.HighPassFilter.Update();

					int32 SampleIndex = SourceInfo.NumInputChannels * Frame;

					// Apply filters, if necessary.
					if (LPFFreq < MAX_FILTER_FREQUENCY)
					{
						//If we stopped processing the low pass filter, we need to clear the filter's memory to prevent pops.
						if (SourceInfo.bIsBypassingLPF)
						{
							SourceInfo.LowPassFilter.ClearMemory();
							SourceInfo.bIsBypassingLPF = false;
						}

						SourceInfo.LowPassFilter.ProcessAudio(&PreDistanceAttenBufferPtr[SampleIndex], &PostDistanceAttenBufferPtr[SampleIndex]);
					}
					else
					{
						SourceInfo.bIsBypassingLPF = true;
						FMemory::Memcpy(&PostDistanceAttenBufferPtr[SampleIndex], &PreDistanceAttenBufferPtr[SampleIndex], SourceInfo.NumInputChannels * sizeof(float));
					}

					if (!DisableHPFilteringCvar && HPFFreq > 0.0f)
					{
						//If we stopped processing the low pass filter, we need to clear the filter's memory to prevent pops.
						if (SourceInfo.bIsBypassingHPF)
						{
							SourceInfo.HighPassFilter.Reset();
							SourceInfo.bIsBypassingHPF = false;
						}

						SourceInfo.HighPassFilter.ProcessAudio(&PostDistanceAttenBufferPtr[SampleIndex], &PostDistanceAttenBufferPtr[SampleIndex]);
					}
					else
					{
						SourceInfo.bIsBypassingHPF = true;
					}
				}

				// Reset the volume and LPF param interpolations
				SourceInfo.LPFCutoffFrequencyParam.Reset();
				SourceInfo.HPFCutoffFrequencyParam.Reset();

				// Apply distance attenuation
				ApplyDistanceAttenuation(SourceInfo, NumSamples);

				// Send source audio to plugins
				ComputePluginAudio(SourceInfo, SourceId, NumSamples);
			}
			else if (DisableFilteringCvar)
			{
				float* PostDistanceAttenBufferPtr = SourceInfo.SourceBuffer.GetData();
				FMemory::Memcpy(PostDistanceAttenBufferPtr, PreDistanceAttenBufferPtr, NumSamples * sizeof(float));

				// Apply distance attenuation
				ApplyDistanceAttenuation(SourceInfo, NumSamples);

				// Send source audio to plugins
				ComputePluginAudio(SourceInfo, SourceId, NumSamples);
			}

			// Check the source effect tails condition
			if (SourceInfo.bIsLastBuffer && SourceInfo.bEffectTailsDone)
			{
				// If we're done and our tails our done, clear everything out
				SourceInfo.CurrentFrameValues.Reset();
				SourceInfo.NextFrameValues.Reset();
				SourceInfo.CurrentPCMBuffer = nullptr;
			}
		}
	}

	void FMixerSourceManager::ComputeOutputBuffersForIdRange(const bool bGenerateBuses, const int32 SourceIdStart, const int32 SourceIdEnd)
	{
		SCOPE_CYCLE_COUNTER(STAT_AudioMixerSourceOutputBuffers);

		for (int32 SourceId = SourceIdStart; SourceId < SourceIdEnd; ++SourceId)
		{
			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			// Don't need to compute anything if the source is not playing or paused (it will remain at 0.0 volume)
			// Note that effect chains will still be able to continue to compute audio output. The source output 
			// will simply stop being read from.
			if (!SourceInfo.bIsBusy || !SourceInfo.bIsPlaying || (SourceInfo.bIsDone && SourceInfo.bEffectTailsDone))
			{
				continue;
			}

			// If we're in generate buses mode and not a bus, or vice versa, or if we're set to only output audio to buses.
			// If set to output buses, no need to do any panning for the source. The buses will do the panning.
			const bool bIsBus = SourceInfo.BusId != INDEX_NONE;
			if ((bGenerateBuses && !bIsBus) || (!bGenerateBuses && bIsBus) || SourceInfo.bOutputToBusOnly)
			{
				continue;
			}

			// Perform output buffer computation for each submix channel type output
			for (int32 SubmixInfoId = 0; SubmixInfoId < (int32)ESubmixChannelFormat::Count; ++SubmixInfoId)
			{
				FSubmixChannelTypeInfo& ChannelInfo = SourceInfo.SubmixChannelInfo[SubmixInfoId];

				if (!ChannelInfo.bUsed)
				{
					continue;
				}

				ESubmixChannelFormat ChannelFormat = (ESubmixChannelFormat)SubmixInfoId;
				if (ChannelFormat == ESubmixChannelFormat::Ambisonics)
				{
					// there are no channel maps for ambisonics format since it's a "pass through" to the ambisonics submix
					ChannelInfo.OutputBuffer.Reset();
					ChannelInfo.OutputBuffer.AddZeroed(NumOutputFrames * SourceInfo.NumInputChannels);

					// Simply copy the post-effect buffers to the output buffer
					FMemory::Memcpy((void*)ChannelInfo.OutputBuffer.GetData(), (void*)SourceInfo.PostEffectBuffers->GetData(), SourceInfo.PostEffectBuffers->Num()*sizeof(float));
				}
				else
				{
					int32 NumOutputChannels = MixerDevice->GetNumChannelsForSubmixFormat((ESubmixChannelFormat)SubmixInfoId);

					ChannelInfo.OutputBuffer.Reset();
					ChannelInfo.OutputBuffer.AddZeroed(NumOutputFrames * NumOutputChannels);

					// If we're paused, then early return now
					if (SourceInfo.bIsPaused)
					{
						continue;
					}

					float* OutputBufferPtr = ChannelInfo.OutputBuffer.GetData();
					float* PostEffetBufferPtr = SourceInfo.PostEffectBuffers->GetData();

					// Apply speaker mapping
					for (int32 Frame = 0; Frame < NumOutputFrames; ++Frame)
					{
						const int32 PostEffectChannels = SourceInfo.NumPostEffectChannels;

						float SourceSampleValue = 0.0f;

						// Make sure that our channel map is appropriate for the source channel and output channel count!
						ChannelInfo.ChannelMapParam.UpdateChannelMap();

						// For each source channel, compute the output channel mapping
						for (int32 SourceChannel = 0; SourceChannel < PostEffectChannels; ++SourceChannel)
						{
							const int32 SourceSampleIndex = Frame * PostEffectChannels + SourceChannel;
							SourceSampleValue = PostEffetBufferPtr[SourceSampleIndex];

							for (int32 OutputChannel = 0; OutputChannel < NumOutputChannels; ++OutputChannel)
							{
								// Look up the channel map value (maps input channels to output channels) for the source
								// This is the step that either applies the spatialization algorithm or just maps a 2d sound
								const int32 ChannelMapIndex = NumOutputChannels * SourceChannel + OutputChannel;
								const float ChannelMapValue = ChannelInfo.ChannelMapParam.GetChannelValue(ChannelMapIndex);

								// If we have a non-zero sample value, write it out. Note that most 3d audio channel maps
								// for surround sound will result in 0.0 sample values so this branch should save a bunch of multiplies + adds
								if (ChannelMapValue > 0.0f)
								{
									// Scale the input source sample for this source channel value
									const float SampleValue = SourceSampleValue * ChannelMapValue;
									const int32 OutputSampleIndex = Frame * NumOutputChannels + OutputChannel;

									OutputBufferPtr[OutputSampleIndex] += SampleValue;
								}
							}
						}
					}

					// Reset the channel map param interpolation
					ChannelInfo.ChannelMapParam.ResetInterpolation();
				}
			}
		}
	}

	void FMixerSourceManager::GenerateSourceAudio(const bool bGenerateBuses, const int32 SourceIdStart, const int32 SourceIdEnd)
	{
		// Buses generate their input buffers independently
		// Get the next block of frames from the source buffers
		ComputeSourceBuffersForIdRange(bGenerateBuses, SourceIdStart, SourceIdEnd);

		// Compute the audio source buffers after their individual effect chain processing
		ComputePostSourceEffectBufferForIdRange(bGenerateBuses, SourceIdStart, SourceIdEnd);

		// Get the audio for the output buffers
		ComputeOutputBuffersForIdRange(bGenerateBuses, SourceIdStart, SourceIdEnd);
	}

	void FMixerSourceManager::GenerateSourceAudio(const bool bGenerateBuses)
	{
		// If there are no buses, don't need to do anything here
		if (bGenerateBuses && !Buses.Num())
		{
			return;
		}

		if (NumSourceWorkers > 0 && !DisableParallelSourceProcessingCvar)
		{
			AUDIO_MIXER_CHECK(SourceWorkers.Num() == NumSourceWorkers);
			for (int32 i = 0; i < SourceWorkers.Num(); ++i)
			{
				FAudioMixerSourceWorker& Worker = SourceWorkers[i]->GetTask();
				Worker.SetGenerateBuses(bGenerateBuses);

				SourceWorkers[i]->StartBackgroundTask();
			}

			for (int32 i = 0; i < SourceWorkers.Num(); ++i)
			{
				SourceWorkers[i]->EnsureCompletion();
			}
		}
		else
		{
			GenerateSourceAudio(bGenerateBuses, 0, NumTotalSources);
		}
	}

	void FMixerSourceManager::MixOutputBuffers(const int32 SourceId, const ESubmixChannelFormat InSubmixChannelType, const float SendLevel, AlignedFloatBuffer& OutWetBuffer) const
	{
		if (SendLevel > 0.0f)
		{
			const FSourceInfo& SourceInfo = SourceInfos[SourceId];

			// Don't need to mix into submixes if the source is paused
			if (!SourceInfo.bIsPaused && !SourceInfo.bIsDone && SourceInfo.bIsPlaying)
			{
				const FSubmixChannelTypeInfo& ChannelInfo = SourceInfo.SubmixChannelInfo[(int32)InSubmixChannelType];
// 				if (InSubmixChannelType == ESubmixChannelFormat::Ambisonics && SourceInfo.NumInputChannels == 1)
// 				{
// 					// TODO: encode 3d sources (non-ambisonics files) to ambisonics
// 				}

				check(ChannelInfo.bUsed);
				check(ChannelInfo.OutputBuffer.Num() == OutWetBuffer.Num());

				const float* SourceOutputBufferPtr = ChannelInfo.OutputBuffer.GetData();
				const int32 OutWetBufferSize = OutWetBuffer.Num();
				float* OutWetBufferPtr = OutWetBuffer.GetData();

				Audio::MixInBufferFast(SourceOutputBufferPtr, OutWetBufferPtr, OutWetBufferSize, SendLevel);
			}
		}
	}

	void FMixerSourceManager::UpdateDeviceChannelCount(const int32 InNumOutputChannels)
	{
		NumOutputSamples = NumOutputFrames * MixerDevice->GetNumDeviceChannels();

		// Update all source's to appropriate channel maps
		for (int32 SourceId = 0; SourceId < NumTotalSources; ++SourceId)
		{
			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			// Don't need to do anything if it's not active
			if (!SourceInfo.bIsActive)
			{
				continue;
			}

			FSubmixChannelTypeInfo& ChannelTypeInfo = SourceInfo.SubmixChannelInfo[(int32)ESubmixChannelFormat::Device];

			if (ChannelTypeInfo.bUsed)
			{
				SourceInfo.ScratchChannelMap.Reset();
				const int32 NumSoureChannels = SourceInfo.bUseHRTFSpatializer ? 2 : SourceInfo.NumInputChannels;

				// If this is a 3d source, then just zero out the channel map, it'll cause a temporary blip
				// but it should reset in the next tick
				if (SourceInfo.bIs3D)
				{
					GameThreadInfo.bNeedsSpeakerMap[SourceId] = true;
					SourceInfo.ScratchChannelMap.AddZeroed(NumSoureChannels * InNumOutputChannels);
				}
				// If it's a 2D sound, then just get a new channel map appropriate for the new device channel count
				else
				{
					SourceInfo.ScratchChannelMap.Reset();
					MixerDevice->Get2DChannelMap(SourceInfo.bIsVorbis, ESubmixChannelFormat::Device, NumSoureChannels, SourceInfo.bIsCenterChannelOnly, SourceInfo.ScratchChannelMap);
				}
			
				ChannelTypeInfo.ChannelMapParam.SetChannelMap(SourceInfo.ScratchChannelMap, NumOutputFrames);
			}
		}
	}

	void FMixerSourceManager::UpdateSourceEffectChain(const uint32 InSourceEffectChainId, const TArray<FSourceEffectChainEntry>& InSourceEffectChain, const bool bPlayEffectChainTails)
	{
		AudioMixerThreadCommand([this, InSourceEffectChainId, InSourceEffectChain, bPlayEffectChainTails]()
		{
			FSoundEffectSourceInitData InitData;
			InitData.AudioClock = MixerDevice->GetAudioClock();
			InitData.SampleRate = MixerDevice->SampleRate;

			for (int32 SourceId = 0; SourceId < NumTotalSources; ++SourceId)
			{
				FSourceInfo& SourceInfo = SourceInfos[SourceId];

				if (SourceInfo.SourceEffectChainId == InSourceEffectChainId)
				{
					SourceInfo.bEffectTailsDone = !bPlayEffectChainTails;

					// Check to see if the chain didn't actually change
					TArray<FSoundEffectSource *>& ThisSourceEffectChain = SourceInfo.SourceEffects;
					bool bReset = false;
					if (InSourceEffectChain.Num() == ThisSourceEffectChain.Num())
					{
						for (int32 SourceEffectId = 0; SourceEffectId < ThisSourceEffectChain.Num(); ++SourceEffectId)
						{
							const FSourceEffectChainEntry& ChainEntry = InSourceEffectChain[SourceEffectId];

							FSoundEffectSource* SourceEffectInstance = ThisSourceEffectChain[SourceEffectId];
							if (!SourceEffectInstance->IsParentPreset(ChainEntry.Preset))
							{
								// As soon as one of the effects change or is not the same, then we need to rebuild the effect graph
								bReset = true;
								break;
							}

							// Otherwise just update if it's just to bypass
							SourceEffectInstance->SetEnabled(!ChainEntry.bBypass);
						}
					}
					else
					{
						bReset = true;
					}

					if (bReset)
					{
						InitData.NumSourceChannels = SourceInfo.NumInputChannels;

						if (SourceInfo.NumInputFrames != INDEX_NONE)
						{
							InitData.SourceDuration = (float)SourceInfo.NumInputFrames / InitData.SampleRate;
						}
						else
						{
							// Procedural sound waves have no known duration
							InitData.SourceDuration = (float)INDEX_NONE;
						}

						// First reset the source effect chain
						ResetSourceEffectChain(SourceId);

						// Rebuild it
						BuildSourceEffectChain(SourceId, InitData, InSourceEffectChain);
					}
				}
			}
		});
	}

	const float* FMixerSourceManager::GetPreDistanceAttenuationBuffer(const int32 SourceId) const
	{
		return SourceInfos[SourceId].PreDistanceAttenuationBuffer.GetData();
	}

	const float* FMixerSourceManager::GetPreEffectBuffer(const int32 SourceId) const
	{
		return SourceInfos[SourceId].PreEffectBuffer.GetData();
	}

	const float* FMixerSourceManager::GetPreviousBusBuffer(const int32 SourceId) const
	{
		const uint32 BusId = SourceInfos[SourceId].BusId;
		const FMixerBus* MixerBus = Buses.Find(BusId);
		return MixerBus->GetPreviousBusBuffer();
	}

	int32 FMixerSourceManager::GetNumChannels(const int32 SourceId) const
	{
		return SourceInfos[SourceId].NumInputChannels;
	}

	bool FMixerSourceManager::IsBus(const int32 SourceId) const
	{
		return SourceInfos[SourceId].BusId != INDEX_NONE;
	}

	void FMixerSourceManager::ComputeNextBlockOfSamples()
	{
		AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

		SCOPE_CYCLE_COUNTER(STAT_AudioMixerSourceManagerUpdate);

		// Get the this blocks commands before rendering audio
		if (bPumpQueue)
		{
			bPumpQueue = false;
			PumpCommandQueue();
		}

		// Update pending tasks and release them if they're finished
		UpdatePendingReleaseData();

		// First generate non-bus audio (bGenerateBuses = false)
		GenerateSourceAudio(false);

		// Now mix in the non-bus audio into the buses
		ComputeBuses();

		// Now generate bus audio (bGenerateBuses = true)
		GenerateSourceAudio(true);

		// Update the buses now
		UpdateBuses();

		// Let the plugin know we finished processing all sources
		if (bUsingSpatializationPlugin)
		{
			AUDIO_MIXER_CHECK(SpatializationPlugin.IsValid());
			SpatializationPlugin->OnAllSourcesProcessed();
		}

		// Update the game thread copy of source doneness
		for (int32 SourceId = 0; SourceId < NumTotalSources; ++SourceId)
		{		
			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			// Check for the stopping condition to "turn the sound off"
			if (SourceInfo.bIsLastBuffer)
			{
				if (!SourceInfo.bIsDone)
				{
					SourceInfo.bIsDone = true;

					// Notify that we're now done with this source
					SourceInfo.SourceListener->OnDone();
				}
			}
		}
	}

	void FMixerSourceManager::ClearStoppingSounds()
	{
		for (int32 SourceId = 0; SourceId < NumTotalSources; ++SourceId)
		{
			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			if (!SourceInfo.bIsDone && SourceInfo.bIsStopping && SourceInfo.VolumeSourceDestination == 0.0f)
			{
				SourceInfo.bIsStopping = false;
				SourceInfo.bIsDone = true;
				SourceInfo.SourceListener->OnDone();
			}

		}
	}

	void FMixerSourceManager::AudioMixerThreadCommand(TFunction<void()> InFunction)
	{
		// Add the function to the command queue
		int32 AudioThreadCommandIndex = AudioThreadCommandBufferIndex.GetValue();
		CommandBuffers[AudioThreadCommandIndex].SourceCommandQueue.Add(MoveTemp(InFunction));
	}

	void FMixerSourceManager::PumpCommandQueue()
	{
		int32 CurrentRenderThreadIndex = RenderThreadCommandBufferIndex.GetValue();

		FCommands& Commands = CommandBuffers[CurrentRenderThreadIndex];

		// Pop and execute all the commands that came since last update tick
		for (int32 Id = 0; Id < Commands.SourceCommandQueue.Num(); ++Id)
		{
			TFunction<void()>& CommandFunction = Commands.SourceCommandQueue[Id];
			CommandFunction();
		}

		Commands.SourceCommandQueue.Reset();

		RenderThreadCommandBufferIndex.Set(!CurrentRenderThreadIndex);
	}

	void FMixerSourceManager::UpdatePendingReleaseData(bool bForceWait)
	{
		// Check for any pending delete procedural sound waves
		for (int32 SourceId = 0; SourceId < NumTotalSources; ++SourceId)
		{
			FSourceInfo& SourceInfo = SourceInfos[SourceId];
			if (SourceInfo.MixerSourceBuffer.IsValid())
			{
				// If we've been flagged to begin destroy
				if (SourceInfo.MixerSourceBuffer->IsBeginDestroy())
				{
					SourceInfo.MixerSourceBuffer->ClearSoundWave();
						
					if (!SourceInfo.bIsDone)
					{
						SourceInfo.bIsDone = true;
						SourceInfo.SourceListener->OnDone();
					}

					// Clear out the mixer source buffer
					SourceInfo.MixerSourceBuffer = nullptr;

					// Set the sound to be done playing
					// This will flag the sound to be released
					SourceInfo.bIsPlaying = false;
					SourceInfo.bIsPaused = false;
					SourceInfo.bIsActive = false;
					SourceInfo.bIsStopping = false;
				}
			}
		}


		// Don't block, but let tasks finish naturally
		for (int32 i = PendingSourceBuffers.Num() - 1; i >= 0; --i)
		{
			FMixerSourceBuffer* MixerSourceBuffer = PendingSourceBuffers[i].Get();

			bool bDeleteSourceBuffer = true;
			if (bForceWait)
			{
				MixerSourceBuffer->EnsureAsyncTaskFinishes();
			}
			else if (!MixerSourceBuffer->IsAsyncTaskDone())
			{			
				bDeleteSourceBuffer = false;
			}

			if (bDeleteSourceBuffer)
			{
				PendingSourceBuffers.RemoveAtSwap(i, 1, false);
			}
		}
	}

}
