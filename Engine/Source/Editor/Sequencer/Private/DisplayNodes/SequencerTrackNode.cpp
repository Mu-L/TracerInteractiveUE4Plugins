// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "DisplayNodes/SequencerTrackNode.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SBox.h"
#include "DisplayNodes/SequencerObjectBindingNode.h"
#include "Framework/Commands/UIAction.h"
#include "Textures/SlateIcon.h"
#include "UObject/UnrealType.h"
#include "MovieSceneTrack.h"
#include "SSequencer.h"
#include "MovieSceneNameableTrack.h"
#include "ISequencerTrackEditor.h"
#include "ScopedTransaction.h"
#include "SequencerUtilities.h"
#include "SKeyNavigationButtons.h"
#include "Compilation/MovieSceneSegmentCompiler.h"

#define LOCTEXT_NAMESPACE "SequencerTrackNode"

namespace SequencerNodeConstants
{
	extern const float CommonPadding;
}


/* FTrackNode structors
 *****************************************************************************/

FSequencerTrackNode::FSequencerTrackNode(UMovieSceneTrack& InAssociatedTrack, ISequencerTrackEditor& InAssociatedEditor, bool bInCanBeDragged, TSharedPtr<FSequencerDisplayNode> InParentNode, FSequencerNodeTree& InParentTree)
	: FSequencerDisplayNode(InAssociatedTrack.GetTrackName(), InParentNode, InParentTree)
	, AssociatedEditor(InAssociatedEditor)
	, AssociatedTrack(&InAssociatedTrack)
	, bCanBeDragged(bInCanBeDragged)
	, SubTrackMode(ESubTrackMode::None)
{ }


/* FTrackNode interface
 *****************************************************************************/

void FSequencerTrackNode::SetSectionAsKeyArea(TSharedRef<IKeyArea>& KeyArea)
{
	if( !TopLevelKeyNode.IsValid() )
	{
		bool bTopLevel = true;
		TopLevelKeyNode = MakeShareable( new FSequencerSectionKeyAreaNode( GetNodeName(), FText::GetEmpty(), nullptr, ParentTree, bTopLevel ) );
	}

	TopLevelKeyNode->AddKeyArea( KeyArea );
}

void FSequencerTrackNode::AddKey(const FGuid& ObjectGuid)
{
	AssociatedEditor.AddKey(ObjectGuid);
}

FSequencerTrackNode::ESubTrackMode FSequencerTrackNode::GetSubTrackMode() const
{
	return SubTrackMode;
}

void FSequencerTrackNode::SetSubTrackMode(FSequencerTrackNode::ESubTrackMode InSubTrackMode)
{
	SubTrackMode = InSubTrackMode;
}

int32 FSequencerTrackNode::GetRowIndex() const
{
	return RowIndex;
}

void FSequencerTrackNode::SetRowIndex(int32 InRowIndex)
{
	RowIndex = InRowIndex;
	NodeName.SetNumber(RowIndex);
}


/* FSequencerDisplayNode interface
 *****************************************************************************/

void FSequencerTrackNode::BuildContextMenu(FMenuBuilder& MenuBuilder)
{
	AssociatedEditor.BuildTrackContextMenu(MenuBuilder, AssociatedTrack.Get());
	UMovieSceneTrack* Track = AssociatedTrack.Get();
	if (Track && Track->GetSupportedBlendTypes().Num() > 0)
	{
		int32 NewRowIndex = SubTrackMode == ESubTrackMode::SubTrack ? GetRowIndex() : Track->GetMaxRowIndex() + 1;
		TWeakPtr<ISequencer> WeakSequencer = GetSequencer().AsShared();

		MenuBuilder.AddSubMenu(
			LOCTEXT("AddSection", "Add Section"),
			FText(),
			FNewMenuDelegate::CreateLambda([=](FMenuBuilder& SubMenuBuilder){
				FSequencerUtilities::PopulateMenu_CreateNewSection(SubMenuBuilder, NewRowIndex, Track, WeakSequencer);
			})
		);
		
	}
	FSequencerDisplayNode::BuildContextMenu(MenuBuilder );
}


bool FSequencerTrackNode::CanRenameNode() const
{
	auto NameableTrack = Cast<UMovieSceneNameableTrack>(AssociatedTrack.Get());

	if (NameableTrack != nullptr)
	{
		return NameableTrack->CanRename();
	}
	return false;
}


FReply FSequencerTrackNode::CreateNewSection() const
{
	UMovieSceneTrack* Track = GetTrack();
	if (!Track)
	{
		return FReply::Handled();
	}

	const int32 InsertAtIndex = SubTrackMode == ESubTrackMode::SubTrack ? GetRowIndex() : Track->GetMaxRowIndex() + 1;
	const float StartAtTime = GetSequencer().GetLocalTime();

	FScopedTransaction Transaction(LOCTEXT("AddSectionText", "Add Section"));
	UMovieSceneSection* Section = Track->CreateNewSection();
	if (Section)
	{
		Track->Modify();

		Section->SetIsInfinite(false);
		Section->SetStartTime(StartAtTime);
		Section->SetEndTime(StartAtTime + 10.f);
		Section->SetRowIndex(InsertAtIndex);

		Track->AddSection(*Section);

		GetSequencer().NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
	}
	else
	{
		Transaction.Cancel();
	}
	return FReply::Handled();
}


