// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraEmitterInstance.h"
#include "Engine/Engine.h"
#include "Materials/Material.h"
#include "VectorVM.h"
#include "NiagaraStats.h"
#include "NiagaraConstants.h"
#include "NiagaraRenderer.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraDataInterface.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraScriptExecutionContext.h"
#include "NiagaraParameterCollection.h"
#include "NiagaraScriptExecutionContext.h"
#include "NiagaraWorldManager.h"
#include "NiagaraSimulationStageBase.h"

DECLARE_DWORD_COUNTER_STAT(TEXT("Num Custom Events"), STAT_NiagaraNumCustomEvents, STATGROUP_Niagara);

//DECLARE_CYCLE_STAT(TEXT("Tick"), STAT_NiagaraTick, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Emitter Simulate [CNC]"), STAT_NiagaraSimulate, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Emitter Spawn [CNC]"), STAT_NiagaraSpawn, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Emitter Post Tick [CNC]"), STAT_NiagaraEmitterPostTick, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Emitter Event Handling [CNC]"), STAT_NiagaraEventHandle, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Emitter Error Check [CNC]"), STAT_NiagaraEmitterErrorCheck, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Init Emitters [GT]"), STAT_NiagaraEmitterInit, STATGROUP_Niagara);

DECLARE_CYCLE_STAT(TEXT("Init Emitters (DataSets) [GT]"), STAT_NiagaraEmitterInit_Datasets, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Init Emitters (ExecContexts) [GT]"), STAT_NiagaraEmitterInit_ExecContexts, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Init Emitters (DirectBindings) [GT]"), STAT_NiagaraEmitterInit_DirectBindings, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Init Emitters (DIDefaultsAndBoundCalcs) [GT]"), STAT_NiagaraEmitterInit_DIDefaultsAndBoundsCalcs, STATGROUP_Niagara);


static int32 GbDumpParticleData = 0;
static FAutoConsoleVariableRef CVarNiagaraDumpParticleData(
	TEXT("fx.DumpParticleData"),
	GbDumpParticleData,
	TEXT("If > 0 current frame particle data will be dumped after simulation. \n"),
	ECVF_Default
	);

static int32 GbNiagaraDumpNans = 0;
static FAutoConsoleVariableRef CVarNiagaraDumpNans(
	TEXT("fx.Niagara.DumpNans"),
	GbNiagaraDumpNans,
	TEXT("If not 0 any NaNs will be dumped always.\n"),
	ECVF_Default
);

static int32 GbNiagaraDumpNansOnce = 0;
static FAutoConsoleVariableRef CVarNiagaraDumpNansOnce(
	TEXT("fx.Niagara.DumpNansOnce"),
	GbNiagaraDumpNansOnce,
	TEXT("If not 0 any NaNs will be dumped for the first emitter that encounters NaNs.\n"),
	ECVF_Default
);

static int32 GbNiagaraShowAllocationWarnings = 0;
static FAutoConsoleVariableRef CVarNiagaraShowAllocationWarnings(
	TEXT("fx.Niagara.ShowAllocationWarnings"),
	GbNiagaraShowAllocationWarnings,
	TEXT("If not 0 then frequent reallocations and over-allocations of particle memory will cause warnings in the log.\n"),
	ECVF_Default
);

/**
TODO: This is mainly to avoid hard limits in our storage/alloc code etc rather than for perf reasons.
We should improve our hard limit/safety code and possibly add a max for perf reasons.
*/
static int32 GMaxNiagaraCPUParticlesPerEmitter = 1000000;
static FAutoConsoleVariableRef CVarMaxNiagaraCPUParticlesPerEmitter(
	TEXT("fx.MaxNiagaraCPUParticlesPerEmitter"),
	GMaxNiagaraCPUParticlesPerEmitter,
	TEXT("The max number of supported CPU particles per emitter in Niagara. \n"),
	ECVF_Default
);

static int32 GMaxNiagaraGPUParticlesSpawnPerFrame = 2000000;
static FAutoConsoleVariableRef CVarMaxNiagaraGPUParticlesSpawnPerFrame(
	TEXT("fx.MaxNiagaraGPUParticlesSpawnPerFrame"),
	GMaxNiagaraGPUParticlesSpawnPerFrame,
	TEXT("The max number of GPU particles we expect to spawn in a single frame.\n"),
	ECVF_Default
);

static int32 GDebugForcedMaxGPUBufferElements = 0;
static FAutoConsoleVariableRef CVarNiagaraDebugForcedMaxGPUBufferElements(
	TEXT("fx.NiagaraDebugForcedMaxGPUBufferElements"),
	GDebugForcedMaxGPUBufferElements,
	TEXT("Force the maximum buffer size supported by the GPU to this value, for debugging purposes."),
	ECVF_Default
);

//////////////////////////////////////////////////////////////////////////

template<bool bAccumulate>
struct FNiagaraEditorOnlyCycleTimer
{
	FORCEINLINE FNiagaraEditorOnlyCycleTimer(uint32& InCyclesOut)
#if WITH_EDITOR
		: CyclesOut(InCyclesOut)
		, StartCycles(FPlatformTime::Cycles())
#endif
	{
	}

#if WITH_EDITOR
	FORCEINLINE ~FNiagaraEditorOnlyCycleTimer()
	{
		uint32 DeltaCycles = FPlatformTime::Cycles() - StartCycles;
		if (bAccumulate)
		{
			CyclesOut += DeltaCycles;
		}
		else
		{
			CyclesOut = DeltaCycles;
		}
	}

	uint32& CyclesOut;
	uint32 StartCycles;
#endif
};

//////////////////////////////////////////////////////////////////////////

FNiagaraEmitterInstance::FNiagaraEmitterInstance(FNiagaraSystemInstance* InParentSystemInstance)
: CPUTimeCycles(0)
, ExecutionState(ENiagaraExecutionState::Inactive)
, CachedBounds(ForceInit)
, GPUExecContext(nullptr)
, ParentSystemInstance(InParentSystemInstance)
, CachedEmitter(nullptr)
, CachedSystemFixedBounds()
, EventSpawnTotal(0)
{
	bDumpAfterEvent = false;
	ParticleDataSet = new FNiagaraDataSet();

	Batcher = ParentSystemInstance ? ParentSystemInstance->GetBatcher() : nullptr;
	check(Batcher != nullptr);
}

FNiagaraEmitterInstance::~FNiagaraEmitterInstance()
{
	// Clear the cached emitter as it is not safe to access the CacheEmitter due to deferred deleted which can happen after the CachedEmitter has been GCed
	CachedEmitter = nullptr;

	//UE_LOG(LogNiagara, Warning, TEXT("~Simulator %p"), this);
	CachedBounds.Init();
	UnbindParameters(false);

	if (GPUExecContext != nullptr)
	{
		/** We defer the deletion of the particle dataset and the compute context to the RT to be sure all in-flight RT commands have finished using it.*/
		NiagaraEmitterInstanceBatcher* Batcher_RT = Batcher && !Batcher->IsPendingKill() ? Batcher : nullptr;
		ENQUEUE_RENDER_COMMAND(FDeleteContextCommand)(
			[Batcher_RT, ExecContext=GPUExecContext, DataSet= ParticleDataSet](FRHICommandListImmediate& RHICmdList)
			{
				if ( Batcher_RT != nullptr )
				{
					Batcher_RT->ReleaseInstanceCounts_RenderThread(ExecContext, DataSet);
				}
				if ( ExecContext != nullptr )
				{
					delete ExecContext;
				}
				if ( DataSet != nullptr )
				{
					delete DataSet;
				}
			}
		);
			
		GPUExecContext = nullptr;
		ParticleDataSet = nullptr;
	}
	else
	{
		if ( ParticleDataSet != nullptr )
		{
			delete ParticleDataSet;
			ParticleDataSet = nullptr;
		}
	}
}

FBox FNiagaraEmitterInstance::GetBounds()
{
	return CachedBounds;
}

bool FNiagaraEmitterInstance::IsReadyToRun() const
{
	if (!IsDisabled() && !CachedEmitter->IsReadyToRun())
	{
		return false;
	}

	return true;
}

void FNiagaraEmitterInstance::Dump()const
{
	UE_LOG(LogNiagara, Log, TEXT("==  %s ========"), *CachedEmitter->GetUniqueEmitterName());
	UE_LOG(LogNiagara, Log, TEXT(".................Spawn................."));
	SpawnExecContext.Parameters.DumpParameters(true);
	UE_LOG(LogNiagara, Log, TEXT(".................Update................."));
	UpdateExecContext.Parameters.DumpParameters(true);
	if (CachedEmitter->SimTarget == ENiagaraSimTarget::GPUComputeSim && GPUExecContext != nullptr)
	{
		UE_LOG(LogNiagara, Log, TEXT("................. %s Combined Parameters ................."), TEXT("GPU Script"));
		GPUExecContext->CombinedParamStore.DumpParameters();
	}
	ParticleDataSet->Dump(0, INDEX_NONE, TEXT("Particle Data"));
}

bool FNiagaraEmitterInstance::IsAllowedToExecute() const
{
	const FNiagaraEmitterHandle& EmitterHandle = GetEmitterHandle();
	return EmitterHandle.GetIsEnabled() &&
		CachedEmitter->IsAllowedByScalability() &&
		// TODO: fall back to CPU sim instead once we have scalability functionality to do so
		(CachedEmitter->SimTarget != ENiagaraSimTarget::GPUComputeSim || (Batcher && FNiagaraUtilities::AllowGPUParticles(Batcher->GetShaderPlatform())));
}

void FNiagaraEmitterInstance::Init(int32 InEmitterIdx, FNiagaraSystemInstanceID InSystemInstanceID)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEmitterInit);
	check(ParticleDataSet);
	FNiagaraDataSet& Data = *ParticleDataSet;
	EmitterIdx = InEmitterIdx;
	OwnerSystemInstanceID = InSystemInstanceID;
	const FNiagaraEmitterHandle& EmitterHandle = GetEmitterHandle();
	CachedEmitter = EmitterHandle.GetInstance();
	CachedIDName = EmitterHandle.GetIdName();

	MaxAllocationCount = 0;
	ReallocationCount = 0;
	MinOverallocation = -1;

	if (CachedEmitter == nullptr)
	{
		//@todo(message manager) Error bubbling here
		ExecutionState = ENiagaraExecutionState::Disabled;
		return;
	}

	MaxAllocationCount = CachedEmitter->GetMaxParticleCountEstimate();
	if (!IsAllowedToExecute())
	{
		ExecutionState = ENiagaraExecutionState::Disabled;
		return;
	}

	const TArray<TSharedRef<const FNiagaraEmitterCompiledData>>& EmitterCompiledData = ParentSystemInstance->GetSystem()->GetEmitterCompiledData();
	if (EmitterCompiledData.IsValidIndex(EmitterIdx) == false)
	{
		//@todo(message manager) Error bubbling here
		ExecutionState = ENiagaraExecutionState::Disabled;
		return;
	}

	CachedEmitterCompiledData = EmitterCompiledData[EmitterIdx];

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST 
	CheckForErrors();
