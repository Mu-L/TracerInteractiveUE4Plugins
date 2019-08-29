// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "Sound/SampleBuffer.h"
#include "AudioMixer.h"
#include "HAL/PlatformFilemanager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "AssetRegistryModule.h"
#include "Sound/SoundWave.h"
#include "AudioDevice.h"
#include "Async/Async.h"

namespace Audio
{
	FSoundWavePCMLoader::FSoundWavePCMLoader()
		: AudioDevice(nullptr)
		, SoundWave(nullptr)
		, bIsLoading(false)
		, bIsLoaded(false)
	{
	}

	void FSoundWavePCMLoader::Init(FAudioDevice* InAudioDevice)
	{
		AudioDevice = InAudioDevice;
	}

	void FSoundWavePCMLoader::LoadSoundWave(USoundWave* InSoundWave)
	{
		if (!AudioDevice || !InSoundWave)
		{
			return;
		}

		// Queue existing sound wave reference so it can be
		// cleared when the audio thread gets newly loaded audio data. 
		// Don't want to kill the sound wave PCM data while it's playing on audio thread.
		if (SoundWave)
		{
			PendingStoppingSoundWaves.Enqueue(SoundWave);
		}

		SoundWave = InSoundWave;
		if (!SoundWave->RawPCMData || SoundWave->AudioDecompressor)
		{
			bIsLoaded = false;
			bIsLoading = true;

			if (!SoundWave->RawPCMData)
			{
				// Kick off a decompression/precache of the sound wave
				AudioDevice->Precache(SoundWave, false, true, true);
			}
		}
		else
		{
			bIsLoading = true;
			bIsLoaded = true;
		}
	}

	bool FSoundWavePCMLoader::Update()
	{
		if (bIsLoading)
		{
			check(SoundWave);
	
			if (bIsLoaded || (SoundWave->AudioDecompressor && SoundWave->AudioDecompressor->IsDone()))
			{
				if (SoundWave->AudioDecompressor)
				{
					check(!bIsLoaded);
					delete SoundWave->AudioDecompressor;
					SoundWave->AudioDecompressor = nullptr;
					SoundWave->SetPrecacheState(ESoundWavePrecacheState::Done);
				}

				bIsLoading = false;
				bIsLoaded = true;

				SampleBuffer.RawPCMData.Reset(new int16[SoundWave->RawPCMDataSize]);
				FMemory::Memcpy(SampleBuffer.RawPCMData.Get(), SoundWave->RawPCMData, SoundWave->RawPCMDataSize);
				SampleBuffer.NumSamples = SoundWave->RawPCMDataSize / sizeof(int16);
				SampleBuffer.NumChannels = SoundWave->NumChannels;
				SampleBuffer.NumFrames = SampleBuffer.NumSamples / SoundWave->NumChannels;
				SampleBuffer.SampleRate = SoundWave->GetSampleRateForCurrentPlatform();
				SampleBuffer.SampleDuration = (float)SampleBuffer.NumFrames / SampleBuffer.SampleRate;

				return true;
			}
		}

		return false;
	}

	void FSoundWavePCMLoader::GetSampleBuffer(TSampleBuffer<>& OutSampleBuffer)
	{
		OutSampleBuffer = SampleBuffer;
	}

	void FSoundWavePCMLoader::Reset()
	{
		PendingStoppingSoundWaves.Empty();
	}

	FSoundWavePCMWriter::FSoundWavePCMWriter(int32 InChunkSize)
		: CurrentSoundWave(0)
		, CurrentState(ESoundWavePCMWriterState::Idle)
		, bWasPreviouslyAddedToRoot(false)
		, ChunkSize(InChunkSize)
		, CurrentPackage(nullptr)
	{
	}


	FSoundWavePCMWriter::~FSoundWavePCMWriter()
	{
		Reset();
	}

