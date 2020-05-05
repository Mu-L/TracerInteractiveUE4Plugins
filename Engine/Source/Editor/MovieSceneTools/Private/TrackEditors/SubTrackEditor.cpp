// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrackEditors/SubTrackEditor.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "Modules/ModuleManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBox.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "EditorStyleSet.h"
#include "GameFramework/PlayerController.h"
#include "Sections/MovieSceneSubSection.h"
#include "Tracks/MovieSceneSubTrack.h"
#include "Tracks/MovieSceneCinematicShotTrack.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "SequencerUtilities.h"
#include "SequencerSectionPainter.h"
#include "ISequenceRecorder.h"
#include "SequenceRecorderSettings.h"
#include "TrackEditors/SubTrackEditorBase.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "MovieSceneToolHelpers.h"
#include "Misc/QualifiedFrameTime.h"
#include "MovieSceneTimeHelpers.h"
#include "EngineAnalytics.h"
#include "Interfaces/IAnalyticsProvider.h"
#include "Algo/Accumulate.h"

#include "CommonMovieSceneTools.h"

namespace SubTrackEditorConstants
{
	const float TrackHeight = 50.0f;
}


#define LOCTEXT_NAMESPACE "FSubTrackEditor"


/**
 * A generic implementation for displaying simple property sections.
 */
