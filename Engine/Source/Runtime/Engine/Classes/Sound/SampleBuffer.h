// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "DSP/Dsp.h"
#include "Misc/Paths.h"
#include "Sound/SoundEffectBase.h"
#include "Async/AsyncWork.h"
#include "Templates/UnrealTypeTraits.h"
#include "UObject/GCObject.h"
#include "Tickable.h"

class USoundWave;
class FAudioDevice;

namespace Audio
{
	typedef int16 DefaultUSoundWaveSampleType;
	
	/************************************************************************/
	/* TSampleBuffer<class SampleType>                                      */
	/* This class owns an audio buffer.                                     */
	/* To convert between fixed Q15 buffers and float buffers,              */
	/* Use the assignment operator. Example:                                */
	/*                                                                      */
	/* TSampleBuffer<float> AFloatBuffer;                                   */
	/* TSampleBuffer<int16> AnIntBuffer = AFloatBuffer;                     */
	/************************************************************************/
	template <class SampleType = DefaultUSoundWaveSampleType>
	class TSampleBuffer
	{
	private:
		// raw PCM data buffer
		TArray<SampleType> RawPCMData;
		// The number of samples in the buffer
		int32 NumSamples;
		// The number of frames in the buffer
		int32 NumFrames;
		// The number of channels in the buffer
		int32 NumChannels;
		// The sample rate of the buffer	
		int32 SampleRate;
		// The duration of the buffer in seconds
		float SampleDuration;

	public:
		// Ensure that we can trivially copy construct private members between templated TSampleBuffers:
		template <class> friend class TSampleBuffer;

		FORCEINLINE TSampleBuffer()
			: NumSamples(0)
			, NumFrames(0)
			, NumChannels(0)
			, SampleRate(0)
			, SampleDuration(0.0f)
		{}

		FORCEINLINE TSampleBuffer(const TSampleBuffer& Other)
		{
			NumSamples = Other.NumSamples;
			NumFrames = Other.NumFrames;
			NumChannels = Other.NumChannels;
			SampleRate = Other.SampleRate;
			SampleDuration = Other.SampleDuration;

			RawPCMData.Reset(NumSamples);
			RawPCMData.AddUninitialized(NumSamples);
			FMemory::Memcpy(RawPCMData.GetData(), Other.RawPCMData.GetData(), NumSamples * sizeof(SampleType));
		}

		FORCEINLINE TSampleBuffer(TSampleBuffer&& Other)
		{
			NumSamples = Other.NumSamples;
			NumFrames = Other.NumFrames;
			NumChannels = Other.NumChannels;
			SampleRate = Other.SampleRate;
			SampleDuration = Other.SampleDuration;

			RawPCMData = MoveTemp(Other.RawPCMData);
		}

		FORCEINLINE TSampleBuffer(AlignedFloatBuffer& InData, int32 InNumChannels, int32 InSampleRate)
		{
			*this =  TSampleBuffer(InData.GetData(), InData.Num(), InNumChannels, InSampleRate);
		}

		FORCEINLINE TSampleBuffer(const float* InBufferPtr, int32 InNumSamples, int32 InNumChannels, int32 InSampleRate)
		{
			NumSamples = InNumSamples;
			NumFrames = NumSamples / InNumChannels;
			NumChannels = InNumChannels;
			SampleRate = InSampleRate;
			SampleDuration = ((float)NumFrames) / SampleRate;

			RawPCMData.Reset(NumSamples);
			RawPCMData.AddUninitialized(NumSamples);
		
			if (TIsSame<SampleType, float>::Value)
			{
				FMemory::Memcpy(RawPCMData.GetData(), InBufferPtr, NumSamples * sizeof(float));
			}
			else if (TIsSame<SampleType, int16>::Value)
			{
				// Convert from float to int:
				for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
				{
					RawPCMData[SampleIndex] = (int16)(InBufferPtr[SampleIndex] * 32767.0f);
				}
			}
			else
			{
				// for any other types, we don't know how to explicitly convert, so we fall back to casts:
				for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
				{
					RawPCMData[SampleIndex] = (SampleType)(InBufferPtr[SampleIndex]);
				}
			}
		}

