// Copyright Epic Games, Inc. All Rights Reserved.

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
#include "NiagaraGpuComputeDebug.h"
#include "Templates/AlignmentTemplates.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "GameFramework/PlayerController.h"
#include "NiagaraCrashReporterHandler.h"
#include "Async/Async.h"

#if WITH_EDITORONLY_DATA
#include "Editor.h"
#endif

DECLARE_CYCLE_STAT(TEXT("System Activate [GT]"), STAT_NiagaraSystemActivate, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Deactivate [GT]"), STAT_NiagaraSystemDeactivate, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Complete [GT]"), STAT_NiagaraSystemComplete, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Reset [GT]"), STAT_NiagaraSystemReset, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Reinit [GT]"), STAT_NiagaraSystemReinit, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Init Emitters [GT]"), STAT_NiagaraSystemInitEmitters, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Advance Simulation [GT] "), STAT_NiagaraSystemAdvanceSim, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System SetSolo[GT] "), STAT_NiagaraSystemSetSolo, STATGROUP_Niagara);

//High level stats for system instance ticks.
DECLARE_CYCLE_STAT(TEXT("System Instance Tick (Component) [GT]"), STAT_NiagaraSystemInst_ComponentTickGT, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Instance Tick [GT]"), STAT_NiagaraSystemInst_TickGT, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Instance Tick [CNC]"), STAT_NiagaraSystemInst_TickCNC, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Instance Finalize [GT]"), STAT_NiagaraSystemInst_FinalizeGT, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Instance WaitForAsyncTick [GT]"), STAT_NiagaraSystemWaitForAsyncTick, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Instance ProcessComponentRendererTasks [GT]"), STAT_NiagaraProcessComponentRendererTasks, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Instance ComponentRendererSpawning [GT]"), STAT_NiagaraComponentRendererSpawning, STATGROUP_Niagara);

DECLARE_CYCLE_STAT(TEXT("InitGPUSystemTick"), STAT_NiagaraInitGPUSystemTick, STATGROUP_Niagara);

static float GWaitForAsyncStallWarnThresholdMS = 0.2f;
static FAutoConsoleVariableRef CVarWaitForAsyncStallWarnThresholdMS(
	TEXT("fx.WaitForAsyncStallWarnThresholdMS"),
	GWaitForAsyncStallWarnThresholdMS,
	TEXT("If we stall in WaitForAsync for longer than this threshold then we emit a stall warning message."),
	ECVF_Default
);

/** Safety time to allow for the LastRenderTime coming back from the RT. This is overkill but that's ok.*/
float GLastRenderTimeSafetyBias = 0.1f;
static FAutoConsoleVariableRef CVarLastRenderTimeSafetyBias(
	TEXT("fx.LastRenderTimeSafetyBias"),
	GLastRenderTimeSafetyBias,
	TEXT("The time to bias the LastRenderTime value to allow for the delay from it being written by the RT."),
	ECVF_Default
);

static int GNiagaraForceLastTickGroup = 0;
static FAutoConsoleVariableRef CVarNiagaraForceLastTickGroup(
	TEXT("fx.Niagara.ForceLastTickGroup"),
	GNiagaraForceLastTickGroup,
	TEXT("Force Niagara ticks to be in the last tick group, this mirrors old behavour and can be useful to test for async overlapping issues."),
	ECVF_Default
);

static float GNiagaraBoundsExpandByPercent = 0.1f;
static FAutoConsoleVariableRef CVarNiagaraBoundsExpandByPercent(
	TEXT("fx.Niagara.BoundsExpandByPercent"),
	GNiagaraBoundsExpandByPercent,
	TEXT("The percentage we expand the bounds to avoid updating every frame."),
	ECVF_Default
);

static int GNiagaraWarnComponentRenderCount = 50;
static FAutoConsoleVariableRef CVarNiagaraWarnComponentRenderCount(
	TEXT("fx.Niagara.WarnComponentRenderCount"),
	GNiagaraWarnComponentRenderCount,
	TEXT("The max number of components that a single system can spawn before a log warning is shown."),
	ECVF_Default
	);

static float GNiagaraComponentRenderPoolInactiveTimeLimit = 5;
static FAutoConsoleVariableRef CVarNiagaraComponentRenderPoolInactiveTimeLimit(
	TEXT("fx.Niagara.ComponentRenderPoolInactiveTimeLimit"),
	GNiagaraComponentRenderPoolInactiveTimeLimit,
	TEXT("The time in seconds an inactive component can linger in the pool before being destroyed."),
	ECVF_Default
	);

static int GNiagaraAllowDeferredReset = 1;
static FAutoConsoleVariableRef CVarNiagaraAllowDeferredReset(
	TEXT("fx.Niagara.AllowDeferredReset"),
	GNiagaraAllowDeferredReset,
	TEXT("If we are running async work when a reset is requested we will instead queue for the finalize to perform, this avoid stalling the GameThread."),
	ECVF_Default
);

FNiagaraSystemInstance::FNiagaraSystemInstance(UWorld& InWorld, UNiagaraSystem& InAsset, FNiagaraUserRedirectionParameterStore* InOverrideParameters,
	USceneComponent* InAttachComponent, ENiagaraTickBehavior InTickBehavior, bool bInPooled)
	: SystemInstanceIndex(INDEX_NONE)
	, SignificanceIndex(INDEX_NONE)
	, World(&InWorld)
	, Asset(&InAsset)
	, OverrideParameters(InOverrideParameters)
	, AttachComponent(InAttachComponent)
	, PrereqComponent(nullptr)
	, TickBehavior(InTickBehavior)
	, Age(0.0f)
	, LastRenderTime(0.0f)
	, TickCount(0)
	, LODDistance(0.0f)
	, MaxLODDistance(FLT_MAX)
	, CurrentFrameIndex(1)
	, ParametersValid(false)
	, bSolo(false)
	, bForceSolo(false)
	, bPendingSpawn(false)
	, bPaused(false)
	, bDataInterfacesHaveTickPrereqs(false)
	, bNeedsFinalize(false)
	, bDataInterfacesInitialized(false)
	, bAlreadyBound(false)
	, bLODDistanceIsValid(false)
	, bPooled(bInPooled)
	, bHasSimulationReset(false)
	, bAsyncWorkInProgress(false)
	, CachedDeltaSeconds(0.0f)
	, RequestedExecutionState(ENiagaraExecutionState::Complete)
	, ActualExecutionState(ENiagaraExecutionState::Complete)
	, FeatureLevel(GMaxRHIFeatureLevel)
{
	static TAtomic<uint64> IDCounter(1);
	ID = IDCounter.IncrementExchange();

	LocalBounds = FBox(FVector::ZeroVector, FVector::ZeroVector);
	if (InAttachComponent)
	{
		InstanceParameters.SetOwner(InAttachComponent);
	}

	if (World->Scene)
	{
		FFXSystemInterface*  FXSystemInterface = World->Scene->GetFXSystem();
		if (FXSystemInterface)
		{
			Batcher = static_cast<NiagaraEmitterInstanceBatcher*>(FXSystemInterface->GetInterface(NiagaraEmitterInstanceBatcher::Name));
		}
		FeatureLevel = World->FeatureLevel;
	}
		
	// In some cases the system may have already stated that you should ignore dependencies and tick as early as possible.		
	if (!InAsset.bRequireCurrentFrameData)
	{
		TickBehavior = ENiagaraTickBehavior::ForceTickFirst;
	}

#if WITH_EDITORONLY_DATA
	if (GEditor)
	{
		// for the component renderer we need to listen for class changes so we can clean up old component renderer instances
		GEditor->OnObjectsReplaced().AddRaw(this, &FNiagaraSystemInstance::OnObjectsReplacedCallback);
	}
#endif
}


void FNiagaraSystemInstance::SetEmitterEnable(FName EmitterName, bool bNewEnableState)
{
	// No need fo this code since it's not supported yet
	//// Wait for any async operations, can complete the system
	//WaitForAsyncTickAndFinalize();
	//if (IsComplete())
	//{
	//	return;
	//}


	UE_LOG(LogNiagara, Warning, TEXT("SetEmitterEnable: Emitter \"%s\" is not currently implemented."), *EmitterName.ToString());
	return;

	/*
	UNiagaraSystem* System = GetSystem();
	if (System != nullptr)
	{
		const TArray<FNiagaraEmitterHandle>& EmitterHandles = GetSystem()->GetEmitterHandles();
		int32 FoundIdx = INDEX_NONE;
		for (int32 EmitterIdx = 0; EmitterIdx < GetSystem()->GetEmitterHandles().Num(); ++EmitterIdx)
		{
			const FNiagaraEmitterHandle& EmitterHandle = EmitterHandles[EmitterIdx];
			if (EmitterName == EmitterHandle.GetName())
			{
				FoundIdx = EmitterIdx;
				break;
			}
		}

		if (FoundIdx != INDEX_NONE && Emitters.IsValidIndex(FoundIdx))
		{
			if (Emitters[FoundIdx]->IsAllowedToExecute())
			{
				
				{
					if (bNewEnableState)
					{
						Emitters[FoundIdx]->SetExecutionState(ENiagaraExecutionState::Active);
					}
					else
					{
						Emitters[FoundIdx]->SetExecutionState(ENiagaraExecutionState::Inactive);
					}
				}
			}
			else
			{
				UE_LOG(LogNiagara, Log, TEXT("SetEmitterEnable: Emitter \"%s\" was found in the system's list of emitters, but it does not pass FNiagaraEmitterInstance::IsAllowedToExecute() and therefore cannot be manually enabled!"), *EmitterName.ToString());
			}
		}
		else
		{
			UE_LOG(LogNiagara, Log, TEXT("SetEmitterEnable: Emitter \"%s\" was not found in the system's list of emitters!"), *EmitterName.ToString());
		}
	}*/
}