#endif

	if (IsDisabled())
	{
		return;
	}

	//Init the spawn infos to the correct number for this system.
	int32 NumEvents = CachedEmitter->GetEventHandlers().Num();
	SpawnInfos.SetNum(CachedEmitterCompiledData->SpawnAttributes.Num());

	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraEmitterInit_Datasets);
		Data.Init(&CachedEmitterCompiledData->DataSetCompiledData);

		ResetSimulation();

		DataSetMap.Empty();

		//Add the particle data to the data set map.
		//Currently just used for the tick loop but will also allow access directly to the particle data from other emitters.
		DataSetMap.Add(Data.GetID(), &Data);
		//Warn the user if there are any attributes used in the update script that are not initialized in the spawn script.
		//TODO: We need some window in the System editor and possibly the graph editor for warnings and errors.

		const bool bVerboseAttributeLogging = false;

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST 
		if (bVerboseAttributeLogging)
		{
			for (FNiagaraVariable& Attr : CachedEmitter->UpdateScriptProps.Script->GetVMExecutableData().Attributes)
			{
				int32 FoundIdx;
				if (!CachedEmitter->SpawnScriptProps.Script->GetVMExecutableData().Attributes.Find(Attr, FoundIdx))
				{
					UE_LOG(LogNiagara, Warning, TEXT("Attribute %s is used in the Update script for %s but it is not initialised in the Spawn script!"), *Attr.GetName().ToString(), *EmitterHandle.GetName().ToString());
				}
				for (int32 i = 0; i < CachedEmitter->GetEventHandlers().Num(); i++)
				{
					if (CachedEmitter->GetEventHandlers()[i].Script && !CachedEmitter->GetEventHandlers()[i].Script->GetVMExecutableData().Attributes.Find(Attr, FoundIdx))
					{
						UE_LOG(LogNiagara, Warning, TEXT("Attribute %s is used in the event handler script for %s but it is not initialised in the Spawn script!"), *Attr.GetName().ToString(), *EmitterHandle.GetName().ToString());
					}
				}
			}
		}
#endif

		ensure(CachedEmitter->UpdateScriptProps.DataSetAccessSynchronized());
		UpdateScriptEventDataSets.Empty();
		UpdateEventGeneratorIsSharedByIndex.SetNumZeroed(CachedEmitter->UpdateScriptProps.EventGenerators.Num());
		int32 UpdateEventGeneratorIndex = 0;
		for (const FNiagaraEventGeneratorProperties &GeneratorProps : CachedEmitter->UpdateScriptProps.EventGenerators)
		{
			FNiagaraDataSet *Set = ParentSystemInstance->CreateEventDataSet(EmitterHandle.GetIdName(), GeneratorProps.ID);
			Set->Init(&GeneratorProps.DataSetCompiledData);

			UpdateScriptEventDataSets.Add(Set);
			UpdateEventGeneratorIsSharedByIndex[UpdateEventGeneratorIndex] = CachedEmitter->IsEventGeneratorShared(GeneratorProps.ID);
			UpdateEventGeneratorIndex++;
		}

		ensure(CachedEmitter->SpawnScriptProps.DataSetAccessSynchronized());
		SpawnScriptEventDataSets.Empty();
		SpawnEventGeneratorIsSharedByIndex.SetNumZeroed(CachedEmitter->SpawnScriptProps.EventGenerators.Num());
		int32 SpawnEventGeneratorIndex = 0;
		for (const FNiagaraEventGeneratorProperties &GeneratorProps : CachedEmitter->SpawnScriptProps.EventGenerators)
		{
			FNiagaraDataSet *Set = ParentSystemInstance->CreateEventDataSet(EmitterHandle.GetIdName(), GeneratorProps.ID);

			Set->Init(&GeneratorProps.DataSetCompiledData);

			SpawnScriptEventDataSets.Add(Set);
			SpawnEventGeneratorIsSharedByIndex[SpawnEventGeneratorIndex] = CachedEmitter->IsEventGeneratorShared(GeneratorProps.ID);
			SpawnEventGeneratorIndex++;
		}
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraEmitterInit_ExecContexts);

		SpawnExecContext.Init(CachedEmitter->SpawnScriptProps.Script, CachedEmitter->SimTarget);
		UpdateExecContext.Init(CachedEmitter->UpdateScriptProps.Script, CachedEmitter->SimTarget);

		// setup the parameer store for the GPU execution context; since spawn and update are combined here, we build one with params from both script props
		if (CachedEmitter->SimTarget == ENiagaraSimTarget::GPUComputeSim)
		{
			GPUExecContext = new FNiagaraComputeExecutionContext();
			const uint32 MaxUpdateIterations = CachedEmitter->bDeprecatedShaderStagesEnabled ? CachedEmitter->MaxUpdateIterations : 1;
			GPUExecContext->InitParams(CachedEmitter->GetGPUComputeScript(), CachedEmitter->SimTarget, CachedEmitter->DefaultShaderStageIndex, MaxUpdateIterations, CachedEmitter->SpawnStages);
#if !UE_BUILD_SHIPPING
			GPUExecContext->SetDebugName(CachedEmitter->GetFullName());
#endif
			GPUExecContext->MainDataSet = &Data;
			GPUExecContext->GPUScript_RT = CachedEmitter->GetGPUComputeScript()->GetRenderThreadScript();

			SpawnExecContext.Parameters.Bind(&GPUExecContext->CombinedParamStore);
			UpdateExecContext.Parameters.Bind(&GPUExecContext->CombinedParamStore);

			for (int32 i = 0; i < CachedEmitter->GetSimulationStages().Num(); i++)
			{
				CachedEmitter->GetSimulationStages()[i]->Script->RapidIterationParameters.Bind(&GPUExecContext->CombinedParamStore);
			}
		}
	
		EventExecContexts.SetNum(CachedEmitter->GetEventHandlers().Num());
		for (int32 i = 0; i < NumEvents; i++)
		{
			ensure(CachedEmitter->GetEventHandlers()[i].DataSetAccessSynchronized());

			UNiagaraScript* EventScript = CachedEmitter->GetEventHandlers()[i].Script;

			//This is cpu explicitly? Are we doing event handlers on GPU?
			EventExecContexts[i].Init(EventScript, ENiagaraSimTarget::CPUSim);
		}
	}


	//Setup direct bindings for setting parameter values.
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraEmitterInit_DirectBindings);

		//Setup direct bindings for setting parameter values.
		SpawnIntervalBinding.Init(SpawnExecContext.Parameters, CachedEmitterCompiledData->EmitterSpawnIntervalVar);
		InterpSpawnStartBinding.Init(SpawnExecContext.Parameters, CachedEmitterCompiledData->EmitterInterpSpawnStartDTVar);
		SpawnGroupBinding.Init(SpawnExecContext.Parameters, CachedEmitterCompiledData->EmitterSpawnGroupVar);

		// Initialize the exec count
		SpawnExecCountBinding.Init(SpawnExecContext.Parameters, SYS_PARAM_ENGINE_EXEC_COUNT);
		UpdateExecCountBinding.Init(UpdateExecContext.Parameters, SYS_PARAM_ENGINE_EXEC_COUNT);
		EventExecCountBindings.SetNum(NumEvents);
		for (int32 i = 0; i < NumEvents; i++)
		{
			EventExecCountBindings[i].Init(EventExecContexts[i].Parameters, SYS_PARAM_ENGINE_EXEC_COUNT);
		}
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraEmitterInit_DIDefaultsAndBoundsCalcs);
		// Collect script defined data interface parameters.
		TArray<UNiagaraScript*, TInlineAllocator<8>> Scripts;
		Scripts.Add(CachedEmitter->SpawnScriptProps.Script);
		Scripts.Add(CachedEmitter->UpdateScriptProps.Script);
		for (const FNiagaraEventScriptProperties& EventHandler : CachedEmitter->GetEventHandlers())
		{
			Scripts.Add(EventHandler.Script);
		}
		for (const UNiagaraSimulationStageBase* SimStage : CachedEmitter->GetSimulationStages())
		{
			Scripts.Add(SimStage->Script);
		}
		FNiagaraUtilities::CollectScriptDataInterfaceParameters(*CachedEmitter, MakeArrayView(Scripts), ScriptDefinedDataInterfaceParameters);

		// Initialize bounds calculators - skip creating if we won't ever use it.  We leave the GPU sims in there with the editor so that we can
		// generate the bounds from the readback in the tool.
#if !WITH_EDITOR
		bool bUseDynamicBounds = !CachedSystemFixedBounds.IsSet() && !CachedEmitter->bFixedBounds && CachedEmitter->SimTarget == ENiagaraSimTarget::CPUSim;
		if (bUseDynamicBounds)
#endif
		{
			BoundsCalculators.Reserve(CachedEmitter->GetRenderers().Num());
			for (UNiagaraRendererProperties* RendererProperties : CachedEmitter->GetRenderers())
			{
				if ((RendererProperties != nullptr) && RendererProperties->GetIsEnabled())
				{
					FNiagaraBoundsCalculator* BoundsCalculator = RendererProperties->CreateBoundsCalculator();
					if (BoundsCalculator != nullptr)
					{
						BoundsCalculator->InitAccessors(*ParticleDataSet);
						BoundsCalculators.Emplace(BoundsCalculator);
					}
				}
			}
		}

		//Bind some stores and unbind immediately just to prime some data from those stores.
		FNiagaraParameterStore& SystemScriptDefinedDataInterfaceParameters = ParentSystemInstance->GetSystemSimulation()->GetScriptDefinedDataInterfaceParameters();
		
		SystemScriptDefinedDataInterfaceParameters.Bind(&SpawnExecContext.Parameters);
		ScriptDefinedDataInterfaceParameters.Bind(&SpawnExecContext.Parameters);
		SpawnExecContext.Parameters.UnbindFromSourceStores();

		SystemScriptDefinedDataInterfaceParameters.Bind(&UpdateExecContext.Parameters);
		ScriptDefinedDataInterfaceParameters.Bind(&UpdateExecContext.Parameters);
		UpdateExecContext.Parameters.UnbindFromSourceStores();

		for (FNiagaraScriptExecutionContext& EventContext : EventExecContexts)
		{
			SystemScriptDefinedDataInterfaceParameters.Bind(&EventContext.Parameters);
			ScriptDefinedDataInterfaceParameters.Bind(&EventContext.Parameters);
			EventContext.Parameters.UnbindFromSourceStores();
		}
	}	

	const int32 NumEventHandlers = CachedEmitter->GetEventHandlers().Num();
	EventHandlingInfo.Reset();
	EventHandlingInfo.SetNum(NumEventHandlers);
	for (int32 i = 0; i < NumEventHandlers; i++)
	{
		const FNiagaraEventScriptProperties& EventHandlerProps = CachedEmitter->GetEventHandlers()[i];
		FNiagaraEventHandlingInfo& Info = EventHandlingInfo[i];
		Info.SourceEmitterGuid = EventHandlerProps.SourceEmitterID;
		Info.SourceEmitterName = Info.SourceEmitterGuid.IsValid() ? *Info.SourceEmitterGuid.ToString() : CachedIDName;
		Info.SpawnCounts.Reset();
		Info.TotalSpawnCount = 0;
		Info.EventData = nullptr;
	}

	// Prevent division by 0 in case there are no renderers.
	uint32 MaxGPUBufferComponents = 1;
	if (CachedEmitter->SimTarget == ENiagaraSimTarget::CPUSim && GbEnableMinimalGPUBuffers)
	{
		// CPU emitters only upload the data needed by the renderers to the GPU. Compute the maximum number of components per particle
		// among all the enabled renderers, since this will decide how many particles we can upload.
		for (UNiagaraRendererProperties* RendererProperty : CachedEmitter->GetEnabledRenderers())
		{
			uint32 RendererMaxNumComponents = RendererProperty->ComputeMaxUsedComponents(*ParticleDataSet);
			MaxGPUBufferComponents = FMath::Max(MaxGPUBufferComponents, RendererMaxNumComponents);
		}
	}
	else
	{
		// GPU emitters must store the entire particle payload on GPU buffers, so get the maximum component count from the dataset.
		MaxGPUBufferComponents = FMath::Max(ParticleDataSet->GetNumFloatComponents(), ParticleDataSet->GetNumInt32Components());
	}

	// See how many particles we can fit in a GPU buffer. This number can be quite small on some platforms.
	uint64 MaxBufferElements = (GDebugForcedMaxGPUBufferElements > 0) ? (uint64)GDebugForcedMaxGPUBufferElements : GetMaxBufferDimension();
	// Don't just cast the result of the division to 32-bit, since that will produce garbage if MaxNumInstances is larger than UINT_MAX. Saturate instead.
	MaxInstanceCount = (uint32)FMath::Min(MaxBufferElements / MaxGPUBufferComponents, (uint64)UINT_MAX);

	if (CachedEmitter->SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		// On GPU, the size of the allocated buffers must be a multiple of NIAGARA_COMPUTE_THREADGROUP_SIZE, so round down.
		MaxInstanceCount = (MaxInstanceCount / NIAGARA_COMPUTE_THREADGROUP_SIZE) * NIAGARA_COMPUTE_THREADGROUP_SIZE;
		// We will need an extra scratch instance, so the maximum number of usable instances is one less than the value we computed.
		MaxInstanceCount -= 1;
	}

	ParticleDataSet->SetMaxInstanceCount(MaxInstanceCount);
}

