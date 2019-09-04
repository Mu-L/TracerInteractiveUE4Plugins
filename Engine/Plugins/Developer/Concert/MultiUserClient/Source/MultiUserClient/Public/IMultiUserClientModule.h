// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class IConcertSyncClient;

/**
 * Interface for the Multi-User module.
 */
class IMultiUserClientModule : public IModuleInterface
{
public:
	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline IMultiUserClientModule& Get()
	{
		static const FName ModuleName = "MultiUserClient";
		return FModuleManager::LoadModuleChecked<IMultiUserClientModule>(ModuleName);
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() during shutdown if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		static const FName ModuleName = "MultiUserClient";
		return FModuleManager::Get().IsModuleLoaded(ModuleName);
	}

	/**
	 * Get the sync client that will performing the MultiUser role
	 * @param InRole The role to create
	 * @return The client
	 */
	virtual TSharedPtr<IConcertSyncClient> GetClient() const = 0;

	/**
	 * Invokes the Multi-User browser tab
	 */
	virtual void OpenBrowser() = 0;

	/**
	 * Hot-links to Concert Settings.
	 */
	virtual void OpenSettings() = 0;

	/**
	 * Connect to the default connection setup
	 */
	virtual void DefaultConnect() = 0;

	/**
	 * Disconnect from the current session if any,
	 * but prompt the about session changes first
	 */
	virtual void DisconnectSession() = 0;

	/**
	 * Launches a server (if none are running) on the local machine. On success, the server is launched. On
	 * failure, an asynchronous notification (banner) is displayed to the user.
	 */
	virtual void LaunchConcertServer() = 0;

	/**
	 * @return true if the Concert server is running on the local machine.
	 */
	virtual bool IsConcertServerRunning() = 0;
};
