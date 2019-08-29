// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "Interfaces/IPluginManager.h"

#include "Editor.h"

#include "Modules/ModuleManager.h"
#include "Features/IModularFeatures.h"
#include "Misc/CoreDelegates.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "EditorStyleSet.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateTypes.h"
#include "Styling/SlateStyleRegistry.h"
#include "Framework/Application/SlateApplication.h"

#include "Features/IModularFeatures.h"
#include "LevelEditor.h"

#include "LiveLinkClient.h"
#include "LiveLinkClientPanel.h"
#include "LiveLinkClientCommands.h"

#include "ISequencerModule.h"
#include "Sequencer/LiveLinkPropertyTrackEditor.h"
#include "SequencerRecorderSections/MovieSceneLiveLinkSectionRecorder.h"

/**
 * Implements the Messaging module.
 */

#define LOCTEXT_NAMESPACE "LiveLinkModule"

static const FName LiveLinkClientTabName(TEXT("LiveLink"));
static const FName LevelEditorModuleName(TEXT("LevelEditor"));
static const FName MovieSceneSectionRecorderFactoryName("MovieSceneSectionRecorderFactory");

#define IMAGE_PLUGIN_BRUSH( RelativePath, ... ) FSlateImageBrush( InPluginContent( RelativePath, ".png" ), __VA_ARGS__ )

FString InPluginContent(const FString& RelativePath, const ANSICHAR* Extension)
{
	static FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("LiveLink"))->GetContentDir();
	return (ContentDir / RelativePath) + Extension;
}

class FLiveLinkEditorModule : public IModuleInterface
{
public:
	TSharedPtr<FSlateStyleSet> StyleSet;

	TSharedPtr< class ISlateStyle > GetStyleSet() { return StyleSet; }

	// IModuleInterface interface

	virtual void StartupModule() override
	{
		static FName LiveLinkStyle(TEXT("LiveLinkStyle"));
		StyleSet = MakeShareable(new FSlateStyleSet(LiveLinkStyle));

		bHasRegisteredTabSpawners = false;

		if (FModuleManager::Get().IsModuleLoaded(LevelEditorModuleName))
		{
			RegisterTabSpawner();
		}
		else
		{
			ModulesChangedHandle = FModuleManager::Get().OnModulesChanged().AddRaw(this, &FLiveLinkEditorModule::ModulesChangesCallback);
		}

		FLiveLinkClientCommands::Register();

		const FVector2D Icon16x16(16.0f, 16.0f);
		const FVector2D Icon20x20(20.0f, 20.0f);
		const FVector2D Icon40x40(40.0f, 40.0f);

		StyleSet->SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));
		StyleSet->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

		StyleSet->Set("LiveLinkClient.Common.Icon", new IMAGE_PLUGIN_BRUSH(TEXT("LiveLink_40x"), Icon40x40));
		StyleSet->Set("LiveLinkClient.Common.Icon.Small", new IMAGE_PLUGIN_BRUSH(TEXT("LiveLink_16x"), Icon16x16));

		StyleSet->Set("LiveLinkClient.Common.AddSource", new IMAGE_PLUGIN_BRUSH(TEXT("icon_AddSource_40x"), Icon40x40));
		StyleSet->Set("LiveLinkClient.Common.RemoveSource", new IMAGE_PLUGIN_BRUSH(TEXT("icon_RemoveSource_40x"), Icon40x40));
		StyleSet->Set("LiveLinkClient.Common.RemoveAllSources", new IMAGE_PLUGIN_BRUSH(TEXT("icon_RemoveSource_40x"), Icon40x40));

		FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());

		ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");
		CreateLiveLinkPropertyTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FLiveLinkPropertyTrackEditor::CreateTrackEditor));

		IModularFeatures::Get().RegisterModularFeature(MovieSceneSectionRecorderFactoryName, &MovieSceneLiveLinkRecorder);

	}

	void ModulesChangesCallback(FName ModuleName, EModuleChangeReason ReasonForChange)
	{
		if (ReasonForChange == EModuleChangeReason::ModuleLoaded && ModuleName == LevelEditorModuleName)
		{
			RegisterTabSpawner();
		}
	}

	virtual void ShutdownModule() override
	{
		UnregisterTabSpawner();

		FModuleManager::Get().OnModulesChanged().Remove(ModulesChangedHandle);

		if (LevelEditorTabManagerChangedHandle.IsValid() && FModuleManager::Get().IsModuleLoaded(LevelEditorModuleName))
		{
			FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(LevelEditorModuleName);
			LevelEditorModule.OnTabManagerChanged().Remove(LevelEditorTabManagerChangedHandle);
		}

		ISequencerModule* SequencerModule = FModuleManager::GetModulePtr<ISequencerModule>("Sequencer");
		if (SequencerModule != nullptr)
		{
			SequencerModule->UnRegisterTrackEditor(CreateLiveLinkPropertyTrackEditorHandle);
		}

		IModularFeatures::Get().UnregisterModularFeature(MovieSceneSectionRecorderFactoryName, &MovieSceneLiveLinkRecorder);
	}

	virtual bool SupportsDynamicReloading() override
	{
		return false;
	}

	static TSharedRef<SDockTab> SpawnLiveLinkTab(const FSpawnTabArgs& SpawnTabArgs, TSharedPtr<FSlateStyleSet> StyleSet)
	{
		FLiveLinkClient* Client = &IModularFeatures::Get().GetModularFeature<FLiveLinkClient>(FLiveLinkClient::ModularFeatureName);

		const FSlateBrush* IconBrush = StyleSet->GetBrush("LiveLinkClient.Common.Icon.Small");

		const TSharedRef<SDockTab> MajorTab =
			SNew(SDockTab)
			.Icon(IconBrush)
			.TabRole(ETabRole::NomadTab);

		MajorTab->SetContent(SNew(SLiveLinkClientPanel, Client));

		return MajorTab;
	}

private:

	void RegisterTabSpawner()
	{
		if (bHasRegisteredTabSpawners)
		{
			UnregisterTabSpawner();
		}

		TSharedPtr<FSlateStyleSet> StyleSetPtr = StyleSet;
		FTabSpawnerEntry& SpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(LiveLinkClientTabName, FOnSpawnTab::CreateStatic(&FLiveLinkEditorModule::SpawnLiveLinkTab, StyleSetPtr))
			.SetDisplayName(LOCTEXT("LiveLinkTabTitle", "Live Link"))
			.SetTooltipText(LOCTEXT("SequenceRecorderTooltipText", "Open the Live Link streaming manager tab."))
			.SetIcon(FSlateIcon(StyleSetPtr->GetStyleSetName(), "LiveLinkClient.Common.Icon.Small"));

		const IWorkspaceMenuStructure& MenuStructure = WorkspaceMenu::GetMenuStructure();
		SpawnerEntry.SetGroup(MenuStructure.GetLevelEditorCategory());

		bHasRegisteredTabSpawners = true;
	}

	void UnregisterTabSpawner()
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(LiveLinkClientTabName);
		bHasRegisteredTabSpawners = false;
	}

	FDelegateHandle LevelEditorTabManagerChangedHandle;
	FDelegateHandle ModulesChangedHandle;
	FDelegateHandle CreateLiveLinkPropertyTrackEditorHandle;
	FMovieSceneLiveLinkSectionRecorderFactory MovieSceneLiveLinkRecorder;

	// Track if we have registered
	bool bHasRegisteredTabSpawners;
};

IMPLEMENT_MODULE(FLiveLinkEditorModule, LiveLinkEditor);

#undef LOCTEXT_NAMESPACE
