// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "ADPCMAudioInfo.h"
#include "CoreMinimal.h"
#include "Interfaces/IAudioFormat.h"
#include "Sound/SoundWave.h"
#include "Audio.h"
#include "ContentStreaming.h"

#define WAVE_FORMAT_LPCM  1
#define WAVE_FORMAT_ADPCM 2


namespace ADPCM
{
	const uint32 MaxChunkSize = 256 * 1024;

	void DecodeBlock(const uint8* EncodedADPCMBlock, int32 BlockSize, int16* DecodedPCMData);
	void DecodeBlockStereo(const uint8* EncodedADPCMBlockLeft, const uint8* EncodedADPCMBlockRight, int32 BlockSize, int16* DecodedPCMData);
}

FADPCMAudioInfo::FADPCMAudioInfo(void)
	: UncompressedBlockSize(0)
	, CompressedBlockSize(0)
	, BlockSize(0)
	, StreamBufferSize(0)
	, TotalDecodedSize(0)
	, NumChannels(0)
	, Format(0)
	, UncompressedBlockData(nullptr)
	, SamplesPerBlock(0)
	, bSeekPending(false)
{
}

FADPCMAudioInfo::~FADPCMAudioInfo(void)
{
	if(UncompressedBlockData != nullptr)
	{
		FMemory::Free(UncompressedBlockData);
		UncompressedBlockData = nullptr;
	}
}

void FADPCMAudioInfo::SeekToTime(const float SeekTime)
{
	CurCompressedChunkData = nullptr;

	if(SeekTime <= 0.0f)
	{
		CurrentCompressedBlockIndex = 0;
		CurrentUncompressedBlockSampleIndex = 0;
		CurrentChunkIndex = 0;
		CurrentChunkBufferOffset = 0;
		TotalSamplesStreamed = 0;

		return;
	}

	// Calculate block index & force SeekTime to be in bounds.
	check(WaveInfo.pSamplesPerSec != nullptr);
	const uint32 SamplesPerSec = *WaveInfo.pSamplesPerSec;
	const uint32 SeekedSamples = static_cast<uint32>(SeekTime * SamplesPerSec);
	TotalSamplesStreamed = FMath::Min<uint32>(SeekedSamples, TotalSamplesPerChannel - 1);

	const uint32 HeaderOffset = static_cast<uint32>(WaveInfo.SampleDataStart - SrcBufferData);

	if (Format == WAVE_FORMAT_ADPCM)
	{
		CurrentCompressedBlockIndex = TotalSamplesStreamed / SamplesPerBlock; // Compute the block index that where SeekTime resides.
		CurrentChunkIndex = 0;
		CurrentChunkBufferOffset = HeaderOffset;

		const int32 ChannelBlockSize = BlockSize * NumChannels;
		for (uint32 BlockIndex = 0; BlockIndex < CurrentCompressedBlockIndex; ++BlockIndex)
		{
			if (CurrentChunkBufferOffset + ChannelBlockSize >= ADPCM::MaxChunkSize)
			{
				++CurrentChunkIndex;
				CurrentChunkBufferOffset = 0;
			}

			// Always add chunks in NumChannels pairs
			CurrentChunkBufferOffset += ChannelBlockSize;
		}
	}
	else if (Format == WAVE_FORMAT_LPCM)
	{
		const int32 ChannelBlockSize = sizeof(int16) * NumChannels;

		// 1. Find total offset
		CurrentChunkBufferOffset = HeaderOffset + (TotalSamplesStreamed * ChannelBlockSize);

		// 2. Calculate index and remove from offset
		CurrentChunkIndex = CurrentChunkBufferOffset / ADPCM::MaxChunkSize;
		CurrentChunkBufferOffset %= ADPCM::MaxChunkSize;

		// 3. Trim remainder of block size, effectively aligning the block to a channel pair boundary
		CurrentChunkBufferOffset -= CurrentChunkBufferOffset % ChannelBlockSize;
	}
	else
	{
		return;
	}

	bSeekPending = true;
}

