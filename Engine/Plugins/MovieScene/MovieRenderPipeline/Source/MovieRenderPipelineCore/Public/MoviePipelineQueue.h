// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Object.h"
#include "MovieRenderPipelineDataTypes.h"
#include "LevelSequence.h"
#include "MoviePipelineMasterConfig.h"
#include "MoviePipelineConfigBase.h"

#include "MoviePipelineQueue.generated.h"

class UMoviePipelineMasterConfig;

UENUM(BlueprintType)
enum class EMoviePipelineExecutorJobStatus : uint8
{
	Uninitialized = 0,
	ReadyToStart = 1,
	InProgress = 2,
	Finished = 3
};

/**
* A particular job within the Queue
*/
UCLASS(BlueprintType)
class MOVIERENDERPIPELINECORE_API UMoviePipelineExecutorJob : public UObject
{
	GENERATED_BODY()
public:
	UMoviePipelineExecutorJob()
	{
		Configuration = CreateDefaultSubobject<UMoviePipelineMasterConfig>("DefaultConfig");
	}

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	bool HasFinished() const
	{
		return JobStatus == EMoviePipelineExecutorJobStatus::Finished;
	}

	float GetProgressPercentage() const
	{
		return 0.2f;
	}

public:
	ULevelSequence* TryLoadSequence()
	{
		if(LoadedSequence)
		{
			return LoadedSequence;
		}
		
		LoadedSequence = Cast<ULevelSequence>(Sequence.TryLoad());
		return LoadedSequence;
	}
	
	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline")
	void SetPresetOrigin(UMoviePipelineMasterConfig* InPreset)
	{
		if (InPreset)
		{
			Configuration->CopyFrom(InPreset);
			PresetOrigin = TSoftObjectPtr<UMoviePipelineMasterConfig>(InPreset);
		}
	}

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	UMoviePipelineMasterConfig* GetPresetOrigin() const
	{
		return PresetOrigin.Get();
	}

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	UMoviePipelineMasterConfig* GetConfiguration() const
	{
		return Configuration;
	}

	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline")
	void SetConfiguration(UMoviePipelineMasterConfig* InPreset)
	{
		if (InPreset)
		{
			Configuration->CopyFrom(InPreset);
			PresetOrigin = nullptr;
		}
	}
	
public:
	/** Which sequence should this job render? */
	UPROPERTY(BlueprintReadWrite, Category = "Movie Render Pipeline")
	FSoftObjectPath Sequence;
	
	/** Which map should this job render on */
	UPROPERTY(BlueprintReadWrite, Category = "Movie Render Pipeline")
	FSoftObjectPath Map;

	/** What state is this particular job instance currently in? */
	UPROPERTY(BlueprintReadOnly, Category = "Movie Render Pipeline")
	EMoviePipelineExecutorJobStatus JobStatus;

	/** (Optional) Name of the person who submitted the job. Can be shown in burn in as a first point of contact about the content. */
	UPROPERTY(BlueprintReadWrite, Category = "Movie Render Pipeline")
	FText Author;

private:
	/** 
	*/
	UPROPERTY(Instanced)
	UMoviePipelineMasterConfig* Configuration;

	/**
	*/
	UPROPERTY()
	TSoftObjectPtr<UMoviePipelineMasterConfig> PresetOrigin;
	
private:
	/** Cache our loaded sequence after the first time someone tries to retrieve information from this job that requires the Sequence. */
	UPROPERTY(Transient)
	ULevelSequence* LoadedSequence;
};

/**
* A queue is a list of jobs that have been executed, are executing and are waiting to be executed. These can be saved
* to specific assets to allow 
*/
UCLASS(BlueprintType)
class MOVIERENDERPIPELINECORE_API UMoviePipelineQueue : public UObject
{
	GENERATED_BODY()
public:
	
	UMoviePipelineQueue()
		: QueueSerialNumber(0)
	{
		// Ensure instances are always transactional
		if (!HasAnyFlags(RF_ClassDefaultObject))
		{
			SetFlags(RF_Transactional);
		}
	}
	
	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline|Queue")
	UMoviePipelineExecutorJob* AllocateNewJob();

	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline|Queue")
	void DeleteJob(UMoviePipelineExecutorJob* InJob);

	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline|Queue")
	UMoviePipelineExecutorJob* DuplicateJob(UMoviePipelineExecutorJob* InJob);
	
	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline|Queue")
	TArray<UMoviePipelineExecutorJob*> GetJobs() const
	{
		return Jobs;
	}
	
	/**
	 * Retrieve the serial number that is incremented when a job is added or removed from this list.
	 * @note: This field is not serialized, and not copied along with UObject duplication.
	 */
	uint32 GetQueueSerialNumber() const
	{
		return QueueSerialNumber;
	}

private:
	UPROPERTY()
	TArray<UMoviePipelineExecutorJob*> Jobs;
	
private:
	int32 QueueSerialNumber;
};