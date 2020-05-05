// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats2.h"
#include "Modules/ModuleInterface.h"

// Forward Declare
class UMoviePipelineExecutorBase;
class UMoviePipelineQueue;
class ULevelSequence;
class UMoviePipeline;

namespace MoviePipeline { struct FMoviePipelineEnginePass; }

// Declare a stat-group for our performance stats to be counted under, readable in game by "stat MovieRenderPipeline".
DECLARE_STATS_GROUP(TEXT("MovieRenderPipeline"), STATGROUP_MoviePipeline, STATCAT_Advanced);

namespace MoviePipelineErrorCodes
{
	/** Everything completed as expected or we (unfortunately) couldn't detect the error. */
	constexpr uint8 Success = 0;
	/** Fallback for any generic critical failure. This should be used for "Unreal concepts aren't working as expected" severity errors. */
	constexpr uint8 Critical = 1;
	/** The specified level sequence asset could not be found. Check the logs for details on what it looked for. */
	constexpr uint8 NoAsset = 2;
	/** The specified pipeline configuration asset could not be found. Check the logs for details on what it looked for. */
	constexpr uint8 NoConfig = 3;

}
/** A delegate which will create an engine render pass for the Curve Editor. This declares a new engine pass which multiple Pipeline Render Passes can share to reduce re-renders. */
DECLARE_DELEGATE_RetVal(TSharedRef<MoviePipeline::FMoviePipelineEnginePass>, FOnCreateEngineRenderPass);

class MOVIERENDERPIPELINECORE_API FMovieRenderPipelineCoreModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	void UnregisterEngineRenderPass(FDelegateHandle InHandle);
	FDelegateHandle RegisterEngineRenderPass(FOnCreateEngineRenderPass InOnCreateEngineRenderPass);

	TArrayView<const FOnCreateEngineRenderPass> GetEngineRenderPasses() const
	{
		return EngineRenderPassDelegates;
	}
private:
	bool IsTryingToRenderMovieFromCommandLine(FString& OutSequenceAssetPath, FString& OutConfigAssetPath, FString& OutExecutorType, FString& OutPipelineType) const;
	void InitializeCommandLineMovieRender();

	void OnCommandLineMovieRenderCompleted(UMoviePipelineExecutorBase* InExecutor, bool bSuccess);
	void OnCommandLineMovieRenderErrored(UMoviePipelineExecutorBase* InExecutor, UMoviePipeline* InPipelineWithError, bool bIsFatal, FText ErrorText);

	uint8 ParseMovieRenderData(const FString& InSequenceAssetPath, const FString& InConfigAssetPath, const FString& InExecutorType, const FString& InPipelineType,
		UMoviePipelineQueue*& OutQueue, UMoviePipelineExecutorBase*& OutExecutor) const;

private:
	TArray<FOnCreateEngineRenderPass> EngineRenderPassDelegates;

private:
	FString MoviePipelineLocalExecutorClassType;
	FString MoviePipelineClassType;
	FString SequenceAssetValue;
	FString SettingsAssetValue;
};

MOVIERENDERPIPELINECORE_API DECLARE_LOG_CATEGORY_EXTERN(LogMovieRenderPipeline, Log, All);