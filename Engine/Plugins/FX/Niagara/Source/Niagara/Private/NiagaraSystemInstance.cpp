// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraSystemInstance.h"
#include "NiagaraConstants.h"
#include "NiagaraCommon.h"
#include "NiagaraDataInterface.h"
#include "NiagaraStats.h"
#include "Async/ParallelFor.h"
#include "NiagaraParameterCollection.h"
#include "NiagaraWorldManager.h"
#include "NiagaraComponent.h"
#include "NiagaraRenderer.h"
#include "Templates/AlignmentTemplates.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "GameFramework/PlayerController.h"


DECLARE_CYCLE_STAT(TEXT("System Activate [GT]"), STAT_NiagaraSystemActivate, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Deactivate [GT]"), STAT_NiagaraSystemDeactivate, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Complete [GT]"), STAT_NiagaraSystemComplete, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Reset [GT]"), STAT_NiagaraSystemReset, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Reinit [GT]"), STAT_NiagaraSystemReinit, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Init Emitters [GT]"), STAT_NiagaraSystemInitEmitters, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Advance Simulation [GT] "), STAT_NiagaraSystemAdvanceSim, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System SetSolo[GT] "), STAT_NiagaraSystemSetSolo, STATGROUP_Niagara);

DECLARE_CYCLE_STAT(TEXT("System PreSimulateTick [CNC]"), STAT_NiagaraSystemPreSimulateTick, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Instance Tick [CNC]"), STAT_NiagaraSystemInstanceTick, STATGROUP_Niagara);

 
/** Safety time to allow for the LastRenderTime coming back from the RT. This is overkill but that's ok.*/
static float GLastRenderTimeSafetyBias = 0.1f;
static FAutoConsoleVariableRef CVarLastRenderTimeSafetyBias(
	TEXT("fx.LastRenderTimeSafetyBias"),
	GLastRenderTimeSafetyBias,
	TEXT("The time to bias the LastRenderTime value to allow for the delay from it being written by the RT."),
	ECVF_Default
);

FNiagaraSystemInstance::FNiagaraSystemInstance(UNiagaraComponent* InComponent)
	: SystemInstanceIndex(INDEX_NONE)
	, Component(InComponent)
	, Age(0.0f)
	, TickCount(0)
	, ID(FGuid::NewGuid())
	, IDName(*ID.ToString())
	, InstanceParameters(Component)
	, bSolo(false)
	, bForceSolo(false)
	, bPendingSpawn(false)
	, bHasTickingEmitters(true)
	, bPaused(false)
	, RequestedExecutionState(ENiagaraExecutionState::Complete)
	, ActualExecutionState(ENiagaraExecutionState::Complete)
	, bDataInterfacesInitialized(false)
{
	SystemBounds.Init();

	if (Component)
	{
		UWorld* World = Component->GetWorld();
		if (World && World->Scene)
		{
			FFXSystemInterface*  FXSystemInterface = World->Scene->GetFXSystem();
			if (FXSystemInterface)
			{
				Batcher = static_cast<NiagaraEmitterInstanceBatcher*>(FXSystemInterface->GetInterface(NiagaraEmitterInstanceBatcher::Name));
			}
		}
	}
}

void FNiagaraSystemInstance::Init(bool bInForceSolo)
{
	bForceSolo = bInForceSolo;
	ActualExecutionState = ENiagaraExecutionState::Inactive;
	RequestedExecutionState = ENiagaraExecutionState::Inactive;

	//InstanceParameters = GetSystem()->GetInstanceParameters();
	// In order to get user data interface parameters in the component to work properly,
	// we need to bind here, otherwise the instances when we init data interfaces during reset will potentially
	// be the defaults (i.e. null) for things like static mesh data interfaces.
	Reset(EResetMode::ReInit);

#if WITH_EDITORONLY_DATA
	InstanceParameters.DebugName = *FString::Printf(TEXT("SystemInstance %p"), this);
#endif
	OnInitializedDelegate.Broadcast();
}

void FNiagaraSystemInstance::SetRequestedExecutionState(ENiagaraExecutionState InState)
{
	//Once in disabled state we can never get out except on Reinit.
	if (RequestedExecutionState != InState && RequestedExecutionState != ENiagaraExecutionState::Disabled)
	{
		/*const UEnum* EnumPtr = FNiagaraTypeDefinition::GetExecutionStateEnum();
		UE_LOG(LogNiagara, Log, TEXT("Component \"%s\" System \"%s\" requested change state: %s to %s, actual %s"), *GetComponent()->GetName(), *GetSystem()->GetName(), *EnumPtr->GetNameStringByValue((int64)RequestedExecutionState),
			*EnumPtr->GetNameStringByValue((int64)InState), *EnumPtr->GetNameStringByValue((int64)ActualExecutionState));
		*/
		if (InState == ENiagaraExecutionState::Disabled)
		{
			//Really move to disabled straight away.
			ActualExecutionState = ENiagaraExecutionState::Disabled;
			Cleanup();
		}
		RequestedExecutionState = InState;
	}
}

void FNiagaraSystemInstance::SetActualExecutionState(ENiagaraExecutionState InState)
{

	//Once in disabled state we can never get out except on Reinit.
	if (ActualExecutionState != InState && ActualExecutionState != ENiagaraExecutionState::Disabled)
	{
		/*const UEnum* EnumPtr = FNiagaraTypeDefinition::GetExecutionStateEnum();
		UE_LOG(LogNiagara, Log, TEXT("Component \"%s\" System \"%s\" actual change state: %s to %s"), *GetComponent()->GetName(), *GetSystem()->GetName(), *EnumPtr->GetNameStringByValue((int64)ActualExecutionState),
			*EnumPtr->GetNameStringByValue((int64)InState));
		*/
		ActualExecutionState = InState;

		if (ActualExecutionState == ENiagaraExecutionState::Active)
		{
			// We only need to notify completion once after each successful active.
			// Here's when we know that we just became active.
			bNotifyOnCompletion = true;

			// We may also end up calling HandleCompletion on each emitter.
			// This may happen *before* we've successfully pulled data off of a 
			// simulation run. This means that we need to synchronize the execution
			// states upon activation.
			for (int32 EmitterIdx = 0; EmitterIdx < Emitters.Num(); ++EmitterIdx)
			{
				FNiagaraEmitterInstance& EmitterInst = Emitters[EmitterIdx].Get();
				EmitterInst.SetExecutionState(ENiagaraExecutionState::Active);
			}
		}
	}
}

void FNiagaraSystemInstance::Dump()const
{
	GetSystemSimulation()->DumpInstance(this);
	for (auto& Emitter : Emitters)
	{
		Emitter->Dump();
	}
}

#if WITH_EDITORONLY_DATA
bool FNiagaraSystemInstance::RequestCapture(const FGuid& RequestId)
{
	if (IsComplete() || CurrentCapture.IsValid())
	{
		return false;
	}

	UE_LOG(LogNiagara, Warning, TEXT("Capture requested!"));

	bWasSoloPriorToCaptureRequest = bSolo;
	SetSolo(true);

	// Go ahead and populate the shared array so that we don't have to do this on the game thread and potentially race.
	TSharedRef<TArray<TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe>>, ESPMode::ThreadSafe> TempCaptureHolder = 
		MakeShared<TArray<TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe>>, ESPMode::ThreadSafe>();
	
	TempCaptureHolder->Add(MakeShared<FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe>(NAME_None, ENiagaraScriptUsage::SystemSpawnScript, FGuid()));
	TempCaptureHolder->Add(MakeShared<FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe>(NAME_None, ENiagaraScriptUsage::SystemUpdateScript, FGuid()));

	for (const FNiagaraEmitterHandle& Handle : GetSystem()->GetEmitterHandles())
	{
		TArray<UNiagaraScript*> Scripts;
		Handle.GetInstance()->GetScripts(Scripts, false);

		for (UNiagaraScript* Script : Scripts)
		{
			TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe> DebugInfoPtr = MakeShared<FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe>(Handle.GetIdName(), Script->GetUsage(), Script->GetUsageId());
			DebugInfoPtr->bWritten = false;

			TempCaptureHolder->Add(DebugInfoPtr);
		}
	}
	CapturedFrames.Add(RequestId, TempCaptureHolder);
	CurrentCapture = TempCaptureHolder;
	CurrentCaptureGuid = MakeShared<FGuid, ESPMode::ThreadSafe>(RequestId);
	return true;
}

void FNiagaraSystemInstance::FinishCapture()
{
	if (!CurrentCapture.IsValid())
	{
		return;
	}

	SetSolo(bWasSoloPriorToCaptureRequest);
	CurrentCapture.Reset();
	CurrentCaptureGuid.Reset();
}

bool FNiagaraSystemInstance::QueryCaptureResults(const FGuid& RequestId, TArray<TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe>>& OutCaptureResults)
{
	if (CurrentCaptureGuid.IsValid() && RequestId == *CurrentCaptureGuid.Get())
	{
		return false;
	}

	const TSharedPtr<TArray<TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe>>, ESPMode::ThreadSafe>* FoundEntry = CapturedFrames.Find(RequestId);
	if (FoundEntry != nullptr)
	{
		TArray<TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe>>* Array = FoundEntry->Get();
		OutCaptureResults.SetNum(Array->Num());

		bool bWaitForGPU = false;
		{
			for (int32 i = 0; i < FoundEntry->Get()->Num(); i++)
			{
				if ((*Array)[i]->bWaitForGPU && (*Array)[i]->bWritten == false)
				{
					bWaitForGPU = true;
					break;
				}
			}
			
			if (bWaitForGPU)
			{
				for (TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe> CachedEmitter : Emitters)
				{
					CachedEmitter->WaitForDebugInfo();
				}
				return false;
			}
		}


		for (int32 i = 0; i < FoundEntry->Get()->Num(); i++)
		{
			OutCaptureResults[i] = (*Array)[i];
		}
		CapturedFrames.Remove(RequestId);
		return true;
	}

	return false;
}

TArray<TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe>>* FNiagaraSystemInstance::GetActiveCaptureResults()
{
	return CurrentCapture.Get();
}

TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe> FNiagaraSystemInstance::GetActiveCaptureWrite(const FName& InHandleName, ENiagaraScriptUsage InUsage, const FGuid& InUsageId)
{
	if (CurrentCapture.IsValid())
	{
		TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe>* FoundEntry = CurrentCapture->FindByPredicate([&](const TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe>& Entry)
		{
			return Entry->HandleName == InHandleName && UNiagaraScript::IsEquivalentUsage(Entry->Usage, InUsage) && Entry->UsageId == InUsageId;
		});

		if (FoundEntry != nullptr)
		{
			return *FoundEntry;
		}
	}
	return nullptr;
}

bool FNiagaraSystemInstance::ShouldCaptureThisFrame() const
{
	return CurrentCapture.IsValid();
}
#endif

void FNiagaraSystemInstance::SetSolo(bool bInSolo)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSetSolo);
	if (bSolo == bInSolo)
	{
		return;
	}

	UNiagaraSystem* System = GetSystem();
	if (bInSolo)
	{
		TSharedPtr<FNiagaraSystemSimulation, ESPMode::ThreadSafe> NewSoloSim = MakeShared<FNiagaraSystemSimulation, ESPMode::ThreadSafe>();
		NewSoloSim->Init(System, Component->GetWorld(), true);

		NewSoloSim->TransferInstance(SystemSimulation.Get(), this);	

		SystemSimulation = NewSoloSim;
		bSolo = true;
	}
	else
	{
		TSharedPtr<FNiagaraSystemSimulation, ESPMode::ThreadSafe> NewSim = GetWorldManager()->GetSystemSimulation(System);

		NewSim->TransferInstance(SystemSimulation.Get(), this);
		
		SystemSimulation = NewSim;
		bSolo = false;
	}
}

