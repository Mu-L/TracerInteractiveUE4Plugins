// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AudioMixerSourceDecode.h"
#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "AudioMixer.h"
#include "Sound/SoundWave.h"
#include "HAL/RunnableThread.h"
#include "AudioMixerBuffer.h"
#include "Async/Async.h"
#include "AudioDecompress.h"

namespace Audio
{

class FAsyncDecodeWorker : public FNonAbandonableTask
{
public:
	FAsyncDecodeWorker(const FHeaderParseAudioTaskData& InTaskData)
		: HeaderParseAudioData(InTaskData)
		, TaskType(EAudioTaskType::Header)
		, bIsDone(false)
	{
	}

	FAsyncDecodeWorker(const FProceduralAudioTaskData& InTaskData)
		: ProceduralTaskData(InTaskData)
		, TaskType(EAudioTaskType::Procedural)
		, bIsDone(false)
	{
	}

	FAsyncDecodeWorker(const FDecodeAudioTaskData& InTaskData)
		: DecodeTaskData(InTaskData)
		, TaskType(EAudioTaskType::Decode)
		, bIsDone(false)
	{
	}

	~FAsyncDecodeWorker()
	{
	}

	void DoWork()
	{
		switch (TaskType)
		{
			case EAudioTaskType::Procedural:
			{
				// Make sure we've been flagged as active
				if (!ProceduralTaskData.ProceduralSoundWave->IsGeneratingAudio())
				{
					// Act as if we generated audio, but return silence.
					FMemory::Memzero(ProceduralTaskData.AudioData, ProceduralTaskData.NumSamples * sizeof(float));
					ProceduralResult.NumSamplesWritten = ProceduralTaskData.NumSamples;
					return;
				}

				// If we're not a float format, we need to convert the format to float
				const EAudioMixerStreamDataFormat::Type FormatType = ProceduralTaskData.ProceduralSoundWave->GetGeneratedPCMDataFormat();
				if (FormatType != EAudioMixerStreamDataFormat::Float)
				{
					check(FormatType == EAudioMixerStreamDataFormat::Int16);

					int32 NumChannels = ProceduralTaskData.NumChannels;
					int32 ByteSize = NumChannels * ProceduralTaskData.NumSamples * sizeof(int16);

					TArray<uint8> DecodeBuffer;
					DecodeBuffer.AddUninitialized(ByteSize);

					const int32 NumBytesWritten = ProceduralTaskData.ProceduralSoundWave->GeneratePCMData(DecodeBuffer.GetData(), ProceduralTaskData.NumSamples);

					check(NumBytesWritten <= ByteSize);

					ProceduralResult.NumSamplesWritten = NumBytesWritten / sizeof(int16);

					// Convert the buffer to float
					int16* DecodedBufferPtr = (int16*)DecodeBuffer.GetData();
					for (int32 SampleIndex = 0; SampleIndex < ProceduralResult.NumSamplesWritten; ++SampleIndex)
					{
						ProceduralTaskData.AudioData[SampleIndex] = (float)(DecodedBufferPtr[SampleIndex]) / 32768.0f;
					}
				}
				else
				{
					const int32 NumBytesWritten = ProceduralTaskData.ProceduralSoundWave->GeneratePCMData((uint8*)ProceduralTaskData.AudioData, ProceduralTaskData.NumSamples);
					ProceduralResult.NumSamplesWritten = NumBytesWritten / sizeof(float);
				}
			}
			break;

			case EAudioTaskType::Header:
			{
				HeaderParseAudioData.MixerBuffer->ReadCompressedInfo(HeaderParseAudioData.SoundWave);
			}
			break;

			case EAudioTaskType::Decode:
			{
				int32 NumChannels = DecodeTaskData.NumChannels;
				int32 ByteSize = NumChannels * DecodeTaskData.NumFramesToDecode * sizeof(int16);

				// Create a buffer to decode into that's of the appropriate size
				TArray<uint8> DecodeBuffer;
				DecodeBuffer.AddUninitialized(ByteSize);

				// skip the first buffers if we've already decoded them during Precache:
				if (DecodeTaskData.bSkipFirstBuffer)
				{
					const int32 kPCMBufferSize = NumChannels * DecodeTaskData.NumPrecacheFrames * sizeof(int16);
					if (DecodeTaskData.BufferType == EBufferType::Streaming)
					{
						for (int32 NumberOfBuffersToSkip = 0; NumberOfBuffersToSkip < PLATFORM_NUM_AUDIODECOMPRESSION_PRECACHE_BUFFERS; NumberOfBuffersToSkip++)
						{
							DecodeTaskData.DecompressionState->StreamCompressedData(DecodeBuffer.GetData(), DecodeTaskData.bLoopingMode, kPCMBufferSize);
						}
					}
					else
					{
						for (int32 NumberOfBuffersToSkip = 0; NumberOfBuffersToSkip < PLATFORM_NUM_AUDIODECOMPRESSION_PRECACHE_BUFFERS; NumberOfBuffersToSkip++)
						{
							DecodeTaskData.DecompressionState->ReadCompressedData(DecodeBuffer.GetData(), DecodeTaskData.bLoopingMode, kPCMBufferSize);
						}
					}
				}

				const int32 kPCMBufferSize = NumChannels * DecodeTaskData.NumFramesToDecode * sizeof(int16);
				if (DecodeTaskData.BufferType == EBufferType::Streaming)
				{
					DecodeResult.bLooped = DecodeTaskData.DecompressionState->StreamCompressedData(DecodeBuffer.GetData(), DecodeTaskData.bLoopingMode, kPCMBufferSize);
				}
				else
				{
					DecodeResult.bLooped = DecodeTaskData.DecompressionState->ReadCompressedData(DecodeBuffer.GetData(), DecodeTaskData.bLoopingMode, kPCMBufferSize);
				}

				// Convert the decoded PCM data into a float buffer while still in the async task
				int32 SampleIndex = 0;
				int16* DecodedBufferPtr = (int16*)DecodeBuffer.GetData();
				for (int32 Frame = 0; Frame < DecodeTaskData.NumFramesToDecode; ++Frame)
				{
					for (int32 Channel = 0; Channel < NumChannels; ++Channel, ++SampleIndex)
					{
						DecodeTaskData.AudioData[SampleIndex] = (float)(DecodedBufferPtr[SampleIndex]) / 32768.0f;
					}
				}
			}
			break;
		}
		bIsDone = true;
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FAsyncDecodeWorker, STATGROUP_ThreadPoolAsyncTasks);
	}

