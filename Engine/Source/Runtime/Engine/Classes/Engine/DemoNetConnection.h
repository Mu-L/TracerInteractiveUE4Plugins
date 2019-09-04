// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/NetConnection.h"
#include "DemoNetConnection.generated.h"

class APlayerController;
class FObjectReplicator;
class UDemoNetDriver;

struct FQueuedDemoPacket
{
	/** The packet data to send */
	TArray<uint8> Data;

	/** The size of the packet in bits */
	int32 SizeBits;

	/** The traits applied to the packet, if applicable */
	FOutPacketTraits Traits;

	/** Index of the level this packet is associated with. 0 indicates no association. */
	uint32 SeenLevelIndex;

public:
	FORCEINLINE FQueuedDemoPacket(uint8* InData, int32 InSizeBytes, int32 InSizeBits) 
		: Data()
		, SizeBits(InSizeBits)
		, Traits()
		, SeenLevelIndex(0)
	{
		Data.AddUninitialized(InSizeBytes);
		FMemory::Memcpy(Data.GetData(), InData, InSizeBytes);
	}

	FORCEINLINE FQueuedDemoPacket(uint8* InData, int32 InSizeBits, FOutPacketTraits& InTraits)
		: Data()
		, SizeBits(InSizeBits)
		, Traits(InTraits)
		, SeenLevelIndex(0)
	{
		int32 SizeBytes = FMath::DivideAndRoundUp(InSizeBits, 8);

		Data.AddUninitialized(SizeBytes);
		FMemory::Memcpy(Data.GetData(), InData, SizeBytes);
	}

	void CountBytes(FArchive& Ar) const
	{
		Data.CountBytes(Ar);
	}
};


/**
 * Simulated network connection for recording and playing back game sessions.
 */
UCLASS(transient, config=Engine)
class ENGINE_API UDemoNetConnection
	: public UNetConnection
{
	GENERATED_UCLASS_BODY()

public:

	// UNetConnection interface.

	virtual void InitConnection( class UNetDriver* InDriver, EConnectionState InState, const FURL& InURL, int32 InConnectionSpeed = 0, int32 InMaxPacket=0) override;
	virtual FString LowLevelGetRemoteAddress( bool bAppendPort = false ) override;
	virtual FString LowLevelDescribe() override;
	virtual void LowLevelSend(void* Data, int32 CountBits, FOutPacketTraits& Traits) override;
	virtual int32 IsNetReady( bool Saturate ) override;
	virtual void FlushNet( bool bIgnoreSimulation = false ) override;
	virtual void HandleClientPlayer( APlayerController* PC, class UNetConnection* NetConnection ) override;
	virtual TSharedPtr<const FInternetAddr> GetRemoteAddr() override { return nullptr; }
	virtual bool ClientHasInitializedLevelFor( const AActor* TestActor ) const override;
	virtual TSharedPtr<FObjectReplicator> CreateReplicatorForNewActorChannel(UObject* Object);
	virtual FString RemoteAddressToString() override { return TEXT("Demo"); }

	virtual void NotifyActorNetGUID(UActorChannel* Channel) override;

public:

	virtual void Serialize(FArchive& Ar) override;

	/** @return The DemoRecording driver object */
	FORCEINLINE class UDemoNetDriver* GetDriver()
	{
		return (UDemoNetDriver*)Driver;
	}

	/** @return The DemoRecording driver object */
	FORCEINLINE const class UDemoNetDriver* GetDriver() const
	{
		return (UDemoNetDriver*)Driver;
	}

	TArray<FQueuedDemoPacket> QueuedDemoPackets;
	TArray<FQueuedDemoPacket> QueuedCheckpointPackets;

	TMap<FNetworkGUID, UActorChannel*>& GetOpenChannelMap() { return OpenChannelMap; }

protected:
	virtual void DestroyIgnoredActor(AActor* Actor) override;

	UE_DEPRECATED(4.21, "Deprecated in favor of QueueNetStartupActorForRewind that does not check dormancy")
	void QueueInitialDormantStartupActorForRewind(AActor* Actor);

	void QueueNetStartupActorForRewind(AActor* Actor);

private:
	void TrackSendForProfiler(const void* Data, int32 NumBytes);

	// Not a weak object pointer, intended to exist only during checkpoint loading
	TMap<FNetworkGUID, UActorChannel*> OpenChannelMap;
};
