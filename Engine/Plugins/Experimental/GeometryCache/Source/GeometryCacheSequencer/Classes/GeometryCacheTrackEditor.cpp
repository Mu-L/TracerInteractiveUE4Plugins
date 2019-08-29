// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "GeometryCacheTrackEditor.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "Modules/ModuleManager.h"
#include "Layout/WidgetPath.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBox.h"
#include "SequencerSectionPainter.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "MovieSceneGeometryCacheTrack.h"
#include "MovieSceneGeometryCacheSection.h"
#include "CommonMovieSceneTools.h"
#include "ContentBrowserModule.h"
#include "SequencerUtilities.h"
#include "ISectionLayoutBuilder.h"
#include "EditorStyleSet.h"
#include "MovieSceneTimeHelpers.h"
#include "Fonts/FontMeasure.h"
#include "SequencerTimeSliderController.h"
#include "Misc/MessageDialog.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "GeometryCacheComponent.h"
#include "GeometryCache.h"

namespace GeometryCacheEditorConstants
{
	// @todo Sequencer Allow this to be customizable
	const uint32 AnimationTrackHeight = 20;
}

#define LOCTEXT_NAMESPACE "FGeometryCacheTrackEditor"

static UGeometryCacheComponent* AcquireGeometryCacheFromObjectGuid(const FGuid& Guid, TSharedPtr<ISequencer> SequencerPtr)
{
	UObject* BoundObject = SequencerPtr.IsValid() ? SequencerPtr->FindSpawnedObjectOrTemplate(Guid) : nullptr;

	if (AActor* Actor = Cast<AActor>(BoundObject))
	{
		TInlineComponentArray<UGeometryCacheComponent*> GeometryMeshComponents;
		Actor->GetComponents(GeometryMeshComponents);

		for (int32 j = 0; j <GeometryMeshComponents.Num(); ++j)
		{
			UGeometryCacheComponent* GeometryMeshComp = GeometryMeshComponents[j];
			if (GeometryMeshComp)
			{
				return GeometryMeshComp;
			}
		}
	}
	else if(UGeometryCacheComponent* GeometryMeshComp = Cast<UGeometryCacheComponent>(BoundObject))
	{
		if (GeometryMeshComp->GetGeometryCache())
		{
			return GeometryMeshComp;
		}
	}

	return nullptr;
}


FGeometryCacheSection::FGeometryCacheSection( UMovieSceneSection& InSection, TWeakPtr<ISequencer> InSequencer)
	: Section(*CastChecked<UMovieSceneGeometryCacheSection>(&InSection))
	, Sequencer(InSequencer)
	, InitialStartOffsetDuringResize(0.f)
	, InitialStartTimeDuringResize(0)
{ }


UMovieSceneSection* FGeometryCacheSection::GetSectionObject()
{ 
	return &Section;
}


FText FGeometryCacheSection::GetSectionTitle() const
{
	if (Section.Params.GeometryCache.ResolveObject() != nullptr )
	{
		UGeometryCacheComponent* GeometryCache = Cast<UGeometryCacheComponent>(Section.Params.GeometryCache.ResolveObject());
		if (GeometryCache && GeometryCache->GetOwner() != nullptr)
		{	
			return FText::FromString(GeometryCache->GetOwner()->GetName());
		}
	}
	return LOCTEXT("NoGeometryCacheSection", "No GeometryCache");
}


float FGeometryCacheSection::GetSectionHeight() const
{
	return (float)GeometryCacheEditorConstants::AnimationTrackHeight;
}


