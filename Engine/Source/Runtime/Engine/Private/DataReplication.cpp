// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DataChannel.cpp: Unreal datachannel implementation.
=============================================================================*/

#include "Net/DataReplication.h"
#include "Misc/MemStack.h"
#include "HAL/IConsoleManager.h"
#include "EngineStats.h"
#include "Engine/World.h"
#include "Net/DataBunch.h"
#include "Net/NetworkProfiler.h"
#include "Engine/PackageMapClient.h"
#include "Net/RepLayout.h"
#include "Engine/ActorChannel.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "Net/NetworkGranularMemoryLogging.h"
#include "Misc/ScopeExit.h"

DECLARE_CYCLE_STAT(TEXT("Custom Delta Property Rep Time"), STAT_NetReplicateCustomDeltaPropTime, STATGROUP_Game);
DECLARE_CYCLE_STAT(TEXT("ReceiveRPC"), STAT_NetReceiveRPC, STATGROUP_Game);

static TAutoConsoleVariable<int32> CVarMaxRPCPerNetUpdate( TEXT( "net.MaxRPCPerNetUpdate" ), 2, TEXT( "Maximum number of RPCs allowed per net update" ) );
static TAutoConsoleVariable<int32> CVarDelayUnmappedRPCs( TEXT("net.DelayUnmappedRPCs" ), 0, TEXT( "If >0 delay received RPCs with unmapped properties" ) );

static TAutoConsoleVariable<FString> CVarNetReplicationDebugProperty(
	TEXT("net.Replication.DebugProperty"),
	TEXT(""),
	TEXT("Debugs Replication of property by name")
	TEXT("Partial name of property to debug"),
	ECVF_Default);

int32 GNetRPCDebug = 0;
static FAutoConsoleVariableRef CVarNetRPCDebug(
	TEXT("net.RPC.Debug"),
	GNetRPCDebug,
	TEXT("Print all RPC bunches sent over the network\n")
	TEXT(" 0: no print.\n")
	TEXT(" 1: Print bunches as they are sent."),
	ECVF_Default);

int32 GSupportsFastArrayDelta = 1;
static FAutoConsoleVariableRef CVarSupportsFastArrayDelta(
	TEXT("net.SupportFastArrayDelta"),
	GSupportsFastArrayDelta,
	TEXT("Whether or not Fast Array Struct Delta Serialization is enabled.")
);

extern TAutoConsoleVariable<int32> CVarNetEnableDetailedScopeCounters;

PRAGMA_DISABLE_DEPRECATION_WARNINGS
class FNetSerializeCB : public INetSerializeCB
{
private:

	// This is an acceleration so if we make back to back requests for the same type
	// we don't have to do repeated lookups.
	struct FCachedRequestState
	{
		UClass* ObjectClass = nullptr;
		UScriptStruct* Struct = nullptr;
		TSharedPtr<FRepLayout> RepLayout;
		bool bWasRequestFromClass = false;
	};

public:

	FNetSerializeCB():
		Driver(nullptr)
	{
		check(0);
	}

	FNetSerializeCB(UNetDriver* InNetDriver):
		Driver(InNetDriver)
	{
	}

	void SetChangelistMgr(TSharedPtr<FReplicationChangelistMgr> InChangelistMgr)
	{
		ChangelistMgr = InChangelistMgr;
	}

private:

	void UpdateCachedRepLayout()
	{
		if (!CachedRequestState.RepLayout.IsValid())
		{
			if (CachedRequestState.bWasRequestFromClass)
			{
				CachedRequestState.RepLayout = Driver->GetObjectClassRepLayout(CachedRequestState.ObjectClass);
			}
			else
			{
				CachedRequestState.RepLayout = Driver->GetStructRepLayout(CachedRequestState.Struct);
			}
		}
	}

	void UpdateCachedState(UClass* ObjectClass, UStruct* Struct)
	{
		if (CachedRequestState.ObjectClass != ObjectClass)
		{
			CachedRequestState.ObjectClass = ObjectClass;
			CachedRequestState.Struct = CastChecked<UScriptStruct>(Struct);
			CachedRequestState.bWasRequestFromClass = true;
			CachedRequestState.RepLayout.Reset();
		}
	}

	void UpdateCachedState(UStruct* Struct)
	{
		if (CachedRequestState.Struct != Struct || CachedRequestState.ObjectClass != nullptr)
		{
			CachedRequestState.ObjectClass = nullptr;
			CachedRequestState.Struct = CastChecked<UScriptStruct>(Struct);
			CachedRequestState.bWasRequestFromClass = false;
			CachedRequestState.RepLayout.Reset();
		}
	}

public:

	virtual void NetSerializeStruct(FNetDeltaSerializeInfo& Params) override final
	{
		UpdateCachedState(Params.Struct);
		FBitArchive& Ar = Params.Reader ? static_cast<FBitArchive&>(*Params.Reader) : static_cast<FBitArchive&>(*Params.Writer);
		Params.bOutHasMoreUnmapped = false;

		if (EnumHasAnyFlags(CachedRequestState.Struct->StructFlags, STRUCT_NetSerializeNative))
		{
			UScriptStruct::ICppStructOps* CppStructOps = CachedRequestState.Struct->GetCppStructOps();
			check(CppStructOps);
			bool bSuccess = true;

			if (!CppStructOps->NetSerialize(Ar, Params.Map, bSuccess, Params.Data))
			{
				Params.bOutHasMoreUnmapped = true;
			}

			if (!bSuccess)
			{
				UE_LOG(LogRep, Warning, TEXT("NetSerializeStruct: Native NetSerialize %s failed."), *Params.Struct->GetFullName());
			}
		}
		else
		{
			UpdateCachedRepLayout();
			UPackageMapClient* PackageMapClient = ((UPackageMapClient*)Params.Map);

			if (PackageMapClient && PackageMapClient->GetConnection()->InternalAck)
			{
				if (Ar.IsSaving())
				{
					TArray< uint16 > Changed;
					CachedRequestState.RepLayout->SendProperties_BackwardsCompatible(nullptr, nullptr, (uint8*)Params.Data, PackageMapClient->GetConnection(), static_cast<FNetBitWriter&>(Ar), Changed);
				}
				else
				{
					bool bHasGuidsChanged = false;
					CachedRequestState.RepLayout->ReceiveProperties_BackwardsCompatible(PackageMapClient->GetConnection(), nullptr, Params.Data, static_cast<FNetBitReader&>(Ar), Params.bOutHasMoreUnmapped, false, bHasGuidsChanged);
				}
			}
			else
			{
				CachedRequestState.RepLayout->SerializePropertiesForStruct(Params.Struct, Ar, Params.Map, Params.Data, Params.bOutHasMoreUnmapped);
			}
		}
	}

	virtual bool NetDeltaSerializeForFastArray(FFastArrayDeltaSerializeParams& Params) override final
	{
		UpdateCachedState(Params.DeltaSerializeInfo.Object->GetClass(), Params.DeltaSerializeInfo.Struct);
		UpdateCachedRepLayout();
		return CachedRequestState.RepLayout->DeltaSerializeFastArrayProperty(Params, ChangelistMgr.Get());
	}

	virtual void GatherGuidReferencesForFastArray(FFastArrayDeltaSerializeParams& Params) override final
	{
		UpdateCachedState(Params.DeltaSerializeInfo.Object->GetClass(), Params.DeltaSerializeInfo.Struct);
		UpdateCachedRepLayout();
		CachedRequestState.RepLayout->GatherGuidReferencesForFastArray(Params);
	}

	virtual bool MoveGuidToUnmappedForFastArray(FFastArrayDeltaSerializeParams& Params) override final
	{
		UpdateCachedState(Params.DeltaSerializeInfo.Object->GetClass(), Params.DeltaSerializeInfo.Struct);
		UpdateCachedRepLayout();
		return CachedRequestState.RepLayout->MoveMappedObjectToUnmappedForFastArray(Params);
	}

	virtual void UpdateUnmappedGuidsForFastArray(FFastArrayDeltaSerializeParams& Params) override final
	{
		UpdateCachedState(Params.DeltaSerializeInfo.Object->GetClass(), Params.DeltaSerializeInfo.Struct);
		UpdateCachedRepLayout();
		CachedRequestState.RepLayout->UpdateUnmappedGuidsForFastArray(Params);
	}

public:

	// These can go away once we do a full merge of Custom Delta and RepLayout.

	static bool SendCustomDeltaProperty(
		const FRepLayout& RepLayout,
		FNetDeltaSerializeInfo& Params,
		uint16 CustomDeltaIndex)
	{
		return RepLayout.SendCustomDeltaProperty(Params, CustomDeltaIndex);
	}

	static bool ReceiveCustomDeltaProperty(
		const FRepLayout& RepLayout,
		FReceivingRepState* ReceivingRepState,
		FNetDeltaSerializeInfo& Params,
		UStructProperty* ReplicatedProp)
	{
		return RepLayout.ReceiveCustomDeltaProperty(ReceivingRepState, Params, ReplicatedProp);
	}

	static void PreSendCustomDeltaProperties(
		const FRepLayout& RepLayout,
		UObject* Object,
		UNetConnection* Connection,
		FReplicationChangelistMgr& ChangelistMgr,
		TArray<TSharedPtr<INetDeltaBaseState>>& CustomDeltaStates)
	{
		RepLayout.PreSendCustomDeltaProperties(Object, Connection, ChangelistMgr, CustomDeltaStates);
	}

	static void PostSendCustomDeltaProperties(
		const FRepLayout& RepLayout,
		UObject* Object,
		UNetConnection* Connection,
		FReplicationChangelistMgr& ChangelistMgr,
		TArray<TSharedPtr<INetDeltaBaseState>>& CustomDeltaStates)
	{
		RepLayout.PostSendCustomDeltaProperties(Object, Connection, ChangelistMgr, CustomDeltaStates);
	}

	static uint16 GetNumLifetimeCustomDeltaProperties(const FRepLayout& RepLayout)
	{
		return RepLayout.GetNumLifetimeCustomDeltaProperties();
	}

	static UProperty* GetLifetimeCustomDeltaProperty(const FRepLayout& RepLayout, const uint16 CustomDeltaPropertyIndex)
	{
		return RepLayout.GetLifetimeCustomDeltaProperty(CustomDeltaPropertyIndex);
	}

	static void UpdateChangelistMgr(
		const FRepLayout& RepLayout,
		FSendingRepState* RESTRICT RepState,
		FReplicationChangelistMgr& InChangelistMgr,
		const UObject* InObject,
		const uint32 ReplicationFrame,
		const FReplicationFlags& RepFlags,
		const bool bForceCompare)
	{
		RepLayout.UpdateChangelistMgr(RepState, InChangelistMgr, InObject, ReplicationFrame, RepFlags, bForceCompare);
	}

	static const ELifetimeCondition GetLifetimeCustomDeltaPropertyCondition(const FRepLayout& RepLayout, const uint16 CustomDeltaPropertyIndex)
	{
		return RepLayout.GetLifetimeCustomDeltaPropertyCondition(CustomDeltaPropertyIndex);
	}

private:

	UNetDriver* Driver;
	FCachedRequestState CachedRequestState;
	TSharedPtr<FReplicationChangelistMgr> ChangelistMgr;
};
PRAGMA_ENABLE_DEPRECATION_WARNINGS

