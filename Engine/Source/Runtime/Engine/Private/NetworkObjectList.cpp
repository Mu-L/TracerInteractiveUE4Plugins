// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Engine/NetworkObjectList.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "EngineUtils.h"
#include "Serialization/Archive.h"

void FNetworkObjectList::AddInitialObjects(UWorld* const World, const FName NetDriverName)
{
	AddInitialObjects(World, GEngine->FindNamedNetDriver(World, NetDriverName));
}

void FNetworkObjectList::AddInitialObjects(UWorld* const World, UNetDriver* NetDriver)
{
	if (World == nullptr || NetDriver == nullptr)
	{
		return;
	}

	for (FActorIterator Iter(World); Iter; ++Iter)
	{
		AActor* Actor = *Iter;
		if (Actor != nullptr && !Actor->IsPendingKill() && ULevel::IsNetActor(Actor))
		{
			FindOrAdd(Actor, NetDriver);
		}
	}
}

TSharedPtr<FNetworkObjectInfo> FNetworkObjectList::Find(AActor* const Actor)
{
	if (Actor == nullptr)
	{
		return nullptr;
	}

	if (TSharedPtr<FNetworkObjectInfo>* InfoPtr = AllNetworkObjects.Find(Actor))
	{
		return *InfoPtr;
	}

	return TSharedPtr<FNetworkObjectInfo>();
}

TSharedPtr<FNetworkObjectInfo>* FNetworkObjectList::FindOrAdd(AActor* const Actor, const FName NetDriverName, bool* OutWasAdded)
{
	if (Actor != nullptr)
	{
		return FindOrAdd(Actor, GEngine->FindNamedNetDriver(Actor->GetWorld(), NetDriverName), OutWasAdded);
	}
	else
	{
		return nullptr;
	}
}

TSharedPtr<FNetworkObjectInfo>* FNetworkObjectList::FindOrAdd(AActor* const Actor, UNetDriver* NetDriver, bool* OutWasAdded)
{
	if (Actor == nullptr || Actor->IsPendingKill() ||

		// This implies the actor was added either added sometime during UWorld::DestroyActor,
		// or was potentially previously destroyed (and its Index points to a diferent, non-PendingKill object).
		!ensureAlwaysMsgf(!Actor->IsActorBeingDestroyed(), TEXT("Attempting to add an actor that's being destroyed to the NetworkObjectList Actor=%s NetDriverName=%s"), *Actor->GetPathName(), NetDriver ? *NetDriver->NetDriverName.ToString() : TEXT("None")))
	{
		return nullptr;
	}

	TSharedPtr<FNetworkObjectInfo>* NetworkObjectInfo = AllNetworkObjects.Find(Actor);
	
	if (NetworkObjectInfo == nullptr)
	{
		// We do a name check here so we don't add an actor to a network list that it shouldn't belong to
		if (NetDriver && NetDriver->ShouldReplicateActor(Actor))
		{
			NetworkObjectInfo = &AllNetworkObjects[AllNetworkObjects.Emplace(new FNetworkObjectInfo(Actor))];
			ActiveNetworkObjects.Add(*NetworkObjectInfo);

			UE_LOG(LogNetDormancy, VeryVerbose, TEXT("FNetworkObjectList::Add: Adding actor. Actor: %s, Total: %i, Active: %i, NetDriverName: %s"), *Actor->GetName(), AllNetworkObjects.Num(), ActiveNetworkObjects.Num(), *NetDriver->NetDriverName.ToString());

			if (OutWasAdded)
			{
				*OutWasAdded = true;
			}
		}
	}
	else
	{
		UE_LOG(LogNetDormancy, VeryVerbose, TEXT("FNetworkObjectList::Add: Already contained. Actor: %s, Total: %i, Active: %i, NetDriverName: %s"), *Actor->GetName(), AllNetworkObjects.Num(), ActiveNetworkObjects.Num(), NetDriver ? *NetDriver->NetDriverName.ToString() : TEXT("None"));
		if (OutWasAdded)
		{
			*OutWasAdded = false;
		}
	}
	
	check((ActiveNetworkObjects.Num() + ObjectsDormantOnAllConnections.Num()) == AllNetworkObjects.Num());

	return NetworkObjectInfo;
}

