// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Components/SynthComponent.h"
#include "AudioDevice.h"
#include "AudioMixerLog.h"

USynthSound::USynthSound(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, OwningSynthComponent(nullptr)
{
}

void USynthSound::Init(USynthComponent* InSynthComponent, const int32 InNumChannels, const int32 InSampleRate, const int32 InCallbackSize)
{
	OwningSynthComponent = InSynthComponent;
	VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
	NumChannels = InNumChannels;
	NumSamplesToGeneratePerCallback = InCallbackSize;
	// Turn off async generation in old audio engine on mac.
#if PLATFORM_MAC
	const FAudioDevice* AudioDevice = InSynthComponent->GetAudioDevice();
	if (AudioDevice && !AudioDevice->IsAudioMixerEnabled())
	{
		bCanProcessAsync = false;
	}
	else
#endif // #if PLATFORM_MAC
	{
		bCanProcessAsync = true;
	}

	Duration = INDEFINITELY_LOOPING_DURATION;
	bLooping = true;
	SampleRate = InSampleRate;
}

void USynthSound::StartOnAudioDevice(FAudioDevice* InAudioDevice)
{
	check(InAudioDevice != nullptr);
	bAudioMixer = InAudioDevice->IsAudioMixerEnabled();
}

void USynthSound::OnBeginGenerate()
{
	check(OwningSynthComponent);
	OwningSynthComponent->OnBeginGenerate();
}

int32 USynthSound::OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples)
{
	OutAudio.Reset();

	if (bAudioMixer)
	{
		// If running with audio mixer, the output audio buffer will be in floats already
		OutAudio.AddZeroed(NumSamples * sizeof(float));
		return OwningSynthComponent->OnGeneratePCMAudio((float*)OutAudio.GetData(), NumSamples);
	}
	else
	{
		// Use the float scratch buffer instead of the out buffer directly
		FloatBuffer.Reset();
		FloatBuffer.AddZeroed(NumSamples * sizeof(float));

		float* FloatBufferDataPtr = FloatBuffer.GetData();
		int32 NumSamplesGenerated = OwningSynthComponent->OnGeneratePCMAudio(FloatBufferDataPtr, NumSamples);

		// Convert the float buffer to int16 data
		OutAudio.AddZeroed(NumSamples * sizeof(int16));
		int16* OutAudioBuffer = (int16*)OutAudio.GetData();
		for (int32 i = 0; i < NumSamples; ++i)
		{
			OutAudioBuffer[i] = (int16)(32767.0f * FMath::Clamp(FloatBufferDataPtr[i], -1.0f, 1.0f));
		}
		return NumSamplesGenerated;
	}

	return NumSamples;
}

void USynthSound::OnEndGenerate()
{
	check(OwningSynthComponent);
	OwningSynthComponent->OnEndGenerate();
}

Audio::EAudioMixerStreamDataFormat::Type USynthSound::GetGeneratedPCMDataFormat() const
{
	// Only audio mixer supports return float buffers
	return bAudioMixer ? Audio::EAudioMixerStreamDataFormat::Float : Audio::EAudioMixerStreamDataFormat::Int16;
}

USynthComponent::USynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAutoActivate = false;

	bStopWhenOwnerDestroyed = true;

	bNeverNeedsRenderUpdate = true;
	bUseAttachParentBound = true; // Avoid CalcBounds() when transform changes.

	bIsSynthPlaying = false;
	bIsInitialized = false;
	bIsUISound = false;

	Synth = nullptr;

	// Set the default sound class
	SoundClass = USoundBase::DefaultSoundClassObject;
	Synth = nullptr;

	PreferredBufferLength = DEFAULT_PROCEDURAL_SOUNDWAVE_BUFFER_SIZE;

#if WITH_EDITORONLY_DATA
	bVisualizeComponent = false;
#endif
}

void USynthComponent::OnAudioComponentEnvelopeValue(const UAudioComponent* InAudioComponent, const USoundWave* SoundWave, const float EnvelopeValue)
{
	if (OnAudioEnvelopeValue.IsBound())
	{
		OnAudioEnvelopeValue.Broadcast(EnvelopeValue);
	}

	if (OnAudioEnvelopeValueNative.IsBound())
	{
		OnAudioEnvelopeValueNative.Broadcast(InAudioComponent, EnvelopeValue);
	}
}

void USynthComponent::Activate(bool bReset)
{
	if (bReset || ShouldActivate())
	{
		Start();
		if (bIsActive)
		{
			OnComponentActivated.Broadcast(this, bReset);
		}
	}
}

void USynthComponent::Deactivate()
{
	if (ShouldActivate() == false)
	{
		Stop();

		if (!bIsActive)
		{
			OnComponentDeactivated.Broadcast(this);
		}
	}
}