void FNiagaraSystemInstance::Activate(EResetMode InResetMode)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemActivate);
	
	UNiagaraSystem* System = GetSystem();
	if (System && System->IsValid() && IsReadyToRun())
	{
		Reset(InResetMode);
	}
	else
	{
		SetRequestedExecutionState(ENiagaraExecutionState::Disabled);
	}
}

void FNiagaraSystemInstance::Deactivate(bool bImmediate)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemDeactivate);
	if (IsComplete())
	{
		return;
	}

	if (bImmediate)
	{
		Complete();
	}
	else
	{
		SetRequestedExecutionState(ENiagaraExecutionState::Inactive);
	}
}

bool FNiagaraSystemInstance::AllocateSystemInstance(class UNiagaraComponent* InComponent, TUniquePtr< FNiagaraSystemInstance >& OutSystemInstanceAllocation)
{
	OutSystemInstanceAllocation = MakeUnique<FNiagaraSystemInstance>(InComponent);
	return true;
}

bool FNiagaraSystemInstance::DeallocateSystemInstance(TUniquePtr< FNiagaraSystemInstance >& SystemInstanceAllocation)
{
	if (SystemInstanceAllocation.IsValid())
	{
		TSharedPtr<FNiagaraSystemSimulation, ESPMode::ThreadSafe> SystemSim = SystemInstanceAllocation->GetSystemSimulation();

		// Make sure we remove the instance
		if (SystemInstanceAllocation->SystemInstanceIndex != INDEX_NONE)
		{
			SystemSim->RemoveInstance(SystemInstanceAllocation.Get());
		}

		// Queue deferred deletion from the WorldManager
		FNiagaraWorldManager* WorldManager = SystemInstanceAllocation->GetWorldManager();
		check(WorldManager != nullptr);

		SystemInstanceAllocation->Component = nullptr;

		WorldManager->DestroySystemInstance(SystemInstanceAllocation);
		check(SystemInstanceAllocation == nullptr);
	}
	SystemInstanceAllocation = nullptr;
	
	return true;
}

void FNiagaraSystemInstance::Complete()
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemComplete);
	
	// Only notify others if have yet to complete
	bool bNeedToNotifyOthers = bNotifyOnCompletion;

	//UE_LOG(LogNiagara, Log, TEXT("FNiagaraSystemInstance::Complete { %p"), this);

	if (SystemInstanceIndex != INDEX_NONE)
	{
		TSharedPtr<FNiagaraSystemSimulation, ESPMode::ThreadSafe> SystemSim = GetSystemSimulation();
		SystemSim->RemoveInstance(this);

		SetActualExecutionState(ENiagaraExecutionState::Complete);
		SetRequestedExecutionState(ENiagaraExecutionState::Complete);

		for (TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe> Simulation : Emitters)
		{
			Simulation->HandleCompletion(true);
		}
	}
	else
	{
		SetActualExecutionState(ENiagaraExecutionState::Complete);
		SetRequestedExecutionState(ENiagaraExecutionState::Complete);
	}

	DestroyDataInterfaceInstanceData();

	UnbindParameters();

	if (bNeedToNotifyOthers)
	{
		// We've already notified once, no need to do so again.
		bNotifyOnCompletion = false;

		OnCompleteDelegate.Broadcast(this);

		if (Component)
		{
			// Note: This call may destroy this instance of FNiagaraSystemInstance, so don't use bNotifyOnCompletion after it!
			Component->OnSystemComplete();
		}
	}
}

