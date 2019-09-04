// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

//
// Ip based implementation of a network connection used by the net driver class
//

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/NetConnection.h"
#include "Async/TaskGraphInterfaces.h"
#include "SocketTypes.h"
#include "IpConnection.generated.h"

class FInternetAddr;
class ISocketSubsystem;

UCLASS(transient, config=Engine)
class ONLINESUBSYSTEMUTILS_API UIpConnection : public UNetConnection
{
    GENERATED_UCLASS_BODY()
	// Variables.

	class FSocket*				Socket;
	class FResolveInfo*			ResolveInfo;

	//~ Begin NetConnection Interface
	virtual void InitBase(UNetDriver* InDriver, class FSocket* InSocket, const FURL& InURL, EConnectionState InState, int32 InMaxPacket = 0, int32 InPacketOverhead = 0) override;
	virtual void InitRemoteConnection(UNetDriver* InDriver, class FSocket* InSocket, const FURL& InURL, const class FInternetAddr& InRemoteAddr, EConnectionState InState, int32 InMaxPacket = 0, int32 InPacketOverhead = 0) override;
	virtual void InitLocalConnection(UNetDriver* InDriver, class FSocket* InSocket, const FURL& InURL, EConnectionState InState, int32 InMaxPacket = 0, int32 InPacketOverhead = 0) override;
	virtual void LowLevelSend(void* Data, int32 CountBits, FOutPacketTraits& Traits) override;
	FString LowLevelGetRemoteAddress(bool bAppendPort=false) override;
	FString LowLevelDescribe() override;
	virtual void Tick() override;
	virtual void CleanUp() override;
	virtual void ReceivedRawPacket(void* Data, int32 Count) override;
	//~ End NetConnection Interface

	/**
	 * If CVarNetIpConnectionUseSendTasks is true, blocks until there are no outstanding send tasks.
	 * Since these tasks need to access the socket, this is called before the net driver closes the socket.
	 */
	void WaitForSendTasks();

private:
	/**
	 * Struct to hold the result of a socket SendTo call. If net.IpConnectionUseSendTasks is true,
	 * these are communicated back to the game thread via SocketSendResults.
	 */
	struct FSocketSendResult
	{
		FSocketSendResult()
			: BytesSent(0)
			, Error(SE_NO_ERROR)
		{
		}

		int32 BytesSent;
		ESocketErrors Error;
	};

	/** Critical section to protect SocketSendResults */
	FCriticalSection SocketSendResultsCriticalSection;

	/** Socket SendTo results from send tasks if net.IpConnectionUseSendTasks is true */
	TArray<FSocketSendResult> SocketSendResults;

	/**
	 * If net.IpConnectionUseSendTasks is true, reference to the last send task used as a prerequisite
	 * for the next send task. Also, CleanUp() blocks until this task is complete.
	 */
	FGraphEventRef LastSendTask;

	/** Instead of disconnecting immediately on a socket error, wait for some time to see if we can recover. Specified in Seconds. */
	UPROPERTY(Config)
	float SocketErrorDisconnectDelay;

	/** Cached time of the first send socket error that will be used to compute disconnect delay. */
	float SocketError_SendDelayStartTime;

	/** Cached time of the first recv socket error that will be used to compute disconnect delay. */
	float SocketError_RecvDelayStartTime;

	friend class FIpConnectionHelper;

	/** Handles any SendTo errors on the game thread. */
	void HandleSocketSendResult(const FSocketSendResult& Result, ISocketSubsystem* SocketSubsystem);

	/** Notifies us that we've encountered an error while receiving a packet. */
	void HandleSocketRecvError(class UNetDriver* NetDriver, const FString& ErrorString);
};
