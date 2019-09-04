// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/World.h"
#include "NiagaraParameterCollection.h"
#include "UObject/GCObject.h"
#include "NiagaraDataSet.h"
#include "NiagaraScriptExecutionContext.h"

class UWorld;
class UNiagaraParameterCollection;
class UNiagaraParameterCollectionInstance;

//TODO: Pull all the layout information here, in the data set and in parameter stores out into a single layout structure that's shared between all instances of it.
//Currently there's tons of extra data and work done setting these up.
struct FNiagaraParameterStoreToDataSetBinding
{
	//Array of floats offsets
	struct FDataOffsets
	{
		/** Offset of this value in the parameter store. */
		int32 ParameterOffset;
		/** Offset of this value in the data set's components. */
		int32 DataSetComponentOffset;
		FDataOffsets(int32 InParamOffset, int32 InDataSetComponentOffset) : ParameterOffset(InParamOffset), DataSetComponentOffset(InDataSetComponentOffset) {}
	};
	TArray<FDataOffsets> FloatOffsets;
	TArray<FDataOffsets> Int32Offsets;

	void Empty()
	{
		FloatOffsets.Empty();
		Int32Offsets.Empty();
	}

	void Init(FNiagaraDataSet& DataSet, const FNiagaraParameterStore& ParameterStore)
	{
		//For now, until I get time to refactor all the layout info into something more coherent we'll init like this and just have to assume the correct layout sets and stores are used later.
		//Can check it but it'd be v slow.

		for (const FNiagaraVariable& Var : DataSet.GetVariables())
		{
			const FNiagaraVariableLayoutInfo* Layout = DataSet.GetVariableLayout(Var);
			const int32* ParameterOffsetPtr = ParameterStore.FindParameterOffset(Var);
			if (ParameterOffsetPtr && Layout)
			{
				int32 ParameterOffset = *ParameterOffsetPtr;
				for (uint32 CompIdx = 0; CompIdx < Layout->GetNumFloatComponents(); ++CompIdx)
				{
					int32 ParamOffset = ParameterOffset + Layout->LayoutInfo.FloatComponentByteOffsets[CompIdx];
					int32 DataSetOffset = Layout->FloatComponentStart + Layout->LayoutInfo.FloatComponentRegisterOffsets[CompIdx];
					FloatOffsets.Emplace(ParamOffset, DataSetOffset);
				}
				for (uint32 CompIdx = 0; CompIdx < Layout->GetNumInt32Components(); ++CompIdx)
				{
					int32 ParamOffset = ParameterOffset + Layout->LayoutInfo.Int32ComponentByteOffsets[CompIdx];
					int32 DataSetOffset = Layout->Int32ComponentStart + Layout->LayoutInfo.Int32ComponentRegisterOffsets[CompIdx];
					Int32Offsets.Emplace(ParamOffset, DataSetOffset);
				}
			}
		}
	}

	FORCEINLINE_DEBUGGABLE void DataSetToParameterStore(FNiagaraParameterStore& ParameterStore, FNiagaraDataSet& DataSet, int32 DataSetInstanceIndex)
	{
#if NIAGARA_NAN_CHECKING
		DataSet.CheckForNaNs();
#endif

		FNiagaraDataBuffer* CurrBuffer = DataSet.GetCurrentData();

		for (const FDataOffsets& DataOffsets : FloatOffsets)
		{
			float* DataSetPtr = CurrBuffer->GetInstancePtrFloat(DataOffsets.DataSetComponentOffset, DataSetInstanceIndex);
			ParameterStore.SetParameterByOffset(DataOffsets.ParameterOffset, *DataSetPtr);
		}
		for (const FDataOffsets& DataOffsets : Int32Offsets)
		{
			int32* DataSetPtr = CurrBuffer->GetInstancePtrInt32(DataOffsets.DataSetComponentOffset, DataSetInstanceIndex);
			ParameterStore.SetParameterByOffset(DataOffsets.ParameterOffset, *DataSetPtr);
		}

#if NIAGARA_NAN_CHECKING
		ParameterStore.CheckForNaNs();
#endif

		ParameterStore.OnParameterChange();
	}

	FORCEINLINE_DEBUGGABLE void ParameterStoreToDataSet(FNiagaraParameterStore& ParameterStore, FNiagaraDataSet& DataSet, int32 DataSetInstanceIndex)
	{
		FNiagaraDataBuffer& CurrBuffer = DataSet.GetDestinationDataChecked();
		const uint8* ParameterData = ParameterStore.GetParameterDataArray().GetData();

#if NIAGARA_NAN_CHECKING
		ParameterStore.CheckForNaNs();
#endif

		for (const FDataOffsets& DataOffsets : FloatOffsets)
		{
			float* ParamPtr = (float*)(ParameterData + DataOffsets.ParameterOffset);
			float* DataSetPtr = CurrBuffer.GetInstancePtrFloat(DataOffsets.DataSetComponentOffset, DataSetInstanceIndex);
			*DataSetPtr = *ParamPtr;
		}
		for (const FDataOffsets& DataOffsets : Int32Offsets)
		{
			int32* ParamPtr = (int32*)(ParameterData + DataOffsets.ParameterOffset);
			int32* DataSetPtr = CurrBuffer.GetInstancePtrInt32(DataOffsets.DataSetComponentOffset, DataSetInstanceIndex);
			*DataSetPtr = *ParamPtr;
		}

#if NIAGARA_NAN_CHECKING
		DataSet.CheckForNaNs();
#endif
	}
};

