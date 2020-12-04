// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioMixerSource.h"
#include "AudioMixerSourceBuffer.h"
#include "ActiveSound.h"
#include "AudioMixerSourceBuffer.h"
#include "AudioMixerDevice.h"
#include "AudioMixerSourceVoice.h"
#include "ContentStreaming.h"
#include "IAudioExtensionPlugin.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundModulationDestination.h"

// Link to "Audio" profiling category
CSV_DECLARE_CATEGORY_MODULE_EXTERN(AUDIOMIXERCORE_API, Audio);

static int32 UseListenerOverrideForSpreadCVar = 0;
FAutoConsoleVariableRef CVarUseListenerOverrideForSpread(
	TEXT("au.UseListenerOverrideForSpread"),
	UseListenerOverrideForSpreadCVar,
	TEXT("Zero attenuation override distance stereo panning\n")
	TEXT("0: Use actual distance, 1: use listener override"),
	ECVF_Default);


namespace Audio
{
	namespace ModulationUtils
	{
		static const FSoundModulationDestinationSettings DefaultDestination;

		const FSoundModulationDestinationSettings& GetRoutedVolumeModulation(const FWaveInstance& InWaveInstance, const USoundWave& InWaveData, FActiveSound* InActiveSound)
		{
			const FSoundModulationDefaultRoutingSettings& RoutingSettings = InActiveSound->ModulationRouting;
			switch (RoutingSettings.VolumeRouting)
			{
				case EModulationRouting::Inherit:
				{
					switch (InWaveData.ModulationSettings.VolumeRouting)
					{
						case EModulationRouting::Inherit:
						{
							USoundClass* SoundClass = InActiveSound->GetSoundClass();
							if (InWaveInstance.SoundClass)
							{
								SoundClass = InWaveInstance.SoundClass;
							}
							if (SoundClass)
							{
								return SoundClass->Properties.ModulationSettings.VolumeModulationDestination;
							}
						}
						break;

						case EModulationRouting::Override:
						{
							return InWaveData.ModulationSettings.VolumeModulationDestination;
						}
						break;

						case EModulationRouting::Disable:
						default:
						break;
					}
				}
				break;

				case EModulationRouting::Override:
				{
					return RoutingSettings.VolumeModulationDestination;
				}
				break;

				case EModulationRouting::Disable:
				default:
				break;
			}

			return DefaultDestination;
		}

		const FSoundModulationDestinationSettings& GetRoutedPitchModulation(const FWaveInstance& InWaveInstance, const USoundWave& InWaveData, FActiveSound* InActiveSound)
		{
			const FSoundModulationDefaultRoutingSettings& RoutingSettings = InActiveSound->ModulationRouting;
			switch (RoutingSettings.PitchRouting)
			{
				case EModulationRouting::Inherit:
				{
					switch (InWaveData.ModulationSettings.PitchRouting)
					{
						case EModulationRouting::Inherit:
						{
							USoundClass* SoundClass = InActiveSound->GetSoundClass();
							if (InWaveInstance.SoundClass)
							{
								SoundClass = InWaveInstance.SoundClass;
							}
							if (SoundClass)
							{
								return SoundClass->Properties.ModulationSettings.PitchModulationDestination;
							}
						}
						break;

						case EModulationRouting::Override:
						{
							return InWaveData.ModulationSettings.PitchModulationDestination;
						}
						break;

						case EModulationRouting::Disable:
						default:
						break;
					}
				}
				break;

				case EModulationRouting::Override:
				{
					return RoutingSettings.PitchModulationDestination;
				}
				break;
				case EModulationRouting::Disable:
				default:
				break;
			}

			return DefaultDestination;
		}

		const FSoundModulationDestinationSettings& GetRoutedHighpassModulation(const FWaveInstance& InWaveInstance, const USoundWave& InWaveData, FActiveSound* InActiveSound)
		{
			const FSoundModulationDefaultRoutingSettings& RoutingSettings = InActiveSound->ModulationRouting;
			switch (RoutingSettings.HighpassRouting)
			{
				case EModulationRouting::Inherit:
				{
					switch (InWaveData.ModulationSettings.HighpassRouting)
					{
						case EModulationRouting::Inherit:
						{
							USoundClass* SoundClass = InActiveSound->GetSoundClass();
							if (InWaveInstance.SoundClass)
							{
								SoundClass = InWaveInstance.SoundClass;
							}
							if (SoundClass)
							{
								return SoundClass->Properties.ModulationSettings.HighpassModulationDestination;
							}
						}
						break;

						case EModulationRouting::Override:
						{
							return InWaveData.ModulationSettings.HighpassModulationDestination;
						}
						break;

						case EModulationRouting::Disable:
						default:
						break;
					}
				}
				break;

				case EModulationRouting::Override:
				{
					return RoutingSettings.HighpassModulationDestination;
				}
				break;

				case EModulationRouting::Disable:
				default:
				break;
			}

			return DefaultDestination;
		}

		const FSoundModulationDestinationSettings& GetRoutedLowpassModulation(const FWaveInstance& InWaveInstance, const USoundWave& InWaveData, FActiveSound* InActiveSound)
		{
			const FSoundModulationDefaultRoutingSettings& RoutingSettings = InActiveSound->ModulationRouting;
			switch (RoutingSettings.LowpassRouting)
			{
				case EModulationRouting::Inherit:
				{
					switch (InWaveData.ModulationSettings.LowpassRouting)
					{
						case EModulationRouting::Inherit:
						{
							USoundClass* SoundClass = InActiveSound->GetSoundClass();
							if (InWaveInstance.SoundClass)
							{
								SoundClass = InWaveInstance.SoundClass;
							}
							if (SoundClass)
							{
								return SoundClass->Properties.ModulationSettings.LowpassModulationDestination;
							}
						}
						break;

						case EModulationRouting::Override:
						{
							return InWaveData.ModulationSettings.LowpassModulationDestination;
						}
						break;

						case EModulationRouting::Disable:
						default:
						break;
					}
				}
				break;

				case EModulationRouting::Override:
				{
					return RoutingSettings.LowpassModulationDestination;
				}
				break;

				case EModulationRouting::Disable:
				default:
				break;
			}

			return DefaultDestination;
		}

		FSoundModulationDefaultSettings GetRoutedModulation(const FWaveInstance& InWaveInstance, const USoundWave& InWaveData, FActiveSound* InActiveSound)
		{
			FSoundModulationDefaultSettings Settings;
			if (InActiveSound)
			{
				Settings.VolumeModulationDestination = GetRoutedVolumeModulation(InWaveInstance, InWaveData, InActiveSound);
				Settings.PitchModulationDestination = GetRoutedPitchModulation(InWaveInstance, InWaveData, InActiveSound);
				Settings.HighpassModulationDestination = GetRoutedHighpassModulation(InWaveInstance, InWaveData, InActiveSound);
				Settings.LowpassModulationDestination = GetRoutedLowpassModulation(InWaveInstance, InWaveData, InActiveSound);
			}

			return Settings;
		}
	} // namespace ModulationUtils

