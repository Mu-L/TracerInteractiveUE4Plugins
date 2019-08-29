// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraScriptExecutionContext.h"
#include "RHI.h"
#include "RHIGPUReadback.h"
#include "NiagaraStats.h"
#include "NiagaraShader.h"
#include "ShaderParameterUtils.h"
#include "SceneUtils.h"
#include "ShaderParameterUtils.h"
#include "ClearQuad.h"

DECLARE_CYCLE_STAT(TEXT("GPU Emitter Dispatch [RT]"), STAT_NiagaraGPUSimTick_RT, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("GPU Data Readback [RT]"), STAT_NiagaraGPUReadback_RT, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Allocate GPU Readback Data [RT]"), STAT_NiagaraAllocateGPUReadback_RT, STATGROUP_Niagara);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Niagara GPU Sim"), STAT_GPU_NiagaraSim, STATGROUP_GPU);
DECLARE_DWORD_COUNTER_STAT(TEXT("# GPU Particles"), STAT_NiagaraGPUParticles, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("Readback latency (frames)"), STAT_NiagaraReadbackLatency, STATGROUP_Niagara);

DECLARE_GPU_STAT_NAMED(NiagaraGPU, TEXT("Niagara"));
DECLARE_GPU_STAT_NAMED(NiagaraGPUSimulation, TEXT("Niagara GPU Simulation"));
DECLARE_GPU_STAT_NAMED(NiagaraIndexBufferClear, TEXT("Niagara index buffer clear"));

uint32 FNiagaraComputeExecutionContext::TickCounter = 0;

const FName NiagaraEmitterInstanceBatcher::Name(TEXT("NiagaraEmitterInstanceBatcher"));

FFXSystemInterface* NiagaraEmitterInstanceBatcher::GetInterface(const FName& InName)
{
	return InName == Name ? this : nullptr;
}

void NiagaraEmitterInstanceBatcher::Queue(FNiagaraComputeExecutionContext *ExecContext)
{
	if (!IsPendingKill())
	{
		//UE_LOG(LogNiagara, Warning, TEXT("Submitted!"));
		//SimulationQueue[CurQueueIndex]->Add(InContext);
		TArray<FNiagaraComputeExecutionContext*>* Queue = &SimulationQueue[0];
		uint32 QueueIndex = CurQueueIndex;
		ENQUEUE_RENDER_COMMAND(QueueNiagaraDispatch)(
			[Queue, QueueIndex, ExecContext](FRHICommandListImmediate& RHICmdList)
		{
			const uint32 QueueIndexMask = (1 << QueueIndex);
			//Don't queue the same context for execution multiple times. TODO: possibly try to combine/accumulate the tick info if we happen to have > 1 before it's executed.
			if (!(ExecContext->PendingExecutionQueueMask & QueueIndexMask))
			{
				Queue[QueueIndex].Add(ExecContext);
				ExecContext->PendingExecutionQueueMask |= QueueIndexMask;
			}
		});
	}
}

void NiagaraEmitterInstanceBatcher::Remove(FNiagaraComputeExecutionContext *ExecContext)
{
	if (!IsPendingKill())
	{
		TArray<FNiagaraComputeExecutionContext*>* Queue = &SimulationQueue[0];
		ENQUEUE_RENDER_COMMAND(RemoveNiagaraDispatch)(
			[Queue, ExecContext](FRHICommandListImmediate& RHICmdList)
		{
			for (int32 i = 0; i < SIMULATION_QUEUE_COUNT; i++)
			{
				Queue[i].Remove(ExecContext);
			}
			ExecContext->PendingExecutionQueueMask = 0;
		});
	}
}

void NiagaraEmitterInstanceBatcher::ExecuteAll(FRHICommandList &RHICmdList, FUniformBufferRHIParamRef ViewUniformBuffer)
{
	SCOPED_DRAW_EVENT(RHICmdList, NiagaraEmitterInstanceBatcher_ExecuteAll);
	SCOPED_GPU_STAT(RHICmdList, NiagaraGPU);

	const uint32 TickedQueueIndex = (CurQueueIndex ^ 0x1);
	const uint32 TickedQueueIndexMask = (1 << TickedQueueIndex);

	TArray<FNiagaraComputeExecutionContext*> &WorkQueue = SimulationQueue[TickedQueueIndex];
	for (FNiagaraComputeExecutionContext *Context : WorkQueue)
	{
		if (Context)
		{
			// need to call RenderThreadInit on data interfaces
			//ExecuteSingle(Context, RHICmdList);
			TickSingle(Context, RHICmdList, ViewUniformBuffer);
			Context->PendingExecutionQueueMask &= ~TickedQueueIndexMask;
		}

	}
	WorkQueue.Empty();
}