PRAGMA_DISABLE_DEPRECATION_WARNINGS
FObjectReplicator::FObjectReplicator()
	: bLastUpdateEmpty(false)
	, bOpenAckCalled(false)
	, bForceUpdateUnmapped(false)
	, bHasReplicatedProperties(false)
	, bSupportsFastArrayDelta(false)
	, ObjectClass(nullptr)
	, ObjectPtr(nullptr)
	, Connection(nullptr)
	, OwningChannel(nullptr)
	, RemoteFunctions(nullptr)
{
}

FObjectReplicator::~FObjectReplicator()
{
	CleanUp();
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

PRAGMA_DISABLE_DEPRECATION_WARNINGS
bool FObjectReplicator::SerializeCustomDeltaProperty(
	UNetConnection* Connection,
	void* Src,
	UProperty* Property,
	uint32 ArrayIndex,
	FNetBitWriter& OutBunch,
	TSharedPtr<INetDeltaBaseState>& NewFullState,
	TSharedPtr<INetDeltaBaseState>& OldState)
{
	check( NewFullState.IsValid() == false ); // NewState is passed in as nullptr and instantiated within this function if necessary

	CONDITIONAL_SCOPE_CYCLE_COUNTER( STAT_NetSerializeItemDeltaTime, CVarNetEnableDetailedScopeCounters.GetValueOnAnyThread() > 0 );

	UStructProperty * StructProperty = CastChecked< UStructProperty >( Property );

	//------------------------------------------------
	//	Custom NetDeltaSerialization
	//------------------------------------------------
	if ( !ensure( ( StructProperty->Struct->StructFlags & STRUCT_NetDeltaSerializeNative ) != 0 ) )
	{
		return false;
	}

	FNetSerializeCB NetSerializeCB( Connection->Driver );

	FNetDeltaSerializeInfo Parms;
	Parms.Data = Property->ContainerPtrToValuePtr<void>(Src, ArrayIndex);
	Parms.Object = reinterpret_cast<UObject*>(Src);
	Parms.Connection = Connection;
	Parms.bInternalAck = Connection->InternalAck;
	Parms.Writer = &OutBunch;
	Parms.Map = Connection->PackageMap;
	Parms.OldState = OldState.Get();
	Parms.NewState = &NewFullState;
	Parms.NetSerializeCB = &NetSerializeCB;
	Parms.bIsWritingOnClient = (Connection->Driver && Connection->Driver->GetWorld()) ? Connection->Driver->GetWorld()->IsRecordingClientReplay() : false;

	UScriptStruct::ICppStructOps * CppStructOps = StructProperty->Struct->GetCppStructOps();

	check(CppStructOps); // else should not have STRUCT_NetSerializeNative

	Parms.Struct = StructProperty->Struct;

	if ( Property->ArrayDim != 1 )
	{
		OutBunch.SerializeIntPacked( ArrayIndex );
	}

	return CppStructOps->NetDeltaSerialize(Parms, Parms.Data);
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

bool FObjectReplicator::SendCustomDeltaProperty(UObject* InObject, UProperty* Property, uint32 ArrayIndex, FNetBitWriter& OutBunch, TSharedPtr<INetDeltaBaseState>& NewFullState, TSharedPtr<INetDeltaBaseState>& OldState)
{
	return SendCustomDeltaProperty(InObject, Property->RepIndex + ArrayIndex, OutBunch, NewFullState, OldState);
}

bool FObjectReplicator::SendCustomDeltaProperty(UObject* InObject, uint16 CustomDeltaIndex, FNetBitWriter& OutBunch, TSharedPtr<INetDeltaBaseState>& NewFullState, TSharedPtr<INetDeltaBaseState>& OldState)
{
	check(!NewFullState.IsValid()); // NewState is passed in as nullptr and instantiated within this function if necessary
	check(RepLayout);

	CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_NetSerializeItemDeltaTime, CVarNetEnableDetailedScopeCounters.GetValueOnAnyThread() > 0);

	UNetDriver* const ConnectionDriver = Connection->GetDriver();
	FNetSerializeCB NetSerializeCB(ConnectionDriver);
	NetSerializeCB.SetChangelistMgr(ChangelistMgr);

	FNetDeltaSerializeInfo Parms;
	Parms.Object = InObject;
	Parms.Writer = &OutBunch;
	Parms.Map = Connection->PackageMap;
	Parms.OldState = OldState.Get();
	Parms.NewState = &NewFullState;
	Parms.NetSerializeCB = &NetSerializeCB;
	Parms.bIsWritingOnClient = ConnectionDriver && ConnectionDriver->GetWorld() && ConnectionDriver->GetWorld()->IsRecordingClientReplay();
	Parms.CustomDeltaIndex = CustomDeltaIndex;
	Parms.bSupportsFastArrayDeltaStructSerialization = bSupportsFastArrayDelta;
	Parms.Connection = Connection;
	Parms.bInternalAck = Connection->InternalAck;

	return FNetSerializeCB::SendCustomDeltaProperty(*RepLayout, Parms, CustomDeltaIndex);
}

/** 
 *	Utility function to make a copy of the net properties 
 *	@param	Source - Memory to copy initial state from
**/
void FObjectReplicator::InitRecentProperties(uint8* Source)
{
	// TODO: Could we just use the cached ObjectPtr here?
	UObject* MyObject = GetObject();

	check(MyObject);
	check(Connection);
	check(!RepState.IsValid());

	UNetDriver* const ConnectionDriver = Connection->GetDriver();
	const bool bIsServer = ConnectionDriver->IsServer();
	const bool bCreateSendingState = bIsServer || ConnectionDriver->MaySendProperties();
	const FRepLayout& LocalRepLayout = *RepLayout;

	UClass* InObjectClass = MyObject->GetClass();

	// Initialize the RepState memory
	// Clients don't need RepChangedPropertyTracker's, as they are mainly
	// used temporarily disable property replication, or store data
	// for replays (and the DemoNetDriver will be acts as a server during recording).
	TSharedPtr<FRepChangedPropertyTracker> RepChangedPropertyTracker = bCreateSendingState ? ConnectionDriver->FindOrCreateRepChangedPropertyTracker(MyObject) : nullptr;

	// If acting as a server and are InternalAck, that means we're recording.
	// In that case, we don't need to create any receiving state, as no one will be sending data to us.
	ECreateRepStateFlags Flags = (Connection->InternalAck && bIsServer) ? ECreateRepStateFlags::SkipCreateReceivingState : ECreateRepStateFlags::None;
	RepState = LocalRepLayout.CreateRepState(Source, RepChangedPropertyTracker, Flags);

	if (!bCreateSendingState)
	{
		// Clients don't need to initialize shadow state (and in fact it causes issues in replays)
		return;
	}

	bSupportsFastArrayDelta = !!GSupportsFastArrayDelta;

	if (FSendingRepState* SendingRepState = RepState->GetSendingRepState())
	{
		// We should just update this method to accept an object pointer.
		UObject* UseObject = reinterpret_cast<UObject*>(Source);

		// Init custom delta property state
		const uint16 NumLifetimeCustomDeltaProperties = FNetSerializeCB::GetNumLifetimeCustomDeltaProperties(LocalRepLayout);
		SendingRepState->RecentCustomDeltaState.SetNum(NumLifetimeCustomDeltaProperties);

		const bool bIsRecordingReplay = Connection->InternalAck;

		if (bIsRecordingReplay)
		{
			SendingRepState->CDOCustomDeltaState.SetNum(NumLifetimeCustomDeltaProperties);
			SendingRepState->CheckpointCustomDeltaState.SetNum(NumLifetimeCustomDeltaProperties);
		}

		for (uint16 CustomDeltaProperty = 0; CustomDeltaProperty < NumLifetimeCustomDeltaProperties; ++CustomDeltaProperty)
		{
			FOutBunch DeltaState(Connection->PackageMap);
			TSharedPtr<INetDeltaBaseState>& NewState = SendingRepState->RecentCustomDeltaState[CustomDeltaProperty];
			NewState.Reset();

			TSharedPtr<INetDeltaBaseState> OldState;

			SendCustomDeltaProperty(UseObject, CustomDeltaProperty, DeltaState, NewState, OldState);

			if (bIsRecordingReplay)
			{
				// Store the initial delta state in case we need it for when we're asked to resend all data since channel was first opened (bResendAllDataSinceOpen)
				SendingRepState->CDOCustomDeltaState[CustomDeltaProperty] = NewState;
				SendingRepState->CheckpointCustomDeltaState[CustomDeltaProperty] = NewState;
			}
		}
	}
}

/** Takes Data, and compares against shadow state to log differences */
bool FObjectReplicator::ValidateAgainstState( const UObject* ObjectState )
{
	if (!RepLayout.IsValid())
	{
		UE_LOG(LogRep, Warning, TEXT("ValidateAgainstState: RepLayout.IsValid() == false"));
		return false;
	}

	if (!RepState.IsValid())
	{
		UE_LOG(LogRep, Warning, TEXT("ValidateAgainstState: RepState.IsValid() == false"));
		return false;
	}

	if (!ChangelistMgr.IsValid())
	{
		UE_LOG(LogRep, Warning, TEXT("ValidateAgainstState: ChangelistMgr.IsValid() == false"));
		return false;
	}

	FRepChangelistState* ChangelistState = ChangelistMgr->GetRepChangelistState();
	if (ChangelistState == nullptr)
	{
		UE_LOG(LogRep, Warning, TEXT("ValidateAgainstState: ChangelistState == nullptr"));
		return false;
	}

	FRepShadowDataBuffer ShadowData(ChangelistState->StaticBuffer.GetData());
	FConstRepObjectDataBuffer ObjectData(ObjectState);

	if (RepLayout->DiffProperties(nullptr, ShadowData, ObjectData, EDiffPropertiesFlags::None))
	{
		UE_LOG(LogRep, Warning, TEXT("ValidateAgainstState: Properties changed for %s"), *ObjectState->GetName());
		return false;
	}

	return true;
}

void FObjectReplicator::InitWithObject( UObject* InObject, UNetConnection * InConnection, bool bUseDefaultState )
{
	check( GetObject() == nullptr );
	check( ObjectClass == nullptr );
	check( bLastUpdateEmpty == false );
	check( Connection == nullptr );
	check( OwningChannel == nullptr );
	check( !RepState.IsValid() );
	check( RemoteFunctions == nullptr );
	check( !RepLayout.IsValid() );

	SetObject( InObject );

	if ( GetObject() == nullptr )
	{
		// This may seem weird that we're checking for nullptr, but the SetObject above will wrap this object with TWeakObjectPtr
		// If the object is pending kill, it will switch to nullptr, we're just making sure we handle this invalid edge case
		UE_LOG(LogRep, Error, TEXT("InitWithObject: Object == nullptr"));
		return;
	}

	ObjectClass					= InObject->GetClass();
	Connection					= InConnection;
	RemoteFunctions				= nullptr;
	bHasReplicatedProperties	= false;
	bOpenAckCalled				= false;
	RepState					= nullptr;
	OwningChannel				= nullptr;		// Initially nullptr until StartReplicating is called
	TrackedGuidMemoryBytes		= 0;

	RepLayout = Connection->Driver->GetObjectClassRepLayout( ObjectClass );

	// Make a copy of the net properties
	uint8* Source = bUseDefaultState ? (uint8*)GetObject()->GetArchetype() : (uint8*)InObject;

	InitRecentProperties( Source );

	Connection->Driver->AllOwnedReplicators.Add(this);
}

void FObjectReplicator::CleanUp()
{
	if ( OwningChannel != nullptr )
	{
		StopReplicating( OwningChannel );		// We shouldn't get here, but just in case
	}

	if ( Connection != nullptr )
	{
		for ( const FNetworkGUID& GUID : ReferencedGuids )
		{
			TSet< FObjectReplicator* >& Replicators = Connection->Driver->GuidToReplicatorMap.FindChecked( GUID );

			Replicators.Remove( this );

			if ( Replicators.Num() == 0 )
			{
				Connection->Driver->GuidToReplicatorMap.Remove( GUID );
			}
		}

		Connection->Driver->UnmappedReplicators.Remove( this );

		Connection->Driver->TotalTrackedGuidMemoryBytes -= TrackedGuidMemoryBytes;

		Connection->Driver->AllOwnedReplicators.Remove( this );
	}
	else
	{
		ensureMsgf( TrackedGuidMemoryBytes == 0, TEXT( "TrackedGuidMemoryBytes should be 0" ) );
		ensureMsgf( ReferencedGuids.Num() == 0, TEXT( "ReferencedGuids should be 0" ) );
	}

	ReferencedGuids.Empty();
	TrackedGuidMemoryBytes = 0;

	SetObject( nullptr );

	ObjectClass					= nullptr;
	Connection					= nullptr;
	RemoteFunctions				= nullptr;
	bHasReplicatedProperties	= false;
	bOpenAckCalled				= false;

	RepState = nullptr;
	CheckpointRepState = nullptr;
}

void FObjectReplicator::StartReplicating(class UActorChannel * InActorChannel)
{
	check(OwningChannel == nullptr);
	check(InActorChannel != nullptr);
	check(InActorChannel->Connection == Connection);

	UObject* const Object = GetObject();
	if (Object == nullptr)
	{
		UE_LOG(LogRep, Error, TEXT("StartReplicating: Object == nullptr"));
		return;
	}

	if (!ensureMsgf(ObjectClass != nullptr, TEXT( "StartReplicating: ObjectClass == nullptr. Object = %s. Channel actor = %s. %s" ), *GetFullNameSafe(Object), *GetFullNameSafe(InActorChannel->GetActor()), *InActorChannel->Connection->Describe()))
	{
		return;
	}

	if (UClass* const ObjectPtrClass = Object->GetClass())
	{
		// Something is overwriting a bit in the ObjectClass pointer so it's becoming invalid - fix up the pointer to prevent crashing later until the real cause can be identified.
		if (!ensureMsgf(ObjectClass == ObjectPtrClass, TEXT("StartReplicating: ObjectClass and ObjectPtr's class are not equal and they should be. Object = %s. Channel actor = %s. %s"), *GetFullNameSafe(Object), *GetFullNameSafe(InActorChannel->GetActor()), *InActorChannel->Connection->Describe()))
		{
			ObjectClass = ObjectPtrClass;
		}
	}

	OwningChannel = InActorChannel;

	UNetDriver* const ConnectionNetDriver = Connection->GetDriver();

	// Cache off netGUID so if this object gets deleted we can close it
	ObjectNetGUID = ConnectionNetDriver->GuidCache->GetOrAssignNetGUID( Object );

	const bool bIsValidToReplicate = !ObjectNetGUID.IsDefault() && ObjectNetGUID.IsValid();
	if (UNLIKELY(!bIsValidToReplicate))
	{
		// This has mostly been seen when doing a Seamless Travel Restart.
		// In that case, the server can think the client is still on the same map and replicate objects before
		// the client has finished the travel, and the client will later remove those references from the Package Map.
		UE_LOG(LogRep, Error, TEXT("StartReplicating: Invalid Net GUID. Object may fail to replicate properties or handle RPCs. Object %s"), *Object->GetPathName());
		return;
	}

	if (ConnectionNetDriver->IsServer() || ConnectionNetDriver->MaySendProperties())
	{
		if (FSendingRepState* SendingRepState = RepState.IsValid() ? RepState->GetSendingRepState() : nullptr)
		{
			// Allocate retirement list.
			// SetNum now constructs, so this is safe

			SendingRepState->Retirement.SetNum(ObjectClass->ClassReps.Num());
		}

		const UWorld* const World = ConnectionNetDriver->GetWorld();
		UNetDriver* const WorldNetDriver = World ? World->GetNetDriver() : nullptr;

		// Prefer the changelist manager on the main net driver (so we share across net drivers if possible)
		if (WorldNetDriver && WorldNetDriver->IsServer())
		{
			ChangelistMgr = WorldNetDriver->GetReplicationChangeListMgr(Object);
		}
		else
		{
			ChangelistMgr = ConnectionNetDriver->GetReplicationChangeListMgr(Object);
		}
	}
}

void ValidateRetirementHistory(const FPropertyRetirement & Retire, const UObject* Object)
{
#if !UE_BUILD_SHIPPING
	checkf( Retire.SanityTag == FPropertyRetirement::ExpectedSanityTag, TEXT( "Invalid Retire.SanityTag. Object: %s" ), *GetFullNameSafe(Object) );

	FPropertyRetirement * Rec = Retire.Next;	// Note the first element is 'head' that we dont actually use

	FPacketIdRange LastRange;

	while ( Rec != nullptr )
	{
		checkf( Rec->SanityTag == FPropertyRetirement::ExpectedSanityTag, TEXT( "Invalid Rec->SanityTag. Object: %s" ), *GetFullNameSafe(Object) );
		checkf( Rec->OutPacketIdRange.Last >= Rec->OutPacketIdRange.First, TEXT( "Invalid packet id range (Last < First). Object: %s" ), *GetFullNameSafe(Object) );
		checkf( Rec->OutPacketIdRange.First >= LastRange.Last, TEXT( "Invalid packet id range (First < LastRange.Last). Object: %s" ), *GetFullNameSafe(Object) );		// Bunch merging and queuing can cause this overlap

		LastRange = Rec->OutPacketIdRange;

		Rec = Rec->Next;
	}
#endif
}

void FObjectReplicator::StopReplicating( class UActorChannel * InActorChannel )
{
	check(OwningChannel != nullptr);
	check(OwningChannel->Connection == Connection);
	check(OwningChannel == InActorChannel);

	OwningChannel = nullptr;

	const UObject* Object = GetObject();

	if (FSendingRepState* SendingRepState = RepState.IsValid() ? RepState->GetSendingRepState() : nullptr)
	{
		// Cleanup retirement records
		for (int32 i = SendingRepState->Retirement.Num() - 1; i >= 0; i--)
		{
			FPropertyRetirement& Retirement = SendingRepState->Retirement[i];
			ValidateRetirementHistory(Retirement, Object);

			FPropertyRetirement* Rec = Retirement.Next;
			Retirement.Next = nullptr;

			// We dont need to explicitly delete Retirement, but anything in the Next chain needs to be.
			while (Rec != nullptr)
			{
				FPropertyRetirement* Next = Rec->Next;
				delete Rec;
				Rec = Next;
			}
		}

		SendingRepState->Retirement.Empty();
	}

	PendingLocalRPCs.Empty();

	if (RemoteFunctions != nullptr)
	{
		delete RemoteFunctions;
		RemoteFunctions = nullptr;
	}
}

/**
 * Handling NAKs / Property Retransmission.
 *
 * Note, NACK handling only occurs on connections that "replicate" data, which is currently
 * only Servers. RPC retransmission is handled elsewhere.
 *
 * RepLayouts:
 *
 *		As we send properties through FRepLayout the is a Changelist Manager that is shared
 *		between all connections tracks sets of properties that were recently changed (history items),
 *		as well as one aggregate set of all properties that have ever been sent.
 *
 *		Each Sending Rep State, which is connection unique, also tracks the set of changed
 *		properties. These history items will only be created when replicating the object,
 *		so there will be fewer of them in general, but they will still contain any properties
 *		that compared differently (not *just* the properties that were actually replicated).
 *
 *		Whenever a NAK is received, we will iterate over the SendingRepState changelist
 *		and mark any of the properties sent in the NAKed packet for retransmission.
 *
 *		The next time Properties are replicated for the Object, we will merge in any changelists
 *		from NAKed history items.
 *
 * Custom Delta Properties:
 *
 *		For Custom Delta Properties (CDP), we rely primarily on FPropertyRetirements and INetDeltaBaseState
 *		for tracking property retransmission.
 *
 *		INetDeltaBaseStates are used to tracked internal state specific to a given type of CDP.
 *		For example, Fast Array Replicators will use FNetFastTArrayBaseState, or some type
 *		derived from that.
 *
 *		When an FObjectReplicator is created, we will create an INetDeltaBaseState for every CDP,
 *		as well as a dummy FPropertyRetirement. This Property Retirement is used as the head
 *		of a linked list of Retirements, and is generally never populated with any useful information.
 *
 *		Every time we replicate a CDP, we will pass in the most recent Base State, and we will be
 *		returned a new CDP. If data is actually sent, then we will create a new Property Retirement,
 *		adding it as the tail of our linked list. The new Property Retirement will also hold a reference
 *		to the old INetDeltaBaseState (i.e., the state of the CDP before it replicated its properties).
 *
 *		Just before replicating, we will go through and free any ACKed FPropertyRetirments (see
 *		UpdateAckedRetirements).
 *
 *		After replicating, we will cache off the returned Base State to be used as the "old" state
 *		the next time the property is replicated.
 *
 *		Whenever a NAK is received, we will run through our Property Retirements. Any retirements
 *		that predate the NACK will be removed and treated as if they were ACKs. The first
 *		retirement that is found to be within the NAKed range will have its INetDeltaBaseState
 *		restored (which should be the state before the NAKed packet was sent), and then
 *		that retirement as well as all remaining will be removed.
 *
 *		The onus is then on the CDP to resend any necessary properties based on its current / live
 *		state and the restored Net Delta Base State.
 *
 * Fast Array Properties:
 *
 *		Fast Array Properties are implemented as Custom Delta Properties (CDP). Therefore, they mostly
 *		follow the flow laid out above.

 *		FNetFastTArrayBaseState is the basis for all Fast Array Serializer INetDeltaBaseStates.
 *		This struct tracks the Replication Key of the Array, the ID to Replication Key map of individual
 *		Array Items, and a History Number.
 *
 *		As we replicate Fast Array Properties, we use the Array Replication key to see if anything
 *		is possibly dirty in the Array and the ID to Replication map to see which Array Element
 *		items actually are dirty. A mismatch between the Net Base State Key and the Key stored on
 *		the live Fast Array (either the Array Replication Key, or any Item Key) is how we determine
 *		if the Array or Items is dirty.
 *
 *		Whenever a NAK is received, our Old Base State will be reset to the last known ACKed value,
 *		as described in the CDP section above. This means that our Array Replication Key and ID To
 *		Item Replication Key should be reset to those states, forcing a mismatch the next time we
 *		replicate if anything has changed.
 *
 *		When net.SupportFastArrayDelta is enabled, we perform an additional step in which we actually
 *		compare the properties of dirty items. This is very similar to normal Property replication
 *		using RepLayouts, and leverages most of the same code.
 *
 *		This includes tracking history items just like Rep Layout. Instead of tracking histories per
 *		Sending Rep State / Per Connection, we just manage a single set of Histories on the Rep
 *		Changelist Mgr. Changelists are stored per Fast Array Item, and are referenced via ID.
 *
 *		Whenever we go to replicate a Fast Array Item, we will merge together all changelists since
 *		we last sent that item, and send those accumulated changes.
 *
 *		This means that property retransmission for Fast Array Items is an amalgamation of Rep Layout
 *		retransmission and CDP retransmission.
 *
 *		Whenever a NAK is received, our History Number should be reset to the last known  ACKed value,
 *		and that should be enough to force us to accumulate any of the NAKed item changelists.
 */

void FObjectReplicator::ReceivedNak( int32 NakPacketId )
{
	const UObject* Object = GetObject();

	if (Object == nullptr)
	{
		UE_LOG(LogNet, Verbose, TEXT("FObjectReplicator::ReceivedNak: Object == nullptr"));
		return;
	}
	else if (ObjectClass == nullptr)
	{
		UE_LOG(LogNet, Verbose, TEXT("FObjectReplicator::ReceivedNak: ObjectClass == nullptr"));
	}
	else if (ERepLayoutState::Normal == RepLayout->GetRepLayoutState())
	{
		if (FSendingRepState* SendingRepState = RepState.IsValid() ? RepState->GetSendingRepState() : nullptr)
		{
			// Go over properties tracked with histories, and mark them as needing to be resent.
			for (int32 i = SendingRepState->HistoryStart; i < SendingRepState->HistoryEnd; ++i)
			{
				const int32 HistoryIndex = i % FSendingRepState::MAX_CHANGE_HISTORY;

				FRepChangedHistory& HistoryItem = SendingRepState->ChangeHistory[HistoryIndex];

				if (!HistoryItem.Resend && HistoryItem.OutPacketIdRange.InRange(NakPacketId))
				{
					check(HistoryItem.Changed.Num() > 0);
					HistoryItem.Resend = true;
					++SendingRepState->NumNaks;
				}
			}

			// Go over our Custom Delta Properties and update their retirements
			for (int32 i = SendingRepState->Retirement.Num() - 1; i >= 0; i--)
			{
				FPropertyRetirement& Retirement = SendingRepState->Retirement[i];
				ValidateRetirementHistory(Retirement, Object);

				// If this is a dynamic array property, we have to look through the list of retirement records to see if we need to reset the base state
				FPropertyRetirement* Rec = Retirement.Next; // Retirement[i] is head and not actually used in this case
				while (Rec != nullptr)
				{
					if (NakPacketId > Rec->OutPacketIdRange.Last)
					{
						// We can assume this means this record's packet was ack'd, so we can get rid of the old state
						check(Retirement.Next == Rec);
						Retirement.Next = Rec->Next;
						delete Rec;
						Rec = Retirement.Next;
						continue;
					}
					else if (NakPacketId >= Rec->OutPacketIdRange.First && NakPacketId <= Rec->OutPacketIdRange.Last)
					{
						UE_LOG(LogNet, Verbose, TEXT("Restoring Previous Base State of dynamic property. Channel: %s, NakId: %d, First: %d, Last: %d, Address: %s)"), *OwningChannel->Describe(), NakPacketId, Rec->OutPacketIdRange.First, Rec->OutPacketIdRange.Last, *Connection->LowLevelGetRemoteAddress(true));

						// The Nack'd packet did update this property, so we need to replace the buffer in RecentDynamic
						// with the buffer we used to create this update (which was dropped), so that the update will be recreated on the next replicate actor
						SendingRepState->RecentCustomDeltaState[i] = Rec->DynamicState;

						// We can get rid of the rest of the saved off base states since we will be regenerating these updates on the next replicate actor
						while (Rec != nullptr)
						{
							FPropertyRetirement * DeleteNext = Rec->Next;
							delete Rec;
							Rec = DeleteNext;
						}

						// Finished
						Retirement.Next = nullptr;
						break;
					}
					Rec = Rec->Next;
				}

				ValidateRetirementHistory(Retirement, Object);
			}
		}
	}
}

bool FObjectReplicator::ReceivedBunch(FNetBitReader& Bunch, const FReplicationFlags& RepFlags, const bool bHasRepLayout, bool& bOutHasUnmapped)
{
	check(RepLayout);

	UObject* Object = GetObject();

	if (!Object)
	{
		UE_LOG(LogNet, Verbose, TEXT("ReceivedBunch: Object == nullptr"));
		return false;
	}

	UNetDriver* const ConnectionNetDriver = Connection->GetDriver();
	UPackageMap* const PackageMap = Connection->PackageMap;

	const bool bIsServer = ConnectionNetDriver->IsServer();
	const bool bCanDelayRPCs = (CVarDelayUnmappedRPCs.GetValueOnGameThread() > 0) && !bIsServer;

	const FClassNetCache* const ClassCache = ConnectionNetDriver->NetCache->GetClassNetCache(ObjectClass);

	if (!ClassCache)
	{
		UE_LOG(LogNet, Error, TEXT("ReceivedBunch: ClassCache == nullptr: %s"), *Object->GetFullName());
		return false;
	}

	const FRepLayout& LocalRepLayout = *RepLayout;
	bool bGuidsChanged = false;
	FReceivingRepState* ReceivingRepState = RepState->GetReceivingRepState();

	// Handle replayout properties
	if (bHasRepLayout)
	{
		// Server shouldn't receive properties.
		if (bIsServer)
		{
			UE_LOG(LogNet, Error, TEXT("Server received RepLayout properties: %s"), *Object->GetFullName());
			return false;
		}

		if (!bHasReplicatedProperties)
		{
			// Persistent, not reset until PostNetReceive is called
			bHasReplicatedProperties = true;
			PreNetReceive();
		}

		EReceivePropertiesFlags ReceivePropFlags = EReceivePropertiesFlags::None;

		if (ConnectionNetDriver->ShouldReceiveRepNotifiesForObject(Object))
		{
			ReceivePropFlags |= EReceivePropertiesFlags::RepNotifies;
		}

		if (RepFlags.bSkipRoleSwap)
		{
			ReceivePropFlags |= EReceivePropertiesFlags::SkipRoleSwap;
		}

		bool bLocalHasUnmapped = false;

		if (!LocalRepLayout.ReceiveProperties(OwningChannel, ObjectClass, RepState->GetReceivingRepState(), Object, Bunch, bLocalHasUnmapped, bGuidsChanged, ReceivePropFlags))
		{
			UE_LOG(LogRep, Error, TEXT( "RepLayout->ReceiveProperties FAILED: %s" ), *Object->GetFullName());
			return false;
		}

		bOutHasUnmapped |= bLocalHasUnmapped;
	}

	FNetFieldExportGroup* NetFieldExportGroup = OwningChannel->GetNetFieldExportGroupForClassNetCache(ObjectClass);

	FNetBitReader Reader( Bunch.PackageMap );

	// Read fields from stream
	const FFieldNetCache * FieldCache = nullptr;
	
	// TODO:	As of now, we replicate all of our Custom Delta Properties immediately after our normal properties.
	//			An optimization could be made here in the future if we replicated / received Custom Delta Properties in RepLayout
	//			immediately with normal properties.
	//
	//			For the Standard case, we expect the RepLayout to be identical on Client and Server.
	//				If the RepLayout doesn't have any Custom Delta Properties, everything stays as it is now.
	//				If the RepLayout does have Custom Delta Properties, then:
	//					1. We replicate a single bit indicating whether or not any were actually sent.
	//					2. We replicate a packed int specifying the number of custom delta properties (if any were sent).
	//					3. We replicate the Header and Payloads as normal.
	//				This may increase bandwidth slightly, but it's likely negligible.
	//
	//			For the Backwards Compatible path, we do the above, except we always send the bit flag, and the count when set.
	//				In that way, if Custom Delta Properties are added or removed, we can always rely on the bit field to try and
	//				read them, and then throw them away if they are incompatible.
	//
	//			In both described cases, we could remove the first cast to a struct property below, and flags checks on the properties
	//			as we could instead use the RepLayout cached command flags which would hopefully reduce cache misses.
	//			This also means that we could leverage the bIsServer and bHasReplicatedProperties that have already taken place.
	//
	//			If we want maintain compatibility with older builds (mostly for replays), we could leave the branch in here for now
	//			but short circuit it with a net version check, still allowing us to skip the cast in new versions.
	//
	//			This also becomes more convenient when we merge RepNotify handling.

	FNetSerializeCB NetSerializeCB(ConnectionNetDriver);

	// Read each property/function blob into Reader (so we've safely jumped over this data in the Bunch/stream at this point)
	while (OwningChannel->ReadFieldHeaderAndPayload(Object, ClassCache, NetFieldExportGroup, Bunch, &FieldCache, Reader))
	{
		if (UNLIKELY(Bunch.IsError()))
		{
			UE_LOG(LogNet, Error, TEXT("ReceivedBunch: Error reading field: %s"), *Object->GetFullName());
			return false;
		}

		else if (UNLIKELY(FieldCache == nullptr))
		{
			UE_LOG(LogNet, Warning, TEXT("ReceivedBunch: FieldCache == nullptr: %s"), *Object->GetFullName());
			continue;
		}

		else if (UNLIKELY(FieldCache->bIncompatible))
		{
			// We've already warned about this property once, so no need to continue to do so
			UE_LOG(LogNet, Verbose, TEXT( "ReceivedBunch: FieldCache->bIncompatible == true. Object: %s, Field: %s" ), *Object->GetFullName(), *FieldCache->Field->GetFName().ToString());
			continue;
		}

		// Handle property
		if (UStructProperty* ReplicatedProp = Cast<UStructProperty>(FieldCache->Field))
		{
			// Server shouldn't receive properties.
			if (bIsServer)
			{
				UE_LOG(LogNet, Error, TEXT("Server received unwanted property value %s in %s"), *ReplicatedProp->GetName(), *Object->GetFullName());
				return false;
			}

			// Call PreNetReceive if we haven't yet
			if (!bHasReplicatedProperties)
			{
				// Persistent, not reset until PostNetReceive is called
				bHasReplicatedProperties = true;
				PreNetReceive();
			}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			{
				FString DebugPropertyStr = CVarNetReplicationDebugProperty.GetValueOnAnyThread();
				if (!DebugPropertyStr.IsEmpty() && ReplicatedProp->GetName().Contains(DebugPropertyStr))
				{
					UE_LOG(LogRep, Log, TEXT("Replicating Property[%d] %s on %s"), ReplicatedProp->RepIndex, *ReplicatedProp->GetName(), *Object->GetName());
				}
			}
#endif

			FNetDeltaSerializeInfo Parms;
			Parms.Map = PackageMap;
			Parms.Reader = &Reader;
			Parms.NetSerializeCB = &NetSerializeCB;
			Parms.Connection = Connection;
			Parms.bInternalAck = Connection->InternalAck;
			Parms.Object = Object;

			if (!FNetSerializeCB::ReceiveCustomDeltaProperty(LocalRepLayout, ReceivingRepState, Parms, ReplicatedProp))
			{
				// Should have already logged the error.
				if (bIsServer)
				{
					return false;
				}

				FieldCache->bIncompatible = true;
				continue;
			}

			if (Parms.bOutHasMoreUnmapped)
			{
				bOutHasUnmapped = true;
			}

			if (Parms.bGuidListsChanged)
			{
				bGuidsChanged = true;
			}

			// Successfully received it.
			UE_LOG(LogRepTraffic, Log, TEXT(" %s - %s"), *Object->GetName(), *Parms.DebugName);
		}
		// Handle function call
		else if ( Cast< UFunction >( FieldCache->Field ) )
		{
			bool bDelayFunction = false;
			TSet<FNetworkGUID> UnmappedGuids;
			bool bSuccess = ReceivedRPC(Reader, RepFlags, FieldCache, bCanDelayRPCs, bDelayFunction, UnmappedGuids);

			if (!bSuccess)
			{
				return false;
			}
			else if (bDelayFunction)
			{
				// This invalidates Reader's buffer
				PendingLocalRPCs.Emplace(FieldCache, RepFlags, Reader, UnmappedGuids);
				bOutHasUnmapped = true;
				bGuidsChanged = true;
				bForceUpdateUnmapped = true;
			}
			else if (Object == nullptr || Object->IsPendingKill())
			{
				// replicated function destroyed Object
				return true;
			}
		}
		else
		{
			UE_LOG( LogRep, Error, TEXT( "ReceivedBunch: Invalid replicated field %i in %s" ), FieldCache->FieldNetIndex, *Object->GetFullName() );
			return false;
		}
	}
	
	// If guids changed, then rebuild acceleration tables
	if (bGuidsChanged)
	{
		UpdateGuidToReplicatorMap();
	}

	return true;
}

#define HANDLE_INCOMPATIBLE_RPC			\
	if ( bIsServer )					\
	{									\
		return false;					\
	}									\
	FieldCache->bIncompatible = true;	\
	return true;						\


bool GReceiveRPCTimingEnabled = false;
struct FScopedRPCTimingTracker
{
	FScopedRPCTimingTracker(UFunction* InFunction, UNetConnection* InConnection) : Connection(InConnection), Function(InFunction)
	{
		if (GReceiveRPCTimingEnabled)
		{
			StartTime = FPlatformTime::Seconds();
		}
	};

	~FScopedRPCTimingTracker()
	{
		if (GReceiveRPCTimingEnabled)
		{
			const double Elapsed = FPlatformTime::Seconds() - StartTime;
			Connection->Driver->NotifyRPCProcessed(Function, Connection, Elapsed);

		}
	}
	UNetConnection* Connection;
	UFunction* Function;
	double StartTime;
};

bool FObjectReplicator::ReceivedRPC(FNetBitReader& Reader, const FReplicationFlags& RepFlags, const FFieldNetCache* FieldCache, const bool bCanDelayRPC, bool& bOutDelayRPC, TSet<FNetworkGUID>& UnmappedGuids)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(HandleRPC);
	const bool bIsServer = Connection->Driver->IsServer();
	UObject* Object = GetObject();
	FName FunctionName = FieldCache->Field->GetFName();
	UFunction* Function = Object->FindFunction(FunctionName);

	FScopedRPCTimingTracker ScopedTracker(Function, Connection);
	SCOPE_CYCLE_COUNTER(STAT_NetReceiveRPC);
	SCOPE_CYCLE_UOBJECT(Function, Function);

	if (Function == nullptr)
	{
		UE_LOG(LogNet, Error, TEXT("ReceivedRPC: Function not found. Object: %s, Function: %s"), *Object->GetFullName(), *FunctionName.ToString());
		HANDLE_INCOMPATIBLE_RPC
	}

	if ((Function->FunctionFlags & FUNC_Net) == 0)
	{
		UE_LOG(LogRep, Error, TEXT("Rejected non RPC function. Object: %s, Function: %s"), *Object->GetFullName(), *FunctionName.ToString());
		HANDLE_INCOMPATIBLE_RPC
	}

	if ((Function->FunctionFlags & (bIsServer ? FUNC_NetServer : (FUNC_NetClient | FUNC_NetMulticast))) == 0)
	{
		UE_LOG(LogRep, Error, TEXT("Rejected RPC function due to access rights. Object: %s, Function: %s"), *Object->GetFullName(), *FunctionName.ToString());
		HANDLE_INCOMPATIBLE_RPC
	}

	UE_LOG(LogRepTraffic, Log, TEXT("      Received RPC: %s"), *FunctionName.ToString());

	// validate that the function is callable here
	// we are client or net owner and shouldn't be ignoring rpcs
	const bool bCanExecute = Connection->Driver->ShouldCallRemoteFunction(Object, Function, RepFlags);

	if (bCanExecute)
	{
		// Only delay if reliable and CVar is enabled
		bool bCanDelayUnmapped = bCanDelayRPC && Function->FunctionFlags & FUNC_NetReliable;

		// Get the parameters.
		FMemMark Mark(FMemStack::Get());
		uint8* Parms = new(FMemStack::Get(), MEM_Zeroed, Function->ParmsSize)uint8;

		// Use the replication layout to receive the rpc parameter values
		TSharedPtr<FRepLayout> FuncRepLayout = Connection->Driver->GetFunctionRepLayout(Function);

		FuncRepLayout->ReceivePropertiesForRPC(Object, Function, OwningChannel, Reader, Parms, UnmappedGuids);

		if (Reader.IsError())
		{
			UE_LOG(LogRep, Error, TEXT("ReceivedRPC: ReceivePropertiesForRPC - Reader.IsError() == true: Function: %s, Object: %s"), *FunctionName.ToString(), *Object->GetFullName());
			HANDLE_INCOMPATIBLE_RPC
		}

		if (Reader.GetBitsLeft() != 0)
		{
			UE_LOG(LogNet, Error, TEXT("ReceivedRPC: ReceivePropertiesForRPC - Mismatch read. Function: %s, Object: %s"), *FunctionName.ToString(), *Object->GetFullName());
			HANDLE_INCOMPATIBLE_RPC
		}

		RPC_ResetLastFailedReason();

		if (bCanDelayUnmapped && (UnmappedGuids.Num() > 0 || PendingLocalRPCs.Num() > 0))
		{
			// If this has unmapped guids or there are already some queued, add to queue
			bOutDelayRPC = true;	
		}
		else
		{
			AActor* OwningActor = OwningChannel->Actor;

			if (Connection->Driver->ShouldForwardFunction(OwningActor, Function, Parms))
			{
				FWorldContext* const Context = GEngine->GetWorldContextFromWorld(Connection->Driver->GetWorld());
				if (Context != nullptr)
				{
					UObject* const SubObject = Object != OwningChannel->Actor ? Object : nullptr;

					for (FNamedNetDriver& Driver : Context->ActiveNetDrivers)
					{
						if (Driver.NetDriver != nullptr && (Driver.NetDriver != Connection->Driver) && Driver.NetDriver->ShouldReplicateFunction(OwningActor, Function))
						{
							Driver.NetDriver->ProcessRemoteFunction(OwningActor, Function, Parms, nullptr, nullptr, SubObject);
						}
					}
				}
			}

			// Reset errors from replay driver
			RPC_ResetLastFailedReason();

			// Call the function.
			Object->ProcessEvent(Function, Parms);
		}

		// Destroy the parameters.
		// warning: highly dependent on UObject::ProcessEvent freeing of parms!
		for (TFieldIterator<UProperty> It(Function); It && (It->PropertyFlags & (CPF_Parm | CPF_ReturnParm)) == CPF_Parm; ++It)
		{
			It->DestroyValue_InContainer(Parms);
		}

		Mark.Pop();

		if (RPC_GetLastFailedReason() != nullptr)
		{
			UE_LOG(LogRep, Error, TEXT("ReceivedRPC: RPC_GetLastFailedReason: %s"), RPC_GetLastFailedReason());
			return false;
		}
	}
	else
	{
		UE_LOG(LogRep, Verbose, TEXT("Rejected unwanted function %s in %s"), *FunctionName.ToString(), *Object->GetFullName());
	}

	return true;
}