		FORCEINLINE TSampleBuffer(const int16* InBufferPtr, int32 InNumSamples, int32 InNumChannels, int32 InSampleRate)
		{
			NumSamples = InNumSamples;
			NumFrames = NumSamples / InNumChannels;
			NumChannels = InNumChannels;
			SampleRate = InSampleRate;
			SampleDuration = ((float)NumFrames) / SampleRate;

			RawPCMData.Reset(NumSamples);
			RawPCMData.AddUninitialized(NumSamples);

			if (TIsSame<SampleType, int16>::Value)
			{
				FMemory::Memcpy(RawPCMData.GetData(), InBufferPtr, NumSamples * sizeof(int16));
			}
			else if (TIsSame<SampleType, float>::Value)
			{
				// Convert from int to float:
				for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
				{
					RawPCMData[SampleIndex] = ((float)InBufferPtr[SampleIndex]) / 32767.0f;
				}
			}
			else
			{
				// for any other types, we don't know how to explicitly convert, so we fall back to casts:
				for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
				{
					RawPCMData[SampleIndex] = (SampleType)(InBufferPtr[SampleIndex]);
				}
			}
		}

		// Vanilla assignment operator:
		TSampleBuffer& operator=(const TSampleBuffer& Other)
		{
			NumSamples = Other.NumSamples;
			NumFrames = Other.NumFrames;
			NumChannels = Other.NumChannels;
			SampleRate = Other.SampleRate;
			SampleDuration = Other.SampleDuration;

			RawPCMData.Reset(NumSamples);
			RawPCMData.AddUninitialized(NumSamples);

			FMemory::Memcpy(RawPCMData.GetData(), Other.RawPCMData.GetData(), NumSamples * sizeof(SampleType));

			return *this;
		}

		// Move assignment operator:
		TSampleBuffer& operator=(TSampleBuffer&& Other)
		{
			NumSamples = Other.NumSamples;
			NumFrames = Other.NumFrames;
			NumChannels = Other.NumChannels;
			SampleRate = Other.SampleRate;
			SampleDuration = Other.SampleDuration;

			RawPCMData = MoveTemp(Other.RawPCMData);
			return *this;
		}

		//SampleType converting assignment operator:
		template<class OtherSampleType>
		TSampleBuffer& operator=(const TSampleBuffer<OtherSampleType>& Other)
		{
			NumSamples = Other.NumSamples;
			NumFrames = Other.NumFrames;
			NumChannels = Other.NumChannels;
			SampleRate = Other.SampleRate;
			SampleDuration = Other.SampleDuration;

			RawPCMData.Reset(NumSamples);
			RawPCMData.AddUninitialized(NumSamples);

			if (TIsSame<SampleType, OtherSampleType>::Value)
			{
				// If buffers are of the same type, copy over:
				FMemory::Memcpy(RawPCMData.GetData(), Other.RawPCMData.GetData(), NumSamples * sizeof(SampleType));
			}
			else if (TIsSame<SampleType, int16>::Value && TIsSame<OtherSampleType, float>::Value)
			{
				// Convert from float to int:
				for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
				{
					RawPCMData[SampleIndex] = (int16)(Other.RawPCMData[SampleIndex] * 32767.0f);
				}
			}
			else if (TIsSame<SampleType, float>::Value && TIsSame<OtherSampleType, int16>::Value)
			{
				// Convert from int to float:
				for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
				{
					RawPCMData[SampleIndex] = ((float)Other.RawPCMData[SampleIndex]) / 32767.0f;
				}
			}
			else
			{
				// for any other types, we don't know how to explicitly convert, so we fall back to casts:
				for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
				{
					RawPCMData[SampleIndex] = Other.RawPCMData[SampleIndex];
				}
			}

			return *this;
		}