TSharedRef<SWidget> FSequencerTrackNode::GetCustomOutlinerContent()
{
	TSharedPtr<FSequencerSectionKeyAreaNode> KeyAreaNode = GetTopLevelKeyNode();
	if (KeyAreaNode.IsValid())
	{
		return KeyAreaNode->GetCustomOutlinerContent();
	}

	TAttribute<bool> NodeIsHovered = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &FSequencerDisplayNode::IsHovered));

	TSharedRef<SHorizontalBox> BoxPanel = SNew(SHorizontalBox);

	FGuid ObjectBinding;
	TSharedPtr<FSequencerDisplayNode> ParentSeqNode = GetParent();

	if (ParentSeqNode.IsValid() && (ParentSeqNode->GetType() == ESequencerNode::Object))
	{
		ObjectBinding = StaticCastSharedPtr<FSequencerObjectBindingNode>(ParentSeqNode)->GetObjectBinding();
	}

	UMovieSceneTrack* Track = AssociatedTrack.Get();

	FBuildEditWidgetParams Params;
	Params.NodeIsHovered = NodeIsHovered;
	if (SubTrackMode == ESubTrackMode::SubTrack)
	{
		Params.TrackInsertRowIndex = GetRowIndex();
	}
	else if (Track->SupportsMultipleRows())
	{
		Params.TrackInsertRowIndex = Track->GetMaxRowIndex()+1;
	}

	TSharedPtr<SWidget> Widget = GetSequencer().IsReadOnly() ? SNullWidget::NullWidget : AssociatedEditor.BuildOutlinerEditWidget(ObjectBinding, Track, Params);

	bool bHasKeyableAreas = false;

	TArray<TSharedRef<FSequencerSectionKeyAreaNode>> ChildKeyAreaNodes;
	FSequencerDisplayNode::GetChildKeyAreaNodesRecursively(ChildKeyAreaNodes);
	for (int32 ChildIndex = 0; ChildIndex < ChildKeyAreaNodes.Num() && !bHasKeyableAreas; ++ChildIndex)
	{
		TArray< TSharedRef<IKeyArea> > ChildKeyAreas = ChildKeyAreaNodes[ChildIndex]->GetAllKeyAreas();

		for (int32 ChildKeyAreaIndex = 0; ChildKeyAreaIndex < ChildKeyAreas.Num() && !bHasKeyableAreas; ++ChildKeyAreaIndex)
		{
			if (ChildKeyAreas[ChildKeyAreaIndex]->CanCreateKeyEditor())
			{
				bHasKeyableAreas = true;
			}
		}
	}

	if (Widget.IsValid())
	{
		BoxPanel->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Right)
		[
			Widget.ToSharedRef()
		];
	}

	if (bHasKeyableAreas)
	{
		BoxPanel->AddSlot()
		.VAlign(VAlign_Center)
		[
			SNew(SKeyNavigationButtons, AsShared())
		];
	}

	return SNew(SBox)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Right)
		[
			BoxPanel
		];
}


const FSlateBrush* FSequencerTrackNode::GetIconBrush() const
{
	return AssociatedEditor.GetIconBrush();
}


bool FSequencerTrackNode::CanDrag() const
{
	return bCanBeDragged;
}

bool FSequencerTrackNode::IsResizable() const
{
	UMovieSceneTrack* Track = GetTrack();
	return Track && AssociatedEditor.IsResizable(Track);
}

void FSequencerTrackNode::Resize(float NewSize)
{
	UMovieSceneTrack* Track = GetTrack();

	float PaddingAmount = 2 * SequencerNodeConstants::CommonPadding;
	if (Track && Sections.Num())
	{
		PaddingAmount *= (Track->GetMaxRowIndex() + 1);
	}
	
	NewSize -= PaddingAmount;

	if (Track && AssociatedEditor.IsResizable(Track))
	{
		AssociatedEditor.Resize(NewSize, Track);
	}
}

void FSequencerTrackNode::GetChildKeyAreaNodesRecursively(TArray<TSharedRef<FSequencerSectionKeyAreaNode>>& OutNodes) const
{
	FSequencerDisplayNode::GetChildKeyAreaNodesRecursively(OutNodes);

	if (TopLevelKeyNode.IsValid())
	{
		OutNodes.Add(TopLevelKeyNode.ToSharedRef());
	}
}


FText FSequencerTrackNode::GetDisplayName() const
{
	return AssociatedTrack.IsValid() ? AssociatedTrack->GetDisplayName() : FText::GetEmpty();
}