	FHeaderParseAudioTaskData HeaderParseAudioData;
	FDecodeAudioTaskData DecodeTaskData;
	FDecodeAudioTaskResults DecodeResult;
	FProceduralAudioTaskData ProceduralTaskData;
	FProceduralAudioTaskResults ProceduralResult;
	EAudioTaskType TaskType;
	FThreadSafeBool bIsDone;
};

class FDecodeHandleBase : public IAudioTask
{
public:
	FDecodeHandleBase()
		: Task(nullptr)
	{}

	virtual ~FDecodeHandleBase()
	{
		if (Task)
		{
			Task->EnsureCompletion();
			delete Task;
		}
	}

	virtual bool IsDone() const override
	{
		if (Task)
		{
			return Task->IsDone();
		}
		return true;
	}

	virtual void EnsureCompletion() override
	{
		if (Task)
		{
			Task->EnsureCompletion();
		}
	}

	virtual void CancelTask() override
	{
		if (Task)
		{
			// If Cancel returns false, it means we weren't able to cancel. So lets then fallback to ensure complete.
			if (!Task->Cancel())
			{
				Task->EnsureCompletion();
			}
		}
	}

protected:

	FAsyncTask<FAsyncDecodeWorker>* Task;
};

class FHeaderDecodeHandle : public FDecodeHandleBase
{
public:
	FHeaderDecodeHandle(const FHeaderParseAudioTaskData& InJobData)
	{
		Task = new FAsyncTask<FAsyncDecodeWorker>(InJobData);
		Task->StartBackgroundTask();
	}

	virtual EAudioTaskType GetType() const override
	{
		return EAudioTaskType::Header;
	}
};

class FProceduralDecodeHandle : public FDecodeHandleBase
{
public:
	FProceduralDecodeHandle(const FProceduralAudioTaskData& InJobData)
	{
		Task = new FAsyncTask<FAsyncDecodeWorker>(InJobData);
		Task->StartBackgroundTask();
	}

	virtual EAudioTaskType GetType() const override
	{ 
		return EAudioTaskType::Procedural; 
	}

	virtual void GetResult(FProceduralAudioTaskResults& OutResult) override
	{
		Task->EnsureCompletion();
		const FAsyncDecodeWorker& DecodeWorker = Task->GetTask();
		OutResult = DecodeWorker.ProceduralResult;
	}
};

class FDecodeHandle : public FDecodeHandleBase
{
public:
	FDecodeHandle(const FDecodeAudioTaskData& InJobData)
	{
		Task = new FAsyncTask<FAsyncDecodeWorker>(InJobData);
		const bool bUseBackground = ShouldUseBackgroundPoolFor_FAsyncRealtimeAudioTask();
		Task->StartBackgroundTask(bUseBackground ? GBackgroundPriorityThreadPool : GThreadPool);
	}

	virtual EAudioTaskType GetType() const override
	{ 
		return EAudioTaskType::Decode; 
	}

	virtual void GetResult(FDecodeAudioTaskResults& OutResult) override
	{
		Task->EnsureCompletion();
		const FAsyncDecodeWorker& DecodeWorker = Task->GetTask();
		OutResult = DecodeWorker.DecodeResult;
	}
};

IAudioTask* CreateAudioTask(const FProceduralAudioTaskData& InJobData)
{
	return new FProceduralDecodeHandle(InJobData);
}

IAudioTask* CreateAudioTask(const FHeaderParseAudioTaskData& InJobData)
{
	return new FHeaderDecodeHandle(InJobData);
}

IAudioTask* CreateAudioTask(const FDecodeAudioTaskData& InJobData)
{
	return new FDecodeHandle(InJobData);
}

}
