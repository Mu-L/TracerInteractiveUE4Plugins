// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if INCLUDE_CHAOS

#include "Framework/PhysicsTickTask.h"

#include "ChaosSolversModule.h"
#include "Framework/Dispatcher.h"
#include "ChaosStats.h"
#include "PBDRigidsSolver.h"

FAutoConsoleTaskPriority CPrio_FPhysicsTickTask(
	TEXT("TaskGraph.TaskPriorities.PhysicsTickTask"),
	TEXT("Task and thread priotiry for Chaos physics tick"),
	ENamedThreads::HighThreadPriority, // if we have high priority task threads, then use them...
	ENamedThreads::NormalTaskPriority, // .. at normal task priority
	ENamedThreads::HighTaskPriority // if we don't have hi pri threads, then use normal priority threads at high task priority instead
);

FPhysicsTickTask::FPhysicsTickTask(FGraphEventRef& InCompletionEvent, float InDt)
	: CompletionEvent(InCompletionEvent)
	, Module(nullptr)
	, Dt(InDt)
{
	Module = FChaosSolversModule::GetModule();

	check(Module);
	checkSlow(Module->GetDispatcher() && Module->GetDispatcher()->GetMode() == EChaosThreadingMode::TaskGraph);
}

TStatId FPhysicsTickTask::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FPhysicsTickTask, STATGROUP_TaskGraphTasks);
}

ENamedThreads::Type FPhysicsTickTask::GetDesiredThread()
{
	return CPrio_FPhysicsTickTask.Get();
}

ESubsequentsMode::Type FPhysicsTickTask::GetSubsequentsMode()
{
	return ESubsequentsMode::TrackSubsequents;
}

void FPhysicsTickTask::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	using namespace Chaos;

	// The command task runs the two global command queues prior to us running the 
	// per-solver commands and the solver advance
	FGraphEventRef CommandsTask = TGraphTask<FPhysicsCommandsTask>::CreateTask().ConstructAndDispatchWhenReady();

	const TArray<FPBDRigidsSolver*>& SolverList = Module->GetSolvers();

	// List of active solvers (assume all are active for single alloc)
	TArray<FPBDRigidsSolver*> ActiveSolvers;
	ActiveSolvers.Reserve(SolverList.Num());

	for(FPBDRigidsSolver* Solver : SolverList)
	{
		if(Solver->HasActiveObjects())
		{
			ActiveSolvers.Add(Solver);
		}
	}

	const int32 NumActiveSolvers = ActiveSolvers.Num();

	// Prereqs for the solver task to run
	FGraphEventArray SolverTaskPrerequisites;
	// Prereqs for the final completion task to run (collection of all the solver tasks)
	FGraphEventArray CompletionTaskPrerequisites;
	// Solver tasks have to depend on the global command queues running before them
	SolverTaskPrerequisites.Add(CommandsTask);

	// For each solver spawn a new solver advance task (which will run the per-solver command buffer and then advance the solver)
	// Record the task reference as a prerequisite for the completion
	for(FPBDRigidsSolver* Solver : ActiveSolvers)
	{
		FGraphEventRef SolverTaskRef = TGraphTask<FPhysicsSolverAdvanceTask>::CreateTask(&SolverTaskPrerequisites).ConstructAndDispatchWhenReady(Solver, Dt);
		CompletionTaskPrerequisites.Add(SolverTaskRef);
	}

	// Finally send the completion task pending on all the solver tasks
	TGraphTask<FPhysicsTickCompleteTask>::CreateTask(&CompletionTaskPrerequisites).ConstructAndDispatchWhenReady(CompletionEvent);

	// Drop our reference as we don't need it anymore - the completion task handles it
	CompletionEvent = nullptr;
}

//////////////////////////////////////////////////////////////////////////

FPhysicsCommandsTask::FPhysicsCommandsTask()
{
	Module = FChaosSolversModule::GetModule();
	check(Module);

	Chaos::IDispatcher* BaseDispatcher = Module->GetDispatcher();
	Dispatcher = BaseDispatcher && BaseDispatcher->GetMode() == EChaosThreadingMode::TaskGraph ? static_cast<Chaos::FDispatcher<EChaosThreadingMode::TaskGraph>*>(BaseDispatcher) : nullptr;
	check(Dispatcher);
}