void FNiagaraEmitterInstance::ResetSimulation(bool bKillExisting /*= true*/)
{
	EmitterAge = 0;
	TickCount = 0;
	CachedBounds.Init();

	if (MinOverallocation > 100 && GbNiagaraShowAllocationWarnings)
	{
		FString SystemName = this->GetParentSystemInstance()->GetSystem()->GetName();
		FString FullName = SystemName + "::" + this->GetEmitterHandle().GetName().ToString();
		UE_LOG(LogNiagara, Warning, TEXT("The emitter %s over-allocated %i particles during its runtime. If this happens frequently, consider setting the emitter's AllocationMode property to 'manual' to improve runtime performance."), *FullName, MinOverallocation);
	}

	if (IsDisabled())
	{
		return;
	}

	SetExecutionState(ENiagaraExecutionState::Active);

	if (bKillExisting)
	{
		bResetPending = true;
		TotalSpawnedParticles = 0;

		ParticleDataSet->ResetBuffers();
		for (FNiagaraDataSet* SpawnScriptEventDataSet : SpawnScriptEventDataSets)
		{
			SpawnScriptEventDataSet->ResetBuffers();
		}
		for (FNiagaraDataSet* UpdateScriptEventDataSet : UpdateScriptEventDataSets)
		{
			UpdateScriptEventDataSet->ResetBuffers();
		}

		if (CachedEmitter->SimTarget == ENiagaraSimTarget::GPUComputeSim && GPUExecContext != nullptr)
		{
			GPUExecContext->Reset(Batcher);
		}
	}
}

void FNiagaraEmitterInstance::CheckForErrors()
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEmitterErrorCheck);
	
	checkSlow(CachedEmitter);

	//Check for various failure conditions and bail.
	if (!CachedEmitter->UpdateScriptProps.Script || !CachedEmitter->SpawnScriptProps.Script )
	{
		//TODO - Arbitrary named scripts. Would need some base functionality for Spawn/Udpate to be called that can be overriden in BPs for emitters with custom scripts.
		UE_LOG(LogNiagara, Error, TEXT("Emitter cannot be enabled because it's doesn't have both an update and spawn script."), *CachedEmitter->GetFullName());
		SetExecutionState(ENiagaraExecutionState::Disabled);
		return;
	}

	if (!CachedEmitter->UpdateScriptProps.Script->IsReadyToRun(ENiagaraSimTarget::CPUSim) || !CachedEmitter->SpawnScriptProps.Script->IsReadyToRun(ENiagaraSimTarget::CPUSim))
	{
		//TODO - Arbitrary named scripts. Would need some base functionality for Spawn/Udpate to be called that can be overriden in BPs for emitters with custom scripts.
		UE_LOG(LogNiagara, Error, TEXT("Emitter cannot be enabled because it's doesn't have both an update and spawn script ready to run CPU scripts."), *CachedEmitter->GetFullName());
		SetExecutionState(ENiagaraExecutionState::Disabled);
		return;
	}

	if (CachedEmitter->SpawnScriptProps.Script->GetVMExecutableData().DataUsage.bReadsAttributeData)
	{
		UE_LOG(LogNiagara, Error, TEXT("%s reads attribute data and so cannot be used as a spawn script. The data being read would be invalid."), *CachedEmitter->SpawnScriptProps.Script->GetName());
		SetExecutionState(ENiagaraExecutionState::Disabled);
		return;
	}
	if (CachedEmitter->UpdateScriptProps.Script->GetVMExecutableData().Attributes.Num() == 0 || CachedEmitter->SpawnScriptProps.Script->GetVMExecutableData().Attributes.Num() == 0)
	{
		UE_LOG(LogNiagara, Error, TEXT("This emitter cannot be enabled because its spawn or update script doesn't have any attributes.."));
		SetExecutionState(ENiagaraExecutionState::Disabled);
		return;
	}

	if (CachedEmitter->SimTarget == ENiagaraSimTarget::CPUSim)
	{
		bool bFailed = false;
		if (!CachedEmitter->SpawnScriptProps.Script->DidScriptCompilationSucceed(false))
		{
			bFailed = true;
			UE_LOG(LogNiagara, Error, TEXT("This emitter cannot be enabled because its CPU Spawn script failed to compile."));
		}

		if (!CachedEmitter->UpdateScriptProps.Script->DidScriptCompilationSucceed(false))
		{
			bFailed = true;
			UE_LOG(LogNiagara, Error, TEXT("This emitter cannot be enabled because its CPU Update script failed to compile."));
		}

		if (CachedEmitter->GetEventHandlers().Num() != 0)
		{
			for (int32 i = 0; i < CachedEmitter->GetEventHandlers().Num(); i++)
			{
				if (!CachedEmitter->GetEventHandlers()[i].Script->DidScriptCompilationSucceed(false))
				{
					bFailed = true;
					UE_LOG(LogNiagara, Error, TEXT("This emitter cannot be enabled because one of its CPU Event scripts failed to compile."));
				}
			}
		}

		if (bFailed)
		{
			SetExecutionState(ENiagaraExecutionState::Disabled);
			return;
		}
	}

	if (CachedEmitter->SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		if (CachedEmitter->GetGPUComputeScript()->IsScriptCompilationPending(true))
		{
			UE_LOG(LogNiagara, Error, TEXT("This emitter cannot be enabled because its GPU script hasn't been compiled.."));
			SetExecutionState(ENiagaraExecutionState::Disabled);
			return;
		}
		if (!CachedEmitter->GetGPUComputeScript()->DidScriptCompilationSucceed(true))
		{
			UE_LOG(LogNiagara, Error, TEXT("This emitter cannot be enabled because its GPU script failed to compile."));
			SetExecutionState(ENiagaraExecutionState::Disabled);
			return;
		}
	}
}

void FNiagaraEmitterInstance::DirtyDataInterfaces()
{
	if (IsDisabled())
	{
		return;
	}

	// Make sure that our function tables need to be regenerated...
	SpawnExecContext.DirtyDataInterfaces();
	UpdateExecContext.DirtyDataInterfaces();

	if (CachedEmitter->SimTarget == ENiagaraSimTarget::GPUComputeSim && GPUExecContext != nullptr)
	{
		GPUExecContext->DirtyDataInterfaces();
	}

	for (FNiagaraScriptExecutionContext& EventContext : EventExecContexts)
	{
		EventContext.DirtyDataInterfaces();
	}
}

//Unsure on usage of this atm. Possibly useful in future.
// void FNiagaraEmitterInstance::RebindParameterCollection(UNiagaraParameterCollectionInstance* OldInstance, UNiagaraParameterCollectionInstance* NewInstance)
// {
// 	OldInstance->GetParameterStore().Unbind(&SpawnExecContext.Parameters);
// 	NewInstance->GetParameterStore().Bind(&SpawnExecContext.Parameters);
// 
// 	OldInstance->GetParameterStore().Unbind(&UpdateExecContext.Parameters);
// 	NewInstance->GetParameterStore().Bind(&UpdateExecContext.Parameters);
// 
// 	for (FNiagaraScriptExecutionContext& EventContext : EventExecContexts)
// 	{
// 		OldInstance->GetParameterStore().Unbind(&EventContext.Parameters);
// 		NewInstance->GetParameterStore().Bind(&EventContext.Parameters);
// 	}
// }

void FNiagaraEmitterInstance::UnbindParameters(bool bExternalOnly)
{
	if (bExternalOnly && !IsDisabled())
	{
		for (UNiagaraParameterCollection* Collection : SpawnExecContext.Script->GetCachedParameterCollectionReferences())
		{
			if (UNiagaraParameterCollectionInstance* NPCInst = ParentSystemInstance->GetParameterCollectionInstance(Collection))
			{
				NPCInst->GetParameterStore().Unbind(&SpawnExecContext.Parameters);
			}
		}
		for (UNiagaraParameterCollection* Collection : UpdateExecContext.Script->GetCachedParameterCollectionReferences())
		{
			if (UNiagaraParameterCollectionInstance* NPCInst = ParentSystemInstance->GetParameterCollectionInstance(Collection))
			{
				NPCInst->GetParameterStore().Unbind(&UpdateExecContext.Parameters);
			}
		}

		for (int32 EventIdx = 0; EventIdx < EventExecContexts.Num(); ++EventIdx)
		{
			for (UNiagaraParameterCollection* Collection : EventExecContexts[EventIdx].Script->GetCachedParameterCollectionReferences())
			{
				if (UNiagaraParameterCollectionInstance* NPCInst = ParentSystemInstance->GetParameterCollectionInstance(Collection))
				{
					NPCInst->GetParameterStore().Unbind(&EventExecContexts[EventIdx].Parameters);
				}
			}
		}
	}
	else
	{
		SpawnExecContext.Parameters.UnbindFromSourceStores();
		UpdateExecContext.Parameters.UnbindFromSourceStores();
		if (GPUExecContext != nullptr)
		{
			GPUExecContext->CombinedParamStore.UnbindFromSourceStores();
		}

		for (int32 EventIdx = 0; EventIdx < EventExecContexts.Num(); ++EventIdx)
		{
			EventExecContexts[EventIdx].Parameters.UnbindFromSourceStores();
		}
	}
}

