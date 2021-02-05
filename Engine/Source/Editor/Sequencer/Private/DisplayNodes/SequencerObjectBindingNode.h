// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "Widgets/SWidget.h"
#include "DisplayNodes/SequencerDisplayNode.h"
#include "PropertyPath.h"

class FSequencerTrackNode;
class FMenuBuilder;
struct FSlateBrush;

enum class ECheckBoxState : uint8;

/** Enumeration specifying what kind of object binding this is */
enum class EObjectBindingType
{
	Possessable, Spawnable, Unknown
};

/**
 * A node for displaying an object binding
 */
class FSequencerObjectBindingNode
	: public FSequencerDisplayNode
{
public:

	/**
	 * Create and initialize a new instance.
	 * 
	 * @param InNodeName The name identifier of then node.
	 * @param InObjectBinding Object binding guid for associating with live objects.
	 * @param InParentTree The tree this node is in.
	 */
	FSequencerObjectBindingNode(FName InNodeName, const FGuid& InObjectBinding, FSequencerNodeTree& InParentTree);

public:

	/** @return The object binding on this node */
	const FGuid& GetObjectBinding() const
	{
		return ObjectBinding;
	}

	/** Access the cached object binding type for this display node */
	EObjectBindingType GetBindingType() const
	{
		return BindingType;
	}

public:

	// FSequencerDisplayNode interface

	virtual void BuildContextMenu(FMenuBuilder& MenuBuilder) override;
	virtual void BuildOrganizeContextMenu(FMenuBuilder& MenuBuilder) override;
	virtual bool CanRenameNode() const override;
	virtual TSharedRef<SWidget> GetCustomOutlinerContent() override;
	virtual TSharedPtr<SWidget> GetAdditionalOutlinerLabel() override;
	virtual FText GetDisplayName() const override;
	virtual FLinearColor GetDisplayNameColor() const override;
	virtual FText GetDisplayNameToolTipText() const override;
	virtual const FSlateBrush* GetIconBrush() const override;
	virtual const FSlateBrush* GetIconOverlayBrush() const override;
	virtual FText GetIconToolTipText() const override;
	virtual float GetNodeHeight() const override;
	virtual FNodePadding GetNodePadding() const override;
	virtual ESequencerNode::Type GetType() const override;
	virtual void SetDisplayName(const FText& NewDisplayName) override;
	virtual bool CanDrag() const override;
	virtual TOptional<EItemDropZone> CanDrop(FSequencerDisplayNodeDragDropOp& DragDropOp, EItemDropZone ItemDropZone) const override;
	virtual void Drop(const TArray<TSharedRef<FSequencerDisplayNode>>& DraggedNodes, EItemDropZone ItemDropZone) override;
	virtual int32 GetSortingOrder() const override;
	virtual void SetSortingOrder(const int32 InSortingOrder) override;
	virtual void ModifyAndSetSortingOrder(const int32 InSortingOrder) override;

protected:

	void AddPropertyMenuItems(FMenuBuilder& AddTrackMenuBuilder, TArray<FPropertyPath> KeyableProperties, int32 PropertyNameIndexStart = 0, int32 PropertyNameIndexEnd = -1);

	void AddSpawnOwnershipMenu(FMenuBuilder& MenuBuilder);
	void AddSpawnLevelMenu(FMenuBuilder& MenuBuilder);
	void AddAssignActorMenu(FMenuBuilder& MenuBuilder);
	void AddTagMenu(FMenuBuilder& MenuBuilder);

	/** Get class for object binding */
	const UClass* GetClassForObjectBinding() const;

private:
	
	TSharedRef<SWidget> HandleAddTrackComboButtonGetMenuContent();
	
	void HandleAddTrackSubMenuNew(FMenuBuilder& AddTrackMenuBuilder, TArray<FPropertyPath> KeyablePropertyPath, int32 PropertyNameIndexStart = 0);

	void HandlePropertyMenuItemExecute(FPropertyPath PropertyPath);

	ECheckBoxState GetTagCheckState(FName TagName);

	void ToggleTag(FName TagName);

	void HandleDeleteTag(FName TagName);

	void HandleAddTag(FName TagName);

private:

	/** The binding to live objects */
	FGuid ObjectBinding;

	/** Enumeration specifying what kind of object binding this is */
	EObjectBindingType BindingType;
};