class FSubSection
	: public TSubSectionMixin<>
{
public:

	FSubSection(TSharedPtr<ISequencer> InSequencer, UMovieSceneSection& InSection, const FText& InDisplayName, TSharedPtr<FSubTrackEditor> InSubTrackEditor)
		: TSubSectionMixin(InSequencer, *CastChecked<UMovieSceneSubSection>(&InSection))
		, DisplayName(InDisplayName)
		, SubTrackEditor(InSubTrackEditor)
	{
	}

public:

	// ISequencerSection interface

	virtual float GetSectionHeight() const override
	{
		return SubTrackEditorConstants::TrackHeight;
	}

	virtual FText GetSectionTitle() const override
	{
		const UMovieSceneSubSection& SectionObject = GetSubSectionObject();
		
		if(SectionObject.GetSequence() == nullptr && UMovieSceneSubSection::GetRecordingSection() == &SectionObject)
		{
			AActor* ActorToRecord = UMovieSceneSubSection::GetActorToRecord();

			ISequenceRecorder& SequenceRecorder = FModuleManager::LoadModuleChecked<ISequenceRecorder>("SequenceRecorder");
			if(SequenceRecorder.IsRecording())
			{
				if(ActorToRecord != nullptr)
				{
					return FText::Format(LOCTEXT("RecordingIndicatorWithActor", "Sequence Recording for \"{0}\""), FText::FromString(ActorToRecord->GetActorLabel()));
				}
				else
				{
					return LOCTEXT("RecordingIndicator", "Sequence Recording");
				}
			}
			else
			{
				if(ActorToRecord != nullptr)
				{
					return FText::Format(LOCTEXT("RecordingPendingIndicatorWithActor", "Sequence Recording Pending for \"{0}\""), FText::FromString(ActorToRecord->GetActorLabel()));
				}
				else
				{
					return LOCTEXT("RecordingPendingIndicator", "Sequence Recording Pending");
				}
			}
		}
		else
		{
			return TSubSectionMixin::GetSectionTitle();
		}
	}
	
	virtual int32 OnPaintSection( FSequencerSectionPainter& InPainter ) const override
	{
		InPainter.PaintSectionBackground();

		const UMovieSceneSubSection& SectionObject = GetSubSectionObject();

		FSubSectionPainterResult PaintResult = FSubSectionPainterUtil::PaintSection(
				GetSequencer(), SectionObject, InPainter, FSubSectionPainterParams(GetContentPadding()));
		if (PaintResult == FSSPR_InvalidSection)
		{
			return InPainter.LayerId;
		}

		int32 LayerId = InPainter.LayerId;

		if (SectionObject.GetSequence() == nullptr && UMovieSceneSubSection::GetRecordingSection() == &SectionObject)
		{
			const ESlateDrawEffect DrawEffects = InPainter.bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

			FColor SubSectionColor = FColor(180, 75, 75, 190);
	
			ISequenceRecorder& SequenceRecorder = FModuleManager::LoadModuleChecked<ISequenceRecorder>("SequenceRecorder");
			if(SequenceRecorder.IsRecording())
			{
				SubSectionColor = FColor(200, 10, 10, 190);
			}

			FSlateDrawElement::MakeBox(
				InPainter.DrawElements,
				++LayerId,
				InPainter.SectionGeometry.ToPaintGeometry(
					FVector2D(0.f, 0.f),
					InPainter.SectionGeometry.Size
				),
				FEditorStyle::GetBrush("Sequencer.Section.BackgroundTint"),
				DrawEffects,
				SubSectionColor
			);

			// display where we will create the recording
			FString Path = SectionObject.GetTargetPathToRecordTo() / SectionObject.GetTargetSequenceName();
			if (Path.Len() > 0)
			{
				FSlateDrawElement::MakeText(
					InPainter.DrawElements,
					++LayerId,
					InPainter.SectionGeometry.ToOffsetPaintGeometry(FVector2D(11.0f, 32.0f)),
					FText::Format(LOCTEXT("RecordingDestination", "Target: \"{0}\""), FText::FromString(Path)),
					FEditorStyle::GetFontStyle("NormalFont"),
					DrawEffects,
					FColor(200, 200, 200)
				);
			}
		}

		return LayerId;
	}

	virtual void BuildSectionContextMenu(FMenuBuilder& MenuBuilder, const FGuid& ObjectBinding) override
	{
		ISequencerSection::BuildSectionContextMenu(MenuBuilder, ObjectBinding);

		MenuBuilder.AddSubMenu(
			LOCTEXT("TakesMenu", "Takes"),
			LOCTEXT("TakesMenuTooltip", "Sub section takes"),
			FNewMenuDelegate::CreateLambda([=](FMenuBuilder& InMenuBuilder){ AddTakesMenu(InMenuBuilder); }));

		MenuBuilder.AddMenuEntry(
			LOCTEXT("PlayableDirectly_Label", "Playable Directly"),
			LOCTEXT("PlayableDirectly_Tip", "When enabled, this sequence will also support being played directly outside of the master sequence. Disable this to save some memory on complex hierarchies of sequences."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateRaw(this, &FSubSection::TogglePlayableDirectly),
				FCanExecuteAction::CreateLambda([]{ return true; }),
				FGetActionCheckState::CreateRaw(this, &FSubSection::IsPlayableDirectly)
			),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}

	void TogglePlayableDirectly()
	{
		TSharedPtr<ISequencer> Sequencer = GetSequencer();
		if (Sequencer)
		{
			FScopedTransaction Transaction(LOCTEXT("SetPlayableDirectly_Transaction", "Set Playable Directly"));

			TArray<UMovieSceneSection*> SelectedSections;
			Sequencer->GetSelectedSections(SelectedSections);

			const bool bNewPlayableDirectly = IsPlayableDirectly() != ECheckBoxState::Checked;

			for (UMovieSceneSection* Section : SelectedSections)
			{
				if (UMovieSceneSubSection* SubSection = Cast<UMovieSceneSubSection>(Section))
				{
					UMovieSceneSequence* Sequence = SubSection->GetSequence();
					if (Sequence->IsPlayableDirectly() != bNewPlayableDirectly)
					{
						Sequence->SetPlayableDirectly(bNewPlayableDirectly);
					}
				}
			}
		}
	}

	ECheckBoxState IsPlayableDirectly() const
	{
		ECheckBoxState CheckboxState = ECheckBoxState::Undetermined;

		TSharedPtr<ISequencer> Sequencer = GetSequencer();
		if (Sequencer)
		{
			TArray<UMovieSceneSection*> SelectedSections;
			Sequencer->GetSelectedSections(SelectedSections);

			for (UMovieSceneSection* Section : SelectedSections)
			{
				if (UMovieSceneSubSection* SubSection = Cast<UMovieSceneSubSection>(Section))
				{
					UMovieSceneSequence* Sequence = SubSection->GetSequence();
					if (Sequence)
					{
						if (CheckboxState == ECheckBoxState::Undetermined)
						{
							CheckboxState = Sequence->IsPlayableDirectly() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}
						else if (CheckboxState == ECheckBoxState::Checked != Sequence->IsPlayableDirectly())
						{
							return ECheckBoxState::Undetermined;
						}
					}
				}
			}
		}

		return CheckboxState;
	}

	virtual bool IsReadOnly() const override
	{
		// Overridden to false regardless of movie scene section read only state so that we can double click into the sub section
		return false;
	}

private:

	void AddTakesMenu(FMenuBuilder& MenuBuilder)
	{
		TArray<FAssetData> AssetData;
		uint32 CurrentTakeNumber = INDEX_NONE;
		UMovieSceneSubSection& SectionObject = GetSubSectionObject();
		MovieSceneToolHelpers::GatherTakes(&SectionObject, AssetData, CurrentTakeNumber);

		AssetData.Sort([&SectionObject](const FAssetData &A, const FAssetData &B) {
			uint32 TakeNumberA = INDEX_NONE;
			uint32 TakeNumberB = INDEX_NONE;
			if (MovieSceneToolHelpers::GetTakeNumber(&SectionObject, A, TakeNumberA) && MovieSceneToolHelpers::GetTakeNumber(&SectionObject, B, TakeNumberB))
			{
				return TakeNumberA < TakeNumberB;
			}
			return true;
		});

		for (auto ThisAssetData : AssetData)
		{
			uint32 TakeNumber = INDEX_NONE;
			if (MovieSceneToolHelpers::GetTakeNumber(&SectionObject, ThisAssetData, TakeNumber))
			{
				UObject* TakeObject = ThisAssetData.GetAsset();

				if (TakeObject)
				{
					MenuBuilder.AddMenuEntry(
						FText::Format(LOCTEXT("TakeNumber", "Take {0}"), FText::AsNumber(TakeNumber)),
						FText::Format(LOCTEXT("TakeNumberTooltip", "Switch to {0}"), FText::FromString(TakeObject->GetPathName())),
						TakeNumber == CurrentTakeNumber ? FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.Star") : FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.Empty"),
						FUIAction(FExecuteAction::CreateSP(SubTrackEditor.Pin().ToSharedRef(), &FSubTrackEditor::SwitchTake, TakeObject))
					);
				}
			}
		}
	}

private:

	/** Display name of the section */
	FText DisplayName;

	/** The sub track editor that contains this section */
	TWeakPtr<FSubTrackEditor> SubTrackEditor;
};


