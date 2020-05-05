// Copyright Epic Games, Inc. All Rights Reserved.

#include "HttpListener.h"
#include "HttpConnection.h"
#include "HttpRequestHandler.h"
#include "HttpRouter.h"
#include "HttpServerConfig.h"

#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

DEFINE_LOG_CATEGORY(LogHttpListener)

FHttpListener::FHttpListener(uint32 InListenPort)
{ 
	check(InListenPort > 0);
	ListenPort = InListenPort;
	Router = MakeShared<FHttpRouter>();
}

FHttpListener::~FHttpListener() 
{ 
	check(nullptr == ListenSocket);
	check(!bIsListening);

	const bool bRequestGracefulExit = false;
	for (const auto& Connection : Connections)
	{
		Connection->RequestDestroy(bRequestGracefulExit);
	}
	Connections.Empty();
}

// --------------------------------------------------------------------------------------------
// Public Interface
// --------------------------------------------------------------------------------------------
bool FHttpListener::StartListening()
{
	check(nullptr == ListenSocket);
	check(!bIsListening);
	bIsListening = true;

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (nullptr == SocketSubsystem)
	{
		UE_LOG(LogHttpListener, Error, 
			TEXT("HttpListener - SocketSubsystem Initialization Failed"));
		return false;
	}

	ListenSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("HttpListenerSocket"));
	if (nullptr == ListenSocket)
	{
		UE_LOG(LogHttpListener, Error, 
			TEXT("HttpListener - Unable to allocate stream socket"));
		return false;
	}
	ListenSocket->SetNonBlocking(true);

	// Bind to config-driven address
	TSharedRef<FInternetAddr> BindAddress = SocketSubsystem->CreateInternetAddr();
	Config = FHttpServerConfig::GetListenerConfig(ListenPort);
	if (0 == Config.BindAddress.Compare(TEXT("any"), ESearchCase::IgnoreCase))
	{
		BindAddress->SetAnyAddress();
	}
	else
	{
		bool bIsValidAddress = false;
		BindAddress->SetIp(*(Config.BindAddress), bIsValidAddress);
		if (!bIsValidAddress)
		{
			UE_LOG(LogHttpListener, Error,
				TEXT("HttpListener detected invalid bind address (%s:%u)"),
				*Config.BindAddress, ListenPort);
			return false;
		}
	}

	BindAddress->SetPort(ListenPort);
	if (!ListenSocket->Bind(*BindAddress))
	{
		UE_LOG(LogHttpListener, Error, 
			TEXT("HttpListener unable to bind to %s:%u"),
			*BindAddress->ToString(true), ListenPort);
		return false;
	}

	int32 ActualBufferSize;
	ListenSocket->SetSendBufferSize(Config.BufferSize, ActualBufferSize);
	if (ActualBufferSize != Config.BufferSize)
	{
		UE_LOG(LogHttpListener, Warning, 
			TEXT("HttpListener unable to set desired buffer size (%d): Limited to %d"),
			Config.BufferSize, ActualBufferSize);
	}

	if (!ListenSocket->Listen(Config.ConnectionsBacklogSize))
	{
		UE_LOG(LogHttpListener, Error, 
			TEXT("HttpListener unable to listen on socket"));
		return false;
	}

	UE_LOG(LogHttpListener, Log, 
		TEXT("Created new HttpListener on %s:%u"), 
		*BindAddress->ToString(true), ListenPort);
	return true;
}

void FHttpListener::StopListening()
{
	check(bIsListening);

	// Tear down our top-level listener first
	if (ListenSocket)
	{
		UE_LOG(LogHttpListener, Log,
			TEXT("HttListener stopping listening on Port %u"), ListenPort);

		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (SocketSubsystem)
		{
			SocketSubsystem->DestroySocket(ListenSocket);
		}
		ListenSocket = nullptr;
	}
	bIsListening = false;

	const bool bRequestGracefulExit = true;
	for (const auto& Connection : Connections)
	{
		Connection->RequestDestroy(bRequestGracefulExit);
	}
}

void FHttpListener::Tick(float DeltaTime)
{
	// Accept new connections
	AcceptConnections();

	// Tick Connections
	TickConnections(DeltaTime);

	// Remove any destroyed connections
	RemoveDestroyedConnections();
}

bool FHttpListener::HasPendingConnections() const 
{
	for (const auto& Connection : Connections)
	{
		switch (Connection->GetState())
		{
		case EHttpConnectionState::Reading:
		case EHttpConnectionState::AwaitingProcessing:
		case EHttpConnectionState::Writing:
			return true;
		}
	}
	return false;
}

// --------------------------------------------------------------------------------------------
// Private Implementation
// --------------------------------------------------------------------------------------------
void FHttpListener::AcceptConnections()
{
	check(ListenSocket);

	for (int32 i = 0; i < Config.MaxConnectionsAcceptPerFrame; ++i)
	{
		// Check pending prior to Accept()ing
		bool bHasPendingConnection = false;
		if (!ListenSocket->HasPendingConnection(bHasPendingConnection))
		{
			UE_LOG(LogHttpListener, 
				Error, TEXT("ListenSocket failed to query pending connection"));
			return;
		}

		if (bHasPendingConnection)
		{
			FSocket* IncomingConnection = ListenSocket->Accept(TEXT("HttpRequest"));

			if (nullptr == IncomingConnection)
			{
				ESocketErrors ErrorCode = ESocketErrors::SE_NO_ERROR;
				FString ErrorStr = TEXT("SocketSubsystem Unavialble");

				ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
				if (SocketSubsystem)
				{
					ErrorCode = SocketSubsystem->GetLastErrorCode();
					ErrorStr = SocketSubsystem->GetSocketError();
				}
				UE_LOG(LogHttpListener, Error,
					TEXT("Error accepting expected connection [%d] %s"), (int32)ErrorCode, *ErrorStr);
				return;
			}

			IncomingConnection->SetNonBlocking(true);
			TSharedPtr<FHttpConnection> Connection = 
				MakeShared<FHttpConnection>(IncomingConnection, Router, ListenPort, NumConnectionsAccepted++);
			Connections.Add(Connection);
		}
	}
}

void FHttpListener::TickConnections(float DeltaTime)
{
	for (const auto& Connection : Connections)
	{
		check(Connection.IsValid());

		switch (Connection->GetState())
		{
		case EHttpConnectionState::AwaitingRead:
		case EHttpConnectionState::Reading:
			Connection->Tick(DeltaTime);
			break;
		}
	}

	for (const auto& Connection : Connections)
	{
		check(Connection.IsValid());

		switch (Connection->GetState())
		{
		case EHttpConnectionState::Writing:
			Connection->Tick(DeltaTime);
			break;
		}
	}
}

void FHttpListener::RemoveDestroyedConnections()
{
	for (auto ConnectionsIter = Connections.CreateIterator(); ConnectionsIter; ++ConnectionsIter)
	{
		// Remove any destroyed connections
		if (EHttpConnectionState::Destroyed == ConnectionsIter->Get()->GetState())
		{
			ConnectionsIter.RemoveCurrent();
			continue;
		}
	}
}