void FNiagaraEmitterInstance::BindParameters(bool bExternalOnly)
{
	if (IsDisabled())
	{
		return;
	}

	for (UNiagaraParameterCollection* Collection : SpawnExecContext.Script->GetCachedParameterCollectionReferences())
	{
		ParentSystemInstance->GetParameterCollectionInstance(Collection)->GetParameterStore().Bind(&SpawnExecContext.Parameters);
	}
	for (UNiagaraParameterCollection* Collection : UpdateExecContext.Script->GetCachedParameterCollectionReferences())
	{
		ParentSystemInstance->GetParameterCollectionInstance(Collection)->GetParameterStore().Bind(&UpdateExecContext.Parameters);
	}

	for (int32 EventIdx = 0; EventIdx < EventExecContexts.Num(); ++EventIdx)
	{
		for (UNiagaraParameterCollection* Collection : EventExecContexts[EventIdx].Script->GetCachedParameterCollectionReferences())
		{
			ParentSystemInstance->GetParameterCollectionInstance(Collection)->GetParameterStore().Bind(&EventExecContexts[EventIdx].Parameters);
		}
	}

	if (!bExternalOnly)
	{
		//Now bind parameters from the component and system.
		FNiagaraParameterStore& InstanceParams = ParentSystemInstance->GetInstanceParameters();		
	
		InstanceParams.Bind(&SpawnExecContext.Parameters);
		InstanceParams.Bind(&UpdateExecContext.Parameters);

		for (FNiagaraScriptExecutionContext& EventContext : EventExecContexts)
		{
			InstanceParams.Bind(&EventContext.Parameters);
		}

	#if WITH_EDITORONLY_DATA
		CachedEmitter->SpawnScriptProps.Script->RapidIterationParameters.Bind(&SpawnExecContext.Parameters);
		CachedEmitter->UpdateScriptProps.Script->RapidIterationParameters.Bind(&UpdateExecContext.Parameters);
		ensure(CachedEmitter->GetEventHandlers().Num() == EventExecContexts.Num());
		for (int32 i = 0; i < CachedEmitter->GetEventHandlers().Num(); i++)
		{
			CachedEmitter->GetEventHandlers()[i].Script->RapidIterationParameters.Bind(&EventExecContexts[i].Parameters);
		}
	#endif

		if (CachedEmitter->SimTarget == ENiagaraSimTarget::GPUComputeSim)
		{
			SpawnExecContext.Parameters.Bind(&GPUExecContext->CombinedParamStore);
			UpdateExecContext.Parameters.Bind(&GPUExecContext->CombinedParamStore);

			for (int32 i = 0; i < CachedEmitter->GetSimulationStages().Num(); i++)
			{
				CachedEmitter->GetSimulationStages()[i]->Script->RapidIterationParameters.Bind(&GPUExecContext->CombinedParamStore);
			}
		}
	}
}

FNiagaraDataSet* FNiagaraEmitterInstance::GetDataSet(FNiagaraDataSetID SetID)
{
	FNiagaraDataSet** SetPtr = DataSetMap.Find(SetID);
	FNiagaraDataSet* Ret = NULL;
	if (SetPtr)
	{
		Ret = *SetPtr;
	}
	else
	{
		// TODO: keep track of data sets generated by the scripts (event writers) and find here
	}

	return Ret;
}

const FNiagaraEmitterHandle& FNiagaraEmitterInstance::GetEmitterHandle() const
{
	UNiagaraSystem* Sys = ParentSystemInstance->GetSystem();
	checkSlow(Sys->GetEmitterHandles().Num() > EmitterIdx);
	return Sys->GetEmitterHandles()[EmitterIdx];
}

float FNiagaraEmitterInstance::GetTotalCPUTimeMS()
{
	uint32 TotalCycles = CPUTimeCycles;

	//TODO: Find some way to include the RT cost here?
	//Possibly have the proxy write back it's most recent frame time during EOF updates?
// 	for (int32 i = 0; i < EmitterRenderer.Num(); i++)
// 	{
// 		if (EmitterRenderer[i])
// 		{
// 			Total += EmitterRenderer[i]->GetCPUTimeMS();// 
// 		}
// 	}

	return FPlatformTime::ToMilliseconds(TotalCycles);
}

int FNiagaraEmitterInstance::GetTotalBytesUsed()
{
	check(ParticleDataSet);
	int32 BytesUsed = ParticleDataSet->GetSizeBytes();
	/*
	for (FNiagaraDataSet& Set : DataSets)
	{
		BytesUsed += Set.GetSizeBytes();
	}
	*/
	return BytesUsed;
}

FBox FNiagaraEmitterInstance::InternalCalculateDynamicBounds(int32 ParticleCount) const
{
	if (!ParticleCount || !BoundsCalculators.Num())
	{
		return FBox(ForceInit);
	}

	FBox Ret;
	Ret.Init();

	for (const TUniquePtr<FNiagaraBoundsCalculator>& BoundsCalculator : BoundsCalculators)
	{
		BoundsCalculator->RefreshAccessors();
		Ret += BoundsCalculator->CalculateBounds(ParticleCount);
	}

	return Ret;
}

#if WITH_EDITOR
void FNiagaraEmitterInstance::CalculateFixedBounds(const FTransform& ToWorldSpace)
{
	check(CachedEmitter);

	if (IsComplete() || CachedEmitter == nullptr)
	{
		return ;
	}

	FScopedNiagaraDataSetGPUReadback ScopedGPUReadback;

	int32 NumInstances = 0;
	if (CachedEmitter->SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		if (GPUExecContext == nullptr)
		{
			return;
		}

		ScopedGPUReadback.ReadbackData(Batcher, GPUExecContext->MainDataSet);
		NumInstances = ScopedGPUReadback.GetNumInstances();
	}
	else
	{
		NumInstances = ParticleDataSet->GetCurrentDataChecked().GetNumInstances();
	}

	if (NumInstances == 0)
	{
		return;
	}

	FBox Bounds = InternalCalculateDynamicBounds(NumInstances);
	if (!Bounds.IsValid)
		return;

	CachedEmitter->Modify();
	CachedEmitter->bFixedBounds = true;
	if (CachedEmitter->bLocalSpace)
	{
		CachedEmitter->FixedBounds = Bounds;
	}
	else
	{
		CachedEmitter->FixedBounds = Bounds.TransformBy(ToWorldSpace);
	}

	CachedBounds = Bounds;
}
#endif

/** 
  * Do any post work such as calculating dynamic bounds.
  */
void FNiagaraEmitterInstance::PostTick()
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEmitterPostTick);

	checkSlow(CachedEmitter);

	//Clear refs to event data buffers.
	for (FNiagaraEventHandlingInfo& Info : EventHandlingInfo)
	{
		Info.SetEventData(nullptr);
	}

	CachedBounds.Init();
	if (CachedSystemFixedBounds.IsSet())
	{
		CachedBounds = CachedSystemFixedBounds.GetValue();
	}
	else if (CachedEmitter->bFixedBounds || CachedEmitter->SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		CachedBounds = CachedEmitter->FixedBounds;
	}
	else
	{
		FBox DynamicBounds = InternalCalculateDynamicBounds(ParticleDataSet->GetCurrentDataChecked().GetNumInstances());
		if (DynamicBounds.IsValid)
		{
			if (CachedEmitter->bLocalSpace)
			{
				CachedBounds = DynamicBounds;
			}
			else
			{
				CachedBounds = DynamicBounds.TransformBy(ParentSystemInstance->GetOwnerParameters().EngineWorldToLocal);
			}
		}
		else
		{
			CachedBounds = CachedEmitter->FixedBounds;
		}
	}
}

bool FNiagaraEmitterInstance::HandleCompletion(bool bForce)
{
	if (IsDisabled())
	{
		return true;
	}

	if (bForce)
	{
		SetExecutionState(ENiagaraExecutionState::Complete);
	}

	if (IsComplete())
	{
		ParticleDataSet->ResetBuffers();
		return true;
	}

	return false;
}

bool FNiagaraEmitterInstance::RequiresPersistentIDs() const
{
	//TODO: can we have this be enabled at runtime from outside the system?
	return GetEmitterHandle().GetInstance()->RequiresPersistentIDs() || ParticleDataSet->HasVariable(SYS_PARAM_PARTICLES_ID);
}

/** 
  * PreTick - handles killing dead particles, emitter death, and buffer swaps
  */
void FNiagaraEmitterInstance::PreTick()
{
	if (IsComplete())
	{
		return;
	}

#if STATS
	FScopeCycleCounter SystemStatCounter(CachedEmitter->GetStatID(true, true));
#endif

	checkSlow(ParticleDataSet);
	FNiagaraDataSet& Data = *ParticleDataSet;

#if WITH_EDITOR
	CachedEmitter->SpawnScriptProps.Script->RapidIterationParameters.Tick();
	CachedEmitter->UpdateScriptProps.Script->RapidIterationParameters.Tick();
	ensure(CachedEmitter->GetEventHandlers().Num() == EventExecContexts.Num());
	for (int32 i = 0; i < CachedEmitter->GetEventHandlers().Num(); i++)
	{
		CachedEmitter->GetEventHandlers()[i].Script->RapidIterationParameters.Tick();
	}
#endif


	bool bOk = true;
	bOk &= SpawnExecContext.Tick(ParentSystemInstance);
	bOk &= UpdateExecContext.Tick(ParentSystemInstance);

	// @todo THREADSAFETY We should not tick GPU contexts on the game thread!
	if (CachedEmitter->SimTarget == ENiagaraSimTarget::GPUComputeSim && GPUExecContext != nullptr)
	{
		bOk &= GPUExecContext->Tick(ParentSystemInstance);
	}
	for (FNiagaraScriptExecutionContext& EventContext : EventExecContexts)
	{
		bOk &= EventContext.Tick(ParentSystemInstance);
	}

	if (!bOk)
	{
		ResetSimulation();
		SetExecutionState(ENiagaraExecutionState::Disabled);
		return;
	}

	if (TickCount == 0)
	{
		//On our very first frame we prime any previous params (for interpolation).
		SpawnExecContext.PostTick();
		UpdateExecContext.PostTick();
		if (CachedEmitter->SimTarget == ENiagaraSimTarget::GPUComputeSim && GPUExecContext != nullptr)
		{
			//We PostTick the GPUExecContext here to prime crucial PREV parameters (such as PREV_Engine.Owner.Position). This PostTick call is necessary as the GPUExecContext has not been sent to the batcher yet.
			GPUExecContext->PostTick();
		}
		for (FNiagaraScriptExecutionContext& EventContext : EventExecContexts)
		{
			EventContext.PostTick();
		}
	}

	checkSlow(Data.GetNumVariables() > 0);
	checkSlow(CachedEmitter->SpawnScriptProps.Script);
	checkSlow(CachedEmitter->UpdateScriptProps.Script);

	if (bResetPending)
	{
		Data.ResetBuffers();
		for (FNiagaraDataSet* SpawnScriptEventDataSet : SpawnScriptEventDataSets)
		{
			SpawnScriptEventDataSet->ResetBuffers();
		}
		for (FNiagaraDataSet* UpdateScriptEventDataSet : UpdateScriptEventDataSets)
		{
			UpdateScriptEventDataSet->ResetBuffers();
		}
		bResetPending = false;
	}

	++TickCount;
	ParticleDataSet->SetIDAcquireTag(TickCount);
}