void FObjectReplicator::UpdateGuidToReplicatorMap()
{
	SCOPE_CYCLE_COUNTER(STAT_NetUpdateGuidToReplicatorMap);

	if (Connection->Driver->IsServer())
	{
		return;
	}

	TSet<FNetworkGUID> LocalReferencedGuids;
	int32 LocalTrackedGuidMemoryBytes = 0;

	check(RepLayout);

	const FRepLayout& LocalRepLayout = *RepLayout;

	// Gather guids on rep layout
	if (RepState.IsValid())
	{
		FNetSerializeCB NetSerializeCB(Connection->Driver);

		FNetDeltaSerializeInfo Parms;
		Parms.NetSerializeCB = &NetSerializeCB;
		Parms.GatherGuidReferences = &LocalReferencedGuids;
		Parms.TrackedGuidMemoryBytes = &LocalTrackedGuidMemoryBytes;
		Parms.Object = GetObject();
		Parms.bInternalAck = Connection->InternalAck;

		LocalRepLayout.GatherGuidReferences(RepState->GetReceivingRepState(), Parms, LocalReferencedGuids, LocalTrackedGuidMemoryBytes);
	}

	// Gather RPC guids
	for (const FRPCPendingLocalCall& PendingRPC : PendingLocalRPCs)
	{
		for (const FNetworkGUID& NetGuid : PendingRPC.UnmappedGuids)
		{
			LocalReferencedGuids.Add(NetGuid);

			LocalTrackedGuidMemoryBytes += PendingRPC.UnmappedGuids.GetAllocatedSize();
			LocalTrackedGuidMemoryBytes += PendingRPC.Buffer.Num();
		}
	}

	// Go over all referenced guids, and make sure we're tracking them in the GuidToReplicatorMap
	for (const FNetworkGUID& GUID : LocalReferencedGuids)
	{
		if (!ReferencedGuids.Contains(GUID))
		{
			Connection->Driver->GuidToReplicatorMap.FindOrAdd(GUID).Add(this);
		}
	}

	// Remove any guids that we were previously tracking but no longer should
	for (const FNetworkGUID& GUID : ReferencedGuids)
	{
		if (!LocalReferencedGuids.Contains(GUID))
		{
			TSet<FObjectReplicator*>& Replicators = Connection->Driver->GuidToReplicatorMap.FindChecked(GUID);

			Replicators.Remove(this);

			if (Replicators.Num() == 0)
			{
				Connection->Driver->GuidToReplicatorMap.Remove(GUID);
			}
		}
	}

	Connection->Driver->TotalTrackedGuidMemoryBytes -= TrackedGuidMemoryBytes;
	TrackedGuidMemoryBytes = LocalTrackedGuidMemoryBytes;
	Connection->Driver->TotalTrackedGuidMemoryBytes += TrackedGuidMemoryBytes;

	ReferencedGuids = MoveTemp(LocalReferencedGuids);
}