	FMixerSource::FMixerSource(FAudioDevice* InAudioDevice)
		: FSoundSource(InAudioDevice)
		, MixerDevice(static_cast<FMixerDevice*>(InAudioDevice))
		, MixerBuffer(nullptr)
		, MixerSourceVoice(nullptr)
		, PreviousAzimuth(-1.0f)
		, PreviousPlaybackPercent(0.0f)
		, InitializationState(EMixerSourceInitializationState::NotInitialized)
		, bPlayedCachedBuffer(false)
		, bPlaying(false)
		, bLoopCallback(false)
		, bIsDone(false)
		, bIsEffectTailsDone(false)
		, bIsPlayingEffectTails(false)
		, bEditorWarnedChangedSpatialization(false)
		, bUsingHRTFSpatialization(false)
		, bIs3D(false)
		, bDebugMode(false)
		, bIsVorbis(false)
		, bIsStoppingVoicesEnabled(InAudioDevice->IsStoppingVoicesEnabled())
		, bSendingAudioToBuses(false)
		, bPrevAllowedSpatializationSetting(false)
	{
	}

	FMixerSource::~FMixerSource()
	{
		FreeResources();
	}

	bool FMixerSource::Init(FWaveInstance* InWaveInstance)
	{
		AUDIO_MIXER_CHECK(MixerBuffer);
		AUDIO_MIXER_CHECK(MixerBuffer->IsRealTimeSourceReady());

		// We've already been passed the wave instance in PrepareForInitialization, make sure we have the same one
		AUDIO_MIXER_CHECK(WaveInstance && WaveInstance == InWaveInstance);

		LLM_SCOPE(ELLMTag::AudioMixer);

		FSoundSource::InitCommon();

		check(WaveInstance);

		USoundWave* WaveData = WaveInstance->WaveData;
		check(WaveData);

		if (WaveData->NumChannels == 0)
		{
			UE_LOG(LogAudioMixer, Warning, TEXT("Soundwave %s has invalid compressed data."), *(WaveData->GetName()));
			FreeResources();
			return false;
		}

		// Get the number of frames before creating the buffer
		int32 NumFrames = INDEX_NONE;
		if (WaveData->DecompressionType != DTYPE_Procedural)
		{
			check(!WaveData->RawPCMData || WaveData->RawPCMDataSize);
			const int32 NumBytes = WaveData->RawPCMDataSize;
			if (WaveInstance->WaveData->NumChannels > 0)
			{
				NumFrames = NumBytes / (WaveData->NumChannels * sizeof(int16));
			}
		}

		// Unfortunately, we need to know if this is a vorbis source since channel maps are different for 5.1 vorbis files
		bIsVorbis = WaveData->bDecompressedFromOgg;

		bIsStoppingVoicesEnabled = AudioDevice->IsStoppingVoicesEnabled();

		bIsStopping = false;
		bIsEffectTailsDone = true;
		bIsDone = false;

		FSoundBuffer* SoundBuffer = static_cast<FSoundBuffer*>(MixerBuffer);
		if (SoundBuffer->NumChannels > 0)
		{
			CSV_SCOPED_TIMING_STAT(Audio, InitSources);

			AUDIO_MIXER_CHECK(MixerDevice);
			MixerSourceVoice = MixerDevice->GetMixerSourceVoice();
			if (!MixerSourceVoice)
			{
				FreeResources();
				UE_LOG(LogAudioMixer, Warning, TEXT("Failed to get a mixer source voice for sound %s."), *InWaveInstance->GetName());
				return false;
			}

			// Initialize the source voice with the necessary format information
			FMixerSourceVoiceInitParams InitParams;
			InitParams.SourceListener = this;
			InitParams.NumInputChannels = WaveData->NumChannels;
			InitParams.NumInputFrames = NumFrames;
			InitParams.SourceVoice = MixerSourceVoice;
			InitParams.bUseHRTFSpatialization = UseObjectBasedSpatialization();
			InitParams.bIsExternalSend = MixerDevice->bSpatializationIsExternalSend;
			InitParams.bIsSoundfield = WaveInstance->bIsAmbisonics && (WaveData->NumChannels == 4);

			FActiveSound* ActiveSound = WaveInstance->ActiveSound;
			InitParams.ModulationSettings = ModulationUtils::GetRoutedModulation(*WaveInstance, *WaveData, ActiveSound);

			// Copy quantization request data
			if (WaveInstance->QuantizedRequestData)
			{
				InitParams.QuantizedRequestData = *WaveInstance->QuantizedRequestData;
			}

			if (WaveInstance->bIsAmbisonics && (WaveData->NumChannels != 4))
			{
				UE_LOG(LogAudioMixer, Warning, TEXT("Sound wave %s was flagged as being ambisonics but had a channel count of %d. Currently the audio engine only supports FOA sources that have four channels."), *InWaveInstance->GetName(), WaveData->NumChannels);
			}

			InitParams.AudioComponentUserID = WaveInstance->ActiveSound->GetAudioComponentUserID();

			InitParams.AudioComponentID = WaveInstance->ActiveSound->GetAudioComponentID();

			InitParams.EnvelopeFollowerAttackTime = WaveInstance->EnvelopeFollowerAttackTime;
			InitParams.EnvelopeFollowerReleaseTime = WaveInstance->EnvelopeFollowerReleaseTime;

			InitParams.SourceEffectChainId = 0;

			// Source manager needs to know if this is a vorbis source for rebuilding speaker maps
			InitParams.bIsVorbis = bIsVorbis;

			if (InitParams.NumInputChannels <= 2)
			{
				if (WaveInstance->SourceEffectChain)
				{
					InitParams.SourceEffectChainId = WaveInstance->SourceEffectChain->GetUniqueID();

					for (int32 i = 0; i < WaveInstance->SourceEffectChain->Chain.Num(); ++i)
					{
						InitParams.SourceEffectChain.Add(WaveInstance->SourceEffectChain->Chain[i]);
						InitParams.bPlayEffectChainTails = WaveInstance->SourceEffectChain->bPlayEffectChainTails;
					}
				}

				// Only need to care about effect chain tails finishing if we're told to play them
				if (InitParams.bPlayEffectChainTails)
				{
					bIsEffectTailsDone = false;
				}

				// Setup the bus Id if this source is a bus
				if (WaveData->bIsSourceBus)
				{
					// We need to check if the source bus has an audio bus specified
					USoundSourceBus* SoundSourceBus = CastChecked<USoundSourceBus>(WaveData);

					// If it does, we will use that audio bus as the source of the audio data for the source bus
					if (SoundSourceBus->AudioBus)
					{
						InitParams.AudioBusId = SoundSourceBus->AudioBus->GetUniqueID();
					}
					else
					{
						InitParams.AudioBusId = WaveData->GetUniqueID();
					}

					if (!WaveData->IsLooping())
					{
						InitParams.SourceBusDuration = WaveData->GetDuration();
					}
				}
			}

			// Toggle muting the source if sending only to output bus.
			// This can get set even if the source doesn't have bus sends since bus sends can be dynamically enabled.
			InitParams.bOutputToBusOnly = WaveInstance->bOutputToBusOnly;
			DynamicBusSendInfos.Reset();

			SetupBusData(InitParams.AudioBusSends);

			// Don't set up any submixing if we're set to output to bus only
			if (!InitParams.bOutputToBusOnly)
			{
				// If we're spatializing using HRTF and its an external send, don't need to setup a default/base submix send to master or EQ submix
				// We'll only be using non-default submix sends (e.g. reverb).
				if (!(InitParams.bUseHRTFSpatialization && MixerDevice->bSpatializationIsExternalSend))
				{
					FMixerSubmixWeakPtr SubmixPtr = WaveInstance->SoundSubmix
						? MixerDevice->GetSubmixInstance(WaveInstance->SoundSubmix)
						: MixerDevice->GetMasterSubmix();

					FMixerSourceSubmixSend SubmixSend;
					SubmixSend.Submix = SubmixPtr;
					SubmixSend.SubmixSendStage = EMixerSourceSubmixSendStage::PostDistanceAttenuation;
					SubmixSend.SendLevel = 1.0f;
					SubmixSend.bIsMainSend = true;
					SubmixSend.SoundfieldFactory = MixerDevice->GetFactoryForSubmixInstance(SubmixSend.Submix);
					InitParams.SubmixSends.Add(SubmixSend);
				}

				// Add submix sends for this source
				for (FSoundSubmixSendInfo& SendInfo : WaveInstance->SoundSubmixSends)
				{
					if (SendInfo.SoundSubmix != nullptr)
					{
						FMixerSourceSubmixSend SubmixSend;
						SubmixSend.Submix = MixerDevice->GetSubmixInstance(SendInfo.SoundSubmix);

						SubmixSend.SubmixSendStage = EMixerSourceSubmixSendStage::PostDistanceAttenuation;
						if (SendInfo.SendStage == ESubmixSendStage::PreDistanceAttenuation)
						{
							SubmixSend.SubmixSendStage = EMixerSourceSubmixSendStage::PreDistanceAttenuation;
						}
						SubmixSend.SendLevel = SendInfo.SendLevel;
						SubmixSend.bIsMainSend = false;
						SubmixSend.SoundfieldFactory = MixerDevice->GetFactoryForSubmixInstance(SubmixSend.Submix);
						InitParams.SubmixSends.Add(SubmixSend);
					}
				}
			}

			// Loop through all submix sends to figure out what speaker maps this source is using
			for (FMixerSourceSubmixSend& Send : InitParams.SubmixSends)
			{
				FMixerSubmixPtr SubmixPtr = Send.Submix.Pin();
				if (SubmixPtr.IsValid())
				{
					ChannelMap.Reset();
				}
			}

			// Check to see if this sound has been flagged to be in debug mode
#if AUDIO_MIXER_ENABLE_DEBUG_MODE
			InitParams.DebugName = WaveInstance->GetName();

			bool bIsDebug = false;
			FString WaveInstanceName = WaveInstance->GetName(); //-V595
			FString TestName = GEngine->GetAudioDeviceManager()->GetDebugger().GetAudioMixerDebugSoundName();
			if (WaveInstanceName.Contains(TestName))
			{
				bDebugMode = true;
				InitParams.bIsDebugMode = bDebugMode;
			}
#endif

			// Whether or not we're 3D
			bIs3D = !UseObjectBasedSpatialization() && WaveInstance->GetUseSpatialization() && SoundBuffer->NumChannels < 3;

			// Pass on the fact that we're 3D to the init params
			InitParams.bIs3D = bIs3D;

			// Grab the source's reverb plugin settings
			InitParams.SpatializationPluginSettings = UseSpatializationPlugin() ? WaveInstance->SpatializationPluginSettings : nullptr;

			// Grab the source's occlusion plugin settings
			InitParams.OcclusionPluginSettings = UseOcclusionPlugin() ? WaveInstance->OcclusionPluginSettings : nullptr;

			// Grab the source's reverb plugin settings
			InitParams.ReverbPluginSettings = UseReverbPlugin() ? WaveInstance->ReverbPluginSettings : nullptr;

			// We support reverb
			SetReverbApplied(true);

			// Update the buffer sample rate to the wave instance sample rate in case it was serialized incorrectly
			MixerBuffer->InitSampleRate(WaveData->GetSampleRateForCurrentPlatform());

			// Retrieve the raw pcm buffer data and the precached buffers before initializing so we can avoid having USoundWave ptrs in audio renderer thread
			EBufferType::Type BufferType = MixerBuffer->GetType();
			if (BufferType == EBufferType::PCM || BufferType == EBufferType::PCMPreview)
			{
				FRawPCMDataBuffer RawPCMDataBuffer;
				MixerBuffer->GetPCMData(&RawPCMDataBuffer.Data, &RawPCMDataBuffer.DataSize);
				MixerSourceBuffer->SetPCMData(RawPCMDataBuffer);
			}
#if PLATFORM_NUM_AUDIODECOMPRESSION_PRECACHE_BUFFERS > 0
			else if (BufferType == EBufferType::PCMRealTime || BufferType == EBufferType::Streaming)
			{
				if (WaveData->CachedRealtimeFirstBuffer)
				{
					const uint32 NumPrecacheSamples = (uint32)(WaveData->NumPrecacheFrames * WaveData->NumChannels);
					const uint32 BufferSize = NumPrecacheSamples * sizeof(int16) * PLATFORM_NUM_AUDIODECOMPRESSION_PRECACHE_BUFFERS;

					TArray<uint8> PrecacheBufferCopy;
					PrecacheBufferCopy.AddUninitialized(BufferSize);

					FMemory::Memcpy(PrecacheBufferCopy.GetData(), WaveData->CachedRealtimeFirstBuffer, BufferSize);

					MixerSourceBuffer->SetCachedRealtimeFirstBuffers(MoveTemp(PrecacheBufferCopy));
				}
			}
#endif

			// Pass the decompression state off to the mixer source buffer if it hasn't already done so
			ICompressedAudioInfo* Decoder = MixerBuffer->GetDecompressionState(true);
			MixerSourceBuffer->SetDecoder(Decoder);

			// Hand off the mixer source buffer decoder
			InitParams.MixerSourceBuffer = MixerSourceBuffer;
			MixerSourceBuffer = nullptr;

			if (MixerSourceVoice->Init(InitParams))
			{
				InitializationState = EMixerSourceInitializationState::Initialized;

				Update();

				return true;
			}
			else
			{
				InitializationState = EMixerSourceInitializationState::NotInitialized;
				UE_LOG(LogAudioMixer, Warning, TEXT("Failed to initialize mixer source voice '%s'."), *InWaveInstance->GetName());
			}
		}
		else
		{
			UE_LOG(LogAudioMixer, Warning, TEXT("Num channels was 0 for sound buffer '%s'."), *InWaveInstance->GetName());
		}

		FreeResources();
		return false;
	}