		// copy from a container of the same element type
		void CopyFrom(const TArray<SampleType>& InArray, int32 InNumChannels, int32 InSampleRate)
		{
			NumSamples = InArray.Num();
			NumFrames = NumSamples / InNumChannels;
			NumChannels = InNumChannels;
			SampleRate = InSampleRate;
			SampleDuration = ((float)NumFrames) / SampleRate;

			RawPCMData.Reset(NumSamples);
			RawPCMData.AddZeroed(NumSamples);

			FMemory::Memcpy(RawPCMData.GetData(), InArray.GetData(), NumSamples * sizeof(SampleType));
		}

		// Append audio data to internal buffer of different sample type of this sample buffer
		template<class OtherSampleType>
		void Append(const OtherSampleType* InputBuffer, int32 InNumSamples)
		{
			int32 StartIndex = RawPCMData.AddUninitialized(InNumSamples);

			if (TIsSame<SampleType, OtherSampleType>::Value)
			{
				FMemory::Memcpy(&RawPCMData[StartIndex], InputBuffer, InNumSamples * sizeof(SampleType));
			}
			else
			{
				if (TIsSame<SampleType, int16>::Value && TIsSame<OtherSampleType, float>::Value)
				{
					// Convert from float to int:
					for (int32 SampleIndex = 0; SampleIndex < InNumSamples; SampleIndex++)
					{
						RawPCMData[StartIndex + SampleIndex] = (int16)(InputBuffer[SampleIndex] * 32767.0f);
					}
				}
				else if (TIsSame<SampleType, float>::Value && TIsSame<OtherSampleType, int16>::Value)
				{
					// Convert from int to float:
					for (int32 SampleIndex = 0; SampleIndex < InNumSamples; SampleIndex++)
					{
						RawPCMData[StartIndex + SampleIndex] = (float)InputBuffer[SampleIndex] / 32767.0f;
					}
				}
				else
				{
					// for any other types, we don't know how to explicitly convert, so we fall back to casts:
					for (int32 SampleIndex = 0; SampleIndex < InNumSamples; SampleIndex++)
					{
						RawPCMData[StartIndex + SampleIndex] = InputBuffer[SampleIndex];
					}
				}
			}

			// Update meta-data
			NumSamples += InNumSamples;
			NumFrames = NumSamples / NumChannels;
			SampleDuration = (float)NumFrames / SampleRate;
		}

		~TSampleBuffer() {};

		// Gets the raw PCM data of the sound wave
		FORCEINLINE const SampleType* GetData() const
		{
			return RawPCMData.GetData();
		}

		FORCEINLINE TArrayView<SampleType> GetArrayView()
		{
			return MakeArrayView(RawPCMData);
		}

		FORCEINLINE TArrayView<const SampleType> GetArrayView() const
		{
			return MakeArrayView(RawPCMData);
		}

		// Gets the number of samples of the sound wave
		FORCEINLINE int32 GetNumSamples() const
		{
			return NumSamples;
		}

		// Gets the number of frames of the sound wave
		FORCEINLINE int32 GetNumFrames() const
		{
			return NumFrames;
		}

		// Gets the number of channels of the sound wave
		FORCEINLINE int32 GetNumChannels() const
		{
			return NumChannels;
		}

		// Gets the sample rate of the sound wave
		FORCEINLINE int32 GetSampleRate() const
		{
			return SampleRate;
		}

		// Gets the sample duration (in seconds) of the sound
		FORCEINLINE float GetSampleDuration() const
		{
			return SampleDuration;
		}