	bool FSoundWavePCMWriter::BeginGeneratingSoundWaveFromBuffer(const TSampleBuffer<>& InSampleBuffer, USoundWave* SoundWaveToSaveTo, TFunction<void(const USoundWave*)> OnSuccess)
	{
		if (!IsDone())
		{
			UE_LOG(LogAudio, Error, TEXT("This instance of FSoundWavePCMWriter is already processing another write operation."));
			return false;
		}

		CurrentState = ESoundWavePCMWriterState::Generating;

		// If SoundWaveToSaveTo is null, create a new object.
		if (SoundWaveToSaveTo == nullptr)
		{
			CurrentSoundWave = NewObject<USoundWave>();
			CurrentSoundWave->AddToRoot();
		}
		else
		{
			CurrentSoundWave = SoundWaveToSaveTo;
			bWasPreviouslyAddedToRoot = CurrentSoundWave->IsRooted();
			CurrentSoundWave->AddToRoot();
			// Ensure this sound wave is not currently in use:
			FAudioDeviceManager* AudioDeviceManager = GEngine->GetAudioDeviceManager();
			if (AudioDeviceManager)
			{
				AudioDeviceManager->StopSoundsUsingResource(CurrentSoundWave);
			}
		}

		CurrentBuffer = InSampleBuffer;

		// TODO: Support writing to multi-channel sound waves. For now, we mix down sound waves to stereo:
		if (CurrentBuffer.GetNumChannels() > 2)
		{
			CurrentBuffer.MixBufferToChannels(2);
		}

		CurrentOperation.Reset(new FAsyncSoundWavePCMWriterTask(this, ESoundWavePCMWriteTaskType::GenerateAndWriteSoundWave, OnSuccess));
		CurrentOperation->StartBackgroundTask();

		return true;
	}

	bool FSoundWavePCMWriter::BeginWriteToSoundWave(const FString& FileName, const TSampleBuffer<>& InSampleBuffer, FString InPath, TFunction<void(const USoundWave*)> OnSuccess)
	{
		if (!IsDone())
		{
			UE_LOG(LogAudio, Error, TEXT("This instance of FSoundWavePCMWriter is already processing another write operation."));
			return false;
		}

		if (!GIsEditor)
		{
			UE_LOG(LogAudio, Error, TEXT("Writing to a SoundWave is only available in the editor."));
			return false;
		}

		CurrentState = ESoundWavePCMWriterState::Generating;

		FPaths::NormalizeDirectoryName(InPath);

		AbsoluteFilePath = TEXT("/Game/") + InPath + FString(TEXT("/")) + FileName;

		AbsoluteFilePath = AbsoluteFilePath.Replace(TEXT("//"), TEXT("/"), ESearchCase::CaseSensitive);

		FText InvalidPathReason;
		bool const bValidPackageName = FPackageName::IsValidLongPackageName(AbsoluteFilePath, false, &InvalidPathReason);

		check(bValidPackageName);

		// Set up Package.
		CurrentPackage = CreatePackage(nullptr, *AbsoluteFilePath);

		// Create a new USoundWave.
		CurrentSoundWave = NewObject<USoundWave>(CurrentPackage, *FileName, RF_Public | RF_Standalone);
		CurrentSoundWave->AddToRoot();

		CurrentBuffer = InSampleBuffer;

		// TODO: Support writing to multi-channel sound waves. For now, we mix down sound waves to stereo:
		if (CurrentBuffer.GetNumChannels() > 2)
		{
			CurrentBuffer.MixBufferToChannels(2);
		}

		CurrentOperation.Reset(new FAsyncSoundWavePCMWriterTask(this, ESoundWavePCMWriteTaskType::GenerateAndWriteSoundWave, OnSuccess));
		CurrentOperation->StartBackgroundTask();

		return true;
	}