void FNetworkObjectList::Remove(AActor* const Actor)
{
	if (Actor == nullptr)
	{
		return;
	}

	TSharedPtr<FNetworkObjectInfo>* NetworkObjectInfoPtr = AllNetworkObjects.Find(Actor);

	if (NetworkObjectInfoPtr == nullptr)
	{
		// Sanity check that we're not on the other lists either
		check(!ActiveNetworkObjects.Contains(Actor));
		check(!ObjectsDormantOnAllConnections.Contains(Actor));
		check((ActiveNetworkObjects.Num() + ObjectsDormantOnAllConnections.Num()) == AllNetworkObjects.Num());
		return;
	}

	FNetworkObjectInfo* NetworkObjectInfo = NetworkObjectInfoPtr->Get();

	// Lower the dormant object count for each connection this object is dormant on
	for (auto ConnectionIt = NetworkObjectInfo->DormantConnections.CreateIterator(); ConnectionIt; ++ConnectionIt)
	{
		UNetConnection* Connection = (*ConnectionIt).Get();

		if (Connection == nullptr || Connection->State == USOCK_Closed)
		{
			ConnectionIt.RemoveCurrent();
			continue;
		}

		int32& NumDormantObjectsPerConnectionRef = NumDormantObjectsPerConnection.FindOrAdd(Connection);
		check( NumDormantObjectsPerConnectionRef > 0 );
		NumDormantObjectsPerConnectionRef--;
	}

	// Remove this object from all lists
	AllNetworkObjects.Remove(Actor);
	ActiveNetworkObjects.Remove(Actor);
	ObjectsDormantOnAllConnections.Remove(Actor);

	check((ActiveNetworkObjects.Num() + ObjectsDormantOnAllConnections.Num()) == AllNetworkObjects.Num());
}

void FNetworkObjectList::MarkDormant(AActor* const Actor, UNetConnection* const Connection, const int32 NumConnections, const FName NetDriverName)
{
	if (Actor)
	{
		MarkDormant(Actor, Connection, NumConnections, GEngine->FindNamedNetDriver(Actor->GetWorld(), NetDriverName));
	}
}

void FNetworkObjectList::MarkDormant(AActor* const Actor, UNetConnection* const Connection, const int32 NumConnections, UNetDriver* NetDriver)
{
	TSharedPtr<FNetworkObjectInfo>* NetworkObjectInfoPtr = FindOrAdd(Actor, NetDriver);

	if (NetworkObjectInfoPtr == nullptr)
	{
		return;		// Actor doesn't belong to this net driver name
	}

	FNetworkObjectInfo* NetworkObjectInfo = NetworkObjectInfoPtr->Get();

	// Add the connection to the list of dormant connections (if it's not already on the list)
	if (!NetworkObjectInfo->DormantConnections.Contains(Connection))
	{
		check(ActiveNetworkObjects.Contains(Actor));

		NetworkObjectInfo->DormantConnections.Add(Connection);

		// Keep track of the number of dormant objects on each connection
		int32& NumDormantObjectsPerConnectionRef = NumDormantObjectsPerConnection.FindOrAdd(Connection);
		NumDormantObjectsPerConnectionRef++;

		UE_LOG(LogNetDormancy, Log, TEXT("FNetworkObjectList::MarkDormant: Actor is now dormant. Actor: %s. NumDormant: %i, Connection: %s"), *Actor->GetName(), NumDormantObjectsPerConnectionRef, *Connection->GetName());
	}

	// Clean up DormantConnections list (remove possible GC'd connections)
	for (auto ConnectionIt = NetworkObjectInfo->DormantConnections.CreateIterator(); ConnectionIt; ++ConnectionIt)
	{
		if ((*ConnectionIt).Get() == nullptr || (*ConnectionIt).Get()->State == USOCK_Closed)
		{
			ConnectionIt.RemoveCurrent();
		}
	}

	// At this point, after removing null references, we should never be over the connection count
	check(NetworkObjectInfo->DormantConnections.Num() <= NumConnections);

	// If the number of dormant connections now matches the number of actual connections, we can remove this object from the active list
	if (NetworkObjectInfo->DormantConnections.Num() == NumConnections)
	{
		ObjectsDormantOnAllConnections.Add(*NetworkObjectInfoPtr);
		ActiveNetworkObjects.Remove(Actor);

		UE_LOG(LogNetDormancy, Log, TEXT("FNetworkObjectList::MarkDormant: Actor is now dormant on all connections. Actor: %s. Total: %i, Active: %i, Connection: %s"), *Actor->GetName(), AllNetworkObjects.Num(), ActiveNetworkObjects.Num(), *Connection->GetName());
	}

	check((ActiveNetworkObjects.Num() + ObjectsDormantOnAllConnections.Num()) == AllNetworkObjects.Num());
}