bool FObjectReplicator::MoveMappedObjectToUnmapped(const FNetworkGUID& GUID)
{
	check(RepLayout);
	const FRepLayout& LocalRepLayout = *RepLayout;

	FNetSerializeCB NetSerializeCB(Connection->Driver);
	FNetDeltaSerializeInfo Parms;
	Parms.Connection = Connection;
	Parms.bInternalAck = Connection->InternalAck;
	Parms.Map = Connection->PackageMap;
	Parms.Object = GetObject();
	Parms.NetSerializeCB = &NetSerializeCB;
	Parms.MoveGuidToUnmapped = &GUID;

	return LocalRepLayout.MoveMappedObjectToUnmapped(RepState->GetReceivingRepState(), Parms, GUID);
}

void FObjectReplicator::PostReceivedBunch()
{
	if ( GetObject() == nullptr )
	{
		UE_LOG(LogNet, Verbose, TEXT("PostReceivedBunch: Object == nullptr"));
		return;
	}

	// Call PostNetReceive
	const bool bIsServer = (OwningChannel->Connection->Driver->ServerConnection == nullptr);
	if (!bIsServer && bHasReplicatedProperties)
	{
		PostNetReceive();
		bHasReplicatedProperties = false;
	}

	// Call RepNotifies
	CallRepNotifies(true);
}