	bool FSoundWavePCMWriter::BeginWriteToWavFile(const TSampleBuffer<>& InSampleBuffer, const FString& FileName, FString& FilePath, TFunction<void()> OnSuccess)
	{
		if (!IsDone())
		{
			UE_LOG(LogAudio, Error, TEXT("This instance of FSoundWavePCMWriter is already processing another write operation."));
			return false;
		}

		FPaths::NormalizeDirectoryName(FilePath);

		FilePath = FPaths::ProjectSavedDir() + TEXT("BouncedWavFiles") + TEXT("/") + FilePath;

		CurrentState = ESoundWavePCMWriterState::Generating;

		if (!CreateDirectoryIfNeeded(FilePath))
		{
			UE_LOG(LogAudio, Error, TEXT("Write to Wav File failed: Invalid directory path %s"), *FilePath);
			CurrentState = ESoundWavePCMWriterState::Failed;
			return false;
		}

		AbsoluteFilePath = FilePath + FString(TEXT("/")) + FileName + FString(TEXT(".wav"));
		AbsoluteFilePath = AbsoluteFilePath.Replace(TEXT("//"), TEXT("/"), ESearchCase::CaseSensitive);

		CurrentBuffer = InSampleBuffer;

		// For convenience in our async task, we only take void(USoundWave*) type lambdas. So let's wrap our void() lambda here:
		TFunction<void(const USoundWave*)> WrappedCallback = [OnSuccess](const USoundWave*)
		{
			OnSuccess();
		};

		CurrentOperation.Reset(new FAsyncSoundWavePCMWriterTask(this, ESoundWavePCMWriteTaskType::WriteWavFile, WrappedCallback));
		CurrentOperation->StartBackgroundTask();

		return true;
	}

	USoundWave* FSoundWavePCMWriter::SynchronouslyWriteSoundWave(const TSampleBuffer<>& InSampleBuffer, const FString* FileName, const FString* FilePath)
	{
		if (!IsDone())
		{
			UE_LOG(LogAudio, Error, TEXT("This instance of FSoundWavePCMWriter is already processing another write operation."));
			return nullptr;
		}

		CurrentState = ESoundWavePCMWriterState::Generating;
		
		bool bWillWriteToDisk = false;

		if (GIsEditor && FileName != nullptr)
		{
			if (FilePath != nullptr)
			{
				AbsoluteFilePath = TEXT("/Game/") + *FilePath + FString(TEXT("/")) + *FileName;
			}
			else
			{
				AbsoluteFilePath = TEXT("/Game/") + *FileName;
			}


			FPaths::NormalizeDirectoryName(AbsoluteFilePath);
			AbsoluteFilePath = AbsoluteFilePath.Replace(TEXT("//"), TEXT("/"), ESearchCase::CaseSensitive);

			FText InvalidPathReason;
			bool const bValidPackageName = FPackageName::IsValidLongPackageName(AbsoluteFilePath, false, &InvalidPathReason);

			check(bValidPackageName);

			// Set up Package.
			CurrentPackage = CreatePackage(nullptr, *AbsoluteFilePath);

			// Create a new USoundWave.
			CurrentSoundWave = NewObject<USoundWave>(CurrentPackage, **FileName, RF_Public | RF_Standalone);
			bWillWriteToDisk = true;
		}
		else
		{
			CurrentSoundWave = NewObject<USoundWave>();
		}
		

		CurrentBuffer = InSampleBuffer;

		// TODO: Support writing to multi-channel sound waves. For now, we mix down sound waves to stereo:
		if (CurrentBuffer.GetNumChannels() > 2)
		{
			CurrentBuffer.MixBufferToChannels(2);
		}

		if (bWillWriteToDisk)
		{
			CurrentOperation.Reset(new FAsyncSoundWavePCMWriterTask(this, ESoundWavePCMWriteTaskType::GenerateAndWriteSoundWave, [](const USoundWave* InSoundWave) {}));
		}
		else
		{
			CurrentOperation.Reset(new FAsyncSoundWavePCMWriterTask(this, ESoundWavePCMWriteTaskType::GenerateSoundWave, [](const USoundWave* InSoundWave) {}));
		}
		
		CurrentOperation->StartSynchronousTask();

		return CurrentSoundWave;
	}

	float FSoundWavePCMWriter::CheckStatus(ESoundWavePCMWriterState* OutCurrentState)
	{
		if (OutCurrentState)
		{
			*OutCurrentState = CurrentState;
		}

		return ((float)Progress.GetValue()) / (SerializedWavData.Num() + 1);
	}

	void FSoundWavePCMWriter::CancelWrite()
	{
		if (CurrentOperation.IsValid())
		{
			if (!CurrentOperation->Cancel())
			{
				CurrentOperation->EnsureCompletion(true);
			}

			CurrentOperation.Reset();
		}
		CurrentState = ESoundWavePCMWriterState::Cancelled;
	}

