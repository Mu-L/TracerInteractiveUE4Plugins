// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "AudioMixerBuffer.h"
#include "AudioMixerBus.h"
#include "AudioMixerDevice.h"
#include "AudioMixerSourceOutputBuffer.h"
#include "AudioMixerSubmix.h"
#include "Containers/Queue.h"
#include "DSP/BufferVectorOperations.h"
#include "DSP/EnvelopeFollower.h"
#include "DSP/Filter.h"
#include "DSP/InterpolatedOnePole.h"
#include "DSP/ParamInterpolator.h"
#include "IAudioExtensionPlugin.h"
#include "ISoundfieldFormat.h"
#include "Sound/SoundModulationDestination.h"
#include "Sound/QuartzQuantizationUtilities.h"

namespace Audio
{
	class FMixerSubmix;
	class FMixerDevice;
	class FMixerSourceVoice;
	class FMixerSourceBuffer;
	class ISourceListener;
	class FMixerSourceSubmixOutputBuffer;

	/** Struct defining a source voice buffer. */
	struct FMixerSourceVoiceBuffer
	{
		/** PCM float data. */
		AlignedFloatBuffer AudioData;

		/** How many times this buffer will loop. */
		int32 LoopCount = 0;

		/** If this buffer is from real-time decoding and needs to make callbacks for more data. */
		bool bRealTimeBuffer = false;
	};


	class ISourceListener
	{
	public:
		// Called before a source begins to generate audio. 
		virtual void OnBeginGenerate() = 0;

		// Called when a loop point is hit
		virtual void OnLoopEnd() = 0;

		// Called when the source finishes on the audio render thread
		virtual void OnDone() = 0;

		// Called when the source's effect tails finish on the audio render thread.
		virtual void OnEffectTailsDone() = 0;

	};

	struct FMixerSourceSubmixSend
	{
		// The submix ptr
		FMixerSubmixWeakPtr Submix;

		// The amount of audio that is to be mixed into this submix
		float SendLevel = 0.0f;

		// Whather or not this is the primary send (i.e. first in the send chain)
		bool bIsMainSend = false;

		// Whether or not this is a pre-distance attenuation send
		EMixerSourceSubmixSendStage SubmixSendStage = EMixerSourceSubmixSendStage::PostDistanceAttenuation;

		// If this is a soundfield submix, this is a pointer to the submix's Soundfield Factory.
		// If this is nullptr, the submix is not a soundfield submix.
		ISoundfieldFactory* SoundfieldFactory = nullptr;
	};

	// Struct holding mappings of bus ids (unique ids) to send level
	struct FInitAudioBusSend
	{
		uint32 AudioBusId = INDEX_NONE;
		float SendLevel = 0.0f;
	};

	struct FMixerSourceVoiceInitParams
	{
		TSharedPtr<FMixerSourceBuffer, ESPMode::ThreadSafe> MixerSourceBuffer = nullptr;
		ISourceListener* SourceListener = nullptr;
		TArray<FMixerSourceSubmixSend> SubmixSends;
		TArray<FInitAudioBusSend> AudioBusSends[(int32)EBusSendType::Count];
		uint32 AudioBusId = INDEX_NONE;
		float SourceBusDuration = 0.0f;
		uint32 SourceEffectChainId = INDEX_NONE;
		TArray<FSourceEffectChainEntry> SourceEffectChain;
		FMixerSourceVoice* SourceVoice = nullptr;
		int32 NumInputChannels = 0;
		int32 NumInputFrames = 0;
		float EnvelopeFollowerAttackTime = 10.0f;
		float EnvelopeFollowerReleaseTime = 100.0f;
		FString DebugName;
		USpatializationPluginSourceSettingsBase* SpatializationPluginSettings = nullptr;
		UOcclusionPluginSourceSettingsBase* OcclusionPluginSettings = nullptr;
		UReverbPluginSourceSettingsBase* ReverbPluginSettings = nullptr;

		FSoundModulationDefaultSettings ModulationSettings;

		FQuartzQuantizedRequestData QuantizedRequestData;

