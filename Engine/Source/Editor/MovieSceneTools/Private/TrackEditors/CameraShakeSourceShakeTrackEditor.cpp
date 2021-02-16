// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrackEditors/CameraShakeSourceShakeTrackEditor.h"
#include "AssetRegistryModule.h"
#include "Camera/CameraAnim.h"
#include "Camera/CameraShake.h"
#include "Camera/CameraShakeSourceComponent.h"
#include "CommonMovieSceneTools.h"
#include "ContentBrowserModule.h"
#include "EditorStyleSet.h"
#include "Engine/Blueprint.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "IContentBrowserSingleton.h"
#include "MovieSceneTimeHelpers.h"
#include "Rendering/DrawElements.h"
#include "Sections/MovieSceneCameraShakeSourceShakeSection.h"
#include "Sections/MovieSceneCameraShakeSourceTriggerSection.h"
#include "SequencerSectionPainter.h"
#include "SequencerUtilities.h"
#include "Tracks/MovieSceneCameraShakeSourceShakeTrack.h"
#include "Tracks/MovieSceneCameraShakeSourceTriggerTrack.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Misc/PackageName.h"

#define LOCTEXT_NAMESPACE "FCameraShakeSourceShakeTrackEditor"

/**
 * Section interface for shake sections
 */
class FCameraShakeSourceShakeSection : public ISequencerSection
{
public:
	FCameraShakeSourceShakeSection(const TSharedPtr<ISequencer> InSequencer, UMovieSceneCameraShakeSourceShakeSection& InSection, const FGuid& InObjectBinding)
		: SequencerPtr(InSequencer)
		, SectionPtr(&InSection)
		, ObjectBinding(InObjectBinding)
	{}

	virtual FText GetSectionTitle() const override
	{
		const TSubclassOf<UCameraShakeBase> ShakeClass = GetCameraShakeClass();
		if (const UClass* ShakeClassPtr = ShakeClass.Get())
		{
			FCameraShakeDuration ShakeDuration;
			const bool bHasDuration = UCameraShakeBase::GetCameraShakeDuration(ShakeClass, ShakeDuration);
			if (bHasDuration)
			{
				// Workaround fix for Matinee camera shakes... will be fixed better in next release.
				const UMatineeCameraShake* MatineeCameraShakeCDO = Cast<UMatineeCameraShake>(ShakeClass->GetDefaultObject());
				if (ShakeDuration.IsFixed() || MatineeCameraShakeCDO != nullptr)
				{
					const float ShakeDurationTime = MatineeCameraShakeCDO ?
						FMath::Max(MatineeCameraShakeCDO->OscillationDuration, (MatineeCameraShakeCDO->Anim ? MatineeCameraShakeCDO->Anim->AnimLength : 0.f)) :
						ShakeDuration.Get();

					if (ShakeDurationTime > SMALL_NUMBER)
					{
						return FText::FromString(ShakeClassPtr->GetName());
					}
					else
					{
						return FText::Format(LOCTEXT("ShakeHasNoDurationWarning", "{0} (warning: shake has no duration)"), FText::FromString(ShakeClassPtr->GetName()));
					}
				}
				else
				{
					return FText::FromString(ShakeClassPtr->GetName());
				}
			}
			else
			{
				return FText::Format(LOCTEXT("ShakeIsInvalidWarning", "{0} (warning: shake is invalid)"), FText::FromString(ShakeClassPtr->GetName()));
			}
		}
		return LOCTEXT("NoCameraShake", "No Camera Shake");
	}

	virtual UMovieSceneSection* GetSectionObject() override
	{
		return SectionPtr.Get();
	}

	virtual bool IsReadOnly() const override 
	{ 
		return SectionPtr.IsValid() ? SectionPtr.Get()->IsReadOnly() : false; 
	}