		void MixBufferToChannels(int32 InNumChannels)
		{
			if (!RawPCMData.Num() || InNumChannels <= 0)
			{
				return;
			}

			TUniquePtr<SampleType[]> TempBuffer;
			TempBuffer.Reset(new SampleType[InNumChannels * NumFrames]);
			FMemory::Memset(TempBuffer.Get(), 0, InNumChannels * NumFrames * sizeof(SampleType));

			const SampleType* SrcBuffer = GetData();

			// Downmixing using the channel modulo assumption:
			// TODO: Use channel matrix for channel conversions.
			for (int32 FrameIndex = 0; FrameIndex < NumFrames; FrameIndex++)
			{
				for (int32 ChannelIndex = 0; ChannelIndex < NumChannels; ChannelIndex++)
				{
					const int32 DstSampleIndex = FrameIndex * InNumChannels + (ChannelIndex % InNumChannels);
					const int32 SrcSampleIndex = FrameIndex * NumChannels + ChannelIndex;

					TempBuffer[DstSampleIndex] += SrcBuffer[SrcSampleIndex];
				}
			}

			NumChannels = InNumChannels;
			NumSamples = NumFrames * NumChannels;

			// Resize our buffer and copy the result in:
			RawPCMData.Reset(NumSamples);
			RawPCMData.AddUninitialized(NumSamples);

			FMemory::Memcpy(RawPCMData.GetData(), TempBuffer.Get(), NumSamples * sizeof(SampleType));
		}

		void Clamp(float Ceiling = 1.0f)
		{
			if (TIsSame<SampleType, float>::Value)
			{
				// Float case:
				float ClampMin = Ceiling * -1.0f;

				for (int32 SampleIndex = 0; SampleIndex < RawPCMData.Num(); SampleIndex++)
				{
					RawPCMData[SampleIndex] = FMath::Clamp<float>(RawPCMData[SampleIndex], ClampMin, Ceiling);
				}
			}
			else if (TIsSame<SampleType, int16>::Value)
			{
				// int16 case:
				Ceiling = FMath::Clamp(Ceiling, 0.0f, 1.0f);

				int16 ClampMax = Ceiling * 32767.0f;
				int16 ClampMin = Ceiling * -32767.0f;

				for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
				{
					RawPCMData[SampleIndex] = FMath::Clamp<int16>(RawPCMData[SampleIndex], ClampMin, ClampMax);
				}
			}
			else
			{
				// Unknown type case:
				float ClampMin = Ceiling * -1.0f;

				for (int32 SampleIndex = 0; SampleIndex < RawPCMData.Num(); SampleIndex++)
				{
					RawPCMData[SampleIndex] = FMath::Clamp<SampleType>(RawPCMData[SampleIndex], ClampMin, Ceiling);
				}
			}
		}

		/**
		 * Appends zeroes to the end of this buffer.
		 * If called with no arguments or NumFramesToAppend = 0, this will ZeroPad
		 */
		void ZeroPad(int32 NumFramesToAppend = 0)
		{
			if (!NumFramesToAppend)
			{
				NumFramesToAppend = FMath::RoundUpToPowerOfTwo(NumFrames) - NumFrames;
			}

			RawPCMData.AddZeroed(NumFramesToAppend * NumChannels);
			NumFrames += NumFramesToAppend;
			NumSamples = NumFrames * NumChannels;
		}

		void SetNumFrames(int32 InNumFrames)
		{
			RawPCMData.SetNum(InNumFrames * NumChannels);
			NumFrames = RawPCMData.Num() / NumChannels;
			NumSamples = RawPCMData.Num();
		}

		// InIndex [0.0f, NumSamples - 1.0f]
		// OutFrame is the multichannel output for one index value
		// Returns InIndex wrapped between 0.0 and NumFrames
		float GetAudioFrameAtFractionalIndex(float InIndex, TArray<SampleType>& OutFrame)
		{
			InIndex = FMath::Fmod(InIndex, static_cast<float>(NumFrames));

			GetAudioFrameAtFractionalIndexInternal(InIndex, OutFrame);

			return InIndex;
		}

