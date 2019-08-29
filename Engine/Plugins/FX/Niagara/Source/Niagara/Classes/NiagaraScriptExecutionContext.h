// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraEmitterInstance.h: Niagara emitter simulation class
==============================================================================*/
#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "NiagaraCommon.h"
#include "NiagaraDataSet.h"
#include "NiagaraEvents.h"
#include "NiagaraCollision.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraEmitter.h"
#include "NiagaraScriptExecutionParameterStore.h"
#include "NiagaraTypes.h"
#include "RHIGPUReadback.h"



struct FNiagaraDataSetExecutionInfo
{
	FNiagaraDataSetExecutionInfo()
		: DataSet(nullptr)
		, StartInstance(0)
		, bAllocate(false)
		, bUpdateInstanceCount(false)
	{
	}

	FNiagaraDataSetExecutionInfo(FNiagaraDataSet* InDataSet, int32 InStartInstance, bool bInAllocate, bool bInUpdateInstanceCount)
		: DataSet(InDataSet)
		, StartInstance(InStartInstance)
		, bAllocate(bInAllocate)
		, bUpdateInstanceCount(bInUpdateInstanceCount)
	{}

	FNiagaraDataSet* DataSet;
	int32 StartInstance;
	bool bAllocate;
	bool bUpdateInstanceCount;
};

struct FNiagaraScriptExecutionContext
{
	UNiagaraScript* Script;

	/** Table of external function delegates called from the VM. */
	TArray<FVMExternalFunction> FunctionTable;

	/** Table of instance data for data interfaces that require it. */
	TArray<void*> DataInterfaceInstDataTable;

	/** Parameter store. Contains all data interfaces and a parameter buffer that can be used directly by the VM or GPU. */
	FNiagaraScriptExecutionParameterStore Parameters;

	TArray<FDataSetMeta> DataSetMetaTable;

	static uint32 TickCounter;

	FNiagaraScriptExecutionContext();
	~FNiagaraScriptExecutionContext();

	bool Init(UNiagaraScript* InScript, ENiagaraSimTarget InTarget);
	

	bool Tick(class FNiagaraSystemInstance* Instance, ENiagaraSimTarget SimTarget = ENiagaraSimTarget::CPUSim);
	void PostTick();

	bool Execute(uint32 NumInstances, TArray<FNiagaraDataSetExecutionInfo, TInlineAllocator<8>>& DataSetInfos);

	const TArray<UNiagaraDataInterface*>& GetDataInterfaces()const { return Parameters.GetDataInterfaces(); }

	void DirtyDataInterfaces();

	bool CanExecute()const;
};




struct FNiagaraComputeExecutionContext
{
	FNiagaraComputeExecutionContext()
		: MainDataSet(nullptr)
		, SpawnRateInstances(0)
		, EventSpawnTotal(0)
		, SpawnScript(nullptr)
		, UpdateScript(nullptr)
		, GPUScript(nullptr)
		, RTUpdateScript(0)
		, RTSpawnScript(0)
		, RTGPUScript(0)
		, GPUDataReadback(nullptr)
		, AccumulatedSpawnRate(0)
		, NumIndicesPerInstance(0)
		, bPendingExecution(0)
	{
	}

	void Reset()
	{
		AccumulatedSpawnRate = 0;
		bPendingExecution = 0;
		if (GPUDataReadback)
		{
			delete GPUDataReadback;
		}
		GPUDataReadback = nullptr;
	}

	void InitParams(UNiagaraScript* InGPUComputeScript, UNiagaraScript* InSpawnScript, UNiagaraScript *InUpdateScript, ENiagaraSimTarget SimTarget)
	{
		CombinedParamStore.InitFromOwningContext(InGPUComputeScript, SimTarget, true);

		GPUScript = InGPUComputeScript;
		SpawnScript = InSpawnScript;
		UpdateScript = InUpdateScript;

#if DO_CHECK
		FNiagaraShader *Shader = InGPUComputeScript->GetRenderThreadScript()->GetShaderGameThread();
		DIParamInfo.Empty();
		if (Shader)
		{
			for (FNiagaraDataInterfaceParamRef& DIParams : Shader->GetDIParameters())
			{
				DIParamInfo.Add(DIParams.ParameterInfo);
			}
		}
		else
		{
			DIParamInfo = InGPUComputeScript->GetRenderThreadScript()->GetDataInterfaceParamInfo();
		}
#endif
	}

	void DirtyDataInterfaces()
	{
		CombinedParamStore.MarkInterfacesDirty();
	}

	bool Tick(FNiagaraSystemInstance* ParentSystemInstance)
	{
		if (CombinedParamStore.GetInterfacesDirty())
		{
#if DO_CHECK
			const TArray<UNiagaraDataInterface*> &DataInterfaces = CombinedParamStore.GetDataInterfaces();
			// We must make sure that the data interfaces match up between the original script values and our overrides...
			if (DIParamInfo.Num() != DataInterfaces.Num())
			{
				UE_LOG(LogNiagara, Warning, TEXT("Mismatch between Niagara GPU Execution Context data interfaces and those in its script!"));
				return false;
			}

			for (int32 i=0; i<DIParamInfo.Num(); ++i)
			{
				FString UsedClassName = DataInterfaces[i]->GetClass()->GetName();
				if (DIParamInfo[i].DIClassName != UsedClassName)
				{
					UE_LOG(LogNiagara, Warning, TEXT("Mismatched class between Niagara GPU Execution Context data interfaces and those in its script!\nIndex:%d\nShader:%s\nScript:%s")
						, i, *DIParamInfo[i].DIClassName, *UsedClassName);
				}
			}
#endif

			CombinedParamStore.Tick();
		}

		return true;
	}

	const TArray<FNiagaraEventScriptProperties> &GetEventHandlers() const { return EventHandlerScriptProps; }

	class FNiagaraDataSet *MainDataSet;
	TArray<FNiagaraDataSet*>UpdateEventWriteDataSets;
	TArray<FNiagaraEventScriptProperties> EventHandlerScriptProps;
	TArray<FNiagaraDataSet*> EventSets;
	uint32 SpawnRateInstances;

	TArray<int32> EventSpawnCounts;
	uint32 EventSpawnTotal;
	UNiagaraScript* SpawnScript;
	UNiagaraScript* UpdateScript;
	UNiagaraScript* GPUScript;
	class FNiagaraShaderScript*  RTUpdateScript;
	class FNiagaraShaderScript*  RTSpawnScript;
	class FNiagaraShaderScript*  RTGPUScript;
	TArray<uint8, TAlignedHeapAllocator<16>> ParamData_RT;		// RT side copy of the parameter data
	FNiagaraScriptExecutionParameterStore CombinedParamStore;
	static uint32 TickCounter;
#if DO_CHECK
	TArray< FNiagaraDataInterfaceGPUParamInfo >  DIParamInfo;
#endif

	FRHIGPUMemoryReadback *GPUDataReadback;
	uint32 AccumulatedSpawnRate;
	uint32 NumIndicesPerInstance;	// how many vtx indices per instance the renderer is going to have for its draw call

	/** Ensures we only enqueue each context once before they're dispatched. */
	uint32 bPendingExecution : 1;
};