/** Simulation performing all system and emitter scripts for a instances of a UNiagaraSystem in a world. */
class FNiagaraSystemSimulation
{
public:
	~FNiagaraSystemSimulation();
	bool Init(UNiagaraSystem* InSystem, UWorld* InWorld, bool bInIsSolo);
	void Destroy();
	bool Tick(float DeltaSeconds);

	void RemoveInstance(FNiagaraSystemInstance* Instance);
	void AddInstance(FNiagaraSystemInstance* Instance);

	void PauseInstance(FNiagaraSystemInstance* Instance);
	void UnpauseInstance(FNiagaraSystemInstance* Instance);

	FORCEINLINE UNiagaraSystem* GetSystem()const { return WeakSystem.Get(); }

	UNiagaraParameterCollectionInstance* GetParameterCollectionInstance(UNiagaraParameterCollection* Collection);

	FNiagaraParameterStore& GetScriptDefinedDataInterfaceParameters();

	/** Transfers a system instance from SourceSimulation. */
	void TransferInstance(FNiagaraSystemSimulation* SourceSimulation, FNiagaraSystemInstance* SystemInst);

	void DumpInstance(const FNiagaraSystemInstance* Inst)const;

	bool GetIsSolo() const { return bIsSolo; }

	FNiagaraScriptExecutionContext& GetSpawnExecutionContext() { return SpawnExecContext; }
	FNiagaraScriptExecutionContext& GetUpdateExecutionContext() { return UpdateExecContext; }

	void TransitionToDeferredDeletionQueue(TUniquePtr< FNiagaraSystemInstance>& InPtr);

protected:

	/** System of instances being simulated.  We use a weak object ptr here because once the last referencing object goes away this system may be come invalid at runtime. */
	TWeakObjectPtr<UNiagaraSystem> WeakSystem;

	/** World this system simulation belongs to. */
	UWorld* World;

	/** Main dataset containing system instance attribute data. */
	FNiagaraDataSet DataSet;
	
	/**
	As there's a 1 to 1 relationship between system instance and their execution in this simulation we must pull all that instances parameters into a dataset for simulation.
	In some cases this might be a big waste of memory as there'll be duplicated data from a parameter store that's shared across all instances.
	Though all these parameters can be unique per instance so for now lets just do the simple thing and improve later.
	*/
	FNiagaraDataSet SpawnInstanceParameterDataSet;
	FNiagaraDataSet UpdateInstanceParameterDataSet;

	FNiagaraScriptExecutionContext SpawnExecContext;
	FNiagaraScriptExecutionContext UpdateExecContext;

	/** Bindings that pull per component parameters into the spawn parameter dataset. */
	FNiagaraParameterStoreToDataSetBinding SpawnInstanceParameterToDataSetBinding;
	/** Bindings that pull per component parameters into the update parameter dataset. */
	FNiagaraParameterStoreToDataSetBinding UpdateInstanceParameterToDataSetBinding;

	/** Binding to push system attributes into each emitter spawn parameters. */
	TArray<FNiagaraParameterStoreToDataSetBinding> DataSetToEmitterSpawnParameters;
	/** Binding to push system attributes into each emitter update parameters. */
	TArray<FNiagaraParameterStoreToDataSetBinding> DataSetToEmitterUpdateParameters;
	/** Binding to push system attributes into each emitter event parameters. */
	TArray<TArray<FNiagaraParameterStoreToDataSetBinding>> DataSetToEmitterEventParameters;

	/** Direct bindings for Engine variables in System Spawn and Update scripts. */
	FNiagaraParameterDirectBinding<float> SpawnTimeParam;
	FNiagaraParameterDirectBinding<float> UpdateTimeParam;

	FNiagaraParameterDirectBinding<float> SpawnDeltaTimeParam;
	FNiagaraParameterDirectBinding<float> UpdateDeltaTimeParam;

	FNiagaraParameterDirectBinding<float> SpawnInvDeltaTimeParam;
	FNiagaraParameterDirectBinding<float> UpdateInvDeltaTimeParam;
	
	FNiagaraParameterDirectBinding<int32> SpawnNumSystemInstancesParam;
	FNiagaraParameterDirectBinding<int32> UpdateNumSystemInstancesParam;

	FNiagaraParameterDirectBinding<float> SpawnGlobalSpawnCountScaleParam;
	FNiagaraParameterDirectBinding<float> UpdateGlobalSpawnCountScaleParam;

	FNiagaraParameterDirectBinding<float> SpawnGlobalSystemCountScaleParam;
	FNiagaraParameterDirectBinding<float> UpdateGlobalSystemCountScaleParam;


	/** System instances that have been spawned and are now simulating. */
	TArray<FNiagaraSystemInstance*> SystemInstances;
	/** System instances that are pending to be spawned. */
	TArray<FNiagaraSystemInstance*> PendingSystemInstances;

	/** System instances that are paused. */
	TArray<FNiagaraSystemInstance*> PausedSystemInstances;
	FNiagaraDataSet PausedInstanceData;

	TArray<TArray<FNiagaraDataSetAccessor<FNiagaraSpawnInfo>>> EmitterSpawnInfoAccessors;

	void InitParameterDataSetBindings(FNiagaraSystemInstance* SystemInst);

	FNiagaraDataSetAccessor<int32> SystemExecutionStateAccessor;
	TArray<FNiagaraDataSetAccessor<int32>> EmitterExecutionStateAccessors;

	uint32 bCanExecute : 1;

	/** A parameter store which contains the data interfaces parameters which were defined by the scripts. */
	FNiagaraParameterStore ScriptDefinedDataInterfaceParameters;

	bool bIsSolo;

	TOptional<float> MaxDeltaTime;

	TArray<TUniquePtr< FNiagaraSystemInstance> > DeferredDeletionQueue;
};