void FNiagaraSystemInstance::Init(bool bInForceSolo)
{
	// We warn if async is not complete here as we should never wait
	WaitForAsyncTickAndFinalize(true);

	bForceSolo = bInForceSolo;
	ActualExecutionState = ENiagaraExecutionState::Inactive;
	RequestedExecutionState = ENiagaraExecutionState::Inactive;
	bAlreadyBound = false;

	//InstanceParameters = GetSystem()->GetInstanceParameters();
	// In order to get user data interface parameters in the component to work properly,
	// we need to bind here, otherwise the instances when we init data interfaces during reset will potentially
	// be the defaults (i.e. null) for things like static mesh data interfaces.
	Reset(EResetMode::ReInit);

#if WITH_EDITORONLY_DATA
	InstanceParameters.DebugName = *FString::Printf(TEXT("SystemInstance %p"), this);
#endif
#if WITH_EDITOR
	OnInitializedDelegate.Broadcast();
#endif
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

void FNiagaraSystemInstance::DumpTickInfo(FOutputDevice& Ar)
{
	static const UEnum* TickingGroupEnum = FindObjectChecked<UEnum>(ANY_PACKAGE, TEXT("ETickingGroup"));

	FString PrereqInfo;
	if (PrereqComponent != nullptr)
	{
		ETickingGroup PrereqTG = FMath::Max(PrereqComponent->PrimaryComponentTick.TickGroup, PrereqComponent->PrimaryComponentTick.EndTickGroup);
		PrereqInfo.Appendf(TEXT(" PreReq(%s = %s)"), *PrereqComponent->GetFullName(), *TickingGroupEnum->GetNameStringByIndex(PrereqTG));
	}

	if (bDataInterfacesHaveTickPrereqs)
	{
		for (TPair<TWeakObjectPtr<UNiagaraDataInterface>, int32>& Pair : DataInterfaceInstanceDataOffsets)
		{
			if (UNiagaraDataInterface* Interface = Pair.Key.Get())
			{
				ETickingGroup PrereqTG = Interface->CalculateTickGroup(&DataInterfaceInstanceData[Pair.Value]);
				PrereqInfo.Appendf(TEXT(" DataInterface(%s = %s)"), *Interface->GetFullName(), *TickingGroupEnum->GetNameStringByIndex(PrereqTG));
			}
		}
	}

	Ar.Logf(TEXT("\t\t\tInstance%s"), *PrereqInfo);
}

#if WITH_EDITORONLY_DATA
bool FNiagaraSystemInstance::RequestCapture(const FGuid& RequestId)
{
	if (IsComplete() || CurrentCapture.IsValid())
	{
		return false;
	}

	// Wait for any async operations, can complete the system
	WaitForAsyncTickAndFinalize();
	if (IsComplete())
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
		if (Handle.GetInstance() && Handle.GetIsEnabled())
		{
			Handle.GetInstance()->GetScripts(Scripts, false);

			for (UNiagaraScript* Script : Scripts)
			{
				if (Script->IsGPUScript(Script->Usage) && Handle.GetInstance()->SimTarget == ENiagaraSimTarget::CPUSim)
					continue;
				TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe> DebugInfoPtr = MakeShared<FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe>(Handle.GetIdName(), Script->GetUsage(), Script->GetUsageId());
				DebugInfoPtr->bWritten = false;

				TempCaptureHolder->Add(DebugInfoPtr);
			}
		}
	}
	CapturedFrames.Add(RequestId, TempCaptureHolder);
	CurrentCapture = TempCaptureHolder;
	CurrentCaptureGuid = MakeShared<FGuid, ESPMode::ThreadSafe>(RequestId);
	return true;
}

void FNiagaraSystemInstance::FinishCapture()
{
	// Wait for any async operations, can complete the system
	WaitForAsyncTickAndFinalize();

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
	// Wait for any async operations, can complete the system
	WaitForAsyncTickAndFinalize();

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

	// Wait for any async operations
	WaitForAsyncTickDoNotFinalize();

	UNiagaraSystem* System = GetSystem();
	if (bInSolo)
	{
		TSharedPtr<FNiagaraSystemSimulation, ESPMode::ThreadSafe> NewSoloSim = MakeShared<FNiagaraSystemSimulation, ESPMode::ThreadSafe>();
		NewSoloSim->Init(System, World, true, TG_MAX);

		NewSoloSim->TransferInstance(SystemSimulation.Get(), this);	

		SystemSimulation = NewSoloSim;
		bSolo = true;
	}
	else
	{
		UpdatePrereqs();
		const ETickingGroup TickGroup = CalculateTickGroup();
		TSharedPtr<FNiagaraSystemSimulation, ESPMode::ThreadSafe> NewSim = GetWorldManager()->GetSystemSimulation(TickGroup, System);

		NewSim->TransferInstance(SystemSimulation.Get(), this);
		
		SystemSimulation = NewSim;
		bSolo = false;
	}

	// Execute any pending finalize
	FinalizeTick_GameThread();
}

void FNiagaraSystemInstance::SetGpuComputeDebug(bool bEnableDebug)
{
#if WITH_EDITOR
	UNiagaraSystem* System  = GetSystem();
	if (Batcher == nullptr || System == nullptr)
	{
		return;
	}

	if (bEnableDebug)
	{
		FString SystemName = System->GetName();
		if (USceneComponent* Owner = AttachComponent.Get())
		{
			SystemName.Append(TEXT("/"));
			if (AActor* Actor = Owner->GetTypedOuter<AActor>())
			{
				SystemName.Append(GetNameSafe(Actor));
			}
			else
			{
				SystemName.Append(GetNameSafe(Owner));
			}
		}

		ENQUEUE_RENDER_COMMAND(NiagaraAddGPUSystemDebug)
		(
			[RT_Batcher=Batcher, RT_InstanceID=GetId(), RT_SystemName=SystemName](FRHICommandListImmediate& RHICmdList)
			{
				if (FNiagaraGpuComputeDebug* GpuComputeDebug = RT_Batcher->GetGpuComputeDebug())
				{
					GpuComputeDebug->AddSystemInstance(RT_InstanceID, RT_SystemName);
				}
			}
		);
	}
	else
	{
		ENQUEUE_RENDER_COMMAND(NiagaraRemoveGPUSystemDebug)
		(
			[RT_Batcher=Batcher, RT_InstanceID=GetId()](FRHICommandListImmediate& RHICmdList)
			{
				if (FNiagaraGpuComputeDebug* GpuComputeDebug = RT_Batcher->GetGpuComputeDebug())
				{
					GpuComputeDebug->RemoveSystemInstance(RT_InstanceID);
				}
			}
		);
	}
#endif
}

void FNiagaraSystemInstance::UpdatePrereqs()
{
	PrereqComponent = AttachComponent.Get();

	// This is to maintain legacy behavior (and perf benefit) of ticking in PrePhysics with unattached UNiagaraComponents that have no DI prereqs
	// NOTE: This means that the system likely ticks with frame-behind transform if the component is moved, but likely doesn't manifest as an issue with local-space emitters
	// TODO: Is there a better way to detect being able to tick early for these perf wins by default, even when not using a NiagaraComponent?
	if (UNiagaraComponent* NiagaraComponent = Cast<UNiagaraComponent>(PrereqComponent))
	{
		PrereqComponent = NiagaraComponent->GetAttachParent();
	}
}

void FNiagaraSystemInstance::Activate(EResetMode InResetMode)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemActivate);

	UNiagaraSystem* System = GetSystem();
	if (System && System->IsValid() && IsReadyToRun())
	{
		if (GNiagaraAllowDeferredReset && (bAsyncWorkInProgress || bNeedsFinalize) && SystemInstanceIndex != INDEX_NONE)
		{
			DeferredResetMode = InResetMode;
		}
		else
		{
			// Wait for any async operations, can complete the system
			WaitForAsyncTickAndFinalize();

			DeferredResetMode = EResetMode::None;
			Reset(InResetMode);			
		}
	}
	else
	{
		SetRequestedExecutionState(ENiagaraExecutionState::Disabled);
	}
}

void FNiagaraSystemInstance::Deactivate(bool bImmediate)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemDeactivate);

	// Clear our pending reset mode
	DeferredResetMode = EResetMode::None;

	if (bImmediate)
	{
		// Wait for any async operations, can complete the system
		WaitForAsyncTickAndFinalize();

		if (!IsComplete())
		{
			Complete(true);
		}
	}
	else
	{
		SetRequestedExecutionState(ENiagaraExecutionState::Inactive);
	}
}

bool FNiagaraSystemInstance::AllocateSystemInstance(TUniquePtr<FNiagaraSystemInstance>& OutSystemInstanceAllocation, UWorld& InWorld, UNiagaraSystem& InAsset,
	FNiagaraUserRedirectionParameterStore* InOverrideParameters, USceneComponent* InAttachComponent, ENiagaraTickBehavior InTickBehavior, bool bInPooled)
{
	OutSystemInstanceAllocation = MakeUnique<FNiagaraSystemInstance>(InWorld, InAsset, InOverrideParameters, InAttachComponent, InTickBehavior, bInPooled);
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
		SystemInstanceAllocation->UnbindParameters();

		// If we have active GPU emitters make sure we remove any pending ticks from the RT
		NiagaraEmitterInstanceBatcher* InstanceBatcher = SystemInstanceAllocation->GetBatcher();
		if (SystemInstanceAllocation->bHasGPUEmitters)
		{
			ENQUEUE_RENDER_COMMAND(NiagaraRemoveGPUSystem)
			(
				[InstanceBatcher, InstanceID=SystemInstanceAllocation->GetId()](FRHICommandListImmediate& RHICmdList) mutable
				{
					InstanceBatcher->InstanceDeallocated_RenderThread(InstanceID);
				}
			);
		}
		
		// Queue deferred deletion from the WorldManager
		FNiagaraWorldManager* WorldManager = SystemInstanceAllocation->GetWorldManager();
		check(WorldManager != nullptr);

		// Make sure we abandon any external interface at this point
		SystemInstanceAllocation->OverrideParameters = nullptr;
		SystemInstanceAllocation->PrereqComponent = nullptr;
		SystemInstanceAllocation->OnPostTickDelegate.Unbind();
		SystemInstanceAllocation->OnCompleteDelegate.Unbind();

		WorldManager->DestroySystemInstance(SystemInstanceAllocation);
		check(SystemInstanceAllocation == nullptr);
	}
	SystemInstanceAllocation = nullptr;
	
	return true;
}

void FNiagaraSystemInstance::Complete(bool bExternalCompletion)
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

	ResetComponentRenderPool();

	DestroyDataInterfaceInstanceData();

	if (!bPooled)
	{
		UnbindParameters(true);
	}

	bPendingSpawn = false;

	if (bNeedToNotifyOthers)
	{
		// We've already notified once, no need to do so again.
		bNotifyOnCompletion = false;
		if (OnCompleteDelegate.IsBound())
		{
			OnCompleteDelegate.Execute(bExternalCompletion);
		}
	}
}