bool FADPCMAudioInfo::ReadCompressedInfo(const uint8* InSrcBufferData, uint32 InSrcBufferDataSize, struct FSoundQualityInfo* QualityInfo)
{
	if (!InSrcBufferData)
	{
		FString Name = QualityInfo ? QualityInfo->DebugName : TEXT("Unknown");
		UE_LOG(LogAudio, Warning, TEXT("Failed to read compressed ADPCM audio from ('%s') because there was no resource data."), *Name);

		return false;
	}

	SrcBufferData = InSrcBufferData;
	SrcBufferDataSize = InSrcBufferDataSize;

	void*	FormatHeader;

	if (!WaveInfo.ReadWaveInfo((uint8*)SrcBufferData, SrcBufferDataSize, nullptr, false, &FormatHeader))
	{
		UE_LOG(LogAudio, Warning, TEXT("WaveInfo.ReadWaveInfo Failed"));
		return false;
	}

	Format = *WaveInfo.pFormatTag;
	NumChannels = *WaveInfo.pChannels;

	if (Format == WAVE_FORMAT_ADPCM)
	{
		ADPCM::ADPCMFormatHeader* ADPCMHeader = (ADPCM::ADPCMFormatHeader*)FormatHeader;
		TotalSamplesPerChannel = ADPCMHeader->SamplesPerChannel;
		SamplesPerBlock = ADPCMHeader->wSamplesPerBlock;

		const uint32 PreambleSize = 7;
		BlockSize = *WaveInfo.pBlockAlign;

		// ADPCM starts with 2 uncompressed samples and then the remaining compressed sample data has 2 samples per byte
		const uint32 UncompressedBlockSamples = (2 + (BlockSize - PreambleSize) * 2);
		UncompressedBlockSize = UncompressedBlockSamples * sizeof(int16);
		CompressedBlockSize = BlockSize;

		const uint32 TargetBlocks = MONO_PCM_BUFFER_SAMPLES / UncompressedBlockSamples;
		StreamBufferSize = TargetBlocks * UncompressedBlockSize;
		TotalDecodedSize = TotalSamplesPerChannel * NumChannels * sizeof(int16);

		UncompressedBlockData = (uint8*)FMemory::Realloc(UncompressedBlockData, NumChannels * UncompressedBlockSize);
		check(UncompressedBlockData != nullptr);
		TotalCompressedBlocksPerChannel = (WaveInfo.SampleDataSize + CompressedBlockSize - 1) / CompressedBlockSize / NumChannels;
	}
	else if(Format == WAVE_FORMAT_LPCM)
	{
		// There are no "blocks" in this case
		BlockSize = 0;
		UncompressedBlockSize = 0;
		CompressedBlockSize = 0;
		StreamBufferSize = 0;
		UncompressedBlockData = nullptr;
		TotalCompressedBlocksPerChannel = 0;

		TotalDecodedSize = WaveInfo.SampleDataSize;

		TotalSamplesPerChannel = TotalDecodedSize / sizeof(uint16) / NumChannels;
	}
	else
	{
		return false;
	}

	if (QualityInfo)
	{
		QualityInfo->SampleRate = *WaveInfo.pSamplesPerSec;
		QualityInfo->NumChannels = *WaveInfo.pChannels;
		QualityInfo->SampleDataSize = TotalDecodedSize;
		QualityInfo->Duration = (float)TotalSamplesPerChannel / QualityInfo->SampleRate;
	}

	CurrentCompressedBlockIndex = 0;
	TotalSamplesStreamed = 0;
	// This is set to the max value to trigger the decompression of the first audio block
	CurrentUncompressedBlockSampleIndex = UncompressedBlockSize / sizeof(uint16);

	return true;
}