	virtual int32 OnPaintSection(FSequencerSectionPainter& Painter) const override
	{
		static const FSlateBrush* GenericDivider = FEditorStyle::GetBrush("Sequencer.GenericDivider");

		Painter.LayerId = Painter.PaintSectionBackground();

		const UMovieSceneCameraShakeSourceShakeSection* SectionObject = SectionPtr.Get();
		const TSharedPtr<ISequencer> Sequencer = SequencerPtr.Pin();
		if (SectionObject == nullptr || !Sequencer.IsValid())
		{
			return Painter.LayerId;
		}

		const UMovieSceneSequence* FocusedSequence = Sequencer->GetFocusedMovieSceneSequence();
		if (FocusedSequence == nullptr)
		{
			return Painter.LayerId;
		}

		const FTimeToPixel& TimeConverter = Painter.GetTimeConverter();
		const FFrameRate TickResolution = FocusedSequence->GetMovieScene()->GetTickResolution();

		const ESlateDrawEffect DrawEffects = Painter.bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
		const TRange<FFrameNumber> SectionRange = SectionObject->GetRange();
		const int32 SectionSize = UE::MovieScene::DiscreteSize(SectionRange);
		const float SectionDuration = FFrameNumber(SectionSize) / TickResolution;

		const float SectionStartTime = SectionObject->GetInclusiveStartFrame() / TickResolution;
		const float SectionStartTimeInPixels = TimeConverter.SecondsToPixel(SectionStartTime);
		const float SectionEndTime = SectionObject->GetExclusiveEndFrame() / TickResolution;
		const float SectionEndTimeInPixels = TimeConverter.SecondsToPixel(SectionEndTime);

		const TSubclassOf<UCameraShakeBase> CameraShakeClass = GetCameraShakeClass();
		const bool bHasValidCameraShake = (CameraShakeClass.Get() != nullptr);
		if (bHasValidCameraShake)
		{
			FCameraShakeDuration ShakeDuration;
			UCameraShakeBase::GetCameraShakeDuration(CameraShakeClass, ShakeDuration);

			// Workaround fix for Matinee camera shakes... will be fixed better in next release.
			const UMatineeCameraShake* MatineeCameraShakeCDO = Cast<UMatineeCameraShake>(CameraShakeClass->GetDefaultObject());
			const float ShakeDurationTime = MatineeCameraShakeCDO ?
				FMath::Max(MatineeCameraShakeCDO->OscillationDuration, (MatineeCameraShakeCDO->Anim ? MatineeCameraShakeCDO->Anim->AnimLength : 0.f)) :
				ShakeDuration.Get();

			const float ShakeEndInPixels = (ShakeDuration.IsFixed() || MatineeCameraShakeCDO) ?
				TimeConverter.SecondsToPixel(FMath::Min(SectionStartTime + ShakeDurationTime, SectionEndTime)) :
				SectionEndTimeInPixels;
			const bool bSectionContainsEntireShake = ((ShakeDuration.IsFixed() || MatineeCameraShakeCDO) && SectionDuration > ShakeDurationTime);

			if (ShakeDuration.IsFixed() || MatineeCameraShakeCDO)
			{
				if (bSectionContainsEntireShake && SectionRange.HasLowerBound())
				{
					// Add some separator where the shake ends.
					float OffsetPixel = ShakeEndInPixels - SectionStartTimeInPixels;

					FSlateDrawElement::MakeBox(
							Painter.DrawElements,
							Painter.LayerId++,
							Painter.SectionGeometry.MakeChild(
								FVector2D(2.f, Painter.SectionGeometry.Size.Y - 2.f),
								FSlateLayoutTransform(FVector2D(OffsetPixel, 1.f))
							).ToPaintGeometry(),
							GenericDivider,
							DrawEffects
					);

					// Draw the rest in a "muted" color.
					const float OverflowSizeInPixels = SectionEndTimeInPixels - ShakeEndInPixels;

					FSlateDrawElement::MakeBox(
							Painter.DrawElements,
							Painter.LayerId++,
							Painter.SectionGeometry.MakeChild(
								FVector2D(OverflowSizeInPixels, Painter.SectionGeometry.Size.Y),
								FSlateLayoutTransform(FVector2D(OffsetPixel, 0))
							).ToPaintGeometry(),
							FEditorStyle::GetBrush("WhiteBrush"),
							ESlateDrawEffect::None,
							FLinearColor::Black.CopyWithNewOpacity(0.5f)
					);
				}

				float ShakeBlendIn = 0.f, ShakeBlendOut = 0.f;
				UCameraShakeBase::GetCameraShakeBlendTimes(CameraShakeClass, ShakeBlendIn, ShakeBlendOut);
				{
					// Draw the shake "intensity" as a line that goes up and down according to
					// blend in and out times.
					const FLinearColor LineColor(0.25f, 0.25f, 1.f, 0.75f);

					const bool bHasBlendIn = (ShakeBlendIn > SMALL_NUMBER);
					const bool bHasBlendOut = (ShakeBlendOut > SMALL_NUMBER);

					float ShakeBlendInEndInPixels = TimeConverter.SecondsToPixel(SectionStartTime + ShakeBlendIn);
					float ShakeBlendOutStartInPixels = ShakeEndInPixels - TimeConverter.SecondsDeltaToPixel(ShakeBlendOut);
					if (ShakeBlendInEndInPixels > ShakeBlendOutStartInPixels)
					{
						// If we have to blend out before we're done blending in, let's switch over
						// at the half mark.
						ShakeBlendInEndInPixels = ShakeBlendOutStartInPixels = (ShakeBlendInEndInPixels + ShakeBlendOutStartInPixels) / 2.f;
					}

					TArray<FVector2D> LinePoints;

					if (bHasBlendIn)
					{
						LinePoints.Add(FVector2D(SectionStartTimeInPixels, Painter.SectionGeometry.Size.Y - 2.f));
						LinePoints.Add(FVector2D(ShakeBlendInEndInPixels, 2.f));
					}
					else
					{
						LinePoints.Add(FVector2D(SectionStartTimeInPixels, 2.f));
					}

					if (bHasBlendOut)
					{
						LinePoints.Add(FVector2D(ShakeBlendOutStartInPixels, 2.f));
						LinePoints.Add(FVector2D(ShakeEndInPixels, Painter.SectionGeometry.Size.Y - 2.f));
					}
					else
					{
						LinePoints.Add(FVector2D(ShakeEndInPixels, 2.f));
					}

					FSlateDrawElement::MakeLines(
							Painter.DrawElements,
							Painter.LayerId++,
							Painter.SectionGeometry.ToPaintGeometry(),
							LinePoints,
							DrawEffects,
							LineColor);
				}
			}
			else
			{
				// Draw the shake in a "warning" orange colour.
				const float SectionDurationInPixels = TimeConverter.SecondsDeltaToPixel(SectionDuration);
				FSlateDrawElement::MakeBox(
						Painter.DrawElements,
						Painter.LayerId++,
						Painter.SectionGeometry.MakeChild(
							FVector2D(SectionDurationInPixels, Painter.SectionGeometry.Size.Y),
							FSlateLayoutTransform(FVector2D(SectionStartTimeInPixels, 0))
						).ToPaintGeometry(),
						FEditorStyle::GetBrush("WhiteBrush"),
						ESlateDrawEffect::None,
						FLinearColor(1.f, 0.5f, 0.f, 0.5f)
				);
			}
		}
		else
		{
			const float SectionDurationInPixels = TimeConverter.SecondsDeltaToPixel(SectionDuration);
			FSlateDrawElement::MakeBox(
					Painter.DrawElements,
					Painter.LayerId++,
					Painter.SectionGeometry.MakeChild(
						FVector2D(SectionDurationInPixels, Painter.SectionGeometry.Size.Y),
						FSlateLayoutTransform(FVector2D(SectionStartTimeInPixels, 0))
						).ToPaintGeometry(),
					FEditorStyle::GetBrush("WhiteBrush"),
					ESlateDrawEffect::None,
					FLinearColor::Red.CopyWithNewOpacity(0.5f)
					);
		}

		return Painter.LayerId;
	}

private:
	TSubclassOf<UCameraShakeBase> GetCameraShakeClass() const
	{
		if (const UMovieSceneCameraShakeSourceShakeSection* SectionObject = SectionPtr.Get())
		{
			if (SectionObject->ShakeData.ShakeClass.Get() != nullptr)
			{
				return SectionObject->ShakeData.ShakeClass;
			}
		}

		TSharedPtr<ISequencer> Sequencer = SequencerPtr.Pin();
		TArrayView<TWeakObjectPtr<>> BoundObjects = Sequencer->FindBoundObjects(ObjectBinding, Sequencer->GetFocusedTemplateID());
		if (BoundObjects.Num() > 0)
		{
			if (const UCameraShakeSourceComponent* Component = Cast<UCameraShakeSourceComponent>(BoundObjects[0].Get()))
			{
				return Component->CameraShake;
			}
		}

		return TSubclassOf<UCameraShakeBase>();
	}

