// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Modules/ModuleManager.h"
#include "MovieSceneCaptureDialogModule.h"
#include "SequencerBindingProxy.h"
#include "SequencerTools.generated.h"

class UFbxExportOption;

DECLARE_DYNAMIC_DELEGATE_OneParam(FOnRenderMovieStopped, bool, bSuccess);

/** 
 * This is a set of helper functions to access various parts of the Sequencer API via Python. Because Sequencer itself is not suitable for exposing, most functionality
 * gets wrapped by UObjects that have an easier API to work with. This UObject provides access to these wrapper UObjects where needed. 
 */
UCLASS(Transient, meta=(ScriptName="SequencerTools"))
class SEQUENCERSCRIPTINGEDITOR_API USequencerToolsFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	/**
	* Attempts to render a sequence to movie based on the specified settings. This will automatically detect
	* if we're rendering via a PIE instance or a new process based on the passed in settings. Will return false
	* if the state is not valid (ie: null or missing required parameters, capture in progress, etc.), true otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | Movie Rendering")
	static bool RenderMovie(class UMovieSceneCapture* InCaptureSettings, FOnRenderMovieStopped OnFinishedCallback);

	/** 
	* Returns if Render to Movie is currently in progress.
	*/
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | Movie Rendering")
	static bool IsRenderingMovie()
	{
		IMovieSceneCaptureDialogModule& MovieSceneCaptureModule = FModuleManager::Get().LoadModuleChecked<IMovieSceneCaptureDialogModule>("MovieSceneCaptureDialog");
		return MovieSceneCaptureModule.GetCurrentCapture().IsValid();
	}

	/**
	* Attempts to cancel an in-progress Render to Movie. Does nothing if there is no render in progress.
	*/
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | Movie Rendering")
	static void CancelMovieRender();

public:
	/*
	 * Export Passed in Bindings to FBX
	 *
	 * @InWorld World to export
	 * @InSequence Sequence to export
	 * @InBindings Bindings to export
	 * @InFBXFileName File to create
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | FBX")
	static bool ExportFBX(UWorld* InWorld, ULevelSequence* InSequence, const TArray<FSequencerBindingProxy>& InBindings, UFbxExportOption* OverrideOptions,const FString& InFBXFileName);

	/*
	 * Import Passed in Bindings to FBX
	 *
	 * @InWorld World to import to
	 * @InSequence InSequence to import
	 * @InBindings InBindings to import
	 * @InImportFBXSettings Settings to control import.
	 * @InImportFileName Path to fbx file to create
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | FBX")
	static bool ImportFBX(UWorld* InWorld, ULevelSequence* InSequence, const TArray<FSequencerBindingProxy>& InBindings, UMovieSceneUserImportFBXSettings* InImportFBXSettings, const FString&  InImportFilename);


};