	void FMixerSource::SetupBusData(TArray<FInitAudioBusSend>* OutAudioBusSends)
	{
		for (int32 BusSendType = 0; BusSendType < (int32)EBusSendType::Count; ++BusSendType)
		{
			// And add all the source bus sends
			for (FSoundSourceBusSendInfo& SendInfo : WaveInstance->BusSends[BusSendType])
			{
				// Avoid redoing duplicate code for sending audio to source bus or audio bus. Most of it is the same other than the bus id.
				auto SetupBusSend = [this](TArray<FInitAudioBusSend>* AudioBusSends, const FSoundSourceBusSendInfo& InSendInfo, int32 InBusSendType, uint32 InBusId)
				{
					FInitAudioBusSend BusSend;
					BusSend.AudioBusId = InBusId;
					BusSend.SendLevel = InSendInfo.SendLevel;

					if (AudioBusSends)
					{
						AudioBusSends[InBusSendType].Add(BusSend);
					}

					FDynamicBusSendInfo NewDynamicBusSendInfo;
					NewDynamicBusSendInfo.SendLevel = InSendInfo.SendLevel;
					NewDynamicBusSendInfo.BusId = BusSend.AudioBusId;
					NewDynamicBusSendInfo.BusSendLevelControlMethod = InSendInfo.SourceBusSendLevelControlMethod;
					NewDynamicBusSendInfo.BusSendType = (EBusSendType)InBusSendType;
					NewDynamicBusSendInfo.MinSendLevel = InSendInfo.MinSendLevel;
					NewDynamicBusSendInfo.MaxSendLevel = InSendInfo.MaxSendLevel;
					NewDynamicBusSendInfo.MinSendDistance = InSendInfo.MinSendDistance;
					NewDynamicBusSendInfo.MaxSendDistance = InSendInfo.MaxSendDistance;
					NewDynamicBusSendInfo.CustomSendLevelCurve = InSendInfo.CustomSendLevelCurve;

					// Copy the bus SourceBusSendInfo structs to a local copy so we can update it in the update tick
					bool bIsNew = true;
					for (FDynamicBusSendInfo& BusSendInfo : DynamicBusSendInfos)
					{
						if (BusSendInfo.BusId == NewDynamicBusSendInfo.BusId)
						{
							BusSendInfo = NewDynamicBusSendInfo;
							BusSendInfo.bIsInit = false;
							bIsNew = false;
							break;
						}
					}

					if (bIsNew)
					{
						DynamicBusSendInfos.Add(NewDynamicBusSendInfo);
					}

					// Flag that we're sending audio to buses so we can check for updates to send levels
					bSendingAudioToBuses = true;
				};

				// Retrieve bus id of the audio bus to use
				if (SendInfo.SoundSourceBus)
				{						
					uint32 BusId;

					// Either use the bus id of the source bus's audio bus id if it was specified
					if (SendInfo.SoundSourceBus->AudioBus)
					{
						BusId = SendInfo.SoundSourceBus->AudioBus->GetUniqueID();
					}
					else
					{
						// otherwise, use the id of the source bus itself (for an automatic source bus)
						BusId = SendInfo.SoundSourceBus->GetUniqueID();
					}

					// Call lambda w/ the correctly derived bus id
					SetupBusSend(OutAudioBusSends, SendInfo, BusSendType, BusId);
				}

				if (SendInfo.AudioBus)
				{
					// Only need to send audio to just the specified audio bus
					uint32 BusId = SendInfo.AudioBus->GetUniqueID();

					// Note we will be sending audio to both the specified source bus and the audio bus with the same send level
					SetupBusSend(OutAudioBusSends, SendInfo, BusSendType, BusId);
				}
			}
		}
	}

