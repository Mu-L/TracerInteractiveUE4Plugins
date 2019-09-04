// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Tracks/MovieSceneMaterialTrack.h"
#include "MovieSceneCommonHelpers.h"
#include "Evaluation/MovieSceneParameterTemplate.h"
#include "Evaluation/MovieSceneEvaluationTrack.h"

UMovieSceneMaterialTrack::UMovieSceneMaterialTrack(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	TrackTint = FColor(64,192,64,65);
#endif
}


bool UMovieSceneMaterialTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UMovieSceneParameterSection::StaticClass();
}

UMovieSceneSection* UMovieSceneMaterialTrack::CreateNewSection()
{
	return NewObject<UMovieSceneParameterSection>(this, NAME_None, RF_Transactional);
}


void UMovieSceneMaterialTrack::RemoveAllAnimationData()
{
	Sections.Empty();
}


bool UMovieSceneMaterialTrack::HasSection(const UMovieSceneSection& Section) const
{
	return Sections.Contains(&Section);
}


void UMovieSceneMaterialTrack::AddSection(UMovieSceneSection& Section)
{
	Sections.Add(&Section);
}


void UMovieSceneMaterialTrack::RemoveSection(UMovieSceneSection& Section)
{
	Sections.Remove(&Section);
}

void UMovieSceneMaterialTrack::RemoveSectionAt(int32 SectionIndex)
{
	Sections.RemoveAt(SectionIndex);
}


bool UMovieSceneMaterialTrack::IsEmpty() const
{
	return Sections.Num() == 0;
}


const TArray<UMovieSceneSection*>& UMovieSceneMaterialTrack::GetAllSections() const
{
	return Sections;
}


void UMovieSceneMaterialTrack::AddScalarParameterKey(FName ParameterName, FFrameNumber Time, float Value)
{
	UMovieSceneParameterSection* NearestSection = Cast<UMovieSceneParameterSection>(MovieSceneHelpers::FindNearestSectionAtTime(Sections, Time));
	if (NearestSection == nullptr)
	{
		NearestSection = Cast<UMovieSceneParameterSection>(CreateNewSection());
		NearestSection->SetRange(TRange<FFrameNumber>::Inclusive(Time, Time));
		Sections.Add(NearestSection);
	}
	if (NearestSection->TryModify())
	{
		NearestSection->AddScalarParameterKey(ParameterName, Time, Value);
	}
}


void UMovieSceneMaterialTrack::AddColorParameterKey(FName ParameterName, FFrameNumber Time, FLinearColor Value)
{
	UMovieSceneParameterSection* NearestSection = Cast<UMovieSceneParameterSection>(MovieSceneHelpers::FindNearestSectionAtTime(Sections, Time));
	if (NearestSection == nullptr)
	{
		NearestSection = Cast<UMovieSceneParameterSection>(CreateNewSection());
		NearestSection->SetRange(TRange<FFrameNumber>::Inclusive(Time, Time));
		Sections.Add(NearestSection);
	}
	if (NearestSection->TryModify())
	{
		NearestSection->AddColorParameterKey(ParameterName, Time, Value);
	}
}


UMovieSceneComponentMaterialTrack::UMovieSceneComponentMaterialTrack(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	
}


FMovieSceneEvalTemplatePtr UMovieSceneComponentMaterialTrack::CreateTemplateForSection(const UMovieSceneSection& InSection) const
{
	return FMovieSceneComponentMaterialSectionTemplate(*CastChecked<UMovieSceneParameterSection>(&InSection), *this);
}

void UMovieSceneComponentMaterialTrack::PostCompile(FMovieSceneEvaluationTrack& OutTrack, const FMovieSceneTrackCompilerArgs& Args) const
{
	OutTrack.SetEvaluationPriority(EvaluationPriority);
}


#if WITH_EDITORONLY_DATA
FText UMovieSceneComponentMaterialTrack::GetDefaultDisplayName() const
{
	return FText::FromString(FString::Printf(TEXT("Material Element %i"), MaterialIndex));
}
#endif