bool FNiagaraEmitterInstance::WaitForDebugInfo()
{
	FNiagaraComputeExecutionContext* DebugContext = GPUExecContext;
	if (CachedEmitter->SimTarget == ENiagaraSimTarget::GPUComputeSim && DebugContext)
	{
		ENQUEUE_RENDER_COMMAND(CaptureCommand)([=](FRHICommandListImmediate& RHICmdList)
		{
			Batcher->ProcessDebugInfo(RHICmdList, GPUExecContext);
		});
		return true;
	}
	return false;
}

void FNiagaraEmitterInstance::SetSystemFixedBoundsOverride(FBox SystemFixedBounds)
{
	CachedSystemFixedBounds = SystemFixedBounds;
}

static int32 GbTriggerCrash = 0;
static FAutoConsoleVariableRef CVarTriggerCrash(
	TEXT("fx.TriggerDebugCrash"),
	GbTriggerCrash,
	TEXT("If > 0 we deliberately crash to test Crash Reporter integration."),
	ECVF_Default
);

FORCENOINLINE void NiagaraTestCrash()
{
	check(0);
}

void FNiagaraEmitterInstance::Tick(float DeltaSeconds)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraTick);
	FNiagaraEditorOnlyCycleTimer<false> TickTime(CPUTimeCycles);

#if STATS
	FScopeCycleCounter SystemStatCounter(CachedEmitter->GetStatID(true, true));
#endif

	if (HandleCompletion())
	{
		return;
	}

	//Test crash allowing us to test CR functionality.
#if !UE_BUILD_SHIPPING
	if (GbTriggerCrash)
	{
		GbTriggerCrash = 0;
		NiagaraTestCrash();
	}