	void FMixerSource::Update()
	{
		CSV_SCOPED_TIMING_STAT(Audio, UpdateSources);

		LLM_SCOPE(ELLMTag::AudioMixer);

		if (!WaveInstance || !MixerSourceVoice || Paused || InitializationState == EMixerSourceInitializationState::NotInitialized)
		{
			return;
		}

		// if MarkPendingKill() was called, WaveInstance->WaveData is null
		if (!WaveInstance->WaveData)
		{
			StopNow();
			return;
		}

		++TickCount;

		UpdatePitch();

		UpdateVolume();

		UpdateSpatialization();

		UpdateEffects();

		UpdateSourceBusSends();

		UpdateChannelMaps();

#if ENABLE_AUDIO_DEBUG
		Audio::FAudioDebugger::DrawDebugInfo(*this);
#endif // ENABLE_AUDIO_DEBUG
	}

	bool FMixerSource::PrepareForInitialization(FWaveInstance* InWaveInstance)
	{
		LLM_SCOPE(ELLMTag::AudioMixer);

		// We are currently not supporting playing audio on a controller
		if (InWaveInstance->OutputTarget == EAudioOutputTarget::Controller)
		{
			return false;
		}

		// We are not initialized yet. We won't be until the sound file finishes loading and parsing the header.
		InitializationState = EMixerSourceInitializationState::Initializing;

		//  Reset so next instance will warn if algorithm changes in-flight
		bEditorWarnedChangedSpatialization = false;

		const bool bIsSeeking = InWaveInstance->StartTime > 0.0f;

		check(InWaveInstance);
		check(AudioDevice);

		check(!MixerBuffer);
		MixerBuffer = FMixerBuffer::Init(AudioDevice, InWaveInstance->WaveData, bIsSeeking /* bForceRealtime */);

		if (!MixerBuffer)
		{
			FreeResources(); // APM: maybe need to call this here too? 
			return false;
		}

		// WaveData must be valid beyond this point, otherwise MixerBuffer
		// would have failed to init.
		check(InWaveInstance->WaveData);
		USoundWave& SoundWave = *InWaveInstance->WaveData;

		Buffer = MixerBuffer;
		WaveInstance = InWaveInstance;

		LPFFrequency = MAX_FILTER_FREQUENCY;
		LastLPFFrequency = FLT_MAX;

		HPFFrequency = 0.0f;
		LastHPFFrequency = FLT_MAX;

		bIsDone = false;

		// Not all wave data types have a non-zero duration
		if (SoundWave.Duration > 0.0f)
		{
			if (!SoundWave.bIsSourceBus)
			{
				NumTotalFrames = SoundWave.Duration * SoundWave.GetSampleRateForCurrentPlatform();
				check(NumTotalFrames > 0);
			}
			else if (!SoundWave.IsLooping())
			{
				NumTotalFrames = SoundWave.Duration * AudioDevice->GetSampleRate();
				check(NumTotalFrames > 0);
			}

			StartFrame = FMath::Clamp<int32>((InWaveInstance->StartTime / SoundWave.Duration) * NumTotalFrames, 0, NumTotalFrames);
		}

		check(!MixerSourceBuffer.IsValid());		
		MixerSourceBuffer = FMixerSourceBuffer::Create(AudioDevice->GetSampleRate(), *MixerBuffer, SoundWave, InWaveInstance->LoopingMode, bIsSeeking);
		
		if (!MixerSourceBuffer.IsValid())
		{
			FreeResources();

			// Guarantee that this wave instance does not try to replay by disabling looping.
			WaveInstance->LoopingMode = LOOP_Never;

			if (ensure(WaveInstance->ActiveSound))
			{
				WaveInstance->ActiveSound->bShouldRemainActiveIfDropped = false;
			}
		}
		
		return MixerSourceBuffer.IsValid();
	}

	bool FMixerSource::IsPreparedToInit()
	{
		LLM_SCOPE(ELLMTag::AudioMixer);

		if (MixerBuffer && MixerBuffer->IsRealTimeSourceReady())
		{
			check(MixerSourceBuffer.IsValid());

			// Check if we have a realtime audio task already (doing first decode)
			if (MixerSourceBuffer->IsAsyncTaskInProgress())
			{
				// not ready
				return MixerSourceBuffer->IsAsyncTaskDone();
			}
			else if (WaveInstance)
			{
				if (WaveInstance->WaveData->bIsSourceBus)
				{
					// Buses don't need to do anything to play audio
					return true;
				}
				else
				{
					// Now check to see if we need to kick off a decode the first chunk of audio
					const EBufferType::Type BufferType = MixerBuffer->GetType();
					if ((BufferType == EBufferType::PCMRealTime || BufferType == EBufferType::Streaming) && WaveInstance->WaveData)
					{
						// If any of these conditions meet, we need to do an initial async decode before we're ready to start playing the sound
						if (WaveInstance->StartTime > 0.0f || WaveInstance->WaveData->bProcedural || WaveInstance->WaveData->bIsSourceBus || !WaveInstance->WaveData->CachedRealtimeFirstBuffer)
						{
							// Before reading more PCMRT data, we first need to seek the buffer
							if (WaveInstance->IsSeekable())
							{
								MixerBuffer->Seek(WaveInstance->StartTime);
							}

							check(MixerSourceBuffer.IsValid());

							ICompressedAudioInfo* Decoder = MixerBuffer->GetDecompressionState(false);
							if (BufferType == EBufferType::Streaming)
							{
								IStreamingManager::Get().GetAudioStreamingManager().AddDecoder(Decoder);
							}

							MixerSourceBuffer->ReadMoreRealtimeData(Decoder, 0, EBufferReadMode::Asynchronous);

							// not ready
							return false;
						}
					}
				}
			}

			return true;
		}

		return false;
	}

