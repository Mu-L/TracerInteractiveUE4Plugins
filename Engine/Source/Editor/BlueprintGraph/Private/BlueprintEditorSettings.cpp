// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintEditorSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "Editor/EditorPerProjectUserSettings.h"
#include "Settings/EditorExperimentalSettings.h"
#include "BlueprintActionDatabase.h"
#include "FindInBlueprintManager.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"

UBlueprintEditorSettings::UBlueprintEditorSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	// Style Settings
	, bDrawMidpointArrowsInBlueprints(false)
	, bShowGraphInstructionText(true)
	, bHideUnrelatedNodes(false)
	, bShowShortTooltips(true)
	// Workflow Settings
	, bSplitContextTargetSettings(true)
	, bExposeAllMemberComponentFunctions(true)
	, bShowContextualFavorites(false)
	, bExposeDeprecatedFunctions(false)
	, bCompactCallOnMemberNodes(false)
	, bFlattenFavoritesMenus(true)
	, bFavorPureCastNodes(false)
	, bAutoCastObjectConnections(false)
	, bShowViewportOnSimulate(false)
	, bShowInheritedVariables(false)
	, bAlwaysShowInterfacesInOverrides(true)
	, bShowParentClassInOverrides(true)
	, bShowEmptySections(true)
	, bShowAccessSpecifier(false)
	, bSpawnDefaultBlueprintNodes(true)
	, bHideConstructionScriptComponentsInDetailsView(true)
	, bHostFindInBlueprintsInGlobalTab(true)
	, bNavigateToNativeFunctionsFromCallNodes(true)
	, bIncludeCommentNodesInBookmarksTab(true)
	, bShowBookmarksForCurrentDocumentOnlyInTab(false)
	// Compiler Settings
	, SaveOnCompile(SoC_Never)
	, bJumpToNodeErrors(false)
	, bAllowExplicitImpureNodeDisabling(false)
	// Developer Settings
	, bShowActionMenuItemSignatures(false)
	// Perf Settings
	, bShowDetailedCompileResults(false)
	, CompileEventDisplayThresholdMs(5)
	, NodeTemplateCacheCapMB(20.f)
{
	// settings that were moved out of experimental...
	UEditorExperimentalSettings const* ExperimentalSettings = GetDefault<UEditorExperimentalSettings>();
	bDrawMidpointArrowsInBlueprints = ExperimentalSettings->bDrawMidpointArrowsInBlueprints;

	// settings that were moved out of editor-user settings...
	UEditorPerProjectUserSettings const* UserSettings = GetDefault<UEditorPerProjectUserSettings>();
	bShowActionMenuItemSignatures = UserSettings->bDisplayActionListItemRefIds;

	FString const ClassConfigKey = GetClass()->GetPathName();

	bool bOldSaveOnCompileVal = false;
	// backwards compatibility: handle the case where users have already switched this on
	if (GConfig->GetBool(*ClassConfigKey, TEXT("bSaveOnCompile"), bOldSaveOnCompileVal, GEditorPerProjectIni) && bOldSaveOnCompileVal)
	{
		SaveOnCompile = SoC_SuccessOnly;
	}
}

void UBlueprintEditorSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UBlueprintEditorSettings, bHostFindInBlueprintsInGlobalTab))
	{
		// Close all open Blueprint editors to reset associated FiB states.
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
		for (UObject* EditedAsset : EditedAssets)
		{
			if (EditedAsset->IsA<UBlueprint>())
			{
				AssetEditorSubsystem->CloseAllEditorsForAsset(EditedAsset);
			}
		}

		// Enable or disable the feature through the FiB manager.
		FFindInBlueprintSearchManager::Get().EnableGlobalFindResults(bHostFindInBlueprintsInGlobalTab);
	}

	bool bShouldRebuildRegistry = false;
	
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UBlueprintEditorSettings, bExposeDeprecatedFunctions))
	{
		bShouldRebuildRegistry = true;
	}

	if (bShouldRebuildRegistry)
	{
		FBlueprintActionDatabase::Get().RefreshAll();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}