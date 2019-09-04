// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SocketSubsystem.h"
#include "BSDSockets/SocketSubsystemBSD.h"
#include "SocketSubsystemPackage.h"

/**
 * iOS specific socket subsystem implementation
 */
class FSocketSubsystemIOS : public FSocketSubsystemBSD
{
protected:
	virtual TSharedRef<FInternetAddr> CreateInternetAddr() override;
	/** Single instantiation of this subsystem */
	static FSocketSubsystemIOS* SocketSingleton;

	/** Whether Init() has been called before or not */
	bool bTriedToInit;

	// @todo ios: This is kind of hacky, since there's no UBT that should set PACKAGE_SCOPE
// PACKAGE_SCOPE:
public:

	/** 
	 * Singleton interface for this subsystem 
	 * @return the only instance of this subsystem
	 */
	static FSocketSubsystemIOS* Create();

	/**
	 * Performs iOS specific socket clean up
	 */
	static void Destroy();


	virtual FName GetDefaultSocketProtocolFamily() const override
	{
		return FNetworkProtocolTypes::IPv6;
	}

public:

	FSocketSubsystemIOS() 
		: bTriedToInit(false)
	{
	}

	virtual ~FSocketSubsystemIOS()
	{
	}

	virtual bool Init(FString& Error) override;
	virtual void Shutdown() override;
	virtual bool HasNetworkDevice() override;
	virtual FSocket* CreateSocket(const FName& SocketType, const FString& SocketDescription, const FName& ProtocolType) override;

	virtual TSharedRef<FInternetAddr> GetLocalHostAddr(FOutputDevice& Out, bool& bCanBindAll) override;
	virtual class FSocketBSD* InternalBSDSocketFactory(SOCKET Socket, ESocketType SocketType, const FString& SocketDescription, const FName& SocketProtocol) override;
};