	bool FMixerSource::IsInitialized() const
	{
		return InitializationState == EMixerSourceInitializationState::Initialized;
	}

	void FMixerSource::Play()
	{
		if (!WaveInstance)
		{
			return;
		}

		// Don't restart the sound if it was stopping when we paused, just stop it.
		if (Paused && (bIsStopping || bIsDone))
		{
			StopNow();
			return;
		}

		if (bIsStopping)
		{
			UE_LOG(LogAudioMixer, Warning, TEXT("Restarting a source which was stopping. Stopping now."));
			return;
		}

		// It's possible if Pause and Play are called while a sound is async initializing. In this case
		// we'll just not actually play the source here. Instead we'll call play when the sound finishes loading.
		if (MixerSourceVoice && InitializationState == EMixerSourceInitializationState::Initialized)
		{
			if (WaveInstance && WaveInstance->WaveData && WaveInstance->WaveData->bProcedural)
			{
				WaveInstance->WaveData->bPlayingProcedural = true;
			}

			MixerSourceVoice->Play();
		}

		bIsStopping = false;
		Paused = false;
		Playing = true;
		bLoopCallback = false;
		bIsDone = false;
	}

	void FMixerSource::Stop()
	{
		LLM_SCOPE(ELLMTag::AudioMixer);

		if (InitializationState == EMixerSourceInitializationState::NotInitialized)
		{
			return;
		}

		if (!MixerSourceVoice)
		{
			StopNow();
			return;
		}

		// Always stop procedural sounds immediately.
		if (WaveInstance && WaveInstance->WaveData && WaveInstance->WaveData->bProcedural)
		{
			WaveInstance->WaveData->bPlayingProcedural = false;
			StopNow();
			return;
		}

		if (bIsDone)
		{
			StopNow();
		}
		else if (!bIsStopping)
		{
			// Otherwise, we need to do a quick fade-out of the sound and put the state
			// of the sound into "stopping" mode. This prevents this source from
			// being put into the "free" pool and prevents the source from freeing its resources
			// until the sound has finished naturally (i.e. faded all the way out)

			// StopFade will stop a sound with a very small fade to avoid discontinuities
			if (MixerSourceVoice && Playing)
			{
				// if MarkPendingKill() was called, WaveInstance->WaveData is null
				if (!WaveInstance || !WaveInstance->WaveData)
				{
					StopNow();
					return;
				}
				else if (bIsStoppingVoicesEnabled && !WaveInstance->WaveData->bProcedural)
				{
					// Let the wave instance know it's stopping
					WaveInstance->SetStopping(true);

					// TODO: parameterize the number of fades
					MixerSourceVoice->StopFade(512);
					bIsStopping = true;
				}
				else
				{
					StopNow();
				}
			}
			Paused = false;
		}
	}

	void FMixerSource::StopNow()
	{
		LLM_SCOPE(ELLMTag::AudioMixer);

		// Immediately stop the sound source

		InitializationState = EMixerSourceInitializationState::NotInitialized;

		IStreamingManager::Get().GetAudioStreamingManager().RemoveStreamingSoundSource(this);

		bIsStopping = false;

		if (WaveInstance)
		{
			if (MixerSourceVoice && Playing)
			{
				MixerSourceVoice->Stop();
			}

			Paused = false;
			Playing = false;

			FreeResources();
		}

		FSoundSource::Stop();
	}

	void FMixerSource::Pause()
	{
		if (!WaveInstance)
		{
			return;
		}

		if (bIsStopping)
		{
			return;
		}

		if (MixerSourceVoice)
		{
			MixerSourceVoice->Pause();
		}

		Paused = true;
	}

	bool FMixerSource::IsFinished()
	{
		// A paused source is not finished.
		if (Paused)
		{
			return false;
		}

		if (InitializationState == EMixerSourceInitializationState::NotInitialized)
		{
			return true;
		}

		if (InitializationState == EMixerSourceInitializationState::Initializing)
		{
			return false;
		}

		if (WaveInstance && MixerSourceVoice)
		{
			if (bIsDone && bIsEffectTailsDone)
			{
				WaveInstance->NotifyFinished();
				bIsStopping = false;
				return true;
			}
			else if (bLoopCallback && WaveInstance->LoopingMode == LOOP_WithNotification)
			{
				WaveInstance->NotifyFinished();
				bLoopCallback = false;
			}

			return false;
		}
		return true;
	}

	float FMixerSource::GetPlaybackPercent() const
	{
		if (InitializationState != EMixerSourceInitializationState::Initialized)
		{
			return PreviousPlaybackPercent;
		}

		if (MixerSourceVoice && NumTotalFrames > 0)
		{
			int64 NumFrames = StartFrame + MixerSourceVoice->GetNumFramesPlayed();
			AUDIO_MIXER_CHECK(NumTotalFrames > 0);
			PreviousPlaybackPercent = (float)NumFrames / NumTotalFrames;
			if (WaveInstance->LoopingMode == LOOP_Never)
			{
				PreviousPlaybackPercent = FMath::Min(PreviousPlaybackPercent, 1.0f);
			}
			return PreviousPlaybackPercent;
		}
		else
		{
			// If we don't have any frames, that means it's a procedural sound wave, which means
			// that we're never going to have a playback percentage.
			return 1.0f;
		}
	}

	float FMixerSource::GetEnvelopeValue() const
	{
		if (MixerSourceVoice)
		{
			return MixerSourceVoice->GetEnvelopeValue();
		}
		return 0.0f;
	}

	void FMixerSource::OnBeginGenerate()
	{
	}

	void FMixerSource::OnDone()
	{
		bIsDone = true;
	}

	void FMixerSource::OnEffectTailsDone()
	{
		bIsEffectTailsDone = true;
	}

	void FMixerSource::FreeResources()
	{
		LLM_SCOPE(ELLMTag::AudioMixer);

		if (MixerBuffer)
		{
			MixerBuffer->EnsureHeaderParseTaskFinished();
		}

		check(!bIsStopping);
		check(!Playing);

		// Make a new pending release data ptr to pass off release data
		if (MixerSourceVoice)
		{
			// We're now "releasing" so don't recycle this voice until we get notified that the source has finished
			bIsReleasing = true;

			// This will trigger FMixerSource::OnRelease from audio render thread.
			MixerSourceVoice->Release();
			MixerSourceVoice = nullptr;
		}

		MixerSourceBuffer.Reset();
		Buffer = nullptr;
		bLoopCallback = false;
		NumTotalFrames = 0;

		if (MixerBuffer)
		{
			EBufferType::Type BufferType = MixerBuffer->GetType();
			if (BufferType == EBufferType::PCMRealTime || BufferType == EBufferType::Streaming)
			{
				delete MixerBuffer;
			}

			MixerBuffer = nullptr;
		}

		// Reset the source's channel maps
		ChannelMap.Reset();

		InitializationState = EMixerSourceInitializationState::NotInitialized;
	}

