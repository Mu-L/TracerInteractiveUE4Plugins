// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioMixer.h"
#include "AudioMixerSourceManager.h"
#include "AudioDevice.h"

class IAudioMixerPlatformInterface;

namespace Audio
{
	class FMixerSourceVoice;

	struct FChannelPositionInfo
	{
		EAudioMixerChannel::Type Channel;
		int32 Azimuth;

		FChannelPositionInfo()
			: Channel(EAudioMixerChannel::Unknown)
			, Azimuth(0)
		{}

		FChannelPositionInfo(EAudioMixerChannel::Type InChannel, int32 InAzimuth)
			: Channel(InChannel)
			, Azimuth(InAzimuth)
		{}
	};

	/** Data used to schedule events automatically in the audio renderer in audio mixer. */
	struct FAudioThreadTimingData
	{
		/** The time since audio device started. */
		double StartTime;

		/** The clock of the audio thread, periodically synced to the audio render thread time. */
		double AudioThreadTime;

		/** The clock of the audio render thread. */
		double AudioRenderThreadTime;

		/** The current audio thread fraction for audio events relative to the render thread. */
		double AudioThreadTimeJitterDelta;

		FAudioThreadTimingData()
			: StartTime(0.0)
			, AudioThreadTime(0.0)
			, AudioRenderThreadTime(0.0)
			, AudioThreadTimeJitterDelta(0.05)
		{}
	};

	// Master submixes
	namespace EMasterSubmixType
	{
		enum Type
		{
			Master,
			Reverb,
			ReverbPlugin,
			EQ,
			Ambisonics,
			Count,
		};
	}