static FORCEINLINE FPropertyRetirement** UpdateAckedRetirements(
	FPropertyRetirement& Retire,
	const int32 OutAckPacketId,
	const UObject* Object)
{
	ValidateRetirementHistory(Retire, Object);

	FPropertyRetirement** Rec = &Retire.Next; // Note the first element is 'head' that we don't actually use

	while (*Rec != nullptr)
	{
		if (OutAckPacketId >= (*Rec)->OutPacketIdRange.Last)
		{
			UE_LOG(LogRepTraffic, Verbose, TEXT("Deleting Property Record (%d >= %d)"), OutAckPacketId, (*Rec)->OutPacketIdRange.Last);

			// They've ack'd this packet so we can ditch this record (easier to do it here than look for these every Ack)
			FPropertyRetirement* ToDelete = *Rec;
			check(Retire.Next == ToDelete); // This should only be able to happen to the first record in the list
			Retire.Next = ToDelete->Next;
			Rec = &Retire.Next;

			delete ToDelete;
			continue;
		}

		Rec = &(*Rec)->Next;
	}

	return Rec;
}

void FObjectReplicator::ReplicateCustomDeltaProperties( FNetBitWriter & Bunch, FReplicationFlags RepFlags )
{
	CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_NetReplicateCustomDeltaPropTime, CVarNetEnableDetailedScopeCounters.GetValueOnAnyThread() > 0);

	check(RepLayout);
	const FRepLayout& LocalRepLayout = *RepLayout;
	const int32 NumLifetimeCustomDeltaProperties = FNetSerializeCB::GetNumLifetimeCustomDeltaProperties(LocalRepLayout);

	if (NumLifetimeCustomDeltaProperties <= 0)
	{
		// No custom properties
		return;
	}

	// TODO: See comments in ReceivedBunch. This code should get merged into RepLayout, to help optimize
	//			the receiving end, and make things more consistent.

	UObject* Object = GetObject();
	FSendingRepState* SendingRepState = RepState->GetSendingRepState();

	check(Object);
	check(OwningChannel);
	check(Connection == OwningChannel->Connection);

	TArray<TSharedPtr<INetDeltaBaseState>>& UsingCustomDeltaStates =
		EResendAllDataState::None == Connection->ResendAllDataState ? SendingRepState->RecentCustomDeltaState :
		EResendAllDataState::SinceOpen == Connection->ResendAllDataState ? SendingRepState->CDOCustomDeltaState :
		SendingRepState->CheckpointCustomDeltaState;

	FNetSerializeCB::PreSendCustomDeltaProperties(LocalRepLayout, Object, Connection, *ChangelistMgr, UsingCustomDeltaStates);

	ON_SCOPE_EXIT
	{
		FNetSerializeCB::PostSendCustomDeltaProperties(LocalRepLayout, Object, Connection, *ChangelistMgr, UsingCustomDeltaStates);
	};

	// Initialize a map of which conditions are valid
	const TStaticBitArray<COND_Max> ConditionMap = FSendingRepState::BuildConditionMapFromRepFlags(RepFlags);

	// Make sure net field export group is registered
	FNetFieldExportGroup* NetFieldExportGroup = OwningChannel->GetOrCreateNetFieldExportGroupForClassNetCache( Object );

	FNetBitWriter TempBitWriter( Connection->PackageMap, 1024 );

	// Replicate those properties.
	for (uint16 CustomDeltaProperty = 0; CustomDeltaProperty < NumLifetimeCustomDeltaProperties; ++CustomDeltaProperty)
	{
		const ELifetimeCondition RepCondition = FNetSerializeCB::GetLifetimeCustomDeltaPropertyCondition(LocalRepLayout, CustomDeltaProperty);

		check(RepCondition >= 0 && RepCondition < COND_Max);

		if (!ConditionMap[RepCondition])
		{
			// We didn't pass the condition so don't replicate us
			continue;
		}

		UProperty* Property = FNetSerializeCB::GetLifetimeCustomDeltaProperty(LocalRepLayout, CustomDeltaProperty);

		// If this is a dynamic array, we do the delta here
		TSharedPtr<INetDeltaBaseState> NewState;

		TempBitWriter.Reset();

		TSharedPtr<INetDeltaBaseState>& OldState = UsingCustomDeltaStates[CustomDeltaProperty];

		if (Connection->ResendAllDataState != EResendAllDataState::None)
		{
			if (Connection->ResendAllDataState == EResendAllDataState::SinceCheckpoint)
			{
				if (!SendCustomDeltaProperty(Object, CustomDeltaProperty, TempBitWriter, NewState, OldState))
				{
					continue;
				}

				// update checkpoint with new state
				OldState = NewState;
			}
			else
			{
				// If we are resending data since open, we don't want to affect the current state of channel/replication, so just do the minimum and send the data, and return
				// In this case, we'll send all of the properties since the CDO, so use the initial CDO delta state
				if (!SendCustomDeltaProperty(Object, CustomDeltaProperty, TempBitWriter, NewState, OldState))
				{
					continue;
				}
			}

			// Write property header and payload to the bunch
			WritePropertyHeaderAndPayload(Object, Property, NetFieldExportGroup, Bunch, TempBitWriter);

			continue;
		}

		// Get info.
		FPropertyRetirement& Retire = SendingRepState->Retirement[CustomDeltaProperty];

		// Update Retirement records with this new state so we can handle packet drops.
		// LastNext will be pointer to the last "Next" pointer in the list (so pointer to a pointer)
		FPropertyRetirement** LastNext = UpdateAckedRetirements(Retire, Connection->OutAckPacketId, Object);

		check(LastNext != nullptr);
		check(*LastNext == nullptr);

		ValidateRetirementHistory(Retire, Object);

		//-----------------------------------------
		//	Do delta serialization on dynamic properties
		//-----------------------------------------
		const bool WroteSomething = SendCustomDeltaProperty(Object, CustomDeltaProperty, TempBitWriter, NewState, OldState);

		if ( !WroteSomething )
		{
			continue;
		}

		*LastNext = new FPropertyRetirement();

		// Remember what the old state was at this point in time.  If we get a nak, we will need to revert back to this.
		(*LastNext)->DynamicState = OldState;		

		// Save NewState into the RecentCustomDeltaState array (old state is a reference into our RecentCustomDeltaState map)
		OldState = NewState; 

		// Write property header and payload to the bunch
		WritePropertyHeaderAndPayload(Object, Property, NetFieldExportGroup, Bunch, TempBitWriter);

		NETWORK_PROFILER(GNetworkProfiler.TrackReplicateProperty(Property, TempBitWriter.GetNumBits(), Connection));
	}
}

