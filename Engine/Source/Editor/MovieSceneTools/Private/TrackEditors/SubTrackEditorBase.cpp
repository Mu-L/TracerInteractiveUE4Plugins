// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrackEditors/SubTrackEditorBase.h"
#include "Tracks/MovieSceneCinematicShotTrack.h"
#include "Tracks/MovieSceneSubTrack.h"

#define LOCTEXT_NAMESPACE "FSubTrackEditorBase"

FSubSectionPainterResult FSubSectionPainterUtil::PaintSection(TSharedPtr<const ISequencer> Sequencer, const UMovieSceneSubSection& SectionObject, FSequencerSectionPainter& InPainter, FSubSectionPainterParams Params)
{
    const TRange<FFrameNumber> SectionRange = SectionObject.GetRange();
    if (SectionRange.GetLowerBound().IsOpen() || SectionRange.GetUpperBound().IsOpen())
    {
        return FSSPR_InvalidSection;
    }

    const int32 SectionSize = MovieScene::DiscreteSize(SectionRange);
    if (SectionSize <= 0)
    {
        return FSSPR_InvalidSection;
    }

    UMovieSceneSequence* InnerSequence = SectionObject.GetSequence();
    if (InnerSequence == nullptr)
    {
        return FSSPR_NoInnerSequence;
    }

    const ESlateDrawEffect DrawEffects = InPainter.bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
    if (SectionObject.Parameters.bCanLoop)
    {
        DoPaintLoopingSection(SectionObject, *InnerSequence, InPainter, DrawEffects);
    }
    else
    {
        DoPaintNonLoopingSection(SectionObject, *InnerSequence, InPainter, DrawEffects);
    }

    UMovieScene* MovieScene = InnerSequence->GetMovieScene();
    const int32 NumTracks = MovieScene->GetPossessableCount() + MovieScene->GetSpawnableCount() + MovieScene->GetMasterTracks().Num();

    FVector2D TopLeft = InPainter.SectionGeometry.AbsoluteToLocal(InPainter.SectionClippingRect.GetTopLeft()) + FVector2D(1.f, -1.f);

    FSlateFontInfo FontInfo = FEditorStyle::GetFontStyle("NormalFont");

    TSharedRef<FSlateFontCache> FontCache = FSlateApplication::Get().GetRenderer()->GetFontCache();

    auto GetFontHeight = [&]
    {
        return FontCache->GetMaxCharacterHeight(FontInfo, 1.f) + FontCache->GetBaseline(FontInfo, 1.f);
    };
    while (GetFontHeight() > InPainter.SectionGeometry.Size.Y && FontInfo.Size > 11)
    {
        FontInfo.Size = FMath::Max(FMath::FloorToInt(FontInfo.Size - 6.f), 11);
    }

    uint32 LayerId = InPainter.LayerId;
    FMargin ContentPadding = Params.ContentPadding;

    if (Params.bShowTrackNum)
    {
        FSlateDrawElement::MakeText(
                InPainter.DrawElements,
                ++LayerId,
                InPainter.SectionGeometry.MakeChild(
                    FVector2D(InPainter.SectionGeometry.Size.X, GetFontHeight()),
                    FSlateLayoutTransform(TopLeft + FVector2D(ContentPadding.Left, ContentPadding.Top) + FVector2D(11.f, GetFontHeight()*2.f))
                    ).ToPaintGeometry(),
                FText::Format(LOCTEXT("NumTracksFormat", "{0} track(s)"), FText::AsNumber(NumTracks)),
                FontInfo,
                DrawEffects,
                FColor(200, 200, 200)
                );
    }

    if (Params.bDrawFrameNumberHintWhenSelected && InPainter.bIsSelected && Sequencer.IsValid())
    {
        FFrameTime CurrentTime = Sequencer->GetLocalTime().Time;
        if (SectionRange.Contains(CurrentTime.FrameNumber))
        {
            UMovieScene* SubSequenceMovieScene = SectionObject.GetSequence()->GetMovieScene();
            const FFrameRate DisplayRate = SubSequenceMovieScene->GetDisplayRate();
            const FFrameRate TickResolution = SubSequenceMovieScene->GetTickResolution();
            const FFrameNumber CurrentFrameNumber = ConvertFrameTime(CurrentTime * SectionObject.OuterToInnerTransform(), TickResolution, DisplayRate).FloorToFrame();

            DrawFrameNumberHint(InPainter, CurrentTime, CurrentFrameNumber.Value);
        }
    }

    InPainter.LayerId = LayerId;

    return FSSPR_Success;
}

