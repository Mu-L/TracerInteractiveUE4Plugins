// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Player/PlayerStreamReader.h"
#include "Player/AdaptiveStreamingPlayerResourceRequest.h"
#include "Demuxer/ParserISO14496-12.h"
#include "InitSegmentCacheHLS.h"
#include "LicenseKeyCacheHLS.h"
#include "HTTP/HTTPManager.h"
#include "Crypto/StreamCryptoAES128.h"

namespace Electra
{

class FStreamSegmentRequestHLSfmp4 : public IStreamSegment
{
public:
	FStreamSegmentRequestHLSfmp4();
	virtual ~FStreamSegmentRequestHLSfmp4();

	virtual void SetPlaybackSequenceID(uint32 PlaybackSequenceID) override;
	virtual uint32 GetPlaybackSequenceID() const override;


	virtual EStreamType GetType() const override;

	virtual void GetDependentStreams(TArray<FDependentStreams>& OutDependentStreams) const override;
	virtual void GetEndedStreams(TArray<TSharedPtrTS<IStreamSegment>>& OutAlreadyEndedStreams) override;

	//! Returns the first PTS value as indicated by the media timeline. This should correspond to the actual absolute PTS of the sample.
	virtual FTimeValue GetFirstPTS() const override;

	virtual int32 GetQualityIndex() const override;
	virtual int32 GetBitrate() const override;

	virtual void GetDownloadStats(Metrics::FSegmentDownloadStats& OutStats) const override;

	void CopyFrom(const FStreamSegmentRequestHLSfmp4& rhs);

	FString																		URL;
	IElectraHttpManager::FParams::FRange										Range;

	EStreamType																	StreamType;							//!< Type of stream (video, audio, etc.)
	uint32																		StreamUniqueID;						//!< The unique stream ID identifying the stream for which this is a request.
	int32																		Bitrate;
	int32																		QualityLevel;

	TSharedPtrTS<IPlaybackAssetRepresentation>									Representation;
	TSharedPtrTS<IPlaybackAssetAdaptationSet>									AdaptationSet;
	TSharedPtrTS<ITimelineMediaAsset>											MediaAsset;
	FString																		CDN;

	//FTimeValue																	PlaylistRelativeStartTime;			//!< The start time of this segment within the media playlist.
	FTimeValue																	AbsoluteDateTime;					//!< The absolute start time of this segment as declared through EXT-X-PROGRAM-DATE-TIME mapping
	FTimeValue																	SegmentDuration;					//!< Duration of the segment as specified in the media playlist.
	int64																		MediaSequence;						//!< The media sequence number of this segment.
	int64																		DiscontinuitySequence;				//!< The discontinuity index after which this segment is located in the media playlist.
	int32																		LocalIndex;							//!< Local index of the segment in the media playlist at the time the request was generated.

	FTimeValue																	FirstAUTimeOffset;					//!< A time offset into the segment to the first access unit to be sent to the decoder (audio).

	bool																		bIsPrefetch;
	bool																		bIsEOSSegment;

	int32																		NumOverallRetries;					//!< Number of retries for this _segment_ across all possible quality levels and CDNs.
	bool																		bInsertFillerData;

	bool																		bHasEncryptedSegments;

	TSharedPtrTS<IInitSegmentCacheHLS> 											InitSegmentCache;
	TSharedPtrTS<const FManifestHLSInternal::FMediaStream::FInitSegmentInfo>	InitSegmentInfo;

	TSharedPtrTS<ILicenseKeyCacheHLS> 											LicenseKeyCache;
	TSharedPtrTS<const FManifestHLSInternal::FMediaStream::FDRMKeyInfo>			LicenseKeyInfo;

	// List of dependent streams. Usually set for initial playback start requests.
	TArray<TSharedPtrTS<FStreamSegmentRequestHLSfmp4>>							DependentStreams;
	bool																		bIsInitialStartRequest;

	FPlayerLoopState															PlayerLoopState;

	uint32																		CurrentPlaybackSequenceID;			//!< Set by the player before adding the request to the stream reader.

	Metrics::FSegmentDownloadStats												DownloadStats;
	HTTP::FConnectionInfo														ConnectionInfo;
	FTimeValue																	NextLargestExpectedTimestamp;	//!< Largest timestamp of all samples (plus its duration) across all tracks.
};



/**
 *
**/
class FStreamReaderHLSfmp4 : public IStreamReader
{
public:
	FStreamReaderHLSfmp4();
	virtual ~FStreamReaderHLSfmp4();

	virtual UEMediaError Create(IPlayerSessionServices* PlayerSessionService, const CreateParam& InCreateParam) override;
	virtual void Close() override;

	//! Adds a request to read from a stream
	virtual EAddResult AddRequest(uint32 CurrentPlaybackSequenceID, TSharedPtrTS<IStreamSegment> Request) override;

	//! Pauses all pending requests.
	virtual void PauseDownload() override;
	//! Resumes all pending requests.
	virtual void ResumeDownload() override;
	//! Cancels all pending requests.
	virtual void CancelRequests() override;

private:

	struct FStreamHandler : public FMediaThread, public IParserISO14496_12::IReader, public IParserISO14496_12::IBoxCallback
	{
		struct FReadBuffer
		{
			FReadBuffer()
			{
				Reset();
			}
			void Reset()
			{
				ReceiveBuffer.Reset();
				ParsePos = 0;
				MaxParsePos = TNumericLimits<int64>::Max();
				DecryptedPos = 0;
			}
			TSharedPtrTS<IElectraHttpManager::FReceiveBuffer>	ReceiveBuffer;
			int64												ParsePos;
			int64												MaxParsePos;
			int32												DecryptedPos;
		};