bool FADPCMAudioInfo::ReadCompressedData(uint8* Destination, bool bLooping, uint32 BufferSize)
{
	// If we've already read through this asset and we are not looping, memzero and early out.
	if (TotalSamplesStreamed >= TotalSamplesPerChannel && !bLooping)
	{
		FMemory::Memzero(Destination, BufferSize);
		return true;
	}

	const uint32 ChannelSampleSize = sizeof(uint16) * NumChannels;

	// This correctly handles any BufferSize as long as its a multiple of sample size * number of channels
	check(Destination);
	check(BufferSize % ChannelSampleSize == 0);

	int16* OutData = (int16*)Destination;

	bool ReachedEndOfSamples = false;

	if(Format == WAVE_FORMAT_ADPCM)
	{
		// We need to loop over the number of samples requested since an uncompressed block will not match the number of frames requested
		while(BufferSize > 0)
		{
			if(CurrentUncompressedBlockSampleIndex >= UncompressedBlockSize / sizeof(uint16))
			{
				// we need to decompress another block of compressed data from the current chunk

				// Decompress one block for each channel and store it in UncompressedBlockData
				for(int32 ChannelItr = 0; ChannelItr < NumChannels; ++ChannelItr)
				{
					ADPCM::DecodeBlock(
						WaveInfo.SampleDataStart + (ChannelItr * TotalCompressedBlocksPerChannel + CurrentCompressedBlockIndex) * CompressedBlockSize,
						CompressedBlockSize,
						(int16*)(UncompressedBlockData + ChannelItr * UncompressedBlockSize));
				}

				// Update some bookkeeping
				CurrentUncompressedBlockSampleIndex = 0;
				++CurrentCompressedBlockIndex;
			}

			// Only copy over the number of samples we currently have available, we will loop around if needed
			uint32 DecompressedSamplesToCopy = FMath::Min<uint32>(UncompressedBlockSize / sizeof(uint16) - CurrentUncompressedBlockSampleIndex, BufferSize / (sizeof(uint16) * NumChannels));
			check(DecompressedSamplesToCopy > 0);

			// Ensure we don't go over the number of samples left in the audio data
			if(DecompressedSamplesToCopy > TotalSamplesPerChannel - TotalSamplesStreamed)
			{
				DecompressedSamplesToCopy = TotalSamplesPerChannel - TotalSamplesStreamed;
			}

			// Copy over the actual sample data
			for(uint32 SampleItr = 0; SampleItr < DecompressedSamplesToCopy; ++SampleItr)
			{
				for(int32 ChannelItr = 0; ChannelItr < NumChannels; ++ChannelItr)
				{
					uint16 Value = *(int16*)(UncompressedBlockData + ChannelItr * UncompressedBlockSize + (CurrentUncompressedBlockSampleIndex + SampleItr) * sizeof(int16));
					*OutData++ = Value;
				}
			}

			// Update bookkeeping
			CurrentUncompressedBlockSampleIndex += DecompressedSamplesToCopy;
			BufferSize -= DecompressedSamplesToCopy * sizeof(uint16) * NumChannels;
			TotalSamplesStreamed += DecompressedSamplesToCopy;

			// Check for the end of the audio samples and loop if needed
			if(TotalSamplesStreamed >= TotalSamplesPerChannel)
			{
				ReachedEndOfSamples = true;
				if(!bLooping)
				{
					// Zero remaining buffer
					FMemory::Memzero(OutData, BufferSize);
					return true;
				}
				else
				{
					// This is set to the max value to trigger the decompression of the first audio block
					CurrentUncompressedBlockSampleIndex = UncompressedBlockSize / sizeof(uint16);
					CurrentCompressedBlockIndex = 0;
					TotalSamplesStreamed = 0;
				}
			}
		}
	}
	else
	{
		uint32 OutDataOffset = 0;
		while (BufferSize > 0)
		{
			uint32 DecompressedSamplesToCopy = BufferSize / ChannelSampleSize;

			// Ensure we don't go over the number of samples left in the audio data
			if (DecompressedSamplesToCopy > TotalSamplesPerChannel - TotalSamplesStreamed)
			{
				DecompressedSamplesToCopy = TotalSamplesPerChannel - TotalSamplesStreamed;
			}

			FMemory::Memcpy(OutData + OutDataOffset, WaveInfo.SampleDataStart + (TotalSamplesStreamed * ChannelSampleSize), DecompressedSamplesToCopy * ChannelSampleSize);
			TotalSamplesStreamed += DecompressedSamplesToCopy;
			BufferSize -= DecompressedSamplesToCopy * ChannelSampleSize;
			OutDataOffset += DecompressedSamplesToCopy * NumChannels;

			// Check for the end of the audio samples and loop if needed
			if (TotalSamplesStreamed >= TotalSamplesPerChannel)
			{
				ReachedEndOfSamples = true;
				TotalSamplesStreamed = 0;
				if (!bLooping)
				{
					// Zero remaining buffer
					FMemory::Memzero(OutData, BufferSize);
					return true;
				}
			}
		}
	}

	return ReachedEndOfSamples;
}

void FADPCMAudioInfo::ExpandFile(uint8* DstBuffer, struct FSoundQualityInfo* QualityInfo)
{
	check(DstBuffer);

	ReadCompressedData(DstBuffer, false, TotalDecodedSize);
}

int FADPCMAudioInfo::GetStreamBufferSize() const
{
	return StreamBufferSize;
}

