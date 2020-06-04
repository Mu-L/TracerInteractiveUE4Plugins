// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioMixerSourceManager.h"
#include "AudioMixerSourceBuffer.h"
#include "AudioMixerSource.h"
#include "AudioMixerDevice.h"
#include "AudioMixerSourceVoice.h"
#include "AudioMixerSubmix.h"
#include "IAudioExtensionPlugin.h"
#include "AudioMixer.h"
#include "SoundFieldRendering.h"
#include "ProfilingDebugging/CsvProfiler.h"

// Link to "Audio" profiling category
CSV_DECLARE_CATEGORY_MODULE_EXTERN(AUDIOMIXERCORE_API, Audio);
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

static int32 FlushCommandBufferOnTimeoutCvar = 0;
FAutoConsoleVariableRef CVarFlushCommandBufferOnTimeout(
	TEXT("au.FlushCommandBufferOnTimeout"),
	FlushCommandBufferOnTimeoutCvar,
	TEXT("When set to 1, flushes audio render thread synchronously when our fence has timed out.\n")
	TEXT("0: Not Disabled, 1: Disabled"),
	ECVF_Default);

static int32 CommandBufferFlushWaitTimeMsCvar = 1000;
FAutoConsoleVariableRef CVarCommandBufferFlushWaitTimeMs(
	TEXT("au.CommandBufferFlushWaitTimeMs"),
	CommandBufferFlushWaitTimeMsCvar,
	TEXT("How long to wait for the command buffer flush to complete.\n"),
	ECVF_Default);


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
		, MaxChannelsSupportedBySpatializationPlugin(1)
	{
		// Get a manual resetable event
		const bool bIsManualReset = true;
		CommandsProcessedEvent = FPlatformProcess::GetSynchEventFromPool(bIsManualReset);
		check(CommandsProcessedEvent != nullptr);

		// Immediately trigger the command processed in case a flush happens before the audio thread swaps command buffers
		CommandsProcessedEvent->Trigger();
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

		FPlatformProcess::ReturnSynchEventToPool(CommandsProcessedEvent);
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

		// Populate downmix array:
		DownmixDataArray.Reset();
		for (int32 Index = 0; Index < NumTotalSources; Index++)
		{
			DownmixDataArray.Emplace(2, MixerDevice->GetNumDeviceChannels(), NumOutputFrames);
		}
		

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
			SourceInfo.bOutputToBusOnly = false;
			SourceInfo.bIsVorbis = false;
			SourceInfo.bIsBypassingLPF = false;
			SourceInfo.bIsBypassingHPF = false;
			SourceInfo.bIsModulationUpdated = false;

#if AUDIO_MIXER_ENABLE_DEBUG_MODE
			SourceInfo.bIsDebugMode = false;
#endif // AUDIO_MIXER_ENABLE_DEBUG_MODE

			SourceInfo.NumInputChannels = 0;
			SourceInfo.NumPostEffectChannels = 0;
			SourceInfo.NumInputFrames = 0;
		}
		
		GameThreadInfo.bIsBusy.AddDefaulted(NumTotalSources);
		GameThreadInfo.bNeedsSpeakerMap.AddDefaulted(NumTotalSources);
		GameThreadInfo.bIsDebugMode.AddDefaulted(NumTotalSources);
		GameThreadInfo.bIsUsingHRTFSpatializer.AddDefaulted(NumTotalSources);
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
			SourceInfo.SourceEffectScratchBuffer.Reset(NumOutputFrames * 8);
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
			MaxChannelsSupportedBySpatializationPlugin = MixerDevice->MaxChannelsSupportedBySpatializationPlugin;
		}

		bInitialized = true;
		bPumpQueue = false;
	}

	void FMixerSourceManager::Update(bool bTimedOut)
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

		if (FPlatformProcess::SupportsMultithreading())
		{
			// If the command was triggered, then we want to do a swap of command buffers
			if (CommandsProcessedEvent->Wait(0))
			{
				int32 CurrentGameIndex = !RenderThreadCommandBufferIndex.GetValue();

				// This flags the audio render thread to be able to pump the next batch of commands
				// And will allow the audio thread to write to a new command slot
				const int32 NextIndex = (CurrentGameIndex + 1) & 1;

				FCommands& NextCommandBuffer = CommandBuffers[NextIndex];

				// Make sure we've actually emptied the command queue from the render thread before writing to it
				if (FlushCommandBufferOnTimeoutCvar && NextCommandBuffer.SourceCommandQueue.Num() != 0)
				{
					UE_LOG(LogAudioMixer, Warning, TEXT("Audio render callback stopped. Flushing %d commands."), NextCommandBuffer.SourceCommandQueue.Num());

					// Pop and execute all the commands that came since last update tick
					for (int32 Id = 0; Id < NextCommandBuffer.SourceCommandQueue.Num(); ++Id)
					{
						TFunction<void()>& CommandFunction = NextCommandBuffer.SourceCommandQueue[Id];
						CommandFunction();
						NumCommands.Decrement();
					}

					NextCommandBuffer.SourceCommandQueue.Reset();
				}

				// Here we ensure that we block for any pending calls to AudioMixerThreadCommand.
				FScopeLock ScopeLock(&CommandBufferIndexCriticalSection);
				RenderThreadCommandBufferIndex.Set(CurrentGameIndex);

				CommandsProcessedEvent->Reset();
			}
		}
		else
		{
			int32 CurrentRenderIndex = RenderThreadCommandBufferIndex.GetValue();
			int32 CurrentGameIndex = !RenderThreadCommandBufferIndex.GetValue();
			check(CurrentGameIndex == 0 || CurrentGameIndex == 1);
			check(CurrentRenderIndex == 0 || CurrentRenderIndex == 1);

			// If these values are the same, that means the audio render thread has finished the last buffer queue so is ready for the next block
			if (CurrentRenderIndex == CurrentGameIndex)
			{
				// This flags the audio render thread to be able to pump the next batch of commands
				// And will allow the audio thread to write to a new command slot
				const int32 NextIndex = !CurrentGameIndex;

				// Make sure we've actually emptied the command queue from the render thread before writing to it
				if (CommandBuffers[NextIndex].SourceCommandQueue.Num() != 0)
				{
					UE_LOG(LogAudioMixer, Warning, TEXT("Source command queue not empty: %d"), CommandBuffers[NextIndex].SourceCommandQueue.Num());
				}
				bPumpQueue = true;
			}
		}

	}

	void FMixerSourceManager::ReleaseSource(const int32 SourceId)
	{
		AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(bInitialized);
		AUDIO_MIXER_CHECK(MixerSources[SourceId] != nullptr);

		if (MixerSources[SourceId] == nullptr)
		{
			UE_LOG(LogAudioMixer, Warning, TEXT("Ignoring double release of SourceId: %i"), SourceId);
			return;
		}

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
			FMixerSubmixPtr SubmixPtr = SubmixSendItem.Submix.Pin();
			if (SubmixPtr.IsValid())
			{
				SubmixPtr->RemoveSourceVoice(MixerSources[SourceId]);
			}
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

		SourceInfo.LowPassFilter.Reset();
		SourceInfo.HighPassFilter.Reset();
		SourceInfo.CurrentPCMBuffer = nullptr;
		SourceInfo.CurrentAudioChunkNumFrames = 0;
		SourceInfo.SourceBuffer.Reset();
		SourceInfo.PreDistanceAttenuationBuffer.Reset();
		SourceInfo.SourceEffectScratchBuffer.Reset();
		SourceInfo.AudioPluginOutputData.AudioBuffer.Reset();
		SourceInfo.CurrentFrameValues.Reset();
		SourceInfo.NextFrameValues.Reset();
		SourceInfo.CurrentFrameAlpha = 0.0f;
		SourceInfo.CurrentFrameIndex = 0;
		SourceInfo.NumFramesPlayed = 0;
		SourceInfo.StartTime = 0.0;
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
		SourceInfo.bIsExternalSend = false;
		SourceInfo.bUseOcclusionPlugin = false;
		SourceInfo.bUseReverbPlugin = false;
		SourceInfo.bHasStarted = false;
		SourceInfo.bOutputToBusOnly = false;
		SourceInfo.bIsBypassingLPF = false;
		SourceInfo.bIsBypassingHPF = false;

#if AUDIO_MIXER_ENABLE_DEBUG_MODE
		SourceInfo.bIsDebugMode = false;
		SourceInfo.DebugName = FString();
#endif //AUDIO_MIXER_ENABLE_DEBUG_MODE

		SourceInfo.NumInputChannels = 0;
		SourceInfo.NumPostEffectChannels = 0;

		GameThreadInfo.bNeedsSpeakerMap[SourceId] = false;
	}

	void FMixerSourceManager::BuildSourceEffectChain(const int32 SourceId, FSoundEffectSourceInitData& InitData, const TArray<FSourceEffectChainEntry>& InSourceEffectChain)
	{
		// Create new source effects. The memory will be owned by the source manager.
		FScopeLock ScopeLock(&EffectChainMutationCriticalSection);
		for (const FSourceEffectChainEntry& ChainEntry : InSourceEffectChain)
		{
			// Presets can have null entries
			if (!ChainEntry.Preset)
			{
				continue;
			}

			// Get this source effect presets unique id so instances can identify their originating preset object
			const uint32 PresetUniqueId = ChainEntry.Preset->GetUniqueID();
			InitData.ParentPresetUniqueId = PresetUniqueId;

			TSoundEffectSourcePtr NewEffect = USoundEffectPreset::CreateInstance<FSoundEffectSourceInitData, FSoundEffectSource>(InitData, *ChainEntry.Preset);
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
		FScopeLock ScopeLock(&EffectChainMutationCriticalSection);
		{
			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			for (int32 i = 0; i < SourceInfo.SourceEffects.Num(); ++i)
			{
				USoundEffectPreset::UnregisterInstance(SourceInfo.SourceEffects[i]);
			}
			SourceInfo.SourceEffects.Reset();

			for (int32 i = 0; i < SourceInfo.SourceEffectPresets.Num(); ++i)
			{
				SourceInfo.SourceEffectPresets[i] = nullptr;
			}
			SourceInfo.SourceEffectPresets.Reset();
		}
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

		GameThreadInfo.bIsUsingHRTFSpatializer[SourceId] = InitParams.bUseHRTFSpatialization;

		// Create the modulation plugin source effect
		if (InitParams.ModulationPluginSettings != nullptr)
		{
			MixerDevice->ModulationInterface->OnInitSource(SourceId, InitParams.AudioComponentUserID, InitParams.NumInputChannels, *InitParams.ModulationPluginSettings);
		}

		AudioMixerThreadCommand([this, SourceId, InitParams]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);
			AUDIO_MIXER_CHECK(InitParams.SourceVoice != nullptr);

			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			// Initialize the mixer source buffer decoder with the given mixer buffer
			SourceInfo.MixerSourceBuffer = InitParams.MixerSourceBuffer;
			SourceInfo.MixerSourceBuffer->Init();
			SourceInfo.MixerSourceBuffer->OnBeginGenerate();

			SourceInfo.bIsPlaying = false;
			SourceInfo.bIsPaused = false;
			SourceInfo.bIsStopping = false;
			SourceInfo.bIsActive = true;
			SourceInfo.bIsBusy = true;
			SourceInfo.bIsDone = false;
			SourceInfo.bIsLastBuffer = false;
			SourceInfo.bUseHRTFSpatializer = InitParams.bUseHRTFSpatialization;
			SourceInfo.bIsExternalSend = InitParams.bIsExternalSend;
			SourceInfo.bIsVorbis = InitParams.bIsVorbis;
			SourceInfo.bIsAmbisonics = InitParams.bIsAmbisonics;
			SourceInfo.AudioComponentID = InitParams.AudioComponentID;
			SourceInfo.bIsAmbisonics = InitParams.bIsAmbisonics;

			// Call initialization from the render thread so anything wanting to do any initialization here can do so (e.g. procedural sound waves)
			SourceInfo.SourceListener = InitParams.SourceListener;
			SourceInfo.SourceListener->OnBeginGenerate();

			SourceInfo.NumInputChannels = InitParams.NumInputChannels;
			SourceInfo.NumInputFrames = InitParams.NumInputFrames;

			// Initialize the number of per-source LPF filters based on input channels
			SourceInfo.LowPassFilter.Init(MixerDevice->SampleRate, InitParams.NumInputChannels);
			SourceInfo.HighPassFilter.Init(MixerDevice->SampleRate, InitParams.NumInputChannels);

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
						Bus->AddInstanceId(SourceId, InitParams.NumInputChannels);
					}
					else
					{
						// If the bus is not registered, make a new entry
						FMixerBus NewBusData(this, InitParams.NumInputChannels, NumOutputFrames);

						NewBusData.AddInstanceId(SourceId, InitParams.NumInputChannels);

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

			// Initialize a new downmix data:
			check(SourceId < SourceInfos.Num());
			const int32 SourceInputChannels = (SourceInfo.bUseHRTFSpatializer && !SourceInfo.bIsExternalSend) ? 2 : SourceInfo.NumInputChannels;
			FSourceDownmixData& DownmixData = InitializeDownmixForSource(SourceId, SourceInputChannels, MixerDevice->GetDeviceOutputChannels(), NumOutputFrames);

			if (SourceInfo.bIsAmbisonics)
			{
				DownmixData.AmbisonicsDecoder = CreateDefaultSourceAmbisonicsDecoder(MixerDevice);
			}

			for (int32 i = 0; i < InitParams.SubmixSends.Num(); ++i)
			{
				const FMixerSourceSubmixSend& MixerSubmixSend = InitParams.SubmixSends[i];

				FMixerSubmixPtr SubmixPtr = MixerSubmixSend.Submix.Pin();
				if (SubmixPtr.IsValid())
				{
					SourceInfo.SubmixSends.Add(MixerSubmixSend);
					SubmixPtr->AddOrSetSourceVoice(InitParams.SourceVoice, MixerSubmixSend.SendLevel);

					
					if (SubmixPtr->IsSoundfieldSubmix())
					{
						FSoundfieldEncodingKey Key = SubmixPtr->GetKeyForSubmixEncoding();
						FSubmixSoundfieldData& SubmixSoundfieldInfo = GetChannelInfoForFormat(Key, DownmixData);

						// Set up encoder (or transcoder if this is an ambisonics source).
						ISoundfieldFactory* Factory = SubmixPtr->GetSoundfieldFactory();
						check(Factory);

						FAudioPluginInitializationParams PluginInitParams = SubmixPtr->GetInitializationParamsForSoundfieldStream();
						PluginInitParams.NumOutputChannels = SourceInputChannels;

						SubmixSoundfieldInfo.EncoderSettings = SubmixPtr->GetSoundfieldSettings().Duplicate();

						if (SourceInfo.bIsAmbisonics)
						{
							// If this soundfield submix can transcode our ambisonics format, set up a transcoder stream.
							if (Factory->GetSoundfieldFormatName() == GetUnrealAmbisonicsFormatName())
							{
								SubmixSoundfieldInfo.bIsUnrealAmbisonicsSubmix = true;
							}
							else if (Factory->CanTranscodeFromSoundfieldFormat(GetUnrealAmbisonicsFormatName(), GetAmbisonicsSourceDefaultSettings()))
							{
								SubmixSoundfieldInfo.AmbiTranscoder = Factory->CreateTranscoderStream(GetUnrealAmbisonicsFormatName(), GetAmbisonicsSourceDefaultSettings(), Factory->GetSoundfieldFormatName(), *SubmixSoundfieldInfo.EncoderSettings, PluginInitParams);
							}
						}
						else
						{
							check(SubmixSoundfieldInfo.EncoderSettings.IsValid());

							SubmixSoundfieldInfo.Encoder = Factory->CreateEncoderStream(PluginInitParams, *SubmixSoundfieldInfo.EncoderSettings);
						}

						SubmixSoundfieldInfo.EncodedPacket = Factory->CreateEmptyPacket();
					}
					else
					{
						// Flag this source as needing to downmix its audio.
						DownmixData.bIsSourceBeingSentToDeviceSubmix = true;
					}
				}
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

		if (MixerDevice->ModulationInterface)
		{
			MixerDevice->ModulationInterface->OnReleaseSource(SourceId);
		}

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

	void FMixerSourceManager::UpdateModulationControls(const int32 SourceId, const FSoundModulationControls& InControls)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		const FSoundModulationControls UpdatedControls = InControls;
		AudioMixerThreadCommand([this, SourceId, UpdatedControls]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

			SourceInfos[SourceId].ModulationControls = UpdatedControls;
			SourceInfos[SourceId].bIsModulationUpdated = true;
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

	void FMixerSourceManager::SetChannelMap(const int32 SourceId, const uint32 NumInputChannels, const Audio::AlignedFloatBuffer& ChannelMap, const bool bInIs3D, const bool bInIsCenterChannelOnly)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AudioMixerThreadCommand([this, SourceId, NumInputChannels, ChannelMap, bInIs3D, bInIsCenterChannelOnly]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

			check(NumOutputFrames > 0);

			FSourceInfo& SourceInfo = SourceInfos[SourceId];
			FSourceDownmixData& DownmixData = DownmixDataArray[SourceId];

			if (DownmixData.NumInputChannels != NumInputChannels && !SourceInfo.bUseHRTFSpatializer)
			{
				// This means that this source has been reinitialized as a different source while this command was in flight,
				// In which case it is of no use to us. Exit.
				return;
			}

			// Set whether or not this is a 3d channel map and if its center channel only. Used for reseting channel maps on device change.
			SourceInfo.bIs3D = bInIs3D;
			SourceInfo.bIsCenterChannelOnly = bInIsCenterChannelOnly;

			FSubmixChannelData& ChannelTypeInfo = GetChannelInfoForDevice(DownmixData);

			// Fix up the channel map in case device output count changed
			const uint32 ChannelMapSize = ChannelTypeInfo.ChannelMap.CopySize / sizeof(float);

			// If this is true, then the device changed while the command was in-flight
			if (ChannelMap.Num() != ChannelMapSize)
			{
				// todo: investigate turning this into a stack array
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
					const uint32 NumOutputChannels = ChannelMapSize / NumInputChannels;
					FMixerDevice::Get2DChannelMap(SourceInfo.bIsVorbis, NumInputChannels, NumOutputChannels, bInIsCenterChannelOnly, NewChannelMap);
				}

				// Make sure we've been flagged to be using this submix channel type entry
				ChannelTypeInfo.ChannelMap.SetChannelMap(NewChannelMap.GetData());
			}
			else
			{
				GameThreadInfo.bNeedsSpeakerMap[SourceId] = false;
				ChannelTypeInfo.ChannelMap.SetChannelMap(ChannelMap.GetData());
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

			SourceInfos[SourceId].LowPassFilter.StartFrequencyInterpolation(InLPFFrequency, NumOutputFrames);
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
			SourceInfos[SourceId].HighPassFilter.StartFrequencyInterpolation(InHPFFrequency, NumOutputFrames);
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

			FMixerSubmixPtr InSubmixPtr = InSubmixSend.Submix.Pin();
			if (InSubmixPtr.IsValid())
			{
				bool bIsNew = true;
				for (FMixerSourceSubmixSend& SubmixSend : SourceInfo.SubmixSends)
				{
					FMixerSubmixPtr SubmixPtr = SubmixSend.Submix.Pin();
					if (SubmixPtr.IsValid())
					{
						if (SubmixPtr->GetId() == InSubmixPtr->GetId())
						{
							SubmixSend.SendLevel = InSubmixSend.SendLevel;
							bIsNew = false;
							break;
						}
					}
				}

				if (bIsNew)
				{
					SourceInfo.SubmixSends.Add(InSubmixSend);
				}

				InSubmixPtr->AddOrSetSourceVoice(MixerSources[SourceId], InSubmixSend.SendLevel);
			}
		});
	}

	void FMixerSourceManager::SetBusSendInfo(const int32 SourceId, EBusSendType InBusSendType, FMixerBusSend& InBusSend)
	{
		AUDIO_MIXER_CHECK(SourceId < NumTotalSources);
		AUDIO_MIXER_CHECK(GameThreadInfo.bIsBusy[SourceId]);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		AudioMixerThreadCommand([this, SourceId, InBusSendType, InBusSend]()
		{
			// Create mapping of source id to bus send level
			FBusSend BusSend{ SourceId, InBusSend.SendLevel };
			FSourceInfo& SourceInfo = SourceInfos[SourceId];

			// Retrieve the bus we want to send audio to
			FMixerBus* Bus = Buses.Find(InBusSend.BusId);

			// If we already have a bus, we update the amount of audio we want to send to it
			if (Bus)
			{
				Bus->AddBusSend(InBusSendType, BusSend);			
			}
			else
			{
				// If the bus is not registered, make a new entry on the send
				FMixerBus NewBusData(this, SourceInfo.NumInputChannels, NumOutputFrames);

				// Add a send to it. This will not have a bus instance id (i.e. won't output audio), but 
				// we register the send anyway in the event that this bus does play, we'll know to send this
				// source's audio to it.
				NewBusData.AddBusSend(InBusSendType, BusSend);

				Buses.Add(InBusSend.BusId, NewBusData);
			}

			// Check to see if we need to create new bus data. If we are not playing a bus with this id, then we
			// need to create a slot for it such that when a bus does play, it'll start rendering audio from this source
			bool bExisted = false;
			for (uint32 BusId : SourceInfo.BusSends[(int32)InBusSendType])
			{
				if (BusId == InBusSend.BusId)
				{
					bExisted = true;
					break;
				}
			}

			if (!bExisted)
			{
				SourceInfo.BusSends[(int32)InBusSendType].Add(InBusSend.BusId);
			}
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

	bool FMixerSourceManager::IsUsingHRTFSpatializer(const int32 SourceId) const
	{
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);
		return GameThreadInfo.bIsUsingHRTFSpatializer[SourceId];
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
			if (SourceInfo.MixerSourceBuffer->GetNumBuffersQueued() > 0 && NumChannels > 0)
			{
				SourceInfo.CurrentPCMBuffer = SourceInfo.MixerSourceBuffer->GetNextBuffer();
				SourceInfo.CurrentAudioChunkNumFrames = SourceInfo.CurrentPCMBuffer->AudioData.Num() / NumChannels;

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
		CSV_SCOPED_TIMING_STAT(Audio, SourceBuffers);

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

			SourceInfo.SourceEffectScratchBuffer.Reset();
			SourceInfo.SourceEffectScratchBuffer.AddZeroed(NumSamples);

			SourceInfo.SourceBuffer.Reset();
			SourceInfo.SourceBuffer.AddZeroed(NumSamples);

			float* PreDistanceAttenBufferPtr = SourceInfo.PreDistanceAttenuationBuffer.GetData();

			// if this is a bus, we just want to copy the bus audio to this source's output audio
			// Note we need to copy this since bus instances may have different audio via dynamic source effects, etc.
			if (bIsBus)
			{
				// Get the source's rendered bus data
				const FMixerBus* Bus = Buses.Find(SourceInfo.BusId);
				const float* RESTRICT BusBufferPtr = Bus->GetCurrentBusBuffer();

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

	void FMixerSourceManager::ComputePluginAudio(FSourceInfo& SourceInfo, FSourceDownmixData& DownmixData, int32 SourceId, int32 NumSamples)
	{
		if (BypassAudioPluginsCvar)
		{
			// If we're bypassing audio plugins, our pre- and post-effect channels are the same as the input channels
			SourceInfo.NumPostEffectChannels = SourceInfo.NumInputChannels;

			// Set the ptr to use for post-effect buffers:
			DownmixData.PostEffectBuffers = &SourceInfo.SourceBuffer;

			return;
		}

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
				DownmixData.ReverbPluginOutputBuffer.Reset();
				DownmixData.ReverbPluginOutputBuffer.Append(SourceInfo.AudioPluginOutputData.AudioBuffer);
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
				const float* ReverbPluginOutputBufferPtr = DownmixData.ReverbPluginOutputBuffer.GetData();
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
			const float* ReverbPluginOutputBufferPtr = DownmixData.ReverbPluginOutputBuffer.GetData();
			Audio::MixInBufferFast(ReverbPluginOutputBufferPtr, PostDistanceAttenBufferPtr, NumSamples);
		}

		// If the source has HRTF processing enabled, run it through the spatializer
		if (SourceInfo.bUseHRTFSpatializer)
		{
			CSV_SCOPED_TIMING_STAT(Audio, HRTF);

			AUDIO_MIXER_CHECK(SpatializationPlugin.IsValid());
			AUDIO_MIXER_CHECK(SourceInfo.NumInputChannels <= MaxChannelsSupportedBySpatializationPlugin);

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
				DownmixData.PostEffectBuffers = &SourceInfo.SourceBuffer;
			}
			else
			{
				// Otherwise, we are now a 2-channel file and should not be spatialized using normal 3d spatialization
				SourceInfo.NumPostEffectChannels = 2;
				DownmixData.PostEffectBuffers = &SourceInfo.AudioPluginOutputData.AudioBuffer;
			}
		}
		else
		{
			// Otherwise our pre- and post-effect channels are the same as the input channels
			SourceInfo.NumPostEffectChannels = SourceInfo.NumInputChannels;

			// Set the ptr to use for post-effect buffers
			DownmixData.PostEffectBuffers = &SourceInfo.SourceBuffer;
		}
	}

	void FMixerSourceManager::ComputeDownmix3D(FSourceDownmixData& DownmixData, FMixerDevice* MixerDevice)
	{
		// This enormous switch statement handles using the correct function for a given number of input and output channels.
		// For 3D sources, we interpolate from ChannelStartGains to ChannelDestinationGains.
		if (DownmixData.AmbisonicsDecoder.IsValid())
		{
			FAmbisonicsSoundfieldBuffer AmbiBuffer;
			AmbiBuffer.AudioBuffer = MoveTemp(*DownmixData.PostEffectBuffers);
			AmbiBuffer.NumChannels = DownmixData.NumInputChannels;
			AmbiBuffer.PreviousRotation = AmbiBuffer.Rotation;
			AmbiBuffer.Rotation = DownmixData.SourceRotation;

			DownmixData.PositionalData.NumChannels = DownmixData.NumDeviceChannels;
			DownmixData.PositionalData.ChannelPositions = MixerDevice->GetDefaultPositionMap(DownmixData.NumDeviceChannels);

			FSoundfieldDecoderInputData InputData =
			{
				AmbiBuffer,
				DownmixData.PositionalData,
				static_cast<int32>(DownmixData.PostEffectBuffers->Num() / DownmixData.NumInputChannels),
				MixerDevice->GetSampleRate()
			};
			
			FSoundfieldDecoderOutputData OutputData = { DownmixData.DeviceSubmixInfo.OutputBuffer };

			DownmixData.AmbisonicsDecoder->Decode(InputData, OutputData);

			// Move the encoded ambisonics source buffer back to PostEffectBuffers to prevent reallocation
			*DownmixData.PostEffectBuffers = MoveTemp(AmbiBuffer.AudioBuffer);
		}
		else if (DownmixData.NumInputChannels == 1)
		{
			switch (DownmixData.NumDeviceChannels)
			{
			case 8:
				Audio::MixMonoTo8ChannelsFast(*DownmixData.PostEffectBuffers, DownmixData.DeviceSubmixInfo.OutputBuffer, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelStartGains, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelDestinationGains);
				break;
			case 6:
				Audio::MixMonoTo6ChannelsFast(*DownmixData.PostEffectBuffers, DownmixData.DeviceSubmixInfo.OutputBuffer, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelStartGains, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelDestinationGains);
				break;
			case 4:
				Audio::MixMonoTo4ChannelsFast(*DownmixData.PostEffectBuffers, DownmixData.DeviceSubmixInfo.OutputBuffer, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelStartGains, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelDestinationGains);
				break;
			case 2:
				Audio::MixMonoTo2ChannelsFast(*DownmixData.PostEffectBuffers, DownmixData.DeviceSubmixInfo.OutputBuffer, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelStartGains, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelDestinationGains);
				break;
			}
		}
		else if (DownmixData.NumInputChannels == 2)
		{
			switch (DownmixData.NumDeviceChannels)
			{
			case 8:
				Audio::Mix2ChannelsTo8ChannelsFast(*DownmixData.PostEffectBuffers, DownmixData.DeviceSubmixInfo.OutputBuffer, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelStartGains, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelDestinationGains);
				break;
			case 6:
				Audio::Mix2ChannelsTo6ChannelsFast(*DownmixData.PostEffectBuffers, DownmixData.DeviceSubmixInfo.OutputBuffer, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelStartGains, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelDestinationGains);
				break;
			case 4:
				Audio::Mix2ChannelsTo4ChannelsFast(*DownmixData.PostEffectBuffers, DownmixData.DeviceSubmixInfo.OutputBuffer, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelStartGains, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelDestinationGains);
				break;
			case 2:
				Audio::Mix2ChannelsTo2ChannelsFast(*DownmixData.PostEffectBuffers, DownmixData.DeviceSubmixInfo.OutputBuffer, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelStartGains, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelDestinationGains);
				break;
			}
		}
		else
		{
			// Use generic calls:
			Audio::DownmixBuffer(DownmixData.NumInputChannels, DownmixData.NumDeviceChannels, *DownmixData.PostEffectBuffers, DownmixData.DeviceSubmixInfo.OutputBuffer, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelStartGains, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelDestinationGains);
		}

		DownmixData.DeviceSubmixInfo.ChannelMap.CopyDestinationToStart();
	}

	void FMixerSourceManager::ComputeDownmix2D(FSourceDownmixData& DownmixData, FMixerDevice* MixerDevice)
	{
		// This enormous switch statement handles using the correct function for a given number of input and output channels.
		// For 2D sources, we just apply the gain matrix in ChannelDestionationGains with no interpolation.
		if (DownmixData.AmbisonicsDecoder.IsValid())
		{
			FAmbisonicsSoundfieldBuffer AmbiBuffer;
			AmbiBuffer.AudioBuffer = MoveTemp(*DownmixData.PostEffectBuffers);
			AmbiBuffer.NumChannels = DownmixData.NumInputChannels;
			AmbiBuffer.PreviousRotation = AmbiBuffer.Rotation;
			AmbiBuffer.Rotation = DownmixData.SourceRotation;

			DownmixData.PositionalData.NumChannels = DownmixData.NumDeviceChannels;
			DownmixData.PositionalData.ChannelPositions = MixerDevice->GetDefaultPositionMap(DownmixData.NumDeviceChannels);

			FSoundfieldDecoderInputData InputData =
			{
				AmbiBuffer,
				DownmixData.PositionalData,
				static_cast<int32>(DownmixData.PostEffectBuffers->Num() / DownmixData.NumInputChannels),
				MixerDevice->GetSampleRate()
			};

			FSoundfieldDecoderOutputData OutputData = { DownmixData.DeviceSubmixInfo.OutputBuffer };

			DownmixData.AmbisonicsDecoder->Decode(InputData, OutputData);

			// Move the encoded ambisonics source buffer back to PostEffectBuffers to prevent reallocation
			*DownmixData.PostEffectBuffers = MoveTemp(AmbiBuffer.AudioBuffer);
		}
		else if (DownmixData.NumInputChannels == 1)
		{
			switch (DownmixData.NumDeviceChannels)
			{
			case 8:
				Audio::MixMonoTo8ChannelsFast(*DownmixData.PostEffectBuffers, DownmixData.DeviceSubmixInfo.OutputBuffer, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelDestinationGains);
				break;
			case 6:
				Audio::MixMonoTo6ChannelsFast(*DownmixData.PostEffectBuffers, DownmixData.DeviceSubmixInfo.OutputBuffer, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelDestinationGains);
				break;
			case 4:
				Audio::MixMonoTo4ChannelsFast(*DownmixData.PostEffectBuffers, DownmixData.DeviceSubmixInfo.OutputBuffer, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelDestinationGains);
				break;
			case 2:
				Audio::MixMonoTo2ChannelsFast(*DownmixData.PostEffectBuffers, DownmixData.DeviceSubmixInfo.OutputBuffer, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelDestinationGains);
				break;
			}
		}
		else if (DownmixData.NumInputChannels == 2)
		{
			switch (DownmixData.NumDeviceChannels)
			{
			case 8:
				Audio::Mix2ChannelsTo8ChannelsFast(*DownmixData.PostEffectBuffers, DownmixData.DeviceSubmixInfo.OutputBuffer, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelDestinationGains);
				break;
			case 6:
				Audio::Mix2ChannelsTo6ChannelsFast(*DownmixData.PostEffectBuffers, DownmixData.DeviceSubmixInfo.OutputBuffer, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelDestinationGains);
				break;
			case 4:
				Audio::Mix2ChannelsTo4ChannelsFast(*DownmixData.PostEffectBuffers, DownmixData.DeviceSubmixInfo.OutputBuffer, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelDestinationGains);
				break;
			case 2:
				Audio::Mix2ChannelsTo2ChannelsFast(*DownmixData.PostEffectBuffers, DownmixData.DeviceSubmixInfo.OutputBuffer, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelDestinationGains);
				break;
			}

		}
		else
		{
			// Use generic calls:
			Audio::DownmixBuffer(DownmixData.NumInputChannels, DownmixData.NumDeviceChannels, *DownmixData.PostEffectBuffers, DownmixData.DeviceSubmixInfo.OutputBuffer, DownmixData.DeviceSubmixInfo.ChannelMap.ChannelDestinationGains);
		}
	}

	void FMixerSourceManager::EncodeToSoundfieldFormats(FSourceDownmixData& DownmixData, const FSourceInfo& InSource, FMixerDevice* InDevice)
	{
		const FSpatializationParams& SpatializationParams = InSource.SpatParams;

		// First, build our SoundfieldSpeakerPositionalData out of our SpatializationParams.
		DownmixData.PositionalData.NumChannels = DownmixData.NumInputChannels;
		
		// Build our input channel positions from the Spatialization params if this is a 3D source, otherwise use the default channel positions.
		DownmixData.InputChannelPositions.Reset();
		if (InSource.bIs3D)
		{
			if (DownmixData.NumInputChannels == 1)
			{
				Audio::FChannelPositionInfo ChannelPosition;
				ChannelPosition.Channel = EAudioMixerChannel::FrontCenter;
				ConvertCartesianToSpherical(SpatializationParams.EmitterPosition, ChannelPosition.Azimuth, ChannelPosition.Elevation, ChannelPosition.Radius);

				DownmixData.InputChannelPositions.Add(ChannelPosition);

				DownmixData.PositionalData.ChannelPositions = &DownmixData.InputChannelPositions;
			}
			else if (DownmixData.NumInputChannels == 2)
			{
				Audio::FChannelPositionInfo LeftChannelPosition;
				LeftChannelPosition.Channel = EAudioMixerChannel::FrontLeft;
				ConvertCartesianToSpherical(SpatializationParams.LeftChannelPosition, LeftChannelPosition.Azimuth, LeftChannelPosition.Elevation, LeftChannelPosition.Radius);

				DownmixData.InputChannelPositions.Add(LeftChannelPosition);

				Audio::FChannelPositionInfo RightChannelPosition;
				LeftChannelPosition.Channel = EAudioMixerChannel::FrontRight;
				ConvertCartesianToSpherical(SpatializationParams.RightChannelPosition, RightChannelPosition.Azimuth, RightChannelPosition.Elevation, RightChannelPosition.Radius);

				DownmixData.InputChannelPositions.Add(RightChannelPosition);

				DownmixData.PositionalData.ChannelPositions = &DownmixData.InputChannelPositions;
			}
			else
			{
				// Spatialization of multichannel audio beyond stereo is not currently supported in the engine.
				DownmixData.PositionalData.ChannelPositions = InDevice->GetDefaultPositionMap(DownmixData.NumInputChannels);
			}
		}
		else
		{
			DownmixData.PositionalData.ChannelPositions = InDevice->GetDefaultPositionMap(DownmixData.NumInputChannels);
		}

		// Finally, run our encoders.
		for (auto& Soundfield : DownmixData.EncodedSoundfieldDownmixes)
		{
			FSubmixSoundfieldData& SoundfieldData = Soundfield.Value;

			check(SoundfieldData.EncoderSettings.IsValid());
			check(SoundfieldData.EncodedPacket.IsValid());

			SoundfieldData.EncodedPacket->Reset();

			// If this is an ambisonics source, transcode it, or if this is going to an ambisonics submix, simply forward the buffer. Otherwise, encode the source 
			// to whatever soundfield format the destination submix is.
			if (SoundfieldData.AmbiTranscoder)
			{
				FAmbisonicsSoundfieldBuffer AmbiBuffer;
				AmbiBuffer.AudioBuffer = MoveTemp(*DownmixData.PostEffectBuffers);
				AmbiBuffer.NumChannels = DownmixData.NumInputChannels;
				AmbiBuffer.PreviousRotation = AmbiBuffer.Rotation;
				AmbiBuffer.Rotation = DownmixData.SourceRotation;

				SoundfieldData.AmbiTranscoder->Transcode(AmbiBuffer, GetAmbisonicsSourceDefaultSettings(), *SoundfieldData.EncodedPacket, *SoundfieldData.EncoderSettings);
				*DownmixData.PostEffectBuffers = MoveTemp(AmbiBuffer.AudioBuffer);
			}
			else if (SoundfieldData.Encoder)
			{
				FSoundfieldEncoderInputData InputData =
				{
					*DownmixData.PostEffectBuffers,
					static_cast<int32>(DownmixData.NumInputChannels),
					*SoundfieldData.EncoderSettings,
					DownmixData.PositionalData
				};

				SoundfieldData.Encoder->Encode(InputData, *SoundfieldData.EncodedPacket);
			}
			else if (SoundfieldData.bIsUnrealAmbisonicsSubmix)
			{
				ensure(InSource.bIsAmbisonics);

				FAmbisonicsSoundfieldBuffer& OutputPacket = DowncastSoundfieldRef<FAmbisonicsSoundfieldBuffer>(*SoundfieldData.EncodedPacket);
				// Fixme: This is an array copy. Can we serve DownmixData.PostEffectBuffers directly to this soundfield, then return it back at the end of the render loop?
				OutputPacket.AudioBuffer = *DownmixData.PostEffectBuffers;
				OutputPacket.NumChannels = DownmixData.NumInputChannels;
				OutputPacket.PreviousRotation = OutputPacket.Rotation;
				OutputPacket.Rotation = DownmixData.SourceRotation;
			}
		}
	}

	void FMixerSourceManager::ConvertCartesianToSpherical(const FVector& InVector, float& OutAzimuth, float& OutElevation, float& OutRadius)
	{
		// Convert coordinates from unreal cartesian system to left handed spherical coordinates (zenith is positive elevation, right is positive azimuth)
		const float InX = -InVector.Z; //InVector.Y;
		const float InY = InVector.X;// -InVector.Z;
		const float InZ = -InVector.Y;


		OutElevation = FMath::Atan2(InY, InX);

		// Note, rather than using arccos(z / radius) here, we use Atan2 to avoid wrapping issues with negative elevation values.
		OutAzimuth = FMath::Atan2(FMath::Sqrt(InX * InX + InY * InY), InZ);
		OutRadius = InVector.Size();
	}

	FMixerSourceManager::FSourceDownmixData& FMixerSourceManager::InitializeDownmixForSource(const int32 SourceId, const int32 NumInputChannels, const int32 NumOutputChannels, const int32 InNumOutputFrames)
	{
		DownmixDataArray[SourceId].ResetData(NumInputChannels, NumOutputChannels);
		return DownmixDataArray[SourceId];
	}

	const Audio::FMixerSourceManager::FSubmixChannelData& FMixerSourceManager::GetChannelInfoForDevice(const FSourceDownmixData& InDownmixData) const
	{
		return InDownmixData.DeviceSubmixInfo;
	}

	Audio::FMixerSourceManager::FSubmixChannelData& FMixerSourceManager::GetChannelInfoForDevice(FSourceDownmixData& InDownmixData)
	{
		return InDownmixData.DeviceSubmixInfo;
	}

	const FMixerSourceManager::FSubmixSoundfieldData& FMixerSourceManager::GetChannelInfoForFormat(const FSoundfieldEncodingKey& InFormat, const FSourceDownmixData& InDownmixData) const
	{
		check(InDownmixData.EncodedSoundfieldDownmixes.Contains(InFormat));
		return InDownmixData.EncodedSoundfieldDownmixes[InFormat];
	}

	FMixerSourceManager::FSubmixSoundfieldData& FMixerSourceManager::GetChannelInfoForFormat(const FSoundfieldEncodingKey& InFormat, FSourceDownmixData& InDownmixData)
	{
		return InDownmixData.EncodedSoundfieldDownmixes.FindOrAdd(InFormat);
	}

	void FMixerSourceManager::ComputePostSourceEffectBufferForIdRange(bool bGenerateBuses, const int32 SourceIdStart, const int32 SourceIdEnd)
	{
		CSV_SCOPED_TIMING_STAT(Audio, SourceEffectsBuffers);

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

			FSourceDownmixData& DownmixData = DownmixDataArray[SourceId];

			// Copy and store the current state of the pre-distance attenuation buffer before we feed it through our source effects
			// This is used by pre-effect sends
			if (SourceInfo.BusSends[(int32)EBusSendType::PreEffect].Num() > 0)
			{
				SourceInfo.PreEffectBuffer.Reset();
				SourceInfo.PreEffectBuffer.Reserve(SourceInfo.PreDistanceAttenuationBuffer.Num());

				FMemory::Memcpy(SourceInfo.PreEffectBuffer.GetData(), SourceInfo.PreDistanceAttenuationBuffer.GetData(), sizeof(float)*SourceInfo.PreDistanceAttenuationBuffer.Num());
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
				// Prepare this source's effect chain input data
				SourceInfo.SourceEffectInputData.CurrentVolume = SourceInfo.VolumeSourceDestination;
				SourceInfo.SourceEffectInputData.CurrentPitch = SourceInfo.PitchSourceParam.GetValue();
				SourceInfo.SourceEffectInputData.AudioClock = MixerDevice->GetAudioClock();
				if (SourceInfo.NumInputFrames > 0)
				{
					SourceInfo.SourceEffectInputData.CurrentPlayFraction = (float)SourceInfo.NumFramesPlayed / SourceInfo.NumInputFrames;
				}
				SourceInfo.SourceEffectInputData.SpatParams = SourceInfo.SpatParams;

				// Get a ptr to pre-distance attenuation buffer ptr
				float* OutputSourceEffectBufferPtr = SourceInfo.SourceEffectScratchBuffer.GetData();

				SourceInfo.SourceEffectInputData.InputSourceEffectBufferPtr = SourceInfo.PreDistanceAttenuationBuffer.GetData();
				SourceInfo.SourceEffectInputData.NumSamples = NumSamples;

				// Loop through the effect chain passing in buffers
				FScopeLock ScopeLock(&EffectChainMutationCriticalSection);
				{
					for (TSoundEffectSourcePtr& SoundEffectSource : SourceInfo.SourceEffects)
					{
						bool bPresetUpdated = false;
						if (SoundEffectSource->IsActive())
						{
							bPresetUpdated = SoundEffectSource->Update();
						}

						// Modulation must be updated regardless of whether or not the source is
						// active to allow for initial conditions to be set if source is reactivated.
						if (bPresetUpdated || SourceInfo.bIsModulationUpdated)
						{
							SoundEffectSource->ProcessControls(SourceInfo.ModulationControls);
						}

						if (SoundEffectSource->IsActive())
						{
							SoundEffectSource->ProcessAudio(SourceInfo.SourceEffectInputData, OutputSourceEffectBufferPtr);

							// Copy output to input
							FMemory::Memcpy(SourceInfo.SourceEffectInputData.InputSourceEffectBufferPtr, OutputSourceEffectBufferPtr, sizeof(float) * NumSamples);
						}
					}
				}

				SourceInfo.bIsModulationUpdated = false;
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

			if (!SourceInfo.bOutputToBusOnly)
			{
				// Only scale with distance attenuation and send to source audio to plugins if we're not in output-to-bus only mode
				const int32 NumOutputSamplesThisSource = NumOutputFrames * SourceInfo.NumInputChannels;

				const bool BypassLPF = DisableFilteringCvar || (SourceInfo.LowPassFilter.GetCutoffFrequency() >= (MAX_FILTER_FREQUENCY - KINDA_SMALL_NUMBER));
				const bool BypassHPF = DisableFilteringCvar || DisableHPFilteringCvar || (SourceInfo.HighPassFilter.GetCutoffFrequency() <= (MIN_FILTER_FREQUENCY + KINDA_SMALL_NUMBER));

				float* PostDistanceAttenBufferPtr = SourceInfo.SourceBuffer.GetData();
				float* HpfInputBuffer = PreDistanceAttenBufferPtr; // assume bypassing LPF (HPF uses input buffer as input)

				if (!BypassLPF)
				{
					// Not bypassing LPF, so tell HPF to use LPF output buffer as input
					HpfInputBuffer = PostDistanceAttenBufferPtr;

					// process LPF audio block
					SourceInfo.LowPassFilter.ProcessAudioBuffer(PreDistanceAttenBufferPtr, PostDistanceAttenBufferPtr, NumOutputSamplesThisSource);
				}

				if(!BypassHPF)
				{
					// process HPF audio block
					SourceInfo.HighPassFilter.ProcessAudioBuffer(HpfInputBuffer, PostDistanceAttenBufferPtr, NumOutputSamplesThisSource);
				}

				// We manually reset interpolation to avoid branches in filter code
				SourceInfo.LowPassFilter.StopFrequencyInterpolation();
				SourceInfo.HighPassFilter.StopFrequencyInterpolation();

				if (BypassLPF && BypassHPF)
				{
					FMemory::Memcpy(PostDistanceAttenBufferPtr, PreDistanceAttenBufferPtr, NumSamples * sizeof(float));
				}

				// Apply distance attenuation
				ApplyDistanceAttenuation(SourceInfo, NumSamples);

				// Send source audio to plugins
				ComputePluginAudio(SourceInfo, DownmixData, SourceId, NumSamples);
			}

			// Check the source effect tails condition
			if (SourceInfo.bIsLastBuffer && SourceInfo.bEffectTailsDone)
			{
				// If we're done and our tails our done, clear everything out
				SourceInfo.CurrentFrameValues.Reset();
				SourceInfo.NextFrameValues.Reset();
				SourceInfo.CurrentPCMBuffer = nullptr;
			}

			SourceInfo.bIsModulationUpdated = false;
		}
	}

	void FMixerSourceManager::ComputeOutputBuffersForIdRange(const bool bGenerateBuses, const int32 SourceIdStart, const int32 SourceIdEnd)
	{
		CSV_SCOPED_TIMING_STAT(Audio, SourceOutputBuffers);

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

			FSourceDownmixData& DownmixData = DownmixDataArray[SourceId];

			if (DownmixData.PostEffectBuffers == nullptr)
			{
				continue;
			}

			DownmixData.PositionalData.Rotation = SourceInfo.SpatParams.ListenerOrientation;
			DownmixData.SourceRotation = SourceInfo.SpatParams.EmitterWorldRotation;

			// If we are sending audio to a non-soundfield submix, we downmix audio to the device configuration here.
			if (DownmixData.bIsSourceBeingSentToDeviceSubmix)
			{
				if (SourceInfo.bIs3D && !DownmixData.bIsInitialDownmix)
				{
					ComputeDownmix3D(DownmixData, MixerDevice);
				}
				else
				{
					ComputeDownmix2D(DownmixData, MixerDevice);
					DownmixData.bIsInitialDownmix = false;
				}
			}

			if (DownmixData.EncodedSoundfieldDownmixes.Num())
			{
				// Perform All Encoding for all sends.
				EncodeToSoundfieldFormats(DownmixData, SourceInfo, MixerDevice);
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

	void FMixerSourceManager::MixOutputBuffers(const int32 SourceId, int32 InNumOutputChannels, const float SendLevel, AlignedFloatBuffer& OutWetBuffer) const
	{
		if (SendLevel > 0.0f)
		{
			const FSourceInfo& SourceInfo = SourceInfos[SourceId];
			const FSourceDownmixData& DownmixData = DownmixDataArray[SourceId];

			// Don't need to mix into submixes if the source is paused
			if (!SourceInfo.bIsPaused && !SourceInfo.bIsDone && SourceInfo.bIsPlaying && DownmixData.bIsSourceBeingSentToDeviceSubmix)
			{
				const FSubmixChannelData& ChannelInfo = GetChannelInfoForDevice(DownmixData);

				const float* RESTRICT SourceOutputBufferPtr = ChannelInfo.OutputBuffer.GetData();

				// TODO: Figure out a fix for this race condition on device swap:
				// const int32 OutWetBufferSize = OutWetBuffer.Num();
				const int32 OutWetBufferSize = FMath::Min<float>(OutWetBuffer.Num(), ChannelInfo.OutputBuffer.Num());
				float* RESTRICT OutWetBufferPtr = OutWetBuffer.GetData();

				Audio::MixInBufferFast(SourceOutputBufferPtr, OutWetBufferPtr, OutWetBufferSize, SendLevel);
			}
		}
	}

	const ISoundfieldAudioPacket* FMixerSourceManager::GetEncodedOutput(const int32 SourceId, const FSoundfieldEncodingKey& InKey) const
	{
		AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

		const FSourceInfo& SourceInfo = SourceInfos[SourceId];

		// Don't need to mix into submixes if the source is paused
		if (!SourceInfo.bIsPaused && !SourceInfo.bIsDone && SourceInfo.bIsPlaying)
		{
			const FSourceDownmixData& DownmixData = DownmixDataArray[SourceId];
			const FSubmixSoundfieldData& ChannelInfo = GetChannelInfoForFormat(InKey, DownmixData);

			return ChannelInfo.EncodedPacket.Get();
		}

		return nullptr;
	}

	const FQuat FMixerSourceManager::GetListenerRotation(const int32 SourceId) const
	{
		const FSourceDownmixData& DownmixData = DownmixDataArray[SourceId];
		return DownmixData.PositionalData.Rotation;
	}

	void FMixerSourceManager::UpdateDeviceChannelCount(const int32 InNumOutputChannels)
	{
		AudioMixerThreadCommand([this, InNumOutputChannels]()
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

				FSourceDownmixData& DownmixData = DownmixDataArray[SourceId];
				DownmixData.ResetNumberOfDeviceChannels(InNumOutputChannels);

				FSubmixChannelData& ChannelTypeInfo = GetChannelInfoForDevice(DownmixData);

				SourceInfo.ScratchChannelMap.Reset();
				const int32 NumSourceChannels = SourceInfo.bUseHRTFSpatializer ? 2 : SourceInfo.NumInputChannels;

				// If this is a 3d source, then just zero out the channel map, it'll cause a temporary blip
				// but it should reset in the next tick
				if (SourceInfo.bIs3D)
				{
					GameThreadInfo.bNeedsSpeakerMap[SourceId] = true;
					SourceInfo.ScratchChannelMap.AddZeroed(NumSourceChannels * InNumOutputChannels);
				}
				// If it's a 2D sound, then just get a new channel map appropriate for the new device channel count
				else
				{
					SourceInfo.ScratchChannelMap.Reset();
					MixerDevice->Get2DChannelMap(SourceInfo.bIsVorbis, NumSourceChannels, InNumOutputChannels, SourceInfo.bIsCenterChannelOnly, SourceInfo.ScratchChannelMap);
				}

				ChannelTypeInfo.ChannelMap.SetChannelMap(SourceInfo.ScratchChannelMap.GetData());
			}
		});
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
					FScopeLock ScopeLock(&EffectChainMutationCriticalSection);
					{
						TArray<TSoundEffectSourcePtr>& ThisSourceEffectChain = SourceInfo.SourceEffects;
						bool bReset = false;
						if (InSourceEffectChain.Num() == ThisSourceEffectChain.Num())
						{
							for (int32 SourceEffectId = 0; SourceEffectId < ThisSourceEffectChain.Num(); ++SourceEffectId)
							{
								const FSourceEffectChainEntry& ChainEntry = InSourceEffectChain[SourceEffectId];

								TSoundEffectSourcePtr SourceEffectInstance = ThisSourceEffectChain[SourceEffectId];
								if (!SourceEffectInstance->IsPreset(ChainEntry.Preset))
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

							// First reset the source effect chain
							ResetSourceEffectChain(SourceId);

							// Rebuild it
							BuildSourceEffectChain(SourceId, InitData, InSourceEffectChain);
						}
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

		CSV_SCOPED_TIMING_STAT(Audio, SourceManagerUpdate);

		if (FPlatformProcess::SupportsMultithreading())
		{
			// Get the this blocks commands before rendering audio
			PumpCommandQueue();
		}
		else if (bPumpQueue)
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
		// Here, we make sure that we don't flip our command double buffer while we are executing this function.
		FScopeLock ScopeLock(&CommandBufferIndexCriticalSection);
		AUDIO_MIXER_CHECK_GAME_THREAD(MixerDevice);

		// Add the function to the command queue:
		int32 AudioThreadCommandIndex = !RenderThreadCommandBufferIndex.GetValue();
		CommandBuffers[AudioThreadCommandIndex].SourceCommandQueue.Add(MoveTemp(InFunction));
		NumCommands.Increment();
	}

	void FMixerSourceManager::PumpCommandQueue()
	{
		// If we're already triggered, we need to wait for the audio thread to reset it before pumping
		if (FPlatformProcess::SupportsMultithreading())
		{
			if (CommandsProcessedEvent->Wait(0))
			{
				return;
			}
		}

		int32 CurrentRenderThreadIndex = RenderThreadCommandBufferIndex.GetValue();

		FCommands& Commands = CommandBuffers[CurrentRenderThreadIndex];

		// Pop and execute all the commands that came since last update tick
		for (int32 Id = 0; Id < Commands.SourceCommandQueue.Num(); ++Id)
		{
			TFunction<void()>& CommandFunction = Commands.SourceCommandQueue[Id];
			CommandFunction();
			NumCommands.Decrement();
		}

		Commands.SourceCommandQueue.Reset();

		if (FPlatformProcess::SupportsMultithreading())
		{
			check(CommandsProcessedEvent != nullptr);
			CommandsProcessedEvent->Trigger();
		}
		else
		{
			RenderThreadCommandBufferIndex.Set(!CurrentRenderThreadIndex);
		}

	}

	void FMixerSourceManager::FlushCommandQueue(bool bPumpInCommand)
	{
		check(CommandsProcessedEvent != nullptr);

		// If we have no commands enqueued, exit
		if (NumCommands.GetValue() == 0)
		{
			UE_LOG(LogAudioMixer, Verbose, TEXT("No commands were queued while flushing the source manager."));
			return;
		}

		// Make sure current current executing 
		bool bTimedOut = false;
		if (!CommandsProcessedEvent->Wait(CommandBufferFlushWaitTimeMsCvar))
		{
			CommandsProcessedEvent->Trigger();
			bTimedOut = true;
			UE_LOG(LogAudioMixer, Warning, TEXT("Timed out waiting to flush the source manager command queue (1)."));
		}
		else
		{
			UE_LOG(LogAudioMixer, Verbose, TEXT("Flush succeeded in the source manager command queue (1)."));
		}

		// Call update to trigger a final pump of commands
		Update(bTimedOut);

		if (bPumpInCommand)
		{
			PumpCommandQueue();
		}

		// Wait one more time for the double pump
		if (!CommandsProcessedEvent->Wait(1000))
		{
			CommandsProcessedEvent->Trigger();
			UE_LOG(LogAudioMixer, Warning, TEXT("Timed out waiting to flush the source manager command queue (2)."));
		}
		else
		{
			UE_LOG(LogAudioMixer, Verbose, TEXT("Flush succeeded the source manager command queue (2)."));
		}
	}

	void FMixerSourceManager::UpdatePendingReleaseData(bool bForceWait)
	{
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
