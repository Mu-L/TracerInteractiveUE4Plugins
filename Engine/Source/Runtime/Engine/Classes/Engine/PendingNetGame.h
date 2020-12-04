// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "Engine/EngineBaseTypes.h"
#include "NetworkDelegates.h"
#include "PendingNetGame.generated.h"

class UEngine;
class UNetConnection;
class UNetDriver;
struct FWorldContext;

UCLASS(customConstructor, transient)
class UPendingNetGame :
	public UObject,
	public FNetworkNotify
{
	GENERATED_BODY()

public:

	/** 
	 * Net driver created for contacting the new server
	 * Transferred to world on successful connection
	 */
	UPROPERTY()
	class UNetDriver*		NetDriver;

	/** 
	 * Demo Net driver created for loading demos, but we need to go through pending net game
	 * Transferred to world on successful connection
	 */
	UE_DEPRECATED(4.26, "DemoNetDriver will be made private in a future release.  Please use GetDemoNetDriver/SetDemoNetDriver instead.")
	UPROPERTY()
	class UDemoNetDriver*	DemoNetDriver;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	/** Gets the demo net driver for this pending world. */
	UDemoNetDriver* GetDemoNetDriver() const { return DemoNetDriver; }

	/** Sets the demo net driver for this pending world. */
	void SetDemoNetDriver(UDemoNetDriver* const InDemoNetDriver) { DemoNetDriver = InDemoNetDriver; }
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	/**
	 * Setup the connection for encryption with a given key
	 * All future packets are expected to be encrypted
	 *
	 * @param Response response from the game containing its encryption key or an error message
	 * @param WeakConnection the connection related to the encryption request
	 */
	void FinalizeEncryptedConnection(const FEncryptionKeyResponse& Response, TWeakObjectPtr<UNetConnection> WeakConnection);

	/**
	 * Set the encryption key for the connection. This doesn't cause outgoing packets to be encrypted,
	 * but it allows the connection to decrypt any incoming packets if needed.
	 *
	 * @param Response response from the game containing its encryption key or an error message
	 */
	ENGINE_API void SetEncryptionKey(const FEncryptionKeyResponse& Response);

public:
	/** URL associated with this level. */
	FURL					URL;

	/** @todo document */
	bool					bSuccessfullyConnected;

	/** @todo document */
	bool					bSentJoinRequest;

	/** @todo document */
	FString					ConnectionError;

	// Constructor.
	void Initialize(const FURL& InURL);

	// Constructor.
	UPendingNetGame(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	void	InitNetDriver();

	/**
	 * Send the packet for triggering the initial join
	 */
	ENGINE_API void SendInitialJoin();

	//~ Begin FNetworkNotify Interface.
	virtual EAcceptConnection::Type NotifyAcceptingConnection() override;
	virtual void NotifyAcceptedConnection( class UNetConnection* Connection ) override;
	virtual bool NotifyAcceptingChannel( class UChannel* Channel ) override;
	virtual void NotifyControlMessage(UNetConnection* Connection, uint8 MessageType, class FInBunch& Bunch) override;
	//~ End FNetworkNotify Interface.

	/**  Update the pending level's status. */
	virtual void Tick( float DeltaTime );

	/** @todo document */
	virtual UNetDriver* GetNetDriver() { return NetDriver; }

	/** Send JOIN to other end */
	virtual void SendJoin();

	//~ Begin UObject Interface.
	virtual void Serialize( FArchive& Ar ) override;

	virtual void FinishDestroy() override
	{
		NetDriver = NULL;
		
		Super::FinishDestroy();
	}
	
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	//~ End UObject Interface.
	

	/** Create the peer net driver and a socket to listen for new client peer connections. */
	void InitPeerListen();

	/** Called by the engine after it calls LoadMap for this PendingNetGame. */
	virtual void LoadMapCompleted(UEngine* Engine, FWorldContext& Context, bool bLoadedMapSuccessfully, const FString& LoadMapError);
};