void FNiagaraSystemInstance::SetPaused(bool bInPaused)
{
	if (bInPaused == bPaused)
	{
		return;
	}
	
	if (SystemInstanceIndex != INDEX_NONE)
	{
		FNiagaraSystemSimulation* SystemSim = GetSystemSimulation().Get();
		if (SystemSim)
		{
			if (bInPaused)
			{
				SystemSim->PauseInstance(this);
			}
			else
			{
				SystemSim->UnpauseInstance(this);
			}
		}
	}

	bPaused = bInPaused;
}

void FNiagaraSystemInstance::Reset(FNiagaraSystemInstance::EResetMode Mode)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemReset);

	if (Mode == EResetMode::None)
	{
		// Right now we don't support binding with reset mode none.
		/*if (Mode == EResetMode::None && bBindParams)
		{
			BindParameters();
		}*/
		return;
	}
	
	if (Component)
	{
		Component->SetLastRenderTime(Component->GetWorld()->GetTimeSeconds());
	}

	SetPaused(false);

	if (SystemSimulation.IsValid())
	{
		SystemSimulation->RemoveInstance(this);
	}
	else
	{
		Mode = EResetMode::ReInit;
	}

	//If we were disabled, try to reinit on reset.
	if (IsDisabled())
	{
		Mode = EResetMode::ReInit;
	}
		
	// Depending on the rest mode we may need to bind or can possibly skip it
	// We must bind if we were previously complete as unbind will have been called, we can not get here if the system was disabled
	bool bBindParams = IsComplete();
	if (Mode == EResetMode::ResetSystem)
	{
		//UE_LOG(LogNiagara, Log, TEXT("FNiagaraSystemInstance::Reset false"));
		ResetInternal(false);
	}
	else if (Mode == EResetMode::ResetAll)
	{
		//UE_LOG(LogNiagara, Log, TEXT("FNiagaraSystemInstance::Reset true"));
		ResetInternal(true);
		bBindParams = !IsDisabled();
	}
	else if (Mode == EResetMode::ReInit)
	{
		//UE_LOG(LogNiagara, Log, TEXT("FNiagaraSystemInstance::ReInit"));
		ReInitInternal();
		bBindParams = !IsDisabled();
	}
	
	if (bBindParams)
	{
		BindParameters();
	}

	SetRequestedExecutionState(ENiagaraExecutionState::Active);
	SetActualExecutionState(ENiagaraExecutionState::Active);

	if (bBindParams)
	{
		InitDataInterfaces();
	}

	//Interface init can disable the system.
	if (!IsComplete())
	{
		bPendingSpawn = true;
		SystemSimulation->AddInstance(this);

		UNiagaraSystem* System = GetSystem();
		if (System->NeedsWarmup())
		{
			int32 WarmupTicks = System->GetWarmupTickCount();
			float WarmupDt = System->GetWarmupTickDelta();
			
			AdvanceSimulation(WarmupTicks, WarmupDt);

			//Reset age to zero.
			Age = 0.0f;
			TickCount = 0;
		}
	}

	if (Component)
	{
		// This system may not tick again immediately so we mark the render state dirty here so that
		// the renderers will be reset this frame.
		Component->MarkRenderDynamicDataDirty();
	}
}

void FNiagaraSystemInstance::ResetInternal(bool bResetSimulations)
{
	Age = 0;
	TickCount = 0;
	UNiagaraSystem* System = GetSystem();
	if (System == nullptr || Component == nullptr || IsDisabled())
	{
		return;
	}

#if WITH_EDITOR
	if (Component->GetWorld() != nullptr && Component->GetWorld()->WorldType == EWorldType::Editor)
	{
		Component->GetOverrideParameters().Tick();
	}
#endif

	bool bAllReadyToRun = IsReadyToRun();

	if (!bAllReadyToRun)
	{
		return;
	}

	if (!System->IsValid())
	{
		SetRequestedExecutionState(ENiagaraExecutionState::Disabled);
		UE_LOG(LogNiagara, Warning, TEXT("Failed to activate Niagara System due to invalid asset!"));
		return;
	}

	for (TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe> Simulation : Emitters)
	{
		Simulation->ResetSimulation(bResetSimulations);
	}

#if WITH_EDITOR
	//UE_LOG(LogNiagara, Log, TEXT("OnResetInternal %p"), this);
	OnResetDelegate.Broadcast();
#endif
}

UNiagaraParameterCollectionInstance* FNiagaraSystemInstance::GetParameterCollectionInstance(UNiagaraParameterCollection* Collection)
{
	return SystemSimulation->GetParameterCollectionInstance(Collection);
}

void FNiagaraSystemInstance::AdvanceSimulation(int32 TickCountToSimulate, float TickDeltaSeconds)
{
	if (TickCountToSimulate > 0)
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemAdvanceSim);
		bool bWasSolo = bSolo;
		SetSolo(true);

		for (int32 TickIdx = 0; TickIdx < TickCountToSimulate; ++TickIdx)
		{
			ComponentTick(TickDeltaSeconds);
		}
		SetSolo(bWasSolo);
	}
}

bool FNiagaraSystemInstance::IsReadyToRun() const
{
	bool bAllReadyToRun = true;

	UNiagaraSystem* System = GetSystem();

	if (!System || !System->IsReadyToRun())
	{
		return false;
	}

	for (TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe> Simulation : Emitters)
	{
		if (!Simulation->IsReadyToRun())
		{
			bAllReadyToRun = false;
		}
	}
	return bAllReadyToRun;
}

bool DoSystemDataInterfacesRequireSolo(const UNiagaraSystem& System, const UNiagaraComponent& Component)
{
	if (System.HasSystemScriptDIsWithPerInstanceData())
	{
		return true;
	}

	const TArray<FName>& UserDINamesReadInSystemScripts = System.GetUserDINamesReadInSystemScripts();
	if (UserDINamesReadInSystemScripts.Num() > 0)
	{
		TArray<FNiagaraVariable> OverrideParameterVariables;
		Component.GetOverrideParameters().GetParameters(OverrideParameterVariables);
		for (const FNiagaraVariable& OverrideParameterVariable : OverrideParameterVariables)
		{
			if (OverrideParameterVariable.IsDataInterface() && UserDINamesReadInSystemScripts.Contains(OverrideParameterVariable.GetName()))
			{
				return true;
			}
		}
	}

	return false;
}

