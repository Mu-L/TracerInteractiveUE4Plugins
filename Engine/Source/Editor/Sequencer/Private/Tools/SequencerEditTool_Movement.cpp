// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/SequencerEditTool_Movement.h"
#include "Editor.h"
#include "Fonts/FontMeasure.h"
#include "Styling/CoreStyle.h"
#include "EditorStyleSet.h"
#include "SequencerCommonHelpers.h"
#include "SSequencer.h"
#include "ISequencerHotspot.h"
#include "SequencerHotspots.h"
#include "VirtualTrackArea.h"
#include "SequencerSettings.h"
#include "Tools/EditToolDragOperations.h"
#include "IKeyArea.h"
#include "Widgets/Layout/SBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/Timecode.h"
#include "MovieSceneTimeHelpers.h"

const FName FSequencerEditTool_Movement::Identifier = "Movement";


FSequencerEditTool_Movement::FSequencerEditTool_Movement(FSequencer& InSequencer)
	: FSequencerEditTool(InSequencer)
{ }


FReply FSequencerEditTool_Movement::OnMouseButtonDown(SWidget& OwnerWidget, const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	TSharedRef<SSequencer> SequencerWidget = StaticCastSharedRef<SSequencer>(Sequencer.GetSequencerWidget());

	TSharedPtr<ISequencerHotspot> Hotspot = Sequencer.GetHotspot();

	DelayedDrag.Reset();

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton || MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
	{
		const FVirtualTrackArea VirtualTrackArea = SequencerWidget->GetVirtualTrackArea();

		DelayedDrag = FDelayedDrag_Hotspot(VirtualTrackArea.CachedTrackAreaGeometry().AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()), MouseEvent.GetEffectingButton(), Hotspot);

 		if (Sequencer.GetSequencerSettings()->GetSnapPlayTimeToPressedKey() || (MouseEvent.IsShiftDown() && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton) )
		{
			if (DelayedDrag->Hotspot.IsValid())
			{
				if (DelayedDrag->Hotspot->GetType() == ESequencerHotspot::Key)
				{
					TOptional<FFrameNumber> Time = StaticCastSharedPtr<FKeyHotspot>(DelayedDrag->Hotspot)->GetTime();
					if (Time.IsSet())
					{
						Sequencer.SetLocalTime(Time.GetValue());
					}
				}
			}
		}

		return FReply::Handled().PreventThrottling();
	}
	return FReply::Unhandled();
}


FReply FSequencerEditTool_Movement::OnMouseMove(SWidget& OwnerWidget, const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (DelayedDrag.IsSet())
	{
		TSharedRef<SSequencer> SequencerWidget = StaticCastSharedRef<SSequencer>(Sequencer.GetSequencerWidget());
		const FVirtualTrackArea VirtualTrackArea = SequencerWidget->GetVirtualTrackArea();

		FReply Reply = FReply::Handled();

		if (DelayedDrag->IsDragging())
		{
			// If we're already dragging, just update the drag op if it exists
			if (DragOperation.IsValid())
			{
				DragPosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

				if (Sequencer.GetSequencerSettings()->GetIsSnapEnabled() && Sequencer.GetSequencerSettings()->GetSnapKeysAndSectionsToPlayRange() && !Sequencer.GetSequencerSettings()->ShouldKeepPlayRangeInSectionBounds())
				{
					DragPosition.X = FMath::Max(DragPosition.X, 0.f);
					FFrameTime CurrentTime = VirtualTrackArea.PixelToFrame(DragPosition.X);
					CurrentTime = UE::MovieScene::ClampToDiscreteRange(CurrentTime, Sequencer.GetPlaybackRange());
					DragPosition.X = VirtualTrackArea.FrameToPixel(CurrentTime);
				}
					
				double CurrentTime = VirtualTrackArea.PixelToSeconds(DragPosition.X);
				Sequencer.UpdateAutoScroll(CurrentTime);

				DragOperation->OnDrag(MouseEvent, DragPosition, VirtualTrackArea);
			}
		}
		// Otherwise we can attempt a new drag
		else if (DelayedDrag->AttemptDragStart(MouseEvent))
		{
			DragOperation = CreateDrag(MouseEvent);

			if (DragOperation.IsValid())
			{
				DragOperation->OnBeginDrag(MouseEvent, DelayedDrag->GetInitialPosition(), VirtualTrackArea);

				// Steal the capture, as we're now the authoritative widget in charge of a mouse-drag operation
				Reply.CaptureMouse(OwnerWidget.AsShared());
			}
		}

		return Reply;
	}
	return FReply::Unhandled();
}