	const UCameraShakeBase* GetCameraShakeDefaultObject() const
	{
		const TSubclassOf<UCameraShakeBase> ShakeClass = GetCameraShakeClass();
		if (const UClass* ShakeClassPtr = ShakeClass.Get())
		{
			return ShakeClassPtr->GetDefaultObject<UCameraShakeBase>();
		}
		return nullptr;
	}

private:
	TWeakPtr<ISequencer> SequencerPtr;
	TWeakObjectPtr<UMovieSceneCameraShakeSourceShakeSection> SectionPtr;
	FGuid ObjectBinding;
};

/**
 * Section interface for shake triggers.
 */
class FCameraShakeSourceTriggerSection : public FSequencerSection
{
public:
	FCameraShakeSourceTriggerSection(const TSharedPtr<ISequencer> InSequencer, UMovieSceneCameraShakeSourceTriggerSection& InSectionObject)
		: FSequencerSection(InSectionObject)
		, Sequencer(InSequencer)
	{}

private:
	virtual int32 OnPaintSection(FSequencerSectionPainter& Painter) const override;

	bool IsTrackSelected() const;
	void PaintShakeName(FSequencerSectionPainter& Painter, int32 LayerId, TSubclassOf<UCameraShakeBase> ShakeClass, float PixelPos) const;

	TWeakPtr<ISequencer> Sequencer;
};

bool FCameraShakeSourceTriggerSection::IsTrackSelected() const
{
	TSharedPtr<ISequencer> SequencerPtr = Sequencer.Pin();

	TArray<UMovieSceneTrack*> SelectedTracks;
	SequencerPtr->GetSelectedTracks(SelectedTracks);

	UMovieSceneSection* Section = WeakSection.Get();
	UMovieSceneTrack*   Track   = Section ? CastChecked<UMovieSceneTrack>(Section->GetOuter()) : nullptr;
	return Track && SelectedTracks.Contains(Track);
}

