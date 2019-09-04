// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AudioMixerSubmix.h"
#include "AudioMixerDevice.h"
#include "AudioMixerSourceVoice.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundEffectSubmix.h"
#include "ProfilingDebugging/CsvProfiler.h"

// Link to "Audio" profiling category
CSV_DECLARE_CATEGORY_MODULE_EXTERN(AUDIOMIXER_API, Audio);

namespace Audio
{
	// Unique IDs for mixer submixe's
	static uint32 GSubmixMixerIDs = 0;

	FMixerSubmix::FMixerSubmix(FMixerDevice* InMixerDevice)
		: AmbisonicsSettings(nullptr)
		, Id(GSubmixMixerIDs++)
		, ParentSubmix(nullptr)
		, MixerDevice(InMixerDevice)
		, ChannelFormat(ESubmixChannelFormat::Device)
		, NumChannels(0)
		, NumSamples(0)
		, SubmixAmbisonicsEncoderID(INDEX_NONE)
		, SubmixAmbisonicsDecoderID(INDEX_NONE)
		, InitializedOutputVolume(1.0f)
		, OutputVolume(1.0f)
		, TargetOutputVolume(1.0f)
		, EnvelopeNumChannels(0)
		, bIsRecording(false)
		, bIsBackgroundMuted(false)
		, bApplyOutputVolumeScale(false)
		, OwningSubmixObject(nullptr)
	{
	}

	FMixerSubmix::~FMixerSubmix()
	{
		ClearSoundEffectSubmixes();
		if (SubmixAmbisonicsEncoderID != INDEX_NONE)
		{
			TearDownAmbisonicsEncoder();
		}

		if (SubmixAmbisonicsDecoderID != INDEX_NONE)
		{
			TearDownAmbisonicsDecoder();
		}

		if (OwningSubmixObject && bIsRecording)
		{
			FString InterruptedFileName = TEXT("InterruptedRecording.wav");
			UE_LOG(LogAudioMixer, Warning, TEXT("Recording of Submix %s was interrupted. Saving interrupted recording as %s."), *(OwningSubmixObject->GetName()), *InterruptedFileName);
			OwningSubmixObject->StopRecordingOutput(MixerDevice, EAudioRecordingExportType::WavFile, InterruptedFileName, FString());
		}
	}

	void FMixerSubmix::Init(USoundSubmix* InSoundSubmix)
	{
		check(IsInAudioThread());
		if (InSoundSubmix != nullptr)
		{
			// This is a first init and needs to be synchronous
			if (!OwningSubmixObject)
			{
				OwningSubmixObject = InSoundSubmix;
				InitInternal();
			}
			else
			{
				// This is a re-init and needs to be thread safe
				check(OwningSubmixObject == InSoundSubmix);
				SubmixCommand([this]()
				{
					InitInternal();
				});
			}
		}
	}

	void FMixerSubmix::InitInternal()
	{
		// Set the initialized output volume
		InitializedOutputVolume = FMath::Clamp(OwningSubmixObject->OutputVolume, 0.0f, 1.0f);

		if (!FMath::IsNearlyEqual(InitializedOutputVolume, 1.0f))
		{
			bApplyOutputVolumeScale = true;
		}

		// Loop through the submix's presets and make new instances of effects in the same order as the presets
		ClearSoundEffectSubmixes();

		for (USoundEffectSubmixPreset* EffectPreset : OwningSubmixObject->SubmixEffectChain)
		{
			if (EffectPreset)
			{
				// Create a new effect instance using the preset
				FSoundEffectSubmix* SubmixEffect = static_cast<FSoundEffectSubmix*>(EffectPreset->CreateNewEffect());

				FSoundEffectSubmixInitData InitData;
				InitData.SampleRate = MixerDevice->GetSampleRate();
				InitData.PresetSettings = nullptr;

				// Now set the preset
				SubmixEffect->Init(InitData);
				SubmixEffect->SetPreset(EffectPreset);
				SubmixEffect->SetEnabled(true);

				FSubmixEffectInfo EffectInfo;
				EffectInfo.PresetId = EffectPreset->GetUniqueID();
				EffectInfo.EffectInstance = SubmixEffect;

				// Add the effect to this submix's chain
				EffectSubmixChain.Add(EffectInfo);
			}
		}

		ChannelFormat = OwningSubmixObject->ChannelFormat;

		if (ChannelFormat == ESubmixChannelFormat::Ambisonics)
		{
			//Get the ambisonics mixer.
			AmbisonicsMixer = MixerDevice->GetAmbisonicsMixer();

			//If we do have a valid ambisonics decoder, lets use it. Otherwise, treat this submix like a device submix.
			if (AmbisonicsMixer.IsValid())
			{
				if (!OwningSubmixObject->AmbisonicsPluginSettings)
				{
					OwningSubmixObject->AmbisonicsPluginSettings = AmbisonicsMixer->GetDefaultSettings();
				}

				if (OwningSubmixObject->AmbisonicsPluginSettings != nullptr)
				{
					OnAmbisonicsSettingsChanged(OwningSubmixObject->AmbisonicsPluginSettings);
				}
				else
				{
					//Default to first order ambisonics.
					NumChannels = 4;
					const int32 NumOutputFrames = MixerDevice->GetNumOutputFrames();
					NumSamples = NumChannels * NumOutputFrames;
				}
			}
			else
			{
				// There is no valid ambisonics decoder, so fall back to standard downmixing.
				ChannelFormat = ESubmixChannelFormat::Device;
				NumChannels = MixerDevice->GetNumChannelsForSubmixFormat(ChannelFormat);
				const int32 NumOutputFrames = MixerDevice->GetNumOutputFrames();
				NumSamples = NumChannels * NumOutputFrames;
			}
		}
		else
		{
			NumChannels = MixerDevice->GetNumChannelsForSubmixFormat(ChannelFormat);
			const int32 NumOutputFrames = MixerDevice->GetNumOutputFrames();
			NumSamples = NumChannels * NumOutputFrames;
		}
	}