void FNiagaraSystemInstance::OnPooledReuse(UWorld& NewWorld)
{
	World = &NewWorld;
	for (auto&& Emitter : Emitters)
	{
		Emitter->OnPooledReuse();
	}
}

void FNiagaraSystemInstance::SetPaused(bool bInPaused)
{
	if (bInPaused == bPaused)
	{
		return;
	}

	// Wait for any async operations, can complete the system
	WaitForAsyncTickAndFinalize();

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
	FScopeCycleCounterUObject AdditionalScope(GetSystem(), GET_STATID(STAT_NiagaraSystemReset));

	if (Mode == EResetMode::None)
	{
		// Right now we don't support binding with reset mode none.
		/*if (Mode == EResetMode::None && bBindParams)
		{
			BindParameters();
		}*/
		return;
	}

	// Wait for any async operations, can complete the system
	WaitForAsyncTickAndFinalize();

	LastRenderTime = World->GetTimeSeconds();

	SetPaused(false);

	if (SystemSimulation.IsValid())
	{
		SystemSimulation->RemoveInstance(this);
		bNeedsFinalize = false;
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
	
	//If none of our emitters actually made it out of the init process we can just bail here before we ever tick.
	bool bHasActiveEmitters = false;
	for (auto& Inst : Emitters)
	{
		if (!Inst->IsComplete())
		{
			bHasActiveEmitters = true;
			break;
		}
	}

	SetRequestedExecutionState(ENiagaraExecutionState::Active);
	if (bHasActiveEmitters)
	{
		if (bBindParams)
		{
			ResetParameters();
			BindParameters();
		}

		SetActualExecutionState(ENiagaraExecutionState::Active);

		if (bBindParams)
		{
			InstanceParameters.Tick();//Make sure the owner has flushed it's parameters by now. Especially it's DIs.
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
	}
	else
	{
		SetActualExecutionState(ENiagaraExecutionState::Complete);
		Complete(true);
	}
}

void FNiagaraSystemInstance::ResetInternal(bool bResetSimulations)
{
	check(SystemInstanceIndex == INDEX_NONE);
	ensure(bPendingSpawn == false);
	ensure(bPaused == false);
	ensure(bAsyncWorkInProgress == false);
	ensure(bNeedsFinalize == false);

	Age = 0;
	TickCount = 0;
	bHasSimulationReset = bResetSimulations;
	CachedDeltaSeconds = 0.0f;
	bLODDistanceIsValid = false;
	TotalGPUParamSize = 0;
	ActiveGPUEmitterCount = 0;
	GPUParamIncludeInterpolation = false;
	// Note: We do not need to update our bounds here as they are still valid

	UNiagaraSystem* System = GetSystem();
	if (System == nullptr || IsDisabled())
	{
		return;
	}

#if WITH_EDITOR
	check(World);
	if (OverrideParameters && World->WorldType == EWorldType::Editor)
	{
		OverrideParameters->Tick();
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
		UE_LOG(LogNiagara, Warning, TEXT("Failed to activate Niagara System due to invalid asset! System(%s) Component(%s)"), *System->GetName(), *GetFullNameSafe(AttachComponent.Get()));
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
	if (TickCountToSimulate > 0 && !IsPaused())
	{
		// Wait for any async operations, can complete the system
		WaitForAsyncTickAndFinalize();
		if (IsComplete())
		{
			return;
		}

		SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemAdvanceSim);
		bool bWasSolo = bSolo;
		SetSolo(true);

		for (int32 TickIdx = 0; TickIdx < TickCountToSimulate; ++TickIdx)
		{
			//Cannot do multiple tick off the game thread here without additional work. So we pass in null for the completion event which will force GT execution.
			//If this becomes a perf problem I can add a new path for the tick code to handle multiple ticks.
			ManualTick(TickDeltaSeconds, nullptr);
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

bool DoSystemDataInterfacesRequireSolo(const UNiagaraSystem& System, const FNiagaraUserRedirectionParameterStore* OverrideParameters)
{
	if (FNiagaraSystemSimulation::UseLegacySystemSimulationContexts())
	{
		if (System.HasSystemScriptDIsWithPerInstanceData())
		{
			return true;
		}

		const TArray<FName>& UserDINamesReadInSystemScripts = System.GetUserDINamesReadInSystemScripts();
		if (OverrideParameters != nullptr && UserDINamesReadInSystemScripts.Num() > 0)
		{
			TArray<FNiagaraVariable> OverrideParameterVariables;
			OverrideParameters->GetParameters(OverrideParameterVariables);
			for (const FNiagaraVariable& OverrideParameterVariable : OverrideParameterVariables)
			{
				if (OverrideParameterVariable.IsDataInterface() && UserDINamesReadInSystemScripts.Contains(OverrideParameterVariable.GetName()))
				{
					if (UserDINamesReadInSystemScripts.Contains(OverrideParameterVariable.GetName()))
					{
						return true;
					}
				}
			}
		}
	}
	return false;
}

void FNiagaraSystemInstance::ReInitInternal()
{
	check(SystemInstanceIndex == INDEX_NONE);
	ensure(bPendingSpawn == false);
	ensure(bPaused == false);
	ensure(bAsyncWorkInProgress == false);
	ensure(bNeedsFinalize == false);

	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemReinit);

	SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_GT);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Effects);
	LLM_SCOPE(ELLMTag::Niagara);

	Age = 0;
	TickCount = 0;
	bHasSimulationReset = true;
	LocalBounds = FBox(FVector::ZeroVector, FVector::ZeroVector);
	CachedDeltaSeconds = 0.0f;
	bAlreadyBound = false;

	UNiagaraSystem* System = GetSystem();
	if (System == nullptr)
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
		UE_LOG(LogNiagara, Warning, TEXT("Failed to activate Niagara System due to invalid asset! System(%s) Component(%s)"), *System->GetName(), *GetFullNameSafe(AttachComponent.Get()));
		return;
	}

	/** Do we need to run in solo mode? */
	bSolo = bForceSolo || DoSystemDataInterfacesRequireSolo(*System, OverrideParameters);
	if (bSolo)
	{
		if (!SystemSimulation.IsValid())
		{
			SystemSimulation = MakeShared<FNiagaraSystemSimulation, ESPMode::ThreadSafe>();
			SystemSimulation->Init(System, World, true, TG_MAX);
		}
	}
	else
	{
		UpdatePrereqs();
		const ETickingGroup TickGroup = CalculateTickGroup();
		SystemSimulation = GetWorldManager()->GetSystemSimulation(TickGroup, System);
	}

	// Make sure that we've gotten propagated instance parameters before calling InitEmitters, as they might bind to them.
	const FNiagaraSystemCompiledData& SystemCompiledData = System->GetSystemCompiledData();
	InstanceParameters = SystemCompiledData.InstanceParamStore;


	//When re initializing, throw away old emitters and init new ones.
	Emitters.Reset();
	InitEmitters();

	// rebind now after all parameters have been added
	InstanceParameters.Rebind();

	TickInstanceParameters_GameThread(0.01f);
	TickInstanceParameters_Concurrent();

#if WITH_EDITOR
	//UE_LOG(LogNiagara, Log, TEXT("OnResetInternal %p"), this);
	OnResetDelegate.Broadcast();
#endif

}

void FNiagaraSystemInstance::ResetParameters()
{
	UNiagaraSystem* System = GetSystem();
	if (!System)
	{
		return;
	}

	CurrentFrameIndex = 1;
	ParametersValid = false;

	GlobalParameters[0] = FNiagaraGlobalParameters();
	GlobalParameters[1] = FNiagaraGlobalParameters();
	SystemParameters[0] = FNiagaraSystemParameters();
	SystemParameters[1] = FNiagaraSystemParameters();
	OwnerParameters[0] = FNiagaraOwnerParameters();
	OwnerParameters[1] = FNiagaraOwnerParameters();

	EmitterParameters.Reset(Emitters.Num() * 2);
	EmitterParameters.AddDefaulted(Emitters.Num() * 2);
	GatheredInstanceParameters.Init(Emitters.Num());
}

FNiagaraSystemInstance::~FNiagaraSystemInstance()
{
	//UE_LOG(LogNiagara, Log, TEXT("~FNiagaraSystemInstance %p"), this);

	//FlushRenderingCommands();

	Cleanup();

// #if WITH_EDITOR
// 	OnDestroyedDelegate.Broadcast();
// #endif
#if WITH_EDITORONLY_DATA
	if (GEditor)
	{
		GEditor->OnObjectsReplaced().RemoveAll(this);
	}
#endif
}

void FNiagaraSystemInstance::Cleanup()
{
	// We should have no sync operations pending but we will be safe and wait
	WaitForAsyncTickDoNotFinalize();

	if (SystemInstanceIndex != INDEX_NONE)
	{
		TSharedPtr<FNiagaraSystemSimulation, ESPMode::ThreadSafe> SystemSim = GetSystemSimulation();
		SystemSim->RemoveInstance(this);
	}

	ResetComponentRenderPool();

	DestroyDataInterfaceInstanceData();

	UnbindParameters();

	// Clear out the emitters.
	Emitters.Empty(0);

	// clean up any event datasets that we're holding onto for our child emitters
	ClearEventDataSets();
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
	if (OverrideParameters != nullptr)
	{
		if (!bAlreadyBound)
		{
			// NOTE: We don't rebind if it's already bound to improve reset times.
			OverrideParameters->Bind(&InstanceParameters);
		}

		if (SystemSimulation->GetIsSolo() && FNiagaraSystemSimulation::UseLegacySystemSimulationContexts())
		{
			// If this simulation is solo than we can bind the instance parameters to the system simulation contexts so that
			// the system and emitter scripts use the per-instance data interfaces.
			OverrideParameters->Bind(&SystemSimulation->GetSpawnExecutionContext()->Parameters);
			OverrideParameters->Bind(&SystemSimulation->GetUpdateExecutionContext()->Parameters);
		}
	}

	for (TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe> Simulation : Emitters)
	{
		Simulation->BindParameters(bAlreadyBound);
	}

	bAlreadyBound = true;
}

void FNiagaraSystemInstance::UnbindParameters(bool bFromComplete)
{
	if (OverrideParameters != nullptr)
	{
		if (!bFromComplete)
		{
			// NOTE: We don't unbind this on complete to improve reset times.
			OverrideParameters->Unbind(&InstanceParameters);
		}

		if (SystemSimulation.IsValid() && SystemSimulation->GetIsSolo())
		{
			OverrideParameters->Unbind(&SystemSimulation->GetSpawnExecutionContext()->Parameters);
			OverrideParameters->Unbind(&SystemSimulation->GetUpdateExecutionContext()->Parameters);
		}
	}

	bAlreadyBound = bFromComplete && bAlreadyBound;
	for (TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe> Simulation : Emitters)
	{
		Simulation->UnbindParameters(bFromComplete);
	}
}

FNiagaraWorldManager* FNiagaraSystemInstance::GetWorldManager()const
{
	check(World);
	return FNiagaraWorldManager::Get(World);
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

bool FNiagaraSystemInstance::RequiresDepthBuffer() const
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
				if (DataInterface && DataInterface->RequiresDepthBuffer())
				{
					return true;
				}
			}
		}
	}

	return false;
}

