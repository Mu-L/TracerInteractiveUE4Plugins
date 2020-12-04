// Copyright Epic Games, Inc. All Rights Reserved.


#include "NetworkPredictionTrace.h"
#include "Engine/GameInstance.h"
#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"
#include "UObject/ObjectKey.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Engine/NetConnection.h"
#include "UObject/CoreNet.h"
#include "Engine/PackageMapClient.h"
#include "Logging/LogMacros.h"
#include "NetworkPredictionLog.h"
#include "Trace/Trace.inl"

// Also should update string tracing with Trace::AnsiString

UE_TRACE_CHANNEL_DEFINE(NetworkPredictionChannel)

UE_TRACE_EVENT_BEGIN(NetworkPrediction, SimScope)
	UE_TRACE_EVENT_FIELD(int32, TraceID)
UE_TRACE_EVENT_END()

// Trace a simulation creation. GroupName is attached as attachment.
UE_TRACE_EVENT_BEGIN(NetworkPrediction, SimulationCreated)
	UE_TRACE_EVENT_FIELD(uint32, SimulationID) // server assigned (shared client<->server)
	UE_TRACE_EVENT_FIELD(int32, TraceID) // process unique id
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, SimulationConfig)
	UE_TRACE_EVENT_FIELD(int32, TraceID)
	UE_TRACE_EVENT_FIELD(uint8, NetRole)
	UE_TRACE_EVENT_FIELD(uint8, bHasNetConnection)
	UE_TRACE_EVENT_FIELD(uint8, TickingPolicy)
	UE_TRACE_EVENT_FIELD(uint8, NetworkLOD)
	UE_TRACE_EVENT_FIELD(int32, ServiceMask)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, SimulationScope)
	UE_TRACE_EVENT_FIELD(int32, TraceID)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, PieBegin)
	UE_TRACE_EVENT_FIELD(uint8, DummyData)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, WorldFrameStart)
	UE_TRACE_EVENT_FIELD(uint32, GameInstanceID)
	UE_TRACE_EVENT_FIELD(uint64, EngineFrameNumber)
	UE_TRACE_EVENT_FIELD(float, DeltaSeconds)
UE_TRACE_EVENT_END()

// General system fault. Log message is in attachment
UE_TRACE_EVENT_BEGIN(NetworkPrediction, SystemFault)
UE_TRACE_EVENT_END()

// Traces general tick state (called before ticking N sims)
UE_TRACE_EVENT_BEGIN(NetworkPrediction, Tick)
	UE_TRACE_EVENT_FIELD(int32, StartMS)
	UE_TRACE_EVENT_FIELD(int32, DeltaMS)
	UE_TRACE_EVENT_FIELD(int32, OutputFrame)
	UE_TRACE_EVENT_FIELD(int32, LocalOffsetFrame)
UE_TRACE_EVENT_END()

// Signals that the given sim has done a tick. Expected to be called after the 'Tick' event has been traced
UE_TRACE_EVENT_BEGIN(NetworkPrediction, SimTick)
	UE_TRACE_EVENT_FIELD(int32, TraceID)
UE_TRACE_EVENT_END()

// Signals that we are in are receiving a NetSerialize function
UE_TRACE_EVENT_BEGIN(NetworkPrediction, NetRecv)
	UE_TRACE_EVENT_FIELD(int32, Frame)
	UE_TRACE_EVENT_FIELD(int32, TimeMS)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, ShouldReconcile)
	UE_TRACE_EVENT_FIELD(int32, TraceID)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, RollbackInject)
	UE_TRACE_EVENT_FIELD(int32, TraceID)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, PushInputFrame)
	UE_TRACE_EVENT_FIELD(int32, Frame)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, ProduceInput)
	UE_TRACE_EVENT_FIELD(int32, TraceID)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, OOBStateMod)
	UE_TRACE_EVENT_FIELD(int32, TraceID)
	UE_TRACE_EVENT_FIELD(int32, Frame)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, InputCmd)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, SyncState)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, AuxState)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, PhysicsState)
UE_TRACE_EVENT_END()

// Assign Id to UGameInstance ObjectKey. Assignment
struct FGameInstanceIdMap
{
	uint32 GetId(UGameInstance* Instance)
	{
		FObjectKey Key(Instance);
		
		int32 FoundIdx=INDEX_NONE;
		if (AssignedInstances.Find(Key, FoundIdx))
		{
			return (uint32)(FoundIdx+1);
		}

		AssignedInstances.Add(Key);
		uint32 Id = (uint32)AssignedInstances.Num();
		return Id;
	}

private:
	TArray<FObjectKey> AssignedInstances;

} GameInstanceMap;


