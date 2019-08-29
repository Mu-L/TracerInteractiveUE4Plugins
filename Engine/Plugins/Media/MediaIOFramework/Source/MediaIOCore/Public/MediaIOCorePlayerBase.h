// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "IMediaCache.h"
#include "IMediaControls.h"
#include "IMediaPlayer.h"
#include "IMediaTracks.h"
#include "IMediaView.h"

#include "HAL/CriticalSection.h"
#include "Misc/CoreMisc.h"

#include "Misc/FrameRate.h"

class FMediaIOCoreSamples;
class IMediaEventSink;

enum class EMediaTextureSampleFormat;

struct MEDIAIOCORE_API FMediaIOCoreMediaOption
{
	static const FName FrameRateNumerator;
	static const FName FrameRateDenominator;
	static const FName ResolutionWidth;
	static const FName ResolutionHeight;
	static const FName VideoModeName;
};


/**
 * Implements a base player for hardware IO cards. 
 *
 * The processing of metadata and video frames is delayed until the fetch stage
 * (TickFetch) in order to increase the window of opportunity for receiving
 * frames for the current render frame time code.
 *
 * Depending on whether the media source enables time code synchronization,
 * the player's current play time (CurrentTime) is derived either from the
 * time codes embedded in frames or from the Engine's global time code.
 */
class MEDIAIOCORE_API FMediaIOCorePlayerBase
	: public IMediaPlayer
	, protected IMediaCache
	, protected IMediaControls
	, protected IMediaTracks
	, protected IMediaView
	, public FSelfRegisteringExec
{
public:

	/**
	 * Create and initialize a new instance.
	 *
	 * @param InEventSink The object that receives media events from this player.
	 */
	FMediaIOCorePlayerBase(IMediaEventSink& InEventSink);

	/** Virtual destructor. */
	virtual ~FMediaIOCorePlayerBase();

public:

	//~ IMediaPlayer interface

	virtual void Close() override;
	virtual bool Open(const FString& Url, const IMediaOptions* Options) override;
	virtual bool Open(const TSharedRef<FArchive, ESPMode::ThreadSafe>& Archive, const FString& OriginalUrl, const IMediaOptions* Options) override;

	FString GetInfo() const;
	virtual IMediaCache& GetCache() override;
	virtual IMediaControls& GetControls() override;
	virtual IMediaSamples& GetSamples() override;
	const FMediaIOCoreSamples& GetSamples() const;
	virtual FString GetStats() const override;
	virtual IMediaTracks& GetTracks() override;
	virtual FString GetUrl() const override;
	virtual IMediaView& GetView() override;
	virtual void TickTimeManagement();

public:
	//~ IMediaCache interface
	
	virtual bool QueryCacheState(EMediaCacheState State, TRangeSet<FTimespan>& OutTimeRanges) const override;
	virtual int32 GetSampleCount(EMediaCacheState State) const override;

protected:

	//~ IMediaControls interface

	virtual bool CanControl(EMediaControl Control) const override;
	virtual FTimespan GetDuration() const override;
	virtual float GetRate() const override;
	virtual EMediaState GetState() const override;
	virtual EMediaStatus GetStatus() const override;
	virtual TRangeSet<float> GetSupportedRates(EMediaRateThinning Thinning) const override;
	virtual FTimespan GetTime() const override;
	virtual bool IsLooping() const override;
	virtual bool Seek(const FTimespan& Time) override;
	virtual bool SetLooping(bool Looping) override;
	virtual bool SetRate(float Rate) override;

protected:

	//~ IMediaTracks interface

	virtual bool GetAudioTrackFormat(int32 TrackIndex, int32 FormatIndex, FMediaAudioTrackFormat& OutFormat) const override;
	virtual int32 GetNumTracks(EMediaTrackType TrackType) const override;
	virtual int32 GetNumTrackFormats(EMediaTrackType TrackType, int32 TrackIndex) const override;
	virtual int32 GetSelectedTrack(EMediaTrackType TrackType) const override;
	virtual FText GetTrackDisplayName(EMediaTrackType TrackType, int32 TrackIndex) const override;
	virtual int32 GetTrackFormat(EMediaTrackType TrackType, int32 TrackIndex) const override;
	virtual FString GetTrackLanguage(EMediaTrackType TrackType, int32 TrackIndex) const override;
	virtual FString GetTrackName(EMediaTrackType TrackType, int32 TrackIndex) const override;
	virtual bool GetVideoTrackFormat(int32 TrackIndex, int32 FormatIndex, FMediaVideoTrackFormat& OutFormat) const override;
	virtual bool SelectTrack(EMediaTrackType TrackType, int32 TrackIndex) override;
	virtual bool SetTrackFormat(EMediaTrackType TrackType, int32 TrackIndex, int32 FormatIndex) override;


protected:

	//~ FSelfRegisteringExec
	
	virtual bool Exec(class UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override;

protected:
	virtual bool IsHardwareReady() const = 0;

	/** Return true if the options combination are valid */
	virtual bool ReadMediaOptions(const IMediaOptions* Options);

protected:
	/** Critical section for synchronizing access to receiver and sinks. */
	FCriticalSection CriticalSection;

	/** Enable timecode logging */
	bool bIsTimecodeLogEnable;

	/** Url used to open the media player */
	FString OpenUrl;

	/** format of the video */
	FMediaVideoTrackFormat VideoTrackFormat;
	
	/** format of the audio */
	FMediaAudioTrackFormat AudioTrackFormat;

	/** Current state of the media player. */
	EMediaState CurrentState;

	/** Current playback time. */
	FTimespan CurrentTime;

	/** The media event handler. */
	IMediaEventSink& EventSink;

	/** Video frame rate in the last received sample. */
	FFrameRate VideoFrameRate;

	/** The media sample cache. */
	FMediaIOCoreSamples* Samples;

	/** Whether to use the Synchronization Time module as time source. */
	bool bUseTimeSynchronization;

	/** Previous frame Timespan */
	FTimespan PreviousFrameTimespan;
};