bool FNiagaraSystemInstance::RequiresEarlyViewData() const
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
				if (DataInterface && DataInterface->RequiresEarlyViewData())
				{
					return true;
				}
			}
		}
	}

	return false;
}

bool FNiagaraSystemInstance::RequiresViewUniformBuffer() const
{
	if (!bHasGPUEmitters)
	{
		return false;
	}

	for (const TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>& EmitterHandle : Emitters)
	{
		if (FNiagaraComputeExecutionContext* GPUContext = EmitterHandle->GetGPUContext())
		{
			if (UNiagaraEmitter* Emitter = EmitterHandle->GetCachedEmitter())
			{
				if (Emitter->RequiresViewUniformBuffer())
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
	bDataInterfacesHaveTickPrereqs = false;

	// If the System is invalid, it is possible that our cached data interfaces are now bogus and could point to invalid memory.
	// Only the UNiagaraComponent or UNiagaraSystem can hold onto GC references to the DataInterfaces.
	if (GetSystem() == nullptr || IsDisabled())
	{
		return;
	}

	// Wait for any async operations, can complete the system
	WaitForAsyncTickAndFinalize(true);

	if (OverrideParameters != nullptr)
	{
		OverrideParameters->Tick();
	}

	//-TODO: Validate that any queued ticks have been executed
	DestroyDataInterfaceInstanceData();

	PerInstanceDIFunctions[(int32)ENiagaraSystemSimulationScript::Spawn].Reset();
	PerInstanceDIFunctions[(int32)ENiagaraSystemSimulationScript::Update].Reset();

	//Now the interfaces in the simulations are all correct, we can build the per instance data table.
	int32 InstanceDataSize = 0;
	DataInterfaceInstanceDataOffsets.Empty();
	auto CalcInstDataSize = [&](const FNiagaraParameterStore& ParamStore, bool bIsGPUSimulation, bool bSearchInstanceParams)
	{
		const TArrayView<const FNiagaraVariableWithOffset> Params = ParamStore.ReadParameterVariables();
		const TArray<UNiagaraDataInterface*>& Interfaces = ParamStore.GetDataInterfaces();
		for (const FNiagaraVariableWithOffset& Var : Params)
		{
			if (Var.IsDataInterface())
			{
				UNiagaraDataInterface* Interface = Interfaces[Var.Offset];
				//In scripts that deal with multiple instances we have to manually search for this DI in the instance parameters as it's not going to be in the script's exec param store.
				//Otherwise we'll end up initializing pointless default DIs that just happen to be in those stores from the script.
				//They'll never be used as we bind to the per instance functions.
				if (bSearchInstanceParams)
				{
					if (UNiagaraDataInterface* InstParamDI = InstanceParameters.GetDataInterface(Var))
					{
						Interface = InstParamDI;
					}
				}

				if (Interface)
				{
					if (int32 Size = Interface->PerInstanceDataSize())
					{
						auto* ExistingInstanceDataOffset = DataInterfaceInstanceDataOffsets.FindByPredicate([&](auto& Pair) { return Pair.Key.Get() == Interface; });
						if (!ExistingInstanceDataOffset)//Don't add instance data for interfaces we've seen before.
						{
							//UE_LOG(LogNiagara, Log, TEXT("Adding DI %p %s %s"), Interface, *Interface->GetClass()->GetName(), *Interface->GetPathName());
							auto& NewPair = DataInterfaceInstanceDataOffsets.AddDefaulted_GetRef();
							NewPair.Key = Interface;
							NewPair.Value = InstanceDataSize;

							// Assume that some of our data is going to be 16 byte aligned, so enforce that 
							// all per-instance data is aligned that way.
							InstanceDataSize += Align(Size, 16);
						}
					}

					if (bDataInterfacesHaveTickPrereqs == false)
					{
						bDataInterfacesHaveTickPrereqs = Interface->HasTickGroupPrereqs();
					}

					if (bIsGPUSimulation)
					{
						Interface->SetUsedByGPUEmitter(true);
						if(FNiagaraDataInterfaceProxy* Proxy = Interface->GetProxy())
						{
							// We need to store the name of each DI source variable here so that we can look it up later when looking for the iteration interface.
							Proxy->SourceDIName = Var.GetName();
						}
					}
				}
			}
		}
	};

	CalcInstDataSize(InstanceParameters, false, false);//This probably should be a proper exec context. 

	if (SystemSimulation->GetIsSolo() && FNiagaraSystemSimulation::UseLegacySystemSimulationContexts())
	{
		CalcInstDataSize(SystemSimulation->GetSpawnExecutionContext()->Parameters, false, false);
		SystemSimulation->GetSpawnExecutionContext()->DirtyDataInterfaces();

		CalcInstDataSize(SystemSimulation->GetUpdateExecutionContext()->Parameters, false, false);
		SystemSimulation->GetUpdateExecutionContext()->DirtyDataInterfaces();
	}
	else
	{
		CalcInstDataSize(SystemSimulation->GetSpawnExecutionContext()->Parameters, false, true);
		CalcInstDataSize(SystemSimulation->GetUpdateExecutionContext()->Parameters, false, true);
	}

	//Iterate over interfaces to get size for table and clear their interface bindings.
	for (TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe> Simulation : Emitters)
	{
		FNiagaraEmitterInstance& Sim = Simulation.Get();
		if (Sim.IsDisabled())
		{
			continue;
		}

		const bool bGPUSimulation = Sim.GetCachedEmitter() && (Sim.GetCachedEmitter()->SimTarget == ENiagaraSimTarget::GPUComputeSim);

		CalcInstDataSize(Sim.GetSpawnExecutionContext().Parameters, bGPUSimulation, false);
		CalcInstDataSize(Sim.GetUpdateExecutionContext().Parameters, bGPUSimulation, false);
		for (int32 i = 0; i < Sim.GetEventExecutionContexts().Num(); i++)
		{
			CalcInstDataSize(Sim.GetEventExecutionContexts()[i].Parameters, bGPUSimulation, false);
		}

		if (Sim.GetCachedEmitter() && Sim.GetCachedEmitter()->SimTarget == ENiagaraSimTarget::GPUComputeSim && Sim.GetGPUContext())
		{
			CalcInstDataSize(Sim.GetGPUContext()->CombinedParamStore, bGPUSimulation, false);
		}

		//Also force a rebind while we're here.
		Sim.DirtyDataInterfaces();
	}

	DataInterfaceInstanceData.SetNumUninitialized(InstanceDataSize);

	bDataInterfacesInitialized = true;
	PreTickDataInterfaces.Empty();
	PostTickDataInterfaces.Empty();

	GPUDataInterfaceInstanceDataSize = 0;
	GPUDataInterfaces.Empty();

	for (int32 i=0; i < DataInterfaceInstanceDataOffsets.Num(); ++i)
	{
		TPair<TWeakObjectPtr<UNiagaraDataInterface>, int32>& Pair = DataInterfaceInstanceDataOffsets[i];
		if (UNiagaraDataInterface* Interface = Pair.Key.Get())
		{
			check(IsAligned(&DataInterfaceInstanceData[Pair.Value], 16));

			if (Interface->HasPreSimulateTick())
	{
				PreTickDataInterfaces.Add(i);
			}

			if (Interface->HasPostSimulateTick())
		{
				PostTickDataInterfaces.Add(i);
			}

			if (bHasGPUEmitters)
			{
				const int32 GPUDataSize = Interface->PerInstanceDataPassedToRenderThreadSize();
				if (GPUDataSize > 0)
				{
					GPUDataInterfaces.Emplace(Interface, Pair.Value);
					GPUDataInterfaceInstanceDataSize += GPUDataSize;
				}
			}

			//Ideally when we make the batching changes, we can keep the instance data in big single type blocks that can all be updated together with a single virtual call.
			bool bResult = Pair.Key->InitPerInstanceData(&DataInterfaceInstanceData[Pair.Value], this);
			bDataInterfacesInitialized &= bResult;
			if (!bResult)
			{
				UE_LOG(LogNiagara, Error, TEXT("Error initializing data interface \"%s\" for system. %s"), *Interface->GetPathName(), Asset.IsValid() ? *Asset->GetName() : TEXT("nullptr"));
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
		UE_LOG(LogNiagara, Error, TEXT("Error initializing data interfaces. Completing system. %s"), Asset.IsValid() ? *Asset->GetName() : TEXT("nullptr"));
		Complete(true);
		return;
	}
	
	//We have valid DI instance data so now generate the table of function calls.	
	//When using the new exec contexts, each system instance builds it's own tables of DI function bindings for DI calls that require it.
	//i.e. User DIs or those with per instance data that are called from system scripts.
	if (FNiagaraSystemSimulation::UseLegacySystemSimulationContexts() == false)
	{
		bool bSuccess = true;
		bSuccess &= SystemSimulation->GetSpawnExecutionContext()->GeneratePerInstanceDIFunctionTable(this, PerInstanceDIFunctions[(int32)ENiagaraSystemSimulationScript::Spawn]);
		bSuccess &= SystemSimulation->GetUpdateExecutionContext()->GeneratePerInstanceDIFunctionTable(this, PerInstanceDIFunctions[(int32)ENiagaraSystemSimulationScript::Update]);

		if (!bSuccess)
		{
			//Some error initializing the per instance function tables.
			UE_LOG(LogNiagara, Error, TEXT("Error initializing data interfaces. Completing system. %s"), Asset.IsValid() ? *Asset->GetName() : TEXT("nullptr"));
			Complete(true);
			return;
		}
	}
}

void FNiagaraSystemInstance::TickDataInterfaces(float DeltaSeconds, bool bPostSimulate)
{
	if (!GetSystem() || IsDisabled())
	{
		return;
	}

	bool bReInitDataInterfaces = false;
	if (bPostSimulate)
	{
		for (int32 DIPairIndex : PostTickDataInterfaces)
		{
			TPair<TWeakObjectPtr<UNiagaraDataInterface>, int32>& Pair = DataInterfaceInstanceDataOffsets[DIPairIndex];
			if (UNiagaraDataInterface* Interface = Pair.Key.Get())
			{
				//Ideally when we make the batching changes, we can keep the instance data in big single type blocks that can all be updated together with a single virtual call.
				bReInitDataInterfaces |= Interface->PerInstanceTickPostSimulate(&DataInterfaceInstanceData[Pair.Value], this, DeltaSeconds);
			}
		}
	}
	else
	{
		for (int32 DIPairIndex : PreTickDataInterfaces)
		{
			TPair<TWeakObjectPtr<UNiagaraDataInterface>, int32>& Pair = DataInterfaceInstanceDataOffsets[DIPairIndex];
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
	//In most cases this will have been set externally by the scalability manager.
	if (bLODDistanceIsValid)
	{
		return LODDistance;
	}
	
	constexpr float DefaultLODDistance = 0.0f;

	FNiagaraWorldManager* WorldManager = GetWorldManager();
	if (WorldManager == nullptr)
	{
		return DefaultLODDistance;
	}
	
	check(World);
	const FVector EffectLocation = WorldTransform.GetLocation();
	LODDistance = DefaultLODDistance;

	// If we are inside the WorldManager tick we will use the cache player view locations as we can be ticked on different threads
	if (WorldManager->CachedPlayerViewLocationsValid())
	{
		TArrayView<const FVector> PlayerViewLocations = WorldManager->GetCachedPlayerViewLocations();
		if (PlayerViewLocations.Num() == 0)
		{
			LODDistance = DefaultLODDistance;
		}
		else
		{
			// We are being ticked inside the WorldManager and can safely use the list of cached player view locations
			float LODDistanceSqr = FMath::Square(WORLD_MAX);
			for (const FVector& ViewLocation : PlayerViewLocations)
			{
				const float DistanceToEffectSqr = FVector(ViewLocation - EffectLocation).SizeSquared();
				LODDistanceSqr = FMath::Min(LODDistanceSqr, DistanceToEffectSqr);
			}
			LODDistance = FMath::Sqrt(LODDistanceSqr);
		}
	}
	else
	{
		// If we are not inside the WorldManager tick (solo tick) we must look over the player view locations manually
		ensureMsgf(IsInGameThread(), TEXT("FNiagaraSystemInstance::GetLODDistance called in potentially thread unsafe way"));

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
			LODDistance = FMath::Sqrt(LODDistanceSqr);
		}
	}

	bLODDistanceIsValid = true;
	return LODDistance;
}

ETickingGroup FNiagaraSystemInstance::CalculateTickGroup() const
{
	ETickingGroup NewTickGroup = (ETickingGroup)0;

	// Debugging feature to force last tick group
	if (GNiagaraForceLastTickGroup)
	{
		return NiagaraLastTickGroup;
	}

	// Determine tick group
	switch ( TickBehavior )
	{
		default:
		case ENiagaraTickBehavior::UsePrereqs:
			// Handle attached component tick group
			if (PrereqComponent != nullptr)
			{
				//-TODO: This doesn't deal with 'DontCompleteUntil' on the prereq's tick, if we have to handle that it could mean continual TG demotion
				ETickingGroup PrereqTG = ETickingGroup(FMath::Max(PrereqComponent->PrimaryComponentTick.TickGroup, PrereqComponent->PrimaryComponentTick.EndTickGroup) + 1);
				NewTickGroup = FMath::Max(NewTickGroup, PrereqTG);
			}

			// Handle data interfaces that have tick dependencies
			if ( bDataInterfacesHaveTickPrereqs )
			{
				for (const TPair<TWeakObjectPtr<UNiagaraDataInterface>, int32>& Pair : DataInterfaceInstanceDataOffsets)
				{
					if (UNiagaraDataInterface* Interface = Pair.Key.Get())
					{
						ETickingGroup PrereqTG = Interface->CalculateTickGroup(&DataInterfaceInstanceData[Pair.Value]);
						NewTickGroup = FMath::Max(NewTickGroup, PrereqTG);
					}
				}
			}

			// Clamp tick group to our range
			NewTickGroup = FMath::Clamp(NewTickGroup, NiagaraFirstTickGroup, NiagaraLastTickGroup);
			break;

		case ENiagaraTickBehavior::UseComponentTickGroup:
			if (USceneComponent* Component = AttachComponent.Get())
			{
				NewTickGroup = FMath::Clamp((ETickingGroup)Component->PrimaryComponentTick.TickGroup, NiagaraFirstTickGroup, NiagaraLastTickGroup);
			}
			else
			{
				NewTickGroup = NiagaraFirstTickGroup;
			}
			break;

		case ENiagaraTickBehavior::ForceTickFirst:
			NewTickGroup = NiagaraFirstTickGroup;
			break;

		case ENiagaraTickBehavior::ForceTickLast:
			NewTickGroup = NiagaraLastTickGroup;
			break;
	}


	//UE_LOG(LogNiagara, Log, TEXT("TickGroup: %s %d %d"), *Component->GetPathName(), (int32)TickBehavior, (int32)NewTickGroup);

	return NewTickGroup;
}

void FNiagaraSystemInstance::SetTickBehavior(ENiagaraTickBehavior NewTickBehavior)
{
	UNiagaraSystem* System = GetSystem();
	if (!System || System->bRequireCurrentFrameData)
	{
		TickBehavior = NewTickBehavior;
	}
	else
	{
		// Tick as soon as possible
		TickBehavior = ENiagaraTickBehavior::ForceTickFirst;
	}
}

void FNiagaraSystemInstance::TickInstanceParameters_GameThread(float DeltaSeconds)
{
	// If we're associated with a scene component, update our cached transform (otherwise, assume it was previously set externally)
	if (AttachComponent.IsValid())
	{
		WorldTransform = AttachComponent->GetComponentToWorld();
	}
	const bool TransformMatches = GatheredInstanceParameters.ComponentTrans.Equals(WorldTransform);
	if (TransformMatches)
	{
		// we want to update the transforms one more time than the buffer count because even if the transform buffers didn't change,
		// their derivatives (like velocity) also need to be updated correctly which happens a frame later.
		GatheredInstanceParameters.TransformMatchCount = FMath::Min(ParameterBufferCount + 1, GatheredInstanceParameters.TransformMatchCount + 1);
	}
	else
	{
		GatheredInstanceParameters.ComponentTrans = WorldTransform;
		GatheredInstanceParameters.TransformMatchCount = 0;
	}

	GatheredInstanceParameters.EmitterCount = Emitters.Num();
	GatheredInstanceParameters.DeltaSeconds = DeltaSeconds;
	GatheredInstanceParameters.NumAlive = 0;

	//Bias the LastRenderTime slightly to account for any delay as it's written by the RT.
	check(World);
	GatheredInstanceParameters.TimeSeconds = World->TimeSeconds;
	GatheredInstanceParameters.RealTimeSeconds = World->RealTimeSeconds;	

	// flip our buffered parameters
	FlipParameterBuffers();
	uint32 ParameterIndex = GetParameterIndex();

	for (int32 i = 0; i < GatheredInstanceParameters.EmitterCount; ++i)
	{
		auto& CurrentEmitterParameters = EditEmitterParameters(i);

		const TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>& Emitter = Emitters[i];
		if (Emitter->GetExecutionState() != ENiagaraExecutionState::Disabled)
		{
			CurrentEmitterParameters.EmitterNumParticles = Emitter->GetNumParticles();
			CurrentEmitterParameters.EmitterTotalSpawnedParticles = Emitter->GetTotalSpawnedParticles();
			CurrentEmitterParameters.EmitterRandomSeed = Emitter->GetCachedEmitter() ? Emitter->GetCachedEmitter()->RandomSeed : 0;
			CurrentEmitterParameters.EmitterInstanceSeed = Emitter->GetInstanceSeed();
			const FNiagaraEmitterScalabilitySettings& ScalabilitySettings = Emitter->GetScalabilitySettings();
			CurrentEmitterParameters.EmitterSpawnCountScale = ScalabilitySettings.bScaleSpawnCount ? ScalabilitySettings.SpawnCountScale : 1.0f;
			++GatheredInstanceParameters.NumAlive;
		}
		else
		{
			CurrentEmitterParameters.EmitterNumParticles = 0;
		}
	}

	FNiagaraSystemParameters& CurrentSystemParameters = SystemParameters[ParameterIndex];
	CurrentSystemParameters.EngineSystemAge = Age;
	CurrentSystemParameters.EngineTickCount = TickCount;
	CurrentSystemParameters.EngineTimeSinceRendered = FMath::Max(0.0f, GatheredInstanceParameters.TimeSeconds - LastRenderTime - GLastRenderTimeSafetyBias);
	CurrentSystemParameters.EngineExecutionState = static_cast<uint32>(RequestedExecutionState);
	CurrentSystemParameters.EngineLodDistance = GetLODDistance();
	CurrentSystemParameters.EngineLodDistanceFraction = CurrentSystemParameters.EngineLodDistance / MaxLODDistance;
	CurrentSystemParameters.SignificanceIndex = SignificanceIndex;

	if (OverrideParameters)
	{
		OverrideParameters->Tick();
	}
}

void FNiagaraSystemInstance::TickInstanceParameters_Concurrent()
{
	uint32 ParameterIndex = GetParameterIndex();
	FNiagaraSystemParameters& CurrentSystemParameters = SystemParameters[ParameterIndex];
	FNiagaraOwnerParameters& CurrentOwnerParameters = OwnerParameters[ParameterIndex];

	if (GatheredInstanceParameters.TransformMatchCount <= ParameterBufferCount)
	{
		const FMatrix LocalToWorld = GatheredInstanceParameters.ComponentTrans.ToMatrixWithScale();
		const FMatrix LocalToWorldNoScale = GatheredInstanceParameters.ComponentTrans.ToMatrixNoScale();

		const FVector Location = GatheredInstanceParameters.ComponentTrans.GetLocation();
		const FVector LastLocation = FMath::IsNearlyZero(CurrentSystemParameters.EngineSystemAge) ? Location : FVector(OwnerParameters[GetParameterIndex(true)].EnginePosition);

		CurrentOwnerParameters.EngineLocalToWorld = LocalToWorld;
		CurrentOwnerParameters.EngineWorldToLocal = LocalToWorld.Inverse();
		CurrentOwnerParameters.EngineLocalToWorldTransposed = LocalToWorld.GetTransposed();
		CurrentOwnerParameters.EngineWorldToLocalTransposed = CurrentOwnerParameters.EngineWorldToLocal.GetTransposed();
		CurrentOwnerParameters.EngineLocalToWorldNoScale = LocalToWorldNoScale;
		CurrentOwnerParameters.EngineWorldToLocalNoScale = LocalToWorldNoScale.Inverse();
		CurrentOwnerParameters.EngineRotation = GatheredInstanceParameters.ComponentTrans.GetRotation();
		CurrentOwnerParameters.EnginePosition = Location;
		CurrentOwnerParameters.EngineVelocity = (Location - LastLocation) / GatheredInstanceParameters.DeltaSeconds;
		CurrentOwnerParameters.EngineXAxis = CurrentOwnerParameters.EngineRotation.GetAxisX();
		CurrentOwnerParameters.EngineYAxis = CurrentOwnerParameters.EngineRotation.GetAxisY();
		CurrentOwnerParameters.EngineZAxis = CurrentOwnerParameters.EngineRotation.GetAxisZ();
		CurrentOwnerParameters.EngineScale = GatheredInstanceParameters.ComponentTrans.GetScale3D();
	}

	CurrentSystemParameters.EngineEmitterCount = GatheredInstanceParameters.EmitterCount;
	CurrentSystemParameters.EngineAliveEmitterCount = GatheredInstanceParameters.NumAlive;
	CurrentSystemParameters.SignificanceIndex = SignificanceIndex;

	FNiagaraGlobalParameters& CurrentGlobalParameter = GlobalParameters[ParameterIndex];
	CurrentGlobalParameter.EngineDeltaTime = GatheredInstanceParameters.DeltaSeconds;
	CurrentGlobalParameter.EngineInvDeltaTime = 1.0f / GatheredInstanceParameters.DeltaSeconds;
	CurrentGlobalParameter.EngineRealTime = GatheredInstanceParameters.RealTimeSeconds;
	CurrentGlobalParameter.EngineTime = GatheredInstanceParameters.TimeSeconds;
	CurrentGlobalParameter.QualityLevel = FNiagaraPlatformSet::GetQualityLevel();

	InstanceParameters.Tick();
	InstanceParameters.MarkParametersDirty();
}

void FNiagaraSystemInstance::ClearEventDataSets()
{
	for (auto& EventDataSetIt : EmitterEventDataSetMap)
	{
		delete EventDataSetIt.Value;
	}

	EmitterEventDataSetMap.Empty();
}

FNiagaraDataSet*
FNiagaraSystemInstance::CreateEventDataSet(FName EmitterName, FName EventName)
{
	// TODO: find a better way of multiple events trying to write to the same data set; 
	// for example, if two analytical collision primitives want to send collision events, they need to push to the same data set
	FNiagaraDataSet*& OutSet = EmitterEventDataSetMap.FindOrAdd(EmitterEventKey(EmitterName, EventName));

	if (!OutSet)
	{
		OutSet = new FNiagaraDataSet();
	}

	return OutSet;
}

FNiagaraDataSet*
FNiagaraSystemInstance::GetEventDataSet(FName EmitterName, FName EventName) const
{
	FNiagaraDataSet* const* OutDataSet = EmitterEventDataSetMap.Find(EmitterEventKey(EmitterName, EventName));

	return OutDataSet ? *OutDataSet : nullptr;
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

	bHasGPUEmitters = false;

	LocalBounds = FBox(FVector::ZeroVector, FVector::ZeroVector);

	Emitters.Empty(false);
	UNiagaraSystem* System = GetSystem();
	if (System != nullptr)
	{
		const TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();

		const bool bAllowComputeShaders = FNiagaraUtilities::AllowComputeShaders(GShaderPlatformForFeatureLevel[GMaxRHIFeatureLevel]);

		const int32 NumEmitters = EmitterHandles.Num();
		Emitters.Reserve(NumEmitters);
		for (int32 EmitterIdx=0; EmitterIdx < NumEmitters; ++EmitterIdx)
		{
			TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe> Sim = MakeShared<FNiagaraEmitterInstance, ESPMode::ThreadSafe>(this);

			if (System->bFixedBounds)
			{
				// be sure to set the system bounds first so that we can bypass work in the initialization of the emitter
				Sim->SetSystemFixedBoundsOverride(System->GetFixedBounds());
			}

			Sim->Init(EmitterIdx, ID);
			Emitters.Add(Sim);

			//-TODO: We should not create emitter instances for disable emitters
			if (EmitterHandles[EmitterIdx].GetIsEnabled())
			{
				// Only set bHasGPUEmitters if we allow compute shaders on the platform
				if (bAllowComputeShaders)
				{
					if (const UNiagaraEmitter* Emitter = Sim->GetCachedEmitter())
					{
						bHasGPUEmitters |= Emitter->SimTarget == ENiagaraSimTarget::GPUComputeSim;
					}
				}
			}
		}

		// Create the shared context for the batcher if we have a single active GPU emitter in the system
		if (bHasGPUEmitters)
		{
			SharedContext.Reset(new FNiagaraComputeSharedContext());
		}

		if (System->bFixedBounds)
		{
			LocalBounds = System->GetFixedBounds();
		}
	}

	ResetParameters();
}

void FNiagaraSystemInstance::ManualTick(float DeltaSeconds, const FGraphEventRef& MyCompletionGraphEvent)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_GT);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemInst_ComponentTickGT);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Effects);
	LLM_SCOPE(ELLMTag::Niagara);

	if (IsDisabled())
	{
		return;
	}

	TSharedPtr<FNiagaraSystemSimulation, ESPMode::ThreadSafe> SystemSim = GetSystemSimulation();
	check(SystemSim.IsValid());
	check(IsInGameThread());
	check(bSolo);

	SystemSim->Tick_GameThread(DeltaSeconds, MyCompletionGraphEvent);

}

void FNiagaraSystemInstance::WaitForAsyncTickDoNotFinalize(bool bEnsureComplete)
{
	if (bAsyncWorkInProgress == false)
	{
		return;
	}

	ensureAlwaysMsgf(!bEnsureComplete, TEXT("Niagara System Async Task should be complete by now. %s"), *GetSystem()->GetPathName());
	ensureAlwaysMsgf(IsInGameThread(), TEXT("NiagaraSystemInstance::WaitForAsyncTick() call is assuming execution on GT but is not on GT. %s"), *GetSystem()->GetPathName());

	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemWaitForAsyncTick);

	const uint64 StartCycles = FPlatformTime::Cycles64();
	const double WarnSeconds = 5.0;
	const uint64 WarnCycles = StartCycles + uint64(WarnSeconds / FPlatformTime::GetSecondsPerCycle64());
	bool bDoWarning = true;

	while ( bAsyncWorkInProgress )
	{
		FPlatformProcess::SleepNoStats(0.001f);
		if ( bDoWarning && (FPlatformTime::Cycles64() > WarnCycles) )
		{
			bDoWarning = false;
			UE_LOG(LogNiagara, Warning, TEXT("Niagara Effect has stalled GT for %g seconds and is not complete, this may result in a deadlock. Component(%s) System(%s)"), WarnSeconds, *GetFullNameSafe(AttachComponent.Get()), *GetFullNameSafe(GetSystem()));
		}
	}

	const double StallTimeMS = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - StartCycles);
	if (StallTimeMS > GWaitForAsyncStallWarnThresholdMS)
	{
		//-TODO: This should be put back to a warning once EngineTests no longer cause it show up.  The reason it's triggered is because we pause in latent actions right after a TG running Niagara sims.
		UE_LOG(LogNiagara, Log, TEXT("Niagara Effect stalled GT for %g ms. Component(%s) System(%s)"), StallTimeMS, *GetFullNameSafe(AttachComponent.Get()), *GetFullNameSafe(GetSystem()));
	}
}