bool FNetworkObjectList::MarkActive(AActor* const Actor, UNetConnection* const Connection, const FName NetDriverName)
{
	if (Actor != nullptr)
	{
		return MarkActive(Actor, Connection, GEngine->FindNamedNetDriver(Actor->GetWorld(), NetDriverName));
	}
	else
	{
		return false;
	}
}

bool FNetworkObjectList::MarkActive(AActor* const Actor, UNetConnection* const Connection, UNetDriver* NetDriver)
{
	TSharedPtr<FNetworkObjectInfo>* NetworkObjectInfoPtr = FindOrAdd(Actor, NetDriver);

	if (NetworkObjectInfoPtr == nullptr)
	{
		return false;		// Actor doesn't belong to this net driver name
	}

	FNetworkObjectInfo* NetworkObjectInfo = NetworkObjectInfoPtr->Get();

	// Remove from the ObjectsDormantOnAllConnections if needed
	if (ObjectsDormantOnAllConnections.Remove(Actor) > 0)
	{
		// Put this object back on the active list
		ActiveNetworkObjects.Add(*NetworkObjectInfoPtr);

		UE_LOG(LogNetDormancy, Log, TEXT("FNetworkObjectList::MarkDormant: Actor is no longer dormant on all connections. Actor: %s. Total: %i, Active: %i, Connection: %s"), *Actor->GetName(), AllNetworkObjects.Num(), ActiveNetworkObjects.Num(), *Connection->GetName());
	}

	check((ActiveNetworkObjects.Num() + ObjectsDormantOnAllConnections.Num()) == AllNetworkObjects.Num());

	// Remove connection from the dormant connection list
	if (NetworkObjectInfo->DormantConnections.Remove(Connection) > 0)
	{
		// Add the connection to the list of recently dormant connections
		NetworkObjectInfo->RecentlyDormantConnections.Add( Connection );

		int32& NumDormantObjectsPerConnectionRef = NumDormantObjectsPerConnection.FindOrAdd(Connection);
		check(NumDormantObjectsPerConnectionRef > 0);
		NumDormantObjectsPerConnectionRef--;

		UE_LOG(LogNetDormancy, Log, TEXT("FNetworkObjectList::MarkActive: Actor is no longer dormant. Actor: %s. NumDormant: %i, Connection: %s"), *Actor->GetName(), NumDormantObjectsPerConnectionRef, *Connection->GetName());
		return true;
	}

	return false;
}

void FNetworkObjectList::ClearRecentlyDormantConnection(AActor* const Actor, UNetConnection* const Connection, const FName NetDriverName)
{
	if (Actor != nullptr)
	{
		ClearRecentlyDormantConnection(Actor, Connection, GEngine->FindNamedNetDriver(Actor->GetWorld(), NetDriverName));
	}
}

void FNetworkObjectList::ClearRecentlyDormantConnection(AActor* const Actor, UNetConnection* const Connection, UNetDriver* NetDriver)
{
	TSharedPtr<FNetworkObjectInfo>* NetworkObjectInfoPtr = FindOrAdd(Actor, NetDriver);

	if (NetworkObjectInfoPtr == nullptr)
	{
		return;		// Actor doesn't belong to this net driver name
	}

	FNetworkObjectInfo* NetworkObjectInfo = NetworkObjectInfoPtr->Get();

	NetworkObjectInfo->RecentlyDormantConnections.Remove(Connection);
}