int32 FGeometryCacheSection::OnPaintSection( FSequencerSectionPainter& Painter ) const
{
	const ESlateDrawEffect DrawEffects = Painter.bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	
	const FTimeToPixel& TimeToPixelConverter = Painter.GetTimeConverter();

	int32 LayerId = Painter.PaintSectionBackground();

	static const FSlateBrush* GenericDivider = FEditorStyle::GetBrush("Sequencer.GenericDivider");

	if (!Section.HasStartFrame() || !Section.HasEndFrame())
	{
		return LayerId;
	}

	// Add lines where the animation starts and ends/loops
	float AnimPlayRate = FMath::IsNearlyZero(Section.Params.PlayRate) ? 1.0f : Section.Params.PlayRate;
	float SeqLength = (Section.Params.GetSequenceLength() - (Section.Params.StartOffset + Section.Params.EndOffset)) / AnimPlayRate;

	FFrameRate TickResolution = TimeToPixelConverter.GetTickResolution();
	if (!FMath::IsNearlyZero(SeqLength, KINDA_SMALL_NUMBER) && SeqLength > 0)
	{
		float MaxOffset  = Section.GetRange().Size<FFrameTime>() / TickResolution;
		float OffsetTime = SeqLength;
		float StartTime  = Section.GetInclusiveStartFrame() / TickResolution;

		while (OffsetTime < MaxOffset)
		{
			float OffsetPixel = TimeToPixelConverter.SecondsToPixel(StartTime + OffsetTime) - TimeToPixelConverter.SecondsToPixel(StartTime);

			FSlateDrawElement::MakeBox(
				Painter.DrawElements,
				LayerId,
				Painter.SectionGeometry.MakeChild(
					FVector2D(2.f, Painter.SectionGeometry.Size.Y-2.f),
					FSlateLayoutTransform(FVector2D(OffsetPixel, 1.f))
				).ToPaintGeometry(),
				GenericDivider,
				DrawEffects
			);

			OffsetTime += SeqLength;
		}
	}

	TSharedPtr<ISequencer> SequencerPtr = Sequencer.Pin();
	if (Painter.bIsSelected && SequencerPtr.IsValid())
	{
		FFrameTime CurrentTime = SequencerPtr->GetLocalTime().Time;
		if (Section.GetRange().Contains(CurrentTime.FrameNumber) && Section.Params.GeometryCache.ResolveObject() != nullptr)
		{
			const float Time = TimeToPixelConverter.FrameToPixel(CurrentTime); 

			UGeometryCacheComponent* GeometryCache = Cast<UGeometryCacheComponent>(Section.Params.GeometryCache.ResolveObject());

			// Draw the current time next to the scrub handle
			const float AnimTime = Section.MapTimeToAnimation(CurrentTime, TickResolution);			
			int32 FrameTime = GeometryCache->GetFrameAtTime(AnimTime);
			FString FrameString = FString::FromInt(FrameTime);

			const FSlateFontInfo SmallLayoutFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);
			const TSharedRef< FSlateFontMeasure > FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			FVector2D TextSize = FontMeasureService->Measure(FrameString, SmallLayoutFont);

			// Flip the text position if getting near the end of the view range
			static const float TextOffsetPx = 10.f;
			bool  bDrawLeft = (Painter.SectionGeometry.Size.X - Time) < (TextSize.X + 22.f) - TextOffsetPx;
			float TextPosition = bDrawLeft ? Time - TextSize.X - TextOffsetPx : Time + TextOffsetPx;
			//handle mirrored labels
			const float MajorTickHeight = 9.0f; 
			FVector2D TextOffset(TextPosition, Painter.SectionGeometry.Size.Y - (MajorTickHeight + TextSize.Y));

			const FLinearColor DrawColor = FEditorStyle::GetSlateColor("SelectionColor").GetColor(FWidgetStyle());
			const FVector2D BoxPadding = FVector2D(4.0f, 2.0f);
			// draw time string
	
			FSlateDrawElement::MakeBox(
				Painter.DrawElements,
				LayerId + 5,
				Painter.SectionGeometry.ToPaintGeometry(TextOffset - BoxPadding, TextSize + 2.0f * BoxPadding),
				FEditorStyle::GetBrush("WhiteBrush"),
				ESlateDrawEffect::None,
				FLinearColor::Black.CopyWithNewOpacity(0.5f)
			);

			FSlateDrawElement::MakeText(
				Painter.DrawElements,
				LayerId + 6,
				Painter.SectionGeometry.ToPaintGeometry(TextOffset, TextSize),
				FrameString,
				SmallLayoutFont,
				DrawEffects,
				DrawColor
			);

		}
	}
	
	return LayerId;
}

void FGeometryCacheSection::BeginResizeSection()
{
	InitialStartOffsetDuringResize = Section.Params.StartOffset;
	InitialStartTimeDuringResize   = Section.HasStartFrame() ? Section.GetInclusiveStartFrame() : 0;
}

void FGeometryCacheSection::ResizeSection(ESequencerSectionResizeMode ResizeMode, FFrameNumber ResizeTime)
{
	// Adjust the start offset when resizing from the beginning
	if (ResizeMode == SSRM_LeadingEdge)
	{
		FFrameRate FrameRate   = Section.GetTypedOuter<UMovieScene>()->GetTickResolution();
		float      StartOffset = (ResizeTime - InitialStartTimeDuringResize) / FrameRate * Section.Params.PlayRate;

		StartOffset += InitialStartOffsetDuringResize;

		// Ensure start offset is not less than 0 and adjust ResizeTime
		if (StartOffset < 0)
		{
			ResizeTime = ResizeTime - ((StartOffset * Section.Params.PlayRate) * FrameRate).RoundToFrame();

			StartOffset = 0.f;
		}

		Section.Params.StartOffset = StartOffset;
	}

	ISequencerSection::ResizeSection(ResizeMode, ResizeTime);
}

void FGeometryCacheSection::BeginSlipSection()
{
	BeginResizeSection();
}

void FGeometryCacheSection::SlipSection(double SlipTime)
{
	float StartOffset = (SlipTime - InitialStartTimeDuringResize / Section.GetTypedOuter<UMovieScene>()->GetTickResolution()) * Section.Params.PlayRate;
	StartOffset += InitialStartOffsetDuringResize;

	// Ensure start offset is not less than 0
	if (StartOffset < 0)
	{
		StartOffset = 0.f;
	}

	Section.Params.StartOffset = StartOffset;

	ISequencerSection::SlipSection(SlipTime);
}