	void FMixerSource::UpdatePitch()
	{
		AUDIO_MIXER_CHECK(MixerBuffer);

		check(WaveInstance);

		FActiveSound* ActiveSound = WaveInstance->ActiveSound;
		check(ActiveSound);

		Pitch = WaveInstance->GetPitch();

		// Don't apply global pitch scale to UI sounds
		if (!WaveInstance->bIsUISound)
		{
			Pitch *= AudioDevice->GetGlobalPitchScale().GetValue();
		}

		Pitch = AudioDevice->ClampPitch(Pitch);

		// Scale the pitch by the ratio of the audio buffer sample rate and the actual sample rate of the hardware
		if (MixerBuffer)
		{
			const float MixerBufferSampleRate = MixerBuffer->GetSampleRate();
			const float AudioDeviceSampleRate = AudioDevice->GetSampleRate();
			Pitch *= MixerBufferSampleRate / AudioDeviceSampleRate;

			MixerSourceVoice->SetPitch(Pitch);
		}

		USoundWave* WaveData = WaveInstance->WaveData;
		check(WaveData);
		const FSoundModulationDestinationSettings& PitchSettings = ModulationUtils::GetRoutedPitchModulation(*WaveInstance, *WaveData, ActiveSound);
		MixerSourceVoice->SetModPitch(PitchSettings.Value);
	}

	void FMixerSource::UpdateVolume()
	{
		MixerSourceVoice->SetDistanceAttenuation(WaveInstance->GetDistanceAttenuation());

		float CurrentVolume = 0.0f;
		if (!AudioDevice->IsAudioDeviceMuted())
		{
			// 1. Apply device gain stage(s)
			CurrentVolume = WaveInstance->ActiveSound->bIsPreviewSound ? 1.0f : AudioDevice->GetMasterVolume();
			CurrentVolume *= AudioDevice->GetPlatformAudioHeadroom();

			// 2. Apply instance gain stage(s)
			CurrentVolume *= WaveInstance->GetVolume();
			CurrentVolume *= WaveInstance->GetDynamicVolume();

			// 3. Apply editor gain stage(s)
			CurrentVolume = FMath::Clamp<float>(GetDebugVolume(CurrentVolume), 0.0f, MAX_VOLUME);

			FActiveSound* ActiveSound = WaveInstance->ActiveSound;
			check(ActiveSound);

			USoundWave* WaveData = WaveInstance->WaveData;
			check(WaveData);
			const FSoundModulationDestinationSettings& VolumeSettings = ModulationUtils::GetRoutedVolumeModulation(*WaveInstance, *WaveData, ActiveSound);
			MixerSourceVoice->SetModVolume(VolumeSettings.Value);
		}
		MixerSourceVoice->SetVolume(CurrentVolume);
	}

	void FMixerSource::UpdateSpatialization()
	{
		SpatializationParams = GetSpatializationParams();
		if (WaveInstance->GetUseSpatialization())
		{
			MixerSourceVoice->SetSpatializationParams(SpatializationParams);
		}
	}

	void FMixerSource::UpdateEffects()
	{
		// Update the default LPF filter frequency
		SetFilterFrequency();

		if (LastLPFFrequency != LPFFrequency)
		{
			MixerSourceVoice->SetLPFFrequency(LPFFrequency);
			LastLPFFrequency = LPFFrequency;
		}

		if (LastHPFFrequency != HPFFrequency)
		{
			MixerSourceVoice->SetHPFFrequency(HPFFrequency);
			LastHPFFrequency = HPFFrequency;
		}

		check(WaveInstance);
		FActiveSound* ActiveSound = WaveInstance->ActiveSound;
		check(ActiveSound);

		USoundWave* WaveData = WaveInstance->WaveData;
		check(WaveData);

		const FSoundModulationDestinationSettings& HighpassSettings = ModulationUtils::GetRoutedHighpassModulation(*WaveInstance, *WaveData, ActiveSound);
		MixerSourceVoice->SetModHPFFrequency(HighpassSettings.Value);

		const FSoundModulationDestinationSettings& LowpassSettings = ModulationUtils::GetRoutedLowpassModulation(*WaveInstance, *WaveData, ActiveSound);
		MixerSourceVoice->SetModLPFFrequency(LowpassSettings.Value);

		// If reverb is applied, figure out how of the source to "send" to the reverb.
		if (bReverbApplied)
		{
			float ReverbSendLevel = 0.0f;

			if (WaveInstance->ReverbSendMethod == EReverbSendMethod::Manual)
			{
				ReverbSendLevel = FMath::Clamp(WaveInstance->ManualReverbSendLevel, 0.0f, 1.0f);
			}
			else
			{
				// The alpha value is determined identically between manual and custom curve methods
				const FVector2D& ReverbSendRadialRange = WaveInstance->ReverbSendLevelDistanceRange;
				const float Denom = FMath::Max(ReverbSendRadialRange.Y - ReverbSendRadialRange.X, 1.0f);
				const float Alpha = FMath::Clamp((WaveInstance->ListenerToSoundDistance - ReverbSendRadialRange.X) / Denom, 0.0f, 1.0f);

				if (WaveInstance->ReverbSendMethod == EReverbSendMethod::Linear)
				{
					ReverbSendLevel = FMath::Clamp(FMath::Lerp(WaveInstance->ReverbSendLevelRange.X, WaveInstance->ReverbSendLevelRange.Y, Alpha), 0.0f, 1.0f);
				}
				else
				{
					ReverbSendLevel = FMath::Clamp(WaveInstance->CustomRevebSendCurve.GetRichCurveConst()->Eval(Alpha), 0.0f, 1.0f);
				}
			}

			// Send the source audio to the reverb plugin if enabled
			if (UseReverbPlugin() && AudioDevice->ReverbPluginInterface)
			{
				check(MixerDevice);
				FMixerSubmixPtr ReverbPluginSubmixPtr = MixerDevice->GetSubmixInstance(AudioDevice->ReverbPluginInterface->GetSubmix()).Pin();
				if (ReverbPluginSubmixPtr.IsValid())
				{
					MixerSourceVoice->SetSubmixSendInfo(ReverbPluginSubmixPtr, ReverbSendLevel);
				}
			}

			// Send the source audio to the master reverb
			MixerSourceVoice->SetSubmixSendInfo(MixerDevice->GetMasterReverbSubmix(), ReverbSendLevel);
		}

		if (WaveInstance->SubmixSendSettings.Num() > 0)
		{
			for (const FAttenuationSubmixSendSettings& SendSettings : WaveInstance->SubmixSendSettings)
			{
				if (SendSettings.Submix)
				{
					float SubmixSendLevel = 0.0f;

					if (SendSettings.SubmixSendMethod == ESubmixSendMethod::Manual)
					{
						SubmixSendLevel = FMath::Clamp(SendSettings.ManualSubmixSendLevel, 0.0f, 1.0f);
					}
					else
					{
						// The alpha value is determined identically between manual and custom curve methods
						const float Denom = FMath::Max(SendSettings.SubmixSendDistanceMax - SendSettings.SubmixSendDistanceMin, 1.0f);
						const float Alpha = FMath::Clamp((WaveInstance->ListenerToSoundDistance - SendSettings.SubmixSendDistanceMin) / Denom, 0.0f, 1.0f);

						if (WaveInstance->ReverbSendMethod == EReverbSendMethod::Linear)
						{
							SubmixSendLevel = FMath::Clamp(FMath::Lerp(SendSettings.SubmixSendLevelMin, SendSettings.SubmixSendLevelMax, Alpha), 0.0f, 1.0f);
						}
						else
						{
							SubmixSendLevel = FMath::Clamp(SendSettings.CustomSubmixSendCurve.GetRichCurveConst()->Eval(Alpha), 0.0f, 1.0f);
						}
					}


					FMixerSubmixPtr SubmixPtr = MixerDevice->GetSubmixInstance(SendSettings.Submix).Pin();
					MixerSourceVoice->SetSubmixSendInfo(SubmixPtr, SubmixSendLevel);
				}
			}
		}

		// Clear submix sends if they need clearing.
		if (PreviousSubmixSendSettings.Num() > 0)
		{
			// Loop through every previous send setting
			for (FSoundSubmixSendInfo& PreviousSendSetting : PreviousSubmixSendSettings)
			{
				bool bFound = false;

				// See if it's in the current send list
				for (const FSoundSubmixSendInfo& CurrentSendSettings : WaveInstance->SoundSubmixSends)
				{
					if (CurrentSendSettings.SoundSubmix == PreviousSendSetting.SoundSubmix)
					{
						bFound = true;
						break;
					}
				}

				// If it's not in the current send list, add to submixes to clear
				if (!bFound)
				{
					FMixerSubmixPtr SubmixPtr = MixerDevice->GetSubmixInstance(PreviousSendSetting.SoundSubmix).Pin();
					MixerSourceVoice->ClearSubmixSendInfo(SubmixPtr);
				}
			}
		}
		PreviousSubmixSendSettings = WaveInstance->SoundSubmixSends;

		// Update submix send levels
		for (FSoundSubmixSendInfo& SendInfo : WaveInstance->SoundSubmixSends)
		{
			if (SendInfo.SoundSubmix != nullptr)
			{
				FMixerSubmixWeakPtr SubmixInstance = MixerDevice->GetSubmixInstance(SendInfo.SoundSubmix);
				float SendLevel = 1.0f;

				// calculate send level based on distance if that method is enabled
				if (SendInfo.SendLevelControlMethod == ESendLevelControlMethod::Manual)
				{
					SendLevel = FMath::Clamp(SendInfo.SendLevel, 0.0f, 1.0f);
				}
				else
				{
					// The alpha value is determined identically between manual and custom curve methods
					const FVector2D SendRadialRange = { SendInfo.MinSendDistance, SendInfo.MaxSendDistance};
					const FVector2D SendLevelRange = { SendInfo.MinSendLevel, SendInfo.MaxSendLevel };
					const float Denom = FMath::Max(SendRadialRange.Y - SendRadialRange.X, 1.0f);
					const float Alpha = FMath::Clamp((WaveInstance->ListenerToSoundDistance - SendRadialRange.X) / Denom, 0.0f, 1.0f);

					if (SendInfo.SendLevelControlMethod == ESendLevelControlMethod::Linear)
					{
						SendLevel = FMath::Clamp(FMath::Lerp(SendLevelRange.X, SendLevelRange.Y, Alpha), 0.0f, 1.0f);
					}
					else // use curve
					{
						SendLevel = FMath::Clamp(SendInfo.CustomSendLevelCurve.GetRichCurveConst()->Eval(Alpha), 0.0f, 1.0f);
					}
				}

				// set the level for this send
				MixerSourceVoice->SetSubmixSendInfo(SubmixInstance, SendLevel);
			}
		}
 	}