void FNetworkObjectList::HandleConnectionAdded()
{
	// When a new connection is added, we must add all objects back to the active list so the new connection will process it
	// Once the objects is dormant on that connection, it will then be removed from the active list again
	for (auto It = ObjectsDormantOnAllConnections.CreateIterator(); It; ++It)
	{
		ActiveNetworkObjects.Add(*It);
	}

	ObjectsDormantOnAllConnections.Empty();
}

void FNetworkObjectList::ResetDormancyState()
{
	// Reset all state related to dormancy, and move all objects back on to the active list
	ObjectsDormantOnAllConnections.Empty();

	ActiveNetworkObjects = AllNetworkObjects;

	for (auto It = AllNetworkObjects.CreateIterator(); It; ++It)
	{
		FNetworkObjectInfo* NetworkObjectInfo = ( *It ).Get();

		NetworkObjectInfo->DormantConnections.Empty();
		NetworkObjectInfo->RecentlyDormantConnections.Empty();
	}

	NumDormantObjectsPerConnection.Empty();
}

int32 FNetworkObjectList::GetNumDormantActorsForConnection(UNetConnection* const Connection) const
{
	const int32 *Count = NumDormantObjectsPerConnection.Find( Connection );

	return (Count != nullptr) ? *Count : 0;
}

void FNetworkObjectList::ForceActorRelevantNextUpdate(AActor* const Actor, const FName NetDriverName)
{
	if (Actor)
	{
		ForceActorRelevantNextUpdate(Actor, GEngine->FindNamedNetDriver(Actor->GetWorld(), NetDriverName));
	}
}

void FNetworkObjectList::ForceActorRelevantNextUpdate(AActor* const Actor, UNetDriver* NetDriver)
{
	TSharedPtr<FNetworkObjectInfo>* NetworkObjectInfoPtr = FindOrAdd(Actor, NetDriver);

	if (NetworkObjectInfoPtr == nullptr)
	{
		return;		// Actor doesn't belong to this net driver name
	}

	FNetworkObjectInfo* NetworkObjectInfo = NetworkObjectInfoPtr->Get();

	NetworkObjectInfo->ForceRelevantFrame = NetDriver->ReplicationFrame + 1;
}

void FNetworkObjectList::Reset()
{
	// Reset all state
	AllNetworkObjects.Empty();
	ActiveNetworkObjects.Empty();
	ObjectsDormantOnAllConnections.Empty();
	NumDormantObjectsPerConnection.Empty();
}

void FNetworkObjectInfo::CountBytes(FArchive& Ar) const
{
	DormantConnections.CountBytes(Ar);
	RecentlyDormantConnections.CountBytes(Ar);
}

static void CountBytesForNetworkObjectSet(const FNetworkObjectList::FNetworkObjectSet& Set, FArchive& Ar)
{
	
}

void FNetworkObjectList::CountBytes(FArchive& Ar) const
{
	AllNetworkObjects.CountBytes(Ar);
	ActiveNetworkObjects.CountBytes(Ar);
	ObjectsDormantOnAllConnections.CountBytes(Ar);
	NumDormantObjectsPerConnection.CountBytes(Ar);
 
	// ObjectsDormantOnAllConnections and ActiveNetworkObjects are both sub sets of AllNetworkObjects
	// and only have pointers back to the data there.
	// So, to avoid double (or triple) counting, only explicit count the elements from AllNetworkObjects.
	for (const TSharedPtr<FNetworkObjectInfo>& SharedInfo : AllNetworkObjects)
	{
		if (FNetworkObjectInfo const * const Info = SharedInfo.Get())
		{
			Ar.CountBytes(sizeof(FNetworkObjectInfo), sizeof(FNetworkObjectInfo));
			Info->CountBytes(Ar);
		}
	}
}