void NiagaraEmitterInstanceBatcher::TickSingle(FNiagaraComputeExecutionContext *Context, FRHICommandList &RHICmdList, FUniformBufferRHIParamRef ViewUniformBuffer) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraGPUSimTick_RT);

	check(IsInRenderingThread());
	Context->MainDataSet->Tick();

	FNiagaraComputeExecutionContext::TickCounter++;

	FNiagaraShader* ComputeShader = Context->RTGPUScript->GetShader();
	if (!ComputeShader)
	{
		return;
	}

#if WITH_EDITORONLY_DATA
	if (Context->DebugInfo.IsValid())
	{
		ProcessDebugInfo(RHICmdList, Context);
	}
#endif // WITH_EDITORONLY_DATA

	uint32 PrevNumInstances = Context->MainDataSet->PrevData().GetNumInstances();
	uint32 NewNumInstances = Context->SpawnRateInstances + Context->EventSpawnTotal + PrevNumInstances;
	Context->EventSpawnTotal = GetEventSpawnTotal(Context);

	ResizeCurrentBuffer(RHICmdList, Context, NewNumInstances, PrevNumInstances);

	// set up a data set index buffer, if we don't have one yet
	//
	if (!Context->MainDataSet->HasDatasetIndices())
	{
		Context->MainDataSet->SetupCurDatasetIndices();
	}

	// clear data set index buffer for the simulation shader to write number of written instances to
	//
	ClearIndexBufferCur(RHICmdList, Context);

	// run shader, sim and spawn in a single dispatch
	uint32 UpdateStartInstance = 0;
	Run(Context, UpdateStartInstance, NewNumInstances, ComputeShader, RHICmdList, ViewUniformBuffer);

	// assume all instances survived; ResolveDataSetWrites will change this if the deferred readback was successful; that data may be several frames old
	Context->MainDataSet->CurrData().SetNumInstances(NewNumInstances);

	// ResolveDatasetWrites may read this, so we must transition it here.
	RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, Context->MainDataSet->GetCurDataSetIndices().UAV);		// transition to readable; we'll be using this next frame

	// Don't resolve if the data if there are no instances (prevents a transition issue warning).
	if (NewNumInstances > 0)
	{
		// resolve data set writes - grabs the number of instances written from the index set during the simulation run
		ResolveDatasetWrites(RHICmdList, Context);
	}

	/*
	// TODO: hack - only updating event set 0 on update scripts now; need to match them to their indices and update them all
	if (Context->UpdateEventWriteDataSets.Num())
	{ 
		Context->UpdateEventWriteDataSets[0]->CurrDataRender().SetNumInstances(NumInstancesAfterSim[1]);
	}
	RunEventHandlers(Context, NumInstancesAfterSim[0], NumInstancesAfterSpawn, NumInstancesAfterNonEventSpawn, RHICmdList);
	*/

	// the VF grabs PrevDataRender for drawing, so need to transition
	RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, Context->MainDataSet->PrevData().GetGPUBufferFloat()->UAV);
	RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, Context->MainDataSet->PrevData().GetGPUBufferInt()->UAV);

	check(Context->MainDataSet->HasDatasetIndices());
}


/* Calculate total number of spawned instances from all spawn events
*/
uint32 NiagaraEmitterInstanceBatcher::GetEventSpawnTotal(const FNiagaraComputeExecutionContext *InContext) const
{
	uint32 EventSpawnTotal = 0;
	for (int32 i = 0; i < InContext->GetEventHandlers().Num(); i++)
	{
		const FNiagaraEventScriptProperties &EventHandlerProps = InContext->GetEventHandlers()[i];
		if (EventHandlerProps.ExecutionMode == EScriptExecutionMode::SpawnedParticles && InContext->EventSets[i])
		{
			uint32 NumEventsToProcess = InContext->EventSets[i]->PrevData().GetNumInstances();
			uint32 EventSpawnNum = NumEventsToProcess * EventHandlerProps.SpawnNumber;
			EventSpawnTotal += EventSpawnNum;
		}
	}
	return EventSpawnTotal;
}


/* Clear the data set index buffer; needs to be called before a sim run
 */
void NiagaraEmitterInstanceBatcher::ClearIndexBufferCur(FRHICommandList &RHICmdList, FNiagaraComputeExecutionContext *Context) const
{
	FRWBuffer &DatasetIndexBufferWrite = Context->MainDataSet->GetCurDataSetIndices();
	SCOPED_DRAW_EVENTF(RHICmdList, NiagaraIndexBufferClear, TEXT("Niagara index buffer clear"));
	SCOPED_GPU_STAT(RHICmdList, NiagaraIndexBufferClear);

	ClearUAV(RHICmdList, DatasetIndexBufferWrite, 0);
	RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, DatasetIndexBufferWrite.UAV);
}

/* Attempt to read back simulation results (number of live instances) from the GPU via an async readback request; 
 *	If the readback isn't ready to be performed, we accumulate spawn rates and assume all instances have survived, until
 *	the GPU can tell us how many are actually alive; since that data may be several frames old, we'll always end up
 *	overallocating a bit, and the CPU might think we have more particles alive than we actually do;
 *	since we use DrawIndirect with the GPU determining draw call parameters, that's not an issue
 */