void FCameraShakeSourceTriggerSection::PaintShakeName(FSequencerSectionPainter& Painter, int32 LayerId, TSubclassOf<UCameraShakeBase> ShakeClass, float PixelPos) const
{
	static const int32   FontSize      = 10;
	static const float   BoxOffsetPx   = 10.f;
	static const FString AutoShakeText = LOCTEXT("AutoShake", "(Automatic)").ToString();

	const FSlateFontInfo FontAwesomeFont = FEditorStyle::Get().GetFontStyle("FontAwesome.10");
	const FSlateFontInfo SmallLayoutFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);
	const FLinearColor   DrawColor       = FEditorStyle::GetSlateColor("SelectionColor").GetColor(FWidgetStyle());

	const FString ShakeText = (ShakeClass.Get() != nullptr) ? ShakeClass.Get()->GetName() : AutoShakeText;

	TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	const  FMargin   BoxPadding     = FMargin(4.0f, 2.0f);
	const FVector2D  TextSize       = FontMeasureService->Measure(ShakeText, SmallLayoutFont);

	// Flip the text position if getting near the end of the view range
	const bool bDrawLeft = (Painter.SectionGeometry.Size.X - PixelPos) < (TextSize.X + 22.f) - BoxOffsetPx;
	const float BoxPositionX = FMath::Max(
			bDrawLeft ? PixelPos - TextSize.X - BoxOffsetPx : PixelPos + BoxOffsetPx,
			0.f);

	const FVector2D BoxOffset  = FVector2D(BoxPositionX, Painter.SectionGeometry.Size.Y*.5f - TextSize.Y*.5f);
	const FVector2D TextOffset = FVector2D(BoxPadding.Left, 0);

	// Draw the background box.
	FSlateDrawElement::MakeBox(
		Painter.DrawElements,
		LayerId + 1,
		Painter.SectionGeometry.ToPaintGeometry(BoxOffset, TextSize),
		FEditorStyle::GetBrush("WhiteBrush"),
		ESlateDrawEffect::None,
		FLinearColor::Black.CopyWithNewOpacity(0.5f)
	);

	// Draw shake name.
	FSlateDrawElement::MakeText(
		Painter.DrawElements,
		LayerId + 2,
		Painter.SectionGeometry.ToPaintGeometry(BoxOffset + TextOffset, TextSize),
		ShakeText,
		SmallLayoutFont,
		Painter.bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect,
		DrawColor
	);
}

int32 FCameraShakeSourceTriggerSection::OnPaintSection(FSequencerSectionPainter& Painter) const
{
	int32 LayerId = Painter.PaintSectionBackground();

	UMovieSceneCameraShakeSourceTriggerSection* TriggerSection = Cast<UMovieSceneCameraShakeSourceTriggerSection>(WeakSection.Get());
	if (!TriggerSection || !IsTrackSelected())
	{
		return LayerId;
	}

	const FTimeToPixel& TimeToPixelConverter = Painter.GetTimeConverter();
	const FMovieSceneCameraShakeSourceTriggerChannel& TriggerChannel = TriggerSection->GetChannel();
	const TArrayView<const FFrameNumber> Times = TriggerChannel.GetData().GetTimes();
	const TArrayView<const FMovieSceneCameraShakeSourceTrigger> Values = TriggerChannel.GetData().GetValues();
	const TRange<FFrameNumber> SectionRange = TriggerSection->GetRange();

	for (int32 KeyIndex = 0; KeyIndex < Times.Num(); ++KeyIndex)
	{
		const FFrameNumber Time = Times[KeyIndex];
		if (SectionRange.Contains(Time))
		{
			const FMovieSceneCameraShakeSourceTrigger& Value = Values[KeyIndex];
			const float PixelPos = TimeToPixelConverter.FrameToPixel(Time);
			PaintShakeName(Painter, LayerId, Value.ShakeClass, PixelPos);
		}
	}

	return LayerId + 3;
}

FCameraShakeSourceShakeTrackEditor::FCameraShakeSourceShakeTrackEditor(TSharedRef<ISequencer> InSequencer)
	: FMovieSceneTrackEditor(InSequencer) 
{ 
}

TSharedRef<ISequencerTrackEditor> FCameraShakeSourceShakeTrackEditor::CreateTrackEditor(TSharedRef<ISequencer> InSequencer)
{
	return MakeShareable(new FCameraShakeSourceShakeTrackEditor(InSequencer));
}

bool FCameraShakeSourceShakeTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const
{
	return (
		Type == UMovieSceneCameraShakeSourceShakeTrack::StaticClass() ||
		Type == UMovieSceneCameraShakeSourceTriggerTrack::StaticClass()
		);
}

TSharedRef<ISequencerSection> FCameraShakeSourceShakeTrackEditor::MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding)
{
	if (UMovieSceneCameraShakeSourceShakeSection* ShakeSection = Cast<UMovieSceneCameraShakeSourceShakeSection>(&SectionObject))
	{
		return MakeShareable(new FCameraShakeSourceShakeSection(GetSequencer(), *ShakeSection, ObjectBinding));
	}
	else if (UMovieSceneCameraShakeSourceTriggerSection* TriggerSection = Cast<UMovieSceneCameraShakeSourceTriggerSection>(&SectionObject))
	{
		return MakeShared<FCameraShakeSourceTriggerSection>(GetSequencer(), *TriggerSection);
	}

	check(false);
	return MakeShareable(new FSequencerSection(SectionObject));
}

