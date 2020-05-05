// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tracks/MovieSceneAudioTrack.h"
#include "Audio.h"
#include "Sound/SoundWave.h"
#include "MovieScene.h"
#include "Sections/MovieSceneAudioSection.h"
#include "Kismet/GameplayStatics.h"
#include "AudioDecompress.h"
#include "Evaluation/MovieSceneSegment.h"
#include "Compilation/MovieSceneSegmentCompiler.h"
#include "Compilation/MovieSceneCompilerRules.h"
#include "MovieSceneCommonHelpers.h"

#define LOCTEXT_NAMESPACE "MovieSceneAudioTrack"


UMovieSceneAudioTrack::UMovieSceneAudioTrack( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{
	SupportedBlendTypes.Add(EMovieSceneBlendType::Absolute);
#if WITH_EDITORONLY_DATA
	TrackTint = FColor(93, 95, 136);
	RowHeight = 50;
#endif
}


const TArray<UMovieSceneSection*>& UMovieSceneAudioTrack::GetAllSections() const
{
	return AudioSections;
}


bool UMovieSceneAudioTrack::SupportsMultipleRows() const
{
	return true;
}

bool UMovieSceneAudioTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UMovieSceneAudioSection::StaticClass();
}

void UMovieSceneAudioTrack::RemoveAllAnimationData()
{
	AudioSections.Empty();
}


bool UMovieSceneAudioTrack::HasSection(const UMovieSceneSection& Section) const
{
	return AudioSections.Contains(&Section);
}


void UMovieSceneAudioTrack::AddSection(UMovieSceneSection& Section)
{
	AudioSections.Add(&Section);
}


void UMovieSceneAudioTrack::RemoveSection(UMovieSceneSection& Section)
{
	AudioSections.Remove(&Section);
}


void UMovieSceneAudioTrack::RemoveSectionAt(int32 SectionIndex)
{
	AudioSections.RemoveAt(SectionIndex);
}


bool UMovieSceneAudioTrack::IsEmpty() const
{
	return AudioSections.Num() == 0;
}


UMovieSceneSection* UMovieSceneAudioTrack::AddNewSoundOnRow(USoundBase* Sound, FFrameNumber Time, int32 RowIndex)
{
	check(Sound);
	
	FFrameRate FrameRate = GetTypedOuter<UMovieScene>()->GetTickResolution();

	// determine initial duration
	// @todo Once we have infinite sections, we can remove this
	// @todo ^^ Why? Infinte sections would mean there's no starting time?
	FFrameTime DurationToUse = 1.f * FrameRate; // if all else fails, use 1 second duration

	float SoundDuration = MovieSceneHelpers::GetSoundDuration(Sound);
	if (SoundDuration != INDEFINITELY_LOOPING_DURATION)
	{
		DurationToUse = SoundDuration * FrameRate;
	}

	// add the section
	UMovieSceneAudioSection* NewSection = NewObject<UMovieSceneAudioSection>(this, NAME_None, RF_Transactional);
	NewSection->InitialPlacementOnRow( AudioSections, Time, DurationToUse.FrameNumber.Value, RowIndex );
	NewSection->SetSound(Sound);

	AudioSections.Add(NewSection);

	return NewSection;
}


bool UMovieSceneAudioTrack::IsAMasterTrack() const
{
	UMovieScene* MovieScene = Cast<UMovieScene>(GetOuter());
	return MovieScene ? MovieScene->IsAMasterTrack(*this) : false;
}


FMovieSceneTrackRowSegmentBlenderPtr UMovieSceneAudioTrack::GetRowSegmentBlender() const
{
	struct FBlender : FMovieSceneTrackRowSegmentBlender
	{
		virtual void Blend(FSegmentBlendData& BlendData) const override
		{
			// Run the default high pass filter for overlap priority
			MovieSceneSegmentCompiler::FilterOutUnderlappingSections(BlendData);
		}
	};
	return FBlender();
}

UMovieSceneSection* UMovieSceneAudioTrack::CreateNewSection()
{
	return NewObject<UMovieSceneAudioSection>(this, NAME_None, RF_Transactional);
}

void UMovieSceneAudioTrack::PostRename(UObject* OldOuter, const FName OldName)
{
	// If this audio track is outered to something new, update the channel proxy because the channel can depend on whether this is a master track or not
	for (UMovieSceneSection* Section : AudioSections)
	{
		if (UMovieSceneAudioSection* AudioSection =Cast<UMovieSceneAudioSection>(Section))
		{
			AudioSection->UpdateChannelProxy();
		}
	}
}

#undef LOCTEXT_NAMESPACE