#endif

	checkSlow(ParticleDataSet);
	FNiagaraDataSet& Data = *ParticleDataSet;
	EmitterAge += DeltaSeconds;

	//UE_LOG(LogNiagara, Warning, TEXT("Emitter Tick %f"), EmitterAge);

	if (ExecutionState == ENiagaraExecutionState::InactiveClear)
	{
		Data.ResetBuffers();
		ExecutionState = ENiagaraExecutionState::Inactive;
		return;
	}

	if (CachedEmitter->SimTarget == ENiagaraSimTarget::CPUSim && Data.GetCurrentDataChecked().GetNumInstances() == 0 && ExecutionState != ENiagaraExecutionState::Active)
	{
		Data.ResetBuffers();
		return;
	}

	UNiagaraSystem* System = ParentSystemInstance->GetSystem();

	if (GbDumpParticleData || System->bDumpDebugEmitterInfo)
	{
		UE_LOG(LogNiagara, Log, TEXT("|||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||"), *CachedEmitter->GetPathName());
		UE_LOG(LogNiagara, Log, TEXT("|=== FNiagaraEmitterInstance::Tick [ %s ] ===============|"), *CachedEmitter->GetPathName());
	}


	checkSlow(Data.GetNumVariables() > 0);
	checkSlow(CachedEmitter->SpawnScriptProps.Script);
	checkSlow(CachedEmitter->UpdateScriptProps.Script);
	
	// Set up the spawn counts and source datasets for the events. The system ensures that we will run after any emitters
	// we're receiving from, so we can use the data buffers that our sources have computed this tick.
	const int32 NumEventHandlers = CachedEmitter->GetEventHandlers().Num();
	EventSpawnTotal = 0;
	for (int32 i = 0; i < NumEventHandlers; i++)
	{
		const FNiagaraEventScriptProperties& EventHandlerProps = CachedEmitter->GetEventHandlers()[i];
		FNiagaraEventHandlingInfo& Info = EventHandlingInfo[i];

		Info.TotalSpawnCount = 0;//This was being done every frame but should be done in init?
		Info.SpawnCounts.Reset();

		//TODO: We can move this lookup into the init and just store a ptr to the other set?
		if (FNiagaraDataSet* EventSet = ParentSystemInstance->GetEventDataSet(Info.SourceEmitterName, EventHandlerProps.SourceEventName))
		{
			Info.SetEventData(&EventSet->GetCurrentDataChecked());
			uint32 EventSpawnNum = CalculateEventSpawnCount(EventHandlerProps, Info.SpawnCounts, EventSet);
			Info.TotalSpawnCount += EventSpawnNum;
			EventSpawnTotal += EventSpawnNum;
		}
	}

	// Calculate number of new particles from regular spawning 
	uint32 SpawnTotal = 0;
	if (ExecutionState == ENiagaraExecutionState::Active)
	{
		for (FNiagaraSpawnInfo& Info : SpawnInfos)
		{
			if (Info.Count > 0)
			{
				SpawnTotal += Info.Count;
			}
		}
	}
	int32 OrigNumParticles = GetNumParticles();
	int32 AllocationEstimate = CachedEmitter->GetMaxParticleCountEstimate();
	int32 RequiredSize = OrigNumParticles + SpawnTotal + EventSpawnTotal;

	if (RequiredSize == 0)
	{
		//Early out if we have no particles to process.
		//return;
	}

	int32 AllocationSize = FMath::Max<int32>(AllocationEstimate, RequiredSize);
	AllocationSize = (int32)FMath::Min((uint32)AllocationSize, MaxInstanceCount);

	if (AllocationSize > MaxAllocationCount)
	{
		ReallocationCount++;
		MaxAllocationCount = AllocationSize;
		int32 Estimations = CachedEmitter->AddRuntimeAllocation((uint64)this, MaxAllocationCount);
		if (GbNiagaraShowAllocationWarnings && Estimations >= 5 && ReallocationCount == 3)
		{
			FString SystemName = System->GetName();
			FString FullName = SystemName + "::" + this->GetEmitterHandle().GetName().ToString();
			UE_LOG(LogNiagara, Warning, TEXT("The emitter %s required many memory reallocation due to changing particle counts. Consider setting the emitter's AllocationMode property to 'manual' to improve runtime performance."), *FullName);
		}
	}
	int32 Overallocation = AllocationSize - RequiredSize;
	if (Overallocation >= 0 && (MinOverallocation < 0 || Overallocation < MinOverallocation))
	{
		MinOverallocation = Overallocation;
	}

	// add system constants
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraConstants);

		auto& EmitterParameters = ParentSystemInstance->EditEmitterParameters(EmitterIdx);
		EmitterParameters.EmitterTotalSpawnedParticles = TotalSpawnedParticles;
		EmitterParameters.EmitterAge = EmitterAge;
		EmitterParameters.EmitterRandomSeed = CachedEmitter->RandomSeed;
	}

	/* GPU simulation -  we just create an FNiagaraComputeExecutionContext, queue it, and let the batcher take care of the rest
	 */
	if (CachedEmitter->SimTarget == ENiagaraSimTarget::GPUComputeSim && GPUExecContext != nullptr)
	{
		check(GPUExecContext->GPUScript_RT == CachedEmitter->GetGPUComputeScript()->GetRenderThreadScript());
		GPUExecContext->GPUScript_RT = CachedEmitter->GetGPUComputeScript()->GetRenderThreadScript();

#if WITH_EDITORONLY_DATA
		if (ParentSystemInstance->ShouldCaptureThisFrame())
		{
			TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe> DebugInfo = ParentSystemInstance->GetActiveCaptureWrite(CachedIDName, ENiagaraScriptUsage::ParticleGPUComputeScript, FGuid());
			if (DebugInfo)
			{
				//Data.Dump(DebugInfo->Frame, true, 0, OrigNumParticles);
				//DebugInfo->Frame.Dump(true, 0, OrigNumParticles);
				DebugInfo->Parameters = GPUExecContext->CombinedParamStore;
				
				//TODO: This layout info can be pulled into the emitter/systems etc and all sets just refer to them. They are becoming an annoyance here.
				DebugInfo->Frame.Init(&CachedEmitterCompiledData->GPUCaptureDataSetCompiledData);

				GPUExecContext->DebugInfo = DebugInfo;
			}
		}
#endif

		// Calculate spawn information to pass to the RT
		{
			static_assert(((NIAGARA_MAX_GPU_SPAWN_INFOS % 4) == 0) && (NIAGARA_MAX_GPU_SPAWN_INFOS > 0), "NIAGARA_MAX_GPU_SPAWN_INFOS should be greater than zero and a multiple of 4");

			FNiagaraGpuSpawnInfo& GpuSpawnInfo = GPUExecContext->GpuSpawnInfo_GT;
			GpuSpawnInfo.EventSpawnTotal = EventSpawnTotal;
			GpuSpawnInfo.SpawnRateInstances = 0;
			GpuSpawnInfo.MaxParticleCount = AllocationSize;

			int NumSpawnInfos = 0;
			int32 NumSpawnedOnGPUThisFrame = 0;
			if (ExecutionState == ENiagaraExecutionState::Active)
			{
				for (int32 SpawnInfoIdx = 0; SpawnInfoIdx < SpawnInfos.Num(); SpawnInfoIdx++)
				{
					const FNiagaraSpawnInfo& Info = SpawnInfos[SpawnInfoIdx];
					if (Info.Count > 0 && (NumSpawnInfos < NIAGARA_MAX_GPU_SPAWN_INFOS))
					{
						// Ideally, we should clamp the spawn count here, to make sure that we don't exceed the maximum number of particles. However, the value returned by
						// GetNumParticles() can lag behind the real number, so we can't actually determine on the game thread how many particles we're still allowed to
						// spawn. Therefore, we'll send the spawn requests to the render thread as if there was no limit, and we'll clamp the values there, when we prepare
						// the destination dataset for simulation.
						NumSpawnedOnGPUThisFrame += Info.Count;
						if (NumSpawnedOnGPUThisFrame > GMaxNiagaraGPUParticlesSpawnPerFrame)
						{
							UE_LOG(LogNiagara, Warning, TEXT("%s has attempted to execeed max GPU per frame spawn! | Max: %d | Requested: %d | SpawnInfoEntry: %d"), *CachedEmitter->GetUniqueEmitterName(), GMaxNiagaraGPUParticlesSpawnPerFrame, NumSpawnedOnGPUThisFrame, SpawnInfoIdx);
							break;
						}

						GpuSpawnInfo.SpawnInfoParams[NumSpawnInfos].IntervalDt = Info.IntervalDt;
						GpuSpawnInfo.SpawnInfoParams[NumSpawnInfos].InterpStartDt = Info.InterpStartDt;
						GpuSpawnInfo.SpawnInfoParams[NumSpawnInfos].SpawnGroup = Info.SpawnGroup;
						GpuSpawnInfo.SpawnInfoParams[NumSpawnInfos].GroupSpawnStartIndex = (int32)GpuSpawnInfo.SpawnRateInstances;

						GpuSpawnInfo.SpawnRateInstances += Info.Count;
						GpuSpawnInfo.SpawnInfoStartOffsets[NumSpawnInfos] = (int32)GpuSpawnInfo.SpawnRateInstances;

						++NumSpawnInfos;
					}
					else if (Info.Count > 0)
					{
						UE_LOG(LogNiagara, Warning, TEXT("%s Exceeded Gpu spawn info count, see NIAGARA_MAX_GPU_SPAWN_INFOS for more information!"), *CachedEmitter->GetUniqueEmitterName());
						break;
					}

					// Warning: this will be be inaccurate if the render thread clamps the spawn count to keep the total particle count below the limit.
					TotalSpawnedParticles += Info.Count;
				}
			}

			// Clear out the remaining data and leave the end slot as MAX to avoid reading off end of the array on the GPU
			while (NumSpawnInfos < NIAGARA_MAX_GPU_SPAWN_INFOS)
			{
				GpuSpawnInfo.SpawnInfoParams[NumSpawnInfos].IntervalDt = 0.0f;
				GpuSpawnInfo.SpawnInfoParams[NumSpawnInfos].InterpStartDt = 0.0f;
				GpuSpawnInfo.SpawnInfoParams[NumSpawnInfos].SpawnGroup = 0;
				GpuSpawnInfo.SpawnInfoParams[NumSpawnInfos].GroupSpawnStartIndex = (int32)GpuSpawnInfo.SpawnRateInstances;
				GpuSpawnInfo.SpawnInfoStartOffsets[NumSpawnInfos] = INT32_MAX;
				++NumSpawnInfos;
			}
		}

		//GPUExecContext.UpdateInterfaces = CachedEmitter->UpdateScriptProps.Script->GetCachedDefaultDataInterfaces();

		// copy over the constants for the render thread
		//
		if (GbDumpParticleData || System->bDumpDebugEmitterInfo)
		{
			UE_LOG(LogNiagara, Log, TEXT(".................Spawn................."));
			SpawnExecContext.Parameters.DumpParameters(true);
			UE_LOG(LogNiagara, Log, TEXT(".................Update................."));
			UpdateExecContext.Parameters.DumpParameters(true);
			UE_LOG(LogNiagara, Log, TEXT("................. %s Combined Parameters (%d Spawned )................."), TEXT("GPU Script"), SpawnTotal);
			GPUExecContext->CombinedParamStore.DumpParameters();
		}

		int32 ParmSize = GPUExecContext->CombinedParamStore.GetPaddedParameterSizeInBytes();
		// Because each context is only ran once each frame, the CBuffer layout stays constant for the lifetime duration of the CBuffer (one frame).

		// @todo-threadsafety do this once during init. Should not change during runtime...
		GPUExecContext->ExternalCBufferLayout.ConstantBufferSize = ParmSize / (GPUExecContext->HasInterpolationParameters ? 2 : 1);
		GPUExecContext->ExternalCBufferLayout.ComputeHash();

		// Need to call post-tick, which calls the copy to previous for interpolated spawning
		SpawnExecContext.PostTick();
		UpdateExecContext.PostTick();
		// At this stage GPU execution is being handled by the batcher so we do not need to call PostTick() for it
		for (FNiagaraScriptExecutionContext& EventContext : EventExecContexts)
		{
			EventContext.PostTick();
		}

		CachedBounds = CachedEmitter->FixedBounds;

		/*if (CachedEmitter->SpawnScriptProps.Script->GetComputedVMCompilationId().HasInterpolatedParameters())
		{
			GPUExecContext.CombinedParamStore.CopyCurrToPrev();
		}*/

		return;
	}

	//Ensure we don't blow our current hard limits on cpu particle count.
	//TODO: These current limits can be improved relatively easily. Though perf in at these counts will obviously be an issue anyway.
	if (CachedEmitter->SimTarget == ENiagaraSimTarget::CPUSim && AllocationSize > GMaxNiagaraCPUParticlesPerEmitter)
	{
		UE_LOG(LogNiagara, Warning, TEXT("%s has attempted to exceed the max CPU particle count! | Max: %d | Requested: %u"), *CachedEmitter->GetFullName(), GMaxNiagaraCPUParticlesPerEmitter, AllocationSize);

		//We clear the emitters estimate otherwise we get stuck in this state forever.
		CachedEmitter->ClearRuntimeAllocationEstimate();

		//For now we completely bail out of spawning new particles. Possibly should improve this in future.
		AllocationSize = OrigNumParticles;
		SpawnTotal = 0;
		EventSpawnTotal = 0;

		for (FNiagaraEventHandlingInfo& Info : EventHandlingInfo)
		{
			Info.SpawnCounts.Empty();
			Info.TotalSpawnCount = 0;
		}
	}

	Data.BeginSimulate();
	Data.Allocate(AllocationSize);

	int32 SpawnEventGeneratorIndex = 0;
	for (FNiagaraDataSet* SpawnEventDataSet : SpawnScriptEventDataSets)
	{
		int32 NumToAllocate = SpawnTotal + EventSpawnTotal;
		if (SpawnEventGeneratorIsSharedByIndex[SpawnEventGeneratorIndex])
		{
			// For shared event data sets we need to allocate storage for the current particles since
			// the same data set will be used in the update execution.
			NumToAllocate += OrigNumParticles;
		}
		SpawnEventDataSet->BeginSimulate();
		SpawnEventDataSet->Allocate(NumToAllocate);
		SpawnEventGeneratorIndex++;
	}

	int32 UpdateEventGeneratorIndex = 0;
	for (FNiagaraDataSet* UpdateEventDataSet : UpdateScriptEventDataSets)
	{
		if (UpdateEventGeneratorIsSharedByIndex[UpdateEventGeneratorIndex] == false)
		{
			// We only allocate update event data sets if they're not shared, because shared event datasets will have already
			// been allocated as part of the spawn event data set handling.
			UpdateEventDataSet->BeginSimulate();
			UpdateEventDataSet->Allocate(OrigNumParticles);
		}
		UpdateEventGeneratorIndex++;
	}

	// Simulate existing particles forward by DeltaSeconds.
	if (OrigNumParticles > 0)
	{
		Data.GetDestinationDataChecked().SetNumInstances(OrigNumParticles);
		SCOPE_CYCLE_COUNTER(STAT_NiagaraSimulate);

		UpdateExecCountBinding.SetValue(OrigNumParticles);
		UpdateExecContext.BindData(0, Data, 0, true);
		int32 EventDataSetIdx = 1;
		for (FNiagaraDataSet* EventDataSet : UpdateScriptEventDataSets)
		{
			check(EventDataSet);
			EventDataSet->GetDestinationDataChecked().SetNumInstances(OrigNumParticles);
			UpdateExecContext.BindData(EventDataSetIdx++, *EventDataSet, 0, true);
		}

		FScriptExecutionConstantBufferTable UpdateConstantBufferTable;
		BuildConstantBufferTable(UpdateExecContext, UpdateConstantBufferTable);

		UpdateExecContext.Execute(OrigNumParticles, UpdateConstantBufferTable);
		int32 DeltaParticles = Data.GetDestinationDataChecked().GetNumInstances() - OrigNumParticles;

		ensure(DeltaParticles <= 0); // We either lose particles or stay the same, we should never add particles in update!

		if (GbDumpParticleData || System->bDumpDebugEmitterInfo)
		{
			Data.GetDestinationDataChecked().Dump(0, OrigNumParticles, FString::Printf(TEXT("=== Updated %d Particles (%d Died) ==="), OrigNumParticles, -DeltaParticles));
			for (int32 EventIdx = 0; EventIdx < UpdateScriptEventDataSets.Num(); ++EventIdx)
			{
				FNiagaraDataSet* EventDataSet = UpdateScriptEventDataSets[EventIdx];
				if (EventDataSet && EventDataSet->GetDestinationDataChecked().GetNumInstances() > 0)
				{
					EventDataSet->GetDestinationDataChecked().Dump(0, INDEX_NONE, FString::Printf(TEXT("Update Script Event %d"), EventIdx));
				}
			}
		//	UE_LOG(LogNiagara, Log, TEXT("=== Update Parameters ===") );
			UpdateExecContext.Parameters.Dump();
		}
	}
	
	uint32 EventSpawnStart = Data.GetDestinationDataChecked().GetNumInstances();
	int32 NumBeforeSpawn = Data.GetDestinationDataChecked().GetNumInstances();
	uint32 TotalActualEventSpawns = 0;

	Data.GetSpawnedIDsTable().SetNum(0, false);

	int32 SpawnCountRemaining = AllocationSize - OrigNumParticles;

	//Init new particles with the spawn script.
	if (SpawnTotal + EventSpawnTotal > 0)
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraSpawn);

		// note that this constant buffer table is used for each invocation of the spawn, the data within the
		// table will get modified between invocations (TotalSpawnedParticles).
		FScriptExecutionConstantBufferTable SpawnConstantBufferTable;
		BuildConstantBufferTable(SpawnExecContext, SpawnConstantBufferTable);

		//Handle main spawn rate spawning
		auto SpawnParticles = [&](int32 Num, const TCHAR* DumpLabel)
		{
			int32 OrigNum = Data.GetDestinationDataChecked().GetNumInstances();
			Data.GetDestinationDataChecked().SetNumInstances(OrigNum + Num);

			// We need to update Engine.Emitter.TotalSpawnedParticles for each event spawn invocation.
			ParentSystemInstance->EditEmitterParameters(EmitterIdx).EmitterTotalSpawnedParticles = TotalSpawnedParticles;
			
			// NOTE(mv): Updates the count after setting the variable, such that the TotalSpawnedParticles value read 
			//           in the script has the count at the start of the frame. 
			//           This way UniqueID = TotalSpawnedParticles + ExecIndex provide unique and sequential identifiers. 
			// NOTE(mv): Only for CPU particles, as GPU particles early outs further up and has a separate increment. 
			TotalSpawnedParticles += Num;

			SpawnExecCountBinding.SetValue(Num);
			SpawnExecContext.BindData(0, Data, OrigNum, true);

			//UE_LOG(LogNiagara, Log, TEXT("SpawnScriptEventDataSets: %d"), SpawnScriptEventDataSets.Num());
			int32 EventDataSetIdx = 1;
			for (FNiagaraDataSet* EventDataSet : SpawnScriptEventDataSets)
			{
				//UE_LOG(LogNiagara, Log, TEXT("SpawnScriptEventDataSets.. %d"), EventDataSet->GetNumVariables());
				int32 EventOrigNum = EventDataSet->GetDestinationDataChecked().GetNumInstances();
				EventDataSet->GetDestinationDataChecked().SetNumInstances(EventOrigNum + Num);
				SpawnExecContext.BindData(EventDataSetIdx++, *EventDataSet, EventOrigNum, true);
			}

			SpawnExecContext.Execute(Num, SpawnConstantBufferTable);

			if (GbDumpParticleData || System->bDumpDebugEmitterInfo)
			{
				Data.GetDestinationDataChecked().Dump(OrigNum, Num, FString::Printf(TEXT("===  %s Spawned %d Particles==="), DumpLabel, Num));
				for (int32 EventIdx = 0; EventIdx < SpawnScriptEventDataSets.Num(); ++EventIdx)
				{
					FNiagaraDataSet* EventDataSet = SpawnScriptEventDataSets[EventIdx];
					if (EventDataSet && EventDataSet->GetDestinationDataChecked().GetNumInstances() > 0)
					{
						EventDataSet->GetDestinationDataChecked().Dump(0, INDEX_NONE, FString::Printf(TEXT("Spawn Script Event %d"), EventIdx));
					}
				}
				//UE_LOG(LogNiagara, Log, TEXT("=== %s Spawn Parameters ==="), *DumpLabel);
				SpawnExecContext.Parameters.Dump();
			}
		};

		//Perform all our regular spawning that's driven by our emitter script.
		for (const FNiagaraSpawnInfo& Info : SpawnInfos)
		{
			const int32 AdjustedSpawnCount = FMath::Min(Info.Count, SpawnCountRemaining);
			if ( AdjustedSpawnCount > 0 )
			{
				auto& EmitterParameters = ParentSystemInstance->EditEmitterParameters(EmitterIdx);
				SpawnIntervalBinding.SetValue(Info.IntervalDt);
				InterpSpawnStartBinding.SetValue(Info.InterpStartDt);
				SpawnGroupBinding.SetValue(Info.SpawnGroup);
				SpawnParticles(AdjustedSpawnCount, TEXT("Regular Spawn"));
			}
			SpawnCountRemaining -= AdjustedSpawnCount;
		}

		EventSpawnStart = Data.GetDestinationDataChecked().GetNumInstances();

		for (int32 EventScriptIdx = 0; EventScriptIdx < CachedEmitter->GetEventHandlers().Num(); EventScriptIdx++)
		{
			FNiagaraEventHandlingInfo& Info = EventHandlingInfo[EventScriptIdx];
			//Spawn particles coming from events.
			for (int32 i = 0; i < Info.SpawnCounts.Num(); i++)
			{
				const int32 EventNumToSpawn = FMath::Min(Info.SpawnCounts[i], SpawnCountRemaining);
				if (EventNumToSpawn > 0)
				{
					const int32 CurrNumParticles = Data.GetDestinationDataChecked().GetNumInstances();

					//Event spawns are instantaneous at the middle of the frame?
					auto& EmitterParameters = ParentSystemInstance->EditEmitterParameters(EmitterIdx);
					SpawnIntervalBinding.SetValue(0.0f);
					InterpSpawnStartBinding.SetValue(DeltaSeconds * 0.5f);
					SpawnGroupBinding.SetValue(0);
					SpawnParticles(EventNumToSpawn, TEXT("Event Spawn"));

					//Update EventSpawnCounts to the number actually spawned.
					const int32 NumActuallySpawned = Data.GetDestinationDataChecked().GetNumInstances() - CurrNumParticles;
					TotalActualEventSpawns += NumActuallySpawned;
					Info.SpawnCounts[i] = NumActuallySpawned;
					SpawnCountRemaining -= NumActuallySpawned;
				}
			}
		}
	}

	int32 NumAfterSpawn = Data.GetCurrentDataChecked().GetNumInstances();
	int32 TotalNumSpawned = NumAfterSpawn - NumBeforeSpawn;

	Data.GetDestinationDataChecked().SetNumSpawnedInstances(TotalNumSpawned);
	Data.GetDestinationDataChecked().SetIDAcquireTag(Data.GetIDAcquireTag());

	//We're done with this simulation pass.
	Data.EndSimulate();
	for (FNiagaraDataSet* SpawnEventDataSet : SpawnScriptEventDataSets)
	{
		if (SpawnEventDataSet && SpawnEventDataSet->GetDestinationData())
		{
			SpawnEventDataSet->EndSimulate();
		}
	}

	for (FNiagaraDataSet* UpdateEventDataSet : UpdateScriptEventDataSets)
	{
		if (UpdateEventDataSet && UpdateEventDataSet->GetDestinationData())
		{
			UpdateEventDataSet->EndSimulate();
		}
	}

	//Now pull out any debug info we need.
