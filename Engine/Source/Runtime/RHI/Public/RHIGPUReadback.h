// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
  RHIGPUReadback.h: classes for managing fences and staging buffers for
  asynchronous GPU memory updates and readbacks with minimal stalls and no
  RHI thread flushes
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"

/**
 * FRHIGPUMemoryReadback: Represents a memory readback request scheduled with CopyToStagingBuffer
 * Wraps a staging buffer with a FRHIGPUFence for synchronization.
 */
class RHI_API FRHIGPUMemoryReadback
{
public:

	FRHIGPUMemoryReadback(FName RequestName)
	{
		Fence = RHICreateGPUFence(RequestName);
	}

	virtual ~FRHIGPUMemoryReadback() {}

	/** Indicates if the data is in place and ready to be read. */
	FORCEINLINE bool IsReady()
	{
		return !Fence || Fence->Poll();
	}

	/**
	 * Copy the current state of the resource to the readback data.
	 * @param RHICmdList The command list to enqueue the copy request on.
	 * @param SourceBuffer The buffer holding the source data.
	 * @param NumBytes The number of bytes to copy. If 0, this will copy the entire buffer.
	 */
	virtual void EnqueueCopy(FRHICommandList& RHICmdList, FRHIVertexBuffer* SourceBuffer, uint32 NumBytes = 0)
	{
		unimplemented();
	}

	virtual void EnqueueCopy(FRHICommandList& RHICmdList, FRHITexture* SourceTexture, FResolveRect Rect = FResolveRect())
	{
		unimplemented();
	}

	/**
	 * Returns the CPU accessible pointer that backs this staging buffer.
	 * @param NumBytes The maximum number of bytes the host will read from this pointer.
	 * @returns A CPU accessible pointer to the backing buffer.
	 */
	virtual void* Lock(uint32 NumBytes) = 0;

	/**
	 * Signals that the host is finished reading from the backing buffer.
	 */
	virtual void Unlock() = 0;

protected:

	FGPUFenceRHIRef Fence;
};

/** Buffer readback implementation. */
class RHI_API FRHIGPUBufferReadback final : public FRHIGPUMemoryReadback
{
public:

	FRHIGPUBufferReadback(FName RequestName);
	 
	void EnqueueCopy(FRHICommandList& RHICmdList, FRHIVertexBuffer* SourceBuffer, uint32 NumBytes = 0) override;
	void* Lock(uint32 NumBytes) override;
	void Unlock() override;

private:

	FStagingBufferRHIRef DestinationStagingBuffer;
};


/** Texture readback implementation. */
class RHI_API FRHIGPUTextureReadback final : public FRHIGPUMemoryReadback
{
public:

	FRHIGPUTextureReadback(FName RequestName);

	void EnqueueCopy(FRHICommandList& RHICmdList, FRHITexture* SourceTexture, FResolveRect Rect = FResolveRect()) override;
	void* Lock(uint32 NumBytes) override;
	void Unlock() override;

private:

	FTextureRHIRef DestinationStagingBuffer;
};