void FNiagaraSystemInstance::ReInitInternal()
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemReinit);
	Age = 0;
	TickCount = 0;
	UNiagaraSystem* System = GetSystem();
	if (System == nullptr || Component == nullptr)
	{
		return;
	}

	//Bypass the SetExecutionState() and it's check for disabled.
	RequestedExecutionState = ENiagaraExecutionState::Inactive;
	ActualExecutionState = ENiagaraExecutionState::Inactive;

	bool bAllReadyToRun = IsReadyToRun();

	if (!bAllReadyToRun)
	{
		return;
	}
	
	if (!System->IsValid())
	{
		SetRequestedExecutionState(ENiagaraExecutionState::Disabled);
		UE_LOG(LogNiagara, Warning, TEXT("Failed to activate Niagara System due to invalid asset!"));
		return;
	}

	/** Do we need to run in solo mode? */
	bSolo = bForceSolo || DoSystemDataInterfacesRequireSolo(*System, *Component);
	if (bSolo)
	{
		if (!SystemSimulation.IsValid())
		{
			SystemSimulation = MakeShared<FNiagaraSystemSimulation, ESPMode::ThreadSafe>();
			SystemSimulation->Init(System, Component->GetWorld(), true);
		}
	}
	else
	{
		SystemSimulation = GetWorldManager()->GetSystemSimulation(System);
	}

	//When re initializing, throw away old emitters and init new ones.
	Emitters.Reset();
	InitEmitters();
	
	InstanceParameters.Reset();
	InstanceParameters.AddParameter(SYS_PARAM_ENGINE_POSITION, true, false);
	InstanceParameters.AddParameter(SYS_PARAM_ENGINE_ROTATION, true, false);
	InstanceParameters.AddParameter(SYS_PARAM_ENGINE_SCALE, true, false);
	InstanceParameters.AddParameter(SYS_PARAM_ENGINE_VELOCITY, true, false);
	InstanceParameters.AddParameter(SYS_PARAM_ENGINE_X_AXIS, true, false);
	InstanceParameters.AddParameter(SYS_PARAM_ENGINE_Y_AXIS, true, false);
	InstanceParameters.AddParameter(SYS_PARAM_ENGINE_Z_AXIS, true, false);
	InstanceParameters.AddParameter(SYS_PARAM_ENGINE_LOCAL_TO_WORLD, true, false);
	InstanceParameters.AddParameter(SYS_PARAM_ENGINE_WORLD_TO_LOCAL, true, false);
	InstanceParameters.AddParameter(SYS_PARAM_ENGINE_LOCAL_TO_WORLD_TRANSPOSED, true, false);
	InstanceParameters.AddParameter(SYS_PARAM_ENGINE_WORLD_TO_LOCAL_TRANSPOSED, true, false);
	InstanceParameters.AddParameter(SYS_PARAM_ENGINE_LOCAL_TO_WORLD_NO_SCALE, true, false);
	InstanceParameters.AddParameter(SYS_PARAM_ENGINE_WORLD_TO_LOCAL_NO_SCALE, true, false);
	InstanceParameters.AddParameter(SYS_PARAM_ENGINE_DELTA_TIME, true, false);
	InstanceParameters.AddParameter(SYS_PARAM_ENGINE_TIME, true, false);
	InstanceParameters.AddParameter(SYS_PARAM_ENGINE_REAL_TIME, true, false);
	InstanceParameters.AddParameter(SYS_PARAM_ENGINE_INV_DELTA_TIME, true, false);
	InstanceParameters.AddParameter(SYS_PARAM_ENGINE_TIME_SINCE_RENDERED, true, false);
	InstanceParameters.AddParameter(SYS_PARAM_ENGINE_EXECUTION_STATE, true, false);
	InstanceParameters.AddParameter(SYS_PARAM_ENGINE_LOD_DISTANCE, true, false);
	InstanceParameters.AddParameter(SYS_PARAM_ENGINE_SYSTEM_NUM_EMITTERS, true, false);
	InstanceParameters.AddParameter(SYS_PARAM_ENGINE_SYSTEM_NUM_EMITTERS_ALIVE, true, false);
	InstanceParameters.AddParameter(SYS_PARAM_ENGINE_SYSTEM_AGE);
	InstanceParameters.AddParameter(SYS_PARAM_ENGINE_SYSTEM_TICK_COUNT);

	// This is required for user default data interface's (like say static meshes) to be set up properly.
	// Additionally, it must happen here for data to be properly found below.
	const bool bOnlyAdd = false;
	System->GetExposedParameters().CopyParametersTo(InstanceParameters, bOnlyAdd, FNiagaraParameterStore::EDataInterfaceCopyMethod::Reference);

	TArray<FNiagaraVariable> NumParticleVars;
	TArray<FNiagaraVariable> TotalSpawnedParticlesVars;
	for (int32 i = 0; i < Emitters.Num(); i++)
	{
		TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe> Simulation = Emitters[i];
		FString EmitterName = Simulation->GetEmitterHandle().GetInstance()->GetUniqueEmitterName();
		
		{
			FNiagaraVariable Var = SYS_PARAM_ENGINE_EMITTER_NUM_PARTICLES;
			FString ParamName = Var.GetName().ToString().Replace(TEXT("Emitter"), *EmitterName);
			Var.SetName(*ParamName);
			InstanceParameters.AddParameter(Var, true, false);
			NumParticleVars.Add(Var);
		}
		{
			FNiagaraVariable Var = SYS_PARAM_ENGINE_EMITTER_TOTAL_SPAWNED_PARTICLES;
			FString ParamName = Var.GetName().ToString().Replace(TEXT("Emitter"), *EmitterName);
			Var.SetName(*ParamName);
			InstanceParameters.AddParameter(Var, true, false);
			TotalSpawnedParticlesVars.Add(Var);
		}
	}

	// Make sure all parameters are added before initializing the bindings, otherwise parameter store layout changes might invalidate the bindings.
	OwnerPositionParam.Init(InstanceParameters, SYS_PARAM_ENGINE_POSITION);
	OwnerScaleParam.Init(InstanceParameters, SYS_PARAM_ENGINE_SCALE);
	OwnerVelocityParam.Init(InstanceParameters, SYS_PARAM_ENGINE_VELOCITY);
	OwnerXAxisParam.Init(InstanceParameters, SYS_PARAM_ENGINE_X_AXIS);
	OwnerYAxisParam.Init(InstanceParameters, SYS_PARAM_ENGINE_Y_AXIS);
	OwnerZAxisParam.Init(InstanceParameters, SYS_PARAM_ENGINE_Z_AXIS);

	OwnerRotationParam.Init(InstanceParameters, SYS_PARAM_ENGINE_ROTATION);

	OwnerTransformParam.Init(InstanceParameters, SYS_PARAM_ENGINE_LOCAL_TO_WORLD);
	OwnerInverseParam.Init(InstanceParameters, SYS_PARAM_ENGINE_WORLD_TO_LOCAL);
	OwnerTransposeParam.Init(InstanceParameters, SYS_PARAM_ENGINE_LOCAL_TO_WORLD_TRANSPOSED);
	OwnerInverseTransposeParam.Init(InstanceParameters, SYS_PARAM_ENGINE_WORLD_TO_LOCAL_TRANSPOSED);
	OwnerTransformNoScaleParam.Init(InstanceParameters, SYS_PARAM_ENGINE_LOCAL_TO_WORLD_NO_SCALE);
	OwnerInverseNoScaleParam.Init(InstanceParameters, SYS_PARAM_ENGINE_WORLD_TO_LOCAL_NO_SCALE);

	OwnerDeltaSecondsParam.Init(InstanceParameters, SYS_PARAM_ENGINE_DELTA_TIME);
	OwnerInverseDeltaSecondsParam.Init(InstanceParameters, SYS_PARAM_ENGINE_INV_DELTA_TIME);

	SystemAgeParam.Init(InstanceParameters, SYS_PARAM_ENGINE_SYSTEM_AGE);
	SystemTickCountParam.Init(InstanceParameters, SYS_PARAM_ENGINE_SYSTEM_TICK_COUNT);
	OwnerEngineTimeParam.Init(InstanceParameters, SYS_PARAM_ENGINE_TIME);
	OwnerEngineRealtimeParam.Init(InstanceParameters, SYS_PARAM_ENGINE_REAL_TIME);

	OwnerLODDistanceParam.Init(InstanceParameters, SYS_PARAM_ENGINE_LOD_DISTANCE);
	SystemNumEmittersParam.Init(InstanceParameters, SYS_PARAM_ENGINE_SYSTEM_NUM_EMITTERS);
	SystemNumEmittersAliveParam.Init(InstanceParameters, SYS_PARAM_ENGINE_SYSTEM_NUM_EMITTERS_ALIVE);

	SystemTimeSinceRenderedParam.Init(InstanceParameters, SYS_PARAM_ENGINE_TIME_SINCE_RENDERED);

	OwnerExecutionStateParam.Init(InstanceParameters, SYS_PARAM_ENGINE_EXECUTION_STATE);

	ParameterNumParticleBindings.SetNum(NumParticleVars.Num());
	for (int32 i = 0; i < NumParticleVars.Num(); i++)
	{
		ParameterNumParticleBindings[i].Init(InstanceParameters, NumParticleVars[i]);
	}

	ParameterTotalSpawnedParticlesBindings.SetNum(TotalSpawnedParticlesVars.Num());
	for (int32 i = 0; i < TotalSpawnedParticlesVars.Num(); i++)
	{
		ParameterTotalSpawnedParticlesBindings[i].Init(InstanceParameters, TotalSpawnedParticlesVars[i]);
	}

	// rebind now after all parameters have been added
	InstanceParameters.Rebind();

	TickInstanceParameters(0.01f);

	//Invalidate the component render state so we recreate the scene proxy and the renderers.
	Component->MarkRenderStateDirty();