void FSubSectionPainterUtil::DoPaintNonLoopingSection(const UMovieSceneSubSection& SectionObject, const UMovieSceneSequence& InnerSequence, FSequencerSectionPainter& InPainter, ESlateDrawEffect DrawEffects)
{
    const FFrameNumber SectionStartFrame = SectionObject.GetInclusiveStartFrame();

    const TRange<FFrameNumber> SectionRange = SectionObject.GetRange();
    const int32 SectionSize = MovieScene::DiscreteSize(SectionRange);
    const float PixelsPerFrame = InPainter.SectionGeometry.Size.X / float(SectionSize);

    UMovieScene* MovieScene = InnerSequence.GetMovieScene();
    TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();

    // We're in the non-looping case so we know we have a purely linear transform.
    const FMovieSceneSequenceTransform InnerToOuterTransform = SectionObject.OuterToInnerTransform().InverseLinearOnly();
    const FFrameNumber PlaybackStart = (MovieScene::DiscreteInclusiveLower(PlaybackRange) * InnerToOuterTransform).FloorToFrame();
    if (SectionRange.Contains(PlaybackStart))
    {
        const int32 StartOffset = (PlaybackStart - SectionStartFrame).Value;
        // add dark tint for left out-of-bounds
        FSlateDrawElement::MakeBox(
                InPainter.DrawElements,
                InPainter.LayerId++,
                InPainter.SectionGeometry.ToPaintGeometry(
                    FVector2D(0.0f, 0.f),
                    FVector2D(StartOffset * PixelsPerFrame, InPainter.SectionGeometry.Size.Y)
                    ),
                FEditorStyle::GetBrush("WhiteBrush"),
                DrawEffects,
                FLinearColor::Black.CopyWithNewOpacity(0.5f)
                );

        // add green line for playback start
        FSlateDrawElement::MakeBox(
                InPainter.DrawElements,
                InPainter.LayerId++,
                InPainter.SectionGeometry.ToPaintGeometry(
                    FVector2D(StartOffset * PixelsPerFrame, 0.f),
                    FVector2D(1.0f, InPainter.SectionGeometry.Size.Y)
                    ),
                FEditorStyle::GetBrush("WhiteBrush"),
                DrawEffects,
                FColor(32, 128, 32)	// 120, 75, 50 (HSV)
                );
    }

    const FFrameNumber PlaybackEnd = (MovieScene::DiscreteExclusiveUpper(PlaybackRange) * InnerToOuterTransform) .FloorToFrame();
    if (SectionRange.Contains(PlaybackEnd))
    {
        // add dark tint for right out-of-bounds
        const int32 EndOffset = (PlaybackEnd - SectionStartFrame).Value;
        FSlateDrawElement::MakeBox(
                InPainter.DrawElements,
                InPainter.LayerId++,
                InPainter.SectionGeometry.ToPaintGeometry(
                    FVector2D(EndOffset * PixelsPerFrame, 0.f),
                    FVector2D((SectionSize - EndOffset) * PixelsPerFrame, InPainter.SectionGeometry.Size.Y)
                    ),
                FEditorStyle::GetBrush("WhiteBrush"),
                DrawEffects,
                FLinearColor::Black.CopyWithNewOpacity(0.5f)
                );


        // add red line for playback end
        FSlateDrawElement::MakeBox(
                InPainter.DrawElements,
                InPainter.LayerId++,
                InPainter.SectionGeometry.ToPaintGeometry(
                    FVector2D(EndOffset * PixelsPerFrame, 0.f),
                    FVector2D(1.0f, InPainter.SectionGeometry.Size.Y)
                    ),
                FEditorStyle::GetBrush("WhiteBrush"),
                DrawEffects,
                FColor(128, 32, 32)	// 0, 75, 50 (HSV)
                );
    }
}