bool FSequencerEditTool_Movement::GetHotspotTime(FFrameTime& HotspotTime) const
{
	if (DelayedDrag->Hotspot.IsValid())
	{
		TOptional<FFrameNumber> OptionalHotspotTime = DelayedDrag->Hotspot->GetTime();
		if (OptionalHotspotTime.IsSet())
		{
			HotspotTime = OptionalHotspotTime.GetValue();
			return true;
		}
	}
	return false;
}

FFrameTime FSequencerEditTool_Movement::GetHotspotOffsetTime(FFrameTime CurrentTime) const
{
	//@todo abstract dragging offset from shift
	if (DelayedDrag->Hotspot.IsValid() && FSlateApplication::Get().GetModifierKeys().IsShiftDown())
	{
		TOptional<FFrameTime> OptionalOffsetTime = DelayedDrag->Hotspot->GetOffsetTime();
		if (OptionalOffsetTime.IsSet())
		{
			return OptionalOffsetTime.GetValue();
		}
	}
	return CurrentTime - OriginalHotspotTime;
}

TSharedPtr<ISequencerEditToolDragOperation> FSequencerEditTool_Movement::CreateDrag(const FPointerEvent& MouseEvent)
{
	FSequencerSelection& Selection = Sequencer.GetSelection();
	TSharedRef<SSequencer> SequencerWidget = StaticCastSharedRef<SSequencer>(Sequencer.GetSequencerWidget());

	GetHotspotTime(OriginalHotspotTime);

	if (DelayedDrag->Hotspot.IsValid())
	{
		// Let the hotspot start a drag first, if it wants to
		auto HotspotDrag = DelayedDrag->Hotspot->InitiateDrag(Sequencer);
		if (HotspotDrag.IsValid())
		{
			return HotspotDrag;
		}
		auto HotspotType = DelayedDrag->Hotspot->GetType();

		const bool bSectionsSelected = Selection.GetSelectedSections().Num() > 0;
		const bool bKeySelected = Selection.GetSelectedKeys().Num() > 0;
		// @todo sequencer: Make this a customizable UI command modifier?
		const bool bIsDuplicateEvent = MouseEvent.IsAltDown() || MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton;
		const bool bHotspotIsSection = HotspotType == ESequencerHotspot::Section;

		// If they have both keys and sections selected then we only support moving them right now, so we
		// check for that first before trying to figure out if they're resizing or dilating.
		if (bSectionsSelected && bKeySelected && !bIsDuplicateEvent)
		{
			return MakeShareable(new FMoveKeysAndSections(Sequencer, Selection.GetSelectedKeys(), Selection.GetSelectedSections(), bHotspotIsSection));
		}
		else if (bIsDuplicateEvent)
		{
			if (HotspotType == ESequencerHotspot::Key)
			{
				TArrayView<const FSequencerSelectedKey> HoveredKeys = StaticCastSharedPtr<FKeyHotspot>(DelayedDrag->Hotspot)->Keys;

				auto AnyUnselectedKey = [&Selection](const FSequencerSelectedKey& InKey)
				{
					return !Selection.IsSelected(InKey);
				};

				if (HoveredKeys.ContainsByPredicate(AnyUnselectedKey))
				{
					// If any are not selected, we'll treat this as a unique drag
					Selection.EmptySelectedKeys();
					Selection.EmptySelectedSections();
					Selection.EmptyNodesWithSelectedKeysOrSections();
					for (const FSequencerSelectedKey& Key : HoveredKeys)
					{
						Selection.AddToSelection(Key);
					}
					SequencerHelpers::UpdateHoveredNodeFromSelectedKeys(Sequencer);
				}
			}
			else if (HotspotType == ESequencerHotspot::Section)
			{
				UMovieSceneSection* HoveredSection = StaticCastSharedPtr<FSectionHotspot>(DelayedDrag->Hotspot)->WeakSection.Get();

				if (!Selection.IsSelected(HoveredSection))
				{
					Selection.EmptySelectedKeys();
					Selection.EmptySelectedSections();
					Selection.EmptyNodesWithSelectedKeysOrSections();
					Selection.AddToSelection(HoveredSection);
					SequencerHelpers::UpdateHoveredNodeFromSelectedSections(Sequencer);
				}
			}

			return MakeShareable(new FDuplicateKeysAndSections(Sequencer, Selection.GetSelectedKeys(), Selection.GetSelectedSections(), bHotspotIsSection));
		}


		UMovieSceneSection* SectionToDrag = nullptr;
		if (HotspotType == ESequencerHotspot::Section || HotspotType == ESequencerHotspot::EasingArea)
		{
			SectionToDrag = StaticCastSharedPtr<FSectionHotspot>(DelayedDrag->Hotspot)->WeakSection.Get();
		}

		// Moving section(s)?
		if (SectionToDrag)
		{
			if (!Selection.IsSelected(SectionToDrag))
			{
				Selection.EmptySelectedKeys();
				Selection.EmptySelectedSections();
				Selection.EmptyNodesWithSelectedKeysOrSections();
				Selection.AddToSelection(SectionToDrag);
				SequencerHelpers::UpdateHoveredNodeFromSelectedSections(Sequencer);
			}

			if (MouseEvent.IsShiftDown())
			{
				const bool bDraggingByEnd = false;
				const bool bIsSlipping = true;
				return MakeShareable( new FResizeSection( Sequencer, Selection.GetSelectedSections(), bDraggingByEnd, bIsSlipping ) );
			}
			else
			{
				TSet<FSequencerSelectedKey> EmptyKeySet;
				return MakeShareable( new FMoveKeysAndSections( Sequencer, EmptyKeySet, Selection.GetSelectedSections(), true) );
			}
		}
		// Moving key(s)?
		else if (HotspotType == ESequencerHotspot::Key)
		{
			TArrayView<const FSequencerSelectedKey> HoveredKeys = StaticCastSharedPtr<FKeyHotspot>(DelayedDrag->Hotspot)->Keys;

			auto AnyUnselectedKey = [&Selection](const FSequencerSelectedKey& InKey)
			{
				return !Selection.IsSelected(InKey);
			};

			if (HoveredKeys.ContainsByPredicate(AnyUnselectedKey))
			{
				// If any are not selected, we'll treat this as a unique drag
				Selection.EmptySelectedKeys();
				Selection.EmptySelectedSections();
				Selection.EmptyNodesWithSelectedKeysOrSections();
				for (const FSequencerSelectedKey& Key : HoveredKeys)
				{
					Selection.AddToSelection(Key);
				}
				SequencerHelpers::UpdateHoveredNodeFromSelectedKeys(Sequencer);
			}

			TSet<TWeakObjectPtr<UMovieSceneSection>> NoSections;
			return MakeShareable( new FMoveKeysAndSections( Sequencer, Selection.GetSelectedKeys(), NoSections, false) );
		}
	}
	// If we're not dragging a hotspot, sections take precedence over keys
	else if (Selection.GetSelectedSections().Num())
	{
		TSet<FSequencerSelectedKey> EmptyKeySet;
		return MakeShareable( new FMoveKeysAndSections( Sequencer, EmptyKeySet, Selection.GetSelectedSections(), true ) );
	}
	else if (Selection.GetSelectedKeys().Num())
	{
		TSet<TWeakObjectPtr<UMovieSceneSection>> NoSections;
		return MakeShareable( new FMoveKeysAndSections( Sequencer, Selection.GetSelectedKeys(), NoSections, false) );
	}

	return nullptr;
}