	bool FSoundWavePCMWriter::IsDone()
	{
		return (CurrentState == ESoundWavePCMWriterState::Suceeded
			|| CurrentState == ESoundWavePCMWriterState::Failed
			|| CurrentState == ESoundWavePCMWriterState::Cancelled
			|| CurrentState == ESoundWavePCMWriterState::Idle);
	}

	void FSoundWavePCMWriter::Reset()
	{
		CancelWrite();

		if (CurrentSoundWave && !bWasPreviouslyAddedToRoot)
		{
			CurrentSoundWave->RemoveFromRoot();
		}

		CurrentSoundWave = nullptr;
		CurrentPackage = nullptr;

		Progress.Set(0);
		CurrentState = ESoundWavePCMWriterState::Idle;
	}

	USoundWave* FSoundWavePCMWriter::GetFinishedSoundWave()
	{
		if (!IsDone())
		{
			UE_LOG(LogAudio, Warning, TEXT("Failed to get finished soundwave: write operation currently still in progress."));
			return nullptr;
		}
		else if (CurrentState != ESoundWavePCMWriterState::Suceeded)
		{
			UE_LOG(LogAudio, Warning, TEXT("Failed to get finished soundwave: write operation failed."));
			return nullptr;
		}

		if (CurrentSoundWave)
		{
			if (!bWasPreviouslyAddedToRoot)
			{
				CurrentSoundWave->RemoveFromRoot();
			}

			return CurrentSoundWave;
		}

		// If we did not initially create a SoundWave, we can create one here and apply the buffer to it.
		check(CurrentSoundWave == nullptr);
		CurrentSoundWave = NewObject<USoundWave>();
		ApplyBufferToSoundWave();

		return CurrentSoundWave;
	}

	void FSoundWavePCMWriter::SaveFinishedSoundWaveToPath(const FString& FileName, FString InPath)
	{
		// This is an editor only function.
		if (!GIsEditor)
		{
			UE_LOG(LogAudio, Warning, TEXT("SoundWave assets can only be saved with the editor."));
			return;
		}
		else if (!IsDone())
		{
			UE_LOG(LogAudio, Warning, TEXT("Failed to kick off save: write operation still in progress."));
			return;
		}
		else if (CurrentState != ESoundWavePCMWriterState::Suceeded)
		{
			UE_LOG(LogAudio, Warning, TEXT("Failed to kick off save: write operation failed."));
			return;
		}

		if (!CreateDirectoryIfNeeded(InPath))
		{
			UE_LOG(LogAudio, Warning, TEXT("Failed to kick off save: invalid directory %s"), *InPath);
			return;
		}

		AbsoluteFilePath = InPath + FString(TEXT("/")) + FileName;
		AbsoluteFilePath = AbsoluteFilePath.Replace(TEXT("//"), TEXT("/"), ESearchCase::CaseSensitive);
		SerializeSoundWaveToAsset();
	}

	void FSoundWavePCMWriter::ApplyBufferToSoundWave()
	{
		CurrentSoundWave->InvalidateCompressedData();

		CurrentSoundWave->SetSampleRate(CurrentBuffer.GetSampleRate());
		CurrentSoundWave->NumChannels = CurrentBuffer.GetNumChannels();
		CurrentSoundWave->RawPCMDataSize = CurrentBuffer.GetNumSamples() * sizeof(int16);
		CurrentSoundWave->Duration = (float) CurrentBuffer.GetNumFrames() / CurrentBuffer.GetSampleRate();

		if (CurrentSoundWave->RawPCMData != nullptr)
		{
			FMemory::Free(CurrentSoundWave->RawPCMData);
		}

		CurrentSoundWave->RawPCMData = (uint8*)FMemory::Malloc(CurrentSoundWave->RawPCMDataSize);
		FMemory::Memcpy(CurrentSoundWave->RawPCMData, CurrentBuffer.GetData(), CurrentSoundWave->RawPCMDataSize);
	}