/** Replicates properties to the Bunch. Returns true if it wrote anything */
bool FObjectReplicator::ReplicateProperties( FOutBunch & Bunch, FReplicationFlags RepFlags )
{
	UObject* Object = GetObject();

	if ( Object == nullptr )
	{
		UE_LOG(LogRep, Verbose, TEXT("ReplicateProperties: Object == nullptr"));
		return false;
	}

	// some games ship checks() in Shipping so we cannot rely on DO_CHECK here, and these checks are in an extremely hot path
	if (!UE_BUILD_SHIPPING && !UE_BUILD_TEST)
	{
		check(OwningChannel);
		check(RepLayout.IsValid());
		check(RepState.IsValid());
		check(RepState->GetSendingRepState());
		check(RepLayout->GetRepLayoutState() != ERepLayoutState::Uninitialized);
		check(ChangelistMgr.IsValid());
		check(ChangelistMgr->GetRepChangelistState() != nullptr);
		check((ChangelistMgr->GetRepChangelistState()->StaticBuffer.Num() == 0) == (RepLayout->GetRepLayoutState() == ERepLayoutState::Empty));
	}

	UNetConnection* OwningChannelConnection = OwningChannel->Connection;

	FNetBitWriter Writer( Bunch.PackageMap, 8192 );

	// TODO: Maybe ReplicateProperties could just take the RepState, Changelist Manger, Writer, and OwningChannel
	//		and all the work could just be done in a single place.

	// Update change list (this will re-use work done by previous connections)
	FSendingRepState* SendingRepState = ((Connection->ResendAllDataState == EResendAllDataState::SinceCheckpoint) && CheckpointRepState.IsValid()) ? CheckpointRepState->GetSendingRepState() : RepState->GetSendingRepState();
	FNetSerializeCB::UpdateChangelistMgr(*RepLayout, SendingRepState, *ChangelistMgr, Object, Connection->Driver->ReplicationFrame, RepFlags, OwningChannel->bForceCompareProperties);

	// Replicate properties in the layout
	const bool bHasRepLayout = RepLayout->ReplicateProperties(SendingRepState, ChangelistMgr->GetRepChangelistState(), (uint8*)Object, ObjectClass, OwningChannel, Writer, RepFlags);

	// Replicate all the custom delta properties (fast arrays, etc)
	ReplicateCustomDeltaProperties(Writer, RepFlags);

	if ( Connection->ResendAllDataState != EResendAllDataState::None )
	{
		// If we are resending data since open, we don't want to affect the current state of channel/replication, so just send the data, and return
		const bool WroteImportantData = Writer.GetNumBits() != 0;

		if ( WroteImportantData )
		{
			OwningChannel->WriteContentBlockPayload( Object, Bunch, bHasRepLayout, Writer );

			if (Connection->ResendAllDataState == EResendAllDataState::SinceCheckpoint)
			{
				UpdateCheckpoint();
			}

			return true;
		}

		return false;
	}

	// LastUpdateEmpty - this is done before dequeing the multicasted unreliable functions on purpose as they should not prevent
	// an actor channel from going dormant.
	bLastUpdateEmpty = Writer.GetNumBits() == 0;

	// Replicate Queued (unreliable functions)
	if ( RemoteFunctions != nullptr && RemoteFunctions->GetNumBits() > 0 )
	{
		if ( UNLIKELY(GNetRPCDebug == 1) )
		{
			UE_LOG( LogRepTraffic, Warning,	TEXT("      Sending queued RPCs: %s. Channel[%d] [%.1f bytes]"), *Object->GetName(), OwningChannel->ChIndex, RemoteFunctions->GetNumBits() / 8.f );
		}

		Writer.SerializeBits( RemoteFunctions->GetData(), RemoteFunctions->GetNumBits() );
		RemoteFunctions->Reset();
		RemoteFuncInfo.Empty();

		NETWORK_PROFILER(GNetworkProfiler.FlushQueuedRPCs(OwningChannelConnection, Object));
	}

	// See if we wrote something important (anything but the 'end' int below).
	// Note that queued unreliable functions are considered important (WroteImportantData) but not for bLastUpdateEmpty. LastUpdateEmpty
	// is used for dormancy purposes. WroteImportantData is for determining if we should not include a component in replication.
	const bool WroteImportantData = Writer.GetNumBits() != 0;

	if ( WroteImportantData )
	{
		OwningChannel->WriteContentBlockPayload( Object, Bunch, bHasRepLayout, Writer );
	}

	return WroteImportantData;
}

