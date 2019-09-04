// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioMixer.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SampleBuffer.h"
#include "DSP/EnvelopeFollower.h"
#include "DSP/SpectrumAnalyzer.h"

class USoundEffectSubmix;

namespace Audio
{
	class IAudioMixerEffect;
	class FMixerSourceVoice;
	class FMixerDevice;

	struct FSubmixVoiceData
	{
		float SendLevel;
		uint32 AmbisonicsEncoderId;
		FAmbisonicsEncoderInputData CachedEncoderInputData;

		FSubmixVoiceData()
			: SendLevel(1.0f)
			, AmbisonicsEncoderId(INDEX_NONE)
		{
		}
	};

	class FMixerSubmix;

	struct FChildSubmixInfo
	{
		TWeakPtr<FMixerSubmix, ESPMode::ThreadSafe> SubmixPtr;
		bool bNeedsAmbisonicsEncoding;

		FChildSubmixInfo()
			: bNeedsAmbisonicsEncoding(true)
		{
		}
	};

	class FMixerSubmix
	{
	public:
		FMixerSubmix(FMixerDevice* InMixerDevice);
		virtual ~FMixerSubmix();

		// Initialize the submix object with the USoundSubmix ptr. Sets up child and parent connects.
		void Init(USoundSubmix* InSoundSubmix);

		// Returns the mixer submix Id
		uint32 GetId() const { return Id; }

		// Sets the parent submix to the given submix
		void SetParentSubmix(TWeakPtr<FMixerSubmix, ESPMode::ThreadSafe> Submix);

		// Adds the given submix to this submix's children
		void AddChildSubmix(TWeakPtr<FMixerSubmix, ESPMode::ThreadSafe> Submix);

		// Sets the static output volume of the submix
		void SetOutputVolume(float InVolume);

		// Sets the dynamic output volume
		void SetDynamicOutputVolume(float InVolume);

		// Gets the submix channels channels
		ESubmixChannelFormat GetSubmixChannels() const;

		// Gets this submix's parent submix
		TWeakPtr<FMixerSubmix, ESPMode::ThreadSafe> GetParentSubmix();

		// Returns the number of source voices currently a part of this submix.
		int32 GetNumSourceVoices() const;

		// Returns the number of wet effects in this submix.
		int32 GetNumEffects() const;

		// Add (if not already added) or sets the amount of the source voice's send amount
		void AddOrSetSourceVoice(FMixerSourceVoice* InSourceVoice, const float SendLevel);

		/** Removes the given source voice from the submix. */
		void RemoveSourceVoice(FMixerSourceVoice* InSourceVoice);

		/** Appends the effect submix to the effect submix chain. */
		void AddSoundEffectSubmix(uint32 SubmixPresetId, FSoundEffectSubmix* InSoundEffectSubmix);

		/** Removes the submix effect from the effect submix chain. */
		void RemoveSoundEffectSubmix(uint32 SubmixPresetId);

		/** Clears all submix effects from the effect submix chain. */
		void ClearSoundEffectSubmixes();

		/** Whether or not this submix instance is muted. */
		void SetBackgroundMuted(bool bInMuted);

		// Function which processes audio.
		void ProcessAudio(const ESubmixChannelFormat ParentInputChannels, AlignedFloatBuffer& OutAudio);

		// Returns the device sample rate this submix is rendering to
		int32 GetSampleRate() const;

		// Returns the output channels this submix is rendering to
		int32 GetNumOutputChannels() const;

		// Returns the number of effects in this submix's effect chain
		int32 GetNumChainEffects() const;

		// Returns the submix effect at the given effect chain index
		FSoundEffectSubmix* GetSubmixEffect(const int32 InIndex);

		// updates settings, potentially creating or removing ambisonics streams based on
		void OnAmbisonicsSettingsChanged(UAmbisonicsSubmixSettingsBase* AmbisonicsSettings);

		// This is called by the corresponding USoundSubmix when StartRecordingOutput is called.
		void OnStartRecordingOutput(float ExpectedDuration);