void NiagaraEmitterInstanceBatcher::ResolveDatasetWrites(FRHICommandList &RHICmdList, FNiagaraComputeExecutionContext *Context) const
{
	FRWBuffer &DatasetIndexBufferWrite = Context->MainDataSet->GetCurDataSetIndices();
	uint32 SpawnedThisFrame = Context->SpawnRateInstances + Context->EventSpawnTotal;
	Context->AccumulatedSpawnRate += SpawnedThisFrame;
	if (!Context->GPUDataReadback)
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraAllocateGPUReadback_RT);

		Context->GPUDataReadback = new FRHIGPUMemoryReadback(TEXT("Niagara GPU Emitter Readback"));
		INC_DWORD_STAT(STAT_NiagaraReadbackLatency);
		Context->GPUDataReadback->EnqueueCopy(RHICmdList, DatasetIndexBufferWrite.Buffer);
	}
	else if (Context->GPUDataReadback->IsReady())
	{
		bool bSuccessfullyRead = false;
		{
		    SCOPE_CYCLE_COUNTER(STAT_NiagaraGPUReadback_RT);
		    int32 *NumInstancesAfterSim = static_cast<int32*>(Context->GPUDataReadback->Lock(64 * sizeof(int32)));
			if (NumInstancesAfterSim)
			{
				int32 ExistingDataCount = Context->MainDataSet->CurrData().GetNumInstances();// index 1 is always the count
				int32 NewExistingDataCount = NumInstancesAfterSim[1] + Context->AccumulatedSpawnRate;
				Context->MainDataSet->CurrData().SetNumInstances(NewExistingDataCount);
				FString PathName = Context->GPUScript->GetOutermost()->GetPathName();
				//UE_LOG(LogNiagara, Log, TEXT("GPU Syncup %s : Was(%d) Now(%d)"), *PathName, ExistingDataCount, NewExistingDataCount );
				SET_DWORD_STAT(STAT_NiagaraGPUParticles, NewExistingDataCount);
				SET_DWORD_STAT(STAT_NiagaraReadbackLatency, 0);

				Context->AccumulatedSpawnRate = 0;
				bSuccessfullyRead = true;
			}
			else
			{
				UE_LOG(LogNiagara, Warning, TEXT("GPUDataReadback said it was ready, but returned an invalid buffer. Skipping this time.."));
			}
			Context->GPUDataReadback->Unlock();
		}
		if (bSuccessfullyRead)
		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraAllocateGPUReadback_RT);
			// The following code seems to take significant time on d3d12
			Context->GPUDataReadback->EnqueueCopy(RHICmdList, DatasetIndexBufferWrite.Buffer);
		}
	}
}

void NiagaraEmitterInstanceBatcher::ProcessDebugInfo(FRHICommandList &RHICmdList, const FNiagaraComputeExecutionContext *Context) const
{
#if WITH_EDITORONLY_DATA
	// This method may be called from one of two places: in the tick or as part of a paused frame looking for the debug info that was submitted previously...
	// Note that PrevData is where we expect the data to be for rendering, as per NiagaraEmitterInstanceBatcher::TickSingle
	if (Context->DebugInfo.IsValid())
	{
		
		// Fire off the readback if not already doing so
		if (!Context->GPUDebugDataReadbackFloat && !Context->GPUDebugDataReadbackInt && !Context->GPUDebugDataReadbackCounts)
		{
			// Do nothing.., handled in Run
		}
		// We may not have floats or ints, but we should have at least one of the two
		else if ((Context->GPUDebugDataReadbackFloat == nullptr || Context->GPUDebugDataReadbackFloat->IsReady()) 
				&& (Context->GPUDebugDataReadbackInt == nullptr || Context->GPUDebugDataReadbackInt->IsReady())
				&& Context->GPUDebugDataReadbackCounts->IsReady()
			)
		{
			//UE_LOG(LogNiagara, Warning, TEXT("Read back!"));

			int32 *NumInstancesAfterSim = static_cast<int32*>(Context->GPUDebugDataReadbackCounts->Lock(64 * sizeof(int32)));
			int32 NewExistingDataCount = NumInstancesAfterSim[1];
			{
				float* FloatDataBuffer = nullptr;
				if (Context->GPUDebugDataReadbackFloat)
				{
					FloatDataBuffer = static_cast<float*>(Context->GPUDebugDataReadbackFloat->Lock(Context->GPUDebugDataFloatSize));
				}
				int* IntDataBuffer = nullptr;
				if (Context->GPUDebugDataReadbackInt)
				{
					IntDataBuffer = static_cast<int*>(Context->GPUDebugDataReadbackInt->Lock(Context->GPUDebugDataIntSize));
				}
				Context->MainDataSet->DumpGPU(Context->DebugInfo->Frame, FloatDataBuffer, IntDataBuffer, 0, NewExistingDataCount);
				Context->DebugInfo->bWritten = true;

				if (Context->GPUDebugDataReadbackFloat)
				{
					Context->GPUDebugDataReadbackFloat->Unlock();
				}
				if (Context->GPUDebugDataReadbackInt)
				{
					Context->GPUDebugDataReadbackInt->Unlock();
				}
				Context->GPUDebugDataReadbackCounts->Unlock();
			}
			{
				// The following code seems to take significant time on d3d12
				// Clear out the readback buffers...
				if (Context->GPUDebugDataReadbackFloat)
				{
					delete Context->GPUDebugDataReadbackFloat;
					Context->GPUDebugDataReadbackFloat = nullptr;
				}
				if (Context->GPUDebugDataReadbackInt)
				{
					delete Context->GPUDebugDataReadbackInt;
					Context->GPUDebugDataReadbackInt = nullptr;
				}
				delete Context->GPUDebugDataReadbackCounts;
				Context->GPUDebugDataReadbackCounts = nullptr;	
				Context->GPUDebugDataFloatSize = 0;
				Context->GPUDebugDataIntSize = 0;
			}

			// We've updated the debug info directly, now we need to no longer keep asking and querying because this frame is done!
			Context->DebugInfo.Reset();
		}
	}
#endif // WITH_EDITORONLY_DATA
}



/* Resize data set buffers and set number of instances 
 *	Allocates one additional instance at the end, which is a scratch instance; by setting the default index from AcquireIndex in the shader
 *	to that scratch index, we can avoid branching in every single OutputData function
 */
void NiagaraEmitterInstanceBatcher::ResizeCurrentBuffer(FRHICommandList &RHICmdList, FNiagaraComputeExecutionContext *Context, uint32 NewNumInstances, uint32 PrevNumInstances) const
{

	// allocate for additional instances spawned and set the new number in the data set, if the new number is greater (meaning if we're spawning in this run)
	// TODO: interpolated spawning
	//
	if (NewNumInstances > PrevNumInstances)
	{
		//UE_LOG(LogNiagara, Warning, TEXT("Resize up!"));
		Context->MainDataSet->CurrData().AllocateGPU(NewNumInstances + 1, RHICmdList);
		Context->MainDataSet->CurrData().SetNumInstances(NewNumInstances);
	}
	// if we're not spawning, we need to make sure that the current buffer alloc size and number of instances matches the last one
	// we may have spawned in the last tick, so the buffers may be different sizes
	//
	else if (Context->MainDataSet->CurrData().GetNumInstances() < Context->MainDataSet->PrevData().GetNumInstances())
	{
		//UE_LOG(LogNiagara, Warning, TEXT("Resize down!"));
		Context->MainDataSet->CurrData().AllocateGPU(PrevNumInstances + 1, RHICmdList);
		Context->MainDataSet->CurrData().SetNumInstances(PrevNumInstances);
	}
}


/* Set shader parameters for data interfaces
 */
void NiagaraEmitterInstanceBatcher::SetDataInterfaceParameters(const TArray<UNiagaraDataInterface*> &DataInterfaces, FNiagaraShader* Shader, FRHICommandList &RHICmdList, const FNiagaraComputeExecutionContext *Context) const
{
	// set up data interface buffers, as defined by the DIs during compilation
	//
	uint32 InterfaceIndex = 0;
	for (UNiagaraDataInterface* Interface : DataInterfaces)
	{
		FNiagaraDataInterfaceParamRef& DIParam = Shader->GetDIParameters()[InterfaceIndex];
		if (DIParam.Parameters)
		{
			void* PerInstanceData = nullptr;
			int32* OffsetFound = nullptr;
			if (Context->PerInstanceDataSize != 0 && Context->PerInstanceDataInterfaceOffsets != nullptr)
			{
				OffsetFound = Context->PerInstanceDataInterfaceOffsets->Find(Interface);
				if (OffsetFound != nullptr)
				{
					PerInstanceData = (*OffsetFound) + (uint8*)Context->PerInstanceData;
				}
			}
			DIParam.Parameters->Set(RHICmdList, Shader, Interface, PerInstanceData);
		}

		InterfaceIndex++;
	}
}

void NiagaraEmitterInstanceBatcher::UnsetDataInterfaceParameters(const TArray<UNiagaraDataInterface*> &DataInterfaces, FNiagaraShader* Shader, FRHICommandList &RHICmdList, const FNiagaraComputeExecutionContext *Context) const
{
	// set up data interface buffers, as defined by the DIs during compilation
	//
	uint32 InterfaceIndex = 0;
	for (UNiagaraDataInterface* Interface : DataInterfaces)
	{
		FNiagaraDataInterfaceParamRef& DIParam = Shader->GetDIParameters()[InterfaceIndex];
		if (DIParam.Parameters)
		{
			void* PerInstanceData = nullptr;
			int32* OffsetFound = nullptr;
			if (Context->PerInstanceDataSize != 0 && Context->PerInstanceDataInterfaceOffsets != nullptr)
			{
				OffsetFound = Context->PerInstanceDataInterfaceOffsets->Find(Interface);
				if (OffsetFound != nullptr)
				{
					PerInstanceData = (*OffsetFound) + (uint8*)Context->PerInstanceData;
				}
			}
			DIParam.Parameters->Unset(RHICmdList, Shader, Interface, PerInstanceData);
		}

		InterfaceIndex++;
	}
}


/* Kick off a simulation/spawn run
 */
void NiagaraEmitterInstanceBatcher::Run(const FNiagaraComputeExecutionContext *Context, uint32 UpdateStartInstance, const uint32 TotalNumInstances, FNiagaraShader* Shader,
	FRHICommandList &RHICmdList, FUniformBufferRHIParamRef ViewUniformBuffer, bool bCopyBeforeStart) const
{
	if (TotalNumInstances == 0)
	{
		SCOPED_DRAW_EVENTF(RHICmdList, NiagaraGPUSimulationCS, TEXT("Niagara Gpu Sim - %s - NumInstances: %u"),
			*Context->DebugSimName,
			TotalNumInstances);
		return;
	}

	//UE_LOG(LogNiagara, Warning, TEXT("Run"));

	FNiagaraDataSet *DataSet = Context->MainDataSet;
	const FNiagaraParameterStore& ParameterStore = Context->CombinedParamStore;
	const FRHIUniformBufferLayout& CBufferLayout = Context->CBufferLayout;
	const TArray<uint8, TAlignedHeapAllocator<16>> &Params = Context->ParamData_RT;
	const FRWBuffer &WriteIndexBuffer = DataSet->GetCurDataSetIndices();
	FRWBuffer &ReadIndexBuffer = DataSet->GetPrevDataSetIndices();

	// if we don't have a previous index buffer, we need to prep one using the maximum number of instances; this should only happen on the first frame
	//		the data set index buffer is really the param buffer for the indirect draw call; it contains the number of live instances at index 1, and the simulation
	//		CS uses this to determine the current number of active instances in the buffer
	//
	if (ReadIndexBuffer.Buffer == nullptr)
	{
		TResourceArray<int32> InitIndexBuffer;
		InitIndexBuffer.AddUninitialized(64);
		InitIndexBuffer[1] = 0;		// number of instances 
		ReadIndexBuffer.Initialize(sizeof(int32), 64, EPixelFormat::PF_R32_UINT, BUF_DrawIndirect | BUF_Static, nullptr, &InitIndexBuffer);
	}

	RHICmdList.SetComputeShader(Shader->GetComputeShader());

	RHICmdList.SetShaderResourceViewParameter(Shader->GetComputeShader(), Shader->InputIndexBufferParam.GetBaseIndex(), ReadIndexBuffer.SRV);

	// set the view uniform buffer param
	if (Shader->ViewUniformBufferParam.IsBound() && ViewUniformBuffer)
	{
		RHICmdList.SetShaderUniformBuffer(Shader->GetComputeShader(), Shader->ViewUniformBufferParam.GetBaseIndex(), ViewUniformBuffer);
	}

	SetDataInterfaceParameters(ParameterStore.GetDataInterfaces(), Shader, RHICmdList, Context);

	// set the shader and data set params 
	//
	uint32 WriteBufferIdx = 0;
	uint32 ReadBufferIdx = 0;
	DataSet->SetShaderParams(Shader, RHICmdList, WriteBufferIdx, ReadBufferIdx);
	

	// set the index buffer uav
	//
	if (Shader->OutputIndexBufferParam.IsBound())
	{
		RHICmdList.SetUAVParameter(Shader->GetComputeShader(), Shader->OutputIndexBufferParam.GetUAVIndex(), WriteIndexBuffer.UAV);
	}

	// set the execution parameters
	//
	if (Shader->EmitterTickCounterParam.IsBound())
	{
		RHICmdList.SetShaderParameter(Shader->GetComputeShader(), Shader->EmitterTickCounterParam.GetBufferIndex(), Shader->EmitterTickCounterParam.GetBaseIndex(), Shader->EmitterTickCounterParam.GetNumBytes(), &FNiagaraComputeExecutionContext::TickCounter);
	}

	uint32 Copy = bCopyBeforeStart ? 1 : 0;

	RHICmdList.SetShaderParameter(Shader->GetComputeShader(), Shader->UpdateStartInstanceParam.GetBufferIndex(), Shader->UpdateStartInstanceParam.GetBaseIndex(), Shader->UpdateStartInstanceParam.GetNumBytes(), &UpdateStartInstance);					// 0, except for event handler runs
	RHICmdList.SetShaderParameter(Shader->GetComputeShader(), Shader->NumIndicesPerInstanceParam.GetBufferIndex(), Shader->NumIndicesPerInstanceParam.GetBaseIndex(), Shader->NumIndicesPerInstanceParam.GetNumBytes(), &Context->NumIndicesPerInstance);		// set from the renderer in FNiagaraEmitterInstance::Tick
	int32 InstancesToSpawnThisFrame = Context->SpawnRateInstances + Context->EventSpawnTotal;
	RHICmdList.SetShaderParameter(Shader->GetComputeShader(), Shader->NumSpawnedInstancesParam.GetBufferIndex(), Shader->NumSpawnedInstancesParam.GetBaseIndex(), Shader->NumSpawnedInstancesParam.GetNumBytes(), &InstancesToSpawnThisFrame);				// number of instances in the spawn run

	uint32 NumThreadGroups = 1;
	if (TotalNumInstances > NIAGARA_COMPUTE_THREADGROUP_SIZE)
	{
		NumThreadGroups = FMath::Min(NIAGARA_MAX_COMPUTE_THREADGROUPS, FMath::DivideAndRoundUp(TotalNumInstances, NIAGARA_COMPUTE_THREADGROUP_SIZE));
	}

	// setup script parameters
	if (CBufferLayout.ConstantBufferSize)
	{
		check(CBufferLayout.Resources.Num() == 0);
		const uint8* ParamData = Params.GetData();
		FUniformBufferRHIRef CBuffer = RHICreateUniformBuffer(ParamData, CBufferLayout, EUniformBufferUsage::UniformBuffer_SingleDraw);
		RHICmdList.SetShaderUniformBuffer(Shader->GetComputeShader(), Shader->EmitterConstantBufferParam.GetBaseIndex(), CBuffer);
	}

	// Dispatch, if anything needs to be done
	//
	if (TotalNumInstances)
	{
		SCOPED_DRAW_EVENTF(RHICmdList, NiagaraGPUSimulationCS, TEXT("Niagara Gpu Sim - %s - NumInstances: %u"),
			*Context->DebugSimName,
			TotalNumInstances);
		SCOPED_GPU_STAT(RHICmdList, NiagaraGPUSimulation);
		DispatchComputeShader(RHICmdList, Shader, NumThreadGroups, 1, 1);
	}

#if WITH_EDITORONLY_DATA
	// Check to see if we need to queue up a debug dump..
	if (Context->DebugInfo.IsValid())
	{
		//UE_LOG(LogNiagara, Warning, TEXT("Queued up!"));

		if (!Context->GPUDebugDataReadbackFloat && !Context->GPUDebugDataReadbackInt && !Context->GPUDebugDataReadbackCounts && Context->MainDataSet != nullptr)
		{
			FRWBuffer &DatasetIndexBufferWrite = Context->MainDataSet->GetCurDataSetIndices();

			Context->GPUDebugDataCurrBufferIdx = Context->MainDataSet->GetCurrBufferIdx();
			Context->GPUDebugDataFloatSize = 0;
			Context->GPUDebugDataIntSize = 0;

			if (Context->MainDataSet->GetNumFloatComponents() > 0)
			{
				Context->GPUDebugDataReadbackFloat = new FRHIGPUMemoryReadback(TEXT("Niagara GPU Debug Info Float Emitter Readback"));
				Context->GPUDebugDataReadbackFloat->EnqueueCopy(RHICmdList, Context->MainDataSet->GetDataByIndex(WriteBufferIdx).GetGPUBufferFloat()->Buffer);
				Context->GPUDebugDataFloatSize = Context->MainDataSet->GetDataByIndex(WriteBufferIdx).GetGPUBufferFloat()->NumBytes;
			}

			if (Context->MainDataSet->GetNumInt32Components() > 0)
			{
				Context->GPUDebugDataReadbackInt = new FRHIGPUMemoryReadback(TEXT("Niagara GPU Debug Info Int Emitter Readback"));
				Context->GPUDebugDataReadbackInt->EnqueueCopy(RHICmdList, Context->MainDataSet->GetDataByIndex(WriteBufferIdx).GetGPUBufferInt()->Buffer);
				Context->GPUDebugDataIntSize = Context->MainDataSet->GetDataByIndex(WriteBufferIdx).GetGPUBufferInt()->NumBytes;
			}

			Context->GPUDebugDataReadbackCounts = new FRHIGPUMemoryReadback(TEXT("Niagara GPU Emitter Readback"));
			Context->GPUDebugDataReadbackCounts->EnqueueCopy(RHICmdList, DatasetIndexBufferWrite.Buffer);
		}
	}
#endif // WITH_EDITORONLY_DATA

	// Unset UAV parameters and transition resources (TODO: resource transition should be moved to the renderer)
	// 
	UnsetDataInterfaceParameters(ParameterStore.GetDataInterfaces(), Shader, RHICmdList, Context);
	DataSet->UnsetShaderParams(Shader, RHICmdList);
	Shader->OutputIndexBufferParam.UnsetUAV(RHICmdList, Shader->GetComputeShader());
}

/* Kick off event handler runs
 * TODO: compile event handlers into the sim/spawn compute shader, so we can do everything within one dispatch
 */
void NiagaraEmitterInstanceBatcher::RunEventHandlers(const FNiagaraComputeExecutionContext *Context, uint32 NumInstancesAfterSim, uint32 NumInstancesAfterSpawn, uint32 NumInstancesAfterNonEventSpawn, FRHICommandList &RHICmdList) const
{
	// Event handler run
	//
	/*
	for (int32 EventScriptIdx = 0; EventScriptIdx < Context->GetEventHandlers().Num(); EventScriptIdx++)
	{
		const FNiagaraEventScriptProperties &EventHandlerProps = Context->GetEventHandlers()[EventScriptIdx];
		FNiagaraDataSet *EventSet = Context->EventSets[EventScriptIdx];
		if (EventSet)
		{
			int32 NumEvents = EventSet->PrevDataRender().GetNumInstances();

			// handle all-particle events
			if (NumEvents && EventHandlerProps.Script && EventHandlerProps.ExecutionMode == EScriptExecutionMode::EveryParticle && EventSet)
			{
				FNiagaraShader* EventHandlerShader = EventHandlerProps.Script->GetRenderThreadScript()->GetShader();
				if (EventHandlerShader)
				{
					SetupDataInterfaceBuffers(EventHandlerProps.Script->DataInterfaceInfo, EventHandlerShader, RHICmdList);

					RHICmdList.SetShaderParameter(EventHandlerShader->GetComputeShader(), 0, EventHandlerShader->NumParticlesPerEventParam.GetBaseIndex(), sizeof(int32), &NumInstancesAfterSpawn);
					RHICmdList.SetShaderParameter(EventHandlerShader->GetComputeShader(), 0, EventHandlerShader->NumEventsPerParticleParam.GetBaseIndex(), sizeof(int32), &NumEvents);

					// If we have events, Swap buffers, to make sure we don't overwrite previous script results
					Context->MainDataSet->TickRenderThread(ENiagaraSimTarget::GPUComputeSim);
					Context->MainDataSet->CurrDataRender().AllocateGPU(NumInstancesAfterSpawn, RHICmdList);
					Context->MainDataSet->CurrDataRender().SetNumInstances(NumInstancesAfterSpawn);

					SetPrevDataStrideParams(EventSet, EventHandlerShader, RHICmdList);

					// set up event data set buffers
					FShaderResourceParameter &FloatParam = EventHandlerShader->EventFloatSRVParams[0];
					FShaderResourceParameter &IntParam = EventHandlerShader->EventIntSRVParams[0];
					RHICmdList.SetShaderResourceViewParameter(EventHandlerShader->GetComputeShader(), FloatParam.GetBaseIndex(), EventSet->PrevDataRender().GetGPUBufferFloat()->SRV);
					RHICmdList.SetShaderResourceViewParameter(EventHandlerShader->GetComputeShader(), IntParam.GetBaseIndex(), EventSet->PrevDataRender().GetGPUBufferInt()->SRV);

					TArray<uint8, TAlignedHeapAllocator<16>>  BlankParams;
					Run(Context->MainDataSet, 0, NumInstancesAfterNonEventSpawn, EventHandlerShader, BlankParams, RHICmdList, DummyWriteIndexBuffer);
				}
			}

			// handle spawn events
			if (EventHandlerProps.Script && EventHandlerProps.ExecutionMode == EScriptExecutionMode::SpawnedParticles && EventSet)
			{
				uint32 NumEventsToProcess = EventSet->PrevDataRender().GetNumInstances();
				uint32 EventSpawnNum = NumEventsToProcess * EventHandlerProps.SpawnNumber;

				//int32 SpawnNum = Context->EventSpawnCounts[EventScriptIdx];
				FNiagaraShader* EventHandlerShader = EventHandlerProps.Script->GetRenderThreadScript()->GetShader();
				if (NumEvents && EventSpawnNum && EventHandlerShader)
				{
					//SetupDataInterfaceBuffers(EventHandlerProps.Script->DataInterfaceInfo, EventHandlerShader, RHICmdList);
					int32 OneEvent = 1;
					int32 ParticlesPerEvent = EventSpawnNum / NumEvents;
					RHICmdList.SetShaderParameter(EventHandlerShader->GetComputeShader(), 0, EventHandlerShader->NumEventsPerParticleParam.GetBaseIndex(), sizeof(int32), &OneEvent);
					RHICmdList.SetShaderParameter(EventHandlerShader->GetComputeShader(), 0, EventHandlerShader->NumParticlesPerEventParam.GetBaseIndex(), sizeof(int32), &ParticlesPerEvent);

					// If we have events, Swap buffers
					Context->MainDataSet->TickRenderThread(ENiagaraSimTarget::GPUComputeSim);
					Context->MainDataSet->CurrDataRender().AllocateGPU(NumInstancesAfterSpawn, RHICmdList);
					Context->MainDataSet->CurrDataRender().SetNumInstances(NumInstancesAfterSpawn);

					// set up event data set buffers
					FShaderResourceParameter &FloatParam = EventHandlerShader->EventFloatSRVParams[0];
					FShaderResourceParameter &IntParam = EventHandlerShader->EventIntSRVParams[0];
					RHICmdList.SetShaderResourceViewParameter(EventHandlerShader->GetComputeShader(), FloatParam.GetBaseIndex(), EventSet->PrevDataRender().GetGPUBufferFloat()->SRV);
					RHICmdList.SetShaderResourceViewParameter(EventHandlerShader->GetComputeShader(), IntParam.GetBaseIndex(), EventSet->PrevDataRender().GetGPUBufferInt()->SRV);

					SetPrevDataStrideParams(EventSet, EventHandlerShader, RHICmdList);

					// we assume event spawns are at the end of the buffer
					check(NumInstancesAfterNonEventSpawn + EventSpawnNum == Context->MainDataSet->CurrDataRender().GetNumInstances());

					TArray<uint8, TAlignedHeapAllocator<16>>  BlankParams;
					Run(Context->MainDataSet, NumInstancesAfterNonEventSpawn, EventSpawnNum, EventHandlerShader, BlankParams, RHICmdList, DummyWriteIndexBuffer, true);
				}
			}
		}
	}
	*/
}