//Tracks which SimulationIDs we've successfully traced NetGUIDs of
TArray<uint32> TracedSimulationNetGUIDs;
TMap<uint32, TWeakObjectPtr<const AActor>> OwningActorMap;


// ---------------------------------------------------------------------------

void FNetworkPredictionTrace::TraceSimulationCreated_Internal(FNetworkPredictionID ID, FStringBuilderBase& Builder)
{
	const uint16 AttachmentSize = Builder.Len() * sizeof(FStringBuilderBase::ElementType);

	UE_TRACE_LOG(NetworkPrediction, SimulationCreated, NetworkPredictionChannel, AttachmentSize)
		<< SimulationCreated.SimulationID((int32)ID)
		<< SimulationCreated.TraceID(ID.GetTraceID())
		<< SimulationCreated.Attachment(Builder.ToString(), AttachmentSize);
}

void FNetworkPredictionTrace::TraceWorldFrameStart(UGameInstance* GameInstance, float DeltaSeconds)
{
	if (GameInstance->GetWorld()->GetNetMode() == NM_Standalone)
	{
		// No networking yet, don't start tracing
		return;
	}

	uint32 GameInstanceID = GameInstanceMap.GetId(GameInstance);

	UE_TRACE_LOG(NetworkPrediction, WorldFrameStart, NetworkPredictionChannel)
		<< WorldFrameStart.GameInstanceID(GameInstanceID)
		<< WorldFrameStart.EngineFrameNumber(GFrameNumber)
		<< WorldFrameStart.DeltaSeconds(DeltaSeconds);
}

void FNetworkPredictionTrace::TraceSimulationConfig(int32 TraceID, ENetRole NetRole, bool bHasNetConnection, const FNetworkPredictionInstanceArchetype& Archetype, const FNetworkPredictionInstanceConfig& Config, int32 ServiceMask)
{
	UE_TRACE_LOG(NetworkPrediction, SimulationConfig, NetworkPredictionChannel)
		<< SimulationConfig.TraceID(TraceID)
		<< SimulationConfig.NetRole((uint8)NetRole)
		<< SimulationConfig.bHasNetConnection((uint8)bHasNetConnection)
		<< SimulationConfig.TickingPolicy((uint8)Archetype.TickingMode)
		<< SimulationConfig.ServiceMask(ServiceMask);
}

void FNetworkPredictionTrace::TraceSimulationScope(int32 TraceID)
{
	UE_TRACE_LOG(NetworkPrediction, SimulationScope, NetworkPredictionChannel)
		<< SimulationScope.TraceID(TraceID);

}

void FNetworkPredictionTrace::TraceTick(int32 StartMS, int32 DeltaMS, int32 OutputFrame, int32 LocalFrameOffset)
{
	UE_TRACE_LOG(NetworkPrediction, Tick, NetworkPredictionChannel)
		<< Tick.StartMS(StartMS)
		<< Tick.DeltaMS(DeltaMS)
		<< Tick.OutputFrame(OutputFrame)
		<< Tick.LocalOffsetFrame(LocalFrameOffset);
}

void FNetworkPredictionTrace::TraceSimTick(int32 TraceID)
{
	UE_TRACE_LOG(NetworkPrediction, SimTick, NetworkPredictionChannel)
		<< SimTick.TraceID(TraceID);
}

void FNetworkPredictionTrace::TraceUserState_Internal(ETraceUserState StateType, FAnsiStringBuilderBase& Builder)
{
	// Due to how string store works on the analysis side, its better to transmit the string with the null terminator
	const uint16 AttachmentSize = (uint16)((Builder.Len()+1) * sizeof(FAnsiStringBuilderBase::ElementType));

	switch(StateType)
	{
		case ETraceUserState::Input:
		{
			UE_TRACE_LOG(NetworkPrediction, InputCmd, NetworkPredictionChannel, AttachmentSize)
				<< InputCmd.Attachment(Builder.GetData(), AttachmentSize);
			break;
		}
		case ETraceUserState::Sync:
		{
			UE_TRACE_LOG(NetworkPrediction, SyncState, NetworkPredictionChannel, AttachmentSize)
				<< SyncState.Attachment(Builder.GetData(), AttachmentSize);
			break;
		}
		case ETraceUserState::Aux:
		{
			UE_TRACE_LOG(NetworkPrediction, AuxState, NetworkPredictionChannel, AttachmentSize)
				<< AuxState.Attachment(Builder.GetData(), AttachmentSize);
			break;
		}
		case ETraceUserState::Physics:
		{
			UE_TRACE_LOG(NetworkPrediction, PhysicsState, NetworkPredictionChannel, AttachmentSize)
				<< PhysicsState.Attachment(Builder.GetData(), AttachmentSize);
			break;
		}
	}
}