#if WITH_EDITORONLY_DATA
	if (ParentSystemInstance->ShouldCaptureThisFrame())
	{
		//Pull out update data.
		TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe> DebugInfo = ParentSystemInstance->GetActiveCaptureWrite(CachedIDName, ENiagaraScriptUsage::ParticleUpdateScript, FGuid());
		if (DebugInfo)
		{
			Data.CopyTo(DebugInfo->Frame, 0, OrigNumParticles);
			DebugInfo->Parameters = UpdateExecContext.Parameters;
			DebugInfo->bWritten = true;
		}
		//Pull out update data.
		DebugInfo = ParentSystemInstance->GetActiveCaptureWrite(CachedIDName, ENiagaraScriptUsage::ParticleSpawnScript, FGuid());
		if (DebugInfo)
		{
			Data.CopyTo(DebugInfo->Frame, NumBeforeSpawn, TotalNumSpawned);
			DebugInfo->Parameters = SpawnExecContext.Parameters;
			DebugInfo->bWritten = true;
		}
	}
#endif
	/*else if (SpawnTotal + EventSpawnTotal > 0)
	{
		UE_LOG(LogNiagara, Log, TEXT("Skipping spawning due to execution state! %d"), (uint32)ExecutionState)
	}*/

	if (TotalActualEventSpawns > 0)
	{
		if (GbDumpParticleData || System->bDumpDebugEmitterInfo)
		{
			Data.Dump(0, INDEX_NONE, TEXT("Existing Data - Pre Event Alloc"));
		}
		//Allocate a new dest buffer to write spawn event handler results into.
		//Can just do one allocate here for all spawn event handlers.
		//Though this requires us to copy the contents of the instances we're not writing to in this pass over from the previous buffer.
		FNiagaraDataBuffer& DestBuffer = Data.BeginSimulate();
		Data.Allocate(Data.GetCurrentDataChecked().GetNumInstances(), true);
		DestBuffer.SetNumInstances(Data.GetCurrentDataChecked().GetNumInstances());

		//if (GbDumpParticleData || System->bDumpDebugEmitterInfo)
		//{
		//	DestBuffer.Dump(0, INDEX_NONE, TEXT("Existing Data - Post Event Alloc, Pre Events"));
		//}
	}

	int32 SpawnEventScriptStartIndex = EventSpawnStart;
	for (int32 EventScriptIdx = 0; EventScriptIdx < CachedEmitter->GetEventHandlers().Num(); EventScriptIdx++)
	{
		const FNiagaraEventScriptProperties &EventHandlerProps = CachedEmitter->GetEventHandlers()[EventScriptIdx];
		FNiagaraEventHandlingInfo& Info = EventHandlingInfo[EventScriptIdx];

		FScriptExecutionConstantBufferTable EventConstantBufferTable;
		BuildConstantBufferTable(EventExecContexts[EventScriptIdx], EventConstantBufferTable);

		if (Info.EventData && Info.SpawnCounts.Num() > 0)
		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraEventHandle);

			for (int32 i = 0; i < Info.SpawnCounts.Num(); i++)
			{
				int32 EventNumToSpawn = Info.SpawnCounts[i];
				if (EventNumToSpawn > 0)
				{
					EventExecCountBindings[EventScriptIdx].SetValue(EventNumToSpawn);

					EventExecContexts[EventScriptIdx].BindData(0, Data, EventSpawnStart, false);
					EventExecContexts[EventScriptIdx].BindData(1, EventHandlingInfo[EventScriptIdx].EventData, i, false);

					EventExecContexts[EventScriptIdx].Execute(EventNumToSpawn, EventConstantBufferTable);

					if (GbDumpParticleData || System->bDumpDebugEmitterInfo)
					{
						EventHandlingInfo[EventScriptIdx].EventData->Dump(i, 1, FString::Printf(TEXT("=== Event Data %d [%d] ==="), EventScriptIdx, i));
						Data.GetDestinationDataChecked().Dump(EventSpawnStart, EventNumToSpawn, FString::Printf(TEXT("=== Event %d %d Particles ==="), EventScriptIdx, EventNumToSpawn));
						//UE_LOG(LogNiagara, Log, TEXT("=== Event %d Parameters ==="), EventScriptIdx);
						EventExecContexts[EventScriptIdx].Parameters.Dump();
					}

#if WITH_EDITORONLY_DATA
					if (ParentSystemInstance->ShouldCaptureThisFrame())
					{
						FGuid EventGuid = EventExecContexts[EventScriptIdx].Script->GetUsageId();
						TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe> DebugInfo = ParentSystemInstance->GetActiveCaptureWrite(CachedIDName, ENiagaraScriptUsage::ParticleEventScript, EventGuid);
						if (DebugInfo)
						{
							Data.CopyTo(DebugInfo->Frame, EventSpawnStart, EventNumToSpawn);
							DebugInfo->Parameters = EventExecContexts[EventScriptIdx].Parameters;
							DebugInfo->bWritten = true;
						}
					}
#endif
					EventSpawnStart += EventNumToSpawn;
				}
			}
		}
	}

	//If we processed any events we need to end simulate to update the current sim state.
	if (Data.GetDestinationData())
	{
		Data.EndSimulate();
	}

	// Update events need a copy per event so that the previous event's data can be used.
	for (int32 EventScriptIdx = 0; EventScriptIdx < CachedEmitter->GetEventHandlers().Num(); EventScriptIdx++)
	{
		const FNiagaraEventScriptProperties &EventHandlerProps = CachedEmitter->GetEventHandlers()[EventScriptIdx];
		FNiagaraDataBuffer* EventData = EventHandlingInfo[EventScriptIdx].EventData;

		FScriptExecutionConstantBufferTable EventConstantBufferTable;
		BuildConstantBufferTable(EventExecContexts[EventScriptIdx], EventConstantBufferTable);

		// handle all-particle events
		if (EventHandlerProps.Script && EventHandlerProps.ExecutionMode == EScriptExecutionMode::EveryParticle && EventData)
		{
			uint32 NumParticles = Data.GetCurrentDataChecked().GetNumInstances();

			if (EventData->GetNumInstances())
			{
				SCOPE_CYCLE_COUNTER(STAT_NiagaraEventHandle);
				
				for (uint32 i = 0; i < EventData->GetNumInstances(); i++)
				{
					Data.BeginSimulate();
					Data.Allocate(NumParticles);

					uint32 NumInstancesPrev = Data.GetCurrentDataChecked().GetNumInstances();
					EventExecCountBindings[EventScriptIdx].SetValue(NumInstancesPrev);

					EventExecContexts[EventScriptIdx].BindData(0, Data, 0, true);
					EventExecContexts[EventScriptIdx].BindData(1, EventData, i, false);

					EventExecContexts[EventScriptIdx].Execute(NumInstancesPrev, EventConstantBufferTable);

					Data.EndSimulate();

					if (GbDumpParticleData || System->bDumpDebugEmitterInfo)
					{
						EventData->Dump(i, 1, FString::Printf(TEXT("=== Event Data %d [%d] ==="), EventScriptIdx, i));
						Data.GetCurrentDataChecked().Dump(0, NumInstancesPrev, FString::Printf(TEXT("=== Event %d %d Particles ==="), EventScriptIdx, NumInstancesPrev));
						EventExecContexts[EventScriptIdx].Parameters.Dump();
					}

#if WITH_EDITORONLY_DATA
					if (ParentSystemInstance->ShouldCaptureThisFrame())
					{
						FGuid EventGuid = EventExecContexts[EventScriptIdx].Script->GetUsageId();
						TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe> DebugInfo = ParentSystemInstance->GetActiveCaptureWrite(CachedIDName, ENiagaraScriptUsage::ParticleEventScript, EventGuid);
						if (DebugInfo)
						{
							Data.CopyTo(DebugInfo->Frame, 0, NumInstancesPrev);
							DebugInfo->Parameters = EventExecContexts[EventScriptIdx].Parameters;
							DebugInfo->bWritten = true;
						}
					}
#endif
					ensure(NumParticles == Data.GetCurrentDataChecked().GetNumInstances());
				}
			}
		}

		//TODO: Disabling this event mode for now until it can be reworked. Currently it uses index directly with can easily be invalid and cause undefined behavior.
		//
//		// handle single-particle events
//		// TODO: we'll need a way to either skip execution of the VM if an index comes back as invalid, or we'll have to pre-process
//		// event/particle arrays; this is currently a very naive (and comparatively slow) implementation, until full indexed reads work
// 		if (EventHandlerProps.Script && EventHandlerProps.ExecutionMode == EScriptExecutionMode::SingleParticle && EventSet[EventScriptIdx])
// 		{
// 
// 			SCOPE_CYCLE_COUNTER(STAT_NiagaraEventHandle);
// 			FNiagaraVariable IndexVar(FNiagaraTypeDefinition::GetIntDef(), "ParticleIndex");
// 			FNiagaraDataSetIterator<int32> IndexItr(*EventSet[EventScriptIdx], IndexVar, 0, false);
// 			if (IndexItr.IsValid() && EventSet[EventScriptIdx]->GetPrevNumInstances() > 0)
// 			{
// 				EventExecCountBindings[EventScriptIdx].SetValue(1);
// 
// 				Data.CopyCurToPrev();
// 				uint32 NumParticles = Data.GetNumInstances();
// 
// 				for (uint32 i = 0; i < EventSet[EventScriptIdx]->GetPrevNumInstances(); i++)
// 				{
// 					int32 Index = *IndexItr;
// 					IndexItr.Advance();
// 					DataSetExecInfos.SetNum(1, false);
// 					DataSetExecInfos[0].StartInstance = Index;
// 					DataSetExecInfos[0].bUpdateInstanceCount = false;
// 					DataSetExecInfos.Emplace(EventSet[EventScriptIdx], i, false, false);
// 					EventExecContexts[EventScriptIdx].Execute(1, DataSetExecInfos);
// 
// 					if (GbDumpParticleData || System->bDumpDebugEmitterInfo)
// 					{
// 						ensure(EventHandlerProps.Script->RapidIterationParameters.VerifyBinding(&EventExecContexts[EventScriptIdx].Parameters));
// 						UE_LOG(LogNiagara, Log, TEXT("=== Event %d Src Parameters ==="), EventScriptIdx);
// 						EventHandlerProps.Script->RapidIterationParameters.Dump();
// 						UE_LOG(LogNiagara, Log, TEXT("=== Event %d Context Parameters ==="), EventScriptIdx);
// 						EventExecContexts[EventScriptIdx].Parameters.Dump();
// 						UE_LOG(LogNiagara, Log, TEXT("=== Event %d Particles (%d index written, %d total) ==="), EventScriptIdx, Index, Data.GetNumInstances());
// 						Data.Dump(true, Index, 1);
// 					}
// 
// 
// #if WITH_EDITORONLY_DATA
// 					if (ParentSystemInstance->ShouldCaptureThisFrame())
// 					{
// 						FGuid EventGuid = EventExecContexts[EventScriptIdx].Script->GetUsageId();
// 						TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe> DebugInfo = ParentSystemInstance->GetActiveCaptureWrite(CachedIDName, ENiagaraScriptUsage::ParticleEventScript, EventGuid);
// 						if (DebugInfo)
// 						{
// 							Data.Dump(DebugInfo->Frame, true, Index, 1);
// 							//DebugInfo->Frame.Dump(true, 0, 1);
// 							DebugInfo->Parameters = EventExecContexts[EventScriptIdx].Parameters;
// 						}
// 					}
// #endif
// 					ensure(NumParticles == Data.GetNumInstances());
// 				}
// 			}
// 		}
	}

	PostTick();

	SpawnExecContext.PostTick();
	UpdateExecContext.PostTick();
	// At this stage GPU execution is being handled by the batcher so we do not need to call PostTick() for it
	for (FNiagaraScriptExecutionContext& EventContext : EventExecContexts)
	{
		EventContext.PostTick();
	}

	if (GbDumpParticleData || System->bDumpDebugEmitterInfo)
	{
		UE_LOG(LogNiagara, Log, TEXT("|=== END OF FNiagaraEmitterInstance::Tick [ %s ] ===============|"), *CachedEmitter->GetPathName());
		UE_LOG(LogNiagara, Log, TEXT("|||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||"), *CachedEmitter->GetPathName());
	}


	INC_DWORD_STAT_BY(STAT_NiagaraNumParticles, Data.GetCurrentDataChecked().GetNumInstances());
}

