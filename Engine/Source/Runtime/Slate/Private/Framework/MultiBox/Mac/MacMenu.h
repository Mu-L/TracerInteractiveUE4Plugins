// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MultiBox.h"
#include "SMenuEntryBlock.h"
#include "CocoaMenu.h"

@interface FMacMenu : FCocoaMenu <NSMenuDelegate>
@property (assign) TWeakPtr<const FMenuEntryBlock> MenuEntryBlock;
@property (assign) TWeakPtr<const FMultiBox> MultiBox;
@end

class SLATE_API FSlateMacMenu
{
public:

	static void CleanupOnShutdown();
	static void UpdateWithMultiBox(const TSharedPtr<FMultiBox> MultiBox);
	static void UpdateMenu(FMacMenu* Menu);
	static void UpdateCachedState();
	static void ExecuteMenuItemAction(const TSharedRef<const FMenuEntryBlock>& Block);

private:

	static NSString* GetMenuItemTitle(const TSharedRef< const FMenuEntryBlock >& Block);
	static NSImage* GetMenuItemIcon(const TSharedRef<const FMenuEntryBlock>& Block);
	static NSString* GetMenuItemKeyEquivalent(const TSharedRef<const FMenuEntryBlock>& Block, uint32* OutModifiers);
	static bool IsMenuItemEnabled(const TSharedRef<const FMenuEntryBlock>& Block);
	static int32 GetMenuItemState(const TSharedRef<const FMenuEntryBlock>& Block);
};