/* FSubTrackEditor structors
 *****************************************************************************/

FSubTrackEditor::FSubTrackEditor(TSharedRef<ISequencer> InSequencer)
	: FMovieSceneTrackEditor(InSequencer) 
{ }


/* ISequencerTrackEditor interface
 *****************************************************************************/

void FSubTrackEditor::BuildAddTrackMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddSubTrack", "Subscenes Track"),
		LOCTEXT("AddSubTooltip", "Adds a new track that can contain other sequences."),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.Tracks.Sub"),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FSubTrackEditor::HandleAddSubTrackMenuEntryExecute)
		)
	);
}

TSharedPtr<SWidget> FSubTrackEditor::BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params)
{
	// Create a container edit box
	return SNew(SHorizontalBox)

	// Add the sub sequence combo box
	+ SHorizontalBox::Slot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	[
		FSequencerUtilities::MakeAddButton(LOCTEXT("SubText", "Sequence"), FOnGetContent::CreateSP(this, &FSubTrackEditor::HandleAddSubSequenceComboButtonGetMenuContent, Track), Params.NodeIsHovered, GetSequencer())
	];
}


TSharedRef<ISequencerTrackEditor> FSubTrackEditor::CreateTrackEditor(TSharedRef<ISequencer> InSequencer)
{
	return MakeShareable(new FSubTrackEditor(InSequencer));
}