#if WITH_EDITOR
	//UE_LOG(LogNiagara, Log, TEXT("OnResetInternal %p"), this);
	OnResetDelegate.Broadcast();
#endif

}

FNiagaraSystemInstance::~FNiagaraSystemInstance()
{
	//UE_LOG(LogNiagara, Log, TEXT("~FNiagaraSystemInstance %p"), this);

	//FlushRenderingCommands();

	Cleanup();

// #if WITH_EDITOR
// 	OnDestroyedDelegate.Broadcast();
// #endif
}

void FNiagaraSystemInstance::Cleanup()
{
	if (SystemInstanceIndex != INDEX_NONE)
	{
		TSharedPtr<FNiagaraSystemSimulation, ESPMode::ThreadSafe> SystemSim = GetSystemSimulation();
		SystemSim->RemoveInstance(this);
	}

	DestroyDataInterfaceInstanceData();

	UnbindParameters();

	// Clear out the emitters.
	Emitters.Empty(0);
}

//Unsure on usage of this atm. Possibly useful in future.
// void FNiagaraSystemInstance::RebindParameterCollection(UNiagaraParameterCollectionInstance* OldInstance, UNiagaraParameterCollectionInstance* NewInstance)
// {
// 	OldInstance->GetParameterStore().Unbind(&InstanceParameters);
// 	NewInstance->GetParameterStore().Bind(&InstanceParameters);
// 
// 	for (TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe> Simulation : Emitters)
// 	{
// 		Simulation->RebindParameterCollection(OldInstance, NewInstance);
// 	}
// 
// 	//Have to re init the instance data for data interfaces.
// 	//This is actually lots more work than absolutely needed in some cases so we can improve it a fair bit.
// 	InitDataInterfaces();
// }

void FNiagaraSystemInstance::BindParameters()
{
	if (!Component)
	{
		return;
	}

	Component->GetOverrideParameters().Bind(&InstanceParameters);

	if (SystemSimulation->GetIsSolo())
	{
		// If this simulation is solo than we can bind the instance parameters to the system simulation contexts so that
		// the system and emitter scripts use the per-instance data interfaces.
		Component->GetOverrideParameters().Bind(&SystemSimulation->GetSpawnExecutionContext().Parameters);
		Component->GetOverrideParameters().Bind(&SystemSimulation->GetUpdateExecutionContext().Parameters);
	}
	else 
	{
		// If this simulation is not solo than we have bind the source system parameters to the system simulation contexts so that
		// the system and emitter scripts use the default shared data interfaces.
		GetSystem()->GetExposedParameters().Bind(&SystemSimulation->GetSpawnExecutionContext().Parameters);
		GetSystem()->GetExposedParameters().Bind(&SystemSimulation->GetSpawnExecutionContext().Parameters);
	}

	for (TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe> Simulation : Emitters)
	{
		Simulation->BindParameters();
	}
}

void FNiagaraSystemInstance::UnbindParameters()
{

	if (Component)
	{
		Component->GetOverrideParameters().Unbind(&InstanceParameters);
	}

	if (SystemSimulation.IsValid())
	{
		if (SystemSimulation->GetIsSolo())
		{
			if (Component)
			{
				Component->GetOverrideParameters().Unbind(&SystemSimulation->GetSpawnExecutionContext().Parameters);
				Component->GetOverrideParameters().Unbind(&SystemSimulation->GetUpdateExecutionContext().Parameters);
			}
		}
		else 
		{
			UNiagaraSystem* System = GetSystem();
			if (System)
			{
				System->GetExposedParameters().Unbind(&SystemSimulation->GetSpawnExecutionContext().Parameters);
				System->GetExposedParameters().Unbind(&SystemSimulation->GetSpawnExecutionContext().Parameters);
			}
		}
	}

	for (TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe> Simulation : Emitters)
	{
		Simulation->UnbindParameters();
	}
}

FNiagaraWorldManager* FNiagaraSystemInstance::GetWorldManager()const
{
	return Component ? FNiagaraWorldManager::Get(Component->GetWorld()) : nullptr; 
}

bool FNiagaraSystemInstance::RequiresDistanceFieldData() const
{
	if (!bHasGPUEmitters)
	{
		return false;
	}

	for (const TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>& Emitter : Emitters)
	{
		FNiagaraComputeExecutionContext* GPUContext = Emitter->GetGPUContext();
		if (GPUContext)
		{
			for (UNiagaraDataInterface* DataInterface : GPUContext->CombinedParamStore.GetDataInterfaces())
			{
				if (DataInterface && DataInterface->RequiresDistanceFieldData())
				{
					return true;
				}
			}
		}
	}

	return false;
}