	void FSoundWavePCMWriter::SerializeSoundWaveToAsset()
	{
		check(CurrentSoundWave != nullptr);
		CurrentState = ESoundWavePCMWriterState::Generating;

		if (CurrentBuffer.GetNumSamples() == 0)
		{
			UE_LOG(LogAudio, Error, TEXT("Writing out wav file failed- There was no audio data to write."));
			CurrentState = ESoundWavePCMWriterState::Failed;
			return;
		}

		SerializedWavData.Reset();
		SerializeWaveFile(SerializedWavData, (const uint8*)CurrentBuffer.GetData(), CurrentBuffer.GetNumSamples() * sizeof(int16), CurrentBuffer.GetNumChannels(), CurrentBuffer.GetSampleRate());

		UE_LOG(LogAudio, Display, TEXT("Serializing %d sample file (%d bytes) to sound asset at %s"), CurrentBuffer.GetNumSamples(), SerializedWavData.Num(), *AbsoluteFilePath);

		// TODO: Check to see if we need to call USoundWave::FreeResources here.

		// Emplace wav data in the RawData component of the sound wave.
		CurrentSoundWave->RawData.Lock(LOCK_READ_WRITE);
		void* LockedData = CurrentSoundWave->RawData.Realloc(SerializedWavData.Num());
		FMemory::Memcpy(LockedData, SerializedWavData.GetData(), SerializedWavData.Num());
		CurrentSoundWave->RawData.Unlock();

		USoundWave* SavedSoundWave = CurrentSoundWave;

		AsyncTask(ENamedThreads::GameThread, [SavedSoundWave]() {
			FAssetRegistryModule::AssetCreated(SavedSoundWave);
			SavedSoundWave->MarkPackageDirty();
		});
		
		CurrentState = ESoundWavePCMWriterState::Suceeded;
	}

	void FSoundWavePCMWriter::SerializeBufferToWavFile()
	{
		CurrentState = ESoundWavePCMWriterState::Generating;

		if (CurrentBuffer.GetNumSamples() == 0)
		{
			UE_LOG(LogAudio, Error, TEXT("Writing out wav file failed- There was no audio data to write."));
			CurrentState = ESoundWavePCMWriterState::Failed;
			return;
		}

		SerializeWaveFile(SerializedWavData, (const uint8*) CurrentBuffer.GetData(), CurrentBuffer.GetNumSamples() * sizeof(int16), CurrentBuffer.GetNumChannels(), CurrentBuffer.GetSampleRate());
		UE_LOG(LogAudio, Display, TEXT("Serializing %d sample file (%d bytes) to %s"), CurrentBuffer.GetNumSamples(), SerializedWavData.Num(), *AbsoluteFilePath);
		if (SerializedWavData.Num() == 0)
		{
			UE_LOG(LogAudio, Error, TEXT("Wave serialize operation failed: failiure in SerializeWaveFile"));
			CurrentState = ESoundWavePCMWriterState::Failed;
			return;
		}
		else
		{
			CurrentState = ESoundWavePCMWriterState::WritingToDisk;
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

			IFileHandle* FileHandle = PlatformFile.OpenWrite(*AbsoluteFilePath);
			if (FileHandle)
			{
				int32 NumChunks = FMath::CeilToInt(SerializedWavData.Num() / ChunkSize);
				UE_LOG(LogAudio, Display, TEXT("Writing wav file in %d chunks..."), NumChunks);
				for (int32 ChunkIndex = 0; ChunkIndex < NumChunks; ChunkIndex++)
				{
					int32 BufferIndex = ChunkIndex * ChunkSize;
					uint8* BufferPtr = &(SerializedWavData.GetData()[BufferIndex]);
					
					// Account for leftover buffer part:
					int32 CurrentChunkSize = FMath::Min(ChunkSize, SerializedWavData.Num() - BufferIndex);
					
					if (!FileHandle->Write(BufferPtr, CurrentChunkSize))
					{
						UE_LOG(LogAudio, Error, TEXT("Wave serialize operation failed: failiure in SerializeWaveFile. Chunk: %d Index: %d ChunkSize: %d"), ChunkIndex, BufferIndex, CurrentChunkSize);
						SerializedWavData.Reset();
						delete FileHandle;
						CurrentState = ESoundWavePCMWriterState::Failed;
						return;
					}

					// Update progress:
					Progress.Add(CurrentChunkSize);
				}
				

				// IFileHandle's destructor will close the file.
				delete FileHandle;
			}
			else
			{
				UE_LOG(LogAudio, Error, TEXT("Wave serialize operation failed: failiure in SerializeWaveFile"));
				SerializedWavData.Reset();
				CurrentState = ESoundWavePCMWriterState::Failed;
				return;
			}
		}
		UE_LOG(LogAudio, Display, TEXT("Succeeded in writing wav file."));
		CurrentState = ESoundWavePCMWriterState::Suceeded;
	}