		FName AudioComponentUserID;
		uint64 AudioComponentID = 0;
		bool bIs3D = false;
		bool bPlayEffectChainTails = false;
		bool bUseHRTFSpatialization = false;
		bool bIsExternalSend = false;
		bool bIsDebugMode  = false;
		bool bOutputToBusOnly = false;
		bool bIsVorbis = false;
		bool bIsSoundfield = false;
		bool bIsSeeking = false;
	};

	struct FSourceManagerInitParams
	{
		// Total number of sources to use in the source manager
		int32 NumSources = 0;

		// Number of worker threads to use for the source manager.
		int32 NumSourceWorkers = 0;
	};

	class FMixerSourceManager
	{
	public:
		FMixerSourceManager(FMixerDevice* InMixerDevice);
		~FMixerSourceManager();

		void Init(const FSourceManagerInitParams& InitParams);
		void Update(bool bTimedOut = false);

		bool GetFreeSourceId(int32& OutSourceId);
		int32 GetNumActiveSources() const;
		int32 GetNumActiveAudioBuses() const;

		void ReleaseSourceId(const int32 SourceId);
		void InitSource(const int32 SourceId, const FMixerSourceVoiceInitParams& InitParams);

		// Creates an audio bus manually. Returns an audio bus Id.
		void StartAudioBus(uint32 InAudioBusId, int32 InNumChannels, bool bInIsAutomatic);
		void StopAudioBus(uint32 InAudioBusId);
		bool IsAudioBusActive(uint32 InAudioBusId);
		FPatchOutputStrongPtr AddPatchForAudioBus(uint32 InAudioBusId, float PatchGain);

		void Play(const int32 SourceId);
		void Stop(const int32 SourceId);
		void StopInternal(const int32 SourceId);
		void StopFade(const int32 SourceId, const int32 NumFrames);
		void Pause(const int32 SourceId);
		void SetPitch(const int32 SourceId, const float Pitch);
		void SetVolume(const int32 SourceId, const float Volume);
		void SetDistanceAttenuation(const int32 SourceId, const float DistanceAttenuation);
		void SetSpatializationParams(const int32 SourceId, const FSpatializationParams& InParams);
		void SetChannelMap(const int32 SourceId, const uint32 NumInputChannels, const Audio::AlignedFloatBuffer& InChannelMap, const bool bInIs3D, const bool bInIsCenterChannelOnly);
		void SetLPFFrequency(const int32 SourceId, const float Frequency);
		void SetHPFFrequency(const int32 SourceId, const float Frequency);

		// Sets base (i.e. carrier) frequency of modulateable parameters
		void SetModPitch(const int32 SourceId, const float InModPitch);
		void SetModVolume(const int32 SourceId, const float InModVolume);
		void SetModLPFFrequency(const int32 SourceId, const float InModFrequency);
		void SetModHPFFrequency(const int32 SourceId, const float InModFrequency);


		void SetListenerTransforms(const TArray<FTransform>& ListenerTransforms);
		const TArray<FTransform>* GetListenerTransforms() const;

		int64 GetNumFramesPlayed(const int32 SourceId) const;
		float GetEnvelopeValue(const int32 SourceId) const;
		bool IsUsingHRTFSpatializer(const int32 SourceId) const;
		bool NeedsSpeakerMap(const int32 SourceId) const;
		void ComputeNextBlockOfSamples();
		void ClearStoppingSounds();
		void MixOutputBuffers(const int32 SourceId, int32 InNumOutputChannels, const float InSendLevel, EMixerSourceSubmixSendStage InSubmixSendStage, AlignedFloatBuffer& OutWetBuffer) const;

		// Retrieves a channel map for the given source ID for the given output channels
		// can be used even when a source is 3D if the source is doing any kind of bus sending or otherwise needs a channel map
		void Get2DChannelMap(const int32 SourceId, int32 InNumOutputChannels, Audio::AlignedFloatBuffer& OutChannelMap);

		// Called by a soundfield submix to get encoded audio.
		// If this source wasn't encoded (possibly because it is paused or finished playing),
		// this returns nullptr.
		// Returned nonnull pointers are only guaranteed to be valid on the audio mixer render thread.
		const ISoundfieldAudioPacket* GetEncodedOutput(const int32 SourceId, const FSoundfieldEncodingKey& InKey) const;

		const FQuat GetListenerRotation(const int32 SourceId) const;