void FNiagaraSystemInstance::InitDataInterfaces()
{
	// If either the System or the component is invalid, it is possible that our cached data interfaces
	// are now bogus and could point to invalid memory. Only the UNiagaraComponent or UNiagaraSystem
	// can hold onto GC references to the DataInterfaces.
	if (GetSystem() == nullptr || IsDisabled())
	{
		return;
	}

	if (Component == nullptr)
	{
		return;
	}

	Component->GetOverrideParameters().Tick();
	
	DestroyDataInterfaceInstanceData();

	GPUDataInterfaceInstanceDataSize = 0;

	//Now the interfaces in the simulations are all correct, we can build the per instance data table.
	int32 InstanceDataSize = 0;
	DataInterfaceInstanceDataOffsets.Empty();
	auto CalcInstDataSize = [&](const TArray<UNiagaraDataInterface*>& Interfaces)
	{
		for (UNiagaraDataInterface* Interface : Interfaces)
		{
			if (!Interface)
			{
				continue;
			}

			if (int32 Size = Interface->PerInstanceDataSize())
			{
				int32* ExistingInstanceDataOffset = DataInterfaceInstanceDataOffsets.Find(Interface);
				if (!ExistingInstanceDataOffset)//Don't add instance data for interfaces we've seen before.
				{
					DataInterfaceInstanceDataOffsets.Add(Interface) = InstanceDataSize;
					// Assume that some of our data is going to be 16 byte aligned, so enforce that 
					// all per-instance data is aligned that way.
					InstanceDataSize += Align(Size, 16);
				}
			}
		}
	};

	CalcInstDataSize(InstanceParameters.GetDataInterfaces());//This probably should be a proper exec context. 

	if (SystemSimulation->GetIsSolo())
	{
		CalcInstDataSize(SystemSimulation->GetSpawnExecutionContext().GetDataInterfaces());
		SystemSimulation->GetSpawnExecutionContext().DirtyDataInterfaces();

		CalcInstDataSize(SystemSimulation->GetUpdateExecutionContext().GetDataInterfaces());
		SystemSimulation->GetUpdateExecutionContext().DirtyDataInterfaces();
	}

	//Iterate over interfaces to get size for table and clear their interface bindings.
	for (TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe> Simulation : Emitters)
	{
		FNiagaraEmitterInstance& Sim = Simulation.Get();
		CalcInstDataSize(Sim.GetSpawnExecutionContext().GetDataInterfaces());
		CalcInstDataSize(Sim.GetUpdateExecutionContext().GetDataInterfaces());
		for (int32 i = 0; i < Sim.GetEventExecutionContexts().Num(); i++)
		{
			CalcInstDataSize(Sim.GetEventExecutionContexts()[i].GetDataInterfaces());
		}

		//Also force a rebind while we're here.
		Sim.DirtyDataInterfaces();
	}

	DataInterfaceInstanceData.SetNumUninitialized(InstanceDataSize);

	bDataInterfacesInitialized = true;
	for (TPair<TWeakObjectPtr<UNiagaraDataInterface>, int32>& Pair : DataInterfaceInstanceDataOffsets)
	{
		if (UNiagaraDataInterface* Interface = Pair.Key.Get())
		{
			check(IsAligned(&DataInterfaceInstanceData[Pair.Value], 16));

			GPUDataInterfaceInstanceDataSize += Pair.Key->PerInstanceDataPassedToRenderThreadSize();

			//Ideally when we make the batching changes, we can keep the instance data in big single type blocks that can all be updated together with a single virtual call.
			bool bResult = Pair.Key->InitPerInstanceData(&DataInterfaceInstanceData[Pair.Value], this);
			bDataInterfacesInitialized &= bResult;
			if (!bResult)
			{
				UE_LOG(LogNiagara, Error, TEXT("Error initializing data interface \"%s\" for system. %u | %s"), *Interface->GetPathName(), Component, *Component->GetAsset()->GetName());
			}
		}
		else
		{
			UE_LOG(LogNiagara, Error, TEXT("A data interface currently in use by an System has been destroyed."));
			bDataInterfacesInitialized = false;
		}
	}

	if (!bDataInterfacesInitialized && (!IsComplete() && !IsPendingSpawn()))
	{
		//Some error initializing the data interfaces so disable until we're explicitly reinitialized.
		UE_LOG(LogNiagara, Error, TEXT("Error initializing data interfaces. Completing system. %u | %s"), Component, *Component->GetAsset()->GetName());
		Complete();
	}
}

bool FNiagaraSystemInstance::GetPerInstanceDataAndOffsets(void*& OutData, uint32& OutDataSize, TMap<TWeakObjectPtr<UNiagaraDataInterface>, int32>*& OutOffsets)
{
	OutData = DataInterfaceInstanceData.GetData();
	OutDataSize = DataInterfaceInstanceData.Num();
	OutOffsets = &DataInterfaceInstanceDataOffsets;
	return DataInterfaceInstanceDataOffsets.Num() != 0;
}

int32 FNiagaraSystemInstance::GetDetailLevel()const
{
	int32 DetailLevel = INiagaraModule::GetDetailLevel();
#if WITH_EDITOR
	if (Component && Component->bEnablePreviewDetailLevel)
	{
		DetailLevel = Component->PreviewDetailLevel;
	}
#endif
	return DetailLevel;
}

void FNiagaraSystemInstance::TickDataInterfaces(float DeltaSeconds, bool bPostSimulate)
{
	if (!GetSystem() || !Component || IsDisabled())
	{
		return;
	}

	bool bReInitDataInterfaces = false;
	if (bPostSimulate)
	{
		for (TPair<TWeakObjectPtr<UNiagaraDataInterface>, int32>& Pair : DataInterfaceInstanceDataOffsets)
		{
			if (UNiagaraDataInterface* Interface = Pair.Key.Get())
			{
				//Ideally when we make the batching changes, we can keep the instance data in big single type blocks that can all be updated together with a single virtual call.
				bReInitDataInterfaces |= Interface->PerInstanceTickPostSimulate(&DataInterfaceInstanceData[Pair.Value], this, DeltaSeconds);
			}
		}
	}
	else
	{
		for (TPair<TWeakObjectPtr<UNiagaraDataInterface>, int32>& Pair : DataInterfaceInstanceDataOffsets)
		{
			if (UNiagaraDataInterface* Interface = Pair.Key.Get())
			{
				//Ideally when we make the batching changes, we can keep the instance data in big single type blocks that can all be updated together with a single virtual call.
				bReInitDataInterfaces |= Interface->PerInstanceTick(&DataInterfaceInstanceData[Pair.Value], this, DeltaSeconds);
			}
		}
	}

	if (bReInitDataInterfaces)
	{
		InitDataInterfaces();
	}
}