TSharedRef<ISequencerSection> FSubTrackEditor::MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding)
{
	return MakeShareable(new FSubSection(GetSequencer(), SectionObject, Track.GetDisplayName(), SharedThis(this)));
}


bool FSubTrackEditor::HandleAssetAdded(UObject* Asset, const FGuid& TargetObjectGuid)
{
	UMovieSceneSequence* Sequence = Cast<UMovieSceneSequence>(Asset);

	if (Sequence == nullptr)
	{
		return false;
	}

	if (!SupportsSequence(Sequence))
	{
		return false;
	}

	//@todo If there's already a cinematic shot track, allow that track to handle this asset
	UMovieScene* FocusedMovieScene = GetFocusedMovieScene();

	if (FocusedMovieScene != nullptr && FocusedMovieScene->FindMasterTrack<UMovieSceneCinematicShotTrack>() != nullptr)
	{
		return false;
	}

	if (Sequence->GetMovieScene()->GetPlaybackRange().IsEmpty())
	{
		FNotificationInfo Info(FText::Format(LOCTEXT("InvalidSequenceDuration", "Invalid level sequence {0}. The sequence has no duration."), Sequence->GetDisplayName()));
		Info.bUseLargeFont = false;
		FSlateNotificationManager::Get().AddNotification(Info);
		return false;
	}

	if (CanAddSubSequence(*Sequence))
	{
		const FScopedTransaction Transaction(LOCTEXT("AddSubScene_Transaction", "Add Subscene"));

		int32 RowIndex = INDEX_NONE;
		AnimatablePropertyChanged(FOnKeyProperty::CreateRaw(this, &FSubTrackEditor::HandleSequenceAdded, Sequence, RowIndex));

		return true;
	}

	FNotificationInfo Info(FText::Format( LOCTEXT("InvalidSequence", "Invalid level sequence {0}. There could be a circular dependency."), Sequence->GetDisplayName()));	
	Info.bUseLargeFont = false;
	FSlateNotificationManager::Get().AddNotification(Info);

	return false;
}

bool FSubTrackEditor::SupportsSequence(UMovieSceneSequence* InSequence) const
{
	return (InSequence != nullptr) && (InSequence->GetClass()->GetName() == TEXT("LevelSequence"));
}

bool FSubTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const
{
	// We support sub movie scenes
	return Type == UMovieSceneSubTrack::StaticClass();
}

const FSlateBrush* FSubTrackEditor::GetIconBrush() const
{
	return FEditorStyle::GetBrush("Sequencer.Tracks.Sub");
}


bool FSubTrackEditor::OnAllowDrop(const FDragDropEvent& DragDropEvent, UMovieSceneTrack* Track, int32 RowIndex, const FGuid& TargetObjectGuid)
{
	if (!Track->IsA(UMovieSceneSubTrack::StaticClass()) || Track->IsA(UMovieSceneCinematicShotTrack::StaticClass()))
	{
		return false;
	}

	TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();

	if (!Operation.IsValid() || !Operation->IsOfType<FAssetDragDropOp>() )
	{
		return false;
	}
	
	TSharedPtr<FAssetDragDropOp> DragDropOp = StaticCastSharedPtr<FAssetDragDropOp>( Operation );

	for (const FAssetData& AssetData : DragDropOp->GetAssets())
	{
		if (Cast<UMovieSceneSequence>(AssetData.GetAsset()))
		{
			return true;
		}
	}

	return false;
}


