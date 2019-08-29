// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "GenericPlatform/GenericPlatformMath.h"
#include "Misc/FrameNumber.h"
#include "Misc/FrameRate.h"

/**
 * A timecode that stores time in HH:MM:SS format with the remainder
 * of time represented by an integer frame count.
 */
struct FTimecode
{
	/**
	* Default construction for UObject purposes
	*/
	FTimecode()
		: Hours(0)
		, Minutes(0)
		, Seconds(0)
		, Frames(0)
		, bDropFrameFormat(false)
	{}

	/**
	* User construction from a number of hours minutes seconds and frames.
	* @param InbDropFrame - If true, this Timecode represents a "Drop Frame Timecode" format which
							skips the first frames of every minute (except those ending in multiples of 10)
							to account for drift when using a fractional NTSC framerate.
	*/
	FTimecode(int32 InHours, int32 InMinutes, int32 InSeconds, int32 InFrames, bool InbDropFrame)
		: Hours(InHours)
		, Minutes(InMinutes)
		, Seconds(InSeconds)
		, Frames(InFrames)
		, bDropFrameFormat(InbDropFrame)
	{}

public:

	/**
	 * Converts this Timecode back into a Frame Number at the given framerate, taking into account if this is a drop-frame format timecode.
	 */
	FFrameNumber ToFrameNumber(const FFrameRate& InFrameRate) const
	{
		const int32 NumberOfFramesInSecond = FMath::CeilToInt(InFrameRate.AsDecimal());
		const int32 NumberOfFramesInMinute = NumberOfFramesInSecond * 60;
		const int32 NumberOfFramesInHour = NumberOfFramesInMinute * 60;

		// Do a quick pre-pass to take any overflow values and move them into bigger time units.
		int32 SafeSeconds = Seconds + Frames / NumberOfFramesInSecond;
		int32 SafeFrames = Frames % NumberOfFramesInSecond;

		int32 SafeMinutes = Minutes + SafeSeconds / 60;
		SafeSeconds = SafeSeconds % 60;

		int32 SafeHours = Hours + SafeMinutes / 60;
		SafeMinutes = SafeMinutes % 60;

		if (bDropFrameFormat)
		{
			const int32 NumberOfTimecodesToDrop = NumberOfFramesInSecond <= 30 ? 2 : 4;

			// Calculate how many minutes there are total so we can know how many times we've skipped timecodes.
			int32 TotalMinutes = (SafeHours * 60) + SafeMinutes;

			// We skip timecodes 9 times out of every 10.
			int32 TotalDroppedFrames = NumberOfTimecodesToDrop * (TotalMinutes - (int32)FMath::RoundToZero(TotalMinutes / 10.0));
			int32 TotalFrames = (SafeHours * NumberOfFramesInHour) + (SafeMinutes * NumberOfFramesInMinute) + (SafeSeconds * NumberOfFramesInSecond)
				+ SafeFrames - TotalDroppedFrames;

			return FFrameNumber(TotalFrames);
		}
		else
		{
			int32 TotalFrames = (SafeHours *NumberOfFramesInHour) + (SafeMinutes * NumberOfFramesInMinute) + (SafeSeconds * NumberOfFramesInSecond) + SafeFrames;
			return FFrameNumber(TotalFrames);
		}
	}