void USynthComponent::Initialize(int32 SampleRateOverride)
{
	// This will try to create the audio component if it hasn't yet been created
	CreateAudioComponent();

	// Try to get a proper sample rate
	int32 SampleRate = SampleRateOverride;
	if (SampleRate == INDEX_NONE)
	{
		// Check audio device if we've not explicitly been told what sample rate to use
		FAudioDevice* AudioDevice = GetAudioDevice();
		if (AudioDevice)
		{
			SampleRate = AudioDevice->SampleRate;
		}
	}

	// Only allow initialization if we have a proper sample rate
	if (SampleRate != INDEX_NONE)
	{
#if SYNTH_GENERATOR_TEST_TONE
		NumChannels = 2;
		TestSineLeft.Init(SampleRate, 440.0f, 0.5f);
		TestSineRight.Init(SampleRate, 220.0f, 0.5f);
#else
		// Initialize the synth component
		Init(SampleRate);

		if (NumChannels < 0 || NumChannels > 2)
		{
			UE_LOG(LogAudioMixer, Error, TEXT("Synthesis component '%s' has set an invalid channel count '%d' (only mono and stereo currently supported)."), *GetName(), NumChannels);
		}

		NumChannels = FMath::Clamp(NumChannels, 1, 2);
#endif

		if (!Synth)
		{
			Synth = NewObject<USynthSound>(this, TEXT("Synth"));
		}

		// Copy sound base data to the sound
		Synth->SourceEffectChain = SourceEffectChain;
		Synth->SoundSubmixObject = SoundSubmix;
		Synth->SoundSubmixSends = SoundSubmixSends;
		Synth->BusSends = BusSends;
		Synth->PreEffectBusSends = PreEffectBusSends;
		Synth->bOutputToBusOnly = bOutputToBusOnly;

		Synth->Init(this, NumChannels, SampleRate, PreferredBufferLength);

		if (FAudioDevice* AudioDevice = AudioComponent->GetAudioDevice())
		{
			Synth->StartOnAudioDevice(AudioDevice);
		}
	}
}

UAudioComponent* USynthComponent::GetAudioComponent()
{
	return AudioComponent;
}

void USynthComponent::CreateAudioComponent()
{
	if (!AudioComponent)
	{
		// Create the audio component which will be used to play the procedural sound wave
		AudioComponent = NewObject<UAudioComponent>(this);

	}

	if (AudioComponent)
	{
		AudioComponent->bAutoActivate = false;
		AudioComponent->bStopWhenOwnerDestroyed = true;
		AudioComponent->bShouldRemainActiveIfDropped = true;
		AudioComponent->Mobility = EComponentMobility::Movable;
		AudioComponent->Modulation = Modulation;

#if WITH_EDITORONLY_DATA
		AudioComponent->bVisualizeComponent = false;
#endif
		if (!AudioComponent->GetAttachParent() && !AudioComponent->IsAttachedTo(this))
		{
			if (!GetOwner() || !GetOwner()->GetWorld())
			{
				AudioComponent->SetupAttachment(this);
			}
			else
			{
				AudioComponent->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
				AudioComponent->RegisterComponent();
			}
		}

		AudioComponent->OnAudioSingleEnvelopeValueNative.AddUObject(this, &USynthComponent::OnAudioComponentEnvelopeValue);

		// Set defaults to be the same as audio component defaults
		AudioComponent->EnvelopeFollowerAttackTime = EnvelopeFollowerAttackTime;
		AudioComponent->EnvelopeFollowerReleaseTime = EnvelopeFollowerReleaseTime;
	}
}

void USynthComponent::OnRegister()
{
	CreateAudioComponent();

	Super::OnRegister();
}

void USynthComponent::OnUnregister()
{
	// Route OnUnregister event.
	Super::OnUnregister();

	// Don't stop audio and clean up component if owner has been destroyed (default behaviour). This function gets
	// called from AActor::ClearComponents when an actor gets destroyed which is not usually what we want for one-
	// shot sounds.
	AActor* Owner = GetOwner();
	if (!Owner || bStopWhenOwnerDestroyed)
	{
		Stop();
	}

	// Make sure the audio component is destroyed during unregister
	if (AudioComponent)
	{
		if (Owner && Owner->GetWorld())
		{
			AudioComponent->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
			AudioComponent->UnregisterComponent();
		}
		AudioComponent->DestroyComponent();
		AudioComponent = nullptr;
	}
}

bool USynthComponent::IsReadyForOwnerToAutoDestroy() const
{
	const bool bIsAudioComponentReadyForDestroy = !AudioComponent || (AudioComponent && !AudioComponent->IsPlaying());
	const bool bIsSynthSoundReadyForDestroy = !Synth || !Synth->IsGeneratingAudio();
	return bIsAudioComponentReadyForDestroy && bIsSynthSoundReadyForDestroy;
}

