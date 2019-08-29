// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "Tracks/MovieSceneParticleTrack.h"
#include "MovieSceneCommonHelpers.h"
#include "Sections/MovieSceneParticleSection.h"


#define LOCTEXT_NAMESPACE "MovieSceneParticleTrack"


UMovieSceneParticleTrack::UMovieSceneParticleTrack( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{
#if WITH_EDITORONLY_DATA
	TrackTint = FColor(255,255,255,160);
#endif
}


const TArray<UMovieSceneSection*>& UMovieSceneParticleTrack::GetAllSections() const
{
	return ParticleSections;
}


void UMovieSceneParticleTrack::RemoveAllAnimationData()
{
	// do nothing
}


bool UMovieSceneParticleTrack::HasSection(const UMovieSceneSection& Section) const
{
	return ParticleSections.Contains(&Section);
}


void UMovieSceneParticleTrack::AddSection(UMovieSceneSection& Section)
{
	ParticleSections.Add(&Section);
}


void UMovieSceneParticleTrack::RemoveSection(UMovieSceneSection& Section)
{
	ParticleSections.Remove(&Section);
}


bool UMovieSceneParticleTrack::IsEmpty() const
{
	return ParticleSections.Num() == 0;
}


void UMovieSceneParticleTrack::AddNewSection( FFrameNumber SectionTime )
{
	if ( MovieSceneHelpers::FindSectionAtTime( ParticleSections, SectionTime ) == nullptr )
	{
		UMovieSceneParticleSection* NewSection = Cast<UMovieSceneParticleSection>( CreateNewSection() );
		ParticleSections.Add(NewSection);
	}
}

UMovieSceneSection* UMovieSceneParticleTrack::CreateNewSection()
{
	return NewObject<UMovieSceneParticleSection>( this, NAME_None, RF_Transactional );
}

#if WITH_EDITORONLY_DATA
FText UMovieSceneParticleTrack::GetDefaultDisplayName() const
{
	return LOCTEXT("DisplayName", "Particle System");
}
#endif

#undef LOCTEXT_NAMESPACE
