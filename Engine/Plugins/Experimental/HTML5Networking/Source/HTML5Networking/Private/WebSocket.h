// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
//
// libwebsocket client wrapper.
//
#pragma  once
#include "HTML5NetworkingPrivate.h"
#if !PLATFORM_HTML5
#include "Runtime/Sockets/Private/BSDSockets/SocketSubsystemBSD.h"
#define USE_LIBWEBSOCKET 1
#else
#include <netinet/in.h>
#define USE_LIBWEBSOCKET 0
#endif

class FWebSocket
{

public:

	// Initialize as client side socket.
	FWebSocket(const FInternetAddr& ServerAddress);

#if USE_LIBWEBSOCKET
	// Initialize as server side socket.
	FWebSocket(WebSocketInternalContext* InContext, WebSocketInternal* Wsi);
#endif

	// clean up.
	~FWebSocket();

	/************************************************************************/
	/* Set various callbacks for Socket Events                              */
	/************************************************************************/
	void SetConnectedCallBack(FWebsocketInfoCallBack CallBack);
	void SetErrorCallBack(FWebsocketInfoCallBack CallBack);
	void SetRecieveCallBack(FWebsocketPacketRecievedCallBack CallBack);

	/** Send raw data to remote end point. */
	bool Send(uint8* Data, uint32 Size);

	/** service libwebsocket.			   */
	void Tick();
	/** service libwebsocket until outgoing buffer is empty */
	void Flush();

	/** Helper functions to describe end points. */
	TArray<uint8> GetRawRemoteAddr(int32& OutPort);
	FString RemoteEndPoint(bool bAppendPort);
	FString LocalEndPoint(bool bAppendPort);
	struct sockaddr_in* GetRemoteAddr() { return &RemoteAddr; }

// this was made public because of cross-platform build issues
public:

	void HandlePacket();
	void OnRawRecieve(void* Data, uint32 Size);
	void OnRawWebSocketWritable(WebSocketInternal* wsi);

	/************************************************************************/
	/*	Various Socket callbacks											*/
	/************************************************************************/
	FWebsocketPacketRecievedCallBack  RecievedCallBack;
	FWebsocketInfoCallBack ConnectedCallBack;
	FWebsocketInfoCallBack ErrorCallBack;

	/**  Recv and Send Buffers, serviced during the Tick */
	TArray<uint8> RecievedBuffer;
	TArray<TArray<uint8>> OutgoingBuffer;

#if USE_LIBWEBSOCKET
	/** libwebsocket internal context*/
	WebSocketInternalContext* Context;

	/** libwebsocket web socket */
	WebSocketInternal* Wsi;

	/** libwebsocket Protocols that can be serviced by this implemenation*/
	WebSocketInternalProtocol* Protocols;
#else // ! USE_LIBWEBSOCKET -- HTML5 uses BSD network API
	int SockFd;
#endif
	struct sockaddr_in RemoteAddr;

	/** Server side socket or client side*/
	bool IsServerSide;

	friend class FWebSocketServer;
};