	bool FSoundWavePCMWriter::CreateDirectoryIfNeeded(FString& DirectoryPath)
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

		if (!PlatformFile.DirectoryExists(*DirectoryPath))
		{
			if (!PlatformFile.CreateDirectoryTree(*DirectoryPath))
			{
				return false;
			}
		}
		return true;
	}

	FAsyncSoundWavePCMWriteWorker::FAsyncSoundWavePCMWriteWorker(FSoundWavePCMWriter* InWriter, ESoundWavePCMWriteTaskType InTaskType, TFunction<void(const USoundWave*)> OnSuccess)
		: Writer(InWriter)
		, TaskType(InTaskType)
		, CallbackOnSuccess(OnSuccess)
	{
	}

	FAsyncSoundWavePCMWriteWorker::~FAsyncSoundWavePCMWriteWorker()
	{
		FScopeLock AbandonLock(&NonAbandonableSection);
		Abandon();
	}

	void FAsyncSoundWavePCMWriteWorker::DoWork()
	{
		switch (TaskType)
		{
			case Audio::ESoundWavePCMWriteTaskType::GenerateSoundWave:
			{
				if (Writer->CurrentSoundWave == nullptr)
				{
					Writer->CurrentState = ESoundWavePCMWriterState::Failed;
					return;
				}
			
				FScopeLock AbandonLock(&NonAbandonableSection);
				Writer->CurrentState = ESoundWavePCMWriterState::Generating;
				Writer->ApplyBufferToSoundWave();
				Writer->CurrentState = ESoundWavePCMWriterState::Suceeded;
				break;
			}

			case Audio::ESoundWavePCMWriteTaskType::GenerateAndWriteSoundWave:
			{
				if (Writer->CurrentSoundWave == nullptr)
				{
					Writer->CurrentState = ESoundWavePCMWriterState::Failed;
					return;
				}

				{
					FScopeLock AbandonLock(&NonAbandonableSection);
					Writer->CurrentState = ESoundWavePCMWriterState::Generating;
					Writer->ApplyBufferToSoundWave();
				}

				{
					FScopeLock AbandonLock(&NonAbandonableSection);
					Writer->CurrentState = ESoundWavePCMWriterState::WritingToDisk;
					Writer->SerializeSoundWaveToAsset();
				}

				break;
			}
			case Audio::ESoundWavePCMWriteTaskType::WriteSoundWave:
			{
				if (Writer->CurrentSoundWave == nullptr)
				{
					Writer->CurrentState = ESoundWavePCMWriterState::Failed;
					return;
				}

				FScopeLock AbandonLock(&NonAbandonableSection);
				Writer->CurrentState = ESoundWavePCMWriterState::WritingToDisk;
				Writer->SerializeSoundWaveToAsset();
				break;
			}

			case Audio::ESoundWavePCMWriteTaskType::WriteWavFile:
			{
				FScopeLock AbandonLock(&NonAbandonableSection);
				Writer->CurrentState = ESoundWavePCMWriterState::WritingToDisk;
				Writer->SerializeBufferToWavFile();
				break;
			}

			default:
				break;
			}


		// Capture our callback and perform it on the game thread:
		const USoundWave* SoundWave = Writer->CurrentSoundWave;
		TFunction<void(const USoundWave*)> Callback = MoveTemp(CallbackOnSuccess);

		AsyncTask(ENamedThreads::GameThread, [Callback, SoundWave]() {
			Callback(SoundWave);
		});
	}

	void FAsyncSoundWavePCMWriteWorker::Abandon()
	{
		FScopeLock AbandonLock(&NonAbandonableSection);

		// Do any state cleanup here.
		Writer->CurrentState = ESoundWavePCMWriterState::Cancelled;
	}

}


