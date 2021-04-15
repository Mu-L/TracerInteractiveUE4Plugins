// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerCore.h"
#include "Player/Manifest.h"
#include "HTTP/HTTPManager.h"
#include "Demuxer/ParserISO14496-12.h"
#include "SynchronizedClock.h"
#include "PlaylistReaderMP4.h"
#include "Utilities/HashFunctions.h"
#include "Utilities/TimeUtilities.h"
#include "HAL/LowLevelMemTracker.h"
#include "ElectraPlayerPrivate.h"
#include "Player/AdaptiveStreamingPlayerResourceRequest.h"
#include "Player/mp4/StreamReaderMP4.h"
#include "Player/mp4/ManifestMP4.h"


#define ERRCODE_MANIFEST_MP4_STARTSEGMENT_NOT_FOUND						1


namespace Electra
{

//-----------------------------------------------------------------------------
/**
 * CTOR
 */
FManifestMP4Internal::FManifestMP4Internal(IPlayerSessionServices* InPlayerSessionServices)
	: PlayerSessionServices(InPlayerSessionServices)
{
}


//-----------------------------------------------------------------------------
/**
 * DTOR
 */
FManifestMP4Internal::~FManifestMP4Internal()
{
}


//-----------------------------------------------------------------------------
/**
 * Builds the internal manifest from the mp4's moov box.
 *
 * @param MP4Parser
 * @param URL
 * @param InConnectionInfo
 *
 * @return
 */
FErrorDetail FManifestMP4Internal::Build(TSharedPtrTS<IParserISO14496_12> MP4Parser, const FString& URL, const HTTP::FConnectionInfo& InConnectionInfo)
{
	ConnectionInfo = InConnectionInfo;
	MediaAsset = MakeSharedTS<FTimelineAssetMP4>();
	return MediaAsset->Build(PlayerSessionServices, MP4Parser, URL);
}


//-----------------------------------------------------------------------------
/**
 * Logs a message.
 *
 * @param Level
 * @param Message
 */
void FManifestMP4Internal::LogMessage(IInfoLog::ELevel Level, const FString& Message)
{
	if (PlayerSessionServices)
	{
		PlayerSessionServices->PostLog(Facility::EFacility::MP4Playlist, Level, Message);
	}
}


//-----------------------------------------------------------------------------
/**
 * Returns the type of presentation.
 * For a single mp4 file this is always VoD.
 *
 * @return
 */
IManifest::EType FManifestMP4Internal::GetPresentationType() const
{
	return IManifest::EType::OnDemand;
}


//-----------------------------------------------------------------------------
/**
 * Returns the media timeline object for this asset.
 *
 * @return
 */
TSharedPtrTS<IPlaybackAssetTimeline> FManifestMP4Internal::GetTimeline() const
{
	// Since an mp4 file is fixed and will not change the timeline is fixed.
	// That's why we are inheriting from IPlaybackAssetTimeline and return ourselves.
	TSharedPtrTS<const FManifestMP4Internal> This = SharedThis(this);
	return StaticCastSharedPtr<IPlaybackAssetTimeline>(ConstCastSharedPtr<FManifestMP4Internal>(This));
}


//-----------------------------------------------------------------------------
/**
 * Returns the starting bitrate.
 *
 * This is merely informational and not strictly required.
 * If fetching of the moov box provided us with the total size of the mp4 file
 * we will use that divided by the duration.
 *
 * @return
 */
int64 FManifestMP4Internal::GetDefaultStartingBitrate() const
{
	FTimeValue dur = GetDuration();
	if (ConnectionInfo.ContentLength > 0 && dur.IsValid() && dur > FTimeValue::GetZero())
	{
		return (int64)( ConnectionInfo.ContentLength * 8 / dur.GetAsSeconds() );
	}
	return 0;
}


//-----------------------------------------------------------------------------
/**
 * Returns stream metadata.
 *
 * @param OutMetadata
 * @param StreamType
 */
void FManifestMP4Internal::GetStreamMetadata(TArray<FStreamMetadata>& OutMetadata, EStreamType StreamType) const
{
	if (MediaAsset.IsValid())
	{
		for(int32 i=0, iMax = MediaAsset->GetNumberOfAdaptationSets(StreamType); i<iMax; ++i)
		{
			TSharedPtrTS<IPlaybackAssetAdaptationSet> AdaptSet = MediaAsset->GetAdaptationSetByTypeAndIndex(StreamType, i);
			if (AdaptSet.IsValid())
			{
				for(int32 j=0, jMax=AdaptSet->GetNumberOfRepresentations(); j<jMax; ++j)
				{
					TSharedPtrTS<FRepresentationMP4> Repr = StaticCastSharedPtr<FRepresentationMP4>(AdaptSet->GetRepresentationByIndex(j));
					if (Repr.IsValid())
					{
						FStreamMetadata& meta = OutMetadata.AddDefaulted_GetRef();
						meta.CodecInformation = Repr->GetCodecInformation();
						LexFromString(meta.StreamUniqueID, *Repr->GetUniqueIdentifier());
						meta.PlaylistID 	  = Repr->GetCDN();
						meta.Bandwidth  	  = Repr->GetBitrate();
						meta.LanguageCode     = AdaptSet->GetLanguage();
					}
				}
			}
		}
	}
}


//-----------------------------------------------------------------------------
/**
 * Returns the minimum duration of content that must be buffered up before playback
 * will begin. This is an arbitrary choice that could be controlled by a 'pdin' box.
 *
 * @return
 */
FTimeValue FManifestMP4Internal::GetMinBufferTime() const
{
	// NOTE: This could come from a 'pdin' (progressive download information) box, but those are rarely, if ever, set by any tool.
	return FTimeValue().SetFromSeconds(2.0);
}


//-----------------------------------------------------------------------------
/**
 * Creates an instance of a stream reader to stream from the mp4 file.
 *
 * @return
 */
IStreamReader *FManifestMP4Internal::CreateStreamReaderHandler()
{
	return new FStreamReaderMP4;
}


//-----------------------------------------------------------------------------
/**
 * Returns the playback period for the given time.
 *
 * @param OutPlayPeriod
 * @param StartPosition
 * @param SearchType
 *
 * @return
 */
IManifest::FResult FManifestMP4Internal::FindPlayPeriod(TSharedPtrTS<IPlayPeriod>& OutPlayPeriod, const FPlayStartPosition& StartPosition, ESearchType SearchType)
{
	// FIXME: We could however check if the start position falls into the duration of the asset. Not sure why it wouldn't or why we would want to do that.
	OutPlayPeriod = MakeSharedTS<FPlayPeriodMP4>(MediaAsset);
	return IManifest::FResult(IManifest::FResult::EType::Found);
}





//-----------------------------------------------------------------------------
/**
 * Constructs a playback period.
 *
 * @param InMediaAsset
 */
FManifestMP4Internal::FPlayPeriodMP4::FPlayPeriodMP4(TSharedPtrTS<FManifestMP4Internal::FTimelineAssetMP4> InMediaAsset)
	: MediaAsset(InMediaAsset)
	, bIsReady(false)
{
}


//-----------------------------------------------------------------------------
/**
 * Destroys a playback period.
 */
FManifestMP4Internal::FPlayPeriodMP4::~FPlayPeriodMP4()
{
}


//-----------------------------------------------------------------------------
/**
 * Sets stream playback preferences for this playback period.
 *
 * @param InPreferences
 */
void FManifestMP4Internal::FPlayPeriodMP4::SetStreamPreferences(const FStreamPreferences& InPreferences)
{
	Preferences = InPreferences;
}


//-----------------------------------------------------------------------------
/**
 * Returns the ready state of this playback period.
 *
 * @return
 */
IManifest::IPlayPeriod::EReadyState FManifestMP4Internal::FPlayPeriodMP4::GetReadyState()
{
	return bIsReady ? IManifest::IPlayPeriod::EReadyState::IsReady : IManifest::IPlayPeriod::EReadyState::NotReady;
}


//-----------------------------------------------------------------------------
/**
 * Prepares the playback period for playback.
 * With an mp4 file we are actually always ready for playback, but we say we're not
 * one time to get here with any possible options.
 *
 * @param InOptions
 */
void FManifestMP4Internal::FPlayPeriodMP4::PrepareForPlay(const FParamDict& InOptions)
{
	Options = InOptions;
	bIsReady = true;
}


//-----------------------------------------------------------------------------
/**
 * Returns the timeline media asset. We have a weak pointer to it only to
 * prevent any cyclic locks, so we need to lock it first.
 *
 * @return
 */
TSharedPtrTS<ITimelineMediaAsset> FManifestMP4Internal::FPlayPeriodMP4::GetMediaAsset() const
{
	TSharedPtrTS<ITimelineMediaAsset> ma = MediaAsset.Pin();
	return ma;
}


//-----------------------------------------------------------------------------
/**
 * Selects a particular stream (== internal track ID) for playback.
 *
 * @param AdaptationSet
 * @param Representation
 * @param PreferredCDN
 */
void FManifestMP4Internal::FPlayPeriodMP4::SelectStream(const TSharedPtrTS<IPlaybackAssetAdaptationSet>& AdaptationSet, const TSharedPtrTS<IPlaybackAssetRepresentation>& Representation, const FString& PreferredCDN)
{
	// Presently this method is only called by the ABR to switch between quality levels or CDNs.
	// Since a single mp4 doesn't have different quality levels (technically it could, but we are concerning ourselves only with different bitrates and that doesn't apply since we are streaming
	// the single file sequentially and selecting a different stream would not save any bandwidth so we don't bother) we ignore this for now.

	// This may need an implementation when switching between different languages though.
	// .....
}


//-----------------------------------------------------------------------------
/**
 * Creates the starting segment request to start playback with.
 *
 * @param OutSegment
 * @param StartPosition
 * @param SearchType
 *
 * @return
 */
IManifest::FResult FManifestMP4Internal::FPlayPeriodMP4::GetStartingSegment(TSharedPtrTS<IStreamSegment>& OutSegment, const FPlayStartPosition& StartPosition, ESearchType SearchType)
{
	TSharedPtrTS<FTimelineAssetMP4> ma = MediaAsset.Pin();
	return ma.IsValid() ? ma->GetStartingSegment(OutSegment, StartPosition, SearchType, -1) : IManifest::FResult(IManifest::FResult::EType::NotFound);
}


//-----------------------------------------------------------------------------
/**
 * Sets up a starting segment request to loop playback to.
 * The streams selected through SelectStream() will be used.
 *
 * @param OutSegment
 * @param InOutLoopState
 * @param InFinishedSegments
 * @param StartPosition
 * @param SearchType
 *
 * @return
 */
IManifest::FResult FManifestMP4Internal::FPlayPeriodMP4::GetLoopingSegment(TSharedPtrTS<IStreamSegment>& OutSegment, FPlayerLoopState& InOutLoopState, const TMultiMap<EStreamType, TSharedPtrTS<IStreamSegment>>& InFinishedSegments, const FPlayStartPosition& StartPosition, ESearchType SearchType)
{
	TSharedPtrTS<FTimelineAssetMP4> ma = MediaAsset.Pin();
	return ma.IsValid() ? ma->GetLoopingSegment(OutSegment, InOutLoopState, InFinishedSegments, StartPosition, SearchType) : IManifest::FResult(IManifest::FResult::EType::NotFound);
}


//-----------------------------------------------------------------------------
/**
 * Creates the next segment request.
 *
 * @param OutSegment
 * @param CurrentSegment
 * @param InOptions
 *
 * @return
 */
IManifest::FResult FManifestMP4Internal::FPlayPeriodMP4::GetNextSegment(TSharedPtrTS<IStreamSegment>& OutSegment, TSharedPtrTS<const IStreamSegment> CurrentSegment, const FParamDict& InOptions)
{
	TSharedPtrTS<FTimelineAssetMP4> ma = MediaAsset.Pin();
	return ma.IsValid() ? ma->GetNextSegment(OutSegment, CurrentSegment, InOptions) : IManifest::FResult(IManifest::FResult::EType::NotFound);
}


//-----------------------------------------------------------------------------
/**
 * Creates a segment retry request.
 *
 * @param OutSegment
 * @param CurrentSegment
 * @param InOptions
 *
 * @return
 */
IManifest::FResult FManifestMP4Internal::FPlayPeriodMP4::GetRetrySegment(TSharedPtrTS<IStreamSegment>& OutSegment, TSharedPtrTS<const IStreamSegment> CurrentSegment, const FParamDict& InOptions)
{
	TSharedPtrTS<FTimelineAssetMP4> ma = MediaAsset.Pin();
	return ma.IsValid() ? ma->GetRetrySegment(OutSegment, CurrentSegment, InOptions) : IManifest::FResult(IManifest::FResult::EType::NotFound);
}


//-----------------------------------------------------------------------------
/**
 * Returns segment information for the next n segments.
 *
 * @param OutSegmentInformation
 * @param OutAverageSegmentDuration
 * @param CurrentSegment
 * @param LookAheadTime
 * @param AdaptationSet
 * @param Representation
 */
void FManifestMP4Internal::FPlayPeriodMP4::GetSegmentInformation(TArray<FSegmentInformation>& OutSegmentInformation, FTimeValue& OutAverageSegmentDuration, TSharedPtrTS<const IStreamSegment> CurrentSegment, const FTimeValue& LookAheadTime, const TSharedPtrTS<IPlaybackAssetAdaptationSet>& AdaptationSet, const TSharedPtrTS<IPlaybackAssetRepresentation>& Representation)
{
	TSharedPtrTS<FTimelineAssetMP4> ma = MediaAsset.Pin();
	if (ma.IsValid())
	{
		ma->GetSegmentInformation(OutSegmentInformation, OutAverageSegmentDuration, CurrentSegment, LookAheadTime, AdaptationSet, Representation);
	}
}





//-----------------------------------------------------------------------------
/**
 * Builds the timeline asset.
 *
 * @param InPlayerSessionServices
 * @param MP4Parser
 * @param URL
 *
 * @return
 */
FErrorDetail FManifestMP4Internal::FTimelineAssetMP4::Build(IPlayerSessionServices* InPlayerSessionServices, TSharedPtrTS<IParserISO14496_12> MP4Parser, const FString& URL)
{
	PlayerSessionServices = InPlayerSessionServices;
	MediaURL = URL;
	// Go over the supported tracks and create an internal manifest-like structure for the player to work with.
	for(int32 nTrack=0,nMaxTrack=MP4Parser->GetNumberOfTracks(); nTrack < nMaxTrack; ++nTrack)
	{
		const IParserISO14496_12::ITrack* Track = MP4Parser->GetTrackByIndex(nTrack);
		if (Track)
		{
			// In an mp4 file we treat every track as a single adaptation set with one representation only.
			// That's because by definition an adaptation set contains the same content at different bitrates and resolutions, but
			// the type, language and codec has to be the same.
			FErrorDetail err;
			TSharedPtrTS<FAdaptationSetMP4> AdaptationSet = MakeSharedTS<FAdaptationSetMP4>();
			err = AdaptationSet->CreateFrom(Track, URL);
			if (err.IsOK())
			{
				// Add this track to the proper category.
				switch(Track->GetCodecInformation().GetStreamType())
				{
					case EStreamType::Video:
						VideoAdaptationSets.Add(AdaptationSet);
						break;
					case EStreamType::Audio:
						AudioAdaptationSets.Add(AdaptationSet);
						break;
					default:
						break;
				}
			}
			else
			{
				return err;
			}
		}
	}

// FIXME: fragmented mp4's with a sidx and moof boxes!

	// Hold on to the parsed MOOV box for future reference.
	MoovBoxParser = MP4Parser;

	return FErrorDetail();
}


void FManifestMP4Internal::FTimelineAssetMP4::LogMessage(IInfoLog::ELevel Level, const FString& Message)
{
	if (PlayerSessionServices)
	{
		PlayerSessionServices->PostLog(Facility::EFacility::MP4Playlist, Level, Message);
	}
}


void FManifestMP4Internal::FTimelineAssetMP4::LimitSegmentDownloadSize(TSharedPtrTS<IStreamSegment>& InOutSegment, TSharedPtr<IParserISO14496_12::IAllTrackIterator, ESPMode::ThreadSafe> InAllTrackIterator)
{
	// Limit the segment download size.
	// This helps with downloads that might otherwise take too long or keep the connection open for too long (when downloading a large mp4 from start to finish).
	const int64 MaxSegmentSize = 4 * 1024 * 1024;
	if (MaxSegmentSize > 0)
	{
		if (InOutSegment.IsValid())
		{
			FStreamSegmentRequestMP4* Request = static_cast<FStreamSegmentRequestMP4*>(InOutSegment.Get());
			const int64 StartOffset = Request->FileStartOffset;
			TSharedPtr<IParserISO14496_12::IAllTrackIterator, ESPMode::ThreadSafe> AllTrackIterator = InAllTrackIterator;
			if (!AllTrackIterator.IsValid())
			{
				AllTrackIterator = MoovBoxParser->CreateAllTrackIteratorByFilePos(StartOffset);
			}
			bool bFirst = true;
			uint32 TrackId = ~0U;
			uint32 TrackTimeScale = 0;
			int64 TrackDur = 0;
			int64 LastTrackOffset = -1;
			int64 LastSampleSize = 0;
			while(AllTrackIterator.IsValid())
			{
				const IParserISO14496_12::ITrackIterator* CurrentTrackIt = AllTrackIterator->Current();
				if (CurrentTrackIt)
				{
					LastTrackOffset = CurrentTrackIt->GetSampleFileOffset();
					LastSampleSize = CurrentTrackIt->GetSampleSize();
					if (bFirst)
					{
						bFirst = false;
						TrackId = CurrentTrackIt->GetTrack()->GetID();
						TrackTimeScale = CurrentTrackIt->GetTimescale();
					}
					if (TrackId == CurrentTrackIt->GetTrack()->GetID())
					{
						TrackDur += CurrentTrackIt->GetDuration();
					}
					int64 CurrentTrackOffset = LastTrackOffset;
					if (CurrentTrackOffset - StartOffset >= MaxSegmentSize)
					{
						// Limit reached.
						Request->FileEndOffset = CurrentTrackOffset - 1;
						Request->SegmentInternalSize = CurrentTrackOffset - StartOffset;
						Request->SegmentDuration.SetFromND(TrackDur, TrackTimeScale);
						Request->bIsLastSegment = false;
						break;
					}
					AllTrackIterator->Next();
				}
				else
				{
					// Done iterating
					Request->SegmentInternalSize = LastTrackOffset + LastSampleSize - StartOffset;
					break;
				}
			}
		}
	}
}


IManifest::FResult FManifestMP4Internal::FTimelineAssetMP4::GetStartingSegment(TSharedPtrTS<IStreamSegment>& OutSegment, const FPlayStartPosition& StartPosition, ESearchType SearchType, int64 AtAbsoluteFilePos)
{
// TODO: If there is a SIDX box we will look in there.

	// Look at the actual tracks. If there is video search there first for a keyframe/IDR frame.
	if (VideoAdaptationSets.Num())
	{
// TODO: use the selected track index if there are several tracks and we have a selected one.
		TSharedPtrTS<FRepresentationMP4> Repr = StaticCastSharedPtr<FRepresentationMP4>(VideoAdaptationSets[0]->GetRepresentationByIndex(0));
		if (Repr.IsValid())
		{
			int32 TrackID;
			LexFromString(TrackID, *Repr->GetUniqueIdentifier());
			const IParserISO14496_12::ITrack* Track = MoovBoxParser->GetTrackByTrackID(TrackID);
			if (Track)
			{
				//
				TSharedPtrTS<IParserISO14496_12::ITrackIterator> TrackIt(Track->CreateIterator());
				IParserISO14496_12::ITrackIterator::ESearchMode SearchMode =
					SearchType == ESearchType::After  || SearchType == ESearchType::StrictlyAfter  ? IParserISO14496_12::ITrackIterator::ESearchMode::After  :
					SearchType == ESearchType::Before || SearchType == ESearchType::StrictlyBefore ? IParserISO14496_12::ITrackIterator::ESearchMode::Before : IParserISO14496_12::ITrackIterator::ESearchMode::Closest;

				UEMediaError err;
				FTimeValue firstTimestamp;
				int64 firstByteOffset = 0;
				if (AtAbsoluteFilePos < 0)
				{
					// Get the track duration. If the start time is outside the track this could be a deliberate seek past the end of the video track
					// into a longer audio track. We do not want to snap to the last video IDR frame then.
					FTimeFraction TrackDuration = Track->GetDuration();
					if (TrackDuration.IsValid() && FTimeValue().SetFromTimeFraction(TrackDuration) <= StartPosition.Time && (SearchType == ESearchType::After || SearchType == ESearchType::StrictlyAfter || SearchType == ESearchType::Closest))
					{
						err = UEMEDIA_ERROR_END_OF_STREAM;
					}
					else
					{
						err = TrackIt->StartAtTime(StartPosition.Time, SearchMode, true);
						if (err == UEMEDIA_ERROR_OK)
						{
							firstTimestamp.SetFromND(TrackIt->GetDTS(), TrackIt->GetTimescale());
							firstByteOffset = TrackIt->GetSampleFileOffset();
						}
					}
				}
				else
				{
					TSharedPtr<IParserISO14496_12::IAllTrackIterator, ESPMode::ThreadSafe> AllTrackIterator = MoovBoxParser->CreateAllTrackIteratorByFilePos(AtAbsoluteFilePos);
					const IParserISO14496_12::ITrackIterator* CurrentTrackIt = AllTrackIterator->Current();
					if (CurrentTrackIt)
					{
						firstTimestamp.SetFromND(CurrentTrackIt->GetDTS(), CurrentTrackIt->GetTimescale());
						firstByteOffset = CurrentTrackIt->GetSampleFileOffset();
						err = UEMEDIA_ERROR_OK;
					}
					else
					{
						err = UEMEDIA_ERROR_END_OF_STREAM;
					}
				}
				if (err == UEMEDIA_ERROR_OK)
				{
					// Time found. Set up the fragment request.
					TSharedPtrTS<FStreamSegmentRequestMP4> req(new FStreamSegmentRequestMP4);
					OutSegment = req;

					req->MediaAsset 			= SharedThis(this);
					req->PrimaryTrackIterator   = TrackIt;
					req->FirstPTS   			= firstTimestamp;
					req->PrimaryStreamType  	= EStreamType::Video;
					req->FileStartOffset		= firstByteOffset;
					req->FileEndOffset  		= -1;
					req->Bitrate				= Repr->GetBitrate();
					req->bStartingOnMOOF		= false;
					req->bIsContinuationSegment = false;
					req->bIsFirstSegment		= true;
					req->bIsLastSegment			= true;
					req->SegmentDuration		= GetDuration() - req->FirstPTS;

					// FIXME: this may need to add all additional tracks at some point if their individual IDs matter
					if (AudioAdaptationSets.Num())
					{
						req->DependentStreams.AddDefaulted_GetRef().StreamType = EStreamType::Audio;
					}

					LimitSegmentDownloadSize(OutSegment, nullptr);
					return IManifest::FResult(IManifest::FResult::EType::Found);
				}
				else if (err == UEMEDIA_ERROR_END_OF_STREAM)
				{
					// If there are no audio tracks we return an EOS request.
					// Otherwise we search the audio tracks for a start position.
					if (AudioAdaptationSets.Num() == 0)
					{
						TSharedPtrTS<FStreamSegmentRequestMP4> req(new FStreamSegmentRequestMP4);
						OutSegment = req;
						req->MediaAsset 			= SharedThis(this);
						req->FirstPTS   			= FTimeValue::GetZero();
						req->PrimaryStreamType  	= EStreamType::Video;
						req->Bitrate				= Repr->GetBitrate();
						req->bStartingOnMOOF		= false;
						req->bIsContinuationSegment = false;
						req->bIsFirstSegment		= false;
						req->bIsLastSegment			= true;
						req->bAllTracksAtEOS		= true;
						//req->DependentStreams.PushBack().StreamType = EStreamType::Audio;
						return IManifest::FResult(IManifest::FResult::EType::Found);
					}
				}
				else if (err == UEMEDIA_ERROR_INSUFFICIENT_DATA)
				{
					return IManifest::FResult(IManifest::FResult::EType::BeforeStart);
				}
				else
				{
					IManifest::FResult res(IManifest::FResult::EType::NotFound);
					res.SetErrorDetail(FErrorDetail().SetError(err)
									   .SetFacility(Facility::EFacility::MP4Playlist)
									   .SetCode(ERRCODE_MANIFEST_MP4_STARTSEGMENT_NOT_FOUND)
									   .SetMessage(FString::Printf(TEXT("Could not find start segment for time %lld"), (long long int)StartPosition.Time.GetAsHNS())));
					return res;
				}
			}
		}
	}
	// No video track(s). Are there audio tracks?
	if (AudioAdaptationSets.Num())
	{
// TODO: use the selected track index if there are several tracks and we have a selected one.
		TSharedPtrTS<FRepresentationMP4> Repr = StaticCastSharedPtr<FRepresentationMP4>(AudioAdaptationSets[0]->GetRepresentationByIndex(0));
		if (Repr.IsValid())
		{
			int32 TrackID;
			LexFromString(TrackID, *Repr->GetUniqueIdentifier());
			const IParserISO14496_12::ITrack* Track = MoovBoxParser->GetTrackByTrackID(TrackID);
			if (Track)
			{
				//
				TSharedPtrTS<IParserISO14496_12::ITrackIterator> TrackIt(Track->CreateIterator());
				IParserISO14496_12::ITrackIterator::ESearchMode SearchMode =
					SearchType == ESearchType::After  || SearchType == ESearchType::StrictlyAfter  ? IParserISO14496_12::ITrackIterator::ESearchMode::After  :
					SearchType == ESearchType::Before || SearchType == ESearchType::StrictlyBefore ? IParserISO14496_12::ITrackIterator::ESearchMode::Before : IParserISO14496_12::ITrackIterator::ESearchMode::Closest;

				UEMediaError err;
				FTimeValue firstTimestamp;
				int64 firstByteOffset = 0;
				if (AtAbsoluteFilePos < 0)
				{
					// Get the track duration. If the start time is outside the track this could be a deliberate seek past the end of the audio track
					// for which we return EOS.
					FTimeFraction TrackDuration = Track->GetDuration();
					if (TrackDuration.IsValid() && FTimeValue().SetFromTimeFraction(TrackDuration) <= StartPosition.Time && (SearchType == ESearchType::After || SearchType == ESearchType::StrictlyAfter || SearchType == ESearchType::Closest))
					{
						err = UEMEDIA_ERROR_END_OF_STREAM;
					}
					else
					{
						err = TrackIt->StartAtTime(StartPosition.Time, SearchMode, true);
						if (err == UEMEDIA_ERROR_OK)
						{
							firstTimestamp.SetFromND(TrackIt->GetDTS(), TrackIt->GetTimescale());
							firstByteOffset = TrackIt->GetSampleFileOffset();
						}
					}
				}
				else
				{
					TSharedPtr<IParserISO14496_12::IAllTrackIterator, ESPMode::ThreadSafe> AllTrackIterator = MoovBoxParser->CreateAllTrackIteratorByFilePos(AtAbsoluteFilePos);
					const IParserISO14496_12::ITrackIterator* CurrentTrackIt = AllTrackIterator->Current();
					if (CurrentTrackIt)
					{
						firstTimestamp.SetFromND(CurrentTrackIt->GetDTS(), CurrentTrackIt->GetTimescale());
						firstByteOffset = CurrentTrackIt->GetSampleFileOffset();
						err = UEMEDIA_ERROR_OK;
					}
					else
					{
						err = UEMEDIA_ERROR_END_OF_STREAM;
					}
				}
				if (err == UEMEDIA_ERROR_OK)
				{
					// Time found. Set up the fragment request.
					TSharedPtrTS<FStreamSegmentRequestMP4> req(new FStreamSegmentRequestMP4);
					OutSegment = req;

					req->MediaAsset 			= SharedThis(this);
					req->PrimaryTrackIterator   = TrackIt;
					req->FirstPTS   			= firstTimestamp;
					req->PrimaryStreamType  	= EStreamType::Audio;
					req->FileStartOffset		= firstByteOffset;
					req->FileEndOffset  		= -1;
					req->Bitrate				= Repr->GetBitrate();
					req->bStartingOnMOOF		= false;
					req->bIsContinuationSegment = false;
					req->bIsFirstSegment		= true;
					req->bIsLastSegment			= true;
					req->SegmentDuration		= GetDuration() - req->FirstPTS;

					// In case the video stream is shorter than audio we still need to add it as a dependent stream
					// (if it exists) in case the video will loop back to a point where there is video.
					if (VideoAdaptationSets.Num())
					{
						req->DependentStreams.AddDefaulted_GetRef().StreamType = EStreamType::Video;
					}

					// FIXME: there may be subtitle tracks here we need to add as dependent streams.

					LimitSegmentDownloadSize(OutSegment, nullptr);
					return IManifest::FResult(IManifest::FResult::EType::Found);
				}
				else if (err == UEMEDIA_ERROR_END_OF_STREAM)
				{
					// Regardless of there being a video stream or not we return an EOS request.
					TSharedPtrTS<FStreamSegmentRequestMP4> req(new FStreamSegmentRequestMP4);
					OutSegment = req;
					req->MediaAsset 			= SharedThis(this);
					req->FirstPTS   			= FTimeValue::GetZero();
					req->PrimaryStreamType  	= EStreamType::Audio;
					req->Bitrate				= Repr->GetBitrate();
					req->bStartingOnMOOF		= false;
					req->bIsContinuationSegment = false;
					req->bIsFirstSegment		= false;
					req->bIsLastSegment			= true;
					req->bAllTracksAtEOS		= true;
					// But if there is a video track we add it as a dependent stream that is also at EOS.
					if (VideoAdaptationSets.Num())
					{
						req->DependentStreams.AddDefaulted_GetRef().StreamType = EStreamType::Video;
					}
					return IManifest::FResult(IManifest::FResult::EType::Found);
				}
				else if (err == UEMEDIA_ERROR_INSUFFICIENT_DATA)
				{
					return IManifest::FResult(IManifest::FResult::EType::BeforeStart);
				}
				else
				{
					IManifest::FResult res(IManifest::FResult::EType::NotFound);
					res.SetErrorDetail(FErrorDetail().SetError(err)
									   .SetFacility(Facility::EFacility::MP4Playlist)
									   .SetCode(ERRCODE_MANIFEST_MP4_STARTSEGMENT_NOT_FOUND)
									   .SetMessage(FString::Printf(TEXT("Could not find start segment for time %lld"), (long long int)StartPosition.Time.GetAsHNS())));
					return res;
				}
			}
		}
	}

	return IManifest::FResult(IManifest::FResult::EType::NotFound).SetErrorDetail(FErrorDetail().SetError(UEMEDIA_ERROR_INSUFFICIENT_DATA)
					   .SetFacility(Facility::EFacility::MP4Playlist)
					   .SetCode(ERRCODE_MANIFEST_MP4_STARTSEGMENT_NOT_FOUND)
					   .SetMessage(FString::Printf(TEXT("Could not find start segment for time %lld, no valid tracks"), (long long int)StartPosition.Time.GetAsHNS())));
}

IManifest::FResult FManifestMP4Internal::FTimelineAssetMP4::GetNextSegment(TSharedPtrTS<IStreamSegment>& OutSegment, TSharedPtrTS<const IStreamSegment> CurrentSegment, const FParamDict& Options)
{
	const FStreamSegmentRequestMP4* Request = static_cast<const FStreamSegmentRequestMP4*>(CurrentSegment.Get());
	if (Request)
	{
		// Check if the current request did not already go up to the end of the stream. If so there is no next segment.
		if (Request->FileEndOffset >= 0)
		{
			FPlayStartPosition dummyPos;
			IManifest::FResult res = GetStartingSegment(OutSegment, dummyPos, ESearchType::Same, Request->FileEndOffset + 1);
			if (res.GetType() == IManifest::FResult::EType::Found)
			{
				FStreamSegmentRequestMP4* NextRequest = static_cast<FStreamSegmentRequestMP4*>(OutSegment.Get());
				NextRequest->PlayerLoopState   	    = Request->PlayerLoopState;
				NextRequest->bIsContinuationSegment = true;
				NextRequest->bIsFirstSegment		= false;
				return res;
			}
		}
	}
	return IManifest::FResult(IManifest::FResult::EType::PastEOS);
}

IManifest::FResult FManifestMP4Internal::FTimelineAssetMP4::GetRetrySegment(TSharedPtrTS<IStreamSegment>& OutSegment, TSharedPtrTS<const IStreamSegment> CurrentSegment, const FParamDict& Options)
{
	const FStreamSegmentRequestMP4* Request = static_cast<const FStreamSegmentRequestMP4*>(CurrentSegment.Get());
	if (Request)
	{
		FPlayStartPosition dummyPos;
		IManifest::FResult res = GetStartingSegment(OutSegment, dummyPos, ESearchType::Same, Request->CurrentIteratorBytePos);
		if (res.GetType() == IManifest::FResult::EType::Found)
		{
			FStreamSegmentRequestMP4* RetryRequest = static_cast<FStreamSegmentRequestMP4*>(OutSegment.Get());
			RetryRequest->PlayerLoopState   	 = Request->PlayerLoopState;
			RetryRequest->bIsContinuationSegment = true;
			RetryRequest->NumOverallRetries 	 = Request->NumOverallRetries + 1;
			return res;
		}
	}
	return IManifest::FResult(IManifest::FResult::EType::NotFound);
}


IManifest::FResult FManifestMP4Internal::FTimelineAssetMP4::GetLoopingSegment(TSharedPtrTS<IStreamSegment>& OutSegment, FPlayerLoopState& InOutLoopState, const TMultiMap<EStreamType, TSharedPtrTS<IStreamSegment>>& InFinishedSegments, const FPlayStartPosition& StartPosition, ESearchType SearchType)
{
	if (InFinishedSegments.Num())
	{
		auto It = InFinishedSegments.CreateConstIterator();
		const FStreamSegmentRequestMP4* FinishedRequest = static_cast<const FStreamSegmentRequestMP4*>(It->Value.Get());
		if (FinishedRequest)
		{
			IManifest::FResult res = GetStartingSegment(OutSegment, StartPosition, SearchType, -1);
			if (res.GetType() == IManifest::FResult::EType::Found)
			{
				InOutLoopState.bLoopEnabled = true;
				InOutLoopState.LoopBasetime = FinishedRequest->NextLargestExpectedTimestamp.IsValid() ? FinishedRequest->NextLargestExpectedTimestamp : FTimeValue::GetZero();
				++InOutLoopState.LoopCount;
				FStreamSegmentRequestMP4* LoopRequest = static_cast<FStreamSegmentRequestMP4*>(OutSegment.Get());
				LoopRequest->PlayerLoopState = InOutLoopState;
				return res;
			}
		}
	}
	// Return past EOS when we can't loop to indicate we're really done now.
	return IManifest::FResult(IManifest::FResult::EType::PastEOS);
}


void FManifestMP4Internal::FTimelineAssetMP4::GetSegmentInformation(TArray<IManifest::IPlayPeriod::FSegmentInformation>& OutSegmentInformation, FTimeValue& OutAverageSegmentDuration, TSharedPtrTS<const IStreamSegment> CurrentSegment, const FTimeValue& LookAheadTime, const TSharedPtrTS<IPlaybackAssetAdaptationSet>& AdaptationSet, const TSharedPtrTS<IPlaybackAssetRepresentation>& Representation)
{
	// This is not expected to be called. And if it does we return a dummy entry.
	OutAverageSegmentDuration.SetFromSeconds(60.0);
	OutSegmentInformation.Empty();
	IManifest::IPlayPeriod::FSegmentInformation& si = OutSegmentInformation.AddDefaulted_GetRef();
	si.ByteSize = 1024 * 1024 * 1024;
	si.Duration.SetFromSeconds(60.0);
}

TSharedPtrTS<IParserISO14496_12>	FManifestMP4Internal::FTimelineAssetMP4::GetMoovBoxParser()
{
	return MoovBoxParser;
}



FErrorDetail FManifestMP4Internal::FAdaptationSetMP4::CreateFrom(const IParserISO14496_12::ITrack* InTrack, const FString& URL)
{
	Representation = MakeSharedTS<FRepresentationMP4>();
	FErrorDetail err;
	err = Representation->CreateFrom(InTrack, URL);
	if (err.IsOK())
	{
		CodecRFC6381	 = Representation->GetCodecInformation().GetCodecSpecifierRFC6381();
		UniqueIdentifier = FString("adaptation.") + Representation->GetUniqueIdentifier();
		Language		 = InTrack->GetLanguage();
	}
	return err;
}


FErrorDetail FManifestMP4Internal::FRepresentationMP4::CreateFrom(const IParserISO14496_12::ITrack* InTrack, const FString& URL)
{
	CodecInformation	 = InTrack->GetCodecInformation();
	// Get the CSD
	CodecSpecificData    = InTrack->GetCodecSpecificData();
	CodecSpecificDataRAW = InTrack->GetCodecSpecificDataRAW();

	// Since we are dealing with a track inside a multiplexed file there is no choice for CDNs.
	// We set the URL as the CDN.
	CDN = URL;

	// The unique identifier will be the track ID inside the mp4.
	// NOTE: This *MUST* be just a number since it gets parsed back out from a string into a number later! Do *NOT* prepend/append any string literals!!
	UniqueIdentifier = LexToString(InTrack->GetID());

	// Get bitrate from the average or max bitrate as stored in the track. If not stored it will be 0.
	Bitrate = InTrack->GetBitrateInfo().AvgBitrate ? InTrack->GetBitrateInfo().AvgBitrate : InTrack->GetBitrateInfo().MaxBitrate;

	// With no bitrate available we set some defaults. This is mainly to avoid a bitrate of 0 from being surfaced that would prevent
	// events like the initial bitrate change that needs to transition away from 0 to something real.
	if (Bitrate == 0)
	{
		switch(CodecInformation.GetStreamType())
		{
			case EStreamType::Video:
				Bitrate = 1 * 1024 * 1024;
				break;
			case EStreamType::Audio:
				Bitrate = 64 * 1024;
				break;
			case EStreamType::Subtitle:
				Bitrate = 8 * 1024;
				break;
			default:
				// Whatever it is, assume it's a low bitrate.
				Bitrate = 32 * 1024;
				break;
		}
	}

	// Not a whole lot that could have gone wrong here.
	return FErrorDetail();
}


} // namespace Electra