void FNetworkPredictionTrace::TraceNetRecv(int32 Frame, int32 TimeMS)
{
	UE_TRACE_LOG(NetworkPrediction, NetRecv, NetworkPredictionChannel)
		<< NetRecv.Frame(Frame)
		<< NetRecv.TimeMS(TimeMS);
}

void FNetworkPredictionTrace::TraceShouldReconcile(int32 TraceID)
{
	UE_TRACE_LOG(NetworkPrediction, ShouldReconcile, NetworkPredictionChannel)
		<< ShouldReconcile.TraceID(TraceID);
}

void FNetworkPredictionTrace::TraceRollbackInject(int32 TraceID)
{
	UE_TRACE_LOG(NetworkPrediction, RollbackInject, NetworkPredictionChannel)
		<< RollbackInject.TraceID(TraceID);
}

void FNetworkPredictionTrace::TracePIEStart()
{
	UE_TRACE_LOG(NetworkPrediction, PieBegin, NetworkPredictionChannel)
		<< PieBegin.DummyData(0); // temp to quiet clang

}

void FNetworkPredictionTrace::TracePushInputFrame(int32 Frame)
{
	UE_TRACE_LOG(NetworkPrediction, PushInputFrame, NetworkPredictionChannel)
		<< PushInputFrame.Frame(Frame);
}

void FNetworkPredictionTrace::TraceProduceInput(int32 TraceID)
{
	UE_TRACE_LOG(NetworkPrediction, ProduceInput, NetworkPredictionChannel)
		<< ProduceInput.TraceID(TraceID);
}

void FNetworkPredictionTrace::TraceOOBStateMod(int32 TraceID, int32 Frame, const FAnsiStringView& StrView)
{
	const uint16 AttachmentSize = (uint16)((StrView.Len()) * sizeof(FAnsiStringView::ElementType));

	UE_TRACE_LOG(NetworkPrediction, OOBStateMod, NetworkPredictionChannel, AttachmentSize)
		<< OOBStateMod.TraceID(TraceID)
		<< OOBStateMod.Frame(Frame)
		<< OOBStateMod.Attachment(StrView.GetData(), AttachmentSize);
}

#include "CoreTypes.h"
#include "Misc/VarArgs.h"
#include "HAL/UnrealMemory.h"
#include "Templates/UnrealTemplate.h"

// Copied from VarargsHelper.h
#define GROWABLE_LOGF(SerializeFunc) \
	int32	BufferSize	= 1024; \
	TCHAR*	Buffer		= NULL; \
	int32	Result		= -1; \
	/* allocate some stack space to use on the first pass, which matches most strings */ \
	TCHAR	StackBuffer[512]; \
	TCHAR*	AllocatedBuffer = NULL; \
\
	/* first, try using the stack buffer */ \
	Buffer = StackBuffer; \
	GET_VARARGS_RESULT( Buffer, UE_ARRAY_COUNT(StackBuffer), UE_ARRAY_COUNT(StackBuffer) - 1, Fmt, Fmt, Result ); \
\
	/* if that fails, then use heap allocation to make enough space */ \
	while(Result == -1) \
	{ \
		FMemory::SystemFree(AllocatedBuffer); \
		/* We need to use malloc here directly as GMalloc might not be safe. */ \
		Buffer = AllocatedBuffer = (TCHAR*) FMemory::SystemMalloc( BufferSize * sizeof(TCHAR) ); \
		if (Buffer == NULL) \
		{ \
			return; \
		} \
		GET_VARARGS_RESULT( Buffer, BufferSize, BufferSize-1, Fmt, Fmt, Result ); \
		BufferSize *= 2; \
	}; \
	Buffer[Result] = 0; \
	; \
\
	SerializeFunc; \
	/*FMemory::SystemFree(AllocatedBuffer);*/


void FNetworkPredictionTrace::TraceSystemFault(const TCHAR* Fmt, ...)
{
	GROWABLE_LOGF( 

		check(Result >= 0 );
	const uint16 AttachmentSize = (Result+1) * sizeof(TCHAR);

	UE_LOG(LogNetworkPrediction, Warning, TEXT("SystemFault: %s"), Buffer);
	);

	UE_TRACE_LOG(NetworkPrediction, SystemFault, NetworkPredictionChannel, AttachmentSize)
		<< SystemFault.Attachment(Buffer, AttachmentSize);

	FMemory::SystemFree(AllocatedBuffer);
}