	void FMixerSubmix::SetParentSubmix(TWeakPtr<FMixerSubmix, ESPMode::ThreadSafe> SubmixWeakPtr)
	{
		SubmixCommand([this, SubmixWeakPtr]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

			ParentSubmix = SubmixWeakPtr;

			if (ChannelFormat == ESubmixChannelFormat::Ambisonics && AmbisonicsMixer.IsValid())
			{
				UpdateAmbisonicsDecoderForParent();
			}
		});
	}

	void FMixerSubmix::AddChildSubmix(TWeakPtr<FMixerSubmix, ESPMode::ThreadSafe> SubmixWeakPtr)
	{
		SubmixCommand([this, SubmixWeakPtr]()
		{
			AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

			FChildSubmixInfo NewChildSubmixInfo;
			NewChildSubmixInfo.SubmixPtr = SubmixWeakPtr;

			//TODO: switch this conditionally when we are able to route submixes to ambisonics submix.
			NewChildSubmixInfo.bNeedsAmbisonicsEncoding = false;

			TSharedPtr<Audio::FMixerSubmix, ESPMode::ThreadSafe> SubmixSharedPtr = SubmixWeakPtr.Pin();
			if (SubmixSharedPtr.IsValid())
			{
				ChildSubmixes.Add(SubmixSharedPtr->GetId(), NewChildSubmixInfo);

				if (ChannelFormat == ESubmixChannelFormat::Ambisonics && AmbisonicsMixer.IsValid())
				{
					UpdateAmbisonicsEncoderForChildren();
				}
			}
		});
	}

	ESubmixChannelFormat FMixerSubmix::GetSubmixChannels() const
	{
		return ChannelFormat;
	}

	TWeakPtr<FMixerSubmix, ESPMode::ThreadSafe> FMixerSubmix::GetParentSubmix()
	{
		return ParentSubmix;
	}

	int32 FMixerSubmix::GetNumSourceVoices() const
	{
		return MixerSourceVoices.Num();
	}

	int32 FMixerSubmix::GetNumEffects() const
	{
		return EffectSubmixChain.Num();
	}

	void FMixerSubmix::AddOrSetSourceVoice(FMixerSourceVoice* InSourceVoice, const float InSendLevel)
	{
		AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

		FSubmixVoiceData NewVoiceData;
		NewVoiceData.SendLevel = InSendLevel;
		
		// If this is an ambisonics submix, set up a new encoder stream.
		if (ChannelFormat == ESubmixChannelFormat::Ambisonics && AmbisonicsMixer.IsValid())
		{
			//TODO: If a souce is not an ambisonics source, we need to set up an encoder for it.
			const bool bSourceIsAmbisonics = true;
			if (!bSourceIsAmbisonics)
			{
				NewVoiceData.AmbisonicsEncoderId = MixerDevice->GetNewUniqueAmbisonicsStreamID();
				AmbisonicsMixer->OnOpenEncodingStream(NewVoiceData.AmbisonicsEncoderId, AmbisonicsSettings);
			}
			else
			{
				NewVoiceData.AmbisonicsEncoderId = INDEX_NONE;
			}
		}

		MixerSourceVoices.Add(InSourceVoice, NewVoiceData);
	}

	void FMixerSubmix::RemoveSourceVoice(FMixerSourceVoice* InSourceVoice)
	{
		AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);
		
		// If the source has a corresponding ambisonics encoder, close it out.
		uint32 SourceEncoderID = INDEX_NONE;
		const FSubmixVoiceData* MixerSourceVoiceData = MixerSourceVoices.Find(InSourceVoice);
		if (MixerSourceVoiceData)
		{
			SourceEncoderID = MixerSourceVoiceData->AmbisonicsEncoderId;
		}
		
		if (SourceEncoderID != INDEX_NONE)
		{
			check(AmbisonicsMixer.IsValid());
			AmbisonicsMixer->OnCloseEncodingStream(SourceEncoderID);
		}

		// If we did find a valid corresponding FSubmixVoiceData, remove it from the map.
		if (MixerSourceVoiceData)
		{
			int32 NumRemoved = MixerSourceVoices.Remove(InSourceVoice);
			AUDIO_MIXER_CHECK(NumRemoved == 1);
		}
	}

	void FMixerSubmix::AddSoundEffectSubmix(uint32 SubmixPresetId, FSoundEffectSubmix* InSoundEffectSubmix)
	{
		AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

		// Look to see if the submix preset ID is already present
		for (int32 i = 0; i < EffectSubmixChain.Num(); ++i)
		{
			if (EffectSubmixChain[i].PresetId == SubmixPresetId)
			{
				// Already added. 
				return;
			}
		}

		// This is now owned by FMixerSubmix
		FSubmixEffectInfo Info;
		Info.PresetId = SubmixPresetId;
		Info.EffectInstance = InSoundEffectSubmix;

		EffectSubmixChain.Add(Info);
	}

	void FMixerSubmix::RemoveSoundEffectSubmix(uint32 SubmixPresetId)
	{
		AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

		for (int32 i = 0; i < EffectSubmixChain.Num(); ++i)
		{
			// If the ID's match, delete and remove the effect instance but don't modify the effect submix chain array itself
			if (EffectSubmixChain[i].PresetId == SubmixPresetId)
			{
				// Delete the reference to the effect instance
				if (EffectSubmixChain[i].EffectInstance)
				{
					delete EffectSubmixChain[i].EffectInstance;
					EffectSubmixChain[i].EffectInstance = nullptr;
				}
				EffectSubmixChain[i].PresetId = INDEX_NONE;
				return;
			}
		}

	}

	void FMixerSubmix::ClearSoundEffectSubmixes()
	{
		for (FSubmixEffectInfo& Info : EffectSubmixChain)
		{
			if (Info.EffectInstance)
			{
				Info.EffectInstance->ClearPreset();

				delete Info.EffectInstance;
				Info.EffectInstance = nullptr;
			}
		}

		EffectSubmixChain.Reset();
	}

	void FMixerSubmix::SetBackgroundMuted(bool bInMuted)
	{
		SubmixCommand([this, bInMuted]()
		{
			bIsBackgroundMuted = bInMuted;
		});
	}

	void FMixerSubmix::FormatChangeBuffer(const ESubmixChannelFormat InNewChannelType, AlignedFloatBuffer& InBuffer, AlignedFloatBuffer& OutNewBuffer)
	{
		// Retrieve ptr to the cached downmix channel map from the mixer device
		int32 NewChannelCount = MixerDevice->GetNumChannelsForSubmixFormat(InNewChannelType);
		Audio::AlignedFloatBuffer ChannelMap;
		MixerDevice->Get2DChannelMap(false, InNewChannelType, NumChannels, false, ChannelMap);
		float* ChannelMapPtr = ChannelMap.GetData();

		// Input and output frame count is going to be the same
		const int32 NumFrames = InBuffer.Num() / NumChannels;

		// Reset the passed in downmix scratch buffer
		OutNewBuffer.Reset();
		OutNewBuffer.AddZeroed(NumFrames * NewChannelCount);

		if (SubmixAmbisonicsDecoderID == INDEX_NONE)
		{
			float* OutNewBufferPtr = OutNewBuffer.GetData();

			// Loop through the down mix map and perform the downmix operation
			int32 InputSampleIndex = 0;
			int32 DownMixedBufferIndex = 0;
			for (; InputSampleIndex < InBuffer.Num();)
			{
				for (int32 DownMixChannel = 0; DownMixChannel < NewChannelCount; ++DownMixChannel)
				{
					for (int32 InChannel = 0; InChannel < NumChannels; ++InChannel)
					{
						const int32 ChannelMapIndex = NewChannelCount * InChannel + DownMixChannel;
						OutNewBufferPtr[DownMixedBufferIndex + DownMixChannel] += InBuffer[InputSampleIndex + InChannel] * ChannelMapPtr[ChannelMapIndex];
					}
				}

				InputSampleIndex += NumChannels;
				DownMixedBufferIndex += NewChannelCount;
			}
		}
		else
		{
			FAmbisonicsDecoderInputData InputData;
			InputData.AudioBuffer = &InBuffer;
			InputData.NumChannels = NumChannels;

			FAmbisonicsDecoderOutputData OutputData = { OutNewBuffer };

			if (CachedPositionalData.OutputNumChannels != NewChannelCount)
			{
				// re-cache output positions
				CachedPositionalData.OutputNumChannels = NewChannelCount;

				CachedPositionalData.OutputChannelPositions = AmbisonicsStatics::GetDefaultPositionMap(NewChannelCount);
			}

			//TODO: Update listener rotation in CachedPositionalData
			const TArray<FTransform>* ListenerTransforms = MixerDevice->GetListenerTransforms();

			if (ListenerTransforms != nullptr && ListenerTransforms->Num() >= 1)
			{
				CachedPositionalData.ListenerRotation = (*ListenerTransforms)[0].GetRotation();
			}

			// Todo: sum into OutputData rather than decoding directly to it.
			AmbisonicsMixer->DecodeFromAmbisonics(SubmixAmbisonicsDecoderID, InputData, CachedPositionalData, OutputData);
		}
	}

	void FMixerSubmix::MixBufferDownToMono(const AlignedFloatBuffer& InBuffer, int32 NumInputChannels, AlignedFloatBuffer& OutBuffer)
	{
		check(NumInputChannels > 0);

		int32 NumFrames = InBuffer.Num() / NumInputChannels;
		OutBuffer.Reset();
		OutBuffer.AddZeroed(NumFrames);

		const float* InData = InBuffer.GetData();
		float* OutData = OutBuffer.GetData();

		const float GainFactor = 1.0f / FMath::Sqrt((float) NumInputChannels);

		for (int32 FrameIndex = 0; FrameIndex < NumFrames; FrameIndex++)
		{
			for (int32 ChannelIndex = 0; ChannelIndex < NumInputChannels; ChannelIndex++)
			{
				const int32 InputIndex = FrameIndex * NumInputChannels + ChannelIndex;
				OutData[FrameIndex] += InData[InputIndex] * GainFactor;
			}
		}
	}

	void FMixerSubmix::SetUpAmbisonicsEncoder()
	{
		check(AmbisonicsMixer.IsValid());

		// If we haven't already set up the encoder, destroy the old stream.
		if (SubmixAmbisonicsEncoderID == INDEX_NONE)
		{
			TearDownAmbisonicsEncoder();
		}

		//Get a new unique stream ID
		SubmixAmbisonicsEncoderID = MixerDevice->GetNewUniqueAmbisonicsStreamID();
		AmbisonicsMixer->OnOpenEncodingStream(SubmixAmbisonicsEncoderID, AmbisonicsSettings);
	}

	void FMixerSubmix::SetUpAmbisonicsDecoder()
	{
		check(AmbisonicsMixer.IsValid());

		// if we have already set up the decoder, destroy the old stream.
		if (SubmixAmbisonicsDecoderID == INDEX_NONE)
		{
			TearDownAmbisonicsDecoder();
		}

		SubmixAmbisonicsDecoderID = MixerDevice->GetNewUniqueAmbisonicsStreamID();

		SetUpAmbisonicsPositionalData();
		AmbisonicsMixer->OnOpenDecodingStream(SubmixAmbisonicsDecoderID, AmbisonicsSettings, CachedPositionalData);
	}

	void FMixerSubmix::TearDownAmbisonicsEncoder()
	{
		if (SubmixAmbisonicsEncoderID != INDEX_NONE)
		{
			AmbisonicsMixer->OnCloseEncodingStream(SubmixAmbisonicsEncoderID);
			SubmixAmbisonicsEncoderID = INDEX_NONE;
		}
	}

	void FMixerSubmix::TearDownAmbisonicsDecoder()
	{
		if (SubmixAmbisonicsDecoderID != INDEX_NONE)
		{
			AmbisonicsMixer->OnCloseDecodingStream(SubmixAmbisonicsDecoderID);
			SubmixAmbisonicsDecoderID = INDEX_NONE;
		}
	}

	void FMixerSubmix::UpdateAmbisonicsEncoderForChildren()
	{
		bool bNeedsEncoder = false;

		//Here we scan all child submixes to see which submixes need to be reencoded.
		for (auto& Iter : ChildSubmixes)
		{
			FChildSubmixInfo& ChildSubmix = Iter.Value;

			TSharedPtr<Audio::FMixerSubmix, ESPMode::ThreadSafe> SubmixPtr = ChildSubmix.SubmixPtr.Pin();

			//Check to see if this child is an ambisonics submix.
			if (SubmixPtr.IsValid() && SubmixPtr->GetSubmixChannels() == ESubmixChannelFormat::Ambisonics)
			{
				UAmbisonicsSubmixSettingsBase* ChildAmbisonicsSettings = SubmixPtr->AmbisonicsSettings;

				//Check if this child submix needs to be reencoded.
				if (!ChildAmbisonicsSettings || AmbisonicsMixer->ShouldReencodeBetween(ChildAmbisonicsSettings, AmbisonicsSettings))
				{
					ChildSubmix.bNeedsAmbisonicsEncoding = false;
				}
				else
				{
					bNeedsEncoder = true;
				}
			}
			else
			{
				bNeedsEncoder = true;
			}
		}

		if (bNeedsEncoder)
		{
			SetUpAmbisonicsEncoder();
		}
		else
		{
			TearDownAmbisonicsEncoder();
		}
	}

	void FMixerSubmix::UpdateAmbisonicsDecoderForParent()
	{
		UAmbisonicsSubmixSettingsBase* ParentAmbisonicsSettings = nullptr;

		TSharedPtr<FMixerSubmix, ESPMode::ThreadSafe> ParentSubmixSharedPtr = ParentSubmix.Pin();
		if (ParentSubmixSharedPtr.IsValid())
		{
			if (ParentSubmixSharedPtr->GetSubmixChannels() == ESubmixChannelFormat::Ambisonics)
			{
				ParentAmbisonicsSettings = ParentSubmixSharedPtr->AmbisonicsSettings;
			}
		}

		// If we need to reencode between here and the parent submix, set up the submix decoder.
		if (!ParentAmbisonicsSettings || AmbisonicsMixer->ShouldReencodeBetween(AmbisonicsSettings, ParentAmbisonicsSettings))
		{
			SetUpAmbisonicsDecoder();
		}
		else
		{
			TearDownAmbisonicsDecoder();
		}
	}

	void FMixerSubmix::SetUpAmbisonicsPositionalData()
	{
		// If there is a parent and we are not passing it this submix's ambisonics audio, retrieve that submix's channel format.
		TSharedPtr<Audio::FMixerSubmix, ESPMode::ThreadSafe> ParentSubmixSharedPtr = ParentSubmix.Pin();
		if (ParentSubmixSharedPtr.IsValid())
		{
			const ESubmixChannelFormat ParentSubmixFormat = ParentSubmixSharedPtr->GetSubmixChannels();

			const int32 NumParentChannels = MixerDevice->GetNumChannelsForSubmixFormat(ParentSubmixFormat);
			CachedPositionalData.OutputNumChannels = NumParentChannels;
			CachedPositionalData.OutputChannelPositions = AmbisonicsStatics::GetDefaultPositionMap(NumParentChannels);
		}
		
		CachedPositionalData.ListenerRotation = FQuat::Identity;
	}

	void FMixerSubmix::EncodeAndMixInSource(AlignedFloatBuffer& InAudioData, FSubmixVoiceData& InVoiceInfo)
	{
		InVoiceInfo.CachedEncoderInputData.AudioBuffer = &InAudioData;

		FAmbisonicsEncoderOutputData OutputData = { InputAmbisonicsBuffer };
		
		// Encode voice to ambisonics:
		check(AmbisonicsMixer.IsValid());
		AmbisonicsMixer->EncodeToAmbisonics(InVoiceInfo.AmbisonicsEncoderId, InVoiceInfo.CachedEncoderInputData, OutputData, AmbisonicsSettings);
		
		//Sum output to ambisonics bed:
		float* DestinationBuffer = InputBuffer.GetData();
		float* SrcBuffer = InputAmbisonicsBuffer.GetData();
		for (int32 Index = 0; Index < InputBuffer.Num(); Index++)
		{
			DestinationBuffer[Index] += SrcBuffer[Index];
		}
	}

	void FMixerSubmix::EncodeAndMixInChildSubmix(FChildSubmixInfo& Child)
	{
		check(AmbisonicsMixer.IsValid());
		check(SubmixAmbisonicsEncoderID != INDEX_NONE);

		//TODO: Implement generic mixdowns to ambisonics. Set up input encoder channel
	}

	void FMixerSubmix::PumpCommandQueue()
	{
		TFunction<void()> Command;
		while (CommandQueue.Dequeue(Command))
		{
			Command();
		}
	}

	void FMixerSubmix::SubmixCommand(TFunction<void()> Command)
	{
		CommandQueue.Enqueue(MoveTemp(Command));
	}

	void FMixerSubmix::ProcessAudio(const ESubmixChannelFormat ParentChannelType, AlignedFloatBuffer& OutAudioBuffer)
	{
		AUDIO_MIXER_CHECK_AUDIO_PLAT_THREAD(MixerDevice);

		// Pump pending command queues
		PumpCommandQueue();

		// Device format may change channels if device is hot swapped
		if (ChannelFormat == ESubmixChannelFormat::Device)
		{
			NumChannels = MixerDevice->GetNumChannelsForSubmixFormat(ChannelFormat);
			const int32 NumOutputFrames = MixerDevice->GetNumOutputFrames();
			NumSamples = NumChannels * NumOutputFrames;
		}

		InputBuffer.Reset(NumSamples);
		InputBuffer.AddZeroed(NumSamples);

		float* BufferPtr = InputBuffer.GetData();

		// Mix all submix audio into this submix's input scratch buffer
		{
			CSV_SCOPED_TIMING_STAT(Audio, SubmixChildren);

			// First loop this submix's child submixes mixing in their output into this submix's dry/wet buffers.
			for (auto& ChildSubmixEntry : ChildSubmixes)
			{
				TSharedPtr<Audio::FMixerSubmix, ESPMode::ThreadSafe> ChildSubmix = ChildSubmixEntry.Value.SubmixPtr.Pin();
				if (ChildSubmix.IsValid())
				{
					ScratchBuffer.Reset(NumSamples);
					ScratchBuffer.AddZeroed(NumSamples);

					ChildSubmix->ProcessAudio(ChannelFormat, ScratchBuffer);

					float* ScratchBufferPtr = ScratchBuffer.GetData();

					if (ChildSubmixEntry.Value.bNeedsAmbisonicsEncoding)
					{
						// Encode into ambisonics. TODO: Implement.
						EncodeAndMixInChildSubmix(ChildSubmixEntry.Value);
					}
					else
					{
						Audio::MixInBufferFast(ScratchBufferPtr, BufferPtr, NumSamples);
					}
				}
			}
		}

		{
			CSV_SCOPED_TIMING_STAT(Audio, SubmixSource);

			// Loop through this submix's sound sources
			for (const auto MixerSourceVoiceIter : MixerSourceVoices)
			{
				const FMixerSourceVoice* MixerSourceVoice = MixerSourceVoiceIter.Key;
				const float SendLevel = MixerSourceVoiceIter.Value.SendLevel;

				
				MixerSourceVoice->MixOutputBuffers(ChannelFormat, SendLevel, InputBuffer);
			}
		}

		if (EffectSubmixChain.Num() > 0)
		{
			CSV_SCOPED_TIMING_STAT(Audio, SubmixEffectProcessing);

			// Setup the input data buffer
			FSoundEffectSubmixInputData InputData;
			InputData.AudioClock = MixerDevice->GetAudioTime();

			// Compute the number of frames of audio. This will be independent of if we downmix our wet buffer.
			InputData.NumFrames = NumSamples / NumChannels;
			InputData.NumChannels = NumChannels;
			InputData.NumDeviceChannels = MixerDevice->GetNumDeviceChannels();
			InputData.ListenerTransforms = MixerDevice->GetListenerTransforms();
			InputData.AudioClock = MixerDevice->GetAudioClock();

			FSoundEffectSubmixOutputData OutputData;
			OutputData.AudioBuffer = &ScratchBuffer;
			OutputData.NumChannels = NumChannels;

			for (FSubmixEffectInfo& SubmixEffectInfo : EffectSubmixChain)
			{
				FSoundEffectSubmix* SubmixEffect = SubmixEffectInfo.EffectInstance;

				// SubmixEffectInfo.EffectInstance will be null if FMixerSubmix::RemoveSoundEffectSubmix was called earlier.
				if (!SubmixEffect)
				{
					continue;
				}

				// Reset the output scratch buffer
				ScratchBuffer.Reset(NumSamples);
				ScratchBuffer.AddZeroed(NumSamples);

				// Check to see if we need to down-mix our audio before sending to the submix effect
				const uint32 ChannelCountOverride = SubmixEffect->GetDesiredInputChannelCountOverride();

				// Only support downmixing to stereo. TODO: change GetDesiredInputChannelCountOverride() API to be "DownmixToStereo"
				if (ChannelCountOverride < (uint32)NumChannels && ChannelCountOverride == 2)
				{
					// Perform the down-mix operation with the down-mixed scratch buffer
					FormatChangeBuffer(ESubmixChannelFormat::Stereo, InputBuffer, DownmixedBuffer);

					InputData.NumChannels = ChannelCountOverride;
					InputData.AudioBuffer = &DownmixedBuffer;
					SubmixEffect->ProcessAudio(InputData, OutputData);
				}
				else
				{
					// If we're not down-mixing, then just pass in the current wet buffer and our channel count is the same as the output channel count
					InputData.NumChannels = NumChannels;
					InputData.AudioBuffer = &InputBuffer;
					SubmixEffect->ProcessAudio(InputData, OutputData);
				}

				// Mix in the dry signal directly
				const float DryLevel = SubmixEffect->GetDryLevel();
				if (DryLevel > 0.0f)
				{
					Audio::MixInBufferFast(InputBuffer, ScratchBuffer, DryLevel);
				}

				FMemory::Memcpy((void*)BufferPtr, (void*)ScratchBuffer.GetData(), sizeof(float)*NumSamples);
			}
		}

		// If we're muted, memzero the buffer. Note we are still doing all the work to maintain buffer state between mutings.
		if (bIsBackgroundMuted)
		{
			FMemory::Memzero((void*)BufferPtr, sizeof(float)*NumSamples);
		}

		// If we are recording, Add out buffer to the RecordingData buffer:
		{
			FScopeLock ScopedLock(&RecordingCriticalSection);
			if (bIsRecording)
			{
				// TODO: Consider a scope lock between here and OnStopRecordingOutput.
				RecordingData.Append((float*)BufferPtr, NumSamples);
			}
		}

		// If spectrum analysis is enabled for this submix, downmix the resulting audio
		// and push it to the spectrum analyzer.
		if (SpectrumAnalyzer.IsValid())
		{
			MixBufferDownToMono(InputBuffer, NumChannels, MonoMixBuffer);
			SpectrumAnalyzer->PushAudio(MonoMixBuffer.GetData(), MonoMixBuffer.Num());
			SpectrumAnalyzer->PerformAnalysisIfPossible(true, true);
		}

		// If the channel types match, just do a copy
		if (ChannelFormat != ParentChannelType || SubmixAmbisonicsDecoderID != INDEX_NONE)
		{
			FormatChangeBuffer(ParentChannelType, InputBuffer, OutAudioBuffer);
		}
		else
		{
			FMemory::Memcpy((void*)OutAudioBuffer.GetData(), (void*)InputBuffer.GetData(), sizeof(float)*NumSamples);
		}

		// Perform any envelope following if we're told to do so
		if (bIsEnvelopeFollowing)
		{
			const int32 OutBufferSamples = OutAudioBuffer.Num();
			const float* OutAudioBufferPtr = OutAudioBuffer.GetData();

			float TempEnvelopeValues[AUDIO_MIXER_MAX_OUTPUT_CHANNELS];
			FMemory::Memset(TempEnvelopeValues, sizeof(float)*AUDIO_MIXER_MAX_OUTPUT_CHANNELS);

			// Perform envelope following per channel
			for (int32 ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
			{
				// Get the envelope follower for the channel
				FEnvelopeFollower& EnvFollower = EnvelopeFollowers[ChannelIndex];

				// Track the last sample
				for (int32 SampleIndex = ChannelIndex; SampleIndex < OutBufferSamples; SampleIndex += NumChannels)
				{
					const float SampleValue = OutAudioBufferPtr[SampleIndex];
					EnvFollower.ProcessAudio(SampleValue);
				}

				// Store the last value
				TempEnvelopeValues[ChannelIndex] = EnvFollower.GetCurrentValue();
			}

			FScopeLock EnvelopeScopeLock(&EnvelopeCriticalSection);

			EnvelopeNumChannels = NumChannels;
			FMemory::Memcpy(EnvelopeValues, TempEnvelopeValues, sizeof(float)*AUDIO_MIXER_MAX_OUTPUT_CHANNELS);
		}

		// Don't necessarily need to do this if the user isn't using this feature
		if (bApplyOutputVolumeScale)
		{
			const float TargetVolumeProduct = TargetOutputVolume * InitializedOutputVolume;
			const float OutputVolumeProduct = OutputVolume * InitializedOutputVolume;

			// If we've already set the volume, only need to multiply by constant
			if (FMath::IsNearlyEqual(TargetVolumeProduct, OutputVolumeProduct))
			{
				Audio::MultiplyBufferByConstantInPlace(OutAudioBuffer, OutputVolumeProduct);
			}
			else
			{
				// To avoid popping, we do a fade on the buffer to the target volume
				Audio::FadeBufferFast(OutAudioBuffer, OutputVolumeProduct, TargetVolumeProduct);
				OutputVolume = TargetOutputVolume;

				// No longer need to multiply the output buffer if we're now at 1.0
				if (FMath::IsNearlyEqual(OutputVolume * InitializedOutputVolume, 1.0f))
				{
					bApplyOutputVolumeScale = false;
				}
			}
		}

		// Now loop through any buffer listeners and feed the listeners the result of this audio callback
		{
			double AudioClock = MixerDevice->GetAudioTime();
			float SampleRate = MixerDevice->GetSampleRate();
			FScopeLock Lock(&BufferListenerCriticalSection);
			for (ISubmixBufferListener* BufferListener : BufferListeners)
			{
				check(BufferListener);
				BufferListener->OnNewSubmixBuffer(OwningSubmixObject, OutAudioBuffer.GetData(), OutAudioBuffer.Num(), NumChannels, SampleRate, AudioClock);
			}
		}
	}

	int32 FMixerSubmix::GetSampleRate() const
	{
		return MixerDevice->GetDeviceSampleRate();
	}

	int32 FMixerSubmix::GetNumOutputChannels() const
	{
		return MixerDevice->GetNumDeviceChannels();
	}

	int32 FMixerSubmix::GetNumChainEffects() const
	{
		return EffectSubmixChain.Num();
	}

	FSoundEffectSubmix* FMixerSubmix::GetSubmixEffect(const int32 InIndex)
	{
		if (InIndex < EffectSubmixChain.Num())
		{
			return EffectSubmixChain[InIndex].EffectInstance;
		}
		return nullptr;
	}

	void FMixerSubmix::OnAmbisonicsSettingsChanged(UAmbisonicsSubmixSettingsBase* InAmbisonicsSettings)
	{
		check(InAmbisonicsSettings != nullptr);
		if (!AmbisonicsMixer.IsValid())
		{
			AmbisonicsMixer = MixerDevice->GetAmbisonicsMixer();
			if (!AmbisonicsMixer.IsValid())
			{
				return;
			}
		}

		AmbisonicsSettings = InAmbisonicsSettings;
		NumChannels = AmbisonicsMixer->GetNumChannelsForAmbisonicsFormat(AmbisonicsSettings);
		NumSamples = NumChannels * MixerDevice->GetNumOutputFrames();

		UpdateAmbisonicsEncoderForChildren();
		UpdateAmbisonicsDecoderForParent();
	}

	void FMixerSubmix::OnStartRecordingOutput(float ExpectedDuration)
	{
		RecordingData.Reset();
		RecordingData.Reserve(ExpectedDuration * GetSampleRate());
		bIsRecording = true;
	}

	AlignedFloatBuffer& FMixerSubmix::OnStopRecordingOutput(float& OutNumChannels, float& OutSampleRate)
	{
		FScopeLock ScopedLock(&RecordingCriticalSection);
		bIsRecording = false;
		OutNumChannels = NumChannels;
		OutSampleRate = GetSampleRate();
		return RecordingData;
	}

	void FMixerSubmix::PauseRecordingOutput()
	{
		if (!RecordingData.Num())
		{
			UE_LOG(LogAudioMixer, Warning, TEXT("Cannot pause recording output as no recording is in progress."));
			return;
		}
		
		bIsRecording = false;
	}

	void FMixerSubmix::ResumeRecordingOutput()
	{
		if (!RecordingData.Num())
		{
			UE_LOG(LogAudioMixer, Warning, TEXT("Cannot resume recording output as no recording is in progress."));
			return;
		}
		bIsRecording = true;
	}

	void FMixerSubmix::RegisterBufferListener(ISubmixBufferListener* BufferListener)
	{
		FScopeLock Lock(&BufferListenerCriticalSection);
		check(BufferListener);
		BufferListeners.AddUnique(BufferListener);
	}

	void FMixerSubmix::UnregisterBufferListener(ISubmixBufferListener* BufferListener)
	{
		FScopeLock Lock(&BufferListenerCriticalSection);
		check(BufferListener);
		BufferListeners.Remove(BufferListener);
	}
	void FMixerSubmix::StartEnvelopeFollowing(int32 AttackTime, int32 ReleaseTime)
	{
		if (!bIsEnvelopeFollowing)
		{
			// Zero out any previous envelope values which may have been in the array before starting up
			for (int32 ChannelIndex = 0; ChannelIndex < AUDIO_MIXER_MAX_OUTPUT_CHANNELS; ++ChannelIndex)
			{
				EnvelopeValues[ChannelIndex] = 0.0f;
				EnvelopeFollowers[ChannelIndex].Init(GetSampleRate(), AttackTime, ReleaseTime);
			}

			bIsEnvelopeFollowing = true;
		}
	}

	void FMixerSubmix::StopEnvelopeFollowing()
	{
		bIsEnvelopeFollowing = false;
	}

	void FMixerSubmix::AddEnvelopeFollowerDelegate(const FOnSubmixEnvelopeBP& OnSubmixEnvelopeBP)
	{
		OnSubmixEnvelope.AddUnique(OnSubmixEnvelopeBP);
	}

	void FMixerSubmix::StartSpectrumAnalysis(const FSpectrumAnalyzerSettings& InSettings)
	{
		SpectrumAnalyzer.Reset(new FSpectrumAnalyzer(InSettings, MixerDevice->GetSampleRate()));
	}

	void FMixerSubmix::StopSpectrumAnalysis()
	{
		SpectrumAnalyzer.Reset();
	}

	void FMixerSubmix::GetMagnitudeForFrequencies(const TArray<float>& InFrequencies, TArray<float>& OutMagnitudes)
	{
		if (SpectrumAnalyzer.IsValid())
		{
			OutMagnitudes.Reset();
			OutMagnitudes.AddUninitialized(InFrequencies.Num());

			SpectrumAnalyzer->LockOutputBuffer();
			for (int32 Index = 0; Index < InFrequencies.Num(); Index++)
			{
				OutMagnitudes[Index] = SpectrumAnalyzer->GetMagnitudeForFrequency(InFrequencies[Index]);
			}
			SpectrumAnalyzer->UnlockOutputBuffer();
		}
		else
		{
			UE_LOG(LogAudioMixer, Warning, TEXT("Call StartSpectrumAnalysis before calling GetMagnitudeForFrequencies."));
		}
	}

	void FMixerSubmix::GetPhaseForFrequencies(const TArray<float>& InFrequencies, TArray<float>& OutPhases)
	{
		if (SpectrumAnalyzer.IsValid())
		{
			OutPhases.Reset();
			OutPhases.AddUninitialized(InFrequencies.Num());

			SpectrumAnalyzer->LockOutputBuffer();
			for (int32 Index = 0; Index < InFrequencies.Num(); Index++)
			{
				OutPhases[Index] = SpectrumAnalyzer->GetPhaseForFrequency(InFrequencies[Index]);
			}
			SpectrumAnalyzer->UnlockOutputBuffer();
		}
		else
		{
			UE_LOG(LogAudioMixer, Warning, TEXT("Call StartSpectrumAnalysis before calling GetMagnitudeForFrequencies."));
		}
	}

	void FMixerSubmix::SetDynamicOutputVolume(float InVolume)
	{
		InVolume = FMath::Clamp(InVolume, 0.0f, 1.0f);
		if (!FMath::IsNearlyEqual(InVolume, TargetOutputVolume))
		{
			TargetOutputVolume = InVolume;
			bApplyOutputVolumeScale = true;
		}
	}

	void FMixerSubmix::SetOutputVolume(float InVolume)
	{
		InVolume = FMath::Clamp(InVolume, 0.0f, 1.0f);
		if (!FMath::IsNearlyEqual(InitializedOutputVolume, InVolume))
		{
			InitializedOutputVolume = InVolume;
			bApplyOutputVolumeScale = true;
		}
	}

	void FMixerSubmix::BroadcastEnvelope()
	{
		if (bIsEnvelopeFollowing)
		{
			// Get the envelope data
			TArray<float> EnvelopeData;

			{
				// Make the copy of the envelope values using a critical section
				FScopeLock EnvelopeScopeLock(&EnvelopeCriticalSection);

				EnvelopeData.AddUninitialized(EnvelopeNumChannels);
				FMemory::Memcpy(EnvelopeData.GetData(), EnvelopeValues, sizeof(float)*EnvelopeNumChannels);
			}

			// Broadcast to any bound delegates
			if (OnSubmixEnvelope.IsBound())
			{
				OnSubmixEnvelope.Broadcast(EnvelopeData);
			}

		}
	}

}