FGeometryCacheTrackEditor::FGeometryCacheTrackEditor( TSharedRef<ISequencer> InSequencer )
	: FMovieSceneTrackEditor( InSequencer ) 
{ }


TSharedRef<ISequencerTrackEditor> FGeometryCacheTrackEditor::CreateTrackEditor( TSharedRef<ISequencer> InSequencer )
{
	return MakeShareable( new FGeometryCacheTrackEditor( InSequencer ) );
}


bool FGeometryCacheTrackEditor::SupportsType( TSubclassOf<UMovieSceneTrack> Type ) const
{
	return Type == UMovieSceneGeometryCacheTrack::StaticClass();
}


TSharedRef<ISequencerSection> FGeometryCacheTrackEditor::MakeSectionInterface( UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding )
{
	check( SupportsType( SectionObject.GetOuter()->GetClass() ) );
	
	return MakeShareable( new FGeometryCacheSection(SectionObject, GetSequencer()) );
}

void FGeometryCacheTrackEditor::BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, const FGuid& ObjectBinding, const UClass* ObjectClass)
{
	if (ObjectClass->IsChildOf(UGeometryCacheComponent::StaticClass()) || ObjectClass->IsChildOf(AActor::StaticClass()))
	{
		UGeometryCacheComponent* GeomMeshComp = AcquireGeometryCacheFromObjectGuid(ObjectBinding, GetSequencer());

		if (GeomMeshComp)
		{
			UMovieSceneTrack* Track = nullptr;

			MenuBuilder.AddMenuEntry(
				NSLOCTEXT("Sequencer", "AddGeometryCache", "Geometry Cache"),
				NSLOCTEXT("Sequencer", "AddGeometryCacheTooltip", "Adds a Geometry Cache track."),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &FGeometryCacheTrackEditor::BuildGeometryCacheTrack, ObjectBinding, GeomMeshComp, Track)
				)
			);
		}
	}
}

void FGeometryCacheTrackEditor::BuildGeometryCacheTrack(FGuid ObjectBinding, UGeometryCacheComponent *GeomCacheComp, UMovieSceneTrack* Track)
{
	TSharedPtr<ISequencer> SequencerPtr = GetSequencer();

	if (SequencerPtr.IsValid() && ObjectBinding.IsValid())
	{
		UObject* Object = SequencerPtr->FindSpawnedObjectOrTemplate(ObjectBinding);
		if (Object)
		{
			const FScopedTransaction Transaction(LOCTEXT("AddGeometryCache_Transaction", "Add Geometry Cache"));
			AnimatablePropertyChanged(FOnKeyProperty::CreateRaw(this, &FGeometryCacheTrackEditor::AddKeyInternal, Object, GeomCacheComp, Track));
		}
	}
}

FKeyPropertyResult FGeometryCacheTrackEditor::AddKeyInternal( FFrameNumber KeyTime, UObject* Object, UGeometryCacheComponent* GeomCacheComp, UMovieSceneTrack* Track)
{
	FKeyPropertyResult KeyPropertyResult;

	FFindOrCreateHandleResult HandleResult = FindOrCreateHandleToObject( Object );
	FGuid ObjectHandle = HandleResult.Handle;
	KeyPropertyResult.bHandleCreated |= HandleResult.bWasCreated;
	if (ObjectHandle.IsValid())
	{
		if (!Track)
		{
			Track = AddTrack(GetSequencer()->GetFocusedMovieSceneSequence()->GetMovieScene(), ObjectHandle, UMovieSceneGeometryCacheTrack::StaticClass(), NAME_None);
			KeyPropertyResult.bTrackCreated = true;
		}

		if (ensure(Track))
		{
			Track->Modify();

			UMovieSceneSection* NewSection = Cast<UMovieSceneGeometryCacheTrack>(Track)->AddNewAnimation( KeyTime, GeomCacheComp );
			KeyPropertyResult.bTrackModified = true;

			GetSequencer()->EmptySelection();
			GetSequencer()->SelectSection(NewSection);
			GetSequencer()->ThrobSectionSelection();
		}
	}

	return KeyPropertyResult;
}


TSharedPtr<SWidget> FGeometryCacheTrackEditor::BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params)
{
	UGeometryCacheComponent* GeomMeshComp = AcquireGeometryCacheFromObjectGuid(ObjectBinding, GetSequencer());

	if (GeomMeshComp)
	{
		TWeakPtr<ISequencer> WeakSequencer = GetSequencer();

		auto SubMenuCallback = [=]() -> TSharedRef<SWidget>
		{
			FMenuBuilder MenuBuilder(true, nullptr);

			BuildGeometryCacheTrack(ObjectBinding, GeomMeshComp, Track);
			
			return MenuBuilder.MakeWidget();
		};

		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				FSequencerUtilities::MakeAddButton(LOCTEXT("GeomCacheText", "Geometry Cache"), FOnGetContent::CreateLambda(SubMenuCallback), Params.NodeIsHovered)
			];
	}
	else
	{
		return TSharedPtr<SWidget>();
	}
	
}


#undef LOCTEXT_NAMESPACE
