// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FSequencerSectionKeyAreaNode;
class SOverlay;
class IKeyArea;

class SKeyAreaEditorSwitcher : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SKeyAreaEditorSwitcher){}
	SLATE_END_ARGS()

	/** Construct the widget */
	void Construct(const FArguments& InArgs, TSharedRef<FSequencerSectionKeyAreaNode> InKeyAreaNode);

	/** Rebuild this widget from its cached key area node */
	void Rebuild();

private:

	/** Tick this widget. Updates the currently visible key editor */
	virtual void Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime ) override;

	int32 GetWidgetIndex() const;

private:

	/** Our overlay widget */
	TSharedPtr<SOverlay> Overlay;
	/** Index of the currently visible key editor */
	int32 VisibleIndex;
	/** The key area to which we relate */
	TWeakPtr<FSequencerSectionKeyAreaNode> WeakKeyAreaNode;
	/** Key areas cached from the node */
	TArray<TSharedRef<IKeyArea>> CachedKeyAreas;
};