	/**
	 * Create a FTimecode from a specific frame number at the given frame rate. Optionally supports creating a drop frame timecode,
	 * which drops certain timecode display numbers to help account for NTSC frame rates which are fractional.
	 *
	 * @param InFrameNumber - The frame number to convert into a timecode. This should already be converted to InFrameRate's resolution.
	 * @param InFrameRate	- The framerate that this timecode is based in. This should be the playback framerate as it is used to determine
	 *						  when the Frame value wraps over.
	 * @param InbDropFrame	- If true, the returned timecode will drop the first two frames on every minute (except when Minute % 10 == 0)
	 *						  This is only valid for NTSC framerates (29.97, 59.94) and will assert if you try to create a drop-frame format
	 *						  from a non-valid framerate. All framerates can be represented when in non-drop frame format.
	 */
	static FTimecode FromFrameNumber(const FFrameNumber& InFrameNumber, const FFrameRate& InFrameRate, bool InbDropFrame)
	{
		const int32 NumberOfFramesInSecond = FMath::CeilToInt(InFrameRate.AsDecimal());
		const int32 NumberOfFramesInMinute = NumberOfFramesInSecond * 60;
		const int32 NumberOfFramesInHour = NumberOfFramesInMinute * 60;

		if (InbDropFrame)
		{
			// Drop Frame Timecode (DFT) was created to address the issue with playing back whole frames at fractional framerates.
			// DFT is confusingly named though, as no frame numbers are actually dropped, only the display of them. At an ideal 30fps,
			// there are 108,000 frames in an hour. When played back at 29.97 however, there are only 107,892 frames per hour. This
			// leaves us a difference of 108 frames per hour (roughly ~3.6s!). DFT works by accumulating error until the error is significant
			// enough to catch up by a frame. This is accomplished by dropping two (or four) timecode numbers every minute which gives us a
			// total difference of 2*60 = 120 frames per hour. Unfortunately 120 puts us out of sync again as the difference is only 108 frames,
			// so we need to get 12 frames back. By not dropping frames every 10th minute, that gives us 2 frames * 6 (00, 10, 20, 30, 40, 50)
			// which gets us the 12 frame difference we need. In short, we drop frames (skip timecode numbers) every minute, on the minute, except
			// when (Minute % 10 == 0)

			// 29.97 drops two timecode values (frames 0 and 1) while 59.94 drops four values (frames 0, 1, 2 and 3)
			const int32 NumberOfTimecodesToDrop = NumberOfFramesInSecond <= 30 ? 2 : 4;

			// At an ideal 30fps there would be 18,000 frames every 10 minutes, but at 29.97 there's only 17,982 frames.
			const int32 NumTrueFramesPerTenMinutes = FMath::FloorToInt((60 * 10) * InFrameRate.AsDecimal());

			// Calculate out how many times we've skipped dropping frames (ie: Minute 15 gives us a value of 1, as we've only didn't drop frames on the 10th minute)
			const int32 NumTimesSkippedDroppingFrames = FMath::FloorToInt(FMath::Abs(InFrameNumber.Value) / (double)NumTrueFramesPerTenMinutes);

			// Now we can figure out how many frame (displays) have been skipped total; 9 times out of every 10 minutes
			const int32 NumFramesSkippedTotal = NumTimesSkippedDroppingFrames * 9 * NumberOfTimecodesToDrop;

			int32 OffsetFrame = FMath::Abs(InFrameNumber.Value);
			int FrameInTrueFrames = OffsetFrame % NumTrueFramesPerTenMinutes;

			// If we end up with a value of 0 or 1 (or 2 or 3 for 59.94) then we're not skipping timecode numbers this time.
			if (FrameInTrueFrames < NumberOfTimecodesToDrop)
			{
				OffsetFrame += NumFramesSkippedTotal;
			}
			else
			{
				// Each minute we slip a little bit more out of sync by a small amount, we just wait until we've accumulated enough error
				// to skip a whole frame and can catch up.
				const uint32 NumTrueFramesPerMinute = (uint32)FMath::FloorToInt(60 * InFrameRate.AsDecimal());

				// Figure out which minute we are (0-9) to see how many to skip
				int32 CurrentMinuteOfTen = FMath::FloorToInt((FrameInTrueFrames - NumberOfTimecodesToDrop) / (double)NumTrueFramesPerMinute);
				int NumAddedFrames = NumFramesSkippedTotal + (NumberOfTimecodesToDrop * CurrentMinuteOfTen);
				OffsetFrame += NumAddedFrames;
			}

			// Convert to negative timecode at the end if the original was negative.
			OffsetFrame *= FMath::Sign(InFrameNumber.Value);

			// Now that we've fudged what frames it thinks we're converting, we can do a standard Frame -> Timecode conversion.
			int32 Hours = (int32)FMath::RoundToZero(OffsetFrame / (double)NumberOfFramesInHour);
			int32 Minutes = (int32)FMath::RoundToZero(OffsetFrame / (double)NumberOfFramesInMinute) % 60;
			int32 Seconds = (int32)FMath::RoundToZero(OffsetFrame / (double)NumberOfFramesInSecond) % 60;
			int32 Frames = OffsetFrame % NumberOfFramesInSecond;

			return FTimecode(Hours, Minutes, Seconds, Frames, true);
		}
		else
		{
			// If we're in non-drop-frame we just convert straight through without fudging the frame numbers to skip certain timecodes.
			int32 Hours = (int32)FMath::RoundToZero(InFrameNumber.Value / (double)NumberOfFramesInHour);
			int32 Minutes = (int32)FMath::RoundToZero(InFrameNumber.Value / (double)NumberOfFramesInMinute) % 60;
			int32 Seconds = (int32)FMath::RoundToZero(InFrameNumber.Value / (double)NumberOfFramesInSecond) % 60;
			int32 Frames = InFrameNumber.Value % NumberOfFramesInSecond;

			return FTimecode(Hours, Minutes, Seconds, Frames, false);
		}
	}

	static bool IsDropFormatTimecodeSupported(const FFrameRate& InFrameRate)
	{
		// Drop Format Timecode is only valid for NTSC 23.976, 29.97, and 59.94.
		const FFrameRate TwentyThreeNineSevenSix = FFrameRate(24000, 1001);
		const FFrameRate TwentyNineNineSeven = FFrameRate(30000, 1001);
		const FFrameRate FiftyNineNineFour = FFrameRate(60000, 1001);

		return InFrameRate == TwentyThreeNineSevenSix || InFrameRate == TwentyNineNineSeven || InFrameRate == FiftyNineNineFour;
	}

	/**
	* Get the Qualified Timecode formatted in HH:MM:SS:FF or HH;MM;SS;FF depending on if this represents drop-frame timecode or not.
	* @param bForceSignDisplay - Forces the timecode to be prepended with a positive or negative sign.
	Standard behavior is to only show the sign when the value is negative.
	*/
	FString ToString(bool bForceSignDisplay = false) const
	{
		bool bHasNegativeComponent = Hours < 0 || Minutes < 0 || Seconds < 0 || Frames < 0;

		FString SignText;
		if (bHasNegativeComponent)
		{
			SignText = "- ";
		}
		else if (bForceSignDisplay)
		{
			SignText = "+ ";
		}

		if (bDropFrameFormat)
		{
			return FString::Printf(TEXT("%s%02d;%02d;%02d;%02d"), *SignText, FMath::Abs(Hours), FMath::Abs(Minutes), FMath::Abs(Seconds), FMath::Abs(Frames));
		}
		else
		{
			return FString::Printf(TEXT("%s%02d:%02d:%02d:%02d"), *SignText, FMath::Abs(Hours), FMath::Abs(Minutes), FMath::Abs(Seconds), FMath::Abs(Frames));
		}
	}

public:

	/** How many hours does this timecode represent */
	int32 Hours;

	/** How many minutes does this timecode represent */
	int32 Minutes;

	/** How many seconds does this timecode represent */
	int32 Seconds;

	/** How many frames does this timecode represent */
	int32 Frames;

	/** If true, this Timecode represents a Drop Frame timecode used to account for fractional frame rates in NTSC play rates. */
	bool bDropFrameFormat;
};