float FSequencerTrackNode::GetNodeHeight() const
{
	float SectionHeight = Sections.Num() > 0
		? Sections[0]->GetSectionHeight()
		: SequencerLayoutConstants::SectionAreaDefaultHeight;
	float PaddedSectionHeight = SectionHeight + (2 * SequencerNodeConstants::CommonPadding);

	if (SubTrackMode == ESubTrackMode::None && AssociatedTrack.IsValid())
	{
		return PaddedSectionHeight * (AssociatedTrack->GetMaxRowIndex() + 1);
	}
	else
	{
		return PaddedSectionHeight;
	}
}


FNodePadding FSequencerTrackNode::GetNodePadding() const
{
	return FNodePadding(0.f);
}


ESequencerNode::Type FSequencerTrackNode::GetType() const
{
	return ESequencerNode::Track;
}


void FSequencerTrackNode::SetDisplayName(const FText& NewDisplayName)
{
	auto NameableTrack = Cast<UMovieSceneNameableTrack>(AssociatedTrack.Get());

	if (NameableTrack != nullptr)
	{
		NameableTrack->SetDisplayName(NewDisplayName);
		GetSequencer().NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
	}
}

TArray<FSequencerOverlapRange> FSequencerTrackNode::GetUnderlappingSections(UMovieSceneSection* InSection)
{
	TRange<float> InSectionRange = InSection->IsInfinite() ? TRange<float>::All() : InSection->GetRange();

	TMovieSceneEvaluationTree<int32> SectionIndexTree;

	// Iterate all other sections on the same row with <= overlap priority
	for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
	{
		UMovieSceneSection* SectionObj = Sections[SectionIndex]->GetSectionObject();
		if (!SectionObj || SectionObj == InSection || SectionObj->GetRowIndex() != InSection->GetRowIndex() || SectionObj->GetOverlapPriority() > InSection->GetOverlapPriority())
		{
			continue;
		}

		TRange<float> OtherSectionRange = SectionObj->IsInfinite() ? TRange<float>::All() : SectionObj->GetRange();
		TRange<float> Intersection = TRange<float>::Intersection(OtherSectionRange, InSectionRange);
		if (!Intersection.IsEmpty())
		{
			SectionIndexTree.Add(Intersection, SectionIndex);
		}
	}

	TSharedRef<FSequencerTrackNode> TrackNode = SharedThis(this);;

	TArray<FSequencerOverlapRange> Result;
	for (FMovieSceneEvaluationTreeRangeIterator It(SectionIndexTree); It; ++It)
	{
		FSequencerOverlapRange NewRange;

		NewRange.Range = It.Range();

		for (int32 SectionIndex : SectionIndexTree.GetAllData(It.Node()))
		{
			NewRange.Sections.Add(FSectionHandle(TrackNode, SectionIndex));
		}

		if (!NewRange.Sections.Num())
		{
			continue;
		}

		// Sort lowest to highest
		NewRange.Sections.Sort([](const FSectionHandle& A, const FSectionHandle& B){
			return A.GetSectionObject()->GetOverlapPriority() < B.GetSectionObject()->GetOverlapPriority();
		});

		Result.Add(MoveTemp(NewRange));
	}

	return Result;
}

TArray<FSequencerOverlapRange> FSequencerTrackNode::GetEasingSegmentsForSection(UMovieSceneSection* InSection)
{
	TRange<float> InSectionRange = InSection->IsInfinite() ? TRange<float>::All() : InSection->GetRange();

	TArray<FMovieSceneSectionData> CompileData;

	TMovieSceneEvaluationTree<int32> SectionIndexTree;

	// Iterate all active sections on the same row with <= overlap priority
	for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
	{
		UMovieSceneSection* SectionObj = Sections[SectionIndex]->GetSectionObject();
		if (!SectionObj || !SectionObj->IsActive() || SectionObj->GetRowIndex() != InSection->GetRowIndex() || SectionObj->GetOverlapPriority() > InSection->GetOverlapPriority())
		{
			continue;
		}

		TRange<float> Intersection = TRange<float>::Intersection(SectionObj->GetEaseInRange(), InSectionRange);
		if (!Intersection.IsEmpty())
		{
			SectionIndexTree.Add(Intersection, SectionIndex);
		}

		Intersection = TRange<float>::Intersection(SectionObj->GetEaseOutRange(), InSectionRange);
		if (!Intersection.IsEmpty())
		{
			SectionIndexTree.Add(Intersection, SectionIndex);
		}
	}

	TSharedRef<FSequencerTrackNode> TrackNode = SharedThis(this);;

	TArray<FSequencerOverlapRange> Result;
	for (FMovieSceneEvaluationTreeRangeIterator It(SectionIndexTree); It; ++It)
	{
		FSequencerOverlapRange NewRange;

		NewRange.Range = It.Range();

		for (int32 SectionIndex : SectionIndexTree.GetAllData(It.Node()))
		{
			NewRange.Sections.Add(FSectionHandle(TrackNode, SectionIndex));
		}

		if (!NewRange.Sections.Num())
		{
			continue;
		}

		// Sort lowest to highest
		NewRange.Sections.Sort([=](const FSectionHandle& A, const FSectionHandle& B){
			return A.GetSectionObject()->GetOverlapPriority() < B.GetSectionObject()->GetOverlapPriority();
		});

		Result.Add(MoveTemp(NewRange));
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE