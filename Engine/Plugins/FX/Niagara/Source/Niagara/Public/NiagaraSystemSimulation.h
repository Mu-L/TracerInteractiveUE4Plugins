// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/World.h"
#include "NiagaraParameterCollection.h"
#include "UObject/GCObject.h"
#include "NiagaraDataSet.h"
#include "NiagaraScriptExecutionContext.h"
#include "NiagaraSystem.h"

class UNiagaraEffectType;
class UWorld;
class UNiagaraParameterCollection;
class UNiagaraParameterCollectionInstance;

//TODO: It would be good to have the batch size be variable per system to try to keep a good work/overhead ratio.
//Can possibly adjust in future based on average batch execution time.
#define NiagaraSystemTickBatchSize 4
typedef TArray<FNiagaraSystemInstance*, TInlineAllocator<NiagaraSystemTickBatchSize>> FNiagaraSystemTickBatch;

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
			int32 NumFloats = 0;
			int32 NumInts = 0;
			if (ParameterOffsetPtr && Layout)
			{
				int32 ParameterOffset = *ParameterOffsetPtr;
				for (uint32 CompIdx = 0; CompIdx < Layout->GetNumFloatComponents(); ++CompIdx)
				{
					int32 ParamOffset = ParameterOffset + Layout->LayoutInfo.FloatComponentByteOffsets[CompIdx];
					int32 DataSetOffset = Layout->FloatComponentStart + NumFloats++;
					FloatOffsets.Emplace(ParamOffset, DataSetOffset);
				}
				for (uint32 CompIdx = 0; CompIdx < Layout->GetNumInt32Components(); ++CompIdx)
				{
					int32 ParamOffset = ParameterOffset + Layout->LayoutInfo.Int32ComponentByteOffsets[CompIdx];
					int32 DataSetOffset = Layout->Int32ComponentStart + NumInts++;
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

	FORCEINLINE_DEBUGGABLE void ParameterStoreToDataSet(const FNiagaraParameterStore& ParameterStore, FNiagaraDataSet& DataSet, int32 DataSetInstanceIndex)
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

#if 1
struct FNiagaraConstantBufferToDataSetBinding
{
	void Init(const FNiagaraSystemCompiledData& CompiledData);

	void CopyToDataSets(const FNiagaraSystemInstance& SystemInstance, FNiagaraDataSet& SpawnDataSet, FNiagaraDataSet& UpdateDataSet, int32 DataSetInstanceIndex) const;
	

protected:
	void ApplyOffsets(const FNiagaraParameterDataSetBindingCollection& Offsets, const uint8* SourceData, FNiagaraDataSet& DataSet, int32 DataSetInstanceIndex) const;

	FNiagaraParameterDataSetBindingCollection SpawnInstanceGlobalBinding;
	FNiagaraParameterDataSetBindingCollection SpawnInstanceSystemBinding;
	FNiagaraParameterDataSetBindingCollection SpawnInstanceOwnerBinding;
	TArray<FNiagaraParameterDataSetBindingCollection> SpawnInstanceEmitterBindings;

	FNiagaraParameterDataSetBindingCollection UpdateInstanceGlobalBinding;
	FNiagaraParameterDataSetBindingCollection UpdateInstanceSystemBinding;
	FNiagaraParameterDataSetBindingCollection UpdateInstanceOwnerBinding;
	TArray<FNiagaraParameterDataSetBindingCollection> UpdateInstanceEmitterBindings;
};
#endif

struct FNiagaraSystemSimulationTickContext
{
	FNiagaraSystemSimulationTickContext(class FNiagaraSystemSimulation* Owner, TArray<FNiagaraSystemInstance*>& Instances, FNiagaraDataSet& DataSet, float DeltaSeconds, int32 SpawnNum, int EffectsQuality, const FGraphEventRef& MyCompletionGraphEvent);

	class FNiagaraSystemSimulation*		Owner;
	UNiagaraSystem*						System;

	TArray<FNiagaraSystemInstance*>&	Instances;
	FNiagaraDataSet&					DataSet;

	float								DeltaSeconds;
	int32								SpawnNum;

	int									EffectsQuality;

	FGraphEventRef						MyCompletionGraphEvent;
	FGraphEventArray*					FinalizeEvents;

	bool								bTickAsync;
	bool								bTickInstancesAsync;
};

/** Simulation performing all system and emitter scripts for a instances of a UNiagaraSystem in a world. */
class FNiagaraSystemSimulation : public TSharedFromThis<FNiagaraSystemSimulation, ESPMode::ThreadSafe>, FGCObject
{
	friend FNiagaraSystemSimulationTickContext;
public:

	//FGCObject Interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector)override;
	//FGCObject Interface END

	FNiagaraSystemSimulation();
	~FNiagaraSystemSimulation();
	bool Init(UNiagaraSystem* InSystem, UWorld* InWorld, bool bInIsSolo, ETickingGroup TickGroup);
	void Destroy();
	bool Tick(float DeltaSeconds);

	bool IsValid()const { return WeakSystem.Get() != nullptr && bCanExecute && World != nullptr; }

	/** First phase of system sim tick. Must run on GameThread. */
	void Tick_GameThread(float DeltaSeconds, const FGraphEventRef& MyCompletionGraphEvent);
	/** Second phase of system sim tick that can run on any thread. */
	void Tick_Concurrent(FNiagaraSystemSimulationTickContext& Context);

	/** Update TickGroups for pending instances and execute tick group promotions. */
	void UpdateTickGroups_GameThread();
	/** Spawn any pending instances, assumes that you have update tick groups ahead of time. */
	void Spawn_GameThread(float DeltaSeconds);

	/** Promote instances that have ticked during */

	/** Wait for system simulation tick to complete.  If bEnsureComplete is true we will trigger an ensure if it is not complete. */
	void WaitForSystemTickComplete(bool bEnsureComplete = false);
	/** Wait for instances tick to complete.  If bEnsureComplete is true we will trigger an ensure if it is not complete. */
	void WaitForInstancesTickComplete(bool bEnsureComplete = false);

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

	/** Dump information about all instances tick */
	void DumpTickInfo(FOutputDevice& Ar);

	bool GetIsSolo() const { return bIsSolo; }

	FNiagaraScriptExecutionContext& GetSpawnExecutionContext() { return SpawnExecContext; }
	FNiagaraScriptExecutionContext& GetUpdateExecutionContext() { return UpdateExecContext; }

	void AddTickGroupPromotion(FNiagaraSystemInstance* Instance);

	const FString& GetCrashReporterTag()const;
protected:
	/** Sets constant parameter values */
	void SetupParameters_GameThread(float DeltaSeconds);

	/** Does any prep work for system simulation such as pulling instance parameters into a dataset. */
	void PrepareForSystemSimulate(FNiagaraSystemSimulationTickContext& Context);
	/** Runs the system spawn script for new system instances. */
	void SpawnSystemInstances(FNiagaraSystemSimulationTickContext& Context);
	/** Runs the system update script. */
	void UpdateSystemInstances(FNiagaraSystemSimulationTickContext& Context);
	/** Transfers the results of the system simulation into the emitter instances. */
	void TransferSystemSimResults(FNiagaraSystemSimulationTickContext& Context);
	/** Builds the constant buffer table for a given script execution */
	void BuildConstantBufferTable(
		const FNiagaraGlobalParameters& GlobalParameters,
		FNiagaraScriptExecutionContext& ExecContext,
		FScriptExecutionConstantBufferTable& ConstantBufferTable) const;

	/** Should we push the system sim tick off the game thread. */
	FORCEINLINE bool ShouldTickAsync(const FNiagaraSystemSimulationTickContext& Context);
	/** Should we push the system instance ticks off the game thread. */
	FORCEINLINE bool ShouldTickInstancesAsync(const FNiagaraSystemSimulationTickContext& Context);

	void AddSystemToTickBatch(FNiagaraSystemInstance* Instance, FNiagaraSystemSimulationTickContext& Context);
	void FlushTickBatch(FNiagaraSystemSimulationTickContext& Context);

	/** System of instances being simulated.  We use a weak object ptr here because once the last referencing object goes away this system may be come invalid at runtime. */
	TWeakObjectPtr<UNiagaraSystem> WeakSystem;

	/** We cache off the effect type in the unlikely even that someone GCs the System from under us so that we can keep the effect types instance count etc accurate. */
	UNiagaraEffectType* EffectType;

	/** Which tick group we are in, only valid when not in Solo mode. */
	ETickingGroup SystemTickGroup = TG_MAX;

	/** World this system simulation belongs to. */
	UWorld* World;

	/** Main dataset containing system instance attribute data. */
	FNiagaraDataSet MainDataSet;
	/** DataSet used if we have to spawn instances outside of their tick. */
	FNiagaraDataSet SpawningDataSet;
	/** DataSet used to store pausing instance data. */
	FNiagaraDataSet PausedInstanceData;

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

	FNiagaraConstantBufferToDataSetBinding ConstantBufferToDataSetBinding;

	/** Binding to push system attributes into each emitter spawn parameters. */
	TArray<FNiagaraParameterStoreToDataSetBinding> DataSetToEmitterSpawnParameters;
	/** Binding to push system attributes into each emitter update parameters. */
	TArray<FNiagaraParameterStoreToDataSetBinding> DataSetToEmitterUpdateParameters;
	/** Binding to push system attributes into each emitter event parameters. */
	TArray<TArray<FNiagaraParameterStoreToDataSetBinding>> DataSetToEmitterEventParameters;
	/** Binding to push system attributes into each emitter gpu parameters. */
	TArray<FNiagaraParameterStoreToDataSetBinding> DataSetToEmitterGPUParameters;


	/** Direct bindings for Engine variables in System Spawn and Update scripts. */
	FNiagaraParameterDirectBinding<int32> SpawnNumSystemInstancesParam;
	FNiagaraParameterDirectBinding<int32> UpdateNumSystemInstancesParam;

	FNiagaraParameterDirectBinding<float> SpawnGlobalSpawnCountScaleParam;
	FNiagaraParameterDirectBinding<float> UpdateGlobalSpawnCountScaleParam;

	FNiagaraParameterDirectBinding<float> SpawnGlobalSystemCountScaleParam;
	FNiagaraParameterDirectBinding<float> UpdateGlobalSystemCountScaleParam;

	/** System instances that have been spawned and are now simulating. */
	TArray<FNiagaraSystemInstance*> SystemInstances;
	/** System instances that are about to be spawned outside of regular ticking. */
	TArray<FNiagaraSystemInstance*> SpawningInstances;
	/** System instances that are paused. */
	TArray<FNiagaraSystemInstance*> PausedSystemInstances;

	/** System instances that are pending to be spawned. */
	TArray<FNiagaraSystemInstance*> PendingSystemInstances;

	/** List of instances that are pending a tick group promotion. */
	TArray<FNiagaraSystemInstance*> PendingTickGroupPromotions;

	TArray<TArray<FNiagaraDataSetAccessor<FNiagaraSpawnInfo>>> EmitterSpawnInfoAccessors;

	void InitParameterDataSetBindings(FNiagaraSystemInstance* SystemInst);

	FNiagaraDataSetAccessor<int32> SystemExecutionStateAccessor;
	TArray<FNiagaraDataSetAccessor<int32>> EmitterExecutionStateAccessors;

	uint32 bCanExecute : 1;
	uint32 bBindingsInitialized : 1;
	uint32 bInSpawnPhase : 1;
	uint32 bIsSolo : 1;

	/** A parameter store which contains the data interfaces parameters which were defined by the scripts. */
	FNiagaraParameterStore ScriptDefinedDataInterfaceParameters;

	TOptional<float> MaxDeltaTime;

	/** Current tick batch we're filling ready for processing, potentially in an async task. */
	FNiagaraSystemTickBatch TickBatch;

	/** Current task that is executing */
	FGraphEventRef SystemTickGraphEvent;

	mutable FString CrashReporterTag;
};
