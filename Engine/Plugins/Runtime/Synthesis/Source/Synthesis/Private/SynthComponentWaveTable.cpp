// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "SynthComponents/SynthComponentWaveTable.h"
#include "AudioDecompress.h"
#include "AudioDevice.h"
#include "DSP/SampleRateConverter.h"
#include "UObject/Package.h"

USynthSamplePlayer::USynthSamplePlayer(const FObjectInitializer& ObjInitializer)
	: Super(ObjInitializer)
	, SoundWave(nullptr)
	, SampleDurationSec(0.0f)
	, SamplePlaybackProgressSec(0.0F)
{
	PrimaryComponentTick.bCanEverTick = true;
}

USynthSamplePlayer::~USynthSamplePlayer()
{
}

bool USynthSamplePlayer::Init(int32& SampleRate)
{
	NumChannels = 2;

	SampleBufferReader.Init(SampleRate);
	SoundWaveLoader.Init(GetAudioDevice());

	if (SoundWave != nullptr)
	{
		SoundWaveLoader.LoadSoundWave(SoundWave);
	}

	return true;
}

void USynthSamplePlayer::SetPitch(float InPitch, float InTimeSec)
{
	SynthCommand([this, InPitch, InTimeSec]()
	{
		SampleBufferReader.SetPitch(InPitch, InTimeSec);
	});
}

void USynthSamplePlayer::SeekToTime(float InTimeSecs, ESamplePlayerSeekType InSeekType, bool bWrap)
{
	Audio::ESeekType::Type SeekType;
	switch (InSeekType)
	{
		default:
		case ESamplePlayerSeekType::FromBeginning:
			SeekType = Audio::ESeekType::FromBeginning;
			break;

		case ESamplePlayerSeekType::FromCurrentPosition:
			SeekType = Audio::ESeekType::FromCurrentPosition;
			break;

		case ESamplePlayerSeekType::FromEnd:
			SeekType = Audio::ESeekType::FromEnd;
			break;
	}

	SynthCommand([this, InTimeSecs, SeekType, bWrap]()
	{
		SampleBufferReader.SeekTime(InTimeSecs, SeekType, bWrap);
	});
}

void USynthSamplePlayer::SetScrubMode(bool bScrubMode)
{
	SynthCommand([this, bScrubMode]()
	{
		SampleBufferReader.SetScrubMode(bScrubMode);
	});
}

void USynthSamplePlayer::SetScrubTimeWidth(float InScrubTimeWidthSec)
{
	SynthCommand([this, InScrubTimeWidthSec]()
	{
		SampleBufferReader.SetScrubTimeWidth(InScrubTimeWidthSec);
	});
}

float USynthSamplePlayer::GetSampleDuration() const
{
	return SampleDurationSec;
}

bool USynthSamplePlayer::IsLoaded() const
{
	return SoundWaveLoader.IsSoundWaveLoaded();
}

float USynthSamplePlayer::GetCurrentPlaybackProgressTime() const
{
	return SamplePlaybackProgressSec;
}

float USynthSamplePlayer::GetCurrentPlaybackProgressPercent() const
{
	if (SampleDurationSec > 0.0f)
	{
		return SamplePlaybackProgressSec / SampleDurationSec;
	}
	return 0.0f;
}

void USynthSamplePlayer::SetSoundWave(USoundWave* InSoundWave)
{
	SoundWaveLoader.LoadSoundWave(InSoundWave);

	SynthCommand([this]()
	{
		SampleBufferReader.ClearBuffer();
	});
}

void USynthSamplePlayer::OnRegister()
{
	Super::OnRegister();

	SetComponentTickEnabled(true);
	RegisterComponent();
}

void USynthSamplePlayer::OnUnregister()
{
	Super::OnUnregister();
}

void USynthSamplePlayer::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	if (SoundWaveLoader.Update())
	{
		OnSampleLoaded.Broadcast();

		Audio::FSampleBuffer NewSampleBuffer;
		SoundWaveLoader.GetSampleBuffer(NewSampleBuffer);

		SynthCommand([this, NewSampleBuffer]()
		{
			SampleBuffer = NewSampleBuffer;
			SampleBufferReader.ClearBuffer();

			// Clear the pending sound waves queue since we've now loaded a new buffer of data
			SoundWaveLoader.Reset();
		});
	}

	OnSamplePlaybackProgress.Broadcast(GetCurrentPlaybackProgressTime(), GetCurrentPlaybackProgressPercent());
}

int32 USynthSamplePlayer::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	if (SampleBuffer.GetData() && !SampleBufferReader.HasBuffer())
	{
		const int16* BufferData = SampleBuffer.GetData();
		const int32 BufferNumSamples = SampleBuffer.GetNumSamples();
		const int32 BufferNumChannels = SampleBuffer.GetNumChannels();
		const int32 BufferSampleRate = SampleBuffer.GetSampleRate();
		SampleBufferReader.SetBuffer(BufferData, BufferNumSamples, BufferNumChannels, BufferSampleRate);
		SampleDurationSec = BufferNumSamples / (BufferSampleRate * BufferNumChannels);
	}

	if (SampleBufferReader.HasBuffer())
	{
		const int32 NumFrames = NumSamples / NumChannels;
		SampleBufferReader.Generate(OutAudio, NumFrames, NumChannels, true);
		SamplePlaybackProgressSec = SampleBufferReader.GetPlaybackProgress();
	}
	else
	{
		for (int32 Sample = 0; Sample < NumSamples; ++Sample)
		{
			OutAudio[Sample] = 0.0f;
		}
	}
	return NumSamples;
}