FReply FSequencerEditTool_Movement::OnMouseButtonUp(SWidget& OwnerWidget, const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	DelayedDrag.Reset();

	if (DragOperation.IsValid())
	{
		TSharedRef<SSequencer> SequencerWidget = StaticCastSharedRef<SSequencer>(Sequencer.GetSequencerWidget());

		DragOperation->OnEndDrag(MouseEvent, MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()), SequencerWidget->GetVirtualTrackArea());
		DragOperation = nullptr;

		if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
		{
			GEditor->EndTransaction();
		}

		Sequencer.StopAutoscroll();

		// Only return handled if we actually started a drag
		return FReply::Handled().ReleaseMouseCapture();
	}

	SequencerHelpers::PerformDefaultSelection(Sequencer, MouseEvent);

	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton && !Sequencer.IsReadOnly())
	{
		TSharedPtr<SWidget> MenuContent = SequencerHelpers::SummonContextMenu( Sequencer, MyGeometry, MouseEvent );
		if (MenuContent.IsValid())
		{
			FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();

			TSharedPtr<IMenu> Menu = FSlateApplication::Get().PushMenu(
				OwnerWidget.AsShared(),
				WidgetPath,
				MenuContent.ToSharedRef(),
				MouseEvent.GetScreenSpacePosition(),
				FPopupTransitionEffect( FPopupTransitionEffect::ContextMenu )
				);

			// Lock the hotspot while the menu is open
			TSharedPtr<ISequencerHotspot> ExistingHotspot = Sequencer.GetHotspot();
			if (ExistingHotspot.IsValid())
			{
				ExistingHotspot->bIsLocked = true;
			}

			// Unlock and reset the hotspot when the menu closes
			{
				FSequencer* SequencerPtr = &Sequencer;
				Menu->GetOnMenuDismissed().AddLambda(
					[=](TSharedRef<IMenu>)
					{
						if (ExistingHotspot.IsValid())
						{
							ExistingHotspot->bIsLocked = false;
						}
						if (SequencerPtr->GetHotspot() == ExistingHotspot)
						{
							SequencerPtr->SetHotspot(nullptr);
						}
					}
				);
			}

			return FReply::Handled().SetUserFocus(MenuContent.ToSharedRef(), EFocusCause::SetDirectly).ReleaseMouseCapture();
		}
	}

	return FReply::Handled();
}