		// This is called by the corresponding USoundSubmix when StopRecordingOutput is called.
		AlignedFloatBuffer& OnStopRecordingOutput(float& OutNumChannels, float& OutSampleRate);

		// This is called by the corresponding USoundSubmix when PauseRecording is called.
		void PauseRecordingOutput();

		// This is called by the corresponding USoundSubmix when ResumeRecording is called.
		void ResumeRecordingOutput();

		// Register buffer listener with this submix
		void RegisterBufferListener(ISubmixBufferListener* BufferListener);
		
		// Unregister buffer listener with this submix
		void UnregisterBufferListener(ISubmixBufferListener* BufferListener);

		// Starts envelope following with the given attack time and release time
		void StartEnvelopeFollowing(int32 AttackTime, int32 ReleaseTime);

		// Stops envelope following the submix
		void StopEnvelopeFollowing();

		// Adds an envelope follower delegate
		void AddEnvelopeFollowerDelegate(const FOnSubmixEnvelopeBP& OnSubmixEnvelopeBP);

		// Initializes a new FFT analyzer for this submix and immediately begins feeding audio to it.
		void StartSpectrumAnalysis(const FSpectrumAnalyzerSettings& InSettings);

		// Terminates whatever FFT Analyzer is being used for this submix.
		void StopSpectrumAnalysis();

		// Gets the most recent magnitude values for each corresponding value in InFrequencies (in Hz).
		// This requires StartSpectrumAnalysis to be called first.
		void GetMagnitudeForFrequencies(const TArray<float>& InFrequencies, TArray<float>& OutMagnitudes);

		// Gets the most recent phase values for each corresponding value in InFrequencies (in Hz).
		// This requires StartSpectrumAnalysis to be called first.
		void GetPhaseForFrequencies(const TArray<float>& InFrequencies, TArray<float>& OutPhases);

		// Broadcast the envelope value on the game thread
		void BroadcastEnvelope();

	protected:
		// Initialize the submix internal 
		void InitInternal();

		// Down mix the given buffer to the desired down mix channel count
		void FormatChangeBuffer(const ESubmixChannelFormat NewChannelType, AlignedFloatBuffer& InBuffer, AlignedFloatBuffer& OutNewBuffer);

		void MixBufferDownToMono(const AlignedFloatBuffer& InBuffer, int32 NumInputChannels, AlignedFloatBuffer& OutBuffer);

		// Set up ambisonics encoder. Called when ambisonics settings are changed.
		void SetUpAmbisonicsEncoder();

		// Set up ambisonics decoder. Called when ambisonics settings are changed.
		void SetUpAmbisonicsDecoder();

		// Clean up ambisonics encoder.
		void TearDownAmbisonicsEncoder();

		// Clean up ambisonics decoder.
		void TearDownAmbisonicsDecoder();

		// Check if we need to encode for ambisonics for childen (TODO)
		void UpdateAmbisonicsEncoderForChildren();

		// Check to see if we need to decode from ambisonics for parent
		void UpdateAmbisonicsDecoderForParent();

		// This sets up the ambisonics positional data for speakers, based on what new format we need to convert to.
		void SetUpAmbisonicsPositionalData();

		// Encode a source and sum it into the AmbisonicsBuffer.
		void EncodeAndMixInSource(AlignedFloatBuffer& InAudioData, FSubmixVoiceData& InVoiceInfo);

		// Encodes child submix into ambisonics (TODO)
		void EncodeAndMixInChildSubmix(FChildSubmixInfo& Child);

	public:
		// Cached pointer to ambisonics settings.
		UAmbisonicsSubmixSettingsBase* AmbisonicsSettings;

	protected:

		// Pump command queue
		void PumpCommandQueue();

		// Add command to the command queue
		void SubmixCommand(TFunction<void()> Command);

		// This mixer submix's Id
		uint32 Id;

		// Parent submix. 
		TWeakPtr<FMixerSubmix, ESPMode::ThreadSafe> ParentSubmix;