void FSubSectionPainterUtil::DoPaintLoopingSection(const UMovieSceneSubSection& SectionObject, const UMovieSceneSequence& InnerSequence, FSequencerSectionPainter& InPainter, ESlateDrawEffect DrawEffects)
{
    const FFrameNumber SectionStartFrame = SectionObject.GetInclusiveStartFrame();
    const FFrameNumber SectionEndFrame   = SectionObject.GetExclusiveEndFrame();

    const TRange<FFrameNumber> SectionRange = SectionObject.GetRange();
    const int32 SectionSize = MovieScene::DiscreteSize(SectionRange);
    const float PixelsPerFrame = InPainter.SectionGeometry.Size.X / float(SectionSize);

    const float InvTimeScale = FMath::IsNearlyZero(SectionObject.Parameters.TimeScale) ? 1.0f : 1.0f / SectionObject.Parameters.TimeScale;

    const UMovieScene* MovieScene = InnerSequence.GetMovieScene();
    const TRange<FFrameNumber> InnerPlaybackRange = UMovieSceneSubSection::GetValidatedInnerPlaybackRange(SectionObject.Parameters, *MovieScene);

    const FFrameNumber InnerSubSeqLength = MovieScene::DiscreteSize(InnerPlaybackRange);
    const FFrameNumber InnerSubSeqFirstLoopLength = InnerSubSeqLength - SectionObject.Parameters.FirstLoopStartFrameOffset;

    const FFrameRate OuterFrameRate   = SectionObject.GetTypedOuter<UMovieScene>()->GetTickResolution();
    const FFrameRate InnerFrameRate   = MovieScene->GetTickResolution();
    const FFrameNumber OuterSubSeqLength = (ConvertFrameTime(InnerSubSeqLength, InnerFrameRate, OuterFrameRate) * InvTimeScale).FrameNumber;
    const FFrameNumber OuterSubSeqFirstLoopLength = (ConvertFrameTime(InnerSubSeqFirstLoopLength, InnerFrameRate, OuterFrameRate) * InvTimeScale).FrameNumber;

    FFrameNumber CurOffsetFrame = FMath::Max(OuterSubSeqFirstLoopLength, FFrameNumber(0));

    // Draw separators where the sub-sequence is looping. To be consistent with the non-looping case, we draw a red and green
    // separator back to back.
    uint32 MaxLoopBoundaries = 100;
    while (CurOffsetFrame < SectionEndFrame)
    {
        const int32 CurOffset = CurOffsetFrame.Value;
        FSlateDrawElement::MakeBox(
            InPainter.DrawElements,
            InPainter.LayerId++,
            InPainter.SectionGeometry.ToPaintGeometry(
                FVector2D(CurOffset * PixelsPerFrame, 0.f),
                FVector2D(1.0f, InPainter.SectionGeometry.Size.Y)
            ),
            FEditorStyle::GetBrush("WhiteBrush"),
            DrawEffects,
            FColor(32, 128, 32)	// 120, 75, 50 (HSV)
        );
        if (CurOffset > 0)
        {
            FSlateDrawElement::MakeBox(
                InPainter.DrawElements,
                InPainter.LayerId++,
                InPainter.SectionGeometry.ToPaintGeometry(
                    FVector2D(CurOffset * PixelsPerFrame - 1.f, 0.f),
                    FVector2D(1.0f, InPainter.SectionGeometry.Size.Y)
                ),
                FEditorStyle::GetBrush("WhiteBrush"),
                DrawEffects,
                FColor(128, 32, 32)	// 0, 75, 50 (HSV)
            );
        }

        CurOffsetFrame += OuterSubSeqLength;

        if ((--MaxLoopBoundaries) == 0)
        {
            break;
        }
    }
}

