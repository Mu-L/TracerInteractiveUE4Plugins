// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImgMediaLoaderWork.h"
#include "ImgMediaPrivate.h"

#include "Misc/ScopeLock.h"

#include "IImgMediaReader.h"
#include "ImgMediaLoader.h"


/** Time spent abandoning worker threads. */
DECLARE_CYCLE_STAT(TEXT("ImgMedia Loader Abadon Work"), STAT_ImgMedia_LoaderAbandonWork, STATGROUP_Media);

/** Time spent finalizing worker threads. */
DECLARE_CYCLE_STAT(TEXT("ImgMedia Loader Finalize Work"), STAT_ImgMedia_LoaderFinalizeWork, STATGROUP_Media);

/** Time spent reading image frames in worker threads. */
DECLARE_CYCLE_STAT(TEXT("ImgMedia Loader Read Frame"), STAT_ImgMedia_LoaderReadFrame, STATGROUP_Media);


/* FImgMediaLoaderWork structors
 *****************************************************************************/

FImgMediaLoaderWork::FImgMediaLoaderWork(const TSharedRef<FImgMediaLoader, ESPMode::ThreadSafe>& InOwner, const TSharedRef<IImgMediaReader, ESPMode::ThreadSafe>& InReader)
	: FrameNumber(INDEX_NONE)
	, OwnerPtr(InOwner)
	, Reader(InReader)
{ }


/* FImgMediaLoaderWork interface
 *****************************************************************************/

void FImgMediaLoaderWork::Initialize(int32 InFrameNumber, const FString& InImagePath)
{
	FrameNumber = InFrameNumber;
	ImagePath = InImagePath;
}


/* IQueuedWork interface
 *****************************************************************************/

void FImgMediaLoaderWork::Abandon()
{
	SCOPE_CYCLE_COUNTER(STAT_ImgMedia_LoaderAbandonWork);

	Finalize(nullptr);
}


void FImgMediaLoaderWork::DoThreadedWork()
{
	TSharedPtr<FImgMediaFrame, ESPMode::ThreadSafe> Frame;
	UE_LOG(LogImgMedia, Verbose, TEXT("Loader %p: Starting to read %i"), this, FrameNumber);

	if ((FrameNumber == INDEX_NONE) || ImagePath.IsEmpty())
	{
		Frame.Reset();
	}
	else
	{
		SCOPE_CYCLE_COUNTER(STAT_ImgMedia_LoaderReadFrame);

		// read the image frame
		Frame = MakeShareable(new FImgMediaFrame());

		if (!Reader->ReadFrame(ImagePath, Frame, FrameNumber))
		{
			Frame.Reset();
		}
	}

	SCOPE_CYCLE_COUNTER(STAT_ImgMedia_LoaderFinalizeWork);

	Finalize(Frame);
}


/* FImgMediaLoaderWork implementation
 *****************************************************************************/

void FImgMediaLoaderWork::Finalize(TSharedPtr<FImgMediaFrame, ESPMode::ThreadSafe> Frame)
{
	TSharedPtr<FImgMediaLoader, ESPMode::ThreadSafe> Owner = OwnerPtr.Pin();

	if (Owner.IsValid())
	{
		Owner->NotifyWorkComplete(*this, FrameNumber, Frame);
	}
	else
	{
		Frame.Reset();
		Frame = nullptr;
		delete this; // owner is gone, self destruct!
	}
}