UMovieSceneTrack* FCameraShakeSourceShakeTrackEditor::AddTrack(UMovieScene* FocusedMovieScene, const FGuid& ObjectHandle, TSubclassOf<UMovieSceneTrack> TrackClass, FName UniqueTypeName)
{
	UMovieSceneTrack* NewTrack = FMovieSceneTrackEditor::AddTrack(FocusedMovieScene, ObjectHandle, TrackClass, UniqueTypeName);

	if (UMovieSceneCameraShakeSourceTriggerTrack* ShakeTrack = Cast<UMovieSceneCameraShakeSourceTriggerTrack>(NewTrack))
	{
		// If it's a trigger track, auto-add an infinite section in which we can place our trigger keyframes.
		UMovieSceneSection* NewSection = ShakeTrack->CreateNewSection();
		NewSection->SetRange(TRange<FFrameNumber>::All());
		ShakeTrack->AddSection(*NewSection);
	}

	return NewTrack;
}

void FCameraShakeSourceShakeTrackEditor::BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass)
{
	if (const UCameraShakeSourceComponent* ShakeSourceComponent = AcquireCameraShakeSourceComponentFromGuid(ObjectBindings[0]))
	{
		MenuBuilder.AddSubMenu(
				LOCTEXT("AddShakeSourceShake", "Camera Shake"),
				LOCTEXT("AddShakeSourceShakeTooltip", "Adds a camera shake originating from the parent camera shake source."),
				FNewMenuDelegate::CreateSP(this, &FCameraShakeSourceShakeTrackEditor::AddCameraShakeTracksMenu, ObjectBindings));
	}
}

void FCameraShakeSourceShakeTrackEditor::AddCameraShakeTracksMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings)
{
	MenuBuilder.AddMenuEntry(
			LOCTEXT("AddShakeSourceShakeControlled", "Controlled"),
			LOCTEXT("AddShakeSourceShakeControlledTooltip", "Adds a track that lets you start and stop camera shakes originating from the parent camera shake source."),
			FSlateIcon(),
			FUIAction( 
				FExecuteAction::CreateSP( this, &FCameraShakeSourceShakeTrackEditor::AddCameraShakeSection, ObjectBindings)
				)
			);

	MenuBuilder.AddMenuEntry(
			LOCTEXT("AddShakeSourceShakeTrigger", "Trigger"),
			LOCTEXT("AddShakeSourceShakeTriggerTooltip", "Adds a track that lets you trigger camera shakes originating from the parent camera shake source."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &FCameraShakeSourceShakeTrackEditor::AddCameraShakeTriggerTrack, ObjectBindings)
				)
			);
}

void FCameraShakeSourceShakeTrackEditor::AddCameraShakeSection(TArray<FGuid> ObjectHandles)
{
	TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
	if (!SequencerPtr.IsValid() || !SequencerPtr->IsAllowedToChange())
	{
		return;
	}

	TArray<TWeakObjectPtr<UObject>> Objects;
	for (FGuid ObjectHandle : ObjectHandles)
	{
		for (TWeakObjectPtr<UObject> Object : SequencerPtr->FindObjectsInCurrentSequence(ObjectHandle))
		{
			Objects.Add(Object);
		}
	}

	auto OnAddShakeSourceShakeSection = [=](FFrameNumber Time) -> FKeyPropertyResult
	{
		return this->AddCameraShakeSectionKeyInternal(Time, Objects, true);
	};

	const FScopedTransaction Transaction(LOCTEXT("AddCameraShakeSourceShake_Transaction", "Add Camera Shake"));

	AnimatablePropertyChanged(FOnKeyProperty::CreateLambda(OnAddShakeSourceShakeSection));
}

TSharedPtr<SWidget> FCameraShakeSourceShakeTrackEditor::BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params)
{
	if (UMovieSceneCameraShakeSourceShakeTrack* ShakeTrack = Cast<UMovieSceneCameraShakeSourceShakeTrack>(Track))
	{
		return SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				FSequencerUtilities::MakeAddButton(
						LOCTEXT("AddShakeSourceShakeSection", "Camera Shake"),
						FOnGetContent::CreateSP(this, &FCameraShakeSourceShakeTrackEditor::BuildCameraShakeSubMenu, ObjectBinding),
						Params.NodeIsHovered, GetSequencer())
			];
	}
	else
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				FSequencerUtilities::MakeAddButton(
						LOCTEXT("AddSection", "Section"),
						FOnGetContent::CreateSP(this, &FCameraShakeSourceShakeTrackEditor::BuildCameraShakeTracksMenu, ObjectBinding),
						Params.NodeIsHovered, 
						GetSequencer())
			];
	}
}