float FNiagaraSystemInstance::GetLODDistance()
{
	check(Component);
#if WITH_EDITOR
	if (Component->bEnablePreviewLODDistance)
	{
		return Component->PreviewLODDistance;
	}
#endif

	constexpr float DefaultLODDistance = 0.0f;

	FNiagaraWorldManager* WorldManager = GetWorldManager();
	if ( WorldManager == nullptr )
	{
		return DefaultLODDistance;
	}

	const FVector EffectLocation = Component->GetComponentLocation();

	// If we are inside the WorldManager tick we will use the cache player view locations as we can be ticked on different threads
	if (WorldManager->CachedPlayerViewLocationsValid())
	{
		TArrayView<const FVector> PlayerViewLocations = WorldManager->GetCachedPlayerViewLocations();
		if (PlayerViewLocations.Num() == 0)
		{
			return DefaultLODDistance;
		}

		// We are being ticked inside the WorldManager and can safely use the list of cached player view locations
		float LODDistanceSqr = FMath::Square(WORLD_MAX);
		for (const FVector& ViewLocation : PlayerViewLocations)
		{
			const float DistanceToEffectSqr = FVector(ViewLocation - EffectLocation).SizeSquared();
			LODDistanceSqr = FMath::Min(LODDistanceSqr, DistanceToEffectSqr);
		}
		return FMath::Sqrt(LODDistanceSqr);
	}

	// If we are not inside the WorldManager tick (solo tick) we must look over the player view locations manually
	ensureMsgf(IsInGameThread(), TEXT("FNiagaraSystemInstance::GetLODDistance called in potentially thread unsafe way"));

	if ( UWorld* World = Component->GetWorld() )
	{
		TArray<FVector, TInlineAllocator<8> > PlayerViewLocations;
		if (World->GetPlayerControllerIterator())
		{
			for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
			{
				APlayerController* PlayerController = Iterator->Get();
				if (PlayerController && PlayerController->IsLocalPlayerController())
				{
					FVector* ViewLocation = new(PlayerViewLocations) FVector;
					FRotator ViewRotation;
					PlayerController->GetPlayerViewPoint(*ViewLocation, ViewRotation);
				}
			}
		}
		else
		{
			PlayerViewLocations = World->ViewLocationsRenderedLastFrame;
		}

		if (PlayerViewLocations.Num() > 0)
		{
			float LODDistanceSqr = FMath::Square(WORLD_MAX);
			for (const FVector& ViewLocation : PlayerViewLocations)
			{
				const float DistanceToEffectSqr = FVector(ViewLocation - EffectLocation).SizeSquared();
				LODDistanceSqr = FMath::Min(LODDistanceSqr, DistanceToEffectSqr);
			}
			return FMath::Sqrt(LODDistanceSqr);
		}
	}
	return DefaultLODDistance;
}

void FNiagaraSystemInstance::TickInstanceParameters(float DeltaSeconds)
{
	if (!Component)
	{
		return;
	}

	//TODO: Create helper binding objects to avoid the search in set parameter value.
	//Set System params.
	FTransform ComponentTrans = Component->GetComponentTransform();
	FVector CurrPos = ComponentTrans.GetLocation();
	FVector OldPos = FMath::IsNearlyZero(Age) ? CurrPos : OwnerPositionParam.GetValue(); // The first frame the value in OwnerPositionParam is uninitialized memory, we need to make sure that we don't use it.
	OwnerPositionParam.SetValue(CurrPos);
	OwnerScaleParam.SetValue(ComponentTrans.GetScale3D());
	OwnerVelocityParam.SetValue((CurrPos - OldPos) / DeltaSeconds);
	OwnerXAxisParam.SetValue(ComponentTrans.GetRotation().GetAxisX());
	OwnerYAxisParam.SetValue(ComponentTrans.GetRotation().GetAxisY());
	OwnerZAxisParam.SetValue(ComponentTrans.GetRotation().GetAxisZ());

	OwnerRotationParam.SetValue(ComponentTrans.GetRotation());

	FMatrix Transform = ComponentTrans.ToMatrixWithScale();
	FMatrix Inverse = Transform.Inverse();
	FMatrix Transpose = Transform.GetTransposed();
	FMatrix InverseTranspose = Inverse.GetTransposed();
	OwnerTransformParam.SetValue(Transform);
	OwnerInverseParam.SetValue(Inverse);
	OwnerTransposeParam.SetValue(Transpose);
	OwnerInverseTransposeParam.SetValue(InverseTranspose);

	FMatrix TransformNoScale = ComponentTrans.ToMatrixNoScale();
	FMatrix InverseNoScale = TransformNoScale.Inverse();
	OwnerTransformNoScaleParam.SetValue(TransformNoScale);
	OwnerInverseNoScaleParam.SetValue(InverseNoScale);

	OwnerDeltaSecondsParam.SetValue(DeltaSeconds);
	OwnerInverseDeltaSecondsParam.SetValue(1.0f / DeltaSeconds);

	//Calculate the min distance to a camera.
	UWorld* World = Component->GetWorld();
	if (World != NULL)
	{
		float LODDistance = GetLODDistance();
		OwnerLODDistanceParam.SetValue(LODDistance);
		OwnerEngineTimeParam.SetValue(World->TimeSeconds);
		OwnerEngineRealtimeParam.SetValue(World->RealTimeSeconds);
	}
	else
	{
		OwnerEngineTimeParam.SetValue(Age);
		OwnerEngineRealtimeParam.SetValue(Age);
	}
	SystemAgeParam.SetValue(Age);
	SystemTickCountParam.SetValue(TickCount);

	int32 NumAlive = 0;
	const TArray<FNiagaraEmitterHandle>& EmitterHandles = GetSystem()->GetEmitterHandles();
	for (int32 i = 0; i < Emitters.Num(); i++)
	{
		if (EmitterHandles[i].GetIsEnabled())//TODO: We should just null out the entry to the emitter in the array.
		{
			int32 NumParticles = Emitters[i]->GetNumParticles();
			if (!Emitters[i]->IsComplete())
			{
				NumAlive++;
			}
			ParameterNumParticleBindings[i].SetValue(NumParticles);
			ParameterTotalSpawnedParticlesBindings[i].SetValue(Emitters[i]->GetTotalSpawnedParticles());
		}
	}
	SystemNumEmittersParam.SetValue(Emitters.Num());
	SystemNumEmittersAliveParam.SetValue(NumAlive);

	check(World);
	float WorldTime = World->GetTimeSeconds();
	//Bias the LastRenderTime slightly to account for any delay as it's written by the RT.
	float SafeTimeSinceRendererd = FMath::Max(0.0f, WorldTime - Component->GetLastRenderTime() - GLastRenderTimeSafetyBias);
	SystemTimeSinceRenderedParam.SetValue(SafeTimeSinceRendererd);
	
	OwnerExecutionStateParam.SetValue((int32)RequestedExecutionState);
	
	Component->GetOverrideParameters().Tick();
	InstanceParameters.Tick();
	InstanceParameters.MarkParametersDirty();
}

#if WITH_EDITORONLY_DATA

bool FNiagaraSystemInstance::UsesEmitter(const UNiagaraEmitter* Emitter)const
{
	if (GetSystem())
	{
		return GetSystem()->UsesEmitter(Emitter);
	}
	return false;
}

bool FNiagaraSystemInstance::UsesScript(const UNiagaraScript* Script)const
{
	if (GetSystem())
	{
		for (FNiagaraEmitterHandle EmitterHandle : GetSystem()->GetEmitterHandles())
		{
			if (EmitterHandle.GetInstance() && EmitterHandle.GetInstance()->UsesScript(Script))
			{
				return true;
			}
		}
	}
	return false;
}

// bool FNiagaraSystemInstance::UsesDataInterface(UNiagaraDataInterface* Interface)
// {
// 
// }

bool FNiagaraSystemInstance::UsesCollection(const UNiagaraParameterCollection* Collection)const
{
	if (UNiagaraSystem* System = GetSystem())
	{
		if (System->UsesCollection(Collection))
		{
			return true;
		}
	}
	return false;
}

#endif