		// Child submixes
		TMap<uint32, FChildSubmixInfo> ChildSubmixes;

		// Info struct for a submix effect instance
		struct FSubmixEffectInfo
		{
			// The preset object id used to spawn this effect instance
			uint32 PresetId;

			// The effect instance ptr
			FSoundEffectSubmix* EffectInstance;

			FSubmixEffectInfo()
				: PresetId(INDEX_NONE)
				, EffectInstance(nullptr)
			{
			}
		};

		// The effect chain of this submix, based on the sound submix preset chain
		TArray<FSubmixEffectInfo> EffectSubmixChain;

		// Owning mixer device. 
		FMixerDevice* MixerDevice;

		// Map of mixer source voices with a given send level for this submix
		TMap<FMixerSourceVoice*, FSubmixVoiceData> MixerSourceVoices;

		AlignedFloatBuffer ScratchBuffer;
		AlignedFloatBuffer InputBuffer;
		AlignedFloatBuffer DownmixedBuffer;
		AlignedFloatBuffer SourceInputBuffer;

		ESubmixChannelFormat ChannelFormat;
		int32 NumChannels;
		int32 NumSamples;

		// Cached ambisonics mixer
		TAmbisonicsMixerPtr AmbisonicsMixer;

		// Encoder ID set up with Ambisonics Mixer. Set to INDEX_NONE if there is no encoder stream open.
		uint32 SubmixAmbisonicsEncoderID;

		// Decoder ID set up with Ambisonics Mixer. Set to INDEX_NONE if there is no decoder stream open.
		uint32 SubmixAmbisonicsDecoderID;

		// The output volume of the submix set via the USoundSubmix property. Can be set in the editor.
		float InitializedOutputVolume;
		
		// The current dynamic output volume
		float OutputVolume;
		
		// The target dynamic output volume
		float TargetOutputVolume;

		// Envelope following data
		float EnvelopeValues[AUDIO_MIXER_MAX_OUTPUT_CHANNELS];
		Audio::FEnvelopeFollower EnvelopeFollowers[AUDIO_MIXER_MAX_OUTPUT_CHANNELS];
		int32 EnvelopeNumChannels;
		FCriticalSection EnvelopeCriticalSection;

		// Spectrum analyzer:
		TUniquePtr<FSpectrumAnalyzer> SpectrumAnalyzer;
		
		// This buffer is used to downmix the submix output to mono before submitting it to the SpectrumAnalyzer.
		AlignedFloatBuffer MonoMixBuffer;

		// This buffer is encoded into for each source, then summed into the ambisonics buffer.
		AlignedFloatBuffer InputAmbisonicsBuffer;

		// Cached positional data for Ambisonics decoder.
		FAmbisonicsDecoderPositionalData CachedPositionalData;

		// Submix command queue to shuffle commands from audio thread to audio render thread.
		TQueue<TFunction<void()>> CommandQueue;

		// List of submix buffer listeners
		TArray<ISubmixBufferListener*> BufferListeners;

		// Critical section used for modifying and interacting with buffer listeners
		FCriticalSection BufferListenerCriticalSection;

		// This buffer is used for recorded output of the submix.
		AlignedFloatBuffer RecordingData;

		// Bool set to true when this submix is recording data.
		uint8 bIsRecording : 1;

		// Whether or not this submix is muted.
		uint8 bIsBackgroundMuted : 1;

		// Whether or not to apply a volume scale to output
		uint8 bApplyOutputVolumeScale : 1;

		// Bool set to true when envelope following is enabled
		FThreadSafeBool bIsEnvelopeFollowing;

		// Multi-cast delegate to broadcast envelope data from this submix instance
		FOnSubmixEnvelope OnSubmixEnvelope;

		// Critical section used for when we are appending recorded data.
		FCriticalSection RecordingCriticalSection;

		// Handle back to the owning USoundSubmix. Used when the device is shutdown to prematurely end a recording.
		USoundSubmix* OwningSubmixObject;

		friend class FMixerDevice;
	};



}
