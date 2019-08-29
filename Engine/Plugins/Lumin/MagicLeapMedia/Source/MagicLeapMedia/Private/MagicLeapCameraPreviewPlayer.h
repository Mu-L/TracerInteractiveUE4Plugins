// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MagicLeapMediaPlayer.h"

/**
 *	Implement media playback using the MagicLeap MediaPlayer interface.
 */
class FMagicLeapCameraPreviewPlayer : public FMagicLeapMediaPlayer
{
public:
	/**
	* Create and initialize a new instance.
	*
	* @param InEventSink The object that receives media events from this player.
	*/
	FMagicLeapCameraPreviewPlayer(IMediaEventSink& InEventSink);

	bool Open(const FString& Url, const IMediaOptions* Options) override;
	bool GetVideoTrackFormat(int32 TrackIndex, int32 FormatIndex, FMediaVideoTrackFormat& OutFormat) const override;
	bool IsLooping() const override;

private:
	bool SetRateOne() override;
	bool GetMediaPlayerState(uint16 FlagToPoll) const override;
	bool RenderThreadIsBufferAvailable(MLHandle MediaPlayerHandle) override;
	bool RenderThreadGetNativeBuffer(const MLHandle MediaPlayerHandle, MLHandle& NativeBuffer) override;
	bool RenderThreadReleaseNativeBuffer(const MLHandle MediaPlayerHandle, MLHandle NativeBuffer) override;
	bool RenderThreadGetCurrentPosition(const MLHandle MediaPlayerHandle, int32& CurrentPosition) override;
};