void FNiagaraSystemInstance::WaitForAsyncTickAndFinalize(bool bEnsureComplete)
{
	if (bAsyncWorkInProgress)
	{
		WaitForAsyncTickDoNotFinalize(bEnsureComplete);
		FinalizeTick_GameThread();
	}
}

bool FNiagaraSystemInstance::HandleCompletion()
{
	bool bEmittersCompleteOrDisabled = true;
	for (TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>&it : Emitters)
	{
		FNiagaraEmitterInstance& Inst = *it;
		bEmittersCompleteOrDisabled &= Inst.HandleCompletion();
	}

	bool bCompletedAlready = IsComplete();
	if (bCompletedAlready || bEmittersCompleteOrDisabled)
	{
		//UE_LOG(LogNiagara, Log, TEXT("Completion Achieved"));
		Complete(false);
		return true;
	}

	bPendingSpawn = false;
	return false;
}

void FNiagaraSystemInstance::Tick_GameThread(float DeltaSeconds)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemInst_TickGT);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_GT);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Effects);
	LLM_SCOPE(ELLMTag::Niagara);
	
	FNiagaraCrashReporterScope CRScope(this);

	UNiagaraSystem* System = GetSystem();
	FScopeCycleCounter SystemStat(System->GetStatID(true, false));

	// We should have no pending async operations, but wait to be safe
	WaitForAsyncTickAndFinalize(true);
	if (IsComplete())
	{
		return;
	}

	CachedDeltaSeconds = DeltaSeconds;
	bNeedsFinalize = true;

	TickInstanceParameters_GameThread(DeltaSeconds);

	TickDataInterfaces(DeltaSeconds, false);

	Age += DeltaSeconds;
	TickCount += 1;
	
	if ( !IsComplete() )
	{
		BeginAsyncWork();
	}
}