bool FADPCMAudioInfo::StreamCompressedInfoInternal(USoundWave* Wave, struct FSoundQualityInfo* QualityInfo)
{
	check(QualityInfo);

	check(StreamingSoundWave == Wave);

	// Get the first chunk of audio data (should already be loaded)
	uint8 const* FirstChunk = IStreamingManager::Get().GetAudioStreamingManager().GetLoadedChunk(Wave, 0, &CurrentChunkDataSize);

	if (FirstChunk == nullptr)
	{
		return false;
	}

	SrcBufferData = nullptr;
	SrcBufferDataSize = 0;

	void* FormatHeader;

	if (!WaveInfo.ReadWaveInfo((uint8*)FirstChunk, Wave->RunningPlatformData->Chunks[0].AudioDataSize, nullptr, true, &FormatHeader))
	{
		UE_LOG(LogAudio, Warning, TEXT("WaveInfo.ReadWaveInfo Failed"));
		return false;
	}

	SrcBufferData = FirstChunk;

	FirstChunkSampleDataOffset = WaveInfo.SampleDataStart - FirstChunk;
	CurrentChunkBufferOffset = 0;
	CurCompressedChunkData = nullptr;
	CurrentUncompressedBlockSampleIndex = 0;
	CurrentChunkIndex = 0;
	TotalSamplesStreamed = 0;
	Format = *WaveInfo.pFormatTag;
	NumChannels = *WaveInfo.pChannels;

	if (Format == WAVE_FORMAT_ADPCM)
	{
		ADPCM::ADPCMFormatHeader* ADPCMHeader = (ADPCM::ADPCMFormatHeader*)FormatHeader;
		TotalSamplesPerChannel = ADPCMHeader->SamplesPerChannel;
		SamplesPerBlock = ADPCMHeader->wSamplesPerBlock;

		const uint32 PreambleSize = 7;

		BlockSize = *WaveInfo.pBlockAlign;
		UncompressedBlockSize = (2 + (BlockSize - PreambleSize) * 2) * sizeof(int16);
		CompressedBlockSize = BlockSize;

		// Calculate buffer sizes and total number of samples
		const uint32 uncompressedBlockSamples = (2 + (BlockSize - PreambleSize) * 2);
		const uint32 targetBlocks = MONO_PCM_BUFFER_SAMPLES / uncompressedBlockSamples;
		StreamBufferSize = targetBlocks * UncompressedBlockSize;
		TotalDecodedSize = ((WaveInfo.SampleDataSize + CompressedBlockSize - 1) / CompressedBlockSize) * UncompressedBlockSize;

		UncompressedBlockData = (uint8*)FMemory::Realloc(UncompressedBlockData, NumChannels * UncompressedBlockSize);
		check(UncompressedBlockData != nullptr);
	}
	else if (Format == WAVE_FORMAT_LPCM)
	{
		BlockSize = 0;
		UncompressedBlockSize = 0;
		CompressedBlockSize = 0;

		// This is uncompressed, so decoded size and buffer size are the same
		TotalDecodedSize = WaveInfo.SampleDataSize;
		StreamBufferSize = WaveInfo.SampleDataSize;
		TotalSamplesPerChannel = StreamBufferSize / sizeof(uint16) / NumChannels;
	}
	else
	{
		UE_LOG(LogAudio, Error, TEXT("Unsupported wave format"));
		return false;
	}

	if (QualityInfo)
	{
		QualityInfo->SampleRate = *WaveInfo.pSamplesPerSec;
		QualityInfo->NumChannels = *WaveInfo.pChannels;
		QualityInfo->SampleDataSize = TotalDecodedSize;
		QualityInfo->Duration = (float)TotalSamplesPerChannel / QualityInfo->SampleRate;
	}

	return true;
}

