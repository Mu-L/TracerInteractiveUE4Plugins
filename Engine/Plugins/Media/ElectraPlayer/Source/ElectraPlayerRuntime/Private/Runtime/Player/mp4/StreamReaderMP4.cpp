// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerCore.h"
#include "Player/Manifest.h"
#include "HTTP/HTTPManager.h"
#include "Demuxer/ParserISO14496-12.h"
#include "SynchronizedClock.h"
#include "PlaylistReaderMP4.h"
#include "Utilities/TimeUtilities.h"
#include "HAL/LowLevelMemTracker.h"
#include "ElectraPlayerPrivate.h"
#include "Player/PlayerStreamReader.h"
#include "Player/AdaptiveStreamingPlayerABR.h"
#include "Player/mp4/ManifestMP4.h"
#include "Player/mp4/StreamReaderMP4.h"





namespace Electra
{

FStreamSegmentRequestMP4::FStreamSegmentRequestMP4()
{
	PrimaryStreamType      = EStreamType::Video;
	FileStartOffset 	   = -1;
	FileEndOffset   	   = -1;
	SegmentInternalSize	   = -1;
	Bitrate 			   = 0;
	PlaybackSequenceID     = ~0U;
	bStartingOnMOOF 	   = false;
	bIsContinuationSegment = false;
	bIsFirstSegment		   = false;
	bIsLastSegment		   = false;
	bAllTracksAtEOS 	   = false;
	CurrentIteratorBytePos = 0;
	NumOverallRetries      = 0;
}

FStreamSegmentRequestMP4::~FStreamSegmentRequestMP4()
{
}

void FStreamSegmentRequestMP4::SetPlaybackSequenceID(uint32 InPlaybackSequenceID)
{
	PlaybackSequenceID = InPlaybackSequenceID;
}

uint32 FStreamSegmentRequestMP4::GetPlaybackSequenceID() const
{
	return PlaybackSequenceID;
}

EStreamType FStreamSegmentRequestMP4::GetType() const
{
	return  PrimaryStreamType;
}

void FStreamSegmentRequestMP4::GetDependentStreams(TArray<FDependentStreams>& OutDependentStreams) const
{
	// Those are not "real" dependent streams in that they are multiplexed and do not need to be fetched from
	// a different source. This merely indicates the types of non-primary streams we will be demuxing.
	OutDependentStreams = DependentStreams;
}

void FStreamSegmentRequestMP4::GetEndedStreams(TArray<TSharedPtrTS<IStreamSegment>>& OutAlreadyEndedStreams)
{
	OutAlreadyEndedStreams.Empty();
	if (bAllTracksAtEOS)
	{
		OutAlreadyEndedStreams.Push(SharedThis(this));
		for(int32 i=0; i<DependentStreams.Num(); ++i)
		{
			FStreamSegmentRequestMP4* DepReq = new FStreamSegmentRequestMP4;
			// Only need to set the stream type here.
			DepReq->PrimaryStreamType = DependentStreams[i].StreamType;
			TSharedPtrTS<IStreamSegment> p(DepReq);
			OutAlreadyEndedStreams.Push(p);
		}
	}
}

FTimeValue FStreamSegmentRequestMP4::GetFirstPTS() const
{
	return FirstPTS;
}

int32 FStreamSegmentRequestMP4::GetQualityIndex() const
{
	// No quality choice here.
	return 0;
}

int32 FStreamSegmentRequestMP4::GetBitrate() const
{
	return Bitrate;
}

void FStreamSegmentRequestMP4::GetDownloadStats(Metrics::FSegmentDownloadStats& OutStats) const
{
	OutStats = DownloadStats;
}






uint32	FStreamReaderMP4::UniqueDownloadID = 1;

FStreamReaderMP4::FStreamReaderMP4()
{
	bIsStarted  		  = false;
	bTerminate  		  = false;
	bRequestCanceled	  = false;
	bHasErrored 		  = false;
}

FStreamReaderMP4::~FStreamReaderMP4()
{
	Close();
}

UEMediaError FStreamReaderMP4::Create(IPlayerSessionServices* InPlayerSessionService, const CreateParam &InCreateParam)
{
	if (!InCreateParam.MemoryProvider ||
		!InCreateParam.EventListener ||
		!InCreateParam.PlayerSessionService)
	{
		return UEMEDIA_ERROR_BAD_ARGUMENTS;
	}

	Parameters = InCreateParam;
	bTerminate = false;
	bIsStarted = true;

	ThreadSetPriority(InCreateParam.ReaderConfig.ThreadParam.Priority);
	ThreadSetCoreAffinity(InCreateParam.ReaderConfig.ThreadParam.CoreAffinity);
	ThreadSetStackSize(InCreateParam.ReaderConfig.ThreadParam.StackSize);
	ThreadSetName("ElectraPlayer::MP4 streamer");
	ThreadStart(Electra::MakeDelegate(this, &FStreamReaderMP4::WorkerThread));

	return UEMEDIA_ERROR_OK;
}

void FStreamReaderMP4::Close()
{
	if (bIsStarted)
	{
		bIsStarted = false;

		TSharedPtrTS<FStreamSegmentRequestMP4>	Request = CurrentRequest;
		if (Request.IsValid())
		{
			// ...
		}
		CancelRequests();
		bTerminate = true;
		WorkSignal.Signal();
		ThreadWaitDone();
		ThreadReset();
		CurrentRequest.Reset();
	}
}


void FStreamReaderMP4::LogMessage(IInfoLog::ELevel Level, const FString& Message)
{
	if (Parameters.PlayerSessionService)
	{
		Parameters.PlayerSessionService->PostLog(Facility::EFacility::MP4StreamReader, Level, Message);
	}
}

IStreamReader::EAddResult FStreamReaderMP4::AddRequest(uint32 CurrentPlaybackSequenceID, TSharedPtrTS<IStreamSegment> InRequest)
{
	TSharedPtrTS<FStreamSegmentRequestMP4>	Request = CurrentRequest;
	if (Request.IsValid())
	{
		check(!"why is the handler busy??");
		return IStreamReader::EAddResult::TryAgainLater;
	}
	Request = StaticCastSharedPtr<FStreamSegmentRequestMP4>(InRequest);
	Request->SetPlaybackSequenceID(CurrentPlaybackSequenceID);
	bRequestCanceled = false;
	bHasErrored = false;
	// Only add the request if it is not an all-EOS one!
	if (!Request->bAllTracksAtEOS)
	{
		CurrentRequest = Request;
		WorkSignal.Signal();
	}
	return EAddResult::Added;
}

void FStreamReaderMP4::CancelRequests()
{
	bRequestCanceled = true;
	ReadBuffer.Abort();
}

void FStreamReaderMP4::PauseDownload()
{
	// Not implemented.
}

void FStreamReaderMP4::ResumeDownload()
{
	// Not implemented.
}

bool FStreamReaderMP4::HasBeenAborted() const
{
	return bRequestCanceled || ReadBuffer.bAbort;
}

bool FStreamReaderMP4::HasErrored() const
{
	return bHasErrored;
}

int32 FStreamReaderMP4::HTTPProgressCallback(const IElectraHttpManager::FRequest* InRequest)
{
	HTTPUpdateStats(MEDIAutcTime::Current(), InRequest);
	// Aborted?
	return HasBeenAborted() ? 1 : 0;
}

void FStreamReaderMP4::HTTPCompletionCallback(const IElectraHttpManager::FRequest* InRequest)
{
	HTTPUpdateStats(FTimeValue::GetInvalid(), InRequest);
	bHasErrored = InRequest->ConnectionInfo.StatusInfo.ErrorDetail.IsError();
	if (bHasErrored)
	{
		ReadBuffer.SetHasErrored();
	}
}

void FStreamReaderMP4::HTTPUpdateStats(const FTimeValue& CurrentTime, const IElectraHttpManager::FRequest* Request)
{
	TSharedPtrTS<FStreamSegmentRequestMP4> SegmentRequest = CurrentRequest;
	if (SegmentRequest.IsValid())
	{
		FMediaCriticalSection::ScopedLock lock(MetricUpdateLock);
		SegmentRequest->ConnectionInfo = Request->ConnectionInfo;
		// Update the current download stats which we report periodically to the ABR.
		Metrics::FSegmentDownloadStats& ds = SegmentRequest->DownloadStats;
		if (Request->ConnectionInfo.EffectiveURL.Len())
		{
			ds.URL 			  = Request->ConnectionInfo.EffectiveURL;
		}
		ds.HTTPStatusCode     = Request->ConnectionInfo.StatusInfo.HTTPStatus;
		ds.TimeToFirstByte    = Request->ConnectionInfo.TimeUntilFirstByte;
		ds.TimeToDownload     = ((CurrentTime.IsValid() ? CurrentTime : Request->ConnectionInfo.RequestEndTime) - Request->ConnectionInfo.RequestStartTime).GetAsSeconds();
		ds.ByteSize 		  = Request->ConnectionInfo.ContentLength;
		ds.NumBytesDownloaded = Request->ConnectionInfo.BytesReadSoFar;
	}
}

void FStreamReaderMP4::WorkerThread()
{
	LLM_SCOPE(ELLMTag::ElectraPlayer);
	while(!bTerminate)
	{
		WorkSignal.WaitAndReset();
		if (bTerminate)
		{
			break;
		}

		TSharedPtrTS<FStreamSegmentRequestMP4>	Request = CurrentRequest;
		if (!Request.IsValid())
		{
			continue;
		}

		FManifestMP4Internal::FTimelineAssetMP4* TimelineAsset = static_cast<FManifestMP4Internal::FTimelineAssetMP4*>(Request->MediaAsset.Get());

		// Clear the active track map.
		ActiveTrackMap.Reset();

// FIXME: If looping to somewhere within the stream instead of the beginning at zero the offset here must be made relative to the DTS of the first sample we demux.
//        Otherwise there would be a large jump ahead in time!
		FTimeValue LoopTimestampOffset = Request->PlayerLoopState.LoopBasetime;
		TSharedPtr<const FPlayerLoopState, ESPMode::ThreadSafe>		PlayerLoopState = MakeShared<const FPlayerLoopState, ESPMode::ThreadSafe>(Request->PlayerLoopState);


		// Get the list of all the tracks that have been selected in the asset.
		// This does not mean their data will be _used_ for playback, only that the track is usable by the player
		// with regards to type and codec.
		struct FPlaylistTrackMetadata
		{
			EStreamType		Type;
			FString			Language;
			FString			PeriodID;
			FString			AdaptationSetID;
			FString			RepresentationID;
			FString			CDN;
			int32			Bitrate;
		};
		TMap<uint32, FPlaylistTrackMetadata>	SelectedTrackMap;
		const EStreamType TypesOfSupportedTracks[] = { EStreamType::Video, EStreamType::Audio, EStreamType::Subtitle };
		for(int32 nStrType=0; nStrType<UE_ARRAY_COUNT(TypesOfSupportedTracks); ++nStrType)
		{
			int32 NumAdapt = Request->MediaAsset->GetNumberOfAdaptationSets(TypesOfSupportedTracks[nStrType]);
			for(int32 nAdapt=0; nAdapt<NumAdapt; ++nAdapt)
			{
				TSharedPtrTS<IPlaybackAssetAdaptationSet> AdaptationSet = Request->MediaAsset->GetAdaptationSetByTypeAndIndex(TypesOfSupportedTracks[nStrType], nAdapt);
				FString Language = AdaptationSet->GetLanguage();
				FString AdaptID = AdaptationSet->GetUniqueIdentifier();
				int32 NumRepr = AdaptationSet->GetNumberOfRepresentations();
				for(int32 nRepr=0; nRepr<NumRepr; ++nRepr)
				{
					TSharedPtrTS<IPlaybackAssetRepresentation> Representation = AdaptationSet->GetRepresentationByIndex(nRepr);
					// Note: By definition the representations unique identifier is a string of the numeric track ID and can thus be parsed back into a number.
					FString ReprID = Representation->GetUniqueIdentifier();
					uint32 TrackId;
					LexFromString(TrackId, *ReprID); 
					FPlaylistTrackMetadata tmd;
					tmd.Type			 = TypesOfSupportedTracks[nStrType];
					tmd.Language		 = Language;
					tmd.PeriodID		 = Request->MediaAsset->GetUniqueIdentifier();
					tmd.AdaptationSetID  = AdaptID;
					tmd.RepresentationID = ReprID;
					tmd.Bitrate 		 = Representation->GetBitrate();
					tmd.CDN 			 = Representation->GetCDN();
					SelectedTrackMap.Emplace(TrackId, tmd);
				}
			}
		}


		Metrics::FSegmentDownloadStats& ds = Request->DownloadStats;
		ds.StatsID = FMediaInterlockedIncrement(UniqueDownloadID);

		check(Request->PrimaryTrackIterator.IsValid() && Request->PrimaryTrackIterator->GetTrack());
		uint32 PrimaryTrackID = Request->PrimaryTrackIterator->GetTrack()->GetID();
		const FPlaylistTrackMetadata* PrimaryTrackMetadata = SelectedTrackMap.Find(PrimaryTrackID);
		check(PrimaryTrackMetadata);
		if (PrimaryTrackMetadata)
		{
			ds.MediaAssetID 	= PrimaryTrackMetadata->PeriodID;
			ds.AdaptationSetID  = PrimaryTrackMetadata->AdaptationSetID;
			ds.RepresentationID = PrimaryTrackMetadata->RepresentationID;
			ds.Bitrate  		= PrimaryTrackMetadata->Bitrate;
			ds.CDN  			= PrimaryTrackMetadata->CDN;
		}

		ds.FailureReason.Empty();
		ds.bWasSuccessful      = true;
		ds.bWasAborted  	   = false;
		ds.bDidTimeout  	   = false;
		ds.HTTPStatusCode      = 0;
		ds.StreamType   	   = Request->GetType();
		ds.SegmentType  	   = Metrics::ESegmentType::Media;
		ds.PresentationTime    = (Request->FirstPTS + LoopTimestampOffset).GetAsSeconds();
		ds.Duration 		   = Request->SegmentDuration.GetAsSeconds();
		ds.DurationDownloaded  = 0.0;
		ds.DurationDelivered   = 0.0;
		ds.TimeToFirstByte     = 0.0;
		ds.TimeToDownload      = 0.0;
		ds.ByteSize 		   = -1;
		ds.NumBytesDownloaded  = 0;
		ds.ThroughputBps	   = 0;
		ds.bInsertedFillerData = false;
		ds.URL  			   = TimelineAsset->GetMediaURL();
		ds.bIsMissingSegment   = false;
		ds.bParseFailure	   = false;
		ds.RetryNumber  	   = Request->NumOverallRetries;

		Parameters.EventListener->OnFragmentOpen(Request);

		TSharedPtrTS<IElectraHttpManager::FProgressListener>	ProgressListener;
		ProgressListener = MakeSharedTS<IElectraHttpManager::FProgressListener>();
		ProgressListener->CompletionDelegate = Electra::MakeDelegate(this, &FStreamReaderMP4::HTTPCompletionCallback);
		ProgressListener->ProgressDelegate   = Electra::MakeDelegate(this, &FStreamReaderMP4::HTTPProgressCallback);

		ReadBuffer.Reset();
		ReadBuffer.ReceiveBuffer = MakeSharedTS<IElectraHttpManager::FReceiveBuffer>();
		// Set the receive buffer to an okay-ish size. Too small and the file I/O may block too often and get too slow.
		ReadBuffer.ReceiveBuffer->Buffer.Reserve(4 << 20);
		ReadBuffer.ReceiveBuffer->bEnableRingbuffer = true;
		ReadBuffer.SetCurrentPos(Request->FileStartOffset);
		TSharedPtrTS<IElectraHttpManager::FRequest> HTTP(new IElectraHttpManager::FRequest);
		HTTP->Parameters.URL				= TimelineAsset->GetMediaURL();
		HTTP->Parameters.Range.Start		= Request->FileStartOffset;
		HTTP->Parameters.Range.EndIncluding = Request->FileEndOffset;
		// Explicit range?
		int64 NumRequestedBytes = HTTP->Parameters.Range.GetNumberOfBytes();
		int32 SubRequestSize = 0;
		if (NumRequestedBytes > 0)
		{
			if (Request->bIsFirstSegment && !Request->bIsLastSegment)
			{
				SubRequestSize = 512 << 10;
			}
			else if (Request->bIsFirstSegment && Request->bIsLastSegment)
			{
				SubRequestSize = 2 << 20;
			}
		}
		else
		{
			if (Request->SegmentInternalSize < 0)
			{
				SubRequestSize = 2 << 20;
			}
		}
		if (SubRequestSize)
		{
			HTTP->Parameters.SubRangeRequestSize = SubRequestSize;
		}

		HTTP->ReceiveBuffer 				= ReadBuffer.ReceiveBuffer;
		HTTP->ProgressListener  			= ProgressListener;
		Parameters.PlayerSessionService->GetHTTPManager()->AddRequest(HTTP);


		FTimeValue DurationSuccessfullyDelivered(FTimeValue::GetZero());
		FTimeValue DurationSuccessfullyRead(FTimeValue::GetZero());
		FTimeValue NextLargestExpectedTimestamp(FTimeValue::GetZero());
		bool bDone = false;
		TSharedPtr<IParserISO14496_12::IAllTrackIterator, ESPMode::ThreadSafe> AllTrackIterator = TimelineAsset->GetMoovBoxParser()->CreateAllTrackIteratorByFilePos(Request->FileStartOffset);
		while(!bDone && !HasErrored() && !HasBeenAborted() && !bTerminate)
		{
			auto UpdateSelectedTrack = [&SelectedTrackMap](const IParserISO14496_12::ITrackIterator* trkIt, TMap<uint32, FSelectedTrackData>& ActiveTrks) -> FSelectedTrackData&
			{
				const IParserISO14496_12::ITrack* Track = trkIt->GetTrack();
				uint32 tkid = Track->GetID();

				// Check if this track ID is already in our map of active tracks.
				FSelectedTrackData& st = ActiveTrks.FindOrAdd(tkid);
				if (!st.StreamSourceInfo.IsValid())
				{
					auto meta = MakeShared<FStreamSourceInfo, ESPMode::ThreadSafe>();
					meta->NumericTrackID = tkid;

					// Check if this track is in the list of selected tracks.
					const FPlaylistTrackMetadata* SelectedTrackMetadata = SelectedTrackMap.Find(tkid);
					if (SelectedTrackMetadata)
					{
						st.bIsSelectedTrack = true;
						st.StreamType = SelectedTrackMetadata->Type;
						//meta->Role			 = SelectedTrackMetadata->;
						meta->Language  		 = SelectedTrackMetadata->Language;
						meta->PeriodID  		 = SelectedTrackMetadata->PeriodID;
						meta->AdaptationSetID    = SelectedTrackMetadata->AdaptationSetID;
						meta->RepresentationID   = SelectedTrackMetadata->RepresentationID;
					}
					st.StreamSourceInfo = MoveTemp(meta);
				}
				if (!st.CSD.IsValid())
				{
					TSharedPtrTS<FAccessUnit::CodecData> CSD(new FAccessUnit::CodecData);
					CSD->CodecSpecificData = Track->GetCodecSpecificData();
					CSD->RawCSD			= Track->GetCodecSpecificDataRAW();
					CSD->ParsedInfo 		= Track->GetCodecInformation();
					st.CSD = MoveTemp(CSD);
				}
				return st;
			};

			// Handle all the new tracks that have reached EOS while iterating. We do this first here to
			// handle the tracks that hit EOS before reaching the intended start position.
			TArray<const IParserISO14496_12::ITrackIterator*> TracksAtEOS;
			AllTrackIterator->GetNewEOSTracks(TracksAtEOS);
			AllTrackIterator->ClearNewEOSTracks();
			for(int32 nTrk=0; nTrk<TracksAtEOS.Num(); ++nTrk)
			{
				const IParserISO14496_12::ITrackIterator* TrackIt = TracksAtEOS[nTrk];
				FSelectedTrackData& SelectedTrack = UpdateSelectedTrack(TrackIt, ActiveTrackMap);
				// Is this a track that is selected and we are interested in?
				if (SelectedTrack.bIsSelectedTrack)
				{
					Parameters.EventListener->OnFragmentReachedEOS(SelectedTrack.StreamType, SelectedTrack.StreamSourceInfo);
				}
			}

			// Handle current track iterator
			const IParserISO14496_12::ITrackIterator* TrackIt = AllTrackIterator->Current();
			if (TrackIt)
			{
				FSelectedTrackData& SelectedTrack = UpdateSelectedTrack(TrackIt, ActiveTrackMap);

				// Get the sample properties
				uint32 SampleNumber    = TrackIt->GetSampleNumber();
				int64 DTS   		   = TrackIt->GetDTS();
				int64 PTS   		   = TrackIt->GetPTS();
				int64 Duration  	   = TrackIt->GetDuration();
				uint32 Timescale	   = TrackIt->GetTimescale();
				bool bIsSyncSample     = TrackIt->IsSyncSample();
				int64 SampleSize	   = TrackIt->GetSampleSize();
				int64 SampleFileOffset = TrackIt->GetSampleFileOffset();

				// Remember at which file position we are currently at. In case of failure this is where we will retry.
				Request->CurrentIteratorBytePos = SampleFileOffset;

				// Do we need to skip over some data?
				if (SampleFileOffset > ReadBuffer.GetCurrentPos())
				{
					int32 NumBytesToSkip = SampleFileOffset - ReadBuffer.GetCurrentPos();
					int64 nr = ReadBuffer.ReadTo(nullptr, NumBytesToSkip);
					if (nr != NumBytesToSkip)
					{
						bDone = true;
						break;
					}
				}
				else if (SampleFileOffset < ReadBuffer.GetCurrentPos())
				{
					ds.bParseFailure  = true;
					Request->ConnectionInfo.StatusInfo.ErrorDetail.SetMessage(FString::Printf(TEXT("Segment parse error. Sample offset %lld for sample #%u in track %u is before the current read position at %lld"), (long long int)SampleFileOffset, SampleNumber, TrackIt->GetTrack()->GetID(), (long long int)ReadBuffer.GetCurrentPos()));
					bHasErrored = true;
					break;
				}

				// Do we read the sample because the track is selected or do we discard it?
				if (SelectedTrack.bIsSelectedTrack)
				{
					// Is this a sync sample?
					if (bIsSyncSample && !SelectedTrack.bGotKeyframe)
					{
						SelectedTrack.bGotKeyframe = true;
					}
					// Do we need to skip samples from this track until we reach a sync sample?
					bool bSkipUntilSyncSample = !SelectedTrack.bGotKeyframe && !Request->bIsContinuationSegment;

					// Create an access unit.
					FAccessUnit *AccessUnit = FAccessUnit::Create(Parameters.MemoryProvider);
					if (AccessUnit)
					{
						AccessUnit->ESType = SelectedTrack.StreamType;
						AccessUnit->PTS.SetFromND(PTS, Timescale);
						AccessUnit->DTS.SetFromND(DTS, Timescale);
						AccessUnit->Duration.SetFromND(Duration, Timescale);
						AccessUnit->AUSize = (uint32) SampleSize;
						AccessUnit->AUCodecData = SelectedTrack.CSD;
						AccessUnit->DropState = FAccessUnit::EDropState::None;
						// If this is a continuation then we must not tag samples as being too early.
						if (!Request->bIsContinuationSegment)
						{
							if (AccessUnit->DTS < Request->FirstPTS)
							{
								AccessUnit->DropState |= FAccessUnit::EDropState::DtsTooEarly;
							}
							if (AccessUnit->PTS < Request->FirstPTS)
							{
								AccessUnit->DropState |= FAccessUnit::EDropState::PtsTooEarly;
							}
						}
						// FIXME: if we only want to read a partial segment we could set drop state based on the sample being 'too late'.

						// Apply timestamp offsets for looping after checking the timestamp limits.
						AccessUnit->PTS += LoopTimestampOffset;
						AccessUnit->DTS += LoopTimestampOffset;

						AccessUnit->bIsFirstInSequence = SelectedTrack.bIsFirstInSequence;
						AccessUnit->bIsSyncSample = bIsSyncSample;
						AccessUnit->bIsDummyData = false;
						AccessUnit->AUData = AccessUnit->AllocatePayloadBuffer(AccessUnit->AUSize);

						// Set the associated stream metadata
						AccessUnit->StreamSourceInfo = SelectedTrack.StreamSourceInfo;
						AccessUnit->PlayerLoopState = PlayerLoopState;

						SelectedTrack.bIsFirstInSequence = false;

						int64 nr = ReadBuffer.ReadTo(AccessUnit->AUData, (int32)SampleSize);
						if (nr == SampleSize)
						{
							SelectedTrack.DurationSuccessfullyRead += AccessUnit->Duration;

							//LogMessage(IInfoLog::ELevel::Info, FString::Printf("[%u] %4u: DTS=%lld PTS=%lld dur=%lld sync=%d; %lld bytes @ %lld", tkid, SampleNumber, (long long int)AccessUnit->mDTS.GetAsMicroseconds(), (long long int)AccessUnit->mPTS.GetAsMicroseconds(), (long long int)AccessUnit->mDuration.GetAsMicroseconds(), bIsSyncSample?1:0, (long long int)SampleSize, (long long int)SampleFileOffset));

							// Keep track of the next expected sample PTS and remember the largest value of all tracks.
							FTimeValue NextExpectedPTS = AccessUnit->PTS + AccessUnit->Duration;
							if (NextExpectedPTS > NextLargestExpectedTimestamp)
							{
								NextLargestExpectedTimestamp = NextExpectedPTS;
							}

							bool bSentOff = false;
							while(!bSentOff && !HasBeenAborted() && !bTerminate)
							{
								if (Parameters.EventListener->OnFragmentAccessUnitReceived(AccessUnit))
								{
									SelectedTrack.DurationSuccessfullyDelivered += AccessUnit->Duration;
									bSentOff = true;
									AccessUnit = nullptr;

									// Since we have delivered this access unit, if we are detecting an error now we need to then
									// retry on the _next_ AU and not this one again!
									Request->CurrentIteratorBytePos = SampleFileOffset + SampleSize;
								}
								else
								{
									FMediaRunnable::SleepMicroseconds(1000 * 10);
								}
							}

							// Release the AU if we still have it.
							FAccessUnit::Release(AccessUnit);
							AccessUnit = nullptr;

							// For error handling, if we managed to get additional data we reset the retry count.
							if (ds.RetryNumber && SelectedTrack.DurationSuccessfullyRead.GetAsSeconds() > 2.0)
							{
								ds.RetryNumber = 0;
								Request->NumOverallRetries = 0;
							}
						}
						else
						{
							// Did not get the number of bytes we needed. Either because of a read error or because we got aborted.
							FAccessUnit::Release(AccessUnit);
							AccessUnit = nullptr;
							bDone = true;
							break;
						}
					}
					else
					{
						// TODO: Throw OOM error
					}
				}
			}
			else
			{
				break;
			}
			AllTrackIterator->Next();
		}

		// Remove the download request.
		ProgressListener.Reset();
		Parameters.PlayerSessionService->GetHTTPManager()->RemoveRequest(HTTP);
		Request->ConnectionInfo = HTTP->ConnectionInfo;
		HTTP.Reset();

		// Remember the next largest timestamp from all tracks.
		Request->NextLargestExpectedTimestamp = NextLargestExpectedTimestamp;


		// Set downloaded and delivered duration from the primary track.
		FSelectedTrackData& PrimaryTrack = ActiveTrackMap.FindOrAdd(PrimaryTrackID);
		DurationSuccessfullyRead	  = PrimaryTrack.DurationSuccessfullyRead;
		DurationSuccessfullyDelivered = PrimaryTrack.DurationSuccessfullyDelivered;

		// Set up remaining download stat fields.
// Note: currently commented out because of UE-88612.
//       This must be reinstated once we set failure reasons in the loop above so they won't get replaced by this!
//		if (ds.FailureReason.length() == 0)
		{
			ds.FailureReason = Request->ConnectionInfo.StatusInfo.ErrorDetail.GetMessage();
		}
//		if (bAbortedByABR)
//		{
//			// If aborted set the reason as the download failure.
//			ds.FailureReason = ds.ABRState.ProgressDecision.Reason;
//		}
//		ds.bWasAborted  	  = bAbortedByABR;
//		ds.bWasSuccessful     = !bHasErrored && !bAbortedByABR;
		ds.bWasSuccessful     = !bHasErrored;
		ds.URL  			  = Request->ConnectionInfo.EffectiveURL;
		ds.HTTPStatusCode     = Request->ConnectionInfo.StatusInfo.HTTPStatus;
		ds.DurationDownloaded = DurationSuccessfullyRead.GetAsSeconds();
		ds.DurationDelivered  = DurationSuccessfullyDelivered.GetAsSeconds();
		ds.TimeToFirstByte    = Request->ConnectionInfo.TimeUntilFirstByte;
		ds.TimeToDownload     = (Request->ConnectionInfo.RequestEndTime - Request->ConnectionInfo.RequestStartTime).GetAsSeconds();
		ds.ByteSize 		  = Request->ConnectionInfo.ContentLength;
		ds.NumBytesDownloaded = Request->ConnectionInfo.BytesReadSoFar;
		ds.ThroughputBps	  = Request->ConnectionInfo.Throughput.GetThroughput();
		if (ds.ThroughputBps == 0)
		{
			ds.ThroughputBps = ds.TimeToDownload > 0.0 ? 8 * ds.NumBytesDownloaded / ds.TimeToDownload : 0;
		}

		ActiveTrackMap.Reset();

		// Reset the current request so another one can be added immediately when we call OnFragmentClose()
		CurrentRequest.Reset();
		Parameters.PlayerSessionService->GetStreamSelector()->ReportDownloadEnd(ds);
		Parameters.EventListener->OnFragmentClose(Request);
	}
}



int32 FStreamReaderMP4::FReadBuffer::ReadTo(void* ToBuffer, int32 NumBytes)
{
	FPODRingbuffer& SourceBuffer = ReceiveBuffer->Buffer;

	uint8* OutputBuffer = (uint8*)ToBuffer;
	// Do we have enough data in the ringbuffer to satisfy the read?
	if (SourceBuffer.Num() >= NumBytes)
	{
		// Yes. Get the data and return.
		int32 NumGot = SourceBuffer.PopData(OutputBuffer, NumBytes);
		check(NumGot == NumBytes);
		CurrentPos += NumBytes;
		return NumBytes;
	}
	else
	{
		// Do not have enough data yet or we want to read more than the ringbuffer can hold.
		int32 NumBytesToGo = NumBytes;
		while(NumBytesToGo > 0)
		{
			if (bHasErrored || SourceBuffer.WasAborted() || bAbort)
			{
				return -1;
			}
			// EOD?
			if (SourceBuffer.IsEndOfData())
			{
				return 0;
			}

			// Get whatever amount of data is currently available to free up the buffer for receiving more data.
			int32 NumGot = SourceBuffer.PopData(OutputBuffer, NumBytesToGo);
			if ((NumBytesToGo -= NumGot) > 0)
			{
				if (OutputBuffer)
				{
					OutputBuffer += NumGot;
				}
				// Wait for data to arrive in the ringbuffer.
				int32 WaitForBytes = NumBytesToGo > SourceBuffer.Capacity() ? SourceBuffer.Capacity() : NumBytesToGo;
				SourceBuffer.WaitUntilSizeAvailable(WaitForBytes, 1000 * 100);
			}
		}
		CurrentPos += NumBytes;
		return NumBytes;
	}
}


} // namespace Electra