void FObjectReplicator::ForceRefreshUnreliableProperties()
{
	if (GetObject() == nullptr)
	{
		UE_LOG(LogRep, Verbose, TEXT("ForceRefreshUnreliableProperties: Object == nullptr"));
		return;
	}

	check(!bOpenAckCalled);

	if (FSendingRepState* SendingRepState = RepState->GetSendingRepState())
	{
		SendingRepState->bOpenAckedCalled = true;
	}

	bOpenAckCalled = true;
}

void FObjectReplicator::PostSendBunch( FPacketIdRange & PacketRange, uint8 bReliable )
{
	const UObject* Object = GetObject();

	if ( Object == nullptr )
	{
		UE_LOG(LogNet, Verbose, TEXT("PostSendBunch: Object == nullptr"));
		return;
	}

	check(RepLayout);

	// Don't update retirement records for reliable properties. This is ok to do only if we also pause replication on the channel until the acks have gone through.
	bool SkipRetirementUpdate = OwningChannel->bPausedUntilReliableACK;

	const FRepLayout& LocalRepLayout = *RepLayout;

	if (FSendingRepState* SendingRepState = RepState.IsValid() ? RepState->GetSendingRepState() : nullptr)
	{
		if (!SkipRetirementUpdate)
		{
			// Don't call if reliable, since the bunch will be resent. We dont want this to end up in the changelist history
			// But is that enough? How does it know to delta against this latest state?

			for (int32 i = SendingRepState->HistoryStart; i < SendingRepState->HistoryEnd; ++i)
			{
				const int32 HistoryIndex = i % FSendingRepState::MAX_CHANGE_HISTORY;

				FRepChangedHistory & HistoryItem = SendingRepState->ChangeHistory[HistoryIndex];

				if (HistoryItem.OutPacketIdRange.First == INDEX_NONE)
				{
					check(HistoryItem.Changed.Num() > 0);
					check(!HistoryItem.Resend);

					HistoryItem.OutPacketIdRange = PacketRange;

					if (!bReliable && !SendingRepState->bOpenAckedCalled)
					{
						SendingRepState->PreOpenAckHistory.Add(HistoryItem);
					}
				}
			}
		}

		for (FPropertyRetirement& Retirement : SendingRepState->Retirement)
		{
			FPropertyRetirement* Next = Retirement.Next;
			FPropertyRetirement* Prev = &Retirement;

			while (Next != nullptr)
			{
				// This is updating the dynamic properties retirement record that was created above during property replication
				// (we have to wait until we actually send the bunch to know the packetID, which is why we look for .First==INDEX_NONE)
				if (Next->OutPacketIdRange.First == INDEX_NONE)
				{
					if (!SkipRetirementUpdate)
					{
						Next->OutPacketIdRange = PacketRange;

						// Mark the last time on this retirement slot that a property actually changed
						Retirement.OutPacketIdRange = PacketRange;
					}
					else
					{
						// We need to remove the retirement entry here!
						Prev->Next = Next->Next;
						delete Next;
						Next = Prev;
					}
				}

				Prev = Next;
				Next = Next->Next;
			}

			ValidateRetirementHistory(Retirement, Object);
		}
	}
}

void FObjectReplicator::FRPCPendingLocalCall::CountBytes(FArchive& Ar) const
{
	Buffer.CountBytes(Ar);
	UnmappedGuids.CountBytes(Ar);
}


void FObjectReplicator::Serialize(FArchive& Ar)
{
	if (Ar.IsCountingMemory())
	{
		CountBytes(Ar);
	}
}

void FObjectReplicator::CountBytes(FArchive& Ar) const
{
	GRANULAR_NETWORK_MEMORY_TRACKING_INIT(Ar, "FObjectReplicator::CountBytes");

	// FObjectReplicator has a shared pointer to an FRepLayout, but since it's shared with
	// the UNetDriver, the memory isn't tracked here.

	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("RepState",
		if (RepState.IsValid())
		{
			const SIZE_T SizeOfRepState = sizeof(FRepState);
			Ar.CountBytes(SizeOfRepState, SizeOfRepState);
			RepState->CountBytes(Ar);
		}
	);

	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("ReferencedGuids", ReferencedGuids.CountBytes(Ar));

	// ChangelistMgr points to a ReplicationChangelistMgr managed by the UNetDriver, so it's not tracked here

	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("RemoveFuncInfo",
		RemoteFuncInfo.CountBytes(Ar);
		if (RemoteFunctions)
		{
			RemoteFunctions->CountMemory(Ar);
		}
	);

	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("PendingLocalRPCs",
		PendingLocalRPCs.CountBytes(Ar);
		for (const FRPCPendingLocalCall& PendingRPC : PendingLocalRPCs)
		{
			PendingRPC.Buffer.CountBytes(Ar);
			PendingRPC.UnmappedGuids.CountBytes(Ar);
		}
	);
}

void FObjectReplicator::QueueRemoteFunctionBunch( UFunction* Func, FOutBunch &Bunch )
{
	if (Connection == nullptr)
	{
		return;
	}

	// This is a pretty basic throttling method - just don't let same func be called more than
	// twice in one network update period.
	//
	// Long term we want to have priorities and stronger cross channel traffic management that
	// can handle this better
	int32 InfoIdx = INDEX_NONE;
	for (int32 i = 0; i < RemoteFuncInfo.Num(); ++i)
	{
		if (RemoteFuncInfo[i].FuncName == Func->GetFName())
		{
			InfoIdx = i;
			break;
		}
	}

	if (InfoIdx == INDEX_NONE)
	{
		InfoIdx = RemoteFuncInfo.AddUninitialized();
		RemoteFuncInfo[InfoIdx].FuncName = Func->GetFName();
		RemoteFuncInfo[InfoIdx].Calls = 0;
	}
	
	if (++RemoteFuncInfo[InfoIdx].Calls > CVarMaxRPCPerNetUpdate.GetValueOnAnyThread())
	{
		UE_LOG(LogRep, Verbose, TEXT("Too many calls (%d) to RPC %s within a single netupdate. Skipping. %s.  LastCallTime: %.2f. CurrentTime: %.2f. LastRelevantTime: %.2f. LastUpdateTime: %.2f "),
			RemoteFuncInfo[InfoIdx].Calls, *Func->GetName(), *GetPathNameSafe(GetObject()), RemoteFuncInfo[InfoIdx].LastCallTime, OwningChannel->Connection->Driver->Time, OwningChannel->RelevantTime, OwningChannel->LastUpdateTime);

		// The MustBeMappedGuids can just be dropped, because we aren't actually going to send a bunch. If we don't clear it, then we will get warnings when the next channel tries to replicate
		CastChecked<UPackageMapClient>(Connection->PackageMap)->GetMustBeMappedGuidsInLastBunch().Reset();
		return;
	}
	
	RemoteFuncInfo[InfoIdx].LastCallTime = OwningChannel->Connection->Driver->Time;

	if (RemoteFunctions == nullptr)
	{
		RemoteFunctions = new FOutBunch(OwningChannel, 0);
	}

	RemoteFunctions->SerializeBits(Bunch.GetData(), Bunch.GetNumBits());

	if (Connection->PackageMap != nullptr)
	{
		UPackageMapClient* PackageMapClient = CastChecked<UPackageMapClient>(Connection->PackageMap);

		// We need to copy over any info that was obtained on the package map during serialization, and remember it until we actually call SendBunch
		if (PackageMapClient->GetMustBeMappedGuidsInLastBunch().Num())
		{
			OwningChannel->QueuedMustBeMappedGuidsInLastBunch.Append(PackageMapClient->GetMustBeMappedGuidsInLastBunch());
			PackageMapClient->GetMustBeMappedGuidsInLastBunch().Reset();
		}

		if (!Connection->InternalAck)
		{
			// Copy over any exported bunches
			PackageMapClient->AppendExportBunches(OwningChannel->QueuedExportBunches);
		}
	}
}

bool FObjectReplicator::ReadyForDormancy(bool bSuppressLogs)
{
	if (GetObject() == nullptr)
	{
		UE_LOG(LogRep, Verbose, TEXT("ReadyForDormancy: Object == nullptr"));
		return true;		// Technically, we don't want to hold up dormancy, but the owner needs to clean us up, so we warn
	}

	// Can't go dormant until last update produced no new property updates
	if (!bLastUpdateEmpty)
	{
		if (!bSuppressLogs)
		{
			UE_LOG(LogRepTraffic, Verbose, TEXT("    [%d] Not ready for dormancy. bLastUpdateEmpty = false"), OwningChannel->ChIndex);
		}

		return false;
	}

	if (FSendingRepState* SendingRepState = RepState.IsValid() ? RepState->GetSendingRepState() : nullptr)
	{
		if (SendingRepState->HistoryStart != SendingRepState->HistoryEnd)
		{
			// We have change lists that haven't been acked
			return false;
		}

		if (SendingRepState->NumNaks > 0)
		{
			return false;
		}

		if (!SendingRepState->bOpenAckedCalled)
		{
			return false;
		}

		if (SendingRepState->PreOpenAckHistory.Num() > 0)
		{
			return false;
		}

		// Can't go dormant if there are unAckd property updates
		for (FPropertyRetirement& Retirement : SendingRepState->Retirement)
		{
			if (Retirement.Next != nullptr)
			{
				if (!bSuppressLogs)
				{
					UE_LOG(LogRepTraffic, Verbose, TEXT("    [%d] OutAckPacketId: %d First: %d Last: %d "), OwningChannel->ChIndex, OwningChannel->Connection->OutAckPacketId, Retirement.OutPacketIdRange.First, Retirement.OutPacketIdRange.Last);
				}
				return false;
			}
		}
	}

	return true;
}

void FObjectReplicator::StartBecomingDormant()
{
	if ( GetObject() == nullptr )
	{
		UE_LOG( LogRep, Verbose, TEXT( "StartBecomingDormant: Object == nullptr" ) );
		return;
	}

	bLastUpdateEmpty = false; // Ensure we get one more attempt to update properties
}

void FObjectReplicator::CallRepNotifies(bool bSkipIfChannelHasQueuedBunches)
{
	// This logic is mostly a copy of FRepLayout::CallRepNotifies, and they should be merged.

	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RepNotifies);
	UObject* Object = GetObject();

	if (!Object || Object->IsPendingKill())
	{
		return;
	}

	if (Connection && Connection->Driver && Connection->Driver->ShouldSkipRepNotifies())
	{
		return;
	}

	if (bSkipIfChannelHasQueuedBunches && (OwningChannel && OwningChannel->QueuedBunches.Num() > 0))
	{
		return;
	}

	FReceivingRepState* ReceivingRepState = RepState->GetReceivingRepState();
	RepLayout->CallRepNotifies(ReceivingRepState, Object);

	if (!Object->IsPendingKill())
	{
		Object->PostRepNotifies();
	}
}