		// InPhase [0, 1], wrapped, through duration of file (ignores sample rate)
		// OutFrame is the multichannel output for one phase value
		// Returns InPhase wrapped between 0.0 and 1.0
		float GetAudioFrameAtPhase(float InPhase, TArray<SampleType>& OutFrame)
		{
			InPhase = FMath::Fmod(InPhase, 1.0f);

			GetAudioFrameAtFractionalIndexInternal(InPhase * NumFrames, OutFrame);

			return InPhase;
		}


		// InTimeSec, get the value of the buffer at the given time (uses sample rate)
		// OutFrame is the multichannel output for one time value
		// Returns InTimeSec wrapped between 0.0 and (NumSamples / SampleRate)
		float GetAudioFrameAtTime(float InTimeSec, TArray<SampleType>& OutFrame)
		{
			if (InTimeSec >= SampleDuration)
			{
				InTimeSec -= SampleDuration;
			}

			check(InTimeSec >= 0.0f && InTimeSec <= SampleDuration);

			GetAudioFrameAtFractionalIndexInternal(NumSamples * (InTimeSec / SampleDuration), OutFrame);

			return InTimeSec;
		}

	private:
		// Internal implementation. Called by all public GetAudioFrameAt_ _ _ _() functions
		// public functions do range checking/wrapping and then call this function
		void GetAudioFrameAtFractionalIndexInternal(float InIndex, TArray<SampleType>& OutFrame)
		{
			const float Alpha = FMath::Fmod(InIndex, 1.0f);
			const int32 WholeThisIndex = FMath::FloorToInt(InIndex);
			int32 WholeNextIndex = WholeThisIndex + 1;

			// check for interpolation between last and first frames
			if (WholeNextIndex == NumSamples)
			{
				WholeNextIndex = 0;
			}

			// TODO: if(NumChannels < 4)... do the current (non vectorized) way
			OutFrame.SetNumUninitialized(NumChannels);

			for (int32 i = 0; i < NumChannels; ++i)
			{
				float SampleA, SampleB;
				
				if (TIsSame<SampleType, float>::Value)
				{
					SampleA = RawPCMData[i * NumChannels + WholeThisIndex];
					SampleB = RawPCMData[i * NumChannels + WholeNextIndex];
					OutFrame[i] = FMath::Lerp(SampleA, SampleB, Alpha);
				}
				else
				{
					SampleA = static_cast<float>(RawPCMData[i * NumChannels + WholeThisIndex]);
					SampleB = static_cast<float>(RawPCMData[i * NumChannels + WholeNextIndex]);
					OutFrame[i] = static_cast<SampleType>(FMath::Lerp(SampleA, SampleB, Alpha));
				}
			}

			// TODO: else { do vectorized version }
			// make new function in BufferVectorOperations.cpp
			// (use FMath::Lerp() overload for VectorRegisters)
		}
	};

	// FSampleBuffer is a strictly defined TSampleBuffer that uses the same sample format we use for USoundWaves.
	typedef TSampleBuffer<> FSampleBuffer;

	/************************************************************************/
	/* FSoundWavePCMLoader                                                  */
	/* This class loads and decodes a USoundWave asset into a TSampleBuffer.*/
	/* To use, call LoadSoundWave with the sound wave you'd like to load    */
	/* and call Update on every tick until it returns true, at which point  */
	/* you may call GetSampleBuffer to get the decoded audio.               */
	/************************************************************************/
	class ENGINE_API FSoundWavePCMLoader : public FGCObject
	{
	public:
		FSoundWavePCMLoader();

		// Loads a USoundWave, call on game thread.
		void LoadSoundWave(USoundWave* InSoundWave, TFunction<void(const USoundWave* SoundWave, const Audio::FSampleBuffer& OutSampleBuffer)> OnLoaded);


		// Update the loading state. 
		void Update();

		//~ GCObject Interface
		virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
		virtual FString GetReferencerName() const override;
		//~ GCObject Interface

	private:

		struct FLoadingSoundWaveInfo
		{
			// The sound wave which is loading PCM data
			USoundWave* SoundWave;