FKeyPropertyResult FCameraShakeSourceShakeTrackEditor::AddCameraShakeSectionKeyInternal(FFrameNumber KeyTime, const TArray<TWeakObjectPtr<UObject>> Objects, bool bSelect)
{
	return AddCameraShakeSectionKeyInternal(KeyTime, Objects, TSubclassOf<UCameraShakeBase>(), bSelect);
}

FKeyPropertyResult FCameraShakeSourceShakeTrackEditor::AddCameraShakeSectionKeyInternal(FFrameNumber KeyTime, const TArray<TWeakObjectPtr<UObject>> Objects, TSubclassOf<UCameraShakeBase> CameraShake, bool bSelect)
{
	FKeyPropertyResult KeyPropertyResult;

	TArray<UMovieSceneSection*> SectionsToSelect;

	for (int32 ObjectIndex = 0; ObjectIndex < Objects.Num(); ++ObjectIndex)
	{
		UObject* Object = Objects[ObjectIndex].Get();

		if (AActor* Actor = Cast<AActor>(Object))
		{
			UCameraShakeSourceComponent* Component = Actor->FindComponentByClass<UCameraShakeSourceComponent>();
			if (ensure(Component != nullptr))
			{
				Object = Component;
			}
		}

		const bool bIsAutomaticShake = (CameraShake.Get() == nullptr);
		if (bIsAutomaticShake)
		{
			if (UCameraShakeSourceComponent* ShakeSourceComponent = Cast<UCameraShakeSourceComponent>(Object))
			{
				CameraShake = ShakeSourceComponent->CameraShake;
			}
		}

		FFindOrCreateHandleResult HandleResult = FindOrCreateHandleToObject(Object);
		FGuid ObjectHandle = HandleResult.Handle;
		KeyPropertyResult.bHandleCreated |= HandleResult.bWasCreated;

		if (ObjectHandle.IsValid())
		{
			FFindOrCreateTrackResult TrackResult = FindOrCreateTrackForObject(ObjectHandle, UMovieSceneCameraShakeSourceShakeTrack::StaticClass());
			UMovieSceneTrack* Track = TrackResult.Track;
			KeyPropertyResult.bTrackCreated |= TrackResult.bWasCreated;

			if (ensure(Track))
			{
				UMovieSceneSection* NewSection = Cast<UMovieSceneCameraShakeSourceShakeTrack>(Track)->AddNewCameraShake(KeyTime, CameraShake, bIsAutomaticShake);
				KeyPropertyResult.bTrackModified = true;
				KeyPropertyResult.SectionsCreated.Add(NewSection);
				SectionsToSelect.Add(NewSection);
			}
		}
	}

	if (bSelect)
	{
		const TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
		SequencerPtr->EmptySelection();
		for (UMovieSceneSection* SectionToSelect : SectionsToSelect)
		{
			SequencerPtr->SelectSection(SectionToSelect);
		}
		SequencerPtr->ThrobSectionSelection();
	}

	return KeyPropertyResult;
}

TSharedRef<SWidget> FCameraShakeSourceShakeTrackEditor::BuildCameraShakeSubMenu(FGuid ObjectBinding)
{
	FMenuBuilder MenuBuilder(true, nullptr);

	TArray<FGuid> ObjectBindings;
	ObjectBindings.Add(ObjectBinding);

	AddCameraShakeSubMenu(MenuBuilder, ObjectBindings);

	return MenuBuilder.MakeWidget();
}

void FCameraShakeSourceShakeTrackEditor::AddCameraShakeSubMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings)
{
	MenuBuilder.AddMenuEntry(
			LOCTEXT("AddAutoShake", "Automatic Shake"),
			LOCTEXT("AddAutoShakeTooltip", "Adds a section that plays the camera shake already configured on the shake source component."),
			FSlateIcon(),
			FUIAction( 
				FExecuteAction::CreateSP(this, &FCameraShakeSourceShakeTrackEditor::OnAutoCameraShakeSelected, ObjectBindings)
			));

	MenuBuilder.AddSubMenu(
			LOCTEXT("AddOtherShake", "Other Shake"),
			LOCTEXT("AddOtherShakeTooltip", "Adds a section that plays a specific camera shake originating from the shake source component."),
			FNewMenuDelegate::CreateSP(this, &FCameraShakeSourceShakeTrackEditor::AddOtherCameraShakeBrowserSubMenu, ObjectBindings));
}

TSharedRef<SWidget> FCameraShakeSourceShakeTrackEditor::BuildCameraShakeTracksMenu(FGuid ObjectBinding)
{
	FMenuBuilder MenuBuilder(true, nullptr);

	TArray<FGuid> ObjectBindings;
	ObjectBindings.Add(ObjectBinding);

	AddCameraShakeTracksMenu(MenuBuilder, ObjectBindings);

	return MenuBuilder.MakeWidget();
}