FSubSectionEditorUtil::FSubSectionEditorUtil(UMovieSceneSubSection& InSection)
    : SectionObject(InSection)
    , InitialStartOffsetDuringResize(0)
    , InitialStartTimeDuringResize(0)
{
}

void FSubSectionEditorUtil::BeginResizeSection()
{
    InitialStartOffsetDuringResize = SectionObject.Parameters.bCanLoop ? SectionObject.Parameters.FirstLoopStartFrameOffset : SectionObject.Parameters.StartFrameOffset;
    InitialStartTimeDuringResize = SectionObject.HasStartFrame() ? SectionObject.GetInclusiveStartFrame() : 0;
}

FFrameNumber FSubSectionEditorUtil::ResizeSection(ESequencerSectionResizeMode ResizeMode, FFrameNumber ResizeTime)
{
    UMovieSceneSequence* InnerSequence = SectionObject.GetSequence();
    UMovieScene* InnerMovieScene = InnerSequence ? InnerSequence->GetMovieScene() : nullptr;

    if (ResizeMode == SSRM_LeadingEdge && InnerMovieScene != nullptr)
    {
        // Adjust the start offset when resizing from the beginning
        FMovieSceneSectionParameters& SectionParameters = SectionObject.Parameters;

        const FFrameRate    OuterFrameRate   = SectionObject.GetTypedOuter<UMovieScene>()->GetTickResolution();
        const FFrameRate    InnerFrameRate   = InnerSequence->GetMovieScene()->GetTickResolution();
        const FFrameNumber  ResizeDifference = ResizeTime - InitialStartTimeDuringResize;

        const FFrameNumber InnerResizeDifference = (ConvertFrameTime(ResizeDifference, OuterFrameRate, InnerFrameRate) * SectionParameters.TimeScale).FrameNumber;
        FFrameNumber NewStartOffset = InitialStartOffsetDuringResize + InnerResizeDifference;

        const int32 InnerPlaybackLength = MovieScene::DiscreteSize(InnerMovieScene->GetPlaybackRange());
        const float InvTimeScale = FMath::IsNearlyZero(SectionParameters.TimeScale) ? 1.0f : 1.0f / SectionParameters.TimeScale;
        const FFrameNumber InnerLoopLength = InnerPlaybackLength - SectionParameters.StartFrameOffset - SectionParameters.EndFrameOffset;

        const bool bCanLoop = SectionParameters.bCanLoop;
        if (!bCanLoop)
        {
            if (NewStartOffset < 0)
            {
                // Ensure start offset is not less than 0, clamp resize time.
                const FFrameTime OuterFrameTimeOver = ConvertFrameTime(FFrameTime::FromDecimal(NewStartOffset.Value / SectionParameters.TimeScale), InnerFrameRate, OuterFrameRate);
                ResizeTime = ResizeTime - OuterFrameTimeOver.GetFrame(); 
                NewStartOffset = 0;
            }
            SectionParameters.StartFrameOffset = NewStartOffset;
        }
        else
        {
			// If the start offset exceeds the length of one loop, trim it back.
			NewStartOffset = NewStartOffset % InnerLoopLength;
            if (NewStartOffset < 0)
            {
                // Move the first loop offset forward so that it's positive into the new first loop
                // that we just "revealed" by pulling the left edge back.
                NewStartOffset += InnerLoopLength;
            }
            SectionParameters.FirstLoopStartFrameOffset = NewStartOffset;
        }
    }

    return ResizeTime;
}