void FNiagaraSystemInstance::Tick_Concurrent(bool bEnqueueGPUTickIfNeeded)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemInst_TickCNC);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_GT_CNC);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Effects);
	LLM_SCOPE(ELLMTag::Niagara);
	FScopeCycleCounterUObject AdditionalScope(GetSystem(), GET_STATID(STAT_NiagaraOverview_GT_CNC));

	FNiagaraCrashReporterScope CRScope(this);

	// Reset values that will be accumulated during emitter tick.
	TotalGPUParamSize = 0;
	ActiveGPUEmitterCount = 0;
	GPUParamIncludeInterpolation = false;
	UNiagaraSystem* System = GetSystem();

	if (IsComplete() || System == nullptr || CachedDeltaSeconds < SMALL_NUMBER)
	{
		bAsyncWorkInProgress = false;
		return;
	}

	const int32 NumEmitters = Emitters.Num();
	const TConstArrayView<FNiagaraEmitterExecutionIndex> EmitterExecutionOrder = GetEmitterExecutionOrder();
	checkSlow(EmitterExecutionOrder.Num() <= NumEmitters);

	//Determine if any of our emitters should be ticking.
	TBitArray<TInlineAllocator<8>> EmittersShouldTick;
	EmittersShouldTick.Init(false, NumEmitters);

	bool bHasTickingEmitters = false;
	for (const FNiagaraEmitterExecutionIndex& EmitterExecIdx : EmitterExecutionOrder)
	{
		FNiagaraEmitterInstance& Inst = Emitters[EmitterExecIdx.EmitterIndex].Get();
		if (Inst.ShouldTick())
		{
			bHasTickingEmitters = true;
			EmittersShouldTick.SetRange(EmitterExecIdx.EmitterIndex, 1, true);
		}
	}

	if ( !bHasTickingEmitters )
	{
		bAsyncWorkInProgress = false;
		return;
	}

	FScopeCycleCounter SystemStat(System->GetStatID(true, true));

	for (const FNiagaraEmitterExecutionIndex& EmitterExecIdx : EmitterExecutionOrder)
	{
		if (EmittersShouldTick[EmitterExecIdx.EmitterIndex])
		{
			FNiagaraEmitterInstance& Inst = Emitters[EmitterExecIdx.EmitterIndex].Get();
			Inst.PreTick();
		}
	}

	int32 TotalCombinedParamStoreSize = 0;

	// now tick all emitters
	for (const FNiagaraEmitterExecutionIndex& EmitterExecIdx : EmitterExecutionOrder)
	{
		FNiagaraEmitterInstance& Inst = Emitters[EmitterExecIdx.EmitterIndex].Get();
		if (EmittersShouldTick[EmitterExecIdx.EmitterIndex])
		{
			Inst.Tick(CachedDeltaSeconds);
		}

		if (Inst.GetCachedEmitter() && Inst.GetCachedEmitter()->SimTarget == ENiagaraSimTarget::GPUComputeSim && !Inst.IsComplete())
		{
			// Handle edge case where an emitter was set to inactive on the first frame by scalability
			// Since it will not tick we should not execute a GPU tick for it, this test must be symeterical with FNiagaraGPUSystemTick::Init
			const bool bIsInactive = (Inst.GetExecutionState() == ENiagaraExecutionState::Inactive) || (Inst.GetExecutionState() == ENiagaraExecutionState::InactiveClear);
			if (Inst.HasTicked() || !bIsInactive)
			{
				if (const FNiagaraComputeExecutionContext* GPUContext = Inst.GetGPUContext())
				{
					TotalCombinedParamStoreSize += GPUContext->CombinedParamStore.GetPaddedParameterSizeInBytes();
					GPUParamIncludeInterpolation = GPUContext->HasInterpolationParameters || GPUParamIncludeInterpolation;
					ActiveGPUEmitterCount++;
				}
			}
		}
	}

	if (ActiveGPUEmitterCount)
	{
		const int32 InterpFactor = GPUParamIncludeInterpolation ? 2 : 1;

		TotalGPUParamSize = InterpFactor * (sizeof(FNiagaraGlobalParameters) + sizeof(FNiagaraSystemParameters) + sizeof(FNiagaraOwnerParameters));
		TotalGPUParamSize += InterpFactor * ActiveGPUEmitterCount * sizeof(FNiagaraEmitterParameters);
		TotalGPUParamSize += TotalCombinedParamStoreSize;
	}

	// Update local bounds
	if ( System->bFixedBounds )
	{
		LocalBounds = System->GetFixedBounds();
	}
	else
	{
		FBox NewLocalBounds(EForceInit::ForceInit);
		for (const auto& Emitter : Emitters)
		{
			NewLocalBounds += Emitter->GetBounds();
		}

		if (NewLocalBounds.IsValid)
		{
			LocalBounds = NewLocalBounds.ExpandBy(NewLocalBounds.GetExtent() * GNiagaraBoundsExpandByPercent);				
		}
		else
		{
			LocalBounds = FBox(FVector::ZeroVector, FVector::ZeroVector);
		}
	}

	//Enqueue a GPU tick for this sim if we're allowed to do so from a concurrent thread.
	//If we're batching our tick passing we may still need to enqueue here if not called from the regular async task. The caller will tell us with bEnqueueGPUTickIfNeeded.
	FNiagaraSystemSimulation* Sim = SystemSimulation.Get();
	check(Sim);
	ENiagaraGPUTickHandlingMode Mode = Sim->GetGPUTickHandlingMode();
	if (Mode == ENiagaraGPUTickHandlingMode::Concurrent || (Mode == ENiagaraGPUTickHandlingMode::ConcurrentBatched && bEnqueueGPUTickIfNeeded))
	{
		GenerateAndSubmitGPUTick();
	}

	bAsyncWorkInProgress = false;
}

