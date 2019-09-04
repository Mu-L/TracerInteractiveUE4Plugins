// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SocketSubsystemIOS.h"
#include "SocketSubsystemModule.h"
#include "Modules/ModuleManager.h"
#include "SocketsBSDIOS.h"
#include "IPAddressBSDIOS.h"
#include <net/if.h>
#include <ifaddrs.h>

FSocketSubsystemIOS* FSocketSubsystemIOS::SocketSingleton = NULL;

class FSocketBSD* FSocketSubsystemIOS::InternalBSDSocketFactory(SOCKET Socket, ESocketType SocketType, const FString& SocketDescription, const FName& SocketProtocol)
{
	UE_LOG(LogIOS, Log, TEXT(" FSocketSubsystemIOS::InternalBSDSocketFactory"));
	return new FSocketBSDIOS(Socket, SocketType, SocketDescription, SocketProtocol, this);
}

FName CreateSocketSubsystem( FSocketSubsystemModule& SocketSubsystemModule )
{
	FName SubsystemName(TEXT("IOS"));
	// Create and register our singleton factor with the main online subsystem for easy access
	FSocketSubsystemIOS* SocketSubsystem = FSocketSubsystemIOS::Create();
	FString Error;
	if (SocketSubsystem->Init(Error))
	{
		SocketSubsystemModule.RegisterSocketSubsystem(SubsystemName, SocketSubsystem);
		return SubsystemName;
	}
	else
	{
		FSocketSubsystemIOS::Destroy();
		return NAME_None;
	}
}

void DestroySocketSubsystem( FSocketSubsystemModule& SocketSubsystemModule )
{
	SocketSubsystemModule.UnregisterSocketSubsystem(FName(TEXT("IOS")));
	FSocketSubsystemIOS::Destroy();
}

FSocketSubsystemIOS* FSocketSubsystemIOS::Create()
{
	if (SocketSingleton == NULL)
	{
		SocketSingleton = new FSocketSubsystemIOS();
	}

	return SocketSingleton;
}

void FSocketSubsystemIOS::Destroy()
{
	if (SocketSingleton != NULL)
	{
		SocketSingleton->Shutdown();
		delete SocketSingleton;
		SocketSingleton = NULL;
	}
}

bool FSocketSubsystemIOS::Init(FString& Error)
{
	return true;
}

void FSocketSubsystemIOS::Shutdown(void)
{
}


bool FSocketSubsystemIOS::HasNetworkDevice()
{
	return true;
}

FSocket* FSocketSubsystemIOS::CreateSocket(const FName& SocketType, const FString& SocketDescription, const FName& ProtocolType)
{
	FSocketBSD* NewSocket = (FSocketBSD*)FSocketSubsystemBSD::CreateSocket(SocketType, SocketDescription, ProtocolType);
	if (NewSocket)
	{
		if (ProtocolType != FNetworkProtocolTypes::IPv4)
		{
			NewSocket->SetIPv6Only(false);
		}

		// disable the SIGPIPE exception 
		int bAllow = 1;
		setsockopt(NewSocket->GetNativeSocket(), SOL_SOCKET, SO_NOSIGPIPE, &bAllow, sizeof(bAllow));
	}
	return NewSocket;
}

TSharedRef<FInternetAddr> FSocketSubsystemIOS::GetLocalHostAddr(FOutputDevice& Out, bool& bCanBindAll)
{
	TSharedRef<FInternetAddrBSDIOS> HostAddr = MakeShareable(new FInternetAddrBSDIOS(this));
	HostAddr->SetAnyAddress();

	ifaddrs* Interfaces = NULL;
	bool bWasWifiSet = false;
	bool bWasCellSet = false;
	bool bWasIPv6Set = false;

	// get all of the addresses
	if (getifaddrs(&Interfaces) == 0)
	{
		// Loop through linked list of interfaces
		for (ifaddrs* Travel = Interfaces; Travel != NULL; Travel = Travel->ifa_next)
		{
			if (Travel->ifa_addr == NULL)
			{
				continue;
			}

			sockaddr_storage* AddrData = reinterpret_cast<sockaddr_storage*>(Travel->ifa_addr);
			uint32 ScopeInterfaceId = ntohl(if_nametoindex(Travel->ifa_name));
			if (Travel->ifa_addr->sa_family == AF_INET6)
			{
				if (strcmp(Travel->ifa_name, "en0") == 0)
				{
					HostAddr->SetIp(*AddrData);
					HostAddr->SetScopeId(ScopeInterfaceId);
					bWasWifiSet = true;
					bWasIPv6Set = true;
					UE_LOG(LogSockets, Verbose, TEXT("Set IP to WIFI %s"), *HostAddr->ToString(false));
				}
				else if (!bWasWifiSet && strcmp(Travel->ifa_name, "pdp_ip0") == 0)
				{
					HostAddr->SetIp(*AddrData);
					HostAddr->SetScopeId(ScopeInterfaceId);
					bWasCellSet = true;
					UE_LOG(LogSockets, Verbose, TEXT("Set IP to CELL %s"), *HostAddr->ToString(false));
				}
			}
			else if (!bWasIPv6Set && Travel->ifa_addr->sa_family == AF_INET)
			{
				if (strcmp(Travel->ifa_name, "en0") == 0)
				{
					HostAddr->SetIp(*AddrData);
					HostAddr->SetScopeId(ScopeInterfaceId);
					bWasWifiSet = true;
					UE_LOG(LogSockets, Verbose, TEXT("Set IP to WIFI IPv4 %s"), *HostAddr->ToString(false));
				}
				else if (!bWasWifiSet && strcmp(Travel->ifa_name, "pdp_ip0") == 0)
				{
					HostAddr->SetIp(*AddrData);
					HostAddr->SetScopeId(ScopeInterfaceId);
					bWasCellSet = true;
					UE_LOG(LogSockets, Verbose, TEXT("Set IP to CELL IPv4 %s"), *HostAddr->ToString(false));
				}
			}
		}

		// Free memory
		freeifaddrs(Interfaces);

		if (bWasWifiSet)
		{
			UE_LOG(LogIOS, Log, TEXT("Host addr is WIFI: %s"), *HostAddr->ToString(false));
		}
		else if (bWasCellSet)
		{
			UE_LOG(LogIOS, Log, TEXT("Host addr is CELL: %s"), *HostAddr->ToString(false));
		}
		else
		{
			UE_LOG(LogIOS, Log, TEXT("Host addr is INVALID"));
		}
	}

	// return the newly created address
	bCanBindAll = true;
	return HostAddr;
}

TSharedRef<FInternetAddr> FSocketSubsystemIOS::CreateInternetAddr()
{
	return MakeShareable(new FInternetAddrBSDIOS(this));
}