void FNiagaraSystemInstance::InitEmitters()
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemInitEmitters);
	if (Component)
	{
		Component->MarkRenderStateDirty();
	}

	bHasGPUEmitters = false;

	Emitters.Empty();
	UNiagaraSystem* System = GetSystem();
	if (System != nullptr)
	{
		const TArray<FNiagaraEmitterHandle>& EmitterHandles = GetSystem()->GetEmitterHandles();
		for (int32 EmitterIdx=0; EmitterIdx < GetSystem()->GetEmitterHandles().Num(); ++EmitterIdx)
		{
			const FNiagaraEmitterHandle& EmitterHandle = EmitterHandles[EmitterIdx];

			TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe> Sim = MakeShared<FNiagaraEmitterInstance, ESPMode::ThreadSafe>(this);
			Sim->Init(EmitterIdx, IDName);
			if (System->bFixedBounds)
			{
				Sim->SetSystemFixedBoundsOverride(System->GetFixedBounds());
			}
			Emitters.Add(Sim);
		}

		for (TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe> Simulation : Emitters)
		{
			bHasGPUEmitters |= Simulation->GetCachedEmitter()->SimTarget == ENiagaraSimTarget::GPUComputeSim;

			Simulation->PostInitSimulation();
		}
	}
}

void FNiagaraSystemInstance::ComponentTick(float DeltaSeconds)
{
	if (IsDisabled())
	{
		return;
	}

	TSharedPtr<FNiagaraSystemSimulation, ESPMode::ThreadSafe> Sim = GetSystemSimulation();
	check(Sim.IsValid());
	check(IsInGameThread());
	check(bSolo);
	check(Component);
	
	TSharedPtr<FNiagaraSystemSimulation, ESPMode::ThreadSafe> SystemSim = GetSystemSimulation();
	SystemSim->Tick(DeltaSeconds);
}

void FNiagaraSystemInstance::FinalizeTick(float DeltaSeconds)
{
	//Post tick our interfaces.
	TickDataInterfaces(DeltaSeconds, true);

	if (Component)
	{
		if (HasTickingEmitters())
		{
			Component->UpdateComponentToWorld();//Needed for bounds updates. Can probably skip if using fixed bounds
			Component->MarkRenderDynamicDataDirty();
		}
	}
}

bool FNiagaraSystemInstance::HandleCompletion()
{
	bool bEmittersCompleteOrDisabled = true;
	bHasTickingEmitters = false;
	for (TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>&it : Emitters)
	{
		FNiagaraEmitterInstance& Inst = *it;
		bEmittersCompleteOrDisabled &= Inst.HandleCompletion();
		bHasTickingEmitters |= Inst.ShouldTick();
	}

	bool bCompletedAlready = IsComplete();
	if (bCompletedAlready || bEmittersCompleteOrDisabled)
	{
		//UE_LOG(LogNiagara, Log, TEXT("Completion Achieved"));
		Complete();
		return true;
	}
	return false;
}

void FNiagaraSystemInstance::PreSimulateTick(float DeltaSeconds)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemPreSimulateTick);
	UNiagaraSystem* System = GetSystem();
	FScopeCycleCounter SystemStat(System->GetStatID(true, true));

	TickInstanceParameters(DeltaSeconds);

	Age += DeltaSeconds;
	TickCount += 1;
}

void FNiagaraSystemInstance::PostSimulateTick(float DeltaSeconds)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemInstanceTick);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_GT_CNC);

	// Reset values that will be accumulated during emitter tick.
	TotalParamSize = 0;
	ActiveGPUEmitterCount = 0;
	UNiagaraSystem* System = GetSystem();

	if (IsComplete() || !bHasTickingEmitters || GetSystem() == nullptr || Component == nullptr || DeltaSeconds < SMALL_NUMBER)
	{
		return;
	}

	FScopeCycleCounter SystemStat(System->GetStatID(true, true));

	for (int32 EmitterIdx = 0; EmitterIdx < Emitters.Num(); EmitterIdx++)
	{
		FNiagaraEmitterInstance& Inst = Emitters[EmitterIdx].Get();
		Inst.PreTick();
	}

	// now tick all emitters
	for (int32 EmitterIdx = 0; EmitterIdx < Emitters.Num(); EmitterIdx++)
	{
		FNiagaraEmitterInstance& Inst = Emitters[EmitterIdx].Get();
		Inst.Tick(DeltaSeconds);

		if (Inst.GetCachedEmitter()->SimTarget == ENiagaraSimTarget::GPUComputeSim && Inst.GetGPUContext() != nullptr && (Inst.GetExecutionState() != ENiagaraExecutionState::Complete))
		{
			TotalParamSize += Inst.GetGPUContext()->CombinedParamStore.GetPaddedParameterSizeInBytes();
			ActiveGPUEmitterCount++;
		}
	}
}

#if WITH_EDITORONLY_DATA
bool FNiagaraSystemInstance::GetIsolateEnabled() const
{
	UNiagaraSystem* System = GetSystem();
	if (System)
	{
		return System->GetIsolateEnabled();
	}
	return false;
}
#endif

void FNiagaraSystemInstance::DestroyDataInterfaceInstanceData()
{
	for (TPair<TWeakObjectPtr<UNiagaraDataInterface>, int32>& Pair : DataInterfaceInstanceDataOffsets)
	{
		if (UNiagaraDataInterface* Interface = Pair.Key.Get())
		{
			Interface->DestroyPerInstanceData(&DataInterfaceInstanceData[Pair.Value], this);
		}
	}
	DataInterfaceInstanceDataOffsets.Empty();
	DataInterfaceInstanceData.Empty();
}

TSharedPtr<FNiagaraEmitterInstance, ESPMode::ThreadSafe> FNiagaraSystemInstance::GetSimulationForHandle(const FNiagaraEmitterHandle& EmitterHandle)
{
	for (TSharedPtr<FNiagaraEmitterInstance, ESPMode::ThreadSafe> Sim : Emitters)
	{
		if(Sim->GetEmitterHandle().GetId() == EmitterHandle.GetId())
		{
			return Sim;
		}
	}
	return nullptr;
}

UNiagaraSystem* FNiagaraSystemInstance::GetSystem()const
{
	if (Component)
	{
		return Component->GetAsset();
	}
	else
	{
		return nullptr;
	}
}

FNiagaraEmitterInstance* FNiagaraSystemInstance::GetEmitterByID(FGuid InID)
{
	for (TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>& Emitter : Emitters)
	{
		if (Emitter->GetEmitterHandle().GetId() == InID)
		{
			return &Emitter.Get();
		}
	}
	return nullptr;
}

FNiagaraDataSet* FNiagaraSystemInstance::GetDataSet(FNiagaraDataSetID SetID, FName EmitterName)
{
	if (EmitterName == NAME_None)
	{
		if (FNiagaraDataSet* ExternalSet = ExternalEvents.Find(SetID))
		{
			return ExternalSet;
		}
	}
	for (TSharedPtr<FNiagaraEmitterInstance, ESPMode::ThreadSafe> Emitter : Emitters)
	{
		check(Emitter.IsValid());
		if (!Emitter->IsComplete())
		{
			if (Emitter->GetCachedIDName() == EmitterName)
			{
				return Emitter->GetDataSet(SetID);
			}
		}
	}

	return NULL;
}

FNiagaraSystemInstance::FOnInitialized& FNiagaraSystemInstance::OnInitialized()
{
	return OnInitializedDelegate;
}

FNiagaraSystemInstance::FOnComplete& FNiagaraSystemInstance::OnComplete()
{
	return OnCompleteDelegate;
}

#if WITH_EDITOR
FNiagaraSystemInstance::FOnReset& FNiagaraSystemInstance::OnReset()
{
	return OnResetDelegate;
}

FNiagaraSystemInstance::FOnDestroyed& FNiagaraSystemInstance::OnDestroyed()
{
	return OnDestroyedDelegate;
}
#endif