TSet<int32> FNiagaraSystemInstance::GetParticlesWithActiveComponents(USceneComponent* const Component)
{
	TSet<int32> Result;
	TObjectKey<USceneComponent> ObjectKey(Component);
	FRWScopeLock ReadLock(ComponentPoolLock, SLT_ReadOnly);
	TArray<FNiagaraComponentRenderPoolEntry>* Pool = ComponentRenderPool.PoolsByTemplate.Find(ObjectKey);
	if (Pool)
	{
		for (const FNiagaraComponentRenderPoolEntry& Entry : *Pool)
		{
			if (Entry.LastAssignedToParticleID >= 0)
			{
				Result.Add(Entry.LastAssignedToParticleID);
			}
		}
	}
	return Result;
}

void FNiagaraSystemInstance::OnSimulationDestroyed()
{
	// This notifies us that the simulation we're holding a reference to is being abandoned by the world manager and we should also
	// release our reference
	ensureMsgf(!IsSolo(), TEXT("OnSimulationDestroyed should only happen for systems referencing a simulation from the world manager"));
	if (SystemSimulation.IsValid())
	{
		UnbindParameters();
		SystemSimulation = nullptr;
	}
}

void FNiagaraSystemInstance::ProcessComponentRendererTasks()
{
	FRWScopeLock WriteLock(ComponentPoolLock, SLT_Write);
	if (ComponentTasks.IsEmpty() && ComponentRenderPool.PoolsByTemplate.Num() == 0)
	{
		return;
	}

	USceneComponent* Component = AttachComponent.Get();
	if (!Component)
	{
		// we can't attach the components anywhere, so just discard them
		ComponentTasks.Empty();
		return;
	}
	SCOPE_CYCLE_COUNTER(STAT_NiagaraProcessComponentRendererTasks);

	TMap<TObjectKey<USceneComponent>, TArray<FNiagaraComponentRenderPoolEntry>> NewRenderPool;
	int32 AttachedComponentCount = 0;

	FNiagaraComponentUpdateTask UpdateTask;
	while (ComponentTasks.Dequeue(UpdateTask))
	{
		if (!UpdateTask.TemplateObject.IsValid())
		{
			continue;
		}

		TObjectKey<USceneComponent> ObjectKey(UpdateTask.TemplateObject.Get());
		TArray<FNiagaraComponentRenderPoolEntry>& CurrentPool = ComponentRenderPool.PoolsByTemplate.FindOrAdd(ObjectKey);
		USceneComponent* SceneComponent = nullptr;
		FNiagaraComponentRenderPoolEntry NewEntry;
		if (CurrentPool.Num() > 0)
		{
			// grab a component from the pool if there is one available
			int32 FreeComponentIndex = -1;
			if (UpdateTask.ParticleID == -1)
			{
				FreeComponentIndex = CurrentPool.Num() - 1;
			}
			else
			{
				// if we have a particle ID we try to map it to a previously assigned component
				for (int32 i = 0; i < CurrentPool.Num(); i++)
				{
					int32& PoolEntryID = CurrentPool[i].LastAssignedToParticleID;
					if (PoolEntryID > -1 && PoolEntryID < UpdateTask.SmallestID)
					{
						// there is no particle alive any more with this ID, mark component for reuse
						PoolEntryID = -1;
					}

					// search for a previously assigned component for this particle
					if (PoolEntryID == UpdateTask.ParticleID)
					{
						FreeComponentIndex = i;
						break;
					}
					else if (PoolEntryID == -1)
					{
						// if we don't find one we can maybe reuse one that's free anyways
						FreeComponentIndex = i;
					}
				}
			}

			if (FreeComponentIndex != -1)
			{
				NewEntry = CurrentPool[FreeComponentIndex];
				CurrentPool.RemoveAtSwap(FreeComponentIndex, 1, false);
				SceneComponent = NewEntry.Component.Get();
			}
		}

		if (!SceneComponent || SceneComponent->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraComponentRendererSpawning);
			
			AActor* OwnerActor = ComponentRenderPool.OwnerActor.Get();
			if (OwnerActor == nullptr)
			{
				OwnerActor = Component->GetOwner();
				if (OwnerActor == nullptr)
				{
					OwnerActor = World->SpawnActor<AActor>();
					OwnerActor->SetFlags(RF_Transient);
					ComponentRenderPool.OwnerActor = OwnerActor;
				}
			}

			// if we don't have a pooled component we create a new one from the template
			SceneComponent = DuplicateObject<USceneComponent>(UpdateTask.TemplateObject.Get(), OwnerActor);
			SceneComponent->ClearFlags(RF_ArchetypeObject);
			SceneComponent->SetFlags(RF_Transient);
#if WITH_EDITORONLY_DATA
			SceneComponent->bVisualizeComponent = UpdateTask.bVisualizeComponents;
#endif
			SceneComponent->SetupAttachment(Component);
			SceneComponent->RegisterComponent();
			SceneComponent->AddTickPrerequisiteComponent(Component);
			NewEntry = FNiagaraComponentRenderPoolEntry();
			NewEntry.Component = SceneComponent;
		}
		
		// call the update task which sets the values from the particle bindings
		UpdateTask.UpdateCallback(SceneComponent, NewEntry);
		
		// activate the component
		if (!SceneComponent->IsActive())
		{
			SceneComponent->SetVisibility(true);
			SceneComponent->Activate(false);
		}

		NewEntry.LastAssignedToParticleID = UpdateTask.ParticleID;
		NewEntry.InactiveTimeLeft = GNiagaraComponentRenderPoolInactiveTimeLimit;
		NewRenderPool.FindOrAdd(ObjectKey).Add(NewEntry);
		AttachedComponentCount++;
	}

	if (AttachedComponentCount > GNiagaraWarnComponentRenderCount)
	{
		UE_LOG(LogNiagara, Warning, TEXT("System %s has over %i active components spawned from the effect. Either adjust the effect's component renderer or change the warning limit with fx.Niagara.WarnComponentRenderCount."), *GetSystem()->GetName(), GNiagaraWarnComponentRenderCount);
	}

	// go over the pooled components we didn't need this tick to see if we can destroy some and deactivate the rest
	for (TPair<TObjectKey<USceneComponent>, TArray<FNiagaraComponentRenderPoolEntry>>& Pair : ComponentRenderPool.PoolsByTemplate)
	{
		for (FNiagaraComponentRenderPoolEntry& PoolEntry : Pair.Value)
		{
			if (!PoolEntry.Component.IsValid())
			{
				continue;
			}
			PoolEntry.InactiveTimeLeft -= CachedDeltaSeconds;
			if (PoolEntry.InactiveTimeLeft <= 0)
			{
				PoolEntry.Component->DestroyComponent();
			}
			else
			{
				if (PoolEntry.Component->IsActive())
				{
					PoolEntry.Component->Deactivate();
					PoolEntry.Component->SetVisibility(false);
				}
				NewRenderPool.FindOrAdd(Pair.Key).Add(PoolEntry);
			}
		}
	}

	ComponentRenderPool.PoolsByTemplate = NewRenderPool;
}

