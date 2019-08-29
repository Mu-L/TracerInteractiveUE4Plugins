// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "EditorStyleSet.h"

class FPaintModeCommands : public TCommands<FPaintModeCommands>
{
public:
	FPaintModeCommands() : TCommands<FPaintModeCommands> ( "PaintModeCommands", NSLOCTEXT("PaintMode", "CommandsName", "Vertex Painter"), NAME_None, FEditorStyle::GetStyleSetName()) {}

	/**
	* Initialize commands
	*/
	virtual void RegisterCommands() override;

public:
	TSharedPtr<FUICommandInfo> NextTexture;
	TSharedPtr<FUICommandInfo> PreviousTexture;
	TSharedPtr<FUICommandInfo> CommitTexturePainting;
	
	TSharedPtr<FUICommandInfo> Copy;
	TSharedPtr<FUICommandInfo> Paste;
	TSharedPtr<FUICommandInfo> Remove;
	TSharedPtr<FUICommandInfo> Fix;
	TSharedPtr<FUICommandInfo> Fill;
	TSharedPtr<FUICommandInfo> Propagate;
	TSharedPtr<FUICommandInfo> Import;
	TSharedPtr<FUICommandInfo> Save;

	TSharedPtr<FUICommandInfo> SwitchForeAndBackgroundColor;
	TSharedPtr<FUICommandInfo> CycleToNextLOD;
	TSharedPtr<FUICommandInfo> CycleToPreviousLOD;

	TSharedPtr<FUICommandInfo> PropagateTexturePaint;
	TSharedPtr<FUICommandInfo> SaveTexturePaint;

	TSharedPtr<FUICommandInfo> PropagateVertexColorsToLODs;

	TArray<TSharedPtr<FUICommandInfo>> Commands;
};