void FCameraShakeSourceShakeTrackEditor::AddOtherCameraShakeBrowserSubMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings)
{
	FAssetPickerConfig AssetPickerConfig;
	{
		AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &FCameraShakeSourceShakeTrackEditor::OnCameraShakeAssetSelected, ObjectBindings);
		AssetPickerConfig.OnAssetEnterPressed = FOnAssetEnterPressed::CreateSP(this, &FCameraShakeSourceShakeTrackEditor::OnCameraShakeAssetEnterPressed, ObjectBindings);
		AssetPickerConfig.OnShouldFilterAsset = FOnShouldFilterAsset::CreateSP(this, &FCameraShakeSourceShakeTrackEditor::OnShouldFilterCameraShake);
		AssetPickerConfig.bAllowNullSelection = false;
		AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
		AssetPickerConfig.Filter.ClassNames.Add(UBlueprint::StaticClass()->GetFName());

		IAssetRegistry & AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FName> ClassNames;
		TSet<FName> DerivedClassNames;
		ClassNames.Add(UMatineeCameraShake::StaticClass()->GetFName());
		AssetRegistry.GetDerivedClassNames(ClassNames, TSet<FName>(), DerivedClassNames);
						
		AssetPickerConfig.OnShouldFilterAsset = FOnShouldFilterAsset::CreateLambda([DerivedClassNames](const FAssetData& AssetData)
		{
			const FString ParentClassFromData = AssetData.GetTagValueRef<FString>(FBlueprintTags::ParentClassPath);
			if (!ParentClassFromData.IsEmpty())
			{
				const FString ClassObjectPath = FPackageName::ExportTextPathToObjectPath(ParentClassFromData);
				const FName ClassName = FName(*FPackageName::ObjectPathToObjectName(ClassObjectPath));

				if (DerivedClassNames.Contains(ClassName))
				{
					return false;
				}
			}
			return true;
		});
	}

	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	TSharedPtr<SBox> MenuEntry = SNew(SBox)
		.WidthOverride(300.0f)
		.HeightOverride(400.f)
		[
			ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
		];

	MenuBuilder.AddWidget(MenuEntry.ToSharedRef(), FText::GetEmpty(), true);
}

void FCameraShakeSourceShakeTrackEditor::OnCameraShakeAssetSelected(const FAssetData& AssetData, TArray<FGuid> ObjectBindings)
{
	FSlateApplication::Get().DismissAllMenus();

	TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
	UBlueprint* SelectedObject = Cast<UBlueprint>(AssetData.GetAsset());

	if (SelectedObject && SelectedObject->GeneratedClass && SelectedObject->GeneratedClass->IsChildOf(UCameraShakeBase::StaticClass()))
	{
		TSubclassOf<UCameraShakeBase> CameraShakeClass = *(SelectedObject->GeneratedClass);

		TArray<TWeakObjectPtr<>> OutObjects;
		for (FGuid ObjectBinding : ObjectBindings)
		{
			for (TWeakObjectPtr<> Object : SequencerPtr->FindObjectsInCurrentSequence(ObjectBinding))
			{
				OutObjects.Add(Object);
			}
		}
		
		const FScopedTransaction Transaction(LOCTEXT("AddCameraShakeSourceShake_Transaction", "Add Camera Shake"));

		AnimatablePropertyChanged(FOnKeyProperty::CreateSP(this, &FCameraShakeSourceShakeTrackEditor::AddCameraShakeSectionKeyInternal, OutObjects, CameraShakeClass, true));
	}
}

void FCameraShakeSourceShakeTrackEditor::OnCameraShakeAssetEnterPressed(const TArray<FAssetData>& AssetData, TArray<FGuid> ObjectBindings)
{
	if (AssetData.Num() > 0)
	{
		OnCameraShakeAssetSelected(AssetData[0].GetAsset(), ObjectBindings);
	}
}

void FCameraShakeSourceShakeTrackEditor::OnAutoCameraShakeSelected(TArray<FGuid> ObjectBindings)
{
	TArray<TWeakObjectPtr<>> OutObjects;
	TSharedPtr<ISequencer> SequencerPtr = GetSequencer();

	for (FGuid ObjectBinding : ObjectBindings)
	{
		for (TWeakObjectPtr<> Object : SequencerPtr->FindObjectsInCurrentSequence(ObjectBinding))
		{
			OutObjects.Add(Object);
		}
	}

	const FScopedTransaction Transaction(LOCTEXT("AddCameraShakeSourceShake_Transaction", "Add Camera Shake"));

	AnimatablePropertyChanged(FOnKeyProperty::CreateSP(this, &FCameraShakeSourceShakeTrackEditor::AddCameraShakeSectionKeyInternal, OutObjects, true));
}

bool FCameraShakeSourceShakeTrackEditor::OnShouldFilterCameraShake(const FAssetData& AssetData)
{
	const UBlueprint* SelectedObject = Cast<UBlueprint>(AssetData.GetAsset());
	if (SelectedObject && SelectedObject->GeneratedClass && SelectedObject->GeneratedClass->IsChildOf(UCameraShakeBase::StaticClass()))
	{
		TSubclassOf<UCameraShakeBase> CameraShakeClass = *(SelectedObject->GeneratedClass);
		if (const UCameraShakeBase* CameraShakeCDO = Cast<UCameraShakeBase>(CameraShakeClass->ClassDefaultObject))
		{
			return CameraShakeCDO->bSingleInstance;
		}
	}
	return true;
}