/** Calculate total number of spawned particles from events; these all come from event handler script with the SpawnedParticles execution mode
 *  We get the counts ahead of event processing time so we only have to allocate new particles once
 *  TODO: augment for multiple spawning event scripts
 */
uint32 FNiagaraEmitterInstance::CalculateEventSpawnCount(const FNiagaraEventScriptProperties &EventHandlerProps, TArray<int32, TInlineAllocator<16>>& EventSpawnCounts, FNiagaraDataSet *EventSet)
{
	uint32 SpawnTotal = 0;
	uint32 MaxSpawnCount = INT_MAX; //We could probably do to have a CVar for limiting the max event spawn directly but for now just keep the count from overflowing so it's caught by the overall partcle count checks later.
	int32 NumEventsToProcess = 0;

	if (EventSet)
	{
		NumEventsToProcess = EventSet->GetCurrentDataChecked().GetNumInstances();
		if(EventHandlerProps.MaxEventsPerFrame > 0)
		{
			NumEventsToProcess = FMath::Min<int32>(EventSet->GetCurrentDataChecked().GetNumInstances(), EventHandlerProps.MaxEventsPerFrame);
		}

		const bool bUseRandom = EventHandlerProps.bRandomSpawnNumber && EventHandlerProps.MinSpawnNumber < EventHandlerProps.SpawnNumber;
		for (int32 i = 0; i < NumEventsToProcess; i++)
		{
			const uint32 SpawnNumber = bUseRandom ? FMath::RandRange((int32)EventHandlerProps.MinSpawnNumber, (int32)EventHandlerProps.SpawnNumber) : EventHandlerProps.SpawnNumber;
			uint32 NewSpawnTotal = SpawnTotal + SpawnNumber;
			if (ExecutionState == ENiagaraExecutionState::Active && SpawnNumber > 0 && NewSpawnTotal < MaxSpawnCount)
			{
				EventSpawnCounts.Add(SpawnNumber);
				SpawnTotal = NewSpawnTotal;
			}
		}
	}

	return SpawnTotal;
}

void FNiagaraEmitterInstance::SetExecutionState(ENiagaraExecutionState InState)
{
	/*if (InState != ExecutionState)
	{
		const UEnum* EnumPtr = FNiagaraTypeDefinition::GetExecutionStateEnum();
		UE_LOG(LogNiagara, Log, TEXT("Emitter \"%s\" change state: %s to %s"), *GetEmitterHandle().GetName().ToString(), *EnumPtr->GetNameStringByValue((int64)ExecutionState),
			*EnumPtr->GetNameStringByValue((int64)InState));
	}*/

	/*if (InState == ENiagaraExecutionState::Active && ExecutionState == ENiagaraExecutionState::Inactive)
	{
		UE_LOG(LogNiagara, Log, TEXT("Emitter \"%s\" change state N O O O O O "), *GetEmitterHandle().GetName().ToString());
	}*/
	if (ensureMsgf(InState >= ENiagaraExecutionState::Active && InState < ENiagaraExecutionState::Num, 
					TEXT("Setting invalid emitter execution state! %d\nEmitter=%s\nComponent=%s"),
					(int32)InState,
					*CachedEmitter->GetFullName(),
					ParentSystemInstance && ParentSystemInstance->GetComponent() ? *ParentSystemInstance->GetComponent()->GetFullName() : TEXT("nullptr"))
		)
	{
		//We can't move out of disabled without a proper reinit.
		if (ExecutionState != ENiagaraExecutionState::Disabled)
		{
			ExecutionState = InState;
		}
	}
	else
	{
		//Try to gracefully fail in this case.
		ExecutionState = ENiagaraExecutionState::Inactive;
	}

}

bool FNiagaraEmitterInstance::FindBinding(const FNiagaraUserParameterBinding& InBinding, TArray<UMaterialInterface*>& OutMaterials) const
{
	FNiagaraSystemInstance* SystemInstance = GetParentSystemInstance();
	if (SystemInstance)
	{
		UNiagaraComponent* Component = SystemInstance->GetComponent();
		if (Component)
		{
			UObject* Obj = Component->GetOverrideParameters().GetUObject(InBinding.Parameter);
			if (Obj)
			{
				UMaterialInterface* Material = Cast<UMaterialInterface>(Obj);
				if (Material)
				{
					OutMaterials.Add(Material);
					return true;
				}
			}
		}
	}
	return false;
}

void FNiagaraEmitterInstance::BuildConstantBufferTable(
	const FNiagaraScriptExecutionContext& ExecContext,
	FScriptExecutionConstantBufferTable& ConstantBufferTable) const
{
	const auto& ExternalParameterData = ExecContext.Parameters.GetParameterDataArray();
	uint8* ExternalParameterBuffer = const_cast<uint8*>(ExternalParameterData.GetData());

	const uint32 ExternalParameterSize = ExecContext.Parameters.GetExternalParameterSize();
	const uint32 LiteralConstantOffset = (ExecContext.HasInterpolationParameters ? 2 : 1) * ExternalParameterSize;
	const uint32 LiteralConstantSize = ExternalParameterData.Num() - LiteralConstantOffset;

	const uint32 TableCount = 5 * (ExecContext.HasInterpolationParameters ? 2 : 1) + 1;
	ConstantBufferTable.Reset(TableCount);

	ConstantBufferTable.AddTypedBuffer(ParentSystemInstance->GetGlobalParameters());
	ConstantBufferTable.AddTypedBuffer(ParentSystemInstance->GetSystemParameters());
	ConstantBufferTable.AddTypedBuffer(ParentSystemInstance->GetOwnerParameters());
	ConstantBufferTable.AddTypedBuffer(ParentSystemInstance->GetEmitterParameters(EmitterIdx));
	ConstantBufferTable.AddRawBuffer(ExternalParameterBuffer, ExternalParameterSize);

	if (ExecContext.HasInterpolationParameters)
	{
		ConstantBufferTable.AddTypedBuffer(ParentSystemInstance->GetGlobalParameters(true));
		ConstantBufferTable.AddTypedBuffer(ParentSystemInstance->GetSystemParameters(true));
		ConstantBufferTable.AddTypedBuffer(ParentSystemInstance->GetOwnerParameters(true));
		ConstantBufferTable.AddTypedBuffer(ParentSystemInstance->GetEmitterParameters(EmitterIdx, true));
		ConstantBufferTable.AddRawBuffer(ExternalParameterBuffer + ExternalParameterSize, ExternalParameterSize);
	}

	ConstantBufferTable.AddRawBuffer(ExternalParameterBuffer + LiteralConstantOffset, LiteralConstantSize);
}
