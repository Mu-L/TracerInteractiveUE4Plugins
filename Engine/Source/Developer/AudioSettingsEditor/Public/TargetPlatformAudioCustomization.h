// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PropertyHandle.h"
#include "DetailLayoutBuilder.h"

#include "AudioPluginUtilities.h"

class AUDIOSETTINGSEDITOR_API FAudioPluginWidgetManager
{
public:
	FAudioPluginWidgetManager();
	~FAudioPluginWidgetManager();

	/* Builds out the audio category for a specific audio section for a platform settings page. */
	void BuildAudioCategory(IDetailLayoutBuilder& DetailLayout, EAudioPlatform AudioPlatform);

	/** Creates widget from a scan of loaded audio plugins for an individual plugin type. */
	TSharedRef<SWidget> MakeAudioPluginSelectorWidget(const TSharedPtr<IPropertyHandle>& PropertyHandle, EAudioPlugin AudioPluginType, EAudioPlatform AudioPlatform);

private:
	/** Handles when a new plugin is selected. */
	static void OnPluginSelected(FString PluginName, TSharedPtr<IPropertyHandle> PropertyHandle);

	void OnPluginTextCommitted(const FText& InText, ETextCommit::Type CommitType, EAudioPlugin AudioPluginType, TSharedPtr<IPropertyHandle> PropertyHandle);

	FText OnGetPluginText(EAudioPlugin AudioPluginType);

private:
	/** Cached references to text for Spatialization, Reverb and Occlusion settings */
	TSharedPtr<FText> SelectedReverb;
	TSharedPtr<FText> SelectedSpatialization;
	TSharedPtr<FText> SelectedOcclusion;

	TSharedPtr<FText> ManualSpatializationEntry;
	TSharedPtr<FText> ManualReverbEntry;
	TSharedPtr<FText> ManualOcclusionEntry;

	TArray<TSharedPtr<FText>> SpatializationPlugins;
	TArray<TSharedPtr<FText>> ReverbPlugins;
	TArray<TSharedPtr<FText>> OcclusionPlugins;
};
