// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VideoEncoder.h"

DECLARE_STATS_GROUP(TEXT("NvEnc"), STATGROUP_NvEnc, STATCAT_Advanced);

// Video encoder implementation based on NVIDIA Video Codecs SDK: https://developer.nvidia.com/nvidia-video-codec-sdk
// Uses only encoder part
class FPixelStreamingNvVideoEncoder : public IVideoEncoder
{
public:
	FPixelStreamingNvVideoEncoder(const FVideoEncoderSettings& InSettings, const FTexture2DRHIRef& BackBuffer, const FEncodedFrameReadyCallback& InEncodedFrameReadyCallback);
	~FPixelStreamingNvVideoEncoder();

	/**
	 * Check to see if the Nvidia NVENC Video Encoder is available on the
	 * platform we are running on.
	 */
	static bool CheckPlatformCompatibility();

	/**
	* Return name of the encoder.
	*/
	virtual FString GetName() const override
	{ return TEXT("Nvidia Video Codec SDK Encoder"); }

	/**
	* If encoder is supported.
	*/
	virtual bool IsSupported() const override;

	/**
	* Get Sps/Pps header data.
	*/
	virtual const TArray<uint8>& GetSpsPpsHeader() const override;

	/**
	* Encode an input back buffer.
	*/
	virtual void EncodeFrame(const FVideoEncoderSettings& Settings, const FTexture2DRHIRef& BackBuffer, uint64 CaptureMs) override;

	/**
	* Force the next frame to be an IDR frame.
	*/
	virtual void ForceIdrFrame() override;

	/**
	* If encoder is running in async/sync mode.
	*/
	virtual bool IsAsyncEnabled() const override;

private:
	class FPixelStreamingNvVideoEncoderImpl;
	FPixelStreamingNvVideoEncoderImpl* NvVideoEncoderImpl;
	void* DllHandle;

	/**
	 * Get the name of the Nvidia NVENC Video Encoder DLL.
	 */
	static const TCHAR* GetDllName();
};

