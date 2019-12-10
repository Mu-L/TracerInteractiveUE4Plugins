// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "DatasmithDispatcherTask.h"
#include "DatasmithWorkerHandler.h"

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "HAL/Thread.h"

namespace DatasmithDispatcher
{

// Handle a list of tasks, and a set of external workers to consume them.
// Concept of task is currently tightly coupled with cad usage...
class DATASMITHDISPATCHER_API FDatasmithDispatcher
{
public:
	FDatasmithDispatcher(const CADLibrary::FImportParameters& InImportParameters, const FString& InCacheDir, int32 InNumberOfWorkers, TMap<FString, FString>& CADFileToUnrealFileMap, TMap<FString, FString>& CADFileToUnrealGeomMap);

	void AddTask(const FString& FileName);
	TOptional<FTask> GetNextTask();
	void SetTaskState(int32 TaskIndex, ETaskState TaskState);

	void Process(bool bWithProcessor);
	bool IsOver();

	void LinkCTFileToUnrealCacheFile(const FString& CTFile, const FString& UnrealSceneGraphFile, const FString& UnrealGeomFile);

private:
	void SpawnHandlers();
	int32 GetNextWorkerId();
	int32 GetAliveHandlerCount();
	void CloseHandlers();

	void ProcessLocal();

private:
	// Tasks
	FCriticalSection TaskPoolCriticalSection;
	TArray<FTask> TaskPool;
	int32 NextTaskIndex;
	int32 CompletedTaskCount;

	// Scene wide state
	TMap<FString, FString>& CADFileToUnrealFileMap;
	TMap<FString, FString>& CADFileToUnrealGeomMap;
	FString ProcessCacheFolder;
	CADLibrary::FImportParameters ImportParameters;

	// Workers
	int32 NumberOfWorkers;
	int32 NextWorkerId;
	TArray<FDatasmithWorkerHandler> WorkerHandlers;
};

} // NS DatasmithDispatcher
