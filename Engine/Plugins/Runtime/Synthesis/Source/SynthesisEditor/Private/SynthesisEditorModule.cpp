// Copyright Epic Games, Inc. All Rights Reserved.

#include "SynthesisEditorModule.h"
#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "AssetToolsModule.h"
#include "SynthComponents/EpicSynth1Component.h"
#include "Factories/Factory.h"
#include "AssetTypeActions_Base.h"
#include "AudioEditorModule.h"
#include "EpicSynth1PresetBank.h"
#include "MonoWaveTablePresetBank.h"
#include "AudioImpulseResponseAsset.h"
#include "ToolMenus.h"

IMPLEMENT_MODULE(FSynthesisEditorModule, SynthesisEditor)

void FSynthesisEditorModule::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	AssetTools.RegisterAssetTypeActions(MakeShareable(new FAssetTypeActions_ModularSynthPresetBank));
	AssetTools.RegisterAssetTypeActions(MakeShareable(new FAssetTypeActions_MonoWaveTableSynthPreset));
	AssetTools.RegisterAssetTypeActions(MakeShareable(new FAssetTypeActions_AudioImpulseResponse));

	// Now that we've loaded this module, we need to register our effect preset actions
	IAudioEditorModule* AudioEditorModule = &FModuleManager::LoadModuleChecked<IAudioEditorModule>("AudioEditor");
	AudioEditorModule->RegisterEffectPresetAssetActions();

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FSynthesisEditorModule::RegisterMenus));
}

void FSynthesisEditorModule::ShutdownModule()
{
}

void FSynthesisEditorModule::RegisterMenus()
{
	FAudioImpulseResponseExtension::RegisterMenus();
}