			// The lambda function to call when t he sound wave finishes loading
			TFunction<void(const USoundWave* SoundWave, const Audio::FSampleBuffer& LoadedSampleBuffer)> OnLoaded;


		
			enum class LoadStatus : uint8
			{
				// No request to load has been issued (default)
				None = 0,

				// The sound wave load/decode is in-flight
				Loading,

				// The sound wave has already been loaded
				Loaded,
			};

			LoadStatus Status;

			FLoadingSoundWaveInfo()
				: SoundWave(nullptr)
				, Status(LoadStatus::None)
			{
			}
		};

		// Reference to current loading sound wave
		TArray<FLoadingSoundWaveInfo> LoadingSoundWaves;
		
		// MERGE-REVIEW - should be in object or moved into loading info?
		// Whether or not this object is tickable. I.e. a sound wave has been asked to load.
		bool bCanBeTicked;
	};

	// Enum used to express the current state of a FSoundWavePCMWriter's current operation.
	enum class ESoundWavePCMWriterState : uint8
	{
		Idle,
		Generating,
		WritingToDisk,
		Suceeded,
		Failed,
		Cancelled
	};

	// Enum used internally by the FSoundWavePCMWriter.
	enum class ESoundWavePCMWriteTaskType : uint8
	{
		GenerateSoundWave,
		GenerateAndWriteSoundWave,
		WriteSoundWave,
		WriteWavFile
	};

	/************************************************************************/
	/* FAsyncSoundWavePCMWriteWorker                                        */
	/* This class is used by FSoundWavePCMWriter to handle async writing.   */
	/************************************************************************/
	class ENGINE_API FAsyncSoundWavePCMWriteWorker : public FNonAbandonableTask
	{
	protected:
		class FSoundWavePCMWriter* Writer;
		ESoundWavePCMWriteTaskType TaskType;

		FCriticalSection NonAbandonableSection;

		TFunction<void(const USoundWave*)> CallbackOnSuccess;

	public:
		
		FAsyncSoundWavePCMWriteWorker(FSoundWavePCMWriter* InWriter, ESoundWavePCMWriteTaskType InTaskType, TFunction<void(const USoundWave*)> OnSuccess);
		~FAsyncSoundWavePCMWriteWorker();

		/**
		* Performs write operations async.
		*/
		void DoWork();

		bool CanAbandon() 
		{ 
			return true;
		}

		void Abandon();