void FSequencerEditTool_Movement::OnMouseCaptureLost()
{
	DelayedDrag.Reset();
	DragOperation = nullptr;
}


int32 FSequencerEditTool_Movement::OnPaint(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	if (DelayedDrag.IsSet() && DelayedDrag->IsDragging())
	{
		const TSharedPtr<ISequencerHotspot>& Hotspot = DelayedDrag->Hotspot;

		if (Hotspot.IsValid())
		{
			FFrameTime CurrentTime;

			if (GetHotspotTime(CurrentTime))
			{
				TSharedRef<SSequencer> SequencerWidget = StaticCastSharedRef<SSequencer>(Sequencer.GetSequencerWidget());

				const FSlateFontInfo SmallLayoutFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);
				const TSharedRef< FSlateFontMeasure > FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
				const FLinearColor DrawColor = FEditorStyle::GetSlateColor("SelectionColor").GetColor(FWidgetStyle());
				const FVector2D BoxPadding = FVector2D(4.0f, 2.0f);
				const float MousePadding = 20.0f;

				// calculate draw position
				const FVirtualTrackArea VirtualTrackArea = SequencerWidget->GetVirtualTrackArea();
				const float HorizontalDelta = DragPosition.X - DelayedDrag->GetInitialPosition().X;
				const float InitialY = DelayedDrag->GetInitialPosition().Y;

				const FVector2D OldPos = FVector2D(VirtualTrackArea.FrameToPixel(OriginalHotspotTime), InitialY);
				const FVector2D NewPos = FVector2D(VirtualTrackArea.FrameToPixel(CurrentTime), InitialY);

				TArray<FVector2D> LinePoints;
				{
					LinePoints.AddUninitialized(2);
					LinePoints[0] = FVector2D(0.0f, 0.0f);
					LinePoints[1] = FVector2D(0.0f, VirtualTrackArea.GetPhysicalSize().Y);
				}

				// draw old position vertical
				FSlateDrawElement::MakeLines(
					OutDrawElements,
					LayerId + 1,
					AllottedGeometry.ToPaintGeometry(FVector2D(OldPos.X, 0.0f), FVector2D(1.0f, 1.0f)),
					LinePoints,
					ESlateDrawEffect::None,
					FLinearColor::White.CopyWithNewOpacity(0.5f),
					false
				);

				// draw new position vertical
				FSlateDrawElement::MakeLines(
					OutDrawElements,
					LayerId + 1,
					AllottedGeometry.ToPaintGeometry(FVector2D(NewPos.X, 0.0f), FVector2D(1.0f, 1.0f)),
					LinePoints,
					ESlateDrawEffect::None,
					DrawColor,
					false
				);

				// draw time string
				const FString TimeString = TimeToString(CurrentTime, false);
				const FVector2D TimeStringSize = FontMeasureService->Measure(TimeString, SmallLayoutFont);
				const FVector2D TimePos = FVector2D(NewPos.X - MousePadding - TimeStringSize.X, NewPos.Y - 0.5f * TimeStringSize.Y);

				FSlateDrawElement::MakeBox( 
					OutDrawElements,
					LayerId + 2, 
					AllottedGeometry.ToPaintGeometry(TimePos - BoxPadding, TimeStringSize + 2.0f * BoxPadding),
					FEditorStyle::GetBrush("WhiteBrush"),
					ESlateDrawEffect::None, 
					FLinearColor::Black.CopyWithNewOpacity(0.5f)
				);

				FSlateDrawElement::MakeText(
					OutDrawElements,
					LayerId + 3,
					AllottedGeometry.ToPaintGeometry(TimePos, TimeStringSize),
					TimeString,
					SmallLayoutFont,
					ESlateDrawEffect::None,
					DrawColor
				);

				// draw offset string
				FFrameTime OffsetTime = GetHotspotOffsetTime(CurrentTime);
				const FString OffsetString = TimeToString(OffsetTime, true);
				const FVector2D OffsetStringSize = FontMeasureService->Measure(OffsetString, SmallLayoutFont);
				const FVector2D OffsetPos = FVector2D(NewPos.X + MousePadding, NewPos.Y - 0.5f * OffsetStringSize.Y);

				FSlateDrawElement::MakeBox( 
					OutDrawElements,
					LayerId + 2, 
					AllottedGeometry.ToPaintGeometry(OffsetPos - BoxPadding, OffsetStringSize + 2.0f * BoxPadding),
					FEditorStyle::GetBrush("WhiteBrush"),
					ESlateDrawEffect::None, 
					FLinearColor::Black.CopyWithNewOpacity(0.5f)
				);

				FSlateDrawElement::MakeText(
					OutDrawElements,
					LayerId + 3,
					AllottedGeometry.ToPaintGeometry(OffsetPos, TimeStringSize),
					OffsetString,
					SmallLayoutFont,
					ESlateDrawEffect::None,
					DrawColor
				);
			}
		}
	}

	return LayerId;
}


