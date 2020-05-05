// Copyright Epic Games, Inc. All Rights Reserved.
#include "MoviePipelineLinearExecutor.h"
#include "MovieRenderPipelineCoreModule.h"
#include "MoviePipelineQueue.h"
#include "MoviePipelineBlueprintLibrary.h"

#define LOCTEXT_NAMESPACE "MoviePipelineLinearExecutorBase"

void UMoviePipelineLinearExecutorBase::ExecuteImpl(UMoviePipelineQueue* InPipelineQueue)
{
	check(InPipelineQueue);

	if (InPipelineQueue->GetJobs().Num() == 0)
	{
		UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Executor asked to execute on empty list of pipelines."));
		OnExecutorErroredImpl(nullptr, true, LOCTEXT("EmptyPipelineError", "Executor asked to execute empty list of jobs. This was probably not intended!"));
		OnExecutorFinishedImpl();
		return;
	}

	// We'll process them in linear fashion and wait until each one is canceled or finishes on its own
	// before moving onto the next one. This may be parallelizable in the future (either multiple PIE
	// sessions, or multiple external processes) but ideally one render would maximize resource usage anyways...
	Queue = InPipelineQueue;
	InitializationTime = FDateTime::UtcNow();

	UE_LOG(LogMovieRenderPipeline, Log, TEXT("MoviePipelineLinearExecutorBase starting %d jobs."), InPipelineQueue->GetJobs().Num());

	StartPipelineByIndex(0);
}

void UMoviePipelineLinearExecutorBase::StartPipelineByIndex(int32 InPipelineIndex)
{
	check(InPipelineIndex >= 0 && InPipelineIndex < Queue->GetJobs().Num());
	
	CurrentPipelineIndex = InPipelineIndex;

	if (!Queue->GetJobs()[CurrentPipelineIndex]->GetConfiguration())
	{
		UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Found null config in list of configs to render. Aborting the pipeline processing!"));
		OnExecutorErroredImpl(nullptr, true, LOCTEXT("NullPipelineError", "Found null config in list of configs to render with. Does your config have the wrong outer?"));
		OnExecutorFinishedImpl();
		return;
	}
	
	UE_LOG(LogMovieRenderPipeline, Log, TEXT("MoviePipelineLinearExecutorBase starting job [%d/%d]"), CurrentPipelineIndex + 1, Queue->GetJobs().Num());
	Start(Queue->GetJobs()[CurrentPipelineIndex]);
}

void UMoviePipelineLinearExecutorBase::OnIndividualPipelineFinished(UMoviePipeline* /* FinishedPipeline */)
{
	if (CurrentPipelineIndex == Queue->GetJobs().Num() - 1)
	{
		OnExecutorFinishedImpl();
	}
	else
	{
		// Onto the next one!
		StartPipelineByIndex(CurrentPipelineIndex + 1);
	}
}

void UMoviePipelineLinearExecutorBase::OnPipelineErrored(UMoviePipeline* InPipeline, bool bIsFatal, FText ErrorText)
{
	OnExecutorErroredImpl(InPipeline, bIsFatal, ErrorText);
}

void UMoviePipelineLinearExecutorBase::OnExecutorFinishedImpl()
{
	UE_LOG(LogMovieRenderPipeline, Log, TEXT("MoviePipelineLinearExecutorBase finished %d jobs in %s."), Queue->GetJobs().Num(), *(FDateTime::UtcNow() - InitializationTime).ToString());
	// Only say that we're no longer rendering once we've finished all jobs in the executor so the UI doesn't flicker while switching over between jobs.
	bIsRendering = false;
	Super::OnExecutorFinishedImpl();
}


FText UMoviePipelineLinearExecutorBase::GetWindowTitle()
{
	FNumberFormattingOptions PercentFormatOptions;
	PercentFormatOptions.MinimumIntegralDigits = 1;
	PercentFormatOptions.MaximumIntegralDigits = 3;
	PercentFormatOptions.MaximumFractionalDigits = 0;
	PercentFormatOptions.RoundingMode = ERoundingMode::HalfFromZero;

	float CompletionPercentage = 0.f;
	if (ActiveMoviePipeline)
	{
		CompletionPercentage = UMoviePipelineBlueprintLibrary::GetCompletionPercentage(ActiveMoviePipeline) * 100.f;
	}

	FText TitleFormatString = LOCTEXT("MoviePreviewWindowTitleFormat", "Movie Pipeline Render (Preview) [Job {CurrentCount}/{TotalCount} Total] Current Job: {PercentComplete}% Completed.");
	return FText::FormatNamed(TitleFormatString, TEXT("CurrentCount"), FText::AsNumber(CurrentPipelineIndex + 1), TEXT("TotalCount"), FText::AsNumber(Queue->GetJobs().Num()), TEXT("PercentComplete"), FText::AsNumber(CompletionPercentage, &PercentFormatOptions));
}

#undef LOCTEXT_NAMESPACE // "MoviePipelineLinearExecutorBase"