		void SetSubmixSendInfo(const int32 SourceId, const FMixerSourceSubmixSend& SubmixSend);
		void ClearSubmixSendInfo(const int32 SourceId, const FMixerSourceSubmixSend& SubmixSend);

		void SetBusSendInfo(const int32 SourceId, EBusSendType InAudioBusSendType, uint32 AudiobusId, float BusSendLevel);

		void UpdateDeviceChannelCount(const int32 InNumOutputChannels);

		void UpdateSourceEffectChain(const uint32 SourceEffectChainId, const TArray<FSourceEffectChainEntry>& SourceEffectChain, const bool bPlayEffectChainTails);


		// Quantized event methods
		void PauseSoundForQuantizationCommand(const int32 SourceId);
		void SetSubBufferDelayForSound(const int32 SourceId, const int32 FramesToDelay);
		void UnPauseSoundForQuantizationCommand(const int32 SourceId);

		// Buffer getters
		const float* GetPreDistanceAttenuationBuffer(const int32 SourceId) const;
		const float* GetPreEffectBuffer(const int32 SourceId) const;
		const float* GetPreviousSourceBusBuffer(const int32 SourceId) const;
		const float* GetPreviousAudioBusBuffer(const int32 AudioBusId) const;
		int32 GetNumChannels(const int32 SourceId) const;
		int32 GetNumOutputFrames() const { return NumOutputFrames; }
		bool IsSourceBus(const int32 SourceId) const;
		void PumpCommandQueue();
		void UpdatePendingReleaseData(bool bForceWait = false);
		void FlushCommandQueue(bool bPumpCommandQueue = false);
	private:
		void ReleaseSource(const int32 SourceId);
		void BuildSourceEffectChain(const int32 SourceId, FSoundEffectSourceInitData& InitData, const TArray<FSourceEffectChainEntry>& SourceEffectChain, TArray<TSoundEffectSourcePtr>& OutSourceEffects);
		void ResetSourceEffectChain(const int32 SourceId);
		void ReadSourceFrame(const int32 SourceId);

		void GenerateSourceAudio(const bool bGenerateBuses);
		void GenerateSourceAudio(const bool bGenerateBuses, const int32 SourceIdStart, const int32 SourceIdEnd);

		void ComputeSourceBuffersForIdRange(const bool bGenerateBuses, const int32 SourceIdStart, const int32 SourceIdEnd);
		void ComputePostSourceEffectBufferForIdRange(const bool bGenerateBuses, const int32 SourceIdStart, const int32 SourceIdEnd);
		void ComputeOutputBuffersForIdRange(const bool bGenerateBuses, const int32 SourceIdStart, const int32 SourceIdEnd);

		void ComputeBuses();
		void UpdateBuses();

		void AudioMixerThreadCommand(TFunction<void()> InFunction);

		static const int32 NUM_BYTES_PER_SAMPLE = 2;

		// Private class which perform source buffer processing in a worker task
		class FAudioMixerSourceWorker : public FNonAbandonableTask
		{
			FMixerSourceManager* SourceManager;
			int32 StartSourceId;
			int32 EndSourceId;
			bool bGenerateBuses;

		public:
			FAudioMixerSourceWorker(FMixerSourceManager* InSourceManager, const int32 InStartSourceId, const int32 InEndSourceId)
				: SourceManager(InSourceManager)
				, StartSourceId(InStartSourceId)
				, EndSourceId(InEndSourceId)
				, bGenerateBuses(false)
			{
			}

			void SetGenerateBuses(bool bInGenerateBuses)
			{
				bGenerateBuses = bInGenerateBuses;
			}

			void DoWork()
			{
				SourceManager->GenerateSourceAudio(bGenerateBuses, StartSourceId, EndSourceId);
			}

			FORCEINLINE TStatId GetStatId() const
			{
				RETURN_QUICK_DECLARE_CYCLE_STAT(FAudioMixerSourceWorker, STATGROUP_ThreadPoolAsyncTasks);
			}
		};

		// Critical section to ensure mutating effect chains is thread-safe
		FCriticalSection EffectChainMutationCriticalSection;

		FMixerDevice* MixerDevice;

		// Cached ptr to an optional spatialization plugin
		TAudioSpatializationPtr SpatializationPlugin;

		// Array of pointers to game thread audio source objects
		TArray<FMixerSourceVoice*> MixerSources;

