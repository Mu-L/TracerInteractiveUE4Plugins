// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineLinearExecutor.h"
#include "MoviePipelinePIEExecutor.generated.h"

class UMoviePipeline;

/**
* This is the implementation responsible for executing the rendering of
* multiple movie pipelines in the currently running Editor process. This
* involves launching a Play in Editor session for each Movie Pipeline to
* process.
*/
UCLASS(Blueprintable)
class MOVIERENDERPIPELINEEDITOR_API UMoviePipelinePIEExecutor : public UMoviePipelineLinearExecutorBase
{
	GENERATED_BODY()
	
public:
	UMoviePipelinePIEExecutor()
		: UMoviePipelineLinearExecutorBase()
		, RemainingInitializationFrames(-1)
		, bPreviousUseFixedTimeStep(false)
		, PreviousFixedTimeStepDelta(1/30.0)
	{
	}

protected:
	virtual void Start(const UMoviePipelineExecutorJob* InJob) override;

private:
	/** Called when PIE finishes booting up and it is safe for us to spawn an object into that world. */
	void OnPIEStartupFinished(bool);

	/** If they're using delayed initialization, this is called each frame to process the countdown until start, also updates Window Title each frame. */
	void OnTick();

	/** Called before PIE tears down the world during shutdown. Used to detect cancel-via-escape/stop PIE. */
	void OnPIEEnded(bool);
	/** Called when the instance of the pipeline in the PIE world has finished. */
	void OnPIEMoviePipelineFinished(UMoviePipeline* InMoviePipeline);
	/** Called a short period of time after OnPIEMoviePipelineFinished to allow Editor the time to fully close PIE before we make a new request. */
	void DelayedFinishNotification();
private:
	/** If using delayed initialization, how many frames are left before we call Initialize. Will be -1 if not actively counting down. */
	int32 RemainingInitializationFrames;
	bool bPreviousUseFixedTimeStep;
	double PreviousFixedTimeStepDelta;
	TWeakPtr<class SWindow> WeakCustomWindow;
};