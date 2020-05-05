// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/GCObject.h"
#include "MoviePipelineConfigBase.h"
#include "MoviePipelineQueue.h"

class UMoviePipelineConfigBase;
class SMoviePipelineConfigEditor;
struct FAssetData;
class UMoviePipelineExecutorJob;

DECLARE_DELEGATE_TwoParams(FOnMoviePipelineConfigChanged, TWeakObjectPtr<UMoviePipelineExecutorJob>, UMoviePipelineConfigBase*)

/**
 * Outermost widget that is used for setting up a new movie render pipeline config. Operates on a transient UMovieRenderShotConfig that is internally owned and maintained 
 */
class SMoviePipelineConfigPanel : public SCompoundWidget, public FGCObject
{
public:

	~SMoviePipelineConfigPanel();

	SLATE_BEGIN_ARGS(SMoviePipelineConfigPanel)
		: _BasePreset(nullptr)

		{}
		SLATE_ARGUMENT(TWeakObjectPtr<UMoviePipelineExecutorJob>, Job)

		SLATE_EVENT(FOnMoviePipelineConfigChanged, OnConfigurationModified)
		SLATE_EVENT(FOnMoviePipelineConfigChanged, OnConfigurationSetToPreset)

		/*~ All following arguments are mutually-exclusive */
		/*-------------------------------------------------*/
		/** A preset asset to copy into the transient UI object. This will not get modified */
		SLATE_ARGUMENT(UMoviePipelineConfigBase*, BasePreset)

		/** An existing configuration to copy into the transient UI object. This will not get modified */
		SLATE_ARGUMENT(UMoviePipelineConfigBase*, BaseConfig)
		/*-------------------------------------------------*/

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSubclassOf<UMoviePipelineConfigBase> InConfigType);
	UMoviePipelineConfigBase* GetPipelineConfig() const;
	UMoviePipelineExecutorJob* GetOwningJob() const;

private:
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

private:
	/** Attempts to work with the user to find a suitable package path to save the asset under. */
	bool GetSavePresetPackageName(const FString& InExistingName, FString& OutName);

	bool OpenSaveDialog(const FString& InDefaultPath, const FString& InNewNameSuggestion, FString& OutPackageName);

	/** Generate the widget that is visible in the Choose Preset dropdown. */
	TSharedRef<SWidget> OnGeneratePresetsMenu();
	
	FText GetConfigTypeLabel() const;

	FReply OnCancelChanges();
	FReply OnConfirmChanges();
	bool CanAcceptChanges() const;


	/** Allocates a transient preset so that the user can use the pipeline without saving it to an asset first. */
	UMoviePipelineConfigBase* AllocateTransientPreset();

	/** Called when any object has Modify() called on it. Used to track if user edits transient object after exporting a preset. */
	void OnAnyObjectModified(UObject* InModifiedObject);
	
	/** When a user wants to import an existing preset asset over the current config. */
	void OnImportPreset(const FAssetData& InPresetAsset);
	/** Save the current configuration out to an asset. */
	void OnSaveAsPreset();

	FText GetValidationWarningText() const;

private:
	/** The transient preset that we use - kept alive by AddReferencedObjects */
	UMoviePipelineConfigBase* TransientPreset;

	/** This is set each time the user performs an action that makes them feel like they've used a specific preset in this UI. */
	TSoftObjectPtr<UMoviePipelineConfigBase> PresetUsedIfNotModified;

	/** The job this editing panel is for. Kept alive externally. */
	TWeakObjectPtr<UMoviePipelineExecutorJob> WeakJob;

	/** The main movie pipeline editor widget */
	TSharedPtr<SMoviePipelineConfigEditor> MoviePipelineEditorWidget;

	/** What type of asset are we editing? Could be a master config or a per-shot override config. */
	TSubclassOf<UMoviePipelineConfigBase> ConfigAssetType;

	FOnMoviePipelineConfigChanged OnConfigurationModified;
	FOnMoviePipelineConfigChanged OnConfigurationSetToPreset;
};