void FNiagaraSystemInstance::OnObjectsReplacedCallback(const TMap<UObject*, UObject*>& ReplacementsMap)
{
	TArray<UObject*> Keys;
	ReplacementsMap.GetKeys(Keys);
	
	FRWScopeLock WriteLock(ComponentPoolLock, SLT_Write);
	for (UObject* OldObject : Keys)
	{
		TObjectKey<USceneComponent> OldObjectKey(Cast<USceneComponent>(OldObject));
		if (!ComponentRenderPool.PoolsByTemplate.Contains(OldObjectKey))
		{
			continue;
		}
		for (FNiagaraComponentRenderPoolEntry& PoolEntry : ComponentRenderPool.PoolsByTemplate[OldObjectKey])
		{
			if (PoolEntry.Component.IsValid())
			{
				PoolEntry.Component->DestroyComponent();
			}
		}
		ComponentRenderPool.PoolsByTemplate.Remove(OldObjectKey);
	}
}

void FNiagaraSystemInstance::ResetComponentRenderPool()
{
	FRWScopeLock WriteLock(ComponentPoolLock, SLT_Write);
	for (TPair<TObjectKey<USceneComponent>, TArray<FNiagaraComponentRenderPoolEntry>>& Pair : ComponentRenderPool.PoolsByTemplate)
	{
		for (FNiagaraComponentRenderPoolEntry PoolEntry : Pair.Value)
		{
			if (PoolEntry.Component.IsValid())
			{
				PoolEntry.Component->DestroyComponent();
			}
		}
	}
	ComponentRenderPool.PoolsByTemplate.Empty();

	if (AActor* OwnerActor = ComponentRenderPool.OwnerActor.Get())
	{
		ComponentRenderPool.OwnerActor.Reset();
		OwnerActor->Destroy();
	}
}

bool FNiagaraSystemInstance::FinalizeTick_GameThread(bool bEnqueueGPUTickIfNeeded)
{
	if (bNeedsFinalize)//We can come in here twice in one tick if the GT calls WaitForAsync() while there is a GT finalize task in the queue.
	{
		FNiagaraCrashReporterScope CRScope(this);

		SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_GT);
		SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemInst_FinalizeGT);
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Effects);
		LLM_SCOPE(ELLMTag::Niagara);

		//Temporarily force FX to update their own LODDistance on frames where it is not provided by the scalability manager.
		//TODO: Lots of FX wont need an accurate per frame value so implement a good way for FX to opt into this. FORT-248457
		bLODDistanceIsValid = false;

		bNeedsFinalize = false;
		if (!HandleCompletion())
		{
			//Post tick our interfaces.
			TickDataInterfaces(CachedDeltaSeconds, true);

			ProcessComponentRendererTasks();

			//Enqueue a GPU tick for this sim if we have to do this from the GameThread.
			//If we're batching our tick passing we may still need to enqueue here if not called from the regular finalize task. The caller will tell us with bEnqueueGPUTickIfNeeded.
			FNiagaraSystemSimulation* Sim = SystemSimulation.Get();
			check(Sim);
			ENiagaraGPUTickHandlingMode Mode = Sim->GetGPUTickHandlingMode();
			if (Mode == ENiagaraGPUTickHandlingMode::GameThread || (Mode == ENiagaraGPUTickHandlingMode::GameThreadBatched && bEnqueueGPUTickIfNeeded))
			{
				GenerateAndSubmitGPUTick();
			}
		}

		if (DeferredResetMode != EResetMode::None)
		{
			const EResetMode ResetMode = DeferredResetMode;
			DeferredResetMode = EResetMode::None;

			Reset(ResetMode);
		}

		if (OnPostTickDelegate.IsBound())
		{
			OnPostTickDelegate.Execute();
		}

		return true;
	}

	//Tell the caller we didn't actually finalize the system.
	return false;
}

void FNiagaraSystemInstance::GenerateAndSubmitGPUTick()
{
	if (NeedsGPUTick())
	{
		ensure(!IsComplete());
		FNiagaraGPUSystemTick GPUTick;
		InitGPUTick(GPUTick);

		// We will give the data over to the render thread. It is responsible for freeing it.
		// We no longer own it and cannot modify it after this point.
		// @todo We are taking a copy of the object here. This object is small so this overhead should
		// not be very high. And we avoid making a bunch of small allocations here.
		NiagaraEmitterInstanceBatcher* TheBatcher = GetBatcher();
		ENQUEUE_RENDER_COMMAND(FNiagaraGiveSystemInstanceTickToRT)(
			[TheBatcher, GPUTick](FRHICommandListImmediate& RHICmdList) mutable
			{
				TheBatcher->GiveSystemTick_RenderThread(GPUTick);
			}
		);
		ComponentTasks.Empty();
	}
}

void FNiagaraSystemInstance::InitGPUTick(FNiagaraGPUSystemTick& OutTick)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraInitGPUSystemTick);
	OutTick.Init(this);

	//if (GPUTick.DIInstanceData)
	//{
	//	uint8* BasePointer = (uint8*)GPUTick.DIInstanceData->PerInstanceDataForRT;

	//	//UE_LOG(LogNiagara, Log, TEXT("GT Testing (dipacket) %p (baseptr) %p"), GPUTick.DIInstanceData, BasePointer);
	//	for (auto& Pair : GPUTick.DIInstanceData->InterfaceProxiesToOffsets)
	//	{
	//		FNiagaraDataInterfaceProxy* Proxy = Pair.Key;
	//		UE_LOG(LogNiagara, Log, TEXT("\tGT (proxy) %p (size) %u"), Proxy, Proxy->PerInstanceDataPassedToRenderThreadSize());
	//	}
	//}
}

#if WITH_EDITOR
void FNiagaraSystemInstance::RaiseNeedsUIResync()
{
	bNeedsUIResync = true;
}

bool FNiagaraSystemInstance::HandleNeedsUIResync()
{
	bool bRet = bNeedsUIResync;
	bNeedsUIResync = false;
	return bRet;
}
#endif

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
	NiagaraEmitterInstanceBatcher* InstanceBatcher = GetBatcher();
	if (bHasGPUEmitters && FNiagaraUtilities::AllowGPUParticles(InstanceBatcher->GetShaderPlatform()))
	{
		ENQUEUE_RENDER_COMMAND(NiagaraRemoveGPUSystem)
		(
			[InstanceBatcher, InstanceID=GetId()](FRHICommandListImmediate& RHICmdList) mutable
			{
				InstanceBatcher->InstanceDeallocated_RenderThread(InstanceID);
			}
		);
	}

	//
	for (TPair<TWeakObjectPtr<UNiagaraDataInterface>, int32>& Pair : DataInterfaceInstanceDataOffsets)
	{
		if (UNiagaraDataInterface* Interface = Pair.Key.Get())
		{
			Interface->DestroyPerInstanceData(&DataInterfaceInstanceData[Pair.Value], this);
		}
	}

	DataInterfaceInstanceDataOffsets.Empty();
	DataInterfaceInstanceData.Empty();
	PreTickDataInterfaces.Empty();
	PostTickDataInterfaces.Empty();
	GPUDataInterfaces.Empty();
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

TConstArrayView<FNiagaraEmitterExecutionIndex> FNiagaraSystemInstance::GetEmitterExecutionOrder() const
{
	if (SystemSimulation != nullptr)
	{
		const UNiagaraSystem* NiagaraSystem = SystemSimulation->GetSystem();
		if (ensure(NiagaraSystem != nullptr))
		{
			return NiagaraSystem->GetEmitterExecutionOrder();
		}
	}
	return MakeArrayView<FNiagaraEmitterExecutionIndex>(nullptr, 0);
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

#if WITH_EDITOR
FNiagaraSystemInstance::FOnInitialized& FNiagaraSystemInstance::OnInitialized()
{
	return OnInitializedDelegate;
}

FNiagaraSystemInstance::FOnReset& FNiagaraSystemInstance::OnReset()
{
	return OnResetDelegate;
}

FNiagaraSystemInstance::FOnDestroyed& FNiagaraSystemInstance::OnDestroyed()
{
	return OnDestroyedDelegate;
}
#endif

const FString& FNiagaraSystemInstance::GetCrashReporterTag()const
{
	if(CrashReporterTag.IsEmpty())
	{
		UNiagaraSystem* Sys = GetSystem();
		UNiagaraComponent* Component = Cast<UNiagaraComponent>(AttachComponent.Get());
		USceneComponent* AttachParent = Component ? Component->GetAttachParent() : AttachComponent.Get();

		const FString& CompName = GetFullNameSafe(Component);
		const FString& AssetName = GetFullNameSafe(Sys);
		const FString& AttachName = GetFullNameSafe(AttachParent);

		CrashReporterTag = FString::Printf(TEXT("SystemInstance | System: %s | bSolo: %s | Component: %s | AttachedTo: %s |"), *AssetName, IsSolo() ? TEXT("true") : TEXT("false"), *CompName, *AttachName);
	}
	return CrashReporterTag;
}