FKeyPropertyResult FCameraShakeSourceShakeTrackEditor::AddCameraShakeTriggerTrackInternal(FFrameNumber Time, const TArray<TWeakObjectPtr<UObject>> Objects, TSubclassOf<UCameraShakeBase> CameraShake)
{
	FKeyPropertyResult KeyPropertyResult;

	for (int32 ObjectIndex = 0; ObjectIndex < Objects.Num(); ++ObjectIndex)
	{
		UObject* Object = Objects[ObjectIndex].Get();

		if (AActor* Actor = Cast<AActor>(Object))
		{
			UCameraShakeSourceComponent* Component = Actor->FindComponentByClass<UCameraShakeSourceComponent>();
			if (ensure(Component != nullptr))
			{
				Object = Component;
			}
		}

		const bool bIsAutomaticShake = (CameraShake.Get() == nullptr);
		if (bIsAutomaticShake)
		{
			if (UCameraShakeSourceComponent* ShakeSourceComponent = Cast<UCameraShakeSourceComponent>(Object))
			{
				CameraShake = ShakeSourceComponent->CameraShake;
			}
		}

		FFindOrCreateHandleResult HandleResult = FindOrCreateHandleToObject(Object);
		FGuid ObjectHandle = HandleResult.Handle;
		KeyPropertyResult.bHandleCreated |= HandleResult.bWasCreated;

		if (ObjectHandle.IsValid())
		{
			FFindOrCreateTrackResult TrackResult = FindOrCreateTrackForObject(ObjectHandle, UMovieSceneCameraShakeSourceTriggerTrack::StaticClass());
			UMovieSceneTrack* Track = TrackResult.Track;
			KeyPropertyResult.bTrackCreated |= TrackResult.bWasCreated;

			if (ensure(Track))
			{
				TArray<UMovieSceneSection*> AllSections = Track->GetAllSections();
				if (ensure(AllSections.Num() > 0))
				{
					UMovieSceneCameraShakeSourceTriggerSection* FirstSection = Cast<UMovieSceneCameraShakeSourceTriggerSection>(AllSections[0]);
					// TODO: add trigger key at given time.
					GetSequencer()->EmptySelection();
					GetSequencer()->SelectSection(FirstSection);
					GetSequencer()->ThrobSectionSelection();
				}
			}
		}
	}

	return KeyPropertyResult;
}

void FCameraShakeSourceShakeTrackEditor::AddCameraShakeTriggerTrack(const TArray<FGuid> ObjectBindings)
{
	TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
	if (!SequencerPtr.IsValid() || !SequencerPtr->IsAllowedToChange())
	{
		return;
	}

	TArray<TWeakObjectPtr<UObject>> Objects;
	for (FGuid ObjectBinding : ObjectBindings)
	{
		for (TWeakObjectPtr<UObject> Object : SequencerPtr->FindObjectsInCurrentSequence(ObjectBinding))
		{
			Objects.Add(Object);
		}
	}

	auto OnAddShakeSourceShakeSection = [=](FFrameNumber Time) -> FKeyPropertyResult
	{
		return this->AddCameraShakeTriggerTrackInternal(Time, Objects, nullptr);
	};

	const FScopedTransaction Transaction(LOCTEXT("AddCameraShakeSourceShake_Transaction", "Add Camera Shake"));

	AnimatablePropertyChanged(FOnKeyProperty::CreateLambda(OnAddShakeSourceShakeSection));
}

UCameraShakeSourceComponent* FCameraShakeSourceShakeTrackEditor::AcquireCameraShakeSourceComponentFromGuid(const FGuid& Guid)
{
	TArray<UCameraShakeSourceComponent*> ShakeSourceComponents;

	for (TWeakObjectPtr<> WeakObject : GetSequencer()->FindObjectsInCurrentSequence(Guid))
	{
		if (UObject* Obj = WeakObject.Get())
		{
			if (AActor* Actor = Cast<AActor>(Obj))
			{
				TArray<UCameraShakeSourceComponent*> CurShakeSourceComponents;
				Actor->GetComponents<UCameraShakeSourceComponent>(CurShakeSourceComponents);
				ShakeSourceComponents.Append(CurShakeSourceComponents);
			}
			else if (UCameraShakeSourceComponent* ShakeSourceComponent = Cast<UCameraShakeSourceComponent>(Obj))
			{
				ShakeSourceComponents.Add(ShakeSourceComponent);
			}
		}
	}

	UCameraShakeSourceComponent** ActiveComponent = ShakeSourceComponents.FindByPredicate([](UCameraShakeSourceComponent* Component)
			{
				return Component->IsActive();
			});
	if (ActiveComponent != nullptr)
	{
		return *ActiveComponent;
	}

	if (ShakeSourceComponents.Num() > 0)
	{
		return ShakeSourceComponents[0];
	}

	return nullptr;
}

#undef LOCTEXT_NAMESPACE