		class FStaticResourceRequest : public IAdaptiveStreamingPlayerResourceRequest
		{
		public:
			FStaticResourceRequest(FString InURL, EPlaybackResourceType InType)
				: URL(InURL)
				, Type(InType)
			{ }

			virtual ~FStaticResourceRequest()
			{ }

			virtual EPlaybackResourceType GetResourceType() const override
			{ return Type; }

			virtual FString GetResourceURL() const override
			{ return URL; }

			virtual void SetPlaybackData(TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe>	PlaybackData) override
			{ Data = PlaybackData; }

			virtual void SignalDataReady() override
			{ DoneSignal.Signal(); }

			bool IsDone() const
			{ return DoneSignal.IsSignaled(); }
			
			bool WaitDone(int32 WaitMicros)
			{ return DoneSignal.WaitTimeout(WaitMicros); }

			TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe> GetData()
			{ return Data; }

		private:
			FString												URL;
			TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe>		Data;
			FMediaEvent											DoneSignal;
			EPlaybackResourceType								Type;
		};

		enum class EInitSegmentResult
		{
			Ok,
			AlreadyCached,
			DownloadError,
			ParseError,
			LicenseKeyError
		};

		enum class ELicenseKeyResult
		{
			Ok,
			AlreadyCached,
			DownloadError,
			FormatError
		};

		static uint32											UniqueDownloadID;

		IStreamReader::CreateParam								Parameters;
		TSharedPtrTS<FStreamSegmentRequestHLSfmp4>				CurrentRequest;
		FMediaSemaphore											WorkSignal;
		volatile bool											bTerminate;
		volatile bool											bRequestCanceled;
		volatile bool											bHasErrored;
		bool													bAbortedByABR;
		bool													bAllowEarlyEmitting;
		bool													bFillRemainingDuration;

		IPlayerSessionServices*									PlayerSessionService;
		FReadBuffer												ReadBuffer;
		TSharedPtr<IStreamDecrypterAES128, ESPMode::ThreadSafe>	Decrypter;
		FMediaEvent												DownloadCompleteSignal;
		TSharedPtrTS<IParserISO14496_12>						MP4Parser;
		int32													NumMOOFBoxesFound;

		TMediaQueueDynamicNoLock<FAccessUnit *>					AccessUnitFIFO;
		FTimeValue 												DurationSuccessfullyDelivered;

		FMediaCriticalSection									MetricUpdateLock;
		int32													ProgressReportCount;
		TSharedPtrTS<IAdaptiveStreamSelector>					StreamSelector;


		FStreamHandler();
		virtual ~FStreamHandler();
		void Cancel();
		void SignalWork();
		void WorkerThread();
		void HandleRequest();
		FString DemoteMediaURLToHTTP(const FString& InURL, bool bIsEncrypted);
		FString DemoteInitURLToHTTP(const FString& InURL, bool bIsEncrypted);
		EInitSegmentResult GetInitSegment(FErrorDetail& OutErrorDetail, TSharedPtrTS<const IParserISO14496_12>& OutMP4InitSegment, const TSharedPtrTS<FStreamSegmentRequestHLSfmp4>& InRequest);
		ELicenseKeyResult GetLicenseKey(FErrorDetail& OutErrorDetail, TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe>& OutLicenseKeyData, const TSharedPtrTS<FStreamSegmentRequestHLSfmp4>& InRequest, const TSharedPtr<const FManifestHLSInternal::FMediaStream::FDRMKeyInfo, ESPMode::ThreadSafe>& LicenseKeyInfo);

		void LogMessage(IInfoLog::ELevel Level, const FString& Message);

		int32 HTTPProgressCallback(const IElectraHttpManager::FRequest* Request);
		void HTTPCompletionCallback(const IElectraHttpManager::FRequest* Request);
		void HTTPUpdateStats(const FTimeValue& CurrentTime, const IElectraHttpManager::FRequest* Request);


		bool HasErrored() const;

		// Methods from IParserISO14496_12::IReader
		virtual int64 ReadData(void* IntoBuffer, int64 NumBytesToRead) override;
		virtual bool HasReachedEOF() const override;
		virtual bool HasReadBeenAborted() const override;
		virtual int64 GetCurrentOffset() const override;
		// Methods from IParserISO14496_12::IBoxCallback
		virtual IParserISO14496_12::IBoxCallback::EParseContinuation OnFoundBox(IParserISO14496_12::FBoxType Box, int64 BoxSizeInBytes, int64 FileDataOffset, int64 BoxDataOffset) override;
	};

	// Currently set to use 2 handlers, one for video and one for audio. This could become a pool of n if we need to stream
	// multiple dependent segments, keeping a pool of available and active handlers to cycle between.
	FStreamHandler						StreamHandlers[2];		// 0 = video (MEDIAstreamType_Video), 1 = audio (MEDIAstreamType_Audio)
	IPlayerSessionServices*				PlayerSessionService;
	bool								bIsStarted;
	FErrorDetail						ErrorDetail;

	static const FString		OptionKeyDontUseInsecureForEncryptedMediaSegments;		//!< (bool) if false and media segment is using EXT-X-KEY encryption fetch it via http even if it should be https, otherwise keep the original scheme.
	static const FString		OptionKeyDontUseInsecureForInitSegments;				//!< (bool) if false the init segment is fetched via http even if it should be https, otherwise keep the original scheme.
};




} // namespace Electra