void FSubSectionEditorUtil::BeginSlipSection()
{
    // Cache the same values as when resizing.
    BeginResizeSection();
}

FFrameNumber FSubSectionEditorUtil::SlipSection(FFrameNumber SlipTime)
{
    UMovieSceneSequence* InnerSequence = SectionObject.GetSequence();
    UMovieScene* InnerMovieScene = InnerSequence ? InnerSequence->GetMovieScene() : nullptr;

    if (InnerMovieScene != nullptr)
    {
        FMovieSceneSectionParameters& SectionParameters = SectionObject.Parameters;

        const FFrameRate    OuterFrameRate   = SectionObject.GetTypedOuter<UMovieScene>()->GetTickResolution();
        const FFrameRate    InnerFrameRate   = InnerSequence->GetMovieScene()->GetTickResolution();
        const FFrameNumber  ResizeDifference = SlipTime - InitialStartTimeDuringResize;

        const FFrameNumber InnerResizeDifference = (ConvertFrameTime(ResizeDifference, OuterFrameRate, InnerFrameRate) * SectionParameters.TimeScale).FrameNumber;
        FFrameNumber NewStartOffset = InitialStartOffsetDuringResize + InnerResizeDifference;

        const int32 InnerPlaybackLength = MovieScene::DiscreteSize(InnerMovieScene->GetPlaybackRange());
        const float InvTimeScale = FMath::IsNearlyZero(SectionParameters.TimeScale) ? 1.0f : 1.0f / SectionParameters.TimeScale;
        const FFrameNumber InnerLoopLength = InnerPlaybackLength - SectionParameters.StartFrameOffset - SectionParameters.EndFrameOffset;

        const bool bCanLoop = SectionParameters.bCanLoop;
        if (!bCanLoop)
        {
            // Ensure start offset is not less than 0
            SectionParameters.StartFrameOffset = FFrameNumber(FMath::Max(NewStartOffset.Value, 0));
        }
        else
        {
			// If the start offset exceeds the length of one loop, trim it back.
            NewStartOffset = NewStartOffset % InnerLoopLength;
            if (NewStartOffset < 0)
            {
                // Move the first loop offset forward so that it's positive into the new first loop
                // that we just "revealed" by slipping too much to the side.
                NewStartOffset += InnerLoopLength;
            }
            SectionParameters.FirstLoopStartFrameOffset = NewStartOffset;
        }
    }

    return SlipTime;
}

bool FSubTrackEditorUtil::CanAddSubSequence(const UMovieSceneSequence* CurrentSequence, const UMovieSceneSequence& SubSequence)
{
	// Prevent adding ourselves and ensure we have a valid movie scene.
	if ((CurrentSequence == nullptr) || (CurrentSequence == &SubSequence) || (CurrentSequence->GetMovieScene() == nullptr))
	{
		return false;
	}

	// ensure that the other sequence has a valid movie scene
	UMovieScene* SequenceMovieScene = SubSequence.GetMovieScene();

	if (SequenceMovieScene == nullptr)
	{
		return false;
	}

	// make sure we are not contained in the other sequence (circular dependency)
	// @todo sequencer: this check is not sufficient (does not prevent circular dependencies of 2+ levels)
	UMovieSceneSubTrack* SequenceSubTrack = SequenceMovieScene->FindMasterTrack<UMovieSceneSubTrack>();
	if (SequenceSubTrack && SequenceSubTrack->ContainsSequence(*CurrentSequence, true))
	{
		return false;
	}

	UMovieSceneCinematicShotTrack* SequenceCinematicTrack = SequenceMovieScene->FindMasterTrack<UMovieSceneCinematicShotTrack>();
	if (SequenceCinematicTrack && SequenceCinematicTrack->ContainsSequence(*CurrentSequence, true))
	{
		return false;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