#if WITH_EDITOR
void USynthComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (bIsActive)
	{
		// If this is an auto destroy component we need to prevent it from being auto-destroyed since we're really just restarting it
		const bool bWasAutoDestroy = bAutoDestroy;
		bAutoDestroy = false;
		Stop();
		bAutoDestroy = bWasAutoDestroy;
		Start();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void USynthComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

#if WITH_EDITORONLY_DATA
	if (Ar.IsLoading())
	{
		if (ConcurrencySettings_DEPRECATED != nullptr)
		{
			ConcurrencySet.Add(ConcurrencySettings_DEPRECATED);
			ConcurrencySettings_DEPRECATED = nullptr;
		}
	}
#endif // WITH_EDITORONLY_DATA
}

void USynthComponent::PumpPendingMessages()
{
	TFunction<void()> Command;
	while (CommandQueue.Dequeue(Command))
	{
		Command();
	}

	ESynthEvent SynthEvent;
	while (PendingSynthEvents.Dequeue(SynthEvent))
	{
		switch (SynthEvent)
		{
			case ESynthEvent::Start:
				bIsSynthPlaying = true;
				OnStart();
				break;

			case ESynthEvent::Stop:
				bIsSynthPlaying = false;
				OnStop();
				break;

			default:
				break;
		}
	}
}

int32 USynthComponent::OnGeneratePCMAudio(float* GeneratedPCMData, int32 NumSamples)
{
	PumpPendingMessages();

	check(NumSamples > 0);

	// Only call into the synth if we're actually playing, otherwise, we'll write out zero's
	if (bIsSynthPlaying)
	{
		return OnGenerateAudio(GeneratedPCMData, NumSamples);
	}
	return NumSamples;
}

void USynthComponent::Start()
{
	// Only need to start if we're not already active
	if (bIsActive)
	{
		return;
	}

	// We will also ensure that this synth was initialized before attempting to play.
	Initialize();

	// If there is no Synth USoundBase, we can't start. This can happen if start is called in a cook, a server, or
	// if the audio engine is set to "noaudio".
	// TODO: investigate if this should be handled elsewhere before this point
	if (!Synth)
	{
		return;
	}

	if (AudioComponent)
	{
		// Copy the attenuation and concurrency data from the synth component to the audio component
		AudioComponent->AttenuationSettings = AttenuationSettings;
		AudioComponent->bOverrideAttenuation = bOverrideAttenuation;
		AudioComponent->bIsUISound = bIsUISound;
		AudioComponent->bIsPreviewSound = bIsPreviewSound;
		AudioComponent->bAllowSpatialization = bAllowSpatialization;
		AudioComponent->ConcurrencySet = ConcurrencySet;
		AudioComponent->AttenuationOverrides = AttenuationOverrides;
		AudioComponent->SoundClassOverride = SoundClass;
		AudioComponent->EnvelopeFollowerAttackTime = EnvelopeFollowerAttackTime;
		AudioComponent->EnvelopeFollowerReleaseTime = EnvelopeFollowerReleaseTime;

		// Copy sound base data to the sound
		Synth->AttenuationSettings = AttenuationSettings;
		Synth->SourceEffectChain = SourceEffectChain;
		Synth->SoundSubmixObject = SoundSubmix;
		Synth->SoundSubmixSends = SoundSubmixSends;

		// Set the audio component's sound to be our procedural sound wave
		AudioComponent->SetSound(Synth);
		AudioComponent->Play(0);

		// Copy sound base data to the sound
		Synth->SourceEffectChain = SourceEffectChain;
		Synth->SoundSubmixObject = SoundSubmix;
		Synth->SoundSubmixSends = SoundSubmixSends;

		bIsActive = AudioComponent->IsActive();

		if (bIsActive)
		{
			PendingSynthEvents.Enqueue(ESynthEvent::Start);
		}
	}
}

void USynthComponent::Stop()
{
	if (bIsActive)
	{
		PendingSynthEvents.Enqueue(ESynthEvent::Stop);

		if (AudioComponent)
		{
			AudioComponent->Stop();
		}

		bIsActive = false;
	}
}

bool USynthComponent::IsPlaying() const
{
	return AudioComponent && AudioComponent->IsPlaying();
}

void USynthComponent::SetVolumeMultiplier(float VolumeMultiplier)
{
	if (AudioComponent)
	{
		AudioComponent->SetVolumeMultiplier(VolumeMultiplier);
	}
}

void USynthComponent::SetSubmixSend(USoundSubmix* Submix, float SendLevel)
{
	if (AudioComponent)
	{
		AudioComponent->SetSubmixSend(Submix, SendLevel);
	}
}

void USynthComponent::SynthCommand(TFunction<void()> Command)
{
	CommandQueue.Enqueue(MoveTemp(Command));
}