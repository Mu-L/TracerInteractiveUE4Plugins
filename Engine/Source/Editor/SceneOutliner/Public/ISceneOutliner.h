// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "SceneOutlinerFwd.h"
#include "Widgets/SCompoundWidget.h"

template<typename ItemType> class STreeView;

/**
 * The public interface for the Scene Outliner widget
 */
class ISceneOutliner : public SCompoundWidget
{
public:

	/** Sends a requests to the Scene Outliner to refresh itself the next chance it gets */
	virtual void Refresh() = 0;

	/** @return Returns a string to use for highlighting results in the outliner list */
	virtual TAttribute<FText> GetFilterHighlightText() const = 0;

	/** @return Returns the common data for this outliner */
	virtual const SceneOutliner::FSharedOutlinerData& GetSharedData() const = 0;

	/** Get a const reference to the actual tree hierarchy */
	virtual const STreeView<SceneOutliner::FTreeItemPtr>& GetTree() const = 0;

	/** Set the keyboard focus to the outliner */
	virtual void SetKeyboardFocus() = 0;

	/** Gets the cached icon for this class name */
	virtual FSlateBrush* GetCachedIconForClass(FName InClassName) const = 0;

	/** Sets the cached icon for this class name */
	virtual void CacheIconForClass(FName InClassName, FSlateBrush* InSlateBrush) = 0;
};
