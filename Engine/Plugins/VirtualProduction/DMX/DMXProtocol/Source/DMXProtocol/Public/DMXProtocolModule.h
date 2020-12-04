// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"
#include "Modules/ModuleInterface.h"

/**   */
class DMXPROTOCOL_API FDMXProtocolModule 
	: public IModuleInterface
{
public:
	void RegisterProtocol(const FName& FactoryName, IDMXProtocolFactory* Factory);

	void UnregisterProtocol(const FName& FactoryName);

	/**
	 * If protocol exists return the pointer otherwise it create a new protocol first and then return the pointer.
	 * @param  InProtocolName Name of the requested protocol
	 * @return Return the pointer to protocol.
	 */
	virtual IDMXProtocolPtr GetProtocol(const FName InProtocolName = NAME_None);
	
	/**  Get the reference to all protocol factories map */
	const TMap<FName, IDMXProtocolFactory*>& GetProtocolFactories() const;

	/**  Get the reference to all protocols map */
	const TMap<FName, IDMXProtocolPtr>& GetProtocols() const;

	//~ Begin IModuleInterface implementation
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface implementation

	/** Get the instance of this module. */
	static FDMXProtocolModule& Get();

private:
	void ShutdownDMXProtocol(const FName& ProtocolName);
	void ShutdownAllDMXProtocols();

public:
	static const TCHAR* BaseModuleName;
	static const FString LocalHostIpAddress;

private:
	TMap<FName, IDMXProtocolFactory*> DMXFactories;
	TMap<FName, IDMXProtocolPtr> DMXProtocols;
	TMap<FName, bool> DMXProtocolFailureNotes;
};