		// A command queue to execute commands from audio thread (or game thread) to audio mixer device thread.
		struct FCommands
		{
			TArray<TFunction<void()>> SourceCommandQueue;
		};

		FCommands CommandBuffers[2];
		FThreadSafeCounter RenderThreadCommandBufferIndex;

		FEvent* CommandsProcessedEvent;
		FCriticalSection CommandBufferIndexCriticalSection;

		TArray<int32> DebugSoloSources;

		struct FSourceInfo
		{
			FSourceInfo() {}
			~FSourceInfo() {}

			// Object which handles source buffer decoding
			TSharedPtr<FMixerSourceBuffer, ESPMode::ThreadSafe> MixerSourceBuffer;
			ISourceListener* SourceListener;

			// Data used for rendering sources
			TSharedPtr<FMixerSourceVoiceBuffer, ESPMode::ThreadSafe> CurrentPCMBuffer;
			int32 CurrentAudioChunkNumFrames;

			// The post-attenuation source buffer, used to send audio to submixes
			Audio::AlignedFloatBuffer SourceBuffer;
			Audio::AlignedFloatBuffer PreEffectBuffer;
			Audio::AlignedFloatBuffer PreDistanceAttenuationBuffer;
			Audio::AlignedFloatBuffer SourceEffectScratchBuffer;

			// Data used for delaying the rendering of source audio for sample-accurate quantization
			int32 SubCallbackDelayLengthInFrames{ 0 };
			Audio::TCircularAudioBuffer<float> SourceBufferDelayLine;

			TArray<float> CurrentFrameValues;
			TArray<float> NextFrameValues;
			float CurrentFrameAlpha;
			int32 CurrentFrameIndex;
			int64 NumFramesPlayed;

			// The number of frames to wait before starting the source
			double StartTime;

			TArray<FMixerSourceSubmixSend> SubmixSends;

			// What audio bus Id this source is sonfiying, if it is a source bus. This is INDEX_NONE for sources which are not source buses.
			uint32 AudioBusId;

			// Number of samples to count for source bus
			int64 SourceBusDurationFrames;

			// What buses this source is sending its audio to. Used to remove this source from the bus send list.
			TArray<uint32> AudioBusSends[(int32)EBusSendType::Count];

			// Interpolated source params
			FParam PitchSourceParam;
			float VolumeSourceStart;
			float VolumeSourceDestination;
			float VolumeFadeSlope;
			float VolumeFadeStart;
			int32 VolumeFadeFramePosition;
			int32 VolumeFadeNumFrames;

			float DistanceAttenuationSourceStart;
			float DistanceAttenuationSourceDestination;

			// Legacy filter LFP & HPF frequency set directly (not by modulation) on source
			float LowPassFreq;
			float HighPassFreq;

			// One-Pole LPFs and HPFs per source
			Audio::FInterpolatedLPF LowPassFilter;
			Audio::FInterpolatedHPF HighPassFilter;

			// Source effect instances
			uint32 SourceEffectChainId;
			TArray<TSoundEffectSourcePtr> SourceEffects;
			TArray<USoundEffectSourcePreset*> SourceEffectPresets;
			bool bEffectTailsDone;
			FSoundEffectSourceInputData SourceEffectInputData;

			FAudioPluginSourceOutputData AudioPluginOutputData;

			// A DSP object which tracks the amplitude envelope of a source.
			Audio::FEnvelopeFollower SourceEnvelopeFollower;
			float SourceEnvelopeValue;

			// Modulation destinations
			Audio::FModulationDestination VolumeModulation;
			Audio::FModulationDestination PitchModulation;
			Audio::FModulationDestination LowpassModulation;
			Audio::FModulationDestination HighpassModulation;

			// Modulation Base (i.e. Carrier) Values
			float VolumeModulationBase;
			float PitchModulationBase;
			float LowpassModulationBase;
			float HighpassModulationBase;

			FSpatializationParams SpatParams;
			Audio::AlignedFloatBuffer ScratchChannelMap;

			// Quantization data
			FQuartzQuantizedCommandHandle QuantizedCommandHandle;