	class AUDIOMIXER_API FMixerDevice :	public FAudioDevice,
										public IAudioMixer
	{
	public:
		FMixerDevice(IAudioMixerPlatformInterface* InAudioMixerPlatform);
		~FMixerDevice();

		//~ Begin FAudioDevice
		virtual void UpdateDeviceDeltaTime() override;
		virtual void GetAudioDeviceList(TArray<FString>& OutAudioDeviceNames) const override;
		virtual bool InitializeHardware() override;
		virtual void FadeIn() override;
		virtual void FadeOut() override;
		virtual void TeardownHardware() override;
		virtual void UpdateHardwareTiming() override;
		virtual void UpdateGameThread() override;
		virtual void UpdateHardware() override;
		virtual double GetAudioTime() const override;
		virtual FAudioEffectsManager* CreateEffectsManager() override;
		virtual FSoundSource* CreateSoundSource() override;
		virtual FName GetRuntimeFormat(USoundWave* SoundWave) override;
		virtual bool HasCompressedAudioInfoClass(USoundWave* SoundWave) override;
		virtual bool SupportsRealtimeDecompression() const override;
		virtual bool DisablePCMAudioCaching() const override;
		virtual class ICompressedAudioInfo* CreateCompressedAudioInfo(USoundWave* SoundWave) override;
		virtual bool ValidateAPICall(const TCHAR* Function, uint32 ErrorCode) override;
		virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override;
		virtual void CountBytes(class FArchive& Ar) override;
		virtual bool IsExernalBackgroundSoundActive() override;
		virtual void ResumeContext() override;
		virtual void SuspendContext() override;
		virtual void EnableDebugAudioOutput() override;
		virtual void InitSoundSubmixes() override;
		virtual FAudioPlatformSettings GetPlatformSettings() const override;
		virtual void RegisterSoundSubmix(USoundSubmix* SoundSubmix, bool bInit = true) override;
		virtual void UnregisterSoundSubmix(USoundSubmix* SoundSubmix) override;

		virtual void InitSoundEffectPresets() override;
		virtual int32 GetNumActiveSources() const override;

		// Updates the source effect chain (using unique object id). 
		virtual void UpdateSourceEffectChain(const uint32 SourceEffectChainId, const TArray<FSourceEffectChainEntry>& SourceEffectChain, const bool bPlayEffectChainTails) override;
		virtual bool GetCurrentSourceEffectChain(const uint32 SourceEffectChainId, TArray<FSourceEffectChainEntry>& OutCurrentSourceEffectChainEntries) override;

		// Updates submix instances with new properties
		virtual void UpdateSubmixProperties(USoundSubmix* InSubmix) override;
		
		// Sets the submix output volume dynamically
		virtual void SetSubmixOutputVolume(USoundSubmix* InSubmix, float NewVolume) override;

		// Submix recording callbacks:
		virtual void StartRecording(USoundSubmix* InSubmix, float ExpectedRecordingDuration) override;
		virtual Audio::AlignedFloatBuffer& StopRecording(USoundSubmix* InSubmix, float& OutNumChannels, float& OutSampleRate) override;

		virtual void PauseRecording(USoundSubmix* InSubmix);
		virtual void ResumeRecording(USoundSubmix* InSubmix);

		/** Submix envelope following */
		virtual void StartEnvelopeFollowing(USoundSubmix* InSubmix) override;
		virtual void StopEnvelopeFollowing(USoundSubmix* InSubmix) override;
		virtual void AddEnvelopeFollowerDelegate(USoundSubmix* InSubmix, const FOnSubmixEnvelopeBP& OnSubmixEnvelopeBP) override;

		/** Submix Spectrum Analysis */
		virtual void StartSpectrumAnalysis(USoundSubmix* InSubmix, const Audio::FSpectrumAnalyzerSettings& InSettings) override;
		virtual void StopSpectrumAnalysis(USoundSubmix* InSubmix) override;
		virtual void GetMagnitudesForFrequencies(USoundSubmix* InSubmix, const TArray<float>& InFrequencies, TArray<float>& OutMagnitudes);
		virtual void GetPhasesForFrequencies(USoundSubmix* InSubmix, const TArray<float>& InFrequencies, TArray<float>& OutPhases);

		// Submix buffer listener callbacks
		virtual void RegisterSubmixBufferListener(ISubmixBufferListener* InSubmixBufferListener, USoundSubmix* InSubmix = nullptr) override;
		virtual void UnregisterSubmixBufferListener(ISubmixBufferListener* InSubmixBufferListener, USoundSubmix* InSubmix = nullptr) override;

		virtual void FlushAudioRenderingCommands(bool bPumpSynchronously = false) override;
		//~ End FAudioDevice

		//~ Begin IAudioMixer
		virtual bool OnProcessAudioStream(AlignedFloatBuffer& OutputBuffer) override;
		virtual void OnAudioStreamShutdown() override;
		//~ End IAudioMixer

		FMixerSubmixWeakPtr GetSubmixInstance(USoundSubmix* SoundSubmix);

		// Functions which check the thread it's called on and helps make sure functions are called from correct threads
		void CheckAudioThread() const;
		void CheckAudioRenderingThread() const;
		bool IsAudioRenderingThread() const;

		// Public Functions
		FMixerSourceVoice* GetMixerSourceVoice();
		void ReleaseMixerSourceVoice(FMixerSourceVoice* InSourceVoice);
		int32 GetNumSources() const;

		const FAudioPlatformDeviceInfo& GetPlatformDeviceInfo() const { return PlatformInfo; };

		int32 GetNumDeviceChannels() const { return PlatformInfo.NumChannels; }

		int32 GetNumOutputFrames() const { return PlatformSettings.CallbackBufferFrameSize; }

		// Builds a 3D channel map for a spatialized source.
		void Get3DChannelMap(const ESubmixChannelFormat InSubmixChannelType, const FWaveInstance* InWaveInstance, const float EmitterAzimuth, const float NormalizedOmniRadius, Audio::AlignedFloatBuffer& OutChannelMap);

		// Builds a channel gain matrix for a non-spatialized source. The non-static variation of this function queries AudioMixerDevice->NumOutputChannels directly which may not be thread safe.
		void Get2DChannelMap(bool bIsVorbis, const ESubmixChannelFormat InSubmixChannelType, const int32 NumSourceChannels, const bool bIsCenterChannelOnly, Audio::AlignedFloatBuffer& OutChannelMap) const;
		static void Get2DChannelMap(bool bIsVorbis, const int32 NumSourceChannels, const int32 NumOutputChannels, const bool bIsCenterChannelOnly, Audio::AlignedFloatBuffer& OutChannelMap);

		int32 GetDeviceSampleRate() const;
		int32 GetDeviceOutputChannels() const;

		FMixerSourceManager* GetSourceManager();

		FMixerSubmixWeakPtr GetMasterSubmix(); 
		FMixerSubmixWeakPtr GetMasterReverbSubmix();
		FMixerSubmixWeakPtr GetMasterReverbPluginSubmix();
		FMixerSubmixWeakPtr GetMasterEQSubmix();
		FMixerSubmixWeakPtr GetMasterAmbisonicsSubmix();

		// Add submix effect to master submix
		void AddMasterSubmixEffect(uint32 SubmixEffectId, FSoundEffectSubmix* SoundEffect);
		
		// Remove submix effect from master submix
		void RemoveMasterSubmixEffect(uint32 SubmixEffectId);
		
		// Clear all submix effects from master submix
		void ClearMasterSubmixEffects();

		// Returns the number of channels for a given submix channel type
		int32 GetNumChannelsForSubmixFormat(const ESubmixChannelFormat InSubmixChannelType) const;
		ESubmixChannelFormat GetSubmixChannelFormatForNumChannels(const int32 InNumChannels) const;

		uint32 GetNewUniqueAmbisonicsStreamID();

		// Returns the channel array for the given submix channel type
		const TArray<EAudioMixerChannel::Type>& GetChannelArrayForSubmixChannelType(const ESubmixChannelFormat InSubmixChannelType) const;

		// Retrieves the listener transforms
		const TArray<FTransform>* GetListenerTransforms();

		// Audio thread tick timing relative to audio render thread timing
		double GetAudioThreadTime() const { return AudioThreadTimingData.AudioThreadTime; }
		double GetAudioRenderThreadTime() const { return AudioThreadTimingData.AudioRenderThreadTime; }
		double GetAudioClockDelta() const { return AudioClockDelta; }

	protected:

		virtual void OnListenerUpdated(const TArray<FListener>& InListeners) override;

		TArray<FTransform> ListenerTransforms;

	private:
		// Resets the thread ID used for audio rendering
		void ResetAudioRenderingThreadId();

		void Get2DChannelMapInternal(const int32 NumSourceChannels, const int32 NumOutputChannels, const bool bIsCenterChannelOnly, TArray<float>& OutChannelMap) const;
		void InitializeChannelMaps();
		static int32 GetChannelMapCacheId(const int32 NumSourceChannels, const int32 NumOutputChannels, const bool bIsCenterChannelOnly);
		void CacheChannelMap(const int32 NumSourceChannels, const int32 NumOutputChannels, const bool bIsCenterChannelOnly);
		void InitializeChannelAzimuthMap(const int32 NumChannels);

		void WhiteNoiseTest(AlignedFloatBuffer& Output);
		void SineOscTest(AlignedFloatBuffer& Output);

		bool IsMainAudioDevice() const;

	private:

		bool IsMasterSubmixType(USoundSubmix* InSubmix) const;
		FMixerSubmix* GetMasterSubmixInstance(USoundSubmix* InSubmix);

		// Pushes the command to a audio render thread command queue to be executed on render thread
		void AudioRenderThreadCommand(TFunction<void()> Command);
		
		// Pumps the audio render thread command queue
		void PumpCommandQueue();

		static TArray<USoundSubmix*> MasterSubmixes;
		TArray<FMixerSubmixPtr> MasterSubmixInstances;

		/** Ptr to the platform interface, which handles streaming audio to the hardware device. */
		IAudioMixerPlatformInterface* AudioMixerPlatform;
		
		/** Contains a map of channel/speaker azimuth positions. */
		FChannelPositionInfo DefaultChannelAzimuthPosition[EAudioMixerChannel::MaxSupportedChannel];

		/** The azimuth positions for submix channel types. */
		TMap<ESubmixChannelFormat, TArray<FChannelPositionInfo>> ChannelAzimuthPositions;

		int32 OutputChannels[(int32)ESubmixChannelFormat::Count];

		/** Channel type arrays for submix channel types. */
		TMap<ESubmixChannelFormat, TArray<EAudioMixerChannel::Type>> ChannelArrays;

		/** What upmix method to use for mono channel upmixing. */
		EMonoChannelUpmixMethod MonoChannelUpmixMethod;

		/** What panning method to use for panning. */
		EPanningMethod PanningMethod;

		/** The audio output stream parameters used to initialize the audio hardware. */
		FAudioMixerOpenStreamParams OpenStreamParams;

		/** The time delta for each callback block. */
		double AudioClockDelta;

		/** The audio clock from device initialization, updated at block rate. */
		double AudioClock;

		/** What the previous master volume was. */
		float PreviousMasterVolume;

		/** Timing data for audio thread. */
		FAudioThreadTimingData AudioThreadTimingData;

		/** The platform device info for this mixer device. */
		FAudioPlatformDeviceInfo PlatformInfo;

		/** Map of USoundSubmix static data objects to the dynamic audio mixer submix. */
		TMap<USoundSubmix*, FMixerSubmixPtr> Submixes;

		/** Which submixes have been told to envelope follow with this audio device. */
		TArray<USoundSubmix*> EnvelopeFollowingSubmixes;

		/** Queue of mixer source voices. */
		TQueue<FMixerSourceVoice*> SourceVoices;

		TMap<uint32, TArray<FSourceEffectChainEntry>> SourceEffectChainOverrides;

		/** The mixer source manager. */
		FMixerSourceManager SourceManager;

		/** ThreadId for the game thread (or if audio is running a seperate thread, that ID) */
		mutable int32 GameOrAudioThreadId;

		/** ThreadId for the low-level platform audio mixer. */
		mutable int32 AudioPlatformThreadId;

		/** Command queue to send commands to audio render thread from game thread or audio thread. */
		TQueue<TFunction<void()>> CommandQueue;

		/** Whether or not we generate output audio to test multi-platform mixer. */
		bool bDebugOutputEnabled;
	};
}