	void FMixerSource::UpdateSourceBusSends()
	{
		// 1) loop through all bus sends
		// 2) check for any bus sends that are set to update non-manually
		// 3) Cache previous send level and only do update if it's changed in any significant amount

		SetupBusData();

		if (!bSendingAudioToBuses)
		{
			return;
		}

		//If the user actively called a function that alters bus sends since the last update
		FActiveSound* ActiveSound = WaveInstance->ActiveSound;
		check(ActiveSound);

		if (ActiveSound->HasNewBusSends())
		{
			TArray<TTuple<EBusSendType, FSoundSourceBusSendInfo>> NewBusSends = ActiveSound->GetNewBusSends();
			for (TTuple<EBusSendType, FSoundSourceBusSendInfo>& newSend : NewBusSends)
			{
				MixerSourceVoice->SetAudioBusSendInfo(newSend.Key, newSend.Value.SoundSourceBus->GetUniqueID(), newSend.Value.SendLevel);
			}

			ActiveSound->ResetNewBusSends();
		}

		// If this source is sending its audio to a bus, we need to check if it needs to be updated
		for (FDynamicBusSendInfo& DynamicBusSendInfo : DynamicBusSendInfos)
		{
			float SendLevel = 0.0f;

			if (DynamicBusSendInfo.BusSendLevelControlMethod != ESourceBusSendLevelControlMethod::Manual)
			{
				// The alpha value is determined identically between linear and custom curve methods
				const FVector2D SendRadialRange = { DynamicBusSendInfo.MinSendDistance, DynamicBusSendInfo.MaxSendDistance};
				const FVector2D SendLevelRange = { DynamicBusSendInfo.MinSendLevel, DynamicBusSendInfo.MaxSendLevel };
				const float Denom = FMath::Max(SendRadialRange.Y - SendRadialRange.X, 1.0f);
				const float Alpha = FMath::Clamp((WaveInstance->ListenerToSoundDistance - SendRadialRange.X) / Denom, 0.0f, 1.0f);

				if (DynamicBusSendInfo.BusSendLevelControlMethod == ESourceBusSendLevelControlMethod::Linear)
				{
					SendLevel = FMath::Clamp(FMath::Lerp(SendLevelRange.X, SendLevelRange.Y, Alpha), 0.0f, 1.0f);
				}
				else // use curve
				{
					SendLevel = FMath::Clamp(DynamicBusSendInfo.CustomSendLevelCurve.GetRichCurveConst()->Eval(Alpha), 0.0f, 1.0f);
				}

				// If the send level changed, then we need to send an update to the audio render thread
				if (!FMath::IsNearlyEqual(SendLevel, DynamicBusSendInfo.SendLevel) || DynamicBusSendInfo.bIsInit)
				{
					DynamicBusSendInfo.SendLevel = SendLevel;
					DynamicBusSendInfo.bIsInit = false;

					MixerSourceVoice->SetAudioBusSendInfo(DynamicBusSendInfo.BusSendType, DynamicBusSendInfo.BusId, SendLevel);
				}
			}

		}
	}

	void FMixerSource::UpdateChannelMaps()
	{
		SetLFEBleed();

		int32 NumOutputDeviceChannels = MixerDevice->GetNumDeviceChannels();
		const FAudioPlatformDeviceInfo& DeviceInfo = MixerDevice->GetPlatformDeviceInfo();

		// Compute a new speaker map for each possible output channel mapping for the source
		const uint32 NumChannels = Buffer->NumChannels;
		if (ComputeChannelMap(Buffer->NumChannels, ChannelMap))
		{
			MixerSourceVoice->SetChannelMap(NumChannels, ChannelMap, bIs3D, WaveInstance->bCenterChannelOnly);
		}

		bPrevAllowedSpatializationSetting = IsSpatializationCVarEnabled();
	}