void FObjectReplicator::UpdateUnmappedObjects(bool & bOutHasMoreUnmapped)
{
	UObject* Object = GetObject();
	
	if (!Object || Object->IsPendingKill())
	{
		bOutHasMoreUnmapped = false;
		return;
	}

	if (Connection->State == USOCK_Closed)
	{
		UE_LOG(LogNet, Verbose, TEXT("FObjectReplicator::UpdateUnmappedObjects: Connection->State == USOCK_Closed"));
		return;
	}

	// Since RepNotifies aren't processed while a channel has queued bunches, don't assert in that case.
	FReceivingRepState* ReceivingRepState = RepState->GetReceivingRepState();
	const bool bHasQueuedBunches = OwningChannel && OwningChannel->QueuedBunches.Num() > 0;

	checkf(bHasQueuedBunches || ReceivingRepState->RepNotifies.Num() == 0,
		TEXT("Failed RepState RepNotifies check. Num=%d. Object=%s. Channel QueuedBunches=%d"),
		ReceivingRepState->RepNotifies.Num(), *Object->GetFullName(), OwningChannel ? OwningChannel->QueuedBunches.Num() : 0);

	bool bCalledPreNetReceive = false;
	bool bSomeObjectsWereMapped = false;

	check(RepLayout);

	const FRepLayout& LocalRepLayout = *RepLayout;

	FNetSerializeCB NetSerializeCB(Connection->Driver);

	FNetDeltaSerializeInfo Parms;
	Parms.Object = Object;
	Parms.Connection = Connection;
	Parms.bInternalAck = Connection->InternalAck;
	Parms.Map = Connection->PackageMap;
	Parms.NetSerializeCB = &NetSerializeCB;

	Parms.bUpdateUnmappedObjects = true;

	// Let the rep layout update any unmapped properties
	LocalRepLayout.UpdateUnmappedObjects(ReceivingRepState, Connection->PackageMap, Object, Parms, bCalledPreNetReceive, bSomeObjectsWereMapped, bOutHasMoreUnmapped);

	bSomeObjectsWereMapped |= Parms.bOutSomeObjectsWereMapped;
	bOutHasMoreUnmapped |= Parms.bOutHasMoreUnmapped;
	bCalledPreNetReceive |= Parms.bCalledPreNetReceive;

	if (bCalledPreNetReceive)
	{
		// If we mapped some objects, make sure to call PostNetReceive (some game code will need to think this was actually replicated to work)
		PostNetReceive();

		UpdateGuidToReplicatorMap();
	}

	// Call any rep notifies that need to happen when object pointers change
	// Pass in false to override the check for queued bunches. Otherwise, if the owning channel has queued bunches,
	// the RepNotifies will remain in the list and the check for 0 RepNotifies above will fail next time.
	CallRepNotifies(false);

	UPackageMapClient* PackageMapClient = Cast<UPackageMapClient>(Connection->PackageMap);

	if (PackageMapClient && OwningChannel)
	{
		const bool bIsServer = Connection->Driver->IsServer();
		const FClassNetCache* ClassCache = Connection->Driver->NetCache->GetClassNetCache(ObjectClass);

		// Handle pending RPCs, in order
		for (int32 RPCIndex = 0; RPCIndex < PendingLocalRPCs.Num(); RPCIndex++)
		{
			FRPCPendingLocalCall& Pending = PendingLocalRPCs[RPCIndex];
			const FFieldNetCache* FieldCache = ClassCache->GetFromIndex(Pending.RPCFieldIndex);

			FNetBitReader Reader(Connection->PackageMap, Pending.Buffer.GetData(), Pending.NumBits);

			bool bIsGuidPending = false;

			for (const FNetworkGUID& Guid : Pending.UnmappedGuids)
			{
				if (PackageMapClient->IsGUIDPending(Guid))
				{ 
					bIsGuidPending = true;
					break;
				}
			}

			TSet<FNetworkGUID> UnmappedGuids;
			bool bCanDelayRPCs = bIsGuidPending; // Force execute if none of our RPC guids are pending, even if other guids are. This is more consistent behavior as it is less dependent on unrelated actors
			bool bFunctionWasUnmapped = false;
			bool bSuccess = true;
			FString FunctionName = TEXT("(Unknown)");
			
			if (FieldCache == nullptr)
			{
				UE_LOG(LogNet, Warning, TEXT("FObjectReplicator::UpdateUnmappedObjects: FieldCache not found. Object: %s"), *Object->GetFullName());
				bSuccess = false;
			}
			else
			{
				FunctionName = FieldCache->Field->GetName();
				bSuccess = ReceivedRPC(Reader, Pending.RepFlags, FieldCache, bCanDelayRPCs, bFunctionWasUnmapped, UnmappedGuids);
			}

			if (!bSuccess)
			{
				if (bIsServer && !Connection->InternalAck)
				{
					// Close our connection and abort rpcs as things are invalid
					PendingLocalRPCs.Empty();
					bOutHasMoreUnmapped = false;

					UE_LOG(LogNet, Error, TEXT("FObjectReplicator::UpdateUnmappedObjects: Failed executing delayed RPC %s on Object %s, closing connection!"), *FunctionName, *Object->GetFullName());

					Connection->Close();
					return;
				}
				else
				{
					UE_LOG(LogNet, Warning, TEXT("FObjectReplicator::UpdateUnmappedObjects: Failed executing delayed RPC %s on Object %s, skipping RPC!"), *FunctionName, *Object->GetFullName());

					// Skip this RPC, it was marked invalid internally
					PendingLocalRPCs.RemoveAt(RPCIndex);
					RPCIndex--;
				}
			}
			else if (bFunctionWasUnmapped)
			{
				// Still unmapped, update unmapped list
				Pending.UnmappedGuids = UnmappedGuids;
				bOutHasMoreUnmapped = true;
				
				break;
			}
			else
			{
				// We executed, remove this one and continue;
				PendingLocalRPCs.RemoveAt(RPCIndex);
				RPCIndex--;
			}
		}
	}
}

void FObjectReplicator::QueuePropertyRepNotify(
	UObject* Object,
	UProperty * Property,
	const int32 ElementIndex,
	TArray<uint8>& MetaData)
{
	if (!Property->HasAnyPropertyFlags(CPF_RepNotify))
	{
		return;
	}

	FReceivingRepState* ReceivingRepState = RepState.IsValid() ? RepState->GetReceivingRepState() : nullptr;
	if (ensureMsgf(ReceivingRepState, TEXT("FObjectReplicator::QueuePropertyRepNotifiy: No receiving RepState. Object=%s, Property=%s"),
		*GetPathNameSafe(Object), *Property->GetName()))
	{
		//@note: AddUniqueItem() here for static arrays since RepNotify() currently doesn't indicate index,
		//			so reporting the same property multiple times is not useful and wastes CPU
		//			were that changed, this should go back to AddItem() for efficiency
		// @todo UE4 - not checking if replicated value is changed from old.  Either fix or document, as may get multiple repnotifies of unacked properties.
		ReceivingRepState->RepNotifies.AddUnique(Property);

		UFunction* RepNotifyFunc = Object->FindFunctionChecked(Property->RepNotifyFunc);

		if (RepNotifyFunc->NumParms > 0)
		{
			if (Property->ArrayDim != 1)
			{
				// For static arrays, we build the meta data here, but adding the Element index that was just read into the PropMetaData array.
				UE_LOG(LogRepTraffic, Verbose, TEXT("Property %s had ArrayDim: %d change"), *Property->GetName(), ElementIndex);

				// Property is multi dimensional, keep track of what elements changed
				TArray< uint8 > & PropMetaData = ReceivingRepState->RepNotifyMetaData.FindOrAdd(Property);
				PropMetaData.Add(ElementIndex);
			}
			else if (MetaData.Num() > 0)
			{
				// For other properties (TArrays only now) the MetaData array is build within ::NetSerialize. Just add it to the RepNotifyMetaData map here.

				//UE_LOG(LogRepTraffic, Verbose, TEXT("Property %s had MetaData: "), *Property->GetName() );
				//for (auto MetaIt = MetaData.CreateIterator(); MetaIt; ++MetaIt)
				//	UE_LOG(LogRepTraffic, Verbose, TEXT("   %d"), *MetaIt );

				// Property included some meta data about what was serialized. 
				TArray< uint8 > & PropMetaData = ReceivingRepState->RepNotifyMetaData.FindOrAdd(Property);
				PropMetaData = MetaData;
			}
		}
	}
}

void FObjectReplicator::WritePropertyHeaderAndPayload(
	UObject* Object,
	UProperty* Property,
	FNetFieldExportGroup* NetFieldExportGroup,
	FNetBitWriter& Bunch,
	FNetBitWriter& Payload) const
{
	// Get class network info cache.
	const FClassNetCache* ClassCache = Connection->Driver->NetCache->GetClassNetCache(ObjectClass);

	check(ClassCache);

	// Get the network friend property index to replicate
	const FFieldNetCache * FieldCache = ClassCache->GetFromField(Property);

	checkSlow(FieldCache);

	// Send property name and optional array index.
	check(FieldCache->FieldNetIndex <= ClassCache->GetMaxIndex());

	// WriteFieldHeaderAndPayload will return the total number of bits written.
	// So, we subtract out the Payload size to get the actual number of header bits.
	const int32 HeaderBits = static_cast<int64>(OwningChannel->WriteFieldHeaderAndPayload(Bunch, ClassCache, FieldCache, NetFieldExportGroup, Payload)) - Payload.GetNumBits();

	NETWORK_PROFILER(GNetworkProfiler.TrackWritePropertyHeader(Property, HeaderBits, nullptr));
}

void FObjectReplicator::UpdateCheckpoint()
{
	TArray<uint16> CheckpointChangelist;

	if (CheckpointRepState.IsValid())
	{
		CheckpointChangelist = MoveTemp(CheckpointRepState->GetSendingRepState()->LifetimeChangelist);
	}
	else
	{
		CheckpointChangelist = RepState->GetSendingRepState()->LifetimeChangelist;
	}

	// Update rep state
	TSharedPtr<FRepChangedPropertyTracker> RepChangedPropertyTracker = Connection->Driver->FindOrCreateRepChangedPropertyTracker( GetObject() );

	CheckpointRepState = RepLayout->CreateRepState((const uint8*)GetObject(), RepChangedPropertyTracker, ECreateRepStateFlags::SkipCreateReceivingState);

	// Keep current set of changed properties
	CheckpointRepState->GetSendingRepState()->LifetimeChangelist = MoveTemp(CheckpointChangelist);
}

FScopedActorRoleSwap::FScopedActorRoleSwap(AActor* InActor)
	: Actor(InActor)
{
	const bool bShouldSwapRoles = Actor != nullptr && Actor->GetRemoteRole() == ROLE_Authority;

	if (bShouldSwapRoles)
	{
		Actor->SwapRoles();
	}
	else
	{
		Actor = nullptr;
	}
}

FScopedActorRoleSwap::~FScopedActorRoleSwap()
{
	if (Actor != nullptr)
	{
		Actor->SwapRoles();
	}
}