		FORCEINLINE TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(FAsyncSoundWavePCMWriteWorker, STATGROUP_ThreadPoolAsyncTasks);
		}
	};

	typedef FAsyncTask<FAsyncSoundWavePCMWriteWorker> FAsyncSoundWavePCMWriterTask;

	// This is the default chunk size, in bytes that FSoundWavePCMWriter writes to the disk at once.
	static const int32 WriterDefaultChunkSize = 8192;


	/************************************************************************/
	/* FSoundWavePCMWriter                                                  */
	/* This class can be used to save a TSampleBuffer to either a wav  file */
	/* or a USoundWave using BeginGeneratingSoundWaveFromBuffer,            */
	/* BeginWriteToSoundWave, or BeginWriteToWavFile on the game thread.    */
	/* This class uses an async task to generate and write the file to disk.*/
	/************************************************************************/
	class ENGINE_API FSoundWavePCMWriter
	{
	public:
		friend class FAsyncSoundWavePCMWriteWorker;

		FSoundWavePCMWriter(int32 InChunkSize = WriterDefaultChunkSize);
		~FSoundWavePCMWriter();

		// This kicks off an operation to write InSampleBuffer to SoundWaveToSaveTo.
		// If InSoundWave is not nullptr, the audio will be written directly into
		// Returns true on a successful start, false otherwise.
		bool BeginGeneratingSoundWaveFromBuffer(const TSampleBuffer<>& InSampleBuffer, USoundWave* InSoundWave = nullptr, TFunction<void(const USoundWave*)> OnSuccess = [](const USoundWave* ResultingWave){});

		// This kicks off an operation to write InSampleBuffer to a USoundWave asset
		// at the specified file path relative to the project directory.
		// This function should only be used in the editor.
		// If a USoundWave asset already exists 
		bool BeginWriteToSoundWave(const FString& FileName, const TSampleBuffer<>& InSampleBuffer, FString InPath, TFunction<void(const USoundWave*)> OnSuccess = [](const USoundWave* ResultingWave) {});
	
		// This writes out the InSampleBuffer as a wav file at the path specified by FilePath and FileName.
		// If FilePath is a relative path, it will be relative to the /Saved/BouncedWavFiles folder, otherwise specified absolute path will be used.
		// FileName should not contain the extension. This can be used in non-editor builds.
		bool BeginWriteToWavFile(const TSampleBuffer<>& InSampleBuffer, const FString& FileName, FString& FilePath, TFunction<void()> OnSuccess = []() {});

		// This is a blocking call that will return the SoundWave generated from InSampleBuffer.
		// Optionally, if you're using the editor, you can also write the resulting soundwave out to the content browser using the FileName and FilePath parameters.
		USoundWave* SynchronouslyWriteSoundWave(const TSampleBuffer<>& InSampleBuffer, const FString* FileName = nullptr, const FString* FilePath = nullptr);

		// Call this on the game thread to continue the write operation. Optionally provide a pointer
		// to an ESoundWavePCMWriterState which will be written to with the current state of the write operation.
		// Returns a float value from 0 to 1 indicating how complete the write operation is.
		float CheckStatus(ESoundWavePCMWriterState* OutCurrentState = nullptr);

		// Aborts the current write operation.
		void CancelWrite();

		// Whether we have finished the write operation, by either succeeding, failing, or being cancelled.
		bool IsDone();

		// Clean up all resources used.
		void Reset();

		// Used to grab the a handle to the soundwave. 
		USoundWave* GetFinishedSoundWave();

		// This function can be used after generating a USoundWave by calling BeginGeneratingSoundWaveFromBuffer
		// to save the generated soundwave to an asset.
		// This is handy if you'd like to preview or edit the USoundWave before saving it to disk.
		void SaveFinishedSoundWaveToPath(const FString& FileName, FString InPath = FPaths::EngineContentDir());

	private:
		// Current pending buffer.
		TSampleBuffer<> CurrentBuffer;

		// Sound wave currently being written to.
		USoundWave* CurrentSoundWave;

		// Current state of the buffer.
		ESoundWavePCMWriterState CurrentState;

		// Current Absolute File Path we are writing to.
		FString AbsoluteFilePath;

		bool bWasPreviouslyAddedToRoot;

		TUniquePtr<FAsyncSoundWavePCMWriterTask> CurrentOperation;

		// Internal buffer for holding the serialized wav file in memory.
		TArray<uint8> SerializedWavData;

		// Internal progress
		FThreadSafeCounter Progress;

		int32 ChunkSize;

		UPackage* CurrentPackage;

	private:

		//  This is used to emplace CurrentBuffer in CurrentSoundWave.
		void ApplyBufferToSoundWave();

		// This is used to save CurrentSoundWave within CurrentPackage.
		void SerializeSoundWaveToAsset();

		// This is used to write a WavFile in disk.
		void SerializeBufferToWavFile();

		// This checks to see if a directory exists and, if it does not, recursively adds the directory.
		bool CreateDirectoryIfNeeded(FString& DirectoryPath);
	};

	/************************************************************************/
	/* FAudioRecordingData                                                  */
	/* This is used by USoundSubmix and the AudioMixerBlueprintLibrary      */
	/* to contain FSoundWavePCMWriter operations.                           */
	/************************************************************************/
	struct FAudioRecordingData
	{
		TSampleBuffer<int16> InputBuffer;
		FSoundWavePCMWriter Writer;

		~FAudioRecordingData() {};
	};

}
