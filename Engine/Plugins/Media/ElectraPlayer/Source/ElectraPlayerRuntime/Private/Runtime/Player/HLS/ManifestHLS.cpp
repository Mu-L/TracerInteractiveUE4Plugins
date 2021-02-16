// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerCore.h"
#include "ManifestHLS.h"
#include "ManifestBuilderHLS.h"
#include "PlaylistReaderHLS.h"
#include "InitSegmentCacheHLS.h"
#include "StreamReaderHLSfmp4.h"
#include "Player/PlayerSessionServices.h"
#include "SynchronizedClock.h"
#include "Utilities/StringHelpers.h"
#include "Utilities/URLParser.h"


namespace Electra
{

/**
 * Interface to a playback period.
 */
class FPlayPeriodHLS : public IManifest::IPlayPeriod
{
public:
	FPlayPeriodHLS(IPlayerSessionServices* SessionServices, IPlaylistReaderHLS* PlaylistReader, TSharedPtrTS<FManifestHLSInternal> Manifest);
	virtual ~FPlayPeriodHLS();

	virtual void SetStreamPreferences(const FStreamPreferences& Preferences) override;

	virtual EReadyState GetReadyState() override;
	virtual void PrepareForPlay(const FParamDict& Options) override;

	// TODO: need to provide metadata (duration, streams, languages, etc.)

	virtual IManifest::FResult GetStartingSegment(TSharedPtrTS<IStreamSegment>& OutSegment, const FPlayStartPosition& StartPosition, IManifest::ESearchType SearchType) override;
	virtual IManifest::FResult GetNextSegment(TSharedPtrTS<IStreamSegment>& OutSegment, TSharedPtrTS<const IStreamSegment> CurrentSegment, const FParamDict& Options) override;
	virtual IManifest::FResult GetRetrySegment(TSharedPtrTS<IStreamSegment>& OutSegment, TSharedPtrTS<const IStreamSegment> CurrentSegment, const FParamDict& Options) override;
	virtual IManifest::FResult GetLoopingSegment(TSharedPtrTS<IStreamSegment>& OutSegment, FPlayerLoopState& InOutLoopState, const TMultiMap<EStreamType, TSharedPtrTS<IStreamSegment>>& InFinishedSegments, const FPlayStartPosition& StartPosition, IManifest::ESearchType SearchType) override;

	// Obtains information on the stream segmentation of a particular stream starting at a given current reference segment (optional, if not given returns suitable default values).
	virtual void GetSegmentInformation(TArray<FSegmentInformation>& OutSegmentInformation, FTimeValue& OutAverageSegmentDuration, TSharedPtrTS<const IStreamSegment> CurrentSegment, const FTimeValue& LookAheadTime, const TSharedPtrTS<IPlaybackAssetAdaptationSet>& AdaptationSet, const TSharedPtrTS<IPlaybackAssetRepresentation>& Representation) override;

	virtual TSharedPtrTS<ITimelineMediaAsset> GetMediaAsset() const override;

	virtual void SelectStream(const TSharedPtrTS<IPlaybackAssetAdaptationSet>& AdaptationSet, const TSharedPtrTS<IPlaybackAssetRepresentation>& Representation, const FString& PreferredCDN) override;

private:
	struct FSegSearchParam
	{
		FSegSearchParam()
			: MediaSequence(-1)
			, DiscontinuitySequence(-1)
			, LocalIndex(-1)
			, StreamUniqueID(0)
		{
		}
		FTimeValue	Time;						//!< Time to search for.
		FTimeValue	Duration;					//!< If set this we search for a start time of Time + Duration (aka the next segment)
		int64		MediaSequence;				//!< If >= 0 we are searching for a specific segment based on media sequence number
		int64		DiscontinuitySequence;		//!< If >= 0 we are searching for a segment after the this discontinuity sequence number
		int32		LocalIndex;
		uint32		StreamUniqueID;				//!< If != 0 the search is for the same stream as was the previous segment. We can use the media sequence index.
		//FTimeValue	PTS;
	};

	void LogMessage(IInfoLog::ELevel Level, const FString& Message);

	IManifest::FResult GetMediaStreamForID(TSharedPtrTS<FManifestHLSInternal::FPlaylistBase>& OutPlaylist, TSharedPtrTS<FManifestHLSInternal::FMediaStream>& OutMediaStream, uint32 UniqueID) const;

	IManifest::FResult GetNextOrRetrySegment(TSharedPtrTS<IStreamSegment>& OutSegment, TSharedPtrTS<const IStreamSegment> CurrentSegment, bool bRetry);

	IManifest::FResult FindSegment(TSharedPtrTS<FStreamSegmentRequestHLSfmp4>& OutRequest, TSharedPtrTS<FManifestHLSInternal::FPlaylistBase> InPlaylist, TSharedPtrTS<FManifestHLSInternal::FMediaStream> InStream, uint32 StreamUniqueID, EStreamType StreamType, const FSegSearchParam& SearchParam, IManifest::ESearchType SearchType);

	void RefreshBlacklistState();

	TSharedPtrTS<FManifestHLSInternal>			InternalManifest;
	IPlayerSessionServices* 					SessionServices;
	IPlaylistReaderHLS*							PlaylistReader;
	EReadyState									CurrentReadyState;