	bool FMixerSource::ComputeMonoChannelMap(Audio::AlignedFloatBuffer& OutChannelMap)
	{
		if (IsUsingObjectBasedSpatialization())
		{
			if (WaveInstance->SpatializationMethod != ESoundSpatializationAlgorithm::SPATIALIZATION_HRTF && !bEditorWarnedChangedSpatialization)
			{
				bEditorWarnedChangedSpatialization = true;
				UE_LOG(LogAudioMixer, Warning, TEXT("Changing the spatialization method on a playing sound is not supported (WaveInstance: %s)"), *WaveInstance->WaveData->GetFullName());
			}

			// Treat the source as if it is a 2D stereo source:
			return ComputeStereoChannelMap(OutChannelMap);
		}
		else if (WaveInstance->GetUseSpatialization() && (!FMath::IsNearlyEqual(WaveInstance->AbsoluteAzimuth, PreviousAzimuth, 0.01f) || MixerSourceVoice->NeedsSpeakerMap()))
		{
			// Don't need to compute the source channel map if the absolute azimuth hasn't changed much
			PreviousAzimuth = WaveInstance->AbsoluteAzimuth;
			OutChannelMap.Reset();
			MixerDevice->Get3DChannelMap(MixerDevice->GetNumDeviceChannels(), WaveInstance, WaveInstance->AbsoluteAzimuth, SpatializationParams.NormalizedOmniRadius, OutChannelMap);
			return true;
		}
		else if (!OutChannelMap.Num() || (IsSpatializationCVarEnabled() != bPrevAllowedSpatializationSetting))
		{
			// Only need to compute the 2D channel map once
			MixerDevice->Get2DChannelMap(bIsVorbis, 1, WaveInstance->bCenterChannelOnly, OutChannelMap);
			return true;
		}

		// Return false means the channel map hasn't changed
		return false;
	}

	bool FMixerSource::ComputeStereoChannelMap(Audio::AlignedFloatBuffer& OutChannelMap)
	{
		// Only recalculate positional data if the source has moved a significant amount:
		if (WaveInstance->GetUseSpatialization() && (!FMath::IsNearlyEqual(WaveInstance->AbsoluteAzimuth, PreviousAzimuth, 0.01f) || MixerSourceVoice->NeedsSpeakerMap()))
		{
			// Make sure our stereo emitter positions are updated relative to the sound emitter position
			if (Buffer->NumChannels == 2)
			{
				UpdateStereoEmitterPositions();
			}

			// Check whether voice is currently using 
			if (!IsUsingObjectBasedSpatialization())
			{
				float AzimuthOffset = 0.0f;

				float LeftAzimuth = 90.0f;
				float RightAzimuth = 270.0f;

				const float DistanceToUse = UseListenerOverrideForSpreadCVar ? WaveInstance->ListenerToSoundDistance : WaveInstance->ListenerToSoundDistanceForPanning;

				if (DistanceToUse > KINDA_SMALL_NUMBER)
				{
					AzimuthOffset = FMath::Atan(0.5f * WaveInstance->StereoSpread / DistanceToUse);
					AzimuthOffset = FMath::RadiansToDegrees(AzimuthOffset);

					LeftAzimuth = WaveInstance->AbsoluteAzimuth - AzimuthOffset;
					if (LeftAzimuth < 0.0f)
					{
						LeftAzimuth += 360.0f;
					}

					RightAzimuth = WaveInstance->AbsoluteAzimuth + AzimuthOffset;
					if (RightAzimuth > 360.0f)
					{
						RightAzimuth -= 360.0f;
					}
				}

				// Reset the channel map, the stereo spatialization channel mapping calls below will append their mappings
				OutChannelMap.Reset();

				const int32 NumOutputChannels = MixerDevice->GetNumDeviceChannels();

				MixerDevice->Get3DChannelMap(NumOutputChannels, WaveInstance, LeftAzimuth, SpatializationParams.NormalizedOmniRadius, OutChannelMap);
				MixerDevice->Get3DChannelMap(NumOutputChannels, WaveInstance, RightAzimuth, SpatializationParams.NormalizedOmniRadius, OutChannelMap);

				return true;
			}
		}

		if (!OutChannelMap.Num() || (IsSpatializationCVarEnabled() != bPrevAllowedSpatializationSetting))
		{
			MixerDevice->Get2DChannelMap(bIsVorbis, 2, WaveInstance->bCenterChannelOnly, OutChannelMap);
			return true;
		}

		return false;
	}

	bool FMixerSource::ComputeChannelMap(const int32 NumSourceChannels, Audio::AlignedFloatBuffer& OutChannelMap)
	{
		if (NumSourceChannels == 1)
		{
			return ComputeMonoChannelMap(OutChannelMap);
		}
		else if (NumSourceChannels == 2)
		{
			return ComputeStereoChannelMap(OutChannelMap);
		}
		else if (!OutChannelMap.Num())
		{
			MixerDevice->Get2DChannelMap(bIsVorbis, NumSourceChannels, WaveInstance->bCenterChannelOnly, OutChannelMap);
			return true;
		}
		return false;
	}

	bool FMixerSource::UseObjectBasedSpatialization() const
	{
		return (Buffer->NumChannels <= MixerDevice->MaxChannelsSupportedBySpatializationPlugin &&
				AudioDevice->IsSpatializationPluginEnabled() &&
				WaveInstance->SpatializationMethod == ESoundSpatializationAlgorithm::SPATIALIZATION_HRTF);
	}

	bool FMixerSource::IsUsingObjectBasedSpatialization() const
	{
		bool bIsUsingObjectBaseSpatialization = UseObjectBasedSpatialization();

		if (MixerSourceVoice)
		{
			// If it is currently playing, check whether it actively uses HRTF spatializer.
			// HRTF spatialization cannot be altered on currently playing source. So this handles
			// the case where the source was initialized without HRTF spatialization before HRTF
			// spatialization is enabled. 
			bool bDefaultIfNoSourceId = true;
			bIsUsingObjectBaseSpatialization &= MixerSourceVoice->IsUsingHRTFSpatializer(bDefaultIfNoSourceId);
		}
		return bIsUsingObjectBaseSpatialization;
	}

	bool FMixerSource::UseSpatializationPlugin() const
	{
		return (Buffer->NumChannels <= MixerDevice->MaxChannelsSupportedBySpatializationPlugin) &&
			AudioDevice->IsSpatializationPluginEnabled() &&
			WaveInstance->SpatializationPluginSettings != nullptr;
	}

	bool FMixerSource::UseOcclusionPlugin() const
	{
		return (Buffer->NumChannels == 1 || Buffer->NumChannels == 2) &&
			AudioDevice->IsOcclusionPluginEnabled() &&
			WaveInstance->OcclusionPluginSettings != nullptr;
	}

	bool FMixerSource::UseReverbPlugin() const
	{
		return (Buffer->NumChannels == 1 || Buffer->NumChannels == 2) &&
			AudioDevice->IsReverbPluginEnabled() &&
			WaveInstance->ReverbPluginSettings != nullptr;
	}
}