TStatId FPhysicsCommandsTask::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FPhysicsCommandsTask, STATGROUP_TaskGraphTasks);
}

ENamedThreads::Type FPhysicsCommandsTask::GetDesiredThread()
{
	return CPrio_FPhysicsTickTask.Get();
}

ESubsequentsMode::Type FPhysicsCommandsTask::GetSubsequentsMode()
{
	return ESubsequentsMode::TrackSubsequents;
}

void FPhysicsCommandsTask::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	using namespace Chaos;

	// Global and task-level commands (in this threading mode these are analogous as there is no task)
	{
		SCOPE_CYCLE_COUNTER(STAT_PhysCommands);
		TFunction<void()> GlobalCommand;
		while(Dispatcher->GlobalCommandQueue.Dequeue(GlobalCommand))
		{
			GlobalCommand();
		}
	}
	
	{
		SCOPE_CYCLE_COUNTER(STAT_TaskCommands);
		TFunction<void(FPersistentPhysicsTask*)> TaskCommand;
		while(Dispatcher->TaskCommandQueue.Dequeue(TaskCommand))
		{
			TaskCommand(nullptr);
		}
	}
}

//////////////////////////////////////////////////////////////////////////

FPhysicsSolverAdvanceTask::FPhysicsSolverAdvanceTask(Chaos::FPBDRigidsSolver* InSolver, float InDt)
	: Solver(InSolver)
	, Dt(InDt)
{

}

TStatId FPhysicsSolverAdvanceTask::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FPhysicsSolverAdvanceTask, STATGROUP_TaskGraphTasks);
}

ENamedThreads::Type FPhysicsSolverAdvanceTask::GetDesiredThread()
{
	return CPrio_FPhysicsTickTask.Get();
}

ESubsequentsMode::Type FPhysicsSolverAdvanceTask::GetSubsequentsMode()
{
	// The completion task relies on the collection of tick tasks in flight
	return ESubsequentsMode::TrackSubsequents;
}

void FPhysicsSolverAdvanceTask::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	StepSolver(Solver, Dt);
}

void FPhysicsSolverAdvanceTask::StepSolver(Chaos::FPBDRigidsSolver* InSolver, float InDt)
{
	using namespace Chaos;

	check(InSolver);

	// Handle our solver commands
	{
		SCOPE_CYCLE_COUNTER(STAT_HandleSolverCommands);

		TQueue<TFunction<void(FPBDRigidsSolver*)>, EQueueMode::Mpsc>& Queue = InSolver->CommandQueue;
		TFunction<void(FPBDRigidsSolver*)> Command;
		while(Queue.Dequeue(Command))
		{
			Command(InSolver);
		}
	}

	if(InSolver->bEnabled)
	{
		FSolverObjectStorage& Objects = InSolver->GetObjectStorage();

		// Only process if we have something to actually simulate
		if(Objects.GetNumObjects() > 0)
		{
			InSolver->AdvanceSolverBy(InDt);
		}
	}
}

//////////////////////////////////////////////////////////////////////////

FPhysicsTickCompleteTask::FPhysicsTickCompleteTask(FGraphEventRef& InCompletionEvent)
	: CompletionEvent(InCompletionEvent)
{

}

TStatId FPhysicsTickCompleteTask::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FPhysicsTickCompleteTask, STATGROUP_TaskGraphTasks);
}

ENamedThreads::Type FPhysicsTickCompleteTask::GetDesiredThread()
{
	return CPrio_FPhysicsTickTask.Get();
}

ESubsequentsMode::Type FPhysicsTickCompleteTask::GetSubsequentsMode()
{
	// No need to track subsequents for this task as it's the last in the chain and shouldn't be a dependency
	return ESubsequentsMode::FireAndForget;
}

void FPhysicsTickCompleteTask::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	check(CompletionEvent.GetReference()); // Make sure the event still exists

	// Fire off the subsequents on the completion event that we were provided at the beginning of our tick
	TArray<FBaseGraphTask*> NewTasks;
	CompletionEvent->DispatchSubsequents(NewTasks, ENamedThreads::AnyThread);
}

//////////////////////////////////////////////////////////////////////////

#endif
