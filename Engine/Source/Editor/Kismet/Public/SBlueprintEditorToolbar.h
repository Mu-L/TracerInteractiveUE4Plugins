// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "Widgets/SWidget.h"
#include "Textures/SlateIcon.h"
#include "EditorStyleSet.h"
#include "Framework/Commands/Commands.h"
#include "WorkflowOrientedApp/SModeWidget.h"

class FBlueprintEditor;
class FExtender;
class FMenuBuilder;
class FToolBarBuilder;
class UToolMenu;
struct FToolMenuContext;

/**
 * Kismet menu
 */
class KISMET_API FKismet2Menu
{
public:
	static void SetupBlueprintEditorMenu(const FName MainMenuName);
	
protected:
	static void FillFileMenuBlueprintSection(UToolMenu* Menu);

	static void FillEditMenu(UToolMenu* Menu);

	static void FillViewMenu(UToolMenu* Menu);

	static void FillDebugMenu(UToolMenu* Menu);

	static void FillDeveloperMenu(UToolMenu* Menu);

private:
	/** Diff current blueprint against the specified revision */
	static void DiffAgainstRevision( class UBlueprint* Current, int32 OldRevision );

	static TSharedRef<SWidget> MakeDiffMenu(const FToolMenuContext& ToolMenuContext);
};


class FFullBlueprintEditorCommands : public TCommands<FFullBlueprintEditorCommands>
{
public:
	/** Constructor */
	FFullBlueprintEditorCommands() 
		: TCommands<FFullBlueprintEditorCommands>("FullBlueprintEditor", NSLOCTEXT("Contexts", "FullBlueprintEditor", "Full Blueprint Editor"), NAME_None, FEditorStyle::GetStyleSetName())
	{
	}

	/** Compile the blueprint */
	TSharedPtr<FUICommandInfo> Compile;
	TSharedPtr<FUICommandInfo> SaveOnCompile_Never;
	TSharedPtr<FUICommandInfo> SaveOnCompile_SuccessOnly;
	TSharedPtr<FUICommandInfo> SaveOnCompile_Always;
	TSharedPtr<FUICommandInfo> JumpToErrorNode;

	/** Switch between modes in the blueprint editor */
	TSharedPtr<FUICommandInfo> SwitchToScriptingMode;
	TSharedPtr<FUICommandInfo> SwitchToBlueprintDefaultsMode;
	TSharedPtr<FUICommandInfo> SwitchToComponentsMode;
	
	/** Edit Blueprint global options */
	TSharedPtr<FUICommandInfo> EditGlobalOptions;
	TSharedPtr<FUICommandInfo> EditClassDefaults;

	/** Initialize commands */
	virtual void RegisterCommands() override;
};



class KISMET_API FBlueprintEditorToolbar : public TSharedFromThis<FBlueprintEditorToolbar>
{
public:
	FBlueprintEditorToolbar(TSharedPtr<FBlueprintEditor> InBlueprintEditor)
		: BlueprintEditor(InBlueprintEditor) {}

	void AddBlueprintGlobalOptionsToolbar(UToolMenu* InMenu, bool bRegisterViewport = false);
	void AddCompileToolbar(UToolMenu* InMenu);
	void AddNewToolbar(UToolMenu* InMenu);
	void AddScriptingToolbar(UToolMenu* InMenu);
	void AddDebuggingToolbar(UToolMenu* InMenu);

	/** Returns the current status icon for the blueprint being edited */
	FSlateIcon GetStatusImage() const;

	/** Returns the current status as text for the blueprint being edited */
	FText GetStatusTooltip() const;

protected:
	/** Pointer back to the blueprint editor tool that owns us */
	TWeakPtr<FBlueprintEditor> BlueprintEditor;
};