FCursorReply FSequencerEditTool_Movement::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	TSharedPtr<ISequencerHotspot> Hotspot = DelayedDrag.IsSet()
		? DelayedDrag->Hotspot
		: Sequencer.GetHotspot();

	if (Hotspot.IsValid())
	{
		FCursorReply Reply = Hotspot->GetCursor();
		if (Reply.IsEventHandled())
		{
			return Reply;
		}
	}

	return FCursorReply::Cursor(EMouseCursor::CardinalCross);
}


FName FSequencerEditTool_Movement::GetIdentifier() const
{
	return Identifier;
}


bool FSequencerEditTool_Movement::CanDeactivate() const
{
	return !DelayedDrag.IsSet();
}


FString FSequencerEditTool_Movement::TimeToString(FFrameTime Time, bool IsDelta) const
{
	USequencerSettings* Settings = Sequencer.GetSequencerSettings();
	check(Settings);

	// We don't use the Sequencer's Numeric Type interface as we want to show a "+" only for delta movement and not the absolute time.
	EFrameNumberDisplayFormats DisplayFormat = Settings->GetTimeDisplayFormat();
	switch (DisplayFormat)
	{
		case EFrameNumberDisplayFormats::Seconds:
		{
			FFrameRate TickResolution = Sequencer.GetFocusedTickResolution();
			double TimeInSeconds = TickResolution.AsSeconds(Time);
			return IsDelta ? FString::Printf(TEXT("[%+.2fs]"), TimeInSeconds) : FString::Printf(TEXT("%.2fs"), TimeInSeconds);
		}
		case EFrameNumberDisplayFormats::Frames:
		{
			FFrameRate TickResolution = Sequencer.GetFocusedTickResolution();
			FFrameRate DisplayRate    = Sequencer.GetFocusedDisplayRate();

			// Convert from sequence resolution into display rate frames.
			FFrameTime DisplayTime = FFrameRate::TransformTime(Time, TickResolution, DisplayRate);
			FString SubframeIndicator = FMath::IsNearlyZero(DisplayTime.GetSubFrame()) ? TEXT("") : TEXT("*");
			int32 ZeroPadFrames = Sequencer.GetSequencerSettings()->GetZeroPadFrames();
			return IsDelta ? FString::Printf(TEXT("[%+0*d%s]"), ZeroPadFrames, DisplayTime.GetFrame().Value, *SubframeIndicator) : FString::Printf(TEXT("%0*d%s"), ZeroPadFrames, DisplayTime.GetFrame().Value, *SubframeIndicator);
		}
		case EFrameNumberDisplayFormats::NonDropFrameTimecode:
		{
			FFrameRate SourceFrameRate = Sequencer.GetFocusedTickResolution();
			FFrameRate DestinationFrameRate = Sequencer.GetFocusedDisplayRate();

			FFrameNumber DisplayRateFrameNumber = FFrameRate::TransformTime(Time, SourceFrameRate, DestinationFrameRate).FloorToFrame();

			FTimecode AsNonDropTimecode = FTimecode::FromFrameNumber(DisplayRateFrameNumber, DestinationFrameRate, false);

			const bool bForceSignDisplay = IsDelta;
			return IsDelta ? FString::Printf(TEXT("[%s]"), *AsNonDropTimecode.ToString(bForceSignDisplay)) : FString::Printf(TEXT("%s"), *AsNonDropTimecode.ToString(bForceSignDisplay));
		}
		case EFrameNumberDisplayFormats::DropFrameTimecode:
		{
			FFrameRate SourceFrameRate = Sequencer.GetFocusedTickResolution();
			FFrameRate DestinationFrameRate = Sequencer.GetFocusedDisplayRate();

			FFrameNumber DisplayRateFrameNumber = FFrameRate::TransformTime(Time, SourceFrameRate, DestinationFrameRate).FloorToFrame();

			FTimecode AsDropTimecode = FTimecode::FromFrameNumber(DisplayRateFrameNumber, DestinationFrameRate, true);

			const bool bForceSignDisplay = IsDelta;
			return IsDelta ? FString::Printf(TEXT("[%s]"), *AsDropTimecode.ToString(bForceSignDisplay)) : FString::Printf(TEXT("%s"), *AsDropTimecode.ToString(bForceSignDisplay));
		}
	}

	return FString();
}

const ISequencerHotspot* FSequencerEditTool_Movement::GetDragHotspot() const
{
	return DelayedDrag.IsSet() ? DelayedDrag->Hotspot.Get() : nullptr;
}
