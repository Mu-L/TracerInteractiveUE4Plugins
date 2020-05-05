// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "Widgets/SWidget.h"
#include "DisplayNodes/SequencerDisplayNode.h"
#include "DisplayNodes/SequencerSectionKeyAreaNode.h"
#include "ISequencerSection.h"
#include "IKeyArea.h"
#include "SequencerHotspots.h"
#include "SectionHandle.h"

class FMenuBuilder;
class ISequencerTrackEditor;
class UMovieSceneTrack;
struct FSlateBrush;

struct FSequencerOverlapRange
{
	/** The range for the overlap */
	TRange<FFrameNumber> Range;
	/** The sections that occupy this range, sorted by overlap priority */
	TArray<FSectionHandle> Sections;
};

/**
 * Represents an area to display Sequencer sections (possibly on multiple lines).
 */
class FSequencerTrackNode
	: public FSequencerDisplayNode
{
public:

	/**
	 * Create and initialize a new instance.
	 * 
	 * @param InAssociatedType The track that this node represents.
	 * @param InAssociatedEditor The track editor for the track that this node represents.
	 * @param bInCanBeDragged Whether or not this node can be dragged and dropped.
	 * @param InParentTree The tree this node is in.
	 */
	FSequencerTrackNode(UMovieSceneTrack* InAssociatedTrack, ISequencerTrackEditor& InAssociatedEditor, bool bInCanBeDragged, FSequencerNodeTree& InParentTree);

public:
	/** Defines interaction modes when using sub-tracks for sections on multiple rows. */
	enum class ESubTrackMode
	{
		/** This track node isn't part of a sub-track set. */
		None,
		/** This track node is the parent and has child sub tracks. */
		ParentTrack,
		/** This track node is a sub-track of another track node. */
		SubTrack
	};

public:

	/**
	 * Ensure this track's inner hierarchy is up to date, and that this track has the correct sub track mode initialized
	 */
	void UpdateInnerHierarchy();

	/**
	 * Ensure that the section pointers for this track node are all correct based on its sub track mode and row index
	 */
	void UpdateSections();

	/**
	 * @return All sections in this node
	 */
	const TArray<TSharedRef<ISequencerSection>>& GetSections() const
	{
		return Sections;
	}

	TArray<TSharedRef<ISequencerSection>>& GetSections()
	{
		return Sections;
	}

	void SetTopLevelKeyNode(TSharedPtr<FSequencerSectionKeyAreaNode> InTopLevelKeyNode)
	{
		TopLevelKeyNode = InTopLevelKeyNode;
	}

	/** @return Returns the top level key node for the section area if it exists */
	TSharedPtr<FSequencerSectionKeyAreaNode> GetTopLevelKeyNode() const
	{
		return TopLevelKeyNode;
	}

	/** @return the track associated with this section */
	UMovieSceneTrack* GetTrack() const
	{
		return AssociatedTrack.Get();
	}

	/** Gets the track editor associated with this track node. */
	ISequencerTrackEditor& GetTrackEditor() const
	{
		return AssociatedEditor;
	}

	/** Gets the sub track mode for this track node, used when the track supports multiple rows. */
	ESubTrackMode GetSubTrackMode() const;

	/** Sets the sub track mode for this track node, used when the track supports multiple rows. */
	void SetSubTrackMode(ESubTrackMode InSubTrackMode);

	/** Gets the row index for this track node.  This is only relevant when this track node is a sub-track node. */
	int32 GetRowIndex() const;

	/**  Gets the row index for this track node when this track node is a sub-track. */
	void SetRowIndex(int32 InRowIndex);

	/** Gets an array of sections that underlap the specified section */
	TArray<FSequencerOverlapRange> GetUnderlappingSections(UMovieSceneSection* InSection);

	/** Gets an array of sections whose easing bounds underlap the specified section */
	TArray<FSequencerOverlapRange> GetEasingSegmentsForSection(UMovieSceneSection* InSection);

public:

	// FSequencerDisplayNode interface

	virtual void BuildContextMenu( FMenuBuilder& MenuBuilder );
	virtual bool CanRenameNode() const override;
	virtual bool ValidateDisplayName(const FText& NewDisplayName, FText& OutErrorMessage) const override;
	virtual TSharedRef<SWidget> GetCustomOutlinerContent() override;
	virtual void GetChildKeyAreaNodesRecursively(TArray<TSharedRef<FSequencerSectionKeyAreaNode>>& OutNodes) const override;
	virtual FText GetDisplayName() const override;
	virtual FLinearColor GetDisplayNameColor() const override;
	virtual FSlateFontInfo GetDisplayNameFont() const override;
	virtual float GetNodeHeight() const override;
	virtual FNodePadding GetNodePadding() const override;
	virtual ESequencerNode::Type GetType() const override;
	virtual void SetDisplayName(const FText& NewDisplayName) override;
	virtual const FSlateBrush* GetIconBrush() const override;
	virtual bool CanDrag() const override;
	virtual TOptional<EItemDropZone> CanDrop(FSequencerDisplayNodeDragDropOp& DragDropOp, EItemDropZone ItemDropZone) const override;
	virtual void Drop(const TArray<TSharedRef<FSequencerDisplayNode>>& DraggedNodes, EItemDropZone ItemDropZone) override;
	virtual bool IsResizable() const override;
	virtual void Resize(float NewSize) override;
	virtual int32 GetSortingOrder() const override;
	virtual void SetSortingOrder(const int32 InSortingOrder) override;
	virtual void ModifyAndSetSortingOrder(const int32 InSortingOrder) override;

	// ICurveEditorTreeItem interface
	virtual void CreateCurveModels(TArray<TUniquePtr<FCurveModel>>& OutCurveModels) override;

private:

	FReply CreateNewSection() const;

	void ClearChildren();

	void RemoveStaleChildren();

private:

	/** The track editor for the track associated with this node. */
	ISequencerTrackEditor& AssociatedEditor;

	/** The type associated with the sections in this node */
	TWeakObjectPtr<UMovieSceneTrack> AssociatedTrack;

	/** All of the sequencer sections in this node */
	TArray<TSharedRef<ISequencerSection>> Sections;

	/** If the section area is a key area itself, this represents the node for the keys */
	TSharedPtr<FSequencerSectionKeyAreaNode> TopLevelKeyNode;

	/** Whether or not this track node can be dragged. */
	bool bCanBeDragged;

	/** The current sub-track mode this node is using. */
	ESubTrackMode SubTrackMode;

	/** The row index when this track node is a sub-track node. */
	int32 RowIndex;
};
