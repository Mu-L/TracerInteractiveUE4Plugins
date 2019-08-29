// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BSDSockets/SocketSubsystemBSDPrivate.h"

class FInternetAddr;

#if PLATFORM_HAS_BSD_SOCKETS

#include "Sockets.h"


/**
 * Enumerates BSD socket state parameters.
 */
enum class ESocketBSDParam
{
	CanRead,
	CanWrite,
	HasError,
};


/**
 * Enumerates BSD socket state return values.
 */
enum class ESocketBSDReturn
{
	Yes,
	No,
	EncounteredError,
};


/**
 * Implements a BSD network socket.
 */
class FSocketBSD
	: public FSocket
{
public:

	/**
	 * Assigns a BSD socket to this object.
	 *
	 * @param InSocket the socket to assign to this object.
	 * @param InSocketType the type of socket that was created.
	 * @param InSocketDescription the debug description of the socket.
	 */
	FSocketBSD(SOCKET InSocket, ESocketType InSocketType, const FString& InSocketDescription, ISocketSubsystem * InSubsystem) 
		: FSocket(InSocketType, InSocketDescription)
		, Socket(InSocket)
		, LastActivityTime(0)
		, SocketSubsystem(InSubsystem)
	{ }

	/**
	 * Destructor.
	 *
	 * Closes the socket if it is still open
	 */
	virtual ~FSocketBSD()
	{
		Close();
	}

public:

	/**
	* Gets the Socket for anyone who knows they have an FSocketBSD.
	*
	* @return The native socket.
	*/
	SOCKET GetNativeSocket()
	{
		return Socket;
	}

public:

	// FSocket overrides

	virtual bool Close() override;
	virtual bool Bind(const FInternetAddr& Addr) override;
	virtual bool Connect(const FInternetAddr& Addr) override;
	virtual bool Listen(int32 MaxBacklog) override;
	virtual bool WaitForPendingConnection(bool& bHasPendingConnection, const FTimespan& WaitTime) override;
	virtual bool HasPendingData(uint32& PendingDataSize) override;
	virtual class FSocket* Accept(const FString& SocketDescription) override;
	virtual class FSocket* Accept(FInternetAddr& OutAddr, const FString& SocketDescription) override;
	virtual bool SendTo(const uint8* Data, int32 Count, int32& BytesSent, const FInternetAddr& Destination) override;
	virtual bool Send(const uint8* Data, int32 Count, int32& BytesSent) override;
	virtual bool RecvFrom(uint8* Data, int32 BufferSize, int32& BytesRead, FInternetAddr& Source, ESocketReceiveFlags::Type Flags = ESocketReceiveFlags::None) override;
	virtual bool Recv(uint8* Data,int32 BufferSize,int32& BytesRead, ESocketReceiveFlags::Type Flags = ESocketReceiveFlags::None) override;
	virtual bool Wait(ESocketWaitConditions::Type Condition, FTimespan WaitTime) override;
	virtual ESocketConnectionState GetConnectionState() override;
	virtual void GetAddress(FInternetAddr& OutAddr) override;
	virtual bool GetPeerAddress(FInternetAddr& OutAddr) override;
	virtual bool SetNonBlocking(bool bIsNonBlocking = true) override;
	virtual bool SetBroadcast(bool bAllowBroadcast = true) override;
	virtual bool JoinMulticastGroup(const FInternetAddr& GroupAddress) override;
	virtual bool LeaveMulticastGroup(const FInternetAddr& GroupAddress) override;
	virtual bool SetMulticastLoopback(bool bLoopback) override;
	virtual bool SetMulticastTtl(uint8 TimeToLive) override;
	virtual bool SetReuseAddr(bool bAllowReuse = true) override;
	virtual bool SetLinger(bool bShouldLinger = true, int32 Timeout = 0) override;
	virtual bool SetRecvErr(bool bUseErrorQueue = true) override;
	virtual bool SetSendBufferSize(int32 Size,int32& NewSize) override;
	virtual bool SetReceiveBufferSize(int32 Size,int32& NewSize) override;
	virtual int32 GetPortNo() override;

protected:

	/** This is generally select(), but makes it easier for platforms without select to replace it. */
	virtual ESocketBSDReturn HasState(ESocketBSDParam State, FTimespan WaitTime = FTimespan::Zero());

	/** Updates this socket's time of last activity. */
	void UpdateActivity()
	{
		LastActivityTime = FDateTime::UtcNow();
	}

	/** Holds the BSD socket object. */
	SOCKET Socket;

	/** Last activity time. */
	FDateTime LastActivityTime;

	/** Pointer to the subsystem that created it. */
	ISocketSubsystem* SocketSubsystem;
};


#endif	//PLATFORM_HAS_BSD_SOCKETS
