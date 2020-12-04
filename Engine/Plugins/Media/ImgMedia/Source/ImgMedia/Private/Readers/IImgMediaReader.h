// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Misc/FrameRate.h"
#include "IMediaTextureSample.h"
#include "Math/IntPoint.h"
#include "Templates/SharedPointer.h"
#include "RHI.h"
#include "RHIResources.h"

/**
 * Information about an image sequence frame.
 */
struct FImgMediaFrameInfo
{
	/** Name of the image compression algorithm (i.e. "ZIP"). */
	FString CompressionName;

	/** Width and height of the frame (in pixels). */
	FIntPoint Dim;

	/** Name of the image format (i.e. "EXR"). */
	FString FormatName;

	/** Frame rate. */
	FFrameRate FrameRate;

	/** Whether the frame is in sRGB color space. */
	bool Srgb;

	/** Uncompressed size (in bytes). */
	SIZE_T UncompressedSize;

	/** Number of channels (RGB - 3 or RGBA - 4). */
	SIZE_T NumChannels;
};


/**
 * A single frame of an image sequence.
 */
struct FImgMediaFrame
{
	/** The frame's data. */
	TSharedPtr<void, ESPMode::ThreadSafe> Data;

	/** The frame's sample format. */
	EMediaTextureSampleFormat Format;

	/** Additional information about the frame. */
	FImgMediaFrameInfo Info;

	/** The frame's horizontal stride (in bytes). */
	uint32 Stride;

	/** Uncompressed EXR files are read faster via plain read and GPU swizzling. This value is used by ExrImgMediaReaderGpu.*/
	FTexture2DRHIRef Texture;

	/** Sample converter is used by Media Texture Resource to convert the texture or data. */
	TSharedPtr<IMediaTextureSampleConverter, ESPMode::ThreadSafe> SampleConverter;

	virtual IMediaTextureSampleConverter* GetSampleConverter()
	{
		if (!SampleConverter.IsValid())
		{
			return nullptr;
		}
		return SampleConverter.Get();
	};

	/** Virtual non trivial destructor. */
	virtual ~FImgMediaFrame() {};
};


/**
 * Interface for image sequence readers.
 */
class IImgMediaReader
{
public:

	/**
	 * Get information about an image sequence frame.
	 *
	 * @param ImagePath Path to the image file containing the frame.
	 * @param OutInfo Will contain the frame info.
	 * @return true on success, false otherwise.
	 * @see ReadFrame
	 */
	virtual bool GetFrameInfo(const FString& ImagePath, FImgMediaFrameInfo& OutInfo) = 0;

	/**
	 * Read a single image frame.
	 *
	 * @param ImagePath Path to the image file to read.
	 * @param OutFrame Will contain the frame.
	 * @return true on success, false otherwise.
	 * @see GetFrameInfo
	 */
	virtual bool ReadFrame(const FString& ImagePath, TSharedPtr<FImgMediaFrame, ESPMode::ThreadSafe> OutFrame, int32 FrameId) = 0;

	/**
	 * Mark Frame to be canceled based on Frame number. Typically this will be 
	 * @param FrameNumber used for frame lookup.
	 *
	 */
	virtual void CancelFrame(int32 FrameNumber) = 0;

	/**
	 * For some readers this function allows to pre-allocate enough memory to support the
	 * maximum number of frames with as much efficiency as possible.
	 *
	 */
	virtual void PreAllocateMemoryPool(int32 NumFrames, int32 AllocSize) {};

	/**
	 * Used in case reader needs to do some processing once per frame.
	 * Example: ExrImgMediaReaderGpu which returns unused memory to memory pool.
	 * 
	 */
	virtual void OnTick() {};


public:

	/** Virtual destructor. */
	virtual ~IImgMediaReader() { }
};