	uint32										ActiveVideoUniqueID;
	uint32										ActiveAudioUniqueID;
};



TSharedPtrTS<FManifestHLS> FManifestHLS::Create(IPlayerSessionServices* SessionServices, const FParamDict& Options, IPlaylistReaderHLS* PlaylistReader, TSharedPtrTS<FManifestHLSInternal> Manifest)
{
	return TSharedPtrTS<FManifestHLS>(new FManifestHLS(SessionServices, Options, PlaylistReader, Manifest));
}

FManifestHLS::FManifestHLS(IPlayerSessionServices* InSessionServices, const FParamDict& InOptions, IPlaylistReaderHLS* InPlaylistReader, TSharedPtrTS<FManifestHLSInternal> Manifest)
	: Options(InOptions)
	, InternalManifest(Manifest)
	, SessionServices(InSessionServices)
	, PlaylistReader(InPlaylistReader)
{
}

FManifestHLS::~FManifestHLS()
{
}

IManifest::EType FManifestHLS::GetPresentationType() const
{
	FManifestHLSInternal::ScopedLockPlaylists lock(InternalManifest);
	return InternalManifest->MasterPlaylistVars.PresentationType;
}


TSharedPtrTS<IPlaybackAssetTimeline> FManifestHLS::GetTimeline() const
{
	FManifestHLSInternal::ScopedLockPlaylists lock(InternalManifest);
	return InternalManifest->PlaybackTimeline;
}


/**
 * Returns the bitrate of the default stream (usually the first one specified).
 *
 * @return
 */
int64 FManifestHLS::GetDefaultStartingBitrate() const
{
	FManifestHLSInternal::ScopedLockPlaylists lock(InternalManifest);
	if (InternalManifest->VariantStreams.Num())
	{
		return InternalManifest->VariantStreams[0]->Bandwidth;
	}
	return 0;
}

/**
 * Returns stream metadata. For period based presentations the streams can be different per period in which case the metadata of the first period is returned.
 *
 * @param OutMetadata
 * @param StreamType
 */
void FManifestHLS::GetStreamMetadata(TArray<FStreamMetadata>& OutMetadata, EStreamType StreamType) const
{
	FManifestHLSInternal::ScopedLockPlaylists lock(InternalManifest);
	switch(StreamType)
	{
		case EStreamType::Video:
			OutMetadata = InternalManifest->StreamMetadataVideo;
			break;
		case EStreamType::Audio:
			OutMetadata = InternalManifest->StreamMetadataAudio;
			break;
		case EStreamType::Subtitle:
			OutMetadata.Empty();
			break;
	}
}


/**
 * Returns the duration that should be present in the buffers at all times
 * (except for the end of the presentation).
 *
 * @return
 */
FTimeValue FManifestHLS::GetMinBufferTime() const
{
	// HLS does not offer a minimum duration to be in the buffers at all times. For expedited startup we use 2 seconds here.
	return FTimeValue().SetFromSeconds(2.0);
}





/**
 * Returns a play period for the specified start time.
 * Since we are not currently splitting the media timeline into individual periods
 * we simply return a new period here regardless of the starting time.
 *
 * @param OutPlayPeriod
 * @param StartPosition
 * @param SearchType
 *
 * @return
 */
IManifest::FResult FManifestHLS::FindPlayPeriod(TSharedPtrTS<IPlayPeriod>& OutPlayPeriod, const FPlayStartPosition& StartPosition, ESearchType SearchType)
{
	TSharedPtrTS<FPlayPeriodHLS> Period(new FPlayPeriodHLS(SessionServices, PlaylistReader, InternalManifest));
	OutPlayPeriod = Period;
	return IManifest::FResult(IManifest::FResult::EType::Found);
}



/**
 * Creates a stream reader for the media segments.
 *
 * @return
 */
IStreamReader* FManifestHLS::CreateStreamReaderHandler()
{
	return new FStreamReaderHLSfmp4;
}










FPlayPeriodHLS::FPlayPeriodHLS(IPlayerSessionServices* InSessionServices, IPlaylistReaderHLS* InPlaylistReader, TSharedPtrTS<FManifestHLSInternal> Manifest)
	: InternalManifest(Manifest)
	, SessionServices(InSessionServices)
	, PlaylistReader(InPlaylistReader)
{
	check(PlaylistReader);

	CurrentReadyState   			  = IManifest::IPlayPeriod::EReadyState::NotReady;

	// Set the active video and audio stream IDs to 0, which means none are selected.
	ActiveVideoUniqueID = 0;
	ActiveAudioUniqueID = 0;
}

FPlayPeriodHLS::~FPlayPeriodHLS()
{
}


void FPlayPeriodHLS::LogMessage(IInfoLog::ELevel Level, const FString& Message)
{
	if (SessionServices)
	{
		SessionServices->PostLog(Facility::EFacility::HLSManifest, Level, Message);
	}
}

/**
 * Sets stream preferences.
 *
 * @param Preferences
 */
void FPlayPeriodHLS::SetStreamPreferences(const FStreamPreferences& Preferences)
{
}



/**
 * Returns the current ready state of the period.
 *
 * @return
 */
IManifest::IPlayPeriod::EReadyState FPlayPeriodHLS::GetReadyState()
{
	return CurrentReadyState;
}



/**
 * Prepares the period for playback.
 *
 * @param Options
 */
void FPlayPeriodHLS::PrepareForPlay(const FParamDict& Options)
{
	// For now we just go with the streams for which we loaded the playlists initially.
	// FIXME: in the future, based on preferences and options, select the streams we want.

	FManifestHLSInternal::ScopedLockPlaylists lock(InternalManifest);

	uint32 OldVideoUniqueID = ActiveVideoUniqueID;
	uint32 OldAudioUniqueID = ActiveAudioUniqueID;

	for(int32 i=0; i<InternalManifest->VariantStreams.Num(); ++i)
	{
		if (InternalManifest->VariantStreams[i]->Internal.LoadState == FManifestHLSInternal::FPlaylistBase::FInternal::ELoadState::Loaded)
		{
			ActiveVideoUniqueID = InternalManifest->VariantStreams[i]->Internal.UniqueID;
			break;
		}
	}

	for (TMultiMap<FString, TSharedPtrTS<FManifestHLSInternal::FRendition>>::TConstIterator It = InternalManifest->AudioRenditions.CreateConstIterator(); It; ++It)
	{
		if (It.Value()->Internal.LoadState == FManifestHLSInternal::FPlaylistBase::FInternal::ELoadState::Loaded)
		{
			ActiveAudioUniqueID = It.Value()->Internal.UniqueID;
			break;
		}
	}
	// In case there is an audio rendition without a dedicated playlist we look at audio-only variant streams
	if (ActiveAudioUniqueID == 0)
	{
		for(int32 i=0; i<InternalManifest->AudioOnlyStreams.Num(); ++i)
		{
			if (InternalManifest->AudioOnlyStreams[i]->Internal.LoadState == FManifestHLSInternal::FPlaylistBase::FInternal::ELoadState::Loaded)
			{
				ActiveAudioUniqueID = InternalManifest->AudioOnlyStreams[i]->Internal.UniqueID;
				break;
			}
		}
	}

	// Tell the manifest which stream IDs are now actively used.
	InternalManifest->SelectActiveStreamID(ActiveVideoUniqueID, OldVideoUniqueID);
	InternalManifest->SelectActiveStreamID(ActiveAudioUniqueID, OldAudioUniqueID);

	CurrentReadyState = IManifest::IPlayPeriod::EReadyState::IsReady;
}



/**
 * Returns the media stream for the specified ID.
 *
 * @param OutPlaylist
 * @param OutMediaStream
 * @param UniqueID
 *
 * @return
 */
IManifest::FResult FPlayPeriodHLS::GetMediaStreamForID(TSharedPtrTS<FManifestHLSInternal::FPlaylistBase>& OutPlaylist, TSharedPtrTS<FManifestHLSInternal::FMediaStream>& OutMediaStream, uint32 UniqueID) const
{
	FManifestHLSInternal::ScopedLockPlaylists lock(InternalManifest);
	check(UniqueID);
	if (UniqueID)
	{
		TWeakPtrTS<FManifestHLSInternal::FPlaylistBase>* PlaylistID = InternalManifest->PlaylistIDMap.Find(UniqueID);
		if (PlaylistID != nullptr)
		{
			TSharedPtrTS<FManifestHLSInternal::FPlaylistBase> Playlist = PlaylistID->Pin();

			if (Playlist.IsValid())
			{
				// Sanity check the ID
				if (Playlist->Internal.UniqueID == UniqueID)
				{
					OutPlaylist = Playlist;

					// Playlist currently blacklisted?
					if (Playlist->Internal.Blacklisted.IsValid())
					{
						// Return and assume a non-blacklisted stream will be selected.
						return IManifest::FResult().RetryAfterMilliseconds(50);
					}
					// Check the load state
					switch(Playlist->Internal.LoadState)
					{
						case FManifestHLSInternal::FPlaylistBase::FInternal::ELoadState::Loaded:
						{
							TSharedPtrTS<FManifestHLSInternal::FMediaStream>	MediaStream = Playlist->Internal.MediaStream;
							// The stream really better be there!
							if (MediaStream.IsValid())
							{
								OutMediaStream = MediaStream;
								return IManifest::FResult(IManifest::FResult::EType::Found);
							}
							else
							{
								return IManifest::FResult(IManifest::FResult::EType::NotFound).SetErrorDetail(FErrorDetail().SetMessage(FString::Printf(TEXT("Media stream for unique ID %u is not present!"), UniqueID)));
							}
						}
						case FManifestHLSInternal::FPlaylistBase::FInternal::ELoadState::NotLoaded:
						{
							return IManifest::FResult(IManifest::FResult::EType::NotLoaded);
						}
						case FManifestHLSInternal::FPlaylistBase::FInternal::ELoadState::Pending:
						{
							return IManifest::FResult().RetryAfterMilliseconds(50);
						}
					}
					// Should never get here, but if we do let's bail gracefully.
					return IManifest::FResult(IManifest::FResult::EType::NotFound).SetErrorDetail(FErrorDetail().SetMessage("Internal error. Unhandled switch case"));
				}
				else
				{
					return IManifest::FResult(IManifest::FResult::EType::NotFound).SetErrorDetail(FErrorDetail().SetMessage(FString::Printf(TEXT("Playlist unique ID %u does not match requested ID of %u"), Playlist->Internal.UniqueID, UniqueID)));
				}
			}
			else
			{
				return IManifest::FResult(IManifest::FResult::EType::NotFound).SetErrorDetail(FErrorDetail().SetMessage(FString::Printf(TEXT("Playlist for unique ID %u has been destroyed"), UniqueID)));
			}
		}
		else
		{
			return IManifest::FResult(IManifest::FResult::EType::NotFound).SetErrorDetail(FErrorDetail().SetMessage(FString::Printf(TEXT("No media stream found for unique ID %u"), UniqueID)));
		}
	}
	else
	{
		return IManifest::FResult(IManifest::FResult::EType::NotFound).SetErrorDetail(FErrorDetail().SetMessage(FString::Printf(TEXT("Invalid unique media stream ID %u"), UniqueID)));
	}
}





/**
 * Locate a segment in the stream's playlist.
 *
 * @param OutRequest
 * @param InPlaylist
 * @param InStream
 * @param StreamUniqueID
 * @param StreamType
 * @param SearchParam
 * @param SearchType
 *
 * @return
 */
IManifest::FResult FPlayPeriodHLS::FindSegment(TSharedPtrTS<FStreamSegmentRequestHLSfmp4>& OutRequest, TSharedPtrTS<FManifestHLSInternal::FPlaylistBase> InPlaylist, TSharedPtrTS<FManifestHLSInternal::FMediaStream> InStream, uint32 StreamUniqueID, EStreamType StreamType, const FPlayPeriodHLS::FSegSearchParam& SearchParam, IManifest::ESearchType SearchType)
{
	// VOD or EVENT playlist?
///////	if (InStream->PlaylistType == FManifestHLSInternal::FMediaStream::EPlaylistType::VOD || InStream->PlaylistType == FManifestHLSInternal::FMediaStream::EPlaylistType::Event)


	TUniquePtr<IURLParser> UrlBuilder(IURLParser::Create());
	UrlBuilder->ParseURL(InPlaylist->Internal.PlaylistLoadRequest.URL);
	TSharedPtrTS<FStreamSegmentRequestHLSfmp4>	Req(new FStreamSegmentRequestHLSfmp4);
	Req->StreamType 	= StreamType;
	Req->StreamUniqueID = StreamUniqueID;
	if (InternalManifest->PlaybackTimeline.IsValid())
	{
		Req->MediaAsset = InternalManifest->PlaybackTimeline->GetMediaAssetByIndex(0);
		if (!Req->MediaAsset.IsValid())
		{
			return IManifest::FResult(IManifest::FResult::EType::NotFound).SetErrorDetail(FErrorDetail().SetMessage("Internal error, media asset not found on asset timeline!"));
		}
		if (Req->MediaAsset->GetNumberOfAdaptationSets(StreamType) > 1)
		{
			return IManifest::FResult(IManifest::FResult::EType::NotFound).SetErrorDetail(FErrorDetail().SetMessage(FString::Printf(TEXT("Internal error, more than one %s rendition group found on asset timeline!"), GetStreamTypeName(StreamType))));
		}
		Req->AdaptationSet = Req->MediaAsset->GetAdaptationSetByTypeAndIndex(StreamType, 0);
		if (!Req->AdaptationSet.IsValid())
		{
			return IManifest::FResult(IManifest::FResult::EType::NotFound).SetErrorDetail(FErrorDetail().SetMessage(FString::Printf(TEXT("Internal error, no %s rendition group found on asset timeline!"), GetStreamTypeName(StreamType))));
		}
		Req->Representation = Req->AdaptationSet->GetRepresentationByUniqueIdentifier(LexToString(StreamUniqueID));
		if (!Req->Representation.IsValid())
		{
			return IManifest::FResult(IManifest::FResult::EType::NotFound).SetErrorDetail(FErrorDetail().SetMessage(FString::Printf(TEXT("Internal error, %s rendition not found in group on asset timeline!"), GetStreamTypeName(StreamType))));
		}
	}
	Req->InitSegmentCache   	   = InternalManifest->InitSegmentCache;
	Req->LicenseKeyCache		   = InternalManifest->LicenseKeyCache;
	Req->bHasEncryptedSegments     = InStream->bHasEncryptedSegments;
	if (StreamType == EStreamType::Video)
	{
		Req->Bitrate				   = InPlaylist->GetBitrate();
		check(InternalManifest->BandwidthToQualityIndex.Find(Req->Bitrate) != nullptr);
		Req->QualityLevel   		   = InternalManifest->BandwidthToQualityIndex[Req->Bitrate];
	}

	const TArray<FManifestHLSInternal::FMediaStream::FMediaSegment>& SegmentList = InStream->SegmentList;
	if (SegmentList.Num())
	{
		FTimeValue searchTime = SearchParam.Time;
		int32 SelectedSegmentIndex = -1;

		// Searching for the next segment within the same stream?
		if (SearchParam.StreamUniqueID != 0)
		{
			check(SearchType == IManifest::ESearchType::StrictlyAfter || SearchType == IManifest::ESearchType::Same);
			if (SearchType != IManifest::ESearchType::StrictlyAfter && SearchType != IManifest::ESearchType::Same)
			{
				return IManifest::FResult(IManifest::FResult::EType::NotFound).SetErrorDetail(FErrorDetail().SetMessage("Can only find next or retry segment in same stream right now"));
			}

			int64 NextSequenceNumber;
			if (SearchType == IManifest::ESearchType::Same)
			{
				NextSequenceNumber = SearchParam.MediaSequence;
			}
			else
			{
				NextSequenceNumber = SearchParam.MediaSequence + 1;
			}
			// We can use the media sequence number.
			for(int32 i=0,iMax=SegmentList.Num(); i<iMax; ++i)
			{
				if (SegmentList[i].SequenceNumber >= NextSequenceNumber)
				{
					SelectedSegmentIndex = i;
					break;
				}
			}
		}
		else
		{
			for(int32 i=0,iMax=SegmentList.Num(); i<iMax; ++i)
			{
				// Find the segment whose start time is >= the time we're looking for.
				if (SegmentList[i].AbsoluteDateTime >= searchTime)
				{
					// Do we want the segment with start time >= the search time?
					if (SearchType == IManifest::ESearchType::After)
					{
						// Yes, we're done.
						SelectedSegmentIndex = i;
						break;
					}
					// Do we want the segment with start time > the time we're looking for?
					else if (SearchType == IManifest::ESearchType::StrictlyAfter)
					{
						// Only go forward if we did hit the search time exactly ( == ) and we're not on the last first segment already!
						if (SegmentList[i].AbsoluteDateTime == searchTime)
						{
							// Continue the loop. The next segment, if it exists, will have a greater search time and we'll catch it then.
							continue;
						}
						SelectedSegmentIndex = i;
						break;
					}
					// Do we want the segment with start time <= the search time?
					else if (SearchType == IManifest::ESearchType::Before)
					{
						SelectedSegmentIndex = i;
						// Only go back if we did not hit the search time exactly ( == ) and we're not on the very first segment already!
						if (SegmentList[i].AbsoluteDateTime > searchTime && i > 0)
						{
							--SelectedSegmentIndex;
						}
						break;
					}
					// Do we want the segment with start time < the search time?
					else if (SearchType == IManifest::ESearchType::StrictlyBefore)
					{
						// If we cannot go back one segment we can return.
						if (i == 0)
						{
							return IManifest::FResult(IManifest::FResult::EType::BeforeStart);
						}
						SelectedSegmentIndex = i-1;
						break;
					}
					// Do we want the segment whose start time is closest to the search time?
					else if (SearchType == IManifest::ESearchType::Closest)
					{
						SelectedSegmentIndex = i;
						// If there is an earlier segment we can check which one is closer.
						if (i > 0)
						{
							FTimeValue diffHere   = SegmentList[i].AbsoluteDateTime - searchTime;
							FTimeValue diffBefore = searchTime - SegmentList[i - 1].AbsoluteDateTime;
							// In the exceptionally rare case the difference to either segment is the same we pick the earlier one.
							if (diffBefore <= diffHere)
							{
								--SelectedSegmentIndex;
							}
						}
						break;
					}
					// Do we want the segment for the exact same start time as the search time?
					else if (SearchType == IManifest::ESearchType::Same)
					{
						// This is used for retrying a failed segment. Usually on another quality level or CDN.
						// To allow for slight variations in the time we do a 'closest' search if the exact time can't be found.
						SelectedSegmentIndex = i;
						// If we hit the time dead on we are done.
						if (SegmentList[i].AbsoluteDateTime == searchTime)
						{
							break;
						}
						// Otherwise we do the same as for the 'closest' search.
						if (i > 0)
						{
							FTimeValue diffHere   = SegmentList[i].AbsoluteDateTime - searchTime;
							FTimeValue diffBefore = searchTime - SegmentList[i - 1].AbsoluteDateTime;
							// In the exceptionally rare case the difference to either segment is the same we pick the earlier one.
							if (diffBefore <= diffHere)
							{
								--SelectedSegmentIndex;
							}
						}
						break;
					}

					else
					{
						checkNoEntry();
						return IManifest::FResult(IManifest::FResult::EType::NotFound).SetErrorDetail(FErrorDetail().SetMessage("Internal error, unsupported segment search mode!"));
					}
				}
			}
			// If not found but there are segments in the list they all have a start time smaller than we are looking for.
			// Let's look at the last segment to see if the search time falls into the duration of it.
			if (SelectedSegmentIndex < 0 && SegmentList.Num())
			{
				// Whether we can use the last segment also depends on the search mode.
				if (SearchType == IManifest::ESearchType::Closest ||
					SearchType == IManifest::ESearchType::Before ||
					SearchType == IManifest::ESearchType::StrictlyBefore)
				{
					int32 LastSegmentIndex = SegmentList.Num() - 1;
					if (searchTime < SegmentList[LastSegmentIndex].AbsoluteDateTime + SegmentList[LastSegmentIndex].Duration)
					{
						SelectedSegmentIndex = LastSegmentIndex;
					}
				}
			}
		}

		// Did we find the segment?
		if (SelectedSegmentIndex >= 0 && SelectedSegmentIndex < SegmentList.Num())
		{
			//Req->PlaylistRelativeStartTime = SegmentList[SelectedSegmentIndex].RelativeStartTime;
			Req->AbsoluteDateTime   	   = SegmentList[SelectedSegmentIndex].AbsoluteDateTime;
			Req->SegmentDuration		   = SegmentList[SelectedSegmentIndex].Duration;
			Req->MediaSequence  		   = SegmentList[SelectedSegmentIndex].SequenceNumber;
			Req->DiscontinuitySequence     = SegmentList[SelectedSegmentIndex].DiscontinuityCount;
			Req->LocalIndex 			   = SelectedSegmentIndex;
			Req->bIsPrefetch			   = SegmentList[SelectedSegmentIndex].bIsPrefetch;
			Req->bIsEOSSegment  		   = false;
			Req->URL					   = UrlBuilder->ResolveWith(SegmentList[SelectedSegmentIndex].URI);
			Req->FirstAUTimeOffset  	   = searchTime - SegmentList[SelectedSegmentIndex].AbsoluteDateTime;
			Req->InitSegmentInfo		   = SegmentList[SelectedSegmentIndex].InitSegmentInfo;
			Req->LicenseKeyInfo 		   = SegmentList[SelectedSegmentIndex].DRMKeyInfo;

			// This can be negative when we're picking the segment after the search time!
			// But we don't want this since it is a useless case. We will be receiving AUs from behind the search time and
			// that's simply where we are starting at. Set to zero in this case.
			if (Req->FirstAUTimeOffset < FTimeValue::GetZero())
			{
				Req->FirstAUTimeOffset.SetToZero();
			}

			if (SegmentList[SelectedSegmentIndex].ByteRange.IsSet())
			{
				Req->Range.Start		= SegmentList[SelectedSegmentIndex].ByteRange.GetStart();
				Req->Range.EndIncluding = SegmentList[SelectedSegmentIndex].ByteRange.GetEnd();
			}
			OutRequest = Req;
			return IManifest::FResult(IManifest::FResult::EType::Found);
		}
		else
		{
			// Not having found a segment means we're beyond this presentation.
			// Unless this is a VOD list or it has as ENDLIST tag we have to try this later, assuming that an updated playlist has added additional segments.
			if (InStream->PlaylistType == FManifestHLSInternal::FMediaStream::EPlaylistType::VOD || InStream->bHasListEnd)
			{
				Req->bIsEOSSegment = true;
				OutRequest = Req;
				return IManifest::FResult(IManifest::FResult::EType::PastEOS);
			}
			else
			{
				// Try again after half a target duration.
				return IManifest::FResult(IManifest::FResult::EType::TryAgainLater).RetryAfterMilliseconds(InStream->TargetDuration.GetAsMilliseconds() / 2);
			}
		}
	}
	else
	{
		// No segments is not really expected. If this occurs we assume the presentation has ended.
		Req->bIsEOSSegment = true;
		OutRequest = Req;
		return IManifest::FResult(IManifest::FResult::EType::PastEOS);
	}
	return IManifest::FResult(IManifest::FResult::EType::NotFound);
}


IManifest::FResult FPlayPeriodHLS::GetStartingSegment(TSharedPtrTS<IStreamSegment>& OutSegment, const FPlayStartPosition& StartPosition, IManifest::ESearchType SearchType)
{
	FManifestHLSInternal::ScopedLockPlaylists lock(InternalManifest);

	RefreshBlacklistState();

	TSharedPtrTS<FManifestHLSInternal::FPlaylistBase>	VideoPlaylist;
	TSharedPtrTS<FManifestHLSInternal::FPlaylistBase>	AudioPlaylist;
	TSharedPtrTS<FManifestHLSInternal::FMediaStream>	VideoStream;
	TSharedPtrTS<FManifestHLSInternal::FMediaStream>	AudioStream;

	// Get the streams that are selected, if there are selected ones.
	IManifest::FResult vidResult = ActiveVideoUniqueID ? GetMediaStreamForID(VideoPlaylist, VideoStream, ActiveVideoUniqueID) : IManifest::FResult(IManifest::FResult::EType::Found);
	IManifest::FResult audResult = ActiveAudioUniqueID ? GetMediaStreamForID(AudioPlaylist, AudioStream, ActiveAudioUniqueID) : IManifest::FResult(IManifest::FResult::EType::Found);
	if (vidResult.IsSuccess() && audResult.IsSuccess())
	{
		TSharedPtrTS<FStreamSegmentRequestHLSfmp4> 	VideoSegmentRequest;
		TSharedPtrTS<FStreamSegmentRequestHLSfmp4> 	AudioSegmentRequest;
		FSegSearchParam									SearchParam;

		SearchParam.Time = StartPosition.Time;

		// Do we have both video and audio?
		if (ActiveVideoUniqueID && ActiveAudioUniqueID)
		{
			vidResult = FindSegment(VideoSegmentRequest, VideoPlaylist, VideoStream, ActiveVideoUniqueID, EStreamType::Video, SearchParam, SearchType);
			// Found and PastEOS are valid results here. Everything else is not.
			if (vidResult.GetType() != IManifest::FResult::EType::Found && vidResult.GetType() != IManifest::FResult::EType::PastEOS)
			{
				return vidResult;
			}
			// If the search for video was successful we adjust the search parameters for the audio stream.
			if (vidResult.IsSuccess())
			{
				VideoSegmentRequest->bIsInitialStartRequest = true;
				VideoSegmentRequest->FirstAUTimeOffset.SetToZero();

				// With the video segment found let's find the corresponding audio segment.
				SearchParam.Time = VideoSegmentRequest->AbsoluteDateTime;
				SearchParam.DiscontinuitySequence = VideoSegmentRequest->DiscontinuitySequence;
				// For audio we start with the segment before the video segment if there is no precise match.
				// The stream reader will skip over all audio access units before the intended start time.
				SearchType = IManifest::ESearchType::Before;
			}
			// Search for audio.
			audResult = FindSegment(AudioSegmentRequest, AudioPlaylist, AudioStream, ActiveAudioUniqueID, EStreamType::Audio, SearchParam, SearchType);
			// Equally here, if successful or PastEOS is acceptable and everything else is not.
			if (audResult.GetType() != IManifest::FResult::EType::Found && audResult.GetType() != IManifest::FResult::EType::PastEOS)
			{
				return audResult;
			}

			// Both segments found?
			if (vidResult.IsSuccess() && audResult.IsSuccess())
			{
				AudioSegmentRequest->bIsInitialStartRequest = true;
				VideoSegmentRequest->DependentStreams.Push(AudioSegmentRequest);
				OutSegment = VideoSegmentRequest;
				return IManifest::FResult(IManifest::FResult::EType::Found);
			}
			// Only audio found?
			else if (audResult.IsSuccess())
			{
				AudioSegmentRequest->bIsInitialStartRequest = true;
				AudioSegmentRequest->DependentStreams.Push(VideoSegmentRequest);
				OutSegment = AudioSegmentRequest;
				return IManifest::FResult(IManifest::FResult::EType::Found);
			}
			// Only video found? Or neither?
			else //if (vidResult.IsSuccess())
			{
				VideoSegmentRequest->bIsInitialStartRequest = true;
				VideoSegmentRequest->DependentStreams.Push(AudioSegmentRequest);
				OutSegment = VideoSegmentRequest;
				return IManifest::FResult(IManifest::FResult::EType::Found);
			}
		}
		// Video only?
		else if (ActiveVideoUniqueID)
		{
			vidResult = FindSegment(VideoSegmentRequest, VideoPlaylist, VideoStream, ActiveVideoUniqueID, EStreamType::Video, SearchParam, SearchType);
			if (vidResult.IsSuccess())
			{
				VideoSegmentRequest->bIsInitialStartRequest = true;
				VideoSegmentRequest->FirstAUTimeOffset.SetToZero();
				OutSegment = VideoSegmentRequest;
				return IManifest::FResult(IManifest::FResult::EType::Found);
			}
			else
			{
				return vidResult;
			}
		}
		// Audio only
		else
		{
			audResult = FindSegment(AudioSegmentRequest, AudioPlaylist, AudioStream, ActiveAudioUniqueID, EStreamType::Audio, SearchParam, SearchType);
			if (audResult.IsSuccess())
			{
				AudioSegmentRequest->bIsInitialStartRequest = true;
				OutSegment = AudioSegmentRequest;
				return IManifest::FResult(IManifest::FResult::EType::Found);
			}
			else
			{
				return audResult;
			}
		}
	}
	else
	{
		// Either playlist not yet loaded?
		if (vidResult.GetType() == IManifest::FResult::EType::NotLoaded)
		{
			check(VideoPlaylist.IsValid());

			VideoPlaylist->Internal.LoadState = FManifestHLSInternal::FPlaylistBase::FInternal::ELoadState::Pending;

			TUniquePtr<IURLParser> UrlBuilder(IURLParser::Create());
			UrlBuilder->ParseURL(InternalManifest->MasterPlaylistVars.PlaylistLoadRequest.URL);
			FPlaylistLoadRequestHLS Req;
			Req.LoadType			   = FPlaylistLoadRequestHLS::ELoadType::First;
			Req.InternalUniqueID	   = ActiveVideoUniqueID;
			Req.RequestedAtTime 	   = SessionServices->GetSynchronizedUTCTime()->GetTime();
			Req.URL 				   = UrlBuilder->ResolveWith(VideoPlaylist->GetURL());
			Req.AdaptationSetUniqueID  = VideoPlaylist->Internal.AdaptationSetUniqueID;
			Req.RepresentationUniqueID = VideoPlaylist->Internal.RepresentationUniqueID;
			Req.CDN 				   = VideoPlaylist->Internal.CDN;
			PlaylistReader->RequestPlaylistLoad(Req);
			vidResult.RetryAfterMilliseconds(50);
		}
		if (audResult.GetType() == IManifest::FResult::EType::NotLoaded)
		{
			check(AudioPlaylist.IsValid());

			AudioPlaylist->Internal.LoadState = FManifestHLSInternal::FPlaylistBase::FInternal::ELoadState::Pending;

			TUniquePtr<IURLParser> UrlBuilder(IURLParser::Create());
			UrlBuilder->ParseURL(InternalManifest->MasterPlaylistVars.PlaylistLoadRequest.URL);
			FPlaylistLoadRequestHLS Req;
			Req.LoadType			   = FPlaylistLoadRequestHLS::ELoadType::First;
			Req.InternalUniqueID	   = ActiveAudioUniqueID;
			Req.RequestedAtTime 	   = SessionServices->GetSynchronizedUTCTime()->GetTime();
			Req.URL 				   = UrlBuilder->ResolveWith(AudioPlaylist->GetURL());
			Req.AdaptationSetUniqueID  = AudioPlaylist->Internal.AdaptationSetUniqueID;
			Req.RepresentationUniqueID = AudioPlaylist->Internal.RepresentationUniqueID;
			Req.CDN 				   = AudioPlaylist->Internal.CDN;
			PlaylistReader->RequestPlaylistLoad(Req);
			audResult.RetryAfterMilliseconds(50);
		}

		if (vidResult.GetType() == IManifest::FResult::EType::TryAgainLater)
		{
			return vidResult;
		}
		if (audResult.GetType() == IManifest::FResult::EType::TryAgainLater)
		{
			return audResult;
		}
		// If both are a go, go!
		if (vidResult.IsSuccess() && audResult.IsSuccess())
		{
			return vidResult;
		}
		// Return that which is at fault.
		return !vidResult.IsSuccess() ? vidResult : audResult;
	}
}

IManifest::FResult FPlayPeriodHLS::GetLoopingSegment(TSharedPtrTS<IStreamSegment>& OutSegment, FPlayerLoopState& InOutLoopState, const TMultiMap<EStreamType, TSharedPtrTS<IStreamSegment>>& InFinishedSegments, const FPlayStartPosition& StartPosition, IManifest::ESearchType SearchType)
{
	if (InFinishedSegments.Num())
	{
		// Go over all finished segments and get the largest next expected timestamp from all of them.
		FTimeValue LargestNextExpectedTimestamp(FTimeValue::GetZero());
		for(auto It = InFinishedSegments.CreateConstIterator(); It; ++It)
		{
			const FStreamSegmentRequestHLSfmp4* FinishedRequest = static_cast<const FStreamSegmentRequestHLSfmp4*>(It->Value.Get());
			if (FinishedRequest && FinishedRequest->NextLargestExpectedTimestamp > LargestNextExpectedTimestamp)
			{
				LargestNextExpectedTimestamp = FinishedRequest->NextLargestExpectedTimestamp;
			}
		}
		IManifest::FResult res = GetStartingSegment(OutSegment, StartPosition, SearchType);
		if (res.GetType() == IManifest::FResult::EType::Found)
		{
			FStreamSegmentRequestHLSfmp4* LoopRequest = static_cast<FStreamSegmentRequestHLSfmp4*>(OutSegment.Get());
			InOutLoopState.bLoopEnabled = true;
			InOutLoopState.LoopBasetime = LargestNextExpectedTimestamp;	// This is the absolute playback time at which the loop will occur
			++InOutLoopState.LoopCount;
			LoopRequest->PlayerLoopState = InOutLoopState;
			LoopRequest->PlayerLoopState.LoopBasetime -= LoopRequest->AbsoluteDateTime;	// This is the _offset_ to add internally to the PTS to make it loop.
			// Set the loop state in the dependent streams as well.
			for(int32 nDep=0, nDepMax=LoopRequest->DependentStreams.Num(); nDep<nDepMax; ++nDep)
			{
				LoopRequest->DependentStreams[nDep]->PlayerLoopState = LoopRequest->PlayerLoopState;
			}
			return res;
		}
	}
	// Return past EOS when we can't loop to indicate we're really done now.
	return IManifest::FResult(IManifest::FResult::EType::PastEOS);
}



IManifest::FResult FPlayPeriodHLS::GetNextOrRetrySegment(TSharedPtrTS<IStreamSegment>& OutSegment, TSharedPtrTS<const IStreamSegment> InCurrentSegment, bool bRetry)
{
	// Need to have a current segment to find the next one.
	if (!InCurrentSegment.IsValid())
	{
		return IManifest::FResult(IManifest::FResult::EType::NotFound).SetErrorDetail(FErrorDetail().SetMessage("Cannot get next segment without a current segment!"));
	}
	const FStreamSegmentRequestHLSfmp4* CurrentRequest = static_cast<const FStreamSegmentRequestHLSfmp4*>(InCurrentSegment.Get());
	check(CurrentRequest->DependentStreams.Num() == 0);
	if (CurrentRequest->DependentStreams.Num())
	{
		return IManifest::FResult(IManifest::FResult::EType::NotFound).SetErrorDetail(FErrorDetail().SetMessage("Cannot get next segment for a segment with dependent segments!"));
	}
	if (CurrentRequest->StreamUniqueID == 0)
	{
		return IManifest::FResult(IManifest::FResult::EType::NotFound).SetErrorDetail(FErrorDetail().SetMessage("Cannot get next segment for a segment having no unique stream ID!"));
	}

	FManifestHLSInternal::ScopedLockPlaylists lock(InternalManifest);
	uint32 ForStreamID = 0;
	switch(CurrentRequest->GetType())
	{
		case EStreamType::Video:
		{
			ForStreamID = ActiveVideoUniqueID;
			break;
		}
		case EStreamType::Audio:
		{
			ForStreamID = ActiveAudioUniqueID;
			break;
		}
		default:
		{
			return IManifest::FResult(IManifest::FResult::EType::NotFound).SetErrorDetail(FErrorDetail().SetMessage(FString::Printf(TEXT("Cannot get next segment for unsupported stream type \"%s\"!"), GetStreamTypeName(CurrentRequest->GetType()))));
		}
	}
	if (ForStreamID == 0)
	{
		return IManifest::FResult(IManifest::FResult::EType::NotFound).SetErrorDetail(FErrorDetail().SetMessage(FString::Printf(TEXT("Cannot get next segment stream type \"%s\" since no stream is actively selected!"), GetStreamTypeName(CurrentRequest->GetType()))));
	}

	RefreshBlacklistState();

	TSharedPtrTS<FManifestHLSInternal::FMediaStream>	Stream;
	TSharedPtrTS<FManifestHLSInternal::FPlaylistBase> Playlist;
	IManifest::FResult Result = GetMediaStreamForID(Playlist, Stream, ForStreamID);
	if (Result.IsSuccess())
	{
		TSharedPtrTS<FStreamSegmentRequestHLSfmp4> 	NextSegmentRequest;
		FSegSearchParam									SearchParam;

		SearchParam.Time				  = CurrentRequest->AbsoluteDateTime;
		SearchParam.Duration			  = CurrentRequest->SegmentDuration;
		SearchParam.MediaSequence   	  = CurrentRequest->MediaSequence;
		SearchParam.DiscontinuitySequence = CurrentRequest->DiscontinuitySequence;
		SearchParam.LocalIndex  		  = CurrentRequest->LocalIndex;
		SearchParam.StreamUniqueID  	  = CurrentRequest->StreamUniqueID == ForStreamID ? ForStreamID : 0;
		Result = FindSegment(NextSegmentRequest, Playlist, Stream, ForStreamID, CurrentRequest->GetType(), SearchParam, bRetry ? IManifest::ESearchType::Same : IManifest::ESearchType::StrictlyAfter);
		if (Result.IsSuccess())
		{
			// Continuing with the next segment implicitly means there is no AU time offset.
			if (!bRetry)
			{
				NextSegmentRequest->FirstAUTimeOffset.SetToZero();
			}
			else
			{
				NextSegmentRequest->NumOverallRetries = CurrentRequest->NumOverallRetries + 1;
			}
			// Copy over the player loop state.
			NextSegmentRequest->PlayerLoopState = CurrentRequest->PlayerLoopState;
			OutSegment = NextSegmentRequest;
			return IManifest::FResult(IManifest::FResult::EType::Found);
		}
		if (Result.GetType() == IManifest::FResult::EType::TryAgainLater)
		{
			return Result;
		}
		else
		{
			checkf(Result.GetType() == IManifest::FResult::EType::PastEOS, TEXT("What error is this?"));
			return Result;
		}
	}
	else
	{
		// Playlist not loaded?
		if (Result.GetType() == IManifest::FResult::EType::NotLoaded)
		{
			check(Playlist.IsValid());

			Playlist->Internal.LoadState = FManifestHLSInternal::FPlaylistBase::FInternal::ELoadState::Pending;

			TUniquePtr<IURLParser> UrlBuilder(IURLParser::Create());
			UrlBuilder->ParseURL(InternalManifest->MasterPlaylistVars.PlaylistLoadRequest.URL);
			FPlaylistLoadRequestHLS Req;
			Req.LoadType			   = FPlaylistLoadRequestHLS::ELoadType::First;
			Req.InternalUniqueID	   = ForStreamID;
			Req.RequestedAtTime 	   = SessionServices->GetSynchronizedUTCTime()->GetTime();
			Req.URL 				   = UrlBuilder->ResolveWith(Playlist->GetURL());
			Req.AdaptationSetUniqueID  = Playlist->Internal.AdaptationSetUniqueID;
			Req.RepresentationUniqueID = Playlist->Internal.RepresentationUniqueID;
			Req.CDN 				   = Playlist->Internal.CDN;
			PlaylistReader->RequestPlaylistLoad(Req);
			Result.RetryAfterMilliseconds(50);
		}
		return Result;
	}

	return IManifest::FResult(IManifest::FResult::EType::PastEOS);
}

/**
 * Selects the next segment to download.
 * This might be a segment from a different variant stream after a quality switch.
 *
 * @param OutSegment
 * @param InCurrentSegment
 * @param Options
 *
 * @return
 */
IManifest::FResult FPlayPeriodHLS::GetNextSegment(TSharedPtrTS<IStreamSegment>& OutSegment, TSharedPtrTS<const IStreamSegment> InCurrentSegment, const FParamDict& Options)
{
	return GetNextOrRetrySegment(OutSegment, InCurrentSegment, false);
}


IManifest::FResult FPlayPeriodHLS::GetRetrySegment(TSharedPtrTS<IStreamSegment>& OutSegment, TSharedPtrTS<const IStreamSegment> InCurrentSegment, const FParamDict& Options)
{
	bool bInsertFiller = Options.GetValue("insertFiller").SafeGetBool(false);

	// To insert filler data we can use the current request over again.
	if (bInsertFiller)
	{
		const FStreamSegmentRequestHLSfmp4* CurrentRequest = static_cast<const FStreamSegmentRequestHLSfmp4*>(InCurrentSegment.Get());
		TSharedPtrTS<FStreamSegmentRequestHLSfmp4> NewRequest(new FStreamSegmentRequestHLSfmp4);
		NewRequest->CopyFrom(*CurrentRequest);
		NewRequest->bInsertFillerData = true;
		// We treat replacing the segment with filler data as a retry.
		++NewRequest->NumOverallRetries;
		OutSegment = NewRequest;
		return IManifest::FResult(IManifest::FResult::EType::Found);
	}
	return GetNextOrRetrySegment(OutSegment, InCurrentSegment, true);
}


/**
 * Checks if any potentially blacklisted stream can be used again.
 */
void FPlayPeriodHLS::RefreshBlacklistState()
{
	FTimeValue Now = SessionServices->GetSynchronizedUTCTime()->GetTime();

	// Note: the manifest must be locked already.
	for (TMap<uint32, TWeakPtrTS<FManifestHLSInternal::FPlaylistBase>>::TIterator It = InternalManifest->PlaylistIDMap.CreateIterator(); It; ++It)
	{
		TSharedPtrTS<FManifestHLSInternal::FRendition::FPlaylistBase> Stream = It.Value().Pin();
		if (Stream.IsValid())
		{
			if (Stream->Internal.Blacklisted.IsValid())
			{
				if (Now >= Stream->Internal.Blacklisted->BecomesAvailableAgainAtUTC)
				{
					Stream->Internal.LoadState  	  = FManifestHLSInternal::FPlaylistBase::FInternal::ELoadState::NotLoaded;
					Stream->Internal.bReloadTriggered = false;
					Stream->Internal.bNewlySelected   = false;
					Stream->Internal.ExpiresAtTime.SetToPositiveInfinity();

					// Tell the stream selector that this stream is available again.
					TSharedPtrTS<IAdaptiveStreamSelector> StreamSelector(SessionServices->GetStreamSelector());
					StreamSelector->MarkStreamAsAvailable(Stream->Internal.Blacklisted->AssetIDs);

					Stream->Internal.Blacklisted.Reset();
					LogMessage(IInfoLog::ELevel::Info, FString::Printf(TEXT("Lifting blacklist of playlist \"%s\""), *Stream->Internal.PlaylistLoadRequest.URL));
				}
			}
		}
	}
}



/**
 * Returns segment information (duration and estimated byte size) of the
 * next n segments for the indicated stream.
 *
 * @param OutSegmentInformation
 * @param OutAverageSegmentDuration
 * @param InCurrentSegment
 * @param LookAheadTime
 * @param AdaptationSet
 * @param Representation
 */
void FPlayPeriodHLS::GetSegmentInformation(TArray<FSegmentInformation>& OutSegmentInformation, FTimeValue& OutAverageSegmentDuration, TSharedPtrTS<const IStreamSegment> InCurrentSegment, const FTimeValue& LookAheadTime, const TSharedPtrTS<IPlaybackAssetAdaptationSet>& AdaptationSet, const TSharedPtrTS<IPlaybackAssetRepresentation>& Representation)
{
	OutSegmentInformation.Empty();
	OutAverageSegmentDuration.SetToInvalid();

	FTimeValue StartingTime(FTimeValue::GetZero());

	const FStreamSegmentRequestHLSfmp4* CurrentSegment = static_cast<const FStreamSegmentRequestHLSfmp4*>(InCurrentSegment.Get());

	if (CurrentSegment)
	{
		// The time of the next segment needs to be larger than that of the current. We add half the duration to the time to do that.
		// The reason being that adding the whole duration might get us slightly further than the next segment actually is, particularly if it is in
		// another variant playlist.
		StartingTime = CurrentSegment->AbsoluteDateTime + (CurrentSegment->SegmentDuration / 2);
	}

	FManifestHLSInternal::ScopedLockPlaylists lock(InternalManifest);
	const FManifestHLSInternal* pInt = InternalManifest.Get();
	check(pInt);
	if (!pInt)
	{
		return;
	}

	// The representation ID is the unique ID of the stream as a string. Convert it back
	uint32 UniqueID;
	LexFromString(UniqueID, *Representation->GetUniqueIdentifier());
	TSharedPtrTS<FManifestHLSInternal::FPlaylistBase> Playlist = pInt->GetPlaylistForUniqueID(UniqueID);
	if (!Playlist.IsValid() || !Playlist->IsVariantStream())
	{
		return;
	}

	// Get the bitrate of the intended variant stream
	const int32 Bitrate = Playlist->GetBitrate();
	// Is the playlist of this stream loaded?
	TSharedPtrTS<FManifestHLSInternal::FMediaStream>	MediaStream = Playlist->Internal.MediaStream;
	bool bIsIntendedStream = false;
	if (MediaStream.IsValid())
	{
		bIsIntendedStream = true;
	}
	else
	{
		// Not loaded. At this point we have to _assume_ that all video variant streams are segmented the same so we search for any
		// loaded variant and use its segmentation to return information for.
		for(int32 VideoStreamIndex=0; VideoStreamIndex<pInt->VariantStreams.Num(); ++VideoStreamIndex)
		{
			MediaStream = pInt->VariantStreams[VideoStreamIndex]->Internal.MediaStream;
			if (MediaStream.IsValid())
			{
				break;
			}
		}
	}
	// This should have yielded a playlist.
	check(MediaStream.IsValid());
	if (MediaStream.IsValid())
	{
		const TArray<FManifestHLSInternal::FMediaStream::FMediaSegment>& SegmentList = MediaStream->SegmentList;

		FTimeValue TimeToGo(LookAheadTime);
		FTimeValue AccumulatedDuration(FTimeValue::GetZero());

		// Find the segment we need to start with.
		int32 FirstIndex = 0;
		for(; FirstIndex < SegmentList.Num(); ++FirstIndex)
		{
			if (SegmentList[FirstIndex].AbsoluteDateTime >= StartingTime)
			{
				break;
			}
		}

		while(TimeToGo > FTimeValue::GetZero() && FirstIndex < SegmentList.Num())
		{
			FSegmentInformation& si = OutSegmentInformation.AddDefaulted_GetRef();
			si.Duration = SegmentList[FirstIndex].Duration;
			if (si.Duration <= FTimeValue::GetZero())
			{
				break;
			}
			// Set the actual byte size only if a byte range is defined and if we are operating on the intended stream. Otherwise use default size for duration and bitrate.
			si.ByteSize = bIsIntendedStream && SegmentList[FirstIndex].ByteRange.IsSet() ? SegmentList[FirstIndex].ByteRange.GetNumBytes() : static_cast<int64>(Bitrate * si.Duration.GetAsSeconds() / 8);
			AccumulatedDuration += si.Duration;
			TimeToGo -= si.Duration;
			++FirstIndex;
		}
		// Fill the remaining duration with the average segment duration or, if that is somehow not valid, the target duration.
		FTimeValue fillDuration = MediaStream->TotalAccumulatedSegmentDuration;
		if (!fillDuration.IsValid() || fillDuration <= FTimeValue::GetZero() || SegmentList.Num() == 0)
		{
			fillDuration = MediaStream->TargetDuration;
		}
		else
		{
			fillDuration /= SegmentList.Num();
		}
		if (fillDuration > FTimeValue::GetZero())
		{
			while(TimeToGo > FTimeValue::GetZero())
			{
				FSegmentInformation& si = OutSegmentInformation.AddDefaulted_GetRef();
				si.Duration = fillDuration;
				si.ByteSize = static_cast<int64>(Bitrate * si.Duration.GetAsSeconds() / 8);
				AccumulatedDuration += si.Duration;
				TimeToGo -= si.Duration;
			}
		}
		// Set up average duration.
		if (OutSegmentInformation.Num())
		{
			OutAverageSegmentDuration = AccumulatedDuration / OutSegmentInformation.Num();
		}
	}
	else
	{
		// Not a single playlist active?
		// Should we synthesize some information based on stream bitrate (which we have) and a fake fixed duration?
		check(!"Now what?");
	}
}




TSharedPtrTS<ITimelineMediaAsset> FPlayPeriodHLS::GetMediaAsset() const
{
	FManifestHLSInternal::ScopedLockPlaylists lock(InternalManifest);
	const FManifestHLSInternal* pInt = InternalManifest.Get();
	check(pInt && pInt->PlaybackTimeline.IsValid());
	if (pInt && pInt->PlaybackTimeline.IsValid())
	{
		// HLS only has a single playback "asset". (aka Period)
		return pInt->PlaybackTimeline->GetMediaAssetByIndex(0);
	}
	return TSharedPtrTS<ITimelineMediaAsset>();
}



void FPlayPeriodHLS::SelectStream(const TSharedPtrTS<IPlaybackAssetAdaptationSet>& AdaptationSet, const TSharedPtrTS<IPlaybackAssetRepresentation>& Representation, const FString& PreferredCDN)
{
	if (AdaptationSet.IsValid() && Representation.IsValid())
	{
		RefreshBlacklistState();

		// The representation ID is the unique ID of the stream as a string. Convert it back
		uint32 UniqueID;
		LexFromString(UniqueID, *Representation->GetUniqueIdentifier());

		// Which stream type is this?
		switch(Representation->GetCodecInformation().GetStreamType())
		{
			case EStreamType::Video:
			{
				// Different from what we have actively selected?
				if (UniqueID != ActiveVideoUniqueID)
				{
					// FIXME: We could emit a QoS event here. Although selecting the stream does not mean it will actually get used.
					//        There is still a chance that (if the playlist is not loaded yet) another stream will be selected right away.

					// Tell the manifest that we are now using a different stream.
					InternalManifest->SelectActiveStreamID(UniqueID, ActiveVideoUniqueID);

					ActiveVideoUniqueID = UniqueID;
				}
				break;
			}
			case EStreamType::Audio:
			{
				// Different from what we have actively selected?
				if (UniqueID != ActiveAudioUniqueID)
				{
					// FIXME: We could emit a QoS event here. Although selecting the stream does not mean it will actually get used.
					//        There is still a chance that (if the playlist is not loaded yet) another stream will be selected right away.

					// Tell the manifest that we are now using a different stream.
					InternalManifest->SelectActiveStreamID(UniqueID, ActiveAudioUniqueID);

					ActiveAudioUniqueID = UniqueID;
				}
				break;
			}
			default:
			{
				break;
			}
		}
	}
}


} // namespace Electra


