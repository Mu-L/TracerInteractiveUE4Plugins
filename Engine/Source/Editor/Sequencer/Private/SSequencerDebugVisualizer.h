// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "Layout/Visibility.h"
#include "Layout/Geometry.h"
#include "Widgets/SWidget.h"
#include "Layout/Children.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SPanel.h"
#include "MovieSceneSequenceID.h"
#include "Sequencer.h"

class FArrangedChildren;
struct FTimeToPixel;

class SSequencerDebugSlot : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SSequencerDebugSlot){}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, int32 InSegmentIndex)
	{
		SegmentIndex = InSegmentIndex;

		ChildSlot
		[
			InArgs._Content.Widget
		];
	}

	int32 GetSegmentIndex() const { return SegmentIndex; }

private:
	int32 SegmentIndex;
};

class SSequencerDebugVisualizer : public SPanel
{
public:
	SLATE_BEGIN_ARGS(SSequencerDebugVisualizer){}
		SLATE_ATTRIBUTE( TRange<double>, ViewRange )
	SLATE_END_ARGS()

	SSequencerDebugVisualizer()
		: Children(this)
	{}

	void Construct(const FArguments& InArgs, TSharedRef<FSequencer> InSequencer);

protected:
	/** SPanel Interface */
	virtual void OnArrangeChildren( const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren ) const override;
	virtual void Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime ) override;
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual FChildren* GetChildren() override { return &Children; }

protected:

	void RequestRefresh();

	void Refresh();

	FGeometry GetSegmentGeometry(const FGeometry& AllottedGeometry, const SSequencerDebugSlot& Slot, const FTimeToPixel& TimeToPixelConverter) const;

	EVisibility GetSegmentVisibility(TRange<double> Range) const;

	TSharedRef<SWidget> GetTooltipForSegment(int32 SegmentIndex) const;

	void OnSequenceActivated(FMovieSceneSequenceIDRef);

	const FMovieSceneEvaluationTemplate* GetTemplate() const;

private:

	/** The current view range */
	TAttribute<TRange<double>> ViewRange;

	/** All the widgets in the panel */
	TSlotlessChildren<SSequencerDebugSlot> Children;

	TWeakPtr<FSequencer> WeakSequencer;

	FGuid CachedSignature;
};