FReply FSubTrackEditor::OnDrop(const FDragDropEvent& DragDropEvent, UMovieSceneTrack* Track, int32 RowIndex, const FGuid& TargetObjectGuid)
{
	if (!Track->IsA(UMovieSceneSubTrack::StaticClass()) || Track->IsA(UMovieSceneCinematicShotTrack::StaticClass()))
	{
		return FReply::Unhandled();
	}

	TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();

	if (!Operation.IsValid() || !Operation->IsOfType<FAssetDragDropOp>() )
	{
		return FReply::Unhandled();
	}
	
	TSharedPtr<FAssetDragDropOp> DragDropOp = StaticCastSharedPtr<FAssetDragDropOp>( Operation );
	
	bool bAnyDropped = false;
	for (const FAssetData& AssetData : DragDropOp->GetAssets())
	{
		UMovieSceneSequence* Sequence = Cast<UMovieSceneSequence>(AssetData.GetAsset());

		if (Sequence)
		{
			AnimatablePropertyChanged(FOnKeyProperty::CreateRaw(this, &FSubTrackEditor::HandleSequenceAdded, Sequence, RowIndex));

			bAnyDropped = true;
		}
	}

	return bAnyDropped ? FReply::Handled() : FReply::Unhandled();
}

/* FSubTrackEditor callbacks
 *****************************************************************************/

bool FSubTrackEditor::CanAddSubSequence(const UMovieSceneSequence& Sequence) const
{
	// prevent adding ourselves and ensure we have a valid movie scene
	UMovieSceneSequence* FocusedSequence = GetSequencer()->GetFocusedMovieSceneSequence();
	return FSubTrackEditorUtil::CanAddSubSequence(FocusedSequence, Sequence);
}


/* FSubTrackEditor callbacks
 *****************************************************************************/

void FSubTrackEditor::HandleAddSubTrackMenuEntryExecute()
{
	UMovieScene* FocusedMovieScene = GetFocusedMovieScene();

	if (FocusedMovieScene == nullptr)
	{
		return;
	}

	if (FocusedMovieScene->IsReadOnly())
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddSubTrack_Transaction", "Add Sub Track"));
	FocusedMovieScene->Modify();

	auto NewTrack = FocusedMovieScene->AddMasterTrack<UMovieSceneSubTrack>();
	ensure(NewTrack);

	if (GetSequencer().IsValid())
	{
		GetSequencer()->OnAddTrack(NewTrack, FGuid());
	}
}

/** Helper function - get the first PIE world (or first PIE client world if there is more than one) */
static UWorld* GetFirstPIEWorld()
{
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.World()->IsPlayInEditor())
		{
			if(Context.World()->GetNetMode() == ENetMode::NM_Standalone ||
				(Context.World()->GetNetMode() == ENetMode::NM_Client && Context.PIEInstance == 2))
			{
				return Context.World();
			}
		}
	}

	return nullptr;
}

TSharedRef<SWidget> FSubTrackEditor::HandleAddSubSequenceComboButtonGetMenuContent(UMovieSceneTrack* InTrack)
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.BeginSection(TEXT("RecordSequence"), LOCTEXT("RecordSequence", "Record Sequence"));
	{
		AActor* ActorToRecord = nullptr;
		MenuBuilder.AddMenuEntry(
			LOCTEXT("RecordNewSequence", "Record New Sequence"), 
			LOCTEXT("RecordNewSequence_ToolTip", "Record a new level sequence into this sub-track from gameplay/simulation etc.\nThis only primes the track for recording. Click the record button to begin recording into this track once primed.\nOnly one sequence can be recorded at a time."), 
			FSlateIcon(), 
			FUIAction(
				FExecuteAction::CreateSP(this, &FSubTrackEditor::HandleRecordNewSequence, ActorToRecord, InTrack),
				FCanExecuteAction::CreateSP(this, &FSubTrackEditor::CanRecordNewSequence)));

		if(UWorld* PIEWorld = GetFirstPIEWorld())
		{
			APlayerController* Controller = GEngine->GetFirstLocalPlayerController(PIEWorld);
			if(Controller && Controller->GetPawn())
			{
				ActorToRecord = Controller->GetPawn();
				MenuBuilder.AddMenuEntry(
					LOCTEXT("RecordNewSequenceFromPlayer", "Record New Sequence From Current Player"), 
					LOCTEXT("RecordNewSequenceFromPlayer_ToolTip", "Record a new level sequence into this sub track using the current player's pawn.\nThis only primes the track for recording. Click the record button to begin recording into this track once primed.\nOnly one sequence can be recorded at a time."), 
					FSlateIcon(), 
					FUIAction(
						FExecuteAction::CreateSP(this, &FSubTrackEditor::HandleRecordNewSequence, ActorToRecord, InTrack),
						FCanExecuteAction::CreateSP(this, &FSubTrackEditor::CanRecordNewSequence)));
			}
		}
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection(TEXT("ChooseSequence"), LOCTEXT("ChooseSequence", "Choose Sequence"));
	{
		FAssetPickerConfig AssetPickerConfig;
		{
			AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateRaw( this, &FSubTrackEditor::HandleAddSubSequenceComboButtonMenuEntryExecute, InTrack);
			AssetPickerConfig.OnAssetEnterPressed = FOnAssetEnterPressed::CreateRaw( this, &FSubTrackEditor::HandleAddSubSequenceComboButtonMenuEntryEnterPressed, InTrack);
			AssetPickerConfig.bAllowNullSelection = false;
			AssetPickerConfig.InitialAssetViewType = EAssetViewType::Tile;
			AssetPickerConfig.Filter.ClassNames.Add(TEXT("LevelSequence"));
		}

		FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

		TSharedPtr<SBox> MenuEntry = SNew(SBox)
			.WidthOverride(300.0f)
			.HeightOverride(300.f)
			[
				ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
			];

		MenuBuilder.AddWidget(MenuEntry.ToSharedRef(), FText::GetEmpty(), true);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void FSubTrackEditor::HandleAddSubSequenceComboButtonMenuEntryExecute(const FAssetData& AssetData, UMovieSceneTrack* InTrack)
{
	FSlateApplication::Get().DismissAllMenus();

	UObject* SelectedObject = AssetData.GetAsset();

	if (SelectedObject && SelectedObject->IsA(UMovieSceneSequence::StaticClass()))
	{
		UMovieSceneSequence* MovieSceneSequence = CastChecked<UMovieSceneSequence>(AssetData.GetAsset());

		int32 RowIndex = INDEX_NONE;
		AnimatablePropertyChanged( FOnKeyProperty::CreateRaw( this, &FSubTrackEditor::AddKeyInternal, MovieSceneSequence, InTrack, RowIndex) );
	}
}

void FSubTrackEditor::HandleAddSubSequenceComboButtonMenuEntryEnterPressed(const TArray<FAssetData>& AssetData, UMovieSceneTrack* InTrack)
{
	if (AssetData.Num() > 0)
	{
		HandleAddSubSequenceComboButtonMenuEntryExecute(AssetData[0].GetAsset(), InTrack);
	}
}

FKeyPropertyResult FSubTrackEditor::AddKeyInternal(FFrameNumber KeyTime, UMovieSceneSequence* InMovieSceneSequence, UMovieSceneTrack* InTrack, int32 RowIndex)
{	
	FKeyPropertyResult KeyPropertyResult;

	if (InMovieSceneSequence->GetMovieScene()->GetPlaybackRange().IsEmpty())
	{
		FNotificationInfo Info(FText::Format(LOCTEXT("InvalidSequenceDuration", "Invalid level sequence {0}. The sequence has no duration."), InMovieSceneSequence->GetDisplayName()));
		Info.bUseLargeFont = false;
		FSlateNotificationManager::Get().AddNotification(Info);
		return KeyPropertyResult;
	}

	if (CanAddSubSequence(*InMovieSceneSequence))
	{
		UMovieSceneSubTrack* SubTrack = Cast<UMovieSceneSubTrack>(InTrack);

		const FFrameRate TickResolution = InMovieSceneSequence->GetMovieScene()->GetTickResolution();
		const FQualifiedFrameTime InnerDuration = FQualifiedFrameTime(
			MovieScene::DiscreteSize(InMovieSceneSequence->GetMovieScene()->GetPlaybackRange()),
			TickResolution);

		const FFrameRate OuterFrameRate = SubTrack->GetTypedOuter<UMovieScene>()->GetTickResolution();
		const int32      OuterDuration  = InnerDuration.ConvertTo(OuterFrameRate).FrameNumber.Value;

		UMovieSceneSubSection* NewSection = SubTrack->AddSequenceOnRow(InMovieSceneSequence, KeyTime, OuterDuration, RowIndex);
		KeyPropertyResult.bTrackModified = true;

		GetSequencer()->EmptySelection();
		GetSequencer()->SelectSection(NewSection);
		GetSequencer()->ThrobSectionSelection();

		if (TickResolution != OuterFrameRate)
		{
			FNotificationInfo Info(FText::Format(LOCTEXT("TickResolutionMismatch", "The parent sequence has a different tick resolution {0} than the newly added sequence {1}"), OuterFrameRate.ToPrettyText(), TickResolution.ToPrettyText()));
			Info.bUseLargeFont = false;
			FSlateNotificationManager::Get().AddNotification(Info);
		}

		return KeyPropertyResult;
	}

	FNotificationInfo Info(FText::Format( LOCTEXT("InvalidSequence", "Invalid level sequence {0}. There could be a circular dependency."), InMovieSceneSequence->GetDisplayName()));
	Info.bUseLargeFont = false;
	FSlateNotificationManager::Get().AddNotification(Info);

	return KeyPropertyResult;
}

FKeyPropertyResult FSubTrackEditor::HandleSequenceAdded(FFrameNumber KeyTime, UMovieSceneSequence* Sequence, int32 RowIndex)
{
	FKeyPropertyResult KeyPropertyResult;

	auto SubTrack = FindOrCreateMasterTrack<UMovieSceneSubTrack>().Track;

	const FFrameRate TickResolution = Sequence->GetMovieScene()->GetTickResolution();
	const FQualifiedFrameTime InnerDuration = FQualifiedFrameTime(
		MovieScene::DiscreteSize(Sequence->GetMovieScene()->GetPlaybackRange()),
		TickResolution);

	const FFrameRate OuterFrameRate = SubTrack->GetTypedOuter<UMovieScene>()->GetTickResolution();
	const int32      OuterDuration  = InnerDuration.ConvertTo(OuterFrameRate).FrameNumber.Value;

	UMovieSceneSubSection* NewSection = SubTrack->AddSequenceOnRow(Sequence, KeyTime, OuterDuration, RowIndex);
	KeyPropertyResult.bTrackModified = true;

	GetSequencer()->EmptySelection();
	GetSequencer()->SelectSection(NewSection);
	GetSequencer()->ThrobSectionSelection();

	if (TickResolution != OuterFrameRate)
	{
		FNotificationInfo Info(FText::Format(LOCTEXT("TickResolutionMismatch", "The parent sequence has a different tick resolution {0} than the newly added sequence {1}"), OuterFrameRate.ToPrettyText(), TickResolution.ToPrettyText()));
		Info.bUseLargeFont = false;
		FSlateNotificationManager::Get().AddNotification(Info);
	}

	return KeyPropertyResult;
}

bool FSubTrackEditor::CanRecordNewSequence() const
{
	return !UMovieSceneSubSection::IsSetAsRecording();
}

void FSubTrackEditor::HandleRecordNewSequence(AActor* InActorToRecord, UMovieSceneTrack* InTrack)
{
	// Keep track of how many people actually used record new sequence
	if (FEngineAnalytics::IsAvailable())
	{
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Sequencer.RecordNewSequence"));
	}

	FSlateApplication::Get().DismissAllMenus();

	const FScopedTransaction Transaction(LOCTEXT("AddRecordNewSequence_Transaction", "Add Record New Sequence"));

	AnimatablePropertyChanged( FOnKeyProperty::CreateRaw( this, &FSubTrackEditor::HandleRecordNewSequenceInternal, InActorToRecord, InTrack) );
}

FKeyPropertyResult FSubTrackEditor::HandleRecordNewSequenceInternal(FFrameNumber KeyTime, AActor* InActorToRecord, UMovieSceneTrack* InTrack)
{
	FKeyPropertyResult KeyPropertyResult;

	UMovieSceneSubTrack* SubTrack = Cast<UMovieSceneSubTrack>(InTrack);
	UMovieSceneSubSection* Section = SubTrack->AddSequenceToRecord();

	// @todo: we could default to the same directory as a parent sequence, or the last sequence recorded. Lots of options!
	ISequenceRecorder& SequenceRecorder = FModuleManager::LoadModuleChecked<ISequenceRecorder>("SequenceRecorder");

	Section->SetTargetSequenceName(SequenceRecorder.GetSequenceRecordingName());
	Section->SetTargetPathToRecordTo(SequenceRecorder.GetSequenceRecordingBasePath());
	Section->SetActorToRecord(InActorToRecord);
	KeyPropertyResult.bTrackModified = true;

	return KeyPropertyResult;
}

void FSubTrackEditor::SwitchTake(UObject* TakeObject)
{
	bool bSwitchedTake = false;

	const FScopedTransaction Transaction(LOCTEXT("SwitchTake_Transaction", "Switch Take"));

	TArray<UMovieSceneSection*> Sections;
	GetSequencer()->GetSelectedSections(Sections);

	for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
	{
		if (!Sections[SectionIndex]->IsA<UMovieSceneSubSection>())
		{
			continue;
		}

		UMovieSceneSubSection* Section = Cast<UMovieSceneSubSection>(Sections[SectionIndex]);

		if (TakeObject && TakeObject->IsA(UMovieSceneSequence::StaticClass()))
		{
			UMovieSceneSequence* MovieSceneSequence = CastChecked<UMovieSceneSequence>(TakeObject);

			UMovieSceneSubTrack* SubTrack = CastChecked<UMovieSceneSubTrack>(Section->GetOuter());

			TRange<FFrameNumber> NewShotRange         = Section->GetRange();
			FFrameNumber		 NewShotStartOffset   = Section->Parameters.StartFrameOffset;
			float                NewShotTimeScale     = Section->Parameters.TimeScale;
			int32                NewShotPrerollFrames = Section->GetPreRollFrames();
			int32                NewRowIndex          = Section->GetRowIndex();
			FFrameNumber         NewShotStartTime     = NewShotRange.GetLowerBound().IsClosed() ? MovieScene::DiscreteInclusiveLower(NewShotRange) : 0;
			int32                NewShotRowIndex      = Section->GetRowIndex();

			const int32 Duration = (NewShotRange.GetLowerBound().IsClosed() && NewShotRange.GetUpperBound().IsClosed() ) ? MovieScene::DiscreteSize(NewShotRange) : 1;
			UMovieSceneSubSection* NewShot = SubTrack->AddSequence(MovieSceneSequence, NewShotStartTime, Duration);

			if (NewShot != nullptr)
			{
				SubTrack->RemoveSection(*Section);

				NewShot->SetRange(NewShotRange);
				NewShot->Parameters.StartFrameOffset = NewShotStartOffset;
				NewShot->Parameters.TimeScale = NewShotTimeScale;
				NewShot->SetPreRollFrames(NewShotPrerollFrames);
				NewShot->SetRowIndex(NewShotRowIndex);

				bSwitchedTake = true;
			}
		}
	}

	if (bSwitchedTake)
	{
		GetSequencer()->NotifyMovieSceneDataChanged( EMovieSceneDataChangeType::MovieSceneStructureItemsChanged );
	}
}

#undef LOCTEXT_NAMESPACE