/* Set read strides for the sim shader
 */
void NiagaraEmitterInstanceBatcher::SetPrevDataStrideParams(const FNiagaraDataSet *Set, FNiagaraShader* Shader, FRHICommandList &RHICmdList) const
{
	int32 FloatStride = Set->PrevData().GetFloatStride() / sizeof(float);
	int32 IntStride = Set->PrevData().GetInt32Stride() / sizeof(int32);
	RHICmdList.SetShaderParameter(Shader->GetComputeShader(), Shader->EventReadFloatStrideParams[0].GetBufferIndex(), Shader->EventReadFloatStrideParams[0].GetBaseIndex(), sizeof(int32), &FloatStride);
	RHICmdList.SetShaderParameter(Shader->GetComputeShader(), Shader->EventReadIntStrideParams[0].GetBufferIndex(), Shader->EventReadIntStrideParams[0].GetBaseIndex(), sizeof(int32), &IntStride);
}


/* Set up UAVs for the event data sets
*/
void NiagaraEmitterInstanceBatcher::SetupEventUAVs(const FNiagaraComputeExecutionContext *Context, uint32 NewNumInstances, FRHICommandList &RHICmdList) const
{
	FNiagaraShader* UpdateShader = Context->RTUpdateScript->GetShader();

	uint32 SetIndex = 0;
	for (FNiagaraDataSet *Set : Context->UpdateEventWriteDataSets)
	{
		if (NewNumInstances)
		{
			Set->CurrData().AllocateGPU(NewNumInstances, RHICmdList);
			Set->CurrData().SetNumInstances(NewNumInstances);
			FRWShaderParameter &FloatParam = UpdateShader->EventFloatUAVParams[SetIndex];
			FRWShaderParameter &IntParam = UpdateShader->EventIntUAVParams[SetIndex];
			if (FloatParam.IsUAVBound())
			{
				RHICmdList.SetUAVParameter(UpdateShader->GetComputeShader(), FloatParam.GetUAVIndex(), Set->CurrData().GetGPUBufferFloat()->UAV);
			}
			if (IntParam.IsUAVBound())
			{
				RHICmdList.SetUAVParameter(UpdateShader->GetComputeShader(), IntParam.GetUAVIndex(), Set->CurrData().GetGPUBufferInt()->UAV);
			}

			uint32 FloatStride = Set->CurrData().GetFloatStride() / sizeof(float);
			uint32 IntStride = Set->CurrData().GetInt32Stride() / sizeof(int32);
			RHICmdList.SetShaderParameter(UpdateShader->GetComputeShader(), UpdateShader->EventWriteFloatStrideParams[SetIndex].GetBufferIndex(), UpdateShader->EventWriteFloatStrideParams[SetIndex].GetBaseIndex(), sizeof(int32), &FloatStride);
			RHICmdList.SetShaderParameter(UpdateShader->GetComputeShader(), UpdateShader->EventWriteIntStrideParams[SetIndex].GetBufferIndex(), UpdateShader->EventWriteIntStrideParams[SetIndex].GetBaseIndex(), sizeof(int32), &IntStride);
		}

		SetIndex++;
	}


}


void NiagaraEmitterInstanceBatcher::UnsetEventUAVs(const FNiagaraComputeExecutionContext *Context, FRHICommandList &RHICmdList) const
{
	FNiagaraShader* UpdateShader = Context->RTUpdateScript->GetShader();

	for (int32 SetIndex = 0; SetIndex < Context->UpdateEventWriteDataSets.Num(); SetIndex++)
	{
		FRWShaderParameter &FloatParam = UpdateShader->EventFloatUAVParams[SetIndex];
		FRWShaderParameter &IntParam = UpdateShader->EventIntUAVParams[SetIndex];
		FloatParam.UnsetUAV(RHICmdList, UpdateShader->GetComputeShader());
		IntParam.UnsetUAV(RHICmdList, UpdateShader->GetComputeShader());
	}
}