bool FADPCMAudioInfo::StreamCompressedData(uint8* Destination, bool bLooping, uint32 BufferSize)
{
	// Initial sanity checks:
	if (Destination == nullptr || BufferSize == 0)
	{
		UE_LOG(LogAudio, Error, TEXT("Stream Compressed Info not called!"));
		return false;
	}

	if (NumChannels == 0)
	{
		UE_LOG(LogAudio, Error, TEXT("Stream Compressed Info not called!"));
		FMemory::Memzero(Destination, BufferSize);
		return true;
	}

	// Destination samples are interlaced by channel, BufferSize is in bytes
	const int32 ChannelSampleSize = sizeof(uint16) * NumChannels;

	// Ensure that BuffserSize is a multiple of the sample size times the number of channels
	if (BufferSize % ChannelSampleSize != 0)
	{
		UE_LOG(LogAudio, Error, TEXT("Invalid buffer size %d requested for %d channels."), BufferSize, NumChannels);
		FMemory::Memzero(Destination, BufferSize);
		return true;
	}

	int16* OutData = (int16*)Destination;

	bool ReachedEndOfSamples = false;

	if(Format == WAVE_FORMAT_ADPCM)
	{
		// We need to loop over the number of samples requested since an uncompressed block will not match the number of frames requested
		while(BufferSize > 0)
		{
			if(CurCompressedChunkData == nullptr || CurrentUncompressedBlockSampleIndex >= UncompressedBlockSize / sizeof(uint16))
			{
				// we need to decompress another block of compressed data from the current chunk

				if(CurCompressedChunkData == nullptr || CurrentChunkBufferOffset >= CurrentChunkDataSize)
				{
					// Get another chunk of compressed data from the streaming engine

					// CurrentChunkIndex is used to keep track of the current chunk for loading/unloading by the streaming engine, but chunk 0 is
					// preloaded so don't increment this when getting chunk 0, only later chunks. If previous chunk retrieval failed because it was
					// not ready, don't re-increment the CurrentChunkIndex.  Only increment if seek not pending as seek determines the current chunk index
					// and invalidates CurCompressedChunkData.
					if(CurCompressedChunkData != nullptr)
					{
						++CurrentChunkIndex;
					}

					// Request the next chunk of data from the streaming engine
					CurCompressedChunkData = IStreamingManager::Get().GetAudioStreamingManager().GetLoadedChunk(StreamingSoundWave, CurrentChunkIndex, &CurrentChunkDataSize);

					if(CurCompressedChunkData == nullptr)
					{
						// We only need to worry about missing the stream chunk if we were seeking. Seeking might cause a bit of latency with chunk loading. That is expected.
						if (!bSeekPending)
						{
							// If we did not get it then just bail, CurrentChunkIndex will not get incremented on the next callback so in effect another attempt will be made to fetch the chunk
							// Since audio streaming depends on the general data streaming mechanism used by other parts of the engine and new data is prefectched on the game tick thread its possible a game hickup can cause this
							UE_LOG(LogAudio, Verbose, TEXT("Missed Deadline chunk %d"), CurrentChunkIndex);
						}

						// zero out remaining data and bail
						FMemory::Memset(OutData, 0, BufferSize);
						return false;
					}

					// Set the current buffer offset accounting for the header in the first chunk
					if (!bSeekPending)
					{
						CurrentChunkBufferOffset = CurrentChunkIndex == 0 ? FirstChunkSampleDataOffset : 0;
					}
					bSeekPending = false;
				}

				// Decompress one block for each channel and store it in UncompressedBlockData
				for(int32 ChannelItr = 0; ChannelItr < NumChannels; ++ChannelItr)
				{
					ADPCM::DecodeBlock(
						CurCompressedChunkData + CurrentChunkBufferOffset + ChannelItr * CompressedBlockSize,
						CompressedBlockSize,
						(int16*)(UncompressedBlockData + ChannelItr * UncompressedBlockSize));
				}

				// Update some bookkeeping
				CurrentUncompressedBlockSampleIndex = 0;
				CurrentChunkBufferOffset += NumChannels * CompressedBlockSize;
			}

			// Only copy over the number of samples we currently have available, we will loop around if needed
			uint32 DecompressedSamplesToCopy = FMath::Min<uint32>(
				(UncompressedBlockSize / sizeof(uint16)) - CurrentUncompressedBlockSampleIndex,
				BufferSize / (ChannelSampleSize));
			check(DecompressedSamplesToCopy > 0);

			// Ensure we don't go over the number of samples left in the audio data
			if(DecompressedSamplesToCopy > TotalSamplesPerChannel - TotalSamplesStreamed)
			{
				DecompressedSamplesToCopy = TotalSamplesPerChannel - TotalSamplesStreamed;
			}

			// Copy over the actual sample data
			for(uint32 SampleItr = 0; SampleItr < DecompressedSamplesToCopy; ++SampleItr)
			{
				for(int32 ChannelItr = 0; ChannelItr < NumChannels; ++ChannelItr)
				{
					uint16 Value = *(int16*)(UncompressedBlockData + ChannelItr * UncompressedBlockSize + (CurrentUncompressedBlockSampleIndex + SampleItr) * sizeof(int16));
					*OutData++ = Value;
				}
			}

			// Update bookkeeping
			CurrentUncompressedBlockSampleIndex += DecompressedSamplesToCopy;
			BufferSize -= DecompressedSamplesToCopy * ChannelSampleSize;
			TotalSamplesStreamed += DecompressedSamplesToCopy;

			// Check for the end of the audio samples and loop if needed
			if(TotalSamplesStreamed >= TotalSamplesPerChannel)
			{
				ReachedEndOfSamples = true;
				CurrentUncompressedBlockSampleIndex = 0;
				CurrentChunkIndex = 0;
				CurrentChunkBufferOffset = 0;
				TotalSamplesStreamed = 0;
				CurCompressedChunkData = nullptr;
				if(!bLooping)
				{
					// Set the remaining buffer to 0
					FMemory::Memset(OutData, 0, BufferSize);
					return true;
				}
			}
		}
	}
	else
	{
		while(BufferSize > 0)
		{
			if(CurCompressedChunkData == nullptr || CurrentChunkBufferOffset >= CurrentChunkDataSize)
			{
				// Get another chunk of compressed data from the streaming engine

				// CurrentChunkIndex is used to keep track of the current chunk for loading/unloading by the streaming engine but chunk 0
				// is preloaded so we don't want to increment this when getting chunk 0, only later chunks
				// Also, if we failed to get a previous chunk because it was not ready we don't want to re-increment the CurrentChunkIndex
				if(CurCompressedChunkData != nullptr)
				{
					++CurrentChunkIndex;
				}

				// Request the next chunk of data from the streaming engine
				CurCompressedChunkData = IStreamingManager::Get().GetAudioStreamingManager().GetLoadedChunk(StreamingSoundWave, CurrentChunkIndex, &CurrentChunkDataSize);

				if(CurCompressedChunkData == nullptr)
				{
					// Only report missing the stream chunk if we were seeking. Seeking
					// may cause a bit of latency with chunk loading, which is expected.
					if (!bSeekPending)
					{
						// CurrentChunkIndex will not get incremented on the next callback, effectively causing another attempt to fetch the chunk.
						// Since audio streaming depends on the general data streaming mechanism used by other parts of the engine and new data is
						// prefetched on the game tick thread, this may be caused by a game hitch.
						UE_LOG(LogAudio, Verbose, TEXT("Missed streaming ADPCM deadline chunk %d"), CurrentChunkIndex);
					}

					FMemory::Memset(OutData, 0, BufferSize);
					return false;
				}

				// Set the current buffer offset accounting for the header in the first chunk
				if (!bSeekPending)
				{
					CurrentChunkBufferOffset = CurrentChunkIndex == 0 ? FirstChunkSampleDataOffset : 0;
				}
				bSeekPending = false;
			}

			uint32 DecompressedSamplesToCopy = FMath::Min<uint32>(
				(CurrentChunkDataSize - CurrentChunkBufferOffset) / ChannelSampleSize,
				BufferSize / ChannelSampleSize);

			check(DecompressedSamplesToCopy > 0);

			// Ensure we don't go over the number of samples left in the audio data
			if(DecompressedSamplesToCopy > TotalSamplesPerChannel - TotalSamplesStreamed)
			{
				DecompressedSamplesToCopy = TotalSamplesPerChannel - TotalSamplesStreamed;
			}

			const uint32 SampleSize = DecompressedSamplesToCopy * ChannelSampleSize;
			FMemory::Memcpy(OutData, CurCompressedChunkData + CurrentChunkBufferOffset, SampleSize);

			OutData += DecompressedSamplesToCopy * NumChannels;
			CurrentChunkBufferOffset += SampleSize;
			BufferSize -= SampleSize;
			TotalSamplesStreamed += DecompressedSamplesToCopy;

			// Check for the end of the audio samples and loop if needed
			if(TotalSamplesStreamed >= TotalSamplesPerChannel)
			{
				ReachedEndOfSamples = true;
				CurrentChunkIndex = 0;
				CurrentChunkBufferOffset = 0;
				TotalSamplesStreamed = 0;
				CurCompressedChunkData = nullptr;
				if(!bLooping)
				{
					// Set the remaining buffer to 0
					FMemory::Memset(OutData, 0, BufferSize);
					return true;
				}
			}
		}
	}

	return ReachedEndOfSamples;
}