			// State management
			uint8 bIs3D:1;
			uint8 bIsCenterChannelOnly:1;
			uint8 bIsActive:1;
			uint8 bIsPlaying:1;
			uint8 bIsPaused:1;
			uint8 bIsPausedForQuantization:1;
			uint8 bDelayLineSet:1;
			uint8 bIsStopping:1;
			uint8 bHasStarted:1;
			uint8 bIsBusy:1;
			uint8 bUseHRTFSpatializer:1;
			uint8 bIsExternalSend:1;
			uint8 bUseOcclusionPlugin:1;
			uint8 bUseReverbPlugin:1;
			uint8 bIsDone:1;
			uint8 bIsLastBuffer:1;
			uint8 bOutputToBusOnly:1;
			uint8 bIsVorbis:1;
			uint8 bIsSoundfield:1;
			uint8 bIsBypassingLPF:1;
			uint8 bIsBypassingHPF:1;
			uint8 bHasPreDistanceAttenuationSend:1;
			uint8 bModFiltersUpdated : 1;

			// Source format info
			int32 NumInputChannels;
			int32 NumPostEffectChannels;
			int32 NumInputFrames;

			// ID for associated Audio Component if there is one, 0 otherwise
			uint64 AudioComponentID;

			FORCEINLINE void ResetModulators(const Audio::FDeviceId InDeviceId)
			{
				VolumeModulation.Init(InDeviceId, FName("Volume"), false /* bInIsBuffered */, true /* bInValueLinear */);
				PitchModulation.Init(InDeviceId, FName("Pitch"));
				HighpassModulation.Init(InDeviceId, FName("HPFCutoffFrequency"));
				LowpassModulation.Init(InDeviceId, FName("LPFCutoffFrequency"));

				VolumeModulationBase = 0.0f;
				PitchModulationBase = 0.0f;
				HighpassModulationBase = MIN_FILTER_FREQUENCY;
				LowpassModulationBase = MAX_FILTER_FREQUENCY;
			}

#if AUDIO_MIXER_ENABLE_DEBUG_MODE
			uint8 bIsDebugMode : 1;
			FString DebugName;
#endif // AUDIO_MIXER_ENABLE_DEBUG_MODE
		};

		static void ApplyDistanceAttenuation(FSourceInfo& InSourceInfo, int32 NumSamples);
		void ComputePluginAudio(FSourceInfo& InSourceInfo, FMixerSourceSubmixOutputBuffer& InSourceSubmixOutputBuffer, int32 SourceId, int32 NumSamples);

		// Array of listener transforms
		TArray<FTransform> ListenerTransforms;

		// Array of source infos.
		TArray<FSourceInfo> SourceInfos;

		// This array is independent of SourceInfos array to optimize for cache coherency
		TArray<FMixerSourceSubmixOutputBuffer> SourceSubmixOutputBuffers;

		// Map of bus object Id's to audio bus data. 
		TMap<uint32, TSharedPtr<FMixerAudioBus>> AudioBuses;
		TArray<uint32> AudioBusIds_AudioThread;

		// Async task workers for processing sources in parallel
		TArray<FAsyncTask<FAudioMixerSourceWorker>*> SourceWorkers;

		// Array of task data waiting to finished. Processed on audio render thread.
		TArray<TSharedPtr<FMixerSourceBuffer, ESPMode::ThreadSafe>> PendingSourceBuffers;

		// General information about sources in source manager accessible from game thread
		struct FGameThreadInfo
		{
			TArray<int32> FreeSourceIndices;
			TArray<bool> bIsBusy;
			TArray<bool> bNeedsSpeakerMap;
			TArray<bool> bIsDebugMode;
			TArray<bool> bIsUsingHRTFSpatializer;
		} GameThreadInfo;

		int32 NumActiveSources;
		int32 NumTotalSources;
		int32 NumOutputFrames;
		int32 NumOutputSamples;
		int32 NumSourceWorkers;

		// Commands queued up to execute
		FThreadSafeCounter NumCommands;

		uint8 bInitialized : 1;
		uint8 bUsingSpatializationPlugin : 1;
		int32 MaxChannelsSupportedBySpatializationPlugin;

		// Set to true when the audio source manager should pump the command queue
		FThreadSafeBool bPumpQueue;
		uint64 LastPumpTimeInCycles = 0;

		friend class FMixerSourceVoice;
	};
}
